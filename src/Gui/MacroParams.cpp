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
import MacroParams
MacroParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "MacroParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class MacroParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(MacroParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("MacroPath");
        signalParamChanged("LocalEnvironment");
        signalParamChanged("RecordGui");
        signalParamChanged("GuiAsComment");
        signalParamChanged("ScriptToPyConsole");
        signalParamChanged("ReplaceSpaces");
        signalParamChanged("DuplicateFrom001");
        signalParamChanged("DuplicateIgnoreExtraNote");

    // Auto generated code (Tools/params_utils.py:241)
    }
    std::string MacroPath;
    bool LocalEnvironment;
    bool RecordGui;
    bool GuiAsComment;
    bool ScriptToPyConsole;
    bool ReplaceSpaces;
    bool DuplicateFrom001;
    bool DuplicateIgnoreExtraNote;

    // Auto generated code (Tools/params_utils.py:254)
    MacroParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Macro");
        handle->Attach(this);

        MacroPath = this->handle->GetASCII("MacroPath", "");
        funcs["MacroPath"] = &MacroParamsP::updateMacroPath;
        LocalEnvironment = this->handle->GetBool("LocalEnvironment", true);
        funcs["LocalEnvironment"] = &MacroParamsP::updateLocalEnvironment;
        RecordGui = this->handle->GetBool("RecordGui", true);
        funcs["RecordGui"] = &MacroParamsP::updateRecordGui;
        GuiAsComment = this->handle->GetBool("GuiAsComment", true);
        funcs["GuiAsComment"] = &MacroParamsP::updateGuiAsComment;
        ScriptToPyConsole = this->handle->GetBool("ScriptToPyConsole", true);
        funcs["ScriptToPyConsole"] = &MacroParamsP::updateScriptToPyConsole;
        ReplaceSpaces = this->handle->GetBool("ReplaceSpaces", true);
        funcs["ReplaceSpaces"] = &MacroParamsP::updateReplaceSpaces;
        DuplicateFrom001 = this->handle->GetBool("DuplicateFrom001", false);
        funcs["DuplicateFrom001"] = &MacroParamsP::updateDuplicateFrom001;
        DuplicateIgnoreExtraNote = this->handle->GetBool("DuplicateIgnoreExtraNote", false);
        funcs["DuplicateIgnoreExtraNote"] = &MacroParamsP::updateDuplicateIgnoreExtraNote;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~MacroParamsP() override = default;

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
    static void updateMacroPath(MacroParamsP *self) {
        self->MacroPath = self->handle->GetASCII("MacroPath", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLocalEnvironment(MacroParamsP *self) {
        self->LocalEnvironment = self->handle->GetBool("LocalEnvironment", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRecordGui(MacroParamsP *self) {
        self->RecordGui = self->handle->GetBool("RecordGui", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGuiAsComment(MacroParamsP *self) {
        self->GuiAsComment = self->handle->GetBool("GuiAsComment", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateScriptToPyConsole(MacroParamsP *self) {
        self->ScriptToPyConsole = self->handle->GetBool("ScriptToPyConsole", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateReplaceSpaces(MacroParamsP *self) {
        self->ReplaceSpaces = self->handle->GetBool("ReplaceSpaces", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDuplicateFrom001(MacroParamsP *self) {
        self->DuplicateFrom001 = self->handle->GetBool("DuplicateFrom001", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDuplicateIgnoreExtraNote(MacroParamsP *self) {
        self->DuplicateIgnoreExtraNote = self->handle->GetBool("DuplicateIgnoreExtraNote", false);
    }
};

// Auto generated code (Tools/params_utils.py:336)
MacroParamsP *instance() {
    static MacroParamsP *inst = new MacroParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _MacroParamsRegistrar({
    App::ParamInfo("Gui", "MacroParams", "User parameter:BaseApp/Preferences/Macro", "MacroPath", "MacroPath", App::ParamInfo::String, "")
        .setTitle("Macro path")
        .setDoc("Directory where the user's macros are stored and looked for.\n"
"Empty or not set means the Macro directory of the user's\n"
"application data. Read each time macros are listed, recorded or\n"
"run."),
    App::ParamInfo("Gui", "MacroParams", "User parameter:BaseApp/Preferences/Macro", "LocalEnvironment", "LocalEnvironment", App::ParamInfo::Bool, true)
        .setTitle("Run macros in local environment")
        .setDoc("Run a macro in an environment of its own, so that the variables\n"
"it defines do not stay in the Python interpreter afterwards.\n"
"Applies to the next macro run."),
    App::ParamInfo("Gui", "MacroParams", "User parameter:BaseApp/Preferences/Macro", "RecordGui", "RecordGui", App::ParamInfo::Bool, true)
        .setTitle("Record GUI commands")
        .setDoc("Write commands that only affect the user interface -- selection,\n"
"view changes -- to a macro being recorded as well."),
    App::ParamInfo("Gui", "MacroParams", "User parameter:BaseApp/Preferences/Macro", "GuiAsComment", "GuiAsComment", App::ParamInfo::Bool, true)
        .setTitle("Record as comment")
        .setDoc("Write the recorded user interface commands as comment lines, so\n"
"that the macro shows them and does not run them."),
    App::ParamInfo("Gui", "MacroParams", "User parameter:BaseApp/Preferences/Macro", "ScriptToPyConsole", "ScriptToPyConsole", App::ParamInfo::Bool, true)
        .setTitle("Show script commands in Python console")
        .setDoc("Show every command the program issues for a menu or tool bar\n"
"action in the Python console, as it is carried out."),
    App::ParamInfo("Gui", "MacroParams", "User parameter:BaseApp/Preferences/Macro", "ReplaceSpaces", "ReplaceSpaces", App::ParamInfo::Bool, true)
        .setTitle("Replace spaces in macro names")
        .setDoc("Replace spaces by underscores in a macro file name entered in the\n"
"Macro dialog: on create, rename and duplicate."),
    App::ParamInfo("Gui", "MacroParams", "User parameter:BaseApp/Preferences/Macro", "DuplicateFrom001", "DuplicateFrom001", App::ParamInfo::Bool, false)
        .setTitle("Number duplicates from 001")
        .setDoc("When a macro whose name ends in @ and three digits is duplicated,\n"
"look for a free name starting at @001 instead of continuing from\n"
"that macro's own number."),
    App::ParamInfo("Gui", "MacroParams", "User parameter:BaseApp/Preferences/Macro", "DuplicateIgnoreExtraNote", "DuplicateIgnoreExtraNote", App::ParamInfo::Bool, false)
        .setTitle("Drop the note when duplicating")
        .setDoc("When a macro is duplicated, leave out of the suggested name\n"
"whatever stands between the number and the file extension."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle MacroParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
MacroParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void MacroParams::signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *MacroParams::docMacroPath() {
    return QT_TRANSLATE_NOOP("MacroParams",
"Directory where the user's macros are stored and looked for.\n"
"Empty or not set means the Macro directory of the user's\n"
"application data. Read each time macros are listed, recorded or\n"
"run.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MacroParams::getMacroPath() {
    return instance()->MacroPath;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MacroParams::defaultMacroPath() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MacroParams::setMacroPath(const std::string &v) {
    instance()->handle->SetASCII("MacroPath",v);
    instance()->MacroPath = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MacroParams::removeMacroPath() {
    instance()->handle->RemoveASCII("MacroPath");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MacroParams::docLocalEnvironment() {
    return QT_TRANSLATE_NOOP("MacroParams",
"Run a macro in an environment of its own, so that the variables\n"
"it defines do not stay in the Python interpreter afterwards.\n"
"Applies to the next macro run.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MacroParams::getLocalEnvironment() {
    return instance()->LocalEnvironment;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MacroParams::defaultLocalEnvironment() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MacroParams::setLocalEnvironment(const bool &v) {
    instance()->handle->SetBool("LocalEnvironment",v);
    instance()->LocalEnvironment = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MacroParams::removeLocalEnvironment() {
    instance()->handle->RemoveBool("LocalEnvironment");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MacroParams::docRecordGui() {
    return QT_TRANSLATE_NOOP("MacroParams",
"Write commands that only affect the user interface -- selection,\n"
"view changes -- to a macro being recorded as well.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MacroParams::getRecordGui() {
    return instance()->RecordGui;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MacroParams::defaultRecordGui() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MacroParams::setRecordGui(const bool &v) {
    instance()->handle->SetBool("RecordGui",v);
    instance()->RecordGui = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MacroParams::removeRecordGui() {
    instance()->handle->RemoveBool("RecordGui");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MacroParams::docGuiAsComment() {
    return QT_TRANSLATE_NOOP("MacroParams",
"Write the recorded user interface commands as comment lines, so\n"
"that the macro shows them and does not run them.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MacroParams::getGuiAsComment() {
    return instance()->GuiAsComment;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MacroParams::defaultGuiAsComment() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MacroParams::setGuiAsComment(const bool &v) {
    instance()->handle->SetBool("GuiAsComment",v);
    instance()->GuiAsComment = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MacroParams::removeGuiAsComment() {
    instance()->handle->RemoveBool("GuiAsComment");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MacroParams::docScriptToPyConsole() {
    return QT_TRANSLATE_NOOP("MacroParams",
"Show every command the program issues for a menu or tool bar\n"
"action in the Python console, as it is carried out.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MacroParams::getScriptToPyConsole() {
    return instance()->ScriptToPyConsole;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MacroParams::defaultScriptToPyConsole() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MacroParams::setScriptToPyConsole(const bool &v) {
    instance()->handle->SetBool("ScriptToPyConsole",v);
    instance()->ScriptToPyConsole = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MacroParams::removeScriptToPyConsole() {
    instance()->handle->RemoveBool("ScriptToPyConsole");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MacroParams::docReplaceSpaces() {
    return QT_TRANSLATE_NOOP("MacroParams",
"Replace spaces by underscores in a macro file name entered in the\n"
"Macro dialog: on create, rename and duplicate.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MacroParams::getReplaceSpaces() {
    return instance()->ReplaceSpaces;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MacroParams::defaultReplaceSpaces() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MacroParams::setReplaceSpaces(const bool &v) {
    instance()->handle->SetBool("ReplaceSpaces",v);
    instance()->ReplaceSpaces = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MacroParams::removeReplaceSpaces() {
    instance()->handle->RemoveBool("ReplaceSpaces");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MacroParams::docDuplicateFrom001() {
    return QT_TRANSLATE_NOOP("MacroParams",
"When a macro whose name ends in @ and three digits is duplicated,\n"
"look for a free name starting at @001 instead of continuing from\n"
"that macro's own number.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MacroParams::getDuplicateFrom001() {
    return instance()->DuplicateFrom001;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MacroParams::defaultDuplicateFrom001() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MacroParams::setDuplicateFrom001(const bool &v) {
    instance()->handle->SetBool("DuplicateFrom001",v);
    instance()->DuplicateFrom001 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MacroParams::removeDuplicateFrom001() {
    instance()->handle->RemoveBool("DuplicateFrom001");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MacroParams::docDuplicateIgnoreExtraNote() {
    return QT_TRANSLATE_NOOP("MacroParams",
"When a macro is duplicated, leave out of the suggested name\n"
"whatever stands between the number and the file extension.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MacroParams::getDuplicateIgnoreExtraNote() {
    return instance()->DuplicateIgnoreExtraNote;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MacroParams::defaultDuplicateIgnoreExtraNote() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MacroParams::setDuplicateIgnoreExtraNote(const bool &v) {
    instance()->handle->SetBool("DuplicateIgnoreExtraNote",v);
    instance()->DuplicateIgnoreExtraNote = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MacroParams::removeDuplicateIgnoreExtraNote() {
    instance()->handle->RemoveBool("DuplicateIgnoreExtraNote");
}
//[[[end]]]
