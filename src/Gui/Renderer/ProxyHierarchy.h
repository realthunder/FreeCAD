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

#ifndef RENDERER_PROXY_HIERARCHY_H
#define RENDERER_PROXY_HIERARCHY_H

/// The spatial index a far-field cut is chosen through
/// (docs/FarFieldProxies.md §3) — phase 1 of that document's plan.
///
/// **The input is a flat table of drawn instances, never a tree** (§3.1).
/// An assembly's document structure is an *ownership* structure — who
/// contains what, which transform and material apply, what a selection
/// path names — and a cut needs a *location* structure. The common
/// ill-formed assembly settles the point on its own: one holder over
/// twenty thousand children has exactly one interior node, so a
/// document-tree hierarchy offers no cut at all, only "draw everything"
/// or "draw one blob".
///
/// So nothing here knows about Coin, ViewProvider, DocumentObject, bgfx
/// or OCCT, and the arithmetic is plain float — the same discipline
/// SceneLadder.h keeps, and for the same reason: the browser tier has
/// to be able to build the identical hierarchy from the instance table
/// it already receives over the wire.
///
/// What is built is better described as the renderer's spatial index
/// than as "the LOD hierarchy": level of detail is one consumer, and
/// frustum culling, streaming priority and pick acceleration are
/// others.
///
/// Phase 1 deliberately stops short of generating anything. Its purpose
/// is to *predict* the win — the post-aggregation draw count of §11.1 —
/// so that phase 2 is started against a number rather than a hope.

#include <cstdint>
#include <string>
#include <vector>

#include "Renderer.h"

namespace Render {

// ---------------------------------------------------------------------
// The shared projection primitive
// ---------------------------------------------------------------------

/// One bounding box against one camera.
///
/// This is the arithmetic the desktop plan passes already share (refine
/// and demote read the same projection and act on opposite sides of a
/// tolerance); the cut is a third consumer, and it lives here rather
/// than in SceneLadder.cpp so that the cut and the coverage histogram
/// agree about "small on screen" *by construction* rather than by two
/// copies of the same formula staying in step.
struct BoxSight {
    enum What {
        Empty,      ///< no bounds, degenerate, or not judgeable
        Offscreen,  ///< outside the frustum (or wholly behind)
        Inside,     ///< the camera is inside the box span: maximal
        Visible,    ///< on screen at diagPx
    } what = Empty;
    /// Projected size of the box diagonal in pixels (Visible only).
    float diagPx = 0.0f;
};

/// \a V and \a P are GL-layout (column-major) 4x4 matrices, as
/// Renderer::render receives them. An orthographic projection is
/// recognised by P[15] != 0.
RendererExport BoxSight sightBounds(const float *bboxMin, const float *bboxMax,
                                    const float *V, const float *P,
                                    float viewportHeightPx);

/// Bytes a mesh occupies once uploaded: positions, and whichever
/// optional attribute arrays it actually carries, plus every index
/// array. All topologies are counted, not just the one a given draw
/// uses -- residency is a property of the mesh, and a mesh is uploaded
/// whole.
///
/// Shared with the level plan (SceneLadder.h, planMeshDemotes) so that
/// what a far-field cut says it would free and what a demotion says it
/// frees are the same number computed once, rather than two estimates
/// that drift apart. Callers that sum it over draws must dedupe by mesh
/// first: 495 instanced submits stand in for 5432 rows on the model this
/// was measured on, and charging each row would overstate residency
/// several times over.
RendererExport uint32_t meshResidentBytes(const MeshData *m);

/// Whether any corner of the box lies at or beyond the near plane.
///
/// Not a visibility question: it is what says an occlusion query
/// *cannot answer* for this box. Rasterizing a box to test it against
/// the depth buffer only works while the box has front faces to
/// rasterize, and the near plane clips those away — leaving the back
/// faces, which the box's own contents hide, so the box reports itself
/// hidden however plainly visible it is. The whole-model box of a
/// tightly fitted camera is the standard case.
///
/// Exact over the eight corners, unlike sightBounds' centre-plus-margin
/// approximation, because a conservative *over*-estimate here would
/// silently exempt boxes an occlusion test could have answered.
///
/// \a homogeneousDepth selects the clip convention: true for OpenGL's
/// -w..w depth range, false for the 0..w of D3D, Vulkan and Metal.
RendererExport bool boxReachesNearPlane(const float *bboxMin,
                                        const float *bboxMax, const float *V,
                                        const float *P, bool homogeneousDepth);

/// The corner order every occlusion test box is built in: corner `i`
/// takes its x from bit 0, y from bit 1 and z from bit 2 — bit set means
/// the box maximum, clear the minimum.
///
/// ⚠️⚠️ **The cyclic order of a face is therefore 0,1,3,2 and not
/// 0,1,2,3**, because bit 1 and bit 2 of a face's four corners count in
/// binary, not around its rim. Written the natural-looking way, four of
/// the twelve triangles come out as *diagonal cross-sections through the
/// box interior* rather than faces, two faces are missing outright, and
/// two more are missing a quarter each — a box that rasterizes 3.5 of
/// its 6 faces. It still draws something from every angle, so it
/// answers plausibly; what it answers with is an interior surface,
/// which is deeper than the front face it stands in for, so LEQUAL
/// rejects it and the node reports itself hidden while in plain view.
/// That deleted visible geometry no amount of padding could restore
/// (§12.6). `occlusionBoxIndices` is shared and tested rather than
/// written out at the call site for exactly that reason.
///
/// 36 indices, 12 triangles, all six faces closed, wound consistently
/// outward — though winding does not matter to the caller, which
/// disables back-face culling so a camera inside a box still sees it.
RendererExport const unsigned short *occlusionBoxIndices();

/// World-space outward padding a test box needs before the depth buffer
/// can tell its front face from the surface it bounds — \a lsb depth
/// steps, converted to a distance at the box's nearest depth.
///
/// ⚠️⚠️ **The relative pad cannot do this job, and believing it could
/// deleted visible geometry** (§12.6). Padding by a fraction of the
/// box's own diagonal answers "the two surfaces are coincident in
/// world space"; it says nothing about whether the depth *buffer* can
/// resolve the gap. A small part lying flush on a large panel is a box
/// whose diagonal is small — so its relative pad is small — sitting at
/// a distance where one depth step is much larger than that pad. Both
/// surfaces then quantize to the same stored value, the tie is decided
/// by which way the rasterizer rounds, and where the box loses, LEQUAL
/// rejects every fragment and the node calls itself hidden while in
/// plain view. Bigger parts survive it, which is exactly the pattern
/// the image comparison showed: panels kept, the components on them
/// gone.
///
/// The correct unit is therefore the depth buffer's own, not the
/// model's: \a lsb steps of a 24-bit depth buffer (the scene target is
/// D24S8), converted through the projection's depth derivative at the
/// nearest corner — z^2/|P[14]| for a perspective projection, constant
/// for an orthographic one. Returns 0 for a box at or behind the eye,
/// which is the near-plane exemption's case rather than this one.
///
/// Conservative in the only direction that matters: too much padding
/// makes a box test *visible* and costs frame time; too little deletes
/// geometry.
RendererExport float depthQuantumPad(const float *bboxMin,
                                     const float *bboxMax, const float *V,
                                     const float *P, bool homogeneousDepth,
                                     float lsb);

// ---------------------------------------------------------------------
// The instance table
// ---------------------------------------------------------------------

/// One drawn instance as the hierarchy sees it (§3.1). Every field is a
/// projection of a Render::DrawCall the renderer already carries, which
/// is why phase 1 needs no new plumbing on the publish side.
struct ProxyInstance {
    /// Content hash of the scene-graph node path that produced the
    /// draw. Already *per occurrence* rather than per document object,
    /// which is the unit that pays the per-object frame cost and
    /// therefore the unit worth partitioning.
    uint64_t objectKey = 0;
    float bboxMin[3] {0.0f, 0.0f, 0.0f};  ///< world bounds, empty if min > max
    float bboxMax[3] {0.0f, 0.0f, 0.0f};
    /// Which proxy this instance may be merged into (§5.1): a proxy is
    /// generated per (cell, material bucket), so that every proxy
    /// carries exactly one material and nothing is ever averaged.
    uint64_t materialBucket = 0;
    /// Content identity of the mesh — the TShape-level key. Recorded
    /// for phase 2's instancing gate (§7.1: merging 500 instances of
    /// one screw expands what instancing shares) and unused by the
    /// partition itself.
    const void *sourceTag = nullptr;
    /// Primitives this draw issues -- triangles, line segments or
    /// points, whichever the material's topology selects.
    ///
    /// The unit the cut is gated on. It used to be gated on draws
    /// alone, and draws were then measured to cost nothing on this
    /// scene: the frame is GPU-bound and the GPU is geometry-bound
    /// (docs/DrawSubmission.md). A cut that removes draws without
    /// removing primitives buys nothing here.
    uint32_t primCount = 0;
    /// Resident bytes of the mesh behind this draw -- vertices and
    /// indices as uploaded. Keyed to \ref sourceTag, NOT summable over
    /// instances: many instances share one mesh, and its bytes come
    /// back only when the last of them stops needing it.
    ///
    /// Carried because occlusion and aggregation are not only speed
    /// mechanisms. What they remove from residency is what decides
    /// whether a model opens at all, and that axis has a different
    /// answer from the primitive axis beside it.
    uint32_t meshBytes = 0;
    /// Which row of the table this was projected from. The partition
    /// never follows it — it is the join back to whatever the caller
    /// holds the geometry in, which generation needs and selection will
    /// too. Rows the projection skipped shift the numbering, so it
    /// cannot be reconstructed by counting afterwards.
    uint32_t drawIndex = 0;
};

/// Project a draw list into the instance table (§3.1). Draws without
/// usable bounds are skipped: the hierarchy cannot place what it cannot
/// locate, and a draw with no bounds is not judgeable by the cut
/// either.
RendererExport void proxyInstances(const DrawCallList &draws,
                                   std::vector<ProxyInstance> &out);

// ---------------------------------------------------------------------
// The partition
// ---------------------------------------------------------------------

struct ProxyParams {
    /// Note on what "loose" buys, since it is the one place this
    /// differs from a textbook octree: an instance's level is decided
    /// by its **size alone** and its cell by its **centre**, never by
    /// whether it happens to straddle a grid line. Assigning to the
    /// finest cell that contains a box *whole* is the natural-sounding
    /// alternative and was measured to strand 28% of a uniform lattice
    /// at coarse levels, where nothing aggregated them.

    /// **K** — subdivide while a cell holds more than this many
    /// instances. Not a tuning knob: it is simultaneously the bound on
    /// how much geometry one switch changes (§8.1, "the size of a node
    /// is the size of a pop") and the bound on how much draws exactly
    /// when the cut is forced to descend (§8). Those two want it small
    /// and the draw count wants it large; §11.4 records that if no
    /// value satisfies both, it is the pop treatment that must change.
    uint32_t maxPerCell = 32;
    /// Depth cap. Also caps the Morton key: 3 bits per level, packed
    /// with the level into 64 (see ProxyNode::id).
    uint32_t maxLevel = 16;
    /// Stop subdividing once a cell edge falls below this fraction of
    /// the root edge — the extent target of §3.2, expressed relative to
    /// the model so that it means the same thing at any scale.
    float minCellFraction = 1.0f / 8192.0f;
    /// Below this many instances a node is not worth a proxy: merging
    /// one object produces a decimated object, which is what the
    /// per-object ladder already does better. Such a node draws its
    /// members exactly.
    uint32_t minMerge = 2;
};

constexpr int kNoProxyNode = -1;

struct ProxyNode {
    uint32_t level = 0;
    uint32_t cell[3] {0, 0, 0};
    /// **Positional identity** (§3.2): level in the high bits, the
    /// Morton code of the cell in the low ones. Deliberately not
    /// derived from the contents — a median or SAH split would let one
    /// moved part reshuffle the partition and invalidate proxies for
    /// geometry that did not change, where a quantised grid moves that
    /// part between two cells and leaves everything else alone. It is
    /// also identical on every machine and across sessions, which is
    /// what lets §7's generation cache be shared rather than
    /// per-session.
    uint64_t id = 0;
    /// The cell's *nominal* bounds, as the grid defines them. The cell
    /// is loose: it owns whatever is centred in it, so its contents may
    /// reach half a cell beyond this on any side. Nothing judges the
    /// node by these — the cut sights contentMin/Max — and they are
    /// kept because they, not the contents, are what the node's
    /// identity means.
    float cellMin[3] {0.0f, 0.0f, 0.0f};
    float cellMax[3] {0.0f, 0.0f, 0.0f};
    float contentMin[3] {0.0f, 0.0f, 0.0f};  ///< union of the subtree's instances
    float contentMax[3] {0.0f, 0.0f, 0.0f};
    /// Octant children, kNoProxyNode where empty. A node is a leaf when
    /// all eight are empty.
    int child[8] {kNoProxyNode, kNoProxyNode, kNoProxyNode, kNoProxyNode,
                  kNoProxyNode, kNoProxyNode, kNoProxyNode, kNoProxyNode};
    /// Instances assigned to *this* level — too large (or too badly
    /// straddling) to fit a cell one level down. Range into residents().
    uint32_t residentFirst = 0;
    uint32_t residentCount = 0;
    /// Distinct material buckets in the whole subtree, as a range into
    /// buckets(). Summed over a cut this is the post-aggregation draw
    /// count (§5.1).
    uint32_t bucketFirst = 0;
    uint32_t bucketCount = 0;
    /// Instances at or below this node.
    uint32_t subtreeCount = 0;
    /// Primitives at or below this node -- what a proxy here would have
    /// to stand in for, and therefore the ceiling on what it can save.
    uint64_t subtreePrims = 0;

    bool leaf() const
    {
        for (int c : child) {
            if (c != kNoProxyNode)
                return false;
        }
        return true;
    }
};

/// What generation found out about one node, for the cut to read --
/// indexed by node index, parallel to nodes().
///
/// Phase 1 descended by a node's projected *extent* because nothing had
/// been generated that could have an error, and 11.1c measured what
/// that stand-in costs: the extent-to-error ratio is a distribution
/// (0.084 mean against 0.34 worst at cell/8), so a single tolerance
/// over extents stops far too early on some nodes and far too late on
/// others. The ratio is a property of geometry and grid rather than of
/// the camera, so it is measured once at generation and read here.
struct ProxyNodeCost {
    /// The node's committed error as a fraction of its extent, so that
    /// the projected error is this times the projected extent and no
    /// second projection is needed. Negative means no proxy exists for
    /// this node, and a cut cannot stop where there is nothing to draw.
    float errorRatio = -1.0f;
    /// Primitives the node draws if the cut stops on it: the proxy and
    /// the boxes standing in for what it deleted (11.1d). What makes
    /// the saving honest -- phase 1 could only count what a proxy
    /// replaces, never what it costs.
    uint32_t prims = 0;
    /// Draw calls it issues, one per material bucket actually built.
    /// Zero falls back to the node's bucket count.
    uint32_t draws = 0;
};

/// The exactly drawn mass of a cut, split by why it did not aggregate
/// and by how large it is on screen (section 11.1g).
struct ProxyExactBreakdown {
    /// Why the node an exact instance sits in did not stop the cut.
    enum Reason {
        /// Its own error projects above the tolerance. The near field,
        /// and the one answer that is not a gap: descending was right.
        Resolvable,
        /// Nothing was generated for it, so the cut could not stop
        /// there whatever its error would have been.
        NoProxy,
        /// Fewer members than minMerge: proxying one object produces a
        /// decimated object, which the per-object ladder does better.
        TooFewMembers,
        ReasonCount
    };
    /// How large the instance itself is, in multiples of the tolerance:
    /// within it, up to 4x, up to 16x, beyond. The first bin is the
    /// mass a cut could in principle remove and did not.
    static const int SizeBins = 4;
    struct Bin {
        uint32_t instances = 0;
        uint64_t prims = 0;
    };
    Bin bins[ReasonCount][SizeBins];
    Bin byReason[ReasonCount];
    Bin bySize[SizeBins];
    Bin total;
    /// Deepest node level any exact instance was found at, and the
    /// primitive-weighted mean level -- a near field sits deep, a
    /// generation gap can sit anywhere.
    uint32_t maxLevel = 0;
    double meanLevel = 0.0;
};

/// A frontier through the partition, and what it costs.
///
/// Note what a node *above* the frontier still owes: its own residents.
/// An instance too large to descend is not covered by any proxy below
/// it, so it draws exactly — which is right, because assignment by size
/// (§3.2) put it there precisely because it is large on screen.
struct ProxyCut {
    std::vector<int> proxyNodes;      ///< nodes drawing proxies
    std::vector<uint32_t> exact;      ///< instance indices drawn exactly
    /// The node each exact instance was resident in, parallel to
    /// \ref exact. Which node it was is what says *why* the instance
    /// draws exactly -- the descent's reason is a property of the node,
    /// not of the instance -- and it cannot be recovered afterwards
    /// without searching the partition for the instance again.
    std::vector<int> exactNode;
    /// Draws the cut issues: the exact ones, plus one per distinct
    /// material bucket of each proxy node (§5.1). **This is the number
    /// phase 1 exists to produce.**
    uint32_t drawCount = 0;
    uint32_t proxyDraws = 0;          ///< the merged part of drawCount
    uint32_t coveredInstances = 0;    ///< instances a proxy stands for
    uint32_t culledInstances = 0;     ///< off screen, drawn by nobody
    /// The same three populations counted in primitives instead of
    /// draws. `coveredPrims` is the geometry a proxy replaces -- the
    /// ceiling on what the cut can save, before the proxy's own
    /// primitives are subtracted (which needs generating, sec 11.1c).
    /// **This is the number the cut is now gated on**, because draws
    /// were measured not to cost on this scene.
    uint64_t exactPrims = 0;
    uint64_t coveredPrims = 0;
    uint64_t culledPrims = 0;
    /// What the proxies themselves draw, when the cut was given
    /// ProxyNodeCost to read. Zero without it, because an ungenerated
    /// proxy has no cost to report -- and a saving quoted as
    /// coveredPrims alone is the gross figure, not the net one.
    uint64_t proxyPrims = 0;
};

/// The spatial index of §3.2: a loose octree over world bounds with
/// positional node identity and grids that nest.
class RendererExport ProxyHierarchy
{
public:
    /// Partition \a instances. Safe to call repeatedly; the previous
    /// contents are discarded. A copy of the table is kept, so the
    /// caller's draw list may go away.
    void build(const std::vector<ProxyInstance> &instances,
               const ProxyParams &params = ProxyParams());
    void clear();

    bool empty() const;
    int root() const;
    const ProxyParams &params() const;
    const std::vector<ProxyNode> &nodes() const;
    const std::vector<ProxyInstance> &instances() const;
    /// Instance indices, indexed by ProxyNode::residentFirst/Count.
    const std::vector<uint32_t> &residents() const;
    /// Material buckets, indexed by ProxyNode::bucketFirst/Count.
    const std::vector<uint64_t> &buckets() const;

    /// Every instance at or below \a node, appended to \a out — what a
    /// proxy for that node has to stand for, and therefore what
    /// generation merges (§7.1). Yields ProxyNode::subtreeCount indices.
    void subtreeInstances(int node, std::vector<uint32_t> &out) const;

    /// Descend the frontier by projected error (§3.3): a node draws a
    /// proxy once its content bounds project to no more than
    /// \a tolerancePx, and is descended past otherwise.
    ///
    /// **Stateless, and therefore without hysteresis.** The split/merge
    /// margin of §3.3 needs the previous frame's frontier to compare
    /// against and belongs to phase 3, where the cut is retained; what
    /// this answers is the measurement question — what would a cut cost
    /// at this camera — for which a single evaluation is the honest
    /// instrument.
    void selectCut(const float *view, const float *proj,
                   float viewportHeightPx, float tolerancePx,
                   ProxyCut &out) const;

    /// The same descent, reading each node's measured error instead of
    /// standing its extent in for one (section 3.3).
    ///
    /// \a costs is indexed by node index and may be shorter than
    /// nodes(); a node it does not cover, or covers with a negative
    /// ratio, has no proxy and is descended past. A node stops the
    /// frontier when `errorRatio * diagPx` is within \a tolerancePx --
    /// the projected error rather than the projected size, which is
    /// what 11.1c showed a single extent tolerance cannot stand in for.
    void selectCut(const float *view, const float *proj,
                   float viewportHeightPx, float tolerancePx,
                   const std::vector<ProxyNodeCost> &costs,
                   ProxyCut &out) const;

    /// Why the exactly drawn part of \a cut is drawn exactly
    /// (section 11.1g).
    ///
    /// The priced cut measured that a proxy costs 4-9% of what it
    /// replaces, which moves the question: the ceiling is not what
    /// aggregation costs, it is what never aggregates. At a 4px
    /// tolerance two thirds of the visible primitives are still exact,
    /// and "tune the proxies" cannot touch any of it until it is known
    /// which of three quite different things that mass is.
    ///
    /// Every exact instance is a resident of a node the descent went
    /// past, so the reason is the node's: it had an error the camera
    /// can resolve (correct -- this is the near field), it had no
    /// proxy to stop on (a generation gap, and the bug-shaped one), or
    /// it held too few members to be worth one (the partition's own
    /// floor).
    ///
    /// Crossed with how large the instance itself is on screen, which
    /// is what says whether the mass is *addressable at all*: an
    /// instance that projects to less than the tolerance is detail the
    /// camera cannot resolve and that something ought to have merged,
    /// while one that projects to ten times it is near field however it
    /// got there, and no cut should touch it.
    ///
    /// \a costs may be null, in which case the descent is the extent
    /// rule's and no node can be missing a proxy.
    void explainExact(const ProxyCut &cut, const float *view,
                      const float *proj, float viewportHeightPx,
                      float tolerancePx,
                      const std::vector<ProxyNodeCost> *costs,
                      ProxyExactBreakdown &out) const;

    /// §11.3, the invariant asserted before either ladder is coded:
    ///
    /// > Every instance belongs to exactly one cell per level, and for
    /// > each instance exactly one of {the instance, its covering
    /// > proxy} draws.
    ///
    /// Over a partition this is a counting argument rather than the
    /// ancestry test the same rule needs over a document tree. O(n) and
    /// debug-only: it allocates a mark per instance.
    bool verifyCut(const ProxyCut &cut, std::string *why = nullptr) const;

    /// What phase 1 reports (§11.1): the distributions that pick K and
    /// the extent target, per level of the partition.
    struct LevelStats {
        uint32_t nodes = 0;
        uint32_t residents = 0;      ///< instances assigned at this level
        uint32_t subtreeMax = 0;     ///< largest subtree at this level
        uint32_t bucketsMax = 0;     ///< most distinct materials in one subtree
        double bucketsMean = 0.0;
    };
    struct Stats {
        uint32_t instances = 0;
        uint32_t nodes = 0;
        uint32_t depth = 0;          ///< deepest level actually created
        uint32_t distinctBuckets = 0;  ///< over the whole model
        std::vector<LevelStats> byLevel;
    };
    Stats stats() const;

private:
    void selectCutImpl(const float *view, const float *proj,
                       float viewportHeightPx, float tolerancePx,
                       const std::vector<ProxyNodeCost> *costs,
                       ProxyCut &out) const;
    int buildNode(uint32_t level, const uint32_t cell[3],
                  std::vector<uint32_t> items);
    void markSubtree(int node, std::vector<uint8_t> &mark,
                     uint8_t bit, uint32_t &clashes) const;

    std::vector<ProxyInstance> instancedata;
    std::vector<ProxyNode> nodedata;
    std::vector<uint32_t> residentdata;
    std::vector<uint64_t> bucketdata;
    /// Deepest level each instance may be placed at, by index.
    std::vector<uint32_t> assignlevel;
    ProxyParams parameters;
    float rootmin[3] {0.0f, 0.0f, 0.0f};
    float rootedge = 0.0f;
    int rootnode = kNoProxyNode;
};

}  // namespace Render

#endif  // RENDERER_PROXY_HIERARCHY_H
