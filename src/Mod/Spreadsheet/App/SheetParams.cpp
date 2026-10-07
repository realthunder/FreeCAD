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
#include <App/ParamRegistry.h>
#include "SheetParams.h"
using namespace Spreadsheet;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class SheetParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(SheetParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    bool showAliasName;
    std::string DisplayAliasFormatString;
    std::string AliasedCellBackgroundColor;
    std::string AliasedCellForegroundColor;
    std::string LockedAliasedCellColor;
    std::string TextColor;
    std::string PositiveNumberColor;
    std::string NegativeNumberColor;
    bool VerticalConfTable;
    bool DoubleBindConfTable;
    std::string ImportExportDelimiter;
    std::string ImportExportQuoteCharacter;
    std::string ImportExportEscapeCharacter;

    // Auto generated code (Tools/params_utils.py:254)
    SheetParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Spreadsheet");
        handle->Attach(this);

        showAliasName = this->handle->GetBool("showAliasName", false);
        funcs["showAliasName"] = &SheetParamsP::updateshowAliasName;
        DisplayAliasFormatString = this->handle->GetASCII("DisplayAliasFormatString", "%V = %A");
        funcs["DisplayAliasFormatString"] = &SheetParamsP::updateDisplayAliasFormatString;
        AliasedCellBackgroundColor = this->handle->GetASCII("AliasedCellBackgroundColor", "#feff9e");
        funcs["AliasedCellBackgroundColor"] = &SheetParamsP::updateAliasedCellBackgroundColor;
        AliasedCellForegroundColor = this->handle->GetASCII("AliasedCellForegroundColor", "#242424");
        funcs["AliasedCellForegroundColor"] = &SheetParamsP::updateAliasedCellForegroundColor;
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
        ImportExportDelimiter = this->handle->GetASCII("ImportExportDelimiter", "tab");
        funcs["ImportExportDelimiter"] = &SheetParamsP::updateImportExportDelimiter;
        ImportExportQuoteCharacter = this->handle->GetASCII("ImportExportQuoteCharacter", "\"");
        funcs["ImportExportQuoteCharacter"] = &SheetParamsP::updateImportExportQuoteCharacter;
        ImportExportEscapeCharacter = this->handle->GetASCII("ImportExportEscapeCharacter", "\\");
        funcs["ImportExportEscapeCharacter"] = &SheetParamsP::updateImportExportEscapeCharacter;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~SheetParamsP() override = default;

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
    static void updateshowAliasName(SheetParamsP *self) {
        self->showAliasName = self->handle->GetBool("showAliasName", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDisplayAliasFormatString(SheetParamsP *self) {
        self->DisplayAliasFormatString = self->handle->GetASCII("DisplayAliasFormatString", "%V = %A");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAliasedCellBackgroundColor(SheetParamsP *self) {
        self->AliasedCellBackgroundColor = self->handle->GetASCII("AliasedCellBackgroundColor", "#feff9e");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAliasedCellForegroundColor(SheetParamsP *self) {
        self->AliasedCellForegroundColor = self->handle->GetASCII("AliasedCellForegroundColor", "#242424");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLockedAliasedCellColor(SheetParamsP *self) {
        self->LockedAliasedCellColor = self->handle->GetASCII("LockedAliasedCellColor", "#9effff");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTextColor(SheetParamsP *self) {
        self->TextColor = self->handle->GetASCII("TextColor", "#000000");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePositiveNumberColor(SheetParamsP *self) {
        self->PositiveNumberColor = self->handle->GetASCII("PositiveNumberColor", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNegativeNumberColor(SheetParamsP *self) {
        self->NegativeNumberColor = self->handle->GetASCII("NegativeNumberColor", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVerticalConfTable(SheetParamsP *self) {
        self->VerticalConfTable = self->handle->GetBool("VerticalConfTable", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDoubleBindConfTable(SheetParamsP *self) {
        self->DoubleBindConfTable = self->handle->GetBool("DoubleBindConfTable", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateImportExportDelimiter(SheetParamsP *self) {
        self->ImportExportDelimiter = self->handle->GetASCII("ImportExportDelimiter", "tab");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateImportExportQuoteCharacter(SheetParamsP *self) {
        self->ImportExportQuoteCharacter = self->handle->GetASCII("ImportExportQuoteCharacter", "\"");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateImportExportEscapeCharacter(SheetParamsP *self) {
        self->ImportExportEscapeCharacter = self->handle->GetASCII("ImportExportEscapeCharacter", "\\");
    }
};

// Auto generated code (Tools/params_utils.py:336)
SheetParamsP *instance() {
    static SheetParamsP *inst = new SheetParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _SheetParamsRegistrar({
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "showAliasName", "showAliasName", App::ParamInfo::Bool, false)
        .setTitle("Show alias name")
        .setDoc("Show the alias of a cell together with its value in the\n"
"spreadsheet, laid out by the alias format string."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "DisplayAliasFormatString", "DisplayAliasFormatString", App::ParamInfo::String, "%V = %A")
        .setTitle("Display Alias Format String")
        .setDoc("How a cell with an alias is shown when aliases are displayed.\n"
"%V stands for the value and %A for the alias."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "AliasedCellBackgroundColor", "AliasedCellBackgroundColor", App::ParamInfo::String, "#feff9e")
        .setTitle("Aliased Cell Background Color")
        .setDoc("Background colour of spreadsheet cells that have an alias, as a\n"
"colour name or #rrggbb."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "AliasedCellForegroundColor", "AliasedCellForegroundColor", App::ParamInfo::String, "#242424")
        .setTitle("Aliased Cell Foreground Color")
        .setDoc("Text colour of spreadsheet cells that have an alias, as a colour\n"
"name or #rrggbb. A style sheet can override it."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "LockedAliasedCellColor", "LockedAliasedCellColor", App::ParamInfo::String, "#9effff")
        .setTitle("Locked Aliased Cell Color")
        .setDoc("Background colour of spreadsheet cells whose alias is locked, as a\n"
"colour name or #rrggbb."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "TextColor", "TextColor", App::ParamInfo::String, "#000000")
        .setTitle("Text Color")
        .setDoc("Text colour of spreadsheet cells that have no colour of their own,\n"
"as a colour name or #rrggbb."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "PositiveNumberColor", "PositiveNumberColor", App::ParamInfo::String, "")
        .setTitle("Positive Number Color")
        .setDoc("Text colour of spreadsheet cells holding a number that is not\n"
"negative, as a colour name or #rrggbb. Empty uses the normal text\n"
"colour."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "NegativeNumberColor", "NegativeNumberColor", App::ParamInfo::String, "")
        .setTitle("Negative Number Color")
        .setDoc("Text colour of spreadsheet cells holding a negative number, as a\n"
"colour name or #rrggbb. Empty uses the normal text colour."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "VerticalConfTable", "VerticalConfTable", App::ParamInfo::Bool, false)
        .setTitle("Vertical Conf Table")
        .setDoc("Start the configuration table dialog in vertical layout, with one\n"
"configuration per column, when a single column is selected.\n"
"Follows the Vertical checkbox of that dialog."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "DoubleBindConfTable", "DoubleBindConfTable", App::ParamInfo::Bool, false)
        .setTitle("Double Bind Conf Table")
        .setDoc("Tick Double Bind when the configuration table dialog opens on a\n"
"single column selection. The top-left cell of the table then both\n"
"shows and sets the current configuration."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "ImportExportDelimiter", "ImportExportDelimiter", App::ParamInfo::String, "tab")
        .setTitle("Delimiter character")
        .setDoc("Character that separates the fields when a spreadsheet is imported\n"
"from or exported to a text file; the words tab, comma and\n"
"semicolon are accepted too. Takes effect at the next import or\n"
"export."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "ImportExportQuoteCharacter", "ImportExportQuoteCharacter", App::ParamInfo::String, "\"")
        .setTitle("Quote character")
        .setDoc("Character that encloses text fields when a spreadsheet is imported\n"
"from or exported to a text file. It must be a single character.\n"
"Takes effect at the next import or export."),
    App::ParamInfo("Spreadsheet", "SheetParams", "User parameter:BaseApp/Preferences/Mod/Spreadsheet", "ImportExportEscapeCharacter", "ImportExportEscapeCharacter", App::ParamInfo::String, "\\")
        .setTitle("Escape character")
        .setDoc("Character that marks special characters when a spreadsheet is\n"
"imported from or exported to a text file. It must be a single\n"
"character. Takes effect at the next import or export."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle SheetParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docshowAliasName() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Show the alias of a cell together with its value in the\n"
"spreadsheet, laid out by the alias format string.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SheetParams::getshowAliasName() {
    return instance()->showAliasName;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SheetParams::defaultshowAliasName() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setshowAliasName(const bool &v) {
    instance()->handle->SetBool("showAliasName",v);
    instance()->showAliasName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeshowAliasName() {
    instance()->handle->RemoveBool("showAliasName");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docDisplayAliasFormatString() {
    return QT_TRANSLATE_NOOP("SheetParams",
"How a cell with an alias is shown when aliases are displayed.\n"
"%V stands for the value and %A for the alias.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getDisplayAliasFormatString() {
    return instance()->DisplayAliasFormatString;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultDisplayAliasFormatString() {
    const static std::string def = "%V = %A";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setDisplayAliasFormatString(const std::string &v) {
    instance()->handle->SetASCII("DisplayAliasFormatString",v);
    instance()->DisplayAliasFormatString = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeDisplayAliasFormatString() {
    instance()->handle->RemoveASCII("DisplayAliasFormatString");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docAliasedCellBackgroundColor() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Background colour of spreadsheet cells that have an alias, as a\n"
"colour name or #rrggbb.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getAliasedCellBackgroundColor() {
    return instance()->AliasedCellBackgroundColor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultAliasedCellBackgroundColor() {
    const static std::string def = "#feff9e";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setAliasedCellBackgroundColor(const std::string &v) {
    instance()->handle->SetASCII("AliasedCellBackgroundColor",v);
    instance()->AliasedCellBackgroundColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeAliasedCellBackgroundColor() {
    instance()->handle->RemoveASCII("AliasedCellBackgroundColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docAliasedCellForegroundColor() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Text colour of spreadsheet cells that have an alias, as a colour\n"
"name or #rrggbb. A style sheet can override it.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getAliasedCellForegroundColor() {
    return instance()->AliasedCellForegroundColor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultAliasedCellForegroundColor() {
    const static std::string def = "#242424";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setAliasedCellForegroundColor(const std::string &v) {
    instance()->handle->SetASCII("AliasedCellForegroundColor",v);
    instance()->AliasedCellForegroundColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeAliasedCellForegroundColor() {
    instance()->handle->RemoveASCII("AliasedCellForegroundColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docLockedAliasedCellColor() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Background colour of spreadsheet cells whose alias is locked, as a\n"
"colour name or #rrggbb.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getLockedAliasedCellColor() {
    return instance()->LockedAliasedCellColor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultLockedAliasedCellColor() {
    const static std::string def = "#9effff";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setLockedAliasedCellColor(const std::string &v) {
    instance()->handle->SetASCII("LockedAliasedCellColor",v);
    instance()->LockedAliasedCellColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeLockedAliasedCellColor() {
    instance()->handle->RemoveASCII("LockedAliasedCellColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docTextColor() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Text colour of spreadsheet cells that have no colour of their own,\n"
"as a colour name or #rrggbb.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getTextColor() {
    return instance()->TextColor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultTextColor() {
    const static std::string def = "#000000";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setTextColor(const std::string &v) {
    instance()->handle->SetASCII("TextColor",v);
    instance()->TextColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeTextColor() {
    instance()->handle->RemoveASCII("TextColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docPositiveNumberColor() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Text colour of spreadsheet cells holding a number that is not\n"
"negative, as a colour name or #rrggbb. Empty uses the normal text\n"
"colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getPositiveNumberColor() {
    return instance()->PositiveNumberColor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultPositiveNumberColor() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setPositiveNumberColor(const std::string &v) {
    instance()->handle->SetASCII("PositiveNumberColor",v);
    instance()->PositiveNumberColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removePositiveNumberColor() {
    instance()->handle->RemoveASCII("PositiveNumberColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docNegativeNumberColor() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Text colour of spreadsheet cells holding a negative number, as a\n"
"colour name or #rrggbb. Empty uses the normal text colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getNegativeNumberColor() {
    return instance()->NegativeNumberColor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultNegativeNumberColor() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setNegativeNumberColor(const std::string &v) {
    instance()->handle->SetASCII("NegativeNumberColor",v);
    instance()->NegativeNumberColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeNegativeNumberColor() {
    instance()->handle->RemoveASCII("NegativeNumberColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docVerticalConfTable() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Start the configuration table dialog in vertical layout, with one\n"
"configuration per column, when a single column is selected.\n"
"Follows the Vertical checkbox of that dialog.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SheetParams::getVerticalConfTable() {
    return instance()->VerticalConfTable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SheetParams::defaultVerticalConfTable() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setVerticalConfTable(const bool &v) {
    instance()->handle->SetBool("VerticalConfTable",v);
    instance()->VerticalConfTable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeVerticalConfTable() {
    instance()->handle->RemoveBool("VerticalConfTable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docDoubleBindConfTable() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Tick Double Bind when the configuration table dialog opens on a\n"
"single column selection. The top-left cell of the table then both\n"
"shows and sets the current configuration.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SheetParams::getDoubleBindConfTable() {
    return instance()->DoubleBindConfTable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SheetParams::defaultDoubleBindConfTable() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setDoubleBindConfTable(const bool &v) {
    instance()->handle->SetBool("DoubleBindConfTable",v);
    instance()->DoubleBindConfTable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeDoubleBindConfTable() {
    instance()->handle->RemoveBool("DoubleBindConfTable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docImportExportDelimiter() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Character that separates the fields when a spreadsheet is imported\n"
"from or exported to a text file; the words tab, comma and\n"
"semicolon are accepted too. Takes effect at the next import or\n"
"export.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getImportExportDelimiter() {
    return instance()->ImportExportDelimiter;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultImportExportDelimiter() {
    const static std::string def = "tab";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setImportExportDelimiter(const std::string &v) {
    instance()->handle->SetASCII("ImportExportDelimiter",v);
    instance()->ImportExportDelimiter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeImportExportDelimiter() {
    instance()->handle->RemoveASCII("ImportExportDelimiter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docImportExportQuoteCharacter() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Character that encloses text fields when a spreadsheet is imported\n"
"from or exported to a text file. It must be a single character.\n"
"Takes effect at the next import or export.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getImportExportQuoteCharacter() {
    return instance()->ImportExportQuoteCharacter;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultImportExportQuoteCharacter() {
    const static std::string def = "\"";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setImportExportQuoteCharacter(const std::string &v) {
    instance()->handle->SetASCII("ImportExportQuoteCharacter",v);
    instance()->ImportExportQuoteCharacter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeImportExportQuoteCharacter() {
    instance()->handle->RemoveASCII("ImportExportQuoteCharacter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SheetParams::docImportExportEscapeCharacter() {
    return QT_TRANSLATE_NOOP("SheetParams",
"Character that marks special characters when a spreadsheet is\n"
"imported from or exported to a text file. It must be a single\n"
"character. Takes effect at the next import or export.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SheetParams::getImportExportEscapeCharacter() {
    return instance()->ImportExportEscapeCharacter;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SheetParams::defaultImportExportEscapeCharacter() {
    const static std::string def = "\\";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SheetParams::setImportExportEscapeCharacter(const std::string &v) {
    instance()->handle->SetASCII("ImportExportEscapeCharacter",v);
    instance()->ImportExportEscapeCharacter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SheetParams::removeImportExportEscapeCharacter() {
    instance()->handle->RemoveASCII("ImportExportEscapeCharacter");
}
//[[[end]]]
