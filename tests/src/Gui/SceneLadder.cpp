// Tests for the shared ladder policy (docs/SceneStreaming.md §6/§7):
// the budget estimator, the ranker's size weighting, and the level
// plan — what the whole scene should hold, and the executor's step
// toward it. Like the SceneDump and MeshSimplify tests beside them
// these need no GL context and no document.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "Gui/Renderer/MeshSource.h"
#include "Gui/Renderer/SceneLadder.h"

namespace
{

const size_t kMB = 1024 * 1024;

}  // namespace

TEST(MemoryBudget, slabTimingDoesNotBiasTheExpansion)
{
    // The heap grows in slabs, so between slabs every sample says "the
    // payloads cost nothing" and the slab itself arrives as one spike
    // the clamp truncates. An estimator averaging per-sample slopes is
    // dragged toward its floor between slabs and cannot be pulled back
    // by the capped spike — the *dip* is the failure, so the assertion
    // watches the running estimate through the load, not just where it
    // happens to end. True slope 2, slabs of 48 MB.
    Render::MemoryBudget budget;
    budget.reset(0);
    float lo = 1e9f, hi = 0.0f;
    for (size_t payload = 4 * kMB; payload <= 128 * kMB; payload += 4 * kMB) {
        const size_t heap = (2 * payload / (48 * kMB)) * (48 * kMB);
        budget.observe(payload, 0, heap);
        if (payload >= 64 * kMB) {
            lo = std::min(lo, budget.expansion());
            hi = std::max(hi, budget.expansion());
        }
    }
    EXPECT_GE(lo, 1.3f);
    EXPECT_LE(hi, 3.0f);

    // The smooth control: the same growth with no slabs reads the
    // slope nearly exactly.
    budget.reset(0);
    for (size_t payload = 4 * kMB; payload <= 128 * kMB; payload += 4 * kMB)
        budget.observe(payload, 0, 2 * payload);
    EXPECT_NEAR(budget.expansion(), 2.0f, 0.1f);
}

TEST(MemoryBudget, evictionReanchorsInsteadOfMeasuringAcrossIt)
{
    // Eviction frees payloads without the wasm heap shrinking, so a
    // slope measured across it would read an enormous cost per byte.
    Render::MemoryBudget budget;
    budget.reset(0);
    for (size_t payload = 4 * kMB; payload <= 32 * kMB; payload += 4 * kMB)
        budget.observe(payload, 0, 2 * payload);
    // The ladder runs backwards: payload halves, the heap stays.
    budget.observe(16 * kMB, 0, 64 * kMB);
    // And forwards again at the same true slope.
    for (size_t payload = 20 * kMB; payload <= 48 * kMB; payload += 4 * kMB)
        budget.observe(payload, 0, 64 * kMB + 2 * (payload - 16 * kMB));
    EXPECT_NEAR(budget.expansion(), 2.0f, 0.5f);
}

TEST(MemoryBudget, pinnedBudgetIgnoresEveryObservation)
{
    Render::MemoryBudget budget;
    budget.reset(100 * kMB);
    EXPECT_FALSE(budget.adaptive());
    EXPECT_EQ(budget.value(), 100 * kMB);
    budget.observe(64 * kMB, 0, 512 * kMB);
    budget.observeCeiling(64 * kMB);
    EXPECT_EQ(budget.value(), 100 * kMB);
}

TEST(MemoryBudget, anObservedCeilingOnlyEverLowers)
{
    // observeCeiling is "an allocation just failed with the heap at
    // this size": the wall is at the observation, and a later, higher
    // observation must not raise the ceiling back -- memory that
    // failed once is not un-failed by a later success.
    Render::MemoryBudget budget;
    budget.reset(0);
    const size_t before = budget.value();
    budget.observeCeiling(300 * kMB);
    const size_t lowered = budget.ceiling();
    EXPECT_LT(lowered, 300 * kMB);
    EXPECT_LE(budget.value(), before);
    const size_t cut = budget.value();
    budget.observeCeiling(600 * kMB);
    EXPECT_EQ(budget.ceiling(), lowered);
    EXPECT_LE(budget.value(), cut);
    // But never proposes a budget so small it cannot draw anything.
    budget.observeCeiling(1 * kMB);
    EXPECT_GT(budget.value(), 0u);
}

namespace
{

/// A ranker looking straight down +z at one unit box, so every owned
/// chunk scores identically and only the size weighting differs.
Render::RungRanker makeRanker(const Render::LadderWeights &weights =
                                  Render::LadderWeights())
{
    Render::LadderView view;
    view.eye[2] = -10.0f;
    view.at[2] = 0.0f;
    static const float bbox[6] = {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    return Render::RungRanker(
        view, [](uint64_t) -> const float * { return bbox; }, weights);
}

Render::SceneSnapshot::DeferredChunk chunk(const char *key, uint32_t size,
                                           bool owned)
{
    Render::SceneSnapshot::DeferredChunk c;
    c.key = key;
    c.size = size;
    if (owned)
        c.owners.push_back(1);
    return c;
}

}  // namespace

TEST(RungRanker, sizeWeightAboveOneIsHonoredNotClamped)
{
    // ?fetchweight= is a tuning knob; a knob that silently saturates at
    // 1 lies to the person turning it. At weight 2 a payload a hundred
    // times larger must be ten thousand times discounted, not a
    // hundred.
    Render::RungRanker ranker = makeRanker();
    const auto small = chunk("s", 10, true);
    const auto large = chunk("l", 1000, true);
    const float ratio = ranker.value(small, 2.0f) / ranker.value(large, 2.0f);
    EXPECT_NEAR(ratio, 10000.0f, 1.0f);
}

// ----------------------------------------------------------------------
// The plan (§7, "Selection is a plan, not a reaction")
// ----------------------------------------------------------------------

namespace
{

/// eye at (0,0,10) looking at the origin, fovY 90° (tan of the half
/// angle exactly 1), so an object of radius r at the origin projects
/// to a diameter of r/10 * viewportPx pixels — round numbers on
/// purpose.
Render::LadderView levelView()
{
    Render::LadderView view;
    view.eye[2] = 10.0f;
    view.fovY = 90.0f;
    view.aspect = 1.0f;
    return view;
}

/// A mesh entry with the v36 ladder a big mesh declares: two unbuilt
/// or built middle rungs and the exact mesh, owned by object 1.
Render::SceneSnapshot::DeferredChunk levelEntry(bool level0Built,
                                                bool level1Built)
{
    Render::SceneSnapshot::DeferredChunk entry;
    entry.key = std::string(40, 'e');
    entry.size = 100000;
    entry.owners.push_back(1);
    // Geometry: the one payload kind the plan governs.
    entry.release = []() {};
    Render::SceneSnapshot::DeferredChunk::Level lvl;
    lvl.error = 1.0f / 8.0f;
    if (level0Built) {
        lvl.key = std::string(40, 'a');
        lvl.size = 5000;
    }
    entry.levels.push_back(lvl);
    lvl = {};
    lvl.error = 1.0f / 16.0f;
    if (level1Built) {
        lvl.key = std::string(40, 'b');
        lvl.size = 17000;
    }
    entry.levels.push_back(lvl);
    lvl = {};
    lvl.error = 0.0f;
    lvl.key = entry.key;
    lvl.size = entry.size;
    entry.levels.push_back(lvl);
    return entry;
}

}  // namespace

namespace
{

/// The 40 px object of the old chooseLevel tests: radius 0.4 at
/// distance 10 on a 1000 px viewport. Level 0 errs by 5 px, level 1 by
/// 2.5 px.
const float kFarBox[6] = {-0.231f, -0.231f, -0.231f,
                          0.231f, 0.231f, 0.231f};

Render::PlanParams params(float tolerancePx, size_t budget = 100 * kMB)
{
    Render::PlanParams p;
    p.budgetBytes = budget;
    p.tolerancePx = tolerancePx;
    p.viewportPx = 1000.0f;
    return p;
}

}  // namespace

TEST(PlanLevels, theCoarsestRungTheCameraCannotFaultIsTheTarget)
{
    Render::SceneSnapshot snap;
    snap.deferredChunks.push_back(levelEntry(true, true));
    Render::RungRanker ranker(levelView(),
                              [](uint64_t) -> const float * { return kFarBox; });

    // 2.6 px allowed: level 1 (2.5 px) passes, level 0 (5 px) does not.
    Render::planLevels(snap, ranker, params(2.6f));
    EXPECT_EQ(snap.deferredChunks[0].plan, 1);

    // 6 px allowed: even the coarsest rung is indistinguishable.
    Render::planLevels(snap, ranker, params(6.0f));
    EXPECT_EQ(snap.deferredChunks[0].plan, 0);

    // 1 px allowed: only the exact mesh qualifies. So does a
    // non-positive tolerance (the off switch) and an owner with
    // unknown bounds — the answer that is right whatever the camera
    // turns out to see.
    Render::planLevels(snap, ranker, params(1.0f));
    EXPECT_EQ(snap.deferredChunks[0].plan, 2);
    Render::planLevels(snap, ranker, params(0.0f));
    EXPECT_EQ(snap.deferredChunks[0].plan, 2);
    Render::RungRanker noBounds(
        levelView(), [](uint64_t) -> const float * { return nullptr; });
    Render::planLevels(snap, noBounds, params(2.6f));
    EXPECT_EQ(snap.deferredChunks[0].plan, 2);
}

TEST(PlanLevels, theBudgetHoldsAnObjectBelowItsDesire)
{
    Render::SceneSnapshot snap;
    snap.deferredChunks.push_back(levelEntry(true, true));
    Render::RungRanker ranker(levelView(),
                              [](uint64_t) -> const float * { return kFarBox; });

    // Wants exact (100000 B), the budget affords level 0 (5000 B) but
    // not level 1 (17000 B): the plan stops where the money does, and
    // says so.
    const auto stats = Render::planLevels(snap, ranker, params(1.0f, 6000));
    EXPECT_EQ(snap.deferredChunks[0].plan, 0);
    EXPECT_EQ(stats.objects, 1u);
    EXPECT_EQ(stats.capped, 1u);
    EXPECT_EQ(stats.plannedBytes, 5000u);
}

TEST(PlanLevels, evictionDemotesToCoarseNotToNothing)
{
    // A near object's appetite for exact must not price an off-screen
    // neighbor off the scene entirely. Off-screen is where the camera
    // pans next: stripped to its box it re-enters the view as a grey
    // box and waits out a round trip; on its coarsest rung it
    // re-enters as itself. The floor grants every object its cheapest
    // tier before anyone gets finer.
    Render::SceneSnapshot snap;
    auto big = levelEntry(true, true);
    auto aside = levelEntry(true, true);
    aside.key = std::string(40, 'q');
    aside.levels[2].key = aside.key;
    aside.owners[0] = 2;
    snap.deferredChunks.push_back(std::move(big));
    snap.deferredChunks.push_back(std::move(aside));
    static const float offBox[6] = {49.5f, -0.4f, -0.4f,
                                    50.5f, 0.4f, 0.4f};
    Render::RungRanker ranker(
        levelView(), [](uint64_t key) -> const float * {
            return key == 1 ? kFarBox : offBox;
        });
    // Exactly the on-screen object's whole ladder: without the floor
    // the greedy spends it all there and the off-screen object stays
    // a box.
    Render::planLevels(snap, ranker, params(0.5f, 100000));
    EXPECT_GE(snap.deferredChunks[1].plan, 0)
        << "the off-screen object must hold its coarsest rung";
    EXPECT_EQ(snap.deferredChunks[0].plan, 1)
        << "the near object pays for the floor out of its finest tier";
}

TEST(PlanLevels, theWorstErrorOutbidsACoalitionOfCheapUpgrades)
{
    // One huge foreground object whose exact mesh is expensive, four
    // mid objects with cheap exacts. Plain gain per byte minimizes the
    // error TOTAL: the mids' upgrades are better value per byte, they
    // spend the budget, and the 693 px object — the one committing a
    // 87 px error, the worst on screen — stays coarse. Measured on a
    // phone as a 1300 px sphere held coarse at 8 MB of 8 while the
    // mid-field polished. Weighting gain by the standing error makes
    // the plan spend toward the smallest WORST error instead.
    const auto makeEntry = [](char tag, uint64_t owner, uint32_t coarse,
                              uint32_t exact) {
        Render::SceneSnapshot::DeferredChunk entry;
        entry.key = std::string(40, tag);
        entry.size = exact;
        entry.owners.push_back(owner);
        entry.release = []() {};
        Render::SceneSnapshot::DeferredChunk::Level lvl;
        lvl.error = 1.0f / 8.0f;
        lvl.key = std::string(40, char(tag - ('a' - 'A')));
        lvl.size = coarse;
        entry.levels.push_back(lvl);
        lvl = {};
        lvl.error = 0.0f;
        lvl.key = entry.key;
        lvl.size = exact;
        entry.levels.push_back(lvl);
        return entry;
    };
    Render::SceneSnapshot snap;
    snap.deferredChunks.push_back(makeEntry('h', 1, 5000, 2000000));
    for (int i = 0; i < 4; ++i)
        snap.deferredChunks.push_back(
            makeEntry(char('m' + i), uint64_t(2 + i), 1000, 50000));
    const float hugeBox[6] = {-4.0f, -4.0f, -4.0f, 4.0f, 4.0f, 4.0f};
    Render::RungRanker ranker(
        levelView(), [&](uint64_t key) -> const float * {
            return key == 1 ? hugeBox : kFarBox;
        });
    // Floor (9 KB) + the huge exact (2 MB) fit; nothing else does.
    const auto stats = Render::planLevels(snap, ranker, params(1.0f, 2010000));
    EXPECT_EQ(snap.deferredChunks[0].plan, 1)
        << "the object with the largest standing error upgrades first";
    for (size_t i = 1; i < snap.deferredChunks.size(); ++i)
        EXPECT_EQ(snap.deferredChunks[i].plan, 0) << i;
    EXPECT_EQ(stats.capped, 4u);
}

TEST(PlanLevels, anObjectsChunksLandOnTheSameRung)
{
    // The face set and the edge set of one object are separate chunks
    // with separate ladders; independent choices put exact polylines
    // on a coarse surface. The plan assigns the object one error, so
    // the rungs agree — under a generous budget and under one that
    // caps the pair alike.
    const auto makeSnap = [] {
        Render::SceneSnapshot snap;
        snap.deferredChunks.push_back(levelEntry(true, true));
        auto edges = levelEntry(true, true);
        edges.key = std::string(40, 'f');
        edges.size = 2000;
        edges.levels[0].key = std::string(40, 'c');
        edges.levels[0].size = 50;
        edges.levels[1].key = std::string(40, 'd');
        edges.levels[1].size = 170;
        edges.levels[2].key = edges.key;
        edges.levels[2].size = edges.size;
        snap.deferredChunks.push_back(std::move(edges));
        return snap;
    };
    Render::RungRanker ranker(levelView(),
                              [](uint64_t) -> const float * { return kFarBox; });

    Render::SceneSnapshot roomy = makeSnap();
    Render::planLevels(roomy, ranker, params(1.0f));
    EXPECT_EQ(roomy.deferredChunks[0].plan, 2);
    EXPECT_EQ(roomy.deferredChunks[0].plan, roomy.deferredChunks[1].plan);

    Render::SceneSnapshot tight = makeSnap();
    Render::planLevels(tight, ranker, params(1.0f, 6000));
    EXPECT_EQ(tight.deferredChunks[0].plan, 0);
    EXPECT_EQ(tight.deferredChunks[0].plan, tight.deferredChunks[1].plan);
}

TEST(PlanLevels, aSharedChunkIsPlannedOnceAtTheFinestNeed)
{
    // One mesh, two owners: the far one would settle for level 1, the
    // near one needs exact. The finest need wins, and the bytes are
    // counted once.
    const float nearBox[6] = {-4.0f, -4.0f, 4.0f, 4.0f, 4.0f, 6.0f};
    Render::SceneSnapshot snap;
    snap.deferredChunks.push_back(levelEntry(true, true));
    snap.deferredChunks[0].owners.push_back(2);
    Render::RungRanker ranker(
        levelView(), [&](uint64_t key) -> const float * {
            return key == 1 ? kFarBox : nearBox;
        });
    const auto stats = Render::planLevels(snap, ranker, params(2.6f));
    EXPECT_EQ(snap.deferredChunks[0].plan, 2)
        << "a mesh shared with a near object must be fine enough for it";
    EXPECT_EQ(stats.plannedBytes, 100000u);
}

TEST(PlanLevels, theViewsOwnAlwaysTargetTheirFinestBuiltRung)
{
    // Ownerless geometry is the view's own (navigation cube, axis
    // cross): outside the budget entirely, because refusing it is a
    // verdict nothing can appeal. A budget of one byte must not touch
    // it.
    Render::SceneSnapshot snap;
    snap.deferredChunks.push_back(levelEntry(true, true));
    snap.deferredChunks[0].owners.clear();
    Render::RungRanker ranker(levelView(),
                              [](uint64_t) -> const float * { return kFarBox; });
    Render::planLevels(snap, ranker, params(2.6f, 1));
    EXPECT_EQ(snap.deferredChunks[0].plan, 2);
}

TEST(PlanLevels, aDriftingCameraDoesNotFlipTheBudgetBoundary)
{
    // Two near-equal objects, budget for one exact mesh. The camera
    // settling off a touch fling drifts by epsilons, and each replan
    // reorders the raw scores by a fraction — without incumbency the
    // knapsack boundary flipped between them forever, refetching a
    // 155 KB mesh and its coarse rung alternately on a stationary
    // phone. The previous plan's grant must outlast a marginal
    // outbid.
    Render::SceneSnapshot snap;
    for (int i = 0; i < 2; ++i) {
        auto entry = levelEntry(true, true);
        entry.key = std::string(40, char('p' + i));
        entry.levels[2].key = entry.key;
        entry.owners[0] = uint64_t(i + 1);
        snap.deferredChunks.push_back(std::move(entry));
    }
    // Coarse rungs for both fit; only ONE exact does.
    const auto budget = params(1.0f, 5000 + 5000 + 17000 + 17000 + 110000);

    static float boxA[6];
    static float boxB[6];
    const auto setBoxes = [&](float b) {
        for (int i = 0; i < 3; ++i) {
            boxA[i] = -0.231f;
            boxA[3 + i] = 0.231f;
            boxB[i] = -0.231f * b;
            boxB[3 + i] = 0.231f * b;
        }
    };
    const auto bounds = [](uint64_t key) -> const float * {
        return key == 1 ? boxA : boxB;
    };

    // Round 1: A slightly bigger on screen — A gets the exact mesh.
    setBoxes(0.98f);
    Render::RungRanker r1(levelView(), bounds);
    Render::planLevels(snap, r1, budget);
    EXPECT_EQ(snap.deferredChunks[0].plan, 2);
    EXPECT_EQ(snap.deferredChunks[1].plan, 1);

    // Round 2: the drift makes B marginally bigger. A keeps its grant.
    setBoxes(1.02f);
    Render::RungRanker r2(levelView(), bounds);
    Render::planLevels(snap, r2, budget);
    EXPECT_EQ(snap.deferredChunks[0].plan, 2)
        << "a marginal outbid must not displace the incumbent";
    EXPECT_EQ(snap.deferredChunks[1].plan, 1);

    // A genuinely different camera still wins: B four times the size.
    setBoxes(4.0f);
    Render::RungRanker r3(levelView(), bounds);
    Render::planLevels(snap, r3, budget);
    EXPECT_EQ(snap.deferredChunks[1].plan, 2)
        << "real change must override incumbency";
}

TEST(PlanLevels, aDriftingCameraDoesNotFlipTheRungThreshold)
{
    // The tier cut is a threshold on tolerancePx over a projected
    // diameter, and an object sitting at a rung boundary crosses it
    // with every camera epsilon — measured as one object cycling
    // ask-rung/release-rung seven times in a minute of orbiting, and
    // 2695 asks against 2716 releases across the scene. A granted
    // finer tier must survive a marginal drift (the objectErr map
    // passed back in carries the previous grants) and still yield to a
    // camera that genuinely pulled back.
    Render::SceneSnapshot snap;
    snap.deferredChunks.push_back(levelEntry(true, true));
    Render::RungRanker ranker(levelView(),
                              [](uint64_t) -> const float * { return kFarBox; });
    std::map<uint64_t, float> grants;
    auto p = params(2.4f);
    p.objectErr = &grants;

    // 2.4 px allowed: level 1 (2.5 px) misses the tolerance, the plan
    // wants exact — the grant the drift will lean on.
    Render::planLevels(snap, ranker, p);
    EXPECT_EQ(snap.deferredChunks[0].plan, 2);
    ASSERT_EQ(grants.count(1), 1u);
    EXPECT_EQ(grants[1], 0.0f);

    // Drift: 2.6 px now clears level 1, a fresh cut would step the
    // object down — but 2.6 px is within 25% of the 2.5 px boundary,
    // so the exact grant holds. This is the commonest flip in the
    // journal: exact against the first coarse tier.
    p.tolerancePx = 2.6f;
    Render::planLevels(snap, ranker, p);
    EXPECT_EQ(snap.deferredChunks[0].plan, 2)
        << "a boundary epsilon must not walk the object down a rung";
    EXPECT_EQ(grants[1], 0.0f) << "the held grant re-records itself";

    // 3.2 px: past the margin (2.5 × 1.25 = 3.125). The camera really
    // pulled back, and the object steps down to level 1.
    p.tolerancePx = 3.2f;
    Render::planLevels(snap, ranker, p);
    EXPECT_EQ(snap.deferredChunks[0].plan, 1)
        << "real change must still downgrade";
    EXPECT_EQ(grants[1], 1.0f / 16.0f);

    // The same margin guards the next boundary too: 5.5 px would
    // freshly cut at level 0 (5 px) but holds level 1; 6.5 px is past
    // 5 × 1.25 and lets it down.
    p.tolerancePx = 5.5f;
    Render::planLevels(snap, ranker, p);
    EXPECT_EQ(snap.deferredChunks[0].plan, 1)
        << "a drift within the margin keeps the finer rung";
    p.tolerancePx = 6.5f;
    Render::planLevels(snap, ranker, p);
    EXPECT_EQ(snap.deferredChunks[0].plan, 0);
}

TEST(PlanLevels, theSameInputsProduceTheSamePlan)
{
    // Determinism is what the plan has instead of damping: a plan that
    // cannot differ from itself cannot oscillate with itself.
    const auto build = [] {
        Render::SceneSnapshot snap;
        for (int i = 0; i < 8; ++i) {
            auto entry = levelEntry(true, true);
            entry.key = std::string(40, char('g' + i));
            entry.levels[2].key = entry.key;
            entry.owners[0] = uint64_t(i + 1);
            snap.deferredChunks.push_back(std::move(entry));
        }
        return snap;
    };
    Render::RungRanker ranker(levelView(),
                              [](uint64_t) -> const float * { return kFarBox; });
    Render::SceneSnapshot a = build();
    Render::SceneSnapshot b = build();
    // A budget that fits only some of the desires forces the greedy
    // order to decide, which is where nondeterminism would live.
    Render::planLevels(a, ranker, params(1.0f, 40000));
    Render::planLevels(b, ranker, params(1.0f, 40000));
    for (size_t i = 0; i < a.deferredChunks.size(); ++i)
        EXPECT_EQ(a.deferredChunks[i].plan, b.deferredChunks[i].plan) << i;
}

// ----------------------------------------------------------------------
// The executor's step (§7)
// ----------------------------------------------------------------------

TEST(PlanStep, coarseFirstWhenNothingIsResident)
{
    // Exact needed with nothing on screen: fetch the coarsest built
    // rung first — kilobytes now, the target lands as an upgrade over
    // it instead of over a box. With the coarse rung up, the needed
    // rung is fetched directly; with every needed rung held, the
    // stand-in goes back and the ladder is silent.
    auto entry = levelEntry(true, true);
    const uint16_t needed = 1u << 2;
    entry.residentMask = 0;
    auto step = Render::planStep(entry, needed);
    EXPECT_EQ(step.fetch, 0);
    EXPECT_EQ(step.generate, -1);
    EXPECT_EQ(step.surplus, 0);
    entry.residentMask = 1u << 0;
    step = Render::planStep(entry, needed);
    EXPECT_EQ(step.fetch, 2);
    EXPECT_EQ(step.surplus, 0);  // the stand-in stays until then
    entry.residentMask = (1u << 0) | (1u << 2);
    step = Render::planStep(entry, needed);
    EXPECT_EQ(step.fetch, -1);
    EXPECT_EQ(step.surplus, 1u << 0);
    entry.residentMask = 1u << 2;
    step = Render::planStep(entry, needed);
    EXPECT_EQ(step.fetch, -1);
    EXPECT_EQ(step.generate, -1);
    EXPECT_EQ(step.surplus, 0);
}

TEST(PlanStep, anUnbuiltNeededRungGeneratesAndStandsOnTheNearestBuilt)
{
    // Level 1 needed but not built: ask the producer. Standing on
    // level 0 already, there is nothing worth fetching meanwhile — and
    // nothing is released either, because the needed rung cannot be
    // resident yet; with nothing resident, the built coarser rung goes
    // up first.
    auto entry = levelEntry(true, false);
    const uint16_t needed = 1u << 1;
    entry.residentMask = 1u << 0;
    auto step = Render::planStep(entry, needed);
    EXPECT_EQ(step.generate, 1);
    EXPECT_EQ(step.fetch, -1);
    EXPECT_EQ(step.surplus, 0);
    entry.residentMask = 0;
    step = Render::planStep(entry, needed);
    EXPECT_EQ(step.generate, 1);
    EXPECT_EQ(step.fetch, 0);
    // Nothing built on the coarse side at all: the exact mesh is the
    // only rung there is.
    entry = levelEntry(false, false);
    entry.residentMask = 0;
    step = Render::planStep(entry, needed);
    EXPECT_EQ(step.generate, 1);
    EXPECT_EQ(step.fetch, 2);
}

TEST(PlanStep, nothingNeededMakesEverythingSurplus)
{
    // Every owner on its box: everything held is surplus — reported
    // for the budget's eviction pass, never shed by the step itself;
    // holding nothing, nothing to do.
    auto entry = levelEntry(true, true);
    entry.residentMask = (1u << 0) | (1u << 1);
    auto step = Render::planStep(entry, 0);
    EXPECT_EQ(step.surplus, entry.residentMask);
    EXPECT_EQ(step.fetch, -1);
    entry.residentMask = 0;
    step = Render::planStep(entry, 0);
    EXPECT_EQ(step.surplus, 0);
    EXPECT_EQ(step.fetch, -1);
}

TEST(PlanStep, twoOwnersHoldTwoRungsAtOnce)
{
    // The whiskers fix (§7, "one rung per instance"): a far owner
    // needs the coarse rung and a near owner the exact one — BOTH stay
    // resident, and only a rung nobody needs is surplus.
    auto entry = levelEntry(true, true);
    const uint16_t needed = (1u << 0) | (1u << 2);
    entry.residentMask = 1u << 0;
    auto step = Render::planStep(entry, needed);
    EXPECT_EQ(step.fetch, 2);
    EXPECT_EQ(step.surplus, 0);
    entry.residentMask = (1u << 0) | (1u << 2);
    step = Render::planStep(entry, needed);
    EXPECT_EQ(step.fetch, -1);
    EXPECT_EQ(step.surplus, 0);  // both needed: nothing is surplus
    entry.residentMask = (1u << 0) | (1u << 1) | (1u << 2);
    step = Render::planStep(entry, needed);
    EXPECT_EQ(step.surplus, 1u << 1);
}

TEST(PlanStep, planNeededUnionsTheOwnersRungs)
{
    // Two owners at different granted errors need different rungs; a
    // boxed owner contributes nothing; an owner the plan has not seen
    // defaults to the finest built rung; the view's own (ownerless)
    // geometry always needs its finest built rung.
    auto entry = levelEntry(true, true);
    entry.owners = {1, 2};
    auto errOf = [](uint64_t owner) -> float {
        return owner == 1 ? 0.5f : 0.0f;  // coarse enough for rung 0 / exact
    };
    EXPECT_EQ(Render::planNeeded(entry, errOf),
              uint16_t((1u << 0) | (1u << 2)));
    auto boxed = [](uint64_t owner) -> float {
        return owner == 1 ? 0.5f : -1.0f;
    };
    EXPECT_EQ(Render::planNeeded(entry, boxed), uint16_t(1u << 0));
    auto unseen = [](uint64_t) -> float {
        return std::numeric_limits<float>::quiet_NaN();
    };
    EXPECT_EQ(Render::planNeeded(entry, unseen), uint16_t(1u << 2));
    entry.owners.clear();
    EXPECT_EQ(Render::planNeeded(entry, errOf), uint16_t(1u << 2));
    // And an entry with no ladder at all is a one-rung ladder.
    Render::SceneSnapshot::DeferredChunk bare;
    bare.key = std::string(40, 'x');
    bare.size = 500;
    bare.owners.push_back(1);
    bare.release = []() {};
    EXPECT_EQ(Render::planNeeded(bare, unseen), uint16_t(1u << 0));
    auto step = Render::planStep(bare, Render::planNeeded(bare, unseen));
    EXPECT_EQ(step.fetch, 0);
    bare.residentMask = 1u << 0;
    step = Render::planStep(bare, Render::planNeeded(bare, unseen));
    EXPECT_EQ(step.fetch, -1);
}

//////////////////////////////////////////////////////////////////////
// The desktop plan pass (§13 step 2): which coarse-first sources err
// more than the tolerance on the screen.

namespace
{

/// \a verts gives the mesh real geometry, so it prices at `verts * 12`
/// bytes (positions only). The refine and free-tier tests do not care
/// what a source frees and leave it 0; the priced tier is the whole
/// question of how much a demotion gives back.
Render::DrawCall meshDraw(const void *tag, float levelError,
                          float cx, float cy, float cz, float half,
                          int verts = 0)
{
    auto mesh = std::make_shared<Render::MeshData>();
    mesh->sourceTag = tag;
    mesh->levelError = levelError;
    mesh->numVertices = verts;
    Render::DrawCall d;
    d.mesh = mesh;
    d.bboxMin[0] = cx - half; d.bboxMin[1] = cy - half;
    d.bboxMin[2] = cz - half;
    d.bboxMax[0] = cx + half; d.bboxMax[1] = cy + half;
    d.bboxMax[2] = cz + half;
    return d;
}

/// Identity view (eye at the origin looking down -z) and a 90° fovY
/// perspective, so p11 = 1 and the arithmetic stays checkable by hand.
struct PlanCamera {
    float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float proj[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                      0, 0, -1.0002f, -1, 0, 0, -0.20002f, 0};
};

}  // namespace

TEST(PlanMeshRefines, nearRefinesWhileFarStaysCoarse)
{
    // Same object, same coarse error, two distances. Half 5 → diagonal
    // 17.32; at depth ~91 that is ~95 px on a 1000 px viewport, so a
    // 3% coarse error is ~2.8 px — over a 2 px tolerance. At ten times
    // the distance it is ~0.26 px — far under it.
    PlanCamera cam;
    int nearTag = 0, farTag = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&nearTag, 0.03f, 0, 0, -100, 5));
    draws.push_back(meshDraw(&farTag, 0.03f, 0, 0, -1000, 5));
    auto tags = Render::planMeshRefines(draws, cam.view, cam.proj,
                                        1000.0f, 2.0f);
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], &nearTag);
}

TEST(PlanMeshRefines, theErrorIsBoundedByTheScreenBeforeItIsCompared)
{
    // The two passes must speak ONE currency. Under pressure this
    // pass runs at the error the descent had to accept, and that raise
    // is bounded by the viewport height -- so the comparison here is
    // bounded by the same thing, or the ladder asks straight back for
    // what it just gave up.
    //
    // A near object with a huge coarse error: unbounded it projects
    // thousands of pixels, so ANY tolerance the raise can reach would
    // still refine it. Bounded, the screen height is the most it can
    // claim, and a tolerance standing at the screen height refuses it.
    PlanCamera cam;
    int huge = 0;
    Render::DrawCallList draws;
    // Depth 10, half 5: the box projects ~915px of diagonal, so a
    // 0.9 coarse error is ~823px -- unbounded it would clear a
    // 600px tolerance, bounded it cannot.
    draws.push_back(meshDraw(&huge, 0.9f, 0, 0, -10, 5));
    EXPECT_TRUE(Render::planMeshRefines(draws, cam.view, cam.proj, 600.0f,
                                        600.0f)
                    .empty());
    // ...and it comes back the moment the release walks the tolerance
    // down, which is what the staircase is for.
    EXPECT_EQ(Render::planMeshRefines(draws, cam.view, cam.proj, 600.0f,
                                      300.0f)
                  .size(),
              1u);
}

TEST(PlanMeshRefines, offscreenAndBehindNeverRefine)
{
    // A source erring badly but out of the frustum is exactly the
    // residency bill: refining it would tessellate what nobody sees.
    // The camera that turns toward it is a new settle, a new plan.
    PlanCamera cam;
    int aside = 0, behind = 0, inside = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&aside, 0.03f, 500, 0, -100, 5));
    draws.push_back(meshDraw(&behind, 0.03f, 0, 0, 100, 5));
    // The camera sits inside this box's span: as big as it gets.
    draws.push_back(meshDraw(&inside, 0.03f, 0, 0, 0, 5));
    auto tags = Render::planMeshRefines(draws, cam.view, cam.proj,
                                        1000.0f, 2.0f);
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], &inside);
}

TEST(PlanMeshRefines, nonPositiveToleranceRefinesEveryCoarseSource)
{
    // "Every object desires its exact content" — the step-1 reading,
    // kept reachable: no camera math, no frustum, just every source
    // that is not exact yet.
    PlanCamera cam;
    int near = 0, offscreen = 0, exact = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&near, 0.03f, 0, 0, -100, 5));
    draws.push_back(meshDraw(&offscreen, 0.03f, 500, 0, -100, 5));
    draws.push_back(meshDraw(&exact, 0.0f, 0, 0, -100, 5));
    auto tags = Render::planMeshRefines(draws, cam.view, cam.proj,
                                        1000.0f, 0.0f);
    ASSERT_EQ(tags.size(), 2u);
    EXPECT_NE(std::find(tags.begin(), tags.end(), &near), tags.end());
    EXPECT_NE(std::find(tags.begin(), tags.end(), &offscreen), tags.end());
}

TEST(PlanMeshRefines, aTagAppearsOnceHoweverManyDrawsShareIt)
{
    // Instanced draws share the proto's source tag; the plan names the
    // job once (and requestRefine is consume-once besides).
    PlanCamera cam;
    int tag = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&tag, 0.03f, 0, 0, -100, 5));
    draws.push_back(meshDraw(&tag, 0.03f, 20, 0, -100, 5));
    auto tags = Render::planMeshRefines(draws, cam.view, cam.proj,
                                        1000.0f, 2.0f);
    EXPECT_EQ(tags.size(), 1u);
}

TEST(PlanMeshRefines, orthographicErrorIsSizeNotDepth)
{
    // Under an orthographic camera distance changes nothing: the same
    // box errs the same at any depth, and refines at both.
    float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // glOrtho with a 200-unit view height and [1, 2000] depth range.
    float proj[16] = {0.01f, 0, 0, 0, 0, 0.01f, 0, 0,
                      0, 0, -0.0010005f, 0, 0, 0, -1.0010005f, 1};
    int near = 0, far = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&near, 0.03f, 0, 0, -100, 5));
    draws.push_back(meshDraw(&far, 0.03f, 0, 0, -1000, 5));
    auto tags = Render::planMeshRefines(draws, view, proj, 1000.0f, 2.0f);
    EXPECT_EQ(tags.size(), 2u);
}

//////////////////////////////////////////////////////////////////////
// The way back down (§13 step 3): which exact-resident sources an
// observed memory ceiling may drop back to their coarse rung.

namespace
{

/// Answers the coarse-rung error for every tag in the map, 0 for the
/// rest — the registry's demoteError in miniature.
std::function<float(const void *)> demoteErrs(
    const std::map<const void *, float> &errs)
{
    return [&errs](const void *tag) {
        auto it = errs.find(tag);
        return it == errs.end() ? 0.0f : it->second;
    };
}

}  // namespace

TEST(PlanMeshDemotes, offscreenDropsAndVisibleHoldsByTheMargin)
{
    // Three refined sources (levelError 0): one off screen — free to
    // drop; one whose coarse rung errs ~0.26 px, far under half the
    // 2 px tolerance — droppable; one erring ~2.8 px — must keep its
    // exact mesh.
    PlanCamera cam;
    int offscreen = 0, cheap = 0, needed = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&offscreen, 0.0f, 500, 0, -100, 5));
    draws.push_back(meshDraw(&cheap, 0.0f, 0, 0, -1000, 5));
    draws.push_back(meshDraw(&needed, 0.0f, 0, 0, -100, 5));
    std::map<const void *, float> errs{
        {&offscreen, 0.03f}, {&cheap, 0.03f}, {&needed, 0.03f}};
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj,
                                        1000.0f, 2.0f, demoteErrs(errs));
    ASSERT_EQ(tags.size(), 2u);
    EXPECT_NE(std::find(tags.begin(), tags.end(), &offscreen),
              tags.end());
    EXPECT_NE(std::find(tags.begin(), tags.end(), &cheap), tags.end());
}

TEST(PlanMeshDemotes, theMarginKeepsTheRefineBoundaryApart)
{
    // A source the refine pass would NOT want (coarse err just under
    // the tolerance) still must not demote: at ~1.4 px against a 2 px
    // tolerance it is under the refine boundary but over the demote
    // margin (1 px), the hysteresis band a drifting camera sits in
    // without trading tessellations.
    PlanCamera cam;
    int boundary = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&boundary, 0.0f, 0, 0, -200, 5));
    std::map<const void *, float> errs{{&boundary, 0.03f}};
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj,
                                        1000.0f, 2.0f, demoteErrs(errs));
    EXPECT_TRUE(tags.empty());
    // The refine pass agrees it is fine where it is.
    auto refines = Render::planMeshRefines(draws, cam.view, cam.proj,
                                           1000.0f, 2.0f);
    EXPECT_TRUE(refines.empty());
}

TEST(PlanMeshDemotes, anOccludedSourceIsFreeAndRaisesNoTolerance)
{
    // The same boundary source the margin test holds on screen -- too
    // wrong to drop for free, and no deficit stands to price it -- is
    // dropped outright once the caller's occlusion verdict says no
    // pixel of it reaches the screen. And the error it would have
    // committed must NOT enter acceptedErrorPx: nobody can see it, so
    // it must not raise the tolerance the climb runs at.
    PlanCamera cam;
    int boundary = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&boundary, 0.0f, 0, 0, -200, 5));
    std::map<const void *, float> errs{{&boundary, 0.03f}};
    Render::PlanDemoteStats stats;
    auto tags = Render::planMeshDemotes(
        draws, cam.view, cam.proj, 1000.0f, 2.0f, demoteErrs(errs),
        &stats, 0, {},
        [&boundary](const void *tag) { return tag == &boundary; });
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], static_cast<const void *>(&boundary));
    EXPECT_EQ(stats.occludedFree, 1u);
    EXPECT_EQ(stats.eligible, 0u);
    EXPECT_EQ(stats.acceptedErrorPx, 0.0f);
}

TEST(PlanMeshDemotes, occludedMemoryCoversTheDeficitBeforeVisibleError)
{
    // Two sources over the margin, one occluded and one plainly
    // visible, and a deficit the occluded one's bytes already cover:
    // the visible source must not be traded -- the whole point of the
    // feed is buying the memory back with error nobody can see first.
    PlanCamera cam;
    int hidden = 0, shown = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&hidden, 0.0f, 0, 0, -200, 5, 100));
    draws.push_back(meshDraw(&shown, 0.0f, 20, 0, -200, 5, 100));
    std::map<const void *, float> errs{{&hidden, 0.03f},
                                       {&shown, 0.03f}};
    Render::PlanDemoteStats stats;
    auto tags = Render::planMeshDemotes(
        draws, cam.view, cam.proj, 1000.0f, 2.0f, demoteErrs(errs),
        &stats, /*deficit*/ 1, {},
        [&hidden](const void *tag) { return tag == &hidden; });
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], static_cast<const void *>(&hidden));
    EXPECT_EQ(stats.occludedFree, 1u);
    EXPECT_EQ(stats.tooBig, 1u);
    EXPECT_EQ(stats.underPressure, 0u);
}

TEST(PlanMeshDemotes, whatCanDescendIsTheRegistrysAnswer)
{
    // A source with no way down (error 0) is out however plainly it is
    // off screen; a non-positive tolerance demotes nothing at all.
    //
    // A source already displaying a COARSE rung used to be out too, on
    // the reading that it had nothing left to give. Not so under a
    // budget the coarse scene cannot meet: it descends by
    // re-tessellating coarser again (sec 13, dynamic scale), and the
    // registry's error -- what that next step would show -- is the
    // only thing that decides. So the pass asks about every drawn
    // source and lets the answer sort them out.
    PlanCamera cam;
    int coarse = 0, noFallback = 0, cheap = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&coarse, 0.03f, 500, 0, -100, 5));
    draws.push_back(meshDraw(&noFallback, 0.0f, 500, 0, -100, 5));
    draws.push_back(meshDraw(&cheap, 0.0f, 0, 0, -1000, 5));
    std::map<const void *, float> errs{
        {&coarse, 0.03f}, {&cheap, 0.03f}};
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj,
                                        1000.0f, 2.0f, demoteErrs(errs));
    ASSERT_EQ(tags.size(), 2u);
    EXPECT_NE(std::find(tags.begin(), tags.end(), &coarse), tags.end());
    EXPECT_NE(std::find(tags.begin(), tags.end(), &cheap), tags.end());
    EXPECT_EQ(std::find(tags.begin(), tags.end(), &noFallback), tags.end());
    EXPECT_TRUE(Render::planMeshDemotes(draws, cam.view, cam.proj,
                                        1000.0f, 0.0f, demoteErrs(errs))
                    .empty());
}

namespace
{

Render::DrawCall keyedDraw(const void *tag, float levelError,
                           float cx, float cy, float cz, float half,
                           uint64_t objectKey)
{
    Render::DrawCall d = meshDraw(tag, levelError, cx, cy, cz, half);
    d.objectKey = objectKey;
    return d;
}

}  // namespace

TEST(PlanMeshDemotes, aSeamEdgeRidesItsObjectsBounds)
{
    // The churn this guards against: an object's edge draw is a
    // sliver (an ellipsoid's seam is one meridian) that can sit
    // off-screen while the body fills the view. Judged by its own
    // box the edge tag dropped, the shared closure downgraded the
    // whole object, the next plan re-refined it through the face tag
    // — a full rebuild each way, forever. One union box per object
    // gives both tags the face's verdict: keep.
    PlanCamera cam;
    int faceTag = 0, lineTag = 0;
    Render::DrawCallList draws;
    draws.push_back(keyedDraw(&faceTag, 0.0f, 0, 0, -100, 5, 42));
    draws.push_back(keyedDraw(&lineTag, 0.0f, 500, 0, -100, 0.5f, 42));
    std::map<const void *, float> errs{
        {&faceTag, 0.03f}, {&lineTag, 0.03f}};
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj,
                                        1000.0f, 2.0f, demoteErrs(errs));
    EXPECT_TRUE(tags.empty());
    // And the refine mirror: the same sliver, coarse, is wanted with
    // its object even though its own box is off-screen.
    Render::DrawCallList coarse;
    coarse.push_back(keyedDraw(&faceTag, 0.03f, 0, 0, -100, 5, 42));
    coarse.push_back(keyedDraw(&lineTag, 0.03f, 500, 0, -100, 0.5f, 42));
    auto refines = Render::planMeshRefines(coarse, cam.view, cam.proj,
                                           1000.0f, 2.0f);
    ASSERT_EQ(refines.size(), 2u);
}

TEST(PlanMeshDemotes, aSharedSourceStaysExactForItsNeediestOwner)
{
    // Two instances of one proto (same tag, different objects): the
    // near one still needs exact, so the far one's vote must not drop
    // the shared source — demotion is the intersection of its owners.
    PlanCamera cam;
    int proto = 0;
    Render::DrawCallList draws;
    draws.push_back(keyedDraw(&proto, 0.0f, 0, 0, -100, 5, 1));
    draws.push_back(keyedDraw(&proto, 0.0f, 0, 0, -1000, 5, 2));
    std::map<const void *, float> errs{{&proto, 0.03f}};
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj,
                                        1000.0f, 2.0f, demoteErrs(errs));
    EXPECT_TRUE(tags.empty());
    // Both far: now every owner agrees, and the source drops.
    Render::DrawCallList far;
    far.push_back(keyedDraw(&proto, 0.0f, 0, 0, -1000, 5, 1));
    far.push_back(keyedDraw(&proto, 0.0f, 20, 0, -1000, 5, 2));
    auto tags2 = Render::planMeshDemotes(far, cam.view, cam.proj,
                                         1000.0f, 2.0f, demoteErrs(errs));
    ASSERT_EQ(tags2.size(), 1u);
}

//////////////////////////////////////////////////////////////////////
// The priced tier: what a deficit may buy that the margin refused.

namespace
{

/// On axis at depth \a cz, with geometry behind it (see meshDraw).
Render::DrawCall sizedDraw(const void *tag, float levelError,
                           float cz, float half, int verts)
{
    return meshDraw(tag, levelError, 0, 0, cz, half, verts);
}

const uint64_t kVertBytes = 12;  ///< xyz float positions

}  // namespace

TEST(PlanMeshDemotes, anUnownedTagIsNotAMissingRung)
{
    // The registry answers kTagUnknown for a tag it never had, and 0
    // for one that armed no descent. Both refuse the demotion; only
    // one of them is a source that could be given a rung, so a plan
    // that reports them as one number sends the reader after the
    // wrong defect -- which is exactly what happened.
    PlanCamera cam;
    int registered = 0, unowned = 0;
    Render::DrawCallList draws;
    draws.push_back(sizedDraw(&registered, 0.0f, -1000, 5, 1000));
    draws.push_back(sizedDraw(&unowned, 0.0f, -1000, 5, 1000));
    auto errs = [&](const void *tag) {
        return tag == &registered ? 0.0f : Render::kTagUnknown;
    };

    Render::PlanDemoteStats stats;
    EXPECT_TRUE(Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, errs, &stats)
                    .empty());
    EXPECT_EQ(stats.considered, 2u);
    EXPECT_EQ(stats.noRung, 1u);
    EXPECT_EQ(stats.unregistered, 1u);
}

TEST(PlanMeshDemotes, pressureBuysWhatTheMarginRefused)
{
    // Half 5 at depth 100 projects ~95 px, so a 3% coarse rung errs
    // ~2.8 px -- over the 1 px margin of a 2 px tolerance, and refused
    // outright while nothing is asking for memory. It is the only
    // thing this scene has to give, so a deficit takes it: better a
    // model shown coarse than one that cannot be shown.
    PlanCamera cam;
    int only = 0;
    Render::DrawCallList draws;
    draws.push_back(sizedDraw(&only, 0.0f, -100, 5, 1000));
    std::map<const void *, float> errs{{&only, 0.03f}};

    Render::PlanDemoteStats calm;
    EXPECT_TRUE(Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, demoteErrs(errs), &calm)
                    .empty());
    EXPECT_EQ(calm.tooBig, 1u);
    EXPECT_EQ(calm.underPressure, 0u);
    EXPECT_EQ(calm.bytesFreed, 0u);

    Render::PlanDemoteStats pressed;
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, demoteErrs(errs), &pressed,
                                        1000 * kVertBytes);
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], &only);
    EXPECT_EQ(pressed.underPressure, 1u);
    EXPECT_EQ(pressed.tooBig, 0u);
    EXPECT_EQ(pressed.bytesFreed, 1000 * kVertBytes);
    // The tolerance the plan effectively ran at -- an outcome, and one
    // the readout can state rather than a number nobody chose.
    EXPECT_NEAR(pressed.acceptedErrorPx, 2.84f, 0.05f);
    // Both passes examined the same source once, not once per draw.
    EXPECT_EQ(pressed.considered, 1u);
}

TEST(PlanMeshDemotes, theDeficitStopsTheCheapestFirstWalk)
{
    // Three sources over the margin at 2.8, 1.9 and 1.4 px. A deficit
    // of 20 KB against 12 KB apiece must take the two cheapest and
    // stop -- the whole point of pricing the tier is that pressure
    // raises the tolerance only as far as it has to.
    PlanCamera cam;
    int close = 0, mid = 0, cheap = 0;
    Render::DrawCallList draws;
    draws.push_back(sizedDraw(&close, 0.0f, -100, 5, 1000));
    draws.push_back(sizedDraw(&mid, 0.0f, -150, 5, 1000));
    draws.push_back(sizedDraw(&cheap, 0.0f, -200, 5, 1000));
    std::map<const void *, float> errs{
        {&close, 0.03f}, {&mid, 0.03f}, {&cheap, 0.03f}};

    Render::PlanDemoteStats stats;
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, demoteErrs(errs), &stats,
                                        20000);
    ASSERT_EQ(tags.size(), 2u);
    EXPECT_EQ(tags[0], &cheap);
    EXPECT_EQ(tags[1], &mid);
    EXPECT_EQ(stats.underPressure, 2u);
    EXPECT_EQ(stats.tooBig, 1u);
    EXPECT_EQ(stats.bytesFreed, 2000 * kVertBytes);
    EXPECT_NEAR(stats.acceptedErrorPx, 1.9f, 0.1f);
}

TEST(PlanMeshDemotes, theFreeTierIsSpentBeforeAnythingVisible)
{
    // An off-screen source covers the deficit on its own, so the
    // visible one is left alone however much the budget wanted: free
    // bytes always come first, and the priced tier only sees what they
    // did not cover.
    PlanCamera cam;
    int aside = 0, seen = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&aside, 0.0f, 500, 0, -100, 5, 4000));
    draws.push_back(sizedDraw(&seen, 0.0f, -100, 5, 1000));
    std::map<const void *, float> errs{{&aside, 0.03f}, {&seen, 0.03f}};

    Render::PlanDemoteStats stats;
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, demoteErrs(errs), &stats,
                                        20000);
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], &aside);
    EXPECT_EQ(stats.offscreen, 1u);
    EXPECT_EQ(stats.underPressure, 0u);
    EXPECT_EQ(stats.tooBig, 1u);
    // Nothing visible was traded, so the plan ran at no error at all.
    EXPECT_FLOAT_EQ(stats.acceptedErrorPx, 0.0f);
}

TEST(PlanMeshDemotes, anInstancedSourceIsChargedOncePerMesh)
{
    // Two rows of one proto share one upload. Summing per row would
    // report twice what demoting it gives back, and a deficit priced
    // that way stops half way -- the overstatement the far-field cut
    // hit first (495 instanced submits for 5432 rows).
    PlanCamera cam;
    int proto = 0;
    Render::DrawCallList draws;
    draws.push_back(sizedDraw(&proto, 0.0f, -100, 5, 1000));
    Render::DrawCall second = draws.front();
    second.objectKey = 2;
    second.bboxMin[0] += 20;
    second.bboxMax[0] += 20;
    draws.front().objectKey = 1;
    draws.push_back(second);
    std::map<const void *, float> errs{{&proto, 0.03f}};

    Render::PlanDemoteStats stats;
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, demoteErrs(errs), &stats,
                                        1000 * kVertBytes);
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(stats.bytesFreed, 1000 * kVertBytes);
    EXPECT_EQ(stats.considered, 1u);
}

TEST(PlanMeshDemotes, pressurePricesASharedSourceByItsNeediestOwner)
{
    // The free tier's veto becomes a price: a proto with one near
    // instance and one far one is not refused outright under pressure,
    // it is simply expensive -- so a lone far source is spent first,
    // and the proto only follows if the deficit is still open.
    PlanCamera cam;
    int proto = 0, lone = 0;
    Render::DrawCallList draws;
    Render::DrawCall nearRow = sizedDraw(&proto, 0.0f, -100, 5, 1000);
    nearRow.objectKey = 1;
    Render::DrawCall farRow = sizedDraw(&proto, 0.0f, -200, 5, 1000);
    farRow.objectKey = 2;
    draws.push_back(nearRow);
    draws.push_back(farRow);
    Render::DrawCall loneRow = sizedDraw(&lone, 0.0f, -200, 5, 1000);
    loneRow.objectKey = 3;
    draws.push_back(loneRow);
    std::map<const void *, float> errs{{&proto, 0.03f}, {&lone, 0.03f}};

    // Enough for one source: the cheap one, priced at its own 1.4 px,
    // beats the proto priced at its near instance's 2.8 px.
    Render::PlanDemoteStats stats;
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, demoteErrs(errs), &stats,
                                        1000 * kVertBytes);
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], &lone);
    EXPECT_NEAR(stats.acceptedErrorPx, 1.42f, 0.1f);

    // A deficit neither can cover alone takes both, worst last.
    auto more = Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, demoteErrs(errs), nullptr,
                                        1000 * kVertBytes + 1);
    ASSERT_EQ(more.size(), 2u);
    EXPECT_EQ(more[1], &proto);
}

TEST(PlanMeshDemotes, theDeficitIsSpentInTheCurrencyItIsQuotedIn)
{
    // The GPU budget and the CPU ceiling are different quantities, and
    // the same mesh has a different price in each: on the heap a line
    // segment is two int32 indices, on the GPU it is those indices AND
    // a 64-byte quad-expansion instance record. A sweep spending GPU
    // bytes while counting heap bytes reports a deficit covered and
    // leaves the budget standing -- so the caller states the currency.
    PlanCamera cam;
    int a = 0, b = 0;
    Render::DrawCallList draws;
    draws.push_back(sizedDraw(&a, 0.0f, -1000, 5, 1000));
    draws.push_back(sizedDraw(&b, 0.0f, -1000, 5, 1000));
    std::map<const void *, float> errs{{&a, 0.03f}, {&b, 0.03f}};

    // Default currency: the heap arrays. Both sources are off screen
    // (free tier), so both go and the total is what they occupy.
    Render::PlanDemoteStats heap;
    auto plain = Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                         2.0f, demoteErrs(errs), &heap);
    EXPECT_EQ(plain.size(), 2u);
    EXPECT_EQ(heap.bytesFreed, 2 * 1000 * kVertBytes);

    // The caller's currency, eight times dearer, is what gets reported
    // -- the whole point being that the deficit it is compared against
    // is quoted the same way.
    Render::PlanDemoteStats gpu;
    Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f, 2.0f,
                            demoteErrs(errs), &gpu, 0,
                            [](const Render::MeshData *m) -> uint64_t {
                                return uint64_t(m->numVertices) * 8
                                    * kVertBytes;
                            });
    EXPECT_EQ(gpu.bytesFreed, 8 * 2 * 1000 * kVertBytes);
}

TEST(PlanMeshDemotes, aSharedUploadIsOfferedToTheCurrencyOnlyOnce)
{
    // The contract a stateful currency stands on: bytesOf sees each
    // distinct mesh once per pass, so an implementation that charges a
    // shared GPU buffer to the first referent and answers 0 for the
    // rest promises that memory exactly once. Promising it twice is
    // how a sweep covers a deficit on paper and stays over budget --
    // and the renderer's uploads really are shared, by colour variants
    // of one TShape pointing at a single geometry buffer.
    PlanCamera cam;
    int proto = 0;
    Render::DrawCallList draws;
    draws.push_back(sizedDraw(&proto, 0.0f, -1000, 5, 1000));
    Render::DrawCall second = draws.front();  // same MeshData, second row
    second.objectKey = 2;
    draws.push_back(second);
    std::map<const void *, float> errs{{&proto, 0.03f}};

    int calls = 0;
    Render::PlanDemoteStats stats;
    auto tags = Render::planMeshDemotes(
        draws, cam.view, cam.proj, 1000.0f, 2.0f, demoteErrs(errs), &stats, 0,
        [&calls](const Render::MeshData *) -> uint64_t {
            return ++calls == 1 ? 4096 : 0;  // charged once, then free
        });
    EXPECT_EQ(tags.size(), 1u);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(stats.bytesFreed, 4096u);
}

TEST(PlanMeshDemotes, aCoarseSourceDescendsAgainUnderPressure)
{
    // The step that makes the ladder unbounded: an object already at
    // its coarse rung, on screen and too big for the free tier, taken
    // because the deficit demands it -- priced by what its NEXT step
    // coarser would show, not by what it shows now.
    PlanCamera cam;
    int shown = 0;
    Render::DrawCallList draws;
    draws.push_back(sizedDraw(&shown, 0.03f, -100, 5, 1000));
    std::map<const void *, float> errs{{&shown, 0.06f}};

    Render::PlanDemoteStats calm;
    EXPECT_TRUE(Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, demoteErrs(errs), &calm)
                    .empty());
    EXPECT_EQ(calm.tooBig, 1u);

    Render::PlanDemoteStats pressed;
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                        2.0f, demoteErrs(errs), &pressed,
                                        1000 * kVertBytes);
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(pressed.underPressure, 1u);
    // Twice the coarse error of the pressureBuys test, so twice its
    // accepted error: the price is the step down, not the rung it is on.
    EXPECT_NEAR(pressed.acceptedErrorPx, 5.69f, 0.1f);
}

TEST(PlanMeshDemotes, theAcceptedErrorIsQuotedInTheCurrencyTheClimbReads)
{
    // Same bound on the other side: the descent may select on the true
    // projected errors -- the ordering among them is real -- but what
    // it REPORTS becomes the climb's tolerance, and 16211px quoted at a
    // pass that can never see more than the viewport height is not a
    // stricter statement, only an unreadable one.
    PlanCamera cam;
    int giant = 0;
    Render::DrawCallList draws;
    draws.push_back(meshDraw(&giant, 0.0f, 0, 0, -10, 5, 1000));
    std::map<const void *, float> errs{{&giant, 0.9f}};

    Render::PlanDemoteStats stats;
    auto tags = Render::planMeshDemotes(draws, cam.view, cam.proj, 600.0f,
                                        2.0f, demoteErrs(errs), &stats,
                                        1000 * kVertBytes);
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_FLOAT_EQ(stats.acceptedErrorPx, 600.0f);
}

// The release half of the ladder's control loop (sec 13c.3). Everything
// here is about ONE question: what happens on the plan after the budget
// is finally met, which is where the limit cycle lived.
namespace
{
// The camera's own tolerance and viewport for these; the controller does
// no projection of its own, it only arbitrates numbers the sweeps
// produce.
constexpr float kCamTol = 2.0f;
constexpr float kViewport = 1200.0f;
}  // namespace

TEST(PressureTolerance, theClimbReadsTheDescentsAcceptedError)
{
    Render::PressureTolerance pt;
    // No pressure, nothing accepted: the camera's number, unchanged.
    EXPECT_FLOAT_EQ(pt.update(false, 0.0f, kCamTol, kViewport, 0.5f), kCamTol);

    // Pressure, and the descent had to accept 40px of visible error:
    // the climb must not immediately ask back what that just gave up,
    // so its threshold sits above the accepted error by the same margin
    // the two passes use with no pressure at all.
    EXPECT_FLOAT_EQ(pt.update(true, 40.0f, kCamTol, kViewport, 0.5f),
                    40.0f / Render::kPlanDemoteMargin);
    EXPECT_FLOAT_EQ(pt.raisedPx, 40.0f);
}

TEST(PressureTolerance, theAttackIsARunningMaximum)
{
    // One plan accepting less than the last does not mean the scene got
    // cheaper -- the plan after a successful descent finds only sources
    // with no rung left to drop and accepts nothing at all.
    Render::PressureTolerance pt;
    pt.update(true, 40.0f, kCamTol, kViewport, 0.5f);
    pt.update(true, 5.0f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.raisedPx, 40.0f);
    pt.update(true, 51.0f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.raisedPx, 51.0f);
}

TEST(PressureTolerance, aNonFiniteAcceptedErrorIsNotAMeasurement)
{
    // Measured for real on 9 of 30 plans: one non-finite candidate
    // pinned the running maximum at infinity, and an infinite refine
    // tolerance is the climb switched OFF, not a large tolerance.
    Render::PressureTolerance pt;
    pt.update(true, 40.0f, kCamTol, kViewport, 0.5f);
    const float tol = pt.update(true, std::numeric_limits<float>::infinity(),
                                kCamTol, kViewport, 0.5f);
    EXPECT_TRUE(std::isfinite(tol));
    EXPECT_FLOAT_EQ(pt.raisedPx, 40.0f);

    Render::PressureTolerance nan;
    nan.update(true, std::numeric_limits<float>::quiet_NaN(), kCamTol,
               kViewport, 0.5f);
    EXPECT_FLOAT_EQ(nan.raisedPx, 0.0f);
}

TEST(PressureTolerance, theScreenBoundsWhatCanBeAccepted)
{
    // 4.3e11 px was reached before this bound. An error of a million
    // pixels and an error of the viewport height say the same thing.
    Render::PressureTolerance pt;
    pt.update(true, 4.3e11f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.raisedPx, kViewport);
}

TEST(PressureTolerance, theFirstPlanThatFitsDoesNotHandItAllBack)
{
    // THE DEFECT, stated as a test: 51px -> 2px in one step asked 946
    // objects to re-tessellate at once and broke the budget again.
    Render::PressureTolerance pt;
    pt.update(true, 51.0f, kCamTol, kViewport, 0.5f);
    const float first = pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.raisedPx, 25.5f);
    EXPECT_FLOAT_EQ(first, 51.0f);
    EXPECT_TRUE(pt.releasing);
}

TEST(PressureTolerance, aReleaseFractionOfZeroIsTheOldSnap)
{
    // Kept reachable so the defect can be MEASURED against the fix
    // rather than argued about.
    Render::PressureTolerance pt;
    pt.update(true, 51.0f, kCamTol, kViewport, 0.0f);
    EXPECT_FLOAT_EQ(pt.update(false, 0.0f, kCamTol, kViewport, 0.0f), kCamTol);
    EXPECT_FLOAT_EQ(pt.raisedPx, 0.0f);
}

TEST(PressureTolerance, aSceneThatFitsReleasesAllTheWayBack)
{
    // Steps are not a ratchet: when nothing pushes back, the camera's
    // tolerance rules again and the ladder is at full quality.
    Render::PressureTolerance pt;
    pt.update(true, 51.0f, kCamTol, kViewport, 0.5f);
    float tol = 0.0f;
    for (int i = 0; i < 20 && pt.raisedPx > 0.0f; ++i)
        tol = pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.raisedPx, 0.0f);
    EXPECT_FLOAT_EQ(tol, kCamTol);
}

TEST(PressureTolerance, aReleaseThatBreaksTheBudgetIsRemembered)
{
    // The learned equilibrium, and the whole reason this is not just
    // damping: the level that broke the budget becomes a floor, and the
    // walk down stops above it instead of trying it again forever.
    Render::PressureTolerance pt;
    pt.update(true, 51.0f, kCamTol, kViewport, 0.5f);
    pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);   // 25.5, released
    ASSERT_FLOAT_EQ(pt.raisedPx, 25.5f);

    // ...and the scene went back over budget at that level.
    pt.update(true, 30.0f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.floorPx, 25.5f);
    EXPECT_FLOAT_EQ(pt.raisedPx, 30.0f);

    // Now the release walks down and STOPS: 15 would be under the floor,
    // so the ladder holds 30px of error, which is what fitting costs.
    for (int i = 0; i < 10; ++i)
        pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.raisedPx, 30.0f);
    EXPECT_FALSE(pt.releasing);
}

TEST(PressureTolerance, theLoopTerminatesInsteadOfCycling)
{
    // The measured cycle, simulated: a scene that fits at 20px and does
    // not fit below it. Before the floor this ran forever -- 43 plans in
    // 611s with no steady state. Here it must go quiet.
    Render::PressureTolerance pt;
    int releases = 0;
    bool over = true;
    for (int plan = 0; plan < 200; ++plan) {
        const float tol = pt.update(over, over ? 51.0f : 0.0f, kCamTol,
                                    kViewport, 0.5f);
        if (pt.releasing)
            ++releases;
        // The scene is over budget whenever the climb is allowed to ask
        // for anything erring less than 20px on screen.
        over = tol < 20.0f / Render::kPlanDemoteMargin;
    }
    EXPECT_FALSE(over);
    EXPECT_FALSE(pt.releasing);
    EXPECT_GT(pt.floorPx, 0.0f);
    // It settled early rather than kept trading: a handful of steps, not
    // one per plan.
    EXPECT_LT(releases, 20);
}

TEST(PressureTolerance, aLatePressureStillBlamesTheStepThatCausedIt)
{
    // The refines a release step asks for land plans later, so the
    // pressure they cause usually arrives after the staircase has
    // already stopped. Learning only mid-step would re-try the level
    // that failed forever.
    Render::PressureTolerance pt;
    pt.update(true, 51.0f, kCamTol, kViewport, 0.5f);
    pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);      // 25.5
    pt.floorPx = 20.0f;                                    // stops below this
    pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);      // held at 25.5
    ASSERT_FALSE(pt.releasing);

    pt.update(true, 26.0f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.floorPx, 25.5f);
}

TEST(PressureTolerance, theStepThatGivesTheLastOfItBackIsStillALevel)
{
    // Releasing the final scrap sets the raise to 0, and a pressure
    // blaming that step must not learn a floor of nothing -- that is
    // the snap again, one staircase later.
    Render::PressureTolerance pt;
    pt.update(true, 8.0f, kCamTol, kViewport, 0.5f);
    float last = 0.0f;
    for (int i = 0; i < 20 && pt.raisedPx > 0.0f; ++i) {
        last = pt.raisedPx;
        pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);
    }
    ASSERT_FLOAT_EQ(pt.raisedPx, 0.0f);

    pt.update(true, 8.0f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.floorPx, last);
    // ...so the next walk down stops instead of reaching zero again.
    for (int i = 0; i < 20; ++i)
        pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);
    EXPECT_GT(pt.raisedPx, 0.0f);
}

TEST(PressureTolerance, aNewCameraForgetsWhatTheLastOneLearned)
{
    // What a rung costs on screen is a function of where the camera is,
    // so a floor measured from one view is not evidence about another.
    Render::PressureTolerance pt;
    pt.update(true, 51.0f, kCamTol, kViewport, 0.5f);
    pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);
    pt.update(true, 30.0f, kCamTol, kViewport, 0.5f);
    ASSERT_GT(pt.floorPx, 0.0f);

    pt.forget();
    EXPECT_FLOAT_EQ(pt.floorPx, 0.0f);
    for (int i = 0; i < 20 && pt.raisedPx > 0.0f; ++i)
        pt.update(false, 0.0f, kCamTol, kViewport, 0.5f);
    EXPECT_FLOAT_EQ(pt.raisedPx, 0.0f);
}


// The downgrade ledger (SceneLadder.h): the sweep's own unlanded
// orders carried as credit, so a plan sampling the apply transient
// does not re-correct off it. The numbers in the first two cases are
// the measured storm itself: order 93MB at live 157, next plan reads
// 165 (old+new double residency), and without the ledger the sweep
// walked the registry to the bottom.
TEST(DowngradeLedger, theTransientDoesNotRestateTheDeficit)
{
    constexpr uint64_t MB = 1048576;
    Render::DowngradeLedger led;
    led.order(93 * MB, 157 * MB, 10);
    // live ROSE from the swap-in: nothing landed, the full promise
    // stands, and the deficit is what the transient adds beyond the
    // order -- 8MB, not the storm's 101.
    EXPECT_EQ(led.deficit(165 * MB, 64 * MB, 12), 8 * MB);
    // A rise never consumes credit: asked again, same answer.
    EXPECT_EQ(led.deficit(165 * MB, 64 * MB, 13), 8 * MB);
}

TEST(DowngradeLedger, aLandingSettlesThePromiseOnce)
{
    constexpr uint64_t MB = 1048576;
    Render::DowngradeLedger led;
    led.order(93 * MB, 157 * MB, 10);
    // The collapse lands everything: credit settles, and the scene
    // now UNDER budget owes nothing.
    EXPECT_EQ(led.deficit(45 * MB, 64 * MB, 12), 0u);
    // No stale credit shields a genuinely new excess afterwards.
    EXPECT_EQ(led.deficit(165 * MB, 64 * MB, 13), 101 * MB);
}

TEST(DowngradeLedger, aPartialLandingCreditsExactlyTheFall)
{
    constexpr uint64_t MB = 1048576;
    Render::DowngradeLedger led;
    led.order(93 * MB, 157 * MB, 10);
    // Fell 57 of the 93: 36 outstanding, which covers the raw 36MB
    // excess exactly -- the sweep holds.
    EXPECT_EQ(led.deficit(100 * MB, 64 * MB, 12), 0u);
    // The same fall is not credited twice: live unchanged, credit
    // unchanged, still held.
    EXPECT_EQ(led.deficit(100 * MB, 64 * MB, 13), 0u);
}

TEST(DowngradeLedger, anUnlandablePromiseExpiresIntoTheTruth)
{
    constexpr uint64_t MB = 1048576;
    Render::DowngradeLedger led;
    led.order(93 * MB, 157 * MB, 10);
    // Shared-geometry pinning: nothing ever lands. Inside the settle
    // window the sweep is held to the residual...
    EXPECT_EQ(led.deficit(165 * MB, 64 * MB, 12), 8 * MB);
    // ...and at the horizon the promise is written off and the raw
    // excess is acted on again.
    EXPECT_EQ(led.deficit(165 * MB, 64 * MB,
                          10 + Render::DowngradeLedger::kSettleFrames),
              101 * MB);
}

TEST(DowngradeLedger, followUpOrdersAccumulate)
{
    constexpr uint64_t MB = 1048576;
    Render::DowngradeLedger led;
    led.order(93 * MB, 157 * MB, 10);
    led.order(8 * MB, 165 * MB, 12);
    // 101MB in flight against a 101MB excess: held.
    EXPECT_EQ(led.deficit(165 * MB, 64 * MB, 13), 0u);
    // The full landing settles both orders.
    EXPECT_EQ(led.deficit(45 * MB, 64 * MB, 14), 0u);
    EXPECT_EQ(led.promised(), 0u);
}

TEST(DowngradeLedger, unsettledOrdersHoldTheWriteOffHorizon)
{
    // An order now lands as CHAINS of jobs (hook body -> worker build
    // -> pooled fill -> apply), and its bytes cannot fall before its
    // own descent generation drains -- on a loaded pool, far past any
    // frame count. Until then the credit must stand however many
    // frames pass; the settle window starts only once it has.
    constexpr uint64_t MB = 1048576;
    Render::DowngradeLedger led;
    bool drained = false;
    auto settled = [&drained](uint64_t gen) {
        return gen != 7 || drained;
    };
    // The order's chains queue under generation 7.
    led.order(93 * MB, 157 * MB, 10, /*gen*/ 7);
    // 100 frames later the chain still works: promise stands, the
    // sweep is held to the transient's residual.
    EXPECT_EQ(led.deficit(165 * MB, 64 * MB, 110, settled), 8 * MB);
    // The chain drains by frame 110; the settle window runs from
    // there, not from the order.
    drained = true;
    EXPECT_EQ(led.deficit(165 * MB, 64 * MB, 112, settled), 8 * MB);
    // ...and past it the unlanded remainder is written off into the
    // truth -- the phantom promise of drops that freed nothing (a
    // "spent" decimation keeps its mesh) must not shield the excess.
    EXPECT_EQ(led.deficit(165 * MB, 64 * MB,
                          110 + Render::DowngradeLedger::kSettleFrames,
                          settled),
              101 * MB);
}

TEST(DowngradeLedger, landingsStillCreditWhileJobsFly)
{
    // The hold does not defer the observations: falls credit the
    // promise as they land, drained or not.
    constexpr uint64_t MB = 1048576;
    Render::DowngradeLedger led;
    auto flying = [](uint64_t) { return false; };
    led.order(93 * MB, 157 * MB, 10, 7);
    // Half the batch landed while the rest still queues: the fall is
    // credited, the residual excess is actionable.
    EXPECT_EQ(led.deficit(100 * MB, 64 * MB, 50, flying), 0u);
    // Everything lands, the scene settles under budget: nothing owed.
    EXPECT_EQ(led.deficit(45 * MB, 64 * MB, 90, flying), 0u);
    EXPECT_EQ(led.promised(), 0u);
}

TEST(DowngradeLedger, anOldPhantomExpiresAloneUnderNewerOrders)
{
    // The failure the per-order books exist for: a converging ladder
    // orders every plan, and an aggregate horizon re-stamped by each
    // new order held EVERY phantom promise forever -- measured as the
    // sweep crawling 2MB-deficits against 50MB of standing excess.
    // Here the old order's unlanded promise expires on its own clock
    // while the newer order, its chain still working, holds on.
    constexpr uint64_t MB = 1048576;
    Render::DowngradeLedger led;
    bool aDrained = false, bDrained = false;
    auto settled = [&](uint64_t gen) {
        return gen == 1 ? aDrained : bDrained;
    };
    led.order(20 * MB, 100 * MB, 10, /*gen*/ 1);
    led.order(20 * MB, 100 * MB, 20, /*gen*/ 2);
    // Frame 200: A's chain drained long ago and its grace is long
    // out -- its phantom writes off ALONE. B still works and holds.
    aDrained = true;
    EXPECT_EQ(led.deficit(100 * MB, 64 * MB, 200, settled), 16 * MB);
    // B drains at frame 205; its own grace runs from the hold...
    bDrained = true;
    EXPECT_EQ(led.deficit(100 * MB, 64 * MB, 205, settled), 16 * MB);
    // ...and closes on what it never landed.
    EXPECT_EQ(led.deficit(100 * MB, 64 * MB,
                          200 + Render::DowngradeLedger::kSettleFrames,
                          settled),
              36 * MB);
}

TEST(PlanMeshDemotes, theOrderCapDefersAndCountsWhatItDefers)
{
    // Ten droppable off-screen sources, a cap of 3: one pass orders
    // three, reports seven deferred (nothing refused -- their hooks
    // stand for the replan), and bytesFreed prices only what was
    // ordered so the ledger's credit stays honest.
    PlanCamera cam;
    int tags[10];
    Render::DrawCallList draws;
    std::map<const void *, float> errs;
    for (int i = 0; i < 10; ++i) {
        draws.push_back(meshDraw(&tags[i], 0.0f, 500, float(i * 20),
                                 -100, 5, 100));
        errs[&tags[i]] = 0.03f;
    }
    Render::PlanDemoteStats stats;
    auto out = Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                       2.0f, demoteErrs(errs), &stats, 0,
                                       {}, {}, /*maxOrders*/ 3);
    EXPECT_EQ(out.size(), 3u);
    EXPECT_EQ(stats.deferredByCap, 7u);
    EXPECT_EQ(stats.bytesFreed, 3u * 100u * kVertBytes);
    // Uncapped control: all ten go in one pass.
    Render::PlanDemoteStats all;
    EXPECT_EQ(Render::planMeshDemotes(draws, cam.view, cam.proj, 1000.0f,
                                      2.0f, demoteErrs(errs), &all)
                  .size(),
              10u);
    EXPECT_EQ(all.deferredByCap, 0u);
}
