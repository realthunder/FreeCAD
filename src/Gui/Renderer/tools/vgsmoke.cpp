/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

// vg-renderer offscreen smoke test (docs/TechDrawPortAndSection.md sec 16,
// milestone M0): bring bgfx up headless (no window, no Qt), draw paths and
// text through vg-renderer into an offscreen frame buffer, read the pixels
// back and check that every drawing primitive actually produced ink.
//
//   fcvgsmoke [--renderer gl|vk|auto] [--font /path/to/font.ttf]
//             [--out /path/to/dump.ppm] [--size WxH]
//
// Exit code 0 iff bgfx initialized, all primitives rendered, and the text
// band contains glyph pixels. The dump is a PPM for eyeballing.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>
#include <bx/allocator.h>

#include <vg/vg.h>

static std::vector<uint8_t> readFile(const char* path)
{
    std::vector<uint8_t> data;
    if (FILE* f = fopen(path, "rb")) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size > 0) {
            data.resize((size_t)size);
            if (fread(data.data(), 1, data.size(), f) != data.size())
                data.clear();
        }
        fclose(f);
    }
    return data;
}

// Route bgfx's trace and fatal messages to stderr: this build has
// BX_CONFIG_DEBUG=0, so without a callback an init failure is silent.
struct TraceCallback : public bgfx::CallbackI
{
    void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code,
               const char* str) override
    {
        fprintf(stderr, "bgfx fatal %d at %s:%u: %s\n", (int)code,
                filePath ? filePath : "?", line, str ? str : "");
    }
    void traceVargs(const char* filePath, uint16_t line, const char* format,
                    va_list argList) override
    {
        fprintf(stderr, "bgfx %s:%u: ", filePath ? filePath : "?", line);
        vfprintf(stderr, format, argList);
    }
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char*, uint32_t, uint32_t, uint32_t,
                    bgfx::TextureFormat::Enum, const void*, uint32_t, bool) override {}
    void captureBegin(uint32_t, uint32_t, uint32_t,
                      bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

int main(int argc, char** argv)
{
    uint16_t width = 640;
    uint16_t height = 480;
    const char* outPath = "vgsmoke.ppm";
    const char* fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    bgfx::RendererType::Enum type = bgfx::RendererType::Count; // auto

    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* {
            return ++i < argc ? argv[i] : "";
        };
        if (!strcmp(argv[i], "--renderer")) {
            const char* r = next();
            if (!strcmp(r, "gl"))
                type = bgfx::RendererType::OpenGL;
            else if (!strcmp(r, "vk"))
                type = bgfx::RendererType::Vulkan;
            else if (strcmp(r, "auto")) {
                fprintf(stderr, "unknown renderer '%s'\n", r);
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--font"))
            fontPath = next();
        else if (!strcmp(argv[i], "--out"))
            outPath = next();
        else if (!strcmp(argv[i], "--size")) {
            unsigned w = 0, h = 0;
            if (sscanf(next(), "%ux%u", &w, &h) != 2 || !w || !h) {
                fprintf(stderr, "bad --size\n");
                return 2;
            }
            width = (uint16_t)w;
            height = (uint16_t)h;
        }
        else {
            fprintf(stderr, "unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    // Single-threaded bgfx: calling renderFrame() before init() keeps the
    // render loop on this thread, which is all a batch tool needs.
    bgfx::renderFrame();

    TraceCallback traceCb;
    bgfx::Init init;
    init.callback = &traceCb;
    init.type = type;
    // No platformData: all-null platform data asks bgfx for a headless
    // device -- which requires a 0x0 resolution, since there is no
    // backbuffer; rendering happens in our own offscreen frame buffer.
    init.resolution.width = 0;
    init.resolution.height = 0;
    init.resolution.reset = BGFX_RESET_NONE;
    if (!bgfx::init(init)) {
        fprintf(stderr, "FAIL: bgfx::init\n");
        return 1;
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    printf("renderer: %s (vendor 0x%04x device 0x%04x)\n",
           bgfx::getRendererName(caps->rendererType), caps->vendorId,
           caps->deviceId);
    if (!(caps->supported & BGFX_CAPS_TEXTURE_BLIT)
        || !(caps->supported & BGFX_CAPS_TEXTURE_READ_BACK)) {
        fprintf(stderr, "FAIL: no blit/read-back support\n");
        bgfx::shutdown();
        return 1;
    }

    // Offscreen target: color + depth/stencil (vg's clip paths use
    // stencil), plus a blit-destination texture for the read-back.
    bgfx::TextureHandle color = bgfx::createTexture2D(
        width, height, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
        width, height, false, 1, bgfx::TextureFormat::D24S8,
        BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle attachments[] = {color, depth};
    bgfx::FrameBufferHandle fb = bgfx::createFrameBuffer(2, attachments, false);
    bgfx::TextureHandle staging = bgfx::createTexture2D(
        width, height, false, 1, bgfx::TextureFormat::BGRA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);

    const uint32_t clearRGBA = 0x202428ff; // R 0x20, G 0x24, B 0x28
    bgfx::setViewFrameBuffer(0, fb);
    bgfx::setViewRect(0, 0, 0, width, height);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                       clearRGBA, 1.0f, 0);

    bx::DefaultAllocator allocator;
    vg::Context* ctx = vg::createContext(&allocator);
    if (!ctx) {
        fprintf(stderr, "FAIL: vg::createContext\n");
        bgfx::shutdown();
        return 1;
    }

    std::vector<uint8_t> fontData = readFile(fontPath);
    vg::FontHandle font = VG_INVALID_HANDLE;
    if (!fontData.empty())
        font = vg::createFont(ctx, "sans", fontData.data(),
                              (uint32_t)fontData.size(),
                              vg::FontFlags::DontCopyData);
    if (!vg::isValid(font))
        fprintf(stderr, "note: no font at %s, text check will fail\n", fontPath);

    // The page. Bands (so the read-back can attribute ink to primitives):
    //   x [ 20,180]: convex fill (rect)
    //   x [220,380]: concave fill through libtess2 (five-point star)
    //   x [420,600]: gradient fill + AA cubic stroke
    //   y [380,470] full width: text
    vg::begin(ctx, 0, width, height, 1.0f);

    vg::beginPath(ctx);
    vg::rect(ctx, 20.0f, 40.0f, 160.0f, 120.0f);
    vg::fillPath(ctx, vg::color4ub(220, 70, 60, 255), vg::FillFlags::ConvexAA);

    vg::beginPath(ctx);
    vg::moveTo(ctx, 300.0f, 30.0f);
    vg::lineTo(ctx, 336.0f, 140.0f);
    vg::lineTo(ctx, 230.0f, 72.0f);
    vg::lineTo(ctx, 370.0f, 72.0f);
    vg::lineTo(ctx, 264.0f, 140.0f);
    vg::closePath(ctx);
    vg::fillPath(ctx, vg::color4ub(80, 200, 90, 255),
                 vg::FillFlags::ConcaveNonZeroAA);

    vg::GradientHandle grad = vg::createLinearGradient(
        ctx, 420.0f, 40.0f, 600.0f, 160.0f,
        vg::color4ub(60, 90, 220, 255), vg::color4ub(220, 220, 60, 255));
    vg::beginPath(ctx);
    vg::rect(ctx, 420.0f, 40.0f, 180.0f, 120.0f);
    vg::fillPath(ctx, grad, vg::FillFlags::ConvexAA);

    vg::beginPath(ctx);
    vg::moveTo(ctx, 40.0f, 300.0f);
    vg::cubicTo(ctx, 200.0f, 200.0f, 440.0f, 360.0f, 600.0f, 240.0f);
    vg::strokePath(ctx, vg::color4ub(240, 240, 240, 255), 6.0f,
                   vg::StrokeFlags::ButtRoundAA);

    if (vg::isValid(font)) {
        vg::TextConfig tc = vg::makeTextConfig(
            ctx, font, 48.0f, vg::TextAlign::MiddleLeft,
            vg::color4ub(255, 180, 40, 255));
        vg::text(ctx, tc, 40.0f, 425.0f, "vg smoke 0123", nullptr);
    }

    vg::end(ctx);
    vg::frame(ctx);
    bgfx::touch(0);
    bgfx::frame();

    // Read-back: blit runs inside view 1, after view 0 of the same frame.
    bgfx::blit(1, staging, 0, 0, color, 0, 0, width, height);
    std::vector<uint8_t> pixels((size_t)width * height * 4);
    uint32_t ready = bgfx::readTexture(staging, pixels.data());
    for (uint32_t frameNo = bgfx::frame(); frameNo < ready;)
        frameNo = bgfx::frame();

    // GL frame buffers have a bottom-left origin: the raw read-back is
    // vertically flipped relative to page coordinates.
    const bool flipY = caps->originBottomLeft;

    vg::destroyContext(ctx);
    bgfx::destroy(fb);
    bgfx::destroy(staging);
    bgfx::shutdown();

    // The clear color in BGRA byte order, the layout read back.
    const uint8_t bg[4] = {0x28, 0x24, 0x20, 0xff};
    auto isInk = [&](uint32_t x, uint32_t y) {
        uint32_t row = flipY ? height - 1 - y : y;
        const uint8_t* p = &pixels[((size_t)row * width + x) * 4];
        return abs(p[0] - bg[0]) > 8 || abs(p[1] - bg[1]) > 8
            || abs(p[2] - bg[2]) > 8;
    };
    struct Band {
        const char* name;
        uint32_t x0, y0, x1, y1;
        uint32_t ink = 0;
    } bands[] = {
        {"convex-fill", 20, 40, 180, 160},
        {"concave-fill", 230, 30, 370, 140},
        {"gradient-fill", 420, 40, 600, 160},
        {"aa-stroke", 40, 200, 600, 360},
        {"text", 20, 380, 620, 470},
    };
    uint32_t total = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            if (!isInk(x, y))
                continue;
            ++total;
            for (Band& b : bands) {
                if (x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1)
                    ++b.ink;
            }
        }
    }

    if (FILE* f = fopen(outPath, "wb")) {
        fprintf(f, "P6\n%u %u\n255\n", width, height);
        for (uint32_t y = 0; y < height; ++y) {
            uint32_t row = flipY ? height - 1 - y : y;
            const uint8_t* p = &pixels[(size_t)row * width * 4];
            for (uint32_t x = 0; x < width; ++x, p += 4) {
                uint8_t rgb[3] = {p[2], p[1], p[0]};
                fwrite(rgb, 1, 3, f);
            }
        }
        fclose(f);
        printf("dump: %s\n", outPath);
    }

    bool ok = true;
    printf("ink pixels: %u / %u\n", total, (uint32_t)width * height);
    for (const Band& b : bands) {
        // Every band's primitive covers thousands of pixels; 200 is
        // enough to prove the primitive drew without tuning per shape.
        bool pass = b.ink >= 200;
        printf("  %-13s %6u %s\n", b.name, b.ink, pass ? "ok" : "MISSING");
        ok = ok && pass;
    }
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
