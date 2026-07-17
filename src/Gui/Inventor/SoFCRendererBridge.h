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
GuiExport Render::DrawCallList translate(
        const SoFCRenderCache::VertexCacheMap & vcachemap,
        int selId = 0, bool highlight = false);

/// Resolve the hidden-line draw style state from the traversal state
/// (SoFCDisplayModeElement) into the backend-neutral per-frame config.
GuiExport Render::HiddenLineConfig translateHiddenLineConfig(SoState * state);

/// Resolve the section fill (cap) ViewParams into the backend-neutral
/// per-frame config.
GuiExport Render::SectionConfig translateSectionConfig();

} // namespace RendererBridge
} // namespace Gui

#endif // GUI_SOFCRENDERERBRIDGE_H
// vim: noai:ts=2:sw=2
