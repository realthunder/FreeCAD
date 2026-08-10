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

#include "OcclusionCull.h"

#include <cmath>

using namespace Render;

void OcclusionCuller::configure(const OcclusionCullConfig &config)
{
    // ⚠️ A verdict taken with a different test box is not evidence about
    // this one. The padding terms change the box that was rasterized, so
    // changing either has to drop what the old box answered -- otherwise
    // a node stays culled on the strength of a test the current settings
    // would never have made, and the culling that is running is a blend
    // of two configurations. Being switched off and on again is the same
    // case: nothing was tested in between, so every verdict has aged by
    // an unknown amount against a camera that was free to move.
    //
    // Only the hidden bits go. The index is untouched: it is a partition
    // of the draw list, which none of these knobs affect.
    const bool verdictsVoid = conf.padFraction != config.padFraction
        || conf.depthPadLsb != config.depthPadLsb
        || conf.hiddenConfirm != config.hiddenConfirm
        || (config.enabled && !conf.enabled);
    conf = config;
    if (!verdictsVoid)
        return;
    for (auto &st : state) {
        st.hidden = 0;
        st.hiddenStreak = 0;
        st.answered = 0;
        st.lastPx = -1;
    }
}

void OcclusionCuller::build(const std::vector<ProxyInstance> &instances,
                            const ProxyParams &params)
{
    index.build(instances, params);
    // Start from no knowledge rather than from the previous partition's
    // verdicts: see the header on why a verdict may not be carried
    // across a rebuild.
    state.assign(index.nodes().size(), OcclusionNodeState());
    inflight = 0;
    offercursor = 0;
    framestats = OcclusionFrameStats();
}

void OcclusionCuller::clear()
{
    index.clear();
    state.clear();
    inflight = 0;
    offercursor = 0;
    framestats = OcclusionFrameStats();
}

void OcclusionCuller::result(int node, bool visible, int32_t px)
{
    if (node < 0 || size_t(node) >= state.size())
        return;
    OcclusionNodeState &st = state[size_t(node)];
    if (st.pending) {
        st.pending = 0;
        if (inflight)
            --inflight;
    }
    st.answered = 1;
    st.lastAnswer = framecounter;
    st.lastPx = px;
    if (visible) {
        // ⭐ One answer that saw a pixel is enough to draw again, and it
        // resets the streak outright. The asymmetry is the whole point:
        // being slow to cull costs frame time, being quick to cull
        // costs geometry, so evidence for hiding has to accumulate
        // while evidence for drawing never does.
        st.hiddenStreak = 0;
        st.hidden = 0;
        return;
    }
    if (st.hiddenStreak < 255)
        ++st.hiddenStreak;
    // A node already hidden stays hidden on a single confirmation: the
    // streak only has to be *reached*, not re-earned every frame.
    if (st.hiddenStreak >= (conf.hiddenConfirm ? conf.hiddenConfirm : 1))
        st.hidden = 1;
}

int32_t OcclusionCuller::nodePixels(int node) const
{
    if (node < 0 || size_t(node) >= state.size())
        return -1;
    return state[size_t(node)].lastPx;
}

void OcclusionCuller::abandon(int node)
{
    if (node < 0 || size_t(node) >= state.size())
        return;
    OcclusionNodeState &st = state[size_t(node)];
    if (st.pending) {
        st.pending = 0;
        if (inflight)
            --inflight;
    }
    // Deliberately no touch of lastAnswer: an offer that was never
    // answered must age, because ageing is what eventually frees a node
    // whose tests are being dropped.
}

void OcclusionCuller::markSubtree(int node, std::vector<uint8_t> &cullMask)
{
    const auto &nodes = index.nodes();
    const auto &residents = index.residents();
    const auto &instances = index.instances();
    walkstack.clear();
    walkstack.push_back(node);
    while (!walkstack.empty()) {
        const int cur = walkstack.back();
        walkstack.pop_back();
        const ProxyNode &n = nodes[size_t(cur)];
        for (uint32_t r = 0; r < n.residentCount; ++r) {
            const uint32_t inst = residents[n.residentFirst + r];
            const uint32_t row = instances[inst].drawIndex;
            if (row < cullMask.size())
                cullMask[row] = 1;
        }
        for (int c : n.child) {
            if (c != kNoProxyNode)
                walkstack.push_back(c);
        }
    }
}

void OcclusionCuller::cull(const float *view, const float *proj,
                           float viewportHeightPx, bool homogeneousDepth,
                           std::vector<uint8_t> &cullMask,
                           OcclusionTestBatch &batch)
{
    ++framecounter;
    framestats = OcclusionFrameStats();
    batch.mins.clear();
    batch.maxs.clear();
    batch.nodes.clear();
    mustTest.clear();
    mayTest.clear();

    if (index.empty() || index.root() == kNoProxyNode)
        return;
    if (state.size() != index.nodes().size())
        state.assign(index.nodes().size(), OcclusionNodeState());

    const auto &nodes = index.nodes();

    // ⚠️ The padded box, in one place because the box that is *judged*
    // and the box that is *tested* have to be the same one. Two copies
    // of this drifted apart once already: the near-plane exemption is
    // decided on the padded box precisely because padding is what
    // pushes a box through the near plane, so a box offered to the
    // backend with a different pad is a box nobody checked.
    //
    // Two terms, and neither substitutes for the other: a fraction of
    // the box's own diagonal for surfaces that coincide in world space
    // (relative, so it means the same at any model scale), plus the
    // depth buffer's own resolution at this distance for surfaces the
    // buffer cannot separate however far apart they are in model units.
    auto paddedBox = [&](const ProxyNode &n, float *pmin, float *pmax) {
        const float dx = n.contentMax[0] - n.contentMin[0];
        const float dy = n.contentMax[1] - n.contentMin[1];
        const float dz = n.contentMax[2] - n.contentMin[2];
        const float pad =
            conf.padFraction * std::sqrt(dx * dx + dy * dy + dz * dz)
            + depthQuantumPad(n.contentMin, n.contentMax, view, proj,
                              homogeneousDepth, conf.depthPadLsb);
        for (int k = 0; k < 3; ++k) {
            pmin[k] = n.contentMin[k] - pad;
            pmax[k] = n.contentMax[k] + pad;
        }
    };

    // The walk. A hidden node's subtree is cut whole and not descended:
    // that is the entire economy of testing per node rather than per
    // object, and it is also why the verdict has to be conservative —
    // everything below a wrong "hidden" disappears with it.
    std::vector<int> &descend = descendstack;
    descend.clear();
    descend.push_back(index.root());
    while (!descend.empty()) {
        const int node = descend.back();
        descend.pop_back();
        const ProxyNode &n = nodes[size_t(node)];
        OcclusionNodeState &st = state[size_t(node)];
        ++framestats.nodesVisited;

        const BoxSight sight = sightBounds(n.contentMin, n.contentMax, view,
                                           proj, viewportHeightPx);
        if (sight.what == BoxSight::Offscreen) {
            // Left to the frustum mask the renderer computed per draw;
            // counted here only so the readout can separate what
            // occlusion contributed from what the frustum already did.
            ++framestats.nodesOffscreen;
            framestats.offscreenInstances += n.subtreeCount;
            continue;
        }

        // ⚠️ Padded outwards, and judged in that padded form — see
        // OcclusionCullConfig::padFraction and ::depthPadLsb. A test box
        // must be a conservative bound, and neither float equality nor
        // the depth buffer's last bit is conservative.
        float pmin[3], pmax[3];
        paddedBox(n, pmin, pmax);

        // A box the near plane clips cannot be tested at all: the faces
        // that would prove it visible are gone, and the ones left are
        // hidden by the box's own contents, so the query answers
        // "hidden" however plainly the node is in view. A node the
        // camera stands inside is the same case. Both are drawn, and
        // both are descended — a child further away may still be
        // answerable, which is what keeps culling working while the eye
        // is inside the model, the camera §10.3 says is the working
        // case.
        if (sight.what == BoxSight::Inside || sight.what == BoxSight::Empty
                || boxReachesNearPlane(pmin, pmax, view, proj,
                                       homogeneousDepth)) {
            ++framestats.nearExempt;
            st.hidden = 0;
            framestats.drawnInstances += n.residentCount;
            for (int c : n.child) {
                if (c != kNoProxyNode)
                    descend.push_back(c);
            }
            continue;
        }

        // ⭐ A hidden root cannot be true: its box contains every drawn
        // thing, so a frame that put one pixel on the screen has a
        // visible root. Acting on it would mask the entire model, and
        // the resulting empty frame is fast — which is exactly how this
        // failure disguises itself as a result. Refuse, count, descend.
        if (st.hidden && node == index.root()) {
            st.hidden = 0;
            ++framestats.rootRefused;
        }

        if (st.hidden) {
            // ⚠️ The fail-safe. A hidden node's only way back is the
            // answer to a test, so if answers stop arriving it must
            // revert rather than stay hidden — the difference between
            // this costing frame time and it costing geometry.
            if (framecounter - st.lastAnswer >= conf.maxHiddenFrames) {
                st.hidden = 0;
                // Re-entering the visible set on no evidence at all, so
                // the streak has to be earned again from zero rather
                // than resumed where the unanswered tests left it.
                st.hiddenStreak = 0;
                ++framestats.forcedVisible;
            }
            else {
                markSubtree(node, cullMask);
                framestats.hiddenInstances += n.subtreeCount;
                ++framestats.nodesHidden;
                if (!st.pending)
                    mustTest.push_back(node);
                continue;
            }
        }

        // Visible: this node's own residents draw — an instance too
        // large to descend is covered by nobody below it — and the
        // question is asked again of each child.
        framestats.drawnInstances += n.residentCount;
        if (!st.pending && n.subtreeCount >= conf.minSubtree
                && (!st.answered
                    || framecounter - st.lastAnswer >= conf.visibleTtl))
            mayTest.push_back(node);
        for (int c : n.child) {
            if (c != kNoProxyNode)
                descend.push_back(c);
        }
    }

    framestats.nodesOffered = uint32_t(mustTest.size() + mayTest.size());

    // Hidden nodes are offered first and unconditionally: they are the
    // ones currently being cut, and a test is the only thing that can
    // give them back. A visible node that misses its slot merely keeps
    // drawing for another frame.
    //
    // The cursor rotates the starting point so that a model with more
    // hidden nodes than the budget re-tests all of them over successive
    // frames instead of the same prefix forever — without it the tail
    // would be left to the fail-safe, which is a much blunter way back.
    auto offer = [&](int node) {
        const ProxyNode &n = nodes[size_t(node)];
        float pmin[3], pmax[3];
        paddedBox(n, pmin, pmax);
        for (int k = 0; k < 3; ++k) {
            batch.mins.push_back(pmin[k]);
            batch.maxs.push_back(pmax[k]);
        }
        batch.nodes.push_back(node);
        state[size_t(node)].pending = 1;
        ++inflight;
    };

    const uint32_t budget = conf.budget;
    // ⚠️ Re-tests may not consume the whole budget, however many nodes
    // are hidden. They would otherwise starve discovery completely: at
    // the moment the number of hidden nodes reaches the budget, every
    // slot goes to confirming what is already known and no new node is
    // ever tested, so the culling freezes at exactly `budget` nodes
    // however much of the model is in fact hidden. Reserving a quarter
    // of the slots costs the hidden nodes some re-test frequency — the
    // cursor below rotates them, and the fail-safe still bounds how
    // long any of them can go unanswered — and buys a mechanism that
    // keeps growing.
    const uint32_t retestCap = budget > 3 ? (budget * 3) / 4 : budget;
    if (!mustTest.empty()) {
        const uint32_t count = uint32_t(mustTest.size());
        const uint32_t start = offercursor % count;
        uint32_t taken = 0;
        for (uint32_t i = 0; i < count && taken < retestCap; ++i) {
            offer(mustTest[(start + i) % count]);
            ++taken;
        }
        offercursor = (start + taken) % count;
    }
    for (size_t i = 0; i < mayTest.size() && batch.nodes.size() < budget; ++i)
        offer(mayTest[i]);

    framestats.nodesTested = uint32_t(batch.nodes.size());
}
