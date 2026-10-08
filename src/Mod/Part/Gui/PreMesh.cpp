/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <QCoreApplication>
#include <QMetaObject>

#include <BRepMesh_IncrementalMesh.hxx>
#include <IMeshTools_Parameters.hxx>
#include <TopExp_Explorer.hxx>
#include <Standard_Failure.hxx>

#include <Base/ThreadPool.h>
#include <Gui/RenderParams.h>
#include <Mod/Part/App/MeshTwin.h>

#include "PreMesh.h"

namespace PartGui
{

namespace
{

/// The faces and edges of \a shape, by TShape.
void partsOf(const TopoDS_Shape &shape, std::vector<const void *> &parts)
{
    for (TopExp_Explorer xp(shape, TopAbs_FACE); xp.More(); xp.Next())
        parts.push_back(xp.Current().TShape().get());
    for (TopExp_Explorer xp(shape, TopAbs_EDGE); xp.More(); xp.Next())
        parts.push_back(xp.Current().TShape().get());
}

struct Claim
{
    enum class State
    {
        /// Queued: no twin, no worker.
        Pending,
        /// Its twin is with a worker, or back and not yet taken.
        Out,
        /// Its mesh is on the shape, or it ended without one.
        Done,
    };

    /// Pins the TShape this claim is KEYED on. Without it a document
    /// closing frees that TShape, a later allocation reuses the
    /// address, and the stale claim answers -- with a bounding box --
    /// for a shape it knows nothing about. Released by
    /// clearPreMeshClaims, which the drain calls once every queue it
    /// serves is empty -- or, for a claim that was out then, when its
    /// mesh is taken.
    TopoDS_Shape shape;
    Bnd_Box geomBox;
    double deflection = 0.0;
    double angle = 0.0;
    bool skipResident = true;
    bool acceptFiner = false;
    /// The faces and edges of the shape, by TShape: what a mesh is put
    /// on, and what this claim holds in s_parts until it is done.
    std::vector<const void *> parts;
    /// The clear this claim was made after (s_generation).
    uint64_t generation = 0;
    State state = State::Pending;
    /// What the worker meshes: made when the claim is handed out, landed
    /// and dropped when it is taken back.
    std::unique_ptr<Part::MeshTwin> twin;
    /// The worker's verdict, written before it hands the claim back.
    bool failed = false;
    /// Claims that share a face or an edge with this one and came after
    /// it, while it is out: they go when this one is back.
    std::vector<std::shared_ptr<Claim>> waiters;
};
using ClaimPtr = std::shared_ptr<Claim>;

/// Everything below is the GUI thread's, with two exceptions: a worker
/// puts the claim it has meshed on s_finished, under s_mutex, and reads
/// s_stopping. The mutex is held for every access all the same -- it is
/// what orders a worker's hand-back against a waiter's test -- and never
/// across anything that touches a shape or posts a task.
std::mutex s_mutex;
/// Signalled when a worker hands a claim back, and when the last twin a
/// worker was given to free is freed: what the waits sleep on.
std::condition_variable s_handedBack;
/// The claims, keyed by TShape address. Compared, never dereferenced:
/// each claim pins its own shape (see Claim::shape).
std::unordered_map<const void *, ClaimPtr> s_claims;
/// The faces and edges of every claim that is not done, counted.
///
/// A claim is keyed on its shape's own TShape, and for a long time that
/// was all a build asked about. But a mesh goes on faces and edges, and a
/// shape that was never claimed can be made of the faces of one that is:
/// a compound over other objects, the result of a cut or a fuse, a shell
/// put together from their faces (docs/DocumentLoad.md sec 18.8).
std::unordered_map<const void *, int> s_parts;
/// The faces and edges of every claim that is OUT, to that claim: a face
/// is in one twin at a time, so that it is meshed once.
std::unordered_map<const void *, Claim *> s_outParts;
/// Claims not yet handed out, in the order they came.
std::deque<ClaimPtr> s_pending;
/// Claims the workers have handed back, to be taken by the GUI thread.
std::vector<ClaimPtr> s_finished;
/// How many claims are out, and how many faces and edges they hold.
std::size_t s_out = 0;
std::size_t s_outWeight = 0;
/// Twins handed to a worker to be freed and not freed yet.
std::size_t s_freeing = 0;
/// How many claims are not done, and how many are on s_finished: what
/// lets a build's question cost nothing when there is nothing to answer.
std::atomic<std::size_t> s_flying {0};
std::atomic<std::size_t> s_back {0};
std::size_t s_claimed = 0;
std::size_t s_meshed = 0;
std::size_t s_failed = 0;
double s_wall = 0.0;
std::chrono::steady_clock::time_point s_first;
/// Bumped by clearPreMeshClaims: a claim made before it is counted for
/// nobody, and leaves the map when it is done.
std::atomic<uint64_t> s_generation {0};
/// Set while stopPreMesh waits: a worker starts no twin it has not
/// started. One it is in is finished -- nothing interrupts the mesher.
std::atomic<bool> s_stopping {false};
/// A wake of the GUI thread is on its way.
std::atomic<bool> s_wakePosted {false};

void service();

/// How much may be out at once, so that a worker that ends does not wait
/// for the GUI thread to come round and make it a twin -- and so that the
/// twins of a whole document are not all alive, and all made, at the same
/// moment.
///
/// Counted in what the twins hold, faces and edges, and not in twins. A
/// count was the first thing tried, two for each worker: on the reference
/// assembly the median shape meshes in 17 us, seven workers were through
/// fourteen of them long before the GUI thread asked again, and the batch
/// that took 1.1 s meshing in place took 4.8 s. Measured there
/// (docs/DocumentLoad.md sec 18.9), two loads each:
///
///     faces and edges out    batch, progressive / synchronous
///                   5000      3.5-3.8 s / 1.7 s
///                  50000      2.7 s     / 1.4-1.5 s
///              unbounded      2.4 s     / 1.3-1.4 s
///
/// and unbounded bought the load nothing over 50000 -- the slice that
/// submits then makes every twin of the document before anything is
/// built, and every one of them is alive at once. (The batch times are a
/// steady clock's. The loads were timed by a wall clock that was stepping
/// that afternoon, and say no more than that.) The floor is for shapes
/// bigger than the window by themselves: every worker has one, and one
/// stands ready.
const std::size_t kWindowParts = 50000;

std::size_t windowFloor()
{
    return 2 * std::size_t(Base::ThreadPool::compute().size());
}

/// From a worker: have the GUI thread take what has come back. ONE call
/// on its way at a time, whatever the number of claims behind it: Qt
/// delivers every queued call in a single sweep of its queue.
void wake()
{
    if (s_wakePosted.exchange(true, std::memory_order_acq_rel))
        return;
    QCoreApplication *app = QCoreApplication::instance();
    if (!app) {
        // Nobody to wake: whoever asks next takes it.
        s_wakePosted.store(false, std::memory_order_release);
        return;
    }
    QMetaObject::invokeMethod(app, []() {
        s_wakePosted.store(false, std::memory_order_release);
        service();
    }, Qt::QueuedConnection);
}

/// A worker's whole task: the twin, and nothing of the document's.
void meshTwin(ClaimPtr claim)
{
    bool failed = true;
    // Asked to stop: this twin is not started, and its claim ends
    // unmeshed -- as a shape the mesher failed on does, and whoever
    // builds it meshes it.
    if (!s_stopping.load(std::memory_order_acquire)) {
        try {
            IMeshTools_Parameters params;
            params.Deflection = claim->deflection;
            params.Relative = Standard_False;
            params.Angle = claim->angle;
            // The split is across shapes here, so each shape is meshed
            // whole and single-threaded: OCCT's own per-face split
            // would only contend with the siblings already running.
            params.InParallel = Standard_False;
            // Exactly what the display build asks with, so the mesh
            // this leaves is the mesh that build would have made.
            params.AllowQualityDecrease = Standard_True;
            BRepMesh_IncrementalMesh(claim->twin->shape(), params);
            failed = false;
        }
        catch (const Standard_Failure &) {
            // A shape OCCT cannot mesh is not an error here: the
            // display build will make the same call and fail the same
            // way, which is where such a shape has always been handled.
        }
        catch (...) {
        }
    }
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        claim->failed = failed;
        // Moved: the claim, and the shape it pins, are the GUI thread's
        // again from here, and nothing of them is left on this one.
        s_finished.push_back(std::move(claim));
        s_back.fetch_add(1, std::memory_order_acq_rel);
    }
    s_handedBack.notify_all();
    wake();
}

/// End \a claim: nothing is in flight for it from here. The caller holds
/// s_mutex.
void publishLocked(ClaimPtr claim, bool meshed)
{
    const bool wasOut = claim->state == Claim::State::Out;
    claim->state = Claim::State::Done;
    for (const void *part : claim->parts) {
        if (wasOut) {
            auto out = s_outParts.find(part);
            if (out != s_outParts.end() && out->second == claim.get())
                s_outParts.erase(out);
        }
        auto it = s_parts.find(part);
        if (it != s_parts.end() && --it->second <= 0)
            s_parts.erase(it);
    }
    if (wasOut) {
        --s_out;
        s_outWeight -= std::min(s_outWeight, claim->parts.size());
    }
    s_flying.fetch_sub(1, std::memory_order_acq_rel);
    if (claim->generation == s_generation.load(std::memory_order_acquire)) {
        ++(meshed ? s_meshed : s_failed);
        s_wall = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - s_first).count();
    }
    else {
        // Made before the last clear, which had to leave it because a
        // worker had its twin. Nobody is going to ask for its result, and
        // the TShape it is keyed on is pinned only for as long as it
        // stays.
        auto it = s_claims.find(claim->shape.TShape().get());
        if (it != s_claims.end() && it->second == claim)
            s_claims.erase(it);
    }
    // Those that waited for these faces go next, in the order they came.
    std::vector<ClaimPtr> waiters;
    waiters.swap(claim->waiters);
    for (auto it = waiters.rbegin(); it != waiters.rend(); ++it) {
        if ((*it)->state != Claim::State::Pending)
            continue;
        if (s_stopping.load(std::memory_order_acquire))
            publishLocked(*it, false);
        else
            s_pending.push_front(*it);
    }
}

/// Put what a worker made on the shape, and end the claim.
void take(const ClaimPtr &claim)
{
    bool meshed = false;
    if (claim->twin) {
        if (claim->failed) {
            claim->twin->abandon();
        }
        else {
            claim->twin->land();
            meshed = true;
        }
        std::shared_ptr<Part::MeshTwin> spent(std::move(claim->twin));
        if (s_stopping.load(std::memory_order_acquire)) {
            // On the way out of the process: nothing more goes to a
            // worker, which could be freeing it while OCCT is taken down.
            spent.reset();
        }
        else {
            // The twin's memory is given back by a worker -- 0.23 s of the
            // GUI thread over the 17058 shapes of the MiSTer reference
            // otherwise (docs/DocumentLoad.md sec 18.9). What it holds of
            // the original is handles, counted atomically.
            {
                std::lock_guard<std::mutex> guard(s_mutex);
                ++s_freeing;
            }
            Base::ThreadPool::compute().post([spent = std::move(spent)]() mutable {
                spent.reset();
                {
                    std::lock_guard<std::mutex> guard(s_mutex);
                    --s_freeing;
                }
                s_handedBack.notify_all();
            });
        }
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    publishLocked(claim, meshed);
}

/// Hand out what can go: a twin for each claim whose turn it is, while
/// the window has room.
void dispatch()
{
    const std::size_t floor = windowFloor();
    for (;;) {
        std::vector<ClaimPtr> go;
        {
            std::lock_guard<std::mutex> guard(s_mutex);
            if (s_stopping.load(std::memory_order_acquire))
                return;
            while (!s_pending.empty()
                   && (s_out < floor || s_outWeight < kWindowParts)) {
                ClaimPtr claim = std::move(s_pending.front());
                s_pending.pop_front();
                if (claim->state != Claim::State::Pending)
                    continue;
                // A face is in one twin at a time. Two twins holding it
                // would mesh it twice, and the second to land would leave
                // the first one's edge polygons behind; a claim that
                // waits finds the mesh on its faces when its own twin is
                // made.
                Claim *ahead = nullptr;
                for (const void *part : claim->parts) {
                    auto it = s_outParts.find(part);
                    if (it != s_outParts.end()) {
                        ahead = it->second;
                        break;
                    }
                }
                if (ahead) {
                    ahead->waiters.push_back(std::move(claim));
                    continue;
                }
                for (const void *part : claim->parts)
                    s_outParts.emplace(part, claim.get());
                claim->state = Claim::State::Out;
                ++s_out;
                s_outWeight += claim->parts.size();
                go.push_back(std::move(claim));
            }
        }
        if (go.empty())
            return;
        bool freed = false;
        for (const ClaimPtr &claim : go) {
            bool resident = false;
            try {
                // Meshed already, for another claim that holds its faces
                // or by whoever built it in the meantime: the build
                // would not call the mesher for it, and neither does this.
                resident = claim->skipResident
                    && meshAnswersAsk(claim->shape, claim->deflection,
                                      claim->acceptFiner);
                if (!resident)
                    claim->twin = std::make_unique<Part::MeshTwin>(claim->shape);
            }
            catch (const Standard_Failure &) {
                claim->twin.reset();
            }
            catch (const std::bad_alloc &) {
                claim->twin.reset();
            }
            if (!claim->twin) {
                std::lock_guard<std::mutex> guard(s_mutex);
                publishLocked(claim, resident);
                freed = true;
                continue;
            }
            Base::ThreadPool::compute().post([claim]() mutable {
                meshTwin(std::move(claim));
            });
        }
        // A claim that needed no worker left its place in the window.
        if (!freed)
            return;
    }
}

/// Take what the workers have handed back, then hand out what can go.
void service()
{
    if (s_back.load(std::memory_order_acquire) != 0) {
        std::vector<ClaimPtr> back;
        {
            std::lock_guard<std::mutex> guard(s_mutex);
            back.swap(s_finished);
            s_back.store(0, std::memory_order_release);
        }
        for (const ClaimPtr &claim : back)
            take(claim);
    }
    dispatch();
}

/// The caller holds s_mutex.
bool flyingLocked(const void *tshape)
{
    auto it = s_claims.find(tshape);
    return it != s_claims.end() && it->second->state != Claim::State::Done;
}

/// Serve the queue until \a flying, asked under s_mutex, says no; false
/// at \a deadline.
template<typename Flying>
bool waitUntil(std::chrono::steady_clock::time_point deadline, Flying flying)
{
    for (;;) {
        service();
        std::unique_lock<std::mutex> lock(s_mutex);
        if (!flying())
            return true;
        // In flight after a service: a twin is out, and a worker will
        // hand it back.
        if (!s_handedBack.wait_until(lock, deadline,
                                     []() { return !s_finished.empty(); }))
            return false;
    }
}

std::chrono::steady_clock::time_point deadlineIn(double seconds)
{
    return std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(std::max(0.0, seconds)));
}

} // namespace

bool preMeshEnabled()
{
    return Gui::RenderParams::getPreMeshOnLoad();
}

void submitPreMesh(std::vector<PreMeshItem> &&items)
{
    if (items.empty())
        return;
    // Walked before the lock is taken: the shapes are this thread's.
    std::vector<std::vector<const void *>> parts(items.size());
    for (std::size_t index = 0; index < items.size(); ++index)
        partsOf(items[index].shape, parts[index]);
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        const uint64_t generation = s_generation.load(std::memory_order_acquire);
        for (std::size_t index = 0; index < items.size(); ++index) {
            PreMeshItem &item = items[index];
            const void *tshape = item.shape.TShape().get();
            // Claimed already and not done -- a claim a clear left out, a
            // second parking of the same shape, a second document holding
            // the same TShape: the claim it has covers it.
            if (flyingLocked(tshape))
                continue;
            // A new claim, never the old one made in flight again.
            auto claim = std::make_shared<Claim>();
            claim->shape = item.shape;
            claim->geomBox = item.geomBox;
            claim->deflection = item.deflection;
            claim->angle = item.angle;
            claim->skipResident = item.skipResident;
            claim->acceptFiner = item.acceptFiner;
            claim->parts = std::move(parts[index]);
            claim->generation = generation;
            for (const void *part : claim->parts)
                ++s_parts[part];
            s_flying.fetch_add(1, std::memory_order_acq_rel);
            if (s_claimed++ == 0)
                s_first = std::chrono::steady_clock::now();
            s_claims[tshape] = claim;
            s_pending.push_back(std::move(claim));
        }
    }
    // Before the application leaves, once: the event loop's end is ahead
    // of everything a process takes down on its way out, which no exit
    // handler registered here can be sure to be.
    static bool hooked = false;
    if (!hooked && QCoreApplication::instance()) {
        hooked = true;
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         []() { stopPreMesh(); });
    }
    service();
}

bool preMeshInFlight(const void *tshape)
{
    if (!tshape || s_flying.load(std::memory_order_acquire) == 0)
        return false;
    service();
    std::lock_guard<std::mutex> guard(s_mutex);
    return flyingLocked(tshape);
}

bool preMeshInFlight(const TopoDS_Shape &shape)
{
    if (shape.IsNull() || s_flying.load(std::memory_order_acquire) == 0)
        return false;
    service();
    if (s_flying.load(std::memory_order_acquire) == 0)
        return false;
    std::vector<const void *> parts;
    partsOf(shape, parts);
    std::lock_guard<std::mutex> guard(s_mutex);
    if (flyingLocked(shape.TShape().get()))
        return true;
    for (const void *part : parts) {
        if (s_parts.count(part))
            return true;
    }
    return false;
}

bool waitPreMesh(const TopoDS_Shape &shape, double seconds)
{
    if (shape.IsNull() || s_flying.load(std::memory_order_acquire) == 0)
        return true;
    std::vector<const void *> parts;
    partsOf(shape, parts);
    const void *tshape = shape.TShape().get();
    // Every claim done takes its faces and edges out of s_parts, so the
    // wait ends at the last of the claims this shape is made of.
    return waitUntil(deadlineIn(seconds), [tshape, &parts]() {
        if (flyingLocked(tshape))
            return true;
        for (const void *part : parts) {
            if (s_parts.count(part))
                return true;
        }
        return false;
    });
}

bool waitPreMesh(const void *tshape, double seconds)
{
    if (!tshape)
        return true;
    // A claim that is GONE answers as well as one that is done:
    // clearPreMeshClaims drops what it can, and nothing is coming for the
    // shape after it.
    return waitUntil(deadlineIn(seconds),
                     [tshape]() { return flyingLocked(tshape); });
}

bool preMeshBox(const void *tshape, Bnd_Box &box)
{
    if (!tshape || s_flying.load(std::memory_order_acquire) == 0)
        return false;
    std::lock_guard<std::mutex> guard(s_mutex);
    auto it = s_claims.find(tshape);
    if (it == s_claims.end()
            || it->second->state == Claim::State::Done
            || it->second->geomBox.IsVoid())
        return false;
    box = it->second->geomBox;
    return true;
}

void preMeshStats(std::size_t &claimed, std::size_t &meshed,
                  std::size_t &failed, double &wall)
{
    service();
    std::lock_guard<std::mutex> guard(s_mutex);
    claimed = s_claimed;
    meshed = s_meshed;
    failed = s_failed;
    wall = s_wall;
}

void stopPreMesh()
{
    s_stopping.store(true, std::memory_order_release);
    {
        // What no worker has: ended unmeshed, here.
        std::lock_guard<std::mutex> guard(s_mutex);
        std::deque<ClaimPtr> pending;
        pending.swap(s_pending);
        for (const ClaimPtr &claim : pending) {
            if (claim->state == Claim::State::Pending)
                publishLocked(claim, false);
        }
    }
    // No timeout: what is waited for is each worker's twin in hand, and
    // a process that left without waiting is the crash this is for
    // (docs/DocumentLoad.md sec 18.8). A mesh that is ready is taken: it
    // costs microseconds, and the stop is then no reason for a shape to
    // be meshed again.
    for (;;) {
        std::vector<ClaimPtr> back;
        {
            std::unique_lock<std::mutex> lock(s_mutex);
            s_handedBack.wait(lock, []() {
                return !s_finished.empty() || (s_out == 0 && s_freeing == 0);
            });
            back.swap(s_finished);
            s_back.store(0, std::memory_order_release);
        }
        if (back.empty())
            break;
        for (const ClaimPtr &claim : back)
            take(claim);
    }
    s_stopping.store(false, std::memory_order_release);
}

void clearPreMeshClaims()
{
    std::lock_guard<std::mutex> guard(s_mutex);
    // First: what ends below is then a claim made before the clear, and
    // is counted for nobody and taken out of the map.
    s_generation.fetch_add(1, std::memory_order_acq_rel);
    // A claim no worker has is dropped. "No build is going to ask again"
    // is what the callers know, and with nothing of it out there is
    // nothing to wait for.
    std::deque<ClaimPtr> pending;
    pending.swap(s_pending);
    std::vector<ClaimPtr> waiting;
    for (auto &entry : s_claims) {
        if (entry.second->state == Claim::State::Out) {
            for (ClaimPtr &waiter : entry.second->waiters)
                waiting.push_back(std::move(waiter));
            entry.second->waiters.clear();
        }
    }
    for (const ClaimPtr &claim : pending) {
        if (claim->state == Claim::State::Pending)
            publishLocked(claim, false);
    }
    for (const ClaimPtr &claim : waiting) {
        if (claim->state == Claim::State::Pending)
            publishLocked(claim, false);
    }
    // A claim that is OUT stays, and goes on answering as in flight until
    // its mesh is taken: a build that asked would otherwise mesh, on the
    // GUI thread, a shape whose mesh is a moment away. It is not an
    // address a later allocation can reuse either: the claim holds the
    // shape. A drain whose parked shapes were all built by somebody else
    // before its first slice -- an import's own finishRestoring pass did
    // that -- submitted them, popped them unbuilt and was here in the same
    // slice, with every worker still running (sec 18.7).
    for (auto it = s_claims.begin(); it != s_claims.end(); ) {
        if (it->second->state == Claim::State::Done)
            it = s_claims.erase(it);
        else
            ++it;
    }
    s_claimed = s_meshed = s_failed = 0;
    s_wall = 0.0;
    s_handedBack.notify_all();
}

} // namespace PartGui
