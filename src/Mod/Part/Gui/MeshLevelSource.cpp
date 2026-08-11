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

#include <App/PropertyStandard.h>
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
};

std::mutex s_refineMutex;
std::condition_variable s_refineCv;
std::deque<RefineJob> s_refineQueue;
/// tag -> the one live token; absent = nothing wanted (canceled).
std::map<const void *, uint64_t> s_refineTokens;
uint64_t s_refineCounter = 0;
int s_refineThreads = 0;

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

void refineLoop()
{
    for (;;) {
        RefineJob job;
        {
            std::unique_lock<std::mutex> lock(s_refineMutex);
            s_refineCv.wait(lock, [] { return !s_refineQueue.empty(); });
            job = std::move(s_refineQueue.front());
            s_refineQueue.pop_front();
            auto it = s_refineTokens.find(job.tag);
            if (it == s_refineTokens.end() || it->second != job.token)
                continue;  // canceled while queued
        }
        // Pre-build ceiling estimate: a build started under a low
        // MemAvailable is a bad_alloc that has not happened yet — and
        // by the time it does, it may be somebody else's. The job is
        // dropped (its ask stands, so it is not retried into the same
        // wall); the observation flips the plans to demoting.
        // The simulation knob (Render_LevelCeilingSimulateMB): raise
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
            continue;
        }
        if (meshed.IsNull())
            continue;
        // The apply reads and writes live document geometry and Coin
        // nodes: GUI thread only. The token is re-checked there — the
        // marshalled hop is one more window for a cancellation.
        auto payload = std::make_shared<std::pair<RefineJob, TopoDS_Shape>>(
            std::move(job), std::move(meshed));
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [payload]() {
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
            },
            Qt::QueuedConnection);
    }
}

void queueExactRefine(const void *tag, const LevelSourceStatePtr &st,
                      std::function<void(const TopoDS_Shape &)> apply)
{
    resolveMemFloor();
    std::lock_guard<std::mutex> lock(s_refineMutex);
    RefineJob job;
    job.token = ++s_refineCounter;
    job.tag = tag;
    job.st = st;
    job.apply = std::move(apply);
    s_refineTokens[tag] = job.token;
    s_refineQueue.push_back(std::move(job));
    if (s_refineThreads < refineThreadCap()) {
        ++s_refineThreads;
        std::thread(refineLoop).detach();
    }
    s_refineCv.notify_one();
}

void cancelExactRefine(const void *tag)
{
    if (!tag)
        return;
    std::lock_guard<std::mutex> lock(s_refineMutex);
    s_refineTokens.erase(tag);
}

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
            return lvl >= 0 && lvl < 8 ? lvl : -1;
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
    return lvl >= 0 && lvl < 8 ? int(lvl) : -1;
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
                                      const char *origin)
{
    if (shape.IsNull() || (!faceTag && !lineTag))
        return;
    // Re-registration replaces the source, so whatever refine was in
    // flight for the previous one is now building the wrong shape.
    cancelExactRefine(faceTag ? faceTag : lineTag);
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
        // still resident (a downgraded source): then the apply runs
        // right here, GUI thread, with the live shape itself —
        // transferMeshLevels reads same-shape as "activate the finest
        // resident rung", no tessellation at all.
        hooks.refine = [primary, st, fired,
                        apply = std::move(onExactBuilt)]() {
            if (fired->exchange(true))
                return;
            if (meshLevelFinerResident(st->shape)) {
                apply(st->shape);
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
    auto &reg = Render::MeshSourceRegistry::instance();
    if (faceTag)
        reg.add(faceTag, gen, builtError, hooks, origin);
    if (lineTag)
        reg.add(lineTag, gen, builtError, hooks, origin);
}

void PartGui::unregisterMeshLevelSource(SoNode *faceTag, SoNode *lineTag)
{
    cancelExactRefine(faceTag ? faceTag : lineTag);
    auto &reg = Render::MeshSourceRegistry::instance();
    if (faceTag)
        reg.remove(faceTag);
    if (lineTag)
        reg.remove(lineTag);
}
