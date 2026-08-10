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

#include "CullBenefit.h"

#include <algorithm>

using namespace Render;

void CullBenefitEstimator::configure(const CullBenefitConfig &c)
{
    if (c != conf) {
        conf = c;
        reset();
    }
}

void CullBenefitEstimator::reset()
{
    samples.clear();
    culledMs = 0.0f;
    uncalledMs = 0.0f;
    seen = 0;
    probes = 0;
    haveCulled = false;
    haveUncalled = false;
    probing = true;
    // WARNING: Both the verdict and the current frame start at "do not
    // cull". An estimator that has measured nothing has established
    // nothing, and the untested direction has to be the one that draws
    // too much rather than the one that deletes geometry.
    culling = false;
    cullnow = false;
    armCulled = false;
    lastDecision = 0.0;
}

float CullBenefitEstimator::median(std::vector<float> &v)
{
    if (v.empty())
        return 0.0f;
    // The median and not the mean: a frame that hit a document recompute,
    // a texture upload or the compositor is an outlier of tens of
    // milliseconds, and a mean over 24 frames carries it straight into
    // the verdict.
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + long(mid), v.end());
    return v[mid];
}

void CullBenefitEstimator::startArm(bool culled)
{
    samples.clear();
    seen = 0;
    armCulled = culled;
    cullnow = culled;
}

void CullBenefitEstimator::decide()
{
    ++probes;
    probing = false;
    // The gain is measured against the un-culled arm, so it reads as
    // "the fraction of the frame the culling gives back". Negative when
    // deciding costs more than drawing would have.
    const float gain = uncalledMs > 0.0f
            ? (uncalledMs - culledMs) / uncalledMs
            : 0.0f;
    // Asymmetric thresholds -- see CullBenefitConfig::onMargin. Turning
    // on demands more evidence than staying on, so a verdict sitting
    // near the noise floor holds rather than flapping.
    culling = culling ? (gain > conf.offMargin) : (gain > conf.onMargin);
    cullnow = culling;
}

void CullBenefitEstimator::frame(float cpuMs, double nowSeconds)
{
    if (lastDecision == 0.0)
        lastDecision = nowSeconds;

    if (!probing) {
        // A verdict stands until it is stale. Re-probing matters because
        // every term of the trade is a property of the scene and the
        // camera, and both move.
        if (nowSeconds - lastDecision >= double(conf.reprobeSeconds)) {
            probing = true;
            haveCulled = false;
            haveUncalled = false;
            startArm(true);
        }
        return;
    }

    ++seen;
    // Discard the warm-up. Switching arms changes the draw set, and the
    // frames straight after carry the cost of the change rather than the
    // steady state being asked about.
    if (seen <= conf.warmupFrames)
        return;
    if (cpuMs > 0.0f)
        samples.push_back(cpuMs);
    if (samples.size() < conf.sampleFrames)
        return;

    const float m = median(samples);
    if (armCulled) {
        culledMs = m;
        haveCulled = true;
    }
    else {
        uncalledMs = m;
        haveUncalled = true;
    }

    if (haveCulled && haveUncalled) {
        decide();
        lastDecision = nowSeconds;
        return;
    }
    // The other arm next. Deliberately not interleaved frame by frame:
    // alternating every frame would make each arm pay the other's
    // transition cost forever, and the transition is what the warm-up
    // exists to exclude.
    startArm(!armCulled);
}

CullBenefitReport CullBenefitEstimator::report() const
{
    CullBenefitReport r;
    r.culledMs = culledMs;
    r.uncalledMs = uncalledMs;
    r.gain = uncalledMs > 0.0f ? (uncalledMs - culledMs) / uncalledMs : 0.0f;
    r.probes = probes;
    r.probing = probing;
    r.culling = culling;
    return r;
}
