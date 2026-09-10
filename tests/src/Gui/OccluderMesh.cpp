// Tests for the coarse occluder hulls (docs/FarFieldProxies.md 12.16) --
// the geometry the CPU occlusion pass rasterizes when it is asked not to
// spend its whole triangle budget on a handful of full-detail meshes.
//
// **The claim under test is one-directional**, and it is the same one the
// buffer itself carries: a hull may make the pass hide LESS than the mesh
// it stands for, and may never make it hide more. Everything downstream
// of that -- a node wrongly skipped, geometry missing from the screen --
// is that inequality broken, and this workstream has spent three
// sections restoring it once already.
//
// So the central test here is not "the hull looks like the mesh". It is
// an inequality run through the real pass: at equal occluders, every
// draw the coarse arm hides must also be hidden by the exact arm. And it
// is paired with an engagement check, because a mechanism that quietly
// did nothing would satisfy the inequality perfectly.

#include <gtest/gtest.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

#include "Gui/Renderer/MaskedOcclusion.h"
#include "Gui/Renderer/MeshSimplify.h"
#include "Gui/Renderer/OccluderMesh.h"

using namespace Render;

namespace
{

/// GL-layout (column-major) perspective: the camera looks down -Z and
/// `P[11]` is -1, which is the convention the desktop backend hands in.
void perspectiveRH(float *P, float fovyDeg, float aspect, float znear,
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

/// The same projection built left-handed, as bx does: the camera looks
/// down +Z and `P[11]` is +1. Present because the recede direction is
/// derived from exactly this element, and a derivation that is only ever
/// run against one sign has not been tested.
void perspectiveLH(float *P, float fovyDeg, float aspect, float znear,
                   float zfar)
{
    for (int i = 0; i < 16; ++i)
        P[i] = 0.0f;
    const float f = 1.0f / std::tan(fovyDeg * 3.14159265358979f / 360.0f);
    P[0] = f / aspect;
    P[5] = f;
    P[10] = (zfar + znear) / (zfar - znear);
    P[11] = 1.0f;
    P[14] = -2.0f * zfar * znear / (zfar - znear);
}

/// The camera, backed off by \a dist along the axis it looks down.
///
/// WARNING: `sign` is +1 for the right-handed pairing and -1 for the
/// left-handed one, and it moves the *camera*, not the axis: the view
/// matrix is a translation either way, so view-space z is
/// `worldZ - dist` for the first and `worldZ + dist` for the second.
/// Getting this backwards puts the whole scene behind the camera, where
/// nothing is an occluder and every assertion below passes vacuously --
/// which is how the first draft of this file "tested" both handednesses
/// while only ever exercising one.
void viewAt(float *V, float dist, float sign)
{
    for (int i = 0; i < 16; ++i)
        V[i] = 0.0f;
    V[0] = V[5] = V[10] = V[15] = 1.0f;
    V[14] = -dist * sign;
}

/// A dome of \a n by \a n quads over [-half, half], bulging \a rise
/// towards the camera side -- a convex surface, which is the case a
/// chord across it lands *inside* of.
///
/// WARNING: \a n has to be large enough that the clustering grid merges
/// anything at all. The grid is the diagonal over `8 << level`, so at
/// level 2 it is about `0.088 * half` while the vertex spacing is
/// `2 * half / n`: below n ~ 23 every vertex is alone in its cell and
/// simplifyMesh correctly refuses to produce a rung. A test built on
/// such a mesh does not test a coarser hull, it tests the refusal --
/// which is a real behaviour, and has its own test.
void dome(int n, float half, float rise, float z, float toward,
          std::vector<float> &pos, std::vector<int32_t> &idx)
{
    pos.clear();
    idx.clear();
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const float u = -half + 2.0f * half * float(i) / float(n);
            const float v = -half + 2.0f * half * float(j) / float(n);
            const float r = std::sqrt(u * u + v * v) / (half * 1.4143f);
            pos.push_back(u);
            pos.push_back(v);
            pos.push_back(z + toward * rise * (1.0f - r * r));
        }
    }
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int32_t a = int32_t(j * (n + 1) + i);
            const int32_t b = a + 1;
            const int32_t c = a + int32_t(n + 1);
            const int32_t d = c + 1;
            idx.insert(idx.end(), {a, b, d});
            idx.insert(idx.end(), {a, d, c});
        }
    }
}

/// A draw list with real meshes behind it, each with a content id -- a
/// hull is keyed by that id, so a builder that left it zero would test
/// the fallback path and nothing else.
struct SceneBuilder {
    std::deque<std::vector<float>> positions;
    std::deque<std::vector<int32_t>> indices;
    DrawCallList draws;
    uint64_t nextId = 1;

    DrawCall &add(std::vector<float> pos, std::vector<int32_t> idx)
    {
        positions.push_back(std::move(pos));
        indices.push_back(std::move(idx));
        auto mesh = std::make_shared<MeshData>();
        mesh->cacheId = nextId++;
        mesh->numVertices = int(positions.back().size() / 3);
        mesh->positions = positions.back().data();
        mesh->triangleIndices = indices.back().data();
        mesh->numTriangleIndices = int(indices.back().size());
        DrawCall d;
        d.mesh = mesh;
        d.objectKey = mesh->cacheId;
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

    DrawCall &addBox(float cx, float cy, float cz, float half)
    {
        std::vector<float> p;
        for (int c = 0; c < 8; ++c) {
            p.push_back(cx + ((c & 1) ? half : -half));
            p.push_back(cy + ((c & 2) ? half : -half));
            p.push_back(cz + ((c & 4) ? half : -half));
        }
        static const int32_t bi[36] = {
            0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4,
            2, 6, 7, 2, 7, 3, 0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7, 5};
        return add(std::move(p), std::vector<int32_t>(bi, bi + 36));
    }
};

/// The mask the pass produces for a scene, under one configuration.
std::vector<uint8_t> runPass(SceneBuilder &scene, const MaskedCullConfig &conf,
                             const float *V, const float *P, int w, int h,
                             MaskedCullStats *stats = nullptr,
                             uint32_t maxPerCell = 1)
{
    std::vector<ProxyInstance> inst;
    proxyInstances(scene.draws, inst);
    ProxyParams params;
    // Culling is per node, and a scene that fits one cell has only the
    // root -- which can never be hidden by its own contents. The
    // benchmark scenes subdivide on their own; this one is made to.
    //
    // WARNING: At 1 every leaf holds a single instance, so the node walk
    // *is* already a per-instance walk and the per-instance pass
    // correctly finds nothing left to ask. A test of that pass has to
    // group several instances into a node, which is what the real
    // hierarchy does at its default of 32.
    params.maxPerCell = maxPerCell;
    ProxyHierarchy index;
    index.build(inst, params);

    MaskedOccluderPass pass;
    pass.configure(conf);
    // Twice: the hulls are built a few per frame, so the first call is
    // the warm-up every coarse row has. The second is the measurement.
    for (int frame = 0; frame < 2; ++frame)
        pass.build(scene.draws, V, P, true, w, h);
    std::vector<uint8_t> mask(scene.draws.size(), 0);
    pass.cull(index, V, P, float(h), mask);
    if (stats)
        *stats = pass.lastFrame();
    return mask;
}

MeshData meshOf(const std::vector<float> &pos, const std::vector<int32_t> &idx)
{
    MeshData m;
    m.cacheId = 7;
    m.numVertices = int(pos.size() / 3);
    m.positions = pos.data();
    m.triangleIndices = idx.data();
    m.numTriangleIndices = int(idx.size());
    return m;
}

}  // namespace

// -----------------------------------------------------------------
// The hull itself
// -----------------------------------------------------------------

TEST(OccluderHull, IsCheaperThanTheMeshAndBoundsItsOwnError)
{
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(40, 25.0f, 6.0f, 0.0f, 1.0f, pos, idx);
    const MeshData mesh = meshOf(pos, idx);

    CoarseOccluder hull;
    ASSERT_TRUE(buildOccluderHull(mesh, 2, hull));
    EXPECT_LT(hull.triangles(), uint32_t(idx.size() / 3));
    EXPECT_EQ(hull.sourceTriangles, uint32_t(idx.size() / 3));
    EXPECT_GT(hull.displacement, 0.0f);

    // The bound is what the recede distance is computed from, so it is
    // worth checking against the geometry rather than trusting the
    // number that came back: no source vertex may be further from the
    // hull than the hull claims.
    float worst = 0.0f;
    for (size_t v = 0; v + 2 < pos.size(); v += 3) {
        float best = FLT_MAX;
        for (size_t h = 0; h + 2 < hull.positions.size(); h += 3) {
            const float dx = pos[v] - hull.positions[h];
            const float dy = pos[v + 1] - hull.positions[h + 1];
            const float dz = pos[v + 2] - hull.positions[h + 2];
            best = std::min(best, dx * dx + dy * dy + dz * dz);
        }
        worst = std::max(worst, std::sqrt(best));
    }
    EXPECT_LE(worst, hull.displacement * 1.001f + 1e-4f);
}

TEST(OccluderHull, RefusesAMeshItCannotReduce)
{
    // Two triangles: every vertex is alone in its cell, so there is no
    // rung below this one. A refusal is not an error -- the draw is then
    // rasterized from its mesh, which is what it already was.
    std::vector<float> pos = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
    std::vector<int32_t> idx = {0, 1, 2, 0, 2, 3};
    const MeshData mesh = meshOf(pos, idx);
    CoarseOccluder hull;
    EXPECT_FALSE(buildOccluderHull(mesh, 2, hull));
    EXPECT_EQ(hull.triangles(), 0u);
}

TEST(MeshSimplifyTrianglesOnly, KeepsTheSurfaceAndDropsEverythingElse)
{
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(48, 10.0f, 3.0f, 0.0f, 1.0f, pos, idx);

    // Attributes and an edge set, so that dropping them is observable.
    std::vector<float> normals(pos.size(), 0.0f);
    for (size_t i = 2; i < normals.size(); i += 3)
        normals[i] = 1.0f;
    std::vector<int32_t> lines;
    for (int32_t v = 0; v + 1 < int32_t(pos.size() / 3); ++v)
        lines.insert(lines.end(), {v, int32_t(v + 1)});

    MeshData mesh = meshOf(pos, idx);
    mesh.normals = normals.data();
    mesh.lineIndices = lines.data();
    mesh.numLineIndices = int(lines.size());

    const float cell = 1.0f;
    SimplifiedMesh full, surface;
    SimplifyOptions opts;
    ASSERT_TRUE(simplifyMesh(mesh, cell, full, opts));
    opts.trianglesOnly = true;
    ASSERT_TRUE(simplifyMesh(mesh, cell, surface, opts));

    // KEY: The flag changes what the rung CONTAINS, not where its surface
    // sits. If these ever diverge, a hull stops being comparable with
    // the level the same code publishes for display.
    //
    // The triangles are identical outright. The positions are a *prefix*
    // rather than an equal array, and the difference is the point: the
    // full path appends the representatives its edge and point groups
    // need after the surface's, so a mesh with an edge set carries
    // vertices no triangle indexes. That tail is exactly what a hull
    // does not have to hold.
    EXPECT_EQ(surface.triangleIndices, full.triangleIndices);
    ASSERT_LE(surface.positions.size(), full.positions.size());
    EXPECT_TRUE(std::equal(surface.positions.begin(), surface.positions.end(),
                           full.positions.begin()));
    EXPECT_LT(surface.positions.size(), full.positions.size());
    EXPECT_TRUE(surface.normals.empty());
    EXPECT_TRUE(surface.lineIndices.empty());
    EXPECT_FALSE(full.normals.empty());
    EXPECT_FALSE(full.lineIndices.empty());
}

// -----------------------------------------------------------------
// The cache
// -----------------------------------------------------------------

namespace
{

CoarseOccluderConfig cacheConf(uint32_t builds = 8)
{
    CoarseOccluderConfig c;
    c.enabled = true;
    c.level = 2;
    c.minTriangles = 16;
    c.buildsPerFrame = builds;
    return c;
}

}  // namespace

TEST(OccluderHullCache, BuildsOnceAndServesTheSameHull)
{
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(48, 10.0f, 3.0f, 0.0f, 1.0f, pos, idx);
    MeshData mesh = meshOf(pos, idx);

    CoarseOccluderCache cache;
    cache.configure(cacheConf());
    EXPECT_EQ(cache.find(mesh), nullptr);

    std::vector<const MeshData *> want {&mesh};
    cache.build(want, 1);
    EXPECT_EQ(cache.stats().built, 1u);
    const CoarseOccluder *first = cache.find(mesh);
    ASSERT_NE(first, nullptr);

    // A second frame asking for the same mesh builds nothing.
    cache.build(want, 1);
    EXPECT_EQ(cache.stats().built, 0u);
    EXPECT_EQ(cache.find(mesh), first);
}

TEST(OccluderHullCache, ReplacesAHullWhenTheMeshIsRefilled)
{
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(48, 10.0f, 3.0f, 0.0f, 1.0f, pos, idx);
    MeshData mesh = meshOf(pos, idx);

    CoarseOccluderCache cache;
    cache.configure(cacheConf());
    std::vector<const MeshData *> want {&mesh};
    cache.build(want, 1);
    ASSERT_NE(cache.find(mesh), nullptr);

    // The ladder fills one mesh object under one cache id, so a bumped
    // generation is new geometry wearing the old key. Serving the old
    // hull for it would be a hull of something nobody is drawing.
    mesh.generation = 1;
    EXPECT_EQ(cache.find(mesh), nullptr);
    cache.build(want, 1);
    EXPECT_EQ(cache.stats().built, 1u);
    EXPECT_NE(cache.find(mesh), nullptr);
    EXPECT_EQ(cache.stats().entries, 1u);
}

TEST(OccluderHullCache, SpendsAtMostItsPerFrameBudgetAndSaysWhatIsLeft)
{
    std::deque<std::vector<float>> pos;
    std::deque<std::vector<int32_t>> idx;
    std::deque<MeshData> meshes;
    std::vector<const MeshData *> want;
    for (int i = 0; i < 5; ++i) {
        pos.emplace_back();
        idx.emplace_back();
        dome(48, 10.0f + float(i), 3.0f, 0.0f, 1.0f, pos.back(), idx.back());
        meshes.push_back(meshOf(pos.back(), idx.back()));
        meshes.back().cacheId = uint64_t(100 + i);
        want.push_back(&meshes.back());
    }

    CoarseOccluderCache cache;
    cache.configure(cacheConf(2));
    cache.build(want, 4);
    EXPECT_EQ(cache.stats().built, 2u);
    EXPECT_EQ(cache.stats().pending, 3u);
    cache.build(want, 4);
    EXPECT_EQ(cache.stats().built, 2u);
    EXPECT_EQ(cache.stats().pending, 1u);
    cache.build(want, 4);
    EXPECT_EQ(cache.stats().built, 1u);
    EXPECT_EQ(cache.stats().pending, 0u);
    EXPECT_EQ(cache.stats().entries, 5u);
}

TEST(OccluderHullCache, DropsTheLeastRecentlyUsedUnderItsCap)
{
    std::deque<std::vector<float>> pos;
    std::deque<std::vector<int32_t>> idx;
    std::deque<MeshData> meshes;
    std::vector<const MeshData *> want;
    for (int i = 0; i < 4; ++i) {
        pos.emplace_back();
        idx.emplace_back();
        dome(48, 10.0f + float(i), 3.0f, 0.0f, 1.0f, pos.back(), idx.back());
        meshes.push_back(meshOf(pos.back(), idx.back()));
        meshes.back().cacheId = uint64_t(200 + i);
        want.push_back(&meshes.back());
    }

    CoarseOccluderCache cache;
    CoarseOccluderConfig conf = cacheConf();
    cache.configure(conf);
    cache.build(want, 1);
    ASSERT_EQ(cache.stats().entries, 4u);
    const size_t held = cache.stats().bytes;
    ASSERT_GT(held, 0u);

    // Ask for the last one, so it is the most recently used, then cap the
    // cache below what four hulls need.
    ASSERT_NE(cache.find(meshes.back()), nullptr);
    conf.memoryCap = held / 2;
    cache.configure(conf);
    EXPECT_LE(cache.stats().bytes, conf.memoryCap);
    EXPECT_LT(cache.stats().entries, 4u);
    EXPECT_NE(cache.find(meshes.back()), nullptr);
}

TEST(OccluderHullCache, HoldsNothingWhileOff)
{
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(48, 10.0f, 3.0f, 0.0f, 1.0f, pos, idx);
    MeshData mesh = meshOf(pos, idx);

    CoarseOccluderCache cache;
    CoarseOccluderConfig conf = cacheConf();
    cache.configure(conf);
    std::vector<const MeshData *> want {&mesh};
    cache.build(want, 1);
    ASSERT_NE(cache.find(mesh), nullptr);

    conf.enabled = false;
    cache.configure(conf);
    EXPECT_EQ(cache.find(mesh), nullptr);
    cache.build(want, 1);
    EXPECT_EQ(cache.stats().entries, 0u);
    EXPECT_EQ(cache.stats().bytes, 0u);
}

// -----------------------------------------------------------------
// The inequality, run through the pass
// -----------------------------------------------------------------

namespace
{

/// A dome occluder with a field of small boxes behind it, which is the
/// shape of the case that matters: a large tessellated surface standing
/// in front of many small draws.
void occludedScene(SceneBuilder &scene, float side)
{
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(48, 25.0f, 5.0f, 0.0f, side, pos, idx);
    scene.add(pos, idx);
    for (int j = -3; j <= 3; ++j)
        for (int i = -3; i <= 3; ++i)
            scene.addBox(float(i) * 5.0f, float(j) * 5.0f, -side * 30.0f,
                         1.2f);
}

}  // namespace

TEST(CoarseOccluderPass, HidesNoMoreThanTheMeshItStandsFor)
{
    float V[16], P[16];
    viewAt(V, 60.0f, 1.0f);
    perspectiveRH(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    occludedScene(scene, 1.0f);

    MaskedCullConfig exact;
    // Room for every occluder in both arms: this test is about what a
    // hull CLAIMS, and a budget that admitted different occluders in the
    // two arms would be measuring the other half of the mechanism.
    exact.triangleBudget = 1u << 22;
    MaskedCullStats exactStats, coarseStats;
    const auto exactMask =
            runPass(scene, exact, V, P, 512, 512, &exactStats);

    MaskedCullConfig coarse = exact;
    coarse.coarse = cacheConf();
    coarse.coarseBias = 1.0f;
    const auto coarseMask =
            runPass(scene, coarse, V, P, 512, 512, &coarseStats);

    // The mechanism has to have run, or the inequality below is a
    // statement about nothing.
    ASSERT_GT(coarseStats.coarseDraws, 0u);
    ASSERT_GT(coarseStats.coarseTrianglesSaved, 0u);
    EXPECT_LT(coarseStats.occluderTriangles, exactStats.occluderTriangles);
    ASSERT_GT(exactStats.nodesHidden, 0u);

    // KEY: The inequality. Every draw the hull arm hides must be hidden by
    // the mesh arm too. The converse is allowed and expected: a hull
    // that has receded hides less.
    ASSERT_EQ(exactMask.size(), coarseMask.size());
    for (size_t i = 0; i < exactMask.size(); ++i)
        EXPECT_TRUE(!coarseMask[i] || exactMask[i])
                << "draw " << i << " hidden by the hull but not by the mesh";
}

TEST(CoarseOccluderPass, BuysAdmissionsWithTheBudgetItSaves)
{
    float V[16], P[16];
    viewAt(V, 60.0f, 1.0f);
    perspectiveRH(P, 60.0f, 1.0f, 1.0f, 400.0f);

    // Several large occluders and a budget that cannot afford them all
    // at full detail -- the situation measured on the rack model, where
    // 37 draws spent the whole allowance and 1285 candidates never got
    // in (section 12.15).
    SceneBuilder scene;
    for (int i = 0; i < 6; ++i) {
        std::vector<float> pos;
        std::vector<int32_t> idx;
        dome(40, 20.0f, 4.0f, -6.0f * float(i), 1.0f, pos, idx);
        scene.add(pos, idx);
    }

    MaskedCullConfig conf;
    conf.triangleBudget = 4000;
    MaskedCullStats exactStats, coarseStats;
    runPass(scene, conf, V, P, 512, 512, &exactStats);

    conf.coarse = cacheConf();
    runPass(scene, conf, V, P, 512, 512, &coarseStats);

    EXPECT_GT(coarseStats.occluderDraws, exactStats.occluderDraws);
    EXPECT_LT(coarseStats.occludersDropped, exactStats.occludersDropped);
}

TEST(CoarseOccluderPass, DerivesTheRecedeDirectionFromTheMatrices)
{
    // The direction itself, against both conventions and both projection
    // kinds. A sign error here would push every hull TOWARDS the camera
    // and invent occlusion everywhere, so this is checked where it is
    // decided rather than only through the scene below -- which can
    // exercise one convention, for a reason worth writing down:
    //
    // WARNING: The pass around this is right-handed only. `sightBounds`
    // reads a box's depth as `-vz`, so a left-handed view matrix makes
    // every candidate report Offscreen and the pass rasterizes nothing
    // at all. That is a limit of the culling path, not of the recede
    // derivation, and it is why the left-handed arm is tested here
    // rather than end to end.
    float V[16], P[16], dir[3];

    viewAt(V, 60.0f, 1.0f);
    perspectiveRH(P, 60.0f, 1.0f, 1.0f, 400.0f);
    occluderRecede(V, P, dir);
    // Right-handed: the camera looks down -Z, so away is -Z.
    EXPECT_NEAR(dir[0], 0.0f, 1e-6f);
    EXPECT_NEAR(dir[1], 0.0f, 1e-6f);
    EXPECT_NEAR(dir[2], -1.0f, 1e-6f);

    perspectiveLH(P, 60.0f, 1.0f, 1.0f, 400.0f);
    occluderRecede(V, P, dir);
    // Left-handed: the same view row, the opposite sign of w.
    EXPECT_NEAR(dir[2], 1.0f, 1e-6f);

    // Orthographic has no w to read, so the depth gradient answers. The
    // GL convention puts P[10] negative, and away is again -Z.
    for (int i = 0; i < 16; ++i)
        P[i] = 0.0f;
    P[0] = P[5] = 1.0f;
    P[10] = -2.0f / 399.0f;
    P[14] = -401.0f / 399.0f;
    P[15] = 1.0f;
    occluderRecede(V, P, dir);
    EXPECT_NEAR(dir[2], -1.0f, 1e-6f);

    // A camera rotated off the axes: the direction has to follow the
    // camera and stay a unit vector, not merely be one of the axes.
    const float c = std::cos(0.7f), s = std::sin(0.7f);
    for (int i = 0; i < 16; ++i)
        V[i] = 0.0f;
    V[0] = c;
    V[8] = s;
    V[2] = -s;
    V[10] = c;
    V[5] = V[15] = 1.0f;
    perspectiveRH(P, 60.0f, 1.0f, 1.0f, 400.0f);
    occluderRecede(V, P, dir);
    EXPECT_NEAR(std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]),
                1.0f, 1e-5f);
    EXPECT_NEAR(dir[0], s, 1e-5f);
    EXPECT_NEAR(dir[2], -c, 1e-5f);
}

TEST(CoarseOccluderPass, ABiasedHullHidesLessThanOneLeftWhereItWasBuilt)
{
    // The direction test above says which way the hull is pushed; this
    // says the pass actually pushes it, and that pushing it away costs
    // culling rather than adding it.
    float V[16], P[16];
    viewAt(V, 60.0f, 1.0f);
    perspectiveRH(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(48, 25.0f, 5.0f, 0.0f, 1.0f, pos, idx);
    scene.add(pos, idx);
    // Just behind the dome, well inside its silhouette.
    for (int i = -2; i <= 2; ++i)
        scene.addBox(float(i) * 4.0f, 0.0f, -2.0f, 0.6f);

    MaskedCullConfig conf;
    conf.triangleBudget = 1u << 22;
    conf.coarse = cacheConf();
    conf.coarseBias = 0.0f;
    MaskedCullStats sitting, receded;
    const auto atRest = runPass(scene, conf, V, P, 512, 512, &sitting);
    ASSERT_GT(sitting.coarseDraws, 0u);

    size_t hiddenAtRest = 0;
    for (size_t i = 1; i < atRest.size(); ++i)
        hiddenAtRest += atRest[i] ? 1 : 0;
    ASSERT_GT(hiddenAtRest, 0u)
            << "nothing was hidden at all, so the comparison below cannot "
               "observe the bias";

    // Recede by many times the hull's own error: far enough to pass the
    // boxes, so a correct sign frees them and a reversed one buries them.
    conf.coarseBias = 400.0f;
    const auto pushed = runPass(scene, conf, V, P, 512, 512, &receded);
    size_t hiddenPushed = 0;
    for (size_t i = 1; i < pushed.size(); ++i)
        hiddenPushed += pushed[i] ? 1 : 0;
    EXPECT_LT(hiddenPushed, hiddenAtRest)
            << "the bias did not move the hull away from the camera";
}

// -----------------------------------------------------------------
// Asking per instance rather than per node (section 12.17)
// -----------------------------------------------------------------

TEST(PerInstanceCull, HidesTheInvisibleNeighboursOfAVisibleDraw)
{
    // The case the mode exists for: a group holding one draw that
    // sticks out past the occluder and several that do not. The node
    // box covers all of them, so the node answers visible and the whole
    // group is drawn -- which is what 91% of the still-drawn draws on
    // the rack model turned out to be.
    float V[16], P[16];
    viewAt(V, 60.0f, 1.0f);
    perspectiveRH(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(48, 25.0f, 5.0f, 0.0f, 1.0f, pos, idx);
    scene.add(pos, idx);                          // draw 0, the occluder
    // Hidden behind the dome, clustered together.
    const size_t firstHidden = scene.draws.size();
    for (int i = -2; i <= 2; ++i)
        scene.addBox(float(i) * 3.0f, 0.0f, -8.0f, 0.9f);
    // In plain sight beside them, and near enough to share their cells.
    const size_t visible = scene.draws.size();
    scene.addBox(0.0f, 0.0f, 40.0f, 1.0f);

    MaskedCullConfig conf;
    conf.triangleBudget = 1u << 22;
    // Grouped, as the real hierarchy groups: the hidden boxes and the
    // visible one share a node, so the node answers visible for all of
    // them and only a finer test can separate them.
    MaskedCullStats nodeStats, instStats;
    const auto byNode = runPass(scene, conf, V, P, 512, 512, &nodeStats, 32);

    conf.testInstances = true;
    const auto byInstance =
            runPass(scene, conf, V, P, 512, 512, &instStats, 32);

    // The draw in front may never be hidden by either.
    EXPECT_FALSE(byNode[visible]);
    EXPECT_FALSE(byInstance[visible]);

    // Everything the node walk hid stays hidden: this is a finer test of
    // the same buffer, not a different question.
    ASSERT_EQ(byNode.size(), byInstance.size());
    for (size_t i = 0; i < byNode.size(); ++i)
        EXPECT_TRUE(!byNode[i] || byInstance[i])
                << "draw " << i << " was hidden per node and not per instance";

    // And it finds some the node walk could not.
    size_t hidNode = 0, hidInstance = 0;
    for (size_t i = firstHidden; i < visible; ++i) {
        hidNode += byNode[i] ? 1 : 0;
        hidInstance += byInstance[i] ? 1 : 0;
    }
    EXPECT_GT(hidInstance, hidNode);
    EXPECT_GT(instStats.instancesHiddenAlone, 0u);
    EXPECT_GT(instStats.instancesTested, 0u);
}

TEST(PerInstanceCull, NeverHidesADrawThatReachesThePixels)
{
    // The direction that matters. A field of boxes in front of the
    // occluder, none of which may be hidden however finely they are
    // tested -- an instance test is still a test against the same
    // conservative buffer, and a tie answers visible.
    float V[16], P[16];
    viewAt(V, 60.0f, 1.0f);
    perspectiveRH(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(48, 25.0f, 5.0f, -30.0f, 1.0f, pos, idx);
    scene.add(pos, idx);
    const size_t front = scene.draws.size();
    for (int j = -2; j <= 2; ++j)
        for (int i = -2; i <= 2; ++i)
            scene.addBox(float(i) * 6.0f, float(j) * 6.0f, 10.0f, 1.5f);

    MaskedCullConfig conf;
    conf.triangleBudget = 1u << 22;
    conf.testInstances = true;
    MaskedCullStats stats;
    const auto mask = runPass(scene, conf, V, P, 512, 512, &stats);
    for (size_t i = front; i < mask.size(); ++i)
        EXPECT_FALSE(mask[i]) << "draw " << i << " is in front and was hidden";
}

TEST(PerInstanceCull, DoesNotRetestANodeThatIsOneInstance)
{
    // A node holding one instance and nothing below it has that
    // instance's box for its content box, so its test has already been
    // taken. Re-asking would be a projection spent to learn what is on
    // the stack -- and with maxPerCell 1 the scene below is mostly such
    // nodes, so the saving is the common case rather than a corner.
    float V[16], P[16];
    viewAt(V, 60.0f, 1.0f);
    perspectiveRH(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    std::vector<float> pos;
    std::vector<int32_t> idx;
    dome(48, 25.0f, 5.0f, 0.0f, 1.0f, pos, idx);
    scene.add(pos, idx);
    for (int i = -3; i <= 3; ++i)
        scene.addBox(float(i) * 7.0f, 0.0f, 30.0f, 1.0f);

    MaskedCullConfig conf;
    conf.triangleBudget = 1u << 22;
    conf.testInstances = true;
    MaskedCullStats stats;
    runPass(scene, conf, V, P, 512, 512, &stats);
    EXPECT_GT(stats.instancesRedundant, 0u);
}

TEST(PerInstanceCull, CostsNothingAndChangesNothingWhileOff)
{
    float V[16], P[16];
    viewAt(V, 60.0f, 1.0f);
    perspectiveRH(P, 60.0f, 1.0f, 1.0f, 400.0f);

    SceneBuilder scene;
    occludedScene(scene, 1.0f);

    MaskedCullConfig conf;
    conf.triangleBudget = 1u << 22;
    MaskedCullStats stats;
    runPass(scene, conf, V, P, 512, 512, &stats);
    EXPECT_EQ(stats.instancesTested, 0u);
    EXPECT_EQ(stats.instancesHiddenAlone, 0u);
    EXPECT_EQ(stats.instanceMs, 0.0f);
}

// -----------------------------------------------------------------
// How many workers the pass asks for (section 12.18)
// -----------------------------------------------------------------

TEST(OccluderWorkers, CountsCoresRatherThanSiblings)
{
    const uint32_t physical = physicalCoreCount();
    const unsigned logical = std::thread::hardware_concurrency();

    // 0 is "the platform would not say", which is allowed. What is not
    // allowed is claiming more cores than there are threads to run on.
    if (physical > 0 && logical > 0)
        EXPECT_LE(physical, logical);

    const uint32_t automatic = occluderWorkers(0);
    EXPECT_GE(automatic, 1u);
    EXPECT_LE(automatic, 32u);
    if (physical > 0)
        EXPECT_EQ(automatic, std::min<uint32_t>(physical, 32));

    // KEY: On an SMT machine the automatic count must come *down*. This is
    // the whole change: 8 workers on 8 cores measured a flat wall clock
    // against 14 and a third less CPU.
    if (physical > 0 && logical > physical)
        EXPECT_LT(automatic, logical);
}

TEST(OccluderWorkers, HonoursAnExplicitCountAndItsBounds)
{
    EXPECT_EQ(occluderWorkers(1), 1u);
    EXPECT_EQ(occluderWorkers(6), 6u);
    // Clamped, not trusted: these ride a view property a script can set
    // to anything.
    EXPECT_EQ(occluderWorkers(1000), 32u);
}

TEST(OccluderWorkers, IsStableAcrossCalls)
{
    // Cached after the first reading -- it is asked once a frame and the
    // Linux path reads sysfs.
    const uint32_t first = physicalCoreCount();
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(physicalCoreCount(), first);
}
