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
                                 SimplifiedMesh &out);

}  // namespace Render

#endif  // RENDERER_MESH_SIMPLIFY_H
