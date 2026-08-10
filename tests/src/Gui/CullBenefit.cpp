// Tests for the runtime cost/benefit decision (docs/FarFieldProxies.md
// 12.13). No GL context and no scene: the whole point of the class is
// that it decides from timings alone, so it can be fed timings.
//
// The bias every test encodes: when it has not established that culling
// pays, it must not cull. Being wrong that way costs frame time; being
// wrong the other way costs pixels, and this whole section exists
// because of the other way.

#include <gtest/gtest.h>

#include <vector>

#include "Gui/Renderer/CullBenefit.h"

using namespace Render;

namespace
{

/// Drive the estimator through \a frames frames, answering each with the
/// cost that arm is supposed to have. Returns the number of frames that
/// actually ran with culling on.
int drive(CullBenefitEstimator &est, int frames, float culledMs,
          float uncalledMs, double startTime = 1.0, double dt = 0.01)
{
    int culled = 0;
    double t = startTime;
    for (int i = 0; i < frames; ++i) {
        const bool on = est.cullThisFrame();
        culled += on ? 1 : 0;
        est.frame(on ? culledMs : uncalledMs, t);
        t += dt;
    }
    return culled;
}

/// Frames enough to complete one whole probe (both arms).
int probeFrames(const CullBenefitConfig &c)
{
    return int(2 * (c.sampleFrames + c.warmupFrames) + 4);
}

}  // namespace

TEST(CullBenefit, StartsNotCulling)
{
    // An estimator that has measured nothing has established nothing.
    CullBenefitEstimator est;
    EXPECT_FALSE(est.cullThisFrame());
    EXPECT_FALSE(est.worthwhile());
    EXPECT_TRUE(est.report().probing);
}

TEST(CullBenefit, TurnsCullingOnWhenItIsClearlyCheaper)
{
    CullBenefitEstimator est;
    CullBenefitConfig c;
    est.configure(c);
    // 40 ms against 60 ms: a third of the frame given back.
    drive(est, probeFrames(c), 40.0f, 60.0f);

    const auto r = est.report();
    EXPECT_TRUE(r.culling) << "gain was " << r.gain;
    EXPECT_TRUE(est.cullThisFrame());
    EXPECT_NEAR(40.0f, r.culledMs, 0.01f);
    EXPECT_NEAR(60.0f, r.uncalledMs, 0.01f);
    EXPECT_NEAR(1.0f / 3.0f, r.gain, 0.01f);
    EXPECT_EQ(1u, r.probes);
}

TEST(CullBenefit, LeavesCullingOffWhenDecidingCostsMoreThanDrawing)
{
    // The measured case of section 12.12: the culling is correct and
    // costs 26 ms to save 5 ms of submission.
    CullBenefitEstimator est;
    CullBenefitConfig c;
    est.configure(c);
    drive(est, probeFrames(c), 96.0f, 75.0f);

    const auto r = est.report();
    EXPECT_FALSE(r.culling) << "gain was " << r.gain;
    EXPECT_FALSE(est.cullThisFrame());
    EXPECT_LT(r.gain, 0.0f);
}

TEST(CullBenefit, ARoundingDifferenceIsNotAReasonToCull)
{
    // Half a percent is inside anybody's noise, and switching the draw
    // set for it costs more than it can win.
    CullBenefitEstimator est;
    CullBenefitConfig c;
    est.configure(c);
    drive(est, probeFrames(c), 74.6f, 75.0f);
    EXPECT_FALSE(est.report().culling);
}

TEST(CullBenefit, TheTwoThresholdsStopItFlapping)
{
    // KEY: A verdict sitting between the two margins must hold, whichever
    // way it is already set. One threshold at the noise floor flips every
    // probe, and a mechanism that rebuilds the draw set every twenty
    // seconds is worse than one that never culls at all -- section 12.7
    // measured what oscillation costs.
    CullBenefitConfig c;
    CullBenefitEstimator on, off;
    on.configure(c);
    off.configure(c);

    // Get one estimator to "on" with a clear win, the other left at off.
    drive(on, probeFrames(c), 40.0f, 60.0f);
    ASSERT_TRUE(on.report().culling);
    drive(off, probeFrames(c), 75.0f, 75.0f);
    ASSERT_FALSE(off.report().culling);

    // Now feed both the same marginal 2.5% gain, which is above
    // offMargin (1%) and below onMargin (5%).
    const float uncalled = 100.0f, culled = 97.5f;
    double t = 100.0;
    for (int round = 0; round < 3; ++round) {
        // Force a re-probe by advancing past the reprobe interval.
        t += double(c.reprobeSeconds) + 1.0;
        drive(on, probeFrames(c), culled, uncalled, t);
        drive(off, probeFrames(c), culled, uncalled, t);
    }
    EXPECT_TRUE(on.report().culling) << "a holding verdict was thrown away";
    EXPECT_FALSE(off.report().culling) << "a marginal gain turned culling on";
}

TEST(CullBenefit, BothArmsAreActuallyRun)
{
    // The experiment is only an experiment if both arms happen. A probe
    // that never turns the culling on measures one number twice and
    // compares it with zero.
    CullBenefitEstimator est;
    CullBenefitConfig c;
    est.configure(c);
    const int frames = probeFrames(c);
    const int culled = drive(est, frames, 40.0f, 60.0f);
    EXPECT_GT(culled, int(c.warmupFrames))
            << "the culled arm never ran for a full sample";
    EXPECT_LT(culled, frames)
            << "the un-culled arm never ran";
}

TEST(CullBenefit, TheVerdictIsRevisitedAsTheSceneChanges)
{
    // Every term of the trade is a property of the model and the camera,
    // so a verdict has a shelf life. Culling that paid handsomely must be
    // dropped once it stops paying.
    CullBenefitEstimator est;
    CullBenefitConfig c;
    est.configure(c);
    drive(est, probeFrames(c), 40.0f, 60.0f);
    ASSERT_TRUE(est.report().culling);

    // The camera moves outside the model: nothing occludes any more, and
    // the pass is pure cost.
    double t = 500.0;
    for (int round = 0; round < 2; ++round) {
        t += double(c.reprobeSeconds) + 1.0;
        drive(est, probeFrames(c), 90.0f, 60.0f, t);
    }
    EXPECT_FALSE(est.report().culling)
            << "culling that stopped paying was not turned off";
    EXPECT_GE(est.report().probes, 2u);
}

TEST(CullBenefit, AVerdictHoldsBetweenProbes)
{
    // Between probes the answer must be stable: re-deciding every frame
    // is the oscillation this class is built to avoid.
    CullBenefitEstimator est;
    CullBenefitConfig c;
    est.configure(c);
    const double probeEnd = 1.0 + probeFrames(c) * 0.01;
    drive(est, probeFrames(c), 40.0f, 60.0f);
    ASSERT_TRUE(est.cullThisFrame());

    // Well within the reprobe interval, every frame culls. The clock has
    // to continue from where the probe left off: jumping ahead past
    // reprobeSeconds would (correctly) start a new probe, and then this
    // would be testing the re-probe rather than the hold.
    double t = probeEnd;
    for (int i = 0; i < 200; ++i) {
        EXPECT_TRUE(est.cullThisFrame()) << "frame " << i;
        est.frame(40.0f, t);
        t += 0.001;  // 0.2 s in total, far short of reprobeSeconds
    }
    EXPECT_EQ(1u, est.report().probes);
}

TEST(CullBenefit, OutliersDoNotDecideTheVerdict)
{
    // A frame that hit a recompute or a texture upload is tens of
    // milliseconds, and a mean over two dozen frames carries it into the
    // verdict. The median does not.
    CullBenefitEstimator est;
    CullBenefitConfig c;
    est.configure(c);

    double t = 1.0;
    int i = 0;
    const int frames = probeFrames(c);
    while (i < frames) {
        const bool on = est.cullThisFrame();
        float ms = on ? 40.0f : 60.0f;
        if (i % 7 == 0)
            ms += 500.0f;  // a hitch, on both arms alike
        est.frame(ms, t);
        t += 0.01;
        ++i;
    }
    const auto r = est.report();
    EXPECT_TRUE(r.culling);
    EXPECT_NEAR(40.0f, r.culledMs, 0.01f) << "a hitch reached the median";
    EXPECT_NEAR(60.0f, r.uncalledMs, 0.01f);
}

TEST(CullBenefit, ResetForgetsTheVerdictAndStopsCulling)
{
    CullBenefitEstimator est;
    CullBenefitConfig c;
    est.configure(c);
    drive(est, probeFrames(c), 40.0f, 60.0f);
    ASSERT_TRUE(est.cullThisFrame());

    est.reset();
    EXPECT_FALSE(est.cullThisFrame());
    EXPECT_FALSE(est.worthwhile());
    EXPECT_TRUE(est.report().probing);
    EXPECT_EQ(0u, est.report().probes);
}

TEST(CullBenefit, ReconfiguringRestartsTheExperiment)
{
    // A different experiment is a different experiment; carrying the old
    // verdict across would attribute one configuration's numbers to
    // another.
    CullBenefitEstimator est;
    CullBenefitConfig c;
    est.configure(c);
    drive(est, probeFrames(c), 40.0f, 60.0f);
    ASSERT_TRUE(est.worthwhile());

    c.sampleFrames = 8;
    est.configure(c);
    EXPECT_FALSE(est.worthwhile());
    EXPECT_TRUE(est.report().probing);
}
