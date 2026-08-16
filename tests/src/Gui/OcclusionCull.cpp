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
///
/// ⚠️ And more of them than that suggests: a hidden answer is not acted
/// on until `hiddenConfirm` of them have arrived in a row, and a node
/// part-way through that streak is an ordinary visible node, so its
/// next test waits out `visibleTtl`. Culling therefore engages in
/// roughly visibleTtl * hiddenConfirm frames, not in one. Callers that
/// want a settled mask must budget for it; the alternative — settling
/// with the confirmations turned off — would leave the default
/// configuration untested by everything below.
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
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);
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
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);
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
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);
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
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);
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
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);
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
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);
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

// ---------------------------------------------------------------------
// The box the test rasterizes has to be a box
// ---------------------------------------------------------------------

TEST(OcclusionCull, theTestBoxIsClosedAndAllOfItIsSurface)
{
    // ⭐⭐ The second assumption the box test cannot survive losing, and
    // the one that was silently false. The verdict "no fragment of this
    // box passed the depth test" only stands for the node if what
    // rasterized was the box's *surface*: an interior surface lies
    // deeper than the front face it stands in for, so LEQUAL rejects it
    // and the node reports itself hidden while in plain view.
    //
    // The table this checks was once written with a face's corners in
    // the order 0,1,2,3, which reads naturally and is wrong -- the
    // corners are bit-encoded, so a face's rim order is 0,1,3,2. Four of
    // the twelve triangles came out as diagonal cross-sections through
    // the interior, two faces were missing entirely and two more were a
    // quarter short. It drew something from every angle, so nothing
    // failed; it merely deleted geometry.
    //
    // Checked as properties rather than by comparing against a second
    // copy of the table, which would only assert that two transcriptions
    // agree.
    const unsigned short *idx = Render::occlusionBoxIndices();
    // Corner i takes x from bit 0, y from bit 1, z from bit 2.
    auto corner = [](unsigned short i, int axis) {
        return float((i >> axis) & 1);
    };

    double total = 0.0;
    int perFace[3][2] = {};
    for (int t = 0; t < 12; ++t) {
        const unsigned short a = idx[t * 3], b = idx[t * 3 + 1],
                             c = idx[t * 3 + 2];
        ASSERT_LT(a, 8); ASSERT_LT(b, 8); ASSERT_LT(c, 8);
        EXPECT_TRUE(a != b && b != c && a != c)
            << "triangle " << t << " is degenerate";
        // Every triangle must lie in a face plane: all three corners
        // agreeing on one axis, at that axis' minimum or maximum.
        int planes = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const float v = corner(a, axis);
            if (corner(b, axis) == v && corner(c, axis) == v) {
                ++planes;
                ++perFace[axis][int(v)];
            }
        }
        EXPECT_EQ(planes, 1)
            << "triangle " << t << " is not a face of the box -- a "
               "diagonal cross-section answers with the interior";
        // Area of the unit-cube triangle, to sum against the surface.
        float u[3], w[3];
        for (int k = 0; k < 3; ++k) {
            u[k] = corner(b, k) - corner(a, k);
            w[k] = corner(c, k) - corner(a, k);
        }
        const float cx = u[1] * w[2] - u[2] * w[1];
        const float cy = u[2] * w[0] - u[0] * w[2];
        const float cz = u[0] * w[1] - u[1] * w[0];
        total += 0.5 * std::sqrt(double(cx * cx + cy * cy + cz * cz));
    }
    // Two triangles per face, six faces, and the total area exactly the
    // unit cube's surface: enough triangles to close it, no overlap to
    // hide a hole behind. A quarter-covered face fails this even though
    // its two triangles are both genuinely on the face plane.
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            EXPECT_EQ(perFace[axis][side], 2)
                << "axis " << axis << " side " << side
                << " is not closed by exactly two triangles";
        }
    }
    EXPECT_NEAR(total, 6.0, 1e-5)
        << "the twelve triangles do not add up to the box's surface: "
           "either a face is uncovered or two triangles overlap and "
           "leave a hole elsewhere";
}

// ---------------------------------------------------------------------
// Padding in the depth buffer's units, not the model's
// ---------------------------------------------------------------------

TEST(OcclusionCull, depthPaddingGrowsWithDistanceAndIgnoresBoxSize)
{
    // The pad exists to beat the depth buffer's own resolution, which
    // depends on where the box is and not at all on how big it is --
    // the opposite of padFraction, and the reason both terms are
    // needed. A small part flush on a large panel has a small diagonal
    // and therefore a small relative pad, at a distance where one depth
    // step is far larger.
    const float V[16] = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};
    // Perspective, GL layout: P[14] = -2fn/(f-n), P[15] = 0.
    const float n = 0.1f, f = 1000.0f;
    float P[16] = {};
    P[0] = P[5] = 1.0f;
    P[10] = -(f + n) / (f - n);
    P[11] = -1.0f;
    P[14] = -2.0f * f * n / (f - n);

    // The near face is held at `depth` and only the lateral extent
    // varies: growing the box towards the camera instead would move the
    // very corner the pad is computed at, which is a different question
    // and the reason a first draft of this test failed.
    auto padAt = [&](float depth, float lateral) {
        const float lo[3] = {-lateral, -lateral, -depth - 1.0f};
        const float hi[3] = {lateral, lateral, -depth};
        return Render::depthQuantumPad(lo, hi, V, P, true, 16.0f);
    };

    // Same box, four times the distance: the depth step goes with the
    // square of it, so the pad must too.
    const float near_ = padAt(10.0f, 0.5f);
    const float far_ = padAt(40.0f, 0.5f);
    EXPECT_GT(near_, 0.0f);
    EXPECT_NEAR(far_ / near_, 16.0f, 0.5f);

    // Same nearest depth, a box a hundred times wider: unchanged. This
    // is the whole point -- it is not a tolerance on the geometry, so
    // the term padFraction supplies cannot stand in for it.
    EXPECT_NEAR(padAt(10.0f, 0.5f), padAt(10.0f, 50.0f), 1e-6f);

    // A box behind the eye has no answerable depth; the near-plane
    // exemption is that case, not this one.
    const float behind[3] = {-1.0f, -1.0f, 1.0f};
    const float behindMax[3] = {1.0f, 1.0f, 3.0f};
    EXPECT_EQ(Render::depthQuantumPad(behind, behindMax, V, P, true, 16.0f),
              0.0f);
    // Asking for no padding is allowed: it is the un-padded box test,
    // and being able to ask for it back is what let the padding be
    // measured against the picture rather than asserted.
    const float lo[3] = {-1, -1, -11}, hi[3] = {1, 1, -9};
    EXPECT_EQ(Render::depthQuantumPad(lo, hi, V, P, true, 0.0f), 0.0f);
}

// ---------------------------------------------------------------------
// Stability: the difference between right on average and holding still
// ---------------------------------------------------------------------

TEST(OcclusionCull, anAnswerThatFlipsBackIsNeverActedOn)
{
    // ⭐⭐ The oscillator, and the reason a hidden verdict is confirmed
    // rather than obeyed. A test is issued against one frame's depth
    // and read against a later one -- non-blocking by design -- so
    // while it is in flight other nodes are culled and the occluders
    // move underneath the answer. Obeyed singly, a node tested while an
    // occluder was still drawn is skipped after that occluder has gone;
    // the hole it leaves tests visible; it returns; and it alternates.
    // Measured on the server assembly that swung the drawn set between
    // 12 and 8424 instances of 17727, and two captures of the same
    // static scene 30 s apart differed in 14101 pixels (§12.6).
    //
    // Whatever produces it, the signature is this: answers that do not
    // agree with each other. Nothing here may act on them.
    OcclusionCullConfig conf;
    ASSERT_GT(conf.hiddenConfirm, 1u)
        << "the default acts on a single answer; there is no hysteresis "
           "left to test";
    OcclusionCuller c;
    c.configure(conf);
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    ASSERT_FALSE(batch.nodes.empty());

    // Not the root: a hidden root is refused outright by a different
    // guard, which would pass this test for the wrong reason.
    int node = kNoProxyNode;
    for (int n : batch.nodes) {
        if (n != c.hierarchy().root()) {
            node = n;
            break;
        }
    }
    ASSERT_NE(node, kNoProxyNode);

    for (int i = 0; i < 20; ++i) {
        c.result(node, (i % 2) == 0);
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch);
        ASSERT_EQ(c.lastFrame().nodesHidden, 0u)
            << "frame " << i << ": acted on an answer that the next test "
               "contradicted";
    }
    EXPECT_EQ(countSet(mask), 0u);

    // ...and the hysteresis must not be a refusal to ever cull: the
    // same node, answering consistently, is skipped once the streak is
    // reached. The loop above ended on a hidden answer, so the streak
    // stands at one.
    for (uint32_t i = 1; i < conf.hiddenConfirm; ++i) {
        c.result(node, false);
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    }
    EXPECT_GT(c.lastFrame().nodesHidden, 0u)
        << "answers that all agreed were never acted on";
    EXPECT_GT(countSet(mask), 0u);
}

TEST(OcclusionCull, oneVisibleAnswerOutweighsAnyRunOfHiddenOnes)
{
    // The asymmetry the whole file is built on, applied to the streak:
    // evidence for skipping accumulates, evidence for drawing never
    // has to. A node that has answered hidden a hundred times and then
    // sees a single pixel draws again immediately, and must re-earn the
    // whole streak before it can be skipped once more.
    OcclusionCullConfig conf;
    OcclusionCuller c;
    c.configure(conf);
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);
    ASSERT_GT(countSet(mask), 0u);

    // The enclosure opens. Several rounds, not one: a hidden node is
    // not descended, so each frame only offers the frontier of them and
    // the level below is not even reached until the level above has
    // answered visible.
    for (int i = 0; i < 8; ++i) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch);
        answerAll(c, batch, true);
    }
    std::fill(mask.begin(), mask.end(), 0);
    c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    ASSERT_EQ(c.lastFrame().nodesHidden, 0u)
        << "a node stayed skipped after an answer saw pixels in it";

    // And a single hidden answer does not put it straight back.
    answerAll(c, batch, false);
    std::fill(mask.begin(), mask.end(), 0);
    c.cull(cam.V, cam.P, cam.height, true, mask, batch);
    EXPECT_EQ(c.lastFrame().nodesHidden, 0u)
        << "the streak resumed where it left off instead of restarting";
}

// ---------------------------------------------------------------------
// The attribution the readout traces an over-cull with
// (docs/FarFieldProxies.md §12.10). None of this steers the mechanism;
// what it protects is the ability to say *which verdict* deleted a draw
// — and every earlier session of this workstream was lost to a
// measurement that could not.
// ---------------------------------------------------------------------

TEST(OcclusionCull, everyMaskedRowNamesTheVerdictThatCutIt)
{
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    std::vector<int32_t> owner;
    OcclusionTestBatch batch;
    for (int i = 0; i < 24; ++i) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch, &owner);
        answerByLevel(c, batch, 1);
    }
    ASSERT_GT(countSet(mask), 0u);
    ASSERT_EQ(owner.size(), mask.size());

    const auto &nodes = c.hierarchy().nodes();
    uint32_t attributed = 0;
    for (size_t i = 0; i < mask.size(); ++i) {
        if (!mask[i]) {
            EXPECT_LT(owner[i], 0)
                << "row " << i << " was drawn but names a verdict";
            continue;
        }
        ASSERT_GE(owner[i], 0) << "row " << i << " was cut by nobody";
        ASSERT_LT(size_t(owner[i]), nodes.size());
        ++attributed;
    }
    EXPECT_EQ(attributed, countSet(mask));
}

TEST(OcclusionCull, aRowTheFrustumCutIsNotBlamedOnOcclusion)
{
    // The mask is shared with the frustum test, and the two are
    // different bugs. A readout that cannot separate them credits a fix
    // to the wrong mechanism.
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 1);   // the frustum cut everything
    std::vector<int32_t> owner;
    OcclusionTestBatch batch;
    for (int i = 0; i < 24; ++i) {
        c.cull(cam.V, cam.P, cam.height, true, mask, batch, &owner);
        answerAll(c, batch, true);        // and nothing is occluded
    }
    ASSERT_EQ(countSet(mask), 1000u);
    for (size_t i = 0; i < owner.size(); ++i)
        EXPECT_LT(owner[i], 0) << "row " << i << " blamed on occlusion";
}

TEST(OcclusionCull, aWithdrawnVerdictStopsNamingItsRows)
{
    // Unlike the mask, which is additive, an attribution from a
    // previous frame is simply wrong once the verdict is gone.
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    std::vector<int32_t> owner;
    OcclusionTestBatch batch;
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);
    // ⚠️ Answered, like every other frame here. A batch left in flight
    // strands its nodes on `pending`, and a pending hidden node is
    // never offered again — so the enclosure below could not open and
    // the test would fail for a reason that is not the one it is about.
    std::fill(mask.begin(), mask.end(), 0);
    c.cull(cam.V, cam.P, cam.height, true, mask, batch, &owner);
    answerByLevel(c, batch, 1);
    ASSERT_GT(countSet(mask), 0u);

    for (int i = 0; i < 8; ++i) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch, &owner);
        answerAll(c, batch, true);
    }
    std::fill(mask.begin(), mask.end(), 0);
    c.cull(cam.V, cam.P, cam.height, true, mask, batch, &owner);
    ASSERT_EQ(countSet(mask), 0u);
    for (size_t i = 0; i < owner.size(); ++i)
        EXPECT_LT(owner[i], 0) << "row " << i << " still names a dead verdict";
}

TEST(OcclusionCull, theHideCounterCountsEntriesNotConfirmations)
{
    // ⭐ The field that separates an oscillator from a stable wrong
    // verdict. A node re-confirmed hidden every frame must NOT climb —
    // otherwise the counter is a re-test counter wearing the name of a
    // flip counter, and the readout would call the steadiest verdict in
    // the scene the most unstable one.
    OcclusionCuller c;
    c.configure(OcclusionCullConfig());
    c.build(lattice());
    Camera cam;
    std::vector<uint8_t> mask(1000, 0);
    OcclusionTestBatch batch;
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);

    const auto &nodes = c.hierarchy().nodes();
    uint32_t hidden = 0;
    for (size_t n = 0; n < nodes.size(); ++n) {
        if (c.nodeAudit(int(n)).hidden)
            ++hidden;
    }
    ASSERT_GT(hidden, 0u);

    // Twenty more frames of the same answers: confirmations, not events.
    settle(c, cam.V, cam.P, cam.height, mask, batch, 20);
    for (size_t n = 0; n < nodes.size(); ++n) {
        const auto a = c.nodeAudit(int(n));
        if (a.hidden)
            EXPECT_EQ(a.hidEvents, 1) << "node " << n << " counted re-tests";
    }

    // A round trip out of the hidden state and back is one more event.
    for (int i = 0; i < 8; ++i) {
        std::fill(mask.begin(), mask.end(), 0);
        c.cull(cam.V, cam.P, cam.height, true, mask, batch);
        answerAll(c, batch, true);
    }
    settle(c, cam.V, cam.P, cam.height, mask, batch, 24);
    uint32_t twice = 0;
    for (size_t n = 0; n < nodes.size(); ++n) {
        if (c.nodeAudit(int(n)).hidEvents >= 2)
            ++twice;
    }
    EXPECT_GT(twice, 0u) << "a node that left the hidden set and came back "
                            "was never counted as flipping";
}
