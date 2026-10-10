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
#include <QTimer>
#include <App/Application.h>
#include <App/Document.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/RenderParams.h>
#include <Gui/ViewParams.h>
#include <Gui/Renderer/Renderer.h>
#include "ViewProvider.h"

namespace PartGui {
// TaskDimension.h, which wants the viewer's header with it
void refreshDimensions();
}

namespace {
/// Whether a tessellation preference changed since the last reload. Only
/// then are the preferences written into every object's Deviation and
/// AngularDeflection: the same timer also answers the shape-instancing
/// gate -- a view's renderer attaching or going away -- and taking that
/// for a preference change overwrote the per-object values a file was
/// saved with each time a document opened a view.
bool tessellationChanged;

QTimer &getTimer() {
    static QTimer *timer;
    if (!timer) {
        timer = new QTimer();
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, [](){
            const bool applyTessellation = tessellationChanged;
            tessellationChanged = false;
            // search for Part view providers and apply the new settings
            for (auto doc : App::GetApplication().getDocuments()) {
                auto gdoc = Gui::Application::Instance->getDocument(doc);
                for (auto vp : gdoc->getViewProvidersOfType(
                            PartGui::ViewProviderPart::getClassTypeId()))
                    static_cast<PartGui::ViewProviderPart*>(vp)->reload(applyTessellation);
            }
        });
    }
    return *timer;
}

void tessellationParamChanged() {
    tessellationChanged = true;
    getTimer().start(100);
}

// The measurements on screen are built with the colours and the font of the
// moment; a change of one rebuilds them, once for however many changed.
void dimensionParamChanged() {
    static QTimer *timer;
    if (!timer) {
        timer = new QTimer();
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, [](){
            PartGui::refreshDimensions();
        });
    }
    timer->start(100);
}
} // anonymous namespace

/*[[[cog
import PartGuiParams
PartGuiParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "PartParams.h"
using namespace PartGui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class PartParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(PartParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    bool NormalsFromUVNodes;
    bool TwoSideRendering;
    double MinimumDeviation;
    double MeshDeviation;
    double MeshAngularDeflection;
    double MinimumAngularDeflection;
    bool OverrideTessellation;
    bool MapFaceColor;
    bool MapLineColor;
    bool MapPointColor;
    bool MapTransparency;
    bool AutoGridScale;
    unsigned long PreviewAddColor;
    unsigned long PreviewSubColor;
    unsigned long PreviewDressColor;
    unsigned long PreviewIntersectColor;
    bool PreviewOnEdit;
    bool PreviewWithTransparency;
    bool EditOnTop;
    long EditRecomputeWait;
    bool AdjustCameraForNewFeature;
    unsigned long DefaultDatumColor;
    unsigned long DefaultDatumLineColor;
    bool RespectSystemDPI;
    bool ShapeInstancing;
    long SelectionPickThreshold;
    long SelectionPickThreshold2;
    bool SelectionPickRTree;
    unsigned long Dimensions3dColor;
    unsigned long DimensionsDeltaColor;
    unsigned long DimensionsAngularColor;
    long DimensionsFontSize;
    std::string DimensionsFontName;
    bool DimensionsFontStyleBold;
    bool DimensionsFontStyleItalic;
    bool CheckGeometryAutoRun;
    bool CheckGeometryRunBOPCheck;
    bool CheckGeometryRunSingleThreaded;
    bool CheckGeometryLogErrors;
    bool CheckGeometryExpandShapeContent;
    bool CheckGeometryAdvancedShapeContent;
    bool CheckGeometryArgumentTypeMode;
    bool CheckGeometrySelfInterMode;
    bool CheckGeometrySmallEdgeMode;
    bool CheckGeometryRebuildFaceMode;
    bool CheckGeometryContinuityMode;
    bool CheckGeometryTangentMode;
    bool CheckGeometryMergeVertexMode;
    bool CheckGeometryMergeEdgeMode;
    bool CheckGeometryCurveOnSurfaceMode;
    bool ParametricRefine;
    bool AddBaseObjectName;

    // Auto generated code (Tools/params_utils.py:254)
    PartParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Part");
        handle->Attach(this);

        subHandles.resize(1);
        subHandles[0] = handle->GetGroup("CheckGeometry");
        subHandles[0]->Attach(this);
        NormalsFromUVNodes = this->handle->GetBool("NormalsFromUVNodes", true);
        funcs["NormalsFromUVNodes"] = &PartParamsP::updateNormalsFromUVNodes;
        TwoSideRendering = this->handle->GetBool("TwoSideRendering", true);
        funcs["TwoSideRendering"] = &PartParamsP::updateTwoSideRendering;
        MinimumDeviation = this->handle->GetFloat("MinimumDeviation", 0.05);
        funcs["MinimumDeviation"] = &PartParamsP::updateMinimumDeviation;
        MeshDeviation = this->handle->GetFloat("MeshDeviation", 0.2);
        funcs["MeshDeviation"] = &PartParamsP::updateMeshDeviation;
        MeshAngularDeflection = this->handle->GetFloat("MeshAngularDeflection", 28.65);
        funcs["MeshAngularDeflection"] = &PartParamsP::updateMeshAngularDeflection;
        MinimumAngularDeflection = this->handle->GetFloat("MinimumAngularDeflection", 5.0);
        funcs["MinimumAngularDeflection"] = &PartParamsP::updateMinimumAngularDeflection;
        OverrideTessellation = this->handle->GetBool("OverrideTessellation", false);
        funcs["OverrideTessellation"] = &PartParamsP::updateOverrideTessellation;
        MapFaceColor = this->handle->GetBool("MapFaceColor", true);
        funcs["MapFaceColor"] = &PartParamsP::updateMapFaceColor;
        MapLineColor = this->handle->GetBool("MapLineColor", false);
        funcs["MapLineColor"] = &PartParamsP::updateMapLineColor;
        MapPointColor = this->handle->GetBool("MapPointColor", false);
        funcs["MapPointColor"] = &PartParamsP::updateMapPointColor;
        MapTransparency = this->handle->GetBool("MapTransparency", false);
        funcs["MapTransparency"] = &PartParamsP::updateMapTransparency;
        AutoGridScale = this->handle->GetBool("AutoGridScale", false);
        funcs["AutoGridScale"] = &PartParamsP::updateAutoGridScale;
        PreviewAddColor = this->handle->GetUnsigned("PreviewAddColor", 0x64FFFF30);
        funcs["PreviewAddColor"] = &PartParamsP::updatePreviewAddColor;
        PreviewSubColor = this->handle->GetUnsigned("PreviewSubColor", 0xFF646430);
        funcs["PreviewSubColor"] = &PartParamsP::updatePreviewSubColor;
        PreviewDressColor = this->handle->GetUnsigned("PreviewDressColor", 0xFF64FF30);
        funcs["PreviewDressColor"] = &PartParamsP::updatePreviewDressColor;
        PreviewIntersectColor = this->handle->GetUnsigned("PreviewIntersectColor", 0x6464FF30);
        funcs["PreviewIntersectColor"] = &PartParamsP::updatePreviewIntersectColor;
        PreviewOnEdit = this->handle->GetBool("PreviewOnEdit", true);
        funcs["PreviewOnEdit"] = &PartParamsP::updatePreviewOnEdit;
        PreviewWithTransparency = this->handle->GetBool("PreviewWithTransparency", true);
        funcs["PreviewWithTransparency"] = &PartParamsP::updatePreviewWithTransparency;
        EditOnTop = this->handle->GetBool("EditOnTop", false);
        funcs["EditOnTop"] = &PartParamsP::updateEditOnTop;
        EditRecomputeWait = this->handle->GetInt("EditRecomputeWait", 300);
        funcs["EditRecomputeWait"] = &PartParamsP::updateEditRecomputeWait;
        AdjustCameraForNewFeature = this->handle->GetBool("AdjustCameraForNewFeature", true);
        funcs["AdjustCameraForNewFeature"] = &PartParamsP::updateAdjustCameraForNewFeature;
        DefaultDatumColor = this->handle->GetUnsigned("DefaultDatumColor", 0xFFD70066);
        funcs["DefaultDatumColor"] = &PartParamsP::updateDefaultDatumColor;
        DefaultDatumLineColor = this->handle->GetUnsigned("DefaultDatumLineColor", 0xFA9600FF);
        funcs["DefaultDatumLineColor"] = &PartParamsP::updateDefaultDatumLineColor;
        RespectSystemDPI = this->handle->GetBool("RespectSystemDPI", false);
        funcs["RespectSystemDPI"] = &PartParamsP::updateRespectSystemDPI;
        ShapeInstancing = this->handle->GetBool("ShapeInstancing", true);
        funcs["ShapeInstancing"] = &PartParamsP::updateShapeInstancing;
        SelectionPickThreshold = this->handle->GetInt("SelectionPickThreshold", 1000);
        funcs["SelectionPickThreshold"] = &PartParamsP::updateSelectionPickThreshold;
        SelectionPickThreshold2 = this->handle->GetInt("SelectionPickThreshold2", 500);
        funcs["SelectionPickThreshold2"] = &PartParamsP::updateSelectionPickThreshold2;
        SelectionPickRTree = this->handle->GetBool("SelectionPickRTree", true);
        funcs["SelectionPickRTree"] = &PartParamsP::updateSelectionPickRTree;
        Dimensions3dColor = this->handle->GetUnsigned("Dimensions3dColor", 0xFF0000FF);
        funcs["Dimensions3dColor"] = &PartParamsP::updateDimensions3dColor;
        DimensionsDeltaColor = this->handle->GetUnsigned("DimensionsDeltaColor", 0x00FF00FF);
        funcs["DimensionsDeltaColor"] = &PartParamsP::updateDimensionsDeltaColor;
        DimensionsAngularColor = this->handle->GetUnsigned("DimensionsAngularColor", 0x0000FFFF);
        funcs["DimensionsAngularColor"] = &PartParamsP::updateDimensionsAngularColor;
        DimensionsFontSize = this->handle->GetInt("DimensionsFontSize", 30);
        funcs["DimensionsFontSize"] = &PartParamsP::updateDimensionsFontSize;
        DimensionsFontName = this->handle->GetASCII("DimensionsFontName", "defaultFont");
        funcs["DimensionsFontName"] = &PartParamsP::updateDimensionsFontName;
        DimensionsFontStyleBold = this->handle->GetBool("DimensionsFontStyleBold", false);
        funcs["DimensionsFontStyleBold"] = &PartParamsP::updateDimensionsFontStyleBold;
        DimensionsFontStyleItalic = this->handle->GetBool("DimensionsFontStyleItalic", false);
        funcs["DimensionsFontStyleItalic"] = &PartParamsP::updateDimensionsFontStyleItalic;
        CheckGeometryAutoRun = this->subHandles[0]->GetBool("AutoRun", false);
        funcs["AutoRun"] = &PartParamsP::updateCheckGeometryAutoRun;
        CheckGeometryRunBOPCheck = this->subHandles[0]->GetBool("RunBOPCheck", false);
        funcs["RunBOPCheck"] = &PartParamsP::updateCheckGeometryRunBOPCheck;
        CheckGeometryRunSingleThreaded = this->subHandles[0]->GetBool("RunSingleThreaded", false);
        funcs["RunSingleThreaded"] = &PartParamsP::updateCheckGeometryRunSingleThreaded;
        CheckGeometryLogErrors = this->subHandles[0]->GetBool("LogErrors", true);
        funcs["LogErrors"] = &PartParamsP::updateCheckGeometryLogErrors;
        CheckGeometryExpandShapeContent = this->subHandles[0]->GetBool("ExpandShapeContent", false);
        funcs["ExpandShapeContent"] = &PartParamsP::updateCheckGeometryExpandShapeContent;
        CheckGeometryAdvancedShapeContent = this->subHandles[0]->GetBool("AdvancedShapeContent", true);
        funcs["AdvancedShapeContent"] = &PartParamsP::updateCheckGeometryAdvancedShapeContent;
        CheckGeometryArgumentTypeMode = this->subHandles[0]->GetBool("ArgumentTypeMode", true);
        funcs["ArgumentTypeMode"] = &PartParamsP::updateCheckGeometryArgumentTypeMode;
        CheckGeometrySelfInterMode = this->subHandles[0]->GetBool("SelfInterMode", true);
        funcs["SelfInterMode"] = &PartParamsP::updateCheckGeometrySelfInterMode;
        CheckGeometrySmallEdgeMode = this->subHandles[0]->GetBool("SmallEdgeMode", true);
        funcs["SmallEdgeMode"] = &PartParamsP::updateCheckGeometrySmallEdgeMode;
        CheckGeometryRebuildFaceMode = this->subHandles[0]->GetBool("RebuildFaceMode", true);
        funcs["RebuildFaceMode"] = &PartParamsP::updateCheckGeometryRebuildFaceMode;
        CheckGeometryContinuityMode = this->subHandles[0]->GetBool("ContinuityMode", true);
        funcs["ContinuityMode"] = &PartParamsP::updateCheckGeometryContinuityMode;
        CheckGeometryTangentMode = this->subHandles[0]->GetBool("TangentMode", true);
        funcs["TangentMode"] = &PartParamsP::updateCheckGeometryTangentMode;
        CheckGeometryMergeVertexMode = this->subHandles[0]->GetBool("MergeVertexMode", true);
        funcs["MergeVertexMode"] = &PartParamsP::updateCheckGeometryMergeVertexMode;
        CheckGeometryMergeEdgeMode = this->subHandles[0]->GetBool("MergeEdgeMode", true);
        funcs["MergeEdgeMode"] = &PartParamsP::updateCheckGeometryMergeEdgeMode;
        CheckGeometryCurveOnSurfaceMode = this->subHandles[0]->GetBool("CurveOnSurfaceMode", true);
        funcs["CurveOnSurfaceMode"] = &PartParamsP::updateCheckGeometryCurveOnSurfaceMode;
        ParametricRefine = this->handle->GetBool("ParametricRefine", true);
        funcs["ParametricRefine"] = &PartParamsP::updateParametricRefine;
        AddBaseObjectName = this->handle->GetBool("AddBaseObjectName", false);
        funcs["AddBaseObjectName"] = &PartParamsP::updateAddBaseObjectName;
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
    static void updateNormalsFromUVNodes(PartParamsP *self) {
        self->NormalsFromUVNodes = self->handle->GetBool("NormalsFromUVNodes", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTwoSideRendering(PartParamsP *self) {
        self->TwoSideRendering = self->handle->GetBool("TwoSideRendering", true);
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateMinimumDeviation(PartParamsP *self) {
        auto v = self->handle->GetFloat("MinimumDeviation", 0.05);
        if (self->MinimumDeviation != v) {
            self->MinimumDeviation = v;
            PartParams::onMinimumDeviationChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateMeshDeviation(PartParamsP *self) {
        auto v = self->handle->GetFloat("MeshDeviation", 0.2);
        if (self->MeshDeviation != v) {
            self->MeshDeviation = v;
            PartParams::onMeshDeviationChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateMeshAngularDeflection(PartParamsP *self) {
        auto v = self->handle->GetFloat("MeshAngularDeflection", 28.65);
        if (self->MeshAngularDeflection != v) {
            self->MeshAngularDeflection = v;
            PartParams::onMeshAngularDeflectionChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateMinimumAngularDeflection(PartParamsP *self) {
        auto v = self->handle->GetFloat("MinimumAngularDeflection", 5.0);
        if (self->MinimumAngularDeflection != v) {
            self->MinimumAngularDeflection = v;
            PartParams::onMinimumAngularDeflectionChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateOverrideTessellation(PartParamsP *self) {
        auto v = self->handle->GetBool("OverrideTessellation", false);
        if (self->OverrideTessellation != v) {
            self->OverrideTessellation = v;
            PartParams::onOverrideTessellationChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMapFaceColor(PartParamsP *self) {
        self->MapFaceColor = self->handle->GetBool("MapFaceColor", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMapLineColor(PartParamsP *self) {
        self->MapLineColor = self->handle->GetBool("MapLineColor", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMapPointColor(PartParamsP *self) {
        self->MapPointColor = self->handle->GetBool("MapPointColor", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMapTransparency(PartParamsP *self) {
        self->MapTransparency = self->handle->GetBool("MapTransparency", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoGridScale(PartParamsP *self) {
        self->AutoGridScale = self->handle->GetBool("AutoGridScale", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreviewAddColor(PartParamsP *self) {
        self->PreviewAddColor = self->handle->GetUnsigned("PreviewAddColor", 0x64FFFF30);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreviewSubColor(PartParamsP *self) {
        self->PreviewSubColor = self->handle->GetUnsigned("PreviewSubColor", 0xFF646430);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreviewDressColor(PartParamsP *self) {
        self->PreviewDressColor = self->handle->GetUnsigned("PreviewDressColor", 0xFF64FF30);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreviewIntersectColor(PartParamsP *self) {
        self->PreviewIntersectColor = self->handle->GetUnsigned("PreviewIntersectColor", 0x6464FF30);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreviewOnEdit(PartParamsP *self) {
        self->PreviewOnEdit = self->handle->GetBool("PreviewOnEdit", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreviewWithTransparency(PartParamsP *self) {
        self->PreviewWithTransparency = self->handle->GetBool("PreviewWithTransparency", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditOnTop(PartParamsP *self) {
        self->EditOnTop = self->handle->GetBool("EditOnTop", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEditRecomputeWait(PartParamsP *self) {
        self->EditRecomputeWait = self->handle->GetInt("EditRecomputeWait", 300);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAdjustCameraForNewFeature(PartParamsP *self) {
        self->AdjustCameraForNewFeature = self->handle->GetBool("AdjustCameraForNewFeature", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultDatumColor(PartParamsP *self) {
        self->DefaultDatumColor = self->handle->GetUnsigned("DefaultDatumColor", 0xFFD70066);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultDatumLineColor(PartParamsP *self) {
        self->DefaultDatumLineColor = self->handle->GetUnsigned("DefaultDatumLineColor", 0xFA9600FF);
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateRespectSystemDPI(PartParamsP *self) {
        auto v = self->handle->GetBool("RespectSystemDPI", false);
        if (self->RespectSystemDPI != v) {
            self->RespectSystemDPI = v;
            PartParams::onRespectSystemDPIChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateShapeInstancing(PartParamsP *self) {
        auto v = self->handle->GetBool("ShapeInstancing", true);
        if (self->ShapeInstancing != v) {
            self->ShapeInstancing = v;
            PartParams::onShapeInstancingChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionPickThreshold(PartParamsP *self) {
        self->SelectionPickThreshold = self->handle->GetInt("SelectionPickThreshold", 1000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionPickThreshold2(PartParamsP *self) {
        self->SelectionPickThreshold2 = self->handle->GetInt("SelectionPickThreshold2", 500);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSelectionPickRTree(PartParamsP *self) {
        self->SelectionPickRTree = self->handle->GetBool("SelectionPickRTree", true);
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateDimensions3dColor(PartParamsP *self) {
        auto v = self->handle->GetUnsigned("Dimensions3dColor", 0xFF0000FF);
        if (self->Dimensions3dColor != v) {
            self->Dimensions3dColor = v;
            PartParams::onDimensions3dColorChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateDimensionsDeltaColor(PartParamsP *self) {
        auto v = self->handle->GetUnsigned("DimensionsDeltaColor", 0x00FF00FF);
        if (self->DimensionsDeltaColor != v) {
            self->DimensionsDeltaColor = v;
            PartParams::onDimensionsDeltaColorChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateDimensionsAngularColor(PartParamsP *self) {
        auto v = self->handle->GetUnsigned("DimensionsAngularColor", 0x0000FFFF);
        if (self->DimensionsAngularColor != v) {
            self->DimensionsAngularColor = v;
            PartParams::onDimensionsAngularColorChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateDimensionsFontSize(PartParamsP *self) {
        auto v = self->handle->GetInt("DimensionsFontSize", 30);
        if (self->DimensionsFontSize != v) {
            self->DimensionsFontSize = v;
            PartParams::onDimensionsFontSizeChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateDimensionsFontName(PartParamsP *self) {
        auto v = self->handle->GetASCII("DimensionsFontName", "defaultFont");
        if (self->DimensionsFontName != v) {
            self->DimensionsFontName = v;
            PartParams::onDimensionsFontNameChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateDimensionsFontStyleBold(PartParamsP *self) {
        auto v = self->handle->GetBool("DimensionsFontStyleBold", false);
        if (self->DimensionsFontStyleBold != v) {
            self->DimensionsFontStyleBold = v;
            PartParams::onDimensionsFontStyleBoldChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:322)
    static void updateDimensionsFontStyleItalic(PartParamsP *self) {
        auto v = self->handle->GetBool("DimensionsFontStyleItalic", false);
        if (self->DimensionsFontStyleItalic != v) {
            self->DimensionsFontStyleItalic = v;
            PartParams::onDimensionsFontStyleItalicChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryAutoRun(PartParamsP *self) {
        self->CheckGeometryAutoRun = self->subHandles[0]->GetBool("AutoRun", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryRunBOPCheck(PartParamsP *self) {
        self->CheckGeometryRunBOPCheck = self->subHandles[0]->GetBool("RunBOPCheck", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryRunSingleThreaded(PartParamsP *self) {
        self->CheckGeometryRunSingleThreaded = self->subHandles[0]->GetBool("RunSingleThreaded", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryLogErrors(PartParamsP *self) {
        self->CheckGeometryLogErrors = self->subHandles[0]->GetBool("LogErrors", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryExpandShapeContent(PartParamsP *self) {
        self->CheckGeometryExpandShapeContent = self->subHandles[0]->GetBool("ExpandShapeContent", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryAdvancedShapeContent(PartParamsP *self) {
        self->CheckGeometryAdvancedShapeContent = self->subHandles[0]->GetBool("AdvancedShapeContent", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryArgumentTypeMode(PartParamsP *self) {
        self->CheckGeometryArgumentTypeMode = self->subHandles[0]->GetBool("ArgumentTypeMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometrySelfInterMode(PartParamsP *self) {
        self->CheckGeometrySelfInterMode = self->subHandles[0]->GetBool("SelfInterMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometrySmallEdgeMode(PartParamsP *self) {
        self->CheckGeometrySmallEdgeMode = self->subHandles[0]->GetBool("SmallEdgeMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryRebuildFaceMode(PartParamsP *self) {
        self->CheckGeometryRebuildFaceMode = self->subHandles[0]->GetBool("RebuildFaceMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryContinuityMode(PartParamsP *self) {
        self->CheckGeometryContinuityMode = self->subHandles[0]->GetBool("ContinuityMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryTangentMode(PartParamsP *self) {
        self->CheckGeometryTangentMode = self->subHandles[0]->GetBool("TangentMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryMergeVertexMode(PartParamsP *self) {
        self->CheckGeometryMergeVertexMode = self->subHandles[0]->GetBool("MergeVertexMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryMergeEdgeMode(PartParamsP *self) {
        self->CheckGeometryMergeEdgeMode = self->subHandles[0]->GetBool("MergeEdgeMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckGeometryCurveOnSurfaceMode(PartParamsP *self) {
        self->CheckGeometryCurveOnSurfaceMode = self->subHandles[0]->GetBool("CurveOnSurfaceMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateParametricRefine(PartParamsP *self) {
        self->ParametricRefine = self->handle->GetBool("ParametricRefine", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAddBaseObjectName(PartParamsP *self) {
        self->AddBaseObjectName = self->handle->GetBool("AddBaseObjectName", false);
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
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "NormalsFromUVNodes", "NormalsFromUVNodes", App::ParamInfo::Bool, true)
        .setTitle("Normals From UV Nodes")
        .setDoc("Take the shading normals of a shape from its exact surface instead\n"
"of from the display triangles. Gives smoother shading of curved\n"
"faces."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "TwoSideRendering", "TwoSideRendering", App::ParamInfo::Bool, true)
        .setTitle("Two Side Rendering")
        .setDoc("Light new shapes from both sides, so the back of a face looks like\n"
"the front. When off the back shows the backlight colour or black."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MinimumDeviation", "MinimumDeviation", App::ParamInfo::Float, 0.05)
        .setTitle("Minimum Deviation")
        .setDoc("Lower limit of the tessellation deviation of shapes, in percent of\n"
"the object size. Objects asking for a finer mesh are drawn with\n"
"this value instead.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MeshDeviation", "MeshDeviation", App::ParamInfo::Float, 0.2)
        .setTitle("Mesh Deviation")
        .setDoc("Accuracy of the mesh that shapes are drawn with, as the largest\n"
"deviation in percent of the object size. Lower is finer and\n"
"slower. Sets the Deviation of new objects; a change is applied to\n"
"all open objects.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MeshAngularDeflection", "MeshAngularDeflection", App::ParamInfo::Float, 28.65)
        .setTitle("Mesh Angular Deflection")
        .setDoc("Largest angle between neighbouring segments of the mesh that\n"
"shapes are drawn with, in degrees. Lower is smoother and slower.\n"
"Sets the Angular Deflection of new objects; a change is applied to\n"
"all open objects.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MinimumAngularDeflection", "MinimumAngularDeflection", App::ParamInfo::Float, 5.0)
        .setTitle("Minimum Angular Deflection")
        .setDoc("Lower limit of the angular deflection used to mesh shapes, in\n"
"degrees. Objects asking for a smaller angle are drawn with this\n"
"value instead.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "OverrideTessellation", "OverrideTessellation", App::ParamInfo::Bool, false)
        .setTitle("Override Tessellation")
        .setDoc("Draw every shape with the deviation and angular deflection set\n"
"here, ignoring the values stored in each object. When off a change\n"
"of those two settings is written into the open objects instead.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MapFaceColor", "MapFaceColor", App::ParamInfo::Bool, true)
        .setTitle("Map Face Color")
        .setDoc("Let new shapes take their face colours from the shapes they were\n"
"made from. Turn off to give all faces of an object one colour."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MapLineColor", "MapLineColor", App::ParamInfo::Bool, false)
        .setTitle("Map Line Color")
        .setDoc("Let new shapes take their edge colours from the shapes they were\n"
"made from. Turn off to give all edges of an object one colour."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MapPointColor", "MapPointColor", App::ParamInfo::Bool, false)
        .setTitle("Map Point Color")
        .setDoc("Let new shapes take their vertex colours from the shapes they were\n"
"made from. Turn off to give all vertices of an object one colour."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "MapTransparency", "MapTransparency", App::ParamInfo::Bool, false)
        .setTitle("Map Transparency")
        .setDoc("Let new shapes take the transparency of their faces from the\n"
"shapes they were made from. Turn off for one transparency per\n"
"object."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "AutoGridScale", "AutoGridScale", App::ParamInfo::Bool, false)
        .setTitle("Auto Grid Scale")
        .setDoc("Double or halve the grid size of a sketch being edited as the view\n"
"is zoomed, so that the grid keeps a similar spacing on screen."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "PreviewAddColor", "PreviewAddColor", App::ParamInfo::Hex, 0x64FFFF30)
        .setTitle("Preview Add Color")
        .setDoc("Colour of the preview of a feature that adds material. Its alpha\n"
"part sets how transparent the preview is.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "PreviewSubColor", "PreviewSubColor", App::ParamInfo::Hex, 0xFF646430)
        .setTitle("Preview Sub Color")
        .setDoc("Colour of the preview of a feature that removes material. Its\n"
"alpha part sets how transparent the preview is.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "PreviewDressColor", "PreviewDressColor", App::ParamInfo::Hex, 0xFF64FF30)
        .setTitle("Preview Dress Color")
        .setDoc("Colour of the preview of a dress-up feature such as a fillet or a\n"
"chamfer. Its alpha part sets how transparent the preview is.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "PreviewIntersectColor", "PreviewIntersectColor", App::ParamInfo::Hex, 0x6464FF30)
        .setTitle("Preview Intersect Color")
        .setDoc("Colour of the preview of a feature that keeps what it has in\n"
"common with the body. Its alpha part sets how transparent the\n"
"preview is.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "PreviewOnEdit", "PreviewOnEdit", App::ParamInfo::Bool, true)
        .setTitle("Preview On Edit")
        .setDoc("Show a preview of the result while a PartDesign feature is edited,\n"
"and hold back the recompute of the feature until the preview is\n"
"turned off or the edit ends."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "PreviewWithTransparency", "PreviewWithTransparency", App::ParamInfo::Bool, true)
        .setTitle("Preview With Transparency")
        .setDoc("Draw the preview of an edited PartDesign feature transparent. When\n"
"off it is drawn opaque."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "EditOnTop", "EditOnTop", App::ParamInfo::Bool, false)
        .setTitle("Edit On Top")
        .setDoc("Draw the PartDesign feature being edited on top of everything else\n"
"in the 3D view."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "EditRecomputeWait", "EditRecomputeWait", App::ParamInfo::Int, 300)
        .setTitle("Edit Recompute Wait")
        .setDoc("Delay between a change in a PartDesign task panel and the update\n"
"of the feature, in milliseconds. A third of it is used while the\n"
"preview is shown."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "AdjustCameraForNewFeature", "AdjustCameraForNewFeature", App::ParamInfo::Bool, true)
        .setTitle("Adjust Camera For New Feature")
        .setDoc("Move the camera to bring a newly created feature into view. Used\n"
"by Part offset and thickness, by PartDesign features made from a\n"
"selected profile, and by new bodies."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "DefaultDatumColor", "DefaultDatumColor", App::ParamInfo::Hex, 0xFFD70066)
        .setTitle("Default datum colour")
        .setDoc("Colour and transparency of new datum planes, lines and points, of\n"
"shape binders, of sub-shape binders shown in binder style, and of\n"
"PartDesign extrusions: golden yellow, mostly see-through, unless\n"
"set.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "DefaultDatumLineColor", "DefaultDatumLineColor", App::ParamInfo::Hex, 0xFA9600FF)
        .setTitle("Default Datum Line Color")
        .setDoc("Line and point color of a shape binder, darker than DefaultDatumColor\n"
"so that its outline shows against the model (upstream 5dbb4d7c7e)")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "RespectSystemDPI", "RespectSystemDPI", App::ParamInfo::Bool, false)
        .setTitle("Respect system DPI")
        .setDoc("Scale the line width and point size of shapes by the pixel ratio\n"
"of the display. May look wrong with monitors of different scaling.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "ShapeInstancing", "ShapeInstancing", App::ParamInfo::Bool, true)
        .setTitle("Shape Instancing")
        .setDoc("Share the tessellation of repeated sub-shapes (same TopoDS_TShape)\n"
"inside a compound and render them as GPU instances. Only takes\n"
"effect when the renderer supports instanced draws; otherwise the\n"
"geometry is flattened as before.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "SelectionPickThreshold", "SelectionPickThreshold", App::ParamInfo::Int, 1000)
        .setTitle("Selection Pick Threshold")
        .setDoc("Size of a shape above which picking first narrows the search with\n"
"bounding boxes, counted in face, edge or point indices. Smaller\n"
"shapes are tested whole. 0 or less always tests everything."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "SelectionPickThreshold2", "SelectionPickThreshold2", App::ParamInfo::Int, 500)
        .setTitle("Selection Pick Threshold2")
        .setDoc("Size above which a single face or edge gets a search structure of\n"
"its own for picking: the triangles of a face, the points of an\n"
"edge. 0 or less turns it off for faces."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "SelectionPickRTree", "SelectionPickRTree", App::ParamInfo::Bool, true)
        .setTitle("Selection Pick RTree")
        .setDoc("Pick with a spatial index of a part's triangles instead of testing\n"
"every triangle. Much faster on dense parts; the index is built on the\n"
"first pick that reaches a part. Parts smaller than\n"
"SelectionPickThreshold2 are picked directly."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "Dimensions3dColor", "Dimensions3dColor", App::ParamInfo::Hex, 0xFF0000FF)
        .setTitle("Measurement colour")
        .setDoc("Colour of the direct distance of a Part measurement in the 3D view.\n"
"The measurements shown follow a change.")
        .setOnChange()
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "DimensionsDeltaColor", "DimensionsDeltaColor", App::ParamInfo::Hex, 0x00FF00FF)
        .setTitle("Measurement delta colour")
        .setDoc("Colour of the X, Y and Z components of a Part distance measurement\n"
"in the 3D view. The measurements shown follow a change.")
        .setOnChange()
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "DimensionsAngularColor", "DimensionsAngularColor", App::ParamInfo::Hex, 0x0000FFFF)
        .setTitle("Angle measurement colour")
        .setDoc("Colour of a Part angle measurement in the 3D view. The\n"
"measurements shown follow a change.")
        .setOnChange()
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "DimensionsFontSize", "DimensionsFontSize", App::ParamInfo::Int, 30)
        .setTitle("Measurement font size")
        .setDoc("Size of the text of Part measurements in the 3D view. The\n"
"measurements shown follow a change.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "DimensionsFontName", "DimensionsFontName", App::ParamInfo::String, "defaultFont")
        .setTitle("Measurement font")
        .setDoc("Font of the text of Part measurements in the 3D view; defaultFont\n"
"is the 3D view's own. The measurements shown follow a change.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "DimensionsFontStyleBold", "DimensionsFontStyleBold", App::ParamInfo::Bool, false)
        .setTitle("Measurement font bold")
        .setDoc("Draw the text of Part measurements in the 3D view in bold.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "DimensionsFontStyleItalic", "DimensionsFontStyleItalic", App::ParamInfo::Bool, false)
        .setTitle("Measurement font italic")
        .setDoc("Draw the text of Part measurements in the 3D view in italic.")
        .setOnChange(),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryAutoRun", "AutoRun", App::ParamInfo::Bool, false)
        .setTitle("Run the geometry check at once")
        .setDoc("Run the geometry check as soon as its panel opens, without the Run\n"
"Check button."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryRunBOPCheck", "RunBOPCheck", App::ParamInfo::Bool, false)
        .setTitle("Run the Boolean operation check")
        .setDoc("Run the Boolean operation check on shapes the basic check finds\n"
"valid. It finds more, and can be very slow."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryRunSingleThreaded", "RunSingleThreaded", App::ParamInfo::Bool, false)
        .setTitle("Geometry check in a single thread")
        .setDoc("Run the Boolean operation check of the geometry check in a single\n"
"thread: slower, and more stable."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryLogErrors", "LogErrors", App::ParamInfo::Bool, true)
        .setTitle("Log geometry check errors")
        .setDoc("Write the errors the geometry check finds to the report view."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryExpandShapeContent", "ExpandShapeContent", App::ParamInfo::Bool, false)
        .setTitle("Expand shape content")
        .setDoc("Open the shape content of the geometry check's result when it is\n"
"shown."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryAdvancedShapeContent", "AdvancedShapeContent", App::ParamInfo::Bool, true)
        .setTitle("Advanced shape content")
        .setDoc("Show more about the shape in the geometry check's shape content."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryArgumentTypeMode", "ArgumentTypeMode", App::ParamInfo::Bool, true)
        .setTitle("Check for bad argument types")
        .setDoc("Boolean operation check: look for shapes of a kind the operation\n"
"cannot take."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometrySelfInterMode", "SelfInterMode", App::ParamInfo::Bool, true)
        .setTitle("Check for self-intersections")
        .setDoc("Boolean operation check: look for shapes that intersect themselves."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometrySmallEdgeMode", "SmallEdgeMode", App::ParamInfo::Bool, true)
        .setTitle("Check for small edges")
        .setDoc("Boolean operation check: look for edges that are too small."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryRebuildFaceMode", "RebuildFaceMode", App::ParamInfo::Bool, true)
        .setTitle("Check for faces that cannot be rebuilt")
        .setDoc("Boolean operation check: look for faces that cannot be rebuilt."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryContinuityMode", "ContinuityMode", App::ParamInfo::Bool, true)
        .setTitle("Check for continuity")
        .setDoc("Boolean operation check: look for edges that are not continuous."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryTangentMode", "TangentMode", App::ParamInfo::Bool, true)
        .setTitle("Check for tangency")
        .setDoc("Boolean operation check: look for shapes that only touch."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryMergeVertexMode", "MergeVertexMode", App::ParamInfo::Bool, true)
        .setTitle("Check for mergeable vertices")
        .setDoc("Boolean operation check: look for vertices that should be one."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryMergeEdgeMode", "MergeEdgeMode", App::ParamInfo::Bool, true)
        .setTitle("Check for mergeable edges")
        .setDoc("Boolean operation check: look for edges that should be one."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part/CheckGeometry", "CheckGeometryCurveOnSurfaceMode", "CurveOnSurfaceMode", App::ParamInfo::Bool, true)
        .setTitle("Check curves on surfaces")
        .setDoc("Boolean operation check: look for edges whose curve on a face does\n"
"not follow the edge."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "ParametricRefine", "ParametricRefine", App::ParamInfo::Bool, true)
        .setTitle("Refine shape makes a feature")
        .setDoc("Refine Shape makes a parametric Refine feature that follows its\n"
"source. When off it makes a plain copy of the refined shape."),
    App::ParamInfo("PartGui", "PartParams", "User parameter:BaseApp/Preferences/Mod/Part", "AddBaseObjectName", "AddBaseObjectName", App::ParamInfo::Bool, false)
        .setTitle("Add the base object's name")
        .setDoc("Extrude and Scale label their result with the name of the object\n"
"it was made from."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle PartParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docNormalsFromUVNodes() {
    return QT_TRANSLATE_NOOP("PartParams",
"Take the shading normals of a shape from its exact surface instead\n"
"of from the display triangles. Gives smoother shading of curved\n"
"faces.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getNormalsFromUVNodes() {
    return instance()->NormalsFromUVNodes;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultNormalsFromUVNodes() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setNormalsFromUVNodes(const bool &v) {
    instance()->handle->SetBool("NormalsFromUVNodes",v);
    instance()->NormalsFromUVNodes = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeNormalsFromUVNodes() {
    instance()->handle->RemoveBool("NormalsFromUVNodes");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docTwoSideRendering() {
    return QT_TRANSLATE_NOOP("PartParams",
"Light new shapes from both sides, so the back of a face looks like\n"
"the front. When off the back shows the backlight colour or black.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getTwoSideRendering() {
    return instance()->TwoSideRendering;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultTwoSideRendering() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setTwoSideRendering(const bool &v) {
    instance()->handle->SetBool("TwoSideRendering",v);
    instance()->TwoSideRendering = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeTwoSideRendering() {
    instance()->handle->RemoveBool("TwoSideRendering");
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
const char *PartParams::docOverrideTessellation() {
    return QT_TRANSLATE_NOOP("PartParams",
"Draw every shape with the deviation and angular deflection set\n"
"here, ignoring the values stored in each object. When off a change\n"
"of those two settings is written into the open objects instead.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getOverrideTessellation() {
    return instance()->OverrideTessellation;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultOverrideTessellation() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setOverrideTessellation(const bool &v) {
    instance()->handle->SetBool("OverrideTessellation",v);
    instance()->OverrideTessellation = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeOverrideTessellation() {
    instance()->handle->RemoveBool("OverrideTessellation");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docMapFaceColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Let new shapes take their face colours from the shapes they were\n"
"made from. Turn off to give all faces of an object one colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getMapFaceColor() {
    return instance()->MapFaceColor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultMapFaceColor() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setMapFaceColor(const bool &v) {
    instance()->handle->SetBool("MapFaceColor",v);
    instance()->MapFaceColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeMapFaceColor() {
    instance()->handle->RemoveBool("MapFaceColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docMapLineColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Let new shapes take their edge colours from the shapes they were\n"
"made from. Turn off to give all edges of an object one colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getMapLineColor() {
    return instance()->MapLineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultMapLineColor() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setMapLineColor(const bool &v) {
    instance()->handle->SetBool("MapLineColor",v);
    instance()->MapLineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeMapLineColor() {
    instance()->handle->RemoveBool("MapLineColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docMapPointColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Let new shapes take their vertex colours from the shapes they were\n"
"made from. Turn off to give all vertices of an object one colour.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getMapPointColor() {
    return instance()->MapPointColor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultMapPointColor() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setMapPointColor(const bool &v) {
    instance()->handle->SetBool("MapPointColor",v);
    instance()->MapPointColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeMapPointColor() {
    instance()->handle->RemoveBool("MapPointColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docMapTransparency() {
    return QT_TRANSLATE_NOOP("PartParams",
"Let new shapes take the transparency of their faces from the\n"
"shapes they were made from. Turn off for one transparency per\n"
"object.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getMapTransparency() {
    return instance()->MapTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultMapTransparency() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setMapTransparency(const bool &v) {
    instance()->handle->SetBool("MapTransparency",v);
    instance()->MapTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeMapTransparency() {
    instance()->handle->RemoveBool("MapTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docAutoGridScale() {
    return QT_TRANSLATE_NOOP("PartParams",
"Double or halve the grid size of a sketch being edited as the view\n"
"is zoomed, so that the grid keeps a similar spacing on screen.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getAutoGridScale() {
    return instance()->AutoGridScale;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultAutoGridScale() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setAutoGridScale(const bool &v) {
    instance()->handle->SetBool("AutoGridScale",v);
    instance()->AutoGridScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeAutoGridScale() {
    instance()->handle->RemoveBool("AutoGridScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docPreviewAddColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Colour of the preview of a feature that adds material. Its alpha\n"
"part sets how transparent the preview is.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getPreviewAddColor() {
    return instance()->PreviewAddColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultPreviewAddColor() {
    const static unsigned long def = 0x64FFFF30;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setPreviewAddColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("PreviewAddColor",v);
    instance()->PreviewAddColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removePreviewAddColor() {
    instance()->handle->RemoveUnsigned("PreviewAddColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docPreviewSubColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Colour of the preview of a feature that removes material. Its\n"
"alpha part sets how transparent the preview is.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getPreviewSubColor() {
    return instance()->PreviewSubColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultPreviewSubColor() {
    const static unsigned long def = 0xFF646430;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setPreviewSubColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("PreviewSubColor",v);
    instance()->PreviewSubColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removePreviewSubColor() {
    instance()->handle->RemoveUnsigned("PreviewSubColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docPreviewDressColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Colour of the preview of a dress-up feature such as a fillet or a\n"
"chamfer. Its alpha part sets how transparent the preview is.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getPreviewDressColor() {
    return instance()->PreviewDressColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultPreviewDressColor() {
    const static unsigned long def = 0xFF64FF30;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setPreviewDressColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("PreviewDressColor",v);
    instance()->PreviewDressColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removePreviewDressColor() {
    instance()->handle->RemoveUnsigned("PreviewDressColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docPreviewIntersectColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Colour of the preview of a feature that keeps what it has in\n"
"common with the body. Its alpha part sets how transparent the\n"
"preview is.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getPreviewIntersectColor() {
    return instance()->PreviewIntersectColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultPreviewIntersectColor() {
    const static unsigned long def = 0x6464FF30;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setPreviewIntersectColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("PreviewIntersectColor",v);
    instance()->PreviewIntersectColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removePreviewIntersectColor() {
    instance()->handle->RemoveUnsigned("PreviewIntersectColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docPreviewOnEdit() {
    return QT_TRANSLATE_NOOP("PartParams",
"Show a preview of the result while a PartDesign feature is edited,\n"
"and hold back the recompute of the feature until the preview is\n"
"turned off or the edit ends.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getPreviewOnEdit() {
    return instance()->PreviewOnEdit;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultPreviewOnEdit() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setPreviewOnEdit(const bool &v) {
    instance()->handle->SetBool("PreviewOnEdit",v);
    instance()->PreviewOnEdit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removePreviewOnEdit() {
    instance()->handle->RemoveBool("PreviewOnEdit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docPreviewWithTransparency() {
    return QT_TRANSLATE_NOOP("PartParams",
"Draw the preview of an edited PartDesign feature transparent. When\n"
"off it is drawn opaque.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getPreviewWithTransparency() {
    return instance()->PreviewWithTransparency;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultPreviewWithTransparency() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setPreviewWithTransparency(const bool &v) {
    instance()->handle->SetBool("PreviewWithTransparency",v);
    instance()->PreviewWithTransparency = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removePreviewWithTransparency() {
    instance()->handle->RemoveBool("PreviewWithTransparency");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docEditOnTop() {
    return QT_TRANSLATE_NOOP("PartParams",
"Draw the PartDesign feature being edited on top of everything else\n"
"in the 3D view.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getEditOnTop() {
    return instance()->EditOnTop;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultEditOnTop() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setEditOnTop(const bool &v) {
    instance()->handle->SetBool("EditOnTop",v);
    instance()->EditOnTop = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeEditOnTop() {
    instance()->handle->RemoveBool("EditOnTop");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docEditRecomputeWait() {
    return QT_TRANSLATE_NOOP("PartParams",
"Delay between a change in a PartDesign task panel and the update\n"
"of the feature, in milliseconds. A third of it is used while the\n"
"preview is shown.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getEditRecomputeWait() {
    return instance()->EditRecomputeWait;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultEditRecomputeWait() {
    const static long def = 300;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setEditRecomputeWait(const long &v) {
    instance()->handle->SetInt("EditRecomputeWait",v);
    instance()->EditRecomputeWait = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeEditRecomputeWait() {
    instance()->handle->RemoveInt("EditRecomputeWait");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docAdjustCameraForNewFeature() {
    return QT_TRANSLATE_NOOP("PartParams",
"Move the camera to bring a newly created feature into view. Used\n"
"by Part offset and thickness, by PartDesign features made from a\n"
"selected profile, and by new bodies.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getAdjustCameraForNewFeature() {
    return instance()->AdjustCameraForNewFeature;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultAdjustCameraForNewFeature() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setAdjustCameraForNewFeature(const bool &v) {
    instance()->handle->SetBool("AdjustCameraForNewFeature",v);
    instance()->AdjustCameraForNewFeature = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeAdjustCameraForNewFeature() {
    instance()->handle->RemoveBool("AdjustCameraForNewFeature");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDefaultDatumColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Colour and transparency of new datum planes, lines and points, of\n"
"shape binders, of sub-shape binders shown in binder style, and of\n"
"PartDesign extrusions: golden yellow, mostly see-through, unless\n"
"set.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getDefaultDatumColor() {
    return instance()->DefaultDatumColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultDefaultDatumColor() {
    const static unsigned long def = 0xFFD70066;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDefaultDatumColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultDatumColor",v);
    instance()->DefaultDatumColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDefaultDatumColor() {
    instance()->handle->RemoveUnsigned("DefaultDatumColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDefaultDatumLineColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Line and point color of a shape binder, darker than DefaultDatumColor\n"
"so that its outline shows against the model (upstream 5dbb4d7c7e)");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getDefaultDatumLineColor() {
    return instance()->DefaultDatumLineColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultDefaultDatumLineColor() {
    const static unsigned long def = 0xFA9600FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDefaultDatumLineColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultDatumLineColor",v);
    instance()->DefaultDatumLineColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDefaultDatumLineColor() {
    instance()->handle->RemoveUnsigned("DefaultDatumLineColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docRespectSystemDPI() {
    return QT_TRANSLATE_NOOP("PartParams",
"Scale the line width and point size of shapes by the pixel ratio\n"
"of the display. May look wrong with monitors of different scaling.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getRespectSystemDPI() {
    return instance()->RespectSystemDPI;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultRespectSystemDPI() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setRespectSystemDPI(const bool &v) {
    instance()->handle->SetBool("RespectSystemDPI",v);
    instance()->RespectSystemDPI = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeRespectSystemDPI() {
    instance()->handle->RemoveBool("RespectSystemDPI");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docShapeInstancing() {
    return QT_TRANSLATE_NOOP("PartParams",
"Share the tessellation of repeated sub-shapes (same TopoDS_TShape)\n"
"inside a compound and render them as GPU instances. Only takes\n"
"effect when the renderer supports instanced draws; otherwise the\n"
"geometry is flattened as before.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getShapeInstancing() {
    return instance()->ShapeInstancing;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultShapeInstancing() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setShapeInstancing(const bool &v) {
    instance()->handle->SetBool("ShapeInstancing",v);
    instance()->ShapeInstancing = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeShapeInstancing() {
    instance()->handle->RemoveBool("ShapeInstancing");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docSelectionPickThreshold() {
    return QT_TRANSLATE_NOOP("PartParams",
"Size of a shape above which picking first narrows the search with\n"
"bounding boxes, counted in face, edge or point indices. Smaller\n"
"shapes are tested whole. 0 or less always tests everything.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getSelectionPickThreshold() {
    return instance()->SelectionPickThreshold;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultSelectionPickThreshold() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setSelectionPickThreshold(const long &v) {
    instance()->handle->SetInt("SelectionPickThreshold",v);
    instance()->SelectionPickThreshold = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeSelectionPickThreshold() {
    instance()->handle->RemoveInt("SelectionPickThreshold");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docSelectionPickThreshold2() {
    return QT_TRANSLATE_NOOP("PartParams",
"Size above which a single face or edge gets a search structure of\n"
"its own for picking: the triangles of a face, the points of an\n"
"edge. 0 or less turns it off for faces.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getSelectionPickThreshold2() {
    return instance()->SelectionPickThreshold2;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultSelectionPickThreshold2() {
    const static long def = 500;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setSelectionPickThreshold2(const long &v) {
    instance()->handle->SetInt("SelectionPickThreshold2",v);
    instance()->SelectionPickThreshold2 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeSelectionPickThreshold2() {
    instance()->handle->RemoveInt("SelectionPickThreshold2");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docSelectionPickRTree() {
    return QT_TRANSLATE_NOOP("PartParams",
"Pick with a spatial index of a part's triangles instead of testing\n"
"every triangle. Much faster on dense parts; the index is built on the\n"
"first pick that reaches a part. Parts smaller than\n"
"SelectionPickThreshold2 are picked directly.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getSelectionPickRTree() {
    return instance()->SelectionPickRTree;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultSelectionPickRTree() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setSelectionPickRTree(const bool &v) {
    instance()->handle->SetBool("SelectionPickRTree",v);
    instance()->SelectionPickRTree = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeSelectionPickRTree() {
    instance()->handle->RemoveBool("SelectionPickRTree");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDimensions3dColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Colour of the direct distance of a Part measurement in the 3D view.\n"
"The measurements shown follow a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getDimensions3dColor() {
    return instance()->Dimensions3dColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultDimensions3dColor() {
    const static unsigned long def = 0xFF0000FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDimensions3dColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("Dimensions3dColor",v);
    instance()->Dimensions3dColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDimensions3dColor() {
    instance()->handle->RemoveUnsigned("Dimensions3dColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDimensionsDeltaColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Colour of the X, Y and Z components of a Part distance measurement\n"
"in the 3D view. The measurements shown follow a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getDimensionsDeltaColor() {
    return instance()->DimensionsDeltaColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultDimensionsDeltaColor() {
    const static unsigned long def = 0x00FF00FF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDimensionsDeltaColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DimensionsDeltaColor",v);
    instance()->DimensionsDeltaColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDimensionsDeltaColor() {
    instance()->handle->RemoveUnsigned("DimensionsDeltaColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDimensionsAngularColor() {
    return QT_TRANSLATE_NOOP("PartParams",
"Colour of a Part angle measurement in the 3D view. The\n"
"measurements shown follow a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & PartParams::getDimensionsAngularColor() {
    return instance()->DimensionsAngularColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & PartParams::defaultDimensionsAngularColor() {
    const static unsigned long def = 0x0000FFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDimensionsAngularColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DimensionsAngularColor",v);
    instance()->DimensionsAngularColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDimensionsAngularColor() {
    instance()->handle->RemoveUnsigned("DimensionsAngularColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDimensionsFontSize() {
    return QT_TRANSLATE_NOOP("PartParams",
"Size of the text of Part measurements in the 3D view. The\n"
"measurements shown follow a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & PartParams::getDimensionsFontSize() {
    return instance()->DimensionsFontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & PartParams::defaultDimensionsFontSize() {
    const static long def = 30;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDimensionsFontSize(const long &v) {
    instance()->handle->SetInt("DimensionsFontSize",v);
    instance()->DimensionsFontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDimensionsFontSize() {
    instance()->handle->RemoveInt("DimensionsFontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDimensionsFontName() {
    return QT_TRANSLATE_NOOP("PartParams",
"Font of the text of Part measurements in the 3D view; defaultFont\n"
"is the 3D view's own. The measurements shown follow a change.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & PartParams::getDimensionsFontName() {
    return instance()->DimensionsFontName;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & PartParams::defaultDimensionsFontName() {
    const static std::string def = "defaultFont";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDimensionsFontName(const std::string &v) {
    instance()->handle->SetASCII("DimensionsFontName",v);
    instance()->DimensionsFontName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDimensionsFontName() {
    instance()->handle->RemoveASCII("DimensionsFontName");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDimensionsFontStyleBold() {
    return QT_TRANSLATE_NOOP("PartParams",
"Draw the text of Part measurements in the 3D view in bold.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getDimensionsFontStyleBold() {
    return instance()->DimensionsFontStyleBold;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultDimensionsFontStyleBold() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDimensionsFontStyleBold(const bool &v) {
    instance()->handle->SetBool("DimensionsFontStyleBold",v);
    instance()->DimensionsFontStyleBold = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDimensionsFontStyleBold() {
    instance()->handle->RemoveBool("DimensionsFontStyleBold");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docDimensionsFontStyleItalic() {
    return QT_TRANSLATE_NOOP("PartParams",
"Draw the text of Part measurements in the 3D view in italic.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getDimensionsFontStyleItalic() {
    return instance()->DimensionsFontStyleItalic;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultDimensionsFontStyleItalic() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setDimensionsFontStyleItalic(const bool &v) {
    instance()->handle->SetBool("DimensionsFontStyleItalic",v);
    instance()->DimensionsFontStyleItalic = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeDimensionsFontStyleItalic() {
    instance()->handle->RemoveBool("DimensionsFontStyleItalic");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryAutoRun() {
    return QT_TRANSLATE_NOOP("PartParams",
"Run the geometry check as soon as its panel opens, without the Run\n"
"Check button.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryAutoRun() {
    return instance()->CheckGeometryAutoRun;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryAutoRun() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryAutoRun(const bool &v) {
    instance()->subHandles[0]->SetBool("AutoRun",v);
    instance()->CheckGeometryAutoRun = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryAutoRun() {
    instance()->subHandles[0]->RemoveBool("AutoRun");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryRunBOPCheck() {
    return QT_TRANSLATE_NOOP("PartParams",
"Run the Boolean operation check on shapes the basic check finds\n"
"valid. It finds more, and can be very slow.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryRunBOPCheck() {
    return instance()->CheckGeometryRunBOPCheck;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryRunBOPCheck() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryRunBOPCheck(const bool &v) {
    instance()->subHandles[0]->SetBool("RunBOPCheck",v);
    instance()->CheckGeometryRunBOPCheck = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryRunBOPCheck() {
    instance()->subHandles[0]->RemoveBool("RunBOPCheck");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryRunSingleThreaded() {
    return QT_TRANSLATE_NOOP("PartParams",
"Run the Boolean operation check of the geometry check in a single\n"
"thread: slower, and more stable.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryRunSingleThreaded() {
    return instance()->CheckGeometryRunSingleThreaded;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryRunSingleThreaded() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryRunSingleThreaded(const bool &v) {
    instance()->subHandles[0]->SetBool("RunSingleThreaded",v);
    instance()->CheckGeometryRunSingleThreaded = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryRunSingleThreaded() {
    instance()->subHandles[0]->RemoveBool("RunSingleThreaded");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryLogErrors() {
    return QT_TRANSLATE_NOOP("PartParams",
"Write the errors the geometry check finds to the report view.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryLogErrors() {
    return instance()->CheckGeometryLogErrors;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryLogErrors() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryLogErrors(const bool &v) {
    instance()->subHandles[0]->SetBool("LogErrors",v);
    instance()->CheckGeometryLogErrors = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryLogErrors() {
    instance()->subHandles[0]->RemoveBool("LogErrors");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryExpandShapeContent() {
    return QT_TRANSLATE_NOOP("PartParams",
"Open the shape content of the geometry check's result when it is\n"
"shown.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryExpandShapeContent() {
    return instance()->CheckGeometryExpandShapeContent;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryExpandShapeContent() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryExpandShapeContent(const bool &v) {
    instance()->subHandles[0]->SetBool("ExpandShapeContent",v);
    instance()->CheckGeometryExpandShapeContent = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryExpandShapeContent() {
    instance()->subHandles[0]->RemoveBool("ExpandShapeContent");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryAdvancedShapeContent() {
    return QT_TRANSLATE_NOOP("PartParams",
"Show more about the shape in the geometry check's shape content.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryAdvancedShapeContent() {
    return instance()->CheckGeometryAdvancedShapeContent;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryAdvancedShapeContent() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryAdvancedShapeContent(const bool &v) {
    instance()->subHandles[0]->SetBool("AdvancedShapeContent",v);
    instance()->CheckGeometryAdvancedShapeContent = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryAdvancedShapeContent() {
    instance()->subHandles[0]->RemoveBool("AdvancedShapeContent");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryArgumentTypeMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Boolean operation check: look for shapes of a kind the operation\n"
"cannot take.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryArgumentTypeMode() {
    return instance()->CheckGeometryArgumentTypeMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryArgumentTypeMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryArgumentTypeMode(const bool &v) {
    instance()->subHandles[0]->SetBool("ArgumentTypeMode",v);
    instance()->CheckGeometryArgumentTypeMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryArgumentTypeMode() {
    instance()->subHandles[0]->RemoveBool("ArgumentTypeMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometrySelfInterMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Boolean operation check: look for shapes that intersect themselves.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometrySelfInterMode() {
    return instance()->CheckGeometrySelfInterMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometrySelfInterMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometrySelfInterMode(const bool &v) {
    instance()->subHandles[0]->SetBool("SelfInterMode",v);
    instance()->CheckGeometrySelfInterMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometrySelfInterMode() {
    instance()->subHandles[0]->RemoveBool("SelfInterMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometrySmallEdgeMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Boolean operation check: look for edges that are too small.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometrySmallEdgeMode() {
    return instance()->CheckGeometrySmallEdgeMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometrySmallEdgeMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometrySmallEdgeMode(const bool &v) {
    instance()->subHandles[0]->SetBool("SmallEdgeMode",v);
    instance()->CheckGeometrySmallEdgeMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometrySmallEdgeMode() {
    instance()->subHandles[0]->RemoveBool("SmallEdgeMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryRebuildFaceMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Boolean operation check: look for faces that cannot be rebuilt.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryRebuildFaceMode() {
    return instance()->CheckGeometryRebuildFaceMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryRebuildFaceMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryRebuildFaceMode(const bool &v) {
    instance()->subHandles[0]->SetBool("RebuildFaceMode",v);
    instance()->CheckGeometryRebuildFaceMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryRebuildFaceMode() {
    instance()->subHandles[0]->RemoveBool("RebuildFaceMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryContinuityMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Boolean operation check: look for edges that are not continuous.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryContinuityMode() {
    return instance()->CheckGeometryContinuityMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryContinuityMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryContinuityMode(const bool &v) {
    instance()->subHandles[0]->SetBool("ContinuityMode",v);
    instance()->CheckGeometryContinuityMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryContinuityMode() {
    instance()->subHandles[0]->RemoveBool("ContinuityMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryTangentMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Boolean operation check: look for shapes that only touch.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryTangentMode() {
    return instance()->CheckGeometryTangentMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryTangentMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryTangentMode(const bool &v) {
    instance()->subHandles[0]->SetBool("TangentMode",v);
    instance()->CheckGeometryTangentMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryTangentMode() {
    instance()->subHandles[0]->RemoveBool("TangentMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryMergeVertexMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Boolean operation check: look for vertices that should be one.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryMergeVertexMode() {
    return instance()->CheckGeometryMergeVertexMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryMergeVertexMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryMergeVertexMode(const bool &v) {
    instance()->subHandles[0]->SetBool("MergeVertexMode",v);
    instance()->CheckGeometryMergeVertexMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryMergeVertexMode() {
    instance()->subHandles[0]->RemoveBool("MergeVertexMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryMergeEdgeMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Boolean operation check: look for edges that should be one.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryMergeEdgeMode() {
    return instance()->CheckGeometryMergeEdgeMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryMergeEdgeMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryMergeEdgeMode(const bool &v) {
    instance()->subHandles[0]->SetBool("MergeEdgeMode",v);
    instance()->CheckGeometryMergeEdgeMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryMergeEdgeMode() {
    instance()->subHandles[0]->RemoveBool("MergeEdgeMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docCheckGeometryCurveOnSurfaceMode() {
    return QT_TRANSLATE_NOOP("PartParams",
"Boolean operation check: look for edges whose curve on a face does\n"
"not follow the edge.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getCheckGeometryCurveOnSurfaceMode() {
    return instance()->CheckGeometryCurveOnSurfaceMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultCheckGeometryCurveOnSurfaceMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setCheckGeometryCurveOnSurfaceMode(const bool &v) {
    instance()->subHandles[0]->SetBool("CurveOnSurfaceMode",v);
    instance()->CheckGeometryCurveOnSurfaceMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeCheckGeometryCurveOnSurfaceMode() {
    instance()->subHandles[0]->RemoveBool("CurveOnSurfaceMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docParametricRefine() {
    return QT_TRANSLATE_NOOP("PartParams",
"Refine Shape makes a parametric Refine feature that follows its\n"
"source. When off it makes a plain copy of the refined shape.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getParametricRefine() {
    return instance()->ParametricRefine;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultParametricRefine() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setParametricRefine(const bool &v) {
    instance()->handle->SetBool("ParametricRefine",v);
    instance()->ParametricRefine = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeParametricRefine() {
    instance()->handle->RemoveBool("ParametricRefine");
}

// Auto generated code (Tools/params_utils.py:397)
const char *PartParams::docAddBaseObjectName() {
    return QT_TRANSLATE_NOOP("PartParams",
"Extrude and Scale label their result with the name of the object\n"
"it was made from.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & PartParams::getAddBaseObjectName() {
    return instance()->AddBaseObjectName;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & PartParams::defaultAddBaseObjectName() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void PartParams::setAddBaseObjectName(const bool &v) {
    instance()->handle->SetBool("AddBaseObjectName",v);
    instance()->AddBaseObjectName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void PartParams::removeAddBaseObjectName() {
    instance()->handle->RemoveBool("AddBaseObjectName");
}
//[[[end]]]

void PartParams::onMeshDeviationChanged() {
    tessellationParamChanged();
}

void PartParams::onDimensions3dColorChanged() {
    dimensionParamChanged();
}

void PartParams::onDimensionsDeltaColorChanged() {
    dimensionParamChanged();
}

void PartParams::onDimensionsAngularColorChanged() {
    dimensionParamChanged();
}

void PartParams::onDimensionsFontSizeChanged() {
    dimensionParamChanged();
}

void PartParams::onDimensionsFontNameChanged() {
    dimensionParamChanged();
}

void PartParams::onDimensionsFontStyleBoldChanged() {
    dimensionParamChanged();
}

void PartParams::onDimensionsFontStyleItalicChanged() {
    dimensionParamChanged();
}

void PartParams::onMeshAngularDeflectionChanged() {
    tessellationParamChanged();
}

void PartParams::onMinimumDeviationChanged() {
    tessellationParamChanged();
}

void PartParams::onMinimumAngularDeflectionChanged() {
    tessellationParamChanged();
}

void PartParams::onOverrideTessellationChanged() {
    tessellationParamChanged();
}

void PartParams::onRespectSystemDPIChanged() {
    getTimer().start(100);
}

void PartParams::onShapeInstancingChanged() {
    getTimer().start(100);
}

namespace PartGui {
void initShapeInstancingGateObserver();
}

namespace {
// The shape-instancing gate also depends on Gui-side parameters (render
// cache mode and the selected renderer type); refresh the Part visuals
// when those flip so the representation switches between the instanced
// and the flattened build.
class InstancingGateObserver: public ParameterGrp::ObserverType {
public:
    InstancingGateObserver()
    {
        hView = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/View");
        hRender = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/View/Render");
        renderCache = readRenderCache();
        rendererType = readRendererType();
        hView->Attach(this);
        hRender->Attach(this);
        // The gate reads live backend state (Render::Renderer
        // activeCount()/instancingHint()); the parameter observers alone
        // miss a backend attached or torn down without a pref flip
        // (per-view or scripted selection, last 3D view closing).
        Render::Renderer::addActivityObserver([]() {
            getTimer().start(100);
        });
    }
    ~InstancingGateObserver() override
    {
        hView->Detach(this);
        hRender->Detach(this);
    }
    void OnChange(Base::Subject<const char*> &subject, const char *reason) override
    {
        (void)subject;
        // Each group only notifies its own keys: RenderCache lives in the
        // View group, Type in View/Render.
        if (!reason || (strcmp(reason, "RenderCache") != 0 && strcmp(reason, "Type") != 0))
            return;
        // An observer of this kind is told of every WRITE of a key, changed
        // or not, and OK in the preferences writes them all. The timer
        // reloads every Part view provider of every document, a re-mesh of
        // seconds, so only a value that differs from the one last seen
        // starts it.
        long cache = readRenderCache();
        std::string type = readRendererType();
        if (cache == renderCache && type == rendererType)
            return;
        renderCache = cache;
        rendererType = type;
        getTimer().start(100);
    }
private:
    // Read with the defaults of the settings' own definitions, so that a key
    // stored for the first time with its default is no change either.
    long readRenderCache() const
    {
        // the mode the program draws by: 3 with the render engine
        // whatever the key holds (Gui::RenderParams::renderCache())
        return Gui::RenderParams::renderCache();
    }
    std::string readRendererType() const
    {
        return hRender->GetASCII("Type", Gui::RenderParams::defaultType().c_str());
    }

    ParameterGrp::handle hView;
    ParameterGrp::handle hRender;
    long renderCache = 0;
    std::string rendererType;
};
} // anonymous namespace

// Installed once from the ViewProviderPartExt constructor (declared
// locally there — the PartParams class body is cog-generated and takes
// no hand-written members).
void PartGui::initShapeInstancingGateObserver()
{
    static InstancingGateObserver observer;
}
