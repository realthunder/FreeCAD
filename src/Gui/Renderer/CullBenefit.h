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

#ifndef RENDERER_CULL_BENEFIT_H
#define RENDERER_CULL_BENEFIT_H

/// Is the culling paying for itself? Decided by measurement, per scene,
/// and re-decided as the scene changes (docs/FarFieldProxies.md 12.13).
///
/// **Why this is not a constant.** Occlusion culling trades CPU spent
/// deciding for CPU and GPU saved not drawing, and every term in that
/// trade is a property of the model and the camera rather than of the
/// code. What a draw costs to submit depends on its mesh; how much a
/// frame occludes depends on whether the camera is inside a chassis or
/// looking at a silhouette; what the occluder pass costs depends on how
/// many triangles the occluders carry. Section 12.12 measured the same
/// mechanism saving 4.9 ms of submission on one camera while costing
/// 26 ms to decide -- and there is no reason to believe either number
/// transfers to the next model. Guthe et al. (2006) reached the same
/// conclusion for hardware queries and answered it the same way: measure
/// the machine's parameters at runtime, and spend the culling effort
/// only where the measurement says it pays.
///
/// **What this does NOT do.** It does not model anything. There is no
/// microseconds-per-draw constant, no occlusion-probability estimate and
/// no calibration table, because this workstream has twice been wrong by
/// a factor of two by reasoning from a per-draw cost instead of timing
/// the frame (section 12.12's correction). It runs an A/B experiment on
/// the real frames: some frames with the culling on, some with it off,
/// medians compared. The answer is whatever the clock says.
///
/// **The bias.** When it does not know, it does not cull. An untested
/// verdict draws too much, which costs frame time; the opposite costs
/// pixels, and this section exists because of the opposite.

#include <cstdint>
#include <vector>

#include "Renderer.h"

namespace Render {

/// What the experiment is allowed to cost and how sure it has to be.
struct CullBenefitConfig {
    /// Frames measured in each arm of a probe, after the warm-up.
    uint32_t sampleFrames = 24;
    /// Frames discarded at the start of each arm. Switching the culling
    /// changes the draw set, and the first frames after that carry the
    /// cost of the change (a rebuilt occluder buffer, a GPU pipeline
    /// that has not settled) rather than the steady state being asked
    /// about.
    uint32_t warmupFrames = 6;
    /// How long a verdict stands before the experiment is run again.
    /// The scene, the camera and the window all change under it.
    float reprobeSeconds = 20.0f;
    /// How much cheaper the culled arm must be before culling is turned
    /// on, as a fraction of the un-culled frame time.
    ///
    /// KEY: Two thresholds, not one, and the gap between them is the
    /// point. A single threshold at a measurement's noise floor flips
    /// the verdict every probe, and a mechanism that turns culling on
    /// and off every twenty seconds is worse than one that never turns
    /// it on -- the draw set changes, and with it every cache the
    /// renderer keeps. This section has already measured what
    /// oscillation costs (section 12.7).
    float onMargin = 0.05f;
    /// How much cheaper it must remain to stay on. Lower than
    /// `onMargin`, so a verdict that is holding steady is not thrown
    /// away over noise.
    float offMargin = 0.01f;

    bool operator==(const CullBenefitConfig &o) const
    {
        return sampleFrames == o.sampleFrames
                && warmupFrames == o.warmupFrames
                && reprobeSeconds == o.reprobeSeconds
                && onMargin == o.onMargin && offMargin == o.offMargin;
    }
    bool operator!=(const CullBenefitConfig &o) const { return !(*this == o); }
};

/// What the experiment has established, for the readout.
struct CullBenefitReport {
    /// Median frame cost of each arm, in milliseconds, 0 if unmeasured.
    float culledMs = 0.0f;
    float uncalledMs = 0.0f;
    /// (uncalled - culled) / uncalled, i.e. the fraction of the frame
    /// the culling saves. Negative when it costs more than it saves.
    float gain = 0.0f;
    /// Probes completed since the last reset.
    uint32_t probes = 0;
    bool probing = true;
    bool culling = false;
};

/// Runs the experiment and holds the verdict.
///
/// The caller drives it: ask `cullThisFrame()` before deciding whether
/// to apply the cull mask, and hand back what that frame cost with
/// `frame()`. Nothing here knows how the culling is done, so the same
/// object judges the hardware oracle and the software one.
class RendererExport CullBenefitEstimator
{
public:
    void configure(const CullBenefitConfig &c);
    const CullBenefitConfig &config() const { return conf; }

    /// Whether this frame should apply the cull mask. During a probe
    /// this alternates between the arms; afterwards it is the verdict.
    bool cullThisFrame() const { return cullnow; }

    /// Report the cost of the frame just finished, and the time now.
    ///
    /// \a cpuMs should be the whole CPU cost of the frame including
    /// whatever the culling spent deciding -- that is the quantity the
    /// user experiences, and splitting it into "the culling's share" and
    /// "the drawing's share" is exactly the modelling this class exists
    /// to avoid.
    void frame(float cpuMs, double nowSeconds);

    /// Forget everything and probe again from scratch. For a new
    /// document, a new window size, or any change that makes previous
    /// frames a different experiment.
    void reset();

    CullBenefitReport report() const;
    bool worthwhile() const { return culling; }

private:
    static float median(std::vector<float> &v);
    void startArm(bool culled);
    void decide();

    CullBenefitConfig conf;
    /// Samples collected in the current arm.
    std::vector<float> samples;
    /// The last completed medians, 0 when never measured.
    float culledMs = 0.0f;
    float uncalledMs = 0.0f;
    uint32_t seen = 0;        ///< frames in this arm, warm-up included
    uint32_t probes = 0;
    bool cullnow = false;     ///< what the current frame should do
    bool armCulled = false;   ///< which arm is being sampled
    bool probing = true;
    bool culling = false;     ///< the standing verdict
    bool haveCulled = false;
    bool haveUncalled = false;
    double lastDecision = 0.0;
};

}  // namespace Render

#endif  // RENDERER_CULL_BENEFIT_H
