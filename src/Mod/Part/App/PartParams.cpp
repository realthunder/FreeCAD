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
import PartParams
PartParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "PartParams.h"
using namespace Part;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class PartParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(PartParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    bool ShapePropertyCopy;
    bool DisableShapeCache;
    long CommandOverride;
    long EnableWrapFeature;
    bool CopySubShape;
    bool UseBrepToolsOuterWire;
    bool UseBaseObjectName;
    bool AutoGroupSolids;
    bool SingleSolid;
    bool UsePipeForExtrusionDraft;
    bool LinearizeExtrusionDraft;
    bool AutoCorrectLink;
    bool RefineModel;
    bool AuxGroupUniqueLabel;
    bool SplitEllipsoid;
    long ParallelRunThreshold;
    bool AutoValidateShape;
    bool FixShape;
    bool ShareStoredSubShapes;
    long BorrowBelowFace;
    unsigned long LoftMaxDegree;
    long WarnUnnamedInput;
    double MinimumDeviation;
    double MeshDeviation;
    double MeshAngularDeflection;
    double MinimumAngularDeflection;
    bool BooleanRefineModel;
    bool BooleanCheckModel;
    double BooleanFuzzy;
    bool AutoElementMap;

    // Auto generated code (Tools/params_utils.py:254)
    PartParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Part");
        handle->Attach(this);

        subHandles.resize(2);
        subHandles[0] = handle->GetGroup("Boolean");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("General");
        subHandles[1]->Attach(this);
        ShapePropertyCopy = this->handle->GetBool("ShapePropertyCopy", false);
        funcs["ShapePropertyCopy"] = &PartParamsP::updateShapePropertyCopy;
        DisableShapeCache = this->handle->GetBool("DisableShapeCache", false);
        funcs["DisableShapeCache"] = &PartParamsP::updateDisableShapeCache;
        CommandOverride = this->handle->GetInt("CommandOverride", 2);
        funcs["CommandOverride"] = &PartParamsP::updateCommandOverride;
        EnableWrapFeature = this->handle->GetInt("EnableWrapFeature", 2);
        funcs["EnableWrapFeature"] = &PartParamsP::updateEnableWrapFeature;
        CopySubShape = this->handle->GetBool("CopySubShape", false);
        funcs["CopySubShape"] = &PartParamsP::updateCopySubShape;
        UseBrepToolsOuterWire = this->handle->GetBool("UseBrepToolsOuterWire", true);
        funcs["UseBrepToolsOuterWire"] = &PartParamsP::updateUseBrepToolsOuterWire;
        UseBaseObjectName = this->handle->GetBool("UseBaseObjectName", false);
        funcs["UseBaseObjectName"] = &PartParamsP::updateUseBaseObjectName;
        AutoGroupSolids = this->handle->GetBool("AutoGroupSolids", false);
        funcs["AutoGroupSolids"] = &PartParamsP::updateAutoGroupSolids;
        SingleSolid = this->handle->GetBool("SingleSolid", false);
        funcs["SingleSolid"] = &PartParamsP::updateSingleSolid;
        UsePipeForExtrusionDraft = this->handle->GetBool("UsePipeForExtrusionDraft", false);
        funcs["UsePipeForExtrusionDraft"] = &PartParamsP::updateUsePipeForExtrusionDraft;
        LinearizeExtrusionDraft = this->handle->GetBool("LinearizeExtrusionDraft", true);
        funcs["LinearizeExtrusionDraft"] = &PartParamsP::updateLinearizeExtrusionDraft;
        AutoCorrectLink = this->handle->GetBool("AutoCorrectLink", false);
        funcs["AutoCorrectLink"] = &PartParamsP::updateAutoCorrectLink;
        RefineModel = this->handle->GetBool("RefineModel", false);
        funcs["RefineModel"] = &PartParamsP::updateRefineModel;
        AuxGroupUniqueLabel = this->handle->GetBool("AuxGroupUniqueLabel", false);
        funcs["AuxGroupUniqueLabel"] = &PartParamsP::updateAuxGroupUniqueLabel;
        SplitEllipsoid = this->handle->GetBool("SplitEllipsoid", true);
        funcs["SplitEllipsoid"] = &PartParamsP::updateSplitEllipsoid;
        ParallelRunThreshold = this->handle->GetInt("ParallelRunThreshold", 100);
        funcs["ParallelRunThreshold"] = &PartParamsP::updateParallelRunThreshold;
        AutoValidateShape = this->handle->GetBool("AutoValidateShape", false);
        funcs["AutoValidateShape"] = &PartParamsP::updateAutoValidateShape;
        FixShape = this->handle->GetBool("FixShape", false);
        funcs["FixShape"] = &PartParamsP::updateFixShape;
        ShareStoredSubShapes = this->handle->GetBool("ShareStoredSubShapes", true);
        funcs["ShareStoredSubShapes"] = &PartParamsP::updateShareStoredSubShapes;
        BorrowBelowFace = this->handle->GetInt("BorrowBelowFace", 0);
        funcs["BorrowBelowFace"] = &PartParamsP::updateBorrowBelowFace;
        LoftMaxDegree = this->handle->GetUnsigned("LoftMaxDegree", 5);
        funcs["LoftMaxDegree"] = &PartParamsP::updateLoftMaxDegree;
        WarnUnnamedInput = this->handle->GetInt("WarnUnnamedInput", 0);
        funcs["WarnUnnamedInput"] = &PartParamsP::updateWarnUnnamedInput;
        MinimumDeviation = this->handle->GetFloat("MinimumDeviation", 0.05);
        funcs["MinimumDeviation"] = &PartParamsP::updateMinimumDeviation;
        MeshDeviation = this->handle->GetFloat("MeshDeviation", 0.2);
        funcs["MeshDeviation"] = &PartParamsP::updateMeshDeviation;
        MeshAngularDeflection = this->handle->GetFloat("MeshAngularDeflection", 28.65);
        funcs["MeshAngularDeflection"] = &PartParamsP::updateMeshAngularDeflection;
        MinimumAngularDeflection = this->handle->GetFloat("MinimumAngularDeflection", 5.0);
        funcs["MinimumAngularDeflection"] = &PartParamsP::updateMinimumAngularDeflection;
        BooleanRefineModel = this->subHandles[0]->GetBool("RefineModel", false);
        funcs["RefineModel"] = &PartParamsP::updateBooleanRefineModel;
        BooleanCheckModel = this->subHandles[0]->GetBool("CheckModel", false);
        funcs["CheckModel"] = &PartParamsP::updateBooleanCheckModel;
        BooleanFuzzy = this->subHandles[0]->GetFloat("BooleanFuzzy", 10.0);
        funcs["BooleanFuzzy"] = &PartParamsP::updateBooleanFuzzy;
        AutoElementMap = this->subHandles[1]->GetBool("AutoElementMap", true);
        funcs["AutoElementMap"] = &PartParamsP::updateAutoElementMap;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~PartParamsP() override = default;

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
    static void updateShapePropertyCopy(PartParamsP *self) {
        self->ShapePropertyCopy = self->handle->GetBool("ShapePropertyCopy", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDisableShapeCache(PartParamsP *self) {
        self->DisableShapeCache = self->handle->GetBool("DisableShapeCache", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCommandOverride(PartParamsP *self) {
        self->CommandOverride = self->handle->GetInt("CommandOverride", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableWrapFeature(PartParamsP *self) {
        self->EnableWrapFeature = self->handle->GetInt("EnableWrapFeature", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCopySubShape(PartParamsP *self) {
        self->CopySubShape = self->handle->GetBool("CopySubShape", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseBrepToolsOuterWire(PartParamsP *self) {
        self->UseBrepToolsOuterWire = self->handle->GetBool("UseBrepToolsOuterWire", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseBaseObjectName(PartParamsP *self) {
        self->UseBaseObjectName = self->handle->GetBool("UseBaseObjectName", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoGroupSolids(PartParamsP *self) {
        self->AutoGroupSolids = self->handle->GetBool("AutoGroupSolids", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSingleSolid(PartParamsP *self) {
        self->SingleSolid = self->handle->GetBool("SingleSolid", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUsePipeForExtrusionDraft(PartParamsP *self) {
        self->UsePipeForExtrusionDraft = self->handle->GetBool("UsePipeForExtrusionDraft", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLinearizeExtrusionDraft(PartParamsP *self) {
        self->LinearizeExtrusionDraft = self->handle->GetBool("LinearizeExtrusionDraft", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoCorrectLink(PartParamsP *self) {
        self->AutoCorrectLink = self->handle->GetBool("AutoCorrectLink", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRefineModel(PartParamsP *self) {
        self->RefineModel = self->handle->GetBool("RefineModel", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAuxGroupUniqueLabel(PartParamsP *self) {
        self->AuxGroupUniqueLabel = self->handle->GetBool("AuxGroupUniqueLabel", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSplitEllipsoid(PartParamsP *self) {
        self->SplitEllipsoid = self->handle->GetBool("SplitEllipsoid", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateParallelRunThreshold(PartParamsP *self) {
        self->ParallelRunThreshold = self->handle->GetInt("ParallelRunThreshold", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoValidateShape(PartParamsP *self) {
        self->AutoValidateShape = self->handle->GetBool("AutoValidateShape", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFixShape(PartParamsP *self) {
        self->FixShape = self->handle->GetBool("FixShape", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShareStoredSubShapes(PartParamsP *self) {
        self->ShareStoredSubShapes = self->handle->GetBool("ShareStoredSubShapes", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBorrowBelowFace(PartParamsP *self) {
        self->BorrowBelowFace = self->handle->GetInt("BorrowBelowFace", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLoftMaxDegree(PartParamsP *self) {
        self->LoftMaxDegree = self->handle->GetUnsigned("LoftMaxDegree", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWarnUnnamedInput(PartParamsP *self) {
        self->WarnUnnamedInput = self->handle->GetInt("WarnUnnamedInput", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMinimumDeviation(PartParamsP *self) {
        self->MinimumDeviation = self->handle->GetFloat("MinimumDeviation", 0.05);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMeshDeviation(PartParamsP *self) {
        self->MeshDeviation = self->handle->GetFloat("MeshDeviation", 0.2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMeshAngularDeflection(PartParamsP *self) {
        self->MeshAngularDeflection = self->handle->GetFloat("MeshAngularDeflection", 28.65);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMinimumAngularDeflection(PartParamsP *self) {
        self->MinimumAngularDeflection = self->handle->GetFloat("MinimumAngularDeflection", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBooleanRefineModel(PartParamsP *self) {
        self->BooleanRefineModel = self->subHandles[0]->GetBool("RefineModel", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBooleanCheckModel(PartParamsP *self) {
        self->BooleanCheckModel = self->subHandles[0]->GetBool("CheckModel", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBooleanFuzzy(PartParamsP *self) {
        self->BooleanFuzzy = self->subHandles[0]->GetFloat("BooleanFuzzy", 10.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoElementMap(PartParamsP *self) {
        self->AutoElementMap = self->subHandles[1]->GetBool("AutoElementMap", true);
    }
};

// Auto generated code (Tools/params_utils.py:336)
PartParamsP *instance() {
    static PartParamsP *inst = new PartParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _PartParamsRegistrar({
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "ShapePropertyCopy", "ShapePropertyCopy", App::ParamInfo::Bool, false)
        .setTitle("Shape Property Copy")
        .setDoc("Make a full geometric copy whenever a shape property is copied,\n"
"instead of sharing the shape. Uses much more memory on complex\n"
"models."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "DisableShapeCache", "DisableShapeCache", App::ParamInfo::Bool, false)
        .setTitle("Disable Shape Cache")
        .setDoc("Do not keep the shapes computed for an object and its sub-objects\n"
"for reuse. They are rebuilt on every request, which is slower;\n"
"meant for troubleshooting."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "CommandOverride", "CommandOverride", App::ParamInfo::Int, 2)
        .setTitle("Command Override")
        .setDoc("Run the PartDesign equivalent when a Part command is used with a\n"
"PartDesign body active or one of its features selected. 0 never,\n"
"1 always, 2 ask each time."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "EnableWrapFeature", "EnableWrapFeature", App::ParamInfo::Int, 2)
        .setTitle("Enable Wrap Feature")
        .setDoc("Bring a non-PartDesign object that references features of the\n"
"active body into that body through a wrap feature. 0 never,\n"
"1 always, 2 ask each time."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "CopySubShape", "CopySubShape", App::ParamInfo::Bool, false)
        .setTitle("Copy Sub Shape")
        .setDoc("Copy the geometry when a placed sub-shape of an object is handed\n"
"to Python, instead of only moving it. Slower, but avoids kernel\n"
"errors on some transformed shapes."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "UseBrepToolsOuterWire", "UseBrepToolsOuterWire", App::ParamInfo::Bool, true)
        .setTitle("Use Brep Tools Outer Wire")
        .setDoc("Find the outer wire of a face in Python (Face.OuterWire) with the\n"
"kernel's BRepTools. When off its ShapeAnalysis is used; the two\n"
"can differ on unusual faces."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "UseBaseObjectName", "UseBaseObjectName", App::ParamInfo::Bool, false)
        .setTitle("Use Base Object Name")
        .setDoc("Label a new body after the object selected as its base feature.\n"
"The question asked when the body is created has the same checkbox."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "AutoGroupSolids", "AutoGroupSolids", App::ParamInfo::Bool, false)
        .setTitle("Auto Group Solids")
        .setDoc("Turn on Auto Group Solids in new bodies, which groups the features\n"
"of each solid under its latest feature."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "SingleSolid", "SingleSolid", App::ParamInfo::Bool, false)
        .setTitle("Single Solid")
        .setDoc("Turn on Single Solid in new bodies, so that every feature must\n"
"result in one solid."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "UsePipeForExtrusionDraft", "UsePipeForExtrusionDraft", App::ParamInfo::Bool, false)
        .setTitle("Use Pipe For Extrusion Draft")
        .setDoc("Build the draft angle of new pads, pockets and Part extrusions\n"
"with a sweep instead of a loft. Each object keeps its own switch."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "LinearizeExtrusionDraft", "LinearizeExtrusionDraft", App::ParamInfo::Bool, true)
        .setTitle("Linearize Extrusion Draft")
        .setDoc("Turn flat spline faces into planes and straight spline edges into\n"
"lines in new lofts, sweeps and drafted extrusions, in Part and\n"
"PartDesign. Each object keeps its own switch."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "AutoCorrectLink", "AutoCorrectLink", App::ParamInfo::Bool, false)
        .setTitle("Auto Correct Link")
        .setDoc("While a PartDesign feature is edited, replace a reference it is\n"
"given by a sub-shape binder imported into the body automatically."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "RefineModel", "RefineModel", App::ParamInfo::Bool, false)
        .setTitle("Refine Model")
        .setDoc("Turn on Refine in new sub-shape binders, which merges faces lying\n"
"on the same surface. Part booleans and PartDesign features have\n"
"their own settings."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "AuxGroupUniqueLabel", "AuxGroupUniqueLabel", App::ParamInfo::Bool, false)
        .setTitle("Aux Group Unique Label")
        .setDoc("Give the Sketches, Datums and Misc groups of each body a unique\n"
"label such as Datums001. When off they can all carry the same\n"
"label."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "SplitEllipsoid", "SplitEllipsoid", App::ParamInfo::Bool, true)
        .setTitle("Split Ellipsoid")
        .setDoc("Turn on Split in new ellipsoids, which cuts the surface in the\n"
"middle to avoid errors in later boolean operations."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "ParallelRunThreshold", "ParallelRunThreshold", App::ParamInfo::Int, 100)
        .setTitle("Parallel Run Threshold")
        .setDoc("Run boolean operations on several processor threads. Any value\n"
"above 0 turns this on, 0 or less turns it off."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "AutoValidateShape", "AutoValidateShape", App::ParamInfo::Bool, false)
        .setTitle("Auto Validate Shape")
        .setDoc("Turn on Validate Shape in new PartDesign features. An invalid\n"
"result then gets a warning icon in the tree. Can slow down complex\n"
"models."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "FixShape", "FixShape", App::ParamInfo::Bool, false)
        .setTitle("Fix Shape")
        .setDoc("Set Fix Shape to Enabled in new Part objects, so that a result\n"
"found invalid is repaired. When off new objects are left as they\n"
"are computed."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "ShareStoredSubShapes", "ShareStoredSubShapes", App::ParamInfo::Bool, true)
        .setTitle("Share Stored Sub Shapes")
        .setDoc("Let a stored shape borrow a sub-shape from another object's file instead\n"
"of writing its geometry again (docs/SharedShapeStorage.md sec 12.4).\n"
"Turning this off writes every file whole, which is what the format did\n"
"before external references; the files stay readable either way."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "BorrowBelowFace", "BorrowBelowFace", App::ParamInfo::Int, 0)
        .setTitle("Borrow Below Face")
        .setDoc("Which sub-shapes a shape file may borrow from another below the level\n"
"of a shell, as a sum: 1 a face in a shell, 2 an edge in a face or\n"
"wire, 4 a vertex in an edge. 0, the default, none."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "LoftMaxDegree", "LoftMaxDegree", App::ParamInfo::UInt, 5)
        .setTitle("Loft Max Degree")
        .setDoc("Maximum surface degree given to new PartDesign lofts. Kept between\n"
"2 and the highest degree the kernel supports."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "WarnUnnamedInput", "WarnUnnamedInput", App::ParamInfo::Int, 0)
        .setTitle("Warn Unnamed Input")
        .setDoc("Report shape operations whose inputs carry no element names, so the\n"
"result cannot be named either. For workbench developers. 0 off, 1 once\n"
"per operation and recompute, 2 every occurrence."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MinimumDeviation", "MinimumDeviation", App::ParamInfo::Float, 0.05)
        .setTitle("Minimum Deviation")
        .setDoc("Lower limit of the tessellation deviation of shapes, in percent of\n"
"the object size. Objects asking for a finer mesh are drawn with\n"
"this value instead."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MeshDeviation", "MeshDeviation", App::ParamInfo::Float, 0.2)
        .setTitle("Mesh Deviation")
        .setDoc("Accuracy of the mesh that shapes are drawn with, as the largest\n"
"deviation in percent of the object size. Lower is finer and\n"
"slower. Sets the Deviation of new objects; a change is applied to\n"
"all open objects."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MeshAngularDeflection", "MeshAngularDeflection", App::ParamInfo::Float, 28.65)
        .setTitle("Mesh Angular Deflection")
        .setDoc("Largest angle between neighbouring segments of the mesh that\n"
"shapes are drawn with, in degrees. Lower is smoother and slower.\n"
"Sets the Angular Deflection of new objects; a change is applied to\n"
"all open objects."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MinimumAngularDeflection", "MinimumAngularDeflection", App::ParamInfo::Float, 5.0)
        .setTitle("Minimum Angular Deflection")
        .setDoc("Lower limit of the angular deflection used to mesh shapes, in\n"
"degrees. Objects asking for a smaller angle are drawn with this\n"
"value instead."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/Boolean", "BooleanRefineModel", "RefineModel", App::ParamInfo::Bool, false)
        .setTitle("Refine model after Boolean operation")
        .setDoc("New Part Boolean features get Refine switched on: faces that lie\n"
"on the same surface are merged after the operation. Read when a\n"
"feature is created."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/Boolean", "BooleanCheckModel", "CheckModel", App::ParamInfo::Bool, false)
        .setTitle("Check model after Boolean operation")
        .setDoc("Check the result of every Part Boolean operation for validity,\n"
"and fail the feature when it is not valid."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/Boolean", "BooleanFuzzy", "BooleanFuzzy", App::ParamInfo::Float, 10.0)
        .setTitle("Automatic Boolean fuzzy factor")
        .setDoc("Factor of the tolerance a Boolean operation is given when it is\n"
"told to choose one itself: this times the size of the shapes times\n"
"the kernel's precision."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/General", "AutoElementMap", "AutoElementMap", App::ParamInfo::Bool, true)
        .setTitle("Build element names for imported shapes")
        .setDoc("Give a shape that arrives without element names -- read from a\n"
"file, set by a script -- names of its own. Read once, at the first\n"
"such shape of a session."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle PartParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docShapePropertyCopy() {
    return QT_TRANSLATE_NOOP("PartParams",
"Make a full geometric copy whenever a shape property is copied,\n"
"instead of sharing the shape. Uses much more memory on complex\n"
"models.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getShapePropertyCopy() {
    return instance()->ShapePropertyCopy;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultShapePropertyCopy() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setShapePropertyCopy(const bool &v) {
    instance()->handle->SetBool("ShapePropertyCopy",v);
    instance()->ShapePropertyCopy = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeShapePropertyCopy() {
    instance()->handle->RemoveBool("ShapePropertyCopy");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDisableShapeCache() {
    return QT_TRANSLATE_NOOP("PartParams",
"Do not keep the shapes computed for an object and its sub-objects\n"
"for reuse. They are rebuilt on every request, which is slower;\n"
"meant for troubleshooting.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getDisableShapeCache() {
    return instance()->DisableShapeCache;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultDisableShapeCache() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDisableShapeCache(const bool &v) {
    instance()->handle->SetBool("DisableShapeCache",v);
    instance()->DisableShapeCache = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDisableShapeCache() {
    instance()->handle->RemoveBool("DisableShapeCache");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCommandOverride() {
    return QT_TRANSLATE_NOOP("PartParams",
"Run the PartDesign equivalent when a Part command is used with a\n"
"PartDesign body active or one of its features selected. 0 never,\n"
"1 always, 2 ask each time.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getCommandOverride() {
    return instance()->CommandOverride;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultCommandOverride() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCommandOverride(const long &v) {
    instance()->handle->SetInt("CommandOverride",v);
    instance()->CommandOverride = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCommandOverride() {
    instance()->handle->RemoveInt("CommandOverride");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docEnableWrapFeature() {
    return QT_TRANSLATE_NOOP("PartParams",
"Bring a non-PartDesign object that references features of the\n"
"active body into that body through a wrap feature. 0 never,\n"
"1 always, 2 ask each time.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getEnableWrapFeature() {
    return instance()->EnableWrapFeature;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultEnableWrapFeature() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setEnableWrapFeature(const long &v) {
    instance()->handle->SetInt("EnableWrapFeature",v);
    instance()->EnableWrapFeature = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeEnableWrapFeature() {
    instance()->handle->RemoveInt("EnableWrapFeature");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCopySubShape() {
    return QT_TRANSLATE_NOOP("PartParams",
"Copy the geometry when a placed sub-shape of an object is handed\n"
"to Python, instead of only moving it. Slower, but avoids kernel\n"
"errors on some transformed shapes.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCopySubShape() {
    return instance()->CopySubShape;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCopySubShape() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCopySubShape(const bool &v) {
    instance()->handle->SetBool("CopySubShape",v);
    instance()->CopySubShape = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCopySubShape() {
    instance()->handle->RemoveBool("CopySubShape");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docUseBrepToolsOuterWire() {
    return QT_TRANSLATE_NOOP("PartParams",
"Find the outer wire of a face in Python (Face.OuterWire) with the\n"
"kernel's BRepTools. When off its ShapeAnalysis is used; the two\n"
"can differ on unusual faces.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getUseBrepToolsOuterWire() {
    return instance()->UseBrepToolsOuterWire;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultUseBrepToolsOuterWire() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setUseBrepToolsOuterWire(const bool &v) {
    instance()->handle->SetBool("UseBrepToolsOuterWire",v);
    instance()->UseBrepToolsOuterWire = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeUseBrepToolsOuterWire() {
    instance()->handle->RemoveBool("UseBrepToolsOuterWire");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docUseBaseObjectName() {
    return QT_TRANSLATE_NOOP("PartParams",
"Label a new body after the object selected as its base feature.\n"
"The question asked when the body is created has the same checkbox.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getUseBaseObjectName() {
    return instance()->UseBaseObjectName;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultUseBaseObjectName() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setUseBaseObjectName(const bool &v) {
    instance()->handle->SetBool("UseBaseObjectName",v);
    instance()->UseBaseObjectName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeUseBaseObjectName() {
    instance()->handle->RemoveBool("UseBaseObjectName");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docAutoGroupSolids() {
    return QT_TRANSLATE_NOOP("PartParams",
"Turn on Auto Group Solids in new bodies, which groups the features\n"
"of each solid under its latest feature.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getAutoGroupSolids() {
    return instance()->AutoGroupSolids;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultAutoGroupSolids() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setAutoGroupSolids(const bool &v) {
    instance()->handle->SetBool("AutoGroupSolids",v);
    instance()->AutoGroupSolids = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeAutoGroupSolids() {
    instance()->handle->RemoveBool("AutoGroupSolids");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docSingleSolid() {
    return QT_TRANSLATE_NOOP("PartParams",
"Turn on Single Solid in new bodies, so that every feature must\n"
"result in one solid.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getSingleSolid() {
    return instance()->SingleSolid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultSingleSolid() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setSingleSolid(const bool &v) {
    instance()->handle->SetBool("SingleSolid",v);
    instance()->SingleSolid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeSingleSolid() {
    instance()->handle->RemoveBool("SingleSolid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docUsePipeForExtrusionDraft() {
    return QT_TRANSLATE_NOOP("PartParams",
"Build the draft angle of new pads, pockets and Part extrusions\n"
"with a sweep instead of a loft. Each object keeps its own switch.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getUsePipeForExtrusionDraft() {
    return instance()->UsePipeForExtrusionDraft;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultUsePipeForExtrusionDraft() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setUsePipeForExtrusionDraft(const bool &v) {
    instance()->handle->SetBool("UsePipeForExtrusionDraft",v);
    instance()->UsePipeForExtrusionDraft = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeUsePipeForExtrusionDraft() {
    instance()->handle->RemoveBool("UsePipeForExtrusionDraft");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docLinearizeExtrusionDraft() {
    return QT_TRANSLATE_NOOP("PartParams",
"Turn flat spline faces into planes and straight spline edges into\n"
"lines in new lofts, sweeps and drafted extrusions, in Part and\n"
"PartDesign. Each object keeps its own switch.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getLinearizeExtrusionDraft() {
    return instance()->LinearizeExtrusionDraft;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultLinearizeExtrusionDraft() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setLinearizeExtrusionDraft(const bool &v) {
    instance()->handle->SetBool("LinearizeExtrusionDraft",v);
    instance()->LinearizeExtrusionDraft = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeLinearizeExtrusionDraft() {
    instance()->handle->RemoveBool("LinearizeExtrusionDraft");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docAutoCorrectLink() {
    return QT_TRANSLATE_NOOP("PartParams",
"While a PartDesign feature is edited, replace a reference it is\n"
"given by a sub-shape binder imported into the body automatically.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getAutoCorrectLink() {
    return instance()->AutoCorrectLink;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultAutoCorrectLink() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setAutoCorrectLink(const bool &v) {
    instance()->handle->SetBool("AutoCorrectLink",v);
    instance()->AutoCorrectLink = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeAutoCorrectLink() {
    instance()->handle->RemoveBool("AutoCorrectLink");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docRefineModel() {
    return QT_TRANSLATE_NOOP("PartParams",
"Turn on Refine in new sub-shape binders, which merges faces lying\n"
"on the same surface. Part booleans and PartDesign features have\n"
"their own settings.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getRefineModel() {
    return instance()->RefineModel;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultRefineModel() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setRefineModel(const bool &v) {
    instance()->handle->SetBool("RefineModel",v);
    instance()->RefineModel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeRefineModel() {
    instance()->handle->RemoveBool("RefineModel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docAuxGroupUniqueLabel() {
    return QT_TRANSLATE_NOOP("PartParams",
"Give the Sketches, Datums and Misc groups of each body a unique\n"
"label such as Datums001. When off they can all carry the same\n"
"label.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getAuxGroupUniqueLabel() {
    return instance()->AuxGroupUniqueLabel;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultAuxGroupUniqueLabel() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setAuxGroupUniqueLabel(const bool &v) {
    instance()->handle->SetBool("AuxGroupUniqueLabel",v);
    instance()->AuxGroupUniqueLabel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeAuxGroupUniqueLabel() {
    instance()->handle->RemoveBool("AuxGroupUniqueLabel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docSplitEllipsoid() {
    return QT_TRANSLATE_NOOP("PartParams",
"Turn on Split in new ellipsoids, which cuts the surface in the\n"
"middle to avoid errors in later boolean operations.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getSplitEllipsoid() {
    return instance()->SplitEllipsoid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultSplitEllipsoid() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setSplitEllipsoid(const bool &v) {
    instance()->handle->SetBool("SplitEllipsoid",v);
    instance()->SplitEllipsoid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeSplitEllipsoid() {
    instance()->handle->RemoveBool("SplitEllipsoid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docParallelRunThreshold() {
    return QT_TRANSLATE_NOOP("PartParams",
"Run boolean operations on several processor threads. Any value\n"
"above 0 turns this on, 0 or less turns it off.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getParallelRunThreshold() {
    return instance()->ParallelRunThreshold;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultParallelRunThreshold() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setParallelRunThreshold(const long &v) {
    instance()->handle->SetInt("ParallelRunThreshold",v);
    instance()->ParallelRunThreshold = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeParallelRunThreshold() {
    instance()->handle->RemoveInt("ParallelRunThreshold");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docAutoValidateShape() {
    return QT_TRANSLATE_NOOP("PartParams",
"Turn on Validate Shape in new PartDesign features. An invalid\n"
"result then gets a warning icon in the tree. Can slow down complex\n"
"models.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getAutoValidateShape() {
    return instance()->AutoValidateShape;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultAutoValidateShape() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setAutoValidateShape(const bool &v) {
    instance()->handle->SetBool("AutoValidateShape",v);
    instance()->AutoValidateShape = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeAutoValidateShape() {
    instance()->handle->RemoveBool("AutoValidateShape");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docFixShape() {
    return QT_TRANSLATE_NOOP("PartParams",
"Set Fix Shape to Enabled in new Part objects, so that a result\n"
"found invalid is repaired. When off new objects are left as they\n"
"are computed.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getFixShape() {
    return instance()->FixShape;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultFixShape() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setFixShape(const bool &v) {
    instance()->handle->SetBool("FixShape",v);
    instance()->FixShape = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeFixShape() {
    instance()->handle->RemoveBool("FixShape");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docShareStoredSubShapes() {
    return QT_TRANSLATE_NOOP("PartParams",
"Let a stored shape borrow a sub-shape from another object's file instead\n"
"of writing its geometry again (docs/SharedShapeStorage.md sec 12.4).\n"
"Turning this off writes every file whole, which is what the format did\n"
"before external references; the files stay readable either way.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getShareStoredSubShapes() {
    return instance()->ShareStoredSubShapes;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultShareStoredSubShapes() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setShareStoredSubShapes(const bool &v) {
    instance()->handle->SetBool("ShareStoredSubShapes",v);
    instance()->ShareStoredSubShapes = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeShareStoredSubShapes() {
    instance()->handle->RemoveBool("ShareStoredSubShapes");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docBorrowBelowFace() {
    return QT_TRANSLATE_NOOP("PartParams",
"Which sub-shapes a shape file may borrow from another below the level\n"
"of a shell, as a sum: 1 a face in a shell, 2 an edge in a face or\n"
"wire, 4 a vertex in an edge. 0, the default, none.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getBorrowBelowFace() {
    return instance()->BorrowBelowFace;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultBorrowBelowFace() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setBorrowBelowFace(const long &v) {
    instance()->handle->SetInt("BorrowBelowFace",v);
    instance()->BorrowBelowFace = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeBorrowBelowFace() {
    instance()->handle->RemoveInt("BorrowBelowFace");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docLoftMaxDegree() {
    return QT_TRANSLATE_NOOP("PartParams",
"Maximum surface degree given to new PartDesign lofts. Kept between\n"
"2 and the highest degree the kernel supports.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getLoftMaxDegree() {
    return instance()->LoftMaxDegree;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultLoftMaxDegree() {
    const static unsigned long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setLoftMaxDegree(const unsigned long &v) {
    instance()->handle->SetUnsigned("LoftMaxDegree",v);
    instance()->LoftMaxDegree = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeLoftMaxDegree() {
    instance()->handle->RemoveUnsigned("LoftMaxDegree");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docWarnUnnamedInput() {
    return QT_TRANSLATE_NOOP("PartParams",
"Report shape operations whose inputs carry no element names, so the\n"
"result cannot be named either. For workbench developers. 0 off, 1 once\n"
"per operation and recompute, 2 every occurrence.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getWarnUnnamedInput() {
    return instance()->WarnUnnamedInput;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultWarnUnnamedInput() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setWarnUnnamedInput(const long &v) {
    instance()->handle->SetInt("WarnUnnamedInput",v);
    instance()->WarnUnnamedInput = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeWarnUnnamedInput() {
    instance()->handle->RemoveInt("WarnUnnamedInput");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docMinimumDeviation() {
    return QT_TRANSLATE_NOOP("PartParams",
"Lower limit of the tessellation deviation of shapes, in percent of\n"
"the object size. Objects asking for a finer mesh are drawn with\n"
"this value instead.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & PartParams::getMinimumDeviation() {
    return instance()->MinimumDeviation;
}

// Auto generated code (Tools/params_utils.py:413)
const double & PartParams::defaultMinimumDeviation() {
    const static double def = 0.05;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setMinimumDeviation(const double &v) {
    instance()->handle->SetFloat("MinimumDeviation",v);
    instance()->MinimumDeviation = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeMinimumDeviation() {
    instance()->handle->RemoveFloat("MinimumDeviation");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docMeshDeviation() {
    return QT_TRANSLATE_NOOP("PartParams",
"Accuracy of the mesh that shapes are drawn with, as the largest\n"
"deviation in percent of the object size. Lower is finer and\n"
"slower. Sets the Deviation of new objects; a change is applied to\n"
"all open objects.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & PartParams::getMeshDeviation() {
    return instance()->MeshDeviation;
}

// Auto generated code (Tools/params_utils.py:413)
const double & PartParams::defaultMeshDeviation() {
    const static double def = 0.2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setMeshDeviation(const double &v) {
    instance()->handle->SetFloat("MeshDeviation",v);
    instance()->MeshDeviation = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeMeshDeviation() {
    instance()->handle->RemoveFloat("MeshDeviation");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docMeshAngularDeflection() {
    return QT_TRANSLATE_NOOP("PartParams",
"Largest angle between neighbouring segments of the mesh that\n"
"shapes are drawn with, in degrees. Lower is smoother and slower.\n"
"Sets the Angular Deflection of new objects; a change is applied to\n"
"all open objects.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & PartParams::getMeshAngularDeflection() {
    return instance()->MeshAngularDeflection;
}

// Auto generated code (Tools/params_utils.py:413)
const double & PartParams::defaultMeshAngularDeflection() {
    const static double def = 28.65;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setMeshAngularDeflection(const double &v) {
    instance()->handle->SetFloat("MeshAngularDeflection",v);
    instance()->MeshAngularDeflection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeMeshAngularDeflection() {
    instance()->handle->RemoveFloat("MeshAngularDeflection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docMinimumAngularDeflection() {
    return QT_TRANSLATE_NOOP("PartParams",
"Lower limit of the angular deflection used to mesh shapes, in\n"
"degrees. Objects asking for a smaller angle are drawn with this\n"
"value instead.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & PartParams::getMinimumAngularDeflection() {
    return instance()->MinimumAngularDeflection;
}

// Auto generated code (Tools/params_utils.py:413)
const double & PartParams::defaultMinimumAngularDeflection() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setMinimumAngularDeflection(const double &v) {
    instance()->handle->SetFloat("MinimumAngularDeflection",v);
    instance()->MinimumAngularDeflection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeMinimumAngularDeflection() {
    instance()->handle->RemoveFloat("MinimumAngularDeflection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docBooleanRefineModel() {
    return QT_TRANSLATE_NOOP("PartParams",
"New Part Boolean features get Refine switched on: faces that lie\n"
"on the same surface are merged after the operation. Read when a\n"
"feature is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getBooleanRefineModel() {
    return instance()->BooleanRefineModel;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultBooleanRefineModel() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setBooleanRefineModel(const bool &v) {
    instance()->subHandles[0]->SetBool("RefineModel",v);
    instance()->BooleanRefineModel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeBooleanRefineModel() {
    instance()->subHandles[0]->RemoveBool("RefineModel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docBooleanCheckModel() {
    return QT_TRANSLATE_NOOP("PartParams",
"Check the result of every Part Boolean operation for validity,\n"
"and fail the feature when it is not valid.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getBooleanCheckModel() {
    return instance()->BooleanCheckModel;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultBooleanCheckModel() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setBooleanCheckModel(const bool &v) {
    instance()->subHandles[0]->SetBool("CheckModel",v);
    instance()->BooleanCheckModel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeBooleanCheckModel() {
    instance()->subHandles[0]->RemoveBool("CheckModel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docBooleanFuzzy() {
    return QT_TRANSLATE_NOOP("PartParams",
"Factor of the tolerance a Boolean operation is given when it is\n"
"told to choose one itself: this times the size of the shapes times\n"
"the kernel's precision.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & PartParams::getBooleanFuzzy() {
    return instance()->BooleanFuzzy;
}

// Auto generated code (Tools/params_utils.py:413)
const double & PartParams::defaultBooleanFuzzy() {
    const static double def = 10.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setBooleanFuzzy(const double &v) {
    instance()->subHandles[0]->SetFloat("BooleanFuzzy",v);
    instance()->BooleanFuzzy = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeBooleanFuzzy() {
    instance()->subHandles[0]->RemoveFloat("BooleanFuzzy");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docAutoElementMap() {
    return QT_TRANSLATE_NOOP("PartParams",
"Give a shape that arrives without element names -- read from a\n"
"file, set by a script -- names of its own. Read once, at the first\n"
"such shape of a session.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getAutoElementMap() {
    return instance()->AutoElementMap;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultAutoElementMap() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setAutoElementMap(const bool &v) {
    instance()->subHandles[1]->SetBool("AutoElementMap",v);
    instance()->AutoElementMap = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeAutoElementMap() {
    instance()->subHandles[1]->RemoveBool("AutoElementMap");
}
//[[[end]]]
