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
import MeshParams
MeshParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "MeshParams.h"
using namespace Mesh;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class MeshParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(MeshParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    std::string AsymptoteWidth;
    std::string AsymptoteHeight;
    long DefaultShapeType;
    unsigned long MeshColor;
    unsigned long LineColor;
    long MeshTransparency;
    long LineTransparency;
    bool TwoSideRendering;
    bool VertexPerNormals;
    double CreaseAngle;
    std::string DisplayAliasFormatString;
    bool ShowBoundingBox;
    double MaxDeviationExport;
    long RenderTriangleLimit;
    bool CheckNonManifoldPoints;
    bool EnableFoldsCheck;
    bool StrictlyDegenerated;
    bool SubElementSelection;

    // Auto generated code (Tools/params_utils.py:254)
    MeshParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Mesh");
        handle->Attach(this);

        subHandles.resize(2);
        subHandles[0] = handle->GetGroup("Asymptote");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("Evaluation");
        subHandles[1]->Attach(this);
        AsymptoteWidth = this->subHandles[0]->GetASCII("Width", "500");
        funcs["AsymptoteWidth"] = &MeshParamsP::updateAsymptoteWidth;
        AsymptoteHeight = this->subHandles[0]->GetASCII("Height", "500");
        funcs["AsymptoteHeight"] = &MeshParamsP::updateAsymptoteHeight;
        DefaultShapeType = this->handle->GetInt("DefaultShapeType", 0);
        funcs["DefaultShapeType"] = &MeshParamsP::updateDefaultShapeType;
        MeshColor = this->handle->GetUnsigned("MeshColor", 0);
        funcs["MeshColor"] = &MeshParamsP::updateMeshColor;
        LineColor = this->handle->GetUnsigned("LineColor", 0);
        funcs["LineColor"] = &MeshParamsP::updateLineColor;
        MeshTransparency = this->handle->GetInt("MeshTransparency", 0);
        funcs["MeshTransparency"] = &MeshParamsP::updateMeshTransparency;
        LineTransparency = this->handle->GetInt("LineTransparency", 0);
        funcs["LineTransparency"] = &MeshParamsP::updateLineTransparency;
        TwoSideRendering = this->handle->GetBool("TwoSideRendering", false);
        funcs["TwoSideRendering"] = &MeshParamsP::updateTwoSideRendering;
        VertexPerNormals = this->handle->GetBool("VertexPerNormals", false);
        funcs["VertexPerNormals"] = &MeshParamsP::updateVertexPerNormals;
        CreaseAngle = this->handle->GetFloat("CreaseAngle", 0.0);
        funcs["CreaseAngle"] = &MeshParamsP::updateCreaseAngle;
        DisplayAliasFormatString = this->handle->GetASCII("DisplayAliasFormatString", "%V = %A");
        funcs["DisplayAliasFormatString"] = &MeshParamsP::updateDisplayAliasFormatString;
        ShowBoundingBox = this->handle->GetBool("ShowBoundingBox", false);
        funcs["ShowBoundingBox"] = &MeshParamsP::updateShowBoundingBox;
        MaxDeviationExport = this->handle->GetFloat("MaxDeviationExport", 0.1);
        funcs["MaxDeviationExport"] = &MeshParamsP::updateMaxDeviationExport;
        RenderTriangleLimit = this->handle->GetInt("RenderTriangleLimit", -1);
        funcs["RenderTriangleLimit"] = &MeshParamsP::updateRenderTriangleLimit;
        CheckNonManifoldPoints = this->subHandles[1]->GetBool("CheckNonManifoldPoints", false);
        funcs["CheckNonManifoldPoints"] = &MeshParamsP::updateCheckNonManifoldPoints;
        EnableFoldsCheck = this->subHandles[1]->GetBool("EnableFoldsCheck", false);
        funcs["EnableFoldsCheck"] = &MeshParamsP::updateEnableFoldsCheck;
        StrictlyDegenerated = this->subHandles[1]->GetBool("StrictlyDegenerated", true);
        funcs["StrictlyDegenerated"] = &MeshParamsP::updateStrictlyDegenerated;
        SubElementSelection = this->handle->GetBool("SubElementSelection", false);
        funcs["SubElementSelection"] = &MeshParamsP::updateSubElementSelection;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~MeshParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
    }


    // Auto generated code (Tools/params_utils.py:322)
    static void updateAsymptoteWidth(MeshParamsP *self) {
        auto v = self->subHandles[0]->GetASCII("Width", "500");
        if (self->AsymptoteWidth != v) {
            self->AsymptoteWidth = v;
            MeshParams::onAsymptoteWidthChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateAsymptoteHeight(MeshParamsP *self) {
        auto v = self->subHandles[0]->GetASCII("Height", "500");
        if (self->AsymptoteHeight != v) {
            self->AsymptoteHeight = v;
            MeshParams::onAsymptoteHeightChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultShapeType(MeshParamsP *self) {
        self->DefaultShapeType = self->handle->GetInt("DefaultShapeType", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMeshColor(MeshParamsP *self) {
        self->MeshColor = self->handle->GetUnsigned("MeshColor", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineColor(MeshParamsP *self) {
        self->LineColor = self->handle->GetUnsigned("LineColor", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMeshTransparency(MeshParamsP *self) {
        self->MeshTransparency = self->handle->GetInt("MeshTransparency", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineTransparency(MeshParamsP *self) {
        self->LineTransparency = self->handle->GetInt("LineTransparency", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTwoSideRendering(MeshParamsP *self) {
        self->TwoSideRendering = self->handle->GetBool("TwoSideRendering", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVertexPerNormals(MeshParamsP *self) {
        self->VertexPerNormals = self->handle->GetBool("VertexPerNormals", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCreaseAngle(MeshParamsP *self) {
        self->CreaseAngle = self->handle->GetFloat("CreaseAngle", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDisplayAliasFormatString(MeshParamsP *self) {
        self->DisplayAliasFormatString = self->handle->GetASCII("DisplayAliasFormatString", "%V = %A");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowBoundingBox(MeshParamsP *self) {
        self->ShowBoundingBox = self->handle->GetBool("ShowBoundingBox", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxDeviationExport(MeshParamsP *self) {
        self->MaxDeviationExport = self->handle->GetFloat("MaxDeviationExport", 0.1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderTriangleLimit(MeshParamsP *self) {
        self->RenderTriangleLimit = self->handle->GetInt("RenderTriangleLimit", -1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckNonManifoldPoints(MeshParamsP *self) {
        self->CheckNonManifoldPoints = self->subHandles[1]->GetBool("CheckNonManifoldPoints", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableFoldsCheck(MeshParamsP *self) {
        self->EnableFoldsCheck = self->subHandles[1]->GetBool("EnableFoldsCheck", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStrictlyDegenerated(MeshParamsP *self) {
        self->StrictlyDegenerated = self->subHandles[1]->GetBool("StrictlyDegenerated", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSubElementSelection(MeshParamsP *self) {
        self->SubElementSelection = self->handle->GetBool("SubElementSelection", false);
    }
};

// Auto generated code (Tools/params_utils.py:336)
MeshParamsP *instance() {
    static MeshParamsP *inst = new MeshParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _MeshParamsRegistrar({
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh/Asymptote", "AsymptoteWidth", "Width", App::ParamInfo::String, "500")
        .setTitle("Asymptote Width")
        .setOnChange(),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh/Asymptote", "AsymptoteHeight", "Height", App::ParamInfo::String, "500")
        .setTitle("Asymptote Height")
        .setOnChange(),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "DefaultShapeType", "DefaultShapeType", App::ParamInfo::Int, 0)
        .setTitle("Default Shape Type"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "MeshColor", "MeshColor", App::ParamInfo::UInt, 0)
        .setTitle("Mesh Color"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "LineColor", "LineColor", App::ParamInfo::UInt, 0)
        .setTitle("Line Color"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "MeshTransparency", "MeshTransparency", App::ParamInfo::Int, 0)
        .setTitle("Mesh Transparency"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "LineTransparency", "LineTransparency", App::ParamInfo::Int, 0)
        .setTitle("Line Transparency"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "TwoSideRendering", "TwoSideRendering", App::ParamInfo::Bool, false)
        .setTitle("Two Side Rendering"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "VertexPerNormals", "VertexPerNormals", App::ParamInfo::Bool, false)
        .setTitle("Vertex Per Normals"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "CreaseAngle", "CreaseAngle", App::ParamInfo::Float, 0.0)
        .setTitle("Crease Angle"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "DisplayAliasFormatString", "DisplayAliasFormatString", App::ParamInfo::String, "%V = %A")
        .setTitle("Display Alias Format String"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "ShowBoundingBox", "ShowBoundingBox", App::ParamInfo::Bool, false)
        .setTitle("Show Bounding Box"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "MaxDeviationExport", "MaxDeviationExport", App::ParamInfo::Float, 0.1)
        .setTitle("Max Deviation Export"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "RenderTriangleLimit", "RenderTriangleLimit", App::ParamInfo::Int, -1)
        .setTitle("Render Triangle Limit"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh/Evaluation", "CheckNonManifoldPoints", "CheckNonManifoldPoints", App::ParamInfo::Bool, false)
        .setTitle("Check Non Manifold Points"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh/Evaluation", "EnableFoldsCheck", "EnableFoldsCheck", App::ParamInfo::Bool, false)
        .setTitle("Enable Folds Check"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh/Evaluation", "StrictlyDegenerated", "StrictlyDegenerated", App::ParamInfo::Bool, true)
        .setTitle("Strictly Degenerated"),
    App::ParamInfo("Mesh", "MeshParams", "User parameter:BaseApp/Preferences/Mod/Mesh", "SubElementSelection", "SubElementSelection", App::ParamInfo::Bool, false)
        .setTitle("Sub Element Selection"),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle MeshParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docAsymptoteWidth() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MeshParams::getAsymptoteWidth() {
    return instance()->AsymptoteWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MeshParams::defaultAsymptoteWidth() {
    const static std::string def = "500";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setAsymptoteWidth(const std::string &v) {
    instance()->subHandles[0]->SetASCII("Width",v);
    instance()->AsymptoteWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeAsymptoteWidth() {
    instance()->subHandles[0]->RemoveASCII("AsymptoteWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docAsymptoteHeight() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MeshParams::getAsymptoteHeight() {
    return instance()->AsymptoteHeight;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MeshParams::defaultAsymptoteHeight() {
    const static std::string def = "500";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setAsymptoteHeight(const std::string &v) {
    instance()->subHandles[0]->SetASCII("Height",v);
    instance()->AsymptoteHeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeAsymptoteHeight() {
    instance()->subHandles[0]->RemoveASCII("AsymptoteHeight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docDefaultShapeType() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const long & MeshParams::getDefaultShapeType() {
    return instance()->DefaultShapeType;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MeshParams::defaultDefaultShapeType() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setDefaultShapeType(const long &v) {
    instance()->handle->SetInt("DefaultShapeType",v);
    instance()->DefaultShapeType = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeDefaultShapeType() {
    instance()->handle->RemoveInt("DefaultShapeType");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docMeshColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & MeshParams::getMeshColor() {
    return instance()->MeshColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & MeshParams::defaultMeshColor() {
    const static unsigned long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setMeshColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("MeshColor",v);
    instance()->MeshColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeMeshColor() {
    instance()->handle->RemoveUnsigned("MeshColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docLineColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & MeshParams::getLineColor() {
    return instance()->LineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & MeshParams::defaultLineColor() {
    const static unsigned long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setLineColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("LineColor",v);
    instance()->LineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeLineColor() {
    instance()->handle->RemoveUnsigned("LineColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docMeshTransparency() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const long & MeshParams::getMeshTransparency() {
    return instance()->MeshTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MeshParams::defaultMeshTransparency() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setMeshTransparency(const long &v) {
    instance()->handle->SetInt("MeshTransparency",v);
    instance()->MeshTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeMeshTransparency() {
    instance()->handle->RemoveInt("MeshTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docLineTransparency() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const long & MeshParams::getLineTransparency() {
    return instance()->LineTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MeshParams::defaultLineTransparency() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setLineTransparency(const long &v) {
    instance()->handle->SetInt("LineTransparency",v);
    instance()->LineTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeLineTransparency() {
    instance()->handle->RemoveInt("LineTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docTwoSideRendering() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MeshParams::getTwoSideRendering() {
    return instance()->TwoSideRendering;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MeshParams::defaultTwoSideRendering() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setTwoSideRendering(const bool &v) {
    instance()->handle->SetBool("TwoSideRendering",v);
    instance()->TwoSideRendering = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeTwoSideRendering() {
    instance()->handle->RemoveBool("TwoSideRendering");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docVertexPerNormals() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MeshParams::getVertexPerNormals() {
    return instance()->VertexPerNormals;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MeshParams::defaultVertexPerNormals() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setVertexPerNormals(const bool &v) {
    instance()->handle->SetBool("VertexPerNormals",v);
    instance()->VertexPerNormals = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeVertexPerNormals() {
    instance()->handle->RemoveBool("VertexPerNormals");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docCreaseAngle() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const double & MeshParams::getCreaseAngle() {
    return instance()->CreaseAngle;
}

// Auto generated code (Tools/params_utils.py:413)
const double & MeshParams::defaultCreaseAngle() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setCreaseAngle(const double &v) {
    instance()->handle->SetFloat("CreaseAngle",v);
    instance()->CreaseAngle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeCreaseAngle() {
    instance()->handle->RemoveFloat("CreaseAngle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docDisplayAliasFormatString() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MeshParams::getDisplayAliasFormatString() {
    return instance()->DisplayAliasFormatString;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MeshParams::defaultDisplayAliasFormatString() {
    const static std::string def = "%V = %A";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setDisplayAliasFormatString(const std::string &v) {
    instance()->handle->SetASCII("DisplayAliasFormatString",v);
    instance()->DisplayAliasFormatString = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeDisplayAliasFormatString() {
    instance()->handle->RemoveASCII("DisplayAliasFormatString");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docShowBoundingBox() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MeshParams::getShowBoundingBox() {
    return instance()->ShowBoundingBox;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MeshParams::defaultShowBoundingBox() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setShowBoundingBox(const bool &v) {
    instance()->handle->SetBool("ShowBoundingBox",v);
    instance()->ShowBoundingBox = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeShowBoundingBox() {
    instance()->handle->RemoveBool("ShowBoundingBox");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docMaxDeviationExport() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const double & MeshParams::getMaxDeviationExport() {
    return instance()->MaxDeviationExport;
}

// Auto generated code (Tools/params_utils.py:413)
const double & MeshParams::defaultMaxDeviationExport() {
    const static double def = 0.1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setMaxDeviationExport(const double &v) {
    instance()->handle->SetFloat("MaxDeviationExport",v);
    instance()->MaxDeviationExport = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeMaxDeviationExport() {
    instance()->handle->RemoveFloat("MaxDeviationExport");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docRenderTriangleLimit() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const long & MeshParams::getRenderTriangleLimit() {
    return instance()->RenderTriangleLimit;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MeshParams::defaultRenderTriangleLimit() {
    const static long def = -1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setRenderTriangleLimit(const long &v) {
    instance()->handle->SetInt("RenderTriangleLimit",v);
    instance()->RenderTriangleLimit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeRenderTriangleLimit() {
    instance()->handle->RemoveInt("RenderTriangleLimit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docCheckNonManifoldPoints() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MeshParams::getCheckNonManifoldPoints() {
    return instance()->CheckNonManifoldPoints;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MeshParams::defaultCheckNonManifoldPoints() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setCheckNonManifoldPoints(const bool &v) {
    instance()->subHandles[1]->SetBool("CheckNonManifoldPoints",v);
    instance()->CheckNonManifoldPoints = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeCheckNonManifoldPoints() {
    instance()->subHandles[1]->RemoveBool("CheckNonManifoldPoints");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docEnableFoldsCheck() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MeshParams::getEnableFoldsCheck() {
    return instance()->EnableFoldsCheck;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MeshParams::defaultEnableFoldsCheck() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setEnableFoldsCheck(const bool &v) {
    instance()->subHandles[1]->SetBool("EnableFoldsCheck",v);
    instance()->EnableFoldsCheck = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeEnableFoldsCheck() {
    instance()->subHandles[1]->RemoveBool("EnableFoldsCheck");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docStrictlyDegenerated() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MeshParams::getStrictlyDegenerated() {
    return instance()->StrictlyDegenerated;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MeshParams::defaultStrictlyDegenerated() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setStrictlyDegenerated(const bool &v) {
    instance()->subHandles[1]->SetBool("StrictlyDegenerated",v);
    instance()->StrictlyDegenerated = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeStrictlyDegenerated() {
    instance()->subHandles[1]->RemoveBool("StrictlyDegenerated");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MeshParams::docSubElementSelection() {
    return "";
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MeshParams::getSubElementSelection() {
    return instance()->SubElementSelection;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MeshParams::defaultSubElementSelection() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MeshParams::setSubElementSelection(const bool &v) {
    instance()->handle->SetBool("SubElementSelection",v);
    instance()->SubElementSelection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MeshParams::removeSubElementSelection() {
    instance()->handle->RemoveBool("SubElementSelection");
}
//[[[end]]]
