/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef BASE_TIMEINFO_H
#define BASE_TIMEINFO_H

// Std. configurations


#include <cstdio>
#if defined(FC_OS_BSD)
#include <sys/time.h>
#else
#include <sys/timeb.h>
#endif
#include <ctime>

#ifdef __GNUC__
#include <cstdint>
#endif

#include <chrono>
#include <sstream>
#include <string>
#include <FCGlobal.h>

#if defined(FC_OS_BSD)
struct timeb
{
    int64_t time;
    unsigned short millitm;
};
#endif

namespace Base
{
/// BaseClass class and root of the type system
class BaseExport TimeInfo
{

public:
    /// Construction
    TimeInfo();
    TimeInfo(const TimeInfo&) = default;
    TimeInfo(TimeInfo&&) = default;
    /// Destruction
    ~TimeInfo();

    /// sets the object to the actual system time
    void setCurrent();
    void setTime_t(int64_t seconds);

    int64_t getSeconds() const;
    unsigned short getMiliseconds() const;

    TimeInfo& operator=(const TimeInfo& time) = default;
    TimeInfo& operator=(TimeInfo&& time) = default;
    bool operator==(const TimeInfo& time) const;
    bool operator!=(const TimeInfo& time) const;

    bool operator<(const TimeInfo& time) const;
    bool operator<=(const TimeInfo& time) const;
    bool operator>=(const TimeInfo& time) const;
    bool operator>(const TimeInfo& time) const;

    static std::string currentDateTimeString();
    static std::string diffTime(const TimeInfo& timeStart, const TimeInfo& timeEnd = TimeInfo());
    static float diffTimeF(const TimeInfo& timeStart, const TimeInfo& timeEnd = TimeInfo());
    bool isNull() const;
    static TimeInfo null();

private:
    // clang-format off
#if defined(_MSC_VER)
    struct _timeb timebuffer;
#elif defined(__GNUC__)
    struct timeb timebuffer {};
#endif
    // clang-format on
};


inline int64_t TimeInfo::getSeconds() const
{
    return timebuffer.time;
}

inline unsigned short TimeInfo::getMiliseconds() const
{
    return timebuffer.millitm;
}

inline bool TimeInfo::operator!=(const TimeInfo& time) const
{
    return (timebuffer.time != time.timebuffer.time
            || timebuffer.millitm != time.timebuffer.millitm);
}

inline bool TimeInfo::operator==(const TimeInfo& time) const
{
    return (timebuffer.time == time.timebuffer.time
            && timebuffer.millitm == time.timebuffer.millitm);
}

inline bool TimeInfo::operator<(const TimeInfo& time) const
{
    if (timebuffer.time == time.timebuffer.time) {
        return timebuffer.millitm < time.timebuffer.millitm;
    }
    return timebuffer.time < time.timebuffer.time;
}

inline bool TimeInfo::operator<=(const TimeInfo& time) const
{
    if (timebuffer.time == time.timebuffer.time) {
        return timebuffer.millitm <= time.timebuffer.millitm;
    }
    return timebuffer.time <= time.timebuffer.time;
}

inline bool TimeInfo::operator>=(const TimeInfo& time) const
{
    if (timebuffer.time == time.timebuffer.time) {
        return timebuffer.millitm >= time.timebuffer.millitm;
    }
    return timebuffer.time >= time.timebuffer.time;
}

inline bool TimeInfo::operator>(const TimeInfo& time) const
{
    if (timebuffer.time == time.timebuffer.time) {
        return timebuffer.millitm > time.timebuffer.millitm;
    }
    return timebuffer.time > time.timebuffer.time;
}

using Ticks = std::chrono::steady_clock;

/** How long something took, upstream's spelling of it
 *
 * A monotonic stopwatch, which is what a duration wants and what TimeInfo
 * above is not: that one is wall clock, so a clock adjustment lands in the
 * middle of whatever it is timing. Ported from upstream unchanged so that
 * code written against either tree measures the same way.
 *
 * TimeInfo itself is deliberately left alone. Upstream rewrote it onto
 * std::chrono and dropped getSeconds(), getMiliseconds() and
 * currentDateTimeString() in the process, which this fork uses; that is a
 * behaviour change and belongs on its own consideration, not smuggled in
 * behind an additive port.
 */
class TimeElapsed: public std::chrono::time_point<Ticks>
{
public:
    TimeElapsed()
    {
        setCurrent();
    }

    TimeElapsed(const TimeElapsed&) = default;
    TimeElapsed(TimeElapsed&&) = default;
    ~TimeElapsed() = default;

    TimeElapsed& operator=(const TimeElapsed&) = default;
    TimeElapsed& operator=(TimeElapsed&&) = default;

    void setCurrent()
    {
        static_cast<std::chrono::time_point<Ticks>&>(*this) = Ticks::now();
    }

    static float diffTimeF(const TimeElapsed& start, const TimeElapsed& end = TimeElapsed())
    {
        const std::chrono::duration<float> duration = end - start;
        return duration.count();
    }

    static std::string diffTime(const TimeElapsed& start, const TimeElapsed& end = TimeElapsed())
    {
        std::stringstream ss;
        const std::chrono::duration<float> secs = end - start;
        ss << secs.count();
        return ss.str();
    }
};  // class TimeElapsed

}  // namespace Base


#endif  // BASE_TIMEINFO_H
