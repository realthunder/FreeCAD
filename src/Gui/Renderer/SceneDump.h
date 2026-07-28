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

#include <functional>

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
    RenderDebugConfig debugconf; ///< v20; defaulted on older snapshots
    /// User shaders (docs/RenderDebug.md §6; v23; empty on older
    /// snapshots): the scene-level post-stage list. "material"-stage
    /// programs ride the draw materials (Material::usershader) through
    /// a deduplicated shader table in the stream. Serialization
    /// includes the server-compiled viewer binaries
    /// (UserShader::compiled) so the compiler-less viewer tiers can
    /// load the programs.
    UserShaderConfig usershaderconf;
    /// Save-side hook: called once per unique user shader written, to
    /// append server-compiled binary variants for the viewer tiers
    /// beyond whatever the shader already carries. Unset when loading
    /// (and in tiers with no compiler).
    std::function<void(const UserShader &,
                       std::vector<UserShader::Compiled> &)> shaderBins;

    /// Save-side hook enabling out-of-band texture payloads (v26): when
    /// set, a texture is written as its header plus its content key,
    /// and the pixels are handed here instead of into the stream. The
    /// streaming transport uses it so a republish — which happens on
    /// every feed change, including a selection pick — no longer
    /// re-sends every embedded image; the viewer fetches each key once
    /// and caches it. Unset for a bundled snapshot, which must stay
    /// self-contained.
    typedef std::function<void(const std::string &key,
                               std::vector<uint8_t> &&pixels)> TextureBlobSink;
    TextureBlobSink textureBlobs;
    /// Load-side counterpart: the textures that arrived key-only. Their
    /// `pixels` must be filled and `deferred` cleared before the
    /// snapshot is fed to a backend — these alias the entries the draw
    /// materials point at, so filling them in place is enough.
    std::vector<std::shared_ptr<TextureImage>> deferredTextures;
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
    /// pre-expanded); null = none. A texture-table entry since v27, so
    /// that it shares the content key and the out-of-band payload path
    /// above — as a raw blob outside the table it was re-sent whole on
    /// every publish, and it is the single largest item in the stream.
    std::shared_ptr<const TextureImage> hatch;

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

/// The serializer format version this build writes (and the newest it
/// can read) — the version-handshake token of the viewer control
/// channel (docs/RenderDebug.md §4.4).
RendererExport uint32_t sceneDumpVersion();
/// Peek the format version of a serialized snapshot (the payload
/// without the 8-byte stream-version prefix); 0 when it is not a
/// snapshot. A viewer receiving a payload newer than its own
/// sceneDumpVersion() reloads itself.
RendererExport uint32_t sceneSnapshotVersion(const void *data, size_t size);

} // namespace Render

#endif // RENDERER_SCENE_DUMP_H
