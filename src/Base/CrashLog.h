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

#ifndef BASE_CRASHLOG_H
#define BASE_CRASHLOG_H

#include <string>
#include <FCGlobal.h>

namespace Base
{

/** The log a crash writes to, and the only one that outlives the process.
 *
 * One file per run -- <UserAppData>/crash-2026_08_12-17_14_52_318.log, named
 * for when the run's first entry was written -- opened on first use and kept
 * open until the process ends. Every entry is flushed as it is written, so a
 * hard kill loses nothing that had already been reported.
 *
 * Nothing here goes through Base::Console(). What is being reported may well
 * be a fault *inside* the console -- the access violation this was built for
 * faulted in ConsoleSingleton::Error() -- so it uses plain stdio, writes the
 * file before stderr, and never allocates more than it must.
 */
class BaseExport CrashLog
{
public:
    enum class Severity
    {
        /// The process is going down, or has just survived something it should
        /// not have: a signal, an unhandled exception, an access violation.
        Fatal,
        /// An exception reached a boundary that had to swallow it -- notably
        /// GUIApplication::notify(). The application carries on, but nothing
        /// should ever have thrown that far, so it is worth a record.
        Caught
    };

    /** Where the log is written. Call once, as soon as the user directory is
     * known. Until then entries land in the working directory, which still
     * beats losing them.
     */
    static void setDirectory(const std::string& dir);

    /// Path of the file in use, empty until the first entry is written.
    static std::string fileName();

    /** One entry, header included, written on construction.
     *
     * Holds the log's lock for as long as it lives, so the lines of one entry
     * cannot interleave with another thread's. The lock is only ever *tried*:
     * a crash handler must not be the thing that deadlocks, so if some other
     * thread is mid-entry this writes anyway and says so.
     */
    class BaseExport Entry
    {
    public:
        Entry(Severity severity, const std::string& what);
        ~Entry();

        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;

        /// Append a line to this entry. Written verbatim; add your own newline.
        void line(const char* text);
        void line(const std::string& text)
        {
            line(text.c_str());
        }

    private:
        bool locked;
    };
};

}  // namespace Base

#endif  // BASE_CRASHLOG_H
