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
        TopoDS_Shape meshed = PartGui::meshLevelExactCopy(
            job.st->shape, job.st->params.exactDeflection,
            job.st->params.exactAngle);
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

} // anonymous namespace

int PartGui::coarseTessellationLevel()
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
    if (!std::getenv("FC_BGFX_SERVE_SCENE")) {
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
    // global parameter, like every other render parameter; the serving
    // process has one 3D view, so the active view is the served one.
    long lvl = Gui::RenderParams::getCoarseTessellation();
    if (auto *view = qobject_cast<Gui::View3DInventor *>(
            Gui::Application::Instance->activeView())) {
        if (auto *prop = dynamic_cast<App::PropertyInteger *>(
                view->getPropertyByName("Render_CoarseTessellation")))
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
                                          onExactBuilt)
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
    std::function<void()> refine, cancel;
    if (onExactBuilt && builtError > 0.0f
        && !std::getenv("FC_BGFX_SERVE_SCENE")) {
        const void *primary = faceTag ? faceTag : lineTag;
        auto fired = std::make_shared<std::atomic<bool>>(false);
        refine = [primary, st, fired,
                  apply = std::move(onExactBuilt)]() {
            if (fired->exchange(true))
                return;
            queueExactRefine(primary, st, apply);
        };
        cancel = [primary, fired]() {
            if (!fired->exchange(false))
                return;
            cancelExactRefine(primary);
        };
    }
    auto &reg = Render::MeshSourceRegistry::instance();
    if (faceTag)
        reg.add(faceTag, gen, builtError, refine, cancel);
    if (lineTag)
        reg.add(lineTag, gen, builtError, refine, cancel);
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
