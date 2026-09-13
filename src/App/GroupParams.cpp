/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
import GroupParams
GroupParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "GroupParams.h"
using namespace App;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class GroupParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(GroupParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    bool ClaimAllChildren;
    bool KeepHiddenChildren;
    bool ExportChildren;
    bool CreateOrigin;
    bool GeoGroupAllowCrossLink;
    bool CreateGroupInGroup;

    // Auto generated code (Tools/params_utils.py:254)
    GroupParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Group");
        handle->Attach(this);

        ClaimAllChildren = this->handle->GetBool("ClaimAllChildren", true);
        funcs["ClaimAllChildren"] = &GroupParamsP::updateClaimAllChildren;
        KeepHiddenChildren = this->handle->GetBool("KeepHiddenChildren", true);
        funcs["KeepHiddenChildren"] = &GroupParamsP::updateKeepHiddenChildren;
        ExportChildren = this->handle->GetBool("ExportChildren", true);
        funcs["ExportChildren"] = &GroupParamsP::updateExportChildren;
        CreateOrigin = this->handle->GetBool("CreateOrigin", false);
        funcs["CreateOrigin"] = &GroupParamsP::updateCreateOrigin;
        GeoGroupAllowCrossLink = this->handle->GetBool("GeoGroupAllowCrossLink", false);
        funcs["GeoGroupAllowCrossLink"] = &GroupParamsP::updateGeoGroupAllowCrossLink;
        CreateGroupInGroup = this->handle->GetBool("CreateGroupInGroup", true);
        funcs["CreateGroupInGroup"] = &GroupParamsP::updateCreateGroupInGroup;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~GroupParamsP() override = default;

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
    static void updateClaimAllChildren(GroupParamsP *self) {
        self->ClaimAllChildren = self->handle->GetBool("ClaimAllChildren", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateKeepHiddenChildren(GroupParamsP *self) {
        self->KeepHiddenChildren = self->handle->GetBool("KeepHiddenChildren", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExportChildren(GroupParamsP *self) {
        self->ExportChildren = self->handle->GetBool("ExportChildren", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCreateOrigin(GroupParamsP *self) {
        self->CreateOrigin = self->handle->GetBool("CreateOrigin", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGeoGroupAllowCrossLink(GroupParamsP *self) {
        self->GeoGroupAllowCrossLink = self->handle->GetBool("GeoGroupAllowCrossLink", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCreateGroupInGroup(GroupParamsP *self) {
        self->CreateGroupInGroup = self->handle->GetBool("CreateGroupInGroup", true);
    }
};

// Auto generated code (Tools/params_utils.py:336)
GroupParamsP *instance() {
    static GroupParamsP *inst = new GroupParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _GroupParamsRegistrar({
    App::ParamInfo("App", "GroupParams", "User parameter:BaseApp/Preferences/Group", "ClaimAllChildren", "ClaimAllChildren", App::ParamInfo::Bool, true)
        .setTitle("Claim all children")
        .setDoc("Claim all children objects in tree view. If disabled, then only claim\n"
"children that are not claimed by other children."),
    App::ParamInfo("App", "GroupParams", "User parameter:BaseApp/Preferences/Group", "KeepHiddenChildren", "KeepHiddenChildren", App::ParamInfo::Bool, true)
        .setTitle("Remember hidden children")
        .setDoc("Remember invisible children objects and keep those objects hidden\n"
"when the group is made visible."),
    App::ParamInfo("App", "GroupParams", "User parameter:BaseApp/Preferences/Group", "ExportChildren", "ExportChildren", App::ParamInfo::Bool, true)
        .setTitle("Export children by visibility")
        .setDoc("Export visible children (e.g. when doing STEP export). Note, that once this option\n"
"is enabled, the group object will be touched when its child toggles visibility."),
    App::ParamInfo("App", "GroupParams", "User parameter:BaseApp/Preferences/Group", "CreateOrigin", "CreateOrigin", App::ParamInfo::Bool, false)
        .setTitle("Always create origin features in origin group")
        .setDoc("Create all origin features when the origin group is created. If Disabled\n"
"The origin features will only be created when the origin group is expanded\n"
"for the first time."),
    App::ParamInfo("App", "GroupParams", "User parameter:BaseApp/Preferences/Group", "GeoGroupAllowCrossLink", "GeoGroupAllowCrossLink", App::ParamInfo::Bool, false)
        .setTitle("Allow cross coordinate links in GeoFeatureGroup (App::Part)")
        .setDoc("Allow objects to be contained in more than one GeoFeatureGroup (e.g. App::Part).\n"
"If disabled, adding an object to one group will auto remove it from other groups.\n"
"WARNING! Disabling this option may produce an invalid group after changing its children."),
    App::ParamInfo("App", "GroupParams", "User parameter:BaseApp/Preferences/Group", "CreateGroupInGroup", "CreateGroupInGroup", App::ParamInfo::Bool, true)
        .setTitle("Create new group inside current selected group")
        .setDoc("This option only applies to creating a new group when there is a single\n"
"selected object that is also a group (either plain or App::Part).\n"
"\n"
"If the option is enabled, then the new group will be created inside the\n"
"selected group. If disabled, then the selected group will be moved into\n"
"the newly created group instead."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle GroupParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *GroupParams::docClaimAllChildren() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Claim all children objects in tree view. If disabled, then only claim\n"
"children that are not claimed by other children.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GroupParams::getClaimAllChildren() {
    return instance()->ClaimAllChildren;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GroupParams::defaultClaimAllChildren() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GroupParams::setClaimAllChildren(const bool &v) {
    instance()->handle->SetBool("ClaimAllChildren",v);
    instance()->ClaimAllChildren = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GroupParams::removeClaimAllChildren() {
    instance()->handle->RemoveBool("ClaimAllChildren");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GroupParams::docKeepHiddenChildren() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Remember invisible children objects and keep those objects hidden\n"
"when the group is made visible.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GroupParams::getKeepHiddenChildren() {
    return instance()->KeepHiddenChildren;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GroupParams::defaultKeepHiddenChildren() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GroupParams::setKeepHiddenChildren(const bool &v) {
    instance()->handle->SetBool("KeepHiddenChildren",v);
    instance()->KeepHiddenChildren = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GroupParams::removeKeepHiddenChildren() {
    instance()->handle->RemoveBool("KeepHiddenChildren");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GroupParams::docExportChildren() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Export visible children (e.g. when doing STEP export). Note, that once this option\n"
"is enabled, the group object will be touched when its child toggles visibility.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GroupParams::getExportChildren() {
    return instance()->ExportChildren;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GroupParams::defaultExportChildren() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GroupParams::setExportChildren(const bool &v) {
    instance()->handle->SetBool("ExportChildren",v);
    instance()->ExportChildren = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GroupParams::removeExportChildren() {
    instance()->handle->RemoveBool("ExportChildren");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GroupParams::docCreateOrigin() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Create all origin features when the origin group is created. If Disabled\n"
"The origin features will only be created when the origin group is expanded\n"
"for the first time.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GroupParams::getCreateOrigin() {
    return instance()->CreateOrigin;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GroupParams::defaultCreateOrigin() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GroupParams::setCreateOrigin(const bool &v) {
    instance()->handle->SetBool("CreateOrigin",v);
    instance()->CreateOrigin = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GroupParams::removeCreateOrigin() {
    instance()->handle->RemoveBool("CreateOrigin");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GroupParams::docGeoGroupAllowCrossLink() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Allow objects to be contained in more than one GeoFeatureGroup (e.g. App::Part).\n"
"If disabled, adding an object to one group will auto remove it from other groups.\n"
"WARNING! Disabling this option may produce an invalid group after changing its children.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GroupParams::getGeoGroupAllowCrossLink() {
    return instance()->GeoGroupAllowCrossLink;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GroupParams::defaultGeoGroupAllowCrossLink() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GroupParams::setGeoGroupAllowCrossLink(const bool &v) {
    instance()->handle->SetBool("GeoGroupAllowCrossLink",v);
    instance()->GeoGroupAllowCrossLink = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GroupParams::removeGeoGroupAllowCrossLink() {
    instance()->handle->RemoveBool("GeoGroupAllowCrossLink");
}

// Auto generated code (Tools/params_utils.py:397)
const char *GroupParams::docCreateGroupInGroup() {
    return QT_TRANSLATE_NOOP("GroupParams",
"This option only applies to creating a new group when there is a single\n"
"selected object that is also a group (either plain or App::Part).\n"
"\n"
"If the option is enabled, then the new group will be created inside the\n"
"selected group. If disabled, then the selected group will be moved into\n"
"the newly created group instead.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & GroupParams::getCreateGroupInGroup() {
    return instance()->CreateGroupInGroup;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & GroupParams::defaultCreateGroupInGroup() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void GroupParams::setCreateGroupInGroup(const bool &v) {
    instance()->handle->SetBool("CreateGroupInGroup",v);
    instance()->CreateGroupInGroup = v;
}

// Auto generated code (Tools/params_utils.py:431)
void GroupParams::removeCreateGroupInGroup() {
    instance()->handle->RemoveBool("CreateGroupInGroup");
}
//[[[end]]]

