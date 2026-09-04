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
#include <Gui/Renderer/Renderer.h>
#include "ViewProvider.h"

namespace {
QTimer &getTimer() {
    static QTimer *timer;
    if (!timer) {
        timer = new QTimer();
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, [](){
            // search for Part view providers and apply the new settings
            for (auto doc : App::GetApplication().getDocuments()) {
                auto gdoc = Gui::Application::Instance->getDocument(doc);
                for (auto vp : gdoc->getViewProvidersOfType(
                            PartGui::ViewProviderPart::getClassTypeId()))
                    static_cast<PartGui::ViewProviderPart*>(vp)->reload();
            }
        });
    }
    return *timer;
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
#include "PartParams.h"
using namespace PartGui;

// Auto generated code (Tools/params_utils.py:209)
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
    bool RespectSystemDPI;
    bool ShapeInstancing;
    long SelectionPickThreshold;
    long SelectionPickThreshold2;
    bool SelectionPickRTree;

    // Auto generated code (Tools/params_utils.py:253)
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

    // Auto generated code (Tools/params_utils.py:283)
    ~PartParamsP() {
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
        
        
    }


    // Auto generated code (Tools/params_utils.py:310)
    static void updateNormalsFromUVNodes(PartParamsP *self) {
        self->NormalsFromUVNodes = self->handle->GetBool("NormalsFromUVNodes", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateTwoSideRendering(PartParamsP *self) {
        self->TwoSideRendering = self->handle->GetBool("TwoSideRendering", true);
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateMinimumDeviation(PartParamsP *self) {
        auto v = self->handle->GetFloat("MinimumDeviation", 0.05);
        if (self->MinimumDeviation != v) {
            self->MinimumDeviation = v;
            PartParams::onMinimumDeviationChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateMeshDeviation(PartParamsP *self) {
        auto v = self->handle->GetFloat("MeshDeviation", 0.2);
        if (self->MeshDeviation != v) {
            self->MeshDeviation = v;
            PartParams::onMeshDeviationChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateMeshAngularDeflection(PartParamsP *self) {
        auto v = self->handle->GetFloat("MeshAngularDeflection", 28.65);
        if (self->MeshAngularDeflection != v) {
            self->MeshAngularDeflection = v;
            PartParams::onMeshAngularDeflectionChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateMinimumAngularDeflection(PartParamsP *self) {
        auto v = self->handle->GetFloat("MinimumAngularDeflection", 5.0);
        if (self->MinimumAngularDeflection != v) {
            self->MinimumAngularDeflection = v;
            PartParams::onMinimumAngularDeflectionChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateOverrideTessellation(PartParamsP *self) {
        auto v = self->handle->GetBool("OverrideTessellation", false);
        if (self->OverrideTessellation != v) {
            self->OverrideTessellation = v;
            PartParams::onOverrideTessellationChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMapFaceColor(PartParamsP *self) {
        self->MapFaceColor = self->handle->GetBool("MapFaceColor", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMapLineColor(PartParamsP *self) {
        self->MapLineColor = self->handle->GetBool("MapLineColor", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMapPointColor(PartParamsP *self) {
        self->MapPointColor = self->handle->GetBool("MapPointColor", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMapTransparency(PartParamsP *self) {
        self->MapTransparency = self->handle->GetBool("MapTransparency", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAutoGridScale(PartParamsP *self) {
        self->AutoGridScale = self->handle->GetBool("AutoGridScale", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreviewAddColor(PartParamsP *self) {
        self->PreviewAddColor = self->handle->GetUnsigned("PreviewAddColor", 0x64FFFF30);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreviewSubColor(PartParamsP *self) {
        self->PreviewSubColor = self->handle->GetUnsigned("PreviewSubColor", 0xFF646430);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreviewDressColor(PartParamsP *self) {
        self->PreviewDressColor = self->handle->GetUnsigned("PreviewDressColor", 0xFF64FF30);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreviewIntersectColor(PartParamsP *self) {
        self->PreviewIntersectColor = self->handle->GetUnsigned("PreviewIntersectColor", 0x6464FF30);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreviewOnEdit(PartParamsP *self) {
        self->PreviewOnEdit = self->handle->GetBool("PreviewOnEdit", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreviewWithTransparency(PartParamsP *self) {
        self->PreviewWithTransparency = self->handle->GetBool("PreviewWithTransparency", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEditOnTop(PartParamsP *self) {
        self->EditOnTop = self->handle->GetBool("EditOnTop", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEditRecomputeWait(PartParamsP *self) {
        self->EditRecomputeWait = self->handle->GetInt("EditRecomputeWait", 300);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAdjustCameraForNewFeature(PartParamsP *self) {
        self->AdjustCameraForNewFeature = self->handle->GetBool("AdjustCameraForNewFeature", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDefaultDatumColor(PartParamsP *self) {
        self->DefaultDatumColor = self->handle->GetUnsigned("DefaultDatumColor", 0xFFD70066);
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateRespectSystemDPI(PartParamsP *self) {
        auto v = self->handle->GetBool("RespectSystemDPI", false);
        if (self->RespectSystemDPI != v) {
            self->RespectSystemDPI = v;
            PartParams::onRespectSystemDPIChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:318)
    static void updateShapeInstancing(PartParamsP *self) {
        auto v = self->handle->GetBool("ShapeInstancing", true);
        if (self->ShapeInstancing != v) {
            self->ShapeInstancing = v;
            PartParams::onShapeInstancingChanged();
        }
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionPickThreshold(PartParamsP *self) {
        self->SelectionPickThreshold = self->handle->GetInt("SelectionPickThreshold", 1000);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionPickThreshold2(PartParamsP *self) {
        self->SelectionPickThreshold2 = self->handle->GetInt("SelectionPickThreshold2", 500);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSelectionPickRTree(PartParamsP *self) {
        self->SelectionPickRTree = self->handle->GetBool("SelectionPickRTree", true);
    }
};

// Auto generated code (Tools/params_utils.py:332)
PartParamsP *instance() {
    static PartParamsP *inst = new PartParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:343)
ParameterGrp::handle PartParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docNormalsFromUVNodes() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getNormalsFromUVNodes() {
    return instance()->NormalsFromUVNodes;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultNormalsFromUVNodes() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setNormalsFromUVNodes(const bool &v) {
    instance()->handle->SetBool("NormalsFromUVNodes",v);
    instance()->NormalsFromUVNodes = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeNormalsFromUVNodes() {
    instance()->handle->RemoveBool("NormalsFromUVNodes");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docTwoSideRendering() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getTwoSideRendering() {
    return instance()->TwoSideRendering;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultTwoSideRendering() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setTwoSideRendering(const bool &v) {
    instance()->handle->SetBool("TwoSideRendering",v);
    instance()->TwoSideRendering = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeTwoSideRendering() {
    instance()->handle->RemoveBool("TwoSideRendering");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docMinimumDeviation() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & PartParams::getMinimumDeviation() {
    return instance()->MinimumDeviation;
}

// Auto generated code (Tools/params_utils.py:388)
const double & PartParams::defaultMinimumDeviation() {
    const static double def = 0.05;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setMinimumDeviation(const double &v) {
    instance()->handle->SetFloat("MinimumDeviation",v);
    instance()->MinimumDeviation = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeMinimumDeviation() {
    instance()->handle->RemoveFloat("MinimumDeviation");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docMeshDeviation() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & PartParams::getMeshDeviation() {
    return instance()->MeshDeviation;
}

// Auto generated code (Tools/params_utils.py:388)
const double & PartParams::defaultMeshDeviation() {
    const static double def = 0.2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setMeshDeviation(const double &v) {
    instance()->handle->SetFloat("MeshDeviation",v);
    instance()->MeshDeviation = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeMeshDeviation() {
    instance()->handle->RemoveFloat("MeshDeviation");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docMeshAngularDeflection() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & PartParams::getMeshAngularDeflection() {
    return instance()->MeshAngularDeflection;
}

// Auto generated code (Tools/params_utils.py:388)
const double & PartParams::defaultMeshAngularDeflection() {
    const static double def = 28.65;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setMeshAngularDeflection(const double &v) {
    instance()->handle->SetFloat("MeshAngularDeflection",v);
    instance()->MeshAngularDeflection = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeMeshAngularDeflection() {
    instance()->handle->RemoveFloat("MeshAngularDeflection");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docMinimumAngularDeflection() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & PartParams::getMinimumAngularDeflection() {
    return instance()->MinimumAngularDeflection;
}

// Auto generated code (Tools/params_utils.py:388)
const double & PartParams::defaultMinimumAngularDeflection() {
    const static double def = 5.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setMinimumAngularDeflection(const double &v) {
    instance()->handle->SetFloat("MinimumAngularDeflection",v);
    instance()->MinimumAngularDeflection = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeMinimumAngularDeflection() {
    instance()->handle->RemoveFloat("MinimumAngularDeflection");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docOverrideTessellation() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getOverrideTessellation() {
    return instance()->OverrideTessellation;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultOverrideTessellation() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setOverrideTessellation(const bool &v) {
    instance()->handle->SetBool("OverrideTessellation",v);
    instance()->OverrideTessellation = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeOverrideTessellation() {
    instance()->handle->RemoveBool("OverrideTessellation");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docMapFaceColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getMapFaceColor() {
    return instance()->MapFaceColor;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultMapFaceColor() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setMapFaceColor(const bool &v) {
    instance()->handle->SetBool("MapFaceColor",v);
    instance()->MapFaceColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeMapFaceColor() {
    instance()->handle->RemoveBool("MapFaceColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docMapLineColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getMapLineColor() {
    return instance()->MapLineColor;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultMapLineColor() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setMapLineColor(const bool &v) {
    instance()->handle->SetBool("MapLineColor",v);
    instance()->MapLineColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeMapLineColor() {
    instance()->handle->RemoveBool("MapLineColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docMapPointColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getMapPointColor() {
    return instance()->MapPointColor;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultMapPointColor() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setMapPointColor(const bool &v) {
    instance()->handle->SetBool("MapPointColor",v);
    instance()->MapPointColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeMapPointColor() {
    instance()->handle->RemoveBool("MapPointColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docMapTransparency() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getMapTransparency() {
    return instance()->MapTransparency;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultMapTransparency() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setMapTransparency(const bool &v) {
    instance()->handle->SetBool("MapTransparency",v);
    instance()->MapTransparency = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeMapTransparency() {
    instance()->handle->RemoveBool("MapTransparency");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docAutoGridScale() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getAutoGridScale() {
    return instance()->AutoGridScale;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultAutoGridScale() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setAutoGridScale(const bool &v) {
    instance()->handle->SetBool("AutoGridScale",v);
    instance()->AutoGridScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeAutoGridScale() {
    instance()->handle->RemoveBool("AutoGridScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docPreviewAddColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & PartParams::getPreviewAddColor() {
    return instance()->PreviewAddColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & PartParams::defaultPreviewAddColor() {
    const static unsigned long def = 0x64FFFF30;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setPreviewAddColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("PreviewAddColor",v);
    instance()->PreviewAddColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removePreviewAddColor() {
    instance()->handle->RemoveUnsigned("PreviewAddColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docPreviewSubColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & PartParams::getPreviewSubColor() {
    return instance()->PreviewSubColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & PartParams::defaultPreviewSubColor() {
    const static unsigned long def = 0xFF646430;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setPreviewSubColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("PreviewSubColor",v);
    instance()->PreviewSubColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removePreviewSubColor() {
    instance()->handle->RemoveUnsigned("PreviewSubColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docPreviewDressColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & PartParams::getPreviewDressColor() {
    return instance()->PreviewDressColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & PartParams::defaultPreviewDressColor() {
    const static unsigned long def = 0xFF64FF30;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setPreviewDressColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("PreviewDressColor",v);
    instance()->PreviewDressColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removePreviewDressColor() {
    instance()->handle->RemoveUnsigned("PreviewDressColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docPreviewIntersectColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & PartParams::getPreviewIntersectColor() {
    return instance()->PreviewIntersectColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & PartParams::defaultPreviewIntersectColor() {
    const static unsigned long def = 0x6464FF30;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setPreviewIntersectColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("PreviewIntersectColor",v);
    instance()->PreviewIntersectColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removePreviewIntersectColor() {
    instance()->handle->RemoveUnsigned("PreviewIntersectColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docPreviewOnEdit() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getPreviewOnEdit() {
    return instance()->PreviewOnEdit;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultPreviewOnEdit() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setPreviewOnEdit(const bool &v) {
    instance()->handle->SetBool("PreviewOnEdit",v);
    instance()->PreviewOnEdit = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removePreviewOnEdit() {
    instance()->handle->RemoveBool("PreviewOnEdit");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docPreviewWithTransparency() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getPreviewWithTransparency() {
    return instance()->PreviewWithTransparency;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultPreviewWithTransparency() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setPreviewWithTransparency(const bool &v) {
    instance()->handle->SetBool("PreviewWithTransparency",v);
    instance()->PreviewWithTransparency = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removePreviewWithTransparency() {
    instance()->handle->RemoveBool("PreviewWithTransparency");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docEditOnTop() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getEditOnTop() {
    return instance()->EditOnTop;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultEditOnTop() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setEditOnTop(const bool &v) {
    instance()->handle->SetBool("EditOnTop",v);
    instance()->EditOnTop = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeEditOnTop() {
    instance()->handle->RemoveBool("EditOnTop");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docEditRecomputeWait() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & PartParams::getEditRecomputeWait() {
    return instance()->EditRecomputeWait;
}

// Auto generated code (Tools/params_utils.py:388)
const long & PartParams::defaultEditRecomputeWait() {
    const static long def = 300;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setEditRecomputeWait(const long &v) {
    instance()->handle->SetInt("EditRecomputeWait",v);
    instance()->EditRecomputeWait = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeEditRecomputeWait() {
    instance()->handle->RemoveInt("EditRecomputeWait");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docAdjustCameraForNewFeature() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getAdjustCameraForNewFeature() {
    return instance()->AdjustCameraForNewFeature;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultAdjustCameraForNewFeature() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setAdjustCameraForNewFeature(const bool &v) {
    instance()->handle->SetBool("AdjustCameraForNewFeature",v);
    instance()->AdjustCameraForNewFeature = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeAdjustCameraForNewFeature() {
    instance()->handle->RemoveBool("AdjustCameraForNewFeature");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docDefaultDatumColor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & PartParams::getDefaultDatumColor() {
    return instance()->DefaultDatumColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & PartParams::defaultDefaultDatumColor() {
    const static unsigned long def = 0xFFD70066;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setDefaultDatumColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("DefaultDatumColor",v);
    instance()->DefaultDatumColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeDefaultDatumColor() {
    instance()->handle->RemoveUnsigned("DefaultDatumColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docRespectSystemDPI() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getRespectSystemDPI() {
    return instance()->RespectSystemDPI;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultRespectSystemDPI() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setRespectSystemDPI(const bool &v) {
    instance()->handle->SetBool("RespectSystemDPI",v);
    instance()->RespectSystemDPI = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeRespectSystemDPI() {
    instance()->handle->RemoveBool("RespectSystemDPI");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docShapeInstancing() {
    return QT_TRANSLATE_NOOP("PartParams",
"Share the tessellation of repeated sub-shapes (same TopoDS_TShape)\n"
"inside a compound and render them as GPU instances. Only takes\n"
"effect when the renderer supports instanced draws; otherwise the\n"
"geometry is flattened as before.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getShapeInstancing() {
    return instance()->ShapeInstancing;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultShapeInstancing() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setShapeInstancing(const bool &v) {
    instance()->handle->SetBool("ShapeInstancing",v);
    instance()->ShapeInstancing = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeShapeInstancing() {
    instance()->handle->RemoveBool("ShapeInstancing");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docSelectionPickThreshold() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & PartParams::getSelectionPickThreshold() {
    return instance()->SelectionPickThreshold;
}

// Auto generated code (Tools/params_utils.py:388)
const long & PartParams::defaultSelectionPickThreshold() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setSelectionPickThreshold(const long &v) {
    instance()->handle->SetInt("SelectionPickThreshold",v);
    instance()->SelectionPickThreshold = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeSelectionPickThreshold() {
    instance()->handle->RemoveInt("SelectionPickThreshold");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docSelectionPickThreshold2() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & PartParams::getSelectionPickThreshold2() {
    return instance()->SelectionPickThreshold2;
}

// Auto generated code (Tools/params_utils.py:388)
const long & PartParams::defaultSelectionPickThreshold2() {
    const static long def = 500;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setSelectionPickThreshold2(const long &v) {
    instance()->handle->SetInt("SelectionPickThreshold2",v);
    instance()->SelectionPickThreshold2 = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeSelectionPickThreshold2() {
    instance()->handle->RemoveInt("SelectionPickThreshold2");
}

// Auto generated code (Tools/params_utils.py:372)
const char *PartParams::docSelectionPickRTree() {
    return QT_TRANSLATE_NOOP("PartParams",
"Pick with a per-triangle R-tree instead of walking every\n"
"triangle of a part. Without it the only spatial filter is the\n"
"per-part bounding box, so a ray that reaches a dense part\n"
"sends all of its triangles through Coin's primitive callbacks:\n"
"on an imported mesh (one part carrying everything) a selecting\n"
"click cost 116 ms, and 18 ms with this on. The tree is built\n"
"lazily, per part, on the first pick that reaches it -- that\n"
"first pick pays about 15 ms more, every one after it is the\n"
"cheap one. Parts smaller than SelectionPickThreshold2 are\n"
"picked directly either way.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & PartParams::getSelectionPickRTree() {
    return instance()->SelectionPickRTree;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & PartParams::defaultSelectionPickRTree() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void PartParams::setSelectionPickRTree(const bool &v) {
    instance()->handle->SetBool("SelectionPickRTree",v);
    instance()->SelectionPickRTree = v;
}

// Auto generated code (Tools/params_utils.py:406)
void PartParams::removeSelectionPickRTree() {
    instance()->handle->RemoveBool("SelectionPickRTree");
}
//[[[end]]]

void PartParams::onMeshDeviationChanged() {
    getTimer().start(100);
}

void PartParams::onMeshAngularDeflectionChanged() {
    getTimer().start(100);
}

void PartParams::onMinimumDeviationChanged() {
    getTimer().start(100);
}

void PartParams::onMinimumAngularDeflectionChanged() {
    getTimer().start(100);
}

void PartParams::onOverrideTessellationChanged() {
    getTimer().start(100);
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
        if (reason && (strcmp(reason, "RenderCache") == 0
                       || strcmp(reason, "Type") == 0))
            getTimer().start(100);
    }
private:
    ParameterGrp::handle hView;
    ParameterGrp::handle hRender;
};
} // anonymous namespace

// Installed once from the ViewProviderPartExt constructor (declared
// locally there — the PartParams class body is cog-generated and takes
// no hand-written members).
void PartGui::initShapeInstancingGateObserver()
{
    static InstancingGateObserver observer;
}
