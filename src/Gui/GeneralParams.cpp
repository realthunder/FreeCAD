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
import GeneralParams
GeneralParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "GeneralParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class GeneralParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(GeneralParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("Language");
        signalParamChanged("UseLocaleFormatting");
        signalParamChanged("SubstituteDecimalSeparator");
        signalParamChanged("EnableCursorBlinking");
        signalParamChanged("ShowSplasher");
        signalParamChanged("ShowSplasherMessages");
        signalParamChanged("ShowVersionInTitle");
        signalParamChanged("AutoApplyPreference");
        signalParamChanged("SaveUserParameter");
        signalParamChanged("ToolbarIconSize");
        signalParamChanged("WorkbenchTabIconSize");
        signalParamChanged("WorkbenchComboIconSize");
        signalParamChanged("StatusBarIconSize");
        signalParamChanged("MenuBarIconSize");
        signalParamChanged("LockTitleToolBars");
        signalParamChanged("ComboBoxWheelEventFilter");
        signalParamChanged("AutoloadModule");
        signalParamChanged("BackgroundAutoloadModules");
        signalParamChanged("ExportDefaultFilenameSingle");
        signalParamChanged("ExportDefaultFilenameMultiple");
        signalParamChanged("RecentIncludesImported");
        signalParamChanged("RecentIncludesExported");
        signalParamChanged("DownloadPath");
        signalParamChanged("AutoloadTab");
        signalParamChanged("ProgressDetailLevels");
        signalParamChanged("PreferXcbOnWsl");
        signalParamChanged("AdditionalLanguageDomainEntries");
        signalParamChanged("AdditionalTranslationsDirectory");
        signalParamChanged("TempPath");
        signalParamChanged("LastModule");
        signalParamChanged("FileOpenSavePath");
        signalParamChanged("FileImportFilter");
        signalParamChanged("FileExportFilter");
        signalParamChanged("OffscreenImageFormat");
        signalParamChanged("OffscreenImageBackground");
        signalParamChanged("ConfirmAll");
        signalParamChanged("ObjectSelectionAutoDeps");
        signalParamChanged("ObjectSelectionShowDeps");
        signalParamChanged("UserEditMode");

    // Auto generated code (Tools/params_utils.py:241)
    }
    std::string Language;
    long UseLocaleFormatting;
    bool SubstituteDecimalSeparator;
    bool EnableCursorBlinking;
    bool ShowSplasher;
    bool ShowSplasherMessages;
    bool ShowVersionInTitle;
    bool AutoApplyPreference;
    bool SaveUserParameter;
    long ToolbarIconSize;
    long WorkbenchTabIconSize;
    long WorkbenchComboIconSize;
    long StatusBarIconSize;
    long MenuBarIconSize;
    bool LockTitleToolBars;
    bool ComboBoxWheelEventFilter;
    std::string AutoloadModule;
    std::string BackgroundAutoloadModules;
    std::string ExportDefaultFilenameSingle;
    std::string ExportDefaultFilenameMultiple;
    bool RecentIncludesImported;
    bool RecentIncludesExported;
    std::string DownloadPath;
    long AutoloadTab;
    long ProgressDetailLevels;
    bool PreferXcbOnWsl;
    std::string AdditionalLanguageDomainEntries;
    std::string AdditionalTranslationsDirectory;
    std::string TempPath;
    std::string LastModule;
    std::string FileOpenSavePath;
    std::string FileImportFilter;
    std::string FileExportFilter;
    std::string OffscreenImageFormat;
    long OffscreenImageBackground;
    bool ConfirmAll;
    bool ObjectSelectionAutoDeps;
    bool ObjectSelectionShowDeps;
    long UserEditMode;

    // Auto generated code (Tools/params_utils.py:254)
    GeneralParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/General");
        handle->Attach(this);

        Language = this->handle->GetASCII("Language", "");
        funcs["Language"] = &GeneralParamsP::updateLanguage;
        UseLocaleFormatting = this->handle->GetInt("UseLocaleFormatting", 0);
        funcs["UseLocaleFormatting"] = &GeneralParamsP::updateUseLocaleFormatting;
        SubstituteDecimalSeparator = this->handle->GetBool("SubstituteDecimalSeparator", false);
        funcs["SubstituteDecimalSeparator"] = &GeneralParamsP::updateSubstituteDecimalSeparator;
        EnableCursorBlinking = this->handle->GetBool("EnableCursorBlinking", true);
        funcs["EnableCursorBlinking"] = &GeneralParamsP::updateEnableCursorBlinking;
        ShowSplasher = this->handle->GetBool("ShowSplasher", true);
        funcs["ShowSplasher"] = &GeneralParamsP::updateShowSplasher;
        ShowSplasherMessages = this->handle->GetBool("ShowSplasherMessages", true);
        funcs["ShowSplasherMessages"] = &GeneralParamsP::updateShowSplasherMessages;
        ShowVersionInTitle = this->handle->GetBool("ShowVersionInTitle", true);
        funcs["ShowVersionInTitle"] = &GeneralParamsP::updateShowVersionInTitle;
        AutoApplyPreference = this->handle->GetBool("AutoApplyPreference", true);
        funcs["AutoApplyPreference"] = &GeneralParamsP::updateAutoApplyPreference;
        SaveUserParameter = this->handle->GetBool("SaveUserParameter", true);
        funcs["SaveUserParameter"] = &GeneralParamsP::updateSaveUserParameter;
        ToolbarIconSize = this->handle->GetInt("ToolbarIconSize", 24);
        funcs["ToolbarIconSize"] = &GeneralParamsP::updateToolbarIconSize;
        WorkbenchTabIconSize = this->handle->GetInt("WorkbenchTabIconSize", 0);
        funcs["WorkbenchTabIconSize"] = &GeneralParamsP::updateWorkbenchTabIconSize;
        WorkbenchComboIconSize = this->handle->GetInt("WorkbenchComboIconSize", 0);
        funcs["WorkbenchComboIconSize"] = &GeneralParamsP::updateWorkbenchComboIconSize;
        StatusBarIconSize = this->handle->GetInt("StatusBarIconSize", 0);
        funcs["StatusBarIconSize"] = &GeneralParamsP::updateStatusBarIconSize;
        MenuBarIconSize = this->handle->GetInt("MenuBarIconSize", 0);
        funcs["MenuBarIconSize"] = &GeneralParamsP::updateMenuBarIconSize;
        LockTitleToolBars = this->handle->GetBool("LockTitleToolBars", false);
        funcs["LockTitleToolBars"] = &GeneralParamsP::updateLockTitleToolBars;
        ComboBoxWheelEventFilter = this->handle->GetBool("ComboBoxWheelEventFilter", false);
        funcs["ComboBoxWheelEventFilter"] = &GeneralParamsP::updateComboBoxWheelEventFilter;
        AutoloadModule = this->handle->GetASCII("AutoloadModule", "");
        funcs["AutoloadModule"] = &GeneralParamsP::updateAutoloadModule;
        BackgroundAutoloadModules = this->handle->GetASCII("BackgroundAutoloadModules", "");
        funcs["BackgroundAutoloadModules"] = &GeneralParamsP::updateBackgroundAutoloadModules;
        ExportDefaultFilenameSingle = this->handle->GetASCII("ExportDefaultFilenameSingle", "%F-%P-");
        funcs["ExportDefaultFilenameSingle"] = &GeneralParamsP::updateExportDefaultFilenameSingle;
        ExportDefaultFilenameMultiple = this->handle->GetASCII("ExportDefaultFilenameMultiple", "%F");
        funcs["ExportDefaultFilenameMultiple"] = &GeneralParamsP::updateExportDefaultFilenameMultiple;
        RecentIncludesImported = this->handle->GetBool("RecentIncludesImported", true);
        funcs["RecentIncludesImported"] = &GeneralParamsP::updateRecentIncludesImported;
        RecentIncludesExported = this->handle->GetBool("RecentIncludesExported", false);
        funcs["RecentIncludesExported"] = &GeneralParamsP::updateRecentIncludesExported;
        DownloadPath = this->handle->GetASCII("DownloadPath", "");
        funcs["DownloadPath"] = &GeneralParamsP::updateDownloadPath;
        AutoloadTab = this->handle->GetInt("AutoloadTab", 0);
        funcs["AutoloadTab"] = &GeneralParamsP::updateAutoloadTab;
        ProgressDetailLevels = this->handle->GetInt("ProgressDetailLevels", 5);
        funcs["ProgressDetailLevels"] = &GeneralParamsP::updateProgressDetailLevels;
        PreferXcbOnWsl = this->handle->GetBool("PreferXcbOnWsl", true);
        funcs["PreferXcbOnWsl"] = &GeneralParamsP::updatePreferXcbOnWsl;
        AdditionalLanguageDomainEntries = this->handle->GetASCII("AdditionalLanguageDomainEntries", "");
        funcs["AdditionalLanguageDomainEntries"] = &GeneralParamsP::updateAdditionalLanguageDomainEntries;
        AdditionalTranslationsDirectory = this->handle->GetASCII("AdditionalTranslationsDirectory", "");
        funcs["AdditionalTranslationsDirectory"] = &GeneralParamsP::updateAdditionalTranslationsDirectory;
        TempPath = this->handle->GetASCII("TempPath", "");
        funcs["TempPath"] = &GeneralParamsP::updateTempPath;
        LastModule = this->handle->GetASCII("LastModule", "");
        funcs["LastModule"] = &GeneralParamsP::updateLastModule;
        FileOpenSavePath = this->handle->GetASCII("FileOpenSavePath", "");
        funcs["FileOpenSavePath"] = &GeneralParamsP::updateFileOpenSavePath;
        FileImportFilter = this->handle->GetASCII("FileImportFilter", "");
        funcs["FileImportFilter"] = &GeneralParamsP::updateFileImportFilter;
        FileExportFilter = this->handle->GetASCII("FileExportFilter", "");
        funcs["FileExportFilter"] = &GeneralParamsP::updateFileExportFilter;
        OffscreenImageFormat = this->handle->GetASCII("OffscreenImageFormat", "");
        funcs["OffscreenImageFormat"] = &GeneralParamsP::updateOffscreenImageFormat;
        OffscreenImageBackground = this->handle->GetInt("OffscreenImageBackground", 0);
        funcs["OffscreenImageBackground"] = &GeneralParamsP::updateOffscreenImageBackground;
        ConfirmAll = this->handle->GetBool("ConfirmAll", false);
        funcs["ConfirmAll"] = &GeneralParamsP::updateConfirmAll;
        ObjectSelectionAutoDeps = this->handle->GetBool("ObjectSelectionAutoDeps", true);
        funcs["ObjectSelectionAutoDeps"] = &GeneralParamsP::updateObjectSelectionAutoDeps;
        ObjectSelectionShowDeps = this->handle->GetBool("ObjectSelectionShowDeps", false);
        funcs["ObjectSelectionShowDeps"] = &GeneralParamsP::updateObjectSelectionShowDeps;
        UserEditMode = this->handle->GetInt("UserEditMode", 0);
        funcs["UserEditMode"] = &GeneralParamsP::updateUserEditMode;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~GeneralParamsP() override = default;

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
    static void updateLanguage(GeneralParamsP *self) {
        self->Language = self->handle->GetASCII("Language", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseLocaleFormatting(GeneralParamsP *self) {
        self->UseLocaleFormatting = self->handle->GetInt("UseLocaleFormatting", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSubstituteDecimalSeparator(GeneralParamsP *self) {
        self->SubstituteDecimalSeparator = self->handle->GetBool("SubstituteDecimalSeparator", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableCursorBlinking(GeneralParamsP *self) {
        self->EnableCursorBlinking = self->handle->GetBool("EnableCursorBlinking", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowSplasher(GeneralParamsP *self) {
        self->ShowSplasher = self->handle->GetBool("ShowSplasher", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowSplasherMessages(GeneralParamsP *self) {
        self->ShowSplasherMessages = self->handle->GetBool("ShowSplasherMessages", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowVersionInTitle(GeneralParamsP *self) {
        self->ShowVersionInTitle = self->handle->GetBool("ShowVersionInTitle", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoApplyPreference(GeneralParamsP *self) {
        self->AutoApplyPreference = self->handle->GetBool("AutoApplyPreference", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSaveUserParameter(GeneralParamsP *self) {
        self->SaveUserParameter = self->handle->GetBool("SaveUserParameter", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateToolbarIconSize(GeneralParamsP *self) {
        self->ToolbarIconSize = self->handle->GetInt("ToolbarIconSize", 24);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWorkbenchTabIconSize(GeneralParamsP *self) {
        self->WorkbenchTabIconSize = self->handle->GetInt("WorkbenchTabIconSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWorkbenchComboIconSize(GeneralParamsP *self) {
        self->WorkbenchComboIconSize = self->handle->GetInt("WorkbenchComboIconSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStatusBarIconSize(GeneralParamsP *self) {
        self->StatusBarIconSize = self->handle->GetInt("StatusBarIconSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMenuBarIconSize(GeneralParamsP *self) {
        self->MenuBarIconSize = self->handle->GetInt("MenuBarIconSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLockTitleToolBars(GeneralParamsP *self) {
        self->LockTitleToolBars = self->handle->GetBool("LockTitleToolBars", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateComboBoxWheelEventFilter(GeneralParamsP *self) {
        self->ComboBoxWheelEventFilter = self->handle->GetBool("ComboBoxWheelEventFilter", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoloadModule(GeneralParamsP *self) {
        self->AutoloadModule = self->handle->GetASCII("AutoloadModule", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBackgroundAutoloadModules(GeneralParamsP *self) {
        self->BackgroundAutoloadModules = self->handle->GetASCII("BackgroundAutoloadModules", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExportDefaultFilenameSingle(GeneralParamsP *self) {
        self->ExportDefaultFilenameSingle = self->handle->GetASCII("ExportDefaultFilenameSingle", "%F-%P-");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExportDefaultFilenameMultiple(GeneralParamsP *self) {
        self->ExportDefaultFilenameMultiple = self->handle->GetASCII("ExportDefaultFilenameMultiple", "%F");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRecentIncludesImported(GeneralParamsP *self) {
        self->RecentIncludesImported = self->handle->GetBool("RecentIncludesImported", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRecentIncludesExported(GeneralParamsP *self) {
        self->RecentIncludesExported = self->handle->GetBool("RecentIncludesExported", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDownloadPath(GeneralParamsP *self) {
        self->DownloadPath = self->handle->GetASCII("DownloadPath", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoloadTab(GeneralParamsP *self) {
        self->AutoloadTab = self->handle->GetInt("AutoloadTab", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateProgressDetailLevels(GeneralParamsP *self) {
        self->ProgressDetailLevels = self->handle->GetInt("ProgressDetailLevels", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreferXcbOnWsl(GeneralParamsP *self) {
        self->PreferXcbOnWsl = self->handle->GetBool("PreferXcbOnWsl", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAdditionalLanguageDomainEntries(GeneralParamsP *self) {
        self->AdditionalLanguageDomainEntries = self->handle->GetASCII("AdditionalLanguageDomainEntries", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAdditionalTranslationsDirectory(GeneralParamsP *self) {
        self->AdditionalTranslationsDirectory = self->handle->GetASCII("AdditionalTranslationsDirectory", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTempPath(GeneralParamsP *self) {
        self->TempPath = self->handle->GetASCII("TempPath", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLastModule(GeneralParamsP *self) {
        self->LastModule = self->handle->GetASCII("LastModule", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileOpenSavePath(GeneralParamsP *self) {
        self->FileOpenSavePath = self->handle->GetASCII("FileOpenSavePath", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileImportFilter(GeneralParamsP *self) {
        self->FileImportFilter = self->handle->GetASCII("FileImportFilter", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileExportFilter(GeneralParamsP *self) {
        self->FileExportFilter = self->handle->GetASCII("FileExportFilter", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOffscreenImageFormat(GeneralParamsP *self) {
        self->OffscreenImageFormat = self->handle->GetASCII("OffscreenImageFormat", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOffscreenImageBackground(GeneralParamsP *self) {
        self->OffscreenImageBackground = self->handle->GetInt("OffscreenImageBackground", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateConfirmAll(GeneralParamsP *self) {
        self->ConfirmAll = self->handle->GetBool("ConfirmAll", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateObjectSelectionAutoDeps(GeneralParamsP *self) {
        self->ObjectSelectionAutoDeps = self->handle->GetBool("ObjectSelectionAutoDeps", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateObjectSelectionShowDeps(GeneralParamsP *self) {
        self->ObjectSelectionShowDeps = self->handle->GetBool("ObjectSelectionShowDeps", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUserEditMode(GeneralParamsP *self) {
        self->UserEditMode = self->handle->GetInt("UserEditMode", 0);
    }
};

// Auto generated code (Tools/params_utils.py:336)
GeneralParamsP *instance() {
    static GeneralParamsP *inst = new GeneralParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _GeneralParamsRegistrar({
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "Language", "Language", App::ParamInfo::String, "")
        .setTitle("Language")
        .setDoc("User interface language, as its English name (English, German,\n"
"...). Empty or not set uses the system's language. A change\n"
"retranslates the interface at once."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "UseLocaleFormatting", "UseLocaleFormatting", App::ParamInfo::Int, 0)
        .setTitle("Number format")
        .setDoc("Decimal and group separators of numbers: 0 the operating system's,\n"
"1 those of the interface language, 2 C/POSIX. Takes effect shortly\n"
"after a change."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "SubstituteDecimalSeparator", "SubstituteDecimalSeparator", App::ParamInfo::Bool, false)
        .setTitle("Substitute decimal separator")
        .setDoc("Type the locale's decimal separator with the decimal key of the\n"
"numeric keypad, whatever the keyboard layout sends for it."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "EnableCursorBlinking", "EnableCursorBlinking", App::ParamInfo::Bool, true)
        .setTitle("Text cursor blinking")
        .setDoc("Blink the text cursor in text and number input fields. When off it\n"
"stays steady. Takes effect at once."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ShowSplasher", "ShowSplasher", App::ParamInfo::Bool, true)
        .setTitle("Show splash screen")
        .setDoc("Show the splash screen while the program starts. Takes effect at\n"
"the next start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ShowSplasherMessages", "ShowSplasherMessages", App::ParamInfo::Bool, true)
        .setTitle("Show splash screen messages")
        .setDoc("Show the loading messages on the splash screen. When off it shows\n"
"its image only. Takes effect at the next start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ShowVersionInTitle", "ShowVersionInTitle", App::ParamInfo::Bool, true)
        .setTitle("Show version in title")
        .setDoc("Show the version number after the application name in the main\n"
"window's title. Takes effect at the next start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "AutoApplyPreference", "AutoApplyPreference", App::ParamInfo::Bool, true)
        .setTitle("Apply preferences at once")
        .setDoc("Apply each change made in the Preferences dialog as it is made,\n"
"without Apply or OK. When off a change is stored when Apply or OK\n"
"is pressed."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "SaveUserParameter", "SaveUserParameter", App::ParamInfo::Bool, true)
        .setTitle("Save settings on change")
        .setDoc("Write the settings file when the Preferences dialog is accepted and\n"
"when the list of recent files or of recent macros changes. When\n"
"off those do not write it."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ToolbarIconSize", "ToolbarIconSize", App::ParamInfo::Int, 24)
        .setTitle("Tool bar icon size")
        .setDoc("Size of tool bar icons in pixels, at least 5. The icons of the menu\n"
"bar, the status bar and the workbench selector follow it unless\n"
"given a size of their own. Takes effect shortly after a change."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "WorkbenchTabIconSize", "WorkbenchTabIconSize", App::ParamInfo::Int, 0)
        .setTitle("Workbench tab icon size")
        .setDoc("Icon size in pixels of the workbench tab bar. 0 uses the tool bar\n"
"icon size."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "WorkbenchComboIconSize", "WorkbenchComboIconSize", App::ParamInfo::Int, 0)
        .setTitle("Workbench selector icon size")
        .setDoc("Icon size in pixels of the workbench selector's combo box. 0 uses\n"
"0.8 times the tool bar icon size."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "StatusBarIconSize", "StatusBarIconSize", App::ParamInfo::Int, 0)
        .setTitle("Status bar icon size")
        .setDoc("Icon size in pixels of tool bars placed in the status bar. 0 uses\n"
"0.6 times the tool bar icon size."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "MenuBarIconSize", "MenuBarIconSize", App::ParamInfo::Int, 0)
        .setTitle("Menu bar icon size")
        .setDoc("Icon size in pixels of tool bars placed beside the menu bar. 0 uses\n"
"0.8 times the tool bar icon size, or all of it with the custom\n"
"title bar."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "LockTitleToolBars", "LockTitleToolBars", App::ParamInfo::Bool, false)
        .setTitle("Lock menu and status bar tool bars")
        .setDoc("Keep the tool bars docked in the menu bar and in the status bar\n"
"from being dragged out. Set from the tool bar lock menu."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ComboBoxWheelEventFilter", "ComboBoxWheelEventFilter", App::ParamInfo::Bool, false)
        .setTitle("Ignore wheel over unfocused inputs")
        .setDoc("Ignore the mouse wheel over combo boxes and over spin boxes that\n"
"do not have the keyboard focus, so that scrolling a panel does not\n"
"change values. Takes effect at the next start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "AutoloadModule", "AutoloadModule", App::ParamInfo::String, "")
        .setTitle("Start up workbench")
        .setDoc("Workbench activated when the program starts, by its name, or\n"
"$LastModule for the one that was active last. Empty or not set\n"
"uses the configured start workbench. Takes effect at the next\n"
"start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "BackgroundAutoloadModules", "BackgroundAutoloadModules", App::ParamInfo::String, "")
        .setTitle("Workbenches loaded at start")
        .setDoc("Comma separated names of the workbenches loaded in the background\n"
"at start, without being shown. Takes effect at the next start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ExportDefaultFilenameSingle", "ExportDefaultFilenameSingle", App::ParamInfo::String, "%F-%P-")
        .setTitle("Export file name, one object")
        .setDoc("Pattern of the file name Export offers when one object is\n"
"selected. %F is the document, %Lx the object labels joined by x,\n"
"%Px the parent and object labels joined by x, %U the UTC time, %D\n"
"the local time."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ExportDefaultFilenameMultiple", "ExportDefaultFilenameMultiple", App::ParamInfo::String, "%F")
        .setTitle("Export file name, several objects")
        .setDoc("Pattern of the file name Export offers when the selection is not\n"
"exactly one object. The codes are those of the pattern for one\n"
"object: %F, %Lx, %Px, %U, %D."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "RecentIncludesImported", "RecentIncludesImported", App::ParamInfo::Bool, true)
        .setTitle("Recent files include imports")
        .setDoc("Add files opened with Import to the list of recent files."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "RecentIncludesExported", "RecentIncludesExported", App::ParamInfo::Bool, false)
        .setTitle("Recent files include exports")
        .setDoc("Add exported files to the list of recent files, where some module\n"
"can open that type of file."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "DownloadPath", "DownloadPath", App::ParamInfo::String, "")
        .setTitle("Download directory")
        .setDoc("Directory downloaded files are saved in. Empty uses a folder named\n"
"after the program inside the user's Documents folder."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "AutoloadTab", "AutoloadTab", App::ParamInfo::Int, 0)
        .setTitle("Report view first tab")
        .setDoc("Index of the tab the combined report view shows first. Read when\n"
"the view is made."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ProgressDetailLevels", "ProgressDetailLevels", App::ParamInfo::Int, 5)
        .setTitle("Progress detail levels")
        .setDoc("How many nested levels of progress per thread the progress\n"
"detail popup shows. Less than 1 counts as 1."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "PreferXcbOnWsl", "PreferXcbOnWsl", App::ParamInfo::Bool, true)
        .setTitle("Prefer X11 under WSL")
        .setDoc("Under WSL with both Wayland and X11 at hand, and QT_QPA_PLATFORM\n"
"not set, start on X11 (xcb) instead of Wayland. Takes effect at\n"
"the next start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "AdditionalLanguageDomainEntries", "AdditionalLanguageDomainEntries", App::ParamInfo::String, "")
        .setTitle("Additional languages")
        .setDoc("Interface languages added to the built-in table, as pairs\n"
"\"Language Name\"=\"code\"; one after the other. Takes effect at\n"
"the next start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "AdditionalTranslationsDirectory", "AdditionalTranslationsDirectory", App::ParamInfo::String, "")
        .setTitle("Additional translations directory")
        .setDoc("Directory searched for translation files ahead of the user's and\n"
"the installation's own. Takes effect at the next start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "TempPath", "TempPath", App::ParamInfo::String, "")
        .setTitle("Temporary files directory")
        .setDoc("Directory used for temporary files instead of the system's, when\n"
"it exists. Takes effect at the next start."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "LastModule", "LastModule", App::ParamInfo::String, "")
        .setTitle("Last workbench")
        .setDoc("The workbench that was active when the program last closed,\n"
"which the next session starts in while the start-up workbench\n"
"is set to the last one used. Stored by the program; empty until\n"
"then, when the start workbench is used."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "FileOpenSavePath", "FileOpenSavePath", App::ParamInfo::String, "")
        .setTitle("Last folder of the file dialogs")
        .setDoc("The folder the file dialogs were last in, which they open on.\n"
"Stored by the program; empty until then, when the home folder\n"
"is used."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "FileImportFilter", "FileImportFilter", App::ParamInfo::String, "")
        .setTitle("Last file type of the Import dialog")
        .setDoc("The file type last chosen in the Import dialog, which it opens\n"
"with. Stored by the program."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "FileExportFilter", "FileExportFilter", App::ParamInfo::String, "")
        .setTitle("Last file type of the Export dialog")
        .setDoc("The file type last chosen in the Export dialog, which it opens\n"
"with. Stored by the program."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "OffscreenImageFormat", "OffscreenImageFormat", App::ParamInfo::String, "")
        .setTitle("Last format of Save picture")
        .setDoc("The file format last chosen in the Save picture dialog. Stored\n"
"by the program."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "OffscreenImageBackground", "OffscreenImageBackground", App::ParamInfo::Int, 0)
        .setTitle("Last background of Save picture")
        .setDoc("The background last chosen in the options of the Save picture\n"
"dialog, as the number of its entry. Stored by the program."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ConfirmAll", "ConfirmAll", App::ParamInfo::Bool, false)
        .setTitle("Last 'Apply answer to all' of the save question")
        .setDoc("The state the box 'Apply answer to all' was last left in, in the\n"
"question about unsaved documents at closing. Stored by the\n"
"program."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ObjectSelectionAutoDeps", "ObjectSelectionAutoDeps", App::ParamInfo::Bool, true)
        .setTitle("Object selection: auto select dependencies")
        .setDoc("The state the box that selects the dependencies of an object with\n"
"it was last left in, in the object selection dialog. Stored by\n"
"the program."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "ObjectSelectionShowDeps", "ObjectSelectionShowDeps", App::ParamInfo::Bool, false)
        .setTitle("Object selection: show dependencies")
        .setDoc("Whether the object selection dialog last showed its dependency\n"
"lists. Stored by the program."),
    App::ParamInfo("Gui", "GeneralParams", "User parameter:BaseApp/Preferences/General", "UserEditMode", "UserEditMode", App::ParamInfo::Int, 0)
        .setTitle("Edit mode")
        .setDoc("What a double click on an object in the tree does, as the number\n"
"of the entry of Edit > Edit mode: 0 the object's default, 1\n"
"transform, 2 cutting, 3 colour. Stored when the menu is used and\n"
"read at start."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle GeneralParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
GeneralParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void GeneralParams::signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docLanguage() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"User interface language, as its English name (English, German,\n"
"...). Empty or not set uses the system's language. A change\n"
"retranslates the interface at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getLanguage() {
    return instance()->Language;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultLanguage() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setLanguage(const std::string &v) {
    instance()->handle->SetASCII("Language",v);
    instance()->Language = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeLanguage() {
    instance()->handle->RemoveASCII("Language");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docUseLocaleFormatting() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Decimal and group separators of numbers: 0 the operating system's,\n"
"1 those of the interface language, 2 C/POSIX. Takes effect shortly\n"
"after a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getUseLocaleFormatting() {
    return instance()->UseLocaleFormatting;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultUseLocaleFormatting() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setUseLocaleFormatting(const long &v) {
    instance()->handle->SetInt("UseLocaleFormatting",v);
    instance()->UseLocaleFormatting = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeUseLocaleFormatting() {
    instance()->handle->RemoveInt("UseLocaleFormatting");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docSubstituteDecimalSeparator() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Type the locale's decimal separator with the decimal key of the\n"
"numeric keypad, whatever the keyboard layout sends for it.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getSubstituteDecimalSeparator() {
    return instance()->SubstituteDecimalSeparator;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultSubstituteDecimalSeparator() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setSubstituteDecimalSeparator(const bool &v) {
    instance()->handle->SetBool("SubstituteDecimalSeparator",v);
    instance()->SubstituteDecimalSeparator = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeSubstituteDecimalSeparator() {
    instance()->handle->RemoveBool("SubstituteDecimalSeparator");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docEnableCursorBlinking() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Blink the text cursor in text and number input fields. When off it\n"
"stays steady. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getEnableCursorBlinking() {
    return instance()->EnableCursorBlinking;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultEnableCursorBlinking() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setEnableCursorBlinking(const bool &v) {
    instance()->handle->SetBool("EnableCursorBlinking",v);
    instance()->EnableCursorBlinking = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeEnableCursorBlinking() {
    instance()->handle->RemoveBool("EnableCursorBlinking");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docShowSplasher() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Show the splash screen while the program starts. Takes effect at\n"
"the next start.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getShowSplasher() {
    return instance()->ShowSplasher;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultShowSplasher() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setShowSplasher(const bool &v) {
    instance()->handle->SetBool("ShowSplasher",v);
    instance()->ShowSplasher = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeShowSplasher() {
    instance()->handle->RemoveBool("ShowSplasher");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docShowSplasherMessages() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Show the loading messages on the splash screen. When off it shows\n"
"its image only. Takes effect at the next start.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getShowSplasherMessages() {
    return instance()->ShowSplasherMessages;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultShowSplasherMessages() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setShowSplasherMessages(const bool &v) {
    instance()->handle->SetBool("ShowSplasherMessages",v);
    instance()->ShowSplasherMessages = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeShowSplasherMessages() {
    instance()->handle->RemoveBool("ShowSplasherMessages");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docShowVersionInTitle() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Show the version number after the application name in the main\n"
"window's title. Takes effect at the next start.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getShowVersionInTitle() {
    return instance()->ShowVersionInTitle;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultShowVersionInTitle() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setShowVersionInTitle(const bool &v) {
    instance()->handle->SetBool("ShowVersionInTitle",v);
    instance()->ShowVersionInTitle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeShowVersionInTitle() {
    instance()->handle->RemoveBool("ShowVersionInTitle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docAutoApplyPreference() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Apply each change made in the Preferences dialog as it is made,\n"
"without Apply or OK. When off a change is stored when Apply or OK\n"
"is pressed.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getAutoApplyPreference() {
    return instance()->AutoApplyPreference;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultAutoApplyPreference() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setAutoApplyPreference(const bool &v) {
    instance()->handle->SetBool("AutoApplyPreference",v);
    instance()->AutoApplyPreference = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeAutoApplyPreference() {
    instance()->handle->RemoveBool("AutoApplyPreference");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docSaveUserParameter() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Write the settings file when the Preferences dialog is accepted and\n"
"when the list of recent files or of recent macros changes. When\n"
"off those do not write it.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getSaveUserParameter() {
    return instance()->SaveUserParameter;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultSaveUserParameter() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setSaveUserParameter(const bool &v) {
    instance()->handle->SetBool("SaveUserParameter",v);
    instance()->SaveUserParameter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeSaveUserParameter() {
    instance()->handle->RemoveBool("SaveUserParameter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docToolbarIconSize() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Size of tool bar icons in pixels, at least 5. The icons of the menu\n"
"bar, the status bar and the workbench selector follow it unless\n"
"given a size of their own. Takes effect shortly after a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getToolbarIconSize() {
    return instance()->ToolbarIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultToolbarIconSize() {
    const static long def = 24;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setToolbarIconSize(const long &v) {
    instance()->handle->SetInt("ToolbarIconSize",v);
    instance()->ToolbarIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeToolbarIconSize() {
    instance()->handle->RemoveInt("ToolbarIconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docWorkbenchTabIconSize() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Icon size in pixels of the workbench tab bar. 0 uses the tool bar\n"
"icon size.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getWorkbenchTabIconSize() {
    return instance()->WorkbenchTabIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultWorkbenchTabIconSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setWorkbenchTabIconSize(const long &v) {
    instance()->handle->SetInt("WorkbenchTabIconSize",v);
    instance()->WorkbenchTabIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeWorkbenchTabIconSize() {
    instance()->handle->RemoveInt("WorkbenchTabIconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docWorkbenchComboIconSize() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Icon size in pixels of the workbench selector's combo box. 0 uses\n"
"0.8 times the tool bar icon size.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getWorkbenchComboIconSize() {
    return instance()->WorkbenchComboIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultWorkbenchComboIconSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setWorkbenchComboIconSize(const long &v) {
    instance()->handle->SetInt("WorkbenchComboIconSize",v);
    instance()->WorkbenchComboIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeWorkbenchComboIconSize() {
    instance()->handle->RemoveInt("WorkbenchComboIconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docStatusBarIconSize() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Icon size in pixels of tool bars placed in the status bar. 0 uses\n"
"0.6 times the tool bar icon size.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getStatusBarIconSize() {
    return instance()->StatusBarIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultStatusBarIconSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setStatusBarIconSize(const long &v) {
    instance()->handle->SetInt("StatusBarIconSize",v);
    instance()->StatusBarIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeStatusBarIconSize() {
    instance()->handle->RemoveInt("StatusBarIconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docMenuBarIconSize() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Icon size in pixels of tool bars placed beside the menu bar. 0 uses\n"
"0.8 times the tool bar icon size, or all of it with the custom\n"
"title bar.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getMenuBarIconSize() {
    return instance()->MenuBarIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultMenuBarIconSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setMenuBarIconSize(const long &v) {
    instance()->handle->SetInt("MenuBarIconSize",v);
    instance()->MenuBarIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeMenuBarIconSize() {
    instance()->handle->RemoveInt("MenuBarIconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docLockTitleToolBars() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Keep the tool bars docked in the menu bar and in the status bar\n"
"from being dragged out. Set from the tool bar lock menu.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getLockTitleToolBars() {
    return instance()->LockTitleToolBars;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultLockTitleToolBars() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setLockTitleToolBars(const bool &v) {
    instance()->handle->SetBool("LockTitleToolBars",v);
    instance()->LockTitleToolBars = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeLockTitleToolBars() {
    instance()->handle->RemoveBool("LockTitleToolBars");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docComboBoxWheelEventFilter() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Ignore the mouse wheel over combo boxes and over spin boxes that\n"
"do not have the keyboard focus, so that scrolling a panel does not\n"
"change values. Takes effect at the next start.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getComboBoxWheelEventFilter() {
    return instance()->ComboBoxWheelEventFilter;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultComboBoxWheelEventFilter() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setComboBoxWheelEventFilter(const bool &v) {
    instance()->handle->SetBool("ComboBoxWheelEventFilter",v);
    instance()->ComboBoxWheelEventFilter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeComboBoxWheelEventFilter() {
    instance()->handle->RemoveBool("ComboBoxWheelEventFilter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docAutoloadModule() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Workbench activated when the program starts, by its name, or\n"
"$LastModule for the one that was active last. Empty or not set\n"
"uses the configured start workbench. Takes effect at the next\n"
"start.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getAutoloadModule() {
    return instance()->AutoloadModule;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultAutoloadModule() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setAutoloadModule(const std::string &v) {
    instance()->handle->SetASCII("AutoloadModule",v);
    instance()->AutoloadModule = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeAutoloadModule() {
    instance()->handle->RemoveASCII("AutoloadModule");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docBackgroundAutoloadModules() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Comma separated names of the workbenches loaded in the background\n"
"at start, without being shown. Takes effect at the next start.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getBackgroundAutoloadModules() {
    return instance()->BackgroundAutoloadModules;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultBackgroundAutoloadModules() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setBackgroundAutoloadModules(const std::string &v) {
    instance()->handle->SetASCII("BackgroundAutoloadModules",v);
    instance()->BackgroundAutoloadModules = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeBackgroundAutoloadModules() {
    instance()->handle->RemoveASCII("BackgroundAutoloadModules");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docExportDefaultFilenameSingle() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Pattern of the file name Export offers when one object is\n"
"selected. %F is the document, %Lx the object labels joined by x,\n"
"%Px the parent and object labels joined by x, %U the UTC time, %D\n"
"the local time.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getExportDefaultFilenameSingle() {
    return instance()->ExportDefaultFilenameSingle;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultExportDefaultFilenameSingle() {
    const static std::string def = "%F-%P-";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setExportDefaultFilenameSingle(const std::string &v) {
    instance()->handle->SetASCII("ExportDefaultFilenameSingle",v);
    instance()->ExportDefaultFilenameSingle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeExportDefaultFilenameSingle() {
    instance()->handle->RemoveASCII("ExportDefaultFilenameSingle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docExportDefaultFilenameMultiple() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Pattern of the file name Export offers when the selection is not\n"
"exactly one object. The codes are those of the pattern for one\n"
"object: %F, %Lx, %Px, %U, %D.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getExportDefaultFilenameMultiple() {
    return instance()->ExportDefaultFilenameMultiple;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultExportDefaultFilenameMultiple() {
    const static std::string def = "%F";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setExportDefaultFilenameMultiple(const std::string &v) {
    instance()->handle->SetASCII("ExportDefaultFilenameMultiple",v);
    instance()->ExportDefaultFilenameMultiple = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeExportDefaultFilenameMultiple() {
    instance()->handle->RemoveASCII("ExportDefaultFilenameMultiple");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docRecentIncludesImported() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Add files opened with Import to the list of recent files.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getRecentIncludesImported() {
    return instance()->RecentIncludesImported;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultRecentIncludesImported() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setRecentIncludesImported(const bool &v) {
    instance()->handle->SetBool("RecentIncludesImported",v);
    instance()->RecentIncludesImported = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeRecentIncludesImported() {
    instance()->handle->RemoveBool("RecentIncludesImported");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docRecentIncludesExported() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Add exported files to the list of recent files, where some module\n"
"can open that type of file.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getRecentIncludesExported() {
    return instance()->RecentIncludesExported;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultRecentIncludesExported() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setRecentIncludesExported(const bool &v) {
    instance()->handle->SetBool("RecentIncludesExported",v);
    instance()->RecentIncludesExported = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeRecentIncludesExported() {
    instance()->handle->RemoveBool("RecentIncludesExported");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docDownloadPath() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Directory downloaded files are saved in. Empty uses a folder named\n"
"after the program inside the user's Documents folder.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getDownloadPath() {
    return instance()->DownloadPath;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultDownloadPath() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setDownloadPath(const std::string &v) {
    instance()->handle->SetASCII("DownloadPath",v);
    instance()->DownloadPath = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeDownloadPath() {
    instance()->handle->RemoveASCII("DownloadPath");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docAutoloadTab() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Index of the tab the combined report view shows first. Read when\n"
"the view is made.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getAutoloadTab() {
    return instance()->AutoloadTab;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultAutoloadTab() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setAutoloadTab(const long &v) {
    instance()->handle->SetInt("AutoloadTab",v);
    instance()->AutoloadTab = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeAutoloadTab() {
    instance()->handle->RemoveInt("AutoloadTab");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docProgressDetailLevels() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"How many nested levels of progress per thread the progress\n"
"detail popup shows. Less than 1 counts as 1.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getProgressDetailLevels() {
    return instance()->ProgressDetailLevels;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultProgressDetailLevels() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setProgressDetailLevels(const long &v) {
    instance()->handle->SetInt("ProgressDetailLevels",v);
    instance()->ProgressDetailLevels = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeProgressDetailLevels() {
    instance()->handle->RemoveInt("ProgressDetailLevels");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docPreferXcbOnWsl() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Under WSL with both Wayland and X11 at hand, and QT_QPA_PLATFORM\n"
"not set, start on X11 (xcb) instead of Wayland. Takes effect at\n"
"the next start.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getPreferXcbOnWsl() {
    return instance()->PreferXcbOnWsl;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultPreferXcbOnWsl() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setPreferXcbOnWsl(const bool &v) {
    instance()->handle->SetBool("PreferXcbOnWsl",v);
    instance()->PreferXcbOnWsl = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removePreferXcbOnWsl() {
    instance()->handle->RemoveBool("PreferXcbOnWsl");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docAdditionalLanguageDomainEntries() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Interface languages added to the built-in table, as pairs\n"
"\"Language Name\"=\"code\"; one after the other. Takes effect at\n"
"the next start.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getAdditionalLanguageDomainEntries() {
    return instance()->AdditionalLanguageDomainEntries;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultAdditionalLanguageDomainEntries() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setAdditionalLanguageDomainEntries(const std::string &v) {
    instance()->handle->SetASCII("AdditionalLanguageDomainEntries",v);
    instance()->AdditionalLanguageDomainEntries = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeAdditionalLanguageDomainEntries() {
    instance()->handle->RemoveASCII("AdditionalLanguageDomainEntries");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docAdditionalTranslationsDirectory() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Directory searched for translation files ahead of the user's and\n"
"the installation's own. Takes effect at the next start.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getAdditionalTranslationsDirectory() {
    return instance()->AdditionalTranslationsDirectory;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultAdditionalTranslationsDirectory() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setAdditionalTranslationsDirectory(const std::string &v) {
    instance()->handle->SetASCII("AdditionalTranslationsDirectory",v);
    instance()->AdditionalTranslationsDirectory = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeAdditionalTranslationsDirectory() {
    instance()->handle->RemoveASCII("AdditionalTranslationsDirectory");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docTempPath() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Directory used for temporary files instead of the system's, when\n"
"it exists. Takes effect at the next start.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getTempPath() {
    return instance()->TempPath;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultTempPath() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setTempPath(const std::string &v) {
    instance()->handle->SetASCII("TempPath",v);
    instance()->TempPath = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeTempPath() {
    instance()->handle->RemoveASCII("TempPath");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docLastModule() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"The workbench that was active when the program last closed,\n"
"which the next session starts in while the start-up workbench\n"
"is set to the last one used. Stored by the program; empty until\n"
"then, when the start workbench is used.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getLastModule() {
    return instance()->LastModule;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultLastModule() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setLastModule(const std::string &v) {
    instance()->handle->SetASCII("LastModule",v);
    instance()->LastModule = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeLastModule() {
    instance()->handle->RemoveASCII("LastModule");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docFileOpenSavePath() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"The folder the file dialogs were last in, which they open on.\n"
"Stored by the program; empty until then, when the home folder\n"
"is used.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getFileOpenSavePath() {
    return instance()->FileOpenSavePath;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultFileOpenSavePath() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setFileOpenSavePath(const std::string &v) {
    instance()->handle->SetASCII("FileOpenSavePath",v);
    instance()->FileOpenSavePath = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeFileOpenSavePath() {
    instance()->handle->RemoveASCII("FileOpenSavePath");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docFileImportFilter() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"The file type last chosen in the Import dialog, which it opens\n"
"with. Stored by the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getFileImportFilter() {
    return instance()->FileImportFilter;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultFileImportFilter() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setFileImportFilter(const std::string &v) {
    instance()->handle->SetASCII("FileImportFilter",v);
    instance()->FileImportFilter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeFileImportFilter() {
    instance()->handle->RemoveASCII("FileImportFilter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docFileExportFilter() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"The file type last chosen in the Export dialog, which it opens\n"
"with. Stored by the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getFileExportFilter() {
    return instance()->FileExportFilter;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultFileExportFilter() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setFileExportFilter(const std::string &v) {
    instance()->handle->SetASCII("FileExportFilter",v);
    instance()->FileExportFilter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeFileExportFilter() {
    instance()->handle->RemoveASCII("FileExportFilter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docOffscreenImageFormat() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"The file format last chosen in the Save picture dialog. Stored\n"
"by the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & GeneralParams::getOffscreenImageFormat() {
    return instance()->OffscreenImageFormat;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & GeneralParams::defaultOffscreenImageFormat() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setOffscreenImageFormat(const std::string &v) {
    instance()->handle->SetASCII("OffscreenImageFormat",v);
    instance()->OffscreenImageFormat = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeOffscreenImageFormat() {
    instance()->handle->RemoveASCII("OffscreenImageFormat");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docOffscreenImageBackground() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"The background last chosen in the options of the Save picture\n"
"dialog, as the number of its entry. Stored by the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getOffscreenImageBackground() {
    return instance()->OffscreenImageBackground;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultOffscreenImageBackground() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setOffscreenImageBackground(const long &v) {
    instance()->handle->SetInt("OffscreenImageBackground",v);
    instance()->OffscreenImageBackground = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeOffscreenImageBackground() {
    instance()->handle->RemoveInt("OffscreenImageBackground");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docConfirmAll() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"The state the box 'Apply answer to all' was last left in, in the\n"
"question about unsaved documents at closing. Stored by the\n"
"program.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getConfirmAll() {
    return instance()->ConfirmAll;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultConfirmAll() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setConfirmAll(const bool &v) {
    instance()->handle->SetBool("ConfirmAll",v);
    instance()->ConfirmAll = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeConfirmAll() {
    instance()->handle->RemoveBool("ConfirmAll");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docObjectSelectionAutoDeps() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"The state the box that selects the dependencies of an object with\n"
"it was last left in, in the object selection dialog. Stored by\n"
"the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getObjectSelectionAutoDeps() {
    return instance()->ObjectSelectionAutoDeps;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultObjectSelectionAutoDeps() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setObjectSelectionAutoDeps(const bool &v) {
    instance()->handle->SetBool("ObjectSelectionAutoDeps",v);
    instance()->ObjectSelectionAutoDeps = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeObjectSelectionAutoDeps() {
    instance()->handle->RemoveBool("ObjectSelectionAutoDeps");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docObjectSelectionShowDeps() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"Whether the object selection dialog last showed its dependency\n"
"lists. Stored by the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GeneralParams::getObjectSelectionShowDeps() {
    return instance()->ObjectSelectionShowDeps;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GeneralParams::defaultObjectSelectionShowDeps() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setObjectSelectionShowDeps(const bool &v) {
    instance()->handle->SetBool("ObjectSelectionShowDeps",v);
    instance()->ObjectSelectionShowDeps = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeObjectSelectionShowDeps() {
    instance()->handle->RemoveBool("ObjectSelectionShowDeps");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GeneralParams::docUserEditMode() {
    return QT_TRANSLATE_NOOP("GeneralParams",
"What a double click on an object in the tree does, as the number\n"
"of the entry of Edit > Edit mode: 0 the object's default, 1\n"
"transform, 2 cutting, 3 colour. Stored when the menu is used and\n"
"read at start.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & GeneralParams::getUserEditMode() {
    return instance()->UserEditMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & GeneralParams::defaultUserEditMode() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GeneralParams::setUserEditMode(const long &v) {
    instance()->handle->SetInt("UserEditMode",v);
    instance()->UserEditMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GeneralParams::removeUserEditMode() {
    instance()->handle->RemoveInt("UserEditMode");
}
//[[[end]]]
