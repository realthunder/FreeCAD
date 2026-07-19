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
#ifndef _PreComp_
# include <boost/algorithm/string/predicate.hpp>
#endif
#include <Inventor/SoRenderManager.h>
#include "Application.h"
#include "Document.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewParams.h"

/*[[[cog
import RenderParams
RenderParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include "RenderParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:209)
namespace {
class RenderParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(RenderParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::string Type;
    bool SSAO;
    double SSAORadius;
    double SSAOIntensity;
    bool PBR;
    double PBRMetallic;
    double PBRRoughness;
    double PBREnvIntensity;
    double BumpScale;
    bool Parallax;
    bool Volumetric;
    double VolumetricIntensity;
    double VolumetricDensity;
    bool Caustics;
    double CausticsIntensity;
    double CausticsScale;
    double CausticsSpeed;

    // Auto generated code (Tools/params_utils.py:253)
    RenderParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View/Render");
        handle->Attach(this);

        Type = this->handle->GetASCII("Type", "Default");
        funcs["Type"] = &RenderParamsP::updateType;
        SSAO = this->handle->GetBool("SSAO", false);
        funcs["SSAO"] = &RenderParamsP::updateSSAO;
        SSAORadius = this->handle->GetFloat("SSAORadius", 0.0);
        funcs["SSAORadius"] = &RenderParamsP::updateSSAORadius;
        SSAOIntensity = this->handle->GetFloat("SSAOIntensity", 1.0);
        funcs["SSAOIntensity"] = &RenderParamsP::updateSSAOIntensity;
        PBR = this->handle->GetBool("PBR", false);
        funcs["PBR"] = &RenderParamsP::updatePBR;
        PBRMetallic = this->handle->GetFloat("PBRMetallic", 0.0);
        funcs["PBRMetallic"] = &RenderParamsP::updatePBRMetallic;
        PBRRoughness = this->handle->GetFloat("PBRRoughness", 0.0);
        funcs["PBRRoughness"] = &RenderParamsP::updatePBRRoughness;
        PBREnvIntensity = this->handle->GetFloat("PBREnvIntensity", 1.0);
        funcs["PBREnvIntensity"] = &RenderParamsP::updatePBREnvIntensity;
        BumpScale = this->handle->GetFloat("BumpScale", 1.0);
        funcs["BumpScale"] = &RenderParamsP::updateBumpScale;
        Parallax = this->handle->GetBool("Parallax", true);
        funcs["Parallax"] = &RenderParamsP::updateParallax;
        Volumetric = this->handle->GetBool("Volumetric", false);
        funcs["Volumetric"] = &RenderParamsP::updateVolumetric;
        VolumetricIntensity = this->handle->GetFloat("VolumetricIntensity", 1.0);
        funcs["VolumetricIntensity"] = &RenderParamsP::updateVolumetricIntensity;
        VolumetricDensity = this->handle->GetFloat("VolumetricDensity", 0.0);
        funcs["VolumetricDensity"] = &RenderParamsP::updateVolumetricDensity;
        Caustics = this->handle->GetBool("Caustics", false);
        funcs["Caustics"] = &RenderParamsP::updateCaustics;
        CausticsIntensity = this->handle->GetFloat("CausticsIntensity", 1.0);
        funcs["CausticsIntensity"] = &RenderParamsP::updateCausticsIntensity;
        CausticsScale = this->handle->GetFloat("CausticsScale", 0.0);
        funcs["CausticsScale"] = &RenderParamsP::updateCausticsScale;
        CausticsSpeed = this->handle->GetFloat("CausticsSpeed", 1.0);
        funcs["CausticsSpeed"] = &RenderParamsP::updateCausticsSpeed;
    }

    // Auto generated code (Tools/params_utils.py:283)
    ~RenderParamsP() {
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
        
        RenderParams::onRenderParamChanged(sReason);
    }


    // Auto generated code (Tools/params_utils.py:310)
    static void updateType(RenderParamsP *self) {
        self->Type = self->handle->GetASCII("Type", "Default");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSSAO(RenderParamsP *self) {
        self->SSAO = self->handle->GetBool("SSAO", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSSAORadius(RenderParamsP *self) {
        self->SSAORadius = self->handle->GetFloat("SSAORadius", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSSAOIntensity(RenderParamsP *self) {
        self->SSAOIntensity = self->handle->GetFloat("SSAOIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePBR(RenderParamsP *self) {
        self->PBR = self->handle->GetBool("PBR", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePBRMetallic(RenderParamsP *self) {
        self->PBRMetallic = self->handle->GetFloat("PBRMetallic", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePBRRoughness(RenderParamsP *self) {
        self->PBRRoughness = self->handle->GetFloat("PBRRoughness", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePBREnvIntensity(RenderParamsP *self) {
        self->PBREnvIntensity = self->handle->GetFloat("PBREnvIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBumpScale(RenderParamsP *self) {
        self->BumpScale = self->handle->GetFloat("BumpScale", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateParallax(RenderParamsP *self) {
        self->Parallax = self->handle->GetBool("Parallax", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateVolumetric(RenderParamsP *self) {
        self->Volumetric = self->handle->GetBool("Volumetric", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateVolumetricIntensity(RenderParamsP *self) {
        self->VolumetricIntensity = self->handle->GetFloat("VolumetricIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateVolumetricDensity(RenderParamsP *self) {
        self->VolumetricDensity = self->handle->GetFloat("VolumetricDensity", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCaustics(RenderParamsP *self) {
        self->Caustics = self->handle->GetBool("Caustics", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCausticsIntensity(RenderParamsP *self) {
        self->CausticsIntensity = self->handle->GetFloat("CausticsIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCausticsScale(RenderParamsP *self) {
        self->CausticsScale = self->handle->GetFloat("CausticsScale", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCausticsSpeed(RenderParamsP *self) {
        self->CausticsSpeed = self->handle->GetFloat("CausticsSpeed", 1.0);
    }
};

// Auto generated code (Tools/params_utils.py:332)
RenderParamsP *instance() {
    static RenderParamsP *inst = new RenderParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:343)
ParameterGrp::handle RenderParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docType() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Type of the experimental render engine backend. 'Default' keeps\n"
"the plain GL pipeline. Only effective with render cache mode 3.");
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & RenderParams::getType() {
    return instance()->Type;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & RenderParams::defaultType() {
    const static std::string def = "Default";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setType(const std::string &v) {
    instance()->handle->SetASCII("Type",v);
    instance()->Type = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeType() {
    instance()->handle->RemoveASCII("Type");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docSSAO() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable screen space ambient occlusion of the experimental render\n"
"engine (render cache mode 3 with a selected renderer type).");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getSSAO() {
    return instance()->SSAO;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultSSAO() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setSSAO(const bool &v) {
    instance()->handle->SetBool("SSAO",v);
    instance()->SSAO = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeSSAO() {
    instance()->handle->RemoveBool("SSAO");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docSSAORadius() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ambient occlusion sample radius in world units.\n"
"Zero means automatic (a fraction of the scene size).");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getSSAORadius() {
    return instance()->SSAORadius;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultSSAORadius() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setSSAORadius(const double &v) {
    instance()->handle->SetFloat("SSAORadius",v);
    instance()->SSAORadius = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeSSAORadius() {
    instance()->handle->RemoveFloat("SSAORadius");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docSSAOIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ambient occlusion darkening strength.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getSSAOIntensity() {
    return instance()->SSAOIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultSSAOIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setSSAOIntensity(const double &v) {
    instance()->handle->SetFloat("SSAOIntensity",v);
    instance()->SSAOIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeSSAOIntensity() {
    instance()->handle->RemoveFloat("SSAOIntensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docPBR() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable physically based shading with image based lighting of\n"
"the experimental render engine (render cache mode 3 with a\n"
"selected renderer type). Replaces the default headlight shading\n"
"of lit surfaces with a metallic/roughness material lit by a\n"
"built-in studio environment.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getPBR() {
    return instance()->PBR;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultPBR() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBR(const bool &v) {
    instance()->handle->SetBool("PBR",v);
    instance()->PBR = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBR() {
    instance()->handle->RemoveBool("PBR");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docPBRMetallic() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Metalness of physically based shaded surfaces, 0 to 1.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getPBRMetallic() {
    return instance()->PBRMetallic;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultPBRMetallic() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBRMetallic(const double &v) {
    instance()->handle->SetFloat("PBRMetallic",v);
    instance()->PBRMetallic = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBRMetallic() {
    instance()->handle->RemoveFloat("PBRMetallic");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docPBRRoughness() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Roughness of physically based shaded surfaces, 0 to 1.\n"
"Zero means automatic (derived from each material's shininess).");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getPBRRoughness() {
    return instance()->PBRRoughness;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultPBRRoughness() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBRRoughness(const double &v) {
    instance()->handle->SetFloat("PBRRoughness",v);
    instance()->PBRRoughness = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBRRoughness() {
    instance()->handle->RemoveFloat("PBRRoughness");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docPBREnvIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness of the image based lighting environment.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getPBREnvIntensity() {
    return instance()->PBREnvIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultPBREnvIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBREnvIntensity(const double &v) {
    instance()->handle->SetFloat("PBREnvIntensity",v);
    instance()->PBREnvIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBREnvIntensity() {
    instance()->handle->RemoveFloat("PBREnvIntensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docBumpScale() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Strength of bump/normal mapped surfaces (SoBumpMap) of the\n"
"experimental render engine: scales the slope of normal maps and\n"
"the height amplitude of grayscale bump maps.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getBumpScale() {
    return instance()->BumpScale;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultBumpScale() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setBumpScale(const double &v) {
    instance()->handle->SetFloat("BumpScale",v);
    instance()->BumpScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeBumpScale() {
    instance()->handle->RemoveFloat("BumpScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docParallax() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Parallax-occlusion map grayscale bump maps (SoBumpMap) of the\n"
"experimental render engine, shifting the texture with the view\n"
"angle for a strong relief impression.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getParallax() {
    return instance()->Parallax;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultParallax() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setParallax(const bool &v) {
    instance()->handle->SetBool("Parallax",v);
    instance()->Parallax = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeParallax() {
    instance()->handle->RemoveBool("Parallax");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docVolumetric() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable volumetric lighting (light shafts) of the experimental\n"
"render engine: raymarch the shadow map of the Shadow draw style\n"
"through a homogeneous scattering medium. Only effective while\n"
"the Shadow draw style provides a scene light.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getVolumetric() {
    return instance()->Volumetric;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultVolumetric() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setVolumetric(const bool &v) {
    instance()->handle->SetBool("Volumetric",v);
    instance()->Volumetric = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeVolumetric() {
    instance()->handle->RemoveBool("Volumetric");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docVolumetricIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness of the inscattered (light shaft) light.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getVolumetricIntensity() {
    return instance()->VolumetricIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultVolumetricIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setVolumetricIntensity(const double &v) {
    instance()->handle->SetFloat("VolumetricIntensity",v);
    instance()->VolumetricIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeVolumetricIntensity() {
    instance()->handle->RemoveFloat("VolumetricIntensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docVolumetricDensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Scattering medium density in inverse world units.\n"
"Zero means automatic (a fraction of the scene size).");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getVolumetricDensity() {
    return instance()->VolumetricDensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultVolumetricDensity() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setVolumetricDensity(const double &v) {
    instance()->handle->SetFloat("VolumetricDensity",v);
    instance()->VolumetricDensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeVolumetricDensity() {
    instance()->handle->RemoveFloat("VolumetricDensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCaustics() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Project an animated caustic light pattern onto surfaces\n"
"below the water body (objects with the Render_Water property),\n"
"modulated by the shadow map. Only effective while volumetric\n"
"lighting and the Shadow draw style are active.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getCaustics() {
    return instance()->Caustics;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultCaustics() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCaustics(const bool &v) {
    instance()->handle->SetBool("Caustics",v);
    instance()->Caustics = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCaustics() {
    instance()->handle->RemoveBool("Caustics");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCausticsIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness of the projected caustic pattern.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getCausticsIntensity() {
    return instance()->CausticsIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultCausticsIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCausticsIntensity(const double &v) {
    instance()->handle->SetFloat("CausticsIntensity",v);
    instance()->CausticsIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCausticsIntensity() {
    instance()->handle->RemoveFloat("CausticsIntensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCausticsScale() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Caustic pattern cell frequency in inverse world units.\n"
"Zero means automatic (a fraction of the water body size).");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getCausticsScale() {
    return instance()->CausticsScale;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultCausticsScale() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCausticsScale(const double &v) {
    instance()->handle->SetFloat("CausticsScale",v);
    instance()->CausticsScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCausticsScale() {
    instance()->handle->RemoveFloat("CausticsScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCausticsSpeed() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Animation speed of the caustic pattern; zero freezes it.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getCausticsSpeed() {
    return instance()->CausticsSpeed;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultCausticsSpeed() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCausticsSpeed(const double &v) {
    instance()->handle->SetFloat("CausticsSpeed",v);
    instance()->CausticsSpeed = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCausticsSpeed() {
    instance()->handle->RemoveFloat("CausticsSpeed");
}
//[[[end]]]

namespace {

template<class FuncT>
void foreach3DViewer(FuncT func) {
    if (!Gui::Application::Instance)
        return;
    for (auto doc : App::GetApplication().getDocuments()) {
        if (auto gdoc = Gui::Application::Instance->getDocument(doc)) {
            gdoc->foreachView<Gui::View3DInventor>([&func](Gui::View3DInventor *view) {
                if (auto viewer = view->getViewer())
                    func(viewer);
            });
        }
    }
}

} // anonymous namespace

void RenderParams::onRenderParamChanged(const char *sReason)
{
    if (boost::equals(sReason, "Type")) {
        // Re-select the renderer backend on all 3D views. Same rule as
        // View3DSettings: the experimental backend only runs in render
        // cache mode 3; any other mode keeps the plain GL pipeline.
        std::string type = ViewParams::getRenderCache() == 3
            ? getType() : std::string();
        foreach3DViewer([&type](Gui::View3DInventorViewer *viewer) {
            viewer->setRendererType(type);
        });
        return;
    }
    // Every other parameter is re-fed to the backend each frame; a redraw
    // is enough to make the change visible immediately.
    foreach3DViewer([](Gui::View3DInventorViewer *viewer) {
        viewer->getSoRenderManager()->scheduleRedraw();
    });
}

void RenderParams::migrate()
{
    // One-time migration of the pre-split RendererType key: it lived in
    // the parent Preferences/View group (saved by the 3D view preference
    // page) before this child group existed. The other render engine
    // parameters never shipped outside this group, so only this one key
    // needs to move. It is copied to its new name and removed from the
    // old group, so the migration never repeats (and never overrides
    // later changes made here).
    auto hView = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View");
    for (const auto &v : hView->GetASCIIMap()) {
        if (v.first == "RendererType") {
            getHandle()->SetASCII("Type", v.second);
            hView->RemoveASCII("RendererType");
            break;
        }
    }
}
