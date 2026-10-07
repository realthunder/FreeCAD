/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

/*[[[cog
import ReportViewParams
ReportViewParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "ReportViewParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class ReportViewParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(ReportViewParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("checkShowReportViewOnWarning");
        signalParamChanged("checkShowReportViewOnError");
        signalParamChanged("checkShowReportViewOnNormalMessage");
        signalParamChanged("checkShowReportViewOnLogMessage");
        signalParamChanged("checkShowReportViewOnCritical");
        signalParamChanged("checkShowReportTimecode");
        signalParamChanged("LogMessageSize");
        signalParamChanged("DuplicateWindow");
        signalParamChanged("DuplicateKeyLength");
        signalParamChanged("DuplicateTimeout");
        signalParamChanged("CommandRedirect");

    // Auto generated code (Tools/params_utils.py:241)
    }
    bool checkShowReportViewOnWarning;
    bool checkShowReportViewOnError;
    bool checkShowReportViewOnNormalMessage;
    bool checkShowReportViewOnLogMessage;
    bool checkShowReportViewOnCritical;
    bool checkShowReportTimecode;
    long LogMessageSize;
    long DuplicateWindow;
    long DuplicateKeyLength;
    long DuplicateTimeout;
    QString CommandRedirect;

    // Auto generated code (Tools/params_utils.py:254)
    ReportViewParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/OutputWindow");
        handle->Attach(this);

        checkShowReportViewOnWarning = this->handle->GetBool("checkShowReportViewOnWarning", true);
        funcs["checkShowReportViewOnWarning"] = &ReportViewParamsP::updatecheckShowReportViewOnWarning;
        checkShowReportViewOnError = this->handle->GetBool("checkShowReportViewOnError", true);
        funcs["checkShowReportViewOnError"] = &ReportViewParamsP::updatecheckShowReportViewOnError;
        checkShowReportViewOnNormalMessage = this->handle->GetBool("checkShowReportViewOnNormalMessage", false);
        funcs["checkShowReportViewOnNormalMessage"] = &ReportViewParamsP::updatecheckShowReportViewOnNormalMessage;
        checkShowReportViewOnLogMessage = this->handle->GetBool("checkShowReportViewOnLogMessage", false);
        funcs["checkShowReportViewOnLogMessage"] = &ReportViewParamsP::updatecheckShowReportViewOnLogMessage;
        checkShowReportViewOnCritical = this->handle->GetBool("checkShowReportViewOnCritical", false);
        funcs["checkShowReportViewOnCritical"] = &ReportViewParamsP::updatecheckShowReportViewOnCritical;
        checkShowReportTimecode = this->handle->GetBool("checkShowReportTimecode", true);
        funcs["checkShowReportTimecode"] = &ReportViewParamsP::updatecheckShowReportTimecode;
        LogMessageSize = this->handle->GetInt("LogMessageSize", 0);
        funcs["LogMessageSize"] = &ReportViewParamsP::updateLogMessageSize;
        DuplicateWindow = this->handle->GetInt("DuplicateWindow", 3);
        funcs["DuplicateWindow"] = &ReportViewParamsP::updateDuplicateWindow;
        DuplicateKeyLength = this->handle->GetInt("DuplicateKeyLength", 100);
        funcs["DuplicateKeyLength"] = &ReportViewParamsP::updateDuplicateKeyLength;
        DuplicateTimeout = this->handle->GetInt("DuplicateTimeout", 1000);
        funcs["DuplicateTimeout"] = &ReportViewParamsP::updateDuplicateTimeout;
        CommandRedirect = QString::fromUtf8(this->handle->GetASCII("CommandRedirect", "").c_str());
        funcs["CommandRedirect"] = &ReportViewParamsP::updateCommandRedirect;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~ReportViewParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        signalParamChanged(sReason);
    }


    // Auto generated code (Tools/params_utils.py:314)
    static void updatecheckShowReportViewOnWarning(ReportViewParamsP *self) {
        self->checkShowReportViewOnWarning = self->handle->GetBool("checkShowReportViewOnWarning", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatecheckShowReportViewOnError(ReportViewParamsP *self) {
        self->checkShowReportViewOnError = self->handle->GetBool("checkShowReportViewOnError", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatecheckShowReportViewOnNormalMessage(ReportViewParamsP *self) {
        self->checkShowReportViewOnNormalMessage = self->handle->GetBool("checkShowReportViewOnNormalMessage", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatecheckShowReportViewOnLogMessage(ReportViewParamsP *self) {
        self->checkShowReportViewOnLogMessage = self->handle->GetBool("checkShowReportViewOnLogMessage", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatecheckShowReportViewOnCritical(ReportViewParamsP *self) {
        self->checkShowReportViewOnCritical = self->handle->GetBool("checkShowReportViewOnCritical", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatecheckShowReportTimecode(ReportViewParamsP *self) {
        self->checkShowReportTimecode = self->handle->GetBool("checkShowReportTimecode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLogMessageSize(ReportViewParamsP *self) {
        self->LogMessageSize = self->handle->GetInt("LogMessageSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDuplicateWindow(ReportViewParamsP *self) {
        self->DuplicateWindow = self->handle->GetInt("DuplicateWindow", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDuplicateKeyLength(ReportViewParamsP *self) {
        self->DuplicateKeyLength = self->handle->GetInt("DuplicateKeyLength", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDuplicateTimeout(ReportViewParamsP *self) {
        self->DuplicateTimeout = self->handle->GetInt("DuplicateTimeout", 1000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCommandRedirect(ReportViewParamsP *self) {
        self->CommandRedirect = QString::fromUtf8(self->handle->GetASCII("CommandRedirect", "").c_str());
    }
};

// Auto generated code (Tools/params_utils.py:336)
ReportViewParamsP *instance() {
    static ReportViewParamsP *inst = new ReportViewParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _ReportViewParamsRegistrar({
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "checkShowReportViewOnWarning", "checkShowReportViewOnWarning", App::ParamInfo::Bool, true)
        .setTitle("Show report view on warning")
        .setDoc("Bring the report view on screen when a warning arrives."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "checkShowReportViewOnError", "checkShowReportViewOnError", App::ParamInfo::Bool, true)
        .setTitle("Show report view on error")
        .setDoc("Bring the report view on screen when an error arrives."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "checkShowReportViewOnNormalMessage", "checkShowReportViewOnNormalMessage", App::ParamInfo::Bool, false)
        .setTitle("Show report view on normal message")
        .setDoc("Bring the report view on screen when a normal message arrives."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "checkShowReportViewOnLogMessage", "checkShowReportViewOnLogMessage", App::ParamInfo::Bool, false)
        .setTitle("Show report view on log message")
        .setDoc("Bring the report view on screen when a log message arrives."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "checkShowReportViewOnCritical", "checkShowReportViewOnCritical", App::ParamInfo::Bool, false)
        .setTitle("Show report view on critical message")
        .setDoc("Bring the report view on screen when a critical message arrives."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "checkShowReportTimecode", "checkShowReportTimecode", App::ParamInfo::Bool, true)
        .setTitle("Show time code")
        .setDoc("Put the time a message arrived in front of each line of the report\n"
"view."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "LogMessageSize", "LogMessageSize", App::ParamInfo::Int, 0)
        .setTitle("Log Message Size")
        .setDoc("Largest number of characters of one log message shown in the report\n"
"view. A longer message is cut off. 0 uses the built-in limit of 2048\n"
"characters."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "DuplicateWindow", "DuplicateWindow", App::ParamInfo::Int, 3)
        .setTitle("Duplicate Window")
        .setDoc("How many of the most recent lines a new line is compared with. A line\n"
"that repeats one of them is held back and shown once with a count (xN)\n"
"that can be clicked to expand. 0 shows every line. Affects the Report\n"
"view only; the log file and other consoles get every message."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "DuplicateKeyLength", "DuplicateKeyLength", App::ParamInfo::Int, 100)
        .setTitle("Duplicate Key Length")
        .setDoc("How many leading non-digit characters two messages must share to count\n"
"as the same message. Digits are skipped rather than compared, so the same\n"
"sentence carrying a different source line, element index or coordinate\n"
"collapses into one entry instead of one entry per number."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "DuplicateTimeout", "DuplicateTimeout", App::ParamInfo::Int, 1000)
        .setTitle("Duplicate Timeout")
        .setDoc("Milliseconds a held duplicate line waits before it is shown anyway, timed\n"
"from the first repeat rather than the last, so a continuous storm still\n"
"reports at this interval. Set to 0 to hold until another line arrives."),
    App::ParamInfo("Gui", "ReportViewParams", "User parameter:BaseApp/Preferences/OutputWindow", "CommandRedirect", "CommandRedirect", App::ParamInfo::String, "")
        .setTitle("Command Redirect")
        .setDoc("Prefix for marking python command in message to be redirected to Python console\n"
"This is used as a debug help for output command from external libraries"),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle ReportViewParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
ReportViewParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::doccheckShowReportViewOnWarning() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"Bring the report view on screen when a warning arrives.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ReportViewParams::getcheckShowReportViewOnWarning() {
    return instance()->checkShowReportViewOnWarning;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ReportViewParams::defaultcheckShowReportViewOnWarning() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setcheckShowReportViewOnWarning(const bool &v) {
    instance()->handle->SetBool("checkShowReportViewOnWarning",v);
    instance()->checkShowReportViewOnWarning = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removecheckShowReportViewOnWarning() {
    instance()->handle->RemoveBool("checkShowReportViewOnWarning");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::doccheckShowReportViewOnError() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"Bring the report view on screen when an error arrives.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ReportViewParams::getcheckShowReportViewOnError() {
    return instance()->checkShowReportViewOnError;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ReportViewParams::defaultcheckShowReportViewOnError() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setcheckShowReportViewOnError(const bool &v) {
    instance()->handle->SetBool("checkShowReportViewOnError",v);
    instance()->checkShowReportViewOnError = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removecheckShowReportViewOnError() {
    instance()->handle->RemoveBool("checkShowReportViewOnError");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::doccheckShowReportViewOnNormalMessage() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"Bring the report view on screen when a normal message arrives.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ReportViewParams::getcheckShowReportViewOnNormalMessage() {
    return instance()->checkShowReportViewOnNormalMessage;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ReportViewParams::defaultcheckShowReportViewOnNormalMessage() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setcheckShowReportViewOnNormalMessage(const bool &v) {
    instance()->handle->SetBool("checkShowReportViewOnNormalMessage",v);
    instance()->checkShowReportViewOnNormalMessage = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removecheckShowReportViewOnNormalMessage() {
    instance()->handle->RemoveBool("checkShowReportViewOnNormalMessage");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::doccheckShowReportViewOnLogMessage() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"Bring the report view on screen when a log message arrives.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ReportViewParams::getcheckShowReportViewOnLogMessage() {
    return instance()->checkShowReportViewOnLogMessage;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ReportViewParams::defaultcheckShowReportViewOnLogMessage() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setcheckShowReportViewOnLogMessage(const bool &v) {
    instance()->handle->SetBool("checkShowReportViewOnLogMessage",v);
    instance()->checkShowReportViewOnLogMessage = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removecheckShowReportViewOnLogMessage() {
    instance()->handle->RemoveBool("checkShowReportViewOnLogMessage");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::doccheckShowReportViewOnCritical() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"Bring the report view on screen when a critical message arrives.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ReportViewParams::getcheckShowReportViewOnCritical() {
    return instance()->checkShowReportViewOnCritical;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ReportViewParams::defaultcheckShowReportViewOnCritical() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setcheckShowReportViewOnCritical(const bool &v) {
    instance()->handle->SetBool("checkShowReportViewOnCritical",v);
    instance()->checkShowReportViewOnCritical = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removecheckShowReportViewOnCritical() {
    instance()->handle->RemoveBool("checkShowReportViewOnCritical");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::doccheckShowReportTimecode() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"Put the time a message arrived in front of each line of the report\n"
"view.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ReportViewParams::getcheckShowReportTimecode() {
    return instance()->checkShowReportTimecode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ReportViewParams::defaultcheckShowReportTimecode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setcheckShowReportTimecode(const bool &v) {
    instance()->handle->SetBool("checkShowReportTimecode",v);
    instance()->checkShowReportTimecode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removecheckShowReportTimecode() {
    instance()->handle->RemoveBool("checkShowReportTimecode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::docLogMessageSize() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"Largest number of characters of one log message shown in the report\n"
"view. A longer message is cut off. 0 uses the built-in limit of 2048\n"
"characters.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ReportViewParams::getLogMessageSize() {
    return instance()->LogMessageSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ReportViewParams::defaultLogMessageSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setLogMessageSize(const long &v) {
    instance()->handle->SetInt("LogMessageSize",v);
    instance()->LogMessageSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removeLogMessageSize() {
    instance()->handle->RemoveInt("LogMessageSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::docDuplicateWindow() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"How many of the most recent lines a new line is compared with. A line\n"
"that repeats one of them is held back and shown once with a count (xN)\n"
"that can be clicked to expand. 0 shows every line. Affects the Report\n"
"view only; the log file and other consoles get every message.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ReportViewParams::getDuplicateWindow() {
    return instance()->DuplicateWindow;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ReportViewParams::defaultDuplicateWindow() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setDuplicateWindow(const long &v) {
    instance()->handle->SetInt("DuplicateWindow",v);
    instance()->DuplicateWindow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removeDuplicateWindow() {
    instance()->handle->RemoveInt("DuplicateWindow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::docDuplicateKeyLength() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"How many leading non-digit characters two messages must share to count\n"
"as the same message. Digits are skipped rather than compared, so the same\n"
"sentence carrying a different source line, element index or coordinate\n"
"collapses into one entry instead of one entry per number.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ReportViewParams::getDuplicateKeyLength() {
    return instance()->DuplicateKeyLength;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ReportViewParams::defaultDuplicateKeyLength() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setDuplicateKeyLength(const long &v) {
    instance()->handle->SetInt("DuplicateKeyLength",v);
    instance()->DuplicateKeyLength = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removeDuplicateKeyLength() {
    instance()->handle->RemoveInt("DuplicateKeyLength");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::docDuplicateTimeout() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"Milliseconds a held duplicate line waits before it is shown anyway, timed\n"
"from the first repeat rather than the last, so a continuous storm still\n"
"reports at this interval. Set to 0 to hold until another line arrives.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ReportViewParams::getDuplicateTimeout() {
    return instance()->DuplicateTimeout;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ReportViewParams::defaultDuplicateTimeout() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setDuplicateTimeout(const long &v) {
    instance()->handle->SetInt("DuplicateTimeout",v);
    instance()->DuplicateTimeout = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removeDuplicateTimeout() {
    instance()->handle->RemoveInt("DuplicateTimeout");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ReportViewParams::docCommandRedirect() {
    return QT_TRANSLATE_NOOP("ReportViewParams",
"Prefix for marking python command in message to be redirected to Python console\n"
"This is used as a debug help for output command from external libraries");
}

// Auto generated code (Tools/params_utils.py:405)
const QString & ReportViewParams::getCommandRedirect() {
    return instance()->CommandRedirect;
}

// Auto generated code (Tools/params_utils.py:413)
const QString & ReportViewParams::defaultCommandRedirect() {
    const static QString def = QStringLiteral("");
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ReportViewParams::setCommandRedirect(const QString &v) {
    instance()->handle->SetASCII("CommandRedirect",v.toUtf8().constData());
    instance()->CommandRedirect = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ReportViewParams::removeCommandRedirect() {
    instance()->handle->RemoveASCII("CommandRedirect");
}
//[[[end]]]

