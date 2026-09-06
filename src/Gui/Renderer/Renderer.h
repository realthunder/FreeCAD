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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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
class DrawDevice;
class DrawSurface;

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
    /// Bytes per vertex of the material stream below.
    static constexpr int MaterialStride = 12;

    /// Per-vertex material stream of a per-face-material cache, 12 bytes
    /// per vertex: rgba8 emissive, then rgb8 specular with the
    /// shininess (0..1) quantized in the last byte, then the surface
    /// finish palette index (Material::finishpalette) in one byte, the
    /// projection frame palette index (Material::framepalette) in the
    /// next, the per-face TEXTURE layer (Material::texturepalette, 0 =
    /// untextured) in the third and one reserved after it. Null for
    /// uniform objects (then the Material scalars apply). Draws consume
    /// it only when their material sets perfacematerial.
    const uint8_t *materials = nullptr;

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

    /// What one component of \ref pixels IS.
    ///
    /// A photograph is display referred and fits in a byte. A captured
    /// ENVIRONMENT is not: the sky is thousands of times brighter than
    /// the wall below it, and that ratio is the whole reason an image
    /// based light looks like a place rather than like a picture.
    /// Clamped into a byte there can be no sun, which is why the
    /// built-in environment is procedural.
    ///
    /// F32 keeps the payload in \ref pixels as little-endian floats --
    /// four bytes a component, same packing, same row order -- so the
    /// content key, the blob store and every deduplication that hashes
    /// those bytes go on working untouched. The values are LINEAR
    /// radiance already and are never decoded.
    enum Sample : uint8_t { U8, F32 };
    uint8_t sample = U8;
    /// Bytes per component of \ref pixels.
    size_t sampleSize() const { return sample == F32 ? 4u : 1u; }
    /// Read one component as linear light, whatever it is stored as.
    float component(size_t index) const {
        if (sample == F32) {
            float v = 0.0f;
            std::memcpy(&v, pixels.data() + index * 4u, 4u);
            return v;
        }
        return pixels[index] / 255.0f;
    }

    std::vector<uint8_t> pixels;
    /// The file \ref pixels were decoded from, as authored -- a JPEG or
    /// a PNG (ImageDecode.h says which), kept beside the pixels by a
    /// producer that read one. Consumers read \ref pixels; this is for
    /// the TRANSPORT, which ships it instead of the pixels when it is
    /// here: a 2k map is a few hundred kilobytes as a file and sixteen
    /// megabytes decoded, and a document's worth of maps is the
    /// difference between a scene that streams and one that does not
    /// (docs/MaterialStorage.md sec 17.23). The content key is then the
    /// key of these bytes. Empty for a texture nobody read from such a
    /// file -- a Coin texture, a rendered palette, a Radiance picture.
    std::vector<uint8_t> encoded;

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
    /// The deferred payload is an encoded file (see \ref encoded), to be
    /// decoded into `pixels` when it lands rather than copied.
    bool encodedPayload = false;
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
    /// Which sub-view this feed belongs to (Renderer::renderSubViews).
    /// 0 -- the default -- means every sub-view, which is one viewer's
    /// chrome shown in all of them and what a plain render() draws. A
    /// non-zero id is one cell's OWN chrome (its NaviCube, its corner
    /// axis cross), drawn only in that cell's sub-view frame: the
    /// split-view unified canvas gives every cell a bank of one backend,
    /// so without this every cell would draw the feeding cell's cube,
    /// turned by the feeding cell's camera (docs/SplitViews.md sec 16.3).
    int subView = 0;

    bool operator==(const OverlayAnchor &o) const {
        return corner == o.corner && sizeFraction == o.sizeFraction
            && fovDeg == o.fovDeg && orthoHeight == o.orthoHeight
            && cameraDistance == o.cameraDistance
            && nearPlane == o.nearPlane && farPlane == o.farPlane
            && orientFromScene == o.orientFromScene
            && pixelSpace == o.pixelSpace
            && sceneCamera == o.sceneCamera
            && subView == o.subView
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
    /// ViewParams::TouchLoupeLift, CSS pixels: how far above the fingertip
    /// the touch loupe picks (the WASM viewer's hold-to-preselect gesture).
    float loupeLift = 28.0f;

    bool operator==(const PreselHighlightConfig &o) const {
        return color == o.color && outlineWidth == o.outlineWidth
            && faceOutline == o.faceOutline && outlineOnly == o.outlineOnly
            && pickRadius == o.pickRadius && loupeLift == o.loupeLift;
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

/// Per-frame screen-space cavity (curvature) shading configuration —
/// like AOConfig there is no GL-renderer counterpart. A curvature term
/// read from the geometry prepass normals, multiplied onto the finished
/// opaque scene: it states surface shape without depending on the
/// lighting, which is what makes small features readable in an
/// inspection view. Orthogonal to AOConfig — occlusion is a visibility
/// integral over a world-space radius, cavity is a local second
/// derivative over a screen-space one — and the two compose.
struct CavityConfig {
    bool enabled = false;    ///< cavity pass active
    /// Darkening strength in concave creases (curvature > 0) and on
    /// convex ridges (curvature < 0). Both darken: the pass multiplies
    /// an 8-bit scene color, so it cannot brighten past white.
    float valley = 1.0f;
    float ridge = 0.5f;
    /// Baseline the curvature is measured over, in pixels. Decides
    /// which features the pass can see: at 1 it reads only what turns
    /// within one pixel — hard creases, crisply, which is what stands in
    /// for the edges the Shaded draw style does not draw — and widening
    /// it brings broad curvature in at the cost of softening those
    /// creases into bands of this width.
    float radius = 1.0f;

    bool operator==(const CavityConfig &o) const {
        return enabled == o.enabled && valley == o.valley
            && ridge == o.ridge && radius == o.radius;
    }
    bool operator!=(const CavityConfig &o) const { return !(*this == o); }
};

/// Per-frame matcap shading configuration — like AOConfig there is no
/// GL-renderer counterpart. Replaces the scene lighting with a fixed
/// studio attached to the camera, looked up by the view-space normal,
/// so a surface's shading depends only on which way it faces the
/// viewer. The presets are computed in the shader (no matcap images to
/// ship, install or fetch on any tier). Overrides PBRConfig while on.
struct MatcapConfig {
    bool enabled = false;    ///< matcap shading active
    /// Which procedural matcap: 0 studio, 1 clay, 2 metal, 3 pearl.
    int preset = 0;
    /// How much the object's own color tints the matcap, 0 to 1. Zero
    /// shades every object as one uniform material.
    float tint = 0.0f;

    bool operator==(const MatcapConfig &o) const {
        return enabled == o.enabled && preset == o.preset
            && tint == o.tint;
    }
    bool operator!=(const MatcapConfig &o) const { return !(*this == o); }
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
/// A creation serial for the immutable, pointer-shared cache objects
///
/// Pointer identity is what says two draws share a palette or a shader:
/// they are immutable once published and one node makes one. But the
/// render cache's material map ORDERS by these too, and ordering by the
/// ADDRESS made the draw list's order depend on where the allocator
/// happened to put them -- which is different in every run of the same
/// binary. Where two draws then contend for one pixel at equal depth --
/// the rim circle a cylinder's wall and its top face share -- the
/// picture changed from run to run with it. A serial orders them by
/// creation instead, which is the same in every run.
///
/// Every construction takes a FRESH serial, copies and moves included,
/// so two live objects can never share one. They must not: a tie in the
/// ordering is what makes the map treat two palettes as one.
struct RendererExport CacheSerial {
    CacheSerial(): value(next()) {}
    CacheSerial(const CacheSerial &): value(next()) {}
    CacheSerial(CacheSerial &&) noexcept: value(next()) {}
    CacheSerial &operator=(const CacheSerial &) { value = next(); return *this; }
    CacheSerial &operator=(CacheSerial &&) noexcept { value = next(); return *this; }
    ~CacheSerial() = default;

    /// Never 0: 0 is what a null shared_ptr orders as.
    static std::uint64_t next();

    /// A serial for a NODE, memoized by ADDRESS: the same node gets the
    /// same one for as long as it lives, and the sequence is the same in
    /// every run.
    ///
    /// This is what a Coin node id could not be. An id moves on every
    /// notify(), so a material captured before one and a material
    /// captured after it disagreed about the same node, and the
    /// incremental flatten -- which merges a previous publish's map with
    /// entries built now -- turned one light into two buckets. A serial
    /// never moves, so those two materials still meet in one bucket, and
    /// the ordering is still free of addresses.
    ///
    /// Keying the memo on the address is safe precisely because everything
    /// that holds one of these -- SoFCRenderCache's NodeInfo and
    /// TextureInfo -- keeps a STRONG reference (CoinPtr is an
    /// intrusive_ptr). No address can be reused while anything able to
    /// compare its serial is still alive, so two LIVE nodes can never
    /// share one. A dead node's entry may later be inherited by a new
    /// node at that address, which is harmless: they are never live at
    /// once, and the new node wants a serial of its own only in the sense
    /// that it must not collide with a live one.
    static std::uint64_t forNode(const void *node);

    std::uint64_t value;
};

/// One user shader program (standalone so the render cache can hold a
/// shared_ptr to a "material"-stage program inside its per-draw
/// Material without pulling in the whole config).
struct UserShader {
    /// Orders this shader in the render cache's material map. Not part
    /// of what the shader IS, so it takes no part in operator==.
    CacheSerial serial;
    /// What the source strings below ARE (docs/CyclesIntegration.md
    /// sec 8 item 15 phase B). Shading-language text the backend
    /// compiles, or a MaterialX document -- a node graph describing
    /// the surface, which each backend interprets in its own
    /// vocabulary rather than compiling: the path tracer walks it into
    /// its own shader nodes, the raster path generates a
    /// material-inputs function from it. Only a "material"-stage
    /// program is ever anything but ShaderText.
    enum class Dialect : uint8_t {
        ShaderText = 0,
        MaterialX = 1,
    };
    Dialect dialect = Dialect::ShaderText;
    /// Where a MaterialX document came from, when it came from a file
    /// (empty for one authored inline). A real material states its
    /// images as paths RELATIVE to its own document, so this is what
    /// they resolve against; it is also what a self-contained document
    /// would have to replace, which is the open question of
    /// docs/CyclesIntegration.md sec 8 item 15 decision 2.
    std::string sourcePath;
    /// Which surface of a MaterialX document is shaded: the name of one
    /// of its surfacematerial nodes (SoShaderObject::sourceSurface).
    /// Empty renders the first surface the document states, which is
    /// what a single-material document has -- an asset's whole material
    /// set is usually one document, and this picks one out of it
    /// (docs/MaterialStorage.md sec 17.13). Part of the shader's
    /// identity: two surfaces of one document are two shaders.
    std::string surface;
    /// Pipeline stage name from SoShaderProgram::stage. Backends map
    /// known names and warn-and-skip unknown ones.
    std::string stage;
    /// bgfx .sc sources (SoShaderObject sourceType BGFX_SC, or
    /// FILENAME with a .sc suffix — the capture reads the file). An
    /// empty vertex source means the stage's built-in vertex shader
    /// (for "post": the full-screen triangle, input v_texcoord0; for
    /// "material": the stock mesh vertex stage, outputs v_normal,
    /// v_color0, v_vpos).
    /// When dialect is MaterialX, fragmentSource is the MaterialX
    /// document instead and the other two are empty.
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
        /// The glass body splice of a MaterialX document
        /// (docs/MaterialStorage.md sec 17.23): the same generated
        /// material function spliced into the glass pass's fragment
        /// stage, paired with the stock textured mesh vertex stage
        /// like fsBin is. Empty for shader text and for a document
        /// whose surface claims no glass body, where the viewer's flat
        /// glass program stands in.
        std::vector<uint8_t> glassBin;

        bool operator==(const Compiled &o) const {
            return profile == o.profile && vsBin == o.vsBin
                && fsBin == o.fsBin && simBin == o.simBin
                && glassBin == o.glassBin;
        }
        bool operator!=(const Compiled &o) const { return !(*this == o); }
    };
    /// Transport payload attached at snapshot-serialization time
    /// (SceneSnapshot::shipShader); empty on the desktop's own config
    /// feed. Part of equality on purpose: a viewer must re-apply a
    /// config whose sources it already has once the bins arrive.
    std::vector<Compiled> compiled;

    /// One image a MaterialX document names, decoded
    /// (docs/CyclesIntegration.md sec 6.12).
    ///
    /// The document states its maps as file PATHS, and the raster path
    /// cannot open a file: not in a viewer tier that has no filesystem,
    /// and not on the render thread even where it could. So the capture
    /// decodes them and they travel with the shader like its pixels.
    /// The generator, which alone knows the sampler names it emitted,
    /// reports which sampler each path belongs to -- so the two lists
    /// are joined on `path`, and neither side has to predict the
    /// other's naming.
    struct Image {
        /// Absolute path the document resolved to. The join key.
        std::string path;
        std::shared_ptr<const TextureImage> image;
        /// Which layer of the generated program's image array this
        /// file is, or -1 when the generated code does not read it (or
        /// nobody has said). The join, done once: the producer's ship
        /// hook (SceneSnapshot::shipShader) resolves it against its
        /// generator before the shader travels, so a tier with no
        /// generator of its own binds the layers it was handed
        /// (docs/MaterialStorage.md sec 17.23). The desktop leaves it
        /// -1 and joins through its own variant.
        int layer = -1;

        bool operator==(const Image &o) const {
            const uint64_t a = image ? image->textureId : 0;
            const uint64_t b = o.image ? o.image->textureId : 0;
            return path == o.path && a == b && layer == o.layer;
        }
        bool operator!=(const Image &o) const { return !(*this == o); }
    };
    /// The document's images, in document order. Empty for every shader
    /// that is not a MaterialX document naming a file.
    std::vector<Image> images;
    /// The array sampler the generated program declares for those
    /// layers and the unit it claims -- transport fields, filled by the
    /// same ship hook that resolves Image::layer, for the tiers that
    /// have no generator to ask. Empty and 0 until then, and for a
    /// document naming no image.
    std::string imageSampler;
    int imageUnit = 0;

    /// What the MaterialX surface's transmission resolves to for a
    /// consumer that draws it as a glass BODY -- the engine's glass pass,
    /// which has one colour, IOR, density and roughness per draw
    /// (docs/MaterialStorage.md sec 17.21). Resolved by the capture once
    /// per document and surface from DocumentInfo::transmission, and
    /// folded into the draw's Material (glass, glassmtlx, glasscolor,
    /// ...) where the shader is worn. Derived from fragmentSource and
    /// surface, so it takes no part in operator== and does not travel:
    /// a viewer tier receives the Material it was folded into.
    struct Glass {
        bool claimed = false;
        float ior = 1.5f;
        /// 1 / transmission_depth; 0 = the colour tints at the surface.
        float density = 0.0f;
        float roughness = 0.0f;
        /// Linear.
        float color[3] = {1.0f, 1.0f, 1.0f};
    };
    Glass glass;

    bool operator==(const UserShader &o) const {
        return dialect == o.dialect && sourcePath == o.sourcePath
            && surface == o.surface
            && stage == o.stage && vertexSource == o.vertexSource
            && fragmentSource == o.fragmentSource
            && simulateSource == o.simulateSource && params == o.params
            && compiled == o.compiled && images == o.images
            && imageSampler == o.imageSampler && imageUnit == o.imageUnit;
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
    /// Consumed only by a COMPLETE frame (frameComplete: every user
    /// shader compiled, every deferred shape arrived, the frozen
    /// warm-up reached) -- the default, because a capture is of the
    /// scene as authored. False takes the very next frame whatever
    /// its state: the scene as it stands mid-arrival, which is what a
    /// probe of the arrival itself wants to see.
    bool waitComplete = true;
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

    /// Backend handle-pool occupancy at the last completed frame, each
    /// beside the pool it is measured against (-1 where the backend
    /// does not report it).
    ///
    /// These are PROCESS-WIDE, not per view: every 3D view, and the
    /// scene server's own view, draw from the same pools. That is the
    /// number worth watching -- running out is not a graceful
    /// degradation by default, it is what a view falls back to Coin
    /// over, and the fallback only exists because exhausting a pool
    /// used to assert inside the engine.
    int numFrameBuffers = -1;
    int maxFrameBuffers = -1;
    int numTextures = -1;
    int maxTextures = -1;
    int numViews = -1;
    int maxViews = -1;
    /// Backend estimates of texture and render-target bytes, and the
    /// driver's own figure for the process where it reports one.
    long long textureMemory = -1;
    long long renderTargetMemory = -1;
    long long gpuMemoryUsed = -1;
    long long gpuMemoryMax = -1;
    /// Jittered samples averaged into the idle temporal accumulation as
    /// of this frame (0 = not accumulating, -1 = the backend has no such
    /// stage). The one number that separates "converged" from "never
    /// engaged" -- both of which look like an image that is not
    /// changing.
    int temporalSamples = -1;
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

/// What the shadow ground needs to know about the camera to size
/// itself to the view instead of to the scene
/// (LightConfig::groundFollowCamera).
///
/// Not part of LightConfig: the light is the scene's, published when
/// the scene changes, while this is the eye's and changes on every
/// navigation frame. Mixing them would mark the scene dirty for a
/// mouse move.
struct GroundCamera {
    /// False leaves the ground on its scene-bounds sizing. That is the
    /// honest answer before the first frame, and it is what a caller
    /// with no camera to hand must say -- never a guess, because the
    /// quad, the reflection overlay that depth-tests EQUAL against it
    /// and the scene bounds all have to build the SAME quad.
    bool valid = false;
    float pos[3] = {0.0f, 0.0f, 0.0f};  ///< world-space eye position
    /// World-space view direction (the view's -Z). The eye position
    /// alone is not enough: looking straight down at a plane and
    /// looking along it need very different amounts of ground, and the
    /// difference is entirely in this vector.
    float dir[3] = {0.0f, 0.0f, -1.0f};
    /// The projection's y scale, proj[1][1]: 1/tan(fovy/2) under a
    /// perspective camera, 2/height under an orthographic one. Both
    /// answer "how much world does the viewport span", which is the
    /// only question the sizing asks.
    float projY = 1.0f;
    bool perspective = true;
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
    /// Ground sizing and placement, matching what the Coin quad does
    /// (View3DInventorViewer::Private::updateShadowGround):
    ///
    /// - `groundAuto` (ShadowGroundSizeAuto) picks `groundScale` times
    ///   the largest scene dimension over the explicit half extents
    ///   `groundSizeX` / `groundSizeY` (ShadowGroundSizeX/Y).
    /// - `groundAutoPos` (ShadowGroundAutoPosition) puts the quad under
    ///   the scene, **one unit below** the bounding box — Coin's
    ///   `z = center.z - size.z/2 - 1`, a gap that keeps a model resting
    ///   on z=0 from z-fighting its own shadow receiver. Otherwise the
    ///   quad centre is `groundPos` outright.
    /// - `groundMatrix` is ShadowGroundPlacement, applied to the four
    ///   corners so the ground can be tilted. Coin applies it only in
    ///   auto-position mode (where the placement reads as an offset);
    ///   with an explicit position the placement *is* the position and
    ///   the matrix is identity. Column-major, as the rest of the
    ///   renderer's matrices.
    bool groundAuto = true;
    /// Auto sizing measured from the CAMERA rather than from the scene
    /// bounding box (RenderShadow_GroundSizeFollowCamera; no Coin
    /// counterpart). The quad centres under the eye and reaches
    /// `groundScale` times what the viewport spans where it meets the
    /// plane, so it reads as an infinite ground: no plate edge beside
    /// the model, and -- the reason it exists -- nothing about the
    /// scene's extent reaches the ground at all. A bounding box that
    /// twitches (a level-of-detail swap, an object appearing) then
    /// cannot move the receiver, and neither can anything that grows
    /// the box without being part of the model.
    ///
    /// Only the EXTENT and the centre come from the camera. The plane's
    /// height is still the scene's (`groundAutoPos`): a ground that
    /// rose and fell with the eye would not be a ground.
    bool groundFollowCamera = true;
    float groundSizeX = 100.0f;  ///< half extent when !groundAuto
    float groundSizeY = 100.0f;
    bool groundAutoPos = true;
    float groundPos[3] = {0.0f, 0.0f, 0.0f};
    float groundMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                              0, 0, 1, 0, 0, 0, 0, 1};
    /// Ground shading (ShadowGroundShading): off draws the quad in its
    /// flat color, ignoring the light -- Coin's SoLightModel BASE_COLOR
    /// on the ground group. The shadow still darkens it; what goes away
    /// is the diffuse falloff across the quad.
    bool groundShading = true;
    /// Ground back-face culling (ShadowGroundBackFaceCull): the quad is
    /// one-sided, so a camera below the ground plane sees through it
    /// instead of being shut out by a grey slab -- Coin's SoShapeHints
    /// SOLID + COUNTERCLOCKWISE on the ground group.
    bool groundBackFaceCull = true;
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

    /// How transparent the shadow ITSELF is on a shadow-only ground
    /// (RenderShadow_Transparency; Coin's SoShadowTransparency, whose
    /// 0.2 default this keeps). 0 paints a solid shadow, 1 an invisible
    /// one -- the two modes the Coin ground had, both of them hiding
    /// the unshadowed ground entirely. Read only while
    /// groundShadowOnly(): a drawn ground carries the shadow in its
    /// shading instead.
    float shadowTransparency = 0.2f;

    /// A fully transparent ground is not an absent one: it carries the
    /// shadow and nothing else, transparent wherever the scene light
    /// reaches it. That is Coin's TRANSPARENT_SHADOWED style, which
    /// RenderShadow_GroundTransparency = 1 selected there too -- and
    /// what a receiver is usually wanted for, since a solid plane puts
    /// a horizon in a view of a part.
    ///
    /// The quad is the same one either way, depth included, so a
    /// ground reflection still lands on it: what changes is only that
    /// the plane itself is not painted.
    ///
    /// RenderShadow_ShowGround is what says there is no ground at all.
    bool groundShadowOnly() const
    {
        return groundTransparency >= 1.0f;
    }

    /// The ground quad's four corners in world space, wound as Coin
    /// builds them (-x-y, +x-y, +x+y, -x+y). False when there is no
    /// ground to draw, so a caller can use it as its own gate.
    ///
    /// One place, because there were three: the shadow pass, the
    /// reflection pass and the scene bounds each recomputed the extent
    /// from groundScale, and only one of them would have been updated
    /// when the sizing gained cases.
    /// \a halfOut, when given, receives the two half extents — the
    /// texture spans need them separately, and they stop being equal as
    /// soon as the size is set explicitly.
    ///
    /// The reflection implies the receiver: a mirror in the ground plane
    /// is blended onto the ground quad, so Render_GroundReflection with
    /// no ground would ask for a reflection and then have nothing to
    /// show it on. Requiring the user to find RenderShadow_ShowGround as
    /// well is a coupling nobody can guess from either name, and it is
    /// asked for here rather than at each consumer so the scene bounds
    /// (camera auto-clipping) grow to cover the quad too.
    bool groundQuad(const float *bmin, const float *bmax,
                    const GroundCamera &cam,
                    float corners[4][3], float *halfOut = nullptr) const
    {
        float cx, cy, z, hx, hy;
        if (!groundExtent(bmin, bmax, cam, cx, cy, z, hx, hy))
            return false;
        if (halfOut) {
            halfOut[0] = hx;
            halfOut[1] = hy;
        }
        static const float xs[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        static const float ys[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        for (int i = 0; i < 4; ++i) {
            const float p[3] = {cx + xs[i] * hx, cy + ys[i] * hy, z};
            groundToWorld(p, corners[i]);
        }
        return true;
    }

    /// The infinite plane the quad above lies in: a point on it and its
    /// unit normal, both in world space. Same gate, same placement --
    /// what it drops is the extent, which is exactly what a receiver
    /// computed per pixel does not have and does not want.
    bool groundPlane(const float *bmin, const float *bmax,
                     const GroundCamera &cam,
                     float point[3], float normal[3]) const
    {
        float cx, cy, z, hx, hy;
        if (!groundExtent(bmin, bmax, cam, cx, cy, z, hx, hy))
            return false;
        const float p[3] = {cx, cy, z};
        groundToWorld(p, point);
        // The plane's normal is the placement's local +Z as a
        // DIRECTION: no translation, and no inverse-transpose either --
        // groundMatrix is a Base::Placement, so its 3x3 is orthonormal
        // and is its own normal matrix.
        normal[0] = groundMatrix[8];
        normal[1] = groundMatrix[9];
        normal[2] = groundMatrix[10];
        const float len = std::sqrt(normal[0] * normal[0]
                                  + normal[1] * normal[1]
                                  + normal[2] * normal[2]);
        if (len <= 1.0e-12f)
            return false;
        for (int i = 0; i < 3; ++i)
            normal[i] /= len;
        return true;
    }

    /// Where the ground sits and how far it reaches, in the ground's
    /// own frame (before groundMatrix). The one place that answers it:
    /// the quad, the plane and the scene bounds must agree to the bit,
    /// because the ground reflection depth-tests EQUAL against the quad
    /// and a corner that disagreed by a float would drop it.
    bool groundExtent(const float *bmin, const float *bmax,
                      const GroundCamera &cam,
                      float &cx, float &cy, float &z,
                      float &hx, float &hy) const
    {
        if (!valid || !(ground || groundReflection))
            return false;

        // Placement first: a camera-fitted extent is measured from the
        // plane, so the plane's height has to exist before it.
        if (groundAutoPos) {
            cx = (bmin[0] + bmax[0]) * 0.5f;
            cy = (bmin[1] + bmax[1]) * 0.5f;
            // Coin's z = center.z - size.z/2 - 1: one unit *below* the
            // box, not level with its floor. The gap is what keeps a
            // model resting on z=0 out of a z-fight with the quad
            // receiving its shadow.
            z = bmin[2] - 1.0f;
        }
        else {
            cx = groundPos[0];
            cy = groundPos[1];
            z = groundPos[2];
        }

        const float scale = groundScale > 0.0f ? groundScale : 1.0f;
        if (!groundAuto) {
            hx = groundSizeX;
            hy = groundSizeY;
        }
        else if (groundFollowCamera && cam.valid) {
            float eye[3], dir[3];
            groundToLocal(cam.pos, eye);
            groundDirToLocal(cam.dir, dir);
            const float py = cam.projY > 1.0e-6f ? cam.projY : 1.0f;
            const float above = std::fabs(eye[2] - z);
            // What the viewport spans in world units where it meets the
            // plane. A perspective camera's span grows with its
            // distance from the plane; an orthographic camera's is its
            // height, wherever it stands.
            const float span = cam.perspective ? 2.0f * above / py
                                               : 2.0f / py;
            // Centred on what the camera is LOOKING at, not on what it
            // stands over. Looking down, the two are the same point;
            // looking along the plane they are far apart, and centring
            // under the eye then puts the whole visible ground outside
            // the quad -- the ground of a grazing view is in front of
            // the camera, not under it.
            //
            // How far in front is the ray's own answer, so the quad
            // grows exactly as the view flattens. Its reach is capped
            // because that answer runs to infinity at the horizon, and
            // an infinite quad is neither drawable nor wanted: the
            // fade takes over there, which is what a receding plane
            // should do anyway.
            const float toward = eye[2] >= z ? -dir[2] : dir[2];
            float reach = 0.0f;
            cx = eye[0];
            cy = eye[1];
            if (toward > 1.0e-3f && above > 0.0f) {
                const float t = above / toward;
                const float dx = t * dir[0];
                const float dy = t * dir[1];
                const float len = std::sqrt(dx * dx + dy * dy);
                reach = std::min(len, kGroundMaxReach * above);
                if (len > 1.0e-6f) {
                    cx += dx * (reach / len);
                    cy += dy * (reach / len);
                }
            }
            // else: looking along the plane, or away from it. There is
            // no point on it to centre on, so stay under the eye and
            // let the fade end the ground.
            // An eye ON the plane sees it edge on and spans nothing.
            // The floor is not a size, it is what keeps the ground from
            // blinking out of existence (and taking the reflection with
            // it) as the camera crosses.
            hx = hy = std::max(scale * (span + reach), 1.0e-4f);
        }
        else {
            hx = hy = scale * std::max(bmax[0] - bmin[0],
                                       std::max(bmax[1] - bmin[1],
                                                bmax[2] - bmin[2]));
        }
        return hx > 0.0f && hy > 0.0f;
    }

    /// The ground's own frame -> world. Column-major, like the rest of
    /// the renderer's matrices.
    void groundToWorld(const float *p, float *out) const
    {
        for (int r = 0; r < 3; ++r) {
            out[r] = groundMatrix[r] * p[0]
                   + groundMatrix[4 + r] * p[1]
                   + groundMatrix[8 + r] * p[2]
                   + groundMatrix[12 + r];
        }
    }

    /// How far in front of the eye a camera-fitted ground may reach,
    /// as a multiple of the eye's height above the plane. The ray's own
    /// answer runs to infinity as the view flattens toward the horizon,
    /// and past a point the quad is all fade band and no ground.
    static constexpr float kGroundMaxReach = 24.0f;

    /// World -> the ground's own frame. groundMatrix is a
    /// Base::Placement (ShadowGroundPlacement), so its 3x3 is
    /// orthonormal and the inverse rotation is the transpose; nothing
    /// in this fork feeds a scaled or sheared one.
    void groundToLocal(const float *w, float *out) const
    {
        const float d[3] = {w[0] - groundMatrix[12],
                            w[1] - groundMatrix[13],
                            w[2] - groundMatrix[14]};
        groundDirToLocal(d, out);
    }

    /// The same, for a DIRECTION: rotation only, no translation.
    void groundDirToLocal(const float *w, float *out) const
    {
        for (int r = 0; r < 3; ++r) {
            out[r] = groundMatrix[4 * r] * w[0]
                   + groundMatrix[4 * r + 1] * w[1]
                   + groundMatrix[4 * r + 2] * w[2];
        }
    }

    /// ! A new field of this struct belongs in THREE places, and each
    /// omission fails silently in its own way: the SceneDump stream (or
    /// the browser tier keeps the default forever -- the layout assert
    /// there is what catches it), and this comparison, which is what
    /// marks the scene dirty. A field left out here reads correctly and
    /// changes nothing on screen until something else about the light
    /// happens to move, which is how groundShading and groundBackFaceCull
    /// shipped inert for two stages.
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
            && groundShading == o.groundShading
            && groundBackFaceCull == o.groundBackFaceCull
            && groundColor == o.groundColor
            && groundTexture == o.groundTexture
            && groundTextureSize == o.groundTextureSize
            && groundTransparency == o.groundTransparency
            && groundBumpMap == o.groundBumpMap
            && groundReflection == o.groundReflection
            && groundReflectionIntensity == o.groundReflectionIntensity
            && sunDisc == o.sunDisc && sunDiscSize == o.sunDiscSize
            && groundAuto == o.groundAuto
            && groundFollowCamera == o.groundFollowCamera
            && groundSizeX == o.groundSizeX && groundSizeY == o.groundSizeY
            && groundAutoPos == o.groundAutoPos
            && shadowTransparency == o.shadowTransparency
            && std::equal(groundPos, groundPos + 3, o.groundPos)
            && std::equal(groundMatrix, groundMatrix + 16, o.groundMatrix);
    }
    bool operator!=(const LightConfig &o) const { return !(*this == o); }
};

/// Maximum number of ordinary Coin lights carried per frame.
///
/// Eight matches what Coin's own renderer can draw, but that is not a
/// constraint on this one and the number is not inherited from it:
/// `SoLightElement` is an unbounded list, and the familiar 8 is
/// `SoGLLightIdElement::getMaxGLSources()` -- `glGetIntegerv(
/// GL_MAX_LIGHTS)`, binding fixed-function GL. This engine reads the
/// element and shades it itself.
///
/// What bounds it here is the fragment uniform budget: the mesh
/// program totals 183 of the 224 vec4 an ES3/WebGL2 device has to
/// guarantee, so 16 would still fit and 32 would not. Eight is chosen
/// because nothing produces more (upstream's three-point rig plus the
/// scene light is four) and because past it Coin's own compositing
/// would stop at GL_MAX_LIGHTS and diverge. See docs/RenderEngine.md
/// 3.2 for the full budget, including the effect lights, which are a
/// separate array with a separate capacity.
///
/// Keep in step with VIEW_LIGHTS in bgfx/shaders/fc_mesh_lighting.sh,
/// which cannot see this header (the same hand-paired arrangement as
/// LOCAL_LIGHTS / kLocalLights and BULB_SHADOW_TILES).
static constexpr int MaxViewLights = 8;

/// One ordinary light of the Coin traversal: the viewer's headlight and
/// backlight, and any SoDirectionalLight / SoPointLight a document puts
/// in the graph. Distinct from LightConfig, which is the *scene* light
/// -- the single shadow-casting one the Shadow draw style (or
/// Render_Light) supplies, with a shadow map, sun disc and ground of its
/// own. These have none of that: they light, and nothing else.
struct ViewLight {
    /// The way the light travels, world space, normalized. Unused by a
    /// plain positional light; a `spot` reads it as the cone axis.
    float direction[3] = {0.0f, 0.0f, -1.0f};
    /// World-space position of a positional (SoPointLight) light.
    float position[3] = {0.0f, 0.0f, 0.0f};
    bool positional = false;
    /// Distance attenuation, in Coin's SoEnvironment::attenuation order
    /// and units: squared, linear, constant (default 0, 0, 1 = none).
    /// Kept in that order rather than re-sorted to the GL one so the
    /// value can be compared against the node it came from. Positional
    /// lights only -- a directional light has no distance.
    float attenuation[3] = {0.0f, 0.0f, 1.0f};
    uint32_t color = 0xffffffff;   ///< packed 0xRRGGBBAA
    float intensity = 1.0f;
    /// An SoSpotLight past the one the scene light claims (the first is
    /// taken by LightConfig, with the shadow map and the sun disc; these
    /// have neither). A spot is `positional` as well -- it has a
    /// position and Coin's distance attenuation applies to it -- with
    /// `direction` as the cone axis on top.
    bool spot = false;
    /// Coin's SoSpotLight fields, kept in its units (cutOffAngle is the
    /// cone's half angle in radians, dropOffRate the 0..1 field) so they
    /// can be compared against the node they came from; the backend
    /// converts, exactly as LightConfig's pair is converted.
    float cutOffAngle = 0.785398f;
    float dropOffRate = 0.0f;

    /// CAMERA-RELATIVE (a headlight): this light sits before the camera,
    /// so what is fixed about it is its EYE-space direction, and the
    /// world-space `direction` above is only the direction that
    /// corresponded to it under the camera that produced this config.
    ///
    /// That distinction does not matter to a renderer drawing its own
    /// camera's frame -- it recomputes the world direction every
    /// traversal. It matters entirely to a STREAMED viewer, whose camera
    /// is its own: applying the producer's world direction there lights
    /// the scene from wherever that camera happened to point, which for
    /// a headless serving process is a default looking down -Z. Faces
    /// pointing at the viewer then get nothing but ambient.
    bool eyeSpace = false;
    /// The eye-space direction and position `eyeSpace` refers to, valid
    /// only while it is set. A consumer with its own camera re-derives
    /// world space from these; one drawing the producer's frame can keep
    /// using `direction`/`position` and never look at them.
    float eyeDirection[3] = {0.0f, 0.0f, -1.0f};
    float eyePosition[3] = {0.0f, 0.0f, 0.0f};

    bool operator==(const ViewLight &o) const {
        return std::equal(direction, direction + 3, o.direction)
            && std::equal(position, position + 3, o.position)
            && positional == o.positional
            && std::equal(attenuation, attenuation + 3, o.attenuation)
            && color == o.color && intensity == o.intensity
            && spot == o.spot && cutOffAngle == o.cutOffAngle
            && dropOffRate == o.dropOffRate
            && eyeSpace == o.eyeSpace
            && std::equal(eyeDirection, eyeDirection + 3, o.eyeDirection)
            && std::equal(eyePosition, eyePosition + 3, o.eyePosition);
    }
    bool operator!=(const ViewLight &o) const { return !(*this == o); }
};

/// The ordinary Coin lights of a frame (see ViewLight).
///
/// `fed` is what separates "the feed resolved the lighting and there is
/// none" from "nothing filled this in". The first has to render dark --
/// it is what EnableHeadlight off means, and honoring it is the point
/// of carrying this at all. The second is an old scene dump or a
/// consumer that predates the struct, and has to keep rendering the way
/// it always did, so backends substitute their fixed white headlight
/// down the view axis. Leaving `fed` false by default is what makes
/// that the safe direction.
struct ViewLightConfig {
    bool fed = false;
    int count = 0;
    ViewLight lights[MaxViewLights];

    /// The traversal's global ambient (SoEnvironment ambientColor times
    /// ambientIntensity, default 0.2 grey), packed 0xRRGGBBAA. GL's
    /// LIGHT_MODEL_AMBIENT: a surface's ambient term is this times the
    /// material's own ambient colour, which is what Material::ambient
    /// carries. Only meaningful while `fed` -- an unfed config leaves
    /// backends on the flat grey floor they used before this existed.
    uint32_t ambient = 0x333333ff;

    bool operator==(const ViewLightConfig &o) const {
        if (fed != o.fed || count != o.count || ambient != o.ambient)
            return false;
        for (int i = 0; i < count; ++i) {
            if (lights[i] != o.lights[i])
                return false;
        }
        return true;
    }
    bool operator!=(const ViewLightConfig &o) const { return !(*this == o); }
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

/// Idle temporal accumulation (docs/RenderEngine.md sec 3.5). While the
/// camera, the scene and the highlight all hold still, each further frame
/// offsets the projection by a fraction of a pixel and averages into a
/// history target, so the whole pipeline -- geometry edges, the shading
/// itself, and every screen-space pass computed after the multisample
/// resolve -- converges toward what supersampling it would have given.
///
/// Deliberately NOT temporal antialiasing in the usual sense: nothing is
/// reprojected and no history is rejected, because the accumulation only
/// ever runs over frames where nothing moved. Any camera, scene or
/// highlight change replaces the history outright, so interaction costs
/// nothing and no thin edge can ghost or trail. It refines a multisampled
/// frame; it does not replace multisampling.
struct TemporalConfig {
    bool enabled = false;
    /// Jittered samples to converge over, after which the view stops
    /// asking for frames and holds the converged image (clamped 2..256).
    int samples = 32;

    bool operator==(const TemporalConfig &o) const {
        return enabled == o.enabled && samples == o.samples;
    }
    bool operator!=(const TemporalConfig &o) const { return !(*this == o); }
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
    /// How far out of focus that background is, 0..1: 0 draws the
    /// environment at the resolution it was baked at, 1 flattens it to
    /// its average colour. Blender's viewport shading carries the same
    /// control (View3DShading.studiolight_background_blur) and for the
    /// same reason -- a backdrop reads as a place when it is soft and
    /// as a pasted photo when it is sharp -- but only the BACKGROUND is
    /// affected: the lighting and the reflections keep the whole
    /// environment either way, here as there.
    ///
    /// Every backend honours it, by whatever means it has. The raster
    /// one reads a level of its background cubemap. A path tracer has
    /// no lod to read and could not soften the world anyway -- that
    /// world IS its light -- so it mixes a second, smaller bake of the
    /// same environment in on CAMERA rays alone
    /// (SceneTranslator::translateWorld), which is the node graph
    /// Blender users build by hand for this.
    float envBlur = 0.25f;
    /// Read an ordinary Phong appearance's SPECULAR COLOUR as material
    /// data where nothing states a metalness. The metallic/roughness
    /// BRDF has no specular slot -- its reflectance is f0, built from the
    /// base colour and the metalness -- so a Phong gold, whose gold-ness
    /// lives entirely in that colour, otherwise shades as yellow-brown
    /// plastic, and the several presets with a BLACK diffuse and a bright
    /// specular (Steel, Satin, Metalized ...) shade as nearly black. On,
    /// the shader solves the pair back into a base colour and a metalness
    /// (Khronos' spec-gloss conversion). Never applied over anything
    /// authored: a material or frame metalness, a PBR-mode appearance or
    /// a metallic-roughness map all stand.
    bool fromSpecular = true;
    /// How a Phong SHININESS becomes a roughness, where the material
    /// states none of its own (OutputConfig-style enum, see
    /// RenderParams::docShininessMapping): 0 reads shininess as the
    /// fixed-function GL exponent scaled onto 0..128, which is faithful
    /// but bottoms out at roughness 0.35 -- 128 is the sharpest exponent
    /// GL could state, so the lower half of the roughness range cannot
    /// be reached from shininess at all. 1 reads it as the 0..100%
    /// appearance control the dialog presents and maps it onto the whole
    /// exponent range, n = 128 * s / (1 - s): matte at zero, a mirror at
    /// one, and within a few percent of the GL reading over the low
    /// values real materials use.
    int shininessMapping = 1;
    /// Which built-in environment is computed where `envImage` is null.
    /// They differ in contrast and structure, not in brightness: every
    /// one integrates to the same mean radiance, so a change of preset
    /// does not ask for a change of exposure. 0 Studio (soft boxes on a
    /// dark surround), 1 Gradient (the smooth three-band dome this
    /// engine had before the others), 2 Overcast, 3 Sunset, 4 Interior
    /// (the default -- one window against a dark surround, the crispest
    /// key of them), 5 Light tent (bright in BOTH hemispheres with
    /// panel seams throughout, for a subject whose sides matter).
    ///
    /// The point of the others is that Gradient spans barely one stop
    /// and has no edges anywhere, so a smooth dielectric reflecting it
    /// shows the same grey at every roughness and nothing in the frame
    /// reads as a light source.
    int envPreset = 4;
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
            && envBackground == o.envBackground && envBlur == o.envBlur
            && envImage == o.envImage
            && envPreset == o.envPreset
            && fromSpecular == o.fromSpecular
            && shininessMapping == o.shininessMapping;
    }
    bool operator!=(const PBRConfig &o) const { return !(*this == o); }
};

/// What the engine does to a finished frame before it is shown.
///
/// The shading math here is linear -- `mix()`, the GGX lobe, the IBL
/// product, `f0 * env` are all plain arithmetic on light, and they are
/// only correct on linear numbers, which is why material colours are
/// linear by definition on this side and why the environment photo is
/// decoded on load. A display is not linear: it reads the byte it is
/// handed as sRGB. Writing the linear result straight into an 8-bit
/// target therefore shows it about a gamma too dark through the
/// midtones.
///
/// The transform is applied ONCE, at the last write before the frame
/// leaves the engine, so every blend, the weighted-blended transparency
/// composite and every effect pass still run on linear values. It is
/// deliberately NOT pushed back into the material data: pre-compensating
/// there would trade a correct metal reflectance table for a wrong one,
/// and would only reach the objects whose numbers were edited.
struct OutputConfig {
    enum Transform {
        /// Neither end: feed display numbers to the linear shading and
        /// write the linear result out raw. The two errors partly
        /// cancel -- a fully lit surface comes out right -- but the
        /// falloff renders about a gamma too dark. What every frame
        /// before this existed was drawn with, and what an older scene
        /// snapshot has to keep being drawn with.
        None = 0,
        /// Colour managed: authored colours decoded to linear on the
        /// way in (unpackAuthoredColor, and fc_color.sh for the streams
        /// and pictures C++ cannot reach), the finished frame encoded
        /// once on the way out.
        SRGB = 1,
    };
    int transform = SRGB;
    /// A plain multiplier on the linear image before it is encoded.
    /// One leaves the frame alone, bit for bit.
    ///
    /// A colour managed scene is lit in real reflectances -- a mid grey
    /// reflects about 18 per cent, not the 45 per cent its number reads
    /// as -- so a scene whose lights were set before that was true is
    /// lit two to three times too dimly. This answers that without
    /// touching a light. Ignored while transform is None: there the
    /// engine is not working in light at all.
    float exposure = 1.0f;

    bool operator==(const OutputConfig &o) const {
        return transform == o.transform && exposure == o.exposure;
    }
    bool operator!=(const OutputConfig &o) const { return !(*this == o); }
};

/// How many distinct finishes one draw's palette may hold. The backend
/// uploads the palette as a uniform array of this size, so it is a shader
/// contract as much as a storage bound; a face whose finish does not fit
/// falls back to entry 0 (the draw's own finish) at translate time.
static constexpr int MaxFinishPalette = 8;

/// The distinct surface finishes of one per-face-finished draw
///
/// A finish is four numbers, and per-face data rides the per-vertex
/// material stream, where four more arrays would not fit any budget worth
/// spending. So the finishes reduce to this palette and the stream carries
/// a single index into it (MeshData::materials, third slot). Immutable
/// once published: draws share one by pointer, which is also how the
/// backend batches them.
struct FinishPalette {
    /// Orders this palette in the render cache's material map. Not
    /// part of what the palette IS, so it takes no part in operator==.
    CacheSerial serial;

    struct Entry {
        uint8_t pattern = 0;    ///< App::SurfaceFinish::Pattern, 0 = none
        float pitch = 0.0f;     ///< mm of object space, feature spacing
        float depth = 0.0f;     ///< mm of object space, peak to valley
        float angle = 0.0f;     ///< degrees, lay direction

        bool operator==(const Entry &o) const {
            return pattern == o.pattern && pitch == o.pitch
                && depth == o.depth && angle == o.angle;
        }
    };

    /// At most MaxFinishPalette entries; entry 0 repeats the material's
    /// own finish scalars.
    std::vector<Entry> entries;

    bool operator==(const FinishPalette &o) const {
        return entries == o.entries;
    }
};

/// How many distinct projection frames one draw's palette may hold. As
/// with MaxFinishPalette this is a shader contract (the uniform array is
/// declared this long); a face whose frame does not fit falls back to
/// entry 0, the first face's frame -- the same rule the finish palette
/// follows. Frames are canonicalized hard before they are counted (see
/// faceProjectionFrame), so the coplanar faces of a bracket and the
/// coaxial cylinders of a stepped shaft each cost ONE entry, and a part
/// that overflows sixteen is a part with sixteen genuinely different
/// machining setups.
static constexpr int MaxFramePalette = 16;

/// The frame a face's surface finish is laid out in
///
/// Without one, a finish is projected TRIPLANARLY off the object-space
/// normal, which is plausible from any angle but does not know the
/// geometry: a straight knurl on a cylinder comes out with
/// circumferential grooves, and turning marks centre on the object
/// origin rather than on the axis that was turned. A frame states what
/// the machine knew -- the plane's own axes, or the axis a cylinder or
/// cone was turned about -- so the pattern is laid the way the tool
/// laid it.
///
/// Produced from the OCCT surface at tessellation time (the Part view
/// provider), which is the only place the analytic surface is still in
/// hand; a face whose surface is neither planar nor a surface of
/// revolution states Unframed and keeps the triplanar projection.
struct SurfaceFrame {
    enum Kind : uint8_t {
        Unframed = 0,   ///< triplanar, as before frames existed
        Planar = 1,     ///< pattern laid in the plane's own axes
        Radial = 2,     ///< about an axis: cylinder, cone, revolution
    };

    uint8_t kind = Unframed;
    /// Object-space origin: a point of the plane, or a point ON the axis
    /// (canonicalized to the one nearest the object origin, so coaxial
    /// faces share a frame).
    float origin[3] = {0.0f, 0.0f, 0.0f};
    /// Plane normal, or the axis of revolution. Unit length.
    float axis[3] = {0.0f, 0.0f, 1.0f};
    /// Where the frame's first coordinate points: the surface's own X
    /// direction, so a lay angle means what the sketch or the turning
    /// setup meant by it. Unit length and perpendicular to axis.
    float xdir[3] = {1.0f, 0.0f, 0.0f};
    /// Radial frames only: the reference radius in millimetres, which
    /// fixes how many pattern periods fit around the circumference (see
    /// the seam argument in fc_finish.sh). 0 = derive it per fragment.
    float radius = 0.0f;

    bool operator==(const SurfaceFrame &o) const {
        return kind == o.kind && radius == o.radius
            && std::equal(origin, origin + 3, o.origin)
            && std::equal(axis, axis + 3, o.axis)
            && std::equal(xdir, xdir + 3, o.xdir);
    }
    bool operator!=(const SurfaceFrame &o) const { return !(*this == o); }
    /// Field by field, for the render cache's material ordering. Not a
    /// memcmp: the struct has padding after `kind` that nothing writes.
    bool operator<(const SurfaceFrame &o) const {
        if (kind != o.kind) return kind < o.kind;
        for (int i = 0; i < 3; ++i) {
            if (origin[i] != o.origin[i]) return origin[i] < o.origin[i];
            if (axis[i] != o.axis[i]) return axis[i] < o.axis[i];
            if (xdir[i] != o.xdir[i]) return xdir[i] < o.xdir[i];
        }
        return radius < o.radius;
    }
};

/// The distinct projection frames of one draw, indexed by the material
/// stream's third slot, second byte. Immutable once published, like
/// FinishPalette,
/// and shared by pointer for the same batching reason.
struct FramePalette {
    /// Orders this palette in the render cache's material map. Not
    /// part of what the palette IS, so it takes no part in operator==.
    CacheSerial serial;

    /// At most MaxFramePalette entries. Entry 0 is the first face's
    /// frame, which is what a draw with no stream -- an unbound index
    /// attribute, or a mesh whose stream collapsed because every face
    /// is framed alike -- resolves to.
    std::vector<SurfaceFrame> entries;

    bool operator==(const FramePalette &o) const {
        return entries == o.entries;
    }
};

/// How many images one draw's per-face texture palette may hold,
/// LAYER 0 INCLUDED. The backend uploads the palette as one 2D ARRAY
/// texture of this many layers at most, and layer 0 is not an image at
/// all: it is the untextured face, which samples opaque white. So a
/// draw states at most MaxFaceTexturePalette - 1 distinct images, and a
/// face whose image did not fit falls back to layer 0 -- untextured,
/// which is what every face looked like before the palette existed.
static constexpr int MaxFaceTexturePalette = 8;

/// The distinct images of one per-face-textured draw
///
/// A texture cannot ride the per-vertex material stream the way a colour
/// does, and a draw binds ONE sampler -- so the images a shape puts on
/// its individual faces are collected here, uploaded as the layers of a
/// single array texture, and what travels per face is the layer index
/// (MeshData::materials, third slot, third byte).
///
/// Entry i is layer i + 1: layer 0 is the untextured face and has no
/// entry. Immutable once published and shared by pointer, like
/// FinishPalette -- the backend keys both its batching and its uploaded
/// array on that pointer.
struct TexturePalette {
    /// At most MaxFaceTexturePalette - 1 entries, none of them null.
    std::vector<std::shared_ptr<const TextureImage>> entries;

    bool operator==(const TexturePalette &o) const {
        if (entries.size() != o.entries.size())
            return false;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const uint64_t a = entries[i] ? entries[i]->textureId : 0;
            const uint64_t b = o.entries[i] ? o.entries[i]->textureId : 0;
            if (a != b)
                return false;
        }
        return true;
    }
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
    /// Whole triangle draw of a cache whose mesh carries the per-face
    /// material stream (MeshData::materials), with the material arrays
    /// still authoritative (no scalar override on top): the backend
    /// shades emissive/specular/shininess from the stream instead of
    /// the scalars below. Never set on partial draws — those resolve
    /// their face's values into the scalars at translate time.
    bool perfacematerial = false;
    /// That stream's two alpha slots carry the PBR factor pair — the
    /// metallic where the emissive alpha is otherwise a constant 1, the
    /// roughness where the shininess sits — instead of a constant and
    /// the shininess (a per-face PBR appearance). Meaningful only with
    /// perfacematerial, and only the PBR shading branch reads them; the
    /// Phong branch shades a per-face PBR object from the same stream's
    /// colors, which are its Phong derivation.
    bool perfacepbr = false;
    bool lighting = true;        ///< false = flat base color (no light model)
    bool twoside = false;
    bool culling = false;
    bool ccw = true;             ///< front face vertex ordering
    bool transparent = false;    ///< uniform-color / texture transparency
    bool ontop = false;          ///< render after (over) the normal scene
    bool polygonoffset = false;  ///< glPolygonOffset on filled triangles
    /// SoDrawStyleElement::Style as the render cache captured it. The GL
    /// renderer hands LINES/POINTS to glPolygonMode; no modern API has
    /// that state, so the backend draws the primitives instead — see
    /// BGFXView::submitTessellation. This is how the Tessellation draw
    /// style arrives (SoFCUnifiedSelection overrides the element to
    /// LINES for it), and also how a plain SoDrawStyle node in the scene
    /// graph asks for a wireframe -- which drawstyleoverride tells apart.
    enum DrawStyle : uint8_t {
        DrawFilled = 0, DrawLines = 1, DrawPoints = 2, DrawInvisible = 3
    };
    uint8_t drawstyle = DrawFilled;
    /// The draw style above arrived as a scene-wide OVERRIDE, which is
    /// what the Tessellation display mode is -- SoFCUnifiedSelection sets
    /// SoOverrideElement's DRAW_STYLE for it, the only place in the tree
    /// that does. That mode wants the faces filled in the background
    /// colour to occlude what is behind them (Coin gets the same from
    /// SoRenderManager::HIDDEN_LINE); a lone SoDrawStyle node asks for a
    /// wireframe and nothing more, and filling it hides whatever it was
    /// drawn around.
    bool drawstyleoverride = false;
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

    /// Machined surface finish of a triangle draw (App::SurfaceFinish,
    /// carried by SoFCRenderMaterial, either authored on the appearance
    /// or stated by the ViewProvider Render_Finish* properties): a
    /// procedural pattern the backend shades as a perturbed normal, and
    /// as added roughness once its features fall below the pixel
    /// footprint. finish is the App::SurfaceFinish::Pattern value
    /// (0 = none, and an unrecognized one shades as none); finishpitch
    /// (feature spacing) and finishdepth (peak to valley) are in
    /// millimetres of the draw's OBJECT space, so an instanced or scaled
    /// copy keeps the finish attached to its geometry; finishangle is
    /// the lay direction in degrees. Only meaningful while lighting is
    /// on: a pattern is a shading fact, so the depth, shadow and
    /// picking passes ignore it.
    uint8_t finish = 0;
    float finishpitch = 0.0f;
    float finishdepth = 0.0f;
    float finishangle = 0.0f;

    /// Per-face form of that finish (null = the scalars above are the
    /// whole story). A finish is four numbers, so a per-face one rides a
    /// PALETTE of the distinct finishes plus one index per vertex in the
    /// material stream's third slot — not four more per-face arrays.
    /// Entry 0 is what the scalars repeat, which is what an unbound index
    /// attribute (a mesh with no stream) resolves to. Meaningful on whole
    /// triangle draws only, and only together with perfacematerial: a
    /// partial draw resolves its face's entry into the scalars at
    /// translate time and carries no palette. Shared and immutable, so
    /// pointer identity is a batch key (like usershader).
    std::shared_ptr<const FinishPalette> finishpalette;

    /// The frame the finish above is laid out in (Unframed = triplanar).
    /// The draw's own, which is also what the backend uploads as palette
    /// entry 0 -- so a partial draw that resolved one face's frame here
    /// needs no palette, exactly as it needs none for the finish.
    SurfaceFrame frame;

    /// Per-face form of that frame (null = the frame above is the whole
    /// story), indexed by the second byte of the material stream's third
    /// slot. Same rules
    /// as finishpalette: whole triangle draws with perfacematerial only,
    /// shared and immutable, pointer identity is a batch key. Unlike the
    /// finish this comes from the GEOMETRY rather than the appearance,
    /// so it is published only when a finish is stated somewhere -- a
    /// shape nobody finished must not pay for a stream it cannot use.
    std::shared_ptr<const FramePalette> framepalette;

    /// Per-face texture palette of a triangle draw (null = the draw's
    /// faces carry no images of their own, and `texture` above is the
    /// whole texturing story).
    ///
    /// One draw binds one sampler, so per-face images cannot be one
    /// texture each: the palette's entries become the LAYERS of a single
    /// array texture and the material stream's third slot, third byte,
    /// names a face's layer -- 0 being the untextured face, entry i
    /// being layer i + 1 (TexturePalette). Sampled on top of everything
    /// `texture` does: a face's own image modulates the fragment colour
    /// after the unit-0 texture has, so a shape can carry both.
    ///
    /// Shared and immutable, so pointer identity is a batch key and the
    /// key the backend caches the uploaded array under.
    std::shared_ptr<const TexturePalette> texturepalette;

    /// Where the per-face images above are laid out, in millimetres of
    /// OBJECT space per tile -- the size the image is printed at, which
    /// is what a physical decal or a machined marking has, rather than
    /// a fraction of a bounding box that changes when the part does.
    ///
    /// The coordinates themselves come from the face's own projection
    /// frame (`framepalette`, the same frames the finish is laid out
    /// in): a planar face in the plane's own axes, a turned one
    /// unwrapped about its axis, and a face with no analytic surface
    /// triplanarly off the object-space normal.
    ///
    /// <= 0 means the mesh's own texture coordinates instead, which is
    /// what a shape that was really UV mapped wants. A mesh that carries
    /// none then reads (0, 0) -- the image's corner texel -- exactly as
    /// the unit-0 texture does in the same situation.
    float facetexscale = 0.0f;

    /// The layer every fragment of this draw samples, or < 0 to read the
    /// stream. A single-face draw (a selection or preselection highlight)
    /// has no material stream to read an index out of, so the producer
    /// resolves that one face's layer here -- the same way it resolves
    /// the face's finish and PBR pair into the scalars.
    int8_t facetexlayer = -1;

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
    /// The glass claim came from the draw's MaterialX surface, not from
    /// Render_Glass: the producer resolved the document's transmission
    /// into the four fields above (docs/MaterialStorage.md sec 17.21).
    /// Two readings change with it. The body colour is glasscolor --
    /// LINEAR, as the document states it, never the authored diffuse
    /// and never decoded -- and a glassdensity of 0 is not "automatic"
    /// but NONE: OpenPBR's transmission_depth 0 means the colour tints
    /// the transmitted light once at the surface instead of absorbing
    /// over the body.
    bool glassmtlx = false;
    float glasscolor[3] = {1.0f, 1.0f, 1.0f};

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

/// Which primitive buckets a Class-A display style draws
/// (docs/CoinRetirement.md 5.7). Bit i is Material::Type i, so a style
/// admits a draw when `(mask >> mat.type) & 1`.
///
/// The masks are read straight off what the ViewProviders put under
/// each display-mode child of their SoFCSwitch, which the stage-5
/// survey found to be nested subsets of one set of nodes: Shaded is the
/// faces, Wireframe is the lines AND the points (Part's Wireframe root
/// contains its Points root), Flat Lines is all three.
///
/// StyleAsIs (zero) means *no* style override -- every draw the feed
/// captured is drawn. That is what "As Is" means, and it is what every
/// frame outside a unified canvas uses, because there the style is
/// still applied by the Coin traversal that produced the capture.
enum DrawStyleMask : uint8_t {
    StyleAsIs      = 0,
    StyleFaces     = 1 << Material::Triangle,
    StyleLines     = 1 << Material::Line,
    StylePoints    = 1 << Material::Point,
    StyleShaded    = StyleFaces,
    StyleWireframe = StyleLines | StylePoints,
    StyleFlatLines = StyleFaces | StyleLines | StylePoints,
    /// Not a mask: a display mode no mask can describe. See
    /// DrawCall::ownStyle.
    StyleUnknown   = 0xff,
};

/// Which bucket a draw actually RENDERS as, which is not always its
/// Material::Type.
///
/// The mask above reads a draw's bucket off `mat.type`, which is right
/// only while a ViewProvider builds its display-mode children out of
/// separate face, line and point geometry -- which is what Part does.
/// Mesh does not: its "Wireframe" and "Point" children are the SAME
/// mesh node re-styled by an SoDrawStyle (Mod/Mesh/Gui/ViewProvider.cpp
/// -- pcLineStyle is LINES, pcPointStyle is POINTS), so the cache emits
/// them as Material::Triangle carrying a drawstyle. Classifying those
/// by type alone files a wireframe rendering under faces, and a
/// Wireframe filter then drops the mesh entirely.
///
/// A scene-wide drawstyle OVERRIDE is not reclassified: that is the
/// Tessellation display mode, whose filled faces still occupy their
/// faces bucket (they are drawn to occlude), and `drawstyleoverride` is
/// exactly what tells it from a plain SoDrawStyle node in the graph.
inline uint8_t styleBitOf(const Material &mat)
{
    if (mat.type == Material::Triangle && !mat.drawstyleoverride) {
        if (mat.drawstyle == Material::DrawLines)
            return StyleLines;
        if (mat.drawstyle == Material::DrawPoints)
            return StylePoints;
    }
    return static_cast<uint8_t>(1u << mat.type);
}

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
    /// This draw does not define the SCENE's bounds: it is a navigation
    /// gizmo (the axis cross, the rotation-centre sphere), captured
    /// under a Gui::SoSkipBoundingGroup, which is what Coin leaves out
    /// of the scene bounding box. Its own bounds above stay valid --
    /// culling and clipping still need them; what reads this is the
    /// min/max over the published draws that sizes the shadow ground
    /// and drives the camera's auto near/far. The rotation-centre
    /// sphere moves with the spin, so a scene bound that counted it
    /// would move the ground and the clip planes while the view turns.
    bool skipbounds = false;
    /// The display mode this draw's OBJECT is in, as a DrawStyleMask,
    /// and which Class-A style NAMES its display-mode switch has a
    /// child for (SoFCOwnDisplayModeElement::StyleNameBit bits).
    ///
    /// What lets one capture serve views in different display styles
    /// (docs/CoinRetirement.md 5.8): the feed captures the SUPERSET
    /// child and each view resolves its own style per object, the way
    /// Rhino and SolidWorks do. `ownStyle` serves a view showing "As
    /// Is"; `registeredStyles` reproduces the rule that a style whose
    /// name an object's switch does not carry does not apply to that
    /// object at all.
    ///
    /// ownStyle == StyleUnknown means the object's mode is not one of
    /// the four (Mesh's "Point", FEM's "Faces & Wireframe"): its
    /// buckets cannot be named, so nothing may filter this draw.
    uint8_t ownStyle = StyleUnknown;
    uint8_t registeredStyles = 0;
    /// Additive-capture context (docs/CoinRetirement.md 5.9
    /// "Non-standard modes"), all interned mode ids / bits over the
    /// capture's CaptureInterestTable, zero everywhere outside an
    /// interest capture:
    ///
    /// - capturedMode: non-zero = this draw came from an ADDITIVELY
    ///   traversed display-mode child of that name, captured beside
    ///   the normal flow. Such a draw serves exactly one thing -- an
    ///   override resolving to that very mode -- and is dropped by
    ///   every other view, or it would double-draw the object.
    /// - traversedMode: the mode name of the child the NORMAL flow
    ///   traversed, when that name is in the interest set (else 0).
    ///   An override naming it admits the untagged draws as they are:
    ///   they already ARE the mode, and no tagged copy exists.
    /// - interestBits: which interest modes the object's switch has a
    ///   child for. What tells "the override's mode was captured
    ///   additively, suppress the normal draws" from "the object has
    ///   no such mode child, fall back to its own mode" -- the same
    ///   fallback registeredStyles gives a Class-A style.
    uint16_t capturedMode = 0;
    uint16_t traversedMode = 0;
    uint16_t interestBits = 0;
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
    /// A companion drawable of this draw's object was deferred by the
    /// publish's capture budget, so the object is on screen with part
    /// of itself missing or stale. The element gates read it to tell
    /// "the face/line set has not arrived yet" apart from "the display
    /// mode legitimately omits it" (docs/SceneStreaming.md #13b): an
    /// attached point or line set whose companion is merely late must
    /// wait for it, not claim the mode exemption. Publish-transient
    /// and desktop-only for now -- never serialized.
    bool objectIncomplete = false;
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
/// One step of ObjectInfo::path: a document object on the scene-graph
/// node chain that produced a draw.
struct ObjectRef {
    std::string doc;    ///< document internal name
    std::string obj;    ///< object internal name

    bool operator==(const ObjectRef &o) const
    { return obj == o.obj && doc == o.doc; }
};

struct ObjectInfo {
    std::string doc;    ///< document internal name (identity)
    std::string obj;    ///< object internal name (identity)
    std::string label;  ///< user-visible label, presentation only
    std::string type;   ///< DocumentObject type id, e.g. "Part::Box"
    /// The chain of document objects the draw's node path passes
    /// through, outermost first, ending at {doc, obj}; consecutive
    /// duplicates collapsed. Identity only, like doc/obj -- what a
    /// per-view display mode override entry matches against
    /// (docs/CoinRetirement.md 5.9): the leaf alone cannot say which
    /// CONTAINER the draw was reached through, and an override on a
    /// Link/group/assembly must reach the child draws below it. Not
    /// serialized by SceneDump: a remote viewer holds no per-view
    /// override table to resolve against.
    std::vector<ObjectRef> path;
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

/// One per-view per-object display mode override entry
/// (docs/CoinRetirement.md 5.9), parsed by the producer from the view's
/// ObjectDisplayModes property into resolved {doc, obj} steps so the
/// backend never touches a document.
struct StyleOverride {
    /// The objects the entry names, outermost first. Rooted: the first
    /// element must BE ObjectInfo::path[0] and the rest must follow it
    /// in order (an ordered subsequence, not a contiguous run, because
    /// a subname elides objects the scene chain contains -- a Link's
    /// target has a chain step but no subname token). Bare (one
    /// element, rooted false): the element may sit anywhere on the
    /// path -- the object wherever it appears in this view.
    std::vector<ObjectRef> path;
    bool rooted = true;
    /// The style to draw the matched objects with, when the entry's
    /// mode is one of the four Class-A names and the object's switch
    /// registers that name (DrawCall::registeredStyles) -- the same
    /// rule a view style follows. pin instead means the entry is
    /// "As Is": the object follows its OWN mode, escaping the view
    /// style (SolidWorks' "Default Display").
    uint8_t mask = StyleAsIs;
    uint8_t nameBit = 0;
    bool pin = false;
    /// Non-zero when the entry's mode is NOT one of the four Class-A
    /// names (docs/CoinRetirement.md 5.9 "Non-standard modes"): the
    /// interned id (internModeName) of the mode's name. Such a mode is
    /// a different subgraph, not a mask over the superset capture --
    /// the feed captures the named child ADDITIVELY, its draws tagged
    /// with this id (DrawCall::capturedMode), and the entry admits
    /// exactly the draws so tagged. mask/nameBit are meaningless when
    /// this is set.
    uint16_t modeId = 0;
};

/// A view's override table, handed to the backend by pointer: per
/// sub-view via SubViewFrame::styleOverrides, for the plain view via
/// setMainViewStyle(). The producer owns the storage and keeps it
/// alive while the backend may render with it; version is bumped on
/// every content change and is what the backend's resolved
/// objectKey cache keys on (together with objectInfoVersion()).
struct StyleOverrideTable {
    std::vector<StyleOverride> entries;
    uint32_t version = 0;
};

/// Process-lifetime intern table for display mode NAMES outside the
/// four Class-A styles (docs/CoinRetirement.md 5.9 "Non-standard
/// modes"). A name's id is stable for the life of the process and
/// never reused, so a draw tagged with it (DrawCall::capturedMode) and
/// an override entry naming it (StyleOverride::modeId) can meet at
/// submit with an integer compare, and no Coin type crosses into
/// Render. 0 is never returned for a real name; null/empty -> 0.
RendererExport uint16_t internModeName(const char *name);
/// The name behind an interned id, or null for 0/unknown. The returned
/// pointer lives as long as the process.
RendererExport const char *internedModeName(uint16_t id);

/// The capture's additive-mode interest list (docs/CoinRetirement.md
/// 5.9 "Non-standard modes"): the interned ids of every non-standard
/// mode any override of any view sharing the capture wants, in a fixed
/// order. The ORDER is the contract: bit i of DrawCall::interestBits
/// means "this draw's display-mode switch has a child named ids[i]",
/// so the producer that pushes this list to the traversal must hand
/// the SAME list here. At most 16 entries (the bit budget); the
/// producer drops and logs the excess, whose modes stay inert.
/// version bumps on every content change; the backend's resolved
/// override cache keys on it, because the id->bit mapping moved.
struct CaptureInterestTable {
    /// The bit budget: DrawCall::interestBits is 16 bits wide, so a
    /// list longer than this cannot be expressed. The producer drops
    /// and logs the excess.
    static const size_t MaxModes = 16;

    std::vector<uint16_t> ids;
    uint32_t version = 0;

    /// The interestBits bit of \a modeId, or 0 when not listed.
    uint16_t bitOf(uint16_t modeId) const {
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == modeId)
                return uint16_t(1u << i);
        }
        return 0;
    }
};

/// Flag bits of the selection ids fed through Renderer::addSelection
/// (mirroring SoFCRenderer::SelIdBits — the producer side of the feed).
enum SelIdBits : int {
    SelIdImplicit = 0x01000000,  ///< whole-object brought along a partial
    SelIdAlt      = 0x02000000,  ///< alternative (Ctrl) selection group
    SelIdFull     = 0x04000000,  ///< explicit whole-object selection
    SelIdPartial  = 0x08000000,  ///< sub-element selection
    SelIdSelected = SelIdFull | SelIdPartial,
};

/// A consumer that draws its own passes inside a renderer's frame
/// (docs/CAMSimRenderPort.md section 8 -- the borrowed frame).
///
/// The immediate-mode facade (DrawDevice.h) lets an outside module
/// draw with the backend; on its own it only lets that module own a
/// whole widget. A FrameConsumer instead draws INSIDE a 3D view's
/// frame, on pass ids taken from that view's own block, against that
/// view's scene target -- which is what makes its geometry sort with
/// the document's by depth rather than sit on top of it.
///
/// The renderer owns the surface and the frame boundary. Register with
/// Renderer::setFrameConsumer, and draw when drawFrame() is called:
/// there is no beginFrame/endFrame to run, because the host has
/// already begun the frame and bgfx's frame boundary is process-wide.
class RendererExport FrameConsumer
{
public:
    virtual ~FrameConsumer();

    /// How many passes to reserve inside the host frame. Read once
    /// per registration, so a consumer whose pass count changes
    /// re-registers. More than the host offers is refused (nothing is
    /// drawn) rather than silently clamped, because a clamp would put
    /// the consumer's last passes in somebody else's view id.
    virtual unsigned framePasses() const = 0;

    /// Of framePasses(), how many TRAILING passes belong in the
    /// overlay run -- drawn after the transparent composite, against
    /// the frame's finished depth. The rest are the scene run, drawn
    /// while the scene is still being composed: volumetric fog, water
    /// and glass, transparent geometry and everything after them see
    /// the scene run's output and the depth it writes. Ordering
    /// within each run follows pass index. Read at registration like
    /// framePasses(), and refused the same way when a run is over
    /// what the host offers (docs/CAMSimRenderPort.md sec 10.2).
    virtual unsigned overlayPasses() const { return 0; }

    /// Draw into \a surface. Called once per host frame; each pass's
    /// draws land where its run places them (overlayPasses() above),
    /// always before the on-top, highlight and overlay-feed passes.
    ///
    /// The surface's passes are live only for the duration of this
    /// call. Nothing here may cross the frame boundary or disturb the
    /// frame in progress: no scene feeds, no resize, no repaint
    /// request, and above all no bgfx frame of its own (the facade
    /// gives an attached surface no way to ask for one).
    virtual void drawFrame(DrawSurface &surface) = 0;
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

    /// One sub-view of a split-view frame (docs/SplitViews.md sec 9.2):
    /// a viewport rect on the output backbuffer, in device pixels, and
    /// the camera to render the resident scene with there. \a id is a
    /// stable client token naming the sub-view across frames -- the
    /// backend keys its per-sub-view state (sized targets, temporal
    /// accumulation) on it, so a layout change that keeps a cell keeps
    /// its id.
    struct SubViewFrame {
        int id = 0;
        int x = 0, y = 0;
        int width = 0, height = 0;
        const void *viewMatrix = nullptr;
        const void *projMatrix = nullptr;
        /// The Class-A display style this sub-view draws the shared
        /// scene with (docs/CoinRetirement.md 5.7, docs/SplitViews.md
        /// sec 17): a DrawStyleMask filtering the captured draws by
        /// primitive bucket at submit. StyleAsIs -- the default and
        /// every non-canvas frame -- draws what the feed captured.
        ///
        /// This is what lets N cells of ONE backend, fed by ONE
        /// traversal, show N different display styles. A style applied
        /// in the traversal instead would be baked into the shared
        /// capture, which is the whole reason the canvas could not vary
        /// it per cell.
        uint8_t drawStyle = StyleAsIs;
        /// The style NAME above, as a StyleNameBit, and whether the
        /// feed captured the SUPERSET child rather than each object's
        /// own mode (docs/CoinRetirement.md 5.8).
        ///
        /// Under a superset capture the filter is resolved per object:
        /// this sub-view's style where the object's display-mode switch
        /// carries a child of that name, and the object's OWN mode
        /// otherwise -- which is both what "As Is" means and what
        /// already happens today to an object whose switch does not
        /// carry the style's name (Mesh's "Point" under a "Points"
        /// override). Without the superset flag a mask can only remove,
        /// so it cannot serve a style that ADDS geometry the capture
        /// does not hold.
        uint8_t drawStyleName = 0;
        bool styleFromSuperset = false;
        /// The interned id (internModeName) of that style's NAME, when
        /// the capture carries the mode ADDITIVELY as well
        /// (docs/CoinRetirement.md 5.11): a style is an override, and
        /// an override's mode is its own SUBGRAPH -- a mask over the
        /// superset child reproduces it only while the superset child
        /// happens to contain the mode's buckets, which is a
        /// Part-shaped assumption and not a rule (Mesh's "Flat Lines"
        /// holds no point draws). Where the id is in the capture's
        /// interest list the sub-view draws the mode's own tagged
        /// draws and suppresses the untagged ones, exactly as an
        /// override naming the mode does; where it is not, the mask
        /// over the superset stays the answer. Zero outside a superset
        /// capture and for "As Is".
        uint16_t drawStyleMode = 0;
        /// This sub-view's per-object display mode overrides
        /// (docs/CoinRetirement.md 5.9), resolved per draw as the FIRST
        /// clause before the style above. Only meaningful under a
        /// superset capture -- an override can ADD geometry, which a
        /// filter over any other capture cannot serve. The producer
        /// owns the table and keeps it alive across the frame; null
        /// means no overrides.
        const StyleOverrideTable *styleOverrides = nullptr;
    };
    /// Render one frame as \a count sub-views tiling the backbuffer:
    /// the same resident scene feeds every sub-view, each drawn with
    /// its own camera into its own rect, inside a single backend frame.
    /// Returns false when the backend does not support it (the
    /// default), in which case the caller renders whole via render().
    virtual bool renderSubViews(const QColor &bg,
                                const SubViewFrame *subs, int count)
    {
        (void)bg; (void)subs; (void)count;
        return false;
    }
    /// Release the per-sub-view state a vanished sub-view id holds
    /// (targets, view-id block). Never id 0 -- that is the implicit
    /// full-canvas sub-view every plain render() uses.
    virtual void dropSubView(int id) { (void)id; }
    /// The plain (whole-canvas, sub-view id 0) frame's display style
    /// context (docs/CoinRetirement.md 5.9): the view's own Class-A
    /// style as mask + name bit, whether the feed captured the
    /// superset child, and the view's per-object override table (the
    /// caller owns it and keeps it alive; null = none). A plain view
    /// normally leaves all of this at rest -- its traversal applies
    /// its style -- but a view with overrides captures the superset
    /// like a canvas cell and needs the backend to resolve the style
    /// per object the same way.
    virtual void setMainViewStyle(uint8_t styleMask, uint8_t styleNameBit,
                                  bool fromSuperset,
                                  const StyleOverrideTable *overrides,
                                  uint16_t styleMode = 0)
    {
        (void)styleMask; (void)styleNameBit;
        (void)fromSuperset; (void)overrides; (void)styleMode;
    }
    /// The additive-mode interest list of the capture feeding this
    /// backend (docs/CoinRetirement.md 5.9 "Non-standard modes") --
    /// the SAME list, in the same order, that the producer pushed to
    /// the traversal, because it defines what DrawCall::interestBits'
    /// bits mean. One per renderer, not per sub-view: the interest is
    /// a property of the shared capture. The caller owns the storage
    /// and keeps it alive while the backend may render with it; null
    /// (the default and the at-rest state) means no interest capture.
    virtual void setCaptureInterest(const CaptureInterestTable *table)
    {
        (void)table;
    }
    /// Prepare the backend for a renderSubViews frame: build the sized
    /// targets of every unseen sub-view id up front, each against a
    /// freshly reclaimed handle pool, and release what the layout
    /// obsoletes (the implicit full-canvas sub-view's targets when no
    /// sub is id 0), so the frame itself allocates nothing and never
    /// bails. Idempotent and cheap once every bank is warm, so the
    /// host may simply call it at the top of every layout frame -- but
    /// it crosses backend frame boundaries, so it must run while
    /// nothing of the upcoming frame is queued (before any page-cell
    /// draw). Optional: a backend without it just heals the first
    /// frame or two after a layout change.
    virtual void prepareSubViews(const QColor &bg,
                                 const SubViewFrame *subs, int count)
    {
        (void)bg; (void)subs; (void)count;
    }

    /// Render one frame for an offscreen capture -- a screenshot or an
    /// image export -- instead of the on-screen one. Two things differ
    /// from render(): the frame is rendered at \a width x \a height
    /// whatever the host widget's size is, and the finished image is
    /// transferred into the framebuffer the caller has bound rather
    /// than the widget's. Everything else is an ordinary frame with the
    /// same feeds, and the matrices mean what they mean in render() --
    /// build them for the capture's aspect ratio, not the widget's.
    /// Returns false when the backend cannot capture (the default), in
    /// which case the caller's own render path must draw the frame.
    virtual bool renderOffscreen(const QColor &bg,
                                 const void *viewMatrix,
                                 const void *projMatrix,
                                 int width, int height)
    {
        (void)bg; (void)viewMatrix; (void)projMatrix;
        (void)width; (void)height;
        return false;
    }

    /// Restrict subsequent renderOffscreen() frames to the scene draws
    /// of the named objects ({document internal name, object internal
    /// name}, as in ObjectInfo), with the selection, preselection and
    /// overlay feeds stripped and the window background flat
    /// transparent -- the per-capture object filter of the shaded-
    /// underlay capture (docs/TechDrawPortAndSection.md sec 26.2).
    /// Returns false when the backend cannot filter (the default) or
    /// when any named object has no draw in the resident scene (a
    /// 3D-hidden source has none) -- the caller must then fall back to
    /// its own capture path. Active until clearCaptureFilter(); the
    /// caller owns that bracket.
    virtual bool setCaptureFilter(
            const std::vector<std::pair<std::string, std::string>> &objects)
    {
        (void)objects;
        return false;
    }
    virtual void clearCaptureFilter() {}

    /// Provide a transient scene for subsequent renderOffscreen()
    /// frames: the supplied draws stand in for the resident scene feed
    /// during the capture -- selection, preselection and overlay feeds
    /// stripped, flat background, default camera-aligned headlight --
    /// without disturbing the resident feeds or their GPU residency.
    /// This is the shaded-underlay derived-shape capture (docs/
    /// TechDrawPortAndSection.md sec 31): a section's cut solid or a
    /// detail's clipped region exists nowhere in the resident scene, so
    /// the caller builds a dedicated Coin scene, runs it through the
    /// render-cache pipeline (SoFCRenderCacheManager::traverse +
    /// RendererBridge::translate) and hands the translated draws here.
    /// Returns false when the backend cannot render a supplied scene
    /// (the default). Active until clearCaptureScene(); the caller owns
    /// that bracket. Takes precedence over setCaptureFilter() while
    /// both are set.
    virtual bool setCaptureScene(DrawCallList &&draws)
    {
        (void)draws;
        return false;
    }
    virtual void clearCaptureScene() {}

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
    /// How many times setScene() has restated the scene: a consumer
    /// that derives something from the same feed (the Cycles viewport
    /// re-translates the render cache, docs/CyclesIntegration.md sec
    /// 5.2) compares this between frames instead of the draws. Every
    /// override of setScene() owes a noteSceneStated().
    uint64_t sceneGeneration() const { return scenegen; }
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
    /// Register (or clear, with null) the consumer that draws its own
    /// passes inside this renderer's frames (FrameConsumer above).
    /// One per sub-view: registering a second under the same \a subView
    /// replaces the first, whose surface is destroyed. Backends that do
    /// not implement the draw facade ignore this, and the consumer
    /// keeps its own path.
    ///
    /// \a subView scopes the registration to one sub-view of a
    /// renderSubViews frame (SubViewFrame::id), so that N cells of one
    /// backend can each carry a consumer of their own -- a path-traced
    /// cell beside a rasterized one (docs/CyclesIntegration.md sec
    /// 5.11). 0, the default, is the implicit full-canvas sub-view a
    /// plain render() draws, and what every non-canvas host uses. A
    /// consumer registered under a sub-view is dropped with it
    /// (dropSubView).
    virtual void setFrameConsumer(FrameConsumer *consumer, int subView = 0)
    { (void)consumer; (void)subView; }
    /// The surface the FrameConsumer registered under \a subView draws
    /// into, or null when none is registered there (or the backend
    /// serves no consumers). Stable for the lifetime of the
    /// registration: a consumer keys per-host state on it
    /// (docs/CAMSimRenderPort.md sec 11.9) and drops that state when
    /// detaching, before the surface dies.
    virtual DrawSurface *frameConsumerSurface(int subView = 0)
    { (void)subView; return nullptr; }
    /// Per-frame hidden-line draw style state (resolved from the traversal
    /// state each render, like the GL renderer does).
    virtual void setHiddenLineConfig(const HiddenLineConfig &config)
    { (void)config; }
    /// The registered FrameConsumer supplies the shaded image of the
    /// scene (a path tracer's, docs/CyclesIntegration.md sec 5.1 and
    /// 5.3): the backend rasterizes its scene triangles depth-only, so
    /// that everything it still draws -- feature lines and points,
    /// selection and preselection, on-top draws, the host's own
    /// effects -- occludes against its own depth over the consumer's
    /// colour; transparent scene triangles are not drawn at all (the
    /// consumer's image carries their alpha), and a non-on-top
    /// selection fill dims where the scene depth hides it instead of
    /// vanishing. Ignored by backends without a consumer. Scoped like
    /// the registration: \a subView names the sub-view whose consumer
    /// supplies the image, so only that cell of a split-view frame
    /// gives up its raster shading.
    virtual void setExternalBaseLayer(bool on, int subView = 0)
    { (void)on; (void)subView; }
    /// Per-frame section fill (cap) configuration.
    virtual void setSectionConfig(const SectionConfig &config)
    { (void)config; }
    /// Per-frame ambient occlusion configuration.
    virtual void setAOConfig(const AOConfig &config) { (void)config; }
    /// Per-frame screen-space cavity (curvature) shading configuration.
    virtual void setCavityConfig(const CavityConfig &config)
    { (void)config; }
    /// Per-frame matcap shading configuration.
    virtual void setMatcapConfig(const MatcapConfig &config)
    { (void)config; }
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
    /// User shaders compile asynchronously on the desktop (docs/
    /// RenderDebug.md sec 6): a frame drawn while one is in flight
    /// draws the stock material in its place. Whether any compile is in
    /// flight, and a counter that advances each time one finishes
    /// (either way) -- what a consumer that rendered a stand-in frame
    /// polls to know when to render again. Defaults: never pending,
    /// never advancing.
    virtual bool shaderCompilePending() const { return false; }
    virtual int shaderCompileGeneration() const { return 0; }
    /// Per-frame physically based shading configuration.
    virtual void setPBRConfig(const PBRConfig &config) { (void)config; }

    /// The output colour transform (OutputConfig): what happens to the
    /// finished frame before it is shown.
    virtual void setOutputConfig(const OutputConfig &config) { (void)config; }
    /// Per-frame bump/normal mapping configuration.
    virtual void setBumpConfig(const BumpConfig &config) { (void)config; }
    /// Per-frame scene light (Shadow draw style).
    virtual void setLightConfig(const LightConfig &config) { (void)config; }
    /// Per-frame ordinary Coin lights (viewer headlight and backlight,
    /// document SoDirectionalLight / SoPointLight). A backend that does
    /// not implement this keeps its fixed headlight, which is also what
    /// an unfed config asks for.
    virtual void setViewLightConfig(const ViewLightConfig &config)
    { (void)config; }
    /// Per-frame volumetric lighting (light shaft) configuration.
    virtual void setVolumetricConfig(const VolumetricConfig &config)
    { (void)config; }
    /// Per-frame water surface (refraction/reflection) configuration.
    virtual void setWaterConfig(const WaterConfig &config)
    { (void)config; }
    /// Per-frame bloom (glow) configuration.
    virtual void setBloomConfig(const BloomConfig &config)
    { (void)config; }
    /// Idle temporal accumulation configuration. A backend that does not
    /// implement this simply keeps drawing single-sample frames, which is
    /// also what an unfed config asks for.
    virtual void setTemporalConfig(const TemporalConfig &config)
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
    /// The element contract's inputs (docs/SceneStreaming.md #13b),
    /// pushed in like every other parameter -- this library knows
    /// nothing of RenderParams. The contract itself lives in the
    /// backend: an attached point set draws only while its object's
    /// line set is shown and memory allows, an attached line set only
    /// while its face set is shown and memory allows, floating sets
    /// rank with the faces, and pressure spends points -> lines ->
    /// faces, taking them back in reverse.
    /// \a shapeVertices false suppresses attached point sets outright
    /// instead of letting the contract decide; \a pressureEdges false
    /// exempts attached line sets from the pressure stages;
    /// \a loadingDrop true forces both drops while a document is still
    /// arriving -- whether one is loading is a question only the host
    /// can answer, so it is pushed as a state and not derived here.
    /// \a staggerFrames is the frame wait between pressure stages,
    /// escalating and releasing both. None of these ever touches an
    /// on-top or highlight draw.
    /// MEASUREMENT ONLY (Render_TinyElementCutoff, 0 = off): suppress
    /// every line and point draw of this many primitives or fewer,
    /// floating sets included -- which the element contract never
    /// gates, and which is exactly the population a far-field cut is
    /// left drawing (docs/FarFieldProxies.md 11.1i). It exists to
    /// price the DRAW axis in the ~12-primitives-per-draw regime,
    /// which this engine has only ever measured at ~1540.
    virtual void setTinyElementCutoff(int prims) { (void)prims; }

    virtual void setElementGates(bool shapeVertices, bool pressureEdges,
                                 bool loadingDrop, int staggerFrames)
    { (void)shapeVertices; (void)pressureEdges; (void)loadingDrop;
      (void)staggerFrames; }
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
    /// The host's word that the NEXT frame is not the whole scene -- a
    /// publish deferred shapes under its capture budget, so what the
    /// backend holds is a scene still arriving. A pending dump is not
    /// consumed by that frame; the backend holds it for a later one,
    /// as it does for a frame drawn with a user shader still compiling.
    /// Per frame: cleared once the frame has run.
    virtual void holdFrameDump() {}
    /// Whether the last rendered frame held a pending dump (for a
    /// compiling shader or at the host's word). A pumping caller reads
    /// it as progress: a held frame is a frame, and one that builds a
    /// dozen programs on a software driver takes longer than any quiet
    /// timeout should.
    virtual bool frameDumpHeld() const { return false; }
    /// Whether the last rendered frame was COMPLETE -- the picture as
    /// authored: every user-shader program compiled, every shape the
    /// publish deferred arrived, a frozen frame's particle warm-up
    /// reached, and no mesh refine asked for by the level ladder. The
    /// same verdict that holds a frame dump, exposed so a test (or
    /// anyone else) can wait for the picture instead of for a number
    /// of frames. Costs the frame two flag writes and a compare; every
    /// wait lives in the caller. True where the backend has nothing
    /// to say.
    virtual bool frameComplete() const { return true; }
    /// Frames rendered, and complete frames rendered, since the backend
    /// came up. A waiter records the complete count, pumps, and stops
    /// when it advances -- a verdict left over from before the request
    /// proves nothing about the scene since -- and reads the rendered
    /// count as progress, so a frame that takes longer than any quiet
    /// timeout never runs it out.
    virtual uint64_t renderedFrames() const { return 0; }
    virtual uint64_t completeFrames() const { return 0; }
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
    /// Give back every render target this view holds, because nobody is
    /// looking at it (docs/RenderEngine.md #3.3). A view's targets are
    /// its dominant cost -- measured at 287MB for a 1644x653 view with
    /// all effects on -- and they are held whether or not the view is
    /// on screen, so a session with several documents open pays for all
    /// of them to show one. The scene, the programs and the uniforms
    /// stay: this is the resize path's release, and the next frame
    /// rebuilds the targets exactly as a resize does.
    ///
    /// Costs the frame that rebuilds them (~68ms measured on the same
    /// view), so the caller is expected to be sure the view is going to
    /// stay in the background rather than release on every tab click --
    /// hence Render/BackgroundReleaseDelay. The picture that comes back
    /// is byte-identical; what this trades is a hitch, never an image.
    ///
    /// Returns false when the backend holds no releasable targets.
    virtual bool releaseTargets() { return false; }
    //@}

protected:
    /// Record that the identity table has just been stated whole, so a
    /// producer holding a resident copy of it can tell. Every override of
    /// setObjectInfo() owes this call; updateObjectInfo() must not make
    /// it, because a delta leaves the producer's copy still describing
    /// what the renderer holds.
    void noteObjectInfoStated() { ++infoversion; }
    /// Record that setScene() has just restated the scene
    /// (sceneGeneration() above).
    void noteSceneStated() { ++scenegen; }

private:
    static uint64_t nextInstanceId()
    {
        static std::atomic<uint64_t> counter{0};
        return ++counter;
    }
    const uint64_t instanceid = nextInstanceId();
    uint32_t infoversion = 0;
    uint64_t scenegen = 0;
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

    /// Bring the backend up before anything asks it to draw.
    ///
    /// Backend startup is one-time and per process -- a GL context and
    /// the device -- but it was paid by whoever created the first 3D
    /// view, which made the first New Document of a session visibly
    /// slower than every later one. A host that knows a backend will be
    /// wanted can call this early instead; create() then finds the
    /// device already up.
    ///
    /// \a widget supplies the pixel format to build the context from,
    /// and need not be the widget that will eventually draw.
    /// Returns false if the backend does not warm up, or could not --
    /// in which case nothing is broken and create() will try again.
    ///
    /// Milliseconds per phase, so the host can report where the time
    /// went rather than only that it was spent.
    struct WarmupTiming {
        double context = 0;   ///< graphics context and its surface
        double device = 0;    ///< the backend device itself
        double programs = 0;  ///< shader programs and render targets
        double flush = 0;     ///< handing that work to the driver
        double total = 0;
    };
    virtual bool warmup(QOpenGLWidget *, const std::string &,
                        WarmupTiming * = nullptr) { return false; }

    /// Whether this backend's device is up on OpenGL through a context
    /// in Qt's global share group -- the precondition for a host to
    /// composite a backend-rendered texture into its own Qt GL widget
    /// (the 2D page engine's interactive path does exactly that).
    virtual bool deviceSharesQtGL() const { return false; }
    /// Make the device's own GL context current / release it, for
    /// engine code that must pump a backend frame outside a 3D view's
    /// paint (the single-threaded GL device executes its frame in
    /// whichever context is current). Only meaningful when
    /// deviceSharesQtGL(); the caller owns re-acquiring its own
    /// context afterwards.
    virtual bool deviceMakeCurrent() { return false; }
    virtual void deviceDoneCurrent() {}
    /// The backend's immediate-mode draw facade (DrawDevice.h,
    /// docs/CAMSimRenderPort.md section 3), or null: the backend does
    /// not implement the facade, or its graphics device is not up yet.
    /// A virtual rather than a hard-linked entry point so that any
    /// backend can implement it.
    virtual DrawDevice *drawDevice() const { return nullptr; }
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
    /// Bring \a type's backend up ahead of the first create(), so that
    /// the first 3D view of a session does not pay for it. See
    /// RendererLib::warmup.
    static bool warmup(const std::string &type, QOpenGLWidget *widget,
                       RendererLib::WarmupTiming *timing = nullptr);
    /// The first registered backend's draw facade (DrawDevice.h), or
    /// null while none has its device up. DrawDevice::instance() is
    /// the public door to this.
    static DrawDevice *drawDevice();
    static void registerLib(RendererLib *);
    static void setResourcePath(const std::string &path);
    static const std::string &resourcePath();

    /// Highest number of view ids the backend should hand out, or 0 for
    /// the backend's own maximum.
    ///
    /// Every 3D view holds a block of ids for its pass sequence, so this
    /// is what decides how many viewers a session can drive on the
    /// backend at once. It is a STARTUP option: backend startup is
    /// one-time per process (RendererLib::warmup), so the value in force
    /// is whichever was set before the first create()/warmup() and a
    /// change needs a restart to take effect.
    ///
    /// The renderer library cannot read a preference itself, so the host
    /// seeds it -- the same arrangement as setResourcePath and the
    /// scene-stream thread cap.
    static void setMaxViewIds(int count);
    static int maxViewIds();

    /// The RendererLib device-context hooks, resolved over the
    /// registered backends (the process has at most one device; the
    /// first lib that answers wins). See RendererLib::deviceSharesQtGL.
    static bool deviceSharesQtGL();
    static bool deviceMakeCurrent();
    static void deviceDoneCurrent();
};

} // namespace Render

#endif // RENDERER_RENDERER_H
