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

    bool leaf() const
    {
        for (int c : child) {
            if (c != kNoProxyNode)
                return false;
        }
        return true;
    }
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
    /// Draws the cut issues: the exact ones, plus one per distinct
    /// material bucket of each proxy node (§5.1). **This is the number
    /// phase 1 exists to produce.**
    uint32_t drawCount = 0;
    uint32_t proxyDraws = 0;          ///< the merged part of drawCount
    uint32_t coveredInstances = 0;    ///< instances a proxy stands for
    uint32_t culledInstances = 0;     ///< off screen, drawn by nobody
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
