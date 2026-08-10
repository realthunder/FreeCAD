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

#ifndef RENDERER_OCCLUDER_MESH_H
#define RENDERER_OCCLUDER_MESH_H

/// Coarse stand-ins for the meshes the occluder pass rasterizes
/// (docs/FarFieldProxies.md 12.16).
///
/// **What this is for.** The CPU occlusion pass is its rasterization --
/// 69% of it, measured (section 12.15) -- and it spends that budget on
/// almost nothing: 1285 of 1322 candidate occluders never entered the
/// buffer, because 37 full-detail draws ate the whole 250000-triangle
/// allowance. The buffer then hid 45% of a 95.4% ceiling. Both terms
/// have the same cause and the same fix: an occluder does not need the
/// mesh, it needs the *surface*, and a rung of the existing
/// tessellation ladder carries that at a fraction of the triangles.
///
/// **The hull is built from the mesh, not from the shape.** Vertex
/// clustering (MeshSimplify.h) needs no OCCT, no Part and no source
/// geometry -- only the arrays the renderer is already holding -- so it
/// is the same code on the desktop and in the browser, and a hull can
/// be made anywhere a mesh is. That is what makes this the cheap first
/// step: if the gate below passes, no new geometry machinery is needed
/// at all.
///
/// ⚠️⚠️ **A hull is not automatically safe.** Anything that moves a
/// surface *towards* the camera invents occlusion, and clustering
/// minimizes displacement without constraining its sign: a chord across
/// a convex surface lies inside it, and the same chord across a concave
/// one bulges out. Two things answer this, in order:
///
///  1. `maxDisplacement` is a *bound*, not an average -- every point of
///     the hull lies within it of a point of the source surface (a
///     triangle's corners each move by at most that much, and every
///     point of the triangle is an affine combination of them). So the
///     hull can be made to recede by exactly that distance, which is
///     what CoarseOccluder::displacement is carried for.
///  2. Whether that is enough is a question for the audit's 0-pixel
///     over-cull gate, not for an argument. The bound is on distance in
///     space, and what the gate measures is pixels.
///
/// **What it may cost, and what it may not.** A hull that hides less
/// than the mesh costs culling and nothing else -- an occluder that is
/// not there hides nothing. A hull that hides *more* deletes visible
/// geometry, which is the fault three sections of this workstream were
/// spent removing. Every default here errs in the first direction.

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Renderer.h"

namespace Render {

/// One mesh's occluder hull: the surface, and how far it moved to
/// become this coarse.
///
/// Deliberately not a MeshData or a SimplifiedMesh. The depth
/// rasterizer reads three positions per triangle and nothing else, and
/// this is held for every occluder in the scene at once, so what it
/// does not carry is as much the point as what it does.
struct CoarseOccluder {
    std::vector<float> positions;          ///< xyz per vertex, mesh space
    std::vector<int32_t> triangleIndices;
    /// Per-face ranges into triangleIndices, index for index with the
    /// source mesh's own table (empty when the source carried none), so
    /// a draw of a single face can find its share of the hull.
    std::vector<std::pair<int, int>> triangleParts;

    /// KEY: The bound on how far this surface moved, in mesh units. The
    /// hull recedes by this much along the view direction before it is
    /// rasterized, which is what keeps it from claiming to be nearer
    /// than the surface it stands for. Zero is a hull that moved
    /// nothing, not a hull that was not measured.
    float displacement = 0.0f;

    uint32_t sourceTriangles = 0;   ///< what it stands in for
    uint32_t triangles() const
    {
        return uint32_t(triangleIndices.size() / 3);
    }
    /// Roughly what this entry holds, for the memory cap. Capacity
    /// rather than size: a cap that counts what was asked for while the
    /// allocator holds twice that is a cap on the wrong number.
    size_t bytes() const;
};

/// What the cache is allowed to build and to keep.
struct CoarseOccluderConfig {
    /// Off, occluders are rasterized from the meshes themselves --
    /// which is what every measurement before section 12.16 did.
    bool enabled = false;

    /// Which rung of MeshSimplify's ladder to build, coarsest first:
    /// the clustering grid is an eighth of the mesh's diagonal at 0 and
    /// halves per level, so 2 is a thirty-second. Matches the default
    /// of Render_CoarseTessellation, which is the same ladder.
    uint32_t level = 2;

    /// Below this many triangles a draw is rasterized from its mesh.
    /// A hull of a small mesh saves triangles that were never the
    /// problem, and it is the *large* occluders that ate the budget.
    uint32_t minTriangles = 512;

    /// How many hulls may be built in one frame. The build is parallel
    /// but it is not free, and a scene that has just come into view
    /// wants its occluders over several frames rather than one long
    /// one. 0 builds nothing, which freezes the cache at what it has.
    uint32_t buildsPerFrame = 8;

    /// What the cache may hold, in bytes, before the least recently
    /// used entries are dropped.
    size_t memoryCap = size_t(64) << 20;

    bool operator==(const CoarseOccluderConfig &o) const
    {
        return enabled == o.enabled && level == o.level
                && minTriangles == o.minTriangles
                && buildsPerFrame == o.buildsPerFrame
                && memoryCap == o.memoryCap;
    }
    bool operator!=(const CoarseOccluderConfig &o) const
    {
        return !(*this == o);
    }
};

/// What the cache did, reported rather than inferred.
struct CoarseOccluderStats {
    uint32_t entries = 0;      ///< hulls held, usable or not
    uint32_t unusable = 0;     ///< meshes that could not be simplified
    uint32_t built = 0;        ///< hulls built this frame
    uint32_t evicted = 0;      ///< hulls dropped this frame
    uint32_t pending = 0;      ///< wanted this frame, not built yet
    size_t bytes = 0;
    float buildMs = 0.0f;      ///< wall clock of this frame's builds

    /// The exchange rate, over the hulls held: what they stand in for
    /// against what they cost to rasterize. The number that says
    /// whether this mechanism is worth its cache.
    uint64_t sourceTriangles = 0;
    uint64_t hullTriangles = 0;
};

/// The hulls, keyed by the identity of the mesh they stand for.
///
/// Single-threaded from the caller's side: `build()` fans out over
/// workers and joins before it returns, so nothing here is locked and
/// nothing outlives the call. The map is keyed by MeshData::cacheId,
/// with the generation held in the entry rather than in the key --
/// a ladder's rungs all fill one mesh object under one cache id
/// (SceneStreaming.md 7), so a bumped generation must *replace* its
/// hull, and a key that carried the generation would instead
/// accumulate one entry per rung the mesh has ever held.
class RendererExport CoarseOccluderCache
{
public:
    /// A configuration change that would change what a hull *is*
    /// throws away what is held: an entry built at another level is not
    /// the thing that was asked for, and keeping it would make the
    /// cache's contents depend on the order the knobs were turned.
    void configure(const CoarseOccluderConfig &c);
    const CoarseOccluderConfig &config() const { return conf; }

    /// The hull for \a mesh, or null when there is none -- because it
    /// has not been built, because the mesh is too small to be worth
    /// one, or because it cannot be simplified. Marks the entry used,
    /// which is what the eviction order reads.
    const CoarseOccluder *find(const MeshData &mesh);

    /// Build hulls for as many of \a wanted as the per-frame budget
    /// allows, on \a workers threads, in the order given -- so a caller
    /// that offers its occluders largest-on-screen first gets the
    /// largest built first. Meshes that already have a current hull
    /// cost a lookup and nothing else.
    void build(const std::vector<const MeshData *> &wanted, uint32_t workers);

    /// Forget everything. The next frame rebuilds what it asks for.
    void clear();

    const CoarseOccluderStats &stats() const { return framestats; }

private:
    struct Entry {
        CoarseOccluder hull;
        uint32_t generation = 0;
        /// False when simplifyMesh refused -- nothing merged at this
        /// level, or nothing survived. Held rather than dropped so the
        /// refusal is not re-derived on every frame of a static scene.
        bool usable = false;
        uint64_t used = 0;   ///< the tick this was last asked for
    };

    /// Whether a mesh is worth a hull at all, and its identity.
    /// False when there is nothing to stand in for.
    static bool wantsHull(const MeshData &mesh, uint32_t minTriangles,
                          uint64_t &key);

    void evictTo(size_t cap);
    /// Refresh the *held* half of the stats -- what is in the cache
    /// right now, as against what this frame did to it.
    void held();

    CoarseOccluderConfig conf;
    CoarseOccluderStats framestats;
    std::unordered_map<uint64_t, Entry> entries;
    /// Monotonic, bumped on every lookup rather than per frame: it
    /// orders evictions, and within one frame the order the occluders
    /// were offered in is exactly the ranking that decided which
    /// mattered most.
    uint64_t tick = 0;
    size_t heldBytes = 0;

    /// Scratch kept across frames so a frame allocates nothing.
    std::vector<const MeshData *> missing;
    std::vector<CoarseOccluder> results;
    std::vector<uint8_t> resultOk;
    std::unordered_set<uint64_t> seen;
};

/// Build one hull, without a cache. Exposed for the tests and for
/// anything that wants a hull of a mesh it does not own -- the cache is
/// the policy, this is the construction.
///
/// False when the mesh cannot be reduced at this level, which is not an
/// error: a mesh already coarser than the grid has no rung below it.
RendererExport bool buildOccluderHull(const MeshData &mesh, uint32_t level,
                                      CoarseOccluder &out);

}  // namespace Render

#endif  // RENDERER_OCCLUDER_MESH_H
