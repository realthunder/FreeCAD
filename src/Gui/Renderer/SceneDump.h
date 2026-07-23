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
 ****************************************************************************/

#ifndef RENDERER_SCENE_DUMP_H
#define RENDERER_SCENE_DUMP_H

/// Binary snapshot of a complete backend scene feed — the draw calls
/// with their mesh/texture data plus every per-frame config — written
/// by the desktop bridge (FC_BGFX_DUMP_SCENE=<path>, captured on the
/// first non-empty render) and replayed by the standalone/WebAssembly
/// viewer. Little-endian, versioned; both sides of a transfer must be
/// built from the same serializer version.

#include "Renderer.h"

namespace Render {

struct SceneSnapshot {
    DrawCallList scene;
    /// Selection feeds keyed by selection id (SelIdBits) and the
    /// preselection highlight, as fed through addSelection()/
    /// setHighlight() (v2; empty on v1 snapshots).
    std::vector<std::pair<int, DrawCallList>> selections;
    DrawCallList highlight;
    bool highlightWholeOnTop = false;
    /// Overlay feeds as fed through setOverlay() (v3; empty on older
    /// snapshots): viewport-anchored content — the foreground
    /// superimposition, the corner axis cross — replayed by the
    /// standalone/WASM viewer with its own viewport and camera.
    struct Overlay {
        int id = 0;
        OverlayAnchor anchor;
        DrawCallList draws;
    };
    std::vector<Overlay> overlays;
    Background background;
    HiddenLineConfig hlconfig;
    SectionConfig secconf;
    AOConfig aoconf;
    PBRConfig pbrconf;
    BumpConfig bumpconf;
    LightConfig lightconf;
    VolumetricConfig volconf;
    WaterConfig waterconf;
    BloomConfig bloomconf;      ///< v18; defaulted on older snapshots
    float autozoomScale = 1.0f;
    /// Resolution scale (0.25-1.0) of the expensive screen-space effect
    /// passes (reflection re-render, SSAO resolve); 1.0 = full resolution.
    float effectResolution = 1.0f;
    float ssaoResolution = 1.0f;

    /// Preselection (hover) and selection highlight styling from ViewParams
    /// (v10). The standalone/WASM viewer builds both highlights locally (no
    /// round trip), so it reads these to fill or only outline the hovered /
    /// selected face like the backend would (color, outline width, etc.).
    PreselHighlightConfig preselconf;
    PreselHighlightConfig selconf;

    /// Section-cap hatch image, RGBA8 (the renderer stores it
    /// pre-expanded); empty = none.
    std::vector<uint8_t> hatchRGBA;
    int hatchWidth = 0;
    int hatchHeight = 0;

    /// Camera and viewport at capture time (GL-layout matrices).
    float viewMatrix[16];
    float projMatrix[16];
    int width = 0;
    int height = 0;
    /// Clear/background color at capture, packed 0xRRGGBBAA.
    uint32_t clearColor = 0x333333ff;
};

RendererExport bool saveSceneSnapshot(const char *path,
                                      const SceneSnapshot &snap);
RendererExport bool loadSceneSnapshot(const char *path,
                                      SceneSnapshot &snap);

/// In-memory variants (the live-streaming transport).
RendererExport bool saveSceneSnapshot(std::vector<uint8_t> &out,
                                      const SceneSnapshot &snap);
RendererExport bool loadSceneSnapshot(const void *data, size_t size,
                                      SceneSnapshot &snap);

} // namespace Render

#endif // RENDERER_SCENE_DUMP_H
