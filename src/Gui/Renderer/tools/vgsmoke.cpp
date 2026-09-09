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

// vg-renderer offscreen smoke test (docs/TechDrawPortAndSection.md sec 16).
// Brings bgfx up headless (no window, no Qt event loop), draws through
// vg-renderer into an offscreen frame buffer, reads the pixels back and
// checks that every drawing primitive actually produced ink.
//
//   fcvgsmoke [--renderer gl|vk|d3d11|d3d12|auto] [--font /path/to/font.ttf]
//             [--out /path/to/dump.ppm] [--size WxH] [--page2d]
//             [--bench N] [--bench-frames N]
//
// Default mode is the M0 raw-vg scenario (paths + text, band ink checks).
// --page2d runs the M1 scenario instead: a retained Page2D under pan,
// in-band zoom, band crossing, rotation, damage and removal, verified by
// pixel probes at view-transformed positions and by the page counters.
// Exit code 0 iff every check passed. Dumps are PPM for eyeballing
// (--page2d appends a stage letter to the dump name).

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>
#include <bx/allocator.h>

#include <vg/vg.h>

#include "Page2D.h"
#include "Vg2D.h"

// Page2D is compiled into this tool rather than linked from
// FreeCADRenderer (the CMake comment on the target says why), and the
// three device hooks it calls live in Renderer.cpp, which is a Qt
// translation unit. They exist to hand a Qt-owned GL context to the
// frames Page2D pumps, and they are answered by walking the registered
// RendererLibs. A standalone tool registers none, so these are the same
// answers the library itself gives with an empty registry -- not stubs
// that pretend: there is no device here, and the offscreen path brings
// up its own.
namespace Render
{
bool RendererFactory::deviceSharesQtGL()
{
    return false;
}
bool RendererFactory::deviceMakeCurrent()
{
    return false;
}
void RendererFactory::deviceDoneCurrent()
{}
}  // namespace Render

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

// The offscreen rig: color+depth/stencil target on view 0, a staging
// texture the color target blits into for read-back.
struct Offscreen
{
    uint16_t width;
    uint16_t height;
    bgfx::TextureHandle color;
    bgfx::TextureHandle depth;
    bgfx::FrameBufferHandle fb;
    bgfx::TextureHandle staging;
    bool flipY; // GL frame buffers have a bottom-left origin
    std::vector<uint8_t> pixels;

    static const uint32_t clearRGBA = 0x202428ff; // R 0x20, G 0x24, B 0x28

    void create(uint16_t w, uint16_t h)
    {
        width = w;
        height = h;
        color = bgfx::createTexture2D(w, h, false, 1,
                                      bgfx::TextureFormat::BGRA8,
                                      BGFX_TEXTURE_RT);
        depth = bgfx::createTexture2D(w, h, false, 1,
                                      bgfx::TextureFormat::D24S8,
                                      BGFX_TEXTURE_RT_WRITE_ONLY);
        bgfx::TextureHandle attachments[] = {color, depth};
        fb = bgfx::createFrameBuffer(2, attachments, false);
        staging = bgfx::createTexture2D(w, h, false, 1,
                                        bgfx::TextureFormat::BGRA8,
                                        BGFX_TEXTURE_BLIT_DST
                                            | BGFX_TEXTURE_READ_BACK);
        bgfx::setViewFrameBuffer(0, fb);
        bgfx::setViewRect(0, 0, 0, w, h);
        bgfx::setViewClear(0,
                           BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
                               | BGFX_CLEAR_STENCIL,
                           clearRGBA, 1.0f, 0);
        flipY = bgfx::getCaps()->originBottomLeft;
        pixels.resize((size_t)w * h * 4);
    }

    void destroy()
    {
        bgfx::destroy(fb);
        // The frame buffer does not own its attachments.
        bgfx::destroy(color);
        bgfx::destroy(depth);
        bgfx::destroy(staging);
    }

    // Finish the current frame and read the target back into pixels.
    void grab()
    {
        bgfx::touch(0);
        bgfx::frame();
        // The blit runs inside view 1, after view 0 of its frame.
        bgfx::blit(1, staging, 0, 0, color, 0, 0, width, height);
        uint32_t ready = bgfx::readTexture(staging, pixels.data());
        for (uint32_t frameNo = bgfx::frame(); frameNo < ready;)
            frameNo = bgfx::frame();
    }

    const uint8_t* pixel(uint32_t x, uint32_t y) const
    {
        uint32_t row = flipY ? height - 1 - y : y;
        return &pixels[((size_t)row * width + x) * 4];
    }

    bool isInk(uint32_t x, uint32_t y) const
    {
        const uint8_t bg[3] = {0x28, 0x24, 0x20}; // BGRA byte order
        const uint8_t* p = pixel(x, y);
        return abs(p[0] - bg[0]) > 8 || abs(p[1] - bg[1]) > 8
            || abs(p[2] - bg[2]) > 8;
    }

    // Dominant-channel classification of a 3x3 patch, tolerant of the
    // AA fringe a probe may land on: 'r','g','b','w' (whitish),
    // 'k' (background).
    char classify(int x, int y) const
    {
        int r = 0, g = 0, b = 0, ink = 0, n = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int px = x + dx, py = y + dy;
                if (px < 0 || py < 0 || px >= width || py >= height)
                    continue;
                ++n;
                const uint8_t* p = pixel(px, py);
                if (isInk(px, py))
                    ++ink;
                b += p[0];
                g += p[1];
                r += p[2];
            }
        }
        if (!n || ink * 2 < n)
            return 'k';
        r /= n;
        g /= n;
        b /= n;
        if (r > 150 && g > 150 && b > 150)
            return 'w';
        if (r >= g && r >= b)
            return 'r';
        if (g >= r && g >= b)
            return 'g';
        return 'b';
    }

    uint32_t inkInRect(int x0, int y0, int x1, int y1) const
    {
        uint32_t count = 0;
        for (int y = y0 < 0 ? 0 : y0; y < y1 && y < height; ++y)
            for (int x = x0 < 0 ? 0 : x0; x < x1 && x < width; ++x)
                if (isInk(x, y))
                    ++count;
        return count;
    }

    void dump(const char* path) const
    {
        FILE* f = fopen(path, "wb");
        if (!f)
            return;
        fprintf(f, "P6\n%u %u\n255\n", width, height);
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* p = pixel(0, y);
            for (uint32_t x = 0; x < width; ++x, p += 4) {
                uint8_t rgb[3] = {p[2], p[1], p[0]};
                fwrite(rgb, 1, 3, f);
            }
        }
        fclose(f);
        printf("dump: %s\n", path);
    }
};

// The M0 scenario: raw vg calls, per-band ink attribution.
static int runVgScenario(Offscreen& target, const char* fontPath,
                         const char* outPath)
{
    const uint16_t width = target.width;
    const uint16_t height = target.height;

    bx::DefaultAllocator allocator;
    vg::Context* ctx = vg::createContext(&allocator);
    if (!ctx) {
        fprintf(stderr, "FAIL: vg::createContext\n");
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
    target.grab();
    vg::destroyContext(ctx);

    target.dump(outPath);

    struct Band {
        const char* name;
        int x0, y0, x1, y1;
    } bands[] = {
        {"convex-fill", 20, 40, 180, 160},
        {"concave-fill", 230, 30, 370, 140},
        {"gradient-fill", 420, 40, 600, 160},
        {"aa-stroke", 40, 200, 600, 360},
        {"text", 20, 380, 620, 470},
    };
    bool ok = true;
    printf("ink pixels: %u / %u\n", target.inkInRect(0, 0, width, height),
           (uint32_t)width * height);
    for (const Band& b : bands) {
        // Every band's primitive covers thousands of pixels; 200 is
        // enough to prove the primitive drew without tuning per shape.
        uint32_t ink = target.inkInRect(b.x0, b.y0, b.x1, b.y1);
        bool pass = ink >= 200;
        printf("  %-13s %6u %s\n", b.name, ink, pass ? "ok" : "MISSING");
        ok = ok && pass;
    }
    return ok ? 0 : 1;
}

// Retained-store scaling: N stroke items, then the per-frame cost of
// replay (same view), pan, in-band zoom, and a band crossing. This is
// the number the whole design argues about -- Qt raster repaints cost
// 3-4 us/item every frame (doc sec 15); the retained page must make an
// unchanged frame cheap. Informational, never fails.
static void runBench(Offscreen& target, uint32_t count, uint32_t frames)
{
    using Render::Page2D;
    using Render::Vg2D;
    using clock_t_ = std::chrono::steady_clock;

    if (!Vg2D::instance().init()) {
        fprintf(stderr, "bench skipped: no vg context\n");
        return;
    }
    Page2D page;
    // Short two-segment polyline strokes in a grid, page coords spread
    // over ~4x the viewport so zooming has content to pull in.
    const uint32_t cols = (uint32_t)ceilf(sqrtf((float)count));
    for (uint32_t i = 0; i < count; ++i) {
        float x = (float)(i % cols) * 12.0f;
        float y = (float)(i / cols) * 12.0f;
        Page2D::Recorder rec;
        rec.beginPath();
        rec.moveTo(x, y);
        rec.lineTo(x + 8.0f, y + 4.0f);
        rec.lineTo(x + 10.0f, y + 9.0f);
        rec.stroke(0xf0f0f0ff, 1.5f);
        page.setItem(i + 1, Page2D::Kind::Edge, 0, std::move(rec));
    }

    Page2D::View view;
    // ! Two things this has to do that timing one bgfx::frame() does not.
    //
    // AVERAGE. A single frame is a sample, not a rate, and these bounce
    // by 2x run to run -- a first attempt at ranking backends this way
    // read D3D11 at 6.96 ms and 13.13 ms for the same case.
    //
    // And FORCE COMPLETION, which is the one that changes conclusions.
    // bgfx::frame() returns after SUBMISSION, and how much a backend
    // defers past that point is its own business, so wall clock around
    // it ranks how much each driver postpones rather than how much it
    // finishes. That is not hypothetical: it is what made Vulkan look 3x
    // faster than Direct3D here while the readback probe -- which does
    // force completion -- put the two within 4% on the same GPU.
    // grab() is the barrier: it blits, reads back, and pumps frames
    // until the read lands, so everything submitted in the batch above
    // it has completed before the clock stops. Its own cost (a 640x480
    // readback) is amortized over the batch and is well under a
    // hundredth of a frame.
    auto frameMs = [&](const char* what, bool average = true) {
        page.setView(view);
        const uint32_t n = average ? frames : 1;
        if (average) {
            // One frame outside the clock: the view change above may
            // re-record or re-tessellate, and that cost belongs to the
            // "band crossing" case, not to every steady-state one.
            page.render(0, target.width, target.height);
            bgfx::touch(0);
            bgfx::frame();
        }
        auto t0 = clock_t_::now();
        for (uint32_t i = 0; i < n; ++i) {
            page.render(0, target.width, target.height);
            bgfx::touch(0);
            bgfx::frame();
        }
        target.grab();
        double ms = std::chrono::duration<double, std::milli>(
                        clock_t_::now() - t0).count() / double(n);
        printf("  %-28s %8.2f ms  (%.2f us/item, n=%u)\n", what, ms,
               ms * 1000.0 / count, n);
    };

    printf("bench: %u stroke items\n", count);
    frameMs("first frame (record all)", false);
    frameMs("unchanged frame (replay)");
    view.panX = 31.0f;
    view.panY = 17.0f;
    frameMs("pan frame (replay)");
    view.zoom = 1.31f;
    frameMs("in-band zoom (replay)");
    view.zoom = 2.7f;
    frameMs("band crossing (re-tess)", false);
    view.zoom = 2.71f;
    frameMs("post-crossing (replay)");

    page.clear();
    Vg2D::instance().shutdown();
}

// The M1 scenario: a retained Page2D driven through view changes.
static int runPage2DScenario(Offscreen& target, const char* fontPath,
                             const char* outPath)
{
    using Render::Page2D;
    using Render::Vg2D;

    if (!Vg2D::instance().init()) {
        fprintf(stderr, "FAIL: Vg2D::init\n");
        return 1;
    }
    if (!vg::isValid(Vg2D::instance().loadFontFile("sans", fontPath)))
        fprintf(stderr, "note: no font at %s, text check will fail\n",
                fontPath);

    Page2D page;

    { // item 1, Face: red rect (10,10)-(110,90)
        Page2D::Recorder rec;
        rec.beginPath();
        rec.rect(10.0f, 10.0f, 100.0f, 80.0f);
        rec.fillConvex(0xdc4632ff);
        page.setItem(1, Page2D::Kind::Face, 0, std::move(rec));
    }
    { // item 2, Edge: white horizontal line y=150, x 10..110
        Page2D::Recorder rec;
        rec.beginPath();
        rec.moveTo(10.0f, 150.0f);
        rec.lineTo(110.0f, 150.0f);
        rec.stroke(0xf0f0f0ff, 5.0f);
        page.setItem(2, Page2D::Kind::Edge, 0, std::move(rec));
    }
    { // item 3, Decoration: green concave star around (190,128)
        Page2D::Recorder rec;
        rec.beginPath();
        rec.moveTo(190.0f, 100.0f);
        rec.lineTo(208.0f, 155.0f);
        rec.lineTo(155.0f, 121.0f);
        rec.lineTo(225.0f, 121.0f);
        rec.lineTo(172.0f, 155.0f);
        rec.closePath();
        rec.fillConcave(0x50c85aff);
        page.setItem(3, Page2D::Kind::Decoration, 0, std::move(rec));
    }
    { // item 4, Annotation: text baseline at (10,240)
        Page2D::Recorder rec;
        rec.text("sans", 40.0f, 0xffb428ff, 10.0f, 240.0f, "P2D text");
        page.setItem(4, Page2D::Kind::Annotation, 0, std::move(rec));
    }
    { // item 5, Face: blue square (150,10)-(230,90) as two triangles
        const float xy[] = {150.0f, 10.0f, 230.0f, 10.0f,
                            230.0f, 90.0f, 150.0f, 90.0f};
        const uint16_t idx[] = {0, 1, 2, 0, 2, 3};
        Page2D::Recorder rec;
        rec.triangles(xy, 4, idx, 6, 0x3c5adcff);
        page.setItem(5, Page2D::Kind::Face, 0, std::move(rec));
    }

    // A probe takes page coordinates and applies the view transform the
    // page was rendered with.
    Page2D::View view;
    auto probe = [&](float px, float py) {
        float c = cosf(view.rotation), s = sinf(view.rotation);
        float zx = px * view.zoom, zy = py * view.zoom;
        float x = view.panX + c * zx - s * zy;
        float y = view.panY + s * zx + c * zy;
        return target.classify((int)lroundf(x), (int)lroundf(y));
    };

    bool ok = true;
    int stage = 0;
    auto render = [&](const char* what) {
        page.setView(view);
        if (!page.render(0, target.width, target.height)) {
            fprintf(stderr, "FAIL: Page2D::render\n");
            ok = false;
            return;
        }
        target.grab();
        std::string path = outPath;
        path += '.';
        path += (char)('a' + stage++);
        path += ".ppm";
        target.dump(path.c_str());
        printf("stage %c: %s\n", 'a' + (stage - 1), what);
    };
    auto check = [&](const char* what, bool cond) {
        printf("  %-38s %s\n", what, cond ? "ok" : "FAIL");
        ok = ok && cond;
    };

    // Stage a: identity view.
    render("identity view");
    check("face rect red at center", probe(60, 50) == 'r');
    check("triangle square blue at center", probe(190, 50) == 'b');
    check("edge stroke white on line", probe(60, 150) == 'w');
    check("concave star green at center", probe(190, 126) == 'g');
    check("text ink present", target.inkInRect(10, 205, 200, 250) > 200);
    check("background empty", probe(400, 400) == 'k');
    const uint32_t recordsAfterFirst = page.counters().itemRecords;
    check("all items recorded once", recordsAfterFirst == 5);

    // Stage b: pan only -- replays every cached list.
    view.panX = 250.0f;
    view.panY = 120.0f;
    render("pan (250,120)");
    check("rect followed the pan", probe(60, 50) == 'r');
    check("old location empty", target.classify(60, 50) == 'k');
    check("pan re-recorded nothing",
          page.counters().itemRecords == recordsAfterFirst);

    // Stage c: zoom 1.3 -- inside band 1, residual only.
    view.panX = 0.0f;
    view.panY = 0.0f;
    view.zoom = 1.3f;
    render("zoom 1.3 (in band)");
    check("rect scaled by residual", probe(60, 50) == 'r');
    check("star scaled by residual", probe(190, 126) == 'g');
    check("in-band zoom re-recorded nothing",
          page.counters().itemRecords == recordsAfterFirst);
    check("no band crossing yet", page.counters().bandCrossings == 0);

    // Stage d: zoom 3.0 -- band 4, vg re-tessellates internally.
    view.zoom = 3.0f;
    render("zoom 3.0 (band crossing)");
    check("rect correct across band", probe(60, 50) == 'r');
    check("edge correct across band", probe(60, 150) == 'w');
    check("band crossing counted", page.counters().bandCrossings == 1);
    check("crossing re-recorded nothing",
          page.counters().itemRecords == recordsAfterFirst);

    // Stage e: rotation 90 degrees around the page origin, panned into
    // view. Page (x,y) must land at pan + (-y*zoom, x*zoom).
    view.zoom = 1.0f;
    view.rotation = 1.5707963f;
    view.panX = 300.0f;
    view.panY = 20.0f;
    render("rotate 90deg");
    check("rect rotated into place", probe(60, 50) == 'r');
    check("edge rotated into place", probe(60, 150) == 'w');

    // Stage f: damage -- item 1 turns green.
    view = Page2D::View();
    {
        Page2D::Recorder rec;
        rec.beginPath();
        rec.rect(10.0f, 10.0f, 100.0f, 80.0f);
        rec.fillConvex(0x32c846ff);
        page.setItem(1, Page2D::Kind::Face, 0, std::move(rec));
    }
    render("damage item 1 to green");
    check("damaged rect is green", probe(60, 50) == 'g');
    check("damage re-recorded exactly one",
          page.counters().itemRecords == recordsAfterFirst + 1);

    // Stage g: removal.
    page.removeItem(2);
    render("remove edge item");
    check("removed edge gone", probe(60, 150) == 'k');
    check("others still there", probe(60, 50) == 'g' && probe(190, 50) == 'b');

    // Stage h: a hole. Two concentric closed contours in one path,
    // filled even-odd -- the face-with-hole representation the TechDraw
    // feed uses (vg has no native holes; this is the workaround).
    {
        Page2D::Recorder rec;
        rec.beginPath();
        rec.moveTo(300.0f, 300.0f);
        rec.lineTo(420.0f, 300.0f);
        rec.lineTo(420.0f, 420.0f);
        rec.lineTo(300.0f, 420.0f);
        rec.closePath();
        rec.moveTo(340.0f, 340.0f);
        rec.lineTo(380.0f, 340.0f);
        rec.lineTo(380.0f, 380.0f);
        rec.lineTo(340.0f, 380.0f);
        rec.closePath();
        rec.fillConcave(0xdc3232ff, /*evenOdd*/ true);
        page.setItem(6, Page2D::Kind::Face, 0, std::move(rec));
    }
    render("even-odd hole fill");
    check("ring filled", probe(320, 360) == 'r');
    check("hole empty", probe(360, 360) == 'k');

    page.clear();
    Vg2D::instance().shutdown();
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    uint16_t width = 640;
    uint16_t height = 480;
    const char* outPath = "vgsmoke.ppm";
    const char* fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    bgfx::RendererType::Enum type = bgfx::RendererType::Count; // auto
    bool page2d = false;
    uint32_t bench = 0;
    // Frames per averaged bench case. Big enough that the one-off
    // completion barrier at the end of a batch is noise, small
    // enough that a run of every backend stays under a minute.
    uint32_t benchFrames = 200;

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
            // The vg shader pack carries dxbc and dxil (docs/Testing.md,
            // "The vg smokes on Windows"), so both Direct3D backends can
            // be asked for by name. Auto-selection already picks D3D11
            // on Windows; naming them is what makes a backend-against-
            // backend comparison possible, D3D12 especially, which auto
            // never chooses.
            else if (!strcmp(r, "d3d11"))
                type = bgfx::RendererType::Direct3D11;
            else if (!strcmp(r, "d3d12"))
                type = bgfx::RendererType::Direct3D12;
            else if (strcmp(r, "auto")) {
                fprintf(stderr, "unknown renderer '%s'\n", r);
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--font"))
            fontPath = next();
        else if (!strcmp(argv[i], "--out"))
            outPath = next();
        else if (!strcmp(argv[i], "--page2d"))
            page2d = true;
        else if (!strcmp(argv[i], "--bench-frames"))
            benchFrames = (uint32_t)strtoul(next(), nullptr, 10);
        else if (!strcmp(argv[i], "--bench"))
            bench = (uint32_t)strtoul(next(), nullptr, 10);
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

    Offscreen target;
    target.create(width, height);

    int res = 0;
    if (bench)
        runBench(target, bench, benchFrames);
    else if (page2d)
        res = runPage2DScenario(target, fontPath, outPath);
    else
        res = runVgScenario(target, fontPath, outPath);

    target.destroy();
    bgfx::shutdown();
    printf(res == 0 ? "PASS\n" : "FAIL\n");
    return res;
}
