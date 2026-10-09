/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#ifndef GUI_TURNBUDGET_H
#define GUI_TURNBUDGET_H

#include <algorithm>
#include <chrono>

#include <FCGlobal.h>

namespace Gui
{

/// How long a turn of work sliced over the event loop may run, in seconds.
///
/// \a budget is what the work is given a turn where the event loop is
/// quick. With work left over from the turn before (\a backlog), a turn
/// may run \a share of the time the event loop then took to give the
/// thread back (\a away), up to five budgets and never less than one.
/// Every turn ends in a frame, and on a model whose frame is dear a fixed
/// turn is a fixed, small share of the thread: the 17058-solid reference
/// assembly gave the landing pump of the level meshes two turns a second,
/// and its 5100 landings -- 3.4 s of work -- took 30 s to land
/// (docs/DocumentLoad.md sec 18.11). At a share of one half, the pump's,
/// a longer turn is no new stall beside a frame that is longer; where
/// frames are quick the event loop is back in milliseconds, and the
/// budget is all a turn gets.
inline double turnBudget(double budget, double away, bool backlog, double share = 0.5)
{
    if (!backlog || !(away > 0.0)) {
        return budget;
    }
    return std::max(budget, std::min(share * away, 5.0 * budget));
}

/// The share of the event loop's stretch that a slice of a load takes
/// (turnBudget): all of it, so that the load has half the thread. A load
/// puts an object on the screen for the first time, which is ahead of
/// the pump's refining of one that is drawn. Measured on the reference
/// assembly in a window on the desktop (docs/DocumentLoad.md sec 18.16):
/// the view provider drain is through after 13.5 to 17.1 s with a fixed
/// slice, 12.9 to 14.0 s at half the stretch, 11.1 to 11.4 s at the
/// whole, and 9.1 and 12.2 s at twice it; the thread is away from the
/// event loop for 0.7 to 1.0 s at most in every one of them, which is a
/// frame's doing.
constexpr double loadTurnShare = 1.0;

/// What one piece of sliced work knows of the event loop between its
/// turns: when its last turn ended, and whether it left work behind.
///
/// One for each party that takes turns -- a document's view provider
/// drain, the visual drain, the landing pump. Two things keep the plain
/// reading of the clock from being what the event loop costs.
///
/// *The other parties.* What a party is away for is not all the event
/// loop's: the others take their turns in that time too, and a party
/// that read their turns as the price of a frame would lengthen its own
/// for them, and they theirs for it. Two parties settle (each takes half
/// of the other); four documents loading at once would each take half of
/// what the three others took, and go to the ceiling with frames that
/// cost nothing. So the work of every turn runs one clock, shared by
/// all, and a party is away for the time that passed less what that
/// clock ran meanwhile.
///
/// *The events a turn lets through.* A slice of a load reports to the
/// progress bar, and the bar runs the event loop from inside the slice
/// every fifth of a second (Yielded, in the bar's own pump). The window is
/// repainted THERE, more often than between two slices: the next slice is
/// already posted when a slice ends, and is served before the timer that
/// would draw the frame. So the time a turn spent yielded is the event
/// loop's turn and not the work's -- it is not counted against the
/// turn's budget (elapsed()), and it is counted into what the event loop
/// cost when the next turn is measured. A slice that charged itself for
/// the frame drawn inside it was over its budget before it began, and
/// gave up after one object: every other slice of a load on the
/// reference assembly, in a window on the desktop
/// (docs/DocumentLoad.md sec 18.16).
///
/// GUI thread only.
class GuiExport TurnPace
{
public:
    /// One turn, from here to the end of the scope.
    class GuiExport Turn
    {
    public:
        /// \a budget and \a share as for turnBudget().
        Turn(TurnPace& pace, double budget, double share = 0.5);
        ~Turn();
        Turn(const Turn&) = delete;
        Turn& operator=(const Turn&) = delete;

        /// How long this turn may run, in seconds.
        double budget() const
        {
            return _budget;
        }
        /// How long it has run, in seconds: the time since it began,
        /// less what it spent yielded.
        double elapsed() const;
        /// Whether this turn leaves work that the next takes up at once.
        /// Taken for granted unless said otherwise.
        void setBacklog(bool on)
        {
            _backlog = on;
        }

    private:
        friend class TurnPace;
        TurnPace& _pace;
        Turn* _outer;
        std::chrono::steady_clock::time_point _start;
        double _workedAtStart;
        double _yielded = 0.0;
        double _budget;
        bool _backlog = true;
        bool _wasWorking;
    };

    /// The event loop run from inside a turn, from here to the end of
    /// the scope: whoever lets events through says so with one of these.
    /// Outside any turn it is nothing.
    ///
    /// Not "Yield": <winbase.h> defines Yield() as a macro that expands
    /// to nothing, and a class of that name loses its constructor
    /// wherever the Windows headers come first.
    class GuiExport Yielded
    {
    public:
        Yielded();
        ~Yielded();
        Yielded(const Yielded&) = delete;
        Yielded& operator=(const Yielded&) = delete;

    private:
        Turn* _turn;
        std::chrono::steady_clock::time_point _start;
        bool _wasWorking;
    };

    /// The next turn does not follow from the last: the work waited of
    /// its own accord -- for a worker, for a load -- and what passes
    /// until then says nothing of what a frame costs.
    void idle()
    {
        _backlog = false;
    }

    /// Seconds of work so far in turns, all parties' together: the
    /// shared clock, which stands while a turn is yielded.
    static double worked();

private:
    std::chrono::steady_clock::time_point _lastEnd;
    double _workedAtEnd = 0.0;
    /// What the event loop took from inside the last turn.
    double _yieldedInLast = 0.0;
    bool _backlog = false;
};

}  // namespace Gui

#endif  // GUI_TURNBUDGET_H
