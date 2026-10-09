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
#include <Base/ThreadPool.h>
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
#include <Mod/Part/App/MeshTwin.h>

#include "MeshLevelSource.h"

using namespace PartGui;

namespace {

/// The registered closure owns a refcounted handle of the exact shape
/// the display tessellation meshed, plus the job parameters. Const
/// after registration: level threads only read it.
struct LevelSourceState {
    TopoDS_Shape shape;
    MeshLevelJob params;
    /// Drawn as a bounding box in place of the shape: see
    /// registerMeshLevelSource.
    bool standIn = false;
};
using LevelSourceStatePtr = std::shared_ptr<LevelSourceState>;

//////////////////////////////////////////////////////////////////////
// The desktop exact refine (docs/SceneStreaming.md §13, step 1).
//
// A coarse-first display build on a plain desktop process queues one
// job here; a worker meshes a private twin of the shape at the exact
// display parameters and the caller's callback applies it on the GUI
// thread. The twin is made on the GUI thread, when the job is handed
// out, and the worker is a thread of the compute pool: no thread but
// the GUI's reads the document's shape, and none is started here
// (docs/DocumentLoad.md sec 18.9; it used to be OCCT's copier run on a
// worker of this file's own). Job identity is a token per tag: re-registering or
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
    /// A climb that gives its object its FIRST picture -- the coarse mesh
    /// of a shape drawn as a bounding box until now -- and not the
    /// refinement of one it has (docs/DocumentLoad.md sec 18.11). Ahead
    /// of the other climbs wherever they queue, and never held back by a
    /// load.
    bool first = false;
    /// Set and true: asked for AT LEISURE -- a first picture of something
    /// the camera does not see (docs/DocumentLoad.md sec 18.13). Behind
    /// every other job, and its landing held while a load fills in. The
    /// registration owns the flag and the plan turns it: a box that comes
    /// into view moves up with the job it already has (levelJobClass,
    /// reorderLevelJob).
    std::shared_ptr<std::atomic<bool>> idle;
    /// The sweep order this job descends for (0 = none): inherited
    /// from the thread-current generation at enqueue, re-entered
    /// around the landing so chained enqueues inherit it too. What
    /// the downgrade ledger's write-off horizon actually rides.
    uint64_t gen = 0;
};

/// Where a job stands among the others, the lower the sooner, wherever
/// jobs queue:
///   0  a descent -- under the pressure that ordered it, freeing memory
///      outranks spending more;
///   1  a first picture the camera sees (sec 18.11);
///   2  the refinement of an object that has a picture;
///   3  a first picture asked for at leisure (sec 18.13): nothing on the
///      screen changes when it lands, so everything that does goes first
///      -- and the shapes it is for are the large ones, a runner's
///      seconds each.
/// Within a class, in the order they came.
int levelJobClass(const RefineJob &job)
{
    if (job.descent)
        return 0;
    if (job.idle && job.idle->load())
        return 3;
    return job.first ? 1 : 2;
}

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
/// Runner tasks on the compute pool, and spent closures a pool thread
/// has still to destroy: what shutdownMeshLevelWorkers() waits out.
/// Under s_refineMutex.
int s_refineRunners = 0;
int s_reaping = 0;
/// Set once by the shutdown, under s_refineMutex: a job not started
/// does nothing, and nothing enqueues after it.
bool s_refineStop = false;

/// How many jobs run at once. The same sizing rule as the scene server's
/// level threads: modest by default, because BRepMesh already
/// parallelizes each build over OCCT's thread pool; the LevelThreads
/// render parameter sets it, FC_LEVEL_THREADS overrides for the process.
/// The threads themselves are the compute pool's.
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
    /// The refinement of an object that already has a picture: it waits
    /// while a load is still building visuals (loadFilling).
    bool waits = false;
    /// The job's at-leisure flag (RefineJob::idle), read when the pump
    /// comes to the item: a first picture of something out of view waits
    /// like a refinement, and stops waiting if the camera turns to it.
    std::shared_ptr<std::atomic<bool>> idle;

    bool held(bool filling) const
    {
        return filling && (waits || (idle && idle->load()));
    }
};
std::deque<LandingItem> s_landingQueue;

/// Spent pump items go to a thread of the compute pool instead of
/// destructing in the turn: a landing's closure owns the worker's
/// payload -- the meshed twin of an exact climb chief among it -- and
/// freeing those measured 0.1-0.2s of a pump window on the GUI thread
/// (the "frees" split in the pump line). Everything these closures own
/// is safe to destroy off-thread: OCCT handles carry atomic refcounts,
/// Coin nodes appear only as raw unowned pointers, and the detached
/// fill arrays are plain memory. Counted, so that the shutdown can wait
/// for the last of them.
void reapOffThread(std::function<void()> &&fn)
{
    if (!fn)
        return;
    {
        std::lock_guard<std::mutex> lock(s_refineMutex);
        if (s_refineStop) {
            // Shut down: destroy in place, nothing more goes to a
            // thread the process is about to leave under.
            fn = nullptr;
            return;
        }
        ++s_reaping;
    }
    auto spent = std::make_shared<std::function<void()>>(std::move(fn));
    fn = nullptr;
    Base::ThreadPool::compute().post([spent]() mutable {
        // The destruction runs here.
        spent.reset();
        {
            std::lock_guard<std::mutex> lock(s_refineMutex);
            --s_reaping;
        }
        s_refineCv.notify_all();
    });
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
/// A look at what the pump is holding back is on its way.
bool s_landingPollScheduled = false;

void pumpLandings();

void scheduleLandingPump()
{
    if (s_landingScheduled)
        return;
    s_landingScheduled = true;
    QTimer::singleShot(0, QCoreApplication::instance(),
                       []() { pumpLandings(); });
}

/// Whether a load is still giving objects their first picture: the
/// drain of parked visuals has something left (docs/DocumentLoad.md sec
/// 18.11). The GUI thread's time is theirs until it has not. What
/// refines an object that is already drawn -- a climb's landing, which
/// rebuilds its visual -- waits; what frees memory does not, and neither
/// does the coarse mesh of a shape drawn as a box, which IS a first
/// picture. Measured on 528 solids all asking for their exact mesh: the
/// landings took 5.8 to 7.5 s of this thread while the drain ran, and
/// the last object had its picture 12 to 20 s after the open, and 8 to
/// 12 s with them held.
///
/// The HAND-OUT of refinements is not held: the workers go on meshing
/// them while the load fills in, and their results stand on the queue,
/// in memory, until it has. Holding that too was measured and brought
/// the first pictures no sooner (10.2 s against 10.3 s on average) and
/// the refined ones 1.6 s later. Waste of that kind is the price of the
/// first picture and is paid (the user's ruling, 2026-10-09).
bool loadFilling()
{
    auto app = Gui::Application::Instance;
    return app && app->isBuildingVisuals();
}

/// Look again shortly at what a load is holding back. Its own flag: a
/// landing that may run must not wait behind this timer.
void scheduleLandingPoll()
{
    if (s_landingPollScheduled)
        return;
    s_landingPollScheduled = true;
    QTimer::singleShot(50, QCoreApplication::instance(), []() {
        s_landingPollScheduled = false;
        if (!s_landingScheduled)
            pumpLandings();
    });
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
    double budget =
        std::max(1L, Gui::RenderParams::getLevelLandBudgetMS()) / 1000.0;
    const auto start = std::chrono::steady_clock::now();
    // A turn grows with what the event loop costs between turns: see
    // landingTurnBudget.
    static std::chrono::steady_clock::time_point s_lastEnd;
    static bool s_lastBacklog = false;
    budget = PartGui::landingTurnBudget(
        budget, std::chrono::duration<double>(start - s_lastEnd).count(),
        s_lastBacklog);
    auto now = []() { return std::chrono::steady_clock::now(); };
    auto since = [](std::chrono::steady_clock::time_point t0,
                    std::chrono::steady_clock::time_point t1) {
        return std::chrono::duration<double>(t1 - t0).count();
    };
    auto spent = [&start, budget, &now, &since]() {
        return since(start, now()) >= budget;
    };
    PumpAccount &acc = s_pumpAccount;
    // What a load holds back stays on its queue, in its order.
    const bool filling = loadFilling();
    std::size_t held = 0;
    // Landings first: they free memory and re-arm sources; the hook
    // bodies behind them typically queue MORE work.
    while (held < s_landingQueue.size()) {
        if (s_landingQueue[held].held(filling)) {
            ++held;
            continue;
        }
        auto item = std::move(s_landingQueue[held]);
        s_landingQueue.erase(s_landingQueue.begin() + held);
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
    std::size_t heldGui = 0;
    while (heldGui < s_guiWork.size() && (!ranGui || !spent())) {
        // A climb's body is a refinement too (a finer rung still
        // resident, activated with a rebuild).
        if (filling && !s_guiWork[heldGui].descent) {
            ++heldGui;
            continue;
        }
        auto item = std::move(s_guiWork[heldGui]);
        s_guiWork.erase(s_guiWork.begin() + heldGui);
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
    // What is left that may run goes on at once; what a load is holding
    // is looked at again shortly, which is also how the load's end is
    // noticed -- nothing tells this queue when the drain is through.
    bool runnable = false, waiting = false;
    for (const auto &item : s_landingQueue)
        (item.held(filling) ? waiting : runnable) = true;
    for (const auto &item : s_guiWork)
        (filling && !item.descent ? waiting : runnable) = true;
    if (runnable)
        scheduleLandingPump();
    if (waiting)
        scheduleLandingPoll();
    s_lastBacklog = runnable;
    s_lastEnd = std::chrono::steady_clock::now();
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
void queueLandingFromWorker(std::function<void()> fn, uint64_t gen = 0,
                            bool waits = false,
                            std::shared_ptr<std::atomic<bool>> idle = {})
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    auto payload = std::make_shared<std::function<void()>>(std::move(fn));
    QMetaObject::invokeMethod(
        app,
        [payload, gen, waits, idle]() {
            s_landingQueue.push_back(
                {std::move(*payload), gen, waits, idle});
            scheduleLandingPump();
        },
        Qt::QueuedConnection);
}

void dispatchLevelJobs();

/// One job, on a thread of the compute pool. \a twin is the private copy
/// the GUI thread made of the job's shape when it handed the job out
/// (null for a decimation job, which reads no shape at all).
void runLevelJob(RefineJob job, const std::shared_ptr<Part::MeshTwin> &twin)
{
    if (job.work) {
        // A decimation job clusters arrays it owns: no OCCT, no
        // fresh tessellation, and NO memory-floor refusal -- it
        // frees memory, and pressure is exactly when it runs.
        std::function<void()> landing = job.work();
        if (!landing) {
            settleDescent(job);
            return;
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
        return;
    }
    // Pre-build ceiling estimate: a build started under a low
    // MemAvailable is a bad_alloc that has not happened yet -- and
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
        return;
    }
    bool outOfMemory = false;
    // The twin, and nothing of the document's: the copy was made on
    // the GUI thread when this job was handed out, and its mesh is
    // carried back to that thread below.
    TopoDS_Shape meshed;
    if (PartGui::meshLevelTwin(twin->shape(), job.st->params.exactDeflection,
                               job.st->params.exactAngle, &outOfMemory))
        meshed = twin->shape();
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
        return;
    }
    if (meshed.IsNull()) {
        settleDescent(job);
        return;
    }
    // The apply reads and writes live document geometry and Coin
    // nodes: GUI thread only. The token is re-checked there -- the
    // marshalled hop is one more window for a cancellation.
    auto payload = std::make_shared<std::pair<RefineJob, TopoDS_Shape>>(
        std::move(job), std::move(meshed));
    const uint64_t gen = payload->first.gen;
    // A climb that refines waits for a load; a descent and a first
    // picture do not.
    const bool waits = !payload->first.descent && !payload->first.first;
    // ...and neither does a first picture asked for at leisure, for as
    // long as it stands that way: the flag travels with the landing.
    auto idle = payload->first.idle;
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
    }, gen, waits, std::move(idle));
}

/// A job with the private twin of its shape: ready for a runner.
struct ReadyJob {
    RefineJob job;
    /// Null for a decimation job, which reads no shape.
    std::shared_ptr<Part::MeshTwin> twin;
    /// The faces and edges the twin holds.
    std::size_t parts = 0;
};
/// The line between the GUI thread, which makes the twins, and the
/// runners, which take the next job from here without waiting for it:
/// a runner that had to ask the GUI thread for each job would stand idle
/// exactly when that thread is busy landing the others' results. Under
/// s_refineMutex.
std::deque<ReadyJob> s_refineReady;
/// What the twins standing ready hold, faces and edges.
std::size_t s_refineReadyParts = 0;
/// How much stands ready, counted in faces and edges and not in jobs --
/// the pre-mesh's lesson, learned a second time here (docs/DocumentLoad.md
/// sec 18.9). One job ready for each runner was the first thing built:
/// most refines are of small shapes and take milliseconds, a refill
/// takes a turn of the GUI thread's event loop, and on the reference
/// assembly some 500 refines had landed a minute into the load where
/// 5000 had after 40 s.
/// With 20000, 200000 and two million the reference load's refines
/// settled as they had before any of this, 40 to 51 s after the open in
/// every arm, some 5137 landings each: past "not starved" the size of
/// the line is not what the wave waits on.
const std::size_t kReadyParts = 20000;
/// A dispatch is on its way to the GUI thread.
std::atomic<bool> s_dispatchPosted {false};

/// From any thread: have the GUI thread top the ready line up. One call
/// on its way at a time.
void requestLevelDispatch()
{
    if (s_dispatchPosted.exchange(true))
        return;
    QCoreApplication *app = QCoreApplication::instance();
    if (!app) {
        s_dispatchPosted.store(false);
        return;
    }
    QMetaObject::invokeMethod(app, []() {
        s_dispatchPosted.store(false);
        dispatchLevelJobs();
    }, Qt::QueuedConnection);
}

/// A task of the compute pool: run ready jobs until there is none.
void levelRunner()
{
    for (;;) {
        ReadyJob next;
        {
            std::lock_guard<std::mutex> lock(s_refineMutex);
            if (s_refineStop || s_refineReady.empty()) {
                --s_refineRunners;
                break;
            }
            next = std::move(s_refineReady.front());
            s_refineReady.pop_front();
            s_refineReadyParts -= std::min(s_refineReadyParts, next.parts);
            auto it = s_refineTokens.find(next.job.tag);
            if (it == s_refineTokens.end() || it->second != next.job.token) {
                // canceled while it stood ready
                settleDescent(next.job);
                continue;
            }
        }
        // Its place in the line is free
        requestLevelDispatch();
        runLevelJob(std::move(next.job), next.twin);
    }
    s_refineCv.notify_all();
    requestLevelDispatch();
}

/// GUI thread: move queued jobs to the ready line while it has room, and
/// see that runners are there to take them -- as many at once as the cap.
///
/// The private twin of a job's shape is made HERE, as late as it can be
/// and on the one thread the document's shape belongs to: a job canceled
/// while queued is never copied, and the twins alive are the ones
/// running and the ready line's. Stripped (Part::MeshTwin::Resident::Strip): the mesher does
/// not coarsen or refine a mesh it finds, it has to find none, and
/// purely triangulated faces keep the only geometry they have.
void dispatchLevelJobs()
{
    const int cap = refineThreadCap();
    for (;;) {
        RefineJob job;
        {
            std::lock_guard<std::mutex> lock(s_refineMutex);
            // Room: a job for each runner whatever its size, and beyond
            // that as long as the line is light -- or what stands at its
            // end is there at leisure and the job waiting is not: a line
            // full of large shapes nobody is looking at must not keep a
            // job for the view out until a runner has taken one of them.
            if (s_refineStop || s_refineQueue.empty())
                break;
            if (int(s_refineReady.size()) >= cap
                && s_refineReadyParts >= kReadyParts
                && !(levelJobClass(s_refineReady.back().job) == 3
                     && levelJobClass(s_refineQueue.front()) < 3))
                break;
            job = std::move(s_refineQueue.front());
            s_refineQueue.pop_front();
            auto it = s_refineTokens.find(job.tag);
            if (it == s_refineTokens.end() || it->second != job.token) {
                // canceled while queued
                settleDescent(job);
                continue;
            }
        }
        std::shared_ptr<Part::MeshTwin> twin;
        if (!job.work) {
            try {
                twin = std::make_shared<Part::MeshTwin>(
                    job.st->shape, Part::MeshTwin::Resident::Strip);
            }
            catch (const Standard_Failure &) {
                twin.reset();
            }
            catch (const std::bad_alloc &) {
                twin.reset();
            }
            if (!twin || twin->isNull()) {
                settleDescent(job);
                continue;
            }
        }
        const std::size_t parts =
            twin ? twin->faceCount() + twin->edgeCount() : 0;
        std::lock_guard<std::mutex> lock(s_refineMutex);
        if (s_refineStop) {
            settleDescent(job);
            break;
        }
        s_refineReadyParts += parts;
        // The queue's own order holds in the line (levelJobClass).
        const int cls = levelJobClass(job);
        auto at = s_refineReady.begin();
        while (at != s_refineReady.end() && levelJobClass(at->job) <= cls)
            ++at;
        s_refineReady.insert(at, ReadyJob{std::move(job), std::move(twin), parts});
    }
    int start = 0;
    {
        std::lock_guard<std::mutex> lock(s_refineMutex);
        if (!s_refineStop) {
            const int want = std::min(
                cap, s_refineRunners + int(s_refineReady.size()));
            start = std::max(0, want - s_refineRunners);
            s_refineRunners += start;
        }
    }
    for (int i = 0; i < start; ++i)
        Base::ThreadPool::compute().post([]() { levelRunner(); });
}

/// Arm shutdownMeshLevelWorkers() for the application's exit: on
/// aboutToQuit (the event loop returning) and again as a post routine
/// (the QCoreApplication's destruction, for an exit that never ran the
/// loop out). Both before the statics go. GUI thread, once, when the
/// first job is queued -- the resolveMemFloor pattern.
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
    hookWorkerShutdown();
    {
        std::lock_guard<std::mutex> lock(s_refineMutex);
        if (s_refineStop) {
            // Shutting down: the job is dropped, its descent settled.
            settleDescent(job);
            return;
        }
        job.token = ++s_refineCounter;
        s_refineTokens[job.tag] = job.token;
        // By class, and among peers in the order they came -- the
        // plan's own (levelJobClass). In the order they came alone, a
        // shape drawn as a box stood behind the exact meshes of
        // everything that already had a picture.
        const int cls = levelJobClass(job);
        auto it = s_refineQueue.begin();
        while (it != s_refineQueue.end() && levelJobClass(*it) <= cls)
            ++it;
        s_refineQueue.insert(it, std::move(job));
    }
    dispatchLevelJobs();
}

/// GUI thread: the ask behind \a tag's job changed between "for the
/// view" and "at leisure" (its idle flag is already turned). Put the job
/// where its class now stands, in the queue or in the ready line; one
/// that a runner has taken is past moving, and its landing reads the
/// flag for itself.
void reorderLevelJob(const void *tag)
{
    bool moved = false;
    {
        std::lock_guard<std::mutex> lock(s_refineMutex);
        auto live = s_refineTokens.find(tag);
        if (live == s_refineTokens.end())
            return;
        for (auto it = s_refineQueue.begin(); it != s_refineQueue.end();
             ++it) {
            if (it->tag != tag || it->token != live->second)
                continue;
            RefineJob job = std::move(*it);
            s_refineQueue.erase(it);
            const int cls = levelJobClass(job);
            auto at = s_refineQueue.begin();
            while (at != s_refineQueue.end() && levelJobClass(*at) <= cls)
                ++at;
            s_refineQueue.insert(at, std::move(job));
            moved = true;
            break;
        }
        for (auto it = s_refineReady.begin();
             !moved && it != s_refineReady.end(); ++it) {
            if (it->job.tag != tag || it->job.token != live->second)
                continue;
            ReadyJob ready = std::move(*it);
            s_refineReady.erase(it);
            const int cls = levelJobClass(ready.job);
            auto at = s_refineReady.begin();
            while (at != s_refineReady.end()
                   && levelJobClass(at->job) <= cls)
                ++at;
            s_refineReady.insert(at, std::move(ready));
            break;
        }
    }
    // A job moved up in the queue may now have a place in the line.
    if (moved)
        dispatchLevelJobs();
}

void queueExactRefine(const void *tag, const LevelSourceStatePtr &st,
                      std::function<void(const TopoDS_Shape &)> apply,
                      bool descent = false,
                      std::shared_ptr<std::atomic<bool>> idle = {})
{
    RefineJob job;
    job.tag = tag;
    job.st = st;
    job.apply = std::move(apply);
    job.descent = descent;
    job.first = !descent && st && st->standIn;
    job.idle = std::move(idle);
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
    // Queued jobs and the ones standing ready are dropped (each descent
    // settled exactly once, as on every other exit) and nothing is
    // handed out from here. A build in flight finishes first -- a worker
    // inside BRepMesh has no safe interruption point -- and its landing
    // is posted to an application that no longer runs a loop, which is
    // fine: the queued event dies with the application, and the payload
    // it owns (a meshed twin) with it. Waited for, with the closures a
    // pool thread is still destroying: the pool's threads outlive this,
    // and must not be inside OCCT when the process takes it down.
    std::unique_lock<std::mutex> lock(s_refineMutex);
    s_refineStop = true;
    for (const auto &job : s_refineQueue)
        settleDescent(job);
    s_refineQueue.clear();
    for (const auto &ready : s_refineReady)
        settleDescent(ready.job);
    s_refineReady.clear();
    s_refineReadyParts = 0;
    s_refineTokens.clear();
    s_refineCv.wait(lock, [] { return s_refineRunners == 0 && s_reaping == 0; });
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

double PartGui::landingTurnBudget(double budget, double away, bool backlog)
{
    if (!backlog || !(away > 0.0))
        return budget;
    return std::max(budget, std::min(0.5 * away, 5.0 * budget));
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
                                      float scaledError,
                                      bool standIn,
                                      bool owedAtLeisure)
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
    st->standIn = standIn;
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
        // How the ask stands, for the job and for its landing: asked at
        // leisure (a box out of view, sec 18.13), or for the view. The
        // plan turns it by firing the hook again, and the job it queued
        // the first time moves.
        auto atLeisure = std::make_shared<std::atomic<bool>>(false);
        hooks.firstPictureOwed = standIn && owedAtLeisure
            && Gui::RenderParams::getCoarseDeferAtLeisure();
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
        hooks.refine = [primary, st, fired, atLeisure,
                        apply = std::move(onExactBuilt)](bool idle) {
            const bool turned = atLeisure->exchange(idle) != idle;
            if (fired->exchange(true)) {
                if (turned)
                    reorderLevelJob(primary);
                return;
            }
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
            queueExactRefine(primary, st, apply, /*descent*/ false,
                             atLeisure);
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
