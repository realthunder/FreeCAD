# Render Engine Design

The bgfx-based renderer (`src/Gui/Renderer/`) and its user-programmable
shader framework. This is the architecture reference; companion
documents cover specific subsystems in depth:

- `docs/RenderDebug.md` — render debugging, the verification harness,
  and the design history of the user-shader feature (§6).
- `docs/TShapeRenderCache.md` — TShape-level tessellation sharing and
  the cross-object instancing architecture.
- `docs/ComputeBoundaries.md` — the headless-server / unified-protocol
  direction the renderer tiers plug into.
- `docs/RoadMap.md` — where this all is going.

## 1. Goals

- One renderer codebase across **desktop, browser and mobile**. All
  cross-API portability (OpenGL / Vulkan / Metal / WebGL) is delegated
  to bgfx's runtime backend selection — FreeCAD code never branches on
  the graphics API.
- **Coexistence with Coin3D**, not replacement: the renderer draws the
  shaded model; Coin's own GL path still composites everything the
  renderer does not claim. Migration is incremental.
- **Deterministic frames**: given the same scene, camera and
  parameters, a frame is byte-reproducible (the golden-image harness
  depends on it; every animated effect runs on one freezable clock).
- **User-programmable shading** as a first-class document feature, not
  a debug backdoor — parametric properties drive uniforms through the
  same property → recompute → view loop as everything else.

## 2. Pipeline overview

```
Coin scene graph (ViewProviders)
        │  capture (SoFCRenderCacheManager: caches open at
        │  SoFCSelectionRoot boundaries; SoFCVertexCache records
        │  triangles/lines/points, materials, textures, shaders)
        ▼
SoFCRenderCache / SoFCVertexCache          (Gui/Inventor/)
        │  flatten + merge material state, selection/highlight
        │  side-channels, shader overrides
        ▼
SoFCRenderer draw lists                    (Gui/Inventor/SoFCRenderer.*)
        │  translate (SoFCRendererBridge): Render::DrawCall,
        │  Render::Material, light/shadow/AO/water/debug configs,
        │  user shaders, dynamic uniform bindings
        ▼
Render::Renderer interface                 (Gui/Renderer/Renderer.h)
        │  RendererFactory: backends self-register by type string
        ▼
BGFXRenderer                               (Gui/Renderer/BGFXRenderer.cpp)
        │  pass sequence over shared framebuffer, per-frame configs,
        │  program/uniform/texture management, instancing
        ▼
bgfx → GL / Vulkan / Metal / WebGL
```

The renderer only runs when render-cache mode 3 is selected
(`ViewParams::getRenderCache() == 3`). `View3DInventorViewer::renderScene`
calls `renderer->render(...)` first into the shared Qt GL context, then
Coin's `SoGLRenderAction` traverses the (mostly pruned) scene graph on
top for everything still Coin-owned.

### Tiers

The same `BGFXRenderer.cpp` compiles into three deployments:

| Tier | Definition | Scene source | Shaders |
|---|---|---|---|
| Desktop | in-process with the Qt GUI | live render caches | build-time `shaderc` pack + on-demand user compile |
| Standalone / WASM viewer | `FC_RENDERER_STANDALONE`, `Gui/Renderer/wasm/main.cpp` | `SceneDump` snapshot, streamed over WebSocket (`SceneStreamServer`) | its own essl pack + server-compiled user-shader binaries shipped in the snapshot |
| Headless serve | desktop build under Xvfb (`scripts/renderer-serve.sh`) | live caches | as desktop |

`SceneDump` serializes the draw lists, materials, configs and the
user-shader table (sources, parameters, compiled variants); the
snapshot version gates format changes and the viewer self-reloads on a
newer payload.

## 3. Frame anatomy

Each frame is a fixed sequence of bgfx views (`BGFXView::PassView`)
sharing one framebuffer (auxiliary passes own theirs). Groups, in
order:

1. **Background** — clear, gradient quad, optional sun disc.
2. **Shadow block** — variance shadow map (EVSM moments) of the scene
   light + separable blur; glass-caster tint map + blur; up to 10
   bulb-shadow tiles (4×4 VSM atlas, cached until casters change).
3. **AO block** — depth+normal prepass (non-MSAA RGBA16F), GTAO depth
   MIP pyramid, AO generation + denoise into R8.
4. **Media depth intervals** — front/back depth pairs per volumetric
   medium (water, glass, cloud, fire): the entry/exit intervals of the
   respective raymarches.
5. **Volumetrics** — half-res light-shaft raymarch + temporal
   accumulation.
6. **Reflection** — ground-reflection re-render of the opaque scene
   (mirrored camera), media composited in.
7. **Beauty** — `ViewOpaque` (triangles, lines, points), stencil
   section caps, hidden-line outlines, caustics splat, volumetric
   upsample-apply.
8. **Water/glass surfaces** — scene-color copy, then the surface draws
   re-rendered with refraction/absorption/planar reflection.
9. **Transparency** — WBOIT accumulation + fullscreen resolve (or
   depth-sorted draws in `ViewTransparent` when OIT is off).
10. **Bloom** — bright pass, light-source emit, separable blur,
    additive apply.
11. **User post** — scene-color copy + the user post-stage program
    fullscreen (§5).
12. **Debug** — `RenderDebug_ViewMode` buffer visualization
    (`docs/RenderDebug.md` §2).
13. **On-top / highlight** — on-top materials, selection/preselection.
14. **Overlays** — up to 9 overlay feeds (NaviCube, axis cross, HUD
    text, rubberband...) via `Renderer::setOverlay`.
15. **Present** — standalone tier only: copy to the default backbuffer.

Determinism: every time-animated effect (water waves, caustics, fire,
volumetric jitter, user shaders via `u_fcTime`) reads one shared
animation clock. `RenderDebug_FreezeFrame` pins it to 0 and suppresses
self-scheduled redraws; two frozen frames are byte-identical.

## 4. Draw model

- `Render::DrawCall` = mesh reference (+ index sub-range), model
  matrix, bbox, object/selection keys, `Render::Material`.
- `Render::Material` carries the full shading state (diffuse/emissive/
  specular, textures, line/point style, depth func, clip planes,
  effect flags like water/glass/ontop, and `usershader`). Its
  `operator<` is the draw-batching key.
- **Instancing**: draws sharing geometry and compatible state collapse
  into per-instance-buffer submissions (leaf-level global table,
  color-variant branching — see `docs/TShapeRenderCache.md`). Draws
  with a user shader are excluded from cross-object instancing.
- **Selection/highlight channels**: selection and preselection draws
  arrive as separate keyed lists; whole-object overrides can suppress
  the base draw by key. The user-shader Appearance bindings reuse this
  channel (negative ids = normal-pass rendering with base
  suppression).

## 5. User shader framework

User-loadable shaders are document objects riding the standard
parametric loop. Full design history in `docs/RenderDebug.md` §6; this
section is the *authoring reference*.

### 5.1 Object model

- **`App::ShaderProgram`** — one program for one pipeline stage.
  Properties: `Stage` ("material", "post"), `Dialect` (BGFX_SC /
  GLSL), `VertexProgram`, `FragmentProgram` (source text), `Blend`
  (Default / Alpha / Additive), `DepthWrite`, plus user-added
  `Param_<Name>` dynamic properties (§5.5).
- **`App::Shader`** — an effect: a list of ShaderProgram objects (one
  per stage a multi-program effect needs). Carries the demo-preview
  shape (`Demo` = None/Box/Sphere/Cylinder/Cone/**Emitter**, §5.8) —
  the preview renders the effect applied to the shape; inert
  otherwise.
- **`App::Appearance`** — the binder (a LinkGroup): child 0 resolves
  to the Shader (possibly an `App::Link` into a shader-library
  document), remaining children are targets. `Scope` selects
  application: **Object** (attach at the target's view-provider root —
  all instances), **Instance** (suffix-anchored occurrence chains
  through assemblies/links), **Element** (a `Face3`-style subname tail
  shades one face). An Appearance with *no* targets activates its
  post-stage programs scene-wide. Ties on the same target break by
  `TreeRank`. Note: targets placed directly in the group are claimed
  children — hiding the Appearance hides them too; bind through an
  `App::Link` child (or delete the Appearance) when that matters.

Coin transport: the program materializes as a shared `SoShaderProgram`
node (fork extensions: `SoSFName stage`, `SourceType BGFX_SC`);
capture routes a `material`-stage program into the enclosing render
cache (`Material::usershader`), `post`-stage programs into the
scene-level list.

### 5.2 Stages and what they replace

| Stage | Replaces | Passes affected | Untouched |
|---|---|---|---|
| `material` | the mesh program of the draw | beauty only: `ViewOpaque`, non-OIT `ViewTransparent`, `ViewGroundRefl` | depth prepass, shadow casting, picking, highlight/on-top, WBOIT, section clip |
| `post` | — (inserted) | `ViewUserPostCopy` + `ViewUserPost`, after bloom, before debug/on-top | everything else |

While a compile is pending or failed, the stock program stands in —
never a black object. A broken shader reports once to the console and
renders stock.

### 5.3 Authoring contracts

Dialect is bgfx `.sc` (shaderc), compiled against the shipped include
tree (`assets/shaders/src`: `bgfx_shader.sh`, `varying.def.sc`, the
`fc_*.sh` helpers).

**Material fragment program**

```glsl
$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main() { gl_FragColor = vec4(...); }
```

Varyings: `v_normal` (view-space normal), `v_color0` (per-vertex /
instance color), `v_vpos` (view-space position). One color output.

**Material vertex program** (optional — omit to keep the stock
`vs_fc_mesh`)

```glsl
$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
uniform vec4 u_params;
void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    gl_Position.z += u_params.w * gl_Position.w;   // depth-bias parity
    v_normal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
    v_color0 = a_color0;
    v_vpos = mul(u_modelView, vec4(a_position, 1.0)).xyz;
}
```

The output list must be exactly the fragment stage's input list.
Because only the beauty draw uses the user program, a displacing VS
leaves the stock passes at the *undisplaced* geometry: wireframe/edge
lines stay put, shadows and picking use the original shape, and Coin's
auto near/far planes fit the undisplaced bounding box (large
displacements clip). Acceptable for particles/effects; not a general
deformation mechanism.

**Post fragment program**

```glsl
$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_texScene, 0);          // the composited scene color
void main() { gl_FragColor = texture2D(s_texScene, v_texcoord0); }
```

The vertex stage is the stock fullscreen triangle (`vs_fc_comp`);
supplying a custom VS is allowed but rarely useful.

### 5.4 Public uniforms and samplers

bgfx uniforms and samplers are **global by name**: declaring one of
these in a user program picks up the value/texture the engine records
with the consuming draw. Three stability classes:

**Contractual (stable, designed for user shaders):**

| Name | Type | Meaning |
|---|---|---|
| `u_fcTime` | vec4 | `.x` seconds on the shared animation clock (0 under freeze-frame), `.y` 1 while the clock advances, `.zw` reserved. Referencing it makes the shader *animated*: the viewer keeps scheduling frames while it is on screen. |
| `u_<Name>` | vec4[n] | one per `Param_<Name>` dynamic property (§5.5) |

**bgfx predefined (stable by bgfx contract):** `u_model[]`,
`u_modelView`, `u_modelViewProj`, `u_view`, `u_invView`, `u_proj`,
`u_invProj`, `u_viewProj`, `u_invViewProj`, `u_viewRect`,
`u_viewTexel`, `u_alphaRef4`.

**Engine-recorded (usable, semi-stable — they are the stock mesh
shader's inputs and evolve with it; prefer the §5.6 helper API which
absorbs changes):**

| Name | Type | Meaning |
|---|---|---|
| `u_matColor` | vec4 | material diffuse rgba (used when `u_params.x == 0`) |
| `u_matEmissive` | vec4 | emissive rgb add |
| `u_matSpecular` | vec4 | specular rgb, `.w` shininess (Coin 0..1) |
| `u_params` | vec4 | `.x` per-vertex color, `.y` lighting on, `.z` two-sided, `.w` NDC depth bias (polygon-offset emulation) |
| `u_pbrParams` | vec4 | `.x` PBR branch (2 = with metallic-roughness map), `.y` metallic, `.z` roughness, `.w` environment intensity |
| `u_envSH[9]` | vec4 | irradiance SH of the environment (Ramamoorthi form, world space) |
| `u_lightDir` | vec4 | scene light direction, view space; `.w > 0.5` = shadowed scene light active (else headlight) |
| `u_lightPos` | vec4 | spot light position, view space; `.w` = cos cone cutoff, −1 directional |
| `u_lightColor` | vec4 | light rgb × intensity; `.w` spot falloff exponent |
| `u_shadowParams` | vec4 | `.x` draw receives shadows, `.y` min variance, `.z` depth bias |
| `u_shadowMatrix` | mat4 | view space → shadow-map uv + light depth |
| `u_evsm` | vec4 | `.x` EVSM warp exponent, `.y` VsmLookup threshold, `.zw` spread kernel (tap spacing, mode) |
| `u_localLight[8]` / `u_localLightColor[8]` | vec4 | effect point lights (fire flames + Render_Light bulbs): pos.xyz view space, `.w` 1/range²; color × intensity, `.w` shadowed-bulb flag |
| `u_bulbShadowConf/Mtx/Rot` | — | bulb shadow atlas tiles (see `fc_mesh_lighting.sh`) |
| `u_texMatrix` | mat4 | GL texture matrix (textured draws) |
| `u_instParams` | vec4 | instanced path: `.x` selects per-vertex color stream |

Samplers recorded with mesh draws (slot in parentheses): `s_texColor`
(0), `s_texEnv` (cube, 1), `s_texBump` (2), `s_texShadow` (3),
`s_texEmissive` (4), `s_texOcclusion` (5), `s_texMetallicRoughness`
(6), `s_texShadowTint` (7), `s_texBulbShadow` (8), `s_texAOScreen`
(9). Post stage: `s_texScene` (0) = the composited scene color copy.

Everything else in `assets/shaders/src` (`u_volParams`, water/fire
slot arrays, debug uniforms, ...) is **engine-internal** — it may work
today and change without notice.

Reserved names: parameters materialize as `u_<Name>`; `fc_state`
(§5.7) is consumed by the engine and never becomes a uniform contract.
GLSL ES portability rule: never end an identifier with `_` — the essl
cross-compile suffixes locals with `_NN`, and consecutive underscores
are reserved in GLSL ES (a desktop-GL build won't notice; strict
WebGL rejects the shader and the viewer treats that as fatal).

### 5.5 Parameters — the parametric loop into shading

A dynamic property `Param_<Name>` on an `App::ShaderProgram` becomes
`uniform vec4 u_<Name>` (arrays for list properties). Supported
property types and packing: Bool/Integer/Enumeration/Float → `.x`;
Color → rgba; Vector → xyz; FloatList/IntegerList → vec4 lanes
(padded). Editing the property updates the uniform live. Only the
`Param` group binds — other dynamic properties stay ordinary
properties.

An `App::Appearance` overrides parameters *per binding* with
like-named `Param_*` properties of its own; an override the program
does not declare is appended (a binding can drive any uniform the
source declares).

Uniform hygiene is engine-managed: parameter values are recorded with
the consuming draw, and every dynamically bound uniform not in the
draw's list is zeroed for that draw (bgfx uniform state persists
across frames; a removed parameter must not keep feeding its stale
value).

### 5.6 Lighting helper API

`#include "fc_user_lighting.sh"` in a material fragment program:

- `fcStockBase()` → the draw's stock base color (material diffuse or
  per-vertex color).
- `fcLightFragment(base, v_normal, v_vpos)` → `base` shaded exactly
  like the stock renderer: headlight or shadowed scene light,
  Render_Light bulbs with their shadow tiles, fire lights, screen-space
  AO, the PBR branch when active. The identity form
  `fcLightFragment(fcStockBase(), v_normal, v_vpos)` reproduces stock
  rendering.
- `fcLightFragmentAt(base, normal, vpos, fragCoord)` — explicit
  fragCoord variant (the macro exists because shaderc's spirv path
  resolves `gl_FragCoord` only inside `main()`).
- `fcShadeFragment(base, n, geoN, vpos, fragCoord, occ, metal, rough)`
  (`fc_mesh_lighting.sh`) — the raw core for callers that perturb the
  normal or bring their own occlusion/PBR factors.

### 5.7 Render state

`App::ShaderProgram.Blend` (Default / Alpha / Additive) and
`.DepthWrite` override the beauty draw's state. Transport is a
reserved shader parameter (`fc_state`) on the same channel as `Param_*`
— it reaches Appearance clones and the snapshot with no extra
plumbing; the backend consumes it at submit by re-recording the bgfx
state. A blended override stays in its stock draw bucket (an additive
opaque object still draws in `ViewOpaque`) — cross-object ordering
against real transparency is the effect author's concern.

### 5.8 Particles (stateless)

Position-as-`f(seed, t)` in a user vertex shader — no engine-side
simulation, portable to every tier. The pieces:

- **Seed geometry**: `App::Shader Demo="Emitter"` generates
  `EmitterCount` degenerate quads (4 coincident vertices at a random
  anchor in the `DemoSize` box, deterministic per `EmitterSeed`).
  Zero area = invisible under stock rendering. Encoding:
  `a_position` = anchor, `a_normal.xy` = corner (±1), `a_normal.z` =
  particle index 0..1, `a_color0` = per-particle random seed.
- **Motion**: the VS billboards and animates from `u_fcTime` +
  `Param_*` uniforms.
- **Look**: `Blend="Additive"`, `DepthWrite=false`, a fading
  `v_color0`.

Reference VS (from `scripts/user_shader_particles.py`):

```glsl
$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
uniform vec4 u_fcTime;
void main()
{
    vec2 corner = a_normal.xy;
    float life = fract(u_fcTime.x * 0.35 + a_color0.x + a_normal.z);
    vec3 base = a_position;
    base.z += life * 6.0;                       // rise
    vec4 vpos = mul(u_modelView, vec4(base, 1.0));
    vpos.xy += corner * 0.35;                   // billboard
    gl_Position = mul(u_proj, vpos);
    v_normal = vec3(0.0, 0.0, 1.0);
    v_color0 = vec4(1.0, 0.55, 0.15, 1.0) * (1.0 - life);
    v_vpos = vpos.xyz;
}
```

Emitter meshes are built from explicit
`SoCoordinate3`/`SoNormal`/`SoMaterial` nodes — the render cache does
not capture `SoVertexProperty`-fed shapes.

### 5.9 Compilation and caching

- **Desktop**: user source compiles through the host `shaderc`
  **asynchronously** on the event loop (never in the paint path); the
  linked program is consulted per frame and the stock program stands
  in until ready. Disk cache keyed
  SHA1(source × platform × profile × include-tree content hash) under
  `CacheLocation/BGFXUserShaders` — a shipped-helper edit invalidates
  stale bins; compiles write a sidecar and rename into place so a
  partial file is never trusted.
- **Browser tier**: no compiler in the page. The backend
  cross-compiles the viewer targets (essl) through the same cache and
  ships the binaries in the snapshot's user-shader table; the viewer
  loads the profile-matched variant, falls back to stock until a
  republished snapshot carries it.
- **Stock shaders** compile at build time (`ninja Renderer_assets`;
  the WASM build compiles its own essl pack — note both shader lists
  and the include list are configure-time GLOBs: adding a `.sh`/`.sc`
  file needs a cmake re-run in *each* build tree).
- Hot-reload for development: `FC_BGFX_SHADER_DIR` + `view.reloadShaders()`.

### 5.10 Verification

`scripts/user-shader-verify.sh desktop|viewer|all <outdir>` runs the
whole suite matrix (params, post, instancing, element, lighting,
motion, particles; browser legs via headless Chromium against a
serving backend). The golden-image harness
(`scripts/render-verify.sh`) restages captures byte-exact under
freeze-frame. Policy: every framework change lands with a suite, no
throwaway probes (`docs/RenderDebug.md` §0).

## 6. Render debugging facilities

Full design in `docs/RenderDebug.md`; the short tour. Governing policy
(§0 there): **no throwaway probes** — every debugging aid is a
reusable, runtime-toggleable feature (a property or Python API, never
a `#define` or working-tree patch), because the hardest artifacts live
on targets that cannot be recompiled in place (a streamed WASM page, a
phone).

- **`RenderDebug_*` view properties** — hidden dynamic properties on
  the 3D view (property editor "Show all" reveals them; scripts:
  `view.addProperty / removeProperty`, group derived from the name
  prefix). They ride the same config spine as the `Render_*`/`Shadow_*`
  overrides.
  - `RenderDebug_ViewMode` — one enum, one uniform lane, switched in
    the composite shader: 0 Off, 1 Depth, 2 Normal, 3 AO, 4 Shadow,
    5 ShadowTile (per-bulb atlas-tile hue), 6 Overdraw (additive heat
    through the `ViewDebugScene` re-render), 7 ShadowFilter (HW vs SW
    moment-filter precision probe — silent on software rasterizers,
    lights up on real-GPU fixed-point filtering: the stipple-class
    detector), 8 UV. Per-stage visualization makes a pixel diff
    *localizable*: the first divergent stage names the culprit.
  - `RenderDebug_FreezeFrame` — pins the shared animation clock
    (§3) and every temporal effect; the determinism switch behind all
    byte-exact golden comparisons, including frozen `u_fcTime` user
    shaders (§5.4).
  - `RenderDebug_Label` — burns mode/freeze/parameter text into the
    frame via the overlay text path, so a capture is self-describing.
  - Any other `RenderDebug_<Name>` property binds dynamically to a
    `u_<Name>` uniform — the same name-bound mechanism as `Param_*`
    (§5.5), usable to drive experimental shader lanes at runtime;
    `u_userParams[4]` survives as the bootstrap pool for no-compiler
    tiers.
- **Frame capture** — `view.saveRenderDump(path, source=, mode=,
  metadata=)`: `source='renderer'` reads back the renderer's FBO,
  `'framebuffer'` the composited Qt framebuffer, `'viewer'` pushes a
  `dumpFrame` over the WebSocket control channel to every connected
  WASM viewer — an in-page `glReadPixels` on the *real device GPU*
  (works on a phone, no puppeteer needed) uploaded back and written
  locally, returning the path list. Every capture writes a sidecar
  JSON (camera, viewport, backend, build, stats, all
  `Render_*/RenderDebug_*/Shadow_*/HiddenLine_*` props) — enough to
  restage the frame exactly. `view.getRenderStats()` exposes the
  per-frame counters.
- **Golden-image harness** — `scripts/render-verify.sh capture|diff`:
  captures named cameras × debug modes under freeze-frame (isolated
  XDG dirs under xvfb by default, `--gpu` for the real-GPU desktop
  leg, `--viewer` for the browser leg), restages a golden's camera and
  full property set from its sidecar, and diffs per stage in pipeline
  order (depth → normal → ao → shadow → beauty) with heatmaps — the
  cross-tier verification loop that closes the "renders differently on
  the phone" class of bug. The user-shader suites (§5.10) build on the
  same drivers.
- **Shader hot-reload** — `FC_BGFX_SHADER_DIR` (alternate compiled
  asset root) + `view.reloadShaders()`; user-shader sources recompile
  on edit through the §5.9 cache automatically.
- **MCP debug console** — the serving stack (`renderer-serve.sh`)
  exposes `run_python`/`search_api` over MCP with captured console
  output, so an agent can drive a live session end-to-end: set a
  `RenderDebug_*` prop, `saveRenderDump(source='viewer')`, read the
  PNG.

## 7. Known limitations / future work

- Stock passes keep stock programs: a material-stage override does not
  affect shadows, picking, AO or section clipping; a displacing VS
  shows the artifacts listed in §5.3. An emitter-bounds story (bbox
  padding for displaced geometry) comes with the particle framework's
  next phase.
- Stages are currently `material` and `post`. Splitting the built-in
  water/fire/fountain effects into pre-bundled user shaders will need
  additional stages (water's reflection/refraction passes) and a
  packaging/discovery story for a shipped effect library — design
  pending.
- WBOIT draws cannot take a user fragment program (the OIT output
  contract is not a single color).
- Per-draw texture slots for user shaders (custom images) are not yet
  bindable from the document model.
