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

#ifndef BASE_THREADPOOL_H
#define BASE_THREADPOOL_H

#include <functional>
#include <memory>

#include <FCGlobal.h>

namespace Base
{

/** Worker threads that take tasks from one queue.
 *
 * A thin face on boost::asio::thread_pool, so that nobody who needs work
 * done on another thread has to write the threads, the queue and the way
 * out of the process again -- three were written by hand before this
 * (docs/DocumentLoad.md sec 18.9).
 *
 * What it is NOT, and what its users therefore keep to themselves:
 *
 *   - It has no priorities and takes nothing back. Tasks start in the order
 *     they were posted. Whoever needs an order or a cancel keeps their own
 *     queue and posts only what is to run next.
 *   - A task must not wait for another task of the same pool. Nothing here
 *     lends the waiting thread to the work it waits for, and a pool whose
 *     every thread waits is stopped for good.
 *   - It is for work that computes. A task that sleeps or waits on a socket
 *     holds a thread the others are counting on.
 *
 * In a build without threads (the WASI sandbox image) a task runs where it
 * is posted, before post() returns.
 */
class BaseExport ThreadPool
{
public:
    /** The process's pool for compute work.
     *
     * One thread for each hardware thread but one, at least one. Made on
     * first use and never destroyed: its threads may be asleep on a condition
     * variable when the process leaves, and destroying one of those under a
     * sleeper hangs the exit. Whoever posted work that may still be running
     * when the application quits waits for it there, as stop() does for a
     * pool of one's own.
     */
    static ThreadPool& compute();

    /// \a threads workers; 0 for one per hardware thread but one.
    explicit ThreadPool(unsigned threads = 0);
    /// stop().
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /// How many tasks can run at once.
    unsigned size() const;

    /** Run \a task on one of the pool's threads.
     *
     * From any thread. An exception that leaves the task is caught and
     * reported, and the thread goes on to the next task. After stop() the
     * task is dropped without being run.
     */
    void post(std::function<void()> task);

    /** No task starts after this, and the ones running are waited for.
     *
     * Tasks not yet started are dropped WITHOUT being run. Not to be called
     * from one of the pool's own tasks.
     */
    void stop();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Base

#endif  // BASE_THREADPOOL_H
