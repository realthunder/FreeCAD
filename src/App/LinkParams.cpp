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

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "LinkParams.h"
using namespace App;

// Auto generated code (Tools/params_utils.py:210)
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

    // Auto generated code (Tools/params_utils.py:254)
    LinkParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Link");
        handle->Attach(this);

        HideScaleVector = this->handle->GetBool("HideScaleVector", true);
        funcs["HideScaleVector"] = &LinkParamsP::updateHideScaleVector;
        CreateInPlace = this->handle->GetBool("CreateInPlace", true);
        funcs["CreateInPlace"] = &LinkParamsP::updateCreateInPlace;
        CreateInContainer = this->handle->GetBool("CreateInContainer", true);
        funcs["CreateInContainer"] = &LinkParamsP::updateCreateInContainer;
        ActiveContainerKey = this->handle->GetASCII("ActiveContainerKey", "");
        funcs["ActiveContainerKey"] = &LinkParamsP::updateActiveContainerKey;
        CopyOnChangeApplyToAll = this->handle->GetBool("CopyOnChangeApplyToAll", true);
        funcs["CopyOnChangeApplyToAll"] = &LinkParamsP::updateCopyOnChangeApplyToAll;
        ShowElement = this->handle->GetBool("ShowElement", true);
        funcs["ShowElement"] = &LinkParamsP::updateShowElement;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~LinkParamsP() override = default;

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
    static void updateHideScaleVector(LinkParamsP *self) {
        self->HideScaleVector = self->handle->GetBool("HideScaleVector", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCreateInPlace(LinkParamsP *self) {
        self->CreateInPlace = self->handle->GetBool("CreateInPlace", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCreateInContainer(LinkParamsP *self) {
        self->CreateInContainer = self->handle->GetBool("CreateInContainer", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateActiveContainerKey(LinkParamsP *self) {
        self->ActiveContainerKey = self->handle->GetASCII("ActiveContainerKey", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCopyOnChangeApplyToAll(LinkParamsP *self) {
        self->CopyOnChangeApplyToAll = self->handle->GetBool("CopyOnChangeApplyToAll", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowElement(LinkParamsP *self) {
        self->ShowElement = self->handle->GetBool("ShowElement", true);
    }
};

// Auto generated code (Tools/params_utils.py:336)
LinkParamsP *instance() {
    static LinkParamsP *inst = new LinkParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _LinkParamsRegistrar({
    App::ParamInfo("App", "LinkParams", "User parameter:BaseApp/Preferences/Link", "HideScaleVector", "HideScaleVector", App::ParamInfo::Bool, true)
        .setTitle("Hide Scale Vector"),
    App::ParamInfo("App", "LinkParams", "User parameter:BaseApp/Preferences/Link", "CreateInPlace", "CreateInPlace", App::ParamInfo::Bool, true)
        .setTitle("Create In Place"),
    App::ParamInfo("App", "LinkParams", "User parameter:BaseApp/Preferences/Link", "CreateInContainer", "CreateInContainer", App::ParamInfo::Bool, true)
        .setTitle("Create In Container"),
    App::ParamInfo("App", "LinkParams", "User parameter:BaseApp/Preferences/Link", "ActiveContainerKey", "ActiveContainerKey", App::ParamInfo::String, "")
        .setTitle("Active Container Key"),
    App::ParamInfo("App", "LinkParams", "User parameter:BaseApp/Preferences/Link", "CopyOnChangeApplyToAll", "CopyOnChangeApplyToAll", App::ParamInfo::Bool, true)
        .setTitle("Copy On Change Apply To All")
        .setDoc("Stores the last user choice of whether to apply CopyOnChange setup to all link\n"
"that links to the same configurable object"),
    App::ParamInfo("App", "LinkParams", "User parameter:BaseApp/Preferences/Link", "ShowElement", "ShowElement", App::ParamInfo::Bool, true)
        .setTitle("Show array element in Link array")
        .setDoc("Default value of the \"ShowElement\" property in an App::Link object,\n"
"which specifies whether to show the link array element as individual\n"
"object in the tree view."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle LinkParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *LinkParams::docHideScaleVector() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & LinkParams::getHideScaleVector() {
    return instance()->HideScaleVector;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & LinkParams::defaultHideScaleVector() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void LinkParams::setHideScaleVector(const bool &v) {
    instance()->handle->SetBool("HideScaleVector",v);
    instance()->HideScaleVector = v;
}

// Auto generated code (Tools/params_utils.py:431)
void LinkParams::removeHideScaleVector() {
    instance()->handle->RemoveBool("HideScaleVector");
}

// Auto generated code (Tools/params_utils.py:397)
const char *LinkParams::docCreateInPlace() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & LinkParams::getCreateInPlace() {
    return instance()->CreateInPlace;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & LinkParams::defaultCreateInPlace() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void LinkParams::setCreateInPlace(const bool &v) {
    instance()->handle->SetBool("CreateInPlace",v);
    instance()->CreateInPlace = v;
}

// Auto generated code (Tools/params_utils.py:431)
void LinkParams::removeCreateInPlace() {
    instance()->handle->RemoveBool("CreateInPlace");
}

// Auto generated code (Tools/params_utils.py:397)
const char *LinkParams::docCreateInContainer() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & LinkParams::getCreateInContainer() {
    return instance()->CreateInContainer;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & LinkParams::defaultCreateInContainer() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void LinkParams::setCreateInContainer(const bool &v) {
    instance()->handle->SetBool("CreateInContainer",v);
    instance()->CreateInContainer = v;
}

// Auto generated code (Tools/params_utils.py:431)
void LinkParams::removeCreateInContainer() {
    instance()->handle->RemoveBool("CreateInContainer");
}

// Auto generated code (Tools/params_utils.py:397)
const char *LinkParams::docActiveContainerKey() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & LinkParams::getActiveContainerKey() {
    return instance()->ActiveContainerKey;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & LinkParams::defaultActiveContainerKey() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void LinkParams::setActiveContainerKey(const std::string &v) {
    instance()->handle->SetASCII("ActiveContainerKey",v);
    instance()->ActiveContainerKey = v;
}

// Auto generated code (Tools/params_utils.py:431)
void LinkParams::removeActiveContainerKey() {
    instance()->handle->RemoveASCII("ActiveContainerKey");
}

// Auto generated code (Tools/params_utils.py:397)
const char *LinkParams::docCopyOnChangeApplyToAll() {
    return QT_TRANSLATE_NOOP("LinkParams",
"Stores the last user choice of whether to apply CopyOnChange setup to all link\n"
"that links to the same configurable object");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & LinkParams::getCopyOnChangeApplyToAll() {
    return instance()->CopyOnChangeApplyToAll;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & LinkParams::defaultCopyOnChangeApplyToAll() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void LinkParams::setCopyOnChangeApplyToAll(const bool &v) {
    instance()->handle->SetBool("CopyOnChangeApplyToAll",v);
    instance()->CopyOnChangeApplyToAll = v;
}

// Auto generated code (Tools/params_utils.py:431)
void LinkParams::removeCopyOnChangeApplyToAll() {
    instance()->handle->RemoveBool("CopyOnChangeApplyToAll");
}

// Auto generated code (Tools/params_utils.py:397)
const char *LinkParams::docShowElement() {
    return QT_TRANSLATE_NOOP("LinkParams",
"Default value of the \"ShowElement\" property in an App::Link object,\n"
"which specifies whether to show the link array element as individual\n"
"object in the tree view.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & LinkParams::getShowElement() {
    return instance()->ShowElement;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & LinkParams::defaultShowElement() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void LinkParams::setShowElement(const bool &v) {
    instance()->handle->SetBool("ShowElement",v);
    instance()->ShowElement = v;
}

// Auto generated code (Tools/params_utils.py:431)
void LinkParams::removeShowElement() {
    instance()->handle->RemoveBool("ShowElement");
}
//[[[end]]]

