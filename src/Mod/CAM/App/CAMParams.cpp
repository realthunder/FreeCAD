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
import CAMParams
CAMParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "CAMParams.h"
using namespace Path;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class CAMParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(CAMParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    unsigned long DefaultNormalPathColor;
    unsigned long DefaultRapidPathColor;
    unsigned long DefaultProbePathColor;
    unsigned long DefaultPathMarkerColor;
    long DefaultPathLineWidth;
    double DefaultArrowScale;
    long DefaultSelectionStyle;
    unsigned long DefaultBBoxNormalColor;
    unsigned long DefaultBBoxSelectionColor;
    bool HideFirstRapid;
    bool WarningSuppressAllSpeeds;
    bool SimulatorShowInDocumentView;
    bool ForceLegacyGLRender;

    // Auto generated code (Tools/params_utils.py:254)
    CAMParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/CAM");
        handle->Attach(this);

        DefaultNormalPathColor = this->handle->GetUnsigned("DefaultNormalPathColor", 0x00AA00FF);
        funcs["DefaultNormalPathColor"] = &CAMParamsP::updateDefaultNormalPathColor;
        DefaultRapidPathColor = this->handle->GetUnsigned("DefaultRapidPathColor", 0xAA0000FF);
        funcs["DefaultRapidPathColor"] = &CAMParamsP::updateDefaultRapidPathColor;
        DefaultProbePathColor = this->handle->GetUnsigned("DefaultProbePathColor", 0xFFEB00FF);
        funcs["DefaultProbePathColor"] = &CAMParamsP::updateDefaultProbePathColor;
        DefaultPathMarkerColor = this->handle->GetUnsigned("DefaultPathMarkerColor", 0x55FF00FF);
        funcs["DefaultPathMarkerColor"] = &CAMParamsP::updateDefaultPathMarkerColor;
        DefaultPathLineWidth = this->handle->GetInt("DefaultPathLineWidth", 1);
        funcs["DefaultPathLineWidth"] = &CAMParamsP::updateDefaultPathLineWidth;
        DefaultArrowScale = this->handle->GetFloat("DefaultArrowScale", 3.0);
        funcs["DefaultArrowScale"] = &CAMParamsP::updateDefaultArrowScale;
        DefaultSelectionStyle = this->handle->GetInt("DefaultSelectionStyle", 0);
        funcs["DefaultSelectionStyle"] = &CAMParamsP::updateDefaultSelectionStyle;
        DefaultBBoxNormalColor = this->handle->GetUnsigned("DefaultBBoxNormalColor", 0xFFFFFFFF);
        funcs["DefaultBBoxNormalColor"] = &CAMParamsP::updateDefaultBBoxNormalColor;
        DefaultBBoxSelectionColor = this->handle->GetUnsigned("DefaultBBoxSelectionColor", 0xC8FFFFFF);
        funcs["DefaultBBoxSelectionColor"] = &CAMParamsP::updateDefaultBBoxSelectionColor;
        HideFirstRapid = this->handle->GetBool("HideFirstRapid", false);
        funcs["HideFirstRapid"] = &CAMParamsP::updateHideFirstRapid;
        WarningSuppressAllSpeeds = this->handle->GetBool("WarningSuppressAllSpeeds", true);
        funcs["WarningSuppressAllSpeeds"] = &CAMParamsP::updateWarningSuppressAllSpeeds;
        SimulatorShowInDocumentView = this->handle->GetBool("SimulatorShowInDocumentView", true);
        funcs["SimulatorShowInDocumentView"] = &CAMParamsP::updateSimulatorShowInDocumentView;
        ForceLegacyGLRender = this->handle->GetBool("ForceLegacyGLRender", false);
        funcs["ForceLegacyGLRender"] = &CAMParamsP::updateForceLegacyGLRender;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~CAMParamsP() override = default;

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
    static void updateDefaultNormalPathColor(CAMParamsP *self) {
        self->DefaultNormalPathColor = self->handle->GetUnsigned("DefaultNormalPathColor", 0x00AA00FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultRapidPathColor(CAMParamsP *self) {
        self->DefaultRapidPathColor = self->handle->GetUnsigned("DefaultRapidPathColor", 0xAA0000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultProbePathColor(CAMParamsP *self) {
        self->DefaultProbePathColor = self->handle->GetUnsigned("DefaultProbePathColor", 0xFFEB00FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultPathMarkerColor(CAMParamsP *self) {
        self->DefaultPathMarkerColor = self->handle->GetUnsigned("DefaultPathMarkerColor", 0x55FF00FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultPathLineWidth(CAMParamsP *self) {
        self->DefaultPathLineWidth = self->handle->GetInt("DefaultPathLineWidth", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultArrowScale(CAMParamsP *self) {
        self->DefaultArrowScale = self->handle->GetFloat("DefaultArrowScale", 3.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultSelectionStyle(CAMParamsP *self) {
        self->DefaultSelectionStyle = self->handle->GetInt("DefaultSelectionStyle", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultBBoxNormalColor(CAMParamsP *self) {
        self->DefaultBBoxNormalColor = self->handle->GetUnsigned("DefaultBBoxNormalColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultBBoxSelectionColor(CAMParamsP *self) {
        self->DefaultBBoxSelectionColor = self->handle->GetUnsigned("DefaultBBoxSelectionColor", 0xC8FFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHideFirstRapid(CAMParamsP *self) {
        self->HideFirstRapid = self->handle->GetBool("HideFirstRapid", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWarningSuppressAllSpeeds(CAMParamsP *self) {
        self->WarningSuppressAllSpeeds = self->handle->GetBool("WarningSuppressAllSpeeds", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSimulatorShowInDocumentView(CAMParamsP *self) {
        self->SimulatorShowInDocumentView = self->handle->GetBool("SimulatorShowInDocumentView", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateForceLegacyGLRender(CAMParamsP *self) {
        self->ForceLegacyGLRender = self->handle->GetBool("ForceLegacyGLRender", false);
    }
};

// Auto generated code (Tools/params_utils.py:336)
CAMParamsP *instance() {
    static CAMParamsP *inst = new CAMParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _CAMParamsRegistrar({
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "DefaultNormalPathColor", "DefaultNormalPathColor", App::ParamInfo::Hex, 0x00AA00FF)
        .setTitle("Feed move colour")
        .setDoc("Colour of the feed moves of a toolpath. Path objects take it when\n"
"they are created; the CAM simulator follows a change at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "DefaultRapidPathColor", "DefaultRapidPathColor", App::ParamInfo::Hex, 0xAA0000FF)
        .setTitle("Rapid move colour")
        .setDoc("Colour of the rapid moves of a toolpath. Paths in the 3D view pick\n"
"it up the next time their colours are rebuilt; the CAM simulator\n"
"follows a change at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "DefaultProbePathColor", "DefaultProbePathColor", App::ParamInfo::Hex, 0xFFEB00FF)
        .setTitle("Probe move colour")
        .setDoc("Colour of the probe moves of a toolpath. Paths in the 3D view pick\n"
"it up the next time their colours are rebuilt.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "DefaultPathMarkerColor", "DefaultPathMarkerColor", App::ParamInfo::Hex, 0x55FF00FF)
        .setTitle("Path marker colour")
        .setDoc("Colour of the node markers of a toolpath. Applies to path objects\n"
"created afterwards.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "DefaultPathLineWidth", "DefaultPathLineWidth", App::ParamInfo::Int, 1)
        .setTitle("Path line width")
        .setDoc("Line width in pixels of a toolpath in the 3D view. Applies to path\n"
"objects created afterwards."),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "DefaultArrowScale", "DefaultArrowScale", App::ParamInfo::Float, 3.0)
        .setTitle("Path arrow size")
        .setDoc("Size factor of the direction arrow drawn on a toolpath. Applies to\n"
"path objects created afterwards."),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "DefaultSelectionStyle", "DefaultSelectionStyle", App::ParamInfo::Int, 0)
        .setTitle("Path selection style")
        .setDoc("How a toolpath is selected in the 3D view: 0 by its shape, 1 by\n"
"its bounding box, 2 not at all. Applies to path objects created\n"
"afterwards."),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "DefaultBBoxNormalColor", "DefaultBBoxNormalColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Bounding box colour")
        .setDoc("Colour of the bounding box of a toolpath that is selected by shape\n"
"or cannot be selected. Takes effect the next time the bounding box\n"
"is drawn.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "DefaultBBoxSelectionColor", "DefaultBBoxSelectionColor", App::ParamInfo::Hex, 0xC8FFFFFF)
        .setTitle("Bounding box selection colour")
        .setDoc("Colour of the bounding box of a toolpath that is selected by its\n"
"bounding box. Takes effect the next time the bounding box is\n"
"drawn.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "HideFirstRapid", "HideFirstRapid", App::ParamInfo::Bool, false)
        .setTitle("Hide first rapid move")
        .setDoc("Hides the initial rapid move of a toolpath by starting the display\n"
"at the first feed move. Takes effect the next time the path\n"
"changes."),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "WarningSuppressAllSpeeds", "WarningSuppressAllSpeeds", App::ParamInfo::Bool, true)
        .setTitle("Suppress missing speeds warning")
        .setDoc("Suppresses the warning that a tool controller has no feed rate\n"
"when the cycle time of a toolpath is estimated. Takes effect at\n"
"the next estimate."),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "SimulatorShowInDocumentView", "SimulatorShowInDocumentView", App::ParamInfo::Bool, true)
        .setTitle("Simulate in document view")
        .setDoc("Shows the simulated stock in the 3D view of the document while the\n"
"CAM simulator runs. The button in the simulator follows a change\n"
"at once; the view is attached at the next simulation."),
    App::ParamInfo("Path", "CAMParams", "User parameter:BaseApp/Preferences/Mod/CAM", "ForceLegacyGLRender", "ForceLegacyGLRender", App::ParamInfo::Bool, false)
        .setTitle("Force legacy simulator rendering")
        .setDoc("Makes the CAM simulator draw with its legacy OpenGL renderer\n"
"instead of the render backend. Takes effect the next time the\n"
"simulator is opened."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle CAMParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docDefaultNormalPathColor() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Colour of the feed moves of a toolpath. Path objects take it when\n"
"they are created; the CAM simulator follows a change at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & CAMParams::getDefaultNormalPathColor() {
    return instance()->DefaultNormalPathColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & CAMParams::defaultDefaultNormalPathColor() {
    const static unsigned long def = 0x00AA00FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setDefaultNormalPathColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultNormalPathColor",v);
    instance()->DefaultNormalPathColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeDefaultNormalPathColor() {
    instance()->handle->RemoveUnsigned("DefaultNormalPathColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docDefaultRapidPathColor() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Colour of the rapid moves of a toolpath. Paths in the 3D view pick\n"
"it up the next time their colours are rebuilt; the CAM simulator\n"
"follows a change at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & CAMParams::getDefaultRapidPathColor() {
    return instance()->DefaultRapidPathColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & CAMParams::defaultDefaultRapidPathColor() {
    const static unsigned long def = 0xAA0000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setDefaultRapidPathColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultRapidPathColor",v);
    instance()->DefaultRapidPathColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeDefaultRapidPathColor() {
    instance()->handle->RemoveUnsigned("DefaultRapidPathColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docDefaultProbePathColor() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Colour of the probe moves of a toolpath. Paths in the 3D view pick\n"
"it up the next time their colours are rebuilt.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & CAMParams::getDefaultProbePathColor() {
    return instance()->DefaultProbePathColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & CAMParams::defaultDefaultProbePathColor() {
    const static unsigned long def = 0xFFEB00FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setDefaultProbePathColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultProbePathColor",v);
    instance()->DefaultProbePathColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeDefaultProbePathColor() {
    instance()->handle->RemoveUnsigned("DefaultProbePathColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docDefaultPathMarkerColor() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Colour of the node markers of a toolpath. Applies to path objects\n"
"created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & CAMParams::getDefaultPathMarkerColor() {
    return instance()->DefaultPathMarkerColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & CAMParams::defaultDefaultPathMarkerColor() {
    const static unsigned long def = 0x55FF00FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setDefaultPathMarkerColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultPathMarkerColor",v);
    instance()->DefaultPathMarkerColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeDefaultPathMarkerColor() {
    instance()->handle->RemoveUnsigned("DefaultPathMarkerColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docDefaultPathLineWidth() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Line width in pixels of a toolpath in the 3D view. Applies to path\n"
"objects created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & CAMParams::getDefaultPathLineWidth() {
    return instance()->DefaultPathLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & CAMParams::defaultDefaultPathLineWidth() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setDefaultPathLineWidth(const long &v) {
    instance()->handle->SetInt("DefaultPathLineWidth",v);
    instance()->DefaultPathLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeDefaultPathLineWidth() {
    instance()->handle->RemoveInt("DefaultPathLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docDefaultArrowScale() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Size factor of the direction arrow drawn on a toolpath. Applies to\n"
"path objects created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & CAMParams::getDefaultArrowScale() {
    return instance()->DefaultArrowScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & CAMParams::defaultDefaultArrowScale() {
    const static double def = 3.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setDefaultArrowScale(const double &v) {
    instance()->handle->SetFloat("DefaultArrowScale",v);
    instance()->DefaultArrowScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeDefaultArrowScale() {
    instance()->handle->RemoveFloat("DefaultArrowScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docDefaultSelectionStyle() {
    return QT_TRANSLATE_NOOP("CAMParams",
"How a toolpath is selected in the 3D view: 0 by its shape, 1 by\n"
"its bounding box, 2 not at all. Applies to path objects created\n"
"afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & CAMParams::getDefaultSelectionStyle() {
    return instance()->DefaultSelectionStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & CAMParams::defaultDefaultSelectionStyle() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setDefaultSelectionStyle(const long &v) {
    instance()->handle->SetInt("DefaultSelectionStyle",v);
    instance()->DefaultSelectionStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeDefaultSelectionStyle() {
    instance()->handle->RemoveInt("DefaultSelectionStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docDefaultBBoxNormalColor() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Colour of the bounding box of a toolpath that is selected by shape\n"
"or cannot be selected. Takes effect the next time the bounding box\n"
"is drawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & CAMParams::getDefaultBBoxNormalColor() {
    return instance()->DefaultBBoxNormalColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & CAMParams::defaultDefaultBBoxNormalColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setDefaultBBoxNormalColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultBBoxNormalColor",v);
    instance()->DefaultBBoxNormalColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeDefaultBBoxNormalColor() {
    instance()->handle->RemoveUnsigned("DefaultBBoxNormalColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docDefaultBBoxSelectionColor() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Colour of the bounding box of a toolpath that is selected by its\n"
"bounding box. Takes effect the next time the bounding box is\n"
"drawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & CAMParams::getDefaultBBoxSelectionColor() {
    return instance()->DefaultBBoxSelectionColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & CAMParams::defaultDefaultBBoxSelectionColor() {
    const static unsigned long def = 0xC8FFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setDefaultBBoxSelectionColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultBBoxSelectionColor",v);
    instance()->DefaultBBoxSelectionColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeDefaultBBoxSelectionColor() {
    instance()->handle->RemoveUnsigned("DefaultBBoxSelectionColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docHideFirstRapid() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Hides the initial rapid move of a toolpath by starting the display\n"
"at the first feed move. Takes effect the next time the path\n"
"changes.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & CAMParams::getHideFirstRapid() {
    return instance()->HideFirstRapid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & CAMParams::defaultHideFirstRapid() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setHideFirstRapid(const bool &v) {
    instance()->handle->SetBool("HideFirstRapid",v);
    instance()->HideFirstRapid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeHideFirstRapid() {
    instance()->handle->RemoveBool("HideFirstRapid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docWarningSuppressAllSpeeds() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Suppresses the warning that a tool controller has no feed rate\n"
"when the cycle time of a toolpath is estimated. Takes effect at\n"
"the next estimate.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & CAMParams::getWarningSuppressAllSpeeds() {
    return instance()->WarningSuppressAllSpeeds;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & CAMParams::defaultWarningSuppressAllSpeeds() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setWarningSuppressAllSpeeds(const bool &v) {
    instance()->handle->SetBool("WarningSuppressAllSpeeds",v);
    instance()->WarningSuppressAllSpeeds = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeWarningSuppressAllSpeeds() {
    instance()->handle->RemoveBool("WarningSuppressAllSpeeds");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docSimulatorShowInDocumentView() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Shows the simulated stock in the 3D view of the document while the\n"
"CAM simulator runs. The button in the simulator follows a change\n"
"at once; the view is attached at the next simulation.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & CAMParams::getSimulatorShowInDocumentView() {
    return instance()->SimulatorShowInDocumentView;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & CAMParams::defaultSimulatorShowInDocumentView() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setSimulatorShowInDocumentView(const bool &v) {
    instance()->handle->SetBool("SimulatorShowInDocumentView",v);
    instance()->SimulatorShowInDocumentView = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeSimulatorShowInDocumentView() {
    instance()->handle->RemoveBool("SimulatorShowInDocumentView");
}

// Auto generated code (Tools/params_utils.py:397)
const char *CAMParams::docForceLegacyGLRender() {
    return QT_TRANSLATE_NOOP("CAMParams",
"Makes the CAM simulator draw with its legacy OpenGL renderer\n"
"instead of the render backend. Takes effect the next time the\n"
"simulator is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & CAMParams::getForceLegacyGLRender() {
    return instance()->ForceLegacyGLRender;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & CAMParams::defaultForceLegacyGLRender() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void CAMParams::setForceLegacyGLRender(const bool &v) {
    instance()->handle->SetBool("ForceLegacyGLRender",v);
    instance()->ForceLegacyGLRender = v;
}

// Auto generated code (Tools/params_utils.py:431)
void CAMParams::removeForceLegacyGLRender() {
    instance()->handle->RemoveBool("ForceLegacyGLRender");
}
//[[[end]]]
