# Renderer & Shader Design

How the bgfx render-cache backend shades a scene: the overall architecture,
the per-frame pass pipeline, every special effect and the shaders that
implement it, and the property/parameter model that drives them.

This is a companion to `docs/TShapeRenderCache.md` (geometry caching /
instancing) and `docs/RoadMap.md` (project direction). It documents the
`src/Gui/Renderer/` backend, not the legacy Coin GL renderer.

> **Status (2026-08)**: written 2026-07-21, before the user-shader
> release and the effect library. For those, and for the current
> architecture reference, see `docs/RenderEngine.md` (§5 user shaders)
> and `docs/RenderDebug.md` (§6 design history); this file remains the
> deep-dive on the built-in pass pipeline and effect shading math.

---

## 1. Architecture

FreeCAD's experimental renderer runs **alongside** Coin3D, not in place of
it. It is selected only when the render-cache mode is the "renderer" mode
(`ViewParams::getRenderCache() == 3`, Render Type = e.g. `"bgfx - OpenGL"`).

```
DocumentObject → ViewProvider → Coin scene graph
                                   │
                    SoFCRenderCache (per-shape vertex/material caches)
                                   │
              SoFCRenderer  ──►  SoFCRendererBridge::translate()
              (Inventor/)          (Coin material/geometry → backend structs)
                                   │
                                   ▼
                    Render::Renderer  (Renderer.h, backend-neutral)
                      ├── BGFXRenderer   (bgfx — the one we develop)
                      └── DiligentRenderer
```

- **`Render::Renderer` / `RendererFactory`** (`Renderer.h`): the
  backend-neutral interface and the runtime-selected factory. Backends
  self-register at static init.
- **`SoFCRendererBridge`** (`Inventor/SoFCRendererBridge.cpp`): translates
  Coin render caches into backend-neutral structs — `DrawCall`, `Material`,
  `MeshData`, and the per-frame effect configs (`AOConfig`,
  `VolumetricConfig`, `WaterConfig`, `LightConfig`, `PBRConfig`,
  `BumpConfig`, `HiddenLineConfig`, `SectionConfig`). This is the single
  place where a Coin/ViewParams value becomes a shader input.
- **`BGFXRenderer`** (`Renderer/BGFXRenderer.cpp`): the bgfx backend. Owns
  the offscreen framebuffers, all render passes, and the uniform/texture
  bindings. Two build flavors from the same source:
  - **Desktop** (`libFreeCADGui`): renders into a shared Qt GL context and
    composites under Coin's `SoGLRenderAction`.
  - **Standalone / WASM** (`FC_RENDERER_STANDALONE`, `Renderer/wasm/main.cpp`):
    a browser viewer that replays a streamed **scene snapshot**
    (`SceneDump.*`) with its own bgfx instance. Same passes, same shaders
    (essl), cross-API via bgfx backend selection.

Cross-API portability (GL / Vulkan / Metal / WebGL) is delegated to bgfx,
not hand-written here. Keep desktop-GL assumptions out of renderer code.

### Shader sources

- Source: `src/Gui/Renderer/bgfx/shaders/*.sc` (fragment/vertex) and
  `*.sh` (shared includes, e.g. `fc_mesh_fs.sh`, `fc_glass_fs.sh`,
  `fc_volume.sh`, `fc_volume_shadow.sh`).
- Compiled with `shaders/compile.sh` into
  `assets/shaders/{glsl,spirv,essl}/*.bin` (committed artifacts).
  `glsl` = desktop GL, `spirv` = Vulkan, `essl` = WebGL2/WASM.
- **Editing a shared `.sh` include means recompiling every `.sc` that
  includes it.** After `compile.sh`, the copied assets under
  `build/*/share/Renderer` and the WASM `fcviewer.data` must be refreshed
  (rebuild, or copy the `.bin`s) — see §6.

---

## 2. Frame pipeline (pass ordering)

Each frame submits an ordered list of bgfx *views* (`BGFXView::ViewId` in
`BGFXRenderer.cpp`). Order matters — screen-space effects composite into the
scene color before later geometry draws over them. Abbreviated:

| # | Pass | Purpose |
|---|------|---------|
| `ViewBackground` | clear + gradient background (clip space) |
| `ViewShadow` / `…BlurH/V` / `…Tint` | variance shadow-map moments + separable blur; glass casters add a tint map |
| `ViewAOPrepass` | SSAO depth+normal prepass of the opaque scene |
| `ViewWaterFront/Back`, `Glass…`, `Cloud…`, `Fire…` | front/back depth bounds of each closed **medium** body |
| `ViewAOGen` / `ViewAOBlur` | SSAO generation + 4×4 blur (R8 targets) |
| `ViewVolGen` | volumetric light-shaft raymarch (half-res) |
| `ViewGroundRefl` | mirrored-camera opaque re-render for ground reflection |
| `ViewOpaque` | **opaque** triangles / lines / points (PBR + shadow) |
| `ViewSectionCap` | stencil section caps of clipped opaque solids |
| `ViewGroundReflApply` | blend the mirrored scene onto the ground plane |
| `ViewOutline` | hidden-line stencil outlines |
| `ViewCaustics` | additive water-caustics splat over submerged surfaces |
| `ViewVolApply` | bilateral upsample + composite of the light shafts |
| `ViewWaterCopy` | copy scene color → refraction source texture |
| `ViewWaterSurface` | water bodies re-rendered as the animated surface |
| `ViewGlassSurface` | glass bodies re-rendered as refractive glass |
| `ViewTransparent` / `ViewOITComposite` | WBOIT transparent accumulate + resolve |
| `ViewSectionCapTransp` | section caps of clipped transparent solids |
| `ViewOnTop` | scene geometry with **on-top** materials |
| `ViewHighlight` | selection / preselection highlight |
| `ViewOverlay0…8` | overlay feeds (axis cross, NaviCube, datums, dimensions, fps…) |
| `ViewPresent` | standalone only: resolve+blit to the swapchain |

**Consequences to remember**

- `ViewOnTop`, `ViewHighlight`, and the overlay views are submitted **after**
  AO / volumetric / water / caustics. On-top geometry (`Material::ontop`,
  e.g. `SoAnnotation`, the rotation-center gizmo) therefore escapes those
  screen-space effects for free; only lighting and shadow still reach it
  (turn those off per-node — see the rotation-center sphere).
- Overlays additionally force `shaded = false` and zero the fire light
  (`overlayView >= 0`), so UI chrome is flat and untinted.

---

## 3. Effects and their shaders

Each effect is: a set of passes, the shader(s) that implement it, and the
config struct the bridge fills. "Property" = per-view dynamic
`Render_*` / `RenderShadow_*` property (overrides), with the named
`ViewParams` / `RenderParams` as the fallback default.

### 3.1 PBR surface shading + IBL
- **Shaders**: `fs_fc_mesh*.sc` including `fc_mesh_fs.sh`.
- **What**: metallic/roughness BRDF (GGX + Karis visibility + Schlick
  Fresnel), a white view-axis headlight, plus image-based lighting from the
  prefiltered environment cubemap (SH irradiance + prefiltered mips +
  Lazarov analytic BRDF). Falls back to Blinn-Phong when PBR is off.
- **Config**: `PBRConfig` (`translatePBRConfig`).
- **Controls**: per-object `Render_Metallic`, `Render_Roughness`,
  metallic/roughness/occlusion/emissive maps; view `Render_PBR*`;
  `RenderParams` `EnvIntensity`, `Roughness`, etc.

### 3.1a Machined surface finish
- **Shaders**: `fc_finish.sh`, called from `fc_mesh_fs.sh` (so every mesh
  variant, opaque and OIT, textured or not).
- **What**: a procedural knurl / brushed / blasted / turned relief over
  object space (`v_opos`/`v_onrm`), triplanar-projected, perturbing the
  shading normal through Mikkelsen's surface gradient from the pattern's
  analytic gradient — no UV, no tangent frame, no per-draw matrix.
  Filtered against the pixel footprint, with the slope variance the
  filter removes handed to the roughness (or, in the Phong path, to the
  shininess).
- **Config**: none of its own — `u_finishParams` is per-draw, from
  `Render::Material::finish*`.
- **Controls**: the appearance's own `App::SurfaceFinish` (authored, and
  it wins), else per-object `Render_Finish`, `Render_FinishPitch`,
  `Render_FinishDepth`, `Render_FinishAngle`.

### 3.2 Bump / parallax
- **Shaders**: `fc_mesh_fs.sh` (dFdx/dFdy tangent frame; only on textured
  meshes with a bump map).
- **Config**: `BumpConfig` (`translateBumpConfig`).
- **Controls**: `Render_BumpScale`, `Render_Parallax`.

### 3.3 SSAO (ambient occlusion)
- **Passes**: `ViewAOPrepass` → `ViewAOGen` → `ViewAOBlur`.
- **Shaders**: `fs_fc_ssao.sc`, `fs_fc_ssao_blur.sc`.
- **What**: hemisphere kernel rotated per pixel by a tiled 4×4 noise
  texture; depth-compared against the prepass, blurred, then sampled by the
  mesh shaders (`aoMeshTex`, unit 9) to attenuate their ambient term —
  there is no fullscreen apply pass. Only attenuates indirect light.
- **Config**: `AOConfig` (`translateAOConfig`).
- **Controls**: `Render_SSAO` (bool), plus `Render_SSAO*` radius / intensity
  / bias; `RenderParams` `SSAO*` fallback. Live-toggle:
  `Gui.activeView().Render_SSAO = False`.

### 3.4 Shadow (variance shadow map)
- **Passes**: `ViewShadow` (moments) → `ViewShadowBlurH/V` → optional
  `ViewShadowTint` (glass); sampled in `ViewOpaque` / media passes.
- **Shaders**: `vs_fc_shadow.sc` / `fs_fc_shadow.sc`, `fs_fc_shadow_blur.sc`,
  `fs_fc_shadow_tint.sc`; receiver side in `fc_mesh_fs.sh`
  (`fc_shadowTap`) and `fc_volume_shadow.sh` for media.
- **What**: VSM (Chebyshev upper bound) or EVSM; a directional/spot scene
  light inserted by the **Shadow draw style** (`Std_DrawStyleShadow`). The
  viewer headlight stays as an unshadowed "other light."
- **Config**: `LightConfig` (`translateLightConfig`, from the traversal
  state's light element + `RenderShadow_*` properties).
- **Controls**: `Gui::materializeShadowRenderParams` materializes the
  per-view `RenderShadow_*` properties -- `RenderShadow_Epsilon`,
  `Threshold`, `Precision`, `SpreadSize`, `SmoothBorder` and the ground
  receiver's `ShowGround`/`Ground*` -- with `ViewParams` `Shadow*`
  fallbacks; the light itself is `Render_Light*`. (Before
  docs/CoinRetirement.md stage 4d these were the Shadow draw style's own
  `Shadow_*` family; documents carrying that are migrated on restore.)

  **! The `RenderShadow_Epsilon` quirk (VSM variance floor).** `fc_shadowTap`
  computes `pmax = va / (va + dd²)` where the variance
  `va = max(m2 − m1², 0) + epsilon` and `epsilon = u_shadowParams.y`
  comes from `RenderShadow_Epsilon`. **At `epsilon == 0`, `va -> 0` on the
  self-shadowed side (receiver depth ≈ stored depth), so the bound flips
  0/1 per pixel and speckles the terminator band with dark dots** — visible
  on real GPUs (desktop GL, WebGL) though not on the software rasterizer.
  Coin's own shadow path guarded this (`epsilon == 0 → 1e-5`), but the
  Shadow draw style leaves the property at 0 by default and the bridge fed
  that raw 0 into the backend. Fixed by enforcing a **configurable minimum**
  in two places, both keyed on `ViewParams::ShadowEpsilonMinimum`
  (default `1e-6`):
  - `View3DInventorViewer` shadow setup: `RenderShadow_Epsilon`'s
    `PropertyPrecision` (a `PropertyFloatConstraint`) lower-bound constraint
    is set to the minimum instead of `0.0`, and the value is clamped up.
  - `SoFCRendererBridge::translateLightConfig`: clamps
    `LightConfig::epsilon` up to the same minimum (a value stored before the
    constraint existed can still reach the backend).

  Set `ShadowEpsilonMinimum` to 0 to disable the floor. Raise `RenderShadow_Epsilon`
  itself to trade a little peter-panning for softer self-shadow terminators.

### 3.5 Volumetric lighting + media (water / fire / cloud)
- **Passes**: `ViewWaterFront/Back`, `ViewFireFront/Back`,
  `ViewCloudFront/Back` (depth bounds) → `ViewVolGen` (half-res raymarch) →
  `ViewVolApply` (bilateral upsample + extinction/inscatter composite).
- **Shaders**: volumetric raymarch via `fc_volume.sh` /
  `fc_volume_shadow.sh`; per-medium fire/cloud noise.
- **What**: god-rays through a participating medium plus per-object closed
  volumes turned into media: **water** = tinted scattering/absorption bounded
  by its front/back depths; **fire** = emissive rising-FBM flame (geometry
  itself not drawn); **cloud** = procedural FBM density.
- **Config**: `VolumetricConfig` (`translateVolumetricConfig`).
- **Controls**: `Render_Volumetric` + `Render_VolumetricIntensity` /
  `…Density`; per-object `Render_Water`/`Render_WaterDensity`,
  `Render_Fire`/`…Intensity`/`…Detail`/`…Speed`,
  `Render_Cloud`/…; `RenderParams` fallbacks. Caustics and the water medium
  require the volumetric pass to be active.

### 3.6 Caustics
- **Pass**: `ViewCaustics` (additive, over surfaces inside a water body's
  depth interval, before the volumetric extinction multiply).
- **Shader**: `fs_fc_caustics.sc` (+ `fc_volume_shadow.sh`).
- **What**: animated procedural caustic pattern in a light-perpendicular
  frame, Beer-Lambert tinted by the underwater depth, gated by the shadow
  map. Only lands on **submerged** surfaces (pool floor, submerged bodies);
  hidden behind the refractive top surface from a grazing view.
- **Config**: part of `VolumetricConfig`.
- **Controls**: `Render_Caustics` + `Render_CausticsIntensity` /
  `…Scale` / `…Speed`. Requires `Render_Volumetric`.

### 3.7 Water surface (refraction + reflection)
- **Passes**: `ViewWaterCopy` (scene color → refraction source) →
  `ViewWaterSurface` (water bodies re-rendered as the surface).
- **Shader**: `fs_fc_water.sc`.
- **What**: animated wave-perturbed normal; screen-space **refraction** of
  the copied scene behind the surface (depth-rejected against the prepass so
  protruding geometry doesn't smear); **reflection** = Fresnel blend of the
  environment cubemap and a screen-space-reflection (SSR) march that mirrors
  the actual on-screen scene, with env as the off-screen/miss fallback; plus
  a sun glint from the scene light. `FC_WATER_REFLECT` gates the reflection
  block. (SSR is a per-pixel ray march — a real per-frame cost; keep the step
  count modest.)
- **Config**: `WaterConfig` (`translateWaterConfig`).
- **Controls**: `Render_WaterSurface` (the refraction/reflection toggle) +
  `Render_WaterWaveStrength` / `…Scale` / `…Speed`.

### 3.8 Glass
- **Pass**: `ViewGlassFront/Back` (depths) → `ViewGlassSurface`.
- **Shader**: `fs_fc_glass.sc` including `fc_glass_fs.sh`; shadow tint
  in `fs_fc_shadow_tint.sc`.
- **What**: IOR screen-space refraction, Fresnel env reflection, per-channel
  Beer-Lambert absorption over body thickness; casts a soft tinted shadow.
- **Controls**: per-object `Render_Glass`, `Render_GlassIOR`,
  `Render_GlassDensity`, `Render_GlassRoughness`.
- **MaterialX**: a surface stating a constant `transmission_weight` of
  one half or more is a glass body too, claimed by the capture
  (`Material::glassmtlx`, docs/MaterialStorage.md sec 17.21): the
  document's `transmission_color` is the body colour, LINEAR and never
  decoded, its `transmission_depth` the density (`1 / depth`; none =
  a tint applied once at the surface, `u_glassTint`), `specular_ior`
  the IOR and `specular_roughness` the roughness (a mapped one by its
  mean). `Render_Glass` on the same shape wins. Those flat values feed
  the flat consumers only -- the shadow tint, the viewer tier, and the
  pass while its splice compiles: on the desktop the body draws with
  `fc_glass_fs.sh` spliced with the document's generated material
  function (`FC_USER_MATERIAL`, paired with `vs_fc_mesh_tex`), which
  reads the transmission colour, depth and weight, the IOR, the
  roughness and a normal map per fragment (sec 17.22).

### 3.9 Ground reflection
- **Passes**: `ViewGroundRefl` (mirrored-camera opaque re-render) →
  `ViewGroundReflApply` (blend onto the ground plane).
- **Shader**: `fs_fc_groundrefl.sc`.
- **What**: mirrors the model in the Shadow draw style's z=0 ground plane.
  Distinct from the water surface's own reflection; only effective while the
  Shadow ground plane is shown.
- **Controls**: `Render_GroundReflection` + `Render_GroundReflectionIntensity`.

### 3.10 Hidden-line, section caps, transparency
- **Hidden-line outline**: `ViewOutline`, stencil outlines; `HiddenLineConfig`
  (`translateHiddenLineConfig`), `Render_HiddenLine*` / `HiddenLine_*` view
  properties (`outline`, `perFaceOutline`, `hideVertex/Face/Seam`,
  `outlineWidth` — a `PropertyFloatConstraint`, the pattern this doc's
  epsilon fix follows).
- **Section caps**: `ViewSectionCap` / `…Transp`; `SectionConfig`
  (`translateSectionConfig`); stencil caps of clip-plane-cut solids.
- **Transparency**: `ViewTransparent` + `ViewOITComposite` — weighted,
  blended order-independent transparency (WBOIT).

### 3.11 Cavity (curvature) shading
- **Passes**: `ViewCavity` — a fullscreen multiply over the finished opaque
  scene, after the section caps and before the outlines.
- **Shaders**: `fs_fc_cavity.sc` (reads the prepass, `fc_prepass_read.sh`).
- **What**: screen-space divergence of the prepass *normals*, darkening
  concave creases (valley) and convex ridges. Reading positions instead
  would make every tessellation facet boundary read as a crease;
  interpolated normals stay smooth across facets and jump only at real
  edges. It darkens only — the scene target is 8-bit, so a ridge
  *highlight* is not available.
- **Config**: `CavityConfig` (`translateCavityConfig`).
- **Controls**: `Render_Cavity` + `Render_CavityValley` /
  `Render_CavityRidge`. Composes with AO rather than replacing it: cavity
  is a one-pixel curvature term, occlusion a radius-based visibility
  integral.

### 3.12 Matcap shading
- **Shaders**: `fc_mesh_fs.sh` (the matcap branch).
- **What**: replaces the scene lighting with a fixed studio attached to the
  camera, looked up by each fragment's view-space normal, so form reads the
  same wherever the light is. The presets (Studio / Clay / Metal / Pearl)
  are computed in the shader, not sampled from images — no assets, sharp at
  any resolution, and free for the browser tier. Overrides PBR while on.
- **Config**: `MatcapConfig` (`translateMatcapConfig`).
- **Controls**: `Render_Matcap`, `Render_MatcapPreset` (an enumeration),
  `Render_MatcapTint` (how much each object's own color tints it).

---

## 4. Property / parameter model

Three tiers feed the shaders, resolved in the bridge's `translate*Config`
helpers (highest priority first):

1. **Per-object material** — `SoFCRenderMaterial`, usually from a
   ViewProvider's `Render_*` properties (`Render_Metallic`, `Render_Water`,
   `Render_Fire`, `Render_Glass`, `Render_Cloud`, …). Becomes fields on
   `Render::Material`.
2. **Per-view dynamic property** -- `Render_<Name>` (effects),
   `RenderShadow_<Name>` (the scene light's shadow map and its ground
   receiver, group "Render Shadow") or `HiddenLine_<Name>` on the
   `View3DInventor` object. Materialized by
   `View3DInventorViewer::setRendererType` / the draw styles; read read-only
   by `viewParamOverride` in the bridge. **Live-tunable** from the Python
   console, e.g. `Gui.activeView().Render_SSAO = False`. A handful of them
   — the shading model plus cavity, occlusion, shadows and bloom — also
   have a UI: the **Shading** section of the Display style tool button's
   drop-down (`Gui/ShadingOptions.h`), which writes these same properties
   and disables itself when no backend is selected.
3. **Global default** — `RenderParams` (`Preferences/View/Render`, generated
   from `RenderParams.py`) and `ViewParams` (`Preferences/View`, generated
   from `ViewParams.py`). The fallback when no view property is set.

`RenderParams.py` / `ViewParams.py` are the source of truth; the `.cpp`/`.h`
are cog-generated (`python3 -m cogapp -r <file>`). Add a param there and
regenerate — never hand-edit the generated accessors.

---

## 5. Snapshot / streaming (standalone viewer)

The desktop backend can serialize a frame's scene + effect configs into a
**snapshot** (`SceneDump.*`, `FC_BGFX_SERVE_SCENE`) that the WASM viewer
replays. `LightConfig` (incl. the clamped `epsilon`), `VolumetricConfig`,
`WaterConfig`, etc. are serialized field-for-field, so effect fixes made on
the server side (like the epsilon floor) automatically reach the browser.
The snapshot is versioned (`kVersion`); the viewer and backend versions must
match. Bump the version when adding fields and keep read gated on it.

---

## 6. Gotchas

- **`RenderShadow_Epsilon` floor** (sec 3.4) -- a zero VSM epsilon speckles the
  terminator; the `ShadowEpsilonMinimum` floor guards it.
- **On-top escapes screen-space effects** — `Material::ontop` /
  `SoAnnotation` geometry composites after AO/volumetric/water; only
  lighting and shadow still apply. Use `SoLightModel::BASE_COLOR` +
  `SoShadowStyle::NO_SHADOWING` for pure UI chrome (rotation-center gizmo).
- **bgfx owns MSAA** — the offscreen target's sample count comes from the
  AntiAliasing pref (`setMSAASamples`), not the Qt context; a change rebuilds
  the target at a frame boundary, no view clone.
- **bx matrices default to left-handed** — build projection/reflection
  matrices with the right handedness or the scene renders mirrored/inverted.
- **Rebuild propagation** — after a shader or renderer change, refresh the
  copied assets and rebuild dependent modules (a shared `.sh` include touches
  every mesh shader; a `Material`/config header touches every module linking
  it). Per-target builds can leave siblings ABI-stale.
- **Effects only render in render-cache mode 3** — otherwise the backend is
  null and only Coin draws.
- **Every picked colour a pass uploads is decoded on the CPU** --
  `unpackAuthoredColor(..., colorManaged())`, never `unpackColor`, for any
  authored colour that becomes a uniform (the material slots, the glass /
  water / light-body tints, the shadow tint, the outline and cap fills);
  the 8-bit vertex and instance streams decode on the GPU in `fc_color.sh`.
  A raw unpack absorbs, blooms or fills with the display number, and the
  output transform then paints it 1.5x too bright (a 0.5 grey lands at
  0.73) -- the glass tint was 1.8x weaker than Cycles' that way.
