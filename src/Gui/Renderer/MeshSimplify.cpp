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

#include "MeshSimplify.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace Render;

namespace {

/// A cell of the clustering grid. Integer coordinates rather than a
/// hashed key, so the map orders deterministically and two runs over the
/// same mesh produce byte-identical output — which content addressing
/// depends on: a level whose bytes varied per run would have a different
/// key each time and never hit a cache (§9, invariant 2).
struct Cell {
    int32_t x = 0, y = 0, z = 0;
    bool operator<(const Cell &o) const
    {
        if (x != o.x)
            return x < o.x;
        if (y != o.y)
            return y < o.y;
        return z < o.z;
    }
};

/// Where a cell's representative sits: the average position of every
/// vertex that fell in it, whatever element the vertex belongs to. The
/// position is the one thing clustering must keep *global*: two
/// elements decimated apart stay sewn together exactly because the
/// coincident soup vertices along their boundary land in the same cell
/// on both sides and read back the same average.
struct PosCluster {
    double px = 0.0, py = 0.0, pz = 0.0;
    uint32_t count = 0;
    float rep[3] = {0.0f, 0.0f, 0.0f};
};

/// What one element's share of a cell collapses to: the attribute sums
/// that make its representative's normal and colour, and the output
/// index it is assigned. Attributes are per element where positions are
/// not — averaging normals across a face boundary is how a crease
/// shades round.
struct AttrCluster {
    double nx = 0.0, ny = 0.0, nz = 0.0;
    double cr = 0.0, cg = 0.0, cb = 0.0, ca = 0.0;
    uint32_t count = 0;
    int32_t index = -1;
};

/// One run of a source index array processed as a unit: an element from
/// the part table, or (part < 0) a stretch no entry covers.
struct Group {
    int part = -1;
    int begin = 0;   ///< in index units into the source array
    int end = 0;
};

/// The grid is anchored at \a anchor — the mesh's own minimum corner —
/// rather than at the world origin. A part small against its distance
/// from the origin would otherwise divide a huge coordinate by a tiny
/// cell: the quotient overflows int32 (undefined in the cast) long
/// before that, and loses float precision long before *that*, so
/// neighbouring vertices quantize arbitrarily. Relative to the anchor
/// the quotient is bounded by the level's cell count across the mesh.
/// The anchor derives from the vertices alone, so identical content
/// still produces identical bytes wherever it sits (§9, invariant 2) —
/// and a translated mesh produces the translated output.
inline Cell cellOf(const float *p, const float *anchor, float cellSize)
{
    Cell c;
    c.x = int32_t(std::floor((p[0] - anchor[0]) / cellSize));
    c.y = int32_t(std::floor((p[1] - anchor[1]) / cellSize));
    c.z = int32_t(std::floor((p[2] - anchor[2]) / cellSize));
    return c;
}

/// Cut an index array into the runs its part table names, appending the
/// stretches no entry covers as elementless trailing groups. False when
/// the table is malformed — misaligned, out of bounds, overlapping —
/// and the caller then treats the array as carrying no table at all
/// rather than guessing what the producer meant.
bool buildGroups(const std::vector<std::pair<int, int>> &parts, int total,
                 int stride, std::vector<Group> &groups)
{
    groups.clear();
    if (parts.empty())
        return false;
    std::vector<std::pair<int, int>> spans;
    spans.reserve(parts.size());
    for (size_t i = 0; i < parts.size(); ++i) {
        const int start = parts[i].first;
        const int count = parts[i].second;
        if (start < 0 || count < 0 || start % stride != 0
                || count % stride != 0 || start + count > total)
            return false;
        groups.push_back({int(i), start, start + count});
        if (count > 0)
            spans.emplace_back(start, start + count);
    }
    std::sort(spans.begin(), spans.end());
    for (size_t i = 1; i < spans.size(); ++i)
        if (spans[i].first < spans[i - 1].second)
            return false;
    // What no element claims is still geometry to keep — but it has no
    // table slot, so it clusters in trailing groups of its own.
    int cursor = 0;
    for (const auto &span : spans) {
        if (span.first > cursor)
            groups.push_back({-1, cursor, span.first});
        cursor = span.second;
    }
    if (cursor < total)
        groups.push_back({-1, cursor, total});
    return true;
}

}  // namespace

void SimplifiedMesh::fill(MeshData &mesh) const
{
    mesh.numVertices = numVertices();
    mesh.positions = positions.empty() ? nullptr : positions.data();
    mesh.normals = normals.empty() ? nullptr : normals.data();
    mesh.colors = colors.empty() ? nullptr : colors.data();
    mesh.triangleIndices =
        triangleIndices.empty() ? nullptr : triangleIndices.data();
    mesh.numTriangleIndices = int(triangleIndices.size());
    mesh.lineIndices = lineIndices.empty() ? nullptr : lineIndices.data();
    mesh.numLineIndices = int(lineIndices.size());
    mesh.pointIndices = pointIndices.empty() ? nullptr : pointIndices.data();
    mesh.numPointIndices = int(pointIndices.size());
    // The element tables survive decimation (see the header): entry i
    // still names the source's element i, so picking on this rung
    // resolves sub-elements exactly as the full mesh does. The
    // refinement subsets survive as re-emitted runs.
    mesh.triangleParts = triangleParts;
    mesh.lineParts = lineParts;
    mesh.pointParts = pointParts;
    mesh.nonFlatParts = nonFlatParts;
    mesh.solidParts = solidParts;
    mesh.hasSolid = hasSolid;
    // An empty filtered set reads as "no seams", which shows every line
    // -- the same direction non-seam-wins already errs in.
    mesh.noSeamLineIndices =
        noSeamLineIndices.empty() ? nullptr : noSeamLineIndices.data();
    mesh.numNoSeamLineIndices = int(noSeamLineIndices.size());
    mesh.texCoords = nullptr;
}

float Render::levelCellSize(const float *bbox, uint32_t level)
{
    const float dx = bbox[3] - bbox[0];
    const float dy = bbox[4] - bbox[1];
    const float dz = bbox[5] - bbox[2];
    const float diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(diagonal > 0.0f))
        return 0.0f;
    // An eighth of the diagonal at level 0, halving thereafter: the
    // coarsest level is a handful of cells across, so a mesh of any size
    // reduces to a few hundred vertices, and the count roughly
    // octuples per level after that. Capped where the shift would
    // overflow; a grid billions of cells across stopped merging anything
    // long before.
    return diagonal / float(8u << std::min(level, 20u));
}

bool Render::simplifyMesh(const MeshData &src, float cellSize,
                          SimplifiedMesh &out)
{
    out = SimplifiedMesh();
    if (!src.positions || src.numVertices <= 0 || !(cellSize > 0.0f))
        return false;
    if (src.numTriangleIndices <= 0)
        return false;

    // The grid's anchor: the mesh's own minimum corner (see cellOf).
    float anchor[3] = {src.positions[0], src.positions[1], src.positions[2]};
    for (int v = 1; v < src.numVertices; ++v) {
        const float *p = src.positions + size_t(v) * 3;
        anchor[0] = std::min(anchor[0], p[0]);
        anchor[1] = std::min(anchor[1], p[1]);
        anchor[2] = std::min(anchor[2], p[2]);
    }

    // Pass one, over every vertex whatever it belongs to: which cell it
    // falls in, and each cell's representative position. Averaging
    // rather than snapping to the cell centre keeps a flat face flat --
    // snapping visibly corrugates one, because neighbouring vertices on
    // the same plane land in different cells and each jumps to a
    // different centre.
    std::map<Cell, PosCluster> cells;
    std::vector<Cell> vertexCell(size_t(src.numVertices));
    for (int v = 0; v < src.numVertices; ++v) {
        const float *p = src.positions + size_t(v) * 3;
        const Cell cell = cellOf(p, anchor, cellSize);
        vertexCell[size_t(v)] = cell;
        PosCluster &cl = cells[cell];
        cl.px += p[0];
        cl.py += p[1];
        cl.pz += p[2];
        ++cl.count;
    }
    // Nothing can merge even before elements split the cells further:
    // the level would be the mesh, and a rung identical to the one above
    // it is worse than no rung -- it costs a fetch and a key to change
    // nothing.
    if (cells.size() >= size_t(src.numVertices))
        return false;
    for (auto &entry : cells) {
        PosCluster &cl = entry.second;
        const double inv = 1.0 / double(cl.count);
        cl.rep[0] = float(cl.px * inv);
        cl.rep[1] = float(cl.py * inv);
        cl.rep[2] = float(cl.pz * inv);
    }

    // Vertices are gathered per group -- per *element* -- so that no
    // output primitive ever spans two elements and the part tables can
    // carry over (see the header). The stamp makes "count each vertex
    // once per group" O(1) without clearing anything between groups.
    std::vector<int32_t> stamp(size_t(src.numVertices), -1);
    int32_t token = 0;
    int32_t next = 0;

    const auto emitVertices = [&](std::map<Cell, AttrCluster> &local) {
        for (auto &entry : local) {
            AttrCluster &cl = entry.second;
            cl.index = next++;
            const float *rep = cells.find(entry.first)->second.rep;
            out.positions.insert(out.positions.end(),
                                 {rep[0], rep[1], rep[2]});
            if (src.normals) {
                double nx = cl.nx, ny = cl.ny, nz = cl.nz;
                const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-12) {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }
                else {
                    // The group's normals cancelled -- a fold welded
                    // onto itself. Any unit vector is as wrong as any
                    // other; pick a fixed one so the output stays
                    // deterministic.
                    nx = 0.0;
                    ny = 0.0;
                    nz = 1.0;
                }
                out.normals.insert(out.normals.end(),
                                   {float(nx), float(ny), float(nz)});
            }
            if (src.colors) {
                const double inv = cl.count ? 1.0 / double(cl.count) : 0.0;
                out.colors.insert(out.colors.end(),
                                  {uint8_t(cl.cr * inv + 0.5),
                                   uint8_t(cl.cg * inv + 0.5),
                                   uint8_t(cl.cb * inv + 0.5),
                                   uint8_t(cl.ca * inv + 0.5)});
            }
        }
    };

    const auto gather = [&](const int32_t *indices, const Group &g,
                            std::map<Cell, AttrCluster> &local) {
        ++token;
        for (int i = g.begin; i < g.end; ++i) {
            const int32_t v = indices[i];
            if (v < 0 || v >= src.numVertices)
                continue;
            if (stamp[size_t(v)] == token)
                continue;
            stamp[size_t(v)] = token;
            AttrCluster &cl = local[vertexCell[size_t(v)]];
            if (src.normals) {
                const float *n = src.normals + size_t(v) * 3;
                cl.nx += n[0];
                cl.ny += n[1];
                cl.nz += n[2];
            }
            if (src.colors) {
                const uint8_t *c = src.colors + size_t(v) * 4;
                cl.cr += c[0];
                cl.cg += c[1];
                cl.cb += c[2];
                cl.ca += c[3];
            }
            ++cl.count;
        }
    };

    // Triangles, one group per face part. A malformed table degrades to
    // one elementless group over everything -- the pre-table behaviour.
    std::vector<Group> groups;
    const bool triTable =
        buildGroups(src.triangleParts, src.numTriangleIndices, 3, groups);
    if (!triTable)
        groups.assign(1, {-1, 0, src.numTriangleIndices});
    else
        out.triangleParts.assign(src.triangleParts.size(), {0, 0});
    // Which source triangle each output triangle came from, for
    // re-emitting the flag subsets (nonFlatParts, solidParts) as runs
    // over the survivors.
    std::vector<int32_t> outSrcTri;
    for (const Group &g : groups) {
        std::map<Cell, AttrCluster> local;
        gather(src.triangleIndices, g, local);
        emitVertices(local);
        const int rangeStart = int(out.triangleIndices.size());
        for (int i = g.begin; i + 2 < g.end; i += 3) {
            const int32_t a = src.triangleIndices[i];
            const int32_t b = src.triangleIndices[i + 1];
            const int32_t c = src.triangleIndices[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= src.numVertices
                    || b >= src.numVertices || c >= src.numVertices)
                continue;
            const int32_t ia = local.find(vertexCell[size_t(a)])->second.index;
            const int32_t ib = local.find(vertexCell[size_t(b)])->second.index;
            const int32_t ic = local.find(vertexCell[size_t(c)])->second.index;
            // A triangle whose corners no longer span three distinct
            // representatives has no area left to draw and would be a
            // degenerate the backend has to cull every frame.
            if (ia == ib || ib == ic || ia == ic)
                continue;
            out.triangleIndices.insert(out.triangleIndices.end(),
                                       {ia, ib, ic});
            outSrcTri.push_back(i / 3);
        }
        if (g.part >= 0)
            out.triangleParts[size_t(g.part)] = {
                rangeStart, int(out.triangleIndices.size()) - rangeStart};
    }
    if (out.triangleIndices.empty())
        return false;

    // The triangle-flag subsets. Flat-versus-curved and solidness are
    // properties of the *source* faces and survive decimation, so each
    // subset is carried by marking the source triangles it covers and
    // re-emitting maximal runs over the output -- which assumes nothing
    // about how the source ranges were laid out, and yields nothing
    // where a flagged region collapsed entirely.
    const auto markTriangles = [&](const std::vector<std::pair<int, int>>
                                       &ranges,
                                   std::vector<uint8_t> &flags) {
        bool any = false;
        flags.assign(size_t(src.numTriangleIndices) / 3, 0);
        for (const auto &range : ranges) {
            if (range.first < 0 || range.second < 0 || range.first % 3 != 0
                    || range.second % 3 != 0
                    || range.first + range.second > src.numTriangleIndices)
                continue;
            for (int t = range.first / 3;
                 t < (range.first + range.second) / 3; ++t) {
                flags[size_t(t)] = 1;
                any = true;
            }
        }
        return any;
    };
    const auto emitRuns = [&](const std::vector<uint8_t> &flags,
                              std::vector<std::pair<int, int>> &runs) {
        int runStart = -1;
        for (size_t o = 0; o <= outSrcTri.size(); ++o) {
            const bool flagged =
                o < outSrcTri.size() && flags[size_t(outSrcTri[o])] != 0;
            if (flagged && runStart < 0)
                runStart = int(o) * 3;
            else if (!flagged && runStart >= 0) {
                runs.emplace_back(runStart, int(o) * 3 - runStart);
                runStart = -1;
            }
        }
    };
    std::vector<uint8_t> flags;
    if (!src.nonFlatParts.empty() && markTriangles(src.nonFlatParts, flags))
        emitRuns(flags, out.nonFlatParts);
    if (src.hasSolid == 2) {
        // The whole triangle set is solid; there is no subset to remap.
        out.hasSolid = 2;
    }
    else if (src.hasSolid == 1 && markTriangles(src.solidParts, flags)) {
        emitRuns(flags, out.solidParts);
        out.hasSolid = out.solidParts.empty() ? 0 : 1;
    }

    // The edges, one group per edge part, deduplicated within their
    // element: clustering maps many original edges onto the same pair,
    // and a coarse rung that drew each of them would spend more on lines
    // than on the surface it is standing in for.
    if (src.lineIndices && src.numLineIndices > 0) {
        // Which source edges the seam filter kept, so the filter can be
        // carried through the weld. A merged edge may fold a seam edge
        // and a non-seam edge together, and there is no faithful answer
        // for it -- so non-seam wins: the merge stays visible under
        // hideSeam, erring toward showing a line rather than hiding one.
        std::set<std::pair<int32_t, int32_t>> noSeamSrc;
        const bool haveSeams =
            src.noSeamLineIndices && src.numNoSeamLineIndices > 0;
        if (haveSeams) {
            for (int i = 0; i + 1 < src.numNoSeamLineIndices; i += 2) {
                int32_t a = src.noSeamLineIndices[i];
                int32_t b = src.noSeamLineIndices[i + 1];
                if (a > b)
                    std::swap(a, b);
                noSeamSrc.emplace(a, b);
            }
        }
        const bool lineTable =
            buildGroups(src.lineParts, src.numLineIndices, 2, groups);
        if (!lineTable)
            groups.assign(1, {-1, 0, src.numLineIndices});
        else
            out.lineParts.assign(src.lineParts.size(), {0, 0});
        for (const Group &g : groups) {
            std::map<Cell, AttrCluster> local;
            gather(src.lineIndices, g, local);
            emitVertices(local);
            // Ordered by output pair, so emission stays deterministic;
            // the value ORs non-seam-ness across the merged edges.
            std::map<std::pair<int32_t, int32_t>, bool> edges;
            for (int i = g.begin; i + 1 < g.end; i += 2) {
                const int32_t a = src.lineIndices[i];
                const int32_t b = src.lineIndices[i + 1];
                if (a < 0 || b < 0 || a >= src.numVertices
                        || b >= src.numVertices)
                    continue;
                int32_t ia = local.find(vertexCell[size_t(a)])->second.index;
                int32_t ib = local.find(vertexCell[size_t(b)])->second.index;
                if (ia == ib)
                    continue;
                if (ia > ib)
                    std::swap(ia, ib);
                bool nonSeam = false;
                if (haveSeams) {
                    std::pair<int32_t, int32_t> key(std::min(a, b),
                                                    std::max(a, b));
                    nonSeam = noSeamSrc.count(key) != 0;
                }
                edges[{ia, ib}] |= nonSeam;
            }
            const int rangeStart = int(out.lineIndices.size());
            for (const auto &e : edges) {
                out.lineIndices.insert(out.lineIndices.end(),
                                       {e.first.first, e.first.second});
                if (e.second)
                    out.noSeamLineIndices.insert(
                        out.noSeamLineIndices.end(),
                        {e.first.first, e.first.second});
            }
            if (g.part >= 0)
                out.lineParts[size_t(g.part)] = {
                    rangeStart, int(out.lineIndices.size()) - rangeStart};
        }
    }

    // The points, one group per vertex part: each element keeps one
    // point per cell its members fell in -- for the usual one-point
    // element, its position moved to the representative.
    if (src.pointIndices && src.numPointIndices > 0) {
        const bool pointTable =
            buildGroups(src.pointParts, src.numPointIndices, 1, groups);
        if (!pointTable)
            groups.assign(1, {-1, 0, src.numPointIndices});
        else
            out.pointParts.assign(src.pointParts.size(), {0, 0});
        for (const Group &g : groups) {
            std::map<Cell, AttrCluster> local;
            gather(src.pointIndices, g, local);
            emitVertices(local);
            const int rangeStart = int(out.pointIndices.size());
            for (const auto &entry : local)
                out.pointIndices.push_back(entry.second.index);
            if (g.part >= 0)
                out.pointParts[size_t(g.part)] = {
                    rangeStart, int(out.pointIndices.size()) - rangeStart};
        }
    }

    // Splitting cells per element duplicates representatives along the
    // boundaries, so the honest refusal check is on what actually came
    // out: a level no smaller than its source is not a rung.
    if (out.numVertices() >= src.numVertices) {
        out = SimplifiedMesh();
        return false;
    }
    return true;
}
