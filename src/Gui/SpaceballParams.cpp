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
import SpaceballParams
SpaceballParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "SpaceballParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class SpaceballParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(SpaceballParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    long GlobalSensitivity;
    bool Dominant;
    bool FlipYZ;
    bool Translations;
    bool Rotations;
    bool PanLREnable;
    bool PanLRReverse;
    long PanLRSensitivity;
    bool PanUDEnable;
    bool PanUDReverse;
    long PanUDSensitivity;
    bool ZoomEnable;
    bool ZoomReverse;
    long ZoomSensitivity;
    bool TiltEnable;
    bool TiltReverse;
    long TiltSensitivity;
    bool RollEnable;
    bool RollReverse;
    long RollSensitivity;
    bool SpinEnable;
    bool SpinReverse;
    long SpinSensitivity;
    long Remapping;
    bool Calibrate;
    long CalibrationX;
    long CalibrationY;
    long CalibrationZ;
    long CalibrationXr;
    long CalibrationYr;
    long CalibrationZr;
    std::string Model;

    // Auto generated code (Tools/params_utils.py:254)
    SpaceballParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Spaceball/Motion");
        handle->Attach(this);

        subHandles.resize(1);
        subHandles[0] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Spaceball");
        subHandles[0]->Attach(this);
        GlobalSensitivity = this->handle->GetInt("GlobalSensitivity", 0);
        funcs["GlobalSensitivity"] = &SpaceballParamsP::updateGlobalSensitivity;
        Dominant = this->handle->GetBool("Dominant", false);
        funcs["Dominant"] = &SpaceballParamsP::updateDominant;
        FlipYZ = this->handle->GetBool("FlipYZ", false);
        funcs["FlipYZ"] = &SpaceballParamsP::updateFlipYZ;
        Translations = this->handle->GetBool("Translations", true);
        funcs["Translations"] = &SpaceballParamsP::updateTranslations;
        Rotations = this->handle->GetBool("Rotations", true);
        funcs["Rotations"] = &SpaceballParamsP::updateRotations;
        PanLREnable = this->handle->GetBool("PanLREnable", true);
        funcs["PanLREnable"] = &SpaceballParamsP::updatePanLREnable;
        PanLRReverse = this->handle->GetBool("PanLRReverse", false);
        funcs["PanLRReverse"] = &SpaceballParamsP::updatePanLRReverse;
        PanLRSensitivity = this->handle->GetInt("PanLRSensitivity", 0);
        funcs["PanLRSensitivity"] = &SpaceballParamsP::updatePanLRSensitivity;
        PanUDEnable = this->handle->GetBool("PanUDEnable", true);
        funcs["PanUDEnable"] = &SpaceballParamsP::updatePanUDEnable;
        PanUDReverse = this->handle->GetBool("PanUDReverse", false);
        funcs["PanUDReverse"] = &SpaceballParamsP::updatePanUDReverse;
        PanUDSensitivity = this->handle->GetInt("PanUDSensitivity", 0);
        funcs["PanUDSensitivity"] = &SpaceballParamsP::updatePanUDSensitivity;
        ZoomEnable = this->handle->GetBool("ZoomEnable", true);
        funcs["ZoomEnable"] = &SpaceballParamsP::updateZoomEnable;
        ZoomReverse = this->handle->GetBool("ZoomReverse", false);
        funcs["ZoomReverse"] = &SpaceballParamsP::updateZoomReverse;
        ZoomSensitivity = this->handle->GetInt("ZoomSensitivity", 0);
        funcs["ZoomSensitivity"] = &SpaceballParamsP::updateZoomSensitivity;
        TiltEnable = this->handle->GetBool("TiltEnable", true);
        funcs["TiltEnable"] = &SpaceballParamsP::updateTiltEnable;
        TiltReverse = this->handle->GetBool("TiltReverse", false);
        funcs["TiltReverse"] = &SpaceballParamsP::updateTiltReverse;
        TiltSensitivity = this->handle->GetInt("TiltSensitivity", 0);
        funcs["TiltSensitivity"] = &SpaceballParamsP::updateTiltSensitivity;
        RollEnable = this->handle->GetBool("RollEnable", true);
        funcs["RollEnable"] = &SpaceballParamsP::updateRollEnable;
        RollReverse = this->handle->GetBool("RollReverse", false);
        funcs["RollReverse"] = &SpaceballParamsP::updateRollReverse;
        RollSensitivity = this->handle->GetInt("RollSensitivity", 0);
        funcs["RollSensitivity"] = &SpaceballParamsP::updateRollSensitivity;
        SpinEnable = this->handle->GetBool("SpinEnable", true);
        funcs["SpinEnable"] = &SpaceballParamsP::updateSpinEnable;
        SpinReverse = this->handle->GetBool("SpinReverse", false);
        funcs["SpinReverse"] = &SpaceballParamsP::updateSpinReverse;
        SpinSensitivity = this->handle->GetInt("SpinSensitivity", 0);
        funcs["SpinSensitivity"] = &SpaceballParamsP::updateSpinSensitivity;
        Remapping = this->handle->GetInt("Remapping", 12345);
        funcs["Remapping"] = &SpaceballParamsP::updateRemapping;
        Calibrate = this->handle->GetBool("Calibrate", false);
        funcs["Calibrate"] = &SpaceballParamsP::updateCalibrate;
        CalibrationX = this->handle->GetInt("CalibrationX", 0);
        funcs["CalibrationX"] = &SpaceballParamsP::updateCalibrationX;
        CalibrationY = this->handle->GetInt("CalibrationY", 0);
        funcs["CalibrationY"] = &SpaceballParamsP::updateCalibrationY;
        CalibrationZ = this->handle->GetInt("CalibrationZ", 0);
        funcs["CalibrationZ"] = &SpaceballParamsP::updateCalibrationZ;
        CalibrationXr = this->handle->GetInt("CalibrationXr", 0);
        funcs["CalibrationXr"] = &SpaceballParamsP::updateCalibrationXr;
        CalibrationYr = this->handle->GetInt("CalibrationYr", 0);
        funcs["CalibrationYr"] = &SpaceballParamsP::updateCalibrationYr;
        CalibrationZr = this->handle->GetInt("CalibrationZr", 0);
        funcs["CalibrationZr"] = &SpaceballParamsP::updateCalibrationZr;
        Model = this->subHandles[0]->GetASCII("Model", "");
        funcs["Model"] = &SpaceballParamsP::updateModel;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~SpaceballParamsP() override = default;

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
    static void updateGlobalSensitivity(SpaceballParamsP *self) {
        self->GlobalSensitivity = self->handle->GetInt("GlobalSensitivity", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDominant(SpaceballParamsP *self) {
        self->Dominant = self->handle->GetBool("Dominant", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFlipYZ(SpaceballParamsP *self) {
        self->FlipYZ = self->handle->GetBool("FlipYZ", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTranslations(SpaceballParamsP *self) {
        self->Translations = self->handle->GetBool("Translations", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRotations(SpaceballParamsP *self) {
        self->Rotations = self->handle->GetBool("Rotations", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePanLREnable(SpaceballParamsP *self) {
        self->PanLREnable = self->handle->GetBool("PanLREnable", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePanLRReverse(SpaceballParamsP *self) {
        self->PanLRReverse = self->handle->GetBool("PanLRReverse", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePanLRSensitivity(SpaceballParamsP *self) {
        self->PanLRSensitivity = self->handle->GetInt("PanLRSensitivity", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePanUDEnable(SpaceballParamsP *self) {
        self->PanUDEnable = self->handle->GetBool("PanUDEnable", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePanUDReverse(SpaceballParamsP *self) {
        self->PanUDReverse = self->handle->GetBool("PanUDReverse", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePanUDSensitivity(SpaceballParamsP *self) {
        self->PanUDSensitivity = self->handle->GetInt("PanUDSensitivity", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateZoomEnable(SpaceballParamsP *self) {
        self->ZoomEnable = self->handle->GetBool("ZoomEnable", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateZoomReverse(SpaceballParamsP *self) {
        self->ZoomReverse = self->handle->GetBool("ZoomReverse", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateZoomSensitivity(SpaceballParamsP *self) {
        self->ZoomSensitivity = self->handle->GetInt("ZoomSensitivity", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTiltEnable(SpaceballParamsP *self) {
        self->TiltEnable = self->handle->GetBool("TiltEnable", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTiltReverse(SpaceballParamsP *self) {
        self->TiltReverse = self->handle->GetBool("TiltReverse", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTiltSensitivity(SpaceballParamsP *self) {
        self->TiltSensitivity = self->handle->GetInt("TiltSensitivity", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRollEnable(SpaceballParamsP *self) {
        self->RollEnable = self->handle->GetBool("RollEnable", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRollReverse(SpaceballParamsP *self) {
        self->RollReverse = self->handle->GetBool("RollReverse", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRollSensitivity(SpaceballParamsP *self) {
        self->RollSensitivity = self->handle->GetInt("RollSensitivity", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSpinEnable(SpaceballParamsP *self) {
        self->SpinEnable = self->handle->GetBool("SpinEnable", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSpinReverse(SpaceballParamsP *self) {
        self->SpinReverse = self->handle->GetBool("SpinReverse", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSpinSensitivity(SpaceballParamsP *self) {
        self->SpinSensitivity = self->handle->GetInt("SpinSensitivity", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRemapping(SpaceballParamsP *self) {
        self->Remapping = self->handle->GetInt("Remapping", 12345);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCalibrate(SpaceballParamsP *self) {
        self->Calibrate = self->handle->GetBool("Calibrate", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCalibrationX(SpaceballParamsP *self) {
        self->CalibrationX = self->handle->GetInt("CalibrationX", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCalibrationY(SpaceballParamsP *self) {
        self->CalibrationY = self->handle->GetInt("CalibrationY", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCalibrationZ(SpaceballParamsP *self) {
        self->CalibrationZ = self->handle->GetInt("CalibrationZ", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCalibrationXr(SpaceballParamsP *self) {
        self->CalibrationXr = self->handle->GetInt("CalibrationXr", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCalibrationYr(SpaceballParamsP *self) {
        self->CalibrationYr = self->handle->GetInt("CalibrationYr", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCalibrationZr(SpaceballParamsP *self) {
        self->CalibrationZr = self->handle->GetInt("CalibrationZr", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateModel(SpaceballParamsP *self) {
        self->Model = self->subHandles[0]->GetASCII("Model", "");
    }
};

// Auto generated code (Tools/params_utils.py:336)
SpaceballParamsP *instance() {
    static SpaceballParamsP *inst = new SpaceballParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _SpaceballParamsRegistrar({
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "GlobalSensitivity", "GlobalSensitivity", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: global sensitivity")
        .setDoc("How fast the view follows the 3D mouse, for all six movements at\n"
"once: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast.")
        .setProxy("SpinBox")
        .setRange(-50, 50, 1, 0),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "Dominant", "Dominant", App::ParamInfo::Bool, false)
        .setTitle("3D mouse: dominant mode")
        .setDoc("Of the six movements of the 3D mouse only the strongest one is\n"
"followed at a time, so that a push does not also turn the view."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "FlipYZ", "FlipYZ", App::ParamInfo::Bool, false)
        .setTitle("3D mouse: flip Y/Z")
        .setDoc("Swaps the Y and Z axes of the 3D mouse: pushing the cap forward\n"
"zooms where it panned up and down, and the same for the two\n"
"rotations about them."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "Translations", "Translations", App::ParamInfo::Bool, true)
        .setTitle("3D mouse: enable translations")
        .setDoc("The view follows the three pushes of the 3D mouse -- pan left and\n"
"right, pan up and down, zoom. Off, all three are ignored whatever\n"
"each of them is set to."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "Rotations", "Rotations", App::ParamInfo::Bool, true)
        .setTitle("3D mouse: enable rotations")
        .setDoc("The view follows the three turns of the 3D mouse -- tilt, roll,\n"
"spin. Off, all three are ignored whatever each of them is set to."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "PanLREnable", "PanLREnable", App::ParamInfo::Bool, true)
        .setTitle("3D mouse: enable pan left/right")
        .setDoc("The movement of the 3D mouse that pans the view left and right is followed.\n"
"It also needs 'Enable translations'."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "PanLRReverse", "PanLRReverse", App::ParamInfo::Bool, false)
        .setTitle("3D mouse: reverse pan left/right")
        .setDoc("Turns round the direction of the movement of the 3D mouse that\n"
"pans the view left and right."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "PanLRSensitivity", "PanLRSensitivity", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: pan left/right sensitivity")
        .setDoc("How fast the movement of the 3D mouse that pans the view left and right\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.")
        .setProxy("SpinBox")
        .setRange(-50, 50, 1, 0),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "PanUDEnable", "PanUDEnable", App::ParamInfo::Bool, true)
        .setTitle("3D mouse: enable pan up/down")
        .setDoc("The movement of the 3D mouse that pans the view up and down is followed.\n"
"It also needs 'Enable translations'."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "PanUDReverse", "PanUDReverse", App::ParamInfo::Bool, false)
        .setTitle("3D mouse: reverse pan up/down")
        .setDoc("Turns round the direction of the movement of the 3D mouse that\n"
"pans the view up and down."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "PanUDSensitivity", "PanUDSensitivity", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: pan up/down sensitivity")
        .setDoc("How fast the movement of the 3D mouse that pans the view up and down\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.")
        .setProxy("SpinBox")
        .setRange(-50, 50, 1, 0),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "ZoomEnable", "ZoomEnable", App::ParamInfo::Bool, true)
        .setTitle("3D mouse: enable zoom")
        .setDoc("The movement of the 3D mouse that zooms the view is followed.\n"
"It also needs 'Enable translations'."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "ZoomReverse", "ZoomReverse", App::ParamInfo::Bool, false)
        .setTitle("3D mouse: reverse zoom")
        .setDoc("Turns round the direction of the movement of the 3D mouse that\n"
"zooms the view."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "ZoomSensitivity", "ZoomSensitivity", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: zoom sensitivity")
        .setDoc("How fast the movement of the 3D mouse that zooms the view\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.")
        .setProxy("SpinBox")
        .setRange(-50, 50, 1, 0),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "TiltEnable", "TiltEnable", App::ParamInfo::Bool, true)
        .setTitle("3D mouse: enable tilt")
        .setDoc("The movement of the 3D mouse that tilts the view about its horizontal axis is followed.\n"
"It also needs 'Enable rotations'."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "TiltReverse", "TiltReverse", App::ParamInfo::Bool, false)
        .setTitle("3D mouse: reverse tilt")
        .setDoc("Turns round the direction of the movement of the 3D mouse that\n"
"tilts the view about its horizontal axis."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "TiltSensitivity", "TiltSensitivity", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: tilt sensitivity")
        .setDoc("How fast the movement of the 3D mouse that tilts the view about its horizontal axis\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.")
        .setProxy("SpinBox")
        .setRange(-50, 50, 1, 0),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "RollEnable", "RollEnable", App::ParamInfo::Bool, true)
        .setTitle("3D mouse: enable roll")
        .setDoc("The movement of the 3D mouse that rolls the view about the axis into the screen is followed.\n"
"It also needs 'Enable rotations'."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "RollReverse", "RollReverse", App::ParamInfo::Bool, false)
        .setTitle("3D mouse: reverse roll")
        .setDoc("Turns round the direction of the movement of the 3D mouse that\n"
"rolls the view about the axis into the screen."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "RollSensitivity", "RollSensitivity", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: roll sensitivity")
        .setDoc("How fast the movement of the 3D mouse that rolls the view about the axis into the screen\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.")
        .setProxy("SpinBox")
        .setRange(-50, 50, 1, 0),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "SpinEnable", "SpinEnable", App::ParamInfo::Bool, true)
        .setTitle("3D mouse: enable spin")
        .setDoc("The movement of the 3D mouse that spins the view about its vertical axis is followed.\n"
"It also needs 'Enable rotations'."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "SpinReverse", "SpinReverse", App::ParamInfo::Bool, false)
        .setTitle("3D mouse: reverse spin")
        .setDoc("Turns round the direction of the movement of the 3D mouse that\n"
"spins the view about its vertical axis."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "SpinSensitivity", "SpinSensitivity", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: spin sensitivity")
        .setDoc("How fast the movement of the 3D mouse that spins the view about its vertical axis\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.")
        .setProxy("SpinBox")
        .setRange(-50, 50, 1, 0),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "Remapping", "Remapping", App::ParamInfo::Int, 12345)
        .setTitle("3D mouse: axis remapping")
        .setDoc("Which axis of the device each of the six movements is taken from:\n"
"six digits, each of 0 to 5 once, in the order pan left/right, pan\n"
"up/down, zoom, tilt, roll, spin. 12345 (that is 012345) takes them\n"
"as the device gives them; anything that is no such number is\n"
"ignored. On no page."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "Calibrate", "Calibrate", App::ParamInfo::Bool, false)
        .setTitle("3D mouse: calibrate at the next movement")
        .setDoc("A request more than a setting: the Calibrate button of the\n"
"Spaceball Motion page sets it, and the next event of the device\n"
"stores what the device reports at rest as the calibration and\n"
"takes this away again."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "CalibrationX", "CalibrationX", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: calibration, pan left/right")
        .setDoc("What the device reported for pan left/right while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "CalibrationY", "CalibrationY", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: calibration, pan up/down")
        .setDoc("What the device reported for pan up/down while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "CalibrationZ", "CalibrationZ", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: calibration, zoom")
        .setDoc("What the device reported for zoom while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "CalibrationXr", "CalibrationXr", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: calibration, tilt")
        .setDoc("What the device reported for tilt while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "CalibrationYr", "CalibrationYr", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: calibration, roll")
        .setDoc("What the device reported for roll while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball/Motion", "CalibrationZr", "CalibrationZr", App::ParamInfo::Int, 0)
        .setTitle("3D mouse: calibration, spin")
        .setDoc("What the device reported for spin while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand."),
    App::ParamInfo("Gui", "SpaceballParams", "User parameter:BaseApp/Spaceball", "Model", "Model", App::ParamInfo::String, "")
        .setTitle("3D mouse: device model")
        .setDoc("The device chosen in the Spaceball Buttons page of the Customize\n"
"dialog, whose buttons the page lists. Stored when the page's\n"
"Reset button is pressed, not when the choice changes."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle SpaceballParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docGlobalSensitivity() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"How fast the view follows the 3D mouse, for all six movements at\n"
"once: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getGlobalSensitivity() {
    return instance()->GlobalSensitivity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultGlobalSensitivity() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setGlobalSensitivity(const long &v) {
    instance()->handle->SetInt("GlobalSensitivity",v);
    instance()->GlobalSensitivity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeGlobalSensitivity() {
    instance()->handle->RemoveInt("GlobalSensitivity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docDominant() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"Of the six movements of the 3D mouse only the strongest one is\n"
"followed at a time, so that a push does not also turn the view.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getDominant() {
    return instance()->Dominant;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultDominant() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setDominant(const bool &v) {
    instance()->handle->SetBool("Dominant",v);
    instance()->Dominant = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeDominant() {
    instance()->handle->RemoveBool("Dominant");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docFlipYZ() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"Swaps the Y and Z axes of the 3D mouse: pushing the cap forward\n"
"zooms where it panned up and down, and the same for the two\n"
"rotations about them.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getFlipYZ() {
    return instance()->FlipYZ;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultFlipYZ() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setFlipYZ(const bool &v) {
    instance()->handle->SetBool("FlipYZ",v);
    instance()->FlipYZ = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeFlipYZ() {
    instance()->handle->RemoveBool("FlipYZ");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docTranslations() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"The view follows the three pushes of the 3D mouse -- pan left and\n"
"right, pan up and down, zoom. Off, all three are ignored whatever\n"
"each of them is set to.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getTranslations() {
    return instance()->Translations;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultTranslations() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setTranslations(const bool &v) {
    instance()->handle->SetBool("Translations",v);
    instance()->Translations = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeTranslations() {
    instance()->handle->RemoveBool("Translations");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docRotations() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"The view follows the three turns of the 3D mouse -- tilt, roll,\n"
"spin. Off, all three are ignored whatever each of them is set to.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getRotations() {
    return instance()->Rotations;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultRotations() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setRotations(const bool &v) {
    instance()->handle->SetBool("Rotations",v);
    instance()->Rotations = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeRotations() {
    instance()->handle->RemoveBool("Rotations");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docPanLREnable() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"The movement of the 3D mouse that pans the view left and right is followed.\n"
"It also needs 'Enable translations'.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getPanLREnable() {
    return instance()->PanLREnable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultPanLREnable() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setPanLREnable(const bool &v) {
    instance()->handle->SetBool("PanLREnable",v);
    instance()->PanLREnable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removePanLREnable() {
    instance()->handle->RemoveBool("PanLREnable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docPanLRReverse() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"Turns round the direction of the movement of the 3D mouse that\n"
"pans the view left and right.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getPanLRReverse() {
    return instance()->PanLRReverse;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultPanLRReverse() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setPanLRReverse(const bool &v) {
    instance()->handle->SetBool("PanLRReverse",v);
    instance()->PanLRReverse = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removePanLRReverse() {
    instance()->handle->RemoveBool("PanLRReverse");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docPanLRSensitivity() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"How fast the movement of the 3D mouse that pans the view left and right\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getPanLRSensitivity() {
    return instance()->PanLRSensitivity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultPanLRSensitivity() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setPanLRSensitivity(const long &v) {
    instance()->handle->SetInt("PanLRSensitivity",v);
    instance()->PanLRSensitivity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removePanLRSensitivity() {
    instance()->handle->RemoveInt("PanLRSensitivity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docPanUDEnable() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"The movement of the 3D mouse that pans the view up and down is followed.\n"
"It also needs 'Enable translations'.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getPanUDEnable() {
    return instance()->PanUDEnable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultPanUDEnable() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setPanUDEnable(const bool &v) {
    instance()->handle->SetBool("PanUDEnable",v);
    instance()->PanUDEnable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removePanUDEnable() {
    instance()->handle->RemoveBool("PanUDEnable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docPanUDReverse() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"Turns round the direction of the movement of the 3D mouse that\n"
"pans the view up and down.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getPanUDReverse() {
    return instance()->PanUDReverse;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultPanUDReverse() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setPanUDReverse(const bool &v) {
    instance()->handle->SetBool("PanUDReverse",v);
    instance()->PanUDReverse = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removePanUDReverse() {
    instance()->handle->RemoveBool("PanUDReverse");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docPanUDSensitivity() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"How fast the movement of the 3D mouse that pans the view up and down\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getPanUDSensitivity() {
    return instance()->PanUDSensitivity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultPanUDSensitivity() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setPanUDSensitivity(const long &v) {
    instance()->handle->SetInt("PanUDSensitivity",v);
    instance()->PanUDSensitivity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removePanUDSensitivity() {
    instance()->handle->RemoveInt("PanUDSensitivity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docZoomEnable() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"The movement of the 3D mouse that zooms the view is followed.\n"
"It also needs 'Enable translations'.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getZoomEnable() {
    return instance()->ZoomEnable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultZoomEnable() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setZoomEnable(const bool &v) {
    instance()->handle->SetBool("ZoomEnable",v);
    instance()->ZoomEnable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeZoomEnable() {
    instance()->handle->RemoveBool("ZoomEnable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docZoomReverse() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"Turns round the direction of the movement of the 3D mouse that\n"
"zooms the view.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getZoomReverse() {
    return instance()->ZoomReverse;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultZoomReverse() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setZoomReverse(const bool &v) {
    instance()->handle->SetBool("ZoomReverse",v);
    instance()->ZoomReverse = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeZoomReverse() {
    instance()->handle->RemoveBool("ZoomReverse");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docZoomSensitivity() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"How fast the movement of the 3D mouse that zooms the view\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getZoomSensitivity() {
    return instance()->ZoomSensitivity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultZoomSensitivity() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setZoomSensitivity(const long &v) {
    instance()->handle->SetInt("ZoomSensitivity",v);
    instance()->ZoomSensitivity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeZoomSensitivity() {
    instance()->handle->RemoveInt("ZoomSensitivity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docTiltEnable() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"The movement of the 3D mouse that tilts the view about its horizontal axis is followed.\n"
"It also needs 'Enable rotations'.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getTiltEnable() {
    return instance()->TiltEnable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultTiltEnable() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setTiltEnable(const bool &v) {
    instance()->handle->SetBool("TiltEnable",v);
    instance()->TiltEnable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeTiltEnable() {
    instance()->handle->RemoveBool("TiltEnable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docTiltReverse() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"Turns round the direction of the movement of the 3D mouse that\n"
"tilts the view about its horizontal axis.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getTiltReverse() {
    return instance()->TiltReverse;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultTiltReverse() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setTiltReverse(const bool &v) {
    instance()->handle->SetBool("TiltReverse",v);
    instance()->TiltReverse = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeTiltReverse() {
    instance()->handle->RemoveBool("TiltReverse");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docTiltSensitivity() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"How fast the movement of the 3D mouse that tilts the view about its horizontal axis\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getTiltSensitivity() {
    return instance()->TiltSensitivity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultTiltSensitivity() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setTiltSensitivity(const long &v) {
    instance()->handle->SetInt("TiltSensitivity",v);
    instance()->TiltSensitivity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeTiltSensitivity() {
    instance()->handle->RemoveInt("TiltSensitivity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docRollEnable() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"The movement of the 3D mouse that rolls the view about the axis into the screen is followed.\n"
"It also needs 'Enable rotations'.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getRollEnable() {
    return instance()->RollEnable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultRollEnable() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setRollEnable(const bool &v) {
    instance()->handle->SetBool("RollEnable",v);
    instance()->RollEnable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeRollEnable() {
    instance()->handle->RemoveBool("RollEnable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docRollReverse() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"Turns round the direction of the movement of the 3D mouse that\n"
"rolls the view about the axis into the screen.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getRollReverse() {
    return instance()->RollReverse;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultRollReverse() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setRollReverse(const bool &v) {
    instance()->handle->SetBool("RollReverse",v);
    instance()->RollReverse = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeRollReverse() {
    instance()->handle->RemoveBool("RollReverse");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docRollSensitivity() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"How fast the movement of the 3D mouse that rolls the view about the axis into the screen\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getRollSensitivity() {
    return instance()->RollSensitivity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultRollSensitivity() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setRollSensitivity(const long &v) {
    instance()->handle->SetInt("RollSensitivity",v);
    instance()->RollSensitivity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeRollSensitivity() {
    instance()->handle->RemoveInt("RollSensitivity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docSpinEnable() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"The movement of the 3D mouse that spins the view about its vertical axis is followed.\n"
"It also needs 'Enable rotations'.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getSpinEnable() {
    return instance()->SpinEnable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultSpinEnable() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setSpinEnable(const bool &v) {
    instance()->handle->SetBool("SpinEnable",v);
    instance()->SpinEnable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeSpinEnable() {
    instance()->handle->RemoveBool("SpinEnable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docSpinReverse() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"Turns round the direction of the movement of the 3D mouse that\n"
"spins the view about its vertical axis.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getSpinReverse() {
    return instance()->SpinReverse;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultSpinReverse() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setSpinReverse(const bool &v) {
    instance()->handle->SetBool("SpinReverse",v);
    instance()->SpinReverse = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeSpinReverse() {
    instance()->handle->RemoveBool("SpinReverse");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docSpinSensitivity() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"How fast the movement of the 3D mouse that spins the view about its vertical axis\n"
"is followed: -50 slows it to a tenth, 0 leaves it as the device gives it,\n"
"50 makes it three and a half times as fast. The global sensitivity\n"
"multiplies it.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getSpinSensitivity() {
    return instance()->SpinSensitivity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultSpinSensitivity() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setSpinSensitivity(const long &v) {
    instance()->handle->SetInt("SpinSensitivity",v);
    instance()->SpinSensitivity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeSpinSensitivity() {
    instance()->handle->RemoveInt("SpinSensitivity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docRemapping() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"Which axis of the device each of the six movements is taken from:\n"
"six digits, each of 0 to 5 once, in the order pan left/right, pan\n"
"up/down, zoom, tilt, roll, spin. 12345 (that is 012345) takes them\n"
"as the device gives them; anything that is no such number is\n"
"ignored. On no page.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getRemapping() {
    return instance()->Remapping;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultRemapping() {
    const static long def = 12345;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setRemapping(const long &v) {
    instance()->handle->SetInt("Remapping",v);
    instance()->Remapping = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeRemapping() {
    instance()->handle->RemoveInt("Remapping");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docCalibrate() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"A request more than a setting: the Calibrate button of the\n"
"Spaceball Motion page sets it, and the next event of the device\n"
"stores what the device reports at rest as the calibration and\n"
"takes this away again.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SpaceballParams::getCalibrate() {
    return instance()->Calibrate;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SpaceballParams::defaultCalibrate() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setCalibrate(const bool &v) {
    instance()->handle->SetBool("Calibrate",v);
    instance()->Calibrate = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeCalibrate() {
    instance()->handle->RemoveBool("Calibrate");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docCalibrationX() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"What the device reported for pan left/right while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getCalibrationX() {
    return instance()->CalibrationX;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultCalibrationX() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setCalibrationX(const long &v) {
    instance()->handle->SetInt("CalibrationX",v);
    instance()->CalibrationX = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeCalibrationX() {
    instance()->handle->RemoveInt("CalibrationX");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docCalibrationY() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"What the device reported for pan up/down while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getCalibrationY() {
    return instance()->CalibrationY;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultCalibrationY() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setCalibrationY(const long &v) {
    instance()->handle->SetInt("CalibrationY",v);
    instance()->CalibrationY = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeCalibrationY() {
    instance()->handle->RemoveInt("CalibrationY");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docCalibrationZ() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"What the device reported for zoom while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getCalibrationZ() {
    return instance()->CalibrationZ;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultCalibrationZ() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setCalibrationZ(const long &v) {
    instance()->handle->SetInt("CalibrationZ",v);
    instance()->CalibrationZ = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeCalibrationZ() {
    instance()->handle->RemoveInt("CalibrationZ");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docCalibrationXr() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"What the device reported for tilt while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getCalibrationXr() {
    return instance()->CalibrationXr;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultCalibrationXr() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setCalibrationXr(const long &v) {
    instance()->handle->SetInt("CalibrationXr",v);
    instance()->CalibrationXr = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeCalibrationXr() {
    instance()->handle->RemoveInt("CalibrationXr");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docCalibrationYr() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"What the device reported for roll while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getCalibrationYr() {
    return instance()->CalibrationYr;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultCalibrationYr() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setCalibrationYr(const long &v) {
    instance()->handle->SetInt("CalibrationYr",v);
    instance()->CalibrationYr = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeCalibrationYr() {
    instance()->handle->RemoveInt("CalibrationYr");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docCalibrationZr() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"What the device reported for spin while at rest when it\n"
"was last calibrated; taken off every reading. Stored by the\n"
"program after Calibrate, not meant to be set by hand.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SpaceballParams::getCalibrationZr() {
    return instance()->CalibrationZr;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SpaceballParams::defaultCalibrationZr() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setCalibrationZr(const long &v) {
    instance()->handle->SetInt("CalibrationZr",v);
    instance()->CalibrationZr = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeCalibrationZr() {
    instance()->handle->RemoveInt("CalibrationZr");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SpaceballParams::docModel() {
    return QT_TRANSLATE_NOOP("SpaceballParams",
"The device chosen in the Spaceball Buttons page of the Customize\n"
"dialog, whose buttons the page lists. Stored when the page's\n"
"Reset button is pressed, not when the choice changes.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SpaceballParams::getModel() {
    return instance()->Model;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SpaceballParams::defaultModel() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SpaceballParams::setModel(const std::string &v) {
    instance()->subHandles[0]->SetASCII("Model",v);
    instance()->Model = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SpaceballParams::removeModel() {
    instance()->subHandles[0]->RemoveASCII("Model");
}
//[[[end]]]
