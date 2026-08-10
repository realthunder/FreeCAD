// Tests for the masked software occlusion depth buffer
// (docs/FarFieldProxies.md section 12.12). Like the OcclusionCull and
// ProxyHierarchy tests beside them these need no GL context and no
// document -- the whole point of putting the oracle on the CPU is that it
// is ordinary arithmetic that can be tested.
//
// **The bias every test here encodes**, and it is the one the mechanism
// exists to restore: the buffer may under-cull freely and may never
// over-cull. So the central test is not an image comparison but an
// inequality against an independently written full-resolution depth
// buffer: whatever this thing stores, no pixel of it may claim a surface
// nearer than the one really there. Everything downstream -- a node
// wrongly hidden, geometry deleted from the screen -- is that inequality
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
#include <deque>
#include <memory>
#include <random>
#include <vector>

#include "Gui/Renderer/MaskedOcclusion.h"
#include "Gui/Renderer/Simd4.h"

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
/// WARNING: It handles no near-plane clipping, so every scene it is used on
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
/// Returns how many pixels the buffer actually claimed. WARNING: The caller
/// must check it. A one-sided assertion over an empty buffer passes
/// perfectly and means nothing, which is the shape of mistake section 12.9 had
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
    // KEY: The tie section 12.6 lost. A node's own geometry is in the depth
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
    // KEY: The impossible-root guard, expressed as a property rather than
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
    // KEY: The merge path, which is the paper's actual contribution: two
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
    // KEY: The property the whole change is for. The same node is asked
    // twice in one frame and gets different answers, because between the
    // two questions an occluder joined the buffer. No hardware query can
    // do this: its answer is a frame old by construction, and that
    // staleness is what section 12.11 measured as the fault.
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

TEST(MaskedOcclusion, AdjacentTrianglesLeaveNoCrackAlongTheirSharedEdge)
{
    // KEY: An occluder is a mesh, not a bag of triangles, and the seams
    // where its triangles meet must not leak. This was not hypothetical:
    // pulling every edge inwards by 1e-5 px -- which looks like free
    // safety, since it makes any rounding lose coverage rather than
    // invent it -- opened a one-pixel crack along every shared edge, and
    // a two-triangle wall filling the viewport stopped occluding a box
    // sitting on its diagonal.
    //
    // The seam is placed exactly on the query: a symmetric quad's
    // diagonal runs through the centre of the buffer, and the box is
    // centred there too.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16];
    viewAt(V, 20.0f);
    perspective(P, 60.0f, 1.0f, 0.5f, 200.0f);
    md.setCamera(V, P, true);

    const std::vector<float> wall = quad(0.0f, 20.0f);
    md.rasterize(wall.data(), 0, wall.size() / 3);

    // Every pixel along the diagonal must hold the wall's depth.
    int uncovered = 0;
    for (int i = 0; i < md.width() && i < md.height(); ++i)
        if (md.pixelDepth(i, i) == -FLT_MAX)
            ++uncovered;
    EXPECT_EQ(0, uncovered)
            << uncovered << " pixels of the shared diagonal hold no surface";

    float bmin[3], bmax[3];
    boxAt(bmin, bmax, 0.0f, 0.0f, -10.0f, 1.0f);
    EXPECT_EQ(OccludeAnswer::Occluded, md.testBox(bmin, bmax))
            << "a box on the seam between two occluder triangles showed "
               "through it";
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
    // The depth convention differs between the two projections -- 1/w is
    // constant under an orthographic matrix and useless -- so the
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

    // A box under the floor is hidden by it -- which is only true of the
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
    // KEY: The test the rest of the file exists to support. Random
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
    // A test that culls nothing proves nothing; section 12.9 quotes a whole
    // configuration that measured zero over-cull because it was
    // structurally incapable of culling at all.
    EXPECT_GT(occluded, 20) << "the scene did not exercise occlusion";
}

// -----------------------------------------------------------------
// The vector pre-pass (section 12.14)
// -----------------------------------------------------------------
//
// **What has to be proved about it, and what does not.** The pre-pass is
// float and the rasterizer is double, which in this file is normally the
// beginning of an argument about over-culling -- and here it is not one,
// because the pre-pass cannot write to the buffer. It either discards a
// triangle or hands it to the same double path as before, unchanged and
// recomputed from the original vertices. So the claim to test is not
// "the float arithmetic is accurate enough to rasterize with"; it is
// **"it discards only what the exact path would have discarded anyway"**,
// and the sharpest form of that is that the two buffers come out
// identical pixel for pixel.
//
// The direction that is *not* tested is deliberate too: a pre-pass that
// discarded too much would show up as lost occlusion, which this
// mechanism is always allowed to do.

namespace
{

/// The scene the two arms are compared on: triangles at every scale from
/// far below a pixel to filling the view, which is what a CAD
/// tessellation looks like to this buffer.
std::vector<float> mixedScaleScene(unsigned seed, int count)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> xy(-25.0f, 25.0f);
    std::uniform_real_distribution<float> z(-60.0f, -3.0f);
    // Log-uniform, not uniform: a tessellation's triangle sizes span
    // orders of magnitude, and drawing them uniformly would make almost
    // every triangle a large one and the sub-pixel population -- the
    // whole point of the filter -- a rounding error in the scene.
    std::uniform_real_distribution<float> logscale(std::log(0.002f),
                                                   std::log(6.0f));
    std::vector<float> verts;
    for (int t = 0; t < count; ++t) {
        const float cx = xy(rng), cy = xy(rng), cz = z(rng);
        const float s = std::exp(logscale(rng));
        std::uniform_real_distribution<float> off(-s, s);
        for (int i = 0; i < 3; ++i) {
            verts.push_back(cx + off(rng));
            verts.push_back(cy + off(rng));
            verts.push_back(cz + off(rng));
        }
    }
    return verts;
}

/// Both arms of the same scene, so a test can only compare like with
/// like.
void bothArms(MaskedDepth &on, MaskedDepth &off, const float *V,
              const float *P, const std::vector<float> &verts, int w, int h)
{
    for (int arm = 0; arm < 2; ++arm) {
        MaskedDepth &md = arm ? on : off;
        md.resize(w, h);
        md.setCamera(V, P, true);
        md.setSimdFilter(arm != 0);
        md.rasterize(verts.data(), 0, verts.size() / 3);
    }
}

}  // namespace

TEST(MaskedOcclusion, VectorPrePassIsOnByDefault)
{
    // It is an optimization, so it is the default; the switch exists to
    // measure against, not to be found off in the field. Stated as a
    // test because "the default changed" is otherwise a silent way for a
    // measurement to stop meaning what it said.
    MaskedDepth md;
    EXPECT_TRUE(md.simdFilter());
    MaskedCullConfig conf;
    EXPECT_TRUE(conf.simdFilter);
}

TEST(MaskedOcclusion, VectorPrePassLeavesTheBufferIdentical)
{
    // KEY: The central claim. Same scene, same camera, pre-pass on and off:
    // every pixel of the two buffers agrees exactly -- not within a
    // tolerance, exactly -- because every triangle that survives the
    // filter goes through the identical double-precision code in the
    // identical order. A difference here is the filter discarding
    // something the rasterizer would have drawn.
    float V[16], P[16];
    viewAt(V, 12.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 200.0f);

    for (unsigned seed = 0; seed < 8; ++seed) {
        const std::vector<float> verts = mixedScaleScene(1000u + seed, 400);
        MaskedDepth on, off;
        bothArms(on, off, V, P, verts, 120, 88);

        int differing = 0;
        for (int y = 0; y < off.height(); ++y)
            for (int x = 0; x < off.width(); ++x)
                if (on.pixelDepth(x, y) != off.pixelDepth(x, y))
                    ++differing;
        EXPECT_EQ(0, differing) << "seed " << seed << ": the pre-pass changed "
                               << differing << " pixels";

        // The same statement in the counters: what reached the
        // rasterizer, and what it made of it.
        EXPECT_EQ(off.stats().trianglesDrawn, on.stats().trianglesDrawn);
        EXPECT_EQ(off.stats().trianglesSubPixel, on.stats().trianglesSubPixel);
        EXPECT_EQ(off.stats().trianglesOffBuffer, on.stats().trianglesOffBuffer);
        EXPECT_EQ(off.stats().blocksUpdated, on.stats().blocksUpdated);
        EXPECT_EQ(0u, off.stats().trianglesFiltered)
                << "the reference arm ran the filter";
    }
}

TEST(MaskedOcclusion, VectorPrePassDiscardsMostOfWhatIsOffered)
{
    // The cost side: a filter that is correct and never fires is a
    // slower rasterizer. On the benchmark scene 170684 of 249998
    // triangles cover no pixel; this asserts the mechanism reaches that
    // population at all, and that everything it dropped is accounted for
    // in the same two buckets the exact path uses.
    float V[16], P[16];
    viewAt(V, 12.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 200.0f);

    const std::vector<float> verts = mixedScaleScene(4242u, 800);
    MaskedDepth on, off;
    bothArms(on, off, V, P, verts, 120, 88);

    const auto &s = on.stats();
    EXPECT_GT(s.trianglesSubPixel, 100u) << "the scene has nothing to filter";
    EXPECT_GT(s.trianglesFiltered, s.trianglesSubPixel / 2)
            << "the pre-pass judged far less than it was given";
    EXPECT_LE(s.trianglesFiltered, s.trianglesSubPixel + s.trianglesOffBuffer)
            << "the pre-pass discarded more than it classified";
    // Nothing in this scene comes near the near plane or the guard band.
    EXPECT_EQ(0u, s.trianglesGuarded);
}

TEST(MaskedOcclusion, VectorPrePassDeclinesTheNearPlaneRatherThanGuessing)
{
    // A triangle crossing the near plane is the case the exact path is
    // double *for*: it projects to coordinates in the millions where
    // float cancellation is worth whole pixels. The pre-pass does not
    // try to be clever about it -- it hands the lane over and says so.
    MaskedDepth md;
    md.resize(128, 128);
    float V[16], P[16];
    viewAt(V, 10.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 200.0f);
    md.setCamera(V, P, true);

    // The same floor as OccluderCrossingTheNearPlaneIsClippedNotDropped,
    // padded to a full batch of four triangles with two more that do not
    // cross, so the lanes are genuinely mixed.
    const float floorQuad[36] = {
            -50.0f, -2.0f, 40.0f,  50.0f,  -2.0f, 40.0f,
            50.0f,  -2.0f, -60.0f, -50.0f, -2.0f, 40.0f,
            50.0f,  -2.0f, -60.0f, -50.0f, -2.0f, -60.0f,
            -20.0f, 5.0f,  -30.0f, 20.0f,  5.0f,  -30.0f,
            20.0f,  15.0f, -30.0f, -20.0f, 5.0f,  -30.0f,
            20.0f,  15.0f, -30.0f, -20.0f, 15.0f, -30.0f};
    md.rasterize(floorQuad, 0, 12);

    EXPECT_EQ(2u, md.stats().trianglesClipped) << "the cut still happens";
    EXPECT_GE(md.stats().trianglesGuarded, 2u)
            << "the crossing triangles were judged rather than handed over";
    EXPECT_EQ(0u, md.stats().trianglesFiltered)
            << "nothing here covers no pixel";

    // And the picture is the one the exact path alone produces.
    MaskedDepth ref;
    ref.resize(128, 128);
    ref.setCamera(V, P, true);
    ref.setSimdFilter(false);
    ref.rasterize(floorQuad, 0, 12);
    for (int y = 0; y < ref.height(); ++y)
        for (int x = 0; x < ref.width(); ++x)
            ASSERT_EQ(ref.pixelDepth(x, y), md.pixelDepth(x, y))
                    << "at (" << x << "," << y << ")";
}

TEST(MaskedOcclusion, PartialBatchesAreRasterizedToo)
{
    // The lanes are filled four at a time and a mesh is not a multiple
    // of four. The tail takes the exact path, and the bug this guards
    // against is the tail being dropped instead -- which would be
    // invisible in every test above, since they all have hundreds of
    // triangles and would lose at most three.
    float V[16], P[16];
    viewAt(V, 12.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 200.0f);

    const std::vector<float> full = mixedScaleScene(99u, 16);
    for (int tris = 1; tris <= 9; ++tris) {
        const std::vector<float> verts(full.begin(),
                                       full.begin() + size_t(tris) * 9);
        MaskedDepth on, off;
        bothArms(on, off, V, P, verts, 120, 88);
        EXPECT_EQ(uint32_t(tris), on.stats().trianglesIn)
                << tris << " triangles in, some lost in the tail";
        EXPECT_EQ(off.stats().trianglesDrawn, on.stats().trianglesDrawn)
                << "at " << tris << " triangles";
        for (int y = 0; y < off.height(); ++y)
            for (int x = 0; x < off.width(); ++x)
                ASSERT_EQ(off.pixelDepth(x, y), on.pixelDepth(x, y))
                        << tris << " triangles, at (" << x << "," << y << ")";
    }
}

TEST(MaskedOcclusion, VectorPrePassStillNeverClaimsMoreThanTheTruth)
{
    // The file's one inequality, asked again with the pre-pass in the
    // way. It cannot fail while the test above passes -- the buffers are
    // identical -- and it is here because that is an argument and this
    // is a measurement.
    std::mt19937 rng(31337u);
    std::uniform_real_distribution<float> xy(-25.0f, 25.0f);
    std::uniform_real_distribution<float> z(-60.0f, -2.0f);

    float V[16], P[16], VP[16];
    viewAt(V, 10.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 200.0f);
    mat4Mul(VP, P, V);

    for (int scene = 0; scene < 6; ++scene) {
        MaskedDepth md;
        md.resize(56, 40);
        md.setCamera(V, P, true);
        ASSERT_TRUE(md.simdFilter());

        std::vector<float> verts;
        for (int t = 0; t < 24; ++t) {
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

// -----------------------------------------------------------------
// The four lanes themselves
// -----------------------------------------------------------------

TEST(Simd4, FloorRoundsTowardsMinusInfinity)
{
    // The one operation with no single instruction behind it on SSE2 or
    // ARMv7, so the one worth testing directly: truncation and floor
    // differ exactly where the pre-pass cares, on negative coordinates
    // just off the left of the buffer.
    const float in[4] = {-0.5f, 0.5f, -3.0f, 7.75f};
    alignas(16) float src[4] = {in[0], in[1], in[2], in[3]};
    alignas(16) float got[4];
    Render::floor(Render::F4::load(src)).store(got);
    for (int i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(std::floor(in[i]), got[i]) << "lane " << i;
    Render::ceil(Render::F4::load(src)).store(got);
    for (int i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(std::ceil(in[i]), got[i]) << "lane " << i;
}

TEST(Simd4, LaneMaskPutsLaneZeroInBitZero)
{
    // The pre-pass reads its four verdicts out of this ordering, so
    // getting it backwards would judge the wrong triangles -- and would
    // do it silently, since the verdicts are all plausible.
    alignas(16) float a[4] = {1.0f, 5.0f, 2.0f, 9.0f};
    alignas(16) float b[4] = {3.0f, 3.0f, 3.0f, 3.0f};
    const Render::M4 m = Render::cmpLt(Render::F4::load(a),
                                       Render::F4::load(b));
    EXPECT_EQ(0b0101, m.bits());
    EXPECT_EQ(0b1111, (Render::cmpGe(Render::F4::load(a),
                                     Render::F4::load(a))).bits());
    // NaN compares false against everything, which is what makes the
    // pre-pass's trust mask safe -- see filterBatch.
    alignas(16) float n[4] = {std::nanf(""), 1.0f, 1.0f, 1.0f};
    EXPECT_EQ(0b1110, (Render::cmpGe(Render::F4::load(n),
                                     Render::F4::load(n))).bits());
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

// -----------------------------------------------------------------
// The pass: choosing occluders, and the walk that spends them
// -----------------------------------------------------------------

namespace
{

/// A draw list with real triangle meshes behind it. The meshes are held
/// in deques so that the pointers a MeshData carries stay valid as the
/// scene grows.
struct SceneBuilder {
    std::deque<std::vector<float>> positions;
    std::deque<std::vector<int32_t>> indices;
    DrawCallList draws;

    DrawCall &add(std::vector<float> pos, std::vector<int32_t> idx,
                  uint64_t key)
    {
        positions.push_back(std::move(pos));
        indices.push_back(std::move(idx));
        auto mesh = std::make_shared<MeshData>();
        mesh->numVertices = int(positions.back().size() / 3);
        mesh->positions = positions.back().data();
        mesh->triangleIndices = indices.back().data();
        mesh->numTriangleIndices = int(indices.back().size());
        DrawCall d;
        d.mesh = mesh;
        d.objectKey = key;
        for (int k = 0; k < 3; ++k) {
            d.bboxMin[k] = FLT_MAX;
            d.bboxMax[k] = -FLT_MAX;
        }
        for (size_t v = 0; v + 2 < positions.back().size(); v += 3)
            for (int k = 0; k < 3; ++k) {
                d.bboxMin[k] = std::min(d.bboxMin[k], positions.back()[v + k]);
                d.bboxMax[k] = std::max(d.bboxMax[k], positions.back()[v + k]);
            }
        draws.push_back(d);
        return draws.back();
    }

    /// An axis-aligned wall on the plane z, as two triangles.
    DrawCall &addWall(float x0, float y0, float x1, float y1, float z,
                      uint64_t key)
    {
        return add({x0, y0, z, x1, y0, z, x1, y1, z, x0, y1, z},
                   {0, 1, 2, 0, 2, 3}, key);
    }

    /// A closed box, as twelve triangles -- real geometry rather than a
    /// bounding volume, which is what an occluder has to be.
    DrawCall &addBox(float cx, float cy, float cz, float half, uint64_t key)
    {
        std::vector<float> p;
        for (int c = 0; c < 8; ++c) {
            p.push_back(cx + ((c & 1) ? half : -half));
            p.push_back(cy + ((c & 2) ? half : -half));
            p.push_back(cz + ((c & 4) ? half : -half));
        }
        const unsigned short *bi = occlusionBoxIndices();
        std::vector<int32_t> idx;
        for (int i = 0; i < 36; ++i)
            idx.push_back(int32_t(bi[i]));
        return add(std::move(p), std::move(idx), key);
    }
};

/// The pass under a camera, with the hierarchy built from the same draw
/// list. Returns the mask.
///
/// WARNING: `maxPerCell` is 1 rather than the default 32 throughout these
/// tests. Culling is per *node*, so a scene small enough to fit one cell
/// has exactly one node -- the root -- and the root can never be hidden by
/// its own contents. That is correct behaviour and it is also a test
/// that proves nothing, so the partition is forced to subdivide. The
/// benchmark scenes have thousands of instances and subdivide on their
/// own.
std::vector<uint8_t> runPass(MaskedOccluderPass &pass, SceneBuilder &scene,
                             const float *V, const float *P, int w, int h,
                             ProxyHierarchy &index,
                             std::vector<int32_t> *owner = nullptr)
{
    std::vector<ProxyInstance> inst;
    proxyInstances(scene.draws, inst);
    ProxyParams params;
    params.maxPerCell = 1;
    index.build(inst, params);
    pass.build(scene.draws, V, P, true, w, h);
    std::vector<uint8_t> mask(scene.draws.size(), 0);
    pass.cull(index, V, P, float(h), mask, owner);
    return mask;
}

}  // namespace

TEST(MaskedOcclusionPass, HidesWhatTheWallCovers)
{
    float V[16], P[16];
    viewAt(V, 40.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    scene.addWall(-25.0f, -25.0f, 25.0f, 25.0f, 0.0f, 1);  // draw 0
    // Behind the wall, and small enough that the wall covers them whole.
    for (int i = 0; i < 8; ++i)
        scene.addBox(-8.0f + 2.0f * i, 0.0f, -20.0f, 0.8f, 10 + uint64_t(i));
    // In front of the wall.
    const size_t front = scene.draws.size();
    scene.addBox(0.0f, 0.0f, 20.0f, 1.0f, 99);

    MaskedOccluderPass pass;
    ProxyHierarchy index;
    std::vector<int32_t> owner;
    const auto mask = runPass(pass, scene, V, P, 512, 512, index, &owner);

    EXPECT_EQ(0u, pass.lastFrame().rootRefused);
    // The wall, and the box in front of it, which is opaque geometry and
    // occludes whatever is behind it just as legitimately.
    EXPECT_GE(pass.lastFrame().occluderDraws, 1u)
            << "the wall was not an occluder";
    EXPECT_EQ(0, mask[0]) << "the wall culled itself";
    EXPECT_EQ(0, mask[front]) << "geometry in front of the wall was culled";
    int hidden = 0;
    for (size_t i = 1; i < front; ++i)
        hidden += mask[i] ? 1 : 0;
    EXPECT_EQ(int(front - 1), hidden)
            << "the wall did not hide everything behind it";
    EXPECT_GT(pass.lastFrame().hiddenInstances, 0u);
    // The verdict is traceable to the node that took it.
    for (size_t i = 1; i < front; ++i)
        EXPECT_GE(owner[i], 0) << "row " << i << " was cut by nobody";
}

TEST(MaskedOcclusionPass, NothingIsHiddenWithoutAnOccluder)
{
    // The same scene with the wall removed must cull nothing at all.
    // KEY: This is the control the image comparisons of section 12.5 lacked: a
    // mechanism that culls a fixed set regardless of the scene looks
    // identical to one that is working, until it is asked about a scene
    // with nothing to hide behind.
    float V[16], P[16];
    viewAt(V, 40.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    for (int i = 0; i < 8; ++i)
        scene.addBox(-8.0f + 2.0f * i, 0.0f, -20.0f, 0.8f, 10 + uint64_t(i));

    MaskedOccluderPass pass;
    ProxyHierarchy index;
    const auto mask = runPass(pass, scene, V, P, 512, 512, index);
    for (size_t i = 0; i < mask.size(); ++i)
        EXPECT_EQ(0, mask[i]) << "row " << i << " was culled by nothing";
    EXPECT_EQ(0u, pass.lastFrame().hiddenInstances);
}

TEST(MaskedOcclusionPass, OnlyDepthWritingOpaqueTrianglesOcclude)
{
    // A surface that does not write depth cannot hide anything, and
    // rasterizing it would hide geometry the eye can plainly see through.
    float V[16], P[16];
    viewAt(V, 40.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 400.0f);

    for (int variant = 0; variant < 4; ++variant) {
        SceneBuilder scene;
        DrawCall &wall = scene.addWall(-25.0f, -25.0f, 25.0f, 25.0f, 0.0f, 1);
        switch (variant) {
            case 0: wall.material.transparent = true; break;
            case 1: wall.material.ontop = true; break;
            case 2: wall.material.depthwrite = false; break;
            case 3: wall.material.type = Material::Line; break;
        }
        scene.addBox(0.0f, 0.0f, -20.0f, 1.0f, 10);

        MaskedOccluderPass pass;
        ProxyHierarchy index;
        const auto mask = runPass(pass, scene, V, P, 256, 256, index);
        EXPECT_EQ(0u, pass.lastFrame().occluderDraws)
                << "variant " << variant << " was rasterized as an occluder";
        EXPECT_EQ(0, mask[1]) << "variant " << variant << " hid something";
    }
}

TEST(MaskedOcclusionPass, ATriangleBudgetUnderCullsAndSaysSo)
{
    // KEY: A cap that drops occluders must be reported. A silent one reads
    // as "this scene does not occlude" when what happened is "we did not
    // look" -- the shape of mistake section 12.10 had to correct twice.
    float V[16], P[16];
    viewAt(V, 40.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    scene.addWall(-25.0f, -25.0f, 25.0f, 25.0f, 0.0f, 1);
    scene.addBox(0.0f, 0.0f, -20.0f, 1.0f, 10);

    MaskedCullConfig conf;
    conf.triangleBudget = 1;  // the wall is two triangles
    MaskedOccluderPass pass;
    pass.configure(conf);
    ProxyHierarchy index;
    const auto mask = runPass(pass, scene, V, P, 256, 256, index);

    EXPECT_GT(pass.lastFrame().occludersDropped, 0u);
    EXPECT_EQ(0u, pass.lastFrame().occluderDraws);
    for (size_t i = 0; i < mask.size(); ++i)
        EXPECT_EQ(0, mask[i]) << "row " << i << " culled with no occluders";
}

TEST(MaskedOcclusionPass, ANodeIsNotHiddenByItsOwnGeometry)
{
    // KEY: The failure this whole mechanism replaces. On the hardware
    // path a node was re-tested after the pass that wrote its own
    // contents, and its bounding box lost the depth comparison against
    // itself. Here the wall is its own occluder and must survive being
    // asked about.
    float V[16], P[16];
    viewAt(V, 40.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    for (int i = 0; i < 24; ++i) {
        const float x = -20.0f + 1.7f * i;
        scene.addWall(x, -25.0f, x + 1.7f, 25.0f, 0.0f, uint64_t(i));
    }

    MaskedOccluderPass pass;
    ProxyHierarchy index;
    const auto mask = runPass(pass, scene, V, P, 512, 512, index);
    for (size_t i = 0; i < mask.size(); ++i)
        EXPECT_EQ(0, mask[i]) << "row " << i << " was hidden by itself";
    EXPECT_EQ(0u, pass.lastFrame().rootRefused);
}

TEST(MaskedOcclusionPass, TheCameraInsideTheModelStillDrawsIt)
{
    // A node the camera stands inside cannot be reduced to a screen
    // rect. It must be drawn and counted as exempt, not culled -- the
    // trap that once reported an entire model hidden.
    float V[16], P[16];
    viewAt(V, 0.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    scene.addBox(0.0f, 0.0f, 0.0f, 30.0f, 1);   // the camera is inside it
    scene.addBox(0.0f, 0.0f, -20.0f, 1.0f, 2);

    MaskedOccluderPass pass;
    ProxyHierarchy index;
    const auto mask = runPass(pass, scene, V, P, 256, 256, index);
    EXPECT_EQ(0, mask[0]);
    EXPECT_GT(pass.lastFrame().nearExempt, 0u);
    EXPECT_EQ(0u, pass.lastFrame().rootRefused);
}

TEST(MaskedOcclusionPass, ThePhaseBreakdownAccountsForTheWholePass)
{
    // KEY: Not a timing assertion -- those are flaky and would be testing
    // the machine. This tests that the phases *account* for the pass:
    // every term is a real elapsed interval, the parts fit inside the
    // whole, and the worst worker fits inside its phase. That is what
    // makes the breakdown usable as evidence, and section 12.15 exists
    // because for two sessions the missing term was attributed by
    // subtraction instead.
    float V[16], P[16];
    viewAt(V, 40.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 400.0f);

    for (uint32_t threads : {1u, 4u}) {
        SceneBuilder scene;
        for (int i = 0; i < 16; ++i) {
            const float x = -24.0f + 3.0f * i;
            scene.addWall(x, -25.0f, x + 3.0f, 25.0f, -0.1f * i, uint64_t(i));
        }
        MaskedOccluderPass pass;
        MaskedCullConfig c;
        c.threads = threads;
        pass.configure(c);
        ProxyHierarchy index;
        runPass(pass, scene, V, P, 512, 512, index);
        const MaskedCullStats &s = pass.lastFrame();

        EXPECT_EQ(threads, s.occluderThreads);
        for (float t : {s.rasterMs, s.selectMs, s.shardMs, s.mergeMs,
                        s.worstClearMs, s.worstRasterMs, s.worstMergeMs,
                        s.sumRasterMs})
            EXPECT_GE(t, 0.0f) << threads << " workers: a negative interval";

        const float slack = 1e-3f;  // the clock is read between the phases
        EXPECT_LE(s.selectMs + s.shardMs + s.mergeMs, s.rasterMs + slack)
                << threads << " workers: the phases exceed the pass";
        EXPECT_LE(s.worstClearMs + s.worstRasterMs, s.shardMs + slack)
                << threads << " workers: a worker outlasted its own phase";
        EXPECT_LE(s.worstMergeMs, s.mergeMs + slack);
        EXPECT_LE(s.worstRasterMs, s.sumRasterMs + slack)
                << "the worst worker is not part of the sum";
        EXPECT_GT(s.rasterMs, 0.0f) << "the pass took no time at all";

        if (threads == 1) {
            // One worker rasterizes into the shared buffer: there is no
            // shard to clear and nothing to merge, and saying so is what
            // makes the one-worker row the honest measure of the
            // triangle work.
            EXPECT_EQ(0.0f, s.mergeMs);
            EXPECT_EQ(0.0f, s.worstClearMs);
            EXPECT_FLOAT_EQ(s.worstRasterMs, s.sumRasterMs);
        }
    }
}

TEST(MaskedOcclusionPass, WorkersNeverCullMoreThanOneThreadWould)
{
    // KEY: The occluder pass splits its work across workers that each
    // rasterize into their own buffer, and the buffers are merged. Two
    // two-layer blocks cannot merge without loss -- four layers do not
    // fit in two -- so the merged buffer may occlude slightly less. The
    // claim under test is the direction: never more. A worker count that
    // could cull *more* would be culling something no single-threaded
    // run justified, which is the one failure this mechanism exists to
    // remove.
    float V[16], P[16];
    viewAt(V, 40.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 400.0f);

    auto buildScene = [](SceneBuilder &scene) {
        // Enough occluders that the pass actually splits the work: below
        // its minimum it falls back to one worker, and then this test
        // would compare a single-threaded run against itself.
        for (int i = 0; i < 16; ++i) {
            const float x = -24.0f + 3.0f * i;
            scene.addWall(x, -25.0f, x + 3.0f, 25.0f, -0.1f * i, uint64_t(i));
        }
        for (int i = 0; i < 40; ++i)
            scene.addBox(-18.0f + 0.9f * i, 0.0f, -20.0f, 0.4f,
                         100 + uint64_t(i));
    };

    SceneBuilder one;
    buildScene(one);
    MaskedOccluderPass passOne;
    MaskedCullConfig c1;
    c1.threads = 1;
    passOne.configure(c1);
    ProxyHierarchy indexOne;
    const auto maskOne = runPass(passOne, one, V, P, 512, 512, indexOne);

    int hiddenOne = 0;
    for (uint8_t m : maskOne)
        hiddenOne += m ? 1 : 0;
    EXPECT_GT(hiddenOne, 0) << "the single-threaded run culled nothing, so "
                               "the comparison proves nothing";

    for (uint32_t threads : {2u, 3u, 4u, 8u}) {
        SceneBuilder many;
        buildScene(many);
        MaskedOccluderPass passMany;
        MaskedCullConfig cm;
        cm.threads = threads;
        passMany.configure(cm);
        ProxyHierarchy indexMany;
        const auto maskMany = runPass(passMany, many, V, P, 512, 512, indexMany);

        ASSERT_EQ(maskOne.size(), maskMany.size());
        for (size_t i = 0; i < maskOne.size(); ++i) {
            if (maskMany[i])
                EXPECT_TRUE(maskOne[i])
                        << threads << " workers culled row " << i
                        << " that one worker kept";
        }
        EXPECT_EQ(threads, passMany.lastFrame().occluderThreads);
    }
}

TEST(MaskedOcclusionPass, TheMaskIsAdditiveAndTheOwnerIsNot)
{
    // The mask composes with the frustum's rejections, so it is only
    // ever set. The attribution is the opposite: a stale owner names a
    // verdict that may since have been withdrawn, so it is cleared every
    // frame.
    float V[16], P[16];
    viewAt(V, 40.0f);
    perspective(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    scene.addWall(-25.0f, -25.0f, 25.0f, 25.0f, 0.0f, 1);
    scene.addBox(0.0f, 0.0f, -20.0f, 1.0f, 10);

    std::vector<ProxyInstance> inst;
    proxyInstances(scene.draws, inst);
    ProxyHierarchy index;
    index.build(inst, ProxyParams());

    MaskedOccluderPass pass;
    pass.build(scene.draws, V, P, true, 256, 256);
    std::vector<uint8_t> mask(scene.draws.size(), 0);
    mask[0] = 1;  // as if the frustum had rejected the wall
    std::vector<int32_t> owner(scene.draws.size(), 7);
    pass.cull(index, V, P, 256.0f, mask, &owner);

    EXPECT_EQ(1, mask[0]) << "the culler cleared somebody else's rejection";
    EXPECT_EQ(-1, owner[0]) << "a stale attribution survived the frame";
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
    EXPECT_GE(md.stats().trianglesCulled(), 1u);
}
