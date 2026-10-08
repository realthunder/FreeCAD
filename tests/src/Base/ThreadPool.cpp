// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include <Base/ThreadPool.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{

/// Counts down to zero and lets a waiter know.
class Latch
{
public:
    explicit Latch(int count)
        : left(count)
    {}

    void done()
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (--left == 0) {
            reached.notify_all();
        }
    }

    bool wait(double seconds)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return reached.wait_for(lock, std::chrono::duration<double>(seconds), [this]() {
            return left <= 0;
        });
    }

private:
    std::mutex mutex;
    std::condition_variable reached;
    int left;
};

}  // namespace

TEST(ThreadPool, runsEveryTaskOffTheCallersThread)
{
    Base::ThreadPool pool(3);
    EXPECT_EQ(pool.size(), 3U);
    const int tasks = 500;
    Latch latch(tasks);
    std::atomic<int> ran {0};
    std::atomic<int> onCaller {0};
    const std::thread::id caller = std::this_thread::get_id();
    for (int i = 0; i < tasks; ++i) {
        pool.post([&]() {
            ++ran;
            if (std::this_thread::get_id() == caller) {
                ++onCaller;
            }
            latch.done();
        });
    }
    ASSERT_TRUE(latch.wait(30.0));
    EXPECT_EQ(ran.load(), tasks);
    EXPECT_EQ(onCaller.load(), 0);
}

TEST(ThreadPool, runsAsManyAtOnceAsItHasThreads)
{
    Base::ThreadPool pool(3);
    // Three tasks that each wait for the other two to have started: they
    // end only if three threads run them at the same time
    Latch started(3);
    Latch ended(3);
    std::mutex mutex;
    std::set<std::thread::id> threads;
    std::atomic<int> together {0};
    for (int i = 0; i < 3; ++i) {
        pool.post([&]() {
            {
                std::lock_guard<std::mutex> guard(mutex);
                threads.insert(std::this_thread::get_id());
            }
            started.done();
            if (started.wait(30.0)) {
                ++together;
            }
            ended.done();
        });
    }
    ASSERT_TRUE(ended.wait(60.0));
    EXPECT_EQ(together.load(), 3);
    EXPECT_EQ(threads.size(), 3U);
}

TEST(ThreadPool, startsTasksInTheOrderPosted)
{
    Base::ThreadPool pool(1);
    const int tasks = 200;
    Latch latch(tasks);
    std::vector<int> order;
    for (int i = 0; i < tasks; ++i) {
        // One thread: nothing else writes the vector
        pool.post([&order, &latch, i]() {
            order.push_back(i);
            latch.done();
        });
    }
    ASSERT_TRUE(latch.wait(30.0));
    ASSERT_EQ(order.size(), std::size_t(tasks));
    for (int i = 0; i < tasks; ++i) {
        EXPECT_EQ(order[std::size_t(i)], i);
    }
}

TEST(ThreadPool, aTaskThatThrowsDoesNotTakeItsThreadWithIt)
{
    Base::ThreadPool pool(1);
    Latch latch(1);
    std::atomic<int> ran {0};
    pool.post([]() { throw std::runtime_error("meant to"); });
    pool.post([]() { throw 7; });
    pool.post([&]() {
        ++ran;
        latch.done();
    });
    ASSERT_TRUE(latch.wait(30.0));
    EXPECT_EQ(ran.load(), 1);
}

TEST(ThreadPool, stopWaitsForTheTaskRunningAndDropsTheRest)
{
    Base::ThreadPool pool(1);
    Latch started(1);
    std::atomic<bool> release {false};
    std::atomic<bool> finished {false};
    std::atomic<int> others {0};
    pool.post([&]() {
        started.done();
        while (!release.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        finished = true;
    });
    for (int i = 0; i < 10; ++i) {
        pool.post([&]() { ++others; });
    }
    ASSERT_TRUE(started.wait(30.0));
    std::thread releaser([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        release = true;
    });
    pool.stop();
    EXPECT_TRUE(finished.load()) << "stop returned with a task still running";
    EXPECT_EQ(others.load(), 0) << "a task was started after the stop";
    releaser.join();

    // Stopped for good, and a second stop is nothing
    pool.post([&]() { ++others; });
    pool.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(others.load(), 0);
}

TEST(ThreadPool, theComputePoolIsOneAndHasAThread)
{
    Base::ThreadPool& pool = Base::ThreadPool::compute();
    EXPECT_EQ(&pool, &Base::ThreadPool::compute());
    EXPECT_GE(pool.size(), 1U);
    Latch latch(1);
    pool.post([&]() { latch.done(); });
    EXPECT_TRUE(latch.wait(30.0));
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
