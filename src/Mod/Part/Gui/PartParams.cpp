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

    // Auto generated code (Tools/params_utils.py:254)
    PartParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Part");
        handle->Attach(this);

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
        .setTitle("Default Datum Color")
        .setDoc("Default face colour of shape binders, of sub-shape binders shown\n"
"in binder style, and of PartDesign extrusions. Datum planes, lines\n"
"and points take theirs from the PartDesign settings.")
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
"Default face colour of shape binders, of sub-shape binders shown\n"
"in binder style, and of PartDesign extrusions. Datum planes, lines\n"
"and points take theirs from the PartDesign settings.");
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
//[[[end]]]

void PartParams::onMeshDeviationChanged() {
    tessellationParamChanged();
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
        return hView->GetInt("RenderCache", Gui::ViewParams::defaultRenderCache());
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
