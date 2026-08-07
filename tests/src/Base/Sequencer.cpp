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
