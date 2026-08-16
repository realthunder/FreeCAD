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
#include <chrono>
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
    double tu = 0.0, tv = 0.0;
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
    mesh.texCoords = texCoords.empty() ? nullptr : texCoords.data();
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
                          SimplifiedMesh &out, const SimplifyOptions &opts,
                          SimplifyStats *stats)
{
    out = SimplifiedMesh();
    if (stats)
        *stats = SimplifyStats();
    if (!src.positions || src.numVertices <= 0 || !(cellSize > 0.0f))
        return false;
    if (src.numTriangleIndices <= 0)
        return false;

    // The grid's anchor: the level's origin when one was given, else the
    // mesh's own minimum corner (see cellOf and SimplifyOptions).
    float anchor[3] = {src.positions[0], src.positions[1], src.positions[2]};
    if (opts.anchor) {
        anchor[0] = opts.anchor[0];
        anchor[1] = opts.anchor[1];
        anchor[2] = opts.anchor[2];
    }
    else {
        for (int v = 1; v < src.numVertices; ++v) {
            const float *p = src.positions + size_t(v) * 3;
            anchor[0] = std::min(anchor[0], p[0]);
            anchor[1] = std::min(anchor[1], p[1]);
            anchor[2] = std::min(anchor[2], p[2]);
        }
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
    if (stats)
        stats->clusters = uint32_t(cells.size());
    // Nothing can merge even before elements split the cells further:
    // the level would be the mesh, and a rung identical to the one above
    // it is worse than no rung -- it costs a fetch and a key to change
    // nothing. (Nor is there any error to report: a vertex alone in its
    // cell is its own representative, so the refusal leaves the
    // displacement at zero, which is the truth.)
    if (cells.size() >= size_t(src.numVertices))
        return false;
    for (auto &entry : cells) {
        PosCluster &cl = entry.second;
        const double inv = 1.0 / double(cl.count);
        cl.rep[0] = float(cl.px * inv);
        cl.rep[1] = float(cl.py * inv);
        cl.rep[2] = float(cl.pz * inv);
    }

    // What the collapse costs, measured where it happens: how far each
    // vertex had to travel to reach its representative. Bounds the
    // surface deviation, which is what a screen-space tolerance is
    // really asking about (see SimplifyStats).
    if (stats) {
        double sumsq = 0.0;
        float worst = 0.0f;
        for (int v = 0; v < src.numVertices; ++v) {
            const float *p = src.positions + size_t(v) * 3;
            const float *rep = cells.find(vertexCell[size_t(v)])->second.rep;
            const float dx = p[0] - rep[0];
            const float dy = p[1] - rep[1];
            const float dz = p[2] - rep[2];
            const double d2 = double(dx) * dx + double(dy) * dy
                + double(dz) * dz;
            worst = std::max(worst, float(std::sqrt(d2)));
            sumsq += d2;
        }
        stats->maxDisplacement = worst;
        stats->rmsDisplacement =
            float(std::sqrt(sumsq / double(src.numVertices)));
    }

    // Vertices are gathered per group -- per *element* -- so that no
    // output primitive ever spans two elements and the part tables can
    // carry over (see the header). The stamp makes "count each vertex
    // once per group" O(1) without clearing anything between groups.
    std::vector<int32_t> stamp(size_t(src.numVertices), -1);
    int32_t token = 0;
    int32_t next = 0;

    // What the caller will actually read. An occluder hull wants the
    // surface and nothing else (SimplifyOptions::trianglesOnly), and the
    // cheapest way to honor that is to treat the source as if it carried
    // no attributes at all -- the clustering, the representatives and
    // the triangles are then computed by exactly the same code, so the
    // positions it emits are the ones the full path would have emitted.
    const bool wantNormals = src.normals && !opts.trianglesOnly;
    const bool wantColors = src.colors && !opts.trianglesOnly;
    const bool wantTexCoords = src.texCoords && !opts.trianglesOnly;

    const auto emitVertices = [&](std::map<Cell, AttrCluster> &local) {
        for (auto &entry : local) {
            AttrCluster &cl = entry.second;
            cl.index = next++;
            const float *rep = cells.find(entry.first)->second.rep;
            out.positions.insert(out.positions.end(),
                                 {rep[0], rep[1], rep[2]});
            if (wantNormals) {
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
            if (wantColors) {
                const double inv = cl.count ? 1.0 / double(cl.count) : 0.0;
                out.colors.insert(out.colors.end(),
                                  {uint8_t(cl.cr * inv + 0.5),
                                   uint8_t(cl.cg * inv + 0.5),
                                   uint8_t(cl.cb * inv + 0.5),
                                   uint8_t(cl.ca * inv + 0.5)});
            }
            if (wantTexCoords) {
                const double inv = cl.count ? 1.0 / double(cl.count) : 0.0;
                out.texCoords.insert(out.texCoords.end(),
                                     {float(cl.tu * inv),
                                      float(cl.tv * inv)});
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
            if (wantNormals) {
                const float *n = src.normals + size_t(v) * 3;
                cl.nx += n[0];
                cl.ny += n[1];
                cl.nz += n[2];
            }
            if (wantColors) {
                const uint8_t *c = src.colors + size_t(v) * 4;
                cl.cr += c[0];
                cl.cg += c[1];
                cl.cb += c[2];
                cl.ca += c[3];
            }
            if (wantTexCoords) {
                const float *t = src.texCoords + size_t(v) * 2;
                cl.tu += t[0];
                cl.tv += t[1];
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
    // Welded, one pool over the whole index array -- which is the union
    // of the groups, gaps included -- so that a part smaller than a cell
    // shares its neighbours' representatives instead of collapsing to
    // one of its own and vanishing (SimplifyOptions::weldAcrossParts).
    std::map<Cell, AttrCluster> welded;
    if (opts.weldAcrossParts) {
        gather(src.triangleIndices, Group {-1, 0, src.numTriangleIndices},
               welded);
        emitVertices(welded);
    }
    // Which source triangle each output triangle came from, for
    // re-emitting the flag subsets (nonFlatParts, solidParts) as runs
    // over the survivors.
    std::vector<int32_t> outSrcTri;
    for (const Group &g : groups) {
        std::map<Cell, AttrCluster> local;
        std::map<Cell, AttrCluster> &clusters =
            opts.weldAcrossParts ? welded : local;
        if (!opts.weldAcrossParts) {
            gather(src.triangleIndices, g, local);
            emitVertices(local);
        }
        const int rangeStart = int(out.triangleIndices.size());
        for (int i = g.begin; i + 2 < g.end; i += 3) {
            const int32_t a = src.triangleIndices[i];
            const int32_t b = src.triangleIndices[i + 1];
            const int32_t c = src.triangleIndices[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= src.numVertices
                    || b >= src.numVertices || c >= src.numVertices)
                continue;
            const int32_t ia =
                clusters.find(vertexCell[size_t(a)])->second.index;
            const int32_t ib =
                clusters.find(vertexCell[size_t(b)])->second.index;
            const int32_t ic =
                clusters.find(vertexCell[size_t(c)])->second.index;
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
    if (!opts.trianglesOnly) {
        if (!src.nonFlatParts.empty()
            && markTriangles(src.nonFlatParts, flags))
            emitRuns(flags, out.nonFlatParts);
        if (src.hasSolid == 2) {
            // The whole triangle set is solid; there is no subset to
            // remap.
            out.hasSolid = 2;
        }
        else if (src.hasSolid == 1 && markTriangles(src.solidParts, flags)) {
            emitRuns(flags, out.solidParts);
            out.hasSolid = out.solidParts.empty() ? 0 : 1;
        }
    }

    // The edges, one group per edge part, deduplicated within their
    // element: clustering maps many original edges onto the same pair,
    // and a coarse rung that drew each of them would spend more on lines
    // than on the surface it is standing in for.
    if (src.lineIndices && src.numLineIndices > 0 && !opts.trianglesOnly) {
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
        std::map<Cell, AttrCluster> weldedLines;
        if (opts.weldAcrossParts) {
            gather(src.lineIndices, Group {-1, 0, src.numLineIndices},
                   weldedLines);
            emitVertices(weldedLines);
        }
        for (const Group &g : groups) {
            std::map<Cell, AttrCluster> local;
            std::map<Cell, AttrCluster> &clusters =
                opts.weldAcrossParts ? weldedLines : local;
            if (!opts.weldAcrossParts) {
                gather(src.lineIndices, g, local);
                emitVertices(local);
            }
            // Ordered by output pair, so emission stays deterministic;
            // the value ORs non-seam-ness across the merged edges.
            std::map<std::pair<int32_t, int32_t>, bool> edges;
            for (int i = g.begin; i + 1 < g.end; i += 2) {
                const int32_t a = src.lineIndices[i];
                const int32_t b = src.lineIndices[i + 1];
                if (a < 0 || b < 0 || a >= src.numVertices
                        || b >= src.numVertices)
                    continue;
                int32_t ia =
                    clusters.find(vertexCell[size_t(a)])->second.index;
                int32_t ib =
                    clusters.find(vertexCell[size_t(b)])->second.index;
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
    if (src.pointIndices && src.numPointIndices > 0 && !opts.trianglesOnly) {
        const bool pointTable =
            buildGroups(src.pointParts, src.numPointIndices, 1, groups);
        if (!pointTable)
            groups.assign(1, {-1, 0, src.numPointIndices});
        else
            out.pointParts.assign(src.pointParts.size(), {0, 0});
        std::map<Cell, AttrCluster> weldedPoints;
        if (opts.weldAcrossParts) {
            gather(src.pointIndices, Group {-1, 0, src.numPointIndices},
                   weldedPoints);
            emitVertices(weldedPoints);
        }
        for (const Group &g : groups) {
            std::map<Cell, AttrCluster> local;
            const int rangeStart = int(out.pointIndices.size());
            if (opts.weldAcrossParts) {
                // Which of the shared representatives this part reaches.
                // Ordered rather than in index order, so that a part
                // emits the same bytes however its points were listed.
                std::set<Cell> touched;
                for (int i = g.begin; i < g.end; ++i) {
                    const int32_t v = src.pointIndices[i];
                    if (v < 0 || v >= src.numVertices)
                        continue;
                    touched.insert(vertexCell[size_t(v)]);
                }
                for (const Cell &cell : touched)
                    out.pointIndices.push_back(
                        weldedPoints.find(cell)->second.index);
            }
            else {
                gather(src.pointIndices, g, local);
                emitVertices(local);
                for (const auto &entry : local)
                    out.pointIndices.push_back(entry.second.index);
            }
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

// ---------------------------------------------------------------------
// Far-field proxies
// ---------------------------------------------------------------------

namespace {

/// GL layout is column-major, so element (row r, column c) is m[c * 4 + r]
/// and a point transforms as M * p.
inline void transformPoint(const float *m, const float *p, float *out)
{
    out[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    out[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
}

/// How a direction transforms under \a m: by the cofactor matrix of its
/// upper 3x3, which is the inverse-transpose up to the determinant.
///
/// Not the matrix itself. Under a non-uniform scale -- an App::Link
/// scaling one axis is the everyday case -- the matrix tilts a normal
/// off its surface, and a proxy's shading is the one thing about it a
/// viewer sees directly. The determinant's *sign* is kept because it is
/// two facts at once: a mirrored placement both flips the winding and
/// reverses which side the normal should point.
struct NormalXform {
    float cof[9] {1, 0, 0, 0, 1, 0, 0, 0, 1};  ///< row-major
    bool mirrored = false;

    explicit NormalXform(const float *m)
    {
        if (!m)
            return;
        const float a[3][3] = {{m[0], m[4], m[8]},
                               {m[1], m[5], m[9]},
                               {m[2], m[6], m[10]}};
        cof[0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
        cof[1] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
        cof[2] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
        cof[3] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
        cof[4] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
        cof[5] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
        cof[6] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
        cof[7] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
        cof[8] = a[0][0] * a[1][1] - a[0][1] * a[1][0];
        const float det =
            a[0][0] * cof[0] + a[0][1] * cof[1] + a[0][2] * cof[2];
        mirrored = det < 0.0f;
    }

    void apply(const float *n, float *out) const
    {
        float x = cof[0] * n[0] + cof[3] * n[1] + cof[6] * n[2];
        float y = cof[1] * n[0] + cof[4] * n[1] + cof[7] * n[2];
        float z = cof[2] * n[0] + cof[5] * n[1] + cof[8] * n[2];
        if (mirrored) {
            x = -x;
            y = -y;
            z = -z;
        }
        const float len = std::sqrt(x * x + y * y + z * z);
        if (len > 1e-20f) {
            x /= len;
            y /= len;
            z /= len;
        }
        out[0] = x;
        out[1] = y;
        out[2] = z;
    }
};

/// The member's triangle index range, as DrawCall states it: a count of
/// zero means the whole buffer rather than an empty draw.
inline void memberRange(const ProxyMember &m, int &begin, int &end)
{
    const int total = m.mesh->numTriangleIndices;
    if (m.indexCount <= 0) {
        begin = 0;
        end = total;
        return;
    }
    begin = std::max(0, std::min(m.indexStart, total));
    end = std::min(total, begin + m.indexCount);
}

/// Total triangle area of a mesh in owned form — what survived, in the
/// only unit that notices geometry going missing rather than moving.
double triangleArea(const std::vector<float> &positions,
                    const std::vector<int32_t> &indices)
{
    double total = 0.0;
    const size_t verts = positions.size() / 3;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const int32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a < 0 || b < 0 || c < 0 || size_t(a) >= verts || size_t(b) >= verts
                || size_t(c) >= verts)
            continue;
        const float *pa = &positions[size_t(a) * 3];
        const float *pb = &positions[size_t(b) * 3];
        const float *pc = &positions[size_t(c) * 3];
        const double ux = pb[0] - pa[0], uy = pb[1] - pa[1],
                     uz = pb[2] - pa[2];
        const double vx = pc[0] - pa[0], vy = pc[1] - pa[1],
                     vz = pc[2] - pa[2];
        const double cx = uy * vz - uz * vy;
        const double cy = uz * vx - ux * vz;
        const double cz = ux * vy - uy * vx;
        total += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
    }
    return total;
}

double millisSince(const std::chrono::steady_clock::time_point &start)
{
    const std::chrono::duration<double, std::milli> elapsed =
        std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

}  // namespace

bool Render::mergeProxyMembers(const std::vector<ProxyMember> &members,
                               SimplifiedMesh &merged,
                               std::vector<uint64_t> *partKeys,
                               ProxyMeshStats *stats)
{
    merged = SimplifiedMesh();
    if (partKeys)
        partKeys->clear();
    if (stats)
        *stats = ProxyMeshStats();
    if (members.empty())
        return false;

    const auto mergeStart = std::chrono::steady_clock::now();

    // Attributes are all-or-nothing across the merge: a mesh half of
    // whose vertices carry a colour has no honest value for the other
    // half, and inventing one would show. Within a proxy this costs
    // nothing in practice -- per-vertex colour is part of the material,
    // and the material is what the members were grouped by (§5.1).
    bool allNormals = true;
    bool allColors = true;
    uint32_t triangleIndexTotal = 0;
    for (const ProxyMember &m : members) {
        if (!m.mesh || !m.mesh->positions || m.mesh->numVertices <= 0)
            continue;
        int begin = 0, end = 0;
        memberRange(m, begin, end);
        if (end <= begin || !m.mesh->triangleIndices)
            continue;
        triangleIndexTotal += uint32_t(end - begin);
        allNormals = allNormals && m.mesh->normals != nullptr;
        allColors = allColors && m.mesh->colors != nullptr;
    }
    if (!triangleIndexTotal)
        return false;

    merged.positions.reserve(size_t(triangleIndexTotal));
    merged.triangleIndices.reserve(size_t(triangleIndexTotal));
    merged.triangleParts.reserve(members.size());
    if (partKeys)
        partKeys->reserve(members.size());

    float bmin[3] = {0.0f, 0.0f, 0.0f};
    float bmax[3] = {0.0f, 0.0f, 0.0f};
    bool haveBounds = false;
    // Distinct *content*, not distinct objects. A MeshData is a view,
    // and two occurrences of one shape are two views onto the arrays a
    // single cache entry owns -- which is exactly the case the gate
    // exists to catch, and counting the views would report every one of
    // them as its own geometry and say the gate never binds.
    std::set<std::pair<uint64_t, const MeshData *>> seenMeshes;
    uint32_t sourceVertices = 0;
    uint32_t sourceTriangles = 0;
    uint32_t uniqueTriangles = 0;
    // Source vertex -> merged vertex, rebuilt per member: a member
    // contributes only the vertices its own range names, so a draw of
    // one face out of a thousand carries one face's worth of vertices.
    std::vector<int32_t> remap;

    for (const ProxyMember &m : members) {
        const int partStart = int(merged.triangleIndices.size());
        int begin = 0, end = 0;
        const bool usable = m.mesh && m.mesh->positions
            && m.mesh->numVertices > 0 && m.mesh->triangleIndices;
        if (usable)
            memberRange(m, begin, end);
        if (!usable || end <= begin) {
            // Still a slot: the part table is positional, and a member
            // that contributes nothing must not shift the ones after it.
            merged.triangleParts.emplace_back(partStart, 0);
            if (partKeys)
                partKeys->push_back(m.objectKey);
            continue;
        }
        const MeshData &mesh = *m.mesh;
        const NormalXform normals(m.model);
        remap.assign(size_t(mesh.numVertices), -1);
        sourceTriangles += uint32_t((end - begin) / 3);
        // A cache id is the content key where there is one; without it
        // the view's own address is the best identity available.
        const std::pair<uint64_t, const MeshData *> identity(
                mesh.cacheId, mesh.cacheId ? nullptr : &mesh);
        if (seenMeshes.insert(identity).second)
            uniqueTriangles += uint32_t((end - begin) / 3);

        for (int i = begin; i + 2 < end; i += 3) {
            int32_t corner[3] = {mesh.triangleIndices[i],
                                 mesh.triangleIndices[i + 1],
                                 mesh.triangleIndices[i + 2]};
            if (corner[0] < 0 || corner[1] < 0 || corner[2] < 0
                    || corner[0] >= mesh.numVertices
                    || corner[1] >= mesh.numVertices
                    || corner[2] >= mesh.numVertices)
                continue;
            if (normals.mirrored)
                std::swap(corner[1], corner[2]);
            for (int32_t &v : corner) {
                int32_t &slot = remap[size_t(v)];
                if (slot < 0) {
                    slot = int32_t(merged.positions.size() / 3);
                    float p[3];
                    const float *src = mesh.positions + size_t(v) * 3;
                    if (m.model)
                        transformPoint(m.model, src, p);
                    else {
                        p[0] = src[0];
                        p[1] = src[1];
                        p[2] = src[2];
                    }
                    merged.positions.insert(merged.positions.end(),
                                            {p[0], p[1], p[2]});
                    if (!haveBounds) {
                        bmin[0] = bmax[0] = p[0];
                        bmin[1] = bmax[1] = p[1];
                        bmin[2] = bmax[2] = p[2];
                        haveBounds = true;
                    }
                    else {
                        for (int k = 0; k < 3; ++k) {
                            bmin[k] = std::min(bmin[k], p[k]);
                            bmax[k] = std::max(bmax[k], p[k]);
                        }
                    }
                    if (allNormals) {
                        float n[3];
                        normals.apply(mesh.normals + size_t(v) * 3, n);
                        merged.normals.insert(merged.normals.end(),
                                              {n[0], n[1], n[2]});
                    }
                    if (allColors) {
                        const uint8_t *c = mesh.colors + size_t(v) * 4;
                        merged.colors.insert(merged.colors.end(),
                                             {c[0], c[1], c[2], c[3]});
                    }
                    ++sourceVertices;
                }
                v = slot;
            }
            merged.triangleIndices.insert(merged.triangleIndices.end(),
                                          {corner[0], corner[1], corner[2]});
        }
        merged.triangleParts.emplace_back(
            partStart, int(merged.triangleIndices.size()) - partStart);
        if (partKeys)
            partKeys->push_back(m.objectKey);
    }

    const double mergeMs = millisSince(mergeStart);
    float extent = 0.0f;
    if (haveBounds) {
        const float dx = bmax[0] - bmin[0];
        const float dy = bmax[1] - bmin[1];
        const float dz = bmax[2] - bmin[2];
        extent = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    if (stats) {
        stats->members = uint32_t(members.size());
        stats->distinctMeshes = uint32_t(seenMeshes.size());
        stats->sourceVertices = sourceVertices;
        stats->sourceTriangles = sourceTriangles;
        stats->uniqueTriangles = uniqueTriangles;
        stats->mergedVertices = uint32_t(merged.positions.size() / 3);
        stats->extent = extent;
        stats->sourceArea =
            triangleArea(merged.positions, merged.triangleIndices);
        stats->mergeMs = mergeMs;
    }
    return !merged.triangleIndices.empty();
}

bool Render::decimateProxyMesh(const SimplifiedMesh &merged,
                               const ProxyMeshParams &params,
                               SimplifiedMesh &out, ProxyMeshStats *stats)
{
    out = SimplifiedMesh();
    MeshData view;
    merged.fill(view);
    SimplifyOptions opts;
    opts.anchor = params.anchor;
    // Where a proxy parts company with a level of a single object: its
    // members are separate objects, so there is no crease between them
    // to protect and no reason to pay for a duplicate representative
    // per member boundary (SimplifyOptions::weldAcrossParts).
    opts.weldAcrossParts = true;
    SimplifyStats simplified;
    const auto simplifyStart = std::chrono::steady_clock::now();
    const bool made =
        simplifyMesh(view, params.cellSize, out, opts, &simplified);
    if (stats) {
        stats->simplifyMs = millisSince(simplifyStart);
        stats->maxError = simplified.maxDisplacement;
        stats->rmsError = simplified.rmsDisplacement;
        stats->proxyVertices = made ? uint32_t(out.numVertices()) : 0;
        stats->proxyTriangles =
            made ? uint32_t(out.triangleIndices.size() / 3) : 0;
        stats->proxyArea =
            made ? triangleArea(out.positions, out.triangleIndices) : 0.0;
        // A member whose run came back empty is not decimated, it is
        // gone: nothing in the proxy stands for it (§6 lets a pick
        // resolve to no object, never the wrong one).
        for (size_t i = 0; made && i < merged.triangleParts.size(); ++i) {
            if (merged.triangleParts[i].second > 0
                    && (i >= out.triangleParts.size()
                        || out.triangleParts[i].second == 0))
                ++stats->collapsedMembers;
        }
        if (!made) {
            for (const auto &part : merged.triangleParts)
                if (part.second > 0)
                    ++stats->collapsedMembers;
        }
    }
    return made;
}

bool Render::buildProxyMesh(const std::vector<ProxyMember> &members,
                            const ProxyMeshParams &params, SimplifiedMesh &out,
                            std::vector<uint64_t> *partKeys,
                            ProxyMeshStats *stats)
{
    out = SimplifiedMesh();
    SimplifiedMesh merged;
    if (!mergeProxyMembers(members, merged, partKeys, stats))
        return false;
    if (!decimateProxyMesh(merged, params, out, stats)) {
        if (partKeys)
            partKeys->clear();
        return false;
    }
    return true;
}
