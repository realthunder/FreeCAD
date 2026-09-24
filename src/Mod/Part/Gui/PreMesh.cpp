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
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <BRepMesh_IncrementalMesh.hxx>
#include <IMeshTools_Parameters.hxx>
#include <OSD_Parallel.hxx>
#include <Standard_Failure.hxx>

#include <Gui/RenderParams.h>

#include "PreMesh.h"

namespace PartGui
{

namespace
{

struct Claim
{
    /// Pins the TShape this claim is KEYED on. Without it a document
    /// closing frees that TShape, a later allocation reuses the
    /// address, and the stale claim answers -- with a bounding box --
    /// for a shape it knows nothing about. Released by
    /// clearPreMeshClaims, which the drain calls once every queue it
    /// serves is empty.
    TopoDS_Shape shape;
    Bnd_Box geomBox;
    /// False while a worker is still writing this TShape's
    /// triangulation. Read by the GUI thread on every build of the
    /// shape, so it is the one field that must be cheap and atomic.
    std::atomic<bool> done {false};
};

/// The claims, keyed by TShape address. Compared, never dereferenced --
/// the batch owns a TopoDS_Shape handle for every shape it meshes, so a
/// claimed TShape cannot die while its claim is in flight, and a claim
/// left behind by a closed document is dropped by clearPreMeshClaims
/// (a recycled address would otherwise answer for a different shape).
/// The map is only ever inserted into on the GUI thread, at submit; a
/// worker writes nothing but its own claim's flag, and readers take the
/// mutex to find the entry. Entries are stable (unique_ptr), so a
/// worker's flag write cannot be moved by a rehash.
std::mutex s_mutex;
std::unordered_map<const void *, std::unique_ptr<Claim>> s_claims;
std::size_t s_claimed = 0;
std::size_t s_meshed = 0;
std::size_t s_failed = 0;
double s_wall = 0.0;
/// Bumped by clearPreMeshClaims: a batch submitted before it stops
/// publishing, because the shapes its claims describe are gone.
std::atomic<uint64_t> s_generation {0};

/// Signalled every time a claim is published, for waitPreMesh. The
/// publishing store is made under s_mutex as well as being atomic, and
/// that is the whole point of it: without the lock a worker could
/// publish and notify in the window between a waiter's predicate test
/// and its wait, and the waiter would then sleep through the wakeup it
/// had already been owed.
std::condition_variable s_published;

/// One batch's work, owned by the thread that runs it: the shapes stay
/// alive for as long as any worker may touch them.
struct Batch
{
    std::vector<PreMeshItem> items;
    std::vector<Claim *> claims;
    uint64_t generation = 0;
    std::atomic<std::size_t> meshed {0};
    std::atomic<std::size_t> failed {0};
};

struct BatchFunctor
{
    Batch *batch;

    void operator()(int index) const
    {
        const PreMeshItem &item = batch->items[std::size_t(index)];
        Claim *claim = batch->claims[std::size_t(index)];
        try {
            IMeshTools_Parameters params;
            params.Deflection = item.deflection;
            params.Relative = Standard_False;
            params.Angle = item.angle;
            // The split is across shapes here, so each shape is meshed
            // whole and single-threaded: OCCT's own per-face split
            // would only contend with the 27 siblings already running.
            params.InParallel = Standard_False;
            // Exactly what the display build asks with, so the mesh
            // this leaves is the mesh that build would have made.
            params.AllowQualityDecrease = Standard_True;
            BRepMesh_IncrementalMesh(item.shape, params);
            ++batch->meshed;
        }
        catch (const Standard_Failure &) {
            // A shape OCCT cannot mesh is not an error here: the
            // display build will make the same call and fail the same
            // way, which is where such a shape has always been
            // handled. The claim is still published -- the box it
            // carries is the geometry's, mesh or no mesh.
            ++batch->failed;
        }
        catch (...) {
            ++batch->failed;
        }
        // Published last, and only after the tessellation is entirely
        // written: this is what lets the GUI thread touch the shape.
        // Under the mutex because a build may be asleep on this very
        // claim (waitPreMesh), and the lock is what orders this store
        // against that waiter's predicate test.
        {
            std::lock_guard<std::mutex> guard(s_mutex);
            claim->done.store(true, std::memory_order_release);
        }
        s_published.notify_all();
    }
};

void runBatch(Batch *batch)
{
    const auto start = std::chrono::steady_clock::now();
    const int count = int(batch->items.size());
    try {
        OSD_Parallel::For(0, count, BatchFunctor{batch}, count <= 1);
    }
    catch (...) {
        // Whatever is left unpublished would park its shapes' builds
        // forever -- or hold a waiting one until its timeout -- so
        // every claim is released before this unwinds.
        {
            std::lock_guard<std::mutex> guard(s_mutex);
            for (Claim *claim : batch->claims)
                claim->done.store(true, std::memory_order_release);
        }
        s_published.notify_all();
    }
    const double wall = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (batch->generation == s_generation.load(std::memory_order_acquire)) {
            s_meshed += batch->meshed.load();
            s_failed += batch->failed.load();
            s_wall += wall;
        }
    }
    delete batch;
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
    auto *batch = new Batch();
    batch->items = std::move(items);
    batch->claims.reserve(batch->items.size());
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        batch->generation = s_generation.load(std::memory_order_acquire);
        for (const PreMeshItem &item : batch->items) {
            const void *tshape = item.shape.TShape().get();
            auto &slot = s_claims[tshape];
            if (!slot)
                slot = std::make_unique<Claim>();
            slot->shape = item.shape;
            slot->geomBox = item.geomBox;
            slot->done.store(false, std::memory_order_release);
            batch->claims.push_back(slot.get());
            ++s_claimed;
        }
    }
    // Detached, because nothing waits for it: the drain discovers the
    // results through the claims as it walks its queue.
    std::thread(runBatch, batch).detach();
}

bool preMeshInFlight(const void *tshape)
{
    if (!tshape)
        return false;
    std::lock_guard<std::mutex> guard(s_mutex);
    auto it = s_claims.find(tshape);
    return it != s_claims.end()
        && !it->second->done.load(std::memory_order_acquire);
}

bool waitPreMesh(const void *tshape, double seconds)
{
    if (!tshape)
        return true;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(std::max(0.0, seconds)));
    std::unique_lock<std::mutex> lock(s_mutex);
    // A claim that is GONE answers as well as one that is published:
    // clearPreMeshClaims drops the lot, and nothing owns the shape after
    // it. Waiting on the entry itself would be waiting on a claim that
    // no longer exists to be published.
    return s_published.wait_until(lock, deadline, [tshape]() {
        auto it = s_claims.find(tshape);
        return it == s_claims.end()
            || it->second->done.load(std::memory_order_acquire);
    });
}

bool preMeshBox(const void *tshape, Bnd_Box &box)
{
    if (!tshape)
        return false;
    std::lock_guard<std::mutex> guard(s_mutex);
    auto it = s_claims.find(tshape);
    if (it == s_claims.end()
            || !it->second->done.load(std::memory_order_acquire)
            || it->second->geomBox.IsVoid())
        return false;
    box = it->second->geomBox;
    return true;
}

void preMeshStats(std::size_t &claimed, std::size_t &meshed,
                  std::size_t &failed, double &wall)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    claimed = s_claimed;
    meshed = s_meshed;
    failed = s_failed;
    wall = s_wall;
}

void clearPreMeshClaims()
{
    std::lock_guard<std::mutex> guard(s_mutex);
    // An in-flight batch still holds pointers to these claims, so the
    // entries themselves are not freed here -- the generation bump is
    // what tells that batch its results are no longer wanted, and the
    // claims it releases are ones nothing looks up any more.
    for (auto &entry : s_claims)
        entry.second->done.store(true, std::memory_order_release);
    s_claims.clear();
    s_claimed = s_meshed = s_failed = 0;
    s_wall = 0.0;
    s_generation.fetch_add(1, std::memory_order_acq_rel);
    // A build asleep on one of these is waiting for a claim that has
    // just stopped existing; its predicate reads that as settled.
    s_published.notify_all();
}

} // namespace PartGui
