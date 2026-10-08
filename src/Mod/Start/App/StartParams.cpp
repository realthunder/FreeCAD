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
import StartParams
StartParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "StartParams.h"
using namespace Start;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class StartParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(StartParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    bool ShowOnStartup;
    std::string AutoloadModule;
    bool closeStart;
    long FileThumbnailIconsSize;
    long FileCardSpacing;
    long NewFileIconSize;
    long FileCardLabelWith;
    long FileCardDescriptionLines;
    bool FileCardUseStyleSheet;
    unsigned long FileCardBackgroundColor;
    unsigned long FileCardBorderColor;
    unsigned long FileCardSelectionColor;
    unsigned long FileThumbnailBackgroundColor;
    unsigned long FileThumbnailBorderColor;
    unsigned long FileThumbnailSelectionColor;

    // Auto generated code (Tools/params_utils.py:254)
    StartParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Start");
        handle->Attach(this);

        ShowOnStartup = this->handle->GetBool("ShowOnStartup", true);
        funcs["ShowOnStartup"] = &StartParamsP::updateShowOnStartup;
        AutoloadModule = this->handle->GetASCII("AutoloadModule", "");
        funcs["AutoloadModule"] = &StartParamsP::updateAutoloadModule;
        closeStart = this->handle->GetBool("closeStart", false);
        funcs["closeStart"] = &StartParamsP::updatecloseStart;
        FileThumbnailIconsSize = this->handle->GetInt("FileThumbnailIconsSize", 128);
        funcs["FileThumbnailIconsSize"] = &StartParamsP::updateFileThumbnailIconsSize;
        FileCardSpacing = this->handle->GetInt("FileCardSpacing", 16);
        funcs["FileCardSpacing"] = &StartParamsP::updateFileCardSpacing;
        NewFileIconSize = this->handle->GetInt("NewFileIconSize", 48);
        funcs["NewFileIconSize"] = &StartParamsP::updateNewFileIconSize;
        FileCardLabelWith = this->handle->GetInt("FileCardLabelWith", 180);
        funcs["FileCardLabelWith"] = &StartParamsP::updateFileCardLabelWith;
        FileCardDescriptionLines = this->handle->GetInt("FileCardDescriptionLines", 2);
        funcs["FileCardDescriptionLines"] = &StartParamsP::updateFileCardDescriptionLines;
        FileCardUseStyleSheet = this->handle->GetBool("FileCardUseStyleSheet", true);
        funcs["FileCardUseStyleSheet"] = &StartParamsP::updateFileCardUseStyleSheet;
        FileCardBackgroundColor = this->handle->GetUnsigned("FileCardBackgroundColor", 0xDDDDDD00);
        funcs["FileCardBackgroundColor"] = &StartParamsP::updateFileCardBackgroundColor;
        FileCardBorderColor = this->handle->GetUnsigned("FileCardBorderColor", 0x62A0EA00);
        funcs["FileCardBorderColor"] = &StartParamsP::updateFileCardBorderColor;
        FileCardSelectionColor = this->handle->GetUnsigned("FileCardSelectionColor", 0x26A26900);
        funcs["FileCardSelectionColor"] = &StartParamsP::updateFileCardSelectionColor;
        FileThumbnailBackgroundColor = this->handle->GetUnsigned("FileThumbnailBackgroundColor", 0xDDDDDD00);
        funcs["FileThumbnailBackgroundColor"] = &StartParamsP::updateFileThumbnailBackgroundColor;
        FileThumbnailBorderColor = this->handle->GetUnsigned("FileThumbnailBorderColor", 0x62A0EA00);
        funcs["FileThumbnailBorderColor"] = &StartParamsP::updateFileThumbnailBorderColor;
        FileThumbnailSelectionColor = this->handle->GetUnsigned("FileThumbnailSelectionColor", 0x26A26900);
        funcs["FileThumbnailSelectionColor"] = &StartParamsP::updateFileThumbnailSelectionColor;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~StartParamsP() override = default;

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
    static void updateShowOnStartup(StartParamsP *self) {
        self->ShowOnStartup = self->handle->GetBool("ShowOnStartup", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoloadModule(StartParamsP *self) {
        self->AutoloadModule = self->handle->GetASCII("AutoloadModule", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatecloseStart(StartParamsP *self) {
        self->closeStart = self->handle->GetBool("closeStart", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileThumbnailIconsSize(StartParamsP *self) {
        self->FileThumbnailIconsSize = self->handle->GetInt("FileThumbnailIconsSize", 128);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileCardSpacing(StartParamsP *self) {
        self->FileCardSpacing = self->handle->GetInt("FileCardSpacing", 16);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNewFileIconSize(StartParamsP *self) {
        self->NewFileIconSize = self->handle->GetInt("NewFileIconSize", 48);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileCardLabelWith(StartParamsP *self) {
        self->FileCardLabelWith = self->handle->GetInt("FileCardLabelWith", 180);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileCardDescriptionLines(StartParamsP *self) {
        self->FileCardDescriptionLines = self->handle->GetInt("FileCardDescriptionLines", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileCardUseStyleSheet(StartParamsP *self) {
        self->FileCardUseStyleSheet = self->handle->GetBool("FileCardUseStyleSheet", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileCardBackgroundColor(StartParamsP *self) {
        self->FileCardBackgroundColor = self->handle->GetUnsigned("FileCardBackgroundColor", 0xDDDDDD00);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileCardBorderColor(StartParamsP *self) {
        self->FileCardBorderColor = self->handle->GetUnsigned("FileCardBorderColor", 0x62A0EA00);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileCardSelectionColor(StartParamsP *self) {
        self->FileCardSelectionColor = self->handle->GetUnsigned("FileCardSelectionColor", 0x26A26900);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileThumbnailBackgroundColor(StartParamsP *self) {
        self->FileThumbnailBackgroundColor = self->handle->GetUnsigned("FileThumbnailBackgroundColor", 0xDDDDDD00);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileThumbnailBorderColor(StartParamsP *self) {
        self->FileThumbnailBorderColor = self->handle->GetUnsigned("FileThumbnailBorderColor", 0x62A0EA00);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileThumbnailSelectionColor(StartParamsP *self) {
        self->FileThumbnailSelectionColor = self->handle->GetUnsigned("FileThumbnailSelectionColor", 0x26A26900);
    }
};

// Auto generated code (Tools/params_utils.py:336)
StartParamsP *instance() {
    static StartParamsP *inst = new StartParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _StartParamsRegistrar({
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "ShowOnStartup", "ShowOnStartup", App::ParamInfo::Bool, true)
        .setTitle("Show Start page")
        .setDoc("Opens the Start page when FreeCAD starts. Takes effect at the next\n"
"start."),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "AutoloadModule", "AutoloadModule", App::ParamInfo::String, "")
        .setTitle("Workbench after Start")
        .setDoc("Workbench activated after Empty file or Open File is used on the\n"
"Start page; $LastModule means the workbench used last, empty\n"
"leaves the workbench as it is. Takes effect at the next use."),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "closeStart", "closeStart", App::ParamInfo::Bool, false)
        .setTitle("Close Start page after use")
        .setDoc("Closes the Start page after a document has been created or opened\n"
"from it. Takes effect at the next use."),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileThumbnailIconsSize", "FileThumbnailIconsSize", App::ParamInfo::Int, 128)
        .setTitle("Thumbnail size")
        .setDoc("Size in pixels of the file thumbnails on the Start page. Takes\n"
"effect at the next repaint of the page."),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileCardSpacing", "FileCardSpacing", App::ParamInfo::Int, 16)
        .setTitle("Card spacing")
        .setDoc("Spacing in pixels between the file cards of the Start page. The\n"
"file lists follow at the next layout. The New File cards and the\n"
"page layout read the same key, with a spacing of their own while\n"
"it is not stored."),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "NewFileIconSize", "NewFileIconSize", App::ParamInfo::Int, 48)
        .setTitle("New File icon size")
        .setDoc("Size in pixels of the icons on the New File cards of the Start\n"
"page. Takes effect when the Start page is next created."),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileCardLabelWith", "FileCardLabelWith", App::ParamInfo::Int, 180)
        .setTitle("New File text width")
        .setDoc("Width in pixels reserved for the text of a New File card on the\n"
"Start page. Takes effect when the Start page is next created."),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileCardDescriptionLines", "FileCardDescriptionLines", App::ParamInfo::Int, 2)
        .setTitle("Card description lines")
        .setDoc("Number of lines the description of a New File card may wrap to\n"
"before it is cut short. Takes effect when the Start page is next\n"
"created."),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileCardUseStyleSheet", "FileCardUseStyleSheet", App::ParamInfo::Bool, true)
        .setTitle("Colour New File cards")
        .setDoc("Gives the New File cards of the Start page their own colours while\n"
"no theme style sheet is loaded. When off, the cards are drawn as\n"
"ordinary buttons. Takes effect at the next style change or when\n"
"the Start page is next created."),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileCardBackgroundColor", "FileCardBackgroundColor", App::ParamInfo::Hex, 0xDDDDDD00)
        .setTitle("New File card background")
        .setDoc("Background colour of the New File cards on the Start page. Only\n"
"used while no style sheet is loaded. Takes effect at the next\n"
"style change or when the Start page is next created.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileCardBorderColor", "FileCardBorderColor", App::ParamInfo::Hex, 0x62A0EA00)
        .setTitle("New File card hover border")
        .setDoc("Border colour of a New File card under the mouse pointer. Only\n"
"used while no style sheet is loaded. Takes effect at the next\n"
"style change or when the Start page is next created.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileCardSelectionColor", "FileCardSelectionColor", App::ParamInfo::Hex, 0x26A26900)
        .setTitle("New File card pressed border")
        .setDoc("Border colour of a New File card while it is pressed. Only used\n"
"while no style sheet is loaded. Takes effect at the next style\n"
"change or when the Start page is next created.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileThumbnailBackgroundColor", "FileThumbnailBackgroundColor", App::ParamInfo::Hex, 0xDDDDDD00)
        .setTitle("Thumbnail background colour")
        .setDoc("Background colour of the file thumbnails on the Start page. Only\n"
"used while no style sheet is loaded. Takes effect at the next\n"
"repaint.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileThumbnailBorderColor", "FileThumbnailBorderColor", App::ParamInfo::Hex, 0x62A0EA00)
        .setTitle("Thumbnail hover border")
        .setDoc("Border colour of a file thumbnail under the mouse pointer on the\n"
"Start page. Only used while no style sheet is loaded. Takes effect\n"
"at the next repaint.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Start", "StartParams", "User parameter:BaseApp/Preferences/Mod/Start", "FileThumbnailSelectionColor", "FileThumbnailSelectionColor", App::ParamInfo::Hex, 0x26A26900)
        .setTitle("Thumbnail selection border")
        .setDoc("Border colour of the selected file thumbnail on the Start page.\n"
"Only used while no style sheet is loaded. Takes effect at the next\n"
"repaint.")
        .setProxy("Color")
        .setTransparency(false),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle StartParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docShowOnStartup() {
    return QT_TRANSLATE_NOOP("StartParams",
"Opens the Start page when FreeCAD starts. Takes effect at the next\n"
"start.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & StartParams::getShowOnStartup() {
    return instance()->ShowOnStartup;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & StartParams::defaultShowOnStartup() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setShowOnStartup(const bool &v) {
    instance()->handle->SetBool("ShowOnStartup",v);
    instance()->ShowOnStartup = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeShowOnStartup() {
    instance()->handle->RemoveBool("ShowOnStartup");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docAutoloadModule() {
    return QT_TRANSLATE_NOOP("StartParams",
"Workbench activated after Empty file or Open File is used on the\n"
"Start page; $LastModule means the workbench used last, empty\n"
"leaves the workbench as it is. Takes effect at the next use.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & StartParams::getAutoloadModule() {
    return instance()->AutoloadModule;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & StartParams::defaultAutoloadModule() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setAutoloadModule(const std::string &v) {
    instance()->handle->SetASCII("AutoloadModule",v);
    instance()->AutoloadModule = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeAutoloadModule() {
    instance()->handle->RemoveASCII("AutoloadModule");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::doccloseStart() {
    return QT_TRANSLATE_NOOP("StartParams",
"Closes the Start page after a document has been created or opened\n"
"from it. Takes effect at the next use.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & StartParams::getcloseStart() {
    return instance()->closeStart;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & StartParams::defaultcloseStart() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setcloseStart(const bool &v) {
    instance()->handle->SetBool("closeStart",v);
    instance()->closeStart = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removecloseStart() {
    instance()->handle->RemoveBool("closeStart");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileThumbnailIconsSize() {
    return QT_TRANSLATE_NOOP("StartParams",
"Size in pixels of the file thumbnails on the Start page. Takes\n"
"effect at the next repaint of the page.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & StartParams::getFileThumbnailIconsSize() {
    return instance()->FileThumbnailIconsSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & StartParams::defaultFileThumbnailIconsSize() {
    const static long def = 128;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileThumbnailIconsSize(const long &v) {
    instance()->handle->SetInt("FileThumbnailIconsSize",v);
    instance()->FileThumbnailIconsSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileThumbnailIconsSize() {
    instance()->handle->RemoveInt("FileThumbnailIconsSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileCardSpacing() {
    return QT_TRANSLATE_NOOP("StartParams",
"Spacing in pixels between the file cards of the Start page. The\n"
"file lists follow at the next layout. The New File cards and the\n"
"page layout read the same key, with a spacing of their own while\n"
"it is not stored.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & StartParams::getFileCardSpacing() {
    return instance()->FileCardSpacing;
}

// Auto generated code (Tools/params_utils.py:413)
const long & StartParams::defaultFileCardSpacing() {
    const static long def = 16;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileCardSpacing(const long &v) {
    instance()->handle->SetInt("FileCardSpacing",v);
    instance()->FileCardSpacing = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileCardSpacing() {
    instance()->handle->RemoveInt("FileCardSpacing");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docNewFileIconSize() {
    return QT_TRANSLATE_NOOP("StartParams",
"Size in pixels of the icons on the New File cards of the Start\n"
"page. Takes effect when the Start page is next created.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & StartParams::getNewFileIconSize() {
    return instance()->NewFileIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & StartParams::defaultNewFileIconSize() {
    const static long def = 48;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setNewFileIconSize(const long &v) {
    instance()->handle->SetInt("NewFileIconSize",v);
    instance()->NewFileIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeNewFileIconSize() {
    instance()->handle->RemoveInt("NewFileIconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileCardLabelWith() {
    return QT_TRANSLATE_NOOP("StartParams",
"Width in pixels reserved for the text of a New File card on the\n"
"Start page. Takes effect when the Start page is next created.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & StartParams::getFileCardLabelWith() {
    return instance()->FileCardLabelWith;
}

// Auto generated code (Tools/params_utils.py:413)
const long & StartParams::defaultFileCardLabelWith() {
    const static long def = 180;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileCardLabelWith(const long &v) {
    instance()->handle->SetInt("FileCardLabelWith",v);
    instance()->FileCardLabelWith = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileCardLabelWith() {
    instance()->handle->RemoveInt("FileCardLabelWith");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileCardDescriptionLines() {
    return QT_TRANSLATE_NOOP("StartParams",
"Number of lines the description of a New File card may wrap to\n"
"before it is cut short. Takes effect when the Start page is next\n"
"created.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & StartParams::getFileCardDescriptionLines() {
    return instance()->FileCardDescriptionLines;
}

// Auto generated code (Tools/params_utils.py:413)
const long & StartParams::defaultFileCardDescriptionLines() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileCardDescriptionLines(const long &v) {
    instance()->handle->SetInt("FileCardDescriptionLines",v);
    instance()->FileCardDescriptionLines = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileCardDescriptionLines() {
    instance()->handle->RemoveInt("FileCardDescriptionLines");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileCardUseStyleSheet() {
    return QT_TRANSLATE_NOOP("StartParams",
"Gives the New File cards of the Start page their own colours while\n"
"no theme style sheet is loaded. When off, the cards are drawn as\n"
"ordinary buttons. Takes effect at the next style change or when\n"
"the Start page is next created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & StartParams::getFileCardUseStyleSheet() {
    return instance()->FileCardUseStyleSheet;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & StartParams::defaultFileCardUseStyleSheet() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileCardUseStyleSheet(const bool &v) {
    instance()->handle->SetBool("FileCardUseStyleSheet",v);
    instance()->FileCardUseStyleSheet = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileCardUseStyleSheet() {
    instance()->handle->RemoveBool("FileCardUseStyleSheet");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileCardBackgroundColor() {
    return QT_TRANSLATE_NOOP("StartParams",
"Background colour of the New File cards on the Start page. Only\n"
"used while no style sheet is loaded. Takes effect at the next\n"
"style change or when the Start page is next created.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & StartParams::getFileCardBackgroundColor() {
    return instance()->FileCardBackgroundColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & StartParams::defaultFileCardBackgroundColor() {
    const static unsigned long def = 0xDDDDDD00;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileCardBackgroundColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("FileCardBackgroundColor",v);
    instance()->FileCardBackgroundColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileCardBackgroundColor() {
    instance()->handle->RemoveUnsigned("FileCardBackgroundColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileCardBorderColor() {
    return QT_TRANSLATE_NOOP("StartParams",
"Border colour of a New File card under the mouse pointer. Only\n"
"used while no style sheet is loaded. Takes effect at the next\n"
"style change or when the Start page is next created.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & StartParams::getFileCardBorderColor() {
    return instance()->FileCardBorderColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & StartParams::defaultFileCardBorderColor() {
    const static unsigned long def = 0x62A0EA00;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileCardBorderColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("FileCardBorderColor",v);
    instance()->FileCardBorderColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileCardBorderColor() {
    instance()->handle->RemoveUnsigned("FileCardBorderColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileCardSelectionColor() {
    return QT_TRANSLATE_NOOP("StartParams",
"Border colour of a New File card while it is pressed. Only used\n"
"while no style sheet is loaded. Takes effect at the next style\n"
"change or when the Start page is next created.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & StartParams::getFileCardSelectionColor() {
    return instance()->FileCardSelectionColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & StartParams::defaultFileCardSelectionColor() {
    const static unsigned long def = 0x26A26900;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileCardSelectionColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("FileCardSelectionColor",v);
    instance()->FileCardSelectionColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileCardSelectionColor() {
    instance()->handle->RemoveUnsigned("FileCardSelectionColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileThumbnailBackgroundColor() {
    return QT_TRANSLATE_NOOP("StartParams",
"Background colour of the file thumbnails on the Start page. Only\n"
"used while no style sheet is loaded. Takes effect at the next\n"
"repaint.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & StartParams::getFileThumbnailBackgroundColor() {
    return instance()->FileThumbnailBackgroundColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & StartParams::defaultFileThumbnailBackgroundColor() {
    const static unsigned long def = 0xDDDDDD00;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileThumbnailBackgroundColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("FileThumbnailBackgroundColor",v);
    instance()->FileThumbnailBackgroundColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileThumbnailBackgroundColor() {
    instance()->handle->RemoveUnsigned("FileThumbnailBackgroundColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileThumbnailBorderColor() {
    return QT_TRANSLATE_NOOP("StartParams",
"Border colour of a file thumbnail under the mouse pointer on the\n"
"Start page. Only used while no style sheet is loaded. Takes effect\n"
"at the next repaint.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & StartParams::getFileThumbnailBorderColor() {
    return instance()->FileThumbnailBorderColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & StartParams::defaultFileThumbnailBorderColor() {
    const static unsigned long def = 0x62A0EA00;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileThumbnailBorderColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("FileThumbnailBorderColor",v);
    instance()->FileThumbnailBorderColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileThumbnailBorderColor() {
    instance()->handle->RemoveUnsigned("FileThumbnailBorderColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *StartParams::docFileThumbnailSelectionColor() {
    return QT_TRANSLATE_NOOP("StartParams",
"Border colour of the selected file thumbnail on the Start page.\n"
"Only used while no style sheet is loaded. Takes effect at the next\n"
"repaint.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & StartParams::getFileThumbnailSelectionColor() {
    return instance()->FileThumbnailSelectionColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & StartParams::defaultFileThumbnailSelectionColor() {
    const static unsigned long def = 0x26A26900;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void StartParams::setFileThumbnailSelectionColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("FileThumbnailSelectionColor",v);
    instance()->FileThumbnailSelectionColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void StartParams::removeFileThumbnailSelectionColor() {
    instance()->handle->RemoveUnsigned("FileThumbnailSelectionColor");
}
//[[[end]]]
