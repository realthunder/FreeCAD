// Tests for the LOD generator (docs/SceneStreaming.md §7). Like the
// SceneDump tests beside them these need no GL context and no document,
// so they live in an executable of their own.

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <vector>

#include "Gui/Renderer/MeshSimplify.h"

namespace
{

/// A grid of quads on the z = 0 plane, unmerged: every triangle carries
/// its own three vertices, which is how a CAD tessellation actually
/// arrives and the input a topology-based decimator handles worst.
struct SoupGrid {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<int32_t> triangles;
    std::vector<int32_t> lines;

    explicit SoupGrid(int n, float span = 1.0f)
    {
        const float step = span / float(n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                const float x0 = float(i) * step, x1 = x0 + step;
                const float y0 = float(j) * step, y1 = y0 + step;
                const float quad[6][3] = {{x0, y0, 0.0f}, {x1, y0, 0.0f},
                                          {x1, y1, 0.0f}, {x0, y0, 0.0f},
                                          {x1, y1, 0.0f}, {x0, y1, 0.0f}};
                for (const auto &p : quad) {
                    const int32_t base = int32_t(positions.size() / 3);
                    positions.insert(positions.end(), {p[0], p[1], p[2]});
                    normals.insert(normals.end(), {0.0f, 0.0f, 1.0f});
                    triangles.push_back(base);
                }
            }
        }
    }

    Render::MeshData mesh() const
    {
        Render::MeshData m;
        m.numVertices = int(positions.size() / 3);
        m.positions = positions.data();
        m.normals = normals.data();
        m.triangleIndices = triangles.data();
        m.numTriangleIndices = int(triangles.size());
        if (!lines.empty()) {
            m.lineIndices = lines.data();
            m.numLineIndices = int(lines.size());
        }
        return m;
    }
};

}  // namespace

TEST(MeshSimplify, collapsesUnmergedSoupAndKeepsTheSurface)
{
    const SoupGrid grid(16);
    const Render::MeshData src = grid.mesh();
    ASSERT_EQ(src.numVertices, 16 * 16 * 6);

    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(src, 0.25f, out));

    // Four cells across at a quarter of a unit span, so the surface
    // survives as a handful of vertices rather than 1536.
    EXPECT_LE(out.numVertices(), 25);
    EXPECT_GT(out.numVertices(), 0);
    EXPECT_FALSE(out.triangleIndices.empty());
    EXPECT_EQ(out.triangleIndices.size() % 3, 0u);

    // Every index addresses a vertex that exists.
    for (int32_t i : out.triangleIndices) {
        EXPECT_GE(i, 0);
        EXPECT_LT(i, out.numVertices());
    }
    // The plane is still the plane: representatives are averages of
    // coplanar vertices, so nothing may leave z = 0.
    for (size_t v = 0; v + 2 < out.positions.size(); v += 3)
        EXPECT_NEAR(out.positions[v + 2], 0.0f, 1e-5f);
}

TEST(MeshSimplify, dropsTrianglesThatCollapsedRatherThanEmittingSlivers)
{
    const SoupGrid grid(16);
    const Render::MeshData src = grid.mesh();
    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(src, 0.25f, out));

    // A degenerate triangle costs the backend a cull every frame and
    // draws nothing, so none may survive the remap.
    for (size_t i = 0; i + 2 < out.triangleIndices.size(); i += 3) {
        const int32_t a = out.triangleIndices[i];
        const int32_t b = out.triangleIndices[i + 1];
        const int32_t c = out.triangleIndices[i + 2];
        EXPECT_NE(a, b);
        EXPECT_NE(b, c);
        EXPECT_NE(a, c);
    }
}

TEST(MeshSimplify, refusesWhenNothingWouldMerge)
{
    // Three vertices no cell can bring together: the level would be a
    // copy of the mesh, and a rung identical to the one above it costs a
    // fetch and a key to change nothing.
    const std::vector<float> positions = {0.0f, 0.0f, 0.0f,
                                          1.0f, 0.0f, 0.0f,
                                          0.0f, 1.0f, 0.0f};
    const std::vector<int32_t> triangles = {0, 1, 2};
    Render::MeshData src;
    src.numVertices = 3;
    src.positions = positions.data();
    src.triangleIndices = triangles.data();
    src.numTriangleIndices = 3;

    Render::SimplifiedMesh out;
    EXPECT_FALSE(Render::simplifyMesh(src, 1.0e-6f, out));
}

TEST(MeshSimplify, weldingDuplicatesAloneCountsAsAReduction)
{
    // A tessellation arrives as soup: adjacent quads repeat the corner
    // they share, so a cell far smaller than any feature still merges
    // those exact duplicates. That is a real rung -- roughly half the
    // vertices for none of the shape -- and refusing it because "no
    // feature was removed" would throw away the cheapest level there is.
    const SoupGrid grid(4, 1.0f);
    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(grid.mesh(), 1.0e-4f, out));
    EXPECT_LT(out.numVertices(), grid.mesh().numVertices);
    // Nothing else moved: welding coincident vertices is exact.
    for (size_t v = 0; v + 2 < out.positions.size(); v += 3)
        EXPECT_NEAR(out.positions[v + 2], 0.0f, 1e-6f);
}

TEST(MeshSimplify, isTranslationInvariantBecauseTheGridIsAnchoredToTheMesh)
{
    // The clustering grid is anchored at the mesh's own minimum corner,
    // not the world origin, so moving a mesh — including into the
    // negative octant, where floor and truncation part ways — must
    // change nothing but the positions, and those by exactly the move.
    const SoupGrid grid(16);
    SoupGrid moved(16);
    const float shift[3] = {-5.3f, 7.1f, -2.9f};
    for (size_t v = 0; v < moved.positions.size(); v += 3) {
        moved.positions[v] += shift[0];
        moved.positions[v + 1] += shift[1];
        moved.positions[v + 2] += shift[2];
    }

    Render::SimplifiedMesh a, b;
    ASSERT_TRUE(Render::simplifyMesh(grid.mesh(), 0.25f, a));
    ASSERT_TRUE(Render::simplifyMesh(moved.mesh(), 0.25f, b));
    ASSERT_EQ(a.positions.size(), b.positions.size());
    EXPECT_EQ(a.triangleIndices, b.triangleIndices);
    for (size_t v = 0; v < a.positions.size(); v += 3) {
        EXPECT_NEAR(a.positions[v] + shift[0], b.positions[v], 1e-4f);
        EXPECT_NEAR(a.positions[v + 1] + shift[1], b.positions[v + 1], 1e-4f);
        EXPECT_NEAR(a.positions[v + 2] + shift[2], b.positions[v + 2], 1e-4f);
    }
}

TEST(MeshSimplify, survivesAMeshFarFromTheOrigin)
{
    // A unit part a million units out: dividing absolute coordinates by
    // the cell size would exhaust float precision (and eventually
    // overflow the cell index), so vertices would quantize arbitrarily.
    // Anchored to the mesh, the far copy must simplify exactly like the
    // one at the origin.
    const SoupGrid grid(16);
    SoupGrid distant(16);
    for (size_t v = 0; v < distant.positions.size(); v += 3)
        distant.positions[v] += 1.0e6f;

    Render::SimplifiedMesh a, b;
    ASSERT_TRUE(Render::simplifyMesh(grid.mesh(), 0.25f, a));
    ASSERT_TRUE(Render::simplifyMesh(distant.mesh(), 0.25f, b));
    EXPECT_EQ(a.positions.size(), b.positions.size());
    EXPECT_EQ(a.triangleIndices, b.triangleIndices);
}

TEST(MeshSimplify, refusesDegenerateInput)
{
    Render::MeshData empty;
    Render::SimplifiedMesh out;
    EXPECT_FALSE(Render::simplifyMesh(empty, 0.5f, out));

    const SoupGrid grid(4);
    Render::MeshData noTris = grid.mesh();
    noTris.triangleIndices = nullptr;
    noTris.numTriangleIndices = 0;
    EXPECT_FALSE(Render::simplifyMesh(noTris, 0.5f, out));

    // A cell size of zero would divide by it.
    EXPECT_FALSE(Render::simplifyMesh(grid.mesh(), 0.0f, out));
}

TEST(MeshSimplify, isDeterministicBecauseContentAddressingDependsOnIt)
{
    // Two runs must agree byte for byte: a level whose bytes varied per
    // run would hash to a different key every time and never hit a
    // cache (docs/SceneStreaming.md §9, invariant 2).
    const SoupGrid grid(12);
    Render::SimplifiedMesh a, b;
    ASSERT_TRUE(Render::simplifyMesh(grid.mesh(), 0.3f, a));
    ASSERT_TRUE(Render::simplifyMesh(grid.mesh(), 0.3f, b));
    EXPECT_EQ(a.positions, b.positions);
    EXPECT_EQ(a.normals, b.normals);
    EXPECT_EQ(a.triangleIndices, b.triangleIndices);
    EXPECT_EQ(a.lineIndices, b.lineIndices);
}

TEST(MeshSimplify, coarserLevelsAreStrictlyCheaper)
{
    const SoupGrid grid(24);
    const float bbox[6] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f};

    size_t previous = 0;
    for (uint32_t level = 0; level < 4; ++level) {
        const float cell = Render::levelCellSize(bbox, level);
        ASSERT_GT(cell, 0.0f);
        Render::SimplifiedMesh out;
        if (!Render::simplifyMesh(grid.mesh(), cell, out))
            break;   // ran out of room to merge; the exact mesh is next
        const size_t count = out.positions.size();
        EXPECT_GT(count, previous) << "level " << level << " must refine";
        previous = count;
        // Every rung must stay cheaper than the mesh it stands in for,
        // or it is not a rung.
        EXPECT_LT(out.numVertices(), grid.mesh().numVertices);
    }
    EXPECT_GT(previous, 0u);
}

TEST(MeshSimplify, dedupesEdgesInsteadOfDrawingEachOneAgain)
{
    SoupGrid grid(8);
    // Vertices 0/1/2 are one quad's corners, 0.125 apart. The cell size
    // has to be small enough that they land in *different* cells --
    // otherwise every edge collapses to a point, none survive, and the
    // assertion below passes over an empty list without checking
    // anything. (It did, the first time.)
    grid.lines = {0, 1, 0, 1, 1, 2};
    const Render::MeshData src = grid.mesh();

    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(src, 0.1f, out));
    ASSERT_EQ(out.lineIndices.size() % 2, 0u);
    // Three edges in, two distinct pairs out: the repeat is dropped.
    EXPECT_EQ(out.lineIndices.size(), 4u);

    std::set<std::pair<int32_t, int32_t>> seen;
    for (size_t i = 0; i + 1 < out.lineIndices.size(); i += 2) {
        const auto edge =
            std::make_pair(out.lineIndices[i], out.lineIndices[i + 1]);
        EXPECT_NE(edge.first, edge.second);
        EXPECT_TRUE(seen.insert(edge).second) << "duplicate edge emitted";
    }
}

TEST(MeshSimplify, fillPointsAMeshDataAtTheGeneratedArrays)
{
    const SoupGrid grid(8);
    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(grid.mesh(), 0.3f, out));

    Render::MeshData view;
    out.fill(view);
    EXPECT_EQ(view.numVertices, out.numVertices());
    EXPECT_EQ(view.positions, out.positions.data());
    EXPECT_EQ(view.numTriangleIndices, int(out.triangleIndices.size()));
    // A generated rung carries no element map, so it answers no
    // sub-element query -- §9 invariant 6, and the reason picking on it
    // falls back to the whole object as it does on the box.
    EXPECT_TRUE(view.triangleParts.empty());
    EXPECT_TRUE(view.lineParts.empty());
    EXPECT_EQ(view.pointIndices, nullptr);
}
