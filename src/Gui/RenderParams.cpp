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
#include <App/ParamRegistry.h>
#include "RenderParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class RenderParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(RenderParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::string Type;
    long OutputTransform;
    double Exposure;
    long MaxViewIds;
    long ReadbackFrameMode;
    long BackgroundReleaseDelay;
    long CoarseTessellation;
    long CoarseDeferFaces;
    bool CoarseDeferAtLeisure;
    bool PreMeshOnLoad;
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
    double PerViewShownEvictWatermark;
    double LevelScaleBoxError;
    bool SimplifyExhausted;
    bool SimplifyMergeParts;
    double SimplifyMinReduction;
    bool ShapeVertices;
    bool PressureDropEdges;
    long ElementGateStagger;
    long TinyElementCutoff;
    bool LoadDropElements;
    long ElementTakeInSets;
    long ElementTakeInKB;
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
    long MatcapStripes;
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
    long CyclesMaxStreams;
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

    // Auto generated code (Tools/params_utils.py:254)
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
        ReadbackFrameMode = this->handle->GetInt("ReadbackFrameMode", 1);
        funcs["ReadbackFrameMode"] = &RenderParamsP::updateReadbackFrameMode;
        BackgroundReleaseDelay = this->handle->GetInt("BackgroundReleaseDelay", 1000);
        funcs["BackgroundReleaseDelay"] = &RenderParamsP::updateBackgroundReleaseDelay;
        CoarseTessellation = this->handle->GetInt("CoarseTessellation", 2);
        funcs["CoarseTessellation"] = &RenderParamsP::updateCoarseTessellation;
        CoarseDeferFaces = this->handle->GetInt("CoarseDeferFaces", 1000);
        funcs["CoarseDeferFaces"] = &RenderParamsP::updateCoarseDeferFaces;
        CoarseDeferAtLeisure = this->handle->GetBool("CoarseDeferAtLeisure", true);
        funcs["CoarseDeferAtLeisure"] = &RenderParamsP::updateCoarseDeferAtLeisure;
        PreMeshOnLoad = this->handle->GetBool("PreMeshOnLoad", true);
        funcs["PreMeshOnLoad"] = &RenderParamsP::updatePreMeshOnLoad;
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
        PerViewShownEvictWatermark = this->handle->GetFloat("PerViewShownEvictWatermark", 0.9);
        funcs["PerViewShownEvictWatermark"] = &RenderParamsP::updatePerViewShownEvictWatermark;
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
        ElementTakeInSets = this->handle->GetInt("ElementTakeInSets", 1000);
        funcs["ElementTakeInSets"] = &RenderParamsP::updateElementTakeInSets;
        ElementTakeInKB = this->handle->GetInt("ElementTakeInKB", 0);
        funcs["ElementTakeInKB"] = &RenderParamsP::updateElementTakeInKB;
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
        MatcapStripes = this->handle->GetInt("MatcapStripes", 6);
        funcs["MatcapStripes"] = &RenderParamsP::updateMatcapStripes;
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
        CyclesMaxStreams = this->handle->GetInt("CyclesMaxStreams", 4);
        funcs["CyclesMaxStreams"] = &RenderParamsP::updateCyclesMaxStreams;
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

    // Auto generated code (Tools/params_utils.py:284)
    ~RenderParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        RenderParams::onRenderParamChanged(sReason);
    }


    // Auto generated code (Tools/params_utils.py:314)
    static void updateType(RenderParamsP *self) {
        self->Type = self->handle->GetASCII("Type", "Default");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOutputTransform(RenderParamsP *self) {
        self->OutputTransform = self->handle->GetInt("OutputTransform", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExposure(RenderParamsP *self) {
        self->Exposure = self->handle->GetFloat("Exposure", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxViewIds(RenderParamsP *self) {
        self->MaxViewIds = self->handle->GetInt("MaxViewIds", 1024);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateReadbackFrameMode(RenderParamsP *self) {
        self->ReadbackFrameMode = self->handle->GetInt("ReadbackFrameMode", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBackgroundReleaseDelay(RenderParamsP *self) {
        self->BackgroundReleaseDelay = self->handle->GetInt("BackgroundReleaseDelay", 1000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoarseTessellation(RenderParamsP *self) {
        self->CoarseTessellation = self->handle->GetInt("CoarseTessellation", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoarseDeferFaces(RenderParamsP *self) {
        self->CoarseDeferFaces = self->handle->GetInt("CoarseDeferFaces", 1000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoarseDeferAtLeisure(RenderParamsP *self) {
        self->CoarseDeferAtLeisure = self->handle->GetBool("CoarseDeferAtLeisure", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreMeshOnLoad(RenderParamsP *self) {
        self->PreMeshOnLoad = self->handle->GetBool("PreMeshOnLoad", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMeshSkipRedundant(RenderParamsP *self) {
        self->MeshSkipRedundant = self->handle->GetBool("MeshSkipRedundant", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMeshSkipFinerResident(RenderParamsP *self) {
        self->MeshSkipFinerResident = self->handle->GetBool("MeshSkipFinerResident", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMeshSkipInvariant(RenderParamsP *self) {
        self->MeshSkipInvariant = self->handle->GetBool("MeshSkipInvariant", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateProgressiveLoad(RenderParamsP *self) {
        self->ProgressiveLoad = self->handle->GetBool("ProgressiveLoad", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateProgressiveLoadBudgetMS(RenderParamsP *self) {
        self->ProgressiveLoadBudgetMS = self->handle->GetInt("ProgressiveLoadBudgetMS", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelThreads(RenderParamsP *self) {
        self->LevelThreads = self->handle->GetInt("LevelThreads", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelMemoryFloorMB(RenderParamsP *self) {
        self->LevelMemoryFloorMB = self->handle->GetInt("LevelMemoryFloorMB", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelDebug(RenderParamsP *self) {
        self->LevelDebug = self->handle->GetBool("LevelDebug", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelCeilingSimulateMB(RenderParamsP *self) {
        self->LevelCeilingSimulateMB = self->handle->GetInt("LevelCeilingSimulateMB", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGpuMemoryBudgetMB(RenderParamsP *self) {
        self->GpuMemoryBudgetMB = self->handle->GetInt("GpuMemoryBudgetMB", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelTolerance(RenderParamsP *self) {
        self->LevelTolerance = self->handle->GetFloat("LevelTolerance", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelPressureRelease(RenderParamsP *self) {
        self->LevelPressureRelease = self->handle->GetFloat("LevelPressureRelease", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateClimbHardLimit(RenderParamsP *self) {
        self->ClimbHardLimit = self->handle->GetBool("ClimbHardLimit", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateClimbAdmitBatch(RenderParamsP *self) {
        self->ClimbAdmitBatch = self->handle->GetInt("ClimbAdmitBatch", 64);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelLandBudgetMS(RenderParamsP *self) {
        self->LevelLandBudgetMS = self->handle->GetInt("LevelLandBudgetMS", 50);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMeshSkipLanded(RenderParamsP *self) {
        self->MeshSkipLanded = self->handle->GetBool("MeshSkipLanded", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVisualFillOnPool(RenderParamsP *self) {
        self->VisualFillOnPool = self->handle->GetBool("VisualFillOnPool", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVisualFillMinFaces(RenderParamsP *self) {
        self->VisualFillMinFaces = self->handle->GetInt("VisualFillMinFaces", 2000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWorkerVertexCache(RenderParamsP *self) {
        self->WorkerVertexCache = self->handle->GetInt("WorkerVertexCache", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCaptureBudgetMS(RenderParamsP *self) {
        self->CaptureBudgetMS = self->handle->GetInt("CaptureBudgetMS", 50);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelSlowBuildMS(RenderParamsP *self) {
        self->LevelSlowBuildMS = self->handle->GetInt("LevelSlowBuildMS", 200);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDescentOrderBatch(RenderParamsP *self) {
        self->DescentOrderBatch = self->handle->GetInt("DescentOrderBatch", 64);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDowngradeLedger(RenderParamsP *self) {
        self->DowngradeLedger = self->handle->GetBool("DowngradeLedger", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelCount(RenderParamsP *self) {
        self->LevelCount = self->handle->GetInt("LevelCount", 8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelScale(RenderParamsP *self) {
        self->LevelScale = self->handle->GetFloat("LevelScale", 2.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelBudgetDeadband(RenderParamsP *self) {
        self->LevelBudgetDeadband = self->handle->GetFloat("LevelBudgetDeadband", 0.03);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePerViewShownEvictWatermark(RenderParamsP *self) {
        self->PerViewShownEvictWatermark = self->handle->GetFloat("PerViewShownEvictWatermark", 0.9);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLevelScaleBoxError(RenderParamsP *self) {
        self->LevelScaleBoxError = self->handle->GetFloat("LevelScaleBoxError", 0.25);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSimplifyExhausted(RenderParamsP *self) {
        self->SimplifyExhausted = self->handle->GetBool("SimplifyExhausted", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSimplifyMergeParts(RenderParamsP *self) {
        self->SimplifyMergeParts = self->handle->GetBool("SimplifyMergeParts", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSimplifyMinReduction(RenderParamsP *self) {
        self->SimplifyMinReduction = self->handle->GetFloat("SimplifyMinReduction", 20.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShapeVertices(RenderParamsP *self) {
        self->ShapeVertices = self->handle->GetBool("ShapeVertices", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePressureDropEdges(RenderParamsP *self) {
        self->PressureDropEdges = self->handle->GetBool("PressureDropEdges", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateElementGateStagger(RenderParamsP *self) {
        self->ElementGateStagger = self->handle->GetInt("ElementGateStagger", 15);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTinyElementCutoff(RenderParamsP *self) {
        self->TinyElementCutoff = self->handle->GetInt("TinyElementCutoff", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLoadDropElements(RenderParamsP *self) {
        self->LoadDropElements = self->handle->GetBool("LoadDropElements", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateElementTakeInSets(RenderParamsP *self) {
        self->ElementTakeInSets = self->handle->GetInt("ElementTakeInSets", 1000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateElementTakeInKB(RenderParamsP *self) {
        self->ElementTakeInKB = self->handle->GetInt("ElementTakeInKB", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEffectResolution(RenderParamsP *self) {
        self->EffectResolution = self->handle->GetFloat("EffectResolution", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTemporalAccum(RenderParamsP *self) {
        self->TemporalAccum = self->handle->GetBool("TemporalAccum", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTemporalAccumSamples(RenderParamsP *self) {
        self->TemporalAccumSamples = self->handle->GetInt("TemporalAccumSamples", 32);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusion(RenderParamsP *self) {
        self->Occlusion = self->handle->GetBool("Occlusion", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionVisibleTtl(RenderParamsP *self) {
        self->OcclusionVisibleTtl = self->handle->GetInt("OcclusionVisibleTtl", 6);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionBudget(RenderParamsP *self) {
        self->OcclusionBudget = self->handle->GetInt("OcclusionBudget", 128);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionMinSubtree(RenderParamsP *self) {
        self->OcclusionMinSubtree = self->handle->GetInt("OcclusionMinSubtree", 8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionMaxHidden(RenderParamsP *self) {
        self->OcclusionMaxHidden = self->handle->GetInt("OcclusionMaxHidden", 120);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionDepthPad(RenderParamsP *self) {
        self->OcclusionDepthPad = self->handle->GetInt("OcclusionDepthPad", 16);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionConfirm(RenderParamsP *self) {
        self->OcclusionConfirm = self->handle->GetInt("OcclusionConfirm", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionSoftware(RenderParamsP *self) {
        self->OcclusionSoftware = self->handle->GetBool("OcclusionSoftware", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionOccluderTris(RenderParamsP *self) {
        self->OcclusionOccluderTris = self->handle->GetInt("OcclusionOccluderTris", 250000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionMinOccluder(RenderParamsP *self) {
        self->OcclusionMinOccluder = self->handle->GetInt("OcclusionMinOccluder", 24);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionThreads(RenderParamsP *self) {
        self->OcclusionThreads = self->handle->GetInt("OcclusionThreads", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionSimd(RenderParamsP *self) {
        self->OcclusionSimd = self->handle->GetBool("OcclusionSimd", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionResolution(RenderParamsP *self) {
        self->OcclusionResolution = self->handle->GetInt("OcclusionResolution", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionPerInstance(RenderParamsP *self) {
        self->OcclusionPerInstance = self->handle->GetBool("OcclusionPerInstance", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionDemoteStreak(RenderParamsP *self) {
        self->OcclusionDemoteStreak = self->handle->GetInt("OcclusionDemoteStreak", 8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionCoarse(RenderParamsP *self) {
        self->OcclusionCoarse = self->handle->GetBool("OcclusionCoarse", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionCoarseLevel(RenderParamsP *self) {
        self->OcclusionCoarseLevel = self->handle->GetInt("OcclusionCoarseLevel", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionCoarseMinTris(RenderParamsP *self) {
        self->OcclusionCoarseMinTris = self->handle->GetInt("OcclusionCoarseMinTris", 512);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionCoarseBuilds(RenderParamsP *self) {
        self->OcclusionCoarseBuilds = self->handle->GetInt("OcclusionCoarseBuilds", 8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionCoarseBias(RenderParamsP *self) {
        self->OcclusionCoarseBias = self->handle->GetInt("OcclusionCoarseBias", 100);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionCoarseMemory(RenderParamsP *self) {
        self->OcclusionCoarseMemory = self->handle->GetInt("OcclusionCoarseMemory", 64);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOcclusionBenefitProbe(RenderParamsP *self) {
        self->OcclusionBenefitProbe = self->handle->GetBool("OcclusionBenefitProbe", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAO(RenderParamsP *self) {
        self->AO = self->handle->GetBool("AO", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShadow(RenderParamsP *self) {
        self->Shadow = self->handle->GetBool("Shadow", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAOMethod(RenderParamsP *self) {
        self->AOMethod = self->handle->GetInt("AOMethod", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAOSlices(RenderParamsP *self) {
        self->AOSlices = self->handle->GetInt("AOSlices", 9);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAOSteps(RenderParamsP *self) {
        self->AOSteps = self->handle->GetInt("AOSteps", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAORadius(RenderParamsP *self) {
        self->AORadius = self->handle->GetFloat("AORadius", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAOIntensity(RenderParamsP *self) {
        self->AOIntensity = self->handle->GetFloat("AOIntensity", 0.6);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAOResolution(RenderParamsP *self) {
        self->AOResolution = self->handle->GetFloat("AOResolution", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCavity(RenderParamsP *self) {
        self->Cavity = self->handle->GetBool("Cavity", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCavityRadius(RenderParamsP *self) {
        self->CavityRadius = self->handle->GetFloat("CavityRadius", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCavityValley(RenderParamsP *self) {
        self->CavityValley = self->handle->GetFloat("CavityValley", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCavityRidge(RenderParamsP *self) {
        self->CavityRidge = self->handle->GetFloat("CavityRidge", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMatcap(RenderParamsP *self) {
        self->Matcap = self->handle->GetBool("Matcap", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMatcapPreset(RenderParamsP *self) {
        self->MatcapPreset = self->handle->GetInt("MatcapPreset", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMatcapStripes(RenderParamsP *self) {
        self->MatcapStripes = self->handle->GetInt("MatcapStripes", 6);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMatcapTint(RenderParamsP *self) {
        self->MatcapTint = self->handle->GetFloat("MatcapTint", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBR(RenderParamsP *self) {
        self->PBR = self->handle->GetBool("PBR", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBRMetallic(RenderParamsP *self) {
        self->PBRMetallic = self->handle->GetFloat("PBRMetallic", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBRRoughness(RenderParamsP *self) {
        self->PBRRoughness = self->handle->GetFloat("PBRRoughness", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBRFromSpecular(RenderParamsP *self) {
        self->PBRFromSpecular = self->handle->GetBool("PBRFromSpecular", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShininessMapping(RenderParamsP *self) {
        self->ShininessMapping = self->handle->GetInt("ShininessMapping", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBREnvPreset(RenderParamsP *self) {
        self->PBREnvPreset = self->handle->GetInt("PBREnvPreset", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBREnvIntensity(RenderParamsP *self) {
        self->PBREnvIntensity = self->handle->GetFloat("PBREnvIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBREnvImage(RenderParamsP *self) {
        self->PBREnvImage = self->handle->GetASCII("PBREnvImage", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBREnvEmbed(RenderParamsP *self) {
        self->PBREnvEmbed = self->handle->GetBool("PBREnvEmbed", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBREnvBackground(RenderParamsP *self) {
        self->PBREnvBackground = self->handle->GetBool("PBREnvBackground", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePBREnvBlur(RenderParamsP *self) {
        self->PBREnvBlur = self->handle->GetFloat("PBREnvBlur", 0.25);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBumpScale(RenderParamsP *self) {
        self->BumpScale = self->handle->GetFloat("BumpScale", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateParallax(RenderParamsP *self) {
        self->Parallax = self->handle->GetBool("Parallax", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVolumetric(RenderParamsP *self) {
        self->Volumetric = self->handle->GetBool("Volumetric", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVolumetricIntensity(RenderParamsP *self) {
        self->VolumetricIntensity = self->handle->GetFloat("VolumetricIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVolumetricDensity(RenderParamsP *self) {
        self->VolumetricDensity = self->handle->GetFloat("VolumetricDensity", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCaustics(RenderParamsP *self) {
        self->Caustics = self->handle->GetBool("Caustics", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCausticsIntensity(RenderParamsP *self) {
        self->CausticsIntensity = self->handle->GetFloat("CausticsIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCausticsScale(RenderParamsP *self) {
        self->CausticsScale = self->handle->GetFloat("CausticsScale", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCausticsSpeed(RenderParamsP *self) {
        self->CausticsSpeed = self->handle->GetFloat("CausticsSpeed", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterSurface(RenderParamsP *self) {
        self->WaterSurface = self->handle->GetBool("WaterSurface", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterWaveStrength(RenderParamsP *self) {
        self->WaterWaveStrength = self->handle->GetFloat("WaterWaveStrength", 0.3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterWaveScale(RenderParamsP *self) {
        self->WaterWaveScale = self->handle->GetFloat("WaterWaveScale", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterWaveSpeed(RenderParamsP *self) {
        self->WaterWaveSpeed = self->handle->GetFloat("WaterWaveSpeed", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterAbsorption(RenderParamsP *self) {
        self->WaterAbsorption = self->handle->GetFloat("WaterAbsorption", 0.2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterInscatter(RenderParamsP *self) {
        self->WaterInscatter = self->handle->GetFloat("WaterInscatter", 0.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterRefraction(RenderParamsP *self) {
        self->WaterRefraction = self->handle->GetBool("WaterRefraction", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterReflection(RenderParamsP *self) {
        self->WaterReflection = self->handle->GetBool("WaterReflection", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterPlanarReflection(RenderParamsP *self) {
        self->WaterPlanarReflection = self->handle->GetBool("WaterPlanarReflection", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterShadow(RenderParamsP *self) {
        self->WaterShadow = self->handle->GetBool("WaterShadow", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterRippleType(RenderParamsP *self) {
        self->WaterRippleType = self->handle->GetInt("WaterRippleType", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterRippleDensity(RenderParamsP *self) {
        self->WaterRippleDensity = self->handle->GetFloat("WaterRippleDensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterImpactStrength(RenderParamsP *self) {
        self->WaterImpactStrength = self->handle->GetFloat("WaterImpactStrength", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterImpactLife(RenderParamsP *self) {
        self->WaterImpactLife = self->handle->GetFloat("WaterImpactLife", 1.1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWaterShadowWobble(RenderParamsP *self) {
        self->WaterShadowWobble = self->handle->GetFloat("WaterShadowWobble", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBloom(RenderParamsP *self) {
        self->Bloom = self->handle->GetBool("Bloom", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBloomThreshold(RenderParamsP *self) {
        self->BloomThreshold = self->handle->GetFloat("BloomThreshold", 0.9);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBloomIntensity(RenderParamsP *self) {
        self->BloomIntensity = self->handle->GetFloat("BloomIntensity", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBloomRadius(RenderParamsP *self) {
        self->BloomRadius = self->handle->GetFloat("BloomRadius", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLight(RenderParamsP *self) {
        self->Light = self->handle->GetBool("Light", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightIntensity(RenderParamsP *self) {
        self->LightIntensity = self->handle->GetFloat("LightIntensity", 0.8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightDirectionX(RenderParamsP *self) {
        self->LightDirectionX = self->handle->GetFloat("LightDirectionX", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightDirectionY(RenderParamsP *self) {
        self->LightDirectionY = self->handle->GetFloat("LightDirectionY", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightDirectionZ(RenderParamsP *self) {
        self->LightDirectionZ = self->handle->GetFloat("LightDirectionZ", -1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightColor(RenderParamsP *self) {
        self->LightColor = self->handle->GetUnsigned("LightColor", 0xF0FDFFFF);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightSpot(RenderParamsP *self) {
        self->LightSpot = self->handle->GetBool("LightSpot", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightPositionX(RenderParamsP *self) {
        self->LightPositionX = self->handle->GetFloat("LightPositionX", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightPositionY(RenderParamsP *self) {
        self->LightPositionY = self->handle->GetFloat("LightPositionY", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightPositionZ(RenderParamsP *self) {
        self->LightPositionZ = self->handle->GetFloat("LightPositionZ", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightCutOffAngle(RenderParamsP *self) {
        self->LightCutOffAngle = self->handle->GetFloat("LightCutOffAngle", 45.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLightDropOffRate(RenderParamsP *self) {
        self->LightDropOffRate = self->handle->GetFloat("LightDropOffRate", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSunDisc(RenderParamsP *self) {
        self->SunDisc = self->handle->GetBool("SunDisc", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSunDiscSize(RenderParamsP *self) {
        self->SunDiscSize = self->handle->GetFloat("SunDiscSize", 1.5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGroundReflection(RenderParamsP *self) {
        self->GroundReflection = self->handle->GetBool("GroundReflection", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGroundReflectionIntensity(RenderParamsP *self) {
        self->GroundReflectionIntensity = self->handle->GetFloat("GroundReflectionIntensity", 0.4);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCyclesDevice(RenderParamsP *self) {
        self->CyclesDevice = self->handle->GetASCII("CyclesDevice", "CPU");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCyclesSamples(RenderParamsP *self) {
        self->CyclesSamples = self->handle->GetInt("CyclesSamples", 256);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCyclesTimeLimit(RenderParamsP *self) {
        self->CyclesTimeLimit = self->handle->GetFloat("CyclesTimeLimit", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCyclesDenoise(RenderParamsP *self) {
        self->CyclesDenoise = self->handle->GetBool("CyclesDenoise", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCyclesPixelSize(RenderParamsP *self) {
        self->CyclesPixelSize = self->handle->GetInt("CyclesPixelSize", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCyclesMaxStreams(RenderParamsP *self) {
        self->CyclesMaxStreams = self->handle->GetInt("CyclesMaxStreams", 4);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugViewMode(RenderParamsP *self) {
        self->DebugViewMode = self->handle->GetInt("DebugViewMode", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugFreezeFrame(RenderParamsP *self) {
        self->DebugFreezeFrame = self->handle->GetBool("DebugFreezeFrame", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugLabel(RenderParamsP *self) {
        self->DebugLabel = self->handle->GetBool("DebugLabel", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugTiming(RenderParamsP *self) {
        self->DebugTiming = self->handle->GetBool("DebugTiming", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugDelta(RenderParamsP *self) {
        self->DebugDelta = self->handle->GetBool("DebugDelta", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugCoverage(RenderParamsP *self) {
        self->DebugCoverage = self->handle->GetBool("DebugCoverage", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugProxyCut(RenderParamsP *self) {
        self->DebugProxyCut = self->handle->GetBool("DebugProxyCut", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugOcclusion(RenderParamsP *self) {
        self->DebugOcclusion = self->handle->GetBool("DebugOcclusion", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugProxyGen(RenderParamsP *self) {
        self->DebugProxyGen = self->handle->GetBool("DebugProxyGen", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugCullAudit(RenderParamsP *self) {
        self->DebugCullAudit = self->handle->GetBool("DebugCullAudit", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDebugCullBounds(RenderParamsP *self) {
        self->DebugCullBounds = self->handle->GetBool("DebugCullBounds", false);
    }
};

// Auto generated code (Tools/params_utils.py:336)
RenderParamsP *instance() {
    static RenderParamsP *inst = new RenderParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _RenderParamsRegistrar({
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Type", "Type", App::ParamInfo::String, "Default")
        .setTitle("Renderer type")
        .setDoc("What draws a 3D view. 'Default': the render engine, on this\n"
"platform's backend. 'Legacy': the old Coin rendering, without the\n"
"engine. A backend can also be named, as 'bgfx - Direct3D11'. With\n"
"the engine the render cache is always 3, whatever its own setting\n"
"says; under 'Legacy' that setting is what counts.")
        .setProxy("ComboBox")
        .setItems({{"Default (the render engine)", "", "Default"}, {"Legacy (Coin, without the render engine)", "", "Legacy"}}, true, true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OutputTransform", "OutputTransform", App::ParamInfo::Int, 1)
        .setTitle("Output colour transform")
        .setDoc("Colour management of the engine. 'sRGB' decodes authored colours to\n"
"linear for shading and encodes the finished frame for the display,\n"
"which is the correct pipeline. 'Off' is the older one, which shades\n"
"display numbers and renders falloff and shadow too dark; documents\n"
"written before this setting existed use it.")
        .setProxy("ComboBox")
        .setItems({{"Off", "", nullptr}, {"sRGB", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Exposure", "Exposure", App::ParamInfo::Float, 1.0)
        .setTitle("Exposure")
        .setDoc("Brightness multiplier applied to the finished image before it is\n"
"encoded for the screen. 1 leaves it alone. Bright areas roll off\n"
"smoothly instead of clipping. Only used while the output colour\n"
"transform is on."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "MaxViewIds", "MaxViewIds", App::ParamInfo::Int, 1024)
        .setTitle("Backend view id budget")
        .setDoc("How many view ids the render backend may hand out, which limits how\n"
"many 3D views can draw with it at once (about 13 ids per view; a view\n"
"that finds none left falls back to plain GL). 0 asks for the build's\n"
"maximum. Read when the backend starts: a change needs a restart."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ReadbackFrameMode", "ReadbackFrameMode", App::ParamInfo::Int, 1)
        .setTitle("Frame delivery through read-back")
        .setDoc("How a frame reaches the screen on Direct3D, Vulkan and Metal. 'Wait'\n"
"shows every frame as soon as it is drawn. 'Pipelined' shows it a frame\n"
"or two late and is faster. 'Pipelined while animating' waits except\n"
"while the view redraws by itself. Not used with OpenGL.")
        .setProxy("ComboBox")
        .setItems({{"Wait", "", nullptr}, {"Pipelined while animating", "", nullptr}, {"Pipelined", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "BackgroundReleaseDelay", "BackgroundReleaseDelay", App::ParamInfo::Int, 1000)
        .setTitle("Background view release delay")
        .setDoc("Milliseconds a 3D view may stay in the background before it gives its\n"
"render targets back to free GPU memory. Returning to the view costs\n"
"one frame to rebuild them, with the same picture. 0 never releases."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CoarseTessellation", "CoarseTessellation", App::ParamInfo::Int, 2)
        .setTitle("Coarse tessellation level")
        .setDoc("Ladder level a shape is first tessellated at: 0 is the coarsest, each\n"
"level halves the error, and the exact mesh is built on demand when a\n"
"camera needs it. -1 always tessellates exact up front. Used by a view\n"
"in render cache mode 3 and by a scene stream server; takes effect when\n"
"a shape is tessellated again."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CoarseDeferFaces", "CoarseDeferFaces", App::ParamInfo::Int, 1000)
        .setTitle("Coarse defer face threshold")
        .setDoc("During a progressive import on the bgfx renderer, a shape with\n"
"more faces than this gets a bounding-box stand-in immediately and\n"
"even its coarse tessellation is built on the refine worker pool,\n"
"swapped in when it arrives (docs/SceneStreaming.md #13) - the\n"
"import stall otherwise scales with the largest single part. -1\n"
"disables the stand-in so every shape tessellates inline."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CoarseDeferAtLeisure", "CoarseDeferAtLeisure", App::ParamInfo::Bool, true)
        .setTitle("Mesh a bounding-box stand-in out of view")
        .setDoc("A shape drawn as a bounding box (CoarseDeferFaces) gets its mesh in the\n"
"background also where the camera does not see it, so its picture is\n"
"there when the camera turns. Off, it stays a box until the camera turns\n"
"to it. Takes effect when a shape is tessellated again."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PreMeshOnLoad", "PreMeshOnLoad", App::ParamInfo::Bool, true)
        .setTitle("Pre-mesh a restored document in parallel")
        .setDoc("Tessellate the shapes of a document being opened on worker threads,\n"
"before they are built for display one by one on the GUI thread. Opens\n"
"a document of many small parts sooner, with the same result. Shapes\n"
"that share faces or edges with another, and instancing candidates, are\n"
"left to the normal path."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "MeshSkipRedundant", "MeshSkipRedundant", App::ParamInfo::Bool, true)
        .setTitle("Skip redundant tessellation")
        .setDoc("Check whether a shape is already tessellated the way a rebuild wants\n"
"it, and skip the tessellation call when it is. The check is strict: a\n"
"single face that would be re-tessellated and the call runs as before.\n"
"Off makes the call every time."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "MeshSkipFinerResident", "MeshSkipFinerResident", App::ParamInfo::Bool, false)
        .setTitle("Skip when the mesh is finer than asked")
        .setDoc("With redundant tessellation skipped, also count a mesh finer than the\n"
"one asked for as good enough, instead of re-tessellating to coarsen it.\n"
"Saves time on a descent, but can keep memory the level plan asked to\n"
"have back, which is why it is off by default."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "MeshSkipInvariant", "MeshSkipInvariant", App::ParamInfo::Bool, true)
        .setTitle("Skip deflection-invariant tessellation")
        .setDoc("Skip a tessellation call on a shape whose mesh cannot depend on the\n"
"deflection asked for: every face planar and every edge a straight\n"
"line. Such a shape gives the same mesh at any coarseness, so the call\n"
"would only rebuild what is there."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ProgressiveLoad", "ProgressiveLoad", App::ParamInfo::Bool, true)
        .setTitle("Progressive document load")
        .setDoc("Build the display of a document after it has opened instead of during\n"
"the load. The window comes up first and the parts appear in slices,\n"
"with the view painting in between."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ProgressiveLoadBudgetMS", "ProgressiveLoadBudgetMS", App::ParamInfo::Int, 100)
        .setTitle("Progressive load slice (ms)")
        .setDoc("With ProgressiveLoad: milliseconds one slice of building the display\n"
"may run before the window is updated. Larger finishes sooner, smaller\n"
"keeps the window more responsive."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelThreads", "LevelThreads", App::ParamInfo::Int, 0)
        .setTitle("Level build threads")
        .setDoc("How many mesh level builds (the scene server's on-demand\n"
"re-tessellations, docs/SceneStreaming.md #7) may run at once.\n"
"0 sizes the pool automatically - modest, because each BRepMesh\n"
"build already parallelizes internally over OCCT's shared thread\n"
"pool. The FC_LEVEL_THREADS environment variable overrides it.\n"
"Read when the server spawns its first level worker."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelMemoryFloorMB", "LevelMemoryFloorMB", App::ParamInfo::Int, 0)
        .setTitle("Level memory floor (MB)")
        .setDoc("Free system memory, in megabytes, below which no exact\n"
"re-tessellation is started; from then on exact meshes the camera does\n"
"not need are given up as well. 0 chooses automatically (512 MB, or a\n"
"sixteenth of physical memory if that is more)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelDebug", "LevelDebug", App::ParamInfo::Bool, false)
        .setTitle("Level plan debug")
        .setDoc("Diagnostic. Logs what each mesh level plan decides: the GPU budget and\n"
"the memory in use, how many objects are coarse or exact, and how many\n"
"refines and downgrades it ordered. The FC_LEVEL_DEBUG environment\n"
"variable turns it on too. Read at the first plan."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelCeilingSimulateMB", "LevelCeilingSimulateMB", App::ParamInfo::Int, 0)
        .setTitle("Simulate memory ceiling below (MB)")
        .setDoc("Testing aid. Pretend the system has less free memory than this many\n"
"megabytes, so the low-memory behaviour of the level plan can be tried\n"
"on a machine with memory to spare. 0 turns it off."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "GpuMemoryBudgetMB", "GpuMemoryBudgetMB", App::ParamInfo::Int, 0)
        .setTitle("GPU memory budget (MB)")
        .setDoc("GPU memory, in megabytes, the displayed geometry may use. Over it,\n"
"objects the camera would not miss are shown with their coarse mesh;\n"
"the exact one stays in main memory and returns at once on zooming in.\n"
"0 uses the limit the graphics API reports (OpenGL reports none, and\n"
"then no budget applies)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelTolerance", "LevelTolerance", App::ParamInfo::Float, 2.0)
        .setTitle("Level tolerance")
        .setDoc("Error in pixels on screen a coarse mesh may show before the exact one\n"
"is built. When the camera stops, only objects that exceed it are\n"
"refined. 0 or less refines everything at once; larger keeps more of\n"
"the scene coarse."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelPressureRelease", "LevelPressureRelease", App::ParamInfo::Float, 0.5)
        .setTitle("Level pressure release")
        .setDoc("After the scene has been coarsened to fit the GPU budget: the fraction\n"
"of the raised tolerance kept each time a plan fits again, so quality\n"
"returns in steps instead of all at once and over the budget again.\n"
"Smaller returns quality faster. 0 or less returns it in one step."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ClimbHardLimit", "ClimbHardLimit", App::ParamInfo::Bool, true)
        .setTitle("Hard GPU budget for climbs")
        .setDoc("Treat the GPU memory budget as a hard ceiling for refinement: at or\n"
"above it no object is refined and refinements under way are\n"
"cancelled; below it they are admitted in small batches\n"
"(ClimbAdmitBatch). Off refines without this check."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ClimbAdmitBatch", "ClimbAdmitBatch", App::ParamInfo::Int, 64)
        .setTitle("Climb admission batch")
        .setDoc("How many refines one plan may admit while the hard climb\n"
"limit is on and the uploaded total is under budget. Small\n"
"keeps the possible overshoot small and lets the next plan\n"
"re-check the allocator-exact total before admitting more;\n"
"large climbs faster. The set is not ordered by need within a\n"
"plan, but every plan re-evaluates the whole scene, so nothing\n"
"starves across plans."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelLandBudgetMS", "LevelLandBudgetMS", App::ParamInfo::Int, 50)
        .setTitle("Level landing budget (ms)")
        .setDoc("Milliseconds one turn of the event loop may spend installing finished\n"
"mesh level changes before it returns to painting and input. Smaller\n"
"keeps the window more responsive, larger finishes sooner."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "MeshSkipLanded", "MeshSkipLanded", App::ParamInfo::Bool, true)
        .setTitle("Skip mesh call on landing rebuilds")
        .setDoc("Skip the tessellation call in the rebuild that follows a mesh level\n"
"change, where the mesh just installed is the one to display and the\n"
"call could only confirm it."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "VisualFillOnPool", "VisualFillOnPool", App::ParamInfo::Bool, true)
        .setTitle("Fill landing rebuilds on the refine pool")
        .setDoc("Fill the display arrays of a large object on a worker thread instead\n"
"of the GUI thread, after a mesh level change. Keeps the window\n"
"responsive while big objects change level. Applies to objects with at\n"
"least VisualFillMinFaces faces."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "VisualFillMinFaces", "VisualFillMinFaces", App::ParamInfo::Int, 2000)
        .setTitle("Minimum faces for a pooled fill")
        .setDoc("How many faces a landing rebuild must have before its\n"
"array fill goes to the refine pool (Fill landing rebuilds on\n"
"the refine pool). The fill measures ~30us per face on the\n"
"reference model, so the default parks roughly the >60ms\n"
"items; the thousands of small landings in a budget drop stay\n"
"on the cheap inline path rather than paying a snapshot, a\n"
"queue hop and a second landing each."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WorkerVertexCache", "WorkerVertexCache", App::ParamInfo::Int, 1)
        .setTitle("Adopt worker-emitted vertex caches")
        .setDoc("Use the vertex arrays a worker thread already computed for a rebuilt\n"
"shape instead of capturing them again from the scene. 0 off, 1 on,\n"
"2 does both and logs any difference (slow, for checking). Shapes with\n"
"one colour only."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CaptureBudgetMS", "CaptureBudgetMS", App::ParamInfo::Int, 50)
        .setTitle("Vertex capture budget per publish (ms)")
        .setDoc("Milliseconds one scene update may spend capturing changed shapes\n"
"before the rest wait for the next frame. Keeps frames short while many\n"
"objects change at once; a waiting shape shows its previous state for\n"
"a frame or two. 0 captures everything in one frame."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelSlowBuildMS", "LevelSlowBuildMS", App::ParamInfo::Int, 200)
        .setTitle("Slow visual build report (ms)")
        .setDoc("Diagnostic, with LevelDebug: a display rebuild, or a single event,\n"
"that takes longer than this many milliseconds is logged with where the\n"
"time went. 0 turns it off."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DescentOrderBatch", "DescentOrderBatch", App::ParamInfo::Int, 64)
        .setTitle("Descent order batch")
        .setDoc("How many downgrades one plan may order at a time. The rest are found\n"
"again by the next plan, so nothing is lost, only paced. 0 removes the\n"
"limit."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DowngradeLedger", "DowngradeLedger", App::ParamInfo::Bool, true)
        .setTitle("Downgrade ledger")
        .setDoc("Count the GPU memory that downgrades already ordered will free as\n"
"credit against the next plan's shortfall. Without it a plan made while\n"
"those are still in flight orders them again, and far more than needed.\n"
"Off is the older behaviour, kept for comparison."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelCount", "LevelCount", App::ParamInfo::Int, 8)
        .setTitle("Ladder rung count")
        .setDoc("Number of levels of the mesh ladder. Level 0 is the coarsest and each\n"
"further level halves the error, so raising this adds finer levels."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelScale", "LevelScale", App::ParamInfo::Float, 2.0)
        .setTitle("Dynamic coarseness scale")
        .setDoc("Factor by which an object is tessellated coarser again when GPU\n"
"memory is still short at the coarsest ladder level. Applied object by\n"
"object, least visible error first, until the scene fits. 1 or less\n"
"turns this off."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelBudgetDeadband", "LevelBudgetDeadband", App::ParamInfo::Float, 0.03)
        .setTitle("GPU budget deadband")
        .setDoc("Band above the GPU memory budget, as a fraction of it, inside which no\n"
"downgrades are ordered. Keeps a scene that settles at the budget from\n"
"going back and forth across it. 0 removes the band."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PerViewShownEvictWatermark", "PerViewShownEvictWatermark", App::ParamInfo::Float, 0.9)
        .setTitle("Per-view shown eviction watermark")
        .setDoc("GPU memory use, as a fraction of the budget, above which objects that\n"
"one view had shown on its own and no view shows any more are freed.\n"
"They go first, before anything that costs visible quality. 1 or more\n"
"waits for the budget itself."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LevelScaleBoxError", "LevelScaleBoxError", App::ParamInfo::Float, 0.25)
        .setTitle("Level scale box error")
        .setDoc("Error, relative to its diagonal, at which an object is no longer\n"
"tessellated and is drawn as its bounding box. 0 or less never uses a\n"
"box."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "SimplifyExhausted", "SimplifyExhausted", App::ParamInfo::Bool, true)
        .setTitle("Decimate when tessellation is spent")
        .setDoc("When tessellating an object coarser no longer removes triangles,\n"
"decimate the mesh it has instead of going straight to its bounding\n"
"box. Display only: the shape and its exact mesh are untouched. Section\n"
"caps through a decimated object can be rough."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "SimplifyMergeParts", "SimplifyMergeParts", App::ParamInfo::Bool, false)
        .setTitle("Decimate across faces")
        .setDoc("Let decimation merge vertices across face boundaries. Removes far\n"
"more triangles on shapes made of flat faces, but rounds real creases.\n"
"Per-face colour and selection keep working either way."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "SimplifyMinReduction", "SimplifyMinReduction", App::ParamInfo::Float, 20.0)
        .setTitle("Decimation worth doing (%)")
        .setDoc("Percentage of an object's triangles a decimation has to remove for the\n"
"result to be kept. Below it the decimation is dropped and the object\n"
"goes on to its bounding box."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ShapeVertices", "ShapeVertices", App::ParamInfo::Bool, true)
        .setTitle("Draw edge-attached vertices")
        .setDoc("Draw the vertex points at the ends of a shape's edges. They are the\n"
"first thing dropped when GPU memory is short and the last to return.\n"
"Off never draws them. Free vertices and point clouds are always drawn,\n"
"as is everything in Points mode; picking and highlighting are\n"
"unaffected."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PressureDropEdges", "PressureDropEdges", App::ParamInfo::Bool, true)
        .setTitle("Drop face edges under pressure")
        .setDoc("Allow the edges that bound faces to stop being drawn when GPU memory\n"
"is short, after the vertices and before any face quality is given up.\n"
"Wires, sketches and other edges no face uses are never dropped, and\n"
"nothing is dropped in Wireframe mode. Picking and highlighting are\n"
"unaffected."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ElementGateStagger", "ElementGateStagger", App::ParamInfo::Int, 15)
        .setTitle("Element gate stage frames")
        .setDoc("Frames to wait between the steps that drop vertices and then edges\n"
"when GPU memory is short, and between the steps that bring them back."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "TinyElementCutoff", "TinyElementCutoff", App::ParamInfo::Int, 0)
        .setTitle("Tiny element draw cutoff")
        .setDoc("Measuring tool, 0 = off. Stops drawing every line and point set of\n"
"this many primitives or fewer, to see what the number of draw calls\n"
"costs. Real edges disappear while it is on; not a display setting."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LoadDropElements", "LoadDropElements", App::ParamInfo::Bool, true)
        .setTitle("Drop elements while loading")
        .setDoc("Stop drawing face edges and edge vertices while a document is still\n"
"loading, and draw them again once it has arrived. Wires, sketches,\n"
"datum lines and point clouds are always drawn. Applies only with\n"
"coarse-first tessellation (CoarseTessellation 0 or above)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ElementTakeInSets", "ElementTakeInSets", App::ParamInfo::Int, 1000)
        .setTitle("Element sets a frame takes in")
        .setDoc("How many edge and point sets not on the GPU yet one frame may take in;\n"
"the rest comes in the frames after. 0 = no bound. Keeps the frame after\n"
"a load, or a camera fitted to a large assembly, from uploading every\n"
"set in one long call. Never holds back a wire, a sketch, a point cloud\n"
"or a highlight."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ElementTakeInKB", "ElementTakeInKB", App::ParamInfo::Int, 0)
        .setTitle("Element buffers a frame takes in (KB)")
        .setDoc("The same bound in kilobytes of GPU buffers (ElementTakeInSets):\n"
"whichever of the two is spent first ends what a frame takes in.\n"
"One set is always taken, whatever its size. 0 = no bound of\n"
"this kind, and the default: on the driver measured the bytes\n"
"were no cost beside the number of buffers. It is here for a\n"
"model of few and very large edge sets, which was not measured."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "EffectResolution", "EffectResolution", App::ParamInfo::Float, 1.0)
        .setTitle("Effect resolution")
        .setDoc("Resolution of the costly screen-space passes (ground reflection,\n"
"water depth, ambient occlusion) relative to the view, 0.25 to 1.\n"
"Lower is faster and softer; geometry, edges and text stay at full\n"
"resolution."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "TemporalAccum", "TemporalAccum", App::ParamInfo::Bool, false)
        .setTitle("Idle temporal accumulation")
        .setDoc("Keep refining the image while the camera holds still: each further\n"
"frame is shifted by a fraction of a pixel and averaged in, which\n"
"smooths what multisampling cannot (highlights, ambient occlusion,\n"
"outlines). Discarded on any change, so nothing ghosts. Costs GPU time\n"
"while idle, until TemporalAccumSamples frames have been added."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "TemporalAccumSamples", "TemporalAccumSamples", App::ParamInfo::Int, 32)
        .setTitle("Idle accumulation samples")
        .setDoc("How many frames idle accumulation adds up before the view goes quiet,\n"
"2 to 256. Most of the gain comes in the first few; more gives a\n"
"cleaner still image and takes longer."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Occlusion", "Occlusion", App::ParamInfo::Bool, false)
        .setTitle("Occlusion culling")
        .setDoc("Skip drawing objects that are completely hidden behind others. The\n"
"image does not change; what is saved is the draw calls. Helps on\n"
"assemblies that hide their own insides, does little for a model that\n"
"is mostly outline. Shadows and reflections of hidden objects are kept."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionVisibleTtl", "OcclusionVisibleTtl", App::ParamInfo::Int, 6)
        .setTitle("Occlusion visible lifetime")
        .setDoc("How many frames a node found visible is believed before it is\n"
"tested again. Higher spends fewer queries and keeps drawing\n"
"geometry that has since become hidden for a little longer; lower\n"
"tracks the camera more closely at the cost of more tests. Purely\n"
"a cost trade -- being late here draws too much, never too\n"
"little, so it cannot affect the image."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionBudget", "OcclusionBudget", App::ParamInfo::Int, 128)
        .setTitle("Occlusion query budget")
        .setDoc("How many occlusion tests one frame may issue. The GPU offers\n"
"256 for the whole process and the RenderDebug_Occlusion\n"
"measurement is the other claimant, so the default leaves that\n"
"measurement room to run alongside. Asking for more tests than\n"
"the budget allows is not an error: hidden nodes are offered\n"
"first, since a test is the only way one can come back, and the\n"
"rest are offered again next frame."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionMinSubtree", "OcclusionMinSubtree", App::ParamInfo::Int, 8)
        .setTitle("Occlusion minimum subtree")
        .setDoc("Do not test an index node standing for fewer drawn instances\n"
"than this. A test is itself a draw, so testing a node that could\n"
"save one draw loses whether it answers hidden or visible."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionMaxHidden", "OcclusionMaxHidden", App::ParamInfo::Int, 120)
        .setTitle("Occlusion hidden lifetime")
        .setDoc("With GPU occlusion queries: frames a hidden object may go without a\n"
"new answer before it is drawn again. A safeguard for when answers\n"
"stop arriving."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionDepthPad", "OcclusionDepthPad", App::ParamInfo::Int, 16)
        .setTitle("Occlusion depth padding")
        .setDoc("With GPU occlusion queries: how far a test box is moved towards the\n"
"viewer, in depth buffer steps, so that a part lying flat on a larger\n"
"one is not judged hidden. Too small hides visible parts, too large\n"
"hides less; err high."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionConfirm", "OcclusionConfirm", App::ParamInfo::Int, 2)
        .setTitle("Occlusion confirmations")
        .setDoc("With GPU occlusion queries: how many answers of 'hidden' in a row an\n"
"object needs before it is skipped. More reduces flicker and does not\n"
"remove it. Not used when occlusion runs on the CPU, the default."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionSoftware", "OcclusionSoftware", App::ParamInfo::Bool, true)
        .setTitle("Occlusion on the CPU")
        .setDoc("Decide what is hidden with a depth buffer drawn on the CPU instead of\n"
"GPU occlusion queries. The default: its answers belong to the frame\n"
"that asked, where a GPU query answers a frame or two late and can\n"
"flicker. Costs some CPU time per frame."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionOccluderTris", "OcclusionOccluderTris", App::ParamInfo::Int, 250000)
        .setTitle("Occlusion occluder budget")
        .setDoc("How many triangles the CPU occlusion buffer may rasterize in one\n"
"frame. Only used when occlusion runs on the CPU.\n"
"\n"
"Occluders are spent largest-on-screen first, so what the budget\n"
"drops is what would have hidden least. Dropping them costs\n"
"culling and never pixels: an occluder that was not rasterized\n"
"simply hides nothing."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionMinOccluder", "OcclusionMinOccluder", App::ParamInfo::Int, 24)
        .setTitle("Occlusion minimum occluder")
        .setDoc("How large a draw must appear on screen, in pixels across its\n"
"bounding box diagonal, before it is worth rasterizing into the\n"
"CPU occlusion buffer. Smaller draws can hide almost nothing and\n"
"spend budget that a larger one could use."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionThreads", "OcclusionThreads", App::ParamInfo::Int, 0)
        .setTitle("Occlusion occluder threads")
        .setDoc("Worker threads the CPU occlusion buffer may draw its occluders on.\n"
"0 chooses automatically."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionSimd", "OcclusionSimd", App::ParamInfo::Bool, true)
        .setTitle("Occlusion vector pre-pass")
        .setDoc("With CPU occlusion, discard triangles too small to cover a pixel four\n"
"at a time before the exact rasterizer sees them. Faster, and it cannot\n"
"hide anything visible. Turn off only to measure what it saves."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionResolution", "OcclusionResolution", App::ParamInfo::Int, 1)
        .setTitle("Occlusion buffer divisor")
        .setDoc("Resolution of the CPU occlusion buffer, as a divisor of the view size.\n"
"1 matches the view. Above 1 it can hide visible geometry; change it\n"
"only to measure."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionPerInstance", "OcclusionPerInstance", App::ParamInfo::Bool, true)
        .setTitle("Occlusion per instance")
        .setDoc("With CPU occlusion, test each object on its own and not only the group\n"
"it was sorted into, so one visible object no longer keeps its hidden\n"
"neighbours drawn. Hides more for little cost; on by default."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionDemoteStreak", "OcclusionDemoteStreak", App::ParamInfo::Int, 8)
        .setTitle("Occlusion demote streak")
        .setDoc("With CPU occlusion: how many frames in a row an object must have been\n"
"completely hidden before its GPU memory may be given back at no\n"
"quality cost. It stays in main memory and returns when seen again.\n"
"0 never does this."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionCoarse", "OcclusionCoarse", App::ParamInfo::Bool, false)
        .setTitle("Occlusion coarse occluders")
        .setDoc("With CPU occlusion, draw the occluders from simplified hulls instead\n"
"of their full meshes, so many more of them fit the triangle budget and\n"
"more gets hidden. A hull is moved back by its own error, so it cannot\n"
"hide what is visible."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionCoarseLevel", "OcclusionCoarseLevel", App::ParamInfo::Int, 2)
        .setTitle("Occlusion hull level")
        .setDoc("Which rung of the decimation ladder an occluder hull is built\n"
"at, coarsest first: the clustering grid is an eighth of the\n"
"mesh's diagonal at 0 and halves per level, so 2 is a\n"
"thirty-second of it. Lower is cheaper to rasterize and further\n"
"from the surface; higher approaches the mesh itself."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionCoarseMinTris", "OcclusionCoarseMinTris", App::ParamInfo::Int, 512)
        .setTitle("Occlusion hull minimum")
        .setDoc("How many triangles a draw must carry before it is worth a\n"
"hull. Below this it is rasterized from its mesh: a hull of a\n"
"small mesh saves triangles that were never what spent the\n"
"budget."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionCoarseBuilds", "OcclusionCoarseBuilds", App::ParamInfo::Int, 8)
        .setTitle("Occlusion hull builds")
        .setDoc("How many occluder hulls may be built in one frame. Building is\n"
"parallel but not free, so a scene that has just come into view\n"
"acquires its hulls over several frames rather than stalling one.\n"
"0 freezes the cache at what it already holds."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionCoarseBias", "OcclusionCoarseBias", App::ParamInfo::Int, 100)
        .setTitle("Occlusion hull bias")
        .setDoc("With coarse occluders: how far a hull is moved away from the camera,\n"
"as a percentage of its own error. 100 guarantees it hides nothing\n"
"visible; less hides more and may hide visible parts."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionCoarseMemory", "OcclusionCoarseMemory", App::ParamInfo::Int, 64)
        .setTitle("Occlusion hull memory")
        .setDoc("What the occluder hull cache may hold, in megabytes, before\n"
"the least recently used hulls are dropped. A dropped hull costs a\n"
"rebuild when its occluder comes back into view, never\n"
"correctness."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "OcclusionBenefitProbe", "OcclusionBenefitProbe", App::ParamInfo::Bool, false)
        .setTitle("Occlusion benefit probe")
        .setDoc("Diagnostic. Measures whether occlusion culling pays for itself on this\n"
"scene and camera, by turning it on and off for stretches of frames and\n"
"comparing their cost. It disturbs the frames it measures; not for\n"
"normal use."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "AO", "AO", App::ParamInfo::Bool, false)
        .setTitle("Ambient occlusion")
        .setDoc("Enable screen space ambient occlusion of the experimental render\n"
"engine (render cache mode 3 with a selected renderer type)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Shadow", "Shadow", App::ParamInfo::Bool, true)
        .setTitle("Shadow")
        .setDoc("Render the shadow map cast by the Shadow display style's scene\n"
"light (and the god-ray shafts / caustic occlusion that depend on\n"
"it). A convenience switch to drop shadows without leaving the\n"
"Shadow display style; the base headlight and environment lighting\n"
"stay, so the scene remains lit, just flatter. Has no effect unless\n"
"the Shadow display style provides a scene light."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "AOMethod", "AOMethod", App::ParamInfo::Int, 0)
        .setTitle("AO method")
        .setDoc("Ambient occlusion algorithm. 0 = classic hemisphere-kernel\n"
"SSAO (screen-space depth-difference sampling). 1 = GTAO\n"
"(ground-truth ambient occlusion, XeGTAO-style horizon-based\n"
"visibility integration): physically correct occlusion falloff,\n"
"tight contact shadows without the wide low-contrast wash of\n"
"classic SSAO at large radii.")
        .setProxy("ComboBox")
        .setItems({{"SSAO (hemisphere)", "", nullptr}, {"GTAO (horizon)", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "AOSlices", "AOSlices", App::ParamInfo::Int, 9)
        .setTitle("GTAO slices")
        .setDoc("GTAO only: number of screen-space slice directions per pixel\n"
"(XeGTAO High preset = 9). The dominant quality/cost dial —\n"
"direction variance shows as blotchy grain the denoiser cannot\n"
"fully flatten. Cost scales linearly."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "AOSteps", "AOSteps", App::ParamInfo::Int, 3)
        .setTitle("GTAO steps")
        .setDoc("GTAO only: horizon-march samples per slice side. More steps\n"
"resolve distant occluders more stably (less mid-frequency blotch\n"
"on grazing surfaces), at linear cost."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "AORadius", "AORadius", App::ParamInfo::Float, 0.0)
        .setTitle("Sample radius")
        .setDoc("Ambient occlusion sample radius in world units.\n"
"Zero means automatic (a fraction of the scene size)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "AOIntensity", "AOIntensity", App::ParamInfo::Float, 0.6)
        .setTitle("Intensity")
        .setDoc("Ambient occlusion darkening strength."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "AOResolution", "AOResolution", App::ParamInfo::Float, 1.0)
        .setTitle("AO resolution")
        .setDoc("Resolution of ambient occlusion relative to the view, 0.25 to 1,\n"
"independent of EffectResolution. Lower is faster and less sharp."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Cavity", "Cavity", App::ParamInfo::Bool, true)
        .setTitle("Cavity shading")
        .setDoc("Darken creases and ridges of the geometry in screen space, so the\n"
"shape reads without relying on the lighting. Works best with the\n"
"Shaded draw style, where no edges are drawn. Independent of ambient\n"
"occlusion. Needs render cache mode 3 with a renderer selected."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CavityRadius", "CavityRadius", App::ParamInfo::Float, 1.0)
        .setTitle("Cavity radius")
        .setDoc("Distance in pixels over which cavity shading measures curvature. 1\n"
"sees only hard creases, sharply. Larger values bring in fillets and\n"
"broad curvature and widen the creases to a band of that width."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CavityValley", "CavityValley", App::ParamInfo::Float, 1.0)
        .setTitle("Valley darkening")
        .setDoc("Cavity darkening strength in concave creases (inside corners,\n"
"fillets, pockets). Zero disables the valley term."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CavityRidge", "CavityRidge", App::ParamInfo::Float, 0.5)
        .setTitle("Ridge darkening")
        .setDoc("Cavity darkening strength on convex ridges (outside corners,\n"
"chamfers). Reads as a soft contour along edges. Zero disables the\n"
"ridge term.\n"
"\n"
"Both terms darken: the pass multiplies the finished 8-bit scene\n"
"color, which cannot brighten past white, so the ridge highlight\n"
"some workbench renderers use is not available here."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Matcap", "Matcap", App::ParamInfo::Bool, false)
        .setTitle("Matcap shading")
        .setDoc("Shade surfaces by the direction they face the viewer, with a fixed\n"
"studio lighting attached to the camera, so shape reads the same\n"
"wherever the scene light is. Overrides physically based shading.\n"
"Needs render cache mode 3 with a renderer selected."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "MatcapPreset", "MatcapPreset", App::ParamInfo::Int, 0)
        .setTitle("Matcap")
        .setDoc("Which matcap to shade with, computed in the shader. Studio: soft key\n"
"light with a rim. Clay: matte, the most neutral read of form. Metal:\n"
"banded, exaggerates curvature. Pearl: warm and cool, shows shallow\n"
"undulation. Zebra: black and white stripes that step at an angle, kink\n"
"at a tangent seam and run through where curvature is continuous.")
        .setProxy("ComboBox")
        .setItems({{"Studio", "", nullptr}, {"Clay", "", nullptr}, {"Metal", "", nullptr}, {"Pearl", "", nullptr}, {"Zebra", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "MatcapStripes", "MatcapStripes", App::ParamInfo::Int, 6)
        .setTitle("Zebra stripes")
        .setDoc("Zebra matcap only: how many dark/light stripe pairs the\n"
"mirrored room has between its two poles. More stripes show a\n"
"smaller change of direction, until they are finer than the\n"
"view can draw -- zoom in rather than raise it without end. The\n"
"stripes are as true as the view mesh: lower the object's\n"
"Deviation before reading a fine pattern."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "MatcapTint", "MatcapTint", App::ParamInfo::Float, 1.0)
        .setTitle("Matcap object tint")
        .setDoc("How much each object's own color tints the matcap, 0 to 1.\n"
"One multiplies the matcap by the object color, so the matcap\n"
"supplies the shading and the assembly keeps its color coding.\n"
"Zero shades the whole scene as one uniform material instead,\n"
"which drops the color coding but makes shape directly\n"
"comparable across parts."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBR", "PBR", App::ParamInfo::Bool, false)
        .setTitle("Physically based shading")
        .setDoc("Enable physically based shading with image based lighting of\n"
"the experimental render engine (render cache mode 3 with a\n"
"selected renderer type). Replaces the Classic headlight shading\n"
"of lit surfaces with a metallic/roughness material lit by a\n"
"built-in studio environment."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBRMetallic", "PBRMetallic", App::ParamInfo::Float, 0.0)
        .setTitle("Metallic")
        .setDoc("Metalness of physically based shaded surfaces, 0 to 1."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBRRoughness", "PBRRoughness", App::ParamInfo::Float, 0.0)
        .setTitle("Roughness")
        .setDoc("Roughness of physically based shaded surfaces, 0 to 1.\n"
"Zero means automatic (derived from each material's shininess)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBRFromSpecular", "PBRFromSpecular", App::ParamInfo::Bool, true)
        .setTitle("Specular to metallic")
        .setDoc("Read the specular colour of a classic appearance as metalness when\n"
"the material states none, so that presets such as Gold or Steel look\n"
"like metal under physically based shading. A stated metalness is\n"
"never changed."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "ShininessMapping", "ShininessMapping", App::ParamInfo::Int, 1)
        .setTitle("Shininess mapping")
        .setDoc("How the shininess of a classic appearance becomes a roughness when the\n"
"material states none. 'GL exponent' reads it as the OpenGL exponent,\n"
"where the shiniest material is still satin. 'Full range' reads it as\n"
"0 to 100%, matte to mirror. A stated roughness is never changed.")
        .setProxy("ComboBox")
        .setItems({{"GL exponent", "", nullptr}, {"Full range", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBREnvPreset", "PBREnvPreset", App::ParamInfo::Int, 1)
        .setTitle("Environment")
        .setDoc("Built-in environment that lights the scene when no environment image\n"
"is set: Gradient (the default, the most even), Interior, Studio,\n"
"Overcast, Sunset or Light tent. They differ in contrast and structure,\n"
"not in brightness, so one exposure suits them all.")
        .setProxy("ComboBox")
        .setItems({{"Studio", "", nullptr}, {"Gradient", "", nullptr}, {"Overcast", "", nullptr}, {"Sunset", "", nullptr}, {"Interior", "", nullptr}, {"Light tent", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBREnvIntensity", "PBREnvIntensity", App::ParamInfo::Float, 1.0)
        .setTitle("Environment brightness")
        .setDoc("Brightness of the image based lighting environment."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBREnvImage", "PBREnvImage", App::ParamInfo::String, "")
        .setTitle("Environment image")
        .setDoc("Image file used as the lighting environment instead of the built-in\n"
"one. A 2:1 image is read as a lat-long panorama, anything squarer as a\n"
"sphere map. Radiance files (.hdr, .pic) keep their real brightness and\n"
"are the format to use; OpenEXR is not read. 1K or 2K is plenty. Empty\n"
"uses the Texture mapping dialog's image, then the built-in environment."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBREnvEmbed", "PBREnvEmbed", App::ParamInfo::Bool, true)
        .setTitle("Embed environment image")
        .setDoc("Store a copy of the environment image in the document, so the lighting\n"
"travels with the file instead of depending on a path on one machine.\n"
"The copy takes precedence over the path."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBREnvBackground", "PBREnvBackground", App::ParamInfo::Bool, true)
        .setTitle("Environment background")
        .setDoc("Show the lighting environment as the view background while physically\n"
"based shading is active, so reflections have a visible source. Other\n"
"shading models keep the background gradient."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "PBREnvBlur", "PBREnvBlur", App::ParamInfo::Float, 0.25)
        .setTitle("Environment background blur")
        .setDoc("How far out of focus the environment is drawn as the background, 0 to\n"
"1. 0 is as sharp as it was baked. Only the background is affected:\n"
"lighting and reflections always read the sharp environment."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "BumpScale", "BumpScale", App::ParamInfo::Float, 1.0)
        .setTitle("Bump strength")
        .setDoc("Strength of bump/normal mapped surfaces (SoBumpMap) of the\n"
"experimental render engine: scales the slope of normal maps and\n"
"the height amplitude of grayscale bump maps."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Parallax", "Parallax", App::ParamInfo::Bool, true)
        .setTitle("Parallax occlusion mapping")
        .setDoc("Parallax-occlusion map grayscale bump maps (SoBumpMap) of the\n"
"experimental render engine, shifting the texture with the view\n"
"angle for a strong relief impression."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Volumetric", "Volumetric", App::ParamInfo::Bool, false)
        .setTitle("Light shafts")
        .setDoc("Enable volumetric lighting (light shafts) of the experimental\n"
"render engine: raymarch the shadow map of the Shadow display style\n"
"through a homogeneous scattering medium. Only effective while\n"
"the Shadow display style provides a scene light."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "VolumetricIntensity", "VolumetricIntensity", App::ParamInfo::Float, 1.0)
        .setTitle("Intensity")
        .setDoc("Brightness of the inscattered (light shaft) light."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "VolumetricDensity", "VolumetricDensity", App::ParamInfo::Float, 0.0)
        .setTitle("Medium density")
        .setDoc("Scattering medium density in inverse world units.\n"
"Zero means automatic (a fraction of the scene size)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Caustics", "Caustics", App::ParamInfo::Bool, false)
        .setTitle("Water caustics")
        .setDoc("Project an animated caustic light pattern onto surfaces\n"
"below the water body (objects with the Render_Water property),\n"
"modulated by the shadow map. Only effective while volumetric\n"
"lighting and the Shadow display style are active."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CausticsIntensity", "CausticsIntensity", App::ParamInfo::Float, 1.0)
        .setTitle("Caustics intensity")
        .setDoc("Brightness of the projected caustic pattern."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CausticsScale", "CausticsScale", App::ParamInfo::Float, 0.0)
        .setTitle("Caustics scale")
        .setDoc("Caustic pattern cell frequency in inverse world units.\n"
"Zero means automatic (a fraction of the water body size)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CausticsSpeed", "CausticsSpeed", App::ParamInfo::Float, 1.0)
        .setTitle("Caustics speed")
        .setDoc("Animation speed of the caustic pattern; zero freezes it."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterSurface", "WaterSurface", App::ParamInfo::Bool, false)
        .setTitle("Water surface")
        .setDoc("Shade water bodies (objects with the Render_Water property)\n"
"as an animated water surface: screen-space refraction of the\n"
"scene behind it, Fresnel-blended environment reflection and a\n"
"sun glint from the Shadow display style light."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterWaveStrength", "WaterWaveStrength", App::ParamInfo::Float, 0.3)
        .setTitle("Wave strength")
        .setDoc("Amplitude of the animated wave perturbation of the water\n"
"surface normal; zero gives a flat mirror-like surface."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterWaveScale", "WaterWaveScale", App::ParamInfo::Float, 0.0)
        .setTitle("Wave scale")
        .setDoc("Wave frequency in inverse world units.\n"
"Zero means automatic (a fraction of the water body size)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterWaveSpeed", "WaterWaveSpeed", App::ParamInfo::Float, 1.0)
        .setTitle("Wave speed")
        .setDoc("Animation speed of the water surface waves; zero freezes\n"
"them."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterAbsorption", "WaterAbsorption", App::ParamInfo::Float, 0.2)
        .setTitle("Absorption")
        .setDoc("Beer-Lambert absorption strength of the water surface\n"
"refraction: the refracted scene is dimmed and tinted by the\n"
"water column it travels through (channels the water color lacks\n"
"are absorbed most), so the water gains body and the bottom\n"
"recedes with depth. Zero = crystal clear."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterInscatter", "WaterInscatter", App::ParamInfo::Float, 0.5)
        .setTitle("In-scatter")
        .setDoc("How much the water's own color is added back into the\n"
"depth-absorbed refraction (in-scattering); zero leaves absorbed\n"
"regions dark, one fills them with the water color."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterRefraction", "WaterRefraction", App::ParamInfo::Bool, true)
        .setTitle("Refraction")
        .setDoc("Screen-space refraction of the scene behind the water\n"
"surface. When off the surface shows a flat water colour instead\n"
"of the see-through refracted scene."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterReflection", "WaterReflection", App::ParamInfo::Bool, true)
        .setTitle("Reflection")
        .setDoc("Reflection on the water surface (Fresnel-blended). When off\n"
"the surface only refracts. See WaterPlanarReflection for the\n"
"reflection method."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterPlanarReflection", "WaterPlanarReflection", App::ParamInfo::Bool, true)
        .setTitle("Planar reflection")
        .setDoc("Reflection method when WaterReflection is on: planar (a\n"
"mirror-camera re-render of the scene about the water plane -\n"
"exact, no taper) when true, else screen-space reflection (a\n"
"cheaper per-pixel ray march that can only reflect on-screen\n"
"geometry and tapers past it). The environment cubemap is the\n"
"fallback for both."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterShadow", "WaterShadow", App::ParamInfo::Bool, true)
        .setTitle("Water shadow")
        .setDoc("Receive the scene light's shadow on the water surface: a\n"
"shadow band on the water where a caster blocks the light and\n"
"the sun glint killed there. Requires the Shadow display style\n"
"with an active shadow map; off leaves the surface fully lit.\n"
"The refracted scene below the surface keeps its own shadow\n"
"regardless."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterRippleType", "WaterRippleType", App::ParamInfo::Int, 0)
        .setTitle("Ripple type")
        .setDoc("Ripples the water surface has by itself: 0 wind waves, 1 rain rings,\n"
"2 none. Rings from fountains and from particles striking the water\n"
"show in every case.")
        .setProxy("ComboBox")
        .setItems({{"Waves (directional)", "", nullptr}, {"Rain (drops)", "", nullptr}, {"None (still)", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterRippleDensity", "WaterRippleDensity", App::ParamInfo::Float, 1.0)
        .setTitle("Ripple density")
        .setDoc("Drop density of the rain ripple type: how many drop cells\n"
"fit per wave-scale unit. Higher rains harder - more, smaller\n"
"rings; lower gives sparse large rings. The wave ripple type\n"
"ignores it."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterImpactStrength", "WaterImpactStrength", App::ParamInfo::Float, 1.0)
        .setTitle("Impact ring strength")
        .setDoc("Height of the rings raised where particles actually strike\n"
"the water - a fountain's droplets landing in its own basin.\n"
"Unlike the rain ripple type these are not a pattern: nothing\n"
"appears unless something hits the surface, and it appears\n"
"where it hit. Zero turns them off. Needs a stateful emitter\n"
"whose step program reports its impacts."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterImpactLife", "WaterImpactLife", App::ParamInfo::Float, 1.1)
        .setTitle("Impact ring life")
        .setDoc("How long an impact ring lives, in seconds - which is also\n"
"how far it travels, since a ring is sized to have crossed two\n"
"cells of the impact map when it dies. Longer makes slower,\n"
"wider-travelling rings out of the same hits."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "WaterShadowWobble", "WaterShadowWobble", App::ParamInfo::Float, 1.0)
        .setTitle("Shadow wobble")
        .setDoc("How much the shadow band on the water surface wobbles with\n"
"the wave field: the shadow is looked up at the wave-displaced\n"
"surface point scaled by this factor. Zero pins the shadow\n"
"boundary to the flat surface (a straight edge), one is the\n"
"physical wave height, larger values exaggerate the ripple."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Bloom", "Bloom", App::ParamInfo::Bool, false)
        .setTitle("Bloom")
        .setDoc("Bleed a blurred glow halo from bright pixels and from\n"
"light-source bodies (objects with the Render_Light property)\n"
"over their surroundings."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "BloomThreshold", "BloomThreshold", App::ParamInfo::Float, 0.9)
        .setTitle("Bloom threshold")
        .setDoc("Scene brightness above which a pixel feeds the glow halo\n"
"(with a soft knee below it). Light-source bodies always feed\n"
"it regardless, scaled by their intensity."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "BloomIntensity", "BloomIntensity", App::ParamInfo::Float, 1.0)
        .setTitle("Bloom intensity")
        .setDoc("Brightness multiplier of the composited glow halo."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "BloomRadius", "BloomRadius", App::ParamInfo::Float, 1.0)
        .setTitle("Bloom radius")
        .setDoc("Radius scale of the glow halo. One is the default gaussian\n"
"footprint; larger blooms wider."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "Light", "Light", App::ParamInfo::Bool, false)
        .setTitle("Renderer scene light")
        .setDoc("Let the render engine use a scene light of its own, described by the\n"
"Light settings below, for shadows, light shafts and ground reflection.\n"
"Without it the only such light is the one the Shadow display style\n"
"adds. A light found in the scene still takes precedence."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightIntensity", "LightIntensity", App::ParamInfo::Float, 0.8)
        .setTitle("Light intensity")
        .setDoc("Brightness of the renderer's own scene light."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightDirectionX", "LightDirectionX", App::ParamInfo::Float, -1.0)
        .setTitle("Light Direction X")
        .setDoc("X component of the direction the render engine's own scene light\n"
"shines along, in world coordinates. A direction of zero length\n"
"falls back to (-1, -1, -1)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightDirectionY", "LightDirectionY", App::ParamInfo::Float, -1.0)
        .setTitle("Light Direction Y")
        .setDoc("Y component of the direction the render engine's own scene light\n"
"shines along, in world coordinates. A direction of zero length\n"
"falls back to (-1, -1, -1)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightDirectionZ", "LightDirectionZ", App::ParamInfo::Float, -1.0)
        .setTitle("Light Direction Z")
        .setDoc("Z component of the direction the render engine's own scene light\n"
"shines along, in world coordinates. A direction of zero length\n"
"falls back to (-1, -1, -1)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightColor", "LightColor", App::ParamInfo::Hex, 0xF0FDFFFF)
        .setTitle("Light color")
        .setDoc("Colour of the renderer's own scene light.")
        .setProxy("Color")
        .setTransparency(true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightSpot", "LightSpot", App::ParamInfo::Bool, false)
        .setTitle("Use spot light")
        .setDoc("Make the renderer's own light a spot rather than a directional\n"
"one. A spot has a position and a cone; a directional light has\n"
"only a direction."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightPositionX", "LightPositionX", App::ParamInfo::Float, 0.0)
        .setTitle("Light Position X")
        .setDoc("X coordinate of the render engine's own scene light when it is a\n"
"spot light, in world coordinates. A directional light ignores it."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightPositionY", "LightPositionY", App::ParamInfo::Float, 0.0)
        .setTitle("Light Position Y")
        .setDoc("Y coordinate of the render engine's own scene light when it is a\n"
"spot light, in world coordinates. A directional light ignores it."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightPositionZ", "LightPositionZ", App::ParamInfo::Float, 0.0)
        .setTitle("Light Position Z")
        .setDoc("Z coordinate of the render engine's own scene light when it is a\n"
"spot light, in world coordinates. A directional light ignores it."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightCutOffAngle", "LightCutOffAngle", App::ParamInfo::Float, 45.0)
        .setTitle("Spot cut-off angle")
        .setDoc("Half angle of the spot cone, in degrees."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "LightDropOffRate", "LightDropOffRate", App::ParamInfo::Float, 0.0)
        .setTitle("Spot drop-off rate")
        .setDoc("How sharply a spot falls off from the cone axis. Zero is even\n"
"across the cone."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "SunDisc", "SunDisc", App::ParamInfo::Bool, false)
        .setTitle("Sun disc")
        .setDoc("Draw a visible sun -- a bright disc with a limb glow -- in\n"
"the sky along the Shadow display style's directional scene light,\n"
"occluded by geometry and feeding the bloom glow. Perspective\n"
"cameras only; spot lights have no sky direction."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "SunDiscSize", "SunDiscSize", App::ParamInfo::Float, 1.5)
        .setTitle("Sun disc size")
        .setDoc("Angular radius of the sun disc in degrees (the real sun is\n"
"about 0.27; larger reads better in a CAD scene)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "GroundReflection", "GroundReflection", App::ParamInfo::Bool, false)
        .setTitle("Ground reflection")
        .setDoc("Mirror the model in the ground plane of the experimental\n"
"render engine: the opaque scene is re-rendered with a reflected\n"
"camera and blended onto the ground. Brings the ground plane out\n"
"on its own -- neither the Shadow display style nor its ground\n"
"switch is needed -- and the ground keeps its own appearance\n"
"settings (color, size, texture) from the shadow group."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "GroundReflectionIntensity", "GroundReflectionIntensity", App::ParamInfo::Float, 0.4)
        .setTitle("Reflection intensity")
        .setDoc("Blend factor of the mirrored model on the ground plane."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CyclesDevice", "CyclesDevice", App::ParamInfo::String, "CPU")
        .setTitle("Cycles device")
        .setDoc("Device the path tracer runs on: 'CPU' always works; 'CUDA', 'OPTIX' or\n"
"'HIP' when the machine has the GPU and driver. A document saved with a\n"
"device this machine lacks uses the first one available."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CyclesSamples", "CyclesSamples", App::ParamInfo::Int, 256)
        .setTitle("Cycles samples")
        .setDoc("Samples per pixel the External shading model refines to\n"
"before it rests. More is cleaner and slower to settle; the view\n"
"stays interactive either way, restarting from one sample on\n"
"every camera move."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CyclesTimeLimit", "CyclesTimeLimit", App::ParamInfo::Float, 0.0)
        .setTitle("Cycles time limit")
        .setDoc("Seconds the External shading model may refine after each\n"
"change before it rests, whatever the sample budget still says.\n"
"0 means no limit: the sample count alone decides."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CyclesDenoise", "CyclesDenoise", App::ParamInfo::Bool, true)
        .setTitle("Cycles denoise")
        .setDoc("Run OpenImageDenoise over the refining External shading\n"
"frame, trading the raw noise of the early samples for a smooth\n"
"image that sharpens as samples arrive."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CyclesPixelSize", "CyclesPixelSize", App::ParamInfo::Int, 1)
        .setTitle("Cycles pixel size")
        .setDoc("Render the External shading model at 1/n resolution and\n"
"scale up -- Blender's preview pixel size. 2 or 4 keeps a large\n"
"view fluid on a weak device at the cost of a blockier preview."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "CyclesMaxStreams", "CyclesMaxStreams", App::ParamInfo::Int, 4)
        .setTitle("Cycles served sessions")
        .setDoc("How many path-traced views this process serves to browser viewers at\n"
"once. A request past the limit is refused and that viewer stays on its\n"
"raster view. 0 or less means no limit. Desktop views are not counted."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugViewMode", "DebugViewMode", App::ParamInfo::Int, 0)
        .setTitle("Debug view mode")
        .setDoc("Diagnostic. Shows an intermediate buffer instead of the shaded scene:\n"
"1 depth, 2 normals, 3 ambient occlusion, 4 shadow, 5 shadow map\n"
"coverage, 6 overdraw, 7 shadow precision, 8 texture coordinates,\n"
"9 reflection target, 10 particle impact map. 0 renders normally.")
        .setProxy("ComboBox")
        .setItems({{"Off", "", nullptr}, {"Depth", "", nullptr}, {"Normal", "", nullptr}, {"AO", "", nullptr}, {"Shadow", "", nullptr}, {"ShadowTile", "", nullptr}, {"Overdraw", "", nullptr}, {"ShadowFilter", "", nullptr}, {"UV", "", nullptr}, {"Reflection", "", nullptr}, {"ImpactMap", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugFreezeFrame", "DebugFreezeFrame", App::ParamInfo::Bool, false)
        .setTitle("Debug freeze frame")
        .setDoc("Freeze every intentionally time- or history-dependent render\n"
"input: temporal accumulation and per-frame sampling jitter, and\n"
"time-driven animation (water waves, fire). Two frames of the same\n"
"scene, camera and parameters then render identically -- the\n"
"determinism switch for golden-image comparison\n"
"(docs/RenderDebug.md)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugLabel", "DebugLabel", App::ParamInfo::Bool, false)
        .setTitle("Debug capture label")
        .setDoc("Burn a self-describing label into a corner of the rendered\n"
"frame while render debugging: the active debug view mode, the\n"
"freeze-frame state and any custom RenderDebug_* parameter values.\n"
"A captured PNG then documents its own settings without its\n"
"sidecar (docs/RenderDebug.md)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugTiming", "DebugTiming", App::ParamInfo::Bool, false)
        .setTitle("Render stage timing")
        .setDoc("Diagnostic. Logs once a second where the time of a rendered frame\n"
"goes, by pipeline stage, and what the frame costs on the CPU against\n"
"the GPU."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugDelta", "DebugDelta", App::ParamInfo::Bool, false)
        .setTitle("Publish change set")
        .setDoc("Log what each published frame actually changed: how many of\n"
"the scene cache's children the publish reused, how many it added\n"
"or dropped, and how many separators the traversal below it reused\n"
"against how many it rebuilt. One summary line per second. A\n"
"publish rebuilds the whole scene however little moved, and these\n"
"counts are how much of that rebuild was avoidable\n"
"(docs/IncrementalPublish.md §5)."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugCoverage", "DebugCoverage", App::ParamInfo::Bool, false)
        .setTitle("Screen coverage histogram")
        .setDoc("Diagnostic. Logs how much of the screen each drawn object covers,\n"
"as a histogram over its size in pixels. Shows how much of a model is\n"
"drawn only a few pixels large."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugProxyCut", "DebugProxyCut", App::ParamInfo::Bool, false)
        .setTitle("Far-field cut estimate")
        .setDoc("Diagnostic. Logs how many draw calls replacing distant parts by\n"
"far-field proxies would save for the current camera, without building\n"
"any."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugOcclusion", "DebugOcclusion", App::ParamInfo::Bool, false)
        .setTitle("Occluded fraction")
        .setDoc("Diagnostic. Measures how much of what a frame draws is hidden behind\n"
"something else, using GPU occlusion queries. A large model takes\n"
"several frames per report."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugProxyGen", "DebugProxyGen", App::ParamInfo::Bool, false)
        .setTitle("Far-field proxy generation")
        .setDoc("Diagnostic. Builds real far-field proxies for a sample of nodes and\n"
"reports their triangle cost and the error they introduce. Expensive:\n"
"it builds meshes."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugCullAudit", "DebugCullAudit", App::ParamInfo::Bool, false)
        .setTitle("Occlusion cull audit")
        .setDoc("Diagnostic. Checks what occlusion culling skipped against what really\n"
"reaches the screen, by drawing every object once more with its\n"
"identity as its colour, and reports objects culled by mistake. Reads\n"
"the image back once a second. Not available on WebGL2."),
    App::ParamInfo("Gui", "RenderParams", "User parameter:BaseApp/Preferences/View/Render", "DebugCullBounds", "DebugCullBounds", App::ParamInfo::Bool, false)
        .setTitle("Occludee bound diagnostic")
        .setDoc("Diagnostic. Measures whether tighter bounds around objects would let\n"
"occlusion culling hide more, by asking again about every object still\n"
"drawn in three ways. Reports only, changes nothing on screen. Needs\n"
"the cull audit and CPU occlusion, and is far too slow to leave on."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle RenderParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docType() {
    return QT_TRANSLATE_NOOP("RenderParams",
"What draws a 3D view. 'Default': the render engine, on this\n"
"platform's backend. 'Legacy': the old Coin rendering, without the\n"
"engine. A backend can also be named, as 'bgfx - Direct3D11'. With\n"
"the engine the render cache is always 3, whatever its own setting\n"
"says; under 'Legacy' that setting is what counts.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & RenderParams::getType() {
    return instance()->Type;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & RenderParams::defaultType() {
    const static std::string def = "Default";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setType(const std::string &v) {
    instance()->handle->SetASCII("Type",v);
    instance()->Type = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeType() {
    instance()->handle->RemoveASCII("Type");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOutputTransform() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Colour management of the engine. 'sRGB' decodes authored colours to\n"
"linear for shading and encodes the finished frame for the display,\n"
"which is the correct pipeline. 'Off' is the older one, which shades\n"
"display numbers and renders falloff and shadow too dark; documents\n"
"written before this setting existed use it.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOutputTransform() {
    return instance()->OutputTransform;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOutputTransform() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOutputTransform(const long &v) {
    instance()->handle->SetInt("OutputTransform",v);
    instance()->OutputTransform = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOutputTransform() {
    instance()->handle->RemoveInt("OutputTransform");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docExposure() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness multiplier applied to the finished image before it is\n"
"encoded for the screen. 1 leaves it alone. Bright areas roll off\n"
"smoothly instead of clipping. Only used while the output colour\n"
"transform is on.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getExposure() {
    return instance()->Exposure;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultExposure() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setExposure(const double &v) {
    instance()->handle->SetFloat("Exposure",v);
    instance()->Exposure = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeExposure() {
    instance()->handle->RemoveFloat("Exposure");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docMaxViewIds() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many view ids the render backend may hand out, which limits how\n"
"many 3D views can draw with it at once (about 13 ids per view; a view\n"
"that finds none left falls back to plain GL). 0 asks for the build's\n"
"maximum. Read when the backend starts: a change needs a restart.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getMaxViewIds() {
    return instance()->MaxViewIds;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultMaxViewIds() {
    const static long def = 1024;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setMaxViewIds(const long &v) {
    instance()->handle->SetInt("MaxViewIds",v);
    instance()->MaxViewIds = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeMaxViewIds() {
    instance()->handle->RemoveInt("MaxViewIds");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docReadbackFrameMode() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How a frame reaches the screen on Direct3D, Vulkan and Metal. 'Wait'\n"
"shows every frame as soon as it is drawn. 'Pipelined' shows it a frame\n"
"or two late and is faster. 'Pipelined while animating' waits except\n"
"while the view redraws by itself. Not used with OpenGL.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getReadbackFrameMode() {
    return instance()->ReadbackFrameMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultReadbackFrameMode() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setReadbackFrameMode(const long &v) {
    instance()->handle->SetInt("ReadbackFrameMode",v);
    instance()->ReadbackFrameMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeReadbackFrameMode() {
    instance()->handle->RemoveInt("ReadbackFrameMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docBackgroundReleaseDelay() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Milliseconds a 3D view may stay in the background before it gives its\n"
"render targets back to free GPU memory. Returning to the view costs\n"
"one frame to rebuild them, with the same picture. 0 never releases.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getBackgroundReleaseDelay() {
    return instance()->BackgroundReleaseDelay;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultBackgroundReleaseDelay() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setBackgroundReleaseDelay(const long &v) {
    instance()->handle->SetInt("BackgroundReleaseDelay",v);
    instance()->BackgroundReleaseDelay = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeBackgroundReleaseDelay() {
    instance()->handle->RemoveInt("BackgroundReleaseDelay");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCoarseTessellation() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ladder level a shape is first tessellated at: 0 is the coarsest, each\n"
"level halves the error, and the exact mesh is built on demand when a\n"
"camera needs it. -1 always tessellates exact up front. Used by a view\n"
"in render cache mode 3 and by a scene stream server; takes effect when\n"
"a shape is tessellated again.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getCoarseTessellation() {
    return instance()->CoarseTessellation;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultCoarseTessellation() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCoarseTessellation(const long &v) {
    instance()->handle->SetInt("CoarseTessellation",v);
    instance()->CoarseTessellation = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCoarseTessellation() {
    instance()->handle->RemoveInt("CoarseTessellation");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCoarseDeferFaces() {
    return QT_TRANSLATE_NOOP("RenderParams",
"During a progressive import on the bgfx renderer, a shape with\n"
"more faces than this gets a bounding-box stand-in immediately and\n"
"even its coarse tessellation is built on the refine worker pool,\n"
"swapped in when it arrives (docs/SceneStreaming.md #13) - the\n"
"import stall otherwise scales with the largest single part. -1\n"
"disables the stand-in so every shape tessellates inline.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getCoarseDeferFaces() {
    return instance()->CoarseDeferFaces;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultCoarseDeferFaces() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCoarseDeferFaces(const long &v) {
    instance()->handle->SetInt("CoarseDeferFaces",v);
    instance()->CoarseDeferFaces = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCoarseDeferFaces() {
    instance()->handle->RemoveInt("CoarseDeferFaces");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCoarseDeferAtLeisure() {
    return QT_TRANSLATE_NOOP("RenderParams",
"A shape drawn as a bounding box (CoarseDeferFaces) gets its mesh in the\n"
"background also where the camera does not see it, so its picture is\n"
"there when the camera turns. Off, it stays a box until the camera turns\n"
"to it. Takes effect when a shape is tessellated again.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getCoarseDeferAtLeisure() {
    return instance()->CoarseDeferAtLeisure;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultCoarseDeferAtLeisure() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCoarseDeferAtLeisure(const bool &v) {
    instance()->handle->SetBool("CoarseDeferAtLeisure",v);
    instance()->CoarseDeferAtLeisure = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCoarseDeferAtLeisure() {
    instance()->handle->RemoveBool("CoarseDeferAtLeisure");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPreMeshOnLoad() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Tessellate the shapes of a document being opened on worker threads,\n"
"before they are built for display one by one on the GUI thread. Opens\n"
"a document of many small parts sooner, with the same result. Shapes\n"
"that share faces or edges with another, and instancing candidates, are\n"
"left to the normal path.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getPreMeshOnLoad() {
    return instance()->PreMeshOnLoad;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultPreMeshOnLoad() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPreMeshOnLoad(const bool &v) {
    instance()->handle->SetBool("PreMeshOnLoad",v);
    instance()->PreMeshOnLoad = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePreMeshOnLoad() {
    instance()->handle->RemoveBool("PreMeshOnLoad");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docMeshSkipRedundant() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Check whether a shape is already tessellated the way a rebuild wants\n"
"it, and skip the tessellation call when it is. The check is strict: a\n"
"single face that would be re-tessellated and the call runs as before.\n"
"Off makes the call every time.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getMeshSkipRedundant() {
    return instance()->MeshSkipRedundant;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultMeshSkipRedundant() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setMeshSkipRedundant(const bool &v) {
    instance()->handle->SetBool("MeshSkipRedundant",v);
    instance()->MeshSkipRedundant = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeMeshSkipRedundant() {
    instance()->handle->RemoveBool("MeshSkipRedundant");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docMeshSkipFinerResident() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With redundant tessellation skipped, also count a mesh finer than the\n"
"one asked for as good enough, instead of re-tessellating to coarsen it.\n"
"Saves time on a descent, but can keep memory the level plan asked to\n"
"have back, which is why it is off by default.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getMeshSkipFinerResident() {
    return instance()->MeshSkipFinerResident;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultMeshSkipFinerResident() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setMeshSkipFinerResident(const bool &v) {
    instance()->handle->SetBool("MeshSkipFinerResident",v);
    instance()->MeshSkipFinerResident = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeMeshSkipFinerResident() {
    instance()->handle->RemoveBool("MeshSkipFinerResident");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docMeshSkipInvariant() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Skip a tessellation call on a shape whose mesh cannot depend on the\n"
"deflection asked for: every face planar and every edge a straight\n"
"line. Such a shape gives the same mesh at any coarseness, so the call\n"
"would only rebuild what is there.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getMeshSkipInvariant() {
    return instance()->MeshSkipInvariant;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultMeshSkipInvariant() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setMeshSkipInvariant(const bool &v) {
    instance()->handle->SetBool("MeshSkipInvariant",v);
    instance()->MeshSkipInvariant = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeMeshSkipInvariant() {
    instance()->handle->RemoveBool("MeshSkipInvariant");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docProgressiveLoad() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Build the display of a document after it has opened instead of during\n"
"the load. The window comes up first and the parts appear in slices,\n"
"with the view painting in between.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getProgressiveLoad() {
    return instance()->ProgressiveLoad;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultProgressiveLoad() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setProgressiveLoad(const bool &v) {
    instance()->handle->SetBool("ProgressiveLoad",v);
    instance()->ProgressiveLoad = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeProgressiveLoad() {
    instance()->handle->RemoveBool("ProgressiveLoad");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docProgressiveLoadBudgetMS() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With ProgressiveLoad: milliseconds one slice of building the display\n"
"may run before the window is updated. Larger finishes sooner, smaller\n"
"keeps the window more responsive.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getProgressiveLoadBudgetMS() {
    return instance()->ProgressiveLoadBudgetMS;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultProgressiveLoadBudgetMS() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setProgressiveLoadBudgetMS(const long &v) {
    instance()->handle->SetInt("ProgressiveLoadBudgetMS",v);
    instance()->ProgressiveLoadBudgetMS = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeProgressiveLoadBudgetMS() {
    instance()->handle->RemoveInt("ProgressiveLoadBudgetMS");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelThreads() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many mesh level builds (the scene server's on-demand\n"
"re-tessellations, docs/SceneStreaming.md #7) may run at once.\n"
"0 sizes the pool automatically - modest, because each BRepMesh\n"
"build already parallelizes internally over OCCT's shared thread\n"
"pool. The FC_LEVEL_THREADS environment variable overrides it.\n"
"Read when the server spawns its first level worker.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getLevelThreads() {
    return instance()->LevelThreads;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultLevelThreads() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelThreads(const long &v) {
    instance()->handle->SetInt("LevelThreads",v);
    instance()->LevelThreads = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelThreads() {
    instance()->handle->RemoveInt("LevelThreads");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelMemoryFloorMB() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Free system memory, in megabytes, below which no exact\n"
"re-tessellation is started; from then on exact meshes the camera does\n"
"not need are given up as well. 0 chooses automatically (512 MB, or a\n"
"sixteenth of physical memory if that is more).");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getLevelMemoryFloorMB() {
    return instance()->LevelMemoryFloorMB;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultLevelMemoryFloorMB() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelMemoryFloorMB(const long &v) {
    instance()->handle->SetInt("LevelMemoryFloorMB",v);
    instance()->LevelMemoryFloorMB = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelMemoryFloorMB() {
    instance()->handle->RemoveInt("LevelMemoryFloorMB");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelDebug() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Logs what each mesh level plan decides: the GPU budget and\n"
"the memory in use, how many objects are coarse or exact, and how many\n"
"refines and downgrades it ordered. The FC_LEVEL_DEBUG environment\n"
"variable turns it on too. Read at the first plan.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getLevelDebug() {
    return instance()->LevelDebug;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultLevelDebug() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelDebug(const bool &v) {
    instance()->handle->SetBool("LevelDebug",v);
    instance()->LevelDebug = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelDebug() {
    instance()->handle->RemoveBool("LevelDebug");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelCeilingSimulateMB() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Testing aid. Pretend the system has less free memory than this many\n"
"megabytes, so the low-memory behaviour of the level plan can be tried\n"
"on a machine with memory to spare. 0 turns it off.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getLevelCeilingSimulateMB() {
    return instance()->LevelCeilingSimulateMB;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultLevelCeilingSimulateMB() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelCeilingSimulateMB(const long &v) {
    instance()->handle->SetInt("LevelCeilingSimulateMB",v);
    instance()->LevelCeilingSimulateMB = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelCeilingSimulateMB() {
    instance()->handle->RemoveInt("LevelCeilingSimulateMB");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docGpuMemoryBudgetMB() {
    return QT_TRANSLATE_NOOP("RenderParams",
"GPU memory, in megabytes, the displayed geometry may use. Over it,\n"
"objects the camera would not miss are shown with their coarse mesh;\n"
"the exact one stays in main memory and returns at once on zooming in.\n"
"0 uses the limit the graphics API reports (OpenGL reports none, and\n"
"then no budget applies).");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getGpuMemoryBudgetMB() {
    return instance()->GpuMemoryBudgetMB;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultGpuMemoryBudgetMB() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setGpuMemoryBudgetMB(const long &v) {
    instance()->handle->SetInt("GpuMemoryBudgetMB",v);
    instance()->GpuMemoryBudgetMB = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeGpuMemoryBudgetMB() {
    instance()->handle->RemoveInt("GpuMemoryBudgetMB");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelTolerance() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Error in pixels on screen a coarse mesh may show before the exact one\n"
"is built. When the camera stops, only objects that exceed it are\n"
"refined. 0 or less refines everything at once; larger keeps more of\n"
"the scene coarse.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLevelTolerance() {
    return instance()->LevelTolerance;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLevelTolerance() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelTolerance(const double &v) {
    instance()->handle->SetFloat("LevelTolerance",v);
    instance()->LevelTolerance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelTolerance() {
    instance()->handle->RemoveFloat("LevelTolerance");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelPressureRelease() {
    return QT_TRANSLATE_NOOP("RenderParams",
"After the scene has been coarsened to fit the GPU budget: the fraction\n"
"of the raised tolerance kept each time a plan fits again, so quality\n"
"returns in steps instead of all at once and over the budget again.\n"
"Smaller returns quality faster. 0 or less returns it in one step.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLevelPressureRelease() {
    return instance()->LevelPressureRelease;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLevelPressureRelease() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelPressureRelease(const double &v) {
    instance()->handle->SetFloat("LevelPressureRelease",v);
    instance()->LevelPressureRelease = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelPressureRelease() {
    instance()->handle->RemoveFloat("LevelPressureRelease");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docClimbHardLimit() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Treat the GPU memory budget as a hard ceiling for refinement: at or\n"
"above it no object is refined and refinements under way are\n"
"cancelled; below it they are admitted in small batches\n"
"(ClimbAdmitBatch). Off refines without this check.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getClimbHardLimit() {
    return instance()->ClimbHardLimit;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultClimbHardLimit() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setClimbHardLimit(const bool &v) {
    instance()->handle->SetBool("ClimbHardLimit",v);
    instance()->ClimbHardLimit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeClimbHardLimit() {
    instance()->handle->RemoveBool("ClimbHardLimit");
}

// Auto generated code (Tools/params_utils.py:397)
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

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getClimbAdmitBatch() {
    return instance()->ClimbAdmitBatch;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultClimbAdmitBatch() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setClimbAdmitBatch(const long &v) {
    instance()->handle->SetInt("ClimbAdmitBatch",v);
    instance()->ClimbAdmitBatch = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeClimbAdmitBatch() {
    instance()->handle->RemoveInt("ClimbAdmitBatch");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelLandBudgetMS() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Milliseconds one turn of the event loop may spend installing finished\n"
"mesh level changes before it returns to painting and input. Smaller\n"
"keeps the window more responsive, larger finishes sooner.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getLevelLandBudgetMS() {
    return instance()->LevelLandBudgetMS;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultLevelLandBudgetMS() {
    const static long def = 50;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelLandBudgetMS(const long &v) {
    instance()->handle->SetInt("LevelLandBudgetMS",v);
    instance()->LevelLandBudgetMS = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelLandBudgetMS() {
    instance()->handle->RemoveInt("LevelLandBudgetMS");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docMeshSkipLanded() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Skip the tessellation call in the rebuild that follows a mesh level\n"
"change, where the mesh just installed is the one to display and the\n"
"call could only confirm it.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getMeshSkipLanded() {
    return instance()->MeshSkipLanded;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultMeshSkipLanded() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setMeshSkipLanded(const bool &v) {
    instance()->handle->SetBool("MeshSkipLanded",v);
    instance()->MeshSkipLanded = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeMeshSkipLanded() {
    instance()->handle->RemoveBool("MeshSkipLanded");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docVisualFillOnPool() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Fill the display arrays of a large object on a worker thread instead\n"
"of the GUI thread, after a mesh level change. Keeps the window\n"
"responsive while big objects change level. Applies to objects with at\n"
"least VisualFillMinFaces faces.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getVisualFillOnPool() {
    return instance()->VisualFillOnPool;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultVisualFillOnPool() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setVisualFillOnPool(const bool &v) {
    instance()->handle->SetBool("VisualFillOnPool",v);
    instance()->VisualFillOnPool = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeVisualFillOnPool() {
    instance()->handle->RemoveBool("VisualFillOnPool");
}

// Auto generated code (Tools/params_utils.py:397)
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

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getVisualFillMinFaces() {
    return instance()->VisualFillMinFaces;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultVisualFillMinFaces() {
    const static long def = 2000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setVisualFillMinFaces(const long &v) {
    instance()->handle->SetInt("VisualFillMinFaces",v);
    instance()->VisualFillMinFaces = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeVisualFillMinFaces() {
    instance()->handle->RemoveInt("VisualFillMinFaces");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWorkerVertexCache() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Use the vertex arrays a worker thread already computed for a rebuilt\n"
"shape instead of capturing them again from the scene. 0 off, 1 on,\n"
"2 does both and logs any difference (slow, for checking). Shapes with\n"
"one colour only.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getWorkerVertexCache() {
    return instance()->WorkerVertexCache;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultWorkerVertexCache() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWorkerVertexCache(const long &v) {
    instance()->handle->SetInt("WorkerVertexCache",v);
    instance()->WorkerVertexCache = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWorkerVertexCache() {
    instance()->handle->RemoveInt("WorkerVertexCache");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCaptureBudgetMS() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Milliseconds one scene update may spend capturing changed shapes\n"
"before the rest wait for the next frame. Keeps frames short while many\n"
"objects change at once; a waiting shape shows its previous state for\n"
"a frame or two. 0 captures everything in one frame.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getCaptureBudgetMS() {
    return instance()->CaptureBudgetMS;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultCaptureBudgetMS() {
    const static long def = 50;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCaptureBudgetMS(const long &v) {
    instance()->handle->SetInt("CaptureBudgetMS",v);
    instance()->CaptureBudgetMS = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCaptureBudgetMS() {
    instance()->handle->RemoveInt("CaptureBudgetMS");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelSlowBuildMS() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic, with LevelDebug: a display rebuild, or a single event,\n"
"that takes longer than this many milliseconds is logged with where the\n"
"time went. 0 turns it off.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getLevelSlowBuildMS() {
    return instance()->LevelSlowBuildMS;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultLevelSlowBuildMS() {
    const static long def = 200;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelSlowBuildMS(const long &v) {
    instance()->handle->SetInt("LevelSlowBuildMS",v);
    instance()->LevelSlowBuildMS = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelSlowBuildMS() {
    instance()->handle->RemoveInt("LevelSlowBuildMS");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDescentOrderBatch() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many downgrades one plan may order at a time. The rest are found\n"
"again by the next plan, so nothing is lost, only paced. 0 removes the\n"
"limit.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getDescentOrderBatch() {
    return instance()->DescentOrderBatch;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultDescentOrderBatch() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDescentOrderBatch(const long &v) {
    instance()->handle->SetInt("DescentOrderBatch",v);
    instance()->DescentOrderBatch = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDescentOrderBatch() {
    instance()->handle->RemoveInt("DescentOrderBatch");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDowngradeLedger() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Count the GPU memory that downgrades already ordered will free as\n"
"credit against the next plan's shortfall. Without it a plan made while\n"
"those are still in flight orders them again, and far more than needed.\n"
"Off is the older behaviour, kept for comparison.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDowngradeLedger() {
    return instance()->DowngradeLedger;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDowngradeLedger() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDowngradeLedger(const bool &v) {
    instance()->handle->SetBool("DowngradeLedger",v);
    instance()->DowngradeLedger = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDowngradeLedger() {
    instance()->handle->RemoveBool("DowngradeLedger");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelCount() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Number of levels of the mesh ladder. Level 0 is the coarsest and each\n"
"further level halves the error, so raising this adds finer levels.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getLevelCount() {
    return instance()->LevelCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultLevelCount() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelCount(const long &v) {
    instance()->handle->SetInt("LevelCount",v);
    instance()->LevelCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelCount() {
    instance()->handle->RemoveInt("LevelCount");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelScale() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Factor by which an object is tessellated coarser again when GPU\n"
"memory is still short at the coarsest ladder level. Applied object by\n"
"object, least visible error first, until the scene fits. 1 or less\n"
"turns this off.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLevelScale() {
    return instance()->LevelScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLevelScale() {
    const static double def = 2.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelScale(const double &v) {
    instance()->handle->SetFloat("LevelScale",v);
    instance()->LevelScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelScale() {
    instance()->handle->RemoveFloat("LevelScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelBudgetDeadband() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Band above the GPU memory budget, as a fraction of it, inside which no\n"
"downgrades are ordered. Keeps a scene that settles at the budget from\n"
"going back and forth across it. 0 removes the band.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLevelBudgetDeadband() {
    return instance()->LevelBudgetDeadband;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLevelBudgetDeadband() {
    const static double def = 0.03;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelBudgetDeadband(const double &v) {
    instance()->handle->SetFloat("LevelBudgetDeadband",v);
    instance()->LevelBudgetDeadband = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelBudgetDeadband() {
    instance()->handle->RemoveFloat("LevelBudgetDeadband");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPerViewShownEvictWatermark() {
    return QT_TRANSLATE_NOOP("RenderParams",
"GPU memory use, as a fraction of the budget, above which objects that\n"
"one view had shown on its own and no view shows any more are freed.\n"
"They go first, before anything that costs visible quality. 1 or more\n"
"waits for the budget itself.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getPerViewShownEvictWatermark() {
    return instance()->PerViewShownEvictWatermark;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultPerViewShownEvictWatermark() {
    const static double def = 0.9;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPerViewShownEvictWatermark(const double &v) {
    instance()->handle->SetFloat("PerViewShownEvictWatermark",v);
    instance()->PerViewShownEvictWatermark = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePerViewShownEvictWatermark() {
    instance()->handle->RemoveFloat("PerViewShownEvictWatermark");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLevelScaleBoxError() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Error, relative to its diagonal, at which an object is no longer\n"
"tessellated and is drawn as its bounding box. 0 or less never uses a\n"
"box.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLevelScaleBoxError() {
    return instance()->LevelScaleBoxError;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLevelScaleBoxError() {
    const static double def = 0.25;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLevelScaleBoxError(const double &v) {
    instance()->handle->SetFloat("LevelScaleBoxError",v);
    instance()->LevelScaleBoxError = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLevelScaleBoxError() {
    instance()->handle->RemoveFloat("LevelScaleBoxError");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docSimplifyExhausted() {
    return QT_TRANSLATE_NOOP("RenderParams",
"When tessellating an object coarser no longer removes triangles,\n"
"decimate the mesh it has instead of going straight to its bounding\n"
"box. Display only: the shape and its exact mesh are untouched. Section\n"
"caps through a decimated object can be rough.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getSimplifyExhausted() {
    return instance()->SimplifyExhausted;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultSimplifyExhausted() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setSimplifyExhausted(const bool &v) {
    instance()->handle->SetBool("SimplifyExhausted",v);
    instance()->SimplifyExhausted = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeSimplifyExhausted() {
    instance()->handle->RemoveBool("SimplifyExhausted");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docSimplifyMergeParts() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Let decimation merge vertices across face boundaries. Removes far\n"
"more triangles on shapes made of flat faces, but rounds real creases.\n"
"Per-face colour and selection keep working either way.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getSimplifyMergeParts() {
    return instance()->SimplifyMergeParts;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultSimplifyMergeParts() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setSimplifyMergeParts(const bool &v) {
    instance()->handle->SetBool("SimplifyMergeParts",v);
    instance()->SimplifyMergeParts = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeSimplifyMergeParts() {
    instance()->handle->RemoveBool("SimplifyMergeParts");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docSimplifyMinReduction() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Percentage of an object's triangles a decimation has to remove for the\n"
"result to be kept. Below it the decimation is dropped and the object\n"
"goes on to its bounding box.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getSimplifyMinReduction() {
    return instance()->SimplifyMinReduction;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultSimplifyMinReduction() {
    const static double def = 20.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setSimplifyMinReduction(const double &v) {
    instance()->handle->SetFloat("SimplifyMinReduction",v);
    instance()->SimplifyMinReduction = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeSimplifyMinReduction() {
    instance()->handle->RemoveFloat("SimplifyMinReduction");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docShapeVertices() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Draw the vertex points at the ends of a shape's edges. They are the\n"
"first thing dropped when GPU memory is short and the last to return.\n"
"Off never draws them. Free vertices and point clouds are always drawn,\n"
"as is everything in Points mode; picking and highlighting are\n"
"unaffected.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getShapeVertices() {
    return instance()->ShapeVertices;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultShapeVertices() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setShapeVertices(const bool &v) {
    instance()->handle->SetBool("ShapeVertices",v);
    instance()->ShapeVertices = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeShapeVertices() {
    instance()->handle->RemoveBool("ShapeVertices");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPressureDropEdges() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Allow the edges that bound faces to stop being drawn when GPU memory\n"
"is short, after the vertices and before any face quality is given up.\n"
"Wires, sketches and other edges no face uses are never dropped, and\n"
"nothing is dropped in Wireframe mode. Picking and highlighting are\n"
"unaffected.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getPressureDropEdges() {
    return instance()->PressureDropEdges;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultPressureDropEdges() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPressureDropEdges(const bool &v) {
    instance()->handle->SetBool("PressureDropEdges",v);
    instance()->PressureDropEdges = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePressureDropEdges() {
    instance()->handle->RemoveBool("PressureDropEdges");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docElementGateStagger() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Frames to wait between the steps that drop vertices and then edges\n"
"when GPU memory is short, and between the steps that bring them back.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getElementGateStagger() {
    return instance()->ElementGateStagger;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultElementGateStagger() {
    const static long def = 15;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setElementGateStagger(const long &v) {
    instance()->handle->SetInt("ElementGateStagger",v);
    instance()->ElementGateStagger = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeElementGateStagger() {
    instance()->handle->RemoveInt("ElementGateStagger");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docTinyElementCutoff() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Measuring tool, 0 = off. Stops drawing every line and point set of\n"
"this many primitives or fewer, to see what the number of draw calls\n"
"costs. Real edges disappear while it is on; not a display setting.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getTinyElementCutoff() {
    return instance()->TinyElementCutoff;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultTinyElementCutoff() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setTinyElementCutoff(const long &v) {
    instance()->handle->SetInt("TinyElementCutoff",v);
    instance()->TinyElementCutoff = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeTinyElementCutoff() {
    instance()->handle->RemoveInt("TinyElementCutoff");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLoadDropElements() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Stop drawing face edges and edge vertices while a document is still\n"
"loading, and draw them again once it has arrived. Wires, sketches,\n"
"datum lines and point clouds are always drawn. Applies only with\n"
"coarse-first tessellation (CoarseTessellation 0 or above).");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getLoadDropElements() {
    return instance()->LoadDropElements;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultLoadDropElements() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLoadDropElements(const bool &v) {
    instance()->handle->SetBool("LoadDropElements",v);
    instance()->LoadDropElements = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLoadDropElements() {
    instance()->handle->RemoveBool("LoadDropElements");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docElementTakeInSets() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many edge and point sets not on the GPU yet one frame may take in;\n"
"the rest comes in the frames after. 0 = no bound. Keeps the frame after\n"
"a load, or a camera fitted to a large assembly, from uploading every\n"
"set in one long call. Never holds back a wire, a sketch, a point cloud\n"
"or a highlight.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getElementTakeInSets() {
    return instance()->ElementTakeInSets;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultElementTakeInSets() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setElementTakeInSets(const long &v) {
    instance()->handle->SetInt("ElementTakeInSets",v);
    instance()->ElementTakeInSets = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeElementTakeInSets() {
    instance()->handle->RemoveInt("ElementTakeInSets");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docElementTakeInKB() {
    return QT_TRANSLATE_NOOP("RenderParams",
"The same bound in kilobytes of GPU buffers (ElementTakeInSets):\n"
"whichever of the two is spent first ends what a frame takes in.\n"
"One set is always taken, whatever its size. 0 = no bound of\n"
"this kind, and the default: on the driver measured the bytes\n"
"were no cost beside the number of buffers. It is here for a\n"
"model of few and very large edge sets, which was not measured.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getElementTakeInKB() {
    return instance()->ElementTakeInKB;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultElementTakeInKB() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setElementTakeInKB(const long &v) {
    instance()->handle->SetInt("ElementTakeInKB",v);
    instance()->ElementTakeInKB = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeElementTakeInKB() {
    instance()->handle->RemoveInt("ElementTakeInKB");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docEffectResolution() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Resolution of the costly screen-space passes (ground reflection,\n"
"water depth, ambient occlusion) relative to the view, 0.25 to 1.\n"
"Lower is faster and softer; geometry, edges and text stay at full\n"
"resolution.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getEffectResolution() {
    return instance()->EffectResolution;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultEffectResolution() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setEffectResolution(const double &v) {
    instance()->handle->SetFloat("EffectResolution",v);
    instance()->EffectResolution = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeEffectResolution() {
    instance()->handle->RemoveFloat("EffectResolution");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docTemporalAccum() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Keep refining the image while the camera holds still: each further\n"
"frame is shifted by a fraction of a pixel and averaged in, which\n"
"smooths what multisampling cannot (highlights, ambient occlusion,\n"
"outlines). Discarded on any change, so nothing ghosts. Costs GPU time\n"
"while idle, until TemporalAccumSamples frames have been added.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getTemporalAccum() {
    return instance()->TemporalAccum;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultTemporalAccum() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setTemporalAccum(const bool &v) {
    instance()->handle->SetBool("TemporalAccum",v);
    instance()->TemporalAccum = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeTemporalAccum() {
    instance()->handle->RemoveBool("TemporalAccum");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docTemporalAccumSamples() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many frames idle accumulation adds up before the view goes quiet,\n"
"2 to 256. Most of the gain comes in the first few; more gives a\n"
"cleaner still image and takes longer.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getTemporalAccumSamples() {
    return instance()->TemporalAccumSamples;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultTemporalAccumSamples() {
    const static long def = 32;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setTemporalAccumSamples(const long &v) {
    instance()->handle->SetInt("TemporalAccumSamples",v);
    instance()->TemporalAccumSamples = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeTemporalAccumSamples() {
    instance()->handle->RemoveInt("TemporalAccumSamples");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusion() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Skip drawing objects that are completely hidden behind others. The\n"
"image does not change; what is saved is the draw calls. Helps on\n"
"assemblies that hide their own insides, does little for a model that\n"
"is mostly outline. Shadows and reflections of hidden objects are kept.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getOcclusion() {
    return instance()->Occlusion;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultOcclusion() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusion(const bool &v) {
    instance()->handle->SetBool("Occlusion",v);
    instance()->Occlusion = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusion() {
    instance()->handle->RemoveBool("Occlusion");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionVisibleTtl() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many frames a node found visible is believed before it is\n"
"tested again. Higher spends fewer queries and keeps drawing\n"
"geometry that has since become hidden for a little longer; lower\n"
"tracks the camera more closely at the cost of more tests. Purely\n"
"a cost trade -- being late here draws too much, never too\n"
"little, so it cannot affect the image.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionVisibleTtl() {
    return instance()->OcclusionVisibleTtl;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionVisibleTtl() {
    const static long def = 6;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionVisibleTtl(const long &v) {
    instance()->handle->SetInt("OcclusionVisibleTtl",v);
    instance()->OcclusionVisibleTtl = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionVisibleTtl() {
    instance()->handle->RemoveInt("OcclusionVisibleTtl");
}

// Auto generated code (Tools/params_utils.py:397)
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

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionBudget() {
    return instance()->OcclusionBudget;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionBudget() {
    const static long def = 128;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionBudget(const long &v) {
    instance()->handle->SetInt("OcclusionBudget",v);
    instance()->OcclusionBudget = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionBudget() {
    instance()->handle->RemoveInt("OcclusionBudget");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionMinSubtree() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Do not test an index node standing for fewer drawn instances\n"
"than this. A test is itself a draw, so testing a node that could\n"
"save one draw loses whether it answers hidden or visible.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionMinSubtree() {
    return instance()->OcclusionMinSubtree;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionMinSubtree() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionMinSubtree(const long &v) {
    instance()->handle->SetInt("OcclusionMinSubtree",v);
    instance()->OcclusionMinSubtree = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionMinSubtree() {
    instance()->handle->RemoveInt("OcclusionMinSubtree");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionMaxHidden() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With GPU occlusion queries: frames a hidden object may go without a\n"
"new answer before it is drawn again. A safeguard for when answers\n"
"stop arriving.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionMaxHidden() {
    return instance()->OcclusionMaxHidden;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionMaxHidden() {
    const static long def = 120;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionMaxHidden(const long &v) {
    instance()->handle->SetInt("OcclusionMaxHidden",v);
    instance()->OcclusionMaxHidden = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionMaxHidden() {
    instance()->handle->RemoveInt("OcclusionMaxHidden");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionDepthPad() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With GPU occlusion queries: how far a test box is moved towards the\n"
"viewer, in depth buffer steps, so that a part lying flat on a larger\n"
"one is not judged hidden. Too small hides visible parts, too large\n"
"hides less; err high.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionDepthPad() {
    return instance()->OcclusionDepthPad;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionDepthPad() {
    const static long def = 16;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionDepthPad(const long &v) {
    instance()->handle->SetInt("OcclusionDepthPad",v);
    instance()->OcclusionDepthPad = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionDepthPad() {
    instance()->handle->RemoveInt("OcclusionDepthPad");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionConfirm() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With GPU occlusion queries: how many answers of 'hidden' in a row an\n"
"object needs before it is skipped. More reduces flicker and does not\n"
"remove it. Not used when occlusion runs on the CPU, the default.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionConfirm() {
    return instance()->OcclusionConfirm;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionConfirm() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionConfirm(const long &v) {
    instance()->handle->SetInt("OcclusionConfirm",v);
    instance()->OcclusionConfirm = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionConfirm() {
    instance()->handle->RemoveInt("OcclusionConfirm");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionSoftware() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Decide what is hidden with a depth buffer drawn on the CPU instead of\n"
"GPU occlusion queries. The default: its answers belong to the frame\n"
"that asked, where a GPU query answers a frame or two late and can\n"
"flicker. Costs some CPU time per frame.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getOcclusionSoftware() {
    return instance()->OcclusionSoftware;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultOcclusionSoftware() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionSoftware(const bool &v) {
    instance()->handle->SetBool("OcclusionSoftware",v);
    instance()->OcclusionSoftware = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionSoftware() {
    instance()->handle->RemoveBool("OcclusionSoftware");
}

// Auto generated code (Tools/params_utils.py:397)
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

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionOccluderTris() {
    return instance()->OcclusionOccluderTris;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionOccluderTris() {
    const static long def = 250000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionOccluderTris(const long &v) {
    instance()->handle->SetInt("OcclusionOccluderTris",v);
    instance()->OcclusionOccluderTris = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionOccluderTris() {
    instance()->handle->RemoveInt("OcclusionOccluderTris");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionMinOccluder() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How large a draw must appear on screen, in pixels across its\n"
"bounding box diagonal, before it is worth rasterizing into the\n"
"CPU occlusion buffer. Smaller draws can hide almost nothing and\n"
"spend budget that a larger one could use.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionMinOccluder() {
    return instance()->OcclusionMinOccluder;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionMinOccluder() {
    const static long def = 24;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionMinOccluder(const long &v) {
    instance()->handle->SetInt("OcclusionMinOccluder",v);
    instance()->OcclusionMinOccluder = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionMinOccluder() {
    instance()->handle->RemoveInt("OcclusionMinOccluder");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionThreads() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Worker threads the CPU occlusion buffer may draw its occluders on.\n"
"0 chooses automatically.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionThreads() {
    return instance()->OcclusionThreads;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionThreads() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionThreads(const long &v) {
    instance()->handle->SetInt("OcclusionThreads",v);
    instance()->OcclusionThreads = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionThreads() {
    instance()->handle->RemoveInt("OcclusionThreads");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionSimd() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With CPU occlusion, discard triangles too small to cover a pixel four\n"
"at a time before the exact rasterizer sees them. Faster, and it cannot\n"
"hide anything visible. Turn off only to measure what it saves.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getOcclusionSimd() {
    return instance()->OcclusionSimd;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultOcclusionSimd() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionSimd(const bool &v) {
    instance()->handle->SetBool("OcclusionSimd",v);
    instance()->OcclusionSimd = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionSimd() {
    instance()->handle->RemoveBool("OcclusionSimd");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionResolution() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Resolution of the CPU occlusion buffer, as a divisor of the view size.\n"
"1 matches the view. Above 1 it can hide visible geometry; change it\n"
"only to measure.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionResolution() {
    return instance()->OcclusionResolution;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionResolution() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionResolution(const long &v) {
    instance()->handle->SetInt("OcclusionResolution",v);
    instance()->OcclusionResolution = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionResolution() {
    instance()->handle->RemoveInt("OcclusionResolution");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionPerInstance() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With CPU occlusion, test each object on its own and not only the group\n"
"it was sorted into, so one visible object no longer keeps its hidden\n"
"neighbours drawn. Hides more for little cost; on by default.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getOcclusionPerInstance() {
    return instance()->OcclusionPerInstance;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultOcclusionPerInstance() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionPerInstance(const bool &v) {
    instance()->handle->SetBool("OcclusionPerInstance",v);
    instance()->OcclusionPerInstance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionPerInstance() {
    instance()->handle->RemoveBool("OcclusionPerInstance");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionDemoteStreak() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With CPU occlusion: how many frames in a row an object must have been\n"
"completely hidden before its GPU memory may be given back at no\n"
"quality cost. It stays in main memory and returns when seen again.\n"
"0 never does this.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionDemoteStreak() {
    return instance()->OcclusionDemoteStreak;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionDemoteStreak() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionDemoteStreak(const long &v) {
    instance()->handle->SetInt("OcclusionDemoteStreak",v);
    instance()->OcclusionDemoteStreak = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionDemoteStreak() {
    instance()->handle->RemoveInt("OcclusionDemoteStreak");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionCoarse() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With CPU occlusion, draw the occluders from simplified hulls instead\n"
"of their full meshes, so many more of them fit the triangle budget and\n"
"more gets hidden. A hull is moved back by its own error, so it cannot\n"
"hide what is visible.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getOcclusionCoarse() {
    return instance()->OcclusionCoarse;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultOcclusionCoarse() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionCoarse(const bool &v) {
    instance()->handle->SetBool("OcclusionCoarse",v);
    instance()->OcclusionCoarse = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionCoarse() {
    instance()->handle->RemoveBool("OcclusionCoarse");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionCoarseLevel() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Which rung of the decimation ladder an occluder hull is built\n"
"at, coarsest first: the clustering grid is an eighth of the\n"
"mesh's diagonal at 0 and halves per level, so 2 is a\n"
"thirty-second of it. Lower is cheaper to rasterize and further\n"
"from the surface; higher approaches the mesh itself.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionCoarseLevel() {
    return instance()->OcclusionCoarseLevel;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionCoarseLevel() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionCoarseLevel(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseLevel",v);
    instance()->OcclusionCoarseLevel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionCoarseLevel() {
    instance()->handle->RemoveInt("OcclusionCoarseLevel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionCoarseMinTris() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many triangles a draw must carry before it is worth a\n"
"hull. Below this it is rasterized from its mesh: a hull of a\n"
"small mesh saves triangles that were never what spent the\n"
"budget.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionCoarseMinTris() {
    return instance()->OcclusionCoarseMinTris;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionCoarseMinTris() {
    const static long def = 512;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionCoarseMinTris(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseMinTris",v);
    instance()->OcclusionCoarseMinTris = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionCoarseMinTris() {
    instance()->handle->RemoveInt("OcclusionCoarseMinTris");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionCoarseBuilds() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many occluder hulls may be built in one frame. Building is\n"
"parallel but not free, so a scene that has just come into view\n"
"acquires its hulls over several frames rather than stalling one.\n"
"0 freezes the cache at what it already holds.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionCoarseBuilds() {
    return instance()->OcclusionCoarseBuilds;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionCoarseBuilds() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionCoarseBuilds(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseBuilds",v);
    instance()->OcclusionCoarseBuilds = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionCoarseBuilds() {
    instance()->handle->RemoveInt("OcclusionCoarseBuilds");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionCoarseBias() {
    return QT_TRANSLATE_NOOP("RenderParams",
"With coarse occluders: how far a hull is moved away from the camera,\n"
"as a percentage of its own error. 100 guarantees it hides nothing\n"
"visible; less hides more and may hide visible parts.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionCoarseBias() {
    return instance()->OcclusionCoarseBias;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionCoarseBias() {
    const static long def = 100;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionCoarseBias(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseBias",v);
    instance()->OcclusionCoarseBias = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionCoarseBias() {
    instance()->handle->RemoveInt("OcclusionCoarseBias");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionCoarseMemory() {
    return QT_TRANSLATE_NOOP("RenderParams",
"What the occluder hull cache may hold, in megabytes, before\n"
"the least recently used hulls are dropped. A dropped hull costs a\n"
"rebuild when its occluder comes back into view, never\n"
"correctness.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getOcclusionCoarseMemory() {
    return instance()->OcclusionCoarseMemory;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultOcclusionCoarseMemory() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionCoarseMemory(const long &v) {
    instance()->handle->SetInt("OcclusionCoarseMemory",v);
    instance()->OcclusionCoarseMemory = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionCoarseMemory() {
    instance()->handle->RemoveInt("OcclusionCoarseMemory");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docOcclusionBenefitProbe() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Measures whether occlusion culling pays for itself on this\n"
"scene and camera, by turning it on and off for stretches of frames and\n"
"comparing their cost. It disturbs the frames it measures; not for\n"
"normal use.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getOcclusionBenefitProbe() {
    return instance()->OcclusionBenefitProbe;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultOcclusionBenefitProbe() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setOcclusionBenefitProbe(const bool &v) {
    instance()->handle->SetBool("OcclusionBenefitProbe",v);
    instance()->OcclusionBenefitProbe = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeOcclusionBenefitProbe() {
    instance()->handle->RemoveBool("OcclusionBenefitProbe");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docAO() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable screen space ambient occlusion of the experimental render\n"
"engine (render cache mode 3 with a selected renderer type).");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getAO() {
    return instance()->AO;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultAO() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setAO(const bool &v) {
    instance()->handle->SetBool("AO",v);
    instance()->AO = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeAO() {
    instance()->handle->RemoveBool("AO");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docShadow() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Render the shadow map cast by the Shadow display style's scene\n"
"light (and the god-ray shafts / caustic occlusion that depend on\n"
"it). A convenience switch to drop shadows without leaving the\n"
"Shadow display style; the base headlight and environment lighting\n"
"stay, so the scene remains lit, just flatter. Has no effect unless\n"
"the Shadow display style provides a scene light.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getShadow() {
    return instance()->Shadow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultShadow() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setShadow(const bool &v) {
    instance()->handle->SetBool("Shadow",v);
    instance()->Shadow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeShadow() {
    instance()->handle->RemoveBool("Shadow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docAOMethod() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ambient occlusion algorithm. 0 = classic hemisphere-kernel\n"
"SSAO (screen-space depth-difference sampling). 1 = GTAO\n"
"(ground-truth ambient occlusion, XeGTAO-style horizon-based\n"
"visibility integration): physically correct occlusion falloff,\n"
"tight contact shadows without the wide low-contrast wash of\n"
"classic SSAO at large radii.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getAOMethod() {
    return instance()->AOMethod;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultAOMethod() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setAOMethod(const long &v) {
    instance()->handle->SetInt("AOMethod",v);
    instance()->AOMethod = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeAOMethod() {
    instance()->handle->RemoveInt("AOMethod");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docAOSlices() {
    return QT_TRANSLATE_NOOP("RenderParams",
"GTAO only: number of screen-space slice directions per pixel\n"
"(XeGTAO High preset = 9). The dominant quality/cost dial —\n"
"direction variance shows as blotchy grain the denoiser cannot\n"
"fully flatten. Cost scales linearly.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getAOSlices() {
    return instance()->AOSlices;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultAOSlices() {
    const static long def = 9;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setAOSlices(const long &v) {
    instance()->handle->SetInt("AOSlices",v);
    instance()->AOSlices = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeAOSlices() {
    instance()->handle->RemoveInt("AOSlices");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docAOSteps() {
    return QT_TRANSLATE_NOOP("RenderParams",
"GTAO only: horizon-march samples per slice side. More steps\n"
"resolve distant occluders more stably (less mid-frequency blotch\n"
"on grazing surfaces), at linear cost.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getAOSteps() {
    return instance()->AOSteps;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultAOSteps() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setAOSteps(const long &v) {
    instance()->handle->SetInt("AOSteps",v);
    instance()->AOSteps = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeAOSteps() {
    instance()->handle->RemoveInt("AOSteps");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docAORadius() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ambient occlusion sample radius in world units.\n"
"Zero means automatic (a fraction of the scene size).");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getAORadius() {
    return instance()->AORadius;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultAORadius() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setAORadius(const double &v) {
    instance()->handle->SetFloat("AORadius",v);
    instance()->AORadius = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeAORadius() {
    instance()->handle->RemoveFloat("AORadius");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docAOIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ambient occlusion darkening strength.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getAOIntensity() {
    return instance()->AOIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultAOIntensity() {
    const static double def = 0.6;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setAOIntensity(const double &v) {
    instance()->handle->SetFloat("AOIntensity",v);
    instance()->AOIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeAOIntensity() {
    instance()->handle->RemoveFloat("AOIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docAOResolution() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Resolution of ambient occlusion relative to the view, 0.25 to 1,\n"
"independent of EffectResolution. Lower is faster and less sharp.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getAOResolution() {
    return instance()->AOResolution;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultAOResolution() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setAOResolution(const double &v) {
    instance()->handle->SetFloat("AOResolution",v);
    instance()->AOResolution = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeAOResolution() {
    instance()->handle->RemoveFloat("AOResolution");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCavity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Darken creases and ridges of the geometry in screen space, so the\n"
"shape reads without relying on the lighting. Works best with the\n"
"Shaded draw style, where no edges are drawn. Independent of ambient\n"
"occlusion. Needs render cache mode 3 with a renderer selected.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getCavity() {
    return instance()->Cavity;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultCavity() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCavity(const bool &v) {
    instance()->handle->SetBool("Cavity",v);
    instance()->Cavity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCavity() {
    instance()->handle->RemoveBool("Cavity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCavityRadius() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Distance in pixels over which cavity shading measures curvature. 1\n"
"sees only hard creases, sharply. Larger values bring in fillets and\n"
"broad curvature and widen the creases to a band of that width.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getCavityRadius() {
    return instance()->CavityRadius;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultCavityRadius() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCavityRadius(const double &v) {
    instance()->handle->SetFloat("CavityRadius",v);
    instance()->CavityRadius = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCavityRadius() {
    instance()->handle->RemoveFloat("CavityRadius");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCavityValley() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Cavity darkening strength in concave creases (inside corners,\n"
"fillets, pockets). Zero disables the valley term.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getCavityValley() {
    return instance()->CavityValley;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultCavityValley() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCavityValley(const double &v) {
    instance()->handle->SetFloat("CavityValley",v);
    instance()->CavityValley = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCavityValley() {
    instance()->handle->RemoveFloat("CavityValley");
}

// Auto generated code (Tools/params_utils.py:397)
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

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getCavityRidge() {
    return instance()->CavityRidge;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultCavityRidge() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCavityRidge(const double &v) {
    instance()->handle->SetFloat("CavityRidge",v);
    instance()->CavityRidge = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCavityRidge() {
    instance()->handle->RemoveFloat("CavityRidge");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docMatcap() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Shade surfaces by the direction they face the viewer, with a fixed\n"
"studio lighting attached to the camera, so shape reads the same\n"
"wherever the scene light is. Overrides physically based shading.\n"
"Needs render cache mode 3 with a renderer selected.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getMatcap() {
    return instance()->Matcap;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultMatcap() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setMatcap(const bool &v) {
    instance()->handle->SetBool("Matcap",v);
    instance()->Matcap = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeMatcap() {
    instance()->handle->RemoveBool("Matcap");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docMatcapPreset() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Which matcap to shade with, computed in the shader. Studio: soft key\n"
"light with a rim. Clay: matte, the most neutral read of form. Metal:\n"
"banded, exaggerates curvature. Pearl: warm and cool, shows shallow\n"
"undulation. Zebra: black and white stripes that step at an angle, kink\n"
"at a tangent seam and run through where curvature is continuous.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getMatcapPreset() {
    return instance()->MatcapPreset;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultMatcapPreset() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setMatcapPreset(const long &v) {
    instance()->handle->SetInt("MatcapPreset",v);
    instance()->MatcapPreset = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeMatcapPreset() {
    instance()->handle->RemoveInt("MatcapPreset");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docMatcapStripes() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Zebra matcap only: how many dark/light stripe pairs the\n"
"mirrored room has between its two poles. More stripes show a\n"
"smaller change of direction, until they are finer than the\n"
"view can draw -- zoom in rather than raise it without end. The\n"
"stripes are as true as the view mesh: lower the object's\n"
"Deviation before reading a fine pattern.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getMatcapStripes() {
    return instance()->MatcapStripes;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultMatcapStripes() {
    const static long def = 6;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setMatcapStripes(const long &v) {
    instance()->handle->SetInt("MatcapStripes",v);
    instance()->MatcapStripes = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeMatcapStripes() {
    instance()->handle->RemoveInt("MatcapStripes");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docMatcapTint() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How much each object's own color tints the matcap, 0 to 1.\n"
"One multiplies the matcap by the object color, so the matcap\n"
"supplies the shading and the assembly keeps its color coding.\n"
"Zero shades the whole scene as one uniform material instead,\n"
"which drops the color coding but makes shape directly\n"
"comparable across parts.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getMatcapTint() {
    return instance()->MatcapTint;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultMatcapTint() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setMatcapTint(const double &v) {
    instance()->handle->SetFloat("MatcapTint",v);
    instance()->MatcapTint = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeMatcapTint() {
    instance()->handle->RemoveFloat("MatcapTint");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBR() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable physically based shading with image based lighting of\n"
"the experimental render engine (render cache mode 3 with a\n"
"selected renderer type). Replaces the Classic headlight shading\n"
"of lit surfaces with a metallic/roughness material lit by a\n"
"built-in studio environment.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getPBR() {
    return instance()->PBR;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultPBR() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBR(const bool &v) {
    instance()->handle->SetBool("PBR",v);
    instance()->PBR = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBR() {
    instance()->handle->RemoveBool("PBR");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBRMetallic() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Metalness of physically based shaded surfaces, 0 to 1.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getPBRMetallic() {
    return instance()->PBRMetallic;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultPBRMetallic() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBRMetallic(const double &v) {
    instance()->handle->SetFloat("PBRMetallic",v);
    instance()->PBRMetallic = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBRMetallic() {
    instance()->handle->RemoveFloat("PBRMetallic");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBRRoughness() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Roughness of physically based shaded surfaces, 0 to 1.\n"
"Zero means automatic (derived from each material's shininess).");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getPBRRoughness() {
    return instance()->PBRRoughness;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultPBRRoughness() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBRRoughness(const double &v) {
    instance()->handle->SetFloat("PBRRoughness",v);
    instance()->PBRRoughness = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBRRoughness() {
    instance()->handle->RemoveFloat("PBRRoughness");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBRFromSpecular() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Read the specular colour of a classic appearance as metalness when\n"
"the material states none, so that presets such as Gold or Steel look\n"
"like metal under physically based shading. A stated metalness is\n"
"never changed.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getPBRFromSpecular() {
    return instance()->PBRFromSpecular;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultPBRFromSpecular() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBRFromSpecular(const bool &v) {
    instance()->handle->SetBool("PBRFromSpecular",v);
    instance()->PBRFromSpecular = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBRFromSpecular() {
    instance()->handle->RemoveBool("PBRFromSpecular");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docShininessMapping() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How the shininess of a classic appearance becomes a roughness when the\n"
"material states none. 'GL exponent' reads it as the OpenGL exponent,\n"
"where the shiniest material is still satin. 'Full range' reads it as\n"
"0 to 100%, matte to mirror. A stated roughness is never changed.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getShininessMapping() {
    return instance()->ShininessMapping;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultShininessMapping() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setShininessMapping(const long &v) {
    instance()->handle->SetInt("ShininessMapping",v);
    instance()->ShininessMapping = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeShininessMapping() {
    instance()->handle->RemoveInt("ShininessMapping");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBREnvPreset() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Built-in environment that lights the scene when no environment image\n"
"is set: Gradient (the default, the most even), Interior, Studio,\n"
"Overcast, Sunset or Light tent. They differ in contrast and structure,\n"
"not in brightness, so one exposure suits them all.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getPBREnvPreset() {
    return instance()->PBREnvPreset;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultPBREnvPreset() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBREnvPreset(const long &v) {
    instance()->handle->SetInt("PBREnvPreset",v);
    instance()->PBREnvPreset = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBREnvPreset() {
    instance()->handle->RemoveInt("PBREnvPreset");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBREnvIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness of the image based lighting environment.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getPBREnvIntensity() {
    return instance()->PBREnvIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultPBREnvIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBREnvIntensity(const double &v) {
    instance()->handle->SetFloat("PBREnvIntensity",v);
    instance()->PBREnvIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBREnvIntensity() {
    instance()->handle->RemoveFloat("PBREnvIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBREnvImage() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Image file used as the lighting environment instead of the built-in\n"
"one. A 2:1 image is read as a lat-long panorama, anything squarer as a\n"
"sphere map. Radiance files (.hdr, .pic) keep their real brightness and\n"
"are the format to use; OpenEXR is not read. 1K or 2K is plenty. Empty\n"
"uses the Texture mapping dialog's image, then the built-in environment.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & RenderParams::getPBREnvImage() {
    return instance()->PBREnvImage;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & RenderParams::defaultPBREnvImage() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBREnvImage(const std::string &v) {
    instance()->handle->SetASCII("PBREnvImage",v);
    instance()->PBREnvImage = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBREnvImage() {
    instance()->handle->RemoveASCII("PBREnvImage");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBREnvEmbed() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Store a copy of the environment image in the document, so the lighting\n"
"travels with the file instead of depending on a path on one machine.\n"
"The copy takes precedence over the path.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getPBREnvEmbed() {
    return instance()->PBREnvEmbed;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultPBREnvEmbed() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBREnvEmbed(const bool &v) {
    instance()->handle->SetBool("PBREnvEmbed",v);
    instance()->PBREnvEmbed = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBREnvEmbed() {
    instance()->handle->RemoveBool("PBREnvEmbed");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBREnvBackground() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Show the lighting environment as the view background while physically\n"
"based shading is active, so reflections have a visible source. Other\n"
"shading models keep the background gradient.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getPBREnvBackground() {
    return instance()->PBREnvBackground;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultPBREnvBackground() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBREnvBackground(const bool &v) {
    instance()->handle->SetBool("PBREnvBackground",v);
    instance()->PBREnvBackground = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBREnvBackground() {
    instance()->handle->RemoveBool("PBREnvBackground");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docPBREnvBlur() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How far out of focus the environment is drawn as the background, 0 to\n"
"1. 0 is as sharp as it was baked. Only the background is affected:\n"
"lighting and reflections always read the sharp environment.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getPBREnvBlur() {
    return instance()->PBREnvBlur;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultPBREnvBlur() {
    const static double def = 0.25;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setPBREnvBlur(const double &v) {
    instance()->handle->SetFloat("PBREnvBlur",v);
    instance()->PBREnvBlur = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removePBREnvBlur() {
    instance()->handle->RemoveFloat("PBREnvBlur");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docBumpScale() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Strength of bump/normal mapped surfaces (SoBumpMap) of the\n"
"experimental render engine: scales the slope of normal maps and\n"
"the height amplitude of grayscale bump maps.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getBumpScale() {
    return instance()->BumpScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultBumpScale() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setBumpScale(const double &v) {
    instance()->handle->SetFloat("BumpScale",v);
    instance()->BumpScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeBumpScale() {
    instance()->handle->RemoveFloat("BumpScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docParallax() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Parallax-occlusion map grayscale bump maps (SoBumpMap) of the\n"
"experimental render engine, shifting the texture with the view\n"
"angle for a strong relief impression.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getParallax() {
    return instance()->Parallax;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultParallax() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setParallax(const bool &v) {
    instance()->handle->SetBool("Parallax",v);
    instance()->Parallax = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeParallax() {
    instance()->handle->RemoveBool("Parallax");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docVolumetric() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Enable volumetric lighting (light shafts) of the experimental\n"
"render engine: raymarch the shadow map of the Shadow display style\n"
"through a homogeneous scattering medium. Only effective while\n"
"the Shadow display style provides a scene light.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getVolumetric() {
    return instance()->Volumetric;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultVolumetric() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setVolumetric(const bool &v) {
    instance()->handle->SetBool("Volumetric",v);
    instance()->Volumetric = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeVolumetric() {
    instance()->handle->RemoveBool("Volumetric");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docVolumetricIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness of the inscattered (light shaft) light.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getVolumetricIntensity() {
    return instance()->VolumetricIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultVolumetricIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setVolumetricIntensity(const double &v) {
    instance()->handle->SetFloat("VolumetricIntensity",v);
    instance()->VolumetricIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeVolumetricIntensity() {
    instance()->handle->RemoveFloat("VolumetricIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docVolumetricDensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Scattering medium density in inverse world units.\n"
"Zero means automatic (a fraction of the scene size).");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getVolumetricDensity() {
    return instance()->VolumetricDensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultVolumetricDensity() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setVolumetricDensity(const double &v) {
    instance()->handle->SetFloat("VolumetricDensity",v);
    instance()->VolumetricDensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeVolumetricDensity() {
    instance()->handle->RemoveFloat("VolumetricDensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCaustics() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Project an animated caustic light pattern onto surfaces\n"
"below the water body (objects with the Render_Water property),\n"
"modulated by the shadow map. Only effective while volumetric\n"
"lighting and the Shadow display style are active.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getCaustics() {
    return instance()->Caustics;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultCaustics() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCaustics(const bool &v) {
    instance()->handle->SetBool("Caustics",v);
    instance()->Caustics = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCaustics() {
    instance()->handle->RemoveBool("Caustics");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCausticsIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness of the projected caustic pattern.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getCausticsIntensity() {
    return instance()->CausticsIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultCausticsIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCausticsIntensity(const double &v) {
    instance()->handle->SetFloat("CausticsIntensity",v);
    instance()->CausticsIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCausticsIntensity() {
    instance()->handle->RemoveFloat("CausticsIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCausticsScale() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Caustic pattern cell frequency in inverse world units.\n"
"Zero means automatic (a fraction of the water body size).");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getCausticsScale() {
    return instance()->CausticsScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultCausticsScale() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCausticsScale(const double &v) {
    instance()->handle->SetFloat("CausticsScale",v);
    instance()->CausticsScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCausticsScale() {
    instance()->handle->RemoveFloat("CausticsScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCausticsSpeed() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Animation speed of the caustic pattern; zero freezes it.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getCausticsSpeed() {
    return instance()->CausticsSpeed;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultCausticsSpeed() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCausticsSpeed(const double &v) {
    instance()->handle->SetFloat("CausticsSpeed",v);
    instance()->CausticsSpeed = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCausticsSpeed() {
    instance()->handle->RemoveFloat("CausticsSpeed");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterSurface() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Shade water bodies (objects with the Render_Water property)\n"
"as an animated water surface: screen-space refraction of the\n"
"scene behind it, Fresnel-blended environment reflection and a\n"
"sun glint from the Shadow display style light.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getWaterSurface() {
    return instance()->WaterSurface;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultWaterSurface() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterSurface(const bool &v) {
    instance()->handle->SetBool("WaterSurface",v);
    instance()->WaterSurface = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterSurface() {
    instance()->handle->RemoveBool("WaterSurface");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterWaveStrength() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Amplitude of the animated wave perturbation of the water\n"
"surface normal; zero gives a flat mirror-like surface.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getWaterWaveStrength() {
    return instance()->WaterWaveStrength;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultWaterWaveStrength() {
    const static double def = 0.3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterWaveStrength(const double &v) {
    instance()->handle->SetFloat("WaterWaveStrength",v);
    instance()->WaterWaveStrength = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterWaveStrength() {
    instance()->handle->RemoveFloat("WaterWaveStrength");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterWaveScale() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Wave frequency in inverse world units.\n"
"Zero means automatic (a fraction of the water body size).");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getWaterWaveScale() {
    return instance()->WaterWaveScale;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultWaterWaveScale() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterWaveScale(const double &v) {
    instance()->handle->SetFloat("WaterWaveScale",v);
    instance()->WaterWaveScale = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterWaveScale() {
    instance()->handle->RemoveFloat("WaterWaveScale");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterWaveSpeed() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Animation speed of the water surface waves; zero freezes\n"
"them.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getWaterWaveSpeed() {
    return instance()->WaterWaveSpeed;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultWaterWaveSpeed() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterWaveSpeed(const double &v) {
    instance()->handle->SetFloat("WaterWaveSpeed",v);
    instance()->WaterWaveSpeed = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterWaveSpeed() {
    instance()->handle->RemoveFloat("WaterWaveSpeed");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterAbsorption() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Beer-Lambert absorption strength of the water surface\n"
"refraction: the refracted scene is dimmed and tinted by the\n"
"water column it travels through (channels the water color lacks\n"
"are absorbed most), so the water gains body and the bottom\n"
"recedes with depth. Zero = crystal clear.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getWaterAbsorption() {
    return instance()->WaterAbsorption;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultWaterAbsorption() {
    const static double def = 0.2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterAbsorption(const double &v) {
    instance()->handle->SetFloat("WaterAbsorption",v);
    instance()->WaterAbsorption = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterAbsorption() {
    instance()->handle->RemoveFloat("WaterAbsorption");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterInscatter() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How much the water's own color is added back into the\n"
"depth-absorbed refraction (in-scattering); zero leaves absorbed\n"
"regions dark, one fills them with the water color.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getWaterInscatter() {
    return instance()->WaterInscatter;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultWaterInscatter() {
    const static double def = 0.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterInscatter(const double &v) {
    instance()->handle->SetFloat("WaterInscatter",v);
    instance()->WaterInscatter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterInscatter() {
    instance()->handle->RemoveFloat("WaterInscatter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterRefraction() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Screen-space refraction of the scene behind the water\n"
"surface. When off the surface shows a flat water colour instead\n"
"of the see-through refracted scene.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getWaterRefraction() {
    return instance()->WaterRefraction;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultWaterRefraction() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterRefraction(const bool &v) {
    instance()->handle->SetBool("WaterRefraction",v);
    instance()->WaterRefraction = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterRefraction() {
    instance()->handle->RemoveBool("WaterRefraction");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterReflection() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Reflection on the water surface (Fresnel-blended). When off\n"
"the surface only refracts. See WaterPlanarReflection for the\n"
"reflection method.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getWaterReflection() {
    return instance()->WaterReflection;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultWaterReflection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterReflection(const bool &v) {
    instance()->handle->SetBool("WaterReflection",v);
    instance()->WaterReflection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterReflection() {
    instance()->handle->RemoveBool("WaterReflection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterPlanarReflection() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Reflection method when WaterReflection is on: planar (a\n"
"mirror-camera re-render of the scene about the water plane -\n"
"exact, no taper) when true, else screen-space reflection (a\n"
"cheaper per-pixel ray march that can only reflect on-screen\n"
"geometry and tapers past it). The environment cubemap is the\n"
"fallback for both.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getWaterPlanarReflection() {
    return instance()->WaterPlanarReflection;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultWaterPlanarReflection() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterPlanarReflection(const bool &v) {
    instance()->handle->SetBool("WaterPlanarReflection",v);
    instance()->WaterPlanarReflection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterPlanarReflection() {
    instance()->handle->RemoveBool("WaterPlanarReflection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterShadow() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Receive the scene light's shadow on the water surface: a\n"
"shadow band on the water where a caster blocks the light and\n"
"the sun glint killed there. Requires the Shadow display style\n"
"with an active shadow map; off leaves the surface fully lit.\n"
"The refracted scene below the surface keeps its own shadow\n"
"regardless.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getWaterShadow() {
    return instance()->WaterShadow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultWaterShadow() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterShadow(const bool &v) {
    instance()->handle->SetBool("WaterShadow",v);
    instance()->WaterShadow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterShadow() {
    instance()->handle->RemoveBool("WaterShadow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterRippleType() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Ripples the water surface has by itself: 0 wind waves, 1 rain rings,\n"
"2 none. Rings from fountains and from particles striking the water\n"
"show in every case.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getWaterRippleType() {
    return instance()->WaterRippleType;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultWaterRippleType() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterRippleType(const long &v) {
    instance()->handle->SetInt("WaterRippleType",v);
    instance()->WaterRippleType = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterRippleType() {
    instance()->handle->RemoveInt("WaterRippleType");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterRippleDensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Drop density of the rain ripple type: how many drop cells\n"
"fit per wave-scale unit. Higher rains harder - more, smaller\n"
"rings; lower gives sparse large rings. The wave ripple type\n"
"ignores it.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getWaterRippleDensity() {
    return instance()->WaterRippleDensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultWaterRippleDensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterRippleDensity(const double &v) {
    instance()->handle->SetFloat("WaterRippleDensity",v);
    instance()->WaterRippleDensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterRippleDensity() {
    instance()->handle->RemoveFloat("WaterRippleDensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterImpactStrength() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Height of the rings raised where particles actually strike\n"
"the water - a fountain's droplets landing in its own basin.\n"
"Unlike the rain ripple type these are not a pattern: nothing\n"
"appears unless something hits the surface, and it appears\n"
"where it hit. Zero turns them off. Needs a stateful emitter\n"
"whose step program reports its impacts.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getWaterImpactStrength() {
    return instance()->WaterImpactStrength;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultWaterImpactStrength() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterImpactStrength(const double &v) {
    instance()->handle->SetFloat("WaterImpactStrength",v);
    instance()->WaterImpactStrength = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterImpactStrength() {
    instance()->handle->RemoveFloat("WaterImpactStrength");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterImpactLife() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How long an impact ring lives, in seconds - which is also\n"
"how far it travels, since a ring is sized to have crossed two\n"
"cells of the impact map when it dies. Longer makes slower,\n"
"wider-travelling rings out of the same hits.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getWaterImpactLife() {
    return instance()->WaterImpactLife;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultWaterImpactLife() {
    const static double def = 1.1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterImpactLife(const double &v) {
    instance()->handle->SetFloat("WaterImpactLife",v);
    instance()->WaterImpactLife = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterImpactLife() {
    instance()->handle->RemoveFloat("WaterImpactLife");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docWaterShadowWobble() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How much the shadow band on the water surface wobbles with\n"
"the wave field: the shadow is looked up at the wave-displaced\n"
"surface point scaled by this factor. Zero pins the shadow\n"
"boundary to the flat surface (a straight edge), one is the\n"
"physical wave height, larger values exaggerate the ripple.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getWaterShadowWobble() {
    return instance()->WaterShadowWobble;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultWaterShadowWobble() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setWaterShadowWobble(const double &v) {
    instance()->handle->SetFloat("WaterShadowWobble",v);
    instance()->WaterShadowWobble = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeWaterShadowWobble() {
    instance()->handle->RemoveFloat("WaterShadowWobble");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docBloom() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Bleed a blurred glow halo from bright pixels and from\n"
"light-source bodies (objects with the Render_Light property)\n"
"over their surroundings.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getBloom() {
    return instance()->Bloom;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultBloom() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setBloom(const bool &v) {
    instance()->handle->SetBool("Bloom",v);
    instance()->Bloom = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeBloom() {
    instance()->handle->RemoveBool("Bloom");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docBloomThreshold() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Scene brightness above which a pixel feeds the glow halo\n"
"(with a soft knee below it). Light-source bodies always feed\n"
"it regardless, scaled by their intensity.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getBloomThreshold() {
    return instance()->BloomThreshold;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultBloomThreshold() {
    const static double def = 0.9;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setBloomThreshold(const double &v) {
    instance()->handle->SetFloat("BloomThreshold",v);
    instance()->BloomThreshold = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeBloomThreshold() {
    instance()->handle->RemoveFloat("BloomThreshold");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docBloomIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness multiplier of the composited glow halo.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getBloomIntensity() {
    return instance()->BloomIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultBloomIntensity() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setBloomIntensity(const double &v) {
    instance()->handle->SetFloat("BloomIntensity",v);
    instance()->BloomIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeBloomIntensity() {
    instance()->handle->RemoveFloat("BloomIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docBloomRadius() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Radius scale of the glow halo. One is the default gaussian\n"
"footprint; larger blooms wider.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getBloomRadius() {
    return instance()->BloomRadius;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultBloomRadius() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setBloomRadius(const double &v) {
    instance()->handle->SetFloat("BloomRadius",v);
    instance()->BloomRadius = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeBloomRadius() {
    instance()->handle->RemoveFloat("BloomRadius");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLight() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Let the render engine use a scene light of its own, described by the\n"
"Light settings below, for shadows, light shafts and ground reflection.\n"
"Without it the only such light is the one the Shadow display style\n"
"adds. A light found in the scene still takes precedence.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getLight() {
    return instance()->Light;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultLight() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLight(const bool &v) {
    instance()->handle->SetBool("Light",v);
    instance()->Light = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLight() {
    instance()->handle->RemoveBool("Light");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Brightness of the renderer's own scene light.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLightIntensity() {
    return instance()->LightIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLightIntensity() {
    const static double def = 0.8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightIntensity(const double &v) {
    instance()->handle->SetFloat("LightIntensity",v);
    instance()->LightIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightIntensity() {
    instance()->handle->RemoveFloat("LightIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightDirectionX() {
    return QT_TRANSLATE_NOOP("RenderParams",
"X component of the direction the render engine's own scene light\n"
"shines along, in world coordinates. A direction of zero length\n"
"falls back to (-1, -1, -1).");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLightDirectionX() {
    return instance()->LightDirectionX;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLightDirectionX() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightDirectionX(const double &v) {
    instance()->handle->SetFloat("LightDirectionX",v);
    instance()->LightDirectionX = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightDirectionX() {
    instance()->handle->RemoveFloat("LightDirectionX");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightDirectionY() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Y component of the direction the render engine's own scene light\n"
"shines along, in world coordinates. A direction of zero length\n"
"falls back to (-1, -1, -1).");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLightDirectionY() {
    return instance()->LightDirectionY;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLightDirectionY() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightDirectionY(const double &v) {
    instance()->handle->SetFloat("LightDirectionY",v);
    instance()->LightDirectionY = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightDirectionY() {
    instance()->handle->RemoveFloat("LightDirectionY");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightDirectionZ() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Z component of the direction the render engine's own scene light\n"
"shines along, in world coordinates. A direction of zero length\n"
"falls back to (-1, -1, -1).");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLightDirectionZ() {
    return instance()->LightDirectionZ;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLightDirectionZ() {
    const static double def = -1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightDirectionZ(const double &v) {
    instance()->handle->SetFloat("LightDirectionZ",v);
    instance()->LightDirectionZ = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightDirectionZ() {
    instance()->handle->RemoveFloat("LightDirectionZ");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightColor() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Colour of the renderer's own scene light.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & RenderParams::getLightColor() {
    return instance()->LightColor;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & RenderParams::defaultLightColor() {
    const static unsigned long def = 0xF0FDFFFF;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightColor(const unsigned long &v) {
    instance()->handle->SetUnsigned("LightColor",v);
    instance()->LightColor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightColor() {
    instance()->handle->RemoveUnsigned("LightColor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightSpot() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Make the renderer's own light a spot rather than a directional\n"
"one. A spot has a position and a cone; a directional light has\n"
"only a direction.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getLightSpot() {
    return instance()->LightSpot;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultLightSpot() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightSpot(const bool &v) {
    instance()->handle->SetBool("LightSpot",v);
    instance()->LightSpot = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightSpot() {
    instance()->handle->RemoveBool("LightSpot");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightPositionX() {
    return QT_TRANSLATE_NOOP("RenderParams",
"X coordinate of the render engine's own scene light when it is a\n"
"spot light, in world coordinates. A directional light ignores it.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLightPositionX() {
    return instance()->LightPositionX;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLightPositionX() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightPositionX(const double &v) {
    instance()->handle->SetFloat("LightPositionX",v);
    instance()->LightPositionX = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightPositionX() {
    instance()->handle->RemoveFloat("LightPositionX");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightPositionY() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Y coordinate of the render engine's own scene light when it is a\n"
"spot light, in world coordinates. A directional light ignores it.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLightPositionY() {
    return instance()->LightPositionY;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLightPositionY() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightPositionY(const double &v) {
    instance()->handle->SetFloat("LightPositionY",v);
    instance()->LightPositionY = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightPositionY() {
    instance()->handle->RemoveFloat("LightPositionY");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightPositionZ() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Z coordinate of the render engine's own scene light when it is a\n"
"spot light, in world coordinates. A directional light ignores it.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLightPositionZ() {
    return instance()->LightPositionZ;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLightPositionZ() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightPositionZ(const double &v) {
    instance()->handle->SetFloat("LightPositionZ",v);
    instance()->LightPositionZ = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightPositionZ() {
    instance()->handle->RemoveFloat("LightPositionZ");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightCutOffAngle() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Half angle of the spot cone, in degrees.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLightCutOffAngle() {
    return instance()->LightCutOffAngle;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLightCutOffAngle() {
    const static double def = 45.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightCutOffAngle(const double &v) {
    instance()->handle->SetFloat("LightCutOffAngle",v);
    instance()->LightCutOffAngle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightCutOffAngle() {
    instance()->handle->RemoveFloat("LightCutOffAngle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docLightDropOffRate() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How sharply a spot falls off from the cone axis. Zero is even\n"
"across the cone.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getLightDropOffRate() {
    return instance()->LightDropOffRate;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultLightDropOffRate() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setLightDropOffRate(const double &v) {
    instance()->handle->SetFloat("LightDropOffRate",v);
    instance()->LightDropOffRate = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeLightDropOffRate() {
    instance()->handle->RemoveFloat("LightDropOffRate");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docSunDisc() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Draw a visible sun -- a bright disc with a limb glow -- in\n"
"the sky along the Shadow display style's directional scene light,\n"
"occluded by geometry and feeding the bloom glow. Perspective\n"
"cameras only; spot lights have no sky direction.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getSunDisc() {
    return instance()->SunDisc;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultSunDisc() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setSunDisc(const bool &v) {
    instance()->handle->SetBool("SunDisc",v);
    instance()->SunDisc = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeSunDisc() {
    instance()->handle->RemoveBool("SunDisc");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docSunDiscSize() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Angular radius of the sun disc in degrees (the real sun is\n"
"about 0.27; larger reads better in a CAD scene).");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getSunDiscSize() {
    return instance()->SunDiscSize;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultSunDiscSize() {
    const static double def = 1.5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setSunDiscSize(const double &v) {
    instance()->handle->SetFloat("SunDiscSize",v);
    instance()->SunDiscSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeSunDiscSize() {
    instance()->handle->RemoveFloat("SunDiscSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docGroundReflection() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Mirror the model in the ground plane of the experimental\n"
"render engine: the opaque scene is re-rendered with a reflected\n"
"camera and blended onto the ground. Brings the ground plane out\n"
"on its own -- neither the Shadow display style nor its ground\n"
"switch is needed -- and the ground keeps its own appearance\n"
"settings (color, size, texture) from the shadow group.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getGroundReflection() {
    return instance()->GroundReflection;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultGroundReflection() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setGroundReflection(const bool &v) {
    instance()->handle->SetBool("GroundReflection",v);
    instance()->GroundReflection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeGroundReflection() {
    instance()->handle->RemoveBool("GroundReflection");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docGroundReflectionIntensity() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Blend factor of the mirrored model on the ground plane.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getGroundReflectionIntensity() {
    return instance()->GroundReflectionIntensity;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultGroundReflectionIntensity() {
    const static double def = 0.4;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setGroundReflectionIntensity(const double &v) {
    instance()->handle->SetFloat("GroundReflectionIntensity",v);
    instance()->GroundReflectionIntensity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeGroundReflectionIntensity() {
    instance()->handle->RemoveFloat("GroundReflectionIntensity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCyclesDevice() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Device the path tracer runs on: 'CPU' always works; 'CUDA', 'OPTIX' or\n"
"'HIP' when the machine has the GPU and driver. A document saved with a\n"
"device this machine lacks uses the first one available.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & RenderParams::getCyclesDevice() {
    return instance()->CyclesDevice;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & RenderParams::defaultCyclesDevice() {
    const static std::string def = "CPU";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCyclesDevice(const std::string &v) {
    instance()->handle->SetASCII("CyclesDevice",v);
    instance()->CyclesDevice = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCyclesDevice() {
    instance()->handle->RemoveASCII("CyclesDevice");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCyclesSamples() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Samples per pixel the External shading model refines to\n"
"before it rests. More is cleaner and slower to settle; the view\n"
"stays interactive either way, restarting from one sample on\n"
"every camera move.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getCyclesSamples() {
    return instance()->CyclesSamples;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultCyclesSamples() {
    const static long def = 256;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCyclesSamples(const long &v) {
    instance()->handle->SetInt("CyclesSamples",v);
    instance()->CyclesSamples = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCyclesSamples() {
    instance()->handle->RemoveInt("CyclesSamples");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCyclesTimeLimit() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Seconds the External shading model may refine after each\n"
"change before it rests, whatever the sample budget still says.\n"
"0 means no limit: the sample count alone decides.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & RenderParams::getCyclesTimeLimit() {
    return instance()->CyclesTimeLimit;
}

// Auto generated code (Tools/params_utils.py:413)
const double & RenderParams::defaultCyclesTimeLimit() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCyclesTimeLimit(const double &v) {
    instance()->handle->SetFloat("CyclesTimeLimit",v);
    instance()->CyclesTimeLimit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCyclesTimeLimit() {
    instance()->handle->RemoveFloat("CyclesTimeLimit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCyclesDenoise() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Run OpenImageDenoise over the refining External shading\n"
"frame, trading the raw noise of the early samples for a smooth\n"
"image that sharpens as samples arrive.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getCyclesDenoise() {
    return instance()->CyclesDenoise;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultCyclesDenoise() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCyclesDenoise(const bool &v) {
    instance()->handle->SetBool("CyclesDenoise",v);
    instance()->CyclesDenoise = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCyclesDenoise() {
    instance()->handle->RemoveBool("CyclesDenoise");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCyclesPixelSize() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Render the External shading model at 1/n resolution and\n"
"scale up -- Blender's preview pixel size. 2 or 4 keeps a large\n"
"view fluid on a weak device at the cost of a blockier preview.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getCyclesPixelSize() {
    return instance()->CyclesPixelSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultCyclesPixelSize() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCyclesPixelSize(const long &v) {
    instance()->handle->SetInt("CyclesPixelSize",v);
    instance()->CyclesPixelSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCyclesPixelSize() {
    instance()->handle->RemoveInt("CyclesPixelSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docCyclesMaxStreams() {
    return QT_TRANSLATE_NOOP("RenderParams",
"How many path-traced views this process serves to browser viewers at\n"
"once. A request past the limit is refused and that viewer stays on its\n"
"raster view. 0 or less means no limit. Desktop views are not counted.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getCyclesMaxStreams() {
    return instance()->CyclesMaxStreams;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultCyclesMaxStreams() {
    const static long def = 4;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setCyclesMaxStreams(const long &v) {
    instance()->handle->SetInt("CyclesMaxStreams",v);
    instance()->CyclesMaxStreams = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeCyclesMaxStreams() {
    instance()->handle->RemoveInt("CyclesMaxStreams");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugViewMode() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Shows an intermediate buffer instead of the shaded scene:\n"
"1 depth, 2 normals, 3 ambient occlusion, 4 shadow, 5 shadow map\n"
"coverage, 6 overdraw, 7 shadow precision, 8 texture coordinates,\n"
"9 reflection target, 10 particle impact map. 0 renders normally.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & RenderParams::getDebugViewMode() {
    return instance()->DebugViewMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & RenderParams::defaultDebugViewMode() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugViewMode(const long &v) {
    instance()->handle->SetInt("DebugViewMode",v);
    instance()->DebugViewMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugViewMode() {
    instance()->handle->RemoveInt("DebugViewMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugFreezeFrame() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Freeze every intentionally time- or history-dependent render\n"
"input: temporal accumulation and per-frame sampling jitter, and\n"
"time-driven animation (water waves, fire). Two frames of the same\n"
"scene, camera and parameters then render identically -- the\n"
"determinism switch for golden-image comparison\n"
"(docs/RenderDebug.md).");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugFreezeFrame() {
    return instance()->DebugFreezeFrame;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugFreezeFrame() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugFreezeFrame(const bool &v) {
    instance()->handle->SetBool("DebugFreezeFrame",v);
    instance()->DebugFreezeFrame = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugFreezeFrame() {
    instance()->handle->RemoveBool("DebugFreezeFrame");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugLabel() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Burn a self-describing label into a corner of the rendered\n"
"frame while render debugging: the active debug view mode, the\n"
"freeze-frame state and any custom RenderDebug_* parameter values.\n"
"A captured PNG then documents its own settings without its\n"
"sidecar (docs/RenderDebug.md).");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugLabel() {
    return instance()->DebugLabel;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugLabel() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugLabel(const bool &v) {
    instance()->handle->SetBool("DebugLabel",v);
    instance()->DebugLabel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugLabel() {
    instance()->handle->RemoveBool("DebugLabel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugTiming() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Logs once a second where the time of a rendered frame\n"
"goes, by pipeline stage, and what the frame costs on the CPU against\n"
"the GPU.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugTiming() {
    return instance()->DebugTiming;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugTiming() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugTiming(const bool &v) {
    instance()->handle->SetBool("DebugTiming",v);
    instance()->DebugTiming = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugTiming() {
    instance()->handle->RemoveBool("DebugTiming");
}

// Auto generated code (Tools/params_utils.py:397)
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

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugDelta() {
    return instance()->DebugDelta;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugDelta() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugDelta(const bool &v) {
    instance()->handle->SetBool("DebugDelta",v);
    instance()->DebugDelta = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugDelta() {
    instance()->handle->RemoveBool("DebugDelta");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugCoverage() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Logs how much of the screen each drawn object covers,\n"
"as a histogram over its size in pixels. Shows how much of a model is\n"
"drawn only a few pixels large.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugCoverage() {
    return instance()->DebugCoverage;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugCoverage() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugCoverage(const bool &v) {
    instance()->handle->SetBool("DebugCoverage",v);
    instance()->DebugCoverage = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugCoverage() {
    instance()->handle->RemoveBool("DebugCoverage");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugProxyCut() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Logs how many draw calls replacing distant parts by\n"
"far-field proxies would save for the current camera, without building\n"
"any.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugProxyCut() {
    return instance()->DebugProxyCut;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugProxyCut() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugProxyCut(const bool &v) {
    instance()->handle->SetBool("DebugProxyCut",v);
    instance()->DebugProxyCut = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugProxyCut() {
    instance()->handle->RemoveBool("DebugProxyCut");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugOcclusion() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Measures how much of what a frame draws is hidden behind\n"
"something else, using GPU occlusion queries. A large model takes\n"
"several frames per report.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugOcclusion() {
    return instance()->DebugOcclusion;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugOcclusion() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugOcclusion(const bool &v) {
    instance()->handle->SetBool("DebugOcclusion",v);
    instance()->DebugOcclusion = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugOcclusion() {
    instance()->handle->RemoveBool("DebugOcclusion");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugProxyGen() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Builds real far-field proxies for a sample of nodes and\n"
"reports their triangle cost and the error they introduce. Expensive:\n"
"it builds meshes.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugProxyGen() {
    return instance()->DebugProxyGen;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugProxyGen() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugProxyGen(const bool &v) {
    instance()->handle->SetBool("DebugProxyGen",v);
    instance()->DebugProxyGen = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugProxyGen() {
    instance()->handle->RemoveBool("DebugProxyGen");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugCullAudit() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Checks what occlusion culling skipped against what really\n"
"reaches the screen, by drawing every object once more with its\n"
"identity as its colour, and reports objects culled by mistake. Reads\n"
"the image back once a second. Not available on WebGL2.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugCullAudit() {
    return instance()->DebugCullAudit;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugCullAudit() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugCullAudit(const bool &v) {
    instance()->handle->SetBool("DebugCullAudit",v);
    instance()->DebugCullAudit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void RenderParams::removeDebugCullAudit() {
    instance()->handle->RemoveBool("DebugCullAudit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *RenderParams::docDebugCullBounds() {
    return QT_TRANSLATE_NOOP("RenderParams",
"Diagnostic. Measures whether tighter bounds around objects would let\n"
"occlusion culling hide more, by asking again about every object still\n"
"drawn in three ways. Reports only, changes nothing on screen. Needs\n"
"the cull audit and CPU occlusion, and is far too slow to leave on.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & RenderParams::getDebugCullBounds() {
    return instance()->DebugCullBounds;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & RenderParams::defaultDebugCullBounds() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void RenderParams::setDebugCullBounds(const bool &v) {
    instance()->handle->SetBool("DebugCullBounds",v);
    instance()->DebugCullBounds = v;
}

// Auto generated code (Tools/params_utils.py:431)
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
        // The type alone says whether the engine draws, and with it
        // which render cache mode the program goes by (renderCache()):
        // from "Legacy" with another mode stored to the engine, or back,
        // the mode in force changes though its setting did not. Those
        // who follow the mode are told as if it had; they ask
        // renderCache() for it.
        App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/View")->Notify("RenderCache");
        // Then the backend on every 3D view: what the type means, not
        // what it reads.
        const std::string type = renderCache() == 3 ? engineType() : std::string();
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

bool RenderParams::usesEngine()
{
    return getType() != legacyType();
}

std::string RenderParams::engineType()
{
    const std::string &type = getType();
    if (type == legacyType())
        return std::string();
    if (!type.empty() && type != "Default") {
        for (const auto &t : Render::RendererFactory::types()) {
            if (t == type)
                return type;
        }
    }
    const std::string preferred = preferredType();
    return preferred == "Default" ? std::string() : preferred;
}

int RenderParams::renderCache()
{
    // Render cache 3 is what feeds the render engine, so with the engine
    // it is the path whether or not a backend comes up: with one, the
    // backend draws; without one, the cache's own GL renderer does, and
    // a failure at any stage falls back to that by itself (a backend
    // that cannot be created, a shader pack that will not load, and a
    // frame that returns false all leave canSkipInternal() false).
    const std::string type = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View/Render")
                ->GetASCII("Type", defaultType().c_str());
    if (type != legacyType())
        return 3;
    return int(App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View")
                ->GetInt("RenderCache", ViewParams::defaultRenderCache()));
}

std::vector<std::string> RenderParams::backendTypes()
{
    return Render::RendererFactory::types();
}

std::string RenderParams::preferredType()
{
    // The backend: the engine's own where this build has it, and
    // whatever else registered if not. Resolved against what is
    // actually registered rather than named by a literal, so a build
    // without the engine says "Default" instead of asking for a type
    // nobody can create, and a stored name from another build cannot
    // survive into this one.
    //
    // Among the engine's own backends the list below decides, most
    // preferred first. It has to be said here because the registered
    // names arrive alphabetically -- RendererFactory::types() walks a
    // std::map -- and an alphabetical accident is not a choice.
    //
    // Windows leads with Direct3D 11, and on measurement rather than
    // taste: docs/RenderEngine.md 7.10 ("The composite priced") prices
    // the readback composite at +2.17 ms a frame on it against +7.09 on
    // Direct3D 12 and +7.61 on Vulkan, which puts it 8% ahead of both
    // while submission itself is a three-way tie -- and all three spend
    // about an eighth of what OpenGL spends to issue the same draws.
    //
    // macOS leads with Metal because nothing else can run this renderer
    // there at all (Apple caps the compatibility profile Coin needs at
    // GL 2.1). Everywhere else OpenGL still leads: Vulkan is registered
    // only when asked for, because no one has yet looked at a session on
    // it, and an unverified default is a worse answer than an opt-in.
    // See BGFXRendererLibP's constructor, which is where each backend's
    // availability is decided.
    //
    // A name that did not register is skipped, so this is a preference
    // and not a requirement: a benchmark leg that registers one backend
    // and names it through this group's Type key still gets it.
    static const char *const order[] = {
#if defined(FC_OS_WIN32)
        "bgfx - Direct3D11",
        "bgfx - Direct3D12",
        "bgfx - Vulkan",
        "bgfx - OpenGL",
#elif defined(FC_OS_MACOSX)
        "bgfx - Metal",
        "bgfx - OpenGL",
#else
        "bgfx - OpenGL",
        "bgfx - Vulkan",
#endif
    };
    const std::vector<std::string> types = Render::RendererFactory::types();
    for (const char *name : order) {
        for (const auto &t : types) {
            if (t == name)
                return t;
        }
    }
    // Anything the list does not name -- a backend added to the engine
    // and not to it, or another engine's type entirely.
    std::string type;
    for (const auto &t : types) {
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
