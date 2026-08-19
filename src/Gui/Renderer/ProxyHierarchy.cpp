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

#include "ProxyHierarchy.h"
// materialIdentity() lives beside the serializer it is derived from, so
// that a field added to the material format cannot silently drop out of
// the bucket and merge two materials that render differently (§5.1).
#include "SceneDump.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Render;

// ---------------------------------------------------------------------
// The shared projection primitive
// ---------------------------------------------------------------------

BoxSight Render::sightBounds(const float *bmin, const float *bmax,
                             const float *V, const float *P,
                             float viewportHeightPx)
{
    BoxSight res;
    if (!bmin || !bmax || !V || !P || bmin[0] > bmax[0])
        return res;
    const float dx = bmax[0] - bmin[0];
    const float dy = bmax[1] - bmin[1];
    const float dz = bmax[2] - bmin[2];
    const float diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(diag > 0.0f))
        return res;
    // GL layout: column-major, points transform as M * p. proj[15] == 1
    // is orthographic (w does not depend on z), 0 is perspective.
    const bool ortho = P[15] != 0.0f;
    const float p11 = P[5];
    const float cx = 0.5f * (bmin[0] + bmax[0]);
    const float cy = 0.5f * (bmin[1] + bmax[1]);
    const float cz = 0.5f * (bmin[2] + bmax[2]);
    // View space; the camera looks down -z.
    const float vx = V[0] * cx + V[4] * cy + V[8] * cz + V[12];
    const float vy = V[1] * cx + V[5] * cy + V[9] * cz + V[13];
    const float vz = V[2] * cx + V[6] * cy + V[10] * cz + V[14];
    // Projected size of the diagonal in pixels: NDC height of a length
    // d is d * P11 (orthographic) or d * P11 / depth (perspective), and
    // one NDC unit is half the viewport.
    if (ortho) {
        res.diagPx = diag * p11 * 0.5f * viewportHeightPx;
    }
    else {
        // Depth of the box's near side. A box wholly behind the camera
        // is off-screen; a camera *inside* the box span sees it as
        // large as anything gets.
        if (-vz + 0.5f * diag <= 0.0f) {
            res.what = BoxSight::Offscreen;
            return res;
        }
        const float depth = -vz - 0.5f * diag;
        if (depth <= 0.0f) {
            res.what = BoxSight::Inside;
            return res;
        }
        res.diagPx = diag * p11 / depth * 0.5f * viewportHeightPx;
    }
    // Clip-space test at the box centre, inflated by the projected half
    // diagonal (in NDC units of the viewport height; the width margin
    // is approximated with the same value, conservatively).
    const float ndcMargin = res.diagPx / (0.5f * viewportHeightPx);
    const float cxc = P[0] * vx + P[4] * vy + P[8] * vz + P[12];
    const float cyc = P[1] * vx + P[5] * vy + P[9] * vz + P[13];
    const float w = ortho
        ? 1.0f : P[3] * vx + P[7] * vy + P[11] * vz + P[15];
    if (w <= 0.0f || std::fabs(cxc) > w * (1.0f + ndcMargin)
        || std::fabs(cyc) > w * (1.0f + ndcMargin)) {
        res.what = BoxSight::Offscreen;
        return res;
    }
    res.what = BoxSight::Visible;
    return res;
}

bool Render::boxReachesNearPlane(const float *bmin, const float *bmax,
                                 const float *V, const float *P,
                                 bool homogeneousDepth)
{
    if (!bmin || !bmax || !V || !P || bmin[0] > bmax[0])
        return true;   // nothing to rasterize: not answerable either
    for (int i = 0; i < 8; ++i) {
        const float x = (i & 1) ? bmax[0] : bmin[0];
        const float y = (i & 2) ? bmax[1] : bmin[1];
        const float z = (i & 4) ? bmax[2] : bmin[2];
        // GL layout, as sightBounds above: column-major, points
        // transform as M * p.
        const float vx = V[0] * x + V[4] * y + V[8] * z + V[12];
        const float vy = V[1] * x + V[5] * y + V[9] * z + V[13];
        const float vz = V[2] * x + V[6] * y + V[10] * z + V[14];
        const float vw = V[3] * x + V[7] * y + V[11] * z + V[15];
        const float cz = P[2] * vx + P[6] * vy + P[10] * vz + P[14] * vw;
        const float cw = P[3] * vx + P[7] * vy + P[11] * vz + P[15] * vw;
        if (cw <= 0.0f)
            return true;   // at or behind the eye
        // With a margin, not on the exact plane. A camera fitted to the
        // model puts the near plane *on* the whole-model box, so the
        // question is decided in the last bits of a float — and the
        // answer has to agree with the rasterizer's own clipping, which
        // computes it differently. Half a percent of the depth range
        // costs nothing (a box that close to the near plane is one the
        // camera is practically inside) and removes the disagreement.
        const float near = homogeneousDepth ? -cw : 0.0f;
        if (cz < near + 0.005f * cw)
            return true;
    }
    return false;
}

const unsigned short *Render::occlusionBoxIndices()
{
    // Each face as its four corners in rim order (0,1,3,2 for the low-z
    // face and so on), split on a diagonal of that rim. See the header
    // on why the obvious 0,1,2,3 spelling is wrong.
    static const unsigned short kIndices[36] = {
        0, 1, 3,  3, 2, 0,   // z = min
        4, 5, 7,  7, 6, 4,   // z = max
        0, 1, 5,  5, 4, 0,   // y = min
        2, 3, 7,  7, 6, 2,   // y = max
        0, 2, 6,  6, 4, 0,   // x = min
        1, 3, 7,  7, 5, 1,   // x = max
    };
    return kIndices;
}

float Render::depthQuantumPad(const float *bmin, const float *bmax,
                              const float *V, const float *P,
                              bool homogeneousDepth, float lsb)
{
    if (!bmin || !bmax || !V || !P || bmin[0] > bmax[0] || !(lsb > 0.0f))
        return 0.0f;
    // The nearest corner decides: LEQUAL needs one fragment to pass, and
    // the front of the box is where that happens.
    float nearest = 0.0f;
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        const float x = (i & 1) ? bmax[0] : bmin[0];
        const float y = (i & 2) ? bmax[1] : bmin[1];
        const float z = (i & 4) ? bmax[2] : bmin[2];
        // GL layout, as sightBounds: column-major, points as M * p, and
        // the camera looks down -z, so eye distance is -vz.
        const float e = -(V[2] * x + V[6] * y + V[10] * z + V[14]);
        if (!(e > 0.0f))
            continue;
        if (!any || e < nearest) {
            nearest = e;
            any = true;
        }
    }
    if (!any)
        return 0.0f;
    // One depth step in NDC. The 24 bits are the scene target's
    // (D24S8); the -1..1 range of OpenGL's clip convention spends them
    // over twice the interval D3D/Vulkan/Metal's 0..1 does.
    const float step = lsb * (homogeneousDepth ? 2.0f : 1.0f) / 16777216.0f;
    const bool ortho = P[15] != 0.0f;
    // Invert the projection's depth derivative at that corner. For a
    // perspective projection ndc_z = -P[10] + P[14]/e, so d(ndc)/de is
    // -P[14]/e^2; for an orthographic one ndc_z = -P[10]*e + P[14] and
    // the derivative is constant. A degenerate projection (either term
    // zero) has no usable derivative — pad nothing rather than divide.
    const float denom = ortho ? std::fabs(P[10])
                              : std::fabs(P[14]) / (nearest * nearest);
    if (!(denom > 0.0f))
        return 0.0f;
    return step / denom;
}

// ---------------------------------------------------------------------
// The instance table
// ---------------------------------------------------------------------

namespace
{

/// Primitives one draw issues. `indexCount` is the draw's own range
/// when set; 0 means the whole buffer, and which buffer is decided by
/// the material's topology, not by which arrays the mesh happens to
/// carry -- a mesh usually has both triangles and lines, and charging a
/// line draw for the triangle array would count most of the scene twice.
uint32_t drawPrimitives(const DrawCall &d)
{
    int indices = d.indexCount;
    int perPrim = 3;
    if (d.material.type == Material::Line)
        perPrim = 2;
    else if (d.material.type == Material::Point)
        perPrim = 1;
    if (indices <= 0) {
        if (!d.mesh)
            return 0;
        indices = d.material.type == Material::Line ? d.mesh->numLineIndices
            : d.material.type == Material::Point ? d.mesh->numPointIndices
            : d.mesh->numTriangleIndices;
    }
    return indices > 0 ? uint32_t(indices / perPrim) : 0u;
}

}  // anonymous namespace

uint32_t Render::meshResidentBytes(const MeshData *m)
{
    if (!m)
        return 0;
    uint64_t bytes = uint64_t(m->numVertices) * 3 * sizeof(float);
    if (m->normals)
        bytes += uint64_t(m->numVertices) * 3 * sizeof(float);
    if (m->colors)
        bytes += uint64_t(m->numVertices) * 4;
    bytes += uint64_t(m->numTriangleIndices) * sizeof(int32_t);
    bytes += uint64_t(m->numLineIndices) * sizeof(int32_t);
    bytes += uint64_t(m->numPointIndices) * sizeof(int32_t);
    return uint32_t(std::min<uint64_t>(bytes, 0xffffffffULL));
}

void Render::proxyInstances(const DrawCallList &draws,
                            std::vector<ProxyInstance> &out)
{
    out.clear();
    out.reserve(draws.size());
    for (size_t i = 0; i < draws.size(); ++i) {
        const auto &d = draws[i];
        if (d.bboxMin[0] > d.bboxMax[0])
            continue;
        ProxyInstance inst;
        inst.drawIndex = uint32_t(i);
        inst.objectKey = d.objectKey;
        std::memcpy(inst.bboxMin, d.bboxMin, sizeof(inst.bboxMin));
        std::memcpy(inst.bboxMax, d.bboxMax, sizeof(inst.bboxMax));
        inst.materialBucket = d.materialIndex >= 0
            ? uint64_t(d.materialIndex) : materialIdentity(d.material);
        inst.sourceTag = d.mesh ? static_cast<const void *>(d.mesh.get())
                                : nullptr;
        // What generation will and will not accept (ProxyStore::build):
        // triangles only, and never a stand-in box, which is not
        // source geometry in the first place.
        inst.mergeable =
            d.material.type == Material::Triangle && !d.standIn && d.mesh;
        inst.primCount = drawPrimitives(d);
        inst.meshBytes = Render::meshResidentBytes(d.mesh.get());
        out.push_back(inst);
    }
}

// ---------------------------------------------------------------------
// The partition
// ---------------------------------------------------------------------

namespace
{

/// Spread the low 16 bits of \a x so that bit i lands at bit 3i — the
/// interleave half of a Morton code.
uint64_t part1By2(uint64_t x)
{
    // Three coordinates interleave, so bit i has to land at bit 3i and
    // the masks are the base-8 ones. The base-4 masks of a
    // two-dimensional Morton code (0x5555...) look almost right and are
    // not: they spread bit i to bit 2i, so the three shifted halves
    // overlap and cells collide -- (0,0,1) and (2,0,0) both came out as
    // 4, which gave two nodes of one level the same id. Sixteen bits in,
    // forty-eight out, which is what leaves the level its room above.
    x &= 0xffffULL;
    x = (x | (x << 32)) & 0x001f00000000ffffULL;
    x = (x | (x << 16)) & 0x001f0000ff0000ffULL;
    x = (x | (x << 8))  & 0x100f00f00f00f00fULL;
    x = (x | (x << 4))  & 0x10c30c30c30c30c3ULL;
    x = (x | (x << 2))  & 0x1249249249249249ULL;
    return x;
}

/// (level, cell) as one stable value. Sixteen levels of three bits fit
/// in 48, leaving the level room in the high bits.
uint64_t nodeIdOf(uint32_t level, const uint32_t cell[3])
{
    const uint64_t morton = part1By2(cell[0])
        | (part1By2(cell[1]) << 1) | (part1By2(cell[2]) << 2);
    return (uint64_t(level) << 56) | morton;
}

/// Index of \a p on the level-\a level grid, clamped into the root.
uint32_t cellOf(float p, float rootMin, float rootEdge, uint32_t level)
{
    const uint32_t span = 1u << level;
    if (!(rootEdge > 0.0f))
        return 0;
    const float t = (p - rootMin) / rootEdge;
    if (!(t > 0.0f))
        return 0;
    if (t >= 1.0f)
        return span - 1;
    const uint32_t idx = uint32_t(t * float(span));
    return idx < span ? idx : span - 1;
}

void growBounds(float *bmin, float *bmax, const float *omin, const float *omax)
{
    for (int i = 0; i < 3; ++i) {
        if (omin[i] < bmin[i])
            bmin[i] = omin[i];
        if (omax[i] > bmax[i])
            bmax[i] = omax[i];
    }
}

}  // namespace

bool ProxyHierarchy::empty() const
{
    return rootnode == kNoProxyNode;
}

int ProxyHierarchy::root() const
{
    return rootnode;
}

const ProxyParams &ProxyHierarchy::params() const
{
    return parameters;
}

const std::vector<ProxyNode> &ProxyHierarchy::nodes() const
{
    return nodedata;
}

const std::vector<ProxyInstance> &ProxyHierarchy::instances() const
{
    return instancedata;
}

const std::vector<uint32_t> &ProxyHierarchy::residents() const
{
    return residentdata;
}

const std::vector<uint64_t> &ProxyHierarchy::buckets() const
{
    return bucketdata;
}

void ProxyHierarchy::subtreeInstances(int node,
                                      std::vector<uint32_t> &out) const
{
    if (node == kNoProxyNode || node < 0 || size_t(node) >= nodedata.size())
        return;
    const ProxyNode &n = nodedata[size_t(node)];
    out.insert(out.end(), residentdata.begin() + n.residentFirst,
               residentdata.begin() + n.residentFirst + n.residentCount);
    for (int c : n.child)
        subtreeInstances(c, out);
}

void ProxyHierarchy::clear()
{
    instancedata.clear();
    nodedata.clear();
    residentdata.clear();
    bucketdata.clear();
    assignlevel.clear();
    rootnode = kNoProxyNode;
    rootedge = 0.0f;
    rootmin[0] = rootmin[1] = rootmin[2] = 0.0f;
}

void ProxyHierarchy::build(const std::vector<ProxyInstance> &instances,
                           const ProxyParams &params)
{
    clear();
    parameters = params;
    if (parameters.maxPerCell < 1)
        parameters.maxPerCell = 1;
    if (parameters.maxLevel > 16)
        parameters.maxLevel = 16;  // three bits a level, packed into 48

    instancedata.reserve(instances.size());
    for (const auto &inst : instances) {
        if (inst.bboxMin[0] > inst.bboxMax[0])
            continue;  // not locatable, therefore not partitionable
        instancedata.push_back(inst);
    }
    if (instancedata.empty())
        return;

    // Root bounds, cubed: cells stay cubes at every level, so one
    // extent target means the same thing on all three axes.
    float bmin[3], bmax[3];
    std::memcpy(bmin, instancedata[0].bboxMin, sizeof(bmin));
    std::memcpy(bmax, instancedata[0].bboxMax, sizeof(bmax));
    for (const auto &inst : instancedata)
        growBounds(bmin, bmax, inst.bboxMin, inst.bboxMax);
    float edge = 0.0f;
    for (int i = 0; i < 3; ++i)
        edge = std::max(edge, bmax[i] - bmin[i]);
    if (!(edge > 0.0f)) {
        // Every instance at one point: a partition cannot say anything
        // useful, so the root holds them all.
        edge = 1.0f;
    }
    // Centre the cube on the content, and pad it a hair so that an
    // instance touching the far face still lands inside the grid.
    for (int i = 0; i < 3; ++i)
        rootmin[i] = 0.5f * (bmin[i] + bmax[i]) - 0.5f * edge;
    rootedge = edge * 1.0001f;
    for (int i = 0; i < 3; ++i)
        rootmin[i] -= 0.00005f * edge;

    // The extent target as a level cap: the finest level whose cell
    // edge is still at or above minCellFraction of the root.
    uint32_t levelCap = parameters.maxLevel;
    if (parameters.minCellFraction > 0.0f) {
        uint32_t l = 0;
        float frac = 1.0f;
        while (l < parameters.maxLevel
               && frac * 0.5f >= parameters.minCellFraction) {
            frac *= 0.5f;
            ++l;
        }
        levelCap = l;
    }

    // Assignment by size (§3.2), and by size *alone*: the finest level
    // whose cell edge is still at least the instance's extent. Placement
    // is then by centre, and the cell is **loose** — it owns everything
    // whose centre falls in it, which may reach half a cell beyond its
    // nominal bounds on every side. Since an instance placed at level L
    // is no larger than a level-L cell, that half-cell skirt contains it
    // whatever its position.
    //
    // The alternative — assign to the finest cell that contains the box
    // *whole* — reads more natural and is a trap, measured rather than
    // reasoned: on a uniform lattice it pushed 28% of the parts up to
    // coarse levels purely for straddling a grid line, where nothing
    // ever aggregated them and they drew exactly forever. Position must
    // not decide fidelity; only size may.
    assignlevel.resize(instancedata.size(), 0);
    for (size_t i = 0; i < instancedata.size(); ++i) {
        const ProxyInstance &inst = instancedata[i];
        float ext = 0.0f;
        for (int a = 0; a < 3; ++a)
            ext = std::max(ext, inst.bboxMax[a] - inst.bboxMin[a]);
        uint32_t level = levelCap;
        if (ext > 0.0f) {
            const float ratio = rootedge / ext;
            if (ratio > 1.0f) {
                const int byLog = int(std::floor(std::log2(ratio)));
                if (byLog < int(level))
                    level = uint32_t(byLog < 0 ? 0 : byLog);
            }
            else {
                level = 0;
            }
        }
        assignlevel[i] = level;
    }

    std::vector<uint32_t> all(instancedata.size());
    for (uint32_t i = 0; i < uint32_t(instancedata.size()); ++i)
        all[i] = i;
    const uint32_t origin[3] = {0, 0, 0};
    rootnode = buildNode(0, origin, std::move(all));
}

int ProxyHierarchy::buildNode(uint32_t level, const uint32_t cell[3],
                              std::vector<uint32_t> items)
{
    const int self = int(nodedata.size());
    nodedata.emplace_back();
    {
        ProxyNode &node = nodedata[size_t(self)];
        node.level = level;
        node.cell[0] = cell[0];
        node.cell[1] = cell[1];
        node.cell[2] = cell[2];
        node.id = nodeIdOf(level, cell);
        const float size = rootedge / float(1u << level);
        for (int a = 0; a < 3; ++a) {
            node.cellMin[a] = rootmin[a] + float(cell[a]) * size;
            node.cellMax[a] = node.cellMin[a] + size;
        }
        node.subtreeCount = uint32_t(items.size());
        uint64_t prims = 0;
        for (uint32_t idx : items)
            prims += instancedata[idx].primCount;
        node.subtreePrims = prims;
    }

    // Stop conditions (§3.2): K, the depth cap, and the extent target.
    // Reaching any of them makes every remaining item a resident,
    // whatever level its size would have allowed.
    const bool split = items.size() > parameters.maxPerCell
        && level < parameters.maxLevel;
    std::vector<uint32_t> octant[8];
    std::vector<uint32_t> mine;
    if (!split) {
        mine.swap(items);
    }
    else {
        mine.reserve(8);
        for (uint32_t idx : items) {
            if (assignlevel[idx] <= level) {
                mine.push_back(idx);
                continue;
            }
            // By centre: a loose cell owns what is centred in it, which
            // is what keeps a grid line from deciding an instance's
            // level. The centre of something placed here lies inside
            // this node's nominal cell, so it lands in one of the eight.
            uint32_t c[3];
            for (int a = 0; a < 3; ++a) {
                const float mid = 0.5f * (instancedata[idx].bboxMin[a]
                                          + instancedata[idx].bboxMax[a]);
                c[a] = cellOf(mid, rootmin[a], rootedge, level + 1);
            }
            const int o = int((c[0] - cell[0] * 2) & 1u)
                | int(((c[1] - cell[1] * 2) & 1u) << 1)
                | int(((c[2] - cell[2] * 2) & 1u) << 2);
            octant[o].push_back(idx);
        }
    }

    // Residents are appended before recursing, so a node's range stays
    // contiguous while its children append theirs after it.
    const uint32_t residentFirst = uint32_t(residentdata.size());
    residentdata.insert(residentdata.end(), mine.begin(), mine.end());
    {
        ProxyNode &node = nodedata[size_t(self)];
        node.residentFirst = residentFirst;
        node.residentCount = uint32_t(mine.size());
    }

    if (split) {
        for (int o = 0; o < 8; ++o) {
            if (octant[o].empty())
                continue;
            const uint32_t childCell[3] = {
                cell[0] * 2 + uint32_t(o & 1),
                cell[1] * 2 + uint32_t((o >> 1) & 1),
                cell[2] * 2 + uint32_t((o >> 2) & 1),
            };
            const int kid = buildNode(level + 1, childCell,
                                      std::move(octant[o]));
            nodedata[size_t(self)].child[o] = kid;
        }
    }

    // Content bounds and the material set, bottom-up: this is the same
    // direction §7.1 generates proxies in, and for the same reason —
    // a parent is a function of its children, not of the source.
    float cmin[3] = {0.0f, 0.0f, 0.0f};
    float cmax[3] = {0.0f, 0.0f, 0.0f};
    bool any = false;
    std::vector<uint64_t> mats;
    for (uint32_t r = 0; r < nodedata[size_t(self)].residentCount; ++r) {
        const ProxyInstance &inst =
            instancedata[residentdata[residentFirst + r]];
        if (!any) {
            std::memcpy(cmin, inst.bboxMin, sizeof(cmin));
            std::memcpy(cmax, inst.bboxMax, sizeof(cmax));
            any = true;
        }
        else {
            growBounds(cmin, cmax, inst.bboxMin, inst.bboxMax);
        }
        mats.push_back(inst.materialBucket);
    }
    for (int o = 0; o < 8; ++o) {
        const int kid = nodedata[size_t(self)].child[o];
        if (kid == kNoProxyNode)
            continue;
        const ProxyNode &c = nodedata[size_t(kid)];
        if (!any) {
            std::memcpy(cmin, c.contentMin, sizeof(cmin));
            std::memcpy(cmax, c.contentMax, sizeof(cmax));
            any = true;
        }
        else {
            growBounds(cmin, cmax, c.contentMin, c.contentMax);
        }
        for (uint32_t b = 0; b < c.bucketCount; ++b)
            mats.push_back(bucketdata[c.bucketFirst + b]);
    }
    std::sort(mats.begin(), mats.end());
    mats.erase(std::unique(mats.begin(), mats.end()), mats.end());

    const uint32_t bucketFirst = uint32_t(bucketdata.size());
    bucketdata.insert(bucketdata.end(), mats.begin(), mats.end());
    {
        ProxyNode &node = nodedata[size_t(self)];
        node.bucketFirst = bucketFirst;
        node.bucketCount = uint32_t(mats.size());
        if (any) {
            std::memcpy(node.contentMin, cmin, sizeof(cmin));
            std::memcpy(node.contentMax, cmax, sizeof(cmax));
        }
        else {
            // An empty node cannot be sighted; mark it unjudgeable the
            // way an empty draw is (min > max).
            node.contentMin[0] = 1.0f;
            node.contentMax[0] = -1.0f;
        }
    }
    return self;
}

// ---------------------------------------------------------------------
// The cut
// ---------------------------------------------------------------------

void ProxyHierarchy::selectCut(const float *view, const float *proj,
                               float viewportHeightPx, float tolerancePx,
                               ProxyCut &out) const
{
    selectCutImpl(view, proj, viewportHeightPx, tolerancePx, nullptr, out);
}

void ProxyHierarchy::selectCut(const float *view, const float *proj,
                               float viewportHeightPx, float tolerancePx,
                               const std::vector<ProxyNodeCost> &costs,
                               ProxyCut &out) const
{
    selectCutImpl(view, proj, viewportHeightPx, tolerancePx, &costs, out);
}

void ProxyHierarchy::selectCutImpl(const float *view, const float *proj,
                                   float viewportHeightPx, float tolerancePx,
                                   const std::vector<ProxyNodeCost> *costs,
                                   ProxyCut &out) const
{
    out.proxyNodes.clear();
    out.exact.clear();
    out.exactNode.clear();
    out.drawCount = 0;
    out.proxyDraws = 0;
    out.coveredInstances = 0;
    out.culledInstances = 0;
    out.unmergeableInstances = 0;
    out.unmergeablePrims = 0;
    out.exactPrims = 0;
    out.coveredPrims = 0;
    out.culledPrims = 0;
    out.proxyPrims = 0;
    if (rootnode == kNoProxyNode || !view || !proj || viewportHeightPx <= 0.0f)
        return;

    std::vector<int> stack;
    std::vector<uint32_t> scratch;
    stack.push_back(rootnode);
    while (!stack.empty()) {
        const int ni = stack.back();
        stack.pop_back();
        const ProxyNode &node = nodedata[size_t(ni)];
        const BoxSight sight = sightBounds(node.contentMin, node.contentMax,
                                           view, proj, viewportHeightPx);
        if (sight.what == BoxSight::Offscreen || sight.what == BoxSight::Empty) {
            // The population a cut stops touching altogether.
            out.culledInstances += node.subtreeCount;
            out.culledPrims += node.subtreePrims;
            continue;
        }
        // A node draws a proxy when the camera cannot resolve the
        // difference and there is something to merge. minMerge keeps a
        // lone instance out: merging one object is a decimated object,
        // which the per-object ladder already does better.
        //
        // What "cannot resolve" means is the whole of section 3.3.
        // Given generation's measurement it is the node's own error,
        // projected -- errorRatio is a fraction of the extent, so the
        // extent already projected times that ratio is the error in
        // pixels, and no second projection is needed. Without it the
        // extent stands in, which is phase 1's approximation and which
        // 11.1c measured to be a distribution rather than a constant.
        const ProxyNodeCost *cost =
            costs && size_t(ni) < costs->size() ? &(*costs)[size_t(ni)]
                                                : nullptr;
        const bool judgeable =
            !costs || (cost && cost->errorRatio >= 0.0f);
        const float judged =
            costs ? (cost ? cost->errorRatio * sight.diagPx : 0.0f)
                  : sight.diagPx;
        if (sight.what == BoxSight::Visible && judgeable
            && judged <= tolerancePx
            && node.subtreeCount >= parameters.minMerge) {
            out.proxyNodes.push_back(ni);
            out.proxyDraws +=
                cost && cost->draws ? cost->draws : node.bucketCount;
            if (cost)
                out.proxyPrims += cost->prims;
            // Not everything below a stopped node is a proxy's to
            // cover. Lines, points and stand-in boxes are refused by
            // generation on purpose, so nothing above them ever draws
            // them, and counting them as covered was counting a hole --
            // 17-27% of the covered figure on MiSTer, 99% of it edges
            // (11.1g). They draw exactly, beside the proxy.
            //
            // This walks the subtree, which a counting-only cut did not
            // have to do. Phase 4 has to walk it anyway to issue those
            // draws, and a saving that cannot be issued is not a
            // saving.
            scratch.clear();
            subtreeInstances(ni, scratch);
            for (uint32_t idx : scratch) {
                const ProxyInstance &inst = instancedata[idx];
                if (inst.mergeable) {
                    out.coveredInstances += 1;
                    out.coveredPrims += inst.primCount;
                    continue;
                }
                // Judged like any other exact draw, since that is what
                // it is: off screen it is culled rather than covered.
                const BoxSight is = sightBounds(inst.bboxMin, inst.bboxMax,
                                                view, proj, viewportHeightPx);
                if (is.what == BoxSight::Offscreen
                        || is.what == BoxSight::Empty) {
                    out.culledInstances += 1;
                    out.culledPrims += inst.primCount;
                    continue;
                }
                out.exact.push_back(idx);
                out.exactNode.push_back(ni);
                out.exactPrims += inst.primCount;
                out.unmergeableInstances += 1;
                out.unmergeablePrims += inst.primCount;
            }
            continue;
        }
        // Descended past: this node's own residents are covered by no
        // proxy below them, so they draw exactly.
        for (uint32_t r = 0; r < node.residentCount; ++r) {
            const uint32_t idx = residentdata[node.residentFirst + r];
            const ProxyInstance &inst = instancedata[idx];
            const BoxSight is = sightBounds(inst.bboxMin, inst.bboxMax,
                                            view, proj, viewportHeightPx);
            if (is.what == BoxSight::Offscreen || is.what == BoxSight::Empty) {
                out.culledInstances += 1;
                out.culledPrims += inst.primCount;
            }
            else {
                out.exact.push_back(idx);
                out.exactNode.push_back(ni);
                out.exactPrims += inst.primCount;
            }
        }
        for (int o = 0; o < 8; ++o) {
            if (node.child[o] != kNoProxyNode)
                stack.push_back(node.child[o]);
        }
    }
    out.drawCount = out.proxyDraws + uint32_t(out.exact.size());
}

void ProxyHierarchy::explainExact(const ProxyCut &cut, const float *view,
                                  const float *proj, float viewportHeightPx,
                                  float tolerancePx,
                                  const std::vector<ProxyNodeCost> *costs,
                                  ProxyExactBreakdown &out) const
{
    out = ProxyExactBreakdown();
    if (!view || !proj || viewportHeightPx <= 0.0f)
        return;

    double levelWeight = 0.0;
    for (size_t i = 0; i < cut.exact.size(); ++i) {
        const uint32_t idx = cut.exact[i];
        if (idx >= instancedata.size())
            continue;
        const ProxyInstance &inst = instancedata[idx];
        const int ni = i < cut.exactNode.size() ? cut.exactNode[i]
                                                : kNoProxyNode;

        // The node's reason, asked in the same order the descent asks
        // it: a node without a proxy never reaches the tolerance test,
        // and one below minMerge never reaches it either.
        ProxyExactBreakdown::Reason reason = ProxyExactBreakdown::Resolvable;
        if (ni != kNoProxyNode && size_t(ni) < nodedata.size()) {
            const ProxyNode &node = nodedata[size_t(ni)];
            const ProxyNodeCost *cost =
                costs && size_t(ni) < costs->size() ? &(*costs)[size_t(ni)]
                                                    : nullptr;
            // minMerge first, though the descent tests it last: a
            // node below it has no proxy *because* of it, and reading
            // that as a generation gap would blame the store for
            // obeying the partition's own floor.
            if (node.subtreeCount < parameters.minMerge)
                reason = ProxyExactBreakdown::TooFewMembers;
            else if (costs && (!cost || cost->errorRatio < 0.0f))
                reason = ProxyExactBreakdown::NoProxy;
            if (node.level > out.maxLevel)
                out.maxLevel = node.level;
            levelWeight += double(node.level) * double(inst.primCount);
        }

        // How large the instance is on its own. Inside the box counts
        // as maximal rather than as unknown: a camera within an
        // instance's bounds is the near field by any reading.
        const BoxSight sight = sightBounds(inst.bboxMin, inst.bboxMax,
                                           view, proj, viewportHeightPx);
        const float px = sight.what == BoxSight::Inside
            ? viewportHeightPx
            : sight.diagPx;
        int bin = ProxyExactBreakdown::SizeBins - 1;
        if (tolerancePx > 0.0f) {
            const float ratio = px / tolerancePx;
            bin = ratio <= 1.0f ? 0 : (ratio <= 4.0f ? 1 : (ratio <= 16.0f ? 2 : 3));
        }

        const auto add = [&](ProxyExactBreakdown::Bin &b) {
            b.instances += 1;
            b.prims += inst.primCount;
        };
        add(out.bins[reason][bin]);
        add(out.byReason[reason]);
        add(out.bySize[bin]);
        add(out.total);
    }
    if (out.total.prims)
        out.meanLevel = levelWeight / double(out.total.prims);
}

void ProxyHierarchy::markSubtree(int node, std::vector<uint8_t> &mark,
                                 uint8_t bit, uint32_t &clashes) const
{
    const ProxyNode &n = nodedata[size_t(node)];
    for (uint32_t r = 0; r < n.residentCount; ++r) {
        const uint32_t idx = residentdata[n.residentFirst + r];
        // What a proxy covers is what generation would accept from it;
        // an edge below a stopped node is drawn by the cut itself and
        // is marked there, not here (11.1g).
        if (!instancedata[idx].mergeable)
            continue;
        uint8_t &m = mark[idx];
        if (m)
            ++clashes;
        m = uint8_t(m | bit);
    }
    for (int o = 0; o < 8; ++o) {
        if (n.child[o] != kNoProxyNode)
            markSubtree(n.child[o], mark, bit, clashes);
    }
}

bool ProxyHierarchy::verifyCut(const ProxyCut &cut, std::string *why) const
{
    std::vector<uint8_t> mark(instancedata.size(), 0);
    uint32_t clashes = 0;
    for (int ni : cut.proxyNodes)
        markSubtree(ni, mark, 1, clashes);
    for (uint32_t idx : cut.exact) {
        if (idx >= mark.size()) {
            if (why)
                *why = "exact list names an instance that does not exist";
            return false;
        }
        if (mark[idx])
            ++clashes;
        mark[idx] = uint8_t(mark[idx] | 2);
    }
    if (clashes) {
        if (why) {
            *why = "an instance is drawn twice: "
                + std::to_string(clashes) + " overlap(s) between a proxy "
                  "and either another proxy or an exact draw";
        }
        return false;
    }
    uint32_t unmarked = 0;
    for (uint8_t m : mark) {
        if (!m)
            ++unmarked;
    }
    if (unmarked != cut.culledInstances) {
        if (why) {
            *why = "instances drawn by nobody (" + std::to_string(unmarked)
                + ") do not match the reported cull count ("
                + std::to_string(cut.culledInstances) + ")";
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// What phase 1 reports
// ---------------------------------------------------------------------

ProxyHierarchy::Stats ProxyHierarchy::stats() const
{
    Stats st;
    st.instances = uint32_t(instancedata.size());
    st.nodes = uint32_t(nodedata.size());
    if (rootnode != kNoProxyNode)
        st.distinctBuckets = nodedata[size_t(rootnode)].bucketCount;
    for (const auto &node : nodedata) {
        if (node.level >= st.byLevel.size())
            st.byLevel.resize(node.level + 1);
        LevelStats &ls = st.byLevel[node.level];
        ls.nodes += 1;
        ls.residents += node.residentCount;
        ls.subtreeMax = std::max(ls.subtreeMax, node.subtreeCount);
        ls.bucketsMax = std::max(ls.bucketsMax, node.bucketCount);
        ls.bucketsMean += double(node.bucketCount);
        st.depth = std::max(st.depth, node.level);
    }
    for (auto &ls : st.byLevel) {
        if (ls.nodes)
            ls.bucketsMean /= double(ls.nodes);
    }
    return st;
}
