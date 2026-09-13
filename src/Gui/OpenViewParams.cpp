/****************************************************************************
 *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>             *
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
import OpenViewParams
OpenViewParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "OpenViewParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class OpenViewParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(OpenViewParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::string DocumentTarget;
    std::string DocViewTarget;
    std::string UtilityTarget;
    std::string SplitDirection;

    // Auto generated code (Tools/params_utils.py:254)
    OpenViewParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View/OpenView");
        handle->Attach(this);

        DocumentTarget = this->handle->GetASCII("DocumentTarget", "Tab");
        funcs["DocumentTarget"] = &OpenViewParamsP::updateDocumentTarget;
        DocViewTarget = this->handle->GetASCII("DocViewTarget", "Split");
        funcs["DocViewTarget"] = &OpenViewParamsP::updateDocViewTarget;
        UtilityTarget = this->handle->GetASCII("UtilityTarget", "Tab");
        funcs["UtilityTarget"] = &OpenViewParamsP::updateUtilityTarget;
        SplitDirection = this->handle->GetASCII("SplitDirection", "Auto");
        funcs["SplitDirection"] = &OpenViewParamsP::updateSplitDirection;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~OpenViewParamsP() override = default;

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
    static void updateDocumentTarget(OpenViewParamsP *self) {
        self->DocumentTarget = self->handle->GetASCII("DocumentTarget", "Tab");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDocViewTarget(OpenViewParamsP *self) {
        self->DocViewTarget = self->handle->GetASCII("DocViewTarget", "Split");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUtilityTarget(OpenViewParamsP *self) {
        self->UtilityTarget = self->handle->GetASCII("UtilityTarget", "Tab");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSplitDirection(OpenViewParamsP *self) {
        self->SplitDirection = self->handle->GetASCII("SplitDirection", "Auto");
    }
};

// Auto generated code (Tools/params_utils.py:336)
OpenViewParamsP *instance() {
    static OpenViewParamsP *inst = new OpenViewParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _OpenViewParamsRegistrar({
    App::ParamInfo("Gui", "OpenViewParams", "User parameter:BaseApp/Preferences/View/OpenView", "DocumentTarget", "DocumentTarget", App::ParamInfo::String, "Tab")
        .setTitle("New documents open in")
        .setDoc("Where the first view of a newly opened document lands")
        .setProxy("ComboBox")
        .setItems({{"Their own tab", "", "Tab"}, {"A split beside the current view", "", "Split"}, {"A floating window", "", "Floating"}}, true, true),
    App::ParamInfo("Gui", "OpenViewParams", "User parameter:BaseApp/Preferences/View/OpenView", "DocViewTarget", "DocViewTarget", App::ParamInfo::String, "Split")
        .setTitle("Additional views of a document open in")
        .setDoc("Where a second 3D view, a drawing page, a spreadsheet or the\n"
"CAM simulator of an already open document lands")
        .setProxy("ComboBox")
        .setItems({{"Their own tab", "", "Tab"}, {"A split beside the current view", "", "Split"}, {"A new split, never reusing a cell", "", "NewSplit"}, {"A floating window", "", "Floating"}}, true, true),
    App::ParamInfo("Gui", "OpenViewParams", "User parameter:BaseApp/Preferences/View/OpenView", "UtilityTarget", "UtilityTarget", App::ParamInfo::String, "Tab")
        .setTitle("Utility windows open in")
        .setDoc("Where a window that shows no document data lands, such as the\n"
"dependency graph")
        .setProxy("ComboBox")
        .setItems({{"Their own tab", "", "Tab"}, {"A split beside the current view", "", "Split"}, {"A floating window", "", "Floating"}}, true, true),
    App::ParamInfo("Gui", "OpenViewParams", "User parameter:BaseApp/Preferences/View/OpenView", "SplitDirection", "SplitDirection", App::ParamInfo::String, "Auto")
        .setTitle("New splits go")
        .setDoc("Which way a cell is divided when a view opens in a split")
        .setProxy("ComboBox")
        .setItems({{"Along the longer side", "", "Auto"}, {"To the right", "", "Right"}, {"Below", "", "Down"}}, true, true),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle OpenViewParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *OpenViewParams::docDocumentTarget() {
    return QT_TRANSLATE_NOOP("OpenViewParams",
"Where the first view of a newly opened document lands");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & OpenViewParams::getDocumentTarget() {
    return instance()->DocumentTarget;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & OpenViewParams::defaultDocumentTarget() {
    const static std::string def = "Tab";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void OpenViewParams::setDocumentTarget(const std::string &v) {
    instance()->handle->SetASCII("DocumentTarget",v);
    instance()->DocumentTarget = v;
}

// Auto generated code (Tools/params_utils.py:431)
void OpenViewParams::removeDocumentTarget() {
    instance()->handle->RemoveASCII("DocumentTarget");
}

// Auto generated code (Tools/params_utils.py:397)
const char *OpenViewParams::docDocViewTarget() {
    return QT_TRANSLATE_NOOP("OpenViewParams",
"Where a second 3D view, a drawing page, a spreadsheet or the\n"
"CAM simulator of an already open document lands");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & OpenViewParams::getDocViewTarget() {
    return instance()->DocViewTarget;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & OpenViewParams::defaultDocViewTarget() {
    const static std::string def = "Split";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void OpenViewParams::setDocViewTarget(const std::string &v) {
    instance()->handle->SetASCII("DocViewTarget",v);
    instance()->DocViewTarget = v;
}

// Auto generated code (Tools/params_utils.py:431)
void OpenViewParams::removeDocViewTarget() {
    instance()->handle->RemoveASCII("DocViewTarget");
}

// Auto generated code (Tools/params_utils.py:397)
const char *OpenViewParams::docUtilityTarget() {
    return QT_TRANSLATE_NOOP("OpenViewParams",
"Where a window that shows no document data lands, such as the\n"
"dependency graph");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & OpenViewParams::getUtilityTarget() {
    return instance()->UtilityTarget;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & OpenViewParams::defaultUtilityTarget() {
    const static std::string def = "Tab";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void OpenViewParams::setUtilityTarget(const std::string &v) {
    instance()->handle->SetASCII("UtilityTarget",v);
    instance()->UtilityTarget = v;
}

// Auto generated code (Tools/params_utils.py:431)
void OpenViewParams::removeUtilityTarget() {
    instance()->handle->RemoveASCII("UtilityTarget");
}

// Auto generated code (Tools/params_utils.py:397)
const char *OpenViewParams::docSplitDirection() {
    return QT_TRANSLATE_NOOP("OpenViewParams",
"Which way a cell is divided when a view opens in a split");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & OpenViewParams::getSplitDirection() {
    return instance()->SplitDirection;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & OpenViewParams::defaultSplitDirection() {
    const static std::string def = "Auto";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void OpenViewParams::setSplitDirection(const std::string &v) {
    instance()->handle->SetASCII("SplitDirection",v);
    instance()->SplitDirection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void OpenViewParams::removeSplitDirection() {
    instance()->handle->RemoveASCII("SplitDirection");
}
//[[[end]]]
