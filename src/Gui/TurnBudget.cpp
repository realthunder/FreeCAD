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

#include "PreCompiled.h"

#include "TurnBudget.h"

using namespace Gui;

namespace
{

using Clock = std::chrono::steady_clock;

/// The clock every turn's work runs. It goes while the innermost thing
/// under way is a turn, and stands while that is a yield: a turn inside
/// another's yield is work again, and its own yield is not.
struct Account
{
    double total = 0.0;
    bool working = false;
    Clock::time_point since;
    TurnPace::Turn* current = nullptr;
};

Account& account()
{
    static Account acc;
    return acc;
}

double seconds(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double>(to - from).count();
}

double workedAt(Clock::time_point now)
{
    const Account& acc = account();
    return acc.total + (acc.working ? seconds(acc.since, now) : 0.0);
}

void setWorking(bool on, Clock::time_point now)
{
    Account& acc = account();
    if (acc.working == on) {
        return;
    }
    if (acc.working) {
        acc.total += seconds(acc.since, now);
    }
    acc.working = on;
    acc.since = now;
}

}  // namespace

double TurnPace::worked()
{
    return workedAt(Clock::now());
}

TurnPace::Turn::Turn(TurnPace& pace, double budget, double share)
    : _pace(pace)
    , _outer(account().current)
    , _start(Clock::now())
    , _workedAtStart(workedAt(_start))
    , _budget(budget)
    , _wasWorking(account().working)
{
    // Away for the time that passed, less the work of the turns taken in
    // it; and what the event loop took from inside the last turn was its
    // cost as much as what it took after
    const double away = seconds(pace._lastEnd, _start) - (_workedAtStart - pace._workedAtEnd)
        + pace._yieldedInLast;
    _budget = turnBudget(budget, away, pace._backlog, share);
    account().current = this;
    setWorking(true, _start);
}

TurnPace::Turn::~Turn()
{
    const auto now = Clock::now();
    const double worked = workedAt(now);
    // Of this turn's time, what was nobody's work: the event loop's
    // share of what it yielded. A turn another party took in there is
    // on the clock.
    _pace._yieldedInLast = std::max(0.0, seconds(_start, now) - (worked - _workedAtStart));
    _pace._lastEnd = now;
    _pace._workedAtEnd = worked;
    _pace._backlog = _backlog;
    account().current = _outer;
    setWorking(_wasWorking, now);
}

double TurnPace::Turn::elapsed() const
{
    return seconds(_start, Clock::now()) - _yielded;
}

TurnPace::Yielded::Yielded()
    : _turn(account().current)
    , _start(Clock::now())
    , _wasWorking(account().working)
{
    setWorking(false, _start);
}

TurnPace::Yielded::~Yielded()
{
    const auto now = Clock::now();
    if (_turn) {
        _turn->_yielded += seconds(_start, now);
    }
    setWorking(_wasWorking, now);
}
