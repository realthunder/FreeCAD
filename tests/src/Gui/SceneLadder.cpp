// Tests for the shared ladder policy (docs/SceneStreaming.md §6/§7):
// the budget estimator, the ranker's size weighting, and the level
// plan — what the whole scene should hold, and the executor's step
// toward it. Like the SceneDump and MeshSimplify tests beside them
// these need no GL context and no document.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

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
    // Target exact with nothing on screen: fetch the coarsest built
    // rung first — kilobytes now, the target lands as an upgrade over
    // it instead of over a box. With the coarse rung up, the target is
    // fetched directly; at target, nothing.
    auto entry = levelEntry(true, true);
    entry.plan = 2;
    auto step = Render::planStep(entry, -1);
    EXPECT_EQ(step.fetch, 0);
    EXPECT_EQ(step.generate, -1);
    EXPECT_FALSE(step.release);
    step = Render::planStep(entry, 0);
    EXPECT_EQ(step.fetch, 2);
    step = Render::planStep(entry, 2);
    EXPECT_EQ(step.fetch, -1);
    EXPECT_EQ(step.generate, -1);
    EXPECT_FALSE(step.release);
}

TEST(PlanStep, anUnbuiltTargetGeneratesAndStandsOnTheNearestBuilt)
{
    // Level 1 targeted but not built: ask the producer. Standing on
    // level 0 already, there is nothing worth fetching meanwhile; with
    // nothing resident, the built coarser rung goes up first.
    auto entry = levelEntry(true, false);
    entry.plan = 1;
    auto step = Render::planStep(entry, 0);
    EXPECT_EQ(step.generate, 1);
    EXPECT_EQ(step.fetch, -1);
    step = Render::planStep(entry, -1);
    EXPECT_EQ(step.generate, 1);
    EXPECT_EQ(step.fetch, 0);
    // Nothing built on the coarse side at all: the exact mesh is the
    // only rung there is.
    entry = levelEntry(false, false);
    entry.plan = 1;
    step = Render::planStep(entry, -1);
    EXPECT_EQ(step.generate, 1);
    EXPECT_EQ(step.fetch, 2);
}

TEST(PlanStep, aboveTheTargetWalksBackDown)
{
    // Downgrades are the plan's move now: resident above the target
    // fetches the built target (the arrival bookkeeping releases the
    // finer rung), and a box target releases outright.
    auto entry = levelEntry(true, true);
    entry.plan = 0;
    auto step = Render::planStep(entry, 2);
    EXPECT_EQ(step.fetch, 0);
    EXPECT_FALSE(step.release);
    entry.plan = Render::SceneSnapshot::DeferredChunk::kPlanBox;
    step = Render::planStep(entry, 1);
    EXPECT_TRUE(step.release);
    EXPECT_EQ(step.fetch, -1);
    step = Render::planStep(entry, -1);
    EXPECT_FALSE(step.release);
    EXPECT_EQ(step.fetch, -1);
}

TEST(PlanStep, anUnplannedEntryTargetsItsFinestBuiltRung)
{
    // kPlanUnset is "no plan has looked yet": behave as a consumer did
    // before there was a plan — the finest built rung — so an entry
    // discovered between plans is never stranded.
    auto entry = levelEntry(true, true);
    ASSERT_EQ(entry.plan, Render::SceneSnapshot::DeferredChunk::kPlanUnset);
    auto step = Render::planStep(entry, 0);
    EXPECT_EQ(step.fetch, 2);
    // And an entry with no ladder at all is a one-rung ladder.
    Render::SceneSnapshot::DeferredChunk bare;
    bare.key = std::string(40, 'x');
    bare.size = 500;
    bare.release = []() {};
    step = Render::planStep(bare, -1);
    EXPECT_EQ(step.fetch, 0);
    step = Render::planStep(bare, 0);
    EXPECT_EQ(step.fetch, -1);
}
