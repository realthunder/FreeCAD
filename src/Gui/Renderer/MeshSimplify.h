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

#ifndef RENDERER_MESH_SIMPLIFY_H
#define RENDERER_MESH_SIMPLIFY_H

/// Where a middle rung of docs/SceneStreaming.md §7's ladder comes from.
///
/// §7 contemplates two generators: re-tessellating the shape at a coarser
/// deviation, or decimating the mesh that already exists. This is the
/// second, and it is deliberately the one built first — it needs no OCCT,
/// no Part, and no shape at all, only the `MeshData` the renderer is
/// already holding. That makes it the same code on the desktop and in the
/// publish path, and it means a level can be produced anywhere a mesh is,
/// which is what generating on demand requires.
///
/// It is not the better generator. Re-tessellation knows the surface and
/// can put its vertices where the curvature is; decimation only knows the
/// triangles it was handed. But the two are interchangeable behind
/// RungProvider::generate, so replacing this one later costs nothing in
/// the format or the consumer — which was the point of the seam.

#include <cstdint>
#include <vector>

#include "Renderer.h"

namespace Render {

/// A generated rung: the arrays a MeshData would point into, owned.
///
/// Deliberately not a MeshData. A MeshData is a view onto storage
/// somebody else owns, and a generated level owns everything about
/// itself; `fill()` hands out the view when one is wanted.
struct RendererExport SimplifiedMesh {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint8_t> colors;
    std::vector<int32_t> triangleIndices;
    std::vector<int32_t> lineIndices;
    std::vector<int32_t> pointIndices;
    /// lineIndices with seam edges filtered out, under non-seam-wins
    /// (see above). Empty either when the source carried no filter or
    /// when nothing non-seam survived; fill() maps both to "no seams".
    std::vector<int32_t> noSeamLineIndices;

    /// The element tables, preserved index for index from the source
    /// mesh (empty when the source carried none): entry i of the output
    /// names the same element as entry i of the input, with an empty
    /// range where the element collapsed entirely.
    std::vector<std::pair<int, int>> triangleParts;
    std::vector<std::pair<int, int>> lineParts;
    std::vector<std::pair<int, int>> pointParts;

    /// The refinement subsets, re-emitted as maximal runs over the
    /// surviving triangles (see above).
    std::vector<std::pair<int, int>> nonFlatParts;
    std::vector<std::pair<int, int>> solidParts;
    int hasSolid = 0;

    /// Point the non-owning fields of \a mesh at this storage. The
    /// result is valid only while this object is.
    void fill(MeshData &mesh) const;

    int numVertices() const { return int(positions.size() / 3); }
};

/// Where the clustering grid sits, and what may weld to what.
///
/// Both defaults describe the single-object rung this file was written
/// for; both are changed by the far-field proxy of
/// docs/FarFieldProxies.md §5, and the reasons are worth stating
/// because they are opposite in kind.
struct SimplifyOptions {
    /// The grid's origin. Null anchors it at the mesh's own minimum
    /// corner, which is what a level of one object wants: identical
    /// content then produces identical bytes wherever it sits.
    ///
    /// A proxy wants the opposite. Its grid is the *level's* grid —
    /// one origin shared by every node, halved per level so the levels
    /// nest (§3.2) — because a proxy is built from its children's
    /// proxies (§7.1), and a grid that moved with each node's contents
    /// would re-quantize everything it inherited at every level.
    const float *anchor = nullptr;

    /// Let one output vertex serve primitives from more than one part.
    ///
    /// Off, a cell is split per element and no output primitive ever
    /// spans two of them — which is what keeps a crease a crease, since
    /// normals then average within one face rather than round its
    /// boundary. The cost is a duplicated representative in every cell
    /// two elements share.
    ///
    /// On, positions *and* attributes cluster once over the whole mesh.
    /// The part tables still carry over — a triangle's three corners
    /// come from one part's index triple however the vertices are
    /// shared, so the runs stay attributable — and what is given up is
    /// the crease.
    ///
    /// A proxy wants it because its parts are *different objects*, not
    /// faces of one. Two objects that abut were never a modelled
    /// crease, so there is nothing to protect; and the duplication is
    /// then per member rather than per face, which over the hundreds of
    /// members of one proxy is most of what the output holds.
    ///
    /// ⚠️ What it does **not** do is save a member smaller than a cell.
    /// Such a member has all its vertices in one cell, so all its
    /// triangles are degenerate and are dropped, welded or not — the
    /// deletion is clustering's, not the grouping's. How much of an
    /// assembly that removes is what ProxyMeshStats::proxyArea is for.
    bool weldAcrossParts = false;
};

/// What the decimation actually committed, for the caller that has to
/// decide whether the rung is good enough to draw.
///
/// The displacement is measured per source vertex against the
/// representative it collapsed onto, which is not merely a proxy for
/// the surface deviation but a bound on it: a triangle's three corners
/// each move by at most `maxDisplacement`, and every point of the
/// triangle is an affine combination of them, so no point of the
/// decimated surface is further than that from the point it came from.
/// (The one thing it does not bound is a triangle dropped entirely for
/// having collapsed — that hole is bounded by the cell, which is the
/// same size.)
struct SimplifyStats {
    float maxDisplacement = 0.0f;  ///< world units, over all source vertices
    float rmsDisplacement = 0.0f;
    uint32_t clusters = 0;         ///< occupied grid cells
};

/// The edge length of the clustering grid for \a level over a mesh of
/// this diagonal, coarsest first.
///
/// Halving per level, from an eighth of the diagonal: level 0 is a mesh
/// reduced to at most a few hundred vertices whatever it started as,
/// which is the rung just above the box, and each step doubles the
/// resolution until the exact mesh is cheaper than another level of it.
RendererExport float levelCellSize(const float *bbox, uint32_t level);

/// Collapse \a src onto a grid of \a cellSize and write the result to
/// \a out. False when there is nothing to simplify — no positions, no
/// triangles, or a cell size that would not merge anything.
///
/// **Vertex clustering, not edge collapse.** It is the cruder of the two
/// classic answers and it is chosen for what it does not need: no
/// topology, no manifold assumption, no priority queue, and a single
/// pass over the triangles. CAD tessellations arrive as unmerged
/// triangle soup with seams and duplicated vertices, which is the input
/// edge-collapse handles worst and clustering does not care about at
/// all. A representative is the *average* of the vertices in its cell
/// rather than the cell centre, so a flat face stays where it was
/// instead of stepping onto the grid.
///
/// **Element identity survives decimation.** Clustering never welds
/// across element boundaries: vertices are clustered per element (per
/// face for triangles, per edge for lines, per vertex for points), so
/// every output primitive belongs to exactly one source element and the
/// part tables carry over index for index — an element that collapses
/// entirely keeps its slot as an empty range, so table positions keep
/// their meaning and a pick can never name the wrong element, only no
/// element (§9 invariant 6). What keeps the elements sewn together
/// regardless is that a representative's *position* comes from a grid
/// the whole mesh shares: the coincident soup vertices two adjacent
/// faces both carry land in the same cell and take the same average, so
/// decimated faces still meet exactly where their tessellations met.
/// A bonus over global welding: normals average within one element
/// only, so a crease stays a crease instead of shading round.
///
/// The refinements below the table carry over too, each by the rule its
/// meaning allows. Flat-versus-curved and solidness are properties of
/// the *source* faces, invariant under decimation, so nonFlatParts and
/// solidParts are carried by marking source triangles and re-emitting
/// maximal runs over the survivors — no assumption about how the source
/// ranges were laid out. Seam-ness is per source edge, and welding can
/// merge a seam edge with a non-seam one; the merged edge has no
/// faithful answer, so **non-seam wins**: an output edge is kept in the
/// no-seam set if any source edge it merges was non-seam, which errs
/// toward showing a line rather than hiding one. One caution stands:
/// section capping assumes closed geometry, and clustering does not
/// preserve watertightness, so a cap cut through a decimated solid can
/// be visibly rough — judged worth carrying so the rung caps at all,
/// and the full mesh restores exactness when it lands.
RendererExport bool simplifyMesh(const MeshData &src, float cellSize,
                                 SimplifiedMesh &out,
                                 const SimplifyOptions &opts = SimplifyOptions(),
                                 SimplifyStats *stats = nullptr);

// ---------------------------------------------------------------------
// Far-field proxies (docs/FarFieldProxies.md §5, phase 2)
// ---------------------------------------------------------------------

/// One instance going into a proxy: a mesh, where it sits, and what it
/// stands for.
///
/// The mesh may equally be an object's tessellation or a child node's
/// proxy — which is what makes the bottom-up generation of §7.1 fall
/// out of the same call rather than needing a second one. A child's
/// proxy arrives with an identity transform and its own part table;
/// a source object arrives with its placement and one part.
struct ProxyMember {
    const MeshData *mesh = nullptr;
    /// GL-layout (column-major) 4x4 world transform, null = identity.
    const float *model = nullptr;
    /// The draw's range into the mesh's triangle index buffer, as
    /// DrawCall carries it: count 0 means the whole buffer. A draw of a
    /// single face contributes that face alone.
    int indexStart = 0;
    int indexCount = 0;
    /// What the proxy's part table will name this member as (§6). Not
    /// interpreted here — it is handed back per part slot so a pick
    /// into the proxy can be resolved to whatever the caller keys
    /// objects by.
    uint64_t objectKey = 0;
};

/// The grid a proxy is decimated on. Both fields come from the level,
/// never from the member set: see SimplifyOptions::anchor.
struct ProxyMeshParams {
    float anchor[3] {0.0f, 0.0f, 0.0f};
    float cellSize = 0.0f;
};

/// What one proxy cost and what it commits, for the measurement §11.1b
/// asks for and for the gate of §7.1.
struct ProxyMeshStats {
    uint32_t members = 0;
    /// Distinct mesh *contents* among the members, counted by cache id
    /// rather than by MeshData address — two occurrences of one shape
    /// are two views onto one cache entry, and they are precisely what
    /// the gate is looking for. Five
    /// hundred instances of one screw share one vertex buffer today and
    /// a baked proxy holds five hundred copies, so a proxy is only
    /// worth making where its decimated triangle count beats what
    /// instancing already achieves. `uniqueTriangles` is what
    /// instancing pays; `sourceTriangles` is what the merge would bake
    /// before decimation.
    uint32_t distinctMeshes = 0;
    uint32_t sourceVertices = 0;
    uint32_t sourceTriangles = 0;
    uint32_t uniqueTriangles = 0;
    uint32_t mergedVertices = 0;
    uint32_t proxyVertices = 0;
    uint32_t proxyTriangles = 0;
    /// Diagonal of the merged content's bounds, world units — the
    /// *extent* the error of §11.1b is a fraction of.
    float extent = 0.0f;
    float maxError = 0.0f;   ///< SimplifyStats::maxDisplacement
    float rmsError = 0.0f;

    /// How much of the surface is still there, and how much of the
    /// assembly went missing entirely.
    ///
    /// The displacement error above says how far what survived had to
    /// move; it is silent about what did not survive, and clustering
    /// deletes rather than moves whenever a member is smaller than a
    /// cell — every one of its triangles has its three corners in one
    /// cell and is dropped as degenerate. A cloud of small parts can
    /// therefore vanish as a body while every reported error stays
    /// inside the tolerance, so the area retained and the count of
    /// members that came back empty are reported beside it. They are
    /// the numbers that say whether a decimated mesh is the right
    /// representation for a far field at all, or whether the small
    /// stuff needs standing in for rather than simplifying.
    double sourceArea = 0.0;
    double proxyArea = 0.0;
    uint32_t collapsedMembers = 0;

    double mergeMs = 0.0;
    double simplifyMs = 0.0;
};

/// Merge \a members into one world-space mesh, one triangle part per
/// member — the first half of buildProxyMesh, separated because a node
/// generating more than one rung merges once and decimates repeatedly,
/// and because a parent's merge takes its children's proxies as members
/// (§7.1). Fills the merge half of \a stats.
RendererExport bool mergeProxyMembers(const std::vector<ProxyMember> &members,
                                      SimplifiedMesh &merged,
                                      std::vector<uint64_t> *partKeys = nullptr,
                                      ProxyMeshStats *stats = nullptr);

/// Decimate an already merged proxy mesh onto \a params' grid, filling
/// the decimation half of \a stats and leaving the merge half as
/// mergeProxyMembers left it.
RendererExport bool decimateProxyMesh(const SimplifiedMesh &merged,
                                      const ProxyMeshParams &params,
                                      SimplifiedMesh &out,
                                      ProxyMeshStats *stats = nullptr);

/// Merge \a members into one world-space mesh, one triangle part per
/// member, and decimate it on \a params' grid.
///
/// The part table is the whole of §6's identity story and it costs
/// nothing to keep: `simplifyMesh` already preserves element tables
/// index for index, so recording which member each input run came from
/// *is* the part table, and \a partKeys returns the objectKey sitting
/// in each slot. A slot may come back empty, which means that member
/// collapsed entirely at this grid — a pick can then name no object,
/// never the wrong one.
///
/// Triangles only. A member's lines and points are dropped: they are
/// separate draws under separate materials, so they form their own
/// buckets and stay exact until there is a measurement saying a line
/// proxy is worth having.
///
/// False when there is nothing to make: no triangles among the members,
/// or a grid that merges nothing. \a stats is filled either way, so a
/// refusal is still measurable.
RendererExport bool buildProxyMesh(const std::vector<ProxyMember> &members,
                                   const ProxyMeshParams &params,
                                   SimplifiedMesh &out,
                                   std::vector<uint64_t> *partKeys = nullptr,
                                   ProxyMeshStats *stats = nullptr);

}  // namespace Render

#endif  // RENDERER_MESH_SIMPLIFY_H
