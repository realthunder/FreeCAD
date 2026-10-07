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

    // Auto generated code (Tools/params_utils.py:254)
    TechDrawParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/TechDraw");
        handle->Attach(this);

        subHandles.resize(1);
        subHandles[0] = handle->GetGroup("General");
        subHandles[0]->Attach(this);
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
    App::ParamInfo("TechDraw", "TechDrawParams", "User parameter:BaseApp/Preferences/Mod/TechDraw/General", "ProjectionAngle", "ProjectionAngle", App::ParamInfo::Int, 0)
        .setTitle("Projection Angle")
        .setDoc("Projection convention of new pages and projection groups: 0 first\n"
"angle, 1 third angle. The third choice on the page, Page,\n"
"currently reads as first angle."),
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
const char *TechDrawParams::docProjectionAngle() {
    return QT_TRANSLATE_NOOP("TechDrawParams",
"Projection convention of new pages and projection groups: 0 first\n"
"angle, 1 third angle. The third choice on the page, Page,\n"
"currently reads as first angle.");
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
//[[[end]]]
