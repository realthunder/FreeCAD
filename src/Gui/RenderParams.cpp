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
#include "Renderer/Renderer.h"
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
    long OutputTransform;
    double Exposure;
    long MaxViewIds;
    long BackgroundReleaseDelay;
    long CoarseTessellation;
    long CoarseDeferFaces;
    bool MeshSkipRedundant;
    bool MeshSkipFinerResident;
    bool MeshSkipInvariant;
    bool ProgressiveLoad;
    long ProgressiveLoadBudgetMS;
    long LevelThreads;
    long LevelMemoryFloorMB;
    bool LevelDebug;
    long LevelCeilingSimulateMB;
    long GpuMemoryBudgetMB;
    double LevelTolerance;
    double LevelPressureRelease;
    bool ClimbHardLimit;
    long ClimbAdmitBatch;
    long LevelLandBudgetMS;
    bool MeshSkipLanded;
    bool VisualFillOnPool;
    long VisualFillMinFaces;
    long WorkerVertexCache;
    long CaptureBudgetMS;
    long LevelSlowBuildMS;
    long DescentOrderBatch;
    bool DowngradeLedger;
    long LevelCount;
    double LevelScale;
    double LevelBudgetDeadband;
    double LevelScaleBoxError;
    bool SimplifyExhausted;
    bool SimplifyMergeParts;
    double SimplifyMinReduction;
    bool ShapeVertices;
    bool PressureDropEdges;
    long ElementGateStagger;
    long TinyElementCutoff;
    bool LoadDropElements;
    double EffectResolution;
    bool TemporalAccum;
    long TemporalAccumSamples;
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
    long OcclusionDemoteStreak;
    bool OcclusionCoarse;
    long OcclusionCoarseLevel;
    long OcclusionCoarseMinTris;
    long OcclusionCoarseBuilds;
    long OcclusionCoarseBias;
    long OcclusionCoarseMemory;
    bool OcclusionBenefitProbe;
    bool AO;
    bool Shadow;
    long AOMethod;
    long AOSlices;
    long AOSteps;
    double AORadius;
    double AOIntensity;
    double AOResolution;
    bool Cavity;
    double CavityRadius;
    double CavityValley;
    double CavityRidge;
    bool Matcap;
    long MatcapPreset;
    double MatcapTint;
    bool PBR;
    double PBRMetallic;
    double PBRRoughness;
    bool PBRFromSpecular;
    long ShininessMapping;
    long PBREnvPreset;
    double PBREnvIntensity;
    std::string PBREnvImage;
    bool PBREnvEmbed;
    bool PBREnvBackground;
    double PBREnvBlur;
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
    bool Light;
    double LightIntensity;
    double LightDirectionX;
    double LightDirectionY;
    double LightDirectionZ;
    unsigned long LightColor;
    bool LightSpot;
    double LightPositionX;
    double LightPositionY;
    double LightPositionZ;
    double LightCutOffAngle;
    double LightDropOffRate;
    bool SunDisc;
    double SunDiscSize;
    bool GroundReflection;
    double GroundReflectionIntensity;
    std::string CyclesDevice;
    long CyclesSamples;
    double CyclesTimeLimit;
    bool CyclesDenoise;
    long CyclesPixelSize;
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
    bool DebugCullBounds;

    // Auto generated code (Tools/params_utils.py:253)
    RenderParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View/Render");
        handle->Attach(this);

        Type = this->handle->GetASCII("Type", "Default");
        funcs["Type"] = &RenderParamsP::updateType;
        OutputTransform = this->handle->GetInt("OutputTransform", 1);
        funcs["OutputTransform"] = &RenderParamsP::updateOutputTransform;
        Exposure = this->handle->GetFloat("Exposure", 1.0);
        funcs["Exposure"] = &RenderParamsP::updateExposure;
        MaxViewIds = this->handle->GetInt("MaxViewIds", 1024);
        funcs["MaxViewIds"] = &RenderParamsP::updateMaxViewIds;
        BackgroundReleaseDelay = this->handle->GetInt("BackgroundReleaseDelay", 1000);
        funcs["BackgroundReleaseDelay"] = &RenderParamsP::updateBackgroundReleaseDelay;
        CoarseTessellation = this->handle->GetInt("CoarseTessellation", 2);
        funcs["CoarseTessellation"] = &RenderParamsP::updateCoarseTessellation;
        CoarseDeferFaces = this->handle->GetInt("CoarseDeferFaces", 1000);
        funcs["CoarseDeferFaces"] = &RenderParamsP::updateCoarseDeferFaces;
        MeshSkipRedundant = this->handle->GetBool("MeshSkipRedundant", true);
        funcs["MeshSkipRedundant"] = &RenderParamsP::updateMeshSkipRedundant;
        MeshSkipFinerResident = this->handle->GetBool("MeshSkipFinerResident", false);
        funcs["MeshSkipFinerResident"] = &RenderParamsP::updateMeshSkipFinerResident;
        MeshSkipInvariant = this->handle->GetBool("MeshSkipInvariant", true);
        funcs["MeshSkipInvariant"] = &RenderParamsP::updateMeshSkipInvariant;
        ProgressiveLoad = this->handle->GetBool("ProgressiveLoad", true);
        funcs["ProgressiveLoad"] = &RenderParamsP::updateProgressiveLoad;
        ProgressiveLoadBudgetMS = this->handle->GetInt("ProgressiveLoadBudgetMS", 100);
        funcs["ProgressiveLoadBudgetMS"] = &RenderParamsP::updateProgressiveLoadBudgetMS;
        LevelThreads = this->handle->GetInt("LevelThreads", 0);
        funcs["LevelThreads"] = &RenderParamsP::updateLevelThreads;
        LevelMemoryFloorMB = this->handle->GetInt("LevelMemoryFloorMB", 0);
        funcs["LevelMemoryFloorMB"] = &RenderParamsP::updateLevelMemoryFloorMB;
        LevelDebug = this->handle->GetBool("LevelDebug", false);
        funcs["LevelDebug"] = &RenderParamsP::updateLevelDebug;
        LevelCeilingSimulateMB = this->handle->GetInt("LevelCeilingSimulateMB", 0);
        funcs["LevelCeilingSimulateMB"] = &RenderParamsP::updateLevelCeilingSimulateMB;
        GpuMemoryBudgetMB = this->handle->GetInt("GpuMemoryBudgetMB", 0);
        funcs["GpuMemoryBudgetMB"] = &RenderParamsP::updateGpuMemoryBudgetMB;
        LevelTolerance = this->handle->GetFloat("LevelTolerance", 2.0);
        funcs["LevelTolerance"] = &RenderParamsP::updateLevelTolerance;
        LevelPressureRelease = this->handle->GetFloat("LevelPressureRelease", 0.5);
        funcs["LevelPressureRelease"] = &RenderParamsP::updateLevelPressureRelease;
        ClimbHardLimit = this->handle->GetBool("ClimbHardLimit", true);
        funcs["ClimbHardLimit"] = &RenderParamsP::updateClimbHardLimit;
        ClimbAdmitBatch = this->handle->GetInt("ClimbAdmitBatch", 64);
        funcs["ClimbAdmitBatch"] = &RenderParamsP::updateClimbAdmitBatch;
        LevelLandBudgetMS = this->handle->GetInt("LevelLandBudgetMS", 50);
        funcs["LevelLandBudgetMS"] = &RenderParamsP::updateLevelLandBudgetMS;
        MeshSkipLanded = this->handle->GetBool("MeshSkipLanded", true);
        funcs["MeshSkipLanded"] = &RenderParamsP::updateMeshSkipLanded;
        VisualFillOnPool = this->handle->GetBool("VisualFillOnPool", true);
        funcs["VisualFillOnPool"] = &RenderParamsP::updateVisualFillOnPool;
        VisualFillMinFaces = this->handle->GetInt("VisualFillMinFaces", 2000);
        funcs["VisualFillMinFaces"] = &RenderParamsP::updateVisualFillMinFaces;
        WorkerVertexCache = this->handle->GetInt("WorkerVertexCache", 1);
        funcs["WorkerVertexCache"] = &RenderParamsP::updateWorkerVertexCache;
        CaptureBudgetMS = this->handle->GetInt("CaptureBudgetMS", 50);
        funcs["CaptureBudgetMS"] = &RenderParamsP::updateCaptureBudgetMS;
        LevelSlowBuildMS = this->handle->GetInt("LevelSlowBuildMS", 200);
        funcs["LevelSlowBuildMS"] = &RenderParamsP::updateLevelSlowBuildMS;
        DescentOrderBatch = this->handle->GetInt("DescentOrderBatch", 64);
        funcs["DescentOrderBatch"] = &RenderParamsP::updateDescentOrderBatch;
        DowngradeLedger = this->handle->GetBool("DowngradeLedger", true);
        funcs["DowngradeLedger"] = &RenderParamsP::updateDowngradeLedger;
        LevelCount = this->handle->GetInt("LevelCount", 8);
        funcs["LevelCount"] = &RenderParamsP::updateLevelCount;
        LevelScale = this->handle->GetFloat("LevelScale", 2.0);
        funcs["LevelScale"] = &RenderParamsP::updateLevelScale;
        LevelBudgetDeadband = this->handle->GetFloat("LevelBudgetDeadband", 0.03);
        funcs["LevelBudgetDeadband"] = &RenderParamsP::updateLevelBudgetDeadband;
        LevelScaleBoxError = this->handle->GetFloat("LevelScaleBoxError", 0.25);
        funcs["LevelScaleBoxError"] = &RenderParamsP::updateLevelScaleBoxError;
        SimplifyExhausted = this->handle->GetBool("SimplifyExhausted", true);
        funcs["SimplifyExhausted"] = &RenderParamsP::updateSimplifyExhausted;
        SimplifyMergeParts = this->handle->GetBool("SimplifyMergeParts", false);
        funcs["SimplifyMergeParts"] = &RenderParamsP::updateSimplifyMergeParts;
        SimplifyMinReduction = this->handle->GetFloat("SimplifyMinReduction", 20.0);
        funcs["SimplifyMinReduction"] = &RenderParamsP::updateSimplifyMinReduction;
        ShapeVertices = this->handle->GetBool("ShapeVertices", true);
        funcs["ShapeVertices"] = &RenderParamsP::updateShapeVertices;
        PressureDropEdges = this->handle->GetBool("PressureDropEdges", true);
        funcs["PressureDropEdges"] = &RenderParamsP::updatePressureDropEdges;
        ElementGateStagger = this->handle->GetInt("ElementGateStagger", 15);
        funcs["ElementGateStagger"] = &RenderParamsP::updateElementGateStagger;
        TinyElementCutoff = this->handle->GetInt("TinyElementCutoff", 0);
        funcs["TinyElementCutoff"] = &RenderParamsP::updateTinyElementCutoff;
        LoadDropElements = this->handle->GetBool("LoadDropElements", true);
        funcs["LoadDropElements"] = &RenderParamsP::updateLoadDropElements;
        EffectResolution = this->handle->GetFloat("EffectResolution", 1.0);
        funcs["EffectResolution"] = &RenderParamsP::updateEffectResolution;
        TemporalAccum = this->handle->GetBool("TemporalAccum", false);
        funcs["TemporalAccum"] = &RenderParamsP::updateTemporalAccum;
        TemporalAccumSamples = this->handle->GetInt("TemporalAccumSamples", 32);
        funcs["TemporalAccumSamples"] = &RenderParamsP::updateTemporalAccumSamples;
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
        OcclusionSoftware = this->handle->GetBool("OcclusionSoftware", true);
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
        OcclusionDemoteStreak = this->handle->GetInt("OcclusionDemoteStreak", 8);
        funcs["OcclusionDemoteStreak"] = &RenderParamsP::updateOcclusionDemoteStreak;
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
        OcclusionBenefitProbe = this->handle->GetBool("OcclusionBenefitProbe", false);
        funcs["OcclusionBenefitProbe"] = &RenderParamsP::updateOcclusionBenefitProbe;
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
        Cavity = this->handle->GetBool("Cavity", true);
        funcs["Cavity"] = &RenderParamsP::updateCavity;
        CavityRadius = this->handle->GetFloat("CavityRadius", 1.0);
        funcs["CavityRadius"] = &RenderParamsP::updateCavityRadius;
        CavityValley = this->handle->GetFloat("CavityValley", 1.0);
        funcs["CavityValley"] = &RenderParamsP::updateCavityValley;
        CavityRidge = this->handle->GetFloat("CavityRidge", 0.5);
        funcs["CavityRidge"] = &RenderParamsP::updateCavityRidge;
        Matcap = this->handle->GetBool("Matcap", false);
        funcs["Matcap"] = &RenderParamsP::updateMatcap;
        MatcapPreset = this->handle->GetInt("MatcapPreset", 0);
        funcs["MatcapPreset"] = &RenderParamsP::updateMatcapPreset;
        MatcapTint = this->handle->GetFloat("MatcapTint", 1.0);
        funcs["MatcapTint"] = &RenderParamsP::updateMatcapTint;
        PBR = this->handle->GetBool("PBR", false);
        funcs["PBR"] = &RenderParamsP::updatePBR;
        PBRMetallic = this->handle->GetFloat("PBRMetallic", 0.0);
        funcs["PBRMetallic"] = &RenderParamsP::updatePBRMetallic;
        PBRRoughness = this->handle->GetFloat("PBRRoughness", 0.0);
        funcs["PBRRoughness"] = &RenderParamsP::updatePBRRoughness;
        PBRFromSpecular = this->handle->GetBool("PBRFromSpecular", true);
        funcs["PBRFromSpecular"] = &RenderParamsP::updatePBRFromSpecular;
        ShininessMapping = this->handle->GetInt("ShininessMapping", 1);
        funcs["ShininessMapping"] = &RenderParamsP::updateShininessMapping;
        PBREnvPreset = this->handle->GetInt("PBREnvPreset", 1);
        funcs["PBREnvPreset"] = &RenderParamsP::updatePBREnvPreset;
        PBREnvIntensity = this->handle->GetFloat("PBREnvIntensity", 1.0);
        funcs["PBREnvIntensity"] = &RenderParamsP::updatePBREnvIntensity;
        PBREnvImage = this->handle->GetASCII("PBREnvImage", "");
        funcs["PBREnvImage"] = &RenderParamsP::updatePBREnvImage;
        PBREnvEmbed = this->handle->GetBool("PBREnvEmbed", true);
        funcs["PBREnvEmbed"] = &RenderParamsP::updatePBREnvEmbed;
        PBREnvBackground = this->handle->GetBool("PBREnvBackground", true);
        funcs["PBREnvBackground"] = &RenderParamsP::updatePBREnvBackground;
        PBREnvBlur = this->handle->GetFloat("PBREnvBlur", 0.25);
        funcs["PBREnvBlur"] = &RenderParamsP::updatePBREnvBlur;
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
        Light = this->handle->GetBool("Light", false);
        funcs["Light"] = &RenderParamsP::updateLight;
        LightIntensity = this->handle->GetFloat("LightIntensity", 0.8);
        funcs["LightIntensity"] = &RenderParamsP::updateLightIntensity;
        LightDirectionX = this->handle->GetFloat("LightDirectionX", -1.0);
        funcs["LightDirectionX"] = &RenderParamsP::updateLightDirectionX;
        LightDirectionY = this->handle->GetFloat("LightDirectionY", -1.0);
        funcs["LightDirectionY"] = &RenderParamsP::updateLightDirectionY;
        LightDirectionZ = this->handle->GetFloat("LightDirectionZ", -1.0);
        funcs["LightDirectionZ"] = &RenderParamsP::updateLightDirectionZ;
        LightColor = this->handle->GetUnsigned("LightColor", 0xF0FDFFFF);
        funcs["LightColor"] = &RenderParamsP::updateLightColor;
        LightSpot = this->handle->GetBool("LightSpot", false);
        funcs["LightSpot"] = &RenderParamsP::updateLightSpot;
        LightPositionX = this->handle->GetFloat("LightPositionX", 0.0);
        funcs["LightPositionX"] = &RenderParamsP::updateLightPositionX;
        LightPositionY = this->handle->GetFloat("LightPositionY", 0.0);
        funcs["LightPositionY"] = &RenderParamsP::updateLightPositionY;
        LightPositionZ = this->handle->GetFloat("LightPositionZ", 0.0);
        funcs["LightPositionZ"] = &RenderParamsP::updateLightPositionZ;
        LightCutOffAngle = this->handle->GetFloat("LightCutOffAngle", 45.0);
        funcs["LightCutOffAngle"] = &RenderParamsP::updateLightCutOffAngle;
        LightDropOffRate = this->handle->GetFloat("LightDropOffRate", 0.0);
        funcs["LightDropOffRate"] = &RenderParamsP::updateLightDropOffRate;
        SunDisc = this->handle->GetBool("SunDisc", false);
        funcs["SunDisc"] = &RenderParamsP::updateSunDisc;
        SunDiscSize = this->handle->GetFloat("SunDiscSize", 1.5);
        funcs["SunDiscSize"] = &RenderParamsP::updateSunDiscSize;
        GroundReflection = this->handle->GetBool("GroundReflection", false);
        funcs["GroundReflection"] = &RenderParamsP::updateGroundReflection;
        GroundReflectionIntensity = this->handle->GetFloat("GroundReflectionIntensity", 0.4);
        funcs["GroundReflectionIntensity"] = &RenderParamsP::updateGroundReflectionIntensity;
        CyclesDevice = this->handle->GetASCII("CyclesDevice", "CPU");
        funcs["CyclesDevice"] = &RenderParamsP::updateCyclesDevice;
        CyclesSamples = this->handle->GetInt("CyclesSamples", 256);
        funcs["CyclesSamples"] = &RenderParamsP::updateCyclesSamples;
        CyclesTimeLimit = this->handle->GetFloat("CyclesTimeLimit", 0.0);
        funcs["CyclesTimeLimit"] = &RenderParamsP::updateCyclesTimeLimit;
        CyclesDenoise = this->handle->GetBool("CyclesDenoise", true);
        funcs["CyclesDenoise"] = &RenderParamsP::updateCyclesDenoise;
        CyclesPixelSize = this->handle->GetInt("CyclesPixelSize", 1);
        funcs["CyclesPixelSize"] = &RenderParamsP::updateCyclesPixelSize;
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
        DebugCullBounds = this->handle->GetBool("DebugCullBounds", false);
        funcs["DebugCullBounds"] = &RenderParamsP::updateDebugCullBounds;
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
    static void updateOutputTransform(RenderParamsP *self) {
        self->OutputTransform = self->handle->GetInt("OutputTransform", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateExposure(RenderParamsP *self) {
        self->Exposure = self->handle->GetFloat("Exposure", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMaxViewIds(RenderParamsP *self) {
        self->MaxViewIds = self->handle->GetInt("MaxViewIds", 1024);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBackgroundReleaseDelay(RenderParamsP *self) {
        self->BackgroundReleaseDelay = self->handle->GetInt("BackgroundReleaseDelay", 1000);
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
    static void updateMeshSkipRedundant(RenderParamsP *self) {
        self->MeshSkipRedundant = self->handle->GetBool("MeshSkipRedundant", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMeshSkipFinerResident(RenderParamsP *self) {
        self->MeshSkipFinerResident = self->handle->GetBool("MeshSkipFinerResident", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMeshSkipInvariant(RenderParamsP *self) {
        self->MeshSkipInvariant = self->handle->GetBool("MeshSkipInvariant", true);
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
    static void updateLevelDebug(RenderParamsP *self) {
        self->LevelDebug = self->handle->GetBool("LevelDebug", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelCeilingSimulateMB(RenderParamsP *self) {
        self->LevelCeilingSimulateMB = self->handle->GetInt("LevelCeilingSimulateMB", 0);
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
    static void updateLevelPressureRelease(RenderParamsP *self) {
        self->LevelPressureRelease = self->handle->GetFloat("LevelPressureRelease", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateClimbHardLimit(RenderParamsP *self) {
        self->ClimbHardLimit = self->handle->GetBool("ClimbHardLimit", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateClimbAdmitBatch(RenderParamsP *self) {
        self->ClimbAdmitBatch = self->handle->GetInt("ClimbAdmitBatch", 64);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelLandBudgetMS(RenderParamsP *self) {
        self->LevelLandBudgetMS = self->handle->GetInt("LevelLandBudgetMS", 50);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMeshSkipLanded(RenderParamsP *self) {
        self->MeshSkipLanded = self->handle->GetBool("MeshSkipLanded", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateVisualFillOnPool(RenderParamsP *self) {
        self->VisualFillOnPool = self->handle->GetBool("VisualFillOnPool", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateVisualFillMinFaces(RenderParamsP *self) {
        self->VisualFillMinFaces = self->handle->GetInt("VisualFillMinFaces", 2000);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWorkerVertexCache(RenderParamsP *self) {
        self->WorkerVertexCache = self->handle->GetInt("WorkerVertexCache", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCaptureBudgetMS(RenderParamsP *self) {
        self->CaptureBudgetMS = self->handle->GetInt("CaptureBudgetMS", 50);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelSlowBuildMS(RenderParamsP *self) {
        self->LevelSlowBuildMS = self->handle->GetInt("LevelSlowBuildMS", 200);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDescentOrderBatch(RenderParamsP *self) {
        self->DescentOrderBatch = self->handle->GetInt("DescentOrderBatch", 64);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDowngradeLedger(RenderParamsP *self) {
        self->DowngradeLedger = self->handle->GetBool("DowngradeLedger", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelCount(RenderParamsP *self) {
        self->LevelCount = self->handle->GetInt("LevelCount", 8);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelScale(RenderParamsP *self) {
        self->LevelScale = self->handle->GetFloat("LevelScale", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelBudgetDeadband(RenderParamsP *self) {
        self->LevelBudgetDeadband = self->handle->GetFloat("LevelBudgetDeadband", 0.03);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLevelScaleBoxError(RenderParamsP *self) {
        self->LevelScaleBoxError = self->handle->GetFloat("LevelScaleBoxError", 0.25);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSimplifyExhausted(RenderParamsP *self) {
        self->SimplifyExhausted = self->handle->GetBool("SimplifyExhausted", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSimplifyMergeParts(RenderParamsP *self) {
        self->SimplifyMergeParts = self->handle->GetBool("SimplifyMergeParts", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSimplifyMinReduction(RenderParamsP *self) {
        self->SimplifyMinReduction = self->handle->GetFloat("SimplifyMinReduction", 20.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShapeVertices(RenderParamsP *self) {
        self->ShapeVertices = self->handle->GetBool("ShapeVertices", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePressureDropEdges(RenderParamsP *self) {
        self->PressureDropEdges = self->handle->GetBool("PressureDropEdges", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateElementGateStagger(RenderParamsP *self) {
        self->ElementGateStagger = self->handle->GetInt("ElementGateStagger", 15);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateTinyElementCutoff(RenderParamsP *self) {
        self->TinyElementCutoff = self->handle->GetInt("TinyElementCutoff", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLoadDropElements(RenderParamsP *self) {
        self->LoadDropElements = self->handle->GetBool("LoadDropElements", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEffectResolution(RenderParamsP *self) {
        self->EffectResolution = self->handle->GetFloat("EffectResolution", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateTemporalAccum(RenderParamsP *self) {
        self->TemporalAccum = self->handle->GetBool("TemporalAccum", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateTemporalAccumSamples(RenderParamsP *self) {
        self->TemporalAccumSamples = self->handle->GetInt("TemporalAccumSamples", 32);
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
        self->OcclusionSoftware = self->handle->GetBool("OcclusionSoftware", true);
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
    static void updateOcclusionDemoteStreak(RenderParamsP *self) {
        self->OcclusionDemoteStreak = self->handle->GetInt("OcclusionDemoteStreak", 8);
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
    static void updateOcclusionBenefitProbe(RenderParamsP *self) {
        self->OcclusionBenefitProbe = self->handle->GetBool("OcclusionBenefitProbe", false);
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
    static void updateCavity(RenderParamsP *self) {
        self->Cavity = self->handle->GetBool("Cavity", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCavityRadius(RenderParamsP *self) {
        self->CavityRadius = self->handle->GetFloat("CavityRadius", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCavityValley(RenderParamsP *self) {
        self->CavityValley = self->handle->GetFloat("CavityValley", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCavityRidge(RenderParamsP *self) {
        self->CavityRidge = self->handle->GetFloat("CavityRidge", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMatcap(RenderParamsP *self) {
        self->Matcap = self->handle->GetBool("Matcap", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMatcapPreset(RenderParamsP *self) {
        self->MatcapPreset = self->handle->GetInt("MatcapPreset", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateMatcapTint(RenderParamsP *self) {
        self->MatcapTint = self->handle->GetFloat("MatcapTint", 1.0);
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
    static void updatePBRFromSpecular(RenderParamsP *self) {
        self->PBRFromSpecular = self->handle->GetBool("PBRFromSpecular", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateShininessMapping(RenderParamsP *self) {
        self->ShininessMapping = self->handle->GetInt("ShininessMapping", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePBREnvPreset(RenderParamsP *self) {
        self->PBREnvPreset = self->handle->GetInt("PBREnvPreset", 1);
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
        self->PBREnvEmbed = self->handle->GetBool("PBREnvEmbed", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePBREnvBackground(RenderParamsP *self) {
        self->PBREnvBackground = self->handle->GetBool("PBREnvBackground", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePBREnvBlur(RenderParamsP *self) {
        self->PBREnvBlur = self->handle->GetFloat("PBREnvBlur", 0.25);
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
    static void updateLight(RenderParamsP *self) {
        self->Light = self->handle->GetBool("Light", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightIntensity(RenderParamsP *self) {
        self->LightIntensity = self->handle->GetFloat("LightIntensity", 0.8);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightDirectionX(RenderParamsP *self) {
        self->LightDirectionX = self->handle->GetFloat("LightDirectionX", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightDirectionY(RenderParamsP *self) {
        self->LightDirectionY = self->handle->GetFloat("LightDirectionY", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightDirectionZ(RenderParamsP *self) {
        self->LightDirectionZ = self->handle->GetFloat("LightDirectionZ", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightColor(RenderParamsP *self) {
        self->LightColor = self->handle->GetUnsigned("LightColor", 0xF0FDFFFF);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightSpot(RenderParamsP *self) {
        self->LightSpot = self->handle->GetBool("LightSpot", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightPositionX(RenderParamsP *self) {
        self->LightPositionX = self->handle->GetFloat("LightPositionX", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightPositionY(RenderParamsP *self) {
        self->LightPositionY = self->handle->GetFloat("LightPositionY", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightPositionZ(RenderParamsP *self) {
        self->LightPositionZ = self->handle->GetFloat("LightPositionZ", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightCutOffAngle(RenderParamsP *self) {
        self->LightCutOffAngle = self->handle->GetFloat("LightCutOffAngle", 45.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateLightDropOffRate(RenderParamsP *self) {
        self->LightDropOffRate = self->handle->GetFloat("LightDropOffRate", 0.0);
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
    static void updateCyclesDevice(RenderParamsP *self) {
        self->CyclesDevice = self->handle->GetASCII("CyclesDevice", "CPU");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCyclesSamples(RenderParamsP *self) {
        self->CyclesSamples = self->handle->GetInt("CyclesSamples", 256);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCyclesTimeLimit(RenderParamsP *self) {
        self->CyclesTimeLimit = self->handle->GetFloat("CyclesTimeLimit", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCyclesDenoise(RenderParamsP *self) {
        self->CyclesDenoise = self->handle->GetBool("CyclesDenoise", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCyclesPixelSize(RenderParamsP *self) {
        self->CyclesPixelSize = self->handle->GetInt("CyclesPixelSize", 1);
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
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDebugCullBounds(RenderParamsP *self) {
        self->DebugCullBounds = self->handle->GetBool("DebugCullBounds", false);
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
const char *RenderParams::docOutputTransform() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Whether the engine is colour managed.\n"
"\n"
"The shading is linear -- mixes, the GGX lobe, the image based\n"
"lighting product are all arithmetic on light, and they are only\n"
"correct on linear numbers. A colour someone picked is not: it\n"
"is a display number, which makes it sRGB encoded. And a display\n"
"reads the byte it is handed as sRGB too.\n"
"\n"
"'sRGB' honours both ends. Authored colours -- materials, the\n"
"lights, the background, the base colour and emissive textures --\n"
"are decoded to linear as they enter, and the finished frame is\n"
"encoded once at the last write before it is shown. An UNSHADED\n"
"authored colour therefore survives the round trip exactly, and\n"
"so does a fully lit surface; what changes is the shading in\n"
"between, which is the part that was wrong.\n"
"\n"
"'Off' is the older pipeline, which did neither: it fed display\n"
"numbers to the linear shading and wrote the linear result out\n"
"raw. The two errors partly cancel -- a fully lit surface comes\n"
"out right -- but everything in falloff and shadow renders about\n"
"a gamma too dark. Documents written before this existed are\n"
"drawn that way, which is how they were authored.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOutputTransform() {
    return instance()->OutputTransform;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOutputTransform() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOutputTransform(const long &v) {
    instance()->handle->SetInt("OutputTransform",v);
    instance()->OutputTransform = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOutputTransform() {
    instance()->handle->RemoveInt("OutputTransform");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docExposure() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How much light the frame is developed with, as a plain\n"
"multiplier on the linear image before it is encoded for the\n"
"screen. One leaves it alone.\n"
"\n"
"It exists because a colour managed scene is lit in real\n"
"reflectances, and a mid grey reflects about 18 per cent of what\n"
"falls on it rather than the 45 per cent its number reads as. A\n"
"scene whose lights were set before that was true is lit about\n"
"two to three times too dimly, and this is the control that\n"
"answers it without touching a single light.\n"
"\n"
"Raising it does not clip. Anything the multiplier pushes past\n"
"the top of the range rolls off smoothly instead, and the roll\n"
"off is exactly nothing below the knee -- so at an exposure of\n"
"one the frame is bit for bit what it would have been without\n"
"this stage at all.\n"
"\n"
"Only meaningful while the output colour transform is on: with\n"
"it off the engine is not working in light, and a multiplier\n"
"there would scale display numbers rather than exposure.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getExposure() {
    return instance()->Exposure;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultExposure() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setExposure(const double &v) {
    instance()->handle->SetFloat("Exposure",v);
    instance()->Exposure = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeExposure() {
    instance()->handle->RemoveFloat("Exposure");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docMaxViewIds() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many backend view ids the render engine may hand out, which\n"
"is what decides how many 3D views can draw on it at once: each\n"
"view takes a block for its pass sequence (about 13 ids for a\n"
"plain viewer, docs/RenderEngine.md #3.1), and a view that finds\n"
"no block left falls back to plain GL rather than failing. So the\n"
"default is roughly 64 viewers, and 0 asks for the build's own\n"
"ceiling instead, which is four times that.\n"
"\n"
"It is worth having a limit below the ceiling because the backend\n"
"copies its whole view table once a frame and sizes its per-view\n"
"pools from this number, so ids nobody opens are still paid for\n"
"in every frame. Measured on a desktop GPU that cost is invisible\n"
"against a 16ms frame at this width - but at the ceiling, with\n"
"render stage timing on, it is not: the per-view GPU timer pools\n"
"take a 59fps session to 19. Raise it for many-viewer work, not\n"
"as a matter of course.\n"
"\n"
"Read once, when the backend starts: a change needs a restart.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getMaxViewIds() {
    return instance()->MaxViewIds;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultMaxViewIds() {
    const static long def = 1024;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setMaxViewIds(const long &v) {
    instance()->handle->SetInt("MaxViewIds",v);
    instance()->MaxViewIds = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeMaxViewIds() {
    instance()->handle->RemoveInt("MaxViewIds");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docBackgroundReleaseDelay() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Milliseconds a 3D view may sit in the background before it gives\n"
"its render targets back, or 0 to let a hidden view keep them.\n"
"\n"
"Targets are what a view mostly costs: 287MB was measured for one\n"
"1644x653 view with every effect on, and until now it held them\n"
"whether or not anyone could see it -- so a session with several\n"
"documents open paid for all of their views to look at one. This\n"
"gives that back for the views nobody is looking at. What the view\n"
"keeps is everything a resize keeps: its programs, its uniforms\n"
"and its uploaded scene, so coming back is the resize path and not\n"
"a reload.\n"
"\n"
"The delay is what stops it firing on a click through the tabs.\n"
"Coming back costs the one frame that rebuilds the targets (~68ms\n"
"on the view measured above) and gives a byte-identical picture --\n"
"the trade is a hitch on return against the memory in between,\n"
"never a difference in the image. Lower it to release sooner on a\n"
"machine short of VRAM; raise it if switching back and forth\n"
"hitches.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getBackgroundReleaseDelay() {
    return instance()->BackgroundReleaseDelay;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultBackgroundReleaseDelay() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setBackgroundReleaseDelay(const long &v) {
    instance()->handle->SetInt("BackgroundReleaseDelay",v);
    instance()->BackgroundReleaseDelay = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeBackgroundReleaseDelay() {
    instance()->handle->RemoveInt("BackgroundReleaseDelay");
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
const char *RenderParams::docMeshSkipRedundant() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ask the shape whether it is already tessellated the way this\n"
"rebuild wants it, and skip the tessellation call outright when it\n"
"is (docs/SceneStreaming.md #13e).\n"
"A visual rebuild always called BRepMesh_IncrementalMesh, on the\n"
"assumption that a mesh already resident makes the call nearly\n"
"free. Measured, it does not: half the calls of a mass descent --\n"
"2462 of 4942 -- changed no triangle at all and still cost about\n"
"19ms each, 27% of the whole descent's rebuild time, because\n"
"reaching the conclusion means building OCCT's internal mesh model\n"
"of the shape first.\n"
"The check asks the same question that model would have answered,\n"
"off the triangulations already hanging on the faces: OCCT's own\n"
"consistency rule (BRepMesh_ModelPreProcessor), per face, plus the\n"
"3D polygon of every free edge. It is all-or-nothing per shape and\n"
"deliberately the stricter test -- one face that would be\n"
"re-tessellated, one triangulation with an index out of range, and\n"
"the call runs exactly as before, because the fallback is the real\n"
"thing and there is nothing to gain by guessing.\n"
"A resident mesh FINER than the ask is not adequate. That is not\n"
"an oversight: the descent asks for a coarser mesh on purpose, to\n"
"give memory back, and OCCT would coarsen it. Skipping there would\n"
"quietly hold the memory the plan asked for.\n"
"Off, the call is made unconditionally, as it always was. With the\n"
"level plan narrating, the off arm also reports how often the\n"
"check and the call agreed, which is what says the check is safe.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getMeshSkipRedundant() {
    return instance()->MeshSkipRedundant;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultMeshSkipRedundant() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setMeshSkipRedundant(const bool &v) {
    instance()->handle->SetBool("MeshSkipRedundant",v);
    instance()->MeshSkipRedundant = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeMeshSkipRedundant() {
    instance()->handle->RemoveBool("MeshSkipRedundant");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docMeshSkipFinerResident() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Count a resident mesh FINER than the rebuild asked for as\n"
"adequate, instead of re-tessellating to coarsen it\n"
"(docs/SceneStreaming.md #13e). Only consulted when redundant\n"
"tessellation is being skipped at all.\n"
"Strictly, finer is not adequate: the descent asks coarse on\n"
"purpose to hand memory back, and OCCT coarsens the mesh when\n"
"asked with quality decrease allowed. That is why the check\n"
"refuses it by default -- accepting it would be the feature\n"
"quietly holding the memory the level plan asked for.\n"
"Measured on the descent, though, that is what the refusal is\n"
"actually costing and it is nearly all of it: 2599 of the 2765\n"
"refused calls had a resident mesh exactly twice as fine as the\n"
"ask -- the previous ladder rung, one dynamic scale step back --\n"
"and every one of them changed no triangle when the call was\n"
"made anyway. The faces were already at their floor; a face of\n"
"two triangles does not coarsen.\n"
"So this trades a coarsening that mostly achieves nothing for the\n"
"~19ms it costs to find that out. What it risks is the minority\n"
"where the coarsening WOULD have removed triangles, which is\n"
"memory the plan then has to recover some other way -- through\n"
"the refine pool's own coarser rung, where it was always meant to\n"
"come from.\n"
"OFF BY DEFAULT, and the reason is that risk, measured. Audited\n"
"with every call still made so the check can be scored against\n"
"what the call actually did, this rule predicted 3381 calls\n"
"redundant and 753 of them -- 22%, better than one in five --\n"
"rebuilt anyway. Those are real coarsenings it would have\n"
"skipped, and real memory the plan would not get back. The\n"
"strict rule's own score on the same instrument is 1 in 7403.\n"
"/!\\ Never read that count from a run with the skip ON: a call\n"
"that is skipped is never made, so nothing can say whether it\n"
"would have rebuilt, and the wrong-verdict column can only\n"
"count calls the check refused. A zero there is guaranteed by\n"
"construction rather than earned.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getMeshSkipFinerResident() {
    return instance()->MeshSkipFinerResident;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultMeshSkipFinerResident() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setMeshSkipFinerResident(const bool &v) {
    instance()->handle->SetBool("MeshSkipFinerResident",v);
    instance()->MeshSkipFinerResident = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeMeshSkipFinerResident() {
    instance()->handle->RemoveBool("MeshSkipFinerResident");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docMeshSkipInvariant() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Skip a tessellation call on a shape whose mesh provably cannot\n"
"depend on the deflection asked: every face planar, every edge\n"
"curve a straight line (docs/SceneStreaming.md #13e). A plane\n"
"deviates from its triangulation by zero and a straight edge\n"
"discretizes to its two endpoints at ANY deflection, so the call\n"
"would rebuild the identical mesh -- there is no ask, coarser or\n"
"finer, at which such a shape tessellates differently.\n"
"This is the geometric statement behind the measured descent\n"
"waste: most mechanical parts hit their floor immediately, and a\n"
"mass descent then pays ~19-38ms per object per step (56-60% of\n"
"all drop-phase mesh time on the rack model) for BRepMesh to\n"
"rebuild what cannot change. The empirical exhaustion proof the\n"
"ladder keeps (scaleSpent) cannot be used for a skip -- audited\n"
"twice, 14-20% of proved shapes resume coarsening at some later\n"
"ask, and those rebuilds reclaim real memory. The geometric rule\n"
"is immune to that leak: the shapes that resume are exactly the\n"
"curved ones it refuses to claim, and an all-linear mesh cannot\n"
"shrink, so no reclaim is ever forgone.\n"
"The classification walks surface and curve TYPES once per shape\n"
"and is cached; conservative on both counts (a trimmed or offset\n"
"plane, a straight b-spline, count as curved). The skip is also\n"
"refused while any face is missing its triangulation -- building\n"
"that is exactly the call's job.\n"
"With the level plan narrating and this OFF, the rule is still\n"
"evaluated and scored against every call it would have skipped --\n"
"read its WRONG column from that arm only; a run with the skip on\n"
"cannot score calls it never made.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getMeshSkipInvariant() {
    return instance()->MeshSkipInvariant;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultMeshSkipInvariant() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setMeshSkipInvariant(const bool &v) {
    instance()->handle->SetBool("MeshSkipInvariant",v);
    instance()->MeshSkipInvariant = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeMeshSkipInvariant() {
    instance()->handle->RemoveBool("MeshSkipInvariant");
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
const char *RenderParams::docLevelDebug() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Narrate what each mesh-level plan decides (docs/SceneStreaming.md\n"
"#13): the GPU budget it decided against and the bytes in use, how\n"
"many displayed sources stand at their coarse and exact rungs, and\n"
"how many refines, demotes and downgrades the plan asked for.\n"
"Reported on the plan's own cadence - a camera pause - because it\n"
"is a decision, not a per-frame cost.\n"
"Needed to tell a ladder that will not descend apart from one that\n"
"never ran: on the desktop OpenGL backend the automatic GPU budget\n"
"is 0 (bgfx's GL renderer reports no limit), so the downgrade half\n"
"of the plan never executed at all and nothing said so.\n"
"The FC_LEVEL_DEBUG environment variable also turns it on. Read\n"
"once, at the first plan.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getLevelDebug() {
    return instance()->LevelDebug;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultLevelDebug() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelDebug(const bool &v) {
    instance()->handle->SetBool("LevelDebug",v);
    instance()->LevelDebug = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelDebug() {
    instance()->handle->RemoveBool("LevelDebug");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelCeilingSimulateMB() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Pretend the system ran out of memory for exact re-tessellation\n"
"(docs/SceneStreaming.md #13), so the CPU-side half of the level\n"
"plan can be exercised on a machine that has memory to spare.\n"
"Non-zero raises the floor that the refine worker compares\n"
"available memory against, so builds are refused and a memory\n"
"ceiling is observed - after which the plans start demoting exact\n"
"meshes the camera would not miss back to their coarse rung.\n"
"A simulation knob, not a tuning one: LevelMemoryFloorMB is the\n"
"real floor, and this overrides it upward only.\n"
"Read when a refine is dequeued, so it takes effect live.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getLevelCeilingSimulateMB() {
    return instance()->LevelCeilingSimulateMB;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultLevelCeilingSimulateMB() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelCeilingSimulateMB(const long &v) {
    instance()->handle->SetInt("LevelCeilingSimulateMB",v);
    instance()->LevelCeilingSimulateMB = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelCeilingSimulateMB() {
    instance()->handle->RemoveInt("LevelCeilingSimulateMB");
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
const char *RenderParams::docLevelPressureRelease() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How much of the raised refine tolerance the plan keeps each\n"
"time it comes in under the GPU budget (docs/SceneStreaming.md\n"
"#13c.3). While the budget is exceeded the plan accepts visible\n"
"error to fit the scene, and it must not hand that error straight\n"
"back the moment one plan fits: measured on a 5455-object model at\n"
"a 64MB budget, clearing it in one step took the tolerance from\n"
"51 pixels to 2, asked 946 objects to re-tessellate at once, broke\n"
"the budget again and cycled -- 43 plans in 611 seconds with no\n"
"steady state at any point.\n"
"So quality comes back in steps: each plan that fits keeps this\n"
"fraction of the standing tolerance, and a step that puts the\n"
"scene back over budget is remembered as a floor the release never\n"
"passes again, so the ladder settles at the coarsest tolerance\n"
"that actually fits instead of oscillating around it. The floor is\n"
"forgotten when the camera moves or the budget changes, which is\n"
"when what a rung costs on screen changes.\n"
"Smaller gives quality back faster and risks the cycle; larger is\n"
"gentler and slower. 0 or less restores the immediate snap.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLevelPressureRelease() {
    return instance()->LevelPressureRelease;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLevelPressureRelease() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelPressureRelease(const double &v) {
    instance()->handle->SetFloat("LevelPressureRelease",v);
    instance()->LevelPressureRelease = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelPressureRelease() {
    instance()->handle->RemoveFloat("LevelPressureRelease");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docClimbHardLimit() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Whether the GPU budget is an absolute ceiling for the level\n"
"plan's climbs (docs/SceneStreaming.md #13c.5). With it on, a\n"
"plan whose allocator-exact uploaded total stands at or above\n"
"the budget admits NO refine and cancels every climb still in\n"
"flight -- the existing de-want pass aborts them -- and below\n"
"the ceiling climbs are admitted in small batches (Climb\n"
"admission batch) so the total approaches the ceiling in\n"
"verified steps instead of overshooting it in one plan. Judged\n"
"against the uploaded TOTAL, not the two-frame live census: the\n"
"census alternates under churn and is what let climbs land\n"
"over budget. A crossing is bounded by one batch's bytes;\n"
"per-climb pre-sizing needs rung-keyed GPU cache entries and is\n"
"future work. Off restores unadmitted climbing.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getClimbHardLimit() {
    return instance()->ClimbHardLimit;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultClimbHardLimit() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setClimbHardLimit(const bool &v) {
    instance()->handle->SetBool("ClimbHardLimit",v);
    instance()->ClimbHardLimit = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeClimbHardLimit() {
    instance()->handle->RemoveBool("ClimbHardLimit");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docClimbAdmitBatch() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many refines one plan may admit while the hard climb\n"
"limit is on and the uploaded total is under budget. Small\n"
"keeps the possible overshoot small and lets the next plan\n"
"re-check the allocator-exact total before admitting more;\n"
"large climbs faster. The set is not ordered by need within a\n"
"plan, but every plan re-evaluates the whole scene, so nothing\n"
"starves across plans.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getClimbAdmitBatch() {
    return instance()->ClimbAdmitBatch;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultClimbAdmitBatch() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setClimbAdmitBatch(const long &v) {
    instance()->handle->SetInt("ClimbAdmitBatch",v);
    instance()->ClimbAdmitBatch = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeClimbAdmitBatch() {
    instance()->handle->RemoveInt("ClimbAdmitBatch");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelLandBudgetMS() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How long one event-loop turn may spend landing finished\n"
"worker jobs (climb refines and descent coarsenings alike).\n"
"Landings arrive as queued events, and Qt delivers every\n"
"pending one in a single sweep -- a batch of 64 landings ran\n"
"back-to-back for measured 1-2.7s stretches in which no paint,\n"
"timer or input event was served. The pump runs landings until\n"
"this budget is spent, then yields the loop and reschedules;\n"
"a single landing larger than the budget still lands whole\n"
"(items are not sliceable). Small keeps the UI responsive\n"
"under a landing storm; large lands a converging scene sooner.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getLevelLandBudgetMS() {
    return instance()->LevelLandBudgetMS;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultLevelLandBudgetMS() {
    const static long def = 50;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelLandBudgetMS(const long &v) {
    instance()->handle->SetInt("LevelLandBudgetMS",v);
    instance()->LevelLandBudgetMS = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelLandBudgetMS() {
    instance()->handle->RemoveInt("LevelLandBudgetMS");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docMeshSkipLanded() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Whether the rebuild half of a landing skips its OCCT mesh\n"
"call. A worker landing (climb, scale-descent, stand-in\n"
"resolution) or a demote/downgrade installs or re-activates the\n"
"very triangulation the following rebuild displays, and on\n"
"every such path the resident rung is never coarser than the\n"
"ask -- BRepMesh there can only validate: measured 18.3s of a\n"
"92s budget drop (991 validated-only calls, 0.1-0.8s each on\n"
"large compounds), plus ~1s per landing of a giant re-FAILING\n"
"the faces the worker's mesher had already failed. Keyed on\n"
"the path of the one rebuild the landing just prepared, never\n"
"on the shape's descent history (the exhaustion-proof leak\n"
"that killed the spent-keyed skip does not reach a per-rebuild\n"
"claim). Audited at 94 percent exact no-ops; the rest are\n"
"BRepMesh re-meshing a few faces within ~5 percent of the\n"
"triangle count in either direction -- perturbation of a rung\n"
"the ladder chose to display, not reclaim forgone. The level\n"
"debug flag scores the claim either way; read the 'landed\n"
"rule' audit line before trusting a change here.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getMeshSkipLanded() {
    return instance()->MeshSkipLanded;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultMeshSkipLanded() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setMeshSkipLanded(const bool &v) {
    instance()->handle->SetBool("MeshSkipLanded",v);
    instance()->MeshSkipLanded = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeMeshSkipLanded() {
    instance()->handle->RemoveBool("MeshSkipLanded");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docVisualFillOnPool() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Whether the display-array fill of a big landing rebuild runs\n"
"on the refine worker pool instead of the GUI thread. After the\n"
"mesh call was skipped on landings (Skip mesh call on landing\n"
"rebuilds), the traversal that copies the resident\n"
"triangulations into the Coin arrays became the per-item floor\n"
"of the landing pump: 0.3-0.65s per 15-21k-face compound,\n"
"unsliceable, against a 200ms interactivity gate. With this on,\n"
"the rebuild captures handles to the resident triangulations\n"
"and edge polygons (the only state another thread may swap\n"
"under it -- the topology itself is immutable at runtime),\n"
"fills detached arrays on a worker, and lands them back through\n"
"the landing pump as plain array writes. The landing is\n"
"guarded by the shape identity and a per-object generation\n"
"count, so a rebuild that ran for any other reason in between\n"
"simply wins. Only rebuilds inside the landing pump with at\n"
"least 'Minimum faces for a pooled fill' faces take this path;\n"
"everything else fills inline exactly as before.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getVisualFillOnPool() {
    return instance()->VisualFillOnPool;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultVisualFillOnPool() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setVisualFillOnPool(const bool &v) {
    instance()->handle->SetBool("VisualFillOnPool",v);
    instance()->VisualFillOnPool = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeVisualFillOnPool() {
    instance()->handle->RemoveBool("VisualFillOnPool");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docVisualFillMinFaces() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many faces a landing rebuild must have before its\n"
"array fill goes to the refine pool (Fill landing rebuilds on\n"
"the refine pool). The fill measures ~30us per face on the\n"
"reference model, so the default parks roughly the >60ms\n"
"items; the thousands of small landings in a budget drop stay\n"
"on the cheap inline path rather than paying a snapshot, a\n"
"queue hop and a second landing each.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getVisualFillMinFaces() {
    return instance()->VisualFillMinFaces;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultVisualFillMinFaces() {
    const static long def = 2000;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setVisualFillMinFaces(const long &v) {
    instance()->handle->SetInt("VisualFillMinFaces",v);
    instance()->VisualFillMinFaces = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeVisualFillMinFaces() {
    instance()->handle->RemoveInt("VisualFillMinFaces");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docWorkerVertexCache() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Whether a scene publish adopts the vertex-cache content the\n"
"fill worker emitted at landing instead of re-capturing the\n"
"shape by traversal (docs/WorkerVertexCache.md). The capture\n"
"walks every triangle through a hash-dedup a second time to\n"
"rebuild exactly the arrays the fill already computed; with\n"
"this on, the worker emits those arrays next to the display\n"
"arrays and the publish installs them directly. Uniform-color\n"
"shapes only -- per-face colors, textures and marker sets fall\n"
"back to the traversal capture, as does any shape whose nodes\n"
"were touched after the landing registered the content. 0 is\n"
"off, 1 adopts, 2 adopts nothing but runs the traversal capture\n"
"and compares it against the worker's content, logging any\n"
"disagreement -- slow, for checking the emission, not for use.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getWorkerVertexCache() {
    return instance()->WorkerVertexCache;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultWorkerVertexCache() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setWorkerVertexCache(const long &v) {
    instance()->handle->SetInt("WorkerVertexCache",v);
    instance()->WorkerVertexCache = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeWorkerVertexCache() {
    instance()->handle->RemoveInt("WorkerVertexCache");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCaptureBudgetMS() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How long one scene publish may spend re-capturing changed\n"
"shapes into vertex caches before the rest are deferred. The\n"
"capture walks a changed shape's primitives one triangle at a\n"
"time, and during a descent storm every landed batch pays that\n"
"on the next paint: mid-paint stack samples put the capture at\n"
"about half of 250-850ms publish frames. Once this budget is\n"
"spent, each remaining changed shape keeps its previous vertex\n"
"cache for this frame (a shape captured for the first time\n"
"stays out of the frame entirely -- progressive appearance,\n"
"same as a live import), the caches on its path are left\n"
"unclosed for reuse, and another publish is scheduled; captured\n"
"shapes turn valid and prune, so successive frames always make\n"
"progress. The display is at worst a few frames stale in a\n"
"scene that is churning anyway; a single changed object never\n"
"comes near the budget. 0 captures everything in one frame,\n"
"as before this parameter existed.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getCaptureBudgetMS() {
    return instance()->CaptureBudgetMS;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultCaptureBudgetMS() {
    const static long def = 50;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCaptureBudgetMS(const long &v) {
    instance()->handle->SetInt("CaptureBudgetMS",v);
    instance()->CaptureBudgetMS = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCaptureBudgetMS() {
    instance()->handle->RemoveInt("CaptureBudgetMS");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelSlowBuildMS() {
    return QT_TRANSLATE_NOOP("RenderParams",
"A visual rebuild whose own cost passes this many\n"
"milliseconds reports its time split (traversal, mesh,\n"
"prologue, instancing, highlight) on one line naming the\n"
"object, under the level debug flag. The aggregate split says\n"
"where a mass descent's time goes; the landing pump's worst\n"
"turn is a single object's whole rebuild, and only a per-build\n"
"line says what that object spent it on. The same threshold\n"
"arms the slow-dispatch line in GUIApplication::notify, which\n"
"names the receiver of any single event-loop dispatch this\n"
"slow -- the net that catches a stall no timer above\n"
"bracketed. 0 turns both lines off.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getLevelSlowBuildMS() {
    return instance()->LevelSlowBuildMS;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultLevelSlowBuildMS() {
    const static long def = 200;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelSlowBuildMS(const long &v) {
    instance()->handle->SetInt("LevelSlowBuildMS",v);
    instance()->LevelSlowBuildMS = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelSlowBuildMS() {
    instance()->handle->RemoveInt("LevelSlowBuildMS");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDescentOrderBatch() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many descents (demotes/downgrades) one plan pass may\n"
"order, free tier and priced tier together; 0 removes the cap.\n"
"Each order enqueues a worker job -- the coarsening itself runs\n"
"on the refine pool -- but the enqueue snapshots the object's\n"
"display arrays on the GUI thread, so an unbounded pass (the\n"
"measured 1500-order plans) is itself a stall. Deferred\n"
"candidates keep their hooks and the replan after the batch\n"
"lands re-finds them, so nothing is refused, only paced -- the\n"
"climb admission batch's mirror.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getDescentOrderBatch() {
    return instance()->DescentOrderBatch;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultDescentOrderBatch() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDescentOrderBatch(const long &v) {
    instance()->handle->SetInt("DescentOrderBatch",v);
    instance()->DescentOrderBatch = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDescentOrderBatch() {
    instance()->handle->RemoveInt("DescentOrderBatch");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDowngradeLedger() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Whether the GPU downgrade sweep carries its own unlanded\n"
"orders as credit against the next plan's deficit\n"
"(docs/SceneStreaming.md #13c.4). A downgrade frees exactly the\n"
"bytes it prices, but not WHEN the plan next looks: the swap\n"
"uploads the coarse rung immediately while the fine buffers\n"
"leave the live meter only after the collection window -- on a\n"
"heavy scene, seconds -- so a plan sampling mid-transition reads\n"
"old+new at once, computes a larger deficit than the one just\n"
"covered, and walks other sources further down. Measured on a\n"
"5455-object model at 64MB with the camera inside the assembly:\n"
"single plans requesting 1500+ downgrades, live tripling during\n"
"the storm, and the whole registry drained to its bottom rung\n"
"while the settled memory was under budget all along.\n"
"With the ledger, promised bytes hold the sweep until they are\n"
"observed landing or written off a few frames after the ordered\n"
"worker jobs have all drained (an order's bytes cannot land\n"
"before its descent job does); off restores the storming\n"
"behaviour for comparison.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDowngradeLedger() {
    return instance()->DowngradeLedger;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDowngradeLedger() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDowngradeLedger(const bool &v) {
    instance()->handle->SetBool("DowngradeLedger",v);
    instance()->DowngradeLedger = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDowngradeLedger() {
    instance()->handle->RemoveBool("DowngradeLedger");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelCount() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many rungs the fidelity ladder declares\n"
"(docs/SceneStreaming.md #13). Rung n is tessellated at a\n"
"deflection of the shape diagonal over 8<<n, so rung 0 is the\n"
"coarsest and each further rung halves the error; this bounds\n"
"what Coarse tessellation level may select and how far a source\n"
"may climb. Raising it adds finer rungs, not coarser ones -- to\n"
"go below rung 0 the plan scales an object's error instead, see\n"
"Level scale.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getLevelCount() {
    return instance()->LevelCount;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultLevelCount() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelCount(const long &v) {
    instance()->handle->SetInt("LevelCount",v);
    instance()->LevelCount = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelCount() {
    instance()->handle->RemoveInt("LevelCount");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelScale() {
    return QT_TRANSLATE_NOOP("RenderParams",
"What the level plan multiplies an object's error by when it\n"
"must free memory and every ordinary descent is exhausted\n"
"(docs/SceneStreaming.md #13). Rung 0 is not the floor: under a\n"
"budget the plan keeps picking objects -- individually, cheapest\n"
"visible error first, never the whole scene at once -- and\n"
"re-tessellates each one this much coarser again, until the\n"
"model fits. An object whose scaled error reaches Level scale\n"
"box error is replaced by its bounding box, which is the real\n"
"floor: coarsening a deflection cannot drop a planar face below\n"
"the two triangles it always has, and on a measured STEP\n"
"assembly a 4x coarser tessellation removed only 19% of the\n"
"primitives. 1 or less turns dynamic scaling off, and then a\n"
"budget under what rung 0 costs cannot be honoured.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLevelScale() {
    return instance()->LevelScale;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLevelScale() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelScale(const double &v) {
    instance()->handle->SetFloat("LevelScale",v);
    instance()->LevelScale = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelScale() {
    instance()->handle->RemoveFloat("LevelScale");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelBudgetDeadband() {
    return QT_TRANSLATE_NOOP("RenderParams",
"The rest band above the GPU memory budget, as a fraction of\n"
"it, inside which the level plan orders NO downgrades. The sweep\n"
"triggers only past budget*(1+this) and still corrects back to\n"
"the budget itself, so the band is hysteresis, not a higher\n"
"budget.\n"
"Without it an equilibrium that lands ON the budget line has\n"
"nowhere to rest: the plan orders 2-3 downgrades, the release\n"
"staircase re-wants the quality back, and the ladder dithers\n"
"0.2-0.4MB across the line for as long as the process lives --\n"
"measured on the rack model as the difference between a run\n"
"that settles in ~250s and one that churns its whole 600s\n"
"window. Climbs already stop AT the budget (Climb hard limit),\n"
"so inside the band neither direction acts and the plans go\n"
"genuinely quiet; pressure counts as standing there, which\n"
"keeps the raised tolerance and the edge gate latched exactly\n"
"as they were while the equilibrium was reached.\n"
"The band tolerates standing that fraction over the stated\n"
"budget (about 2MB at 64MB). 0 restores the bare line and with\n"
"it the dither.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLevelBudgetDeadband() {
    return instance()->LevelBudgetDeadband;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLevelBudgetDeadband() {
    const static double def = 0.03;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelBudgetDeadband(const double &v) {
    instance()->handle->SetFloat("LevelBudgetDeadband",v);
    instance()->LevelBudgetDeadband = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelBudgetDeadband() {
    instance()->handle->RemoveFloat("LevelBudgetDeadband");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLevelScaleBoxError() {
    return QT_TRANSLATE_NOOP("RenderParams",
"The scaled error at which an object stops being tessellated\n"
"at all and is drawn as its bounding box (12 triangles whatever\n"
"its face count), expressed relative to the shape diagonal. This\n"
"is where the ladder stops paying for topology it can no longer\n"
"resolve: past roughly a quarter of the diagonal a re-tessellated\n"
"shape and its box commit similar error, and only the box\n"
"actually removes the faces. 0 or less never substitutes a box.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLevelScaleBoxError() {
    return instance()->LevelScaleBoxError;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLevelScaleBoxError() {
    const static double def = 0.25;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLevelScaleBoxError(const double &v) {
    instance()->handle->SetFloat("LevelScaleBoxError",v);
    instance()->LevelScaleBoxError = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLevelScaleBoxError() {
    instance()->handle->RemoveFloat("LevelScaleBoxError");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docSimplifyExhausted() {
    return QT_TRANSLATE_NOOP("RenderParams",
"When re-tessellating an object coarser stops removing\n"
"geometry, decimate the mesh it already has instead of dropping\n"
"straight to its bounding box (docs/SceneStreaming.md #13c).\n"
"The descent coarsens an object by asking OCCT for a larger\n"
"deflection, and that saturates: a planar face is two triangles\n"
"at any deflection, so a shape of flat faces answers the same\n"
"mesh however coarse the ask. Past that point the only thing\n"
"that removes geometry is a representation with fewer faces.\n"
"Vertex clustering is the rung between the two: it keeps the\n"
"object's shape, where the bounding box does not.\n"
"Rewrites the display nodes only. Nothing re-tessellates and the\n"
"OCCT triangulation is untouched, so the way back is one ordinary\n"
"rebuild, and each further step down clusters on a coarser grid.\n"
"Face and edge numbering survive: a face that decimates away to\n"
"nothing keeps its (empty) slot, because those tables are read by\n"
"element number.\n"
"What it gives up is exactness of the decimated rung -- section\n"
"caps through it can be rough, since clustering does not preserve\n"
"watertightness, and the hidden-line seam filter is dropped\n"
"because a welded edge may fold a seam and a non-seam together.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getSimplifyExhausted() {
    return instance()->SimplifyExhausted;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultSimplifyExhausted() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setSimplifyExhausted(const bool &v) {
    instance()->handle->SetBool("SimplifyExhausted",v);
    instance()->SimplifyExhausted = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeSimplifyExhausted() {
    instance()->handle->RemoveBool("SimplifyExhausted");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docSimplifyMergeParts() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Let the decimator weld vertices across face boundaries\n"
"instead of clustering each face on its own grid.\n"
"Off, no output triangle spans two faces, so a modelled crease\n"
"stays a crease and each face keeps at least the triangles its\n"
"own cells produce. That floor is the catch: this rung is reached\n"
"precisely when a shape is mostly flat faces, and per-face\n"
"clustering cannot take a two-triangle face below two triangles.\n"
"On, positions and attributes cluster once over the whole mesh,\n"
"which is what actually removes geometry there -- at the cost of\n"
"shading round creases the model really has.\n"
"Face identity survives either way: a triangle still belongs to\n"
"the face it came from, so per-face colour and selection keep\n"
"working. Only the geometry is shared.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getSimplifyMergeParts() {
    return instance()->SimplifyMergeParts;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultSimplifyMergeParts() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setSimplifyMergeParts(const bool &v) {
    instance()->handle->SetBool("SimplifyMergeParts",v);
    instance()->SimplifyMergeParts = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeSimplifyMergeParts() {
    instance()->handle->RemoveBool("SimplifyMergeParts");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docSimplifyMinReduction() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How much of an object's triangle count a decimation pass has\n"
"to remove for the result to be kept, as a percentage.\n"
"Below it the pass is refused and the descent takes its next step\n"
"instead, which is the bounding box. A rung that removes almost\n"
"nothing is worse than not having one: it costs a node rewrite\n"
"and still holds the memory that made the plan ask.\n"
"This is also what stops the descent looping. Each step clusters\n"
"on a coarser grid, so a mesh that has run out of things to merge\n"
"keeps answering no and the object moves on to the box.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getSimplifyMinReduction() {
    return instance()->SimplifyMinReduction;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultSimplifyMinReduction() {
    const static double def = 20.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setSimplifyMinReduction(const double &v) {
    instance()->handle->SetFloat("SimplifyMinReduction",v);
    instance()->SimplifyMinReduction = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeSimplifyMinReduction() {
    instance()->handle->RemoveFloat("SimplifyMinReduction");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docShapeVertices() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Let the vertex points that sit on the ends of a shape's edges\n"
"draw under the element contract (docs/SceneStreaming.md #13b):\n"
"an attached point set draws only while its object's line set is\n"
"shown and memory allows, is the FIRST class dropped under\n"
"pressure and the LAST taken back. Off suppresses attached point\n"
"sets outright, memory or not.\n"
"A point is not cheap: it costs the GPU a 32-byte sprite instance\n"
"record plus its index, roughly nine times what it occupies in\n"
"the heap, which is why a CPU-currency measurement made them look\n"
"negligible.\n"
"All or nothing per point set, and only ATTACHED sets are ever\n"
"gated: one floating vertex -- one no edge touches, and every\n"
"point of a point cloud -- and the whole set ranks with the\n"
"faces, because nothing else would show it. Objects are in\n"
"practice all floating or none, so a per-vertex subset would buy\n"
"nothing and cost an index permutation.\n"
"It never applies in the Points display mode, where the vertices\n"
"are what the mode exists to show.\n"
"Picking, pre-selection and selection highlighting are unaffected:\n"
"the point geometry stays published and resident, the highlight\n"
"draws render on top as always, and only the base-pass submission\n"
"is skipped.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getShapeVertices() {
    return instance()->ShapeVertices;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultShapeVertices() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setShapeVertices(const bool &v) {
    instance()->handle->SetBool("ShapeVertices",v);
    instance()->ShapeVertices = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeShapeVertices() {
    instance()->handle->RemoveBool("ShapeVertices");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docPressureDropEdges() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Let the pressure stages of the element contract\n"
"(docs/SceneStreaming.md #13b) stop drawing the edges that bound\n"
"faces. Under the contract an attached line set draws only while\n"
"its object's face set is shown and memory allows; pressure\n"
"spends the classes points -> lines -> faces and takes them back\n"
"in reverse, and this is the switch on the lines stage. Off\n"
"exempts line sets from the pressure stages (a loading document\n"
"still drops them).\n"
"Edge geometry is the GPU's most expensive geometry per unit of\n"
"screen information: a segment is 8 bytes of index in the heap\n"
"and those 8 bytes plus a 64-byte quad-expansion instance record\n"
"on the GPU.\n"
"All or nothing per edge set, attached sets only: one floating\n"
"edge -- a wire, a sketch, a datum line, any edge no face uses --\n"
"and the whole set ranks with the faces, because it is the\n"
"object, and dropping it would show nothing at all.\n"
"It never applies in the Wireframe display mode, where the edges\n"
"are what the mode exists to show.\n"
"A display gate, not a residency change -- nothing is demoted and\n"
"nothing re-tessellates, so entering and leaving it costs one\n"
"frame, which is why it is spent before any rung is given up.\n"
"Picking, highlighting and on-top rendering are unaffected.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getPressureDropEdges() {
    return instance()->PressureDropEdges;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultPressureDropEdges() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPressureDropEdges(const bool &v) {
    instance()->handle->SetBool("PressureDropEdges",v);
    instance()->PressureDropEdges = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePressureDropEdges() {
    instance()->handle->RemoveBool("PressureDropEdges");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docElementGateStagger() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many frames the element contract's pressure latch waits\n"
"between stages (docs/SceneStreaming.md #13b), both escalating\n"
"(points dropped, then lines if the budget is still exceeded) and\n"
"releasing (lines back, then points, once the ladder has given\n"
"back all raised error). The wait is what lets the buffer\n"
"collector's census answer whether the cheaper stage was enough\n"
"before the next one is spent, and what keeps the release from\n"
"re-opening into the memory the collector just freed.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getElementGateStagger() {
    return instance()->ElementGateStagger;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultElementGateStagger() {
    const static long def = 15;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setElementGateStagger(const long &v) {
    instance()->handle->SetInt("ElementGateStagger",v);
    instance()->ElementGateStagger = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeElementGateStagger() {
    instance()->handle->RemoveInt("ElementGateStagger");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docTinyElementCutoff() {
    return QT_TRANSLATE_NOOP("RenderParams",
"MEASUREMENT INSTRUMENT, 0 = off. Suppress every line and\n"
"point draw issuing this many primitives or fewer, regardless of\n"
"the element contract -- floating sets included, which is the\n"
"point: the contract deliberately never gates those, and they\n"
"are what a far-field cut is left drawing\n"
"(docs/FarFieldProxies.md 11.1i).\n"
"\n"
"It exists to price the DRAW axis, which this engine has only\n"
"ever measured in the opposite regime. docs/DrawSubmission.md\n"
"dismissed draw count on a frame averaging ~1540 primitives per\n"
"draw, where the GPU is geometry-bound and a draw is free; the\n"
"far-field residue is ~12 primitives per draw, where a draw is\n"
"nearly all overhead. Setting this to ~24 on MiSTer removes\n"
"about 2% of the primitives and about 44% of the draws, so any\n"
"frame-time difference is attributable to draw count and not to\n"
"geometry.\n"
"\n"
"Not a display feature: it makes real edges vanish, and picking,\n"
"highlighting and on-top draws are exempt so the scene stays\n"
"usable while it is on.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getTinyElementCutoff() {
    return instance()->TinyElementCutoff;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultTinyElementCutoff() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setTinyElementCutoff(const long &v) {
    instance()->handle->SetInt("TinyElementCutoff",v);
    instance()->TinyElementCutoff = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeTinyElementCutoff() {
    instance()->handle->RemoveInt("TinyElementCutoff");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLoadDropElements() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Stop drawing edges AND vertices for as long as a document is\n"
"still arriving (docs/SceneStreaming.md #13b), and let the two\n"
"standing gates above decide again the moment it has finished.\n"
"A load is when the tier can least afford those two classes and\n"
"can least use them: the faces are arriving coarse-first and\n"
"being replaced under the camera, nobody inspects a vertex of a\n"
"model that is still half there, and every byte not uploaded to\n"
"an edge instance buffer now is one the arriving geometry gets\n"
"instead.\n"
"RE-MEASURED 2026-08-15, and the earlier reading no longer\n"
"holds. It used to suppress NOTHING on a .FCStd open: the load\n"
"parked every visual build and published in one step at the\n"
"end, so the renderer held an empty scene throughout -- 0\n"
"drawables across 17.8s on a 5455-object model. The publish is\n"
"incremental now, so the same open feeds the scene while the\n"
"drain runs and the gate has real work: on the same model it\n"
"climbs from 1123 to 5909 point and line draws suppressed, out\n"
"of 11818 eligible in a 17727-drawable scene, and both edges\n"
"are logged -- ON with an empty scene, OFF as the drain ends.\n"
"It overrides both gates while it lasts -- vertices drop even\n"
"with ShapeVertices on, edges drop with no pressure yet declared\n"
"-- but it is subject to the same all-or-nothing classification\n"
"and the same display-mode exemptions: a wire, a sketch, a datum\n"
"line or a point cloud draws throughout, because nothing else on\n"
"screen would show it, and neither class is dropped in the mode\n"
"that exists to show it.\n"
"Independent of this gate, the contract's dependency rule already\n"
"holds back an attached point or line set whose companion the\n"
"publish's capture budget deferred: an adopted vertex cache never\n"
"draws frames ahead of the face set it decorates, load gate or\n"
"not.\n"
"Costs one frame to leave, like the pressure gate, so what it\n"
"holds back comes straight back when the load lets go.\n"
"Applies only where coarse-first is on (CoarseTessellation 0 or\n"
"above): with everything tessellated exact up front there is no\n"
"progressive arrival for this to make room for.\n"
"A load here means a document restoring, a progressive import\n"
"filling one, or the deferred view-provider drain that follows a\n"
"restore -- geometry is still being built into the view in all\n"
"three.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getLoadDropElements() {
    return instance()->LoadDropElements;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultLoadDropElements() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLoadDropElements(const bool &v) {
    instance()->handle->SetBool("LoadDropElements",v);
    instance()->LoadDropElements = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLoadDropElements() {
    instance()->handle->RemoveBool("LoadDropElements");
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
const char *RenderParams::docTemporalAccum() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Keep refining the image while the camera holds still.\n"
"\n"
"Multisampling antialiases the geometry it rasterizes and nothing\n"
"else: every sample inside one triangle is shaded once, so a\n"
"specular highlight crawling across a curved surface, a normal or\n"
"texture detail below the pixel, and every screen-space pass\n"
"computed after the resolve -- ambient occlusion, outlines,\n"
"section caps, the light shafts -- are left exactly as aliased or\n"
"as noisy as they were drawn. More coverage samples cannot help\n"
"any of them.\n"
"\n"
"This spends time instead. Once the camera stops, each further\n"
"frame offsets the projection by a fraction of a pixel and\n"
"averages into what is already on screen, so the whole pipeline\n"
"converges toward what supersampling it would have given -- and\n"
"it costs nothing at all while anything is moving.\n"
"\n"
"There is no reprojection and no history rejection, because\n"
"nothing moved: the accumulation is thrown away outright on any\n"
"camera, scene or highlight change, so a drag or an orbit returns\n"
"to the ordinary multisampled frame immediately with no ghosting,\n"
"smearing or trailing on thin edges. It is a refinement on top of\n"
"multisampling, not a replacement for it -- leave the antialiasing\n"
"preference where it is.\n"
"\n"
"The cost is idle GPU time: a parked view keeps drawing until it\n"
"has converged (TemporalAccumSamples), then stops and asks for\n"
"nothing more. On a laptop or a tablet that is battery, which is\n"
"why this is off by default and why it does not travel in a saved\n"
"document.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getTemporalAccum() {
    return instance()->TemporalAccum;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultTemporalAccum() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setTemporalAccum(const bool &v) {
    instance()->handle->SetBool("TemporalAccum",v);
    instance()->TemporalAccum = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeTemporalAccum() {
    instance()->handle->RemoveBool("TemporalAccum");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docTemporalAccumSamples() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many jittered samples the idle accumulation converges over\n"
"before the view goes quiet (2-256, TemporalAccum only).\n"
"\n"
"The sequence is a Halton (2,3) pair over the pixel, so it fills\n"
"the pixel evenly at every count rather than clumping, and it is\n"
"indexed by sample number -- frame N of an accumulation is the\n"
"same frame N every time, which is what keeps a rendered\n"
"comparison reproducible.\n"
"\n"
"Most of the visible gain arrives in the first handful of\n"
"samples, since the error of an average falls with the square\n"
"root of the count: 32 halves the residual noise of 8, and 128\n"
"halves it again for four times the work. Raise it for a still\n"
"worth waiting on, lower it to reach the quiet state sooner.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getTemporalAccumSamples() {
    return instance()->TemporalAccumSamples;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultTemporalAccumSamples() {
    const static long def = 32;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setTemporalAccumSamples(const long &v) {
    instance()->handle->SetInt("TemporalAccumSamples",v);
    instance()->TemporalAccumSamples = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeTemporalAccumSamples() {
    instance()->handle->RemoveInt("TemporalAccumSamples");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docOcclusion() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Skip drawing what the depth buffer proves could not have\n"
"reached the screen (docs/FarFieldProxies.md §12). Bounding boxes\n"
"of the spatial index's nodes are tested against the depth the\n"
"occluders leave behind -- by default in a software depth buffer\n"
"on the CPU (Render_OcclusionSoftware), which answers within the\n"
"frame that asked -- and a node that puts no pixel through has\n"
"its whole subtree skipped, one test standing for thousands of\n"
"draws.\n"
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
"A test is issued against one frame's depth and read against a\n"
"later one -- it does not block, because stalling for it would\n"
"cost the frame time the culling exists to save -- so while an\n"
"answer is in flight, other geometry is culled and the occluders\n"
"move underneath it. Acted on singly, a node tested while an\n"
"occluder was still drawn gets skipped after that occluder has\n"
"gone; the hole it leaves tests visible; it comes back; and it\n"
"oscillates, which is a picture that flickers rather than one\n"
"that is merely wrong.\n"
"\n"
"Confirmations DILUTE that oscillation; measured, they do not\n"
"remove it (docs/FarFieldProxies.md #12.7): the false answers\n"
"arrive in runs, so tripling the confirmations bought a factor\n"
"of two, and the residual damage tracks how often nodes are\n"
"re-tested, which this setting cannot reach. The query path is\n"
"therefore not image-stable at any value here; occlusion on the\n"
"CPU (the default oracle) does not read this setting at all.");
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
"(docs/FarFieldProxies.md #12.12). The default, because it is\n"
"the one oracle whose picture holds still.\n"
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
    const static bool def = true;
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
const char *RenderParams::docOcclusionDemoteStreak() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many consecutive frames every draw of an object must have\n"
"been culled before the level plan's downgrade sweep may treat\n"
"it as free -- give its GPU upload back without charging the\n"
"camera any visible error. 0 never does. Only used when\n"
"occlusion runs on the CPU, whose verdicts are exact per frame.\n"
"\n"
"This is occlusion acting as a MEMORY mechanism: an enclosed\n"
"assembly's interior is inside the view frustum, so without a\n"
"hidden verdict the plan prices its downgrade as visible error\n"
"and pays for it in quality somewhere that actually shows. What\n"
"the sweep drops stays resident in CPU RAM; the way back is an\n"
"ordinary refine, so a verdict the camera later overturns costs\n"
"one upload. The streak is the hysteresis that keeps a drifting\n"
"camera from paying that upload per flap.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getOcclusionDemoteStreak() {
    return instance()->OcclusionDemoteStreak;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultOcclusionDemoteStreak() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionDemoteStreak(const long &v) {
    instance()->handle->SetInt("OcclusionDemoteStreak",v);
    instance()->OcclusionDemoteStreak = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionDemoteStreak() {
    instance()->handle->RemoveInt("OcclusionDemoteStreak");
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
const char *RenderParams::docOcclusionBenefitProbe() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Measure whether the culling pays for itself on THIS scene and\n"
"camera (docs/FarFieldProxies.md 12.13): alternate stretches of\n"
"frames with the whole occlusion block on and off, compare median\n"
"frame cost, and print the verdict with the culling readout\n"
"(Render_LevelDebug cadence). The probe is an intervention -- its\n"
"off arm draws everything and pauses the hidden-streak demote\n"
"feed for those frames -- so it is a measuring instrument, not a\n"
"mode to leave on. The verdict gates nothing yet; it is the\n"
"number the wire-or-delete decision for CullBenefitEstimator\n"
"reads.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getOcclusionBenefitProbe() {
    return instance()->OcclusionBenefitProbe;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultOcclusionBenefitProbe() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setOcclusionBenefitProbe(const bool &v) {
    instance()->handle->SetBool("OcclusionBenefitProbe",v);
    instance()->OcclusionBenefitProbe = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeOcclusionBenefitProbe() {
    instance()->handle->RemoveBool("OcclusionBenefitProbe");
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
"Render the shadow map cast by the Shadow display style's scene\n"
"light (and the god-ray shafts / caustic occlusion that depend on\n"
"it). A convenience switch to drop shadows without leaving the\n"
"Shadow display style; the base headlight and environment lighting\n"
"stay, so the scene remains lit, just flatter. Has no effect unless\n"
"the Shadow display style provides a scene light.");
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
const char *RenderParams::docCavity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable screen space cavity (curvature) shading of the\n"
"experimental render engine (render cache mode 3 with a selected\n"
"renderer type). Darkens concave creases and convex ridges found\n"
"in the geometry prepass normals, which makes surface shape and\n"
"small features read without relying on the lighting.\n"
"\n"
"Best paired with the Shaded draw style, the one that draws no\n"
"edges: there the darkened crease is the only thing stating where\n"
"a face ends, so cavity does the job the edge lines do elsewhere,\n"
"without the wireframe over every tessellated curve. In a style\n"
"that already draws edges (Flat Lines) the two land on the same\n"
"pixels and cavity mostly restates them.\n"
"\n"
"Independent of ambient occlusion: cavity is a local curvature\n"
"term, occlusion is a visibility integral over a world-space\n"
"radius (contact darkening). They compose.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getCavity() {
    return instance()->Cavity;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultCavity() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCavity(const bool &v) {
    instance()->handle->SetBool("Cavity",v);
    instance()->Cavity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCavity() {
    instance()->handle->RemoveBool("Cavity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCavityRadius() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Baseline the cavity curvature is measured over, in pixels.\n"
"\n"
"This decides which features the pass can see at all. The term\n"
"reads how far the surface normal turns between the two\n"
"neighbours, so at the default of 1 it sees only what turns\n"
"within a single pixel: hard creases, crisply, which is what\n"
"stands in for the edge lines the Shaded draw style does not\n"
"draw. Widening it brings broad curvature (fillets, blends, a\n"
"sculpted face) in, at the cost of spreading a hard crease into a\n"
"band of this width.\n"
"\n"
"Being in pixels it is resolution-relative: the same value covers\n"
"less of the model on a high-DPI display, so a large model on a\n"
"dense screen may want more than 1.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getCavityRadius() {
    return instance()->CavityRadius;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultCavityRadius() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCavityRadius(const double &v) {
    instance()->handle->SetFloat("CavityRadius",v);
    instance()->CavityRadius = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCavityRadius() {
    instance()->handle->RemoveFloat("CavityRadius");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCavityValley() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Cavity darkening strength in concave creases (inside corners,\n"
"fillets, pockets). Zero disables the valley term.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getCavityValley() {
    return instance()->CavityValley;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultCavityValley() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCavityValley(const double &v) {
    instance()->handle->SetFloat("CavityValley",v);
    instance()->CavityValley = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCavityValley() {
    instance()->handle->RemoveFloat("CavityValley");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCavityRidge() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Cavity darkening strength on convex ridges (outside corners,\n"
"chamfers). Reads as a soft contour along edges. Zero disables the\n"
"ridge term.\n"
"\n"
"Both terms darken: the pass multiplies the finished 8-bit scene\n"
"color, which cannot brighten past white, so the ridge highlight\n"
"some workbench renderers use is not available here.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getCavityRidge() {
    return instance()->CavityRidge;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultCavityRidge() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCavityRidge(const double &v) {
    instance()->handle->SetFloat("CavityRidge",v);
    instance()->CavityRidge = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCavityRidge() {
    instance()->handle->RemoveFloat("CavityRidge");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docMatcap() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable matcap shading of the experimental render engine\n"
"(render cache mode 3 with a selected renderer type). Replaces\n"
"the scene's lighting with a fixed studio attached to the camera,\n"
"looked up by each fragment's view space normal: the shading of a\n"
"surface then depends only on which way it faces the viewer, so\n"
"form reads identically wherever the scene light happens to be.\n"
"The classic inspection shading -- pair it with Cavity for edge\n"
"definition. Overrides physically based shading while on.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getMatcap() {
    return instance()->Matcap;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultMatcap() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setMatcap(const bool &v) {
    instance()->handle->SetBool("Matcap",v);
    instance()->Matcap = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeMatcap() {
    instance()->handle->RemoveBool("Matcap");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docMatcapPreset() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Which matcap to shade with. The presets are computed in the\n"
"shader rather than sampled from images, so they cost no assets\n"
"and stay sharp at any resolution. Studio = soft key light with a\n"
"rim; Clay = matte, no highlight, the most neutral read of form;\n"
"Metal = banded sweep with a hard edge, exaggerates curvature;\n"
"Pearl = warm/cool dual tone, shows shallow undulation.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getMatcapPreset() {
    return instance()->MatcapPreset;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultMatcapPreset() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setMatcapPreset(const long &v) {
    instance()->handle->SetInt("MatcapPreset",v);
    instance()->MatcapPreset = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeMatcapPreset() {
    instance()->handle->RemoveInt("MatcapPreset");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docMatcapTint() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How much each object's own color tints the matcap, 0 to 1.\n"
"One multiplies the matcap by the object color, so the matcap\n"
"supplies the shading and the assembly keeps its color coding.\n"
"Zero shades the whole scene as one uniform material instead,\n"
"which drops the color coding but makes shape directly\n"
"comparable across parts.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getMatcapTint() {
    return instance()->MatcapTint;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultMatcapTint() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setMatcapTint(const double &v) {
    instance()->handle->SetFloat("MatcapTint",v);
    instance()->MatcapTint = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeMatcapTint() {
    instance()->handle->RemoveFloat("MatcapTint");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docPBR() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable physically based shading with image based lighting of\n"
"the experimental render engine (render cache mode 3 with a\n"
"selected renderer type). Replaces the Classic headlight shading\n"
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
const char *RenderParams::docPBRFromSpecular() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Read an ordinary Phong appearance's specular COLOUR as\n"
"physically based material data, where nothing states a\n"
"metalness of its own. The metallic/roughness model has no\n"
"specular slot -- its reflectance follows from the base colour\n"
"and the metalness -- so a classic Gold, whose gold-ness lives\n"
"entirely in that colour, otherwise shades as yellow-brown\n"
"plastic, and the presets built from a black diffuse and a\n"
"bright specular (Steel, Satin, Metalized) shade as nearly\n"
"black. Anything authored stands: a stated metalness, a PBR\n"
"appearance, a metallic-roughness map.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getPBRFromSpecular() {
    return instance()->PBRFromSpecular;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultPBRFromSpecular() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBRFromSpecular(const bool &v) {
    instance()->handle->SetBool("PBRFromSpecular",v);
    instance()->PBRFromSpecular = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBRFromSpecular() {
    instance()->handle->RemoveBool("PBRFromSpecular");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docShininessMapping() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How a classic Phong appearance's SHININESS becomes a\n"
"roughness, where the material states no roughness of its own.\n"
"\n"
"Either way the conversion itself is the standard match of the\n"
"GGX lobe width to a Phong exponent n, roughness =\n"
"(2 / (n + 2)) ^ 1/4. What differs is what shininess MEANS.\n"
"\n"
"'GL exponent' reads it the way fixed-function GL did, as the\n"
"exponent scaled onto 0..128. That is faithful, but 128 is the\n"
"sharpest exponent GL could state, and it converts to a\n"
"roughness of 0.35 -- so on this reading a fully shiny Phong\n"
"material is satin, and the lower half of the roughness range\n"
"cannot be reached from shininess at all.\n"
"\n"
"'Full range' reads shininess as what the Appearance dialog\n"
"presents, a 0 to 100% appearance control, and maps it onto the\n"
"whole exponent range instead: n = 128 * s / (1 - s). Matte at\n"
"zero and a mirror at one, and over the low shininess values\n"
"real materials use it agrees with the GL reading to within a\n"
"few percent (FreeCAD's default 0.2 gives 0.49 rather than\n"
"0.52, the Gold preset 0.66 rather than 0.67).\n"
"\n"
"Neither reading touches anything authored: a stated roughness,\n"
"a PBR appearance, a metallic-roughness map and the per-object\n"
"Render_Roughness override all stand.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getShininessMapping() {
    return instance()->ShininessMapping;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultShininessMapping() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setShininessMapping(const long &v) {
    instance()->handle->SetInt("ShininessMapping",v);
    instance()->ShininessMapping = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeShininessMapping() {
    instance()->handle->RemoveInt("ShininessMapping");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docPBREnvPreset() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Which built-in environment lights the scene, where no\n"
"environment image is set. They are computed rather than\n"
"sampled from a file, so they cost no assets and work on every\n"
"tier including the browser.\n"
"\n"
"What separates them is contrast and structure, not brightness:\n"
"all five integrate to the same mean radiance, so the exposure\n"
"that suits one suits the others. That matters because a\n"
"surround with no bright sources and no edges cannot put a\n"
"highlight on anything that reads as a light, and a smooth\n"
"surface reflecting it shows the same flat grey at every\n"
"roughness -- which is what made physically based shading look\n"
"like painted plastic.\n"
"\n"
"Interior = a room with one window and a ceiling\n"
"panel, walls close enough to bounce. One hard key against a\n"
"dark surround, which is what gives the crispest highlight and\n"
"the strongest read of form. Studio = four soft boxes on a dark\n"
"surround, the product-shot rig, gentler and more even than\n"
"Interior. Gradient (the default) = the smooth three-band dome\n"
"this engine used before the others existed; the flattest and\n"
"the most even, which is why it is where a view starts -- it\n"
"stays out of the way of the model being worked on, and it is\n"
"the one to pick to have an older document's look back.\n"
"Overcast = a bright sky weighted to the zenith over dark\n"
"ground, soft and neutral. Sunset = a low warm sun with a deep\n"
"sky, the strongest colour separation, and the only one that\n"
"tints the whole frame. Light tent = a box of white panels,\n"
"bright BELOW the horizon as well as above it and seamed all\n"
"the way round; the one to pick when the SIDES of a subject\n"
"matter, since every other environment here puts a floor under\n"
"it and a standing wall reflects the floor.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getPBREnvPreset() {
    return instance()->PBREnvPreset;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultPBREnvPreset() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBREnvPreset(const long &v) {
    instance()->handle->SetInt("PBREnvPreset",v);
    instance()->PBREnvPreset = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBREnvPreset() {
    instance()->handle->RemoveInt("PBREnvPreset");
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
"\n"
"A Radiance picture (.hdr, .pic) is read as real radiance and\n"
"is the format worth using: a sky is thousands of times\n"
"brighter than the wall beneath it, and an ordinary 8-bit image\n"
"cannot hold that ratio, which is what makes one light a model\n"
"like a picture rather than like a place. An HDR environment\n"
"needs the output colour transform on, since it is the exposure\n"
"that decides how its range lands on the screen.\n"
"\n"
"What to load, in short:\n"
"\n"
" - A Radiance .hdr or .pic. OpenEXR is NOT read: anything\n"
"   that is not Radiance goes through Qt, which has no EXR\n"
"   plugin, so an .exr loads nothing and the procedural\n"
"   environment stays on.\n"
" - 2:1 proportions, so it is taken as a lat-long panorama\n"
"   and not as a mirror ball. Up is +Z, and the middle of\n"
"   the image faces +X.\n"
" - 1K or 2K is plenty. The picture is held as 32-bit float\n"
"   RGB (2K is about 25 MB, 8K about 400 MB) and is baked\n"
"   into a 128 pixel per face cubemap, so a larger one\n"
"   costs memory without showing more.\n"
" - Free CC0 panoramas: polyhaven.com/hdris.\n"
"\n"
"How sharp it is DRAWN behind the model is a separate\n"
"question, and the answer is Render_PBREnvBlur: the background\n"
"pass draws that cubemap through a lens aperture, the way a\n"
"real backdrop is out of focus, and at zero the aperture is\n"
"shut and it is drawn as baked. The lighting and the\n"
"reflections read the sharp environment whatever the blur\n"
"says.\n"
"\n"
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
"image path while set.\n"
"\n"
"On by default: a document whose lighting depends on a file\n"
"somewhere on one machine opens lit differently everywhere\n"
"else, and the path is the part of the setting least likely\n"
"to survive the trip.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getPBREnvEmbed() {
    return instance()->PBREnvEmbed;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultPBREnvEmbed() {
    const static bool def = true;
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
"background while physically based shading is active, so\n"
"reflective surfaces visibly mirror their surroundings.\n"
"\n"
"On by default, because a reflective object standing in front of\n"
"a flat gradient reads as fake for a reason that is not the\n"
"object's fault: the reflection has no visible source, so there\n"
"is nothing in the frame for the eye to reconcile it against.\n"
"Affects nothing outside physically based shading -- the\n"
"Classic and Matcap models keep the background gradient.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getPBREnvBackground() {
    return instance()->PBREnvBackground;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultPBREnvBackground() {
    const static bool def = true;
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
const char *RenderParams::docPBREnvBlur() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How far out of focus the environment background is, 0 to 1.\n"
"Zero is sharp -- the resolution it was baked at; one opens the\n"
"aperture to 45 degrees, and in between it doubles every eighth\n"
"of the range. Only the BACKGROUND is affected -- the lighting\n"
"and the reflections read the whole environment whatever this\n"
"says.\n"
"\n"
"It is a defocus, not a smudge: the environment is convolved\n"
"with the disc of directions an aperture subtends, in linear\n"
"radiance, so a small bright source spreads into an even bokeh\n"
"disc that keeps its energy rather than being averaged away.\n"
"\n"
"A backdrop wants some of this. A real one is out of focus, and\n"
"softening also lets a small bright source bleed into a wide\n"
"gentle falloff instead of sitting in the frame as a hard\n"
"rectangle. Too much of it and there is nothing left for a\n"
"reflection to be reconciled against, which is the whole reason\n"
"the background is drawn at all. Blender's viewport shading\n"
"carries the same control for the same reasons, and defaults it\n"
"higher than this does.\n"
"\n"
"Both shading models honour it, and at zero the two show the\n"
"same backdrop: they bake the environment at the same angular\n"
"resolution. The external path tracer gets there differently,\n"
"since the world it samples IS the light and softening it\n"
"would relight the scene -- so a second bake of the same\n"
"environment through the same aperture is mixed in on CAMERA\n"
"rays alone, and the lighting, reflections and refractions keep\n"
"the sharp world. One consequence of that rule: a camera ray\n"
"stays a camera ray through a transparent surface, so a\n"
"see-through pass-through shows the soft backdrop as well.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getPBREnvBlur() {
    return instance()->PBREnvBlur;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultPBREnvBlur() {
    const static double def = 0.25;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setPBREnvBlur(const double &v) {
    instance()->handle->SetFloat("PBREnvBlur",v);
    instance()->PBREnvBlur = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removePBREnvBlur() {
    instance()->handle->RemoveFloat("PBREnvBlur");
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
"render engine: raymarch the shadow map of the Shadow display style\n"
"through a homogeneous scattering medium. Only effective while\n"
"the Shadow display style provides a scene light.");
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
"lighting and the Shadow display style are active.");
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
"sun glint from the Shadow display style light.");
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
"the sun glint killed there. Requires the Shadow display style\n"
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
const char *RenderParams::docLight() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Let the render engine supply its own directional or spot scene\n"
"light, described by the Light* settings below, instead of taking\n"
"one out of the Coin traversal.\n"
"\n"
"Everything the engine keys off a light -- shadows, volumetric\n"
"shafts, the sun disc, ground reflection -- today has exactly one\n"
"source: the Shadow display style, which is what puts an\n"
"SoShadowDirectionalLight or SoSpotLight in the scene graph at all\n"
"(the viewer headlight is a plain SoDirectionalLight, which the\n"
"engine rejects by type). That makes a draw style the owner of the\n"
"lighting, and it is why the style cannot simply be retired\n"
"(docs/CoinRetirement.md 3.4).\n"
"\n"
"Off by default, and while off nothing changes. A light found in\n"
"the traversal still wins when one is there, so the Shadow style\n"
"keeps behaving exactly as before; these settings supply a light\n"
"when it does not.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getLight() {
    return instance()->Light;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultLight() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLight(const bool &v) {
    instance()->handle->SetBool("Light",v);
    instance()->Light = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLight() {
    instance()->handle->RemoveBool("Light");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness of the renderer's own scene light.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLightIntensity() {
    return instance()->LightIntensity;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLightIntensity() {
    const static double def = 0.8;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightIntensity(const double &v) {
    instance()->handle->SetFloat("LightIntensity",v);
    instance()->LightIntensity = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightIntensity() {
    instance()->handle->RemoveFloat("LightIntensity");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightDirectionX() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLightDirectionX() {
    return instance()->LightDirectionX;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLightDirectionX() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightDirectionX(const double &v) {
    instance()->handle->SetFloat("LightDirectionX",v);
    instance()->LightDirectionX = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightDirectionX() {
    instance()->handle->RemoveFloat("LightDirectionX");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightDirectionY() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLightDirectionY() {
    return instance()->LightDirectionY;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLightDirectionY() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightDirectionY(const double &v) {
    instance()->handle->SetFloat("LightDirectionY",v);
    instance()->LightDirectionY = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightDirectionY() {
    instance()->handle->RemoveFloat("LightDirectionY");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightDirectionZ() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLightDirectionZ() {
    return instance()->LightDirectionZ;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLightDirectionZ() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightDirectionZ(const double &v) {
    instance()->handle->SetFloat("LightDirectionZ",v);
    instance()->LightDirectionZ = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightDirectionZ() {
    instance()->handle->RemoveFloat("LightDirectionZ");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightColor() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Colour of the renderer's own scene light.");
}

// Auto generated code (Tools/params_utils.py:380)
const unsigned long & RenderParams::getLightColor() {
    return instance()->LightColor;
}

// Auto generated code (Tools/params_utils.py:388)
const unsigned long & RenderParams::defaultLightColor() {
    const static unsigned long def = 0xF0FDFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("LightColor",v);
    instance()->LightColor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightColor() {
    instance()->handle->RemoveUnsigned("LightColor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightSpot() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Make the renderer's own light a spot rather than a directional\n"
"one. A spot has a position and a cone; a directional light has\n"
"only a direction.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getLightSpot() {
    return instance()->LightSpot;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultLightSpot() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightSpot(const bool &v) {
    instance()->handle->SetBool("LightSpot",v);
    instance()->LightSpot = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightSpot() {
    instance()->handle->RemoveBool("LightSpot");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightPositionX() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLightPositionX() {
    return instance()->LightPositionX;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLightPositionX() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightPositionX(const double &v) {
    instance()->handle->SetFloat("LightPositionX",v);
    instance()->LightPositionX = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightPositionX() {
    instance()->handle->RemoveFloat("LightPositionX");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightPositionY() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLightPositionY() {
    return instance()->LightPositionY;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLightPositionY() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightPositionY(const double &v) {
    instance()->handle->SetFloat("LightPositionY",v);
    instance()->LightPositionY = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightPositionY() {
    instance()->handle->RemoveFloat("LightPositionY");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightPositionZ() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLightPositionZ() {
    return instance()->LightPositionZ;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLightPositionZ() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightPositionZ(const double &v) {
    instance()->handle->SetFloat("LightPositionZ",v);
    instance()->LightPositionZ = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightPositionZ() {
    instance()->handle->RemoveFloat("LightPositionZ");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightCutOffAngle() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Half angle of the spot cone, in degrees.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLightCutOffAngle() {
    return instance()->LightCutOffAngle;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLightCutOffAngle() {
    const static double def = 45.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightCutOffAngle(const double &v) {
    instance()->handle->SetFloat("LightCutOffAngle",v);
    instance()->LightCutOffAngle = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightCutOffAngle() {
    instance()->handle->RemoveFloat("LightCutOffAngle");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docLightDropOffRate() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How sharply a spot falls off from the cone axis. Zero is even\n"
"across the cone.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getLightDropOffRate() {
    return instance()->LightDropOffRate;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultLightDropOffRate() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setLightDropOffRate(const double &v) {
    instance()->handle->SetFloat("LightDropOffRate",v);
    instance()->LightDropOffRate = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeLightDropOffRate() {
    instance()->handle->RemoveFloat("LightDropOffRate");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docSunDisc() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Draw a visible sun -- a bright disc with a limb glow -- in\n"
"the sky along the Shadow display style's directional scene light,\n"
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
"Mirror the model in the ground plane of the experimental\n"
"render engine: the opaque scene is re-rendered with a reflected\n"
"camera and blended onto the ground. Brings the ground plane out\n"
"on its own -- neither the Shadow display style nor its ground\n"
"switch is needed -- and the ground keeps its own appearance\n"
"settings (color, size, texture) from the shadow group.");
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
const char *RenderParams::docCyclesDevice() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Compute device type the External shading model path traces\n"
"on, as Gui.cyclesDevices() names them: 'CPU' always works, and\n"
"'CUDA', 'OPTIX' or 'HIP' when this machine has the GPU and the\n"
"driver for it. Seeds the per-view Cycles_Device property, which\n"
"offers only the devices the machine actually has -- a document\n"
"saved elsewhere falls back to the first local device when its\n"
"choice does not exist here.");
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & RenderParams::getCyclesDevice() {
    return instance()->CyclesDevice;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & RenderParams::defaultCyclesDevice() {
    const static std::string def = "CPU";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCyclesDevice(const std::string &v) {
    instance()->handle->SetASCII("CyclesDevice",v);
    instance()->CyclesDevice = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCyclesDevice() {
    instance()->handle->RemoveASCII("CyclesDevice");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCyclesSamples() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Samples per pixel the External shading model refines to\n"
"before it rests. More is cleaner and slower to settle; the view\n"
"stays interactive either way, restarting from one sample on\n"
"every camera move.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getCyclesSamples() {
    return instance()->CyclesSamples;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultCyclesSamples() {
    const static long def = 256;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCyclesSamples(const long &v) {
    instance()->handle->SetInt("CyclesSamples",v);
    instance()->CyclesSamples = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCyclesSamples() {
    instance()->handle->RemoveInt("CyclesSamples");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCyclesTimeLimit() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Seconds the External shading model may refine after each\n"
"change before it rests, whatever the sample budget still says.\n"
"0 means no limit: the sample count alone decides.");
}

// Auto generated code (Tools/params_utils.py:380)
const double & RenderParams::getCyclesTimeLimit() {
    return instance()->CyclesTimeLimit;
}

// Auto generated code (Tools/params_utils.py:388)
const double & RenderParams::defaultCyclesTimeLimit() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCyclesTimeLimit(const double &v) {
    instance()->handle->SetFloat("CyclesTimeLimit",v);
    instance()->CyclesTimeLimit = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCyclesTimeLimit() {
    instance()->handle->RemoveFloat("CyclesTimeLimit");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCyclesDenoise() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Run OpenImageDenoise over the refining External shading\n"
"frame, trading the raw noise of the early samples for a smooth\n"
"image that sharpens as samples arrive.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getCyclesDenoise() {
    return instance()->CyclesDenoise;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultCyclesDenoise() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCyclesDenoise(const bool &v) {
    instance()->handle->SetBool("CyclesDenoise",v);
    instance()->CyclesDenoise = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCyclesDenoise() {
    instance()->handle->RemoveBool("CyclesDenoise");
}

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docCyclesPixelSize() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Render the External shading model at 1/n resolution and\n"
"scale up -- Blender's preview pixel size. 2 or 4 keeps a large\n"
"view fluid on a weak device at the cost of a blockier preview.");
}

// Auto generated code (Tools/params_utils.py:380)
const long & RenderParams::getCyclesPixelSize() {
    return instance()->CyclesPixelSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & RenderParams::defaultCyclesPixelSize() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setCyclesPixelSize(const long &v) {
    instance()->handle->SetInt("CyclesPixelSize",v);
    instance()->CyclesPixelSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeCyclesPixelSize() {
    instance()->handle->RemoveInt("CyclesPixelSize");
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

// Auto generated code (Tools/params_utils.py:372)
const char *RenderParams::docDebugCullBounds() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Measure whether a tighter occludee volume would cull more\n"
"(docs/FarFieldProxies.md §12.19). After per-instance testing, 90%\n"
"of the draws a frame still submits reach no pixel while each was\n"
"tested and answered visible -- so the geometry is hidden and the\n"
"box around it is not. This re-asks every still-drawn row three\n"
"ways against the same occluder buffer: with the world box that\n"
"ships, with the mesh's own box through the model matrix (an\n"
"oriented box, where the shipping one is the axis-aligned box\n"
"around it), and with every triangle asked separately -- which is\n"
"far too slow to ship and is here as the ceiling, since nothing\n"
"asked about the occludee can beat asking about its geometry. The\n"
"verdicts are counted against the cull audit's id image, never\n"
"acted on, so an arm that would have deleted something visible\n"
"reports itself instead of being believed.\n"
"Needs the cull audit on (it supplies the image) and the software\n"
"occluder pass, which owns the buffer being asked. Runs on the\n"
"audit's frame only, and costs far more than a frame: it is a\n"
"measurement, not a mode to leave on.");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & RenderParams::getDebugCullBounds() {
    return instance()->DebugCullBounds;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & RenderParams::defaultDebugCullBounds() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void RenderParams::setDebugCullBounds(const bool &v) {
    instance()->handle->SetBool("DebugCullBounds",v);
    instance()->DebugCullBounds = v;
}

// Auto generated code (Tools/params_utils.py:406)
void RenderParams::removeDebugCullBounds() {
    instance()->handle->RemoveBool("DebugCullBounds");
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
    if (boost::equals(sReason, "BackgroundReleaseDelay")) {
        // Not a per-frame feed like the rest: it times a view that has
        // stopped drawing, so the views already in the background are
        // waiting on the old value and have to be re-armed here.
        foreach3DViewer([](Gui::View3DInventorViewer *viewer) {
            viewer->armBackgroundRelease();
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

void RenderParams::selectRenderPath()
{
    // Render cache 3 is what feeds the render engine, so it is the path
    // whether or not a backend comes up: with one, the backend draws;
    // without one, the cache's own GL renderer does, and a failure at
    // any stage below falls back to that by itself (a backend that
    // cannot be created, a shader pack that will not load, and a frame
    // that returns false all leave canSkipInternal() false).
    if (ViewParams::getRenderCache() != 3)
        ViewParams::setRenderCache(3);

    const std::string type = preferredType();
    if (getType() != type)
        setType(type);
}

std::string RenderParams::preferredType()
{
    // The backend: the engine's own where this build has it, and
    // whatever else registered if not. Resolved against what is
    // actually registered rather than named by a literal, so a build
    // without the engine says "Default" instead of asking for a type
    // nobody can create, and a stored name from another build cannot
    // survive into this one.
    std::string type;
    for (const auto &t : Render::RendererFactory::types()) {
        if (boost::starts_with(t, "bgfx")) {
            return t;
        }
        if (type.empty())
            type = t;
    }
    if (type.empty())
        type = "Default";
    return type;
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
