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

const float Render::kEvictMargin = 1.25f;

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

float RungRanker::residency(const SceneSnapshot::DeferredChunk &chunk)
{
    return value(chunk, m_weights.keep);
}

// ----------------------------------------------------------------------
// Evictor
// ----------------------------------------------------------------------

Evictor::Evictor(SceneSnapshot &snap, RungRanker &ranker,
                 ResidentTest isResident, Release release)
    : m_snap(snap)
    , m_ranker(ranker)
    , m_isResident(std::move(isResident))
    , m_release(std::move(release))
{}

void Evictor::build()
{
    m_built = true;
    for (size_t i = 0; i < m_snap.deferredChunks.size(); ++i) {
        const auto &entry = m_snap.deferredChunks[i];
        // Still outstanding, or with no rung below it to fall back to,
        // or not actually held: nothing to give back.
        if (entry.fill || !entry.release
                || !(m_isResident && m_isResident(entry.key)))
            continue;
        // The view's own chunks are never victims. Their residency
        // score is infinite anyway, but stating the rule here keeps it
        // a rule rather than an arithmetic accident of FLT_MAX
        // surviving the margin multiply.
        if (entry.owners.empty())
            continue;
        m_victims.emplace_back(m_ranker.residency(entry), i);
    }
    std::sort(m_victims.begin(), m_victims.end(),
              [](const std::pair<float, size_t> &a,
                 const std::pair<float, size_t> &b) {
                  return a.first < b.first;
              });
}

bool Evictor::makeRoom(float incoming, size_t need)
{
    if (!m_built)
        build();
    size_t take = m_next, freed = 0;
    while (take < m_victims.size() && freed < need
           && m_victims[take].first * kEvictMargin < incoming) {
        freed += m_snap.deferredChunks[m_victims[take].second].size;
        ++take;
    }
    if (freed < need) {
        if (m_trace) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "no room for a chunk worth %.3g: need %zu B, "
                          "freed %zu of %zu candidates (best spare %.3g)",
                          incoming, need, freed, m_victims.size() - m_next,
                          m_next < m_victims.size() ? m_victims[m_next].first
                                                    : -1.0f);
            m_trace(buf);
        }
        return false;
    }
    for (; m_next < take; ++m_next) {
        auto &entry = m_snap.deferredChunks[m_victims[m_next].second];
        m_release(entry, m_victims[m_next].first);
    }
    return true;
}
