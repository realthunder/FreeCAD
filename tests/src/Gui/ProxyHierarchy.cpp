// Tests for the far-field spatial index (docs/FarFieldProxies.md §3,
// phase 1 of §11.1): the partition, the per-cell cut, and the invariant
// of §11.3. Like the SceneDump, SceneLadder and MeshSimplify tests
// beside them these need no GL context and no document — which is the
// point of the header taking a flat instance table rather than anything
// that knows what a document is.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "Gui/Renderer/ProxyHierarchy.h"
#include "Gui/Renderer/SceneDump.h"

using namespace Render;

namespace
{

/// GL-layout (column-major) perspective, matching what the renderer is
/// handed. Only P[5] and P[15] matter to the projection the cut reads,
/// but the clip test uses the rest.
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

/// Camera on +Z looking down -Z at the origin, \a dist away.
void viewAt(float *V, float dist)
{
    for (int i = 0; i < 16; ++i)
        V[i] = 0.0f;
    V[0] = V[5] = V[10] = V[15] = 1.0f;
    V[14] = -dist;
}

ProxyInstance boxAt(float cx, float cy, float cz, float half,
                    uint64_t key, uint64_t bucket = 0)
{
    ProxyInstance inst;
    inst.objectKey = key;
    inst.materialBucket = bucket;
    inst.bboxMin[0] = cx - half;
    inst.bboxMin[1] = cy - half;
    inst.bboxMin[2] = cz - half;
    inst.bboxMax[0] = cx + half;
    inst.bboxMax[1] = cy + half;
    inst.bboxMax[2] = cz + half;
    return inst;
}

/// The case the whole design turns on: one holder, N children, no
/// intermediate structure anywhere. A 20x20x20 lattice of small parts
/// spanning [-1,1], which is what an importer or a script produces and
/// what a document-tree hierarchy would offer exactly one node for.
std::vector<ProxyInstance> flatAssembly(int n = 20, uint64_t buckets = 1)
{
    std::vector<ProxyInstance> out;
    out.reserve(size_t(n) * n * n);
    const float step = 2.0f / float(n);
    uint64_t key = 1;
    for (int x = 0; x < n; ++x) {
        for (int y = 0; y < n; ++y) {
            for (int z = 0; z < n; ++z) {
                out.push_back(boxAt(-1.0f + (float(x) + 0.5f) * step,
                                    -1.0f + (float(y) + 0.5f) * step,
                                    -1.0f + (float(z) + 0.5f) * step,
                                    0.2f * step, key, key % buckets));
                ++key;
            }
        }
    }
    return out;
}

std::multiset<uint64_t> nodeIds(const ProxyHierarchy &h)
{
    std::multiset<uint64_t> ids;
    for (const auto &n : h.nodes())
        ids.insert(n.id);
    return ids;
}

}  // namespace

// ---------------------------------------------------------------------
// The partition
// ---------------------------------------------------------------------

TEST(ProxyHierarchy, aFlatAssemblyStillGetsALadder)
{
    // The motivating failure (§3): coupled to the document tree this
    // assembly has one interior node and therefore no cut at all —
    // draw all 8000, or draw one blob. The spatial index has to grade
    // it regardless of how the document is shaped, and the document is
    // never consulted, so "flat" is not even expressible here.
    ProxyHierarchy h;
    h.build(flatAssembly());
    const auto st = h.stats();
    EXPECT_EQ(st.instances, 8000u);
    // 8000 instances at K=32 need ~250 leaves, which is level 3 of an
    // octree (512 cells). The claim under test is that a ladder exists
    // at all, which is precisely what the document tree could not give.
    EXPECT_GE(st.depth, 3u) << "a flat assembly must still produce levels";
    EXPECT_GT(st.nodes, 100u);

    // K is a bound on leaves, and it is what bounds both a pop and the
    // cost of descending (§3.2).
    for (const auto &n : h.nodes()) {
        if (n.leaf()) {
            EXPECT_LE(n.subtreeCount, h.params().maxPerCell);
        }
    }
}

TEST(ProxyHierarchy, everyInstanceIsPlacedExactlyOnce)
{
    ProxyHierarchy h;
    h.build(flatAssembly(12));
    std::vector<int> seen(h.instances().size(), 0);
    for (uint32_t idx : h.residents()) {
        ASSERT_LT(idx, seen.size());
        ++seen[idx];
    }
    for (size_t i = 0; i < seen.size(); ++i)
        EXPECT_EQ(seen[i], 1) << "instance " << i << " placed " << seen[i]
                              << " times";
    EXPECT_EQ(h.residents().size(), h.instances().size());
    EXPECT_EQ(h.nodes()[size_t(h.root())].subtreeCount, h.instances().size());
}

TEST(ProxyHierarchy, assignmentBySizeKeepsASpanningPartOutOfTheFineCells)
{
    // §3.2: an instance goes to the finest cell that contains it whole.
    // A frame member spanning the machine therefore lands at the root
    // and is drawn exactly — self-correcting, because a thing that
    // large is never small on screen. Getting this wrong is how a flat
    // assembly's one outlier would otherwise force a shallow cut on
    // everything.
    auto instances = flatAssembly(10);
    instances.push_back(boxAt(0.0f, 0.0f, 0.0f, 1.0f, 999999));
    ProxyHierarchy h;
    h.build(instances);

    const ProxyNode &root = h.nodes()[size_t(h.root())];
    bool spanningAtRoot = false;
    for (uint32_t r = 0; r < root.residentCount; ++r) {
        if (h.instances()[h.residents()[root.residentFirst + r]].objectKey
            == 999999)
            spanningAtRoot = true;
    }
    EXPECT_TRUE(spanningAtRoot)
        << "a model-spanning instance must sit at the root, not in a cell";
}

TEST(ProxyHierarchy, nodeIdentityIsPositionalNotDataDependent)
{
    // §3.2's reason for a quantised grid over a median or SAH split: a
    // data-dependent split lets one moved part reshuffle the partition
    // and invalidate proxies for geometry that did not change. Two
    // consequences are testable — the partition does not depend on the
    // order instances arrive in, and nudging one part inside its own
    // cell changes nothing at all.
    auto a = flatAssembly(10);
    auto b = a;
    std::mt19937 rng(12345);
    std::shuffle(b.begin(), b.end(), rng);

    ProxyHierarchy ha;
    ProxyHierarchy hb;
    ha.build(a);
    hb.build(b);
    EXPECT_EQ(nodeIds(ha), nodeIds(hb))
        << "the partition depends on input order";

    auto nudged = a;
    nudged[0].bboxMin[0] += 0.001f;
    nudged[0].bboxMax[0] += 0.001f;
    ProxyHierarchy hn;
    hn.build(nudged);
    EXPECT_EQ(nodeIds(ha), nodeIds(hn))
        << "a sub-cell move reshuffled the partition";
}

// ---------------------------------------------------------------------
// The cut
// ---------------------------------------------------------------------

TEST(ProxyCutTest, drawCountFollowsCoverageNotModelSize)
{
    // The property being bought (§3): draws follow screen coverage, not
    // model size. This is the phase-1 measurement in miniature — the
    // number is (cells on the cut x material buckets present), taken
    // before a single proxy has been generated.
    ProxyHierarchy h;
    h.build(flatAssembly());

    float V[16];
    float P[16];
    viewAt(V, 10.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);

    uint32_t previous = 0xffffffffu;
    for (float tol : {8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f}) {
        ProxyCut cut;
        h.selectCut(V, P, 1200.0f, tol, cut);
        std::string why;
        ASSERT_TRUE(h.verifyCut(cut, &why)) << "tol " << tol << ": " << why;
        std::cout << "  tol " << tol << "px: draws " << cut.drawCount
                  << " (proxy " << cut.proxyDraws << " + exact "
                  << cut.exact.size() << "), covering "
                  << cut.coveredInstances << " of " << h.instances().size()
                  << "\n";
        EXPECT_LE(cut.drawCount, previous)
            << "a coarser tolerance cost more draws";
        previous = cut.drawCount;
    }
    // The floor at the tight end is not a property of the cut but of
    // the tree: below the tolerance its deepest level projects at, there
    // is nothing left to stop on and everything draws exactly. Which
    // level that is, is K's doing — see kIsWhatSharpensTheCut.

    // The win is what justifies phase 2, so it is asserted rather than
    // only printed — at a tolerance the whole model still reads at.
    ProxyCut coarse;
    h.selectCut(V, P, 1200.0f, 256.0f, coarse);
    EXPECT_LT(coarse.drawCount, h.instances().size() / 20)
        << "aggregation bought less than 20x on a lattice of 8000";
}

TEST(ProxyCutTest, kIsWhatSharpensTheCut)
{
    // §11.4 recorded K as pulling two ways — small for the size of a
    // pop, large for the draw count. The second half is wrong, and this
    // is where it shows: a leaf that misses the tolerance dumps *all*
    // its residents as exact draws, so a large K is a cliff K draws
    // deep, while a small K gives the cut a finer level to stop at.
    // Both correctness properties and the draw count want it small; the
    // real cost of small is generation and chunk count (§9.1).
    float V[16];
    float P[16];
    viewAt(V, 10.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);

    uint32_t previous = 0;
    for (uint32_t k : {4u, 8u, 16u, 32u, 64u, 128u}) {
        ProxyParams params;
        params.maxPerCell = k;
        ProxyHierarchy h;
        h.build(flatAssembly(), params);
        ProxyCut cut;
        h.selectCut(V, P, 1200.0f, 64.0f, cut);
        std::string why;
        ASSERT_TRUE(h.verifyCut(cut, &why)) << "K " << k << ": " << why;
        std::vector<uint32_t> proxyByLevel(h.stats().depth + 2, 0);
        for (int ni : cut.proxyNodes)
            proxyByLevel[h.nodes()[size_t(ni)].level] += 1;
        std::cout << "  K " << k << ": draws " << cut.drawCount << " (proxy "
                  << cut.proxyDraws << " + exact " << cut.exact.size()
                  << "), nodes " << h.nodes().size() << ", depth "
                  << h.stats().depth << ", proxies by level [";
        for (size_t l = 0; l < proxyByLevel.size(); ++l)
            std::cout << (l ? " " : "") << proxyByLevel[l];
        std::cout << "]\n";
        EXPECT_GE(cut.drawCount, previous)
            << "raising K to " << k << " reduced the draw count, which would "
               "mean K really does trade against it";
        previous = cut.drawCount;
    }
}

TEST(ProxyCutTest, aTightToleranceDescendsToEveryPart)
{
    // The near field must keep exact geometry: that is what editing and
    // picking need, and it is also why editing needs no special case
    // (§8) — you edit at close range, where the cut is already deep.
    ProxyHierarchy h;
    h.build(flatAssembly(10));

    float V[16];
    float P[16];
    viewAt(V, 10.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);

    ProxyCut cut;
    h.selectCut(V, P, 1200.0f, 0.0f, cut);
    EXPECT_TRUE(cut.proxyNodes.empty());
    EXPECT_EQ(cut.exact.size() + cut.culledInstances, h.instances().size());
    std::string why;
    EXPECT_TRUE(h.verifyCut(cut, &why)) << why;
}

TEST(ProxyCutTest, theInvariantHoldsAcrossToleranceAndDistance)
{
    // §11.3, asserted from the start because it is cheap here and
    // expensive to discover in phase 4:
    //
    //   every instance belongs to exactly one cell per level, and for
    //   each instance exactly one of {the instance, its covering proxy}
    //   draws.
    auto instances = flatAssembly(10, 4);
    instances.push_back(boxAt(0.0f, 0.0f, 0.0f, 1.0f, 999999, 2));
    ProxyHierarchy h;
    h.build(instances);

    float P[16];
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);
    for (float dist : {3.0f, 6.0f, 12.0f, 60.0f, 400.0f}) {
        float V[16];
        viewAt(V, dist);
        for (float tol : {0.0f, 1.0f, 4.0f, 16.0f, 64.0f, 256.0f, 4096.0f}) {
            ProxyCut cut;
            h.selectCut(V, P, 1200.0f, tol, cut);
            std::string why;
            EXPECT_TRUE(h.verifyCut(cut, &why))
                << "dist " << dist << " tol " << tol << ": " << why;
            EXPECT_EQ(cut.coveredInstances + cut.exact.size()
                          + cut.culledInstances,
                      h.instances().size())
                << "dist " << dist << " tol " << tol;
        }
    }
}

TEST(ProxyCutTest, aSpanningPartDrawsExactlyWhileItIsLarge)
{
    // The other half of assignment by size: the spanning part is a
    // resident of a node above the cut, and a node above the cut still
    // owes its own residents. Nothing below covers them.
    auto instances = flatAssembly(10);
    instances.push_back(boxAt(0.0f, 0.0f, 0.0f, 1.0f, 999999));
    ProxyHierarchy h;
    h.build(instances);

    float V[16];
    float P[16];
    viewAt(V, 10.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);

    ProxyCut cut;
    h.selectCut(V, P, 1200.0f, 64.0f, cut);
    bool drawnExactly = false;
    for (uint32_t idx : cut.exact) {
        if (h.instances()[idx].objectKey == 999999)
            drawnExactly = true;
    }
    EXPECT_TRUE(drawnExactly)
        << "the spanning part was swallowed by a proxy it is too big for";
}

TEST(ProxyCutTest, offScreenSubtreesAreDrawnByNobody)
{
    ProxyHierarchy h;
    h.build(flatAssembly(10));

    float V[16];
    float P[16];
    // Behind the camera: the whole model is off screen.
    viewAt(V, -20.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);

    ProxyCut cut;
    h.selectCut(V, P, 1200.0f, 16.0f, cut);
    EXPECT_EQ(cut.drawCount, 0u);
    EXPECT_EQ(cut.culledInstances, h.instances().size());
    std::string why;
    EXPECT_TRUE(h.verifyCut(cut, &why)) << why;
}

// ---------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------

TEST(ProxyMaterials, theCutCostsOneDrawPerBucketPerCell)
{
    // §5.1: the material is part of the partition, so a proxy carries
    // exactly one material and nothing is averaged. The price is a
    // fan-out below the cut, and this is what bounds it.
    float V[16];
    float P[16];
    viewAt(V, 10.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);

    uint32_t prev = 0;
    for (uint64_t buckets : {uint64_t(1), uint64_t(4), uint64_t(16)}) {
        ProxyHierarchy h;
        h.build(flatAssembly(16, buckets));
        ProxyCut cut;
        h.selectCut(V, P, 1200.0f, 64.0f, cut);

        // Never more than one draw per (proxy node, bucket).
        uint32_t bound = uint32_t(cut.exact.size())
            + uint32_t(cut.proxyNodes.size()) * uint32_t(buckets);
        EXPECT_LE(cut.drawCount, bound);
        EXPECT_GE(cut.drawCount, prev)
            << "more materials cannot cost fewer draws";
        prev = cut.drawCount;
    }
}

TEST(ProxyMaterials, identityFollowsTheSerializedBytes)
{
    Material a;
    Material b;
    EXPECT_EQ(materialIdentity(a), materialIdentity(b));

    b.diffuse = 0x11223344;
    EXPECT_NE(materialIdentity(a), materialIdentity(b));

    Material c;
    c.transparent = true;
    EXPECT_NE(materialIdentity(a), materialIdentity(c));

    Material d;
    d.type = Material::Line;
    EXPECT_NE(materialIdentity(a), materialIdentity(d));
}

// ---------------------------------------------------------------------
// Degenerate input
// ---------------------------------------------------------------------

TEST(ProxyHierarchy, emptyAndUnlocatableInputAreHarmless)
{
    ProxyHierarchy h;
    h.build({});
    EXPECT_TRUE(h.empty());

    ProxyCut cut;
    float V[16];
    float P[16];
    viewAt(V, 10.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);
    h.selectCut(V, P, 1200.0f, 16.0f, cut);
    EXPECT_EQ(cut.drawCount, 0u);

    // A draw with no bounds cannot be located, so it cannot be
    // partitioned; it must be dropped rather than placed at the origin.
    ProxyInstance nowhere;
    nowhere.bboxMin[0] = 1.0f;
    nowhere.bboxMax[0] = -1.0f;
    std::vector<ProxyInstance> mixed {nowhere, boxAt(0, 0, 0, 1, 7)};
    h.build(mixed);
    EXPECT_EQ(h.instances().size(), 1u);
}

TEST(ProxyCutTest, aStoppedNodeDrawsWhatNoProxyCanStandForItself)
{
    // Section 11.1g. Generation refuses lines, points and stand-in
    // boxes on purpose -- a decimated edge is not an edge -- so nothing
    // above one ever draws it. A cut that counted them as covered was
    // quoting as removed what would still have to be issued: 17-27% of
    // the covered figure on MiSTer, 99% of it edges.
    auto instances = flatAssembly(8);
    for (auto &inst : instances)
        inst.primCount = 100;
    // One in eight is an edge draw, spread through the model rather
    // than gathered, which is how a document's edges actually sit.
    uint64_t edges = 0, edgePrims = 0;
    for (size_t i = 0; i < instances.size(); i += 8) {
        instances[i].mergeable = false;
        instances[i].materialBucket = 7;
        ++edges;
        edgePrims += instances[i].primCount;
    }
    ProxyHierarchy h;
    h.build(instances);

    float V[16];
    float P[16];
    viewAt(V, 400.0f);  // far enough that the cut stops at the root
    perspective(P, 45.0f, 1.6f, 0.1f, 5000.0f);
    ProxyCut cut;
    h.selectCut(V, P, 1200.0f, 64.0f, cut);

    ASSERT_FALSE(cut.proxyNodes.empty());
    EXPECT_EQ(cut.unmergeableInstances, edges);
    EXPECT_EQ(cut.unmergeablePrims, edgePrims);
    EXPECT_EQ(cut.exact.size() + cut.coveredInstances + cut.culledInstances,
              instances.size())
        << "an instance was neither drawn nor covered nor culled";
    EXPECT_EQ(cut.coveredInstances, instances.size() - edges);
    // And the invariant agrees: an edge below a stopped node is drawn
    // by the cut, so marking it as covered too would be drawing it
    // twice.
    std::string why;
    EXPECT_TRUE(h.verifyCut(cut, &why)) << why;

    // The saving is what it is, not what ignoring the edges made it
    // look like.
    EXPECT_GT(cut.exactPrims, 0u);
    EXPECT_EQ(cut.exactPrims, edgePrims);
}

TEST(ProxyCutTest, theExactMassIsAttributedToTheNodesReason)
{
    // Section 11.1g. The priced cut said a proxy costs 4-9% of what it
    // replaces, which means the ceiling is what stays exact -- so the
    // exact mass has to be readable as something other than one number.
    // With no costs given there is no such thing as a missing proxy, so
    // every exact instance is a resident of a node whose extent the
    // camera resolves, and the totals must still add up to the cut's.
    auto instances = flatAssembly(10);
    for (auto &inst : instances)
        inst.primCount = 100;
    ProxyHierarchy h;
    h.build(instances);

    float V[16];
    float P[16];
    viewAt(V, 10.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);
    ProxyCut cut;
    h.selectCut(V, P, 1200.0f, 4.0f, cut);
    ASSERT_FALSE(cut.exact.empty());
    EXPECT_EQ(cut.exactNode.size(), cut.exact.size());

    ProxyExactBreakdown ex;
    h.explainExact(cut, V, P, 1200.0f, 4.0f, nullptr, ex);
    EXPECT_EQ(ex.total.instances, cut.exact.size());
    EXPECT_EQ(ex.total.prims, cut.exactPrims);
    EXPECT_EQ(ex.byReason[ProxyExactBreakdown::NoProxy].prims, 0u)
        << "a descent that never consulted a store found a missing proxy";
    EXPECT_EQ(ex.byReason[ProxyExactBreakdown::Resolvable].prims,
              cut.exactPrims);

    // The two cross-cuts are partitions of the same mass, not two
    // different measurements of it.
    uint64_t bySize = 0, byReason = 0;
    for (int b = 0; b < ProxyExactBreakdown::SizeBins; ++b)
        bySize += ex.bySize[b].prims;
    for (int r = 0; r < ProxyExactBreakdown::ReasonCount; ++r)
        byReason += ex.byReason[r].prims;
    EXPECT_EQ(bySize, ex.total.prims);
    EXPECT_EQ(byReason, ex.total.prims);
}

TEST(ProxyCutTest, aGenerationGapReadsDifferentlyFromNearField)
{
    // The distinction the whole readout exists for: geometry drawn
    // exactly because the camera can resolve it is the near field and
    // is correct, geometry drawn exactly because nothing was ever
    // generated above it is a gap. They are indistinguishable in the
    // exact count and must not be in the breakdown.
    auto instances = flatAssembly(10);
    for (auto &inst : instances)
        inst.primCount = 100;
    ProxyHierarchy h;
    h.build(instances);

    float V[16];
    float P[16];
    viewAt(V, 400.0f);  // far enough that nothing is resolvable
    perspective(P, 45.0f, 1.6f, 0.1f, 5000.0f);

    // Every node has a proxy of no error at all: the cut stops at the
    // root and there is no exact mass to explain.
    std::vector<ProxyNodeCost> costs(h.nodes().size());
    for (auto &cost : costs) {
        cost.errorRatio = 0.0f;
        cost.prims = 1;
        cost.draws = 1;
    }
    ProxyCut stopped;
    h.selectCut(V, P, 1200.0f, 4.0f, costs, stopped);
    ProxyExactBreakdown none;
    h.explainExact(stopped, V, P, 1200.0f, 4.0f, &costs, none);
    EXPECT_EQ(none.total.prims, 0u);

    // Nothing generated anywhere: the same camera, the same tolerance,
    // and now the whole model draws exactly -- as a gap, not as near
    // field.
    for (auto &cost : costs)
        cost.errorRatio = -1.0f;
    ProxyCut gap;
    h.selectCut(V, P, 1200.0f, 4.0f, costs, gap);
    ProxyExactBreakdown ex;
    h.explainExact(gap, V, P, 1200.0f, 4.0f, &costs, ex);
    EXPECT_EQ(ex.total.prims, gap.exactPrims);
    EXPECT_GT(ex.total.prims, 0u);
    EXPECT_EQ(ex.byReason[ProxyExactBreakdown::NoProxy].prims, ex.total.prims);
    EXPECT_EQ(ex.byReason[ProxyExactBreakdown::Resolvable].prims, 0u);
}

TEST(ProxyCutTest, theSizeCrossCutSaysWhetherTheMassIsAddressable)
{
    // Reason alone cannot answer the question, because "the node is
    // resolvable" is true of a node holding one large part and of a
    // node holding a thousand small ones that happens to sit high in
    // the tree. What separates them is the instance's own projected
    // size: below the tolerance it is detail nothing merged, well above
    // it, it is near field whatever the reason says.
    auto instances = flatAssembly(10);
    for (auto &inst : instances)
        inst.primCount = 10;
    // One part spanning the whole model, which no cell below the root
    // can hold and which is therefore a root resident.
    ProxyInstance big = boxAt(0.0f, 0.0f, 0.0f, 0.9f, 999999);
    big.primCount = 1000;
    instances.push_back(big);

    ProxyHierarchy h;
    h.build(instances);
    float V[16];
    float P[16];
    viewAt(V, 6.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);
    ProxyCut cut;
    h.selectCut(V, P, 1200.0f, 4.0f, cut);

    ProxyExactBreakdown ex;
    h.explainExact(cut, V, P, 1200.0f, 4.0f, nullptr, ex);
    // The big part is drawn exactly and lands in the coarsest bin.
    EXPECT_GE(ex.bySize[ProxyExactBreakdown::SizeBins - 1].prims, 1000u);
    // A tolerance the whole model is smaller than puts everything in
    // the first bin instead, which is what "addressable" would look
    // like if it were true.
    ProxyExactBreakdown wide;
    h.explainExact(cut, V, P, 1200.0f, 100000.0f, nullptr, wide);
    EXPECT_EQ(wide.bySize[0].prims, wide.total.prims);
    EXPECT_EQ(wide.total.prims, ex.total.prims)
        << "the tolerance changed what the same cut is made of";
}

TEST(ProxyHierarchy, coincidentInstancesDoNotRecurseForever)
{
    // Ten thousand parts at the same point cannot be separated by any
    // amount of subdivision. The depth cap is what stops this, and it
    // is the reason the cap exists at all.
    std::vector<ProxyInstance> pile;
    for (uint64_t i = 0; i < 10000; ++i)
        pile.push_back(boxAt(0.0f, 0.0f, 0.0f, 0.5f, i + 1));
    ProxyHierarchy h;
    h.build(pile);
    EXPECT_EQ(h.instances().size(), 10000u);
    EXPECT_LE(h.stats().depth, h.params().maxLevel);

    float V[16];
    float P[16];
    viewAt(V, 10.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);
    ProxyCut cut;
    h.selectCut(V, P, 1200.0f, 16.0f, cut);
    std::string why;
    EXPECT_TRUE(h.verifyCut(cut, &why)) << why;
}

TEST(ProxyHierarchy, theSubtreeWalkYieldsExactlyWhatAProxyWouldStandFor)
{
    // Generation merges everything a node covers (§7.1), so the walk
    // has to agree with the count the node advertises -- and with the
    // draw rows the instances came from, since the geometry is fetched
    // through those and a partition that renumbered them would merge
    // the wrong meshes.
    ProxyHierarchy h;
    h.build(flatAssembly(12, 3));

    std::vector<uint32_t> all;
    h.subtreeInstances(h.root(), all);
    EXPECT_EQ(all.size(), h.instances().size());
    std::set<uint32_t> distinct(all.begin(), all.end());
    EXPECT_EQ(distinct.size(), all.size()) << "an instance covered twice";

    for (size_t i = 0; i < h.nodes().size(); ++i) {
        std::vector<uint32_t> subtree;
        h.subtreeInstances(int(i), subtree);
        ASSERT_EQ(subtree.size(), h.nodes()[i].subtreeCount) << "node " << i;
    }

    // The join back to the caller's table survives the projection.
    for (uint32_t inst : all)
        EXPECT_LT(h.instances()[inst].drawIndex, h.instances().size());
    h.subtreeInstances(kNoProxyNode, all);  // harmless on an empty child
}

TEST(ProxyHierarchy, theDrawRowSurvivesTheProjection)
{
    // proxyInstances() skips draws it cannot locate, so the row a
    // ProxyInstance came from is not its position in the table.
    DrawCallList draws(4);
    for (auto &d : draws) {
        d.bboxMin[0] = d.bboxMin[1] = d.bboxMin[2] = 0.0f;
        d.bboxMax[0] = d.bboxMax[1] = d.bboxMax[2] = 1.0f;
    }
    draws[1].bboxMin[0] = 1.0f;  // empty bounds: not judgeable, skipped
    draws[1].bboxMax[0] = 0.0f;
    std::vector<ProxyInstance> instances;
    proxyInstances(draws, instances);
    ASSERT_EQ(instances.size(), 3u);
    EXPECT_EQ(instances[0].drawIndex, 0u);
    EXPECT_EQ(instances[1].drawIndex, 2u);
    EXPECT_EQ(instances[2].drawIndex, 3u);
}

TEST(BoxNearPlane, aBoxWhollyInFrontOfTheCameraIsAnswerable)
{
    // The ordinary case: an occlusion test can rasterize this box's
    // front faces, so nothing exempts it.
    float V[16], P[16];
    viewAt(V, 100.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);
    const float mn[3] = {-1.0f, -1.0f, -1.0f};
    const float mx[3] = {1.0f, 1.0f, 1.0f};
    EXPECT_FALSE(boxReachesNearPlane(mn, mx, V, P, true));
    EXPECT_FALSE(boxReachesNearPlane(mn, mx, V, P, false));
}

TEST(BoxNearPlane, aBoxTheCameraStandsInsideIsNotAnswerable)
{
    // Its front faces are behind the eye and get clipped away, leaving
    // only faces its own contents hide — so a query on it would report
    // it hidden however plainly it is in view.
    float V[16], P[16];
    viewAt(V, 5.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);
    const float mn[3] = {-50.0f, -50.0f, -50.0f};
    const float mx[3] = {50.0f, 50.0f, 50.0f};
    EXPECT_TRUE(boxReachesNearPlane(mn, mx, V, P, true));
    EXPECT_TRUE(boxReachesNearPlane(mn, mx, V, P, false));
}

TEST(BoxNearPlane, aBoxStraddlingTheNearPlaneIsNotAnswerable)
{
    // The camera is outside the box, so nothing about its bounds says
    // it is a special case — only the near plane cutting through it
    // does, which is exactly what the whole-model box of a tightly
    // fitted camera does.
    float V[16], P[16];
    viewAt(V, 10.0f);
    perspective(P, 45.0f, 1.6f, 1.0f, 1000.0f);
    // Spans z = -2..12 in world, i.e. 9.0 down to -2.0 in front of a
    // camera whose near plane sits at 1.0.
    const float mn[3] = {-1.0f, -1.0f, -2.0f};
    const float mx[3] = {1.0f, 1.0f, 12.0f};
    EXPECT_TRUE(boxReachesNearPlane(mn, mx, V, P, true));
}

TEST(BoxNearPlane, degenerateInputIsNotAnswerable)
{
    // Empty bounds have no faces to rasterize; refusing to test them is
    // the same answer as refusing a clipped box, and for the same
    // reason.
    float V[16], P[16];
    viewAt(V, 100.0f);
    perspective(P, 45.0f, 1.6f, 0.1f, 1000.0f);
    const float mn[3] = {1.0f, 1.0f, 1.0f};
    const float mx[3] = {-1.0f, -1.0f, -1.0f};
    EXPECT_TRUE(boxReachesNearPlane(mn, mx, V, P, true));
    EXPECT_TRUE(boxReachesNearPlane(nullptr, mx, V, P, true));
}

TEST(ProxyHierarchy, aNodeIdNamesOneNodeAndOnlyOne)
{
    // The id is what a generation cache is keyed by (section 7), so two
    // nodes sharing one is not a cosmetic fault: it serves one node's
    // proxy for another node's geometry. It happened -- the Morton
    // spread used the masks of a two-dimensional code, which puts bit i
    // at bit 2i, so the three interleaved halves overlapped and
    // (0,0,1) collided with (2,0,0).
    ProxyHierarchy h;
    ProxyParams params;
    params.maxPerCell = 8;
    h.build(flatAssembly(16), params);
    ASSERT_GT(h.nodes().size(), 8u);

    std::map<uint64_t, size_t> seen;
    for (size_t i = 0; i < h.nodes().size(); ++i) {
        const ProxyNode &node = h.nodes()[i];
        const auto it = seen.find(node.id);
        ASSERT_EQ(it, seen.end())
            << "nodes " << it->second << " and " << i << " share id "
            << node.id;
        seen[node.id] = i;
    }

    // And it is positional: the same id comes back for the same level
    // and cell, which is what lets the cache outlive the session.
    ProxyHierarchy again;
    again.build(flatAssembly(16), params);
    ASSERT_EQ(again.nodes().size(), h.nodes().size());
    for (size_t i = 0; i < h.nodes().size(); ++i)
        EXPECT_EQ(again.nodes()[i].id, h.nodes()[i].id);
}
