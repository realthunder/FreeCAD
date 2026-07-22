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

namespace Gui {

class View3DInventor;

namespace RendererBridge {

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
GuiExport Render::DrawCallList translate(
        const SoFCRenderCache::VertexCacheMap & vcachemap,
        int selId = 0, bool highlight = false,
        bool sequentialOrder = false);

/// Resolve the hidden-line draw style state from the traversal state
/// (SoFCDisplayModeElement) into the backend-neutral per-frame config.
GuiExport Render::HiddenLineConfig translateHiddenLineConfig(SoState * state);

/// Resolve the section fill (cap) ViewParams into the backend-neutral
/// per-frame config.
GuiExport Render::SectionConfig translateSectionConfig();

/// Resolve the ambient occlusion settings into the backend-neutral
/// per-frame config: the view's Render_SSAO* dynamic properties when
/// present (see View3DInventorViewer::setRendererType, which materializes
/// them like the Shadow draw style's Shadow_* properties), with the
/// global RenderParams as fallback.
GuiExport Render::AOConfig translateAOConfig(View3DInventor * view);

/// Resolve the physically based shading settings (Render_PBR* view
/// properties, RenderParams fallback) into the backend-neutral per-frame
/// config.
GuiExport Render::PBRConfig translatePBRConfig(View3DInventor * view);

/// Resolve the volumetric lighting settings (Render_Volumetric* view
/// properties, RenderParams fallback) into the backend-neutral per-frame
/// config.
GuiExport Render::VolumetricConfig translateVolumetricConfig(
        View3DInventor * view);

/// Resolve the water surface settings (Render_WaterSurface/WaterWave*
/// view properties, RenderParams fallback) into the backend-neutral
/// per-frame config.
GuiExport Render::WaterConfig translateWaterConfig(View3DInventor * view);

/// Resolve the bump mapping settings (Render_BumpScale/Render_Parallax
/// view properties, RenderParams fallback) into the backend-neutral
/// per-frame config.
GuiExport Render::BumpConfig translateBumpConfig(View3DInventor * view);

/// Resolve the scene (shadow) light from the traversal state's light
/// element; the viewer headlight is filtered out by node type. The
/// ground receiver settings honor the view's Shadow_* dynamic properties
/// (the Shadow draw style creates them) with ViewParams fallback.
GuiExport Render::LightConfig translateLightConfig(SoState * state,
                                                   View3DInventor * view);

/// Resolve the per-frame autozoom scale from the traversal state's view
/// volume (the exact SoAutoZoomTranslation::getScaleFactor math with a
/// node scaleFactor of 1); Material::autozoom entries multiply their own
/// scaleFactor on top in the backend.
GuiExport float translateAutoZoomScale(SoState * state);

/// Resolution scale of the expensive screen-space effect passes (reflection
/// re-render, SSAO resolve) from Render_EffectResolution / RenderParams.
GuiExport float translateEffectResolution(View3DInventor * view);

} // namespace RendererBridge
} // namespace Gui

#endif // GUI_SOFCRENDERERBRIDGE_H
// vim: noai:ts=2:sw=2
