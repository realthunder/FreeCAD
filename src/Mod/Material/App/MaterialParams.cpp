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
import MaterialParams
MaterialParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "MaterialParams.h"
using namespace Materials;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class MaterialParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(MaterialParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    std::string DefaultMaterial;
    bool EditorShowFavorites;
    bool EditorShowRecent;
    bool EditorShowEmptyFolders;
    bool EditorShowEmptyLibraries;
    bool EditorShowLegacy;
    bool SelectorShowFavorites;
    bool SelectorShowRecent;
    bool SelectorShowEmptyFolders;
    bool SelectorShowEmptyLibraries;
    bool SelectorShowLegacy;
    long SelectorIconSize;
    long RecentMax;
    long ModelsRecentMax;
    bool UseBuiltInMaterials;
    bool UseMaterialsFromWorkbenches;
    bool UseMaterialsFromConfigDir;
    bool UseMaterialsFromCustomDir;
    std::string CustomMaterialsDir;
    bool UseExternal;
    std::string ExternalInterface;
    long ModelCacheSize;
    long MaterialCacheSize;
    long EditorWidth;
    long EditorHeight;
    long FavoritesCount;
    long RecentCount;
    long ModelsFavoritesCount;
    long ModelsRecentCount;

    // Auto generated code (Tools/params_utils.py:254)
    MaterialParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Material");
        handle->Attach(this);

        subHandles.resize(8);
        subHandles[0] = handle->GetGroup("Editor");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("TreeWidget");
        subHandles[1]->Attach(this);
        subHandles[2] = handle->GetGroup("Recent");
        subHandles[2]->Attach(this);
        subHandles[3] = handle->GetGroup("Models/Recent");
        subHandles[3]->Attach(this);
        subHandles[4] = handle->GetGroup("Resources");
        subHandles[4]->Attach(this);
        subHandles[5] = handle->GetGroup("ExternalInterface");
        subHandles[5]->Attach(this);
        subHandles[6] = handle->GetGroup("Favorites");
        subHandles[6]->Attach(this);
        subHandles[7] = handle->GetGroup("Models/Favorites");
        subHandles[7]->Attach(this);
        DefaultMaterial = this->handle->GetASCII("DefaultMaterial", "7f9fd73b-50c9-41d8-b7b2-575a030c1eeb");
        funcs["DefaultMaterial"] = &MaterialParamsP::updateDefaultMaterial;
        EditorShowFavorites = this->subHandles[0]->GetBool("ShowFavorites", true);
        funcs["ShowFavorites"] = &MaterialParamsP::updateEditorShowFavorites;
        EditorShowRecent = this->subHandles[0]->GetBool("ShowRecent", true);
        funcs["ShowRecent"] = &MaterialParamsP::updateEditorShowRecent;
        EditorShowEmptyFolders = this->subHandles[0]->GetBool("ShowEmptyFolders", false);
        funcs["ShowEmptyFolders"] = &MaterialParamsP::updateEditorShowEmptyFolders;
        EditorShowEmptyLibraries = this->subHandles[0]->GetBool("ShowEmptyLibraries", true);
        funcs["ShowEmptyLibraries"] = &MaterialParamsP::updateEditorShowEmptyLibraries;
        EditorShowLegacy = this->subHandles[0]->GetBool("ShowLegacy", false);
        funcs["ShowLegacy"] = &MaterialParamsP::updateEditorShowLegacy;
        SelectorShowFavorites = this->subHandles[1]->GetBool("ShowFavorites", true);
        funcs["ShowFavorites"] = &MaterialParamsP::updateSelectorShowFavorites;
        SelectorShowRecent = this->subHandles[1]->GetBool("ShowRecent", true);
        funcs["ShowRecent"] = &MaterialParamsP::updateSelectorShowRecent;
        SelectorShowEmptyFolders = this->subHandles[1]->GetBool("ShowEmptyFolders", false);
        funcs["ShowEmptyFolders"] = &MaterialParamsP::updateSelectorShowEmptyFolders;
        SelectorShowEmptyLibraries = this->subHandles[1]->GetBool("ShowEmptyLibraries", true);
        funcs["ShowEmptyLibraries"] = &MaterialParamsP::updateSelectorShowEmptyLibraries;
        SelectorShowLegacy = this->subHandles[1]->GetBool("ShowLegacy", false);
        funcs["ShowLegacy"] = &MaterialParamsP::updateSelectorShowLegacy;
        SelectorIconSize = this->subHandles[1]->GetInt("IconSize", 64);
        funcs["IconSize"] = &MaterialParamsP::updateSelectorIconSize;
        RecentMax = this->subHandles[2]->GetInt("RecentMax", 5);
        funcs["RecentMax"] = &MaterialParamsP::updateRecentMax;
        ModelsRecentMax = this->subHandles[3]->GetInt("RecentMax", 5);
        funcs["RecentMax"] = &MaterialParamsP::updateModelsRecentMax;
        UseBuiltInMaterials = this->subHandles[4]->GetBool("UseBuiltInMaterials", true);
        funcs["UseBuiltInMaterials"] = &MaterialParamsP::updateUseBuiltInMaterials;
        UseMaterialsFromWorkbenches = this->subHandles[4]->GetBool("UseMaterialsFromWorkbenches", true);
        funcs["UseMaterialsFromWorkbenches"] = &MaterialParamsP::updateUseMaterialsFromWorkbenches;
        UseMaterialsFromConfigDir = this->subHandles[4]->GetBool("UseMaterialsFromConfigDir", true);
        funcs["UseMaterialsFromConfigDir"] = &MaterialParamsP::updateUseMaterialsFromConfigDir;
        UseMaterialsFromCustomDir = this->subHandles[4]->GetBool("UseMaterialsFromCustomDir", true);
        funcs["UseMaterialsFromCustomDir"] = &MaterialParamsP::updateUseMaterialsFromCustomDir;
        CustomMaterialsDir = this->subHandles[4]->GetASCII("CustomMaterialsDir", "");
        funcs["CustomMaterialsDir"] = &MaterialParamsP::updateCustomMaterialsDir;
        UseExternal = this->subHandles[5]->GetBool("UseExternal", false);
        funcs["UseExternal"] = &MaterialParamsP::updateUseExternal;
        ExternalInterface = this->subHandles[5]->GetASCII("Current", "None");
        funcs["Current"] = &MaterialParamsP::updateExternalInterface;
        ModelCacheSize = this->subHandles[5]->GetInt("ModelCacheSize", 100);
        funcs["ModelCacheSize"] = &MaterialParamsP::updateModelCacheSize;
        MaterialCacheSize = this->subHandles[5]->GetInt("MaterialCacheSize", 100);
        funcs["MaterialCacheSize"] = &MaterialParamsP::updateMaterialCacheSize;
        EditorWidth = this->subHandles[0]->GetInt("EditorWidth", 835);
        funcs["EditorWidth"] = &MaterialParamsP::updateEditorWidth;
        EditorHeight = this->subHandles[0]->GetInt("EditorHeight", 542);
        funcs["EditorHeight"] = &MaterialParamsP::updateEditorHeight;
        FavoritesCount = this->subHandles[6]->GetInt("Favorites", 0);
        funcs["Favorites"] = &MaterialParamsP::updateFavoritesCount;
        RecentCount = this->subHandles[2]->GetInt("Recent", 0);
        funcs["Recent"] = &MaterialParamsP::updateRecentCount;
        ModelsFavoritesCount = this->subHandles[7]->GetInt("Favorites", 0);
        funcs["Favorites"] = &MaterialParamsP::updateModelsFavoritesCount;
        ModelsRecentCount = this->subHandles[3]->GetInt("Recent", 0);
        funcs["Recent"] = &MaterialParamsP::updateModelsRecentCount;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~MaterialParamsP() override = default;

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
    static void updateDefaultMaterial(MaterialParamsP *self) {
        self->DefaultMaterial = self->handle->GetASCII("DefaultMaterial", "7f9fd73b-50c9-41d8-b7b2-575a030c1eeb");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditorShowFavorites(MaterialParamsP *self) {
        self->EditorShowFavorites = self->subHandles[0]->GetBool("ShowFavorites", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditorShowRecent(MaterialParamsP *self) {
        self->EditorShowRecent = self->subHandles[0]->GetBool("ShowRecent", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditorShowEmptyFolders(MaterialParamsP *self) {
        self->EditorShowEmptyFolders = self->subHandles[0]->GetBool("ShowEmptyFolders", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditorShowEmptyLibraries(MaterialParamsP *self) {
        self->EditorShowEmptyLibraries = self->subHandles[0]->GetBool("ShowEmptyLibraries", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditorShowLegacy(MaterialParamsP *self) {
        self->EditorShowLegacy = self->subHandles[0]->GetBool("ShowLegacy", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectorShowFavorites(MaterialParamsP *self) {
        self->SelectorShowFavorites = self->subHandles[1]->GetBool("ShowFavorites", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectorShowRecent(MaterialParamsP *self) {
        self->SelectorShowRecent = self->subHandles[1]->GetBool("ShowRecent", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectorShowEmptyFolders(MaterialParamsP *self) {
        self->SelectorShowEmptyFolders = self->subHandles[1]->GetBool("ShowEmptyFolders", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectorShowEmptyLibraries(MaterialParamsP *self) {
        self->SelectorShowEmptyLibraries = self->subHandles[1]->GetBool("ShowEmptyLibraries", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectorShowLegacy(MaterialParamsP *self) {
        self->SelectorShowLegacy = self->subHandles[1]->GetBool("ShowLegacy", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectorIconSize(MaterialParamsP *self) {
        self->SelectorIconSize = self->subHandles[1]->GetInt("IconSize", 64);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRecentMax(MaterialParamsP *self) {
        self->RecentMax = self->subHandles[2]->GetInt("RecentMax", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateModelsRecentMax(MaterialParamsP *self) {
        self->ModelsRecentMax = self->subHandles[3]->GetInt("RecentMax", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseBuiltInMaterials(MaterialParamsP *self) {
        self->UseBuiltInMaterials = self->subHandles[4]->GetBool("UseBuiltInMaterials", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseMaterialsFromWorkbenches(MaterialParamsP *self) {
        self->UseMaterialsFromWorkbenches = self->subHandles[4]->GetBool("UseMaterialsFromWorkbenches", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseMaterialsFromConfigDir(MaterialParamsP *self) {
        self->UseMaterialsFromConfigDir = self->subHandles[4]->GetBool("UseMaterialsFromConfigDir", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseMaterialsFromCustomDir(MaterialParamsP *self) {
        self->UseMaterialsFromCustomDir = self->subHandles[4]->GetBool("UseMaterialsFromCustomDir", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCustomMaterialsDir(MaterialParamsP *self) {
        self->CustomMaterialsDir = self->subHandles[4]->GetASCII("CustomMaterialsDir", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseExternal(MaterialParamsP *self) {
        self->UseExternal = self->subHandles[5]->GetBool("UseExternal", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalInterface(MaterialParamsP *self) {
        self->ExternalInterface = self->subHandles[5]->GetASCII("Current", "None");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateModelCacheSize(MaterialParamsP *self) {
        self->ModelCacheSize = self->subHandles[5]->GetInt("ModelCacheSize", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaterialCacheSize(MaterialParamsP *self) {
        self->MaterialCacheSize = self->subHandles[5]->GetInt("MaterialCacheSize", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditorWidth(MaterialParamsP *self) {
        self->EditorWidth = self->subHandles[0]->GetInt("EditorWidth", 835);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditorHeight(MaterialParamsP *self) {
        self->EditorHeight = self->subHandles[0]->GetInt("EditorHeight", 542);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFavoritesCount(MaterialParamsP *self) {
        self->FavoritesCount = self->subHandles[6]->GetInt("Favorites", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRecentCount(MaterialParamsP *self) {
        self->RecentCount = self->subHandles[2]->GetInt("Recent", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateModelsFavoritesCount(MaterialParamsP *self) {
        self->ModelsFavoritesCount = self->subHandles[7]->GetInt("Favorites", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateModelsRecentCount(MaterialParamsP *self) {
        self->ModelsRecentCount = self->subHandles[3]->GetInt("Recent", 0);
    }
};

// Auto generated code (Tools/params_utils.py:336)
MaterialParamsP *instance() {
    static MaterialParamsP *inst = new MaterialParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _MaterialParamsRegistrar({
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material", "DefaultMaterial", "DefaultMaterial", App::ParamInfo::String, "7f9fd73b-50c9-41d8-b7b2-575a030c1eeb")
        .setTitle("Default material")
        .setDoc("Material used wherever the default material is asked for, such as\n"
"for objects that have none of their own. Takes effect at the next\n"
"use."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Editor", "EditorShowFavorites", "ShowFavorites", App::ParamInfo::Bool, true)
        .setTitle("Show favourites (editor)")
        .setDoc("Shows the Favourites group in the tree of the materials editor.\n"
"Takes effect the next time the editor is opened."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Editor", "EditorShowRecent", "ShowRecent", App::ParamInfo::Bool, true)
        .setTitle("Show recent (editor)")
        .setDoc("Shows the group of recently used materials in the tree of the\n"
"materials editor. Takes effect the next time the editor is opened."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Editor", "EditorShowEmptyFolders", "ShowEmptyFolders", App::ParamInfo::Bool, false)
        .setTitle("Show empty folders (editor)")
        .setDoc("Shows folders that hold no material in the tree of the materials\n"
"editor. Takes effect the next time the editor is opened."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Editor", "EditorShowEmptyLibraries", "ShowEmptyLibraries", App::ParamInfo::Bool, true)
        .setTitle("Show empty libraries (editor)")
        .setDoc("Shows libraries that hold no material in the tree of the materials\n"
"editor. Takes effect the next time the editor is opened."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Editor", "EditorShowLegacy", "ShowLegacy", App::ParamInfo::Bool, false)
        .setTitle("Show legacy files (editor)")
        .setDoc("Shows material cards that are still in the old file format in the\n"
"tree of the materials editor. Takes effect the next time the\n"
"editor is opened."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget", "SelectorShowFavorites", "ShowFavorites", App::ParamInfo::Bool, true)
        .setTitle("Show favourites (selector)")
        .setDoc("Shows the Favourites group in the list of the material selector.\n"
"Takes effect the next time a material selector is created."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget", "SelectorShowRecent", "ShowRecent", App::ParamInfo::Bool, true)
        .setTitle("Show recent (selector)")
        .setDoc("Shows the group of recently used materials in the list of the\n"
"material selector. Takes effect the next time a material selector\n"
"is created."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget", "SelectorShowEmptyFolders", "ShowEmptyFolders", App::ParamInfo::Bool, false)
        .setTitle("Show empty folders (selector)")
        .setDoc("Shows folders that hold no material in the list of the material\n"
"selector. Takes effect the next time a material selector is\n"
"created."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget", "SelectorShowEmptyLibraries", "ShowEmptyLibraries", App::ParamInfo::Bool, true)
        .setTitle("Show empty libraries (selector)")
        .setDoc("Shows libraries that hold no material in the list of the material\n"
"selector. Takes effect the next time a material selector is\n"
"created."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget", "SelectorShowLegacy", "ShowLegacy", App::ParamInfo::Bool, false)
        .setTitle("Show legacy files (selector)")
        .setDoc("Shows material cards that are still in the old file format in the\n"
"list of the material selector. Takes effect the next time a\n"
"material selector is created."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget", "SelectorIconSize", "IconSize", App::ParamInfo::Int, 64)
        .setTitle("Material preview size")
        .setDoc("Size in pixels of the material previews in the list of the\n"
"material selector, kept within a legible range. Takes effect the\n"
"next time a material selector is created."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Recent", "RecentMax", "RecentMax", App::ParamInfo::Int, 5)
        .setTitle("Recent materials kept")
        .setDoc("Greatest number of recently used materials that is remembered.\n"
"Takes effect the next time the materials editor or a material\n"
"selector is opened."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Models/Recent", "ModelsRecentMax", "RecentMax", App::ParamInfo::Int, 5)
        .setTitle("Recent models kept")
        .setDoc("Greatest number of recently used material models that is\n"
"remembered. Takes effect the next time the model selection\n"
"dialogue is opened."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Resources", "UseBuiltInMaterials", "UseBuiltInMaterials", App::ParamInfo::Bool, true)
        .setTitle("Use built-in materials")
        .setDoc("Lists the material cards and models that come with FreeCAD. Takes\n"
"effect when the material libraries are next loaded."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Resources", "UseMaterialsFromWorkbenches", "UseMaterialsFromWorkbenches", App::ParamInfo::Bool, true)
        .setTitle("Use workbench materials")
        .setDoc("Lists the material cards and models added by external workbenches.\n"
"Takes effect when the material libraries are next loaded."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Resources", "UseMaterialsFromConfigDir", "UseMaterialsFromConfigDir", App::ParamInfo::Bool, true)
        .setTitle("Use user materials")
        .setDoc("Lists the material cards and models found in the Material and\n"
"Models folders of the user's FreeCAD data directory. Takes effect\n"
"when the material libraries are next loaded."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Resources", "UseMaterialsFromCustomDir", "UseMaterialsFromCustomDir", App::ParamInfo::Bool, true)
        .setTitle("Use custom directory")
        .setDoc("Lists the material cards and models found in the user-defined\n"
"directory. Takes effect when the material libraries are next\n"
"loaded."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Resources", "CustomMaterialsDir", "CustomMaterialsDir", App::ParamInfo::String, "")
        .setTitle("Custom materials directory")
        .setDoc("Directory with the user's own material cards. Takes effect when\n"
"the material libraries are next loaded."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/ExternalInterface", "UseExternal", "UseExternal", App::ParamInfo::Bool, false)
        .setTitle("Use external interface")
        .setDoc("Uses an external material interface in addition to the local\n"
"material libraries. Takes effect at once."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/ExternalInterface", "ExternalInterface", "Current", App::ParamInfo::String, "None")
        .setTitle("External interface")
        .setDoc("Name of the external material interface to use, or None. Takes\n"
"effect at once: the connection is made again when the value\n"
"changes."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/ExternalInterface", "ModelCacheSize", "ModelCacheSize", App::ParamInfo::Int, 100)
        .setTitle("Model cache size")
        .setDoc("Number of material models kept in the cache of the external\n"
"material interface. Takes effect after restart."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/ExternalInterface", "MaterialCacheSize", "MaterialCacheSize", App::ParamInfo::Int, 100)
        .setTitle("Material cache size")
        .setDoc("Number of materials kept in the cache of the external material\n"
"interface. Takes effect after restart."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Editor", "EditorWidth", "EditorWidth", App::ParamInfo::Int, 835)
        .setTitle("Materials editor: width")
        .setDoc("Width in pixels the materials editor last had. Stored when the\n"
"editor closes."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Editor", "EditorHeight", "EditorHeight", App::ParamInfo::Int, 542)
        .setTitle("Materials editor: height")
        .setDoc("Height in pixels the materials editor last had. Stored when the\n"
"editor closes."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Favorites", "FavoritesCount", "Favorites", App::ParamInfo::Int, 0)
        .setTitle("Favourite materials: count")
        .setDoc("How many favourite materials are kept, in the keys beside this\n"
"one. Stored by the program with the list."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Recent", "RecentCount", "Recent", App::ParamInfo::Int, 0)
        .setTitle("Recent materials: count")
        .setDoc("How many recent materials are kept, in the keys beside this one.\n"
"Stored by the program with the list."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Models/Favorites", "ModelsFavoritesCount", "Favorites", App::ParamInfo::Int, 0)
        .setTitle("Favourite material models: count")
        .setDoc("How many favourite material models are kept, in the keys beside\n"
"this one. Stored by the program with the list."),
    App::ParamInfo("Materials", "MaterialParams", "User parameter:BaseApp/Preferences/Mod/Material/Models/Recent", "ModelsRecentCount", "Recent", App::ParamInfo::Int, 0)
        .setTitle("Recent material models: count")
        .setDoc("How many recent material models are kept, in the keys beside this\n"
"one. Stored by the program with the list."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle MaterialParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docDefaultMaterial() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Material used wherever the default material is asked for, such as\n"
"for objects that have none of their own. Takes effect at the next\n"
"use.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MaterialParams::getDefaultMaterial() {
    return instance()->DefaultMaterial;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MaterialParams::defaultDefaultMaterial() {
    const static std::string def = "7f9fd73b-50c9-41d8-b7b2-575a030c1eeb";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setDefaultMaterial(const std::string &v) {
    instance()->handle->SetASCII("DefaultMaterial",v);
    instance()->DefaultMaterial = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeDefaultMaterial() {
    instance()->handle->RemoveASCII("DefaultMaterial");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docEditorShowFavorites() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows the Favourites group in the tree of the materials editor.\n"
"Takes effect the next time the editor is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getEditorShowFavorites() {
    return instance()->EditorShowFavorites;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultEditorShowFavorites() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setEditorShowFavorites(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowFavorites",v);
    instance()->EditorShowFavorites = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeEditorShowFavorites() {
    instance()->subHandles[0]->RemoveBool("ShowFavorites");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docEditorShowRecent() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows the group of recently used materials in the tree of the\n"
"materials editor. Takes effect the next time the editor is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getEditorShowRecent() {
    return instance()->EditorShowRecent;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultEditorShowRecent() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setEditorShowRecent(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowRecent",v);
    instance()->EditorShowRecent = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeEditorShowRecent() {
    instance()->subHandles[0]->RemoveBool("ShowRecent");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docEditorShowEmptyFolders() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows folders that hold no material in the tree of the materials\n"
"editor. Takes effect the next time the editor is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getEditorShowEmptyFolders() {
    return instance()->EditorShowEmptyFolders;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultEditorShowEmptyFolders() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setEditorShowEmptyFolders(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowEmptyFolders",v);
    instance()->EditorShowEmptyFolders = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeEditorShowEmptyFolders() {
    instance()->subHandles[0]->RemoveBool("ShowEmptyFolders");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docEditorShowEmptyLibraries() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows libraries that hold no material in the tree of the materials\n"
"editor. Takes effect the next time the editor is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getEditorShowEmptyLibraries() {
    return instance()->EditorShowEmptyLibraries;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultEditorShowEmptyLibraries() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setEditorShowEmptyLibraries(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowEmptyLibraries",v);
    instance()->EditorShowEmptyLibraries = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeEditorShowEmptyLibraries() {
    instance()->subHandles[0]->RemoveBool("ShowEmptyLibraries");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docEditorShowLegacy() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows material cards that are still in the old file format in the\n"
"tree of the materials editor. Takes effect the next time the\n"
"editor is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getEditorShowLegacy() {
    return instance()->EditorShowLegacy;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultEditorShowLegacy() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setEditorShowLegacy(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowLegacy",v);
    instance()->EditorShowLegacy = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeEditorShowLegacy() {
    instance()->subHandles[0]->RemoveBool("ShowLegacy");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docSelectorShowFavorites() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows the Favourites group in the list of the material selector.\n"
"Takes effect the next time a material selector is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getSelectorShowFavorites() {
    return instance()->SelectorShowFavorites;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultSelectorShowFavorites() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setSelectorShowFavorites(const bool &v) {
    instance()->subHandles[1]->SetBool("ShowFavorites",v);
    instance()->SelectorShowFavorites = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeSelectorShowFavorites() {
    instance()->subHandles[1]->RemoveBool("ShowFavorites");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docSelectorShowRecent() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows the group of recently used materials in the list of the\n"
"material selector. Takes effect the next time a material selector\n"
"is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getSelectorShowRecent() {
    return instance()->SelectorShowRecent;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultSelectorShowRecent() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setSelectorShowRecent(const bool &v) {
    instance()->subHandles[1]->SetBool("ShowRecent",v);
    instance()->SelectorShowRecent = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeSelectorShowRecent() {
    instance()->subHandles[1]->RemoveBool("ShowRecent");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docSelectorShowEmptyFolders() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows folders that hold no material in the list of the material\n"
"selector. Takes effect the next time a material selector is\n"
"created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getSelectorShowEmptyFolders() {
    return instance()->SelectorShowEmptyFolders;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultSelectorShowEmptyFolders() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setSelectorShowEmptyFolders(const bool &v) {
    instance()->subHandles[1]->SetBool("ShowEmptyFolders",v);
    instance()->SelectorShowEmptyFolders = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeSelectorShowEmptyFolders() {
    instance()->subHandles[1]->RemoveBool("ShowEmptyFolders");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docSelectorShowEmptyLibraries() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows libraries that hold no material in the list of the material\n"
"selector. Takes effect the next time a material selector is\n"
"created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getSelectorShowEmptyLibraries() {
    return instance()->SelectorShowEmptyLibraries;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultSelectorShowEmptyLibraries() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setSelectorShowEmptyLibraries(const bool &v) {
    instance()->subHandles[1]->SetBool("ShowEmptyLibraries",v);
    instance()->SelectorShowEmptyLibraries = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeSelectorShowEmptyLibraries() {
    instance()->subHandles[1]->RemoveBool("ShowEmptyLibraries");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docSelectorShowLegacy() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Shows material cards that are still in the old file format in the\n"
"list of the material selector. Takes effect the next time a\n"
"material selector is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getSelectorShowLegacy() {
    return instance()->SelectorShowLegacy;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultSelectorShowLegacy() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setSelectorShowLegacy(const bool &v) {
    instance()->subHandles[1]->SetBool("ShowLegacy",v);
    instance()->SelectorShowLegacy = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeSelectorShowLegacy() {
    instance()->subHandles[1]->RemoveBool("ShowLegacy");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docSelectorIconSize() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Size in pixels of the material previews in the list of the\n"
"material selector, kept within a legible range. Takes effect the\n"
"next time a material selector is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getSelectorIconSize() {
    return instance()->SelectorIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultSelectorIconSize() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setSelectorIconSize(const long &v) {
    instance()->subHandles[1]->SetInt("IconSize",v);
    instance()->SelectorIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeSelectorIconSize() {
    instance()->subHandles[1]->RemoveInt("IconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docRecentMax() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Greatest number of recently used materials that is remembered.\n"
"Takes effect the next time the materials editor or a material\n"
"selector is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getRecentMax() {
    return instance()->RecentMax;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultRecentMax() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setRecentMax(const long &v) {
    instance()->subHandles[2]->SetInt("RecentMax",v);
    instance()->RecentMax = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeRecentMax() {
    instance()->subHandles[2]->RemoveInt("RecentMax");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docModelsRecentMax() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Greatest number of recently used material models that is\n"
"remembered. Takes effect the next time the model selection\n"
"dialogue is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getModelsRecentMax() {
    return instance()->ModelsRecentMax;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultModelsRecentMax() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setModelsRecentMax(const long &v) {
    instance()->subHandles[3]->SetInt("RecentMax",v);
    instance()->ModelsRecentMax = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeModelsRecentMax() {
    instance()->subHandles[3]->RemoveInt("RecentMax");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docUseBuiltInMaterials() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Lists the material cards and models that come with FreeCAD. Takes\n"
"effect when the material libraries are next loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getUseBuiltInMaterials() {
    return instance()->UseBuiltInMaterials;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultUseBuiltInMaterials() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setUseBuiltInMaterials(const bool &v) {
    instance()->subHandles[4]->SetBool("UseBuiltInMaterials",v);
    instance()->UseBuiltInMaterials = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeUseBuiltInMaterials() {
    instance()->subHandles[4]->RemoveBool("UseBuiltInMaterials");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docUseMaterialsFromWorkbenches() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Lists the material cards and models added by external workbenches.\n"
"Takes effect when the material libraries are next loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getUseMaterialsFromWorkbenches() {
    return instance()->UseMaterialsFromWorkbenches;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultUseMaterialsFromWorkbenches() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setUseMaterialsFromWorkbenches(const bool &v) {
    instance()->subHandles[4]->SetBool("UseMaterialsFromWorkbenches",v);
    instance()->UseMaterialsFromWorkbenches = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeUseMaterialsFromWorkbenches() {
    instance()->subHandles[4]->RemoveBool("UseMaterialsFromWorkbenches");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docUseMaterialsFromConfigDir() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Lists the material cards and models found in the Material and\n"
"Models folders of the user's FreeCAD data directory. Takes effect\n"
"when the material libraries are next loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getUseMaterialsFromConfigDir() {
    return instance()->UseMaterialsFromConfigDir;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultUseMaterialsFromConfigDir() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setUseMaterialsFromConfigDir(const bool &v) {
    instance()->subHandles[4]->SetBool("UseMaterialsFromConfigDir",v);
    instance()->UseMaterialsFromConfigDir = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeUseMaterialsFromConfigDir() {
    instance()->subHandles[4]->RemoveBool("UseMaterialsFromConfigDir");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docUseMaterialsFromCustomDir() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Lists the material cards and models found in the user-defined\n"
"directory. Takes effect when the material libraries are next\n"
"loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getUseMaterialsFromCustomDir() {
    return instance()->UseMaterialsFromCustomDir;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultUseMaterialsFromCustomDir() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setUseMaterialsFromCustomDir(const bool &v) {
    instance()->subHandles[4]->SetBool("UseMaterialsFromCustomDir",v);
    instance()->UseMaterialsFromCustomDir = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeUseMaterialsFromCustomDir() {
    instance()->subHandles[4]->RemoveBool("UseMaterialsFromCustomDir");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docCustomMaterialsDir() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Directory with the user's own material cards. Takes effect when\n"
"the material libraries are next loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MaterialParams::getCustomMaterialsDir() {
    return instance()->CustomMaterialsDir;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MaterialParams::defaultCustomMaterialsDir() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setCustomMaterialsDir(const std::string &v) {
    instance()->subHandles[4]->SetASCII("CustomMaterialsDir",v);
    instance()->CustomMaterialsDir = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeCustomMaterialsDir() {
    instance()->subHandles[4]->RemoveASCII("CustomMaterialsDir");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docUseExternal() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Uses an external material interface in addition to the local\n"
"material libraries. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MaterialParams::getUseExternal() {
    return instance()->UseExternal;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MaterialParams::defaultUseExternal() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setUseExternal(const bool &v) {
    instance()->subHandles[5]->SetBool("UseExternal",v);
    instance()->UseExternal = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeUseExternal() {
    instance()->subHandles[5]->RemoveBool("UseExternal");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docExternalInterface() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Name of the external material interface to use, or None. Takes\n"
"effect at once: the connection is made again when the value\n"
"changes.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MaterialParams::getExternalInterface() {
    return instance()->ExternalInterface;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MaterialParams::defaultExternalInterface() {
    const static std::string def = "None";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setExternalInterface(const std::string &v) {
    instance()->subHandles[5]->SetASCII("Current",v);
    instance()->ExternalInterface = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeExternalInterface() {
    instance()->subHandles[5]->RemoveASCII("Current");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docModelCacheSize() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Number of material models kept in the cache of the external\n"
"material interface. Takes effect after restart.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getModelCacheSize() {
    return instance()->ModelCacheSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultModelCacheSize() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setModelCacheSize(const long &v) {
    instance()->subHandles[5]->SetInt("ModelCacheSize",v);
    instance()->ModelCacheSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeModelCacheSize() {
    instance()->subHandles[5]->RemoveInt("ModelCacheSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docMaterialCacheSize() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Number of materials kept in the cache of the external material\n"
"interface. Takes effect after restart.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getMaterialCacheSize() {
    return instance()->MaterialCacheSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultMaterialCacheSize() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setMaterialCacheSize(const long &v) {
    instance()->subHandles[5]->SetInt("MaterialCacheSize",v);
    instance()->MaterialCacheSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeMaterialCacheSize() {
    instance()->subHandles[5]->RemoveInt("MaterialCacheSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docEditorWidth() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Width in pixels the materials editor last had. Stored when the\n"
"editor closes.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getEditorWidth() {
    return instance()->EditorWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultEditorWidth() {
    const static long def = 835;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setEditorWidth(const long &v) {
    instance()->subHandles[0]->SetInt("EditorWidth",v);
    instance()->EditorWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeEditorWidth() {
    instance()->subHandles[0]->RemoveInt("EditorWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docEditorHeight() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"Height in pixels the materials editor last had. Stored when the\n"
"editor closes.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getEditorHeight() {
    return instance()->EditorHeight;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultEditorHeight() {
    const static long def = 542;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setEditorHeight(const long &v) {
    instance()->subHandles[0]->SetInt("EditorHeight",v);
    instance()->EditorHeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeEditorHeight() {
    instance()->subHandles[0]->RemoveInt("EditorHeight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docFavoritesCount() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"How many favourite materials are kept, in the keys beside this\n"
"one. Stored by the program with the list.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getFavoritesCount() {
    return instance()->FavoritesCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultFavoritesCount() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setFavoritesCount(const long &v) {
    instance()->subHandles[6]->SetInt("Favorites",v);
    instance()->FavoritesCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeFavoritesCount() {
    instance()->subHandles[6]->RemoveInt("Favorites");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docRecentCount() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"How many recent materials are kept, in the keys beside this one.\n"
"Stored by the program with the list.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getRecentCount() {
    return instance()->RecentCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultRecentCount() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setRecentCount(const long &v) {
    instance()->subHandles[2]->SetInt("Recent",v);
    instance()->RecentCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeRecentCount() {
    instance()->subHandles[2]->RemoveInt("Recent");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docModelsFavoritesCount() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"How many favourite material models are kept, in the keys beside\n"
"this one. Stored by the program with the list.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getModelsFavoritesCount() {
    return instance()->ModelsFavoritesCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultModelsFavoritesCount() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setModelsFavoritesCount(const long &v) {
    instance()->subHandles[7]->SetInt("Favorites",v);
    instance()->ModelsFavoritesCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeModelsFavoritesCount() {
    instance()->subHandles[7]->RemoveInt("Favorites");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MaterialParams::docModelsRecentCount() {
    return QT_TRANSLATE_NOOP("MaterialParams",
"How many recent material models are kept, in the keys beside this\n"
"one. Stored by the program with the list.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MaterialParams::getModelsRecentCount() {
    return instance()->ModelsRecentCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MaterialParams::defaultModelsRecentCount() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MaterialParams::setModelsRecentCount(const long &v) {
    instance()->subHandles[3]->SetInt("Recent",v);
    instance()->ModelsRecentCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MaterialParams::removeModelsRecentCount() {
    instance()->subHandles[3]->RemoveInt("Recent");
}
//[[[end]]]
