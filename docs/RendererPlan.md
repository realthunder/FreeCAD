# Renderer Plan: bgfx-based Render Engine

Status: draft, 2026-07-16; amended 2026-07-17 (Phase 2b: material/settings
plumbing + glTF interchange). Companion to `RoadMap.md` (renderer workstream)
and `ComputeBoundaries.md`.

Goal: a modern render engine for FreeCAD with PBR materials + IBL, SSAO,
shadows with volumetric lighting, order-independent transparency, capped
section views, outline rendering, and conventional CAD shaded/wireframe
display — portable across desktop GL/Vulkan/Metal/D3D, browser (WASM), and
mobile via bgfx's runtime backend selection.

**Hard constraint: the existing plain-GL pipeline must keep working unchanged.**
That means render cache modes 0–2 (pure Coin) and mode 3's `SoFCRenderer`
fixed-function GL path stay the default and untouched. The new engine only
activates in mode 3 *and* when a renderer type is explicitly selected, and it
must degrade to the current behavior when unavailable.

---

## 1. Current architecture (deep-dive summary)

The mode-3 ("Experimental" render cache) pipeline, which is the foundation the
new engine builds on:

```
Coin scene graph
  │  SoCallbackAction traversal (not SoGLRenderAction)
  ▼
SoFCRenderCacheManager        src/Gui/Inventor/SoFCRenderCacheManager.cpp
  │  per-SoFCSelectionRoot SoFCRenderCache, per-shape SoFCVertexCache,
  │  node-pointer keyed cache tables + node-id validation, lazy rebuild
  ▼
SoFCRenderCache::VertexCacheMap = flat_map<Material, VertexCacheEntry[]>
  │  Material = fully flattened render state: diffuse/ambient/emissive/
  │  specular, line/point style, depth state, polygon offset, on-top order,
  │  clip planes (!), lights, textures, outline flag   SoFCRenderCache.h:184
  │  VertexCacheEntry = {SoFCVertexCache, model matrix, partidx, key}
  ▼
SoFCRenderer                  src/Gui/Inventor/SoFCRenderer.cpp
  │  setScene/addSelection/setHighlight → DrawEntry buckets
  │  (opaque / transparent / on-top / selection / highlight × tri/line/point)
  │  ~15-sub-pass fixed-function GL loop      SoFCRenderer.cpp:2173
  ▼
SoFCVertexCache::renderTriangles/Lines/Points → VBO glMultiDrawElements
```

Key facts for the bridge design:

- **Activation**: `ViewParams::getRenderCache() == 3` (`isUsingRenderer()`).
  `SoFCUnifiedSelection::GLRenderBelowPath` routes to `manager.render(action)`
  and skips Coin traversal entirely (SoFCUnifiedSelection.cpp:1688).
  The manager is owned by value per unified-selection root
  (SoFCUnifiedSelection.cpp:276), i.e. one pipeline per 3D view.
- **`Render::Renderer` today** (src/Gui/Renderer/): 3-method interface
  (`render(bg, viewMatrix, projMatrix)`, `boundBox`), backends self-register
  into `RendererFactory`. `View3DInventorViewer::renderScene()` calls
  `renderer->render()` *before* the Coin pass; the bgfx backend draws into its
  own offscreen target and `glBlitFramebuffer`s color+depth into the Qt
  default framebuffer, so Coin composites on top with correct depth
  (BGFXRenderer.cpp:554). Both POC backends draw a demo cube grid only.
- **Not wired**: `View3DInventorViewer::setRendererType()` has *no caller*.
  The 3D-view prefs page saves a `RendererType` string (visible when render
  cache = 3) but `View3DSettings` never applies it.
- **Vertex data**: separate (non-interleaved) ref-counted attribute arrays —
  position/normal float3, texcoord float4, color packed RGBA8 — with separate
  16/32-bit index sets for triangles/lines/points and per-part (per-face)
  offset tables enabling partial draws (single face/edge highlight).
  Arrays are shared across cache generations and exposed as plain pointers
  (`getVertexArray()` etc.), so GPU upload needs no Coin involvement.
- **Sections today**: clip planes are captured into `Material::clippers`;
  capping is stencil-based (`_renderSection`: INVERT stencil over solid
  geometry, then fill with optional hatch texture; SoFCRenderer.cpp:1646).
  Solid detection comes from the `SoFCShapeInfo`/`SoFCShapeInstance` nodes
  attached by `ViewProviderPartExt::updateVisual()`.
- **Outline today**: geometric stencil outline per part (fill stencil, redraw
  `GL_LINE` where stencil ≠ 1; SoFCRenderer.cpp:1325) plus a whole-scene
  variant for the hidden-line draw style.
- **Transparency today**: back-to-front sort of draw entries by bbox-center
  distance; per-triangle depth sort inside a cache is disabled
  (`depthSortTriangles` returns FALSE unconditionally,
  SoFCVertexCache.cpp:1898).
- **Instancing groundwork**: `ViewProviderPartExt` keeps a global shape table
  keyed on the OCCT `TShape` (shared geometry across placements,
  ViewProviderExt.cpp:1786) and attaches `SoFCShapeInstance` nodes
  (partIndex + transform + shared `SoFCShapeInfo`). Currently only used for
  solid detection. The old `LinkShapeTable` branch (a6f714368d, 2021) tried
  full shape-node dedup and recorded the lesson in its commit message: many
  small shared nodes are *slower* without real GPU instancing; the win
  requires per-instance transforms at draw level — exactly what bgfx provides.
  The current `SoFCVertexCache::merge` machinery (RenderCacheMergeCount*)
  attacks draw-call count by merging small caches instead.

### Known defects noticed during the dive (all in the GL path)

- ~~`SoFCVertexCache.cpp:945` — missing `!` in the `isOfType` check inside the
  partial-solid loop; the `hassolid == 1` path never collected
  `solidpartindices`.~~ Fixed.
- ~~`SoFCRenderCache.h:376` — `Material::operator=` had identical then/else
  branches (dead conditional).~~ Fixed (removed the redundant special members).
- ~~`_renderSection` (grouped section fill, `setupmatrix=true`) transformed
  `DrawEntry::bbox` by the entry's model matrix even though the DrawEntry
  constructor already applies it — doubly transformed bounds of non-identity
  entries inflated the grouped cap quad and shifted its hatch phase.~~ Fixed
  (2026-07, found while verifying the bgfx section caps); the ungrouped
  default path is bit-identical before/after.
- Per-cache transparent sorting hard-disabled: `depthSortTriangles` starts
  with an unconditional `return FALSE;` added by 8c96674289 (2023-11) while
  fixing partial rendering for face sets without part numbers — the
  triangle-level sort permutes the shared index buffer in place, which
  invalidated part-relative partial draws. The kill switch also disables the
  *non-destructive* part-level sort (`sortedpartarray` multi-draw ordering).
  Consequence: transparent geometry within one cache renders in creation
  order (cross-object bbox ordering still applies). Superseded by WBOIT in
  the new engine; a minimal GL-path improvement would re-enable only the
  part-sorted branch.

---

## 2. Backend decision

Research snapshot 2026-07 (see PR/issue links in section 6):

| | bgfx | Diligent Engine |
|---|---|---|
| Health | Active daily, ~26 contributors/quarter, shipped by Minecraft, Babylon Native, MAME, ProtoTwin (browser industrial sim) | Active daily but bus factor = 1, no tagged release since 2024-09, small community |
| Browser | WebGL1/2 via Emscripten mature today; WebGPU backend rewritten 2026-01, Dawn-native only, browser support is the maintainer's declared 2026 focus | WebGL2 + WebGPU shipping since 2024-09 with live demos |
| Metal / mobile | Open source, shipping | **Metal backend closed-source (commercial license)** |
| Built-ins | Examples only: WBOIT (ex.19), ASSAO (ex.39), IBL (ex.18); no SSR/sections/outline; no geometry shaders by design | Maintained modules: PBR+IBL, SSR, SSAO, TAA, OIT (weighted + layered), selection outline, shaded+edges mode; sections DIY |
| Shaders | Own GLSL dialect, offline `shaderc` per backend | HLSL-first, runtime or offline cross-compile |

Decision: **bgfx is the primary backend** (matches RoadMap; no licensing
landmine on Apple platforms; strongest longevity signals; the browser story we
need first — WebGL — is its most mature path). The `Render::Renderer`
abstraction stays honestly backend-neutral and the Diligent POC is kept
compiling as a hedge. Diligent's Apache-2.0 module shaders (DiligentFX
PostProcess, Hydrogent task graph) serve as reference implementations when
writing our bgfx passes. Re-evaluate wgpu-native/Dawn in ~12 months: WebGPU
now ships in all major browsers, Kitware moved VTK to it for exactly this
workload, and it would collapse the backend matrix to one modern API — but
today it lacks bgfx's reach and our WebGL fallback need.

Feature-technique choices (what shipping CAD viewers actually do):

- **Reflections: IBL, not SSR.** No shipping CAD viewer uses SSR (SolidWorks
  RealView, Fusion 360 use environment maps). SSR is deferred to an optional
  post pass; photoreal output is a future path-tracer handoff, not stacked
  screen-space effects.
- **OIT: weighted-blended** (McGuire/Bavoil) — the CAD-industry standard
  (OCCT 7.2+ uses it), one geometry pass + composite, works on WebGL2
  (float RT), no compute/atomics needed. Per-pixel linked lists only as a
  much later native/WebGPU upgrade.
- **AO: ASSAO first** (bgfx ex.39 port), GTAO later where compute exists.
- **Sections: shader clip + stencil capping**, porting the semantics already
  in `_renderSection` (hatch fill included). Screen-space cap approximation
  as fallback for non-watertight input.
- **Outline: keep the geometric B-rep edge advantage** (free, exact, from
  OCCT tessellation) + a screen-space depth/normal post pass for the
  outline/hidden-line draw styles; drop the multi-pass-per-part stencil
  technique (it's a draw-call multiplier).
- **PBR: glTF metallic-roughness** with matcap/Phong fallback sharing the
  same vertex pipeline.
- **Shadows: cached variance shadow map (VSM), EVSM upgrade.** The scheme
  that fits CAD: one dominant directional (or spot) light, and a scene that
  is *static between edits* — so the shadow map is rendered once and reused
  until geometry/light changes, not every frame (large-assembly friendly;
  the existing `ShadowSync`/`ShadowExtraRedraw` params already model this).
  VSM specifically because (a) it is what Coin's `SoShadowGroup` implements,
  so the fork's existing GL "Shadow" draw style (31 `Shadow*` ViewParams:
  spot/directional, ground plane + texture/bump, per-object casting flags)
  has directly portable semantics and a pixel-comparable reference; (b) its
  pre-blurred softness gives the soft, stable shadows CAD viewers want with
  a single filterable texture — no per-pixel kernel like PCF, which shimmer
  on the razor-straight edges CAD tessellation produces; (c) it is plain
  fragment-shader + linear filtering on an RG16F/RG32F target — WebGL2-safe,
  no compute. Light bleeding at overlapping occluders is VSM's known flaw;
  the EVSM (exponential VSM) upgrade fixes most of it in the same pipeline.
  Rejected: stencil shadow volumes (per-silhouette draw-call and fill-rate
  multiplier — exactly wrong for large assemblies; hard shadows only);
  cascaded maps (deferred until warranted — CAD frames one model, not an
  open world; a single well-fit cascade + the cached-map policy suffices);
  ray-traced shadows (no WebGL/mobile story).
- **Volumetric lighting: shadow-map-raymarched light shafts** as a post
  pass — march the view ray per pixel at half resolution (~32-48 steps,
  dithered start offset), accumulating inscatter where the shadow map says
  the light reaches, then bilateral-upsample and composite before the
  transparent bucket. Pure fragment-shader work over the same VSM/EVSM
  texture (WebGL2-safe), and it inherits the cached-map economics. This is
  the presentation-quality \"studio\" effect (dusty workshop shafts), not a
  physical fog model — froxel/compute volumetrics (Frostbite-style) stay a
  much later native/WebGPU option, same policy as GTAO vs the SSAO cut.

---

## 3. Phases

Estimates are focused full-time weeks for one senior dev who knows this
codebase; calendar time will stretch with everything else going on.

### Phase 0 — bridge foundation (8–10 wks)

The engine-agnostic core; everything later depends on it.

1. **Wiring** (small, do first): call `setRendererType()` from
   `View3DSettings` / viewer setup so the prefs `RendererType` takes effect;
   handle renderer-creation failure by falling back to the GL path.
   *Done (2026-07), including a fix for the bgfx static-teardown crash at
   application exit.*
2. **Interface growth**: extend `Render::Renderer` from demo-cube signature to
   a scene API mirroring `SoFCRenderer`'s:
   `setScene / addSelection / removeSelection / setHighlight / clearHighlight
   / render / boundBox`.
   *Done (2026-07).* Feed topology decided: the backend hangs off
   `SoFCRenderer` itself (`setExternalRenderer()`, plumbed viewer →
   `SoFCUnifiedSelection` → manager → renderer). `SoFCRenderer` keeps
   building all its structures (fallback + bbox), forwards each feed in
   translated form, and skips its own GL emission while the backend
   reports `canSkipInternal()` (`FC_RENDERER_PARALLEL_GL=1` keeps GL
   drawing on top for A/B). The neutral types (`Render::MeshData` /
   `Material` / `DrawCall`, Coin-free — FreeCADRenderer cannot link Gui)
   are produced by the Gui-side bridge
   `src/Gui/Inventor/SoFCRendererBridge.{h,cpp}`; `MeshData::owner`
   ref-holds the `SoFCVertexCache` so arrays outlive the feed call.
3. **Geometry/material translation**: consume `VertexCacheMap` — upload
   attribute + index arrays keyed by `SoFCVertexCache::getCacheId()`;
   translate `Material` (flags → pipeline state, colors, clip planes,
   textures); keep per-part offset tables for partial draws.
   *Basic version done (2026-07):* interleaved pos+normal+rgba8 vertex
   buffer + INDEX32 tri/line/point buffers per cache id, uploaded lazily at
   render time and evicted when unreferenced for 2 frames; material →
   bgfx state (depth func/test/write, cull, blend, PT_LINES/POINTS) +
   headlight Blinn-Phong shaders (`bgfx/shaders/*.sc`, compiled by
   `compile.sh` into the runtime assets). *Partial draws done (2026-07)*:
   `SoFCVertexCache::get{Triangle,Line,Point}PartRange()` exposes the
   per-part index ranges (parts-table or primitive-unit, mirroring the
   GL partial render path), the bridge resolves `partidx` into
   `DrawCall::indexStart/indexCount`, and the backend does ranged
   `setIndexBuffer` — single-face/edge selection and preselect now
   render. *Polygon offset (2026-07)*: `glPolygonOffset(factor, units)` on
   filled triangles is bridged and approximated by a constant NDC depth
   bias in the vertex shader (`u_params.w`; no per-pixel slope term —
   bgfx has no fixed-function polygon offset). *Clip planes done
   (2026-07, no caps)*: the bridge resolves `Material::clippers` into
   world-space plane equations (`Render::Material::clipplanes`, max 6),
   applying `applyMaterial`'s on-top exception (`NoSectionOnTop` /
   concave), and the backend renders clipped draws with `*_clip` shader
   program variants that `discard` (`fc_clip.sh`); concave mode
   (`SectionConcave`) becomes a union-of-half-spaces test in the same
   shader instead of GL's one-plane-per-pass loop. The unclipped
   programs contain no `discard` (keeps early-Z) — and bgfx rejects
   programs whose VS outputs don't exactly match the FS inputs, which is
   why the clip variants need their own VS (`v_wpos` varying) and the
   shader bodies are shared via `fc_{mesh,flat}_{vs,fs}.sh` includes.
   *Section caps/fill done (2026-07, Phase 2 — see there).*
   Verified GL/bgfx pixel-identical geometry extents for 1-plane and
   2-plane (intersection) cuts, and (2026-07) for a 2-plane
   `SectionConcave` union cut (two `SoClipPlane` nodes, à la
   Std_Clipping). *Textures done (2026-07)*: the unit-0 `SoTexture2` of
   triangle materials is translated (`Render::TextureImage` — pixels
   copied, keyed by the Coin node id — wrap/model/blendColor plus the
   merged texture matrix) together with the vertex cache's texcoords
   (second vertex stream, built on first textured use), and rendered by
   `vs/fs_fc_mesh_tex[_clip]` / `fs_fc_mesh_oit_tex[_clip]` program
   variants replaying the GL fixed-function texture environment
   (modulate/decal/blend/replace) on the lit color; GPU textures are
   cached per texture id and evicted with the meshes. Verified vs GL:
   modulate within the fill class, transform variant exact up to
   minification speckle (~~both sides non-mip linear~~ *bgfx mips done
   2026-07*: full CPU box-filter chain + trilinear — under strong
   minification bgfx now shows the correct filtered average where the
   non-mipped GL path aliases into stripes), transparent RGBA
   exact under the sorted fallback (`FC_BGFX_DEBUG_NO_OIT`) — WBOIT
   widens its approximation class on high-contrast layered texels.
   Deviations: GL also textures lines/points (visible with REPLACE on
   styled edges; the default modulate on dark edges hides it), further
   texture units are ignored, non-GL bgfx backends may see images
   v-flipped. *Autozoom done (2026-07)*: `Material::autozoom` mirrors
   the cache's SoAutoZoomTranslation list; the Gui side feeds the exact
   Coin world-to-screen scale per frame
   (`Renderer::setAutoZoomScale`, one frame late like the rest), and
   the backend rebuilds those draws' model matrices every frame by
   replaying GL's `setupMatrix` (scale substitution recovers rotation by
   row normalization — no shear). App::Placement axis crosses verified
   GL-identical (2 px front view; 49 px rotated, all half-pixel diagonal
   line stepping). Still missing:
   per-vertex-transparent caches go wholesale to the transparent bucket.
   ~~**Known issue — transparent scene geometry is invisible**~~ *Fixed
   (2026-07) by background compositing*: the backend now draws the window
   background itself (`Render::Background` fed from the viewer,
   `BGFXView::submitBackground` replicates `SoFCBackgroundGradient`'s
   linear/radial tessellation as a clip-space quad in a dedicated first
   bgfx view), so transparent draws blend against the real background;
   `View3DInventorViewer::renderScene` suppresses Coin's gradient node for
   backend-rendered frames (`SoFCBackgroundGradient::setSuppressed`, a
   plain flag so toggling doesn't trigger notification) — it used to
   repaint every far-plane pixel, erasing transparent geometry which
   writes no depth. Verified: linear and radial background pixels match
   GL exactly; transparent-over-background blends. ~~Remaining
   transparency parity gap: GL renders noticeably brighter transparent
   fills.~~ *Resolved (2026-07)* — two causes: GL forces two-sided
   lighting for transparent/on-top draws and disables culling for
   transparent draws (`applyMaterial` ~745), which the backend now
   replicates (the headlight shader used to render transparent back
   faces nearly black); and WBOIT (Phase 2, done) blends the layers.
4. **Pass skeleton**: bgfx view sequence reproducing today's ordering.
   *First cut done (2026-07):* 5 views — background (clear + gradient
   quad) → opaque → transparent (bbox-center depth sort via
   `ViewMode::DepthDescending`) → on-top → selection/highlight. *Grown (2026-07)*: depth-write-only prepass of on-top fills
   and the hidden(dimmed)/solid two-pass for on-top lines/points, matching
   the GL delayed-pass order (see item 5). Still to grow: outline,
   section, line pattern, plus the depth+normal prepass slot for
   AO/outline.
   **Compositing gotcha (fixed)**: the backend blits color+depth *before*
   the Coin pass, and `View3DInventorViewer::renderScene()` used to call
   `drawSingleBackground()` afterwards with depth test off — wiping the
   backend's color and leaving flat-colored silhouettes (the gradient
   background node is depth-tested at the far plane and only fills empty
   pixels). The flat background fill is now skipped when the backend
   rendered the frame.
5. **Selection/highlight/picking**: reuse the existing
   `buildHighlightCache` products (they arrive as `VertexCacheMap` too);
   picking stays on the Coin side initially (manager `doLatePick`), GPU
   ID-buffer picking later.
   *Selection/highlight feeds work (2026-07)* — translated like the scene
   and drawn in the highlight view. Whole-object-on-top selection double
   draws (the GL renderer's cache-key skip logic is not replicated yet).
   *Partial (per-face/edge) selection + preselect render (2026-07)*: the
   on-top/highlight bgfx views use `ViewMode::Sequential` and partial
   triangle draws are submitted after whole fills and lines, mimicking
   the GL pass order (whole transparent fill → on-top lines →
   `transpselectionsfaceontop`); selected-face color matches GL closely.
   ~~Known deviation: GL draws the *preselected* face as outline only
   while bgfx fills it.~~ *Closed (2026-07)* by the Phase 2 face
   outline (see below). ~~Highlight lines are not thickened~~ *Done (2026-07, see
   Phase 1)*. `FC_BGFX_DEBUG_FEED=1` dumps the translated
   selection/highlight draw calls, `FC_BGFX_DEBUG_SUBMIT=1` the per-draw
   view/pass/state words.
   *On-top semantics (2026-07)*: GL parity per `applyMaterial` ~520 —
   on-top draws render with depth test off (which also disables depth
   writes), non-on-top transparent draws drop the depth write. The
   delayed-pass ordering is reproduced: scene on-top fills → selection
   whole fills → whole-on-top preselect fills → depth-write-only prepass
   of on-top fills → on-top lines/points twice (hidden pass: no depth
   test, alpha dimmed to `TransparencyOnTop` via `u_params.w` in the flat
   shader; solid pass: LEQUAL, no depth write) → single-part selection
   fills → preselect. Whole-object on-top selection/highlight draws now
   *hide* the object's normal scene draws (GL's selectionkeys/
   highlightkeys skip): the bridge exposes `DrawCall::objectKey` (content
   hash of the `SoFCSelectionRoot::NodeKey` path) + `wholeObject`, and
   the backend skips matching scene draws. *Per-selection dedup done
   (2026-07)*: the same object selected through several ids (e.g. two
   element selections both carrying the object's whole-object on-top
   draws) draws once, replicating GL's `renderkeys` skip on
   (objectKey, cacheId, primitive type). ~~Not replicated: selection
   line pattern (`SelectionLinePattern`).~~ *Done (2026-07, see Phase 1
   line patterns).*

Exit criteria: a real model renders in bgfx visually close to today's mode-3
output (shaded + edges + selection/highlight + clip planes without caps), GL
path bit-identical when renderer off.

### Phase 1 — CAD parity (4–5 wks)

Line pattern/width shaders (no `glLineStipple`/`GL_LINE` in modern APIs —
screen-space quad-expanded lines), point sprites, per-face color, polygon
offset semantics, hidden-line draw style, on-top/annotation ordering, draw
styles. This is grind, but semantics are all encoded in
`SoFCRenderer::applyMaterial` + the pass loop.

*Line width done (2026-07)*: lines wider than 1px render as instanced
screen-space quads — each cache uploads a per-segment instance buffer
(endpoints + endpoint colors), `vs_fc_line`(`_clip`) expands a shared
unit quad to the requested pixel width (`u_viewRect`, near-plane clamp)
and pairs with the flat fragment shaders; partial per-edge draws map
their index range onto an instance range; 1px fallback without
instancing caps. The bridge applies GL's selection thickening
(`SelectionLineThicken`/`MaxWidth`, `SelectionPointScale`/`MaxSize`,
with `applyMaterial`'s `RenderPassHighlight` routing rules) at
translate time, so `RendererBridge::translate` now takes the feed
context (selection id / highlight). Verified: selection lines and scene
edges pixel-match GL widths and positions, clipped scenes match
exactly. Still open here: the GL quirk that the dimmed pass of
whole-on-top preselect lines stays thin (bgfx thickens both passes).

*Line patterns done (2026-07)*: glLineStipple semantics on the quad
path — `vs_fc_line_pat`(`_clip`) outputs the screen-space pixel
distance along the segment (times clip w; the fragment shader divides
it back to undo perspective correction), `fs_fc_line_pat`(`_clip`)
discards unset pattern bits with float-only math (bgfx's GL backend
may emit version-less GLSL where integer bit ops don't exist). The
bridge translates `Material::linepattern` (`factor << 16 | pattern`)
plus a separate hidden-pass pattern implementing GL's
`SelectionLinePattern` substitution (applies to the dimmed pass of
on-top lines only when the material has no pattern of its own).
Patterned lines of any width use the instanced quad path; no
instancing → solid 1px fallback. Verified pixel-identical dash runs
vs GL for `DrawStyle=Dashed` scene edges and `SelectionLinePattern=
0xff00` hidden selection edges.

*Point sprites done (2026-07)*: points > 1px render as instanced
screen-space quads (`vs_fc_point`(`_clip`)` + flat fragment shaders,
sharing the line unit-quad geometry and a per-point instance buffer),
replicating glPointSize's screen-aligned square — the portable
replacement for `BGFX_STATE_POINT_SIZE`, which only exists on bgfx's
OpenGL backend. Partial (per-vertex) draws map index ranges 1:1 onto
instance ranges. Verified identical vertex dot extents vs GL at
PointSize=7.

*Hidden-line draw style done (2026-07)*: the per-frame
`HiddenLineConfig` is resolved from the traversal state in
`SoFCRenderer::render` and fed through a new
`Renderer::setHiddenLineConfig`; the bridge translates
`Material::outline`/`linecolor` plus, on demand, a materialized
seam-free line index set (the cache's `noseamindexer` only *filters
parts at draw time*, so `SoFCVertexCache::getNoSeamLineIndices` builds
the actual index list) and per-face-part triangle ranges for clipped
outlines. Backend: hideFace skips fills (and their outlines, like GL),
hideSeam switches whole-cache line draws to lazily-built seam-free
GPU buffers, hideVertex skips point draws and enables outline corner
caps; each outline-material triangle draw gets the GL stencil outline
(generalized `submitOutline`, now fed from persistent per-mesh
triangle-edge/corner instance buffers — instance i maps 1:1 onto
triangle index position i, so face-outline partial ranges share them —
instead of per-frame transients) in a new Sequential `ViewOutline`
between the opaque and transparent buckets. Outline edges/caps of
whole-cache outlines write depth: that stands in for GL's back-to-front
entry order, keeping nearer outlines crisp while nearer transparent
fills still dim hidden outlines; line/point quads gained an NDC depth
bias in `u_params.z` so the owning polygon-offset fill keeps blending
over its own outline. Line quad widths and point sizes now round to
the nearest integer like GL's non-AA rasterizer (a 1.5px HL edge
covers 2 rows in GL). Verified pixel-identical to GL (zero diff) for
the default hidden-line style (transparent fills + outline + hidden
seam/vertex), hideFace, and the selection-outline regression.
Known deviations, per-face-part hidden-line outlines only (clipped or
`perFaceOutline`): GL's `glPolygonMode(GL_LINE)` also rasterizes the
*post-clipping* polygon boundary, i.e. draws the section-cut outline,
which fragment-discard clipping cannot produce (revisit with Phase 2
stencil section caps); and per-part outlines skip the depth write, so
fills of farther objects dim them where GL's ordered draw kept them
crisp (writing depth was retried for the unclipped per-face variant
and still came out worse — the self-fill z-failed like in the clipped
case).

*Hidden-line outline variants done (2026-07)*: the `sceneOutline`
mode renders GL's `renderSceneOutline` — every scene triangle draw
stencil-marks under one shared reference (depth-independent, matching
GL's replace-on-depth-fail), then each one's edges/caps redraw where
the stencil differs, leaving a single silhouette around the union of
the scene; it runs at GL's position (after all line/highlight passes,
before the face outlines) at the head of the highlight view, edges
1.5x the configured outline width, caps unscaled (`OutlineSpec::
capWidth`), colored by the resolved hidden-line color, and suppresses
the per-entry outlines unless `perFaceOutline` is also set. The
`perFaceOutline` mode outlines each face part separately —
`MeshData::triangleParts` is now filled for every outline mesh and a
new `nonFlatParts` subset carries the curved faces, which are the
only ones outlined when combined with `sceneOutline` or a zero
outline width (GL: `getNonFlatParts()`); an outline-less part table
now also outlines nothing under clip planes, like GL's
zero-iteration loop. Whole-object selection draws get their
hidden-line outline too (GL: renderOutline from the slentries
passes), routed into the highlight view for on-top selections; and
the whole-object *preselection highlight* outline renders on top with
the bridge-resolved selection-thickened width
(`Material::outlinewidth`, now computed independently of the
face-outline params) raised by the configured outline width.
`submitOutline` split into `submitOutlineMark`/`submitOutlineEdges`
to share the passes with the silhouette. Verified pixel-identical to
GL (zero diff): sceneOutline, perFaceOutline (non-flat variant),
sceneOutline+perFaceOutline, and the default-HL regression; the
whole-object selection outline matches within the fill-shading
tolerance (max 16/255), the preselect outline leaves 4 corner-cap
pixels above threshold. Deviations: perFaceOutline with an outline
width dims per-part outlines (the depth-write class above); the
silhouette edge passes apply each entry's own clip planes where GL
reuses whatever clip state its mark loop left behind. Note: the
two-object per-face-selection HL scene shows a pre-existing ~2k px
ordering deviation (green face fill vs on-top black lines) that
predates this work — verified bit-identical before/after.

### Phase 2 — visual features (11–15 wks)

| Feature | Est. | Notes |
|---|---|---|
| WBOIT | 1.5–2 | *Done (2026-07), first cut.* RGBA16F accum + R16F revealage MRT sharing the scene depth (test only), weight = McGuire eq. 10, independent per-target blending, fullscreen composite view (`vs/fs_fc_comp`) resolving INV_SRC_ALPHA/SRC_ALPHA onto the scene FBO. Active where independent blend + half-float FB formats exist (WebGL2-compatible set); falls back to the bbox-sorted alpha blend otherwise or when a frame has no transparent scene triangles. Verified: transparent brightness within the general fill-shading tolerance of GL (~-8/255 vs -7 on opaque fills). *MSAA resolve chain done (2026-07)*: the accum/reveal targets carry the scene's sample count and are created without `BGFX_TEXTURE_RT_WRITE_ONLY`, so bgfx pairs each with a single-sample resolve texture and blit-resolves automatically when the transparent view's framebuffer is switched away — the composite pass then samples the resolved images (formats gated on `BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA` under MSAA). Resolve-then-composite averages accum/revealage per pixel before the WBOIT formula (the standard approximation; transparent silhouettes get coverage-weighted edges rather than per-sample compositing). Fixed on the way: the scene depth target was created with `flags & ~BGFX_TEXTURE_RT`, but the MSAA levels are an *enum* in that flag nibble — under MSAA4x this silently degraded the depth renderbuffer to 2 samples, desyncing it from the color attachment. Verified vs GL at MSAA4x (threshold 30, all within the fill/AA-edge tolerance of the same scenes): transparent WBOIT 1039 px, opaque 669 px, clip+transparent caps 408 px; non-MSAA transparent regression 324 px. |
| SSAO (ASSAO) | 1.5–2 | needs depth+normal prepass from Phase 0. *First cut done (2026-07), hemisphere-kernel pixel-shader SSAO (not yet the ASSAO port — bgfx ex.39 is compute-only, which WebGL2 lacks; this cut is the WebGL2-portable baseline and builds the prepass infrastructure ASSAO/GTAO need).* Three new views before the opaque pass render a depth+normal prepass of the opaque scene triangles (own non-MSAA RGBA16F target + D24S8 depth: octahedral-encoded viewer-facing view normal + linear view depth, fp16 — models beyond ~65k units will wrap) and resolve AO from it (16-sample rotated-hemisphere kernel with 4x4 tiled noise, range-checked with smoothstep falloff, 4x4 box blur), and a fourth view multiplies the blurred AO onto the scene color between the section caps and the outline/transparent passes — transparent geometry neither receives nor casts AO, and the on-top/highlight buckets stay clean. Position reconstruction and the prepass front-facing flip handle orthographic and perspective projections separately (u_proj[2][3] decides; the naive dot(n, viewpos) facing test mis-flips near-eye-plane faces under ortho — found as a black cylinder cap in testing). Configured per frame via `Render::AOConfig` from new ViewParams (`RendererSSAO`, `RendererSSAORadius` — 0 = auto, 5% of the scene bbox diagonal — `RendererSSAOIntensity`); disabled in hidden-line mode and gated on renderable RGBA16F+R8 (the WebGL2 float-buffer set, like WBOIT). Verified on llvmpipe: SSAO-off bit-compatible with GL (0 px > 30); SSAO-on front view of unoccluded geometry stays within the same tolerance; iso/perspective/clip/MSAA4x/WBOIT scenes show correct contact darkening at a box-slot/cylinder junction only. Known gaps: caps and on-top draws are not prepass sources; AO under a clip plane comes from the clipped interior back faces; no mip/half-res path yet (full-res generate). |
| PBR + IBL | 3–4 | BRDF + env prefilter pipeline; matcap fallback; material property plumbing from ViewProvider. *First cut done (2026-07).* Uniform-selected branch of the shared mesh fragment shader (all 8 mesh programs, no new variants): metallic/roughness GGX with a white headlight (L=V; the direct specular term is clamped — with a headlight every facing plane sits exactly on the GGX peak 1/πa², which would flash whole faces white at low roughness) plus IBL from a fixed procedural Z-up studio environment (ground/horizon/sky gradient + 3 light lobes; the ground stays fairly bright — metals reflect the lower hemisphere over most side faces) built once per view on the CPU, deterministic: GGX-prefiltered RGBA16F cubemap mip chain (64px, N=V=R Hammersley importance sampling, shader lod = roughness×5) with Lazarov's analytic env BRDF (no LUT texture) for specular, cosine-convolved SH9 irradiance (Ramamoorthi polynomial, basis/cosine/1/π constants folded into 9 vec4 uniforms) for diffuse — all fragment-shader-only, WebGL2-safe. Per-frame `Render::PBRConfig` from new ViewParams (`RendererPBR`, `RendererPBRMetallic`, `RendererPBRRoughness` — 0 = derive per draw from the material shininess via √(2/(exp+2)) — `RendererPBREnvIntensity`); baseColor = material diffuse / per-vertex color; disabled in hidden-line mode like SSAO; gated on cube-samplable RGBA16F; a dummy black cube keeps the always-present sampler valid when off. Verified on llvmpipe: PBR-off bit-compatible with GL (0 px > 30); dielectric/metal/WBOIT-transparent/SSAO-combined renders show the expected environment response (sky-tinted tops, floor reflections on metal sides, contact AO composes). Known gaps: per-object material plumbing from ViewProvider not started (global metallic/roughness only — planned, Phase 2b item 3); no matcap fallback; environment is fixed (no user HDRI — Phase 2b item 2); textures still apply through the fixed-function texenv replay on top of the PBR result (no albedo-map semantics); no specular occlusion from SSAO. |
| Bump/normal/parallax mapping | 1–1.5 | with (or right after) PBR, sharing its texture plumbing: tangent-space normal maps plus classic grayscale bump (height-to-normal in the sampler), and parallax-occlusion mapping (bgfx ex.33) for strong relief — all fragment-shader-only, WebGL2-safe. The tangent frame comes from screen-space derivatives (the cotangent-frame/surface-gradient trick: ddx/ddy of position and UV), NOT from authored vertex tangents — OCCT tessellation has no canonical UVs, Coin texcoords may be texgen'd, and the vertex caches carry no tangent attribute; the derivative frame works for any UV source and needs no cache format change. Reuses/extends the existing unit-0 texture bridge with additional material texture slots (normal/height), and gives the GL Shadow ground's `ShadowGroundBumpMap` a bgfx equivalent. True *displacement* is deferred to Phase 3. *First cut done (2026-07).* Unit-0 SoBumpMap captured by a dedicated cache-manager post callback (SoBumpMap::callback() is a no-op — the element only exists in GL rendering) into a new `Material::bumpmaps` map, kept out of `textures` (the GL renderer GLRenders those with their unit set); bridged as `Render::Material::bumpmap` (1/2-component = grayscale height, 3/4 = tangent-space normal map). In the backend the bump map rides the textured mesh programs (a white 1x1 stands in at unit 0 when there is no color texture — no new program variants; bump sampler at unit 2, new v_vpos varying in the tex family), derivative cotangent frame as planned, central-difference height-to-normal, 16-step POM with linear refinement that also offsets the color texture lookup; the perturbed normal feeds both the Blinn-Phong and PBR paths. New ViewParams `RendererBumpScale`, `RendererParallax` → per-frame `Render::BumpConfig`. Verified on llvmpipe: bump-off 0 px vs GL; height/normal relief correct on box+cylinder, with/without POM, composes with PBR; textured scene stays in the minification-speckle class. Known limits: needs mesh texcoords (only an enabled texture unit provides them — pair bump-only scenes with a plain white texture); SoBumpMapCoordinate/SoBumpMapMatrixElement unhandled; ~~`ShadowGroundBumpMap` waits for the Shadows row~~ *done (2026-07, see the Shadows row)*; SSAO prepass keeps geometric normals. |
| Shadows (VSM) | 2–3 | cached single-light variance shadow map, semantics ported from the GL Shadow draw style (Coin `SoShadowGroup`): directional/spot per `ShadowSpotLight`, ground-plane receiver with texture/bump (`ShadowShowGround`\*), per-object casting flags; depth-from-light pass reuses the prepass shader family; re-render only on scene/light change (`ShadowSync`); EVSM variant where RG32F is renderable to curb light bleeding. *First cut done (2026-07), directional light.* Key integration fact: the Shadow style's `SoShadowGroup` (light + ground) sits *above* the render-cache traversal root, so neither reaches the material feed — the light resolves per frame from the state's `SoLightElement` (`Render::LightConfig`; headlight filtered by node type; **the element's matrices map to view-reference coordinates**, the inverse viewing matrix multiplies back in), and the backend draws its own ground receiver quad (ShadowShowGround/Scale/Color). `Material::shadowstyle` (SoShadowStyleElement bitmask, captured by the existing manager callback) routes casters/receivers. New ViewShadow pass: 1024² RG16F(+D24S8; RG32F fallback) moments from `gl_FragCoord.z` under an ortho light camera fit to the scene bbox; position-only caster shaders + clip variant. Receivers sample via a view-space→shadow-uv matrix (crop per `homogeneousDepth`/`originBottomLeft`; new `v_vpos` varying in *all* mesh variants) with Chebyshev + light-bleed linstep; outside-map = lit. The light replaces the headlight in both lighting models (Blinn-Phong keeps the 0.2/diffuse split; PBR runs full GGX + Schlick Fresnel), and unlit receivers are force-lit like Coin's ground shaders. Debug: `FC_BGFX_DEBUG_SHADOW` (state + CPU matrix cross-check), `FC_BGFX_DEBUG_SHADOW_VIS` (moments/receiver-depth as color). Verified on llvmpipe: correct ground + self shadows (default light), composes with PBR+IBL; shadow-off 0 px vs GL. *VSM blur done (2026-07)*: `Shadow_SmoothBorder` (per-view property, ViewParams fallback, 0..100) drives a separable gaussian over the moments — two views right after the caster pass blur into a ping texture and back into the moments texture through a second color-only framebuffer, so receivers and the volumetric raymarch keep their sampling unchanged (9-tap kernel as 5 linear fetches, 100 = 4-texel base step). *Cached map done (2026-07)*: an FNV-1a hash of the light camera + smoothing + caster set (cache ids, transforms, ranges, clip planes, autozoom scale, selection-hidden keys) skips the caster pass and blur while unchanged — camera moves never re-render, scene/light edits do (hidden-line mode disables the cache; hash resets with the resources on resize; `FC_BGFX_SHADOW_NOCACHE` forces every-frame for A/B). Verified: static scenes match every-frame renders (1-2 px AA), a mid-session caster move re-renders bit-identical (0 px) to the uncached run. *Spot light done (2026-07)*: perspective light camera at the SoSpotLight position (fov from cutOffAngle, depth range fit to the scene bounding sphere), per-fragment light vector with GL-convention cone falloff (cutoff + pow(cd, dropOffRate·128)) folded into the shadow factor of both lighting models and the volumetric raymarch; new `u_lightPos` uniform (w = cos cutoff, -1 = directional), perspective divide added to the shadow projection everywhere (no-op for ortho). Verified vs Coin's SoShadowGroup: cone pool/arc geometry matches; directional regression 1 px. *EVSM done (2026-07)*: the moments store exp(c z), exp(c z)² — RG32F preferred (c = 42; RG16F fallback c = 5, half-float range bound), palette clear for the warped far plane, receivers/raymarch warp the biased depth, relative variance floor; sharp shadows match VSM, ShadowSmoothBorder penumbras tighten where VSM bled. WebGL2 float-linear caveat on RG32F noted for the WASM build. *Gain parity done (2026-07)*: the backend used to replace the viewer headlight with the scene light — but Coin's SoShadowGroup keeps non-shadow lights, so GL rendered headlight + shadow light. Both lighting models now add the shadowed scene light on top of the unshadowed headlight; tone matches Coin (mean 3/255, remaining deltas are shadow-edge smoothing). *Ground texture done (2026-07)*: `Shadow_GroundTexture` (+`GroundTextureSize` world tiling) decoded by Qt into `LightConfig::groundTexture` and rendered by the textured mesh program on the ground quad, modulated by the ground color — checker tiling/phase and received shadows match Coin. *Ground bump map + transparency done (2026-07)*: `Shadow_GroundTransparency` alpha-blends the quad over the background (depth still written; 1.0 skips the draw — Coin's shadow-only ground variant is not ported) and `Shadow_GroundBumpMap` rides the textured ground program with the tiled ground UVs — height relief with parallax-occlusion or normal-map shading, sharing the scene bump scale/parallax settings. On the way this fixed grayscale bump decoding everywhere (ground map and `Render_NormalMap`): Qt-decoded gray images were expanded to RGB, which the shader reads as a degenerate ±(T+B) normal map that two-sided lighting folds flat — grayscale bump sources now stay one component. *Transparent casting done (2026-07)*: transparent draws (material and per-vertex alike) cast like opaque ones — Coin's depth-map pass ignores alpha — with water still excluded; the transparent bucket also receives through the shared mesh shader. *Default penumbra parity done (2026-07)*: at SmoothBorder 0 the map stores plain (z, z²) moments and receivers/ground/raymarch run Coin's exact VsmLookup (Shadow_Epsilon adds to the variance, Shadow_Threshold smoothsteps the tail) — the soft distance-growing default penumbra now matches GL (edge-AA class + a faint profile ring remain); the EVSM warp and its tighter penumbra only engage with the SmoothBorder blur. Map size 2048² (Coin's default; the ShadowPrecision size parameter is still unplumbed). *On-top casting done (2026-07)*: selection-hidden scene draws and on-top scene draws stay in the caster pass — a selected object keeps its shadow like GL, and the cached map survives select/deselect (the caster set no longer depends on the selection). *ShadowPrecision done (2026-07)*: the map targets are recreated per the Precision property with Coin's sizing (next power of two of precision × the 2048 cap). *Spread kernel done (2026-07)*: SpreadSize/SpreadSampleSize (the raw properties the viewer packs into Coin's smoothBorder field) ride `Render::LightConfig` into the free `u_evsm.zw` — tap spacing in map uv (Coin swidth·0.001; the 5e-4 scale, the 10000 wrap of the packed decode, the spot 0.1× and the 256/scene-extent spot shrink all replicated) and kernel mode; the shared mesh shader's moments lookup refactored into an `fc_shadowTap()` helper (now with Coin's far-plane `map.x < 0.9999` early-out) looping either the pixel-parity-dithered 4-tap kernel (sample 0) or Coin's integer-centered N×N grid (N = min(2·sample+1, 8)), offsets scaled by the homogeneous w like `shadowCoord.w`; the ground receives through the same programs, the volumetric raymarch stays single-tap (no Coin counterpart). Verified vs GL: dither pattern and grid softening match in kind and magnitude (same-renderer deltas 8.7k/5.9k px vs GL's 7.0k/7.6k, threshold 30), spread-off control unchanged. **Coin ground composite fixed on the way (2026-07)**: with a backend active, Coin's `SoShadowGroup` ground quad still GL-rendered *on top* of the backend output (the group sits above the render-cache root) — z-fighting bands wherever Coin's differently-blurred ground shadow won the depth test, most visibly a dark border band along the map window once SmoothBorder engaged Coin's own blur (looked like backend light bleed; it was Coin's). The viewer now switches the Coin ground off while a renderer is selected (re-evaluated on type change), the backend quad takes over Coin's sizing (GroundSizeScale × largest scene dimension, z included — was half that, x/y only) and `Renderer::boundBox()` reports the quad so camera auto-clipping covers it. Every earlier shadow-ground screenshot compared bgfx-with-Coin-ground-on-top; the honest baseline is the penumbra-ring class (41.6k px > 30 in the steep-light axo scene, ground-edge lines + edge AA included; plain GL path untouched, 1 px). |
| Volumetric light shafts | 1–1.5 | after shadows: half-res per-pixel raymarch of the shadow map (~32-48 dithered steps) + bilateral upsample, composited before the transparent bucket; intensity/density params; skip when shadows are off. Extension (+~1): **water as a bounded medium** — per-ray entry/exit bounds (water plane, or front/back depth targets of a closed water body) and per-channel Beer–Lambert extinction (also attenuating surfaces seen through the water) cover underwater shafts and tinted harbor/tank/pool views with the same raymarch core; still fragment-shader-only. Above-water refraction and caustics are *not* volumetric and are deferred to Phase 3. *First cut done (2026-07), shafts (the water extension remains).* Two new views: `ViewVolGen` raymarches at half resolution (`fs_fc_volume`, 32 steps with an interleaved-gradient-noise start offset; the scene view/projection stays bound so the predefined `u_proj` reconstructs the ray like the SSAO pass, ortho + perspective) accumulating inscatter where the VSM says the light reaches (same Chebyshev/linstep as the mesh receivers), and `ViewVolApply` (between the outlines and the transparent bucket) bilateral-upsamples — 4-tap, bilinear weights modulated by surface-ray-length similarity, the half-res target's alpha carries that length — and composites with premultiplied-alpha blending: the output alpha is the Beer–Lambert extinction over the in-medium path, so dst = inscatter + transmittance·scene. **The medium is bounded** — a sphere fit around the scene bbox (0.75 × diag) with per-ray entry/exit like the planned water medium — because with an unbounded medium the camera's stand-off distance alone (fitAll ≈ 2-3 × model size) puts the whole model behind several optical depths of fog. Ray ends come from the SSAO prepass, which now rasterizes when either SSAO or volumetrics need it (`prepassActive`) and then also includes the shadow ground quad so shafts terminate on it (ground stays out of the prepass under SSAO alone — preserved behavior). New `RenderParams` `Volumetric`/`VolumetricIntensity`/`VolumetricDensity` (0 = auto: unit optical depth over the medium radius) → per-view `Render_*` properties → per-frame `Render::VolumetricConfig`; requires the shadow + SSAO resource sets, disabled in hidden-line mode. Debug: `FC_BGFX_DEBUG_VOL`. Verified on llvmpipe: vol-off no-shadow scene stays at the 1px edge-AA baseline vs GL; shafts/haze correct in ortho + perspective, compose with SSAO, PBR-less shadows, MSAA4x + WBOIT transparency. *Water extension done (2026-07).* **A special material turns any shaped object into water**: a `Render_Water` ViewProvider dynamic property (per-object, like `Render_Metallic`) sets a new `SoFCRenderMaterial::water` field, captured into the cache material and bridged as `Render::Material::water`. Two new prepass-family views rasterize the water draws' nearest front-face / farthest back-face linear depths (`ViewWaterFront`/`ViewWaterBack`, the back view clears depth to 0 and tests GREATER; culling forced by face side) — the per-ray entry/exit bounds; no front face + a back face = underwater camera. The raymarch carries per-channel transmittance and swaps the air density for the water sigma inside the interval (absorption = density × complement of the diffuse color + 0.35 × density wavelength-independent scattering; `Render_WaterDensity`, 0 = auto: 3/body-diagonal), so deep shafts tint toward the water color. The composite splits into an analytic per-channel extinction multiply (`fs_fc_volume_ext`, ZERO/SRC_COLOR) followed by the additive inscatter (the apply view turned sequential) — surfaces seen through deep water darken toward the water color; the split reproduces the previous single-pass shafts result bit-near (1 px > 30). Water draws neither end volumetric rays (excluded from the prepass) nor cast shadows, so light enters the water; their surface still renders normally — set object transparency to see inside. Verified: tinted tank scene (submerged box/cylinder/ground tint progressively with depth, above-water parts clear). Known gaps: one shared water appearance per frame (first flagged draw wins — differing multiple bodies not supported); a single [entry, exit] interval per pixel (non-convex bodies over-estimate); no light-path attenuation inside water (light arrives untinted — caustics/refraction are Phase 3); opaque water surfaces occlude their own interior (use transparency). |
| Section caps | 2–3 | stencil capping + hatch, port `_renderSection` semantics. *Done (2026-07).* Two new sequential bgfx views (opaque caps between the opaque and outline passes, transparent caps after the OIT composite — GL's grouped-pass order). Per section plane: depth-independent stencil INVERT parity mark of the solid triangle ranges (`renderSolids` ported via new `SoFCVertexCache::getSolidPartRange` → `MeshData::solidParts`/`hasSolid`, plus `Material::solidshape` from the shape hints) clipped by that plane alone; then a world-space cap quad (`vs/fs_fc_cap(_clip)`, hatch texture modulate, depth LESS + write so the fill keeps the rim like GL's cap-before-fill order, clipped by the remaining planes, unclipped in concave mode) where the parity is odd; then a stencil-cleanup quad standing in for GL's per-pass stencil clear (the cap views also stencil-clear at view start — the outline passes leave marks behind). The bridge feeds a per-frame `Render::SectionConfig` (fill/invert/group/concave/hatch ViewParams) and the hatch image (`Renderer::setHatchImage`, forwarded from `SoFCRenderer` including on late attach); the fill-invert color transform, Coin's z→normal rotation (quad/hatch orientation), and the mid-depth world-to-pixel hatch scale are ported. Verified vs GL (threshold 30; deviations at or below the no-clip fill/edge baseline of the same scene): 1-plane, 2-plane intersection, 2-plane concave union, hatch off, invert off, transparent solids (WBOIT active), SectionFillGroup, and hidden-line+clip (caps match; the missing section-cut *outline* of clipped per-part HL outlines remains — the Phase 1 deviation, not closed by caps). Known deviations: cap sources are scene + whole-object selection draws only (GL also sections on-top buckets when `NoSectionOnTop` is off); transparent caps always follow GL's *grouped* order (after the whole transparent bucket); the grouping key ignores autozoom. |
| Outline/hidden-line | 1.5–2 | screen-space depth/normal pass + existing edge geometry. *Selection/preselection face outline done (2026-07)*: ported the GL stencil technique — stencil-mark the face, redraw its triangle edges as instanced thick lines + point-sprite corner caps where the stencil differs (the portable stand-in for `glPolygonMode`); per-outline stencil refs avoid per-part clears; the bridge resolves the Show*/No*WithOutline params and outline width. Verified pixel-identical to GL for preselect (outline-only) and two-face selection. *Whole-scene + hidden-line outline variants done (2026-07, see Phase 1).* |

### Phase 2b — material & settings plumbing, glTF interchange (6–8 wks)

The Phase 2 features are all driven by *global* `ViewParams` today. This
phase gives them a proper settings model (global → per-view → per-object)
and connects it to a standard interchange format. Sequencing: item 1 first
(everything else reads its defaults through it); items 2 and 3 are
independent of each other; item 4 builds on item 3's property model.

1. **`RenderParams` split (0.5 wk)** — move the renderer parameters out of
   `ViewParams` into a new generated parameter class `Gui::RenderParams`
   (`src/Gui/RenderParams.py`, cog + `Tools/params_utils.py`, same pattern
   as `ViewParams.py`), parameter path
   `User parameter:BaseApp/Preferences/View/Render` — a child group of the
   View path. Movers: the ten `Renderer*` params (`RendererSSAO`\*,
   `RendererPBR`\*, `RendererBumpScale`, `RendererParallax`) plus the raw
   `RendererType` string currently read straight off the View group by
   `View3DSettings` (View3DSettings.cpp:346) and the 3D-view prefs page.
   Drop the now-redundant `Renderer` name prefix inside the new group
   (`SSAO`, `PBRMetallic`, `Type`, …) with a one-time migration that copies
   any existing old-key values from the View group. Wiring notes:
   `View3DSettings::OnChange` only observes the View group handle — it must
   also attach to the Render child group (or `RenderParams`'s own
   `UserOnChange` hook triggers the redraw, mirroring
   `ViewParams::onViewParamChanged`); the draw-styles prefs page
   (`DlgSettingsDrawStyles`) and 3D-view page rewire to the new getters.
   All *future* renderer settings (shadows, env/HDRI, volumetrics …) land
   here, not in `ViewParams`.

2. **Per-view render settings, Shadow-style (1–1.5 wks)** — expose the
   engine settings as `Render_*` dynamic properties on the `View3DInventor`
   view object, exactly like the Shadow draw style's 31 `Shadow_*`
   properties: a `_renderParam` helper mirroring `_shadowParam`
   (View3DInventorViewer.cpp:356) materializes each property in property
   group "Render", default-initialized from `RenderParams`, so users can
   override per view/document what the preferences set globally.
   *Done (2026-07), first cut.* The properties materialize when a renderer
   backend is selected (`View3DInventorViewer::initRenderProperties`, from
   `setRendererType`), NOT lazily from the render loop — the per-frame
   config feed must not mutate the view. The view object rides the
   existing external-renderer plumbing (`setExternalRenderer(renderer,
   view)` through `SoFCUnifiedSelection` → cache manager → `SoFCRenderer`),
   and the bridge's config translators (`translateAOConfig` /
   `translatePBRConfig` / `translateBumpConfig` / `translateLightConfig`)
   take the view and do a read-only property lookup with `RenderParams`
   fallback. `onViewPropertyChanged` reacts to the `Render_` prefix with a
   redraw (configs are re-read every frame). Covered: SSAO
   (enable/radius/intensity), PBR (enable/metallic/roughness with 0-1
   constraints, env intensity), bump (scale/parallax); the bgfx shadow
   ground now honors the *existing* `Shadow_ShowGround` /
   `Shadow_GroundSizeScale` / `Shadow_GroundColor` view properties —
   closing the ground part of the "per-document Shadow_* overrides
   ignored" gap in the Phase 2 Shadows row (light direction/color/
   intensity were already per-view through the Coin light node).
   Unlike Shadow these settings are not tied to one draw style: they apply
   whenever the backend renders (render cache 3 + renderer type selected).
   Verified on llvmpipe: per-view `Render_PBR`/`Render_PBRMetallic` render
   metallic with the global param off; per-view `Shadow_GroundColor`
   recolors the bgfx shadow ground; control run bit-identical to
   pre-change bgfx output. Remaining: HDRI env file property
   (`App::PropertyFileIncluded`, waits for user-HDRI support in the
   backend); `Shadow_GroundSizeAuto=false` explicit ground extents;
   prefs-page UI for the new RenderParams entries (SSAO/PBR/bump are still
   parameter-editor-only globals).

3. **ViewProvider material / texture / render settings (2–3 wks)** —
   per-object appearance beyond today's fixed-function `ShapeMaterial`.
   New optional "Render" property group on `ViewProviderGeometryObject` /
   `ViewProviderPartExt` (added lazily, dynamic-property style, so plain
   documents don't grow): `Metallic`, `Roughness`
   (`PropertyFloatConstraint`, overriding the global/per-view defaults),
   `BaseColorTexture`, `NormalMap`, `EmissiveMap`, `OcclusionMap` as
   `App::PropertyFileIncluded` so images embed in the `.FCStd`, a texture
   transform (scale/offset/rotation), and render flags (`CastShadow` /
   `ReceiveShadow` mapping onto the existing `SoShadowStyle` bitmask
   plumbing). Plumbing strategy — reuse what the bridge already captures:
   texture properties build unit-0 `SoTexture2` and `SoBumpMap` nodes in
   the view provider's display-mode subgraph, so base-color/normal maps
   render in the plain Coin GL modes too and reach the backend through the
   existing `Material::textures`/`bumpmaps` capture; the scalar PBR set has
   no Coin element, so add a lightweight `SoFCRenderMaterial` node captured
   by a dedicated cache-manager post callback (the `SoBumpMap` precedent)
   into new `SoFCRenderCache::Material` fields → per-draw
   `Render::Material::{metallic, roughness, flags}` — the mesh shader
   already branches on per-draw uniforms, so no new program variants. This
   closes the Phase 2 PBR-row gap "per-object material plumbing from
   ViewProvider" and resolves the §6 open question: material parameters
   live on the ViewProvider (document-saved, per-object, GUI-side); any
   future App-side material model maps onto these properties rather than
   replacing them.
   *Scalar plumbing done (2026-07)*: `Gui::SoFCRenderMaterial` (new Coin
   node, no GL effect) carries metallic/roughness (< 0 = unset); captured
   by a dedicated cache-manager post callback (the `SoBumpMap` precedent)
   into new `SoFCRenderCache::Material::{metallic,roughness}` fields
   (part of the material key) → `Render::Material` → per-draw override
   of the PBR frame parameters in the bgfx submit loop.
   `ViewProviderGeometryObject` mirrors optional `Render_Metallic` /
   `Render_Roughness` dynamic properties (group "Render",
   `App::PropertyFloat*`) into the node (created on demand at the head
   of the view provider root, removed when both properties go away;
   resynced in `finishRestoring`). Verified on llvmpipe: per-object
   metallic/roughness on one of two objects changes exactly that
   object's pixels under global PBR; default render bit-identical to
   before.
   *Texture properties done (2026-07)*: optional
   `Render_BaseColorTexture` / `Render_NormalMap` dynamic properties
   (`App::PropertyFileIncluded` — images embed in the `.FCStd`) build
   unit-0 `SoTexture2` / `SoBumpMap` nodes with the pixels decoded by Qt
   into the node's `image` field (no Coin/simage dependency; also feeds
   the render-cache texture capture directly). A normal map alone still
   inserts a 1x1 white color texture — an enabled texture unit is what
   makes shapes generate texture coordinates. Verified: per-object
   checkerboard textures exactly one of two objects in both bgfx and
   mode-3 GL. Deviation noted while testing (pre-existing, not from this
   plumbing — both renderers read the same cached UVs): the bgfx texture
   path renders a cylinder's *cap* face differently from GL (dim
   continued checker vs GL's stripes) — ~~investigate with the texture
   rows' minification work~~ *resolved (2026-07)* by the texture mip
   chain: the difference was pure minification aliasing (GL non-mipped
   stripes vs bgfx's now-correct filtered average).
   *Texture transform + shadow flags done (2026-07)*: optional
   `Render_TextureScale` / `Render_TextureOffset` (`PropertyVector`,
   x/y) and `Render_TextureRotation` (`PropertyAngle`) build an
   `SoTexture2Transform` node beside the texture nodes — the render
   cache's existing texture-matrix capture carries it, verified
   pixel-matching between mode-3 GL and bgfx (edge-AA class only).
   `Render_CastShadow` / `Render_ReceiveShadow` map onto an
   `SoShadowStyle` node consumed through `Material::shadowstyle` by
   both the GL Shadow style and the backend shadow pass (verified in
   bgfx: cast-off removes exactly the object's shadow, receive-off
   lights exactly its shadowed pixels). Two integration facts: a
   freshly added Render_* property must be applied from a new
   `ViewProviderGeometryObject::addDynamicProperty` override — this
   fork's `Property::hasSetValue` skips same-value writes, so e.g.
   adding `Render_CastShadow` (default false = "don't cast") would
   stay inert until toggled; and derived view providers' *vtables live
   in their module libs* — a Gui-only rebuild leaves e.g. PartGui
   dispatching to the old base (symptom: the override never runs).
   *Prefs page done (2026-07)*: cog-generated Display > Render engine
   page (`DlgSettingsRender`, the params_utils preference-dialog
   pattern) exposing the RenderParams globals — closes the item-1
   leftover "prefs-page UI".
   *Per-object UI done (2026-07)*: `TaskRenderSettings` task panel
   (object context menu "Render settings..." + `Std_RenderSettings`
   beside `Std_SetAppearance` in the View menu) edits the whole
   Render_* set for the selected geometry objects — each row an
   override toggle, disabled rows remove their property (shadow flags
   only materialize in the off state, keeping documents clean); links
   resolve to their linked object. On the way:
   `ViewProviderGeometryObject::removeDynamicProperty` now resyncs the
   render nodes like `addDynamicProperty` (deleting a Render_* property
   in the property editor used to leave the nodes stale). Verified by
   a scripted GUI run (apply / reload / remove round trip).
   *Emissive/occlusion map slots done (2026-07)*: optional
   `Render_EmissiveMap` / `Render_OcclusionMap` dynamic properties
   (`App::PropertyFileIncluded`) build a new Coin-inert
   `Gui::SoFCRenderTexture` node each (a plain `SoNode` with
   slot/image/wrap fields — deriving from `SoTexture2` would feed
   Coin's texture element and the unit-0 capture), captured like the
   bump map into `Material::emissivemaps`/`occlusionmaps` (material
   key) → `Render::Material::emissivemap`/`occlusionmap`. The bgfx
   mesh path binds them at units 4/5 of the textured programs
   (`u_texParams.zw` flag presence): emissive rgb adds *after* the
   fixed-function texture environment so the base color texture does
   not modulate the glow; the occlusion first channel multiplies only
   the ambient/environment light — the constant ambient of the
   fixed-function paths and the IBL of the PBR path (glTF semantics);
   direct light stays untouched. Map-only draws route through the
   textured programs with the white unit-0 stand-in (the lone-bump-map
   pattern). The task panel gets both rows. Verified on llvmpipe:
   plain control bit-identical bgfx vs mode-3 GL; both maps vary
   correctly across an explicit-UV surface; mode-3 GL is bit-identical
   with and without the maps (only external backends draw them).
   Limitation found while verifying (pre-existing, all modes): B-Rep
   tessellation carries no real texture coordinates anywhere in this
   pipeline — Part shapes sample a constant corner texel (Coin's
   default texgen never runs for the Brep face sets, in plain Coin GL
   too). The map slots become fully useful with the UV-preserving glTF
   import (item 4); ~~a bbox-based default-UV generator for B-Rep
   tessellation is a possible follow-up~~ *default-UV generator done
   (2026-07)*: `ViewProviderPartExt::updateVisual` now fills the
   texcoord node for every tessellation node — each regular B-Rep face
   projects along the dominant axis of its accumulated normal onto the
   shape bounding box (box mapping), normalized by the largest box
   dimension for a uniform texel scale across faces; authored glTF UVs
   keep priority, closed surfaces whose normals cancel fall back to the
   z axis. Feeds plain Coin, the mode-3 GL renderer and bgfx alike (the
   GPU caches still only capture texcoords while a texture unit is
   enabled). Verified: untextured scenes bit-unchanged (0 px, both
   renderers); a checker base color tiles identically across all three
   modes on box + cylinder; the PBR map slots now vary across plain
   Part faces. *glTF wiring done (2026-07)*: the item-4 import/export round trip now carries EmissiveTexture/OcclusionTexture into/out of these properties (emissiveFactor set to 1 beside an exported emissive texture — the glTF default 0 would cancel it).
   *Metallic-roughness map slot done (2026-07)*: `Render_MetallicRoughnessMap`
   completes the material map set through the same `SoFCRenderTexture`
   route (new `METALLIC_ROUGHNESS` slot → `Material::metallicroughnessmaps`
   → `Render::Material::metallicroughnessmap` → bgfx unit 6 of the
   textured programs). `u_texParams` has no free component, so
   `u_pbrParams.x = 2` flags the map on top of the PBR branch it
   exclusively feeds: green multiplies the roughness factor, blue the
   metallic factor (glTF semantics), the 0.02 roughness floor kept after
   the multiply; the modulated roughness also drives the IBL mip/BRDF
   terms. Task panel row included. Verified on llvmpipe: plain control
   0 px bgfx vs mode-3 GL, GL bit-identical with/without the map, and
   the map's roughness/metallic gradients shade correctly across an
   explicit-UV surface under PBR while an unmapped sibling draw stays
   untouched. *glTF wiring done too*: MetallicRoughnessTexture
   round-trips into/out of the property (with the texture present an
   unset metallic factor exports as 1, not the plain-dielectric 0 — the
   factors multiply the map channels in conforming viewers).

4. **glTF import/export with materials & textures (2–3 wks)** — round-trip
   the renderer's material model (deliberately chosen as glTF
   metallic-roughness in §2) through the Import module's OCCT RWGltf path.
   Today `ReaderGltf` flattens each `XCAFDoc_VisMaterial` to a per-face
   color label (ReaderGltf.cpp:126) and `WriterGltf`/`ExportOCAF2` export
   geometry + colors only — materials and textures are dropped both ways.
   *Import*: keep the `VisMaterial` labels, extend `ImportOCAF2` to carry
   PBR data (base-color factor + texture, metallic/roughness factors +
   texture, normal/emissive/occlusion maps, double-sided, alpha mode) into
   the item-3 ViewProvider properties, extracting embedded `Image_Texture`
   blobs into `PropertyFileIncluded` files; preserve mesh UVs — glTF meshes
   arrive as triangulation-only faces whose `Poly_Triangulation` carries UV
   nodes, and `ViewProviderPartExt::updateVisual` must reuse that stored
   triangulation (not re-tessellate, which would discard the UVs) so Coin
   texcoords line up with the imported maps. *Export*: `ExportOCAF2` fills
   `XCAFDoc_VisMaterialTool` from the same ViewProvider properties (plus
   `ShapeMaterial`'s common-material fallback) so `RWGltf_CafWriter`
   serializes materials, texture images, and UV coordinates; the current
   color-only behavior stays for objects without render properties.
   Acceptance: a textured Khronos sample glTF imports and renders textured
   in both bgfx and the Coin GL modes; export → re-import is visually
   stable; a FreeCAD model with per-object metallic/roughness survives the
   round trip.
   *First cut done (2026-07)*: neutral `Import::RenderMaterial` struct
   carried both ways. Import: `ImportOCAF2::getRenderMaterial` resolves
   the label's `XCAFDoc_VisMaterial` (sub-shape labels and referred
   labels included; per-face materials collapse to whole-object),
   extracts embedded textures via `Image_Texture::WriteImage` to temp
   files, and the new `applyRenderMaterial` hook (`ImportOCAFGui`) sets
   the Render_* dynamic properties on the view provider. A material at
   the glTF defaults (metallic=roughness=1, no textures) is skipped —
   that is what a color-only export reads back as. Fixed on the way:
   `ReaderGltf::processDocument` now calls
   `XCAFDoc_ShapeTool::UpdateAssemblies()` after its fixShape
   replacements — without it assembly traversal yielded shapes whose
   labels `FindShape` could not resolve (imports also lost their proper
   names). Export: `ExportOCAF2::setGetRenderMaterial` hook (fed from
   the Gui module reading the Render_* properties + ShapeColor) writes
   an `XCAFDoc_VisMaterialPBR` per object label; unset factors export
   as dielectric (metallic 0, roughness 1) instead of the glTF metallic
   defaults. **Instancing through App::Link preserved** (companion OCCT
   fork commit d997dd346): `RWGltf_CafWriter` used to emit one glTF
   mesh per scene node, duplicating instanced meshes; it now shares one
   mesh entry among instance nodes of the same shape label (equal
   style) via a thread-local node→mesh-index map (no class layout
   change, binary compatible), and on import repeated shapes already
   become `App::Link`s.
   Verified live (GUI round trips on this box): metallic/roughness +
   base-color texture survive export→import on exactly the objects that
   carry them; base + two `App::Link`s export as 1 glTF mesh / 3 nodes
   and re-import as 1 `Part::Feature` + 2 `App::Link`s.
   *UV preservation done (2026-07)*: textured glTF meshes now skip
   `ReaderGltf::fixShape`'s facets→B-Rep rebuild (which discarded the
   triangulation's UV nodes and authored normals) and stay purely
   triangulated faces; `ViewProviderPartExt::updateVisual` emits the
   stored UV nodes of surface-less faces as an explicit
   `SoTextureCoordinate2` (indexed like the coordinates — picked up by
   plain Coin, the mode-3 GL renderer and bgfx alike) and their stored
   normals through `getPointNormals`. Fixed on the way: 4-component
   texture uploads marked every textured draw transparent (opaque
   images now upload as RGB888); a purely triangulated face has no
   vertices and was rejected as an empty shape by `ImportOCAF2`; a
   top-level free mesh lost its base color factor (material→color-label
   conversion only ran for sub-shape labels) and, with
   `ExportKeepPlacement` on, its placement (the bake-in transform has
   no B-Rep geometry to move — the location is now kept and exported
   as the node placement). BinTools persistence stores triangulation
   (with UV/normal arrays) for surface-less faces, so the shapes
   survive `.FCStd` save/restore. Verified on llvmpipe: the Khronos
   BoxTextured sample renders identically (0 px > 30) in plain Coin,
   mode-3 GL and bgfx, matching the reference screenshot;
   save/reopen, export→re-import (keep placement) and a normal-map
   round trip are all pixel-identical; untextured glTF still rebuilds
   planar B-Rep faces; regular B-Rep scenes are bit-unchanged.
   *Per-face materials done (2026-07)*: a mesh whose primitives carry
   different render-relevant materials now splits on import into one
   `Part::Feature` per material group under the same group container an
   assembly uses (links to the mesh label reference the container), each
   carrying its own material through the existing whole-object Render_*
   path with the glTF material name as its label. Grouping ignores the
   base color — materials differing only in color merge and the per-face
   color path keeps the distinction, so multi-color/uniform-factor
   meshes still import as a single feature — and keys textures by the
   reader-shared `Image_Texture` handle; untextured primitives arrive
   fixShape-rebuilt (possibly sewn), so the scan explores each sub
   shape's faces. Verified: texture + metal two-primitive mesh splits
   correctly and renders in bgfx; instanced mesh nodes become links to
   the split container; color-only material pairs and single-material
   meshes import unchanged; STEP untouched. Export of a split group
   writes one node+mesh per feature (per-face materials become
   per-object — visually equivalent, structurally N meshes instead of N
   primitives). Item-4 remaining: placement of a single textured mesh is
   still dropped with the default `ExportKeepPlacement=false`
   (deliberate preference semantics).

### Phase 3 — performance & portability (open-ended)

- **Instancing**: extend the `TShape` shape table so identical solids (Link
  arrays!) share one GPU buffer + per-instance transform/color — the fix the
  2021 `LinkShapeTable` branch identified but couldn't do in Coin. Requires
  a vertex-cache-level dedup key (TShape pointer) surfaced into
  `VertexCacheEntry`.
  *First cut done (2026-07).* No cache-level key turned out to be needed:
  Link placements already share one `SoFCVertexCache` (the render-cache
  flattening reuses the child cache pointer per placement, only the
  per-entry matrix differs), so the bridge already feeds one
  `MeshData`/`cacheId` with N `DrawCall`s differing in model matrix and
  diffuse — instancing became a pure backend grouping problem. At
  `setScene` the bgfx backend groups triangle draws by (mesh, index range,
  material-bar-diffuse); eligible are opaque, untextured, unclipped,
  non-on-top, non-water draws without autozoom. Per frame, each group of
  ≥ 2 non-hidden members collapses into a single `bgfx::submit` of the new
  `vs_fc_mesh_inst` program (paired with the stock `fs_fc_mesh`), fed a
  transient instance buffer of {model matrix columns, diffuse} per
  instance (i_data0-4; the VS picks per-vertex vs per-instance color by
  `u_instParams.x`, the FS is forced onto the v_color0 path). Members keep
  their per-draw side submissions (SSAO prepass, shadow caster, water
  depths); selection-hidden members drop out of the group per frame (the
  selected object re-renders on top). Hidden-line frames disable
  instancing wholesale (the stencil outline path reworks fill submits).
  `FC_BGFX_NO_INSTANCING` reverts to per-draw for A/B;
  `FC_BGFX_DEBUG_FEED` prints group coverage. Verified on llvmpipe (6-box
  Link-array scene): instanced vs per-draw bit-identical, incl.
  per-link override colors and a link-selected frame (group thins 6 → 5);
  shadow frame maxdiff 1/255 (float association order); vs GL stays in
  the usual axo edge-AA class. Known gaps: only the main opaque color
  pass batches (prepass/shadow/caster submits stay per-draw); textured/
  clipped/transparent/OIT draws and line/point primitives are not
  instanced (lines/points already instance per-segment for thick-quad
  expansion — cross-object batching there would collide); a non-zero
  `RenderCacheMergeCount` bakes matrices into merged caches and defeats
  grouping (default 0 is the instancing-friendly path).
  *TShape-level cut done (2026-07).* Instancing now reaches below the
  object boundary into `TopoDS_TShape`: a compound holding located
  instances of one TShape (`TopoDS_Shape` = TShape + placement) used to
  bake N transformed copies of the tessellation into one coordinate node
  — `ViewProviderPartExt::buildInstanced()` instead tessellates each
  unique leaf sub-shape once (in its local frame, deflection from the
  leaf bbox so the same part in differently sized parents shares) into a
  **global** refcounted table keyed (TShape, orientation, quantized
  tessellation parameters) — shared across objects — and instantiates
  per-leaf `SoSeparator[SoMatrixTransform, shared SoFCSelectionRoot]`
  wrappers in the display-mode roots. The shared selection root is the
  render-cache child boundary, so the existing flattening yields
  shared-cache entries with per-instance matrices and the backend's
  instanced submit batches them (verified: 6-instance compound = one
  vertex cache per primitive type, one instanced face submit).
  **Flatten-unless-provably-better**: the legacy baked build runs
  whenever the `ShapeInstancing` param is off, render-cache mode ≠ 3, no
  backend renderer is selected, the backend published no GPU-instancing
  capability (`Render::Renderer::instancingHint`, from
  `BGFX_CAPS_INSTANCING` — without real instancing many small shared
  nodes are a net loss, the 2021 `LinkShapeTable` lesson), the shape is
  not a compound with a repeated (TShape, orientation), an instance is
  mirror-placed, two leaves are identical incl. location (MapShapes
  dedup would shift element numbering), the defensive
  global-vs-concatenated numbering check fails, or the **final applied
  per-element colors diverge in value** — a same-valued `DiffuseColor`
  array counts as uniform; only resolved values matter, checked at the
  `setHighlighted*` apply points which also restructure on transitions
  in both directions (per-part colors bake into the shared vertex caches
  and cannot differ per instance until the color-variant layer lands).
  A parameter observer rebuilds Part visuals when the gate parameters
  flip. Element naming stays exact (pick paths resolve the instance
  wrapper and add its face/edge/vertex base offsets); sub-element
  highlight degrades to whole-object highlight (a detail context on the
  shared node would light every instance) — the explicitly colored
  selection now wins the backend's whole-object dedup (`SelIdBits`
  mirrored into Renderer.h, model matrix added to the dedup key so
  distinct placements never collapse). Verified on llvmpipe: instanced
  vs flattened bit-identical (boxes, incl. selection of the Link-array
  suite), GL renderer stays flattened (scene-graph probe), divergent
  per-face colors flatten while same-valued arrays stay instanced,
  cylinders render correctly. Next phases (user-agreed): **(B)**
  color-variant layer — instances partition by resolved per-face color
  vector, one shared variant faceset per distinct vector referencing the
  same coordinate/index data (draw-minimal: draws = #distinct vectors,
  memory bounded by flatten); **(C)** separate color vertex stream in
  cache/backend so variants share position/normal/index GPU buffers
  (texcoords already ride a second stream), plus forced UV capture for
  shared groups (today a shared cache built under an untextured user
  leaves a textured sharer without UVs). Per-instance sub-element
  highlight (path-keyed contexts) remains open.
- Frustum/occlusion culling, GPU-driven paths (bgfx ex.37/48) for very large
  assemblies.
- WASM build of the renderer; progressive refinement (drop AA/AO during
  camera motion, refine on idle — the Fusion 360 pattern).
- SSR (optional), GTAO, TAA where compute is available.
- **Displacement mapping** (true geometric displacement, beyond Phase 2's
  parallax illusion): vertex-shader height sampling where
  vertex-texture-fetch exists (WebGL2 guarantees it) on a GPU-subdivided
  patch, or hardware tessellation on native backends only — bgfx has no
  tessellation-shader abstraction, so the portable route is pre-subdivision.
  For CAD the honest alternative is CPU-side: re-tessellate through OCCT at
  finer deflection and displace on upload, which also keeps picking/snapping
  consistent with what is drawn — screen-space illusions never do. Decide
  per use case (fabric/knurling presentation vs. measurable geometry) when
  a concrete need appears.
- **Water surface: refraction + caustics** (presentation renders — marine/
  civil models; builds on the Phase 2 water-bounded volumetric medium).
  Refraction as the standard screen-space scheme: render the scene below
  the surface to a texture (or reuse the frame), sample it through
  normal-perturbed UV offsets at the water surface, Fresnel-blend with the
  IBL reflection — no ray tracing, WebGL2-safe. Caustics as a light-space
  projected texture modulated by the shadow map: start with an animated
  procedural/tiled caustic pattern projected onto receivers below the
  surface (the industry-standard fake); a physically-derived option later
  (render the water surface from the light, refract per texel, accumulate
  into a caustic map — PS-only, but needs float blending) where formats
  allow. Camera-above-water volumetric shafts keep using the unrefracted
  ray (the universal approximation); revisit only if a path-tracer handoff
  materializes.

Total to full feature list: **~36–46 wks** (bgfx, incl. Phase 2b) — ~7–10
months with SSR deferred.

---

## 4. Do-not-break rules

- All new behavior is gated behind `ViewParams::getRenderCache() == 3` *and*
  a non-empty selected renderer type; modes 0–2 must not change at all.
- Mode 3 without a renderer selected keeps using `SoFCRenderer` GL exactly as
  today; it also stays the fallback when backend init fails (headless, GL
  context issues).
- No behavior change to `SoFCVertexCache`/`SoFCRenderCache` build logic in
  Phase 0; the bridge is a *consumer* of `VertexCacheMap`. Any refactor that
  touches the shared structs (e.g. adding a TShape key) must keep the GL
  renderer compiling and rendering identically.
- Coin remains the owner of camera, events, manipulators, overlays,
  and picking until explicitly migrated.

## 5. Verification notes (2026-07, WSLg dev box)

- Prefs wiring verified live: RenderCache=3 + RendererType="bgfx - OpenGL"
  initializes bgfx and renders on first paint, under both xcb and
  `QT_QPA_PLATFORM=wayland`.
- **bgfx submodule updated to v1.150 (2026-07)**: upstream deleted the Linux
  GLX glue — desktop GL now always goes through **EGL** (`glcontext_egl.cpp`)
  with `BGFX_WITH_WAYLAND`/`WL_EGL_PLATFORM` on by default, so native Wayland
  comes with the update. Fork patches rebased: bgfx carries 5 (getInternal
  API, renderbuffer check, bgfx_utils `_path` param, framebuffer-format probe
  workaround, gitignore); bgfx.cmake carries 4 (SHARED default, bgfx-glslang
  target rename, generated-file fix, fork submodule URL). bx requires C++20
  (set on `FreeCADRenderer`). Demo shaders recompiled (glsl profile 140 +
  spirv variants for a future Vulkan backend).
- **Context sharing now requires Qt-on-EGL** when the renderer is active:
  automatic under Wayland; on X11 set `QT_XCB_GL_INTEGRATION=xcb_egl`.
  Coin follows automatically — its glue supports GLX+EGL simultaneously
  (upstream feature, enabled in the local build) and picks EGL at runtime
  via `eglGetCurrentContext()` (`COIN_EGL=0/1` overrides). ~~On plain
  X11/GLX, bgfx boots its own EGL context and does not crash, but blit
  sharing with Qt's GLX context is unverified.~~ *Audited (2026-07)*:
  under plain `QT_QPA_PLATFORM=xcb` (no `xcb_egl` forced) the full
  scene renders and blits correctly on this box. The renderer passes
  Qt's own context to bgfx as an *external* context
  (`init.platformData.context`), so bgfx imports GL symbols and renders
  into whatever context the runtime makes current — the GLX-vs-EGL
  choice belongs entirely to Qt and the blit shares within one Qt share
  group either way. Real-hardware X11 still untested (WSLg only).
  Check `SoOffscreenRenderer` (thumbnails) once EGL is the daily path.
- Exit-time noise on this box (pre-existing, not caused by the renderer
  work): the baseline app aborts at exit with a `QOpenGLWidget` assert in
  the debug Qt build (with the old GLX bgfx there was also an X `BadAccess`,
  gone since the EGL switch). Do not chase these when verifying renderer
  changes; a crash *before* teardown is what matters.
- `FC_NO_BGFX_QUITHOOK=1` disables the bgfx aboutToQuit cleanup hook for
  teardown debugging.
- **Wayland destination-alpha bleed (fixed 2026-07)**: blended transparent
  geometry leaves alpha < 1 in the framebuffer; Wayland compositors (WSLg)
  honor destination alpha and blend the FreeCAD window with windows behind
  it (X11 ignores it). `renderScene()` now force-clears the alpha channel
  to 1 at the end of every onscreen frame (color-masked clear), both GL and
  backend paths. Side effect: `grabFramebuffer()` images are now fully
  opaque — earlier color checks on transparent pixels were skewed by the
  premultiplied-alpha unpack in the QImage→PNG save.
- **Phase 0 bridge smoke-tested (2026-07, Wayland/WSLg)**: box+cylinder
  scene renders through bgfx (shaded faces, black edges, per-object colors,
  selection tint on top, gradient background composited), GL pass skipped
  while the backend is live; mode-3 with `RendererType=Default` verified
  pixel-identical to before. Debug helpers: `FC_RENDERER_PARALLEL_GL=1`
  (draw GL on top of the backend), `FC_BGFX_DEBUG_CLEAR=1` (red clear
  color), `FC_BGFX_DEBUG_READBACK=1` (per-frame FBO pixel statistics on
  stderr).

## 6. Open questions

- ~~Feed the backend from `SoFCRenderCacheManager` (parallel sink to
  `SoFCRenderer`) vs. behind `SoFCRenderer` (replace its GL emission)?~~
  Resolved (2026-07): behind `SoFCRenderer` — it is the single choke point
  for all five feeds (the manager calls `addSelection` from ~15 sites), its
  structures stay alive for fallback/bbox, and its GL emission is skipped
  per frame based on `canSkipInternal()`. Note the one-frame latency: the
  backend draws *before* the Coin traversal that rebuilds/feeds the scene,
  so a scene change shows one frame late; `Renderer::needsRedraw()` +
  `renderScene()` scheduling converges on the next frame.
- bgfx shared-context vs. blit: current blit works but costs a full-screen
  copy; investigate rendering directly into the Qt FBO once passes need
  MSAA/HDR targets anyway.
- Per-view renderer instances vs. shared engine with per-view views
  (bgfx view ids are a global 16-bit space — needs a small allocator for
  multiple 3D views).
- ~~Where PBR material parameters live (new `ViewProvider` properties vs.
  App-side material model) — coordinate with upstream material work.~~
  Resolved (2026-07): ViewProvider-side properties, document-saved,
  glTF-metallic-roughness-shaped (Phase 2b item 3); an App-side material
  model can map onto them later. Interchange goes through the Import
  module's RWGltf/XCAF path (Phase 2b item 4).

## 7. References

- Research (2026-07): bgfx WebGPU status <https://bkaradzic.github.io/posts/webgpu/>;
  bgfx examples <https://bkaradzic.github.io/bgfx/examples.html>;
  DiligentFX modules <https://github.com/DiligentGraphics/DiligentFX>;
  OCCT WBOIT <https://dev.opencascade.org/content/weighted-blended-order-independent-transparency>;
  OCCT PBR <https://www.opencascade.com/blog/pbr-in-occt-3d-viewer/>;
  VTK-on-WebGPU <https://www.kitware.com/vtk-webgpu-on-the-desktop/>;
  WBOIT paper <http://casual-effects.blogspot.com/2014/03/weighted-blended-order-independent.html>;
  stencil capping reference <https://threejs.org/examples/webgl_clipping_stencil.html>.
- Branches: `origin/LinkShapeTable` (a6f714368d — shape-table/instancing
  lesson), `origin/NewRenderer`, `origin/LinkRender` (renderer module
  history), `origin/BrepFaceSetRTREE` (pre-cache-era perf work).
