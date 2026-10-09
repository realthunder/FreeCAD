// SPDX-License-Identifier: LGPL-2.1-or-later

// How long a turn of work sliced over the event loop may run
// (docs/DocumentLoad.md sec 18.11 and 18.16).
//
// Such work takes a turn, gives the thread back, and takes the next one
// after the event loop has drawn its frame: the landing pump of the level
// meshes, a document's view provider drain, the visual drain. With a
// fixed turn of 50 ms and a frame of 0.4 s that is a ninth of the thread,
// whatever is waiting: the 17058-solid reference assembly landed 3.4 s of
// work in 30 s. The rule lets a turn grow with what the event loop takes
// between turns, and only then.
//
// And the event loop's cost is not what the clock says between two turns.
// The other parties take their turns in that time: the work of every turn
// runs one shared clock, and what it ran is taken off. And a turn lets
// events through from inside itself, where the frame is drawn as often as
// not: what a turn yielded is not its own time, and is the event loop's
// cost for the next.
//
// The rule and the clock alone: no document, no application, no event
// loop. The clock is Gui's own, so this links FreeCADGui.

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <Gui/TurnBudget.h>

namespace
{

const double budget = 0.05;

void wait(double seconds)
{
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// One turn of \a pace, \a seconds long; what it was allowed.
double turnOf(Gui::TurnPace& pace, double base, double seconds = 0.0, bool backlog = true)
{
    Gui::TurnPace::Turn turn(pace, base);
    turn.setBacklog(backlog);
    if (seconds > 0.0) {
        wait(seconds);
    }
    return turn.budget();
}

}  // namespace

TEST(TurnBudget, aTurnWithNothingLeftBehindItIsTheBudget)
{
    // The thread was away because nothing was asked of it, not because a
    // frame was dear: an idle minute says nothing about the next frame.
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.0, false), budget);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.4, false), budget);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 60.0, false), budget);
}

TEST(TurnBudget, whereFramesAreQuickTheBudgetIsAllATurnGets)
{
    // Back in 20 ms, in 100 ms: half of that is under the budget
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.02, true), budget);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.1, true), budget);
}

TEST(TurnBudget, aDearFrameBuysATurnOfHalfItsLength)
{
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.3, true), 0.15);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.4, true), 0.2);
}

TEST(TurnBudget, aTurnIsNeverOverFiveBudgets)
{
    // A frame of seconds does not make a turn of seconds
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.5, true), 0.25);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 2.0, true), 0.25);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 8.0, true), 0.25);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(0.01, 8.0, true), 0.05);
}

TEST(TurnBudget, aTurnIsNeverUnderTheBudgetNorLongerThanTheFrameBesideIt)
{
    for (double away = 0.0; away < 3.0; away += 0.01) {
        const double turn = Gui::turnBudget(budget, away, true);
        EXPECT_GE(turn, budget) << away;
        // No new stall: a turn over the budget is shorter than the stretch
        // the event loop itself has just taken
        if (turn > budget) {
            EXPECT_LT(turn, away) << away;
        }
    }
}

TEST(TurnBudget, aClockThatWentBackwardsIsNotAFrame)
{
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, -1.0, true), budget);
}

TEST(TurnBudget, aLoadTakesTheWholeOfTheStretch)
{
    // A slice of a load may run as long as the event loop took: half the
    // thread, where the pump's refinements have a third
    EXPECT_DOUBLE_EQ(Gui::loadTurnShare, 1.0);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.04, true, Gui::loadTurnShare), budget);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.08, true, Gui::loadTurnShare), 0.08);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.2, true, Gui::loadTurnShare), 0.2);
    // ...under the same ceiling
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.3, true, Gui::loadTurnShare), 0.25);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 8.0, true, Gui::loadTurnShare), 0.25);
    EXPECT_DOUBLE_EQ(Gui::turnBudget(budget, 0.3, false, Gui::loadTurnShare), budget);
}

TEST(TurnPace, theFirstTurnIsTheBudget)
{
    // Nothing is known of the event loop yet, however long ago the
    // process began
    Gui::TurnPace pace;
    EXPECT_DOUBLE_EQ(turnOf(pace, 0.01), 0.01);
}

TEST(TurnPace, aTurnAfterADearStretchIsLonger)
{
    Gui::TurnPace pace;
    turnOf(pace, 0.01);
    wait(0.06);
    // Half of 60 ms and more, and under the ceiling unless the wait ran
    // to 100 ms
    const double turn = turnOf(pace, 0.01);
    EXPECT_GE(turn, 0.03);
    EXPECT_LE(turn, 0.05);
    wait(0.12);
    EXPECT_DOUBLE_EQ(turnOf(pace, 0.01), 5.0 * 0.01);
}

TEST(TurnPace, aTurnOfALoadAfterADearStretchIsAsLongAsTheStretch)
{
    Gui::TurnPace pace;
    {
        Gui::TurnPace::Turn turn(pace, 0.01, Gui::loadTurnShare);
    }
    wait(0.03);
    Gui::TurnPace::Turn turn(pace, 0.01, Gui::loadTurnShare);
    EXPECT_GE(turn.budget(), 0.03);
    EXPECT_LE(turn.budget(), 0.05);
}

TEST(TurnPace, aTurnThatLeftNothingBehindIsFollowedByTheBudget)
{
    Gui::TurnPace pace;
    turnOf(pace, 0.01, 0.0, false);
    wait(0.12);
    EXPECT_DOUBLE_EQ(turnOf(pace, 0.01), 0.01);
}

TEST(TurnPace, aWaitOfItsOwnAccordIsNotWhatAFrameCosts)
{
    Gui::TurnPace pace;
    turnOf(pace, 0.01);
    // The work looks again in a while: for a worker, for a load
    pace.idle();
    wait(0.12);
    EXPECT_DOUBLE_EQ(turnOf(pace, 0.01), 0.01);
}

TEST(TurnPace, anothersTurnIsNotWhatAFrameCosts)
{
    // Two parties, turn about, and an event loop that costs nothing: each
    // is away for the other's turn and for nothing else. Read as the price
    // of a frame, 120 ms would buy a turn of five budgets.
    Gui::TurnPace one;
    Gui::TurnPace other;
    turnOf(one, 0.01);
    turnOf(other, 0.01, 0.12);
    EXPECT_DOUBLE_EQ(turnOf(one, 0.01), 0.01);
    // ...and with a dear stretch besides, it is that stretch that counts
    turnOf(other, 0.01, 0.12);
    wait(0.06);
    const double turn = turnOf(one, 0.01);
    EXPECT_GE(turn, 0.03);
    EXPECT_LT(turn, 0.05);
}

TEST(TurnPace, aTurnInsideAnotherIsWorkedOnce)
{
    Gui::TurnPace outer;
    Gui::TurnPace inner;
    const double worked0 = Gui::TurnPace::worked();
    const double wall0 = now();
    {
        Gui::TurnPace::Turn slice(outer, 0.01);
        turnOf(inner, 0.01, 0.1);
        // Under way, and counted so far
        EXPECT_GE(Gui::TurnPace::worked() - worked0, 0.1);
    }
    const double wall = now() - wall0;
    const double worked = Gui::TurnPace::worked() - worked0;
    EXPECT_GE(worked, 0.1);
    // Twice over it would be 0.2 s and more, in 0.1 s of time
    EXPECT_LE(worked, wall);
}

TEST(TurnPace, whatATurnYieldsIsNotItsOwnTime)
{
    // A slice's progress bar lets events run, and a frame is drawn in
    // there: a slice charged for it is over its budget before it began
    Gui::TurnPace pace;
    const double worked0 = Gui::TurnPace::worked();
    const double wall0 = now();
    Gui::TurnPace::Turn turn(pace, 0.01);
    wait(0.03);
    {
        Gui::TurnPace::Yield yield;
        wait(0.1);
    }
    const double wall = now() - wall0;
    EXPECT_GE(turn.elapsed(), 0.03);
    EXPECT_LE(turn.elapsed(), wall - 0.1);
    // ...and the shared clock stood for as long
    EXPECT_GE(Gui::TurnPace::worked() - worked0, 0.03);
    EXPECT_LE(Gui::TurnPace::worked() - worked0, wall - 0.1);
}

TEST(TurnPace, whatATurnYieldedIsWhatTheEventLoopCost)
{
    // The next slice is posted as a slice ends and is served before the
    // frame is: between the two the event loop costs nothing, and the
    // frame is drawn inside the slice. That is its price all the same.
    Gui::TurnPace pace;
    {
        Gui::TurnPace::Turn turn(pace, 0.01);
        Gui::TurnPace::Yield yield;
        wait(0.12);
    }
    EXPECT_DOUBLE_EQ(turnOf(pace, 0.01), 5.0 * 0.01);
    // Once: a turn that yielded nothing is followed by the budget
    EXPECT_DOUBLE_EQ(turnOf(pace, 0.01), 0.01);
}

TEST(TurnPace, anothersTurnInsideAYieldIsNotWhatTheEventLoopCost)
{
    // One of the events a slice lets through is another party's turn
    Gui::TurnPace one;
    Gui::TurnPace other;
    {
        Gui::TurnPace::Turn turn(one, 0.01);
        Gui::TurnPace::Yield yield;
        turnOf(other, 0.01, 0.12);
    }
    EXPECT_DOUBLE_EQ(turnOf(one, 0.01), 0.01);
    // ...and that party is not charged for the slice it ran inside of
    EXPECT_DOUBLE_EQ(turnOf(other, 0.01), 0.01);
}

TEST(TurnPace, aYieldOutsideAnyTurnIsNothing)
{
    // The progress bar of a blocking operation lets events through too
    Gui::TurnPace pace;
    turnOf(pace, 0.05);
    const double worked0 = Gui::TurnPace::worked();
    {
        Gui::TurnPace::Yield yield;
        wait(0.02);
    }
    EXPECT_DOUBLE_EQ(Gui::TurnPace::worked(), worked0);
    EXPECT_DOUBLE_EQ(turnOf(pace, 0.05), 0.05);
}
