/****************************************************************************
 *   Copyright (c) 2018 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
#ifndef _PreComp_
# include <QApplication>
# include <QStatusBar>
# include <QToolBar>
# include <QMenuBar>
#endif
#include "Application.h"
#include "PreferencePages/DlgSettingsDrawStyles.h"
#include "Document.h"
#include "ViewProvider.h"
#include "TreeParams.h"
#include "Selection.h"
#include "OverlayWidgets.h"
#include "Widgets.h"
#include "MainWindow.h"
#include "ViewArea.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "RenderParams.h"
#include "QSint/actionpanel/taskheader_p.h"

/*[[[cog
import ViewParams
ViewParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "ViewParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class ViewParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(ViewParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    bool UseViewArea;
    bool UseNewSelection;
    bool UseSelectionRoot;
    bool EnableSelection;
    bool EnablePreselection;
    long RenderCache;
    bool UnifiedCanvas;
    bool RandomColor;
    unsigned long BoundingBoxColor;
    unsigned long AnnotationTextColor;
    unsigned long CursorCrosshairColor;
    unsigned long HighlightColor;
    unsigned long SelectionColor;
    long MarkerSize;
    unsigned long DefaultLinkColor;
    unsigned long DefaultShapeLineColor;
    unsigned long DefaultShapeVertexColor;
    unsigned long DefaultShapeColor;
    long DefaultShapeTransparency;
    long DefaultShapeLineWidth;
    long DefaultShapePointSize;
    bool CoinCycleCheck;
    bool EnablePropertyViewForInactiveDocument;
    bool ShowSelectionBoundingBox;
    long ShowSelectionBoundingBoxThreshold;
    bool UpdateSelectionVisual;
    bool LinkChildrenDirect;
    bool ShowSelectionOnTop;
    bool ShowPreSelectedFaceOnTop;
    bool ShowPreSelectedFaceOutline;
    bool ShowSelectedFaceOutline;
    double OutlineThicken;
    bool NoSelFaceHighlightWithOutline;
    bool NoPreSelFaceHighlightWithOutline;
    bool AutoTransparentPick;
    bool SelectElementOnTop;
    double TransparencyOnTop;
    long HiddenLineSync;
    bool HiddenLineSelectionOnTop;
    bool PartialHighlightOnFullSelect;
    double SelectionLineThicken;
    double SelectionLineMaxWidth;
    double SelectionPointScale;
    double SelectionPointMaxSize;
    double PickRadius;
    double TouchLoupeLift;
    double SelectionTransparency;
    long SelectionLinePattern;
    long SelectionLinePatternScale;
    double SelectionHiddenLineWidth;
    double SelectionBBoxLineWidth;
    bool ShowHighlightEdgeOnly;
    double PreSelectionDelay;
    long PickBackFaceDelay;
    bool UseNewRayPick;
    double ViewSelectionExtendFactor;
    bool UseTightBoundingBox;
    bool UseBoundingBoxCache;
    bool RenderProjectedBBox;
    bool SelectionFaceWire;
    double NewDocumentCameraScale;
    long MaxOnTopSelections;
    long MaxViewSelections;
    long MaxSelectionNotification;
    bool MapChildrenPlacement;
    double EditingTransparency;
    bool PerViewEdit;
    double DraggerScale;
    double HiddenLineTransparency;
    bool HiddenLineOverrideTransparency;
    unsigned long HiddenLineFaceColor;
    bool HiddenLineOverrideFaceColor;
    unsigned long HiddenLineColor;
    bool HiddenLineOverrideColor;
    unsigned long HiddenLineBackground;
    bool HiddenLineOverrideBackground;
    bool HiddenLineShaded;
    bool HiddenLineShowOutline;
    bool HiddenLinePerFaceOutline;
    bool HiddenLineSceneOutline;
    double HiddenLineOutlineWidth;
    double HiddenLineWidth;
    double HiddenLinePointSize;
    bool HiddenLineHideSeam;
    bool HiddenLineHideVertex;
    bool HiddenLineHideFace;
    long StatusMessageTimeout;
    long ShadowSync;
    bool ShadowFlatLines;
    long ShadowDisplayMode;
    bool ShadowSpotLight;
    double ShadowLightIntensity;
    double ShadowLightDirectionX;
    double ShadowLightDirectionY;
    double ShadowLightDirectionZ;
    unsigned long ShadowLightColor;
    bool ShadowShowGround;
    bool ShadowGroundBackFaceCull;
    double ShadowGroundScale;
    unsigned long ShadowGroundColor;
    std::string ShadowGroundBumpMap;
    std::string ShadowGroundTexture;
    double ShadowGroundTextureSize;
    double ShadowTransparency;
    double ShadowGroundTransparency;
    bool ShadowGroundShading;
    bool ShadowExtraRedraw;
    long ShadowSmoothBorder;
    long ShadowSpreadSize;
    long ShadowSpreadSampleSize;
    double ShadowPrecision;
    double ShadowEpsilon;
    double ShadowEpsilonMinimum;
    double ShadowThreshold;
    double ShadowBoundBoxScale;
    double ShadowMaxDistance;
    bool ShadowTransparentShadow;
    bool ShadowUpdateGround;
    unsigned long PropertyViewTimer;
    bool HierarchyAscend;
    long CommandHistorySize;
    long PieMenuIconSize;
    long PieMenuRadius;
    long PieMenuTriggerRadius;
    long PieMenuFontSize;
    long PieMenuTriggerDelay;
    bool PieMenuTriggerAction;
    long PieMenuAnimationDuration;
    long PieMenuAnimationCurve;
    long PieMenuCenterRadius;
    bool PieMenuPopup;
    bool StickyTaskControl;
    bool ColorOnTop;
    bool AutoSortWBList;
    long MaxCameraAnimatePeriod;
    bool TaskNoWheelFocus;
    bool GestureLongPressRotationCenter;
    bool CheckWidgetPlacementOnRestore;
    long TextCursorWidth;
    long PreselectionToolTipCorner;
    long PreselectionToolTipOffsetX;
    long PreselectionToolTipOffsetY;
    long PreselectionToolTipFontSize;
    bool SectionFill;
    bool SectionFillInvert;
    bool SectionConcave;
    bool NoSectionOnTop;
    double SectionHatchTextureScale;
    std::string SectionHatchTexture;
    bool SectionHatchTextureEnable;
    bool SectionFillGroup;
    bool ShowClipPlane;
    double ClipPlaneSize;
    std::string ClipPlaneColor;
    double ClipPlaneLineWidth;
    bool TransformOnTop;
    double SelectionColorDifference;
    long RenderCacheMergeCount;
    long RenderCacheMergeCountMin;
    long RenderCacheMergeCountMax;
    long RenderCacheMergeDepthMax;
    long RenderCacheMergeDepthMin;
    long RenderCacheKeepMax;
    long RenderCacheIncremental;
    long RenderCacheMeshReuse;
    long LiveImportRedrawInterval;
    long LiveImportRedrawBudget;
    long LiveImportPumpInterval;
    double RenderHighlightPolygonOffsetFactor;
    double RenderHighlightPolygonOffsetUnits;
    bool ForceSolidSingleSideLighting;
    long DefaultFontSize;
    bool EnableTaskPanelKeyTranslate;
    bool EnableMenuBarCheckBox;
    bool EnableBacklight;
    unsigned long BacklightColor;
    long BacklightIntensity;
    bool OverrideSelectability;
    unsigned long SelectionStackSize;
    long DefaultDrawStyle;
    long ToolTipIconSize;
    bool ToolTipDisable;
    unsigned long AxisXColor;
    unsigned long AxisYColor;
    unsigned long AxisZColor;
    bool DatumScreenSize;
    double DatumScale;
    double DatumPlaneSize;
    double DatumLineSize;
    double DatumTemporaryScaleFactor;
    double EyeDistance;
    bool CornerCoordSystem;
    long CornerCoordSystemSize;
    bool ShowAxisCross;
    bool ShowFPS;
    bool UseVBO;
    bool Orthographic;
    bool Perspective;
    bool ApplyCameraTypeToAll;
    long AntiAliasing;
    long TransparentObjectRenderType;
    std::string InternalTextureFormat;
    bool Gradient;
    bool RadialGradient;
    bool Simple;
    unsigned long BackgroundColor;
    unsigned long BackgroundColor2;
    unsigned long BackgroundColor3;
    unsigned long BackgroundColor4;
    bool UseBackgroundColorMid;
    bool EnableHeadlight;
    unsigned long HeadlightColor;
    long HeadlightIntensity;
    std::string HeadlightDirection;
    std::string BacklightDirection;
    bool EnableFillLight;
    unsigned long FillLightColor;
    long FillLightIntensity;
    std::string FillLightDirection;
    unsigned long AmbientLightColor;
    long AmbientLightIntensity;
    std::string NavigationStyle;
    bool SameStyleForAllViews;
    long OrbitStyle;
    long RotationMode;
    double Sensitivity;
    bool ResetCursorPosition;
    bool InvertZoom;
    bool ZoomAtCursor;
    double ZoomStep;
    bool UseNavigationAnimations;
    bool UseSpinningAnimations;
    long AnimationDuration;
    long stopAnimatingIfDeactivated;
    bool ShowRotationCenter;
    double RotationCenterSize;
    unsigned long RotationCenterColor;
    std::string NewDocumentCameraOrientation;
    bool AutoFitToView;
    bool ShowNaviCube;
    bool DisableTouchTilt;
    bool NavigationDebug;
    std::string GestureRollFwdCommand;
    std::string GestureRollBackCommand;
    bool SaveWBbyTab;
    unsigned long CbLabelColor;
    long CbLabelTextSize;
    double BoundingBoxFontSize;
    double DatumPointSize;
    double LocalCoordinateSystemSize;
    long DefaultShapeShininess;
    bool DimensionsVisible;
    bool Dimensions3dVisible;
    bool DimensionsDeltaVisible;
    std::string SavePicture;
    double HeadlightRotationX;
    double HeadlightRotationY;
    double HeadlightRotationZ;
    double HeadlightRotationW;

    // Auto generated code (Tools/params_utils.py:254)
    ViewParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
        handle->Attach(this);

        UseViewArea = this->handle->GetBool("UseViewArea", true);
        funcs["UseViewArea"] = &ViewParamsP::updateUseViewArea;
        UseNewSelection = this->handle->GetBool("UseNewSelection", true);
        funcs["UseNewSelection"] = &ViewParamsP::updateUseNewSelection;
        UseSelectionRoot = this->handle->GetBool("UseSelectionRoot", true);
        funcs["UseSelectionRoot"] = &ViewParamsP::updateUseSelectionRoot;
        EnableSelection = this->handle->GetBool("EnableSelection", true);
        funcs["EnableSelection"] = &ViewParamsP::updateEnableSelection;
        EnablePreselection = this->handle->GetBool("EnablePreselection", true);
        funcs["EnablePreselection"] = &ViewParamsP::updateEnablePreselection;
        RenderCache = this->handle->GetInt("RenderCache", 3);
        funcs["RenderCache"] = &ViewParamsP::updateRenderCache;
        UnifiedCanvas = this->handle->GetBool("UnifiedCanvas", false);
        funcs["UnifiedCanvas"] = &ViewParamsP::updateUnifiedCanvas;
        RandomColor = this->handle->GetBool("RandomColor", false);
        funcs["RandomColor"] = &ViewParamsP::updateRandomColor;
        BoundingBoxColor = this->handle->GetUnsigned("BoundingBoxColor", 0xFFFFFFFF);
        funcs["BoundingBoxColor"] = &ViewParamsP::updateBoundingBoxColor;
        AnnotationTextColor = this->handle->GetUnsigned("AnnotationTextColor", 0xFFFFFFFF);
        funcs["AnnotationTextColor"] = &ViewParamsP::updateAnnotationTextColor;
        CursorCrosshairColor = this->handle->GetUnsigned("CursorCrosshairColor", 0xFFFFFFFF);
        funcs["CursorCrosshairColor"] = &ViewParamsP::updateCursorCrosshairColor;
        HighlightColor = this->handle->GetUnsigned("HighlightColor", 0xE1E114FF);
        funcs["HighlightColor"] = &ViewParamsP::updateHighlightColor;
        SelectionColor = this->handle->GetUnsigned("SelectionColor", 0x1CAD1CFF);
        funcs["SelectionColor"] = &ViewParamsP::updateSelectionColor;
        MarkerSize = this->handle->GetInt("MarkerSize", 7);
        funcs["MarkerSize"] = &ViewParamsP::updateMarkerSize;
        DefaultLinkColor = this->handle->GetUnsigned("DefaultLinkColor", 0x66FFFFFF);
        funcs["DefaultLinkColor"] = &ViewParamsP::updateDefaultLinkColor;
        DefaultShapeLineColor = this->handle->GetUnsigned("DefaultShapeLineColor", 0x191919FF);
        funcs["DefaultShapeLineColor"] = &ViewParamsP::updateDefaultShapeLineColor;
        DefaultShapeVertexColor = this->handle->GetUnsigned("DefaultShapeVertexColor", 0x191919FF);
        funcs["DefaultShapeVertexColor"] = &ViewParamsP::updateDefaultShapeVertexColor;
        DefaultShapeColor = this->handle->GetUnsigned("DefaultShapeColor", 0xCCCCE6FF);
        funcs["DefaultShapeColor"] = &ViewParamsP::updateDefaultShapeColor;
        DefaultShapeTransparency = this->handle->GetInt("DefaultShapeTransparency", 0);
        funcs["DefaultShapeTransparency"] = &ViewParamsP::updateDefaultShapeTransparency;
        DefaultShapeLineWidth = this->handle->GetInt("DefaultShapeLineWidth", 2);
        funcs["DefaultShapeLineWidth"] = &ViewParamsP::updateDefaultShapeLineWidth;
        DefaultShapePointSize = this->handle->GetInt("DefaultShapePointSize", 2);
        funcs["DefaultShapePointSize"] = &ViewParamsP::updateDefaultShapePointSize;
        CoinCycleCheck = this->handle->GetBool("CoinCycleCheck", true);
        funcs["CoinCycleCheck"] = &ViewParamsP::updateCoinCycleCheck;
        EnablePropertyViewForInactiveDocument = this->handle->GetBool("EnablePropertyViewForInactiveDocument", true);
        funcs["EnablePropertyViewForInactiveDocument"] = &ViewParamsP::updateEnablePropertyViewForInactiveDocument;
        ShowSelectionBoundingBox = this->handle->GetBool("ShowSelectionBoundingBox", false);
        funcs["ShowSelectionBoundingBox"] = &ViewParamsP::updateShowSelectionBoundingBox;
        ShowSelectionBoundingBoxThreshold = this->handle->GetInt("ShowSelectionBoundingBoxThreshold", 0);
        funcs["ShowSelectionBoundingBoxThreshold"] = &ViewParamsP::updateShowSelectionBoundingBoxThreshold;
        UpdateSelectionVisual = this->handle->GetBool("UpdateSelectionVisual", true);
        funcs["UpdateSelectionVisual"] = &ViewParamsP::updateUpdateSelectionVisual;
        LinkChildrenDirect = this->handle->GetBool("LinkChildrenDirect", true);
        funcs["LinkChildrenDirect"] = &ViewParamsP::updateLinkChildrenDirect;
        ShowSelectionOnTop = this->handle->GetBool("ShowSelectionOnTop", true);
        funcs["ShowSelectionOnTop"] = &ViewParamsP::updateShowSelectionOnTop;
        ShowPreSelectedFaceOnTop = this->handle->GetBool("ShowPreSelectedFaceOnTop", true);
        funcs["ShowPreSelectedFaceOnTop"] = &ViewParamsP::updateShowPreSelectedFaceOnTop;
        ShowPreSelectedFaceOutline = this->handle->GetBool("ShowPreSelectedFaceOutline", true);
        funcs["ShowPreSelectedFaceOutline"] = &ViewParamsP::updateShowPreSelectedFaceOutline;
        ShowSelectedFaceOutline = this->handle->GetBool("ShowSelectedFaceOutline", true);
        funcs["ShowSelectedFaceOutline"] = &ViewParamsP::updateShowSelectedFaceOutline;
        OutlineThicken = this->handle->GetFloat("OutlineThicken", 4);
        funcs["OutlineThicken"] = &ViewParamsP::updateOutlineThicken;
        NoSelFaceHighlightWithOutline = this->handle->GetBool("NoSelFaceHighlightWithOutline", false);
        funcs["NoSelFaceHighlightWithOutline"] = &ViewParamsP::updateNoSelFaceHighlightWithOutline;
        NoPreSelFaceHighlightWithOutline = this->handle->GetBool("NoPreSelFaceHighlightWithOutline", true);
        funcs["NoPreSelFaceHighlightWithOutline"] = &ViewParamsP::updateNoPreSelFaceHighlightWithOutline;
        AutoTransparentPick = this->handle->GetBool("AutoTransparentPick", false);
        funcs["AutoTransparentPick"] = &ViewParamsP::updateAutoTransparentPick;
        SelectElementOnTop = this->handle->GetBool("SelectElementOnTop", false);
        funcs["SelectElementOnTop"] = &ViewParamsP::updateSelectElementOnTop;
        TransparencyOnTop = this->handle->GetFloat("TransparencyOnTop", 0.5);
        funcs["TransparencyOnTop"] = &ViewParamsP::updateTransparencyOnTop;
        HiddenLineSync = this->handle->GetInt("HiddenLineSync", 1);
        funcs["HiddenLineSync"] = &ViewParamsP::updateHiddenLineSync;
        HiddenLineSelectionOnTop = this->handle->GetBool("HiddenLineSelectionOnTop", true);
        funcs["HiddenLineSelectionOnTop"] = &ViewParamsP::updateHiddenLineSelectionOnTop;
        PartialHighlightOnFullSelect = this->handle->GetBool("PartialHighlightOnFullSelect", false);
        funcs["PartialHighlightOnFullSelect"] = &ViewParamsP::updatePartialHighlightOnFullSelect;
        SelectionLineThicken = this->handle->GetFloat("SelectionLineThicken", 1.5);
        funcs["SelectionLineThicken"] = &ViewParamsP::updateSelectionLineThicken;
        SelectionLineMaxWidth = this->handle->GetFloat("SelectionLineMaxWidth", 4.0);
        funcs["SelectionLineMaxWidth"] = &ViewParamsP::updateSelectionLineMaxWidth;
        SelectionPointScale = this->handle->GetFloat("SelectionPointScale", 2.5);
        funcs["SelectionPointScale"] = &ViewParamsP::updateSelectionPointScale;
        SelectionPointMaxSize = this->handle->GetFloat("SelectionPointMaxSize", 6.0);
        funcs["SelectionPointMaxSize"] = &ViewParamsP::updateSelectionPointMaxSize;
        PickRadius = this->handle->GetFloat("PickRadius", 5.0);
        funcs["PickRadius"] = &ViewParamsP::updatePickRadius;
        TouchLoupeLift = this->handle->GetFloat("TouchLoupeLift", 28.0);
        funcs["TouchLoupeLift"] = &ViewParamsP::updateTouchLoupeLift;
        SelectionTransparency = this->handle->GetFloat("SelectionTransparency", 0.5);
        funcs["SelectionTransparency"] = &ViewParamsP::updateSelectionTransparency;
        SelectionLinePattern = this->handle->GetInt("SelectionLinePattern", 0);
        funcs["SelectionLinePattern"] = &ViewParamsP::updateSelectionLinePattern;
        SelectionLinePatternScale = this->handle->GetInt("SelectionLinePatternScale", 1);
        funcs["SelectionLinePatternScale"] = &ViewParamsP::updateSelectionLinePatternScale;
        SelectionHiddenLineWidth = this->handle->GetFloat("SelectionHiddenLineWidth", 1.0);
        funcs["SelectionHiddenLineWidth"] = &ViewParamsP::updateSelectionHiddenLineWidth;
        SelectionBBoxLineWidth = this->handle->GetFloat("SelectionBBoxLineWidth", 3.0);
        funcs["SelectionBBoxLineWidth"] = &ViewParamsP::updateSelectionBBoxLineWidth;
        ShowHighlightEdgeOnly = this->handle->GetBool("ShowHighlightEdgeOnly", false);
        funcs["ShowHighlightEdgeOnly"] = &ViewParamsP::updateShowHighlightEdgeOnly;
        PreSelectionDelay = this->handle->GetFloat("PreSelectionDelay", 0.1);
        funcs["PreSelectionDelay"] = &ViewParamsP::updatePreSelectionDelay;
        PickBackFaceDelay = this->handle->GetInt("PickBackFaceDelay", 2);
        funcs["PickBackFaceDelay"] = &ViewParamsP::updatePickBackFaceDelay;
        UseNewRayPick = this->handle->GetBool("UseNewRayPick", true);
        funcs["UseNewRayPick"] = &ViewParamsP::updateUseNewRayPick;
        ViewSelectionExtendFactor = this->handle->GetFloat("ViewSelectionExtendFactor", 0.5);
        funcs["ViewSelectionExtendFactor"] = &ViewParamsP::updateViewSelectionExtendFactor;
        UseTightBoundingBox = this->handle->GetBool("UseTightBoundingBox", true);
        funcs["UseTightBoundingBox"] = &ViewParamsP::updateUseTightBoundingBox;
        UseBoundingBoxCache = this->handle->GetBool("UseBoundingBoxCache", true);
        funcs["UseBoundingBoxCache"] = &ViewParamsP::updateUseBoundingBoxCache;
        RenderProjectedBBox = this->handle->GetBool("RenderProjectedBBox", true);
        funcs["RenderProjectedBBox"] = &ViewParamsP::updateRenderProjectedBBox;
        SelectionFaceWire = this->handle->GetBool("SelectionFaceWire", false);
        funcs["SelectionFaceWire"] = &ViewParamsP::updateSelectionFaceWire;
        NewDocumentCameraScale = this->handle->GetFloat("NewDocumentCameraScale", 100.0);
        funcs["NewDocumentCameraScale"] = &ViewParamsP::updateNewDocumentCameraScale;
        MaxOnTopSelections = this->handle->GetInt("MaxOnTopSelections", 100);
        funcs["MaxOnTopSelections"] = &ViewParamsP::updateMaxOnTopSelections;
        MaxViewSelections = this->handle->GetInt("MaxViewSelections", 100);
        funcs["MaxViewSelections"] = &ViewParamsP::updateMaxViewSelections;
        MaxSelectionNotification = this->handle->GetInt("MaxSelectionNotification", 100);
        funcs["MaxSelectionNotification"] = &ViewParamsP::updateMaxSelectionNotification;
        MapChildrenPlacement = this->handle->GetBool("MapChildrenPlacement", false);
        funcs["MapChildrenPlacement"] = &ViewParamsP::updateMapChildrenPlacement;
        EditingTransparency = this->handle->GetFloat("EditingTransparency", 0.5);
        funcs["EditingTransparency"] = &ViewParamsP::updateEditingTransparency;
        PerViewEdit = this->handle->GetBool("PerViewEdit", false);
        funcs["PerViewEdit"] = &ViewParamsP::updatePerViewEdit;
        DraggerScale = this->handle->GetFloat("DraggerScale", 0.03);
        funcs["DraggerScale"] = &ViewParamsP::updateDraggerScale;
        HiddenLineTransparency = this->handle->GetFloat("HiddenLineTransparency", 0.4);
        funcs["HiddenLineTransparency"] = &ViewParamsP::updateHiddenLineTransparency;
        HiddenLineOverrideTransparency = this->handle->GetBool("HiddenLineOverrideTransparency", true);
        funcs["HiddenLineOverrideTransparency"] = &ViewParamsP::updateHiddenLineOverrideTransparency;
        HiddenLineFaceColor = this->handle->GetUnsigned("HiddenLineFaceColor", 0xFFFFFFFF);
        funcs["HiddenLineFaceColor"] = &ViewParamsP::updateHiddenLineFaceColor;
        HiddenLineOverrideFaceColor = this->handle->GetBool("HiddenLineOverrideFaceColor", true);
        funcs["HiddenLineOverrideFaceColor"] = &ViewParamsP::updateHiddenLineOverrideFaceColor;
        HiddenLineColor = this->handle->GetUnsigned("HiddenLineColor", 0x000000FF);
        funcs["HiddenLineColor"] = &ViewParamsP::updateHiddenLineColor;
        HiddenLineOverrideColor = this->handle->GetBool("HiddenLineOverrideColor", true);
        funcs["HiddenLineOverrideColor"] = &ViewParamsP::updateHiddenLineOverrideColor;
        HiddenLineBackground = this->handle->GetUnsigned("HiddenLineBackground", 0xFFFFFFFF);
        funcs["HiddenLineBackground"] = &ViewParamsP::updateHiddenLineBackground;
        HiddenLineOverrideBackground = this->handle->GetBool("HiddenLineOverrideBackground", true);
        funcs["HiddenLineOverrideBackground"] = &ViewParamsP::updateHiddenLineOverrideBackground;
        HiddenLineShaded = this->handle->GetBool("HiddenLineShaded", false);
        funcs["HiddenLineShaded"] = &ViewParamsP::updateHiddenLineShaded;
        HiddenLineShowOutline = this->handle->GetBool("HiddenLineShowOutline", true);
        funcs["HiddenLineShowOutline"] = &ViewParamsP::updateHiddenLineShowOutline;
        HiddenLinePerFaceOutline = this->handle->GetBool("HiddenLinePerFaceOutline", false);
        funcs["HiddenLinePerFaceOutline"] = &ViewParamsP::updateHiddenLinePerFaceOutline;
        HiddenLineSceneOutline = this->handle->GetBool("HiddenLineSceneOutline", false);
        funcs["HiddenLineSceneOutline"] = &ViewParamsP::updateHiddenLineSceneOutline;
        HiddenLineOutlineWidth = this->handle->GetFloat("HiddenLineOutlineWidth", 0.0);
        funcs["HiddenLineOutlineWidth"] = &ViewParamsP::updateHiddenLineOutlineWidth;
        HiddenLineWidth = this->handle->GetFloat("HiddenLineWidth", 1.5);
        funcs["HiddenLineWidth"] = &ViewParamsP::updateHiddenLineWidth;
        HiddenLinePointSize = this->handle->GetFloat("HiddenLinePointSize", 2);
        funcs["HiddenLinePointSize"] = &ViewParamsP::updateHiddenLinePointSize;
        HiddenLineHideSeam = this->handle->GetBool("HiddenLineHideSeam", true);
        funcs["HiddenLineHideSeam"] = &ViewParamsP::updateHiddenLineHideSeam;
        HiddenLineHideVertex = this->handle->GetBool("HiddenLineHideVertex", true);
        funcs["HiddenLineHideVertex"] = &ViewParamsP::updateHiddenLineHideVertex;
        HiddenLineHideFace = this->handle->GetBool("HiddenLineHideFace", false);
        funcs["HiddenLineHideFace"] = &ViewParamsP::updateHiddenLineHideFace;
        StatusMessageTimeout = this->handle->GetInt("StatusMessageTimeout", 5000);
        funcs["StatusMessageTimeout"] = &ViewParamsP::updateStatusMessageTimeout;
        ShadowSync = this->handle->GetInt("ShadowSync", 1);
        funcs["ShadowSync"] = &ViewParamsP::updateShadowSync;
        ShadowFlatLines = this->handle->GetBool("ShadowFlatLines", true);
        funcs["ShadowFlatLines"] = &ViewParamsP::updateShadowFlatLines;
        ShadowDisplayMode = this->handle->GetInt("ShadowDisplayMode", 2);
        funcs["ShadowDisplayMode"] = &ViewParamsP::updateShadowDisplayMode;
        ShadowSpotLight = this->handle->GetBool("ShadowSpotLight", false);
        funcs["ShadowSpotLight"] = &ViewParamsP::updateShadowSpotLight;
        ShadowLightIntensity = this->handle->GetFloat("ShadowLightIntensity", 0.8);
        funcs["ShadowLightIntensity"] = &ViewParamsP::updateShadowLightIntensity;
        ShadowLightDirectionX = this->handle->GetFloat("ShadowLightDirectionX", -1.0);
        funcs["ShadowLightDirectionX"] = &ViewParamsP::updateShadowLightDirectionX;
        ShadowLightDirectionY = this->handle->GetFloat("ShadowLightDirectionY", -1.0);
        funcs["ShadowLightDirectionY"] = &ViewParamsP::updateShadowLightDirectionY;
        ShadowLightDirectionZ = this->handle->GetFloat("ShadowLightDirectionZ", -1.0);
        funcs["ShadowLightDirectionZ"] = &ViewParamsP::updateShadowLightDirectionZ;
        ShadowLightColor = this->handle->GetUnsigned("ShadowLightColor", 0xF0FDFFFF);
        funcs["ShadowLightColor"] = &ViewParamsP::updateShadowLightColor;
        ShadowShowGround = this->handle->GetBool("ShadowShowGround", true);
        funcs["ShadowShowGround"] = &ViewParamsP::updateShadowShowGround;
        ShadowGroundBackFaceCull = this->handle->GetBool("ShadowGroundBackFaceCull", true);
        funcs["ShadowGroundBackFaceCull"] = &ViewParamsP::updateShadowGroundBackFaceCull;
        ShadowGroundScale = this->handle->GetFloat("ShadowGroundScale", 2.0);
        funcs["ShadowGroundScale"] = &ViewParamsP::updateShadowGroundScale;
        ShadowGroundColor = this->handle->GetUnsigned("ShadowGroundColor", 0x7D7D7DFF);
        funcs["ShadowGroundColor"] = &ViewParamsP::updateShadowGroundColor;
        ShadowGroundBumpMap = this->handle->GetASCII("ShadowGroundBumpMap", "");
        funcs["ShadowGroundBumpMap"] = &ViewParamsP::updateShadowGroundBumpMap;
        ShadowGroundTexture = this->handle->GetASCII("ShadowGroundTexture", "");
        funcs["ShadowGroundTexture"] = &ViewParamsP::updateShadowGroundTexture;
        ShadowGroundTextureSize = this->handle->GetFloat("ShadowGroundTextureSize", 100.0);
        funcs["ShadowGroundTextureSize"] = &ViewParamsP::updateShadowGroundTextureSize;
        ShadowTransparency = this->handle->GetFloat("ShadowTransparency", 0.2);
        funcs["ShadowTransparency"] = &ViewParamsP::updateShadowTransparency;
        ShadowGroundTransparency = this->handle->GetFloat("ShadowGroundTransparency", 1.0);
        funcs["ShadowGroundTransparency"] = &ViewParamsP::updateShadowGroundTransparency;
        ShadowGroundShading = this->handle->GetBool("ShadowGroundShading", true);
        funcs["ShadowGroundShading"] = &ViewParamsP::updateShadowGroundShading;
        ShadowExtraRedraw = this->handle->GetBool("ShadowExtraRedraw", true);
        funcs["ShadowExtraRedraw"] = &ViewParamsP::updateShadowExtraRedraw;
        ShadowSmoothBorder = this->handle->GetInt("ShadowSmoothBorder", 40);
        funcs["ShadowSmoothBorder"] = &ViewParamsP::updateShadowSmoothBorder;
        ShadowSpreadSize = this->handle->GetInt("ShadowSpreadSize", 0);
        funcs["ShadowSpreadSize"] = &ViewParamsP::updateShadowSpreadSize;
        ShadowSpreadSampleSize = this->handle->GetInt("ShadowSpreadSampleSize", 0);
        funcs["ShadowSpreadSampleSize"] = &ViewParamsP::updateShadowSpreadSampleSize;
        ShadowPrecision = this->handle->GetFloat("ShadowPrecision", 1.0);
        funcs["ShadowPrecision"] = &ViewParamsP::updateShadowPrecision;
        ShadowEpsilon = this->handle->GetFloat("ShadowEpsilon", 1e-05);
        funcs["ShadowEpsilon"] = &ViewParamsP::updateShadowEpsilon;
        ShadowEpsilonMinimum = this->handle->GetFloat("ShadowEpsilonMinimum", 1e-06);
        funcs["ShadowEpsilonMinimum"] = &ViewParamsP::updateShadowEpsilonMinimum;
        ShadowThreshold = this->handle->GetFloat("ShadowThreshold", 0.0);
        funcs["ShadowThreshold"] = &ViewParamsP::updateShadowThreshold;
        ShadowBoundBoxScale = this->handle->GetFloat("ShadowBoundBoxScale", 1.2);
        funcs["ShadowBoundBoxScale"] = &ViewParamsP::updateShadowBoundBoxScale;
        ShadowMaxDistance = this->handle->GetFloat("ShadowMaxDistance", 0.0);
        funcs["ShadowMaxDistance"] = &ViewParamsP::updateShadowMaxDistance;
        ShadowTransparentShadow = this->handle->GetBool("ShadowTransparentShadow", false);
        funcs["ShadowTransparentShadow"] = &ViewParamsP::updateShadowTransparentShadow;
        ShadowUpdateGround = this->handle->GetBool("ShadowUpdateGround", true);
        funcs["ShadowUpdateGround"] = &ViewParamsP::updateShadowUpdateGround;
        PropertyViewTimer = this->handle->GetUnsigned("PropertyViewTimer", 100);
        funcs["PropertyViewTimer"] = &ViewParamsP::updatePropertyViewTimer;
        HierarchyAscend = this->handle->GetBool("HierarchyAscend", false);
        funcs["HierarchyAscend"] = &ViewParamsP::updateHierarchyAscend;
        CommandHistorySize = this->handle->GetInt("CommandHistorySize", 20);
        funcs["CommandHistorySize"] = &ViewParamsP::updateCommandHistorySize;
        PieMenuIconSize = this->handle->GetInt("PieMenuIconSize", 24);
        funcs["PieMenuIconSize"] = &ViewParamsP::updatePieMenuIconSize;
        PieMenuRadius = this->handle->GetInt("PieMenuRadius", 100);
        funcs["PieMenuRadius"] = &ViewParamsP::updatePieMenuRadius;
        PieMenuTriggerRadius = this->handle->GetInt("PieMenuTriggerRadius", 60);
        funcs["PieMenuTriggerRadius"] = &ViewParamsP::updatePieMenuTriggerRadius;
        PieMenuFontSize = this->handle->GetInt("PieMenuFontSize", 0);
        funcs["PieMenuFontSize"] = &ViewParamsP::updatePieMenuFontSize;
        PieMenuTriggerDelay = this->handle->GetInt("PieMenuTriggerDelay", 200);
        funcs["PieMenuTriggerDelay"] = &ViewParamsP::updatePieMenuTriggerDelay;
        PieMenuTriggerAction = this->handle->GetBool("PieMenuTriggerAction", false);
        funcs["PieMenuTriggerAction"] = &ViewParamsP::updatePieMenuTriggerAction;
        PieMenuAnimationDuration = this->handle->GetInt("PieMenuAnimationDuration", 250);
        funcs["PieMenuAnimationDuration"] = &ViewParamsP::updatePieMenuAnimationDuration;
        PieMenuAnimationCurve = this->handle->GetInt("PieMenuAnimationCurve", 38);
        funcs["PieMenuAnimationCurve"] = &ViewParamsP::updatePieMenuAnimationCurve;
        PieMenuCenterRadius = this->handle->GetInt("PieMenuCenterRadius", 10);
        funcs["PieMenuCenterRadius"] = &ViewParamsP::updatePieMenuCenterRadius;
        PieMenuPopup = this->handle->GetBool("PieMenuPopup", false);
        funcs["PieMenuPopup"] = &ViewParamsP::updatePieMenuPopup;
        StickyTaskControl = this->handle->GetBool("StickyTaskControl", true);
        funcs["StickyTaskControl"] = &ViewParamsP::updateStickyTaskControl;
        ColorOnTop = this->handle->GetBool("ColorOnTop", true);
        funcs["ColorOnTop"] = &ViewParamsP::updateColorOnTop;
        AutoSortWBList = this->handle->GetBool("AutoSortWBList", false);
        funcs["AutoSortWBList"] = &ViewParamsP::updateAutoSortWBList;
        MaxCameraAnimatePeriod = this->handle->GetInt("MaxCameraAnimatePeriod", 3000);
        funcs["MaxCameraAnimatePeriod"] = &ViewParamsP::updateMaxCameraAnimatePeriod;
        TaskNoWheelFocus = this->handle->GetBool("TaskNoWheelFocus", true);
        funcs["TaskNoWheelFocus"] = &ViewParamsP::updateTaskNoWheelFocus;
        GestureLongPressRotationCenter = this->handle->GetBool("GestureLongPressRotationCenter", false);
        funcs["GestureLongPressRotationCenter"] = &ViewParamsP::updateGestureLongPressRotationCenter;
        CheckWidgetPlacementOnRestore = this->handle->GetBool("CheckWidgetPlacementOnRestore", true);
        funcs["CheckWidgetPlacementOnRestore"] = &ViewParamsP::updateCheckWidgetPlacementOnRestore;
        TextCursorWidth = this->handle->GetInt("TextCursorWidth", 1);
        funcs["TextCursorWidth"] = &ViewParamsP::updateTextCursorWidth;
        PreselectionToolTipCorner = this->handle->GetInt("PreselectionToolTipCorner", 3);
        funcs["PreselectionToolTipCorner"] = &ViewParamsP::updatePreselectionToolTipCorner;
        PreselectionToolTipOffsetX = this->handle->GetInt("PreselectionToolTipOffsetX", 0);
        funcs["PreselectionToolTipOffsetX"] = &ViewParamsP::updatePreselectionToolTipOffsetX;
        PreselectionToolTipOffsetY = this->handle->GetInt("PreselectionToolTipOffsetY", 0);
        funcs["PreselectionToolTipOffsetY"] = &ViewParamsP::updatePreselectionToolTipOffsetY;
        PreselectionToolTipFontSize = this->handle->GetInt("PreselectionToolTipFontSize", 0);
        funcs["PreselectionToolTipFontSize"] = &ViewParamsP::updatePreselectionToolTipFontSize;
        SectionFill = this->handle->GetBool("SectionFill", true);
        funcs["SectionFill"] = &ViewParamsP::updateSectionFill;
        SectionFillInvert = this->handle->GetBool("SectionFillInvert", true);
        funcs["SectionFillInvert"] = &ViewParamsP::updateSectionFillInvert;
        SectionConcave = this->handle->GetBool("SectionConcave", false);
        funcs["SectionConcave"] = &ViewParamsP::updateSectionConcave;
        NoSectionOnTop = this->handle->GetBool("NoSectionOnTop", true);
        funcs["NoSectionOnTop"] = &ViewParamsP::updateNoSectionOnTop;
        SectionHatchTextureScale = this->handle->GetFloat("SectionHatchTextureScale", 1.0);
        funcs["SectionHatchTextureScale"] = &ViewParamsP::updateSectionHatchTextureScale;
        SectionHatchTexture = this->handle->GetASCII("SectionHatchTexture", ":icons/section-hatch.png");
        funcs["SectionHatchTexture"] = &ViewParamsP::updateSectionHatchTexture;
        SectionHatchTextureEnable = this->handle->GetBool("SectionHatchTextureEnable", true);
        funcs["SectionHatchTextureEnable"] = &ViewParamsP::updateSectionHatchTextureEnable;
        SectionFillGroup = this->handle->GetBool("SectionFillGroup", false);
        funcs["SectionFillGroup"] = &ViewParamsP::updateSectionFillGroup;
        ShowClipPlane = this->handle->GetBool("ShowClipPlane", false);
        funcs["ShowClipPlane"] = &ViewParamsP::updateShowClipPlane;
        ClipPlaneSize = this->handle->GetFloat("ClipPlaneSize", 40.0);
        funcs["ClipPlaneSize"] = &ViewParamsP::updateClipPlaneSize;
        ClipPlaneColor = this->handle->GetASCII("ClipPlaneColor", "cyan");
        funcs["ClipPlaneColor"] = &ViewParamsP::updateClipPlaneColor;
        ClipPlaneLineWidth = this->handle->GetFloat("ClipPlaneLineWidth", 2.0);
        funcs["ClipPlaneLineWidth"] = &ViewParamsP::updateClipPlaneLineWidth;
        TransformOnTop = this->handle->GetBool("TransformOnTop", true);
        funcs["TransformOnTop"] = &ViewParamsP::updateTransformOnTop;
        SelectionColorDifference = this->handle->GetFloat("SelectionColorDifference", 25.0);
        funcs["SelectionColorDifference"] = &ViewParamsP::updateSelectionColorDifference;
        RenderCacheMergeCount = this->handle->GetInt("RenderCacheMergeCount", 0);
        funcs["RenderCacheMergeCount"] = &ViewParamsP::updateRenderCacheMergeCount;
        RenderCacheMergeCountMin = this->handle->GetInt("RenderCacheMergeCountMin", 10);
        funcs["RenderCacheMergeCountMin"] = &ViewParamsP::updateRenderCacheMergeCountMin;
        RenderCacheMergeCountMax = this->handle->GetInt("RenderCacheMergeCountMax", 0);
        funcs["RenderCacheMergeCountMax"] = &ViewParamsP::updateRenderCacheMergeCountMax;
        RenderCacheMergeDepthMax = this->handle->GetInt("RenderCacheMergeDepthMax", -1);
        funcs["RenderCacheMergeDepthMax"] = &ViewParamsP::updateRenderCacheMergeDepthMax;
        RenderCacheMergeDepthMin = this->handle->GetInt("RenderCacheMergeDepthMin", 1);
        funcs["RenderCacheMergeDepthMin"] = &ViewParamsP::updateRenderCacheMergeDepthMin;
        RenderCacheKeepMax = this->handle->GetInt("RenderCacheKeepMax", 32);
        funcs["RenderCacheKeepMax"] = &ViewParamsP::updateRenderCacheKeepMax;
        RenderCacheIncremental = this->handle->GetInt("RenderCacheIncremental", 1);
        funcs["RenderCacheIncremental"] = &ViewParamsP::updateRenderCacheIncremental;
        RenderCacheMeshReuse = this->handle->GetInt("RenderCacheMeshReuse", 1);
        funcs["RenderCacheMeshReuse"] = &ViewParamsP::updateRenderCacheMeshReuse;
        LiveImportRedrawInterval = this->handle->GetInt("LiveImportRedrawInterval", 200);
        funcs["LiveImportRedrawInterval"] = &ViewParamsP::updateLiveImportRedrawInterval;
        LiveImportRedrawBudget = this->handle->GetInt("LiveImportRedrawBudget", 10);
        funcs["LiveImportRedrawBudget"] = &ViewParamsP::updateLiveImportRedrawBudget;
        LiveImportPumpInterval = this->handle->GetInt("LiveImportPumpInterval", 50);
        funcs["LiveImportPumpInterval"] = &ViewParamsP::updateLiveImportPumpInterval;
        RenderHighlightPolygonOffsetFactor = this->handle->GetFloat("RenderHighlightPolygonOffsetFactor", 1);
        funcs["RenderHighlightPolygonOffsetFactor"] = &ViewParamsP::updateRenderHighlightPolygonOffsetFactor;
        RenderHighlightPolygonOffsetUnits = this->handle->GetFloat("RenderHighlightPolygonOffsetUnits", 1);
        funcs["RenderHighlightPolygonOffsetUnits"] = &ViewParamsP::updateRenderHighlightPolygonOffsetUnits;
        ForceSolidSingleSideLighting = this->handle->GetBool("ForceSolidSingleSideLighting", true);
        funcs["ForceSolidSingleSideLighting"] = &ViewParamsP::updateForceSolidSingleSideLighting;
        DefaultFontSize = this->handle->GetInt("DefaultFontSize", 0);
        funcs["DefaultFontSize"] = &ViewParamsP::updateDefaultFontSize;
        EnableTaskPanelKeyTranslate = this->handle->GetBool("EnableTaskPanelKeyTranslate", false);
        funcs["EnableTaskPanelKeyTranslate"] = &ViewParamsP::updateEnableTaskPanelKeyTranslate;
        EnableMenuBarCheckBox = this->handle->GetBool("EnableMenuBarCheckBox", FC_ENABLE_MENUBAR_CHECKBOX);
        funcs["EnableMenuBarCheckBox"] = &ViewParamsP::updateEnableMenuBarCheckBox;
        EnableBacklight = this->handle->GetBool("EnableBacklight", false);
        funcs["EnableBacklight"] = &ViewParamsP::updateEnableBacklight;
        BacklightColor = this->handle->GetUnsigned("BacklightColor", 0xFFFFFFFF);
        funcs["BacklightColor"] = &ViewParamsP::updateBacklightColor;
        BacklightIntensity = this->handle->GetInt("BacklightIntensity", 100);
        funcs["BacklightIntensity"] = &ViewParamsP::updateBacklightIntensity;
        OverrideSelectability = this->handle->GetBool("OverrideSelectability", false);
        funcs["OverrideSelectability"] = &ViewParamsP::updateOverrideSelectability;
        SelectionStackSize = this->handle->GetUnsigned("SelectionStackSize", 30);
        funcs["SelectionStackSize"] = &ViewParamsP::updateSelectionStackSize;
        DefaultDrawStyle = this->handle->GetInt("DefaultDrawStyle", 0);
        funcs["DefaultDrawStyle"] = &ViewParamsP::updateDefaultDrawStyle;
        ToolTipIconSize = this->handle->GetInt("ToolTipIconSize", 64);
        funcs["ToolTipIconSize"] = &ViewParamsP::updateToolTipIconSize;
        ToolTipDisable = this->handle->GetBool("ToolTipDisable", false);
        funcs["ToolTipDisable"] = &ViewParamsP::updateToolTipDisable;
        AxisXColor = this->handle->GetUnsigned("AxisXColor", 0xCC333300);
        funcs["AxisXColor"] = &ViewParamsP::updateAxisXColor;
        AxisYColor = this->handle->GetUnsigned("AxisYColor", 0x33CC3300);
        funcs["AxisYColor"] = &ViewParamsP::updateAxisYColor;
        AxisZColor = this->handle->GetUnsigned("AxisZColor", 0x3333CC00);
        funcs["AxisZColor"] = &ViewParamsP::updateAxisZColor;
        DatumScreenSize = this->handle->GetBool("DatumScreenSize", true);
        funcs["DatumScreenSize"] = &ViewParamsP::updateDatumScreenSize;
        DatumScale = this->handle->GetFloat("DatumScale", 100.0);
        funcs["DatumScale"] = &ViewParamsP::updateDatumScale;
        DatumPlaneSize = this->handle->GetFloat("DatumPlaneSize", 62.0);
        funcs["DatumPlaneSize"] = &ViewParamsP::updateDatumPlaneSize;
        DatumLineSize = this->handle->GetFloat("DatumLineSize", 70.0);
        funcs["DatumLineSize"] = &ViewParamsP::updateDatumLineSize;
        DatumTemporaryScaleFactor = this->handle->GetFloat("DatumTemporaryScaleFactor", 2.0);
        funcs["DatumTemporaryScaleFactor"] = &ViewParamsP::updateDatumTemporaryScaleFactor;
        EyeDistance = this->handle->GetFloat("EyeDistance", 5.0);
        funcs["EyeDistance"] = &ViewParamsP::updateEyeDistance;
        CornerCoordSystem = this->handle->GetBool("CornerCoordSystem", true);
        funcs["CornerCoordSystem"] = &ViewParamsP::updateCornerCoordSystem;
        CornerCoordSystemSize = this->handle->GetInt("CornerCoordSystemSize", 10);
        funcs["CornerCoordSystemSize"] = &ViewParamsP::updateCornerCoordSystemSize;
        ShowAxisCross = this->handle->GetBool("ShowAxisCross", false);
        funcs["ShowAxisCross"] = &ViewParamsP::updateShowAxisCross;
        ShowFPS = this->handle->GetBool("ShowFPS", false);
        funcs["ShowFPS"] = &ViewParamsP::updateShowFPS;
        UseVBO = this->handle->GetBool("UseVBO", false);
        funcs["UseVBO"] = &ViewParamsP::updateUseVBO;
        Orthographic = this->handle->GetBool("Orthographic", true);
        funcs["Orthographic"] = &ViewParamsP::updateOrthographic;
        Perspective = this->handle->GetBool("Perspective", false);
        funcs["Perspective"] = &ViewParamsP::updatePerspective;
        ApplyCameraTypeToAll = this->handle->GetBool("ApplyCameraTypeToAll", false);
        funcs["ApplyCameraTypeToAll"] = &ViewParamsP::updateApplyCameraTypeToAll;
        AntiAliasing = this->handle->GetInt("AntiAliasing", 3);
        funcs["AntiAliasing"] = &ViewParamsP::updateAntiAliasing;
        TransparentObjectRenderType = this->handle->GetInt("TransparentObjectRenderType", 0);
        funcs["TransparentObjectRenderType"] = &ViewParamsP::updateTransparentObjectRenderType;
        InternalTextureFormat = this->handle->GetASCII("InternalTextureFormat", "Default");
        funcs["InternalTextureFormat"] = &ViewParamsP::updateInternalTextureFormat;
        Gradient = this->handle->GetBool("Gradient", true);
        funcs["Gradient"] = &ViewParamsP::updateGradient;
        RadialGradient = this->handle->GetBool("RadialGradient", false);
        funcs["RadialGradient"] = &ViewParamsP::updateRadialGradient;
        Simple = this->handle->GetBool("Simple", false);
        funcs["Simple"] = &ViewParamsP::updateSimple;
        BackgroundColor = this->handle->GetUnsigned("BackgroundColor", 0xEAE5DCFF);
        funcs["BackgroundColor"] = &ViewParamsP::updateBackgroundColor;
        BackgroundColor2 = this->handle->GetUnsigned("BackgroundColor2", 0x333365FF);
        funcs["BackgroundColor2"] = &ViewParamsP::updateBackgroundColor2;
        BackgroundColor3 = this->handle->GetUnsigned("BackgroundColor3", 0xABABC1FF);
        funcs["BackgroundColor3"] = &ViewParamsP::updateBackgroundColor3;
        BackgroundColor4 = this->handle->GetUnsigned("BackgroundColor4", 0x6F6F93FF);
        funcs["BackgroundColor4"] = &ViewParamsP::updateBackgroundColor4;
        UseBackgroundColorMid = this->handle->GetBool("UseBackgroundColorMid", false);
        funcs["UseBackgroundColorMid"] = &ViewParamsP::updateUseBackgroundColorMid;
        EnableHeadlight = this->handle->GetBool("EnableHeadlight", true);
        funcs["EnableHeadlight"] = &ViewParamsP::updateEnableHeadlight;
        HeadlightColor = this->handle->GetUnsigned("HeadlightColor", 0xFFFFFFFF);
        funcs["HeadlightColor"] = &ViewParamsP::updateHeadlightColor;
        HeadlightIntensity = this->handle->GetInt("HeadlightIntensity", 100);
        funcs["HeadlightIntensity"] = &ViewParamsP::updateHeadlightIntensity;
        HeadlightDirection = this->handle->GetASCII("HeadlightDirection", "");
        funcs["HeadlightDirection"] = &ViewParamsP::updateHeadlightDirection;
        BacklightDirection = this->handle->GetASCII("BacklightDirection", "");
        funcs["BacklightDirection"] = &ViewParamsP::updateBacklightDirection;
        EnableFillLight = this->handle->GetBool("EnableFillLight", false);
        funcs["EnableFillLight"] = &ViewParamsP::updateEnableFillLight;
        FillLightColor = this->handle->GetUnsigned("FillLightColor", 0xE6FAFFFF);
        funcs["FillLightColor"] = &ViewParamsP::updateFillLightColor;
        FillLightIntensity = this->handle->GetInt("FillLightIntensity", 60);
        funcs["FillLightIntensity"] = &ViewParamsP::updateFillLightIntensity;
        FillLightDirection = this->handle->GetASCII("FillLightDirection", "");
        funcs["FillLightDirection"] = &ViewParamsP::updateFillLightDirection;
        AmbientLightColor = this->handle->GetUnsigned("AmbientLightColor", 0xFFFFFFFF);
        funcs["AmbientLightColor"] = &ViewParamsP::updateAmbientLightColor;
        AmbientLightIntensity = this->handle->GetInt("AmbientLightIntensity", 20);
        funcs["AmbientLightIntensity"] = &ViewParamsP::updateAmbientLightIntensity;
        NavigationStyle = this->handle->GetASCII("NavigationStyle", "Gui::CADNavigationStyle");
        funcs["NavigationStyle"] = &ViewParamsP::updateNavigationStyle;
        SameStyleForAllViews = this->handle->GetBool("SameStyleForAllViews", true);
        funcs["SameStyleForAllViews"] = &ViewParamsP::updateSameStyleForAllViews;
        OrbitStyle = this->handle->GetInt("OrbitStyle", 1);
        funcs["OrbitStyle"] = &ViewParamsP::updateOrbitStyle;
        RotationMode = this->handle->GetInt("RotationMode", 1);
        funcs["RotationMode"] = &ViewParamsP::updateRotationMode;
        Sensitivity = this->handle->GetFloat("Sensitivity", 2.0);
        funcs["Sensitivity"] = &ViewParamsP::updateSensitivity;
        ResetCursorPosition = this->handle->GetBool("ResetCursorPosition", false);
        funcs["ResetCursorPosition"] = &ViewParamsP::updateResetCursorPosition;
        InvertZoom = this->handle->GetBool("InvertZoom", true);
        funcs["InvertZoom"] = &ViewParamsP::updateInvertZoom;
        ZoomAtCursor = this->handle->GetBool("ZoomAtCursor", true);
        funcs["ZoomAtCursor"] = &ViewParamsP::updateZoomAtCursor;
        ZoomStep = this->handle->GetFloat("ZoomStep", 0.2);
        funcs["ZoomStep"] = &ViewParamsP::updateZoomStep;
        UseNavigationAnimations = this->handle->GetBool("UseNavigationAnimations", true);
        funcs["UseNavigationAnimations"] = &ViewParamsP::updateUseNavigationAnimations;
        UseSpinningAnimations = this->handle->GetBool("UseSpinningAnimations", false);
        funcs["UseSpinningAnimations"] = &ViewParamsP::updateUseSpinningAnimations;
        AnimationDuration = this->handle->GetInt("AnimationDuration", 250);
        funcs["AnimationDuration"] = &ViewParamsP::updateAnimationDuration;
        stopAnimatingIfDeactivated = this->handle->GetInt("stopAnimatingIfDeactivated", 3000);
        funcs["stopAnimatingIfDeactivated"] = &ViewParamsP::updatestopAnimatingIfDeactivated;
        ShowRotationCenter = this->handle->GetBool("ShowRotationCenter", true);
        funcs["ShowRotationCenter"] = &ViewParamsP::updateShowRotationCenter;
        RotationCenterSize = this->handle->GetFloat("RotationCenterSize", 5.0);
        funcs["RotationCenterSize"] = &ViewParamsP::updateRotationCenterSize;
        RotationCenterColor = this->handle->GetUnsigned("RotationCenterColor", 0xFF000033);
        funcs["RotationCenterColor"] = &ViewParamsP::updateRotationCenterColor;
        NewDocumentCameraOrientation = this->handle->GetASCII("NewDocumentCameraOrientation", "Trimetric");
        funcs["NewDocumentCameraOrientation"] = &ViewParamsP::updateNewDocumentCameraOrientation;
        AutoFitToView = this->handle->GetBool("AutoFitToView", true);
        funcs["AutoFitToView"] = &ViewParamsP::updateAutoFitToView;
        ShowNaviCube = this->handle->GetBool("ShowNaviCube", true);
        funcs["ShowNaviCube"] = &ViewParamsP::updateShowNaviCube;
        DisableTouchTilt = this->handle->GetBool("DisableTouchTilt", true);
        funcs["DisableTouchTilt"] = &ViewParamsP::updateDisableTouchTilt;
        NavigationDebug = this->handle->GetBool("NavigationDebug", false);
        funcs["NavigationDebug"] = &ViewParamsP::updateNavigationDebug;
        GestureRollFwdCommand = this->handle->GetASCII("GestureRollFwdCommand", "Std_SelForward");
        funcs["GestureRollFwdCommand"] = &ViewParamsP::updateGestureRollFwdCommand;
        GestureRollBackCommand = this->handle->GetASCII("GestureRollBackCommand", "Std_SelBack");
        funcs["GestureRollBackCommand"] = &ViewParamsP::updateGestureRollBackCommand;
        SaveWBbyTab = this->handle->GetBool("SaveWBbyTab", false);
        funcs["SaveWBbyTab"] = &ViewParamsP::updateSaveWBbyTab;
        CbLabelColor = this->handle->GetUnsigned("CbLabelColor", 0xFFFFFFFF);
        funcs["CbLabelColor"] = &ViewParamsP::updateCbLabelColor;
        CbLabelTextSize = this->handle->GetInt("CbLabelTextSize", 13);
        funcs["CbLabelTextSize"] = &ViewParamsP::updateCbLabelTextSize;
        BoundingBoxFontSize = this->handle->GetFloat("BoundingBoxFontSize", 10.0);
        funcs["BoundingBoxFontSize"] = &ViewParamsP::updateBoundingBoxFontSize;
        DatumPointSize = this->handle->GetFloat("DatumPointSize", 2.5);
        funcs["DatumPointSize"] = &ViewParamsP::updateDatumPointSize;
        LocalCoordinateSystemSize = this->handle->GetFloat("LocalCoordinateSystemSize", 1.0);
        funcs["LocalCoordinateSystemSize"] = &ViewParamsP::updateLocalCoordinateSystemSize;
        DefaultShapeShininess = this->handle->GetInt("DefaultShapeShininess", 37);
        funcs["DefaultShapeShininess"] = &ViewParamsP::updateDefaultShapeShininess;
        DimensionsVisible = this->handle->GetBool("DimensionsVisible", true);
        funcs["DimensionsVisible"] = &ViewParamsP::updateDimensionsVisible;
        Dimensions3dVisible = this->handle->GetBool("Dimensions3dVisible", true);
        funcs["Dimensions3dVisible"] = &ViewParamsP::updateDimensions3dVisible;
        DimensionsDeltaVisible = this->handle->GetBool("DimensionsDeltaVisible", true);
        funcs["DimensionsDeltaVisible"] = &ViewParamsP::updateDimensionsDeltaVisible;
        SavePicture = this->handle->GetASCII("SavePicture", "");
        funcs["SavePicture"] = &ViewParamsP::updateSavePicture;
        HeadlightRotationX = this->handle->GetFloat("HeadlightRotationX", 0.0);
        funcs["HeadlightRotationX"] = &ViewParamsP::updateHeadlightRotationX;
        HeadlightRotationY = this->handle->GetFloat("HeadlightRotationY", 0.0);
        funcs["HeadlightRotationY"] = &ViewParamsP::updateHeadlightRotationY;
        HeadlightRotationZ = this->handle->GetFloat("HeadlightRotationZ", 0.0);
        funcs["HeadlightRotationZ"] = &ViewParamsP::updateHeadlightRotationZ;
        HeadlightRotationW = this->handle->GetFloat("HeadlightRotationW", 1.0);
        funcs["HeadlightRotationW"] = &ViewParamsP::updateHeadlightRotationW;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~ViewParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        ViewParams::onViewParamChanged(sReason);
    }


    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseViewArea(ViewParamsP *self) {
        self->UseViewArea = self->handle->GetBool("UseViewArea", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseNewSelection(ViewParamsP *self) {
        self->UseNewSelection = self->handle->GetBool("UseNewSelection", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseSelectionRoot(ViewParamsP *self) {
        self->UseSelectionRoot = self->handle->GetBool("UseSelectionRoot", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableSelection(ViewParamsP *self) {
        self->EnableSelection = self->handle->GetBool("EnableSelection", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnablePreselection(ViewParamsP *self) {
        self->EnablePreselection = self->handle->GetBool("EnablePreselection", true);
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateRenderCache(ViewParamsP *self) {
        auto v = self->handle->GetInt("RenderCache", 3);
        if (self->RenderCache != v) {
            self->RenderCache = v;
            ViewParams::onRenderCacheChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateUnifiedCanvas(ViewParamsP *self) {
        auto v = self->handle->GetBool("UnifiedCanvas", false);
        if (self->UnifiedCanvas != v) {
            self->UnifiedCanvas = v;
            ViewParams::onUnifiedCanvasChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRandomColor(ViewParamsP *self) {
        self->RandomColor = self->handle->GetBool("RandomColor", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBoundingBoxColor(ViewParamsP *self) {
        self->BoundingBoxColor = self->handle->GetUnsigned("BoundingBoxColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAnnotationTextColor(ViewParamsP *self) {
        self->AnnotationTextColor = self->handle->GetUnsigned("AnnotationTextColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCursorCrosshairColor(ViewParamsP *self) {
        self->CursorCrosshairColor = self->handle->GetUnsigned("CursorCrosshairColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHighlightColor(ViewParamsP *self) {
        self->HighlightColor = self->handle->GetUnsigned("HighlightColor", 0xE1E114FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionColor(ViewParamsP *self) {
        self->SelectionColor = self->handle->GetUnsigned("SelectionColor", 0x1CAD1CFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMarkerSize(ViewParamsP *self) {
        self->MarkerSize = self->handle->GetInt("MarkerSize", 7);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultLinkColor(ViewParamsP *self) {
        self->DefaultLinkColor = self->handle->GetUnsigned("DefaultLinkColor", 0x66FFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultShapeLineColor(ViewParamsP *self) {
        self->DefaultShapeLineColor = self->handle->GetUnsigned("DefaultShapeLineColor", 0x191919FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultShapeVertexColor(ViewParamsP *self) {
        self->DefaultShapeVertexColor = self->handle->GetUnsigned("DefaultShapeVertexColor", 0x191919FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultShapeColor(ViewParamsP *self) {
        self->DefaultShapeColor = self->handle->GetUnsigned("DefaultShapeColor", 0xCCCCE6FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultShapeTransparency(ViewParamsP *self) {
        self->DefaultShapeTransparency = self->handle->GetInt("DefaultShapeTransparency", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultShapeLineWidth(ViewParamsP *self) {
        self->DefaultShapeLineWidth = self->handle->GetInt("DefaultShapeLineWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultShapePointSize(ViewParamsP *self) {
        self->DefaultShapePointSize = self->handle->GetInt("DefaultShapePointSize", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoinCycleCheck(ViewParamsP *self) {
        self->CoinCycleCheck = self->handle->GetBool("CoinCycleCheck", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnablePropertyViewForInactiveDocument(ViewParamsP *self) {
        self->EnablePropertyViewForInactiveDocument = self->handle->GetBool("EnablePropertyViewForInactiveDocument", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowSelectionBoundingBox(ViewParamsP *self) {
        self->ShowSelectionBoundingBox = self->handle->GetBool("ShowSelectionBoundingBox", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowSelectionBoundingBoxThreshold(ViewParamsP *self) {
        self->ShowSelectionBoundingBoxThreshold = self->handle->GetInt("ShowSelectionBoundingBoxThreshold", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUpdateSelectionVisual(ViewParamsP *self) {
        self->UpdateSelectionVisual = self->handle->GetBool("UpdateSelectionVisual", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLinkChildrenDirect(ViewParamsP *self) {
        self->LinkChildrenDirect = self->handle->GetBool("LinkChildrenDirect", true);
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateShowSelectionOnTop(ViewParamsP *self) {
        auto v = self->handle->GetBool("ShowSelectionOnTop", true);
        if (self->ShowSelectionOnTop != v) {
            self->ShowSelectionOnTop = v;
            ViewParams::onShowSelectionOnTopChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowPreSelectedFaceOnTop(ViewParamsP *self) {
        self->ShowPreSelectedFaceOnTop = self->handle->GetBool("ShowPreSelectedFaceOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowPreSelectedFaceOutline(ViewParamsP *self) {
        self->ShowPreSelectedFaceOutline = self->handle->GetBool("ShowPreSelectedFaceOutline", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowSelectedFaceOutline(ViewParamsP *self) {
        self->ShowSelectedFaceOutline = self->handle->GetBool("ShowSelectedFaceOutline", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOutlineThicken(ViewParamsP *self) {
        self->OutlineThicken = self->handle->GetFloat("OutlineThicken", 4);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNoSelFaceHighlightWithOutline(ViewParamsP *self) {
        self->NoSelFaceHighlightWithOutline = self->handle->GetBool("NoSelFaceHighlightWithOutline", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNoPreSelFaceHighlightWithOutline(ViewParamsP *self) {
        self->NoPreSelFaceHighlightWithOutline = self->handle->GetBool("NoPreSelFaceHighlightWithOutline", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoTransparentPick(ViewParamsP *self) {
        self->AutoTransparentPick = self->handle->GetBool("AutoTransparentPick", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectElementOnTop(ViewParamsP *self) {
        self->SelectElementOnTop = self->handle->GetBool("SelectElementOnTop", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransparencyOnTop(ViewParamsP *self) {
        self->TransparencyOnTop = self->handle->GetFloat("TransparencyOnTop", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineSync(ViewParamsP *self) {
        self->HiddenLineSync = self->handle->GetInt("HiddenLineSync", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineSelectionOnTop(ViewParamsP *self) {
        self->HiddenLineSelectionOnTop = self->handle->GetBool("HiddenLineSelectionOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePartialHighlightOnFullSelect(ViewParamsP *self) {
        self->PartialHighlightOnFullSelect = self->handle->GetBool("PartialHighlightOnFullSelect", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionLineThicken(ViewParamsP *self) {
        self->SelectionLineThicken = self->handle->GetFloat("SelectionLineThicken", 1.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionLineMaxWidth(ViewParamsP *self) {
        self->SelectionLineMaxWidth = self->handle->GetFloat("SelectionLineMaxWidth", 4.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionPointScale(ViewParamsP *self) {
        self->SelectionPointScale = self->handle->GetFloat("SelectionPointScale", 2.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionPointMaxSize(ViewParamsP *self) {
        self->SelectionPointMaxSize = self->handle->GetFloat("SelectionPointMaxSize", 6.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePickRadius(ViewParamsP *self) {
        self->PickRadius = self->handle->GetFloat("PickRadius", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTouchLoupeLift(ViewParamsP *self) {
        self->TouchLoupeLift = self->handle->GetFloat("TouchLoupeLift", 28.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionTransparency(ViewParamsP *self) {
        self->SelectionTransparency = self->handle->GetFloat("SelectionTransparency", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionLinePattern(ViewParamsP *self) {
        self->SelectionLinePattern = self->handle->GetInt("SelectionLinePattern", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionLinePatternScale(ViewParamsP *self) {
        self->SelectionLinePatternScale = self->handle->GetInt("SelectionLinePatternScale", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionHiddenLineWidth(ViewParamsP *self) {
        self->SelectionHiddenLineWidth = self->handle->GetFloat("SelectionHiddenLineWidth", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionBBoxLineWidth(ViewParamsP *self) {
        self->SelectionBBoxLineWidth = self->handle->GetFloat("SelectionBBoxLineWidth", 3.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowHighlightEdgeOnly(ViewParamsP *self) {
        self->ShowHighlightEdgeOnly = self->handle->GetBool("ShowHighlightEdgeOnly", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreSelectionDelay(ViewParamsP *self) {
        self->PreSelectionDelay = self->handle->GetFloat("PreSelectionDelay", 0.1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePickBackFaceDelay(ViewParamsP *self) {
        self->PickBackFaceDelay = self->handle->GetInt("PickBackFaceDelay", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseNewRayPick(ViewParamsP *self) {
        self->UseNewRayPick = self->handle->GetBool("UseNewRayPick", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateViewSelectionExtendFactor(ViewParamsP *self) {
        self->ViewSelectionExtendFactor = self->handle->GetFloat("ViewSelectionExtendFactor", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseTightBoundingBox(ViewParamsP *self) {
        self->UseTightBoundingBox = self->handle->GetBool("UseTightBoundingBox", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseBoundingBoxCache(ViewParamsP *self) {
        self->UseBoundingBoxCache = self->handle->GetBool("UseBoundingBoxCache", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderProjectedBBox(ViewParamsP *self) {
        self->RenderProjectedBBox = self->handle->GetBool("RenderProjectedBBox", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionFaceWire(ViewParamsP *self) {
        self->SelectionFaceWire = self->handle->GetBool("SelectionFaceWire", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNewDocumentCameraScale(ViewParamsP *self) {
        self->NewDocumentCameraScale = self->handle->GetFloat("NewDocumentCameraScale", 100.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxOnTopSelections(ViewParamsP *self) {
        self->MaxOnTopSelections = self->handle->GetInt("MaxOnTopSelections", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxViewSelections(ViewParamsP *self) {
        self->MaxViewSelections = self->handle->GetInt("MaxViewSelections", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxSelectionNotification(ViewParamsP *self) {
        self->MaxSelectionNotification = self->handle->GetInt("MaxSelectionNotification", 100);
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateMapChildrenPlacement(ViewParamsP *self) {
        auto v = self->handle->GetBool("MapChildrenPlacement", false);
        if (self->MapChildrenPlacement != v) {
            self->MapChildrenPlacement = v;
            ViewParams::onMapChildrenPlacementChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditingTransparency(ViewParamsP *self) {
        self->EditingTransparency = self->handle->GetFloat("EditingTransparency", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePerViewEdit(ViewParamsP *self) {
        self->PerViewEdit = self->handle->GetBool("PerViewEdit", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDraggerScale(ViewParamsP *self) {
        self->DraggerScale = self->handle->GetFloat("DraggerScale", 0.03);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineTransparency(ViewParamsP *self) {
        self->HiddenLineTransparency = self->handle->GetFloat("HiddenLineTransparency", 0.4);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineOverrideTransparency(ViewParamsP *self) {
        self->HiddenLineOverrideTransparency = self->handle->GetBool("HiddenLineOverrideTransparency", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineFaceColor(ViewParamsP *self) {
        self->HiddenLineFaceColor = self->handle->GetUnsigned("HiddenLineFaceColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineOverrideFaceColor(ViewParamsP *self) {
        self->HiddenLineOverrideFaceColor = self->handle->GetBool("HiddenLineOverrideFaceColor", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineColor(ViewParamsP *self) {
        self->HiddenLineColor = self->handle->GetUnsigned("HiddenLineColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineOverrideColor(ViewParamsP *self) {
        self->HiddenLineOverrideColor = self->handle->GetBool("HiddenLineOverrideColor", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineBackground(ViewParamsP *self) {
        self->HiddenLineBackground = self->handle->GetUnsigned("HiddenLineBackground", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineOverrideBackground(ViewParamsP *self) {
        self->HiddenLineOverrideBackground = self->handle->GetBool("HiddenLineOverrideBackground", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineShaded(ViewParamsP *self) {
        self->HiddenLineShaded = self->handle->GetBool("HiddenLineShaded", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineShowOutline(ViewParamsP *self) {
        self->HiddenLineShowOutline = self->handle->GetBool("HiddenLineShowOutline", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLinePerFaceOutline(ViewParamsP *self) {
        self->HiddenLinePerFaceOutline = self->handle->GetBool("HiddenLinePerFaceOutline", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineSceneOutline(ViewParamsP *self) {
        self->HiddenLineSceneOutline = self->handle->GetBool("HiddenLineSceneOutline", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineOutlineWidth(ViewParamsP *self) {
        self->HiddenLineOutlineWidth = self->handle->GetFloat("HiddenLineOutlineWidth", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineWidth(ViewParamsP *self) {
        self->HiddenLineWidth = self->handle->GetFloat("HiddenLineWidth", 1.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLinePointSize(ViewParamsP *self) {
        self->HiddenLinePointSize = self->handle->GetFloat("HiddenLinePointSize", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineHideSeam(ViewParamsP *self) {
        self->HiddenLineHideSeam = self->handle->GetBool("HiddenLineHideSeam", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineHideVertex(ViewParamsP *self) {
        self->HiddenLineHideVertex = self->handle->GetBool("HiddenLineHideVertex", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLineHideFace(ViewParamsP *self) {
        self->HiddenLineHideFace = self->handle->GetBool("HiddenLineHideFace", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStatusMessageTimeout(ViewParamsP *self) {
        self->StatusMessageTimeout = self->handle->GetInt("StatusMessageTimeout", 5000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowSync(ViewParamsP *self) {
        self->ShadowSync = self->handle->GetInt("ShadowSync", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowFlatLines(ViewParamsP *self) {
        self->ShadowFlatLines = self->handle->GetBool("ShadowFlatLines", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowDisplayMode(ViewParamsP *self) {
        self->ShadowDisplayMode = self->handle->GetInt("ShadowDisplayMode", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowSpotLight(ViewParamsP *self) {
        self->ShadowSpotLight = self->handle->GetBool("ShadowSpotLight", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowLightIntensity(ViewParamsP *self) {
        self->ShadowLightIntensity = self->handle->GetFloat("ShadowLightIntensity", 0.8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowLightDirectionX(ViewParamsP *self) {
        self->ShadowLightDirectionX = self->handle->GetFloat("ShadowLightDirectionX", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowLightDirectionY(ViewParamsP *self) {
        self->ShadowLightDirectionY = self->handle->GetFloat("ShadowLightDirectionY", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowLightDirectionZ(ViewParamsP *self) {
        self->ShadowLightDirectionZ = self->handle->GetFloat("ShadowLightDirectionZ", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowLightColor(ViewParamsP *self) {
        self->ShadowLightColor = self->handle->GetUnsigned("ShadowLightColor", 0xF0FDFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowShowGround(ViewParamsP *self) {
        self->ShadowShowGround = self->handle->GetBool("ShadowShowGround", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowGroundBackFaceCull(ViewParamsP *self) {
        self->ShadowGroundBackFaceCull = self->handle->GetBool("ShadowGroundBackFaceCull", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowGroundScale(ViewParamsP *self) {
        self->ShadowGroundScale = self->handle->GetFloat("ShadowGroundScale", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowGroundColor(ViewParamsP *self) {
        self->ShadowGroundColor = self->handle->GetUnsigned("ShadowGroundColor", 0x7D7D7DFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowGroundBumpMap(ViewParamsP *self) {
        self->ShadowGroundBumpMap = self->handle->GetASCII("ShadowGroundBumpMap", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowGroundTexture(ViewParamsP *self) {
        self->ShadowGroundTexture = self->handle->GetASCII("ShadowGroundTexture", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowGroundTextureSize(ViewParamsP *self) {
        self->ShadowGroundTextureSize = self->handle->GetFloat("ShadowGroundTextureSize", 100.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowTransparency(ViewParamsP *self) {
        self->ShadowTransparency = self->handle->GetFloat("ShadowTransparency", 0.2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowGroundTransparency(ViewParamsP *self) {
        self->ShadowGroundTransparency = self->handle->GetFloat("ShadowGroundTransparency", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowGroundShading(ViewParamsP *self) {
        self->ShadowGroundShading = self->handle->GetBool("ShadowGroundShading", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowExtraRedraw(ViewParamsP *self) {
        self->ShadowExtraRedraw = self->handle->GetBool("ShadowExtraRedraw", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowSmoothBorder(ViewParamsP *self) {
        self->ShadowSmoothBorder = self->handle->GetInt("ShadowSmoothBorder", 40);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowSpreadSize(ViewParamsP *self) {
        self->ShadowSpreadSize = self->handle->GetInt("ShadowSpreadSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowSpreadSampleSize(ViewParamsP *self) {
        self->ShadowSpreadSampleSize = self->handle->GetInt("ShadowSpreadSampleSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowPrecision(ViewParamsP *self) {
        self->ShadowPrecision = self->handle->GetFloat("ShadowPrecision", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowEpsilon(ViewParamsP *self) {
        self->ShadowEpsilon = self->handle->GetFloat("ShadowEpsilon", 1e-05);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowEpsilonMinimum(ViewParamsP *self) {
        self->ShadowEpsilonMinimum = self->handle->GetFloat("ShadowEpsilonMinimum", 1e-06);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowThreshold(ViewParamsP *self) {
        self->ShadowThreshold = self->handle->GetFloat("ShadowThreshold", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowBoundBoxScale(ViewParamsP *self) {
        self->ShadowBoundBoxScale = self->handle->GetFloat("ShadowBoundBoxScale", 1.2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowMaxDistance(ViewParamsP *self) {
        self->ShadowMaxDistance = self->handle->GetFloat("ShadowMaxDistance", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowTransparentShadow(ViewParamsP *self) {
        self->ShadowTransparentShadow = self->handle->GetBool("ShadowTransparentShadow", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadowUpdateGround(ViewParamsP *self) {
        self->ShadowUpdateGround = self->handle->GetBool("ShadowUpdateGround", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewTimer(ViewParamsP *self) {
        self->PropertyViewTimer = self->handle->GetUnsigned("PropertyViewTimer", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHierarchyAscend(ViewParamsP *self) {
        self->HierarchyAscend = self->handle->GetBool("HierarchyAscend", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCommandHistorySize(ViewParamsP *self) {
        self->CommandHistorySize = self->handle->GetInt("CommandHistorySize", 20);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuIconSize(ViewParamsP *self) {
        self->PieMenuIconSize = self->handle->GetInt("PieMenuIconSize", 24);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuRadius(ViewParamsP *self) {
        self->PieMenuRadius = self->handle->GetInt("PieMenuRadius", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuTriggerRadius(ViewParamsP *self) {
        self->PieMenuTriggerRadius = self->handle->GetInt("PieMenuTriggerRadius", 60);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuFontSize(ViewParamsP *self) {
        self->PieMenuFontSize = self->handle->GetInt("PieMenuFontSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuTriggerDelay(ViewParamsP *self) {
        self->PieMenuTriggerDelay = self->handle->GetInt("PieMenuTriggerDelay", 200);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuTriggerAction(ViewParamsP *self) {
        self->PieMenuTriggerAction = self->handle->GetBool("PieMenuTriggerAction", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuAnimationDuration(ViewParamsP *self) {
        self->PieMenuAnimationDuration = self->handle->GetInt("PieMenuAnimationDuration", 250);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuAnimationCurve(ViewParamsP *self) {
        self->PieMenuAnimationCurve = self->handle->GetInt("PieMenuAnimationCurve", 38);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuCenterRadius(ViewParamsP *self) {
        self->PieMenuCenterRadius = self->handle->GetInt("PieMenuCenterRadius", 10);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePieMenuPopup(ViewParamsP *self) {
        self->PieMenuPopup = self->handle->GetBool("PieMenuPopup", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStickyTaskControl(ViewParamsP *self) {
        self->StickyTaskControl = self->handle->GetBool("StickyTaskControl", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateColorOnTop(ViewParamsP *self) {
        self->ColorOnTop = self->handle->GetBool("ColorOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoSortWBList(ViewParamsP *self) {
        self->AutoSortWBList = self->handle->GetBool("AutoSortWBList", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxCameraAnimatePeriod(ViewParamsP *self) {
        self->MaxCameraAnimatePeriod = self->handle->GetInt("MaxCameraAnimatePeriod", 3000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTaskNoWheelFocus(ViewParamsP *self) {
        self->TaskNoWheelFocus = self->handle->GetBool("TaskNoWheelFocus", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGestureLongPressRotationCenter(ViewParamsP *self) {
        self->GestureLongPressRotationCenter = self->handle->GetBool("GestureLongPressRotationCenter", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckWidgetPlacementOnRestore(ViewParamsP *self) {
        self->CheckWidgetPlacementOnRestore = self->handle->GetBool("CheckWidgetPlacementOnRestore", true);
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateTextCursorWidth(ViewParamsP *self) {
        auto v = self->handle->GetInt("TextCursorWidth", 1);
        if (self->TextCursorWidth != v) {
            self->TextCursorWidth = v;
            ViewParams::onTextCursorWidthChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreselectionToolTipCorner(ViewParamsP *self) {
        self->PreselectionToolTipCorner = self->handle->GetInt("PreselectionToolTipCorner", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreselectionToolTipOffsetX(ViewParamsP *self) {
        self->PreselectionToolTipOffsetX = self->handle->GetInt("PreselectionToolTipOffsetX", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreselectionToolTipOffsetY(ViewParamsP *self) {
        self->PreselectionToolTipOffsetY = self->handle->GetInt("PreselectionToolTipOffsetY", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreselectionToolTipFontSize(ViewParamsP *self) {
        self->PreselectionToolTipFontSize = self->handle->GetInt("PreselectionToolTipFontSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionFill(ViewParamsP *self) {
        self->SectionFill = self->handle->GetBool("SectionFill", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionFillInvert(ViewParamsP *self) {
        self->SectionFillInvert = self->handle->GetBool("SectionFillInvert", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionConcave(ViewParamsP *self) {
        self->SectionConcave = self->handle->GetBool("SectionConcave", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNoSectionOnTop(ViewParamsP *self) {
        self->NoSectionOnTop = self->handle->GetBool("NoSectionOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionHatchTextureScale(ViewParamsP *self) {
        self->SectionHatchTextureScale = self->handle->GetFloat("SectionHatchTextureScale", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateSectionHatchTexture(ViewParamsP *self) {
        auto v = self->handle->GetASCII("SectionHatchTexture", ":icons/section-hatch.png");
        if (self->SectionHatchTexture != v) {
            self->SectionHatchTexture = v;
            ViewParams::onSectionHatchTextureChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionHatchTextureEnable(ViewParamsP *self) {
        self->SectionHatchTextureEnable = self->handle->GetBool("SectionHatchTextureEnable", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionFillGroup(ViewParamsP *self) {
        self->SectionFillGroup = self->handle->GetBool("SectionFillGroup", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowClipPlane(ViewParamsP *self) {
        self->ShowClipPlane = self->handle->GetBool("ShowClipPlane", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateClipPlaneSize(ViewParamsP *self) {
        self->ClipPlaneSize = self->handle->GetFloat("ClipPlaneSize", 40.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateClipPlaneColor(ViewParamsP *self) {
        self->ClipPlaneColor = self->handle->GetASCII("ClipPlaneColor", "cyan");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateClipPlaneLineWidth(ViewParamsP *self) {
        self->ClipPlaneLineWidth = self->handle->GetFloat("ClipPlaneLineWidth", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransformOnTop(ViewParamsP *self) {
        self->TransformOnTop = self->handle->GetBool("TransformOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionColorDifference(ViewParamsP *self) {
        self->SelectionColorDifference = self->handle->GetFloat("SelectionColorDifference", 25.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderCacheMergeCount(ViewParamsP *self) {
        self->RenderCacheMergeCount = self->handle->GetInt("RenderCacheMergeCount", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderCacheMergeCountMin(ViewParamsP *self) {
        self->RenderCacheMergeCountMin = self->handle->GetInt("RenderCacheMergeCountMin", 10);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderCacheMergeCountMax(ViewParamsP *self) {
        self->RenderCacheMergeCountMax = self->handle->GetInt("RenderCacheMergeCountMax", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderCacheMergeDepthMax(ViewParamsP *self) {
        self->RenderCacheMergeDepthMax = self->handle->GetInt("RenderCacheMergeDepthMax", -1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderCacheMergeDepthMin(ViewParamsP *self) {
        self->RenderCacheMergeDepthMin = self->handle->GetInt("RenderCacheMergeDepthMin", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderCacheKeepMax(ViewParamsP *self) {
        self->RenderCacheKeepMax = self->handle->GetInt("RenderCacheKeepMax", 32);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderCacheIncremental(ViewParamsP *self) {
        self->RenderCacheIncremental = self->handle->GetInt("RenderCacheIncremental", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderCacheMeshReuse(ViewParamsP *self) {
        self->RenderCacheMeshReuse = self->handle->GetInt("RenderCacheMeshReuse", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLiveImportRedrawInterval(ViewParamsP *self) {
        self->LiveImportRedrawInterval = self->handle->GetInt("LiveImportRedrawInterval", 200);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLiveImportRedrawBudget(ViewParamsP *self) {
        self->LiveImportRedrawBudget = self->handle->GetInt("LiveImportRedrawBudget", 10);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLiveImportPumpInterval(ViewParamsP *self) {
        self->LiveImportPumpInterval = self->handle->GetInt("LiveImportPumpInterval", 50);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderHighlightPolygonOffsetFactor(ViewParamsP *self) {
        self->RenderHighlightPolygonOffsetFactor = self->handle->GetFloat("RenderHighlightPolygonOffsetFactor", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRenderHighlightPolygonOffsetUnits(ViewParamsP *self) {
        self->RenderHighlightPolygonOffsetUnits = self->handle->GetFloat("RenderHighlightPolygonOffsetUnits", 1);
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateForceSolidSingleSideLighting(ViewParamsP *self) {
        auto v = self->handle->GetBool("ForceSolidSingleSideLighting", true);
        if (self->ForceSolidSingleSideLighting != v) {
            self->ForceSolidSingleSideLighting = v;
            ViewParams::onForceSolidSingleSideLightingChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateDefaultFontSize(ViewParamsP *self) {
        auto v = self->handle->GetInt("DefaultFontSize", 0);
        if (self->DefaultFontSize != v) {
            self->DefaultFontSize = v;
            ViewParams::onDefaultFontSizeChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateEnableTaskPanelKeyTranslate(ViewParamsP *self) {
        auto v = self->handle->GetBool("EnableTaskPanelKeyTranslate", false);
        if (self->EnableTaskPanelKeyTranslate != v) {
            self->EnableTaskPanelKeyTranslate = v;
            ViewParams::onEnableTaskPanelKeyTranslateChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableMenuBarCheckBox(ViewParamsP *self) {
        self->EnableMenuBarCheckBox = self->handle->GetBool("EnableMenuBarCheckBox", FC_ENABLE_MENUBAR_CHECKBOX);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableBacklight(ViewParamsP *self) {
        self->EnableBacklight = self->handle->GetBool("EnableBacklight", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBacklightColor(ViewParamsP *self) {
        self->BacklightColor = self->handle->GetUnsigned("BacklightColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBacklightIntensity(ViewParamsP *self) {
        self->BacklightIntensity = self->handle->GetInt("BacklightIntensity", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverrideSelectability(ViewParamsP *self) {
        self->OverrideSelectability = self->handle->GetBool("OverrideSelectability", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionStackSize(ViewParamsP *self) {
        self->SelectionStackSize = self->handle->GetUnsigned("SelectionStackSize", 30);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultDrawStyle(ViewParamsP *self) {
        self->DefaultDrawStyle = self->handle->GetInt("DefaultDrawStyle", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateToolTipIconSize(ViewParamsP *self) {
        self->ToolTipIconSize = self->handle->GetInt("ToolTipIconSize", 64);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateToolTipDisable(ViewParamsP *self) {
        self->ToolTipDisable = self->handle->GetBool("ToolTipDisable", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisXColor(ViewParamsP *self) {
        self->AxisXColor = self->handle->GetUnsigned("AxisXColor", 0xCC333300);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisYColor(ViewParamsP *self) {
        self->AxisYColor = self->handle->GetUnsigned("AxisYColor", 0x33CC3300);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAxisZColor(ViewParamsP *self) {
        self->AxisZColor = self->handle->GetUnsigned("AxisZColor", 0x3333CC00);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDatumScreenSize(ViewParamsP *self) {
        self->DatumScreenSize = self->handle->GetBool("DatumScreenSize", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDatumScale(ViewParamsP *self) {
        self->DatumScale = self->handle->GetFloat("DatumScale", 100.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDatumPlaneSize(ViewParamsP *self) {
        self->DatumPlaneSize = self->handle->GetFloat("DatumPlaneSize", 62.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDatumLineSize(ViewParamsP *self) {
        self->DatumLineSize = self->handle->GetFloat("DatumLineSize", 70.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDatumTemporaryScaleFactor(ViewParamsP *self) {
        self->DatumTemporaryScaleFactor = self->handle->GetFloat("DatumTemporaryScaleFactor", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEyeDistance(ViewParamsP *self) {
        self->EyeDistance = self->handle->GetFloat("EyeDistance", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCornerCoordSystem(ViewParamsP *self) {
        self->CornerCoordSystem = self->handle->GetBool("CornerCoordSystem", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCornerCoordSystemSize(ViewParamsP *self) {
        self->CornerCoordSystemSize = self->handle->GetInt("CornerCoordSystemSize", 10);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowAxisCross(ViewParamsP *self) {
        self->ShowAxisCross = self->handle->GetBool("ShowAxisCross", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowFPS(ViewParamsP *self) {
        self->ShowFPS = self->handle->GetBool("ShowFPS", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseVBO(ViewParamsP *self) {
        self->UseVBO = self->handle->GetBool("UseVBO", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOrthographic(ViewParamsP *self) {
        self->Orthographic = self->handle->GetBool("Orthographic", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePerspective(ViewParamsP *self) {
        self->Perspective = self->handle->GetBool("Perspective", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateApplyCameraTypeToAll(ViewParamsP *self) {
        self->ApplyCameraTypeToAll = self->handle->GetBool("ApplyCameraTypeToAll", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAntiAliasing(ViewParamsP *self) {
        self->AntiAliasing = self->handle->GetInt("AntiAliasing", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransparentObjectRenderType(ViewParamsP *self) {
        self->TransparentObjectRenderType = self->handle->GetInt("TransparentObjectRenderType", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInternalTextureFormat(ViewParamsP *self) {
        self->InternalTextureFormat = self->handle->GetASCII("InternalTextureFormat", "Default");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGradient(ViewParamsP *self) {
        self->Gradient = self->handle->GetBool("Gradient", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRadialGradient(ViewParamsP *self) {
        self->RadialGradient = self->handle->GetBool("RadialGradient", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSimple(ViewParamsP *self) {
        self->Simple = self->handle->GetBool("Simple", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBackgroundColor(ViewParamsP *self) {
        self->BackgroundColor = self->handle->GetUnsigned("BackgroundColor", 0xEAE5DCFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBackgroundColor2(ViewParamsP *self) {
        self->BackgroundColor2 = self->handle->GetUnsigned("BackgroundColor2", 0x333365FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBackgroundColor3(ViewParamsP *self) {
        self->BackgroundColor3 = self->handle->GetUnsigned("BackgroundColor3", 0xABABC1FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBackgroundColor4(ViewParamsP *self) {
        self->BackgroundColor4 = self->handle->GetUnsigned("BackgroundColor4", 0x6F6F93FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseBackgroundColorMid(ViewParamsP *self) {
        self->UseBackgroundColorMid = self->handle->GetBool("UseBackgroundColorMid", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableHeadlight(ViewParamsP *self) {
        self->EnableHeadlight = self->handle->GetBool("EnableHeadlight", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHeadlightColor(ViewParamsP *self) {
        self->HeadlightColor = self->handle->GetUnsigned("HeadlightColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHeadlightIntensity(ViewParamsP *self) {
        self->HeadlightIntensity = self->handle->GetInt("HeadlightIntensity", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHeadlightDirection(ViewParamsP *self) {
        self->HeadlightDirection = self->handle->GetASCII("HeadlightDirection", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBacklightDirection(ViewParamsP *self) {
        self->BacklightDirection = self->handle->GetASCII("BacklightDirection", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableFillLight(ViewParamsP *self) {
        self->EnableFillLight = self->handle->GetBool("EnableFillLight", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFillLightColor(ViewParamsP *self) {
        self->FillLightColor = self->handle->GetUnsigned("FillLightColor", 0xE6FAFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFillLightIntensity(ViewParamsP *self) {
        self->FillLightIntensity = self->handle->GetInt("FillLightIntensity", 60);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFillLightDirection(ViewParamsP *self) {
        self->FillLightDirection = self->handle->GetASCII("FillLightDirection", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAmbientLightColor(ViewParamsP *self) {
        self->AmbientLightColor = self->handle->GetUnsigned("AmbientLightColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAmbientLightIntensity(ViewParamsP *self) {
        self->AmbientLightIntensity = self->handle->GetInt("AmbientLightIntensity", 20);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNavigationStyle(ViewParamsP *self) {
        self->NavigationStyle = self->handle->GetASCII("NavigationStyle", "Gui::CADNavigationStyle");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSameStyleForAllViews(ViewParamsP *self) {
        self->SameStyleForAllViews = self->handle->GetBool("SameStyleForAllViews", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOrbitStyle(ViewParamsP *self) {
        self->OrbitStyle = self->handle->GetInt("OrbitStyle", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRotationMode(ViewParamsP *self) {
        self->RotationMode = self->handle->GetInt("RotationMode", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSensitivity(ViewParamsP *self) {
        self->Sensitivity = self->handle->GetFloat("Sensitivity", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateResetCursorPosition(ViewParamsP *self) {
        self->ResetCursorPosition = self->handle->GetBool("ResetCursorPosition", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInvertZoom(ViewParamsP *self) {
        self->InvertZoom = self->handle->GetBool("InvertZoom", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateZoomAtCursor(ViewParamsP *self) {
        self->ZoomAtCursor = self->handle->GetBool("ZoomAtCursor", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateZoomStep(ViewParamsP *self) {
        self->ZoomStep = self->handle->GetFloat("ZoomStep", 0.2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseNavigationAnimations(ViewParamsP *self) {
        self->UseNavigationAnimations = self->handle->GetBool("UseNavigationAnimations", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseSpinningAnimations(ViewParamsP *self) {
        self->UseSpinningAnimations = self->handle->GetBool("UseSpinningAnimations", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAnimationDuration(ViewParamsP *self) {
        self->AnimationDuration = self->handle->GetInt("AnimationDuration", 250);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatestopAnimatingIfDeactivated(ViewParamsP *self) {
        self->stopAnimatingIfDeactivated = self->handle->GetInt("stopAnimatingIfDeactivated", 3000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowRotationCenter(ViewParamsP *self) {
        self->ShowRotationCenter = self->handle->GetBool("ShowRotationCenter", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRotationCenterSize(ViewParamsP *self) {
        self->RotationCenterSize = self->handle->GetFloat("RotationCenterSize", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRotationCenterColor(ViewParamsP *self) {
        self->RotationCenterColor = self->handle->GetUnsigned("RotationCenterColor", 0xFF000033);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNewDocumentCameraOrientation(ViewParamsP *self) {
        self->NewDocumentCameraOrientation = self->handle->GetASCII("NewDocumentCameraOrientation", "Trimetric");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoFitToView(ViewParamsP *self) {
        self->AutoFitToView = self->handle->GetBool("AutoFitToView", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowNaviCube(ViewParamsP *self) {
        self->ShowNaviCube = self->handle->GetBool("ShowNaviCube", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDisableTouchTilt(ViewParamsP *self) {
        self->DisableTouchTilt = self->handle->GetBool("DisableTouchTilt", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNavigationDebug(ViewParamsP *self) {
        self->NavigationDebug = self->handle->GetBool("NavigationDebug", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGestureRollFwdCommand(ViewParamsP *self) {
        self->GestureRollFwdCommand = self->handle->GetASCII("GestureRollFwdCommand", "Std_SelForward");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGestureRollBackCommand(ViewParamsP *self) {
        self->GestureRollBackCommand = self->handle->GetASCII("GestureRollBackCommand", "Std_SelBack");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSaveWBbyTab(ViewParamsP *self) {
        self->SaveWBbyTab = self->handle->GetBool("SaveWBbyTab", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCbLabelColor(ViewParamsP *self) {
        self->CbLabelColor = self->handle->GetUnsigned("CbLabelColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCbLabelTextSize(ViewParamsP *self) {
        self->CbLabelTextSize = self->handle->GetInt("CbLabelTextSize", 13);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBoundingBoxFontSize(ViewParamsP *self) {
        self->BoundingBoxFontSize = self->handle->GetFloat("BoundingBoxFontSize", 10.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDatumPointSize(ViewParamsP *self) {
        self->DatumPointSize = self->handle->GetFloat("DatumPointSize", 2.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLocalCoordinateSystemSize(ViewParamsP *self) {
        self->LocalCoordinateSystemSize = self->handle->GetFloat("LocalCoordinateSystemSize", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultShapeShininess(ViewParamsP *self) {
        self->DefaultShapeShininess = self->handle->GetInt("DefaultShapeShininess", 37);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensionsVisible(ViewParamsP *self) {
        self->DimensionsVisible = self->handle->GetBool("DimensionsVisible", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensions3dVisible(ViewParamsP *self) {
        self->Dimensions3dVisible = self->handle->GetBool("Dimensions3dVisible", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensionsDeltaVisible(ViewParamsP *self) {
        self->DimensionsDeltaVisible = self->handle->GetBool("DimensionsDeltaVisible", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSavePicture(ViewParamsP *self) {
        self->SavePicture = self->handle->GetASCII("SavePicture", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHeadlightRotationX(ViewParamsP *self) {
        self->HeadlightRotationX = self->handle->GetFloat("HeadlightRotationX", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHeadlightRotationY(ViewParamsP *self) {
        self->HeadlightRotationY = self->handle->GetFloat("HeadlightRotationY", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHeadlightRotationZ(ViewParamsP *self) {
        self->HeadlightRotationZ = self->handle->GetFloat("HeadlightRotationZ", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHeadlightRotationW(ViewParamsP *self) {
        self->HeadlightRotationW = self->handle->GetFloat("HeadlightRotationW", 1.0);
    }
};

// Auto generated code (Tools/params_utils.py:336)
ViewParamsP *instance() {
    static ViewParamsP *inst = new ViewParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _ViewParamsRegistrar({
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseViewArea", "UseViewArea", App::ParamInfo::Bool, true)
        .setTitle("Tile views inside one tab")
        .setDoc("Host views in a split-capable view area, so that several views\n"
"can share one tab side by side. Off, every view gets its own tab\n"
"and the split placement choices below do not apply."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseNewSelection", "UseNewSelection", App::ParamInfo::Bool, true)
        .setTitle("Use New Selection")
        .setDoc("Highlight selection and preselection through the selection root of\n"
"the 3D view, which allows picking sub-elements and objects inside\n"
"links. When off, only objects that ask for it are handled that way."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseSelectionRoot", "UseSelectionRoot", App::ParamInfo::Bool, true)
        .setTitle("Use Selection Root")
        .setDoc("Give the visual copy a link makes of its linked object a selection\n"
"root of its own, so that each link is highlighted separately. When\n"
"off, a plain group without render caching is used."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EnableSelection", "EnableSelection", App::ParamInfo::Bool, true)
        .setTitle("Enable selection")
        .setDoc("Enable selection, highlighted with specified color"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EnablePreselection", "EnablePreselection", App::ParamInfo::Bool, true)
        .setTitle("Enable preselection")
        .setDoc("Enable preselection, highlighted with specified color"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderCache", "RenderCache", App::ParamInfo::Int, 3)
        .setTitle("Render Cache")
        .setDoc("Which render path draws a 3D view: 0 auto, 1 distributed,\n"
"2 centralized Coin caching, 3 the render cache that feeds the\n"
"render engine. Looked at only under the render type 'Legacy'\n"
"(View/Render/Type): with the render engine the program draws\n"
"by 3 whatever this holds. Not on a preferences page.")
        .setOnChange(),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UnifiedCanvas", "UnifiedCanvas", App::ParamInfo::Bool, false)
        .setTitle("Unified split-view canvas")
        .setDoc("Draw all the 3D cells of a split view (ViewArea) into ONE\n"
"canvas widget, as sub-views of a single render backend, instead\n"
"of composing each cell's own widget. One backend instance and\n"
"one copy of the GPU scene serve every cell (the browser tier's\n"
"model). Experimental; needs the render engine (render cache\n"
"mode 3). See docs/SplitViews.md sec 13.")
        .setOnChange(),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RandomColor", "RandomColor", App::ParamInfo::Bool, false)
        .setTitle("Random Color")
        .setDoc("Give every new object a random shape colour instead of the default\n"
"shape colour."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "BoundingBoxColor", "BoundingBoxColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Bounding Box Color")
        .setDoc("Colour of the bounding box drawn for an object that has its bounding\n"
"box display turned on."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AnnotationTextColor", "AnnotationTextColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Annotation Text Color")
        .setDoc("Default text colour of new annotation objects."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "CursorCrosshairColor", "CursorCrosshairColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Crosshair cursor colour")
        .setDoc("Colour of the crosshair the drawing tools of the Sketcher put in\n"
"place of the mouse pointer. The Sketcher's Appearance page shows\n"
"it. Takes effect at the next tool started.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HighlightColor", "HighlightColor", App::ParamInfo::Hex, 0xE1E114FF)
        .setTitle("Pre-selection highlight color")
        .setDoc("Pre-selection highlight color")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionColor", "SelectionColor", App::ParamInfo::Hex, 0x1CAD1CFF)
        .setTitle("Selection highlight color")
        .setDoc("Selection highlight color")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "MarkerSize", "MarkerSize", App::ParamInfo::Int, 7)
        .setTitle("Marker Size")
        .setDoc("Size in pixels of the point markers drawn in the 3D view, such as\n"
"sketch vertices and the end points of a measurement."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultLinkColor", "DefaultLinkColor", App::ParamInfo::Hex, 0x66FFFFFF)
        .setTitle("Default Link Color")
        .setDoc("Default colour of the material of a new link, which is used when the\n"
"link overrides the material of its linked object."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultShapeLineColor", "DefaultShapeLineColor", App::ParamInfo::Hex, 0x191919FF)
        .setTitle("Default Shape Line Color")
        .setDoc("Default line colour of new shapes."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultShapeVertexColor", "DefaultShapeVertexColor", App::ParamInfo::Hex, 0x191919FF)
        .setTitle("Default Shape Vertex Color")
        .setDoc("Default vertex colour of new shapes."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultShapeColor", "DefaultShapeColor", App::ParamInfo::Hex, 0xCCCCE6FF)
        .setTitle("Default Shape Color")
        .setDoc("Default face colour of new shapes. Not used while random colours are\n"
"turned on."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultShapeTransparency", "DefaultShapeTransparency", App::ParamInfo::Int, 0)
        .setTitle("Default Shape Transparency")
        .setDoc("Default transparency of new shapes in percent. 0 is opaque, 100 is\n"
"fully transparent."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultShapeLineWidth", "DefaultShapeLineWidth", App::ParamInfo::Int, 2)
        .setTitle("Default Shape Line Width")
        .setDoc("Default line width of new shapes, in pixels."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultShapePointSize", "DefaultShapePointSize", App::ParamInfo::Int, 2)
        .setTitle("Default Shape Point Size")
        .setDoc("Default vertex size of new shapes, in pixels."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "CoinCycleCheck", "CoinCycleCheck", App::ParamInfo::Bool, true)
        .setTitle("Coin Cycle Check")
        .setDoc("Check the 3D scene for an object that contains itself while the\n"
"scene is traversed. A cycle is reported and skipped instead of being\n"
"followed without end."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EnablePropertyViewForInactiveDocument", "EnablePropertyViewForInactiveDocument", App::ParamInfo::Bool, true)
        .setTitle("Enable Property View For Inactive Document")
        .setDoc("Keep the property view usable when the selected objects belong to a\n"
"document other than the active one. When off, the property view is\n"
"disabled for such a selection."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowSelectionBoundingBox", "ShowSelectionBoundingBox", App::ParamInfo::Bool, false)
        .setTitle("Show selection bounding box instead of highlight")
        .setDoc("Show selection bounding box instead of highlight"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowSelectionBoundingBoxThreshold", "ShowSelectionBoundingBoxThreshold", App::ParamInfo::Int, 0)
        .setTitle("Threshold for showing bounding box instead of selection highlight")
        .setDoc("Threshold for showing bounding box instead of selection highlight"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UpdateSelectionVisual", "UpdateSelectionVisual", App::ParamInfo::Bool, true)
        .setTitle("Update Selection Visual")
        .setDoc("Bring back the selection highlight of a selected object when it is\n"
"shown again after being hidden."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "LinkChildrenDirect", "LinkChildrenDirect", App::ParamInfo::Bool, true)
        .setTitle("Link Children Direct")
        .setDoc("Show the children of a group with its own coordinate system, such as\n"
"a part or body, through a link view. A link to the group then shows\n"
"the children's visuals directly."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowSelectionOnTop", "ShowSelectionOnTop", App::ParamInfo::Bool, true)
        .setTitle("Show selection always on top")
        .setDoc("Show selection always on top")
        .setOnChange(),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowPreSelectedFaceOnTop", "ShowPreSelectedFaceOnTop", App::ParamInfo::Bool, true)
        .setTitle("Show pre-selected face always on top")
        .setDoc("Show pre-selected face always on top"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowPreSelectedFaceOutline", "ShowPreSelectedFaceOutline", App::ParamInfo::Bool, true)
        .setTitle("Show pre-selected face outline")
        .setDoc("Show pre-selected face outline"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowSelectedFaceOutline", "ShowSelectedFaceOutline", App::ParamInfo::Bool, true)
        .setTitle("Show selected face outline")
        .setDoc("Show selected face outline"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "OutlineThicken", "OutlineThicken", App::ParamInfo::Float, 4)
        .setTitle("Outline width multiplier")
        .setDoc("Muplication factor to increase outline width of the selected face."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "NoSelFaceHighlightWithOutline", "NoSelFaceHighlightWithOutline", App::ParamInfo::Bool, false)
        .setTitle("No face selection highlight with outline")
        .setDoc("Do not highlight selected face if outline is enabled"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "NoPreSelFaceHighlightWithOutline", "NoPreSelFaceHighlightWithOutline", App::ParamInfo::Bool, true)
        .setTitle("No face pre-selection highlight with outline")
        .setDoc("Do not highlight pre-selected face if outline is enabled"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AutoTransparentPick", "AutoTransparentPick", App::ParamInfo::Bool, false)
        .setTitle("Make pre-selected object transparent for picking hidden lines")
        .setDoc("Make pre-selected object transparent for picking hidden lines"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectElementOnTop", "SelectElementOnTop", App::ParamInfo::Bool, false)
        .setTitle("Do box/lasso element selection on already selected objects if SelectionOnTop is enabled.")
        .setDoc("Do box/lasso element selection on already selected objects if SelectionOnTop is enabled."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "TransparencyOnTop", "TransparencyOnTop", App::ParamInfo::Float, 0.5)
        .setTitle("Transparency")
        .setDoc("Transparency for the selected object when being shown on top."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineSync", "HiddenLineSync", App::ParamInfo::Int, 1)
        .setTitle("Synchronize")
        .setDoc("Specifies how to sync hidden line display style settings to opened document")
        .setProxy("ComboBox")
        .setItems({{"None", "No change to opened document", nullptr}, {"Apply to active view", "Auto apply changed setting to the current active view", nullptr}, {"Apply to active document", "Auto apply changed setting to all views of the current active document", nullptr}, {"Apply to all open documents", "Auto apply changed setting to all opened documents", nullptr}}, false, true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineSelectionOnTop", "HiddenLineSelectionOnTop", App::ParamInfo::Bool, true)
        .setTitle("Enable hidden line/point selection when SelectionOnTop is active.")
        .setDoc("Enable hidden line/point selection when SelectionOnTop is active."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PartialHighlightOnFullSelect", "PartialHighlightOnFullSelect", App::ParamInfo::Bool, false)
        .setTitle("Enable partial highlight on full selection for object that supports it.")
        .setDoc("Enable partial highlight on full selection for object that supports it."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionLineThicken", "SelectionLineThicken", App::ParamInfo::Float, 1.5)
        .setTitle("Line width multiplier")
        .setDoc("Muplication factor to increase the width of the selected line."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionLineMaxWidth", "SelectionLineMaxWidth", App::ParamInfo::Float, 4.0)
        .setTitle("Maximum line width")
        .setDoc("Limit the selected line width when applying line thickening."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionPointScale", "SelectionPointScale", App::ParamInfo::Float, 2.5)
        .setTitle("Point size multiplier")
        .setDoc("Muplication factor to increase the size of the selected point.\n"
"If zero, then use line multiplication factor."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionPointMaxSize", "SelectionPointMaxSize", App::ParamInfo::Float, 6.0)
        .setTitle("Maximum point size")
        .setDoc("Limit the selected point size when applying size scale."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PickRadius", "PickRadius", App::ParamInfo::Float, 5.0)
        .setTitle("Pick radius (px)")
        .setDoc("Area for picking elements in 3D view. Larger value make it easy to pick things,\n"
"but can also make small features impossible to select.")
        .setProxy("SpinBox")
        .setRange(0.5, 200.0, 1.0, 1),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "TouchLoupeLift", "TouchLoupeLift", App::ParamInfo::Float, 28.0)
        .setTitle("Touch loupe lift (px)")
        .setDoc("How far above the fingertip the touch loupe picks, in CSS pixels.\n"
"The pick ring and its centre dot sit this far above the contact\n"
"point so the finger never covers what it is aiming at.")
        .setProxy("SpinBox")
        .setRange(0.0, 200.0, 1.0, 1),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionTransparency", "SelectionTransparency", App::ParamInfo::Float, 0.5)
        .setTitle("Selection Transparency")
        .setDoc("Transparency given to a selected face so that what lies behind it\n"
"stays visible, used when picking through objects and when the\n"
"highlight is drawn on top. 0 is opaque, 1 is invisible."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionLinePattern", "SelectionLinePattern", App::ParamInfo::Int, 0)
        .setTitle("Selected hidden line pattern")
        .setDoc("Dash pattern of the hidden part of a selected line that is shown on\n"
"top of the scene, as a 16 bit mask. 0 draws it solid.")
        .setProxy("LinePattern"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionLinePatternScale", "SelectionLinePatternScale", App::ParamInfo::Int, 1)
        .setTitle("Selected line pattern scale")
        .setDoc("Number of times each bit of the dash pattern of a selected hidden\n"
"line is repeated. Larger values give longer dashes. 1 or less uses\n"
"the pattern as it is."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionHiddenLineWidth", "SelectionHiddenLineWidth", App::ParamInfo::Float, 1.0)
        .setTitle("Selected hidden line width")
        .setDoc("Width of the hidden line."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionBBoxLineWidth", "SelectionBBoxLineWidth", App::ParamInfo::Float, 3.0)
        .setTitle("Selection BBox Line Width")
        .setDoc("Line width in pixels of the bounding box drawn around a selected\n"
"object when selection is shown by bounding box."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowHighlightEdgeOnly", "ShowHighlightEdgeOnly", App::ParamInfo::Bool, false)
        .setTitle("Show pre-selection highlight edge only")
        .setDoc("Show pre-selection highlight edge only"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PreSelectionDelay", "PreSelectionDelay", App::ParamInfo::Float, 0.1)
        .setTitle("Pre Selection Delay")
        .setDoc("Shortest time in seconds between two preselection picks in the 3D\n"
"view while the mouse moves. 0 picks on every mouse move."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PickBackFaceDelay", "PickBackFaceDelay", App::ParamInfo::Int, 2)
        .setTitle("Pick Back Face Delay")
        .setDoc("Number of mouse wheel steps, with Shift and Ctrl held, that it takes\n"
"to move the pick one object further behind or back toward the\n"
"front."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseNewRayPick", "UseNewRayPick", App::ParamInfo::Bool, true)
        .setTitle("Use New Ray Pick")
        .setDoc("Stop a single pick in the 3D view at the nearest hit instead of\n"
"collecting everything along the pick ray. Off is the older and\n"
"slower way."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ViewSelectionExtendFactor", "ViewSelectionExtendFactor", App::ParamInfo::Float, 0.5)
        .setTitle("View Selection Extend Factor")
        .setDoc("Scale applied to the bounding box of the selection when testing\n"
"whether it is already in view, before the view is extended to\n"
"include it. Currently has no effect."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseTightBoundingBox", "UseTightBoundingBox", App::ParamInfo::Bool, true)
        .setTitle("Show more accurate bounds when using bounding box selection style")
        .setDoc("Show more accurate bounds when using bounding box selection style"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseBoundingBoxCache", "UseBoundingBoxCache", App::ParamInfo::Bool, true)
        .setTitle("Use Bounding Box Cache")
        .setDoc("Remember the bounding boxes of objects instead of computing them\n"
"again on every request."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderProjectedBBox", "RenderProjectedBBox", App::ParamInfo::Bool, true)
        .setTitle("Render Projected BBox")
        .setDoc("Show projected bounding box that is aligned to axes of\n"
"global coordinate space"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionFaceWire", "SelectionFaceWire", App::ParamInfo::Bool, false)
        .setTitle("Show hidden tirangulation wires for selected face")
        .setDoc("Show hidden tirangulation wires for selected face"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "NewDocumentCameraScale", "NewDocumentCameraScale", App::ParamInfo::Float, 100.0)
        .setTitle("New Document Camera Scale")
        .setDoc("Camera zoom of a new document, as the diameter of the sphere that\n"
"fits on the screen. A quarter of it is the default size of a new\n"
"coordinate system."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "MaxOnTopSelections", "MaxOnTopSelections", App::ParamInfo::Int, 100)
        .setTitle("Max On Top Selections")
        .setDoc("Largest number of selected objects that are drawn on top of the\n"
"scene. A larger selection is highlighted in place, and the tree view\n"
"does not expand to show its items."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "MaxViewSelections", "MaxViewSelections", App::ParamInfo::Int, 100)
        .setTitle("Max View Selections")
        .setDoc("Largest number of selected objects taken into account when the view\n"
"is fitted or aligned to the selection."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "MaxSelectionNotification", "MaxSelectionNotification", App::ParamInfo::Int, 100)
        .setTitle("Max Selection Notification")
        .setDoc("Number of pending add and remove selection notices after which they\n"
"are replaced by one notice that the whole selection changed. 0 sets\n"
"no limit."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "MapChildrenPlacement", "MapChildrenPlacement", App::ParamInfo::Bool, false)
        .setTitle("Map Children Placement")
        .setDoc("Map child object into parent's coordinate space when showing on top.\n"
"Note that once activated, this option will also activate option ShowOnTop.\n"
"WARNING! This is an experimental option. Please use with caution.")
        .setOnChange(),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EditingTransparency", "EditingTransparency", App::ParamInfo::Float, 0.5)
        .setTitle("Automatically make all object transparent except the one in edit")
        .setDoc("Automatically make all object transparent except the one in edit"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PerViewEdit", "PerViewEdit", App::ParamInfo::Bool, false)
        .setTitle("Edit in one view only")
        .setDoc("Run an edit in the one view it is started in. The other 3D views of\n"
"the document, and the other viewers of a served one, keep showing the\n"
"document as it is and take no input for the edit. When off, every view\n"
"of the document joins the edit and can work in it. Takes effect with\n"
"the next edit."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DraggerScale", "DraggerScale", App::ParamInfo::Float, 0.03)
        .setTitle("Transform dragger scale")
        .setDoc("Size of the transform dragger relative to the viewport."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineTransparency", "HiddenLineTransparency", App::ParamInfo::Float, 0.4)
        .setTitle("Overridden transparency value of all objects in the scene.")
        .setDoc("Overridden transparency value of all objects in the scene.")
        .setProxy("Proxy"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineOverrideTransparency", "HiddenLineOverrideTransparency", App::ParamInfo::Bool, true)
        .setTitle("Override transparency")
        .setDoc("Whether to override transparency of all objects in the scene."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineFaceColor", "HiddenLineFaceColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Hidden Line Face Color")
        .setDoc("Colour all faces are drawn in by the hidden line display style when\n"
"it overrides the face colour.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineOverrideFaceColor", "HiddenLineOverrideFaceColor", App::ParamInfo::Bool, true)
        .setTitle("Override face color")
        .setDoc("Enable preselection and highlight by specified color."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineColor", "HiddenLineColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Hidden Line Color")
        .setDoc("Colour all lines and outlines are drawn in by the hidden line\n"
"display style when it overrides the line colour.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineOverrideColor", "HiddenLineOverrideColor", App::ParamInfo::Bool, true)
        .setTitle("Override line color")
        .setDoc("Enable selection highlighting and use specified color"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineBackground", "HiddenLineBackground", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Hidden Line Background")
        .setDoc("Background colour of a 3D view in the hidden line display style,\n"
"used when overriding the background is turned on.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineOverrideBackground", "HiddenLineOverrideBackground", App::ParamInfo::Bool, true)
        .setTitle("Override background color")
        .setDoc("Replace the background of a 3D view with the hidden line background\n"
"colour while the hidden line display style is active."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineShaded", "HiddenLineShaded", App::ParamInfo::Bool, false)
        .setTitle("Shaded")
        .setDoc("Whether to enable shading in hidden line display style"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineShowOutline", "HiddenLineShowOutline", App::ParamInfo::Bool, true)
        .setTitle("Draw outline")
        .setDoc("Show outline in hidden line display style (only works in experiemental renderer),."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLinePerFaceOutline", "HiddenLinePerFaceOutline", App::ParamInfo::Bool, false)
        .setTitle("Draw per face outline")
        .setDoc("Render per face outline in hidden line display style (Warning! this may cause slow down),."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineSceneOutline", "HiddenLineSceneOutline", App::ParamInfo::Bool, false)
        .setTitle("Draw scene outline")
        .setDoc("Render outline of the whole scene."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineOutlineWidth", "HiddenLineOutlineWidth", App::ParamInfo::Float, 0.0)
        .setTitle("Outline width")
        .setDoc("Width in pixels of the outlines drawn by the hidden line display\n"
"style. 0 uses the default width.")
        .setProxy("SpinBox")
        .setRange(0.0, 100.0, 0.5, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineWidth", "HiddenLineWidth", App::ParamInfo::Float, 1.5)
        .setTitle("Line width")
        .setDoc("Width in pixels of the lines of all objects in the hidden line\n"
"display style. A value below 1 keeps each object's own line width."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLinePointSize", "HiddenLinePointSize", App::ParamInfo::Float, 2)
        .setTitle("Point size")
        .setDoc("Size in pixels of the vertices of all objects in the hidden line\n"
"display style. A value below 1 keeps each object's own point size."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineHideSeam", "HiddenLineHideSeam", App::ParamInfo::Bool, true)
        .setTitle("Hide seam edge")
        .setDoc("Hide seam edges in hidden line display style."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineHideVertex", "HiddenLineHideVertex", App::ParamInfo::Bool, true)
        .setTitle("Hide vertex")
        .setDoc("Hide vertex in hidden line display style."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HiddenLineHideFace", "HiddenLineHideFace", App::ParamInfo::Bool, false)
        .setTitle("Hide face")
        .setDoc("Hide face in hidden line display style."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "StatusMessageTimeout", "StatusMessageTimeout", App::ParamInfo::Int, 5000)
        .setTitle("Status Message Timeout")
        .setDoc("Milliseconds a message stays in the status bar when the command\n"
"showing it gives no time of its own. 0 keeps it until the next\n"
"message."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowSync", "ShadowSync", App::ParamInfo::Int, 1)
        .setTitle("Synchronize")
        .setDoc("Specifies how to sync shadow display style settings to opened document")
        .setProxy("ComboBox")
        .setItems({{"None", "No change to opened document", nullptr}, {"Apply to active view", "Auto apply changed setting to the current active view", nullptr}, {"Apply to active document", "Auto apply changed setting to all views of the current active document", nullptr}, {"Apply to all open documents", "Auto apply changed setting to all opened documents", nullptr}}, false, true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowFlatLines", "ShadowFlatLines", App::ParamInfo::Bool, true)
        .setTitle("Draw object with 'Flat lines' style when shadow is enabled.")
        .setDoc("Draw object with 'Flat lines' style when shadow is enabled."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowDisplayMode", "ShadowDisplayMode", App::ParamInfo::Int, 2)
        .setTitle("Override display mode")
        .setDoc("Override view object display mode when shadow is enabled.")
        .setProxy("ComboBox")
        .setItems({{"Flat Lines", "", nullptr}, {"Shaded", "", nullptr}, {"As Is", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowSpotLight", "ShadowSpotLight", App::ParamInfo::Bool, false)
        .setTitle("Use spot light")
        .setDoc("Whether to use spot light or directional light."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowLightIntensity", "ShadowLightIntensity", App::ParamInfo::Float, 0.8)
        .setTitle("Light intensity")
        .setDoc("Brightness of the light that casts the shadow."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowLightDirectionX", "ShadowLightDirectionX", App::ParamInfo::Float, -1.0)
        .setTitle("Shadow Light Direction X")
        .setDoc("X component of the direction of the light that casts the shadow."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowLightDirectionY", "ShadowLightDirectionY", App::ParamInfo::Float, -1.0)
        .setTitle("Shadow Light Direction Y")
        .setDoc("Y component of the direction of the light that casts the shadow."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowLightDirectionZ", "ShadowLightDirectionZ", App::ParamInfo::Float, -1.0)
        .setTitle("Shadow Light Direction Z")
        .setDoc("Z component of the direction of the light that casts the shadow."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowLightColor", "ShadowLightColor", App::ParamInfo::Hex, 0xF0FDFFFF)
        .setTitle("Light color")
        .setDoc("Colour of the light that casts the shadow.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowShowGround", "ShadowShowGround", App::ParamInfo::Bool, true)
        .setTitle("Show ground")
        .setDoc("Whether to show auto generated ground face. You can specify you own ground\n"
"object by changing its view property 'ShadowStyle' to 'Shadowed', meaning\n"
"that it will only receive but not cast shadow."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowGroundBackFaceCull", "ShadowGroundBackFaceCull", App::ParamInfo::Bool, true)
        .setTitle("Ground back face culling")
        .setDoc("Whether to show the ground when viewing from under the ground face"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowGroundScale", "ShadowGroundScale", App::ParamInfo::Float, 2.0)
        .setTitle("Ground scale")
        .setDoc("The auto generated ground face is determined by the scene bounding box\n"
"multiplied by this scale")
        .setProxy("SpinBox")
        .setRange(0.0, 10000000.0, 0.5, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowGroundColor", "ShadowGroundColor", App::ParamInfo::Hex, 0x7D7D7DFF)
        .setTitle("Ground color")
        .setDoc("Colour of the ground that receives the shadow.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowGroundBumpMap", "ShadowGroundBumpMap", App::ParamInfo::String, "")
        .setTitle("Ground bump map")
        .setDoc("Image file used as a bump map that gives the shadow ground a surface\n"
"relief. Empty for a flat ground.")
        .setProxy("File"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowGroundTexture", "ShadowGroundTexture", App::ParamInfo::String, "")
        .setTitle("Ground texture")
        .setDoc("Image file drawn as a texture on the ground that receives the\n"
"shadow. Empty for a ground of plain colour.")
        .setProxy("File"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowGroundTextureSize", "ShadowGroundTextureSize", App::ParamInfo::Float, 100.0)
        .setTitle("Ground texture size")
        .setDoc("Specifies the physcal length of the ground texture image size.\n"
"Texture mappings beyond this size will be wrapped around")
        .setProxy("SpinBox")
        .setRange(0.0, 10000000.0, 10.0, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowTransparency", "ShadowTransparency", App::ParamInfo::Float, 0.2)
        .setTitle("Shadow transparency")
        .setDoc("How transparent the shadow itself is, where the ground carries the\n"
"shadow and nothing else (Ground transparency at 1). 0 paints a solid\n"
"shadow, 1 an invisible one; the unshadowed ground is hidden either\n"
"way. Coin spelled this SoShadowTransparency and defaulted it to the\n"
"same 0.2.\n"
"\n"
"A drawn ground ignores it -- there the shadow is the ground shaded,\n"
"and how dark it goes is a matter of the light.")
        .setProxy("SpinBox")
        .setRange(0.0, 1.0, 0.1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowGroundTransparency", "ShadowGroundTransparency", App::ParamInfo::Float, 1.0)
        .setTitle("Ground transparency")
        .setDoc("Transparency of the ground that receives the shadow. 1 (the default)\n"
"draws the shadow only, with no ground plane; lower values draw a\n"
"shaded ground of that transparency. A ground reflection always draws\n"
"the ground.")
        .setProxy("SpinBox")
        .setRange(0.0, 1.0, 0.1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowGroundShading", "ShadowGroundShading", App::ParamInfo::Bool, true)
        .setTitle("Ground shading")
        .setDoc("Render ground with shading. If disabled, the ground and the shadow casted\n"
"on ground will not change shading when viewing in different angle."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowExtraRedraw", "ShadowExtraRedraw", App::ParamInfo::Bool, true)
        .setTitle("Shadow Extra Redraw")
        .setDoc("Redraw the 3D view once more after a change while shadows are shown,\n"
"so that the shadow catches up with the scene. Currently has no\n"
"effect."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowSmoothBorder", "ShadowSmoothBorder", App::ParamInfo::Int, 40)
        .setTitle("Smooth border")
        .setDoc("Specifies the blur raidus of the shadow edge. Higher number will result in\n"
"slower rendering speed on scene change. Use a lower 'Precision' value to\n"
"counter the effect."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowSpreadSize", "ShadowSpreadSize", App::ParamInfo::Int, 0)
        .setTitle("Spread size")
        .setDoc("Specifies the spread size for a soft shadow. The resulting spread size is\n"
"dependent on the model scale")
        .setProxy("SpinBox")
        .setRange(0, 10000000.0, 500, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowSpreadSampleSize", "ShadowSpreadSampleSize", App::ParamInfo::Int, 0)
        .setTitle("Spread sample size")
        .setDoc("Specifies the sample size used for rendering shadow spread. A value 0\n"
"corresponds to a sampling square of 2x2. And 1 corresponds to 3x3, etc.\n"
"The bigger the size the slower the rendering speed. You can use a lower\n"
"'Precision' value to counter the effect."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowPrecision", "ShadowPrecision", App::ParamInfo::Float, 1.0)
        .setTitle("Precision")
        .setDoc("Specifies shadow precision. This parameter affects the internal texture\n"
"size used to hold the casted shadows. You might want a bigger texture if\n"
"you want a hard shadow but a smaller one for soft shadow.")
        .setProxy("SpinBox")
        .setRange(0.0, 1.0, 0.1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowEpsilon", "ShadowEpsilon", App::ParamInfo::Float, 1e-05)
        .setTitle("Epsilon")
        .setDoc("Epsilon is used to offset the shadow map depth from the model depth.\n"
"Should be set to as low a number as possible without causing flickering\n"
"in the shadows or on non-shadowed objects.")
        .setProxy("SpinBox")
        .setRange(0.0, 1.0, 1e-05, 10),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowEpsilonMinimum", "ShadowEpsilonMinimum", App::ParamInfo::Float, 1e-06)
        .setTitle("Epsilon minimum")
        .setDoc("Lower bound enforced on the shadow Epsilon (both the per-view\n"
"Shadow_Epsilon property constraint and the render-cache backend).\n"
"The variance shadow map needs a small non-zero epsilon or its\n"
"Chebyshev bound is numerically unstable and speckles the\n"
"self-shadowed side of curved surfaces. Zero disables the floor.")
        .setProxy("SpinBox")
        .setRange(0.0, 1.0, 1e-06, 10),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowThreshold", "ShadowThreshold", App::ParamInfo::Float, 0.0)
        .setTitle("Threshold")
        .setDoc("Can be used to avoid light bleeding in merged shadows cast from different objects.")
        .setProxy("SpinBox")
        .setRange(0.0, 1.0, 0.1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowBoundBoxScale", "ShadowBoundBoxScale", App::ParamInfo::Float, 1.2)
        .setTitle("Bounding box scale")
        .setDoc("Scene bounding box is used to determine the scale of the shadow texture.\n"
"You can increase the bounding box scale to avoid execessive clipping of\n"
"shadows when viewing up close in certain angle.")
        .setProxy("SpinBox")
        .setRange(0.0, 10000000.0, 0.5, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowMaxDistance", "ShadowMaxDistance", App::ParamInfo::Float, 0.0)
        .setTitle("Maximum distance")
        .setDoc("Specifics the clipping distance for when rendering shadows.\n"
"You can increase the bounding box scale to avoid execessive\n"
"clipping of shadows when viewing up close in certain angle.")
        .setProxy("SpinBox")
        .setRange(0.0, 10000000.0, 0.5, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowTransparentShadow", "ShadowTransparentShadow", App::ParamInfo::Bool, false)
        .setTitle("Transparent shadow")
        .setDoc("Whether to cast shadow from transparent objects."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShadowUpdateGround", "ShadowUpdateGround", App::ParamInfo::Bool, true)
        .setTitle("Update ground on scene change")
        .setDoc("Auto update shadow ground on scene changes. You can manually\n"
"update the ground by using the 'Fit view' command"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PropertyViewTimer", "PropertyViewTimer", App::ParamInfo::UInt, 100)
        .setTitle("Property View Timer")
        .setDoc("Milliseconds the property view waits before it refreshes after the\n"
"selection or a property changes."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HierarchyAscend", "HierarchyAscend", App::ParamInfo::Bool, false)
        .setTitle("Hierarchy Ascend")
        .setDoc("Enable selection of upper hierarchy by repeatedly click some already\n"
"selected sub-element."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "CommandHistorySize", "CommandHistorySize", App::ParamInfo::Int, 20)
        .setTitle("Maximum number of commands saved in history")
        .setDoc("Maximum number of commands saved in history"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuIconSize", "PieMenuIconSize", App::ParamInfo::Int, 24)
        .setTitle("Icon size")
        .setDoc("Pie menu icon size")
        .setProxy("SpinBox")
        .setRange(0, 64, 1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuRadius", "PieMenuRadius", App::ParamInfo::Int, 100)
        .setTitle("Radius")
        .setDoc("Pie menu radius")
        .setProxy("SpinBox")
        .setRange(10, 500, 10, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuTriggerRadius", "PieMenuTriggerRadius", App::ParamInfo::Int, 60)
        .setTitle("Trigger radius")
        .setDoc("Pie menu hover trigger radius")
        .setProxy("SpinBox")
        .setRange(10, 500, 10, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuFontSize", "PieMenuFontSize", App::ParamInfo::Int, 0)
        .setTitle("Font size")
        .setDoc("Pie menu font size")
        .setProxy("SpinBox")
        .setRange(0, 32, 1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuTriggerDelay", "PieMenuTriggerDelay", App::ParamInfo::Int, 200)
        .setTitle("Trigger delay (ms)")
        .setDoc("Pie menu sub-menu hover trigger delay, 0 to disable")
        .setProxy("SpinBox")
        .setRange(0, 10000, 100, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuTriggerAction", "PieMenuTriggerAction", App::ParamInfo::Bool, false)
        .setTitle("Trigger action")
        .setDoc("Pie menu action trigger on hover"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuAnimationDuration", "PieMenuAnimationDuration", App::ParamInfo::Int, 250)
        .setTitle("Animation duration (ms)")
        .setDoc("Pie menu animation duration, 0 to disable")
        .setProxy("SpinBox")
        .setRange(0, 5000, 100, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuAnimationCurve", "PieMenuAnimationCurve", App::ParamInfo::Int, 38)
        .setTitle("Animation curve type")
        .setDoc("Pie menu animation curve type")
        .setProxy("AnimationCurve"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuCenterRadius", "PieMenuCenterRadius", App::ParamInfo::Int, 10)
        .setTitle("Center radius")
        .setDoc("Pie menu center circle radius, 0 to disable")
        .setProxy("SpinBox")
        .setRange(0, 250, 1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PieMenuPopup", "PieMenuPopup", App::ParamInfo::Bool, false)
        .setTitle("Show pie menu as popup")
        .setDoc("Show pie menu as a popup widget, disable it to work around some graphics driver problem"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "StickyTaskControl", "StickyTaskControl", App::ParamInfo::Bool, true)
        .setTitle("Makes the task dialog buttons stay at top or bottom of task view.")
        .setDoc("Makes the task dialog buttons stay at top or bottom of task view."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ColorOnTop", "ColorOnTop", App::ParamInfo::Bool, true)
        .setTitle("Show object on top when editing its color.")
        .setDoc("Show object on top when editing its color."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AutoSortWBList", "AutoSortWBList", App::ParamInfo::Bool, false)
        .setTitle("Sort workbench entries by their names in the combo box.")
        .setDoc("Sort workbench entries by their names in the combo box."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "MaxCameraAnimatePeriod", "MaxCameraAnimatePeriod", App::ParamInfo::Int, 3000)
        .setTitle("Maximum camera move animation duration in milliseconds.")
        .setDoc("Maximum camera move animation duration in milliseconds."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "TaskNoWheelFocus", "TaskNoWheelFocus", App::ParamInfo::Bool, true)
        .setTitle("Do not accept wheel focus on input fields in task panels.")
        .setDoc("Do not accept wheel focus on input fields in task panels."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "GestureLongPressRotationCenter", "GestureLongPressRotationCenter", App::ParamInfo::Bool, false)
        .setTitle("Set rotation center on press in gesture navigation mode.")
        .setDoc("Set rotation center on press in gesture navigation mode."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "CheckWidgetPlacementOnRestore", "CheckWidgetPlacementOnRestore", App::ParamInfo::Bool, true)
        .setTitle("Check widget position and size on restore to make sure it is within the current screen.")
        .setDoc("Check widget position and size on restore to make sure it is within the current screen."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "TextCursorWidth", "TextCursorWidth", App::ParamInfo::Int, 1)
        .setTitle("Text cursor width")
        .setDoc("Text cursor width in pixel.")
        .setOnChange()
        .setProxy("SpinBox")
        .setRange(1, 100, 1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PreselectionToolTipCorner", "PreselectionToolTipCorner", App::ParamInfo::Int, 3)
        .setTitle("Corner")
        .setDoc("Preselection tool tip docking corner.")
        .setProxy("ComboBox")
        .setItems({{"Top Left", "", nullptr}, {"Top Right", "", nullptr}, {"Bottom Left", "", nullptr}, {"Bottom Right", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PreselectionToolTipOffsetX", "PreselectionToolTipOffsetX", App::ParamInfo::Int, 0)
        .setTitle("Offset X")
        .setDoc("Preselection tool tip x offset relative to its docking corner.")
        .setProxy("SpinBox")
        .setRange(0, 4000, 1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PreselectionToolTipOffsetY", "PreselectionToolTipOffsetY", App::ParamInfo::Int, 0)
        .setTitle("Offset Y")
        .setDoc("Preselection tool tip y offset relative to its docking corner.")
        .setProxy("SpinBox")
        .setRange(0, 4000, 1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "PreselectionToolTipFontSize", "PreselectionToolTipFontSize", App::ParamInfo::Int, 0)
        .setTitle("Font size")
        .setDoc("Preselection tool tip font size. Set to 0 to use system default.")
        .setProxy("SpinBox")
        .setRange(0, 100, 1, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SectionFill", "SectionFill", App::ParamInfo::Bool, true)
        .setTitle("Fill cross section plane.")
        .setDoc("Fill cross section plane."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SectionFillInvert", "SectionFillInvert", App::ParamInfo::Bool, true)
        .setTitle("Invert cross section plane fill color.")
        .setDoc("Invert cross section plane fill color."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SectionConcave", "SectionConcave", App::ParamInfo::Bool, false)
        .setTitle("Cross section in concave.")
        .setDoc("Cross section in concave."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "NoSectionOnTop", "NoSectionOnTop", App::ParamInfo::Bool, true)
        .setTitle("Ignore section clip planes when rendering on top.")
        .setDoc("Ignore section clip planes when rendering on top."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SectionHatchTextureScale", "SectionHatchTextureScale", App::ParamInfo::Float, 1.0)
        .setTitle("Section filling texture image scale.")
        .setDoc("Section filling texture image scale."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SectionHatchTexture", "SectionHatchTexture", App::ParamInfo::String, ":icons/section-hatch.png")
        .setTitle("Section filling texture image path.")
        .setDoc("Section filling texture image path.")
        .setOnChange(),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SectionHatchTextureEnable", "SectionHatchTextureEnable", App::ParamInfo::Bool, true)
        .setTitle("Enable section fill texture.")
        .setDoc("Enable section fill texture."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SectionFillGroup", "SectionFillGroup", App::ParamInfo::Bool, false)
        .setTitle("Section Fill Group")
        .setDoc("Render cross section filling of objects with similar materials together.\n"
"Intersecting objects will act as boolean cut operation"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowClipPlane", "ShowClipPlane", App::ParamInfo::Bool, false)
        .setTitle("Show clip plane")
        .setDoc("Show clip plane"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ClipPlaneSize", "ClipPlaneSize", App::ParamInfo::Float, 40.0)
        .setTitle("Clip plane visual size")
        .setDoc("Clip plane visual size"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ClipPlaneColor", "ClipPlaneColor", App::ParamInfo::String, "cyan")
        .setTitle("Clip plane color")
        .setDoc("Clip plane color"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ClipPlaneLineWidth", "ClipPlaneLineWidth", App::ParamInfo::Float, 2.0)
        .setTitle("Clip plane line width")
        .setDoc("Clip plane line width"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "TransformOnTop", "TransformOnTop", App::ParamInfo::Bool, true)
        .setTitle("Transform On Top")
        .setDoc("Show an object on top of the scene while it is moved with the\n"
"transform dragger. Currently has no effect."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionColorDifference", "SelectionColorDifference", App::ParamInfo::Float, 25.0)
        .setTitle("Selection Color Difference")
        .setDoc("Color difference threshold for auto making distinct\n"
"selection highlight color")
        .setProxy("SpinBox")
        .setRange(0, 100, 1, 1),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderCacheMergeCount", "RenderCacheMergeCount", App::ParamInfo::Int, 0)
        .setTitle("Render Cache Merge Count")
        .setDoc("Merge draw caches of multiple objects to reduce number of draw\n"
"calls and improve render performance. Set zero to disable. Only\n"
"effective when using experimental render cache."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderCacheMergeCountMin", "RenderCacheMergeCountMin", App::ParamInfo::Int, 10)
        .setTitle("Internal use to limit the render cache merge count")
        .setDoc("Internal use to limit the render cache merge count"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderCacheMergeCountMax", "RenderCacheMergeCountMax", App::ParamInfo::Int, 0)
        .setTitle("Maximum draw crash merges on any hierarchy. Zero means no limit.")
        .setDoc("Maximum draw crash merges on any hierarchy. Zero means no limit."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderCacheMergeDepthMax", "RenderCacheMergeDepthMax", App::ParamInfo::Int, -1)
        .setTitle("Maximum hierarchy depth that the cache merge can happen. Less than 0 means no limit.")
        .setDoc("Maximum hierarchy depth that the cache merge can happen. Less than 0 means no limit."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderCacheMergeDepthMin", "RenderCacheMergeDepthMin", App::ParamInfo::Int, 1)
        .setTitle("Minimum hierarchy depth that the cache merge can happen.")
        .setDoc("Minimum hierarchy depth that the cache merge can happen."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderCacheKeepMax", "RenderCacheKeepMax", App::ParamInfo::Int, 32)
        .setTitle("Render Cache Keep Max")
        .setDoc("Largest render cache, in entries, an object keeps after its parent has\n"
"merged it. Keeping the small ones avoids rebuilding them every frame.\n"
"0 keeps none."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderCacheIncremental", "RenderCacheIncremental", App::ParamInfo::Int, 1)
        .setTitle("Render Cache Incremental")
        .setDoc("Update a container's render cache from its previous one instead of\n"
"merging all its children again. Faster on large assemblies, at the\n"
"cost of some memory. 0 off, 1 on, 2 does both and logs any difference\n"
"(slow, for checking)."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderCacheMeshReuse", "RenderCacheMeshReuse", App::ParamInfo::Int, 1)
        .setTitle("Render Cache Mesh Reuse")
        .setDoc("Reuse the mesh a render cache was converted to for the render backend\n"
"instead of converting it at every update. 0 off, 1 on, 2 does both and\n"
"logs any difference (slow, for checking)."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "LiveImportRedrawInterval", "LiveImportRedrawInterval", App::ParamInfo::Int, 200)
        .setTitle("Live Import Redraw Interval")
        .setDoc("Minimum interval in milliseconds between 3D view redraws while a\n"
"progressive import is filling the document, and the window after\n"
"any mouse input during which redraws are never held back. Set zero\n"
"to redraw on every change."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "LiveImportRedrawBudget", "LiveImportRedrawBudget", App::ParamInfo::Int, 10)
        .setTitle("Live Import Redraw Budget")
        .setDoc("Percentage of time the 3D view may spend redrawing while a progressive\n"
"import fills the document. The wait between frames is never shorter\n"
"than LiveImportRedrawInterval nor longer than ten times it. 0 uses the\n"
"plain interval."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "LiveImportPumpInterval", "LiveImportPumpInterval", App::ParamInfo::Int, 50)
        .setTitle("Live Import Pump Interval")
        .setDoc("Minimum milliseconds between two chances for the window to process\n"
"input and repaint while a live import fills the document. 0 offers one\n"
"at every opportunity."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderHighlightPolygonOffsetFactor", "RenderHighlightPolygonOffsetFactor", App::ParamInfo::Float, 1)
        .setTitle("Render Highlight Polygon Offset Factor")
        .setDoc("Slope scaled depth offset that pulls selection and preselection\n"
"highlights toward the viewer, so that the faces under them do not\n"
"hide them. Preselection gets twice the offset."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RenderHighlightPolygonOffsetUnits", "RenderHighlightPolygonOffsetUnits", App::ParamInfo::Float, 1)
        .setTitle("Render Highlight Polygon Offset Units")
        .setDoc("Constant depth offset, in depth buffer units, that pulls selection\n"
"and preselection highlights toward the viewer, so that the faces\n"
"under them do not hide them. Preselection gets twice the offset."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ForceSolidSingleSideLighting", "ForceSolidSingleSideLighting", App::ParamInfo::Bool, true)
        .setTitle("Force single side lighting on solid")
        .setDoc("Force single side lighting on solid. This can help visualizing invalid\n"
"solid shapes with flipped normals.")
        .setOnChange(),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultFontSize", "DefaultFontSize", App::ParamInfo::Int, 0)
        .setTitle("Default Font Size")
        .setDoc("Point size of the application font. 0 uses the system default.\n"
"Sizes from 1 to 7 are raised to 8.")
        .setOnChange(),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EnableTaskPanelKeyTranslate", "EnableTaskPanelKeyTranslate", App::ParamInfo::Bool, false)
        .setTitle("Enable Task Panel Key Translate")
        .setDoc("Let the Up and Down arrow keys move the keyboard focus through the\n"
"task panel, the way Shift+Tab and Tab do.")
        .setOnChange(),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EnableMenuBarCheckBox", "EnableMenuBarCheckBox", App::ParamInfo::Bool, FC_ENABLE_MENUBAR_CHECKBOX)
        .setTitle("Enable Menu Bar Check Box")
        .setDoc("Show the entries of the toolbar and dock window menus of the menu\n"
"bar as checkboxes, the way the right-click menu of the main window\n"
"shows them. Off by default on macOS."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EnableBacklight", "EnableBacklight", App::ParamInfo::Bool, false)
        .setTitle("Enable Backlight")
        .setDoc("Turn on the backlight of the 3D view, a second light that shines on\n"
"the faces turned away from the viewer."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "BacklightColor", "BacklightColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Backlight Color")
        .setDoc("Colour of the backlight, the light that shines on the faces turned\n"
"away from the viewer."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "BacklightIntensity", "BacklightIntensity", App::ParamInfo::Int, 100)
        .setTitle("Backlight Intensity")
        .setDoc("Backlight intensity, as a percentage. An integer because that is the\n"
"slot everything else uses: the Clipping dialog's slider, the 3D view\n"
"preference page and the viewer, which divides it by a hundred. This\n"
"class used to read a Float fraction from the same name -- a second,\n"
"separate slot that nothing ever wrote; see ViewParams::migrate()."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "OverrideSelectability", "OverrideSelectability", App::ParamInfo::Bool, false)
        .setTitle("Override object selectability to enable selection")
        .setDoc("Override object selectability to enable selection"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SelectionStackSize", "SelectionStackSize", App::ParamInfo::UInt, 30)
        .setTitle("Maximum selection history record size")
        .setDoc("Maximum selection history record size"),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultDrawStyle", "DefaultDrawStyle", App::ParamInfo::Int, 0)
        .setTitle("Default display style")
        .setDoc("Default display style of a new document")
        .setProxy("ComboBox")
        .setItems({{"As Is", "Display style, normal display mode", nullptr}, {"Points", "Display style, show points only", nullptr}, {"Wireframe", "Display style, show wire frame only", nullptr}, {"Hidden Line", "Display style, show hidden line by display object as transparent", nullptr}, {"No Shading", "Display style, shading forced off", nullptr}, {"Shaded", "Display style, shading force on", nullptr}, {"Flat Lines", "Display style, show both wire frame and face with shading", nullptr}, {"Tessellation", "Display style, show tessellation wire frame", nullptr}}, false, true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ToolTipIconSize", "ToolTipIconSize", App::ParamInfo::Int, 64)
        .setTitle("Tool tip icon size")
        .setDoc("Specifies the size of static icon image in tooltip. GIF animation\n"
"will be shown in its original size. You can disable all images in\n"
"the tooltip by setting this option to zero.")
        .setProxy("SpinBox")
        .setRange(0, 512, 10, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ToolTipDisable", "ToolTipDisable", App::ParamInfo::Bool, false)
        .setTitle("Tool Tip Disable")
        .setDoc("Turn off the tool tips of the application. Tips shown as an overlay\n"
"in the 3D view, such as the preselection tip, still appear."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AxisXColor", "AxisXColor", App::ParamInfo::Hex, 0xCC333300)
        .setTitle("Axis X color")
        .setDoc("Colour of the X axis of the transform dragger and of other axis\n"
"markers in the 3D view."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AxisYColor", "AxisYColor", App::ParamInfo::Hex, 0x33CC3300)
        .setTitle("Axis Y color")
        .setDoc("Colour of the Y axis of the transform dragger and of other axis\n"
"markers in the 3D view."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AxisZColor", "AxisZColor", App::ParamInfo::Hex, 0x3333CC00)
        .setTitle("Axis Z color")
        .setDoc("Colour of the Z axis of the transform dragger and of other axis\n"
"markers in the 3D view."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DatumScreenSize", "DatumScreenSize", App::ParamInfo::Bool, true)
        .setTitle("Constant datum size on screen")
        .setDoc("Draw origins, coordinate systems and datum elements at a constant size on\n"
"screen, the way upstream FreeCAD does. When off, an origin is sized to the\n"
"objects of its body or part."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DatumScale", "DatumScale", App::ParamInfo::Float, 100.0)
        .setTitle("Datum scale")
        .setDoc("Size in percent of origins, coordinate systems and datum elements drawn\n"
"at a constant size on screen.")
        .setProxy("SpinBox")
        .setRange(1.0, 1000.0, 10.0, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DatumPlaneSize", "DatumPlaneSize", App::ParamInfo::Float, 62.0)
        .setTitle("Datum plane size")
        .setDoc("On-screen size of a datum plane, before the datum scale.")
        .setProxy("SpinBox")
        .setRange(1.0, 1000.0, 1.0, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DatumLineSize", "DatumLineSize", App::ParamInfo::Float, 70.0)
        .setTitle("Datum line size")
        .setDoc("On-screen length of a datum axis, before the datum scale.")
        .setProxy("SpinBox")
        .setRange(1.0, 1000.0, 1.0, 0),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DatumTemporaryScaleFactor", "DatumTemporaryScaleFactor", App::ParamInfo::Float, 2.0)
        .setTitle("Datum temporary scale")
        .setDoc("How much datum planes grow while a reference is picked from them.")
        .setProxy("SpinBox")
        .setRange(1.0, 10.0, 0.5, 1),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EyeDistance", "EyeDistance", App::ParamInfo::Float, 5.0)
        .setTitle("Eye distance for stereo modes")
        .setDoc("Offset between the left and the right eye image of a stereo 3D\n"
"view. 0.1 to 1000. Applies at once to all open 3D views."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "CornerCoordSystem", "CornerCoordSystem", App::ParamInfo::Bool, true)
        .setTitle("Show coordinate system in the corner")
        .setDoc("Show the small coordinate system in the corner of every 3D view.\n"
"Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "CornerCoordSystemSize", "CornerCoordSystemSize", App::ParamInfo::Int, 10)
        .setTitle("Corner coordinate system size")
        .setDoc("Size of the coordinate system in the corner of the 3D views, 2 to\n"
"100. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowAxisCross", "ShowAxisCross", App::ParamInfo::Bool, false)
        .setTitle("Show axis cross")
        .setDoc("Show the axis cross at the origin of the 3D views. Applies at once\n"
"to the open views and to new ones."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowFPS", "ShowFPS", App::ParamInfo::Bool, false)
        .setTitle("Show counter of frames per second")
        .setDoc("Show a frames per second counter in the 3D views. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseVBO", "UseVBO", App::ParamInfo::Bool, false)
        .setTitle("Use vertex buffer objects")
        .setDoc("Let Coin draw with vertex buffer objects. Applies at once to the\n"
"open 3D views except split views; the driver override that goes\n"
"with it is set at startup only."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "Orthographic", "Orthographic", App::ParamInfo::Bool, true)
        .setTitle("Orthographic rendering")
        .setDoc("Use an orthographic camera in the 3D views; the opposite of\n"
"Perspective."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "Perspective", "Perspective", App::ParamInfo::Bool, false)
        .setTitle("Perspective rendering")
        .setDoc("Use a perspective camera in the 3D views. Read when a view is\n"
"created; with ApplyCameraTypeToAll on, a change switches the open\n"
"views too."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ApplyCameraTypeToAll", "ApplyCameraTypeToAll", App::ParamInfo::Bool, false)
        .setTitle("Apply camera type to existing views")
        .setDoc("When the camera type setting changes, switch every open 3D view\n"
"to it as well."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AntiAliasing", "AntiAliasing", App::ParamInfo::Int, 3)
        .setTitle("Anti-aliasing")
        .setDoc("Anti-aliasing of the 3D views: 0 none, 1 line smoothing, 2 MSAA\n"
"2x, 3 MSAA 4x, 4 MSAA 8x. Without multisampling the rim of a\n"
"face that no edge runs along is a staircase. A change is taken\n"
"by the open views at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "TransparentObjectRenderType", "TransparentObjectRenderType", App::ParamInfo::Int, 0)
        .setTitle("Transparent object render type")
        .setDoc("How Coin draws transparent objects: 0 in one pass, 1 with the back\n"
"faces of non-solid objects in a pass of their own. Applies at once\n"
"to the open 3D views except split views."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "InternalTextureFormat", "InternalTextureFormat", App::ParamInfo::String, "Default")
        .setTitle("Offscreen buffer format")
        .setDoc("Pixel format of the offscreen buffer a 3D view is drawn into:\n"
"Default, GL_RGB, GL_RGBA, GL_RGB8, GL_RGBA8, GL_RGB10,\n"
"GL_RGB10_A2, GL_RGB16, GL_RGBA16, GL_RGB32F or GL_RGBA32F. Read\n"
"each time a buffer is created."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "Gradient", "Gradient", App::ParamInfo::Bool, true)
        .setTitle("Linear background gradient")
        .setDoc("Fill the background of the 3D views with a linear gradient from\n"
"BackgroundColor2 (top) to BackgroundColor3 (bottom). Wins over\n"
"RadialGradient. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RadialGradient", "RadialGradient", App::ParamInfo::Bool, false)
        .setTitle("Radial background gradient")
        .setDoc("Fill the background of the 3D views with a radial gradient; used\n"
"when Gradient is off. With both off the plain BackgroundColor is\n"
"used. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "Simple", "Simple", App::ParamInfo::Bool, false)
        .setTitle("Simple background colour")
        .setDoc("The 'Simple color' choice of the Colors page. The views use a\n"
"plain background whenever Gradient and RadialGradient are both\n"
"off, whatever this says."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "BackgroundColor", "BackgroundColor", App::ParamInfo::Hex, 0xEAE5DCFF)
        .setTitle("Background colour")
        .setDoc("Colour of the 3D view background when no gradient is used.\n"
"Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "BackgroundColor2", "BackgroundColor2", App::ParamInfo::Hex, 0x333365FF)
        .setTitle("Background gradient, first colour")
        .setDoc("First colour of the background gradient of the 3D views: the top\n"
"of a linear one, the centre of a radial one. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "BackgroundColor3", "BackgroundColor3", App::ParamInfo::Hex, 0xABABC1FF)
        .setTitle("Background gradient, last colour")
        .setDoc("Last colour of the background gradient of the 3D views: the\n"
"bottom of a linear one, the rim of a radial one. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "BackgroundColor4", "BackgroundColor4", App::ParamInfo::Hex, 0x6F6F93FF)
        .setTitle("Background gradient, middle colour")
        .setDoc("Middle colour of the background gradient of the 3D views; used\n"
"only with UseBackgroundColorMid on. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseBackgroundColorMid", "UseBackgroundColorMid", App::ParamInfo::Bool, false)
        .setTitle("Use a middle background colour")
        .setDoc("Give the background gradient of the 3D views a third, middle\n"
"colour (BackgroundColor4). Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EnableHeadlight", "EnableHeadlight", App::ParamInfo::Bool, true)
        .setTitle("Enable headlight")
        .setDoc("Light the 3D views with the headlight, which follows the camera.\n"
"Applies at once to every view with no light setting of its own."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HeadlightColor", "HeadlightColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Headlight colour")
        .setDoc("Colour of the headlight of the 3D views. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HeadlightIntensity", "HeadlightIntensity", App::ParamInfo::Int, 100)
        .setTitle("Headlight intensity")
        .setDoc("Intensity of the headlight of the 3D views in percent, 0 to 100.\n"
"Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HeadlightDirection", "HeadlightDirection", App::ParamInfo::String, "")
        .setTitle("Headlight direction")
        .setDoc("Direction of the headlight relative to the camera, as (x,y,z).\n"
"Empty keeps the built-in direction. Turned in a 3D view from the\n"
"Display style menu (Direction), and stored here by its Save as\n"
"default. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "BacklightDirection", "BacklightDirection", App::ParamInfo::String, "")
        .setTitle("Backlight direction")
        .setDoc("Direction of the backlight relative to the camera, as (x,y,z).\n"
"Empty keeps the built-in direction. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "EnableFillLight", "EnableFillLight", App::ParamInfo::Bool, false)
        .setTitle("Enable fill light")
        .setDoc("Light the 3D views with an extra fill light from the side.\n"
"Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "FillLightColor", "FillLightColor", App::ParamInfo::Hex, 0xE6FAFFFF)
        .setTitle("Fill light colour")
        .setDoc("Colour of the fill light of the 3D views. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "FillLightIntensity", "FillLightIntensity", App::ParamInfo::Int, 60)
        .setTitle("Fill light intensity")
        .setDoc("Intensity of the fill light of the 3D views in percent, 0 to 100.\n"
"Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "FillLightDirection", "FillLightDirection", App::ParamInfo::String, "")
        .setTitle("Fill light direction")
        .setDoc("Direction of the fill light relative to the camera, as (x,y,z).\n"
"Empty keeps the built-in direction. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AmbientLightColor", "AmbientLightColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Ambient light colour")
        .setDoc("Colour of the ambient light of the 3D views. Applies at once.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AmbientLightIntensity", "AmbientLightIntensity", App::ParamInfo::Int, 20)
        .setTitle("Ambient light intensity")
        .setDoc("Intensity of the ambient light of the 3D views in percent, 0 to\n"
"100. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "NavigationStyle", "NavigationStyle", App::ParamInfo::String, "Gui::CADNavigationStyle")
        .setTitle("3D navigation style")
        .setDoc("Mouse navigation style of the 3D views, as a class name such as\n"
"Gui::CADNavigationStyle. Applies at once to all open 3D views;\n"
"TechDraw pages follow it as well."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SameStyleForAllViews", "SameStyleForAllViews", App::ParamInfo::Bool, true)
        .setTitle("Same navigation style for all views")
        .setDoc("A navigation style picked from a 3D view's context menu becomes\n"
"the NavigationStyle setting, so that every view follows. When off\n"
"it changes that view only."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "OrbitStyle", "OrbitStyle", App::ParamInfo::Int, 1)
        .setTitle("Orbit style")
        .setDoc("How dragging rotates the 3D view: 0 turntable, 1 trackball, 2 free\n"
"turntable. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RotationMode", "RotationMode", App::ParamInfo::Int, 1)
        .setTitle("Rotation mode")
        .setDoc("Centre of rotation in the 3D views: 0 the window centre, 1 the\n"
"point under the cursor, 2 the centre of the objects. Applies at\n"
"once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "Sensitivity", "Sensitivity", App::ParamInfo::Float, 2.0)
        .setTitle("Rotation sensitivity")
        .setDoc("A value above 1 multiplies the angle of a mouse rotation of the 3D\n"
"view. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ResetCursorPosition", "ResetCursorPosition", App::ParamInfo::Bool, false)
        .setTitle("Reset cursor position on rotation")
        .setDoc("Move the mouse cursor to the rotation centre when a rotation of\n"
"the 3D view starts. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "InvertZoom", "InvertZoom", App::ParamInfo::Bool, true)
        .setTitle("Invert zoom")
        .setDoc("Invert the direction of zooming with the mouse wheel. The 3D\n"
"views and TechDraw pages follow at once; the dependency graph\n"
"reads it when it is opened."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ZoomAtCursor", "ZoomAtCursor", App::ParamInfo::Bool, true)
        .setTitle("Zoom at cursor")
        .setDoc("Zoom towards the point under the mouse cursor instead of the\n"
"centre of the view. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ZoomStep", "ZoomStep", App::ParamInfo::Float, 0.2)
        .setTitle("Zoom step")
        .setDoc("Zoom factor of one step of the mouse wheel, 0.01 to 1. Applies at\n"
"once to the 3D views and TechDraw pages."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseNavigationAnimations", "UseNavigationAnimations", App::ParamInfo::Bool, true)
        .setTitle("Animate camera moves")
        .setDoc("Animate camera moves such as switching to a standard view.\n"
"Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "UseSpinningAnimations", "UseSpinningAnimations", App::ParamInfo::Bool, false)
        .setTitle("Spin after a rotation")
        .setDoc("Let the model keep spinning when the mouse button is released\n"
"during a rotation. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AnimationDuration", "AnimationDuration", App::ParamInfo::Int, 250)
        .setTitle("Animation duration")
        .setDoc("Duration of an animated camera move in milliseconds, 100 to\n"
"10000. Read each time an animation starts."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "stopAnimatingIfDeactivated", "stopAnimatingIfDeactivated", App::ParamInfo::Int, 3000)
        .setTitle("Stop spinning when hidden after")
        .setDoc("Milliseconds after which a spinning 3D view stops once it is\n"
"hidden or minimized. A negative value never stops it."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowRotationCenter", "ShowRotationCenter", App::ParamInfo::Bool, true)
        .setTitle("Show rotation centre")
        .setDoc("Show a marker at the centre of rotation while a 3D view is\n"
"rotated. Read at each rotation."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RotationCenterSize", "RotationCenterSize", App::ParamInfo::Float, 5.0)
        .setTitle("Rotation centre size")
        .setDoc("Size of the rotation centre marker, 1 to 100. Read when the marker\n"
"is next created."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "RotationCenterColor", "RotationCenterColor", App::ParamInfo::Hex, 0xFF000033)
        .setTitle("Rotation centre colour")
        .setDoc("Colour and opacity of the rotation centre marker: red and mostly\n"
"see-through unless set. Read when the marker is next created.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "NewDocumentCameraOrientation", "NewDocumentCameraOrientation", App::ParamInfo::String, "Trimetric")
        .setTitle("Default camera orientation")
        .setDoc("Camera orientation of a new document: Isometric, Dimetric,\n"
"Trimetric, Top, Front, Left, Right, Rear, Bottom, or Custom. Once\n"
"set, the Home view takes it too; until then Home is Top. Read at\n"
"each use."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "AutoFitToView", "AutoFitToView", App::ParamInfo::Bool, true)
        .setTitle("Fit view after opening a file")
        .setDoc("Fit the 3D view to the model after a file is opened or imported."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "ShowNaviCube", "ShowNaviCube", App::ParamInfo::Bool, true)
        .setTitle("Show navigation cube")
        .setDoc("Show the navigation cube in the 3D views. Applies at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DisableTouchTilt", "DisableTouchTilt", App::ParamInfo::Bool, true)
        .setTitle("Disable touchscreen tilt gesture")
        .setDoc("Gesture navigation: ignore the rotation part of a two-finger\n"
"gesture on a touchscreen. Read at the start of each gesture."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "NavigationDebug", "NavigationDebug", App::ParamInfo::Bool, false)
        .setTitle("Log gesture navigation")
        .setDoc("Gesture navigation: write its state changes to the log. Read when\n"
"the Gesture style is created."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "GestureRollFwdCommand", "GestureRollFwdCommand", App::ParamInfo::String, "Std_SelForward")
        .setTitle("Roll forward gesture command")
        .setDoc("Gesture navigation: command run by the forward roll gesture."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "GestureRollBackCommand", "GestureRollBackCommand", App::ParamInfo::String, "Std_SelBack")
        .setTitle("Roll back gesture command")
        .setDoc("Gesture navigation: command run by the backward roll gesture."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SaveWBbyTab", "SaveWBbyTab", App::ParamInfo::Bool, false)
        .setTitle("Remember active workbench by tab")
        .setDoc("Remember the active workbench separately for each view tab and\n"
"switch back to it when the tab is activated."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "CbLabelColor", "CbLabelColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Colour bar label colour")
        .setDoc("Colour of the value labels of a colour bar in the 3D view. Read\n"
"when the labels are next rebuilt.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "CbLabelTextSize", "CbLabelTextSize", App::ParamInfo::Int, 13)
        .setTitle("Colour bar label size")
        .setDoc("Text size of the value labels of a colour bar in the 3D view, 4 to\n"
"36. Read when the labels are next rebuilt."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "BoundingBoxFontSize", "BoundingBoxFontSize", App::ParamInfo::Float, 10.0)
        .setTitle("Bounding box font size")
        .setDoc("Font size of the dimension labels on an object's bounding box, 2\n"
"to 64. Read when a bounding box is first shown for an object."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DatumPointSize", "DatumPointSize", App::ParamInfo::Float, 2.5)
        .setTitle("Datum point size")
        .setDoc("Radius of the sphere drawn for a datum point."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "LocalCoordinateSystemSize", "LocalCoordinateSystemSize", App::ParamInfo::Float, 1.0)
        .setTitle("Datum scale factor")
        .setDoc("Scale factor of datum objects -- origin axes, planes, points --\n"
"when they are drawn at a fixed size on screen."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DefaultShapeShininess", "DefaultShapeShininess", App::ParamInfo::Int, 37)
        .setTitle("Default shape shininess")
        .setDoc("Shininess of the appearance given to new objects, in percent.\n"
"Read each time a default appearance is made."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DimensionsVisible", "DimensionsVisible", App::ParamInfo::Bool, true)
        .setTitle("Measurements shown")
        .setDoc("The measurements made with the measure tools are shown in the 3D\n"
"views. Stored by the command that toggles them; the views follow\n"
"at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "Dimensions3dVisible", "Dimensions3dVisible", App::ParamInfo::Bool, true)
        .setTitle("3D measurements shown")
        .setDoc("The direct, 3D, lines of the measurements are shown. Stored by\n"
"the command that toggles them; the views follow at once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "DimensionsDeltaVisible", "DimensionsDeltaVisible", App::ParamInfo::Bool, true)
        .setTitle("Delta measurements shown")
        .setDoc("The lines of the measurements along the three axes are shown.\n"
"Stored by the command that toggles them; the views follow at\n"
"once."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "SavePicture", "SavePicture", App::ParamInfo::String, "")
        .setTitle("Save picture: last method")
        .setDoc("How the Save picture dialog last made its picture:\n"
"FramebufferObject, CoinOffscreenRenderer, GrabFramebuffer, or\n"
"empty for the dialog's own way. Stored when a method is chosen in\n"
"the dialog's options, and used for every picture saved."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HeadlightRotationX", "HeadlightRotationX", App::ParamInfo::Float, 0.0)
        .setTitle("Headlight dragger: rotation x")
        .setDoc("First component of the rotation the dragger of the Light\n"
"sources page was left with. The view itself uses the headlight\n"
"direction, which the page stores with it."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HeadlightRotationY", "HeadlightRotationY", App::ParamInfo::Float, 0.0)
        .setTitle("Headlight dragger: rotation y")
        .setDoc("Second component of the rotation of the dragger of the Light\n"
"sources page."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HeadlightRotationZ", "HeadlightRotationZ", App::ParamInfo::Float, 0.0)
        .setTitle("Headlight dragger: rotation z")
        .setDoc("Third component of the rotation of the dragger of the Light\n"
"sources page."),
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View", "HeadlightRotationW", "HeadlightRotationW", App::ParamInfo::Float, 1.0)
        .setTitle("Headlight dragger: rotation w")
        .setDoc("Fourth component of the rotation of the dragger of the Light\n"
"sources page."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle ViewParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseViewArea() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Host views in a split-capable view area, so that several views\n"
"can share one tab side by side. Off, every view gets its own tab\n"
"and the split placement choices below do not apply.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseViewArea() {
    return instance()->UseViewArea;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseViewArea() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseViewArea(const bool &v) {
    instance()->handle->SetBool("UseViewArea",v);
    instance()->UseViewArea = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseViewArea() {
    instance()->handle->RemoveBool("UseViewArea");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseNewSelection() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Highlight selection and preselection through the selection root of\n"
"the 3D view, which allows picking sub-elements and objects inside\n"
"links. When off, only objects that ask for it are handled that way.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseNewSelection() {
    return instance()->UseNewSelection;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseNewSelection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseNewSelection(const bool &v) {
    instance()->handle->SetBool("UseNewSelection",v);
    instance()->UseNewSelection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseNewSelection() {
    instance()->handle->RemoveBool("UseNewSelection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseSelectionRoot() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Give the visual copy a link makes of its linked object a selection\n"
"root of its own, so that each link is highlighted separately. When\n"
"off, a plain group without render caching is used.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseSelectionRoot() {
    return instance()->UseSelectionRoot;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseSelectionRoot() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseSelectionRoot(const bool &v) {
    instance()->handle->SetBool("UseSelectionRoot",v);
    instance()->UseSelectionRoot = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseSelectionRoot() {
    instance()->handle->RemoveBool("UseSelectionRoot");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEnableSelection() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable selection, highlighted with specified color");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getEnableSelection() {
    return instance()->EnableSelection;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultEnableSelection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEnableSelection(const bool &v) {
    instance()->handle->SetBool("EnableSelection",v);
    instance()->EnableSelection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEnableSelection() {
    instance()->handle->RemoveBool("EnableSelection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEnablePreselection() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable preselection, highlighted with specified color");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getEnablePreselection() {
    return instance()->EnablePreselection;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultEnablePreselection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEnablePreselection(const bool &v) {
    instance()->handle->SetBool("EnablePreselection",v);
    instance()->EnablePreselection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEnablePreselection() {
    instance()->handle->RemoveBool("EnablePreselection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderCache() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Which render path draws a 3D view: 0 auto, 1 distributed,\n"
"2 centralized Coin caching, 3 the render cache that feeds the\n"
"render engine. Looked at only under the render type 'Legacy'\n"
"(View/Render/Type): with the render engine the program draws\n"
"by 3 whatever this holds. Not on a preferences page.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRenderCache() {
    return instance()->RenderCache;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRenderCache() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderCache(const long &v) {
    instance()->handle->SetInt("RenderCache",v);
    instance()->RenderCache = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderCache() {
    instance()->handle->RemoveInt("RenderCache");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUnifiedCanvas() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Draw all the 3D cells of a split view (ViewArea) into ONE\n"
"canvas widget, as sub-views of a single render backend, instead\n"
"of composing each cell's own widget. One backend instance and\n"
"one copy of the GPU scene serve every cell (the browser tier's\n"
"model). Experimental; needs the render engine (render cache\n"
"mode 3). See docs/SplitViews.md sec 13.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUnifiedCanvas() {
    return instance()->UnifiedCanvas;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUnifiedCanvas() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUnifiedCanvas(const bool &v) {
    instance()->handle->SetBool("UnifiedCanvas",v);
    instance()->UnifiedCanvas = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUnifiedCanvas() {
    instance()->handle->RemoveBool("UnifiedCanvas");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRandomColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Give every new object a random shape colour instead of the default\n"
"shape colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getRandomColor() {
    return instance()->RandomColor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultRandomColor() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRandomColor(const bool &v) {
    instance()->handle->SetBool("RandomColor",v);
    instance()->RandomColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRandomColor() {
    instance()->handle->RemoveBool("RandomColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docBoundingBoxColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the bounding box drawn for an object that has its bounding\n"
"box display turned on.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getBoundingBoxColor() {
    return instance()->BoundingBoxColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultBoundingBoxColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setBoundingBoxColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("BoundingBoxColor",v);
    instance()->BoundingBoxColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeBoundingBoxColor() {
    instance()->handle->RemoveUnsigned("BoundingBoxColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAnnotationTextColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default text colour of new annotation objects.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getAnnotationTextColor() {
    return instance()->AnnotationTextColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultAnnotationTextColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAnnotationTextColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AnnotationTextColor",v);
    instance()->AnnotationTextColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAnnotationTextColor() {
    instance()->handle->RemoveUnsigned("AnnotationTextColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docCursorCrosshairColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the crosshair the drawing tools of the Sketcher put in\n"
"place of the mouse pointer. The Sketcher's Appearance page shows\n"
"it. Takes effect at the next tool started.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getCursorCrosshairColor() {
    return instance()->CursorCrosshairColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultCursorCrosshairColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setCursorCrosshairColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("CursorCrosshairColor",v);
    instance()->CursorCrosshairColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeCursorCrosshairColor() {
    instance()->handle->RemoveUnsigned("CursorCrosshairColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHighlightColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pre-selection highlight color");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getHighlightColor() {
    return instance()->HighlightColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultHighlightColor() {
    const static unsigned long def = 0xE1E114FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHighlightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("HighlightColor",v);
    instance()->HighlightColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHighlightColor() {
    instance()->handle->RemoveUnsigned("HighlightColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Selection highlight color");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getSelectionColor() {
    return instance()->SelectionColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultSelectionColor() {
    const static unsigned long def = 0x1CAD1CFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("SelectionColor",v);
    instance()->SelectionColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionColor() {
    instance()->handle->RemoveUnsigned("SelectionColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docMarkerSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Size in pixels of the point markers drawn in the 3D view, such as\n"
"sketch vertices and the end points of a measurement.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getMarkerSize() {
    return instance()->MarkerSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultMarkerSize() {
    const static long def = 7;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setMarkerSize(const long &v) {
    instance()->handle->SetInt("MarkerSize",v);
    instance()->MarkerSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeMarkerSize() {
    instance()->handle->RemoveInt("MarkerSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultLinkColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default colour of the material of a new link, which is used when the\n"
"link overrides the material of its linked object.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getDefaultLinkColor() {
    return instance()->DefaultLinkColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultDefaultLinkColor() {
    const static unsigned long def = 0x66FFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultLinkColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultLinkColor",v);
    instance()->DefaultLinkColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultLinkColor() {
    instance()->handle->RemoveUnsigned("DefaultLinkColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultShapeLineColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default line colour of new shapes.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getDefaultShapeLineColor() {
    return instance()->DefaultShapeLineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultDefaultShapeLineColor() {
    const static unsigned long def = 0x191919FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultShapeLineColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultShapeLineColor",v);
    instance()->DefaultShapeLineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultShapeLineColor() {
    instance()->handle->RemoveUnsigned("DefaultShapeLineColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultShapeVertexColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default vertex colour of new shapes.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getDefaultShapeVertexColor() {
    return instance()->DefaultShapeVertexColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultDefaultShapeVertexColor() {
    const static unsigned long def = 0x191919FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultShapeVertexColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultShapeVertexColor",v);
    instance()->DefaultShapeVertexColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultShapeVertexColor() {
    instance()->handle->RemoveUnsigned("DefaultShapeVertexColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultShapeColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default face colour of new shapes. Not used while random colours are\n"
"turned on.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getDefaultShapeColor() {
    return instance()->DefaultShapeColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultDefaultShapeColor() {
    const static unsigned long def = 0xCCCCE6FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultShapeColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultShapeColor",v);
    instance()->DefaultShapeColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultShapeColor() {
    instance()->handle->RemoveUnsigned("DefaultShapeColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultShapeTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default transparency of new shapes in percent. 0 is opaque, 100 is\n"
"fully transparent.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getDefaultShapeTransparency() {
    return instance()->DefaultShapeTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultDefaultShapeTransparency() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultShapeTransparency(const long &v) {
    instance()->handle->SetInt("DefaultShapeTransparency",v);
    instance()->DefaultShapeTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultShapeTransparency() {
    instance()->handle->RemoveInt("DefaultShapeTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultShapeLineWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default line width of new shapes, in pixels.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getDefaultShapeLineWidth() {
    return instance()->DefaultShapeLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultDefaultShapeLineWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultShapeLineWidth(const long &v) {
    instance()->handle->SetInt("DefaultShapeLineWidth",v);
    instance()->DefaultShapeLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultShapeLineWidth() {
    instance()->handle->RemoveInt("DefaultShapeLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultShapePointSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default vertex size of new shapes, in pixels.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getDefaultShapePointSize() {
    return instance()->DefaultShapePointSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultDefaultShapePointSize() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultShapePointSize(const long &v) {
    instance()->handle->SetInt("DefaultShapePointSize",v);
    instance()->DefaultShapePointSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultShapePointSize() {
    instance()->handle->RemoveInt("DefaultShapePointSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docCoinCycleCheck() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Check the 3D scene for an object that contains itself while the\n"
"scene is traversed. A cycle is reported and skipped instead of being\n"
"followed without end.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getCoinCycleCheck() {
    return instance()->CoinCycleCheck;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultCoinCycleCheck() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setCoinCycleCheck(const bool &v) {
    instance()->handle->SetBool("CoinCycleCheck",v);
    instance()->CoinCycleCheck = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeCoinCycleCheck() {
    instance()->handle->RemoveBool("CoinCycleCheck");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEnablePropertyViewForInactiveDocument() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Keep the property view usable when the selected objects belong to a\n"
"document other than the active one. When off, the property view is\n"
"disabled for such a selection.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getEnablePropertyViewForInactiveDocument() {
    return instance()->EnablePropertyViewForInactiveDocument;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultEnablePropertyViewForInactiveDocument() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEnablePropertyViewForInactiveDocument(const bool &v) {
    instance()->handle->SetBool("EnablePropertyViewForInactiveDocument",v);
    instance()->EnablePropertyViewForInactiveDocument = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEnablePropertyViewForInactiveDocument() {
    instance()->handle->RemoveBool("EnablePropertyViewForInactiveDocument");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowSelectionBoundingBox() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show selection bounding box instead of highlight");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowSelectionBoundingBox() {
    return instance()->ShowSelectionBoundingBox;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowSelectionBoundingBox() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowSelectionBoundingBox(const bool &v) {
    instance()->handle->SetBool("ShowSelectionBoundingBox",v);
    instance()->ShowSelectionBoundingBox = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowSelectionBoundingBox() {
    instance()->handle->RemoveBool("ShowSelectionBoundingBox");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowSelectionBoundingBoxThreshold() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Threshold for showing bounding box instead of selection highlight");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getShowSelectionBoundingBoxThreshold() {
    return instance()->ShowSelectionBoundingBoxThreshold;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultShowSelectionBoundingBoxThreshold() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowSelectionBoundingBoxThreshold(const long &v) {
    instance()->handle->SetInt("ShowSelectionBoundingBoxThreshold",v);
    instance()->ShowSelectionBoundingBoxThreshold = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowSelectionBoundingBoxThreshold() {
    instance()->handle->RemoveInt("ShowSelectionBoundingBoxThreshold");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUpdateSelectionVisual() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Bring back the selection highlight of a selected object when it is\n"
"shown again after being hidden.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUpdateSelectionVisual() {
    return instance()->UpdateSelectionVisual;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUpdateSelectionVisual() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUpdateSelectionVisual(const bool &v) {
    instance()->handle->SetBool("UpdateSelectionVisual",v);
    instance()->UpdateSelectionVisual = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUpdateSelectionVisual() {
    instance()->handle->RemoveBool("UpdateSelectionVisual");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docLinkChildrenDirect() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show the children of a group with its own coordinate system, such as\n"
"a part or body, through a link view. A link to the group then shows\n"
"the children's visuals directly.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getLinkChildrenDirect() {
    return instance()->LinkChildrenDirect;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultLinkChildrenDirect() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setLinkChildrenDirect(const bool &v) {
    instance()->handle->SetBool("LinkChildrenDirect",v);
    instance()->LinkChildrenDirect = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeLinkChildrenDirect() {
    instance()->handle->RemoveBool("LinkChildrenDirect");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowSelectionOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show selection always on top");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowSelectionOnTop() {
    return instance()->ShowSelectionOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowSelectionOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowSelectionOnTop(const bool &v) {
    instance()->handle->SetBool("ShowSelectionOnTop",v);
    instance()->ShowSelectionOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowSelectionOnTop() {
    instance()->handle->RemoveBool("ShowSelectionOnTop");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowPreSelectedFaceOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show pre-selected face always on top");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowPreSelectedFaceOnTop() {
    return instance()->ShowPreSelectedFaceOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowPreSelectedFaceOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowPreSelectedFaceOnTop(const bool &v) {
    instance()->handle->SetBool("ShowPreSelectedFaceOnTop",v);
    instance()->ShowPreSelectedFaceOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowPreSelectedFaceOnTop() {
    instance()->handle->RemoveBool("ShowPreSelectedFaceOnTop");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowPreSelectedFaceOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show pre-selected face outline");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowPreSelectedFaceOutline() {
    return instance()->ShowPreSelectedFaceOutline;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowPreSelectedFaceOutline() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowPreSelectedFaceOutline(const bool &v) {
    instance()->handle->SetBool("ShowPreSelectedFaceOutline",v);
    instance()->ShowPreSelectedFaceOutline = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowPreSelectedFaceOutline() {
    instance()->handle->RemoveBool("ShowPreSelectedFaceOutline");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowSelectedFaceOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show selected face outline");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowSelectedFaceOutline() {
    return instance()->ShowSelectedFaceOutline;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowSelectedFaceOutline() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowSelectedFaceOutline(const bool &v) {
    instance()->handle->SetBool("ShowSelectedFaceOutline",v);
    instance()->ShowSelectedFaceOutline = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowSelectedFaceOutline() {
    instance()->handle->RemoveBool("ShowSelectedFaceOutline");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docOutlineThicken() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Muplication factor to increase outline width of the selected face.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getOutlineThicken() {
    return instance()->OutlineThicken;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultOutlineThicken() {
    const static double def = 4;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setOutlineThicken(const double &v) {
    instance()->handle->SetFloat("OutlineThicken",v);
    instance()->OutlineThicken = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeOutlineThicken() {
    instance()->handle->RemoveFloat("OutlineThicken");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docNoSelFaceHighlightWithOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Do not highlight selected face if outline is enabled");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getNoSelFaceHighlightWithOutline() {
    return instance()->NoSelFaceHighlightWithOutline;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultNoSelFaceHighlightWithOutline() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setNoSelFaceHighlightWithOutline(const bool &v) {
    instance()->handle->SetBool("NoSelFaceHighlightWithOutline",v);
    instance()->NoSelFaceHighlightWithOutline = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeNoSelFaceHighlightWithOutline() {
    instance()->handle->RemoveBool("NoSelFaceHighlightWithOutline");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docNoPreSelFaceHighlightWithOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Do not highlight pre-selected face if outline is enabled");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getNoPreSelFaceHighlightWithOutline() {
    return instance()->NoPreSelFaceHighlightWithOutline;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultNoPreSelFaceHighlightWithOutline() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setNoPreSelFaceHighlightWithOutline(const bool &v) {
    instance()->handle->SetBool("NoPreSelFaceHighlightWithOutline",v);
    instance()->NoPreSelFaceHighlightWithOutline = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeNoPreSelFaceHighlightWithOutline() {
    instance()->handle->RemoveBool("NoPreSelFaceHighlightWithOutline");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAutoTransparentPick() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Make pre-selected object transparent for picking hidden lines");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getAutoTransparentPick() {
    return instance()->AutoTransparentPick;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultAutoTransparentPick() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAutoTransparentPick(const bool &v) {
    instance()->handle->SetBool("AutoTransparentPick",v);
    instance()->AutoTransparentPick = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAutoTransparentPick() {
    instance()->handle->RemoveBool("AutoTransparentPick");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectElementOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Do box/lasso element selection on already selected objects if SelectionOnTop is enabled.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSelectElementOnTop() {
    return instance()->SelectElementOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSelectElementOnTop() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectElementOnTop(const bool &v) {
    instance()->handle->SetBool("SelectElementOnTop",v);
    instance()->SelectElementOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectElementOnTop() {
    instance()->handle->RemoveBool("SelectElementOnTop");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docTransparencyOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Transparency for the selected object when being shown on top.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getTransparencyOnTop() {
    return instance()->TransparencyOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultTransparencyOnTop() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setTransparencyOnTop(const double &v) {
    instance()->handle->SetFloat("TransparencyOnTop",v);
    instance()->TransparencyOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeTransparencyOnTop() {
    instance()->handle->RemoveFloat("TransparencyOnTop");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineSync() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies how to sync hidden line display style settings to opened document");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getHiddenLineSync() {
    return instance()->HiddenLineSync;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultHiddenLineSync() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineSync(const long &v) {
    instance()->handle->SetInt("HiddenLineSync",v);
    instance()->HiddenLineSync = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineSync() {
    instance()->handle->RemoveInt("HiddenLineSync");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineSelectionOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable hidden line/point selection when SelectionOnTop is active.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineSelectionOnTop() {
    return instance()->HiddenLineSelectionOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineSelectionOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineSelectionOnTop(const bool &v) {
    instance()->handle->SetBool("HiddenLineSelectionOnTop",v);
    instance()->HiddenLineSelectionOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineSelectionOnTop() {
    instance()->handle->RemoveBool("HiddenLineSelectionOnTop");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPartialHighlightOnFullSelect() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable partial highlight on full selection for object that supports it.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getPartialHighlightOnFullSelect() {
    return instance()->PartialHighlightOnFullSelect;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultPartialHighlightOnFullSelect() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPartialHighlightOnFullSelect(const bool &v) {
    instance()->handle->SetBool("PartialHighlightOnFullSelect",v);
    instance()->PartialHighlightOnFullSelect = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePartialHighlightOnFullSelect() {
    instance()->handle->RemoveBool("PartialHighlightOnFullSelect");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionLineThicken() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Muplication factor to increase the width of the selected line.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSelectionLineThicken() {
    return instance()->SelectionLineThicken;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSelectionLineThicken() {
    const static double def = 1.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionLineThicken(const double &v) {
    instance()->handle->SetFloat("SelectionLineThicken",v);
    instance()->SelectionLineThicken = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionLineThicken() {
    instance()->handle->RemoveFloat("SelectionLineThicken");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionLineMaxWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Limit the selected line width when applying line thickening.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSelectionLineMaxWidth() {
    return instance()->SelectionLineMaxWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSelectionLineMaxWidth() {
    const static double def = 4.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionLineMaxWidth(const double &v) {
    instance()->handle->SetFloat("SelectionLineMaxWidth",v);
    instance()->SelectionLineMaxWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionLineMaxWidth() {
    instance()->handle->RemoveFloat("SelectionLineMaxWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionPointScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Muplication factor to increase the size of the selected point.\n"
"If zero, then use line multiplication factor.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSelectionPointScale() {
    return instance()->SelectionPointScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSelectionPointScale() {
    const static double def = 2.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionPointScale(const double &v) {
    instance()->handle->SetFloat("SelectionPointScale",v);
    instance()->SelectionPointScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionPointScale() {
    instance()->handle->RemoveFloat("SelectionPointScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionPointMaxSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Limit the selected point size when applying size scale.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSelectionPointMaxSize() {
    return instance()->SelectionPointMaxSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSelectionPointMaxSize() {
    const static double def = 6.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionPointMaxSize(const double &v) {
    instance()->handle->SetFloat("SelectionPointMaxSize",v);
    instance()->SelectionPointMaxSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionPointMaxSize() {
    instance()->handle->RemoveFloat("SelectionPointMaxSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPickRadius() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Area for picking elements in 3D view. Larger value make it easy to pick things,\n"
"but can also make small features impossible to select.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getPickRadius() {
    return instance()->PickRadius;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultPickRadius() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPickRadius(const double &v) {
    instance()->handle->SetFloat("PickRadius",v);
    instance()->PickRadius = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePickRadius() {
    instance()->handle->RemoveFloat("PickRadius");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docTouchLoupeLift() {
    return QT_TRANSLATE_NOOP("ViewParams",
"How far above the fingertip the touch loupe picks, in CSS pixels.\n"
"The pick ring and its centre dot sit this far above the contact\n"
"point so the finger never covers what it is aiming at.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getTouchLoupeLift() {
    return instance()->TouchLoupeLift;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultTouchLoupeLift() {
    const static double def = 28.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setTouchLoupeLift(const double &v) {
    instance()->handle->SetFloat("TouchLoupeLift",v);
    instance()->TouchLoupeLift = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeTouchLoupeLift() {
    instance()->handle->RemoveFloat("TouchLoupeLift");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Transparency given to a selected face so that what lies behind it\n"
"stays visible, used when picking through objects and when the\n"
"highlight is drawn on top. 0 is opaque, 1 is invisible.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSelectionTransparency() {
    return instance()->SelectionTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSelectionTransparency() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionTransparency(const double &v) {
    instance()->handle->SetFloat("SelectionTransparency",v);
    instance()->SelectionTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionTransparency() {
    instance()->handle->RemoveFloat("SelectionTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionLinePattern() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Dash pattern of the hidden part of a selected line that is shown on\n"
"top of the scene, as a 16 bit mask. 0 draws it solid.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getSelectionLinePattern() {
    return instance()->SelectionLinePattern;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultSelectionLinePattern() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionLinePattern(const long &v) {
    instance()->handle->SetInt("SelectionLinePattern",v);
    instance()->SelectionLinePattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionLinePattern() {
    instance()->handle->RemoveInt("SelectionLinePattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionLinePatternScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Number of times each bit of the dash pattern of a selected hidden\n"
"line is repeated. Larger values give longer dashes. 1 or less uses\n"
"the pattern as it is.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getSelectionLinePatternScale() {
    return instance()->SelectionLinePatternScale;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultSelectionLinePatternScale() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionLinePatternScale(const long &v) {
    instance()->handle->SetInt("SelectionLinePatternScale",v);
    instance()->SelectionLinePatternScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionLinePatternScale() {
    instance()->handle->RemoveInt("SelectionLinePatternScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionHiddenLineWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Width of the hidden line.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSelectionHiddenLineWidth() {
    return instance()->SelectionHiddenLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSelectionHiddenLineWidth() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionHiddenLineWidth(const double &v) {
    instance()->handle->SetFloat("SelectionHiddenLineWidth",v);
    instance()->SelectionHiddenLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionHiddenLineWidth() {
    instance()->handle->RemoveFloat("SelectionHiddenLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionBBoxLineWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Line width in pixels of the bounding box drawn around a selected\n"
"object when selection is shown by bounding box.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSelectionBBoxLineWidth() {
    return instance()->SelectionBBoxLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSelectionBBoxLineWidth() {
    const static double def = 3.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionBBoxLineWidth(const double &v) {
    instance()->handle->SetFloat("SelectionBBoxLineWidth",v);
    instance()->SelectionBBoxLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionBBoxLineWidth() {
    instance()->handle->RemoveFloat("SelectionBBoxLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowHighlightEdgeOnly() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show pre-selection highlight edge only");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowHighlightEdgeOnly() {
    return instance()->ShowHighlightEdgeOnly;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowHighlightEdgeOnly() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowHighlightEdgeOnly(const bool &v) {
    instance()->handle->SetBool("ShowHighlightEdgeOnly",v);
    instance()->ShowHighlightEdgeOnly = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowHighlightEdgeOnly() {
    instance()->handle->RemoveBool("ShowHighlightEdgeOnly");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPreSelectionDelay() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Shortest time in seconds between two preselection picks in the 3D\n"
"view while the mouse moves. 0 picks on every mouse move.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getPreSelectionDelay() {
    return instance()->PreSelectionDelay;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultPreSelectionDelay() {
    const static double def = 0.1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPreSelectionDelay(const double &v) {
    instance()->handle->SetFloat("PreSelectionDelay",v);
    instance()->PreSelectionDelay = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePreSelectionDelay() {
    instance()->handle->RemoveFloat("PreSelectionDelay");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPickBackFaceDelay() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Number of mouse wheel steps, with Shift and Ctrl held, that it takes\n"
"to move the pick one object further behind or back toward the\n"
"front.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPickBackFaceDelay() {
    return instance()->PickBackFaceDelay;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPickBackFaceDelay() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPickBackFaceDelay(const long &v) {
    instance()->handle->SetInt("PickBackFaceDelay",v);
    instance()->PickBackFaceDelay = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePickBackFaceDelay() {
    instance()->handle->RemoveInt("PickBackFaceDelay");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseNewRayPick() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Stop a single pick in the 3D view at the nearest hit instead of\n"
"collecting everything along the pick ray. Off is the older and\n"
"slower way.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseNewRayPick() {
    return instance()->UseNewRayPick;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseNewRayPick() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseNewRayPick(const bool &v) {
    instance()->handle->SetBool("UseNewRayPick",v);
    instance()->UseNewRayPick = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseNewRayPick() {
    instance()->handle->RemoveBool("UseNewRayPick");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docViewSelectionExtendFactor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Scale applied to the bounding box of the selection when testing\n"
"whether it is already in view, before the view is extended to\n"
"include it. Currently has no effect.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getViewSelectionExtendFactor() {
    return instance()->ViewSelectionExtendFactor;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultViewSelectionExtendFactor() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setViewSelectionExtendFactor(const double &v) {
    instance()->handle->SetFloat("ViewSelectionExtendFactor",v);
    instance()->ViewSelectionExtendFactor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeViewSelectionExtendFactor() {
    instance()->handle->RemoveFloat("ViewSelectionExtendFactor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseTightBoundingBox() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show more accurate bounds when using bounding box selection style");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseTightBoundingBox() {
    return instance()->UseTightBoundingBox;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseTightBoundingBox() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseTightBoundingBox(const bool &v) {
    instance()->handle->SetBool("UseTightBoundingBox",v);
    instance()->UseTightBoundingBox = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseTightBoundingBox() {
    instance()->handle->RemoveBool("UseTightBoundingBox");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseBoundingBoxCache() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Remember the bounding boxes of objects instead of computing them\n"
"again on every request.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseBoundingBoxCache() {
    return instance()->UseBoundingBoxCache;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseBoundingBoxCache() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseBoundingBoxCache(const bool &v) {
    instance()->handle->SetBool("UseBoundingBoxCache",v);
    instance()->UseBoundingBoxCache = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseBoundingBoxCache() {
    instance()->handle->RemoveBool("UseBoundingBoxCache");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderProjectedBBox() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show projected bounding box that is aligned to axes of\n"
"global coordinate space");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getRenderProjectedBBox() {
    return instance()->RenderProjectedBBox;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultRenderProjectedBBox() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderProjectedBBox(const bool &v) {
    instance()->handle->SetBool("RenderProjectedBBox",v);
    instance()->RenderProjectedBBox = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderProjectedBBox() {
    instance()->handle->RemoveBool("RenderProjectedBBox");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionFaceWire() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show hidden tirangulation wires for selected face");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSelectionFaceWire() {
    return instance()->SelectionFaceWire;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSelectionFaceWire() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionFaceWire(const bool &v) {
    instance()->handle->SetBool("SelectionFaceWire",v);
    instance()->SelectionFaceWire = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionFaceWire() {
    instance()->handle->RemoveBool("SelectionFaceWire");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docNewDocumentCameraScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Camera zoom of a new document, as the diameter of the sphere that\n"
"fits on the screen. A quarter of it is the default size of a new\n"
"coordinate system.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getNewDocumentCameraScale() {
    return instance()->NewDocumentCameraScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultNewDocumentCameraScale() {
    const static double def = 100.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setNewDocumentCameraScale(const double &v) {
    instance()->handle->SetFloat("NewDocumentCameraScale",v);
    instance()->NewDocumentCameraScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeNewDocumentCameraScale() {
    instance()->handle->RemoveFloat("NewDocumentCameraScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docMaxOnTopSelections() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Largest number of selected objects that are drawn on top of the\n"
"scene. A larger selection is highlighted in place, and the tree view\n"
"does not expand to show its items.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getMaxOnTopSelections() {
    return instance()->MaxOnTopSelections;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultMaxOnTopSelections() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setMaxOnTopSelections(const long &v) {
    instance()->handle->SetInt("MaxOnTopSelections",v);
    instance()->MaxOnTopSelections = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeMaxOnTopSelections() {
    instance()->handle->RemoveInt("MaxOnTopSelections");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docMaxViewSelections() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Largest number of selected objects taken into account when the view\n"
"is fitted or aligned to the selection.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getMaxViewSelections() {
    return instance()->MaxViewSelections;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultMaxViewSelections() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setMaxViewSelections(const long &v) {
    instance()->handle->SetInt("MaxViewSelections",v);
    instance()->MaxViewSelections = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeMaxViewSelections() {
    instance()->handle->RemoveInt("MaxViewSelections");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docMaxSelectionNotification() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Number of pending add and remove selection notices after which they\n"
"are replaced by one notice that the whole selection changed. 0 sets\n"
"no limit.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getMaxSelectionNotification() {
    return instance()->MaxSelectionNotification;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultMaxSelectionNotification() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setMaxSelectionNotification(const long &v) {
    instance()->handle->SetInt("MaxSelectionNotification",v);
    instance()->MaxSelectionNotification = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeMaxSelectionNotification() {
    instance()->handle->RemoveInt("MaxSelectionNotification");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docMapChildrenPlacement() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Map child object into parent's coordinate space when showing on top.\n"
"Note that once activated, this option will also activate option ShowOnTop.\n"
"WARNING! This is an experimental option. Please use with caution.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getMapChildrenPlacement() {
    return instance()->MapChildrenPlacement;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultMapChildrenPlacement() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setMapChildrenPlacement(const bool &v) {
    instance()->handle->SetBool("MapChildrenPlacement",v);
    instance()->MapChildrenPlacement = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeMapChildrenPlacement() {
    instance()->handle->RemoveBool("MapChildrenPlacement");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEditingTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Automatically make all object transparent except the one in edit");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getEditingTransparency() {
    return instance()->EditingTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultEditingTransparency() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEditingTransparency(const double &v) {
    instance()->handle->SetFloat("EditingTransparency",v);
    instance()->EditingTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEditingTransparency() {
    instance()->handle->RemoveFloat("EditingTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPerViewEdit() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Run an edit in the one view it is started in. The other 3D views of\n"
"the document, and the other viewers of a served one, keep showing the\n"
"document as it is and take no input for the edit. When off, every view\n"
"of the document joins the edit and can work in it. Takes effect with\n"
"the next edit.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getPerViewEdit() {
    return instance()->PerViewEdit;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultPerViewEdit() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPerViewEdit(const bool &v) {
    instance()->handle->SetBool("PerViewEdit",v);
    instance()->PerViewEdit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePerViewEdit() {
    instance()->handle->RemoveBool("PerViewEdit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDraggerScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Size of the transform dragger relative to the viewport.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getDraggerScale() {
    return instance()->DraggerScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultDraggerScale() {
    const static double def = 0.03;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDraggerScale(const double &v) {
    instance()->handle->SetFloat("DraggerScale",v);
    instance()->DraggerScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDraggerScale() {
    instance()->handle->RemoveFloat("DraggerScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Overridden transparency value of all objects in the scene.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getHiddenLineTransparency() {
    return instance()->HiddenLineTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultHiddenLineTransparency() {
    const static double def = 0.4;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineTransparency(const double &v) {
    instance()->handle->SetFloat("HiddenLineTransparency",v);
    instance()->HiddenLineTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineTransparency() {
    instance()->handle->RemoveFloat("HiddenLineTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineOverrideTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to override transparency of all objects in the scene.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineOverrideTransparency() {
    return instance()->HiddenLineOverrideTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineOverrideTransparency() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineOverrideTransparency(const bool &v) {
    instance()->handle->SetBool("HiddenLineOverrideTransparency",v);
    instance()->HiddenLineOverrideTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineOverrideTransparency() {
    instance()->handle->RemoveBool("HiddenLineOverrideTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineFaceColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour all faces are drawn in by the hidden line display style when\n"
"it overrides the face colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getHiddenLineFaceColor() {
    return instance()->HiddenLineFaceColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultHiddenLineFaceColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineFaceColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("HiddenLineFaceColor",v);
    instance()->HiddenLineFaceColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineFaceColor() {
    instance()->handle->RemoveUnsigned("HiddenLineFaceColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineOverrideFaceColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable preselection and highlight by specified color.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineOverrideFaceColor() {
    return instance()->HiddenLineOverrideFaceColor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineOverrideFaceColor() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineOverrideFaceColor(const bool &v) {
    instance()->handle->SetBool("HiddenLineOverrideFaceColor",v);
    instance()->HiddenLineOverrideFaceColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineOverrideFaceColor() {
    instance()->handle->RemoveBool("HiddenLineOverrideFaceColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour all lines and outlines are drawn in by the hidden line\n"
"display style when it overrides the line colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getHiddenLineColor() {
    return instance()->HiddenLineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultHiddenLineColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("HiddenLineColor",v);
    instance()->HiddenLineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineColor() {
    instance()->handle->RemoveUnsigned("HiddenLineColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineOverrideColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable selection highlighting and use specified color");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineOverrideColor() {
    return instance()->HiddenLineOverrideColor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineOverrideColor() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineOverrideColor(const bool &v) {
    instance()->handle->SetBool("HiddenLineOverrideColor",v);
    instance()->HiddenLineOverrideColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineOverrideColor() {
    instance()->handle->RemoveBool("HiddenLineOverrideColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineBackground() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Background colour of a 3D view in the hidden line display style,\n"
"used when overriding the background is turned on.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getHiddenLineBackground() {
    return instance()->HiddenLineBackground;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultHiddenLineBackground() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineBackground(const unsigned long &v) {
    instance()->handle->SetUnsigned("HiddenLineBackground",v);
    instance()->HiddenLineBackground = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineBackground() {
    instance()->handle->RemoveUnsigned("HiddenLineBackground");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineOverrideBackground() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Replace the background of a 3D view with the hidden line background\n"
"colour while the hidden line display style is active.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineOverrideBackground() {
    return instance()->HiddenLineOverrideBackground;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineOverrideBackground() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineOverrideBackground(const bool &v) {
    instance()->handle->SetBool("HiddenLineOverrideBackground",v);
    instance()->HiddenLineOverrideBackground = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineOverrideBackground() {
    instance()->handle->RemoveBool("HiddenLineOverrideBackground");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineShaded() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to enable shading in hidden line display style");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineShaded() {
    return instance()->HiddenLineShaded;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineShaded() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineShaded(const bool &v) {
    instance()->handle->SetBool("HiddenLineShaded",v);
    instance()->HiddenLineShaded = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineShaded() {
    instance()->handle->RemoveBool("HiddenLineShaded");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineShowOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show outline in hidden line display style (only works in experiemental renderer),.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineShowOutline() {
    return instance()->HiddenLineShowOutline;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineShowOutline() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineShowOutline(const bool &v) {
    instance()->handle->SetBool("HiddenLineShowOutline",v);
    instance()->HiddenLineShowOutline = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineShowOutline() {
    instance()->handle->RemoveBool("HiddenLineShowOutline");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLinePerFaceOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Render per face outline in hidden line display style (Warning! this may cause slow down),.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLinePerFaceOutline() {
    return instance()->HiddenLinePerFaceOutline;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLinePerFaceOutline() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLinePerFaceOutline(const bool &v) {
    instance()->handle->SetBool("HiddenLinePerFaceOutline",v);
    instance()->HiddenLinePerFaceOutline = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLinePerFaceOutline() {
    instance()->handle->RemoveBool("HiddenLinePerFaceOutline");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineSceneOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Render outline of the whole scene.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineSceneOutline() {
    return instance()->HiddenLineSceneOutline;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineSceneOutline() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineSceneOutline(const bool &v) {
    instance()->handle->SetBool("HiddenLineSceneOutline",v);
    instance()->HiddenLineSceneOutline = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineSceneOutline() {
    instance()->handle->RemoveBool("HiddenLineSceneOutline");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineOutlineWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Width in pixels of the outlines drawn by the hidden line display\n"
"style. 0 uses the default width.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getHiddenLineOutlineWidth() {
    return instance()->HiddenLineOutlineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultHiddenLineOutlineWidth() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineOutlineWidth(const double &v) {
    instance()->handle->SetFloat("HiddenLineOutlineWidth",v);
    instance()->HiddenLineOutlineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineOutlineWidth() {
    instance()->handle->RemoveFloat("HiddenLineOutlineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Width in pixels of the lines of all objects in the hidden line\n"
"display style. A value below 1 keeps each object's own line width.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getHiddenLineWidth() {
    return instance()->HiddenLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultHiddenLineWidth() {
    const static double def = 1.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineWidth(const double &v) {
    instance()->handle->SetFloat("HiddenLineWidth",v);
    instance()->HiddenLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineWidth() {
    instance()->handle->RemoveFloat("HiddenLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLinePointSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Size in pixels of the vertices of all objects in the hidden line\n"
"display style. A value below 1 keeps each object's own point size.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getHiddenLinePointSize() {
    return instance()->HiddenLinePointSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultHiddenLinePointSize() {
    const static double def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLinePointSize(const double &v) {
    instance()->handle->SetFloat("HiddenLinePointSize",v);
    instance()->HiddenLinePointSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLinePointSize() {
    instance()->handle->RemoveFloat("HiddenLinePointSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineHideSeam() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Hide seam edges in hidden line display style.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineHideSeam() {
    return instance()->HiddenLineHideSeam;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineHideSeam() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineHideSeam(const bool &v) {
    instance()->handle->SetBool("HiddenLineHideSeam",v);
    instance()->HiddenLineHideSeam = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineHideSeam() {
    instance()->handle->RemoveBool("HiddenLineHideSeam");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineHideVertex() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Hide vertex in hidden line display style.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineHideVertex() {
    return instance()->HiddenLineHideVertex;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineHideVertex() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineHideVertex(const bool &v) {
    instance()->handle->SetBool("HiddenLineHideVertex",v);
    instance()->HiddenLineHideVertex = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineHideVertex() {
    instance()->handle->RemoveBool("HiddenLineHideVertex");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHiddenLineHideFace() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Hide face in hidden line display style.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHiddenLineHideFace() {
    return instance()->HiddenLineHideFace;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHiddenLineHideFace() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHiddenLineHideFace(const bool &v) {
    instance()->handle->SetBool("HiddenLineHideFace",v);
    instance()->HiddenLineHideFace = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHiddenLineHideFace() {
    instance()->handle->RemoveBool("HiddenLineHideFace");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docStatusMessageTimeout() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Milliseconds a message stays in the status bar when the command\n"
"showing it gives no time of its own. 0 keeps it until the next\n"
"message.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getStatusMessageTimeout() {
    return instance()->StatusMessageTimeout;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultStatusMessageTimeout() {
    const static long def = 5000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setStatusMessageTimeout(const long &v) {
    instance()->handle->SetInt("StatusMessageTimeout",v);
    instance()->StatusMessageTimeout = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeStatusMessageTimeout() {
    instance()->handle->RemoveInt("StatusMessageTimeout");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowSync() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies how to sync shadow display style settings to opened document");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getShadowSync() {
    return instance()->ShadowSync;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultShadowSync() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowSync(const long &v) {
    instance()->handle->SetInt("ShadowSync",v);
    instance()->ShadowSync = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowSync() {
    instance()->handle->RemoveInt("ShadowSync");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowFlatLines() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Draw object with 'Flat lines' style when shadow is enabled.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShadowFlatLines() {
    return instance()->ShadowFlatLines;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShadowFlatLines() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowFlatLines(const bool &v) {
    instance()->handle->SetBool("ShadowFlatLines",v);
    instance()->ShadowFlatLines = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowFlatLines() {
    instance()->handle->RemoveBool("ShadowFlatLines");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowDisplayMode() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Override view object display mode when shadow is enabled.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getShadowDisplayMode() {
    return instance()->ShadowDisplayMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultShadowDisplayMode() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowDisplayMode(const long &v) {
    instance()->handle->SetInt("ShadowDisplayMode",v);
    instance()->ShadowDisplayMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowDisplayMode() {
    instance()->handle->RemoveInt("ShadowDisplayMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowSpotLight() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to use spot light or directional light.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShadowSpotLight() {
    return instance()->ShadowSpotLight;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShadowSpotLight() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowSpotLight(const bool &v) {
    instance()->handle->SetBool("ShadowSpotLight",v);
    instance()->ShadowSpotLight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowSpotLight() {
    instance()->handle->RemoveBool("ShadowSpotLight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowLightIntensity() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Brightness of the light that casts the shadow.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowLightIntensity() {
    return instance()->ShadowLightIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowLightIntensity() {
    const static double def = 0.8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowLightIntensity(const double &v) {
    instance()->handle->SetFloat("ShadowLightIntensity",v);
    instance()->ShadowLightIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowLightIntensity() {
    instance()->handle->RemoveFloat("ShadowLightIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowLightDirectionX() {
    return QT_TRANSLATE_NOOP("ViewParams",
"X component of the direction of the light that casts the shadow.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowLightDirectionX() {
    return instance()->ShadowLightDirectionX;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowLightDirectionX() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowLightDirectionX(const double &v) {
    instance()->handle->SetFloat("ShadowLightDirectionX",v);
    instance()->ShadowLightDirectionX = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowLightDirectionX() {
    instance()->handle->RemoveFloat("ShadowLightDirectionX");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowLightDirectionY() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Y component of the direction of the light that casts the shadow.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowLightDirectionY() {
    return instance()->ShadowLightDirectionY;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowLightDirectionY() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowLightDirectionY(const double &v) {
    instance()->handle->SetFloat("ShadowLightDirectionY",v);
    instance()->ShadowLightDirectionY = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowLightDirectionY() {
    instance()->handle->RemoveFloat("ShadowLightDirectionY");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowLightDirectionZ() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Z component of the direction of the light that casts the shadow.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowLightDirectionZ() {
    return instance()->ShadowLightDirectionZ;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowLightDirectionZ() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowLightDirectionZ(const double &v) {
    instance()->handle->SetFloat("ShadowLightDirectionZ",v);
    instance()->ShadowLightDirectionZ = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowLightDirectionZ() {
    instance()->handle->RemoveFloat("ShadowLightDirectionZ");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowLightColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the light that casts the shadow.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getShadowLightColor() {
    return instance()->ShadowLightColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultShadowLightColor() {
    const static unsigned long def = 0xF0FDFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowLightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("ShadowLightColor",v);
    instance()->ShadowLightColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowLightColor() {
    instance()->handle->RemoveUnsigned("ShadowLightColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowShowGround() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to show auto generated ground face. You can specify you own ground\n"
"object by changing its view property 'ShadowStyle' to 'Shadowed', meaning\n"
"that it will only receive but not cast shadow.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShadowShowGround() {
    return instance()->ShadowShowGround;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShadowShowGround() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowShowGround(const bool &v) {
    instance()->handle->SetBool("ShadowShowGround",v);
    instance()->ShadowShowGround = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowShowGround() {
    instance()->handle->RemoveBool("ShadowShowGround");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowGroundBackFaceCull() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to show the ground when viewing from under the ground face");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShadowGroundBackFaceCull() {
    return instance()->ShadowGroundBackFaceCull;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShadowGroundBackFaceCull() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowGroundBackFaceCull(const bool &v) {
    instance()->handle->SetBool("ShadowGroundBackFaceCull",v);
    instance()->ShadowGroundBackFaceCull = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowGroundBackFaceCull() {
    instance()->handle->RemoveBool("ShadowGroundBackFaceCull");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowGroundScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"The auto generated ground face is determined by the scene bounding box\n"
"multiplied by this scale");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowGroundScale() {
    return instance()->ShadowGroundScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowGroundScale() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowGroundScale(const double &v) {
    instance()->handle->SetFloat("ShadowGroundScale",v);
    instance()->ShadowGroundScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowGroundScale() {
    instance()->handle->RemoveFloat("ShadowGroundScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowGroundColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the ground that receives the shadow.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getShadowGroundColor() {
    return instance()->ShadowGroundColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultShadowGroundColor() {
    const static unsigned long def = 0x7D7D7DFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowGroundColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("ShadowGroundColor",v);
    instance()->ShadowGroundColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowGroundColor() {
    instance()->handle->RemoveUnsigned("ShadowGroundColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowGroundBumpMap() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Image file used as a bump map that gives the shadow ground a surface\n"
"relief. Empty for a flat ground.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getShadowGroundBumpMap() {
    return instance()->ShadowGroundBumpMap;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultShadowGroundBumpMap() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowGroundBumpMap(const std::string &v) {
    instance()->handle->SetASCII("ShadowGroundBumpMap",v);
    instance()->ShadowGroundBumpMap = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowGroundBumpMap() {
    instance()->handle->RemoveASCII("ShadowGroundBumpMap");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowGroundTexture() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Image file drawn as a texture on the ground that receives the\n"
"shadow. Empty for a ground of plain colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getShadowGroundTexture() {
    return instance()->ShadowGroundTexture;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultShadowGroundTexture() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowGroundTexture(const std::string &v) {
    instance()->handle->SetASCII("ShadowGroundTexture",v);
    instance()->ShadowGroundTexture = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowGroundTexture() {
    instance()->handle->RemoveASCII("ShadowGroundTexture");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowGroundTextureSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the physcal length of the ground texture image size.\n"
"Texture mappings beyond this size will be wrapped around");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowGroundTextureSize() {
    return instance()->ShadowGroundTextureSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowGroundTextureSize() {
    const static double def = 100.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowGroundTextureSize(const double &v) {
    instance()->handle->SetFloat("ShadowGroundTextureSize",v);
    instance()->ShadowGroundTextureSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowGroundTextureSize() {
    instance()->handle->RemoveFloat("ShadowGroundTextureSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"How transparent the shadow itself is, where the ground carries the\n"
"shadow and nothing else (Ground transparency at 1). 0 paints a solid\n"
"shadow, 1 an invisible one; the unshadowed ground is hidden either\n"
"way. Coin spelled this SoShadowTransparency and defaulted it to the\n"
"same 0.2.\n"
"\n"
"A drawn ground ignores it -- there the shadow is the ground shaded,\n"
"and how dark it goes is a matter of the light.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowTransparency() {
    return instance()->ShadowTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowTransparency() {
    const static double def = 0.2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowTransparency(const double &v) {
    instance()->handle->SetFloat("ShadowTransparency",v);
    instance()->ShadowTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowTransparency() {
    instance()->handle->RemoveFloat("ShadowTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowGroundTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Transparency of the ground that receives the shadow. 1 (the default)\n"
"draws the shadow only, with no ground plane; lower values draw a\n"
"shaded ground of that transparency. A ground reflection always draws\n"
"the ground.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowGroundTransparency() {
    return instance()->ShadowGroundTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowGroundTransparency() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowGroundTransparency(const double &v) {
    instance()->handle->SetFloat("ShadowGroundTransparency",v);
    instance()->ShadowGroundTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowGroundTransparency() {
    instance()->handle->RemoveFloat("ShadowGroundTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowGroundShading() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Render ground with shading. If disabled, the ground and the shadow casted\n"
"on ground will not change shading when viewing in different angle.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShadowGroundShading() {
    return instance()->ShadowGroundShading;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShadowGroundShading() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowGroundShading(const bool &v) {
    instance()->handle->SetBool("ShadowGroundShading",v);
    instance()->ShadowGroundShading = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowGroundShading() {
    instance()->handle->RemoveBool("ShadowGroundShading");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowExtraRedraw() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Redraw the 3D view once more after a change while shadows are shown,\n"
"so that the shadow catches up with the scene. Currently has no\n"
"effect.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShadowExtraRedraw() {
    return instance()->ShadowExtraRedraw;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShadowExtraRedraw() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowExtraRedraw(const bool &v) {
    instance()->handle->SetBool("ShadowExtraRedraw",v);
    instance()->ShadowExtraRedraw = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowExtraRedraw() {
    instance()->handle->RemoveBool("ShadowExtraRedraw");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowSmoothBorder() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the blur raidus of the shadow edge. Higher number will result in\n"
"slower rendering speed on scene change. Use a lower 'Precision' value to\n"
"counter the effect.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getShadowSmoothBorder() {
    return instance()->ShadowSmoothBorder;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultShadowSmoothBorder() {
    const static long def = 40;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowSmoothBorder(const long &v) {
    instance()->handle->SetInt("ShadowSmoothBorder",v);
    instance()->ShadowSmoothBorder = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowSmoothBorder() {
    instance()->handle->RemoveInt("ShadowSmoothBorder");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowSpreadSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the spread size for a soft shadow. The resulting spread size is\n"
"dependent on the model scale");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getShadowSpreadSize() {
    return instance()->ShadowSpreadSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultShadowSpreadSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowSpreadSize(const long &v) {
    instance()->handle->SetInt("ShadowSpreadSize",v);
    instance()->ShadowSpreadSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowSpreadSize() {
    instance()->handle->RemoveInt("ShadowSpreadSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowSpreadSampleSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the sample size used for rendering shadow spread. A value 0\n"
"corresponds to a sampling square of 2x2. And 1 corresponds to 3x3, etc.\n"
"The bigger the size the slower the rendering speed. You can use a lower\n"
"'Precision' value to counter the effect.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getShadowSpreadSampleSize() {
    return instance()->ShadowSpreadSampleSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultShadowSpreadSampleSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowSpreadSampleSize(const long &v) {
    instance()->handle->SetInt("ShadowSpreadSampleSize",v);
    instance()->ShadowSpreadSampleSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowSpreadSampleSize() {
    instance()->handle->RemoveInt("ShadowSpreadSampleSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowPrecision() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies shadow precision. This parameter affects the internal texture\n"
"size used to hold the casted shadows. You might want a bigger texture if\n"
"you want a hard shadow but a smaller one for soft shadow.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowPrecision() {
    return instance()->ShadowPrecision;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowPrecision() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowPrecision(const double &v) {
    instance()->handle->SetFloat("ShadowPrecision",v);
    instance()->ShadowPrecision = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowPrecision() {
    instance()->handle->RemoveFloat("ShadowPrecision");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowEpsilon() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Epsilon is used to offset the shadow map depth from the model depth.\n"
"Should be set to as low a number as possible without causing flickering\n"
"in the shadows or on non-shadowed objects.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowEpsilon() {
    return instance()->ShadowEpsilon;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowEpsilon() {
    const static double def = 1e-05;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowEpsilon(const double &v) {
    instance()->handle->SetFloat("ShadowEpsilon",v);
    instance()->ShadowEpsilon = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowEpsilon() {
    instance()->handle->RemoveFloat("ShadowEpsilon");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowEpsilonMinimum() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Lower bound enforced on the shadow Epsilon (both the per-view\n"
"Shadow_Epsilon property constraint and the render-cache backend).\n"
"The variance shadow map needs a small non-zero epsilon or its\n"
"Chebyshev bound is numerically unstable and speckles the\n"
"self-shadowed side of curved surfaces. Zero disables the floor.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowEpsilonMinimum() {
    return instance()->ShadowEpsilonMinimum;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowEpsilonMinimum() {
    const static double def = 1e-06;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowEpsilonMinimum(const double &v) {
    instance()->handle->SetFloat("ShadowEpsilonMinimum",v);
    instance()->ShadowEpsilonMinimum = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowEpsilonMinimum() {
    instance()->handle->RemoveFloat("ShadowEpsilonMinimum");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowThreshold() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Can be used to avoid light bleeding in merged shadows cast from different objects.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowThreshold() {
    return instance()->ShadowThreshold;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowThreshold() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowThreshold(const double &v) {
    instance()->handle->SetFloat("ShadowThreshold",v);
    instance()->ShadowThreshold = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowThreshold() {
    instance()->handle->RemoveFloat("ShadowThreshold");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowBoundBoxScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Scene bounding box is used to determine the scale of the shadow texture.\n"
"You can increase the bounding box scale to avoid execessive clipping of\n"
"shadows when viewing up close in certain angle.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowBoundBoxScale() {
    return instance()->ShadowBoundBoxScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowBoundBoxScale() {
    const static double def = 1.2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowBoundBoxScale(const double &v) {
    instance()->handle->SetFloat("ShadowBoundBoxScale",v);
    instance()->ShadowBoundBoxScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowBoundBoxScale() {
    instance()->handle->RemoveFloat("ShadowBoundBoxScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowMaxDistance() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifics the clipping distance for when rendering shadows.\n"
"You can increase the bounding box scale to avoid execessive\n"
"clipping of shadows when viewing up close in certain angle.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getShadowMaxDistance() {
    return instance()->ShadowMaxDistance;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultShadowMaxDistance() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowMaxDistance(const double &v) {
    instance()->handle->SetFloat("ShadowMaxDistance",v);
    instance()->ShadowMaxDistance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowMaxDistance() {
    instance()->handle->RemoveFloat("ShadowMaxDistance");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowTransparentShadow() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to cast shadow from transparent objects.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShadowTransparentShadow() {
    return instance()->ShadowTransparentShadow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShadowTransparentShadow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowTransparentShadow(const bool &v) {
    instance()->handle->SetBool("ShadowTransparentShadow",v);
    instance()->ShadowTransparentShadow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowTransparentShadow() {
    instance()->handle->RemoveBool("ShadowTransparentShadow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShadowUpdateGround() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Auto update shadow ground on scene changes. You can manually\n"
"update the ground by using the 'Fit view' command");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShadowUpdateGround() {
    return instance()->ShadowUpdateGround;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShadowUpdateGround() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShadowUpdateGround(const bool &v) {
    instance()->handle->SetBool("ShadowUpdateGround",v);
    instance()->ShadowUpdateGround = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShadowUpdateGround() {
    instance()->handle->RemoveBool("ShadowUpdateGround");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPropertyViewTimer() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Milliseconds the property view waits before it refreshes after the\n"
"selection or a property changes.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getPropertyViewTimer() {
    return instance()->PropertyViewTimer;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultPropertyViewTimer() {
    const static unsigned long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPropertyViewTimer(const unsigned long &v) {
    instance()->handle->SetUnsigned("PropertyViewTimer",v);
    instance()->PropertyViewTimer = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePropertyViewTimer() {
    instance()->handle->RemoveUnsigned("PropertyViewTimer");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHierarchyAscend() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable selection of upper hierarchy by repeatedly click some already\n"
"selected sub-element.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getHierarchyAscend() {
    return instance()->HierarchyAscend;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultHierarchyAscend() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHierarchyAscend(const bool &v) {
    instance()->handle->SetBool("HierarchyAscend",v);
    instance()->HierarchyAscend = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHierarchyAscend() {
    instance()->handle->RemoveBool("HierarchyAscend");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docCommandHistorySize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum number of commands saved in history");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getCommandHistorySize() {
    return instance()->CommandHistorySize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultCommandHistorySize() {
    const static long def = 20;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setCommandHistorySize(const long &v) {
    instance()->handle->SetInt("CommandHistorySize",v);
    instance()->CommandHistorySize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeCommandHistorySize() {
    instance()->handle->RemoveInt("CommandHistorySize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuIconSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu icon size");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPieMenuIconSize() {
    return instance()->PieMenuIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPieMenuIconSize() {
    const static long def = 24;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuIconSize(const long &v) {
    instance()->handle->SetInt("PieMenuIconSize",v);
    instance()->PieMenuIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuIconSize() {
    instance()->handle->RemoveInt("PieMenuIconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuRadius() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu radius");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPieMenuRadius() {
    return instance()->PieMenuRadius;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPieMenuRadius() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuRadius(const long &v) {
    instance()->handle->SetInt("PieMenuRadius",v);
    instance()->PieMenuRadius = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuRadius() {
    instance()->handle->RemoveInt("PieMenuRadius");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuTriggerRadius() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu hover trigger radius");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPieMenuTriggerRadius() {
    return instance()->PieMenuTriggerRadius;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPieMenuTriggerRadius() {
    const static long def = 60;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuTriggerRadius(const long &v) {
    instance()->handle->SetInt("PieMenuTriggerRadius",v);
    instance()->PieMenuTriggerRadius = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuTriggerRadius() {
    instance()->handle->RemoveInt("PieMenuTriggerRadius");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuFontSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu font size");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPieMenuFontSize() {
    return instance()->PieMenuFontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPieMenuFontSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuFontSize(const long &v) {
    instance()->handle->SetInt("PieMenuFontSize",v);
    instance()->PieMenuFontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuFontSize() {
    instance()->handle->RemoveInt("PieMenuFontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuTriggerDelay() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu sub-menu hover trigger delay, 0 to disable");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPieMenuTriggerDelay() {
    return instance()->PieMenuTriggerDelay;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPieMenuTriggerDelay() {
    const static long def = 200;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuTriggerDelay(const long &v) {
    instance()->handle->SetInt("PieMenuTriggerDelay",v);
    instance()->PieMenuTriggerDelay = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuTriggerDelay() {
    instance()->handle->RemoveInt("PieMenuTriggerDelay");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuTriggerAction() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu action trigger on hover");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getPieMenuTriggerAction() {
    return instance()->PieMenuTriggerAction;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultPieMenuTriggerAction() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuTriggerAction(const bool &v) {
    instance()->handle->SetBool("PieMenuTriggerAction",v);
    instance()->PieMenuTriggerAction = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuTriggerAction() {
    instance()->handle->RemoveBool("PieMenuTriggerAction");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuAnimationDuration() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu animation duration, 0 to disable");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPieMenuAnimationDuration() {
    return instance()->PieMenuAnimationDuration;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPieMenuAnimationDuration() {
    const static long def = 250;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuAnimationDuration(const long &v) {
    instance()->handle->SetInt("PieMenuAnimationDuration",v);
    instance()->PieMenuAnimationDuration = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuAnimationDuration() {
    instance()->handle->RemoveInt("PieMenuAnimationDuration");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuAnimationCurve() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu animation curve type");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPieMenuAnimationCurve() {
    return instance()->PieMenuAnimationCurve;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPieMenuAnimationCurve() {
    const static long def = 38;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuAnimationCurve(const long &v) {
    instance()->handle->SetInt("PieMenuAnimationCurve",v);
    instance()->PieMenuAnimationCurve = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuAnimationCurve() {
    instance()->handle->RemoveInt("PieMenuAnimationCurve");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuCenterRadius() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu center circle radius, 0 to disable");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPieMenuCenterRadius() {
    return instance()->PieMenuCenterRadius;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPieMenuCenterRadius() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuCenterRadius(const long &v) {
    instance()->handle->SetInt("PieMenuCenterRadius",v);
    instance()->PieMenuCenterRadius = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuCenterRadius() {
    instance()->handle->RemoveInt("PieMenuCenterRadius");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPieMenuPopup() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show pie menu as a popup widget, disable it to work around some graphics driver problem");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getPieMenuPopup() {
    return instance()->PieMenuPopup;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultPieMenuPopup() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPieMenuPopup(const bool &v) {
    instance()->handle->SetBool("PieMenuPopup",v);
    instance()->PieMenuPopup = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePieMenuPopup() {
    instance()->handle->RemoveBool("PieMenuPopup");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docStickyTaskControl() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Makes the task dialog buttons stay at top or bottom of task view.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getStickyTaskControl() {
    return instance()->StickyTaskControl;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultStickyTaskControl() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setStickyTaskControl(const bool &v) {
    instance()->handle->SetBool("StickyTaskControl",v);
    instance()->StickyTaskControl = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeStickyTaskControl() {
    instance()->handle->RemoveBool("StickyTaskControl");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docColorOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show object on top when editing its color.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getColorOnTop() {
    return instance()->ColorOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultColorOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setColorOnTop(const bool &v) {
    instance()->handle->SetBool("ColorOnTop",v);
    instance()->ColorOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeColorOnTop() {
    instance()->handle->RemoveBool("ColorOnTop");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAutoSortWBList() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Sort workbench entries by their names in the combo box.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getAutoSortWBList() {
    return instance()->AutoSortWBList;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultAutoSortWBList() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAutoSortWBList(const bool &v) {
    instance()->handle->SetBool("AutoSortWBList",v);
    instance()->AutoSortWBList = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAutoSortWBList() {
    instance()->handle->RemoveBool("AutoSortWBList");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docMaxCameraAnimatePeriod() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum camera move animation duration in milliseconds.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getMaxCameraAnimatePeriod() {
    return instance()->MaxCameraAnimatePeriod;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultMaxCameraAnimatePeriod() {
    const static long def = 3000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setMaxCameraAnimatePeriod(const long &v) {
    instance()->handle->SetInt("MaxCameraAnimatePeriod",v);
    instance()->MaxCameraAnimatePeriod = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeMaxCameraAnimatePeriod() {
    instance()->handle->RemoveInt("MaxCameraAnimatePeriod");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docTaskNoWheelFocus() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Do not accept wheel focus on input fields in task panels.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getTaskNoWheelFocus() {
    return instance()->TaskNoWheelFocus;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultTaskNoWheelFocus() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setTaskNoWheelFocus(const bool &v) {
    instance()->handle->SetBool("TaskNoWheelFocus",v);
    instance()->TaskNoWheelFocus = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeTaskNoWheelFocus() {
    instance()->handle->RemoveBool("TaskNoWheelFocus");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docGestureLongPressRotationCenter() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Set rotation center on press in gesture navigation mode.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getGestureLongPressRotationCenter() {
    return instance()->GestureLongPressRotationCenter;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultGestureLongPressRotationCenter() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setGestureLongPressRotationCenter(const bool &v) {
    instance()->handle->SetBool("GestureLongPressRotationCenter",v);
    instance()->GestureLongPressRotationCenter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeGestureLongPressRotationCenter() {
    instance()->handle->RemoveBool("GestureLongPressRotationCenter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docCheckWidgetPlacementOnRestore() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Check widget position and size on restore to make sure it is within the current screen.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getCheckWidgetPlacementOnRestore() {
    return instance()->CheckWidgetPlacementOnRestore;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultCheckWidgetPlacementOnRestore() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setCheckWidgetPlacementOnRestore(const bool &v) {
    instance()->handle->SetBool("CheckWidgetPlacementOnRestore",v);
    instance()->CheckWidgetPlacementOnRestore = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeCheckWidgetPlacementOnRestore() {
    instance()->handle->RemoveBool("CheckWidgetPlacementOnRestore");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docTextCursorWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Text cursor width in pixel.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getTextCursorWidth() {
    return instance()->TextCursorWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultTextCursorWidth() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setTextCursorWidth(const long &v) {
    instance()->handle->SetInt("TextCursorWidth",v);
    instance()->TextCursorWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeTextCursorWidth() {
    instance()->handle->RemoveInt("TextCursorWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPreselectionToolTipCorner() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Preselection tool tip docking corner.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPreselectionToolTipCorner() {
    return instance()->PreselectionToolTipCorner;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPreselectionToolTipCorner() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPreselectionToolTipCorner(const long &v) {
    instance()->handle->SetInt("PreselectionToolTipCorner",v);
    instance()->PreselectionToolTipCorner = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePreselectionToolTipCorner() {
    instance()->handle->RemoveInt("PreselectionToolTipCorner");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPreselectionToolTipOffsetX() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Preselection tool tip x offset relative to its docking corner.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPreselectionToolTipOffsetX() {
    return instance()->PreselectionToolTipOffsetX;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPreselectionToolTipOffsetX() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPreselectionToolTipOffsetX(const long &v) {
    instance()->handle->SetInt("PreselectionToolTipOffsetX",v);
    instance()->PreselectionToolTipOffsetX = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePreselectionToolTipOffsetX() {
    instance()->handle->RemoveInt("PreselectionToolTipOffsetX");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPreselectionToolTipOffsetY() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Preselection tool tip y offset relative to its docking corner.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPreselectionToolTipOffsetY() {
    return instance()->PreselectionToolTipOffsetY;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPreselectionToolTipOffsetY() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPreselectionToolTipOffsetY(const long &v) {
    instance()->handle->SetInt("PreselectionToolTipOffsetY",v);
    instance()->PreselectionToolTipOffsetY = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePreselectionToolTipOffsetY() {
    instance()->handle->RemoveInt("PreselectionToolTipOffsetY");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPreselectionToolTipFontSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Preselection tool tip font size. Set to 0 to use system default.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getPreselectionToolTipFontSize() {
    return instance()->PreselectionToolTipFontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultPreselectionToolTipFontSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPreselectionToolTipFontSize(const long &v) {
    instance()->handle->SetInt("PreselectionToolTipFontSize",v);
    instance()->PreselectionToolTipFontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePreselectionToolTipFontSize() {
    instance()->handle->RemoveInt("PreselectionToolTipFontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSectionFill() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Fill cross section plane.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSectionFill() {
    return instance()->SectionFill;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSectionFill() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSectionFill(const bool &v) {
    instance()->handle->SetBool("SectionFill",v);
    instance()->SectionFill = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSectionFill() {
    instance()->handle->RemoveBool("SectionFill");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSectionFillInvert() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Invert cross section plane fill color.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSectionFillInvert() {
    return instance()->SectionFillInvert;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSectionFillInvert() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSectionFillInvert(const bool &v) {
    instance()->handle->SetBool("SectionFillInvert",v);
    instance()->SectionFillInvert = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSectionFillInvert() {
    instance()->handle->RemoveBool("SectionFillInvert");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSectionConcave() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Cross section in concave.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSectionConcave() {
    return instance()->SectionConcave;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSectionConcave() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSectionConcave(const bool &v) {
    instance()->handle->SetBool("SectionConcave",v);
    instance()->SectionConcave = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSectionConcave() {
    instance()->handle->RemoveBool("SectionConcave");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docNoSectionOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Ignore section clip planes when rendering on top.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getNoSectionOnTop() {
    return instance()->NoSectionOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultNoSectionOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setNoSectionOnTop(const bool &v) {
    instance()->handle->SetBool("NoSectionOnTop",v);
    instance()->NoSectionOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeNoSectionOnTop() {
    instance()->handle->RemoveBool("NoSectionOnTop");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSectionHatchTextureScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Section filling texture image scale.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSectionHatchTextureScale() {
    return instance()->SectionHatchTextureScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSectionHatchTextureScale() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSectionHatchTextureScale(const double &v) {
    instance()->handle->SetFloat("SectionHatchTextureScale",v);
    instance()->SectionHatchTextureScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSectionHatchTextureScale() {
    instance()->handle->RemoveFloat("SectionHatchTextureScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSectionHatchTexture() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Section filling texture image path.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getSectionHatchTexture() {
    return instance()->SectionHatchTexture;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultSectionHatchTexture() {
    const static std::string def = ":icons/section-hatch.png";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSectionHatchTexture(const std::string &v) {
    instance()->handle->SetASCII("SectionHatchTexture",v);
    instance()->SectionHatchTexture = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSectionHatchTexture() {
    instance()->handle->RemoveASCII("SectionHatchTexture");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSectionHatchTextureEnable() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable section fill texture.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSectionHatchTextureEnable() {
    return instance()->SectionHatchTextureEnable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSectionHatchTextureEnable() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSectionHatchTextureEnable(const bool &v) {
    instance()->handle->SetBool("SectionHatchTextureEnable",v);
    instance()->SectionHatchTextureEnable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSectionHatchTextureEnable() {
    instance()->handle->RemoveBool("SectionHatchTextureEnable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSectionFillGroup() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Render cross section filling of objects with similar materials together.\n"
"Intersecting objects will act as boolean cut operation");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSectionFillGroup() {
    return instance()->SectionFillGroup;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSectionFillGroup() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSectionFillGroup(const bool &v) {
    instance()->handle->SetBool("SectionFillGroup",v);
    instance()->SectionFillGroup = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSectionFillGroup() {
    instance()->handle->RemoveBool("SectionFillGroup");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowClipPlane() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show clip plane");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowClipPlane() {
    return instance()->ShowClipPlane;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowClipPlane() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowClipPlane(const bool &v) {
    instance()->handle->SetBool("ShowClipPlane",v);
    instance()->ShowClipPlane = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowClipPlane() {
    instance()->handle->RemoveBool("ShowClipPlane");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docClipPlaneSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Clip plane visual size");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getClipPlaneSize() {
    return instance()->ClipPlaneSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultClipPlaneSize() {
    const static double def = 40.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setClipPlaneSize(const double &v) {
    instance()->handle->SetFloat("ClipPlaneSize",v);
    instance()->ClipPlaneSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeClipPlaneSize() {
    instance()->handle->RemoveFloat("ClipPlaneSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docClipPlaneColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Clip plane color");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getClipPlaneColor() {
    return instance()->ClipPlaneColor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultClipPlaneColor() {
    const static std::string def = "cyan";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setClipPlaneColor(const std::string &v) {
    instance()->handle->SetASCII("ClipPlaneColor",v);
    instance()->ClipPlaneColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeClipPlaneColor() {
    instance()->handle->RemoveASCII("ClipPlaneColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docClipPlaneLineWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Clip plane line width");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getClipPlaneLineWidth() {
    return instance()->ClipPlaneLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultClipPlaneLineWidth() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setClipPlaneLineWidth(const double &v) {
    instance()->handle->SetFloat("ClipPlaneLineWidth",v);
    instance()->ClipPlaneLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeClipPlaneLineWidth() {
    instance()->handle->RemoveFloat("ClipPlaneLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docTransformOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show an object on top of the scene while it is moved with the\n"
"transform dragger. Currently has no effect.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getTransformOnTop() {
    return instance()->TransformOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultTransformOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setTransformOnTop(const bool &v) {
    instance()->handle->SetBool("TransformOnTop",v);
    instance()->TransformOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeTransformOnTop() {
    instance()->handle->RemoveBool("TransformOnTop");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionColorDifference() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Color difference threshold for auto making distinct\n"
"selection highlight color");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSelectionColorDifference() {
    return instance()->SelectionColorDifference;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSelectionColorDifference() {
    const static double def = 25.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionColorDifference(const double &v) {
    instance()->handle->SetFloat("SelectionColorDifference",v);
    instance()->SelectionColorDifference = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionColorDifference() {
    instance()->handle->RemoveFloat("SelectionColorDifference");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderCacheMergeCount() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Merge draw caches of multiple objects to reduce number of draw\n"
"calls and improve render performance. Set zero to disable. Only\n"
"effective when using experimental render cache.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRenderCacheMergeCount() {
    return instance()->RenderCacheMergeCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRenderCacheMergeCount() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderCacheMergeCount(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeCount",v);
    instance()->RenderCacheMergeCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderCacheMergeCount() {
    instance()->handle->RemoveInt("RenderCacheMergeCount");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderCacheMergeCountMin() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Internal use to limit the render cache merge count");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRenderCacheMergeCountMin() {
    return instance()->RenderCacheMergeCountMin;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRenderCacheMergeCountMin() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderCacheMergeCountMin(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeCountMin",v);
    instance()->RenderCacheMergeCountMin = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderCacheMergeCountMin() {
    instance()->handle->RemoveInt("RenderCacheMergeCountMin");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderCacheMergeCountMax() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum draw crash merges on any hierarchy. Zero means no limit.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRenderCacheMergeCountMax() {
    return instance()->RenderCacheMergeCountMax;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRenderCacheMergeCountMax() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderCacheMergeCountMax(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeCountMax",v);
    instance()->RenderCacheMergeCountMax = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderCacheMergeCountMax() {
    instance()->handle->RemoveInt("RenderCacheMergeCountMax");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderCacheMergeDepthMax() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum hierarchy depth that the cache merge can happen. Less than 0 means no limit.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRenderCacheMergeDepthMax() {
    return instance()->RenderCacheMergeDepthMax;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRenderCacheMergeDepthMax() {
    const static long def = -1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderCacheMergeDepthMax(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeDepthMax",v);
    instance()->RenderCacheMergeDepthMax = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderCacheMergeDepthMax() {
    instance()->handle->RemoveInt("RenderCacheMergeDepthMax");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderCacheMergeDepthMin() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Minimum hierarchy depth that the cache merge can happen.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRenderCacheMergeDepthMin() {
    return instance()->RenderCacheMergeDepthMin;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRenderCacheMergeDepthMin() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderCacheMergeDepthMin(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeDepthMin",v);
    instance()->RenderCacheMergeDepthMin = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderCacheMergeDepthMin() {
    instance()->handle->RemoveInt("RenderCacheMergeDepthMin");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderCacheKeepMax() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Largest render cache, in entries, an object keeps after its parent has\n"
"merged it. Keeping the small ones avoids rebuilding them every frame.\n"
"0 keeps none.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRenderCacheKeepMax() {
    return instance()->RenderCacheKeepMax;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRenderCacheKeepMax() {
    const static long def = 32;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderCacheKeepMax(const long &v) {
    instance()->handle->SetInt("RenderCacheKeepMax",v);
    instance()->RenderCacheKeepMax = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderCacheKeepMax() {
    instance()->handle->RemoveInt("RenderCacheKeepMax");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderCacheIncremental() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Update a container's render cache from its previous one instead of\n"
"merging all its children again. Faster on large assemblies, at the\n"
"cost of some memory. 0 off, 1 on, 2 does both and logs any difference\n"
"(slow, for checking).");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRenderCacheIncremental() {
    return instance()->RenderCacheIncremental;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRenderCacheIncremental() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderCacheIncremental(const long &v) {
    instance()->handle->SetInt("RenderCacheIncremental",v);
    instance()->RenderCacheIncremental = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderCacheIncremental() {
    instance()->handle->RemoveInt("RenderCacheIncremental");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderCacheMeshReuse() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Reuse the mesh a render cache was converted to for the render backend\n"
"instead of converting it at every update. 0 off, 1 on, 2 does both and\n"
"logs any difference (slow, for checking).");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRenderCacheMeshReuse() {
    return instance()->RenderCacheMeshReuse;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRenderCacheMeshReuse() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderCacheMeshReuse(const long &v) {
    instance()->handle->SetInt("RenderCacheMeshReuse",v);
    instance()->RenderCacheMeshReuse = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderCacheMeshReuse() {
    instance()->handle->RemoveInt("RenderCacheMeshReuse");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docLiveImportRedrawInterval() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Minimum interval in milliseconds between 3D view redraws while a\n"
"progressive import is filling the document, and the window after\n"
"any mouse input during which redraws are never held back. Set zero\n"
"to redraw on every change.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getLiveImportRedrawInterval() {
    return instance()->LiveImportRedrawInterval;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultLiveImportRedrawInterval() {
    const static long def = 200;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setLiveImportRedrawInterval(const long &v) {
    instance()->handle->SetInt("LiveImportRedrawInterval",v);
    instance()->LiveImportRedrawInterval = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeLiveImportRedrawInterval() {
    instance()->handle->RemoveInt("LiveImportRedrawInterval");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docLiveImportRedrawBudget() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Percentage of time the 3D view may spend redrawing while a progressive\n"
"import fills the document. The wait between frames is never shorter\n"
"than LiveImportRedrawInterval nor longer than ten times it. 0 uses the\n"
"plain interval.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getLiveImportRedrawBudget() {
    return instance()->LiveImportRedrawBudget;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultLiveImportRedrawBudget() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setLiveImportRedrawBudget(const long &v) {
    instance()->handle->SetInt("LiveImportRedrawBudget",v);
    instance()->LiveImportRedrawBudget = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeLiveImportRedrawBudget() {
    instance()->handle->RemoveInt("LiveImportRedrawBudget");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docLiveImportPumpInterval() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Minimum milliseconds between two chances for the window to process\n"
"input and repaint while a live import fills the document. 0 offers one\n"
"at every opportunity.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getLiveImportPumpInterval() {
    return instance()->LiveImportPumpInterval;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultLiveImportPumpInterval() {
    const static long def = 50;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setLiveImportPumpInterval(const long &v) {
    instance()->handle->SetInt("LiveImportPumpInterval",v);
    instance()->LiveImportPumpInterval = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeLiveImportPumpInterval() {
    instance()->handle->RemoveInt("LiveImportPumpInterval");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderHighlightPolygonOffsetFactor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Slope scaled depth offset that pulls selection and preselection\n"
"highlights toward the viewer, so that the faces under them do not\n"
"hide them. Preselection gets twice the offset.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getRenderHighlightPolygonOffsetFactor() {
    return instance()->RenderHighlightPolygonOffsetFactor;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultRenderHighlightPolygonOffsetFactor() {
    const static double def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderHighlightPolygonOffsetFactor(const double &v) {
    instance()->handle->SetFloat("RenderHighlightPolygonOffsetFactor",v);
    instance()->RenderHighlightPolygonOffsetFactor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderHighlightPolygonOffsetFactor() {
    instance()->handle->RemoveFloat("RenderHighlightPolygonOffsetFactor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRenderHighlightPolygonOffsetUnits() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Constant depth offset, in depth buffer units, that pulls selection\n"
"and preselection highlights toward the viewer, so that the faces\n"
"under them do not hide them. Preselection gets twice the offset.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getRenderHighlightPolygonOffsetUnits() {
    return instance()->RenderHighlightPolygonOffsetUnits;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultRenderHighlightPolygonOffsetUnits() {
    const static double def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRenderHighlightPolygonOffsetUnits(const double &v) {
    instance()->handle->SetFloat("RenderHighlightPolygonOffsetUnits",v);
    instance()->RenderHighlightPolygonOffsetUnits = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRenderHighlightPolygonOffsetUnits() {
    instance()->handle->RemoveFloat("RenderHighlightPolygonOffsetUnits");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docForceSolidSingleSideLighting() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Force single side lighting on solid. This can help visualizing invalid\n"
"solid shapes with flipped normals.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getForceSolidSingleSideLighting() {
    return instance()->ForceSolidSingleSideLighting;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultForceSolidSingleSideLighting() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setForceSolidSingleSideLighting(const bool &v) {
    instance()->handle->SetBool("ForceSolidSingleSideLighting",v);
    instance()->ForceSolidSingleSideLighting = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeForceSolidSingleSideLighting() {
    instance()->handle->RemoveBool("ForceSolidSingleSideLighting");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultFontSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Point size of the application font. 0 uses the system default.\n"
"Sizes from 1 to 7 are raised to 8.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getDefaultFontSize() {
    return instance()->DefaultFontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultDefaultFontSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultFontSize(const long &v) {
    instance()->handle->SetInt("DefaultFontSize",v);
    instance()->DefaultFontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultFontSize() {
    instance()->handle->RemoveInt("DefaultFontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEnableTaskPanelKeyTranslate() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Let the Up and Down arrow keys move the keyboard focus through the\n"
"task panel, the way Shift+Tab and Tab do.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getEnableTaskPanelKeyTranslate() {
    return instance()->EnableTaskPanelKeyTranslate;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultEnableTaskPanelKeyTranslate() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEnableTaskPanelKeyTranslate(const bool &v) {
    instance()->handle->SetBool("EnableTaskPanelKeyTranslate",v);
    instance()->EnableTaskPanelKeyTranslate = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEnableTaskPanelKeyTranslate() {
    instance()->handle->RemoveBool("EnableTaskPanelKeyTranslate");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEnableMenuBarCheckBox() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show the entries of the toolbar and dock window menus of the menu\n"
"bar as checkboxes, the way the right-click menu of the main window\n"
"shows them. Off by default on macOS.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getEnableMenuBarCheckBox() {
    return instance()->EnableMenuBarCheckBox;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultEnableMenuBarCheckBox() {
    const static bool def = FC_ENABLE_MENUBAR_CHECKBOX;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEnableMenuBarCheckBox(const bool &v) {
    instance()->handle->SetBool("EnableMenuBarCheckBox",v);
    instance()->EnableMenuBarCheckBox = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEnableMenuBarCheckBox() {
    instance()->handle->RemoveBool("EnableMenuBarCheckBox");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEnableBacklight() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Turn on the backlight of the 3D view, a second light that shines on\n"
"the faces turned away from the viewer.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getEnableBacklight() {
    return instance()->EnableBacklight;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultEnableBacklight() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEnableBacklight(const bool &v) {
    instance()->handle->SetBool("EnableBacklight",v);
    instance()->EnableBacklight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEnableBacklight() {
    instance()->handle->RemoveBool("EnableBacklight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docBacklightColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the backlight, the light that shines on the faces turned\n"
"away from the viewer.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getBacklightColor() {
    return instance()->BacklightColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultBacklightColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setBacklightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("BacklightColor",v);
    instance()->BacklightColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeBacklightColor() {
    instance()->handle->RemoveUnsigned("BacklightColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docBacklightIntensity() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Backlight intensity, as a percentage. An integer because that is the\n"
"slot everything else uses: the Clipping dialog's slider, the 3D view\n"
"preference page and the viewer, which divides it by a hundred. This\n"
"class used to read a Float fraction from the same name -- a second,\n"
"separate slot that nothing ever wrote; see ViewParams::migrate().");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getBacklightIntensity() {
    return instance()->BacklightIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultBacklightIntensity() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setBacklightIntensity(const long &v) {
    instance()->handle->SetInt("BacklightIntensity",v);
    instance()->BacklightIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeBacklightIntensity() {
    instance()->handle->RemoveInt("BacklightIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docOverrideSelectability() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Override object selectability to enable selection");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getOverrideSelectability() {
    return instance()->OverrideSelectability;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultOverrideSelectability() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setOverrideSelectability(const bool &v) {
    instance()->handle->SetBool("OverrideSelectability",v);
    instance()->OverrideSelectability = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeOverrideSelectability() {
    instance()->handle->RemoveBool("OverrideSelectability");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSelectionStackSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum selection history record size");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getSelectionStackSize() {
    return instance()->SelectionStackSize;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultSelectionStackSize() {
    const static unsigned long def = 30;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSelectionStackSize(const unsigned long &v) {
    instance()->handle->SetUnsigned("SelectionStackSize",v);
    instance()->SelectionStackSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSelectionStackSize() {
    instance()->handle->RemoveUnsigned("SelectionStackSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultDrawStyle() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default display style of a new document");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getDefaultDrawStyle() {
    return instance()->DefaultDrawStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultDefaultDrawStyle() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultDrawStyle(const long &v) {
    instance()->handle->SetInt("DefaultDrawStyle",v);
    instance()->DefaultDrawStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultDrawStyle() {
    instance()->handle->RemoveInt("DefaultDrawStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docToolTipIconSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the size of static icon image in tooltip. GIF animation\n"
"will be shown in its original size. You can disable all images in\n"
"the tooltip by setting this option to zero.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getToolTipIconSize() {
    return instance()->ToolTipIconSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultToolTipIconSize() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setToolTipIconSize(const long &v) {
    instance()->handle->SetInt("ToolTipIconSize",v);
    instance()->ToolTipIconSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeToolTipIconSize() {
    instance()->handle->RemoveInt("ToolTipIconSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docToolTipDisable() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Turn off the tool tips of the application. Tips shown as an overlay\n"
"in the 3D view, such as the preselection tip, still appear.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getToolTipDisable() {
    return instance()->ToolTipDisable;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultToolTipDisable() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setToolTipDisable(const bool &v) {
    instance()->handle->SetBool("ToolTipDisable",v);
    instance()->ToolTipDisable = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeToolTipDisable() {
    instance()->handle->RemoveBool("ToolTipDisable");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAxisXColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the X axis of the transform dragger and of other axis\n"
"markers in the 3D view.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getAxisXColor() {
    return instance()->AxisXColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultAxisXColor() {
    const static unsigned long def = 0xCC333300;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAxisXColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AxisXColor",v);
    instance()->AxisXColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAxisXColor() {
    instance()->handle->RemoveUnsigned("AxisXColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAxisYColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the Y axis of the transform dragger and of other axis\n"
"markers in the 3D view.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getAxisYColor() {
    return instance()->AxisYColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultAxisYColor() {
    const static unsigned long def = 0x33CC3300;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAxisYColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AxisYColor",v);
    instance()->AxisYColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAxisYColor() {
    instance()->handle->RemoveUnsigned("AxisYColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAxisZColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the Z axis of the transform dragger and of other axis\n"
"markers in the 3D view.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getAxisZColor() {
    return instance()->AxisZColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultAxisZColor() {
    const static unsigned long def = 0x3333CC00;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAxisZColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AxisZColor",v);
    instance()->AxisZColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAxisZColor() {
    instance()->handle->RemoveUnsigned("AxisZColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDatumScreenSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Draw origins, coordinate systems and datum elements at a constant size on\n"
"screen, the way upstream FreeCAD does. When off, an origin is sized to the\n"
"objects of its body or part.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getDatumScreenSize() {
    return instance()->DatumScreenSize;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultDatumScreenSize() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDatumScreenSize(const bool &v) {
    instance()->handle->SetBool("DatumScreenSize",v);
    instance()->DatumScreenSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDatumScreenSize() {
    instance()->handle->RemoveBool("DatumScreenSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDatumScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Size in percent of origins, coordinate systems and datum elements drawn\n"
"at a constant size on screen.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getDatumScale() {
    return instance()->DatumScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultDatumScale() {
    const static double def = 100.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDatumScale(const double &v) {
    instance()->handle->SetFloat("DatumScale",v);
    instance()->DatumScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDatumScale() {
    instance()->handle->RemoveFloat("DatumScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDatumPlaneSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"On-screen size of a datum plane, before the datum scale.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getDatumPlaneSize() {
    return instance()->DatumPlaneSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultDatumPlaneSize() {
    const static double def = 62.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDatumPlaneSize(const double &v) {
    instance()->handle->SetFloat("DatumPlaneSize",v);
    instance()->DatumPlaneSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDatumPlaneSize() {
    instance()->handle->RemoveFloat("DatumPlaneSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDatumLineSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"On-screen length of a datum axis, before the datum scale.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getDatumLineSize() {
    return instance()->DatumLineSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultDatumLineSize() {
    const static double def = 70.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDatumLineSize(const double &v) {
    instance()->handle->SetFloat("DatumLineSize",v);
    instance()->DatumLineSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDatumLineSize() {
    instance()->handle->RemoveFloat("DatumLineSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDatumTemporaryScaleFactor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"How much datum planes grow while a reference is picked from them.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getDatumTemporaryScaleFactor() {
    return instance()->DatumTemporaryScaleFactor;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultDatumTemporaryScaleFactor() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDatumTemporaryScaleFactor(const double &v) {
    instance()->handle->SetFloat("DatumTemporaryScaleFactor",v);
    instance()->DatumTemporaryScaleFactor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDatumTemporaryScaleFactor() {
    instance()->handle->RemoveFloat("DatumTemporaryScaleFactor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEyeDistance() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Offset between the left and the right eye image of a stereo 3D\n"
"view. 0.1 to 1000. Applies at once to all open 3D views.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getEyeDistance() {
    return instance()->EyeDistance;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultEyeDistance() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEyeDistance(const double &v) {
    instance()->handle->SetFloat("EyeDistance",v);
    instance()->EyeDistance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEyeDistance() {
    instance()->handle->RemoveFloat("EyeDistance");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docCornerCoordSystem() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show the small coordinate system in the corner of every 3D view.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getCornerCoordSystem() {
    return instance()->CornerCoordSystem;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultCornerCoordSystem() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setCornerCoordSystem(const bool &v) {
    instance()->handle->SetBool("CornerCoordSystem",v);
    instance()->CornerCoordSystem = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeCornerCoordSystem() {
    instance()->handle->RemoveBool("CornerCoordSystem");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docCornerCoordSystemSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Size of the coordinate system in the corner of the 3D views, 2 to\n"
"100. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getCornerCoordSystemSize() {
    return instance()->CornerCoordSystemSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultCornerCoordSystemSize() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setCornerCoordSystemSize(const long &v) {
    instance()->handle->SetInt("CornerCoordSystemSize",v);
    instance()->CornerCoordSystemSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeCornerCoordSystemSize() {
    instance()->handle->RemoveInt("CornerCoordSystemSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowAxisCross() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show the axis cross at the origin of the 3D views. Applies at once\n"
"to the open views and to new ones.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowAxisCross() {
    return instance()->ShowAxisCross;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowAxisCross() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowAxisCross(const bool &v) {
    instance()->handle->SetBool("ShowAxisCross",v);
    instance()->ShowAxisCross = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowAxisCross() {
    instance()->handle->RemoveBool("ShowAxisCross");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowFPS() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show a frames per second counter in the 3D views. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowFPS() {
    return instance()->ShowFPS;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowFPS() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowFPS(const bool &v) {
    instance()->handle->SetBool("ShowFPS",v);
    instance()->ShowFPS = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowFPS() {
    instance()->handle->RemoveBool("ShowFPS");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseVBO() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Let Coin draw with vertex buffer objects. Applies at once to the\n"
"open 3D views except split views; the driver override that goes\n"
"with it is set at startup only.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseVBO() {
    return instance()->UseVBO;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseVBO() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseVBO(const bool &v) {
    instance()->handle->SetBool("UseVBO",v);
    instance()->UseVBO = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseVBO() {
    instance()->handle->RemoveBool("UseVBO");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docOrthographic() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Use an orthographic camera in the 3D views; the opposite of\n"
"Perspective.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getOrthographic() {
    return instance()->Orthographic;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultOrthographic() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setOrthographic(const bool &v) {
    instance()->handle->SetBool("Orthographic",v);
    instance()->Orthographic = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeOrthographic() {
    instance()->handle->RemoveBool("Orthographic");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docPerspective() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Use a perspective camera in the 3D views. Read when a view is\n"
"created; with ApplyCameraTypeToAll on, a change switches the open\n"
"views too.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getPerspective() {
    return instance()->Perspective;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultPerspective() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setPerspective(const bool &v) {
    instance()->handle->SetBool("Perspective",v);
    instance()->Perspective = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removePerspective() {
    instance()->handle->RemoveBool("Perspective");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docApplyCameraTypeToAll() {
    return QT_TRANSLATE_NOOP("ViewParams",
"When the camera type setting changes, switch every open 3D view\n"
"to it as well.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getApplyCameraTypeToAll() {
    return instance()->ApplyCameraTypeToAll;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultApplyCameraTypeToAll() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setApplyCameraTypeToAll(const bool &v) {
    instance()->handle->SetBool("ApplyCameraTypeToAll",v);
    instance()->ApplyCameraTypeToAll = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeApplyCameraTypeToAll() {
    instance()->handle->RemoveBool("ApplyCameraTypeToAll");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAntiAliasing() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Anti-aliasing of the 3D views: 0 none, 1 line smoothing, 2 MSAA\n"
"2x, 3 MSAA 4x, 4 MSAA 8x. Without multisampling the rim of a\n"
"face that no edge runs along is a staircase. A change is taken\n"
"by the open views at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getAntiAliasing() {
    return instance()->AntiAliasing;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultAntiAliasing() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAntiAliasing(const long &v) {
    instance()->handle->SetInt("AntiAliasing",v);
    instance()->AntiAliasing = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAntiAliasing() {
    instance()->handle->RemoveInt("AntiAliasing");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docTransparentObjectRenderType() {
    return QT_TRANSLATE_NOOP("ViewParams",
"How Coin draws transparent objects: 0 in one pass, 1 with the back\n"
"faces of non-solid objects in a pass of their own. Applies at once\n"
"to the open 3D views except split views.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getTransparentObjectRenderType() {
    return instance()->TransparentObjectRenderType;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultTransparentObjectRenderType() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setTransparentObjectRenderType(const long &v) {
    instance()->handle->SetInt("TransparentObjectRenderType",v);
    instance()->TransparentObjectRenderType = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeTransparentObjectRenderType() {
    instance()->handle->RemoveInt("TransparentObjectRenderType");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docInternalTextureFormat() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pixel format of the offscreen buffer a 3D view is drawn into:\n"
"Default, GL_RGB, GL_RGBA, GL_RGB8, GL_RGBA8, GL_RGB10,\n"
"GL_RGB10_A2, GL_RGB16, GL_RGBA16, GL_RGB32F or GL_RGBA32F. Read\n"
"each time a buffer is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getInternalTextureFormat() {
    return instance()->InternalTextureFormat;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultInternalTextureFormat() {
    const static std::string def = "Default";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setInternalTextureFormat(const std::string &v) {
    instance()->handle->SetASCII("InternalTextureFormat",v);
    instance()->InternalTextureFormat = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeInternalTextureFormat() {
    instance()->handle->RemoveASCII("InternalTextureFormat");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docGradient() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Fill the background of the 3D views with a linear gradient from\n"
"BackgroundColor2 (top) to BackgroundColor3 (bottom). Wins over\n"
"RadialGradient. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getGradient() {
    return instance()->Gradient;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultGradient() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setGradient(const bool &v) {
    instance()->handle->SetBool("Gradient",v);
    instance()->Gradient = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeGradient() {
    instance()->handle->RemoveBool("Gradient");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRadialGradient() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Fill the background of the 3D views with a radial gradient; used\n"
"when Gradient is off. With both off the plain BackgroundColor is\n"
"used. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getRadialGradient() {
    return instance()->RadialGradient;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultRadialGradient() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRadialGradient(const bool &v) {
    instance()->handle->SetBool("RadialGradient",v);
    instance()->RadialGradient = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRadialGradient() {
    instance()->handle->RemoveBool("RadialGradient");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSimple() {
    return QT_TRANSLATE_NOOP("ViewParams",
"The 'Simple color' choice of the Colors page. The views use a\n"
"plain background whenever Gradient and RadialGradient are both\n"
"off, whatever this says.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSimple() {
    return instance()->Simple;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSimple() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSimple(const bool &v) {
    instance()->handle->SetBool("Simple",v);
    instance()->Simple = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSimple() {
    instance()->handle->RemoveBool("Simple");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docBackgroundColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the 3D view background when no gradient is used.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getBackgroundColor() {
    return instance()->BackgroundColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultBackgroundColor() {
    const static unsigned long def = 0xEAE5DCFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setBackgroundColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("BackgroundColor",v);
    instance()->BackgroundColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeBackgroundColor() {
    instance()->handle->RemoveUnsigned("BackgroundColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docBackgroundColor2() {
    return QT_TRANSLATE_NOOP("ViewParams",
"First colour of the background gradient of the 3D views: the top\n"
"of a linear one, the centre of a radial one. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getBackgroundColor2() {
    return instance()->BackgroundColor2;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultBackgroundColor2() {
    const static unsigned long def = 0x333365FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setBackgroundColor2(const unsigned long &v) {
    instance()->handle->SetUnsigned("BackgroundColor2",v);
    instance()->BackgroundColor2 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeBackgroundColor2() {
    instance()->handle->RemoveUnsigned("BackgroundColor2");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docBackgroundColor3() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Last colour of the background gradient of the 3D views: the\n"
"bottom of a linear one, the rim of a radial one. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getBackgroundColor3() {
    return instance()->BackgroundColor3;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultBackgroundColor3() {
    const static unsigned long def = 0xABABC1FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setBackgroundColor3(const unsigned long &v) {
    instance()->handle->SetUnsigned("BackgroundColor3",v);
    instance()->BackgroundColor3 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeBackgroundColor3() {
    instance()->handle->RemoveUnsigned("BackgroundColor3");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docBackgroundColor4() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Middle colour of the background gradient of the 3D views; used\n"
"only with UseBackgroundColorMid on. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getBackgroundColor4() {
    return instance()->BackgroundColor4;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultBackgroundColor4() {
    const static unsigned long def = 0x6F6F93FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setBackgroundColor4(const unsigned long &v) {
    instance()->handle->SetUnsigned("BackgroundColor4",v);
    instance()->BackgroundColor4 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeBackgroundColor4() {
    instance()->handle->RemoveUnsigned("BackgroundColor4");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseBackgroundColorMid() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Give the background gradient of the 3D views a third, middle\n"
"colour (BackgroundColor4). Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseBackgroundColorMid() {
    return instance()->UseBackgroundColorMid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseBackgroundColorMid() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseBackgroundColorMid(const bool &v) {
    instance()->handle->SetBool("UseBackgroundColorMid",v);
    instance()->UseBackgroundColorMid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseBackgroundColorMid() {
    instance()->handle->RemoveBool("UseBackgroundColorMid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEnableHeadlight() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Light the 3D views with the headlight, which follows the camera.\n"
"Applies at once to every view with no light setting of its own.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getEnableHeadlight() {
    return instance()->EnableHeadlight;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultEnableHeadlight() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEnableHeadlight(const bool &v) {
    instance()->handle->SetBool("EnableHeadlight",v);
    instance()->EnableHeadlight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEnableHeadlight() {
    instance()->handle->RemoveBool("EnableHeadlight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHeadlightColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the headlight of the 3D views. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getHeadlightColor() {
    return instance()->HeadlightColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultHeadlightColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHeadlightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("HeadlightColor",v);
    instance()->HeadlightColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHeadlightColor() {
    instance()->handle->RemoveUnsigned("HeadlightColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHeadlightIntensity() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Intensity of the headlight of the 3D views in percent, 0 to 100.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getHeadlightIntensity() {
    return instance()->HeadlightIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultHeadlightIntensity() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHeadlightIntensity(const long &v) {
    instance()->handle->SetInt("HeadlightIntensity",v);
    instance()->HeadlightIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHeadlightIntensity() {
    instance()->handle->RemoveInt("HeadlightIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHeadlightDirection() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Direction of the headlight relative to the camera, as (x,y,z).\n"
"Empty keeps the built-in direction. Turned in a 3D view from the\n"
"Display style menu (Direction), and stored here by its Save as\n"
"default. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getHeadlightDirection() {
    return instance()->HeadlightDirection;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultHeadlightDirection() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHeadlightDirection(const std::string &v) {
    instance()->handle->SetASCII("HeadlightDirection",v);
    instance()->HeadlightDirection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHeadlightDirection() {
    instance()->handle->RemoveASCII("HeadlightDirection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docBacklightDirection() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Direction of the backlight relative to the camera, as (x,y,z).\n"
"Empty keeps the built-in direction. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getBacklightDirection() {
    return instance()->BacklightDirection;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultBacklightDirection() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setBacklightDirection(const std::string &v) {
    instance()->handle->SetASCII("BacklightDirection",v);
    instance()->BacklightDirection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeBacklightDirection() {
    instance()->handle->RemoveASCII("BacklightDirection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docEnableFillLight() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Light the 3D views with an extra fill light from the side.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getEnableFillLight() {
    return instance()->EnableFillLight;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultEnableFillLight() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setEnableFillLight(const bool &v) {
    instance()->handle->SetBool("EnableFillLight",v);
    instance()->EnableFillLight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeEnableFillLight() {
    instance()->handle->RemoveBool("EnableFillLight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docFillLightColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the fill light of the 3D views. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getFillLightColor() {
    return instance()->FillLightColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultFillLightColor() {
    const static unsigned long def = 0xE6FAFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setFillLightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("FillLightColor",v);
    instance()->FillLightColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeFillLightColor() {
    instance()->handle->RemoveUnsigned("FillLightColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docFillLightIntensity() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Intensity of the fill light of the 3D views in percent, 0 to 100.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getFillLightIntensity() {
    return instance()->FillLightIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultFillLightIntensity() {
    const static long def = 60;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setFillLightIntensity(const long &v) {
    instance()->handle->SetInt("FillLightIntensity",v);
    instance()->FillLightIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeFillLightIntensity() {
    instance()->handle->RemoveInt("FillLightIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docFillLightDirection() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Direction of the fill light relative to the camera, as (x,y,z).\n"
"Empty keeps the built-in direction. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getFillLightDirection() {
    return instance()->FillLightDirection;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultFillLightDirection() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setFillLightDirection(const std::string &v) {
    instance()->handle->SetASCII("FillLightDirection",v);
    instance()->FillLightDirection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeFillLightDirection() {
    instance()->handle->RemoveASCII("FillLightDirection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAmbientLightColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the ambient light of the 3D views. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getAmbientLightColor() {
    return instance()->AmbientLightColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultAmbientLightColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAmbientLightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AmbientLightColor",v);
    instance()->AmbientLightColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAmbientLightColor() {
    instance()->handle->RemoveUnsigned("AmbientLightColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAmbientLightIntensity() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Intensity of the ambient light of the 3D views in percent, 0 to\n"
"100. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getAmbientLightIntensity() {
    return instance()->AmbientLightIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultAmbientLightIntensity() {
    const static long def = 20;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAmbientLightIntensity(const long &v) {
    instance()->handle->SetInt("AmbientLightIntensity",v);
    instance()->AmbientLightIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAmbientLightIntensity() {
    instance()->handle->RemoveInt("AmbientLightIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docNavigationStyle() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Mouse navigation style of the 3D views, as a class name such as\n"
"Gui::CADNavigationStyle. Applies at once to all open 3D views;\n"
"TechDraw pages follow it as well.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getNavigationStyle() {
    return instance()->NavigationStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultNavigationStyle() {
    const static std::string def = "Gui::CADNavigationStyle";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setNavigationStyle(const std::string &v) {
    instance()->handle->SetASCII("NavigationStyle",v);
    instance()->NavigationStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeNavigationStyle() {
    instance()->handle->RemoveASCII("NavigationStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSameStyleForAllViews() {
    return QT_TRANSLATE_NOOP("ViewParams",
"A navigation style picked from a 3D view's context menu becomes\n"
"the NavigationStyle setting, so that every view follows. When off\n"
"it changes that view only.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSameStyleForAllViews() {
    return instance()->SameStyleForAllViews;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSameStyleForAllViews() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSameStyleForAllViews(const bool &v) {
    instance()->handle->SetBool("SameStyleForAllViews",v);
    instance()->SameStyleForAllViews = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSameStyleForAllViews() {
    instance()->handle->RemoveBool("SameStyleForAllViews");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docOrbitStyle() {
    return QT_TRANSLATE_NOOP("ViewParams",
"How dragging rotates the 3D view: 0 turntable, 1 trackball, 2 free\n"
"turntable. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getOrbitStyle() {
    return instance()->OrbitStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultOrbitStyle() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setOrbitStyle(const long &v) {
    instance()->handle->SetInt("OrbitStyle",v);
    instance()->OrbitStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeOrbitStyle() {
    instance()->handle->RemoveInt("OrbitStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRotationMode() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Centre of rotation in the 3D views: 0 the window centre, 1 the\n"
"point under the cursor, 2 the centre of the objects. Applies at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getRotationMode() {
    return instance()->RotationMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultRotationMode() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRotationMode(const long &v) {
    instance()->handle->SetInt("RotationMode",v);
    instance()->RotationMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRotationMode() {
    instance()->handle->RemoveInt("RotationMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSensitivity() {
    return QT_TRANSLATE_NOOP("ViewParams",
"A value above 1 multiplies the angle of a mouse rotation of the 3D\n"
"view. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getSensitivity() {
    return instance()->Sensitivity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultSensitivity() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSensitivity(const double &v) {
    instance()->handle->SetFloat("Sensitivity",v);
    instance()->Sensitivity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSensitivity() {
    instance()->handle->RemoveFloat("Sensitivity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docResetCursorPosition() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Move the mouse cursor to the rotation centre when a rotation of\n"
"the 3D view starts. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getResetCursorPosition() {
    return instance()->ResetCursorPosition;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultResetCursorPosition() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setResetCursorPosition(const bool &v) {
    instance()->handle->SetBool("ResetCursorPosition",v);
    instance()->ResetCursorPosition = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeResetCursorPosition() {
    instance()->handle->RemoveBool("ResetCursorPosition");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docInvertZoom() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Invert the direction of zooming with the mouse wheel. The 3D\n"
"views and TechDraw pages follow at once; the dependency graph\n"
"reads it when it is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getInvertZoom() {
    return instance()->InvertZoom;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultInvertZoom() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setInvertZoom(const bool &v) {
    instance()->handle->SetBool("InvertZoom",v);
    instance()->InvertZoom = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeInvertZoom() {
    instance()->handle->RemoveBool("InvertZoom");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docZoomAtCursor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Zoom towards the point under the mouse cursor instead of the\n"
"centre of the view. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getZoomAtCursor() {
    return instance()->ZoomAtCursor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultZoomAtCursor() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setZoomAtCursor(const bool &v) {
    instance()->handle->SetBool("ZoomAtCursor",v);
    instance()->ZoomAtCursor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeZoomAtCursor() {
    instance()->handle->RemoveBool("ZoomAtCursor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docZoomStep() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Zoom factor of one step of the mouse wheel, 0.01 to 1. Applies at\n"
"once to the 3D views and TechDraw pages.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getZoomStep() {
    return instance()->ZoomStep;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultZoomStep() {
    const static double def = 0.2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setZoomStep(const double &v) {
    instance()->handle->SetFloat("ZoomStep",v);
    instance()->ZoomStep = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeZoomStep() {
    instance()->handle->RemoveFloat("ZoomStep");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseNavigationAnimations() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Animate camera moves such as switching to a standard view.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseNavigationAnimations() {
    return instance()->UseNavigationAnimations;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseNavigationAnimations() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseNavigationAnimations(const bool &v) {
    instance()->handle->SetBool("UseNavigationAnimations",v);
    instance()->UseNavigationAnimations = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseNavigationAnimations() {
    instance()->handle->RemoveBool("UseNavigationAnimations");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docUseSpinningAnimations() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Let the model keep spinning when the mouse button is released\n"
"during a rotation. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getUseSpinningAnimations() {
    return instance()->UseSpinningAnimations;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultUseSpinningAnimations() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setUseSpinningAnimations(const bool &v) {
    instance()->handle->SetBool("UseSpinningAnimations",v);
    instance()->UseSpinningAnimations = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeUseSpinningAnimations() {
    instance()->handle->RemoveBool("UseSpinningAnimations");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAnimationDuration() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Duration of an animated camera move in milliseconds, 100 to\n"
"10000. Read each time an animation starts.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getAnimationDuration() {
    return instance()->AnimationDuration;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultAnimationDuration() {
    const static long def = 250;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAnimationDuration(const long &v) {
    instance()->handle->SetInt("AnimationDuration",v);
    instance()->AnimationDuration = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAnimationDuration() {
    instance()->handle->RemoveInt("AnimationDuration");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docstopAnimatingIfDeactivated() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Milliseconds after which a spinning 3D view stops once it is\n"
"hidden or minimized. A negative value never stops it.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getstopAnimatingIfDeactivated() {
    return instance()->stopAnimatingIfDeactivated;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultstopAnimatingIfDeactivated() {
    const static long def = 3000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setstopAnimatingIfDeactivated(const long &v) {
    instance()->handle->SetInt("stopAnimatingIfDeactivated",v);
    instance()->stopAnimatingIfDeactivated = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removestopAnimatingIfDeactivated() {
    instance()->handle->RemoveInt("stopAnimatingIfDeactivated");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowRotationCenter() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show a marker at the centre of rotation while a 3D view is\n"
"rotated. Read at each rotation.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowRotationCenter() {
    return instance()->ShowRotationCenter;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowRotationCenter() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowRotationCenter(const bool &v) {
    instance()->handle->SetBool("ShowRotationCenter",v);
    instance()->ShowRotationCenter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowRotationCenter() {
    instance()->handle->RemoveBool("ShowRotationCenter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRotationCenterSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Size of the rotation centre marker, 1 to 100. Read when the marker\n"
"is next created.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getRotationCenterSize() {
    return instance()->RotationCenterSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultRotationCenterSize() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRotationCenterSize(const double &v) {
    instance()->handle->SetFloat("RotationCenterSize",v);
    instance()->RotationCenterSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRotationCenterSize() {
    instance()->handle->RemoveFloat("RotationCenterSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docRotationCenterColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour and opacity of the rotation centre marker: red and mostly\n"
"see-through unless set. Read when the marker is next created.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getRotationCenterColor() {
    return instance()->RotationCenterColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultRotationCenterColor() {
    const static unsigned long def = 0xFF000033;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setRotationCenterColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("RotationCenterColor",v);
    instance()->RotationCenterColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeRotationCenterColor() {
    instance()->handle->RemoveUnsigned("RotationCenterColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docNewDocumentCameraOrientation() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Camera orientation of a new document: Isometric, Dimetric,\n"
"Trimetric, Top, Front, Left, Right, Rear, Bottom, or Custom. Once\n"
"set, the Home view takes it too; until then Home is Top. Read at\n"
"each use.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getNewDocumentCameraOrientation() {
    return instance()->NewDocumentCameraOrientation;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultNewDocumentCameraOrientation() {
    const static std::string def = "Trimetric";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setNewDocumentCameraOrientation(const std::string &v) {
    instance()->handle->SetASCII("NewDocumentCameraOrientation",v);
    instance()->NewDocumentCameraOrientation = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeNewDocumentCameraOrientation() {
    instance()->handle->RemoveASCII("NewDocumentCameraOrientation");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docAutoFitToView() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Fit the 3D view to the model after a file is opened or imported.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getAutoFitToView() {
    return instance()->AutoFitToView;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultAutoFitToView() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setAutoFitToView(const bool &v) {
    instance()->handle->SetBool("AutoFitToView",v);
    instance()->AutoFitToView = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeAutoFitToView() {
    instance()->handle->RemoveBool("AutoFitToView");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docShowNaviCube() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show the navigation cube in the 3D views. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getShowNaviCube() {
    return instance()->ShowNaviCube;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultShowNaviCube() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setShowNaviCube(const bool &v) {
    instance()->handle->SetBool("ShowNaviCube",v);
    instance()->ShowNaviCube = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeShowNaviCube() {
    instance()->handle->RemoveBool("ShowNaviCube");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDisableTouchTilt() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Gesture navigation: ignore the rotation part of a two-finger\n"
"gesture on a touchscreen. Read at the start of each gesture.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getDisableTouchTilt() {
    return instance()->DisableTouchTilt;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultDisableTouchTilt() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDisableTouchTilt(const bool &v) {
    instance()->handle->SetBool("DisableTouchTilt",v);
    instance()->DisableTouchTilt = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDisableTouchTilt() {
    instance()->handle->RemoveBool("DisableTouchTilt");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docNavigationDebug() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Gesture navigation: write its state changes to the log. Read when\n"
"the Gesture style is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getNavigationDebug() {
    return instance()->NavigationDebug;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultNavigationDebug() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setNavigationDebug(const bool &v) {
    instance()->handle->SetBool("NavigationDebug",v);
    instance()->NavigationDebug = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeNavigationDebug() {
    instance()->handle->RemoveBool("NavigationDebug");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docGestureRollFwdCommand() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Gesture navigation: command run by the forward roll gesture.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getGestureRollFwdCommand() {
    return instance()->GestureRollFwdCommand;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultGestureRollFwdCommand() {
    const static std::string def = "Std_SelForward";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setGestureRollFwdCommand(const std::string &v) {
    instance()->handle->SetASCII("GestureRollFwdCommand",v);
    instance()->GestureRollFwdCommand = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeGestureRollFwdCommand() {
    instance()->handle->RemoveASCII("GestureRollFwdCommand");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docGestureRollBackCommand() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Gesture navigation: command run by the backward roll gesture.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getGestureRollBackCommand() {
    return instance()->GestureRollBackCommand;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultGestureRollBackCommand() {
    const static std::string def = "Std_SelBack";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setGestureRollBackCommand(const std::string &v) {
    instance()->handle->SetASCII("GestureRollBackCommand",v);
    instance()->GestureRollBackCommand = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeGestureRollBackCommand() {
    instance()->handle->RemoveASCII("GestureRollBackCommand");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSaveWBbyTab() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Remember the active workbench separately for each view tab and\n"
"switch back to it when the tab is activated.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getSaveWBbyTab() {
    return instance()->SaveWBbyTab;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultSaveWBbyTab() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSaveWBbyTab(const bool &v) {
    instance()->handle->SetBool("SaveWBbyTab",v);
    instance()->SaveWBbyTab = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSaveWBbyTab() {
    instance()->handle->RemoveBool("SaveWBbyTab");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docCbLabelColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Colour of the value labels of a colour bar in the 3D view. Read\n"
"when the labels are next rebuilt.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & ViewParams::getCbLabelColor() {
    return instance()->CbLabelColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & ViewParams::defaultCbLabelColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setCbLabelColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("CbLabelColor",v);
    instance()->CbLabelColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeCbLabelColor() {
    instance()->handle->RemoveUnsigned("CbLabelColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docCbLabelTextSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Text size of the value labels of a colour bar in the 3D view, 4 to\n"
"36. Read when the labels are next rebuilt.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getCbLabelTextSize() {
    return instance()->CbLabelTextSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultCbLabelTextSize() {
    const static long def = 13;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setCbLabelTextSize(const long &v) {
    instance()->handle->SetInt("CbLabelTextSize",v);
    instance()->CbLabelTextSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeCbLabelTextSize() {
    instance()->handle->RemoveInt("CbLabelTextSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docBoundingBoxFontSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Font size of the dimension labels on an object's bounding box, 2\n"
"to 64. Read when a bounding box is first shown for an object.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getBoundingBoxFontSize() {
    return instance()->BoundingBoxFontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultBoundingBoxFontSize() {
    const static double def = 10.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setBoundingBoxFontSize(const double &v) {
    instance()->handle->SetFloat("BoundingBoxFontSize",v);
    instance()->BoundingBoxFontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeBoundingBoxFontSize() {
    instance()->handle->RemoveFloat("BoundingBoxFontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDatumPointSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Radius of the sphere drawn for a datum point.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getDatumPointSize() {
    return instance()->DatumPointSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultDatumPointSize() {
    const static double def = 2.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDatumPointSize(const double &v) {
    instance()->handle->SetFloat("DatumPointSize",v);
    instance()->DatumPointSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDatumPointSize() {
    instance()->handle->RemoveFloat("DatumPointSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docLocalCoordinateSystemSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Scale factor of datum objects -- origin axes, planes, points --\n"
"when they are drawn at a fixed size on screen.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getLocalCoordinateSystemSize() {
    return instance()->LocalCoordinateSystemSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultLocalCoordinateSystemSize() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setLocalCoordinateSystemSize(const double &v) {
    instance()->handle->SetFloat("LocalCoordinateSystemSize",v);
    instance()->LocalCoordinateSystemSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeLocalCoordinateSystemSize() {
    instance()->handle->RemoveFloat("LocalCoordinateSystemSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDefaultShapeShininess() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Shininess of the appearance given to new objects, in percent.\n"
"Read each time a default appearance is made.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & ViewParams::getDefaultShapeShininess() {
    return instance()->DefaultShapeShininess;
}

// Auto generated code (Tools/params_utils.py:413)
const long & ViewParams::defaultDefaultShapeShininess() {
    const static long def = 37;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDefaultShapeShininess(const long &v) {
    instance()->handle->SetInt("DefaultShapeShininess",v);
    instance()->DefaultShapeShininess = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDefaultShapeShininess() {
    instance()->handle->RemoveInt("DefaultShapeShininess");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDimensionsVisible() {
    return QT_TRANSLATE_NOOP("ViewParams",
"The measurements made with the measure tools are shown in the 3D\n"
"views. Stored by the command that toggles them; the views follow\n"
"at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getDimensionsVisible() {
    return instance()->DimensionsVisible;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultDimensionsVisible() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDimensionsVisible(const bool &v) {
    instance()->handle->SetBool("DimensionsVisible",v);
    instance()->DimensionsVisible = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDimensionsVisible() {
    instance()->handle->RemoveBool("DimensionsVisible");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDimensions3dVisible() {
    return QT_TRANSLATE_NOOP("ViewParams",
"The direct, 3D, lines of the measurements are shown. Stored by\n"
"the command that toggles them; the views follow at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getDimensions3dVisible() {
    return instance()->Dimensions3dVisible;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultDimensions3dVisible() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDimensions3dVisible(const bool &v) {
    instance()->handle->SetBool("Dimensions3dVisible",v);
    instance()->Dimensions3dVisible = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDimensions3dVisible() {
    instance()->handle->RemoveBool("Dimensions3dVisible");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docDimensionsDeltaVisible() {
    return QT_TRANSLATE_NOOP("ViewParams",
"The lines of the measurements along the three axes are shown.\n"
"Stored by the command that toggles them; the views follow at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & ViewParams::getDimensionsDeltaVisible() {
    return instance()->DimensionsDeltaVisible;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & ViewParams::defaultDimensionsDeltaVisible() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setDimensionsDeltaVisible(const bool &v) {
    instance()->handle->SetBool("DimensionsDeltaVisible",v);
    instance()->DimensionsDeltaVisible = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeDimensionsDeltaVisible() {
    instance()->handle->RemoveBool("DimensionsDeltaVisible");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docSavePicture() {
    return QT_TRANSLATE_NOOP("ViewParams",
"How the Save picture dialog last made its picture:\n"
"FramebufferObject, CoinOffscreenRenderer, GrabFramebuffer, or\n"
"empty for the dialog's own way. Stored when a method is chosen in\n"
"the dialog's options, and used for every picture saved.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & ViewParams::getSavePicture() {
    return instance()->SavePicture;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & ViewParams::defaultSavePicture() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setSavePicture(const std::string &v) {
    instance()->handle->SetASCII("SavePicture",v);
    instance()->SavePicture = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeSavePicture() {
    instance()->handle->RemoveASCII("SavePicture");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHeadlightRotationX() {
    return QT_TRANSLATE_NOOP("ViewParams",
"First component of the rotation the dragger of the Light\n"
"sources page was left with. The view itself uses the headlight\n"
"direction, which the page stores with it.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getHeadlightRotationX() {
    return instance()->HeadlightRotationX;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultHeadlightRotationX() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHeadlightRotationX(const double &v) {
    instance()->handle->SetFloat("HeadlightRotationX",v);
    instance()->HeadlightRotationX = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHeadlightRotationX() {
    instance()->handle->RemoveFloat("HeadlightRotationX");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHeadlightRotationY() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Second component of the rotation of the dragger of the Light\n"
"sources page.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getHeadlightRotationY() {
    return instance()->HeadlightRotationY;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultHeadlightRotationY() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHeadlightRotationY(const double &v) {
    instance()->handle->SetFloat("HeadlightRotationY",v);
    instance()->HeadlightRotationY = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHeadlightRotationY() {
    instance()->handle->RemoveFloat("HeadlightRotationY");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHeadlightRotationZ() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Third component of the rotation of the dragger of the Light\n"
"sources page.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getHeadlightRotationZ() {
    return instance()->HeadlightRotationZ;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultHeadlightRotationZ() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHeadlightRotationZ(const double &v) {
    instance()->handle->SetFloat("HeadlightRotationZ",v);
    instance()->HeadlightRotationZ = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHeadlightRotationZ() {
    instance()->handle->RemoveFloat("HeadlightRotationZ");
}

// Auto generated code (Tools/params_utils.py:397)
const char *ViewParams::docHeadlightRotationW() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Fourth component of the rotation of the dragger of the Light\n"
"sources page.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & ViewParams::getHeadlightRotationW() {
    return instance()->HeadlightRotationW;
}

// Auto generated code (Tools/params_utils.py:413)
const double & ViewParams::defaultHeadlightRotationW() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void ViewParams::setHeadlightRotationW(const double &v) {
    instance()->handle->SetFloat("HeadlightRotationW",v);
    instance()->HeadlightRotationW = v;
}

// Auto generated code (Tools/params_utils.py:431)
void ViewParams::removeHeadlightRotationW() {
    instance()->handle->RemoveFloat("HeadlightRotationW");
}

// Auto generated code (Gui/ViewParams.py:1102)
const std::vector<QString> ViewParams::AnimationCurveTypes = {
    QStringLiteral("Linear"),
    QStringLiteral("InQuad"),
    QStringLiteral("OutQuad"),
    QStringLiteral("InOutQuad"),
    QStringLiteral("OutInQuad"),
    QStringLiteral("InCubic"),
    QStringLiteral("OutCubic"),
    QStringLiteral("InOutCubic"),
    QStringLiteral("OutInCubic"),
    QStringLiteral("InQuart"),
    QStringLiteral("OutQuart"),
    QStringLiteral("InOutQuart"),
    QStringLiteral("OutInQuart"),
    QStringLiteral("InQuint"),
    QStringLiteral("OutQuint"),
    QStringLiteral("InOutQuint"),
    QStringLiteral("OutInQuint"),
    QStringLiteral("InSine"),
    QStringLiteral("OutSine"),
    QStringLiteral("InOutSine"),
    QStringLiteral("OutInSine"),
    QStringLiteral("InExpo"),
    QStringLiteral("OutExpo"),
    QStringLiteral("InOutExpo"),
    QStringLiteral("OutInExpo"),
    QStringLiteral("InCirc"),
    QStringLiteral("OutCirc"),
    QStringLiteral("InOutCirc"),
    QStringLiteral("OutInCirc"),
    QStringLiteral("InElastic"),
    QStringLiteral("OutElastic"),
    QStringLiteral("InOutElastic"),
    QStringLiteral("OutInElastic"),
    QStringLiteral("InBack"),
    QStringLiteral("OutBack"),
    QStringLiteral("InOutBack"),
    QStringLiteral("OutInBack"),
    QStringLiteral("InBounce"),
    QStringLiteral("OutBounce"),
    QStringLiteral("InOutBounce"),
    QStringLiteral("OutInBounce"),
};

// Auto generated code (Gui/ViewParams.py:1110)
static const char *DrawStyleNames[] = {
    QT_TRANSLATE_NOOP("DrawStyle", "As Is"),
    QT_TRANSLATE_NOOP("DrawStyle", "Points"),
    QT_TRANSLATE_NOOP("DrawStyle", "Wireframe"),
    QT_TRANSLATE_NOOP("DrawStyle", "Hidden Line"),
    QT_TRANSLATE_NOOP("DrawStyle", "No Shading"),
    QT_TRANSLATE_NOOP("DrawStyle", "Shaded"),
    QT_TRANSLATE_NOOP("DrawStyle", "Flat Lines"),
    QT_TRANSLATE_NOOP("DrawStyle", "Tessellation"),
    nullptr,
};

// Auto generated code (Gui/ViewParams.py:1120)
static const char *DrawStyleDocs[] = {
    QT_TRANSLATE_NOOP("DrawStyle", "Display style, normal display mode"),
    QT_TRANSLATE_NOOP("DrawStyle", "Display style, show points only"),
    QT_TRANSLATE_NOOP("DrawStyle", "Display style, show wire frame only"),
    QT_TRANSLATE_NOOP("DrawStyle", "Display style, show hidden line by display object as transparent"),
    QT_TRANSLATE_NOOP("DrawStyle", "Display style, shading forced off"),
    QT_TRANSLATE_NOOP("DrawStyle", "Display style, shading force on"),
    QT_TRANSLATE_NOOP("DrawStyle", "Display style, show both wire frame and face with shading"),
    QT_TRANSLATE_NOOP("DrawStyle", "Display style, show tessellation wire frame"),
};

namespace Gui {
// Auto generated code (Gui/ViewParams.py:1130)
const char **drawStyleNames()
{
    return DrawStyleNames;
}

// Auto generated code (Gui/ViewParams.py:1137)
const char *drawStyleNameFromIndex(int i)
{
    if (i < 0 || i>= 8)
        return nullptr;
    return DrawStyleNames[i];
}

// Auto generated code (Gui/ViewParams.py:1146)
int drawStyleIndexFromName(const char *name)
{
    if (!name)
        return -1;
    for (int i=0; i< 8; ++i) {
        if (strcmp(name, DrawStyleNames[i]) == 0)
            return i;
    }
    return -1;
}

// Auto generated code (Gui/ViewParams.py:1159)
const char *drawStyleDocumentation(int i)
{
    if (i < 0 || i>= 8)
        return "";
    return DrawStyleDocs[i];
}

} // namespace Gui
//[[[end]]]

void ViewParams::onShowSelectionOnTopChanged() {
    Selection().clearCompleteSelection();
    if(getMapChildrenPlacement())
        setMapChildrenPlacement(false);
}

void ViewParams::onMapChildrenPlacementChanged() {
    ViewProvider::clearBoundingBoxCache();
    if(!getShowSelectionOnTop())
        setShowSelectionOnTop(true);
}

void ViewParams::onTextCursorWidthChanged() {
    LineEditStyle::setupChildren(getMainWindow());
}

void ViewParams::onSectionHatchTextureChanged() {
    for (auto doc : App::GetApplication().getDocuments()) {
        Gui::Document* gdoc = Gui::Application::Instance->getDocument(doc);
        if (gdoc) {
            for (auto v : gdoc->getMDIViewsOfType(View3DInventor::getClassTypeId())) {
                auto view = static_cast<View3DInventor*>(v)->getViewer();
                view->updateHatchTexture();
            }
        }
    }
}

void ViewParams::onRenderCacheChanged() {
    onSectionHatchTextureChanged();
}

void ViewParams::onUnifiedCanvasChanged() {
    // Applied live, both ways: a container either builds its canvas and
    // takes its 3D cells over, or hands every cell back its own widget
    // composition (docs/SplitViews.md sec 13).
    if (auto mw = getMainWindow()) {
        for (auto area : mw->findChildren<ViewArea*>())
            area->syncCanvas();
    }
}

bool ViewParams::isUsingRenderer()
{
    // the mode the program draws by: 3 with the render engine whatever
    // the setting holds, the setting under the render type "Legacy"
    return RenderParams::renderCache() == 3;
}

void ViewParams::useRenderer(bool enable)
{
    // Only under "Legacy" is there anything to switch: with the render
    // engine the render cache is in use, and stays so.
    if (isUsingRenderer()) {
        if (!enable && !RenderParams::usesEngine())
            setRenderCache(0);
    } else if (enable)
        setRenderCache(3);
}

int ViewParams::appDefaultFontSize() {
    static int defaultSize;
    if (!defaultSize) {
        QFont font;
        defaultSize = font.pointSize();
    }
    return defaultSize;
}

void ViewParams::onDefaultFontSizeChanged() {
    int defaultSize = appDefaultFontSize();
    int fontSize = getDefaultFontSize();
    if (fontSize <= 0)
        fontSize = defaultSize;
    else if (fontSize < 8)
        fontSize = 8;
    QFont font = QApplication::font();
    if (font.pointSize() != fontSize) {
        font.setPointSize(fontSize);
        QApplication::setFont(font);
        QEvent e(QEvent::ApplicationFontChange);
        for (auto w : QApplication::allWidgets())
            QApplication::sendEvent(w, &e);
    }

    if (TreeParams::getFontSize() <= 0)
        TreeParams::onFontSizeChanged();
}

void ViewParams::onEnableTaskPanelKeyTranslateChanged() {
    QSint::TaskHeader::enableKeyTranslate(getEnableTaskPanelKeyTranslate());
}

void ViewParams::init() {
    onDefaultFontSizeChanged();
}

bool ViewParams::highlightPick()
{
    return Selection().needPickedList() || getAutoTransparentPick();
}

bool ViewParams::hiddenLineSelectionOnTop()
{
    return getHiddenLineSelectionOnTop() || highlightPick();
}

void ViewParams::refreshRenderCache() {
    for (auto doc : App::GetApplication().getDocuments()) {
        if (auto gdoc = Gui::Application::Instance->getDocument(doc)) {
            gdoc->foreachView<View3DInventor>([](View3DInventor *view){
                view->getViewer()->refreshRenderCache();
            });
        }
    }
}

void ViewParams::onForceSolidSingleSideLightingChanged()
{
    refreshRenderCache();
}

void ViewParams::onViewParamChanged(const char *sReason)
{
    Dialog::DlgSettingsDrawStyles::onParamChanged(sReason);
}

void ViewParams::migrate()
{
    // BacklightIntensity was read out of two different typed slots of the
    // same group under the same name -- a group is a set of typed maps, and
    // an Int and a Float of one name are two separate values. This class
    // took the Float as a fraction; the Clipping dialog's slider, the 3D
    // view preference page and the viewer all took the Int as a per cent.
    // Only the Int ever lit anything, which is why this class's accessor
    // (and the slider default it seeds) returned a value nothing wrote.
    //
    // The Float is dropped, carried over as a per cent first if a config
    // has one and no Int -- scripting is the only thing that could have
    // written it. Removing it is what keeps the migration from repeating.
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View");
    for (const auto &v : hGrp->GetFloatMap("BacklightIntensity")) {
        if (v.first != "BacklightIntensity")
            continue;
        bool hasInt = false;
        for (const auto &i : hGrp->GetIntMap("BacklightIntensity"))
            hasInt = hasInt || i.first == "BacklightIntensity";
        if (!hasInt)
            hGrp->SetInt("BacklightIntensity", long(v.second * 100 + 0.5));
        hGrp->RemoveFloat("BacklightIntensity");
        break;
    }
}
