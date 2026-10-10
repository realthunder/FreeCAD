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
import PointsParams
PointsParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "PointsParams.h"
using namespace Points;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class PointsParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(PointsParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    bool UseColor;
    bool CheckInvalidState;
    double MinDistance;

    // Auto generated code (Tools/params_utils.py:254)
    PointsParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Points");
        handle->Attach(this);

        subHandles.resize(1);
        subHandles[0] = handle->GetGroup("E57");
        subHandles[0]->Attach(this);
        UseColor = this->subHandles[0]->GetBool("UseColor", true);
        funcs["UseColor"] = &PointsParamsP::updateUseColor;
        CheckInvalidState = this->subHandles[0]->GetBool("CheckInvalidState", true);
        funcs["CheckInvalidState"] = &PointsParamsP::updateCheckInvalidState;
        MinDistance = this->subHandles[0]->GetFloat("MinDistance", -1.0);
        funcs["MinDistance"] = &PointsParamsP::updateMinDistance;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~PointsParamsP() override = default;

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
    static void updateUseColor(PointsParamsP *self) {
        self->UseColor = self->subHandles[0]->GetBool("UseColor", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckInvalidState(PointsParamsP *self) {
        self->CheckInvalidState = self->subHandles[0]->GetBool("CheckInvalidState", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMinDistance(PointsParamsP *self) {
        self->MinDistance = self->subHandles[0]->GetFloat("MinDistance", -1.0);
    }
};

// Auto generated code (Tools/params_utils.py:336)
PointsParamsP *instance() {
    static PointsParamsP *inst = new PointsParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _PointsParamsRegistrar({
    App::ParamInfo("Points", "PointsParams", "User parameter:BaseApp/Preferences/Mod/Points/E57", "UseColor", "UseColor", App::ParamInfo::Bool, true)
        .setTitle("Read E57 colours")
        .setDoc("Reads the colour of each point when an E57 file that has colours\n"
"is imported. Takes effect at the next import."),
    App::ParamInfo("Points", "PointsParams", "User parameter:BaseApp/Preferences/Mod/Points/E57", "CheckInvalidState", "CheckInvalidState", App::ParamInfo::Bool, true)
        .setTitle("Skip invalid E57 points")
        .setDoc("Takes the invalid-point flag of an E57 file into account when the\n"
"file is imported. Takes effect at the next import."),
    App::ParamInfo("Points", "PointsParams", "User parameter:BaseApp/Preferences/Mod/Points/E57", "MinDistance", "MinDistance", App::ParamInfo::Float, -1.0)
        .setTitle("Minimum E57 point distance")
        .setDoc("Leaves out a point of an E57 file that lies closer than this\n"
"distance to the point read before it; a negative value keeps every\n"
"point. Takes effect at the next import."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle PointsParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *PointsParams::docUseColor() {
    return QT_TRANSLATE_NOOP("PointsParams",
"Reads the colour of each point when an E57 file that has colours\n"
"is imported. Takes effect at the next import.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PointsParams::getUseColor() {
    return instance()->UseColor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PointsParams::defaultUseColor() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PointsParams::setUseColor(const bool &v) {
    instance()->subHandles[0]->SetBool("UseColor",v);
    instance()->UseColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PointsParams::removeUseColor() {
    instance()->subHandles[0]->RemoveBool("UseColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PointsParams::docCheckInvalidState() {
    return QT_TRANSLATE_NOOP("PointsParams",
"Takes the invalid-point flag of an E57 file into account when the\n"
"file is imported. Takes effect at the next import.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PointsParams::getCheckInvalidState() {
    return instance()->CheckInvalidState;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PointsParams::defaultCheckInvalidState() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PointsParams::setCheckInvalidState(const bool &v) {
    instance()->subHandles[0]->SetBool("CheckInvalidState",v);
    instance()->CheckInvalidState = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PointsParams::removeCheckInvalidState() {
    instance()->subHandles[0]->RemoveBool("CheckInvalidState");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PointsParams::docMinDistance() {
    return QT_TRANSLATE_NOOP("PointsParams",
"Leaves out a point of an E57 file that lies closer than this\n"
"distance to the point read before it; a negative value keeps every\n"
"point. Takes effect at the next import.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & PointsParams::getMinDistance() {
    return instance()->MinDistance;
}

// Auto generated code (Tools/params_utils.py:413)
const double & PointsParams::defaultMinDistance() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PointsParams::setMinDistance(const double &v) {
    instance()->subHandles[0]->SetFloat("MinDistance",v);
    instance()->MinDistance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PointsParams::removeMinDistance() {
    instance()->subHandles[0]->RemoveFloat("MinDistance");
}
//[[[end]]]
