// Tests for bottom-up far-field proxy generation (docs/FarFieldProxies.md
// section 7.1, the second half of phase 2): a node's proxy is merged
// from its children's proxies rather than from source geometry, and the
// three properties that are supposed to fall out of that -- cost, a
// monotone error, and a deleted mass that stays bounded by the grid.
// Like the tests beside them these need no GL context and no document.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include "Gui/Renderer/ProxyStore.h"

using namespace Render;

namespace
{

/// A closed box of 12 triangles, unmerged soup, spanning [0,span]^3 --
/// a member with volume, unlike the planar grids the MeshSimplify tests
/// use, because a proxy's deleted mass is a three-dimensional claim.
struct BoxMesh {
    std::vector<float> pos;
    std::vector<float> norm;
    std::vector<int32_t> tris;
    std::shared_ptr<MeshData> data = std::make_shared<MeshData>();

    /// \a grid quads per face: a member of 12 triangles decimates to
    /// nothing much, so the cost of a merge would be the same however
    /// it was fed and the bottom-up claim would be untestable.
    explicit BoxMesh(float span, uint64_t cacheId = 0, int grid = 4)
    {
        static const int face[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5},
                                       {0, 1, 5, 4}, {2, 6, 7, 3},
                                       {0, 2, 3, 1}, {4, 5, 7, 6}};
        static const float normal[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                                           {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        for (int f = 0; f < 6; ++f) {
            for (int u = 0; u < grid; ++u)
                for (int v = 0; v < grid; ++v) {
                    const int32_t base = int32_t(pos.size() / 3);
                    for (int k = 0; k < 4; ++k) {
                        const int c = face[f][k];
                        // The face's four corners, subdivided: the
                        // quad (u,v) of the grid over that face.
                        const float fu =
                            (float(u) + ((k == 1 || k == 2) ? 1.0f : 0.0f))
                            / float(grid);
                        const float fv =
                            (float(v) + ((k == 2 || k == 3) ? 1.0f : 0.0f))
                            / float(grid);
                        float p[3] = {(c & 1) ? span : 0.0f,
                                      (c & 2) ? span : 0.0f,
                                      (c & 4) ? span : 0.0f};
                        // Slide within the face plane: the two axes the
                        // face's normal does not use.
                        int axis = 0;
                        for (int a = 0; a < 3; ++a)
                            if (normal[f][a] == 0.0f)
                                p[a] = (axis++ == 0) ? fu * span : fv * span;
                        pos.insert(pos.end(), {p[0], p[1], p[2]});
                        norm.insert(norm.end(),
                                    {normal[f][0], normal[f][1],
                                     normal[f][2]});
                    }
                    for (int32_t i : {0, 1, 2, 0, 2, 3})
                        tris.push_back(base + i);
                }
        }
        data->cacheId = cacheId;
        data->numVertices = int(pos.size() / 3);
        data->positions = pos.data();
        data->normals = norm.data();
        data->triangleIndices = tris.data();
        data->numTriangleIndices = int(tris.size());
    }
};

/// One drawn occurrence of \a mesh, translated to (x, y, z).
DrawCall drawAt(const std::shared_ptr<const MeshData> &mesh, float x, float y,
                float z, float span, uint64_t key, uint32_t diffuse)
{
    DrawCall d;
    d.objectKey = key;
    d.mesh = mesh;
    d.material.type = Material::Triangle;
    d.material.diffuse = diffuse;
    d.identity = false;
    for (int i = 0; i < 16; ++i)
        d.model[i] = 0.0f;
    d.model[0] = d.model[5] = d.model[10] = d.model[15] = 1.0f;
    d.model[12] = x;
    d.model[13] = y;
    d.model[14] = z;
    d.bboxMin[0] = x;
    d.bboxMin[1] = y;
    d.bboxMin[2] = z;
    d.bboxMax[0] = x + span;
    d.bboxMax[1] = y + span;
    d.bboxMax[2] = z + span;
    return d;
}

/// The case the design exists for: a flat lattice of many small equal
/// parts, deep enough that generation has several levels to climb.
struct Lattice {
    BoxMesh box {0.2f, 0x51};
    DrawCallList draws;
    std::vector<ProxyInstance> instances;
    ProxyHierarchy index;

    explicit Lattice(int n = 8, uint64_t buckets = 1)
    {
        uint64_t key = 1;
        for (int x = 0; x < n; ++x)
            for (int y = 0; y < n; ++y)
                for (int z = 0; z < n; ++z) {
                    const uint32_t diffuse =
                        uint32_t(0xff000000u + (key % buckets) * 0x001100u);
                    draws.push_back(drawAt(box.data, float(x), float(y),
                                           float(z), 0.2f, key, diffuse));
                    ++key;
                }
        proxyInstances(draws, instances);
        ProxyParams params;
        params.maxPerCell = 8;
        index.build(instances, params);
    }
};

/// The node a generated entry belongs to, by id.
int nodeOf(const ProxyHierarchy &index, uint64_t id)
{
    for (size_t i = 0; i < index.nodes().size(); ++i)
        if (index.nodes()[i].id == id)
            return int(i);
    return kNoProxyNode;
}

}  // namespace

TEST(ProxyStore, generatesTheWholeSubtreeAndKeysItByPositionalNodeId)
{
    Lattice scene(8);
    ProxyStore store;
    ASSERT_TRUE(store.generate(scene.index, scene.index.root(), scene.draws));
    EXPECT_GT(store.stats().nodes, 1u);
    EXPECT_EQ(store.stats().entries, uint32_t(store.entries().size()));

    // Every entry answers by the id the partition gave its node, which
    // is positional -- level plus Morton code -- and therefore the same
    // on any machine that builds the same partition (section 3.2).
    for (const ProxyEntry &entry : store.entries()) {
        const ProxyEntry *found = store.find(entry.nodeId, entry.bucket);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->nodeId, entry.nodeId);
        const int node = nodeOf(scene.index, entry.nodeId);
        ASSERT_NE(node, kNoProxyNode);
        EXPECT_EQ(scene.index.nodes()[size_t(node)].level, entry.level);
    }
}

TEST(ProxyStore, aParentMergesItsChildrensProxiesNotTheSourceAgain)
{
    // The claim of section 7.1: total generation is O(leaf triangles)
    // rather than O(levels x leaves), because each level merges a
    // decimation of the level below instead of the source once per
    // level. The control generates the same nodes from source, which is
    // what the phase 2 readout did.
    Lattice scene(8);
    ProxyStore bottomUp, fromSource;
    ProxyStore::Options control;
    control.fromSource = true;
    ASSERT_TRUE(bottomUp.generate(scene.index, scene.index.root(),
                                  scene.draws));
    ASSERT_TRUE(fromSource.generate(scene.index, scene.index.root(),
                                    scene.draws, control));

    // Same nodes, same entries -- only what each merge was fed differs.
    EXPECT_EQ(bottomUp.stats().nodes, fromSource.stats().nodes);
    EXPECT_GT(bottomUp.stats().maxLevelSpan, 1u);
    // And the merge is strictly cheaper, by more than a rounding.
    EXPECT_LT(bottomUp.stats().mergedTriangles,
              fromSource.stats().mergedTriangles);
    EXPECT_LT(double(bottomUp.stats().mergedTriangles),
              0.75 * double(fromSource.stats().mergedTriangles));
}

TEST(ProxyStore, theErrorIsMonotoneUpTheTreeBecauseItAccumulates)
{
    // Section 8.1 needs each node to decide on its own error while the
    // cut stays globally consistent, which holds only if a parent never
    // claims less error than a child. Built bottom-up that is a
    // consequence of the construction: worst input plus this level's
    // displacement.
    Lattice scene(8);
    ProxyStore store;
    ASSERT_TRUE(store.generate(scene.index, scene.index.root(), scene.draws));

    std::map<uint64_t, const ProxyEntry *> byId;
    for (const ProxyEntry &entry : store.entries())
        byId[entry.nodeId ^ (entry.bucket << 1)] = &entry;

    uint32_t compared = 0;
    for (const ProxyEntry &entry : store.entries()) {
        const int node = nodeOf(scene.index, entry.nodeId);
        ASSERT_NE(node, kNoProxyNode);
        for (int c : scene.index.nodes()[size_t(node)].child) {
            if (c == kNoProxyNode)
                continue;
            const ProxyEntry *child =
                store.find(scene.index.nodes()[size_t(c)].id, entry.bucket);
            if (!child)
                continue;
            EXPECT_GE(entry.error, child->error)
                << "level " << entry.level << " claims less error than its "
                << "child at level " << child->level;
            ++compared;
        }
    }
    EXPECT_GT(compared, 0u) << "no parent/child pair was compared at all";
}

TEST(ProxyStore, theDeletedMassIsCarriedAndStaysBoundedByTheGrid)
{
    // 11.1d: the mass a decimation deletes is carried per occupied
    // cell, and the recursion closes because a child's boxes are handed
    // to the parent's merge as ordinary geometry -- so a parent's
    // stand-in count answers to its own grid, not to how many members
    // were deleted anywhere below it.
    Lattice scene(8);
    ProxyStore with, without;
    ProxyStore::Options bare;
    bare.standIns = false;
    ASSERT_TRUE(with.generate(scene.index, scene.index.root(), scene.draws));
    ASSERT_TRUE(without.generate(scene.index, scene.index.root(), scene.draws,
                                 bare));

    EXPECT_GT(with.stats().standInTriangles, 0u);
    EXPECT_EQ(without.stats().standInTriangles, 0u);

    // The area that survives at the top of the tree is what says the
    // small parts are still represented: without stand-ins a lattice of
    // sub-cell boxes decimates to nothing much.
    const auto topArea = [](const ProxyStore &store) {
        double kept = 0.0, source = 0.0;
        uint32_t level = 0xffffffffu;
        for (const ProxyEntry &entry : store.entries())
            level = std::min(level, entry.level);
        for (const ProxyEntry &entry : store.entries())
            if (entry.level == level) {
                kept += entry.keptArea;
                source += entry.sourceArea;
            }
        return source > 0.0 ? kept / source : 0.0;
    };
    EXPECT_GT(topArea(with), topArea(without));

    // Bounded by the grid: no node stands in with more boxes than its
    // decimation grid has cells, whatever its subtree holds.
    const uint32_t subdivision = ProxyStore::Options().subdivision;
    const uint32_t cells = subdivision * subdivision * subdivision;
    for (const ProxyEntry &entry : with.entries())
        EXPECT_LE(entry.standInStats.boxes, cells)
            << "node at level " << entry.level << " stands in per member";
}

TEST(ProxyStore, aProxyIsGeneratedPerMaterialBucketAndNeverAcrossOne)
{
    // Section 5.1: the unit is (cell, material bucket), so that every
    // proxy carries exactly one material and nothing is averaged.
    Lattice scene(8, 3);
    ProxyStore store;
    ASSERT_TRUE(store.generate(scene.index, scene.index.root(), scene.draws));

    std::map<uint64_t, std::vector<uint64_t>> bucketsPerNode;
    for (const ProxyEntry &entry : store.entries())
        bucketsPerNode[entry.nodeId].push_back(entry.bucket);
    ASSERT_FALSE(bucketsPerNode.empty());
    uint32_t multi = 0;
    for (auto &node : bucketsPerNode) {
        std::vector<uint64_t> &list = node.second;
        std::sort(list.begin(), list.end());
        EXPECT_EQ(std::unique(list.begin(), list.end()), list.end())
            << "a node generated two proxies for one bucket";
        if (list.size() > 1)
            ++multi;
    }
    EXPECT_GT(multi, 0u) << "the scene never exercised more than one bucket";
}

TEST(ProxyStore, aPartSlotNamesAnInstanceOrAChildNodeAndSaysWhich)
{
    // Section 6, carried up the tree: identity thins as the merge
    // climbs -- a slot high in the tree names a child node rather than
    // an object -- but it never becomes wrong, and the table says which
    // kind of name it holds.
    Lattice scene(8);
    ProxyStore store;
    ASSERT_TRUE(store.generate(scene.index, scene.index.root(), scene.draws));

    bool sawNode = false, sawInstance = false;
    for (const ProxyEntry &entry : store.entries()) {
        ASSERT_EQ(entry.partKeys.size(), entry.partIsNode.size());
        for (size_t i = 0; i < entry.partKeys.size(); ++i) {
            if (entry.partIsNode[i]) {
                sawNode = true;
                EXPECT_NE(nodeOf(scene.index, entry.partKeys[i]),
                          kNoProxyNode)
                    << "a slot claims to name a node that does not exist";
            }
            else
                sawInstance = true;
        }
    }
    EXPECT_TRUE(sawNode) << "nothing above the leaves merged a child proxy";
    EXPECT_TRUE(sawInstance) << "no node merged its own residents";
}

TEST(ProxyStore, aBudgetStopsGenerationAndSaysHowMuchItSkipped)
{
    // A generation pass over an assembly is the assembly, so a caller
    // that wants a sample has to be able to ask for one and to see what
    // it did not get -- a measurement that silently drops most of its
    // work reads as coverage it did not have.
    Lattice scene(8);
    ProxyStore full, capped;
    ASSERT_TRUE(full.generate(scene.index, scene.index.root(), scene.draws));
    ProxyStore::Options budget;
    budget.triangleBudget = full.stats().mergedTriangles / 4;
    capped.generate(scene.index, scene.index.root(), scene.draws, budget);

    EXPECT_LT(capped.stats().mergedTriangles, full.stats().mergedTriangles);
    EXPECT_GT(capped.stats().overBudget, 0u);
    EXPECT_LT(capped.stats().entries, full.stats().entries);
}

TEST(ProxyStore, aLoneChildProxyStillCoarsensRatherThanBreakingTheChain)
{
    // Where a subtree narrows to one bucket with one child in it, the
    // parent has a single member. Refusing there -- which the
    // single-object rule would do -- leaves the cut nothing to stop on
    // above that point, and generating from source would not have
    // refused, so the two modes would disagree about which nodes exist.
    Lattice scene(8);
    ProxyStore bottomUp, fromSource;
    ProxyStore::Options control;
    control.fromSource = true;
    ASSERT_TRUE(bottomUp.generate(scene.index, scene.index.root(),
                                  scene.draws));
    ASSERT_TRUE(fromSource.generate(scene.index, scene.index.root(),
                                    scene.draws, control));

    // Every node the from-source pass produced a bucket for, the
    // bottom-up pass produced one for too.
    for (const ProxyEntry &entry : fromSource.entries())
        EXPECT_NE(bottomUp.find(entry.nodeId, entry.bucket), nullptr)
            << "level " << entry.level << " exists from source and not "
            << "from its children";
}
