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
    long ReadSurfaceCurveMode;
    long WriteSurfaceCurveMode;
    bool IgesBrepMode;
    long IgesUnit;
    std::string IgesCompany;
    std::string IgesAuthor;
    std::string IgesProduct;
    bool SkipBlankEntities;
    long StepUnit;
    std::string StepScheme;
    std::string StepProduct;
    std::string StepCompany;
    std::string StepAuthor;
    bool VisibleExportDialog;
    bool ExportHiddenObject;
    bool ImportHiddenObject;
    bool ExportKeepPlacement;
    bool UseAppPart;
    bool UseBaseName;
    bool ReduceObjects;
    bool ShowProgress;
    bool ProgressiveImport;
    long StreamBatchStart;
    long StreamBatchFactor;
    long ImportMode;
    long GltfRebuildBRep;
    bool ReadShapeCompoundMode;

    // Auto generated code (Tools/params_utils.py:254)
    PartParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Part");
        handle->Attach(this);

        subHandles.resize(6);
        subHandles[0] = handle->GetGroup("Boolean");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("General");
        subHandles[1]->Attach(this);
        subHandles[2] = handle->GetGroup("IGES");
        subHandles[2]->Attach(this);
        subHandles[3] = handle->GetGroup("STEP");
        subHandles[3]->Attach(this);
        subHandles[4] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Import");
        subHandles[4]->Attach(this);
        subHandles[5] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Import/hSTEP");
        subHandles[5]->Attach(this);
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
        ReadSurfaceCurveMode = this->subHandles[1]->GetInt("ReadSurfaceCurveMode", 0);
        funcs["ReadSurfaceCurveMode"] = &PartParamsP::updateReadSurfaceCurveMode;
        WriteSurfaceCurveMode = this->subHandles[1]->GetInt("WriteSurfaceCurveMode", 0);
        funcs["WriteSurfaceCurveMode"] = &PartParamsP::updateWriteSurfaceCurveMode;
        IgesBrepMode = this->subHandles[2]->GetBool("BrepMode", false);
        funcs["BrepMode"] = &PartParamsP::updateIgesBrepMode;
        IgesUnit = this->subHandles[2]->GetInt("Unit", 0);
        funcs["Unit"] = &PartParamsP::updateIgesUnit;
        IgesCompany = this->subHandles[2]->GetASCII("Company", "");
        funcs["Company"] = &PartParamsP::updateIgesCompany;
        IgesAuthor = this->subHandles[2]->GetASCII("Author", "");
        funcs["Author"] = &PartParamsP::updateIgesAuthor;
        IgesProduct = this->subHandles[2]->GetASCII("Product", "");
        funcs["Product"] = &PartParamsP::updateIgesProduct;
        SkipBlankEntities = this->subHandles[2]->GetBool("SkipBlankEntities", true);
        funcs["SkipBlankEntities"] = &PartParamsP::updateSkipBlankEntities;
        StepUnit = this->subHandles[3]->GetInt("Unit", 0);
        funcs["Unit"] = &PartParamsP::updateStepUnit;
        StepScheme = this->subHandles[3]->GetASCII("Scheme", "");
        funcs["Scheme"] = &PartParamsP::updateStepScheme;
        StepProduct = this->subHandles[3]->GetASCII("Product", "");
        funcs["Product"] = &PartParamsP::updateStepProduct;
        StepCompany = this->subHandles[3]->GetASCII("Company", "");
        funcs["Company"] = &PartParamsP::updateStepCompany;
        StepAuthor = this->subHandles[3]->GetASCII("Author", "Author");
        funcs["Author"] = &PartParamsP::updateStepAuthor;
        VisibleExportDialog = this->subHandles[3]->GetBool("VisibleExportDialog", true);
        funcs["VisibleExportDialog"] = &PartParamsP::updateVisibleExportDialog;
        ExportHiddenObject = this->subHandles[4]->GetBool("ExportHiddenObject", true);
        funcs["ExportHiddenObject"] = &PartParamsP::updateExportHiddenObject;
        ImportHiddenObject = this->subHandles[4]->GetBool("ImportHiddenObject", true);
        funcs["ImportHiddenObject"] = &PartParamsP::updateImportHiddenObject;
        ExportKeepPlacement = this->subHandles[4]->GetBool("ExportKeepPlacement", false);
        funcs["ExportKeepPlacement"] = &PartParamsP::updateExportKeepPlacement;
        UseAppPart = this->subHandles[4]->GetBool("UseAppPart", true);
        funcs["UseAppPart"] = &PartParamsP::updateUseAppPart;
        UseBaseName = this->subHandles[4]->GetBool("UseBaseName", true);
        funcs["UseBaseName"] = &PartParamsP::updateUseBaseName;
        ReduceObjects = this->subHandles[4]->GetBool("ReduceObjects", false);
        funcs["ReduceObjects"] = &PartParamsP::updateReduceObjects;
        ShowProgress = this->subHandles[4]->GetBool("ShowProgress", true);
        funcs["ShowProgress"] = &PartParamsP::updateShowProgress;
        ProgressiveImport = this->subHandles[4]->GetBool("ProgressiveImport", true);
        funcs["ProgressiveImport"] = &PartParamsP::updateProgressiveImport;
        StreamBatchStart = this->subHandles[4]->GetInt("StreamBatchStart", 1);
        funcs["StreamBatchStart"] = &PartParamsP::updateStreamBatchStart;
        StreamBatchFactor = this->subHandles[4]->GetInt("StreamBatchFactor", 8);
        funcs["StreamBatchFactor"] = &PartParamsP::updateStreamBatchFactor;
        ImportMode = this->subHandles[4]->GetInt("ImportMode", 0);
        funcs["ImportMode"] = &PartParamsP::updateImportMode;
        GltfRebuildBRep = this->subHandles[4]->GetInt("GltfRebuildBRep", 0);
        funcs["GltfRebuildBRep"] = &PartParamsP::updateGltfRebuildBRep;
        ReadShapeCompoundMode = this->subHandles[5]->GetBool("ReadShapeCompoundMode", false);
        funcs["ReadShapeCompoundMode"] = &PartParamsP::updateReadShapeCompoundMode;
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
    // Auto generated code (Tools/params_utils.py:314)
    static void updateReadSurfaceCurveMode(PartParamsP *self) {
        self->ReadSurfaceCurveMode = self->subHandles[1]->GetInt("ReadSurfaceCurveMode", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWriteSurfaceCurveMode(PartParamsP *self) {
        self->WriteSurfaceCurveMode = self->subHandles[1]->GetInt("WriteSurfaceCurveMode", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIgesBrepMode(PartParamsP *self) {
        self->IgesBrepMode = self->subHandles[2]->GetBool("BrepMode", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIgesUnit(PartParamsP *self) {
        self->IgesUnit = self->subHandles[2]->GetInt("Unit", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIgesCompany(PartParamsP *self) {
        self->IgesCompany = self->subHandles[2]->GetASCII("Company", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIgesAuthor(PartParamsP *self) {
        self->IgesAuthor = self->subHandles[2]->GetASCII("Author", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIgesProduct(PartParamsP *self) {
        self->IgesProduct = self->subHandles[2]->GetASCII("Product", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSkipBlankEntities(PartParamsP *self) {
        self->SkipBlankEntities = self->subHandles[2]->GetBool("SkipBlankEntities", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStepUnit(PartParamsP *self) {
        self->StepUnit = self->subHandles[3]->GetInt("Unit", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStepScheme(PartParamsP *self) {
        self->StepScheme = self->subHandles[3]->GetASCII("Scheme", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStepProduct(PartParamsP *self) {
        self->StepProduct = self->subHandles[3]->GetASCII("Product", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStepCompany(PartParamsP *self) {
        self->StepCompany = self->subHandles[3]->GetASCII("Company", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStepAuthor(PartParamsP *self) {
        self->StepAuthor = self->subHandles[3]->GetASCII("Author", "Author");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVisibleExportDialog(PartParamsP *self) {
        self->VisibleExportDialog = self->subHandles[3]->GetBool("VisibleExportDialog", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExportHiddenObject(PartParamsP *self) {
        self->ExportHiddenObject = self->subHandles[4]->GetBool("ExportHiddenObject", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateImportHiddenObject(PartParamsP *self) {
        self->ImportHiddenObject = self->subHandles[4]->GetBool("ImportHiddenObject", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExportKeepPlacement(PartParamsP *self) {
        self->ExportKeepPlacement = self->subHandles[4]->GetBool("ExportKeepPlacement", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseAppPart(PartParamsP *self) {
        self->UseAppPart = self->subHandles[4]->GetBool("UseAppPart", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseBaseName(PartParamsP *self) {
        self->UseBaseName = self->subHandles[4]->GetBool("UseBaseName", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateReduceObjects(PartParamsP *self) {
        self->ReduceObjects = self->subHandles[4]->GetBool("ReduceObjects", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowProgress(PartParamsP *self) {
        self->ShowProgress = self->subHandles[4]->GetBool("ShowProgress", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateProgressiveImport(PartParamsP *self) {
        self->ProgressiveImport = self->subHandles[4]->GetBool("ProgressiveImport", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStreamBatchStart(PartParamsP *self) {
        self->StreamBatchStart = self->subHandles[4]->GetInt("StreamBatchStart", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStreamBatchFactor(PartParamsP *self) {
        self->StreamBatchFactor = self->subHandles[4]->GetInt("StreamBatchFactor", 8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateImportMode(PartParamsP *self) {
        self->ImportMode = self->subHandles[4]->GetInt("ImportMode", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGltfRebuildBRep(PartParamsP *self) {
        self->GltfRebuildBRep = self->subHandles[4]->GetInt("GltfRebuildBRep", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateReadShapeCompoundMode(PartParamsP *self) {
        self->ReadShapeCompoundMode = self->subHandles[5]->GetBool("ReadShapeCompoundMode", false);
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
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/General", "ReadSurfaceCurveMode", "ReadSurfaceCurveMode", App::ParamInfo::Int, 0)
        .setTitle("Read surface curve mode")
        .setDoc("Which curve is kept when an entity of a STEP or IGES file has both\n"
"a 2D and a 3D one: 0 both, 3 the 3D curve and the 2D one is\n"
"rebuilt from it; for IGES also 2 prefer the 2D curve, -2 always\n"
"the 2D, -3 always the 3D. Read when Part is loaded."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/General", "WriteSurfaceCurveMode", "WriteSurfaceCurveMode", App::ParamInfo::Int, 0)
        .setTitle("Write curves on surfaces")
        .setDoc("Write the curves in the parameter space of surfaces (pcurves) into\n"
"STEP files: 0 off, which makes smaller files, 1 on. Stored by the\n"
"STEP export options."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/IGES", "IgesBrepMode", "BrepMode", App::ParamInfo::Bool, false)
        .setTitle("Write IGES solids as BRep")
        .setDoc("Write solids and shells into IGES files as BRep entities (type\n"
"186) instead of trimmed surfaces (type 144)."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/IGES", "IgesUnit", "Unit", App::ParamInfo::Int, 0)
        .setTitle("IGES export unit")
        .setDoc("Unit of exported IGES files: 0 millimetre, 1 metre, 2 inch."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/IGES", "IgesCompany", "Company", App::ParamInfo::String, "")
        .setTitle("IGES header company")
        .setDoc("Company named in the header of exported IGES files."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/IGES", "IgesAuthor", "Author", App::ParamInfo::String, "")
        .setTitle("IGES header author")
        .setDoc("Author named in the header of exported IGES files."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/IGES", "IgesProduct", "Product", App::ParamInfo::String, "")
        .setTitle("IGES header product")
        .setDoc("Product named in the header of exported IGES files. Empty uses the\n"
"kernel's own. Read when Part is loaded."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/IGES", "SkipBlankEntities", "SkipBlankEntities", App::ParamInfo::Bool, true)
        .setTitle("Skip blank IGES entities")
        .setDoc("Leave out the blank (hidden) entities of an IGES file that is\n"
"imported."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/STEP", "StepUnit", "Unit", App::ParamInfo::Int, 0)
        .setTitle("STEP export unit")
        .setDoc("Unit of exported STEP files: 0 millimetre, 1 metre, 2 inch."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/STEP", "StepScheme", "Scheme", App::ParamInfo::String, "")
        .setTitle("STEP export scheme")
        .setDoc("Application protocol of exported STEP files: AP203, AP214CD,\n"
"AP214DIS, AP214IS or AP242DIS. Empty uses the kernel's own."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/STEP", "StepProduct", "Product", App::ParamInfo::String, "")
        .setTitle("STEP product name")
        .setDoc("Product name written into exported STEP files. Empty uses the\n"
"kernel's own. Read when Part is loaded."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/STEP", "StepCompany", "Company", App::ParamInfo::String, "")
        .setTitle("STEP header company")
        .setDoc("Organisation named in the header of exported STEP files."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/STEP", "StepAuthor", "Author", App::ParamInfo::String, "Author")
        .setTitle("STEP header author")
        .setDoc("Author named in the header of exported STEP files."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/STEP", "VisibleExportDialog", "VisibleExportDialog", App::ParamInfo::Bool, true)
        .setTitle("Show the STEP export options")
        .setDoc("Show the options dialog each time a STEP file is exported."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "ExportHiddenObject", "ExportHiddenObject", App::ParamInfo::Bool, true)
        .setTitle("Export invisible objects")
        .setDoc("Write objects that are hidden as well, marked invisible. Switch\n"
"off for programs that do not understand invisibility in a STEP\n"
"file."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "ImportHiddenObject", "ImportHiddenObject", App::ParamInfo::Bool, true)
        .setTitle("Import invisible objects")
        .setDoc("Read the objects a file marks invisible as well."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "ExportKeepPlacement", "ExportKeepPlacement", App::ParamInfo::Bool, false)
        .setTitle("Export single object placement")
        .setDoc("Keep the placement when a single object is exported. Read back,\n"
"the placement is part of the shape's geometry and not a Placement\n"
"property."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "UseAppPart", "UseAppPart", App::ParamInfo::Bool, true)
        .setTitle("Use Part container")
        .setDoc("Import the groups of an assembly as App::Part containers; off uses\n"
"App::LinkGroup."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "UseBaseName", "UseBaseName", App::ParamInfo::Bool, true)
        .setTitle("Ignore instance names")
        .setDoc("Name imported objects after what they are an instance of, not\n"
"after the instance. Useful for old STEP files whose instance names\n"
"are generated and mean nothing."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "ReduceObjects", "ReduceObjects", App::ParamInfo::Bool, false)
        .setTitle("Reduce number of objects")
        .setDoc("Import repeated instances as Link arrays, which makes fewer\n"
"objects."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "ShowProgress", "ShowProgress", App::ParamInfo::Bool, true)
        .setTitle("Show progress when importing")
        .setDoc("Show a progress bar while a file is imported."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "ProgressiveImport", "ProgressiveImport", App::ParamInfo::Bool, true)
        .setTitle("Progressive import")
        .setDoc("Create the imported objects step by step, so the model shows while\n"
"the import still runs. Single document mode only."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "StreamBatchStart", "StreamBatchStart", App::ParamInfo::Int, 1)
        .setTitle("Progressive import, first batch")
        .setDoc("Number of units -- roots, or the components of a single root --\n"
"the first batch of a progressive import transfers. At least 1."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "StreamBatchFactor", "StreamBatchFactor", App::ParamInfo::Int, 8)
        .setTitle("Progressive import, batch growth")
        .setDoc("Factor by which each batch of a progressive import is larger than\n"
"the one before; 1 keeps the size. Each batch repeats passes over\n"
"the whole file, hence the steep growth."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "ImportMode", "ImportMode", App::ParamInfo::Int, 0)
        .setTitle("Import mode")
        .setDoc("How an assembly file becomes documents: 0 a single document, 1 a\n"
"group per document, 2 a group per directory, 3 an object per\n"
"document, 4 an object per directory."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import", "GltfRebuildBRep", "GltfRebuildBRep", App::ParamInfo::Int, 0)
        .setTitle("Rebuild BRep from glTF")
        .setDoc("Whether the meshes of a glTF file are rebuilt as BRep faces: 0\n"
"never, each mesh arrives as it was read with its triangles, UVs\n"
"and normals; 1 only where nothing would be lost; 2 always."),
    App::ParamInfo("Part", "PartParams", "User parameter:BaseApp/Preferences/Mod/Import/hSTEP", "ReadShapeCompoundMode", "ReadShapeCompoundMode", App::ParamInfo::Bool, false)
        .setTitle("STEP compound merge")
        .setDoc("The option 'Enable STEP Compound merge' of the STEP import: the\n"
"parts of a file are merged into one compound instead of imported\n"
"as objects of their own."),
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

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docReadSurfaceCurveMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Which curve is kept when an entity of a STEP or IGES file has both\n"
"a 2D and a 3D one: 0 both, 3 the 3D curve and the 2D one is\n"
"rebuilt from it; for IGES also 2 prefer the 2D curve, -2 always\n"
"the 2D, -3 always the 3D. Read when Part is loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getReadSurfaceCurveMode() {
    return instance()->ReadSurfaceCurveMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultReadSurfaceCurveMode() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setReadSurfaceCurveMode(const long &v) {
    instance()->subHandles[1]->SetInt("ReadSurfaceCurveMode",v);
    instance()->ReadSurfaceCurveMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeReadSurfaceCurveMode() {
    instance()->subHandles[1]->RemoveInt("ReadSurfaceCurveMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docWriteSurfaceCurveMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Write the curves in the parameter space of surfaces (pcurves) into\n"
"STEP files: 0 off, which makes smaller files, 1 on. Stored by the\n"
"STEP export options.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getWriteSurfaceCurveMode() {
    return instance()->WriteSurfaceCurveMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultWriteSurfaceCurveMode() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setWriteSurfaceCurveMode(const long &v) {
    instance()->subHandles[1]->SetInt("WriteSurfaceCurveMode",v);
    instance()->WriteSurfaceCurveMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeWriteSurfaceCurveMode() {
    instance()->subHandles[1]->RemoveInt("WriteSurfaceCurveMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docIgesBrepMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Write solids and shells into IGES files as BRep entities (type\n"
"186) instead of trimmed surfaces (type 144).");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getIgesBrepMode() {
    return instance()->IgesBrepMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultIgesBrepMode() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setIgesBrepMode(const bool &v) {
    instance()->subHandles[2]->SetBool("BrepMode",v);
    instance()->IgesBrepMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeIgesBrepMode() {
    instance()->subHandles[2]->RemoveBool("BrepMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docIgesUnit() {
    return QT_TRANSLATE_NOOP("PartParams",
"Unit of exported IGES files: 0 millimetre, 1 metre, 2 inch.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getIgesUnit() {
    return instance()->IgesUnit;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultIgesUnit() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setIgesUnit(const long &v) {
    instance()->subHandles[2]->SetInt("Unit",v);
    instance()->IgesUnit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeIgesUnit() {
    instance()->subHandles[2]->RemoveInt("Unit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docIgesCompany() {
    return QT_TRANSLATE_NOOP("PartParams",
"Company named in the header of exported IGES files.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & PartParams::getIgesCompany() {
    return instance()->IgesCompany;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & PartParams::defaultIgesCompany() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setIgesCompany(const std::string &v) {
    instance()->subHandles[2]->SetASCII("Company",v);
    instance()->IgesCompany = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeIgesCompany() {
    instance()->subHandles[2]->RemoveASCII("Company");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docIgesAuthor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Author named in the header of exported IGES files.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & PartParams::getIgesAuthor() {
    return instance()->IgesAuthor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & PartParams::defaultIgesAuthor() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setIgesAuthor(const std::string &v) {
    instance()->subHandles[2]->SetASCII("Author",v);
    instance()->IgesAuthor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeIgesAuthor() {
    instance()->subHandles[2]->RemoveASCII("Author");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docIgesProduct() {
    return QT_TRANSLATE_NOOP("PartParams",
"Product named in the header of exported IGES files. Empty uses the\n"
"kernel's own. Read when Part is loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & PartParams::getIgesProduct() {
    return instance()->IgesProduct;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & PartParams::defaultIgesProduct() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setIgesProduct(const std::string &v) {
    instance()->subHandles[2]->SetASCII("Product",v);
    instance()->IgesProduct = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeIgesProduct() {
    instance()->subHandles[2]->RemoveASCII("Product");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docSkipBlankEntities() {
    return QT_TRANSLATE_NOOP("PartParams",
"Leave out the blank (hidden) entities of an IGES file that is\n"
"imported.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getSkipBlankEntities() {
    return instance()->SkipBlankEntities;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultSkipBlankEntities() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setSkipBlankEntities(const bool &v) {
    instance()->subHandles[2]->SetBool("SkipBlankEntities",v);
    instance()->SkipBlankEntities = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeSkipBlankEntities() {
    instance()->subHandles[2]->RemoveBool("SkipBlankEntities");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docStepUnit() {
    return QT_TRANSLATE_NOOP("PartParams",
"Unit of exported STEP files: 0 millimetre, 1 metre, 2 inch.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getStepUnit() {
    return instance()->StepUnit;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultStepUnit() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setStepUnit(const long &v) {
    instance()->subHandles[3]->SetInt("Unit",v);
    instance()->StepUnit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeStepUnit() {
    instance()->subHandles[3]->RemoveInt("Unit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docStepScheme() {
    return QT_TRANSLATE_NOOP("PartParams",
"Application protocol of exported STEP files: AP203, AP214CD,\n"
"AP214DIS, AP214IS or AP242DIS. Empty uses the kernel's own.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & PartParams::getStepScheme() {
    return instance()->StepScheme;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & PartParams::defaultStepScheme() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setStepScheme(const std::string &v) {
    instance()->subHandles[3]->SetASCII("Scheme",v);
    instance()->StepScheme = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeStepScheme() {
    instance()->subHandles[3]->RemoveASCII("Scheme");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docStepProduct() {
    return QT_TRANSLATE_NOOP("PartParams",
"Product name written into exported STEP files. Empty uses the\n"
"kernel's own. Read when Part is loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & PartParams::getStepProduct() {
    return instance()->StepProduct;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & PartParams::defaultStepProduct() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setStepProduct(const std::string &v) {
    instance()->subHandles[3]->SetASCII("Product",v);
    instance()->StepProduct = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeStepProduct() {
    instance()->subHandles[3]->RemoveASCII("Product");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docStepCompany() {
    return QT_TRANSLATE_NOOP("PartParams",
"Organisation named in the header of exported STEP files.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & PartParams::getStepCompany() {
    return instance()->StepCompany;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & PartParams::defaultStepCompany() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setStepCompany(const std::string &v) {
    instance()->subHandles[3]->SetASCII("Company",v);
    instance()->StepCompany = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeStepCompany() {
    instance()->subHandles[3]->RemoveASCII("Company");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docStepAuthor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Author named in the header of exported STEP files.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & PartParams::getStepAuthor() {
    return instance()->StepAuthor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & PartParams::defaultStepAuthor() {
    const static std::string def = "Author";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setStepAuthor(const std::string &v) {
    instance()->subHandles[3]->SetASCII("Author",v);
    instance()->StepAuthor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeStepAuthor() {
    instance()->subHandles[3]->RemoveASCII("Author");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docVisibleExportDialog() {
    return QT_TRANSLATE_NOOP("PartParams",
"Show the options dialog each time a STEP file is exported.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getVisibleExportDialog() {
    return instance()->VisibleExportDialog;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultVisibleExportDialog() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setVisibleExportDialog(const bool &v) {
    instance()->subHandles[3]->SetBool("VisibleExportDialog",v);
    instance()->VisibleExportDialog = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeVisibleExportDialog() {
    instance()->subHandles[3]->RemoveBool("VisibleExportDialog");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docExportHiddenObject() {
    return QT_TRANSLATE_NOOP("PartParams",
"Write objects that are hidden as well, marked invisible. Switch\n"
"off for programs that do not understand invisibility in a STEP\n"
"file.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getExportHiddenObject() {
    return instance()->ExportHiddenObject;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultExportHiddenObject() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setExportHiddenObject(const bool &v) {
    instance()->subHandles[4]->SetBool("ExportHiddenObject",v);
    instance()->ExportHiddenObject = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeExportHiddenObject() {
    instance()->subHandles[4]->RemoveBool("ExportHiddenObject");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docImportHiddenObject() {
    return QT_TRANSLATE_NOOP("PartParams",
"Read the objects a file marks invisible as well.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getImportHiddenObject() {
    return instance()->ImportHiddenObject;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultImportHiddenObject() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setImportHiddenObject(const bool &v) {
    instance()->subHandles[4]->SetBool("ImportHiddenObject",v);
    instance()->ImportHiddenObject = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeImportHiddenObject() {
    instance()->subHandles[4]->RemoveBool("ImportHiddenObject");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docExportKeepPlacement() {
    return QT_TRANSLATE_NOOP("PartParams",
"Keep the placement when a single object is exported. Read back,\n"
"the placement is part of the shape's geometry and not a Placement\n"
"property.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getExportKeepPlacement() {
    return instance()->ExportKeepPlacement;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultExportKeepPlacement() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setExportKeepPlacement(const bool &v) {
    instance()->subHandles[4]->SetBool("ExportKeepPlacement",v);
    instance()->ExportKeepPlacement = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeExportKeepPlacement() {
    instance()->subHandles[4]->RemoveBool("ExportKeepPlacement");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docUseAppPart() {
    return QT_TRANSLATE_NOOP("PartParams",
"Import the groups of an assembly as App::Part containers; off uses\n"
"App::LinkGroup.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getUseAppPart() {
    return instance()->UseAppPart;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultUseAppPart() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setUseAppPart(const bool &v) {
    instance()->subHandles[4]->SetBool("UseAppPart",v);
    instance()->UseAppPart = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeUseAppPart() {
    instance()->subHandles[4]->RemoveBool("UseAppPart");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docUseBaseName() {
    return QT_TRANSLATE_NOOP("PartParams",
"Name imported objects after what they are an instance of, not\n"
"after the instance. Useful for old STEP files whose instance names\n"
"are generated and mean nothing.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getUseBaseName() {
    return instance()->UseBaseName;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultUseBaseName() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setUseBaseName(const bool &v) {
    instance()->subHandles[4]->SetBool("UseBaseName",v);
    instance()->UseBaseName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeUseBaseName() {
    instance()->subHandles[4]->RemoveBool("UseBaseName");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docReduceObjects() {
    return QT_TRANSLATE_NOOP("PartParams",
"Import repeated instances as Link arrays, which makes fewer\n"
"objects.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getReduceObjects() {
    return instance()->ReduceObjects;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultReduceObjects() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setReduceObjects(const bool &v) {
    instance()->subHandles[4]->SetBool("ReduceObjects",v);
    instance()->ReduceObjects = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeReduceObjects() {
    instance()->subHandles[4]->RemoveBool("ReduceObjects");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docShowProgress() {
    return QT_TRANSLATE_NOOP("PartParams",
"Show a progress bar while a file is imported.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getShowProgress() {
    return instance()->ShowProgress;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultShowProgress() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setShowProgress(const bool &v) {
    instance()->subHandles[4]->SetBool("ShowProgress",v);
    instance()->ShowProgress = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeShowProgress() {
    instance()->subHandles[4]->RemoveBool("ShowProgress");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docProgressiveImport() {
    return QT_TRANSLATE_NOOP("PartParams",
"Create the imported objects step by step, so the model shows while\n"
"the import still runs. Single document mode only.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getProgressiveImport() {
    return instance()->ProgressiveImport;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultProgressiveImport() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setProgressiveImport(const bool &v) {
    instance()->subHandles[4]->SetBool("ProgressiveImport",v);
    instance()->ProgressiveImport = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeProgressiveImport() {
    instance()->subHandles[4]->RemoveBool("ProgressiveImport");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docStreamBatchStart() {
    return QT_TRANSLATE_NOOP("PartParams",
"Number of units -- roots, or the components of a single root --\n"
"the first batch of a progressive import transfers. At least 1.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getStreamBatchStart() {
    return instance()->StreamBatchStart;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultStreamBatchStart() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setStreamBatchStart(const long &v) {
    instance()->subHandles[4]->SetInt("StreamBatchStart",v);
    instance()->StreamBatchStart = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeStreamBatchStart() {
    instance()->subHandles[4]->RemoveInt("StreamBatchStart");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docStreamBatchFactor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Factor by which each batch of a progressive import is larger than\n"
"the one before; 1 keeps the size. Each batch repeats passes over\n"
"the whole file, hence the steep growth.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getStreamBatchFactor() {
    return instance()->StreamBatchFactor;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultStreamBatchFactor() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setStreamBatchFactor(const long &v) {
    instance()->subHandles[4]->SetInt("StreamBatchFactor",v);
    instance()->StreamBatchFactor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeStreamBatchFactor() {
    instance()->subHandles[4]->RemoveInt("StreamBatchFactor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docImportMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"How an assembly file becomes documents: 0 a single document, 1 a\n"
"group per document, 2 a group per directory, 3 an object per\n"
"document, 4 an object per directory.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getImportMode() {
    return instance()->ImportMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultImportMode() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setImportMode(const long &v) {
    instance()->subHandles[4]->SetInt("ImportMode",v);
    instance()->ImportMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeImportMode() {
    instance()->subHandles[4]->RemoveInt("ImportMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docGltfRebuildBRep() {
    return QT_TRANSLATE_NOOP("PartParams",
"Whether the meshes of a glTF file are rebuilt as BRep faces: 0\n"
"never, each mesh arrives as it was read with its triangles, UVs\n"
"and normals; 1 only where nothing would be lost; 2 always.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getGltfRebuildBRep() {
    return instance()->GltfRebuildBRep;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultGltfRebuildBRep() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setGltfRebuildBRep(const long &v) {
    instance()->subHandles[4]->SetInt("GltfRebuildBRep",v);
    instance()->GltfRebuildBRep = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeGltfRebuildBRep() {
    instance()->subHandles[4]->RemoveInt("GltfRebuildBRep");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docReadShapeCompoundMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"The option 'Enable STEP Compound merge' of the STEP import: the\n"
"parts of a file are merged into one compound instead of imported\n"
"as objects of their own.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getReadShapeCompoundMode() {
    return instance()->ReadShapeCompoundMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultReadShapeCompoundMode() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setReadShapeCompoundMode(const bool &v) {
    instance()->subHandles[5]->SetBool("ReadShapeCompoundMode",v);
    instance()->ReadShapeCompoundMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeReadShapeCompoundMode() {
    instance()->subHandles[5]->RemoveBool("ReadShapeCompoundMode");
}
//[[[end]]]
