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

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <vg/vg.h>

#include "Page2D.h"
#include "Vg2D.h"

using namespace Render;

struct Page2D::Private
{
    struct Item
    {
        Kind kind = Kind::Face;
        uint32_t layer = 0;
        uint64_t seq = 0;
        std::vector<uint8_t> ops;
        vg::CommandListHandle list = VG_INVALID_HANDLE;
        bool recorded = false;
    };

    std::map<ItemId, Item> items;
    std::vector<Item*> drawOrder;
    bool orderDirty = false;
    uint64_t nextSeq = 0;
    float lastBandScale = 0.0f;

    void releaseList(Item& item)
    {
        if (!vg::isValid(item.list))
            return;
        if (Vg2D::instance().initialized())
            vg::destroyCommandList(Vg2D::instance().context(), item.list);
        item.list = VG_INVALID_HANDLE;
        item.recorded = false;
    }
};

// The op buffer: 1-byte opcode, raw little-endian payload. This is the
// item's retained representation and (M3) its wire format, so nothing
// here may depend on host struct layout.
namespace {

enum class Op : uint8_t {
    BeginPath = 0,
    MoveTo,
    LineTo,
    CubicTo,
    QuadraticTo,
    Arc,
    ClosePath,
    Rect,
    Circle,
    Ellipse,
    Polyline,
    FillConvex,
    FillConcave,
    FillLinearGradient,
    Stroke,
    Text,
    Triangles,
};

void putBytes(std::vector<uint8_t>& out, const void* p, size_t n)
{
    const uint8_t* b = (const uint8_t*)p;
    out.insert(out.end(), b, b + n);
}

void putOp(std::vector<uint8_t>& out, Op op)
{
    out.push_back((uint8_t)op);
}

void putF(std::vector<uint8_t>& out, float v)
{
    putBytes(out, &v, sizeof(v));
}

void putU32(std::vector<uint8_t>& out, uint32_t v)
{
    putBytes(out, &v, sizeof(v));
}

void putU8(std::vector<uint8_t>& out, uint8_t v)
{
    out.push_back(v);
}

struct OpReader
{
    const uint8_t* p;
    const uint8_t* end;

    bool done() const { return p >= end; }
    bool fits(size_t n) const { return (size_t)(end - p) >= n; }

    uint8_t u8() { return *p++; }
    uint32_t u32()
    {
        uint32_t v;
        memcpy(&v, p, sizeof(v));
        p += sizeof(v);
        return v;
    }
    float f()
    {
        float v;
        memcpy(&v, p, sizeof(v));
        p += sizeof(v);
        return v;
    }
};

vg::Color decodeColor(uint32_t rgba)
{
    return vg::color4ub((uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16),
                        (uint8_t)(rgba >> 8), (uint8_t)rgba);
}

} // namespace

void Page2D::Recorder::beginPath()
{
    putOp(ops, Op::BeginPath);
}

void Page2D::Recorder::moveTo(float x, float y)
{
    putOp(ops, Op::MoveTo);
    putF(ops, x);
    putF(ops, y);
}

void Page2D::Recorder::lineTo(float x, float y)
{
    putOp(ops, Op::LineTo);
    putF(ops, x);
    putF(ops, y);
}

void Page2D::Recorder::cubicTo(float c1x, float c1y, float c2x, float c2y,
                               float x, float y)
{
    putOp(ops, Op::CubicTo);
    putF(ops, c1x);
    putF(ops, c1y);
    putF(ops, c2x);
    putF(ops, c2y);
    putF(ops, x);
    putF(ops, y);
}

void Page2D::Recorder::quadraticTo(float cx, float cy, float x, float y)
{
    putOp(ops, Op::QuadraticTo);
    putF(ops, cx);
    putF(ops, cy);
    putF(ops, x);
    putF(ops, y);
}

void Page2D::Recorder::arc(float cx, float cy, float r, float a0, float a1,
                           bool clockwise)
{
    putOp(ops, Op::Arc);
    putF(ops, cx);
    putF(ops, cy);
    putF(ops, r);
    putF(ops, a0);
    putF(ops, a1);
    putU8(ops, clockwise ? 1 : 0);
}

void Page2D::Recorder::closePath()
{
    putOp(ops, Op::ClosePath);
}

void Page2D::Recorder::rect(float x, float y, float w, float h)
{
    putOp(ops, Op::Rect);
    putF(ops, x);
    putF(ops, y);
    putF(ops, w);
    putF(ops, h);
}

void Page2D::Recorder::circle(float cx, float cy, float r)
{
    putOp(ops, Op::Circle);
    putF(ops, cx);
    putF(ops, cy);
    putF(ops, r);
}

void Page2D::Recorder::ellipse(float cx, float cy, float rx, float ry)
{
    putOp(ops, Op::Ellipse);
    putF(ops, cx);
    putF(ops, cy);
    putF(ops, rx);
    putF(ops, ry);
}

void Page2D::Recorder::polyline(const float* xy, uint32_t numPoints)
{
    if (!xy || numPoints < 2)
        return;
    putOp(ops, Op::Polyline);
    putU32(ops, numPoints);
    putBytes(ops, xy, sizeof(float) * 2 * numPoints);
}

void Page2D::Recorder::fillConvex(uint32_t rgba)
{
    putOp(ops, Op::FillConvex);
    putU32(ops, rgba);
}

void Page2D::Recorder::fillConcave(uint32_t rgba, bool evenOdd)
{
    putOp(ops, Op::FillConcave);
    putU32(ops, rgba);
    putU8(ops, evenOdd ? 1 : 0);
}

void Page2D::Recorder::fillLinearGradient(float sx, float sy, float ex, float ey,
                                          uint32_t rgbaStart, uint32_t rgbaEnd)
{
    putOp(ops, Op::FillLinearGradient);
    putF(ops, sx);
    putF(ops, sy);
    putF(ops, ex);
    putF(ops, ey);
    putU32(ops, rgbaStart);
    putU32(ops, rgbaEnd);
}

void Page2D::Recorder::stroke(uint32_t rgba, float width)
{
    putOp(ops, Op::Stroke);
    putU32(ops, rgba);
    putF(ops, width);
}

void Page2D::Recorder::text(const char* font, float size, uint32_t rgba,
                            float x, float y, const char* utf8)
{
    if (!font || !utf8)
        return;
    size_t nameLen = strlen(font);
    size_t textLen = strlen(utf8);
    if (!nameLen || nameLen > 255 || !textLen)
        return;
    putOp(ops, Op::Text);
    putU8(ops, (uint8_t)nameLen);
    putBytes(ops, font, nameLen);
    putF(ops, size);
    putU32(ops, rgba);
    putF(ops, x);
    putF(ops, y);
    putU32(ops, (uint32_t)textLen);
    putBytes(ops, utf8, textLen);
}

void Page2D::Recorder::triangles(const float* xy, uint32_t numVertices,
                                 const uint16_t* indices, uint32_t numIndices,
                                 uint32_t rgba)
{
    if (!xy || !indices || !numVertices || !numIndices || numIndices % 3)
        return;
    putOp(ops, Op::Triangles);
    putU32(ops, numVertices);
    putBytes(ops, xy, sizeof(float) * 2 * numVertices);
    putU32(ops, numIndices);
    putBytes(ops, indices, sizeof(uint16_t) * numIndices);
    putU32(ops, rgba);
}

// Replay one op buffer into the currently recording vg command list.
static void replayOps(vg::Context* ctx, const std::vector<uint8_t>& ops)
{
    OpReader r {ops.data(), ops.data() + ops.size()};
    while (!r.done()) {
        switch ((Op)r.u8()) {
        case Op::BeginPath:
            vg::beginPath(ctx);
            break;
        case Op::MoveTo: {
            float x = r.f(), y = r.f();
            vg::moveTo(ctx, x, y);
            break;
        }
        case Op::LineTo: {
            float x = r.f(), y = r.f();
            vg::lineTo(ctx, x, y);
            break;
        }
        case Op::CubicTo: {
            float c1x = r.f(), c1y = r.f(), c2x = r.f(), c2y = r.f();
            float x = r.f(), y = r.f();
            vg::cubicTo(ctx, c1x, c1y, c2x, c2y, x, y);
            break;
        }
        case Op::QuadraticTo: {
            float cx = r.f(), cy = r.f(), x = r.f(), y = r.f();
            vg::quadraticTo(ctx, cx, cy, x, y);
            break;
        }
        case Op::Arc: {
            float cx = r.f(), cy = r.f(), rr = r.f(), a0 = r.f(), a1 = r.f();
            vg::Winding::Enum dir = r.u8() ? vg::Winding::CW : vg::Winding::CCW;
            vg::arc(ctx, cx, cy, rr, a0, a1, dir);
            break;
        }
        case Op::ClosePath:
            vg::closePath(ctx);
            break;
        case Op::Rect: {
            float x = r.f(), y = r.f(), w = r.f(), h = r.f();
            vg::rect(ctx, x, y, w, h);
            break;
        }
        case Op::Circle: {
            float cx = r.f(), cy = r.f(), rr = r.f();
            vg::circle(ctx, cx, cy, rr);
            break;
        }
        case Op::Ellipse: {
            float cx = r.f(), cy = r.f(), rx = r.f(), ry = r.f();
            vg::ellipse(ctx, cx, cy, rx, ry);
            break;
        }
        case Op::Polyline: {
            uint32_t n = r.u32();
            if (!r.fits(sizeof(float) * 2 * n))
                return;
            // Copy out of the byte-packed buffer (alignment, and wasm
            // will not tolerate unaligned float loads). vg::polyline
            // only appends: the current subpath must be started first.
            std::vector<float> xy(2 * n);
            memcpy(xy.data(), r.p, sizeof(float) * 2 * n);
            r.p += sizeof(float) * 2 * n;
            vg::moveTo(ctx, xy[0], xy[1]);
            if (n > 1)
                vg::polyline(ctx, xy.data() + 2, n - 1);
            break;
        }
        case Op::FillConvex:
            vg::fillPath(ctx, decodeColor(r.u32()), vg::FillFlags::ConvexAA);
            break;
        case Op::FillConcave: {
            vg::Color c = decodeColor(r.u32());
            vg::fillPath(ctx, c,
                         r.u8() ? vg::FillFlags::ConcaveEvenOddAA
                                : vg::FillFlags::ConcaveNonZeroAA);
            break;
        }
        case Op::FillLinearGradient: {
            float sx = r.f(), sy = r.f(), ex = r.f(), ey = r.f();
            vg::Color c0 = decodeColor(r.u32());
            vg::Color c1 = decodeColor(r.u32());
            vg::GradientHandle grad =
                vg::createLinearGradient(ctx, sx, sy, ex, ey, c0, c1);
            vg::fillPath(ctx, grad, vg::FillFlags::ConvexAA);
            break;
        }
        case Op::Stroke: {
            vg::Color c = decodeColor(r.u32());
            float w = r.f();
            vg::strokePath(ctx, c, w, vg::StrokeFlags::ButtRoundAA);
            break;
        }
        case Op::Text: {
            uint8_t nameLen = r.u8();
            if (!r.fits(nameLen))
                return;
            char name[256];
            memcpy(name, r.p, nameLen);
            name[nameLen] = 0;
            r.p += nameLen;
            float size = r.f();
            vg::Color c = decodeColor(r.u32());
            float x = r.f(), y = r.f();
            uint32_t textLen = r.u32();
            if (!r.fits(textLen))
                return;
            const char* str = (const char*)r.p;
            r.p += textLen;
            vg::FontHandle font = Vg2D::instance().font(name);
            if (vg::isValid(font)) {
                vg::TextConfig tc = vg::makeTextConfig(
                    ctx, font, size,
                    vg::TextAlign::Baseline | vg::TextAlign::Left, c);
                vg::text(ctx, tc, x, y, str, str + textLen);
            }
            break;
        }
        case Op::Triangles: {
            uint32_t nv = r.u32();
            if (!r.fits(sizeof(float) * 2 * nv))
                return;
            std::vector<float> pos(2 * nv);
            memcpy(pos.data(), r.p, sizeof(float) * 2 * nv);
            r.p += sizeof(float) * 2 * nv;
            uint32_t ni = r.u32();
            if (!r.fits(sizeof(uint16_t) * ni))
                return;
            std::vector<uint16_t> idx(ni);
            memcpy(idx.data(), r.p, sizeof(uint16_t) * ni);
            r.p += sizeof(uint16_t) * ni;
            vg::Color c = decodeColor(r.u32());
            vg::ImageHandle noImage = VG_INVALID_HANDLE;
            vg::indexedTriList(ctx, pos.data(), nullptr, nv, &c, 1, idx.data(),
                               ni, noImage);
            break;
        }
        default:
            // Unknown op: the buffer is from a newer writer; stop rather
            // than misparse the rest.
            return;
        }
    }
}

Page2D::Page2D()
    : d(new Private)
{
}

Page2D::~Page2D()
{
    clear();
}

void Page2D::setItem(ItemId id, Kind kind, uint32_t layer, Recorder&& content)
{
    Private::Item& item = d->items[id];
    if (item.seq == 0)
        item.seq = ++d->nextSeq;
    if (item.kind != kind || item.layer != layer) {
        item.kind = kind;
        item.layer = layer;
        d->orderDirty = true;
    }
    item.ops = std::move(content.ops);
    // Damage: keep the command list, re-record it from the new ops at
    // the next render.
    item.recorded = false;
    if (d->drawOrder.empty() || d->items.size() != d->drawOrder.size())
        d->orderDirty = true;
}

void Page2D::removeItem(ItemId id)
{
    auto it = d->items.find(id);
    if (it == d->items.end())
        return;
    d->releaseList(it->second);
    d->items.erase(it);
    d->orderDirty = true;
}

bool Page2D::hasItem(ItemId id) const
{
    return d->items.count(id) != 0;
}

void Page2D::clear()
{
    for (auto& v : d->items)
        d->releaseList(v.second);
    d->items.clear();
    d->drawOrder.clear();
    d->orderDirty = false;
}

float Page2D::bandScale(float zoom)
{
    if (!(zoom > 1e-6f))
        zoom = 1e-6f;
    return exp2f(roundf(log2f(zoom)));
}

bool Page2D::render(uint16_t viewId, uint16_t width, uint16_t height)
{
    Vg2D& vg2d = Vg2D::instance();
    if (!vg2d.initialized() && !vg2d.init())
        return false;
    vg::Context* ctx = vg2d.context();

    const float band = bandScale(pageView.zoom);
    if (band != d->lastBandScale) {
        if (d->lastBandScale != 0.0f)
            ++stats.bandCrossings;
        d->lastBandScale = band;
        // Nothing to invalidate here: each cached command list keys on
        // the vg state scale and re-tessellates itself when it changes.
    }

    if (d->orderDirty) {
        d->drawOrder.clear();
        d->drawOrder.reserve(d->items.size());
        for (auto& v : d->items)
            d->drawOrder.push_back(&v.second);
        std::sort(d->drawOrder.begin(), d->drawOrder.end(),
                  [](const Private::Item* a, const Private::Item* b) {
                      if (a->layer != b->layer)
                          return a->layer < b->layer;
                      if (a->kind != b->kind)
                          return (uint8_t)a->kind < (uint8_t)b->kind;
                      return a->seq < b->seq;
                  });
        d->orderDirty = false;
    }

    vg::begin(ctx, viewId, width, height, pageView.devicePixelRatio);
    vg::transformScale(ctx, band, band);
    for (Private::Item* item : d->drawOrder) {
        if (!item->recorded) {
            if (!vg::isValid(item->list)) {
                item->list =
                    vg::createCommandList(ctx, vg::CommandListFlags::Cacheable);
                if (!vg::isValid(item->list)) {
                    // Out of command list slots: skip rather than hand vg
                    // an invalid handle. The counter keeps it visible.
                    ++stats.droppedItems;
                    continue;
                }
            }
            else
                vg::resetCommandList(ctx, item->list);
            vg::beginCommandList(ctx, item->list);
            replayOps(ctx, item->ops);
            vg::endCommandList(ctx);
            item->recorded = true;
            ++stats.itemRecords;
        }
        vg::submitCommandList(ctx, item->list);
        ++stats.listSubmits;
    }
    vg::end(ctx);
    vg::frame(ctx);

    // The other half of the transform split: pan, rotation and the
    // residual zoom (1/sqrt(2)..sqrt(2)) go into the bgfx view matrix,
    // so they never touch the vg state scale and every cached
    // tessellation stays valid across pan/zoom/rotate within a band.
    const float residual = pageView.zoom / band;
    float viewMtx[16];
    // bx's rotation convention is opposite to the page's (positive =
    // clockwise on a y-down screen, the Qt convention), hence the sign.
    bx::mtxSRT(viewMtx, residual, residual, 1.0f, 0.0f, 0.0f,
               -pageView.rotation, pageView.panX, pageView.panY, 0.0f);
    float proj[16];
    bx::mtxOrtho(proj, 0.0f, (float)width, (float)height, 0.0f, 0.0f, 1.0f,
                 0.0f, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(viewId, viewMtx, proj);
    return true;
}

// Whether renderOffscreen() itself brought the bgfx device up. When the
// process already initialized bgfx (the 3D renderer), init() below
// simply fails and we use the existing device.
static bool _offscreenDeviceUp = false;

bool Page2D::renderOffscreen(uint16_t width, uint16_t height,
                             std::vector<uint8_t>& rgba)
{
    if (!width || !height)
        return false;
    if (!_offscreenDeviceUp && !Vg2D::instance().initialized()) {
        // Bring up a headless device: all-null platform data with the
        // mandatory 0x0 backbuffer. No renderFrame() first -- in a GUI
        // process the 3D renderer may already own an initialized bgfx,
        // and pumping its render loop from here executes its pending
        // commands out of band. init() on an initialized bgfx is a
        // harmless refusal, and then we share the existing device.
        bgfx::Init init;
#ifdef __linux__
        // Auto-selection takes OpenGL when a DISPLAY exists, and under a
        // virtual X server that is Mesa swrast, which crashes in bgfx's
        // headless context path. Vulkan needs no display at all.
        init.type = bgfx::RendererType::Vulkan;
#endif
        if (const char* env = getenv("FC_PAGE2D_RENDERER")) {
            if (!strcmp(env, "gl"))
                init.type = bgfx::RendererType::OpenGL;
            else if (!strcmp(env, "vk"))
                init.type = bgfx::RendererType::Vulkan;
            else if (!strcmp(env, "auto"))
                init.type = bgfx::RendererType::Count;
        }
        init.resolution.width = 0;
        init.resolution.height = 0;
        init.resolution.reset = BGFX_RESET_NONE;
        if (bgfx::init(init))
            _offscreenDeviceUp = true;
        // else: a device already exists (the 3D renderer's); use it.
    }

    const uint64_t rtFlags = BGFX_TEXTURE_RT;
    bgfx::TextureHandle color = bgfx::createTexture2D(
        width, height, false, 1, bgfx::TextureFormat::BGRA8, rtFlags);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
        width, height, false, 1, bgfx::TextureFormat::D24S8,
        BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle attachments[] = {color, depth};
    bgfx::FrameBufferHandle fb = bgfx::createFrameBuffer(2, attachments, false);
    bgfx::TextureHandle staging = bgfx::createTexture2D(
        width, height, false, 1, bgfx::TextureFormat::BGRA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);

    // The page draws on the offscreen view id pair right below bgfx's
    // ceiling, out of the way of any 3D viewer's blocks.
    const uint16_t viewId = (uint16_t)(bgfx::getCaps()->limits.maxViews - 2);
    bgfx::setViewFrameBuffer(viewId, fb);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId,
                       BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                       0xffffffff, 1.0f, 0);

    bool ok = render(viewId, width, height);
    bgfx::touch(viewId);
    bgfx::frame();

    std::vector<uint8_t> raw((size_t)width * height * 4);
    if (ok) {
        bgfx::blit((uint16_t)(viewId + 1), staging, 0, 0, color, 0, 0, width,
                   height);
        uint32_t ready = bgfx::readTexture(staging, raw.data());
        for (uint32_t frameNo = bgfx::frame(); frameNo < ready;)
            frameNo = bgfx::frame();
    }

    bgfx::FrameBufferHandle noFb = BGFX_INVALID_HANDLE;
    bgfx::setViewFrameBuffer(viewId, noFb);
    bgfx::destroy(fb);
    bgfx::destroy(staging);

    if (!ok)
        return false;

    // BGRA rows (bottom-up on GL) -> tight RGBA rows, top-down.
    const bool flipY = bgfx::getCaps()->originBottomLeft;
    rgba.resize(raw.size());
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src =
            &raw[(size_t)(flipY ? height - 1 - y : y) * width * 4];
        uint8_t* dst = &rgba[(size_t)y * width * 4];
        for (uint32_t x = 0; x < width; ++x, src += 4, dst += 4) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
        }
    }
    return true;
}
