// SPDX-License-Identifier: LGPL-2.1-or-later

/// A renderer that publishes and never draws (docs/HeadlessServe.md
/// §3.1, stage 2b). The point of the mode is that a serving process
/// needs no GPU and no display at all, so these tests run in a plain
/// process — no QApplication, no X server, no GL widget — and the last
/// one asserts that staying that way is not an accident: no graphics
/// driver may be mapped into the process that just published a scene.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// _WIN32_WINNT before Asio, on Windows: Asio warns when the target
// version is unset and then assumes 0x0601 (Windows 7), which compiles
// this one translation unit against a Windows 7 API surface while every
// other unit in the same binary gets the SDK default. 0x0A00 is that
// default. Same reasoning as SceneServerWire.cpp.
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00
#endif
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <fstream>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
// psapi.h after windows.h: it needs the handle types.
#include <psapi.h>
#endif

#include <QColor>

#include <Gui/Renderer/Renderer.h>
#include <Gui/Renderer/SceneDump.h>
#include <Gui/Renderer/SceneServer.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace
{

/// `setenv` is POSIX and absent from the MSVC CRT, so the one call site
/// below goes through this instead (the shape committed in f7939faad8).
void setEnv(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

/// A minimal but complete draw: one triangle with its own arrays, of
/// the shape RendererBridge::translate() would have produced.
std::shared_ptr<Render::MeshData> makeMesh()
{
    struct Owned: Render::MeshData
    {
        std::vector<float> pos;
        std::vector<int32_t> tris;
    };
    auto m = std::make_shared<Owned>();
    m->cacheId = 4242;
    m->numVertices = 3;
    m->pos = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    m->tris = {0, 1, 2};
    m->positions = m->pos.data();
    m->triangleIndices = m->tris.data();
    m->numTriangleIndices = int(m->tris.size());
    return m;
}

Render::DrawCallList makeScene()
{
    Render::DrawCall d;
    d.objectKey = 1;
    d.mesh = makeMesh();
    d.material.diffuse = 0xff8040ffu;
    d.material.type = 0;
    d.bboxMin[0] = d.bboxMin[1] = d.bboxMin[2] = 0.f;
    d.bboxMax[0] = d.bboxMax[1] = 1.f;
    d.bboxMax[2] = 0.f;
    Render::DrawCallList scene;
    scene.push_back(std::move(d));
    return scene;
}

/// Identity view, identity-ish projection: the publish only has to
/// carry them, and a joining viewer re-frames anyway.
const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                             0, 0, 1, 0, 0, 0, 0, 1};

/// A port nothing is listening on, found by letting the kernel pick one
/// and handing it straight back. Racy in principle; in a test process
/// that binds it again microseconds later, good enough, and far better
/// than a hard-coded number colliding with the user's serving rig.
int freePort()
{
    net::io_context ioc;
    tcp::acceptor a(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
    return a.local_endpoint().port();
}

struct HttpReply
{
    bool ok = false;
    unsigned status = 0;
    std::string body;
};

/// One HTTP GET against the running scene server, answered with the
/// version-prefixed payload (SceneServer.h, the polling fallback).
/// Not ok on any failure -- including "nothing is listening yet", which
/// is why the caller retries rather than this.  Beast parses the reply,
/// so the status is the status and the body is the body; the hand-rolled
/// version this replaced searched the raw response text for "200" and
/// split it on the header terminator itself.
HttpReply httpGet(int port, const std::string& path)
{
    HttpReply out;
    net::io_context ioc;
    tcp::socket sock(ioc);
    beast::error_code ec;
    sock.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), uint16_t(port)), ec);
    if (ec) {
        return out;
    }
    http::request<http::string_body> req {http::verb::get, path, 11};
    req.set(http::field::host, "localhost");
    req.set(http::field::connection, "close");
    http::write(sock, req, ec);
    if (ec) {
        return out;
    }
    beast::flat_buffer buf;
    http::response<http::string_body> res;
    http::read(sock, buf, res, ec);
    if (ec) {
        return out;
    }
    out.ok = true;
    out.status = res.result_int();
    out.body = res.body();
    return out;
}

/// Shared across the cases in this file: the server is a singleton and
/// the serve port is read once, so the whole file publishes to one
/// server on one port.
int servePort()
{
    static const int port = [] {
        const int p = freePort();
        setEnv("FC_BGFX_SERVE_SCENE", std::to_string(p).c_str());
        return p;
    }();
    return port;
}

std::unique_ptr<Render::Renderer> makePublisher()
{
    return Render::RendererFactory::create("bgfx - OpenGL", nullptr, true);
}

/// Every image this process has mapped, as the platform reports it.
/// An empty list means the platform does not say -- which is a skip,
/// not a pass.
std::vector<std::string> mappedImages()
{
    std::vector<std::string> images;
#if defined(__APPLE__)
    // dyld's own list, which is what a Mach-O process has instead of a
    // maps file: everything the loader mapped, link-time dependency and
    // dlopen alike, in load order.
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        if (const char* name = _dyld_get_image_name(i)) {
            images.emplace_back(name);
        }
    }
#elif defined(_WIN32)
    // The Win32 answer to /proc/self/maps: every module the loader has
    // mapped, link-time import and LoadLibrary alike. EnumProcessModules
    // wants the buffer sized up front and reports what it would have
    // needed, so ask once, grow, ask again.
    const HANDLE process = ::GetCurrentProcess();
    // Deliberately smaller than any real process needs -- 180 modules
    // were measured in this test on Windows, and even a bare console
    // process maps dozens -- so the grow-and-retry below runs every time
    // instead of being a branch nothing ever takes. One extra
    // EnumProcessModules call buys a continuously exercised path.
    std::vector<HMODULE> mods(32);
    // How many the call actually WROTE, which is not the same as how
    // many it wants: the count it reports back can exceed the buffer,
    // and the entries past the end were never filled in. Walking those
    // would hand GetModuleFileNameEx a null module, and a null module
    // is not an error there -- it names the executable, so the list
    // would gain a phantom copy of ourselves for every unfilled slot.
    size_t written = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        DWORD needed = 0;
        if (!::EnumProcessModules(process,
                                  mods.data(),
                                  DWORD(mods.size() * sizeof(HMODULE)),
                                  &needed)) {
            return images;
        }
        const size_t count = needed / sizeof(HMODULE);
        written = count < mods.size() ? count : mods.size();
        if (count <= mods.size()) {
            break;
        }
        mods.resize(count);  // one retry, at the size it asked for
    }
    for (size_t i = 0; i < written; ++i) {
        char path[MAX_PATH] = {};
        if (::GetModuleFileNameExA(process, mods[i], path, DWORD(sizeof(path)))) {
            images.emplace_back(path);
        }
    }
#else
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        // The path is the last field, and only file-backed mappings
        // have one; anonymous mappings and [heap]/[stack] do not.
        const auto sp = line.rfind(' ');
        if (sp != std::string::npos && sp + 1 < line.size()
            && line[sp + 1] == '/') {
            images.push_back(line.substr(sp + 1));
        }
    }
#endif
    return images;
}

}  // namespace

TEST(PublishOnly, constructsWithoutAWidget)
{
    auto r = makePublisher();
    ASSERT_TRUE(r) << "the bgfx backend must offer a publish-only mode";
    EXPECT_EQ(r->type(), "bgfx - OpenGL");
}

TEST(PublishOnly, refusesToRender)
{
    auto r = makePublisher();
    ASSERT_TRUE(r);
    r->setScene(makeScene());
    // Not "it draws nothing": it must not even try, because trying is
    // what would create the graphics device this mode exists to avoid.
    EXPECT_FALSE(r->render(QColor(0, 0, 0), kIdentity, kIdentity));
}

TEST(PublishOnly, rejectsAFrameItCannotDescribe)
{
    auto r = makePublisher();
    ASSERT_TRUE(r);
    r->setScene(makeScene());
    EXPECT_FALSE(r->publish(QColor(), nullptr, kIdentity, 800, 600));
    EXPECT_FALSE(r->publish(QColor(), kIdentity, kIdentity, 0, 600));
}

TEST(PublishOnly, putsARealSceneOnTheWire)
{
    const int port = servePort();
    ASSERT_GT(port, 0);

    auto r = makePublisher();
    ASSERT_TRUE(r);
    r->setScene(makeScene());

    ASSERT_TRUE(r->publish(QColor(32, 32, 32), kIdentity, kIdentity,
                           1280, 720));
    ASSERT_TRUE(Render::SceneStreamServer::instance().running())
        << "the publish is what starts the server";

    // Give the accept loop its thread.
    HttpReply reply;
    for (int i = 0; i < 200 && !reply.ok; ++i) {
        reply = httpGet(port, "/scene?v=0");
        if (!reply.ok) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ASSERT_TRUE(reply.ok) << "no answer from the scene server";
    EXPECT_EQ(reply.status, 200u) << "a viewer that has seen nothing must get a payload, not 204";

    const std::string& body = reply.body;
    // 8-byte version prefix, then the payload the serializer wrote.
    ASSERT_GT(body.size(), 8u) << "a payload of nothing is not a scene";

    // And it deserializes: what the live stream publishes is the
    // manifest root (docs/SceneStreaming.md §4), so the geometry itself
    // is out of band and resolving it needs the blob layer — which
    // SceneDump's own tests cover, and a real viewer covers end to end.
    // What belongs here is that a publish-only process wrote a
    // well-formed root that names the object and carries the frame it
    // was given.
    Render::SceneSnapshot back;
    ASSERT_TRUE(Render::loadSceneSnapshot(
            reinterpret_cast<const uint8_t*>(body.data()) + 8,
            body.size() - 8, back));
    EXPECT_FALSE(back.objectUpdates.empty())
        << "the root must name the object that was fed in";
    EXPECT_EQ(back.width, 1280);
    EXPECT_EQ(back.height, 720);
}

TEST(PublishOnly, noGraphicsDeviceIsCreated)
{
    // Runs after the publish above (gtest runs a file's tests in
    // declaration order), so the process has done the whole job by now.
    //
    // What is checked is the *driver*, not the client-side API library,
    // because only the driver is evidence of a device. On Linux libGL,
    // libEGL, libGLX and libX11 are DT_NEEDED of FreeCADRenderer itself
    // -- the loader maps them whatever this process intends, and under
    // libglvnd they are dispatch stubs that have not chosen a vendor.
    // The vendor library and the DRI/software rasterizer behind it are
    // dlopened when a context is created, and nowhere else.
    //
    // macOS draws the same line, but a great deal further in: the
    // renderer names OpenGL.framework and libbgfx.dylib, Qt6OpenGL and
    // QuartzCore come with them, and dyld duly maps the whole
    // client-side stack at launch -- OpenGL and its libGL/libGLU/
    // libGFXShared/libCoreVMClient, Metal, all of
    // MetalPerformanceShaders, MetalTools, GPUCompiler, GPUWrangler,
    // IOAccelerator, IOSurface, CoreImage, CoreVideo. None of that is a
    // device; it is what a Mach-O process links against. What only a
    // real device brings in is the renderer plugin behind those
    // frameworks: GLEngine, the vendor's *GLDriver/*MTLDriver bundle
    // under /System/Library/Extensions, AppleGVA.
    //
    // So the absence of those is exactly the claim of
    // docs/HeadlessServe.md section 3.1: this process published a scene
    // without ever bringing up a graphics device.
    const std::vector<std::string> images = mappedImages();
    if (images.empty()) {
        GTEST_SKIP() << "this platform does not report its mapped images";
    }
    static const char* forbidden[] = {
#if defined(__APPLE__)
        "GLEngine", "GLDriver", "MTLDriver", "AppleGVA",
        "/System/Library/Extensions/",
#elif defined(_WIN32)
        // Windows draws the same line in the same place. opengl32.dll,
        // gdi32.dll and dxgi.dll are the client side -- they map into
        // processes that never draw, so naming them here would fail the
        // test on a machine that did nothing wrong. What only a real
        // device brings in is what sits BEHIND them: the OpenGL ICD the
        // loader picks per vendor, the Direct3D user-mode driver, and
        // WARP when there is no hardware to pick.
        //
        // the OpenGL ICDs, one name per vendor:
        "nvoglv",
        "atioglxx",
        "atig",
        "icd",
        // the Direct3D user-mode drivers:
        "nvwgf2um",
        "amdxc",
        "igd10iumd",
        "igd12umd",
        // and WARP, the software device, plus the Vulkan loader:
        "d3d10warp",
        "vulkan-1",
#else
        "_dri.so", "swrast", "llvmpipe", "libvulkan", "libnvidia-gl",
        "libGLX_", "libEGL_",
#endif
    };
    std::vector<std::string> found;
    for (const std::string& image : images) {
        for (const char* f : forbidden) {
            if (image.find(f) != std::string::npos) {
                found.push_back(image);
            }
        }
    }
    EXPECT_TRUE(found.empty())
        << "a publish-only process brought up a graphics device: "
        << (found.empty() ? std::string() : found.front());
}
