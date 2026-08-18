// Tests for the LOD generator (docs/SceneStreaming.md §7). Like the
// SceneDump tests beside them these need no GL context and no document,
// so they live in an executable of their own.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <set>
#include <tuple>
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

/// Two square faces meeting at a right-angle crease along the line
/// (1, y, 0): part 0 an n x n soup grid on z = 0 spanning [0,1]^2 in x
/// and y, part 1 the same grid stood upright on the plane x = 1. The
/// fold's vertices are carried by both parts as coincident duplicates,
/// exactly as a CAD tessellation duplicates a shared model edge.
struct SoupFold {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<int32_t> triangles;
    std::vector<std::pair<int, int>> parts;

    explicit SoupFold(int n)
    {
        addGrid(n, false);
        addGrid(n, true);
    }

    void addGrid(int n, bool upright)
    {
        const int start = int(triangles.size());
        const float step = 1.0f / float(n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                const float u0 = float(i) * step, u1 = u0 + step;
                const float v0 = float(j) * step, v1 = v0 + step;
                const float corners[6][2] = {{u0, v0}, {u1, v0}, {u1, v1},
                                             {u0, v0}, {u1, v1}, {u0, v1}};
                for (const auto &uv : corners) {
                    const int32_t base = int32_t(positions.size() / 3);
                    if (upright) {
                        positions.insert(positions.end(),
                                         {1.0f, uv[1], uv[0]});
                        normals.insert(normals.end(), {1.0f, 0.0f, 0.0f});
                    }
                    else {
                        positions.insert(positions.end(),
                                         {uv[0], uv[1], 0.0f});
                        normals.insert(normals.end(), {0.0f, 0.0f, 1.0f});
                    }
                    triangles.push_back(base);
                }
            }
        }
        parts.push_back({start, int(triangles.size()) - start});
    }

    Render::MeshData mesh() const
    {
        Render::MeshData m;
        m.numVertices = int(positions.size() / 3);
        m.positions = positions.data();
        m.normals = normals.data();
        m.triangleIndices = triangles.data();
        m.numTriangleIndices = int(triangles.size());
        m.triangleParts = parts;
        return m;
    }
};

/// The distinct positions referenced by the triangles of one part range.
static std::set<std::tuple<float, float, float>>
partPositions(const Render::SimplifiedMesh &out, std::pair<int, int> range)
{
    std::set<std::tuple<float, float, float>> result;
    for (int i = range.first; i < range.first + range.second; ++i) {
        const size_t v = size_t(out.triangleIndices[size_t(i)]) * 3;
        result.emplace(out.positions[v], out.positions[v + 1],
                       out.positions[v + 2]);
    }
    return result;
}

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

TEST(MeshSimplify, keepsTheFacePartTableThroughDecimation)
{
    // Sub-element selection is what CAD operations run on, so a rung
    // must keep the element map: entry i of the output table names the
    // same face as entry i of the input, and no output triangle may
    // span two faces.
    const SoupFold fold(16);
    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(fold.mesh(), 0.25f, out));

    ASSERT_EQ(out.triangleParts.size(), 2u);
    const auto p0 = out.triangleParts[0];
    const auto p1 = out.triangleParts[1];
    EXPECT_GT(p0.second, 0);
    EXPECT_GT(p1.second, 0);
    // The ranges partition the output triangle list in table order.
    EXPECT_EQ(p0.first, 0);
    EXPECT_EQ(p1.first, p0.second);
    EXPECT_EQ(size_t(p1.first + p1.second), out.triangleIndices.size());

    // Each face's triangles stay within a cell of that face's surface:
    // part 0 was tessellated on z = 0, part 1 on x = 1. Exactly *on* it
    // is too strong at the fold, where a cell holds both faces' vertices
    // and the shared position average sits between them -- that pull is
    // the rung's geometric error, and it is bounded by the cell size.
    for (const auto &pos : partPositions(out, p0))
        EXPECT_NEAR(std::get<2>(pos), 0.0f, 0.25f);
    for (const auto &pos : partPositions(out, p1))
        EXPECT_NEAR(std::get<0>(pos), 1.0f, 0.25f);

    // The crease survives: normals average within one element only, so
    // part 0 still faces +z and part 1 still faces +x. Welding across
    // the fold would blur both toward the 45-degree diagonal.
    for (int i = p0.first; i < p0.first + p0.second; ++i)
        EXPECT_GT(out.normals[size_t(out.triangleIndices[size_t(i)]) * 3 + 2],
                  0.99f);
    for (int i = p1.first; i < p1.first + p1.second; ++i)
        EXPECT_GT(out.normals[size_t(out.triangleIndices[size_t(i)]) * 3],
                  0.99f);

    // And all of it deterministically, tables included (invariant 2).
    Render::SimplifiedMesh again;
    ASSERT_TRUE(Render::simplifyMesh(fold.mesh(), 0.25f, again));
    EXPECT_EQ(out.positions, again.positions);
    EXPECT_EQ(out.triangleIndices, again.triangleIndices);
    EXPECT_EQ(out.triangleParts, again.triangleParts);
}

TEST(MeshSimplify, decimatedFacesStillMeetAlongTheirSharedBoundary)
{
    // Clustering per element must not open the mesh along element
    // boundaries. Positions come from a grid the whole mesh shares: the
    // coincident soup vertices both faces carry along the fold land in
    // the same cell on either side and read back the same average, so
    // the decimated faces still meet exactly -- bitwise -- where their
    // tessellations met.
    const SoupFold fold(16);
    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(fold.mesh(), 0.25f, out));
    ASSERT_EQ(out.triangleParts.size(), 2u);

    const auto a = partPositions(out, out.triangleParts[0]);
    const auto b = partPositions(out, out.triangleParts[1]);
    int shared = 0;
    for (const auto &pos : a)
        shared += b.count(pos);
    // The fold is a unit line at a quarter-unit cell: both faces must
    // reference the same handful of representatives along it.
    EXPECT_GE(shared, 4);
}

TEST(MeshSimplify, aCollapsedElementKeepsItsSlotAsAnEmptyRange)
{
    // A face smaller than a cell loses all its triangles. Its table
    // entry must survive as an empty range: element identity is the
    // *position* in the table, so dropping the entry would shift every
    // element after it onto the wrong name -- a pick that lies. An
    // empty range can cover no index, so the collapsed face is merely
    // unpickable at this rung, never misattributed.
    SoupFold fold(8);
    const int tinyStart = int(fold.triangles.size());
    const float quad[6][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
                              {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (const auto &uv : quad) {
        const int32_t base = int32_t(fold.positions.size() / 3);
        fold.positions.insert(fold.positions.end(),
                              {2.0f + uv[0] * 0.01f, uv[1] * 0.01f, 0.0f});
        fold.normals.insert(fold.normals.end(), {0.0f, 0.0f, 1.0f});
        fold.triangles.push_back(base);
    }
    fold.parts.push_back({tinyStart, int(fold.triangles.size()) - tinyStart});

    Render::MeshData src = fold.mesh();
    // The collapsed face is also the only solid one: with none of its
    // triangles surviving there is nothing left that is solid, and
    // claiming hasSolid over an empty set would send the capping pass
    // looking for geometry that is not there.
    src.hasSolid = 1;
    src.solidParts = {fold.parts[2]};

    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(src, 0.25f, out));
    ASSERT_EQ(out.triangleParts.size(), 3u);
    EXPECT_GT(out.triangleParts[0].second, 0);
    EXPECT_GT(out.triangleParts[1].second, 0);
    EXPECT_EQ(out.triangleParts[2].second, 0);
    EXPECT_EQ(out.hasSolid, 0);
    EXPECT_TRUE(out.solidParts.empty());
}

TEST(MeshSimplify, carriesTheTriangleFlagSubsetsAsRunsOverSurvivors)
{
    // Flat-versus-curved and solidness are properties of the source
    // faces and survive decimation, so the subsets carry over -- as
    // maximal runs over the surviving triangles, which here must land
    // exactly on the faces they marked.
    const SoupFold fold(16);
    Render::MeshData src = fold.mesh();
    src.nonFlatParts = {fold.parts[1]};
    src.hasSolid = 1;
    src.solidParts = {fold.parts[0]};

    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(src, 0.25f, out));
    ASSERT_EQ(out.triangleParts.size(), 2u);
    ASSERT_EQ(out.nonFlatParts.size(), 1u);
    EXPECT_EQ(out.nonFlatParts[0], out.triangleParts[1]);
    EXPECT_EQ(out.hasSolid, 1);
    ASSERT_EQ(out.solidParts.size(), 1u);
    EXPECT_EQ(out.solidParts[0], out.triangleParts[0]);

    // A wholly-solid mesh has no subset to remap and stays wholly solid.
    src.hasSolid = 2;
    src.solidParts.clear();
    ASSERT_TRUE(Render::simplifyMesh(src, 0.25f, out));
    EXPECT_EQ(out.hasSolid, 2);
    EXPECT_TRUE(out.solidParts.empty());
}

TEST(MeshSimplify, seamFilterSurvivesTheWeldAndNonSeamWins)
{
    // Four positions on a line, each a duplicated soup pair; the middle
    // two (x = 0.5 and 0.52) share a cell at 0.1, so edges reaching
    // either weld onto one output edge.
    std::vector<float> positions;
    for (float x : {0.0f, 0.0f, 0.5f, 0.5f, 0.52f, 0.52f, 1.0f, 1.0f})
        positions.insert(positions.end(), {x, 0.0f, 0.0f});
    const std::vector<int32_t> triangles = {0, 2, 6};
    // One seam edge and one non-seam edge that weld together, and one
    // seam edge that stays alone.
    const std::vector<int32_t> lines = {1, 3, 0, 4, 2, 6};
    const std::vector<int32_t> noSeam = {0, 4};

    Render::MeshData src;
    src.numVertices = int(positions.size() / 3);
    src.positions = positions.data();
    src.triangleIndices = triangles.data();
    src.numTriangleIndices = int(triangles.size());
    src.lineIndices = lines.data();
    src.numLineIndices = int(lines.size());
    src.noSeamLineIndices = noSeam.data();
    src.numNoSeamLineIndices = int(noSeam.size());

    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(src, 0.1f, out));
    ASSERT_EQ(out.lineIndices.size(), 4u);
    // The merged edge folded a seam edge and a non-seam edge together;
    // there is no faithful answer for it, and non-seam wins so it stays
    // visible under hideSeam. The seam-only edge is filtered.
    ASSERT_EQ(out.noSeamLineIndices.size(), 2u);
    // Identify the kept edge by where it sits: it is the one touching
    // x = 0, not the one running to x = 1.
    float maxX = 0.0f;
    for (const int32_t v : out.noSeamLineIndices)
        maxX = std::max(maxX, out.positions[size_t(v) * 3]);
    EXPECT_LT(maxX, 0.6f);
}

TEST(MeshSimplify, keepsEdgeAndVertexPartTables)
{
    // Edges and vertices are elements too (lineParts / pointParts), and
    // their tables carry over the same way the face table does.
    SoupGrid grid(8);
    grid.lines = {0, 1, 1, 2, 3, 4};
    std::vector<float> &pos = grid.positions;
    const std::vector<int32_t> points = {0, 5};

    Render::MeshData src = grid.mesh();
    src.lineParts = {{0, 4}, {4, 2}};
    src.pointIndices = points.data();
    src.numPointIndices = int(points.size());
    src.pointParts = {{0, 1}, {1, 1}};

    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::simplifyMesh(src, 0.1f, out));

    ASSERT_EQ(out.lineParts.size(), 2u);
    for (const auto &part : out.lineParts) {
        EXPECT_GE(part.first, 0);
        EXPECT_LE(size_t(part.first + part.second), out.lineIndices.size());
        EXPECT_EQ(part.second % 2, 0);
    }
    EXPECT_GT(out.lineParts[0].second, 0);
    EXPECT_GT(out.lineParts[1].second, 0);

    // Each vertex element keeps a point, and its representative stays
    // within a cell of where the vertex was.
    ASSERT_EQ(out.pointParts.size(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(out.pointParts[i].second, 1);
        const size_t v =
            size_t(out.pointIndices[size_t(out.pointParts[i].first)]) * 3;
        const size_t s = size_t(points[i]) * 3;
        EXPECT_NEAR(out.positions[v], pos[s], 0.1f);
        EXPECT_NEAR(out.positions[v + 1], pos[s + 1], 0.1f);
        EXPECT_NEAR(out.positions[v + 2], pos[s + 2], 0.1f);
    }
}

// ---------------------------------------------------------------------
// Far-field proxies (docs/FarFieldProxies.md §5, phase 2)
// ---------------------------------------------------------------------

namespace
{

/// GL-layout (column-major) translation.
void translation(float *m, float x, float y, float z)
{
    for (int i = 0; i < 16; ++i)
        m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

/// A member standing on its own mesh at a placement.
Render::ProxyMember memberAt(const Render::MeshData &mesh, const float *model,
                             uint64_t key)
{
    Render::ProxyMember m;
    m.mesh = &mesh;
    m.model = model;
    m.objectKey = key;
    return m;
}

}  // namespace

TEST(ProxyMesh, mergesEachMemberWhereItsPlacementSaysAndNotWhereItsMeshIs)
{
    // One mesh, three placements -- the case a proxy exists for, and
    // the case a merge that ignored the transform would still pass a
    // triangle-count test on.
    const SoupGrid grid(4);
    const Render::MeshData mesh = grid.mesh();
    float a[16], b[16], c[16];
    translation(a, 0.0f, 0.0f, 0.0f);
    translation(b, 3.0f, 0.0f, 0.0f);
    translation(c, 0.0f, 3.0f, 0.0f);
    const std::vector<Render::ProxyMember> members = {
        memberAt(mesh, a, 11), memberAt(mesh, b, 22), memberAt(mesh, c, 33)};

    Render::ProxyMeshParams params;
    params.cellSize = 0.5f;
    Render::SimplifiedMesh out;
    std::vector<uint64_t> keys;
    Render::ProxyMeshStats stats;
    ASSERT_TRUE(Render::buildProxyMesh(members, params, out, &keys, &stats));

    // The merged content spans all three placements: a 1x1 grid at the
    // origin plus copies three units out in x and in y.
    EXPECT_NEAR(stats.extent, std::sqrt(16.0f + 16.0f), 0.01f);
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (size_t v = 0; v + 2 < out.positions.size(); v += 3)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], out.positions[v + size_t(k)]);
            hi[k] = std::max(hi[k], out.positions[v + size_t(k)]);
        }
    // Within a cell of the nominal corners, not on them: a
    // representative is its cell's average, so the extremes move
    // inward. The exact span is stats.extent above, which is measured
    // on the merge before anything is collapsed.
    EXPECT_NEAR(lo[0], 0.0f, params.cellSize);
    EXPECT_NEAR(hi[0], 4.0f, params.cellSize);
    EXPECT_NEAR(hi[1], 4.0f, params.cellSize);
    EXPECT_EQ(keys, (std::vector<uint64_t> {11, 22, 33}));
}

TEST(ProxyMesh, thePartTableNamesTheMemberEachRunCameFrom)
{
    // §6: the part table of a proxy is the element table simplifyMesh
    // already preserves, with one run per member instead of per face.
    const SoupGrid grid(4);
    const Render::MeshData mesh = grid.mesh();
    float a[16], b[16];
    translation(a, 0.0f, 0.0f, 0.0f);
    translation(b, 8.0f, 0.0f, 0.0f);
    const std::vector<Render::ProxyMember> members = {memberAt(mesh, a, 7),
                                                      memberAt(mesh, b, 9)};
    Render::ProxyMeshParams params;
    params.cellSize = 0.34f;
    Render::SimplifiedMesh out;
    std::vector<uint64_t> keys;
    ASSERT_TRUE(Render::buildProxyMesh(members, params, out, &keys));

    ASSERT_EQ(out.triangleParts.size(), 2u);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_GT(out.triangleParts[0].second, 0);
    EXPECT_GT(out.triangleParts[1].second, 0);
    // Every triangle of the second member's run sits at the second
    // member's placement, so a pick landing in that run names object 9.
    for (const auto &p : partPositions(out, out.triangleParts[1]))
        EXPECT_GE(std::get<0>(p), 7.9f);
    for (const auto &p : partPositions(out, out.triangleParts[0]))
        EXPECT_LE(std::get<0>(p), 1.1f);
}

TEST(ProxyMesh, weldingAcrossPartsSpendsFewerVerticesOnTheirSharedBoundary)
{
    // The one thing the weld actually buys: per-element clustering has
    // to duplicate a representative in every cell two parts share, and
    // over the members of a proxy that duplication is most of the
    // output. It is not what saves a small member -- see below.
    const SoupFold fold(8);
    const Render::MeshData mesh = fold.mesh();

    Render::SimplifiedMesh split, welded;
    Render::SimplifyOptions weldOpts;
    weldOpts.weldAcrossParts = true;
    ASSERT_TRUE(Render::simplifyMesh(mesh, 0.3f, split));
    ASSERT_TRUE(Render::simplifyMesh(mesh, 0.3f, welded, weldOpts));

    EXPECT_LT(welded.numVertices(), split.numVertices());
    // Both parts still draw, and both tables still have their slots.
    ASSERT_EQ(welded.triangleParts.size(), 2u);
    EXPECT_GT(welded.triangleParts[0].second, 0);
    EXPECT_GT(welded.triangleParts[1].second, 0);
}

TEST(ProxyMesh, theErrorItCommitsStaysInsideTheCellItClusteredOn)
{
    // What a screen-space tolerance is really asking about: no point of
    // the decimated surface is further from where it came from than a
    // vertex had to travel, and no vertex travels outside its cell.
    const SoupGrid grid(24);
    const Render::MeshData mesh = grid.mesh();
    for (float cellSize : {0.05f, 0.1f, 0.2f}) {
        Render::SimplifiedMesh out;
        Render::SimplifyStats stats;
        ASSERT_TRUE(Render::simplifyMesh(mesh, cellSize, out,
                                         Render::SimplifyOptions(), &stats))
            << cellSize;
        EXPECT_LE(stats.maxDisplacement, cellSize * std::sqrt(3.0f))
            << cellSize;
        EXPECT_LE(stats.rmsDisplacement, stats.maxDisplacement);
        EXPECT_GT(stats.clusters, 0u);
    }
}

TEST(ProxyMesh, theErrorAgainstTheExtentIsSetByTheGridNotByTheContents)
{
    // The ratio §11.1b needs in order to read as an error rather than
    // as an extent: halve the grid and the error against the same
    // content halves with it.
    const SoupGrid grid(32, 4.0f);
    const Render::MeshData mesh = grid.mesh();
    float model[16];
    translation(model, 0.0f, 0.0f, 0.0f);
    const std::vector<Render::ProxyMember> members = {memberAt(mesh, model, 1)};

    float previous = 0.0f;
    for (float cellSize : {0.5f, 0.25f, 0.125f}) {
        Render::ProxyMeshParams params;
        params.cellSize = cellSize;
        Render::SimplifiedMesh out;
        Render::ProxyMeshStats stats;
        ASSERT_TRUE(Render::buildProxyMesh(members, params, out, nullptr,
                                           &stats))
            << cellSize;
        const float ratio = stats.maxError / stats.extent;
        EXPECT_LT(ratio, cellSize * std::sqrt(3.0f) / stats.extent + 1e-6f);
        if (previous > 0.0f) {
            EXPECT_LT(ratio, previous);
        }
        previous = ratio;
    }
}

TEST(ProxyMesh, membersSmallerThanACellAreDeletedAndTheAreaIsWhatSaysSo)
{
    // The finding the displacement error cannot report: clustering does
    // not shrink a member smaller than a cell, it removes it, because
    // every one of its triangles has three corners in one cell. A field
    // of small parts can therefore vanish as a body while every error
    // reported stays inside the tolerance -- which is why the area
    // retained and the collapsed-member count are reported beside it.
    const SoupGrid small(2, 0.05f);
    const Render::MeshData mesh = small.mesh();
    std::vector<std::array<float, 16>> models(16);
    std::vector<Render::ProxyMember> members;
    for (size_t i = 0; i < models.size(); ++i) {
        translation(models[i].data(), float(i % 4), float(i / 4), 0.0f);
        members.push_back(memberAt(mesh, models[i].data(), uint64_t(i)));
    }

    Render::ProxyMeshParams params;
    params.cellSize = 0.25f;  // five times the span of any member
    Render::SimplifiedMesh out;
    Render::ProxyMeshStats stats;
    EXPECT_FALSE(Render::buildProxyMesh(members, params, out, nullptr, &stats));
    EXPECT_EQ(stats.collapsedMembers, members.size());
    EXPECT_GT(stats.sourceArea, 0.0);
    EXPECT_EQ(stats.proxyArea, 0.0);

    // A grid fine enough to resolve a member keeps it, and then the
    // area survives with it.
    params.cellSize = 0.02f;
    Render::ProxyMeshStats kept;
    ASSERT_TRUE(Render::buildProxyMesh(members, params, out, nullptr, &kept));
    EXPECT_EQ(kept.collapsedMembers, 0u);
    EXPECT_GT(kept.proxyArea, 0.5 * kept.sourceArea);
}

TEST(ProxyMesh, theGridIsTheLevelsNotTheContents)
{
    // §3.2: a proxy is built from its children's proxies, so the grid
    // has to be the level's -- shared origin, nesting by halving. Two
    // anchors a whole cell apart describe the same grid and must
    // produce the same bytes; an anchor half a cell over does not.
    const SoupGrid grid(16);
    const Render::MeshData mesh = grid.mesh();
    float model[16];
    translation(model, 0.0f, 0.0f, 0.0f);
    const std::vector<Render::ProxyMember> members = {memberAt(mesh, model, 1)};

    Render::ProxyMeshParams onGrid;
    onGrid.cellSize = 0.1f;
    onGrid.anchor[0] = -1.0f;  // ten whole cells away
    Render::ProxyMeshParams shifted = onGrid;
    shifted.anchor[0] = -1.05f;  // half a cell away
    Render::ProxyMeshParams origin;
    origin.cellSize = 0.1f;

    Render::SimplifiedMesh a, b, c;
    ASSERT_TRUE(Render::buildProxyMesh(members, origin, a));
    ASSERT_TRUE(Render::buildProxyMesh(members, onGrid, b));
    ASSERT_TRUE(Render::buildProxyMesh(members, shifted, c));
    EXPECT_EQ(a.positions, b.positions);
    EXPECT_EQ(a.triangleIndices, b.triangleIndices);
    EXPECT_NE(a.positions, c.positions);
}

TEST(ProxyMesh, theInstancingGateSeesOneMeshBehindManyMembers)
{
    // §7.1: five hundred instances of one screw share one vertex buffer
    // today and a baked proxy holds five hundred copies. The merge
    // reports both numbers so the caller can refuse.
    const SoupGrid grid(4);
    const Render::MeshData mesh = grid.mesh();
    std::vector<std::array<float, 16>> models(9);
    std::vector<Render::ProxyMember> members;
    for (size_t i = 0; i < models.size(); ++i) {
        translation(models[i].data(), float(i), 0.0f, 0.0f);
        members.push_back(memberAt(mesh, models[i].data(), uint64_t(i)));
    }
    Render::ProxyMeshParams params;
    params.cellSize = 0.3f;
    Render::SimplifiedMesh out;
    Render::ProxyMeshStats stats;
    ASSERT_TRUE(Render::buildProxyMesh(members, params, out, nullptr, &stats));

    EXPECT_EQ(stats.members, 9u);
    EXPECT_EQ(stats.distinctMeshes, 1u);
    EXPECT_EQ(stats.sourceTriangles, 9u * 32u);
    EXPECT_EQ(stats.uniqueTriangles, 32u);

    // And the case that actually occurs: nine *views* onto one cache
    // entry, which is how the renderer hands out an instanced shape.
    // Counted by address these read as nine distinct geometries and the
    // gate would report that it never binds.
    std::vector<Render::MeshData> views(9, mesh);
    for (size_t i = 0; i < views.size(); ++i) {
        views[i].cacheId = 4242;
        members[i].mesh = &views[i];
    }
    ASSERT_TRUE(Render::buildProxyMesh(members, params, out, nullptr, &stats));
    EXPECT_EQ(stats.distinctMeshes, 1u);
    EXPECT_EQ(stats.uniqueTriangles, 32u);
}

TEST(ProxyMesh, aMirroredPlacementKeepsTheSurfaceFacingTheWayItDid)
{
    // A negative determinant is two facts: the winding reverses and the
    // normal's side flips. Handling one without the other turns the
    // member inside out in a proxy that has no other geometry to
    // contradict it.
    const SoupGrid grid(2);
    const Render::MeshData mesh = grid.mesh();  // +z facing, on z = 0
    float mirror[16];
    translation(mirror, 0.0f, 0.0f, 0.0f);
    mirror[10] = -1.0f;  // mirror through z = 0
    const std::vector<Render::ProxyMember> members = {
        memberAt(mesh, mirror, 1)};
    Render::ProxyMeshParams params;
    params.cellSize = 0.3f;
    Render::SimplifiedMesh out;
    ASSERT_TRUE(Render::buildProxyMesh(members, params, out));

    ASSERT_FALSE(out.normals.empty());
    for (size_t v = 0; v + 2 < out.normals.size(); v += 3)
        EXPECT_LT(out.normals[v + 2], -0.9f);
    // The geometric winding agrees with that normal rather than
    // contradicting it: the mirrored surface still winds anticlockwise
    // seen from the side its normal points at.
    ASSERT_GE(out.triangleIndices.size(), 3u);
    for (size_t i = 0; i + 2 < out.triangleIndices.size(); i += 3) {
        const float *pa = &out.positions[size_t(out.triangleIndices[i]) * 3];
        const float *pb =
            &out.positions[size_t(out.triangleIndices[i + 1]) * 3];
        const float *pc =
            &out.positions[size_t(out.triangleIndices[i + 2]) * 3];
        const float cz = (pb[0] - pa[0]) * (pc[1] - pa[1])
            - (pb[1] - pa[1]) * (pc[0] - pa[0]);
        EXPECT_LT(cz, 0.0f);
    }
}

TEST(ProxyMesh, aMemberThatContributesNothingKeepsItsSlot)
{
    // The part table is positional, so an empty member must not shift
    // the members after it -- the same rule a collapsed element already
    // follows inside one mesh.
    const SoupGrid grid(4);
    const Render::MeshData mesh = grid.mesh();
    Render::MeshData empty;  // no positions, no indices
    float a[16], b[16];
    translation(a, 0.0f, 0.0f, 0.0f);
    translation(b, 8.0f, 0.0f, 0.0f);
    std::vector<Render::ProxyMember> members = {
        memberAt(mesh, a, 5), memberAt(empty, b, 6), memberAt(mesh, b, 7)};

    Render::ProxyMeshParams params;
    params.cellSize = 0.34f;
    Render::SimplifiedMesh out;
    std::vector<uint64_t> keys;
    ASSERT_TRUE(Render::buildProxyMesh(members, params, out, &keys));
    ASSERT_EQ(keys.size(), 3u);
    ASSERT_EQ(out.triangleParts.size(), 3u);
    EXPECT_EQ(keys[1], 6u);
    EXPECT_EQ(out.triangleParts[1].second, 0);
    EXPECT_GT(out.triangleParts[2].second, 0);
    for (const auto &p : partPositions(out, out.triangleParts[2]))
        EXPECT_GE(std::get<0>(p), 7.9f);
}

TEST(ProxyMesh, aDrawOfOneFaceContributesThatFaceAlone)
{
    // DrawCall carries an index range, and a proxy has to honour it or
    // a per-face draw drags the whole object into the merge.
    const SoupGrid grid(4);
    Render::MeshData mesh = grid.mesh();
    float model[16];
    translation(model, 0.0f, 0.0f, 0.0f);
    Render::ProxyMember part = memberAt(mesh, model, 1);
    part.indexStart = 0;
    part.indexCount = 12;  // four triangles of the thirty-two

    Render::ProxyMeshParams params;
    params.cellSize = 0.05f;
    Render::SimplifiedMesh out;
    Render::ProxyMeshStats stats;
    ASSERT_TRUE(Render::buildProxyMesh({part}, params, out, nullptr, &stats));
    EXPECT_EQ(stats.sourceTriangles, 4u);
    EXPECT_LE(stats.sourceVertices, 12u);
}

// ---------------------------------------------------------------------
// Standing in for what the decimation deleted (docs/FarFieldProxies.md
// 11.1c) -- the two representations, measured against each other.
// ---------------------------------------------------------------------

namespace
{

/// The boxes of a stand-in mesh, read back as bounds: 24 vertices each,
/// in the order emitBox wrote them.
std::vector<std::array<float, 6>> standInBoxes(const Render::SimplifiedMesh &m)
{
    std::vector<std::array<float, 6>> boxes;
    for (size_t v = 0; v + 71 < m.positions.size(); v += 72) {
        std::array<float, 6> b {m.positions[v], m.positions[v + 1],
                                m.positions[v + 2], m.positions[v],
                                m.positions[v + 1], m.positions[v + 2]};
        for (size_t k = 0; k < 24; ++k)
            for (size_t c = 0; c < 3; ++c) {
                const float p = m.positions[v + k * 3 + c];
                b[c] = std::min(b[c], p);
                b[3 + c] = std::max(b[3 + c], p);
            }
        boxes.push_back(b);
    }
    return boxes;
}

/// Is \a p inside any of \a boxes, to within the tolerance a float
/// round trip needs?
bool covered(const std::vector<std::array<float, 6>> &boxes, const float *p)
{
    for (const auto &b : boxes) {
        bool in = true;
        for (int k = 0; k < 3; ++k)
            in = in && p[k] >= b[size_t(k)] - 1e-4f
                && p[k] <= b[size_t(3 + k)] + 1e-4f;
        if (in)
            return true;
    }
    return false;
}

/// A field of \a n members too small for the grid, spaced \a pitch
/// apart along x and y -- the case the deletion finding is about.
struct SmallField {
    SoupGrid grid {2, 0.05f};
    std::vector<std::array<float, 16>> models;
    std::vector<Render::ProxyMember> members;
    Render::MeshData mesh;

    SmallField(size_t n, float pitch)
        : models(n)
    {
        mesh = grid.mesh();
        for (size_t i = 0; i < n; ++i) {
            translation(models[i].data(), float(i % 4) * pitch,
                        float(i / 4) * pitch, 0.0f);
            members.push_back(memberAt(mesh, models[i].data(), uint64_t(i)));
        }
    }
};

}  // namespace

TEST(ProxyStandIn, everyDeletedMemberGetsABoxAndEveryDeletedPointIsInside)
{
    // The contract that makes the two modes comparable at all: both are
    // conservative covers, so nothing that was deleted ends up outside
    // the geometry that stands in for it.
    SmallField field(16, 1.0f);
    Render::ProxyMeshParams params;
    params.cellSize = 0.25f;  // five times the span of any member

    Render::SimplifiedMesh merged, proxy;
    ASSERT_TRUE(Render::mergeProxyMembers(field.members, merged));
    // Refused: at this grid there is nothing left to draw at all.
    EXPECT_FALSE(Render::decimateProxyMesh(merged, params, proxy));

    for (Render::StandInMode mode :
         {Render::StandInMode::PerMember, Render::StandInMode::PerCell}) {
        Render::SimplifiedMesh out;
        Render::StandInStats stats;
        ASSERT_TRUE(
            Render::buildStandIns(merged, proxy, params, mode, out, &stats));
        EXPECT_EQ(stats.members, 16u);
        EXPECT_EQ(stats.triangles, stats.boxes * 12);
        EXPECT_GT(stats.deletedArea, 0.0);

        const auto boxes = standInBoxes(out);
        ASSERT_EQ(boxes.size(), stats.boxes);
        for (size_t v = 0; v + 2 < merged.positions.size(); v += 3)
            EXPECT_TRUE(covered(boxes, &merged.positions[v]))
                << "vertex " << v / 3 << " of the merge is outside every box";
    }
}

TEST(ProxyStandIn, perCellIsBoundedByTheGridAndPerMemberByTheMemberCount)
{
    // The choice itself: sixteen members inside one cell cost sixteen
    // boxes one way and one box the other, and that ratio is the whole
    // argument for standing in per cell.
    SmallField field(16, 0.01f);  // every member inside one cell
    Render::ProxyMeshParams params;
    params.cellSize = 1.0f;

    Render::SimplifiedMesh merged, proxy;
    ASSERT_TRUE(Render::mergeProxyMembers(field.members, merged));
    Render::decimateProxyMesh(merged, params, proxy);

    Render::SimplifiedMesh perMember, perCell;
    Render::StandInStats a, b;
    ASSERT_TRUE(Render::buildStandIns(merged, proxy, params,
                                      Render::StandInMode::PerMember,
                                      perMember, &a));
    ASSERT_TRUE(Render::buildStandIns(merged, proxy, params,
                                      Render::StandInMode::PerCell, perCell,
                                      &b));
    EXPECT_EQ(a.boxes, 16u);
    EXPECT_EQ(b.boxes, 1u);
    EXPECT_EQ(b.cells, 1u);
    // And the cost of that: the cell's box spans the gaps between the
    // members, so it covers more than the sixteen tight ones together.
    EXPECT_GT(b.area, a.area / 16.0);
}

TEST(ProxyStandIn, aStandInNamesItsOwnMemberOrNoneButNeverAnother)
{
    // Section 6's invariant, carried into the stand-ins: a per-member
    // box sits in its member's slot, and a cell shared by several
    // members names none of them rather than picking one.
    Render::ProxyMeshParams params;
    params.cellSize = 1.0f;

    SmallField crowded(16, 0.01f);  // one cell, sixteen members
    Render::SimplifiedMesh merged, proxy, out;
    ASSERT_TRUE(Render::mergeProxyMembers(crowded.members, merged));
    Render::decimateProxyMesh(merged, params, proxy);

    Render::StandInStats named;
    ASSERT_TRUE(Render::buildStandIns(merged, proxy, params,
                                      Render::StandInMode::PerMember, out,
                                      &named));
    ASSERT_EQ(out.triangleParts.size(), 16u);
    for (const auto &part : out.triangleParts)
        EXPECT_EQ(part.second, 36);
    EXPECT_EQ(named.named, 16u);

    Render::StandInStats shared;
    ASSERT_TRUE(Render::buildStandIns(merged, proxy, params,
                                      Render::StandInMode::PerCell, out,
                                      &shared));
    EXPECT_EQ(shared.sharedCells, 1u);
    EXPECT_EQ(shared.named, 0u);
    ASSERT_EQ(out.triangleParts.size(), 16u);
    for (const auto &part : out.triangleParts)
        EXPECT_EQ(part.second, 0);

    // Alone in its cell, a member is named again -- the ambiguity is
    // the crowding, not the mode.
    SmallField spread(16, 1.0f);
    Render::ProxyMeshParams fine;
    fine.cellSize = 0.25f;
    Render::SimplifiedMesh spreadMerged, spreadProxy;
    ASSERT_TRUE(Render::mergeProxyMembers(spread.members, spreadMerged));
    Render::decimateProxyMesh(spreadMerged, fine, spreadProxy);
    Render::StandInStats alone;
    ASSERT_TRUE(Render::buildStandIns(spreadMerged, spreadProxy, fine,
                                      Render::StandInMode::PerCell, out,
                                      &alone));
    EXPECT_EQ(alone.sharedCells, 0u);
    EXPECT_EQ(alone.named, 16u);
}

TEST(ProxyStandIn, aGridFineEnoughToKeepTheMembersHasNothingToStandInFor)
{
    // The good case reports itself as one: no members deleted, no
    // boxes, and a false return the caller can skip on.
    SmallField field(16, 1.0f);
    Render::ProxyMeshParams params;
    params.cellSize = 0.02f;

    Render::SimplifiedMesh merged, proxy, out;
    ASSERT_TRUE(Render::mergeProxyMembers(field.members, merged));
    ASSERT_TRUE(Render::decimateProxyMesh(merged, params, proxy));
    Render::StandInStats stats;
    EXPECT_FALSE(Render::buildStandIns(merged, proxy, params,
                                       Render::StandInMode::PerMember, out,
                                       &stats));
    EXPECT_EQ(stats.members, 0u);
    EXPECT_EQ(stats.boxes, 0u);
    EXPECT_TRUE(out.triangleIndices.empty());
}

TEST(ProxyStandIn, standingInIsSkippedForTheMembersThatSurvived)
{
    // A proxy that kept some members and dropped others must stand in
    // for exactly the ones it dropped, or the survivors are drawn
    // twice -- once decimated and once as a box around them.
    SoupGrid big(8, 1.0f);
    const Render::MeshData bigMesh = big.mesh();
    SmallField field(4, 1.0f);
    std::vector<Render::ProxyMember> members = field.members;
    float model[16];
    translation(model, 0.0f, 0.0f, 0.0f);
    members.push_back(memberAt(bigMesh, model, 99));

    Render::ProxyMeshParams params;
    params.cellSize = 0.25f;
    Render::SimplifiedMesh merged, proxy, out;
    ASSERT_TRUE(Render::mergeProxyMembers(members, merged));
    ASSERT_TRUE(Render::decimateProxyMesh(merged, params, proxy));

    Render::StandInStats stats;
    ASSERT_TRUE(Render::buildStandIns(merged, proxy, params,
                                      Render::StandInMode::PerMember, out,
                                      &stats));
    EXPECT_EQ(stats.members, 4u);
    ASSERT_EQ(out.triangleParts.size(), members.size());
    EXPECT_EQ(out.triangleParts.back().second, 0);  // the survivor
    for (size_t i = 0; i < 4; ++i)
        EXPECT_EQ(out.triangleParts[i].second, 36);
}
