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
/// (docs/FarFieldProxies.md section 12.8 step 3, section 12.12) -- the
/// CPU oracle that replaces the hardware occlusion query.
///
/// **Why this exists at all, when a hardware query already works.** The
/// query mechanism is built, correct per test in isolation, and it still
/// deletes visible geometry. section 12.11 closed the last way to explain that
/// away: with one query handle created per test and destroyed after its
/// read, a box that reports zero samples cannot be reading somebody
/// else's zero -- and the worst nodes still report `lastpx0 age1f`, a box
/// that rasterized nothing one frame ago for contents that are plainly
/// on screen. That is section 12.6's account standing on a measurement: a node
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
/// HPG 2016 paper is explicit about both properties -- "doesn't introduce
/// any latency into the system", "supports interleaving occluder
/// rasterization and occlusion queries without penalty" -- and both of
/// this workstream's failure modes are absent by construction rather
/// than by policy.
///
/// **What is masked about it.** A full-resolution depth buffer at
/// 1863x1064 is 8 MB to clear and touch per frame. The paper's structure
/// keeps, per 8x4 block of pixels, *two* depth values and a 32-bit
/// coverage mask saying which pixels belong to the nearer of them --
/// 12 bytes per 32 pixels, a 21x reduction -- and reports culling within
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
/// WARNING: Nothing here knows about bgfx, Coin, Qt or OCCT, for the reason
/// SceneLadder.h and ProxyHierarchy.h keep the same discipline: this is
/// the tier that has to behave *identically* in the browser. A GPU query
/// is a different mechanism on WebGL2 with a different latency; a
/// software rasterizer is the same code.
///
/// That is also why the vector pre-pass added in section 12.14 is 128 bits
/// wide and not 256: SIMD128 is what WebAssembly has, so the desktop
/// runs the width the browser runs (Gui/Renderer/Simd4.h). It sits in
/// front of the exact path rather than replacing it -- see
/// `setSimdFilter` for why that is not a compromise but the reason a
/// float pre-pass is admissible at all in a file whose entire claim is a
/// one-sided bound on stored depth.

#include <cstdint>
#include <utility>
#include <vector>

#include "ProxyHierarchy.h"
#include "Renderer.h"

namespace Render {

/// What a query could establish. WARNING: `Visible` is the answer given
/// whenever the buffer *cannot* answer -- an empty buffer, a box crossing
/// the near plane, a degenerate rect. Every unknown resolves to drawing.
enum class OccludeAnswer : uint8_t {
    Visible,    ///< something at or nearer than the query survived
    Occluded,   ///< every block covering it is filled by nearer surfaces
    Offscreen,  ///< projected outside the buffer entirely
};

/// What one frame's rasterization did, reported rather than inferred
/// (docs/RenderDebug.md section 1).
struct MaskedOcclusionStats {
    uint32_t trianglesIn = 0;        ///< offered by the caller
    uint32_t trianglesClipped = 0;   ///< crossed the near plane and were cut
    uint32_t trianglesDrawn = 0;     ///< reached block traversal
    /// KEY: Why a triangle contributed nothing, split four ways rather
    /// than counted once. On the benchmark scene two thirds of the
    /// rasterized triangles land in these buckets, and which bucket
    /// decides what is worth optimizing: geometry that is off the buffer
    /// wants a cheaper reject before the perspective divide, geometry
    /// that is merely too small wants coarser occluders, and the two
    /// look identical under one counter.
    uint32_t trianglesOffBuffer = 0;   ///< outside the viewport entirely
    uint32_t trianglesSubPixel = 0;    ///< on the buffer, over no pixel centre
    uint32_t trianglesBackFacing = 0;  ///< only when two-sided is off
    uint32_t trianglesDegenerate = 0;  ///< zero screen area
    uint32_t trianglesBehind = 0;      ///< wholly behind the near plane
    /// Of the culled triangles, those the four-wide vector pre-pass
    /// discarded on its own, so the exact path never saw them. This is
    /// the counter that says whether the vector path is doing anything:
    /// it is bounded above by `trianglesOffBuffer + trianglesSubPixel`,
    /// and if it is far below that sum the batches are being declined
    /// rather than judged.
    uint32_t trianglesFiltered = 0;
    /// Triangles the vector pre-pass refused to judge -- near the near
    /// plane, or projected outside its guard band -- and handed to the
    /// exact path unclassified. Every one of these pays for both paths,
    /// so it is the cost side of the same measurement.
    uint32_t trianglesGuarded = 0;
    /// Every triangle that did not reach block traversal.
    uint32_t trianglesCulled() const
    {
        return trianglesOffBuffer + trianglesSubPixel + trianglesBackFacing
                + trianglesDegenerate + trianglesBehind;
    }
    uint32_t blocksTouched = 0;      ///< block updates attempted
    uint32_t blocksUpdated = 0;      ///< block updates that changed something
    uint32_t queries = 0;
    uint32_t queriesOccluded = 0;
    uint32_t queriesOffscreen = 0;
    /// KEY: Queries refused because the box crosses the near plane, and so
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
/// interleaving is the point -- see the class comment.
class RendererExport MaskedDepth
{
public:
    /// A block is 8x4 pixels so that its coverage is exactly one
    /// uint32_t, which is what makes the mask free to test and merge.
    /// WARNING: Do not "tune" these: 32 bits per block is the structure, not a
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

    /// Forget every occluder. Cheap -- 12 bytes per 32 pixels -- but not
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
    /// WARNING: Calling this does *not* clear the buffer: a caller that moves
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
    /// KEY: **Two-sided by design.** Back-face culling would halve the
    /// work, and every renderer does it, but it assumes consistent
    /// winding -- which tessellated B-Rep out of a CAD kernel does not
    /// reliably have, and a wrongly culled occluder face is a *hole* in
    /// the depth buffer that under-culls silently. A back face is a real
    /// surface at a real depth; rasterizing it costs time and cannot
    /// cost correctness. `setTwoSided(false)` is available for callers
    /// that know their winding.
    void rasterize(const float *positions, size_t stride, size_t vertexCount,
                   const uint32_t *indices, size_t indexCount,
                   const float *model = nullptr);

    /// The same, for the signed indices a MeshData carries. A negative
    /// index is not dereferenced -- it fails the same bounds check an
    /// out-of-range unsigned one does.
    void rasterize(const float *positions, size_t stride, size_t vertexCount,
                   const int32_t *indices, size_t indexCount,
                   const float *model = nullptr);

    /// The same, for an unindexed triangle list.
    void rasterize(const float *positions, size_t stride, size_t vertexCount,
                   const float *model = nullptr);

    /// Whether a world-space axis-aligned box could have reached the
    /// screen given the occluders rasterized so far.
    ///
    /// WARNING: A box that reaches the near plane answers `Visible` and is
    /// counted in `queriesNearPlane`: its projection is unbounded, so
    /// there is no rect to test. This is the same exemption the GPU path
    /// needs (`boxReachesNearPlane`) and for a related reason -- the
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
    /// KEY: The comparison is strict, so a surface exactly coincident with
    /// the query answers `Visible`. That tie is the one section 12.6 lost: a
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

    /// Whether back faces are rasterized (default true -- see
    /// `rasterize`).
    void setTwoSided(bool on) { twosided = on; }
    bool twoSided() const { return twosided; }

    /// Whether the four-wide vector pre-pass runs (default on).
    ///
    /// KEY: **It can only discard, never draw.** Four triangles are
    /// transformed and projected in float; a triangle the vector code
    /// finds off the buffer or over no pixel centre is dropped there,
    /// and *everything else is handed to the same double-precision path
    /// that ran before this existed*, which recomputes it from the
    /// original vertices. So the float arithmetic decides how much work
    /// is skipped and nothing else: a lane it gets wrong either wastes
    /// the exact path's time or loses one sub-pixel triangle's
    /// occlusion, and neither direction can store a depth nearer than
    /// the truth. That is the whole reason the pre-pass is allowed to be
    /// float at all, given the near-plane cancellation argued beside
    /// `kMinW` in the .cpp.
    ///
    /// Off is the reference arm: same buffer, same camera, every
    /// triangle through the exact path. It exists to be measured
    /// against, not as a fallback for a suspected bug.
    void setSimdFilter(bool on) { simdfilter = on; }
    bool simdFilter() const { return simdfilter; }

    const MaskedOcclusionStats &stats() const { return framestats; }
    void resetStats() { framestats = MaskedOcclusionStats(); }

    /// The depth stored for one pixel, in the internal convention, or 0
    /// where nothing has been rasterized. For tests and the debug
    /// readout only -- the mechanism never reads a single pixel.
    float pixelDepth(int x, int y) const;

    /// The conservative floor of the block containing (\a x, \a y): no
    /// surface in that block is farther than this. For tests.
    float blockFloor(int x, int y) const;

    /// Fold \a other's occluders into this buffer. Both must have the
    /// same size and camera.
    ///
    /// KEY: This is what lets the occluder pass run on several threads
    /// without any shared state: each worker rasterizes its own slice of
    /// the occluder list into its own buffer, and the slices are merged
    /// afterwards. Every step of the merge is an ordinary block update,
    /// so the one-sided invariant survives it by construction rather
    /// than by a separate argument.
    ///
    /// WARNING: Two two-layer blocks cannot merge into one without loss --
    /// four layers do not fit in two -- so a merged buffer may occlude
    /// slightly less than a single-threaded one. Less, never more, and
    /// the result is deterministic for a fixed worker count.
    void merge(const MaskedDepth &other);

    /// Blocks in the buffer, so a caller can split a merge across
    /// workers.
    size_t blockCount() const { return blocks.size(); }

    /// Merge only blocks [\a lo, \a hi) of \a other, touching no
    /// counters.
    ///
    /// KEY: The merge is per block and the blocks are independent, so
    /// this is what lets the merge run on the same workers the
    /// rasterization did. Measured, a serial merge of fourteen shards
    /// was most of what parallelizing the rasterization had saved. It
    /// deliberately updates no statistics: two workers incrementing one
    /// counter is a data race, and the counters are folded in afterwards
    /// by mergeStats() on one thread.
    void mergeBlocks(const MaskedDepth &other, size_t lo, size_t hi);
    /// Fold \a other's rasterization counters in. Not thread safe, and
    /// not needed to be.
    void mergeStats(const MaskedDepth &other);

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

    /// Triangles the vector pre-pass judges in one go. Fixed at the
    /// lane count of the only SIMD width that exists everywhere
    /// (Gui/Renderer/Simd4.h); a partial batch at the end of a mesh
    /// simply takes the exact path.
    static const int Batch = 4;

    /// What the vector pre-pass concluded about one lane.
    enum Verdict : uint8_t {
        VerdictKeep,       ///< judged, and it may cover pixels
        VerdictOffBuffer,  ///< judged: outside the buffer
        VerdictSubPixel,   ///< judged: over no pixel centre
        VerdictExact,      ///< declined to judge; the exact path decides
    };

    /// Judge \a Batch triangles at once, writing one Verdict per lane.
    /// Reads nothing but the camera and the buffer size, writes nothing
    /// but \a verdict -- it is a filter, not a rasterizer.
    void filterBatch(const float *const *v, const float *mvp,
                     uint8_t *verdict) const;
    /// Run \a n <= Batch triangles through the filter and hand the
    /// survivors to `emitTriangle`.
    void emitBatch(const float *const *v, int n, const float *mvp);

    /// One source triangle: transform, near-clip, project, fan out.
    void emitTriangle(const float *pa, const float *pb, const float *pc,
                      const float *mvp);
    void rasterTri(const double *sx, const double *sy, const double *sd);
    void updateBlock(int bx, int by, uint32_t coverage, float ztri);
    void updateBlockAt(size_t index, uint32_t coverage, float ztri);
    /// The block update itself, free of any counter so that it can run
    /// on a worker. True when it changed something.
    static bool applyBlock(Block &b, uint32_t coverage, float ztri);
    static float floorOf(const Block &b);

    std::vector<Block> blocks;
    int bufw = 0;          ///< usable pixels, a multiple of BlockW
    int bufh = 0;
    int bw = 0;            ///< blocks across
    int bh = 0;
    bool twosided = true;
    bool simdfilter = true;
    bool homogeneous = true;
    bool orthographic = false;
    float viewproj[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    mutable MaskedOcclusionStats framestats;
};

// ---------------------------------------------------------------------
// Using it: choosing occluders, and the walk that spends them
// ---------------------------------------------------------------------

/// What the occluder pass is allowed to cost, and how it spends it.
struct MaskedCullConfig {
    /// Buffer resolution as a divisor of the viewport.
    ///
    /// WARNING: **Above 1 this can over-cull, and over-culling is the bug
    /// this whole mechanism exists to remove.** A coarse pixel is marked
    /// covered when the occluder reaches its centre, but it stands for
    /// several real pixels, and the ones the occluder missed are then
    /// claimed too. The literature runs at reduced resolution and
    /// accepts it; this workstream has spent three sections on deleted
    /// geometry, so the default is 1 and anything else is a measurement,
    /// not a setting. Correct reduction needs coverage sampled over the
    /// coarse pixel's whole footprint rather than at its centre, which
    /// is not built.
    ///
    /// The memory argument for reducing is weak anyway: a full 1863x1064
    /// buffer is 233x266 blocks at 12 bytes, about 744 KB.
    int resolutionDivisor = 1;
    /// Triangles rasterized per frame, at most. The budget is spent
    /// largest-on-screen first, so what it drops is what occludes least.
    uint32_t triangleBudget = 250000;
    /// A draw whose projected bounding-box diagonal is under this many
    /// pixels is not worth rasterizing: it can hide almost nothing, and
    /// the budget it spends is taken from something that could.
    float minOccluderPx = 24.0f;
    /// How many draws may be considered at all, after sorting.
    uint32_t maxOccluders = 8192;
    /// Workers the occluder pass may use, 0 for automatic.
    ///
    /// KEY: Each worker rasterizes its own slice of the occluder list
    /// into its own buffer and the buffers are merged afterwards, so
    /// there is no shared state and no locking. Intel's implementation
    /// added exactly this in 2018 as an alternative to binning
    /// ("MergeBuffer... an alternative method for parallelizing buffer
    /// creation"), and reports ~3x on four threads for the binned form.
    ///
    /// WARNING: The merge is lossy -- see MaskedDepth::merge -- so a
    /// different worker count can cull slightly differently. Never more,
    /// only less, and deterministically for a fixed count.
    uint32_t threads = 0;
    /// Whether the four-wide vector pre-pass runs -- see
    /// MaskedDepth::setSimdFilter. Off is the reference arm of the
    /// measurement, not a fallback.
    bool simdFilter = true;

    bool operator==(const MaskedCullConfig &o) const
    {
        return resolutionDivisor == o.resolutionDivisor
                && triangleBudget == o.triangleBudget
                && minOccluderPx == o.minOccluderPx
                && maxOccluders == o.maxOccluders && threads == o.threads
                && simdFilter == o.simdFilter;
    }
    bool operator!=(const MaskedCullConfig &o) const { return !(*this == o); }
};

/// What one frame did, reported rather than inferred.
struct MaskedCullStats {
    uint32_t occluderCandidates = 0;  ///< draws that could have occluded
    uint32_t occluderDraws = 0;       ///< draws actually rasterized
    uint32_t occluderTriangles = 0;
    /// KEY: Candidates the triangle budget refused. A silent cap reads as
    /// "everything was rasterized" when it was not, and the difference
    /// is the difference between "the scene does not occlude" and "we
    /// did not look".
    uint32_t occludersDropped = 0;
    /// Workers the occluder pass actually used.
    uint32_t occluderThreads = 1;

    uint32_t nodesVisited = 0;
    uint32_t nodesOffscreen = 0;
    uint32_t nodesTested = 0;
    uint32_t nodesHidden = 0;
    uint32_t nearExempt = 0;
    /// KEY: The root answered hidden. Structurally impossible -- its box
    /// contains every occluder, so its nearest corner is in front of all
    /// of them -- which makes any non-zero count a report that the test
    /// is broken rather than a scene that is entirely hidden. Counted
    /// and refused, never acted on.
    uint32_t rootRefused = 0;

    uint32_t hiddenInstances = 0;
    uint32_t offscreenInstances = 0;
    uint32_t drawnInstances = 0;

    float rasterMs = 0.0f;
    float walkMs = 0.0f;

    /// KEY: Where `rasterMs` went, split so that it cannot be attributed by
    /// arithmetic. Two sessions running have improved a component of this
    /// pass and found the wall clock barely moved -- the draw-submission
    /// saving priced at 10-12 ms that measured 4.9 (section 12.12), the
    /// vector pre-pass that took 4x off the transform and 0.5 ms off the
    /// frame (section 12.14) -- and in both cases the thing that was
    /// missing was a *measurement of the other terms*, not a better
    /// estimate of the one being changed. These are that measurement.
    ///
    /// `rasterMs` is the sum of `selectMs`, `shardMs` and `mergeMs` plus
    /// the little left over; the `worst*` fields are the largest single
    /// worker's share of a phase, so the difference between a phase and
    /// its worst worker is what the threading cost rather than the work.
    float selectMs = 0.0f;   ///< sizing, clearing, ranking, job building
    float shardMs = 0.0f;    ///< phase 1 wall: clear + rasterize on workers
    float mergeMs = 0.0f;    ///< phase 2 wall: folding the shards together
    float worstClearMs = 0.0f;   ///< the slowest worker's shard clear
    float worstRasterMs = 0.0f;  ///< the slowest worker's rasterization
    float worstMergeMs = 0.0f;   ///< the slowest worker's merge range
    /// Every worker's rasterization added up. Against `worstRasterMs`
    /// this is the load balance; against `shardMs` times the worker
    /// count it is how much of the machine the phase actually used.
    float sumRasterMs = 0.0f;
};

/// The occluder pass and the walk that spends it.
///
/// KEY: **There is no state between frames and no policy layer.** No
/// verdict is stored, so nothing can go stale; no test is pending, so
/// nothing can be starved; no answer is confirmed over several frames,
/// because the answer is taken and used inside one walk. The
/// hidden-streak counter, the visible time-to-live, the query pool, the
/// lease expiry and the starvation fail-safe that the hardware path
/// needs are all consequences of the answer arriving a frame late, and
/// none of them have anything to answer for here. That deletion is most
/// of what this change is worth (docs/FarFieldProxies.md section 12.12).
///
/// WARNING: It is also why the two paths cannot share `OcclusionCuller`: its
/// per-node state exists to survive latency, and carrying it here would
/// re-introduce exactly the coupling being removed.
class RendererExport MaskedOccluderPass
{
public:
    void configure(const MaskedCullConfig &c) { conf = c; }
    const MaskedCullConfig &config() const { return conf; }

    /// Rasterize this camera's occluders. Sizes and clears the buffer,
    /// so it is the only call that has to happen before the walk.
    void build(const DrawCallList &draws, const float *view,
               const float *proj, bool homogeneousDepth, int viewportW,
               int viewportH);

    /// Walk \a index and set \a cullMask for every draw row that cannot
    /// have reached the screen.
    ///
    /// \a cullMask is indexed by `DrawCall` row and is only ever *set*,
    /// never cleared, so it composes with the frustum mask the renderer
    /// already computed. \a cullOwner, when given, receives the node
    /// whose verdict cut each row -- the join that makes an over-culled
    /// draw traceable to the test that deleted it.
    void cull(const ProxyHierarchy &index, const float *view,
              const float *proj, float viewportHeightPx,
              std::vector<uint8_t> &cullMask,
              std::vector<int32_t> *cullOwner = nullptr);

    const MaskedDepth &depth() const { return buffer; }
    MaskedDepth &depth() { return buffer; }
    const MaskedCullStats &lastFrame() const { return framestats; }

private:
    /// One occluder admitted by the budget: the draw row and the index
    /// range of it to rasterize.
    struct OccluderJob {
        uint32_t draw;
        uint32_t first;
        uint32_t count;
    };

    MaskedDepth buffer;
    MaskedCullConfig conf;
    MaskedCullStats framestats;
    /// Scratch kept across frames so that a frame allocates nothing.
    std::vector<std::pair<float, uint32_t>> ranking;
    std::vector<OccluderJob> jobs;
    /// One buffer per worker, kept across frames: a 744 KB allocation
    /// per worker per frame would cost more than the rasterization.
    std::vector<MaskedDepth> shards;
    /// What each worker spent, written once per phase by the worker that
    /// owns the entry and read after the join. Separated from the shard
    /// so that timing one costs nothing when nobody reads it.
    struct WorkerTime {
        float clearMs;
        float rasterMs;
        float mergeMs;
    };
    std::vector<WorkerTime> times;
    std::vector<int> walkstack;
    std::vector<int> descendstack;
};

}  // namespace Render

#endif  // RENDERER_MASKED_OCCLUSION_H
