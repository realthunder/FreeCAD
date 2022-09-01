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

// Auto generated code (Tools/params_utils.py:166)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include "GroupParams.h"
using namespace App;

// Auto generated code (Tools/params_utils.py:175)
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

    // Auto generated code (Tools/params_utils.py:203)
    GroupParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Group");
        handle->Attach(this);

        ClaimAllChildren = handle->GetBool("ClaimAllChildren", true);
        funcs["ClaimAllChildren"] = &GroupParamsP::updateClaimAllChildren;
        KeepHiddenChildren = handle->GetBool("KeepHiddenChildren", true);
        funcs["KeepHiddenChildren"] = &GroupParamsP::updateKeepHiddenChildren;
        ExportChildren = handle->GetBool("ExportChildren", true);
        funcs["ExportChildren"] = &GroupParamsP::updateExportChildren;
        CreateOrigin = handle->GetBool("CreateOrigin", false);
        funcs["CreateOrigin"] = &GroupParamsP::updateCreateOrigin;
        GeoGroupAllowCrossLink = handle->GetBool("GeoGroupAllowCrossLink", false);
        funcs["GeoGroupAllowCrossLink"] = &GroupParamsP::updateGeoGroupAllowCrossLink;
        CreateGroupInGroup = handle->GetBool("CreateGroupInGroup", true);
        funcs["CreateGroupInGroup"] = &GroupParamsP::updateCreateGroupInGroup;
    }

    // Auto generated code (Tools/params_utils.py:217)
    ~GroupParamsP() {
    }

    // Auto generated code (Tools/params_utils.py:222)
    void OnChange(Base::Subject<const char*> &, const char* sReason) {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        
    }


    // Auto generated code (Tools/params_utils.py:238)
    static void updateClaimAllChildren(GroupParamsP *self) {
        self->ClaimAllChildren = self->handle->GetBool("ClaimAllChildren", true);
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateKeepHiddenChildren(GroupParamsP *self) {
        self->KeepHiddenChildren = self->handle->GetBool("KeepHiddenChildren", true);
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateExportChildren(GroupParamsP *self) {
        self->ExportChildren = self->handle->GetBool("ExportChildren", true);
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateCreateOrigin(GroupParamsP *self) {
        self->CreateOrigin = self->handle->GetBool("CreateOrigin", false);
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateGeoGroupAllowCrossLink(GroupParamsP *self) {
        self->GeoGroupAllowCrossLink = self->handle->GetBool("GeoGroupAllowCrossLink", false);
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateCreateGroupInGroup(GroupParamsP *self) {
        self->CreateGroupInGroup = self->handle->GetBool("CreateGroupInGroup", true);
    }
};

// Auto generated code (Tools/params_utils.py:256)
GroupParamsP *instance() {
    static GroupParamsP *inst = new GroupParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:265)
ParameterGrp::handle GroupParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:288)
const char *GroupParams::docClaimAllChildren() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Claim all children objects in tree view. If disabled, then only claim\n"
"children that are not claimed by other children.");
}

// Auto generated code (Tools/params_utils.py:294)
const bool & GroupParams::getClaimAllChildren() {
    return instance()->ClaimAllChildren;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & GroupParams::defaultClaimAllChildren() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void GroupParams::setClaimAllChildren(const bool &v) {
    instance()->handle->SetBool("ClaimAllChildren",v);
    instance()->ClaimAllChildren = v;
}

// Auto generated code (Tools/params_utils.py:314)
void GroupParams::removeClaimAllChildren() {
    instance()->handle->RemoveBool("ClaimAllChildren");
}

// Auto generated code (Tools/params_utils.py:288)
const char *GroupParams::docKeepHiddenChildren() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Remember invisible children objects and keep those objects hidden\n"
"when the group is made visible.");
}

// Auto generated code (Tools/params_utils.py:294)
const bool & GroupParams::getKeepHiddenChildren() {
    return instance()->KeepHiddenChildren;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & GroupParams::defaultKeepHiddenChildren() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void GroupParams::setKeepHiddenChildren(const bool &v) {
    instance()->handle->SetBool("KeepHiddenChildren",v);
    instance()->KeepHiddenChildren = v;
}

// Auto generated code (Tools/params_utils.py:314)
void GroupParams::removeKeepHiddenChildren() {
    instance()->handle->RemoveBool("KeepHiddenChildren");
}

// Auto generated code (Tools/params_utils.py:288)
const char *GroupParams::docExportChildren() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Export visible children (e.g. when doing STEP export). Note, that once this option\n"
"is enabled, the group object will be touched when its child toggles visibility.");
}

// Auto generated code (Tools/params_utils.py:294)
const bool & GroupParams::getExportChildren() {
    return instance()->ExportChildren;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & GroupParams::defaultExportChildren() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void GroupParams::setExportChildren(const bool &v) {
    instance()->handle->SetBool("ExportChildren",v);
    instance()->ExportChildren = v;
}

// Auto generated code (Tools/params_utils.py:314)
void GroupParams::removeExportChildren() {
    instance()->handle->RemoveBool("ExportChildren");
}

// Auto generated code (Tools/params_utils.py:288)
const char *GroupParams::docCreateOrigin() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Create all origin features when the origin group is created. If Disabled\n"
"The origin features will only be created when the origin group is expanded\n"
"for the first time.");
}

// Auto generated code (Tools/params_utils.py:294)
const bool & GroupParams::getCreateOrigin() {
    return instance()->CreateOrigin;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & GroupParams::defaultCreateOrigin() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void GroupParams::setCreateOrigin(const bool &v) {
    instance()->handle->SetBool("CreateOrigin",v);
    instance()->CreateOrigin = v;
}

// Auto generated code (Tools/params_utils.py:314)
void GroupParams::removeCreateOrigin() {
    instance()->handle->RemoveBool("CreateOrigin");
}

// Auto generated code (Tools/params_utils.py:288)
const char *GroupParams::docGeoGroupAllowCrossLink() {
    return QT_TRANSLATE_NOOP("GroupParams",
"Allow objects to be contained in more than one GeoFeatureGroup (e.g. App::Part).\n"
"If disabled, adding an object to one group will auto remove it from other groups.\n"
"WARNING! Disabling this option may produce an invalid group after changing its children.");
}

// Auto generated code (Tools/params_utils.py:294)
const bool & GroupParams::getGeoGroupAllowCrossLink() {
    return instance()->GeoGroupAllowCrossLink;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & GroupParams::defaultGeoGroupAllowCrossLink() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void GroupParams::setGeoGroupAllowCrossLink(const bool &v) {
    instance()->handle->SetBool("GeoGroupAllowCrossLink",v);
    instance()->GeoGroupAllowCrossLink = v;
}

// Auto generated code (Tools/params_utils.py:314)
void GroupParams::removeGeoGroupAllowCrossLink() {
    instance()->handle->RemoveBool("GeoGroupAllowCrossLink");
}

// Auto generated code (Tools/params_utils.py:288)
const char *GroupParams::docCreateGroupInGroup() {
    return QT_TRANSLATE_NOOP("GroupParams",
"This option only applies to creating a new group when there is a single\n"
"selected object that is also a group (either plain or App::Part).\n"
"\n"
"If the option is enabled, then the new group will be created inside the\n"
"selected group. If disabled, then the selected group will be moved into\n"
"the newly created group instead.");
}

// Auto generated code (Tools/params_utils.py:294)
const bool & GroupParams::getCreateGroupInGroup() {
    return instance()->CreateGroupInGroup;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & GroupParams::defaultCreateGroupInGroup() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void GroupParams::setCreateGroupInGroup(const bool &v) {
    instance()->handle->SetBool("CreateGroupInGroup",v);
    instance()->CreateGroupInGroup = v;
}

// Auto generated code (Tools/params_utils.py:314)
void GroupParams::removeCreateGroupInGroup() {
    instance()->handle->RemoveBool("CreateGroupInGroup");
}
//[[[end]]]

