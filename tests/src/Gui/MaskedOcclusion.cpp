// Tests for the masked software occlusion depth buffer
// (docs/FarFieldProxies.md §12.12). Like the OcclusionCull and
// ProxyHierarchy tests beside them these need no GL context and no
// document — the whole point of putting the oracle on the CPU is that it
// is ordinary arithmetic that can be tested.
//
// **The bias every test here encodes**, and it is the one the mechanism
// exists to restore: the buffer may under-cull freely and may never
// over-cull. So the central test is not an image comparison but an
// inequality against an independently written full-resolution depth
// buffer: whatever this thing stores, no pixel of it may claim a surface
// nearer than the one really there. Everything downstream — a node
// wrongly hidden, geometry deleted from the screen — is that inequality
// broken.
//
// The reference below is deliberately stupid: it tests all three edge
// functions at every pixel centre and interpolates with barycentrics. It
// shares no code with the implementation's scanline spans, block masks
// or two-layer merge, which is what makes agreeing with it mean
// something.

#include <gtest/gtest.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "Gui/Renderer/MaskedOcclusion.h"

using namespace Render;

namespace
{

/// GL-layout (column-major) perspective, as the renderer is handed.
void perspective(float *P, float fovyDeg, float aspect, float znear,
                 float zfar)
{
    for (int i = 0; i < 16; ++i)
        P[i] = 0.0f;
    const float f = 1.0f / std::tan(fovyDeg * 3.14159265358979f / 360.0f);
    P[0] = f / aspect;
    P[5] = f;
    P[10] = (zfar + znear) / (znear - zfar);
    P[11] = -1.0f;
    P[14] = 2.0f * zfar * znear / (znear - zfar);
}

/// GL-layout orthographic. `P[15] == 1`, which is how the projection is
/// recognised as orthographic.
void orthographic(float *P, float halfW, float halfH, float znear,
                  float zfar)
{
    for (int i = 0; i < 16; ++i)
        P[i] = 0.0f;
    P[0] = 1.0f / halfW;
    P[5] = 1.0f / halfH;
    P[10] = -2.0f / (zfar - znear);
    P[14] = -(zfar + znear) / (zfar - znear);
    P[15] = 1.0f;
}

/// Camera at the origin looking down -Z, moved back by \a dist.
void viewAt(float *V, float dist)
{
    for (int i = 0; i < 16; ++i)
        V[i] = 0.0f;
    V[0] = V[5] = V[10] = V[15] = 1.0f;
    V[14] = -dist;
}

void mat4Mul(float *o, const float *a, const float *b)
{
    float t[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += a[k * 4 + r] * b[c * 4 + k];
            t[c * 4 + r] = s;
        }
    for (int i = 0; i < 16; ++i)
        o[i] = t[i];
}

/// A quad on the plane z = \a z, spanning [-\a half, \a half] in x and y,
/// as two triangles. Returned as 18 floats.
std::vector<float> quad(float z, float half)
{
    const float v[4][3] = {{-half, -half, z},
                           {half, -half, z},
                           {half, half, z},
                           {-half, half, z}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    std::vector<float> out;
    for (int i = 0; i < 6; ++i)
        for (int k = 0; k < 3; ++k)
            out.push_back(v[idx[i]][k]);
    return out;
}

/// A quad spanning an explicit rectangle, so a test can cover exactly
/// part of a block.
std::vector<float> quadRect(float x0, float y0, float x1, float y1, float z)
{
    const float v[4][3] = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    std::vector<float> out;
    for (int i = 0; i < 6; ++i)
        for (int k = 0; k < 3; ++k)
            out.push_back(v[idx[i]][k]);
    return out;
}

void boxAt(float *bmin, float *bmax, float cx, float cy, float cz, float half)
{
    bmin[0] = cx - half;
    bmin[1] = cy - half;
    bmin[2] = cz - half;
    bmax[0] = cx + half;
    bmax[1] = cy + half;
    bmax[2] = cz + half;
}

// -----------------------------------------------------------------
// The reference: a full-resolution depth buffer, brute force
// -----------------------------------------------------------------

/// Independently written, and written to be obviously right rather than
/// fast: no blocks, no layers, no spans, one depth per pixel.
///
/// ⚠️ It handles no near-plane clipping, so every scene it is used on
/// keeps its geometry in front of the near plane. That is a limitation
/// of the *reference*, not of what is being tested; the clipping paths
/// have their own cases below.
struct RefDepth {
    int w = 0, h = 0;
    bool ortho = false;
    std::vector<float> depth;

    void reset(int W, int H, bool orthographic)
    {
        w = W;
        h = H;
        ortho = orthographic;
        depth.assign(size_t(W) * size_t(H), -FLT_MAX);
    }

    void project(const float *mvp, const float *p, double &sx, double &sy,
                 double &d) const
    {
        const double cx =
                mvp[0] * p[0] + mvp[4] * p[1] + mvp[8] * p[2] + mvp[12];
        const double cy =
                mvp[1] * p[0] + mvp[5] * p[1] + mvp[9] * p[2] + mvp[13];
        const double cz =
                mvp[2] * p[0] + mvp[6] * p[1] + mvp[10] * p[2] + mvp[14];
        const double cw =
                mvp[3] * p[0] + mvp[7] * p[1] + mvp[11] * p[2] + mvp[15];
        const double iw = 1.0 / cw;
        sx = (cx * iw * 0.5 + 0.5) * w;
        sy = (cy * iw * 0.5 + 0.5) * h;
        d = ortho ? -(cz * iw) : iw;
    }

    void addTriangles(const float *mvp, const std::vector<float> &verts)
    {
        for (size_t t = 0; t + 8 < verts.size(); t += 9) {
            double sx[3], sy[3], sd[3];
            for (int i = 0; i < 3; ++i)
                project(mvp, &verts[t + size_t(i) * 3], sx[i], sy[i], sd[i]);
            const double area = (sx[1] - sx[0]) * (sy[2] - sy[0])
                                - (sy[1] - sy[0]) * (sx[2] - sx[0]);
            if (area == 0.0)
                continue;
            for (int py = 0; py < h; ++py) {
                for (int px = 0; px < w; ++px) {
                    const double X = px + 0.5, Y = py + 0.5;
                    double e[3];
                    for (int k = 0; k < 3; ++k) {
                        const int j = (k + 1) % 3;
                        e[k] = (sx[j] - sx[k]) * (Y - sy[k])
                               - (sy[j] - sy[k]) * (X - sx[k]);
                    }
                    const bool inside = (e[0] >= 0 && e[1] >= 0 && e[2] >= 0)
                                        || (e[0] <= 0 && e[1] <= 0 && e[2] <= 0);
                    if (!inside)
                        continue;
                    // Barycentrics: e[k] is twice the area opposite
                    // vertex (k+2)%3.
                    const double b0 = e[1] / area;
                    const double b1 = e[2] / area;
                    const double b2 = e[0] / area;
                    const double d = b0 * sd[0] + b1 * sd[1] + b2 * sd[2];
                    float &slot = depth[size_t(py) * size_t(w) + size_t(px)];
                    if (float(d) > slot)
                        slot = float(d);
                }
            }
        }
    }

    float at(int x, int y) const
    {
        return depth[size_t(y) * size_t(w) + size_t(x)];
    }
};

/// The one claim the whole file is about: nothing stored is nearer than
/// what is really there.
///
/// Returns how many pixels the buffer actually claimed. ⚠️ The caller
/// must check it. A one-sided assertion over an empty buffer passes
/// perfectly and means nothing, which is the shape of mistake §12.9 had
/// to correct once already: a configuration that measured zero over-cull
/// because it was structurally incapable of culling.
int expectConservative(const MaskedDepth &md, const RefDepth &ref)
{
    int violations = 0;
    int claimed = 0;
    float worst = 0.0f;
    for (int y = 0; y < ref.h; ++y) {
        for (int x = 0; x < ref.w; ++x) {
            const float got = md.pixelDepth(x, y);
            const float want = ref.at(x, y);
            if (got == -FLT_MAX)
                continue;  // nothing claimed here
            ++claimed;
            // A relative slack, because the reference interpolates with
            // barycentrics and the implementation with a plane equation
            // evaluated at a block corner; they agree to float rounding,
            // not bit for bit.
            const float slack =
                    1e-4f * std::max(1.0f, std::fabs(want == -FLT_MAX ? got : want));
            if (got > want + slack) {
                ++violations;
                worst = std::max(worst, got - want);
            }
        }
    }
    EXPECT_EQ(0, violations)
            << "the buffer claimed a surface nearer than the truth at "
            << violations << " pixels, worst by " << worst;
    return claimed;
}

}  // namespace

// -----------------------------------------------------------------
// The floor of the whole thing: an empty buffer hides nothing
// -----------------------------------------------------------------

TEST(MaskedOcclusion, EmptyBufferAnswersVisible)
{
    MaskedDepth md;
    md.resize(64, 64);
    float V[16], P[16];
    viewAt(V, 10.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 100.0f);
    md.setCamera(V, P, true);

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0, 0, 0, 1);
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));
}

TEST(MaskedOcclusion, UnsizedBufferAnswersVisible)
{
    // A renderer that has not sized the buffer yet must draw everything,
    // not cull everything. The difference between those two bugs is the
    // whole model.
    MaskedDepth md;
    float V[16], P[16];
    viewAt(V, 10.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 100.0f);
    md.setCamera(V, P, true);
    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0, 0, 0, 1);
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));
    EXPECT_TRUE(md.empty());
}

// -----------------------------------------------------------------
// The basic occlusion cases
// -----------------------------------------------------------------

TEST(MaskedOcclusion, WallHidesWhatIsBehindIt)
{
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16], VP[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    mat4Mul(VP, P, V);
    md.setCamera(V, P, true);

    const std::vector<float> wall = quad(0.0f, 20.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0, 0, -10, 1);
    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax));

    // ... and does not hide what is in front of it.
    boxAt(bmin, bmax, 0, 0, 10, 1);
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));

    // ... nor what is beside it, off the wall's edge.
    boxAt(bmin, bmax, 60, 0, -10, 1);
    const OccludeAnswer beside = md.testBox(bmin, bmax);
    EXPECT_TRUE(beside == OccludeAnswer::Visible
                || beside == OccludeAnswer::Offscreen);

    RefDepth ref;
    ref.reset(md.width(), md.height(), false);
    ref.addTriangles(VP, wall);
    // A wall filling most of the view: the comparison above is only
    // worth making if the buffer actually holds something.
    EXPECT_GT(expectConservative(md, ref), md.width() * md.height() / 2);
}

TEST(MaskedOcclusion, CoincidentSurfaceAnswersVisible)
{
    // ⭐⭐ The tie §12.6 lost. A node's own geometry is in the depth
    // buffer when its bounding box is tested, and the box's front face
    // is coincident with it. On the GPU that comparison went the wrong
    // way and deleted the node. Here it must answer visible.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);

    const std::vector<float> wall = quad(0.0f, 20.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    // A box whose nearest face is exactly the plane just rasterized.
    float bmin[3] = {-1.0f, -1.0f, -2.0f};
    float bmax[3] = {1.0f, 1.0f, 0.0f};
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));
}

TEST(MaskedOcclusion, BoxCrossingNearPlaneIsRefusedNotCulled)
{
    // The trap that once made the GPU probe report the entire model
    // hidden: a box the camera is inside has no bounded projection. It
    // must be reported as unanswerable and drawn, and the refusal must
    // be counted rather than silently folded into "visible".
    MaskedDepth md;
    md.resize(64, 64);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);

    const std::vector<float> wall = quad(0.0f, 20.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);
    md.resetStats();

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0, 0, 20, 40);  // contains the camera
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));
    EXPECT_EQ(1u, md.stats().queriesNearPlane);
    EXPECT_EQ(0u, md.stats().queriesOccluded);
}

TEST(MaskedOcclusion, RootBoxIsNeverOccludedByItsOwnContents)
{
    // ⭐ The impossible-root guard, expressed as a property rather than
    // as a runtime counter: the box that contains every occluder cannot
    // be hidden by them, because its nearest point is in front of all of
    // them. A mechanism answering otherwise is broken, not lucky.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16];
    viewAt(V, 40.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 400.0f);
    md.setCamera(V, P, true);

    for (int i = 0; i < 8; ++i) {
        const std::vector<float> wall = quad(-2.0f * i, 30.0f);
        md.rasterize(wall.data(), 0, wall.size() / 3);
    }
    float bmin[3] = {-30.0f, -30.0f, -20.0f};
    float bmax[3] = {30.0f, 30.0f, 2.0f};
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));
}

// -----------------------------------------------------------------
// What the two layers are for
// -----------------------------------------------------------------

TEST(MaskedOcclusion, PartialCoverageDoesNotOcclude)
{
    // Half a block covered must not hide anything behind the other half.
    // This is the case a single conservative minimum per block gets
    // right by luck and a careless mask gets wrong.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);

    // A wall covering only the left half of the view.
    const std::vector<float> wall = quadRect(-40.0f, -40.0f, 0.0f, 40.0f, 0.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, -5.0f, 0.0f, -10.0f, 1.0f);  // behind the covered half
    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax));

    boxAt(bmin, bmax, 5.0f, 0.0f, -10.0f, 1.0f);  // behind the open half
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));
}

TEST(MaskedOcclusion, TwoPartialOccludersCompleteABlock)
{
    // ⭐ The merge path, which is the paper's actual contribution: two
    // triangles at similar depth each covering part of a block together
    // hide what a single conservative minimum could not, because the
    // mask remembers the coverage until it completes.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);

    const std::vector<float> left = quadRect(-40.0f, -40.0f, 0.0f, 40.0f, 0.0f);
    const std::vector<float> right = quadRect(0.0f, -40.0f, 40.0f, 40.0f, -0.1f);
    md.rasterize(left.data(), 0, left.size() / 3);
    md.rasterize(right.data(), 0, right.size() / 3);

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0.0f, 0.0f, -10.0f, 2.0f);  // straddles the seam
    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax));
}

TEST(MaskedOcclusion, InterleavingChangesTheAnswerWithinTheFrame)
{
    // ⭐⭐ The property the whole change is for. The same node is asked
    // twice in one frame and gets different answers, because between the
    // two questions an occluder joined the buffer. No hardware query can
    // do this: its answer is a frame old by construction, and that
    // staleness is what §12.11 measured as the fault.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0, 0, -10, 1);
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));

    const std::vector<float> wall = quad(0.0f, 20.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax));
}

TEST(MaskedOcclusion, WindingDoesNotDecideWhetherASurfaceOccludes)
{
    // Two-sided by design: a CAD tessellation's winding is not to be
    // trusted, and a wrongly discarded occluder is a hole nothing
    // reports. The reversed wall must occlude exactly as the forward one
    // does.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);

    std::vector<float> wall = quad(0.0f, 20.0f);
    // Reverse each triangle's winding.
    for (size_t t = 0; t + 8 < wall.size(); t += 9)
        for (int k = 0; k < 3; ++k)
            std::swap(wall[t + 3 + size_t(k)], wall[t + 6 + size_t(k)]);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0, 0, -10, 1);
    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax));
}

// -----------------------------------------------------------------
// Projections
// -----------------------------------------------------------------

TEST(MaskedOcclusion, OrthographicProjectionOccludes)
{
    // The depth convention differs between the two projections — 1/w is
    // constant under an orthographic matrix and useless — so the
    // orthographic path is a separate claim and gets its own case.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16], VP[16];
    viewAt(V, 20.0f);
    orthographic(P, 30.0f, 30.0f, 0.5f, 200.0f);
    mat4Mul(VP, P, V);
    md.setCamera(V, P, true);

    const std::vector<float> wall = quad(0.0f, 20.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0, 0, -10, 1);
    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax));
    boxAt(bmin, bmax, 0, 0, 10, 1);
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));

    RefDepth ref;
    ref.reset(md.width(), md.height(), true);
    ref.addTriangles(VP, wall);
    // The wall spans 40 of the 60 units this projection shows, so it
    // covers four ninths of the buffer and no more.
    EXPECT_GT(expectConservative(md, ref), md.width() * md.height() / 4);
}

TEST(MaskedOcclusion, ZeroToOneDepthConventionOccludes)
{
    // The non-OpenGL clip convention only changes where the near plane
    // is, but that is the plane the clipper cuts against, so a wrong
    // convention silently deletes every occluder near the camera.
    MaskedDepth md;
    md.resize(64, 64);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    // Convert the GL projection's depth range to 0..1.
    P[10] = 200.0f / (0.5f - 200.0f);
    P[14] = 0.5f * 200.0f / (0.5f - 200.0f);
    md.setCamera(V, P, false);

    const std::vector<float> wall = quad(0.0f, 20.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0, 0, -10, 1);
    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax));
}

TEST(MaskedOcclusion, OccluderCrossingTheNearPlaneIsClippedNotDropped)
{
    // The geometry nearest the camera occludes the most, and it is
    // exactly the geometry that crosses the near plane. Dropping such a
    // triangle is a hole in the buffer that nothing reports; the
    // clipping path exists so that its visible part still occludes.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16];
    viewAt(V, 10.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 200.0f);
    md.setCamera(V, P, true);

    // A floor just below the eye, running from behind the camera
    // (z = +40, past the near plane at z = +9) out to z = -60. It fills
    // the lower half of the view, and both of its triangles have to be
    // cut to produce that.
    const float floorQuad[18] = {-50.0f, -2.0f, 40.0f,  50.0f, -2.0f, 40.0f,
                                 50.0f,  -2.0f, -60.0f, -50.0f, -2.0f, 40.0f,
                                 50.0f,  -2.0f, -60.0f, -50.0f, -2.0f, -60.0f};
    md.rasterize(floorQuad, 0, 6);
    EXPECT_EQ(2u, md.stats().trianglesClipped);
    EXPECT_GT(md.stats().blocksUpdated, 0u);

    // A box under the floor is hidden by it — which is only true of the
    // part of the floor that survived the cut.
    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0.0f, -6.0f, -30.0f, 2.0f);
    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax));

    // ... and one above it is not.
    boxAt(bmin, bmax, 0.0f, 6.0f, -30.0f, 2.0f);
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));
}

// -----------------------------------------------------------------
// The inequality, over scenes nobody designed
// -----------------------------------------------------------------

TEST(MaskedOcclusion, NeverClaimsASurfaceNearerThanTheTruth)
{
    // ⭐⭐ The test the rest of the file exists to support. Random
    // triangles at random depths, compared pixel by pixel against a
    // full-resolution buffer built by a different algorithm. The
    // assertion is one-sided on purpose: this buffer is allowed to be
    // less informed than the reference and is never allowed to be more.
    std::mt19937 rng(20260810u);
    std::uniform_real_distribution<float> xy(-25.0f, 25.0f);
    std::uniform_real_distribution<float> z(-60.0f, -2.0f);

    float V[16], P[16], VP[16];
    viewAt(V, 10.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 200.0f);
    mat4Mul(VP, P, V);

    for (int scene = 0; scene < 12; ++scene) {
        MaskedDepth md;
        md.resize(56, 40);
        md.setCamera(V, P, true);

        std::vector<float> verts;
        for (int t = 0; t < 24; ++t) {
            // A triangle with a common depth per vertex some of the
            // time and independent depths otherwise, so that both flat
            // occluders and steeply sloped ones are covered.
            const float zc = z(rng);
            for (int i = 0; i < 3; ++i) {
                verts.push_back(xy(rng));
                verts.push_back(xy(rng));
                verts.push_back((t % 2) ? zc : z(rng));
            }
        }
        md.rasterize(verts.data(), 0, verts.size() / 3);

        RefDepth ref;
        ref.reset(md.width(), md.height(), false);
        ref.addTriangles(VP, verts);
        EXPECT_GT(expectConservative(md, ref), 200)
                << "scene " << scene << " rasterized almost nothing";
    }
}

TEST(MaskedOcclusion, AnOccludedVerdictSurvivesTheReference)
{
    // The query-level form of the same claim: whenever a box is reported
    // hidden, the full-resolution buffer agrees that every pixel it
    // could have reached is already covered by something nearer.
    std::mt19937 rng(777u);
    std::uniform_real_distribution<float> xy(-20.0f, 20.0f);
    std::uniform_real_distribution<float> zd(-50.0f, -5.0f);

    float V[16], P[16], VP[16];
    viewAt(V, 15.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 200.0f);
    mat4Mul(VP, P, V);

    MaskedDepth md;
    md.resize(96, 96);
    md.setCamera(V, P, true);

    std::vector<float> verts;
    for (int i = 0; i < 6; ++i) {
        const std::vector<float> w =
                quadRect(xy(rng) - 15.0f, xy(rng) - 15.0f, xy(rng) + 15.0f,
                         xy(rng) + 15.0f, zd(rng));
        verts.insert(verts.end(), w.begin(), w.end());
    }
    md.rasterize(verts.data(), 0, verts.size() / 3);

    RefDepth ref;
    ref.reset(md.width(), md.height(), false);
    ref.addTriangles(VP, verts);

    int occluded = 0;
    for (int trial = 0; trial < 400; ++trial) {
        float bmin[3], bmax[3];
        boxAt(bmin, bmax, xy(rng), xy(rng), zd(rng), 1.5f);
        float rect[4], dnear = 0.0f;
        if (!md.projectBox(bmin, bmax, rect, &dnear))
            continue;
        if (md.testBox(bmin, bmax) != OccludeAnswer::Occluded)
            continue;
        ++occluded;
        // Every pixel the box's rect touches must already hold something
        // strictly nearer in the reference buffer.
        const int px0 = std::max(0, int(std::floor(rect[0])));
        const int py0 = std::max(0, int(std::floor(rect[1])));
        const int px1 = std::min(md.width() - 1, int(std::ceil(rect[2])) - 1);
        const int py1 = std::min(md.height() - 1, int(std::ceil(rect[3])) - 1);
        for (int y = py0; y <= py1; ++y)
            for (int x = px0; x <= px1; ++x)
                ASSERT_GT(ref.at(x, y), dnear)
                        << "box reported hidden but pixel (" << x << "," << y
                        << ") is open";
    }
    // A test that culls nothing proves nothing; §12.9 quotes a whole
    // configuration that measured zero over-cull because it was
    // structurally incapable of culling at all.
    EXPECT_GT(occluded, 20) << "the scene did not exercise occlusion";
}

// -----------------------------------------------------------------
// Housekeeping
// -----------------------------------------------------------------

TEST(MaskedOcclusion, ClearForgetsEveryOccluder)
{
    MaskedDepth md;
    md.resize(64, 64);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);

    const std::vector<float> wall = quad(0.0f, 20.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0, 0, -10, 1);
    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax));

    md.clear();
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));
}

TEST(MaskedOcclusion, ResizeRoundsUpToWholeBlocks)
{
    MaskedDepth md;
    md.resize(100, 30);
    EXPECT_EQ(0, md.width() % MaskedDepth::BlockW);
    EXPECT_EQ(0, md.height() % MaskedDepth::BlockH);
    EXPECT_GE(md.width(), 100);
    EXPECT_GE(md.height(), 30);
    EXPECT_FALSE(md.empty());

    md.resize(0, 0);
    EXPECT_TRUE(md.empty());
}

TEST(MaskedOcclusion, DegenerateBoundsAreNotAnAnswer)
{
    MaskedDepth md;
    md.resize(64, 64);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);
    const std::vector<float> wall = quad(0.0f, 20.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    // An empty box (min > max) is what a draw with no bounds carries.
    float bmin[3] = {1.0f, 1.0f, 1.0f};
    float bmax[3] = {-1.0f, -1.0f, -1.0f};
    EXPECT_EQ(OccludeAnswer::Visible, md.testBox(bmin, bmax));
}

TEST(MaskedOcclusion, IndexedAndUnindexedAgree)
{
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);

    const float verts[12] = {-20.0f, -20.0f, 0.0f, 20.0f, -20.0f, 0.0f,
                             20.0f,  20.0f,  0.0f, -20.0f, 20.0f, 0.0f};
    const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};

    MaskedDepth a, b;
    a.resize(64, 64);
    b.resize(64, 64);
    a.setCamera(V, P, true);
    b.setCamera(V, P, true);
    a.rasterize(verts, 0, 4, idx, 6);
    const std::vector<float> flat = quad(0.0f, 20.0f);
    b.rasterize(flat.data(), 0, flat.size() / 3);

    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            ASSERT_EQ(a.pixelDepth(x, y), b.pixelDepth(x, y))
                    << "at (" << x << "," << y << ")";
}

TEST(MaskedOcclusion, OutOfRangeIndicesAreCulledNotDereferenced)
{
    MaskedDepth md;
    md.resize(64, 64);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);

    const float verts[9] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f,
                            0.0f,  1.0f,  0.0f};
    const uint32_t idx[6] = {0, 1, 2, 0, 1, 99};
    md.rasterize(verts, 0, 3, idx, 6);
    EXPECT_EQ(2u, md.stats().trianglesIn);
    EXPECT_GE(md.stats().trianglesCulled, 1u);
}
