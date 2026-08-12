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

#include "SceneLadder.h"
#include "ProxyHierarchy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <queue>
#include <set>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#include <emscripten/heap.h>
#elif defined(_WIN32)
// windows.h defines min/max as macros unless told not to, which turns every
// std::min/std::max below into a syntax error (C2589).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>
#endif

using namespace Render;

namespace {

// Just enough vector arithmetic for a projected size. Written out
// rather than pulled from bx, because this file is the half of the
// ladder that both tiers share and neither should have to link a math
// library to rank a payload.

inline void sub3(const float *a, const float *b, float *out)
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

inline float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline float length3(const float *a)
{
    return std::sqrt(dot3(a, a));
}

inline void normalize3(float *v)
{
    const float len = length3(v);
    if (len > 0.0f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

// Out of line so the vtable has a home here rather than in every
// translation unit that sees the header.
bool RungProvider::generate(const LevelRequest &)
{
    return false;
}

// ----------------------------------------------------------------------
// MemoryBudget
// ----------------------------------------------------------------------

namespace {

/// The share of the largest possible heap a scene may aim to occupy.
///
/// Well under half, because the ceiling is where the process *dies*,
/// not where it is uncomfortable: on wasm the growth cap is a hard wall
/// an allocation walks into with no warning and no chance to report,
/// and everything the budget does not know about — the backend's own
/// buffers, a texture upload in flight, whatever the page is doing —
/// comes out of the same space. Aiming at a third leaves room for all
/// of it to be wrong at once.
const float kCeilingShare = 0.33f;

/// Never propose less than this, whatever the arithmetic says. A budget
/// small enough to hold nothing does not degrade gracefully — it draws
/// every object as a box forever, which reads as a broken viewer rather
/// than a loaded one.
const size_t kMinBudget = 24u * 1024 * 1024;

/// Nor more than this from a guess alone. The point of the ceiling is
/// that a machine which will not say how much memory it has should not
/// be assumed to have all of it.
const size_t kMaxGuess = 512u * 1024 * 1024;

/// Over how much payload growth a slope sample fades from the
/// estimate. The expansion is a ratio of two series that both jump — a
/// batch lands, the heap grows in slabs — and the failure mode of
/// averaging per-sample slopes is systematic, not noisy: between slabs
/// every sample reads "the heap did not grow" and drags the estimate
/// down, then one slab arrives as a single clamped spike that cannot
/// pull it back. So the slope is the ratio of two *decayed sums*
/// instead — a slab's bytes land in the numerator whenever they land,
/// against all the payload growth of the window rather than one
/// sample's worth. The window must comfortably exceed the largest slab
/// the allocator grows by, or the same bias returns at window scale.
const double kSlopeWindow = 64.0 * 1024 * 1024;

/// The share of an observed hard ceiling to aim under. The observation
/// is the heap at the moment an allocation failed, so the wall is at
/// it, not near it — and everything the budget does not track still
/// has to fit below.
const float kObservedCeilingShare = 0.66f;

/// The ceiling to start from on a mobile browser that will not say how
/// much memory it has (navigator.deviceMemory is absent on exactly the
/// platform where the wall is nearest, Safari). The wasm growth cap is
/// no substitute — a phone whose heap may grow to 2 GB does not have
/// 2 GB to give, and the OS kills the tab without any signal the
/// process could observe. Below any modern phone's real allowance,
/// which is the direction to be wrong in; the measured expansion still
/// adapts the budget underneath it.
const size_t kMobileNoHintCeiling = 256u * 1024 * 1024;

/// Bounds on a measured expansion. Outside these the measurement is not
/// telling us about geometry: below, the heap grew less than the
/// payloads that supposedly filled it (a slab already had room); far
/// above, something other than the scene is allocating and dividing by
/// it would collapse the budget to nothing.
const float kMinExpansion = 0.5f;
const float kMaxExpansion = 8.0f;

/// How much the resident geometry must grow between two samples before
/// the slope between them means anything. Too small and the ratio is
/// two heap slabs divided by rounding noise.
const size_t kSlopeMinDelta = 2u * 1024 * 1024;

}  // namespace

size_t MemoryBudget::systemMemory()
{
#ifdef __EMSCRIPTEN__
    // What the wasm heap may grow to: MAXIMUM_MEMORY if the build set
    // one, else the wasm32 address space. It is an upper bound on what
    // could ever be allocated and not a promise that it can be — which
    // is what kCeilingShare is for.
    return size_t(emscripten_get_heap_max());
#elif defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        return size_t(status.ullTotalPhys);
    return 0;
#elif defined(__APPLE__)
    int64_t bytes = 0;
    size_t len = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0 && bytes > 0)
        return size_t(bytes);
    return 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0)
        return size_t(pages) * size_t(pageSize);
    return 0;
#endif
}

size_t MemoryBudget::availableMemory()
{
#ifdef __EMSCRIPTEN__
    // The wasm heap has no meaningful "available" beyond its growth
    // cap; the tier estimates through observe() instead.
    return 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        return size_t(status.ullAvailPhys);
    return 0;
#elif defined(__APPLE__)
    // No cheap MemAvailable equivalent worth a Mach call here yet.
    return 0;
#else
    // MemAvailable is the kernel's own estimate of what can be
    // allocated without swapping — the number an "am I about to hurt
    // this machine" check wants, which free pages alone are not.
    if (std::FILE *f = std::fopen("/proc/meminfo", "r")) {
        char line[128];
        size_t kb = 0;
        while (std::fgets(line, sizeof(line), f)) {
            if (std::sscanf(line, "MemAvailable: %zu kB", &kb) == 1)
                break;
            kb = 0;
        }
        std::fclose(f);
        if (kb)
            return kb * 1024;
    }
    return 0;
#endif
}

size_t MemoryBudget::deviceHint()
{
#ifdef __EMSCRIPTEN__
    // Chrome and the Android browsers only, rounded to a power of two
    // and capped at 8: a hint, never a limit. Absent everywhere else,
    // which is why nothing downstream may depend on it.
    const int gib = EM_ASM_INT({
        return (navigator && navigator.deviceMemory) ? navigator.deviceMemory : 0;
    });
    return gib > 0 ? size_t(gib) * 1024u * 1024u * 1024u : 0;
#else
    return 0;
#endif
}

namespace {

/// Whether this looks like a mobile browser, for the one decision that
/// wants it: how low to start a ceiling nobody will state. Crude by
/// nature — user agents lie (an iPad claims to be a Mac) — so touch
/// support backs the UA test up, and the answer is only ever a
/// starting guess.
bool looksMobile()
{
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({
        try {
            if (navigator.userAgentData
                    && navigator.userAgentData.mobile !== undefined)
                return navigator.userAgentData.mobile ? 1 : 0;
            var mobileUa = /Mobi|Android|iPhone|iPad/.test(navigator.userAgent);
            var touch = 'ontouchstart' in window
                && (navigator.maxTouchPoints || 0) > 1;
            return (mobileUa || touch) ? 1 : 0;
        } catch (e) {
            return 0;
        }
    }) != 0;
#else
    return false;
#endif
}

}  // namespace

void MemoryBudget::reset(size_t explicitBytes)
{
    m_pinned = explicitBytes != 0;
    m_expansion = 0.0f;
    m_baseHeap = 0;
    m_prevPayload = 0;
    m_prevNet = 0;
    m_sumPayload = 0.0;
    m_sumNet = 0.0;
    m_havePrev = false;
    if (m_pinned) {
        m_budget = explicitBytes;
        m_ceiling = 0;
        return;
    }
    size_t system = systemMemory();
    // Where a device says how much memory it has and that is less than
    // the address space, believe the device: a phone whose wasm heap may
    // grow to 2 GB does not have 2 GB to give.
    const size_t hint = deviceHint();
    if (hint)
        system = system ? std::min(system, hint) : hint;
    m_ceiling = size_t(float(system) * kCeilingShare);
    // A phone that will not say is assumed poor, not rich: the growth
    // cap alone would put the ceiling in the hundreds of megabytes on
    // exactly the platform (Safari) where the OS kills the tab first
    // and signals nothing.
    if (!hint && looksMobile())
        m_ceiling = m_ceiling ? std::min(m_ceiling, kMobileNoHintCeiling)
                              : kMobileNoHintCeiling;
    // Before anything has been measured the budget is a guess at what
    // the payloads for that heap would be, at no expansion at all. The
    // first heartbeats correct it in whichever direction is real.
    m_budget = std::min(m_ceiling ? m_ceiling : kMaxGuess, kMaxGuess);
    m_budget = std::max(m_budget, kMinBudget);
}

void MemoryBudget::observe(size_t payloadBytes, size_t rawBytes,
                           size_t heapBytes)
{
    if (m_pinned || !m_ceiling)
        return;
    // The heap net of the raw payloads, which are held about one for
    // one and are not what "expansion" is asking about.
    const size_t net = heapBytes > rawBytes ? heapBytes - rawBytes : 0;

    // A *slope*, not a ratio from the origin. The tempting estimator —
    // (heap - heap when the scene was empty) / payload — is wrong, and
    // measurably so: the heap before any geometry has arrived is not
    // the fixed cost of running, because the backend's own buffers, the
    // textures and the snapshot structures are all created as the first
    // payloads land. Charging that one-off to the few megabytes
    // resident at the time reported a factor of six on the 200-object
    // scene and had the budget still falling through 4.8 when the load
    // finished. What the budget actually needs is the marginal cost of
    // the *next* megabyte, and that is the slope between two samples.
    // The anchor moves only when a sample is actually drawn from it.
    // Advancing it every heartbeat instead is a subtle way to measure
    // nothing at all: the scene grows about a megabyte between ticks, so
    // each delta falls under the threshold, and re-anchoring means the
    // gap never accumulates to reach it. Observed as an expansion that
    // stayed at zero for a whole load.
    if (!m_havePrev || payloadBytes < m_prevPayload) {
        // First sight, or the ladder ran backwards. Eviction frees
        // payloads without the wasm heap ever shrinking, so a slope
        // measured across it would attribute a fall in geometry to no
        // fall in memory and read as an enormous cost per byte.
        m_havePrev = true;
        m_prevPayload = payloadBytes;
        m_prevNet = net;
        return;
    }
    if (payloadBytes - m_prevPayload < kSlopeMinDelta)
        return;
    // Ratio of decayed sums, not an average of per-sample slopes (see
    // kSlopeWindow): a sample where the heap did not grow contributes
    // its payload bytes to the denominator and nothing to the
    // numerator, which is exactly what it is evidence of, and a slab
    // contributes its whole size whenever it happens to land. The
    // decay is proportional to the payload growth it admits, so the
    // window is measured in scene growth rather than in heartbeats.
    const double dPay = double(payloadBytes - m_prevPayload);
    const double dNet = net > m_prevNet ? double(net - m_prevNet) : 0.0;
    const double keep = std::max(0.0, 1.0 - dPay / kSlopeWindow);
    m_sumPayload = m_sumPayload * keep + dPay;
    m_sumNet = m_sumNet * keep + dNet;
    m_expansion = std::min(std::max(float(m_sumNet / m_sumPayload),
                                    kMinExpansion),
                           kMaxExpansion);
    m_prevPayload = payloadBytes;
    m_prevNet = net;
    if (m_expansion <= 0.0f)
        return;

    // With a slope in hand the intercept follows: what the process
    // occupies that is *not* proportional to geometry. Derived rather
    // than sampled, so it includes everything created along the way.
    const float fixed = float(net) - m_expansion * float(payloadBytes);
    m_baseHeap = size_t(std::max(fixed, 0.0f));

    // What the ceiling leaves for geometry, converted from heap bytes
    // into the payload bytes the ladder actually ranks and bounds. The
    // cache is subtracted as it actually stands rather than at its own
    // bound: it is prunable, so charging the budget its worst case
    // would keep a scene coarse to protect memory nothing is using.
    const size_t overhead = m_baseHeap + rawBytes;
    if (m_ceiling <= overhead) {
        // The process is already over its share before drawing
        // anything. Nothing to do but hold the floor and let eviction
        // keep the scene coarse.
        m_budget = kMinBudget;
        return;
    }
    const float room = float(m_ceiling - overhead) / m_expansion;
    m_budget = std::max(size_t(room), kMinBudget);
}

void MemoryBudget::observeCeiling(size_t heapBytes)
{
    if (m_pinned || heapBytes == 0)
        return;
    // The wall is *at* the observation, not near it, and a second
    // observation may only lower the ceiling: memory that failed once
    // is not un-failed by a later success.
    const size_t observed = size_t(float(heapBytes) * kObservedCeilingShare);
    m_ceiling = m_ceiling ? std::min(m_ceiling, observed) : observed;
    // Cut the budget now rather than at the next heartbeat: the next
    // allocation is what walks into the wall, and eviction needs to be
    // making room before it. The conversion uses what has been
    // measured; before any measurement, assume the worst that is not
    // yet disproven rather than the best.
    const float expansion = m_expansion > 0.0f ? m_expansion : 1.0f;
    const size_t base = m_ceiling > m_baseHeap ? m_ceiling - m_baseHeap : 0;
    m_budget = std::min(m_budget,
                        std::max(size_t(float(base) / expansion),
                                 kMinBudget));
}

// ----------------------------------------------------------------------
// RungRanker
// ----------------------------------------------------------------------

RungRanker::RungRanker(const LadderView &view, LadderBounds bounds,
                       const LadderWeights &weights)
    : m_view(view)
    , m_bounds(std::move(bounds))
    , m_weights(weights)
{
    sub3(m_view.at, m_view.eye, m_fwd);
    normalize3(m_fwd);
    m_tanHalfFov = std::tan(0.5f * m_view.fovY * kPi / 180.0f);
    if (!(m_view.aspect > 0.0f))
        m_view.aspect = 1.0f;
}

float RungRanker::project(const float *bbox) const
{
    const float center[3] = {0.5f * (bbox[0] + bbox[3]),
                             0.5f * (bbox[1] + bbox[4]),
                             0.5f * (bbox[2] + bbox[5])};
    const float extent[3] = {bbox[3] - bbox[0],
                             bbox[4] - bbox[1],
                             bbox[5] - bbox[2]};
    const float radius = 0.5f * length3(extent);
    float rel[3];
    sub3(center, m_view.eye, rel);
    const float along = dot3(rel, m_fwd);
    // Clamped at the object's own radius: something at or behind the
    // eye is not a hundred times more urgent than something in front of
    // it, it is off-screen, which the penalty below says.
    const float dist = std::max(along, radius + 1e-4f);
    const float score = radius / dist;
    const float halfV = m_tanHalfFov * std::max(along, 0.0f);
    // LadderView::right may be the negation of the camera's right axis,
    // which does not matter to a symmetric test.
    const bool onScreen = along + radius > 0.0f
        && std::fabs(dot3(rel, m_view.right)) <= halfV * m_view.aspect + radius
        && std::fabs(dot3(rel, m_view.up)) <= halfV + radius;
    return onScreen ? score : score * 1e-3f;
}

float RungRanker::owner(uint64_t objectKey)
{
    auto memoIt = m_memo.find(objectKey);
    if (memoIt != m_memo.end())
        return memoIt->second;
    float score = 0.0f;
    if (m_bounds) {
        if (const float *bbox = m_bounds(objectKey))
            score = project(bbox);
    }
    m_memo.emplace(objectKey, score);
    return score;
}

float RungRanker::value(const SceneSnapshot::DeferredChunk &chunk,
                        float sizeWeight)
{
    if (chunk.owners.empty()) {
        // The root's own sections and the overlay feeds, which no
        // object claims: they are what names the rest, and the
        // navigation cube is wanted before any of the model.
        return std::numeric_limits<float>::max();
    }
    float best = 0.0f;
    for (uint64_t key : chunk.owners)
        best = std::max(best, owner(key));
    const float bytes = float(std::max<uint32_t>(chunk.size, 1));
    if (sizeWeight <= 0.0f)
        return best;
    // The exact power at 1 is just the bytes; anything else — below
    // *or* above 1 — is honored as stated, so the ?fetchweight= knob
    // never silently saturates.
    return best / (sizeWeight == 1.0f ? bytes : std::pow(bytes, sizeWeight));
}

float RungRanker::acquisition(const SceneSnapshot::DeferredChunk &chunk)
{
    return value(chunk, m_weights.acquire);
}

float RungRanker::acquisition(const SceneSnapshot::DeferredChunk &chunk,
                              uint32_t bytes)
{
    if (chunk.owners.empty())
        return std::numeric_limits<float>::max();
    float best = 0.0f;
    for (uint64_t key : chunk.owners)
        best = std::max(best, owner(key));
    const float weight = m_weights.acquire;
    const float b = float(std::max<uint32_t>(bytes, 1));
    if (weight <= 0.0f)
        return best;
    return best / (weight == 1.0f ? b : std::pow(b, weight));
}

float RungRanker::residency(const SceneSnapshot::DeferredChunk &chunk)
{
    return value(chunk, m_weights.keep);
}

// ----------------------------------------------------------------------
// The plan (docs/SceneStreaming.md §7, "Selection is a plan, not a
// reaction")
// ----------------------------------------------------------------------

size_t Render::planRungs(const SceneSnapshot::DeferredChunk &entry)
{
    return entry.levels.empty() ? 1 : entry.levels.size();
}

const std::string &Render::planRungKey(
    const SceneSnapshot::DeferredChunk &entry, size_t rung)
{
    return entry.levels.empty() ? entry.key : entry.levels[rung].key;
}

uint32_t Render::planRungSize(const SceneSnapshot::DeferredChunk &entry,
                              size_t rung)
{
    return entry.levels.empty() ? entry.size : entry.levels[rung].size;
}

float Render::planRungError(const SceneSnapshot::DeferredChunk &entry,
                            size_t rung)
{
    return entry.levels.empty() ? 0.0f : entry.levels[rung].error;
}

int Render::finestBuiltRung(const SceneSnapshot::DeferredChunk &entry)
{
    for (size_t i = planRungs(entry); i-- > 0;) {
        if (!planRungKey(entry, i).empty())
            return int(i);
    }
    return -1;
}

int Render::coarsestBuiltRung(const SceneSnapshot::DeferredChunk &entry)
{
    const size_t count = planRungs(entry);
    for (size_t i = 0; i < count; ++i) {
        if (!planRungKey(entry, i).empty())
            return int(i);
    }
    return -1;
}

namespace {

/// What holding rung \a rung would cost, estimated where it has to be:
/// an unbuilt rung has no bytes yet, but the generator's grid halves
/// its cell per level, so triangle count — and payload — scale about
/// four to one per step from the nearest built rung. An estimate in
/// the budget is corrected by a re-plan the moment the announcement
/// states the real size; what it must not do is let an unpriced rung
/// ride into the plan for free.
size_t rungCost(const Render::SceneSnapshot::DeferredChunk &entry, int rung)
{
    if (rung < 0)
        return 0;
    const uint32_t stated = Render::planRungSize(entry, size_t(rung));
    if (stated)
        return stated;
    int built = -1;
    for (int i = rung; i-- > 0;) {
        if (!Render::planRungKey(entry, size_t(i)).empty()) {
            built = i;
            break;
        }
    }
    bool builtIsCoarser = built >= 0;
    if (built < 0) {
        for (size_t i = size_t(rung) + 1; i < Render::planRungs(entry); ++i) {
            if (!Render::planRungKey(entry, i).empty()) {
                built = int(i);
                break;
            }
        }
    }
    if (built < 0)
        return 0;
    const size_t steps = size_t(builtIsCoarser ? rung - built : built - rung);
    const size_t base = Render::planRungSize(entry, size_t(built));
    // Shifts capped well below overflow; a ladder is a handful of rungs.
    const size_t scale = size_t(1) << (2 * std::min<size_t>(steps, 12));
    return builtIsCoarser ? base * scale : std::max<size_t>(base / scale, 1);
}

/// The screen-space error of an object drawn as its box: the whole of
/// it is wrong, so the relative error is the whole diagonal. What
/// matters is only that it is larger than any declared rung's error,
/// so the first upgrade off the box is always worth something.
constexpr float kBoxError = 1.0f;

/// How much an upgrade the PREVIOUS plan already granted outranks a
/// newcomer of equal worth. Determinism alone is not hysteresis once
/// the input drifts: a camera settling off a touch fling moves by
/// accumulated epsilons, each replan reorders the greedy heap by a
/// fraction of a percent, and the objects at the budget's knapsack
/// boundary flip between exact and coarse forever — measured on a
/// phone as the same three objects refetching a 155 KB exact mesh and
/// its coarse rung alternately every few seconds, stationary. The
/// incumbent keeps its grant unless a genuinely different camera
/// outbids it by this margin.
constexpr float kPlanKeepBonus = 1.3f;

/// How far past the boundary tier's error — the coarser tier a fresh
/// cut would grant — the desired error must drift before the plan
/// lets an object DOWN off a finer grant. The tier cut is a
/// threshold on a continuous input — tolerancePx over a projected
/// diameter that changes with every camera epsilon — so an object
/// sitting exactly at a boundary flips between two rungs as the camera
/// drifts, and each flip is a fetch round-trip, a parse and a GPU
/// upload (measured: 2695 asks against 2716 releases in under a minute
/// of orbiting, the same rungs over and over). A quarter of margin
/// keeps the finer tier through drift; a camera that genuinely pulled
/// back still downgrades.
constexpr float kPlanDowngradeMargin = 1.25f;

}  // namespace

int Render::rungWithin(const SceneSnapshot::DeferredChunk &entry, float err)
{
    const size_t count = planRungs(entry);
    for (size_t i = 0; i < count; ++i) {
        if (planRungError(entry, i) <= err)
            return int(i);
    }
    return int(count) - 1;
}

PlanStats Render::planLevels(SceneSnapshot &snap, RungRanker &ranker,
                             const PlanParams &params)
{
    PlanStats stats;

    /// One object's slice of the plan: the entries it owns, its
    /// projected diameter in pixels, and the descending error tiers
    /// its ladders offer — cut off at the first tier the camera could
    /// not tell from exact.
    struct Obj {
        std::vector<size_t> entries;
        float diamPx = 0.0f;
        std::vector<float> tiers;
        /// Index into \a tiers applied so far; -1 is the box.
        int tier = -1;
        bool done = false;
    };

    // Owned geometry is planned; the view's own geometry always
    // targets its finest built rung, outside the budget, because a
    // navigation cube refused for want of memory is a verdict nothing
    // can appeal (it can never be a victim either).
    std::map<uint64_t, Obj> objs;
    const int kUntouched = std::numeric_limits<int>::min();
    std::vector<int> plan(snap.deferredChunks.size(), kUntouched);
    /// What the previous plan granted, per entry, before this one
    /// overwrites it — the incumbency the greedy honors.
    std::vector<int> prev(snap.deferredChunks.size(),
                          int(SceneSnapshot::DeferredChunk::kPlanBox));
    for (size_t i = 0; i < snap.deferredChunks.size(); ++i) {
        auto &entry = snap.deferredChunks[i];
        if (!entry.release)
            continue;
        if (entry.owners.empty()) {
            entry.plan = int16_t(finestBuiltRung(entry));
            continue;
        }
        plan[i] = -1;
        if (entry.plan != SceneSnapshot::DeferredChunk::kPlanUnset)
            prev[i] = int(entry.plan);
        for (uint64_t owner : entry.owners)
            objs[owner].entries.push_back(i);
    }

    // project() scores angular size — radius over distance. The
    // viewport's half height spans tan(fovY/2) of the same measure, so
    // an owner's projected diameter in pixels is score / tan(fovY/2) ×
    // the viewport height; a rung's error is relative to the diagonal,
    // which is that diameter.
    const float tanHalf =
        std::tan(ranker.view().fovY * 0.5f * kPi / 180.0f);
    const bool pixelsKnown = tanHalf > 0.0f && params.viewportPx > 0.0f;
    for (auto &kv : objs) {
        Obj &o = kv.second;
        std::sort(o.entries.begin(), o.entries.end());
        o.entries.erase(std::unique(o.entries.begin(), o.entries.end()),
                        o.entries.end());
        o.diamPx = pixelsKnown
            ? ranker.owner(kv.first) / tanHalf * params.viewportPx
            : 0.0f;
        // The coarsest error the camera cannot resolve at the
        // tolerance. Non-positive tolerance, or an object whose size
        // on screen is unknown, desires exact — the answer that is
        // right whatever the camera turns out to see.
        float desired =
            (params.tolerancePx > 0.0f && o.diamPx > 0.0f)
            ? params.tolerancePx / o.diamPx
            : 0.0f;
        // The candidate tiers, finest ladder first: the union of the
        // owned ladders' errors, descending, cut after the first tier
        // within the desired error — beyond it a byte buys nothing
        // visible.
        std::set<float, std::greater<float>> tiers;
        for (size_t idx : o.entries) {
            const auto &entry = snap.deferredChunks[idx];
            for (size_t r = 0; r < planRungs(entry); ++r)
                tiers.insert(planRungError(entry, r));
        }
        // Hysteresis on the tier cut (kPlanDowngradeMargin): the
        // incoming objectErr map still holds the PREVIOUS plan's
        // grants, and an object it placed on a finer tier than a
        // fresh cut would grant only steps down once the desired
        // error clears the coarser tier's — the boundary being
        // crossed — by the margin. Measured against the boundary
        // rather than the old grant, because the commonest flip is
        // exact (error 0) against the first coarse tier, and no
        // multiple of zero is a margin. Only downgrades are damped —
        // an upgrade is the camera actually asking for more.
        if (params.objectErr && desired > 0.0f) {
            auto held = params.objectErr->find(kv.first);
            if (held != params.objectErr->end() && held->second >= 0.0f
                && held->second < desired) {
                float boundary = -1.0f;
                for (float t : tiers) {
                    if (t <= desired) {
                        boundary = t;
                        break;
                    }
                }
                if (boundary > held->second
                    && desired <= boundary * kPlanDowngradeMargin)
                    desired = held->second;
            }
        }
        for (float t : tiers) {
            o.tiers.push_back(t);
            if (t <= desired)
                break;
        }
    }
    stats.objects = objs.size();

    // Greedy: the best screen-space error removed per byte added, one
    // upgrade at a time, until the budget is spent or every object is
    // at its desired tier. The heap is lazy — a shared chunk another
    // owner already raised makes an upgrade cheaper than its queued
    // score claims — so a popped candidate is re-scored fresh and
    // re-queued if it got better; it can never have gotten worse.
    const auto tierCost = [&](const Obj &o, float err) {
        size_t cost = 0;
        for (size_t idx : o.entries) {
            const auto &entry = snap.deferredChunks[idx];
            const int rung = rungWithin(entry, err);
            if (rung > plan[idx])
                cost += rungCost(entry, rung)
                    - (plan[idx] >= 0 ? rungCost(entry, plan[idx]) : 0);
        }
        return cost;
    };
    const auto tierScore = [&](const Obj &o) {
        const float from = o.tier < 0 ? kBoxError : o.tiers[o.tier];
        const float to = o.tiers[o.tier + 1];
        const float diam = std::max(o.diamPx, 1e-6f);
        float gain = (from - to) * diam;
        // Weighted by the error the object is committing NOW, floored
        // at a pixel. Plain gain per byte minimizes the error TOTAL,
        // and a coalition of mid objects with cheap upgrades outbids
        // the one huge foreground object whose next rung is expensive
        // — measured as a 1300 px sphere held coarse at 8 MB of 8
        // while mid-field objects polished. The weight makes the score
        // convex in the standing error, so the worst-off object's
        // upgrades rank first and the plan spends toward the smallest
        // WORST error, not the smallest sum.
        gain *= std::max(from * diam, 1.0f);
        // Incumbency (kPlanKeepBonus): this upgrade only re-affirms
        // what the previous plan already granted every chunk it
        // touches, so under input drift it outranks an equal-worth
        // newcomer and the boundary set stays put.
        bool incumbent = true;
        for (size_t idx : o.entries) {
            if (rungWithin(snap.deferredChunks[idx], to) > prev[idx]) {
                incumbent = false;
                break;
            }
        }
        if (incumbent)
            gain *= kPlanKeepBonus;
        const size_t cost = tierCost(o, to);
        return cost ? gain / float(cost) : std::numeric_limits<float>::max();
    };

    // Ordered by score, ties broken by object key: the same inputs
    // must yield the same plan, or the plan oscillates with itself.
    std::priority_queue<std::pair<float, uint64_t>> heap;
    size_t spent = 0;

    // The floor: every object holds its coarsest tier before any
    // object holds a finer one. Eviction demotes to coarse, not to
    // nothing — an object stripped to its box to fund someone else's
    // exact mesh is invisible the moment the camera pans it back in,
    // and the camera pans far more often than the budget genuinely
    // starves. A coarsest rung is a few kilobytes; only when even
    // those do not fit does the floor itself start dropping objects,
    // smallest on screen first.
    {
        std::vector<std::pair<float, uint64_t>> order;
        order.reserve(objs.size());
        for (auto &kv : objs) {
            if (!kv.second.tiers.empty() && kv.second.tier < 0)
                order.emplace_back(kv.second.diamPx, kv.first);
        }
        std::sort(order.begin(), order.end(),
                  [](const std::pair<float, uint64_t> &a,
                     const std::pair<float, uint64_t> &b) {
                      return a.first != b.first ? a.first > b.first
                                                : a.second < b.second;
                  });
        for (const auto &item : order) {
            Obj &o = objs[item.second];
            const float err = o.tiers[0];
            const size_t cost = tierCost(o, err);
            if (params.budgetBytes && spent + cost > params.budgetBytes) {
                o.done = true;
                ++stats.capped;
                continue;
            }
            for (size_t idx : o.entries) {
                const auto &entry = snap.deferredChunks[idx];
                const int rung = rungWithin(entry, err);
                if (rung > plan[idx])
                    plan[idx] = rung;
            }
            spent += cost;
            o.tier = 0;
        }
    }

    for (auto &kv : objs) {
        if (!kv.second.done
            && kv.second.tier + 1 < int(kv.second.tiers.size()))
            heap.emplace(tierScore(kv.second), kv.first);
    }
    while (!heap.empty()) {
        const auto top = heap.top();
        heap.pop();
        Obj &o = objs[top.second];
        if (o.done || o.tier + 1 >= int(o.tiers.size()))
            continue;
        const float fresh = tierScore(o);
        if (fresh > top.first) {
            // Cheaper than when queued (a shared chunk was raised):
            // let it compete at its real score.
            heap.emplace(fresh, top.second);
            continue;
        }
        const float err = o.tiers[o.tier + 1];
        const size_t cost = tierCost(o, err);
        if (params.budgetBytes && spent + cost > params.budgetBytes) {
            // This object stops here; smaller upgrades elsewhere may
            // still fit, so the round goes on without it.
            o.done = true;
            ++stats.capped;
            continue;
        }
        for (size_t idx : o.entries) {
            const auto &entry = snap.deferredChunks[idx];
            const int rung = rungWithin(entry, err);
            if (rung > plan[idx])
                plan[idx] = rung;
        }
        spent += cost;
        ++o.tier;
        if (o.tier + 1 < int(o.tiers.size()))
            heap.emplace(tierScore(o), top.second);
    }
    stats.plannedBytes = spent;

    if (params.trace) {
        for (auto &kv : objs)
            params.trace(kv.first, kv.second.diamPx, kv.second.tier,
                         kv.second.tiers.size());
    }
    if (params.objectErr) {
        params.objectErr->clear();
        for (auto &kv : objs) {
            (*params.objectErr)[kv.first] =
                kv.second.tier < 0 ? -1.0f : kv.second.tiers[kv.second.tier];
        }
    }

    for (size_t i = 0; i < snap.deferredChunks.size(); ++i) {
        if (plan[i] != kUntouched)
            snap.deferredChunks[i].plan = int16_t(plan[i]);
    }
    return stats;
}

uint16_t Render::planNeeded(const SceneSnapshot::DeferredChunk &entry,
                            const std::function<float(uint64_t)> &errOf)
{
    if (!entry.release)
        return 0;
    const auto builtBit = [&entry](int rung) -> uint16_t {
        return rung >= 0 && rung < 16 ? uint16_t(1u << rung) : 0;
    };
    if (entry.owners.empty()) {
        // The view's own geometry always stands at its finest built
        // rung, outside the budget (§7).
        return builtBit(finestBuiltRung(entry));
    }
    uint16_t mask = 0;
    for (uint64_t owner : entry.owners) {
        const float err = errOf ? errOf(owner)
                                : std::numeric_limits<float>::quiet_NaN();
        if (std::isnan(err)) {
            // The plan has not seen this owner: the pre-plan default
            // is the finest built rung, exactly what an unplanned
            // entry targeted before there were per-owner tiers.
            mask |= builtBit(finestBuiltRung(entry));
        }
        else if (err >= 0.0f) {
            mask |= builtBit(rungWithin(entry, err));
        }
        // err < 0: the plan left this owner on its box — it needs no
        // rung, and contributes nothing.
    }
    return mask;
}

PlanStep Render::planStep(const SceneSnapshot::DeferredChunk &entry,
                          uint16_t neededMask)
{
    PlanStep step;
    if (!entry.release)
        return step;
    const int count = std::min(int(planRungs(entry)), 16);
    const uint16_t resident = entry.residentMask;
    if (!neededMask) {
        // Every owner is on its box: everything held is surplus.
        step.surplus = resident;
        return step;
    }
    // Fetch the coarsest needed rung not yet held; an unbuilt needed
    // rung is the producer's to make (generate) while the search goes
    // on for one that can be fetched today.
    int fetch = -1;
    for (int r = 0; r < count; ++r) {
        const uint16_t bit = uint16_t(1u << r);
        if (!(neededMask & bit) || (resident & bit))
            continue;
        if (planRungKey(entry, size_t(r)).empty()) {
            if (step.generate < 0)
                step.generate = r;
            continue;
        }
        fetch = r;
        break;
    }
    // En route, coarse first (§7): nothing on screen yet and the rung
    // to fetch is not the cheapest built one — put the cheapest up
    // this round, and the target lands as an upgrade over it instead
    // of over a box. Also the stand-in when every needed rung is
    // unbuilt: a generate's answer is an announcement, and the object
    // should not be a box while it waits.
    if (resident == 0) {
        const int coarsest = coarsestBuiltRung(entry);
        if (coarsest >= 0 && (fetch < 0 || coarsest < fetch))
            fetch = coarsest;
    }
    if (fetch >= 0 && !(resident & uint16_t(1u << fetch)))
        step.fetch = fetch;
    // A resident rung nobody needs counts as surplus only once every
    // needed rung is resident: until then it is the stand-in some
    // owner is drawing. An unbuilt needed rung keeps the hold by
    // construction — it cannot be resident yet.
    if ((neededMask & resident) == neededMask)
        step.surplus = uint16_t(resident & ~neededMask);
    return step;
}

namespace {

/// One draw's bounding box against one camera — the shared math of the
/// desktop plan passes (refine and demote read the same projection,
/// they just act on opposite sides of the tolerance).
///
/// The arithmetic itself moved to ProxyHierarchy.h, where the far-field
/// cut is its third consumer: the cut and the coverage histogram have
/// to agree about "small on screen" by construction rather than by two
/// copies of one formula staying in step (docs/FarFieldProxies.md §3.3).
BoxSight sightBox(const Render::DrawCall &draw, const float *V,
                  const float *P, float viewportHeightPx)
{
    return sightBounds(draw.bboxMin, draw.bboxMax, V, P, viewportHeightPx);
}

}  // namespace

namespace {

/// The bounds one plan verdict stands on: the union box of every
/// tagged draw sharing the draw's objectKey, or the draw's own box
/// when it has no object identity (objectKey 0 — the tests' case, and
/// any feed that never filled keys).
///
/// Judging per DRAW was the churn bug this exists to fix: an object's
/// face and edge draws carry different boxes — an ellipsoid's seam
/// edge is one meridian, a sliver that can sit off-screen while the
/// body fills the view — and since the face and line sources of one
/// object share their refine/demote action, opposite verdicts made
/// every plan cycle downgrade through the edge tag and re-refine
/// through the face tag, a full rebuild each way, forever. One box
/// per object gives every tag of the object the same verdict.
struct PlanBoxes {
    std::map<uint64_t, Render::DrawCall> objectBox;

    explicit PlanBoxes(const Render::DrawCallList &draws)
    {
        for (const auto &draw : draws) {
            if (!draw.mesh || !draw.mesh->sourceTag || !draw.objectKey)
                continue;
            if (draw.bboxMin[0] > draw.bboxMax[0])
                continue;
            auto res = objectBox.try_emplace(draw.objectKey, draw);
            if (res.second)
                continue;
            Render::DrawCall &box = res.first->second;
            for (int i = 0; i < 3; ++i) {
                box.bboxMin[i] = std::min(box.bboxMin[i],
                                          draw.bboxMin[i]);
                box.bboxMax[i] = std::max(box.bboxMax[i],
                                          draw.bboxMax[i]);
            }
        }
    }

    BoxSight sight(const Render::DrawCall &draw, const float *view,
                   const float *proj, float viewportHeightPx) const
    {
        auto it = objectBox.find(draw.objectKey);
        const Render::DrawCall &box =
            it != objectBox.end() ? it->second : draw;
        return sightBox(box, view, proj, viewportHeightPx);
    }
};

}  // namespace

std::vector<const void *> Render::planMeshRefines(
    const DrawCallList &draws, const float *viewMatrix,
    const float *projMatrix, float viewportHeightPx, float tolerancePx)
{
    std::vector<const void *> out;
    if (!viewMatrix || !projMatrix || viewportHeightPx <= 0.0f)
        return out;
    const PlanBoxes boxes(draws);
    // A tag is wanted when ANY draw carrying it wants finer — a shared
    // (instanced) source refines for its neediest owner.
    std::set<const void *> wanted, seen;
    for (const auto &draw : draws) {
        if (!draw.mesh)
            continue;
        const MeshData &mesh = *draw.mesh;
        if (!mesh.sourceTag || mesh.levelError <= 0.0f)
            continue;
        if (wanted.count(mesh.sourceTag))
            continue;
        if (tolerancePx <= 0.0f) {
            // "Every object desires its exact content" — the step-1
            // behavior, and the same reading as PlanParams::tolerancePx
            // — except a draw with no judgeable bounds at all.
            if (draw.bboxMin[0] > draw.bboxMax[0])
                continue;
        }
        else {
            const BoxSight sight = boxes.sight(draw, viewMatrix,
                                               projMatrix,
                                               viewportHeightPx);
            // Off-screen never refines — the residency bill this pass
            // exists to stop paying; the camera inside the box span
            // refines outright.
            //
            // The error is bounded by the SCREEN before it is compared,
            // and that is not cosmetic. The tolerance this pass runs at
            // under memory pressure is the error the descent had to
            // accept (sec 13c.3), and that raise is bounded by the
            // viewport height -- an error of a million pixels and one
            // of the screen height being the same statement. If the
            // comparison here were not bounded by the same thing, the
            // two passes would be speaking different currencies again,
            // and the ladder would ask straight back for exactly what
            // it had just given up. Measured on the rack model: with
            // the tolerance at its 600px bound and descents accepting
            // 3531, 6324 and 16211px, single plans asked 386, 546 and
            // 950 objects to refine while the budget was still broken.
            const float bound = std::max(1.0f, viewportHeightPx);
            const bool want = sight.what == BoxSight::Inside
                || (sight.what == BoxSight::Visible
                    && std::min(mesh.levelError * sight.diagPx, bound)
                           > tolerancePx);
            if (!want)
                continue;
        }
        wanted.insert(mesh.sourceTag);
        if (!seen.count(mesh.sourceTag)) {
            seen.insert(mesh.sourceTag);
            out.push_back(mesh.sourceTag);
        }
    }
    return out;
}

float Render::PressureTolerance::update(bool underPressure, float acceptedPx,
                                        float cameraTolPx, float viewportPx,
                                        float releaseFraction)
{
    // Below this the raise says nothing: the climb runs at
    // max(cameraTolPx, raisedPx / kPlanDemoteMargin), so a raised error
    // under the camera's own tolerance times the margin is already the
    // camera's number.
    const float spent = std::max(0.0f, cameraTolPx) * kPlanDemoteMargin;
    const float frac = releaseFraction > 0.0f
        ? std::min(releaseFraction, 0.99f) : 0.0f;

    if (underPressure) {
        // Pressure back after a release: the level released TO is proven
        // too generous, whatever else this plan does. That is the whole
        // of what the loop learns, and it is learned from the one event
        // that can teach it. Asked of gaveBack rather than of
        // `releasing`, because the refines a release asked for land
        // plans later -- by then the staircase has usually stopped, and
        // a controller that only learned mid-step would re-try the
        // level that failed forever.
        const float culprit = raisedPx > 0.0f ? raisedPx : lastStepPx;
        if (gaveBack && culprit > 0.0f)
            floorPx = std::max(floorPx, culprit);
        releasing = false;
        gaveBack = false;
        if (std::isfinite(acceptedPx))
            raisedPx = std::max(raisedPx,
                                std::min(acceptedPx,
                                         std::max(1.0f, viewportPx)));
    }
    else if (raisedPx > 0.0f) {
        if (frac <= 0.0f) {
            // The old behaviour, kept reachable so an arm can measure
            // the defect rather than argue about it: everything given
            // back at once, the moment one plan comes in under budget.
            raisedPx = 0.0f;
            releasing = false;
        }
        else {
            const float next = raisedPx * frac;
            if (next <= spent && floorPx <= spent) {
                // Nothing left to hold back: the view fits with room,
                // and the camera's tolerance rules again. The level it
                // let go of is still remembered, because THIS is the
                // step a returning pressure would be blaming, and a
                // controller that learned 0 from it would walk the
                // whole staircase down again next time.
                lastStepPx = raisedPx;
                raisedPx = 0.0f;
                releasing = gaveBack = true;
            }
            else if (next <= floorPx) {
                // Releasing this far is measured to break the budget.
                // Stopping here IS the equilibrium -- holding some
                // error is what fitting costs on this scene.
                releasing = false;
            }
            else {
                raisedPx = lastStepPx = next;
                releasing = gaveBack = true;
            }
        }
    }
    else
        releasing = false;

    return raisedPx > 0.0f
        ? std::max(cameraTolPx, raisedPx / kPlanDemoteMargin)
        : cameraTolPx;
}

int Render::CoverageHistogram::atOrUnder(float px) const
{
    int n = 0;
    for (int i = 0; i < kBuckets - 1; ++i) {
        if (kEdges[i] <= px)
            n += counts[i];
    }
    return n;
}

Render::CoverageHistogram Render::coverageHistogram(const DrawCallList &draws,
                                                    const float *viewMatrix,
                                                    const float *projMatrix,
                                                    float viewportHeightPx)
{
    CoverageHistogram out;
    if (!viewMatrix || !projMatrix || viewportHeightPx <= 0.0f)
        return out;
    // Per object, not per draw. One part can draw several times — opaque
    // and transparent, faces and lines — and still costs one object's
    // worth of the overhead this measurement is about, so counting draws
    // would inflate exactly the number in question.
    std::map<uint64_t, DrawCall> boxes;
    std::vector<DrawCall> anonymous;
    for (const auto &draw : draws) {
        if (draw.bboxMin[0] > draw.bboxMax[0]) {
            ++out.noBounds;
            continue;
        }
        if (!draw.objectKey) {
            // No identity to merge on: judge it on its own rather than
            // dropping it, so overlays and one-off geometry still show up.
            anonymous.push_back(draw);
            continue;
        }
        auto res = boxes.try_emplace(draw.objectKey, draw);
        if (res.second)
            continue;
        DrawCall &box = res.first->second;
        for (int i = 0; i < 3; ++i) {
            box.bboxMin[i] = std::min(box.bboxMin[i], draw.bboxMin[i]);
            box.bboxMax[i] = std::max(box.bboxMax[i], draw.bboxMax[i]);
        }
    }

    const auto tally = [&](const DrawCall &box) {
        ++out.total;
        const BoxSight sight = sightBox(box, viewMatrix, projMatrix,
                                        viewportHeightPx);
        switch (sight.what) {
        case BoxSight::Empty:
            ++out.noBounds;
            return;
        case BoxSight::Offscreen:
            ++out.offScreen;
            return;
        case BoxSight::Inside:
            // The camera is inside this object's span: it is as large as
            // an object gets, whatever the diagonal projects to.
            ++out.counts[CoverageHistogram::kBuckets - 1];
            return;
        case BoxSight::Visible:
            break;
        }
        for (int i = 0; i < CoverageHistogram::kBuckets - 1; ++i) {
            if (sight.diagPx <= CoverageHistogram::kEdges[i]) {
                ++out.counts[i];
                return;
            }
        }
        ++out.counts[CoverageHistogram::kBuckets - 1];
    };
    for (const auto &entry : boxes)
        tally(entry.second);
    for (const auto &box : anonymous)
        tally(box);
    return out;
}

namespace
{

/// One demotable source, priced: what dropping it would show and what
/// it would give back. Accumulated over every draw carrying the tag,
/// because the answer belongs to the source and the draws are only how
/// it appears on screen.
struct DemoteCandidate {
    const void *tag = nullptr;
    /// The coarse rung's error relative to the shape diagonal, as the
    /// registry answered it -- asked once per source, not once per draw.
    float coarseErr = 0.0f;
    /// Largest projected coarse error over the source's owners, in
    /// pixels -- what the neediest instance would show.
    float errPx = 0.0f;
    /// Resident bytes, per distinct mesh (see meshResidentBytes).
    uint64_t bytes = 0;
    /// Some owner is on screen: an all-off-screen source is free.
    bool visible = false;
    /// Some owner could not be judged (no bounds, or the camera inside
    /// the box, where projected size is meaningless). Unpriceable, so
    /// never dropped at any pressure -- a price nobody can compute is
    /// not a licence to guess it low.
    bool unpriceable = false;
};

}  // namespace

std::vector<const void *> Render::planMeshDemotes(
    const DrawCallList &draws, const float *viewMatrix,
    const float *projMatrix, float viewportHeightPx, float tolerancePx,
    const std::function<float(const void *)> &demoteErrOf,
    PlanDemoteStats *stats, size_t deficitBytes,
    const std::function<uint64_t(const MeshData *)> &bytesOf)
{
    std::vector<const void *> out;
    if (!viewMatrix || !projMatrix || viewportHeightPx <= 0.0f
        || tolerancePx <= 0.0f || !demoteErrOf)
        return out;
    // The currency this pass prices in (see bytesOf): the CPU arrays by
    // default, the caller's own accounting when it is spending against
    // a different budget.
    const auto price = [&bytesOf](const MeshData *m) -> uint64_t {
        return bytesOf ? bytesOf(m) : uint64_t(Render::meshResidentBytes(m));
    };
    const PlanBoxes boxes(draws);
    // Price every candidate first, decide after: which sources are
    // worth dropping cannot be answered draw by draw once pressure is
    // allowed to widen the selection, because the cheapest source is
    // only known once they have all been seen.
    std::vector<DemoteCandidate> cands;
    std::map<const void *, size_t> index;
    // Bytes belong to a mesh, not to a draw. An instanced source is one
    // upload behind many rows, so the same MeshData is charged once.
    //
    // Keyed by the mesh alone. The tag it used to be paired with is
    // read off that same mesh, so the pair never distinguished
    // anything -- but the key is also the guarantee bytesOf is given
    // ("at most once per distinct mesh"), and a stateful currency
    // charging a shared upload to its first referent depends on it, so
    // it is worth stating in the type rather than deriving.
    std::set<const MeshData *> charged;
    for (const auto &draw : draws) {
        if (!draw.mesh)
            continue;
        const MeshData &mesh = *draw.mesh;
        // Every drawn source is asked, and the registry alone answers
        // whether one has a way down.
        //
        // This used to skip sources already displaying a coarse rung,
        // on the reading that a coarse source is the refine pass's
        // business and has nothing left to give. That is false under a
        // budget the coarse scene itself cannot meet -- measured, the
        // rack model sits at 441MB of coarse geometry against 64MB with
        // every exact rung already surrendered. Rung 0 is not a floor:
        // a coarse source descends by re-tessellating COARSER again,
        // step after step, and the registry's error callback is what
        // says whether a step exists.
        if (!mesh.sourceTag)
            continue;
        auto found = index.find(mesh.sourceTag);
        if (found == index.end()) {
            if (stats)
                ++stats->considered;
            const float coarseErr = demoteErrOf(mesh.sourceTag);
            if (coarseErr <= 0.0f) {
                // A source that armed no descent and a tag nobody owns
                // are separate answers to "why can this not descend".
                // Both are priced, because what decides whether either
                // is worth fixing is the residency standing behind it,
                // not how many there are.
                if (stats) {
                    ++(coarseErr < 0.0f ? stats->unregistered
                                        : stats->noRung);
                    if (charged.emplace(&mesh).second)
                        stats->unreachableBytes += price(&mesh);
                }
                // Remembered as a non-candidate so the registry is
                // asked once per source rather than once per draw.
                index.emplace(mesh.sourceTag, size_t(-1));
                continue;
            }
            found = index.emplace(mesh.sourceTag, cands.size()).first;
            DemoteCandidate cand;
            cand.tag = mesh.sourceTag;
            cand.coarseErr = coarseErr;
            cands.push_back(cand);
        }
        if (found->second == size_t(-1)) {
            // A second mesh under the same unreachable tag still costs
            // its bytes; only the count is per source.
            if (stats && charged.emplace(&mesh).second)
                stats->unreachableBytes += price(&mesh);
            continue;
        }
        DemoteCandidate &cand = cands[found->second];
        if (charged.emplace(&mesh).second)
            cand.bytes += price(&mesh);
        const BoxSight sight = boxes.sight(draw, viewMatrix, projMatrix,
                                           viewportHeightPx);
        if (sight.what == BoxSight::Visible) {
            cand.visible = true;
            cand.errPx = std::max(cand.errPx,
                                  cand.coarseErr * sight.diagPx);
        }
        else if (sight.what != BoxSight::Offscreen)
            cand.unpriceable = true;
    }

    // The free tier: nothing the camera can see changes. Off screen is
    // free outright; on screen only when the coarse rung clears the
    // tolerance by the demote margin -- at the refine boundary itself a
    // drifting camera would trade a full tessellation back and forth
    // across it.
    const float freeErrPx = tolerancePx * kPlanDemoteMargin;
    std::vector<const DemoteCandidate *> priced;
    uint64_t freed = 0;
    float accepted = 0.0f;
    for (const DemoteCandidate &cand : cands) {
        if (cand.unpriceable) {
            if (stats)
                ++stats->unpriceable;
            continue;
        }
        if (cand.visible && cand.errPx > freeErrPx) {
            priced.push_back(&cand);
            continue;
        }
        if (stats)
            ++(cand.visible ? stats->eligible : stats->offscreen);
        out.push_back(cand.tag);
        freed += cand.bytes;
        accepted = std::max(accepted, cand.errPx);
    }

    // The priced tier: visible error, bought only with a deficit and
    // only as much of it as the deficit needs. Cheapest first, so the
    // tolerance rises no further than the budget forces it to.
    std::stable_sort(priced.begin(), priced.end(),
                     [](const DemoteCandidate *a, const DemoteCandidate *b) {
                         return a->errPx < b->errPx;
                     });
    for (const DemoteCandidate *cand : priced) {
        if (freed >= deficitBytes || !cand->bytes) {
            // A source that frees nothing cannot close a deficit, and
            // showing its coarse rung for nothing is a pure loss.
            if (stats)
                ++stats->tooBig;
            continue;
        }
        if (stats)
            ++stats->underPressure;
        out.push_back(cand->tag);
        freed += cand->bytes;
        accepted = std::max(accepted, cand->errPx);
    }
    if (stats) {
        stats->bytesFreed = freed;
        // Reported bounded by the screen, while the SELECTION above ran
        // on the true numbers: the ordering among candidates erring
        // thousands of pixels is real and worth keeping (cheapest
        // first), but what leaves this function is a tolerance that the
        // refine pass will be compared against, and that comparison is
        // screen-bounded (sec 13c.3). An accepted error of 16211px
        // quoted at a pass that can never see more than the viewport
        // height is not a stricter statement, only an unreadable one.
        stats->acceptedErrorPx =
            std::min(accepted, std::max(1.0f, viewportHeightPx));
    }
    return out;
}
