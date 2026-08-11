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
#include "Renderer/SceneServer.h"
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
    long CoarseTessellation;
    long CoarseDeferFaces;
    bool ProgressiveLoad;
    long ProgressiveLoadBudgetMS;
    long LevelThreads;
    long LevelMemoryFloorMB;
    long GpuMemoryBudgetMB;
    double LevelTolerance;
    double EffectResolution;
    bool Occlusion;
    long OcclusionVisibleTtl;
    long OcclusionBudget;
    long OcclusionMinSubtree;
    long OcclusionMaxHidden;
    long OcclusionDepthPad;
    long OcclusionConfirm;
    bool OcclusionSoftware;
    long OcclusionOccluderTris;
    long OcclusionMinOccluder;
    long OcclusionThreads;
    bool OcclusionSimd;
    long OcclusionResolution;
    bool OcclusionPerInstance;
    bool OcclusionCoarse;
    long OcclusionCoarseLevel;
    long OcclusionCoarseMinTris;
    long OcclusionCoarseBuilds;
    long OcclusionCoarseBias;
    long OcclusionCoarseMemory;
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
    std::string PBREnvImage;
    bool PBREnvEmbed;
    bool PBREnvBackground;
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
    double WaterImpactStrength;
    double WaterImpactLife;
    double WaterShadowWobble;
    bool Bloom;
    double BloomThreshold;
    double BloomIntensity;
    double BloomRadius;
    bool SunDisc;
    double SunDiscSize;
    bool GroundReflection;
    double GroundReflectionIntensity;
    long DebugViewMode;
    bool DebugFreezeFrame;
    bool DebugLabel;
    bool DebugTiming;
    bool DebugDelta;
    bool DebugCoverage;
    bool DebugProxyCut;
    bool DebugOcclusion;
    bool DebugProxyGen;
    bool DebugCullAudit;

    // Auto generated code (Tools/params_utils.py:253)
    RenderParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View/Render");
        handle->Attach(this);

        Type = this->handle->GetASCII("Type", "Default");
        funcs["Type"] = &RenderParamsP::updateType;
        CoarseTessellation = this->handle->GetInt("CoarseTessellation", 2);
        funcs["CoarseTessellation"] = &RenderParamsP::updateCoarseTessellation;
        CoarseDeferFaces = this->handle->GetInt("CoarseDeferFaces", 1000);
        funcs["CoarseDeferFaces"] = &RenderParamsP::updateCoarseDeferFaces;
        ProgressiveLoad = this->handle->GetBool("ProgressiveLoad", true);
        funcs["ProgressiveLoad"] = &RenderParamsP::updateProgressiveLoad;
        ProgressiveLoadBudgetMS = this->handle->GetInt("ProgressiveLoadBudgetMS", 100);
        funcs["ProgressiveLoadBudgetMS"] = &RenderParamsP::updateProgressiveLoadBudgetMS;
        LevelThreads = this->handle->GetInt("LevelThreads", 0);
        funcs["LevelThreads"] = &RenderParamsP::updateLevelThreads;
        LevelMemoryFloorMB = this->handle->GetInt("LevelMemoryFloorMB", 0);
        funcs["LevelMemoryFloorMB"] = &RenderParamsP::updateLevelMemoryFloorMB;
        GpuMemoryBudgetMB = this->handle->GetInt("GpuMemoryBudgetMB", 0);
        funcs["GpuMemoryBudgetMB"] = &RenderParamsP::updateGpuMemoryBudgetMB;
        LevelTolerance = this->handle->GetFloat("LevelTolerance", 2.0);
        funcs["LevelTolerance"] = &RenderParamsP::updateLevelTolerance;
        EffectResolution = this->handle->GetFloat("EffectResolution", 1.0);
        funcs["EffectResolution"] = &RenderParamsP::updateEffectResolution;
        Occlusion = this->handle->GetBool("Occlusion", false);
        funcs["Occlusion"] = &RenderParamsP::updateOcclusion;
        OcclusionVisibleTtl = this->handle->GetInt("OcclusionVisibleTtl", 6);
        funcs["OcclusionVisibleTtl"] = &RenderParamsP::updateOcclusionVisibleTtl;
        OcclusionBudget = this->handle->GetInt("OcclusionBudget", 128);
        funcs["OcclusionBudget"] = &RenderParamsP::updateOcclusionBudget;
        OcclusionMinSubtree = this->handle->GetInt("OcclusionMinSubtree", 8);
        funcs["OcclusionMinSubtree"] = &RenderParamsP::updateOcclusionMinSubtree;
        OcclusionMaxHidden = this->handle->GetInt("OcclusionMaxHidden", 120);
        funcs["OcclusionMaxHidden"] = &RenderParamsP::updateOcclusionMaxHidden;
        OcclusionDepthPad = this->handle->GetInt("OcclusionDepthPad", 16);
        funcs["OcclusionDepthPad"] = &RenderParamsP::updateOcclusionDepthPad;
        OcclusionConfirm = this->handle->GetInt("OcclusionConfirm", 2);
        funcs["OcclusionConfirm"] = &RenderParamsP::updateOcclusionConfirm;
        OcclusionSoftware = this->handle->GetBool("OcclusionSoftware", false);
        funcs["OcclusionSoftware"] = &RenderParamsP::updateOcclusionSoftware;
        OcclusionOccluderTris = this->handle->GetInt("OcclusionOccluderTris", 250000);
        funcs["OcclusionOccluderTris"] = &RenderParamsP::updateOcclusionOccluderTris;
        OcclusionMinOccluder = this->handle->GetInt("OcclusionMinOccluder", 24);
        funcs["OcclusionMinOccluder"] = &RenderParamsP::updateOcclusionMinOccluder;
        OcclusionThreads = this->handle->GetInt("OcclusionThreads", 0);
        funcs["OcclusionThreads"] = &RenderParamsP::updateOcclusionThreads;
        OcclusionSimd = this->handle->GetBool("OcclusionSimd", true);
        funcs["OcclusionSimd"] = &RenderParamsP::updateOcclusionSimd;
        OcclusionResolution = this->handle->GetInt("OcclusionResolution", 1);
        funcs["OcclusionResolution"] = &RenderParamsP::updateOcclusionResolution;
        OcclusionPerInstance = this->handle->GetBool("OcclusionPerInstance", true);
        funcs["OcclusionPerInstance"] = &RenderParamsP::updateOcclusionPerInstance;
        OcclusionCoarse = this->handle->GetBool("OcclusionCoarse", false);
        funcs["OcclusionCoarse"] = &RenderParamsP::updateOcclusionCoarse;
        OcclusionCoarseLevel = this->handle->GetInt("OcclusionCoarseLevel", 2);
        funcs["OcclusionCoarseLevel"] = &RenderParamsP::updateOcclusionCoarseLevel;
        OcclusionCoarseMinTris = this->handle->GetInt("OcclusionCoarseMinTris", 512);
        funcs["OcclusionCoarseMinTris"] = &RenderParamsP::updateOcclusionCoarseMinTris;
        OcclusionCoarseBuilds = this->handle->GetInt("OcclusionCoarseBuilds", 8);
        funcs["OcclusionCoarseBuilds"] = &RenderParamsP::updateOcclusionCoarseBuilds;
        OcclusionCoarseBias = this->handle->GetInt("OcclusionCoarseBias", 100);
        funcs["OcclusionCoarseBias"] = &RenderParamsP::updateOcclusionCoarseBias;
        OcclusionCoarseMemory = this->handle->GetInt("OcclusionCoarseMemory", 64);
        funcs["OcclusionCoarseMemory"] = &RenderParamsP::updateOcclusionCoarseMemory;
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
        PBREnvImage = this->handle->GetASCII("PBREnvImage", "");
        funcs["PBREnvImage"] = &RenderParamsP::updatePBREnvImage;
        PBREnvEmbed = this->handle->GetBool("PBREnvEmbed", false);
        funcs["PBREnvEmbed"] = &RenderParamsP::updatePBREnvEmbed;
        PBREnvBackground = this->handle->GetBool("PBREnvBackground", false);
        funcs["PBREnvBackground"] = &RenderParamsP::updatePBREnvBackground;
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
        WaterImpactStrength = this->handle->GetFloat("WaterImpactStrength", 1.0);
        funcs["WaterImpactStrength"] = &RenderParamsP::updateWaterImpactStrength;
        WaterImpactLife = this->handle->GetFloat("WaterImpactLife", 1.1);
        funcs["WaterImpactLife"] = &RenderParamsP::updateWaterImpactLife;
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
        SunDisc = this->handle->GetBool("SunDisc", false);
        funcs["SunDisc"] = &RenderParamsP::updateSunDisc;
        SunDiscSize = this->handle->GetFloat("SunDiscSize", 1.5);
        funcs["SunDiscSize"] = &RenderParamsP::updateSunDiscSize;
        GroundReflection = this->handle->GetBool("GroundReflection", false);
        funcs["GroundReflection"] = &RenderParamsP::updateGroundReflection;
        GroundReflectionIntensity = this->handle->GetFloat("GroundReflectionIntensity", 0.4);
        funcs["GroundReflectionIntensity"] = &RenderParamsP::updateGroundReflectionIntensity;
        DebugViewMode = this->handle->GetInt("DebugViewMode", 0);
        funcs["DebugViewMode"] = &RenderParamsP::updateDebugViewMode;
        DebugFreezeFrame = this->handle->GetBool("DebugFreezeFrame", false);
        funcs["DebugFreezeFrame"] = &RenderParamsP::updateDebugFreezeFrame;
        DebugLabel = this->handle->GetBool("DebugLabel", false);
        funcs["DebugLabel"] = &RenderParamsP::updateDebugLabel;
        DebugTiming = this->handle->GetBool("DebugTiming", false);
        funcs["DebugTiming"] = &RenderParamsP::updateDebugTiming;
        DebugDelta = this->handle->GetBool("DebugDelta", false);
        funcs["DebugDelta"] = &RenderParamsP::updateDebugDelta;
        DebugCoverage = this->handle->GetBool("DebugCoverage", false);
        funcs["DebugCoverage"] = &RenderParamsP::updateDebugCoverage;
        DebugProxyCut = this->handle->GetBool("DebugProxyCut", false);
        funcs["DebugProxyCut"] = &RenderParamsP::updateDebugProxyCut;
        DebugOcclusion = this->handle->GetBool("DebugOcclusion", false);
        funcs["DebugOcclusion"] = &RenderParamsP::updateDebugOcclusion;
        DebugProxyGen = this->handle->GetBool("DebugProxyGen", false);
        funcs["DebugProxyGen"] = &RenderParamsP::updateDebugProxyGen;
        DebugCullAudit = this->handle->GetBool("DebugCullAudit", false);
        funcs["DebugCullAudit"] = &RenderParamsP::updateDebugCullAudit;
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
    static void updateCoarseTessellation(RenderParamsP *self) {
        self->CoarseTessellation = self->handle->GetInt("CoarseTessellation", 2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCoarseDeferFaces(RenderParamsP *self) {
        self->CoarseDeferFaces = self->handle->GetInt("CoarseDeferFaces", 1000);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateProgressiveLoad(RenderParamsP *self) {
        self->ProgressiveLoad = self->handle->GetBool("ProgressiveLoad", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateProgressiveLoadBudgetMS(RenderParamsP *self) {
        self->ProgressiveLoadBudgetMS = self->handle->GetInt("ProgressiveLoadBudgetMS", 100);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelThreads(RenderParamsP *self) {
        self->LevelThreads = self->handle->GetInt("LevelThreads", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelMemoryFloorMB(RenderParamsP *self) {
        self->LevelMemoryFloorMB = self->handle->GetInt("LevelMemoryFloorMB", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateGpuMemoryBudgetMB(RenderParamsP *self) {
        self->GpuMemoryBudgetMB = self->handle->GetInt("GpuMemoryBudgetMB", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelTolerance(RenderParamsP *self) {
        self->LevelTolerance = self->handle->GetFloat("LevelTolerance", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEffectResolution(RenderParamsP *self) {
        self->EffectResolution = self->handle->GetFloat("EffectResolution", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusion(RenderParamsP *self) {
        self->Occlusion = self->handle->GetBool("Occlusion", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionVisibleTtl(RenderParamsP *self) {
        self->OcclusionVisibleTtl = self->handle->GetInt("OcclusionVisibleTtl", 6);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionBudget(RenderParamsP *self) {
        self->OcclusionBudget = self->handle->GetInt("OcclusionBudget", 128);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionMinSubtree(RenderParamsP *self) {
        self->OcclusionMinSubtree = self->handle->GetInt("OcclusionMinSubtree", 8);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionMaxHidden(RenderParamsP *self) {
        self->OcclusionMaxHidden = self->handle->GetInt("OcclusionMaxHidden", 120);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionDepthPad(RenderParamsP *self) {
        self->OcclusionDepthPad = self->handle->GetInt("OcclusionDepthPad", 16);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionConfirm(RenderParamsP *self) {
        self->OcclusionConfirm = self->handle->GetInt("OcclusionConfirm", 2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionSoftware(RenderParamsP *self) {
        self->OcclusionSoftware = self->handle->GetBool("OcclusionSoftware", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionOccluderTris(RenderParamsP *self) {
        self->OcclusionOccluderTris = self->handle->GetInt("OcclusionOccluderTris", 250000);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionMinOccluder(RenderParamsP *self) {
        self->OcclusionMinOccluder = self->handle->GetInt("OcclusionMinOccluder", 24);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionThreads(RenderParamsP *self) {
        self->OcclusionThreads = self->handle->GetInt("OcclusionThreads", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionSimd(RenderParamsP *self) {
        self->OcclusionSimd = self->handle->GetBool("OcclusionSimd", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionResolution(RenderParamsP *self) {
        self->OcclusionResolution = self->handle->GetInt("OcclusionResolution", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionPerInstance(RenderParamsP *self) {
        self->OcclusionPerInstance = self->handle->GetBool("OcclusionPerInstance", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionCoarse(RenderParamsP *self) {
        self->OcclusionCoarse = self->handle->GetBool("OcclusionCoarse", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionCoarseLevel(RenderParamsP *self) {
        self->OcclusionCoarseLevel = self->handle->GetInt("OcclusionCoarseLevel", 2);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionCoarseMinTris(RenderParamsP *self) {
        self->OcclusionCoarseMinTris = self->handle->GetInt("OcclusionCoarseMinTris", 512);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionCoarseBuilds(RenderParamsP *self) {
        self->OcclusionCoarseBuilds = self->handle->GetInt("OcclusionCoarseBuilds", 8);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionCoarseBias(RenderParamsP *self) {
        self->OcclusionCoarseBias = self->handle->GetInt("OcclusionCoarseBias", 100);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOcclusionCoarseMemory(RenderParamsP *self) {
        self->OcclusionCoarseMemory = self->handle->GetInt("OcclusionCoarseMemory", 64);
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
    static void updatePBREnvImage(RenderParamsP *self) {
        self->PBREnvImage = self->handle->GetASCII("PBREnvImage", "");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePBREnvEmbed(RenderParamsP *self) {
        self->PBREnvEmbed = self->handle->GetBool("PBREnvEmbed", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePBREnvBackground(RenderParamsP *self) {
        self->PBREnvBackground = self->handle->GetBool("PBREnvBackground", false);
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
    static void updateWaterImpactStrength(RenderParamsP *self) {
        self->WaterImpactStrength = self->handle->GetFloat("WaterImpactStrength", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWaterImpactLife(RenderParamsP *self) {
        self->WaterImpactLife = self->handle->GetFloat("WaterImpactLife", 1.1);
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
    static void updateSunDisc(RenderParamsP *self) {
        self->SunDisc = self->handle->GetBool("SunDisc", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSunDiscSize(RenderParamsP *self) {
        self->SunDiscSize = self->handle->GetFloat("SunDiscSize", 1.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateGroundReflection(RenderParamsP *self) {
        self->GroundReflection = self->handle->GetBool("GroundReflection", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateGroundReflectionIntensity(RenderParamsP *self) {
        self->GroundReflectionIntensity = self->handle->GetFloat("GroundReflectionIntensity", 0.4);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugViewMode(RenderParamsP *self) {
        self->DebugViewMode = self->handle->GetInt("DebugViewMode", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugFreezeFrame(RenderParamsP *self) {
        self->DebugFreezeFrame = self->handle->GetBool("DebugFreezeFrame", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugLabel(RenderParamsP *self) {
        self->DebugLabel = self->handle->GetBool("DebugLabel", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugTiming(RenderParamsP *self) {
        self->DebugTiming = self->handle->GetBool("DebugTiming", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugDelta(RenderParamsP *self) {
        self->DebugDelta = self->handle->GetBool("DebugDelta", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugCoverage(RenderParamsP *self) {
        self->DebugCoverage = self->handle->GetBool("DebugCoverage", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugProxyCut(RenderParamsP *self) {
        self->DebugProxyCut = self->handle->GetBool("DebugProxyCut", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugOcclusion(RenderParamsP *self) {
        self->DebugOcclusion = self->handle->GetBool("DebugOcclusion", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugProxyGen(RenderParamsP *self) {
        self->DebugProxyGen = self->handle->GetBool("DebugProxyGen", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugCullAudit(RenderParamsP *self) {
        self->DebugCullAudit = self->handle->GetBool("DebugCullAudit", false);
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
const char *RenderParams::docCoarseTessellation() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ladder level shapes are tessellated at under coarse-first\n"
"(docs/SceneStreaming.md #7): the display mesh is built at this\n"
"rung of the fidelity ladder and the exact tessellation is\n"
"declared unbuilt, generated on demand when a camera asks for it.\n"
"0 is the coarsest rung, each level halves the error; -1 always\n"
"tessellates exact up front (pre-ladder behavior).\n"
"\n"
"Consulted whenever something can deliver the exact rung on\n"
"demand, which is a scene stream server (its viewers ask) OR a\n"
"desktop view in render cache mode 3 whose backend drives mesh\n"
"levels (its own level plan asks when the camera settles) - see\n"
"PartGui::coarseTessellationLevel. Plain Coin display has neither\n"
"and keeps the exact tessellation, because a coarse build there\n"
"would stay coarse forever. This is not a serving-only feature,\n"
"and it does engage for geometry built while a document loads.\n"
"\n"
"What a serving process lacks is not this setting but the local\n"
"level plan, which is disabled there - its rungs refine only where\n"
"a connected viewer's camera asks, so with no viewer attached they\n"
"stay coarse, while a desktop view refines its own. That, not the\n"
"setting, is why the two publish different geometry for one\n"
"document (measured on a 40-object scene: 8310 vertices serving,\n"
"25595 on the desktop). Desktop refinement is tolerance-limited,\n"
"so what it settles at is a property of the framing.\n"
"\n"
"A level whose rung is already finer than a shape's exact\n"
"tessellation coarsens nothing, so on small shapes the low levels\n"
"do nothing visible - the default 2 is a no-op on a scene of small\n"
"ellipsoids that level 0 visibly coarsens.\n"
"\n"
"The FC_COARSE_TESSELLATION environment variable overrides this for\n"
"a whole process and returns before the gate is evaluated, so it\n"
"forces coarse-first on where the gate would have refused. Takes\n"
"effect when a shape (re)tessellates.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getCoarseTessellation() {
    return instance()->CoarseTessellation;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultCoarseTessellation() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCoarseTessellation(const long &v) {
    instance()->handle->SetInt("CoarseTessellation",v);
    instance()->CoarseTessellation = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCoarseTessellation() {
    instance()->handle->RemoveInt("CoarseTessellation");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCoarseDeferFaces() {
    return QT_TRANSLATE_NOOP("RenderParams",
"During a progressive import on the bgfx renderer, a shape with\n"
"more faces than this gets a bounding-box stand-in immediately and\n"
"even its coarse tessellation is built on the refine worker pool,\n"
"swapped in when it arrives (docs/SceneStreaming.md #13) - the\n"
"import stall otherwise scales with the largest single part. -1\n"
"disables the stand-in so every shape tessellates inline.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getCoarseDeferFaces() {
    return instance()->CoarseDeferFaces;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultCoarseDeferFaces() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCoarseDeferFaces(const long &v) {
    instance()->handle->SetInt("CoarseDeferFaces",v);
    instance()->CoarseDeferFaces = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCoarseDeferFaces() {
    instance()->handle->RemoveInt("CoarseDeferFaces");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docProgressiveLoad() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Build the visual representation of a restored document after\n"
"the load instead of inline inside it. Opening a large document\n"
"otherwise tessellates every shape on the main thread while\n"
"nothing paints - the visual build is the largest single stage of\n"
"a load. Deferred, the window comes up first and the parts appear\n"
"in bounded slices with the view painting between them. Read as\n"
"each restored shape asks for its visual.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getProgressiveLoad() {
    return instance()->ProgressiveLoad;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultProgressiveLoad() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setProgressiveLoad(const bool &v) {
    instance()->handle->SetBool("ProgressiveLoad",v);
    instance()->ProgressiveLoad = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeProgressiveLoad() {
    instance()->handle->RemoveBool("ProgressiveLoad");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docProgressiveLoadBudgetMS() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How long one slice of deferred visual building may run before\n"
"returning to the event loop, when Progressive document load is\n"
"on. Larger finishes the document sooner, smaller keeps the window\n"
"more responsive while it fills in. Each slice is paid for with a\n"
"repaint of a large scene, which is why slices this long are worth\n"
"it - much smaller and the fill is paced by redraws rather than by\n"
"the building. Read at each slice.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getProgressiveLoadBudgetMS() {
    return instance()->ProgressiveLoadBudgetMS;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultProgressiveLoadBudgetMS() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setProgressiveLoadBudgetMS(const long &v) {
    instance()->handle->SetInt("ProgressiveLoadBudgetMS",v);
    instance()->ProgressiveLoadBudgetMS = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeProgressiveLoadBudgetMS() {
    instance()->handle->RemoveInt("ProgressiveLoadBudgetMS");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelThreads() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many mesh level builds (the scene server's on-demand\n"
"re-tessellations, docs/SceneStreaming.md #7) may run at once.\n"
"0 sizes the pool automatically - modest, because each BRepMesh\n"
"build already parallelizes internally over OCCT's shared thread\n"
"pool. The FC_LEVEL_THREADS environment variable overrides it.\n"
"Read when the server spawns its first level worker.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getLevelThreads() {
    return instance()->LevelThreads;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultLevelThreads() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelThreads(const long &v) {
    instance()->handle->SetInt("LevelThreads",v);
    instance()->LevelThreads = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelThreads() {
    instance()->handle->RemoveInt("LevelThreads");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelMemoryFloorMB() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Available system memory below which an exact re-tessellation\n"
"will not start (docs/SceneStreaming.md #13): the desktop refine\n"
"worker checks the system's own estimate of allocatable memory\n"
"before each exact build, and dropping under this floor counts as\n"
"a memory-ceiling observation - the same as a caught allocation\n"
"failure - after which the level plans also demote exact meshes\n"
"the camera would not miss back to their resident coarse rung.\n"
"0 sizes the floor automatically (at least 512 MB, or 1/16 of\n"
"physical memory if that is more). Read when the first refine is\n"
"queued.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getLevelMemoryFloorMB() {
    return instance()->LevelMemoryFloorMB;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultLevelMemoryFloorMB() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelMemoryFloorMB(const long &v) {
    instance()->handle->SetInt("LevelMemoryFloorMB",v);
    instance()->LevelMemoryFloorMB = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelMemoryFloorMB() {
    instance()->handle->RemoveInt("LevelMemoryFloorMB");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docGpuMemoryBudgetMB() {
    return QT_TRANSLATE_NOOP("RenderParams",
"GPU geometry budget of the desktop mesh-level plan\n"
"(docs/SceneStreaming.md #13): while the uploaded geometry exceeds\n"
"it, a camera pause downgrades the *displayed* mesh of objects the\n"
"camera would not miss - off screen, or coarse within half the\n"
"Level tolerance - back to their coarse rung. Their exact meshes\n"
"stay in CPU RAM, so zooming back in re-activates them instantly,\n"
"with no re-tessellation. 0 means automatic: the graphics API's\n"
"own reported GPU memory limit where it states one (Direct3D and\n"
"Vulkan do; OpenGL reports nothing, and then no budget applies).");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getGpuMemoryBudgetMB() {
    return instance()->GpuMemoryBudgetMB;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultGpuMemoryBudgetMB() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setGpuMemoryBudgetMB(const long &v) {
    instance()->handle->SetInt("GpuMemoryBudgetMB",v);
    instance()->GpuMemoryBudgetMB = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeGpuMemoryBudgetMB() {
    instance()->handle->RemoveInt("GpuMemoryBudgetMB");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelTolerance() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Screen-space error, in pixels, a coarse tessellation may\n"
"commit before the exact one is built (docs/SceneStreaming.md\n"
"#13): on a coarse-first desktop view (render cache mode 3 with\n"
"a backend that drives the level plan), a camera pause re-plans\n"
"the scene and only objects whose coarse mesh errs by more than\n"
"this many pixels on screen re-tessellate exactly - off-screen\n"
"and distant objects stay at the cheap coarse mesh until the\n"
"camera makes them matter. 0 or less refines everything\n"
"immediately; larger keeps more of the scene coarse. The\n"
"streamed viewer's own tolerance is its lodpx URL parameter\n"
"(same meaning, same default).");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLevelTolerance() {
    return instance()->LevelTolerance;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLevelTolerance() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelTolerance(const double &v) {
    instance()->handle->SetFloat("LevelTolerance",v);
    instance()->LevelTolerance = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelTolerance() {
    instance()->handle->RemoveFloat("LevelTolerance");
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
const char *RenderParams::docOcclusion() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Skip drawing what the depth buffer proves could not have\n"
"reached the screen (docs/FarFieldProxies.md §12). Bounding boxes\n"
"of the spatial index's nodes are tested against the finished\n"
"opaque depth under hardware occlusion queries, and a node that\n"
"puts no pixel through has its whole subtree skipped on the\n"
"following frames -- one test standing for thousands of draws.\n"
"\n"
"Exact, not approximate: only geometry that could not have been\n"
"seen is removed, so the image is unchanged and what is saved is\n"
"the draw call, which measures ~1.2-1.5us of CPU submission plus\n"
"~1.5-1.7us of GPU time whatever it contains (§10.2). It pays on\n"
"assemblies that hide themselves -- an enclosed chassis, a\n"
"populated rack, any interior -- and does nothing for a model\n"
"that is mostly silhouette. Expect roughly a fifth of the draws\n"
"from a camera inside a large assembly (§10.3); the far larger\n"
"figure from outside a closed model is a bound, not a promise.\n"
"\n"
"Casters and reflections are judged separately: geometry hidden\n"
"from the eye still casts its shadow and still appears in the\n"
"ground reflection.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getOcclusion() {
    return instance()->Occlusion;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultOcclusion() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusion(const bool &v) {
    instance()->handle->SetBool("Occlusion",v);
    instance()->Occlusion = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusion() {
    instance()->handle->RemoveBool("Occlusion");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionVisibleTtl() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many frames a node found visible is believed before it is\n"
"tested again. Higher spends fewer queries and keeps drawing\n"
"geometry that has since become hidden for a little longer; lower\n"
"tracks the camera more closely at the cost of more tests. Purely\n"
"a cost trade -- being late here draws too much, never too\n"
"little, so it cannot affect the image.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionVisibleTtl() {
    return instance()->OcclusionVisibleTtl;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionVisibleTtl() {
    const static long def = 6;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionVisibleTtl(const long &v) {
    instance()->handle->SetInt("OcclusionVisibleTtl",v);
    instance()->OcclusionVisibleTtl = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionVisibleTtl() {
    instance()->handle->RemoveInt("OcclusionVisibleTtl");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionBudget() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many occlusion tests one frame may issue. The GPU offers\n"
"256 for the whole process and the RenderDebug_Occlusion\n"
"measurement is the other claimant, so the default leaves that\n"
"measurement room to run alongside. Asking for more tests than\n"
"the budget allows is not an error: hidden nodes are offered\n"
"first, since a test is the only way one can come back, and the\n"
"rest are offered again next frame.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionBudget() {
    return instance()->OcclusionBudget;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionBudget() {
    const static long def = 128;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionBudget(const long &v) {
    instance()->handle->SetInt("OcclusionBudget",v);
    instance()->OcclusionBudget = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionBudget() {
    instance()->handle->RemoveInt("OcclusionBudget");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionMinSubtree() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Do not test an index node standing for fewer drawn instances\n"
"than this. A test is itself a draw, so testing a node that could\n"
"save one draw loses whether it answers hidden or visible.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionMinSubtree() {
    return instance()->OcclusionMinSubtree;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionMinSubtree() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionMinSubtree(const long &v) {
    instance()->handle->SetInt("OcclusionMinSubtree",v);
    instance()->OcclusionMinSubtree = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionMinSubtree() {
    instance()->handle->RemoveInt("OcclusionMinSubtree");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionMaxHidden() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many frames a hidden node may go without an answer before\n"
"it is drawn again. A hidden node is re-tested continuously and\n"
"the answer is its only way back, so if answers stop arriving --\n"
"no query handles left, a dropped batch -- this is what returns\n"
"the geometry instead of leaving it missing. Answers that keep\n"
"confirming the node is hidden keep it hidden indefinitely, so\n"
"this never flickers a node the tests are still reaching.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionMaxHidden() {
    return instance()->OcclusionMaxHidden;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionMaxHidden() {
    const static long def = 120;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionMaxHidden(const long &v) {
    instance()->handle->SetInt("OcclusionMaxHidden",v);
    instance()->OcclusionMaxHidden = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionMaxHidden() {
    instance()->handle->RemoveInt("OcclusionMaxHidden");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionDepthPad() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How far a test box is pushed towards the viewer before it is\n"
"tested, in steps of the 24-bit depth buffer. A test box has to\n"
"be a conservative bound, and at the last bit of the depth buffer\n"
"it is not: a small part lying flush on a large panel quantizes\n"
"to the same stored depth as the panel, LEQUAL loses the tie\n"
"whichever way the rasterizer rounds, and the node reports itself\n"
"hidden while in plain view. Measured that way, the components on\n"
"a board disappeared while the board stayed. Too large costs\n"
"frame time by testing visible what could have been skipped; too\n"
"small deletes geometry, so err high.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionDepthPad() {
    return instance()->OcclusionDepthPad;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionDepthPad() {
    const static long def = 16;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionDepthPad(const long &v) {
    instance()->handle->SetInt("OcclusionDepthPad",v);
    instance()->OcclusionDepthPad = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionDepthPad() {
    instance()->handle->RemoveInt("OcclusionDepthPad");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionConfirm() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many consecutive answers of 'no pixels' a node must give\n"
"before its geometry is actually skipped. 1 acts on every\n"
"answer, and is what an occlusion test naively does.\n"
"\n"
"This is the stability control. A test is issued against one\n"
"frame's depth and read against a later one -- it does not\n"
"block, because stalling for it would cost the frame time the\n"
"culling exists to save -- so while an answer is in flight, other\n"
"geometry is culled and the occluders move underneath it. Acted\n"
"on singly, a node tested while an occluder was still drawn gets\n"
"skipped after that occluder has gone; the hole it leaves tests\n"
"visible; it comes back; and it oscillates, which is a picture\n"
"that flickers rather than one that is merely wrong. Geometry\n"
"that really is hidden answers so every time and costs only the\n"
"extra confirmations.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionConfirm() {
    return instance()->OcclusionConfirm;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionConfirm() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionConfirm(const long &v) {
    instance()->handle->SetInt("OcclusionConfirm",v);
    instance()->OcclusionConfirm = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionConfirm() {
    instance()->handle->RemoveInt("OcclusionConfirm");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionSoftware() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Answer the occlusion question with a software depth buffer on\n"
"the CPU instead of hardware occlusion queries\n"
"(docs/FarFieldProxies.md #12.12).\n"
"\n"
"A hardware query cannot be asked at the moment its answer would\n"
"be right. It is issued against one frame's depth and read a\n"
"frame or two later, so a node is tested after the pass that drew\n"
"its own geometry and is asked to win a depth comparison against\n"
"itself -- measured as boxes returning no samples at all while\n"
"their contents were plainly on screen. The confirmations,\n"
"lifetimes and padding beside this setting all exist to contain\n"
"that, and none of them reach it.\n"
"\n"
"On the CPU, occluders are rasterized and nodes tested against\n"
"the same buffer in one pass, so a node is asked before its own\n"
"geometry joins the buffer and the answer arrives in the frame\n"
"that asked. There is no latency to age, no verdict to confirm\n"
"and no query pool to run out of. It costs CPU time in a frame\n"
"that is already CPU-bound, which is the trade to measure, and it\n"
"behaves identically in the browser, where hardware queries do\n"
"not.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getOcclusionSoftware() {
    return instance()->OcclusionSoftware;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultOcclusionSoftware() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionSoftware(const bool &v) {
    instance()->handle->SetBool("OcclusionSoftware",v);
    instance()->OcclusionSoftware = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionSoftware() {
    instance()->handle->RemoveBool("OcclusionSoftware");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionOccluderTris() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many triangles the CPU occlusion buffer may rasterize in one\n"
"frame. Only used when occlusion runs on the CPU.\n"
"\n"
"Occluders are spent largest-on-screen first, so what the budget\n"
"drops is what would have hidden least. Dropping them costs\n"
"culling and never pixels: an occluder that was not rasterized\n"
"simply hides nothing.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionOccluderTris() {
    return instance()->OcclusionOccluderTris;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionOccluderTris() {
    const static long def = 250000;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionOccluderTris(const long &v) {
    instance()->handle->SetInt("OcclusionOccluderTris",v);
    instance()->OcclusionOccluderTris = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionOccluderTris() {
    instance()->handle->RemoveInt("OcclusionOccluderTris");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionMinOccluder() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How large a draw must appear on screen, in pixels across its\n"
"bounding box diagonal, before it is worth rasterizing into the\n"
"CPU occlusion buffer. Smaller draws can hide almost nothing and\n"
"spend budget that a larger one could use.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionMinOccluder() {
    return instance()->OcclusionMinOccluder;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionMinOccluder() {
    const static long def = 24;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionMinOccluder(const long &v) {
    instance()->handle->SetInt("OcclusionMinOccluder",v);
    instance()->OcclusionMinOccluder = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionMinOccluder() {
    instance()->handle->RemoveInt("OcclusionMinOccluder");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionThreads() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many worker threads the CPU occlusion buffer may rasterize\n"
"its occluders on. 0 picks automatically, leaving the submitting\n"
"thread and one other alone -- this runs in the middle of a\n"
"frame, not on an idle machine.\n"
"\n"
"Each worker rasterizes its own slice of the occluder list into\n"
"its own buffer and the buffers are merged afterwards, so there\n"
"is no locking. The merge is slightly lossy -- two two-layer\n"
"blocks cannot combine into one without loss -- so a higher\n"
"worker count can hide marginally less. Never more.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionThreads() {
    return instance()->OcclusionThreads;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionThreads() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionThreads(const long &v) {
    instance()->handle->SetInt("OcclusionThreads",v);
    instance()->OcclusionThreads = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionThreads() {
    instance()->handle->RemoveInt("OcclusionThreads");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionSimd() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Let the CPU occlusion buffer discard triangles four at a time\n"
"with SIMD before its exact rasterizer looks at them\n"
"(docs/FarFieldProxies.md #12.14).\n"
"\n"
"Two thirds of the triangles offered to the buffer cover no pixel\n"
"at all -- a full-detail CAD tessellation is mostly triangles\n"
"smaller than the pixel grid -- and every one of them is paid for\n"
"in full before being thrown away. The pre-pass transforms and\n"
"projects four at once in single precision and drops the ones that\n"
"land on no pixel centre.\n"
"\n"
"It cannot make the buffer claim a surface that is not there:\n"
"everything it does not discard is handed to the same exact path\n"
"as before, recomputed from the original vertices, and a triangle\n"
"it drops in error is occlusion lost rather than geometry deleted.\n"
"Turn it off to measure what it saves, not to work around a\n"
"suspected fault.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getOcclusionSimd() {
    return instance()->OcclusionSimd;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultOcclusionSimd() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionSimd(const bool &v) {
    instance()->handle->SetBool("OcclusionSimd",v);
    instance()->OcclusionSimd = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionSimd() {
    instance()->handle->RemoveBool("OcclusionSimd");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionResolution() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Resolution of the CPU occlusion buffer, as a divisor of the\n"
"viewport. 1 matches the viewport.\n"
"\n"
"Above 1 this can remove geometry that was visible, which is the\n"
"one failure this mechanism exists to avoid: a coarse pixel is\n"
"marked covered when an occluder reaches its centre, but it\n"
"stands for several real pixels, and the ones the occluder missed\n"
"are claimed with it. Reduce it only to measure what it costs, not\n"
"as a setting.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionResolution() {
    return instance()->OcclusionResolution;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionResolution() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionResolution(const long &v) {
    instance()->handle->SetInt("OcclusionResolution",v);
    instance()->OcclusionResolution = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionResolution() {
    instance()->handle->RemoveInt("OcclusionResolution");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionPerInstance() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Test each object against the CPU occlusion buffer, not just the\n"
"group it was partitioned into\n"
"(docs/FarFieldProxies.md #12.17). Only used when occlusion runs\n"
"on the CPU.\n"
"\n"
"The cull walk tests boxes of groups, and a group is skipped only\n"
"when all of it is hidden -- so one visible object keeps its\n"
"hidden neighbours on screen. Measured, that is what limits the\n"
"culling rather than the quality of the depth buffer: after a\n"
"cull, 91% of what is still drawn reaches no pixel, and making\n"
"the occluders ten times better barely moved it.\n"
"\n"
"The extra tests are read-only against a buffer that is already\n"
"finished, so they run on the same worker threads the occluders\n"
"used and add no state, no latency and nothing the backend has to\n"
"support.\n"
"\n"
"On by default: measured on the benchmark it hides 17% more for\n"
"0.4ms, against 3% for 3.4ms from making the occluders ten times\n"
"better, and it over-culls nothing. It can only ever be more\n"
"correct than testing the group -- a draw is skipped when its own\n"
"box is covered rather than when its neighbours' collectively\n"
"are.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getOcclusionPerInstance() {
    return instance()->OcclusionPerInstance;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultOcclusionPerInstance() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionPerInstance(const bool &v) {
    instance()->handle->SetBool("OcclusionPerInstance",v);
    instance()->OcclusionPerInstance = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionPerInstance() {
    instance()->handle->RemoveBool("OcclusionPerInstance");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionCoarse() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Rasterize the CPU occlusion buffer's occluders from coarse\n"
"hulls instead of from their meshes\n"
"(docs/FarFieldProxies.md #12.16). Only used when occlusion runs\n"
"on the CPU.\n"
"\n"
"An occluder does not need the mesh, it needs the surface, and a\n"
"hull carries that at a fraction of the triangles. What the\n"
"triangle budget above buys is what this changes: measured, 1285\n"
"of 1322 candidate occluders never entered the buffer because 37\n"
"full-detail draws spent the whole allowance, and the buffer then\n"
"hid 45% of what was there to hide.\n"
"\n"
"The hulls are built by vertex clustering from the meshes the\n"
"renderer already holds -- no shape, no tessellator -- a few per\n"
"frame, and cached. A hull recedes by its own measured error\n"
"before it is rasterized, so it cannot claim to be nearer than\n"
"the surface it stands for.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getOcclusionCoarse() {
    return instance()->OcclusionCoarse;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultOcclusionCoarse() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionCoarse(const bool &v) {
    instance()->handle->SetBool("OcclusionCoarse",v);
    instance()->OcclusionCoarse = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionCoarse() {
    instance()->handle->RemoveBool("OcclusionCoarse");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionCoarseLevel() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Which rung of the decimation ladder an occluder hull is built\n"
"at, coarsest first: the clustering grid is an eighth of the\n"
"mesh's diagonal at 0 and halves per level, so 2 is a\n"
"thirty-second of it. Lower is cheaper to rasterize and further\n"
"from the surface; higher approaches the mesh itself.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionCoarseLevel() {
    return instance()->OcclusionCoarseLevel;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionCoarseLevel() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionCoarseLevel(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseLevel",v);
    instance()->OcclusionCoarseLevel = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionCoarseLevel() {
    instance()->handle->RemoveInt("OcclusionCoarseLevel");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionCoarseMinTris() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many triangles a draw must carry before it is worth a\n"
"hull. Below this it is rasterized from its mesh: a hull of a\n"
"small mesh saves triangles that were never what spent the\n"
"budget.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionCoarseMinTris() {
    return instance()->OcclusionCoarseMinTris;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionCoarseMinTris() {
    const static long def = 512;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionCoarseMinTris(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseMinTris",v);
    instance()->OcclusionCoarseMinTris = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionCoarseMinTris() {
    instance()->handle->RemoveInt("OcclusionCoarseMinTris");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionCoarseBuilds() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many occluder hulls may be built in one frame. Building is\n"
"parallel but not free, so a scene that has just come into view\n"
"acquires its hulls over several frames rather than stalling one.\n"
"0 freezes the cache at what it already holds.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionCoarseBuilds() {
    return instance()->OcclusionCoarseBuilds;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionCoarseBuilds() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionCoarseBuilds(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseBuilds",v);
    instance()->OcclusionCoarseBuilds = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionCoarseBuilds() {
    instance()->handle->RemoveInt("OcclusionCoarseBuilds");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionCoarseBias() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How far an occluder hull recedes from the camera before it is\n"
"rasterized, as a percentage of its own measured displacement.\n"
"\n"
"Every point of a hull lies within that displacement of a point of\n"
"the mesh it was built from, so at 100 the hull cannot be nearer\n"
"than the surface it stands for -- which is what makes an\n"
"approximate occluder admissible at all. Below 100 it hides more\n"
"and may hide geometry that was visible; above 100 it hides\n"
"progressively less for nothing. 0 rasterizes the hull where it\n"
"sits, which is the measurement that says whether the bias is\n"
"needed.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionCoarseBias() {
    return instance()->OcclusionCoarseBias;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionCoarseBias() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionCoarseBias(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseBias",v);
    instance()->OcclusionCoarseBias = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionCoarseBias() {
    instance()->handle->RemoveInt("OcclusionCoarseBias");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusionCoarseMemory() {
    return QT_TRANSLATE_NOOP("RenderParams",
"What the occluder hull cache may hold, in megabytes, before\n"
"the least recently used hulls are dropped. A dropped hull costs a\n"
"rebuild when its occluder comes back into view, never\n"
"correctness.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionCoarseMemory() {
    return instance()->OcclusionCoarseMemory;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionCoarseMemory() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionCoarseMemory(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseMemory",v);
    instance()->OcclusionCoarseMemory = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionCoarseMemory() {
    instance()->handle->RemoveInt("OcclusionCoarseMemory");
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
const char *RenderParams::docPBREnvImage() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Image file used as the image based lighting environment,\n"
"replacing the built-in procedural studio environment. A 2:1\n"
"image is read as equirectangular (lat-long), anything squarer\n"
"as a sphere map — the same convention as the Texture mapping\n"
"dialog's Environment mode, so the same file works in both.\n"
"Empty falls back to that dialog's current image, then to the\n"
"procedural environment.");
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & RenderParams::getPBREnvImage() {
    return instance()->PBREnvImage;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & RenderParams::defaultPBREnvImage() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBREnvImage(const std::string &v) {
    instance()->handle->SetASCII("PBREnvImage",v);
    instance()->PBREnvImage = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBREnvImage() {
    instance()->handle->RemoveASCII("PBREnvImage");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docPBREnvEmbed() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Store a copy of the environment image inside the document,\n"
"so it travels with the file instead of depending on the\n"
"original path. The copy lives in the view's\n"
"Render_PBREnvImageData property and takes precedence over the\n"
"image path while set.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getPBREnvEmbed() {
    return instance()->PBREnvEmbed;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultPBREnvEmbed() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBREnvEmbed(const bool &v) {
    instance()->handle->SetBool("PBREnvEmbed",v);
    instance()->PBREnvEmbed = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBREnvEmbed() {
    instance()->handle->RemoveBool("PBREnvEmbed");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docPBREnvBackground() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Show the image based lighting environment itself as the view\n"
"background while PBR shading is active, so reflective surfaces\n"
"visibly mirror their surroundings.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getPBREnvBackground() {
    return instance()->PBREnvBackground;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultPBREnvBackground() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBREnvBackground(const bool &v) {
    instance()->handle->SetBool("PBREnvBackground",v);
    instance()->PBREnvBackground = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBREnvBackground() {
    instance()->handle->RemoveBool("PBREnvBackground");
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
"The ambient ripple pattern on the water surface - the motion\n"
"the surface has of its own accord. 0 = waves: the default sum\n"
"of directional wind waves. 1 = rain: circular rings expanding\n"
"from randomly placed, randomly timed drop impacts, as on a pond\n"
"in rainfall. 2 = none: a still surface, which leaves only what\n"
"the scene disturbs - fountain splash rings and the impact rings\n"
"of particles striking the water still show.");
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
const char *RenderParams::docWaterImpactStrength() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Height of the rings raised where particles actually strike\n"
"the water - a fountain's droplets landing in its own basin.\n"
"Unlike the rain ripple type these are not a pattern: nothing\n"
"appears unless something hits the surface, and it appears\n"
"where it hit. Zero turns them off. Needs a stateful emitter\n"
"whose step program reports its impacts.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getWaterImpactStrength() {
    return instance()->WaterImpactStrength;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultWaterImpactStrength() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterImpactStrength(const double &v) {
    instance()->handle->SetFloat("WaterImpactStrength",v);
    instance()->WaterImpactStrength = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterImpactStrength() {
    instance()->handle->RemoveFloat("WaterImpactStrength");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWaterImpactLife() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How long an impact ring lives, in seconds - which is also\n"
"how far it travels, since a ring is sized to have crossed two\n"
"cells of the impact map when it dies. Longer makes slower,\n"
"wider-travelling rings out of the same hits.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getWaterImpactLife() {
    return instance()->WaterImpactLife;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultWaterImpactLife() {
    const static double def = 1.1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWaterImpactLife(const double &v) {
    instance()->handle->SetFloat("WaterImpactLife",v);
    instance()->WaterImpactLife = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWaterImpactLife() {
    instance()->handle->RemoveFloat("WaterImpactLife");
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
const char *RenderParams::docSunDisc() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Draw a visible sun -- a bright disc with a limb glow -- in\n"
"the sky along the Shadow draw style's directional scene light,\n"
"occluded by geometry and feeding the bloom glow. Perspective\n"
"cameras only; spot lights have no sky direction.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getSunDisc() {
    return instance()->SunDisc;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultSunDisc() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setSunDisc(const bool &v) {
    instance()->handle->SetBool("SunDisc",v);
    instance()->SunDisc = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeSunDisc() {
    instance()->handle->RemoveBool("SunDisc");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docSunDiscSize() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Angular radius of the sun disc in degrees (the real sun is\n"
"about 0.27; larger reads better in a CAD scene).");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getSunDiscSize() {
    return instance()->SunDiscSize;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultSunDiscSize() {
    const static double def = 1.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setSunDiscSize(const double &v) {
    instance()->handle->SetFloat("SunDiscSize",v);
    instance()->SunDiscSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeSunDiscSize() {
    instance()->handle->RemoveFloat("SunDiscSize");
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

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugViewMode() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Render debugging buffer visualization (docs/RenderDebug.md).\n"
"Routes an intermediate render target to the screen instead of the\n"
"shaded scene: 1 = linearized scene depth, 2 = view-space normals,\n"
"3 = ambient occlusion term only, 4 = shadow term only, 5 = shadow\n"
"map / bulb-tile coverage as color, 6 = overdraw heatmap, 7 =\n"
"shadow-moment filtering-precision probe, 8 = UV / texcoord,\n"
"9 = the planar reflection target, 10 = the particle impact map\n"
"(green where a hit is recorded, brightness its age, red where\n"
"nothing has ever struck).\n"
"0 renders normally. The on-top, highlight and overlay passes\n"
"still draw on top so the view stays navigable.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getDebugViewMode() {
    return instance()->DebugViewMode;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultDebugViewMode() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugViewMode(const long &v) {
    instance()->handle->SetInt("DebugViewMode",v);
    instance()->DebugViewMode = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugViewMode() {
    instance()->handle->RemoveInt("DebugViewMode");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugFreezeFrame() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Freeze every intentionally time- or history-dependent render\n"
"input: temporal accumulation and per-frame sampling jitter, and\n"
"time-driven animation (water waves, fire). Two frames of the same\n"
"scene, camera and parameters then render identically -- the\n"
"determinism switch for golden-image comparison\n"
"(docs/RenderDebug.md).");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugFreezeFrame() {
    return instance()->DebugFreezeFrame;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugFreezeFrame() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugFreezeFrame(const bool &v) {
    instance()->handle->SetBool("DebugFreezeFrame",v);
    instance()->DebugFreezeFrame = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugFreezeFrame() {
    instance()->handle->RemoveBool("DebugFreezeFrame");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugLabel() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Burn a self-describing label into a corner of the rendered\n"
"frame while render debugging: the active debug view mode, the\n"
"freeze-frame state and any custom RenderDebug_* parameter values.\n"
"A captured PNG then documents its own settings without its\n"
"sidecar (docs/RenderDebug.md).");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugLabel() {
    return instance()->DebugLabel;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugLabel() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugLabel(const bool &v) {
    instance()->handle->SetBool("DebugLabel",v);
    instance()->DebugLabel = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugLabel() {
    instance()->handle->RemoveBool("DebugLabel");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugTiming() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Log where the time of a rendered frame goes, by pipeline\n"
"stage: the Coin traversal, the flattening of the vertex caches,\n"
"the draw-entry build, the translation to the backend, the\n"
"backend's own bookkeeping and the draw itself. One summary line\n"
"per second, so a long operation shows how each stage grows with\n"
"the scene rather than one average (docs/IncrementalPublish.md).\n"
"Those stages end at submission, so a second line reports what\n"
"happens after it: the frame's cost on the CPU issuing draw\n"
"commands against its cost on the GPU drawing them, and the same\n"
"pair per draw call (docs/FarFieldProxies.md §10.1). Which of the\n"
"two a scene is bound by is what decides whether a culling scheme\n"
"has to remove the draw or may leave it to the GPU to reject.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugTiming() {
    return instance()->DebugTiming;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugTiming() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugTiming(const bool &v) {
    instance()->handle->SetBool("DebugTiming",v);
    instance()->DebugTiming = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugTiming() {
    instance()->handle->RemoveBool("DebugTiming");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugDelta() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Log what each published frame actually changed: how many of\n"
"the scene cache's children the publish reused, how many it added\n"
"or dropped, and how many separators the traversal below it reused\n"
"against how many it rebuilt. One summary line per second. A\n"
"publish rebuilds the whole scene however little moved, and these\n"
"counts are how much of that rebuild was avoidable\n"
"(docs/IncrementalPublish.md §5).");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugDelta() {
    return instance()->DebugDelta;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugDelta() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugDelta(const bool &v) {
    instance()->handle->SetBool("DebugDelta",v);
    instance()->DebugDelta = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugDelta() {
    instance()->handle->RemoveBool("DebugDelta");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugCoverage() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Log how much of the screen each drawn object actually covers,\n"
"as a histogram over its projected size in pixels. A camera that\n"
"sees a whole assembly draws most of it at a few pixels, and every\n"
"one of those parts still costs a full object; the histogram says\n"
"how much of the model is in that state, which is what decides\n"
"whether aggregating distant parts is worth building\n"
"(docs/FarFieldProxies.md §9).");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugCoverage() {
    return instance()->DebugCoverage;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugCoverage() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugCoverage(const bool &v) {
    instance()->handle->SetBool("DebugCoverage",v);
    instance()->DebugCoverage = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugCoverage() {
    instance()->handle->RemoveBool("DebugCoverage");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugProxyCut() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Log what a far-field cut would cost this camera, without\n"
"generating anything: the drawn instances are partitioned into the\n"
"spatial index of docs/FarFieldProxies.md §3, a frontier is chosen\n"
"by projected error at several tolerances, and the draws that cut\n"
"would issue -- one per (cell, material) proxy plus whatever stays\n"
"exact -- are reported against the draws issued today. This is the\n"
"number that says whether generating proxies is worth building\n"
"(§11.1). Also reports the distributions that size the partition:\n"
"instances and material buckets per cell, per level.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugProxyCut() {
    return instance()->DebugProxyCut;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugProxyCut() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugProxyCut(const bool &v) {
    instance()->handle->SetBool("DebugProxyCut",v);
    instance()->DebugProxyCut = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugProxyCut() {
    instance()->handle->RemoveBool("DebugProxyCut");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugOcclusion() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Measure how much of what the frame draws could not have\n"
"reached the screen (docs/FarFieldProxies.md §10.1). Bounding\n"
"boxes of the spatial index's nodes are re-rasterized against the\n"
"finished depth buffer under hardware occlusion queries, writing\n"
"neither colour nor depth, and every instance is attributed to the\n"
"highest node that rejects it -- so a hidden subtree is counted\n"
"once, not at every level it is hidden at. Boxes bound their\n"
"contents loosely and the frustum's own rejections are reported\n"
"separately, so the hidden share it prints is a floor rather than\n"
"an estimate. A GPU offers 256 queries at a time, so a large model\n"
"takes several frames to walk and a line is printed per completed\n"
"walk, never for a partial one.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugOcclusion() {
    return instance()->DebugOcclusion;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugOcclusion() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugOcclusion(const bool &v) {
    instance()->handle->SetBool("DebugOcclusion",v);
    instance()->DebugOcclusion = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugOcclusion() {
    instance()->handle->RemoveBool("DebugOcclusion");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugProxyGen() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Generate real proxies for a sample of the nodes a far-field\n"
"cut stops on, and report what they cost and what they commit\n"
"(docs/FarFieldProxies.md §11.1c). The cut estimate above selects\n"
"by a node's projected *extent* because no proxy exists yet to\n"
"have an error; this one merges each (cell, material) group and\n"
"decimates it, so the error it commits can be measured as a\n"
"fraction of that extent -- which is the ratio that says whether\n"
"the estimate reads as its 16px row or its 64px row. Reports\n"
"alongside it the triangle cost against what instancing already\n"
"achieves (§7.1) and how much surface area survives, since\n"
"clustering deletes geometry smaller than a cell rather than\n"
"shrinking it. Expensive: it builds meshes. Samples a bounded\n"
"number of nodes and reports how many it skipped.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugProxyGen() {
    return instance()->DebugProxyGen;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugProxyGen() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugProxyGen(const bool &v) {
    instance()->handle->SetBool("DebugProxyGen",v);
    instance()->DebugProxyGen = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugProxyGen() {
    instance()->handle->RemoveBool("DebugProxyGen");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugCullAudit() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Check what the occlusion culling skipped against what the\n"
"geometry actually put on screen (docs/FarFieldProxies.md §12.9).\n"
"Every other measurement of the culling compares two pictures and\n"
"reports how many pixels differ, which says that something is\n"
"wrong without saying what: this re-rasterizes the scene with the\n"
"cull mask ignored and each draw writing its own identity instead\n"
"of a colour, so the ids that own a pixel are an exact answer to\n"
"which draws reach the screen. Their intersection with the mask is\n"
"a list of proven over-culls -- each one a named draw with a pixel\n"
"count -- and the ids that own nothing while being drawn are the\n"
"converse: the headroom the culling has not taken. Reads the image\n"
"back to the CPU once a second, so it costs a full-resolution\n"
"transfer on the frames it reports and nothing while off. Needs a\n"
"backend with texture readback, which WebGL2 is not.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugCullAudit() {
    return instance()->DebugCullAudit;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugCullAudit() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugCullAudit(const bool &v) {
    instance()->handle->SetBool("DebugCullAudit",v);
    instance()->DebugCullAudit = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugCullAudit() {
    instance()->handle->RemoveBool("DebugCullAudit");
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
    if (boost::equals(sReason, "LevelThreads")) {
        // The renderer layer cannot read Gui parameters — push the cap
        // down (applies to level workers not yet spawned).
        Render::SceneStreamServer::setLevelThreadCap(int(getLevelThreads()));
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
