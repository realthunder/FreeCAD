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
#include <thread>

#include "Simd4.h"

using namespace Render;

namespace
{

/// A vertex in clip space, kept in double through clipping and
/// projection.
///
/// WARNING: Double, and not because the depths need the precision -- they do
/// not. A triangle straddling the near plane projects to screen
/// coordinates in the millions, and the edge equations below are
/// differences of products of those coordinates. In float the
/// cancellation puts the error at whole pixels, which shows up as
/// coverage bits set in blocks the triangle never reached: a surface
/// claimed where there is none, which is the one direction this file may
/// not be wrong in.
///
/// KEY: The vector pre-pass below is float and does not weaken that, because
/// it never writes to the buffer. It only decides which triangles this
/// path does not have to look at, and the triangles it drops are the ones
/// it computed to cover no pixel at all. See `filterBatch`.
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

/// Smallest w a perspective divide is allowed to see. Not a near plane --
/// the near plane clip below is separate and exact; this only keeps the
/// division finite for the degenerate case of a projection with no near
/// plane at all.
const double kMinW = 1e-7;

// WARNING: There is deliberately no inward epsilon on the edges here, and
// the reason is worth recording because it looks like free safety and is
// not.
//
// The row solve below multiplies by a reciprocal computed once per
// triangle rather than dividing per row. That is not bit-identical to the
// division, so a pixel centre lying exactly on an edge can fall either
// way, and the tempting fix is to shrink every span by a hair first so
// that any rounding loses coverage rather than inventing it.
//
// It breaks the buffer. An occluder is a *mesh*, and its triangles meet
// along shared edges; shrinking every triangle opens a crack along every
// one of those seams. Measured: a two-triangle wall filling the viewport
// stopped occluding anything on the diagonal where its halves join, and
// six tests that had a box on that seam went from hidden to visible.
// Watertightness between adjacent triangles is not a detail of this
// rasterizer, it is the thing that makes a mesh a surface.
//
// The reciprocal is safe without it. A pixel centre on a *shared* edge is
// genuinely covered -- the surface is there -- so including it is right;
// on a *silhouette* edge the error is about 1e-9 pixels, far below the
// block granularity any query is answered at, and the block's stored
// depth is already a conservative under-estimate of the triangle's.

/// The same near-plane guard as kMinW, in the pre-pass's precision.
const float kMinWf = 1e-7f;

/// Screen coordinates beyond this are handed to the exact path.
///
/// KEY: This is what makes the float pre-pass tractable. The whole reason
/// the exact path is double is that a triangle clipped at the near plane
/// projects to coordinates in the millions, where float cancellation is
/// worth whole pixels; the pre-pass simply refuses those. It also keeps
/// every value inside the range where the floor in Simd4.h is exact
/// (2^23), and inside the range where the epsilon below is a believable
/// error bound. Measured on the benchmark scene, nothing reaches it:
/// `clipped 0` for the camera the audit runs at, which is what makes
/// refusing affordable.
const float kGuardPx = 1.0e6f;

/// How far a projected coordinate is assumed to be able to move between
/// the float pre-pass and the double path.
///
/// KEY: This is a *culling-quality* number and not a correctness one, and
/// the difference is the whole design. Too small and a triangle the
/// exact path would have drawn gets dropped -- which loses occlusion,
/// the safe direction, and only ever for triangles within a hundredth of
/// a pixel of covering nothing. Too large and sub-pixel triangles stop
/// being rejected and the pre-pass buys nothing, since a bbox grown by a
/// whole pixel almost always contains a pixel centre. Neither can put a
/// surface in the buffer where there is none, because the pre-pass does
/// not write to the buffer.
///
/// The relative term is scaled by the larger of the coordinate and the
/// buffer, so a model far from the world origin -- where the transform's
/// terms are large and cancel -- gets proportionally more room, and a
/// small test buffer still gets a sane floor.
const float kFilterEpsRel = 1.0e-5f;
const float kFilterEpsAbs = 1.0e-3f;

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
    // WARNING: The cleared floor is -FLT_MAX and not zero. Zero is a perfectly
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
// The block update -- the paper's heuristic, and the invariant it keeps
// ---------------------------------------------------------------------

void MaskedDepth::updateBlock(int bx, int by, uint32_t cov, float ztri)
{
    updateBlockAt(size_t(by) * size_t(bw) + size_t(bx), cov, ztri);
}

void MaskedDepth::merge(const MaskedDepth &other)
{
    mergeBlocks(other, 0, blocks.size());
    mergeStats(other);
}

void MaskedDepth::mergeBlocks(const MaskedDepth &other, size_t lo, size_t hi)
{
    if (blocks.size() != other.blocks.size())
        return;
    hi = std::min(hi, blocks.size());
    for (size_t i = lo; i < hi; ++i) {
        const Block &b = other.blocks[i];
        // Order matters, and this is the one that keeps the most: the
        // other buffer's floor first, then its nearer layer. Reversed,
        // a full-coverage floor update would promote and discard the
        // layer just merged. Both orders are safe -- every step is an
        // ordinary block update, and those can only ever move the stored
        // depth away from the viewer -- so what is at stake is culling,
        // not correctness.
        if (b.z0 > -FLT_MAX)
            applyBlock(blocks[i], 0xFFFFFFFFu, b.z0);
        if (b.mask != 0)
            applyBlock(blocks[i], b.mask, b.z1);
    }
}

void MaskedDepth::mergeStats(const MaskedDepth &other)
{
    // The rasterization counters belong to the frame, not to the worker
    // that happened to do the work. The query counters deliberately do
    // not merge: queries are asked of the merged buffer, never of a
    // worker's.
    framestats.trianglesIn += other.framestats.trianglesIn;
    framestats.trianglesClipped += other.framestats.trianglesClipped;
    framestats.trianglesDrawn += other.framestats.trianglesDrawn;
    framestats.trianglesOffBuffer += other.framestats.trianglesOffBuffer;
    framestats.trianglesSubPixel += other.framestats.trianglesSubPixel;
    framestats.trianglesBackFacing += other.framestats.trianglesBackFacing;
    framestats.trianglesDegenerate += other.framestats.trianglesDegenerate;
    framestats.trianglesBehind += other.framestats.trianglesBehind;
    framestats.trianglesFiltered += other.framestats.trianglesFiltered;
    framestats.trianglesGuarded += other.framestats.trianglesGuarded;
    framestats.blocksTouched += other.framestats.blocksTouched;
    framestats.blocksUpdated += other.framestats.blocksUpdated;
}

void MaskedDepth::updateBlockAt(size_t index, uint32_t cov, float ztri)
{
    ++framestats.blocksTouched;
    if (applyBlock(blocks[index], cov, ztri))
        ++framestats.blocksUpdated;
}

bool MaskedDepth::applyBlock(Block &b, uint32_t cov, float ztri)
{
    // The floor already stands in front of the incoming surface
    // everywhere in the block, so there is nothing this triangle can
    // add. Not merely an optimization: it is also what guarantees
    // z1 > z0 below, which is what lets the query read the floor off z0
    // alone.
    if (ztri <= b.z0)
        return false;

    const uint32_t Full = 0xFFFFFFFFu;

    // KEY: The merge decision, and the whole reason two layers beat one
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
    return true;
}

// ---------------------------------------------------------------------
// Rasterization
// ---------------------------------------------------------------------

void MaskedDepth::rasterTri(const double *X, const double *Y, const double *D)
{
    // KEY: The rejects come first, and that ordering is the measurement of
    // section 12.12 rather than a style preference. On the benchmark scene
    // 170684 of 249998 rasterized triangles never touch a pixel -- a
    // full-detail CAD tessellation is mostly triangles smaller than the
    // pixel grid -- so everything computed before this test is computed
    // for the two thirds that are about to be discarded. Nothing below
    // needs the winding, the depth gradients or the edge equations.
    double minx = std::min(X[0], std::min(X[1], X[2]));
    double maxx = std::max(X[0], std::max(X[1], X[2]));
    double miny = std::min(Y[0], std::min(Y[1], Y[2]));
    double maxy = std::max(Y[0], std::max(Y[1], Y[2]));
    if (!(maxx >= 0.0 && minx <= double(bufw) && maxy >= 0.0
          && miny <= double(bufh))) {
        ++framestats.trianglesOffBuffer;  // also catches NaN
        return;
    }
    // Clamp before the cast: a triangle clipped at the near plane
    // projects to coordinates a long way outside int range.
    minx = std::max(minx, -1.0);
    maxx = std::min(maxx, double(bufw) + 1.0);
    miny = std::max(miny, -1.0);
    maxy = std::min(maxy, double(bufh) + 1.0);

    const int px0 = std::max(0, int(std::ceil(minx - 0.5)));
    const int px1 = std::min(bufw - 1, int(std::floor(maxx - 0.5)));
    const int py0 = std::max(0, int(std::ceil(miny - 0.5)));
    const int py1 = std::min(bufh - 1, int(std::floor(maxy - 0.5)));
    if (px0 > px1 || py0 > py1) {
        // On the buffer, but not over any pixel centre.
        ++framestats.trianglesSubPixel;
        return;
    }

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
            ++framestats.trianglesBackFacing;
            return;
        }
        i1 = 2;
        i2 = 1;
        std::swap(ax, bx);
        std::swap(ay, by);
        det = -det;
    }
    if (!(det > 0.0)) {
        ++framestats.trianglesDegenerate;  // zero area, or NaN
        return;
    }
    ++framestats.trianglesDrawn;

    const double x0 = X[0], y0 = Y[0], d0 = D[0];
    const double x[3] = {X[0], X[i1], X[i2]};
    const double y[3] = {Y[0], Y[i1], Y[i2]};
    const double d1 = D[i1], d2 = D[i2];

    // Depth is affine in screen space by construction (see projection
    // below), so three samples fix it exactly.
    //
    // Still a division and deliberately so, unlike the edge reciprocals
    // below: there are two of these per *drawn* triangle against three
    // per pixel row for the edges, so reciprocating them would save
    // nothing measurable while making the stored depth differ from the
    // divided form by an ulp. In the one file whose whole claim is a
    // one-sided bound on stored depth, that is a bad trade at any speed.
    const double dzdx = ((d1 - d0) * by - (d2 - d0) * ay) / det;
    const double dzdy = ((d2 - d0) * ax - (d1 - d0) * bx) / det;
    const double dmax = std::max(d0, std::max(d1, d2));

    // Edge half spaces, inside positive: for the counter-clockwise
    // winding established above, a point is inside when it is left of
    // every directed edge.
    //
    // KEY: `eInvA` is why this loop exists separately from the row solve
    // below. Solving an edge for x on a row is a division, and doing it
    // in the row loop costs three per pixel row -- for a large occluder,
    // thousands per triangle. Reciprocated once here it is three per
    // triangle and a multiply per row. See the note beside kMinW for why
    // the substitution carries no inward epsilon to make it safe.
    double eA[3], eB[3], eC[3], eInvA[3];
    for (int k = 0; k < 3; ++k) {
        const int j = (k + 1) % 3;
        eA[k] = -(y[j] - y[k]);
        eB[k] = x[j] - x[k];
        eC[k] = -eA[k] * x[k] - eB[k] * y[k];
        eInvA[k] = eA[k] != 0.0 ? 1.0 / eA[k] : 0.0;
    }

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
                    const double v = rhs * eInvA[k];
                    if (v > lo)
                        lo = v;
                }
                else if (eA[k] < 0.0) {
                    const double v = rhs * eInvA[k];
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
    const float *batch[3 * Batch];
    int nb = 0;
    for (size_t t = 0; t < tris; ++t) {
        const uint32_t ia = indices[t * 3 + 0];
        const uint32_t ib = indices[t * 3 + 1];
        const uint32_t ic = indices[t * 3 + 2];
        if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) {
            ++framestats.trianglesIn;
            ++framestats.trianglesDegenerate;
            continue;
        }
        batch[nb * 3 + 0] = reinterpret_cast<const float *>(base + ia * stride);
        batch[nb * 3 + 1] = reinterpret_cast<const float *>(base + ib * stride);
        batch[nb * 3 + 2] = reinterpret_cast<const float *>(base + ic * stride);
        if (++nb == Batch) {
            emitBatch(batch, nb, mvp);
            nb = 0;
        }
    }
    if (nb)
        emitBatch(batch, nb, mvp);
}

void MaskedDepth::rasterize(const float *positions, size_t stride,
                            size_t vertexCount, const int32_t *indices,
                            size_t indexCount, const float *model)
{
    // A MeshData's indices are signed and never negative in practice.
    // Reading them as unsigned turns any that are into a very large
    // value, which the bounds check in the call below rejects -- so the
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
    const float *batch[3 * Batch];
    int nb = 0;
    for (size_t t = 0; t < tris; ++t) {
        for (int k = 0; k < 3; ++k)
            batch[nb * 3 + k] = reinterpret_cast<const float *>(
                    base + (t * 3 + size_t(k)) * stride);
        if (++nb == Batch) {
            emitBatch(batch, nb, mvp);
            nb = 0;
        }
    }
    if (nb)
        emitBatch(batch, nb, mvp);
}

// ---------------------------------------------------------------------
// The vector pre-pass
// ---------------------------------------------------------------------

void MaskedDepth::filterBatch(const float *const *v, const float *mvp,
                              uint8_t *verdict) const
{
    // Transposed into lanes: one array per component per vertex, four
    // triangles wide. The gather is scalar and there is no way around
    // it -- the vertices come from an index buffer -- but it buys the
    // whole of the rest of this function in one instruction per step.
    alignas(16) float vx[3][Batch], vy[3][Batch], vz[3][Batch];
    for (int i = 0; i < Batch; ++i) {
        for (int k = 0; k < 3; ++k) {
            const float *p = v[i * 3 + k];
            vx[k][i] = p[0];
            vy[k][i] = p[1];
            vz[k][i] = p[2];
        }
    }

    const F4 zero(0.0f);
    const F4 half(0.5f);
    const F4 one(1.0f);
    const F4 guard(kGuardPx);
    const F4 minw(kMinWf);
    // Written as an assignment and not `F4 fw(float(bufw))`, which the
    // grammar reads as a function declaration.
    const F4 fw = F4(float(bufw));
    const F4 fh = F4(float(bufh));

    // KEY: Everything is accumulated as a *reason to trust the lane*, never
    // as a reason to reject it, and that is what makes NaN safe here. A
    // NaN coordinate fails every comparison below, so it fails to be
    // trusted and goes to the exact path, which classifies it the way it
    // always did. Written the other way round -- as an OR of failures --
    // a NaN would pass every test and be silently judged.
    M4 trust = cmpGe(zero, zero);  // all lanes, to be narrowed
    F4 sx[3], sy[3];
    for (int k = 0; k < 3; ++k) {
        const F4 x = F4::load(vx[k]);
        const F4 y = F4::load(vy[k]);
        const F4 z = F4::load(vz[k]);
        const F4 cx = F4(mvp[0]) * x + F4(mvp[4]) * y + F4(mvp[8]) * z
                + F4(mvp[12]);
        const F4 cy = F4(mvp[1]) * x + F4(mvp[5]) * y + F4(mvp[9]) * z
                + F4(mvp[13]);
        const F4 cz = F4(mvp[2]) * x + F4(mvp[6]) * y + F4(mvp[10]) * z
                + F4(mvp[14]);
        const F4 cw = F4(mvp[3]) * x + F4(mvp[7]) * y + F4(mvp[11]) * z
                + F4(mvp[15]);

        // In front of the near plane, and far enough from w == 0 to
        // divide. A triangle with any vertex behind the near plane has
        // to be *clipped*, and clipping is the exact path's job: it is
        // also precisely the case whose projected coordinates explode.
        trust = trust & cmpGe(homogeneous ? cz + cw : cz, zero)
                & cmpGe(cw, minw);

        const F4 iw = one / cw;
        sx[k] = (cx * iw * half + half) * fw;
        sy[k] = (cy * iw * half + half) * fh;
        trust = trust & cmpLe(abs(sx[k]), guard) & cmpLe(abs(sy[k]), guard);
    }
    // No depth is computed. The pre-pass answers "does this cover any
    // pixel", and depth has no bearing on that.

    const F4 minx = min(sx[0], min(sx[1], sx[2]));
    const F4 maxx = max(sx[0], max(sx[1], sx[2]));
    const F4 miny = min(sy[0], min(sy[1], sy[2]));
    const F4 maxy = max(sy[0], max(sy[1], sy[2]));

    const F4 eps = max(F4(kFilterEpsAbs),
                       F4(kFilterEpsRel)
                               * max(max(abs(minx), abs(maxx)), max(fw, fh)));
    // Grown, always outwards: the exact path's triangle is somewhere
    // inside this box, so a box that reaches no pixel proves the
    // triangle reaches none either.
    const F4 lox = minx - eps;
    const F4 hix = maxx + eps;
    const F4 loy = miny - eps;
    const F4 hiy = maxy + eps;

    // The same two tests rasterTri opens with, in the same order, so the
    // counters mean the same thing whichever path did the judging.
    const M4 onbuffer = cmpGe(hix, zero) & cmpLe(lox, fw) & cmpGe(hiy, zero)
            & cmpLe(loy, fh);
    const F4 px0 = max(ceil(lox - half), zero);
    const F4 px1 = min(floor(hix - half), fw - one);
    const F4 py0 = max(ceil(loy - half), zero);
    const F4 py1 = min(floor(hiy - half), fh - one);
    const M4 haspixel = cmpLe(px0, px1) & cmpLe(py0, py1);

    const int tb = trust.bits();
    const int ob = onbuffer.bits();
    const int pb = haspixel.bits();
    for (int i = 0; i < Batch; ++i) {
        const int bit = 1 << i;
        if (!(tb & bit))
            verdict[i] = VerdictExact;
        else if (!(ob & bit))
            verdict[i] = VerdictOffBuffer;
        else if (!(pb & bit))
            verdict[i] = VerdictSubPixel;
        else
            verdict[i] = VerdictKeep;
    }
}

void MaskedDepth::emitBatch(const float *const *v, int n, const float *mvp)
{
    uint8_t verdict[Batch];
    if (simdfilter && n == Batch)
        filterBatch(v, mvp, verdict);
    else
        std::fill(verdict, verdict + Batch, uint8_t(VerdictKeep));

    for (int i = 0; i < n; ++i) {
        if (verdict[i] == VerdictOffBuffer || verdict[i] == VerdictSubPixel) {
            // The exact path would have counted these itself; it never
            // sees them, so the tally is kept here instead of being
            // lost.
            ++framestats.trianglesIn;
            ++framestats.trianglesFiltered;
            if (verdict[i] == VerdictOffBuffer)
                ++framestats.trianglesOffBuffer;
            else
                ++framestats.trianglesSubPixel;
            continue;
        }
        if (verdict[i] == VerdictExact)
            ++framestats.trianglesGuarded;
        emitTriangle(v[i * 3 + 0], v[i * 3 + 1], v[i * 3 + 2], mvp);
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
            ++framestats.trianglesBehind;
            return;
        }
        std::memcpy(poly, tmp, size_t(n) * sizeof(CVert));
        ++framestats.trianglesClipped;
    }

    // KEY: Reject off-screen geometry before the perspective divide, not
    // after it. Everything in front of the near plane has w > 0, so
    // `x > w` for all three vertices means all three are right of the
    // viewport -- the standard homogeneous outcode test, and it is
    // exactly the geometry that would otherwise pay three divisions to
    // land outside the buffer and be thrown away by rasterTri. Only a
    // *unanimous* verdict rejects: a triangle with one vertex on each
    // side of the screen fails every plane individually and still covers
    // it.
    {
        int out[4] = {0, 0, 0, 0};
        for (int i = 0; i < n; ++i) {
            out[0] += poly[i].x > poly[i].w;
            out[1] += poly[i].x < -poly[i].w;
            out[2] += poly[i].y > poly[i].w;
            out[3] += poly[i].y < -poly[i].w;
        }
        for (int p = 0; p < 4; ++p) {
            if (out[p] == n) {
                ++framestats.trianglesOffBuffer;
                return;
            }
        }
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
                ++framestats.trianglesBehind;
                return;
            }
            std::memcpy(poly, tmp, size_t(n) * sizeof(CVert));
            for (int i = 0; i < n; ++i)
                if (poly[i].w < kMinW)
                    poly[i].w = kMinW;
        }
    }

    // Project. KEY: Both depth conventions below are *affine in screen
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
            // KEY: Strictly nearer, so a coincident surface answers
            // visible. The tie a node's own geometry creates against its
            // own bounding box is the one section 12.6 lost, and losing it is
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
    // WARNING: A stand-in is a box drawn where the real mesh has not arrived
    // (docs/SceneStreaming.md section 6). It *does* write depth, so it really
    // does occlude the frame it appears in -- but it is bigger than the
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

void Render::occluderRecede(const float *view, const float *proj, float *out)
{
    out[0] = out[1] = out[2] = 0.0f;
    if (!view || !proj)
        return;
    // Orthographic (proj[15] != 0) has no w to read the convention off,
    // so its depth gradient answers instead.
    const float along = proj[15] != 0.0f ? proj[10] : proj[11];
    // The world direction whose view-space image is +z: row 2 of the
    // view matrix. A unit vector for a rigid camera, and normalized
    // anyway because nothing here promises one.
    const float ax = view[2], ay = view[6], az = view[10];
    const float len = std::sqrt(ax * ax + ay * ay + az * az);
    if (!(len > 0.0f) || along == 0.0f)
        return;
    const float step = (along < 0.0f ? -1.0f : 1.0f) / len;
    out[0] = ax * step;
    out[1] = ay * step;
    out[2] = az * step;
}

void MaskedOccluderPass::build(const DrawCallList &draws, const float *view,
                               const float *proj, bool homogeneousDepth,
                               int viewportW, int viewportH)
{
    const auto t0 = std::chrono::steady_clock::now();
    framestats = MaskedCullStats();

    const int div = std::max(1, conf.resolutionDivisor);
    // WARNING: Only when the size actually changed -- the same trap the shard
    // loop below documents, which this line was falling into. resize()
    // fills every block and then calls clear(), which fills them again,
    // so an unconditional resize is *two* passes over 744 KB before a
    // triangle is looked at, every frame, for a viewport that has not
    // moved. The clear below is the one that has to happen.
    //
    // WARNING: Against the *rounded* size, not the viewport. resize() rounds
    // up to whole blocks, so a 1863-pixel viewport gives a 1864-pixel
    // buffer -- and comparing against 1863 would find them different
    // every frame and skip nothing, which is the shape of a cache that
    // never hits.
    const int wantW = viewportW / div;
    const int wantH = viewportH / div;
    const auto roundUp = [](int v, int block) {
        return v > 0 ? (v + block - 1) / block * block : 0;
    };
    if (buffer.width() != roundUp(wantW, MaskedDepth::BlockW)
        || buffer.height() != roundUp(wantH, MaskedDepth::BlockH))
        buffer.resize(wantW, wantH);
    if (buffer.empty())
        return;
    buffer.clear();
    buffer.resetStats();
    buffer.setCamera(view, proj, homogeneousDepth);
    buffer.setSimdFilter(conf.simdFilter);

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

    uint32_t workers = conf.threads;
    if (workers == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        // Leave the submitting thread and one other alone: this runs in
        // the middle of a frame, not on an idle machine.
        workers = hw > 3 ? hw - 2 : 1;
    }
    workers = std::max<uint32_t>(1, std::min<uint32_t>(workers, 32));

    const size_t limit = std::min<size_t>(ranking.size(), conf.maxOccluders);

    // The hulls, built before the budget is cut rather than after,
    // because which candidates fit depends on which geometry each of
    // them is going to be rasterized from. Offered in ranking order, so
    // a frame that can only afford a few builds spends them on the
    // occluders that cover the most screen.
    coarse.configure(conf.coarse);
    hullWanted.clear();
    if (conf.coarse.enabled) {
        hullWanted.reserve(limit);
        for (size_t r = 0; r < limit; ++r)
            hullWanted.push_back(draws[ranking[r].second].mesh.get());
    }
    coarse.build(hullWanted, workers);
    {
        const CoarseOccluderStats &cs = coarse.stats();
        framestats.coarseEntries = cs.entries;
        framestats.coarseBuilt = cs.built;
        framestats.coarsePending = cs.pending;
        framestats.coarseBytes = cs.bytes;
        framestats.coarseBuildMs = cs.buildMs;
    }

    // Which way is *away from the camera* -- see occluderRecede, which
    // is where the derivation and its warning live.
    const float bias = conf.coarseBias > 0.0f ? conf.coarseBias : 0.0f;
    float recede[3] = {0.0f, 0.0f, 0.0f};
    if (bias > 0.0f)
        occluderRecede(view, proj, recede);

    // Decide the whole admitted set before rasterizing any of it, so
    // that the budget is spent identically however many workers run.
    uint32_t budget = conf.triangleBudget;
    jobs.clear();
    models.clear();
    models.reserve(limit);
    for (size_t r = 0; r < ranking.size(); ++r) {
        if (r >= limit || budget == 0) {
            ++framestats.occludersDropped;
            continue;
        }
        const DrawCall &d = draws[ranking[r].second];
        size_t first = 0, count = 0;
        indexRange(d, first, count);
        const uint32_t exactTris = uint32_t(count / 3);

        // The hull stands in for the draw only where it can cover the
        // same surface. A whole-mesh draw takes the whole hull; a draw
        // of one face takes that face's range, which survives decimation
        // index for index; and a face that collapsed to nothing falls
        // back to the mesh, because an occluder that vanished is
        // occlusion given away for no saving.
        const CoarseOccluder *hull = coarse.find(*d.mesh);
        if (hull) {
            size_t hfirst = 0, hcount = 0;
            if (d.partIndex < 0 && d.indexStart <= 0
                && (d.indexCount <= 0
                    || d.indexCount >= d.mesh->numTriangleIndices)) {
                hcount = hull->triangleIndices.size();
            }
            else if (d.partIndex >= 0
                     && size_t(d.partIndex) < hull->triangleParts.size()) {
                const auto &part = hull->triangleParts[size_t(d.partIndex)];
                hfirst = size_t(part.first);
                hcount = size_t(part.second);
            }
            // Nothing to stand in with: a sub-range that is not a part,
            // or a face that collapsed to nothing. The mesh's own range
            // is still sitting in first/count, untouched.
            if (hcount == 0)
                hull = nullptr;
            else {
                first = hfirst;
                count = hcount;
            }
        }

        const uint32_t tris = uint32_t(count / 3);
        if (tris == 0)
            continue;
        if (tris > budget) {
            // WARNING: Truncating a mesh mid-list leaves a partial surface,
            // which is a *hole*: the remaining triangles still occlude
            // correctly, but nothing behind the missing part is hidden.
            // That is under-culling, so it is allowed -- and it is
            // counted, because a budget that quietly halves an occluder
            // reads as a scene that does not occlude.
            ++framestats.occludersDropped;
            continue;
        }
        // KEY: A draw is split into chunks rather than handed out whole.
        // The unit of work has to be smaller than the imbalance it is
        // meant to hide: measured, 37 admitted draws over 14 workers is
        // two or three each, the draws differ in triangle count by more
        // than an order of magnitude, and the frame then waits on
        // whichever worker drew the largest single mesh. Triangles are
        // independent, so a chunk of one draw's index range rasterizes
        // exactly as that draw's own triangles would.
        //
        // Aimed at roughly eight chunks per worker, which is enough to
        // absorb the variance without making the per-chunk setup matter.
        const uint32_t perChunk = std::max<uint32_t>(
                256u, std::min<uint32_t>(16384u,
                                         conf.triangleBudget
                                                 / std::max(1u, workers * 8)));

        // Where the hull's surface is allowed to claim to be. Every
        // point of it lies within `displacement` of a point of the mesh
        // (CoarseOccluder), so a hull moved that far from the camera
        // cannot be nearer than the surface it stands for -- which is
        // the whole of what makes an approximate occluder admissible.
        // Measured in the mesh's own units, so an instanced draw scales
        // it by its transform; the largest axis, because the bound is a
        // distance and not a component.
        const float *model = d.identity ? nullptr : d.model;
        if (hull && bias > 0.0f && hull->displacement > 0.0f) {
            float scale = 1.0f;
            if (!d.identity) {
                scale = 0.0f;
                for (int c = 0; c < 3; ++c) {
                    const float *col = d.model + c * 4;
                    scale = std::max(scale,
                                     std::sqrt(col[0] * col[0]
                                               + col[1] * col[1]
                                               + col[2] * col[2]));
                }
            }
            const float delta = hull->displacement * scale * bias;
            std::array<float, 16> biased = {1, 0, 0, 0, 0, 1, 0, 0,
                                            0, 0, 1, 0, 0, 0, 0, 1};
            if (!d.identity)
                std::memcpy(biased.data(), d.model, sizeof(biased));
            biased[12] += delta * recede[0];
            biased[13] += delta * recede[1];
            biased[14] += delta * recede[2];
            models.push_back(biased);
            model = models.back().data();
        }

        for (size_t off = 0; off < count; off += size_t(perChunk) * 3) {
            const size_t n = std::min<size_t>(size_t(perChunk) * 3, count - off);
            OccluderJob job;
            job.positions = hull ? hull->positions.data()
                                 : d.mesh->positions;
            job.vertexCount = hull ? hull->positions.size() / 3
                                   : size_t(d.mesh->numVertices);
            job.indices = hull ? hull->triangleIndices.data()
                               : d.mesh->triangleIndices;
            job.first = uint32_t(first + off);
            job.count = uint32_t(n);
            job.model = model;
            jobs.push_back(job);
        }
        budget -= tris;
        ++framestats.occluderDraws;
        framestats.occluderTriangles += tris;
        if (hull) {
            ++framestats.coarseDraws;
            framestats.coarseTrianglesSaved +=
                    exactTris > tris ? exactTris - tris : 0;
        }
    }

    // Everything above is the same work whatever the triangles do: sizing
    // the buffer, clearing it, projecting every candidate's bounds,
    // sorting them and cutting the budget. Timed separately because it is
    // the one term that a change to the rasterizer cannot move.
    framestats.selectMs = millisSince(t0);

    auto runJob = [](MaskedDepth &into, const OccluderJob &j) {
        into.rasterize(j.positions, 0, j.vertexCount, j.indices + j.first,
                       j.count, j.model);
    };

    // Below this there is nothing to share out and the merge would cost
    // more than the split saves.
    if (jobs.size() < 8)
        workers = 1;
    framestats.occluderThreads = workers;

    if (workers == 1) {
        const auto t1 = std::chrono::steady_clock::now();
        for (const OccluderJob &j : jobs)
            runJob(buffer, j);
        // One worker rasterizes straight into the shared buffer, so there
        // is no shard to clear and nothing to merge: the whole of phase
        // one is the rasterization, and phase two does not exist. That is
        // what makes the one-worker row the honest measure of the
        // triangle work itself.
        framestats.shardMs = millisSince(t1);
        framestats.worstRasterMs = framestats.shardMs;
        framestats.sumRasterMs = framestats.shardMs;
        framestats.rasterMs = millisSince(t0);
        return;
    }

    // Greedy longest-first assignment. The ranking is already
    // largest-on-screen first, which correlates with triangle count, so
    // handing out round-robin from the front balances well enough
    // without a bin-packing pass.
    shards.resize(workers);
    for (uint32_t w = 0; w < workers; ++w) {
        MaskedDepth &shard = shards[w];
        // WARNING: Only when the size actually changed. resize() refills
        // every block, so calling it per frame is a second full clear of
        // 744 KB per worker -- for fourteen workers that is 10 MB of
        // memset per frame, paid before any triangle is looked at.
        if (shard.width() != buffer.width()
            || shard.height() != buffer.height())
            shard.resize(buffer.width(), buffer.height());
        shard.setCamera(view, proj, homogeneousDepth);
        shard.setTwoSided(buffer.twoSided());
        shard.setSimdFilter(buffer.simdFilter());
    }

    // KEY: Both phases run on the workers, and the second one is why. A
    // serial merge of fourteen shards is fourteen passes over every block
    // in the buffer, and measured it gave back most of what parallelizing
    // the rasterization had just saved -- 25.8 ms became 15.7 ms rather
    // than the ~4 ms the rasterization alone would predict. Each worker
    // owns a disjoint range of blocks in phase two, so no block is
    // written twice and nothing is locked.
    std::vector<std::thread> pool;
    pool.reserve(workers - 1);
    const size_t nblocks = buffer.blockCount();
    times.assign(workers, WorkerTime{0.0f, 0.0f, 0.0f});
    auto work = [&](uint32_t w) {
        MaskedDepth &shard = shards[w];
        const auto tc = std::chrono::steady_clock::now();
        shard.clear();  // parallel, for the same reason as the merge
        shard.resetStats();
        const auto tr = std::chrono::steady_clock::now();
        times[w].clearMs =
                float(std::chrono::duration<double, std::milli>(tr - tc).count());
        for (size_t i = w; i < jobs.size(); i += workers)
            runJob(shard, jobs[i]);
        // Written once, by the worker that owns the entry: three clock
        // reads per worker per frame, and nothing reads these until the
        // join.
        times[w].rasterMs = millisSince(tr);
    };
    auto mergeRange = [&](uint32_t w) {
        const auto tm = std::chrono::steady_clock::now();
        const size_t lo = nblocks * w / workers;
        const size_t hi = nblocks * (w + 1) / workers;
        for (uint32_t s = 0; s < workers; ++s)
            buffer.mergeBlocks(shards[s], lo, hi);
        times[w].mergeMs = millisSince(tm);
    };
    auto both = [&](uint32_t w) { work(w); };

    const auto tphase1 = std::chrono::steady_clock::now();
    for (uint32_t w = 1; w < workers; ++w)
        pool.emplace_back(both, w);
    both(0);
    for (std::thread &t : pool)
        t.join();
    pool.clear();
    framestats.shardMs = millisSince(tphase1);

    const auto tphase2 = std::chrono::steady_clock::now();
    for (uint32_t w = 1; w < workers; ++w)
        pool.emplace_back(mergeRange, w);
    mergeRange(0);
    for (std::thread &t : pool)
        t.join();
    framestats.mergeMs = millisSince(tphase2);

    // KEY: The worst worker, not the average. A phase costs what its
    // slowest thread costs, and the gap between a phase's wall clock and
    // its slowest worker is what the threading itself took -- spawning
    // and joining 26 threads, and waiting for the scheduler to run them.
    // Reported rather than inferred, because that gap is precisely the
    // quantity two previous estimates in this section got wrong.
    for (uint32_t w = 0; w < workers; ++w) {
        framestats.worstClearMs =
                std::max(framestats.worstClearMs, times[w].clearMs);
        framestats.worstRasterMs =
                std::max(framestats.worstRasterMs, times[w].rasterMs);
        framestats.worstMergeMs =
                std::max(framestats.worstMergeMs, times[w].mergeMs);
        framestats.sumRasterMs += times[w].rasterMs;
    }
    // Counters last, on one thread: two workers incrementing one counter
    // is a data race, and these are reported rather than acted on.
    for (uint32_t w = 0; w < workers; ++w)
        buffer.mergeStats(shards[w]);
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

    // KEY: No padding. The hardware path needs two pad terms because its
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

// WARNING: The *rasterizer* is still deliberately scalar and double. Only the
// pre-pass is vectorized, and the reason is the one thing this file is
// for: filterBatch cannot write to the buffer, so its float arithmetic
// can lose culling and can waste time and can do nothing else, while a
// float rasterTri would decide coverage bits -- and a coverage bit set
// where no triangle reached is a surface claimed where there is none,
// which is the failure the whole of docs/FarFieldProxies.md section 12
// has been chasing. The pre-pass was worth doing first because it is
// where the triangles are: 170684 of 249998 rasterized triangles never
// touch a pixel on the benchmark scene, so two thirds of the pass is
// transform, project and reject, and that is exactly the part a filter
// can take over.
//
// What is left for SIMD after that is rasterTri's row solve and
// updateBlock across four blocks at once, which is a rewrite rather than
// a filter and has to answer the precision question honestly rather than
// side-step it. It is not obviously the next thing to do either: coarse
// occluder geometry would *delete* the sub-pixel two thirds rather than
// accelerate them (section 12.12).
