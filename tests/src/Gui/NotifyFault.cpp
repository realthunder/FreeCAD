/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>              *
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

/// A native fault inside an event handler ends the process, on the record
/// (Windows only; GUIApplication::guardedDispatch).
///
/// /EHsc's catch (...) in GUIApplication::notify() never saw an access
/// violation, and on a box whose DLP agent hooks the window procedure the
/// agent claimed it and the event loop ran on: an OCCT STEP writer crash
/// looked like a hang (OCCT tests/occ-issues local05). The guard now takes
/// the fault first. In a death-test child this builds a real GUIApplication,
/// faults in a QTimer slot -- dispatched through the window procedure like
/// any timer -- and gives the loop five seconds to come back if something
/// swallows it. The parent then wants:
///
///   - the child gone with the fault's own code, not alive at the fallback;
///   - the guard's Fatal entry in the crash log, naming the fault and the
///     event. Without the guard the fault ends the child some other way --
///     on the box this was written on, offscreen, gtest's own SEH handler
///     took it (exit status 1) -- and nothing writes that entry.

#include <gtest/gtest.h>

#include <windows.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <QDir>
#include <QTimer>

#include <App/Application.h>
#include <Base/CrashLog.h>
#include <Gui/GuiApplication.h>

namespace
{

std::string logDirectory()
{
    return QDir(QDir::tempPath()).filePath(QStringLiteral("NotifyFault_tests_run")).toStdString();
}

[[noreturn]] void faultInATimerSlot()
{
    // No dialog from Windows Error Reporting: it would hold the child open
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    App::Application::Config()["ExeName"] = "NotifyFault_tests_run";
    int argc = 1;
    static std::array<char, 32> exename {"NotifyFault_tests_run"};
    std::array<char*, 2> argv {exename.data(), nullptr};
    App::Application::init(argc, argv.data());
    // After init(), which points the log at the user directory
    Base::CrashLog::setDirectory(logDirectory() + "/");

    qputenv("QT_QPA_PLATFORM", "offscreen");
    static int qargc = 1;
    static std::array<char*, 2> qargv {exename.data(), nullptr};
    Gui::GUIApplication app(qargc, qargv.data());

    QTimer fault;
    fault.setSingleShot(true);
    QObject::connect(&fault, &QTimer::timeout, [] {
        int* volatile nowhere = nullptr;
        *nowhere = 1;
    });
    fault.start(0);
    // Still here: the fault was claimed somewhere and the loop ran on
    QTimer::singleShot(5000, [] { std::_Exit(3); });
    app.exec();
    std::_Exit(4);
}

std::string readLog()
{
    QDir dir(QString::fromStdString(logDirectory()));
    std::string text;
    for (const auto& name : dir.entryList({QStringLiteral("crash-*.log")}, QDir::Files)) {
        std::ifstream in(dir.filePath(name).toStdString());
        std::stringstream all;
        all << in.rdbuf();
        text += all.str();
    }
    return text;
}

}  // namespace

TEST(NotifyFault, nativeFaultInAnEventHandlerEndsTheProcessOnTheRecord)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    QDir(QString::fromStdString(logDirectory())).removeRecursively();
    QDir().mkpath(QString::fromStdString(logDirectory()));

    EXPECT_EXIT(faultInATimerSlot(),
                ::testing::ExitedWithCode(static_cast<int>(EXCEPTION_ACCESS_VIOLATION)),
                "");

    const std::string text = readLog();
    EXPECT_NE(text.find("FATAL"), std::string::npos) << text;
    EXPECT_NE(text.find("native exception 0xc0000005"), std::string::npos) << text;
    EXPECT_NE(text.find("writing 0x0"), std::string::npos) << text;
    EXPECT_NE(text.find("receiver QTimer"), std::string::npos) << text;
}
