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

#ifndef RENDERER_MASKED_OCCLUSION_H
#define RENDERER_MASKED_OCCLUSION_H

/// A masked software occlusion depth buffer
/// (docs/FarFieldProxies.md §12.8 step 3, §12.12) — the CPU oracle that
/// replaces the hardware occlusion query.
///
/// **Why this exists at all, when a hardware query already works.** The
/// query mechanism is built, correct per test in isolation, and it still
/// deletes visible geometry. §12.11 closed the last way to explain that
/// away: with one query handle created per test and destroyed after its
/// read, a box that reports zero samples cannot be reading somebody
/// else's zero — and the worst nodes still report `lastpx0 age1f`, a box
/// that rasterized nothing one frame ago for contents that are plainly
/// on screen. That is §12.6's account standing on a measurement: a node
/// re-tested *after* the pass that wrote its own contents is asked to
/// win a depth comparison against itself, and loses. Padding, hysteresis
/// and freshness policy were each tried and none of them reach it,
/// because none of them change *when the question can be asked*.
///
/// A CPU depth buffer changes exactly that. Occluders are rasterized and
/// queries answered against the same buffer in whatever order the caller
/// likes, so a node can be tested *before* its own geometry has been
/// added; and the answer arrives inside the frame that asked, so there
/// is no window in which the world moves underneath a verdict. Intel's
/// HPG 2016 paper is explicit about both properties — "doesn't introduce
/// any latency into the system", "supports interleaving occluder
/// rasterization and occlusion queries without penalty" — and both of
/// this workstream's failure modes are absent by construction rather
/// than by policy.
///
/// **What is masked about it.** A full-resolution depth buffer at
/// 1863x1064 is 8 MB to clear and touch per frame. The paper's structure
/// keeps, per 8x4 block of pixels, *two* depth values and a 32-bit
/// coverage mask saying which pixels belong to the nearer of them —
/// 12 bytes per 32 pixels, a 21x reduction — and reports culling within
/// 2% of what the full-resolution buffer would achieve. The two layers
/// are what make it work where a plain hierarchical-Z minimum does not:
/// a single conservative minimum per block is destroyed by one distant
/// fragment, whereas a partially covered block can hold the new surface
/// separately until coverage completes and it can be promoted.
///
/// **The one invariant everything here is built to keep.** For every
/// pixel, the depth this buffer stores is *no nearer* than the true
/// nearest surface at that pixel. Every heuristic below may throw
/// information away and every one of them does; none may ever claim a
/// surface is nearer than it is. Violated, this over-culls, which is the
/// bug the whole section exists to remove; kept, the mechanism can only
/// ever fail by drawing something it could have skipped. `MaskedDepth`
/// is therefore safe to be wrong and the tests beside it assert that
/// direction rather than an exact image.
///
/// ⚠️ Nothing here knows about bgfx, Coin, Qt or OCCT and the arithmetic
/// is plain float, for the reason SceneLadder.h and ProxyHierarchy.h
/// keep the same discipline: this is the tier that has to behave
/// *identically* in the browser. A GPU query is a different mechanism on
/// WebGL2 with a different latency; a software rasterizer is the same
/// code, and WASM has SIMD128 for the day the scalar version below is
/// measured to be the bottleneck.

#include <cstdint>
#include <vector>

#include "Renderer.h"

namespace Render {

/// What a query could establish. ⚠️ `Visible` is the answer given
/// whenever the buffer *cannot* answer — an empty buffer, a box crossing
/// the near plane, a degenerate rect. Every unknown resolves to drawing.
enum class OccludeAnswer : uint8_t {
    Visible,    ///< something at or nearer than the query survived
    Occluded,   ///< every block covering it is filled by nearer surfaces
    Offscreen,  ///< projected outside the buffer entirely
};

/// What one frame's rasterization did, reported rather than inferred
/// (docs/RenderDebug.md §1).
struct MaskedOcclusionStats {
    uint32_t trianglesIn = 0;        ///< offered by the caller
    uint32_t trianglesCulled = 0;    ///< degenerate, backfacing or off-buffer
    uint32_t trianglesClipped = 0;   ///< crossed the near plane and were cut
    uint32_t trianglesDrawn = 0;     ///< reached block traversal
    uint32_t blocksTouched = 0;      ///< block updates attempted
    uint32_t blocksUpdated = 0;      ///< block updates that changed something
    uint32_t queries = 0;
    uint32_t queriesOccluded = 0;
    uint32_t queriesOffscreen = 0;
    /// ⭐ Queries refused because the box crosses the near plane, and so
    /// has no bounded screen rect to test. Separated from `queries`
    /// because it is the trap that made the GPU probe report the whole
    /// model hidden (see `boxReachesNearPlane`), and a mechanism that
    /// silently answers "visible" for half the scene looks identical to
    /// one that is working.
    uint32_t queriesNearPlane = 0;
};

/// The two-layer masked depth buffer.
///
/// Usage within a frame is: `resize` once, then per frame `clear`,
/// `setCamera`, some number of `rasterize` calls, and any number of
/// `testBox`/`testRect` calls interleaved with them in any order. The
/// interleaving is the point — see the class comment.
class RendererExport MaskedDepth
{
public:
    /// A block is 8x4 pixels so that its coverage is exactly one
    /// uint32_t, which is what makes the mask free to test and merge.
    /// ⚠️ Do not "tune" these: 32 bits per block is the structure, not a
    /// parameter.
    static const int BlockW = 8;
    static const int BlockH = 4;

    /// Round \a widthPx x \a heightPx up to whole blocks and drop all
    /// contents. Sizes at or below zero empty the buffer, which then
    /// answers `Visible` to everything.
    void resize(int widthPx, int heightPx);
    int width() const { return bufw; }
    int height() const { return bufh; }
    bool empty() const { return blocks.empty(); }

    /// Forget every occluder. Cheap — 12 bytes per 32 pixels — but not
    /// free, and it is what must run once per frame.
    void clear();

    /// The camera every later call is relative to.
    ///
    /// \a view and \a proj are GL-layout (column-major) 4x4, as
    /// Renderer::render receives them. \a homogeneousDepth selects the
    /// clip convention for near-plane clipping: true for OpenGL's -w..w
    /// depth range, false for the 0..w of D3D, Vulkan and Metal. An
    /// orthographic projection is recognised by `proj[15] != 0`, the
    /// same test `sightBounds` uses.
    ///
    /// ⚠️ Calling this does *not* clear the buffer: a caller that moves
    /// the camera without clearing gets occluders from two cameras mixed,
    /// which breaks the invariant. `clear()` first, always.
    void setCamera(const float *view, const float *proj,
                   bool homogeneousDepth);

    /// Rasterize an indexed triangle list as occluders.
    ///
    /// \a positions is 3 floats per vertex, \a stride bytes apart
    /// (0 meaning tightly packed). \a model is a GL-layout matrix taking
    /// those positions to world space, or null for identity.
    ///
    /// ⭐ **Two-sided by design.** Back-face culling would halve the
    /// work, and every renderer does it, but it assumes consistent
    /// winding — which tessellated B-Rep out of a CAD kernel does not
    /// reliably have, and a wrongly culled occluder face is a *hole* in
    /// the depth buffer that under-culls silently. A back face is a real
    /// surface at a real depth; rasterizing it costs time and cannot
    /// cost correctness. `setTwoSided(false)` is available for callers
    /// that know their winding.
    void rasterize(const float *positions, size_t stride, size_t vertexCount,
                   const uint32_t *indices, size_t indexCount,
                   const float *model = nullptr);

    /// The same, for an unindexed triangle list.
    void rasterize(const float *positions, size_t stride, size_t vertexCount,
                   const float *model = nullptr);

    /// Whether a world-space axis-aligned box could have reached the
    /// screen given the occluders rasterized so far.
    ///
    /// ⚠️ A box that reaches the near plane answers `Visible` and is
    /// counted in `queriesNearPlane`: its projection is unbounded, so
    /// there is no rect to test. This is the same exemption the GPU path
    /// needs (`boxReachesNearPlane`) and for a related reason — the
    /// difference being that here it is a missing *rect* rather than a
    /// box whose front faces have been clipped away.
    OccludeAnswer testBox(const float *bboxMin, const float *bboxMax) const;

    /// The primitive `testBox` reduces to: is the screen rect
    /// [\a x0,\a x1] x [\a y0,\a y1] wholly behind what has been
    /// rasterized, given that nothing in it is nearer than \a depthNear?
    ///
    /// Depths are this class's internal convention (larger is nearer);
    /// `boxDepthNear` produces one. Pixel coordinates have y increasing
    /// upwards, matching NDC rather than a window system.
    ///
    /// ⭐ The comparison is strict, so a surface exactly coincident with
    /// the query answers `Visible`. That tie is the one §12.6 lost: a
    /// node whose own geometry is already in the buffer must not be able
    /// to hide itself.
    OccludeAnswer testRect(float x0, float y0, float x1, float y1,
                           float depthNear) const;

    /// The screen rect and nearest depth of a world-space box, in the
    /// form `testRect` wants. Returns false if the box reaches the near
    /// plane, i.e. cannot be reduced to a bounded rect.
    ///
    /// Exposed because a caller that already holds the projection (the
    /// culler's walk does) should not pay for it twice, and because the
    /// tests need to ask about a rect without inventing a box.
    bool projectBox(const float *bboxMin, const float *bboxMax, float *rect,
                    float *depthNear) const;

    /// Whether back faces are rasterized (default true — see
    /// `rasterize`).
    void setTwoSided(bool on) { twosided = on; }
    bool twoSided() const { return twosided; }

    const MaskedOcclusionStats &stats() const { return framestats; }
    void resetStats() { framestats = MaskedOcclusionStats(); }

    /// The depth stored for one pixel, in the internal convention, or 0
    /// where nothing has been rasterized. For tests and the debug
    /// readout only — the mechanism never reads a single pixel.
    float pixelDepth(int x, int y) const;

    /// The conservative floor of the block containing (\a x, \a y): no
    /// surface in that block is farther than this. For tests.
    float blockFloor(int x, int y) const;

private:
    /// One 8x4 block: two depth layers and the mask saying which pixels
    /// belong to layer 1. A pixel with its bit clear is at `z0`.
    ///
    /// The invariant, which every path below preserves and the tests
    /// assert: the value a pixel resolves to is never nearer than the
    /// true nearest surface at that pixel.
    struct Block {
        float z0;       ///< the floor: no pixel in the block is farther
        float z1;       ///< the nearer layer, valid where `mask` is set
        uint32_t mask;  ///< bit (row * 8 + column), row 0 lowest
    };

    /// One source triangle: transform, near-clip, project, fan out.
    void emitTriangle(const float *pa, const float *pb, const float *pc,
                      const float *mvp);
    void rasterTri(const double *sx, const double *sy, const double *sd);
    void updateBlock(int bx, int by, uint32_t coverage, float ztri);
    static float floorOf(const Block &b);

    std::vector<Block> blocks;
    int bufw = 0;          ///< usable pixels, a multiple of BlockW
    int bufh = 0;
    int bw = 0;            ///< blocks across
    int bh = 0;
    bool twosided = true;
    bool homogeneous = true;
    bool orthographic = false;
    float viewproj[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    mutable MaskedOcclusionStats framestats;
};

}  // namespace Render

#endif  // RENDERER_MASKED_OCCLUSION_H
