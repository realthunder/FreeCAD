/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

#include "OccluderMesh.h"

#include "MeshSimplify.h"

#include <algorithm>
#include <chrono>
#include <thread>

using namespace Render;

namespace {

template<class T>
size_t vectorBytes(const std::vector<T> &v)
{
    return v.capacity() * sizeof(T);
}

}  // namespace

size_t CoarseOccluder::bytes() const
{
    return vectorBytes(positions) + vectorBytes(triangleIndices)
            + vectorBytes(triangleParts) + sizeof(CoarseOccluder);
}

bool Render::buildOccluderHull(const MeshData &mesh, uint32_t level,
                              CoarseOccluder &out)
{
    out = CoarseOccluder();
    if (!mesh.positions || mesh.numVertices <= 0
        || !mesh.triangleIndices || mesh.numTriangleIndices < 3)
        return false;

    // The grid is a fraction of *this mesh's* diagonal, so the bounds
    // have to come from the mesh rather than from the draw that named
    // it: a draw carries world bounds, and a hull is built once in mesh
    // space and reused by every instance of it.
    float bbox[6] = {mesh.positions[0], mesh.positions[1], mesh.positions[2],
                     mesh.positions[0], mesh.positions[1], mesh.positions[2]};
    for (int v = 1; v < mesh.numVertices; ++v) {
        const float *p = mesh.positions + size_t(v) * 3;
        for (int c = 0; c < 3; ++c) {
            bbox[c] = std::min(bbox[c], p[c]);
            bbox[c + 3] = std::max(bbox[c + 3], p[c]);
        }
    }
    const float cell = levelCellSize(bbox, level);
    if (!(cell > 0.0f))
        return false;

    SimplifyOptions opts;
    // Nothing here will ever be shaded: the rasterizer reads three
    // positions per triangle. Both flags say so, from different
    // directions -- one drops the attributes, the other drops the
    // crease that keeping elements apart exists to protect. Welding is
    // also what makes the hull *smaller* than the per-element rung: two
    // faces that meet share one representative instead of two, and the
    // triangles that collapse onto it are dropped.
    opts.trianglesOnly = true;
    opts.weldAcrossParts = true;

    SimplifiedMesh simplified;
    SimplifyStats stats;
    if (!simplifyMesh(mesh, cell, simplified, opts, &stats))
        return false;
    if (simplified.triangleIndices.empty())
        return false;

    out.positions = std::move(simplified.positions);
    out.triangleIndices = std::move(simplified.triangleIndices);
    out.triangleParts = std::move(simplified.triangleParts);
    out.displacement = stats.maxDisplacement;
    out.sourceTriangles = uint32_t(mesh.numTriangleIndices / 3);
    // A hull that is not cheaper than what it stands for is not a hull.
    // It costs the cache its memory and the frame its build, and buys
    // the budget nothing -- and the exact mesh is the safer occluder of
    // the two, since it needs no displacement bound to be true.
    if (out.triangles() >= out.sourceTriangles) {
        out = CoarseOccluder();
        return false;
    }
    return true;
}

bool CoarseOccluderCache::wantsHull(const MeshData &mesh,
                                    uint32_t minTriangles, uint64_t &key)
{
    // No identity, no cache. A hull is keyed by the content id the mesh
    // was published under, and a mesh without one cannot be recognized
    // across frames -- its address could, but an address is reused, and
    // a hull served for the wrong mesh is geometry deleted.
    if (mesh.cacheId == 0)
        return false;
    if (!mesh.positions || !mesh.triangleIndices || mesh.numVertices < 3
        || mesh.numTriangleIndices < 3)
        return false;
    if (uint32_t(mesh.numTriangleIndices / 3) < minTriangles)
        return false;
    key = mesh.cacheId;
    return true;
}

void CoarseOccluderCache::configure(const CoarseOccluderConfig &c)
{
    if (c.level != conf.level || c.minTriangles != conf.minTriangles)
        clear();
    conf = c;
    if (!conf.enabled)
        clear();
    else if (heldBytes > conf.memoryCap)
        evictTo(conf.memoryCap);
}

void CoarseOccluderCache::clear()
{
    entries.clear();
    heldBytes = 0;
    held();
}

void CoarseOccluderCache::held()
{
    // WARNING: What is *held* is live, not a snapshot of the last build.
    // Entries can be dropped by a configure() between frames, and a
    // reader that then saw the pre-eviction byte count would be reading
    // a cache that no longer exists. The per-frame counters beside these
    // -- built, evicted, pending -- are the opposite: they belong to one
    // call and are reset by it.
    framestats.entries = uint32_t(entries.size());
    framestats.bytes = heldBytes;
}

const CoarseOccluder *CoarseOccluderCache::find(const MeshData &mesh)
{
    if (!conf.enabled)
        return nullptr;
    uint64_t key = 0;
    if (!wantsHull(mesh, conf.minTriangles, key))
        return nullptr;
    auto it = entries.find(key);
    if (it == entries.end())
        return nullptr;
    // A rung landing in the same mesh object replaces its arrays under
    // the same cache id (MeshData::generation). The hull of the mesh
    // that was there is then a hull of geometry nobody is drawing.
    if (it->second.generation != mesh.generation)
        return nullptr;
    it->second.used = ++tick;
    return it->second.usable ? &it->second.hull : nullptr;
}

void CoarseOccluderCache::evictTo(size_t cap)
{
    if (heldBytes <= cap)
        return;
    // Least recently asked for first. Sorting the keys rather than
    // keeping an intrusive list: eviction runs when a cap is crossed,
    // not per lookup, and a cache this size is a few thousand entries.
    std::vector<std::pair<uint64_t, uint64_t>> order;  // used, key
    order.reserve(entries.size());
    for (const auto &e : entries)
        order.emplace_back(e.second.used, e.first);
    std::sort(order.begin(), order.end());
    for (const auto &o : order) {
        if (heldBytes <= cap)
            break;
        auto it = entries.find(o.second);
        if (it == entries.end())
            continue;
        heldBytes -= it->second.hull.bytes();
        entries.erase(it);
        ++framestats.evicted;
    }
    held();
}

void CoarseOccluderCache::build(const std::vector<const MeshData *> &wanted,
                                uint32_t workers)
{
    const auto t0 = std::chrono::steady_clock::now();
    framestats.built = 0;
    framestats.evicted = 0;
    framestats.pending = 0;
    framestats.buildMs = 0.0f;
    if (!conf.enabled) {
        framestats.unusable = 0;
        framestats.sourceTriangles = 0;
        framestats.hullTriangles = 0;
        held();
        return;
    }

    missing.clear();
    // One mesh is normally named by several draws, and a mesh queued
    // twice would be built twice into two entries of one key.
    seen.clear();
    for (const MeshData *mesh : wanted) {
        if (!mesh)
            continue;
        uint64_t key = 0;
        if (!wantsHull(*mesh, conf.minTriangles, key))
            continue;
        auto it = entries.find(key);
        if (it != entries.end() && it->second.generation == mesh->generation)
            continue;
        if (!seen.insert(key).second)
            continue;
        missing.push_back(mesh);
    }

    const size_t take =
            std::min<size_t>(missing.size(), conf.buildsPerFrame);
    framestats.pending = uint32_t(missing.size() - take);
    if (take > 0) {
        results.assign(take, CoarseOccluder());
        resultOk.assign(take, 0);
        const uint32_t threads = std::max<uint32_t>(
                1, std::min<uint32_t>(workers, uint32_t(take)));
        auto work = [&](uint32_t w) {
            for (size_t i = w; i < take; i += threads)
                resultOk[i] = buildOccluderHull(*missing[i], conf.level,
                                                results[i])
                        ? 1
                        : 0;
        };
        if (threads == 1) {
            work(0);
        }
        else {
            std::vector<std::thread> pool;
            pool.reserve(threads - 1);
            for (uint32_t w = 1; w < threads; ++w)
                pool.emplace_back(work, w);
            work(0);
            for (auto &t : pool)
                t.join();
        }

        for (size_t i = 0; i < take; ++i) {
            const uint64_t key = missing[i]->cacheId;
            auto it = entries.find(key);
            if (it != entries.end())
                heldBytes -= it->second.hull.bytes();
            Entry &e = entries[key];
            e.hull = std::move(results[i]);
            e.generation = missing[i]->generation;
            e.usable = resultOk[i] != 0;
            e.used = ++tick;
            heldBytes += e.hull.bytes();
            ++framestats.built;
        }
        results.clear();
    }

    evictTo(conf.memoryCap);
    held();

    framestats.unusable = 0;
    framestats.sourceTriangles = 0;
    framestats.hullTriangles = 0;
    for (const auto &e : entries) {
        if (!e.second.usable) {
            ++framestats.unusable;
            continue;
        }
        framestats.sourceTriangles += e.second.hull.sourceTriangles;
        framestats.hullTriangles += e.second.hull.triangles();
    }
    framestats.buildMs = float(std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - t0)
                                       .count());
}
