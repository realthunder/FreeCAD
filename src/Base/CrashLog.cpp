/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License (LGPL)   *
 *   as published by the Free Software Foundation; either version 2 of     *
 *   the License, or (at your option) any later version.                   *
 *   for detail see the LICENCE text file.                                 *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with FreeCAD; if not, write to the Free Software        *
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
 *   USA                                                                   *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#endif

#if defined(_MSC_VER)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "CrashLog.h"

using namespace Base;

namespace
{

std::recursive_mutex& theLock()
{
    static std::recursive_mutex mutex;
    return mutex;
}

std::string& theDirectory()
{
    static std::string dir;
    return dir;
}

std::string& theFileName()
{
    static std::string name;
    return name;
}

long processId()
{
#if defined(_MSC_VER)
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

enum class Shape
{
    ToRead,      // "2026-08-12 17:14:52.318"
    ForFileName  // "2026_08_12-17_14_52_318", nothing a file system objects to
};

/// Local date and time down to the millisecond.
std::string timestamp(Shape shape)
{
    const auto now = std::chrono::system_clock::now();
    const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(now - secs);
    const std::time_t tt = std::chrono::system_clock::to_time_t(secs);

    std::tm tmbuf {};
#if defined(_MSC_VER)
    localtime_s(&tmbuf, &tt);
#else
    localtime_r(&tt, &tmbuf);
#endif
    // Spelled out twice rather than passed a format variable, so both strftime()
    // and snprintf() keep seeing string literals and the compiler can go on
    // checking them.
    char buf[32] {};
    char out[48] {};
    const int ms = static_cast<int>(msec.count());
    if (shape == Shape::ForFileName) {
        std::strftime(buf, sizeof(buf), "%Y_%m_%d-%H_%M_%S", &tmbuf);
        std::snprintf(out, sizeof(out), "%s_%03d", buf, ms);
    }
    else {
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmbuf);
        std::snprintf(out, sizeof(out), "%s.%03d", buf, ms);
    }
    return out;
}

/** The one open file, for the life of the process.
 *
 * Opened on the first entry and never closed: a crash must not depend on
 * anything being torn down in the right order, and the process exiting closes
 * it anyway. Every write is flushed, so what has been reported is on disk even
 * if the next instruction is the one that kills us.
 */
std::FILE* theFile()
{
    static std::FILE* file = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::string path = theDirectory();
        path += "crash-" + timestamp(Shape::ForFileName) + ".log";
        file = std::fopen(path.c_str(), "a");
        if (file) {
            theFileName() = path;
            std::cerr << "Writing crash log to " << path << std::endl;
        }
        else {
            std::cerr << "Could not open crash log " << path << ", stderr only" << std::endl;
        }
    }
    return file;
}

/// File first, then stderr. The file is the copy that outlives the terminal,
/// and on a Windows GUI launch there is no terminal at all.
void emit(const char* text)
{
    if (std::FILE* file = theFile()) {
        std::fputs(text, file);
        std::fflush(file);
    }
    std::cerr << text;
}

}  // namespace

void CrashLog::setDirectory(const std::string& dir)
{
    std::lock_guard<std::recursive_mutex> guard(theLock());
    theDirectory() = dir;
}

std::string CrashLog::fileName()
{
    std::lock_guard<std::recursive_mutex> guard(theLock());
    return theFileName();
}

CrashLog::Entry::Entry(Severity severity, const std::string& what)
    // Only ever *tried*. A crash handler that blocks on a mutex some other
    // thread will never release has turned a diagnosable fault into a hang, so
    // if the log is busy this writes anyway and marks the entry.
    : locked(theLock().try_lock())
{
    std::ostringstream str;
    str << "===== " << timestamp(Shape::ToRead) << "  "
        << (severity == Severity::Fatal ? "FATAL " : "CAUGHT") << "  pid " << processId()
        << "  thread " << std::this_thread::get_id();
    if (!locked) {
        str << "  (interleaved)";
    }
    if (!what.empty()) {
        str << "  " << what;
    }
    str << " =====\n";
    emit(str.str().c_str());
}

CrashLog::Entry::~Entry()
{
    emit("\n");
    if (locked) {
        theLock().unlock();
    }
}

void CrashLog::Entry::line(const char* text)
{
    emit(text);
}
