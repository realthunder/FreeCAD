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
import SketcherParams
SketcherParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "SketcherParams.h"
using namespace Sketcher;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class SketcherParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(SketcherParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    bool AutoRecompute;
    bool AutoRemoveRedundants;
    bool ContinuousCreationMode;
    bool ContinuousConstraintMode;
    bool ShowDialogOnDistanceConstraint;
    double RadiusDiameterConstraintDisplayBaseAngle;
    double RadiusDiameterConstraintDisplayAngleRandomness;
    long GeometryHistoryLevel;
    double ArcFitTolerance;
    long ExternalBSplineMaxDegree;
    double ExternalBSplineTolerance;
    bool MakeInternals;
    bool LeaveSketchWithEscape;
    bool ShowSolverAdvancedWidget;
    bool RecalculateInitialSolutionWhileDragging;
    bool ExtendedConstraintInformation;
    bool HideInternalAlignment;
    bool VisualisationTrackingFilter;
    bool HideUnits;
    bool ShowDimensionalName;
    std::string DimensionalStringFormat;
    bool ShowCursorCoords;
    bool UseSystemDecimals;
    bool AllowFaceExternalPick;
    bool ViewBottomOnEdit;
    bool AdjustCamera;
    bool FitSketchOnEdit;
    bool HideDependent;
    bool ShowLinks;
    bool ShowSupport;
    bool RestoreCamera;
    bool ForceOrtho;
    bool SectionView;
    bool AutoConstraints;
    bool AvoidRedundantAutoconstraints;
    bool ShowOriginalColor;
    bool SketchAutoTransparentPick;
    double ZHeight;
    long AxisTransparency;
    unsigned long FaceColor;
    bool ShowGrid;
    double GridSize;
    bool GridAuto;
    long GridSizePixelThreshold;
    long GridNumberSubdivision;
    long GridLinePattern;
    long GridDivLinePattern;
    long GridLineWidth;
    long GridDivLineWidth;
    unsigned long GridLineColor;
    unsigned long GridDivLineColor;
    long GridTransparency;
    long TopRenderGeometryId;
    long MidRenderGeometryId;
    long LowRenderGeometryId;
    bool BSplineExternalVisible;
    bool EditDatumInPlace;
    bool DatumEscapeTakesBack;
    bool ShowDirectionalAutoConstraintHints;
    long DragAutoConstraintDelay;
    bool NotifyConstraintSubstitutions;
    long EdgeWidth;
    long EdgePattern;
    long ConstructionWidth;
    long ConstructionPattern;
    long InternalWidth;
    long InternalPattern;
    long ExternalWidth;
    long ExternalPattern;
    long ExternalDefiningWidth;
    long ExternalDefiningPattern;
    long InformationWidth;
    long InformationPattern;
    long DimensionalConstraintLineWidth;
    long DimensionalConstraintLinePattern;
    long AxisLineWidth;
    long AxisLinePattern;
    bool SingleDimensioningTool;
    bool SeparatedDimensioningTools;
    bool DimensioningDiameter;
    bool DimensioningRadius;
    long AutoScaleMode;
    long OnViewParameterVisibility;
    bool UnifiedCoincident;
    bool AutoHorVer;
    bool UnifiedLineCommands;
    bool Snap;
    bool SnapToObjects;
    bool SnapToGrid;
    double SnapAngle;
    long ElementIconSize;
    std::string EditSketcherFontName;
    long ConstraintIconLabelsPerLine;
    long ConstraintIconLabelLines;
    double ViewScalingFactor;
    long SegmentsPerGeometry;
    unsigned long CursorTextColor;
    unsigned long SketchEdgeColor;
    unsigned long SketchVertexColor;
    unsigned long EditedEdgeColor;
    unsigned long ConstructionColor;
    unsigned long InternalAlignedGeoColor;
    unsigned long FullyConstraintElementColor;
    unsigned long FullyConstraintConstructionElementColor;
    unsigned long FullyConstraintInternalAlignmentColor;
    unsigned long InvalidSketchColor;
    unsigned long FullyConstrainedColor;
    unsigned long ConstrainedDimColor;
    unsigned long ConstrainedIcoColor;
    unsigned long NonDrivingConstrDimColor;
    unsigned long ExprBasedConstrDimColor;
    unsigned long DeactivatedConstrDimColor;
    unsigned long ExternalColor;
    unsigned long ExternalDefiningColor;
    unsigned long InformationColor;
    unsigned long FrozenColor;
    unsigned long DetachedColor;
    unsigned long MissingColor;

    // Auto generated code (Tools/params_utils.py:254)
    SketcherParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Sketcher");
        handle->Attach(this);

        subHandles.resize(10);
        subHandles[0] = handle->GetGroup("General");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("General/GridSize");
        subHandles[1]->Attach(this);
        subHandles[2] = handle->GetGroup("View");
        subHandles[2]->Attach(this);
        subHandles[3] = handle->GetGroup("dimensioning");
        subHandles[3]->Attach(this);
        subHandles[4] = handle->GetGroup("Tools");
        subHandles[4]->Attach(this);
        subHandles[5] = handle->GetGroup("Constraints");
        subHandles[5]->Attach(this);
        subHandles[6] = handle->GetGroup("Commands");
        subHandles[6]->Attach(this);
        subHandles[7] = handle->GetGroup("Snap");
        subHandles[7]->Attach(this);
        subHandles[8] = handle->GetGroup("Elements");
        subHandles[8]->Attach(this);
        subHandles[9] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
        subHandles[9]->Attach(this);
        AutoRecompute = this->handle->GetBool("AutoRecompute", false);
        funcs["AutoRecompute"] = &SketcherParamsP::updateAutoRecompute;
        AutoRemoveRedundants = this->handle->GetBool("AutoRemoveRedundants", false);
        funcs["AutoRemoveRedundants"] = &SketcherParamsP::updateAutoRemoveRedundants;
        ContinuousCreationMode = this->handle->GetBool("ContinuousCreationMode", true);
        funcs["ContinuousCreationMode"] = &SketcherParamsP::updateContinuousCreationMode;
        ContinuousConstraintMode = this->handle->GetBool("ContinuousConstraintMode", true);
        funcs["ContinuousConstraintMode"] = &SketcherParamsP::updateContinuousConstraintMode;
        ShowDialogOnDistanceConstraint = this->handle->GetBool("ShowDialogOnDistanceConstraint", true);
        funcs["ShowDialogOnDistanceConstraint"] = &SketcherParamsP::updateShowDialogOnDistanceConstraint;
        RadiusDiameterConstraintDisplayBaseAngle = this->handle->GetFloat("RadiusDiameterConstraintDisplayBaseAngle", 15.0);
        funcs["RadiusDiameterConstraintDisplayBaseAngle"] = &SketcherParamsP::updateRadiusDiameterConstraintDisplayBaseAngle;
        RadiusDiameterConstraintDisplayAngleRandomness = this->handle->GetFloat("RadiusDiameterConstraintDisplayAngleRandomness", 0.0);
        funcs["RadiusDiameterConstraintDisplayAngleRandomness"] = &SketcherParamsP::updateRadiusDiameterConstraintDisplayAngleRandomness;
        GeometryHistoryLevel = this->handle->GetInt("GeometryHistoryLevel", 1);
        funcs["GeometryHistoryLevel"] = &SketcherParamsP::updateGeometryHistoryLevel;
        ArcFitTolerance = this->handle->GetFloat("ArcFitTolerance", 1e-06);
        funcs["ArcFitTolerance"] = &SketcherParamsP::updateArcFitTolerance;
        ExternalBSplineMaxDegree = this->handle->GetInt("ExternalBSplineMaxDegree", 5);
        funcs["ExternalBSplineMaxDegree"] = &SketcherParamsP::updateExternalBSplineMaxDegree;
        ExternalBSplineTolerance = this->handle->GetFloat("ExternalBSplineTolerance", 0.0001);
        funcs["ExternalBSplineTolerance"] = &SketcherParamsP::updateExternalBSplineTolerance;
        MakeInternals = this->handle->GetBool("MakeInternals", true);
        funcs["MakeInternals"] = &SketcherParamsP::updateMakeInternals;
        LeaveSketchWithEscape = this->handle->GetBool("LeaveSketchWithEscape", true);
        funcs["LeaveSketchWithEscape"] = &SketcherParamsP::updateLeaveSketchWithEscape;
        ShowSolverAdvancedWidget = this->handle->GetBool("ShowSolverAdvancedWidget", false);
        funcs["ShowSolverAdvancedWidget"] = &SketcherParamsP::updateShowSolverAdvancedWidget;
        RecalculateInitialSolutionWhileDragging = this->handle->GetBool("RecalculateInitialSolutionWhileDragging", true);
        funcs["RecalculateInitialSolutionWhileDragging"] = &SketcherParamsP::updateRecalculateInitialSolutionWhileDragging;
        ExtendedConstraintInformation = this->handle->GetBool("ExtendedConstraintInformation", false);
        funcs["ExtendedConstraintInformation"] = &SketcherParamsP::updateExtendedConstraintInformation;
        HideInternalAlignment = this->handle->GetBool("HideInternalAlignment", false);
        funcs["HideInternalAlignment"] = &SketcherParamsP::updateHideInternalAlignment;
        VisualisationTrackingFilter = this->handle->GetBool("VisualisationTrackingFilter", false);
        funcs["VisualisationTrackingFilter"] = &SketcherParamsP::updateVisualisationTrackingFilter;
        HideUnits = this->handle->GetBool("HideUnits", false);
        funcs["HideUnits"] = &SketcherParamsP::updateHideUnits;
        ShowDimensionalName = this->handle->GetBool("ShowDimensionalName", true);
        funcs["ShowDimensionalName"] = &SketcherParamsP::updateShowDimensionalName;
        DimensionalStringFormat = this->handle->GetASCII("DimensionalStringFormat", "%N = %V");
        funcs["DimensionalStringFormat"] = &SketcherParamsP::updateDimensionalStringFormat;
        ShowCursorCoords = this->handle->GetBool("ShowCursorCoords", true);
        funcs["ShowCursorCoords"] = &SketcherParamsP::updateShowCursorCoords;
        UseSystemDecimals = this->handle->GetBool("UseSystemDecimals", true);
        funcs["UseSystemDecimals"] = &SketcherParamsP::updateUseSystemDecimals;
        AllowFaceExternalPick = this->subHandles[0]->GetBool("AllowFaceExternalPick", true);
        funcs["AllowFaceExternalPick"] = &SketcherParamsP::updateAllowFaceExternalPick;
        ViewBottomOnEdit = this->subHandles[0]->GetBool("ViewBottomOnEdit", false);
        funcs["ViewBottomOnEdit"] = &SketcherParamsP::updateViewBottomOnEdit;
        AdjustCamera = this->subHandles[0]->GetBool("AdjustCamera", true);
        funcs["AdjustCamera"] = &SketcherParamsP::updateAdjustCamera;
        FitSketchOnEdit = this->subHandles[0]->GetBool("FitSketchOnEdit", true);
        funcs["FitSketchOnEdit"] = &SketcherParamsP::updateFitSketchOnEdit;
        HideDependent = this->subHandles[0]->GetBool("HideDependent", true);
        funcs["HideDependent"] = &SketcherParamsP::updateHideDependent;
        ShowLinks = this->subHandles[0]->GetBool("ShowLinks", true);
        funcs["ShowLinks"] = &SketcherParamsP::updateShowLinks;
        ShowSupport = this->subHandles[0]->GetBool("ShowSupport", true);
        funcs["ShowSupport"] = &SketcherParamsP::updateShowSupport;
        RestoreCamera = this->subHandles[0]->GetBool("RestoreCamera", true);
        funcs["RestoreCamera"] = &SketcherParamsP::updateRestoreCamera;
        ForceOrtho = this->subHandles[0]->GetBool("ForceOrtho", false);
        funcs["ForceOrtho"] = &SketcherParamsP::updateForceOrtho;
        SectionView = this->subHandles[0]->GetBool("SectionView", false);
        funcs["SectionView"] = &SketcherParamsP::updateSectionView;
        AutoConstraints = this->subHandles[0]->GetBool("AutoConstraints", true);
        funcs["AutoConstraints"] = &SketcherParamsP::updateAutoConstraints;
        AvoidRedundantAutoconstraints = this->subHandles[0]->GetBool("AvoidRedundantAutoconstraints", true);
        funcs["AvoidRedundantAutoconstraints"] = &SketcherParamsP::updateAvoidRedundantAutoconstraints;
        ShowOriginalColor = this->subHandles[0]->GetBool("ShowOriginalColor", false);
        funcs["ShowOriginalColor"] = &SketcherParamsP::updateShowOriginalColor;
        SketchAutoTransparentPick = this->subHandles[0]->GetBool("SketchAutoTransparentPick", false);
        funcs["SketchAutoTransparentPick"] = &SketcherParamsP::updateSketchAutoTransparentPick;
        ZHeight = this->subHandles[0]->GetFloat("ZHeight", 1e-06);
        funcs["ZHeight"] = &SketcherParamsP::updateZHeight;
        AxisTransparency = this->subHandles[0]->GetInt("AxisTransparency", 30);
        funcs["AxisTransparency"] = &SketcherParamsP::updateAxisTransparency;
        FaceColor = this->subHandles[0]->GetUnsigned("FaceColor", 0x54ABFF7F);
        funcs["FaceColor"] = &SketcherParamsP::updateFaceColor;
        ShowGrid = this->subHandles[0]->GetBool("ShowGrid", true);
        funcs["ShowGrid"] = &SketcherParamsP::updateShowGrid;
        GridSize = this->subHandles[1]->GetFloat("GridSize", 10.0);
        funcs["GridSize"] = &SketcherParamsP::updateGridSize;
        GridAuto = this->subHandles[0]->GetBool("GridAuto", true);
        funcs["GridAuto"] = &SketcherParamsP::updateGridAuto;
        GridSizePixelThreshold = this->subHandles[0]->GetInt("GridSizePixelThreshold", 15);
        funcs["GridSizePixelThreshold"] = &SketcherParamsP::updateGridSizePixelThreshold;
        GridNumberSubdivision = this->subHandles[0]->GetInt("GridNumberSubdivision", 10);
        funcs["GridNumberSubdivision"] = &SketcherParamsP::updateGridNumberSubdivision;
        GridLinePattern = this->subHandles[0]->GetInt("GridLinePattern", 65535);
        funcs["GridLinePattern"] = &SketcherParamsP::updateGridLinePattern;
        GridDivLinePattern = this->subHandles[0]->GetInt("GridDivLinePattern", 65535);
        funcs["GridDivLinePattern"] = &SketcherParamsP::updateGridDivLinePattern;
        GridLineWidth = this->subHandles[0]->GetInt("GridLineWidth", 1);
        funcs["GridLineWidth"] = &SketcherParamsP::updateGridLineWidth;
        GridDivLineWidth = this->subHandles[0]->GetInt("GridDivLineWidth", 2);
        funcs["GridDivLineWidth"] = &SketcherParamsP::updateGridDivLineWidth;
        GridLineColor = this->subHandles[0]->GetUnsigned("GridLineColor", 0xB2B2B2FF);
        funcs["GridLineColor"] = &SketcherParamsP::updateGridLineColor;
        GridDivLineColor = this->subHandles[0]->GetUnsigned("GridDivLineColor", 0xB2B2B2FF);
        funcs["GridDivLineColor"] = &SketcherParamsP::updateGridDivLineColor;
        GridTransparency = this->subHandles[0]->GetInt("GridTransparency", 60);
        funcs["GridTransparency"] = &SketcherParamsP::updateGridTransparency;
        TopRenderGeometryId = this->subHandles[0]->GetInt("TopRenderGeometryId", 1);
        funcs["TopRenderGeometryId"] = &SketcherParamsP::updateTopRenderGeometryId;
        MidRenderGeometryId = this->subHandles[0]->GetInt("MidRenderGeometryId", 2);
        funcs["MidRenderGeometryId"] = &SketcherParamsP::updateMidRenderGeometryId;
        LowRenderGeometryId = this->subHandles[0]->GetInt("LowRenderGeometryId", 3);
        funcs["LowRenderGeometryId"] = &SketcherParamsP::updateLowRenderGeometryId;
        BSplineExternalVisible = this->subHandles[0]->GetBool("BSplineExternalVisible", false);
        funcs["BSplineExternalVisible"] = &SketcherParamsP::updateBSplineExternalVisible;
        EditDatumInPlace = this->subHandles[0]->GetBool("EditDatumInPlace", true);
        funcs["EditDatumInPlace"] = &SketcherParamsP::updateEditDatumInPlace;
        DatumEscapeTakesBack = this->subHandles[0]->GetBool("DatumEscapeTakesBack", false);
        funcs["DatumEscapeTakesBack"] = &SketcherParamsP::updateDatumEscapeTakesBack;
        ShowDirectionalAutoConstraintHints = this->subHandles[0]->GetBool("ShowDirectionalAutoConstraintHints", true);
        funcs["ShowDirectionalAutoConstraintHints"] = &SketcherParamsP::updateShowDirectionalAutoConstraintHints;
        DragAutoConstraintDelay = this->subHandles[0]->GetInt("DragAutoConstraintDelay", 400);
        funcs["DragAutoConstraintDelay"] = &SketcherParamsP::updateDragAutoConstraintDelay;
        NotifyConstraintSubstitutions = this->subHandles[0]->GetBool("NotifyConstraintSubstitutions", true);
        funcs["NotifyConstraintSubstitutions"] = &SketcherParamsP::updateNotifyConstraintSubstitutions;
        EdgeWidth = this->subHandles[2]->GetInt("EdgeWidth", 2);
        funcs["EdgeWidth"] = &SketcherParamsP::updateEdgeWidth;
        EdgePattern = this->subHandles[2]->GetInt("EdgePattern", 65535);
        funcs["EdgePattern"] = &SketcherParamsP::updateEdgePattern;
        ConstructionWidth = this->subHandles[2]->GetInt("ConstructionWidth", 2);
        funcs["ConstructionWidth"] = &SketcherParamsP::updateConstructionWidth;
        ConstructionPattern = this->subHandles[2]->GetInt("ConstructionPattern", 64764);
        funcs["ConstructionPattern"] = &SketcherParamsP::updateConstructionPattern;
        InternalWidth = this->subHandles[2]->GetInt("InternalWidth", 2);
        funcs["InternalWidth"] = &SketcherParamsP::updateInternalWidth;
        InternalPattern = this->subHandles[2]->GetInt("InternalPattern", 64764);
        funcs["InternalPattern"] = &SketcherParamsP::updateInternalPattern;
        ExternalWidth = this->subHandles[2]->GetInt("ExternalWidth", 2);
        funcs["ExternalWidth"] = &SketcherParamsP::updateExternalWidth;
        ExternalPattern = this->subHandles[2]->GetInt("ExternalPattern", 64764);
        funcs["ExternalPattern"] = &SketcherParamsP::updateExternalPattern;
        ExternalDefiningWidth = this->subHandles[2]->GetInt("ExternalDefiningWidth", 2);
        funcs["ExternalDefiningWidth"] = &SketcherParamsP::updateExternalDefiningWidth;
        ExternalDefiningPattern = this->subHandles[2]->GetInt("ExternalDefiningPattern", 65535);
        funcs["ExternalDefiningPattern"] = &SketcherParamsP::updateExternalDefiningPattern;
        InformationWidth = this->subHandles[2]->GetInt("InformationWidth", 1);
        funcs["InformationWidth"] = &SketcherParamsP::updateInformationWidth;
        InformationPattern = this->subHandles[2]->GetInt("InformationPattern", 64764);
        funcs["InformationPattern"] = &SketcherParamsP::updateInformationPattern;
        DimensionalConstraintLineWidth = this->subHandles[2]->GetInt("DimensionalConstraintLineWidth", 2);
        funcs["DimensionalConstraintLineWidth"] = &SketcherParamsP::updateDimensionalConstraintLineWidth;
        DimensionalConstraintLinePattern = this->subHandles[2]->GetInt("DimensionalConstraintLinePattern", 65535);
        funcs["DimensionalConstraintLinePattern"] = &SketcherParamsP::updateDimensionalConstraintLinePattern;
        AxisLineWidth = this->subHandles[2]->GetInt("AxisLineWidth", 2);
        funcs["AxisLineWidth"] = &SketcherParamsP::updateAxisLineWidth;
        AxisLinePattern = this->subHandles[2]->GetInt("AxisLinePattern", 65535);
        funcs["AxisLinePattern"] = &SketcherParamsP::updateAxisLinePattern;
        SingleDimensioningTool = this->subHandles[3]->GetBool("SingleDimensioningTool", true);
        funcs["SingleDimensioningTool"] = &SketcherParamsP::updateSingleDimensioningTool;
        SeparatedDimensioningTools = this->subHandles[3]->GetBool("SeparatedDimensioningTools", false);
        funcs["SeparatedDimensioningTools"] = &SketcherParamsP::updateSeparatedDimensioningTools;
        DimensioningDiameter = this->subHandles[3]->GetBool("DimensioningDiameter", true);
        funcs["DimensioningDiameter"] = &SketcherParamsP::updateDimensioningDiameter;
        DimensioningRadius = this->subHandles[3]->GetBool("DimensioningRadius", true);
        funcs["DimensioningRadius"] = &SketcherParamsP::updateDimensioningRadius;
        AutoScaleMode = this->subHandles[3]->GetInt("AutoScaleMode", 2);
        funcs["AutoScaleMode"] = &SketcherParamsP::updateAutoScaleMode;
        OnViewParameterVisibility = this->subHandles[4]->GetInt("OnViewParameterVisibility", 1);
        funcs["OnViewParameterVisibility"] = &SketcherParamsP::updateOnViewParameterVisibility;
        UnifiedCoincident = this->subHandles[5]->GetBool("UnifiedCoincident", true);
        funcs["UnifiedCoincident"] = &SketcherParamsP::updateUnifiedCoincident;
        AutoHorVer = this->subHandles[5]->GetBool("AutoHorVer", true);
        funcs["AutoHorVer"] = &SketcherParamsP::updateAutoHorVer;
        UnifiedLineCommands = this->subHandles[6]->GetBool("UnifiedLineCommands", true);
        funcs["UnifiedLineCommands"] = &SketcherParamsP::updateUnifiedLineCommands;
        Snap = this->subHandles[7]->GetBool("Snap", true);
        funcs["Snap"] = &SketcherParamsP::updateSnap;
        SnapToObjects = this->subHandles[7]->GetBool("SnapToObjects", true);
        funcs["SnapToObjects"] = &SketcherParamsP::updateSnapToObjects;
        SnapToGrid = this->subHandles[7]->GetBool("SnapToGrid", false);
        funcs["SnapToGrid"] = &SketcherParamsP::updateSnapToGrid;
        SnapAngle = this->subHandles[7]->GetFloat("SnapAngle", 5.0);
        funcs["SnapAngle"] = &SketcherParamsP::updateSnapAngle;
        ElementIconSize = this->subHandles[8]->GetInt("ElementIconSize", 32);
        funcs["ElementIconSize"] = &SketcherParamsP::updateElementIconSize;
        EditSketcherFontName = this->subHandles[9]->GetASCII("EditSketcherFontName", "");
        funcs["EditSketcherFontName"] = &SketcherParamsP::updateEditSketcherFontName;
        ConstraintIconLabelsPerLine = this->subHandles[9]->GetInt("ConstraintIconLabelsPerLine", 10);
        funcs["ConstraintIconLabelsPerLine"] = &SketcherParamsP::updateConstraintIconLabelsPerLine;
        ConstraintIconLabelLines = this->subHandles[9]->GetInt("ConstraintIconLabelLines", 3);
        funcs["ConstraintIconLabelLines"] = &SketcherParamsP::updateConstraintIconLabelLines;
        ViewScalingFactor = this->subHandles[9]->GetFloat("ViewScalingFactor", 1.0);
        funcs["ViewScalingFactor"] = &SketcherParamsP::updateViewScalingFactor;
        SegmentsPerGeometry = this->subHandles[9]->GetInt("SegmentsPerGeometry", 50);
        funcs["SegmentsPerGeometry"] = &SketcherParamsP::updateSegmentsPerGeometry;
        CursorTextColor = this->subHandles[9]->GetUnsigned("CursorTextColor", 0x0000FFFF);
        funcs["CursorTextColor"] = &SketcherParamsP::updateCursorTextColor;
        SketchEdgeColor = this->subHandles[9]->GetUnsigned("SketchEdgeColor", 0xFFFFFFFF);
        funcs["SketchEdgeColor"] = &SketcherParamsP::updateSketchEdgeColor;
        SketchVertexColor = this->subHandles[9]->GetUnsigned("SketchVertexColor", 0xFFFFFFFF);
        funcs["SketchVertexColor"] = &SketcherParamsP::updateSketchVertexColor;
        EditedEdgeColor = this->subHandles[9]->GetUnsigned("EditedEdgeColor", 0xFFFFFFFF);
        funcs["EditedEdgeColor"] = &SketcherParamsP::updateEditedEdgeColor;
        ConstructionColor = this->subHandles[9]->GetUnsigned("ConstructionColor", 0x0000DCFF);
        funcs["ConstructionColor"] = &SketcherParamsP::updateConstructionColor;
        InternalAlignedGeoColor = this->subHandles[9]->GetUnsigned("InternalAlignedGeoColor", 0xB2B27FFF);
        funcs["InternalAlignedGeoColor"] = &SketcherParamsP::updateInternalAlignedGeoColor;
        FullyConstraintElementColor = this->subHandles[9]->GetUnsigned("FullyConstraintElementColor", 0x80D0A0FF);
        funcs["FullyConstraintElementColor"] = &SketcherParamsP::updateFullyConstraintElementColor;
        FullyConstraintConstructionElementColor = this->subHandles[9]->GetUnsigned("FullyConstraintConstructionElementColor", 0x8FA9FDFF);
        funcs["FullyConstraintConstructionElementColor"] = &SketcherParamsP::updateFullyConstraintConstructionElementColor;
        FullyConstraintInternalAlignmentColor = this->subHandles[9]->GetUnsigned("FullyConstraintInternalAlignmentColor", 0xDEDEC8FF);
        funcs["FullyConstraintInternalAlignmentColor"] = &SketcherParamsP::updateFullyConstraintInternalAlignmentColor;
        InvalidSketchColor = this->subHandles[9]->GetUnsigned("InvalidSketchColor", 0xFF6D00FF);
        funcs["InvalidSketchColor"] = &SketcherParamsP::updateInvalidSketchColor;
        FullyConstrainedColor = this->subHandles[9]->GetUnsigned("FullyConstrainedColor", 0x00FF00FF);
        funcs["FullyConstrainedColor"] = &SketcherParamsP::updateFullyConstrainedColor;
        ConstrainedDimColor = this->subHandles[9]->GetUnsigned("ConstrainedDimColor", 0xFF2600FF);
        funcs["ConstrainedDimColor"] = &SketcherParamsP::updateConstrainedDimColor;
        ConstrainedIcoColor = this->subHandles[9]->GetUnsigned("ConstrainedIcoColor", 0xFF2600FF);
        funcs["ConstrainedIcoColor"] = &SketcherParamsP::updateConstrainedIcoColor;
        NonDrivingConstrDimColor = this->subHandles[9]->GetUnsigned("NonDrivingConstrDimColor", 0x0026FFFF);
        funcs["NonDrivingConstrDimColor"] = &SketcherParamsP::updateNonDrivingConstrDimColor;
        ExprBasedConstrDimColor = this->subHandles[9]->GetUnsigned("ExprBasedConstrDimColor", 0xFF7F26FF);
        funcs["ExprBasedConstrDimColor"] = &SketcherParamsP::updateExprBasedConstrDimColor;
        DeactivatedConstrDimColor = this->subHandles[9]->GetUnsigned("DeactivatedConstrDimColor", 0xCCCCCCFF);
        funcs["DeactivatedConstrDimColor"] = &SketcherParamsP::updateDeactivatedConstrDimColor;
        ExternalColor = this->subHandles[9]->GetUnsigned("ExternalColor", 0xCC3399FF);
        funcs["ExternalColor"] = &SketcherParamsP::updateExternalColor;
        ExternalDefiningColor = this->subHandles[9]->GetUnsigned("ExternalDefiningColor", 0xCC3399FF);
        funcs["ExternalDefiningColor"] = &SketcherParamsP::updateExternalDefiningColor;
        InformationColor = this->subHandles[9]->GetUnsigned("InformationColor", 0x00FF00FF);
        funcs["InformationColor"] = &SketcherParamsP::updateInformationColor;
        FrozenColor = this->subHandles[9]->GetUnsigned("FrozenColor", 0x7FFFFFFF);
        funcs["FrozenColor"] = &SketcherParamsP::updateFrozenColor;
        DetachedColor = this->subHandles[9]->GetUnsigned("DetachedColor", 0x1C7F1CFF);
        funcs["DetachedColor"] = &SketcherParamsP::updateDetachedColor;
        MissingColor = this->subHandles[9]->GetUnsigned("MissingColor", 0x7F00FFFF);
        funcs["MissingColor"] = &SketcherParamsP::updateMissingColor;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~SketcherParamsP() override = default;

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
    static void updateAutoRecompute(SketcherParamsP *self) {
        self->AutoRecompute = self->handle->GetBool("AutoRecompute", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoRemoveRedundants(SketcherParamsP *self) {
        self->AutoRemoveRedundants = self->handle->GetBool("AutoRemoveRedundants", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateContinuousCreationMode(SketcherParamsP *self) {
        self->ContinuousCreationMode = self->handle->GetBool("ContinuousCreationMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateContinuousConstraintMode(SketcherParamsP *self) {
        self->ContinuousConstraintMode = self->handle->GetBool("ContinuousConstraintMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowDialogOnDistanceConstraint(SketcherParamsP *self) {
        self->ShowDialogOnDistanceConstraint = self->handle->GetBool("ShowDialogOnDistanceConstraint", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRadiusDiameterConstraintDisplayBaseAngle(SketcherParamsP *self) {
        self->RadiusDiameterConstraintDisplayBaseAngle = self->handle->GetFloat("RadiusDiameterConstraintDisplayBaseAngle", 15.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRadiusDiameterConstraintDisplayAngleRandomness(SketcherParamsP *self) {
        self->RadiusDiameterConstraintDisplayAngleRandomness = self->handle->GetFloat("RadiusDiameterConstraintDisplayAngleRandomness", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGeometryHistoryLevel(SketcherParamsP *self) {
        self->GeometryHistoryLevel = self->handle->GetInt("GeometryHistoryLevel", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateArcFitTolerance(SketcherParamsP *self) {
        self->ArcFitTolerance = self->handle->GetFloat("ArcFitTolerance", 1e-06);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalBSplineMaxDegree(SketcherParamsP *self) {
        self->ExternalBSplineMaxDegree = self->handle->GetInt("ExternalBSplineMaxDegree", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalBSplineTolerance(SketcherParamsP *self) {
        self->ExternalBSplineTolerance = self->handle->GetFloat("ExternalBSplineTolerance", 0.0001);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMakeInternals(SketcherParamsP *self) {
        self->MakeInternals = self->handle->GetBool("MakeInternals", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLeaveSketchWithEscape(SketcherParamsP *self) {
        self->LeaveSketchWithEscape = self->handle->GetBool("LeaveSketchWithEscape", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowSolverAdvancedWidget(SketcherParamsP *self) {
        self->ShowSolverAdvancedWidget = self->handle->GetBool("ShowSolverAdvancedWidget", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRecalculateInitialSolutionWhileDragging(SketcherParamsP *self) {
        self->RecalculateInitialSolutionWhileDragging = self->handle->GetBool("RecalculateInitialSolutionWhileDragging", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExtendedConstraintInformation(SketcherParamsP *self) {
        self->ExtendedConstraintInformation = self->handle->GetBool("ExtendedConstraintInformation", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHideInternalAlignment(SketcherParamsP *self) {
        self->HideInternalAlignment = self->handle->GetBool("HideInternalAlignment", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVisualisationTrackingFilter(SketcherParamsP *self) {
        self->VisualisationTrackingFilter = self->handle->GetBool("VisualisationTrackingFilter", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHideUnits(SketcherParamsP *self) {
        self->HideUnits = self->handle->GetBool("HideUnits", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowDimensionalName(SketcherParamsP *self) {
        self->ShowDimensionalName = self->handle->GetBool("ShowDimensionalName", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensionalStringFormat(SketcherParamsP *self) {
        self->DimensionalStringFormat = self->handle->GetASCII("DimensionalStringFormat", "%N = %V");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowCursorCoords(SketcherParamsP *self) {
        self->ShowCursorCoords = self->handle->GetBool("ShowCursorCoords", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseSystemDecimals(SketcherParamsP *self) {
        self->UseSystemDecimals = self->handle->GetBool("UseSystemDecimals", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAllowFaceExternalPick(SketcherParamsP *self) {
        self->AllowFaceExternalPick = self->subHandles[0]->GetBool("AllowFaceExternalPick", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateViewBottomOnEdit(SketcherParamsP *self) {
        self->ViewBottomOnEdit = self->subHandles[0]->GetBool("ViewBottomOnEdit", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAdjustCamera(SketcherParamsP *self) {
        self->AdjustCamera = self->subHandles[0]->GetBool("AdjustCamera", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFitSketchOnEdit(SketcherParamsP *self) {
        self->FitSketchOnEdit = self->subHandles[0]->GetBool("FitSketchOnEdit", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHideDependent(SketcherParamsP *self) {
        self->HideDependent = self->subHandles[0]->GetBool("HideDependent", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowLinks(SketcherParamsP *self) {
        self->ShowLinks = self->subHandles[0]->GetBool("ShowLinks", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowSupport(SketcherParamsP *self) {
        self->ShowSupport = self->subHandles[0]->GetBool("ShowSupport", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRestoreCamera(SketcherParamsP *self) {
        self->RestoreCamera = self->subHandles[0]->GetBool("RestoreCamera", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateForceOrtho(SketcherParamsP *self) {
        self->ForceOrtho = self->subHandles[0]->GetBool("ForceOrtho", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionView(SketcherParamsP *self) {
        self->SectionView = self->subHandles[0]->GetBool("SectionView", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoConstraints(SketcherParamsP *self) {
        self->AutoConstraints = self->subHandles[0]->GetBool("AutoConstraints", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAvoidRedundantAutoconstraints(SketcherParamsP *self) {
        self->AvoidRedundantAutoconstraints = self->subHandles[0]->GetBool("AvoidRedundantAutoconstraints", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowOriginalColor(SketcherParamsP *self) {
        self->ShowOriginalColor = self->subHandles[0]->GetBool("ShowOriginalColor", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSketchAutoTransparentPick(SketcherParamsP *self) {
        self->SketchAutoTransparentPick = self->subHandles[0]->GetBool("SketchAutoTransparentPick", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateZHeight(SketcherParamsP *self) {
        self->ZHeight = self->subHandles[0]->GetFloat("ZHeight", 1e-06);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisTransparency(SketcherParamsP *self) {
        self->AxisTransparency = self->subHandles[0]->GetInt("AxisTransparency", 30);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFaceColor(SketcherParamsP *self) {
        self->FaceColor = self->subHandles[0]->GetUnsigned("FaceColor", 0x54ABFF7F);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowGrid(SketcherParamsP *self) {
        self->ShowGrid = self->subHandles[0]->GetBool("ShowGrid", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridSize(SketcherParamsP *self) {
        self->GridSize = self->subHandles[1]->GetFloat("GridSize", 10.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridAuto(SketcherParamsP *self) {
        self->GridAuto = self->subHandles[0]->GetBool("GridAuto", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridSizePixelThreshold(SketcherParamsP *self) {
        self->GridSizePixelThreshold = self->subHandles[0]->GetInt("GridSizePixelThreshold", 15);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridNumberSubdivision(SketcherParamsP *self) {
        self->GridNumberSubdivision = self->subHandles[0]->GetInt("GridNumberSubdivision", 10);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridLinePattern(SketcherParamsP *self) {
        self->GridLinePattern = self->subHandles[0]->GetInt("GridLinePattern", 65535);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridDivLinePattern(SketcherParamsP *self) {
        self->GridDivLinePattern = self->subHandles[0]->GetInt("GridDivLinePattern", 65535);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridLineWidth(SketcherParamsP *self) {
        self->GridLineWidth = self->subHandles[0]->GetInt("GridLineWidth", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridDivLineWidth(SketcherParamsP *self) {
        self->GridDivLineWidth = self->subHandles[0]->GetInt("GridDivLineWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridLineColor(SketcherParamsP *self) {
        self->GridLineColor = self->subHandles[0]->GetUnsigned("GridLineColor", 0xB2B2B2FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridDivLineColor(SketcherParamsP *self) {
        self->GridDivLineColor = self->subHandles[0]->GetUnsigned("GridDivLineColor", 0xB2B2B2FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGridTransparency(SketcherParamsP *self) {
        self->GridTransparency = self->subHandles[0]->GetInt("GridTransparency", 60);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTopRenderGeometryId(SketcherParamsP *self) {
        self->TopRenderGeometryId = self->subHandles[0]->GetInt("TopRenderGeometryId", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMidRenderGeometryId(SketcherParamsP *self) {
        self->MidRenderGeometryId = self->subHandles[0]->GetInt("MidRenderGeometryId", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLowRenderGeometryId(SketcherParamsP *self) {
        self->LowRenderGeometryId = self->subHandles[0]->GetInt("LowRenderGeometryId", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBSplineExternalVisible(SketcherParamsP *self) {
        self->BSplineExternalVisible = self->subHandles[0]->GetBool("BSplineExternalVisible", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditDatumInPlace(SketcherParamsP *self) {
        self->EditDatumInPlace = self->subHandles[0]->GetBool("EditDatumInPlace", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDatumEscapeTakesBack(SketcherParamsP *self) {
        self->DatumEscapeTakesBack = self->subHandles[0]->GetBool("DatumEscapeTakesBack", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowDirectionalAutoConstraintHints(SketcherParamsP *self) {
        self->ShowDirectionalAutoConstraintHints = self->subHandles[0]->GetBool("ShowDirectionalAutoConstraintHints", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDragAutoConstraintDelay(SketcherParamsP *self) {
        self->DragAutoConstraintDelay = self->subHandles[0]->GetInt("DragAutoConstraintDelay", 400);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNotifyConstraintSubstitutions(SketcherParamsP *self) {
        self->NotifyConstraintSubstitutions = self->subHandles[0]->GetBool("NotifyConstraintSubstitutions", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEdgeWidth(SketcherParamsP *self) {
        self->EdgeWidth = self->subHandles[2]->GetInt("EdgeWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEdgePattern(SketcherParamsP *self) {
        self->EdgePattern = self->subHandles[2]->GetInt("EdgePattern", 65535);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateConstructionWidth(SketcherParamsP *self) {
        self->ConstructionWidth = self->subHandles[2]->GetInt("ConstructionWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateConstructionPattern(SketcherParamsP *self) {
        self->ConstructionPattern = self->subHandles[2]->GetInt("ConstructionPattern", 64764);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInternalWidth(SketcherParamsP *self) {
        self->InternalWidth = self->subHandles[2]->GetInt("InternalWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInternalPattern(SketcherParamsP *self) {
        self->InternalPattern = self->subHandles[2]->GetInt("InternalPattern", 64764);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalWidth(SketcherParamsP *self) {
        self->ExternalWidth = self->subHandles[2]->GetInt("ExternalWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalPattern(SketcherParamsP *self) {
        self->ExternalPattern = self->subHandles[2]->GetInt("ExternalPattern", 64764);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalDefiningWidth(SketcherParamsP *self) {
        self->ExternalDefiningWidth = self->subHandles[2]->GetInt("ExternalDefiningWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalDefiningPattern(SketcherParamsP *self) {
        self->ExternalDefiningPattern = self->subHandles[2]->GetInt("ExternalDefiningPattern", 65535);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInformationWidth(SketcherParamsP *self) {
        self->InformationWidth = self->subHandles[2]->GetInt("InformationWidth", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInformationPattern(SketcherParamsP *self) {
        self->InformationPattern = self->subHandles[2]->GetInt("InformationPattern", 64764);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensionalConstraintLineWidth(SketcherParamsP *self) {
        self->DimensionalConstraintLineWidth = self->subHandles[2]->GetInt("DimensionalConstraintLineWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensionalConstraintLinePattern(SketcherParamsP *self) {
        self->DimensionalConstraintLinePattern = self->subHandles[2]->GetInt("DimensionalConstraintLinePattern", 65535);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisLineWidth(SketcherParamsP *self) {
        self->AxisLineWidth = self->subHandles[2]->GetInt("AxisLineWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisLinePattern(SketcherParamsP *self) {
        self->AxisLinePattern = self->subHandles[2]->GetInt("AxisLinePattern", 65535);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSingleDimensioningTool(SketcherParamsP *self) {
        self->SingleDimensioningTool = self->subHandles[3]->GetBool("SingleDimensioningTool", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSeparatedDimensioningTools(SketcherParamsP *self) {
        self->SeparatedDimensioningTools = self->subHandles[3]->GetBool("SeparatedDimensioningTools", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensioningDiameter(SketcherParamsP *self) {
        self->DimensioningDiameter = self->subHandles[3]->GetBool("DimensioningDiameter", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensioningRadius(SketcherParamsP *self) {
        self->DimensioningRadius = self->subHandles[3]->GetBool("DimensioningRadius", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoScaleMode(SketcherParamsP *self) {
        self->AutoScaleMode = self->subHandles[3]->GetInt("AutoScaleMode", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOnViewParameterVisibility(SketcherParamsP *self) {
        self->OnViewParameterVisibility = self->subHandles[4]->GetInt("OnViewParameterVisibility", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUnifiedCoincident(SketcherParamsP *self) {
        self->UnifiedCoincident = self->subHandles[5]->GetBool("UnifiedCoincident", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoHorVer(SketcherParamsP *self) {
        self->AutoHorVer = self->subHandles[5]->GetBool("AutoHorVer", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUnifiedLineCommands(SketcherParamsP *self) {
        self->UnifiedLineCommands = self->subHandles[6]->GetBool("UnifiedLineCommands", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSnap(SketcherParamsP *self) {
        self->Snap = self->subHandles[7]->GetBool("Snap", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSnapToObjects(SketcherParamsP *self) {
        self->SnapToObjects = self->subHandles[7]->GetBool("SnapToObjects", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSnapToGrid(SketcherParamsP *self) {
        self->SnapToGrid = self->subHandles[7]->GetBool("SnapToGrid", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSnapAngle(SketcherParamsP *self) {
        self->SnapAngle = self->subHandles[7]->GetFloat("SnapAngle", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateElementIconSize(SketcherParamsP *self) {
        self->ElementIconSize = self->subHandles[8]->GetInt("ElementIconSize", 32);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditSketcherFontName(SketcherParamsP *self) {
        self->EditSketcherFontName = self->subHandles[9]->GetASCII("EditSketcherFontName", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateConstraintIconLabelsPerLine(SketcherParamsP *self) {
        self->ConstraintIconLabelsPerLine = self->subHandles[9]->GetInt("ConstraintIconLabelsPerLine", 10);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateConstraintIconLabelLines(SketcherParamsP *self) {
        self->ConstraintIconLabelLines = self->subHandles[9]->GetInt("ConstraintIconLabelLines", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateViewScalingFactor(SketcherParamsP *self) {
        self->ViewScalingFactor = self->subHandles[9]->GetFloat("ViewScalingFactor", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSegmentsPerGeometry(SketcherParamsP *self) {
        self->SegmentsPerGeometry = self->subHandles[9]->GetInt("SegmentsPerGeometry", 50);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCursorTextColor(SketcherParamsP *self) {
        self->CursorTextColor = self->subHandles[9]->GetUnsigned("CursorTextColor", 0x0000FFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSketchEdgeColor(SketcherParamsP *self) {
        self->SketchEdgeColor = self->subHandles[9]->GetUnsigned("SketchEdgeColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSketchVertexColor(SketcherParamsP *self) {
        self->SketchVertexColor = self->subHandles[9]->GetUnsigned("SketchVertexColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditedEdgeColor(SketcherParamsP *self) {
        self->EditedEdgeColor = self->subHandles[9]->GetUnsigned("EditedEdgeColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateConstructionColor(SketcherParamsP *self) {
        self->ConstructionColor = self->subHandles[9]->GetUnsigned("ConstructionColor", 0x0000DCFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInternalAlignedGeoColor(SketcherParamsP *self) {
        self->InternalAlignedGeoColor = self->subHandles[9]->GetUnsigned("InternalAlignedGeoColor", 0xB2B27FFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFullyConstraintElementColor(SketcherParamsP *self) {
        self->FullyConstraintElementColor = self->subHandles[9]->GetUnsigned("FullyConstraintElementColor", 0x80D0A0FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFullyConstraintConstructionElementColor(SketcherParamsP *self) {
        self->FullyConstraintConstructionElementColor = self->subHandles[9]->GetUnsigned("FullyConstraintConstructionElementColor", 0x8FA9FDFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFullyConstraintInternalAlignmentColor(SketcherParamsP *self) {
        self->FullyConstraintInternalAlignmentColor = self->subHandles[9]->GetUnsigned("FullyConstraintInternalAlignmentColor", 0xDEDEC8FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInvalidSketchColor(SketcherParamsP *self) {
        self->InvalidSketchColor = self->subHandles[9]->GetUnsigned("InvalidSketchColor", 0xFF6D00FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFullyConstrainedColor(SketcherParamsP *self) {
        self->FullyConstrainedColor = self->subHandles[9]->GetUnsigned("FullyConstrainedColor", 0x00FF00FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateConstrainedDimColor(SketcherParamsP *self) {
        self->ConstrainedDimColor = self->subHandles[9]->GetUnsigned("ConstrainedDimColor", 0xFF2600FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateConstrainedIcoColor(SketcherParamsP *self) {
        self->ConstrainedIcoColor = self->subHandles[9]->GetUnsigned("ConstrainedIcoColor", 0xFF2600FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNonDrivingConstrDimColor(SketcherParamsP *self) {
        self->NonDrivingConstrDimColor = self->subHandles[9]->GetUnsigned("NonDrivingConstrDimColor", 0x0026FFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExprBasedConstrDimColor(SketcherParamsP *self) {
        self->ExprBasedConstrDimColor = self->subHandles[9]->GetUnsigned("ExprBasedConstrDimColor", 0xFF7F26FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDeactivatedConstrDimColor(SketcherParamsP *self) {
        self->DeactivatedConstrDimColor = self->subHandles[9]->GetUnsigned("DeactivatedConstrDimColor", 0xCCCCCCFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalColor(SketcherParamsP *self) {
        self->ExternalColor = self->subHandles[9]->GetUnsigned("ExternalColor", 0xCC3399FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalDefiningColor(SketcherParamsP *self) {
        self->ExternalDefiningColor = self->subHandles[9]->GetUnsigned("ExternalDefiningColor", 0xCC3399FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInformationColor(SketcherParamsP *self) {
        self->InformationColor = self->subHandles[9]->GetUnsigned("InformationColor", 0x00FF00FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFrozenColor(SketcherParamsP *self) {
        self->FrozenColor = self->subHandles[9]->GetUnsigned("FrozenColor", 0x7FFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDetachedColor(SketcherParamsP *self) {
        self->DetachedColor = self->subHandles[9]->GetUnsigned("DetachedColor", 0x1C7F1CFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMissingColor(SketcherParamsP *self) {
        self->MissingColor = self->subHandles[9]->GetUnsigned("MissingColor", 0x7F00FFFF);
    }
};

// Auto generated code (Tools/params_utils.py:336)
SketcherParamsP *instance() {
    static SketcherParamsP *inst = new SketcherParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _SketcherParamsRegistrar({
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "AutoRecompute", "AutoRecompute", App::ParamInfo::Bool, false)
        .setTitle("Auto update")
        .setDoc("Recompute the active document after every sketch action. Set from\n"
"the Auto-update box of the solver messages panel; applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "AutoRemoveRedundants", "AutoRemoveRedundants", App::ParamInfo::Bool, false)
        .setTitle("Auto remove redundant constraints")
        .setDoc("Remove redundant constraints automatically when they are detected.\n"
"Works only while Auto-update is on. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ContinuousCreationMode", "ContinuousCreationMode", App::ParamInfo::Bool, true)
        .setTitle("Geometry creation continue mode")
        .setDoc("Keep a geometry tool active after it creates an element, so the\n"
"next one can be drawn at once. Applies to the next element."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ContinuousConstraintMode", "ContinuousConstraintMode", App::ParamInfo::Bool, true)
        .setTitle("Constraint creation continue mode")
        .setDoc("Keep a constraint tool active after it creates a constraint.\n"
"Applies the next time a constraint tool is started."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ShowDialogOnDistanceConstraint", "ShowDialogOnDistanceConstraint", App::ParamInfo::Bool, true)
        .setTitle("Ask for value after creating a dimensional constraint")
        .setDoc("Ask for the value right after a dimensional constraint is created.\n"
"Applies to the next constraint."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "RadiusDiameterConstraintDisplayBaseAngle", "RadiusDiameterConstraintDisplayBaseAngle", App::ParamInfo::Float, 15.0)
        .setTitle("Radius and diameter label angle")
        .setDoc("Angle in degrees at which the label of a new radius or diameter\n"
"constraint is placed. Applies to new constraints."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "RadiusDiameterConstraintDisplayAngleRandomness", "RadiusDiameterConstraintDisplayAngleRandomness", App::ParamInfo::Float, 0.0)
        .setTitle("Radius and diameter label angle spread")
        .setDoc("Random spread in degrees added around the base angle of new radius\n"
"or diameter labels, so labels overlap less."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "GeometryHistoryLevel", "GeometryHistoryLevel", App::ParamInfo::Int, 1)
        .setTitle("Geometry history level")
        .setDoc("How much geometry history a sketch keeps for stable element names:\n"
"0 none, 1 default, above 1 keeps more. Read when a sketch object\n"
"is created or loaded."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ArcFitTolerance", "ArcFitTolerance", App::ParamInfo::Float, 1e-06)
        .setTitle("Arc fit tolerance")
        .setDoc("Tolerance used to fit arcs for a new sketch; copied into the\n"
"sketch's ArcFitTolerance property when it is created."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ExternalBSplineMaxDegree", "ExternalBSplineMaxDegree", App::ParamInfo::Int, 5)
        .setTitle("External B-spline maximum degree")
        .setDoc("Maximum degree of B-splines made from external geometry in a new\n"
"sketch. Copied into the sketch when it is created."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ExternalBSplineTolerance", "ExternalBSplineTolerance", App::ParamInfo::Float, 0.0001)
        .setTitle("External B-spline tolerance")
        .setDoc("Tolerance of B-splines made from external geometry in a new\n"
"sketch.0001. Copied into the sketch when it is created."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "MakeInternals", "MakeInternals", App::ParamInfo::Bool, true)
        .setTitle("Generate internal faces")
        .setDoc("Generate internal faces from the closed regions of a new sketch.\n"
"Copied into the sketch's MakeInternals property when it is\n"
"created."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "LeaveSketchWithEscape", "LeaveSketchWithEscape", App::ParamInfo::Bool, true)
        .setTitle("Esc can leave sketch edit mode")
        .setDoc("Let the Esc key leave sketch edit mode. Takes effect the next time\n"
"a sketch is edited."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ShowSolverAdvancedWidget", "ShowSolverAdvancedWidget", App::ParamInfo::Bool, false)
        .setTitle("Show section 'Advanced solver control'")
        .setDoc("Show the Advanced solver control section in the sketch task panel.\n"
"Takes effect the next time a sketch is edited."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "RecalculateInitialSolutionWhileDragging", "RecalculateInitialSolutionWhileDragging", App::ParamInfo::Bool, true)
        .setTitle("Improve solving while dragging")
        .setDoc("Recalculate the solver's starting point while a point is dragged,\n"
"which improves solving. Takes effect the next time a sketch is\n"
"edited."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ExtendedConstraintInformation", "ExtendedConstraintInformation", App::ParamInfo::Bool, false)
        .setTitle("Extended constraint information")
        .setDoc("Show extended information for each entry of the constraint list.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "HideInternalAlignment", "HideInternalAlignment", App::ParamInfo::Bool, false)
        .setTitle("Hide internal alignment constraints")
        .setDoc("Hide internal alignment constraints in the constraint list.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "VisualisationTrackingFilter", "VisualisationTrackingFilter", App::ParamInfo::Bool, false)
        .setTitle("Show only filtered constraints in the 3D view")
        .setDoc("Show in the 3D view only the constraints that pass the constraint\n"
"list filter. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "HideUnits", "HideUnits", App::ParamInfo::Bool, false)
        .setTitle("Hide base length units")
        .setDoc("Hide the base length unit in dimension labels and cursor\n"
"coordinates for unit systems that support it. Shown at the next\n"
"redraw."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ShowDimensionalName", "ShowDimensionalName", App::ParamInfo::Bool, true)
        .setTitle("Show dimensional constraint name")
        .setDoc("Show the name of a named dimensional constraint in its label,\n"
"using the format string. Shown at the next redraw."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "DimensionalStringFormat", "DimensionalStringFormat", App::ParamInfo::String, "%N = %V")
        .setTitle("Dimensional constraint label format")
        .setDoc("Format of a named dimension's label: %N is the constraint name, %V\n"
"its value. Shown at the next redraw."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ShowCursorCoords", "ShowCursorCoords", App::ParamInfo::Bool, true)
        .setTitle("Show coordinates beside cursor")
        .setDoc("Show the coordinates next to the cursor while drawing in a sketch.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "UseSystemDecimals", "UseSystemDecimals", App::ParamInfo::Bool, true)
        .setTitle("Use system decimals for cursor coordinates")
        .setDoc("Show cursor coordinates with the number of decimals of the unit\n"
"settings instead of a short form. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "AllowFaceExternalPick", "AllowFaceExternalPick", App::ParamInfo::Bool, true)
        .setTitle("Allow picking faces as external geometry")
        .setDoc("Allow picking a face as external geometry. Set in the Edit\n"
"controls of the sketch task panel; applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "ViewBottomOnEdit", "ViewBottomOnEdit", App::ParamInfo::Bool, false)
        .setTitle("View sketch from bottom")
        .setDoc("Look at the sketch from below instead of from above when it is\n"
"edited. Set by the view-sketch-from-bottom commands."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "AdjustCamera", "AdjustCamera", App::ParamInfo::Bool, true)
        .setTitle("Adjust camera when a sketch is edited")
        .setDoc("Turn the camera to face the sketch plane when a sketch is opened\n"
"for editing. Applies the next time a sketch is edited."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "FitSketchOnEdit", "FitSketchOnEdit", App::ParamInfo::Bool, true)
        .setTitle("Fit sketch when it is edited")
        .setDoc("Fit the view to the sketch's geometry when it is opened for\n"
"editing. Applies the next time a sketch is edited."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "HideDependent", "HideDependent", App::ParamInfo::Bool, true)
        .setTitle("Hide objects depending on the sketch")
        .setDoc("Hide the objects that depend on a sketch while it is edited, for\n"
"new sketches."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "ShowLinks", "ShowLinks", App::ParamInfo::Bool, true)
        .setTitle("Show objects the sketch links to")
        .setDoc("Keep the objects a sketch links to visible while it is edited, for\n"
"new sketches."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "ShowSupport", "ShowSupport", App::ParamInfo::Bool, true)
        .setTitle("Show the sketch's support")
        .setDoc("Keep the object a sketch is attached to visible while it is\n"
"edited, for new sketches."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "RestoreCamera", "RestoreCamera", App::ParamInfo::Bool, true)
        .setTitle("Restore camera when leaving a sketch")
        .setDoc("Put the camera back where it was when editing of a sketch ends,\n"
"for new sketches."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "ForceOrtho", "ForceOrtho", App::ParamInfo::Bool, false)
        .setTitle("Force orthographic camera in a sketch")
        .setDoc("Switch the view to an orthographic camera while a sketch is\n"
"edited, and back afterwards, for new sketches. Needs\n"
"RestoreCamera."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "SectionView", "SectionView", App::ParamInfo::Bool, false)
        .setTitle("Section view in a sketch")
        .setDoc("Clip everything in front of the sketch plane while a sketch is\n"
"edited, for new sketches."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "AutoConstraints", "AutoConstraints", App::ParamInfo::Bool, true)
        .setTitle("Automatic constraints")
        .setDoc("Suggest and apply automatic constraints while drawing, for new\n"
"sketches."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "AvoidRedundantAutoconstraints", "AvoidRedundantAutoconstraints", App::ParamInfo::Bool, true)
        .setTitle("Avoid redundant automatic constraints")
        .setDoc("Do not create automatic constraints that would be redundant, for\n"
"new sketches."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "ShowOriginalColor", "ShowOriginalColor", App::ParamInfo::Bool, false)
        .setTitle("Show original colours while editing")
        .setDoc("Draw the sketch in its original colours instead of the constraint-\n"
"status colours while editing. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "SketchAutoTransparentPick", "SketchAutoTransparentPick", App::ParamInfo::Bool, false)
        .setTitle("Transparent picking of external geometry")
        .setDoc("While picking external geometry, make objects transparent to\n"
"picking so hidden edges can be chosen. Applies at once while the\n"
"tool is active."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "ZHeight", "ZHeight", App::ParamInfo::Float, 1e-06)
        .setTitle("Height step between sketch layers")
        .setDoc("Height step between the drawing layers of a sketch in edit mode\n"
"(lines, constraints, points). Raise it if elements flicker through\n"
"each other. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "AxisTransparency", "AxisTransparency", App::ParamInfo::Int, 30)
        .setTitle("Sketch axes transparency")
        .setDoc("Transparency of the sketch axes in edit mode, in percent. 0 to\n"
"100. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "FaceColor", "FaceColor", App::ParamInfo::Hex, 0x54ABFF7F)
        .setTitle("Internal face colour")
        .setDoc("Colour and opacity of the faces shown inside a sketch's closed\n"
"regions. Applies at once to sketches that use automatic colours.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "ShowGrid", "ShowGrid", App::ParamInfo::Bool, true)
        .setTitle("Show grid in new sketches")
        .setDoc("Show a grid in new sketches while they are edited. An existing\n"
"sketch keeps its own setting."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General/GridSize", "GridSize", "GridSize", App::ParamInfo::Float, 10.0)
        .setTitle("Grid spacing")
        .setDoc("Distance in millimetres between two grid lines of a new sketch;\n"
"with automatic spacing, the spacing it starts from. A sketch that\n"
"exists keeps its own."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridAuto", "GridAuto", App::ParamInfo::Bool, true)
        .setTitle("Automatic grid spacing")
        .setDoc("Let the grid spacing of new sketches adapt to the zoom level."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridSizePixelThreshold", "GridSizePixelThreshold", App::ParamInfo::Int, 15)
        .setTitle("Grid pixel threshold")
        .setDoc("With auto spacing, the smallest distance in pixels between two\n"
"grid lines before the grid switches to a coarser spacing. 3 to\n"
"10000. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridNumberSubdivision", "GridNumberSubdivision", App::ParamInfo::Int, 10)
        .setTitle("Grid subdivisions")
        .setDoc("Number of grid cells between two major (division) lines. 1 to\n"
"10000. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridLinePattern", "GridLinePattern", App::ParamInfo::Int, 65535)
        .setTitle("Minor grid line pattern")
        .setDoc("Line pattern of the minor grid lines, as a 16 bit stipple mask.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridDivLinePattern", "GridDivLinePattern", App::ParamInfo::Int, 65535)
        .setTitle("Major grid line pattern")
        .setDoc("Line pattern of the major grid lines, as a 16 bit stipple mask.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridLineWidth", "GridLineWidth", App::ParamInfo::Int, 1)
        .setTitle("Minor grid line width")
        .setDoc("Width of the minor grid lines in pixels. 1 to 99. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridDivLineWidth", "GridDivLineWidth", App::ParamInfo::Int, 2)
        .setTitle("Major grid line width")
        .setDoc("Width of the major grid lines in pixels. 1 to 99. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridLineColor", "GridLineColor", App::ParamInfo::Hex, 0xB2B2B2FF)
        .setTitle("Minor grid line colour")
        .setDoc("Colour of the minor grid lines. Applies at once.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridDivLineColor", "GridDivLineColor", App::ParamInfo::Hex, 0xB2B2B2FF)
        .setTitle("Major grid line colour")
        .setDoc("Colour of the major grid lines. Applies at once.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "GridTransparency", "GridTransparency", App::ParamInfo::Int, 60)
        .setTitle("Grid transparency")
        .setDoc("Transparency of the grid lines in percent, 0 is opaque. 0 to 100.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "TopRenderGeometryId", "TopRenderGeometryId", App::ParamInfo::Int, 1)
        .setTitle("Geometry drawn on top")
        .setDoc("Which kind of geometry is drawn on top in sketch edit mode: 1\n"
"normal, 2 construction, 3 external. Set by dragging in the\n"
"Rendering order list; applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "MidRenderGeometryId", "MidRenderGeometryId", App::ParamInfo::Int, 2)
        .setTitle("Geometry drawn in the middle")
        .setDoc("Which kind of geometry is drawn in the middle: 1 normal, 2\n"
"construction, 3 external."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "LowRenderGeometryId", "LowRenderGeometryId", App::ParamInfo::Int, 3)
        .setTitle("Geometry drawn at the bottom")
        .setDoc("Which kind of geometry is drawn at the bottom: 1 normal, 2\n"
"construction, 3 external."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "BSplineExternalVisible", "BSplineExternalVisible", App::ParamInfo::Bool, false)
        .setTitle("B-spline information of external geometry")
        .setDoc("Show the B-spline information overlays for external B-splines too."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "EditDatumInPlace", "EditDatumInPlace", App::ParamInfo::Bool, true)
        .setTitle("Edit dimensions at their label")
        .setDoc("Edit a dimension's value in a field at its label instead of in a\n"
"dialog. Applies to the next edit."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "DatumEscapeTakesBack", "DatumEscapeTakesBack", App::ParamInfo::Bool, false)
        .setTitle("Esc cancels a dimension being typed")
        .setDoc("Esc cancels the dimension value being typed at a label instead of\n"
"leaving the field with the value entered."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "ShowDirectionalAutoConstraintHints", "ShowDirectionalAutoConstraintHints", App::ParamInfo::Bool, true)
        .setTitle("Show hints for direction constraints")
        .setDoc("Show helper lines for direction based automatic constraints (line\n"
"extension, parallel, perpendicular) while drawing. Applies at\n"
"once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "DragAutoConstraintDelay", "DragAutoConstraintDelay", App::ParamInfo::Int, 400)
        .setTitle("Delay of automatic constraints while dragging")
        .setDoc("Time in milliseconds the pointer must rest while dragging before\n"
"an automatic constraint is offered. 0 to 5000."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/General", "NotifyConstraintSubstitutions", "NotifyConstraintSubstitutions", App::ParamInfo::Bool, true)
        .setTitle("Notify automatic constraint substitutions")
        .setDoc("Show a message when the Sketcher replaces constraints\n"
"automatically, for example a coincident and a tangent by an\n"
"endpoint tangency."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "EdgeWidth", "EdgeWidth", App::ParamInfo::Int, 2)
        .setTitle("Normal geometry line width")
        .setDoc("Line width of normal geometry in edit mode, in pixels. 1 to 99.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "EdgePattern", "EdgePattern", App::ParamInfo::Int, 65535)
        .setTitle("Normal geometry line pattern")
        .setDoc("Line pattern of normal geometry in edit mode, a 16 bit stipple\n"
"mask."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "ConstructionWidth", "ConstructionWidth", App::ParamInfo::Int, 2)
        .setTitle("Construction geometry line width")
        .setDoc("Line width of construction geometry in edit mode, in pixels."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "ConstructionPattern", "ConstructionPattern", App::ParamInfo::Int, 64764)
        .setTitle("Construction geometry line pattern")
        .setDoc("Line pattern of construction geometry in edit mode."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "InternalWidth", "InternalWidth", App::ParamInfo::Int, 2)
        .setTitle("Internal alignment line width")
        .setDoc("Line width of internal alignment geometry in edit mode, in pixels."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "InternalPattern", "InternalPattern", App::ParamInfo::Int, 64764)
        .setTitle("Internal alignment line pattern")
        .setDoc("Line pattern of internal alignment geometry in edit mode."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "ExternalWidth", "ExternalWidth", App::ParamInfo::Int, 2)
        .setTitle("External geometry line width")
        .setDoc("Line width of external geometry in edit mode, in pixels."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "ExternalPattern", "ExternalPattern", App::ParamInfo::Int, 64764)
        .setTitle("External geometry line pattern")
        .setDoc("Line pattern of external geometry in edit mode."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "ExternalDefiningWidth", "ExternalDefiningWidth", App::ParamInfo::Int, 2)
        .setTitle("Defining external geometry line width")
        .setDoc("Line width of defining external geometry in edit mode, in pixels."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "ExternalDefiningPattern", "ExternalDefiningPattern", App::ParamInfo::Int, 65535)
        .setTitle("Defining external geometry line pattern")
        .setDoc("Line pattern of defining external geometry in edit mode."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "InformationWidth", "InformationWidth", App::ParamInfo::Int, 1)
        .setTitle("Information layer line width")
        .setDoc("Line width of the information layer (B-spline polygons, combs,\n"
"hints), in pixels."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "InformationPattern", "InformationPattern", App::ParamInfo::Int, 64764)
        .setTitle("Information layer line pattern")
        .setDoc("Line pattern of the information layer."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "DimensionalConstraintLineWidth", "DimensionalConstraintLineWidth", App::ParamInfo::Int, 2)
        .setTitle("Dimensional constraint line width")
        .setDoc("Line width of dimensional constraints, in pixels. 1 to 4."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "DimensionalConstraintLinePattern", "DimensionalConstraintLinePattern", App::ParamInfo::Int, 65535)
        .setTitle("Dimensional constraint line pattern")
        .setDoc("Line pattern of dimensional constraints."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "AxisLineWidth", "AxisLineWidth", App::ParamInfo::Int, 2)
        .setTitle("Sketch axes line width")
        .setDoc("Line width of the sketch axes in edit mode, in pixels."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/View", "AxisLinePattern", "AxisLinePattern", App::ParamInfo::Int, 65535)
        .setTitle("Sketch axes line pattern")
        .setDoc("Line pattern of the sketch axes in edit mode."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning", "SingleDimensioningTool", "SingleDimensioningTool", App::ParamInfo::Bool, true)
        .setTitle("Single dimension tool")
        .setDoc("Put the single Dimension tool on the Sketcher tool bar. Together\n"
"with the separated tools option this gives the modes Single tool,\n"
"Separated tools, Both. Tool bars are rebuilt when the page is\n"
"saved."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning", "SeparatedDimensioningTools", "SeparatedDimensioningTools", App::ParamInfo::Bool, false)
        .setTitle("Separated dimension tools")
        .setDoc("Put the separate dimension tools (horizontal, vertical, distance,\n"
"radius/diameter, angle, lock) on the Sketcher tool bar."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning", "DimensioningDiameter", "DimensioningDiameter", App::ParamInfo::Bool, true)
        .setTitle("Dimension tool makes diameters")
        .setDoc("Let the Dimension tool create diameters. With radius also on the\n"
"tool picks: diameter for circles, radius for arcs."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning", "DimensioningRadius", "DimensioningRadius", App::ParamInfo::Bool, true)
        .setTitle("Dimension tool makes radii")
        .setDoc("Let the Dimension tool create radii. With diameter also on the\n"
"tool picks: diameter for circles, radius for arcs."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning", "AutoScaleMode", "AutoScaleMode", App::ParamInfo::Int, 2)
        .setTitle("Scale sketch to first dimension")
        .setDoc("Scale the whole sketch to the value of its first dimension: 0\n"
"always, 1 never, 2 only when no feature that fixes the scale is\n"
"visible. Applies to the next dimension."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools", "OnViewParameterVisibility", "OnViewParameterVisibility", App::ParamInfo::Int, 1)
        .setTitle("On-view parameters")
        .setDoc("Which on-view parameters a drawing tool shows at the cursor: 0\n"
"none, 1 dimensions only, 2 position and dimensions. Applies to the\n"
"next tool started."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/Constraints", "UnifiedCoincident", "UnifiedCoincident", App::ParamInfo::Bool, true)
        .setTitle("Unify coincident and point-on-object")
        .setDoc("Use one tool for coincident and point-on-object constraints. Tool\n"
"bars and menus follow when the page is saved; the shortcuts follow\n"
"after a restart."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/Constraints", "AutoHorVer", "AutoHorVer", App::ParamInfo::Bool, true)
        .setTitle("Automatic horizontal or vertical tool")
        .setDoc("Use one tool that chooses between a horizontal and a vertical\n"
"constraint. Tool bars follow when the page is saved."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/Commands", "UnifiedLineCommands", "UnifiedLineCommands", App::ParamInfo::Bool, true)
        .setTitle("Group line and polyline")
        .setDoc("Group the polyline and line commands under one tool bar button.\n"
"Tool bars follow when the page is saved."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/Snap", "Snap", "Snap", App::ParamInfo::Bool, true)
        .setTitle("Snap")
        .setDoc("Master switch for snapping in sketch edit mode. Toggled by the\n"
"Snap tool bar button; applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/Snap", "SnapToObjects", "SnapToObjects", App::ParamInfo::Bool, true)
        .setTitle("Snap to objects")
        .setDoc("Snap new points to the preselected object, and to the middle of\n"
"lines and arcs. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/Snap", "SnapToGrid", "SnapToGrid", App::ParamInfo::Bool, false)
        .setTitle("Snap to grid")
        .setDoc("Snap new points to the nearest grid line when closer than a fifth\n"
"of the grid spacing. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/Snap", "SnapAngle", "SnapAngle", App::ParamInfo::Float, 5.0)
        .setTitle("Snap angle")
        .setDoc("Angular step in degrees for tools that snap at an angle while Ctrl\n"
"is held, measured from the sketch's positive X axis. Applies at\n"
"once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher/Elements", "ElementIconSize", "ElementIconSize", App::ParamInfo::Int, 32)
        .setTitle("Element list icon size")
        .setDoc("Size in pixels of the icons in the element list of the sketch task\n"
"panel. 16 to 128. Takes effect the next time a sketch is edited."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "EditSketcherFontName", "EditSketcherFontName", App::ParamInfo::String, "")
        .setTitle("Sketch label font")
        .setDoc("Font family of the dimension labels in sketch edit mode. Empty\n"
"means the labels' own font. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "ConstraintIconLabelsPerLine", "ConstraintIconLabelsPerLine", App::ParamInfo::Int, 10)
        .setTitle("Constraint numbers per line")
        .setDoc("How many constraint numbers fit on one line of the label beside a\n"
"combined constraint icon, 1 to 100."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "ConstraintIconLabelLines", "ConstraintIconLabelLines", App::ParamInfo::Int, 3)
        .setTitle("Lines of constraint numbers")
        .setDoc("Largest number of lines of the label beside a combined constraint\n"
"icon, 1 to 100."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "ViewScalingFactor", "ViewScalingFactor", App::ParamInfo::Float, 1.0)
        .setTitle("Sketch view scale factor")
        .setDoc("Scale factor of the fixed pixel sizes of sketch edit mode (points,\n"
"constraint lines), 0.5 to 5. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "SegmentsPerGeometry", "SegmentsPerGeometry", App::ParamInfo::Int, 50)
        .setTitle("Segments per geometry")
        .setDoc("Number of straight segments a curve is drawn with in sketch edit\n"
"mode. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "CursorTextColor", "CursorTextColor", App::ParamInfo::Hex, 0x0000FFFF)
        .setTitle("Cursor text colour")
        .setDoc("Colour of the coordinate text shown at the cursor in sketch edit\n"
"mode. Takes effect the next time a sketch is edited.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "SketchEdgeColor", "SketchEdgeColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Sketch edge colour")
        .setDoc("Colour of a sketch's edges outside edit mode, for sketches that\n"
"use automatic colours. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "SketchVertexColor", "SketchVertexColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Sketch vertex colour")
        .setDoc("Colour of a sketch's vertices outside edit mode, for sketches that\n"
"use automatic colours. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "EditedEdgeColor", "EditedEdgeColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Geometry colour")
        .setDoc("Colour of normal geometry in sketch edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "ConstructionColor", "ConstructionColor", App::ParamInfo::Hex, 0x0000DCFF)
        .setTitle("Construction geometry colour")
        .setDoc("Colour of construction geometry in sketch edit mode. Applies at\n"
"once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "InternalAlignedGeoColor", "InternalAlignedGeoColor", App::ParamInfo::Hex, 0xB2B27FFF)
        .setTitle("Internal alignment colour")
        .setDoc("Colour of internal alignment geometry in sketch edit mode. Applies\n"
"at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "FullyConstraintElementColor", "FullyConstraintElementColor", App::ParamInfo::Hex, 0x80D0A0FF)
        .setTitle("Fully constrained geometry colour")
        .setDoc("Colour of a fully constrained element of normal geometry in sketch\n"
"edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "FullyConstraintConstructionElementColor", "FullyConstraintConstructionElementColor", App::ParamInfo::Hex, 0x8FA9FDFF)
        .setTitle("Fully constrained construction colour")
        .setDoc("Colour of a fully constrained element of construction geometry in\n"
"sketch edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "FullyConstraintInternalAlignmentColor", "FullyConstraintInternalAlignmentColor", App::ParamInfo::Hex, 0xDEDEC8FF)
        .setTitle("Fully constrained internal alignment colour")
        .setDoc("Colour of a fully constrained element of internal alignment\n"
"geometry in sketch edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "InvalidSketchColor", "InvalidSketchColor", App::ParamInfo::Hex, 0xFF6D00FF)
        .setTitle("Invalid sketch colour")
        .setDoc("Colour of the geometry of a sketch with conflicting or redundant\n"
"constraints in sketch edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "FullyConstrainedColor", "FullyConstrainedColor", App::ParamInfo::Hex, 0x00FF00FF)
        .setTitle("Fully constrained sketch colour")
        .setDoc("Colour of the geometry of a fully constrained sketch in sketch\n"
"edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "ConstrainedDimColor", "ConstrainedDimColor", App::ParamInfo::Hex, 0xFF2600FF)
        .setTitle("Dimensional constraint colour")
        .setDoc("Colour of dimensional constraints in sketch edit mode. Applies at\n"
"once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "ConstrainedIcoColor", "ConstrainedIcoColor", App::ParamInfo::Hex, 0xFF2600FF)
        .setTitle("Constraint symbol colour")
        .setDoc("Colour of constraint symbols in sketch edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "NonDrivingConstrDimColor", "NonDrivingConstrDimColor", App::ParamInfo::Hex, 0x0026FFFF)
        .setTitle("Reference constraint colour")
        .setDoc("Colour of reference (non-driving) dimensional constraints in\n"
"sketch edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "ExprBasedConstrDimColor", "ExprBasedConstrDimColor", App::ParamInfo::Hex, 0xFF7F26FF)
        .setTitle("Expression constraint colour")
        .setDoc("Colour of dimensional constraints whose value is an expression in\n"
"sketch edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "DeactivatedConstrDimColor", "DeactivatedConstrDimColor", App::ParamInfo::Hex, 0xCCCCCCFF)
        .setTitle("Deactivated constraint colour")
        .setDoc("Colour of deactivated constraints in sketch edit mode. Applies at\n"
"once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "ExternalColor", "ExternalColor", App::ParamInfo::Hex, 0xCC3399FF)
        .setTitle("External geometry colour")
        .setDoc("Colour of external geometry in sketch edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "ExternalDefiningColor", "ExternalDefiningColor", App::ParamInfo::Hex, 0xCC3399FF)
        .setTitle("Defining external geometry colour")
        .setDoc("Colour of defining external geometry in sketch edit mode. Applies\n"
"at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "InformationColor", "InformationColor", App::ParamInfo::Hex, 0x00FF00FF)
        .setTitle("Information layer colour")
        .setDoc("Colour of the information layer -- B-spline polygons, combs, hints\n"
"-- in sketch edit mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "FrozenColor", "FrozenColor", App::ParamInfo::Hex, 0x7FFFFFFF)
        .setTitle("Frozen external geometry colour")
        .setDoc("Colour of frozen external geometry in sketch edit mode. Applies at\n"
"once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "DetachedColor", "DetachedColor", App::ParamInfo::Hex, 0x1C7F1CFF)
        .setTitle("Detached external geometry colour")
        .setDoc("Colour of detached external geometry in sketch edit mode. Applies\n"
"at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/View", "MissingColor", "MissingColor", App::ParamInfo::Hex, 0x7F00FFFF)
        .setTitle("Missing external geometry colour")
        .setDoc("Colour of external geometry whose source is missing in sketch edit\n"
"mode. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle SketcherParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAutoRecompute() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Recompute the active document after every sketch action. Set from\n"
"the Auto-update box of the solver messages panel; applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getAutoRecompute() {
    return instance()->AutoRecompute;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultAutoRecompute() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAutoRecompute(const bool &v) {
    instance()->handle->SetBool("AutoRecompute",v);
    instance()->AutoRecompute = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAutoRecompute() {
    instance()->handle->RemoveBool("AutoRecompute");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAutoRemoveRedundants() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Remove redundant constraints automatically when they are detected.\n"
"Works only while Auto-update is on. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getAutoRemoveRedundants() {
    return instance()->AutoRemoveRedundants;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultAutoRemoveRedundants() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAutoRemoveRedundants(const bool &v) {
    instance()->handle->SetBool("AutoRemoveRedundants",v);
    instance()->AutoRemoveRedundants = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAutoRemoveRedundants() {
    instance()->handle->RemoveBool("AutoRemoveRedundants");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docContinuousCreationMode() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Keep a geometry tool active after it creates an element, so the\n"
"next one can be drawn at once. Applies to the next element.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getContinuousCreationMode() {
    return instance()->ContinuousCreationMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultContinuousCreationMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setContinuousCreationMode(const bool &v) {
    instance()->handle->SetBool("ContinuousCreationMode",v);
    instance()->ContinuousCreationMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeContinuousCreationMode() {
    instance()->handle->RemoveBool("ContinuousCreationMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docContinuousConstraintMode() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Keep a constraint tool active after it creates a constraint.\n"
"Applies the next time a constraint tool is started.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getContinuousConstraintMode() {
    return instance()->ContinuousConstraintMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultContinuousConstraintMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setContinuousConstraintMode(const bool &v) {
    instance()->handle->SetBool("ContinuousConstraintMode",v);
    instance()->ContinuousConstraintMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeContinuousConstraintMode() {
    instance()->handle->RemoveBool("ContinuousConstraintMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowDialogOnDistanceConstraint() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Ask for the value right after a dimensional constraint is created.\n"
"Applies to the next constraint.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowDialogOnDistanceConstraint() {
    return instance()->ShowDialogOnDistanceConstraint;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowDialogOnDistanceConstraint() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowDialogOnDistanceConstraint(const bool &v) {
    instance()->handle->SetBool("ShowDialogOnDistanceConstraint",v);
    instance()->ShowDialogOnDistanceConstraint = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowDialogOnDistanceConstraint() {
    instance()->handle->RemoveBool("ShowDialogOnDistanceConstraint");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docRadiusDiameterConstraintDisplayBaseAngle() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Angle in degrees at which the label of a new radius or diameter\n"
"constraint is placed. Applies to new constraints.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getRadiusDiameterConstraintDisplayBaseAngle() {
    return instance()->RadiusDiameterConstraintDisplayBaseAngle;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultRadiusDiameterConstraintDisplayBaseAngle() {
    const static double def = 15.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setRadiusDiameterConstraintDisplayBaseAngle(const double &v) {
    instance()->handle->SetFloat("RadiusDiameterConstraintDisplayBaseAngle",v);
    instance()->RadiusDiameterConstraintDisplayBaseAngle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeRadiusDiameterConstraintDisplayBaseAngle() {
    instance()->handle->RemoveFloat("RadiusDiameterConstraintDisplayBaseAngle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docRadiusDiameterConstraintDisplayAngleRandomness() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Random spread in degrees added around the base angle of new radius\n"
"or diameter labels, so labels overlap less.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getRadiusDiameterConstraintDisplayAngleRandomness() {
    return instance()->RadiusDiameterConstraintDisplayAngleRandomness;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultRadiusDiameterConstraintDisplayAngleRandomness() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setRadiusDiameterConstraintDisplayAngleRandomness(const double &v) {
    instance()->handle->SetFloat("RadiusDiameterConstraintDisplayAngleRandomness",v);
    instance()->RadiusDiameterConstraintDisplayAngleRandomness = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeRadiusDiameterConstraintDisplayAngleRandomness() {
    instance()->handle->RemoveFloat("RadiusDiameterConstraintDisplayAngleRandomness");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGeometryHistoryLevel() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"How much geometry history a sketch keeps for stable element names:\n"
"0 none, 1 default, above 1 keeps more. Read when a sketch object\n"
"is created or loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getGeometryHistoryLevel() {
    return instance()->GeometryHistoryLevel;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultGeometryHistoryLevel() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGeometryHistoryLevel(const long &v) {
    instance()->handle->SetInt("GeometryHistoryLevel",v);
    instance()->GeometryHistoryLevel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGeometryHistoryLevel() {
    instance()->handle->RemoveInt("GeometryHistoryLevel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docArcFitTolerance() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Tolerance used to fit arcs for a new sketch; copied into the\n"
"sketch's ArcFitTolerance property when it is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getArcFitTolerance() {
    return instance()->ArcFitTolerance;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultArcFitTolerance() {
    const static double def = 1e-06;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setArcFitTolerance(const double &v) {
    instance()->handle->SetFloat("ArcFitTolerance",v);
    instance()->ArcFitTolerance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeArcFitTolerance() {
    instance()->handle->RemoveFloat("ArcFitTolerance");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalBSplineMaxDegree() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Maximum degree of B-splines made from external geometry in a new\n"
"sketch. Copied into the sketch when it is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getExternalBSplineMaxDegree() {
    return instance()->ExternalBSplineMaxDegree;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultExternalBSplineMaxDegree() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalBSplineMaxDegree(const long &v) {
    instance()->handle->SetInt("ExternalBSplineMaxDegree",v);
    instance()->ExternalBSplineMaxDegree = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalBSplineMaxDegree() {
    instance()->handle->RemoveInt("ExternalBSplineMaxDegree");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalBSplineTolerance() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Tolerance of B-splines made from external geometry in a new\n"
"sketch.0001. Copied into the sketch when it is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getExternalBSplineTolerance() {
    return instance()->ExternalBSplineTolerance;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultExternalBSplineTolerance() {
    const static double def = 0.0001;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalBSplineTolerance(const double &v) {
    instance()->handle->SetFloat("ExternalBSplineTolerance",v);
    instance()->ExternalBSplineTolerance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalBSplineTolerance() {
    instance()->handle->RemoveFloat("ExternalBSplineTolerance");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docMakeInternals() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Generate internal faces from the closed regions of a new sketch.\n"
"Copied into the sketch's MakeInternals property when it is\n"
"created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getMakeInternals() {
    return instance()->MakeInternals;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultMakeInternals() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setMakeInternals(const bool &v) {
    instance()->handle->SetBool("MakeInternals",v);
    instance()->MakeInternals = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeMakeInternals() {
    instance()->handle->RemoveBool("MakeInternals");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docLeaveSketchWithEscape() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Let the Esc key leave sketch edit mode. Takes effect the next time\n"
"a sketch is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getLeaveSketchWithEscape() {
    return instance()->LeaveSketchWithEscape;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultLeaveSketchWithEscape() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setLeaveSketchWithEscape(const bool &v) {
    instance()->handle->SetBool("LeaveSketchWithEscape",v);
    instance()->LeaveSketchWithEscape = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeLeaveSketchWithEscape() {
    instance()->handle->RemoveBool("LeaveSketchWithEscape");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowSolverAdvancedWidget() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show the Advanced solver control section in the sketch task panel.\n"
"Takes effect the next time a sketch is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowSolverAdvancedWidget() {
    return instance()->ShowSolverAdvancedWidget;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowSolverAdvancedWidget() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowSolverAdvancedWidget(const bool &v) {
    instance()->handle->SetBool("ShowSolverAdvancedWidget",v);
    instance()->ShowSolverAdvancedWidget = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowSolverAdvancedWidget() {
    instance()->handle->RemoveBool("ShowSolverAdvancedWidget");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docRecalculateInitialSolutionWhileDragging() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Recalculate the solver's starting point while a point is dragged,\n"
"which improves solving. Takes effect the next time a sketch is\n"
"edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getRecalculateInitialSolutionWhileDragging() {
    return instance()->RecalculateInitialSolutionWhileDragging;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultRecalculateInitialSolutionWhileDragging() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setRecalculateInitialSolutionWhileDragging(const bool &v) {
    instance()->handle->SetBool("RecalculateInitialSolutionWhileDragging",v);
    instance()->RecalculateInitialSolutionWhileDragging = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeRecalculateInitialSolutionWhileDragging() {
    instance()->handle->RemoveBool("RecalculateInitialSolutionWhileDragging");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExtendedConstraintInformation() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show extended information for each entry of the constraint list.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getExtendedConstraintInformation() {
    return instance()->ExtendedConstraintInformation;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultExtendedConstraintInformation() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExtendedConstraintInformation(const bool &v) {
    instance()->handle->SetBool("ExtendedConstraintInformation",v);
    instance()->ExtendedConstraintInformation = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExtendedConstraintInformation() {
    instance()->handle->RemoveBool("ExtendedConstraintInformation");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docHideInternalAlignment() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Hide internal alignment constraints in the constraint list.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getHideInternalAlignment() {
    return instance()->HideInternalAlignment;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultHideInternalAlignment() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setHideInternalAlignment(const bool &v) {
    instance()->handle->SetBool("HideInternalAlignment",v);
    instance()->HideInternalAlignment = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeHideInternalAlignment() {
    instance()->handle->RemoveBool("HideInternalAlignment");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docVisualisationTrackingFilter() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show in the 3D view only the constraints that pass the constraint\n"
"list filter. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getVisualisationTrackingFilter() {
    return instance()->VisualisationTrackingFilter;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultVisualisationTrackingFilter() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setVisualisationTrackingFilter(const bool &v) {
    instance()->handle->SetBool("VisualisationTrackingFilter",v);
    instance()->VisualisationTrackingFilter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeVisualisationTrackingFilter() {
    instance()->handle->RemoveBool("VisualisationTrackingFilter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docHideUnits() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Hide the base length unit in dimension labels and cursor\n"
"coordinates for unit systems that support it. Shown at the next\n"
"redraw.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getHideUnits() {
    return instance()->HideUnits;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultHideUnits() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setHideUnits(const bool &v) {
    instance()->handle->SetBool("HideUnits",v);
    instance()->HideUnits = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeHideUnits() {
    instance()->handle->RemoveBool("HideUnits");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowDimensionalName() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show the name of a named dimensional constraint in its label,\n"
"using the format string. Shown at the next redraw.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowDimensionalName() {
    return instance()->ShowDimensionalName;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowDimensionalName() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowDimensionalName(const bool &v) {
    instance()->handle->SetBool("ShowDimensionalName",v);
    instance()->ShowDimensionalName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowDimensionalName() {
    instance()->handle->RemoveBool("ShowDimensionalName");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDimensionalStringFormat() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Format of a named dimension's label: %N is the constraint name, %V\n"
"its value. Shown at the next redraw.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SketcherParams::getDimensionalStringFormat() {
    return instance()->DimensionalStringFormat;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SketcherParams::defaultDimensionalStringFormat() {
    const static std::string def = "%N = %V";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDimensionalStringFormat(const std::string &v) {
    instance()->handle->SetASCII("DimensionalStringFormat",v);
    instance()->DimensionalStringFormat = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDimensionalStringFormat() {
    instance()->handle->RemoveASCII("DimensionalStringFormat");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowCursorCoords() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show the coordinates next to the cursor while drawing in a sketch.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowCursorCoords() {
    return instance()->ShowCursorCoords;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowCursorCoords() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowCursorCoords(const bool &v) {
    instance()->handle->SetBool("ShowCursorCoords",v);
    instance()->ShowCursorCoords = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowCursorCoords() {
    instance()->handle->RemoveBool("ShowCursorCoords");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docUseSystemDecimals() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show cursor coordinates with the number of decimals of the unit\n"
"settings instead of a short form. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getUseSystemDecimals() {
    return instance()->UseSystemDecimals;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultUseSystemDecimals() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setUseSystemDecimals(const bool &v) {
    instance()->handle->SetBool("UseSystemDecimals",v);
    instance()->UseSystemDecimals = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeUseSystemDecimals() {
    instance()->handle->RemoveBool("UseSystemDecimals");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAllowFaceExternalPick() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Allow picking a face as external geometry. Set in the Edit\n"
"controls of the sketch task panel; applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getAllowFaceExternalPick() {
    return instance()->AllowFaceExternalPick;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultAllowFaceExternalPick() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAllowFaceExternalPick(const bool &v) {
    instance()->subHandles[0]->SetBool("AllowFaceExternalPick",v);
    instance()->AllowFaceExternalPick = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAllowFaceExternalPick() {
    instance()->subHandles[0]->RemoveBool("AllowFaceExternalPick");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docViewBottomOnEdit() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Look at the sketch from below instead of from above when it is\n"
"edited. Set by the view-sketch-from-bottom commands.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getViewBottomOnEdit() {
    return instance()->ViewBottomOnEdit;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultViewBottomOnEdit() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setViewBottomOnEdit(const bool &v) {
    instance()->subHandles[0]->SetBool("ViewBottomOnEdit",v);
    instance()->ViewBottomOnEdit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeViewBottomOnEdit() {
    instance()->subHandles[0]->RemoveBool("ViewBottomOnEdit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAdjustCamera() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Turn the camera to face the sketch plane when a sketch is opened\n"
"for editing. Applies the next time a sketch is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getAdjustCamera() {
    return instance()->AdjustCamera;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultAdjustCamera() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAdjustCamera(const bool &v) {
    instance()->subHandles[0]->SetBool("AdjustCamera",v);
    instance()->AdjustCamera = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAdjustCamera() {
    instance()->subHandles[0]->RemoveBool("AdjustCamera");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docFitSketchOnEdit() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Fit the view to the sketch's geometry when it is opened for\n"
"editing. Applies the next time a sketch is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getFitSketchOnEdit() {
    return instance()->FitSketchOnEdit;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultFitSketchOnEdit() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setFitSketchOnEdit(const bool &v) {
    instance()->subHandles[0]->SetBool("FitSketchOnEdit",v);
    instance()->FitSketchOnEdit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeFitSketchOnEdit() {
    instance()->subHandles[0]->RemoveBool("FitSketchOnEdit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docHideDependent() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Hide the objects that depend on a sketch while it is edited, for\n"
"new sketches.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getHideDependent() {
    return instance()->HideDependent;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultHideDependent() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setHideDependent(const bool &v) {
    instance()->subHandles[0]->SetBool("HideDependent",v);
    instance()->HideDependent = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeHideDependent() {
    instance()->subHandles[0]->RemoveBool("HideDependent");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowLinks() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Keep the objects a sketch links to visible while it is edited, for\n"
"new sketches.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowLinks() {
    return instance()->ShowLinks;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowLinks() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowLinks(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowLinks",v);
    instance()->ShowLinks = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowLinks() {
    instance()->subHandles[0]->RemoveBool("ShowLinks");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowSupport() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Keep the object a sketch is attached to visible while it is\n"
"edited, for new sketches.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowSupport() {
    return instance()->ShowSupport;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowSupport() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowSupport(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowSupport",v);
    instance()->ShowSupport = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowSupport() {
    instance()->subHandles[0]->RemoveBool("ShowSupport");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docRestoreCamera() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Put the camera back where it was when editing of a sketch ends,\n"
"for new sketches.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getRestoreCamera() {
    return instance()->RestoreCamera;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultRestoreCamera() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setRestoreCamera(const bool &v) {
    instance()->subHandles[0]->SetBool("RestoreCamera",v);
    instance()->RestoreCamera = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeRestoreCamera() {
    instance()->subHandles[0]->RemoveBool("RestoreCamera");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docForceOrtho() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Switch the view to an orthographic camera while a sketch is\n"
"edited, and back afterwards, for new sketches. Needs\n"
"RestoreCamera.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getForceOrtho() {
    return instance()->ForceOrtho;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultForceOrtho() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setForceOrtho(const bool &v) {
    instance()->subHandles[0]->SetBool("ForceOrtho",v);
    instance()->ForceOrtho = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeForceOrtho() {
    instance()->subHandles[0]->RemoveBool("ForceOrtho");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSectionView() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Clip everything in front of the sketch plane while a sketch is\n"
"edited, for new sketches.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getSectionView() {
    return instance()->SectionView;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultSectionView() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSectionView(const bool &v) {
    instance()->subHandles[0]->SetBool("SectionView",v);
    instance()->SectionView = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSectionView() {
    instance()->subHandles[0]->RemoveBool("SectionView");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAutoConstraints() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Suggest and apply automatic constraints while drawing, for new\n"
"sketches.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getAutoConstraints() {
    return instance()->AutoConstraints;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultAutoConstraints() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAutoConstraints(const bool &v) {
    instance()->subHandles[0]->SetBool("AutoConstraints",v);
    instance()->AutoConstraints = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAutoConstraints() {
    instance()->subHandles[0]->RemoveBool("AutoConstraints");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAvoidRedundantAutoconstraints() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Do not create automatic constraints that would be redundant, for\n"
"new sketches.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getAvoidRedundantAutoconstraints() {
    return instance()->AvoidRedundantAutoconstraints;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultAvoidRedundantAutoconstraints() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAvoidRedundantAutoconstraints(const bool &v) {
    instance()->subHandles[0]->SetBool("AvoidRedundantAutoconstraints",v);
    instance()->AvoidRedundantAutoconstraints = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAvoidRedundantAutoconstraints() {
    instance()->subHandles[0]->RemoveBool("AvoidRedundantAutoconstraints");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowOriginalColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Draw the sketch in its original colours instead of the constraint-\n"
"status colours while editing. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowOriginalColor() {
    return instance()->ShowOriginalColor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowOriginalColor() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowOriginalColor(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowOriginalColor",v);
    instance()->ShowOriginalColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowOriginalColor() {
    instance()->subHandles[0]->RemoveBool("ShowOriginalColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSketchAutoTransparentPick() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"While picking external geometry, make objects transparent to\n"
"picking so hidden edges can be chosen. Applies at once while the\n"
"tool is active.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getSketchAutoTransparentPick() {
    return instance()->SketchAutoTransparentPick;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultSketchAutoTransparentPick() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSketchAutoTransparentPick(const bool &v) {
    instance()->subHandles[0]->SetBool("SketchAutoTransparentPick",v);
    instance()->SketchAutoTransparentPick = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSketchAutoTransparentPick() {
    instance()->subHandles[0]->RemoveBool("SketchAutoTransparentPick");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docZHeight() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Height step between the drawing layers of a sketch in edit mode\n"
"(lines, constraints, points). Raise it if elements flicker through\n"
"each other. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getZHeight() {
    return instance()->ZHeight;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultZHeight() {
    const static double def = 1e-06;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setZHeight(const double &v) {
    instance()->subHandles[0]->SetFloat("ZHeight",v);
    instance()->ZHeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeZHeight() {
    instance()->subHandles[0]->RemoveFloat("ZHeight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAxisTransparency() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Transparency of the sketch axes in edit mode, in percent. 0 to\n"
"100. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getAxisTransparency() {
    return instance()->AxisTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultAxisTransparency() {
    const static long def = 30;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAxisTransparency(const long &v) {
    instance()->subHandles[0]->SetInt("AxisTransparency",v);
    instance()->AxisTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAxisTransparency() {
    instance()->subHandles[0]->RemoveInt("AxisTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docFaceColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour and opacity of the faces shown inside a sketch's closed\n"
"regions. Applies at once to sketches that use automatic colours.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getFaceColor() {
    return instance()->FaceColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultFaceColor() {
    const static unsigned long def = 0x54ABFF7F;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setFaceColor(const unsigned long &v) {
    instance()->subHandles[0]->SetUnsigned("FaceColor",v);
    instance()->FaceColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeFaceColor() {
    instance()->subHandles[0]->RemoveUnsigned("FaceColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowGrid() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show a grid in new sketches while they are edited. An existing\n"
"sketch keeps its own setting.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowGrid() {
    return instance()->ShowGrid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowGrid() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowGrid(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowGrid",v);
    instance()->ShowGrid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowGrid() {
    instance()->subHandles[0]->RemoveBool("ShowGrid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridSize() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Distance in millimetres between two grid lines of a new sketch;\n"
"with automatic spacing, the spacing it starts from. A sketch that\n"
"exists keeps its own.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getGridSize() {
    return instance()->GridSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultGridSize() {
    const static double def = 10.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridSize(const double &v) {
    instance()->subHandles[1]->SetFloat("GridSize",v);
    instance()->GridSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridSize() {
    instance()->subHandles[1]->RemoveFloat("GridSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridAuto() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Let the grid spacing of new sketches adapt to the zoom level.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getGridAuto() {
    return instance()->GridAuto;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultGridAuto() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridAuto(const bool &v) {
    instance()->subHandles[0]->SetBool("GridAuto",v);
    instance()->GridAuto = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridAuto() {
    instance()->subHandles[0]->RemoveBool("GridAuto");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridSizePixelThreshold() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"With auto spacing, the smallest distance in pixels between two\n"
"grid lines before the grid switches to a coarser spacing. 3 to\n"
"10000. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getGridSizePixelThreshold() {
    return instance()->GridSizePixelThreshold;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultGridSizePixelThreshold() {
    const static long def = 15;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridSizePixelThreshold(const long &v) {
    instance()->subHandles[0]->SetInt("GridSizePixelThreshold",v);
    instance()->GridSizePixelThreshold = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridSizePixelThreshold() {
    instance()->subHandles[0]->RemoveInt("GridSizePixelThreshold");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridNumberSubdivision() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Number of grid cells between two major (division) lines. 1 to\n"
"10000. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getGridNumberSubdivision() {
    return instance()->GridNumberSubdivision;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultGridNumberSubdivision() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridNumberSubdivision(const long &v) {
    instance()->subHandles[0]->SetInt("GridNumberSubdivision",v);
    instance()->GridNumberSubdivision = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridNumberSubdivision() {
    instance()->subHandles[0]->RemoveInt("GridNumberSubdivision");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridLinePattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of the minor grid lines, as a 16 bit stipple mask.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getGridLinePattern() {
    return instance()->GridLinePattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultGridLinePattern() {
    const static long def = 65535;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridLinePattern(const long &v) {
    instance()->subHandles[0]->SetInt("GridLinePattern",v);
    instance()->GridLinePattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridLinePattern() {
    instance()->subHandles[0]->RemoveInt("GridLinePattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridDivLinePattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of the major grid lines, as a 16 bit stipple mask.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getGridDivLinePattern() {
    return instance()->GridDivLinePattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultGridDivLinePattern() {
    const static long def = 65535;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridDivLinePattern(const long &v) {
    instance()->subHandles[0]->SetInt("GridDivLinePattern",v);
    instance()->GridDivLinePattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridDivLinePattern() {
    instance()->subHandles[0]->RemoveInt("GridDivLinePattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridLineWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Width of the minor grid lines in pixels. 1 to 99. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getGridLineWidth() {
    return instance()->GridLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultGridLineWidth() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridLineWidth(const long &v) {
    instance()->subHandles[0]->SetInt("GridLineWidth",v);
    instance()->GridLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridLineWidth() {
    instance()->subHandles[0]->RemoveInt("GridLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridDivLineWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Width of the major grid lines in pixels. 1 to 99. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getGridDivLineWidth() {
    return instance()->GridDivLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultGridDivLineWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridDivLineWidth(const long &v) {
    instance()->subHandles[0]->SetInt("GridDivLineWidth",v);
    instance()->GridDivLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridDivLineWidth() {
    instance()->subHandles[0]->RemoveInt("GridDivLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridLineColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of the minor grid lines. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getGridLineColor() {
    return instance()->GridLineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultGridLineColor() {
    const static unsigned long def = 0xB2B2B2FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridLineColor(const unsigned long &v) {
    instance()->subHandles[0]->SetUnsigned("GridLineColor",v);
    instance()->GridLineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridLineColor() {
    instance()->subHandles[0]->RemoveUnsigned("GridLineColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridDivLineColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of the major grid lines. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getGridDivLineColor() {
    return instance()->GridDivLineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultGridDivLineColor() {
    const static unsigned long def = 0xB2B2B2FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridDivLineColor(const unsigned long &v) {
    instance()->subHandles[0]->SetUnsigned("GridDivLineColor",v);
    instance()->GridDivLineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridDivLineColor() {
    instance()->subHandles[0]->RemoveUnsigned("GridDivLineColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGridTransparency() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Transparency of the grid lines in percent, 0 is opaque. 0 to 100.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getGridTransparency() {
    return instance()->GridTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultGridTransparency() {
    const static long def = 60;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGridTransparency(const long &v) {
    instance()->subHandles[0]->SetInt("GridTransparency",v);
    instance()->GridTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGridTransparency() {
    instance()->subHandles[0]->RemoveInt("GridTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docTopRenderGeometryId() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Which kind of geometry is drawn on top in sketch edit mode: 1\n"
"normal, 2 construction, 3 external. Set by dragging in the\n"
"Rendering order list; applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getTopRenderGeometryId() {
    return instance()->TopRenderGeometryId;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultTopRenderGeometryId() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setTopRenderGeometryId(const long &v) {
    instance()->subHandles[0]->SetInt("TopRenderGeometryId",v);
    instance()->TopRenderGeometryId = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeTopRenderGeometryId() {
    instance()->subHandles[0]->RemoveInt("TopRenderGeometryId");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docMidRenderGeometryId() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Which kind of geometry is drawn in the middle: 1 normal, 2\n"
"construction, 3 external.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getMidRenderGeometryId() {
    return instance()->MidRenderGeometryId;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultMidRenderGeometryId() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setMidRenderGeometryId(const long &v) {
    instance()->subHandles[0]->SetInt("MidRenderGeometryId",v);
    instance()->MidRenderGeometryId = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeMidRenderGeometryId() {
    instance()->subHandles[0]->RemoveInt("MidRenderGeometryId");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docLowRenderGeometryId() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Which kind of geometry is drawn at the bottom: 1 normal, 2\n"
"construction, 3 external.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getLowRenderGeometryId() {
    return instance()->LowRenderGeometryId;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultLowRenderGeometryId() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setLowRenderGeometryId(const long &v) {
    instance()->subHandles[0]->SetInt("LowRenderGeometryId",v);
    instance()->LowRenderGeometryId = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeLowRenderGeometryId() {
    instance()->subHandles[0]->RemoveInt("LowRenderGeometryId");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docBSplineExternalVisible() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show the B-spline information overlays for external B-splines too.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getBSplineExternalVisible() {
    return instance()->BSplineExternalVisible;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultBSplineExternalVisible() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setBSplineExternalVisible(const bool &v) {
    instance()->subHandles[0]->SetBool("BSplineExternalVisible",v);
    instance()->BSplineExternalVisible = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeBSplineExternalVisible() {
    instance()->subHandles[0]->RemoveBool("BSplineExternalVisible");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docEditDatumInPlace() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Edit a dimension's value in a field at its label instead of in a\n"
"dialog. Applies to the next edit.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getEditDatumInPlace() {
    return instance()->EditDatumInPlace;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultEditDatumInPlace() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setEditDatumInPlace(const bool &v) {
    instance()->subHandles[0]->SetBool("EditDatumInPlace",v);
    instance()->EditDatumInPlace = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeEditDatumInPlace() {
    instance()->subHandles[0]->RemoveBool("EditDatumInPlace");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDatumEscapeTakesBack() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Esc cancels the dimension value being typed at a label instead of\n"
"leaving the field with the value entered.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getDatumEscapeTakesBack() {
    return instance()->DatumEscapeTakesBack;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultDatumEscapeTakesBack() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDatumEscapeTakesBack(const bool &v) {
    instance()->subHandles[0]->SetBool("DatumEscapeTakesBack",v);
    instance()->DatumEscapeTakesBack = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDatumEscapeTakesBack() {
    instance()->subHandles[0]->RemoveBool("DatumEscapeTakesBack");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowDirectionalAutoConstraintHints() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show helper lines for direction based automatic constraints (line\n"
"extension, parallel, perpendicular) while drawing. Applies at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowDirectionalAutoConstraintHints() {
    return instance()->ShowDirectionalAutoConstraintHints;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowDirectionalAutoConstraintHints() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowDirectionalAutoConstraintHints(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowDirectionalAutoConstraintHints",v);
    instance()->ShowDirectionalAutoConstraintHints = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowDirectionalAutoConstraintHints() {
    instance()->subHandles[0]->RemoveBool("ShowDirectionalAutoConstraintHints");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDragAutoConstraintDelay() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Time in milliseconds the pointer must rest while dragging before\n"
"an automatic constraint is offered. 0 to 5000.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getDragAutoConstraintDelay() {
    return instance()->DragAutoConstraintDelay;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultDragAutoConstraintDelay() {
    const static long def = 400;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDragAutoConstraintDelay(const long &v) {
    instance()->subHandles[0]->SetInt("DragAutoConstraintDelay",v);
    instance()->DragAutoConstraintDelay = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDragAutoConstraintDelay() {
    instance()->subHandles[0]->RemoveInt("DragAutoConstraintDelay");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docNotifyConstraintSubstitutions() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show a message when the Sketcher replaces constraints\n"
"automatically, for example a coincident and a tangent by an\n"
"endpoint tangency.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getNotifyConstraintSubstitutions() {
    return instance()->NotifyConstraintSubstitutions;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultNotifyConstraintSubstitutions() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setNotifyConstraintSubstitutions(const bool &v) {
    instance()->subHandles[0]->SetBool("NotifyConstraintSubstitutions",v);
    instance()->NotifyConstraintSubstitutions = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeNotifyConstraintSubstitutions() {
    instance()->subHandles[0]->RemoveBool("NotifyConstraintSubstitutions");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docEdgeWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line width of normal geometry in edit mode, in pixels. 1 to 99.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getEdgeWidth() {
    return instance()->EdgeWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultEdgeWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setEdgeWidth(const long &v) {
    instance()->subHandles[2]->SetInt("EdgeWidth",v);
    instance()->EdgeWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeEdgeWidth() {
    instance()->subHandles[2]->RemoveInt("EdgeWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docEdgePattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of normal geometry in edit mode, a 16 bit stipple\n"
"mask.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getEdgePattern() {
    return instance()->EdgePattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultEdgePattern() {
    const static long def = 65535;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setEdgePattern(const long &v) {
    instance()->subHandles[2]->SetInt("EdgePattern",v);
    instance()->EdgePattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeEdgePattern() {
    instance()->subHandles[2]->RemoveInt("EdgePattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docConstructionWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line width of construction geometry in edit mode, in pixels.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getConstructionWidth() {
    return instance()->ConstructionWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultConstructionWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setConstructionWidth(const long &v) {
    instance()->subHandles[2]->SetInt("ConstructionWidth",v);
    instance()->ConstructionWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeConstructionWidth() {
    instance()->subHandles[2]->RemoveInt("ConstructionWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docConstructionPattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of construction geometry in edit mode.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getConstructionPattern() {
    return instance()->ConstructionPattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultConstructionPattern() {
    const static long def = 64764;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setConstructionPattern(const long &v) {
    instance()->subHandles[2]->SetInt("ConstructionPattern",v);
    instance()->ConstructionPattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeConstructionPattern() {
    instance()->subHandles[2]->RemoveInt("ConstructionPattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docInternalWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line width of internal alignment geometry in edit mode, in pixels.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getInternalWidth() {
    return instance()->InternalWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultInternalWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setInternalWidth(const long &v) {
    instance()->subHandles[2]->SetInt("InternalWidth",v);
    instance()->InternalWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeInternalWidth() {
    instance()->subHandles[2]->RemoveInt("InternalWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docInternalPattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of internal alignment geometry in edit mode.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getInternalPattern() {
    return instance()->InternalPattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultInternalPattern() {
    const static long def = 64764;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setInternalPattern(const long &v) {
    instance()->subHandles[2]->SetInt("InternalPattern",v);
    instance()->InternalPattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeInternalPattern() {
    instance()->subHandles[2]->RemoveInt("InternalPattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line width of external geometry in edit mode, in pixels.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getExternalWidth() {
    return instance()->ExternalWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultExternalWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalWidth(const long &v) {
    instance()->subHandles[2]->SetInt("ExternalWidth",v);
    instance()->ExternalWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalWidth() {
    instance()->subHandles[2]->RemoveInt("ExternalWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalPattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of external geometry in edit mode.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getExternalPattern() {
    return instance()->ExternalPattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultExternalPattern() {
    const static long def = 64764;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalPattern(const long &v) {
    instance()->subHandles[2]->SetInt("ExternalPattern",v);
    instance()->ExternalPattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalPattern() {
    instance()->subHandles[2]->RemoveInt("ExternalPattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalDefiningWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line width of defining external geometry in edit mode, in pixels.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getExternalDefiningWidth() {
    return instance()->ExternalDefiningWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultExternalDefiningWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalDefiningWidth(const long &v) {
    instance()->subHandles[2]->SetInt("ExternalDefiningWidth",v);
    instance()->ExternalDefiningWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalDefiningWidth() {
    instance()->subHandles[2]->RemoveInt("ExternalDefiningWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalDefiningPattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of defining external geometry in edit mode.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getExternalDefiningPattern() {
    return instance()->ExternalDefiningPattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultExternalDefiningPattern() {
    const static long def = 65535;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalDefiningPattern(const long &v) {
    instance()->subHandles[2]->SetInt("ExternalDefiningPattern",v);
    instance()->ExternalDefiningPattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalDefiningPattern() {
    instance()->subHandles[2]->RemoveInt("ExternalDefiningPattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docInformationWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line width of the information layer (B-spline polygons, combs,\n"
"hints), in pixels.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getInformationWidth() {
    return instance()->InformationWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultInformationWidth() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setInformationWidth(const long &v) {
    instance()->subHandles[2]->SetInt("InformationWidth",v);
    instance()->InformationWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeInformationWidth() {
    instance()->subHandles[2]->RemoveInt("InformationWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docInformationPattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of the information layer.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getInformationPattern() {
    return instance()->InformationPattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultInformationPattern() {
    const static long def = 64764;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setInformationPattern(const long &v) {
    instance()->subHandles[2]->SetInt("InformationPattern",v);
    instance()->InformationPattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeInformationPattern() {
    instance()->subHandles[2]->RemoveInt("InformationPattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDimensionalConstraintLineWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line width of dimensional constraints, in pixels. 1 to 4.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getDimensionalConstraintLineWidth() {
    return instance()->DimensionalConstraintLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultDimensionalConstraintLineWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDimensionalConstraintLineWidth(const long &v) {
    instance()->subHandles[2]->SetInt("DimensionalConstraintLineWidth",v);
    instance()->DimensionalConstraintLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDimensionalConstraintLineWidth() {
    instance()->subHandles[2]->RemoveInt("DimensionalConstraintLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDimensionalConstraintLinePattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of dimensional constraints.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getDimensionalConstraintLinePattern() {
    return instance()->DimensionalConstraintLinePattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultDimensionalConstraintLinePattern() {
    const static long def = 65535;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDimensionalConstraintLinePattern(const long &v) {
    instance()->subHandles[2]->SetInt("DimensionalConstraintLinePattern",v);
    instance()->DimensionalConstraintLinePattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDimensionalConstraintLinePattern() {
    instance()->subHandles[2]->RemoveInt("DimensionalConstraintLinePattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAxisLineWidth() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line width of the sketch axes in edit mode, in pixels.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getAxisLineWidth() {
    return instance()->AxisLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultAxisLineWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAxisLineWidth(const long &v) {
    instance()->subHandles[2]->SetInt("AxisLineWidth",v);
    instance()->AxisLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAxisLineWidth() {
    instance()->subHandles[2]->RemoveInt("AxisLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAxisLinePattern() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Line pattern of the sketch axes in edit mode.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getAxisLinePattern() {
    return instance()->AxisLinePattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultAxisLinePattern() {
    const static long def = 65535;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAxisLinePattern(const long &v) {
    instance()->subHandles[2]->SetInt("AxisLinePattern",v);
    instance()->AxisLinePattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAxisLinePattern() {
    instance()->subHandles[2]->RemoveInt("AxisLinePattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSingleDimensioningTool() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Put the single Dimension tool on the Sketcher tool bar. Together\n"
"with the separated tools option this gives the modes Single tool,\n"
"Separated tools, Both. Tool bars are rebuilt when the page is\n"
"saved.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getSingleDimensioningTool() {
    return instance()->SingleDimensioningTool;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultSingleDimensioningTool() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSingleDimensioningTool(const bool &v) {
    instance()->subHandles[3]->SetBool("SingleDimensioningTool",v);
    instance()->SingleDimensioningTool = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSingleDimensioningTool() {
    instance()->subHandles[3]->RemoveBool("SingleDimensioningTool");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSeparatedDimensioningTools() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Put the separate dimension tools (horizontal, vertical, distance,\n"
"radius/diameter, angle, lock) on the Sketcher tool bar.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getSeparatedDimensioningTools() {
    return instance()->SeparatedDimensioningTools;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultSeparatedDimensioningTools() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSeparatedDimensioningTools(const bool &v) {
    instance()->subHandles[3]->SetBool("SeparatedDimensioningTools",v);
    instance()->SeparatedDimensioningTools = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSeparatedDimensioningTools() {
    instance()->subHandles[3]->RemoveBool("SeparatedDimensioningTools");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDimensioningDiameter() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Let the Dimension tool create diameters. With radius also on the\n"
"tool picks: diameter for circles, radius for arcs.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getDimensioningDiameter() {
    return instance()->DimensioningDiameter;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultDimensioningDiameter() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDimensioningDiameter(const bool &v) {
    instance()->subHandles[3]->SetBool("DimensioningDiameter",v);
    instance()->DimensioningDiameter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDimensioningDiameter() {
    instance()->subHandles[3]->RemoveBool("DimensioningDiameter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDimensioningRadius() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Let the Dimension tool create radii. With diameter also on the\n"
"tool picks: diameter for circles, radius for arcs.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getDimensioningRadius() {
    return instance()->DimensioningRadius;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultDimensioningRadius() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDimensioningRadius(const bool &v) {
    instance()->subHandles[3]->SetBool("DimensioningRadius",v);
    instance()->DimensioningRadius = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDimensioningRadius() {
    instance()->subHandles[3]->RemoveBool("DimensioningRadius");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAutoScaleMode() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Scale the whole sketch to the value of its first dimension: 0\n"
"always, 1 never, 2 only when no feature that fixes the scale is\n"
"visible. Applies to the next dimension.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getAutoScaleMode() {
    return instance()->AutoScaleMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultAutoScaleMode() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAutoScaleMode(const long &v) {
    instance()->subHandles[3]->SetInt("AutoScaleMode",v);
    instance()->AutoScaleMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAutoScaleMode() {
    instance()->subHandles[3]->RemoveInt("AutoScaleMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docOnViewParameterVisibility() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Which on-view parameters a drawing tool shows at the cursor: 0\n"
"none, 1 dimensions only, 2 position and dimensions. Applies to the\n"
"next tool started.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getOnViewParameterVisibility() {
    return instance()->OnViewParameterVisibility;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultOnViewParameterVisibility() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setOnViewParameterVisibility(const long &v) {
    instance()->subHandles[4]->SetInt("OnViewParameterVisibility",v);
    instance()->OnViewParameterVisibility = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeOnViewParameterVisibility() {
    instance()->subHandles[4]->RemoveInt("OnViewParameterVisibility");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docUnifiedCoincident() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Use one tool for coincident and point-on-object constraints. Tool\n"
"bars and menus follow when the page is saved; the shortcuts follow\n"
"after a restart.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getUnifiedCoincident() {
    return instance()->UnifiedCoincident;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultUnifiedCoincident() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setUnifiedCoincident(const bool &v) {
    instance()->subHandles[5]->SetBool("UnifiedCoincident",v);
    instance()->UnifiedCoincident = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeUnifiedCoincident() {
    instance()->subHandles[5]->RemoveBool("UnifiedCoincident");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAutoHorVer() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Use one tool that chooses between a horizontal and a vertical\n"
"constraint. Tool bars follow when the page is saved.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getAutoHorVer() {
    return instance()->AutoHorVer;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultAutoHorVer() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAutoHorVer(const bool &v) {
    instance()->subHandles[5]->SetBool("AutoHorVer",v);
    instance()->AutoHorVer = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAutoHorVer() {
    instance()->subHandles[5]->RemoveBool("AutoHorVer");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docUnifiedLineCommands() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Group the polyline and line commands under one tool bar button.\n"
"Tool bars follow when the page is saved.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getUnifiedLineCommands() {
    return instance()->UnifiedLineCommands;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultUnifiedLineCommands() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setUnifiedLineCommands(const bool &v) {
    instance()->subHandles[6]->SetBool("UnifiedLineCommands",v);
    instance()->UnifiedLineCommands = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeUnifiedLineCommands() {
    instance()->subHandles[6]->RemoveBool("UnifiedLineCommands");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSnap() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Master switch for snapping in sketch edit mode. Toggled by the\n"
"Snap tool bar button; applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getSnap() {
    return instance()->Snap;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultSnap() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSnap(const bool &v) {
    instance()->subHandles[7]->SetBool("Snap",v);
    instance()->Snap = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSnap() {
    instance()->subHandles[7]->RemoveBool("Snap");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSnapToObjects() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Snap new points to the preselected object, and to the middle of\n"
"lines and arcs. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getSnapToObjects() {
    return instance()->SnapToObjects;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultSnapToObjects() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSnapToObjects(const bool &v) {
    instance()->subHandles[7]->SetBool("SnapToObjects",v);
    instance()->SnapToObjects = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSnapToObjects() {
    instance()->subHandles[7]->RemoveBool("SnapToObjects");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSnapToGrid() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Snap new points to the nearest grid line when closer than a fifth\n"
"of the grid spacing. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getSnapToGrid() {
    return instance()->SnapToGrid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultSnapToGrid() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSnapToGrid(const bool &v) {
    instance()->subHandles[7]->SetBool("SnapToGrid",v);
    instance()->SnapToGrid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSnapToGrid() {
    instance()->subHandles[7]->RemoveBool("SnapToGrid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSnapAngle() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Angular step in degrees for tools that snap at an angle while Ctrl\n"
"is held, measured from the sketch's positive X axis. Applies at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getSnapAngle() {
    return instance()->SnapAngle;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultSnapAngle() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSnapAngle(const double &v) {
    instance()->subHandles[7]->SetFloat("SnapAngle",v);
    instance()->SnapAngle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSnapAngle() {
    instance()->subHandles[7]->RemoveFloat("SnapAngle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docElementIconSize() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Size in pixels of the icons in the element list of the sketch task\n"
"panel. 16 to 128. Takes effect the next time a sketch is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getElementIconSize() {
    return instance()->ElementIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultElementIconSize() {
    const static long def = 32;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setElementIconSize(const long &v) {
    instance()->subHandles[8]->SetInt("ElementIconSize",v);
    instance()->ElementIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeElementIconSize() {
    instance()->subHandles[8]->RemoveInt("ElementIconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docEditSketcherFontName() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Font family of the dimension labels in sketch edit mode. Empty\n"
"means the labels' own font. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SketcherParams::getEditSketcherFontName() {
    return instance()->EditSketcherFontName;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SketcherParams::defaultEditSketcherFontName() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setEditSketcherFontName(const std::string &v) {
    instance()->subHandles[9]->SetASCII("EditSketcherFontName",v);
    instance()->EditSketcherFontName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeEditSketcherFontName() {
    instance()->subHandles[9]->RemoveASCII("EditSketcherFontName");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docConstraintIconLabelsPerLine() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"How many constraint numbers fit on one line of the label beside a\n"
"combined constraint icon, 1 to 100.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getConstraintIconLabelsPerLine() {
    return instance()->ConstraintIconLabelsPerLine;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultConstraintIconLabelsPerLine() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setConstraintIconLabelsPerLine(const long &v) {
    instance()->subHandles[9]->SetInt("ConstraintIconLabelsPerLine",v);
    instance()->ConstraintIconLabelsPerLine = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeConstraintIconLabelsPerLine() {
    instance()->subHandles[9]->RemoveInt("ConstraintIconLabelsPerLine");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docConstraintIconLabelLines() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Largest number of lines of the label beside a combined constraint\n"
"icon, 1 to 100.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getConstraintIconLabelLines() {
    return instance()->ConstraintIconLabelLines;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultConstraintIconLabelLines() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setConstraintIconLabelLines(const long &v) {
    instance()->subHandles[9]->SetInt("ConstraintIconLabelLines",v);
    instance()->ConstraintIconLabelLines = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeConstraintIconLabelLines() {
    instance()->subHandles[9]->RemoveInt("ConstraintIconLabelLines");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docViewScalingFactor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Scale factor of the fixed pixel sizes of sketch edit mode (points,\n"
"constraint lines), 0.5 to 5. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getViewScalingFactor() {
    return instance()->ViewScalingFactor;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultViewScalingFactor() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setViewScalingFactor(const double &v) {
    instance()->subHandles[9]->SetFloat("ViewScalingFactor",v);
    instance()->ViewScalingFactor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeViewScalingFactor() {
    instance()->subHandles[9]->RemoveFloat("ViewScalingFactor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSegmentsPerGeometry() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Number of straight segments a curve is drawn with in sketch edit\n"
"mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getSegmentsPerGeometry() {
    return instance()->SegmentsPerGeometry;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultSegmentsPerGeometry() {
    const static long def = 50;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSegmentsPerGeometry(const long &v) {
    instance()->subHandles[9]->SetInt("SegmentsPerGeometry",v);
    instance()->SegmentsPerGeometry = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSegmentsPerGeometry() {
    instance()->subHandles[9]->RemoveInt("SegmentsPerGeometry");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docCursorTextColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of the coordinate text shown at the cursor in sketch edit\n"
"mode. Takes effect the next time a sketch is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getCursorTextColor() {
    return instance()->CursorTextColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultCursorTextColor() {
    const static unsigned long def = 0x0000FFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setCursorTextColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("CursorTextColor",v);
    instance()->CursorTextColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeCursorTextColor() {
    instance()->subHandles[9]->RemoveUnsigned("CursorTextColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSketchEdgeColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of a sketch's edges outside edit mode, for sketches that\n"
"use automatic colours. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getSketchEdgeColor() {
    return instance()->SketchEdgeColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultSketchEdgeColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSketchEdgeColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("SketchEdgeColor",v);
    instance()->SketchEdgeColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSketchEdgeColor() {
    instance()->subHandles[9]->RemoveUnsigned("SketchEdgeColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docSketchVertexColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of a sketch's vertices outside edit mode, for sketches that\n"
"use automatic colours. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getSketchVertexColor() {
    return instance()->SketchVertexColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultSketchVertexColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setSketchVertexColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("SketchVertexColor",v);
    instance()->SketchVertexColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeSketchVertexColor() {
    instance()->subHandles[9]->RemoveUnsigned("SketchVertexColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docEditedEdgeColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of normal geometry in sketch edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getEditedEdgeColor() {
    return instance()->EditedEdgeColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultEditedEdgeColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setEditedEdgeColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("EditedEdgeColor",v);
    instance()->EditedEdgeColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeEditedEdgeColor() {
    instance()->subHandles[9]->RemoveUnsigned("EditedEdgeColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docConstructionColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of construction geometry in sketch edit mode. Applies at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getConstructionColor() {
    return instance()->ConstructionColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultConstructionColor() {
    const static unsigned long def = 0x0000DCFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setConstructionColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("ConstructionColor",v);
    instance()->ConstructionColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeConstructionColor() {
    instance()->subHandles[9]->RemoveUnsigned("ConstructionColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docInternalAlignedGeoColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of internal alignment geometry in sketch edit mode. Applies\n"
"at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getInternalAlignedGeoColor() {
    return instance()->InternalAlignedGeoColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultInternalAlignedGeoColor() {
    const static unsigned long def = 0xB2B27FFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setInternalAlignedGeoColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("InternalAlignedGeoColor",v);
    instance()->InternalAlignedGeoColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeInternalAlignedGeoColor() {
    instance()->subHandles[9]->RemoveUnsigned("InternalAlignedGeoColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docFullyConstraintElementColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of a fully constrained element of normal geometry in sketch\n"
"edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getFullyConstraintElementColor() {
    return instance()->FullyConstraintElementColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultFullyConstraintElementColor() {
    const static unsigned long def = 0x80D0A0FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setFullyConstraintElementColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("FullyConstraintElementColor",v);
    instance()->FullyConstraintElementColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeFullyConstraintElementColor() {
    instance()->subHandles[9]->RemoveUnsigned("FullyConstraintElementColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docFullyConstraintConstructionElementColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of a fully constrained element of construction geometry in\n"
"sketch edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getFullyConstraintConstructionElementColor() {
    return instance()->FullyConstraintConstructionElementColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultFullyConstraintConstructionElementColor() {
    const static unsigned long def = 0x8FA9FDFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setFullyConstraintConstructionElementColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("FullyConstraintConstructionElementColor",v);
    instance()->FullyConstraintConstructionElementColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeFullyConstraintConstructionElementColor() {
    instance()->subHandles[9]->RemoveUnsigned("FullyConstraintConstructionElementColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docFullyConstraintInternalAlignmentColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of a fully constrained element of internal alignment\n"
"geometry in sketch edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getFullyConstraintInternalAlignmentColor() {
    return instance()->FullyConstraintInternalAlignmentColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultFullyConstraintInternalAlignmentColor() {
    const static unsigned long def = 0xDEDEC8FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setFullyConstraintInternalAlignmentColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("FullyConstraintInternalAlignmentColor",v);
    instance()->FullyConstraintInternalAlignmentColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeFullyConstraintInternalAlignmentColor() {
    instance()->subHandles[9]->RemoveUnsigned("FullyConstraintInternalAlignmentColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docInvalidSketchColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of the geometry of a sketch with conflicting or redundant\n"
"constraints in sketch edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getInvalidSketchColor() {
    return instance()->InvalidSketchColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultInvalidSketchColor() {
    const static unsigned long def = 0xFF6D00FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setInvalidSketchColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("InvalidSketchColor",v);
    instance()->InvalidSketchColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeInvalidSketchColor() {
    instance()->subHandles[9]->RemoveUnsigned("InvalidSketchColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docFullyConstrainedColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of the geometry of a fully constrained sketch in sketch\n"
"edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getFullyConstrainedColor() {
    return instance()->FullyConstrainedColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultFullyConstrainedColor() {
    const static unsigned long def = 0x00FF00FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setFullyConstrainedColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("FullyConstrainedColor",v);
    instance()->FullyConstrainedColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeFullyConstrainedColor() {
    instance()->subHandles[9]->RemoveUnsigned("FullyConstrainedColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docConstrainedDimColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of dimensional constraints in sketch edit mode. Applies at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getConstrainedDimColor() {
    return instance()->ConstrainedDimColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultConstrainedDimColor() {
    const static unsigned long def = 0xFF2600FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setConstrainedDimColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("ConstrainedDimColor",v);
    instance()->ConstrainedDimColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeConstrainedDimColor() {
    instance()->subHandles[9]->RemoveUnsigned("ConstrainedDimColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docConstrainedIcoColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of constraint symbols in sketch edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getConstrainedIcoColor() {
    return instance()->ConstrainedIcoColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultConstrainedIcoColor() {
    const static unsigned long def = 0xFF2600FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setConstrainedIcoColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("ConstrainedIcoColor",v);
    instance()->ConstrainedIcoColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeConstrainedIcoColor() {
    instance()->subHandles[9]->RemoveUnsigned("ConstrainedIcoColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docNonDrivingConstrDimColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of reference (non-driving) dimensional constraints in\n"
"sketch edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getNonDrivingConstrDimColor() {
    return instance()->NonDrivingConstrDimColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultNonDrivingConstrDimColor() {
    const static unsigned long def = 0x0026FFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setNonDrivingConstrDimColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("NonDrivingConstrDimColor",v);
    instance()->NonDrivingConstrDimColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeNonDrivingConstrDimColor() {
    instance()->subHandles[9]->RemoveUnsigned("NonDrivingConstrDimColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExprBasedConstrDimColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of dimensional constraints whose value is an expression in\n"
"sketch edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getExprBasedConstrDimColor() {
    return instance()->ExprBasedConstrDimColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultExprBasedConstrDimColor() {
    const static unsigned long def = 0xFF7F26FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExprBasedConstrDimColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("ExprBasedConstrDimColor",v);
    instance()->ExprBasedConstrDimColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExprBasedConstrDimColor() {
    instance()->subHandles[9]->RemoveUnsigned("ExprBasedConstrDimColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDeactivatedConstrDimColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of deactivated constraints in sketch edit mode. Applies at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getDeactivatedConstrDimColor() {
    return instance()->DeactivatedConstrDimColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultDeactivatedConstrDimColor() {
    const static unsigned long def = 0xCCCCCCFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDeactivatedConstrDimColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("DeactivatedConstrDimColor",v);
    instance()->DeactivatedConstrDimColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDeactivatedConstrDimColor() {
    instance()->subHandles[9]->RemoveUnsigned("DeactivatedConstrDimColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of external geometry in sketch edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getExternalColor() {
    return instance()->ExternalColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultExternalColor() {
    const static unsigned long def = 0xCC3399FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("ExternalColor",v);
    instance()->ExternalColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalColor() {
    instance()->subHandles[9]->RemoveUnsigned("ExternalColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalDefiningColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of defining external geometry in sketch edit mode. Applies\n"
"at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getExternalDefiningColor() {
    return instance()->ExternalDefiningColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultExternalDefiningColor() {
    const static unsigned long def = 0xCC3399FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalDefiningColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("ExternalDefiningColor",v);
    instance()->ExternalDefiningColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalDefiningColor() {
    instance()->subHandles[9]->RemoveUnsigned("ExternalDefiningColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docInformationColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of the information layer -- B-spline polygons, combs, hints\n"
"-- in sketch edit mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getInformationColor() {
    return instance()->InformationColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultInformationColor() {
    const static unsigned long def = 0x00FF00FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setInformationColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("InformationColor",v);
    instance()->InformationColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeInformationColor() {
    instance()->subHandles[9]->RemoveUnsigned("InformationColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docFrozenColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of frozen external geometry in sketch edit mode. Applies at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getFrozenColor() {
    return instance()->FrozenColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultFrozenColor() {
    const static unsigned long def = 0x7FFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setFrozenColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("FrozenColor",v);
    instance()->FrozenColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeFrozenColor() {
    instance()->subHandles[9]->RemoveUnsigned("FrozenColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDetachedColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of detached external geometry in sketch edit mode. Applies\n"
"at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getDetachedColor() {
    return instance()->DetachedColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultDetachedColor() {
    const static unsigned long def = 0x1C7F1CFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDetachedColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("DetachedColor",v);
    instance()->DetachedColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDetachedColor() {
    instance()->subHandles[9]->RemoveUnsigned("DetachedColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docMissingColor() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Colour of external geometry whose source is missing in sketch edit\n"
"mode. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & SketcherParams::getMissingColor() {
    return instance()->MissingColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & SketcherParams::defaultMissingColor() {
    const static unsigned long def = 0x7F00FFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setMissingColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("MissingColor",v);
    instance()->MissingColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeMissingColor() {
    instance()->subHandles[9]->RemoveUnsigned("MissingColor");
}
//[[[end]]]
