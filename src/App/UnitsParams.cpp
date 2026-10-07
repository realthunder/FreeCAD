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
import UnitsParams
UnitsParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "UnitsParams.h"
using namespace App;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class UnitsParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(UnitsParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("UserSchema");
        signalParamChanged("Decimals");
        signalParamChanged("FracInch");
        signalParamChanged("IgnoreProjectSchema");
        signalParamChanged("DecimalsPreSel");

    // Auto generated code (Tools/params_utils.py:241)
    }
    long UserSchema;
    long Decimals;
    long FracInch;
    bool IgnoreProjectSchema;
    long DecimalsPreSel;

    // Auto generated code (Tools/params_utils.py:254)
    UnitsParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Units");
        handle->Attach(this);

        UserSchema = this->handle->GetInt("UserSchema", 0);
        funcs["UserSchema"] = &UnitsParamsP::updateUserSchema;
        Decimals = this->handle->GetInt("Decimals", 2);
        funcs["Decimals"] = &UnitsParamsP::updateDecimals;
        FracInch = this->handle->GetInt("FracInch", 8);
        funcs["FracInch"] = &UnitsParamsP::updateFracInch;
        IgnoreProjectSchema = this->handle->GetBool("IgnoreProjectSchema", false);
        funcs["IgnoreProjectSchema"] = &UnitsParamsP::updateIgnoreProjectSchema;
        DecimalsPreSel = this->handle->GetInt("DecimalsPreSel", -1);
        funcs["DecimalsPreSel"] = &UnitsParamsP::updateDecimalsPreSel;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~UnitsParamsP() override = default;

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
    static void updateUserSchema(UnitsParamsP *self) {
        self->UserSchema = self->handle->GetInt("UserSchema", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDecimals(UnitsParamsP *self) {
        self->Decimals = self->handle->GetInt("Decimals", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFracInch(UnitsParamsP *self) {
        self->FracInch = self->handle->GetInt("FracInch", 8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIgnoreProjectSchema(UnitsParamsP *self) {
        self->IgnoreProjectSchema = self->handle->GetBool("IgnoreProjectSchema", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDecimalsPreSel(UnitsParamsP *self) {
        self->DecimalsPreSel = self->handle->GetInt("DecimalsPreSel", -1);
    }
};

// Auto generated code (Tools/params_utils.py:336)
UnitsParamsP *instance() {
    static UnitsParamsP *inst = new UnitsParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _UnitsParamsRegistrar({
    App::ParamInfo("App", "UnitsParams", "User parameter:BaseApp/Preferences/Units", "UserSchema", "UserSchema", App::ParamInfo::Int, 0)
        .setTitle("Unit system")
        .setDoc("Unit system quantities are shown in, and the one a new document\n"
"starts with: 0 mm/kg/s, 1 m/kg/s, 2 US customary, 3 imperial\n"
"decimal, 4 building Euro, 5 building US, 6 metric CNC (mm,\n"
"mm/min), 7 imperial civil, 8 FEM (mm, N, s), 9 meter decimal. A\n"
"saved document shows its own unless told otherwise."),
    App::ParamInfo("App", "UnitsParams", "User parameter:BaseApp/Preferences/Units", "Decimals", "Decimals", App::ParamInfo::Int, 2)
        .setTitle("Number of decimals")
        .setDoc("Number of decimals numbers and dimensions are shown with. Takes\n"
"effect at once; a field already on screen follows when it is next\n"
"redrawn."),
    App::ParamInfo("App", "UnitsParams", "User parameter:BaseApp/Preferences/Units", "FracInch", "FracInch", App::ParamInfo::Int, 8)
        .setTitle("Minimum fractional inch")
        .setDoc("Smallest fraction of an inch the building US unit system shows,\n"
"as its denominator: 2, 4, 8, 16, 32, 64 or 128."),
    App::ParamInfo("App", "UnitsParams", "User parameter:BaseApp/Preferences/Units", "IgnoreProjectSchema", "IgnoreProjectSchema", App::ParamInfo::Bool, false)
        .setTitle("Ignore project unit system")
        .setDoc("Show every document in the unit system of the UserSchema setting\n"
"and ignore the one stored in the document."),
    App::ParamInfo("App", "UnitsParams", "User parameter:BaseApp/Preferences/Units", "DecimalsPreSel", "DecimalsPreSel", App::ParamInfo::Int, -1)
        .setTitle("Decimals of preselected coordinates")
        .setDoc("Number of decimals of the coordinates shown in the status bar for\n"
"the point under the mouse. -1 uses the general number of\n"
"decimals."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle UnitsParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
UnitsParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void UnitsParams::signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *UnitsParams::docUserSchema() {
    return QT_TRANSLATE_NOOP("UnitsParams",
"Unit system quantities are shown in, and the one a new document\n"
"starts with: 0 mm/kg/s, 1 m/kg/s, 2 US customary, 3 imperial\n"
"decimal, 4 building Euro, 5 building US, 6 metric CNC (mm,\n"
"mm/min), 7 imperial civil, 8 FEM (mm, N, s), 9 meter decimal. A\n"
"saved document shows its own unless told otherwise.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & UnitsParams::getUserSchema() {
    return instance()->UserSchema;
}

// Auto generated code (Tools/params_utils.py:413)
const long & UnitsParams::defaultUserSchema() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void UnitsParams::setUserSchema(const long &v) {
    instance()->handle->SetInt("UserSchema",v);
    instance()->UserSchema = v;
}

// Auto generated code (Tools/params_utils.py:431)
void UnitsParams::removeUserSchema() {
    instance()->handle->RemoveInt("UserSchema");
}

// Auto generated code (Tools/params_utils.py:397)
const char *UnitsParams::docDecimals() {
    return QT_TRANSLATE_NOOP("UnitsParams",
"Number of decimals numbers and dimensions are shown with. Takes\n"
"effect at once; a field already on screen follows when it is next\n"
"redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & UnitsParams::getDecimals() {
    return instance()->Decimals;
}

// Auto generated code (Tools/params_utils.py:413)
const long & UnitsParams::defaultDecimals() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void UnitsParams::setDecimals(const long &v) {
    instance()->handle->SetInt("Decimals",v);
    instance()->Decimals = v;
}

// Auto generated code (Tools/params_utils.py:431)
void UnitsParams::removeDecimals() {
    instance()->handle->RemoveInt("Decimals");
}

// Auto generated code (Tools/params_utils.py:397)
const char *UnitsParams::docFracInch() {
    return QT_TRANSLATE_NOOP("UnitsParams",
"Smallest fraction of an inch the building US unit system shows,\n"
"as its denominator: 2, 4, 8, 16, 32, 64 or 128.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & UnitsParams::getFracInch() {
    return instance()->FracInch;
}

// Auto generated code (Tools/params_utils.py:413)
const long & UnitsParams::defaultFracInch() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void UnitsParams::setFracInch(const long &v) {
    instance()->handle->SetInt("FracInch",v);
    instance()->FracInch = v;
}

// Auto generated code (Tools/params_utils.py:431)
void UnitsParams::removeFracInch() {
    instance()->handle->RemoveInt("FracInch");
}

// Auto generated code (Tools/params_utils.py:397)
const char *UnitsParams::docIgnoreProjectSchema() {
    return QT_TRANSLATE_NOOP("UnitsParams",
"Show every document in the unit system of the UserSchema setting\n"
"and ignore the one stored in the document.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & UnitsParams::getIgnoreProjectSchema() {
    return instance()->IgnoreProjectSchema;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & UnitsParams::defaultIgnoreProjectSchema() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void UnitsParams::setIgnoreProjectSchema(const bool &v) {
    instance()->handle->SetBool("IgnoreProjectSchema",v);
    instance()->IgnoreProjectSchema = v;
}

// Auto generated code (Tools/params_utils.py:431)
void UnitsParams::removeIgnoreProjectSchema() {
    instance()->handle->RemoveBool("IgnoreProjectSchema");
}

// Auto generated code (Tools/params_utils.py:397)
const char *UnitsParams::docDecimalsPreSel() {
    return QT_TRANSLATE_NOOP("UnitsParams",
"Number of decimals of the coordinates shown in the status bar for\n"
"the point under the mouse. -1 uses the general number of\n"
"decimals.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & UnitsParams::getDecimalsPreSel() {
    return instance()->DecimalsPreSel;
}

// Auto generated code (Tools/params_utils.py:413)
const long & UnitsParams::defaultDecimalsPreSel() {
    const static long def = -1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void UnitsParams::setDecimalsPreSel(const long &v) {
    instance()->handle->SetInt("DecimalsPreSel",v);
    instance()->DecimalsPreSel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void UnitsParams::removeDecimalsPreSel() {
    instance()->handle->RemoveInt("DecimalsPreSel");
}
//[[[end]]]
