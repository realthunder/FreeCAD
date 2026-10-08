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
import PartDesignParams
PartDesignParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "PartDesignParams.h"
using namespace PartDesign;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class PartDesignParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(PartDesignParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    bool RefineModel;
    long defaultBaseTypeHole;
    bool NewSketchUseAttachmentDialog;
    bool BooleanDeleteOnRemove;
    bool SwitchToWB;
    bool SwitchToTask;
    long CoordinateSystemFontSize;
    double CoordinateSystemZoom;
    bool CoordinateSystemShowLabel;
    bool CoordinateSystemSelectOnTop;

    // Auto generated code (Tools/params_utils.py:254)
    PartDesignParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/PartDesign");
        handle->Attach(this);

        RefineModel = this->handle->GetBool("RefineModel", false);
        funcs["RefineModel"] = &PartDesignParamsP::updateRefineModel;
        defaultBaseTypeHole = this->handle->GetInt("defaultBaseTypeHole", 1);
        funcs["defaultBaseTypeHole"] = &PartDesignParamsP::updatedefaultBaseTypeHole;
        NewSketchUseAttachmentDialog = this->handle->GetBool("NewSketchUseAttachmentDialog", false);
        funcs["NewSketchUseAttachmentDialog"] = &PartDesignParamsP::updateNewSketchUseAttachmentDialog;
        BooleanDeleteOnRemove = this->handle->GetBool("BooleanDeleteOnRemove", true);
        funcs["BooleanDeleteOnRemove"] = &PartDesignParamsP::updateBooleanDeleteOnRemove;
        SwitchToWB = this->handle->GetBool("SwitchToWB", true);
        funcs["SwitchToWB"] = &PartDesignParamsP::updateSwitchToWB;
        SwitchToTask = this->handle->GetBool("SwitchToTask", true);
        funcs["SwitchToTask"] = &PartDesignParamsP::updateSwitchToTask;
        CoordinateSystemFontSize = this->handle->GetInt("CoordinateSystemFontSize", 10);
        funcs["CoordinateSystemFontSize"] = &PartDesignParamsP::updateCoordinateSystemFontSize;
        CoordinateSystemZoom = this->handle->GetFloat("CoordinateSystemZoom", 1.0);
        funcs["CoordinateSystemZoom"] = &PartDesignParamsP::updateCoordinateSystemZoom;
        CoordinateSystemShowLabel = this->handle->GetBool("CoordinateSystemShowLabel", false);
        funcs["CoordinateSystemShowLabel"] = &PartDesignParamsP::updateCoordinateSystemShowLabel;
        CoordinateSystemSelectOnTop = this->handle->GetBool("CoordinateSystemSelectOnTop", true);
        funcs["CoordinateSystemSelectOnTop"] = &PartDesignParamsP::updateCoordinateSystemSelectOnTop;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~PartDesignParamsP() override = default;

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
    static void updateRefineModel(PartDesignParamsP *self) {
        self->RefineModel = self->handle->GetBool("RefineModel", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatedefaultBaseTypeHole(PartDesignParamsP *self) {
        self->defaultBaseTypeHole = self->handle->GetInt("defaultBaseTypeHole", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNewSketchUseAttachmentDialog(PartDesignParamsP *self) {
        self->NewSketchUseAttachmentDialog = self->handle->GetBool("NewSketchUseAttachmentDialog", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBooleanDeleteOnRemove(PartDesignParamsP *self) {
        self->BooleanDeleteOnRemove = self->handle->GetBool("BooleanDeleteOnRemove", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSwitchToWB(PartDesignParamsP *self) {
        self->SwitchToWB = self->handle->GetBool("SwitchToWB", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSwitchToTask(PartDesignParamsP *self) {
        self->SwitchToTask = self->handle->GetBool("SwitchToTask", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoordinateSystemFontSize(PartDesignParamsP *self) {
        self->CoordinateSystemFontSize = self->handle->GetInt("CoordinateSystemFontSize", 10);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoordinateSystemZoom(PartDesignParamsP *self) {
        self->CoordinateSystemZoom = self->handle->GetFloat("CoordinateSystemZoom", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoordinateSystemShowLabel(PartDesignParamsP *self) {
        self->CoordinateSystemShowLabel = self->handle->GetBool("CoordinateSystemShowLabel", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoordinateSystemSelectOnTop(PartDesignParamsP *self) {
        self->CoordinateSystemSelectOnTop = self->handle->GetBool("CoordinateSystemSelectOnTop", true);
    }
};

// Auto generated code (Tools/params_utils.py:336)
PartDesignParamsP *instance() {
    static PartDesignParamsP *inst = new PartDesignParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _PartDesignParamsRegistrar({
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "RefineModel", "RefineModel", App::ParamInfo::Bool, false)
        .setTitle("Refine model after sketch-based operation")
        .setDoc("New PartDesign features get Refine switched on: faces that lie\n"
"on the same surface are merged after the feature is computed.\n"
"Read when a feature is created."),
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "defaultBaseTypeHole", "defaultBaseTypeHole", App::ParamInfo::Int, 1)
        .setTitle("Default profile type for holes")
        .setDoc("What a new Hole takes from its sketch: 0 circles and arcs, 1\n"
"points, circles and arcs, 2 points. Read when a Hole is created."),
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "NewSketchUseAttachmentDialog", "NewSketchUseAttachmentDialog", App::ParamInfo::Bool, false)
        .setTitle("Always ask how to attach a new sketch")
        .setDoc("Open the attachment panel for every new sketch, also when a\n"
"single planar face or plane is selected, which is otherwise\n"
"sketched on at once."),
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "BooleanDeleteOnRemove", "BooleanDeleteOnRemove", App::ParamInfo::Bool, true)
        .setTitle("Delete a body removed from a Boolean")
        .setDoc("Removing a body from a PartDesign Boolean also deletes the body.\n"
"The check box of the Boolean task panel."),
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "SwitchToWB", "SwitchToWB", App::ParamInfo::Bool, true)
        .setTitle("Switch to PartDesign when a body is edited")
        .setDoc("Activate the PartDesign workbench when a body is double-clicked."),
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "SwitchToTask", "SwitchToTask", App::ParamInfo::Bool, true)
        .setTitle("Show the task panel in PartDesign")
        .setDoc("Bring the task view to the front when the PartDesign workbench is\n"
"activated."),
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "CoordinateSystemFontSize", "CoordinateSystemFontSize", App::ParamInfo::Int, 10)
        .setTitle("Local coordinate system font size")
        .setDoc("Font size of the axis labels of a new local coordinate system."),
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "CoordinateSystemZoom", "CoordinateSystemZoom", App::ParamInfo::Float, 1.0)
        .setTitle("Local coordinate system zoom")
        .setDoc("Size factor of a new local coordinate system."),
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "CoordinateSystemShowLabel", "CoordinateSystemShowLabel", App::ParamInfo::Bool, false)
        .setTitle("Local coordinate system labels")
        .setDoc("Show the axis labels of a new local coordinate system."),
    App::ParamInfo("PartDesign", "PartDesignParams", "User parameter:BaseApp/Preferences/Mod/PartDesign", "CoordinateSystemSelectOnTop", "CoordinateSystemSelectOnTop", App::ParamInfo::Bool, true)
        .setTitle("Local coordinate system on top when selected")
        .setDoc("Draw a new local coordinate system on top of everything else\n"
"while it is selected."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle PartDesignParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docRefineModel() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"New PartDesign features get Refine switched on: faces that lie\n"
"on the same surface are merged after the feature is computed.\n"
"Read when a feature is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartDesignParams::getRefineModel() {
    return instance()->RefineModel;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartDesignParams::defaultRefineModel() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setRefineModel(const bool &v) {
    instance()->handle->SetBool("RefineModel",v);
    instance()->RefineModel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removeRefineModel() {
    instance()->handle->RemoveBool("RefineModel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docdefaultBaseTypeHole() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"What a new Hole takes from its sketch: 0 circles and arcs, 1\n"
"points, circles and arcs, 2 points. Read when a Hole is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartDesignParams::getdefaultBaseTypeHole() {
    return instance()->defaultBaseTypeHole;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartDesignParams::defaultdefaultBaseTypeHole() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setdefaultBaseTypeHole(const long &v) {
    instance()->handle->SetInt("defaultBaseTypeHole",v);
    instance()->defaultBaseTypeHole = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removedefaultBaseTypeHole() {
    instance()->handle->RemoveInt("defaultBaseTypeHole");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docNewSketchUseAttachmentDialog() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"Open the attachment panel for every new sketch, also when a\n"
"single planar face or plane is selected, which is otherwise\n"
"sketched on at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartDesignParams::getNewSketchUseAttachmentDialog() {
    return instance()->NewSketchUseAttachmentDialog;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartDesignParams::defaultNewSketchUseAttachmentDialog() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setNewSketchUseAttachmentDialog(const bool &v) {
    instance()->handle->SetBool("NewSketchUseAttachmentDialog",v);
    instance()->NewSketchUseAttachmentDialog = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removeNewSketchUseAttachmentDialog() {
    instance()->handle->RemoveBool("NewSketchUseAttachmentDialog");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docBooleanDeleteOnRemove() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"Removing a body from a PartDesign Boolean also deletes the body.\n"
"The check box of the Boolean task panel.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartDesignParams::getBooleanDeleteOnRemove() {
    return instance()->BooleanDeleteOnRemove;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartDesignParams::defaultBooleanDeleteOnRemove() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setBooleanDeleteOnRemove(const bool &v) {
    instance()->handle->SetBool("BooleanDeleteOnRemove",v);
    instance()->BooleanDeleteOnRemove = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removeBooleanDeleteOnRemove() {
    instance()->handle->RemoveBool("BooleanDeleteOnRemove");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docSwitchToWB() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"Activate the PartDesign workbench when a body is double-clicked.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartDesignParams::getSwitchToWB() {
    return instance()->SwitchToWB;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartDesignParams::defaultSwitchToWB() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setSwitchToWB(const bool &v) {
    instance()->handle->SetBool("SwitchToWB",v);
    instance()->SwitchToWB = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removeSwitchToWB() {
    instance()->handle->RemoveBool("SwitchToWB");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docSwitchToTask() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"Bring the task view to the front when the PartDesign workbench is\n"
"activated.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartDesignParams::getSwitchToTask() {
    return instance()->SwitchToTask;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartDesignParams::defaultSwitchToTask() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setSwitchToTask(const bool &v) {
    instance()->handle->SetBool("SwitchToTask",v);
    instance()->SwitchToTask = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removeSwitchToTask() {
    instance()->handle->RemoveBool("SwitchToTask");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docCoordinateSystemFontSize() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"Font size of the axis labels of a new local coordinate system.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartDesignParams::getCoordinateSystemFontSize() {
    return instance()->CoordinateSystemFontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartDesignParams::defaultCoordinateSystemFontSize() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setCoordinateSystemFontSize(const long &v) {
    instance()->handle->SetInt("CoordinateSystemFontSize",v);
    instance()->CoordinateSystemFontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removeCoordinateSystemFontSize() {
    instance()->handle->RemoveInt("CoordinateSystemFontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docCoordinateSystemZoom() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"Size factor of a new local coordinate system.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & PartDesignParams::getCoordinateSystemZoom() {
    return instance()->CoordinateSystemZoom;
}

// Auto generated code (Tools/params_utils.py:413)
const double & PartDesignParams::defaultCoordinateSystemZoom() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setCoordinateSystemZoom(const double &v) {
    instance()->handle->SetFloat("CoordinateSystemZoom",v);
    instance()->CoordinateSystemZoom = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removeCoordinateSystemZoom() {
    instance()->handle->RemoveFloat("CoordinateSystemZoom");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docCoordinateSystemShowLabel() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"Show the axis labels of a new local coordinate system.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartDesignParams::getCoordinateSystemShowLabel() {
    return instance()->CoordinateSystemShowLabel;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartDesignParams::defaultCoordinateSystemShowLabel() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setCoordinateSystemShowLabel(const bool &v) {
    instance()->handle->SetBool("CoordinateSystemShowLabel",v);
    instance()->CoordinateSystemShowLabel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removeCoordinateSystemShowLabel() {
    instance()->handle->RemoveBool("CoordinateSystemShowLabel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartDesignParams::docCoordinateSystemSelectOnTop() {
    return QT_TRANSLATE_NOOP("PartDesignParams",
"Draw a new local coordinate system on top of everything else\n"
"while it is selected.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartDesignParams::getCoordinateSystemSelectOnTop() {
    return instance()->CoordinateSystemSelectOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartDesignParams::defaultCoordinateSystemSelectOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartDesignParams::setCoordinateSystemSelectOnTop(const bool &v) {
    instance()->handle->SetBool("CoordinateSystemSelectOnTop",v);
    instance()->CoordinateSystemSelectOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartDesignParams::removeCoordinateSystemSelectOnTop() {
    instance()->handle->RemoveBool("CoordinateSystemSelectOnTop");
}
//[[[end]]]
