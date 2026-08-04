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
#endif

#include <Base/Console.h>

#include "RenderTiming.h"

using namespace Gui;

namespace
{

std::atomic<bool> _enabled {false};

// Accumulated per stage since the last report. The render pipeline runs on
// the GUI thread, so plain members are enough; the enable flag is atomic
// only because it is written from the property change and read here.
struct Totals
{
    long long ns[RenderTiming::StageCount] {};
    int counts[RenderTiming::StageCount] {};
    int frames {0};
    long long reportedAt {0};
    // Shape of the last flattened map this window saw, not a sum:
    // every publish rebuilds the same map, so an average over the
    // window would say nothing the latest one does not.
    int mapbuckets {-1};
    int mapentries {-1};
};

Totals _totals;
RenderTiming::Scope* _current {nullptr};

long long nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

bool RenderTiming::enabled()
{
    return _enabled.load(std::memory_order_relaxed);
}

void RenderTiming::setEnabled(bool on)
{
    if (_enabled.exchange(on, std::memory_order_relaxed) == on)
        return;
    // Starting or stopping mid-flight would otherwise report a window that
    // was only partly measured.
    reset();
}

const char* RenderTiming::stageName(Stage stage)
{
    switch (stage) {
    case Traverse:
        return "traverse";
    case Delta:
        return "delta";
    case Flatten:
        return "flatten";
    case FlattenSub:
        return "flattensub";
    case Entries:
        return "entries";
    case Translate:
        return "translate";
    case Backend:
        return "backend";
    case Submit:
        return "submit";
    default:
        return "?";
    }
}

RenderTiming::Scope::Scope(Stage stage)
    : stage(stage)
    , active(RenderTiming::enabled())
    , start(active ? nowNs() : 0)
    , childNs(0)
    , parent(nullptr)
{
    if (active) {
        parent = _current;
        _current = this;
    }
}

RenderTiming::Scope::~Scope()
{
    stop();
}

void RenderTiming::Scope::stop()
{
    if (!active)
        return;
    active = false;
    _current = parent;
    const long long total = nowNs() - start;
    // Exclusive time: whatever a nested stage already claimed is not this
    // stage's own cost, but it is still the parent's inclusive time.
    _totals.ns[stage] += total - childNs;
    ++_totals.counts[stage];
    if (parent)
        parent->childNs += total;
}

void RenderTiming::totals(double ms[StageCount], int counts[StageCount])
{
    for (int i = 0; i < StageCount; ++i) {
        ms[i] = double(_totals.ns[i]) / 1e6;
        counts[i] = _totals.counts[i];
    }
}

void RenderTiming::reset()
{
    _totals = Totals();
    _totals.reportedAt = nowNs();
}

void RenderTiming::noteMapShape(int buckets, int entries)
{
    if (!enabled())
        return;
    _totals.mapbuckets = buckets;
    _totals.mapentries = entries;
}

void RenderTiming::frameDone()
{
    if (!enabled())
        return;

    ++_totals.frames;

    const long long now = nowNs();
    if (!_totals.reportedAt) {
        _totals.reportedAt = now;
        return;
    }
    const double windowMs = double(now - _totals.reportedAt) / 1e6;
    if (windowMs < 1000.0)
        return;

    std::ostringstream ss;
    ss << "RenderTiming " << int(windowMs) << "ms frames=" << _totals.frames;
    double accounted = 0.0;
    for (int i = 0; i < StageCount; ++i) {
        const double ms = double(_totals.ns[i]) / 1e6;
        accounted += ms;
        ss << ' ' << stageName(Stage(i)) << '=' << int(ms) << '/' << _totals.counts[i];
    }
    // What the stages did not account for is time this view spent outside
    // the instrumented pipeline (Coin's own drawing, the event loop, the
    // operation that is changing the scene in the first place).
    if (_totals.mapbuckets >= 0)
        ss << " map=" << _totals.mapbuckets << '/' << _totals.mapentries;
    ss << " other=" << int(windowMs - accounted);
    Base::Console().Message("%s\n", ss.str().c_str());

    reset();
}
