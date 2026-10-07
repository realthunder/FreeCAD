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
import NaviCubeParams
NaviCubeParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "NaviCubeParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class NaviCubeParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(NaviCubeParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    long CubeSize;
    bool NaviRotateToNearest;
    long NaviStepByTurn;
    bool ShowCS;
    double BorderWidth;
    double ChamferSize;
    bool AutoHideCube;
    bool AutoHideButton;
    long AutoHideTimeout;
    bool FontAutoSize;
    double FontScale;
    std::string FontString;
    long FontSize;
    long FontWeight;
    bool FontItalic;
    long FontStretch;
    std::string AxisFont;
    long AxisFontSize;
    long AxisFontWeight;
    bool AxisFontItalic;
    unsigned long TextColor;
    unsigned long HiliteColor;
    unsigned long FrontColor;
    unsigned long EdgeColor;
    unsigned long CornerColor;
    unsigned long ButtonColor;
    unsigned long BorderColor;
    unsigned long AxisLabelColor;

    // Auto generated code (Tools/params_utils.py:254)
    NaviCubeParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/NaviCube");
        handle->Attach(this);

        CubeSize = this->handle->GetInt("CubeSize", 132);
        funcs["CubeSize"] = &NaviCubeParamsP::updateCubeSize;
        NaviRotateToNearest = this->handle->GetBool("NaviRotateToNearest", true);
        funcs["NaviRotateToNearest"] = &NaviCubeParamsP::updateNaviRotateToNearest;
        NaviStepByTurn = this->handle->GetInt("NaviStepByTurn", 8);
        funcs["NaviStepByTurn"] = &NaviCubeParamsP::updateNaviStepByTurn;
        ShowCS = this->handle->GetBool("ShowCS", true);
        funcs["ShowCS"] = &NaviCubeParamsP::updateShowCS;
        BorderWidth = this->handle->GetFloat("BorderWidth", 1.5);
        funcs["BorderWidth"] = &NaviCubeParamsP::updateBorderWidth;
        ChamferSize = this->handle->GetFloat("ChamferSize", 0.12);
        funcs["ChamferSize"] = &NaviCubeParamsP::updateChamferSize;
        AutoHideCube = this->handle->GetBool("AutoHideCube", false);
        funcs["AutoHideCube"] = &NaviCubeParamsP::updateAutoHideCube;
        AutoHideButton = this->handle->GetBool("AutoHideButton", true);
        funcs["AutoHideButton"] = &NaviCubeParamsP::updateAutoHideButton;
        AutoHideTimeout = this->handle->GetInt("AutoHideTimeout", 300);
        funcs["AutoHideTimeout"] = &NaviCubeParamsP::updateAutoHideTimeout;
        FontAutoSize = this->handle->GetBool("FontAutoSize", true);
        funcs["FontAutoSize"] = &NaviCubeParamsP::updateFontAutoSize;
        FontScale = this->handle->GetFloat("FontScale", 0.22);
        funcs["FontScale"] = &NaviCubeParamsP::updateFontScale;
        FontString = this->handle->GetASCII("FontString", "Helvetica");
        funcs["FontString"] = &NaviCubeParamsP::updateFontString;
        FontSize = this->handle->GetInt("FontSize", 0);
        funcs["FontSize"] = &NaviCubeParamsP::updateFontSize;
        FontWeight = this->handle->GetInt("FontWeight", 87);
        funcs["FontWeight"] = &NaviCubeParamsP::updateFontWeight;
        FontItalic = this->handle->GetBool("FontItalic", false);
        funcs["FontItalic"] = &NaviCubeParamsP::updateFontItalic;
        FontStretch = this->handle->GetInt("FontStretch", 62);
        funcs["FontStretch"] = &NaviCubeParamsP::updateFontStretch;
        AxisFont = this->handle->GetASCII("AxisFont", "Monospace");
        funcs["AxisFont"] = &NaviCubeParamsP::updateAxisFont;
        AxisFontSize = this->handle->GetInt("AxisFontSize", 8);
        funcs["AxisFontSize"] = &NaviCubeParamsP::updateAxisFontSize;
        AxisFontWeight = this->handle->GetInt("AxisFontWeight", 50);
        funcs["AxisFontWeight"] = &NaviCubeParamsP::updateAxisFontWeight;
        AxisFontItalic = this->handle->GetBool("AxisFontItalic", false);
        funcs["AxisFontItalic"] = &NaviCubeParamsP::updateAxisFontItalic;
        TextColor = this->handle->GetUnsigned("TextColor", 0xFF000000);
        funcs["TextColor"] = &NaviCubeParamsP::updateTextColor;
        HiliteColor = this->handle->GetUnsigned("HiliteColor", 0xFFAAE2FF);
        funcs["HiliteColor"] = &NaviCubeParamsP::updateHiliteColor;
        FrontColor = this->handle->GetUnsigned("FrontColor", 0xC0E2E9EF);
        funcs["FrontColor"] = &NaviCubeParamsP::updateFrontColor;
        EdgeColor = this->handle->GetUnsigned("EdgeColor", 0xC0A1A6AB);
        funcs["EdgeColor"] = &NaviCubeParamsP::updateEdgeColor;
        CornerColor = this->handle->GetUnsigned("CornerColor", 0xC0CDD4D9);
        funcs["CornerColor"] = &NaviCubeParamsP::updateCornerColor;
        ButtonColor = this->handle->GetUnsigned("ButtonColor", 0x80E2E9EF);
        funcs["ButtonColor"] = &NaviCubeParamsP::updateButtonColor;
        BorderColor = this->handle->GetUnsigned("BorderColor", 0xFF323232);
        funcs["BorderColor"] = &NaviCubeParamsP::updateBorderColor;
        AxisLabelColor = this->handle->GetUnsigned("AxisLabelColor", 0xFF000000);
        funcs["AxisLabelColor"] = &NaviCubeParamsP::updateAxisLabelColor;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~NaviCubeParamsP() override = default;

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
    static void updateCubeSize(NaviCubeParamsP *self) {
        self->CubeSize = self->handle->GetInt("CubeSize", 132);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNaviRotateToNearest(NaviCubeParamsP *self) {
        self->NaviRotateToNearest = self->handle->GetBool("NaviRotateToNearest", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNaviStepByTurn(NaviCubeParamsP *self) {
        self->NaviStepByTurn = self->handle->GetInt("NaviStepByTurn", 8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowCS(NaviCubeParamsP *self) {
        self->ShowCS = self->handle->GetBool("ShowCS", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBorderWidth(NaviCubeParamsP *self) {
        self->BorderWidth = self->handle->GetFloat("BorderWidth", 1.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateChamferSize(NaviCubeParamsP *self) {
        self->ChamferSize = self->handle->GetFloat("ChamferSize", 0.12);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoHideCube(NaviCubeParamsP *self) {
        self->AutoHideCube = self->handle->GetBool("AutoHideCube", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoHideButton(NaviCubeParamsP *self) {
        self->AutoHideButton = self->handle->GetBool("AutoHideButton", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoHideTimeout(NaviCubeParamsP *self) {
        self->AutoHideTimeout = self->handle->GetInt("AutoHideTimeout", 300);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFontAutoSize(NaviCubeParamsP *self) {
        self->FontAutoSize = self->handle->GetBool("FontAutoSize", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFontScale(NaviCubeParamsP *self) {
        self->FontScale = self->handle->GetFloat("FontScale", 0.22);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFontString(NaviCubeParamsP *self) {
        self->FontString = self->handle->GetASCII("FontString", "Helvetica");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFontSize(NaviCubeParamsP *self) {
        self->FontSize = self->handle->GetInt("FontSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFontWeight(NaviCubeParamsP *self) {
        self->FontWeight = self->handle->GetInt("FontWeight", 87);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFontItalic(NaviCubeParamsP *self) {
        self->FontItalic = self->handle->GetBool("FontItalic", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFontStretch(NaviCubeParamsP *self) {
        self->FontStretch = self->handle->GetInt("FontStretch", 62);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisFont(NaviCubeParamsP *self) {
        self->AxisFont = self->handle->GetASCII("AxisFont", "Monospace");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisFontSize(NaviCubeParamsP *self) {
        self->AxisFontSize = self->handle->GetInt("AxisFontSize", 8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisFontWeight(NaviCubeParamsP *self) {
        self->AxisFontWeight = self->handle->GetInt("AxisFontWeight", 50);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisFontItalic(NaviCubeParamsP *self) {
        self->AxisFontItalic = self->handle->GetBool("AxisFontItalic", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTextColor(NaviCubeParamsP *self) {
        self->TextColor = self->handle->GetUnsigned("TextColor", 0xFF000000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiliteColor(NaviCubeParamsP *self) {
        self->HiliteColor = self->handle->GetUnsigned("HiliteColor", 0xFFAAE2FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFrontColor(NaviCubeParamsP *self) {
        self->FrontColor = self->handle->GetUnsigned("FrontColor", 0xC0E2E9EF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEdgeColor(NaviCubeParamsP *self) {
        self->EdgeColor = self->handle->GetUnsigned("EdgeColor", 0xC0A1A6AB);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCornerColor(NaviCubeParamsP *self) {
        self->CornerColor = self->handle->GetUnsigned("CornerColor", 0xC0CDD4D9);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateButtonColor(NaviCubeParamsP *self) {
        self->ButtonColor = self->handle->GetUnsigned("ButtonColor", 0x80E2E9EF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBorderColor(NaviCubeParamsP *self) {
        self->BorderColor = self->handle->GetUnsigned("BorderColor", 0xFF323232);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisLabelColor(NaviCubeParamsP *self) {
        self->AxisLabelColor = self->handle->GetUnsigned("AxisLabelColor", 0xFF000000);
    }
};

// Auto generated code (Tools/params_utils.py:336)
NaviCubeParamsP *instance() {
    static NaviCubeParamsP *inst = new NaviCubeParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _NaviCubeParamsRegistrar({
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "CubeSize", "CubeSize", App::ParamInfo::Int, 132)
        .setTitle("Navigation cube size")
        .setDoc("Size of the navigation cube in pixels, 10 to 1024."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "NaviRotateToNearest", "NaviRotateToNearest", App::ParamInfo::Bool, true)
        .setTitle("Rotate to nearest")
        .setDoc("A click on a face of the navigation cube turns the view to that\n"
"face in the nearest of its four upright positions. When off the\n"
"face is shown the way its text reads."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "NaviStepByTurn", "NaviStepByTurn", App::ParamInfo::Int, 8)
        .setTitle("Steps by turn")
        .setDoc("Number of steps a full turn is made in with the arrow buttons of\n"
"the navigation cube, 4 to 36."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "ShowCS", "ShowCS", App::ParamInfo::Bool, true)
        .setTitle("Show coordinate system on the cube")
        .setDoc("Draw the X, Y and Z axes at a corner of the navigation cube."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "BorderWidth", "BorderWidth", App::ParamInfo::Float, 1.5)
        .setTitle("Navigation cube border width")
        .setDoc("Width in pixels of the lines around the faces of the navigation\n"
"cube."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "ChamferSize", "ChamferSize", App::ParamInfo::Float, 0.12)
        .setTitle("Navigation cube chamfer size")
        .setDoc("Size of the edge and corner faces of the navigation cube, as a\n"
"fraction of the cube."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "AutoHideCube", "AutoHideCube", App::ParamInfo::Bool, false)
        .setTitle("Auto hide navigation cube")
        .setDoc("Hide the navigation cube while the mouse is away from it."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "AutoHideButton", "AutoHideButton", App::ParamInfo::Bool, true)
        .setTitle("Auto hide navigation cube buttons")
        .setDoc("Hide the arrow buttons around the navigation cube while the mouse\n"
"is away from it."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "AutoHideTimeout", "AutoHideTimeout", App::ParamInfo::Int, 300)
        .setTitle("Auto hide delay")
        .setDoc("Milliseconds the mouse has to be away before the navigation cube\n"
"or its buttons are hidden."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "FontAutoSize", "FontAutoSize", App::ParamInfo::Bool, true)
        .setTitle("Automatic font size")
        .setDoc("Size the texts on the faces of the navigation cube to the faces,\n"
"by FontScale. When off FontSize is used."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "FontScale", "FontScale", App::ParamInfo::Float, 0.22)
        .setTitle("Font scale")
        .setDoc("Height of the texts on the navigation cube as a fraction of a\n"
"face, with FontAutoSize on."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "FontString", "FontString", App::ParamInfo::String, "Helvetica")
        .setTitle("Navigation cube font")
        .setDoc("Font family of the texts on the faces of the navigation cube."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "FontSize", "FontSize", App::ParamInfo::Int, 0)
        .setTitle("Navigation cube font size")
        .setDoc("Font size of the texts on the faces of the navigation cube in\n"
"points, with FontAutoSize off. 0 sizes them to the faces."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "FontWeight", "FontWeight", App::ParamInfo::Int, 87)
        .setTitle("Navigation cube font weight")
        .setDoc("Weight of the font on the faces of the navigation cube, 0 to 99."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "FontItalic", "FontItalic", App::ParamInfo::Bool, false)
        .setTitle("Navigation cube font italic")
        .setDoc("Draw the texts on the faces of the navigation cube in italics."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "FontStretch", "FontStretch", App::ParamInfo::Int, 62)
        .setTitle("Navigation cube font stretch")
        .setDoc("Horizontal stretch of the font on the faces of the navigation\n"
"cube in percent; 100 is the width the font has."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "AxisFont", "AxisFont", App::ParamInfo::String, "Monospace")
        .setTitle("Axis label font")
        .setDoc("Font family of the axis labels of the navigation cube."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "AxisFontSize", "AxisFontSize", App::ParamInfo::Int, 8)
        .setTitle("Axis label font size")
        .setDoc("Font size of the axis labels of the navigation cube in points."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "AxisFontWeight", "AxisFontWeight", App::ParamInfo::Int, 50)
        .setTitle("Axis label font weight")
        .setDoc("Weight of the font of the axis labels of the navigation cube, 0\n"
"to 99."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "AxisFontItalic", "AxisFontItalic", App::ParamInfo::Bool, false)
        .setTitle("Axis label font italic")
        .setDoc("Draw the axis labels of the navigation cube in italics."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "TextColor", "TextColor", App::ParamInfo::Hex, 0xFF000000)
        .setTitle("Navigation cube text colour")
        .setDoc("Colour of the texts on the navigation cube, as 0xAARRGGBB."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "HiliteColor", "HiliteColor", App::ParamInfo::Hex, 0xFFAAE2FF)
        .setTitle("Navigation cube highlight colour")
        .setDoc("Colour of the part of the navigation cube under the mouse, as\n"
"0xAARRGGBB."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "FrontColor", "FrontColor", App::ParamInfo::Hex, 0xC0E2E9EF)
        .setTitle("Navigation cube face colour")
        .setDoc("Colour of the six main faces of the navigation cube, as\n"
"0xAARRGGBB."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "EdgeColor", "EdgeColor", App::ParamInfo::Hex, 0xC0A1A6AB)
        .setTitle("Navigation cube edge colour")
        .setDoc("Colour of the edge faces of the navigation cube, as 0xAARRGGBB."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "CornerColor", "CornerColor", App::ParamInfo::Hex, 0xC0CDD4D9)
        .setTitle("Navigation cube corner colour")
        .setDoc("Colour of the corner faces of the navigation cube, as\n"
"0xAARRGGBB."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "ButtonColor", "ButtonColor", App::ParamInfo::Hex, 0x80E2E9EF)
        .setTitle("Navigation cube button colour")
        .setDoc("Colour of the arrow buttons around the navigation cube, as\n"
"0xAARRGGBB."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "BorderColor", "BorderColor", App::ParamInfo::Hex, 0xFF323232)
        .setTitle("Navigation cube border colour")
        .setDoc("Colour of the lines around the faces of the navigation cube, as\n"
"0xAARRGGBB."),
    App::ParamInfo("Gui", "NaviCubeParams", "User parameter:BaseApp/Preferences/NaviCube", "AxisLabelColor", "AxisLabelColor", App::ParamInfo::Hex, 0xFF000000)
        .setTitle("Axis label colour")
        .setDoc("Colour of the axis labels of the navigation cube, as 0xAARRGGBB."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle NaviCubeParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docCubeSize() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Size of the navigation cube in pixels, 10 to 1024.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NaviCubeParams::getCubeSize() {
    return instance()->CubeSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NaviCubeParams::defaultCubeSize() {
    const static long def = 132;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setCubeSize(const long &v) {
    instance()->handle->SetInt("CubeSize",v);
    instance()->CubeSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeCubeSize() {
    instance()->handle->RemoveInt("CubeSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docNaviRotateToNearest() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"A click on a face of the navigation cube turns the view to that\n"
"face in the nearest of its four upright positions. When off the\n"
"face is shown the way its text reads.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NaviCubeParams::getNaviRotateToNearest() {
    return instance()->NaviRotateToNearest;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NaviCubeParams::defaultNaviRotateToNearest() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setNaviRotateToNearest(const bool &v) {
    instance()->handle->SetBool("NaviRotateToNearest",v);
    instance()->NaviRotateToNearest = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeNaviRotateToNearest() {
    instance()->handle->RemoveBool("NaviRotateToNearest");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docNaviStepByTurn() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Number of steps a full turn is made in with the arrow buttons of\n"
"the navigation cube, 4 to 36.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NaviCubeParams::getNaviStepByTurn() {
    return instance()->NaviStepByTurn;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NaviCubeParams::defaultNaviStepByTurn() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setNaviStepByTurn(const long &v) {
    instance()->handle->SetInt("NaviStepByTurn",v);
    instance()->NaviStepByTurn = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeNaviStepByTurn() {
    instance()->handle->RemoveInt("NaviStepByTurn");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docShowCS() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Draw the X, Y and Z axes at a corner of the navigation cube.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NaviCubeParams::getShowCS() {
    return instance()->ShowCS;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NaviCubeParams::defaultShowCS() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setShowCS(const bool &v) {
    instance()->handle->SetBool("ShowCS",v);
    instance()->ShowCS = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeShowCS() {
    instance()->handle->RemoveBool("ShowCS");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docBorderWidth() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Width in pixels of the lines around the faces of the navigation\n"
"cube.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & NaviCubeParams::getBorderWidth() {
    return instance()->BorderWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const double & NaviCubeParams::defaultBorderWidth() {
    const static double def = 1.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setBorderWidth(const double &v) {
    instance()->handle->SetFloat("BorderWidth",v);
    instance()->BorderWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeBorderWidth() {
    instance()->handle->RemoveFloat("BorderWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docChamferSize() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Size of the edge and corner faces of the navigation cube, as a\n"
"fraction of the cube.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & NaviCubeParams::getChamferSize() {
    return instance()->ChamferSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & NaviCubeParams::defaultChamferSize() {
    const static double def = 0.12;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setChamferSize(const double &v) {
    instance()->handle->SetFloat("ChamferSize",v);
    instance()->ChamferSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeChamferSize() {
    instance()->handle->RemoveFloat("ChamferSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docAutoHideCube() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Hide the navigation cube while the mouse is away from it.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NaviCubeParams::getAutoHideCube() {
    return instance()->AutoHideCube;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NaviCubeParams::defaultAutoHideCube() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setAutoHideCube(const bool &v) {
    instance()->handle->SetBool("AutoHideCube",v);
    instance()->AutoHideCube = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeAutoHideCube() {
    instance()->handle->RemoveBool("AutoHideCube");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docAutoHideButton() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Hide the arrow buttons around the navigation cube while the mouse\n"
"is away from it.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NaviCubeParams::getAutoHideButton() {
    return instance()->AutoHideButton;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NaviCubeParams::defaultAutoHideButton() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setAutoHideButton(const bool &v) {
    instance()->handle->SetBool("AutoHideButton",v);
    instance()->AutoHideButton = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeAutoHideButton() {
    instance()->handle->RemoveBool("AutoHideButton");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docAutoHideTimeout() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Milliseconds the mouse has to be away before the navigation cube\n"
"or its buttons are hidden.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NaviCubeParams::getAutoHideTimeout() {
    return instance()->AutoHideTimeout;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NaviCubeParams::defaultAutoHideTimeout() {
    const static long def = 300;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setAutoHideTimeout(const long &v) {
    instance()->handle->SetInt("AutoHideTimeout",v);
    instance()->AutoHideTimeout = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeAutoHideTimeout() {
    instance()->handle->RemoveInt("AutoHideTimeout");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docFontAutoSize() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Size the texts on the faces of the navigation cube to the faces,\n"
"by FontScale. When off FontSize is used.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NaviCubeParams::getFontAutoSize() {
    return instance()->FontAutoSize;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NaviCubeParams::defaultFontAutoSize() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setFontAutoSize(const bool &v) {
    instance()->handle->SetBool("FontAutoSize",v);
    instance()->FontAutoSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeFontAutoSize() {
    instance()->handle->RemoveBool("FontAutoSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docFontScale() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Height of the texts on the navigation cube as a fraction of a\n"
"face, with FontAutoSize on.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & NaviCubeParams::getFontScale() {
    return instance()->FontScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & NaviCubeParams::defaultFontScale() {
    const static double def = 0.22;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setFontScale(const double &v) {
    instance()->handle->SetFloat("FontScale",v);
    instance()->FontScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeFontScale() {
    instance()->handle->RemoveFloat("FontScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docFontString() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Font family of the texts on the faces of the navigation cube.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & NaviCubeParams::getFontString() {
    return instance()->FontString;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & NaviCubeParams::defaultFontString() {
    const static std::string def = "Helvetica";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setFontString(const std::string &v) {
    instance()->handle->SetASCII("FontString",v);
    instance()->FontString = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeFontString() {
    instance()->handle->RemoveASCII("FontString");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docFontSize() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Font size of the texts on the faces of the navigation cube in\n"
"points, with FontAutoSize off. 0 sizes them to the faces.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NaviCubeParams::getFontSize() {
    return instance()->FontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NaviCubeParams::defaultFontSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setFontSize(const long &v) {
    instance()->handle->SetInt("FontSize",v);
    instance()->FontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeFontSize() {
    instance()->handle->RemoveInt("FontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docFontWeight() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Weight of the font on the faces of the navigation cube, 0 to 99.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NaviCubeParams::getFontWeight() {
    return instance()->FontWeight;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NaviCubeParams::defaultFontWeight() {
    const static long def = 87;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setFontWeight(const long &v) {
    instance()->handle->SetInt("FontWeight",v);
    instance()->FontWeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeFontWeight() {
    instance()->handle->RemoveInt("FontWeight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docFontItalic() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Draw the texts on the faces of the navigation cube in italics.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NaviCubeParams::getFontItalic() {
    return instance()->FontItalic;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NaviCubeParams::defaultFontItalic() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setFontItalic(const bool &v) {
    instance()->handle->SetBool("FontItalic",v);
    instance()->FontItalic = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeFontItalic() {
    instance()->handle->RemoveBool("FontItalic");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docFontStretch() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Horizontal stretch of the font on the faces of the navigation\n"
"cube in percent; 100 is the width the font has.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NaviCubeParams::getFontStretch() {
    return instance()->FontStretch;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NaviCubeParams::defaultFontStretch() {
    const static long def = 62;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setFontStretch(const long &v) {
    instance()->handle->SetInt("FontStretch",v);
    instance()->FontStretch = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeFontStretch() {
    instance()->handle->RemoveInt("FontStretch");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docAxisFont() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Font family of the axis labels of the navigation cube.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & NaviCubeParams::getAxisFont() {
    return instance()->AxisFont;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & NaviCubeParams::defaultAxisFont() {
    const static std::string def = "Monospace";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setAxisFont(const std::string &v) {
    instance()->handle->SetASCII("AxisFont",v);
    instance()->AxisFont = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeAxisFont() {
    instance()->handle->RemoveASCII("AxisFont");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docAxisFontSize() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Font size of the axis labels of the navigation cube in points.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NaviCubeParams::getAxisFontSize() {
    return instance()->AxisFontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NaviCubeParams::defaultAxisFontSize() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setAxisFontSize(const long &v) {
    instance()->handle->SetInt("AxisFontSize",v);
    instance()->AxisFontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeAxisFontSize() {
    instance()->handle->RemoveInt("AxisFontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docAxisFontWeight() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Weight of the font of the axis labels of the navigation cube, 0\n"
"to 99.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NaviCubeParams::getAxisFontWeight() {
    return instance()->AxisFontWeight;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NaviCubeParams::defaultAxisFontWeight() {
    const static long def = 50;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setAxisFontWeight(const long &v) {
    instance()->handle->SetInt("AxisFontWeight",v);
    instance()->AxisFontWeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeAxisFontWeight() {
    instance()->handle->RemoveInt("AxisFontWeight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docAxisFontItalic() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Draw the axis labels of the navigation cube in italics.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NaviCubeParams::getAxisFontItalic() {
    return instance()->AxisFontItalic;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NaviCubeParams::defaultAxisFontItalic() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setAxisFontItalic(const bool &v) {
    instance()->handle->SetBool("AxisFontItalic",v);
    instance()->AxisFontItalic = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeAxisFontItalic() {
    instance()->handle->RemoveBool("AxisFontItalic");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docTextColor() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Colour of the texts on the navigation cube, as 0xAARRGGBB.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & NaviCubeParams::getTextColor() {
    return instance()->TextColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & NaviCubeParams::defaultTextColor() {
    const static unsigned long def = 0xFF000000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setTextColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("TextColor",v);
    instance()->TextColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeTextColor() {
    instance()->handle->RemoveUnsigned("TextColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docHiliteColor() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Colour of the part of the navigation cube under the mouse, as\n"
"0xAARRGGBB.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & NaviCubeParams::getHiliteColor() {
    return instance()->HiliteColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & NaviCubeParams::defaultHiliteColor() {
    const static unsigned long def = 0xFFAAE2FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setHiliteColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("HiliteColor",v);
    instance()->HiliteColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeHiliteColor() {
    instance()->handle->RemoveUnsigned("HiliteColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docFrontColor() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Colour of the six main faces of the navigation cube, as\n"
"0xAARRGGBB.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & NaviCubeParams::getFrontColor() {
    return instance()->FrontColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & NaviCubeParams::defaultFrontColor() {
    const static unsigned long def = 0xC0E2E9EF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setFrontColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("FrontColor",v);
    instance()->FrontColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeFrontColor() {
    instance()->handle->RemoveUnsigned("FrontColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docEdgeColor() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Colour of the edge faces of the navigation cube, as 0xAARRGGBB.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & NaviCubeParams::getEdgeColor() {
    return instance()->EdgeColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & NaviCubeParams::defaultEdgeColor() {
    const static unsigned long def = 0xC0A1A6AB;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setEdgeColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("EdgeColor",v);
    instance()->EdgeColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeEdgeColor() {
    instance()->handle->RemoveUnsigned("EdgeColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docCornerColor() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Colour of the corner faces of the navigation cube, as\n"
"0xAARRGGBB.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & NaviCubeParams::getCornerColor() {
    return instance()->CornerColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & NaviCubeParams::defaultCornerColor() {
    const static unsigned long def = 0xC0CDD4D9;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setCornerColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("CornerColor",v);
    instance()->CornerColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeCornerColor() {
    instance()->handle->RemoveUnsigned("CornerColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docButtonColor() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Colour of the arrow buttons around the navigation cube, as\n"
"0xAARRGGBB.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & NaviCubeParams::getButtonColor() {
    return instance()->ButtonColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & NaviCubeParams::defaultButtonColor() {
    const static unsigned long def = 0x80E2E9EF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setButtonColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("ButtonColor",v);
    instance()->ButtonColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeButtonColor() {
    instance()->handle->RemoveUnsigned("ButtonColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docBorderColor() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Colour of the lines around the faces of the navigation cube, as\n"
"0xAARRGGBB.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & NaviCubeParams::getBorderColor() {
    return instance()->BorderColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & NaviCubeParams::defaultBorderColor() {
    const static unsigned long def = 0xFF323232;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setBorderColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("BorderColor",v);
    instance()->BorderColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeBorderColor() {
    instance()->handle->RemoveUnsigned("BorderColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NaviCubeParams::docAxisLabelColor() {
    return QT_TRANSLATE_NOOP("NaviCubeParams",
"Colour of the axis labels of the navigation cube, as 0xAARRGGBB.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & NaviCubeParams::getAxisLabelColor() {
    return instance()->AxisLabelColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & NaviCubeParams::defaultAxisLabelColor() {
    const static unsigned long def = 0xFF000000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NaviCubeParams::setAxisLabelColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AxisLabelColor",v);
    instance()->AxisLabelColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NaviCubeParams::removeAxisLabelColor() {
    instance()->handle->RemoveUnsigned("AxisLabelColor");
}
//[[[end]]]
