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
        const float desired =
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
        // Every owner is on its box: everything held goes back.
        step.release = resident;
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
    // A resident rung nobody needs goes back only once every needed
    // rung is resident: until then it is the stand-in some owner is
    // drawing. An unbuilt needed rung keeps the hold by construction —
    // it cannot be resident yet.
    if ((neededMask & resident) == neededMask)
        step.release = uint16_t(resident & ~neededMask);
    return step;
}
