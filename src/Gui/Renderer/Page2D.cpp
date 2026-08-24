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

#include "Page2D.h"

// The record/storage side of Page2D is always built, so consumers
// (the TechDraw feed) link unconditionally; everything touching
// vg/bgfx compiles only with the bgfx renderer, and the render entry
// points degrade to a false return without it.
#ifdef HAVE_BGFX
#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <vg/vg.h>

#include "Vg2D.h"
#endif

using namespace Render;

namespace {

// A registered image: the retained straight-alpha RGBA8 pixels plus
// the vg image handle they are uploaded under. Kept at namespace scope
// so the replay functions below can resolve image ops without access
// to Page2D's private parts.
struct PageImage
{
    std::vector<uint8_t> pixels;
    uint16_t width = 0;
    uint16_t height = 0;
    bool repeat = false;
    uint16_t handle = 0xffff; // vg::ImageHandle index, 0xffff = none
    uint32_t version = 0;     // bumped by setImage
    uint32_t uploaded = 0;    // version the current handle carries
};

} // namespace

struct Page2D::Private
{
    // The invalid vg command list handle index, kept as a raw uint16
    // so the item storage builds without the vg headers.
    static const uint16_t kNoList = 0xffff;
    static const uint16_t kNoImage = 0xffff;

    struct Item
    {
        Kind kind = Kind::Face;
        uint32_t layer = 0;
        uint64_t seq = 0;
        std::vector<uint8_t> ops;
        uint16_t list = kNoList;
        bool recorded = false;
        // Image ops bake the vg image handle into the recorded command
        // list; when any registry handle is recreated (imageEpoch
        // moves), an item that references images re-records.
        bool usesImages = false;
        uint32_t imageEpoch = 0;
    };

    std::map<ImageId, PageImage> images;
    uint32_t imageEpoch = 1;

    std::map<ItemId, Item> items;
    std::vector<Item*> drawOrder;
    bool orderDirty = false;
    uint64_t nextSeq = 0;
    float lastBandScale = 0.0f;
    // The Vg2D generation the item handles were created under: when the
    // vg context is destroyed and rebuilt, every handle is dead and the
    // items must forget them rather than submit into the new context.
    uint32_t vgGeneration = 0;

    void releaseList(Item& item)
    {
#ifdef HAVE_BGFX
        if (item.list != kNoList && Vg2D::instance().initialized()
            && vgGeneration == Vg2D::instance().generation()) {
            vg::CommandListHandle handle {item.list};
            vg::destroyCommandList(Vg2D::instance().context(), handle);
        }
#endif
        item.list = kNoList;
        item.recorded = false;
    }

    // The interactive compositor's persistent render target (desktop
    // path; raw bgfx handle indices so the storage builds without the
    // bgfx headers). 0xffff = none.
    uint16_t rtFb = 0xffff;
    uint16_t rtColor = 0xffff;
    uint16_t rtDepth = 0xffff;
    uint16_t rtW = 0;
    uint16_t rtH = 0;

    void releaseTarget()
    {
#ifdef HAVE_BGFX
        // A generation mismatch means the device the handles lived on
        // is gone (Vg2D tears down right before bgfx does): forgetting
        // them is the only legal move.
        if (Vg2D::instance().initialized()
            && vgGeneration == Vg2D::instance().generation()) {
            if (rtFb != 0xffff) {
                bgfx::FrameBufferHandle h {rtFb};
                bgfx::destroy(h);
            }
            if (rtColor != 0xffff) {
                bgfx::TextureHandle h {rtColor};
                bgfx::destroy(h);
            }
            if (rtDepth != 0xffff) {
                bgfx::TextureHandle h {rtDepth};
                bgfx::destroy(h);
            }
        }
#endif
        rtFb = rtColor = rtDepth = 0xffff;
        rtW = rtH = 0;
    }

    void releaseImage(PageImage& img)
    {
#ifdef HAVE_BGFX
        if (img.handle != kNoImage && Vg2D::instance().initialized()
            && vgGeneration == Vg2D::instance().generation()) {
            vg::ImageHandle handle {img.handle};
            vg::destroyImage(Vg2D::instance().context(), handle);
        }
#endif
        img.handle = kNoImage;
        img.uploaded = 0;
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
    PushTransform,
    PopTransform,
    Image,
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
    uint64_t u64()
    {
        uint64_t v;
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

void Page2D::Recorder::pushTransform(const float mtx[6])
{
    if (!mtx)
        return;
    putOp(ops, Op::PushTransform);
    for (int i = 0; i < 6; ++i)
        putF(ops, mtx[i]);
}

void Page2D::Recorder::popTransform()
{
    putOp(ops, Op::PopTransform);
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

void Page2D::Recorder::image(ImageId id, float x, float y, float w, float h)
{
    putOp(ops, Op::Image);
    putBytes(ops, &id, sizeof(id));
    putF(ops, x);
    putF(ops, y);
    putF(ops, w);
    putF(ops, h);
}

#ifdef HAVE_BGFX

static vg::Color decodeColor(uint32_t rgba)
{
    return vg::color4ub((uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16),
                        (uint8_t)(rgba >> 8), (uint8_t)rgba);
}

// The fixed payload bytes that must follow each opcode (the variable
// parts -- polyline points, triangle arrays, text -- are checked where
// they are read). This buffer is the M3 wire format, so replay treats
// it as untrusted input: a short or corrupt buffer stops the replay, it
// never reads past the end.
static size_t opFixedSize(Op op)
{
    switch (op) {
    case Op::BeginPath:
    case Op::ClosePath:
        return 0;
    case Op::MoveTo:
    case Op::LineTo:
    case Op::Stroke:
        return 8;
    case Op::CubicTo:
    case Op::FillLinearGradient:
        return 24;
    case Op::QuadraticTo:
    case Op::Rect:
    case Op::Ellipse:
        return 16;
    case Op::Arc:
        return 21;
    case Op::Circle:
        return 12;
    case Op::Polyline:
    case Op::FillConvex:
    case Op::Triangles:
        return 4;
    case Op::FillConcave:
        return 5;
    case Op::Text:
        return 1;
    case Op::PushTransform:
        return 24;
    case Op::PopTransform:
        return 0;
    case Op::Image:
        return 24;
    }
    return SIZE_MAX; // unknown opcode: newer writer, stop
}

// What replay needs beyond the vg context: the image registry to
// resolve image ops against, and whether any image op was seen (valid
// or not -- a not-yet-registered image must still re-record the item
// once it arrives).
struct ReplayEnv
{
    const std::map<Page2D::ImageId, PageImage>* images = nullptr;
    bool usedImages = false;
};

// Replay one op buffer into the currently recording vg command list.
// stateDepth counts unmatched PushTransform states; the caller pops
// what is left so a truncated or malformed buffer cannot leak state
// pushes into the next item.
static void replayOpsInner(vg::Context* ctx, OpReader& r, int& stateDepth,
                           ReplayEnv& env)
{
    while (!r.done()) {
        const Op op = (Op)r.u8();
        const size_t need = opFixedSize(op);
        if (need == SIZE_MAX || !r.fits(need))
            return;
        switch (op) {
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
            if (n < 2 || !r.fits(sizeof(float) * 2 * n))
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
            if (!r.fits((size_t)nameLen + 20)) // name + size/color/x/y/len
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
            if (!nv || !r.fits(sizeof(float) * 2 * nv + 4))
                return;
            std::vector<float> pos(2 * nv);
            memcpy(pos.data(), r.p, sizeof(float) * 2 * nv);
            r.p += sizeof(float) * 2 * nv;
            uint32_t ni = r.u32();
            if (!ni || ni % 3 || !r.fits(sizeof(uint16_t) * ni + 4))
                return;
            std::vector<uint16_t> idx(ni);
            memcpy(idx.data(), r.p, sizeof(uint16_t) * ni);
            r.p += sizeof(uint16_t) * ni;
            for (uint16_t i : idx) {
                if (i >= nv)
                    return;
            }
            vg::Color c = decodeColor(r.u32());
            vg::ImageHandle noImage = VG_INVALID_HANDLE;
            vg::indexedTriList(ctx, pos.data(), nullptr, nv, &c, 1, idx.data(),
                               ni, noImage);
            break;
        }
        case Op::PushTransform: {
            // Bound the depth: this is wire input, and vg's state stack
            // is finite (32 entries, shared with the submit machinery).
            if (stateDepth >= 16)
                return;
            float m[6];
            for (int i = 0; i < 6; ++i)
                m[i] = r.f();
            vg::pushState(ctx);
            vg::transformMult(ctx, m, vg::TransformOrder::Post);
            ++stateDepth;
            break;
        }
        case Op::PopTransform:
            if (stateDepth <= 0)
                return; // malformed: more pops than pushes
            vg::popState(ctx);
            --stateDepth;
            break;
        case Op::Image: {
            uint64_t id = r.u64();
            float x = r.f(), y = r.f(), w = r.f(), h = r.f();
            env.usedImages = true;
            if (!env.images || !(w > 0.0f) || !(h > 0.0f))
                break;
            auto it = env.images->find(id);
            if (it == env.images->end() || it->second.handle == 0xffff)
                break;
            vg::ImageHandle img {it->second.handle};
            vg::ImagePatternHandle pattern =
                vg::createImagePattern(ctx, x, y, w, h, 0.0f, img);
            if (!vg::isValid(pattern))
                break;
            vg::beginPath(ctx);
            vg::rect(ctx, x, y, w, h);
            vg::fillPath(ctx, pattern, vg::color4ub(255, 255, 255, 255),
                         vg::FillFlags::ConvexAA);
            break;
        }
        default:
            // Unknown op: the buffer is from a newer writer; stop rather
            // than misparse the rest.
            return;
        }
    }
}

static void replayOps(vg::Context* ctx, const std::vector<uint8_t>& ops,
                      ReplayEnv& env)
{
    OpReader r {ops.data(), ops.data() + ops.size()};
    int stateDepth = 0;
    replayOpsInner(ctx, r, stateDepth, env);
    while (stateDepth-- > 0)
        vg::popState(ctx);
}

#endif // HAVE_BGFX

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
    for (auto& v : d->images)
        d->releaseImage(v.second);
    d->images.clear();
    d->releaseTarget();
}

void Page2D::setImage(ImageId id, uint16_t width, uint16_t height,
                      const uint8_t* rgba, bool repeat)
{
    if (!width || !height || !rgba) {
        removeImage(id);
        return;
    }
    PageImage& img = d->images[id];
    // A size or sampler change needs a new texture; releasing the
    // handle here makes the upload pass create one (and the epoch move
    // re-records the items that baked the old handle).
    if (img.handle != Private::kNoImage
        && (img.width != width || img.height != height
            || img.repeat != repeat))
        d->releaseImage(img);
    img.width = width;
    img.height = height;
    img.repeat = repeat;
    img.pixels.assign(rgba, rgba + (size_t)width * height * 4);
    ++img.version;
}

void Page2D::removeImage(ImageId id)
{
    auto it = d->images.find(id);
    if (it == d->images.end())
        return;
    const bool hadHandle = it->second.handle != Private::kNoImage;
    d->releaseImage(it->second);
    d->images.erase(it);
    // Items whose ops still reference the id must drop it from their
    // command lists rather than submit a destroyed handle.
    if (hadHandle)
        ++d->imageEpoch;
}

bool Page2D::hasImage(ImageId id) const
{
    return d->images.count(id) != 0;
}

float Page2D::bandScale(float zoom)
{
    if (!(zoom > 1e-6f))
        zoom = 1e-6f;
    return exp2f(roundf(log2f(zoom)));
}

#ifdef HAVE_BGFX

bool Page2D::render(uint16_t viewId, uint16_t width, uint16_t height)
{
    Vg2D& vg2d = Vg2D::instance();
    if (!vg2d.initialized() && !vg2d.init())
        return false;
    vg::Context* ctx = vg2d.context();

    // A destroyed-and-rebuilt vg context invalidated every command list
    // handle: forget them (nothing to destroy, the context took them
    // along) and re-record from the retained ops.
    if (d->vgGeneration != vg2d.generation()) {
        for (auto& v : d->items) {
            v.second.list = Private::kNoList;
            v.second.recorded = false;
        }
        for (auto& v : d->images) {
            v.second.handle = Private::kNoImage;
            v.second.uploaded = 0;
        }
        d->vgGeneration = vg2d.generation();
    }

    // Upload pass: bring every registered image's texture in sync with
    // its retained pixels. Same-size damage updates the texture in
    // place (recorded command lists stay valid); a new or recreated
    // handle moves the epoch so referencing items re-record below.
    for (auto& v : d->images) {
        PageImage& img = v.second;
        if (img.uploaded == img.version && img.handle != Private::kNoImage)
            continue;
        if (img.handle != Private::kNoImage) {
            vg::ImageHandle handle {img.handle};
            vg::updateImage(ctx, handle, 0, 0, img.width, img.height,
                            img.pixels.data());
        }
        else {
            uint32_t flags = vg::ImageFlags::Filter_Bilinear;
            if (!img.repeat)
                flags |= vg::ImageFlags::Clamp_UV;
            vg::ImageHandle handle = vg::createImage(
                ctx, img.width, img.height, flags, img.pixels.data());
            if (!vg::isValid(handle))
                continue; // out of image slots; retried next render
            img.handle = handle.idx;
            ++d->imageEpoch;
        }
        img.uploaded = img.version;
        ++stats.imageUploads;
    }

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
        // Placeholder items (a feed keeps ids contiguous by storing
        // empty content for skipped geometry) cost nothing here.
        if (item->ops.empty())
            continue;
        if (item->recorded && item->usesImages
            && item->imageEpoch != d->imageEpoch)
            item->recorded = false;
        vg::CommandListHandle list {item->list};
        if (!item->recorded) {
            if (!vg::isValid(list)) {
                list = vg::createCommandList(ctx,
                                             vg::CommandListFlags::Cacheable);
                if (!vg::isValid(list)) {
                    // Out of command list slots: skip rather than hand vg
                    // an invalid handle. The counter keeps it visible.
                    ++stats.droppedItems;
                    continue;
                }
                item->list = list.idx;
            }
            else
                vg::resetCommandList(ctx, list);
            vg::beginCommandList(ctx, list);
            ReplayEnv env;
            env.images = &d->images;
            replayOps(ctx, item->ops, env);
            vg::endCommandList(ctx);
            item->recorded = true;
            item->usesImages = env.usedImages;
            item->imageEpoch = d->imageEpoch;
            ++stats.itemRecords;
        }
        vg::submitCommandList(ctx, list);
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
        if (bgfx::init(init)) {
            _offscreenDeviceUp = true;
        }
        else if (bgfx::getCaps()->limits.maxViews == 0) {
            // init() refuses both when a device already exists (fine,
            // share it) and when it genuinely cannot come up (no
            // Vulkan ICD, ...). getCaps() returns the zero-initialized
            // global until some init succeeds, so a zero view limit
            // tells the two apart.
            return false;
        }
    }

    // On a shared Qt-GL device (the interactive compositor's, or a 3D
    // view's) the frames pumped below execute GL and need the device's
    // own context current -- the headless devices this path brings up
    // itself (Vulkan) need nothing. Callers of this offscreen entry
    // hold no GL context of their own.
    const bool sharedGL = RendererFactory::deviceSharesQtGL();
    if (sharedGL && !RendererFactory::deviceMakeCurrent())
        return false;

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
    // ceiling. The 3D renderer's block allocator keeps its hands off
    // the top granule (BGFXRendererLibP::reserveBlock reserves it for
    // exactly this), so these ids never collide with a live viewer on
    // a shared device.
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
    // The frame buffer was created without texture ownership: the
    // render targets are destroyed here or they leak per call.
    bgfx::destroy(color);
    bgfx::destroy(depth);
    bgfx::destroy(staging);
    if (sharedGL)
        RendererFactory::deviceDoneCurrent();

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

uintptr_t Page2D::renderToTexture(uint16_t width, uint16_t height)
{
    if (!width || !height)
        return 0;
    // GL device in Qt's share group or nothing: Vulkan (or a headless
    // device renderOffscreen brought up) cannot hand a texture to a Qt
    // GL widget, and the standalone/wasm tier never composites -- it
    // renders straight into a backbuffer view.
    if (!RendererFactory::deviceSharesQtGL())
        return 0;

    // A torn-down-and-rebuilt device took the target's handles with
    // it; render() below re-syncs the items and images the same way.
    if (Vg2D::instance().generation() != d->vgGeneration) {
        d->rtFb = d->rtColor = d->rtDepth = 0xffff;
        d->rtW = d->rtH = 0;
    }
    if (d->rtFb != 0xffff && (d->rtW != width || d->rtH != height))
        d->releaseTarget();
    if (d->rtFb == 0xffff) {
        bgfx::TextureHandle color = bgfx::createTexture2D(
            width, height, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_RT);
        bgfx::TextureHandle depth = bgfx::createTexture2D(
            width, height, false, 1, bgfx::TextureFormat::D24S8,
            BGFX_TEXTURE_RT_WRITE_ONLY);
        if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
            if (bgfx::isValid(color))
                bgfx::destroy(color);
            if (bgfx::isValid(depth))
                bgfx::destroy(depth);
            return 0;
        }
        bgfx::TextureHandle attachments[] = {color, depth};
        bgfx::FrameBufferHandle fb =
            bgfx::createFrameBuffer(2, attachments, false);
        if (!bgfx::isValid(fb)) {
            bgfx::destroy(color);
            bgfx::destroy(depth);
            return 0;
        }
        d->rtColor = color.idx;
        d->rtDepth = depth.idx;
        d->rtFb = fb.idx;
        d->rtW = width;
        d->rtH = height;
    }

    // Same view id pair as renderOffscreen: the top granule the 3D
    // renderer's block allocator leaves alone.
    const uint16_t viewId = (uint16_t)(bgfx::getCaps()->limits.maxViews - 2);
    bgfx::FrameBufferHandle fb {d->rtFb};
    bgfx::setViewFrameBuffer(viewId, fb);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    // Transparent clear: the host paints the sheet and backdrop itself
    // and composites this layer with premultiplied alpha (vg's
    // src-alpha blend over transparent black accumulates exactly
    // that).
    bgfx::setViewClear(viewId,
                       BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                       0x00000000, 1.0f, 0);

    const bool ok = render(viewId, width, height);
    bgfx::touch(viewId);

    // The single-threaded GL device executes the frame in whichever
    // context is current; the caller released its own before calling.
    if (!RendererFactory::deviceMakeCurrent())
        return 0;
    bgfx::frame();
    RendererFactory::deviceDoneCurrent();

    bgfx::FrameBufferHandle noFb = BGFX_INVALID_HANDLE;
    bgfx::setViewFrameBuffer(viewId, noFb);
    if (!ok)
        return 0;
    bgfx::TextureHandle color {d->rtColor};
    return bgfx::getInternal(color);
}

void Page2D::registerFont(const char* name, const char* path)
{
    if (name && path)
        Vg2D::instance().loadFontFile(name, path);
}

#else // !HAVE_BGFX

bool Page2D::render(uint16_t, uint16_t, uint16_t)
{
    return false;
}

bool Page2D::renderOffscreen(uint16_t, uint16_t, std::vector<uint8_t>&)
{
    return false;
}

uintptr_t Page2D::renderToTexture(uint16_t, uint16_t)
{
    return 0;
}

void Page2D::registerFont(const char*, const char*) {}

#endif // HAVE_BGFX
