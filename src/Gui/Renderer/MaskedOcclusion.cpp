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

#include "MaskedOcclusion.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>

using namespace Render;

namespace
{

/// A vertex in clip space, kept in double through clipping and
/// projection.
///
/// ⚠️ Double, and not because the depths need the precision — they do
/// not. A triangle straddling the near plane projects to screen
/// coordinates in the millions, and the edge equations below are
/// differences of products of those coordinates. In float the
/// cancellation puts the error at whole pixels, which shows up as
/// coverage bits set in blocks the triangle never reached: a surface
/// claimed where there is none, which is the one direction this file may
/// not be wrong in. The rasterizer is scalar for now anyway (see the
/// note at the end of the file), so the cost is not what decides this.
struct CVert {
    double x, y, z, w;
};

/// GL-layout (column-major) 4x4 product, \a o = \a a * \a b.
void mat4Mul(float *o, const float *a, const float *b)
{
    float t[16];
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += a[k * 4 + r] * b[c * 4 + k];
            t[c * 4 + r] = s;
        }
    }
    std::memcpy(o, t, sizeof(t));
}

void transformPoint(CVert &out, const float *m, double x, double y, double z)
{
    out.x = m[0] * x + m[4] * y + m[8] * z + m[12];
    out.y = m[1] * x + m[5] * y + m[9] * z + m[13];
    out.z = m[2] * x + m[6] * y + m[10] * z + m[14];
    out.w = m[3] * x + m[7] * y + m[11] * z + m[15];
}

/// Sutherland-Hodgman against one clip-space half space
/// \a pa x + \a pb y + \a pc z + \a pd w >= 0.
int clipAgainst(const CVert *in, int n, CVert *out, double pa, double pb,
                double pc, double pd)
{
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const CVert &a = in[i];
        const CVert &b = in[(i + 1) % n];
        const double da = pa * a.x + pb * a.y + pc * a.z + pd * a.w;
        const double db = pa * b.x + pb * b.y + pc * b.z + pd * b.w;
        if (da >= 0.0)
            out[m++] = a;
        if ((da >= 0.0) != (db >= 0.0)) {
            const double t = da / (da - db);
            out[m].x = a.x + (b.x - a.x) * t;
            out[m].y = a.y + (b.y - a.y) * t;
            out[m].z = a.z + (b.z - a.z) * t;
            out[m].w = a.w + (b.w - a.w) * t;
            ++m;
        }
    }
    return m;
}

/// Smallest w a perspective divide is allowed to see. Not a near plane —
/// the near plane clip below is separate and exact; this only keeps the
/// division finite for the degenerate case of a projection with no near
/// plane at all.
const double kMinW = 1e-7;

}  // namespace

// ---------------------------------------------------------------------
// Buffer management
// ---------------------------------------------------------------------

void MaskedDepth::resize(int widthPx, int heightPx)
{
    if (widthPx <= 0 || heightPx <= 0) {
        blocks.clear();
        bufw = bufh = bw = bh = 0;
        return;
    }
    bw = (widthPx + BlockW - 1) / BlockW;
    bh = (heightPx + BlockH - 1) / BlockH;
    bufw = bw * BlockW;
    bufh = bh * BlockH;
    blocks.assign(size_t(bw) * size_t(bh), Block());
    clear();
}

void MaskedDepth::clear()
{
    // ⚠️ The cleared floor is -FLT_MAX and not zero. Zero is a perfectly
    // ordinary depth in the orthographic convention below (and the exact
    // depth of the far plane in the perspective one), so a zero floor
    // would let an empty block claim to occlude everything behind the
    // camera's far plane. -FLT_MAX is the only value that means "nothing
    // here", which is what an empty block has to mean.
    Block b;
    b.z0 = -FLT_MAX;
    b.z1 = -FLT_MAX;
    b.mask = 0;
    std::fill(blocks.begin(), blocks.end(), b);
}

void MaskedDepth::setCamera(const float *view, const float *proj,
                            bool homogeneousDepth)
{
    mat4Mul(viewproj, proj, view);
    homogeneous = homogeneousDepth;
    orthographic = proj[15] != 0.0f;
}

float MaskedDepth::floorOf(const Block &b)
{
    if (b.mask == 0)
        return b.z0;
    if (b.mask == 0xFFFFFFFFu)
        return b.z1;
    return b.z0 < b.z1 ? b.z0 : b.z1;
}

float MaskedDepth::pixelDepth(int x, int y) const
{
    if (blocks.empty() || x < 0 || y < 0 || x >= bufw || y >= bufh)
        return -FLT_MAX;
    const Block &b = blocks[size_t(y / BlockH) * size_t(bw) + size_t(x / BlockW)];
    const uint32_t bit = 1u << ((y % BlockH) * BlockW + (x % BlockW));
    return (b.mask & bit) ? b.z1 : b.z0;
}

float MaskedDepth::blockFloor(int x, int y) const
{
    if (blocks.empty() || x < 0 || y < 0 || x >= bufw || y >= bufh)
        return -FLT_MAX;
    return floorOf(blocks[size_t(y / BlockH) * size_t(bw) + size_t(x / BlockW)]);
}

// ---------------------------------------------------------------------
// The block update — the paper's heuristic, and the invariant it keeps
// ---------------------------------------------------------------------

void MaskedDepth::updateBlock(int bx, int by, uint32_t cov, float ztri)
{
    Block &b = blocks[size_t(by) * size_t(bw) + size_t(bx)];
    ++framestats.blocksTouched;

    // The floor already stands in front of the incoming surface
    // everywhere in the block, so there is nothing this triangle can
    // add. Not merely an optimization: it is also what guarantees
    // z1 > z0 below, which is what lets the query read the floor off z0
    // alone.
    if (ztri <= b.z0)
        return;

    const uint32_t Full = 0xFFFFFFFFu;

    // ⭐ The merge decision, and the whole reason two layers beat one
    // conservative minimum. Layer 1 is kept only while the incoming
    // surface is *nearer to it* than it is to the floor
    // (z1 - z0 < ztri - z1, written without the subtraction). When the
    // incoming surface is much nearer, layer 1 is the stale one: it is
    // dropped to the floor and layer 1 restarted from the new surface.
    // When it is close, the two are the same wall seen through two
    // triangles and merging them keeps the coverage that will eventually
    // complete the block.
    //
    // Dropping layer 1 forgets real occlusion, and that is allowed: the
    // pixels it held fall back to the floor, which is never nearer than
    // the truth. Every branch here may lose culling; none may invent it.
    bool discard = b.mask == 0 || cov == Full
                   || (2.0f * b.z1 < ztri + b.z0);

    if (discard) {
        b.mask = cov;
        b.z1 = ztri;
    }
    else {
        b.mask |= cov;
        if (ztri < b.z1)
            b.z1 = ztri;
    }

    // Coverage completed: every pixel in the block is now behind layer 1,
    // so layer 1 becomes the floor and the mask is free again. This is
    // the promotion that makes a large occluder cost one value per block
    // instead of a mask.
    if (b.mask == Full) {
        b.z0 = b.z1;
        b.mask = 0;
        b.z1 = -FLT_MAX;
    }
    ++framestats.blocksUpdated;
}

// ---------------------------------------------------------------------
// Rasterization
// ---------------------------------------------------------------------

void MaskedDepth::rasterTri(const double *X, const double *Y, const double *D)
{
    double ax = X[1] - X[0], ay = Y[1] - Y[0];
    double bx = X[2] - X[0], by = Y[2] - Y[0];
    double det = ax * by - ay * bx;

    int i1 = 1, i2 = 2;
    if (det < 0.0) {
        // Back-facing in screen space. Flipping rather than discarding is
        // the two-sided policy argued in the header: a CAD tessellation's
        // winding is not to be trusted, and a wrongly discarded occluder
        // is a hole nothing reports.
        if (!twosided) {
            ++framestats.trianglesCulled;
            return;
        }
        i1 = 2;
        i2 = 1;
        std::swap(ax, bx);
        std::swap(ay, by);
        det = -det;
    }
    if (!(det > 0.0)) {
        ++framestats.trianglesCulled;  // degenerate, or NaN
        return;
    }

    const double x0 = X[0], y0 = Y[0], d0 = D[0];
    const double x[3] = {X[0], X[i1], X[i2]};
    const double y[3] = {Y[0], Y[i1], Y[i2]};
    const double d1 = D[i1], d2 = D[i2];

    // Depth is affine in screen space by construction (see projection
    // below), so three samples fix it exactly.
    const double dzdx = ((d1 - d0) * by - (d2 - d0) * ay) / det;
    const double dzdy = ((d2 - d0) * ax - (d1 - d0) * bx) / det;
    const double dmax = std::max(d0, std::max(d1, d2));

    // Edge half spaces, inside positive: for the counter-clockwise
    // winding established above, a point is inside when it is left of
    // every directed edge.
    double eA[3], eB[3], eC[3];
    for (int k = 0; k < 3; ++k) {
        const int j = (k + 1) % 3;
        eA[k] = -(y[j] - y[k]);
        eB[k] = x[j] - x[k];
        eC[k] = -eA[k] * x[k] - eB[k] * y[k];
    }

    double minx = std::min(x[0], std::min(x[1], x[2]));
    double maxx = std::max(x[0], std::max(x[1], x[2]));
    double miny = std::min(y[0], std::min(y[1], y[2]));
    double maxy = std::max(y[0], std::max(y[1], y[2]));
    // Clamp before the cast: a triangle clipped at the near plane
    // projects to coordinates a long way outside int range.
    minx = std::max(minx, -1.0);
    maxx = std::min(maxx, double(bufw) + 1.0);
    miny = std::max(miny, -1.0);
    maxy = std::min(maxy, double(bufh) + 1.0);

    int px0 = std::max(0, int(std::ceil(minx - 0.5)));
    int px1 = std::min(bufw - 1, int(std::floor(maxx - 0.5)));
    int py0 = std::max(0, int(std::ceil(miny - 0.5)));
    int py1 = std::min(bufh - 1, int(std::floor(maxy - 0.5)));
    if (px0 > px1 || py0 > py1) {
        ++framestats.trianglesCulled;
        return;
    }
    ++framestats.trianglesDrawn;

    const int by0 = py0 / BlockH;
    const int by1 = py1 / BlockH;
    for (int byi = by0; byi <= by1; ++byi) {
        // Solve the three half spaces for x on each of the block's rows.
        // Exact for a convex polygon, and cheaper than testing 32 pixel
        // centres against three edges each.
        int rowLo[BlockH], rowHi[BlockH];
        int uLo = bufw, uHi = -1;
        for (int r = 0; r < BlockH; ++r) {
            rowLo[r] = 1;
            rowHi[r] = 0;
            const int py = byi * BlockH + r;
            if (py < py0 || py > py1)
                continue;
            const double yc = py + 0.5;
            double lo = -1.0, hi = double(bufw) + 1.0;
            bool inside = true;
            for (int k = 0; k < 3; ++k) {
                const double rhs = -(eB[k] * yc + eC[k]);
                if (eA[k] > 0.0) {
                    const double v = rhs / eA[k];
                    if (v > lo)
                        lo = v;
                }
                else if (eA[k] < 0.0) {
                    const double v = rhs / eA[k];
                    if (v < hi)
                        hi = v;
                }
                else if (rhs > 0.0) {
                    // A horizontal edge with the row on its outside.
                    inside = false;
                    break;
                }
            }
            if (!inside || lo > hi)
                continue;
            int xa = std::max(px0, int(std::ceil(lo - 0.5)));
            int xb = std::min(px1, int(std::floor(hi - 0.5)));
            if (xa > xb)
                continue;
            rowLo[r] = xa;
            rowHi[r] = xb;
            uLo = std::min(uLo, xa);
            uHi = std::max(uHi, xb);
        }
        if (uHi < uLo)
            continue;

        const int bx0 = uLo / BlockW;
        const int bx1 = uHi / BlockW;
        for (int bxi = bx0; bxi <= bx1; ++bxi) {
            const int c0 = bxi * BlockW;
            uint32_t mask = 0;
            for (int r = 0; r < BlockH; ++r) {
                if (rowLo[r] > rowHi[r])
                    continue;
                const int a = std::max(rowLo[r], c0);
                const int b = std::min(rowHi[r], c0 + BlockW - 1);
                if (a > b)
                    continue;
                const uint32_t bits = ((1u << (b - a + 1)) - 1u) << (a - c0);
                mask |= bits << (r * BlockW);
            }
            if (!mask)
                continue;

            // The conservative depth of the triangle over this block:
            // the affine minimum, taken at whichever corner of the
            // block's sample grid the gradient points away from. Using
            // the block rather than the covered pixels only ever makes
            // it farther, which is the safe direction.
            const double cx = (dzdx > 0.0) ? (c0 + 0.5) : (c0 + BlockW - 0.5);
            const double cy = (dzdy > 0.0) ? (byi * BlockH + 0.5)
                                           : (byi * BlockH + BlockH - 0.5);
            double zb = d0 + dzdx * (cx - x0) + dzdy * (cy - y0);
            if (zb > dmax)
                zb = dmax;  // never extrapolate nearer than the triangle is
            updateBlock(bxi, byi, mask, float(zb));
        }
    }
}

void MaskedDepth::rasterize(const float *positions, size_t stride,
                            size_t vertexCount, const uint32_t *indices,
                            size_t indexCount, const float *model)
{
    if (blocks.empty() || !positions || !indices || indexCount < 3)
        return;
    if (stride == 0)
        stride = 3 * sizeof(float);

    float mvp[16];
    if (model)
        mat4Mul(mvp, viewproj, model);
    else
        std::memcpy(mvp, viewproj, sizeof(mvp));

    const char *base = reinterpret_cast<const char *>(positions);
    const size_t tris = indexCount / 3;
    for (size_t t = 0; t < tris; ++t) {
        const uint32_t ia = indices[t * 3 + 0];
        const uint32_t ib = indices[t * 3 + 1];
        const uint32_t ic = indices[t * 3 + 2];
        if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) {
            ++framestats.trianglesIn;
            ++framestats.trianglesCulled;
            continue;
        }
        const float *pa = reinterpret_cast<const float *>(base + ia * stride);
        const float *pb = reinterpret_cast<const float *>(base + ib * stride);
        const float *pc = reinterpret_cast<const float *>(base + ic * stride);
        emitTriangle(pa, pb, pc, mvp);
    }
}

void MaskedDepth::rasterize(const float *positions, size_t stride,
                            size_t vertexCount, const int32_t *indices,
                            size_t indexCount, const float *model)
{
    // A MeshData's indices are signed and never negative in practice.
    // Reading them as unsigned turns any that are into a very large
    // value, which the bounds check in the call below rejects — so the
    // cast cannot produce a dereference the signed form would have
    // avoided.
    rasterize(positions, stride, vertexCount,
              reinterpret_cast<const uint32_t *>(indices), indexCount, model);
}

void MaskedDepth::rasterize(const float *positions, size_t stride,
                            size_t vertexCount, const float *model)
{
    if (blocks.empty() || !positions || vertexCount < 3)
        return;
    if (stride == 0)
        stride = 3 * sizeof(float);

    float mvp[16];
    if (model)
        mat4Mul(mvp, viewproj, model);
    else
        std::memcpy(mvp, viewproj, sizeof(mvp));

    const char *base = reinterpret_cast<const char *>(positions);
    const size_t tris = vertexCount / 3;
    for (size_t t = 0; t < tris; ++t) {
        const float *pa = reinterpret_cast<const float *>(base + (t * 3 + 0) * stride);
        const float *pb = reinterpret_cast<const float *>(base + (t * 3 + 1) * stride);
        const float *pc = reinterpret_cast<const float *>(base + (t * 3 + 2) * stride);
        emitTriangle(pa, pb, pc, mvp);
    }
}

void MaskedDepth::emitTriangle(const float *pa, const float *pb,
                               const float *pc, const float *mvp)
{
    ++framestats.trianglesIn;

    CVert poly[8], tmp[8];
    transformPoint(poly[0], mvp, pa[0], pa[1], pa[2]);
    transformPoint(poly[1], mvp, pb[0], pb[1], pb[2]);
    transformPoint(poly[2], mvp, pc[0], pc[1], pc[2]);
    int n = 3;

    // The near plane, exactly. A triangle crossing it must be cut and not
    // dropped: dropping it is a hole in the occluder, and the geometry
    // nearest the camera is precisely the geometry that occludes most.
    const bool crossesNear =
            homogeneous
                    ? ((poly[0].z + poly[0].w < 0.0) || (poly[1].z + poly[1].w < 0.0)
                       || (poly[2].z + poly[2].w < 0.0))
                    : (poly[0].z < 0.0 || poly[1].z < 0.0 || poly[2].z < 0.0);
    if (crossesNear) {
        if (homogeneous)
            n = clipAgainst(poly, n, tmp, 0.0, 0.0, 1.0, 1.0);
        else
            n = clipAgainst(poly, n, tmp, 0.0, 0.0, 1.0, 0.0);
        if (n < 3) {
            ++framestats.trianglesCulled;
            return;
        }
        std::memcpy(poly, tmp, size_t(n) * sizeof(CVert));
        ++framestats.trianglesClipped;
    }
    // Keep the divide finite whatever the projection. Orthographic w is
    // 1, so this only ever fires on a perspective matrix with no usable
    // near plane.
    {
        bool small = false;
        for (int i = 0; i < n; ++i)
            small = small || poly[i].w < kMinW;
        if (small) {
            n = clipAgainst(poly, n, tmp, 0.0, 0.0, 0.0, 1.0);
            // clipAgainst's plane is w >= 0; nudge the survivors instead
            // of solving for w >= kMinW, which would move vertices.
            if (n < 3) {
                ++framestats.trianglesCulled;
                return;
            }
            std::memcpy(poly, tmp, size_t(n) * sizeof(CVert));
            for (int i = 0; i < n; ++i)
                if (poly[i].w < kMinW)
                    poly[i].w = kMinW;
        }
    }

    // Project. ⭐ Both depth conventions below are *affine in screen
    // space*, which is what lets rasterTri interpolate them with a plane
    // equation: 1/w is affine under a perspective projection, and NDC z
    // is affine under an orthographic one. Larger is nearer in both, so
    // the rest of the file never asks which projection it is looking at.
    double sx[8], sy[8], sd[8];
    for (int i = 0; i < n; ++i) {
        const double iw = 1.0 / poly[i].w;
        sx[i] = (poly[i].x * iw * 0.5 + 0.5) * bufw;
        sy[i] = (poly[i].y * iw * 0.5 + 0.5) * bufh;
        sd[i] = orthographic ? -(poly[i].z * iw) : iw;
    }

    for (int i = 2; i < n; ++i) {
        const double tx[3] = {sx[0], sx[i - 1], sx[i]};
        const double ty[3] = {sy[0], sy[i - 1], sy[i]};
        const double td[3] = {sd[0], sd[i - 1], sd[i]};
        rasterTri(tx, ty, td);
    }
}

// ---------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------

bool MaskedDepth::projectBox(const float *bboxMin, const float *bboxMax,
                             float *rect, float *depthNear) const
{
    if (blocks.empty())
        return false;

    double rx0 = DBL_MAX, ry0 = DBL_MAX, rx1 = -DBL_MAX, ry1 = -DBL_MAX;
    double dnear = -DBL_MAX;
    for (int c = 0; c < 8; ++c) {
        // The bit-encoded corner order shared with occlusionBoxIndices:
        // x from bit 0, y from bit 1, z from bit 2.
        const double px = (c & 1) ? bboxMax[0] : bboxMin[0];
        const double py = (c & 2) ? bboxMax[1] : bboxMin[1];
        const double pz = (c & 4) ? bboxMax[2] : bboxMin[2];
        CVert v;
        transformPoint(v, viewproj, px, py, pz);
        const bool behind = homogeneous ? (v.z + v.w < 0.0) : (v.z < 0.0);
        if (behind || v.w < kMinW)
            return false;  // no bounded rect; the caller must draw it
        const double iw = 1.0 / v.w;
        const double x = (v.x * iw * 0.5 + 0.5) * bufw;
        const double y = (v.y * iw * 0.5 + 0.5) * bufh;
        // Both conventions are monotonic in the same direction as the
        // rasterizer's, and both are affine in *world* space along the
        // box, so the extremum over the box is attained at a corner.
        const double d = orthographic ? -(v.z * iw) : iw;
        rx0 = std::min(rx0, x);
        rx1 = std::max(rx1, x);
        ry0 = std::min(ry0, y);
        ry1 = std::max(ry1, y);
        dnear = std::max(dnear, d);
    }
    rect[0] = float(rx0);
    rect[1] = float(ry0);
    rect[2] = float(rx1);
    rect[3] = float(ry1);
    *depthNear = float(dnear);
    return true;
}

OccludeAnswer MaskedDepth::testBox(const float *bboxMin,
                                   const float *bboxMax) const
{
    if (blocks.empty())
        return OccludeAnswer::Visible;
    if (bboxMin[0] > bboxMax[0] || bboxMin[1] > bboxMax[1]
        || bboxMin[2] > bboxMax[2])
        return OccludeAnswer::Visible;  // no bounds is not an answer

    float rect[4];
    float dnear = 0.0f;
    if (!projectBox(bboxMin, bboxMax, rect, &dnear)) {
        ++framestats.queriesNearPlane;
        return OccludeAnswer::Visible;
    }
    return testRect(rect[0], rect[1], rect[2], rect[3], dnear);
}

OccludeAnswer MaskedDepth::testRect(float x0, float y0, float x1, float y1,
                                    float depthNear) const
{
    if (blocks.empty())
        return OccludeAnswer::Visible;
    ++framestats.queries;

    // Every pixel the rect *touches* has to be checked, not every pixel
    // whose centre it contains: a rect that misses one pixel centre still
    // shows through it, and a query that skipped that pixel would report
    // hidden for something on screen.
    int px0 = int(std::floor(x0));
    int px1 = int(std::ceil(x1)) - 1;
    int py0 = int(std::floor(y0));
    int py1 = int(std::ceil(y1)) - 1;
    if (px1 < px0)
        px1 = px0;
    if (py1 < py0)
        py1 = py0;
    if (px1 < 0 || py1 < 0 || px0 >= bufw || py0 >= bufh) {
        ++framestats.queriesOffscreen;
        return OccludeAnswer::Offscreen;
    }
    // Clipping to the buffer rather than refusing: the part of the box
    // outside the viewport cannot be seen either, so what survives here
    // is the whole of what a viewer could see of it.
    px0 = std::max(px0, 0);
    py0 = std::max(py0, 0);
    px1 = std::min(px1, bufw - 1);
    py1 = std::min(py1, bufh - 1);

    const int bx0 = px0 / BlockW, bx1 = px1 / BlockW;
    const int by0 = py0 / BlockH, by1 = py1 / BlockH;
    for (int byi = by0; byi <= by1; ++byi) {
        const Block *row = &blocks[size_t(byi) * size_t(bw)];
        for (int bxi = bx0; bxi <= bx1; ++bxi) {
            // ⭐ Strictly nearer, so a coincident surface answers
            // visible. The tie a node's own geometry creates against its
            // own bounding box is the one §12.6 lost, and losing it is
            // what deleted geometry that was on screen.
            if (floorOf(row[bxi]) <= depthNear)
                return OccludeAnswer::Visible;
        }
    }
    ++framestats.queriesOccluded;
    return OccludeAnswer::Occluded;
}

// ---------------------------------------------------------------------
// The occluder pass, and the walk that spends it
// ---------------------------------------------------------------------

namespace
{

/// Whether a draw contributes to the depth the eye passes leave behind.
/// Only such a draw can hide anything, and rasterizing anything else is
/// budget spent on a surface that is not there.
bool occludes(const DrawCall &d)
{
    if (d.material.type != Material::Triangle)
        return false;
    if (!d.material.depthwrite || !d.material.depthtest)
        return false;
    if (d.material.transparent || d.material.ontop)
        return false;
    if (!d.mesh || !d.mesh->positions || !d.mesh->triangleIndices
        || d.mesh->numTriangleIndices < 3 || d.mesh->numVertices < 3)
        return false;
    // ⚠️ A stand-in is a box drawn where the real mesh has not arrived
    // (docs/SceneStreaming.md §6). It *does* write depth, so it really
    // does occlude the frame it appears in — but it is bigger than the
    // shape it replaces, and treating it as an occluder would hide
    // geometry behind a surface that will shrink when the real mesh
    // lands. Under-culling for the few frames a stand-in is up is the
    // cheaper mistake.
    if (d.standIn)
        return false;
    return d.bboxMin[0] <= d.bboxMax[0] && d.bboxMin[1] <= d.bboxMax[1]
            && d.bboxMin[2] <= d.bboxMax[2];
}

/// The index range a draw covers, resolved from its part selection.
void indexRange(const DrawCall &d, size_t &first, size_t &count)
{
    const size_t total = size_t(d.mesh->numTriangleIndices);
    first = size_t(std::max(0, d.indexStart));
    count = d.indexCount > 0 ? size_t(d.indexCount) : total;
    if (first > total)
        first = total;
    if (first + count > total)
        count = total - first;
}

float millisSince(const std::chrono::steady_clock::time_point &t0)
{
    const auto dt = std::chrono::steady_clock::now() - t0;
    return float(std::chrono::duration<double, std::milli>(dt).count());
}

}  // namespace

void MaskedOccluderPass::build(const DrawCallList &draws, const float *view,
                               const float *proj, bool homogeneousDepth,
                               int viewportW, int viewportH)
{
    const auto t0 = std::chrono::steady_clock::now();
    framestats = MaskedCullStats();

    const int div = std::max(1, conf.resolutionDivisor);
    buffer.resize(viewportW / div, viewportH / div);
    if (buffer.empty())
        return;
    buffer.clear();
    buffer.resetStats();
    buffer.setCamera(view, proj, homogeneousDepth);

    // Rank by how much of the screen the draw's bounds cover. Cheap, and
    // it is the right order for a budget: a triangle in a large near
    // occluder hides more than a triangle in a small far one, and what
    // the budget drops is therefore what mattered least.
    const float vh = float(viewportH);
    ranking.clear();
    for (size_t i = 0; i < draws.size(); ++i) {
        const DrawCall &d = draws[i];
        if (!occludes(d))
            continue;
        const BoxSight sight =
                sightBounds(d.bboxMin, d.bboxMax, view, proj, vh);
        if (sight.what == BoxSight::Offscreen || sight.what == BoxSight::Empty)
            continue;
        // A box the camera is inside has no meaningful projected size
        // and is exactly the sort of thing that occludes most, so it
        // goes to the front rather than being measured.
        const float px = sight.what == BoxSight::Inside ? FLT_MAX
                                                        : sight.diagPx;
        if (px < conf.minOccluderPx)
            continue;
        ranking.emplace_back(px, uint32_t(i));
    }
    framestats.occluderCandidates = uint32_t(ranking.size());
    std::sort(ranking.begin(), ranking.end(),
              [](const std::pair<float, uint32_t> &a,
                 const std::pair<float, uint32_t> &b) {
                  return a.first > b.first;
              });

    uint32_t budget = conf.triangleBudget;
    const size_t limit = std::min<size_t>(ranking.size(), conf.maxOccluders);
    for (size_t r = 0; r < ranking.size(); ++r) {
        if (r >= limit || budget == 0) {
            ++framestats.occludersDropped;
            continue;
        }
        const DrawCall &d = draws[ranking[r].second];
        size_t first = 0, count = 0;
        indexRange(d, first, count);
        const uint32_t tris = uint32_t(count / 3);
        if (tris == 0)
            continue;
        if (tris > budget) {
            // ⚠️ Truncating a mesh mid-list leaves a partial surface,
            // which is a *hole*: the remaining triangles still occlude
            // correctly, but nothing behind the missing part is hidden.
            // That is under-culling, so it is allowed — and it is
            // counted, because a budget that quietly halves an occluder
            // reads as a scene that does not occlude.
            ++framestats.occludersDropped;
            continue;
        }
        buffer.rasterize(d.mesh->positions, 0, size_t(d.mesh->numVertices),
                         d.mesh->triangleIndices + first, count,
                         d.identity ? nullptr : d.model);
        budget -= tris;
        ++framestats.occluderDraws;
        framestats.occluderTriangles += tris;
    }
    framestats.rasterMs = millisSince(t0);
}

void MaskedOccluderPass::cull(const ProxyHierarchy &index, const float *view,
                              const float *proj, float viewportHeightPx,
                              std::vector<uint8_t> &cullMask,
                              std::vector<int32_t> *cullOwner)
{
    const auto t0 = std::chrono::steady_clock::now();
    if (cullOwner)
        cullOwner->assign(cullMask.size(), -1);
    if (buffer.empty() || index.empty() || index.root() == kNoProxyNode)
        return;

    const auto &nodes = index.nodes();
    const auto &residents = index.residents();
    const auto &instances = index.instances();
    const int root = index.root();

    auto markSubtree = [&](int from) {
        // Attributed to the node the verdict was taken at, not to the
        // descendant the instance lives in: that node is the one whose
        // box was tested, and the only one there is evidence about.
        const int32_t owner = int32_t(from);
        walkstack.clear();
        walkstack.push_back(from);
        while (!walkstack.empty()) {
            const int cur = walkstack.back();
            walkstack.pop_back();
            const ProxyNode &n = nodes[size_t(cur)];
            for (uint32_t r = 0; r < n.residentCount; ++r) {
                const uint32_t inst = residents[n.residentFirst + r];
                const uint32_t row = instances[inst].drawIndex;
                if (row < cullMask.size())
                    cullMask[row] = 1;
                if (cullOwner && row < cullOwner->size())
                    (*cullOwner)[row] = owner;
            }
            for (int c : n.child) {
                if (c != kNoProxyNode)
                    walkstack.push_back(c);
            }
        }
    };

    // ⭐ No padding. The hardware path needs two pad terms because its
    // box has to win a depth comparison against the very surface it
    // bounds; here a tie answers visible by construction (see
    // MaskedDepth::testRect) and the block floor is already a
    // conservative under-estimate of what is really there. Padding on
    // top of that would only cost culling.
    std::vector<int> &descend = descendstack;
    descend.clear();
    descend.push_back(root);
    while (!descend.empty()) {
        const int node = descend.back();
        descend.pop_back();
        const ProxyNode &n = nodes[size_t(node)];
        ++framestats.nodesVisited;

        const BoxSight sight = sightBounds(n.contentMin, n.contentMax, view,
                                           proj, viewportHeightPx);
        if (sight.what == BoxSight::Offscreen) {
            // Left to the frustum mask the renderer already computed;
            // counted here only so the readout can separate what
            // occlusion contributed from what the frustum did.
            ++framestats.nodesOffscreen;
            framestats.offscreenInstances += n.subtreeCount;
            continue;
        }

        const OccludeAnswer answer =
                buffer.testBox(n.contentMin, n.contentMax);
        if (answer == OccludeAnswer::Offscreen) {
            ++framestats.nodesOffscreen;
            framestats.offscreenInstances += n.subtreeCount;
            continue;
        }
        ++framestats.nodesTested;
        if (answer == OccludeAnswer::Occluded) {
            if (node == root) {
                // Refused, not acted on: every occluder in the buffer is
                // inside this box, so its nearest corner stands in front
                // of all of them and it cannot be hidden by them. Acting
                // would blank the model; counting it makes the symptom
                // visible instead.
                ++framestats.rootRefused;
            }
            else {
                markSubtree(node);
                framestats.hiddenInstances += n.subtreeCount;
                ++framestats.nodesHidden;
                continue;
            }
        }

        framestats.drawnInstances += n.residentCount;
        for (int c : n.child) {
            if (c != kNoProxyNode)
                descend.push_back(c);
        }
    }
    framestats.nearExempt = buffer.stats().queriesNearPlane;
    framestats.walkMs = millisSince(t0);
}

// ⚠️ Deliberately scalar. The layout is the SIMD one — 8x4 blocks whose
// coverage is exactly one 32-bit word, four of which fit a 128-bit lane
// — so vectorizing is a rewrite of rasterTri's inner loops and of
// updateBlock across four blocks at once, not a change of structure or
// of results. It is not done yet because nothing has measured what this
// costs on the benchmark scene, and this workstream has been wrong twice
// about where its time goes (docs/FarFieldProxies.md §12.10). The
// measurement comes first; SIMD128 is available on every tier including
// WASM when it says so.
