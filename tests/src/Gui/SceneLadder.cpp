// Tests for the shared ladder policy (docs/SceneStreaming.md §6/§7):
// the budget estimator, the ranker's size weighting, and the level
// plan — what the whole scene should hold, and the executor's step
// toward it. Like the SceneDump and MeshSimplify tests beside them
// these need no GL context and no document.

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
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

TEST(PlanMeshDemotes, onlyDemotableSourcesAreConsidered)
{
    // A coarse source (levelError > 0) and an exact one the registry
    // reports no fallback for (err 0) are both out, even off screen;
    // and a non-positive tolerance demotes nothing at all.
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
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], &cheap);
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
