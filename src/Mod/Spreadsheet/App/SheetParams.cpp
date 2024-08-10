/****************************************************************************
 *   Copyright (c) 2023 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
import SheetParams
SheetParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include "SheetParams.h"
using namespace Spreadsheet;

// Auto generated code (Tools/params_utils.py:209)
namespace {
class SheetParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(SheetParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    bool showAliasName;
    std::string DisplayAliasFormatString;
    std::string AliasedCellBackgroundColor;
    std::string LockedAliasedCellColor;
    std::string TextColor;
    std::string PositiveNumberColor;
    std::string NegativeNumberColor;
    bool VerticalConfTable;
    bool DoubleBindConfTable;

    // Auto generated code (Tools/params_utils.py:253)
    SheetParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Spreadsheet");
        handle->Attach(this);

        showAliasName = this->handle->GetBool("showAliasName", false);
        funcs["showAliasName"] = &SheetParamsP::updateshowAliasName;
        DisplayAliasFormatString = this->handle->GetASCII("DisplayAliasFormatString", "%V = %A");
        funcs["DisplayAliasFormatString"] = &SheetParamsP::updateDisplayAliasFormatString;
        AliasedCellBackgroundColor = this->handle->GetASCII("AliasedCellBackgroundColor", "#feff9e");
        funcs["AliasedCellBackgroundColor"] = &SheetParamsP::updateAliasedCellBackgroundColor;
        LockedAliasedCellColor = this->handle->GetASCII("LockedAliasedCellColor", "#9effff");
        funcs["LockedAliasedCellColor"] = &SheetParamsP::updateLockedAliasedCellColor;
        TextColor = this->handle->GetASCII("TextColor", "#000000");
        funcs["TextColor"] = &SheetParamsP::updateTextColor;
        PositiveNumberColor = this->handle->GetASCII("PositiveNumberColor", "");
        funcs["PositiveNumberColor"] = &SheetParamsP::updatePositiveNumberColor;
        NegativeNumberColor = this->handle->GetASCII("NegativeNumberColor", "");
        funcs["NegativeNumberColor"] = &SheetParamsP::updateNegativeNumberColor;
        VerticalConfTable = this->handle->GetBool("VerticalConfTable", false);
        funcs["VerticalConfTable"] = &SheetParamsP::updateVerticalConfTable;
        DoubleBindConfTable = this->handle->GetBool("DoubleBindConfTable", false);
        funcs["DoubleBindConfTable"] = &SheetParamsP::updateDoubleBindConfTable;
    }

    // Auto generated code (Tools/params_utils.py:283)
    ~SheetParamsP() {
    }

    // Auto generated code (Tools/params_utils.py:290)
    void OnChange(Base::Subject<const char*> &param, const char* sReason) {
        (void)param;
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        
        
    }


    // Auto generated code (Tools/params_utils.py:310)
    static void updateshowAliasName(SheetParamsP *self) {
        self->showAliasName = self->handle->GetBool("showAliasName", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDisplayAliasFormatString(SheetParamsP *self) {
        self->DisplayAliasFormatString = self->handle->GetASCII("DisplayAliasFormatString", "%V = %A");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAliasedCellBackgroundColor(SheetParamsP *self) {
        self->AliasedCellBackgroundColor = self->handle->GetASCII("AliasedCellBackgroundColor", "#feff9e");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLockedAliasedCellColor(SheetParamsP *self) {
        self->LockedAliasedCellColor = self->handle->GetASCII("LockedAliasedCellColor", "#9effff");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateTextColor(SheetParamsP *self) {
        self->TextColor = self->handle->GetASCII("TextColor", "#000000");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePositiveNumberColor(SheetParamsP *self) {
        self->PositiveNumberColor = self->handle->GetASCII("PositiveNumberColor", "");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateNegativeNumberColor(SheetParamsP *self) {
        self->NegativeNumberColor = self->handle->GetASCII("NegativeNumberColor", "");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateVerticalConfTable(SheetParamsP *self) {
        self->VerticalConfTable = self->handle->GetBool("VerticalConfTable", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDoubleBindConfTable(SheetParamsP *self) {
        self->DoubleBindConfTable = self->handle->GetBool("DoubleBindConfTable", false);
    }
};

// Auto generated code (Tools/params_utils.py:332)
SheetParamsP *instance() {
    static SheetParamsP *inst = new SheetParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:343)
ParameterGrp::handle SheetParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:372)
const char *SheetParams::docshowAliasName() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & SheetParams::getshowAliasName() {
    return instance()->showAliasName;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & SheetParams::defaultshowAliasName() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void SheetParams::setshowAliasName(const bool &v) {
    instance()->handle->SetBool("showAliasName",v);
    instance()->showAliasName = v;
}

// Auto generated code (Tools/params_utils.py:406)
void SheetParams::removeshowAliasName() {
    instance()->handle->RemoveBool("showAliasName");
}

// Auto generated code (Tools/params_utils.py:372)
const char *SheetParams::docDisplayAliasFormatString() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & SheetParams::getDisplayAliasFormatString() {
    return instance()->DisplayAliasFormatString;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & SheetParams::defaultDisplayAliasFormatString() {
    const static std::string def = "%V = %A";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void SheetParams::setDisplayAliasFormatString(const std::string &v) {
    instance()->handle->SetASCII("DisplayAliasFormatString",v);
    instance()->DisplayAliasFormatString = v;
}

// Auto generated code (Tools/params_utils.py:406)
void SheetParams::removeDisplayAliasFormatString() {
    instance()->handle->RemoveASCII("DisplayAliasFormatString");
}

// Auto generated code (Tools/params_utils.py:372)
const char *SheetParams::docAliasedCellBackgroundColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & SheetParams::getAliasedCellBackgroundColor() {
    return instance()->AliasedCellBackgroundColor;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & SheetParams::defaultAliasedCellBackgroundColor() {
    const static std::string def = "#feff9e";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void SheetParams::setAliasedCellBackgroundColor(const std::string &v) {
    instance()->handle->SetASCII("AliasedCellBackgroundColor",v);
    instance()->AliasedCellBackgroundColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void SheetParams::removeAliasedCellBackgroundColor() {
    instance()->handle->RemoveASCII("AliasedCellBackgroundColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *SheetParams::docLockedAliasedCellColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & SheetParams::getLockedAliasedCellColor() {
    return instance()->LockedAliasedCellColor;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & SheetParams::defaultLockedAliasedCellColor() {
    const static std::string def = "#9effff";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void SheetParams::setLockedAliasedCellColor(const std::string &v) {
    instance()->handle->SetASCII("LockedAliasedCellColor",v);
    instance()->LockedAliasedCellColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void SheetParams::removeLockedAliasedCellColor() {
    instance()->handle->RemoveASCII("LockedAliasedCellColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *SheetParams::docTextColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & SheetParams::getTextColor() {
    return instance()->TextColor;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & SheetParams::defaultTextColor() {
    const static std::string def = "#000000";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void SheetParams::setTextColor(const std::string &v) {
    instance()->handle->SetASCII("TextColor",v);
    instance()->TextColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void SheetParams::removeTextColor() {
    instance()->handle->RemoveASCII("TextColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *SheetParams::docPositiveNumberColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & SheetParams::getPositiveNumberColor() {
    return instance()->PositiveNumberColor;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & SheetParams::defaultPositiveNumberColor() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void SheetParams::setPositiveNumberColor(const std::string &v) {
    instance()->handle->SetASCII("PositiveNumberColor",v);
    instance()->PositiveNumberColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void SheetParams::removePositiveNumberColor() {
    instance()->handle->RemoveASCII("PositiveNumberColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *SheetParams::docNegativeNumberColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & SheetParams::getNegativeNumberColor() {
    return instance()->NegativeNumberColor;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & SheetParams::defaultNegativeNumberColor() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void SheetParams::setNegativeNumberColor(const std::string &v) {
    instance()->handle->SetASCII("NegativeNumberColor",v);
    instance()->NegativeNumberColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void SheetParams::removeNegativeNumberColor() {
    instance()->handle->RemoveASCII("NegativeNumberColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *SheetParams::docVerticalConfTable() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & SheetParams::getVerticalConfTable() {
    return instance()->VerticalConfTable;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & SheetParams::defaultVerticalConfTable() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void SheetParams::setVerticalConfTable(const bool &v) {
    instance()->handle->SetBool("VerticalConfTable",v);
    instance()->VerticalConfTable = v;
}

// Auto generated code (Tools/params_utils.py:406)
void SheetParams::removeVerticalConfTable() {
    instance()->handle->RemoveBool("VerticalConfTable");
}

// Auto generated code (Tools/params_utils.py:372)
const char *SheetParams::docDoubleBindConfTable() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & SheetParams::getDoubleBindConfTable() {
    return instance()->DoubleBindConfTable;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & SheetParams::defaultDoubleBindConfTable() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void SheetParams::setDoubleBindConfTable(const bool &v) {
    instance()->handle->SetBool("DoubleBindConfTable",v);
    instance()->DoubleBindConfTable = v;
}

// Auto generated code (Tools/params_utils.py:406)
void SheetParams::removeDoubleBindConfTable() {
    instance()->handle->RemoveBool("DoubleBindConfTable");
}
//[[[end]]]
