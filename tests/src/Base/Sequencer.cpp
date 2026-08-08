#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <Base/Sequencer.h>

// A quiet indicator: keeps ConsoleSequencer's printf out of the test output
// and pins the sequencer framework's notion of "main thread" to this thread.
static void ensureIndicator()
{
    static Base::EmptySequencer indicator;
    (void)indicator;
}

TEST(SequencerManager, SingleSequence)
{
    ensureIndicator();
    ASSERT_EQ(Base::SequencerManager::activeCount(), 0u);
    {
        Base::SequencerLauncher seq("reading", 10);
        EXPECT_EQ(Base::SequencerManager::activeCount(), 1u);
        for (int i = 0; i < 4; ++i)
            seq.next();
        auto snap = Base::SequencerManager::snapshot();
        ASSERT_EQ(snap.sequences.size(), 1u);
        EXPECT_EQ(snap.sequences[0].text, "reading");
        EXPECT_EQ(snap.sequences[0].progress, 4u);
        EXPECT_EQ(snap.sequences[0].total, 10u);
        EXPECT_TRUE(snap.sequences[0].mainThread);
        EXPECT_EQ(snap.progress, 4u);
        EXPECT_EQ(snap.total, 10u);
    }
    EXPECT_EQ(Base::SequencerManager::activeCount(), 0u);
    EXPECT_TRUE(Base::SequencerManager::snapshot().sequences.empty());
}

TEST(SequencerManager, NestedSameThreadReportsHierarchy)
{
    ensureIndicator();
    Base::SequencerLauncher outer("outer", 10);
    outer.next();
    Base::SequencerLauncher inner("inner", 100);
    for (int i = 0; i < 50; ++i)
        inner.next();
    auto snap = Base::SequencerManager::snapshot();
    ASSERT_EQ(snap.sequences.size(), 2u);
    EXPECT_EQ(snap.roots, 1u);
    EXPECT_EQ(snap.sequences[0].text, "outer");
    EXPECT_EQ(snap.sequences[0].depth, 0u);
    EXPECT_EQ(snap.sequences[0].progress, 1u);
    EXPECT_EQ(snap.sequences[1].text, "inner");
    EXPECT_EQ(snap.sequences[1].depth, 1u);
    EXPECT_EQ(snap.sequences[1].progress, 50u);
    EXPECT_EQ(snap.sequences[1].total, 100u);
    // only the root feeds the consolidated numbers
    EXPECT_EQ(snap.total, 10u);
    EXPECT_EQ(snap.progress, 1u);
}

TEST(SequencerManager, NestingDepthIsLimited)
{
    ensureIndicator();
    std::vector<std::unique_ptr<Base::SequencerLauncher>> stack;
    for (int i = 0; i < 7; ++i) {
        auto seq = std::make_unique<Base::SequencerLauncher>("level", 10);
        seq->next();
        stack.push_back(std::move(seq));
    }
    auto snap = Base::SequencerManager::snapshot();  // default limit: 5
    EXPECT_EQ(snap.sequences.size(), 5u);
    EXPECT_EQ(snap.sequences.back().depth, 4u);
    EXPECT_EQ(snap.roots, 1u);

    snap = Base::SequencerManager::snapshot(2);
    EXPECT_EQ(snap.sequences.size(), 2u);
    EXPECT_EQ(snap.sequences.back().depth, 1u);

    snap = Base::SequencerManager::snapshot(0);  // clamped to 1
    EXPECT_EQ(snap.sequences.size(), 1u);
}

TEST(SequencerManager, IdleLauncherInvisibleAndNotShadowing)
{
    ensureIndicator();
    Base::SequencerLauncher idle;  // no steps, never ticked
    {
        auto snap = Base::SequencerManager::snapshot();
        EXPECT_TRUE(snap.sequences.empty());
    }
    // An idle outer launcher must not hide an active inner one
    Base::SequencerLauncher inner("active", 5);
    inner.next();
    auto snap = Base::SequencerManager::snapshot();
    ASSERT_EQ(snap.sequences.size(), 1u);
    EXPECT_EQ(snap.sequences[0].text, "active");
}

TEST(SequencerManager, UnknownTotalIsIndeterminate)
{
    ensureIndicator();
    Base::SequencerLauncher seq("busy", 0);
    seq.next();
    auto snap = Base::SequencerManager::snapshot();
    ASSERT_EQ(snap.sequences.size(), 1u);
    EXPECT_EQ(snap.sequences[0].total, 0u);
    EXPECT_EQ(snap.sequences[0].progress, 1u);
    EXPECT_EQ(snap.total, 0u);
    EXPECT_EQ(snap.progress, 0u);
}

TEST(SequencerManager, ParallelThreadsConsolidate)
{
    ensureIndicator();
    Base::SequencerLauncher mainSeq("main", 100);
    for (int i = 0; i < 10; ++i)
        mainSeq.next();

    std::atomic<int> ready {0};
    std::promise<void> go;
    std::shared_future<void> released = go.get_future().share();
    auto worker = [&](const char* name) {
        Base::SequencerLauncher seq(name, 20);
        for (int i = 0; i < 5; ++i)
            seq.next();
        ready.fetch_add(1);
        released.wait();
    };
    std::thread t1(worker, "w1");
    std::thread t2(worker, "w2");
    while (ready.load() < 2)
        std::this_thread::yield();

    auto snap = Base::SequencerManager::snapshot();
    EXPECT_EQ(snap.sequences.size(), 3u);
    EXPECT_EQ(snap.roots, 3u);
    EXPECT_EQ(snap.total, 140u);    // 100 + 20 + 20
    EXPECT_EQ(snap.progress, 20u);  // 10 + 5 + 5
    size_t mainThreads = 0;
    for (const auto& info : snap.sequences)
        mainThreads += info.mainThread ? 1 : 0;
    EXPECT_EQ(mainThreads, 1u);

    go.set_value();
    t1.join();
    t2.join();

    snap = Base::SequencerManager::snapshot();
    ASSERT_EQ(snap.sequences.size(), 1u);
    EXPECT_EQ(snap.sequences[0].text, "main");
    EXPECT_EQ(snap.total, 100u);
}

TEST(SequencerLauncher, SharedLauncherConcurrentTicks)
{
    ensureIndicator();
    // One launcher shared by several workers (the parallel-recompute shape):
    // concurrent next() must neither lose ticks nor crash.
    const int threads = 4;
    const int ticks = 10000;
    Base::SequencerLauncher seq("shared", (size_t)threads * ticks);
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&seq] {
            for (int i = 0; i < ticks; ++i)
                seq.next();
        });
    }
    for (auto& th : pool)
        th.join();
    EXPECT_EQ(seq.progress(), (size_t)threads * ticks);
}

TEST(SequencerLauncher, WorkerTicksBehindTopLauncher)
{
    ensureIndicator();
    // A blocking launcher owns the indicator; worker launchers tick on the
    // lock-free path and are still counted exactly.
    Base::SequencerLauncher top("top", 10);
    Base::SequencerLauncher shared("jobs", 20000);
    std::vector<std::thread> pool;
    for (int t = 0; t < 2; ++t) {
        pool.emplace_back([&shared] {
            for (int i = 0; i < 10000; ++i)
                shared.next();
        });
    }
    for (auto& th : pool)
        th.join();
    EXPECT_EQ(shared.progress(), 20000u);
    EXPECT_EQ(top.progress(), 0u);
}

TEST(SequencerLauncher, NestedLaunchersDontRestartTop)
{
    ensureIndicator();
    // Counts how often the indicator is (re)started: per-item nested
    // launchers must not restart the running top indicator each time.
    class CountingSequencer: public Base::SequencerBase
    {
    public:
        int starts = 0;

    protected:
        void startStep(bool) override
        {
            ++starts;
        }
    } counter;

    Base::SequencerLauncher top("top", 10);
    EXPECT_EQ(counter.starts, 1);
    for (int i = 0; i < 100; ++i) {
        Base::SequencerLauncher item("item", 5);
        item.next();
    }
    EXPECT_EQ(counter.starts, 1);
    EXPECT_EQ(top.progress(), 0u);
}

TEST(SequencerLauncher, CancelSeenFromWorkerThread)
{
    ensureIndicator();
    Base::SequencerLauncher top("top", 10);  // keeps the worker off the indicator
    Base::SequencerLauncher seq("job", 100);
    seq.setCanceled(true);
    std::thread t([&seq] {
        EXPECT_THROW(seq.next(true), Base::AbortException);
        seq.setNoException(true);
        EXPECT_FALSE(seq.next(true));
    });
    t.join();
    EXPECT_TRUE(seq.wasCanceled());
}

/// Records what the indicator was told about each sequence that started.
class RecordingSequencer: public Base::SequencerBase
{
public:
    std::vector<bool> starts;  ///< one entry per start, true = claims input

protected:
    void startStep(bool blocking) override
    {
        starts.push_back(blocking);
    }
};

/** A sequence that runs in slices on the event loop reports without taking
 * the window away.
 *
 * "Started on the main thread" has always meant "owns the GUI thread until
 * it ends", and the indicator answers it by grabbing input. A load draining
 * in budgeted slices reports one sequence across many returns to the event
 * loop, and the whole point of the drain is that the window stays usable
 * while it runs (docs/ProgressiveLoading.md §2).
 */
TEST(SequencerLauncher, KeepInteractiveDoesNotClaimTheInput)
{
    ensureIndicator();
    RecordingSequencer rec;

    {
        Base::SequencerLauncher owns("owns the thread", 10);
        EXPECT_TRUE(owns.isBlocking());
        ASSERT_EQ(rec.starts.size(), 1u);
        EXPECT_TRUE(rec.starts.back());
    }
    {
        Base::SequencerLauncher sliced("sliced on the event loop", 10,
                                       Base::SequencerLauncher::KeepInteractive);
        EXPECT_FALSE(sliced.isBlocking());
        ASSERT_EQ(rec.starts.size(), 2u);
        EXPECT_FALSE(rec.starts.back());
        // It is still a reported sequence, with everything the poll needs.
        sliced.next();
        auto snap = Base::SequencerManager::snapshot();
        ASSERT_EQ(snap.sequences.size(), 1u);
        EXPECT_EQ(snap.sequences[0].text, "sliced on the event loop");
        EXPECT_EQ(snap.progress, 1u);
        EXPECT_EQ(snap.total, 10u);
        EXPECT_TRUE(snap.sequences[0].mainThread);
    }
}

/// An interactive sequence nested under a blocking one leaves the blocking
/// one's claim alone -- only the top launcher speaks for the indicator.
TEST(SequencerLauncher, KeepInteractiveNestedUnderBlocking)
{
    ensureIndicator();
    RecordingSequencer rec;
    Base::SequencerLauncher top("top", 10);
    ASSERT_EQ(rec.starts.size(), 1u);
    EXPECT_TRUE(top.isBlocking());

    Base::SequencerLauncher sliced("sliced", 10, Base::SequencerLauncher::KeepInteractive);
    EXPECT_EQ(rec.starts.size(), 1u);  // no restart, no second claim
    EXPECT_TRUE(top.isBlocking());
}

/** A worker's sequence promoted to the top must not come out claiming the
 * main thread's input.
 *
 * findNextLauncher() promotes from whatever thread let the previous top go,
 * so the blocking answer has to come from the launcher's owner thread, not
 * from the caller's.
 */
TEST(SequencerLauncher, PromotedWorkerSequenceDoesNotClaimTheInput)
{
    ensureIndicator();
    RecordingSequencer rec;

    auto top = std::make_unique<Base::SequencerLauncher>("main job", 10);
    ASSERT_TRUE(top->isBlocking());

    std::promise<void> created;
    std::promise<void> release;
    auto createdF = created.get_future();
    std::shared_future<void> releaseF = release.get_future().share();
    Base::SequencerLauncher* worker = nullptr;

    std::thread t([&] {
        Base::SequencerLauncher w("worker job", 50);
        worker = &w;
        created.set_value();
        releaseF.wait();
    });
    createdF.wait();
    EXPECT_FALSE(worker->isBlocking());

    // The main thread lets its own sequence go; the worker's is promoted.
    top.reset();
    EXPECT_FALSE(worker->isBlocking());
    ASSERT_FALSE(rec.starts.empty());
    EXPECT_FALSE(rec.starts.back());

    release.set_value();
    t.join();
}
