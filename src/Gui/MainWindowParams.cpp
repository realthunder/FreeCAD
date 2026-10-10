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
import MainWindowParams
MainWindowParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "MainWindowParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class MainWindowParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(MainWindowParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("ColorScheme");
        signalParamChanged("StyleSheet");
        signalParamChanged("OverlayActiveStyleSheet");
        signalParamChanged("MenuStyleSheet");
        signalParamChanged("IconSet");
        signalParamChanged("ThemeIconSetPolicy");
        signalParamChanged("TiledBackground");
        signalParamChanged("QtStyle");
        signalParamChanged("ThemeAuto");
        signalParamChanged("ThemeStyleParametersFile");
        signalParamChanged("CustomTitleBar");
        signalParamChanged("TitleBarToolBars");
        signalParamChanged("FoldTitleBarMenu");
        signalParamChanged("TitleBarMenuClickGuard");
        signalParamChanged("DefaultToolBarArea");
        signalParamChanged("GlobalToolBarArea");
        signalParamChanged("WSPosition");
        signalParamChanged("DockableWindowShortcut");
        signalParamChanged("ClearMenuBar");
        signalParamChanged("Geometry");
        signalParamChanged("Maximized");
        signalParamChanged("MainWindowState");
        signalParamChanged("StatusBar");
        signalParamChanged("WindowStateRestored");
        signalParamChanged("Theme");
        signalParamChanged("ThemeAutoApplied");

    // Auto generated code (Tools/params_utils.py:241)
    }
    std::string ColorScheme;
    std::string StyleSheet;
    std::string OverlayActiveStyleSheet;
    std::string MenuStyleSheet;
    std::string IconSet;
    std::string ThemeIconSetPolicy;
    bool TiledBackground;
    std::string QtStyle;
    bool ThemeAuto;
    std::string ThemeStyleParametersFile;
    bool CustomTitleBar;
    bool TitleBarToolBars;
    bool FoldTitleBarMenu;
    long TitleBarMenuClickGuard;
    std::string DefaultToolBarArea;
    std::string GlobalToolBarArea;
    std::string WSPosition;
    std::string DockableWindowShortcut;
    bool ClearMenuBar;
    std::string Geometry;
    bool Maximized;
    std::string MainWindowState;
    bool StatusBar;
    bool WindowStateRestored;
    std::string Theme;
    std::string ThemeAutoApplied;

    // Auto generated code (Tools/params_utils.py:254)
    MainWindowParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/MainWindow");
        handle->Attach(this);

        ColorScheme = this->handle->GetASCII("ColorScheme", "Light");
        funcs["ColorScheme"] = &MainWindowParamsP::updateColorScheme;
        StyleSheet = this->handle->GetASCII("StyleSheet", "");
        funcs["StyleSheet"] = &MainWindowParamsP::updateStyleSheet;
        OverlayActiveStyleSheet = this->handle->GetASCII("OverlayActiveStyleSheet", "");
        funcs["OverlayActiveStyleSheet"] = &MainWindowParamsP::updateOverlayActiveStyleSheet;
        MenuStyleSheet = this->handle->GetASCII("MenuStyleSheet", "");
        funcs["MenuStyleSheet"] = &MainWindowParamsP::updateMenuStyleSheet;
        IconSet = this->handle->GetASCII("IconSet", "");
        funcs["IconSet"] = &MainWindowParamsP::updateIconSet;
        ThemeIconSetPolicy = this->handle->GetASCII("ThemeIconSetPolicy", "Reset");
        funcs["ThemeIconSetPolicy"] = &MainWindowParamsP::updateThemeIconSetPolicy;
        TiledBackground = this->handle->GetBool("TiledBackground", false);
        funcs["TiledBackground"] = &MainWindowParamsP::updateTiledBackground;
        QtStyle = this->handle->GetASCII("QtStyle", "");
        funcs["QtStyle"] = &MainWindowParamsP::updateQtStyle;
        ThemeAuto = this->handle->GetBool("ThemeAuto", false);
        funcs["ThemeAuto"] = &MainWindowParamsP::updateThemeAuto;
        ThemeStyleParametersFile = this->handle->GetASCII("ThemeStyleParametersFile", "");
        funcs["ThemeStyleParametersFile"] = &MainWindowParamsP::updateThemeStyleParametersFile;
        CustomTitleBar = this->handle->GetBool("CustomTitleBar", false);
        funcs["CustomTitleBar"] = &MainWindowParamsP::updateCustomTitleBar;
        TitleBarToolBars = this->handle->GetBool("TitleBarToolBars", true);
        funcs["TitleBarToolBars"] = &MainWindowParamsP::updateTitleBarToolBars;
        FoldTitleBarMenu = this->handle->GetBool("FoldTitleBarMenu", true);
        funcs["FoldTitleBarMenu"] = &MainWindowParamsP::updateFoldTitleBarMenu;
        TitleBarMenuClickGuard = this->handle->GetInt("TitleBarMenuClickGuard", 1000);
        funcs["TitleBarMenuClickGuard"] = &MainWindowParamsP::updateTitleBarMenuClickGuard;
        DefaultToolBarArea = this->handle->GetASCII("DefaultToolBarArea", "Top");
        funcs["DefaultToolBarArea"] = &MainWindowParamsP::updateDefaultToolBarArea;
        GlobalToolBarArea = this->handle->GetASCII("GlobalToolBarArea", "Top");
        funcs["GlobalToolBarArea"] = &MainWindowParamsP::updateGlobalToolBarArea;
        WSPosition = this->handle->GetASCII("WSPosition", "WSToolbar");
        funcs["WSPosition"] = &MainWindowParamsP::updateWSPosition;
        DockableWindowShortcut = this->handle->GetASCII("DockableWindowShortcut", "D, D");
        funcs["DockableWindowShortcut"] = &MainWindowParamsP::updateDockableWindowShortcut;
        ClearMenuBar = this->handle->GetBool("ClearMenuBar", false);
        funcs["ClearMenuBar"] = &MainWindowParamsP::updateClearMenuBar;
        Geometry = this->handle->GetASCII("Geometry", "");
        funcs["Geometry"] = &MainWindowParamsP::updateGeometry;
        Maximized = this->handle->GetBool("Maximized", false);
        funcs["Maximized"] = &MainWindowParamsP::updateMaximized;
        MainWindowState = this->handle->GetASCII("MainWindowState", "");
        funcs["MainWindowState"] = &MainWindowParamsP::updateMainWindowState;
        StatusBar = this->handle->GetBool("StatusBar", true);
        funcs["StatusBar"] = &MainWindowParamsP::updateStatusBar;
        WindowStateRestored = this->handle->GetBool("WindowStateRestored", false);
        funcs["WindowStateRestored"] = &MainWindowParamsP::updateWindowStateRestored;
        Theme = this->handle->GetASCII("Theme", "");
        funcs["Theme"] = &MainWindowParamsP::updateTheme;
        ThemeAutoApplied = this->handle->GetASCII("ThemeAutoApplied", "");
        funcs["ThemeAutoApplied"] = &MainWindowParamsP::updateThemeAutoApplied;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~MainWindowParamsP() override = default;

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
    static void updateColorScheme(MainWindowParamsP *self) {
        self->ColorScheme = self->handle->GetASCII("ColorScheme", "Light");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStyleSheet(MainWindowParamsP *self) {
        self->StyleSheet = self->handle->GetASCII("StyleSheet", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayActiveStyleSheet(MainWindowParamsP *self) {
        self->OverlayActiveStyleSheet = self->handle->GetASCII("OverlayActiveStyleSheet", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMenuStyleSheet(MainWindowParamsP *self) {
        self->MenuStyleSheet = self->handle->GetASCII("MenuStyleSheet", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIconSet(MainWindowParamsP *self) {
        self->IconSet = self->handle->GetASCII("IconSet", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateThemeIconSetPolicy(MainWindowParamsP *self) {
        self->ThemeIconSetPolicy = self->handle->GetASCII("ThemeIconSetPolicy", "Reset");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTiledBackground(MainWindowParamsP *self) {
        self->TiledBackground = self->handle->GetBool("TiledBackground", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateQtStyle(MainWindowParamsP *self) {
        self->QtStyle = self->handle->GetASCII("QtStyle", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateThemeAuto(MainWindowParamsP *self) {
        self->ThemeAuto = self->handle->GetBool("ThemeAuto", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateThemeStyleParametersFile(MainWindowParamsP *self) {
        self->ThemeStyleParametersFile = self->handle->GetASCII("ThemeStyleParametersFile", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCustomTitleBar(MainWindowParamsP *self) {
        self->CustomTitleBar = self->handle->GetBool("CustomTitleBar", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTitleBarToolBars(MainWindowParamsP *self) {
        self->TitleBarToolBars = self->handle->GetBool("TitleBarToolBars", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFoldTitleBarMenu(MainWindowParamsP *self) {
        self->FoldTitleBarMenu = self->handle->GetBool("FoldTitleBarMenu", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTitleBarMenuClickGuard(MainWindowParamsP *self) {
        self->TitleBarMenuClickGuard = self->handle->GetInt("TitleBarMenuClickGuard", 1000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultToolBarArea(MainWindowParamsP *self) {
        self->DefaultToolBarArea = self->handle->GetASCII("DefaultToolBarArea", "Top");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGlobalToolBarArea(MainWindowParamsP *self) {
        self->GlobalToolBarArea = self->handle->GetASCII("GlobalToolBarArea", "Top");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWSPosition(MainWindowParamsP *self) {
        self->WSPosition = self->handle->GetASCII("WSPosition", "WSToolbar");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDockableWindowShortcut(MainWindowParamsP *self) {
        self->DockableWindowShortcut = self->handle->GetASCII("DockableWindowShortcut", "D, D");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateClearMenuBar(MainWindowParamsP *self) {
        self->ClearMenuBar = self->handle->GetBool("ClearMenuBar", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGeometry(MainWindowParamsP *self) {
        self->Geometry = self->handle->GetASCII("Geometry", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaximized(MainWindowParamsP *self) {
        self->Maximized = self->handle->GetBool("Maximized", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMainWindowState(MainWindowParamsP *self) {
        self->MainWindowState = self->handle->GetASCII("MainWindowState", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStatusBar(MainWindowParamsP *self) {
        self->StatusBar = self->handle->GetBool("StatusBar", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWindowStateRestored(MainWindowParamsP *self) {
        self->WindowStateRestored = self->handle->GetBool("WindowStateRestored", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTheme(MainWindowParamsP *self) {
        self->Theme = self->handle->GetASCII("Theme", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateThemeAutoApplied(MainWindowParamsP *self) {
        self->ThemeAutoApplied = self->handle->GetASCII("ThemeAutoApplied", "");
    }
};

// Auto generated code (Tools/params_utils.py:336)
MainWindowParamsP *instance() {
    static MainWindowParamsP *inst = new MainWindowParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _MainWindowParamsRegistrar({
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "ColorScheme", "ColorScheme", App::ParamInfo::String, "Light")
        .setTitle("Colour scheme")
        .setDoc("Palette the interface is drawn with: Light, Dark, or empty to\n"
"follow the desktop. Not set at all means Light. Ignored while the\n"
"theme follows the desktop. Takes effect at once."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "StyleSheet", "StyleSheet", App::ParamInfo::String, "")
        .setTitle("Style sheet")
        .setDoc("Style sheet of the interface: a file of the 'qss' search path, or\n"
"a full path. Empty means none. Applied shortly after a change."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "OverlayActiveStyleSheet", "OverlayActiveStyleSheet", App::ParamInfo::String, "")
        .setTitle("Overlay style sheet")
        .setDoc("Style sheet of the overlay dock panels: a file of the 'overlay'\n"
"search path, or a path. Empty picks the light or the dark outline\n"
"sheet to match the interface. Takes effect at once."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "MenuStyleSheet", "MenuStyleSheet", App::ParamInfo::String, "")
        .setTitle("View menu style sheet")
        .setDoc("Style sheet of the menus that pop up over the 3D view: a file of\n"
"the 'qssm' search path, or a path. Empty gives ordinary menus.\n"
"Read each time such a menu is set up."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "IconSet", "IconSet", App::ParamInfo::String, "")
        .setTitle("Icon set")
        .setDoc("Icon set that replaces built-in icons: a file of the 'iconset'\n"
"search path or a path, or several separated by ';'. Empty means\n"
"none. Applied with the style sheet shortly after a change."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "ThemeIconSetPolicy", "ThemeIconSetPolicy", App::ParamInfo::String, "Reset")
        .setTitle("Theme icon set policy")
        .setDoc("What applying a theme does to the icon set: Reset takes the\n"
"theme's (none if it names none), Merge puts the theme's over the\n"
"user's, Keep leaves it alone."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "TiledBackground", "TiledBackground", App::ParamInfo::Bool, false)
        .setTitle("Tiled background")
        .setDoc("Fill the empty area of the main window with a tiled image instead\n"
"of plain grey. Drawn only while no style sheet is chosen."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "QtStyle", "QtStyle", App::ParamInfo::String, "")
        .setTitle("Widget style")
        .setDoc("Qt widget style the interface is drawn with: FreeCAD for the\n"
"built-in one, System for the platform's, or any style name Qt\n"
"knows. Empty leaves the style as it is. Themes set it."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "ThemeAuto", "ThemeAuto", App::ParamInfo::Bool, false)
        .setTitle("Theme follows the desktop")
        .setDoc("Follow the desktop's light or dark mode: the Light or the Dark\n"
"theme is applied at start and when the desktop changes. Set by\n"
"Match Desktop on the Start page; cleared when a theme is applied."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "ThemeStyleParametersFile", "ThemeStyleParametersFile", App::ParamInfo::String, "")
        .setTitle("Style parameters file")
        .setDoc("Path of a YAML file of style parameters used instead of the\n"
"current theme's own. Empty uses the theme's. Read at start and\n"
"each time the style sheet is applied."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "CustomTitleBar", "CustomTitleBar", App::ParamInfo::Bool, false)
        .setTitle("Custom title bar")
        .setDoc("Draw the title bar inside the application instead of the\n"
"platform's, so that the menu and tool bars can share its row.\n"
"Themes set it. Takes effect at once."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "TitleBarToolBars", "TitleBarToolBars", App::ParamInfo::Bool, true)
        .setTitle("Tool bars in the title bar")
        .setDoc("With the custom title bar, put the workbench tool bar into the\n"
"title bar instead of under the menu bar. No effect with the\n"
"platform's title bar. Takes effect at once."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "FoldTitleBarMenu", "FoldTitleBarMenu", App::ParamInfo::Bool, true)
        .setTitle("Fold the title bar menu")
        .setDoc("With the custom title bar, hide the menu bar behind the logo\n"
"button and open it on hover, leaving the row to tool bars. No\n"
"effect with the platform's title bar or on macOS."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "TitleBarMenuClickGuard", "TitleBarMenuClickGuard", App::ParamInfo::Int, 1000)
        .setTitle("Title bar menu click guard")
        .setDoc("How long, in milliseconds, a click on the title bar's logo is\n"
"ignored after hovering has opened the folded menu, so that a\n"
"habitual click does not close it again. 0 turns the guard off."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "DefaultToolBarArea", "DefaultToolBarArea", App::ParamInfo::String, "Top")
        .setTitle("Workbench tool bar area")
        .setDoc("Side of the main window where workbench tool bars are docked by\n"
"default: Top, Left, Right or Bottom. Anything else means Top. The\n"
"tool bars are moved shortly after a change."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "GlobalToolBarArea", "GlobalToolBarArea", App::ParamInfo::String, "Top")
        .setTitle("Global tool bar area")
        .setDoc("Side of the main window where the global tool bars (File,\n"
"Structure, Macro, View and those made global) are docked by\n"
"default: Top, Left, Right or Bottom. Anything else means Top."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "WSPosition", "WSPosition", App::ParamInfo::String, "WSToolbar")
        .setTitle("Workbench selector position")
        .setDoc("Where the workbench selector goes. WSToolbar puts it into a tool\n"
"bar of its own. Read each time a workbench sets its tool bars up."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "DockableWindowShortcut", "DockableWindowShortcut", App::ParamInfo::String, "D, D")
        .setTitle("Dock window shortcut prefix")
        .setDoc("Key sequence the entries of the dockable window menu get as a\n"
"shortcut, each followed by its number. Empty turns these shortcuts\n"
"off. Read each time the menu opens."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "ClearMenuBar", "ClearMenuBar", App::ParamInfo::Bool, false)
        .setTitle("Clear the menu bar before it is rebuilt")
        .setDoc("Empties the menu bar each time a workbench sets its menus up: a\n"
"way round a fault of the global menu of some Linux desktops.\n"
"It can break access to the menu bar from Python. On no page."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "Geometry", "Geometry", App::ParamInfo::String, "")
        .setTitle("Main window: place and size")
        .setDoc("Where the main window last was and how large, as 'x y width\n"
"height'. Stored by the program when the window closes."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "Maximized", "Maximized", App::ParamInfo::Bool, false)
        .setTitle("Main window: maximized")
        .setDoc("The main window was last maximized. Stored by the program when\n"
"the window closes."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "MainWindowState", "MainWindowState", App::ParamInfo::String, "")
        .setTitle("Main window: layout")
        .setDoc("Where the tool bars and the dock windows last were, as Qt packs\n"
"it. Stored by the program when the window closes; a preference\n"
"pack that holds a layout stores it too, and the layout follows a\n"
"change. Not for editing by hand."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "StatusBar", "StatusBar", App::ParamInfo::Bool, true)
        .setTitle("Main window: status bar")
        .setDoc("The status bar was last shown. Stored by the program when the\n"
"window closes."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "WindowStateRestored", "WindowStateRestored", App::ParamInfo::Bool, false)
        .setTitle("Main window: layout restored (signal)")
        .setDoc("Flipped by the program each time it has put the layout of the\n"
"main window back, so that code watching the group hears of it.\n"
"Its value means nothing."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "Theme", "Theme", App::ParamInfo::String, "")
        .setTitle("Theme in force")
        .setDoc("Name of the theme last applied, kept by the theme manager so that\n"
"it knows what the interface carries. Choose a theme on the Theme\n"
"page, not here."),
    App::ParamInfo("Gui", "MainWindowParams", "User parameter:BaseApp/Preferences/MainWindow", "ThemeAutoApplied", "ThemeAutoApplied", App::ParamInfo::String, "")
        .setTitle("Theme: what 'follow the desktop' chose")
        .setDoc("Dark or Light, as following the desktop last came out; empty\n"
"while a theme is chosen by hand. Kept by the theme manager."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle MainWindowParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
MainWindowParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void MainWindowParams::signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docColorScheme() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Palette the interface is drawn with: Light, Dark, or empty to\n"
"follow the desktop. Not set at all means Light. Ignored while the\n"
"theme follows the desktop. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getColorScheme() {
    return instance()->ColorScheme;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultColorScheme() {
    const static std::string def = "Light";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setColorScheme(const std::string &v) {
    instance()->handle->SetASCII("ColorScheme",v);
    instance()->ColorScheme = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeColorScheme() {
    instance()->handle->RemoveASCII("ColorScheme");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docStyleSheet() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Style sheet of the interface: a file of the 'qss' search path, or\n"
"a full path. Empty means none. Applied shortly after a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getStyleSheet() {
    return instance()->StyleSheet;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultStyleSheet() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setStyleSheet(const std::string &v) {
    instance()->handle->SetASCII("StyleSheet",v);
    instance()->StyleSheet = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeStyleSheet() {
    instance()->handle->RemoveASCII("StyleSheet");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docOverlayActiveStyleSheet() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Style sheet of the overlay dock panels: a file of the 'overlay'\n"
"search path, or a path. Empty picks the light or the dark outline\n"
"sheet to match the interface. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getOverlayActiveStyleSheet() {
    return instance()->OverlayActiveStyleSheet;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultOverlayActiveStyleSheet() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setOverlayActiveStyleSheet(const std::string &v) {
    instance()->handle->SetASCII("OverlayActiveStyleSheet",v);
    instance()->OverlayActiveStyleSheet = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeOverlayActiveStyleSheet() {
    instance()->handle->RemoveASCII("OverlayActiveStyleSheet");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docMenuStyleSheet() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Style sheet of the menus that pop up over the 3D view: a file of\n"
"the 'qssm' search path, or a path. Empty gives ordinary menus.\n"
"Read each time such a menu is set up.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getMenuStyleSheet() {
    return instance()->MenuStyleSheet;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultMenuStyleSheet() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setMenuStyleSheet(const std::string &v) {
    instance()->handle->SetASCII("MenuStyleSheet",v);
    instance()->MenuStyleSheet = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeMenuStyleSheet() {
    instance()->handle->RemoveASCII("MenuStyleSheet");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docIconSet() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Icon set that replaces built-in icons: a file of the 'iconset'\n"
"search path or a path, or several separated by ';'. Empty means\n"
"none. Applied with the style sheet shortly after a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getIconSet() {
    return instance()->IconSet;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultIconSet() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setIconSet(const std::string &v) {
    instance()->handle->SetASCII("IconSet",v);
    instance()->IconSet = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeIconSet() {
    instance()->handle->RemoveASCII("IconSet");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docThemeIconSetPolicy() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"What applying a theme does to the icon set: Reset takes the\n"
"theme's (none if it names none), Merge puts the theme's over the\n"
"user's, Keep leaves it alone.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getThemeIconSetPolicy() {
    return instance()->ThemeIconSetPolicy;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultThemeIconSetPolicy() {
    const static std::string def = "Reset";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setThemeIconSetPolicy(const std::string &v) {
    instance()->handle->SetASCII("ThemeIconSetPolicy",v);
    instance()->ThemeIconSetPolicy = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeThemeIconSetPolicy() {
    instance()->handle->RemoveASCII("ThemeIconSetPolicy");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docTiledBackground() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Fill the empty area of the main window with a tiled image instead\n"
"of plain grey. Drawn only while no style sheet is chosen.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MainWindowParams::getTiledBackground() {
    return instance()->TiledBackground;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MainWindowParams::defaultTiledBackground() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setTiledBackground(const bool &v) {
    instance()->handle->SetBool("TiledBackground",v);
    instance()->TiledBackground = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeTiledBackground() {
    instance()->handle->RemoveBool("TiledBackground");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docQtStyle() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Qt widget style the interface is drawn with: FreeCAD for the\n"
"built-in one, System for the platform's, or any style name Qt\n"
"knows. Empty leaves the style as it is. Themes set it.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getQtStyle() {
    return instance()->QtStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultQtStyle() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setQtStyle(const std::string &v) {
    instance()->handle->SetASCII("QtStyle",v);
    instance()->QtStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeQtStyle() {
    instance()->handle->RemoveASCII("QtStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docThemeAuto() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Follow the desktop's light or dark mode: the Light or the Dark\n"
"theme is applied at start and when the desktop changes. Set by\n"
"Match Desktop on the Start page; cleared when a theme is applied.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MainWindowParams::getThemeAuto() {
    return instance()->ThemeAuto;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MainWindowParams::defaultThemeAuto() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setThemeAuto(const bool &v) {
    instance()->handle->SetBool("ThemeAuto",v);
    instance()->ThemeAuto = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeThemeAuto() {
    instance()->handle->RemoveBool("ThemeAuto");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docThemeStyleParametersFile() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Path of a YAML file of style parameters used instead of the\n"
"current theme's own. Empty uses the theme's. Read at start and\n"
"each time the style sheet is applied.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getThemeStyleParametersFile() {
    return instance()->ThemeStyleParametersFile;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultThemeStyleParametersFile() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setThemeStyleParametersFile(const std::string &v) {
    instance()->handle->SetASCII("ThemeStyleParametersFile",v);
    instance()->ThemeStyleParametersFile = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeThemeStyleParametersFile() {
    instance()->handle->RemoveASCII("ThemeStyleParametersFile");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docCustomTitleBar() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Draw the title bar inside the application instead of the\n"
"platform's, so that the menu and tool bars can share its row.\n"
"Themes set it. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MainWindowParams::getCustomTitleBar() {
    return instance()->CustomTitleBar;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MainWindowParams::defaultCustomTitleBar() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setCustomTitleBar(const bool &v) {
    instance()->handle->SetBool("CustomTitleBar",v);
    instance()->CustomTitleBar = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeCustomTitleBar() {
    instance()->handle->RemoveBool("CustomTitleBar");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docTitleBarToolBars() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"With the custom title bar, put the workbench tool bar into the\n"
"title bar instead of under the menu bar. No effect with the\n"
"platform's title bar. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MainWindowParams::getTitleBarToolBars() {
    return instance()->TitleBarToolBars;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MainWindowParams::defaultTitleBarToolBars() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setTitleBarToolBars(const bool &v) {
    instance()->handle->SetBool("TitleBarToolBars",v);
    instance()->TitleBarToolBars = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeTitleBarToolBars() {
    instance()->handle->RemoveBool("TitleBarToolBars");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docFoldTitleBarMenu() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"With the custom title bar, hide the menu bar behind the logo\n"
"button and open it on hover, leaving the row to tool bars. No\n"
"effect with the platform's title bar or on macOS.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MainWindowParams::getFoldTitleBarMenu() {
    return instance()->FoldTitleBarMenu;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MainWindowParams::defaultFoldTitleBarMenu() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setFoldTitleBarMenu(const bool &v) {
    instance()->handle->SetBool("FoldTitleBarMenu",v);
    instance()->FoldTitleBarMenu = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeFoldTitleBarMenu() {
    instance()->handle->RemoveBool("FoldTitleBarMenu");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docTitleBarMenuClickGuard() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"How long, in milliseconds, a click on the title bar's logo is\n"
"ignored after hovering has opened the folded menu, so that a\n"
"habitual click does not close it again. 0 turns the guard off.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MainWindowParams::getTitleBarMenuClickGuard() {
    return instance()->TitleBarMenuClickGuard;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MainWindowParams::defaultTitleBarMenuClickGuard() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setTitleBarMenuClickGuard(const long &v) {
    instance()->handle->SetInt("TitleBarMenuClickGuard",v);
    instance()->TitleBarMenuClickGuard = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeTitleBarMenuClickGuard() {
    instance()->handle->RemoveInt("TitleBarMenuClickGuard");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docDefaultToolBarArea() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Side of the main window where workbench tool bars are docked by\n"
"default: Top, Left, Right or Bottom. Anything else means Top. The\n"
"tool bars are moved shortly after a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getDefaultToolBarArea() {
    return instance()->DefaultToolBarArea;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultDefaultToolBarArea() {
    const static std::string def = "Top";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setDefaultToolBarArea(const std::string &v) {
    instance()->handle->SetASCII("DefaultToolBarArea",v);
    instance()->DefaultToolBarArea = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeDefaultToolBarArea() {
    instance()->handle->RemoveASCII("DefaultToolBarArea");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docGlobalToolBarArea() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Side of the main window where the global tool bars (File,\n"
"Structure, Macro, View and those made global) are docked by\n"
"default: Top, Left, Right or Bottom. Anything else means Top.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getGlobalToolBarArea() {
    return instance()->GlobalToolBarArea;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultGlobalToolBarArea() {
    const static std::string def = "Top";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setGlobalToolBarArea(const std::string &v) {
    instance()->handle->SetASCII("GlobalToolBarArea",v);
    instance()->GlobalToolBarArea = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeGlobalToolBarArea() {
    instance()->handle->RemoveASCII("GlobalToolBarArea");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docWSPosition() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Where the workbench selector goes. WSToolbar puts it into a tool\n"
"bar of its own. Read each time a workbench sets its tool bars up.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getWSPosition() {
    return instance()->WSPosition;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultWSPosition() {
    const static std::string def = "WSToolbar";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setWSPosition(const std::string &v) {
    instance()->handle->SetASCII("WSPosition",v);
    instance()->WSPosition = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeWSPosition() {
    instance()->handle->RemoveASCII("WSPosition");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docDockableWindowShortcut() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Key sequence the entries of the dockable window menu get as a\n"
"shortcut, each followed by its number. Empty turns these shortcuts\n"
"off. Read each time the menu opens.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getDockableWindowShortcut() {
    return instance()->DockableWindowShortcut;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultDockableWindowShortcut() {
    const static std::string def = "D, D";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setDockableWindowShortcut(const std::string &v) {
    instance()->handle->SetASCII("DockableWindowShortcut",v);
    instance()->DockableWindowShortcut = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeDockableWindowShortcut() {
    instance()->handle->RemoveASCII("DockableWindowShortcut");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docClearMenuBar() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Empties the menu bar each time a workbench sets its menus up: a\n"
"way round a fault of the global menu of some Linux desktops.\n"
"It can break access to the menu bar from Python. On no page.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MainWindowParams::getClearMenuBar() {
    return instance()->ClearMenuBar;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MainWindowParams::defaultClearMenuBar() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setClearMenuBar(const bool &v) {
    instance()->handle->SetBool("ClearMenuBar",v);
    instance()->ClearMenuBar = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeClearMenuBar() {
    instance()->handle->RemoveBool("ClearMenuBar");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docGeometry() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Where the main window last was and how large, as 'x y width\n"
"height'. Stored by the program when the window closes.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getGeometry() {
    return instance()->Geometry;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultGeometry() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setGeometry(const std::string &v) {
    instance()->handle->SetASCII("Geometry",v);
    instance()->Geometry = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeGeometry() {
    instance()->handle->RemoveASCII("Geometry");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docMaximized() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"The main window was last maximized. Stored by the program when\n"
"the window closes.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MainWindowParams::getMaximized() {
    return instance()->Maximized;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MainWindowParams::defaultMaximized() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setMaximized(const bool &v) {
    instance()->handle->SetBool("Maximized",v);
    instance()->Maximized = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeMaximized() {
    instance()->handle->RemoveBool("Maximized");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docMainWindowState() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Where the tool bars and the dock windows last were, as Qt packs\n"
"it. Stored by the program when the window closes; a preference\n"
"pack that holds a layout stores it too, and the layout follows a\n"
"change. Not for editing by hand.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getMainWindowState() {
    return instance()->MainWindowState;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultMainWindowState() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setMainWindowState(const std::string &v) {
    instance()->handle->SetASCII("MainWindowState",v);
    instance()->MainWindowState = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeMainWindowState() {
    instance()->handle->RemoveASCII("MainWindowState");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docStatusBar() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"The status bar was last shown. Stored by the program when the\n"
"window closes.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MainWindowParams::getStatusBar() {
    return instance()->StatusBar;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MainWindowParams::defaultStatusBar() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setStatusBar(const bool &v) {
    instance()->handle->SetBool("StatusBar",v);
    instance()->StatusBar = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeStatusBar() {
    instance()->handle->RemoveBool("StatusBar");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docWindowStateRestored() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Flipped by the program each time it has put the layout of the\n"
"main window back, so that code watching the group hears of it.\n"
"Its value means nothing.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MainWindowParams::getWindowStateRestored() {
    return instance()->WindowStateRestored;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MainWindowParams::defaultWindowStateRestored() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setWindowStateRestored(const bool &v) {
    instance()->handle->SetBool("WindowStateRestored",v);
    instance()->WindowStateRestored = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeWindowStateRestored() {
    instance()->handle->RemoveBool("WindowStateRestored");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docTheme() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Name of the theme last applied, kept by the theme manager so that\n"
"it knows what the interface carries. Choose a theme on the Theme\n"
"page, not here.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getTheme() {
    return instance()->Theme;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultTheme() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setTheme(const std::string &v) {
    instance()->handle->SetASCII("Theme",v);
    instance()->Theme = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeTheme() {
    instance()->handle->RemoveASCII("Theme");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MainWindowParams::docThemeAutoApplied() {
    return QT_TRANSLATE_NOOP("MainWindowParams",
"Dark or Light, as following the desktop last came out; empty\n"
"while a theme is chosen by hand. Kept by the theme manager.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MainWindowParams::getThemeAutoApplied() {
    return instance()->ThemeAutoApplied;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MainWindowParams::defaultThemeAutoApplied() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MainWindowParams::setThemeAutoApplied(const std::string &v) {
    instance()->handle->SetASCII("ThemeAutoApplied",v);
    instance()->ThemeAutoApplied = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MainWindowParams::removeThemeAutoApplied() {
    instance()->handle->RemoveASCII("ThemeAutoApplied");
}
//[[[end]]]
