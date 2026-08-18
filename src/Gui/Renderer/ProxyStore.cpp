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

#include "ProxyStore.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>

using namespace Render;

namespace {

double millisSince(const std::chrono::steady_clock::time_point &start)
{
    const std::chrono::duration<double, std::milli> elapsed =
        std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

/// One member of a merge, and where its error came from.
///
/// A member is either drawn geometry -- exact, so it contributes no
/// error of its own -- or a child's proxy, which arrives having already
/// committed one. Keeping them in the same list is what lets a node's
/// residents and its children's proxies merge in a single call, which
/// section 7.1 asks for: a node above the leaves usually has both.
struct MemberSource {
    ProxyMember member;
    uint64_t key = 0;      ///< objectKey, or a child node id
    bool isNode = false;
    float error = 0.0f;    ///< what this member had already committed
    double sourceArea = 0.0;
    double mergedArea = 0.0;
};

}  // namespace

void ProxyStore::clear()
{
    entrydata.clear();
    byKey.clear();
    statistics = ProxyStoreStats();
}

std::vector<const ProxyEntry *> ProxyStore::at(uint64_t nodeId) const
{
    std::vector<const ProxyEntry *> found;
    for (auto it = byKey.lower_bound({nodeId, 0});
         it != byKey.end() && it->first.first == nodeId; ++it)
        found.push_back(&entrydata[it->second]);
    return found;
}

const ProxyEntry *ProxyStore::find(uint64_t nodeId, uint64_t bucket) const
{
    const auto it = byKey.find({nodeId, bucket});
    return it == byKey.end() ? nullptr : &entrydata[it->second];
}

bool ProxyStore::generate(const ProxyHierarchy &index, int node,
                          const DrawCallList &draws, const Options &opts)
{
    clear();
    if (index.empty() || node == kNoProxyNode)
        return false;
    const auto start = std::chrono::steady_clock::now();
    uint32_t span = 0;
    build(index, node, draws, opts, span);
    statistics.maxLevelSpan = span;
    statistics.ms = millisSince(start);
    return !entrydata.empty();
}

uint32_t ProxyStore::build(const ProxyHierarchy &index, int node,
                           const DrawCallList &draws, const Options &opts,
                           uint32_t &levelSpan)
{
    levelSpan = 0;
    if (node == kNoProxyNode || node >= int(index.nodes().size()))
        return 0;
    const ProxyNode &n = index.nodes()[size_t(node)];

    // Children first, always -- even under the from-source control, so
    // that the two modes generate the same set of nodes and differ only
    // in what each merge is fed.
    uint32_t childSpan = 0;
    for (int c : n.child) {
        if (c == kNoProxyNode)
            continue;
        uint32_t span = 0;
        build(index, c, draws, opts, span);
        childSpan = std::max(childSpan, span);
    }
    levelSpan = childSpan;

    const float cellEdge = n.cellMax[0] - n.cellMin[0];
    if (!(cellEdge > 0.0f) || opts.subdivision == 0)
        return 0;

    // The members, grouped by material bucket: a proxy carries exactly
    // one material and nothing is ever averaged (section 5.1).
    std::map<uint64_t, std::vector<MemberSource>> byBucket;
    // The MeshData a child's proxy is read through is a view, and a
    // ProxyMember holds a pointer to it, so the views have to outlive
    // the merge and must not move while the list is being built.
    std::deque<MeshData> views;

    const auto addDraw = [&](uint32_t instanceIndex) {
        const ProxyInstance &inst = index.instances()[instanceIndex];
        if (inst.drawIndex >= draws.size())
            return;
        const DrawCall &draw = draws[inst.drawIndex];
        // Lines and points are their own buckets and stay exact; a
        // stand-in is not geometry at all.
        if (draw.material.type != Material::Triangle || draw.standIn
                || !draw.mesh)
            return;
        MemberSource src;
        src.member.mesh = draw.mesh.get();
        src.member.model = draw.identity ? nullptr : draw.model;
        src.member.indexStart = draw.indexStart;
        src.member.indexCount = draw.indexCount;
        src.member.objectKey = draw.objectKey;
        src.key = draw.objectKey;
        byBucket[inst.materialBucket].push_back(src);
    };

    if (opts.fromSource) {
        // The control: everything below the node, from the geometry it
        // was tessellated from, which is what the phase 2 readout did.
        std::vector<uint32_t> subtree;
        index.subtreeInstances(node, subtree);
        for (uint32_t inst : subtree)
            addDraw(inst);
    }
    else {
        // Section 7.1: this node's own residents, plus what its
        // children already built -- their proxies and, with them, the
        // boxes standing in for what those children deleted. Handing
        // the boxes to the merge as ordinary geometry is what keeps the
        // deleted mass bounded by the grid: a box below the parent's
        // cell size collapses like any small member and comes back as
        // one box of the parent's own cell (11.1d).
        for (uint32_t i = 0; i < n.residentCount; ++i)
            addDraw(index.residents()[size_t(n.residentFirst + i)]);
        for (int c : n.child) {
            if (c == kNoProxyNode)
                continue;
            const uint64_t childId = index.nodes()[size_t(c)].id;
            for (const ProxyEntry *entry : at(childId)) {
                // The child's source area is carried by exactly one of
                // its two meshes, or it would be counted twice; the
                // proxy carries it where there is one, the stand-ins
                // where the decimation left nothing else.
                bool carried = false;
                const auto push = [&](const SimplifiedMesh &mesh,
                                      double mergedArea) {
                    if (mesh.triangleIndices.empty())
                        return;
                    views.emplace_back();
                    mesh.fill(views.back());
                    MemberSource src;
                    src.member.mesh = &views.back();
                    src.member.objectKey = childId;
                    src.key = childId;
                    src.isNode = true;
                    src.error = entry->error;
                    src.mergedArea = mergedArea;
                    if (!carried) {
                        src.sourceArea = entry->sourceArea;
                        carried = true;
                    }
                    byBucket[entry->bucket].push_back(src);
                };
                push(entry->proxy, entry->stats.proxyArea);
                push(entry->standIns, entry->standInStats.area);
            }
        }
    }

    uint32_t made = 0;
    for (auto &bucket : byBucket) {
        std::vector<MemberSource> &sources = bucket.second;
        // minMerge is about not proxying a single *object*: merging one
        // gives a decimated object, which the per-object ladder already
        // does better. A single child proxy is not that case -- coarsening
        // it onto this level's grid is exactly what this level is for,
        // and refusing would break the chain wherever a subtree narrows
        // to one bucket, leaving the cut nothing to stop on.
        bool onlyNodes = true;
        for (const MemberSource &src : sources)
            onlyNodes = onlyNodes && src.isNode;
        const size_t needed = onlyNodes ? 1u : index.params().minMerge;
        if (sources.size() < needed) {
            ++statistics.belowMinMerge;
            continue;
        }
        std::vector<ProxyMember> members;
        members.reserve(sources.size());
        uint64_t wouldMerge = 0;
        for (const MemberSource &src : sources) {
            members.push_back(src.member);
            wouldMerge += uint64_t(src.member.indexCount > 0
                                       ? src.member.indexCount
                                       : src.member.mesh->numTriangleIndices)
                / 3;
        }
        if (opts.triangleBudget
                && statistics.mergedTriangles + wouldMerge
                    > opts.triangleBudget) {
            ++statistics.overBudget;
            continue;
        }

        ProxyEntry entry;
        entry.nodeId = n.id;
        entry.bucket = bucket.first;
        entry.level = n.level;
        entry.node = node;

        SimplifiedMesh merged;
        std::vector<uint64_t> keys;
        if (!mergeProxyMembers(members, merged, &keys, &entry.stats))
            continue;
        statistics.mergedTriangles += entry.stats.sourceTriangles;
        statistics.mergeMs += entry.stats.mergeMs;

        ProxyMeshParams params;
        // The node's cell corner is a point of every grid the level
        // subdivides into, so anchoring here anchors on the level's
        // grid rather than on this node's contents (SimplifyOptions).
        params.anchor[0] = n.cellMin[0];
        params.anchor[1] = n.cellMin[1];
        params.anchor[2] = n.cellMin[2];
        params.cellSize = cellEdge / float(opts.subdivision);
        const bool decimated =
            decimateProxyMesh(merged, params, entry.proxy, &entry.stats);
        statistics.simplifyMs += entry.stats.simplifyMs;
        if (opts.standIns) {
            buildStandIns(merged, entry.proxy, params, StandInMode::PerCell,
                          entry.standIns, &entry.standInStats);
            statistics.standInMs += entry.standInStats.ms;
        }
        if (!decimated && entry.standIns.triangleIndices.empty()) {
            // Nothing survived and nothing stands in for it: there is
            // no proxy here, and saying so is the honest answer -- the
            // cut then descends rather than drawing a hole.
            ++statistics.refused;
            continue;
        }

        entry.partKeys = keys;
        entry.partIsNode.assign(sources.size(), 0);
        for (size_t i = 0; i < sources.size() && i < entry.partIsNode.size();
             ++i)
            entry.partIsNode[i] = sources[i].isNode ? 1 : 0;

        // The error against the source, not against what was merged.
        // Worst input plus this level's displacement: the triangle
        // inequality, so a bound rather than an estimate, and monotone
        // up the tree because the first term is a maximum over children
        // that have already been generated (section 8.1).
        float inherited = 0.0f;
        double childSource = 0.0;
        double childMerged = 0.0;
        for (const MemberSource &src : sources) {
            inherited = std::max(inherited, src.error);
            childSource += src.sourceArea;
            childMerged += src.mergedArea;
        }
        entry.error = inherited + entry.stats.maxError;
        entry.extent = entry.stats.extent;
        entry.errorRatio =
            entry.extent > 0.0f ? entry.error / entry.extent : 0.0f;
        // What was merged is part source geometry and part meshes that
        // already stand for source geometry, so the area below is the
        // children's own accounting plus whatever of this merge was not
        // theirs.
        entry.sourceArea =
            childSource + std::max(0.0, entry.stats.sourceArea - childMerged);
        entry.keptArea = entry.stats.proxyArea + entry.standInStats.area;

        statistics.proxyTriangles += entry.proxy.triangleIndices.size() / 3;
        statistics.standInTriangles +=
            entry.standIns.triangleIndices.size() / 3;
        ++statistics.entries;
        ++made;

        byKey[{entry.nodeId, entry.bucket}] = entrydata.size();
        entrydata.push_back(std::move(entry));
    }

    if (made) {
        ++statistics.nodes;
        // What generating this node from source would have cost, which
        // is the comparison section 7.1's O(leaf triangles) claim is
        // about. Taken from the partition rather than counted again:
        // subtreePrims is triangles for a triangle bucket.
        statistics.sourceTriangles += n.subtreePrims;
        levelSpan = childSpan + 1;
    }
    return made;
}
