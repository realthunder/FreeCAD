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
    double EffectResolution;
    bool AO;
    bool Shadow;
    long AOMethod;
    long AOSlices;
    long AOSteps;
    double AORadius;
    double AOIntensity;
    double AOResolution;
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
    bool WaterSurface;
    double WaterWaveStrength;
    double WaterWaveScale;
    double WaterWaveSpeed;
    double WaterAbsorption;
    double WaterInscatter;
    bool WaterRefraction;
    bool WaterReflection;
    bool WaterPlanarReflection;
    bool WaterShadow;
    long WaterRippleType;
    double WaterRippleDensity;
    double WaterShadowWobble;
    bool Bloom;
    double BloomThreshold;
    double BloomIntensity;
    double BloomRadius;
    bool GroundReflection;
    double GroundReflectionIntensity;

    // Auto generated code (Tools/params_utils.py:253)
    RenderParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View/Render");
        handle->Attach(this);

        Type = this->handle->GetASCII("Type", "Default");
        funcs["Type"] = &RenderParamsP::updateType;
        EffectResolution = this->handle->GetFloat("EffectResolution", 1.0);
        funcs["EffectResolution"] = &RenderParamsP::updateEffectResolution;
        AO = this->handle->GetBool("AO", false);
        funcs["AO"] = &RenderParamsP::updateAO;
        Shadow = this->handle->GetBool("Shadow", true);
        funcs["Shadow"] = &RenderParamsP::updateShadow;
        AOMethod = this->handle->GetInt("AOMethod", 0);
        funcs["AOMethod"] = &RenderParamsP::updateAOMethod;
        AOSlices = this->handle->GetInt("AOSlices", 9);
        funcs["AOSlices"] = &RenderParamsP::updateAOSlices;
        AOSteps = this->handle->GetInt("AOSteps", 3);
        funcs["AOSteps"] = &RenderParamsP::updateAOSteps;
        AORadius = this->handle->GetFloat("AORadius", 0.0);
        funcs["AORadius"] = &RenderParamsP::updateAORadius;
        AOIntensity = this->handle->GetFloat("AOIntensity", 0.6);
        funcs["AOIntensity"] = &RenderParamsP::updateAOIntensity;
        AOResolution = this->handle->GetFloat("AOResolution", 1.0);
        funcs["AOResolution"] = &RenderParamsP::updateAOResolution;
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
        WaterSurface = this->handle->GetBool("WaterSurface", false);
        funcs["WaterSurface"] = &RenderParamsP::updateWaterSurface;
        WaterWaveStrength = this->handle->GetFloat("WaterWaveStrength", 0.3);
        funcs["WaterWaveStrength"] = &RenderParamsP::updateWaterWaveStrength;
        WaterWaveScale = this->handle->GetFloat("WaterWaveScale", 0.0);
        funcs["WaterWaveScale"] = &RenderParamsP::updateWaterWaveScale;
        WaterWaveSpeed = this->handle->GetFloat("WaterWaveSpeed", 1.0);
        funcs["WaterWaveSpeed"] = &RenderParamsP::updateWaterWaveSpeed;
        WaterAbsorption = this->handle->GetFloat("WaterAbsorption", 0.2);
        funcs["WaterAbsorption"] = &RenderParamsP::updateWaterAbsorption;
        WaterInscatter = this->handle->GetFloat("WaterInscatter", 0.5);
        funcs["WaterInscatter"] = &RenderParamsP::updateWaterInscatter;
        WaterRefraction = this->handle->GetBool("WaterRefraction", true);
        funcs["WaterRefraction"] = &RenderParamsP::updateWaterRefraction;
        WaterReflection = this->handle->GetBool("WaterReflection", true);
        funcs["WaterReflection"] = &RenderParamsP::updateWaterReflection;
        WaterPlanarReflection = this->handle->GetBool("WaterPlanarReflection", true);
        funcs["WaterPlanarReflection"] = &RenderParamsP::updateWaterPlanarReflection;
        WaterShadow = this->handle->GetBool("WaterShadow", true);
        funcs["WaterShadow"] = &RenderParamsP::updateWaterShadow;
        WaterRippleType = this->handle->GetInt("WaterRippleType", 0);
        funcs["WaterRippleType"] = &RenderParamsP::updateWaterRippleType;
        WaterRippleDensity = this->handle->GetFloat("WaterRippleDensity", 1.0);
        funcs["WaterRippleDensity"] = &RenderParamsP::updateWaterRippleDensity;
        WaterShadowWobble = this->handle->GetFloat("WaterShadowWobble", 1.0);
        funcs["WaterShadowWobble"] = &RenderParamsP::updateWaterShadowWobble;
        Bloom = this->handle->GetBool("Bloom", false);
        funcs["Bloom"] = &RenderParamsP::updateBloom;
        BloomThreshold = this->handle->GetFloat("BloomThreshold", 0.9);
        funcs["BloomThreshold"] = &RenderParamsP::updateBloomThreshold;
        BloomIntensity = this->handle->GetFloat("BloomIntensity", 1.0);
        funcs["BloomIntensity"] = &RenderParamsP::updateBloomIntensity;
        BloomRadius = this->handle->GetFloat("BloomRadius", 1.0);
        funcs["BloomRadius"] = &RenderParamsP::updateBloomRadius;
        GroundReflection = this->handle->GetBool("GroundReflection", false);
        funcs["GroundReflection"] = &RenderParamsP::updateGroundReflection;
        GroundReflectionIntensity = this->handle->GetFloat("GroundReflectionIntensity", 0.4);
        funcs["GroundReflectionIntensity"] = &RenderParamsP::updateGroundReflectionIntensity;
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
    static void updateEffectResolution(RenderParamsP *self) {
        self->EffectResolution = self->handle->GetFloat("EffectResolution", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAO(RenderParamsP *self) {
        self->AO = self->handle->GetBool("AO", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShadow(RenderParamsP *self) {
        self->Shadow = self->handle->GetBool("Shadow", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAOMethod(RenderParamsP *self) {
        self->AOMethod = self->handle->GetInt("AOMethod", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAOSlices(RenderParamsP *self) {
        self->AOSlices = self->handle->GetInt("AOSlices", 9);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAOSteps(RenderParamsP *self) {
        self->AOSteps = self->handle->GetInt("AOSteps", 3);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAORadius(RenderParamsP *self) {
        self->AORadius = self->handle->GetFloat("AORadius", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAOIntensity(RenderParamsP *self) {
        self->AOIntensity = self->handle->GetFloat("AOIntensity", 0.6);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAOResolution(RenderParamsP *self) {
        self->AOResolution = self->handle->GetFloat("AOResolution", 1.0);
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
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterSurface(RenderParamsP *self) {
        self->WaterSurface = self->handle->GetBool("WaterSurface", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterWaveStrength(RenderParamsP *self) {
        self->WaterWaveStrength = self->handle->GetFloat("WaterWaveStrength", 0.3);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterWaveScale(RenderParamsP *self) {
        self->WaterWaveScale = self->handle->GetFloat("WaterWaveScale", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterWaveSpeed(RenderParamsP *self) {
        self->WaterWaveSpeed = self->handle->GetFloat("WaterWaveSpeed", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterAbsorption(RenderParamsP *self) {
        self->WaterAbsorption = self->handle->GetFloat("WaterAbsorption", 0.2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterInscatter(RenderParamsP *self) {
        self->WaterInscatter = self->handle->GetFloat("WaterInscatter", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterRefraction(RenderParamsP *self) {
        self->WaterRefraction = self->handle->GetBool("WaterRefraction", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterReflection(RenderParamsP *self) {
        self->WaterReflection = self->handle->GetBool("WaterReflection", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterPlanarReflection(RenderParamsP *self) {
        self->WaterPlanarReflection = self->handle->GetBool("WaterPlanarReflection", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterShadow(RenderParamsP *self) {
        self->WaterShadow = self->handle->GetBool("WaterShadow", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterRippleType(RenderParamsP *self) {
        self->WaterRippleType = self->handle->GetInt("WaterRippleType", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterRippleDensity(RenderParamsP *self) {
        self->WaterRippleDensity = self->handle->GetFloat("WaterRippleDensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterShadowWobble(RenderParamsP *self) {
        self->WaterShadowWobble = self->handle->GetFloat("WaterShadowWobble", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBloom(RenderParamsP *self) {
        self->Bloom = self->handle->GetBool("Bloom", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBloomThreshold(RenderParamsP *self) {
        self->BloomThreshold = self->handle->GetFloat("BloomThreshold", 0.9);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBloomIntensity(RenderParamsP *self) {
        self->BloomIntensity = self->handle->GetFloat("BloomIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBloomRadius(RenderParamsP *self) {
        self->BloomRadius = self->handle->GetFloat("BloomRadius", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateGroundReflection(RenderParamsP *self) {
        self->GroundReflection = self->handle->GetBool("GroundReflection", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateGroundReflectionIntensity(RenderParamsP *self) {
        self->GroundReflectionIntensity = self->handle->GetFloat("GroundReflectionIntensity", 0.4);
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
const char *RenderParams::docEffectResolution() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Resolution scale (0.25-1.0) of the expensive screen-space effect\n"
"passes -- the planar/ground reflection scene re-render, the water\n"
"body depth prepass and screen-space ambient occlusion -- relative to\n"
"the main view resolution. Lowering it trades effect sharpness for\n"
"speed on large windows, where those per-pixel passes dominate the\n"
"frame; the main geometry, edges, text and overlays stay full\n"
"resolution. 1.0 renders the effects at full resolution. The\n"
"volumetric light shafts already render at half resolution.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getEffectResolution() {
    return instance()->EffectResolution;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultEffectResolution() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setEffectResolution(const double &v) {
    instance()->handle->SetFloat("EffectResolution",v);
    instance()->EffectResolution = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeEffectResolution() {
    instance()->handle->RemoveFloat("EffectResolution");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docAO() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable screen space ambient occlusion of the experimental render\n"
"engine (render cache mode 3 with a selected renderer type).");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getAO() {
    return instance()->AO;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultAO() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setAO(const bool &v) {
    instance()->handle->SetBool("AO",v);
    instance()->AO = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeAO() {
    instance()->handle->RemoveBool("AO");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docShadow() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Render the shadow map cast by the Shadow draw style's scene\n"
"light (and the god-ray shafts / caustic occlusion that depend on\n"
"it). A convenience switch to drop shadows without leaving the\n"
"Shadow draw style; the base headlight and environment lighting\n"
"stay, so the scene remains lit, just flatter. Has no effect unless\n"
"the Shadow draw style provides a scene light.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getShadow() {
    return instance()->Shadow;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultShadow() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setShadow(const bool &v) {
    instance()->handle->SetBool("Shadow",v);
    instance()->Shadow = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeShadow() {
    instance()->handle->RemoveBool("Shadow");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docAOMethod() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ambient occlusion algorithm. 0 = classic hemisphere-kernel\n"
"SSAO (screen-space depth-difference sampling). 1 = GTAO\n"
"(ground-truth ambient occlusion, XeGTAO-style horizon-based\n"
"visibility integration): physically correct occlusion falloff,\n"
"tight contact shadows without the wide low-contrast wash of\n"
"classic SSAO at large radii.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getAOMethod() {
    return instance()->AOMethod;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultAOMethod() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setAOMethod(const long &v) {
    instance()->handle->SetInt("AOMethod",v);
    instance()->AOMethod = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeAOMethod() {
    instance()->handle->RemoveInt("AOMethod");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docAOSlices() {
    return QT_TRANSLATE_NOOP("RenderParams",
"GTAO only: number of screen-space slice directions per pixel\n"
"(XeGTAO High preset = 9). The dominant quality/cost dial —\n"
"direction variance shows as blotchy grain the denoiser cannot\n"
"fully flatten. Cost scales linearly.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getAOSlices() {
    return instance()->AOSlices;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultAOSlices() {
    const static long def = 9;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setAOSlices(const long &v) {
    instance()->handle->SetInt("AOSlices",v);
    instance()->AOSlices = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeAOSlices() {
    instance()->handle->RemoveInt("AOSlices");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docAOSteps() {
    return QT_TRANSLATE_NOOP("RenderParams",
"GTAO only: horizon-march samples per slice side. More steps\n"
"resolve distant occluders more stably (less mid-frequency blotch\n"
"on grazing surfaces), at linear cost.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getAOSteps() {
    return instance()->AOSteps;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultAOSteps() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setAOSteps(const long &v) {
    instance()->handle->SetInt("AOSteps",v);
    instance()->AOSteps = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeAOSteps() {
    instance()->handle->RemoveInt("AOSteps");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docAORadius() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ambient occlusion sample radius in world units.\n"
"Zero means automatic (a fraction of the scene size).");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getAORadius() {
    return instance()->AORadius;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultAORadius() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setAORadius(const double &v) {
    instance()->handle->SetFloat("AORadius",v);
    instance()->AORadius = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeAORadius() {
    instance()->handle->RemoveFloat("AORadius");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docAOIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ambient occlusion darkening strength.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getAOIntensity() {
    return instance()->AOIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultAOIntensity() {
    const static double def = 0.6;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setAOIntensity(const double &v) {
    instance()->handle->SetFloat("AOIntensity",v);
    instance()->AOIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeAOIntensity() {
    instance()->handle->RemoveFloat("AOIntensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docAOResolution() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Resolution scale (0.25-1.0) of the ambient occlusion resolve\n"
"targets relative to the main view resolution, independent of the\n"
"shared Effect resolution. Ambient occlusion is resolution-sensitive\n"
"(contact and crevice detail), so it has its own control; the shared\n"
"Effect resolution drives only the costlier reflection re-render.\n"
"1.0 renders the occlusion at full resolution; lower trades AO\n"
"sharpness for speed.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getAOResolution() {
    return instance()->AOResolution;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultAOResolution() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setAOResolution(const double &v) {
    instance()->handle->SetFloat("AOResolution",v);
    instance()->AOResolution = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeAOResolution() {
    instance()->handle->RemoveFloat("AOResolution");
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

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterSurface() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Shade water bodies (objects with the Render_Water property)\n"
"as an animated water surface: screen-space refraction of the\n"
"scene behind it, Fresnel-blended environment reflection and a\n"
"sun glint from the Shadow draw style light.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getWaterSurface() {
    return instance()->WaterSurface;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultWaterSurface() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterSurface(const bool &v) {
    instance()->handle->SetBool("WaterSurface",v);
    instance()->WaterSurface = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterSurface() {
    instance()->handle->RemoveBool("WaterSurface");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterWaveStrength() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Amplitude of the animated wave perturbation of the water\n"
"surface normal; zero gives a flat mirror-like surface.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getWaterWaveStrength() {
    return instance()->WaterWaveStrength;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultWaterWaveStrength() {
    const static double def = 0.3;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterWaveStrength(const double &v) {
    instance()->handle->SetFloat("WaterWaveStrength",v);
    instance()->WaterWaveStrength = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterWaveStrength() {
    instance()->handle->RemoveFloat("WaterWaveStrength");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterWaveScale() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Wave frequency in inverse world units.\n"
"Zero means automatic (a fraction of the water body size).");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getWaterWaveScale() {
    return instance()->WaterWaveScale;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultWaterWaveScale() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterWaveScale(const double &v) {
    instance()->handle->SetFloat("WaterWaveScale",v);
    instance()->WaterWaveScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterWaveScale() {
    instance()->handle->RemoveFloat("WaterWaveScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterWaveSpeed() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Animation speed of the water surface waves; zero freezes\n"
"them.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getWaterWaveSpeed() {
    return instance()->WaterWaveSpeed;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultWaterWaveSpeed() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterWaveSpeed(const double &v) {
    instance()->handle->SetFloat("WaterWaveSpeed",v);
    instance()->WaterWaveSpeed = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterWaveSpeed() {
    instance()->handle->RemoveFloat("WaterWaveSpeed");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterAbsorption() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Beer-Lambert absorption strength of the water surface\n"
"refraction: the refracted scene is dimmed and tinted by the\n"
"water column it travels through (channels the water color lacks\n"
"are absorbed most), so the water gains body and the bottom\n"
"recedes with depth. Zero = crystal clear.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getWaterAbsorption() {
    return instance()->WaterAbsorption;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultWaterAbsorption() {
    const static double def = 0.2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterAbsorption(const double &v) {
    instance()->handle->SetFloat("WaterAbsorption",v);
    instance()->WaterAbsorption = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterAbsorption() {
    instance()->handle->RemoveFloat("WaterAbsorption");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterInscatter() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How much the water's own color is added back into the\n"
"depth-absorbed refraction (in-scattering); zero leaves absorbed\n"
"regions dark, one fills them with the water color.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getWaterInscatter() {
    return instance()->WaterInscatter;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultWaterInscatter() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterInscatter(const double &v) {
    instance()->handle->SetFloat("WaterInscatter",v);
    instance()->WaterInscatter = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterInscatter() {
    instance()->handle->RemoveFloat("WaterInscatter");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterRefraction() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Screen-space refraction of the scene behind the water\n"
"surface. When off the surface shows a flat water colour instead\n"
"of the see-through refracted scene.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getWaterRefraction() {
    return instance()->WaterRefraction;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultWaterRefraction() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterRefraction(const bool &v) {
    instance()->handle->SetBool("WaterRefraction",v);
    instance()->WaterRefraction = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterRefraction() {
    instance()->handle->RemoveBool("WaterRefraction");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterReflection() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Reflection on the water surface (Fresnel-blended). When off\n"
"the surface only refracts. See WaterPlanarReflection for the\n"
"reflection method.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getWaterReflection() {
    return instance()->WaterReflection;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultWaterReflection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterReflection(const bool &v) {
    instance()->handle->SetBool("WaterReflection",v);
    instance()->WaterReflection = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterReflection() {
    instance()->handle->RemoveBool("WaterReflection");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterPlanarReflection() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Reflection method when WaterReflection is on: planar (a\n"
"mirror-camera re-render of the scene about the water plane -\n"
"exact, no taper) when true, else screen-space reflection (a\n"
"cheaper per-pixel ray march that can only reflect on-screen\n"
"geometry and tapers past it). The environment cubemap is the\n"
"fallback for both.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getWaterPlanarReflection() {
    return instance()->WaterPlanarReflection;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultWaterPlanarReflection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterPlanarReflection(const bool &v) {
    instance()->handle->SetBool("WaterPlanarReflection",v);
    instance()->WaterPlanarReflection = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterPlanarReflection() {
    instance()->handle->RemoveBool("WaterPlanarReflection");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterShadow() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Receive the scene light's shadow on the water surface: a\n"
"shadow band on the water where a caster blocks the light and\n"
"the sun glint killed there. Requires the Shadow draw style\n"
"with an active shadow map; off leaves the surface fully lit.\n"
"The refracted scene below the surface keeps its own shadow\n"
"regardless.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getWaterShadow() {
    return instance()->WaterShadow;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultWaterShadow() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterShadow(const bool &v) {
    instance()->handle->SetBool("WaterShadow",v);
    instance()->WaterShadow = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterShadow() {
    instance()->handle->RemoveBool("WaterShadow");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterRippleType() {
    return QT_TRANSLATE_NOOP("RenderParams",
"The animated ripple pattern on the water surface. 0 = waves:\n"
"the default sum of directional wind waves. 1 = rain: circular\n"
"rings expanding from randomly placed, randomly timed drop\n"
"impacts, as on a pond in rainfall.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getWaterRippleType() {
    return instance()->WaterRippleType;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultWaterRippleType() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterRippleType(const long &v) {
    instance()->handle->SetInt("WaterRippleType",v);
    instance()->WaterRippleType = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterRippleType() {
    instance()->handle->RemoveInt("WaterRippleType");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterRippleDensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Drop density of the rain ripple type: how many drop cells\n"
"fit per wave-scale unit. Higher rains harder - more, smaller\n"
"rings; lower gives sparse large rings. The wave ripple type\n"
"ignores it.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getWaterRippleDensity() {
    return instance()->WaterRippleDensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultWaterRippleDensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterRippleDensity(const double &v) {
    instance()->handle->SetFloat("WaterRippleDensity",v);
    instance()->WaterRippleDensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterRippleDensity() {
    instance()->handle->RemoveFloat("WaterRippleDensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterShadowWobble() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How much the shadow band on the water surface wobbles with\n"
"the wave field: the shadow is looked up at the wave-displaced\n"
"surface point scaled by this factor. Zero pins the shadow\n"
"boundary to the flat surface (a straight edge), one is the\n"
"physical wave height, larger values exaggerate the ripple.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getWaterShadowWobble() {
    return instance()->WaterShadowWobble;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultWaterShadowWobble() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterShadowWobble(const double &v) {
    instance()->handle->SetFloat("WaterShadowWobble",v);
    instance()->WaterShadowWobble = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterShadowWobble() {
    instance()->handle->RemoveFloat("WaterShadowWobble");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docBloom() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Bleed a blurred glow halo from bright pixels and from\n"
"light-source bodies (objects with the Render_Light property)\n"
"over their surroundings.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getBloom() {
    return instance()->Bloom;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultBloom() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setBloom(const bool &v) {
    instance()->handle->SetBool("Bloom",v);
    instance()->Bloom = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeBloom() {
    instance()->handle->RemoveBool("Bloom");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docBloomThreshold() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Scene brightness above which a pixel feeds the glow halo\n"
"(with a soft knee below it). Light-source bodies always feed\n"
"it regardless, scaled by their intensity.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getBloomThreshold() {
    return instance()->BloomThreshold;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultBloomThreshold() {
    const static double def = 0.9;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setBloomThreshold(const double &v) {
    instance()->handle->SetFloat("BloomThreshold",v);
    instance()->BloomThreshold = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeBloomThreshold() {
    instance()->handle->RemoveFloat("BloomThreshold");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docBloomIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness multiplier of the composited glow halo.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getBloomIntensity() {
    return instance()->BloomIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultBloomIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setBloomIntensity(const double &v) {
    instance()->handle->SetFloat("BloomIntensity",v);
    instance()->BloomIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeBloomIntensity() {
    instance()->handle->RemoveFloat("BloomIntensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docBloomRadius() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Radius scale of the glow halo. One is the default gaussian\n"
"footprint; larger blooms wider.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getBloomRadius() {
    return instance()->BloomRadius;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultBloomRadius() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setBloomRadius(const double &v) {
    instance()->handle->SetFloat("BloomRadius",v);
    instance()->BloomRadius = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeBloomRadius() {
    instance()->handle->RemoveFloat("BloomRadius");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docGroundReflection() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Mirror the model in the shadow ground plane of the\n"
"experimental render engine: the opaque scene is re-rendered\n"
"with a reflected camera and blended onto the ground. Only\n"
"effective while the Shadow draw style shows a ground plane.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getGroundReflection() {
    return instance()->GroundReflection;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultGroundReflection() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setGroundReflection(const bool &v) {
    instance()->handle->SetBool("GroundReflection",v);
    instance()->GroundReflection = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeGroundReflection() {
    instance()->handle->RemoveBool("GroundReflection");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docGroundReflectionIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Blend factor of the mirrored model on the ground plane.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getGroundReflectionIntensity() {
    return instance()->GroundReflectionIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultGroundReflectionIntensity() {
    const static double def = 0.4;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setGroundReflectionIntensity(const double &v) {
    instance()->handle->SetFloat("GroundReflectionIntensity",v);
    instance()->GroundReflectionIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeGroundReflectionIntensity() {
    instance()->handle->RemoveFloat("GroundReflectionIntensity");
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
