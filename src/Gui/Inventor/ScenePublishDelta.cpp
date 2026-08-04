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

#ifndef _PreComp_
#include <atomic>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <vector>
#endif

#include <Base/Console.h>

// A cache entry holds a reference to a vertex cache, so releasing one
// needs the type complete, not just declared.
#include "SoFCVertexCache.h"
#include "ScenePublishDelta.h"

using namespace Gui;

typedef SoFCRenderCache::CacheEntry CacheEntry;
typedef SoFCRenderCache::Material Material;

namespace
{

std::atomic<bool> _logging {false};

long long nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// What a child is, for matching purposes. Pointer identity is only
/// meaningful because the delta retains the previous scene cache, which
/// holds a reference to every child it lists: no address can have been
/// recycled underneath a comparison.
const void* childKey(const CacheEntry& entry)
{
    if (entry.cache) {
        return static_cast<const void*>(entry.cache.get());
    }
    return static_cast<const void*>(entry.vcache.get());
}


}  // namespace

/// Whether two children contribute identically, and so whether the newer
/// one may keep whatever the older one produced.
///
/// The child cache alone is not enough. A child is captured together with
/// the transform and material in force where it was reached, and both
/// live in the parent's entry, so an untouched child under a moved
/// parent transform is a changed contribution.
bool ScenePublishDelta::sameEntry(const CacheEntry& a, const CacheEntry& b)
{
    if (a.cache != b.cache || a.vcache != b.vcache) {
        return false;
    }
    if (a.identity != b.identity || a.resetmatrix != b.resetmatrix) {
        return false;
    }
    if (!a.identity && a.matrix != b.matrix) {
        return false;
    }

    // Materials compare through the very ordering the flattened map is
    // keyed by: two materials that are neither less than the other land
    // in the same bucket with the same draw state, which is all the
    // flatten does with a captured material.
    //
    // That ordering compares a different set of fields per material type,
    // and the flatten re-types a captured material to whatever kind of
    // geometry the child turns out to hold. Only the triangle branch
    // covers every field it could then read, so anything else is reported
    // as changed rather than trusted — captured materials are always
    // triangles today, so this costs nothing and stops a later change of
    // that from quietly making the diff unsound.
    if (a.material.type != Material::Triangle || b.material.type != Material::Triangle) {
        return false;
    }
    if (a.material < b.material || b.material < a.material) {
        return false;
    }

    return true;
}

bool ScenePublishDelta::logging()
{
    return _logging.load(std::memory_order_relaxed);
}

void ScenePublishDelta::setLogging(bool on)
{
    _logging.store(on, std::memory_order_relaxed);
}

void ScenePublishDelta::begin()
{
    this->reusedseps = 0;
    this->rebuiltseps = 0;
    this->addedtotal = 0;
    this->removedtotal = 0;
    this->kepttotal = 0;
    this->diffedcaches = 0;
}

void ScenePublishDelta::countSeparator(bool reused)
{
    if (reused) {
        ++this->reusedseps;
    }
    else {
        ++this->rebuiltseps;
    }
}

void ScenePublishDelta::clear()
{
    this->curscene.reset();
    this->prevscene.reset();
    this->addedidx.clear();
    this->removedidx.clear();
    this->keptcount = 0;
}

int ScenePublishDelta::diff(SoFCRenderCache* prev,
                            SoFCRenderCache* cur,
                            SbFCVector<int>& added,
                            SbFCVector<int>& removed)
{
    const SbFCVector<CacheEntry> empty;
    const auto& oldkids = prev ? prev->getChildCaches() : empty;
    const auto& newkids = cur ? cur->getChildCaches() : empty;

    const int oldcount = static_cast<int>(oldkids.size());
    const int newcount = static_cast<int>(newkids.size());

    if (!oldcount) {
        added.reserve(added.size() + newcount);
        for (int i = 0; i < newcount; ++i) {
            added.push_back(i);
        }
        return 0;
    }

    // Index the previous children by identity. One child can appear more
    // than once — the same separator reached through several paths, held
    // under a transform each time — so a key carries every entry that
    // used it, and each of them is claimed by at most one new child.
    std::unordered_map<const void*, std::vector<int>> oldbykey;
    oldbykey.reserve(oldcount * 2);
    for (int i = 0; i < oldcount; ++i) {
        oldbykey[childKey(oldkids[i])].push_back(i);
    }

    std::vector<char> claimed(oldcount, 0);
    int kept = 0;

    for (int i = 0; i < newcount; ++i) {
        const auto it = oldbykey.find(childKey(newkids[i]));
        int match = -1;
        if (it != oldbykey.end()) {
            for (int j : it->second) {
                if (!claimed[j] && sameEntry(oldkids[j], newkids[i])) {
                    match = j;
                    break;
                }
            }
        }
        if (match < 0) {
            added.push_back(i);
        }
        else {
            claimed[match] = 1;
            ++kept;
        }
    }

    for (int j = 0; j < oldcount; ++j) {
        if (!claimed[j]) {
            removed.push_back(j);
        }
    }

    return kept;
}

void ScenePublishDelta::updateCache(SoFCRenderCache* cache, SoFCRenderCache* prev)
{
    if (!cache || !prev) {
        return;
    }

    this->scratchadded.clear();
    this->scratchremoved.clear();
    const int kept = diff(prev, cache, this->scratchadded, this->scratchremoved);

    ++this->diffedcaches;
    this->addedtotal += static_cast<int>(this->scratchadded.size());
    this->removedtotal += static_cast<int>(this->scratchremoved.size());
    this->kepttotal += kept;
}

void ScenePublishDelta::update(SoFCRenderCache* scene)
{
    this->addedidx.clear();
    this->removedidx.clear();

    this->prevscene = this->curscene;
    this->curscene = scene;

    this->keptcount = diff(this->prevscene, this->curscene, this->addedidx, this->removedidx);

    ++this->diffedcaches;
    this->addedtotal += static_cast<int>(this->addedidx.size());
    this->removedtotal += static_cast<int>(this->removedidx.size());
    this->kepttotal += this->keptcount;

    report();
}

void ScenePublishDelta::report()
{
    if (!logging()) {
        return;
    }

    ++this->logpublishes;
    this->logadded += this->addedtotal;
    this->logremoved += this->removedtotal;
    this->logkept += this->kepttotal;
    this->logdiffed += this->diffedcaches;
    this->logsceneadded += static_cast<int>(this->addedidx.size());
    this->logsceneremoved += static_cast<int>(this->removedidx.size());
    this->logreused += this->reusedseps;
    this->logrebuilt += this->rebuiltseps;

    const long long now = nowNs();
    if (!this->reportedAt) {
        this->reportedAt = now;
        return;
    }
    const double windowMs = double(now - this->reportedAt) / 1e6;
    if (windowMs < 1000.0) {
        return;
    }

    const int children =
        this->curscene ? static_cast<int>(this->curscene->getChildCaches().size()) : 0;
    const int total = this->logkept + this->logadded + this->logremoved;

    std::ostringstream ss;
    ss << "ScenePublishDelta " << int(windowMs) << "ms publishes="
       << this->logpublishes
       // The scene root on its own, which in a document under a container
       // is one child that is replaced every time anything moves: the
       // number that says why the change set has to go down the hierarchy.
       << " scene=" << children << " children +" << this->logsceneadded << " -"
       << this->logsceneremoved
       // And the hierarchy: every cache the publish rebuilt, diffed
       // against the cache it replaced.
       << " | caches=" << this->logdiffed << " diffed, children +" << this->logadded << " -"
       << this->logremoved << " kept=" << this->logkept;
    if (total) {
        ss << " (" << int(100.0 * this->logkept / total) << "% unchanged)";
    }
    ss << " | separators=" << this->logreused << " reused/" << this->logrebuilt << " rebuilt";
    Base::Console().Message("%s\n", ss.str().c_str());

    this->logpublishes = 0;
    this->logadded = 0;
    this->logremoved = 0;
    this->logkept = 0;
    this->logdiffed = 0;
    this->logsceneadded = 0;
    this->logsceneremoved = 0;
    this->logreused = 0;
    this->logrebuilt = 0;
    this->reportedAt = now;
}
