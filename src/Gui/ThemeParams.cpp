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
import ThemeParams
ThemeParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "ThemeParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class ThemeParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(ThemeParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("ThemeAccentColor1");
        signalParamChanged("ThemeAccentColor2");
        signalParamChanged("ThemeAccentColor3");

    // Auto generated code (Tools/params_utils.py:241)
    }
    unsigned long ThemeAccentColor1;
    unsigned long ThemeAccentColor2;
    unsigned long ThemeAccentColor3;

    // Auto generated code (Tools/params_utils.py:254)
    ThemeParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Themes");
        handle->Attach(this);

        ThemeAccentColor1 = this->handle->GetUnsigned("ThemeAccentColor1", 0x557BB6FF);
        funcs["ThemeAccentColor1"] = &ThemeParamsP::updateThemeAccentColor1;
        ThemeAccentColor2 = this->handle->GetUnsigned("ThemeAccentColor2", 0x405C89FF);
        funcs["ThemeAccentColor2"] = &ThemeParamsP::updateThemeAccentColor2;
        ThemeAccentColor3 = this->handle->GetUnsigned("ThemeAccentColor3", 0x4B6CA0FF);
        funcs["ThemeAccentColor3"] = &ThemeParamsP::updateThemeAccentColor3;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~ThemeParamsP() override = default;

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
    static void updateThemeAccentColor1(ThemeParamsP *self) {
        self->ThemeAccentColor1 = self->handle->GetUnsigned("ThemeAccentColor1", 0x557BB6FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateThemeAccentColor2(ThemeParamsP *self) {
        self->ThemeAccentColor2 = self->handle->GetUnsigned("ThemeAccentColor2", 0x405C89FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateThemeAccentColor3(ThemeParamsP *self) {
        self->ThemeAccentColor3 = self->handle->GetUnsigned("ThemeAccentColor3", 0x4B6CA0FF);
    }
};

// Auto generated code (Tools/params_utils.py:336)
ThemeParamsP *instance() {
    static ThemeParamsP *inst = new ThemeParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _ThemeParamsRegistrar({
    App::ParamInfo("Gui", "ThemeParams", "User parameter:BaseApp/Preferences/Themes", "ThemeAccentColor1", "ThemeAccentColor1", App::ParamInfo::Hex, 0x557BB6FF)
        .setTitle("Accent colour 1")
        .setDoc("Highlight colour of the style sheets: hovered, selected and\n"
"checked items. Applied shortly after a change.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ThemeParams", "User parameter:BaseApp/Preferences/Themes", "ThemeAccentColor2", "ThemeAccentColor2", App::ParamInfo::Hex, 0x405C89FF)
        .setTitle("Accent colour 2")
        .setDoc("Colour the style sheets use for the engaged state: focus, a\n"
"pressed button, an open combo box. The Dark theme sets a lighter\n"
"one. Applied shortly after a change.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ThemeParams", "User parameter:BaseApp/Preferences/Themes", "ThemeAccentColor3", "ThemeAccentColor3", App::ParamInfo::Hex, 0x4B6CA0FF)
        .setTitle("Accent colour 3")
        .setDoc("Far end of the gradients the style sheets draw from accent colour\n"
"1. Applied shortly after a change.")
        .setProxy("Color")
        .setTransparency(false),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle ThemeParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
ThemeParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void ThemeParams::signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *ThemeParams::docThemeAccentColor1() {
    return QT_TRANSLATE_NOOP("ThemeParams",
"Highlight colour of the style sheets: hovered, selected and\n"
"checked items. Applied shortly after a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ThemeParams::getThemeAccentColor1() {
    return instance()->ThemeAccentColor1;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ThemeParams::defaultThemeAccentColor1() {
    const static unsigned long def = 0x557BB6FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ThemeParams::setThemeAccentColor1(const unsigned long &v) {
    instance()->handle->SetUnsigned("ThemeAccentColor1",v);
    instance()->ThemeAccentColor1 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ThemeParams::removeThemeAccentColor1() {
    instance()->handle->RemoveUnsigned("ThemeAccentColor1");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ThemeParams::docThemeAccentColor2() {
    return QT_TRANSLATE_NOOP("ThemeParams",
"Colour the style sheets use for the engaged state: focus, a\n"
"pressed button, an open combo box. The Dark theme sets a lighter\n"
"one. Applied shortly after a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ThemeParams::getThemeAccentColor2() {
    return instance()->ThemeAccentColor2;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ThemeParams::defaultThemeAccentColor2() {
    const static unsigned long def = 0x405C89FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ThemeParams::setThemeAccentColor2(const unsigned long &v) {
    instance()->handle->SetUnsigned("ThemeAccentColor2",v);
    instance()->ThemeAccentColor2 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ThemeParams::removeThemeAccentColor2() {
    instance()->handle->RemoveUnsigned("ThemeAccentColor2");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ThemeParams::docThemeAccentColor3() {
    return QT_TRANSLATE_NOOP("ThemeParams",
"Far end of the gradients the style sheets draw from accent colour\n"
"1. Applied shortly after a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ThemeParams::getThemeAccentColor3() {
    return instance()->ThemeAccentColor3;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ThemeParams::defaultThemeAccentColor3() {
    const static unsigned long def = 0x4B6CA0FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ThemeParams::setThemeAccentColor3(const unsigned long &v) {
    instance()->handle->SetUnsigned("ThemeAccentColor3",v);
    instance()->ThemeAccentColor3 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ThemeParams::removeThemeAccentColor3() {
    instance()->handle->RemoveUnsigned("ThemeAccentColor3");
}
//[[[end]]]
