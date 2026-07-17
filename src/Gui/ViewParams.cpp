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
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "QSint/actionpanel/taskheader_p.h"

/*[[[cog
import ViewParams
ViewParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include "ViewParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:209)
namespace {
class ViewParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(ViewParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    bool UseNewSelection;
    bool UseSelectionRoot;
    bool EnableSelection;
    bool EnablePreselection;
    long RenderCache;
    bool RandomColor;
    unsigned long BoundingBoxColor;
    unsigned long AnnotationTextColor;
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
    double ShadowGroundTransparency;
    bool ShadowGroundShading;
    bool ShadowExtraRedraw;
    long ShadowSmoothBorder;
    long ShadowSpreadSize;
    long ShadowSpreadSampleSize;
    double ShadowPrecision;
    double ShadowEpsilon;
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
    double RenderHighlightPolygonOffsetFactor;
    double RenderHighlightPolygonOffsetUnits;
    bool ForceSolidSingleSideLighting;
    long DefaultFontSize;
    bool EnableTaskPanelKeyTranslate;
    bool EnableMenuBarCheckBox;
    bool EnableBacklight;
    unsigned long BacklightColor;
    double BacklightIntensity;
    bool OverrideSelectability;
    unsigned long SelectionStackSize;
    long DefaultDrawStyle;
    long ToolTipIconSize;
    bool ToolTipDisable;
    unsigned long AxisXColor;
    unsigned long AxisYColor;
    unsigned long AxisZColor;

    // Auto generated code (Tools/params_utils.py:253)
    ViewParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
        handle->Attach(this);

        UseNewSelection = this->handle->GetBool("UseNewSelection", true);
        funcs["UseNewSelection"] = &ViewParamsP::updateUseNewSelection;
        UseSelectionRoot = this->handle->GetBool("UseSelectionRoot", true);
        funcs["UseSelectionRoot"] = &ViewParamsP::updateUseSelectionRoot;
        EnableSelection = this->handle->GetBool("EnableSelection", true);
        funcs["EnableSelection"] = &ViewParamsP::updateEnableSelection;
        EnablePreselection = this->handle->GetBool("EnablePreselection", true);
        funcs["EnablePreselection"] = &ViewParamsP::updateEnablePreselection;
        RenderCache = this->handle->GetInt("RenderCache", 0);
        funcs["RenderCache"] = &ViewParamsP::updateRenderCache;
        RandomColor = this->handle->GetBool("RandomColor", false);
        funcs["RandomColor"] = &ViewParamsP::updateRandomColor;
        BoundingBoxColor = this->handle->GetUnsigned("BoundingBoxColor", 0xFFFFFFFF);
        funcs["BoundingBoxColor"] = &ViewParamsP::updateBoundingBoxColor;
        AnnotationTextColor = this->handle->GetUnsigned("AnnotationTextColor", 0xFFFFFFFF);
        funcs["AnnotationTextColor"] = &ViewParamsP::updateAnnotationTextColor;
        HighlightColor = this->handle->GetUnsigned("HighlightColor", 0xE1E114FF);
        funcs["HighlightColor"] = &ViewParamsP::updateHighlightColor;
        SelectionColor = this->handle->GetUnsigned("SelectionColor", 0x1CAD1CFF);
        funcs["SelectionColor"] = &ViewParamsP::updateSelectionColor;
        MarkerSize = this->handle->GetInt("MarkerSize", 9);
        funcs["MarkerSize"] = &ViewParamsP::updateMarkerSize;
        DefaultLinkColor = this->handle->GetUnsigned("DefaultLinkColor", 0x66FFFFFF);
        funcs["DefaultLinkColor"] = &ViewParamsP::updateDefaultLinkColor;
        DefaultShapeLineColor = this->handle->GetUnsigned("DefaultShapeLineColor", 0x191919FF);
        funcs["DefaultShapeLineColor"] = &ViewParamsP::updateDefaultShapeLineColor;
        DefaultShapeVertexColor = this->handle->GetUnsigned("DefaultShapeVertexColor", 0x191919FF);
        funcs["DefaultShapeVertexColor"] = &ViewParamsP::updateDefaultShapeVertexColor;
        DefaultShapeColor = this->handle->GetUnsigned("DefaultShapeColor", 0xCCCCCCFF);
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
        ShadowGroundTransparency = this->handle->GetFloat("ShadowGroundTransparency", 0.0);
        funcs["ShadowGroundTransparency"] = &ViewParamsP::updateShadowGroundTransparency;
        ShadowGroundShading = this->handle->GetBool("ShadowGroundShading", true);
        funcs["ShadowGroundShading"] = &ViewParamsP::updateShadowGroundShading;
        ShadowExtraRedraw = this->handle->GetBool("ShadowExtraRedraw", true);
        funcs["ShadowExtraRedraw"] = &ViewParamsP::updateShadowExtraRedraw;
        ShadowSmoothBorder = this->handle->GetInt("ShadowSmoothBorder", 0);
        funcs["ShadowSmoothBorder"] = &ViewParamsP::updateShadowSmoothBorder;
        ShadowSpreadSize = this->handle->GetInt("ShadowSpreadSize", 0);
        funcs["ShadowSpreadSize"] = &ViewParamsP::updateShadowSpreadSize;
        ShadowSpreadSampleSize = this->handle->GetInt("ShadowSpreadSampleSize", 0);
        funcs["ShadowSpreadSampleSize"] = &ViewParamsP::updateShadowSpreadSampleSize;
        ShadowPrecision = this->handle->GetFloat("ShadowPrecision", 1.0);
        funcs["ShadowPrecision"] = &ViewParamsP::updateShadowPrecision;
        ShadowEpsilon = this->handle->GetFloat("ShadowEpsilon", 1e-05);
        funcs["ShadowEpsilon"] = &ViewParamsP::updateShadowEpsilon;
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
        BacklightIntensity = this->handle->GetFloat("BacklightIntensity", 1.0);
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
    }

    // Auto generated code (Tools/params_utils.py:283)
    ~ViewParamsP() {
    }

    // Auto generated code (Tools/params_utils.py:290)
    void OnChange(Base::Subject<const char*> &param, const char* sReason) {
        (void)param;
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        
        ViewParams::onViewParamChanged(sReason);
    }


    // Auto generated code (Tools/params_utils.py:310)
    static void updateUseNewSelection(ViewParamsP *self) {
        self->UseNewSelection = self->handle->GetBool("UseNewSelection", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateUseSelectionRoot(ViewParamsP *self) {
        self->UseSelectionRoot = self->handle->GetBool("UseSelectionRoot", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEnableSelection(ViewParamsP *self) {
        self->EnableSelection = self->handle->GetBool("EnableSelection", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEnablePreselection(ViewParamsP *self) {
        self->EnablePreselection = self->handle->GetBool("EnablePreselection", true);
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateRenderCache(ViewParamsP *self) {
        auto v = self->handle->GetInt("RenderCache", 0);
        if (self->RenderCache != v) {
            self->RenderCache = v;
            ViewParams::onRenderCacheChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRandomColor(ViewParamsP *self) {
        self->RandomColor = self->handle->GetBool("RandomColor", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBoundingBoxColor(ViewParamsP *self) {
        self->BoundingBoxColor = self->handle->GetUnsigned("BoundingBoxColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAnnotationTextColor(ViewParamsP *self) {
        self->AnnotationTextColor = self->handle->GetUnsigned("AnnotationTextColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHighlightColor(ViewParamsP *self) {
        self->HighlightColor = self->handle->GetUnsigned("HighlightColor", 0xE1E114FF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionColor(ViewParamsP *self) {
        self->SelectionColor = self->handle->GetUnsigned("SelectionColor", 0x1CAD1CFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMarkerSize(ViewParamsP *self) {
        self->MarkerSize = self->handle->GetInt("MarkerSize", 9);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDefaultLinkColor(ViewParamsP *self) {
        self->DefaultLinkColor = self->handle->GetUnsigned("DefaultLinkColor", 0x66FFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDefaultShapeLineColor(ViewParamsP *self) {
        self->DefaultShapeLineColor = self->handle->GetUnsigned("DefaultShapeLineColor", 0x191919FF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDefaultShapeVertexColor(ViewParamsP *self) {
        self->DefaultShapeVertexColor = self->handle->GetUnsigned("DefaultShapeVertexColor", 0x191919FF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDefaultShapeColor(ViewParamsP *self) {
        self->DefaultShapeColor = self->handle->GetUnsigned("DefaultShapeColor", 0xCCCCCCFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDefaultShapeTransparency(ViewParamsP *self) {
        self->DefaultShapeTransparency = self->handle->GetInt("DefaultShapeTransparency", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDefaultShapeLineWidth(ViewParamsP *self) {
        self->DefaultShapeLineWidth = self->handle->GetInt("DefaultShapeLineWidth", 2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDefaultShapePointSize(ViewParamsP *self) {
        self->DefaultShapePointSize = self->handle->GetInt("DefaultShapePointSize", 2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCoinCycleCheck(ViewParamsP *self) {
        self->CoinCycleCheck = self->handle->GetBool("CoinCycleCheck", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEnablePropertyViewForInactiveDocument(ViewParamsP *self) {
        self->EnablePropertyViewForInactiveDocument = self->handle->GetBool("EnablePropertyViewForInactiveDocument", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShowSelectionBoundingBox(ViewParamsP *self) {
        self->ShowSelectionBoundingBox = self->handle->GetBool("ShowSelectionBoundingBox", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShowSelectionBoundingBoxThreshold(ViewParamsP *self) {
        self->ShowSelectionBoundingBoxThreshold = self->handle->GetInt("ShowSelectionBoundingBoxThreshold", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateUpdateSelectionVisual(ViewParamsP *self) {
        self->UpdateSelectionVisual = self->handle->GetBool("UpdateSelectionVisual", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLinkChildrenDirect(ViewParamsP *self) {
        self->LinkChildrenDirect = self->handle->GetBool("LinkChildrenDirect", true);
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateShowSelectionOnTop(ViewParamsP *self) {
        auto v = self->handle->GetBool("ShowSelectionOnTop", true);
        if (self->ShowSelectionOnTop != v) {
            self->ShowSelectionOnTop = v;
            ViewParams::onShowSelectionOnTopChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShowPreSelectedFaceOnTop(ViewParamsP *self) {
        self->ShowPreSelectedFaceOnTop = self->handle->GetBool("ShowPreSelectedFaceOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShowPreSelectedFaceOutline(ViewParamsP *self) {
        self->ShowPreSelectedFaceOutline = self->handle->GetBool("ShowPreSelectedFaceOutline", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShowSelectedFaceOutline(ViewParamsP *self) {
        self->ShowSelectedFaceOutline = self->handle->GetBool("ShowSelectedFaceOutline", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOutlineThicken(ViewParamsP *self) {
        self->OutlineThicken = self->handle->GetFloat("OutlineThicken", 4);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateNoSelFaceHighlightWithOutline(ViewParamsP *self) {
        self->NoSelFaceHighlightWithOutline = self->handle->GetBool("NoSelFaceHighlightWithOutline", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateNoPreSelFaceHighlightWithOutline(ViewParamsP *self) {
        self->NoPreSelFaceHighlightWithOutline = self->handle->GetBool("NoPreSelFaceHighlightWithOutline", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAutoTransparentPick(ViewParamsP *self) {
        self->AutoTransparentPick = self->handle->GetBool("AutoTransparentPick", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectElementOnTop(ViewParamsP *self) {
        self->SelectElementOnTop = self->handle->GetBool("SelectElementOnTop", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateTransparencyOnTop(ViewParamsP *self) {
        self->TransparencyOnTop = self->handle->GetFloat("TransparencyOnTop", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineSync(ViewParamsP *self) {
        self->HiddenLineSync = self->handle->GetInt("HiddenLineSync", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineSelectionOnTop(ViewParamsP *self) {
        self->HiddenLineSelectionOnTop = self->handle->GetBool("HiddenLineSelectionOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePartialHighlightOnFullSelect(ViewParamsP *self) {
        self->PartialHighlightOnFullSelect = self->handle->GetBool("PartialHighlightOnFullSelect", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionLineThicken(ViewParamsP *self) {
        self->SelectionLineThicken = self->handle->GetFloat("SelectionLineThicken", 1.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionLineMaxWidth(ViewParamsP *self) {
        self->SelectionLineMaxWidth = self->handle->GetFloat("SelectionLineMaxWidth", 4.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionPointScale(ViewParamsP *self) {
        self->SelectionPointScale = self->handle->GetFloat("SelectionPointScale", 2.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionPointMaxSize(ViewParamsP *self) {
        self->SelectionPointMaxSize = self->handle->GetFloat("SelectionPointMaxSize", 6.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePickRadius(ViewParamsP *self) {
        self->PickRadius = self->handle->GetFloat("PickRadius", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionTransparency(ViewParamsP *self) {
        self->SelectionTransparency = self->handle->GetFloat("SelectionTransparency", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionLinePattern(ViewParamsP *self) {
        self->SelectionLinePattern = self->handle->GetInt("SelectionLinePattern", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionLinePatternScale(ViewParamsP *self) {
        self->SelectionLinePatternScale = self->handle->GetInt("SelectionLinePatternScale", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionHiddenLineWidth(ViewParamsP *self) {
        self->SelectionHiddenLineWidth = self->handle->GetFloat("SelectionHiddenLineWidth", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionBBoxLineWidth(ViewParamsP *self) {
        self->SelectionBBoxLineWidth = self->handle->GetFloat("SelectionBBoxLineWidth", 3.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShowHighlightEdgeOnly(ViewParamsP *self) {
        self->ShowHighlightEdgeOnly = self->handle->GetBool("ShowHighlightEdgeOnly", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreSelectionDelay(ViewParamsP *self) {
        self->PreSelectionDelay = self->handle->GetFloat("PreSelectionDelay", 0.1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePickBackFaceDelay(ViewParamsP *self) {
        self->PickBackFaceDelay = self->handle->GetInt("PickBackFaceDelay", 2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateUseNewRayPick(ViewParamsP *self) {
        self->UseNewRayPick = self->handle->GetBool("UseNewRayPick", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateViewSelectionExtendFactor(ViewParamsP *self) {
        self->ViewSelectionExtendFactor = self->handle->GetFloat("ViewSelectionExtendFactor", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateUseTightBoundingBox(ViewParamsP *self) {
        self->UseTightBoundingBox = self->handle->GetBool("UseTightBoundingBox", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateUseBoundingBoxCache(ViewParamsP *self) {
        self->UseBoundingBoxCache = self->handle->GetBool("UseBoundingBoxCache", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRenderProjectedBBox(ViewParamsP *self) {
        self->RenderProjectedBBox = self->handle->GetBool("RenderProjectedBBox", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionFaceWire(ViewParamsP *self) {
        self->SelectionFaceWire = self->handle->GetBool("SelectionFaceWire", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateNewDocumentCameraScale(ViewParamsP *self) {
        self->NewDocumentCameraScale = self->handle->GetFloat("NewDocumentCameraScale", 100.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMaxOnTopSelections(ViewParamsP *self) {
        self->MaxOnTopSelections = self->handle->GetInt("MaxOnTopSelections", 100);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMaxViewSelections(ViewParamsP *self) {
        self->MaxViewSelections = self->handle->GetInt("MaxViewSelections", 100);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMaxSelectionNotification(ViewParamsP *self) {
        self->MaxSelectionNotification = self->handle->GetInt("MaxSelectionNotification", 100);
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateMapChildrenPlacement(ViewParamsP *self) {
        auto v = self->handle->GetBool("MapChildrenPlacement", false);
        if (self->MapChildrenPlacement != v) {
            self->MapChildrenPlacement = v;
            ViewParams::onMapChildrenPlacementChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEditingTransparency(ViewParamsP *self) {
        self->EditingTransparency = self->handle->GetFloat("EditingTransparency", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineTransparency(ViewParamsP *self) {
        self->HiddenLineTransparency = self->handle->GetFloat("HiddenLineTransparency", 0.4);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineOverrideTransparency(ViewParamsP *self) {
        self->HiddenLineOverrideTransparency = self->handle->GetBool("HiddenLineOverrideTransparency", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineFaceColor(ViewParamsP *self) {
        self->HiddenLineFaceColor = self->handle->GetUnsigned("HiddenLineFaceColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineOverrideFaceColor(ViewParamsP *self) {
        self->HiddenLineOverrideFaceColor = self->handle->GetBool("HiddenLineOverrideFaceColor", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineColor(ViewParamsP *self) {
        self->HiddenLineColor = self->handle->GetUnsigned("HiddenLineColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineOverrideColor(ViewParamsP *self) {
        self->HiddenLineOverrideColor = self->handle->GetBool("HiddenLineOverrideColor", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineBackground(ViewParamsP *self) {
        self->HiddenLineBackground = self->handle->GetUnsigned("HiddenLineBackground", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineOverrideBackground(ViewParamsP *self) {
        self->HiddenLineOverrideBackground = self->handle->GetBool("HiddenLineOverrideBackground", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineShaded(ViewParamsP *self) {
        self->HiddenLineShaded = self->handle->GetBool("HiddenLineShaded", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineShowOutline(ViewParamsP *self) {
        self->HiddenLineShowOutline = self->handle->GetBool("HiddenLineShowOutline", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLinePerFaceOutline(ViewParamsP *self) {
        self->HiddenLinePerFaceOutline = self->handle->GetBool("HiddenLinePerFaceOutline", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineSceneOutline(ViewParamsP *self) {
        self->HiddenLineSceneOutline = self->handle->GetBool("HiddenLineSceneOutline", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineOutlineWidth(ViewParamsP *self) {
        self->HiddenLineOutlineWidth = self->handle->GetFloat("HiddenLineOutlineWidth", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineWidth(ViewParamsP *self) {
        self->HiddenLineWidth = self->handle->GetFloat("HiddenLineWidth", 1.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLinePointSize(ViewParamsP *self) {
        self->HiddenLinePointSize = self->handle->GetFloat("HiddenLinePointSize", 2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineHideSeam(ViewParamsP *self) {
        self->HiddenLineHideSeam = self->handle->GetBool("HiddenLineHideSeam", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineHideVertex(ViewParamsP *self) {
        self->HiddenLineHideVertex = self->handle->GetBool("HiddenLineHideVertex", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHiddenLineHideFace(ViewParamsP *self) {
        self->HiddenLineHideFace = self->handle->GetBool("HiddenLineHideFace", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateStatusMessageTimeout(ViewParamsP *self) {
        self->StatusMessageTimeout = self->handle->GetInt("StatusMessageTimeout", 5000);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowSync(ViewParamsP *self) {
        self->ShadowSync = self->handle->GetInt("ShadowSync", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowFlatLines(ViewParamsP *self) {
        self->ShadowFlatLines = self->handle->GetBool("ShadowFlatLines", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowDisplayMode(ViewParamsP *self) {
        self->ShadowDisplayMode = self->handle->GetInt("ShadowDisplayMode", 2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowSpotLight(ViewParamsP *self) {
        self->ShadowSpotLight = self->handle->GetBool("ShadowSpotLight", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowLightIntensity(ViewParamsP *self) {
        self->ShadowLightIntensity = self->handle->GetFloat("ShadowLightIntensity", 0.8);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowLightDirectionX(ViewParamsP *self) {
        self->ShadowLightDirectionX = self->handle->GetFloat("ShadowLightDirectionX", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowLightDirectionY(ViewParamsP *self) {
        self->ShadowLightDirectionY = self->handle->GetFloat("ShadowLightDirectionY", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowLightDirectionZ(ViewParamsP *self) {
        self->ShadowLightDirectionZ = self->handle->GetFloat("ShadowLightDirectionZ", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowLightColor(ViewParamsP *self) {
        self->ShadowLightColor = self->handle->GetUnsigned("ShadowLightColor", 0xF0FDFFFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowShowGround(ViewParamsP *self) {
        self->ShadowShowGround = self->handle->GetBool("ShadowShowGround", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowGroundBackFaceCull(ViewParamsP *self) {
        self->ShadowGroundBackFaceCull = self->handle->GetBool("ShadowGroundBackFaceCull", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowGroundScale(ViewParamsP *self) {
        self->ShadowGroundScale = self->handle->GetFloat("ShadowGroundScale", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowGroundColor(ViewParamsP *self) {
        self->ShadowGroundColor = self->handle->GetUnsigned("ShadowGroundColor", 0x7D7D7DFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowGroundBumpMap(ViewParamsP *self) {
        self->ShadowGroundBumpMap = self->handle->GetASCII("ShadowGroundBumpMap", "");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowGroundTexture(ViewParamsP *self) {
        self->ShadowGroundTexture = self->handle->GetASCII("ShadowGroundTexture", "");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowGroundTextureSize(ViewParamsP *self) {
        self->ShadowGroundTextureSize = self->handle->GetFloat("ShadowGroundTextureSize", 100.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowGroundTransparency(ViewParamsP *self) {
        self->ShadowGroundTransparency = self->handle->GetFloat("ShadowGroundTransparency", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowGroundShading(ViewParamsP *self) {
        self->ShadowGroundShading = self->handle->GetBool("ShadowGroundShading", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowExtraRedraw(ViewParamsP *self) {
        self->ShadowExtraRedraw = self->handle->GetBool("ShadowExtraRedraw", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowSmoothBorder(ViewParamsP *self) {
        self->ShadowSmoothBorder = self->handle->GetInt("ShadowSmoothBorder", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowSpreadSize(ViewParamsP *self) {
        self->ShadowSpreadSize = self->handle->GetInt("ShadowSpreadSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowSpreadSampleSize(ViewParamsP *self) {
        self->ShadowSpreadSampleSize = self->handle->GetInt("ShadowSpreadSampleSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowPrecision(ViewParamsP *self) {
        self->ShadowPrecision = self->handle->GetFloat("ShadowPrecision", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowEpsilon(ViewParamsP *self) {
        self->ShadowEpsilon = self->handle->GetFloat("ShadowEpsilon", 1e-05);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowThreshold(ViewParamsP *self) {
        self->ShadowThreshold = self->handle->GetFloat("ShadowThreshold", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowBoundBoxScale(ViewParamsP *self) {
        self->ShadowBoundBoxScale = self->handle->GetFloat("ShadowBoundBoxScale", 1.2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowMaxDistance(ViewParamsP *self) {
        self->ShadowMaxDistance = self->handle->GetFloat("ShadowMaxDistance", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowTransparentShadow(ViewParamsP *self) {
        self->ShadowTransparentShadow = self->handle->GetBool("ShadowTransparentShadow", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadowUpdateGround(ViewParamsP *self) {
        self->ShadowUpdateGround = self->handle->GetBool("ShadowUpdateGround", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePropertyViewTimer(ViewParamsP *self) {
        self->PropertyViewTimer = self->handle->GetUnsigned("PropertyViewTimer", 100);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHierarchyAscend(ViewParamsP *self) {
        self->HierarchyAscend = self->handle->GetBool("HierarchyAscend", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCommandHistorySize(ViewParamsP *self) {
        self->CommandHistorySize = self->handle->GetInt("CommandHistorySize", 20);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuIconSize(ViewParamsP *self) {
        self->PieMenuIconSize = self->handle->GetInt("PieMenuIconSize", 24);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuRadius(ViewParamsP *self) {
        self->PieMenuRadius = self->handle->GetInt("PieMenuRadius", 100);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuTriggerRadius(ViewParamsP *self) {
        self->PieMenuTriggerRadius = self->handle->GetInt("PieMenuTriggerRadius", 60);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuFontSize(ViewParamsP *self) {
        self->PieMenuFontSize = self->handle->GetInt("PieMenuFontSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuTriggerDelay(ViewParamsP *self) {
        self->PieMenuTriggerDelay = self->handle->GetInt("PieMenuTriggerDelay", 200);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuTriggerAction(ViewParamsP *self) {
        self->PieMenuTriggerAction = self->handle->GetBool("PieMenuTriggerAction", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuAnimationDuration(ViewParamsP *self) {
        self->PieMenuAnimationDuration = self->handle->GetInt("PieMenuAnimationDuration", 250);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuAnimationCurve(ViewParamsP *self) {
        self->PieMenuAnimationCurve = self->handle->GetInt("PieMenuAnimationCurve", 38);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuCenterRadius(ViewParamsP *self) {
        self->PieMenuCenterRadius = self->handle->GetInt("PieMenuCenterRadius", 10);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePieMenuPopup(ViewParamsP *self) {
        self->PieMenuPopup = self->handle->GetBool("PieMenuPopup", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateStickyTaskControl(ViewParamsP *self) {
        self->StickyTaskControl = self->handle->GetBool("StickyTaskControl", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateColorOnTop(ViewParamsP *self) {
        self->ColorOnTop = self->handle->GetBool("ColorOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAutoSortWBList(ViewParamsP *self) {
        self->AutoSortWBList = self->handle->GetBool("AutoSortWBList", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMaxCameraAnimatePeriod(ViewParamsP *self) {
        self->MaxCameraAnimatePeriod = self->handle->GetInt("MaxCameraAnimatePeriod", 3000);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateTaskNoWheelFocus(ViewParamsP *self) {
        self->TaskNoWheelFocus = self->handle->GetBool("TaskNoWheelFocus", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateGestureLongPressRotationCenter(ViewParamsP *self) {
        self->GestureLongPressRotationCenter = self->handle->GetBool("GestureLongPressRotationCenter", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCheckWidgetPlacementOnRestore(ViewParamsP *self) {
        self->CheckWidgetPlacementOnRestore = self->handle->GetBool("CheckWidgetPlacementOnRestore", true);
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateTextCursorWidth(ViewParamsP *self) {
        auto v = self->handle->GetInt("TextCursorWidth", 1);
        if (self->TextCursorWidth != v) {
            self->TextCursorWidth = v;
            ViewParams::onTextCursorWidthChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreselectionToolTipCorner(ViewParamsP *self) {
        self->PreselectionToolTipCorner = self->handle->GetInt("PreselectionToolTipCorner", 3);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreselectionToolTipOffsetX(ViewParamsP *self) {
        self->PreselectionToolTipOffsetX = self->handle->GetInt("PreselectionToolTipOffsetX", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreselectionToolTipOffsetY(ViewParamsP *self) {
        self->PreselectionToolTipOffsetY = self->handle->GetInt("PreselectionToolTipOffsetY", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreselectionToolTipFontSize(ViewParamsP *self) {
        self->PreselectionToolTipFontSize = self->handle->GetInt("PreselectionToolTipFontSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSectionFill(ViewParamsP *self) {
        self->SectionFill = self->handle->GetBool("SectionFill", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSectionFillInvert(ViewParamsP *self) {
        self->SectionFillInvert = self->handle->GetBool("SectionFillInvert", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSectionConcave(ViewParamsP *self) {
        self->SectionConcave = self->handle->GetBool("SectionConcave", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateNoSectionOnTop(ViewParamsP *self) {
        self->NoSectionOnTop = self->handle->GetBool("NoSectionOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSectionHatchTextureScale(ViewParamsP *self) {
        self->SectionHatchTextureScale = self->handle->GetFloat("SectionHatchTextureScale", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateSectionHatchTexture(ViewParamsP *self) {
        auto v = self->handle->GetASCII("SectionHatchTexture", ":icons/section-hatch.png");
        if (self->SectionHatchTexture != v) {
            self->SectionHatchTexture = v;
            ViewParams::onSectionHatchTextureChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSectionHatchTextureEnable(ViewParamsP *self) {
        self->SectionHatchTextureEnable = self->handle->GetBool("SectionHatchTextureEnable", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSectionFillGroup(ViewParamsP *self) {
        self->SectionFillGroup = self->handle->GetBool("SectionFillGroup", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShowClipPlane(ViewParamsP *self) {
        self->ShowClipPlane = self->handle->GetBool("ShowClipPlane", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateClipPlaneSize(ViewParamsP *self) {
        self->ClipPlaneSize = self->handle->GetFloat("ClipPlaneSize", 40.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateClipPlaneColor(ViewParamsP *self) {
        self->ClipPlaneColor = self->handle->GetASCII("ClipPlaneColor", "cyan");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateClipPlaneLineWidth(ViewParamsP *self) {
        self->ClipPlaneLineWidth = self->handle->GetFloat("ClipPlaneLineWidth", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateTransformOnTop(ViewParamsP *self) {
        self->TransformOnTop = self->handle->GetBool("TransformOnTop", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionColorDifference(ViewParamsP *self) {
        self->SelectionColorDifference = self->handle->GetFloat("SelectionColorDifference", 25.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRenderCacheMergeCount(ViewParamsP *self) {
        self->RenderCacheMergeCount = self->handle->GetInt("RenderCacheMergeCount", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRenderCacheMergeCountMin(ViewParamsP *self) {
        self->RenderCacheMergeCountMin = self->handle->GetInt("RenderCacheMergeCountMin", 10);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRenderCacheMergeCountMax(ViewParamsP *self) {
        self->RenderCacheMergeCountMax = self->handle->GetInt("RenderCacheMergeCountMax", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRenderCacheMergeDepthMax(ViewParamsP *self) {
        self->RenderCacheMergeDepthMax = self->handle->GetInt("RenderCacheMergeDepthMax", -1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRenderCacheMergeDepthMin(ViewParamsP *self) {
        self->RenderCacheMergeDepthMin = self->handle->GetInt("RenderCacheMergeDepthMin", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRenderHighlightPolygonOffsetFactor(ViewParamsP *self) {
        self->RenderHighlightPolygonOffsetFactor = self->handle->GetFloat("RenderHighlightPolygonOffsetFactor", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRenderHighlightPolygonOffsetUnits(ViewParamsP *self) {
        self->RenderHighlightPolygonOffsetUnits = self->handle->GetFloat("RenderHighlightPolygonOffsetUnits", 1);
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateForceSolidSingleSideLighting(ViewParamsP *self) {
        auto v = self->handle->GetBool("ForceSolidSingleSideLighting", true);
        if (self->ForceSolidSingleSideLighting != v) {
            self->ForceSolidSingleSideLighting = v;
            ViewParams::onForceSolidSingleSideLightingChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateDefaultFontSize(ViewParamsP *self) {
        auto v = self->handle->GetInt("DefaultFontSize", 0);
        if (self->DefaultFontSize != v) {
            self->DefaultFontSize = v;
            ViewParams::onDefaultFontSizeChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateEnableTaskPanelKeyTranslate(ViewParamsP *self) {
        auto v = self->handle->GetBool("EnableTaskPanelKeyTranslate", false);
        if (self->EnableTaskPanelKeyTranslate != v) {
            self->EnableTaskPanelKeyTranslate = v;
            ViewParams::onEnableTaskPanelKeyTranslateChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEnableMenuBarCheckBox(ViewParamsP *self) {
        self->EnableMenuBarCheckBox = self->handle->GetBool("EnableMenuBarCheckBox", FC_ENABLE_MENUBAR_CHECKBOX);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEnableBacklight(ViewParamsP *self) {
        self->EnableBacklight = self->handle->GetBool("EnableBacklight", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBacklightColor(ViewParamsP *self) {
        self->BacklightColor = self->handle->GetUnsigned("BacklightColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBacklightIntensity(ViewParamsP *self) {
        self->BacklightIntensity = self->handle->GetFloat("BacklightIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOverrideSelectability(ViewParamsP *self) {
        self->OverrideSelectability = self->handle->GetBool("OverrideSelectability", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionStackSize(ViewParamsP *self) {
        self->SelectionStackSize = self->handle->GetUnsigned("SelectionStackSize", 30);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDefaultDrawStyle(ViewParamsP *self) {
        self->DefaultDrawStyle = self->handle->GetInt("DefaultDrawStyle", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateToolTipIconSize(ViewParamsP *self) {
        self->ToolTipIconSize = self->handle->GetInt("ToolTipIconSize", 64);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateToolTipDisable(ViewParamsP *self) {
        self->ToolTipDisable = self->handle->GetBool("ToolTipDisable", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAxisXColor(ViewParamsP *self) {
        self->AxisXColor = self->handle->GetUnsigned("AxisXColor", 0xCC333300);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAxisYColor(ViewParamsP *self) {
        self->AxisYColor = self->handle->GetUnsigned("AxisYColor", 0x33CC3300);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAxisZColor(ViewParamsP *self) {
        self->AxisZColor = self->handle->GetUnsigned("AxisZColor", 0x3333CC00);
    }
};

// Auto generated code (Tools/params_utils.py:332)
ViewParamsP *instance() {
    static ViewParamsP *inst = new ViewParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:343)
ParameterGrp::handle ViewParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docUseNewSelection() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getUseNewSelection() {
    return instance()->UseNewSelection;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultUseNewSelection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setUseNewSelection(const bool &v) {
    instance()->handle->SetBool("UseNewSelection",v);
    instance()->UseNewSelection = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeUseNewSelection() {
    instance()->handle->RemoveBool("UseNewSelection");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docUseSelectionRoot() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getUseSelectionRoot() {
    return instance()->UseSelectionRoot;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultUseSelectionRoot() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setUseSelectionRoot(const bool &v) {
    instance()->handle->SetBool("UseSelectionRoot",v);
    instance()->UseSelectionRoot = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeUseSelectionRoot() {
    instance()->handle->RemoveBool("UseSelectionRoot");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docEnableSelection() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable selection, highlighted with specified color");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getEnableSelection() {
    return instance()->EnableSelection;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultEnableSelection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setEnableSelection(const bool &v) {
    instance()->handle->SetBool("EnableSelection",v);
    instance()->EnableSelection = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeEnableSelection() {
    instance()->handle->RemoveBool("EnableSelection");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docEnablePreselection() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable preselection, highlighted with specified color");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getEnablePreselection() {
    return instance()->EnablePreselection;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultEnablePreselection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setEnablePreselection(const bool &v) {
    instance()->handle->SetBool("EnablePreselection",v);
    instance()->EnablePreselection = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeEnablePreselection() {
    instance()->handle->RemoveBool("EnablePreselection");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRenderCache() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getRenderCache() {
    return instance()->RenderCache;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultRenderCache() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRenderCache(const long &v) {
    instance()->handle->SetInt("RenderCache",v);
    instance()->RenderCache = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRenderCache() {
    instance()->handle->RemoveInt("RenderCache");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRandomColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getRandomColor() {
    return instance()->RandomColor;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultRandomColor() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRandomColor(const bool &v) {
    instance()->handle->SetBool("RandomColor",v);
    instance()->RandomColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRandomColor() {
    instance()->handle->RemoveBool("RandomColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docBoundingBoxColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getBoundingBoxColor() {
    return instance()->BoundingBoxColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultBoundingBoxColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setBoundingBoxColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("BoundingBoxColor",v);
    instance()->BoundingBoxColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeBoundingBoxColor() {
    instance()->handle->RemoveUnsigned("BoundingBoxColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docAnnotationTextColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getAnnotationTextColor() {
    return instance()->AnnotationTextColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultAnnotationTextColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setAnnotationTextColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AnnotationTextColor",v);
    instance()->AnnotationTextColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeAnnotationTextColor() {
    instance()->handle->RemoveUnsigned("AnnotationTextColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHighlightColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pre-selection highlight color");
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getHighlightColor() {
    return instance()->HighlightColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultHighlightColor() {
    const static unsigned long def = 0xE1E114FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHighlightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("HighlightColor",v);
    instance()->HighlightColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHighlightColor() {
    instance()->handle->RemoveUnsigned("HighlightColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Selection highlight color");
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getSelectionColor() {
    return instance()->SelectionColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultSelectionColor() {
    const static unsigned long def = 0x1CAD1CFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("SelectionColor",v);
    instance()->SelectionColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionColor() {
    instance()->handle->RemoveUnsigned("SelectionColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docMarkerSize() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getMarkerSize() {
    return instance()->MarkerSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultMarkerSize() {
    const static long def = 9;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setMarkerSize(const long &v) {
    instance()->handle->SetInt("MarkerSize",v);
    instance()->MarkerSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeMarkerSize() {
    instance()->handle->RemoveInt("MarkerSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docDefaultLinkColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getDefaultLinkColor() {
    return instance()->DefaultLinkColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultDefaultLinkColor() {
    const static unsigned long def = 0x66FFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setDefaultLinkColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultLinkColor",v);
    instance()->DefaultLinkColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeDefaultLinkColor() {
    instance()->handle->RemoveUnsigned("DefaultLinkColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docDefaultShapeLineColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getDefaultShapeLineColor() {
    return instance()->DefaultShapeLineColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultDefaultShapeLineColor() {
    const static unsigned long def = 0x191919FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setDefaultShapeLineColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultShapeLineColor",v);
    instance()->DefaultShapeLineColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeDefaultShapeLineColor() {
    instance()->handle->RemoveUnsigned("DefaultShapeLineColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docDefaultShapeVertexColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getDefaultShapeVertexColor() {
    return instance()->DefaultShapeVertexColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultDefaultShapeVertexColor() {
    const static unsigned long def = 0x191919FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setDefaultShapeVertexColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultShapeVertexColor",v);
    instance()->DefaultShapeVertexColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeDefaultShapeVertexColor() {
    instance()->handle->RemoveUnsigned("DefaultShapeVertexColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docDefaultShapeColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getDefaultShapeColor() {
    return instance()->DefaultShapeColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultDefaultShapeColor() {
    const static unsigned long def = 0xCCCCCCFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setDefaultShapeColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultShapeColor",v);
    instance()->DefaultShapeColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeDefaultShapeColor() {
    instance()->handle->RemoveUnsigned("DefaultShapeColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docDefaultShapeTransparency() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getDefaultShapeTransparency() {
    return instance()->DefaultShapeTransparency;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultDefaultShapeTransparency() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setDefaultShapeTransparency(const long &v) {
    instance()->handle->SetInt("DefaultShapeTransparency",v);
    instance()->DefaultShapeTransparency = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeDefaultShapeTransparency() {
    instance()->handle->RemoveInt("DefaultShapeTransparency");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docDefaultShapeLineWidth() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getDefaultShapeLineWidth() {
    return instance()->DefaultShapeLineWidth;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultDefaultShapeLineWidth() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setDefaultShapeLineWidth(const long &v) {
    instance()->handle->SetInt("DefaultShapeLineWidth",v);
    instance()->DefaultShapeLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeDefaultShapeLineWidth() {
    instance()->handle->RemoveInt("DefaultShapeLineWidth");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docDefaultShapePointSize() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getDefaultShapePointSize() {
    return instance()->DefaultShapePointSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultDefaultShapePointSize() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setDefaultShapePointSize(const long &v) {
    instance()->handle->SetInt("DefaultShapePointSize",v);
    instance()->DefaultShapePointSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeDefaultShapePointSize() {
    instance()->handle->RemoveInt("DefaultShapePointSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docCoinCycleCheck() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getCoinCycleCheck() {
    return instance()->CoinCycleCheck;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultCoinCycleCheck() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setCoinCycleCheck(const bool &v) {
    instance()->handle->SetBool("CoinCycleCheck",v);
    instance()->CoinCycleCheck = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeCoinCycleCheck() {
    instance()->handle->RemoveBool("CoinCycleCheck");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docEnablePropertyViewForInactiveDocument() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getEnablePropertyViewForInactiveDocument() {
    return instance()->EnablePropertyViewForInactiveDocument;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultEnablePropertyViewForInactiveDocument() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setEnablePropertyViewForInactiveDocument(const bool &v) {
    instance()->handle->SetBool("EnablePropertyViewForInactiveDocument",v);
    instance()->EnablePropertyViewForInactiveDocument = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeEnablePropertyViewForInactiveDocument() {
    instance()->handle->RemoveBool("EnablePropertyViewForInactiveDocument");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShowSelectionBoundingBox() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show selection bounding box instead of highlight");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShowSelectionBoundingBox() {
    return instance()->ShowSelectionBoundingBox;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShowSelectionBoundingBox() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShowSelectionBoundingBox(const bool &v) {
    instance()->handle->SetBool("ShowSelectionBoundingBox",v);
    instance()->ShowSelectionBoundingBox = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShowSelectionBoundingBox() {
    instance()->handle->RemoveBool("ShowSelectionBoundingBox");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShowSelectionBoundingBoxThreshold() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Threshold for showing bounding box instead of selection highlight");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getShowSelectionBoundingBoxThreshold() {
    return instance()->ShowSelectionBoundingBoxThreshold;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultShowSelectionBoundingBoxThreshold() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShowSelectionBoundingBoxThreshold(const long &v) {
    instance()->handle->SetInt("ShowSelectionBoundingBoxThreshold",v);
    instance()->ShowSelectionBoundingBoxThreshold = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShowSelectionBoundingBoxThreshold() {
    instance()->handle->RemoveInt("ShowSelectionBoundingBoxThreshold");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docUpdateSelectionVisual() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getUpdateSelectionVisual() {
    return instance()->UpdateSelectionVisual;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultUpdateSelectionVisual() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setUpdateSelectionVisual(const bool &v) {
    instance()->handle->SetBool("UpdateSelectionVisual",v);
    instance()->UpdateSelectionVisual = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeUpdateSelectionVisual() {
    instance()->handle->RemoveBool("UpdateSelectionVisual");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docLinkChildrenDirect() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getLinkChildrenDirect() {
    return instance()->LinkChildrenDirect;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultLinkChildrenDirect() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setLinkChildrenDirect(const bool &v) {
    instance()->handle->SetBool("LinkChildrenDirect",v);
    instance()->LinkChildrenDirect = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeLinkChildrenDirect() {
    instance()->handle->RemoveBool("LinkChildrenDirect");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShowSelectionOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show selection always on top");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShowSelectionOnTop() {
    return instance()->ShowSelectionOnTop;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShowSelectionOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShowSelectionOnTop(const bool &v) {
    instance()->handle->SetBool("ShowSelectionOnTop",v);
    instance()->ShowSelectionOnTop = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShowSelectionOnTop() {
    instance()->handle->RemoveBool("ShowSelectionOnTop");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShowPreSelectedFaceOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show pre-selected face always on top");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShowPreSelectedFaceOnTop() {
    return instance()->ShowPreSelectedFaceOnTop;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShowPreSelectedFaceOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShowPreSelectedFaceOnTop(const bool &v) {
    instance()->handle->SetBool("ShowPreSelectedFaceOnTop",v);
    instance()->ShowPreSelectedFaceOnTop = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShowPreSelectedFaceOnTop() {
    instance()->handle->RemoveBool("ShowPreSelectedFaceOnTop");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShowPreSelectedFaceOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show pre-selected face outline");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShowPreSelectedFaceOutline() {
    return instance()->ShowPreSelectedFaceOutline;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShowPreSelectedFaceOutline() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShowPreSelectedFaceOutline(const bool &v) {
    instance()->handle->SetBool("ShowPreSelectedFaceOutline",v);
    instance()->ShowPreSelectedFaceOutline = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShowPreSelectedFaceOutline() {
    instance()->handle->RemoveBool("ShowPreSelectedFaceOutline");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShowSelectedFaceOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show selected face outline");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShowSelectedFaceOutline() {
    return instance()->ShowSelectedFaceOutline;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShowSelectedFaceOutline() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShowSelectedFaceOutline(const bool &v) {
    instance()->handle->SetBool("ShowSelectedFaceOutline",v);
    instance()->ShowSelectedFaceOutline = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShowSelectedFaceOutline() {
    instance()->handle->RemoveBool("ShowSelectedFaceOutline");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docOutlineThicken() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Muplication factor to increase outline width of the selected face.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getOutlineThicken() {
    return instance()->OutlineThicken;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultOutlineThicken() {
    const static double def = 4;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setOutlineThicken(const double &v) {
    instance()->handle->SetFloat("OutlineThicken",v);
    instance()->OutlineThicken = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeOutlineThicken() {
    instance()->handle->RemoveFloat("OutlineThicken");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docNoSelFaceHighlightWithOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Do not highlight selected face if outline is enabled");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getNoSelFaceHighlightWithOutline() {
    return instance()->NoSelFaceHighlightWithOutline;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultNoSelFaceHighlightWithOutline() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setNoSelFaceHighlightWithOutline(const bool &v) {
    instance()->handle->SetBool("NoSelFaceHighlightWithOutline",v);
    instance()->NoSelFaceHighlightWithOutline = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeNoSelFaceHighlightWithOutline() {
    instance()->handle->RemoveBool("NoSelFaceHighlightWithOutline");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docNoPreSelFaceHighlightWithOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Do not highlight pre-selected face if outline is enabled");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getNoPreSelFaceHighlightWithOutline() {
    return instance()->NoPreSelFaceHighlightWithOutline;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultNoPreSelFaceHighlightWithOutline() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setNoPreSelFaceHighlightWithOutline(const bool &v) {
    instance()->handle->SetBool("NoPreSelFaceHighlightWithOutline",v);
    instance()->NoPreSelFaceHighlightWithOutline = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeNoPreSelFaceHighlightWithOutline() {
    instance()->handle->RemoveBool("NoPreSelFaceHighlightWithOutline");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docAutoTransparentPick() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Make pre-selected object transparent for picking hidden lines");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getAutoTransparentPick() {
    return instance()->AutoTransparentPick;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultAutoTransparentPick() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setAutoTransparentPick(const bool &v) {
    instance()->handle->SetBool("AutoTransparentPick",v);
    instance()->AutoTransparentPick = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeAutoTransparentPick() {
    instance()->handle->RemoveBool("AutoTransparentPick");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectElementOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Do box/lasso element selection on already selected objects if SelectionOnTop is enabled.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getSelectElementOnTop() {
    return instance()->SelectElementOnTop;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultSelectElementOnTop() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectElementOnTop(const bool &v) {
    instance()->handle->SetBool("SelectElementOnTop",v);
    instance()->SelectElementOnTop = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectElementOnTop() {
    instance()->handle->RemoveBool("SelectElementOnTop");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docTransparencyOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Transparency for the selected object when being shown on top.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getTransparencyOnTop() {
    return instance()->TransparencyOnTop;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultTransparencyOnTop() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setTransparencyOnTop(const double &v) {
    instance()->handle->SetFloat("TransparencyOnTop",v);
    instance()->TransparencyOnTop = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeTransparencyOnTop() {
    instance()->handle->RemoveFloat("TransparencyOnTop");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineSync() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies how to sync hidden line draw style settings to opened document");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getHiddenLineSync() {
    return instance()->HiddenLineSync;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultHiddenLineSync() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineSync(const long &v) {
    instance()->handle->SetInt("HiddenLineSync",v);
    instance()->HiddenLineSync = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineSync() {
    instance()->handle->RemoveInt("HiddenLineSync");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineSelectionOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable hidden line/point selection when SelectionOnTop is active.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineSelectionOnTop() {
    return instance()->HiddenLineSelectionOnTop;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineSelectionOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineSelectionOnTop(const bool &v) {
    instance()->handle->SetBool("HiddenLineSelectionOnTop",v);
    instance()->HiddenLineSelectionOnTop = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineSelectionOnTop() {
    instance()->handle->RemoveBool("HiddenLineSelectionOnTop");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPartialHighlightOnFullSelect() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable partial highlight on full selection for object that supports it.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getPartialHighlightOnFullSelect() {
    return instance()->PartialHighlightOnFullSelect;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultPartialHighlightOnFullSelect() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPartialHighlightOnFullSelect(const bool &v) {
    instance()->handle->SetBool("PartialHighlightOnFullSelect",v);
    instance()->PartialHighlightOnFullSelect = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePartialHighlightOnFullSelect() {
    instance()->handle->RemoveBool("PartialHighlightOnFullSelect");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionLineThicken() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Muplication factor to increase the width of the selected line.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getSelectionLineThicken() {
    return instance()->SelectionLineThicken;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultSelectionLineThicken() {
    const static double def = 1.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionLineThicken(const double &v) {
    instance()->handle->SetFloat("SelectionLineThicken",v);
    instance()->SelectionLineThicken = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionLineThicken() {
    instance()->handle->RemoveFloat("SelectionLineThicken");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionLineMaxWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Limit the selected line width when applying line thickening.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getSelectionLineMaxWidth() {
    return instance()->SelectionLineMaxWidth;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultSelectionLineMaxWidth() {
    const static double def = 4.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionLineMaxWidth(const double &v) {
    instance()->handle->SetFloat("SelectionLineMaxWidth",v);
    instance()->SelectionLineMaxWidth = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionLineMaxWidth() {
    instance()->handle->RemoveFloat("SelectionLineMaxWidth");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionPointScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Muplication factor to increase the size of the selected point.\n"
"If zero, then use line multiplication factor.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getSelectionPointScale() {
    return instance()->SelectionPointScale;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultSelectionPointScale() {
    const static double def = 2.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionPointScale(const double &v) {
    instance()->handle->SetFloat("SelectionPointScale",v);
    instance()->SelectionPointScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionPointScale() {
    instance()->handle->RemoveFloat("SelectionPointScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionPointMaxSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Limit the selected point size when applying size scale.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getSelectionPointMaxSize() {
    return instance()->SelectionPointMaxSize;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultSelectionPointMaxSize() {
    const static double def = 6.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionPointMaxSize(const double &v) {
    instance()->handle->SetFloat("SelectionPointMaxSize",v);
    instance()->SelectionPointMaxSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionPointMaxSize() {
    instance()->handle->RemoveFloat("SelectionPointMaxSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPickRadius() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Area for picking elements in 3D view. Larger value make it easy to pick things,\n"
"but can also make small features impossible to select.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getPickRadius() {
    return instance()->PickRadius;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultPickRadius() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPickRadius(const double &v) {
    instance()->handle->SetFloat("PickRadius",v);
    instance()->PickRadius = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePickRadius() {
    instance()->handle->RemoveFloat("PickRadius");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionTransparency() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getSelectionTransparency() {
    return instance()->SelectionTransparency;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultSelectionTransparency() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionTransparency(const double &v) {
    instance()->handle->SetFloat("SelectionTransparency",v);
    instance()->SelectionTransparency = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionTransparency() {
    instance()->handle->RemoveFloat("SelectionTransparency");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionLinePattern() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getSelectionLinePattern() {
    return instance()->SelectionLinePattern;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultSelectionLinePattern() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionLinePattern(const long &v) {
    instance()->handle->SetInt("SelectionLinePattern",v);
    instance()->SelectionLinePattern = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionLinePattern() {
    instance()->handle->RemoveInt("SelectionLinePattern");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionLinePatternScale() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getSelectionLinePatternScale() {
    return instance()->SelectionLinePatternScale;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultSelectionLinePatternScale() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionLinePatternScale(const long &v) {
    instance()->handle->SetInt("SelectionLinePatternScale",v);
    instance()->SelectionLinePatternScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionLinePatternScale() {
    instance()->handle->RemoveInt("SelectionLinePatternScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionHiddenLineWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Width of the hidden line.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getSelectionHiddenLineWidth() {
    return instance()->SelectionHiddenLineWidth;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultSelectionHiddenLineWidth() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionHiddenLineWidth(const double &v) {
    instance()->handle->SetFloat("SelectionHiddenLineWidth",v);
    instance()->SelectionHiddenLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionHiddenLineWidth() {
    instance()->handle->RemoveFloat("SelectionHiddenLineWidth");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionBBoxLineWidth() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getSelectionBBoxLineWidth() {
    return instance()->SelectionBBoxLineWidth;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultSelectionBBoxLineWidth() {
    const static double def = 3.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionBBoxLineWidth(const double &v) {
    instance()->handle->SetFloat("SelectionBBoxLineWidth",v);
    instance()->SelectionBBoxLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionBBoxLineWidth() {
    instance()->handle->RemoveFloat("SelectionBBoxLineWidth");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShowHighlightEdgeOnly() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show pre-selection highlight edge only");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShowHighlightEdgeOnly() {
    return instance()->ShowHighlightEdgeOnly;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShowHighlightEdgeOnly() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShowHighlightEdgeOnly(const bool &v) {
    instance()->handle->SetBool("ShowHighlightEdgeOnly",v);
    instance()->ShowHighlightEdgeOnly = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShowHighlightEdgeOnly() {
    instance()->handle->RemoveBool("ShowHighlightEdgeOnly");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPreSelectionDelay() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getPreSelectionDelay() {
    return instance()->PreSelectionDelay;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultPreSelectionDelay() {
    const static double def = 0.1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPreSelectionDelay(const double &v) {
    instance()->handle->SetFloat("PreSelectionDelay",v);
    instance()->PreSelectionDelay = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePreSelectionDelay() {
    instance()->handle->RemoveFloat("PreSelectionDelay");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPickBackFaceDelay() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPickBackFaceDelay() {
    return instance()->PickBackFaceDelay;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPickBackFaceDelay() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPickBackFaceDelay(const long &v) {
    instance()->handle->SetInt("PickBackFaceDelay",v);
    instance()->PickBackFaceDelay = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePickBackFaceDelay() {
    instance()->handle->RemoveInt("PickBackFaceDelay");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docUseNewRayPick() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getUseNewRayPick() {
    return instance()->UseNewRayPick;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultUseNewRayPick() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setUseNewRayPick(const bool &v) {
    instance()->handle->SetBool("UseNewRayPick",v);
    instance()->UseNewRayPick = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeUseNewRayPick() {
    instance()->handle->RemoveBool("UseNewRayPick");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docViewSelectionExtendFactor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getViewSelectionExtendFactor() {
    return instance()->ViewSelectionExtendFactor;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultViewSelectionExtendFactor() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setViewSelectionExtendFactor(const double &v) {
    instance()->handle->SetFloat("ViewSelectionExtendFactor",v);
    instance()->ViewSelectionExtendFactor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeViewSelectionExtendFactor() {
    instance()->handle->RemoveFloat("ViewSelectionExtendFactor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docUseTightBoundingBox() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show more accurate bounds when using bounding box selection style");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getUseTightBoundingBox() {
    return instance()->UseTightBoundingBox;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultUseTightBoundingBox() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setUseTightBoundingBox(const bool &v) {
    instance()->handle->SetBool("UseTightBoundingBox",v);
    instance()->UseTightBoundingBox = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeUseTightBoundingBox() {
    instance()->handle->RemoveBool("UseTightBoundingBox");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docUseBoundingBoxCache() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getUseBoundingBoxCache() {
    return instance()->UseBoundingBoxCache;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultUseBoundingBoxCache() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setUseBoundingBoxCache(const bool &v) {
    instance()->handle->SetBool("UseBoundingBoxCache",v);
    instance()->UseBoundingBoxCache = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeUseBoundingBoxCache() {
    instance()->handle->RemoveBool("UseBoundingBoxCache");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRenderProjectedBBox() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show projected bounding box that is aligned to axes of\n"
"global coordinate space");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getRenderProjectedBBox() {
    return instance()->RenderProjectedBBox;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultRenderProjectedBBox() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRenderProjectedBBox(const bool &v) {
    instance()->handle->SetBool("RenderProjectedBBox",v);
    instance()->RenderProjectedBBox = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRenderProjectedBBox() {
    instance()->handle->RemoveBool("RenderProjectedBBox");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionFaceWire() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show hidden tirangulation wires for selected face");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getSelectionFaceWire() {
    return instance()->SelectionFaceWire;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultSelectionFaceWire() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionFaceWire(const bool &v) {
    instance()->handle->SetBool("SelectionFaceWire",v);
    instance()->SelectionFaceWire = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionFaceWire() {
    instance()->handle->RemoveBool("SelectionFaceWire");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docNewDocumentCameraScale() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getNewDocumentCameraScale() {
    return instance()->NewDocumentCameraScale;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultNewDocumentCameraScale() {
    const static double def = 100.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setNewDocumentCameraScale(const double &v) {
    instance()->handle->SetFloat("NewDocumentCameraScale",v);
    instance()->NewDocumentCameraScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeNewDocumentCameraScale() {
    instance()->handle->RemoveFloat("NewDocumentCameraScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docMaxOnTopSelections() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getMaxOnTopSelections() {
    return instance()->MaxOnTopSelections;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultMaxOnTopSelections() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setMaxOnTopSelections(const long &v) {
    instance()->handle->SetInt("MaxOnTopSelections",v);
    instance()->MaxOnTopSelections = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeMaxOnTopSelections() {
    instance()->handle->RemoveInt("MaxOnTopSelections");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docMaxViewSelections() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getMaxViewSelections() {
    return instance()->MaxViewSelections;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultMaxViewSelections() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setMaxViewSelections(const long &v) {
    instance()->handle->SetInt("MaxViewSelections",v);
    instance()->MaxViewSelections = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeMaxViewSelections() {
    instance()->handle->RemoveInt("MaxViewSelections");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docMaxSelectionNotification() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getMaxSelectionNotification() {
    return instance()->MaxSelectionNotification;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultMaxSelectionNotification() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setMaxSelectionNotification(const long &v) {
    instance()->handle->SetInt("MaxSelectionNotification",v);
    instance()->MaxSelectionNotification = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeMaxSelectionNotification() {
    instance()->handle->RemoveInt("MaxSelectionNotification");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docMapChildrenPlacement() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Map child object into parent's coordinate space when showing on top.\n"
"Note that once activated, this option will also activate option ShowOnTop.\n"
"WARNING! This is an experimental option. Please use with caution.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getMapChildrenPlacement() {
    return instance()->MapChildrenPlacement;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultMapChildrenPlacement() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setMapChildrenPlacement(const bool &v) {
    instance()->handle->SetBool("MapChildrenPlacement",v);
    instance()->MapChildrenPlacement = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeMapChildrenPlacement() {
    instance()->handle->RemoveBool("MapChildrenPlacement");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docEditingTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Automatically make all object transparent except the one in edit");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getEditingTransparency() {
    return instance()->EditingTransparency;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultEditingTransparency() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setEditingTransparency(const double &v) {
    instance()->handle->SetFloat("EditingTransparency",v);
    instance()->EditingTransparency = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeEditingTransparency() {
    instance()->handle->RemoveFloat("EditingTransparency");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Overridden transparency value of all objects in the scene.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getHiddenLineTransparency() {
    return instance()->HiddenLineTransparency;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultHiddenLineTransparency() {
    const static double def = 0.4;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineTransparency(const double &v) {
    instance()->handle->SetFloat("HiddenLineTransparency",v);
    instance()->HiddenLineTransparency = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineTransparency() {
    instance()->handle->RemoveFloat("HiddenLineTransparency");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineOverrideTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to override transparency of all objects in the scene.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineOverrideTransparency() {
    return instance()->HiddenLineOverrideTransparency;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineOverrideTransparency() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineOverrideTransparency(const bool &v) {
    instance()->handle->SetBool("HiddenLineOverrideTransparency",v);
    instance()->HiddenLineOverrideTransparency = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineOverrideTransparency() {
    instance()->handle->RemoveBool("HiddenLineOverrideTransparency");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineFaceColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getHiddenLineFaceColor() {
    return instance()->HiddenLineFaceColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultHiddenLineFaceColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineFaceColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("HiddenLineFaceColor",v);
    instance()->HiddenLineFaceColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineFaceColor() {
    instance()->handle->RemoveUnsigned("HiddenLineFaceColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineOverrideFaceColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable preselection and highlight by specified color.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineOverrideFaceColor() {
    return instance()->HiddenLineOverrideFaceColor;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineOverrideFaceColor() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineOverrideFaceColor(const bool &v) {
    instance()->handle->SetBool("HiddenLineOverrideFaceColor",v);
    instance()->HiddenLineOverrideFaceColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineOverrideFaceColor() {
    instance()->handle->RemoveBool("HiddenLineOverrideFaceColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getHiddenLineColor() {
    return instance()->HiddenLineColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultHiddenLineColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("HiddenLineColor",v);
    instance()->HiddenLineColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineColor() {
    instance()->handle->RemoveUnsigned("HiddenLineColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineOverrideColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable selection highlighting and use specified color");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineOverrideColor() {
    return instance()->HiddenLineOverrideColor;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineOverrideColor() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineOverrideColor(const bool &v) {
    instance()->handle->SetBool("HiddenLineOverrideColor",v);
    instance()->HiddenLineOverrideColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineOverrideColor() {
    instance()->handle->RemoveBool("HiddenLineOverrideColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineBackground() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getHiddenLineBackground() {
    return instance()->HiddenLineBackground;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultHiddenLineBackground() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineBackground(const unsigned long &v) {
    instance()->handle->SetUnsigned("HiddenLineBackground",v);
    instance()->HiddenLineBackground = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineBackground() {
    instance()->handle->RemoveUnsigned("HiddenLineBackground");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineOverrideBackground() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineOverrideBackground() {
    return instance()->HiddenLineOverrideBackground;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineOverrideBackground() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineOverrideBackground(const bool &v) {
    instance()->handle->SetBool("HiddenLineOverrideBackground",v);
    instance()->HiddenLineOverrideBackground = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineOverrideBackground() {
    instance()->handle->RemoveBool("HiddenLineOverrideBackground");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineShaded() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to enable shading in hidden line draw style");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineShaded() {
    return instance()->HiddenLineShaded;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineShaded() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineShaded(const bool &v) {
    instance()->handle->SetBool("HiddenLineShaded",v);
    instance()->HiddenLineShaded = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineShaded() {
    instance()->handle->RemoveBool("HiddenLineShaded");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineShowOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show outline in hidden line draw style (only works in experiemental renderer),.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineShowOutline() {
    return instance()->HiddenLineShowOutline;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineShowOutline() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineShowOutline(const bool &v) {
    instance()->handle->SetBool("HiddenLineShowOutline",v);
    instance()->HiddenLineShowOutline = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineShowOutline() {
    instance()->handle->RemoveBool("HiddenLineShowOutline");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLinePerFaceOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Render per face outline in hidden line draw style (Warning! this may cause slow down),.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLinePerFaceOutline() {
    return instance()->HiddenLinePerFaceOutline;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLinePerFaceOutline() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLinePerFaceOutline(const bool &v) {
    instance()->handle->SetBool("HiddenLinePerFaceOutline",v);
    instance()->HiddenLinePerFaceOutline = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLinePerFaceOutline() {
    instance()->handle->RemoveBool("HiddenLinePerFaceOutline");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineSceneOutline() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Render outline of the whole scene.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineSceneOutline() {
    return instance()->HiddenLineSceneOutline;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineSceneOutline() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineSceneOutline(const bool &v) {
    instance()->handle->SetBool("HiddenLineSceneOutline",v);
    instance()->HiddenLineSceneOutline = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineSceneOutline() {
    instance()->handle->RemoveBool("HiddenLineSceneOutline");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineOutlineWidth() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getHiddenLineOutlineWidth() {
    return instance()->HiddenLineOutlineWidth;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultHiddenLineOutlineWidth() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineOutlineWidth(const double &v) {
    instance()->handle->SetFloat("HiddenLineOutlineWidth",v);
    instance()->HiddenLineOutlineWidth = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineOutlineWidth() {
    instance()->handle->RemoveFloat("HiddenLineOutlineWidth");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineWidth() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getHiddenLineWidth() {
    return instance()->HiddenLineWidth;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultHiddenLineWidth() {
    const static double def = 1.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineWidth(const double &v) {
    instance()->handle->SetFloat("HiddenLineWidth",v);
    instance()->HiddenLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineWidth() {
    instance()->handle->RemoveFloat("HiddenLineWidth");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLinePointSize() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getHiddenLinePointSize() {
    return instance()->HiddenLinePointSize;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultHiddenLinePointSize() {
    const static double def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLinePointSize(const double &v) {
    instance()->handle->SetFloat("HiddenLinePointSize",v);
    instance()->HiddenLinePointSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLinePointSize() {
    instance()->handle->RemoveFloat("HiddenLinePointSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineHideSeam() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Hide seam edges in hidden line draw style.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineHideSeam() {
    return instance()->HiddenLineHideSeam;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineHideSeam() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineHideSeam(const bool &v) {
    instance()->handle->SetBool("HiddenLineHideSeam",v);
    instance()->HiddenLineHideSeam = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineHideSeam() {
    instance()->handle->RemoveBool("HiddenLineHideSeam");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineHideVertex() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Hide vertex in hidden line draw style.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineHideVertex() {
    return instance()->HiddenLineHideVertex;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineHideVertex() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineHideVertex(const bool &v) {
    instance()->handle->SetBool("HiddenLineHideVertex",v);
    instance()->HiddenLineHideVertex = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineHideVertex() {
    instance()->handle->RemoveBool("HiddenLineHideVertex");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHiddenLineHideFace() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Hide face in hidden line draw style.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHiddenLineHideFace() {
    return instance()->HiddenLineHideFace;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHiddenLineHideFace() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHiddenLineHideFace(const bool &v) {
    instance()->handle->SetBool("HiddenLineHideFace",v);
    instance()->HiddenLineHideFace = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHiddenLineHideFace() {
    instance()->handle->RemoveBool("HiddenLineHideFace");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docStatusMessageTimeout() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getStatusMessageTimeout() {
    return instance()->StatusMessageTimeout;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultStatusMessageTimeout() {
    const static long def = 5000;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setStatusMessageTimeout(const long &v) {
    instance()->handle->SetInt("StatusMessageTimeout",v);
    instance()->StatusMessageTimeout = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeStatusMessageTimeout() {
    instance()->handle->RemoveInt("StatusMessageTimeout");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowSync() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies how to sync shadow draw style settings to opened document");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getShadowSync() {
    return instance()->ShadowSync;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultShadowSync() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowSync(const long &v) {
    instance()->handle->SetInt("ShadowSync",v);
    instance()->ShadowSync = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowSync() {
    instance()->handle->RemoveInt("ShadowSync");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowFlatLines() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Draw object with 'Flat lines' style when shadow is enabled.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShadowFlatLines() {
    return instance()->ShadowFlatLines;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShadowFlatLines() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowFlatLines(const bool &v) {
    instance()->handle->SetBool("ShadowFlatLines",v);
    instance()->ShadowFlatLines = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowFlatLines() {
    instance()->handle->RemoveBool("ShadowFlatLines");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowDisplayMode() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Override view object display mode when shadow is enabled.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getShadowDisplayMode() {
    return instance()->ShadowDisplayMode;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultShadowDisplayMode() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowDisplayMode(const long &v) {
    instance()->handle->SetInt("ShadowDisplayMode",v);
    instance()->ShadowDisplayMode = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowDisplayMode() {
    instance()->handle->RemoveInt("ShadowDisplayMode");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowSpotLight() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to use spot light or directional light.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShadowSpotLight() {
    return instance()->ShadowSpotLight;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShadowSpotLight() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowSpotLight(const bool &v) {
    instance()->handle->SetBool("ShadowSpotLight",v);
    instance()->ShadowSpotLight = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowSpotLight() {
    instance()->handle->RemoveBool("ShadowSpotLight");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowLightIntensity() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowLightIntensity() {
    return instance()->ShadowLightIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowLightIntensity() {
    const static double def = 0.8;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowLightIntensity(const double &v) {
    instance()->handle->SetFloat("ShadowLightIntensity",v);
    instance()->ShadowLightIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowLightIntensity() {
    instance()->handle->RemoveFloat("ShadowLightIntensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowLightDirectionX() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowLightDirectionX() {
    return instance()->ShadowLightDirectionX;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowLightDirectionX() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowLightDirectionX(const double &v) {
    instance()->handle->SetFloat("ShadowLightDirectionX",v);
    instance()->ShadowLightDirectionX = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowLightDirectionX() {
    instance()->handle->RemoveFloat("ShadowLightDirectionX");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowLightDirectionY() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowLightDirectionY() {
    return instance()->ShadowLightDirectionY;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowLightDirectionY() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowLightDirectionY(const double &v) {
    instance()->handle->SetFloat("ShadowLightDirectionY",v);
    instance()->ShadowLightDirectionY = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowLightDirectionY() {
    instance()->handle->RemoveFloat("ShadowLightDirectionY");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowLightDirectionZ() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowLightDirectionZ() {
    return instance()->ShadowLightDirectionZ;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowLightDirectionZ() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowLightDirectionZ(const double &v) {
    instance()->handle->SetFloat("ShadowLightDirectionZ",v);
    instance()->ShadowLightDirectionZ = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowLightDirectionZ() {
    instance()->handle->RemoveFloat("ShadowLightDirectionZ");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowLightColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getShadowLightColor() {
    return instance()->ShadowLightColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultShadowLightColor() {
    const static unsigned long def = 0xF0FDFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowLightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("ShadowLightColor",v);
    instance()->ShadowLightColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowLightColor() {
    instance()->handle->RemoveUnsigned("ShadowLightColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowShowGround() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to show auto generated ground face. You can specify you own ground\n"
"object by changing its view property 'ShadowStyle' to 'Shadowed', meaning\n"
"that it will only receive but not cast shadow.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShadowShowGround() {
    return instance()->ShadowShowGround;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShadowShowGround() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowShowGround(const bool &v) {
    instance()->handle->SetBool("ShadowShowGround",v);
    instance()->ShadowShowGround = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowShowGround() {
    instance()->handle->RemoveBool("ShadowShowGround");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowGroundBackFaceCull() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to show the ground when viewing from under the ground face");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShadowGroundBackFaceCull() {
    return instance()->ShadowGroundBackFaceCull;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShadowGroundBackFaceCull() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowGroundBackFaceCull(const bool &v) {
    instance()->handle->SetBool("ShadowGroundBackFaceCull",v);
    instance()->ShadowGroundBackFaceCull = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowGroundBackFaceCull() {
    instance()->handle->RemoveBool("ShadowGroundBackFaceCull");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowGroundScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"The auto generated ground face is determined by the scene bounding box\n"
"multiplied by this scale");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowGroundScale() {
    return instance()->ShadowGroundScale;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowGroundScale() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowGroundScale(const double &v) {
    instance()->handle->SetFloat("ShadowGroundScale",v);
    instance()->ShadowGroundScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowGroundScale() {
    instance()->handle->RemoveFloat("ShadowGroundScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowGroundColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getShadowGroundColor() {
    return instance()->ShadowGroundColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultShadowGroundColor() {
    const static unsigned long def = 0x7D7D7DFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowGroundColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("ShadowGroundColor",v);
    instance()->ShadowGroundColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowGroundColor() {
    instance()->handle->RemoveUnsigned("ShadowGroundColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowGroundBumpMap() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & ViewParams::getShadowGroundBumpMap() {
    return instance()->ShadowGroundBumpMap;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & ViewParams::defaultShadowGroundBumpMap() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowGroundBumpMap(const std::string &v) {
    instance()->handle->SetASCII("ShadowGroundBumpMap",v);
    instance()->ShadowGroundBumpMap = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowGroundBumpMap() {
    instance()->handle->RemoveASCII("ShadowGroundBumpMap");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowGroundTexture() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & ViewParams::getShadowGroundTexture() {
    return instance()->ShadowGroundTexture;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & ViewParams::defaultShadowGroundTexture() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowGroundTexture(const std::string &v) {
    instance()->handle->SetASCII("ShadowGroundTexture",v);
    instance()->ShadowGroundTexture = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowGroundTexture() {
    instance()->handle->RemoveASCII("ShadowGroundTexture");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowGroundTextureSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the physcal length of the ground texture image size.\n"
"Texture mappings beyond this size will be wrapped around");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowGroundTextureSize() {
    return instance()->ShadowGroundTextureSize;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowGroundTextureSize() {
    const static double def = 100.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowGroundTextureSize(const double &v) {
    instance()->handle->SetFloat("ShadowGroundTextureSize",v);
    instance()->ShadowGroundTextureSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowGroundTextureSize() {
    instance()->handle->RemoveFloat("ShadowGroundTextureSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowGroundTransparency() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifics the ground transparency. When set to 0, the non-shadowed part\n"
"of the ground will be complete transparent, showing only the shadowed part\n"
"of the ground with some transparency.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowGroundTransparency() {
    return instance()->ShadowGroundTransparency;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowGroundTransparency() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowGroundTransparency(const double &v) {
    instance()->handle->SetFloat("ShadowGroundTransparency",v);
    instance()->ShadowGroundTransparency = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowGroundTransparency() {
    instance()->handle->RemoveFloat("ShadowGroundTransparency");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowGroundShading() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Render ground with shading. If disabled, the ground and the shadow casted\n"
"on ground will not change shading when viewing in different angle.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShadowGroundShading() {
    return instance()->ShadowGroundShading;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShadowGroundShading() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowGroundShading(const bool &v) {
    instance()->handle->SetBool("ShadowGroundShading",v);
    instance()->ShadowGroundShading = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowGroundShading() {
    instance()->handle->RemoveBool("ShadowGroundShading");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowExtraRedraw() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShadowExtraRedraw() {
    return instance()->ShadowExtraRedraw;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShadowExtraRedraw() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowExtraRedraw(const bool &v) {
    instance()->handle->SetBool("ShadowExtraRedraw",v);
    instance()->ShadowExtraRedraw = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowExtraRedraw() {
    instance()->handle->RemoveBool("ShadowExtraRedraw");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowSmoothBorder() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the blur raidus of the shadow edge. Higher number will result in\n"
"slower rendering speed on scene change. Use a lower 'Precision' value to\n"
"counter the effect.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getShadowSmoothBorder() {
    return instance()->ShadowSmoothBorder;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultShadowSmoothBorder() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowSmoothBorder(const long &v) {
    instance()->handle->SetInt("ShadowSmoothBorder",v);
    instance()->ShadowSmoothBorder = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowSmoothBorder() {
    instance()->handle->RemoveInt("ShadowSmoothBorder");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowSpreadSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the spread size for a soft shadow. The resulting spread size is\n"
"dependent on the model scale");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getShadowSpreadSize() {
    return instance()->ShadowSpreadSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultShadowSpreadSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowSpreadSize(const long &v) {
    instance()->handle->SetInt("ShadowSpreadSize",v);
    instance()->ShadowSpreadSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowSpreadSize() {
    instance()->handle->RemoveInt("ShadowSpreadSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowSpreadSampleSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the sample size used for rendering shadow spread. A value 0\n"
"corresponds to a sampling square of 2x2. And 1 corresponds to 3x3, etc.\n"
"The bigger the size the slower the rendering speed. You can use a lower\n"
"'Precision' value to counter the effect.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getShadowSpreadSampleSize() {
    return instance()->ShadowSpreadSampleSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultShadowSpreadSampleSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowSpreadSampleSize(const long &v) {
    instance()->handle->SetInt("ShadowSpreadSampleSize",v);
    instance()->ShadowSpreadSampleSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowSpreadSampleSize() {
    instance()->handle->RemoveInt("ShadowSpreadSampleSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowPrecision() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies shadow precision. This parameter affects the internal texture\n"
"size used to hold the casted shadows. You might want a bigger texture if\n"
"you want a hard shadow but a smaller one for soft shadow.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowPrecision() {
    return instance()->ShadowPrecision;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowPrecision() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowPrecision(const double &v) {
    instance()->handle->SetFloat("ShadowPrecision",v);
    instance()->ShadowPrecision = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowPrecision() {
    instance()->handle->RemoveFloat("ShadowPrecision");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowEpsilon() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Epsilon is used to offset the shadow map depth from the model depth.\n"
"Should be set to as low a number as possible without causing flickering\n"
"in the shadows or on non-shadowed objects.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowEpsilon() {
    return instance()->ShadowEpsilon;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowEpsilon() {
    const static double def = 1e-05;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowEpsilon(const double &v) {
    instance()->handle->SetFloat("ShadowEpsilon",v);
    instance()->ShadowEpsilon = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowEpsilon() {
    instance()->handle->RemoveFloat("ShadowEpsilon");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowThreshold() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Can be used to avoid light bleeding in merged shadows cast from different objects.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowThreshold() {
    return instance()->ShadowThreshold;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowThreshold() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowThreshold(const double &v) {
    instance()->handle->SetFloat("ShadowThreshold",v);
    instance()->ShadowThreshold = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowThreshold() {
    instance()->handle->RemoveFloat("ShadowThreshold");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowBoundBoxScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Scene bounding box is used to determine the scale of the shadow texture.\n"
"You can increase the bounding box scale to avoid execessive clipping of\n"
"shadows when viewing up close in certain angle.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowBoundBoxScale() {
    return instance()->ShadowBoundBoxScale;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowBoundBoxScale() {
    const static double def = 1.2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowBoundBoxScale(const double &v) {
    instance()->handle->SetFloat("ShadowBoundBoxScale",v);
    instance()->ShadowBoundBoxScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowBoundBoxScale() {
    instance()->handle->RemoveFloat("ShadowBoundBoxScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowMaxDistance() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifics the clipping distance for when rendering shadows.\n"
"You can increase the bounding box scale to avoid execessive\n"
"clipping of shadows when viewing up close in certain angle.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getShadowMaxDistance() {
    return instance()->ShadowMaxDistance;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultShadowMaxDistance() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowMaxDistance(const double &v) {
    instance()->handle->SetFloat("ShadowMaxDistance",v);
    instance()->ShadowMaxDistance = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowMaxDistance() {
    instance()->handle->RemoveFloat("ShadowMaxDistance");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowTransparentShadow() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Whether to cast shadow from transparent objects.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShadowTransparentShadow() {
    return instance()->ShadowTransparentShadow;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShadowTransparentShadow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowTransparentShadow(const bool &v) {
    instance()->handle->SetBool("ShadowTransparentShadow",v);
    instance()->ShadowTransparentShadow = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowTransparentShadow() {
    instance()->handle->RemoveBool("ShadowTransparentShadow");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShadowUpdateGround() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Auto update shadow ground on scene changes. You can manually\n"
"update the ground by using the 'Fit view' command");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShadowUpdateGround() {
    return instance()->ShadowUpdateGround;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShadowUpdateGround() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShadowUpdateGround(const bool &v) {
    instance()->handle->SetBool("ShadowUpdateGround",v);
    instance()->ShadowUpdateGround = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShadowUpdateGround() {
    instance()->handle->RemoveBool("ShadowUpdateGround");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPropertyViewTimer() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getPropertyViewTimer() {
    return instance()->PropertyViewTimer;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultPropertyViewTimer() {
    const static unsigned long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPropertyViewTimer(const unsigned long &v) {
    instance()->handle->SetUnsigned("PropertyViewTimer",v);
    instance()->PropertyViewTimer = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePropertyViewTimer() {
    instance()->handle->RemoveUnsigned("PropertyViewTimer");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docHierarchyAscend() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable selection of upper hierarchy by repeatedly click some already\n"
"selected sub-element.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getHierarchyAscend() {
    return instance()->HierarchyAscend;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultHierarchyAscend() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setHierarchyAscend(const bool &v) {
    instance()->handle->SetBool("HierarchyAscend",v);
    instance()->HierarchyAscend = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeHierarchyAscend() {
    instance()->handle->RemoveBool("HierarchyAscend");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docCommandHistorySize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum number of commands saved in history");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getCommandHistorySize() {
    return instance()->CommandHistorySize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultCommandHistorySize() {
    const static long def = 20;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setCommandHistorySize(const long &v) {
    instance()->handle->SetInt("CommandHistorySize",v);
    instance()->CommandHistorySize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeCommandHistorySize() {
    instance()->handle->RemoveInt("CommandHistorySize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuIconSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu icon size");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPieMenuIconSize() {
    return instance()->PieMenuIconSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPieMenuIconSize() {
    const static long def = 24;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuIconSize(const long &v) {
    instance()->handle->SetInt("PieMenuIconSize",v);
    instance()->PieMenuIconSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuIconSize() {
    instance()->handle->RemoveInt("PieMenuIconSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuRadius() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu radius");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPieMenuRadius() {
    return instance()->PieMenuRadius;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPieMenuRadius() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuRadius(const long &v) {
    instance()->handle->SetInt("PieMenuRadius",v);
    instance()->PieMenuRadius = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuRadius() {
    instance()->handle->RemoveInt("PieMenuRadius");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuTriggerRadius() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu hover trigger radius");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPieMenuTriggerRadius() {
    return instance()->PieMenuTriggerRadius;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPieMenuTriggerRadius() {
    const static long def = 60;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuTriggerRadius(const long &v) {
    instance()->handle->SetInt("PieMenuTriggerRadius",v);
    instance()->PieMenuTriggerRadius = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuTriggerRadius() {
    instance()->handle->RemoveInt("PieMenuTriggerRadius");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuFontSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu font size");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPieMenuFontSize() {
    return instance()->PieMenuFontSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPieMenuFontSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuFontSize(const long &v) {
    instance()->handle->SetInt("PieMenuFontSize",v);
    instance()->PieMenuFontSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuFontSize() {
    instance()->handle->RemoveInt("PieMenuFontSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuTriggerDelay() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu sub-menu hover trigger delay, 0 to disable");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPieMenuTriggerDelay() {
    return instance()->PieMenuTriggerDelay;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPieMenuTriggerDelay() {
    const static long def = 200;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuTriggerDelay(const long &v) {
    instance()->handle->SetInt("PieMenuTriggerDelay",v);
    instance()->PieMenuTriggerDelay = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuTriggerDelay() {
    instance()->handle->RemoveInt("PieMenuTriggerDelay");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuTriggerAction() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu action trigger on hover");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getPieMenuTriggerAction() {
    return instance()->PieMenuTriggerAction;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultPieMenuTriggerAction() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuTriggerAction(const bool &v) {
    instance()->handle->SetBool("PieMenuTriggerAction",v);
    instance()->PieMenuTriggerAction = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuTriggerAction() {
    instance()->handle->RemoveBool("PieMenuTriggerAction");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuAnimationDuration() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu animation duration, 0 to disable");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPieMenuAnimationDuration() {
    return instance()->PieMenuAnimationDuration;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPieMenuAnimationDuration() {
    const static long def = 250;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuAnimationDuration(const long &v) {
    instance()->handle->SetInt("PieMenuAnimationDuration",v);
    instance()->PieMenuAnimationDuration = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuAnimationDuration() {
    instance()->handle->RemoveInt("PieMenuAnimationDuration");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuAnimationCurve() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu animation curve type");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPieMenuAnimationCurve() {
    return instance()->PieMenuAnimationCurve;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPieMenuAnimationCurve() {
    const static long def = 38;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuAnimationCurve(const long &v) {
    instance()->handle->SetInt("PieMenuAnimationCurve",v);
    instance()->PieMenuAnimationCurve = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuAnimationCurve() {
    instance()->handle->RemoveInt("PieMenuAnimationCurve");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuCenterRadius() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Pie menu center circle radius, 0 to disable");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPieMenuCenterRadius() {
    return instance()->PieMenuCenterRadius;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPieMenuCenterRadius() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuCenterRadius(const long &v) {
    instance()->handle->SetInt("PieMenuCenterRadius",v);
    instance()->PieMenuCenterRadius = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuCenterRadius() {
    instance()->handle->RemoveInt("PieMenuCenterRadius");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPieMenuPopup() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show pie menu as a popup widget, disable it to work around some graphics driver problem");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getPieMenuPopup() {
    return instance()->PieMenuPopup;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultPieMenuPopup() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPieMenuPopup(const bool &v) {
    instance()->handle->SetBool("PieMenuPopup",v);
    instance()->PieMenuPopup = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePieMenuPopup() {
    instance()->handle->RemoveBool("PieMenuPopup");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docStickyTaskControl() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Makes the task dialog buttons stay at top or bottom of task view.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getStickyTaskControl() {
    return instance()->StickyTaskControl;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultStickyTaskControl() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setStickyTaskControl(const bool &v) {
    instance()->handle->SetBool("StickyTaskControl",v);
    instance()->StickyTaskControl = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeStickyTaskControl() {
    instance()->handle->RemoveBool("StickyTaskControl");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docColorOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show object on top when editing its color.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getColorOnTop() {
    return instance()->ColorOnTop;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultColorOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setColorOnTop(const bool &v) {
    instance()->handle->SetBool("ColorOnTop",v);
    instance()->ColorOnTop = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeColorOnTop() {
    instance()->handle->RemoveBool("ColorOnTop");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docAutoSortWBList() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Sort workbench entries by their names in the combo box.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getAutoSortWBList() {
    return instance()->AutoSortWBList;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultAutoSortWBList() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setAutoSortWBList(const bool &v) {
    instance()->handle->SetBool("AutoSortWBList",v);
    instance()->AutoSortWBList = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeAutoSortWBList() {
    instance()->handle->RemoveBool("AutoSortWBList");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docMaxCameraAnimatePeriod() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum camera move animation duration in milliseconds.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getMaxCameraAnimatePeriod() {
    return instance()->MaxCameraAnimatePeriod;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultMaxCameraAnimatePeriod() {
    const static long def = 3000;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setMaxCameraAnimatePeriod(const long &v) {
    instance()->handle->SetInt("MaxCameraAnimatePeriod",v);
    instance()->MaxCameraAnimatePeriod = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeMaxCameraAnimatePeriod() {
    instance()->handle->RemoveInt("MaxCameraAnimatePeriod");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docTaskNoWheelFocus() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Do not accept wheel focus on input fields in task panels.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getTaskNoWheelFocus() {
    return instance()->TaskNoWheelFocus;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultTaskNoWheelFocus() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setTaskNoWheelFocus(const bool &v) {
    instance()->handle->SetBool("TaskNoWheelFocus",v);
    instance()->TaskNoWheelFocus = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeTaskNoWheelFocus() {
    instance()->handle->RemoveBool("TaskNoWheelFocus");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docGestureLongPressRotationCenter() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Set rotation center on press in gesture navigation mode.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getGestureLongPressRotationCenter() {
    return instance()->GestureLongPressRotationCenter;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultGestureLongPressRotationCenter() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setGestureLongPressRotationCenter(const bool &v) {
    instance()->handle->SetBool("GestureLongPressRotationCenter",v);
    instance()->GestureLongPressRotationCenter = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeGestureLongPressRotationCenter() {
    instance()->handle->RemoveBool("GestureLongPressRotationCenter");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docCheckWidgetPlacementOnRestore() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Check widget position and size on restore to make sure it is within the current screen.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getCheckWidgetPlacementOnRestore() {
    return instance()->CheckWidgetPlacementOnRestore;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultCheckWidgetPlacementOnRestore() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setCheckWidgetPlacementOnRestore(const bool &v) {
    instance()->handle->SetBool("CheckWidgetPlacementOnRestore",v);
    instance()->CheckWidgetPlacementOnRestore = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeCheckWidgetPlacementOnRestore() {
    instance()->handle->RemoveBool("CheckWidgetPlacementOnRestore");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docTextCursorWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Text cursor width in pixel.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getTextCursorWidth() {
    return instance()->TextCursorWidth;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultTextCursorWidth() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setTextCursorWidth(const long &v) {
    instance()->handle->SetInt("TextCursorWidth",v);
    instance()->TextCursorWidth = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeTextCursorWidth() {
    instance()->handle->RemoveInt("TextCursorWidth");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPreselectionToolTipCorner() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Preselection tool tip docking corner.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPreselectionToolTipCorner() {
    return instance()->PreselectionToolTipCorner;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPreselectionToolTipCorner() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPreselectionToolTipCorner(const long &v) {
    instance()->handle->SetInt("PreselectionToolTipCorner",v);
    instance()->PreselectionToolTipCorner = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePreselectionToolTipCorner() {
    instance()->handle->RemoveInt("PreselectionToolTipCorner");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPreselectionToolTipOffsetX() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Preselection tool tip x offset relative to its docking corner.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPreselectionToolTipOffsetX() {
    return instance()->PreselectionToolTipOffsetX;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPreselectionToolTipOffsetX() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPreselectionToolTipOffsetX(const long &v) {
    instance()->handle->SetInt("PreselectionToolTipOffsetX",v);
    instance()->PreselectionToolTipOffsetX = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePreselectionToolTipOffsetX() {
    instance()->handle->RemoveInt("PreselectionToolTipOffsetX");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPreselectionToolTipOffsetY() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Preselection tool tip y offset relative to its docking corner.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPreselectionToolTipOffsetY() {
    return instance()->PreselectionToolTipOffsetY;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPreselectionToolTipOffsetY() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPreselectionToolTipOffsetY(const long &v) {
    instance()->handle->SetInt("PreselectionToolTipOffsetY",v);
    instance()->PreselectionToolTipOffsetY = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePreselectionToolTipOffsetY() {
    instance()->handle->RemoveInt("PreselectionToolTipOffsetY");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docPreselectionToolTipFontSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Preselection tool tip font size. Set to 0 to use system default.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getPreselectionToolTipFontSize() {
    return instance()->PreselectionToolTipFontSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultPreselectionToolTipFontSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setPreselectionToolTipFontSize(const long &v) {
    instance()->handle->SetInt("PreselectionToolTipFontSize",v);
    instance()->PreselectionToolTipFontSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removePreselectionToolTipFontSize() {
    instance()->handle->RemoveInt("PreselectionToolTipFontSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSectionFill() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Fill cross section plane.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getSectionFill() {
    return instance()->SectionFill;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultSectionFill() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSectionFill(const bool &v) {
    instance()->handle->SetBool("SectionFill",v);
    instance()->SectionFill = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSectionFill() {
    instance()->handle->RemoveBool("SectionFill");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSectionFillInvert() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Invert cross section plane fill color.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getSectionFillInvert() {
    return instance()->SectionFillInvert;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultSectionFillInvert() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSectionFillInvert(const bool &v) {
    instance()->handle->SetBool("SectionFillInvert",v);
    instance()->SectionFillInvert = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSectionFillInvert() {
    instance()->handle->RemoveBool("SectionFillInvert");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSectionConcave() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Cross section in concave.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getSectionConcave() {
    return instance()->SectionConcave;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultSectionConcave() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSectionConcave(const bool &v) {
    instance()->handle->SetBool("SectionConcave",v);
    instance()->SectionConcave = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSectionConcave() {
    instance()->handle->RemoveBool("SectionConcave");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docNoSectionOnTop() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Ignore section clip planes when rendering on top.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getNoSectionOnTop() {
    return instance()->NoSectionOnTop;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultNoSectionOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setNoSectionOnTop(const bool &v) {
    instance()->handle->SetBool("NoSectionOnTop",v);
    instance()->NoSectionOnTop = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeNoSectionOnTop() {
    instance()->handle->RemoveBool("NoSectionOnTop");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSectionHatchTextureScale() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Section filling texture image scale.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getSectionHatchTextureScale() {
    return instance()->SectionHatchTextureScale;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultSectionHatchTextureScale() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSectionHatchTextureScale(const double &v) {
    instance()->handle->SetFloat("SectionHatchTextureScale",v);
    instance()->SectionHatchTextureScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSectionHatchTextureScale() {
    instance()->handle->RemoveFloat("SectionHatchTextureScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSectionHatchTexture() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Section filling texture image path.");
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & ViewParams::getSectionHatchTexture() {
    return instance()->SectionHatchTexture;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & ViewParams::defaultSectionHatchTexture() {
    const static std::string def = ":icons/section-hatch.png";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSectionHatchTexture(const std::string &v) {
    instance()->handle->SetASCII("SectionHatchTexture",v);
    instance()->SectionHatchTexture = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSectionHatchTexture() {
    instance()->handle->RemoveASCII("SectionHatchTexture");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSectionHatchTextureEnable() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Enable section fill texture.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getSectionHatchTextureEnable() {
    return instance()->SectionHatchTextureEnable;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultSectionHatchTextureEnable() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSectionHatchTextureEnable(const bool &v) {
    instance()->handle->SetBool("SectionHatchTextureEnable",v);
    instance()->SectionHatchTextureEnable = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSectionHatchTextureEnable() {
    instance()->handle->RemoveBool("SectionHatchTextureEnable");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSectionFillGroup() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Render cross section filling of objects with similar materials together.\n"
"Intersecting objects will act as boolean cut operation");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getSectionFillGroup() {
    return instance()->SectionFillGroup;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultSectionFillGroup() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSectionFillGroup(const bool &v) {
    instance()->handle->SetBool("SectionFillGroup",v);
    instance()->SectionFillGroup = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSectionFillGroup() {
    instance()->handle->RemoveBool("SectionFillGroup");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docShowClipPlane() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Show clip plane");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getShowClipPlane() {
    return instance()->ShowClipPlane;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultShowClipPlane() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setShowClipPlane(const bool &v) {
    instance()->handle->SetBool("ShowClipPlane",v);
    instance()->ShowClipPlane = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeShowClipPlane() {
    instance()->handle->RemoveBool("ShowClipPlane");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docClipPlaneSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Clip plane visual size");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getClipPlaneSize() {
    return instance()->ClipPlaneSize;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultClipPlaneSize() {
    const static double def = 40.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setClipPlaneSize(const double &v) {
    instance()->handle->SetFloat("ClipPlaneSize",v);
    instance()->ClipPlaneSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeClipPlaneSize() {
    instance()->handle->RemoveFloat("ClipPlaneSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docClipPlaneColor() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Clip plane color");
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & ViewParams::getClipPlaneColor() {
    return instance()->ClipPlaneColor;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & ViewParams::defaultClipPlaneColor() {
    const static std::string def = "cyan";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setClipPlaneColor(const std::string &v) {
    instance()->handle->SetASCII("ClipPlaneColor",v);
    instance()->ClipPlaneColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeClipPlaneColor() {
    instance()->handle->RemoveASCII("ClipPlaneColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docClipPlaneLineWidth() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Clip plane line width");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getClipPlaneLineWidth() {
    return instance()->ClipPlaneLineWidth;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultClipPlaneLineWidth() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setClipPlaneLineWidth(const double &v) {
    instance()->handle->SetFloat("ClipPlaneLineWidth",v);
    instance()->ClipPlaneLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeClipPlaneLineWidth() {
    instance()->handle->RemoveFloat("ClipPlaneLineWidth");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docTransformOnTop() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getTransformOnTop() {
    return instance()->TransformOnTop;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultTransformOnTop() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setTransformOnTop(const bool &v) {
    instance()->handle->SetBool("TransformOnTop",v);
    instance()->TransformOnTop = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeTransformOnTop() {
    instance()->handle->RemoveBool("TransformOnTop");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionColorDifference() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Color difference threshold for auto making distinct\n"
"selection highlight color");
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getSelectionColorDifference() {
    return instance()->SelectionColorDifference;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultSelectionColorDifference() {
    const static double def = 25.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionColorDifference(const double &v) {
    instance()->handle->SetFloat("SelectionColorDifference",v);
    instance()->SelectionColorDifference = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionColorDifference() {
    instance()->handle->RemoveFloat("SelectionColorDifference");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRenderCacheMergeCount() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Merge draw caches of multiple objects to reduce number of draw\n"
"calls and improve render performance. Set zero to disable. Only\n"
"effective when using experimental render cache.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getRenderCacheMergeCount() {
    return instance()->RenderCacheMergeCount;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultRenderCacheMergeCount() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRenderCacheMergeCount(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeCount",v);
    instance()->RenderCacheMergeCount = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRenderCacheMergeCount() {
    instance()->handle->RemoveInt("RenderCacheMergeCount");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRenderCacheMergeCountMin() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Internal use to limit the render cache merge count");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getRenderCacheMergeCountMin() {
    return instance()->RenderCacheMergeCountMin;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultRenderCacheMergeCountMin() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRenderCacheMergeCountMin(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeCountMin",v);
    instance()->RenderCacheMergeCountMin = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRenderCacheMergeCountMin() {
    instance()->handle->RemoveInt("RenderCacheMergeCountMin");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRenderCacheMergeCountMax() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum draw crash merges on any hierarchy. Zero means no limit.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getRenderCacheMergeCountMax() {
    return instance()->RenderCacheMergeCountMax;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultRenderCacheMergeCountMax() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRenderCacheMergeCountMax(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeCountMax",v);
    instance()->RenderCacheMergeCountMax = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRenderCacheMergeCountMax() {
    instance()->handle->RemoveInt("RenderCacheMergeCountMax");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRenderCacheMergeDepthMax() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum hierarchy depth that the cache merge can happen. Less than 0 means no limit.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getRenderCacheMergeDepthMax() {
    return instance()->RenderCacheMergeDepthMax;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultRenderCacheMergeDepthMax() {
    const static long def = -1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRenderCacheMergeDepthMax(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeDepthMax",v);
    instance()->RenderCacheMergeDepthMax = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRenderCacheMergeDepthMax() {
    instance()->handle->RemoveInt("RenderCacheMergeDepthMax");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRenderCacheMergeDepthMin() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Minimum hierarchy depth that the cache merge can happen.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getRenderCacheMergeDepthMin() {
    return instance()->RenderCacheMergeDepthMin;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultRenderCacheMergeDepthMin() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRenderCacheMergeDepthMin(const long &v) {
    instance()->handle->SetInt("RenderCacheMergeDepthMin",v);
    instance()->RenderCacheMergeDepthMin = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRenderCacheMergeDepthMin() {
    instance()->handle->RemoveInt("RenderCacheMergeDepthMin");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRenderHighlightPolygonOffsetFactor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getRenderHighlightPolygonOffsetFactor() {
    return instance()->RenderHighlightPolygonOffsetFactor;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultRenderHighlightPolygonOffsetFactor() {
    const static double def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRenderHighlightPolygonOffsetFactor(const double &v) {
    instance()->handle->SetFloat("RenderHighlightPolygonOffsetFactor",v);
    instance()->RenderHighlightPolygonOffsetFactor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRenderHighlightPolygonOffsetFactor() {
    instance()->handle->RemoveFloat("RenderHighlightPolygonOffsetFactor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docRenderHighlightPolygonOffsetUnits() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getRenderHighlightPolygonOffsetUnits() {
    return instance()->RenderHighlightPolygonOffsetUnits;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultRenderHighlightPolygonOffsetUnits() {
    const static double def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setRenderHighlightPolygonOffsetUnits(const double &v) {
    instance()->handle->SetFloat("RenderHighlightPolygonOffsetUnits",v);
    instance()->RenderHighlightPolygonOffsetUnits = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeRenderHighlightPolygonOffsetUnits() {
    instance()->handle->RemoveFloat("RenderHighlightPolygonOffsetUnits");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docForceSolidSingleSideLighting() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Force single side lighting on solid. This can help visualizing invalid\n"
"solid shapes with flipped normals.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getForceSolidSingleSideLighting() {
    return instance()->ForceSolidSingleSideLighting;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultForceSolidSingleSideLighting() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setForceSolidSingleSideLighting(const bool &v) {
    instance()->handle->SetBool("ForceSolidSingleSideLighting",v);
    instance()->ForceSolidSingleSideLighting = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeForceSolidSingleSideLighting() {
    instance()->handle->RemoveBool("ForceSolidSingleSideLighting");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docDefaultFontSize() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getDefaultFontSize() {
    return instance()->DefaultFontSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultDefaultFontSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setDefaultFontSize(const long &v) {
    instance()->handle->SetInt("DefaultFontSize",v);
    instance()->DefaultFontSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeDefaultFontSize() {
    instance()->handle->RemoveInt("DefaultFontSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docEnableTaskPanelKeyTranslate() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getEnableTaskPanelKeyTranslate() {
    return instance()->EnableTaskPanelKeyTranslate;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultEnableTaskPanelKeyTranslate() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setEnableTaskPanelKeyTranslate(const bool &v) {
    instance()->handle->SetBool("EnableTaskPanelKeyTranslate",v);
    instance()->EnableTaskPanelKeyTranslate = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeEnableTaskPanelKeyTranslate() {
    instance()->handle->RemoveBool("EnableTaskPanelKeyTranslate");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docEnableMenuBarCheckBox() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getEnableMenuBarCheckBox() {
    return instance()->EnableMenuBarCheckBox;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultEnableMenuBarCheckBox() {
    const static bool def = FC_ENABLE_MENUBAR_CHECKBOX;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setEnableMenuBarCheckBox(const bool &v) {
    instance()->handle->SetBool("EnableMenuBarCheckBox",v);
    instance()->EnableMenuBarCheckBox = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeEnableMenuBarCheckBox() {
    instance()->handle->RemoveBool("EnableMenuBarCheckBox");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docEnableBacklight() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getEnableBacklight() {
    return instance()->EnableBacklight;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultEnableBacklight() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setEnableBacklight(const bool &v) {
    instance()->handle->SetBool("EnableBacklight",v);
    instance()->EnableBacklight = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeEnableBacklight() {
    instance()->handle->RemoveBool("EnableBacklight");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docBacklightColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getBacklightColor() {
    return instance()->BacklightColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultBacklightColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setBacklightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("BacklightColor",v);
    instance()->BacklightColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeBacklightColor() {
    instance()->handle->RemoveUnsigned("BacklightColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docBacklightIntensity() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & ViewParams::getBacklightIntensity() {
    return instance()->BacklightIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & ViewParams::defaultBacklightIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setBacklightIntensity(const double &v) {
    instance()->handle->SetFloat("BacklightIntensity",v);
    instance()->BacklightIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeBacklightIntensity() {
    instance()->handle->RemoveFloat("BacklightIntensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docOverrideSelectability() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Override object selectability to enable selection");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getOverrideSelectability() {
    return instance()->OverrideSelectability;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultOverrideSelectability() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setOverrideSelectability(const bool &v) {
    instance()->handle->SetBool("OverrideSelectability",v);
    instance()->OverrideSelectability = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeOverrideSelectability() {
    instance()->handle->RemoveBool("OverrideSelectability");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docSelectionStackSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Maximum selection history record size");
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getSelectionStackSize() {
    return instance()->SelectionStackSize;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultSelectionStackSize() {
    const static unsigned long def = 30;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setSelectionStackSize(const unsigned long &v) {
    instance()->handle->SetUnsigned("SelectionStackSize",v);
    instance()->SelectionStackSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeSelectionStackSize() {
    instance()->handle->RemoveUnsigned("SelectionStackSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docDefaultDrawStyle() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Default draw style of a new document");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getDefaultDrawStyle() {
    return instance()->DefaultDrawStyle;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultDefaultDrawStyle() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setDefaultDrawStyle(const long &v) {
    instance()->handle->SetInt("DefaultDrawStyle",v);
    instance()->DefaultDrawStyle = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeDefaultDrawStyle() {
    instance()->handle->RemoveInt("DefaultDrawStyle");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docToolTipIconSize() {
    return QT_TRANSLATE_NOOP("ViewParams",
"Specifies the size of static icon image in tooltip. GIF animation\n"
"will be shown in its original size. You can disable all images in\n"
"the tooltip by setting this option to zero.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & ViewParams::getToolTipIconSize() {
    return instance()->ToolTipIconSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & ViewParams::defaultToolTipIconSize() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setToolTipIconSize(const long &v) {
    instance()->handle->SetInt("ToolTipIconSize",v);
    instance()->ToolTipIconSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeToolTipIconSize() {
    instance()->handle->RemoveInt("ToolTipIconSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docToolTipDisable() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & ViewParams::getToolTipDisable() {
    return instance()->ToolTipDisable;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & ViewParams::defaultToolTipDisable() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setToolTipDisable(const bool &v) {
    instance()->handle->SetBool("ToolTipDisable",v);
    instance()->ToolTipDisable = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeToolTipDisable() {
    instance()->handle->RemoveBool("ToolTipDisable");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docAxisXColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getAxisXColor() {
    return instance()->AxisXColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultAxisXColor() {
    const static unsigned long def = 0xCC333300;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setAxisXColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AxisXColor",v);
    instance()->AxisXColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeAxisXColor() {
    instance()->handle->RemoveUnsigned("AxisXColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docAxisYColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getAxisYColor() {
    return instance()->AxisYColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultAxisYColor() {
    const static unsigned long def = 0x33CC3300;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setAxisYColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AxisYColor",v);
    instance()->AxisYColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeAxisYColor() {
    instance()->handle->RemoveUnsigned("AxisYColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *ViewParams::docAxisZColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & ViewParams::getAxisZColor() {
    return instance()->AxisZColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & ViewParams::defaultAxisZColor() {
    const static unsigned long def = 0x3333CC00;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void ViewParams::setAxisZColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("AxisZColor",v);
    instance()->AxisZColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void ViewParams::removeAxisZColor() {
    instance()->handle->RemoveUnsigned("AxisZColor");
}

// Auto generated code (Gui/ViewParams.py:520)
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

// Auto generated code (Gui/ViewParams.py:528)
static const char *DrawStyleNames[] = {
    QT_TRANSLATE_NOOP("DrawStyle", "As Is"),
    QT_TRANSLATE_NOOP("DrawStyle", "Points"),
    QT_TRANSLATE_NOOP("DrawStyle", "Wireframe"),
    QT_TRANSLATE_NOOP("DrawStyle", "Hidden Line"),
    QT_TRANSLATE_NOOP("DrawStyle", "No Shading"),
    QT_TRANSLATE_NOOP("DrawStyle", "Shaded"),
    QT_TRANSLATE_NOOP("DrawStyle", "Flat Lines"),
    QT_TRANSLATE_NOOP("DrawStyle", "Tessellation"),
    QT_TRANSLATE_NOOP("DrawStyle", "Shadow"),
    nullptr,
};

// Auto generated code (Gui/ViewParams.py:538)
static const char *DrawStyleDocs[] = {
    QT_TRANSLATE_NOOP("DrawStyle", "Draw style, normal display mode"),
    QT_TRANSLATE_NOOP("DrawStyle", "Draw style, show points only"),
    QT_TRANSLATE_NOOP("DrawStyle", "Draw style, show wire frame only"),
    QT_TRANSLATE_NOOP("DrawStyle", "Draw style, show hidden line by display object as transparent"),
    QT_TRANSLATE_NOOP("DrawStyle", "Draw style, shading forced off"),
    QT_TRANSLATE_NOOP("DrawStyle", "Draw style, shading force on"),
    QT_TRANSLATE_NOOP("DrawStyle", "Draw style, show both wire frame and face with shading"),
    QT_TRANSLATE_NOOP("DrawStyle", "Draw style, show tessellation wire frame"),
    QT_TRANSLATE_NOOP("DrawStyle", "Draw style, drop shadows for the scene.\nClick this button while in shadow mode to toggle light manipulator"),
};

namespace Gui {
// Auto generated code (Gui/ViewParams.py:548)
const char **drawStyleNames()
{
    return DrawStyleNames;
}

// Auto generated code (Gui/ViewParams.py:555)
const char *drawStyleNameFromIndex(int i)
{
    if (i < 0 || i>= 9)
        return nullptr;
    return DrawStyleNames[i];
}

// Auto generated code (Gui/ViewParams.py:564)
int drawStyleIndexFromName(const char *name)
{
    if (!name)
        return -1;
    for (int i=0; i< 9; ++i) {
        if (strcmp(name, DrawStyleNames[i]) == 0)
            return i;
    }
    return -1;
}

// Auto generated code (Gui/ViewParams.py:577)
const char *drawStyleDocumentation(int i)
{
    if (i < 0 || i>= 9)
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

bool ViewParams::isUsingRenderer()
{
    return getRenderCache() == 3;
}

void ViewParams::useRenderer(bool enable)
{
    if (isUsingRenderer()) {
        if (!enable)
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
