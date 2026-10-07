/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
import PythonConsoleParams
PythonConsoleParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "PythonConsoleParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class PythonConsoleParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(PythonConsoleParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("PythonWordWrap");
        signalParamChanged("PythonBlockCursor");
        signalParamChanged("SavePythonHistory");
        signalParamChanged("ProfilerInterval");
        signalParamChanged("ExternalPythonExecutable");

    // Auto generated code (Tools/params_utils.py:241)
    }
    bool PythonWordWrap;
    bool PythonBlockCursor;
    bool SavePythonHistory;
    long ProfilerInterval;
    std::string ExternalPythonExecutable;

    // Auto generated code (Tools/params_utils.py:254)
    PythonConsoleParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/PythonConsole");
        handle->Attach(this);

        PythonWordWrap = this->handle->GetBool("PythonWordWrap", true);
        funcs["PythonWordWrap"] = &PythonConsoleParamsP::updatePythonWordWrap;
        PythonBlockCursor = this->handle->GetBool("PythonBlockCursor", false);
        funcs["PythonBlockCursor"] = &PythonConsoleParamsP::updatePythonBlockCursor;
        SavePythonHistory = this->handle->GetBool("SavePythonHistory", false);
        funcs["SavePythonHistory"] = &PythonConsoleParamsP::updateSavePythonHistory;
        ProfilerInterval = this->handle->GetInt("ProfilerInterval", 200);
        funcs["ProfilerInterval"] = &PythonConsoleParamsP::updateProfilerInterval;
        ExternalPythonExecutable = this->handle->GetASCII("ExternalPythonExecutable", "");
        funcs["ExternalPythonExecutable"] = &PythonConsoleParamsP::updateExternalPythonExecutable;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~PythonConsoleParamsP() override = default;

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
    static void updatePythonWordWrap(PythonConsoleParamsP *self) {
        self->PythonWordWrap = self->handle->GetBool("PythonWordWrap", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePythonBlockCursor(PythonConsoleParamsP *self) {
        self->PythonBlockCursor = self->handle->GetBool("PythonBlockCursor", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSavePythonHistory(PythonConsoleParamsP *self) {
        self->SavePythonHistory = self->handle->GetBool("SavePythonHistory", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateProfilerInterval(PythonConsoleParamsP *self) {
        self->ProfilerInterval = self->handle->GetInt("ProfilerInterval", 200);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalPythonExecutable(PythonConsoleParamsP *self) {
        self->ExternalPythonExecutable = self->handle->GetASCII("ExternalPythonExecutable", "");
    }
};

// Auto generated code (Tools/params_utils.py:336)
PythonConsoleParamsP *instance() {
    static PythonConsoleParamsP *inst = new PythonConsoleParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _PythonConsoleParamsRegistrar({
    App::ParamInfo("Gui", "PythonConsoleParams", "User parameter:BaseApp/Preferences/PythonConsole", "PythonWordWrap", "PythonWordWrap", App::ParamInfo::Bool, true)
        .setTitle("Word wrap")
        .setDoc("Wrap lines of the Python console that are longer than the window\n"
"is wide. Also in the console's context menu. Takes effect at once."),
    App::ParamInfo("Gui", "PythonConsoleParams", "User parameter:BaseApp/Preferences/PythonConsole", "PythonBlockCursor", "PythonBlockCursor", App::ParamInfo::Bool, false)
        .setTitle("Block cursor")
        .setDoc("Draw the Python console's text cursor as a block one character\n"
"wide instead of a line. Takes effect at once."),
    App::ParamInfo("Gui", "PythonConsoleParams", "User parameter:BaseApp/Preferences/PythonConsole", "SavePythonHistory", "SavePythonHistory", App::ParamInfo::Bool, false)
        .setTitle("Save history")
        .setDoc("Keep the Python console's command history between sessions: the\n"
"last 100 entries, written when the program ends and read at the\n"
"next start. Also in the console's context menu."),
    App::ParamInfo("Gui", "PythonConsoleParams", "User parameter:BaseApp/Preferences/PythonConsole", "ProfilerInterval", "ProfilerInterval", App::ParamInfo::Int, 200)
        .setTitle("Python profiler interval")
        .setDoc("Interval in milliseconds at which running Python code lets the\n"
"user interface process events, so that it stays responsive. 0\n"
"turns that off. Read when a script or command starts."),
    App::ParamInfo("Gui", "PythonConsoleParams", "User parameter:BaseApp/Preferences/PythonConsole", "ExternalPythonExecutable", "ExternalPythonExecutable", App::ParamInfo::String, "")
        .setTitle("Path to external Python executable")
        .setDoc("Python executable used for work done outside the program, such as\n"
"installing packages with pip or debugging with debugpy. Empty lets\n"
"the program look for one when it is needed, and store what it\n"
"found."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle PythonConsoleParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
PythonConsoleParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void PythonConsoleParams::signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *PythonConsoleParams::docPythonWordWrap() {
    return QT_TRANSLATE_NOOP("PythonConsoleParams",
"Wrap lines of the Python console that are longer than the window\n"
"is wide. Also in the console's context menu. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PythonConsoleParams::getPythonWordWrap() {
    return instance()->PythonWordWrap;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PythonConsoleParams::defaultPythonWordWrap() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PythonConsoleParams::setPythonWordWrap(const bool &v) {
    instance()->handle->SetBool("PythonWordWrap",v);
    instance()->PythonWordWrap = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PythonConsoleParams::removePythonWordWrap() {
    instance()->handle->RemoveBool("PythonWordWrap");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PythonConsoleParams::docPythonBlockCursor() {
    return QT_TRANSLATE_NOOP("PythonConsoleParams",
"Draw the Python console's text cursor as a block one character\n"
"wide instead of a line. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PythonConsoleParams::getPythonBlockCursor() {
    return instance()->PythonBlockCursor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PythonConsoleParams::defaultPythonBlockCursor() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PythonConsoleParams::setPythonBlockCursor(const bool &v) {
    instance()->handle->SetBool("PythonBlockCursor",v);
    instance()->PythonBlockCursor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PythonConsoleParams::removePythonBlockCursor() {
    instance()->handle->RemoveBool("PythonBlockCursor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PythonConsoleParams::docSavePythonHistory() {
    return QT_TRANSLATE_NOOP("PythonConsoleParams",
"Keep the Python console's command history between sessions: the\n"
"last 100 entries, written when the program ends and read at the\n"
"next start. Also in the console's context menu.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PythonConsoleParams::getSavePythonHistory() {
    return instance()->SavePythonHistory;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PythonConsoleParams::defaultSavePythonHistory() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PythonConsoleParams::setSavePythonHistory(const bool &v) {
    instance()->handle->SetBool("SavePythonHistory",v);
    instance()->SavePythonHistory = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PythonConsoleParams::removeSavePythonHistory() {
    instance()->handle->RemoveBool("SavePythonHistory");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PythonConsoleParams::docProfilerInterval() {
    return QT_TRANSLATE_NOOP("PythonConsoleParams",
"Interval in milliseconds at which running Python code lets the\n"
"user interface process events, so that it stays responsive. 0\n"
"turns that off. Read when a script or command starts.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PythonConsoleParams::getProfilerInterval() {
    return instance()->ProfilerInterval;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PythonConsoleParams::defaultProfilerInterval() {
    const static long def = 200;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PythonConsoleParams::setProfilerInterval(const long &v) {
    instance()->handle->SetInt("ProfilerInterval",v);
    instance()->ProfilerInterval = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PythonConsoleParams::removeProfilerInterval() {
    instance()->handle->RemoveInt("ProfilerInterval");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PythonConsoleParams::docExternalPythonExecutable() {
    return QT_TRANSLATE_NOOP("PythonConsoleParams",
"Python executable used for work done outside the program, such as\n"
"installing packages with pip or debugging with debugpy. Empty lets\n"
"the program look for one when it is needed, and store what it\n"
"found.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & PythonConsoleParams::getExternalPythonExecutable() {
    return instance()->ExternalPythonExecutable;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & PythonConsoleParams::defaultExternalPythonExecutable() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PythonConsoleParams::setExternalPythonExecutable(const std::string &v) {
    instance()->handle->SetASCII("ExternalPythonExecutable",v);
    instance()->ExternalPythonExecutable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PythonConsoleParams::removeExternalPythonExecutable() {
    instance()->handle->RemoveASCII("ExternalPythonExecutable");
}
//[[[end]]]
