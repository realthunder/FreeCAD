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
 ***************************************************************************/

/// The producer half of the shape-backed level generator: registration
/// of shapes against the render feed's source tags
/// (Render::MeshSourceRegistry). The build itself is a pure function
/// of (shape, params, source chunk) — MeshLevelBuild.cpp — so the
/// registered closure is safe on any of the scene server's level
/// threads, and levels of one publish build concurrently.

#include "PreCompiled.h"

#ifndef _PreComp_
# include <BRepBndLib.hxx>
# include <Bnd_Box.hxx>
# include <Precision.hxx>
# include <Standard_Failure.hxx>
# include <TopoDS_Shape.hxx>
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QTimer>

#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/RenderParams.h>
#include <Gui/Renderer/MeshSource.h>
#include <Gui/Renderer/Renderer.h>
#include <Gui/Renderer/SceneDump.h>
#include <Gui/Renderer/SceneLadder.h>
#include <Gui/Renderer/SceneServer.h>
#include <Gui/SceneServeSource.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewParams.h>

#include "MeshLevelSource.h"

using namespace PartGui;

namespace {

/// The registered closure owns a refcounted handle of the exact shape
/// the display tessellation meshed, plus the job parameters. Const
/// after registration: level threads only read it.
struct LevelSourceState {
    TopoDS_Shape shape;
    MeshLevelJob params;
};
using LevelSourceStatePtr = std::shared_ptr<LevelSourceState>;

//////////////////////////////////////////////////////////////////////
// The desktop exact refine (docs/SceneStreaming.md §13, step 1).
//
// A coarse-first display build on a plain desktop process queues one
// job here; a worker meshes a structure copy of the shape at the
// exact display parameters and the caller's callback applies it on
// the GUI thread. Job identity is a token per tag: re-registering or
// unregistering the tag invalidates the token, which both cancels a
// queued job and drops a finished one at the door — the "cancel"
// leg of the provider seam, at the only granularity the desktop
// needs before the plan pass exists.

struct RefineJob {
    uint64_t token = 0;
    const void *tag = nullptr;
    LevelSourceStatePtr st;
    std::function<void(const TopoDS_Shape &)> apply;
    /// The decimation descent's job body (sec 13c): runs on the worker
    /// -- it owns a snapshot of the display arrays, never the nodes --
    /// and returns the landing to marshal to the GUI thread, or null
    /// when the rung refused. Set instead of st/apply.
    std::function<std::function<void()>()> work;
    /// A plan-ordered descent (the dynamic-scale re-tessellation or a
    /// decimation job): counted in the registry's in-flight tally --
    /// the downgrade ledger holds its credit open on it -- and queued
    /// AHEAD of climbs: under the pressure that ordered it, freeing
    /// memory outranks spending more (the climb hard gate refuses new
    /// climbs anyway, but jobs already queued should not starve it).
    bool descent = false;
    /// The sweep order this job descends for (0 = none): inherited
    /// from the thread-current generation at enqueue, re-entered
    /// around the landing so chained enqueues inherit it too. What
    /// the downgrade ledger's write-off horizon actually rides.
    uint64_t gen = 0;
};

/// The mutex and the condition variable are LEAKED on purpose: a
/// worker waits on them, and glibc's pthread_cond_destroy blocks until
/// every waiter has left -- destroying them as statics with a worker
/// still parked hung the process in exit() (found 2026-09-06 under
/// gdb, the sandbox's corpus gate). The orderly way out is
/// shutdownMeshLevelWorkers(), hooked to the application's quit the
/// first time a worker starts; the leak covers an exit that never ran
/// the hook (no event loop, a script's exit()).
std::mutex &s_refineMutex = *new std::mutex;
std::condition_variable &s_refineCv = *new std::condition_variable;
std::deque<RefineJob> s_refineQueue;
/// tag -> the one live token; absent = nothing wanted (canceled).
std::map<const void *, uint64_t> s_refineTokens;
uint64_t s_refineCounter = 0;
/// The refine workers, joinable, joined by shutdownMeshLevelWorkers();
/// leaked like the primitives so a static destruction that arrives
/// without the shutdown does not std::terminate on a joinable thread.
std::vector<std::thread> &s_refineWorkers = *new std::vector<std::thread>;
/// Set once by the shutdown, under s_refineMutex: the workers leave
/// their wait and return, and nothing enqueues after it.
bool s_refineStop = false;

/// Same sizing rule as the scene server's level threads: modest by
/// default, because BRepMesh already parallelizes each build over
/// OCCT's thread pool; the LevelThreads render parameter sets it,
/// FC_LEVEL_THREADS overrides for the process.
int refineThreadCap()
{
    static const int envCap = [] {
        const char *env = std::getenv("FC_LEVEL_THREADS");
        return env ? std::atoi(env) : 0;
    }();
    int n = envCap > 0 ? envCap : int(Gui::RenderParams::getLevelThreads());
    if (n > 0)
        return std::min(n, 64);
    unsigned hw = std::thread::hardware_concurrency();
    return int(std::max(1u, std::min(4u, hw / 4)));
}

/// The CPU-memory floor an exact build must not start under (§13
/// step 3): available system memory below this counts as a ceiling
/// observation without waiting for the bad_alloc. The
/// LevelMemoryFloorMB render parameter sets it; 0 sizes it
/// automatically — read once, on the GUI thread, when the first
/// refine is queued (the LevelThreads pattern).
size_t s_memFloorBytes = 0;

void resolveMemFloor()
{
    if (s_memFloorBytes)
        return;
    long mb = long(Gui::RenderParams::getLevelMemoryFloorMB());
    if (mb > 0) {
        s_memFloorBytes = size_t(mb) << 20;
        return;
    }
    const size_t total = Render::MemoryBudget::systemMemory();
    s_memFloorBytes =
        std::max(size_t(512) << 20, total ? total / 16 : size_t(0));
}

/// FC_DEBUG_MESH_CEILING=<n>: treat the n-th exact refine build as an
/// allocation failure — the only way to exercise the demotion path
/// without actually running the machine out of memory.
bool debugCeilingHit()
{
    static const long at = [] {
        const char *env = std::getenv("FC_DEBUG_MESH_CEILING");
        return env ? std::atol(env) : 0;
    }();
    if (at <= 0)
        return false;
    static std::atomic<long> builds {0};
    return ++builds == at;
}

/// Every descent job settles its in-flight count exactly once, on
/// whichever exit it takes -- the ledger's write-off horizon rides it.
void settleDescent(const RefineJob &job)
{
    if (job.descent)
        Render::MeshSourceRegistry::instance().noteDescentSettled(job.gen);
}

//////////////////////////////////////////////////////////////////////
// The landing pump. A worker marshals its finished job to the GUI
// thread with a queued invocation -- and Qt delivers EVERY pending
// queued event in one sendPostedEvents sweep, so a batch of landings
// (ClimbAdmitBatch refines, a DescentOrderBatch of coarsenings) ran
// back-to-back inside a single event-loop turn: measured 1-2.7s
// stretches in which no timer, paint or input event was served. The
// pump gives each turn a time budget instead: landings queue here, a
// zero-timer runs as many as the budget allows, and everything else
// the loop owes gets its turn between reschedules.
//
// GUI thread only, all of it -- the workers reach it through one
// queued hop that does nothing but enqueue.

/// A worker landing and the sweep order it descends for: the pump
/// re-enters the generation while running it, so what the landing
/// queues in turn (a pooled fill, a deferred rebuild) counts under
/// the same order.
struct LandingItem {
    std::function<void()> fn;
    uint64_t gen = 0;
};
std::deque<LandingItem> s_landingQueue;

/// Spent pump items go to a reaper thread instead of destructing in
/// the turn: a landing's closure owns the worker's payload -- the
/// meshed TopoShape copy of an exact climb chief among it -- and
/// freeing those measured 0.1-0.2s of a pump window on the GUI
/// thread (the "frees" split in the pump line). Everything these
/// closures own is safe to destroy off-thread: OCCT handles carry
/// atomic refcounts, Coin nodes appear only as raw unowned pointers,
/// and the detached fill arrays are plain memory.
/// Leaked, joined and stopped like the refine pool (see s_refineMutex).
std::mutex &s_reaperMutex = *new std::mutex;
std::condition_variable &s_reaperCv = *new std::condition_variable;
std::deque<std::function<void()>> s_reaperQueue;
std::thread &s_reaperThread = *new std::thread;
bool s_reaperStop = false;

void reapOffThread(std::function<void()> &&fn)
{
    if (!fn)
        return;
    {
        std::lock_guard<std::mutex> lock(s_reaperMutex);
        if (s_reaperStop) {
            // Shut down: destroy in place, there is no thread to hand
            // it to any more.
            fn = nullptr;
            return;
        }
        s_reaperQueue.push_back(std::move(fn));
        if (!s_reaperThread.joinable()) {
            s_reaperThread = std::thread([]() {
                for (;;) {
                    std::deque<std::function<void()>> batch;
                    {
                        std::unique_lock<std::mutex> lock(s_reaperMutex);
                        s_reaperCv.wait(lock, [] {
                            return s_reaperStop || !s_reaperQueue.empty();
                        });
                        if (s_reaperQueue.empty())
                            return; // stopped, and drained
                        batch.swap(s_reaperQueue);
                    }
                    // The destructions run here, unlocked.
                    batch.clear();
                }
            });
        }
    }
    s_reaperCv.notify_one();
}
/// Plan-ordered hook bodies deferred out of the plan callback (the
/// pacing's other half): a sweep fires up to a batch of hooks in ONE
/// callback, and running their bodies there -- a snapshot each for the
/// decimation descents, a resident-rung rebuild each for the
/// exact-resident downgrades -- measured 0.6-1.3s bursts. Keyed by the
/// source's primary tag so an unregistration (the view provider's
/// destructor among them) can purge what must not run on a dead owner.
/// Each queued item counts in the registry's descent tally from
/// enqueue to run/purge -- the ledger's horizon covers the deferral.
struct GuiWorkItem {
    const void *tag = nullptr;
    std::function<void()> body;
    /// Whether the item counts in the registry's descent tally (a
    /// paced climb body does not -- see queueLevelGuiWork).
    bool descent = true;
    /// The sweep order this body descends for (see RefineJob::gen).
    uint64_t gen = 0;
};
std::deque<GuiWorkItem> s_guiWork;
bool s_landingScheduled = false;

void pumpLandings();

void scheduleLandingPump()
{
    if (s_landingScheduled)
        return;
    s_landingScheduled = true;
    QTimer::singleShot(0, QCoreApplication::instance(),
                       []() { pumpLandings(); });
}

/// What the landing pump actually spends, split by what it ran
/// (LevelDebug narration). The <200ms interactivity gate fails on this
/// pump's turns, and the visual-build split only covers the updateVisual
/// inside the bodies -- without this line the difference between "the
/// pump's items are the stall" and "the stall is somewhere else
/// entirely" is not measurable. Reported like VisualSplitReporter: a
/// cumulative delta line once 0.2s of pump time has accumulated, plus
/// the worst single turn in the window, which is the number the gate's
/// worst gap must be compared against.
struct PumpAccount {
    double landSec = 0, bodySec = 0, worstTurn = 0;
    /// The worst single item this window: a turn near budget plus one
    /// oversized atom is the shape the gate fails on, and without this
    /// number "the items are too big" and "too many items ran" read
    /// the same.
    double worstItem = 0;
    /// What handing spent items to the reaper still costs the turn
    /// (the destruction itself runs off-thread now; this measured
    /// 0.1-0.2s per window when the frees ran inline).
    double freeSec = 0;
    std::size_t turns = 0, landings = 0, bodies = 0;
};
static PumpAccount s_pumpAccount;

/// Whether the level plan is narrating (same rule as the visual-build
/// split in ViewProviderExt.cpp): the global parameter or the
/// FC_LEVEL_DEBUG environment.
static bool pumpDebugOn()
{
    static const bool env = std::getenv("FC_LEVEL_DEBUG") != nullptr;
    return env || Gui::RenderParams::getLevelDebug();
}

bool s_inLandingPump = false;

void pumpLandings()
{
    s_landingScheduled = false;
    Base::StateLocker pumping(s_inLandingPump);
    const double budget =
        std::max(1L, Gui::RenderParams::getLevelLandBudgetMS()) / 1000.0;
    const auto start = std::chrono::steady_clock::now();
    auto now = []() { return std::chrono::steady_clock::now(); };
    auto since = [](std::chrono::steady_clock::time_point t0,
                    std::chrono::steady_clock::time_point t1) {
        return std::chrono::duration<double>(t1 - t0).count();
    };
    auto spent = [&start, budget, &now, &since]() {
        return since(start, now()) >= budget;
    };
    PumpAccount &acc = s_pumpAccount;
    // Landings first: they free memory and re-arm sources; the hook
    // bodies behind them typically queue MORE work.
    while (!s_landingQueue.empty()) {
        auto item = std::move(s_landingQueue.front());
        s_landingQueue.pop_front();
        auto t0 = now();
        {
            // The landing's chained enqueues (a pooled fill, a
            // deferred rebuild) inherit its sweep order -- the
            // downgrade ledger's horizon rides the whole chain.
            Render::MeshSourceRegistry::DescentGenScope scope(item.gen);
            item.fn();
        }
        const double d = since(t0, now());
        acc.landSec += d;
        acc.worstItem = std::max(acc.worstItem, d);
        ++acc.landings;
        auto f0 = now();
        reapOffThread(std::move(item.fn));
        acc.freeSec += since(f0, now());
        if (spent())
            break;
    }
    // At least one hook body per turn even when the budget went to
    // landings: a steady landing stream must not starve the orders
    // that free memory.
    bool ranGui = false;
    while (!s_guiWork.empty() && (!ranGui || !spent())) {
        auto item = std::move(s_guiWork.front());
        s_guiWork.pop_front();
        auto t0 = now();
        {
            Render::MeshSourceRegistry::DescentGenScope scope(item.gen);
            item.body();
        }
        const double d = since(t0, now());
        acc.bodySec += d;
        acc.worstItem = std::max(acc.worstItem, d);
        ++acc.bodies;
        if (item.descent)
            Render::MeshSourceRegistry::instance().noteDescentSettled(
                item.gen);
        auto f0 = now();
        reapOffThread(std::move(item.body));
        acc.freeSec += since(f0, now());
        ranGui = true;
    }
    ++acc.turns;
    acc.worstTurn = std::max(acc.worstTurn, since(start, now()));
    const double total = acc.landSec + acc.bodySec;
    if (total >= 0.2 && pumpDebugOn()) {
        Base::Console().Message(
            "landing pump: %zu turns spent %.3fs = landings %.3fs in %zu "
            "+ hook bodies %.3fs in %zu + frees %.3fs; worst turn %.0fms, "
            "worst item %.0fms (budget %.0fms)\n",
            acc.turns, total, acc.landSec, acc.landings, acc.bodySec,
            acc.bodies, acc.freeSec, acc.worstTurn * 1000.0,
            acc.worstItem * 1000.0, budget * 1000.0);
        acc = PumpAccount{};
    }
    if (!s_landingQueue.empty() || !s_guiWork.empty())
        scheduleLandingPump();
}

/// Purge every deferred hook body queued under \a tag (the tag is
/// being unregistered or re-registered); their in-flight counts settle
/// here, unrun. GUI thread.
void purgeLevelGuiWork(const void *tag)
{
    if (!tag || s_guiWork.empty())
        return;
    auto it = s_guiWork.begin();
    while (it != s_guiWork.end()) {
        if (it->tag == tag) {
            const bool counted = it->descent;
            const uint64_t gen = it->gen;
            it = s_guiWork.erase(it);
            if (counted)
                Render::MeshSourceRegistry::instance().noteDescentSettled(
                    gen);
        }
        else {
            ++it;
        }
    }
}

/// Marshal \a fn from a worker to the paced GUI queue; \a gen is the
/// sweep order the landing belongs to (RefineJob::gen).
void queueLandingFromWorker(std::function<void()> fn, uint64_t gen = 0)
{
    auto payload = std::make_shared<std::function<void()>>(std::move(fn));
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [payload, gen]() {
            s_landingQueue.push_back({std::move(*payload), gen});
            scheduleLandingPump();
        },
        Qt::QueuedConnection);
}

void refineLoop()
{
    for (;;) {
        RefineJob job;
        {
            std::unique_lock<std::mutex> lock(s_refineMutex);
            s_refineCv.wait(
                lock, [] { return s_refineStop || !s_refineQueue.empty(); });
            if (s_refineStop)
                return;
            job = std::move(s_refineQueue.front());
            s_refineQueue.pop_front();
            auto it = s_refineTokens.find(job.tag);
            if (it == s_refineTokens.end() || it->second != job.token) {
                // canceled while queued
                settleDescent(job);
                continue;
            }
        }
        if (job.work) {
            // A decimation job clusters arrays it owns: no OCCT, no
            // fresh tessellation, and NO memory-floor refusal -- it
            // frees memory, and pressure is exactly when it runs.
            std::function<void()> landing = job.work();
            if (!landing) {
                settleDescent(job);
                continue;
            }
            auto payload = std::make_shared<
                std::pair<RefineJob, std::function<void()>>>(
                std::move(job), std::move(landing));
            const uint64_t gen = payload->first.gen;
            queueLandingFromWorker([payload]() {
                settleDescent(payload->first);
                {
                    std::lock_guard<std::mutex> lock(s_refineMutex);
                    auto it = s_refineTokens.find(payload->first.tag);
                    if (it == s_refineTokens.end()
                        || it->second != payload->first.token)
                        return;
                    s_refineTokens.erase(it);
                }
                payload->second();
            }, gen);
            continue;
        }
        // Pre-build ceiling estimate: a build started under a low
        // MemAvailable is a bad_alloc that has not happened yet — and
        // by the time it does, it may be somebody else's. The job is
        // dropped (its ask stands, so it is not retried into the same
        // wall); the observation flips the plans to demoting.
        // The simulation knob (the LevelCeilingSimulateMB parameter):
        // raise
        // the floor above whatever the machine actually has free, and
        // every exact build is refused exactly as it would be on a
        // machine that had run out -- which is the only way to exercise
        // this half of the plan on a box with memory to spare, and the
        // premise of the whole coarse-first design is a model that does
        // not fit. Read per job, not once, so it can be turned on
        // against a running viewer.
        size_t floor = s_memFloorBytes;
        if (const long simMB = Gui::RenderParams::getLevelCeilingSimulateMB())
            floor = std::max(floor, size_t(simMB) << 20);
        const size_t avail = Render::MemoryBudget::availableMemory();
        if (avail && avail < floor) {
            // The shortfall travels with the observation: it is the
            // only place that knows both numbers, and it is what lets
            // the level plan buy memory back with visible error --
            // exactly as much as the floor is missing and no more.
            Render::MeshSourceRegistry::instance().observeMemoryCeiling(
                floor - avail);
            settleDescent(job);
            continue;
        }
        bool outOfMemory = false;
        TopoDS_Shape meshed = PartGui::meshLevelExactCopy(
            job.st->shape, job.st->params.exactDeflection,
            job.st->params.exactAngle, &outOfMemory);
        if (debugCeilingHit()) {
            outOfMemory = true;
            meshed.Nullify();
        }
        if (outOfMemory) {
            // A bad_alloc states only "no", never how much: what it
            // costs to make the next build fit is exactly the number
            // nobody has. So the shortfall is re-read here rather than
            // invented -- normally the allocation failed because the
            // system is under the floor, and that gap is the ask; when
            // it is not (one outsized build on a machine with room),
            // 0 says so, and the plan keeps to what the camera cannot
            // see.
            const size_t now = Render::MemoryBudget::availableMemory();
            Render::MeshSourceRegistry::instance().observeMemoryCeiling(
                now && now < floor ? floor - now : 0);
            settleDescent(job);
            continue;
        }
        if (meshed.IsNull()) {
            settleDescent(job);
            continue;
        }
        // The apply reads and writes live document geometry and Coin
        // nodes: GUI thread only. The token is re-checked there — the
        // marshalled hop is one more window for a cancellation.
        auto payload = std::make_shared<std::pair<RefineJob, TopoDS_Shape>>(
            std::move(job), std::move(meshed));
        const uint64_t gen = payload->first.gen;
        queueLandingFromWorker([payload]() {
            settleDescent(payload->first);
            {
                std::lock_guard<std::mutex> lock(s_refineMutex);
                auto it = s_refineTokens.find(payload->first.tag);
                if (it == s_refineTokens.end()
                    || it->second != payload->first.token)
                    return;
                // Consumed: the callback re-registers (at error 0),
                // but should that not happen, a second fire is not
                // an option either.
                s_refineTokens.erase(it);
            }
            payload->first.apply(payload->second);
        }, gen);
    }
}

/// Arm shutdownMeshLevelWorkers() for the application's exit: on
/// aboutToQuit (the event loop returning) and again as a post routine
/// (the QCoreApplication's destruction, for an exit that never ran the
/// loop out). Both before the statics go. GUI thread, once, when the
/// first worker starts -- the resolveMemFloor pattern.
void hookWorkerShutdown()
{
    static bool hooked = false;
    if (hooked)
        return;
    hooked = true;
    if (auto *app = QCoreApplication::instance())
        QObject::connect(app, &QCoreApplication::aboutToQuit,
                         shutdownMeshLevelWorkers);
    qAddPostRoutine(shutdownMeshLevelWorkers);
}

void enqueueLevelJob(RefineJob &&job)
{
    resolveMemFloor();
    if (job.descent) {
        job.gen =
            Render::MeshSourceRegistry::currentDescentGeneration();
        Render::MeshSourceRegistry::instance().noteDescentQueued(job.gen);
    }
    std::lock_guard<std::mutex> lock(s_refineMutex);
    if (s_refineStop) {
        // Shutting down: the job is dropped, its descent settled.
        settleDescent(job);
        return;
    }
    job.token = ++s_refineCounter;
    s_refineTokens[job.tag] = job.token;
    if (job.descent) {
        // Ahead of the climbs, behind descents already queued: under
        // the pressure that ordered it, freeing memory outranks
        // spending more, and the plan's own order is kept among peers.
        auto it = s_refineQueue.begin();
        while (it != s_refineQueue.end() && it->descent)
            ++it;
        s_refineQueue.insert(it, std::move(job));
    }
    else {
        s_refineQueue.push_back(std::move(job));
    }
    if (int(s_refineWorkers.size()) < refineThreadCap()) {
        hookWorkerShutdown();
        s_refineWorkers.emplace_back(refineLoop);
    }
    s_refineCv.notify_one();
}

void queueExactRefine(const void *tag, const LevelSourceStatePtr &st,
                      std::function<void(const TopoDS_Shape &)> apply,
                      bool descent = false)
{
    RefineJob job;
    job.tag = tag;
    job.st = st;
    job.apply = std::move(apply);
    job.descent = descent;
    enqueueLevelJob(std::move(job));
}

void cancelExactRefine(const void *tag)
{
    // The worker token ONLY. The deferred hook bodies are NOT purged
    // here: this cancel is also the climb's de-want leg, fired blindly
    // by every plan over the coarse sources it no longer wants refined
    // -- and purging then silently deleted the descent orders the SAME
    // plan had just queued (measured: 1657 downgrade orders, 367
    // landed effects, the ladder stuck 37MB over its budget with the
    // pressure tolerance blown to 2128px). The bodies die with their
    // OWNER instead: register/unregister purge explicitly.
    if (!tag)
        return;
    std::lock_guard<std::mutex> lock(s_refineMutex);
    s_refineTokens.erase(tag);
}

} // anonymous namespace

void PartGui::shutdownMeshLevelWorkers()
{
    // The refine pool: queued jobs are dropped (each descent settled
    // exactly once, as on every other exit), the workers told to leave
    // their wait and joined. A build in flight finishes first -- a
    // worker inside BRepMesh has no safe interruption point -- and its
    // landing is posted to an application that no longer runs a loop,
    // which is fine: the queued event dies with the application, and
    // the payload it owns (a meshed TopoShape copy) with it.
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(s_refineMutex);
        s_refineStop = true;
        for (const auto &job : s_refineQueue)
            settleDescent(job);
        s_refineQueue.clear();
        s_refineTokens.clear();
        workers.swap(s_refineWorkers);
    }
    s_refineCv.notify_all();
    for (auto &worker : workers) {
        if (worker.joinable())
            worker.join();
    }
    // The reaper: drained, then joined.
    std::thread reaper;
    {
        std::lock_guard<std::mutex> lock(s_reaperMutex);
        s_reaperStop = true;
        reaper.swap(s_reaperThread);
    }
    s_reaperCv.notify_all();
    if (reaper.joinable())
        reaper.join();
    if (!workers.empty())
        Base::Console().Log("MeshLevelSource: joined %d level worker(s)\n",
                            int(workers.size()));
}

namespace {

/// Is \a doc's scene being served?
///
/// ⚠️ Not "was FC_BGFX_SERVE_SCENE set", and not "is the listener up"
/// either. The env var is one of two ways to serve — it still counts
/// on its own, process-wide, because it names a port the renderer has
/// not necessarily bound yet: a document can be loaded, and
/// tessellated, before any frame happens. Gui.serveDocument is the
/// other way, and for it the question is per document, asked of the
/// source (docs/MultiDocServe.md §5): a listener can be running with
/// no publisher behind it — a failed serve must not flip this gate —
/// and serving one document says nothing about another's views. Null
/// \a doc asks whether *any* document is served.
bool sceneServed(App::Document *doc)
{
    static const bool byEnv = [] {
        const char *env = std::getenv("FC_BGFX_SERVE_SCENE");
        return env && *env;
    }();
    if (byEnv)
        return true;
    if (doc)
        return Gui::SceneServeSource::serving(doc);
    return Gui::SceneServeSource::renderProperties() != nullptr;
}

/// The render properties in force for \a doc: its serving source's
/// container when the document is served — the publisher that owns the
/// stream owns its container (docs/MultiDocServe.md §5) — else the
/// active 3D view's, else the first-served source's (the headless
/// process with no view at all, docs/HeadlessServe.md §3.3).
App::PropertyContainer *renderOverrides(App::Document *doc)
{
    if (doc) {
        if (auto *props = Gui::SceneServeSource::renderProperties(doc))
            return props;
    }
    if (auto *view = qobject_cast<Gui::View3DInventor *>(
            Gui::Application::Instance->activeView()))
        return view;
    return Gui::SceneServeSource::renderProperties();
}

/// How many rungs the ladder declares (Render_LevelCount). Clamped to
/// at least one: a ladder with no rungs is the pre-ladder behaviour,
/// which -1 already says, and a zero here would silently disable
/// coarse-first for every shape instead.
unsigned ladderRungs()
{
    const long n = Gui::RenderParams::getLevelCount();
    return unsigned(std::max(1L, std::min(n, 16L)));
}

} // anonymous namespace

int PartGui::coarseTessellationLevel(App::Document *doc)
{
    // The environment variable is the whole-process override — set, it
    // decides for every view and any value outside the ladder means
    // "exact", so a recipe can also force the feature off with -1.
    static const bool haveEnv = [] {
        const char *env = std::getenv("FC_COARSE_TESSELLATION");
        return env && *env;
    }();
    if (haveEnv) {
        static const int level = [] {
            int lvl = std::atoi(std::getenv("FC_COARSE_TESSELLATION"));
            return lvl >= 0 && unsigned(lvl) < ladderRungs() ? lvl : -1;
        }();
        return level;
    }
    // Coarse-first tessellation only pays off where something can
    // deliver the exact rung on demand: a scene stream server (its
    // viewers ask), or a desktop view whose render backend runs the
    // level plan pass — the plan fires the refine worker when the
    // camera settles on a source erring too much (§13 step 2). Plain
    // Coin display and a backend that answers drivesMeshLevels()
    // false have neither, and a coarse build there would simply stay
    // coarse forever.
    if (!sceneServed(doc)) {
        if (Gui::ViewParams::getRenderCache() != 3)
            return -1;
        auto *view3d = qobject_cast<Gui::View3DInventor *>(
            Gui::Application::Instance->activeView());
        auto *viewer = view3d ? view3d->getViewer() : nullptr;
        auto *renderer = viewer ? viewer->getExternalRenderer() : nullptr;
        if (!renderer || !renderer->drivesMeshLevels())
            return -1;
    }
    // The per-view Render_CoarseTessellation property overrides the
    // global parameter, like every other render parameter — read off
    // whichever container holds this process's render settings, since
    // a headless serving process has them without a view.
    long lvl = Gui::RenderParams::getCoarseTessellation();
    if (auto *container = renderOverrides(doc)) {
        if (auto *prop = dynamic_cast<App::PropertyInteger *>(
                container->getPropertyByName("Render_CoarseTessellation")))
            lvl = prop->getValue();
    }
    return lvl >= 0 && unsigned(lvl) < ladderRungs() ? int(lvl) : -1;
}

void PartGui::queueMeshLevelBuild(const void *tag,
                                 const TopoDS_Shape &shape,
                                 double deflection, double angle,
                                 std::function<void(const TopoDS_Shape &)>
                                     apply)
{
    if (shape.IsNull() || !tag || !apply || !(deflection > 0.0))
        return;
    // A state of its own, so the parameters travel with the job rather
    // than being read off the registration -- the descent asks for a
    // deflection the registration knows nothing about.
    auto st = std::make_shared<LevelSourceState>();
    st->shape = shape;
    st->params.exactDeflection = deflection;
    st->params.exactAngle = angle;
    queueExactRefine(tag, st, std::move(apply), /*descent*/ true);
}

void PartGui::queueMeshDescentWork(const void *tag,
                                   std::function<std::function<void()>()>
                                       work)
{
    if (!tag || !work)
        return;
    RefineJob job;
    job.tag = tag;
    job.work = std::move(work);
    job.descent = true;
    enqueueLevelJob(std::move(job));
}

void PartGui::cancelMeshLevelWork(const void *tag)
{
    cancelExactRefine(tag);
}

void PartGui::queueLevelGuiWork(const void *tag, std::function<void()> body,
                                bool descent)
{
    if (!body)
        return;
    uint64_t gen = 0;
    if (descent) {
        gen = Render::MeshSourceRegistry::currentDescentGeneration();
        Render::MeshSourceRegistry::instance().noteDescentQueued(gen);
    }
    s_guiWork.push_back({tag, std::move(body), descent, gen});
    scheduleLandingPump();
}

bool PartGui::inLandingPump()
{
    return s_inLandingPump;
}

void PartGui::registerMeshLevelSource(const TopoDS_Shape &shape,
                                      bool normalsFromUV, SoNode *faceTag,
                                      SoNode *lineTag, float builtError,
                                      double exactDeflection,
                                      double exactAngle,
                                      std::function<void(const TopoDS_Shape &)>
                                          onExactBuilt,
                                      std::function<void()> onDemote,
                                      float demoteError,
                                      std::function<void()> onDowngrade,
                                      App::Document *doc,
                                      const char *origin,
                                      std::function<void()> onScaleDown,
                                      float scaledError)
{
    if (shape.IsNull() || (!faceTag && !lineTag))
        return;
    // Re-registration replaces the source, so whatever refine was in
    // flight for the previous one is now building the wrong shape --
    // and the deferred hook bodies of the previous registration go
    // with it (they capture the owner's state as it was).
    cancelExactRefine(faceTag ? faceTag : lineTag);
    purgeLevelGuiWork(faceTag ? static_cast<const void *>(faceTag)
                              : static_cast<const void *>(lineTag));
    // A degenerate shape cannot ladder; checked here so a registered
    // source always means "levels can be built".
    try {
        Bnd_Box bounds;
        BRepBndLib::Add(shape, bounds);
        bounds.SetGap(0.0);
        if (bounds.IsVoid())
            return;
        Standard_Real x0, y0, z0, x1, y1, z1;
        bounds.Get(x0, y0, z0, x1, y1, z1);
        double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
        if (!(std::sqrt(dx * dx + dy * dy + dz * dz) > 0))
            return;
    }
    catch (const Standard_Failure &) {
        return;
    }

    auto st = std::make_shared<LevelSourceState>();
    st->shape = shape;
    st->params.normalsFromUV = normalsFromUV;
    st->params.exactDeflection = exactDeflection;
    st->params.exactAngle = exactAngle;

    auto gen = [st](uint32_t level, const void *chunk, size_t size,
                    std::vector<uint8_t> &out) {
        MeshLevelJob job = st->params;
        job.level = level;
        return buildMeshLevel(st->shape, job, chunk, size, out);
    };
    // The desktop tier's climb back to exact (§13): armed — not run —
    // when the build itself was coarse, and never on a serving process:
    // there the viewers' cameras decide whether the exact rung is worth
    // building at all, and a local refine would republish every mesh
    // at error 0 out from under the streamed ladder. The level plan
    // pass fires it (MeshSourceRegistry::requestRefine) when the
    // camera settles on the source erring more than the tolerance
    // (planMeshRefines), and retracts it (cancelRefine) when a later
    // plan stops wanting a job that has not built — the fired flag,
    // shared by the face and line sources, keeps the pair to a single
    // build and its reset re-arms the pair as one. A face and line
    // draw of the same object share bounds and error, so a plan wants
    // or drops them together; the shared job relies on that.
    Render::MeshSourceRegistry::LevelHooks hooks;
    if (onExactBuilt && builtError > 0.0f && !sceneServed(doc)) {
        const void *primary = faceTag ? faceTag : lineTag;
        auto fired = std::make_shared<std::atomic<bool>>(false);
        // The climb goes through the worker — unless a finer rung is
        // still resident (a downgraded source): then the apply needs
        // no tessellation at all -- transferMeshLevels reads same-shape
        // as "activate the finest resident rung". It is still a full
        // GUI rebuild, and a plan admits up to a whole climb batch in
        // one callback, so the body is paced through the landing pump
        // rather than run in place (a batch of these was the one
        // rebuild path left outside the pump: measured as a 4.2s
        // event-loop turn against the pump's worst 1.7s). The fired
        // flag doubles as the cancel -- a de-want resets it and the
        // queued body declines to run -- and the item is not a descent:
        // the settle tally feeds the downgrade ledger.
        hooks.refine = [primary, st, fired,
                        apply = std::move(onExactBuilt)]() {
            if (fired->exchange(true))
                return;
            if (meshLevelFinerResident(st->shape)) {
                queueLevelGuiWork(
                    primary,
                    [fired, st, apply]() {
                        if (!fired->load())
                            return;
                        apply(st->shape);
                    },
                    /*descent*/ false);
                return;
            }
            queueExactRefine(primary, st, apply);
        };
        hooks.cancelRefine = [primary, fired]() {
            if (!fired->exchange(false))
                return;
            cancelExactRefine(primary);
        };
        // A downgraded source still holds its exact rung in CPU RAM;
        // a CPU memory ceiling drops that hidden rung outright
        // (dropHiddenLevels) — nothing displayed changes, and the
        // next climb goes back through the worker. Idempotent, so no
        // fired flag: the second tag's drop finds one rung and stops.
        if (meshLevelFinerResident(shape)) {
            hooks.demote = [st]() { demoteMeshLevels(st->shape); };
            hooks.fallbackError = builtError;
            // The one demote that really is free: the finer rung it
            // drops is not the one being displayed.
            hooks.demoteDropsHiddenRung = true;
        }
        // The dynamic-scale descent (sec 13): a source already showing its
        // coarse rung is not out of moves. Where a step coarser exists
        // it arms BOTH ways down at that step's error, because a
        // coarser display mesh gives back CPU RAM and upload bytes
        // alike -- unlike the exact/coarse pair above, where the two
        // directions free different things. A shared flag keeps the
        // pair to one action; the rebuild re-registers and arms the
        // step after it, which is what lets the plan keep descending
        // the same object until the model fits.
        //
        // It does not overwrite a demote armed just above: that one
        // drops a hidden exact rung for free, so it is strictly the
        // better move and the plan should spend it first.
        if (onScaleDown && scaledError > 0.0f) {
            auto fired = std::make_shared<std::atomic<bool>>(false);
            auto once = [fired, apply = std::move(onScaleDown)]() {
                if (fired->exchange(true))
                    return;
                apply();
            };
            if (!hooks.demote) {
                hooks.demote = once;
                hooks.fallbackError = scaledError;
            }
            hooks.downgrade = once;
            hooks.downgradeFallbackError = scaledError;
        }
    }
    else if (builtError <= 0.0f && demoteError > 0.0f) {
        // Exact-resident: the two ways back down (§13 step 3), one
        // shared closure per direction under both tags, a shared flag
        // keeping each pair to a single action.
        if (onDemote) {
            auto fired = std::make_shared<std::atomic<bool>>(false);
            hooks.demote = [fired, apply = std::move(onDemote)]() {
                if (fired->exchange(true))
                    return;
                apply();
            };
        }
        if (onDowngrade) {
            auto fired = std::make_shared<std::atomic<bool>>(false);
            hooks.downgrade = [fired, apply = std::move(onDowngrade)]() {
                if (fired->exchange(true))
                    return;
                apply();
            };
        }
        hooks.fallbackError = demoteError;
    }
    // The ways DOWN are paced: a plan sweep fires up to a whole batch
    // of these in one callback, and the bodies -- a snapshot each for
    // the decimation descents, a resident-rung rebuild each for the
    // exact-resident drops -- measured 0.6-1.3s of one event-loop turn
    // when run in place. The hook itself becomes an enqueue onto the
    // landing pump; the primary tag keys the purge that runs before
    // the owner may die. The climb (refine) is not wrapped: it already
    // only queues a worker job.
    const void *primaryTag = faceTag ? static_cast<const void *>(faceTag)
                                     : static_cast<const void *>(lineTag);
    auto pace = [primaryTag](std::function<void()> fn)
        -> std::function<void()> {
        if (!fn)
            return fn;
        return [primaryTag, fn = std::move(fn)]() {
            queueLevelGuiWork(primaryTag, fn);
        };
    };
    hooks.demote = pace(std::move(hooks.demote));
    hooks.downgrade = pace(std::move(hooks.downgrade));

    auto &reg = Render::MeshSourceRegistry::instance();
    if (faceTag)
        reg.add(faceTag, gen, builtError, hooks, origin);
    if (lineTag)
        reg.add(lineTag, gen, builtError, hooks, origin);
}

void PartGui::unregisterMeshLevelSource(SoNode *faceTag, SoNode *lineTag)
{
    cancelExactRefine(faceTag ? faceTag : lineTag);
    // Before the owner may die: the deferred bodies capture it.
    purgeLevelGuiWork(faceTag ? static_cast<const void *>(faceTag)
                              : static_cast<const void *>(lineTag));
    auto &reg = Render::MeshSourceRegistry::instance();
    if (faceTag)
        reg.remove(faceTag);
    if (lineTag)
        reg.remove(lineTag);
}
