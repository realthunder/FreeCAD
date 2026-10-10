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
import TechDrawParams
TechDrawParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "TechDrawParams.h"
using namespace TechDraw;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class TechDrawParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(TechDrawParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    bool AllowPageOverride;
    bool AutoDist;
    double DefaultScale;
    long DefaultScaleType;
    double DefaultViewScale;
    bool DrawFaceEdges;
    double EdgeFuzz;
    bool FixColorAlphaOnLoad;
    double FocusDistance;
    bool GlobalUpdateDrawings;
    bool HandleFaces;
    long KbPan;
    long KbScroll;
    bool KeepPagesUpToDate;
    double MarkFuzz;
    bool NewFaceFinder;
    bool PageRendererVg;
    bool PageRendererVgComposite;
    bool PageRendererVgVerify;
    bool PageRendererVgRoundLineWidth;
    long ProjectionAngle;
    bool restoreCosmetic;
    long ScrubCount;
    bool SectionFuseFirst;
    bool SectionUsePreviousCut;
    bool ShadedUnderlayBackend;
    long ShadedUnderlayMaxFaces;
    bool ShowDetailHighlight;
    bool ShowDetailMatting;
    bool ShowSectionEdges;
    bool StoreProjectedGeometry;
    double TemplateDotSize;
    double VertexScale;
    double gridSpacing;
    bool multiSelection;
    bool showGrid;
    long BalloonArrow;
    long BalloonShape;
    double CenterMarkScale;
    double CosmoCLExtend;
    long CutSurfaceDisplay;
    long MattingStyle;
    long MaxSVGTile;
    bool PrintCenterMarks;
    bool PyramidOrtho;
    bool SectionLineMarks;
    bool ShowCenterMarks;
    double SvgOverlapFactor;
    double SymbolFactor;
    long AltDecimals;
    long ArrowStyle;
    bool AutoCorrectRefs;
    double BalloonKink;
    double GapASME;
    double GapISO;
    bool ShowUnits;
    double SymbolSize;
    double TileTextAdjust;
    double TolSizeAdjust;
    bool UseGlobalDecimals;
    std::string formatSpec;
    bool HardHid;
    bool IsoHid;
    bool IsoViz;
    bool SeamHid;
    bool SeamViz;
    bool SmoothHid;
    bool SmoothViz;
    double GeomWeight;
    long MaxSeg;
    std::string NamePattern;
    bool ClearFace;
    bool LightOnDark;
    bool Monochrome;
    std::string LabelFont;
    double LabelSize;
    bool AutoHorizontal;
    double Resolution;
    double TrackerWeight;
    bool allowCrazyEdge;
    bool debugDetail;
    bool debugSection;
    unsigned long PreSelectColor;
    unsigned long SelectColor;
    unsigned long Background;
    unsigned long CutSurfaceColor;
    unsigned long FaceColor;
    unsigned long GeomHatch;
    unsigned long Hatch;
    unsigned long HiddenColor;
    unsigned long LightTextColor;
    unsigned long NormalColor;
    unsigned long PageColor;
    unsigned long TemplateUnderlineColor;
    unsigned long gridColor;
    unsigned long BreaklineColor;
    unsigned long CenterColor;
    unsigned long HighlightColor;
    unsigned long SectionColor;
    unsigned long VertexColor;
    unsigned long DimensionColor;
    unsigned long LeaderLineColor;
    unsigned long TrackerColor;
    long LineGroup;
    long LineStyleCenter;
    long LineStyleHidden;
    long LineStyleBreak;
    long BreakType;
    long LineStyleSection;
    long HighlightStyle;
    long CenterLine;
    long CenterLineStyle;
    long HiddenLine;
    long EdgeCapStyle;
    long LineStandard;
    long SectionLineStandard;
    long IsoCount;
    bool CoarseView;
    double LineSpacingFactorISO;
    long SectionUpdateDelay;
    double ArrowSize;
    double FontSize;
    std::string DiameterSymbol;
    long StandardAndStyle;
    std::string TemplateFile;
    std::string TemplateDir;
    std::string LineGroupFile;
    std::string FileHatch;
    std::string WeldingDir;
    std::string LineDefLocation;
    std::string LineElementLocation;
    std::string FilePattern;
    unsigned long TileColor;
    bool SectionLiveUpdate;

    // Auto generated code (Tools/params_utils.py:254)
    TechDrawParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/TechDraw");
        handle->Attach(this);

        subHandles.resize(13);
        subHandles[0] = handle->GetGroup("General");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("Decorations");
        subHandles[1]->Attach(this);
        subHandles[2] = handle->GetGroup("Dimensions");
        subHandles[2]->Attach(this);
        subHandles[3] = handle->GetGroup("HLR");
        subHandles[3]->Attach(this);
        subHandles[4] = handle->GetGroup("PAT");
        subHandles[4]->Attach(this);
        subHandles[5] = handle->GetGroup("Colors");
        subHandles[5]->Attach(this);
        subHandles[6] = handle->GetGroup("Labels");
        subHandles[6]->Attach(this);
        subHandles[7] = handle->GetGroup("LeaderLine");
        subHandles[7]->Attach(this);
        subHandles[8] = handle->GetGroup("Rez");
        subHandles[8]->Attach(this);
        subHandles[9] = handle->GetGroup("Tracker");
        subHandles[9]->Attach(this);
        subHandles[10] = handle->GetGroup("debug");
        subHandles[10]->Attach(this);
        subHandles[11] = handle->GetGroup("Standards");
        subHandles[11]->Attach(this);
        subHandles[12] = handle->GetGroup("Files");
        subHandles[12]->Attach(this);
        AllowPageOverride = this->subHandles[0]->GetBool("AllowPageOverride", true);
        funcs["AllowPageOverride"] = &TechDrawParamsP::updateAllowPageOverride;
        AutoDist = this->subHandles[0]->GetBool("AutoDist", true);
        funcs["AutoDist"] = &TechDrawParamsP::updateAutoDist;
        DefaultScale = this->subHandles[0]->GetFloat("DefaultScale", 1.0);
        funcs["DefaultScale"] = &TechDrawParamsP::updateDefaultScale;
        DefaultScaleType = this->subHandles[0]->GetInt("DefaultScaleType", 0);
        funcs["DefaultScaleType"] = &TechDrawParamsP::updateDefaultScaleType;
        DefaultViewScale = this->subHandles[0]->GetFloat("DefaultViewScale", 1.0);
        funcs["DefaultViewScale"] = &TechDrawParamsP::updateDefaultViewScale;
        DrawFaceEdges = this->subHandles[0]->GetBool("DrawFaceEdges", false);
        funcs["DrawFaceEdges"] = &TechDrawParamsP::updateDrawFaceEdges;
        EdgeFuzz = this->subHandles[0]->GetFloat("EdgeFuzz", 10.0);
        funcs["EdgeFuzz"] = &TechDrawParamsP::updateEdgeFuzz;
        FixColorAlphaOnLoad = this->subHandles[0]->GetBool("FixColorAlphaOnLoad", true);
        funcs["FixColorAlphaOnLoad"] = &TechDrawParamsP::updateFixColorAlphaOnLoad;
        FocusDistance = this->subHandles[0]->GetFloat("FocusDistance", 100.0);
        funcs["FocusDistance"] = &TechDrawParamsP::updateFocusDistance;
        GlobalUpdateDrawings = this->subHandles[0]->GetBool("GlobalUpdateDrawings", true);
        funcs["GlobalUpdateDrawings"] = &TechDrawParamsP::updateGlobalUpdateDrawings;
        HandleFaces = this->subHandles[0]->GetBool("HandleFaces", true);
        funcs["HandleFaces"] = &TechDrawParamsP::updateHandleFaces;
        KbPan = this->subHandles[0]->GetInt("KbPan", 1);
        funcs["KbPan"] = &TechDrawParamsP::updateKbPan;
        KbScroll = this->subHandles[0]->GetInt("KbScroll", 1);
        funcs["KbScroll"] = &TechDrawParamsP::updateKbScroll;
        KeepPagesUpToDate = this->subHandles[0]->GetBool("KeepPagesUpToDate", true);
        funcs["KeepPagesUpToDate"] = &TechDrawParamsP::updateKeepPagesUpToDate;
        MarkFuzz = this->subHandles[0]->GetFloat("MarkFuzz", 5.0);
        funcs["MarkFuzz"] = &TechDrawParamsP::updateMarkFuzz;
        NewFaceFinder = this->subHandles[0]->GetBool("NewFaceFinder", false);
        funcs["NewFaceFinder"] = &TechDrawParamsP::updateNewFaceFinder;
        PageRendererVg = this->subHandles[0]->GetBool("PageRendererVg", false);
        funcs["PageRendererVg"] = &TechDrawParamsP::updatePageRendererVg;
        PageRendererVgComposite = this->subHandles[0]->GetBool("PageRendererVgComposite", true);
        funcs["PageRendererVgComposite"] = &TechDrawParamsP::updatePageRendererVgComposite;
        PageRendererVgVerify = this->subHandles[0]->GetBool("PageRendererVgVerify", false);
        funcs["PageRendererVgVerify"] = &TechDrawParamsP::updatePageRendererVgVerify;
        PageRendererVgRoundLineWidth = this->subHandles[0]->GetBool("PageRendererVgRoundLineWidth", false);
        funcs["PageRendererVgRoundLineWidth"] = &TechDrawParamsP::updatePageRendererVgRoundLineWidth;
        ProjectionAngle = this->subHandles[0]->GetInt("ProjectionAngle", 0);
        funcs["ProjectionAngle"] = &TechDrawParamsP::updateProjectionAngle;
        restoreCosmetic = this->subHandles[0]->GetBool("restoreCosmetic", true);
        funcs["restoreCosmetic"] = &TechDrawParamsP::updaterestoreCosmetic;
        ScrubCount = this->subHandles[0]->GetInt("ScrubCount", 0);
        funcs["ScrubCount"] = &TechDrawParamsP::updateScrubCount;
        SectionFuseFirst = this->subHandles[0]->GetBool("SectionFuseFirst", false);
        funcs["SectionFuseFirst"] = &TechDrawParamsP::updateSectionFuseFirst;
        SectionUsePreviousCut = this->subHandles[0]->GetBool("SectionUsePreviousCut", false);
        funcs["SectionUsePreviousCut"] = &TechDrawParamsP::updateSectionUsePreviousCut;
        ShadedUnderlayBackend = this->subHandles[0]->GetBool("ShadedUnderlayBackend", true);
        funcs["ShadedUnderlayBackend"] = &TechDrawParamsP::updateShadedUnderlayBackend;
        ShadedUnderlayMaxFaces = this->subHandles[0]->GetInt("ShadedUnderlayMaxFaces", 5000);
        funcs["ShadedUnderlayMaxFaces"] = &TechDrawParamsP::updateShadedUnderlayMaxFaces;
        ShowDetailHighlight = this->subHandles[0]->GetBool("ShowDetailHighlight", true);
        funcs["ShowDetailHighlight"] = &TechDrawParamsP::updateShowDetailHighlight;
        ShowDetailMatting = this->subHandles[0]->GetBool("ShowDetailMatting", true);
        funcs["ShowDetailMatting"] = &TechDrawParamsP::updateShowDetailMatting;
        ShowSectionEdges = this->subHandles[0]->GetBool("ShowSectionEdges", true);
        funcs["ShowSectionEdges"] = &TechDrawParamsP::updateShowSectionEdges;
        StoreProjectedGeometry = this->subHandles[0]->GetBool("StoreProjectedGeometry", true);
        funcs["StoreProjectedGeometry"] = &TechDrawParamsP::updateStoreProjectedGeometry;
        TemplateDotSize = this->subHandles[0]->GetFloat("TemplateDotSize", 5.0);
        funcs["TemplateDotSize"] = &TechDrawParamsP::updateTemplateDotSize;
        VertexScale = this->subHandles[0]->GetFloat("VertexScale", 3.0);
        funcs["VertexScale"] = &TechDrawParamsP::updateVertexScale;
        gridSpacing = this->subHandles[0]->GetFloat("gridSpacing", 10.0);
        funcs["gridSpacing"] = &TechDrawParamsP::updategridSpacing;
        multiSelection = this->subHandles[0]->GetBool("multiSelection", false);
        funcs["multiSelection"] = &TechDrawParamsP::updatemultiSelection;
        showGrid = this->subHandles[0]->GetBool("showGrid", false);
        funcs["showGrid"] = &TechDrawParamsP::updateshowGrid;
        BalloonArrow = this->subHandles[1]->GetInt("BalloonArrow", 0);
        funcs["BalloonArrow"] = &TechDrawParamsP::updateBalloonArrow;
        BalloonShape = this->subHandles[1]->GetInt("BalloonShape", 0);
        funcs["BalloonShape"] = &TechDrawParamsP::updateBalloonShape;
        CenterMarkScale = this->subHandles[1]->GetFloat("CenterMarkScale", 0.5);
        funcs["CenterMarkScale"] = &TechDrawParamsP::updateCenterMarkScale;
        CosmoCLExtend = this->subHandles[1]->GetFloat("CosmoCLExtend", 3.0);
        funcs["CosmoCLExtend"] = &TechDrawParamsP::updateCosmoCLExtend;
        CutSurfaceDisplay = this->subHandles[1]->GetInt("CutSurfaceDisplay", 2);
        funcs["CutSurfaceDisplay"] = &TechDrawParamsP::updateCutSurfaceDisplay;
        MattingStyle = this->subHandles[1]->GetInt("MattingStyle", 0);
        funcs["MattingStyle"] = &TechDrawParamsP::updateMattingStyle;
        MaxSVGTile = this->subHandles[1]->GetInt("MaxSVGTile", 10000);
        funcs["MaxSVGTile"] = &TechDrawParamsP::updateMaxSVGTile;
        PrintCenterMarks = this->subHandles[1]->GetBool("PrintCenterMarks", false);
        funcs["PrintCenterMarks"] = &TechDrawParamsP::updatePrintCenterMarks;
        PyramidOrtho = this->subHandles[1]->GetBool("PyramidOrtho", true);
        funcs["PyramidOrtho"] = &TechDrawParamsP::updatePyramidOrtho;
        SectionLineMarks = this->subHandles[1]->GetBool("SectionLineMarks", true);
        funcs["SectionLineMarks"] = &TechDrawParamsP::updateSectionLineMarks;
        ShowCenterMarks = this->subHandles[1]->GetBool("ShowCenterMarks", false);
        funcs["ShowCenterMarks"] = &TechDrawParamsP::updateShowCenterMarks;
        SvgOverlapFactor = this->subHandles[1]->GetFloat("SvgOverlapFactor", 1.25);
        funcs["SvgOverlapFactor"] = &TechDrawParamsP::updateSvgOverlapFactor;
        SymbolFactor = this->subHandles[1]->GetFloat("SymbolFactor", 1.25);
        funcs["SymbolFactor"] = &TechDrawParamsP::updateSymbolFactor;
        AltDecimals = this->subHandles[2]->GetInt("AltDecimals", 2);
        funcs["AltDecimals"] = &TechDrawParamsP::updateAltDecimals;
        ArrowStyle = this->subHandles[2]->GetInt("ArrowStyle", 0);
        funcs["ArrowStyle"] = &TechDrawParamsP::updateArrowStyle;
        AutoCorrectRefs = this->subHandles[2]->GetBool("AutoCorrectRefs", true);
        funcs["AutoCorrectRefs"] = &TechDrawParamsP::updateAutoCorrectRefs;
        BalloonKink = this->subHandles[2]->GetFloat("BalloonKink", 5.0);
        funcs["BalloonKink"] = &TechDrawParamsP::updateBalloonKink;
        GapASME = this->subHandles[2]->GetFloat("GapASME", 0.0);
        funcs["GapASME"] = &TechDrawParamsP::updateGapASME;
        GapISO = this->subHandles[2]->GetFloat("GapISO", 0.0);
        funcs["GapISO"] = &TechDrawParamsP::updateGapISO;
        ShowUnits = this->subHandles[2]->GetBool("ShowUnits", false);
        funcs["ShowUnits"] = &TechDrawParamsP::updateShowUnits;
        SymbolSize = this->subHandles[2]->GetFloat("SymbolSize", 64.0);
        funcs["SymbolSize"] = &TechDrawParamsP::updateSymbolSize;
        TileTextAdjust = this->subHandles[2]->GetFloat("TileTextAdjust", 0.75);
        funcs["TileTextAdjust"] = &TechDrawParamsP::updateTileTextAdjust;
        TolSizeAdjust = this->subHandles[2]->GetFloat("TolSizeAdjust", 0.5);
        funcs["TolSizeAdjust"] = &TechDrawParamsP::updateTolSizeAdjust;
        UseGlobalDecimals = this->subHandles[2]->GetBool("UseGlobalDecimals", true);
        funcs["UseGlobalDecimals"] = &TechDrawParamsP::updateUseGlobalDecimals;
        formatSpec = this->subHandles[2]->GetASCII("formatSpec", "%.2w");
        funcs["formatSpec"] = &TechDrawParamsP::updateformatSpec;
        HardHid = this->subHandles[3]->GetBool("HardHid", false);
        funcs["HardHid"] = &TechDrawParamsP::updateHardHid;
        IsoHid = this->subHandles[3]->GetBool("IsoHid", false);
        funcs["IsoHid"] = &TechDrawParamsP::updateIsoHid;
        IsoViz = this->subHandles[3]->GetBool("IsoViz", false);
        funcs["IsoViz"] = &TechDrawParamsP::updateIsoViz;
        SeamHid = this->subHandles[3]->GetBool("SeamHid", false);
        funcs["SeamHid"] = &TechDrawParamsP::updateSeamHid;
        SeamViz = this->subHandles[3]->GetBool("SeamViz", false);
        funcs["SeamViz"] = &TechDrawParamsP::updateSeamViz;
        SmoothHid = this->subHandles[3]->GetBool("SmoothHid", false);
        funcs["SmoothHid"] = &TechDrawParamsP::updateSmoothHid;
        SmoothViz = this->subHandles[3]->GetBool("SmoothViz", true);
        funcs["SmoothViz"] = &TechDrawParamsP::updateSmoothViz;
        GeomWeight = this->subHandles[4]->GetFloat("GeomWeight", 0.1);
        funcs["GeomWeight"] = &TechDrawParamsP::updateGeomWeight;
        MaxSeg = this->subHandles[4]->GetInt("MaxSeg", 10000);
        funcs["MaxSeg"] = &TechDrawParamsP::updateMaxSeg;
        NamePattern = this->subHandles[4]->GetASCII("NamePattern", "Diamond");
        funcs["NamePattern"] = &TechDrawParamsP::updateNamePattern;
        ClearFace = this->subHandles[5]->GetBool("ClearFace", false);
        funcs["ClearFace"] = &TechDrawParamsP::updateClearFace;
        LightOnDark = this->subHandles[5]->GetBool("LightOnDark", false);
        funcs["LightOnDark"] = &TechDrawParamsP::updateLightOnDark;
        Monochrome = this->subHandles[5]->GetBool("Monochrome", false);
        funcs["Monochrome"] = &TechDrawParamsP::updateMonochrome;
        LabelFont = this->subHandles[6]->GetASCII("LabelFont", "osifont");
        funcs["LabelFont"] = &TechDrawParamsP::updateLabelFont;
        LabelSize = this->subHandles[6]->GetFloat("LabelSize", 5.0);
        funcs["LabelSize"] = &TechDrawParamsP::updateLabelSize;
        AutoHorizontal = this->subHandles[7]->GetBool("AutoHorizontal", true);
        funcs["AutoHorizontal"] = &TechDrawParamsP::updateAutoHorizontal;
        Resolution = this->subHandles[8]->GetFloat("Resolution", 10.0);
        funcs["Resolution"] = &TechDrawParamsP::updateResolution;
        TrackerWeight = this->subHandles[9]->GetFloat("TrackerWeight", 4.0);
        funcs["TrackerWeight"] = &TechDrawParamsP::updateTrackerWeight;
        allowCrazyEdge = this->subHandles[10]->GetBool("allowCrazyEdge", false);
        funcs["allowCrazyEdge"] = &TechDrawParamsP::updateallowCrazyEdge;
        debugDetail = this->subHandles[10]->GetBool("debugDetail", false);
        funcs["debugDetail"] = &TechDrawParamsP::updatedebugDetail;
        debugSection = this->subHandles[10]->GetBool("debugSection", false);
        funcs["debugSection"] = &TechDrawParamsP::updatedebugSection;
        PreSelectColor = this->subHandles[5]->GetUnsigned("PreSelectColor", 0x00000000);
        funcs["PreSelectColor"] = &TechDrawParamsP::updatePreSelectColor;
        SelectColor = this->subHandles[5]->GetUnsigned("SelectColor", 0x00000000);
        funcs["SelectColor"] = &TechDrawParamsP::updateSelectColor;
        Background = this->subHandles[5]->GetUnsigned("Background", 0x707070FF);
        funcs["Background"] = &TechDrawParamsP::updateBackground;
        CutSurfaceColor = this->subHandles[5]->GetUnsigned("CutSurfaceColor", 0xD3D3D3FF);
        funcs["CutSurfaceColor"] = &TechDrawParamsP::updateCutSurfaceColor;
        FaceColor = this->subHandles[5]->GetUnsigned("FaceColor", 0xFFFFFFFF);
        funcs["FaceColor"] = &TechDrawParamsP::updateFaceColor;
        GeomHatch = this->subHandles[5]->GetUnsigned("GeomHatch", 0x00FF00FF);
        funcs["GeomHatch"] = &TechDrawParamsP::updateGeomHatch;
        Hatch = this->subHandles[5]->GetUnsigned("Hatch", 0x00FF00FF);
        funcs["Hatch"] = &TechDrawParamsP::updateHatch;
        HiddenColor = this->subHandles[5]->GetUnsigned("HiddenColor", 0x000000FF);
        funcs["HiddenColor"] = &TechDrawParamsP::updateHiddenColor;
        LightTextColor = this->subHandles[5]->GetUnsigned("LightTextColor", 0xFFFFFFFF);
        funcs["LightTextColor"] = &TechDrawParamsP::updateLightTextColor;
        NormalColor = this->subHandles[5]->GetUnsigned("NormalColor", 0x000000FF);
        funcs["NormalColor"] = &TechDrawParamsP::updateNormalColor;
        PageColor = this->subHandles[5]->GetUnsigned("PageColor", 0xFFFFFFFF);
        funcs["PageColor"] = &TechDrawParamsP::updatePageColor;
        TemplateUnderlineColor = this->subHandles[5]->GetUnsigned("TemplateUnderlineColor", 0x0000FFFF);
        funcs["TemplateUnderlineColor"] = &TechDrawParamsP::updateTemplateUnderlineColor;
        gridColor = this->subHandles[5]->GetUnsigned("gridColor", 0x000000FF);
        funcs["gridColor"] = &TechDrawParamsP::updategridColor;
        BreaklineColor = this->subHandles[1]->GetUnsigned("BreaklineColor", 0x000000FF);
        funcs["BreaklineColor"] = &TechDrawParamsP::updateBreaklineColor;
        CenterColor = this->subHandles[1]->GetUnsigned("CenterColor", 0x000000FF);
        funcs["CenterColor"] = &TechDrawParamsP::updateCenterColor;
        HighlightColor = this->subHandles[1]->GetUnsigned("HighlightColor", 0x000000FF);
        funcs["HighlightColor"] = &TechDrawParamsP::updateHighlightColor;
        SectionColor = this->subHandles[1]->GetUnsigned("SectionColor", 0x000000FF);
        funcs["SectionColor"] = &TechDrawParamsP::updateSectionColor;
        VertexColor = this->subHandles[1]->GetUnsigned("VertexColor", 0x000000FF);
        funcs["VertexColor"] = &TechDrawParamsP::updateVertexColor;
        DimensionColor = this->subHandles[2]->GetUnsigned("Color", 0x000000FF);
        funcs["Color"] = &TechDrawParamsP::updateDimensionColor;
        LeaderLineColor = this->subHandles[7]->GetUnsigned("Color", 0x000000FF);
        funcs["Color"] = &TechDrawParamsP::updateLeaderLineColor;
        TrackerColor = this->subHandles[9]->GetUnsigned("TrackerColor", 0xFF0000FF);
        funcs["TrackerColor"] = &TechDrawParamsP::updateTrackerColor;
        LineGroup = this->subHandles[1]->GetInt("LineGroup", 3);
        funcs["LineGroup"] = &TechDrawParamsP::updateLineGroup;
        LineStyleCenter = this->subHandles[1]->GetInt("LineStyleCenter", 4);
        funcs["LineStyleCenter"] = &TechDrawParamsP::updateLineStyleCenter;
        LineStyleHidden = this->subHandles[1]->GetInt("LineStyleHidden", 1);
        funcs["LineStyleHidden"] = &TechDrawParamsP::updateLineStyleHidden;
        LineStyleBreak = this->subHandles[1]->GetInt("LineStyleBreak", 0);
        funcs["LineStyleBreak"] = &TechDrawParamsP::updateLineStyleBreak;
        BreakType = this->subHandles[1]->GetInt("BreakType", 2);
        funcs["BreakType"] = &TechDrawParamsP::updateBreakType;
        LineStyleSection = this->subHandles[1]->GetInt("LineStyleSection", 3);
        funcs["LineStyleSection"] = &TechDrawParamsP::updateLineStyleSection;
        HighlightStyle = this->subHandles[1]->GetInt("HighlightStyle", 2);
        funcs["HighlightStyle"] = &TechDrawParamsP::updateHighlightStyle;
        CenterLine = this->subHandles[1]->GetInt("CenterLine", 2);
        funcs["CenterLine"] = &TechDrawParamsP::updateCenterLine;
        CenterLineStyle = this->subHandles[1]->GetInt("CenterLineStyle", 2);
        funcs["CenterLineStyle"] = &TechDrawParamsP::updateCenterLineStyle;
        HiddenLine = this->subHandles[0]->GetInt("HiddenLine", 0);
        funcs["HiddenLine"] = &TechDrawParamsP::updateHiddenLine;
        EdgeCapStyle = this->subHandles[0]->GetInt("EdgeCapStyle", 0);
        funcs["EdgeCapStyle"] = &TechDrawParamsP::updateEdgeCapStyle;
        LineStandard = this->subHandles[11]->GetInt("LineStandard", 1);
        funcs["LineStandard"] = &TechDrawParamsP::updateLineStandard;
        SectionLineStandard = this->subHandles[11]->GetInt("SectionLineStandard", 1);
        funcs["SectionLineStandard"] = &TechDrawParamsP::updateSectionLineStandard;
        IsoCount = this->subHandles[3]->GetInt("IsoCount", 0);
        funcs["IsoCount"] = &TechDrawParamsP::updateIsoCount;
        CoarseView = this->subHandles[0]->GetBool("CoarseView", false);
        funcs["CoarseView"] = &TechDrawParamsP::updateCoarseView;
        LineSpacingFactorISO = this->subHandles[2]->GetFloat("LineSpacingFactorISO", 2.0);
        funcs["LineSpacingFactorISO"] = &TechDrawParamsP::updateLineSpacingFactorISO;
        SectionUpdateDelay = this->subHandles[0]->GetInt("SectionUpdateDelay", 300);
        funcs["SectionUpdateDelay"] = &TechDrawParamsP::updateSectionUpdateDelay;
        ArrowSize = this->subHandles[2]->GetFloat("ArrowSize", 3.5);
        funcs["ArrowSize"] = &TechDrawParamsP::updateArrowSize;
        FontSize = this->subHandles[2]->GetFloat("FontSize", 5.0);
        funcs["FontSize"] = &TechDrawParamsP::updateFontSize;
        DiameterSymbol = this->subHandles[2]->GetASCII("DiameterSymbol", "\342\214\200");
        funcs["DiameterSymbol"] = &TechDrawParamsP::updateDiameterSymbol;
        StandardAndStyle = this->subHandles[2]->GetInt("StandardAndStyle", 0);
        funcs["StandardAndStyle"] = &TechDrawParamsP::updateStandardAndStyle;
        TemplateFile = this->subHandles[12]->GetASCII("TemplateFile", "");
        funcs["TemplateFile"] = &TechDrawParamsP::updateTemplateFile;
        TemplateDir = this->subHandles[12]->GetASCII("TemplateDir", "");
        funcs["TemplateDir"] = &TechDrawParamsP::updateTemplateDir;
        LineGroupFile = this->subHandles[12]->GetASCII("LineGroupFile", "");
        funcs["LineGroupFile"] = &TechDrawParamsP::updateLineGroupFile;
        FileHatch = this->subHandles[12]->GetASCII("FileHatch", "");
        funcs["FileHatch"] = &TechDrawParamsP::updateFileHatch;
        WeldingDir = this->subHandles[12]->GetASCII("WeldingDir", "");
        funcs["WeldingDir"] = &TechDrawParamsP::updateWeldingDir;
        LineDefLocation = this->subHandles[12]->GetASCII("LineDefLocation", "");
        funcs["LineDefLocation"] = &TechDrawParamsP::updateLineDefLocation;
        LineElementLocation = this->subHandles[12]->GetASCII("LineElementLocation", "");
        funcs["LineElementLocation"] = &TechDrawParamsP::updateLineElementLocation;
        FilePattern = this->subHandles[4]->GetASCII("FilePattern", "");
        funcs["FilePattern"] = &TechDrawParamsP::updateFilePattern;
        TileColor = this->subHandles[5]->GetUnsigned("TileColor", 0x000000FF);
        funcs["TileColor"] = &TechDrawParamsP::updateTileColor;
        SectionLiveUpdate = this->subHandles[0]->GetBool("SectionLiveUpdate", true);
        funcs["SectionLiveUpdate"] = &TechDrawParamsP::updateSectionLiveUpdate;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~TechDrawParamsP() override = default;

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
    static void updateAllowPageOverride(TechDrawParamsP *self) {
        self->AllowPageOverride = self->subHandles[0]->GetBool("AllowPageOverride", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoDist(TechDrawParamsP *self) {
        self->AutoDist = self->subHandles[0]->GetBool("AutoDist", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultScale(TechDrawParamsP *self) {
        self->DefaultScale = self->subHandles[0]->GetFloat("DefaultScale", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultScaleType(TechDrawParamsP *self) {
        self->DefaultScaleType = self->subHandles[0]->GetInt("DefaultScaleType", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultViewScale(TechDrawParamsP *self) {
        self->DefaultViewScale = self->subHandles[0]->GetFloat("DefaultViewScale", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDrawFaceEdges(TechDrawParamsP *self) {
        self->DrawFaceEdges = self->subHandles[0]->GetBool("DrawFaceEdges", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEdgeFuzz(TechDrawParamsP *self) {
        self->EdgeFuzz = self->subHandles[0]->GetFloat("EdgeFuzz", 10.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFixColorAlphaOnLoad(TechDrawParamsP *self) {
        self->FixColorAlphaOnLoad = self->subHandles[0]->GetBool("FixColorAlphaOnLoad", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFocusDistance(TechDrawParamsP *self) {
        self->FocusDistance = self->subHandles[0]->GetFloat("FocusDistance", 100.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGlobalUpdateDrawings(TechDrawParamsP *self) {
        self->GlobalUpdateDrawings = self->subHandles[0]->GetBool("GlobalUpdateDrawings", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHandleFaces(TechDrawParamsP *self) {
        self->HandleFaces = self->subHandles[0]->GetBool("HandleFaces", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateKbPan(TechDrawParamsP *self) {
        self->KbPan = self->subHandles[0]->GetInt("KbPan", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateKbScroll(TechDrawParamsP *self) {
        self->KbScroll = self->subHandles[0]->GetInt("KbScroll", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateKeepPagesUpToDate(TechDrawParamsP *self) {
        self->KeepPagesUpToDate = self->subHandles[0]->GetBool("KeepPagesUpToDate", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMarkFuzz(TechDrawParamsP *self) {
        self->MarkFuzz = self->subHandles[0]->GetFloat("MarkFuzz", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNewFaceFinder(TechDrawParamsP *self) {
        self->NewFaceFinder = self->subHandles[0]->GetBool("NewFaceFinder", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePageRendererVg(TechDrawParamsP *self) {
        self->PageRendererVg = self->subHandles[0]->GetBool("PageRendererVg", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePageRendererVgComposite(TechDrawParamsP *self) {
        self->PageRendererVgComposite = self->subHandles[0]->GetBool("PageRendererVgComposite", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePageRendererVgVerify(TechDrawParamsP *self) {
        self->PageRendererVgVerify = self->subHandles[0]->GetBool("PageRendererVgVerify", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePageRendererVgRoundLineWidth(TechDrawParamsP *self) {
        self->PageRendererVgRoundLineWidth = self->subHandles[0]->GetBool("PageRendererVgRoundLineWidth", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateProjectionAngle(TechDrawParamsP *self) {
        self->ProjectionAngle = self->subHandles[0]->GetInt("ProjectionAngle", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updaterestoreCosmetic(TechDrawParamsP *self) {
        self->restoreCosmetic = self->subHandles[0]->GetBool("restoreCosmetic", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateScrubCount(TechDrawParamsP *self) {
        self->ScrubCount = self->subHandles[0]->GetInt("ScrubCount", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionFuseFirst(TechDrawParamsP *self) {
        self->SectionFuseFirst = self->subHandles[0]->GetBool("SectionFuseFirst", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionUsePreviousCut(TechDrawParamsP *self) {
        self->SectionUsePreviousCut = self->subHandles[0]->GetBool("SectionUsePreviousCut", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadedUnderlayBackend(TechDrawParamsP *self) {
        self->ShadedUnderlayBackend = self->subHandles[0]->GetBool("ShadedUnderlayBackend", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadedUnderlayMaxFaces(TechDrawParamsP *self) {
        self->ShadedUnderlayMaxFaces = self->subHandles[0]->GetInt("ShadedUnderlayMaxFaces", 5000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowDetailHighlight(TechDrawParamsP *self) {
        self->ShowDetailHighlight = self->subHandles[0]->GetBool("ShowDetailHighlight", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowDetailMatting(TechDrawParamsP *self) {
        self->ShowDetailMatting = self->subHandles[0]->GetBool("ShowDetailMatting", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowSectionEdges(TechDrawParamsP *self) {
        self->ShowSectionEdges = self->subHandles[0]->GetBool("ShowSectionEdges", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStoreProjectedGeometry(TechDrawParamsP *self) {
        self->StoreProjectedGeometry = self->subHandles[0]->GetBool("StoreProjectedGeometry", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTemplateDotSize(TechDrawParamsP *self) {
        self->TemplateDotSize = self->subHandles[0]->GetFloat("TemplateDotSize", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVertexScale(TechDrawParamsP *self) {
        self->VertexScale = self->subHandles[0]->GetFloat("VertexScale", 3.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updategridSpacing(TechDrawParamsP *self) {
        self->gridSpacing = self->subHandles[0]->GetFloat("gridSpacing", 10.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatemultiSelection(TechDrawParamsP *self) {
        self->multiSelection = self->subHandles[0]->GetBool("multiSelection", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateshowGrid(TechDrawParamsP *self) {
        self->showGrid = self->subHandles[0]->GetBool("showGrid", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBalloonArrow(TechDrawParamsP *self) {
        self->BalloonArrow = self->subHandles[1]->GetInt("BalloonArrow", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBalloonShape(TechDrawParamsP *self) {
        self->BalloonShape = self->subHandles[1]->GetInt("BalloonShape", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCenterMarkScale(TechDrawParamsP *self) {
        self->CenterMarkScale = self->subHandles[1]->GetFloat("CenterMarkScale", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCosmoCLExtend(TechDrawParamsP *self) {
        self->CosmoCLExtend = self->subHandles[1]->GetFloat("CosmoCLExtend", 3.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCutSurfaceDisplay(TechDrawParamsP *self) {
        self->CutSurfaceDisplay = self->subHandles[1]->GetInt("CutSurfaceDisplay", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMattingStyle(TechDrawParamsP *self) {
        self->MattingStyle = self->subHandles[1]->GetInt("MattingStyle", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxSVGTile(TechDrawParamsP *self) {
        self->MaxSVGTile = self->subHandles[1]->GetInt("MaxSVGTile", 10000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePrintCenterMarks(TechDrawParamsP *self) {
        self->PrintCenterMarks = self->subHandles[1]->GetBool("PrintCenterMarks", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePyramidOrtho(TechDrawParamsP *self) {
        self->PyramidOrtho = self->subHandles[1]->GetBool("PyramidOrtho", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionLineMarks(TechDrawParamsP *self) {
        self->SectionLineMarks = self->subHandles[1]->GetBool("SectionLineMarks", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowCenterMarks(TechDrawParamsP *self) {
        self->ShowCenterMarks = self->subHandles[1]->GetBool("ShowCenterMarks", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSvgOverlapFactor(TechDrawParamsP *self) {
        self->SvgOverlapFactor = self->subHandles[1]->GetFloat("SvgOverlapFactor", 1.25);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSymbolFactor(TechDrawParamsP *self) {
        self->SymbolFactor = self->subHandles[1]->GetFloat("SymbolFactor", 1.25);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAltDecimals(TechDrawParamsP *self) {
        self->AltDecimals = self->subHandles[2]->GetInt("AltDecimals", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateArrowStyle(TechDrawParamsP *self) {
        self->ArrowStyle = self->subHandles[2]->GetInt("ArrowStyle", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoCorrectRefs(TechDrawParamsP *self) {
        self->AutoCorrectRefs = self->subHandles[2]->GetBool("AutoCorrectRefs", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBalloonKink(TechDrawParamsP *self) {
        self->BalloonKink = self->subHandles[2]->GetFloat("BalloonKink", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGapASME(TechDrawParamsP *self) {
        self->GapASME = self->subHandles[2]->GetFloat("GapASME", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGapISO(TechDrawParamsP *self) {
        self->GapISO = self->subHandles[2]->GetFloat("GapISO", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowUnits(TechDrawParamsP *self) {
        self->ShowUnits = self->subHandles[2]->GetBool("ShowUnits", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSymbolSize(TechDrawParamsP *self) {
        self->SymbolSize = self->subHandles[2]->GetFloat("SymbolSize", 64.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTileTextAdjust(TechDrawParamsP *self) {
        self->TileTextAdjust = self->subHandles[2]->GetFloat("TileTextAdjust", 0.75);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTolSizeAdjust(TechDrawParamsP *self) {
        self->TolSizeAdjust = self->subHandles[2]->GetFloat("TolSizeAdjust", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseGlobalDecimals(TechDrawParamsP *self) {
        self->UseGlobalDecimals = self->subHandles[2]->GetBool("UseGlobalDecimals", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateformatSpec(TechDrawParamsP *self) {
        self->formatSpec = self->subHandles[2]->GetASCII("formatSpec", "%.2w");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHardHid(TechDrawParamsP *self) {
        self->HardHid = self->subHandles[3]->GetBool("HardHid", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIsoHid(TechDrawParamsP *self) {
        self->IsoHid = self->subHandles[3]->GetBool("IsoHid", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIsoViz(TechDrawParamsP *self) {
        self->IsoViz = self->subHandles[3]->GetBool("IsoViz", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSeamHid(TechDrawParamsP *self) {
        self->SeamHid = self->subHandles[3]->GetBool("SeamHid", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSeamViz(TechDrawParamsP *self) {
        self->SeamViz = self->subHandles[3]->GetBool("SeamViz", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSmoothHid(TechDrawParamsP *self) {
        self->SmoothHid = self->subHandles[3]->GetBool("SmoothHid", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSmoothViz(TechDrawParamsP *self) {
        self->SmoothViz = self->subHandles[3]->GetBool("SmoothViz", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGeomWeight(TechDrawParamsP *self) {
        self->GeomWeight = self->subHandles[4]->GetFloat("GeomWeight", 0.1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxSeg(TechDrawParamsP *self) {
        self->MaxSeg = self->subHandles[4]->GetInt("MaxSeg", 10000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNamePattern(TechDrawParamsP *self) {
        self->NamePattern = self->subHandles[4]->GetASCII("NamePattern", "Diamond");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateClearFace(TechDrawParamsP *self) {
        self->ClearFace = self->subHandles[5]->GetBool("ClearFace", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightOnDark(TechDrawParamsP *self) {
        self->LightOnDark = self->subHandles[5]->GetBool("LightOnDark", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMonochrome(TechDrawParamsP *self) {
        self->Monochrome = self->subHandles[5]->GetBool("Monochrome", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLabelFont(TechDrawParamsP *self) {
        self->LabelFont = self->subHandles[6]->GetASCII("LabelFont", "osifont");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLabelSize(TechDrawParamsP *self) {
        self->LabelSize = self->subHandles[6]->GetFloat("LabelSize", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoHorizontal(TechDrawParamsP *self) {
        self->AutoHorizontal = self->subHandles[7]->GetBool("AutoHorizontal", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateResolution(TechDrawParamsP *self) {
        self->Resolution = self->subHandles[8]->GetFloat("Resolution", 10.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTrackerWeight(TechDrawParamsP *self) {
        self->TrackerWeight = self->subHandles[9]->GetFloat("TrackerWeight", 4.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateallowCrazyEdge(TechDrawParamsP *self) {
        self->allowCrazyEdge = self->subHandles[10]->GetBool("allowCrazyEdge", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatedebugDetail(TechDrawParamsP *self) {
        self->debugDetail = self->subHandles[10]->GetBool("debugDetail", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatedebugSection(TechDrawParamsP *self) {
        self->debugSection = self->subHandles[10]->GetBool("debugSection", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreSelectColor(TechDrawParamsP *self) {
        self->PreSelectColor = self->subHandles[5]->GetUnsigned("PreSelectColor", 0x00000000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectColor(TechDrawParamsP *self) {
        self->SelectColor = self->subHandles[5]->GetUnsigned("SelectColor", 0x00000000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBackground(TechDrawParamsP *self) {
        self->Background = self->subHandles[5]->GetUnsigned("Background", 0x707070FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCutSurfaceColor(TechDrawParamsP *self) {
        self->CutSurfaceColor = self->subHandles[5]->GetUnsigned("CutSurfaceColor", 0xD3D3D3FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFaceColor(TechDrawParamsP *self) {
        self->FaceColor = self->subHandles[5]->GetUnsigned("FaceColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGeomHatch(TechDrawParamsP *self) {
        self->GeomHatch = self->subHandles[5]->GetUnsigned("GeomHatch", 0x00FF00FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHatch(TechDrawParamsP *self) {
        self->Hatch = self->subHandles[5]->GetUnsigned("Hatch", 0x00FF00FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenColor(TechDrawParamsP *self) {
        self->HiddenColor = self->subHandles[5]->GetUnsigned("HiddenColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightTextColor(TechDrawParamsP *self) {
        self->LightTextColor = self->subHandles[5]->GetUnsigned("LightTextColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNormalColor(TechDrawParamsP *self) {
        self->NormalColor = self->subHandles[5]->GetUnsigned("NormalColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePageColor(TechDrawParamsP *self) {
        self->PageColor = self->subHandles[5]->GetUnsigned("PageColor", 0xFFFFFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTemplateUnderlineColor(TechDrawParamsP *self) {
        self->TemplateUnderlineColor = self->subHandles[5]->GetUnsigned("TemplateUnderlineColor", 0x0000FFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updategridColor(TechDrawParamsP *self) {
        self->gridColor = self->subHandles[5]->GetUnsigned("gridColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBreaklineColor(TechDrawParamsP *self) {
        self->BreaklineColor = self->subHandles[1]->GetUnsigned("BreaklineColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCenterColor(TechDrawParamsP *self) {
        self->CenterColor = self->subHandles[1]->GetUnsigned("CenterColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHighlightColor(TechDrawParamsP *self) {
        self->HighlightColor = self->subHandles[1]->GetUnsigned("HighlightColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionColor(TechDrawParamsP *self) {
        self->SectionColor = self->subHandles[1]->GetUnsigned("SectionColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVertexColor(TechDrawParamsP *self) {
        self->VertexColor = self->subHandles[1]->GetUnsigned("VertexColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensionColor(TechDrawParamsP *self) {
        self->DimensionColor = self->subHandles[2]->GetUnsigned("Color", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLeaderLineColor(TechDrawParamsP *self) {
        self->LeaderLineColor = self->subHandles[7]->GetUnsigned("Color", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTrackerColor(TechDrawParamsP *self) {
        self->TrackerColor = self->subHandles[9]->GetUnsigned("TrackerColor", 0xFF0000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineGroup(TechDrawParamsP *self) {
        self->LineGroup = self->subHandles[1]->GetInt("LineGroup", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineStyleCenter(TechDrawParamsP *self) {
        self->LineStyleCenter = self->subHandles[1]->GetInt("LineStyleCenter", 4);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineStyleHidden(TechDrawParamsP *self) {
        self->LineStyleHidden = self->subHandles[1]->GetInt("LineStyleHidden", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineStyleBreak(TechDrawParamsP *self) {
        self->LineStyleBreak = self->subHandles[1]->GetInt("LineStyleBreak", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBreakType(TechDrawParamsP *self) {
        self->BreakType = self->subHandles[1]->GetInt("BreakType", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineStyleSection(TechDrawParamsP *self) {
        self->LineStyleSection = self->subHandles[1]->GetInt("LineStyleSection", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHighlightStyle(TechDrawParamsP *self) {
        self->HighlightStyle = self->subHandles[1]->GetInt("HighlightStyle", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCenterLine(TechDrawParamsP *self) {
        self->CenterLine = self->subHandles[1]->GetInt("CenterLine", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCenterLineStyle(TechDrawParamsP *self) {
        self->CenterLineStyle = self->subHandles[1]->GetInt("CenterLineStyle", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHiddenLine(TechDrawParamsP *self) {
        self->HiddenLine = self->subHandles[0]->GetInt("HiddenLine", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEdgeCapStyle(TechDrawParamsP *self) {
        self->EdgeCapStyle = self->subHandles[0]->GetInt("EdgeCapStyle", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineStandard(TechDrawParamsP *self) {
        self->LineStandard = self->subHandles[11]->GetInt("LineStandard", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionLineStandard(TechDrawParamsP *self) {
        self->SectionLineStandard = self->subHandles[11]->GetInt("SectionLineStandard", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIsoCount(TechDrawParamsP *self) {
        self->IsoCount = self->subHandles[3]->GetInt("IsoCount", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoarseView(TechDrawParamsP *self) {
        self->CoarseView = self->subHandles[0]->GetBool("CoarseView", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineSpacingFactorISO(TechDrawParamsP *self) {
        self->LineSpacingFactorISO = self->subHandles[2]->GetFloat("LineSpacingFactorISO", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionUpdateDelay(TechDrawParamsP *self) {
        self->SectionUpdateDelay = self->subHandles[0]->GetInt("SectionUpdateDelay", 300);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateArrowSize(TechDrawParamsP *self) {
        self->ArrowSize = self->subHandles[2]->GetFloat("ArrowSize", 3.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFontSize(TechDrawParamsP *self) {
        self->FontSize = self->subHandles[2]->GetFloat("FontSize", 5.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDiameterSymbol(TechDrawParamsP *self) {
        self->DiameterSymbol = self->subHandles[2]->GetASCII("DiameterSymbol", "\342\214\200");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStandardAndStyle(TechDrawParamsP *self) {
        self->StandardAndStyle = self->subHandles[2]->GetInt("StandardAndStyle", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTemplateFile(TechDrawParamsP *self) {
        self->TemplateFile = self->subHandles[12]->GetASCII("TemplateFile", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTemplateDir(TechDrawParamsP *self) {
        self->TemplateDir = self->subHandles[12]->GetASCII("TemplateDir", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineGroupFile(TechDrawParamsP *self) {
        self->LineGroupFile = self->subHandles[12]->GetASCII("LineGroupFile", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFileHatch(TechDrawParamsP *self) {
        self->FileHatch = self->subHandles[12]->GetASCII("FileHatch", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWeldingDir(TechDrawParamsP *self) {
        self->WeldingDir = self->subHandles[12]->GetASCII("WeldingDir", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineDefLocation(TechDrawParamsP *self) {
        self->LineDefLocation = self->subHandles[12]->GetASCII("LineDefLocation", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLineElementLocation(TechDrawParamsP *self) {
        self->LineElementLocation = self->subHandles[12]->GetASCII("LineElementLocation", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFilePattern(TechDrawParamsP *self) {
        self->FilePattern = self->subHandles[4]->GetASCII("FilePattern", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTileColor(TechDrawParamsP *self) {
        self->TileColor = self->subHandles[5]->GetUnsigned("TileColor", 0x000000FF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSectionLiveUpdate(TechDrawParamsP *self) {
        self->SectionLiveUpdate = self->subHandles[0]->GetBool("SectionLiveUpdate", true);
    }
};

// Auto generated code (Tools/params_utils.py:336)
TechDrawParamsP *instance() {
    static TechDrawParamsP *inst = new TechDrawParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _TechDrawParamsRegistrar({
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "AllowPageOverride", "AllowPageOverride", App::ParamInfo::Bool, true)
        .setTitle("Allow Page Override")
        .setDoc("When updating of drawings is switched off globally, let a page\n"
"whose Keep Updated property is on update anyway. Read each time a\n"
"page decides whether to update."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "AutoDist", "AutoDist", App::ParamInfo::Bool, true)
        .setTitle("Auto Dist")
        .setDoc("New projection groups position their secondary views\n"
"automatically. Applies to groups created afterwards; each group\n"
"has its own AutoDistribute property."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "DefaultScale", "DefaultScale", App::ParamInfo::Float, 1.0)
        .setTitle("Default Scale")
        .setDoc("Scale of a new drawing page. Applies to pages created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "DefaultScaleType", "DefaultScaleType", App::ParamInfo::Int, 0)
        .setTitle("Default Scale Type")
        .setDoc("Scale type of a new view: 0 Page (follow the page scale), 1\n"
"Automatic (fit the page), 2 Custom. Applies to views created\n"
"afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "DefaultViewScale", "DefaultViewScale", App::ParamInfo::Float, 1.0)
        .setTitle("Default View Scale")
        .setDoc("Scale of a new view whose scale type is Custom. Applies to views\n"
"created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "DrawFaceEdges", "DrawFaceEdges", App::ParamInfo::Bool, false)
        .setTitle("Draw Face Edges")
        .setDoc("Draw the outline of each detected face in addition to its fill.\n"
"Takes effect when a view is redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "EdgeFuzz", "EdgeFuzz", App::ParamInfo::Float, 10.0)
        .setTitle("Edge Fuzz")
        .setDoc("Size of the area around an edge in which a click selects it.\n"
"Larger makes edges easier to pick but may catch a neighbouring\n"
"edge. the unit."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "FixColorAlphaOnLoad", "FixColorAlphaOnLoad", App::ParamInfo::Bool, true)
        .setTitle("Fix Color Alpha On Load")
        .setDoc("When opening a document, read colours stored with zero opacity as\n"
"opaque; older files saved an unused transparency of 0. Applies to\n"
"documents opened afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "FocusDistance", "FocusDistance", App::ParamInfo::Float, 100.0)
        .setTitle("Focus Distance")
        .setDoc("Focus distance of a new perspective view. Applies to views created\n"
"afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "GlobalUpdateDrawings", "GlobalUpdateDrawings", App::ParamInfo::Bool, true)
        .setTitle("Global Update Drawings")
        .setDoc("Keep all drawing pages in step with the 3D model. Off stops pages\n"
"updating unless page override is allowed. Read each time a page\n"
"decides whether to update."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "HandleFaces", "HandleFaces", App::ParamInfo::Bool, true)
        .setTitle("Handle Faces")
        .setDoc("Find the faces of each view, needed for hatching and face colours.\n"
"Off is faster. Takes effect when a view is recomputed."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "KbPan", "KbPan", App::ParamInfo::Int, 1)
        .setTitle("Kb Pan")
        .setDoc("Direction of horizontal movement of a drawing page with the arrow\n"
"keys: 1 normal, -1 reversed (used as a multiplier). Read when a\n"
"page view is opened."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "KbScroll", "KbScroll", App::ParamInfo::Int, 1)
        .setTitle("Kb Scroll")
        .setDoc("Direction of vertical movement of a drawing page with the arrow\n"
"keys: 1 normal, -1 reversed. Read when a page view is opened."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "KeepPagesUpToDate", "KeepPagesUpToDate", App::ParamInfo::Bool, true)
        .setTitle("Keep Pages Up To Date")
        .setDoc("New pages keep themselves in step with the model. Applies to pages\n"
"created afterwards; each page has its own Keep Updated property."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "MarkFuzz", "MarkFuzz", App::ParamInfo::Float, 5.0)
        .setTitle("Mark Fuzz")
        .setDoc("Size of the selection area around a centre mark. Larger makes\n"
"marks easier to pick."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "NewFaceFinder", "NewFaceFinder", App::ParamInfo::Bool, false)
        .setTitle("New Face Finder")
        .setDoc("Use the newer algorithm to find the faces of a view. Takes effect\n"
"when a view is recomputed."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "PageRendererVg", "PageRendererVg", App::ParamInfo::Bool, false)
        .setTitle("Page Renderer Vg")
        .setDoc("Draw drawing pages with the rendering backend instead of the Qt\n"
"scene items. Applies at once to open pages."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "PageRendererVgComposite", "PageRendererVgComposite", App::ParamInfo::Bool, true)
        .setTitle("Page Renderer Vg Composite")
        .setDoc("With the backend page renderer, compose its picture on the\n"
"graphics card instead of reading it back as an image. Applies at\n"
"once."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "PageRendererVgVerify", "PageRendererVgVerify", App::ParamInfo::Bool, false)
        .setTitle("Page Renderer Vg Verify")
        .setDoc("Also paint the Qt scene items over the backend's picture of the\n"
"page, so that differences between the two show. Applies at once."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "PageRendererVgRoundLineWidth", "PageRendererVgRoundLineWidth", App::ParamInfo::Bool, false)
        .setTitle("Page Renderer Vg Round Line Width")
        .setDoc("With the backend page renderer, round a line's width down to a\n"
"whole tenth of a millimetre, as the Qt scene items draw it: 0.35\n"
"mm as 0.3. Off, a line is as wide as it is asked to be. Its\n"
"dashes are counted in the width it is drawn at. Applies at once\n"
"to open pages."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "ProjectionAngle", "ProjectionAngle", App::ParamInfo::Int, 0)
        .setTitle("Projection Angle")
        .setDoc("Projection convention of new pages and projection groups: 0 first\n"
"angle, 1 third angle, 2 Page: a new projection group follows its\n"
"page, and a new page is first angle."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "restoreCosmetic", "restoreCosmetic", App::ParamInfo::Bool, true)
        .setTitle("Restore Cosmetic")
        .setDoc("Read cosmetic vertices, edges and centre lines when a document is\n"
"opened. Off skips them, to open a file whose cosmetic data is\n"
"damaged."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "ScrubCount", "ScrubCount", App::ParamInfo::Int, 0)
        .setTitle("Scrub Count")
        .setDoc("Number of extra clean-up passes over the edges produced by hidden\n"
"line removal in a new view. Applies to views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "SectionFuseFirst", "SectionFuseFirst", App::ParamInfo::Bool, false)
        .setTitle("Section Fuse First")
        .setDoc("New section views fuse the source shapes into one before cutting.\n"
"Slower, but needed for some overlapping shapes. Applies to\n"
"sections created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "SectionUsePreviousCut", "SectionUsePreviousCut", App::ParamInfo::Bool, false)
        .setTitle("Section Use Previous Cut")
        .setDoc("A new section whose base view is itself a section cuts the already\n"
"cut shape instead of the original. Applies to sections created\n"
"afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "ShadedUnderlayBackend", "ShadedUnderlayBackend", App::ParamInfo::Bool, true)
        .setTitle("Shaded Underlay Backend")
        .setDoc("Render the shaded picture under a view with the rendering backend;\n"
"off falls back to capturing it from the 3D view."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "ShadedUnderlayMaxFaces", "ShadedUnderlayMaxFaces", App::ParamInfo::Int, 5000)
        .setTitle("Shaded Underlay Max Faces")
        .setDoc("Largest number of faces of a shape for which the shaded underlay\n"
"works out a colour per face; above it the shape gets one colour."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "ShowDetailHighlight", "ShowDetailHighlight", App::ParamInfo::Bool, true)
        .setTitle("Show Detail Highlight")
        .setDoc("New detail views show the highlight outline in their source view.\n"
"Applies to detail views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "ShowDetailMatting", "ShowDetailMatting", App::ParamInfo::Bool, true)
        .setTitle("Show Detail Matting")
        .setDoc("New detail views show the matting, the frame around the detail.\n"
"Applies to detail views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "ShowSectionEdges", "ShowSectionEdges", App::ParamInfo::Bool, true)
        .setTitle("Show Section Edges")
        .setDoc("Draw the edges of the cut surface in section views. Takes effect\n"
"when a section is redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "StoreProjectedGeometry", "StoreProjectedGeometry", App::ParamInfo::Bool, true)
        .setTitle("Store Projected Geometry")
        .setDoc("Keep each view's projected geometry in the document so that\n"
"reopening it does not project again. Off makes pages rebuild when\n"
"opened and keeps files smaller."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "TemplateDotSize", "TemplateDotSize", App::ParamInfo::Float, 5.0)
        .setTitle("Template Dot Size")
        .setDoc("Size in mm of the click boxes that mark the editable texts of a\n"
"page template. Takes effect when a template is loaded again."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "VertexScale", "VertexScale", App::ParamInfo::Float, 3.0)
        .setTitle("Vertex Scale")
        .setDoc("Size of vertex dots as a multiple of the visible line width.\n"
"Applies to views and cosmetic vertices created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "gridSpacing", "gridSpacing", App::ParamInfo::Float, 10.0)
        .setTitle("Grid Spacing")
        .setDoc("Spacing in mm of the grid on a new drawing page. Applies to pages\n"
"created afterwards; each page has its own Grid Spacing property."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "multiSelection", "multiSelection", App::ParamInfo::Bool, false)
        .setTitle("Multi Selection")
        .setDoc("Clicking adds to the selection without holding Ctrl. Applies at\n"
"once."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "showGrid", "showGrid", App::ParamInfo::Bool, false)
        .setTitle("Show Grid")
        .setDoc("New drawing pages show a grid. Applies to pages created\n"
"afterwards; each page has its own Show Grid property."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "BalloonArrow", "BalloonArrow", App::ParamInfo::Int, 0)
        .setTitle("Balloon Arrow")
        .setDoc("Arrowhead at the end of a new balloon's leader line, as an index\n"
"into the list of arrow styles. Applies to balloons created\n"
"afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "BalloonShape", "BalloonShape", App::ParamInfo::Int, 0)
        .setTitle("Balloon Shape")
        .setDoc("Shape of a new balloon: 0 circular, 1 none, 2 triangle, 3\n"
"inspection, 4 hexagon, 5 square, 6 rectangle, 7 line. Applies to\n"
"balloons created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "CenterMarkScale", "CenterMarkScale", App::ParamInfo::Float, 0.5)
        .setTitle("Center Mark Scale")
        .setDoc("Size of the centre marks of arcs and circles in a new view, as a\n"
"factor. Applies to views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "CosmoCLExtend", "CosmoCLExtend", App::ParamInfo::Float, 3.0)
        .setTitle("Cosmo CLExtend")
        .setDoc("Distance in mm by which a new cosmetic centre line extends beyond\n"
"the geometry it is drawn on."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "CutSurfaceDisplay", "CutSurfaceDisplay", App::ParamInfo::Int, 2)
        .setTitle("Cut Surface Display")
        .setDoc("How a new section shows its cut surface: 0 hidden, 1 solid colour,\n"
"2 SVG hatch, 3 PAT hatch. Applies to sections created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "MattingStyle", "MattingStyle", App::ParamInfo::Int, 0)
        .setTitle("Matting Style")
        .setDoc("Outline of detail views and of their highlight in the source view:\n"
"0 circle, 1 square. Takes effect when detail views are recomputed."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "MaxSVGTile", "MaxSVGTile", App::ParamInfo::Int, 10000)
        .setTitle("Max SVGTile")
        .setDoc("Largest number of SVG tiles used to hatch one face. 1 to 1000000.\n"
"A limit that keeps a very fine hatch from freezing the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "PrintCenterMarks", "PrintCenterMarks", App::ParamInfo::Bool, false)
        .setTitle("Print Center Marks")
        .setDoc("Include centre marks when a page is printed or exported. Takes\n"
"effect at the next print or redraw."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "PyramidOrtho", "PyramidOrtho", App::ParamInfo::Bool, true)
        .setTitle("Pyramid Ortho")
        .setDoc("Keep a filled-triangle balloon end symbol upright instead of\n"
"turning it with the leader line. Takes effect when balloons are\n"
"redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "SectionLineMarks", "SectionLineMarks", App::ParamInfo::Bool, true)
        .setTitle("Section Line Marks")
        .setDoc("New views show marks where the section line of a complex section\n"
"changes direction. Applies to views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "ShowCenterMarks", "ShowCenterMarks", App::ParamInfo::Bool, false)
        .setTitle("Show Center Marks")
        .setDoc("New views show centre marks on arcs and circles. Applies to views\n"
"created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "SvgOverlapFactor", "SvgOverlapFactor", App::ParamInfo::Float, 1.25)
        .setTitle("Svg Overlap Factor")
        .setDoc("How far the tiled SVG hatch reaches beyond the face it fills, as a\n"
"factor of the face size. Raise it if a hatch leaves gaps at the\n"
"edge of a face."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "SymbolFactor", "SymbolFactor", App::ParamInfo::Float, 1.25)
        .setTitle("Symbol Factor")
        .setDoc("Size factor for welding symbols. Takes effect when welding symbols\n"
"are redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "AltDecimals", "AltDecimals", App::ParamInfo::Int, 2)
        .setTitle("Alt Decimals")
        .setDoc("Number of decimals in dimension values when Use Global Decimals is\n"
"off. Takes effect when dimensions are recomputed."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "ArrowStyle", "ArrowStyle", App::ParamInfo::Int, 0)
        .setTitle("Arrow Style")
        .setDoc("Arrowhead style for dimensions, as an index into the list of arrow\n"
"styles. Takes effect when dimensions are redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "AutoCorrectRefs", "AutoCorrectRefs", App::ParamInfo::Bool, true)
        .setTitle("Auto Correct Refs")
        .setDoc("When the geometry a dimension refers to has changed, try to find\n"
"the matching geometry again. Read each time a dimension is\n"
"recomputed."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "BalloonKink", "BalloonKink", App::ParamInfo::Float, 5.0)
        .setTitle("Balloon Kink")
        .setDoc("Length in mm of the short segment between a new balloon and the\n"
"bend of its leader line. Applies to balloons created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "GapASME", "GapASME", App::ParamInfo::Float, 0.0)
        .setTitle("Gap ASME")
        .setDoc("Gap between the measured point and the start of the extension line\n"
"for ASME dimensions, as a factor. Applies to dimensions created\n"
"afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "GapISO", "GapISO", App::ParamInfo::Float, 0.0)
        .setTitle("Gap ISO")
        .setDoc("Gap between the measured point and the start of the extension line\n"
"for ISO dimensions, as a factor. Applies to dimensions created\n"
"afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "ShowUnits", "ShowUnits", App::ParamInfo::Bool, false)
        .setTitle("Show Units")
        .setDoc("Append the unit to dimension values. Takes effect when dimensions\n"
"are recomputed."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "SymbolSize", "SymbolSize", App::ParamInfo::Float, 64.0)
        .setTitle("Symbol Size")
        .setDoc("Nominal size of the welding symbol pictures; the supplied symbols\n"
"are drawn at 64. Change only for a symbol set drawn at another\n"
"size."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "TileTextAdjust", "TileTextAdjust", App::ParamInfo::Float, 0.75)
        .setTitle("Tile Text Adjust")
        .setDoc("Text size of a new welding symbol relative to the dimension font\n"
"size. Applies to welding symbols created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "TolSizeAdjust", "TolSizeAdjust", App::ParamInfo::Float, 0.5)
        .setTitle("Tol Size Adjust")
        .setDoc("Size of tolerance text relative to the dimension text. Takes\n"
"effect when dimensions are redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "UseGlobalDecimals", "UseGlobalDecimals", App::ParamInfo::Bool, true)
        .setTitle("Use Global Decimals")
        .setDoc("Show dimension values with the number of decimals set for the\n"
"whole program. Off uses the alternate decimals instead."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "formatSpec", "formatSpec", App::ParamInfo::String, "%.2w")
        .setTitle("Format Spec")
        .setDoc("Format of dimension values when global decimals are not used, in\n"
"printf style, for example %.2f; with w in place of f trailing\n"
"zeros are dropped."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/HLR", "HardHid", "HardHid", App::ParamInfo::Bool, false)
        .setTitle("Hard Hid")
        .setDoc("New views show hidden hard edges. Applies to views created\n"
"afterwards; each view has its own property."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/HLR", "IsoHid", "IsoHid", App::ParamInfo::Bool, false)
        .setTitle("Iso Hid")
        .setDoc("New views show hidden iso-parameter lines. Applies to views\n"
"created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/HLR", "IsoViz", "IsoViz", App::ParamInfo::Bool, false)
        .setTitle("Iso Viz")
        .setDoc("New views show visible iso-parameter lines. Applies to views\n"
"created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/HLR", "SeamHid", "SeamHid", App::ParamInfo::Bool, false)
        .setTitle("Seam Hid")
        .setDoc("New views show hidden seam lines. Applies to views created\n"
"afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/HLR", "SeamViz", "SeamViz", App::ParamInfo::Bool, false)
        .setTitle("Seam Viz")
        .setDoc("New views show visible seam lines. Applies to views created\n"
"afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/HLR", "SmoothHid", "SmoothHid", App::ParamInfo::Bool, false)
        .setTitle("Smooth Hid")
        .setDoc("New views show hidden smooth edges, where faces meet tangentially.\n"
"Applies to views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/HLR", "SmoothViz", "SmoothViz", App::ParamInfo::Bool, true)
        .setTitle("Smooth Viz")
        .setDoc("New views show visible smooth edges, where faces meet\n"
"tangentially. Applies to views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/PAT", "GeomWeight", "GeomWeight", App::ParamInfo::Float, 0.1)
        .setTitle("Geom Weight")
        .setDoc("Line width of the PAT hatch on the cut surface of a new section.\n"
"Applies to sections created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/PAT", "MaxSeg", "MaxSeg", App::ParamInfo::Int, 10000)
        .setTitle("Max Seg")
        .setDoc("Largest number of line segments used to draw the PAT hatch of one\n"
"face. 1 to 1000000. A limit that keeps a very fine hatch from\n"
"freezing the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/PAT", "NamePattern", "NamePattern", App::ParamInfo::String, "Diamond")
        .setTitle("Name Pattern")
        .setDoc("Name of the pattern, within the PAT file, used for new geometric\n"
"hatches. Applies to hatches created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "ClearFace", "ClearFace", App::ParamInfo::Bool, false)
        .setTitle("Clear Face")
        .setDoc("Faces of new views are transparent instead of filled with the face\n"
"colour. Applies to views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "LightOnDark", "LightOnDark", App::ParamInfo::Bool, false)
        .setTitle("Light On Dark")
        .setDoc("Draw pages in light colours for a dark page background. Printing\n"
"and export always use the normal colours. Takes effect when a page\n"
"is redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "Monochrome", "Monochrome", App::ParamInfo::Bool, false)
        .setTitle("Monochrome")
        .setDoc("With Light on dark, draw everything in the single light text\n"
"colour instead of lightened colours. Takes effect when a page is\n"
"redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Labels", "LabelFont", "LabelFont", App::ParamInfo::String, "osifont")
        .setTitle("Label font")
        .setDoc("Font of view labels, and the font new dimensions, balloons and\n"
"annotations start with."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Labels", "LabelSize", "LabelSize", App::ParamInfo::Float, 5.0)
        .setTitle("Label size")
        .setDoc("Text size of view labels in mm, and the size new annotations start\n"
"with."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/LeaderLine", "AutoHorizontal", "AutoHorizontal", App::ParamInfo::Bool, true)
        .setTitle("Leader line auto horizontal")
        .setDoc("New leader lines end in a horizontal segment."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Rez", "Resolution", "Resolution", App::ParamInfo::Float, 10.0)
        .setTitle("Scene resolution")
        .setDoc("Scene units per millimetre of a drawing page. Read once when the\n"
"TechDraw user interface is loaded."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Tracker", "TrackerWeight", "TrackerWeight", App::ParamInfo::Float, 4.0)
        .setTitle("Tracker line width")
        .setDoc("Line width of the rubber band lines drawn while a tool tracks the\n"
"mouse on a page."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/debug", "allowCrazyEdge", "allowCrazyEdge", App::ParamInfo::Bool, false)
        .setTitle("Allow crazy edges")
        .setDoc("Keep edges of unreasonable length that projection sometimes\n"
"produces instead of dropping them. For developers."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/debug", "debugDetail", "debugDetail", App::ParamInfo::Bool, false)
        .setTitle("Debug detail views")
        .setDoc("Write the intermediate shapes of a detail view to files while it\n"
"is recomputed. For developers."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/debug", "debugSection", "debugSection", App::ParamInfo::Bool, false)
        .setTitle("Debug section views")
        .setDoc("Write the intermediate shapes of a section view to files while it\n"
"is recomputed. For developers."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "PreSelectColor", "PreSelectColor", App::ParamInfo::Hex, 0x00000000)
        .setTitle("Preselection colour")
        .setDoc("Colour of what the pointer is over in a page view. 0, the value\n"
"while it is not set, follows the preselection colour of the 3D\n"
"view. Read at each hover.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "SelectColor", "SelectColor", App::ParamInfo::Hex, 0x00000000)
        .setTitle("Selection colour")
        .setDoc("Colour of what is selected in a page view. 0, the value while it\n"
"is not set, follows the selection colour of the 3D view. Read at\n"
"each selection.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "Background", "Background", App::ParamInfo::Hex, 0x707070FF)
        .setTitle("Page view background")
        .setDoc("Colour of the area around the sheet in a page view. Read when a\n"
"page view is opened.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "CutSurfaceColor", "CutSurfaceColor", App::ParamInfo::Hex, 0xD3D3D3FF)
        .setTitle("Cut surface colour")
        .setDoc("Colour of the cut surface of new sections that show it as a solid\n"
"colour. Applies to sections created afterwards.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "FaceColor", "FaceColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Face colour")
        .setDoc("Fill colour of the faces of new views. Applies to views created\n"
"afterwards; each view has its own Face Color property.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "GeomHatch", "GeomHatch", App::ParamInfo::Hex, 0x00FF00FF)
        .setTitle("Geometric hatch colour")
        .setDoc("Line colour of new geometric (PAT) hatches. Applies to hatches\n"
"created afterwards.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "Hatch", "Hatch", App::ParamInfo::Hex, 0x00FF00FF)
        .setTitle("Hatch colour")
        .setDoc("Colour of new SVG hatches. Applies to hatches created afterwards.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "HiddenColor", "HiddenColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Hidden line colour")
        .setDoc("Colour of hidden lines. Takes effect when a view is redrawn.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "LightTextColor", "LightTextColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Light text colour")
        .setDoc("The one colour everything is drawn in while Light on dark and\n"
"Monochrome are both on.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "NormalColor", "NormalColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Normal colour")
        .setDoc("Colour of lines and text that are not selected, and the colour new\n"
"cosmetic lines and annotations start with.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "PageColor", "PageColor", App::ParamInfo::Hex, 0xFFFFFFFF)
        .setTitle("Sheet colour")
        .setDoc("Colour of the sheet in a page view.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "TemplateUnderlineColor", "TemplateUnderlineColor", App::ParamInfo::Hex, 0x0000FFFF)
        .setTitle("Template click box colour")
        .setDoc("Colour of the click boxes on the editable texts of a template.\n"
"Read when a template is loaded.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "gridColor", "gridColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Grid colour")
        .setDoc("Colour of the page grid.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "BreaklineColor", "BreaklineColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Break line colour")
        .setDoc("Colour of the break lines of broken views.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "CenterColor", "CenterColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Centre line colour")
        .setDoc("Colour of centre lines and centre marks.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "HighlightColor", "HighlightColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Detail highlight colour")
        .setDoc("Colour of the detail highlight in new views. Applies to views\n"
"created afterwards.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "SectionColor", "SectionColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Section line colour")
        .setDoc("Colour of section lines.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "VertexColor", "VertexColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Vertex colour")
        .setDoc("Colour of vertex dots, and the colour new cosmetic vertices start\n"
"with.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "DimensionColor", "Color", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Dimension colour")
        .setDoc("Colour of dimensions and balloons.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/LeaderLine", "LeaderLineColor", "Color", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Leader line colour")
        .setDoc("Colour of leader lines; new leaders and rich annotations start\n"
"with it.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Tracker", "TrackerColor", "TrackerColor", App::ParamInfo::Hex, 0xFF0000FF)
        .setTitle("Tracker colour")
        .setDoc("Colour of the rubber band lines drawn while a tool tracks the\n"
"mouse on a page.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "LineGroup", "LineGroup", App::ParamInfo::Int, 3)
        .setTitle("Line group")
        .setDoc("Index of the line group -- a set of thin, graphic and thick line\n"
"widths -- in the line group file. Read at each width lookup."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "LineStyleCenter", "LineStyleCenter", App::ParamInfo::Int, 4)
        .setTitle("Centre line style")
        .setDoc("Line style of centre lines, as an index into the lines of the\n"
"active line standard, counted from 0. Takes effect when views are\n"
"redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "LineStyleHidden", "LineStyleHidden", App::ParamInfo::Int, 1)
        .setTitle("Hidden line style")
        .setDoc("Line style of hidden lines, as an index into the lines of the\n"
"active line standard, counted from 0. Takes effect when views are\n"
"redrawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "LineStyleBreak", "LineStyleBreak", App::ParamInfo::Int, 0)
        .setTitle("Break line style")
        .setDoc("Line style of the break lines of new broken views, as an index\n"
"into the lines of the active line standard, counted from 0.\n"
"Applies to views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "BreakType", "BreakType", App::ParamInfo::Int, 2)
        .setTitle("Break type")
        .setDoc("How breaks are drawn in new broken views: 0 not at all, 1 zig-zag,\n"
"2 simple. Applies to views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "LineStyleSection", "LineStyleSection", App::ParamInfo::Int, 3)
        .setTitle("Section line style")
        .setDoc("Line style of the section line of new views, as an index into the\n"
"lines of the active line standard, counted from 0. Applies to\n"
"views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "HighlightStyle", "HighlightStyle", App::ParamInfo::Int, 2)
        .setTitle("Detail highlight style")
        .setDoc("Line style of the detail highlight in new views, as the number of\n"
"a line of the active line standard. Applies to views created\n"
"afterwards. The Detail Highlight Style list of the Annotation page\n"
"stores another key, which nothing that draws reads."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "CenterLine", "CenterLine", App::ParamInfo::Int, 2)
        .setTitle("Centre line pen style")
        .setDoc("Pen style of centre line items: 1 solid, 2 dashed, 3 dotted, 4\n"
"dash-dot, 5 dash-dot-dot. Read as each centre line item is made."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations", "CenterLineStyle", "CenterLineStyle", App::ParamInfo::Int, 2)
        .setTitle("Cosmetic line style")
        .setDoc("Line style new cosmetic edges and centre lines start with, as the\n"
"number of a line of the active line standard. Applies to lines\n"
"created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "HiddenLine", "HiddenLine", App::ParamInfo::Int, 0)
        .setTitle("Hidden edge pen style")
        .setDoc("Pen style of an edge marked hidden: 0 solid, 1 dashed, 2 dotted, 3\n"
"dash-dot, 4 dash-dot-dot. Read when an edge is marked hidden."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "EdgeCapStyle", "EdgeCapStyle", App::ParamInfo::Int, 0)
        .setTitle("Line end shape")
        .setDoc("Shape of line ends: 0 round, 1 square, 2 flat. Flat or square ends\n"
"suit drawings printed 1:1 as cutting guides. Read as pens are\n"
"made."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Standards", "LineStandard", "LineStandard", App::ParamInfo::Int, 1)
        .setTitle("Line standard")
        .setDoc("Line standard in use, as an index into the standards found in the\n"
"line definition folder. Read each time line definitions are\n"
"loaded."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Standards", "SectionLineStandard", "SectionLineStandard", App::ParamInfo::Int, 1)
        .setTitle("Section line standard")
        .setDoc("Where the arrows and letters of a section line sit: 0 as ANSI and\n"
"ASME have them, 1 as ISO has them. Read each time a section line\n"
"is drawn."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/HLR", "IsoCount", "IsoCount", App::ParamInfo::Int, 0)
        .setTitle("Iso-parameter line count")
        .setDoc("Number of iso-parameter lines per face in new views. Applies to\n"
"views created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "CoarseView", "CoarseView", App::ParamInfo::Bool, false)
        .setTitle("Use polygon approximation")
        .setDoc("New views use the fast polygon approximation for hidden lines:\n"
"quicker, but curves become short straight segments and faces are\n"
"not found. Applies to views created afterwards; each view has its\n"
"own Coarse View property."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "LineSpacingFactorISO", "LineSpacingFactorISO", App::ParamInfo::Float, 2.0)
        .setTitle("Line spacing, ISO")
        .setDoc("Space between the dimension line and the text of new ISO\n"
"dimensions, as a multiple of the line width. Applies to dimensions\n"
"created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "SectionUpdateDelay", "SectionUpdateDelay", App::ParamInfo::Int, 300)
        .setTitle("Section update delay")
        .setDoc("Time in milliseconds between a change in the section view dialog\n"
"and the update of the section while live update is on. At least\n"
"100."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "ArrowSize", "ArrowSize", App::ParamInfo::Float, 3.5)
        .setTitle("Arrow size")
        .setDoc("Size in mm of dimension arrowheads. Applies to dimensions created\n"
"afterwards; each dimension has its own Arrow Size property."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "FontSize", "FontSize", App::ParamInfo::Float, 5.0)
        .setTitle("Dimension font size")
        .setDoc("Text size in mm of dimensions and of other annotation text that\n"
"follows it. Applies to dimensions created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "DiameterSymbol", "DiameterSymbol", App::ParamInfo::String, "\342\214\200")
        .setTitle("Diameter symbol")
        .setDoc("Character put in front of diameter dimensions; the diameter sign\n"
"unless another is given. Takes effect when dimensions are\n"
"recomputed."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Dimensions", "StandardAndStyle", "StandardAndStyle", App::ParamInfo::Int, 0)
        .setTitle("Dimension standard and style")
        .setDoc("Standard and text placement of new dimensions: 0 ISO oriented, 1\n"
"ISO referencing, 2 ASME inlined, 3 ASME referencing. Applies to\n"
"dimensions created afterwards."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Files", "TemplateFile", "TemplateFile", App::ParamInfo::String, "")
        .setTitle("Template file")
        .setDoc("Template of a new page. Empty uses A4_LandscapeTD.svg, supplied\n"
"with the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Files", "TemplateDir", "TemplateDir", App::ParamInfo::String, "")
        .setTitle("Template folder")
        .setDoc("Folder the template chooser opens in. Empty uses the one supplied\n"
"with the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Files", "LineGroupFile", "LineGroupFile", App::ParamInfo::String, "")
        .setTitle("Line group file")
        .setDoc("File of line groups -- sets of line widths. Empty uses the one\n"
"supplied with the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Files", "FileHatch", "FileHatch", App::ParamInfo::String, "")
        .setTitle("Hatch file")
        .setDoc("SVG file new hatches take their pattern from. Empty uses\n"
"simple.svg, supplied with the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Files", "WeldingDir", "WeldingDir", App::ParamInfo::String, "")
        .setTitle("Welding symbol folder")
        .setDoc("Folder of welding symbols. Empty uses the AWS symbols supplied\n"
"with the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Files", "LineDefLocation", "LineDefLocation", App::ParamInfo::String, "")
        .setTitle("Line definition folder")
        .setDoc("Folder of the line standard definitions. Empty uses the one\n"
"supplied with the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Files", "LineElementLocation", "LineElementLocation", App::ParamInfo::String, "")
        .setTitle("Line element folder")
        .setDoc("Folder of the line element definitions of the line standards.\n"
"Empty uses the one supplied with the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/PAT", "FilePattern", "FilePattern", App::ParamInfo::String, "")
        .setTitle("PAT file")
        .setDoc("PAT file new geometric hatches take their pattern from. Empty uses\n"
"FCPAT.pat, supplied with the program."),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/Colors", "TileColor", "TileColor", App::ParamInfo::Hex, 0x000000FF)
        .setTitle("Welding symbol tile colour")
        .setDoc("Colour the tiles of a welding symbol are drawn in. On no page.\n"
"Takes effect when a tile is next drawn.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "SectionLiveUpdate", "SectionLiveUpdate", App::ParamInfo::Bool, true)
        .setTitle("Section task: live update")
        .setDoc("'Live update' of the section view task was last checked: the\n"
"section follows each change in the task at once. Stored when the\n"
"box is clicked."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle TechDrawParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docAllowPageOverride() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"When updating of drawings is switched off globally, let a page\n"
"whose Keep Updated property is on update anyway. Read each time a\n"
"page decides whether to update.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getAllowPageOverride() {
    return instance()->AllowPageOverride;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultAllowPageOverride() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setAllowPageOverride(const bool &v) {
    instance()->subHandles[0]->SetBool("AllowPageOverride",v);
    instance()->AllowPageOverride = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeAllowPageOverride() {
    instance()->subHandles[0]->RemoveBool("AllowPageOverride");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docAutoDist() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New projection groups position their secondary views\n"
"automatically. Applies to groups created afterwards; each group\n"
"has its own AutoDistribute property.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getAutoDist() {
    return instance()->AutoDist;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultAutoDist() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setAutoDist(const bool &v) {
    instance()->subHandles[0]->SetBool("AutoDist",v);
    instance()->AutoDist = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeAutoDist() {
    instance()->subHandles[0]->RemoveBool("AutoDist");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docDefaultScale() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Scale of a new drawing page. Applies to pages created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getDefaultScale() {
    return instance()->DefaultScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultDefaultScale() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setDefaultScale(const double &v) {
    instance()->subHandles[0]->SetFloat("DefaultScale",v);
    instance()->DefaultScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeDefaultScale() {
    instance()->subHandles[0]->RemoveFloat("DefaultScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docDefaultScaleType() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Scale type of a new view: 0 Page (follow the page scale), 1\n"
"Automatic (fit the page), 2 Custom. Applies to views created\n"
"afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getDefaultScaleType() {
    return instance()->DefaultScaleType;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultDefaultScaleType() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setDefaultScaleType(const long &v) {
    instance()->subHandles[0]->SetInt("DefaultScaleType",v);
    instance()->DefaultScaleType = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeDefaultScaleType() {
    instance()->subHandles[0]->RemoveInt("DefaultScaleType");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docDefaultViewScale() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Scale of a new view whose scale type is Custom. Applies to views\n"
"created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getDefaultViewScale() {
    return instance()->DefaultViewScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultDefaultViewScale() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setDefaultViewScale(const double &v) {
    instance()->subHandles[0]->SetFloat("DefaultViewScale",v);
    instance()->DefaultViewScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeDefaultViewScale() {
    instance()->subHandles[0]->RemoveFloat("DefaultViewScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docDrawFaceEdges() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Draw the outline of each detected face in addition to its fill.\n"
"Takes effect when a view is redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getDrawFaceEdges() {
    return instance()->DrawFaceEdges;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultDrawFaceEdges() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setDrawFaceEdges(const bool &v) {
    instance()->subHandles[0]->SetBool("DrawFaceEdges",v);
    instance()->DrawFaceEdges = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeDrawFaceEdges() {
    instance()->subHandles[0]->RemoveBool("DrawFaceEdges");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docEdgeFuzz() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Size of the area around an edge in which a click selects it.\n"
"Larger makes edges easier to pick but may catch a neighbouring\n"
"edge. the unit.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getEdgeFuzz() {
    return instance()->EdgeFuzz;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultEdgeFuzz() {
    const static double def = 10.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setEdgeFuzz(const double &v) {
    instance()->subHandles[0]->SetFloat("EdgeFuzz",v);
    instance()->EdgeFuzz = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeEdgeFuzz() {
    instance()->subHandles[0]->RemoveFloat("EdgeFuzz");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docFixColorAlphaOnLoad() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"When opening a document, read colours stored with zero opacity as\n"
"opaque; older files saved an unused transparency of 0. Applies to\n"
"documents opened afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getFixColorAlphaOnLoad() {
    return instance()->FixColorAlphaOnLoad;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultFixColorAlphaOnLoad() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setFixColorAlphaOnLoad(const bool &v) {
    instance()->subHandles[0]->SetBool("FixColorAlphaOnLoad",v);
    instance()->FixColorAlphaOnLoad = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeFixColorAlphaOnLoad() {
    instance()->subHandles[0]->RemoveBool("FixColorAlphaOnLoad");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docFocusDistance() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Focus distance of a new perspective view. Applies to views created\n"
"afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getFocusDistance() {
    return instance()->FocusDistance;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultFocusDistance() {
    const static double def = 100.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setFocusDistance(const double &v) {
    instance()->subHandles[0]->SetFloat("FocusDistance",v);
    instance()->FocusDistance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeFocusDistance() {
    instance()->subHandles[0]->RemoveFloat("FocusDistance");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docGlobalUpdateDrawings() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Keep all drawing pages in step with the 3D model. Off stops pages\n"
"updating unless page override is allowed. Read each time a page\n"
"decides whether to update.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getGlobalUpdateDrawings() {
    return instance()->GlobalUpdateDrawings;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultGlobalUpdateDrawings() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setGlobalUpdateDrawings(const bool &v) {
    instance()->subHandles[0]->SetBool("GlobalUpdateDrawings",v);
    instance()->GlobalUpdateDrawings = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeGlobalUpdateDrawings() {
    instance()->subHandles[0]->RemoveBool("GlobalUpdateDrawings");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docHandleFaces() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Find the faces of each view, needed for hatching and face colours.\n"
"Off is faster. Takes effect when a view is recomputed.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getHandleFaces() {
    return instance()->HandleFaces;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultHandleFaces() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setHandleFaces(const bool &v) {
    instance()->subHandles[0]->SetBool("HandleFaces",v);
    instance()->HandleFaces = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeHandleFaces() {
    instance()->subHandles[0]->RemoveBool("HandleFaces");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docKbPan() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Direction of horizontal movement of a drawing page with the arrow\n"
"keys: 1 normal, -1 reversed (used as a multiplier). Read when a\n"
"page view is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getKbPan() {
    return instance()->KbPan;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultKbPan() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setKbPan(const long &v) {
    instance()->subHandles[0]->SetInt("KbPan",v);
    instance()->KbPan = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeKbPan() {
    instance()->subHandles[0]->RemoveInt("KbPan");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docKbScroll() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Direction of vertical movement of a drawing page with the arrow\n"
"keys: 1 normal, -1 reversed. Read when a page view is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getKbScroll() {
    return instance()->KbScroll;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultKbScroll() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setKbScroll(const long &v) {
    instance()->subHandles[0]->SetInt("KbScroll",v);
    instance()->KbScroll = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeKbScroll() {
    instance()->subHandles[0]->RemoveInt("KbScroll");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docKeepPagesUpToDate() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New pages keep themselves in step with the model. Applies to pages\n"
"created afterwards; each page has its own Keep Updated property.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getKeepPagesUpToDate() {
    return instance()->KeepPagesUpToDate;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultKeepPagesUpToDate() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setKeepPagesUpToDate(const bool &v) {
    instance()->subHandles[0]->SetBool("KeepPagesUpToDate",v);
    instance()->KeepPagesUpToDate = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeKeepPagesUpToDate() {
    instance()->subHandles[0]->RemoveBool("KeepPagesUpToDate");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docMarkFuzz() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Size of the selection area around a centre mark. Larger makes\n"
"marks easier to pick.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getMarkFuzz() {
    return instance()->MarkFuzz;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultMarkFuzz() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setMarkFuzz(const double &v) {
    instance()->subHandles[0]->SetFloat("MarkFuzz",v);
    instance()->MarkFuzz = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeMarkFuzz() {
    instance()->subHandles[0]->RemoveFloat("MarkFuzz");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docNewFaceFinder() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Use the newer algorithm to find the faces of a view. Takes effect\n"
"when a view is recomputed.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getNewFaceFinder() {
    return instance()->NewFaceFinder;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultNewFaceFinder() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setNewFaceFinder(const bool &v) {
    instance()->subHandles[0]->SetBool("NewFaceFinder",v);
    instance()->NewFaceFinder = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeNewFaceFinder() {
    instance()->subHandles[0]->RemoveBool("NewFaceFinder");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docPageRendererVg() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Draw drawing pages with the rendering backend instead of the Qt\n"
"scene items. Applies at once to open pages.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getPageRendererVg() {
    return instance()->PageRendererVg;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultPageRendererVg() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setPageRendererVg(const bool &v) {
    instance()->subHandles[0]->SetBool("PageRendererVg",v);
    instance()->PageRendererVg = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removePageRendererVg() {
    instance()->subHandles[0]->RemoveBool("PageRendererVg");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docPageRendererVgComposite() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"With the backend page renderer, compose its picture on the\n"
"graphics card instead of reading it back as an image. Applies at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getPageRendererVgComposite() {
    return instance()->PageRendererVgComposite;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultPageRendererVgComposite() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setPageRendererVgComposite(const bool &v) {
    instance()->subHandles[0]->SetBool("PageRendererVgComposite",v);
    instance()->PageRendererVgComposite = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removePageRendererVgComposite() {
    instance()->subHandles[0]->RemoveBool("PageRendererVgComposite");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docPageRendererVgVerify() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Also paint the Qt scene items over the backend's picture of the\n"
"page, so that differences between the two show. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getPageRendererVgVerify() {
    return instance()->PageRendererVgVerify;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultPageRendererVgVerify() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setPageRendererVgVerify(const bool &v) {
    instance()->subHandles[0]->SetBool("PageRendererVgVerify",v);
    instance()->PageRendererVgVerify = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removePageRendererVgVerify() {
    instance()->subHandles[0]->RemoveBool("PageRendererVgVerify");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docPageRendererVgRoundLineWidth() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"With the backend page renderer, round a line's width down to a\n"
"whole tenth of a millimetre, as the Qt scene items draw it: 0.35\n"
"mm as 0.3. Off, a line is as wide as it is asked to be. Its\n"
"dashes are counted in the width it is drawn at. Applies at once\n"
"to open pages.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getPageRendererVgRoundLineWidth() {
    return instance()->PageRendererVgRoundLineWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultPageRendererVgRoundLineWidth() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setPageRendererVgRoundLineWidth(const bool &v) {
    instance()->subHandles[0]->SetBool("PageRendererVgRoundLineWidth",v);
    instance()->PageRendererVgRoundLineWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removePageRendererVgRoundLineWidth() {
    instance()->subHandles[0]->RemoveBool("PageRendererVgRoundLineWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docProjectionAngle() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Projection convention of new pages and projection groups: 0 first\n"
"angle, 1 third angle, 2 Page: a new projection group follows its\n"
"page, and a new page is first angle.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getProjectionAngle() {
    return instance()->ProjectionAngle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultProjectionAngle() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setProjectionAngle(const long &v) {
    instance()->subHandles[0]->SetInt("ProjectionAngle",v);
    instance()->ProjectionAngle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeProjectionAngle() {
    instance()->subHandles[0]->RemoveInt("ProjectionAngle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docrestoreCosmetic() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Read cosmetic vertices, edges and centre lines when a document is\n"
"opened. Off skips them, to open a file whose cosmetic data is\n"
"damaged.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getrestoreCosmetic() {
    return instance()->restoreCosmetic;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultrestoreCosmetic() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setrestoreCosmetic(const bool &v) {
    instance()->subHandles[0]->SetBool("restoreCosmetic",v);
    instance()->restoreCosmetic = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removerestoreCosmetic() {
    instance()->subHandles[0]->RemoveBool("restoreCosmetic");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docScrubCount() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Number of extra clean-up passes over the edges produced by hidden\n"
"line removal in a new view. Applies to views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getScrubCount() {
    return instance()->ScrubCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultScrubCount() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setScrubCount(const long &v) {
    instance()->subHandles[0]->SetInt("ScrubCount",v);
    instance()->ScrubCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeScrubCount() {
    instance()->subHandles[0]->RemoveInt("ScrubCount");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSectionFuseFirst() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New section views fuse the source shapes into one before cutting.\n"
"Slower, but needed for some overlapping shapes. Applies to\n"
"sections created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getSectionFuseFirst() {
    return instance()->SectionFuseFirst;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultSectionFuseFirst() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSectionFuseFirst(const bool &v) {
    instance()->subHandles[0]->SetBool("SectionFuseFirst",v);
    instance()->SectionFuseFirst = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSectionFuseFirst() {
    instance()->subHandles[0]->RemoveBool("SectionFuseFirst");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSectionUsePreviousCut() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"A new section whose base view is itself a section cuts the already\n"
"cut shape instead of the original. Applies to sections created\n"
"afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getSectionUsePreviousCut() {
    return instance()->SectionUsePreviousCut;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultSectionUsePreviousCut() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSectionUsePreviousCut(const bool &v) {
    instance()->subHandles[0]->SetBool("SectionUsePreviousCut",v);
    instance()->SectionUsePreviousCut = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSectionUsePreviousCut() {
    instance()->subHandles[0]->RemoveBool("SectionUsePreviousCut");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docShadedUnderlayBackend() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Render the shaded picture under a view with the rendering backend;\n"
"off falls back to capturing it from the 3D view.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getShadedUnderlayBackend() {
    return instance()->ShadedUnderlayBackend;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultShadedUnderlayBackend() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setShadedUnderlayBackend(const bool &v) {
    instance()->subHandles[0]->SetBool("ShadedUnderlayBackend",v);
    instance()->ShadedUnderlayBackend = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeShadedUnderlayBackend() {
    instance()->subHandles[0]->RemoveBool("ShadedUnderlayBackend");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docShadedUnderlayMaxFaces() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Largest number of faces of a shape for which the shaded underlay\n"
"works out a colour per face; above it the shape gets one colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getShadedUnderlayMaxFaces() {
    return instance()->ShadedUnderlayMaxFaces;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultShadedUnderlayMaxFaces() {
    const static long def = 5000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setShadedUnderlayMaxFaces(const long &v) {
    instance()->subHandles[0]->SetInt("ShadedUnderlayMaxFaces",v);
    instance()->ShadedUnderlayMaxFaces = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeShadedUnderlayMaxFaces() {
    instance()->subHandles[0]->RemoveInt("ShadedUnderlayMaxFaces");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docShowDetailHighlight() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New detail views show the highlight outline in their source view.\n"
"Applies to detail views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getShowDetailHighlight() {
    return instance()->ShowDetailHighlight;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultShowDetailHighlight() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setShowDetailHighlight(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowDetailHighlight",v);
    instance()->ShowDetailHighlight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeShowDetailHighlight() {
    instance()->subHandles[0]->RemoveBool("ShowDetailHighlight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docShowDetailMatting() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New detail views show the matting, the frame around the detail.\n"
"Applies to detail views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getShowDetailMatting() {
    return instance()->ShowDetailMatting;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultShowDetailMatting() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setShowDetailMatting(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowDetailMatting",v);
    instance()->ShowDetailMatting = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeShowDetailMatting() {
    instance()->subHandles[0]->RemoveBool("ShowDetailMatting");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docShowSectionEdges() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Draw the edges of the cut surface in section views. Takes effect\n"
"when a section is redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getShowSectionEdges() {
    return instance()->ShowSectionEdges;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultShowSectionEdges() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setShowSectionEdges(const bool &v) {
    instance()->subHandles[0]->SetBool("ShowSectionEdges",v);
    instance()->ShowSectionEdges = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeShowSectionEdges() {
    instance()->subHandles[0]->RemoveBool("ShowSectionEdges");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docStoreProjectedGeometry() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Keep each view's projected geometry in the document so that\n"
"reopening it does not project again. Off makes pages rebuild when\n"
"opened and keeps files smaller.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getStoreProjectedGeometry() {
    return instance()->StoreProjectedGeometry;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultStoreProjectedGeometry() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setStoreProjectedGeometry(const bool &v) {
    instance()->subHandles[0]->SetBool("StoreProjectedGeometry",v);
    instance()->StoreProjectedGeometry = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeStoreProjectedGeometry() {
    instance()->subHandles[0]->RemoveBool("StoreProjectedGeometry");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docTemplateDotSize() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Size in mm of the click boxes that mark the editable texts of a\n"
"page template. Takes effect when a template is loaded again.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getTemplateDotSize() {
    return instance()->TemplateDotSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultTemplateDotSize() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setTemplateDotSize(const double &v) {
    instance()->subHandles[0]->SetFloat("TemplateDotSize",v);
    instance()->TemplateDotSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeTemplateDotSize() {
    instance()->subHandles[0]->RemoveFloat("TemplateDotSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docVertexScale() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Size of vertex dots as a multiple of the visible line width.\n"
"Applies to views and cosmetic vertices created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getVertexScale() {
    return instance()->VertexScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultVertexScale() {
    const static double def = 3.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setVertexScale(const double &v) {
    instance()->subHandles[0]->SetFloat("VertexScale",v);
    instance()->VertexScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeVertexScale() {
    instance()->subHandles[0]->RemoveFloat("VertexScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docgridSpacing() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Spacing in mm of the grid on a new drawing page. Applies to pages\n"
"created afterwards; each page has its own Grid Spacing property.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getgridSpacing() {
    return instance()->gridSpacing;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultgridSpacing() {
    const static double def = 10.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setgridSpacing(const double &v) {
    instance()->subHandles[0]->SetFloat("gridSpacing",v);
    instance()->gridSpacing = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removegridSpacing() {
    instance()->subHandles[0]->RemoveFloat("gridSpacing");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docmultiSelection() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Clicking adds to the selection without holding Ctrl. Applies at\n"
"once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getmultiSelection() {
    return instance()->multiSelection;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultmultiSelection() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setmultiSelection(const bool &v) {
    instance()->subHandles[0]->SetBool("multiSelection",v);
    instance()->multiSelection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removemultiSelection() {
    instance()->subHandles[0]->RemoveBool("multiSelection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docshowGrid() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New drawing pages show a grid. Applies to pages created\n"
"afterwards; each page has its own Show Grid property.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getshowGrid() {
    return instance()->showGrid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultshowGrid() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setshowGrid(const bool &v) {
    instance()->subHandles[0]->SetBool("showGrid",v);
    instance()->showGrid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeshowGrid() {
    instance()->subHandles[0]->RemoveBool("showGrid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docBalloonArrow() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Arrowhead at the end of a new balloon's leader line, as an index\n"
"into the list of arrow styles. Applies to balloons created\n"
"afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getBalloonArrow() {
    return instance()->BalloonArrow;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultBalloonArrow() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setBalloonArrow(const long &v) {
    instance()->subHandles[1]->SetInt("BalloonArrow",v);
    instance()->BalloonArrow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeBalloonArrow() {
    instance()->subHandles[1]->RemoveInt("BalloonArrow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docBalloonShape() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Shape of a new balloon: 0 circular, 1 none, 2 triangle, 3\n"
"inspection, 4 hexagon, 5 square, 6 rectangle, 7 line. Applies to\n"
"balloons created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getBalloonShape() {
    return instance()->BalloonShape;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultBalloonShape() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setBalloonShape(const long &v) {
    instance()->subHandles[1]->SetInt("BalloonShape",v);
    instance()->BalloonShape = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeBalloonShape() {
    instance()->subHandles[1]->RemoveInt("BalloonShape");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docCenterMarkScale() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Size of the centre marks of arcs and circles in a new view, as a\n"
"factor. Applies to views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getCenterMarkScale() {
    return instance()->CenterMarkScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultCenterMarkScale() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setCenterMarkScale(const double &v) {
    instance()->subHandles[1]->SetFloat("CenterMarkScale",v);
    instance()->CenterMarkScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeCenterMarkScale() {
    instance()->subHandles[1]->RemoveFloat("CenterMarkScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docCosmoCLExtend() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Distance in mm by which a new cosmetic centre line extends beyond\n"
"the geometry it is drawn on.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getCosmoCLExtend() {
    return instance()->CosmoCLExtend;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultCosmoCLExtend() {
    const static double def = 3.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setCosmoCLExtend(const double &v) {
    instance()->subHandles[1]->SetFloat("CosmoCLExtend",v);
    instance()->CosmoCLExtend = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeCosmoCLExtend() {
    instance()->subHandles[1]->RemoveFloat("CosmoCLExtend");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docCutSurfaceDisplay() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"How a new section shows its cut surface: 0 hidden, 1 solid colour,\n"
"2 SVG hatch, 3 PAT hatch. Applies to sections created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getCutSurfaceDisplay() {
    return instance()->CutSurfaceDisplay;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultCutSurfaceDisplay() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setCutSurfaceDisplay(const long &v) {
    instance()->subHandles[1]->SetInt("CutSurfaceDisplay",v);
    instance()->CutSurfaceDisplay = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeCutSurfaceDisplay() {
    instance()->subHandles[1]->RemoveInt("CutSurfaceDisplay");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docMattingStyle() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Outline of detail views and of their highlight in the source view:\n"
"0 circle, 1 square. Takes effect when detail views are recomputed.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getMattingStyle() {
    return instance()->MattingStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultMattingStyle() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setMattingStyle(const long &v) {
    instance()->subHandles[1]->SetInt("MattingStyle",v);
    instance()->MattingStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeMattingStyle() {
    instance()->subHandles[1]->RemoveInt("MattingStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docMaxSVGTile() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Largest number of SVG tiles used to hatch one face. 1 to 1000000.\n"
"A limit that keeps a very fine hatch from freezing the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getMaxSVGTile() {
    return instance()->MaxSVGTile;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultMaxSVGTile() {
    const static long def = 10000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setMaxSVGTile(const long &v) {
    instance()->subHandles[1]->SetInt("MaxSVGTile",v);
    instance()->MaxSVGTile = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeMaxSVGTile() {
    instance()->subHandles[1]->RemoveInt("MaxSVGTile");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docPrintCenterMarks() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Include centre marks when a page is printed or exported. Takes\n"
"effect at the next print or redraw.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getPrintCenterMarks() {
    return instance()->PrintCenterMarks;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultPrintCenterMarks() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setPrintCenterMarks(const bool &v) {
    instance()->subHandles[1]->SetBool("PrintCenterMarks",v);
    instance()->PrintCenterMarks = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removePrintCenterMarks() {
    instance()->subHandles[1]->RemoveBool("PrintCenterMarks");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docPyramidOrtho() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Keep a filled-triangle balloon end symbol upright instead of\n"
"turning it with the leader line. Takes effect when balloons are\n"
"redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getPyramidOrtho() {
    return instance()->PyramidOrtho;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultPyramidOrtho() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setPyramidOrtho(const bool &v) {
    instance()->subHandles[1]->SetBool("PyramidOrtho",v);
    instance()->PyramidOrtho = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removePyramidOrtho() {
    instance()->subHandles[1]->RemoveBool("PyramidOrtho");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSectionLineMarks() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views show marks where the section line of a complex section\n"
"changes direction. Applies to views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getSectionLineMarks() {
    return instance()->SectionLineMarks;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultSectionLineMarks() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSectionLineMarks(const bool &v) {
    instance()->subHandles[1]->SetBool("SectionLineMarks",v);
    instance()->SectionLineMarks = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSectionLineMarks() {
    instance()->subHandles[1]->RemoveBool("SectionLineMarks");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docShowCenterMarks() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views show centre marks on arcs and circles. Applies to views\n"
"created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getShowCenterMarks() {
    return instance()->ShowCenterMarks;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultShowCenterMarks() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setShowCenterMarks(const bool &v) {
    instance()->subHandles[1]->SetBool("ShowCenterMarks",v);
    instance()->ShowCenterMarks = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeShowCenterMarks() {
    instance()->subHandles[1]->RemoveBool("ShowCenterMarks");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSvgOverlapFactor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"How far the tiled SVG hatch reaches beyond the face it fills, as a\n"
"factor of the face size. Raise it if a hatch leaves gaps at the\n"
"edge of a face.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getSvgOverlapFactor() {
    return instance()->SvgOverlapFactor;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultSvgOverlapFactor() {
    const static double def = 1.25;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSvgOverlapFactor(const double &v) {
    instance()->subHandles[1]->SetFloat("SvgOverlapFactor",v);
    instance()->SvgOverlapFactor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSvgOverlapFactor() {
    instance()->subHandles[1]->RemoveFloat("SvgOverlapFactor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSymbolFactor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Size factor for welding symbols. Takes effect when welding symbols\n"
"are redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getSymbolFactor() {
    return instance()->SymbolFactor;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultSymbolFactor() {
    const static double def = 1.25;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSymbolFactor(const double &v) {
    instance()->subHandles[1]->SetFloat("SymbolFactor",v);
    instance()->SymbolFactor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSymbolFactor() {
    instance()->subHandles[1]->RemoveFloat("SymbolFactor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docAltDecimals() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Number of decimals in dimension values when Use Global Decimals is\n"
"off. Takes effect when dimensions are recomputed.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getAltDecimals() {
    return instance()->AltDecimals;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultAltDecimals() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setAltDecimals(const long &v) {
    instance()->subHandles[2]->SetInt("AltDecimals",v);
    instance()->AltDecimals = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeAltDecimals() {
    instance()->subHandles[2]->RemoveInt("AltDecimals");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docArrowStyle() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Arrowhead style for dimensions, as an index into the list of arrow\n"
"styles. Takes effect when dimensions are redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getArrowStyle() {
    return instance()->ArrowStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultArrowStyle() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setArrowStyle(const long &v) {
    instance()->subHandles[2]->SetInt("ArrowStyle",v);
    instance()->ArrowStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeArrowStyle() {
    instance()->subHandles[2]->RemoveInt("ArrowStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docAutoCorrectRefs() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"When the geometry a dimension refers to has changed, try to find\n"
"the matching geometry again. Read each time a dimension is\n"
"recomputed.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getAutoCorrectRefs() {
    return instance()->AutoCorrectRefs;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultAutoCorrectRefs() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setAutoCorrectRefs(const bool &v) {
    instance()->subHandles[2]->SetBool("AutoCorrectRefs",v);
    instance()->AutoCorrectRefs = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeAutoCorrectRefs() {
    instance()->subHandles[2]->RemoveBool("AutoCorrectRefs");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docBalloonKink() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Length in mm of the short segment between a new balloon and the\n"
"bend of its leader line. Applies to balloons created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getBalloonKink() {
    return instance()->BalloonKink;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultBalloonKink() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setBalloonKink(const double &v) {
    instance()->subHandles[2]->SetFloat("BalloonKink",v);
    instance()->BalloonKink = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeBalloonKink() {
    instance()->subHandles[2]->RemoveFloat("BalloonKink");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docGapASME() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Gap between the measured point and the start of the extension line\n"
"for ASME dimensions, as a factor. Applies to dimensions created\n"
"afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getGapASME() {
    return instance()->GapASME;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultGapASME() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setGapASME(const double &v) {
    instance()->subHandles[2]->SetFloat("GapASME",v);
    instance()->GapASME = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeGapASME() {
    instance()->subHandles[2]->RemoveFloat("GapASME");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docGapISO() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Gap between the measured point and the start of the extension line\n"
"for ISO dimensions, as a factor. Applies to dimensions created\n"
"afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getGapISO() {
    return instance()->GapISO;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultGapISO() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setGapISO(const double &v) {
    instance()->subHandles[2]->SetFloat("GapISO",v);
    instance()->GapISO = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeGapISO() {
    instance()->subHandles[2]->RemoveFloat("GapISO");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docShowUnits() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Append the unit to dimension values. Takes effect when dimensions\n"
"are recomputed.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getShowUnits() {
    return instance()->ShowUnits;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultShowUnits() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setShowUnits(const bool &v) {
    instance()->subHandles[2]->SetBool("ShowUnits",v);
    instance()->ShowUnits = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeShowUnits() {
    instance()->subHandles[2]->RemoveBool("ShowUnits");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSymbolSize() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Nominal size of the welding symbol pictures; the supplied symbols\n"
"are drawn at 64. Change only for a symbol set drawn at another\n"
"size.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getSymbolSize() {
    return instance()->SymbolSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultSymbolSize() {
    const static double def = 64.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSymbolSize(const double &v) {
    instance()->subHandles[2]->SetFloat("SymbolSize",v);
    instance()->SymbolSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSymbolSize() {
    instance()->subHandles[2]->RemoveFloat("SymbolSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docTileTextAdjust() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Text size of a new welding symbol relative to the dimension font\n"
"size. Applies to welding symbols created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getTileTextAdjust() {
    return instance()->TileTextAdjust;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultTileTextAdjust() {
    const static double def = 0.75;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setTileTextAdjust(const double &v) {
    instance()->subHandles[2]->SetFloat("TileTextAdjust",v);
    instance()->TileTextAdjust = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeTileTextAdjust() {
    instance()->subHandles[2]->RemoveFloat("TileTextAdjust");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docTolSizeAdjust() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Size of tolerance text relative to the dimension text. Takes\n"
"effect when dimensions are redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getTolSizeAdjust() {
    return instance()->TolSizeAdjust;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultTolSizeAdjust() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setTolSizeAdjust(const double &v) {
    instance()->subHandles[2]->SetFloat("TolSizeAdjust",v);
    instance()->TolSizeAdjust = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeTolSizeAdjust() {
    instance()->subHandles[2]->RemoveFloat("TolSizeAdjust");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docUseGlobalDecimals() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Show dimension values with the number of decimals set for the\n"
"whole program. Off uses the alternate decimals instead.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getUseGlobalDecimals() {
    return instance()->UseGlobalDecimals;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultUseGlobalDecimals() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setUseGlobalDecimals(const bool &v) {
    instance()->subHandles[2]->SetBool("UseGlobalDecimals",v);
    instance()->UseGlobalDecimals = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeUseGlobalDecimals() {
    instance()->subHandles[2]->RemoveBool("UseGlobalDecimals");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docformatSpec() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Format of dimension values when global decimals are not used, in\n"
"printf style, for example %.2f; with w in place of f trailing\n"
"zeros are dropped.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getformatSpec() {
    return instance()->formatSpec;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultformatSpec() {
    const static std::string def = "%.2w";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setformatSpec(const std::string &v) {
    instance()->subHandles[2]->SetASCII("formatSpec",v);
    instance()->formatSpec = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeformatSpec() {
    instance()->subHandles[2]->RemoveASCII("formatSpec");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docHardHid() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views show hidden hard edges. Applies to views created\n"
"afterwards; each view has its own property.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getHardHid() {
    return instance()->HardHid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultHardHid() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setHardHid(const bool &v) {
    instance()->subHandles[3]->SetBool("HardHid",v);
    instance()->HardHid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeHardHid() {
    instance()->subHandles[3]->RemoveBool("HardHid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docIsoHid() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views show hidden iso-parameter lines. Applies to views\n"
"created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getIsoHid() {
    return instance()->IsoHid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultIsoHid() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setIsoHid(const bool &v) {
    instance()->subHandles[3]->SetBool("IsoHid",v);
    instance()->IsoHid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeIsoHid() {
    instance()->subHandles[3]->RemoveBool("IsoHid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docIsoViz() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views show visible iso-parameter lines. Applies to views\n"
"created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getIsoViz() {
    return instance()->IsoViz;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultIsoViz() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setIsoViz(const bool &v) {
    instance()->subHandles[3]->SetBool("IsoViz",v);
    instance()->IsoViz = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeIsoViz() {
    instance()->subHandles[3]->RemoveBool("IsoViz");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSeamHid() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views show hidden seam lines. Applies to views created\n"
"afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getSeamHid() {
    return instance()->SeamHid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultSeamHid() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSeamHid(const bool &v) {
    instance()->subHandles[3]->SetBool("SeamHid",v);
    instance()->SeamHid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSeamHid() {
    instance()->subHandles[3]->RemoveBool("SeamHid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSeamViz() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views show visible seam lines. Applies to views created\n"
"afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getSeamViz() {
    return instance()->SeamViz;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultSeamViz() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSeamViz(const bool &v) {
    instance()->subHandles[3]->SetBool("SeamViz",v);
    instance()->SeamViz = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSeamViz() {
    instance()->subHandles[3]->RemoveBool("SeamViz");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSmoothHid() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views show hidden smooth edges, where faces meet tangentially.\n"
"Applies to views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getSmoothHid() {
    return instance()->SmoothHid;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultSmoothHid() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSmoothHid(const bool &v) {
    instance()->subHandles[3]->SetBool("SmoothHid",v);
    instance()->SmoothHid = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSmoothHid() {
    instance()->subHandles[3]->RemoveBool("SmoothHid");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSmoothViz() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views show visible smooth edges, where faces meet\n"
"tangentially. Applies to views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getSmoothViz() {
    return instance()->SmoothViz;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultSmoothViz() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSmoothViz(const bool &v) {
    instance()->subHandles[3]->SetBool("SmoothViz",v);
    instance()->SmoothViz = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSmoothViz() {
    instance()->subHandles[3]->RemoveBool("SmoothViz");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docGeomWeight() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line width of the PAT hatch on the cut surface of a new section.\n"
"Applies to sections created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getGeomWeight() {
    return instance()->GeomWeight;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultGeomWeight() {
    const static double def = 0.1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setGeomWeight(const double &v) {
    instance()->subHandles[4]->SetFloat("GeomWeight",v);
    instance()->GeomWeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeGeomWeight() {
    instance()->subHandles[4]->RemoveFloat("GeomWeight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docMaxSeg() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Largest number of line segments used to draw the PAT hatch of one\n"
"face. 1 to 1000000. A limit that keeps a very fine hatch from\n"
"freezing the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getMaxSeg() {
    return instance()->MaxSeg;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultMaxSeg() {
    const static long def = 10000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setMaxSeg(const long &v) {
    instance()->subHandles[4]->SetInt("MaxSeg",v);
    instance()->MaxSeg = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeMaxSeg() {
    instance()->subHandles[4]->RemoveInt("MaxSeg");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docNamePattern() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Name of the pattern, within the PAT file, used for new geometric\n"
"hatches. Applies to hatches created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getNamePattern() {
    return instance()->NamePattern;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultNamePattern() {
    const static std::string def = "Diamond";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setNamePattern(const std::string &v) {
    instance()->subHandles[4]->SetASCII("NamePattern",v);
    instance()->NamePattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeNamePattern() {
    instance()->subHandles[4]->RemoveASCII("NamePattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docClearFace() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Faces of new views are transparent instead of filled with the face\n"
"colour. Applies to views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getClearFace() {
    return instance()->ClearFace;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultClearFace() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setClearFace(const bool &v) {
    instance()->subHandles[5]->SetBool("ClearFace",v);
    instance()->ClearFace = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeClearFace() {
    instance()->subHandles[5]->RemoveBool("ClearFace");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLightOnDark() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Draw pages in light colours for a dark page background. Printing\n"
"and export always use the normal colours. Takes effect when a page\n"
"is redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getLightOnDark() {
    return instance()->LightOnDark;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultLightOnDark() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLightOnDark(const bool &v) {
    instance()->subHandles[5]->SetBool("LightOnDark",v);
    instance()->LightOnDark = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLightOnDark() {
    instance()->subHandles[5]->RemoveBool("LightOnDark");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docMonochrome() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"With Light on dark, draw everything in the single light text\n"
"colour instead of lightened colours. Takes effect when a page is\n"
"redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getMonochrome() {
    return instance()->Monochrome;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultMonochrome() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setMonochrome(const bool &v) {
    instance()->subHandles[5]->SetBool("Monochrome",v);
    instance()->Monochrome = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeMonochrome() {
    instance()->subHandles[5]->RemoveBool("Monochrome");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLabelFont() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Font of view labels, and the font new dimensions, balloons and\n"
"annotations start with.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getLabelFont() {
    return instance()->LabelFont;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultLabelFont() {
    const static std::string def = "osifont";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLabelFont(const std::string &v) {
    instance()->subHandles[6]->SetASCII("LabelFont",v);
    instance()->LabelFont = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLabelFont() {
    instance()->subHandles[6]->RemoveASCII("LabelFont");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLabelSize() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Text size of view labels in mm, and the size new annotations start\n"
"with.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getLabelSize() {
    return instance()->LabelSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultLabelSize() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLabelSize(const double &v) {
    instance()->subHandles[6]->SetFloat("LabelSize",v);
    instance()->LabelSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLabelSize() {
    instance()->subHandles[6]->RemoveFloat("LabelSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docAutoHorizontal() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New leader lines end in a horizontal segment.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getAutoHorizontal() {
    return instance()->AutoHorizontal;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultAutoHorizontal() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setAutoHorizontal(const bool &v) {
    instance()->subHandles[7]->SetBool("AutoHorizontal",v);
    instance()->AutoHorizontal = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeAutoHorizontal() {
    instance()->subHandles[7]->RemoveBool("AutoHorizontal");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docResolution() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Scene units per millimetre of a drawing page. Read once when the\n"
"TechDraw user interface is loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getResolution() {
    return instance()->Resolution;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultResolution() {
    const static double def = 10.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setResolution(const double &v) {
    instance()->subHandles[8]->SetFloat("Resolution",v);
    instance()->Resolution = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeResolution() {
    instance()->subHandles[8]->RemoveFloat("Resolution");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docTrackerWeight() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line width of the rubber band lines drawn while a tool tracks the\n"
"mouse on a page.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getTrackerWeight() {
    return instance()->TrackerWeight;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultTrackerWeight() {
    const static double def = 4.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setTrackerWeight(const double &v) {
    instance()->subHandles[9]->SetFloat("TrackerWeight",v);
    instance()->TrackerWeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeTrackerWeight() {
    instance()->subHandles[9]->RemoveFloat("TrackerWeight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docallowCrazyEdge() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Keep edges of unreasonable length that projection sometimes\n"
"produces instead of dropping them. For developers.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getallowCrazyEdge() {
    return instance()->allowCrazyEdge;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultallowCrazyEdge() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setallowCrazyEdge(const bool &v) {
    instance()->subHandles[10]->SetBool("allowCrazyEdge",v);
    instance()->allowCrazyEdge = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeallowCrazyEdge() {
    instance()->subHandles[10]->RemoveBool("allowCrazyEdge");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docdebugDetail() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Write the intermediate shapes of a detail view to files while it\n"
"is recomputed. For developers.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getdebugDetail() {
    return instance()->debugDetail;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultdebugDetail() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setdebugDetail(const bool &v) {
    instance()->subHandles[10]->SetBool("debugDetail",v);
    instance()->debugDetail = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removedebugDetail() {
    instance()->subHandles[10]->RemoveBool("debugDetail");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docdebugSection() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Write the intermediate shapes of a section view to files while it\n"
"is recomputed. For developers.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getdebugSection() {
    return instance()->debugSection;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultdebugSection() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setdebugSection(const bool &v) {
    instance()->subHandles[10]->SetBool("debugSection",v);
    instance()->debugSection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removedebugSection() {
    instance()->subHandles[10]->RemoveBool("debugSection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docPreSelectColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of what the pointer is over in a page view. 0, the value\n"
"while it is not set, follows the preselection colour of the 3D\n"
"view. Read at each hover.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getPreSelectColor() {
    return instance()->PreSelectColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultPreSelectColor() {
    const static unsigned long def = 0x00000000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setPreSelectColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("PreSelectColor",v);
    instance()->PreSelectColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removePreSelectColor() {
    instance()->subHandles[5]->RemoveUnsigned("PreSelectColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSelectColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of what is selected in a page view. 0, the value while it\n"
"is not set, follows the selection colour of the 3D view. Read at\n"
"each selection.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getSelectColor() {
    return instance()->SelectColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultSelectColor() {
    const static unsigned long def = 0x00000000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSelectColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("SelectColor",v);
    instance()->SelectColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSelectColor() {
    instance()->subHandles[5]->RemoveUnsigned("SelectColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docBackground() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of the area around the sheet in a page view. Read when a\n"
"page view is opened.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getBackground() {
    return instance()->Background;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultBackground() {
    const static unsigned long def = 0x707070FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setBackground(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("Background",v);
    instance()->Background = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeBackground() {
    instance()->subHandles[5]->RemoveUnsigned("Background");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docCutSurfaceColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of the cut surface of new sections that show it as a solid\n"
"colour. Applies to sections created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getCutSurfaceColor() {
    return instance()->CutSurfaceColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultCutSurfaceColor() {
    const static unsigned long def = 0xD3D3D3FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setCutSurfaceColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("CutSurfaceColor",v);
    instance()->CutSurfaceColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeCutSurfaceColor() {
    instance()->subHandles[5]->RemoveUnsigned("CutSurfaceColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docFaceColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Fill colour of the faces of new views. Applies to views created\n"
"afterwards; each view has its own Face Color property.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getFaceColor() {
    return instance()->FaceColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultFaceColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setFaceColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("FaceColor",v);
    instance()->FaceColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeFaceColor() {
    instance()->subHandles[5]->RemoveUnsigned("FaceColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docGeomHatch() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line colour of new geometric (PAT) hatches. Applies to hatches\n"
"created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getGeomHatch() {
    return instance()->GeomHatch;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultGeomHatch() {
    const static unsigned long def = 0x00FF00FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setGeomHatch(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("GeomHatch",v);
    instance()->GeomHatch = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeGeomHatch() {
    instance()->subHandles[5]->RemoveUnsigned("GeomHatch");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docHatch() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of new SVG hatches. Applies to hatches created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getHatch() {
    return instance()->Hatch;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultHatch() {
    const static unsigned long def = 0x00FF00FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setHatch(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("Hatch",v);
    instance()->Hatch = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeHatch() {
    instance()->subHandles[5]->RemoveUnsigned("Hatch");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docHiddenColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of hidden lines. Takes effect when a view is redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getHiddenColor() {
    return instance()->HiddenColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultHiddenColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setHiddenColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("HiddenColor",v);
    instance()->HiddenColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeHiddenColor() {
    instance()->subHandles[5]->RemoveUnsigned("HiddenColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLightTextColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"The one colour everything is drawn in while Light on dark and\n"
"Monochrome are both on.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getLightTextColor() {
    return instance()->LightTextColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultLightTextColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLightTextColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("LightTextColor",v);
    instance()->LightTextColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLightTextColor() {
    instance()->subHandles[5]->RemoveUnsigned("LightTextColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docNormalColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of lines and text that are not selected, and the colour new\n"
"cosmetic lines and annotations start with.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getNormalColor() {
    return instance()->NormalColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultNormalColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setNormalColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("NormalColor",v);
    instance()->NormalColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeNormalColor() {
    instance()->subHandles[5]->RemoveUnsigned("NormalColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docPageColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of the sheet in a page view.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getPageColor() {
    return instance()->PageColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultPageColor() {
    const static unsigned long def = 0xFFFFFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setPageColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("PageColor",v);
    instance()->PageColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removePageColor() {
    instance()->subHandles[5]->RemoveUnsigned("PageColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docTemplateUnderlineColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of the click boxes on the editable texts of a template.\n"
"Read when a template is loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getTemplateUnderlineColor() {
    return instance()->TemplateUnderlineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultTemplateUnderlineColor() {
    const static unsigned long def = 0x0000FFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setTemplateUnderlineColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("TemplateUnderlineColor",v);
    instance()->TemplateUnderlineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeTemplateUnderlineColor() {
    instance()->subHandles[5]->RemoveUnsigned("TemplateUnderlineColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docgridColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of the page grid.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getgridColor() {
    return instance()->gridColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultgridColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setgridColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("gridColor",v);
    instance()->gridColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removegridColor() {
    instance()->subHandles[5]->RemoveUnsigned("gridColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docBreaklineColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of the break lines of broken views.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getBreaklineColor() {
    return instance()->BreaklineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultBreaklineColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setBreaklineColor(const unsigned long &v) {
    instance()->subHandles[1]->SetUnsigned("BreaklineColor",v);
    instance()->BreaklineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeBreaklineColor() {
    instance()->subHandles[1]->RemoveUnsigned("BreaklineColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docCenterColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of centre lines and centre marks.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getCenterColor() {
    return instance()->CenterColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultCenterColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setCenterColor(const unsigned long &v) {
    instance()->subHandles[1]->SetUnsigned("CenterColor",v);
    instance()->CenterColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeCenterColor() {
    instance()->subHandles[1]->RemoveUnsigned("CenterColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docHighlightColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of the detail highlight in new views. Applies to views\n"
"created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getHighlightColor() {
    return instance()->HighlightColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultHighlightColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setHighlightColor(const unsigned long &v) {
    instance()->subHandles[1]->SetUnsigned("HighlightColor",v);
    instance()->HighlightColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeHighlightColor() {
    instance()->subHandles[1]->RemoveUnsigned("HighlightColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSectionColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of section lines.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getSectionColor() {
    return instance()->SectionColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultSectionColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSectionColor(const unsigned long &v) {
    instance()->subHandles[1]->SetUnsigned("SectionColor",v);
    instance()->SectionColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSectionColor() {
    instance()->subHandles[1]->RemoveUnsigned("SectionColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docVertexColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of vertex dots, and the colour new cosmetic vertices start\n"
"with.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getVertexColor() {
    return instance()->VertexColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultVertexColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setVertexColor(const unsigned long &v) {
    instance()->subHandles[1]->SetUnsigned("VertexColor",v);
    instance()->VertexColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeVertexColor() {
    instance()->subHandles[1]->RemoveUnsigned("VertexColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docDimensionColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of dimensions and balloons.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getDimensionColor() {
    return instance()->DimensionColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultDimensionColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setDimensionColor(const unsigned long &v) {
    instance()->subHandles[2]->SetUnsigned("Color",v);
    instance()->DimensionColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeDimensionColor() {
    instance()->subHandles[2]->RemoveUnsigned("Color");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLeaderLineColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of leader lines; new leaders and rich annotations start\n"
"with it.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getLeaderLineColor() {
    return instance()->LeaderLineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultLeaderLineColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLeaderLineColor(const unsigned long &v) {
    instance()->subHandles[7]->SetUnsigned("Color",v);
    instance()->LeaderLineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLeaderLineColor() {
    instance()->subHandles[7]->RemoveUnsigned("Color");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docTrackerColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour of the rubber band lines drawn while a tool tracks the\n"
"mouse on a page.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getTrackerColor() {
    return instance()->TrackerColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultTrackerColor() {
    const static unsigned long def = 0xFF0000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setTrackerColor(const unsigned long &v) {
    instance()->subHandles[9]->SetUnsigned("TrackerColor",v);
    instance()->TrackerColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeTrackerColor() {
    instance()->subHandles[9]->RemoveUnsigned("TrackerColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineGroup() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Index of the line group -- a set of thin, graphic and thick line\n"
"widths -- in the line group file. Read at each width lookup.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getLineGroup() {
    return instance()->LineGroup;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultLineGroup() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineGroup(const long &v) {
    instance()->subHandles[1]->SetInt("LineGroup",v);
    instance()->LineGroup = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineGroup() {
    instance()->subHandles[1]->RemoveInt("LineGroup");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineStyleCenter() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line style of centre lines, as an index into the lines of the\n"
"active line standard, counted from 0. Takes effect when views are\n"
"redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getLineStyleCenter() {
    return instance()->LineStyleCenter;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultLineStyleCenter() {
    const static long def = 4;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineStyleCenter(const long &v) {
    instance()->subHandles[1]->SetInt("LineStyleCenter",v);
    instance()->LineStyleCenter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineStyleCenter() {
    instance()->subHandles[1]->RemoveInt("LineStyleCenter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineStyleHidden() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line style of hidden lines, as an index into the lines of the\n"
"active line standard, counted from 0. Takes effect when views are\n"
"redrawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getLineStyleHidden() {
    return instance()->LineStyleHidden;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultLineStyleHidden() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineStyleHidden(const long &v) {
    instance()->subHandles[1]->SetInt("LineStyleHidden",v);
    instance()->LineStyleHidden = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineStyleHidden() {
    instance()->subHandles[1]->RemoveInt("LineStyleHidden");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineStyleBreak() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line style of the break lines of new broken views, as an index\n"
"into the lines of the active line standard, counted from 0.\n"
"Applies to views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getLineStyleBreak() {
    return instance()->LineStyleBreak;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultLineStyleBreak() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineStyleBreak(const long &v) {
    instance()->subHandles[1]->SetInt("LineStyleBreak",v);
    instance()->LineStyleBreak = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineStyleBreak() {
    instance()->subHandles[1]->RemoveInt("LineStyleBreak");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docBreakType() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"How breaks are drawn in new broken views: 0 not at all, 1 zig-zag,\n"
"2 simple. Applies to views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getBreakType() {
    return instance()->BreakType;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultBreakType() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setBreakType(const long &v) {
    instance()->subHandles[1]->SetInt("BreakType",v);
    instance()->BreakType = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeBreakType() {
    instance()->subHandles[1]->RemoveInt("BreakType");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineStyleSection() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line style of the section line of new views, as an index into the\n"
"lines of the active line standard, counted from 0. Applies to\n"
"views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getLineStyleSection() {
    return instance()->LineStyleSection;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultLineStyleSection() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineStyleSection(const long &v) {
    instance()->subHandles[1]->SetInt("LineStyleSection",v);
    instance()->LineStyleSection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineStyleSection() {
    instance()->subHandles[1]->RemoveInt("LineStyleSection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docHighlightStyle() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line style of the detail highlight in new views, as the number of\n"
"a line of the active line standard. Applies to views created\n"
"afterwards. The Detail Highlight Style list of the Annotation page\n"
"stores another key, which nothing that draws reads.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getHighlightStyle() {
    return instance()->HighlightStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultHighlightStyle() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setHighlightStyle(const long &v) {
    instance()->subHandles[1]->SetInt("HighlightStyle",v);
    instance()->HighlightStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeHighlightStyle() {
    instance()->subHandles[1]->RemoveInt("HighlightStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docCenterLine() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Pen style of centre line items: 1 solid, 2 dashed, 3 dotted, 4\n"
"dash-dot, 5 dash-dot-dot. Read as each centre line item is made.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getCenterLine() {
    return instance()->CenterLine;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultCenterLine() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setCenterLine(const long &v) {
    instance()->subHandles[1]->SetInt("CenterLine",v);
    instance()->CenterLine = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeCenterLine() {
    instance()->subHandles[1]->RemoveInt("CenterLine");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docCenterLineStyle() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line style new cosmetic edges and centre lines start with, as the\n"
"number of a line of the active line standard. Applies to lines\n"
"created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getCenterLineStyle() {
    return instance()->CenterLineStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultCenterLineStyle() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setCenterLineStyle(const long &v) {
    instance()->subHandles[1]->SetInt("CenterLineStyle",v);
    instance()->CenterLineStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeCenterLineStyle() {
    instance()->subHandles[1]->RemoveInt("CenterLineStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docHiddenLine() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Pen style of an edge marked hidden: 0 solid, 1 dashed, 2 dotted, 3\n"
"dash-dot, 4 dash-dot-dot. Read when an edge is marked hidden.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getHiddenLine() {
    return instance()->HiddenLine;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultHiddenLine() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setHiddenLine(const long &v) {
    instance()->subHandles[0]->SetInt("HiddenLine",v);
    instance()->HiddenLine = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeHiddenLine() {
    instance()->subHandles[0]->RemoveInt("HiddenLine");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docEdgeCapStyle() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Shape of line ends: 0 round, 1 square, 2 flat. Flat or square ends\n"
"suit drawings printed 1:1 as cutting guides. Read as pens are\n"
"made.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getEdgeCapStyle() {
    return instance()->EdgeCapStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultEdgeCapStyle() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setEdgeCapStyle(const long &v) {
    instance()->subHandles[0]->SetInt("EdgeCapStyle",v);
    instance()->EdgeCapStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeEdgeCapStyle() {
    instance()->subHandles[0]->RemoveInt("EdgeCapStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineStandard() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Line standard in use, as an index into the standards found in the\n"
"line definition folder. Read each time line definitions are\n"
"loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getLineStandard() {
    return instance()->LineStandard;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultLineStandard() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineStandard(const long &v) {
    instance()->subHandles[11]->SetInt("LineStandard",v);
    instance()->LineStandard = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineStandard() {
    instance()->subHandles[11]->RemoveInt("LineStandard");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSectionLineStandard() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Where the arrows and letters of a section line sit: 0 as ANSI and\n"
"ASME have them, 1 as ISO has them. Read each time a section line\n"
"is drawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getSectionLineStandard() {
    return instance()->SectionLineStandard;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultSectionLineStandard() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSectionLineStandard(const long &v) {
    instance()->subHandles[11]->SetInt("SectionLineStandard",v);
    instance()->SectionLineStandard = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSectionLineStandard() {
    instance()->subHandles[11]->RemoveInt("SectionLineStandard");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docIsoCount() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Number of iso-parameter lines per face in new views. Applies to\n"
"views created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getIsoCount() {
    return instance()->IsoCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultIsoCount() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setIsoCount(const long &v) {
    instance()->subHandles[3]->SetInt("IsoCount",v);
    instance()->IsoCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeIsoCount() {
    instance()->subHandles[3]->RemoveInt("IsoCount");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docCoarseView() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"New views use the fast polygon approximation for hidden lines:\n"
"quicker, but curves become short straight segments and faces are\n"
"not found. Applies to views created afterwards; each view has its\n"
"own Coarse View property.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getCoarseView() {
    return instance()->CoarseView;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultCoarseView() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setCoarseView(const bool &v) {
    instance()->subHandles[0]->SetBool("CoarseView",v);
    instance()->CoarseView = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeCoarseView() {
    instance()->subHandles[0]->RemoveBool("CoarseView");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineSpacingFactorISO() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Space between the dimension line and the text of new ISO\n"
"dimensions, as a multiple of the line width. Applies to dimensions\n"
"created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getLineSpacingFactorISO() {
    return instance()->LineSpacingFactorISO;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultLineSpacingFactorISO() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineSpacingFactorISO(const double &v) {
    instance()->subHandles[2]->SetFloat("LineSpacingFactorISO",v);
    instance()->LineSpacingFactorISO = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineSpacingFactorISO() {
    instance()->subHandles[2]->RemoveFloat("LineSpacingFactorISO");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSectionUpdateDelay() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Time in milliseconds between a change in the section view dialog\n"
"and the update of the section while live update is on. At least\n"
"100.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getSectionUpdateDelay() {
    return instance()->SectionUpdateDelay;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultSectionUpdateDelay() {
    const static long def = 300;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSectionUpdateDelay(const long &v) {
    instance()->subHandles[0]->SetInt("SectionUpdateDelay",v);
    instance()->SectionUpdateDelay = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSectionUpdateDelay() {
    instance()->subHandles[0]->RemoveInt("SectionUpdateDelay");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docArrowSize() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Size in mm of dimension arrowheads. Applies to dimensions created\n"
"afterwards; each dimension has its own Arrow Size property.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getArrowSize() {
    return instance()->ArrowSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultArrowSize() {
    const static double def = 3.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setArrowSize(const double &v) {
    instance()->subHandles[2]->SetFloat("ArrowSize",v);
    instance()->ArrowSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeArrowSize() {
    instance()->subHandles[2]->RemoveFloat("ArrowSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docFontSize() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Text size in mm of dimensions and of other annotation text that\n"
"follows it. Applies to dimensions created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & TechDrawParams::getFontSize() {
    return instance()->FontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & TechDrawParams::defaultFontSize() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setFontSize(const double &v) {
    instance()->subHandles[2]->SetFloat("FontSize",v);
    instance()->FontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeFontSize() {
    instance()->subHandles[2]->RemoveFloat("FontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docDiameterSymbol() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Character put in front of diameter dimensions; the diameter sign\n"
"unless another is given. Takes effect when dimensions are\n"
"recomputed.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getDiameterSymbol() {
    return instance()->DiameterSymbol;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultDiameterSymbol() {
    const static std::string def = "\342\214\200";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setDiameterSymbol(const std::string &v) {
    instance()->subHandles[2]->SetASCII("DiameterSymbol",v);
    instance()->DiameterSymbol = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeDiameterSymbol() {
    instance()->subHandles[2]->RemoveASCII("DiameterSymbol");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docStandardAndStyle() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Standard and text placement of new dimensions: 0 ISO oriented, 1\n"
"ISO referencing, 2 ASME inlined, 3 ASME referencing. Applies to\n"
"dimensions created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & TechDrawParams::getStandardAndStyle() {
    return instance()->StandardAndStyle;
}

// Auto generated code (Tools/params_utils.py:413)
const long & TechDrawParams::defaultStandardAndStyle() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setStandardAndStyle(const long &v) {
    instance()->subHandles[2]->SetInt("StandardAndStyle",v);
    instance()->StandardAndStyle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeStandardAndStyle() {
    instance()->subHandles[2]->RemoveInt("StandardAndStyle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docTemplateFile() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Template of a new page. Empty uses A4_LandscapeTD.svg, supplied\n"
"with the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getTemplateFile() {
    return instance()->TemplateFile;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultTemplateFile() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setTemplateFile(const std::string &v) {
    instance()->subHandles[12]->SetASCII("TemplateFile",v);
    instance()->TemplateFile = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeTemplateFile() {
    instance()->subHandles[12]->RemoveASCII("TemplateFile");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docTemplateDir() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Folder the template chooser opens in. Empty uses the one supplied\n"
"with the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getTemplateDir() {
    return instance()->TemplateDir;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultTemplateDir() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setTemplateDir(const std::string &v) {
    instance()->subHandles[12]->SetASCII("TemplateDir",v);
    instance()->TemplateDir = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeTemplateDir() {
    instance()->subHandles[12]->RemoveASCII("TemplateDir");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineGroupFile() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"File of line groups -- sets of line widths. Empty uses the one\n"
"supplied with the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getLineGroupFile() {
    return instance()->LineGroupFile;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultLineGroupFile() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineGroupFile(const std::string &v) {
    instance()->subHandles[12]->SetASCII("LineGroupFile",v);
    instance()->LineGroupFile = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineGroupFile() {
    instance()->subHandles[12]->RemoveASCII("LineGroupFile");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docFileHatch() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"SVG file new hatches take their pattern from. Empty uses\n"
"simple.svg, supplied with the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getFileHatch() {
    return instance()->FileHatch;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultFileHatch() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setFileHatch(const std::string &v) {
    instance()->subHandles[12]->SetASCII("FileHatch",v);
    instance()->FileHatch = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeFileHatch() {
    instance()->subHandles[12]->RemoveASCII("FileHatch");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docWeldingDir() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Folder of welding symbols. Empty uses the AWS symbols supplied\n"
"with the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getWeldingDir() {
    return instance()->WeldingDir;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultWeldingDir() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setWeldingDir(const std::string &v) {
    instance()->subHandles[12]->SetASCII("WeldingDir",v);
    instance()->WeldingDir = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeWeldingDir() {
    instance()->subHandles[12]->RemoveASCII("WeldingDir");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineDefLocation() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Folder of the line standard definitions. Empty uses the one\n"
"supplied with the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getLineDefLocation() {
    return instance()->LineDefLocation;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultLineDefLocation() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineDefLocation(const std::string &v) {
    instance()->subHandles[12]->SetASCII("LineDefLocation",v);
    instance()->LineDefLocation = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineDefLocation() {
    instance()->subHandles[12]->RemoveASCII("LineDefLocation");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docLineElementLocation() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Folder of the line element definitions of the line standards.\n"
"Empty uses the one supplied with the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getLineElementLocation() {
    return instance()->LineElementLocation;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultLineElementLocation() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setLineElementLocation(const std::string &v) {
    instance()->subHandles[12]->SetASCII("LineElementLocation",v);
    instance()->LineElementLocation = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeLineElementLocation() {
    instance()->subHandles[12]->RemoveASCII("LineElementLocation");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docFilePattern() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"PAT file new geometric hatches take their pattern from. Empty uses\n"
"FCPAT.pat, supplied with the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & TechDrawParams::getFilePattern() {
    return instance()->FilePattern;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & TechDrawParams::defaultFilePattern() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setFilePattern(const std::string &v) {
    instance()->subHandles[4]->SetASCII("FilePattern",v);
    instance()->FilePattern = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeFilePattern() {
    instance()->subHandles[4]->RemoveASCII("FilePattern");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docTileColor() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Colour the tiles of a welding symbol are drawn in. On no page.\n"
"Takes effect when a tile is next drawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & TechDrawParams::getTileColor() {
    return instance()->TileColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & TechDrawParams::defaultTileColor() {
    const static unsigned long def = 0x000000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setTileColor(const unsigned long &v) {
    instance()->subHandles[5]->SetUnsigned("TileColor",v);
    instance()->TileColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeTileColor() {
    instance()->subHandles[5]->RemoveUnsigned("TileColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *TechDrawParams::docSectionLiveUpdate() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"'Live update' of the section view task was last checked: the\n"
"section follows each change in the task at once. Stored when the\n"
"box is clicked.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & TechDrawParams::getSectionLiveUpdate() {
    return instance()->SectionLiveUpdate;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & TechDrawParams::defaultSectionLiveUpdate() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void TechDrawParams::setSectionLiveUpdate(const bool &v) {
    instance()->subHandles[0]->SetBool("SectionLiveUpdate",v);
    instance()->SectionLiveUpdate = v;
}

// Auto generated code (Tools/params_utils.py:431)
void TechDrawParams::removeSectionLiveUpdate() {
    instance()->subHandles[0]->RemoveBool("SectionLiveUpdate");
}
//[[[end]]]
