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

#ifndef RENDERER_PROXY_STORE_H
#define RENDERER_PROXY_STORE_H

/// Where the far-field proxies of docs/FarFieldProxies.md are generated
/// and kept -- section 7.1, the bottom-up half of phase 2.
///
/// The readout that measured phase 2 generated every node it sampled
/// from source geometry, which is the thing section 7.1 says not to do.
/// A node's proxy is produced by merging **its children's proxies**
/// together with its own residents, and three properties follow from
/// that and from nothing else:
///
/// - total generation is O(leaf triangles) rather than O(levels x
///   leaves), because each level merges a decimation of the level below
///   instead of the source again;
/// - the error becomes monotone up the tree by construction, which is
///   what section 8.1 needs for a node to decide independently while
///   the cut stays globally consistent;
/// - a change costs a path rather than a tree: the edited part's leaf
///   is rebuilt from source and every ancestor re-merges from children
///   that are already built.
///
/// It also decides where the deleted mass goes, and 11.1d settled that:
/// per occupied cell. That choice is what makes the recursion close.
/// A child's stand-in boxes are handed to the parent's merge as
/// ordinary geometry, so a box too small for the parent's grid collapses
/// exactly as a small part does and is re-emitted as one box of the
/// parent's own cell. The deleted mass therefore stays bounded by the
/// grid at every level instead of accumulating up the tree, which is
/// what a per-member table would have done.
///
/// Keyed by ProxyNode::id, which is positional (level plus Morton code)
/// and identical across sessions and machines -- section 3.2 chose it
/// that way precisely so a generation cache could be shared rather than
/// rebuilt per session.

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

#include "MeshSimplify.h"
#include "ProxyHierarchy.h"

namespace Render {

/// One generated proxy: what a node draws for one material bucket.
struct ProxyEntry {
    uint64_t nodeId = 0;
    uint64_t bucket = 0;
    uint32_t level = 0;
    int node = kNoProxyNode;  ///< index into the hierarchy that built it

    /// The decimated merge, and the boxes standing in for what it
    /// deleted (11.1d). Kept apart rather than appended because the
    /// stand-ins are a different kind of claim about the model -- this
    /// much mass is here, shape unknown -- and because a caller may
    /// want them under their own material or not at all.
    SimplifiedMesh proxy;
    SimplifiedMesh standIns;
    /// What each part slot of \ref proxy names: a source instance's
    /// objectKey where the member was drawn geometry, and a child
    /// node's id where it was that child's proxy. A pick that lands on
    /// a slot naming a node has to descend into that node's own table
    /// to get further, which is the honest answer -- identity thins as
    /// the merge climbs, it does not become wrong.
    std::vector<uint64_t> partKeys;
    /// True for the slots of \ref partKeys that name a child node
    /// rather than an instance.
    std::vector<uint8_t> partIsNode;

    ProxyMeshStats stats;
    StandInStats standInStats;

    /// The error this proxy commits against the **source** geometry,
    /// not against the meshes it was merged from.
    ///
    /// One level's displacement bounds how far its own input moved; a
    /// proxy built from proxies has to add what its inputs had already
    /// committed, or a node three levels up would report the error of
    /// its last decimation alone. Accumulated as the worst input error
    /// plus this level's displacement, which is the triangle inequality
    /// and therefore a bound rather than an estimate -- and monotone up
    /// the tree because a parent's term is a maximum over its children.
    float error = 0.0f;
    /// The same quantity as a fraction of the node's content extent,
    /// which is the form section 3.3's cut reads: a node descends when
    /// its own projected error exceeds the tolerance, and the ratio is
    /// a property of geometry and grid rather than of the camera
    /// (11.1c), so it is measured once here and stored.
    float errorRatio = 0.0f;
    float extent = 0.0f;

    /// Surface area of the source below this node, and how much of it
    /// is still represented -- by triangles or by a stand-in box. The
    /// displacement above cannot see a deletion, so the cut needs this
    /// beside it (11.1c, and section 4.1's requirement that the stored
    /// error account for what is missing).
    double sourceArea = 0.0;
    double keptArea = 0.0;

    /// Triangles this node draws if the cut stops on it: the proxy and
    /// its stand-ins together. What phase 3 prices a node at.
    uint32_t drawnTriangles() const
    {
        return uint32_t(proxy.triangleIndices.size() / 3
                        + standIns.triangleIndices.size() / 3);
    }
};

/// What a generation pass cost, and what it produced.
struct ProxyStoreStats {
    uint32_t nodes = 0;    ///< nodes that produced at least one entry
    uint32_t entries = 0;  ///< (node, bucket) pairs generated
    uint32_t refused = 0;  ///< groups that decimated to nothing
    uint32_t belowMinMerge = 0;
    uint32_t overBudget = 0;

    /// The claim of section 7.1, in the only unit that can test it.
    /// `mergedTriangles` is what generation actually pushed through the
    /// merge; `sourceTriangles` is what the same nodes would have cost
    /// generated from source geometry, which is what the phase 2
    /// readout did. Their ratio is the geometric series the bottom-up
    /// rule buys.
    uint64_t mergedTriangles = 0;
    uint64_t sourceTriangles = 0;
    uint64_t proxyTriangles = 0;
    uint64_t standInTriangles = 0;
    uint32_t maxLevelSpan = 0;  ///< deepest chain of merges that ran

    double mergeMs = 0.0;
    double simplifyMs = 0.0;
    double standInMs = 0.0;
    double ms = 0.0;
};

struct ProxyGenOptions {
    /// The decimation grid of a node is its cell edge divided by
    /// this. A power of two, so that every level's grid stays a
    /// refinement of the level above it (section 3.2) -- with a
    /// node's cell corner as the anchor, which is on every grid the
    /// level subdivides into.
    uint32_t subdivision = 8;
    /// Stop after this many source triangles have gone through the
    /// merge. Zero is unbounded. A generation pass over a whole
    /// assembly is the whole assembly, so a caller that wants a
    /// sample says so here and reads ProxyStoreStats::overBudget to
    /// see what it did not get.
    uint64_t triangleBudget = 0;
    /// Carry the deleted mass (11.1d). Off, a node's small members
    /// simply vanish, which is what phase 2 measured and what this
    /// exists to stop.
    bool standIns = true;
    /// Generate every node from source geometry instead of from its
    /// children's proxies. The control for the measurement, and the
    /// thing section 7.1 argues against -- kept because "bottom-up
    /// is cheaper" is a claim about this codebase's numbers, and a
    /// claim wants an experiment beside it rather than a citation.
    bool fromSource = false;
};

/// Generate and hold the proxies of a partition, bottom-up.
class RendererExport ProxyStore
{
public:
    /// See ProxyGenOptions, which lives outside the class only
    /// because a default argument cannot name a nested type still
    /// being defined.
    using Options = ProxyGenOptions;

    /// Generate the subtree rooted at \a node and keep the result.
    ///
    /// Post-order: every child of a node is complete before the node
    /// merges, which is what makes a parent's merge cheap and its error
    /// a maximum over numbers that already exist. Returns false when
    /// nothing was generated at all.
    bool generate(const ProxyHierarchy &index, int node,
                  const DrawCallList &draws, const Options &opts = Options());

    /// Every entry a node produced, or an empty range. Ordered by
    /// material bucket, which is the order a cut would issue them in.
    std::vector<const ProxyEntry *> at(uint64_t nodeId) const;
    const ProxyEntry *find(uint64_t nodeId, uint64_t bucket) const;

    /// A deque, not a vector: generation holds pointers to a child's
    /// entries while it merges them into the parent's, and a growing
    /// vector would move the entries out from under those pointers.
    /// What the cut has to read, indexed by node index of \a index
    /// (section 3.3): the worst error ratio over a node's buckets, the
    /// primitives all of them draw together, and how many draws that
    /// is. A node with no entry keeps a negative ratio, which is what
    /// tells the descent it cannot stop there.
    ///
    /// Worst rather than mean over the buckets because the cut's
    /// decision covers a cell's buckets at once (section 3.3), so the
    /// one that would look wrong is the one that decides.
    void costs(const ProxyHierarchy &index,
               std::vector<ProxyNodeCost> &out) const;

    const std::deque<ProxyEntry> &entries() const { return entrydata; }
    const ProxyStoreStats &stats() const { return statistics; }
    void clear();

private:
    /// Generate one node, its children first. Returns the number of
    /// entries the node itself produced.
    uint32_t build(const ProxyHierarchy &index, int node,
                   const DrawCallList &draws, const Options &opts,
                   uint32_t &levelSpan);

    std::deque<ProxyEntry> entrydata;
    /// (node id, bucket) -> index into entrydata.
    std::map<std::pair<uint64_t, uint64_t>, size_t> byKey;
    ProxyStoreStats statistics;
};

}  // namespace Render

#endif  // RENDERER_PROXY_STORE_H
