# Render Debugging & Verification Architecture

Status: phases **1–4 implemented** (RenderDebug view properties + bgfx
buffer visualization/freeze-frame; `saveRenderDump`/`getRenderStats` +
sidecar JSON; browser `dumpFrame`/`reload` control channel; the
verification harness under `scripts/`; dynamic named uniform binding +
`u_userParams` pool + shader hot-reload). Phases 5+ are design.

This document defines (1) the governing policy for render-debugging code in this
repo, (2) the debug-parameter protocol that rides the existing render-property
plumbing, (3) the frame-capture Python API, and (4) how all of this is designed
as the first constrained slice of an eventual **user-loadable shader** feature.

Companion docs: `docs/RoadMap.md` (workstreams), `docs/TShapeRenderCache.md`
(renderer instancing), `docs/ThinClient.md` (browser control channel — the
same channel these debug knobs travel over).

---

## 1. Governing policy

The bgfx renderer runs on three targets — desktop GPU, streamed WASM/browser,
mobile browser — and its hardest bugs are target-specific (fp16 filtering,
missing WebGL extensions, driver-dependent precision). A debug probe that only
exists as a local patch on one target is nearly worthless. Hence:

1. **Debug output is a feature, not a patch.** No probe lands as
   working-tree-only code. If it is worth writing, it is worth a name, a
   runtime toggle, and a commit.
2. **Runtime toggle over recompile.** Every debug knob is a property or
   parameter, settable live. A `#define` cannot be flipped on a phone or in a
   streamed WASM session; a `RenderDebug_*` view property can, over the same
   channels the render properties already travel.
3. **Generalize the mechanism, not the instance.** Do not add
   "tint shadow cascade N red". Add "bind a named parameter to a shader lane",
   after which the cascade tint is a property value, not a C++ change.
4. **Capture through an API, never ad hoc.** Before writing any
   dump/readback code, look for an existing Python API. If none fits, add one
   (section 4) — the API outlives the bug.
5. **One knob surface, every backend.** A debug knob is defined once, on the
   shared config feed (section 2), so desktop and WASM honor the *same* knob.
   This is a hard requirement: cross-target pixel comparison is only
   meaningful when both legs were staged by identical, scriptable state.

Corollary for reviews: a PR that adds `getenv`-gated or `#if 0` render-debug
code should be reworked to a `RenderDebug_*` parameter unless there is a concrete
reason (e.g. code that must run before any parameter system exists).
Existing env gates (`FC_BGFX_DEBUG_READBACK`, `FC_BGFX_DEBUG_DUMP_FRAME`) are
grandfathered and get absorbed by section 4.

---

## 2. The debug-parameter protocol

### 2.1 The existing spine (already implemented)

Runtime render settings already flow through one pipeline:

```
Preferences/View/Render key            (global default, RenderParams — cog-generated)
        │  per-view override
        ▼
<Group>_<Name> dynamic property on the View3DInventor object
        │  viewPropOverride/viewParamOverride    (SoFCRendererBridge.cpp)
        ▼
RendererBridge::translate<Group>Config  → Render::<Group>Config struct
        │  per-frame                            (SoFCRenderer.cpp ~2287)
        ▼
Renderer::set<Group>Config virtual      → BGFXRenderer marks dirty
        │  frame packing
        ▼
bgfx::setUniform(u_*)                   → uniform vec4 u_* in the .sc shader
```

Two properties of this spine make it the right home for debug knobs:

- **The naming protocol is already in place.** `viewPropOverride` looks up
  properties by `"%s_%s" (group, name)`, and since f491c40a75,
  `ViewProviderGeometryObject::addDynamicProperty` derives the property-editor
  group from the name's prefix before the first `_`. So a dynamic property
  named `RenderDebug_ViewMode` automatically lands in group "RenderDebug" and is found by
  the bridge with no registration step. (Note: the prefix must be non-empty —
  a *leading* underscore is excluded — so the convention is `RenderDebug_Foo`,
  never `_RenderDebug_Foo`.)
- **Both backends consume the same feed.** The desktop GL and WASM builds run
  the same `SoFCRenderer` → `set*Config` path, so a knob works on every
  target the moment it exists, including over the thin-client control channel.

### 2.2 What we add

A `RenderDebug` group, structurally identical to the existing `AO`/`Water`/`Bloom`
groups:

- **No special UI.** Debug knobs are dynamic properties on the view object in
  the existing property editor, flagged `Hidden` so they only appear under
  "Show all" (or when added explicitly from Python). Global defaults live as
  plain keys in `Preferences/View/Render` (`RenderParams.py` cog source),
  reachable via Edit Parameters — no preference page, no task panel.
- `Render::RenderDebugConfig` struct in `Renderer.h` + `Renderer::setRenderDebugConfig()`
  virtual (default no-op, so the Diligent backend and any future backend
  compile unchanged).
- `RendererBridge::translateRenderDebugConfig()` reading each field through
  `viewParamOverride(view, "RenderDebug", ...)`.
- One `u_debugParams` (vec4) uniform in the bgfx backend, packed from the
  config each frame, declared in the shaders that participate (initially the
  final composite pass).

Setting a knob from Python is then one line, live, on any target:

```python
view = Gui.ActiveDocument.ActiveView
view.addProperty("App::PropertyInteger", "RenderDebug_ViewMode")  # group auto-derived
view.RenderDebug_ViewMode = 2   # world-space normals
```

(`MDIView.addProperty`/`removeProperty` — available on every MDI view,
mirroring the DocumentObject API; the editor group is derived from the
name prefix before the first `_`.)

### 2.3 `RenderDebug_ViewMode` — buffer visualization

The workhorse knob: a single integer that routes an intermediate render target
or derived quantity to the screen, instead of N per-artifact tint hacks. This
is the "view mode" dropdown every production engine ships.

| value | name | shows | diagnoses |
|---|---|---|---|
| 0 | Off | off (normal shading) | — |
| 1 | Depth | linearized depth | prepass, precision, far-plane issues |
| 2 | Normal | view-space normals | tessellation/normal-generation bugs |
| 3 | AO | AO term only | GTAO artifacts, resolution-scaling seams |
| 4 | Shadow | shadow term only | acne/peter-panning, EVSM bleeding |
| 5 | ShadowTile | scene-shadow-map coverage (gray) + bulb atlas tile index as color | shadow projection reach, bulb-tile coverage/selection |
| 6 | Overdraw | overdraw heatmap (dedicated counting re-render, additive, depth test off) | transparency sorting, instancing regressions |
| 7 | ShadowFilter | shadow-moment filtering-precision probe: hardware bilinear vs the same four texels blended at full shader precision, amplified; B = the variance term | filtering-precision issues (exactly the RG16F stipple class) |
| 8 | UV | UV / texcoord of the visible surface (depth-tested re-render) | mapping bugs |

Implementation shape: the mode rides `u_debugParams.x`; the final composite
shader (`fs_fc_debug.sc`) ends in a mode `switch` that samples the relevant
intermediate target. Modes 1–5/7 are pure routing over targets that already
exist for the effect passes (modes 4/5/7 reconstruct the view-space position
from the prepass depth, so they force the prepass on like 1–3). Modes 6/8
share a dedicated *debug scene re-render* pass (`ViewDebugScene`, repurposing
the retired AO-apply view slot): every main-pass triangle fill re-rasterizes
into a full-res RGBA16F target — additive with the depth test off for the
fragment count, depth-tested texcoord output for UV. The enum is append-only.

Mode-specific tuning rides the `u_userParams[0]` bootstrap lane: `.z`
overrides the overdraw full-red count (default 8) and the mode-7 probe
amplification (default 4096); `.x/.y` stay the generic output scale/bias.
(This renderer has a single scene shadow map — no cascades — so mode 5's
"cascade index" reduces to scene-map coverage plus the 4x4 bulb tile atlas.)

**Why this matters for verification:** a golden-image diff of the final frame
says "something changed". A diff of mode 2 vs mode 3 vs mode 4 *localizes* the
regression to a pipeline stage. The verification harness (section 5) captures
a small set of modes per scripted camera, not just the beauty shot.

### 2.4 `RenderDebug_FreezeFrame` — determinism switch

A boolean that disables every intentionally non-deterministic input:
temporal jitter/accumulation (AO frame cache), time-driven animation (water,
fire), and any per-frame random sampling. Without it, golden-image comparison
flakes and every pixel-diff threshold becomes a negotiation. With it, two
frames of the same scene+camera+params are bit-stable per backend.

This is also the knob the capture API (section 4) sets implicitly when asked
for a "verification capture".

### 2.5 Generic named parameters — dynamic, not pre-declared

Debug shading frequently needs a couple of tweakable values (a bias to
sweep, a threshold to bisect, a channel selector). These are **not**
restricted to a pre-declared set, because nothing in the stack requires it:

- `bgfx::createUniform(name, type)` is fully dynamic at runtime — uniforms
  are resolved by name. The only hard rule is that a *compiled* shader
  binary has exactly the uniforms its source declared; you cannot inject a
  new uniform into an existing `.bin`. Since shaders are compiled on demand
  (§3 hot-reload, §6.3 compile cache), declaring a uniform is just an edit
  to the shader source.
- The C++ side is data-driven, not per-uniform code (implemented): the
  bridge enumerates every `RenderDebug_*` view property beyond the fixed
  knobs into `RenderDebugConfig::userParams` — the uniform name is
  `"u_" + <Name>` (or `<Name>` verbatim when it already starts with
  `u_`), so `RenderDebug_myKnob` feeds `uniform vec4 u_myKnob`.
  Supported property types: Bool/Integer/Enumeration/Float → the x
  lane; Color → rgba; Vector → xyz; Float/IntegerList → consecutive
  lanes, zero-padded to vec4 arrays. Unsupported types warn once and
  are skipped. The backend resolves names to handles lazily
  (`setUserUniform`) and pushes the values against the debug-pass draw.
  Adding a knob is therefore: declare `uniform vec4 u_myKnob;` in the
  shader + add a like-named property. No FreeCAD recompile, no
  registration step.
- Residual constraint (bgfx, not us): uniform types are vec4 / matrix /
  sampler — scalars pack into vec4 lanes regardless of mechanism.
- **Binding is per-draw, not per-frame** (bgfx reality, learned the hard
  way): uniform updates recorded before an *empty* submit —
  `bgfx::touch`, i.e. the view clears — are discarded together with the
  dropped draw, so a frame-start "set everything once" push silently
  never reaches the GPU. Values must be set against a real draw that
  precedes (or is) their consumer. The stock push therefore lives with
  the debug-pass submit; a user-shader stage (§6) sets its parameters
  when binding its own pass.

**Bootstrap fallback (implemented).** One regime cannot compile on
demand: a target running stock precompiled binaries with no compile
service (the browser tier until §6.3's server-side compile lands). For
that case only, the stock debug-capable shaders reserve a small
`uniform vec4 u_userParams[4]` pool that properties map onto by lane —
a `RenderDebug_userParams` float-list property fills it (16 lanes),
riding the same dynamic binding. The debug composite shader applies
lane 0 as an output transform (`x` = scale, `y` = bias, backend
default 1/0), which amplifies subtle differences in captured debug
buffers. The pool value travels in the scene snapshot (v21), so a
stock WASM viewer honors it. It is a compatibility floor, not the
architecture — once every tier can reach a compiler, the pool is just
another set of named uniforms.

This dynamic binding is exactly how the user-shader feature (§6) binds
property-driven values too, which is why it lands under the protocol and
not as a debug-only hack.

---

## 3. Shader hot-reload (developer loop)

Editing a `.sc` shader used to mean recompile + restart. Implemented:

- **Shaders are distributed by source**: the repository carries only
  `.sc`/`.sh`; every `.bin` is a build artifact. The desktop build
  compiles all profiles with the in-tree `shaderc`
  (`BGFXShaders.cmake` → `ninja Renderer_assets`, incremental per
  shader) into the build tree's resource path; the WASM viewer build
  compiles+packs its own essl set with a host `shaderc`
  (`FCVIEWER_SHADERC`). No committed binaries, no stale-copy step.
- `FC_BGFX_SHADER_DIR=<dir>` points the shader loads at an alternate
  asset root (a directory containing `shaders/{glsl,essl,spirv}/`) —
  e.g. `bgfx/shaders/compile.sh`'s output (default
  `build/shaders-dev`), for A/B shader experiments that must not touch
  the build tree.
- `View3DInventor.reloadShaders()` reloads every program from disk on
  the next rendered frame (a shader-generation bump forces the view
  re-init that already owns program lifetime). The loop is:
  edit `.sc` → `ninja Renderer_assets` (or `compile.sh` into the
  override root) → `view.reloadShaders()` — no restart, verified
  byte-exact reversible.
- Desktop-only for now (shaderc is a host tool); the WASM story arrives
  with the user-shader feature's server-side compile step (section 6.3).
  A file watcher on the override dir could remove the explicit reload
  call later. True runtime JIT of the built-ins (ship source + shaderc
  in the distribution, compile on demand through the §6.3 cache) is a
  phase-6 option on top of this — the build-time compile stays as the
  fallback and the commit-time error gate either way.

This converts "hack the shader to print a color" from a rebuild cycle into an
edit-save-see loop, while keeping the shader *source* the artifact — which is
also the groundwork for loading user shader source at runtime.

---

## 4. Frame capture: a real Python API

### 4.1 What exists

- `View3DInventor.saveImage(...)` → `View3DInventorViewer::savePicture()`.
  When an external renderer is active it already forces the
  framebuffer-capture path, so the composited (Coin + bgfx) frame is
  capturable from Python today.
- `FC_BGFX_DEBUG_READBACK` / `FC_BGFX_DEBUG_DUMP_FRAME=<path>`
  (BGFXRenderer.cpp ~5704): a true desktop-GL readback of the bgfx color FBO
  (pre-Coin-composite), written as a PPM, overwritten every frame, env-gated.
  This was built during the stipple hunt precisely because streamed-viewer
  screenshots render on SwiftShader and cannot show real-GPU artifacts.

The env-gated dump is exactly the capability the verification harness needs,
in exactly the wrong form (no scripting, no format choice, no metadata,
always-on-per-frame).

### 4.2 What we add: `saveRenderDump`

```python
view.saveRenderDump(path,
                    source="renderer",    # "renderer" = bgfx FBO readback (pre-composite)
                                          # "framebuffer" = composited frame (savePicture path)
                    mode=None,            # optional RenderDebug_ViewMode for this capture only
                    metadata=True)        # write sidecar JSON
```

- One-shot: arms a capture that executes on the next rendered frame, then
  disarms. No env vars, no per-frame overwrite.
- `source="renderer"` reuses the existing readback code path, promoted from
  env-gated static to a renderer-level `requestFrameDump(path, mode)` API;
  PNG via Qt's imagewriter instead of hand-rolled PPM.
- `mode=` temporarily overrides `RenderDebug_ViewMode` for the captured frame, so a
  harness can sweep buffer visualizations without touching view state.
- **Sidecar metadata** (`<path>.json`): camera (position/orientation/type/
  scale), viewport size, backend type + renderer caps line, MSAA samples,
  git describe, and the full set of active `Render_*`/`RenderDebug_*` values.
  A capture is thereby *reproducible*: the harness (or a human) can re-stage
  the exact frame from the sidecar alone. "I saw stipple once" becomes a
  checked-in test case.

`FC_BGFX_DEBUG_READBACK`'s stats (geometry-pixel count, average color) get the
same treatment as a `getRenderStats()` Python call — cheap numeric
assertions ("the scene is not black", "N pixels covered") are often enough
for smoke tests and are far more robust than pixel diffs.

### 4.3 Self-labeling captures

An optional burn-in of the active view-mode name + key parameter values into
a screen corner, so a PNG in a bug report is self-describing even without its
sidecar. Implemented as the Hidden `RenderDebug_Label` boolean (default off —
goldens stay label-free unless asked): while on, the viewer feeds an
orange monospace text quad (the fps-readout machinery, one percent in from
the top-left) through the standard overlay feed — the WASM viewer therefore
burns the same label into its own `dumpFrame` captures. The text names the
view mode, the freeze state, and every custom `RenderDebug_*` parameter as
`name=value`.

### 4.4 Live capture from a running browser (the third leg)

The WASM viewer runs *our* code on the *real device GPU* — the one place
server-side screenshots (SwiftShader) are constitutionally blind. So the
browser leg is the same one-shot dump protocol, transported over the
existing WS control channel:

1. Harness/Python sends `{cmd: "dumpFrame", mode: …, id: …}` as a JSON text
   frame (the same control channel the thin-client design uses).
2. The viewer arms a one-shot capture: on the next frame, after bgfx
   submits, it reads back the resolved color target via `glReadPixels`
   inside WASM — real WebGL2 on the user's actual GPU, desktop browser or
   phone.
3. Pixels + metadata (canvas size, echo of active `RenderDebug_*` state,
   `WEBGL_debug_renderer_info` string, snapshot version) return as a binary
   WS frame tagged with the request `id`; the receiving side writes the same
   PNG + sidecar JSON format as the desktop leg.

Consequences of this design:

- **No puppeteer required for capture.** It works against a phone
  mid-session — something no browser-automation screenshot can do.
  Puppeteer remains the *driver* in CI (open page, navigate); pixels come
  from the in-page readback, not `page.screenshot()`, which captures
  OS-composited output and can diverge from the GL result.
- **Phone convenience without the harness:** a HUD control triggering the
  same capture path and offering the image as a download — one extra
  button on an identical code path.
- `saveRenderDump` semantics become uniform across all three legs
  (desktop FBO readback / browser in-page readback / composited
  framebuffer), all landing as interchangeable PNG+JSON pairs.

**Unattended operation (agent/CI debug loop).** The capture is fully
drivable from the backend side with no human at the browser:
`saveRenderDump(path, source="viewer")` on the backend pushes `dumpFrame`
to the connected viewer(s), receives the upload, writes PNG + sidecar to
local disk, and returns the path(s). An agent (via the MCP debug console)
or a CI script therefore runs the whole loop — trigger, retrieve, inspect —
against whatever device is connected, including a phone left on the page.
If several viewers are connected, all are captured; filenames are
disambiguated by the sidecar's renderer-info string (one trigger →
per-device evidence). If no viewer is connected, the harness can launch a
local browser itself — noting that a *headless* browser renders on
SwiftShader, so it validates function/layout but not device-GPU precision;
real-GPU evidence comes from the desktop leg or a real connected device.

**Remote reload.** The same channel lets the backend refresh the viewer
itself when the served build changes:

- `{cmd: "reload", cacheBust: "<build-hash>"}` — the viewer re-navigates
  with the hash as a query parameter (not a bare `location.reload()`,
  which may reuse a stale cached `.wasm`/`.js` bundle).
- Steady state: the connect handshake carries the viewer's build/snapshot
  version; on mismatch with what the backend currently serves, the backend
  replies with `reload` automatically. Rebuild → every connected device
  (including a phone) refreshes itself on its next message, no commands
  from anyone. The viewer refuses a second reload for the same version
  string, so a bad build can't cause a reload loop.

---

## 5. The verification harness (consumer of all of the above)

The harness itself is a follow-up work item, but it is the reason for the
shape of everything above, so its contract is stated here:

- **Two legs, identical staging.** Desktop leg: real-GPU build,
  `saveRenderDump(source="renderer")`. Browser leg: streamed/WASM build, the
  same scene + camera + `RenderDebug_*` values sent over the thin-client control
  channel, pixels captured in-page via the `dumpFrame` protocol (§4.4).
- **Scripted cameras + `RenderDebug_FreezeFrame` on.** Each test scene defines a
  fixed camera list; every (scene, camera) captures the beauty shot plus a
  short list of `RenderDebug_ViewMode` buffers.
- **Diffs are per-stage.** Regressions are reported against the first
  pipeline stage whose buffer diverges, not just the final image.
- **Sidecars are the test manifest.** A stored golden is its PNG + JSON; the
  harness re-stages from the JSON, so goldens survive default-value changes.

This closes the "no reliable way to verify rendering" gap: the SwiftShader
blindspot is covered by the desktop leg being a *real-GPU readback* of the
same knob-for-knob staged frame.

---

## 6. Endgame: user-loadable shaders

The debug machinery above is deliberately the **first constrained slice** of a
user-facing feature: letting users attach their own shaders at predefined
pipeline stages, with parameters bound to document/view properties by the
same naming protocol (`<Group>_<Name>` → shader parameter).

### 6.1 Authoring model: reuse Coin's shader nodes

Coin already ships a complete, persistable, Python-visible shader data model
(`src/shaders/` in the coin fork):

- `SoShaderProgram` — container; `SoMFNode shaderObject` holding the stages.
- `SoVertexShader` / `SoFragmentShader` (subclasses of `SoShaderObject`) —
  each with `sourceType` (`GLSL_PROGRAM` inline / `FILENAME`),
  `sourceProgram` (the source text or a path), and `SoMFNode parameter`.
- `SoShaderParameter1f/2f/3f/4f/1i/…/Matrix` + array variants — **each named
  uniform is a node with a `name` string and a typed `value` field**, plus
  `SoShaderStateMatrixParameter` for auto-fed modelview/projection.

FreeCAD currently uses none of these nodes anywhere (verified: zero
references in the fork), so this is a greenfield integration with no
compatibility baggage. We adopt these nodes as the **authoring and
persistence model**: what a user builds (from Python or a future material
editor), what gets saved in the document, and what Coin's own GL path can
even render directly for a degraded preview.

### 6.2 Bridging: the SoFCRenderMaterial pattern, again

The render-cache bridge already has the exact pattern needed, proven by
`SoFCRenderMaterial` (a node carrying no Coin GL state, existing purely to be
captured for the external backend). A shader node reaches bgfx through the
same 4-step chain:

1. Register a post-callback for the node type in `SoFCRenderCacheManager`
   (mirroring the `SoFCRenderMaterial` registration).
2. Copy shader identity + parameter block into the cache `Material` struct in
   `SoFCRenderCache` (and extend material `operator<` so materials with
   different shaders don't collapse into one draw batch).
3. Carry it through `RendererBridge::translateMaterial` into
   `Render::Material`.
4. Consume it in the backend: program lookup + uniform binding.

The backend treats a user shader like it already treats `water`/`glass`
material flags — a selector plus a parameter block driving either a branch in
the program-selection tree (for `material`-stage shaders) or a dedicated
pass (for `post`-stage shaders, mirroring the water-pass pattern).

Implementation notes from the first slice (post stage):

- **Scene placement.** A scene-level (`post`) program must sit at the top
  level of the scene graph: the cache manager only re-traverses invalid
  caches, so a program nested inside a still-valid cached separator is
  pruned with its subtree and vanishes from the capture on the next
  rebuild.
- **Compile off the paint path.** The backend render runs inside the Qt
  widget repaint; spawning the compiler there (blocking `QProcess`)
  hitches the frame and re-enters Qt's repaint machinery. Compiles are
  asynchronous: `render()` only consults the bin/program caches, missing
  bins queue a compile onto the event loop, and a finished compile bumps
  a generation that re-dirties the scene for idle-skip clients.
- **pivy ABI.** Adding fields to Coin node classes (the `stage` field)
  changes their object size — a pivy built against older Coin headers
  then heap-overflows on `coin.SoShaderProgram()` (the allocation size is
  baked into the wrapper). Rebuild pivy whenever the coin fork changes a
  node's layout; the pivy-feedstock needs the same bump for distribution
  images.

Implementation notes from the second slice (`material` stage):

- **Per-object attachment = the SoFCRenderMaterial placement rules.** A
  `stage="material"` program is routed by the cache-manager callback into
  the *enclosing* render cache (`SoFCRenderCache::setUserShader`) instead
  of the scene-level list: it applies to the shapes captured after it in
  the same cache (insert it like `ViewProviderGeometryObject` inserts
  `SoFCRenderMaterial` — child 0 of the view provider root) and, like the
  water/glass flags, does **not** merge from a parent cache into child
  caches. The translated program rides the cache `Material` as a
  `shared_ptr<Render::UserShader>` whose pointer identity keys draw
  batching (`operator<`) — a node edit invalidates the cache and
  re-translates.
- **Fragment stage only replaces the beauty shading.** The user program
  substitutes for the mesh program in the scene beauty passes (opaque /
  sorted-transparent / ground reflection) with the stock `vs_fc_mesh`
  vertex stage when the program carries none (fragment contract: `$input
  v_normal, v_color0, v_vpos`). The depth prepass, shadow casters,
  picking, highlight/on-top and WBOIT draws keep the stock programs (the
  user contract is one color output), section-clip discard is not applied
  to user programs, and a draw with a user shader is excluded from the
  cross-object instancing path. While the async compile is pending or
  failed the standard program stands in — never a black object.

### 6.3 Compilation reality

The bgfx backend consumes **precompiled per-API binaries** (`.sc` → `shaderc`
→ `.bin` loaded from `assets/shaders/{glsl,essl,spirv,metal}/`); there is no
runtime GLSL compiler. Coin's nodes carry *source*. Reconciliation:

- The Coin node is the source of truth; the backend maintains a
  **compile cache keyed on source hash × backend API**, invoking `shaderc`
  on demand. Section 3's hot-reload is the prototype of exactly this.
- **Dialects are extensible, not singular.** We own the Coin fork, and
  `SoShaderObject::sourceType` was designed as a multi-dialect enum
  (ARB/Cg/GLSL) — so we extend it (e.g. `BGFX_SC`) rather than inventing a
  parallel description. But a future backend may demand yet another dialect,
  so the node model must carry **multiple tagged source variants** of one
  logical shader: `sourceType`/`sourceProgram` stay as-is for the
  single-source case, and the program/object node gains the ability to hold
  alternates keyed by dialect. The bridge hands all variants + tags to the
  backend; each backend picks the dialect it can consume (compiling or
  transpiling as it knows how). bgfx's `.sc` is the first added dialect;
  plain GLSL remains valid and lets Coin's own GL path preview
  fragment-only `post` shaders.
- Browser tier: `shaderc` is a host tool, so the scene server compiles and
  ships binaries to the WASM viewer over the existing snapshot/asset
  protocol — same shape as every other asset the viewer already receives.

### 6.4 Design constraints (fixed)

- **Property binding via the protocol.** An `SoShaderParameter` named
  `Foo_Bar` binds to the like-named dynamic property; the dynamic
  name→uniform binding from section 2.5 is the transport. Editing the property updates
  the uniform live — the parametric loop reaching into shading.
- **Predefined stages, not arbitrary hooks.** Users pick named attachment
  points: `post` (full-screen pass over composited color — simplest, first),
  `material` (replace surface shading for an object), later possibly
  effect overrides (water/ground). Each stage documents its available
  inputs (samplers/targets) and a fixed output contract.
- **The stage is declared on the node.** `SoShaderProgram` (Coin fork) gains
  an `SoSFName stage` field (default `"material"`). A name, not an enum:
  stage names are renderer-pipeline-specific and will grow, and a name field
  needs no Coin recompile per new stage. `SoSFName` rather than
  `SoSFString` because `SbName` interns strings in Coin's permanent name
  hash — identical strings share one address, so the per-traversal stage
  dispatch is a pointer compare against pre-interned stage names, and it is
  the idiomatic Coin type for identifiers. The bridge passes the tag
  through; the backend maps known names and warns-and-skips unknown ones.
  Coin's own GL path ignores the field — it has no such pipeline stages,
  which is the correct degradation.
- **Sandboxed failure.** A shader that fails to compile logs the error and
  falls back to the standard program — never a black screen, never a crash.
  Compile errors are surfaced through the report view (and the control
  channel on the browser tier).

### 6.5 Document object model (settled 2026-07-25)

The user-facing carrier of shaders is a family of three document objects.
All three keep their `DocumentObject` class in `src/App/` — the headless
server tier and `FreeCADCmd` must restore both library and working documents
— with all behavior, Coin node ownership and editors on the Gui side.
Rationale for a dedicated binder object (instead of a link property on
consumers): FreeCAD object hierarchy is not scene-graph hierarchy — objects
may have no scene graph of their own, and one object may modify another's
visuals. Hierarchy-dependent application ("this instance of a linked body,
not all instances") needs full instance paths, which only a
`PropertyXLinkSubList` on a document object can carry; and cross-document
shader libraries require `PropertyXLink`, which must be owned by a
`DocumentObject` anyway.

- **App::ShaderProgram** — one stage-tagged program: multi-dialect source
  variants (§6.3), the stage name, and its parameter set (`Foo_Bar` dynamic
  properties per §6.4).
- **App::Shader** — groups a list of ShaderProgram objects into one logical
  effect/material. One effect legitimately needs several programs: a cutout
  or displacing material needs matching depth-prepass and shadow-caster
  programs, a toon look = `material` shading + `post` outline, multi-pass
  post chains (blur, bloom) are N programs in sequence. Inert on its own —
  opening a shader-library document applies nothing. **Preview:** a
  `PropertyEnumeration` demo shape (None / Box / Sphere / Cylinder / Cone)
  with basic sizing properties lets the Shader's own view provider display
  the effect on a demo shape — Coin's builtin primitive nodes (`SoCube`,
  `SoSphere`, `SoCylinder`, `SoCone`; sizing properties map onto the node
  fields), no mesh generation of our own (verify the render-cache capture
  handles `generatePrimitives`-tessellated shapes; else fall back to a tiny
  `SoIndexedFaceSet` tessellation). Complex-shape previews use a normal
  Appearance binding instead.
- **App::Appearance** — the binder that activates shading: a
  `PropertyXLinkSubList` of targets carrying full instance paths, plus an
  XLink to a Shader object, possibly in another document (user-built shader
  libraries). An Appearance with an empty target list applies the shader's
  scene-level (`post`) programs globally — one uniform activation mechanism
  for both stages. Like-named dynamic properties on the Appearance override
  the shader's parameter values for that binding only ("same toon shader,
  different tint for these instances").

Application semantics ride the existing hierarchy-dependent material
override machinery: a new `FLAG_USER_SHADER` in
`SoFCRenderCache::Material::overrideflags` merges parent→child from the
bound path's cache into descendants — unlike the direct-node attachment of
the second slice, which stays self-scoped (no merge); both modes coexist.
Precedence on nested paths follows the existing merge convention (outer
override wins); identical-path collisions are tie-broken by
`App::DocumentObject::TreeRank` (the persisted tree-ordering key).
Element-level scoping (target subnames ending in `Face3`, as the
per-instance color override already supports) is an explicit follow-up, not
part of the first slice. The injection mechanism (secondary context along a
resolved `SoPath` vs a path-keyed override table on
`SoFCRenderCacheManager`) is decided after reading the `OverrideMaterial`
flow; lean = the cache-manager table (capture-side, keeps the
headless/WASM tier simple).

---

## 7. Implementation phasing

| phase | contents | depends on |
|---|---|---|
| 1 | **DONE** — `Render::RenderDebugConfig` + `translateRenderDebugConfig` + `u_debugParams`; `RenderDebug_ViewMode` modes 1–4 (existing targets only); `RenderDebug_FreezeFrame` | nothing — pure spine reuse |
| 2 | **DONE** — `saveRenderDump` Python API + sidecar JSON + `getRenderStats`; absorb `FC_BGFX_DEBUG_*` env gates; browser `dumpFrame` WS protocol + version-handshake/self `reload` (§4.4) | phase 1 (mode override) |
| 3 | **DONE** — verification harness (`scripts/render-verify.sh` + `render_verify.py` + `render_diff.py` + `wasm-hold.js`): named-view or golden-sidecar restaging, xvfb/`--gpu`/`--viewer` capture legs, first-divergent-stage diffing with heatmaps | phases 1–2 |
| 4 | **DONE** — dynamic name→uniform binding (`RenderDebug_*` props → like-named vec4 uniforms, snapshot v21) + `u_userParams[4]` fallback pool (lane 0 = debug output scale/bias); shader hot-reload (`FC_BGFX_SHADER_DIR` + `reloadShaders()`); `View3DInventor.addProperty/removeProperty` Python API | phase 1 |
| 5 | **DONE** — view modes 5–8 (ShadowTile coverage, Overdraw counting pass on the repurposed `ViewDebugScene` slot, ShadowFilter precision probe, UV re-render; snapshot v22); self-labeling burn-in (`RenderDebug_Label`) | 1, 4 |
| 6 | **first slice DONE** — user-loadable shaders on the Coin node model, `post` stage (coin fork: `SoShaderProgram::stage` + `BGFX_SC` source type; capture: cache-manager post-callback → `Render::UserShaderConfig` → `setUserShaderConfig`; backend: async shaderc compile cache + `ViewUserPostCopy`/`ViewUserPost` full-screen pass; sandboxed failure verified). **second slice DONE** — `material` stage with per-object attachment (`Material::usershader` through the render-cache chain, stock `vs_fc_mesh` pairing, beauty passes only, instancing exclusion). Remaining: browser tier (server-side compile + snapshot transport), property-bound parameters (§6.4), material-stage lighting helper library | 4; shader compile cache (§6.3) |

Phases 1+2 are the minimum end-to-end slice: set a mode from Python, capture
a real-GPU frame with metadata, diff it.
