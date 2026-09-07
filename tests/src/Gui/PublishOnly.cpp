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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <QColor>

#include <Gui/Renderer/Renderer.h>
#include <Gui/Renderer/SceneDump.h>
#include <Gui/Renderer/SceneServer.h>

namespace
{

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
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof(addr);
    int port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), len) == 0
        && ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        port = ntohs(addr.sin_port);
    }
    ::close(fd);
    return port;
}

/// One HTTP GET against the running scene server, answered with the
/// version-prefixed payload (SceneServer.h, the polling fallback).
/// Returns the body, or empty on any failure.
std::string httpGet(int port, const std::string& path)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return {};
    }
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(uint16_t(port));
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return {};
    }
    const std::string req = "GET " + path
        + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (::write(fd, req.data(), req.size()) != ssize_t(req.size())) {
        ::close(fd);
        return {};
    }
    std::string resp;
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        resp.append(buf, size_t(n));
    }
    ::close(fd);
    return resp;
}

/// Shared across the cases in this file: the server is a singleton and
/// the serve port is read once, so the whole file publishes to one
/// server on one port.
int servePort()
{
    static const int port = [] {
        const int p = freePort();
        ::setenv("FC_BGFX_SERVE_SCENE", std::to_string(p).c_str(), 1);
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
    std::string resp;
    for (int i = 0; i < 200 && resp.empty(); ++i) {
        resp = httpGet(port, "/scene?v=0");
        if (resp.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ASSERT_FALSE(resp.empty()) << "no answer from the scene server";
    EXPECT_NE(resp.find("200"), std::string::npos)
        << "a viewer that has seen nothing must get a payload, not 204";

    const auto hdr = resp.find("\r\n\r\n");
    ASSERT_NE(hdr, std::string::npos);
    const std::string body = resp.substr(hdr + 4);
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
