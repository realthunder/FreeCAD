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

/// What a cell collapses to: the running sums that make the
/// representative, plus the index it is assigned in the output.
struct Cluster {
    double px = 0.0, py = 0.0, pz = 0.0;
    double nx = 0.0, ny = 0.0, nz = 0.0;
    double cr = 0.0, cg = 0.0, cb = 0.0, ca = 0.0;
    uint32_t count = 0;
    int32_t index = -1;
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
    // Everything an element map would answer is deliberately absent --
    // see the header. A rung with no part tables picks as the whole
    // object, which is what the box already does.
    mesh.noSeamLineIndices = nullptr;
    mesh.numNoSeamLineIndices = 0;
    mesh.pointIndices = nullptr;
    mesh.numPointIndices = 0;
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

    // Pass one: which cell each vertex falls in, and the sums that make
    // each cell's representative. Averaging rather than snapping to the
    // cell centre keeps a flat face flat -- snapping visibly corrugates
    // one, because neighbouring vertices on the same plane land in
    // different cells and each jumps to a different centre.
    std::map<Cell, Cluster> clusters;
    std::vector<Cluster *> vertexCluster(size_t(src.numVertices), nullptr);
    for (int v = 0; v < src.numVertices; ++v) {
        const float *p = src.positions + size_t(v) * 3;
        Cluster &cl = clusters[cellOf(p, anchor, cellSize)];
        cl.px += p[0];
        cl.py += p[1];
        cl.pz += p[2];
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
        vertexCluster[size_t(v)] = &cl;
    }
    // Nothing merged: the level would be the mesh, and a rung identical
    // to the one above it is worse than no rung -- it costs a fetch and
    // a key to change nothing.
    if (clusters.size() >= size_t(src.numVertices))
        return false;

    // Pass two: assign output indices in the map's order, which is
    // deterministic, and emit the representatives.
    out.positions.reserve(clusters.size() * 3);
    if (src.normals)
        out.normals.reserve(clusters.size() * 3);
    if (src.colors)
        out.colors.reserve(clusters.size() * 4);
    int32_t next = 0;
    for (auto &entry : clusters) {
        Cluster &cl = entry.second;
        cl.index = next++;
        const double inv = 1.0 / double(cl.count);
        out.positions.push_back(float(cl.px * inv));
        out.positions.push_back(float(cl.py * inv));
        out.positions.push_back(float(cl.pz * inv));
        if (src.normals) {
            double nx = cl.nx, ny = cl.ny, nz = cl.nz;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-12) {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            else {
                // The cell's normals cancelled -- a fold welded onto
                // itself. Any unit vector is as wrong as any other;
                // pick a fixed one so the output stays deterministic.
                nx = 0.0;
                ny = 0.0;
                nz = 1.0;
            }
            out.normals.push_back(float(nx));
            out.normals.push_back(float(ny));
            out.normals.push_back(float(nz));
        }
        if (src.colors) {
            out.colors.push_back(uint8_t(cl.cr * inv + 0.5));
            out.colors.push_back(uint8_t(cl.cg * inv + 0.5));
            out.colors.push_back(uint8_t(cl.cb * inv + 0.5));
            out.colors.push_back(uint8_t(cl.ca * inv + 0.5));
        }
    }

    // Pass three: remap the triangles, dropping those that collapsed.
    // A triangle whose corners no longer land in three distinct cells
    // has no area left to draw and would be a degenerate the backend
    // has to cull every frame.
    out.triangleIndices.reserve(size_t(src.numTriangleIndices));
    for (int i = 0; i + 2 < src.numTriangleIndices; i += 3) {
        const int32_t a = src.triangleIndices[i];
        const int32_t b = src.triangleIndices[i + 1];
        const int32_t c = src.triangleIndices[i + 2];
        if (a < 0 || b < 0 || c < 0 || a >= src.numVertices
                || b >= src.numVertices || c >= src.numVertices)
            continue;
        const int32_t ia = vertexCluster[size_t(a)]->index;
        const int32_t ib = vertexCluster[size_t(b)]->index;
        const int32_t ic = vertexCluster[size_t(c)]->index;
        if (ia == ib || ib == ic || ia == ic)
            continue;
        out.triangleIndices.push_back(ia);
        out.triangleIndices.push_back(ib);
        out.triangleIndices.push_back(ic);
    }
    if (out.triangleIndices.empty())
        return false;

    // The edges too, deduplicated: clustering maps many original edges
    // onto the same pair, and a coarse rung that drew each of them would
    // spend more on lines than on the surface it is standing in for.
    if (src.lineIndices && src.numLineIndices > 0) {
        std::vector<std::pair<int32_t, int32_t>> edges;
        edges.reserve(size_t(src.numLineIndices / 2));
        for (int i = 0; i + 1 < src.numLineIndices; i += 2) {
            const int32_t a = src.lineIndices[i];
            const int32_t b = src.lineIndices[i + 1];
            if (a < 0 || b < 0 || a >= src.numVertices
                    || b >= src.numVertices)
                continue;
            int32_t ia = vertexCluster[size_t(a)]->index;
            int32_t ib = vertexCluster[size_t(b)]->index;
            if (ia == ib)
                continue;
            if (ia > ib)
                std::swap(ia, ib);
            edges.emplace_back(ia, ib);
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        out.lineIndices.reserve(edges.size() * 2);
        for (const auto &e : edges) {
            out.lineIndices.push_back(e.first);
            out.lineIndices.push_back(e.second);
        }
    }
    return true;
}
