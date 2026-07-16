# Renderer Plan: bgfx-based Render Engine

Status: draft, 2026-07-16. Companion to `RoadMap.md` (renderer workstream) and
`ComputeBoundaries.md`.

Goal: a modern render engine for FreeCAD with PBR materials + IBL, SSAO,
order-independent transparency, capped section views, outline rendering, and
conventional CAD shaded/wireframe display — portable across desktop GL/Vulkan/
Metal/D3D, browser (WASM), and mobile via bgfx's runtime backend selection.

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
   bgfx has no fixed-function polygon offset). Still missing:
   textures/clip planes/autozoom ignored, per-vertex-transparent caches
   go wholesale to the transparent bucket.
   **Known issue — transparent scene geometry is invisible**: transparent
   draws don't write depth (GL parity), so after the color+depth blit the
   Coin gradient background node (depth-tested at the far plane) repaints
   those pixels; they also blend against the bgfx clear color rather than
   the real background because bgfx renders before Coin. Fix direction:
   render the background inside bgfx (gradient quad / clear) and skip
   Coin's background when the backend rendered — part of the item-5
   compositing audit.
4. **Pass skeleton**: bgfx view sequence reproducing today's ordering.
   *First cut done (2026-07):* 4 views — opaque → transparent (bbox-center
   depth sort via `ViewMode::DepthDescending`) → on-top → selection/
   highlight. *Grown (2026-07)*: depth-write-only prepass of on-top fills
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
   Known deviation: GL draws the *preselected* face as outline only
   (`NoPreSelFaceHighlightWithOutline` + `ShowPreSelectedFaceOutline`
   defaults) while bgfx fills it, since the outline pass is a Phase 2
   feature; highlight lines are not thickened (`SelectionLineThicken`,
   Phase 1). `FC_BGFX_DEBUG_FEED=1` dumps the translated
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
   the backend skips matching scene draws. Not replicated: the
   per-selection dedup (same object in several selection ids draws more
   than once), selection line pattern (`SelectionLinePattern`).

Exit criteria: a real model renders in bgfx visually close to today's mode-3
output (shaded + edges + selection/highlight + clip planes without caps), GL
path bit-identical when renderer off.

### Phase 1 — CAD parity (4–5 wks)

Line pattern/width shaders (no `glLineStipple`/`GL_LINE` in modern APIs —
screen-space quad-expanded lines), point sprites, per-face color, polygon
offset semantics, hidden-line draw style, on-top/annotation ordering, draw
styles. This is grind, but semantics are all encoded in
`SoFCRenderer::applyMaterial` + the pass loop.

### Phase 2 — visual features (7–9 wks)

| Feature | Est. | Notes |
|---|---|---|
| WBOIT | 1.5–2 | replaces bbox-sort for transparent bucket |
| SSAO (ASSAO) | 1.5–2 | needs depth+normal prepass from Phase 0 |
| PBR + IBL | 3–4 | BRDF + env prefilter pipeline; matcap fallback; material property plumbing from ViewProvider |
| Section caps | 2–3 | stencil capping + hatch, port `_renderSection` semantics |
| Outline/hidden-line | 1.5–2 | screen-space depth/normal pass + existing edge geometry |

### Phase 3 — performance & portability (open-ended)

- **Instancing**: extend the `TShape` shape table so identical solids (Link
  arrays!) share one GPU buffer + per-instance transform/color — the fix the
  2021 `LinkShapeTable` branch identified but couldn't do in Coin. Requires
  a vertex-cache-level dedup key (TShape pointer) surfaced into
  `VertexCacheEntry`.
- Frustum/occlusion culling, GPU-driven paths (bgfx ex.37/48) for very large
  assemblies.
- WASM build of the renderer; progressive refinement (drop AA/AO during
  camera motion, refine on idle — the Fusion 360 pattern).
- SSR (optional), GTAO, TAA where compute is available.

Total to full feature list: **~26–33 wks** (bgfx) — ~5.5–6.5 months with SSR
deferred.

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
  via `eglGetCurrentContext()` (`COIN_EGL=0/1` overrides). On plain
  X11/GLX, bgfx boots its own EGL context and does not crash, but blit
  sharing with Qt's GLX context is unverified — audit the POC's
  native-context passing during the Phase 0 compositing work. Check
  `SoOffscreenRenderer` (thumbnails) once EGL is the daily path.
- Exit-time noise on this box (pre-existing, not caused by the renderer
  work): the baseline app aborts at exit with a `QOpenGLWidget` assert in
  the debug Qt build (with the old GLX bgfx there was also an X `BadAccess`,
  gone since the EGL switch). Do not chase these when verifying renderer
  changes; a crash *before* teardown is what matters.
- `FC_NO_BGFX_QUITHOOK=1` disables the bgfx aboutToQuit cleanup hook for
  teardown debugging.
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
- Where PBR material parameters live (new `ViewProvider` properties vs.
  App-side material model) — coordinate with upstream material work.

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
