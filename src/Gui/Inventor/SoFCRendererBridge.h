/****************************************************************************
 *   Copyright (c) 2026 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef GUI_SOFCRENDERERBRIDGE_H
#define GUI_SOFCRENDERERBRIDGE_H

#include "SoFCRenderCache.h"
#include "../Renderer/Renderer.h"

namespace App {
class Property;
}

namespace App {
class PropertyContainer;
}

namespace Gui {

class View3DInventor;

namespace RendererBridge {

/// Whether the draws being translated take the section clip planes when
/// they are drawn on top, and whether the section is concave. Passed in
/// rather than read from ViewParams inside, because these are the view's
/// own Section_NoOnTop/Section_Concave style where it carries one
/// (docs/ViewSettings.md 6) -- and because a translated draw carries the
/// answer with it: unlike the per-frame configs it is baked in here, so a
/// change of either key has to re-translate (View3DInventorViewer::
/// refreshRenderCache), not merely redraw.
struct SectionOnTop {
    bool noOnTop = true;
    bool concave = false;
};

/// Translate a flattened Coin vertex cache map (the SoFCRenderer feed
/// format) into backend-neutral draw calls for a Render::Renderer.
/// The returned draw calls keep the underlying SoFCVertexCache data alive
/// through MeshData::owner, so they may outlive the input map.
///
/// \a selId (a SoFCRenderer::SelIdBits selection id, 0 = scene feed) and
/// \a highlight (preselection feed) give the feed context needed to apply
/// the GL renderer's selection line/point thickening
/// (ViewParams::SelectionLineThicken etc., applyMaterial's
/// RenderPassHighlight handling) to the translated materials.
/// \a sequentialOrder returns the draws sorted by vertex-cache id —
/// ascending creation order, which follows the scene-graph traversal
/// order. Overlay feeds need it: they render in a Sequential view where
/// submission order is blending order (the material-keyed cache map
/// itself has no traversal order).
/// \a objectInfo, when given, collects the document identity of every
/// named draw (docs/ThinClient.md §4.1): the cache key's captured origin,
/// which carries the document and object internal names and is read
/// straight off the key -- nothing here resolves a document. One entry
/// per distinct objectKey; keys whose chain never crossed a ViewProvider
/// stay absent. The label and type a viewer shows are presentation, and
/// come from the ObjectMetaMap instead (Render::Renderer::setObjectMeta).
///
/// \a objectInfo may be a map kept across publishes: an identity is
/// fixed, so an entry that is already there is already right and is left
/// untouched. \a addedInfo, when given as well, collects just the keys
/// this call had to add, which is what a resident renderer needs told
/// (Render::Renderer::updateObjectInfo) instead of the whole table.
GuiExport Render::DrawCallList translate(
        const SoFCRenderCache::VertexCacheMap & vcachemap,
        const SectionOnTop & sectionOnTop,
        int selId = 0, bool highlight = false,
        bool sequentialOrder = false,
        Render::ObjectInfoMap * objectInfo = nullptr,
        Render::ObjectInfoMap * addedInfo = nullptr);

/// Resolve the hidden-line draw style state from the traversal state
/// (SoFCDisplayModeElement) into the backend-neutral per-frame config.
GuiExport Render::HiddenLineConfig translateHiddenLineConfig(SoState * state);

/// Resolve the section fill (cap) settings into the backend-neutral
/// per-frame config: the view's Section_* overrides where it has them,
/// and the ViewParams preferences behind them.
GuiExport Render::SectionConfig translateSectionConfig(App::PropertyContainer * view);

/// Resolve the ambient occlusion settings into the backend-neutral
/// per-frame config: the view's Render_SSAO* dynamic properties when
/// present (see View3DInventorViewer::setRendererType, which materializes
/// them like the Shadow draw style's Shadow_* properties), with the
/// global RenderParams as fallback.
GuiExport Render::AOConfig translateAOConfig(App::PropertyContainer * view);
GuiExport Render::CavityConfig translateCavityConfig(App::PropertyContainer * view);
GuiExport Render::MatcapConfig translateMatcapConfig(App::PropertyContainer * view);
GuiExport Render::RenderDebugConfig translateRenderDebugConfig(App::PropertyContainer * view);
GuiExport Render::OcclusionCullConfig translateOcclusionCullConfig(App::PropertyContainer * view);

/// Extract a property value as floats zero-padded to vec4 lanes — the
/// dynamic-property shader parameter protocol (docs/RenderDebug.md §2.5,
/// §6.4) shared by the RenderDebug_* view properties, App::ShaderProgram
/// parameter properties and App::ShaderBinding per-binding overrides.
/// Returns false (values untouched) for unsupported property types.
GuiExport bool translateShaderParamValues(const App::Property * prop,
                                          std::vector<float> & values);

/// Uniform name of a Group_Name shader parameter property (§6.4): the
/// part after the first '_' (the group prefix used for property-editor
/// grouping is dropped), prefixed with "u_" unless already so —
/// "Param_Tint" → "u_Tint", "RenderDebug_userParams" → "u_userParams".
GuiExport std::string shaderParamUniformName(const char * propName);

/// Capture one scene SoShaderProgram node into a user shader entry
/// (docs/RenderDebug.md §6): stage name, bgfx .sc sources (inline
/// BGFX_SC, or FILENAME with a .sc suffix — read here), and the
/// attached SoShaderParameter values packed into vec4 lanes. Returns
/// false when the node is inactive or carries no consumable source.
GuiExport bool translateShaderProgram(const SoNode * node,
                                      Render::UserShader & out);

/// Resolve the physically based shading settings (Render_PBR* view
/// properties, RenderParams fallback) into the backend-neutral per-frame
/// config.
GuiExport Render::PBRConfig translatePBRConfig(App::PropertyContainer * view);

/// Resolve the volumetric lighting settings (Render_Volumetric* view
/// properties, RenderParams fallback) into the backend-neutral per-frame
/// config.
GuiExport Render::VolumetricConfig translateVolumetricConfig(
        App::PropertyContainer * view);

/// Resolve the water surface settings (Render_WaterSurface/WaterWave*
/// view properties, RenderParams fallback) into the backend-neutral
/// per-frame config.
GuiExport Render::WaterConfig translateWaterConfig(App::PropertyContainer * view);

/// Resolve the bloom (glow) settings (Render_Bloom* view properties,
/// RenderParams fallback) into the backend-neutral per-frame config.
GuiExport Render::BloomConfig translateBloomConfig(App::PropertyContainer * view);
/// Resolve the idle temporal accumulation settings (Render_TemporalAccum,
/// Render_TemporalAccumSamples) from the view's overrides or RenderParams.
GuiExport Render::TemporalConfig translateTemporalConfig(App::PropertyContainer * view);
GuiExport Render::OutputConfig translateOutputConfig(App::PropertyContainer * view);

/// Resolve the preselection highlight styling (HighlightColor,
/// ShowPreSelectedFaceOutline, NoPreSelFaceHighlightWithOutline, outline
/// width from the selection/outline thicken params) into the backend-neutral
/// config the standalone/WASM viewer reads for its local hover highlight.
GuiExport Render::PreselHighlightConfig translatePreselConfig();

/// Same as translatePreselConfig() but for the selection highlight
/// (SelectionColor, ShowSelectedFaceOutline, NoSelFaceHighlightWithOutline) —
/// the viewer applies it to its local (client-side) selection.
GuiExport Render::PreselHighlightConfig translateSelConfig();

/// Resolve the bump mapping settings (Render_BumpScale/Render_Parallax
/// view properties, RenderParams fallback) into the backend-neutral
/// per-frame config.
GuiExport Render::BumpConfig translateBumpConfig(App::PropertyContainer * view);

/// Resolve the scene (shadow) light from the traversal state's light
/// element; the viewer headlight is filtered out by node type -- it is
/// an ordinary light, and translateViewLightConfig below is what carries
/// it. The ground receiver settings honor the view's Shadow_* dynamic
/// properties (the Shadow draw style creates them) with ViewParams
/// fallback.
GuiExport Render::LightConfig translateLightConfig(SoState * state,
                                                   App::PropertyContainer * view);

/// Resolve the ordinary lights of the traversal -- the viewer's
/// headlight and backlight, and any SoDirectionalLight / SoPointLight a
/// document adds -- into the per-frame view-light config. Exactly the
/// complement of translateLightConfig: the shadow-capable node types it
/// claims for the scene light are the ones skipped here, so no light is
/// counted twice and none is dropped.
GuiExport Render::ViewLightConfig translateViewLightConfig(SoState * state);

/// Resolve the per-frame autozoom scale from the traversal state's view
/// volume (the exact SoAutoZoomTranslation::getScaleFactor math with a
/// node scaleFactor of 1); Material::autozoom entries multiply their own
/// scaleFactor on top in the backend.
GuiExport float translateAutoZoomScale(SoState * state);

/// Resolution scale of the expensive screen-space effect passes (reflection
/// re-render, SSAO resolve) from Render_EffectResolution / RenderParams.
GuiExport float translateEffectResolution(App::PropertyContainer * view);
GuiExport float translateSSAOResolution(App::PropertyContainer * view);
GuiExport float translateLevelTolerance(App::PropertyContainer * view);
GuiExport size_t translateGpuMemoryBudget(App::PropertyContainer * view);
/// How much of the error held back to fit the GPU budget survives each
/// plan that fits (docs/SceneStreaming.md #13c.3) -- the release half
/// of the ladder's control loop, and the half whose absence made it
/// oscillate.
GuiExport float translateLevelPressureRelease(App::PropertyContainer * view);
GuiExport bool translateLevelDebug(App::PropertyContainer * view);
GuiExport bool translateDowngradeLedger(App::PropertyContainer * view);
GuiExport bool translateClimbHardLimit(App::PropertyContainer * view);
GuiExport int translateClimbAdmitBatch(App::PropertyContainer * view);
GuiExport int translateDescentOrderBatch(App::PropertyContainer * view);
/// The rest band above the GPU budget inside which the downgrade sweep
/// does not trigger (docs/SceneStreaming.md #13c.6) -- what lets an
/// equilibrium that lands on the budget line stand instead of dither.
GuiExport float translateLevelBudgetDeadband(App::PropertyContainer * view);
GuiExport bool translateShapeVertices(App::PropertyContainer * view);
GuiExport bool translatePressureDropEdges(App::PropertyContainer * view);
/// The frame wait between the element contract's pressure stages,
/// escalating and releasing both (docs/SceneStreaming.md #13b).
GuiExport int translateElementGateStagger(App::PropertyContainer * view);
/// MEASUREMENT ONLY: the primitive count at or below which a line or
/// point draw is suppressed outright, to price the draw axis
/// (docs/FarFieldProxies.md 11.1i). 0 = off.
GuiExport int translateTinyElementCutoff(App::PropertyContainer * view);
/// Whether the element gates should suppress both classes outright
/// right now: Render_LoadDropElements is on, coarse-first is on, and
/// some document is still arriving (docs/SceneStreaming.md #13b).
GuiExport bool translateLoadDropElements(App::PropertyContainer * view);

} // namespace RendererBridge
} // namespace Gui

#endif // GUI_SOFCRENDERERBRIDGE_H
// vim: noai:ts=2:sw=2
