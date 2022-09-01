/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
import LinkParams
LinkParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:166)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include "LinkParams.h"
using namespace App;

// Auto generated code (Tools/params_utils.py:175)
namespace {
class LinkParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(LinkParamsP*),App::CStringHasher,App::CStringHasher> funcs;

    bool HideScaleVector;
    bool CreateInPlace;
    bool CreateInContainer;
    std::string ActiveContainerKey;
    bool CopyOnChangeApplyToAll;
    bool ShowElement;

    // Auto generated code (Tools/params_utils.py:203)
    LinkParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Link");
        handle->Attach(this);

        HideScaleVector = handle->GetBool("HideScaleVector", true);
        funcs["HideScaleVector"] = &LinkParamsP::updateHideScaleVector;
        CreateInPlace = handle->GetBool("CreateInPlace", true);
        funcs["CreateInPlace"] = &LinkParamsP::updateCreateInPlace;
        CreateInContainer = handle->GetBool("CreateInContainer", true);
        funcs["CreateInContainer"] = &LinkParamsP::updateCreateInContainer;
        ActiveContainerKey = handle->GetASCII("ActiveContainerKey", "");
        funcs["ActiveContainerKey"] = &LinkParamsP::updateActiveContainerKey;
        CopyOnChangeApplyToAll = handle->GetBool("CopyOnChangeApplyToAll", true);
        funcs["CopyOnChangeApplyToAll"] = &LinkParamsP::updateCopyOnChangeApplyToAll;
        ShowElement = handle->GetBool("ShowElement", true);
        funcs["ShowElement"] = &LinkParamsP::updateShowElement;
    }

    // Auto generated code (Tools/params_utils.py:217)
    ~LinkParamsP() {
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
    static void updateHideScaleVector(LinkParamsP *self) {
        self->HideScaleVector = self->handle->GetBool("HideScaleVector", true);
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateCreateInPlace(LinkParamsP *self) {
        self->CreateInPlace = self->handle->GetBool("CreateInPlace", true);
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateCreateInContainer(LinkParamsP *self) {
        self->CreateInContainer = self->handle->GetBool("CreateInContainer", true);
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateActiveContainerKey(LinkParamsP *self) {
        self->ActiveContainerKey = self->handle->GetASCII("ActiveContainerKey", "");
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateCopyOnChangeApplyToAll(LinkParamsP *self) {
        self->CopyOnChangeApplyToAll = self->handle->GetBool("CopyOnChangeApplyToAll", true);
    }
    // Auto generated code (Tools/params_utils.py:238)
    static void updateShowElement(LinkParamsP *self) {
        self->ShowElement = self->handle->GetBool("ShowElement", true);
    }
};

// Auto generated code (Tools/params_utils.py:256)
LinkParamsP *instance() {
    static LinkParamsP *inst = new LinkParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:265)
ParameterGrp::handle LinkParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:288)
const char *LinkParams::docHideScaleVector() {
    return "";
}

// Auto generated code (Tools/params_utils.py:294)
const bool & LinkParams::getHideScaleVector() {
    return instance()->HideScaleVector;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & LinkParams::defaultHideScaleVector() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void LinkParams::setHideScaleVector(const bool &v) {
    instance()->handle->SetBool("HideScaleVector",v);
    instance()->HideScaleVector = v;
}

// Auto generated code (Tools/params_utils.py:314)
void LinkParams::removeHideScaleVector() {
    instance()->handle->RemoveBool("HideScaleVector");
}

// Auto generated code (Tools/params_utils.py:288)
const char *LinkParams::docCreateInPlace() {
    return "";
}

// Auto generated code (Tools/params_utils.py:294)
const bool & LinkParams::getCreateInPlace() {
    return instance()->CreateInPlace;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & LinkParams::defaultCreateInPlace() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void LinkParams::setCreateInPlace(const bool &v) {
    instance()->handle->SetBool("CreateInPlace",v);
    instance()->CreateInPlace = v;
}

// Auto generated code (Tools/params_utils.py:314)
void LinkParams::removeCreateInPlace() {
    instance()->handle->RemoveBool("CreateInPlace");
}

// Auto generated code (Tools/params_utils.py:288)
const char *LinkParams::docCreateInContainer() {
    return "";
}

// Auto generated code (Tools/params_utils.py:294)
const bool & LinkParams::getCreateInContainer() {
    return instance()->CreateInContainer;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & LinkParams::defaultCreateInContainer() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void LinkParams::setCreateInContainer(const bool &v) {
    instance()->handle->SetBool("CreateInContainer",v);
    instance()->CreateInContainer = v;
}

// Auto generated code (Tools/params_utils.py:314)
void LinkParams::removeCreateInContainer() {
    instance()->handle->RemoveBool("CreateInContainer");
}

// Auto generated code (Tools/params_utils.py:288)
const char *LinkParams::docActiveContainerKey() {
    return "";
}

// Auto generated code (Tools/params_utils.py:294)
const std::string & LinkParams::getActiveContainerKey() {
    return instance()->ActiveContainerKey;
}

// Auto generated code (Tools/params_utils.py:300)
const std::string & LinkParams::defaultActiveContainerKey() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void LinkParams::setActiveContainerKey(const std::string &v) {
    instance()->handle->SetASCII("ActiveContainerKey",v);
    instance()->ActiveContainerKey = v;
}

// Auto generated code (Tools/params_utils.py:314)
void LinkParams::removeActiveContainerKey() {
    instance()->handle->RemoveASCII("ActiveContainerKey");
}

// Auto generated code (Tools/params_utils.py:288)
const char *LinkParams::docCopyOnChangeApplyToAll() {
    return "";
}

// Auto generated code (Tools/params_utils.py:294)
const bool & LinkParams::getCopyOnChangeApplyToAll() {
    return instance()->CopyOnChangeApplyToAll;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & LinkParams::defaultCopyOnChangeApplyToAll() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void LinkParams::setCopyOnChangeApplyToAll(const bool &v) {
    instance()->handle->SetBool("CopyOnChangeApplyToAll",v);
    instance()->CopyOnChangeApplyToAll = v;
}

// Auto generated code (Tools/params_utils.py:314)
void LinkParams::removeCopyOnChangeApplyToAll() {
    instance()->handle->RemoveBool("CopyOnChangeApplyToAll");
}

// Auto generated code (Tools/params_utils.py:288)
const char *LinkParams::docShowElement() {
    return QT_TRANSLATE_NOOP("LinkParams",
"Default value of the \"ShowElement\" property in an App::Link object,\n"
"which specifies whether to show the link array element as individual\n"
"object in the tree view.");
}

// Auto generated code (Tools/params_utils.py:294)
const bool & LinkParams::getShowElement() {
    return instance()->ShowElement;
}

// Auto generated code (Tools/params_utils.py:300)
const bool & LinkParams::defaultShowElement() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:307)
void LinkParams::setShowElement(const bool &v) {
    instance()->handle->SetBool("ShowElement",v);
    instance()->ShowElement = v;
}

// Auto generated code (Tools/params_utils.py:314)
void LinkParams::removeShowElement() {
    instance()->handle->RemoveBool("ShowElement");
}
//[[[end]]]

