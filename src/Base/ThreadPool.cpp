/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <utility>

#ifndef __wasi__
# include <thread>
# include <boost/asio/post.hpp>
# include <boost/asio/thread_pool.hpp>
#endif

#include "Console.h"
#include "ThreadPool.h"

using namespace Base;

namespace
{

void run(const std::function<void()>& task)
{
    try {
        task();
    }
    catch (const std::exception& e) {
        Base::Console().Warning("ThreadPool: a task threw: %s\n", e.what());
    }
    catch (...) {
        Base::Console().Warning("ThreadPool: a task threw\n");
    }
}

}  // namespace

#ifndef __wasi__

struct ThreadPool::Private
{
    explicit Private(unsigned threads)
        : count(threads)
        , pool(threads)
    {}

    unsigned count;
    boost::asio::thread_pool pool;
    std::atomic<bool> stopped {false};
};

ThreadPool::ThreadPool(unsigned threads)
{
    if (threads == 0) {
        const unsigned hardware = std::thread::hardware_concurrency();
        threads = hardware > 1 ? hardware - 1 : 1;
    }
    d = std::make_unique<Private>(threads);
}

ThreadPool::~ThreadPool()
{
    stop();
}

unsigned ThreadPool::size() const
{
    return d->count;
}

void ThreadPool::post(std::function<void()> task)
{
    if (!task || d->stopped.load(std::memory_order_acquire)) {
        return;
    }
    boost::asio::post(d->pool, [task = std::move(task)]() { run(task); });
}

void ThreadPool::stop()
{
    if (d->stopped.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    d->pool.stop();
    d->pool.join();
}

#else  // __wasi__

struct ThreadPool::Private
{
    bool stopped = false;
};

ThreadPool::ThreadPool(unsigned /*threads*/)
    : d(std::make_unique<Private>())
{}

ThreadPool::~ThreadPool() = default;

unsigned ThreadPool::size() const
{
    return 1;
}

void ThreadPool::post(std::function<void()> task)
{
    if (task && !d->stopped) {
        run(task);
    }
}

void ThreadPool::stop()
{
    d->stopped = true;
}

#endif  // __wasi__

ThreadPool& ThreadPool::compute()
{
    // Leaked on purpose, see the header.
    static ThreadPool* pool = new ThreadPool();
    return *pool;
}
