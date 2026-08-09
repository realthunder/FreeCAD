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
| 9 | Reflection | the planar reflection target, tinted dark red where the mirror covered nothing | mirror pass not running/stale, mirrored camera framing, what does and does not reach the mirror |
| 10 | ImpactMap | the particle impact map (docs/RenderEngine.md §5.8) stretched over the screen: green where a hit is recorded, brightness its age against the ring lifetime, blue its strength, dark red where nothing has ever struck | impact-driven water rings — separates "the step program reported nothing" from "reported in the wrong place" from "the surface fails to show what is there" |

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

### 2.4b `RenderDebug_Coverage` — what the camera can resolve

A boolean that logs, once a second, a histogram of how many pixels each
drawn object covers: its projected bounding-box diagonal, bucketed
(`<=1px`, `<=4`, `<=16`, `<=64`, `<=256`, above), with on-screen,
off-screen and no-bounds counts and the share of on-screen objects at or
under 4px.

It answers the measurement `docs/FarFieldProxies.md` §9 gates that
workstream on: a part costs a whole object — a cache entry, a draw entry,
a material, an identity — whether it fills the screen or four pixels of
it, so what decides whether aggregating distant parts pays is how much of
the model a normal camera cannot resolve.

The projection is `PlanBoxes::sight()`, the same one the level plan ranks
with, so the histogram and the refine pass agree by construction about
what "small on screen" means. It is computed per *object*, not per draw —
a part drawing several times (opaque and transparent, faces and lines)
costs one object's worth of the overhead in question. Reported on the
level planner's schedule rather than per frame, since it is a property of
where the camera settled.

### 2.4c `RenderDebug_ProxyCut` — what a far-field cut would cost

A boolean that logs, once a second, what aggregating distant parts *would*
buy this camera — with nothing generated. The drawn instances are
partitioned into the spatial index of `docs/FarFieldProxies.md` §3, a
frontier is descended at 1, 4, 16 and 64 pixels, and each tolerance
reports the draws that cut would issue: one per (cell, material) proxy
(§5.1) plus whatever stays exact. A second line gives the per-level
distributions — nodes, residents, largest subtree, mean material buckets —
which are what size `K` and the extent target.

It is the gate of §11.1: if that draw count is not far below the draws
issued today, generating proxies is not worth building.

Two differences from `RenderDebug_Coverage` above are deliberate. It
counts **instances, not objects** — a part drawing three times pays three
draw entries, and instances are what a cut partitions. And it rebuilds the
partition on every report rather than caching it, because a measurement
that can go stale measures the wrong thing; the build cost is reported
rather than hidden, since the plan pass of phase 3 has to pay it too.

Note these, `RenderDebug_Timing` and `RenderDebug_Delta` are all
measurement switches, not shader inputs, so §2.5's dynamic-uniform binding
skips them — otherwise each would upload a `vec4` uniform nothing
declares.

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
- ⚠️ **Bless goldens from a restaged capture, not from a fresh one.** The
  sidecar stores the camera as Coin ASCII, which keeps about 8 significant
  digits, so a restaged camera is never bit-identical to the freshly staged one
  it came from -- `orientation` and `nearDistance` differ in their last digit or
  two. Most stages absorb that, but `ViewMode` 7 (the shadow-moment
  filtering-precision probe) is threshold-banded and flips ~0.3% of its pixels
  (max delta 147), which reads as a regression forever after. Restage-vs-restage
  is byte-exact because both sides then share the same already-rounded camera,
  and the rounding is idempotent after one pass. So: capture once, re-capture
  with `--golden` pointing at that first set, and bless *the second* directory.
- ⚠️ **Always pass the full `--modes` list.** The default is `0,1,2,3,4`; a
  golden set holding 0-8 then compares only five stages and prints the rest as a
  `modes only on one side` *note*, not a failure -- a silently partial pass.
- ⚠️ **`--gpu` needs a real Wayland socket.** From a shell without
  `/run/user/$(id -u)` (agent sessions), set
  `XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir` or Qt finds no platform plugin and the
  capture aborts.

This closes the "no reliable way to verify rendering" gap: the SwiftShader
blindspot is covered by the desktop leg being a *real-GPU readback* of the
same knob-for-knob staged frame.

The user-shader feature (section 6) has its own companion harness,
`scripts/user-shader-verify.sh`: a desktop leg running the
document-object-model GUI suites under xvfb (`user_shader_params.py`,
`user_shader_post.py` — property binding, per-binding overrides,
activation/deactivation with byte-exact restores) and a viewer leg
re-running the pipeline against a live headless-Chromium WASM viewer
(`user_shader_viewer.py` scene-graph route,
`user_shader_viewer_appearance.py` document-object route).

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
- **User vertex stage.** A program carrying a vertex source replaces the
  stock `vs_fc_mesh` pairing (vertex contract: `$input a_position,
  a_normal, a_color0` / `$output v_normal, v_color0, v_vpos`; the
  predefined `u_modelViewProj`/`u_modelView` chain and the engine's
  `u_params` are available — bgfx uniforms are global by name). This is
  the displacement/particle route: positions are a pure function of the
  vertex attributes, `Param_*` uniforms and the clock below. Two known
  consequences of the stock passes keeping their own programs: line and
  point draws stay at the undisplaced geometry, and Coin's auto near/far
  planes fit the undisplaced bounding box, so a large displacement can
  clip (an emitter-bounds story comes with the particle framework).
- **Render-state override.** `App::ShaderProgram` carries `Blend`
  (Default / Alpha / Additive) and `DepthWrite` properties for the
  material-stage beauty draw — the states particles and glow effects
  need. A non-default combination rides the existing parameter channel
  as a reserved `SoShaderParameter` named `fc_state` (no `u_` prefix —
  it is not a uniform contract), so it reaches Appearance per-binding
  clones and the snapshot transport with no new node fields; the
  backend consumes it at the user-draw submit by re-recording the draw
  state. The stock passes (prepass, shadow, pick) are untouched, and a
  blended override stays in its stock draw bucket — cross-object
  blend-vs-transparency ordering is the emitter framework's problem,
  not this override's.
- **Emitter seed geometry.** `App::Shader` `Demo="Emitter"` generates
  the particle seed mesh: `EmitterCount` quads whose 4 vertices
  coincide at a random anchor inside the `DemoSize` spread box
  (deterministic per `EmitterSeed`). Zero area means the stock
  pipeline shows nothing; a particle vertex shader expands them into
  billboards from the seed attributes — `a_normal.xy` = corner (±1),
  `a_normal.z` = the particle's 0..1 index, `a_color0` = the
  per-particle random seed, `a_position` = the anchor. Combined with
  `u_fcTime`, `Param_*` uniforms and the Blend/DepthWrite override,
  position-as-`f(seed, t)` gives stateless GPU particles on every tier
  with no engine-side simulation. The mesh is built from explicit
  element nodes (`SoCoordinate3`/`SoNormal`/`SoMaterial`) — the render
  cache does not capture `SoVertexProperty`-fed shapes.
- **Animation clock `u_fcTime`.** The engine records `uniform vec4
  u_fcTime` with every consuming user draw (material and post): `.x` =
  seconds on the shared effect clock (the one driving water/fire/
  caustics), `.y` = 1 while the clock advances, `.z/.w` reserved. A user
  shader whose source references `u_fcTime` is thereby *animated* — the
  reference itself is the opt-in, no declared flag — and keeps the
  viewer's redraw loop alive exactly like the stock timed effects
  (`animating()` → `scheduleRedraw`). `RenderDebug_FreezeFrame` pins the
  clock to 0 with `.y` = 0 and stops the self-scheduling, so frozen
  captures of time-animated shaders stay byte-deterministic (the golden
  harness contract).
- **Lighting helper library.** A user material program that only wants a
  custom base color should not lose the engine's lighting. The stock
  mesh shading is factored into `fc_mesh_lighting.sh` (uniforms,
  samplers and `fcShadeFragment()` — headlight or shadowed scene light,
  Render_Light bulbs incl. their shadow tiles, fire lights, screen-space
  AO, the PBR branch), consumed by the stock `fc_mesh_fs.sh` and by the
  user-facing `fc_user_lighting.sh`: `gl_FragColor =
  fcLightFragment(base, v_normal, v_vpos)` shades any albedo exactly
  like the standard renderer (`fcStockBase()` returns the draw's stock
  base color, so the identity form reproduces stock rendering). bgfx
  uniforms/samplers are global by name, so the values and textures the
  engine records with the draw feed the include's declarations
  unchanged; `gl_FragCoord` rides a macro because shaderc's spirv path
  resolves it only inside `main()`. The includes ship in
  `assets/shaders/src`, and the user-shader compile cache key carries a
  content hash of that include tree, so a shipped-helper change never
  reuses stale cached bins.

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

- **Property binding via the protocol.** A dynamic property in the
  `Param` group of an `App::ShaderProgram` is a shader parameter: its view
  provider materializes it as an `SoShaderParameter` node on the shared
  Coin shader object, named by the section-2.5 rule (group prefix
  dropped, `u_` prepended — `Param_Tint` feeds `uniform vec4 u_Tint`),
  valued by the same property→vec4-lane packing as the `RenderDebug_*`
  view properties (one shared bridge helper). Only `Param_*` binds —
  dynamic properties in other groups stay ordinary properties.
  Editing the property updates the uniform live — the
  parametric loop reaching into shading — and every consumer of the node
  (demo preview, Appearance bindings, the direct scene-graph route, the
  browser tier via the snapshot's shader table) sees it, because the
  parameters ride the captured `UserShader` and are recorded with the
  consuming draws. Like-named dynamic properties on an `App::Appearance`
  override the program's values for that binding only; a parameter the
  program does not carry is added, so a binding can drive any uniform the
  shader source declares. The backend zeroes every dynamically bound
  uniform that is not in the consuming draw's parameter list — uniform
  values persist backend-side between frames, so a removed parameter
  would otherwise keep feeding its stale value.
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
visuals. Instance context ("this occurrence of a linked body, not all
instances") needs a link path, which only a link object or property owned
by a `DocumentObject` can carry — cross-document shader libraries likewise
need XLink machinery owned by a document object.

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
- **App::Appearance** — the binder that activates shading, an
  `App::LinkGroup` whose children carry both the effect and its scope. The
  shader is the first child that resolves (through any chain of links) to
  an `App::Shader` — use an `App::Link` child to pull the effect from a
  shader-library document. Every other child is a target. Plain children
  are claimed into the group in the tree; the convention for binding
  without restructuring the document — and for carrying instance context
  at all — is an `App::Link` child (a link whose subname path points into
  an assembly). The children render like any link group's, so a target
  link child also shows an instance carrying the effect; a shader-only
  Appearance (no target children) applies the effect's scene-level
  (`post`) programs globally — one uniform activation mechanism for both
  stages. Like-named dynamic properties on the Appearance override the
  shader's parameter values for that binding only ("same toon shader,
  different tint for these instances"). A `Scope` enumeration
  selects between the two application modes below (default `Object`;
  `Element` is the reserved follow-up value).

**Scope=Object — direct attachment, the cheap mode.** Each target is
resolved to its final object and the binding's own `SoShaderProgram` node
(per-binding parameter overrides baked in) is inserted at child 0 of that
object's view-provider root. The capture callback folds it into the
object's own render cache (`SoFCRenderCache::setUserShader` →
`Material::usershader`), and `mergeMaterial` merges it **down through all
child caches, outer shader wins** — exactly the link material-override
semantics (`SoFCSelectionRoot::setColorOverride` → `overrideflags`).
Because view-provider snapshots share the root's children, every instance
everywhere picks it up, in every view, at zero per-frame cost; a group or
assembly target shades all its children. Position inside the owning cache
is irrelevant (override semantics, not Coin's after-the-node material
rule). Note the one asymmetry: a subname shortcut link (`Link → 
A1."A2.Box."`) renders only the tail object's snapshot with a baked
transform — it bypasses the intermediate objects' nodes, so a shader
attached to `A2` does not reach that shortcut's Box (it is an instance of
Box, not of A2).

**Scope=Instance — per-occurrence chain override, the sparse mode.** Each
target child contributes its **resolved object chain**: a link child
`Link001 → A1."Assembly2.Box."` registers `[A1, A2, Box]` (the link's own
root deliberately excluded), a plain child registers `[itself]` (= every
occurrence). The binding registry then enumerates the document's logical
occurrences — depth-first over every object's `getSubObjects()`, each
visited object expanded to its resolved sequence so occurrences *inside*
subname shortcuts still match the chain they resolve through — and every
occurrence whose resolved sequence **ends with** a registered chain
(suffix-anchored: `Assembly3.A1.A2.Box` matches, `A4.A2.Box` does not)
gets a per-path override. A match stops the descent: the override covers
the whole subtree, so an outer binding wins over any deeper match.
Occurrence overlap between Appearances: longest chain wins, then
`App::DocumentObject::TreeRank`, then name. Document signals (object
add/remove, link property changes) re-run the scan, coalesced through the
event loop and only while chains are registered.

The per-occurrence override rides the **per-path selection side channel**,
not a material merge: `getVertexCaches` memoizes one `vcachemap` per
render cache shared by every path through it, so a path-scoped override
cannot ride `Material::overrideflags` merge-down (that slot is per-cache,
path-blind). Instead `SoFCRenderCacheManager::addShaderOverride(key,
nodepath, shader)` mirrors the element-color-override machinery
(`selcaches`): a path-keyed sensor rebuilds a whole-object
`VertexCacheMap` for the bound instance path —
`SoFCRenderCache::buildWholeCacheMap`, which keeps the original geometry
and materials (normals intact; the highlight-index caches
`buildHighlightCache` substitutes carry none) — stamps `usershader` on its
triangle materials, and registers it as a **non-on-top whole-object
selection entry**: it renders in the normal scene passes with the shader
substituted while the base draws are suppressed by the whole-object key
(GL: `selectionkeys`/`draw_entry.skip`; bgfx: `hiddenKeys`). The backend
keeps the replaced geometry in the shadow-caster, depth-prepass,
debug-scene and ground-reflection passes (stock programs), so only the
beauty shading changes. Occurrence paths are materialized per 3D view
(`appendDetailPath` + `getDetailPath`).

**Scope=Element — face-level override.** As Instance, but a target
subname ending in a face element (`A1."A2.Box.Face3"`) restricts the
override to that face of every matched occurrence; a target without an
element part behaves like Instance. The element ref is appended to the
occurrence subname, so `getDetailPath` yields the tail shape's
`SoDetail`, and `buildWholeCacheMap` narrows the map to that face's
triangles out of the *original* cache (partial-index rendering — real
normals, unlike the highlight-index substitutes). The entry is partial,
so the base draw is **not** suppressed: the face renders over its
coincident base copy, with a small negative polygon offset (the
backend's NDC depth-bias emulation) so it wins the depth contest
regardless of draw order. Face elements only — edges and vertices have
no material stage to replace. Precedence: element-scoped bindings
coexist with a whole-occurrence winner and with each other (one winner
per element, the usual chain/TreeRank/name rule); whole bindings
register first so a face override draws over a whole-occurrence shader
on the same occurrence.

Scene-level activation is the same registry: a visible shader-only
Appearance contributes its Shader's `post`-stage programs (with its
parameter overrides applied) to a per-view list the renderer appends
after the node-captured shaders — `SoFCRenderer::setAppearanceShaders`,
forwarded through the render cache manager, replaced wholesale on every
binding rebuild and independent of scene recapture. Appending last makes
a document-object activation win the backend's "last shader on a stage"
rule over raw scene nodes; multiple shader-only Appearances order
ascending by TreeRank (name fallback), so the highest-ranked one wins,
matching the per-target precedence direction. A targeted Appearance
applies only object-scoped programs — post programs need a shader-only
group.

Found on the way (fixed): clearing a `PropertyXLink*` property to empty
never notified — `Property::hasSetValue`'s `isSame(_old)` optimization
compares live `getLinks()`, but a `copyBeforeChange()` snapshot of an
XLink stores only names (`copyTo`), so empty-after vs populated-before
compared "same". `PropertyXLink::isSame`/`PropertyXLinkSubList::isSame`
now compare name-level identity.

---

## 7. Implementation phasing

| phase | contents | depends on |
|---|---|---|
| 1 | **DONE** — `Render::RenderDebugConfig` + `translateRenderDebugConfig` + `u_debugParams`; `RenderDebug_ViewMode` modes 1–4 (existing targets only); `RenderDebug_FreezeFrame` | nothing — pure spine reuse |
| 2 | **DONE** — `saveRenderDump` Python API + sidecar JSON + `getRenderStats`; absorb `FC_BGFX_DEBUG_*` env gates; browser `dumpFrame` WS protocol + version-handshake/self `reload` (§4.4) | phase 1 (mode override) |
| 3 | **DONE** — verification harness (`scripts/render-verify.sh` + `render_verify.py` + `render_diff.py` + `wasm-hold.js`): named-view or golden-sidecar restaging, xvfb/`--gpu`/`--viewer` capture legs, first-divergent-stage diffing with heatmaps | phases 1–2 |
| 4 | **DONE** — dynamic name→uniform binding (`RenderDebug_*` props → like-named vec4 uniforms, snapshot v21) + `u_userParams[4]` fallback pool (lane 0 = debug output scale/bias); shader hot-reload (`FC_BGFX_SHADER_DIR` + `reloadShaders()`); `View3DInventor.addProperty/removeProperty` Python API | phase 1 |
| 5 | **DONE** — view modes 5–8 (ShadowTile coverage, Overdraw counting pass on the repurposed `ViewDebugScene` slot, ShadowFilter precision probe, UV re-render; snapshot v22); self-labeling burn-in (`RenderDebug_Label`) | 1, 4 |
| 6 | **first slice DONE** — user-loadable shaders on the Coin node model, `post` stage (coin fork: `SoShaderProgram::stage` + `BGFX_SC` source type; capture: cache-manager post-callback → `Render::UserShaderConfig` → `setUserShaderConfig`; backend: async shaderc compile cache + `ViewUserPostCopy`/`ViewUserPost` full-screen pass; sandboxed failure verified). **second slice DONE** — `material` stage with per-object attachment (`Material::usershader` through the render-cache chain, stock `vs_fc_mesh` pairing, beauty passes only, instancing exclusion). **third slice DONE** — browser tier (server-side compile through the async shaderc cache, snapshot v23 user-shader table, viewer loads shipped bins). **fourth slice DONE** — §6.5 document object model (App::ShaderProgram/Shader/Appearance + view providers, path-keyed shader overrides through the render cache manager) and §6.4 property-bound parameters (`Group_Name` dynamic props → SoShaderParameter nodes → uniforms with the consuming draws; Appearance per-binding overrides; stale-uniform zeroing). **fifth slice DONE** — scene-level Appearance activation (shader-only Appearance → `setAppearanceShaders` per view, TreeRank-ordered, wins over raw scene nodes). **sixth slice DONE** — Appearance reworked as an `App::LinkGroup` (children = shader + targets, `Scope` enum {Object, Instance}: direct merge-down attachment vs suffix-anchored per-occurrence chain overrides via the logical occurrence scan). **seventh slice DONE** — element-scoped targets (`Scope=Element`: face-level per-occurrence overrides through the same path channel, partial entries over the untouched base draw) and the new-view rebind hook (a 3D view created after the bindings exist gets the per-view registrations through a coalesced rebuild). **eighth slice DONE** — the material-stage lighting helper library (`fc_mesh_lighting.sh` core shared with the stock mesh shader, user-facing `fc_user_lighting.sh`, include-tree hash in the compile cache key). Phase complete | 4; shader compile cache (§6.3) |

Phases 1+2 are the minimum end-to-end slice: set a mode from Python, capture
a real-GPU frame with metadata, diff it.
