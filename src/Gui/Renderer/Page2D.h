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

#ifndef RENDERER_PAGE2D_H
#define RENDERER_PAGE2D_H

/// The retained 2D page store (docs/TechDrawPortAndSection.md sec 16,
/// milestone M1). A Page2D holds items keyed by stable ids; each item's
/// content is an op buffer -- a compact, position-independent capture of
/// vg draw commands in page coordinates. That buffer is the retained
/// representation: damage re-records only the touched item's vg command
/// list from its ops, and (M3) the same bytes are what a page streams
/// over the wire.
///
/// Zoom bands: the page-to-screen zoom is split into a power-of-two band
/// scale, applied in the vg state transform, and a bounded residual
/// (1/sqrt(2)..sqrt(2)), applied with pan and rotation in the bgfx view
/// matrix. vg's command-list cache keys on the state's average scale, so
/// within a band every unchanged item replays cached tessellation, pan
/// and rotation included; crossing a band changes the vg-side scale and
/// vg re-tessellates each list itself at the new scale, keeping the AA
/// fringe and flattening tolerance honest.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Renderer.h"

namespace Render {

class RendererExport Page2D
{
public:
    using ItemId = uint64_t;

    /// Draw order between items of the same layer; also what a feed or
    /// a picker can use to tell geometry classes apart.
    enum class Kind : uint8_t {
        Face = 0,
        Decoration, // hatches, section lines, center marks, ...
        Edge,
        Vertex,
        Annotation,
    };

    /// Page-to-screen view: screen = translate(panX, panY) * rotate *
    /// scale(zoom) * pagePoint. Pixels; rotation in radians, positive
    /// turning the page clockwise on screen (y-down, Qt convention).
    struct View
    {
        float panX = 0.0f;
        float panY = 0.0f;
        float zoom = 1.0f;
        float rotation = 0.0f;
        float devicePixelRatio = 1.0f;
    };

    /// Records one item's content in page coordinates. Filled by the
    /// feed, handed to setItem(); the page keeps the bytes.
    class Recorder
    {
    public:
        void beginPath();
        void moveTo(float x, float y);
        void lineTo(float x, float y);
        void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y);
        void quadraticTo(float cx, float cy, float x, float y);
        void arc(float cx, float cy, float r, float a0, float a1, bool clockwise);
        void closePath();
        void rect(float x, float y, float w, float h);
        void circle(float cx, float cy, float r);
        void ellipse(float cx, float cy, float rx, float ry);
        void polyline(const float* xy, uint32_t numPoints);

        /// Fill the current path. Convex fills tessellate cheaper; a
        /// concave fill goes through libtess2 with the given rule.
        void fillConvex(uint32_t rgba);
        void fillConcave(uint32_t rgba, bool evenOdd = false);
        void fillLinearGradient(float sx, float sy, float ex, float ey,
                                uint32_t rgbaStart, uint32_t rgbaEnd);
        void stroke(uint32_t rgba, float width);

        /// UTF-8 text anchored at x/y. The font is a Vg2D registry
        /// name; an unregistered name draws nothing.
        void text(const char* font, float size, uint32_t rgba, float x, float y,
                  const char* utf8);

        /// Push a coordinate transform: ops until the matching pop see
        /// their coordinates mapped by the 2x3 affine [m11 m12 m21 m22
        /// dx dy] (column-vector: x' = m11*x + m21*y + dx), composed
        /// under the page view. Rotated or scaled text is the intended
        /// use; plain geometry is cheaper pre-mapped by the feed.
        /// Pushes must balance within the item; replay pops leftovers.
        void pushTransform(const float mtx[6]);
        void popTransform();

        /// Pre-triangulated geometry (holed face fills arrive here).
        /// Solid color across all vertices.
        void triangles(const float* xy, uint32_t numVertices,
                       const uint16_t* indices, uint32_t numIndices,
                       uint32_t rgba);

        bool empty() const { return ops.empty(); }
        const std::vector<uint8_t>& bytes() const { return ops; }

    private:
        friend class Page2D;
        std::vector<uint8_t> ops;
    };

    Page2D();
    ~Page2D();
    Page2D(const Page2D&) = delete;
    Page2D& operator=(const Page2D&) = delete;

    /// Define or replace (= damage) an item. Layer orders items
    /// (ascending) before kind and insertion order do.
    void setItem(ItemId id, Kind kind, uint32_t layer, Recorder&& content);
    void removeItem(ItemId id);
    bool hasItem(ItemId id) const;
    void clear();

    void setView(const View& v) { pageView = v; }
    const View& view() const { return pageView; }

    /// Draw the page into the bgfx view id: the full vg cycle
    /// (begin / submit each item's command list / end / frame) plus the
    /// bgfx view and projection matrices. Initializes Vg2D on first
    /// use; a false return means no vg context (bgfx not up).
    bool render(uint16_t viewId, uint16_t width, uint16_t height);

    struct Counters
    {
        uint32_t itemRecords = 0;  // op buffers replayed into command lists
        uint32_t listSubmits = 0;  // command lists submitted to vg
        uint32_t bandCrossings = 0;// renders whose band differs from the last
        uint32_t droppedItems = 0; // no command list slot left (uint16 space)
    };
    const Counters& counters() const { return stats; }

    /// The band scale the current view quantizes to (exposed for tests).
    static float bandScale(float zoom);

    /// Register a font file under the registry name text ops refer to.
    /// Legal any time -- before any GPU context exists, and again after
    /// one is torn down; the bytes are retained and the font follows
    /// every context. Re-registering a name is a no-op.
    static void registerFont(const char* name, const char* path);

    /// Offscreen convenience for verification hosts and tools: ensure a
    /// bgfx device exists (bringing one up headless if nothing did),
    /// render this page once into a private offscreen target and return
    /// tightly packed RGBA8 pixels, row 0 on top. Not a per-frame path.
    /// Fails when bgfx cannot come up -- or was brought up by someone
    /// else in a state we cannot verify; test hosts run without the 3D
    /// renderer active.
    bool renderOffscreen(uint16_t width, uint16_t height,
                         std::vector<uint8_t>& rgba);

private:
    // All vg types stay out of this header: consumers of the page
    // (the TechDraw feed, the wire) see only ids, ops and pixels.
    struct Private;
    std::unique_ptr<Private> d;
    View pageView;
    Counters stats;
};

} // namespace Render

#endif // RENDERER_PAGE2D_H
