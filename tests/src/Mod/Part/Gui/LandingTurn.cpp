// SPDX-License-Identifier: LGPL-2.1-or-later

// How long a turn of the level-of-detail landing pump may run
// (docs/DocumentLoad.md sec 18.11).
//
// The pump takes a turn, gives the thread back, and takes the next one
// after the event loop has drawn its frame. With a fixed turn of 50 ms and
// a frame of 0.4 s that is a ninth of the thread, whatever is waiting: the
// 17058-solid reference assembly landed 3.4 s of work in 30 s. The rule
// here lets a turn grow with what the event loop takes between turns, and
// only then.
//
// The rule alone: no document, no application, no pump.

#include <gtest/gtest.h>

#include <Mod/Part/Gui/MeshLevelSource.h>

namespace
{

const double budget = 0.05;

}  // namespace

TEST(LandingTurn, aTurnWithNothingLeftBehindItIsTheBudget)
{
    // The thread was away because nothing was asked of it, not because a
    // frame was dear: an idle minute says nothing about the next frame.
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 0.0, false), budget);
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 0.4, false), budget);
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 60.0, false), budget);
}

TEST(LandingTurn, whereFramesAreQuickTheBudgetIsAllATurnGets)
{
    // Back in 20 ms, in 100 ms: half of that is under the budget
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 0.02, true), budget);
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 0.1, true), budget);
}

TEST(LandingTurn, aDearFrameBuysATurnOfHalfItsLength)
{
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 0.3, true), 0.15);
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 0.4, true), 0.2);
}

TEST(LandingTurn, aTurnIsNeverOverFiveBudgets)
{
    // A frame of seconds does not make a turn of seconds
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 0.5, true), 0.25);
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 2.0, true), 0.25);
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, 8.0, true), 0.25);
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(0.01, 8.0, true), 0.05);
}

TEST(LandingTurn, aTurnIsNeverUnderTheBudgetNorLongerThanTheFrameBesideIt)
{
    for (double away = 0.0; away < 3.0; away += 0.01) {
        const double turn = PartGui::landingTurnBudget(budget, away, true);
        EXPECT_GE(turn, budget) << away;
        // No new stall: a turn over the budget is shorter than the stretch
        // the event loop itself has just taken
        if (turn > budget) {
            EXPECT_LT(turn, away) << away;
        }
    }
}

TEST(LandingTurn, aClockThatWentBackwardsIsNotAFrame)
{
    EXPECT_DOUBLE_EQ(PartGui::landingTurnBudget(budget, -1.0, true), budget);
}
