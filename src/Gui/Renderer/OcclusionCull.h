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

#ifndef RENDERER_OCCLUSION_CULL_H
#define RENDERER_OCCLUSION_CULL_H

/// Occlusion culling over the renderer's spatial index
/// (docs/FarFieldProxies.md §10.4, §12) — the mechanism
/// `RenderDebug_Occlusion` measured, now acting on its answers.
///
/// **Why the draw must be removed and not merely shortened.** §10.2
/// measured a frame at ~1.2-1.5 µs of CPU submission plus ~1.5-1.7 µs of
/// GPU time *per draw call*, and the GPU half is per-draw state rather
/// than fill or triangles: sixteen times fewer pixels changed it by 0%,
/// and a thirty-fold difference in triangles per draw by 12%. So a
/// scheme that still issues the draw saves nothing, whichever side is
/// counted. Culling here is therefore a CPU-side decision taken before
/// submission, and what it produces is a mask over draw rows.
///
/// **Why per node and not per object.** §10.3 measured eight node tests
/// removing 99.9% of the instances of a 5455-part assembly. The index
/// (`ProxyHierarchy`) is the same one the far-field cut descends — §3 of
/// that document always said level of detail was one consumer of it and
/// culling another — so nothing new is partitioned to get this.
///
/// **What it does not promise.** The honest working number is §10.3's
/// in-model camera, not its whole-assembly one: 33.5% hidden, 48.3%
/// already rejected by the frustum, 18.2% still drawn. This is a ~5×
/// mechanism where the eye is inside the model, and the 1500× reading is
/// what an outside view of a closed chassis flatters it to.
///
/// Nothing here knows about bgfx, Coin, Qt or OCCT, and the arithmetic
/// is plain float: the visibility *policy* — who is tested, when a
/// verdict expires, what happens when the answers do not arrive — is the
/// part worth sharing with the browser tier, where only the box
/// rasterization differs.

#include <cstdint>
#include <vector>

#include "ProxyHierarchy.h"
#include "Renderer.h"

namespace Render {

/// The visibility state of one index node, across frames.
///
/// Deliberately tiny: there is one per node and a large assembly
/// partitions into thousands, walked every frame.
struct OcclusionNodeState {
    /// Enough consecutive answers of "no pixels" have arrived to act.
    /// While set, the node's whole subtree is masked out and not
    /// descended.
    uint8_t hidden = 0;
    /// ⭐⭐ Consecutive "no pixels" answers, reset by any answer that
    /// saw one. The verdict is only acted on at
    /// `OcclusionCullConfig::hiddenConfirm`, and that hysteresis is
    /// what makes the mechanism stable rather than merely correct on
    /// average (§12.6).
    ///
    /// A query is issued against one frame's depth and read against a
    /// later one — deliberately, since blocking for it would cost the
    /// frame time this exists to save — so between the two, other
    /// nodes are culled and the occluders move underneath the answer.
    /// Acted on singly, a node tested while an occluder was still drawn
    /// is culled after that occluder has gone; the hole it leaves tests
    /// visible; it comes back; and it oscillates with a period of two.
    /// Measured, that swung the drawn set between 12 and 8424 instances
    /// of 17727 and made the picture flicker. A node that is genuinely
    /// hidden answers so every time and loses only the confirmations;
    /// an oscillator never gets two in a row.
    uint8_t hiddenStreak = 0;
    /// A query is in flight and this node must not be offered again
    /// until it is answered or abandoned.
    uint8_t pending = 0;
    /// Whether any answer has ever arrived for this node. Distinct from
    /// `lastAnswer == 0` because frame numbering starts there: without
    /// it a node that has never been tested reads as one tested on
    /// frame zero, and on a young index that means *nothing is ever
    /// offered a first test*.
    uint8_t answered = 0;
    /// Frame of the last answer actually received. Aging is measured
    /// from this rather than from when a test was *offered*, because
    /// the fail-safe below exists precisely for the case where offers
    /// are made and never answered.
    uint32_t lastAnswer = 0;
    /// Pixels the last answer carried, -1 if never answered. Kept for
    /// the readout only: a verdict of "hidden" and a verdict of "the
    /// query is not answering" are the same byte otherwise, and the
    /// second is a bug that looks exactly like a spectacular result.
    int32_t lastPx = -1;

    // ⛔ Not recorded here: whether the deciding test was taken while
    // the node's own contents were drawn. §12.6's account of the
    // per-test failure is that a *drawn* node loses a depth tie against
    // its own geometry, and the obvious instrument is to flag which set
    // the offer came from. It would answer "always drawn", for every
    // node, always -- and mean nothing: only a node that is *already*
    // hidden is offered from the hidden set, so the answer that first
    // sets `hidden` can only ever come from the drawn set. A tautology
    // reported as a measurement is the failure mode this whole section
    // exists to avoid (see OcclusionFrameStats::rootRefused), so the
    // field is deliberately absent rather than present and misleading.
    // What separates a query that lied from a world that moved is
    // below: the age of the deciding answer, and how often the node has
    // flipped.

    /// How many times this node has entered the hidden state. ⭐ One
    /// separates a stable wrong verdict from an oscillator without
    /// needing a second frame to compare against: a node that entered
    /// once and stayed is answering consistently (and wrongly), while
    /// a node with a high count is flipping.
    uint16_t hidEvents = 0;
    /// Frame `hidden` was last set from clear. Ages the verdict itself
    /// rather than the answer that maintains it.
    uint32_t hidFrame = 0;
};

/// One node's visibility state, flattened for the readout
/// (docs/FarFieldProxies.md §12.10). Nothing here drives the mechanism;
/// it exists so that an over-culled draw can be traced back to the
/// verdict that deleted it, and that verdict judged for freshness and
/// stability.
struct OcclusionNodeAudit {
    uint32_t level = 0;
    uint32_t residentCount = 0;
    uint32_t subtreeCount = 0;
    int32_t lastPx = -1;
    uint32_t framesSinceAnswer = 0;
    uint32_t framesHidden = 0;
    uint16_t hidEvents = 0;
    uint8_t hidden = 0;
    uint8_t hiddenStreak = 0;
    uint8_t answered = 0;
};

/// Which nodes a frame wants tested, as boxes for the backend to
/// rasterize against the finished opaque depth.
///
/// ⚠️ The boxes are already padded outwards; see the note on
/// `OcclusionCullConfig::padFraction` for why the measurement does not
/// work without it.
struct OcclusionTestBatch {
    std::vector<float> mins;   ///< three floats per entry
    std::vector<float> maxs;
    std::vector<int> nodes;    ///< index into the hierarchy's nodes()
};

/// What one frame's walk did. Reported rather than inferred, per
/// docs/RenderDebug.md §1.
struct OcclusionFrameStats {
    uint32_t nodesVisited = 0;
    uint32_t nodesHidden = 0;      ///< subtrees cut by a hidden verdict
    uint32_t nodesOffscreen = 0;   ///< subtrees the frustum rejected
    uint32_t nodesOffered = 0;     ///< tests this frame wanted
    uint32_t nodesTested = 0;      ///< tests the batch had room for
    uint32_t nearExempt = 0;       ///< unanswerable, so drawn
    uint32_t forcedVisible = 0;    ///< the starvation fail-safe fired
    /// ⭐ The root answered hidden and was refused. Structurally
    /// impossible — the root's box contains every drawn thing, so a
    /// frame that put one pixel on screen has a visible root — which
    /// makes any non-zero count here a report that the box test is not
    /// answering, not a scene that is entirely hidden. Acted on, it
    /// would blank the model; the count is the symptom made visible
    /// rather than a fast, empty, entirely wrong picture.
    uint32_t rootRefused = 0;
    uint32_t hiddenInstances = 0;
    uint32_t offscreenInstances = 0;
    uint32_t drawnInstances = 0;
};

/// Maintains a per-node verdict and turns it into a per-draw mask.
///
/// The shape is CHC++'s: a verdict is a frame old, previously visible
/// nodes are believed and re-checked occasionally, previously hidden
/// nodes are cut but re-tested continuously so that they have a way
/// back. What is deliberately *not* CHC++ is interleaving the queries
/// with the traversal to test against a partially filled depth buffer —
/// here every query is asked once, after the whole opaque pass, which is
/// both simpler and the only placement that answers correctly (see the
/// depth-target note in `docs/FarFieldProxies.md` §10.2 and
/// `RenderDebug_Occlusion`'s history: asked at the end of the frame, the
/// root itself read hidden).
class RendererExport OcclusionCuller
{
public:
    void configure(const OcclusionCullConfig &config);
    const OcclusionCullConfig &config() const { return conf; }

    /// Partition \a instances and start from no knowledge.
    ///
    /// ⚠️ **Verdicts never survive a rebuild.** Node identity is
    /// positional and therefore stable across builds, but node
    /// *indices* are not, and a verdict applied to the wrong node hides
    /// geometry that was never tested. One frame of drawing everything
    /// is the correct price.
    void build(const std::vector<ProxyInstance> &instances,
               const ProxyParams &params = ProxyParams());
    void clear();
    bool empty() const { return index.empty(); }
    const ProxyHierarchy &hierarchy() const { return index; }

    /// Walk the index against this camera and mask what cannot be seen.
    ///
    /// \a cullMask is indexed by `DrawCall` row — `ProxyInstance::drawIndex`
    /// — and is only ever *set*, never cleared, so it composes with the
    /// frustum mask the renderer has already computed rather than
    /// overwriting it. It must already be sized to the draw list.
    ///
    /// \a batch is filled with the boxes to test; the caller submits as
    /// many as it can and reports each answer through result(), or
    /// abandon() for the ones it dropped.
    ///
    /// \a cullOwner, when given, is sized to \a cullMask and receives
    /// the node whose hidden verdict cut each row, -1 for a row this
    /// walk did not cut. ⭐ It is what makes an over-culled draw
    /// traceable: the mask alone cannot say whether a wrongly deleted
    /// row was deleted by occlusion or by the frustum test that shares
    /// the mask, and those are different bugs.
    void cull(const float *view, const float *proj, float viewportHeightPx,
              bool homogeneousDepth, std::vector<uint8_t> &cullMask,
              OcclusionTestBatch &batch,
              std::vector<int32_t> *cullOwner = nullptr);

    /// One query's answer. \a visible false means the box put no
    /// fragment through the depth test, so nothing at or below the node
    /// can have reached the screen.
    void result(int node, bool visible, int32_t px = -1);
    /// The last answer's pixel count for \a node, -1 if never answered.
    int32_t nodePixels(int node) const;
    /// \a node's visibility state, for the readout. An out-of-range
    /// node returns the default, which reads as "never answered".
    OcclusionNodeAudit nodeAudit(int node) const;
    /// A test that was offered and will never be answered — the backend
    /// had no query handle, or the walk was thrown away. The node keeps
    /// whatever it believed and is simply offered again.
    void abandon(int node);
    /// True while at least one query is in flight.
    bool testsPending() const { return inflight != 0; }

    const OcclusionFrameStats &lastFrame() const { return framestats; }
    uint32_t frame() const { return framecounter; }

private:
    void markSubtree(int node, std::vector<uint8_t> &cullMask,
                     std::vector<int32_t> *cullOwner);

    ProxyHierarchy index;
    std::vector<OcclusionNodeState> state;
    OcclusionCullConfig conf;
    OcclusionFrameStats framestats;
    uint32_t framecounter = 0;
    uint32_t inflight = 0;
    /// Where the last over-budget frame stopped offering tests. Without
    /// it a model with more hidden nodes than the query pool would
    /// re-test the same prefix every frame and leave the tail to the
    /// fail-safe forever.
    uint32_t offercursor = 0;
    /// Scratch, kept across frames so that a walk allocates nothing.
    /// Two stacks rather than one: masking a hidden subtree runs inside
    /// the traversal that found it.
    std::vector<int> walkstack;
    std::vector<int> descendstack;
    std::vector<int> mustTest;
    std::vector<int> mayTest;
};

}  // namespace Render

#endif  // RENDERER_OCCLUSION_CULL_H
