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
import AssemblyParams
AssemblyParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "AssemblyParams.h"
using namespace Assembly;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class AssemblyParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(AssemblyParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    bool SolveOnRecompute;
    bool SolveOnMove;
    bool LeaveEditWithEscape;
    bool SwitchToWB;
    unsigned long JointHighlightColor;
    bool LogSolverDebug;
    std::string BomMirroredSuffix;

    // Auto generated code (Tools/params_utils.py:254)
    AssemblyParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Assembly");
        handle->Attach(this);

        SolveOnRecompute = this->handle->GetBool("SolveOnRecompute", true);
        funcs["SolveOnRecompute"] = &AssemblyParamsP::updateSolveOnRecompute;
        SolveOnMove = this->handle->GetBool("SolveOnMove", true);
        funcs["SolveOnMove"] = &AssemblyParamsP::updateSolveOnMove;
        LeaveEditWithEscape = this->handle->GetBool("LeaveEditWithEscape", true);
        funcs["LeaveEditWithEscape"] = &AssemblyParamsP::updateLeaveEditWithEscape;
        SwitchToWB = this->handle->GetBool("SwitchToWB", true);
        funcs["SwitchToWB"] = &AssemblyParamsP::updateSwitchToWB;
        JointHighlightColor = this->handle->GetUnsigned("JointHighlightColor", 0xCC1A1AFF);
        funcs["JointHighlightColor"] = &AssemblyParamsP::updateJointHighlightColor;
        LogSolverDebug = this->handle->GetBool("LogSolverDebug", false);
        funcs["LogSolverDebug"] = &AssemblyParamsP::updateLogSolverDebug;
        BomMirroredSuffix = this->handle->GetASCII("BomMirroredSuffix", " (mirrored)");
        funcs["BomMirroredSuffix"] = &AssemblyParamsP::updateBomMirroredSuffix;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~AssemblyParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
    }


    // Auto generated code (Tools/params_utils.py:314)
    static void updateSolveOnRecompute(AssemblyParamsP *self) {
        self->SolveOnRecompute = self->handle->GetBool("SolveOnRecompute", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSolveOnMove(AssemblyParamsP *self) {
        self->SolveOnMove = self->handle->GetBool("SolveOnMove", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLeaveEditWithEscape(AssemblyParamsP *self) {
        self->LeaveEditWithEscape = self->handle->GetBool("LeaveEditWithEscape", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSwitchToWB(AssemblyParamsP *self) {
        self->SwitchToWB = self->handle->GetBool("SwitchToWB", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateJointHighlightColor(AssemblyParamsP *self) {
        self->JointHighlightColor = self->handle->GetUnsigned("JointHighlightColor", 0xCC1A1AFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLogSolverDebug(AssemblyParamsP *self) {
        self->LogSolverDebug = self->handle->GetBool("LogSolverDebug", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBomMirroredSuffix(AssemblyParamsP *self) {
        self->BomMirroredSuffix = self->handle->GetASCII("BomMirroredSuffix", " (mirrored)");
    }
};

// Auto generated code (Tools/params_utils.py:336)
AssemblyParamsP *instance() {
    static AssemblyParamsP *inst = new AssemblyParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _AssemblyParamsRegistrar({
    App::ParamInfo("Assembly", "AssemblyParams", "User parameter:BaseApp/Preferences/Mod/Assembly", "SolveOnRecompute", "SolveOnRecompute", App::ParamInfo::Bool, true)
        .setTitle("Solve on recompute")
        .setDoc("Solves the joints of an assembly every time the assembly is\n"
"recomputed. Takes effect at the next recompute."),
    App::ParamInfo("Assembly", "AssemblyParams", "User parameter:BaseApp/Preferences/Mod/Assembly", "SolveOnMove", "SolveOnMove", App::ParamInfo::Bool, true)
        .setTitle("Solve while dragging")
        .setDoc("Solves the joints continuously while a part is dragged, so that\n"
"connected parts follow. When off, only the joint markers are\n"
"redrawn during the drag. Takes effect at the next drag."),
    App::ParamInfo("Assembly", "AssemblyParams", "User parameter:BaseApp/Preferences/Mod/Assembly", "LeaveEditWithEscape", "LeaveEditWithEscape", App::ParamInfo::Bool, true)
        .setTitle("Esc leaves edit mode")
        .setDoc("Lets the Esc key leave the edit mode of an assembly when no task\n"
"dialogue is open. Takes effect at once."),
    App::ParamInfo("Assembly", "AssemblyParams", "User parameter:BaseApp/Preferences/Mod/Assembly", "SwitchToWB", "SwitchToWB", App::ParamInfo::Bool, true)
        .setTitle("Switch to Assembly workbench")
        .setDoc("Switches to the Assembly workbench when an assembly is double-\n"
"clicked for editing. Takes effect at the next double-click."),
    App::ParamInfo("Assembly", "AssemblyParams", "User parameter:BaseApp/Preferences/Mod/Assembly", "JointHighlightColor", "JointHighlightColor", App::ParamInfo::Hex, 0xCC1A1AFF)
        .setTitle("Joint highlight colour")
        .setDoc("Colour the elements a joint connects are shown in while the\n"
"joint is selected or edited: a red, unless set. Takes effect at\n"
"the next highlight.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Assembly", "AssemblyParams", "User parameter:BaseApp/Preferences/Mod/Assembly", "LogSolverDebug", "LogSolverDebug", App::ParamInfo::Bool, false)
        .setTitle("Log dragging steps")
        .setDoc("Writes the dragging steps of the solver to the files\n"
"runPreDrag.asmt and dragging.log, which helps when reporting a\n"
"solver problem. Takes effect the next time the assembly is solved."),
    App::ParamInfo("Assembly", "AssemblyParams", "User parameter:BaseApp/Preferences/Mod/Assembly", "BomMirroredSuffix", "BomMirroredSuffix", App::ParamInfo::String, " (mirrored)")
        .setTitle("Mirrored part suffix")
        .setDoc("Text appended to the name of a mirrored part in a bill of\n"
"materials. Takes effect the next time the bill of materials is\n"
"generated."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle AssemblyParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *AssemblyParams::docSolveOnRecompute() {
    return QT_TRANSLATE_NOOP("AssemblyParams",
"Solves the joints of an assembly every time the assembly is\n"
"recomputed. Takes effect at the next recompute.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & AssemblyParams::getSolveOnRecompute() {
    return instance()->SolveOnRecompute;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & AssemblyParams::defaultSolveOnRecompute() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void AssemblyParams::setSolveOnRecompute(const bool &v) {
    instance()->handle->SetBool("SolveOnRecompute",v);
    instance()->SolveOnRecompute = v;
}

// Auto generated code (Tools/params_utils.py:431)
void AssemblyParams::removeSolveOnRecompute() {
    instance()->handle->RemoveBool("SolveOnRecompute");
}

// Auto generated code (Tools/params_utils.py:397)
const char *AssemblyParams::docSolveOnMove() {
    return QT_TRANSLATE_NOOP("AssemblyParams",
"Solves the joints continuously while a part is dragged, so that\n"
"connected parts follow. When off, only the joint markers are\n"
"redrawn during the drag. Takes effect at the next drag.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & AssemblyParams::getSolveOnMove() {
    return instance()->SolveOnMove;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & AssemblyParams::defaultSolveOnMove() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void AssemblyParams::setSolveOnMove(const bool &v) {
    instance()->handle->SetBool("SolveOnMove",v);
    instance()->SolveOnMove = v;
}

// Auto generated code (Tools/params_utils.py:431)
void AssemblyParams::removeSolveOnMove() {
    instance()->handle->RemoveBool("SolveOnMove");
}

// Auto generated code (Tools/params_utils.py:397)
const char *AssemblyParams::docLeaveEditWithEscape() {
    return QT_TRANSLATE_NOOP("AssemblyParams",
"Lets the Esc key leave the edit mode of an assembly when no task\n"
"dialogue is open. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & AssemblyParams::getLeaveEditWithEscape() {
    return instance()->LeaveEditWithEscape;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & AssemblyParams::defaultLeaveEditWithEscape() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void AssemblyParams::setLeaveEditWithEscape(const bool &v) {
    instance()->handle->SetBool("LeaveEditWithEscape",v);
    instance()->LeaveEditWithEscape = v;
}

// Auto generated code (Tools/params_utils.py:431)
void AssemblyParams::removeLeaveEditWithEscape() {
    instance()->handle->RemoveBool("LeaveEditWithEscape");
}

// Auto generated code (Tools/params_utils.py:397)
const char *AssemblyParams::docSwitchToWB() {
    return QT_TRANSLATE_NOOP("AssemblyParams",
"Switches to the Assembly workbench when an assembly is double-\n"
"clicked for editing. Takes effect at the next double-click.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & AssemblyParams::getSwitchToWB() {
    return instance()->SwitchToWB;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & AssemblyParams::defaultSwitchToWB() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void AssemblyParams::setSwitchToWB(const bool &v) {
    instance()->handle->SetBool("SwitchToWB",v);
    instance()->SwitchToWB = v;
}

// Auto generated code (Tools/params_utils.py:431)
void AssemblyParams::removeSwitchToWB() {
    instance()->handle->RemoveBool("SwitchToWB");
}

// Auto generated code (Tools/params_utils.py:397)
const char *AssemblyParams::docJointHighlightColor() {
    return QT_TRANSLATE_NOOP("AssemblyParams",
"Colour the elements a joint connects are shown in while the\n"
"joint is selected or edited: a red, unless set. Takes effect at\n"
"the next highlight.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & AssemblyParams::getJointHighlightColor() {
    return instance()->JointHighlightColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & AssemblyParams::defaultJointHighlightColor() {
    const static unsigned long def = 0xCC1A1AFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void AssemblyParams::setJointHighlightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("JointHighlightColor",v);
    instance()->JointHighlightColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void AssemblyParams::removeJointHighlightColor() {
    instance()->handle->RemoveUnsigned("JointHighlightColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *AssemblyParams::docLogSolverDebug() {
    return QT_TRANSLATE_NOOP("AssemblyParams",
"Writes the dragging steps of the solver to the files\n"
"runPreDrag.asmt and dragging.log, which helps when reporting a\n"
"solver problem. Takes effect the next time the assembly is solved.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & AssemblyParams::getLogSolverDebug() {
    return instance()->LogSolverDebug;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & AssemblyParams::defaultLogSolverDebug() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void AssemblyParams::setLogSolverDebug(const bool &v) {
    instance()->handle->SetBool("LogSolverDebug",v);
    instance()->LogSolverDebug = v;
}

// Auto generated code (Tools/params_utils.py:431)
void AssemblyParams::removeLogSolverDebug() {
    instance()->handle->RemoveBool("LogSolverDebug");
}

// Auto generated code (Tools/params_utils.py:397)
const char *AssemblyParams::docBomMirroredSuffix() {
    return QT_TRANSLATE_NOOP("AssemblyParams",
"Text appended to the name of a mirrored part in a bill of\n"
"materials. Takes effect the next time the bill of materials is\n"
"generated.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & AssemblyParams::getBomMirroredSuffix() {
    return instance()->BomMirroredSuffix;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & AssemblyParams::defaultBomMirroredSuffix() {
    const static std::string def = " (mirrored)";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void AssemblyParams::setBomMirroredSuffix(const std::string &v) {
    instance()->handle->SetASCII("BomMirroredSuffix",v);
    instance()->BomMirroredSuffix = v;
}

// Auto generated code (Tools/params_utils.py:431)
void AssemblyParams::removeBomMirroredSuffix() {
    instance()->handle->RemoveASCII("BomMirroredSuffix");
}
//[[[end]]]
