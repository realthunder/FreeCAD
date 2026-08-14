/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef RENDERER_RENDERER_H
#define RENDERER_RENDERER_H

#include <FCConfig.h>

#ifdef FreeCADRenderer_STATIC
#   define RendererExport
#elif defined(FreeCADRenderer_EXPORTS)
#   define RendererExport   FREECAD_DECL_EXPORT
#else
#   define RendererExport   FREECAD_DECL_IMPORT
#endif

#include <cstdint>
#include <functional>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class QOpenGLWidget;
class QColor;

namespace Render {

class RenderLib;

/// CPU-side snapshot of one geometry cache (SoFCVertexCache on the Gui side).
/// All array pointers stay valid for as long as `owner` is held. Backends key
/// GPU uploads on `cacheId`: the same id always refers to identical content.
struct MeshData {
    uint64_t cacheId = 0;
    std::shared_ptr<const void> owner;
    /// Opaque identity of the geometry that fed this mesh (the Gui
    /// bridge stores the shape node, proto node preferred so color
    /// variants share it). Never dereferenced by the renderer — it is
    /// the tag a shape-backed level generator was registered under
    /// (MeshSource.h), carried so the publisher can associate the
    /// mesh's content key with its source.
    const void *sourceTag = nullptr;
    /// What this mesh itself is on its ladder (§7 coarse-first): 0 =
    /// the exact tessellation, else the level error (relative to the
    /// shape diagonal) it was deliberately built coarse at — in which
    /// case the serializer declares the exact mesh as an unbuilt rung
    /// above it. Stamped by the bridge from the source registry.
    float levelError = 0.0f;

    /// Bumped every time the arrays behind this mesh are REPLACED in
    /// place — a level ladder's rungs all fill one mesh object under
    /// one cacheId (docs/SceneStreaming.md §7), and a release empties
    /// it the same way. A GPU cache keyed by cacheId must compare this
    /// before trusting its upload: "a cache id always refers to
    /// identical content" stopped being true the day meshes grew
    /// rungs — measured as exact geometry resident in every book while
    /// the screen kept drawing the coarse upload, with
    /// "glDrawElementsInstanced: Insufficient buffer size" where the
    /// refined index counts overran the stale buffers. Zero for
    /// bridge-built meshes, which really are immutable.
    uint32_t generation = 0;

    int numVertices = 0;
    const float *positions = nullptr;   ///< xyz per vertex, never null
    const float *normals = nullptr;     ///< xyz per vertex, may be null
    const uint8_t *colors = nullptr;    ///< rgba8 per vertex, may be null

    const int32_t *triangleIndices = nullptr;
    int numTriangleIndices = 0;
    const int32_t *lineIndices = nullptr;   ///< GL_LINES style vertex pairs
    int numLineIndices = 0;
    const int32_t *pointIndices = nullptr;
    int numPointIndices = 0;

    /// Line indices with seam lines (e.g. the closing edge of a cylinder
    /// face) filtered out; null when the cache has no seams. Used by the
    /// hidden-line draw style's hideSeam option.
    const int32_t *noSeamLineIndices = nullptr;
    int numNoSeamLineIndices = 0;

    /// Per-face-part {start, count} ranges into triangleIndices, filled
    /// for hidden-line outline materials: the GL renderer outlines
    /// geometry face part by face part under clip planes and in the
    /// perFaceOutline hidden-line mode. Empty when the cache carries no
    /// face part table (then those modes outline nothing, like GL).
    std::vector<std::pair<int, int>> triangleParts;
    /// The subset of triangleParts belonging to non-flat (curved) faces;
    /// the perFaceOutline mode outlines only these when combined with
    /// sceneOutline or a zero outline width (GL: getNonFlatParts()).
    std::vector<std::pair<int, int>> nonFlatParts;

    /// Per-edge-part {start, count} ranges into lineIndices, and per-vertex-
    /// part ranges into pointIndices (stride 1), filled for the whole-object
    /// line/point meshes so the browser viewer maps a hovered edge/vertex to
    /// its element (like triangleParts for faces). Empty when the cache
    /// carries no such part table.
    std::vector<std::pair<int, int>> lineParts;
    std::vector<std::pair<int, int>> pointParts;

    /// Solid-geometry knowledge for section capping (SoFCShapeInfo):
    /// 0 = none, 1 = some face parts belong to solids (solidParts holds
    /// their {start, count} ranges into triangleIndices), 2 = the whole
    /// triangle set is solid. Filled for clipped triangle materials.
    int hasSolid = 0;
    std::vector<std::pair<int, int>> solidParts;

    bool hasTransparency = false;   ///< some per-vertex colors are transparent
    bool hasOpaqueParts = false;    ///< some per-vertex colors are opaque

    /// Every element of this drawable is ATTACHED to higher-dimensional
    /// geometry of the same shape: every vertex of a point set is an
    /// edge endpoint, or every edge of a line set bounds a face
    /// (docs/SceneStreaming.md #13b). Such a drawable may be suppressed
    /// under memory pressure, because what makes it redundant is drawn
    /// anyway -- a vertex sits on an edge already on screen, an edge
    /// runs along a face silhouette already on screen.
    ///
    /// All or nothing per drawable, and false by default so anything
    /// the producer has not classified always draws. One floating
    /// element -- a point cloud's points, a wire, a sketch, a datum
    /// line -- makes the whole drawable unsuppressable, because nothing
    /// else on screen would show it.
    bool attachedOnly = false;

    /// Texture coordinates, xyzw per vertex (Coin's SbVec4f layout; the
    /// default texgen and 2D texcoord nodes produce (s, t, 0, 1)). Null
    /// when the cache was built without an active texture.
    const float *texCoords = nullptr;
};

/// CPU-side snapshot of a texture image applied to triangle draws
/// (unit-0 SoTexture2 on the Gui side). Pixels are copied at translate
/// time; backends key GPU uploads on `textureId`: the same id always
/// refers to identical content.
struct TextureImage {
    uint64_t textureId = 0;
    int width = 0;
    int height = 0;
    /// 1 = luminance, 2 = luminance+alpha, 3 = rgb, 4 = rgba; rows are
    /// tightly packed, bottom-up like GL.
    int numComponents = 0;
    std::vector<uint8_t> pixels;

    enum Wrap : uint8_t { Repeat, Clamp };
    uint8_t wrapS = Repeat;
    uint8_t wrapT = Repeat;

    /// GL texture environment (SoTexture2::model).
    enum Model : uint8_t { Modulate, Decal, Blend, Replace };
    uint8_t model = Modulate;
    /// rgb used by the Blend model, packed 0xRRGGBBAA.
    uint32_t blendColor = 0;

    /// Content key of the pixel payload — SHA-1 of `pixels`, as 40 hex
    /// characters; empty until the serializer computes it. This is the
    /// address the streaming tier fetches and caches the pixels under
    /// (`GET /blob?key=`), so it must depend on the bytes alone and not
    /// on `textureId`, which is only process-stable.
    mutable std::string contentKey;
    /// Set on a texture that arrived key-only: `pixels` is empty and
    /// must be filled from the key before the texture can be uploaded.
    /// Only a streamed snapshot defers; a bundled one is self-contained.
    bool deferred = false;
};

/// Window background drawn behind the scene, mirroring the Coin-side
/// gradient background node. Backends that consume the scene must draw it
/// themselves so transparent geometry blends against the real background
/// (and the Coin node is skipped for backend-rendered frames). Colors are
/// packed 0xRRGGBBAA.
struct Background {
    enum Type : uint8_t { Flat, LinearGradient, RadialGradient };
    uint8_t type = Flat;
    uint32_t fromColor = 0;  ///< flat color / gradient top / radial center
    uint32_t toColor = 0;    ///< gradient bottom / radial edge
    uint32_t midColor = 0;   ///< optional intermediate color
    bool hasMid = false;
};

/// Declarative placement of one overlay feed (Renderer::setOverlay):
/// viewport-anchored content — the corner axis cross, the viewer's
/// foreground superimposition — drawn on top of the finished frame with
/// its own camera. The anchor carries camera/viewport parameters instead
/// of baked matrices so every consumer (including the WASM viewer) can
/// re-derive them from its own viewport size and camera each frame —
/// overlays re-anchor on resize and follow the local orbit camera for
/// free.
struct OverlayAnchor {
    enum Corner : uint8_t {
        FullViewport,  ///< cover the whole viewport (sizeFraction ignored)
        BottomLeft, BottomRight, TopLeft, TopRight,
    };
    uint8_t corner = FullViewport;
    /// Corner-anchored square viewport edge length as a fraction of
    /// min(viewport width, height). Ignored for FullViewport.
    float sizeFraction = 0.25f;
    /// Perspective vertical field of view in degrees; 0 = orthographic.
    float fovDeg = 0.0f;
    /// Orthographic view height in model units (used when fovDeg == 0);
    /// width follows the viewport aspect ratio.
    float orthoHeight = 10.0f;
    /// Eye distance from the overlay model origin along +z (the view
    /// matrix is translate(0,0,-cameraDistance) after the optional scene
    /// orientation).
    float cameraDistance = 5.0f;
    float nearPlane = 0.0f;
    float farPlane = 10.0f;
    /// Rotate the overlay content by the main camera's orientation (the
    /// rotation part of the scene view matrix) so it tracks the scene,
    /// like the axis cross does.
    bool orientFromScene = false;
    /// Distance in pixels between the viewport edges and the overlay
    /// rect, along the anchoring corner's directions (NaviCube margin +
    /// user offsets). Ignored for FullViewport.
    float marginX = 0.0f;
    float marginY = 0.0f;
    /// Pixel-space overlay (screen-space content: rubber band, 2D text):
    /// the projection maps one model unit to one pixel with the origin at
    /// the viewport rect's top-left corner and y growing downward (Qt
    /// widget coordinates), z clipped to [-1, 1]. Meant for FullViewport;
    /// fovDeg/orthoHeight/cameraDistance/nearPlane/farPlane/
    /// orientFromScene are ignored.
    bool pixelSpace = false;
    /// Scene-camera overlay (in-scene 3D content that must share the main
    /// scene's camera: editing overlays, measurement dimensions). The
    /// backend draws the feed over the full viewport with the current main
    /// view/projection matrices and a fresh depth buffer (on top of the
    /// finished scene, depth-tested within itself) — the world-space
    /// geometry lines up with the main scene for free, and re-derives from
    /// the local camera in the WASM viewer like the other overlays. When
    /// set, all corner/fov/ortho/cameraDistance/orient/pixelSpace fields
    /// are ignored.
    bool sceneCamera = false;

    bool operator==(const OverlayAnchor &o) const {
        return corner == o.corner && sizeFraction == o.sizeFraction
            && fovDeg == o.fovDeg && orthoHeight == o.orthoHeight
            && cameraDistance == o.cameraDistance
            && nearPlane == o.nearPlane && farPlane == o.farPlane
            && orientFromScene == o.orientFromScene
            && pixelSpace == o.pixelSpace
            && sceneCamera == o.sceneCamera
            && marginX == o.marginX && marginY == o.marginY;
    }
    bool operator!=(const OverlayAnchor &o) const { return !(*this == o); }
};

/// Per-frame hidden-line draw style configuration, mirroring the Coin-side
/// SoFCDisplayModeElement::HiddenLineConfig resolved at render time. Only
/// meaningful while `show` is true; scene materials carry the matching
/// Material::outline flag.
struct HiddenLineConfig {
    bool show = false;           ///< hidden-line draw style active
    bool hideFace = false;       ///< skip triangle face fills
    bool hideSeam = false;       ///< skip seam lines (MeshData no-seam set)
    bool hideVertex = false;     ///< skip point draws; outline corner caps
    bool perFaceOutline = false; ///< outline each face part separately
    bool sceneOutline = false;   ///< one silhouette around the whole scene
    float outlineWidth = 0.0f;   ///< minimum outline width in pixels
    /// ViewParams::OutlineThicken — outline width is at least
    /// linewidth * outlineThicken (GL: renderOutline ~1468).
    float outlineThicken = 4.0f;
    /// Resolved scene-outline line color (display-mode line color or
    /// ViewParams::HiddenLineColor), packed 0xRRGGBBAA.
    uint32_t lineColor = 0;

    bool operator==(const HiddenLineConfig &o) const {
        return show == o.show && hideFace == o.hideFace
            && hideSeam == o.hideSeam && hideVertex == o.hideVertex
            && perFaceOutline == o.perFaceOutline
            && sceneOutline == o.sceneOutline
            && outlineWidth == o.outlineWidth
            && outlineThicken == o.outlineThicken
            && lineColor == o.lineColor;
    }
    bool operator!=(const HiddenLineConfig &o) const { return !(*this == o); }
};

/// Preselection (hover) highlight styling, resolved from ViewParams by the
/// bridge like the other configs. The desktop backend applies it through the
/// streamed highlight draws; the standalone/WASM viewer, which builds its own
/// hover highlight locally with no round trip, reads it to decide whether to
/// fill the hovered face or only outline it — so it tracks the backend's
/// ShowPreSelectedFaceOutline / NoPreSelFaceHighlightWithOutline settings
/// instead of hardcoding.
struct PreselHighlightConfig {
    uint32_t color = 0xE1E114FF;   ///< ViewParams::HighlightColor, 0xRRGGBBAA
    float outlineWidth = 2.0f;     ///< resolved face-outline width in pixels
    bool faceOutline = true;       ///< ViewParams::ShowPreSelectedFaceOutline
    bool outlineOnly = true;       ///< NoPreSelFaceHighlightWithOutline (no fill)
    float pickRadius = 5.0f;       ///< ViewParams::PickRadius, screen pixels

    bool operator==(const PreselHighlightConfig &o) const {
        return color == o.color && outlineWidth == o.outlineWidth
            && faceOutline == o.faceOutline && outlineOnly == o.outlineOnly
            && pickRadius == o.pickRadius;
    }
    bool operator!=(const PreselHighlightConfig &o) const {
        return !(*this == o);
    }
};

/// Per-frame section (clip plane) fill configuration, mirroring the
/// ViewParams the GL renderer reads in renderSection/_renderSection.
/// Resolved by the bridge each render like HiddenLineConfig.
struct SectionConfig {
    bool fill = true;        ///< cap the cross section of clipped solids
    bool fillInvert = true;  ///< invert the cap fill color
    bool fillGroup = false;  ///< cap same-material solids together
    bool concave = false;    ///< SectionConcave (union of half-spaces)
    bool hatchEnable = true; ///< modulate the cap with the hatch texture
    float hatchScale = 1.0f; ///< hatch texture scale

    bool operator==(const SectionConfig &o) const {
        return fill == o.fill && fillInvert == o.fillInvert
            && fillGroup == o.fillGroup && concave == o.concave
            && hatchEnable == o.hatchEnable && hatchScale == o.hatchScale;
    }
    bool operator!=(const SectionConfig &o) const { return !(*this == o); }
};

/// Per-frame screen-space ambient occlusion configuration (there is no
/// GL-renderer counterpart; the effect exists only in external backends).
/// Resolved by the bridge each render like SectionConfig.
struct AOConfig {
    bool enabled = false;    ///< ambient occlusion pass active
    /// Sample radius in world units; 0 = automatic (a fraction of the
    /// scene bounding-sphere size, resolved by the backend).
    float radius = 0.0f;
    float intensity = 1.0f;  ///< occlusion darkening strength
    /// Algorithm: 0 = classic hemisphere-kernel SSAO, 1 = GTAO
    /// (ground-truth horizon-based, XeGTAO-style).
    int method = 0;
    /// Interaction fast path (GTAO only): fewer slices/steps so AO stays
    /// visible while the camera moves, refined once idle. A per-frame
    /// hint, not persisted in scene snapshots.
    bool fast = false;
    /// GTAO tuning (0 = backend default): screen-space slice directions
    /// per pixel and horizon-march steps per slice side.
    int slices = 0;
    int steps = 0;

    bool operator==(const AOConfig &o) const {
        return enabled == o.enabled && radius == o.radius
            && intensity == o.intensity && method == o.method
            && fast == o.fast && slices == o.slices && steps == o.steps;
    }
    bool operator!=(const AOConfig &o) const { return !(*this == o); }
};

/// Per-frame occlusion culling configuration
/// (docs/FarFieldProxies.md §12). Resolved by the bridge each render
/// from the Render_Occlusion* view properties / global RenderParams
/// defaults, like AOConfig.
///
/// The mechanism is described in Gui/Renderer/OcclusionCull.h; what
/// belongs here is only what a user or a script may turn. Every default
/// is chosen so that being wrong costs frame time rather than pixels:
/// too small a budget or too long a lifetime draws geometry that could
/// have been skipped, never the reverse.
struct OcclusionCullConfig {
    /// Skip draws whose spatial-index node the depth buffer proved
    /// could not have contributed a pixel. Off leaves the frustum
    /// culling the renderer already does untouched.
    bool enabled = false;
    /// How many frames a *visible* verdict is believed before the node
    /// is re-tested. Purely a cost/latency trade: geometry that becomes
    /// hidden keeps drawing until its verdict expires, which is
    /// invisible in the image and merely wasteful.
    uint32_t visibleTtl = 6;
    /// Tests issued per frame. The GPU offers 256 queries at a time
    /// (BGFX_CONFIG_MAX_OCCLUSION_QUERIES) and `RenderDebug_Occlusion`
    /// is the other consumer of that pool, so the default leaves it
    /// room; exceeding what the backend can hand out is not an error,
    /// the surplus is simply offered again next frame.
    uint32_t budget = 128;
    /// Do not test a node standing for fewer instances than this. A
    /// query is itself a draw, so testing a node that could save one
    /// draw loses whether it answers hidden or visible.
    uint32_t minSubtree = 8;
    /// ⚠️ **The fail-safe, and the reason a bug here costs frames and
    /// not correctness.** A hidden node is cut *and* re-tested every
    /// frame, so its way back is the answer to that test. If the
    /// answers stop arriving — no query handles, a backend that dropped
    /// the batch, a walk abandoned — a node would otherwise stay hidden
    /// forever and geometry would simply be missing. After this many
    /// frames without an *answer* (not without an offer), a hidden node
    /// reverts to visible. Confirmations keep it hidden indefinitely,
    /// so this never flickers a node the tests are still answering.
    uint32_t maxHiddenFrames = 120;
    /// Outward padding of a test box, as a fraction of its own
    /// diagonal — relative, so it means the same at any model scale.
    ///
    /// ⚠️ Not a tolerance: the measurement does not work without it. A
    /// node's bounds are the union of its contents', so a box face
    /// coincides *exactly* with a real surface whenever a part has a
    /// flat face at its own extreme — in CAD the common case, not the
    /// edge case. Rasterized at equal depth the two disagree in the
    /// last bit, and where the box loses, LEQUAL rejects every fragment
    /// and the node calls itself hidden while in plain view. Un-padded
    /// this reported 99% of a 5455-part model hidden, the root
    /// included.
    float padFraction = 1.0e-3f;
    /// Additional outward padding of a test box, in depth-buffer steps
    /// (24-bit; the scene target is D24S8) converted to a world
    /// distance at the box's nearest corner.
    ///
    /// ⚠️ The other half of the padding, and the half `padFraction`
    /// cannot supply. A pad measured in the box's own diagonal says
    /// nothing about whether the *depth buffer* can separate the box
    /// from the surface it bounds: a small part lying flush on a large
    /// panel has a small diagonal and therefore a small pad, at a
    /// distance where one depth step is far larger. The two quantize to
    /// the same value, LEQUAL loses the tie half the time, and the node
    /// reports itself hidden in plain view — measured as the whole
    /// component detail of a board disappearing while the board stayed
    /// (§12.6). Costs frame time when too large and pixels when too
    /// small, so the default is generous.
    float depthPadLsb = 16.0f;
    /// How many consecutive answers of "no pixels" a node must give
    /// before its subtree is actually skipped. 1 acts on every answer.
    ///
    /// A test is issued against one frame's depth and read against a
    /// later one -- non-blocking on purpose -- so the occluders move
    /// underneath the answer while it is in flight. Acted on singly, a
    /// node tested while an occluder was still drawn gets culled after
    /// that occluder has gone; the hole tests visible; it returns; and
    /// it oscillates. Measured at 1, two captures of the same static
    /// scene 30 s apart differed in 14101 pixels (sec 12.6).
    ///
    /// WARNING: confirmations DILUTE that oscillation and were measured
    /// not to remove it (sec 12.7) -- false answers come in runs, and
    /// the residual damage tracks the re-test count, which this knob
    /// cannot reach. The hardware-query path is therefore not
    /// image-stable at any setting of this; the software oracle below
    /// is, by construction, and is the shipped answer.
    uint32_t hiddenConfirm = 2;

    /// KEY: Answer with a CPU software depth buffer
    /// (Gui/Renderer/MaskedOcclusion.h) instead of hardware queries.
    ///
    /// Everything above this line -- the lifetimes, the confirmations,
    /// the budget, both pads -- exists because a hardware query's answer
    /// arrives a frame or two after the question, and section 12.11 measured
    /// that none of them reach the failure it causes. The software path
    /// reads none of them: occluders are rasterized and nodes tested
    /// against the same buffer in one pass, so there is no latency to
    /// age and no verdict to confirm. What it reads instead is the three
    /// fields below. Default ON, matching Render_OcclusionSoftware:
    /// the query path is not image-stable at any knob setting.
    bool software = true;
    /// Triangles the software occluder pass may rasterize per frame.
    uint32_t occluderTriangles = 250000;
    /// Projected bounding-box diagonal, in pixels, under which a draw is
    /// not worth rasterizing as an occluder.
    float minOccluderPx = 24.0f;
    /// Software buffer resolution as a divisor of the viewport.
    /// WARNING: Above 1 this can over-cull -- see MaskedCullConfig.
    uint32_t softwareDivisor = 1;
    /// Worker threads for the software occluder pass, 0 = automatic.
    uint32_t softwareThreads = 0;
    /// KEY: Run the software pass's four-wide vector pre-pass, which
    /// discards triangles that cover no pixel before the exact
    /// rasterizer looks at them (Gui/Renderer/MaskedOcclusion.h,
    /// `setSimdFilter`). It can only discard, so turning it off changes
    /// how long the pass takes and -- at the margin, for triangles a
    /// hundredth of a pixel from covering nothing -- how much it hides.
    /// It cannot change what the buffer claims is there.
    bool softwareSimd = true;

    /// KEY: Ask the occlusion question per object rather than per
    /// partition group (Gui/Renderer/MaskedOcclusion.h, `testInstances`).
    /// The group is the unit the walk can skip, and measurement says it
    /// is the unit that is failing to resolve: 91% of what a cull still
    /// submits reaches no pixel, unmoved by a tenfold better buffer.
    bool perInstance = false;

    /// How many consecutive frames every draw of a source must have
    /// been culled before the level plan's DOWNGRADE sweep may treat
    /// that source as free -- occlusion as a memory mechanism, not just
    /// a time one. 0 disables the feed. Only the software oracle's
    /// verdicts are folded (deterministic per frame); the streak is the
    /// hysteresis that keeps a camera drifting across a verdict from
    /// costing an upload per flap.
    uint32_t demoteStreak = 8;

    /// KEY: Rasterize the software pass's occluders from coarse hulls
    /// rather than from their meshes (Gui/Renderer/OccluderMesh.h). The
    /// budget above says what the pass may spend; this says what it
    /// buys with it -- a hull is a fraction of the triangles, so the
    /// same allowance admits far more of the candidates, and it was the
    /// candidates that never got in rather than the buffer's speed that
    /// left the pass hiding 45% of a 95.4% ceiling.
    bool coarseOccluders = false;
    /// Decimation rung a hull is built at, coarsest first.
    uint32_t coarseLevel = 2;
    /// Triangles a draw must carry before it is worth a hull.
    uint32_t coarseMinTriangles = 512;
    /// Hulls that may be built in one frame; 0 freezes the cache.
    uint32_t coarseBuilds = 8;
    /// How far a hull recedes from the camera before it is rasterized,
    /// as a multiple of its own measured displacement bound. 1 is the
    /// value at which it cannot claim to be nearer than the surface it
    /// stands for; 0 is the unbiased measurement.
    float coarseBias = 1.0f;
    /// What the hull cache may hold, in bytes.
    size_t coarseMemory = size_t(64) << 20;

    /// Run the frame-level A/B probe (CullBenefit.h): alternate frames
    /// with the whole cull block on and off, compare median frame
    /// cost, and print the verdict on the culling readout cadence.
    /// While a probe's off-arm runs, the oracle does not run at all --
    /// the hidden-streak demote feed pauses with it, which over one
    /// probe is a handful of frames. Measurement only: the verdict
    /// does not yet gate anything by itself (12.13's wire-or-delete
    /// decision reads it first).
    bool benefitProbe = false;

    bool operator==(const OcclusionCullConfig &o) const {
        return enabled == o.enabled && visibleTtl == o.visibleTtl
            && budget == o.budget && minSubtree == o.minSubtree
            && maxHiddenFrames == o.maxHiddenFrames
            && padFraction == o.padFraction
            && depthPadLsb == o.depthPadLsb
            && hiddenConfirm == o.hiddenConfirm
            && software == o.software
            && occluderTriangles == o.occluderTriangles
            && minOccluderPx == o.minOccluderPx
            && softwareDivisor == o.softwareDivisor
            && softwareThreads == o.softwareThreads
            && softwareSimd == o.softwareSimd
            && perInstance == o.perInstance
            && demoteStreak == o.demoteStreak
            && coarseOccluders == o.coarseOccluders
            && coarseLevel == o.coarseLevel
            && coarseMinTriangles == o.coarseMinTriangles
            && coarseBuilds == o.coarseBuilds
            && coarseBias == o.coarseBias
            && coarseMemory == o.coarseMemory
            && benefitProbe == o.benefitProbe;
    }
    bool operator!=(const OcclusionCullConfig &o) const { return !(*this == o); }
};

/// Per-frame render debugging configuration (docs/RenderDebug.md).
/// Resolved by the bridge each render from the RenderDebug_* view
/// properties / global RenderParams defaults, like AOConfig.
struct RenderDebugConfig {
    /// Buffer visualization routed to the screen instead of the shaded
    /// scene: 0 = off, 1 = linearized depth, 2 = view-space normals,
    /// 3 = ambient occlusion term, 4 = shadow term, 5 = shadow tile
    /// coverage, 6 = overdraw, 7 = shadow filtering probe, 8 = UV,
    /// 9 = planar reflection, 10 = particle impact map, 11 =
    /// per-instance draw id. The on-top, highlight and overlay passes
    /// still draw on top.
    int viewMode = 0;
    /// Freeze every intentionally time- or history-dependent input
    /// (temporal accumulation/jitter, water/fire animation time) so a
    /// repeat frame renders identically — the determinism switch for
    /// golden-image comparison.
    bool freezeFrame = false;
    /// Log what a frame costs the CPU against what it costs the GPU,
    /// once a second (docs/FarFieldProxies.md §10.1). Shares the
    /// RenderDebug_Timing switch with the pipeline stage timers of
    /// Gui/RenderTiming.h, because it answers the half of the same
    /// question they cannot: their last stage ends at submission, and
    /// whether a per-object cost is submission or the GPU drawing it is
    /// what decides the mechanism of any culling scheme.
    bool frameTiming = false;
    /// Measure how much of what the frame draws could not have reached
    /// the screen (docs/FarFieldProxies.md §10.1): bounding boxes of
    /// spatial-index nodes re-tested against the finished depth buffer
    /// under hardware occlusion queries, a batch per frame, one line
    /// per completed walk. This is the proposed culling mechanism run
    /// without acting on its answers, not an estimate of one.
    bool occlusion = false;
    /// Log a histogram of how many pixels each drawn object covers, once
    /// a second (docs/FarFieldProxies.md §9). Costs one projection per
    /// object on the frames it reports and nothing while off.
    bool coverage = false;
    /// Log what a far-field cut would cost this camera, once a second,
    /// without generating a single proxy (docs/FarFieldProxies.md
    /// §11.1). Partitions the drawn instances and descends a frontier at
    /// several tolerances; the resulting draw count against today's is
    /// the number that decides whether phase 2 is worth building. Costs
    /// one partition build on the frames it reports — which is itself a
    /// reported number, since phase 3 has to pay it — and nothing while
    /// off.
    bool proxyCut = false;
    /// Generate proxies for a sample of the nodes that cut stops on and
    /// report what they commit (docs/FarFieldProxies.md §11.1c) — the
    /// measurement that turns the cut estimate's *extent* axis into an
    /// error axis. Unlike the others this one builds meshes, so it is
    /// bounded by a node sample and reports what it left out.
    bool proxyGen = false;
    /// ⭐ Audit the occlusion culling against the geometry itself, once
    /// a second (docs/FarFieldProxies.md §12.9). Every other measurement
    /// of the culling compares *pictures* — this one compares a verdict
    /// against what the draws it covers actually put on screen: the
    /// scene is re-rasterized with the cull mask ignored and each draw
    /// writing its own identity, so the set of ids owning a pixel is an
    /// exact answer to "which draws reach the screen", and its
    /// intersection with the mask is a list of proven over-culls, each
    /// named, with a pixel count. The same histogram gives the converse
    /// for free: drawn rows that own no pixel at all, which is the
    /// headroom the culling has not taken.
    ///
    /// Independent of \ref viewMode — the id re-render runs either for
    /// mode 11 (look at it) or for this (measure it), and the audit does
    /// not disturb what is on screen.
    bool cullAudit = false;

    /// Re-ask every still-drawn row with a tighter occludee volume and
    /// count what would have flipped (docs/FarFieldProxies.md §12.19).
    /// A diagnostic that decides whether a mechanism is worth building,
    /// not a mechanism: nothing is culled by it, the verdicts are only
    /// counted against \ref cullAudit's id image — which is also why it
    /// needs that audit on to report anything.
    ///
    /// ⚠️ One of its arms asks about every triangle of every drawn
    /// object. It runs on the audit's frame alone and still costs far
    /// more than a frame.
    bool cullBounds = false;

    /// A dynamically bound named shader parameter (docs/RenderDebug.md
    /// §2.5): any RenderDebug_* view property beyond the fixed knobs
    /// becomes a like-named vec4(-array) uniform — RenderDebug_myKnob
    /// feeds "uniform vec4 u_myKnob". The backend resolves the name to
    /// a uniform handle lazily; a value only reaches a shader that
    /// declares the uniform, so unknown names are harmless.
    struct UserParam {
        /// Uniform name, "u_" prefix included.
        std::string name;
        /// Values packed into vec4 lanes; size is a multiple of 4
        /// (zero-padded), size/4 = the uniform's vec4 array count.
        std::vector<float> values;

        bool operator==(const UserParam &o) const {
            return name == o.name && values == o.values;
        }
        bool operator!=(const UserParam &o) const { return !(*this == o); }
    };
    /// Sorted by name (the bridge enumerates a name-ordered property
    /// map), so equality is order-stable.
    std::vector<UserParam> userParams;

    bool operator==(const RenderDebugConfig &o) const {
        return viewMode == o.viewMode && freezeFrame == o.freezeFrame
            && frameTiming == o.frameTiming && occlusion == o.occlusion
            && coverage == o.coverage && proxyCut == o.proxyCut
            && proxyGen == o.proxyGen && cullAudit == o.cullAudit
            && cullBounds == o.cullBounds
            && userParams == o.userParams;
    }
    bool operator!=(const RenderDebugConfig &o) const { return !(*this == o); }
};

/// User-loadable shaders (docs/RenderDebug.md §6): programs authored on
/// Coin SoShaderProgram nodes in the scene graph, captured by the render
/// cache manager and attached to named backend pipeline stages. The
/// first supported stage is "post" — a full-screen fragment pass over
/// the composited scene color, drawn before the on-top/highlight/overlay
/// passes. Compilation is the backend's job (bgfx: runtime shaderc
/// compile cache); a shader that fails to compile is skipped with an
/// error report, never a black screen.
/// One user shader program (standalone so the render cache can hold a
/// shared_ptr to a "material"-stage program inside its per-draw
/// Material without pulling in the whole config).
struct UserShader {
    /// Pipeline stage name from SoShaderProgram::stage. Backends map
    /// known names and warn-and-skip unknown ones.
    std::string stage;
    /// bgfx .sc sources (SoShaderObject sourceType BGFX_SC, or
    /// FILENAME with a .sc suffix — the capture reads the file). An
    /// empty vertex source means the stage's built-in vertex shader
    /// (for "post": the full-screen triangle, input v_texcoord0; for
    /// "material": the stock mesh vertex stage, outputs v_normal,
    /// v_color0, v_vpos).
    std::string vertexSource;
    std::string fragmentSource;
    /// Particle state step of a stateful emitter (docs/RenderEngine.md
    /// §5.8): a fragment program run over the emitter's state textures
    /// once per fixed simulation step, reading the previous state
    /// (s_pstate0/1) and writing the next. Carried as the *second*
    /// SoFragmentShader of the program node — no new Coin node type,
    /// and the existing capture/merge/transport paths keep working.
    /// Empty leaves the emitter stateless (position = f(seed, time)),
    /// which is what every tier falls back to when float render
    /// targets are unavailable.
    std::string simulateSource;
    /// SoShaderParameter values attached to the shader objects,
    /// packed like RenderDebugConfig::UserParam (values zero-padded
    /// to vec4 lanes; names are uniform names, "u_" prefix and all).
    std::vector<RenderDebugConfig::UserParam> params;

    /// One server-compiled binary variant of this shader for a viewer
    /// tier that has no compiler of its own (docs/RenderDebug.md §6.3).
    /// The profile is the shaderc profile label the consuming backend
    /// matches against its own ("300_es" = the WebGL/WASM viewer,
    /// "140" = the native GL standalone viewer). An empty vsBin means
    /// the stage's stock vertex shader from the viewer's asset pack.
    struct Compiled {
        std::string profile;
        std::vector<uint8_t> vsBin;
        std::vector<uint8_t> fsBin;
        /// Binary of simulateSource, paired with the viewer's stock
        /// full-screen vertex shader. Empty for a stateless program.
        std::vector<uint8_t> simBin;

        bool operator==(const Compiled &o) const {
            return profile == o.profile && vsBin == o.vsBin
                && fsBin == o.fsBin && simBin == o.simBin;
        }
        bool operator!=(const Compiled &o) const { return !(*this == o); }
    };
    /// Transport payload attached at snapshot-serialization time
    /// (SceneSnapshot::shaderBins); empty on the desktop's own config
    /// feed. Part of equality on purpose: a viewer must re-apply a
    /// config whose sources it already has once the bins arrive.
    std::vector<Compiled> compiled;

    bool operator==(const UserShader &o) const {
        return stage == o.stage && vertexSource == o.vertexSource
            && fragmentSource == o.fragmentSource
            && simulateSource == o.simulateSource && params == o.params
            && compiled == o.compiled;
    }
    bool operator!=(const UserShader &o) const { return !(*this == o); }
};

struct UserShaderConfig {
    using Shader = UserShader;
    /// In traversal order; a later shader on the same stage wins.
    /// Only scene-level stages ("post") ride here — "material"-stage
    /// programs attach per object through Material::usershader instead.
    std::vector<UserShader> shaders;
    /// Assembled volume-splice variants (docs/RenderEngine.md §5.11),
    /// snapshot v24 transport only: the backend serializer assembles
    /// the same medium-splice programs its frame loop would, compiles
    /// the viewer-tier binaries and ships them here; a compiler-less
    /// tier that assembles the identical source adopts the shipped
    /// entry (with its binaries) instead. Empty on the desktop's own
    /// config feed.
    std::vector<UserShader> splices;

    bool operator==(const UserShaderConfig &o) const {
        return shaders == o.shaders && splices == o.splices;
    }
    bool operator!=(const UserShaderConfig &o) const { return !(*this == o); }
};

/// One-shot frame capture request (docs/RenderDebug.md §4): armed via
/// Renderer::requestFrameDump, executed by the next rendered frame.
struct FrameDumpRequest {
    /// Image output path; the writer picks the format from the
    /// extension (.ppm = raw PPM, anything else via Qt's image
    /// writers). Empty = statistics-only readback, no file.
    std::string path;
    /// RenderDebug view-mode override for the captured frame only
    /// (RenderDebugConfig::viewMode); -1 keeps the active mode.
    int mode = -1;
    /// Whether the captured frame carries the viewport chrome: the
    /// corner-anchored and pixel-space overlay feeds (navigation cube,
    /// corner axis cross, on-screen text). A debug capture wants the
    /// frame as staged and keeps them; an image export is of the model
    /// and does not. In-scene overlay feeds (OverlayAnchor::sceneCamera
    /// — editing overlays, dimensions) are scene content and are drawn
    /// either way.
    bool overlays = true;
};

/// Readback statistics of a captured frame — the cheap numeric
/// assertions of docs/RenderDebug.md §4.2 ("the scene is not black",
/// "N pixels covered") that smoke tests prefer over pixel diffs.
struct RenderStats {
    int width = 0;
    int height = 0;
    /// Pixels whose depth is in front of the far plane (< 0.999),
    /// i.e. covered by geometry; -1 when no depth was read.
    long long geometryPixels = -1;
    /// Average color of the geometry pixels, 0-255 per channel;
    /// -1 when no geometry pixel exists.
    float avgColor[3] = {-1.0f, -1.0f, -1.0f};
    bool valid = false;
};

/// Per-frame bump/normal mapping configuration (like AOConfig there is
/// no GL-renderer counterpart; the GL renderer never draws scene bump
/// maps). Applies to triangle draws carrying a Material::bumpmap.
struct BumpConfig {
    /// Strength multiplier: scales the slope of normal maps and the
    /// height amplitude of grayscale bump maps.
    float scale = 1.0f;
    /// Parallax-occlusion map grayscale height maps (UV offset along
    /// the view ray) instead of plain height-to-normal shading.
    bool parallax = true;

    bool operator==(const BumpConfig &o) const {
        return scale == o.scale && parallax == o.parallax;
    }
    bool operator!=(const BumpConfig &o) const { return !(*this == o); }
};

/// Per-frame scene light, resolved from the traversal state's light
/// element. Only the Shadow draw style places a shadow light into the
/// scene graph (the viewer headlight is filtered out); while valid,
/// backends replace their headlight with this light and render a shadow
/// map from it, honoring each draw's Material::shadowstyle.
struct LightConfig {
    bool valid = false;
    /// Render the shadow map (and the shafts/caustic occlusion depending
    /// on it); false drops shadows but keeps the scene lit. From the
    /// Render_Shadow view property.
    bool shadow = true;
    bool spot = false;       ///< spot light; directional otherwise
    float direction[3] = {0.0f, 0.0f, -1.0f};  ///< world, normalized
    float position[3] = {0.0f, 0.0f, 0.0f};    ///< world, spot only
    /// Spot cone half angle in radians (SoSpotLight::cutOffAngle) and
    /// intensity falloff inside the cone (SoSpotLight::dropOffRate,
    /// 0..1 mapping to a GL spot exponent of dropOffRate * 128).
    float cutOffAngle = 0.78539816f;
    float dropOffRate = 0.0f;
    uint32_t color = 0xffffffff;               ///< packed 0xRRGGBBAA
    float intensity = 1.0f;

    /// Shadow border smoothing (ShadowSmoothBorder / the Shadow draw
    /// style's SmoothBorder property, 0..100): scales the gaussian blur
    /// of the variance shadow map moments. 0 (the default) renders
    /// plain VSM with Coin's exact lookup semantics — the GL Shadow
    /// style's soft distance-growing penumbra; > 0 switches to the
    /// blurred EVSM variant (tighter penumbras than GL, a documented
    /// deviation).
    float smoothBorder = 0.0f;
    /// Coin VsmLookup parameters (ShadowEpsilon/ShadowThreshold, the
    /// Shadow draw style's Epsilon/Threshold properties): epsilon adds
    /// to the moment variance outright, threshold smoothsteps the
    /// Chebyshev tail (light-bleed reduction). Plain-VSM mode only.
    float epsilon = 1.0e-5f;
    float threshold = 0.0f;
    /// Coin SoShadowGroup N-tap receiver spread kernel
    /// (ShadowSpreadSize / ShadowSpreadSampleSize, the Shadow draw
    /// style's SpreadSize/SpreadSampleSize properties): spreadSize
    /// scales the tap spacing (Coin: swidth = size * 5e-4 shadow map
    /// UV per unit offset, spot lights * 0.1), spreadSampleSize picks
    /// the kernel — 0 a 4-tap dithered kernel (spreadSize > 0), s >= 1
    /// an N x N grid with N = min(2 s + 1, 8).
    float spreadSize = 0.0f;
    float spreadSampleSize = 0.0f;
    /// Shadow map size factor (ShadowPrecision, 0..1 of the backend's
    /// maximum — Coin's precision field semantics).
    float precision = 1.0f;

    /// Shadow ground plane (ShadowShowGround*): the Coin-side ground
    /// lives outside the captured scene graph, so backends draw their
    /// own — a receiving quad under the scene bounds.
    bool ground = false;
    float groundScale = 2.0f;    ///< times the scene extent
    uint32_t groundColor = 0x7d7d7dff;
    /// Ground texture (ShadowGroundTexture), modulated by the ground
    /// color and tiled every groundTextureSize world units
    /// (<= 0 = stretched once over the quad); null = plain color.
    std::shared_ptr<const TextureImage> groundTexture;
    float groundTextureSize = 100.0f;
    /// Ground transparency (ShadowGroundTransparency, 0..1): the ground
    /// quad alpha-blends over the background; 1 = invisible (Coin then
    /// keeps a shadow-only ground, not ported - the ground disappears).
    float groundTransparency = 0.0f;
    /// Ground bump map (ShadowGroundBumpMap): grayscale height (1/2
    /// components) or tangent-space normal map (3/4), perturbing the
    /// ground lighting like a scene Material::bumpmap; tiled with the
    /// ground texture coordinates.
    std::shared_ptr<const TextureImage> groundBumpMap;
    /// Ground reflection (RenderParams::GroundReflection, backend-only —
    /// no Coin counterpart): mirror the opaque scene in the ground plane
    /// and blend it onto the ground quad by the intensity factor.
    bool groundReflection = false;
    float groundReflectionIntensity = 0.4f;
    /// Sun disc (RenderParams::SunDisc, backend-only): draw a visible
    /// sun — a bright disc plus limb glow — in the sky along the
    /// directional scene light, occluded by geometry and feeding the
    /// bloom pass. Perspective cameras only (an orthographic sky has no
    /// per-pixel direction); ignored for spot lights.
    bool sunDisc = false;
    float sunDiscSize = 1.5f;  ///< angular radius in degrees

    bool operator==(const LightConfig &o) const {
        return valid == o.valid && shadow == o.shadow && spot == o.spot
            && direction[0] == o.direction[0]
            && direction[1] == o.direction[1]
            && direction[2] == o.direction[2]
            && position[0] == o.position[0]
            && position[1] == o.position[1]
            && position[2] == o.position[2]
            && color == o.color && intensity == o.intensity
            && cutOffAngle == o.cutOffAngle
            && dropOffRate == o.dropOffRate
            && smoothBorder == o.smoothBorder
            && epsilon == o.epsilon && threshold == o.threshold
            && spreadSize == o.spreadSize
            && spreadSampleSize == o.spreadSampleSize
            && precision == o.precision
            && ground == o.ground && groundScale == o.groundScale
            && groundColor == o.groundColor
            && groundTexture == o.groundTexture
            && groundTextureSize == o.groundTextureSize
            && groundTransparency == o.groundTransparency
            && groundBumpMap == o.groundBumpMap
            && groundReflection == o.groundReflection
            && groundReflectionIntensity == o.groundReflectionIntensity
            && sunDisc == o.sunDisc && sunDiscSize == o.sunDiscSize;
    }
    bool operator!=(const LightConfig &o) const { return !(*this == o); }
};

/// Per-frame volumetric lighting (light shaft) configuration (like
/// AOConfig there is no GL-renderer counterpart). While enabled and a
/// scene light with a shadow map is active (LightConfig::valid), the
/// backend raymarches the shadow map through a homogeneous scattering
/// medium and composites the inscattered light over the opaque scene.
struct VolumetricConfig {
    bool enabled = false;
    float intensity = 1.0f;  ///< inscattered light brightness
    /// Medium density (extinction coefficient) in inverse world units;
    /// 0 = automatic (a fraction of the scene size, resolved by the
    /// backend).
    float density = 0.0f;
    /// Water caustics: while a water body is active, project an animated
    /// caustic light pattern (modulated by the shadow map) onto surfaces
    /// below the water surface.
    bool caustics = false;
    float causticsIntensity = 1.0f; ///< caustic pattern brightness
    /// Caustic pattern cell frequency in inverse world units; 0 =
    /// automatic (a fraction of the water body size, resolved by the
    /// backend).
    float causticsScale = 0.0f;
    /// Caustic animation speed; 0 freezes the pattern.
    float causticsSpeed = 1.0f;

    bool operator==(const VolumetricConfig &o) const {
        return enabled == o.enabled && intensity == o.intensity
            && density == o.density && caustics == o.caustics
            && causticsIntensity == o.causticsIntensity
            && causticsScale == o.causticsScale
            && causticsSpeed == o.causticsSpeed;
    }
    bool operator!=(const VolumetricConfig &o) const { return !(*this == o); }
};

/// Per-frame water surface configuration. While enabled and the scene
/// carries a water body (Material::water), the water draws leave the
/// ordinary transparent path and render as an animated water surface:
/// screen-space refraction of the opaque scene behind them, a
/// Fresnel-blended environment reflection and a sun glint from the scene
/// light. Independent of the volumetric pass.
struct WaterConfig {
    bool enabled = false;
    /// Amplitude of the animated wave normal perturbation; 0 = flat
    /// mirror-like surface.
    float waveStrength = 0.3f;
    /// Wave frequency in inverse world units; 0 = automatic (a fraction
    /// of the water body size, resolved by the backend).
    float waveScale = 0.0f;
    /// Wave animation speed; 0 freezes the surface.
    float waveSpeed = 1.0f;
    /// Beer-Lambert absorption strength of the refraction over the water
    /// column depth; 0 = crystal clear.
    float absorption = 0.2f;
    /// Fraction of the water color scattered back into the absorbed
    /// refraction (in-scattering).
    float inscatter = 0.5f;
    /// Screen-space refraction of the scene behind the surface; off = flat
    /// water color.
    bool refraction = true;
    /// Reflection on the surface (Fresnel-blended); off = refraction only.
    bool reflection = true;
    /// Reflection method when reflection is on: true = planar mirror pass
    /// (exact), false = screen-space reflection (march). Env cubemap is the
    /// fallback for both.
    bool planarReflection = true;
    /// Receive the scene light's shadow on the surface (a shadow band on
    /// the water and a killed sun glint where shadowed). Needs the Shadow
    /// draw style with an active shadow map (LightConfig::shadow); off =
    /// the surface stays fully lit regardless of casters.
    bool shadow = true;
    /// How much the received shadow wobbles with the wave field: the
    /// shadow map is tapped at the wave-displaced surface point scaled
    /// by this factor. 0 = straight boundary on the flat surface, 1 =
    /// physical wave height, larger exaggerates the ripple.
    float shadowWobble = 1.0f;
    /// The ambient ripple pattern — what the surface does on its own:
    /// 0 = directional waves (the default four-octave slope-wave sum),
    /// 1 = rain drops (hashed cells each cycling an expanding circular
    /// ring), 2 = none, a still surface. None does not mean a dead
    /// surface: the disturbances the scene causes — fountain splash
    /// rings and the impact rings below — are added on top of the
    /// ambient field and survive it being switched off, which is how a
    /// pool reads as glass until something lands in it.
    int rippleType = 0;
    /// Rain drop density: cells per wave-frequency unit — higher packs
    /// more, smaller rings on the same surface. Directional waves
    /// ignore it.
    float rippleDensity = 1.0f;
    /// Height of the rings raised where particles report striking the
    /// surface (the impact map, docs/RenderEngine.md §5.8), relative to
    /// the wave strength. 0 = off. These are events, not a pattern:
    /// with nothing hitting the water there is nothing to see, which
    /// is what separates them from the rain ripple type.
    float impactStrength = 1.0f;
    /// Lifetime of one impact ring in seconds — also how far it
    /// travels, since a ring is sized to cross two impact-map cells in
    /// its life.
    float impactLife = 1.1f;

    bool operator==(const WaterConfig &o) const {
        return enabled == o.enabled && waveStrength == o.waveStrength
            && waveScale == o.waveScale && waveSpeed == o.waveSpeed
            && absorption == o.absorption && inscatter == o.inscatter
            && refraction == o.refraction && reflection == o.reflection
            && planarReflection == o.planarReflection
            && shadow == o.shadow && shadowWobble == o.shadowWobble
            && rippleType == o.rippleType
            && rippleDensity == o.rippleDensity
            && impactStrength == o.impactStrength
            && impactLife == o.impactLife;
    }
    bool operator!=(const WaterConfig &o) const { return !(*this == o); }
};

/// Per-frame bloom (glow) configuration. While enabled, pixels brighter
/// than the threshold bleed a blurred halo over their surroundings, and
/// light-source bodies (Material::lightsource) add their diffuse color
/// times lightintensity to the halo source — an emitter reads as
/// glowing instead of clipping to a flat bright shape.
struct BloomConfig {
    bool enabled = false;
    /// Scene luminance above which a pixel feeds the halo (soft knee
    /// below it). The scene target is LDR, so 1 disables everything but
    /// the light-source bodies' own contribution.
    float threshold = 0.9f;
    float intensity = 1.0f;  ///< halo brightness multiplier
    /// Halo radius scale (1 = default gaussian footprint, larger blooms
    /// wider).
    float radius = 1.0f;

    bool operator==(const BloomConfig &o) const {
        return enabled == o.enabled && threshold == o.threshold
            && intensity == o.intensity && radius == o.radius;
    }
    bool operator!=(const BloomConfig &o) const { return !(*this == o); }
};

/// Per-frame physically based shading configuration (like AOConfig there
/// is no GL-renderer counterpart). While enabled, lit triangle surfaces
/// use a metallic/roughness BRDF with image based lighting from a
/// backend-built environment instead of the default headlight shading.
struct PBRConfig {
    bool enabled = false;
    float metallic = 0.0f;      ///< surface metalness, 0..1
    /// Surface roughness, 0..1; 0 = automatic (derived per draw from the
    /// material's shininess).
    float roughness = 0.0f;
    float envIntensity = 1.0f;  ///< environment lighting brightness
    /// Draw the environment itself as the visible background (replaces
    /// the background gradient while PBR is active).
    bool envBackground = false;
    /// User environment image replacing the built-in procedural studio
    /// environment; null = procedural. A 2:1 image is read as
    /// equirectangular (lat-long), anything squarer as a GL sphere map
    /// — the same convention as the Texture mapping dialog's
    /// Environment mode (SoTextureCoordinateEnvironment), so the same
    /// file works in both.
    std::shared_ptr<const TextureImage> envImage;

    bool operator==(const PBRConfig &o) const {
        return enabled == o.enabled && metallic == o.metallic
            && roughness == o.roughness && envIntensity == o.envIntensity
            && envBackground == o.envBackground && envImage == o.envImage;
    }
    bool operator!=(const PBRConfig &o) const { return !(*this == o); }
};

/// Flattened per-draw render state, translated from the Coin-side material
/// (SoFCRenderCache::Material). Colors are packed 0xRRGGBBAA.
struct Material {
    enum Type : uint8_t { Triangle, Line, Point };
    enum DepthFunc : uint8_t {
        Never, Always, Less, LEqual, Equal, GEqual, Greater, NotEqual
    };

    uint8_t type = Triangle;
    uint8_t depthfunc = LEqual;
    bool depthtest = true;
    bool depthwrite = true;
    bool pervertexcolor = false;
    bool lighting = true;        ///< false = flat base color (no light model)
    bool twoside = false;
    bool culling = false;
    bool ccw = true;             ///< front face vertex ordering
    bool transparent = false;    ///< uniform-color / texture transparency
    bool ontop = false;          ///< render after (over) the normal scene
    bool polygonoffset = false;  ///< glPolygonOffset on filled triangles
    uint32_t diffuse = 0xCCCCCCFF;
    uint32_t emissive = 0;
    uint32_t specular = 0;
    uint32_t ambient = 0;
    /// Edge color override of a triangle material (hidden-line outline
    /// color falls back to diffuse when 0, like the GL renderer).
    uint32_t linecolor = 0;
    float shininess = 0.0f;
    float linewidth = 1.0f;
    float pointsize = 1.0f;
    /// Line stipple, glLineStipple encoding: low 16 bits = pattern (LSB
    /// drawn first), bits 16+ = pixel repeat factor (0/1 = one pixel per
    /// bit). 0xffff in the low bits = solid.
    uint32_t linepattern = 0xffff;
    /// Pattern of the depth-occluded (dimmed) pass of on-top lines; GL
    /// substitutes ViewParams::SelectionLinePattern there when the
    /// material has no pattern of its own.
    uint32_t hiddenlinepattern = 0xffff;
    /// glPolygonOffset(factor, units); positive pushes away from the viewer
    float polygonoffsetfactor = 0.0f;
    float polygonoffsetunits = 0.0f;
    /// Alpha used to dim the depth-occluded part of on-top lines/points
    /// (ViewParams::TransparencyOnTop); 1 = no dimming.
    float hiddenlinealpha = 1.0f;
    /// Line/point draw of a selection/preselection highlight (GL renders
    /// it under RenderPassHighlight): drawn after the uncolored on-top
    /// companion lines (GL bucket order selsontop -> selslineontop), and
    /// its depth-tested passes get a tiny toward-viewer bias so the
    /// thickened quad wins the depth tie against the object's own scene
    /// line the way GL's width-independent line rasterization does.
    bool highlightline = false;

    /// Stencil face outline of partial (per-face) triangle draws in the
    /// selection/highlight feeds (GL: RenderPassSelectionOutline): the
    /// face is drawn into the stencil buffer, then its triangle edges
    /// redraw as thick lines where the stencil does not match, leaving
    /// the boundary. The outline color is the material's emissive.
    bool faceoutline = false;   ///< outline partial triangle draws
    bool outlineonly = false;   ///< and skip their face fill
    float outlinewidth = 1.0f;  ///< outline width in pixels

    /// Hidden-line draw style material (SoFCRenderCache::Material::outline):
    /// whole-cache triangle draws get a stencil outline, and the active
    /// HiddenLineConfig's hideFace/hideSeam/hideVertex rules apply.
    bool outline = false;

    /// Shadow participation of a triangle draw, the Coin
    /// SoShadowStyleElement bitmask: 1 = casts shadows, 2 = receives
    /// (is shadowed). Only meaningful while a scene light is fed
    /// (LightConfig::valid).
    uint8_t shadowstyle = 3;

    /// Coin shape hints declare the geometry a closed solid
    /// (SoShapeHintsElement::SOLID); together with MeshData::hasSolid this
    /// gates the stencil section cap of clipped draws.
    bool solidshape = false;

    /// Per-object PBR parameters of a triangle draw (SoFCRenderMaterial,
    /// typically fed from ViewProvider Render_* properties); < 0 = unset,
    /// the frame's PBRConfig values apply. Only used while the PBR
    /// shading path is active.
    float metallic = -1.0f;
    float roughness = -1.0f;

    /// Water body flag of a triangle draw (SoFCRenderMaterial, typically
    /// fed from a ViewProvider Render_Water property): while the
    /// volumetric lighting pass is active, the draw's closed volume
    /// becomes a scattering medium tinted by the diffuse color instead
    /// of an ordinary surface — it neither ends volumetric rays nor
    /// casts shadows, and its front/back depths bound the underwater
    /// stretch of each view ray. waterdensity is the extinction density
    /// in inverse world units, <= 0 = automatic (from the draw bounds).
    bool water = false;
    float waterdensity = 0.0f;

    /// Glass body flag of a triangle draw (SoFCRenderMaterial, typically
    /// fed from a ViewProvider Render_Glass property): the draw leaves
    /// the ordinary (transparent) path and renders with screen-space
    /// refraction, Fresnel-blended environment reflection and
    /// per-channel Beer-Lambert absorption tinted by the diffuse color
    /// over the body thickness (front/back depth interval). glassior
    /// <= 0 = default 1.5; glassdensity <= 0 = automatic (from the draw
    /// bounds); glassroughness in 0..1 blurs the reflection.
    bool glass = false;
    float glassior = 0.0f;
    float glassdensity = 0.0f;
    float glassroughness = 0.0f;

    /// Cloud body flag of a triangle draw (SoFCRenderMaterial, typically
    /// fed from a ViewProvider Render_Cloud property): while the
    /// volumetric lighting pass is active the draw's closed volume
    /// raymarches as a procedural-density (FBM) scattering medium and
    /// the geometry itself is not rendered. clouddensity/clouddetail
    /// <= 0 = automatic (from the draw bounds); cloudspeed scales the
    /// drift animation of the noise domain.
    bool cloud = false;
    float clouddensity = 0.0f;
    float clouddetail = 0.0f;
    float cloudspeed = 1.0f;

    /// Fire body flag of a triangle draw (SoFCRenderMaterial, typically
    /// fed from a ViewProvider Render_Fire property): while the
    /// volumetric lighting pass is active the draw's closed volume
    /// raymarches as an emissive flame medium (rising FBM noise through
    /// a blackbody-style color ramp, added on top of the scene) and the
    /// geometry itself is not rendered. fireintensity <= 0 = 1 (a plain
    /// brightness multiplier); firedetail <= 0 = automatic (from the
    /// draw bounds); firespeed scales the rise animation.
    bool fire = false;
    float fireintensity = 0.0f;
    float firedetail = 0.0f;
    float firespeed = 1.0f;
    /// Fountain body: the closed volume raymarches as a water-spray
    /// scattering medium of the volumetric pass (a rising jet plus a
    /// parabolic fall envelope with streak noise, splash rings fed to
    /// the water surface below) and the geometry itself is not
    /// rendered. fountaindensity/fountaindetail <= 0 = automatic (from
    /// the draw bounds); fountainspeed scales the flow animation.
    bool fountain = false;
    float fountaindensity = 0.0f;
    float fountaindetail = 0.0f;
    float fountainspeed = 1.0f;

    /// Light-source body flag of a triangle draw (SoFCRenderMaterial,
    /// typically fed from a ViewProvider Render_Light property): the
    /// geometry renders unshaded at its diffuse color (a glowing bulb /
    /// sun disc), feeds the bloom pass at lightintensity (an HDR
    /// multiplier — the halo scales with it even though the scene
    /// target clips at 1), and acts as an unshadowed point light on
    /// lit surfaces around it (the fire-light shortcut — no shadow map
    /// from it). lightintensity <= 0 = 1; lightrange <= 0 = automatic
    /// (from the draw bounds).
    bool lightsource = false;
    float lightintensity = 0.0f;
    float lightrange = 0.0f;
    /// The light-source body casts shadows from its point light: the
    /// backend renders a cached shadow-map tile for it (a downward
    /// wide-cone view; static scenes re-render it only on change).
    /// Off by default — a tile costs a scene depth render on every
    /// invalidation.
    bool lightshadow = false;
    /// Extended (omnidirectional) light shadow: the tile becomes six
    /// cube faces around the bulb, so geometry in any direction can
    /// shadow the light (not just below it). Costs up to six cached
    /// tile renders per invalidation; off by default.
    bool lightshadowext = false;

    /// User "material"-stage shader of a triangle draw (a scene
    /// SoShaderProgram node with stage="material" captured in the same
    /// render cache as the shape, docs/RenderDebug.md §6): the backend
    /// substitutes the program for the standard mesh fragment stage in
    /// the beauty passes (depth prepass, shadows and picking keep the
    /// stock shaders). Shared with the producing cache; pointer
    /// identity doubles as the draw-batch key.
    std::shared_ptr<const UserShader> usershader;

    /// Texture of a triangle draw (unit 0 only; GL applies further units
    /// on top, a known deviation) with its texture matrix, applied to
    /// MeshData::texCoords. Only sampled when the mesh carries texture
    /// coordinates.
    std::shared_ptr<const TextureImage> texture;
    float texmatrix[16];        ///< GL-layout, valid when !texidentity
    bool texidentity = true;

    /// Bump map of a triangle draw (unit-0 SoBumpMap): 1/2-component
    /// images perturb as grayscale height maps (parallax-occlusion
    /// mapped when BumpConfig::parallax), 3/4-component images are
    /// tangent-space normal maps (Coin's convention). The tangent frame
    /// comes from screen-space derivatives, so any UV source works —
    /// but the mesh must carry MeshData::texCoords, which only an
    /// enabled texture unit provides (pair bump-only scenes with a
    /// plain white texture). The wrap fields apply; model/blendColor
    /// are ignored.
    std::shared_ptr<const TextureImage> bumpmap;

    /// Emissive/occlusion material maps of a triangle draw
    /// (SoFCRenderTexture, typically fed from ViewProvider
    /// Render_EmissiveMap/Render_OcclusionMap properties). The emissive
    /// map's rgb adds to the lit (and textured) fragment color; the
    /// occlusion map's first channel multiplies the ambient/environment
    /// light contribution. Sampled with MeshData::texCoords like the
    /// bump map (same white-stand-in caveat); wrap fields apply,
    /// model/blendColor are ignored.
    std::shared_ptr<const TextureImage> emissivemap;
    std::shared_ptr<const TextureImage> occlusionmap;

    /// glTF metallic-roughness map of a triangle draw (SoFCRenderTexture,
    /// typically fed from a ViewProvider Render_MetallicRoughnessMap
    /// property): the green channel multiplies the roughness factor and
    /// the blue channel the metallic factor (glTF semantics) — only used
    /// while the PBR shading path is active. Sampling caveats as above.
    std::shared_ptr<const TextureImage> metallicroughnessmap;

    /// Autozoom transforms (SoAutoZoomTranslation): the draw's model
    /// matrix is rebuilt every frame by replaying these entries like the
    /// GL renderer's setupMatrix — accumulate each entry's matrix (or
    /// reset to it), then substitute the accumulated scale with
    /// scaleFactor times the per-frame world-to-screen scale fed through
    /// Renderer::setAutoZoomScale() (a scaleFactor of 0 keeps scale 1) —
    /// and DrawCall::model multiplies in last. Matrices are GL-layout
    /// like DrawCall::model.
    struct AutoZoomEntry {
        float matrix[16];
        float scaleFactor = 1.0f;
        bool identity = true;
        bool resetmatrix = false;
        /// Screen-align (billboard) the draw: the backend substitutes the
        /// upper 3x3 with the camera basis (so the quad always faces the
        /// viewer, like SoText2) instead of keeping the model rotation. Used
        /// by SoTextImage glyph quads.
        bool billboard = false;
        /// Billboard draws only: on-screen pixels per emitted geometry unit.
        /// 0 keeps the backend's glyph-legibility text factor; image quads
        /// emitted in native pixels (SoImage capture companions) use 1 for
        /// raw-GL pixel parity.
        float pixelscale = 0.0f;
        /// Datum-label auto-flip: keep the glyph in its dimension plane, but
        /// mirror its local X/Y per frame so the number always reads
        /// left-to-right / upright from the current viewpoint (SoDatumLabel's
        /// GLRender does this via a projected-axis + backfacing test). `normal`
        /// is the world-space plane normal used for the backfacing decision.
        bool datumFlip = false;
        float normal[3] = {0.f, 0.f, 1.f};
    };
    std::vector<AutoZoomEntry> autozoom;

    /// World-space clip plane equations (sections). A fragment survives
    /// when dot(pos, plane.xyz) + plane.w >= 0 holds for every plane, or,
    /// in concave mode, for at least one plane (GL parity: SectionConcave
    /// renders one enabled plane per pass, i.e. the union of half-spaces).
    static constexpr int MaxClipPlanes = 6;
    uint8_t numclipplanes = 0;
    bool clipconcave = false;
    float clipplanes[MaxClipPlanes][4];
};

/// One draw of (a part of) a mesh with a material and model transform.
struct DrawCall {
    Material material;
    /// Index of `material` in the snapshot's material table, or -1.
    /// Load-side only (SceneDump v31): when the table itself is served
    /// out of band, it is what lets the draws be built before their
    /// materials have arrived. Never set by the live desktop feed.
    int32_t materialIndex = -1;
    std::shared_ptr<const MeshData> mesh;
    float model[16];        ///< GL-style layout, valid when !identity
    bool identity = true;
    /// Content hash of the scene-graph node path that produced this draw;
    /// the same object yields the same key in the scene and the
    /// selection/highlight feeds. 0 = unknown.
    uint64_t objectKey = 0;
    /// True when this draw covers the object's whole geometry of its
    /// primitive type (a whole-object on-top selection/highlight draw
    /// replaces — hides — the object's scene draws with equal objectKey).
    bool wholeObject = false;
    int partIndex = -1;     ///< -1 = whole mesh, >= 0 = single face/edge part
    /// Index range of the draw inside the index buffer selected by
    /// material.type. Resolved from partIndex by the producer; count 0
    /// means the whole buffer.
    int indexStart = 0;
    int indexCount = 0;
    float bboxMin[3] = {0.0f, 0.0f, 0.0f};  ///< world space bounds,
    float bboxMax[3] = {0.0f, 0.0f, 0.0f};  ///< empty if min > max
    /// This draw is a coarse stand-in for geometry that has not arrived:
    /// a unit box scaled onto the bounds above, the bottom rung of the
    /// fidelity ladder (docs/SceneStreaming.md §6). It occupies space —
    /// it writes depth, including in the prepass — but it is not the
    /// shape, so it casts no shadow and takes no outline, capping or
    /// hidden-line pass: those are shading, not occupancy.
    ///
    /// Consumer-side only. A stand-in is synthesised by whoever is
    /// waiting, from the bounding box the manifest carries anyway, and
    /// is never serialized: nothing on the wire ever claims to be the
    /// mesh it stands in for.
    bool standIn = false;
};

typedef std::vector<DrawCall> DrawCallList;

/// The document object behind an objectKey. Everything is a plain
/// string: the renderer and the serving path must stay free of App/Gui
/// types.
///
/// Two halves with different lifetimes, and the split is the point:
///
/// - `doc` + `obj` are the **identity**, and they are fixed. An internal
///   name never changes, so the producer reads both straight off the
///   cache key's origin (setObjectInfo()) without touching a document.
/// - `label` + `type` are **presentation**, for a viewer to show a
///   human a name. They have nothing to do with a mesh, so they are not
///   resolved on the publish path: the serving path fills them from an
///   ObjectMetaMap (setObjectMeta()) that changes only when a document
///   does. A publish that nobody serves resolves neither.
///
/// ⚠️ Every one of these strings is UTF-8 and may hold any character a
/// Python identifier may — internal names included. Nothing here may be
/// byte-inspected, case-folded or truncated.
struct ObjectInfo {
    std::string doc;    ///< document internal name (identity)
    std::string obj;    ///< object internal name (identity)
    std::string label;  ///< user-visible label, presentation only
    std::string type;   ///< DocumentObject type id, e.g. "Part::Box"
};

typedef std::unordered_map<uint64_t, ObjectInfo> ObjectInfoMap;

/// What a viewer needs to *name* an object to a human. Not identity,
/// not geometry: a rename changes this and nothing else.
struct ObjectMeta {
    std::string label;  ///< the object's Label at the time it was pushed
    std::string type;   ///< DocumentObject type id
};

/// Presentation metadata by document internal name, then object
/// internal name. Nested rather than a joined key precisely because
/// both names are arbitrary UTF-8: there is no separator byte that
/// cannot occur in a name.
typedef std::unordered_map<std::string,
        std::unordered_map<std::string, ObjectMeta>> ObjectMetaMap;

/// Flag bits of the selection ids fed through Renderer::addSelection
/// (mirroring SoFCRenderer::SelIdBits — the producer side of the feed).
enum SelIdBits : int {
    SelIdImplicit = 0x01000000,  ///< whole-object brought along a partial
    SelIdAlt      = 0x02000000,  ///< alternative (Ctrl) selection group
    SelIdFull     = 0x04000000,  ///< explicit whole-object selection
    SelIdPartial  = 0x08000000,  ///< sub-element selection
    SelIdSelected = SelIdFull | SelIdPartial,
};

class RendererExport Renderer
{
public:
    Renderer();
    virtual ~Renderer();

    /// Unique for the lifetime of the process, and never reused. A
    /// producer that keeps state about what a renderer has already been
    /// told has to store this next to it: a renderer's *address* is
    /// reused freely — a backend torn down and rebuilt on a preference
    /// change can land where the last one was — and a producer comparing
    /// pointers would go on sending deltas against a table the new
    /// renderer never received.
    uint64_t instanceId() const { return instanceid; }

    /// How many times this renderer's identity table has been stated
    /// whole (setObjectInfo). Together with instanceId() this is the
    /// token a producer stores beside its resident copy, so that anything
    /// replacing the table behind the producer's back is a mismatch on
    /// the next publish rather than a silently incomplete table.
    uint32_t objectInfoVersion() const { return infoversion; }

    virtual const std::string &type() const = 0;
    virtual bool render(const QColor &bg,
                        const void *viewMatrix,
                        const void *projMatrix) = 0;
    virtual bool boundBox(float &xmin, float &ymin, float &zmin,
                          float &xmax, float &ymax, float &zmax) = 0;

    /// Serialize the feeds and hand them to the scene-stream server,
    /// without drawing anything (docs/HeadlessServe.md §3.1). This is
    /// what render() does on the way past on a serving process; a
    /// publish-only renderer -- one with no GL widget and no graphics
    /// device behind it -- can only do this. \a viewMatrix and \a
    /// projMatrix are the camera a joining viewer adopts before it
    /// frames the scene itself, and the viewport is what those
    /// matrices were built for. Returns false when the backend cannot
    /// publish (default) or the feeds have not changed.
    virtual bool publish(const QColor &bg,
                         const void *viewMatrix,
                         const void *projMatrix,
                         int width, int height)
    {
        (void)bg; (void)viewMatrix; (void)projMatrix;
        (void)width; (void)height;
        return false;
    }

    /// Which document group this renderer's publishes belong to on the
    /// scene-stream server (docs/MultiDocServe.md §3). Empty -- the
    /// default -- publishes into the server's default group, which is
    /// the single-document behavior; a view-less serve source names its
    /// document here so a second document is a second group rather than
    /// a fight over one. Set before the first publish.
    virtual void setPublishGroup(const std::string &doc) { (void)doc; }

    /// Whether the frame just rendered contains time-animated content
    /// (e.g. water caustics): the viewer keeps scheduling redraws while
    /// this returns true, so the animation advances without user input.
    virtual bool animating() const { return false; }

    /// Global hint whether the active backend supports GPU-instanced
    /// draws. Geometry producers (e.g. the Part tessellation) consult it
    /// before emitting shared-instance scene structure: without real
    /// instancing many small shared nodes are a net loss, so they keep
    /// flattening instead. Defaults to true; a backend publishes its
    /// actual capability once initialized.
    static void setInstancingHint(bool supported);
    static bool instancingHint();

    /// Number of live backend renderer instances in the process. This —
    /// not any preference string — is the truth about whether a backend
    /// is active: a backend can be attached with the type preference
    /// still "Default" (per-view or scripted selection), and a
    /// preference naming a backend yields none when creation fails
    /// (plain-GL fallback).
    static int activeCount();
    /// Register a callback fired whenever activeCount() or
    /// instancingHint() changes. Observers are never removed — register
    /// only from static-lifetime contexts. Fires on the thread doing the
    /// change (backend create/destroy happens on the GUI thread).
    static void addActivityObserver(std::function<void()> observer);

    /// \name Scene API
    /// Mirrors SoFCRenderer's feed. Backends that don't consume scene data
    /// keep the default no-ops and canSkipInternal() == false, so the
    /// existing GL pipeline continues to draw the scene.
    //@{
    /// Replace the whole scene. An empty list clears it.
    virtual void setScene(DrawCallList &&draws) { (void)draws; }
    /// Which document object each objectKey renders, resolved by the
    /// scene producer (the renderer itself has no document access — this
    /// library stays App-free). Replaced wholesale alongside setScene();
    /// keys the producer could not name are simply absent. Consumed by
    /// the scene-serving snapshot so a remote viewer can name what it
    /// picks (docs/ThinClient.md §4.1).
    virtual void setObjectInfo(ObjectInfoMap &&info) { (void)info; }
    /// Add identities the renderer does not have yet, leaving the rest of
    /// the table alone. What an objectKey renders is *fixed* -- a document
    /// and an object internal name, neither of which can change -- so a
    /// publish has nothing to correct here, only new keys to announce.
    /// Rebuilding the whole map to hand it over cost an insert and two
    /// string copies per object on every publish for a table that was
    /// already right (docs/IncrementalPublish.md §4d-iv).
    /// setObjectInfo() remains the way to state the whole table: the first
    /// publish, a renderer that has just been attached, and whenever the
    /// producer drops its resident copy rather than let it grow.
    virtual void updateObjectInfo(ObjectInfoMap &&added) { (void)added; }
    /// Presentation metadata (label, type) for the objects this renderer
    /// publishes, by document and object internal name. Pushed by the
    /// serving source when a document changes it — a rename, an object
    /// added or removed — and NOT per publish: nothing here describes a
    /// mesh, and re-deriving it per publish cost a document lookup per
    /// draw for a table only a serving viewer reads. A renderer nobody
    /// serves is never given one, and its published entries carry
    /// identity alone.
    virtual void setObjectMeta(ObjectMetaMap &&meta) { (void)meta; }
    /// Apply a change to that metadata: \a changed replaces or adds the
    /// entries it names, \a removed drops {document, object} pairs. The
    /// renderer holds the resident table, so a rename in a large
    /// document sends one entry rather than all of them — and a live
    /// import announcing thousands of new objects sends what arrived
    /// since the last publish rather than everything so far, every
    /// frame. setObjectMeta() remains the way to state the whole table,
    /// for the first push and whenever the producer cannot say what
    /// changed.
    virtual void updateObjectMeta(
            ObjectMetaMap &&changed,
            const std::vector<std::pair<std::string, std::string>> &removed)
    { (void)changed; (void)removed; }
    /// Describe the window background for the next render(). The bg color
    /// passed to render() stays the clear-color fallback for backends that
    /// ignore this.
    virtual void setBackground(const Background &bg) { (void)bg; }
    /// Add/replace one selection identified by id (SoFCRenderer::SelIdBits).
    virtual void addSelection(int id, DrawCallList &&draws)
    { (void)id; (void)draws; }
    virtual void removeSelection(int id) { (void)id; }
    /// Set the preselection highlight geometry.
    virtual void setHighlight(DrawCallList &&draws, bool wholeOnTop)
    { (void)draws; (void)wholeOnTop; }
    virtual void clearHighlight() {}
    /// Add/replace one overlay draw list keyed by \a id: viewport-anchored
    /// content (corner axis cross, foreground superimposition) drawn after
    /// the whole scene against a fresh depth buffer, with viewport and
    /// camera derived from \a anchor each frame. An empty list removes the
    /// overlay, same as removeOverlay().
    virtual void setOverlay(int id, DrawCallList &&draws,
                            const OverlayAnchor &anchor)
    { (void)id; (void)draws; (void)anchor; }
    virtual void removeOverlay(int id) { (void)id; }
    /// Per-frame hidden-line draw style state (resolved from the traversal
    /// state each render, like the GL renderer does).
    virtual void setHiddenLineConfig(const HiddenLineConfig &config)
    { (void)config; }
    /// Per-frame section fill (cap) configuration.
    virtual void setSectionConfig(const SectionConfig &config)
    { (void)config; }
    /// Per-frame ambient occlusion configuration.
    virtual void setAOConfig(const AOConfig &config) { (void)config; }
    /// Per-frame render debugging configuration (docs/RenderDebug.md).
    virtual void setRenderDebugConfig(const RenderDebugConfig &config)
    { (void)config; }
    /// Per-frame occlusion culling configuration
    /// (docs/FarFieldProxies.md §12).
    virtual void setOcclusionCullConfig(const OcclusionCullConfig &config)
    { (void)config; }
    /// User-loadable shaders captured from scene SoShaderProgram nodes
    /// (docs/RenderDebug.md §6).
    virtual void setUserShaderConfig(const UserShaderConfig &config)
    { (void)config; }
    /// Whether the last rendered frame contained time-animated content
    /// (water waves, fire, clouds, caustics) — a repeat frame with the
    /// same camera and scene would differ. Clients that skip rendering
    /// while idle must keep rendering while this is true; the default
    /// is conservatively true.
    virtual bool isSceneAnimated() const { return true; }
    /// Whether scene/config changes are pending that the next render
    /// would pick up (selection, highlight, configs, a new scene feed).
    /// Clients that skip rendering while idle must render while this is
    /// true; the default is conservatively true.
    virtual bool isSceneDirty() const { return true; }
    /// Per-frame physically based shading configuration.
    virtual void setPBRConfig(const PBRConfig &config) { (void)config; }
    /// Per-frame bump/normal mapping configuration.
    virtual void setBumpConfig(const BumpConfig &config) { (void)config; }
    /// Per-frame scene light (Shadow draw style).
    virtual void setLightConfig(const LightConfig &config) { (void)config; }
    /// Per-frame volumetric lighting (light shaft) configuration.
    virtual void setVolumetricConfig(const VolumetricConfig &config)
    { (void)config; }
    /// Per-frame water surface (refraction/reflection) configuration.
    virtual void setWaterConfig(const WaterConfig &config)
    { (void)config; }
    /// Per-frame bloom (glow) configuration.
    virtual void setBloomConfig(const BloomConfig &config)
    { (void)config; }
    /// Preselection (hover) highlight styling from ViewParams; carried in
    /// the snapshot for the standalone/WASM viewer's local hover highlight.
    virtual void setPreselConfig(const PreselHighlightConfig &config)
    { (void)config; }
    /// Selection highlight styling from ViewParams; carried in the snapshot
    /// for the standalone/WASM viewer's local (client-side) selection.
    virtual void setSelConfig(const PreselHighlightConfig &config)
    { (void)config; }
    /// Per-frame world-to-screen scale at the world origin consumed by
    /// Material::autozoom draws (Coin: SoAutoZoomTranslation's
    /// getWorldToScreenScale((0,0,0), 0.1) / (5 * viewport aspect)).
    /// Resolved from the traversal state each render like the
    /// hidden-line config, so it applies one frame late too.
    virtual void setAutoZoomScale(float scale) { (void)scale; }
    /// Scene multisample (MSAA) sample count for the backend's own render
    /// target (0/1 = off). Backends that render into an offscreen buffer own
    /// their MSAA independently of the host GL context, so a preference change
    /// is applied here rather than by recreating the view. Takes effect when
    /// the render target is next (re)created.
    virtual void setMSAASamples(int samples) { (void)samples; }
    /// Resolution scale (0.25-1.0) of the expensive screen-space effect
    /// passes -- the planar/ground reflection scene re-render and the SSAO
    /// resolve -- relative to the main view resolution. Lowering it trades
    /// effect sharpness for speed on large windows where those per-pixel
    /// passes dominate; the main scene, geometry prepass and overlays stay
    /// full resolution. Takes effect when the targets are next (re)created.
    virtual void setEffectResolution(float scale) { (void)scale; }
    /// Resolution scale (0.25-1.0) of the SSAO resolve targets, independent
    /// of setEffectResolution -- ambient occlusion is resolution-sensitive,
    /// so it has its own control. Takes effect when the targets are next
    /// (re)created.
    virtual void setSSAOResolution(float scale) { (void)scale; }
    /// Screen-space error tolerance in pixels of the desktop mesh-level
    /// plan (docs/SceneStreaming.md §13): a coarse-first tessellation
    /// whose stated error projects to more than this many pixels is
    /// re-tessellated exactly when the camera settles on it; 0 or less
    /// refines everything as soon as it appears. Only meaningful on a
    /// backend that drivesMeshLevels().
    virtual void setLevelTolerance(float px) { (void)px; }
    /// Whether this backend runs the desktop mesh-level plan pass (§13):
    /// a coarse-first display build (MeshSourceRegistry with a refine
    /// callback) only climbs back to exact when something plans it, so
    /// the coarse-first tessellation must not engage for a desktop view
    /// whose backend answers false here — the build would simply stay
    /// coarse.
    virtual bool drivesMeshLevels() const { return false; }
    /// GPU geometry budget in bytes for the desktop mesh-level plan
    /// (§13 step 3): over it, the plan downgrades the *displayed* rung
    /// of sources the camera would not miss — their exact meshes stay
    /// in CPU RAM, so the climb back is an instant re-activation. 0 =
    /// automatic: the backend's own reported GPU memory limit where
    /// the API states one (D3D/Vulkan do), else no budget at all.
    virtual void setGpuMemoryBudget(size_t bytes) { (void)bytes; }
    /// Narrate what each mesh-level plan decides. Needed to tell a
    /// ladder that will not descend apart from one that never ran --
    /// on desktop OpenGL the automatic GPU budget is 0, so the
    /// downgrade half of the plan had never executed and nothing said
    /// so.
    virtual void setLevelDebug(bool on) { (void)on; }
    /// How much of the error the plan is holding back to fit its GPU
    /// budget survives each plan that fits (Render::PressureTolerance,
    /// sec 13c.3). Handing it all back at the first plan inside the budget
    /// is what made this ladder cycle: the tolerance fell from 51px to
    /// 2px in one step, 946 objects re-tessellated at once, and the
    /// budget broke again. 0 or less restores that snap.
    virtual void setLevelPressureRelease(float fraction) { (void)fraction; }
    /// Whether the GPU downgrade sweep carries its unlanded orders as
    /// credit against the next plans' deficits (Render_DowngradeLedger;
    /// see Render::DowngradeLedger for why a sweep without one storms).
    virtual void setDowngradeLedger(bool on) { (void)on; }
    /// The hard-ceiling admission for climbs (Render_ClimbHardLimit /
    /// Render_ClimbAdmitBatch): at or over the budget the plan admits
    /// no refine and aborts those in flight; under it, climbs are
    /// admitted in batches of \a batch so the allocator-exact total
    /// approaches the ceiling in verified steps.
    virtual void setClimbAdmission(bool hardLimit, int batch)
    { (void)hardLimit; (void)batch; }
    /// How many descents (demotes/downgrades) one plan pass may order
    /// (Render_DescentOrderBatch, the climb batch's mirror): each
    /// order enqueues a worker job but pays a GUI-thread snapshot at
    /// the hook, so a pass is bounded and the replan after the batch
    /// lands takes the rest. 0 or less removes the cap.
    virtual void setDescentOrderBatch(int batch) { (void)batch; }
    /// The rest band above the GPU budget, as a fraction of it
    /// (Render_LevelBudgetDeadband): the downgrade sweep triggers only
    /// past budget*(1+fraction) and still corrects back to the budget,
    /// so an equilibrium that lands just over the line may stand --
    /// climbs already stop at the budget, and inside the band neither
    /// direction acts. 0 restores the bare line and with it the
    /// boundary dither.
    virtual void setLevelBudgetDeadband(float fraction) { (void)fraction; }
    /// The display gates of the memory response
    /// (docs/SceneStreaming.md #13b), pushed in like every other
    /// parameter -- this library knows nothing of RenderParams.
    /// \a shapeVertices false suppresses point drawables whose every
    /// vertex sits on an edge that is itself drawn; \a pressureEdges
    /// true suppresses line drawables whose every edge bounds a drawn
    /// face, and only while the GPU budget stands exceeded;
    /// \a loadingDrop true suppresses both classes outright, and is
    /// what a host asserts while a document is still arriving. Whether
    /// a document is loading is a question only the host can answer, so
    /// it is pushed as a state and not derived here. None of the three
    /// ever touches an on-top or highlight draw.
    virtual void setElementGates(bool shapeVertices, bool pressureEdges,
                                 bool loadingDrop)
    { (void)shapeVertices; (void)pressureEdges; (void)loadingDrop; }
    /// Section cap hatch texture pixels; \a nc-component 8-bit rows,
    /// tightly packed. Null data clears the texture. The pixels are copied.
    virtual void setHatchImage(const void *data, int nc,
                               int width, int height)
    { (void)data; (void)nc; (void)width; (void)height; }
    /// Arm a one-shot frame capture (docs/RenderDebug.md §4): the next
    /// rendered frame reads back the backend's scene color target
    /// (pre-composite) and, if the request carries a path, writes the
    /// image there; the readback statistics land in getRenderStats()
    /// either way. Returns false when the backend has no capture path
    /// (default, and the standalone build where the host reads the
    /// backbuffer itself).
    virtual bool requestFrameDump(const FrameDumpRequest &req)
    { (void)req; return false; }
    /// True while an armed frame dump has not been consumed by a
    /// rendered frame yet — the caller pumps frames until this clears.
    virtual bool frameDumpPending() const { return false; }
    /// Statistics of the last frame readback (a consumed frame dump);
    /// false while none has run.
    virtual bool getRenderStats(RenderStats &stats) const
    { (void)stats; return false; }
    /// Reload the backend's shader programs from disk on the next
    /// rendered frame (docs/RenderDebug.md §3): with FC_BGFX_SHADER_DIR
    /// pointing at a development asset tree, recompiling a shader
    /// (shaders/compile.sh) followed by this call is a live
    /// edit-save-see loop. Returns false when the backend has no
    /// reloadable shaders (default).
    virtual bool reloadShaders() { return false; }
    /// True if scene data changed after the last render() and another
    /// frame should be scheduled.
    virtual bool needsRedraw() const { return false; }
    /// True when this backend has rendered the current scene and the
    /// internal fixed-function GL pass can be skipped.
    virtual bool canSkipInternal() const { return false; }
    //@}

protected:
    /// Record that the identity table has just been stated whole, so a
    /// producer holding a resident copy of it can tell. Every override of
    /// setObjectInfo() owes this call; updateObjectInfo() must not make
    /// it, because a delta leaves the producer's copy still describing
    /// what the renderer holds.
    void noteObjectInfoStated() { ++infoversion; }

private:
    static uint64_t nextInstanceId()
    {
        static std::atomic<uint64_t> counter{0};
        return ++counter;
    }
    const uint64_t instanceid = nextInstanceId();
    uint32_t infoversion = 0;
};

class RendererLib
{
public:
    virtual ~RendererLib() {}
    virtual const std::string &name() const = 0;
    virtual const std::vector<std::string> &types() const = 0;
    virtual std::unique_ptr<Renderer> create(
            const std::string &type, QOpenGLWidget *widget,
            bool publishOnly = false) const = 0;
};

/// CPU accounting for the part of a frame that is *not* the renderer's.
///
/// The frame line splits its CPU into three: our own C++, the bgfx call
/// it ends in, and `outside` -- everything else between one backend
/// frame and the next. `outside` is derived (frame minus ours), so it is
/// complete by construction but says nothing about what is in it, and on
/// a native run it is the largest of the three. This is what puts an
/// instrument on it.
///
/// It lives here, as a process-wide accumulator rather than a method on
/// Renderer, because the code being timed is Gui's -- Coin's composite,
/// Qt's paint, the overlays -- and runs after render() has returned. The
/// backend drains it on the same tick it prints the frame line, and the
/// remainder it cannot attribute is printed too: a breakdown of a
/// derived quantity is only trustworthy if it says how much it missed.
class RendererExport FrameOutside
{
public:
    enum Phase {
        /// renderScene() before the backend call: viewport, background
        /// colour resolution, the meta feed.
        Pre,
        /// The background root traversal (and the GL background fill on
        /// frames the backend did not draw).
        Background,
        /// inherited::actualRedraw() -- the whole Coin scene-graph
        /// traversal. At render-cache mode 3 the geometry went to the
        /// backend, so this ought to be compositing overlays and little
        /// else; if it is not, that is a bug, not a tuning knob.
        Coin,
        /// The foreground root traversal.
        Foreground,
        /// Overlay captures re-traversed and fed to the backend.
        Captures,
        /// The chrome after them: axis cross, dimension text, navigation
        /// redraw, graphics items, fps string, NaviCube, alpha fixup.
        Chrome,
        /// QuarterWidget::paintEvent() before the redraw, minus the
        /// delay queue below.
        PaintPre,
        /// Coin's delay queue, drained at the top of the paint event
        /// with the GL context released around it.
        DelayQueue,
        /// QGraphicsView::paintEvent() -- Qt's own painting over the GL
        /// viewport, which runs after the scene is drawn.
        GraphicsView,
        /// The tail of the paint event after that.
        PaintPost,
        PhaseCount
    };

    /// Whether anything is draining. False costs one relaxed load, which
    /// is what makes it safe to leave the calls in the render path.
    static bool enabled() { return active.load(std::memory_order_relaxed); }
    static void setEnabled(bool on);
    static void add(Phase p, double ms);
    /// Move the accumulated per-phase milliseconds out, zeroing them.
    /// \a out is indexed by Phase and always fully written.
    static void drain(double *out);

private:
    static std::atomic<bool> active;
    static double phaseMs[PhaseCount];
};

/// Times a scope into a FrameOutside phase. Reads the switch once, on
/// entry, so a scope that spans the frame where it is turned on is
/// either wholly counted or wholly not -- never half.
class RendererExport FrameOutsideScope
{
public:
    explicit FrameOutsideScope(FrameOutside::Phase p);
    ~FrameOutsideScope();
    /// End the region early and disarm. For a span that starts at the
    /// top of a function and ends in its middle, where a nested block
    /// would put the locals it declares out of reach of the rest.
    /// Several scopes may name the same phase; they add.
    void stop();
    FrameOutsideScope(const FrameOutsideScope &) = delete;
    FrameOutsideScope &operator=(const FrameOutsideScope &) = delete;

private:
    FrameOutside::Phase phase;
    bool on;
    int64_t t0;
};

class RendererExport RendererFactory
{
public:
    static std::vector<std::string> types();
    /// \a publishOnly asks for a renderer that will never draw: no
    /// graphics device is created and none is required, so the process
    /// needs no GPU and no display (docs/HeadlessServe.md §3.1). Such a
    /// renderer answers publish() and nothing else. It is a mode of its
    /// own rather than "widget == nullptr", which the standalone viewer
    /// already passes for a renderer that very much does draw.
    static std::unique_ptr<Renderer> create(const std::string &type,
                                            QOpenGLWidget *widget,
                                            bool publishOnly = false);
    static void registerLib(RendererLib *);
    static void setResourcePath(const std::string &path);
    static const std::string &resourcePath();
};

} // namespace Render

#endif // RENDERER_RENDERER_H
