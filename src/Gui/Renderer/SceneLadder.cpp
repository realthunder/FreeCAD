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
    return best / (sizeWeight >= 1.0f ? bytes : std::pow(bytes, sizeWeight));
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
