// Tests for occlusion culling over the spatial index
// (docs/FarFieldProxies.md §12). Like the ProxyHierarchy tests beside
// them these need no GL context and no document: the part worth testing
// is the visibility *policy* — who is tested, when a verdict expires,
// and what happens when the answers do not arrive — and that part is
// deliberately free of any backend.
//
// The bias every test here encodes: **being wrong must cost frame time,
// never pixels.** A node that draws when it could have been skipped is a
// missed optimization; a node that is skipped when it should have drawn
// is missing geometry. So the cases below are mostly about the ways a
// node could get stuck hidden.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "Gui/Renderer/OcclusionCull.h"

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

/// Camera on +Z looking down -Z at the origin, \a dist away.
void viewAt(float *V, float dist)
{
    for (int i = 0; i < 16; ++i)
        V[i] = 0.0f;
    V[0] = V[5] = V[10] = V[15] = 1.0f;
    V[14] = -dist;
}

ProxyInstance boxAt(float cx, float cy, float cz, float half, uint64_t key,
                    uint32_t drawIndex)
{
    ProxyInstance inst;
    inst.objectKey = key;
    inst.drawIndex = drawIndex;
    inst.bboxMin[0] = cx - half;
    inst.bboxMin[1] = cy - half;
    inst.bboxMin[2] = cz - half;
    inst.bboxMax[0] = cx + half;
    inst.bboxMax[1] = cy + half;
    inst.bboxMax[2] = cz + half;
    return inst;
}

/// A lattice spanning [-1,1], one draw row per instance.
std::vector<ProxyInstance> lattice(int n = 10)
{
    std::vector<ProxyInstance> out;
    out.reserve(size_t(n) * n * n);
    const float step = 2.0f / float(n);
    uint32_t row = 0;
    for (int x = 0; x < n; ++x) {
        for (int y = 0; y < n; ++y) {
            for (int z = 0; z < n; ++z) {
                out.push_back(boxAt(-1.0f + (float(x) + 0.5f) * step,
                                    -1.0f + (float(y) + 0.5f) * step,
                                    -1.0f + (float(z) + 0.5f) * step,
                                    0.2f * step, row + 1, row));
                ++row;
            }
        }
    }
    return out;
}

/// The camera the measurements were taken from: far enough out that the
/// whole model is on screen and no box reaches the near plane, which is
/// the only configuration in which a box test can answer at all.
struct Camera {
    float V[16];
    float P[16];
    float height = 1000.0f;
    Camera()
    {
        viewAt(V, 12.0f);
        perspective(P, 45.0f, 1.6f, 0.5f, 100.0f);
    }
};

/// Answer every test in \a batch the same way — the backend the state
/// machine is being examined without.
void answerAll(OcclusionCuller &c, const OcclusionTestBatch &batch,
               bool visible)
{
    for (int node : batch.nodes)
        c.result(node, visible);
}

/// The shape of the scene this mechanism exists for: an enclosure whose
/// outside is visible and whose interior is not. Nodes at or below
/// \a hideFromLevel answer hidden, the ones above them visible.
///
/// Answering *everything* hidden is not a stricter version of this, it
/// is a degenerate one: the root is then hidden, its subtree is not
/// descended, and there is never a second node to test — which
/// exercises none of the scheduling.
void answerByLevel(OcclusionCuller &c, const OcclusionTestBatch &batch,
                   uint32_t hideFromLevel)
{
    const auto &nodes = c.hierarchy().nodes();
    for (int node : batch.nodes)
        c.result(node, nodes[size_t(node)].level < hideFromLevel);
}

/// Run \a frames frames under the enclosure policy and leave \a mask
/// holding the last one. Several frames because a verdict is a frame
/// old by construction and the descent only reaches a node once its
/// parent has answered visible — depth costs rounds.
void settle(OcclusionCuller &c, const float *V, const float *P, float height,
            std::vector<uint8_t> &mask, OcclusionTestBatch &batch,
            int frames, uint32_t hideFromLevel = 1)
{
    for (int i = 0; i < frames; ++i) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(V, P, height, true, mask, batch);
        answerByLevel(c, batch, hideFromLevel);
    }
}

uint32_t countSet(const std::vector<uint8_t> &mask)
{
    uint32_t n = 0;
    for (uint8_t m : mask)
        n += m ? 1 : 0;
    return n;
}

}  // namespace

// ---------------------------------------------------------------------
// What a verdict does
// ---------------------------------------------------------------------

TEST(OcclusionCull, knowingNothingDrawsEverything)
{
    // The first frame after a build has no answers yet, and the only
    // safe reading of "no answer" is "draws".
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    EXPECT_EQ(countSet(mask), 0u);
    EXPECT_GT(batch.nodes.size(), 0u) << "nothing was even offered for test";
}

TEST(OcclusionCull, aHiddenNodeMasksItsWholeSubtree)
{
    // The economy of testing per node rather than per object: one
    // answer has to remove everything below it, or the mechanism is
    // just a slower per-object test.
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    settle(c, cam.V, cam.P, cam.height, mask, batch, 4);
    EXPECT_GT(countSet(mask), 0u);
    EXPECT_GT(c.lastFrame().hiddenInstances, c.lastFrame().nodesHidden)
        << "a hidden node stood for no more than itself";
}

TEST(OcclusionCull, theMaskIsOnlyEverAdditive)
{
    // It composes with the frustum mask the renderer already computed,
    // so clearing a bit would put back a draw the frustum rejected.
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 1);
    OcclusionTestBatch batch;
    c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    EXPECT_EQ(countSet(mask), 1000u);
}

TEST(OcclusionCull, aVisibleAnswerGivesTheSubtreeBack)
{
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    settle(c, cam.V, cam.P, cam.height, mask, batch, 4);
    ASSERT_GT(countSet(mask), 0u);

    // The enclosure opens: every test now answers visible.
    for (int i = 0; i < 4; ++i) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch);
        answerAll(c, batch, true);
    }
    EXPECT_EQ(countSet(mask), 0u);
}

// ---------------------------------------------------------------------
// The ways a node could get stuck hidden — the failures that cost
// pixels rather than time
// ---------------------------------------------------------------------

TEST(OcclusionCull, aHiddenNodeIsAlwaysOfferedATestBack)
{
    // A hidden node is not descended, so the only thing that can ever
    // change its mind is a test of itself. If a frame stopped offering
    // one, the geometry would be gone for good.
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    settle(c, cam.V, cam.P, cam.height, mask, batch, 4);
    ASSERT_GT(c.lastFrame().nodesHidden, 0u);

    for (int frame = 0; frame < 5; ++frame) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch);
        ASSERT_GT(c.lastFrame().nodesHidden, 0u) << "frame " << frame;
        EXPECT_GT(batch.nodes.size(), 0u)
            << "frame " << frame << ": hidden nodes were offered no way back";
        answerByLevel(c, batch, 1);
    }
}

TEST(OcclusionCull, answersThatNeverArriveRevertToVisible)
{
    // ⚠️ The fail-safe. Offers are made and simply never answered — no
    // query handles, a backend that dropped the batch. Ageing is
    // measured from the last *answer* precisely so that this case
    // eventually gives the geometry back.
    OcclusionCullConfig conf;
    conf.maxHiddenFrames = 10;
    OcclusionCuller c;
    c.configure(conf);
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    settle(c, cam.V, cam.P, cam.height, mask, batch, 4);
    ASSERT_GT(countSet(mask), 0u);

    bool everHidden = false;
    for (int frame = 0; frame < 40; ++frame) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch);
        if (countSet(mask))
            everHidden = true;
        // Offered and abandoned, over and over — the starvation case.
        for (int node : batch.nodes)
            c.abandon(node);
    }
    EXPECT_TRUE(everHidden) << "the test never got as far as hiding anything";
    std::fill(mask.begin(), mask.end(), 0);
    c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    EXPECT_EQ(countSet(mask), 0u)
        << "geometry stayed hidden although no test was ever answered";
}

TEST(OcclusionCull, confirmationsDoNotFlickerTheGeometryBack)
{
    // The other side of the fail-safe: while the answers keep arriving
    // and keep saying hidden, the node stays hidden however long that
    // runs. A lifetime applied to confirmations would show a frame of
    // the whole interior every maxHiddenFrames.
    OcclusionCullConfig conf;
    conf.maxHiddenFrames = 4;
    OcclusionCuller c;
    c.configure(conf);
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    settle(c, cam.V, cam.P, cam.height, mask, batch, 4);
    ASSERT_GT(countSet(mask), 0u);

    for (int frame = 0; frame < 20; ++frame) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch);
        EXPECT_GT(countSet(mask), 0u) << "frame " << frame << " un-hid a node "
                                         "whose tests all answered hidden";
        answerByLevel(c, batch, 1);
        EXPECT_EQ(c.lastFrame().forcedVisible, 0u) << "frame " << frame;
    }
}

TEST(OcclusionCull, aStarvedBudgetStillRotatesThroughEveryHiddenNode)
{
    // With fewer tests per frame than there are hidden nodes, a cursor
    // rotates the offers. Without it the same prefix would be re-tested
    // forever and the tail would only ever return via the fail-safe.
    OcclusionCullConfig conf;
    conf.budget = 8;                 // fewer than the enclosure hides
    conf.maxHiddenFrames = 100000;   // out of the way; the cursor is the test
    OcclusionCuller c;
    c.configure(conf);
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    // Hidden two levels down, not one: a hidden node is not descended,
    // so hiding at level 1 puts a ceiling of eight octants on how many
    // nodes can ever be hidden — a ceiling of the scene, not of the
    // budget, and it would make this test pass for the wrong reason.
    settle(c, cam.V, cam.P, cam.height, mask, batch, 200, 2);
    const uint32_t hidden = c.lastFrame().nodesHidden;
    // Growing past the budget is itself the property: re-tests are
    // capped precisely so that discovery cannot be starved out.
    ASSERT_GT(hidden, conf.budget)
        << "culling froze at the budget instead of growing past it";

    std::set<int> offered;
    for (uint32_t frame = 0; frame < hidden * 4; ++frame) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch);
        for (int node : batch.nodes)
            offered.insert(node);
        answerByLevel(c, batch, 2);
    }
    EXPECT_GE(offered.size(), size_t(hidden))
        << "some hidden nodes were never offered a test at all";
}

TEST(OcclusionCull, aRebuildForgetsEveryVerdict)
{
    // ⚠️ Node identity is positional and stable, but node *indices* are
    // not. A verdict carried across a rebuild would hide whatever
    // landed at that index — geometry that was never tested.
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    settle(c, cam.V, cam.P, cam.height, mask, batch, 4);
    ASSERT_GT(countSet(mask), 0u);

    c.build(lattice());
    std::fill(mask.begin(), mask.end(), 0);
    c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    EXPECT_EQ(countSet(mask), 0u) << "a verdict survived the partition it "
                                     "was taken over";
}

TEST(OcclusionCull, anEmptyIndexMasksNothing)
{
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    Camera cam;
    std::vector<uint8_t> mask(16, 0);
    OcclusionTestBatch batch;
    c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    EXPECT_EQ(countSet(mask), 0u);
    EXPECT_TRUE(batch.nodes.empty());
}

// ---------------------------------------------------------------------
// What cannot be asked
// ---------------------------------------------------------------------

TEST(OcclusionCull, aCameraInsideTheModelTestsNothingItCannotAnswer)
{
    // ⚠️ A box the near plane clips cannot be tested: the faces that
    // would prove it visible are clipped away and the ones left are
    // hidden by the box's own contents, so the query answers "hidden"
    // however plainly the node is in view. The camera inside the model
    // is §10.3's working case, not an edge case, so what matters is
    // that those nodes are exempted and *still descended* — a child
    // further off may be answerable.
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    float V[16], P[16];
    viewAt(V, 0.0f);   // at the model centre
    perspective(P, 60.0f, 1.6f, 0.1f, 100.0f);
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    c.cull(V, P, 1000.0f, true, mask, batch);

    EXPECT_GT(c.lastFrame().nearExempt, 0u)
        << "no node was exempted although the camera is inside the model";
    EXPECT_GT(c.lastFrame().nodesVisited, c.lastFrame().nearExempt)
        << "an unanswerable node stopped the descent instead of continuing it";
    EXPECT_EQ(countSet(mask), 0u) << "an unanswerable node was culled anyway";
}

TEST(OcclusionCull, offscreenNodesAreLeftToTheFrustum)
{
    // Counted, never masked here: attributing the frustum's rejections
    // to occlusion would be the easiest way to report a win that
    // already existed.
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    float V[16], P[16];
    viewAt(V, 12.0f);
    // Looking away: a narrow field of view with the model off to one
    // side leaves the root outside the frustum.
    perspective(P, 5.0f, 1.6f, 0.5f, 8.0f);
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    c.cull(V, P, 1000.0f, true, mask, batch);
    EXPECT_EQ(countSet(mask), 0u);
    EXPECT_EQ(c.lastFrame().nodesHidden, 0u);
}

TEST(OcclusionCull, tinySubtreesAreNotWorthATest)
{
    // A test is itself a draw, so a node standing for fewer draws than
    // the test costs loses whichever way it answers.
    OcclusionCullConfig conf;
    conf.minSubtree = 1000000;
    OcclusionCuller c;
    c.configure(conf);
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    EXPECT_TRUE(batch.nodes.empty())
        << "a node below the size threshold was tested anyway";
}

// ---------------------------------------------------------------------
// The property the whole box test rests on
// ---------------------------------------------------------------------

TEST(OcclusionCull, aNodesBoxContainsEverythingBelowIt)
{
    // ⭐⭐ The one assumption an occlusion test cannot survive losing. A
    // node is culled by rasterizing its bounds and asking whether any
    // fragment passed; that answer only stands for the node's contents
    // if the bounds *contain* the contents. A box that is too small can
    // sit behind an occluder while the geometry it stands for pokes out
    // in front of it, and everything below it is then deleted from a
    // frame it was plainly visible in.
    //
    // The cells of this partition are deliberately loose (§3.2) -- an
    // instance is assigned by its centre, so its box may reach half a
    // cell beyond the cell's nominal bounds -- which is exactly the
    // arrangement in which nominal bounds and content bounds part
    // company. Nothing here may be judged by cellMin/cellMax.
    ProxyHierarchy h;
    h.build(lattice(12));
    const auto &nodes = h.nodes();
    const auto &inst = h.instances();
    std::vector<uint32_t> members;
    for (size_t n = 0; n < nodes.size(); ++n) {
        members.clear();
        h.subtreeInstances(int(n), members);
        ASSERT_EQ(members.size(), nodes[n].subtreeCount) << "node " << n;
        for (uint32_t m : members) {
            for (int k = 0; k < 3; ++k) {
                EXPECT_LE(nodes[n].contentMin[k], inst[m].bboxMin[k])
                    << "node " << n << " axis " << k
                    << ": content bounds do not reach instance " << m;
                EXPECT_GE(nodes[n].contentMax[k], inst[m].bboxMax[k])
                    << "node " << n << " axis " << k
                    << ": content bounds do not reach instance " << m;
            }
        }
    }
}
