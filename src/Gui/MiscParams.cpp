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
import MiscParams
MiscParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "MiscParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class MiscParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(MiscParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    long RecentMacros;
    long ShortcutCount;
    std::string ShortcutModifiers;
    long CoarseLinearSnapMultiplier;
    long CoarseRotationSnapMultiplier;
    unsigned long CacheLimit;
    long CachePeriod;
    long ShortcutTimeout;
    bool ShowTabBar;
    bool TabBarShowText;
    long TabBarMaxLength;
    bool DisableDpiScaling;
    bool UseSoftwareOpenGL;
    bool Unflatten;

    // Auto generated code (Tools/params_utils.py:254)
    MiscParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences");
        handle->Attach(this);

        subHandles.resize(8);
        subHandles[0] = handle->GetGroup("RecentMacros");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("Gui/Gizmos");
        subHandles[1]->Attach(this);
        subHandles[2] = handle->GetGroup("CacheDirectory");
        subHandles[2]->Attach(this);
        subHandles[3] = handle->GetGroup("Shortcut/Settings");
        subHandles[3]->Attach(this);
        subHandles[4] = handle->GetGroup("Workbenches");
        subHandles[4]->Attach(this);
        subHandles[5] = handle->GetGroup("HighDPI");
        subHandles[5]->Attach(this);
        subHandles[6] = handle->GetGroup("OpenGL");
        subHandles[6]->Attach(this);
        subHandles[7] = handle->GetGroup("DependencyGraph");
        subHandles[7]->Attach(this);
        RecentMacros = this->subHandles[0]->GetInt("RecentMacros", 12);
        funcs["RecentMacros"] = &MiscParamsP::updateRecentMacros;
        ShortcutCount = this->subHandles[0]->GetInt("ShortcutCount", 3);
        funcs["ShortcutCount"] = &MiscParamsP::updateShortcutCount;
        ShortcutModifiers = this->subHandles[0]->GetASCII("ShortcutModifiers", "Ctrl+Shift+");
        funcs["ShortcutModifiers"] = &MiscParamsP::updateShortcutModifiers;
        CoarseLinearSnapMultiplier = this->subHandles[1]->GetInt("CoarseLinearSnapMultiplier", 5);
        funcs["CoarseLinearSnapMultiplier"] = &MiscParamsP::updateCoarseLinearSnapMultiplier;
        CoarseRotationSnapMultiplier = this->subHandles[1]->GetInt("CoarseRotationSnapMultiplier", 5);
        funcs["CoarseRotationSnapMultiplier"] = &MiscParamsP::updateCoarseRotationSnapMultiplier;
        CacheLimit = this->subHandles[2]->GetUnsigned("Limit", 500);
        funcs["Limit"] = &MiscParamsP::updateCacheLimit;
        CachePeriod = this->subHandles[2]->GetInt("Period", 2);
        funcs["Period"] = &MiscParamsP::updateCachePeriod;
        ShortcutTimeout = this->subHandles[3]->GetInt("ShortcutTimeout", 300);
        funcs["ShortcutTimeout"] = &MiscParamsP::updateShortcutTimeout;
        ShowTabBar = this->subHandles[4]->GetBool("ShowTabBar", false);
        funcs["ShowTabBar"] = &MiscParamsP::updateShowTabBar;
        TabBarShowText = this->subHandles[4]->GetBool("TabBarShowText", false);
        funcs["TabBarShowText"] = &MiscParamsP::updateTabBarShowText;
        TabBarMaxLength = this->subHandles[4]->GetInt("TabBarMaxLength", 0);
        funcs["TabBarMaxLength"] = &MiscParamsP::updateTabBarMaxLength;
        DisableDpiScaling = this->subHandles[5]->GetBool("DisableDpiScaling", false);
        funcs["DisableDpiScaling"] = &MiscParamsP::updateDisableDpiScaling;
        UseSoftwareOpenGL = this->subHandles[6]->GetBool("UseSoftwareOpenGL", false);
        funcs["UseSoftwareOpenGL"] = &MiscParamsP::updateUseSoftwareOpenGL;
        Unflatten = this->subHandles[7]->GetBool("Unflatten", true);
        funcs["Unflatten"] = &MiscParamsP::updateUnflatten;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~MiscParamsP() override = default;

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
    static void updateRecentMacros(MiscParamsP *self) {
        self->RecentMacros = self->subHandles[0]->GetInt("RecentMacros", 12);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShortcutCount(MiscParamsP *self) {
        self->ShortcutCount = self->subHandles[0]->GetInt("ShortcutCount", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShortcutModifiers(MiscParamsP *self) {
        self->ShortcutModifiers = self->subHandles[0]->GetASCII("ShortcutModifiers", "Ctrl+Shift+");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoarseLinearSnapMultiplier(MiscParamsP *self) {
        self->CoarseLinearSnapMultiplier = self->subHandles[1]->GetInt("CoarseLinearSnapMultiplier", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoarseRotationSnapMultiplier(MiscParamsP *self) {
        self->CoarseRotationSnapMultiplier = self->subHandles[1]->GetInt("CoarseRotationSnapMultiplier", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCacheLimit(MiscParamsP *self) {
        self->CacheLimit = self->subHandles[2]->GetUnsigned("Limit", 500);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCachePeriod(MiscParamsP *self) {
        self->CachePeriod = self->subHandles[2]->GetInt("Period", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShortcutTimeout(MiscParamsP *self) {
        self->ShortcutTimeout = self->subHandles[3]->GetInt("ShortcutTimeout", 300);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowTabBar(MiscParamsP *self) {
        self->ShowTabBar = self->subHandles[4]->GetBool("ShowTabBar", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTabBarShowText(MiscParamsP *self) {
        self->TabBarShowText = self->subHandles[4]->GetBool("TabBarShowText", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTabBarMaxLength(MiscParamsP *self) {
        self->TabBarMaxLength = self->subHandles[4]->GetInt("TabBarMaxLength", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDisableDpiScaling(MiscParamsP *self) {
        self->DisableDpiScaling = self->subHandles[5]->GetBool("DisableDpiScaling", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseSoftwareOpenGL(MiscParamsP *self) {
        self->UseSoftwareOpenGL = self->subHandles[6]->GetBool("UseSoftwareOpenGL", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUnflatten(MiscParamsP *self) {
        self->Unflatten = self->subHandles[7]->GetBool("Unflatten", true);
    }
};

// Auto generated code (Tools/params_utils.py:336)
MiscParamsP *instance() {
    static MiscParamsP *inst = new MiscParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _MiscParamsRegistrar({
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/RecentMacros", "RecentMacros", "RecentMacros", App::ParamInfo::Int, 12)
        .setTitle("Size of recent macro list")
        .setDoc("Number of macros the recent macros menu lists."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/RecentMacros", "ShortcutCount", "ShortcutCount", App::ParamInfo::Int, 3)
        .setTitle("Recent macros with a shortcut")
        .setDoc("Number of entries of the recent macros menu that get a keyboard\n"
"shortcut, the modifiers below and a digit. At most 9."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/RecentMacros", "ShortcutModifiers", "ShortcutModifiers", App::ParamInfo::String, "Ctrl+Shift+")
        .setTitle("Recent macro shortcut modifiers")
        .setDoc("Modifier keys of the shortcuts of the recent macros menu, written\n"
"as in a shortcut and ending in +, such as Ctrl+Shift+."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Gui/Gizmos", "CoarseLinearSnapMultiplier", "CoarseLinearSnapMultiplier", App::ParamInfo::Int, 5)
        .setTitle("Coarse linear step of a gizmo")
        .setDoc("How many times larger the step of a linear gizmo is while the\n"
"key for coarse steps is held."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Gui/Gizmos", "CoarseRotationSnapMultiplier", "CoarseRotationSnapMultiplier", App::ParamInfo::Int, 5)
        .setTitle("Coarse rotation step of a gizmo")
        .setDoc("How many times larger the step of a rotation gizmo is while the\n"
"key for coarse steps is held."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/CacheDirectory", "CacheLimit", "Limit", App::ParamInfo::UInt, 500)
        .setTitle("Cache size limit")
        .setDoc("Size in megabytes the cache directory may grow to before the\n"
"program offers to clean it."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/CacheDirectory", "CachePeriod", "Period", App::ParamInfo::Int, 2)
        .setTitle("Cache check period")
        .setDoc("How often the size of the cache directory is checked: 0 always,\n"
"1 daily, 2 weekly, 3 monthly, 4 yearly, 5 never."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Shortcut/Settings", "ShortcutTimeout", "ShortcutTimeout", App::ParamInfo::Int, 300)
        .setTitle("Shortcut sequence timeout")
        .setDoc("Milliseconds the program waits for the next key of a shortcut\n"
"made of several keys before it acts on what was typed."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Workbenches", "ShowTabBar", "ShowTabBar", App::ParamInfo::Bool, false)
        .setTitle("Workbench tab bar")
        .setDoc("Show the workbenches as a bar of tabs instead of a drop-down\n"
"list."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Workbenches", "TabBarShowText", "TabBarShowText", App::ParamInfo::Bool, false)
        .setTitle("Workbench tab bar text")
        .setDoc("Show the name of each workbench on its tab, beside its icon."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Workbenches", "TabBarMaxLength", "TabBarMaxLength", App::ParamInfo::Int, 0)
        .setTitle("Workbench tab bar length")
        .setDoc("Room in pixels the workbench tab bar may take along the way its\n"
"tabs run. 0 takes what its tabs need."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/HighDPI", "DisableDpiScaling", "DisableDpiScaling", App::ParamInfo::Bool, false)
        .setTitle("Disable high DPI scaling")
        .setDoc("Switch Qt's scaling for high resolution screens off. Read at\n"
"startup."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/OpenGL", "UseSoftwareOpenGL", "UseSoftwareOpenGL", App::ParamInfo::Bool, false)
        .setTitle("Use software OpenGL")
        .setDoc("Draw with a software implementation of OpenGL instead of the\n"
"graphics driver. Read at startup."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DependencyGraph", "Unflatten", "Unflatten", App::ParamInfo::Bool, true)
        .setTitle("Unflatten the dependency graph")
        .setDoc("Run the dependency graph through Graphviz's unflatten, which\n"
"makes wide graphs narrower."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle MiscParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docRecentMacros() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Number of macros the recent macros menu lists.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getRecentMacros() {
    return instance()->RecentMacros;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultRecentMacros() {
    const static long def = 12;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setRecentMacros(const long &v) {
    instance()->subHandles[0]->SetInt("RecentMacros",v);
    instance()->RecentMacros = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeRecentMacros() {
    instance()->subHandles[0]->RemoveInt("RecentMacros");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShortcutCount() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Number of entries of the recent macros menu that get a keyboard\n"
"shortcut, the modifiers below and a digit. At most 9.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getShortcutCount() {
    return instance()->ShortcutCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultShortcutCount() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShortcutCount(const long &v) {
    instance()->subHandles[0]->SetInt("ShortcutCount",v);
    instance()->ShortcutCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShortcutCount() {
    instance()->subHandles[0]->RemoveInt("ShortcutCount");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShortcutModifiers() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Modifier keys of the shortcuts of the recent macros menu, written\n"
"as in a shortcut and ending in +, such as Ctrl+Shift+.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getShortcutModifiers() {
    return instance()->ShortcutModifiers;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultShortcutModifiers() {
    const static std::string def = "Ctrl+Shift+";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShortcutModifiers(const std::string &v) {
    instance()->subHandles[0]->SetASCII("ShortcutModifiers",v);
    instance()->ShortcutModifiers = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShortcutModifiers() {
    instance()->subHandles[0]->RemoveASCII("ShortcutModifiers");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCoarseLinearSnapMultiplier() {
    return QT_TRANSLATE_NOOP("MiscParams",
"How many times larger the step of a linear gizmo is while the\n"
"key for coarse steps is held.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getCoarseLinearSnapMultiplier() {
    return instance()->CoarseLinearSnapMultiplier;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultCoarseLinearSnapMultiplier() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCoarseLinearSnapMultiplier(const long &v) {
    instance()->subHandles[1]->SetInt("CoarseLinearSnapMultiplier",v);
    instance()->CoarseLinearSnapMultiplier = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCoarseLinearSnapMultiplier() {
    instance()->subHandles[1]->RemoveInt("CoarseLinearSnapMultiplier");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCoarseRotationSnapMultiplier() {
    return QT_TRANSLATE_NOOP("MiscParams",
"How many times larger the step of a rotation gizmo is while the\n"
"key for coarse steps is held.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getCoarseRotationSnapMultiplier() {
    return instance()->CoarseRotationSnapMultiplier;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultCoarseRotationSnapMultiplier() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCoarseRotationSnapMultiplier(const long &v) {
    instance()->subHandles[1]->SetInt("CoarseRotationSnapMultiplier",v);
    instance()->CoarseRotationSnapMultiplier = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCoarseRotationSnapMultiplier() {
    instance()->subHandles[1]->RemoveInt("CoarseRotationSnapMultiplier");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCacheLimit() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Size in megabytes the cache directory may grow to before the\n"
"program offers to clean it.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & MiscParams::getCacheLimit() {
    return instance()->CacheLimit;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & MiscParams::defaultCacheLimit() {
    const static unsigned long def = 500;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCacheLimit(const unsigned long &v) {
    instance()->subHandles[2]->SetUnsigned("Limit",v);
    instance()->CacheLimit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCacheLimit() {
    instance()->subHandles[2]->RemoveUnsigned("Limit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCachePeriod() {
    return QT_TRANSLATE_NOOP("MiscParams",
"How often the size of the cache directory is checked: 0 always,\n"
"1 daily, 2 weekly, 3 monthly, 4 yearly, 5 never.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getCachePeriod() {
    return instance()->CachePeriod;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultCachePeriod() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCachePeriod(const long &v) {
    instance()->subHandles[2]->SetInt("Period",v);
    instance()->CachePeriod = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCachePeriod() {
    instance()->subHandles[2]->RemoveInt("Period");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShortcutTimeout() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Milliseconds the program waits for the next key of a shortcut\n"
"made of several keys before it acts on what was typed.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getShortcutTimeout() {
    return instance()->ShortcutTimeout;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultShortcutTimeout() {
    const static long def = 300;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShortcutTimeout(const long &v) {
    instance()->subHandles[3]->SetInt("ShortcutTimeout",v);
    instance()->ShortcutTimeout = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShortcutTimeout() {
    instance()->subHandles[3]->RemoveInt("ShortcutTimeout");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShowTabBar() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Show the workbenches as a bar of tabs instead of a drop-down\n"
"list.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getShowTabBar() {
    return instance()->ShowTabBar;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultShowTabBar() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShowTabBar(const bool &v) {
    instance()->subHandles[4]->SetBool("ShowTabBar",v);
    instance()->ShowTabBar = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShowTabBar() {
    instance()->subHandles[4]->RemoveBool("ShowTabBar");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docTabBarShowText() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Show the name of each workbench on its tab, beside its icon.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getTabBarShowText() {
    return instance()->TabBarShowText;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultTabBarShowText() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setTabBarShowText(const bool &v) {
    instance()->subHandles[4]->SetBool("TabBarShowText",v);
    instance()->TabBarShowText = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeTabBarShowText() {
    instance()->subHandles[4]->RemoveBool("TabBarShowText");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docTabBarMaxLength() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Room in pixels the workbench tab bar may take along the way its\n"
"tabs run. 0 takes what its tabs need.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getTabBarMaxLength() {
    return instance()->TabBarMaxLength;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultTabBarMaxLength() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setTabBarMaxLength(const long &v) {
    instance()->subHandles[4]->SetInt("TabBarMaxLength",v);
    instance()->TabBarMaxLength = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeTabBarMaxLength() {
    instance()->subHandles[4]->RemoveInt("TabBarMaxLength");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDisableDpiScaling() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Switch Qt's scaling for high resolution screens off. Read at\n"
"startup.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getDisableDpiScaling() {
    return instance()->DisableDpiScaling;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultDisableDpiScaling() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDisableDpiScaling(const bool &v) {
    instance()->subHandles[5]->SetBool("DisableDpiScaling",v);
    instance()->DisableDpiScaling = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDisableDpiScaling() {
    instance()->subHandles[5]->RemoveBool("DisableDpiScaling");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docUseSoftwareOpenGL() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Draw with a software implementation of OpenGL instead of the\n"
"graphics driver. Read at startup.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getUseSoftwareOpenGL() {
    return instance()->UseSoftwareOpenGL;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultUseSoftwareOpenGL() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setUseSoftwareOpenGL(const bool &v) {
    instance()->subHandles[6]->SetBool("UseSoftwareOpenGL",v);
    instance()->UseSoftwareOpenGL = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeUseSoftwareOpenGL() {
    instance()->subHandles[6]->RemoveBool("UseSoftwareOpenGL");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docUnflatten() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Run the dependency graph through Graphviz's unflatten, which\n"
"makes wide graphs narrower.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getUnflatten() {
    return instance()->Unflatten;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultUnflatten() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setUnflatten(const bool &v) {
    instance()->subHandles[7]->SetBool("Unflatten",v);
    instance()->Unflatten = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeUnflatten() {
    instance()->subHandles[7]->RemoveBool("Unflatten");
}
//[[[end]]]
