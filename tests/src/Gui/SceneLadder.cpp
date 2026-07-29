// Tests for the shared ladder policy (docs/SceneStreaming.md §6/§11 4c):
// the budget estimator, the ranker's size weighting and the evictor's
// victim rules. Like the SceneDump and MeshSimplify tests beside them
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

TEST(Evictor, theViewsOwnChunksAreNeverVictims)
{
    // An ownerless chunk is the view's own (navigation cube, axis
    // cross): infinitely valuable to residency, and excluded from the
    // victim list by rule rather than left to FLT_MAX arithmetic.
    Render::SceneSnapshot snap;
    snap.deferredChunks.push_back(chunk("view", 4096, false));
    snap.deferredChunks.push_back(chunk("model", 4096, true));
    for (auto &entry : snap.deferredChunks)
        entry.release = []() {};

    std::vector<std::string> released;
    Render::RungRanker ranker = makeRanker();
    Render::Evictor evictor(
        snap, ranker, [](const std::string &) { return true; },
        [&released](Render::SceneSnapshot::DeferredChunk &entry, float) {
            released.push_back(entry.key);
        });

    const float incoming = 1e9f;
    // Both chunks' worth of room cannot be made: only the model chunk
    // is a candidate, and a planned eviction that cannot reach its goal
    // releases nothing.
    EXPECT_FALSE(evictor.makeRoom(incoming, 8192));
    EXPECT_TRUE(released.empty());
    // One chunk's worth can, and it is the model's, never the view's.
    EXPECT_TRUE(evictor.makeRoom(incoming, 4096));
    ASSERT_EQ(released.size(), 1u);
    EXPECT_EQ(released[0], "model");
}
