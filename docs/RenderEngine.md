# Render Engine Design

The bgfx-based renderer (`src/Gui/Renderer/`) and its user-programmable
shader framework. This is the architecture reference; companion
documents cover specific subsystems in depth:

- `docs/RenderDebug.md` — render debugging, the verification harness,
  and the design history of the user-shader feature (§6).
- `docs/TShapeRenderCache.md` — TShape-level tessellation sharing and
  the cross-object instancing architecture.
- `docs/ViewSettings.md` -- the per-view settings model (`Render_*`,
  `Light_*`, `Section_*`), what a document is allowed to carry to
  somebody else's installation, and the Clipping panel.
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
BGFXRenderer                               (Gui/Renderer/BGFXRendererP.h + BGFX*.cpp)
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

A frame the backend declines (`render()` returning false) is drawn
entirely by Coin, and that is also what a broken stock shader pack
falls back to. The programs load through `fcLoadProgram`
(BGFXRenderer.cpp) rather than bgfx_utils' loader, which asserts on a
missing `.bin` and then hands `createShader` a null block; a stage
that will not load is reported and yields an invalid handle instead.
Feature programs degrade individually (bgfx drops a submit with an
invalid program), but if one of the core programs — mesh/flat, their
clip and texture variants, the section caps, and the present pass in
the standalone tier — is missing, `BGFXView::init()` reports the pack
and tears the view back down, and every frame after that declines
until `reloadShaders()` moves the shader generation.

### Startup and backend lifetime

The backend used to be built by the first 3D view, which put its whole
cost (GL context, `bgfx::init`, device objects, every shader program) in
front of the user's first **New Document**. It now comes up during the
splash instead: `Gui::postMainWindowSetup` calls `RendererLib::warmup()`
just before `mw.stopSplasher()`, gated on render-cache mode 3 and a
non-empty renderer `Type`. `warmup()` is a default no-op on
`RendererLib`, so a backend that has nothing to warm costs nothing.

The bgfx implementation warms two distinct things, and both are needed:

1. `prepare()`: the offscreen surface, the GL context and `bgfx::init`.
2. The **programs**, by building a view (its own `init()`) and pumping
   one `bgfx::frame()`. WARNING: creating a program is nearly free on
   the API side; bgfx defers the real work to the frame submit, so
   warming without a `bgfx::frame()` moves nothing.

WARNING: **the warm view is kept alive for the life of the process.**
`removeView()` of the last view calls `shutdown()`, so releasing the
warm view tears the device back down and warms nothing, and frees the
context out from under the `doneCurrent()` that follows, which
segfaults in the startup path. The consequence is deliberate and worth
knowing: **bgfx now stays up for the whole session** rather than going
away when the last 3D view closes.

WARNING: **Qt 6.4+ destroys and recreates a top-level's native window
the first time a `QOpenGLWidget` appears under it.** The surface type
changes from `RasterSurface` to `OpenGLSurface`
(doc.qt.io/qt-6/qopenglwidget.html, "This behavior is new in Qt 6.4").
In FreeCAD the 3D view is that first widget, so the *first* New Document
made the main window vanish, taskbar entry included, and come back.
The fix is one hidden 1x1 `QOpenGLWidget` named `GLSurfaceWarmup`,
created in the `MainWindow` constructor where no native window exists
yet; the renderer warm-up reuses it for its pixel format. It must be
**kept**: constructing and destroying it takes the surface state back
with it and the vanish returns unchanged.

Measured with `fcad-probes/newdoc_delay_probe.py` (xvfb/llvmpipe debug
build), seconds to the first three New Documents:

| | doc 1 | doc 2 | doc 3 |
| --- | --- | --- | --- |
| before | 1.406 | 0.223 | 0.284 |
| + `GLSurfaceWarmup` | 1.049 | 0.185 | 0.237 |
| + device warm-up | 0.735 | 0.173 | 0.202 |
| + programs (shipped) | **0.236** | 0.213 | 0.208 |

The first document now costs what every later one does. The warm-up
itself is 458 ms there (context 21, device 141, programs 5, flush 292)
and **1015 ms on the real GPU** (Mesa d3d12 / RTX 3070 Ti: context 73,
device 373, programs 6, flush 564), and 1102 ms with a cold driver
shader cache, so the first launch after a build pays more. It is spent
under the splash, which was already on screen for longer than that.

WARNING: `getView()` calls `prepare()` again and `prepare()` zeroes its own
timing counters on entry, so read them *before* `getView()` or the
context and device phases both report 0.

The attribution that settled where the time went: run the same probe
with `Type=Default`. The plain-GL path was flat (0.103 / 0.092 / 0.088),
which rules out document, `Gui::Document` and 3D-view construction and
leaves the backend.

### Tiers

The engine is one private header, `BGFXRendererP.h` (class definitions,
vertex/GPU cache structs, shared inline helpers), plus per-feature
translation units — `BGFXRenderer.cpp` (public API, `BGFXRendererLibP`,
shader pipeline), `BGFXFrame.cpp` (`Private::render()`: pass table, view
config, submit loop, section caps), `BGFXScene.cpp` (scene feed,
snapshot/publish, instance groups), and the `BGFXView*` files
(`Lifecycle`, `Env`, `Shadow`, `Overlay`, `Particles`, `Effects`,
`Submit`). The same sources compile into three deployments:

| Tier | Definition | Scene source | Shaders |
|---|---|---|---|
| Desktop | in-process with the Qt GUI | live render caches | build-time `shaderc` pack + on-demand user compile |
| Standalone / WASM viewer | `FC_RENDERER_STANDALONE`, `Gui/Renderer/wasm/main.cpp` | `SceneDump` snapshot, streamed over WebSocket (`SceneStreamServer`) | its own essl pack + server-compiled user-shader binaries shipped in the snapshot |
| Headless serve | desktop build under Xvfb (`scripts/renderer-serve.sh`) | live caches | as desktop |

`SceneDump` serializes the draw lists, materials, configs and the
user-shader table (sources, parameters, compiled variants); the
snapshot version gates format changes and the viewer self-reloads on a
newer payload. (Version numbers cited in this doc — v24, v26, v39, … —
are the versions that introduced each lane; the current one is
`kVersion` in `SceneDump.cpp`.)

#### Out-of-band texture payloads (v26)

A snapshot is republished whenever any feed changes — down to a
selection pick — while the embedded images in it almost never change
with it. So on the streaming transport the texture pixels leave the
stream: each texture is written as its header plus a **content key**
(SHA-1 of the pixel bytes, 40 hex characters), and the payload is
handed to a sink that the scene server serves separately at
`GET /blob?key=<key>`.

The viewer resolves a key from, in order, its resident cache, its
IndexedDB store, then the network — and writes what it fetched back to
IndexedDB, so the cost survives a page reload, not just a republish. A
staged snapshot is applied only once **every** key it names is in hand:
applying progressively would hand the backend a texture it has already
keyed a GPU upload on under the same `textureId`, which it would not
re-upload. A key that cannot be fetched renders that texture untextured
rather than stalling the scene, and is retried on the next publish.

Content addressing is what makes the caches sound: a key names those
bytes and no others, so an entry is valid forever and needs no
invalidation protocol. It is deliberately *not* `textureId`, which
guarantees only "same id, same pixels" **within one process** and so
could never back a store that outlives the page.

The section-cap hatch image rides the same path since **v27**, by being
a texture-table entry rather than the raw blob written inline beside the
configs that it used to be. That one line of the stream was 64% of a
small scene's payload, re-sent on every publish, purely because it sat
outside the table the deferral applies to.

Mesh chunks follow the same shape in **v28**, with two differences
that follow from meshes being many and small rather than few and
large. They are pulled in **batches** packed to a byte budget rather
than one request each, and their key is memoized on `MeshData::cacheId`
rather than recomputed per publish — sound because a cacheId names one
content for the life of the process, which is the one direction that
counter guarantees. The cacheId itself is excluded from the hashed
bytes: it changes on every re-tessellation, so hashing it would mint a
new key for geometry that did not change.

A payload the viewer gets back from its IndexedDB store is **verified
against its key** before it is used. The store is content addressed, so
the key is the hash and checking costs one pass over bytes that would
otherwise have been downloaded. This is not paranoia about bit rot: an
entry whose content does not match its key is indistinguishable from a
correct one at every later step — it parses, it renders, and it yields
garbage geometry and out-of-bounds picks rather than an error — so
nothing but comparing the bytes catches it. A store is only
self-verifying if something actually verifies; content addressing
guarantees the *writer* named the bytes correctly, not that the map in
the browser still holds what the name says. A mismatch drops that entry
and refetches it; `?noidb` skips the store entirely, which separates a
store problem from a stream problem in one reload.

Every out-of-band chunk begins with a **chunk-layout version** (v32),
inside the bytes the key hashes. A change to a chunk's layout therefore
changes every key, and a payload cached by an older build can never be
handed to a reader that would misread it — the key covers the format,
not just the content. A cached payload that still fails to parse means
the store is stale or damaged; since it is a pure optimization, the
viewer clears the whole store and reloads rather than repairing it
entry by entry, and stops its scene pipeline first so nothing runs on
into a page that is navigating away.

A snapshot missing any payload is **not applied at all**. The draws of
a mesh that never arrived still point at it, and the backend reaches a
mesh through the shadow, outline and segment-instancing paths as well
as the guarded submit one — a scene with holes crashes there. Textures
are the exception: a draw renders untextured rather than not at all.

Deferral is a property of the transport, not of the format: the sink is
set only by the streaming publisher, so a snapshot captured to a file
(`FC_BGFX_DUMP_SCENE`, the bundled `/scene.fcsd`) stays self-contained
and needs no server to load. The server keeps a blob for the current
and previous publish; older ones are dropped, since a scene's textures
are bounded but a session's history is not.

This is the browser-tier half of the content-addressed storage in
`docs/FileBlobsManager.md` — the same idea (identity is the bytes,
lifetime follows the references) applied to the wire instead of the
`.FCStd`.

Textures are the leaf tier of a larger design, now implemented:
`docs/SceneStreaming.md` specifies the manifest tree (root →
per-object → mesh/material/texture) and the delta sync that a thin
client needs to serve big models, for which re-sending an unchanged
scene on every publish is the wall. The code lives in `SceneLadder`
(per-object level rungs under CPU/GPU budgets), `MeshSimplify` /
`MeshSource` (rung generation), and the snapshot's delta form
(`manifestVersion`/`baseVersion` in `SceneDump`). The server side has
likewise grown past a single-snapshot pipe: `SceneServer` serves
multiple documents (`Gui.serveDocument` / `Gui.serveClients`) with
per-endpoint access tokens, grants and a sharing roster — see
`docs/MultiDocServe.md`, `docs/ShareAccess.md` and
`docs/ThinClientUI.md` for that tier.

## 3. Frame anatomy

Each frame is a fixed sequence of bgfx views (`BGFXView::PassView`)
sharing one framebuffer (auxiliary passes own theirs). Groups, in
order:

0. **Particle state** — stateful-particle simulation and impact-map
   views (ping-pong FP textures, §5.8), first in id order ahead of the
   scene groups.
1. **Background** — clear, gradient quad (or the PBR environment
   itself, see below), optional sun disc.
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
   section caps, cavity multiply, hidden-line outlines, caustics splat,
   volumetric upsample-apply.
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

### Cavity (curvature) shading

`ViewCavity` is one fullscreen multiply between the opaque passes and
the outlines, enabled by `Render_Cavity`. It darkens concave creases
(`Render_CavityValley`) and convex ridges (`Render_CavityRidge`) so
surface shape reads independently of how the scene is lit — the
inspection shading a workbench view wants. It reuses the AO block's
geometry prepass, so it costs one extra pass, and it turns that prepass
on by itself when ambient occlusion is off.

Two things about it are deliberate:

- **Curvature comes from the normal field, not from positions.** The
  textbook estimator — each neighbour's signed distance from the centre
  pixel's tangent plane — reads positions, which are piecewise linear
  across a tessellation, so every facet boundary on a sphere or cylinder
  registers as a crease and the surface ends up wearing its own triangle
  grid. Interpolated normals stay smooth across those facets while still
  jumping at a real edge, where the crease angle splits them.
- **Both terms darken.** The pass multiplies the finished 8-bit scene
  color and so cannot brighten past white; the ridge *highlight* other
  workbench renderers add is not available without an HDR scene target.

### Matcap shading

`Render_Matcap` replaces the lit shading with a fixed studio welded to
the camera, looked up by the fragment's view-space normal — a third
branch in `fcShadeFragment` beside the headlight and PBR ones, so it
overrides PBR while on. No lights and no shadow tap take part: a
surface's shading then depends only on which way it faces the viewer,
which is what makes form comparable between parts anywhere in the
scene. Screen-space AO still multiplies in, because occlusion is not a
light and contact darkening is a cue worth keeping.

The presets (`Render_MatcapPreset`: studio, clay, metal, pearl) are
computed analytically in `fc_matcap.sh` rather than sampled from matcap
images. That is what lets the feature ship with no image assets to
commit, install or fetch — the browser tier gets it with no bundle
growth, at any resolution. `Render_MatcapTint` mixes each object's own
color back in: 0 shades the scene as one uniform material, 1 keeps the
assembly's color coding.

Pair it with cavity: matcap gives every same-facing surface the same
value, which is exactly when curvature darkening has to supply the
edges.

Fill-bound effect passes (AO, volumetrics, bloom, reflection…) can run
below main resolution via the `Render_EffectResolution` view property
(`BGFXRenderer::setEffectResolution`); the scene and line passes always
render at full resolution.

Determinism: every time-animated effect (water waves, caustics, fire,
volumetric jitter, user shaders via `u_fcTime`) reads one shared
animation clock. `RenderDebug_FreezeFrame` pins it to 0 and suppresses
self-scheduled redraws; two frozen frames are byte-identical.

### 3.1 The view-id budget

bgfx addresses views by id out of a fixed table, and submitting an id
past it is fatal, not an error return. Two numbers bound it:
`BGFX_CONFIG_MAX_VIEWS` (`src/3rdParty/CMakeLists.txt`) is the
**ceiling** the build can address -- 4096, which is what the draw sort
key's view field costs a bit for -- and `Render/MaxViewIds` is the
**runtime limit** a session actually hands out and walks, default 1024,
0 asking for the ceiling. They are separate because they used to be one
number: raising the constant for capacity also made every frame pay for
slots no view would ever use, so capacity is cheap and use is not (with
render stage timing on, running at the ceiling takes a 59fps session to
19). A change to the limit is read when the backend starts, so it needs
a restart.

The pass sequence above is 87 ids wide (`BGFXView::NUM_VIEWS`), so
reserving all of it per viewer fit only five 3D views in the 512-id
build this began as, and the sixth was refused.

A frame draws far less than the whole sequence, so each one **declares
the passes it will use** and those are mapped onto the consecutive ids
of a block sized to fit them (`markPass` / `mapPasses` / `vid`). Enum
order is draw order is bgfx's submission order, so compaction preserves
the sequence. Blocks come from a granule pool and only grow, which
keeps a viewer's ids still as its scene changes. A plain viewer needs
13 ids (a 16-id block at the 8-id granule), so the default limit is
roughly 64 viewers and the ceiling roughly 256.

The declarations come from **one per-frame pass table** in `render()`:
every pass states its liveness predicate exactly once, next to the
closure that configures its bgfx view, and the mark phase, the
view-config phase and the frame-level submit gates (`passLive`) all
read the same entry — the three phases cannot drift apart, and a
`PassView` added without a table entry reports itself once instead of
silently discarding its draws.

Two properties make this safe to be wrong about:

- A pass the frame did not declare maps to a per-view **discard target**
  (1×1), never to another pass's id. Missing a case costs that pass its
  pixels, and the draws that land there are counted and reported by pass
  number — it cannot corrupt a neighbour or abort the process.
- When the pool cannot fit a viewer, that viewer **falls back to Coin**
  for the frame with one message, the same answer as before.

`FC_BGFX_DEBUG_VIEWS=1` prints each block as it is handed out.

### 3.2 The light budget

Lights reach a frame down **two independent paths**, with separate
capacities. Neither can consume the other's slots, and a scene can run
both full at once.

| | array | slots | fed from |
|---|---|---|---|
| Coin lights | `u_viewLight` | 8 | `SoLightElement`, via `translateViewLightConfig` |
| fire flames | `u_localLight` 0..3 | 4 | fire body appearance slots, in `render()` |
| `Render_Light` bulbs | `u_localLight` 4..7 | 4 | `Material::lightsource` draws, in `render()` |
| scene light | `u_lightDir` etc. | 1 | `SoLightElement`, via `translateLightConfig` |

The rig itself is per view: each of its fourteen keys can be answered by
a `Light_*` property on the view object rather than by this
installation's preference, which is what lets a document carry its own
lighting (`docs/ViewSettings.md`).

**Coin lights** (`Render::ViewLightConfig`) are the viewer's rig --
headlight, backlight, fill light -- and any `SoDirectionalLight` /
`SoPointLight` the traversal holds above the render-cache root. The
ambient of the same config is the `SoEnvironment` the viewer carries
beside the fill light (§7). **Effect lights**
(`u_localLight`, split in half by `kMediumSlots`) never come from the
traversal at all: they are computed per frame from the draw materials,
the fire half carrying the flame centroids with their clock flicker,
the bulb half the light-source bodies and their shadow tiles. The
**scene light** is the single shadow-casting one (the Shadow draw style
or `Render_Light`), with a map, sun disc and ground of its own.

⚠️ **Bloom is not a light.** A `lightsource` body feeds its emission
into the bloom pass at `lightintensity`, but the illumination it casts
on nearby surfaces is its bulb slot above. Turning bloom off removes
the halo and changes no lighting.

**Why 8 Coin lights, and what it would cost to raise.** Not a Coin
limit: `SoLightElement::add` is an unbounded `SbList` append, and the
familiar 8 is `SoGLLightIdElement::getMaxGLSources()`, i.e. a literal
`glGetIntegerv(GL_MAX_LIGHTS)` binding Coin's own fixed-function
renderer. This engine reads the element and drives its own shader, so
that ceiling never applied to it. The real one is the fragment uniform
budget, against the 224 vec4 an ES3/WebGL2 device has to guarantee:

| | vec4 |
|---|---|
| `u_bulbShadowMtx` (16 bulb shadow tiles) | 64 |
| `u_frameParams` (surface finish frame palette) | 48 |
| `u_viewLight` + `u_viewLightColor` + `u_viewLightAtt` | 24 |
| `u_localLight` + `u_localLightColor` | 16 |
| `u_envSH`, `u_finishParams`, matrices, scalars | 32 |
| **total** | **184** |

So 16 Coin lights would fit (208, 16 spare) and 32 would not (256).
Note what that table says about where the room actually is: the effect
lights cost 80 vec4 against the Coin lights' 24, and the single
largest consumer in the shader is a shadow atlas sized for at most
four bulbs. Repacking a light is possible too -- one needs 10 floats
(direction or position 3, colour 3, attenuation 3, kind 1) of the 12
it occupies, and a directional light never uses the attenuation -- but
that trades away the exact Coin attenuation the current layout keeps.

Raising the cap is capacity for a producer that does not exist:
upstream's three-point rig plus the scene light is four. And past 8,
Coin's own compositing in cache mode 3 would still stop at
`GL_MAX_LIGHTS`, so a ninth light becomes a bgfx-vs-Coin divergence of
exactly the kind the ambient work removed.

The loop cost does not argue either way: active slots are packed from
0 with the tail zeroed, so the shader stops at the first empty one and
unused capacity is free.

**A spot light spends two slots.** Its position, colour and
attenuation already fill a slot's twelve floats; the cone's cutoff
cosine and falloff exponent take the two spare `.w` components, and the
axis -- three floats with nowhere left to go -- takes the following
slot whole (`kind = 4`, which the shader shades as a light of zero
colour so the loop walks past it). That is why the table above did not
move: a fourth per-light array would have cost 8 vec4 in every frame,
spot or not, and the same room can be borrowed from a capacity nothing
fills. The packer never starts a spot in the last slot, so the
shader's `i + 1` is always in range.

### 3.3 Targets follow demand, not capability

A view's render targets used to be decided by what the GPU *can* do:
`m_shadow` / `m_oit` / `m_ssao` / `m_vol` are capability tests, so a
session with volumetrics, water, bloom, reflections and shadows all
switched off still paid for every one of them. At 1080p that is
**~352MB of a ~554MB** per-view target footprint, on every open 3D
view, for passes that never run.

`BGFXView::updateEffect(EffectGroup, bool)` reconciles each group once
per frame from the frame's configuration block, **before** anything
reads a handle. Capability still gates absolutely -- a group whose
`m_*` flag is false is never allocated whatever the configuration says
-- but on top of it a group is built by the first frame that wants it
and released when the want goes away. A group whose allocation fails
(the pools are shared with every other view) is **latched off** rather
than retried every frame: a bailed frame never reaches `bgfx::frame()`,
which is the only place bgfx reclaims, so retrying is how a full pool
turns into a spin. The latch clears on a resize, and whenever the
configuration turns the group off and on again.

| group | targets | at 1080p | wanted by |
|---|---|---|---|
| `EffectVolumetric` | raymarch + history pair, water/cloud/fire intervals | ~166MB | `Render_Volumetric` |
| `EffectShadow` | moments + depth, blur ping, glass tint pair | ~117MB | a scene light *and* `Render_Shadow` |
| `EffectSSAO` | depth+normal prepass, AO chain, glass interval | ~98MB | AO / cavity / volumetric / water / debug views / glass seen |
| `EffectBulbShadow` | the 2048^2 bulb tile atlas | ~50MB | a light-source body (allocate only) |
| `EffectReflection` | mirrored-camera re-render | viewport | ground reflection or planar water |
| `EffectBloom` | quarter-res halo + blur ping | viewport/16 | `Render_Bloom` |

! **Release is driven by CONFIGURATION, never by scene content.** The
frame's `*Active` flags fold in things like "this frame's scene has a
water body" or "the shadow pass ran"; releasing on those frees and
rebuilds across ordinary editing, and across the momentarily empty feed
of a document switch. Scene state may **add** to a group's demand and
never take it away -- which is how the two groups with no preference
behind them join in: the bulb atlas is allocate-only, and glass (a
material, so there is nothing to switch) latches `glassSeen`.

`EffectShadow` is the one group whose extent is a setting rather than
the viewport, so `ShadowPrecision` joins the reconcile: a size change
rebuilds the set, and clears its failure latch, a smaller map being a
real chance to fit where the last one did not.

Measured at 1920x1080, one view: the four effect groups are 10 fbo / 18
tex / **86.9MB**, and the shadow group another 6 fbo / 5 tex /
**117.4MB** (0.25 precision: 7.4MB). Every one of them returns to
exactly its baseline when switched off again.

### 3.4 Background views give their targets back

Ids are not what a view mostly costs -- its render targets are. One
1644x653 view with every effect on holds **287MB** of them, and it held
them whether or not anyone could see it, so a session with four
documents open paid for four view's worth of targets to look at one.
The demand allocation above (each effect group built by the first frame
that wants it) answers the *unused* half of that; this answers the
*unwatched* half, and they compose -- a background view releases
whatever it had built.

The release is `BGFXView::destroyTargets()`, reached through the
backend-agnostic `Render::Renderer::releaseTargets()`. It is exactly the
release half of a resize: the sized targets go, the programs, uniforms
and uploaded scene stay. **Nothing restores them**, because nothing has
to -- a frame already rebuilds on `!isValid(bgfxFbo) && !targetsFailed`,
which is precisely the state `destroyTargets()` leaves, so the first
frame after the view comes back takes the resize path it would have
taken anyway.

Two things the mechanism has to get right:

- **The wait cannot live in the frame path.** A view nobody is looking
  at renders no frames, so a deadline checked there never comes due.
  `View3DInventorViewer::armBackgroundRelease()` starts or cancels a
  single-shot timer, and every signal that could change the answer calls
  it: `View3DInventor::windowStateChanged` (the MDI case), the viewer's
  hide/show events, and a change to the delay itself.
- **"Nobody is looking" is not a Qt visibility.** Measured: switching
  MDI tabs delivers the outgoing view a `QHideEvent` *immediately
  followed* by a `QShowEvent`, and leaves it `isVisible() == true`,
  stacked behind the maximized incoming one -- a first implementation
  hung the release off `hideEvent` and it therefore never fired on the
  one case that matters. So `View3DInventor::isBackgroundView()` **asks
  a question** instead of remembering an event: hidden or minimized, or
  else a sibling MDI child is maximized over me. In the tiled MDI modes
  nothing is maximized and no view is in the background, which is right
  -- they are all on screen at once. Asking rather than latching also
  makes the order of the two `windowStateChanged` emissions a tab switch
  produces (one per view whose state changed) irrelevant.
- **The destroys have to be executed, not just queued.** `bgfx::destroy`
  writes a command; the memory comes back when a frame executes the
  buffer. The one process that needs this most -- a single 3D view, user
  now on a spreadsheet tab -- has no other frame to ride on, so
  `releaseTargets()` does the context dance a frame does and calls
  `bgfx::frame()` itself.

`Render/BackgroundReleaseDelay` (ms, default 1000, 0 = keep) is the
grace period, with the usual `Render_BackgroundReleaseDelay` per-view
override; it is a `_localRenderParam`, so it never travels in a
document. The delay exists because coming back costs the frame that
rebuilds -- ~68ms on the view measured above, against ~23ms steady --
and a click through the tabs should not pay it. What comes back is
**byte-identical** (avgColor difference 0.000 across a rebuild): the
trade is a hitch, never an image.

For scale, releasing only the demand-allocated effect groups instead
was measured at 95.5MB of the 287MB (33%) and +11.4ms -- taken before
the shadow group joined them, so a shadowed view would give back more
now, and still not the core, OIT and prepass targets a full release
takes. The full release is worth the delay it needs.

Measured end to end on the real GPU (D3D12 under WSLg), two documents
open with every effect on, the pools read through the *other* view --
bgfx's counters are process-wide, and reading them pumps a frame, so a
background view cannot be asked about itself:

| | frame buffers | textures | render target memory |
|---|---|---|---|
| A alone | 36 | 66 | 287.4MB |
| A + B built | 68 | 121 | 574.8MB |
| A backgrounded | 36 | 74 | 287.4MB |
| A back on screen | 68 | 121 | 574.8MB |

That is **all** of A's 287.4MB, and A's captured frame is md5-identical
before and after. The undrawn case was confirmed separately on software
GL, where render targets are ordinary host memory: RSS fell 32MB across
a background transition with no frame drawn anywhere in the process,
which is what the `bgfx::frame()` drain is for (under d3d12 the same
transition moves RSS by 0.3MB, because the driver allocates outside the
process's accounting -- the GPU is the wrong place to ask this
question).

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

### Surface finish (procedural machining relief)

A `Render::Material` may carry an `App::SurfaceFinish` — the pattern the
surface was machined with (diamond/straight knurl, brushed, blasted,
turned) plus its pitch and depth **in millimetres** and a lay angle.
`fc_finish.sh` shades it inside the CAD mesh fragment shader, one
uniform-selected branch that costs nothing on a scene that states none.

- **Source**: either authored on the appearance (`ShapeAppearance`'s
  per-entry finish, which is per-face-capable storage) or stated by the
  `Render_Finish` / `Render_FinishPitch` / `Render_FinishDepth` /
  `Render_FinishAngle` dynamic ViewProvider properties. The authored one
  wins, the same way a PBR-mode appearance beats `Render_Metallic`. Both
  end up on `SoFCRenderMaterial` and travel the ordinary render-cache
  route.
- **Per face, through two palettes**: a finish is four numbers and a
  frame is ten, so neither widens the per-vertex material stream. The
  distinct finishes of an appearance become a `Render::FinishPalette`
  and the distinct frames of the geometry a `Render::FramePalette`; what
  the stream's third slot carries is one byte of index into each
  (`docs/ShapeAppearanceDesign.md` 9.9 and 9.11). A draw with no stream
  reads entry 0 of both.
- **Object space, in the face's own frame**: the pattern is anchored to
  the geometry (`v_opos`/`v_onrm`), so a moved, scaled or instanced copy
  carries the same finish rather than one that swims as it is placed.
  Where the geometry could state a frame - a plane's own axes, or the
  axis a cylinder or cone was turned about, read off the OCCT surface at
  tessellation time - the pattern is laid out in it, which is what makes
  a straight knurl run along the axis and turning marks centre on it.
  A face with no analytic surface falls back to the TRIPLANAR
  projection: three axis-aligned projections blended by the object-space
  normal, plausible from any view but ignorant of the geometry, and what
  every face got before frames existed.
- **No parametrization**: the shading normal is rotated by Mikkelsen's
  surface gradient, whose screen-space derivatives come from the
  pattern's *analytic* object-space gradient. No tangent frame, no UV,
  no per-draw matrix, and no differencing of the pattern itself (which
  would blur every crest to the 2x2 quad).
- **Filtered against the pixel footprint, and the remainder becomes
  roughness**: features below ~2 px fade out, and the slope variance the
  fade removes is added to the roughness (Toksvig) — a real 0.15 mm
  brushed lay on a part zoomed to fit is *supposed* to read as a
  direction-less sheen rather than as geometry. This is why the finish
  is stated in physical units and not as a normalised amplitude. In the
  Phong path the same quantity travels through the shininess slot.

### Reading a Phong appearance as PBR material data

Almost nothing in a FreeCAD document is authored as metallic/roughness.
What the branch is handed is an ordinary Blinn-Phong appearance -- a
diffuse colour, a specular colour and a shininess -- and how those three
are read decides what every existing model looks like under Realistic
shading. Two readings, both per draw, neither touching anything that was
authored (a stated metalness or roughness, a PBR-mode appearance, a
metallic-roughness map):

- **Shininess to roughness** is the classical microfacet match on the
  GGX *width*, `alpha = sqrt(2 / (n + 2))` for a Phong exponent
  `n = shininess * 128`. Roughness is the square root of the width
  because the BRDF squares it back (`a = rough * rough`, the glTF
  convention), so the conversion is the FOURTH root of that ratio --
  `fcRoughFromShininess`, with `fcShininessFromRough` as its inverse and
  `App::Material::shininessToRoughness` as the C++ copy. Handing the
  alpha over directly instead squares it twice and shades every ordinary
  surface as a mirror.
- **Specular colour to base colour and metalness** (`Render_PBRFromSpecular`,
  on by default) recovers what the branch has no slot for. Its
  reflectance is `f0`, built from the base colour and the metalness, so
  the specular colour is otherwise dropped -- and a classic Gold, whose
  gold-ness lives entirely in that colour, shades as yellow-brown
  plastic, while the presets built from a BLACK diffuse over a bright
  specular (Steel, Satin, Metalized, Shiny plastic) shade as black
  spheres. `fcBaseFromSpecular` is Khronos' specular-glossiness to
  metallic-roughness solve: the metalness for which the dielectric f0 of
  0.04 and some base colour reproduce the diffuse/specular pair, then the
  base colour recombined from both readings. It runs in the SHADER, not
  at translation time, because the base colour can arrive per vertex or
  per face; the engine asks for it by passing a negative metalness.

### What the branch costs

Measured 2026-08-19 on an RTX 3070 Ti Laptop (D3D12 via WSLg), one
1430x725 view, 1018692 covered pixels, every other effect pinned off, by
`scripts/pbr_cost_probe.py` + `scripts/pbr_cost_report.py`. Per
full-screen shading pass:

| leg | serialized clock | free-running clock |
| --- | --- | --- |
| Phong | 0.057 ms | 0.059 ms |
| PBR | 0.105 ms | 0.107 ms |
| PBR, `PBRFromSpecular` off | 0.111 ms | 0.109 ms |

**PBR costs 1.8x Phong per fragment** -- about +0.048 ms on a fully
covered 1080p frame, +0.09 ms at 1440p. The two clocks are independent
(one serialized by a readback, one free-running) and agree to 2%.

Two things follow, and the second is the one that decides anything:

- **`fcBaseFromSpecular` is free.** It was expected to be the expensive
  half, since it runs for every Phong-authored material -- which is
  nearly all of them -- and it does not show up above noise on either
  clock. Whether to read a specular colour as material data is a
  question about how models should LOOK, not about frame cost.
- **A desktop frame cannot see any of this.** It had to be amplified by
  ~800x overdraw before the GPU became what the frame waits for. At
  ordinary coverage the frame is bound by this renderer's own C++ and
  the Qt/Coin composite, and a 14x sweep of the covered-pixel count
  moved it by nothing at all. So on the desktop tier the shading model
  is not what a frame costs.

  It does not follow that the ratio is harmless everywhere. 1.8x is a
  ratio on fragment work, and the mobile and browser tiers are exactly
  where fragment work binds. The number to carry to that decision is the
  ratio, not the milliseconds.

### Environment (image based lighting)

PBR shading (`Render_PBR`) is lit by a prefiltered environment cubemap
plus its irradiance SH, built once per view on the CPU and rebuilt when
the source changes:

- Default source is a built-in **procedural environment**, computed on
  the CPU and fixed so frames stay deterministic. `Render_PBREnvPreset`
  picks which one: `Interior` (**default** -- one window and a ceiling
  panel against a dark surround, the crispest key of the five),
  `Studio` (four soft boxes on a dark surround), `Gradient` (the Z-up
  ground/horizon/sky ramp plus three cosine lobes this engine had
  before the others, kept so an older document can have its look
  back), `Overcast`, `Sunset`.

  All five are scaled to integrate to the **same mean radiance** over
  the sphere (0.565 in luminance, Gradient's). That is load bearing:
  choosing a preset changes contrast and structure and NOT how bright
  the scene comes out, so one exposure suits all of them. The scale
  constants in `envRadianceProcedural` were measured by integrating
  each shape over a uniform sphere -- edit a shape and its constant is
  stale.

  Why more than one: Gradient spans barely one stop peak-to-floor
  (about 12:1) and has no edges anywhere, so a smooth dielectric
  reflecting it shows the same flat grey at *every* roughness and
  nothing in the frame reads as a light source. Studio is about 370:1
  with rectangular sources, which is what makes a polished surface look
  polished. Rectangular and not a cosine lobe on purpose -- the edge is
  the point.

  `Overcast` weights its sky to the **zenith**, about 8:1 over the
  horizon where CIE's standard overcast distribution says 3:1. Same
  mean radiance as the rest, so the same light arrives -- it just
  arrives from higher up, which is what keeps the band immediately
  above the horizon dark enough to be a backdrop. An evenly bright
  dome cannot: forced to the common mean it is bright everywhere,
  including the part of it that fills the frame behind the model, and
  a near-white appearance like Plaster then has nothing to stand
  against.
- `Render_PBREnvImage` replaces it with a user image. A 2:1 image is
  read as equirectangular (lat-long), anything squarer as a GL sphere
  map — the convention Coin's `SoTextureCoordinateEnvironment` uses, so
  the same file works in the Tools → Texture mapping dialog's
  *Environment* mode. With the property empty the renderer falls back to
  that dialog's current image (`Config()["TextureImage"]`), then to the
  procedural environment.
- `Render_PBREnvBackground` draws the environment itself as the view
  background instead of the gradient quad, so reflective surfaces
  visibly mirror their surroundings. **On by default.** It gates the
  background pass ALONE -- the environment lights the scene either way
  -- so turning it off is how to have image based lighting over the
  ordinary background colour or gradient, which is a common enough
  thing to want that the Shading popup carries it as a checkbox beside
  the preset. The background pass keeps the scene
  matrices for it (`fs_fc_env` reconstructs per-pixel world directions
  from `u_proj`/`u_invView`); orthographic cameras get a fixed 45°
  virtual field of view since they have no per-pixel ray fan.

The scene's own ambient (Coin's `LIGHT_MODEL_AMBIENT`, i.e. the
viewer's `SoEnvironment`, 3.2) joins that environment rather than the
diffuse: `u_envAmbient` carries it as a uniform-radiance environment and
the branch adds it to BOTH the irradiance and the prefiltered term.
That is the only form which reaches a **metal** -- a metal has no
diffuse at all, so an ambient folded into `kd` would leave it exactly as
dark as before, which is what used to force `Render_PBREnvIntensity` up
to 3 to make metal read (and washed everything else out on the way).
Note the uniform is the light-model ambient *alone*, not Blinn-Phong's
material-ambient-times-it: the BRDF already states the surface, and a
material read as metallic/roughness has no meaningful ambient slot.
`u_pbrParams.w` does not scale it -- that knob says how bright the
user's environment map is, and this is a light beside it.

Note that FreeCAD has no other environment mechanism to honor: Coin's
`SoSceneTextureCubeMap` exists as a node class but is never instantiated
in the tree, and the Texture mapping dialog is the only place a user
picks an environment image today.

## 5. User shader framework

User-loadable shaders are document objects riding the standard
parametric loop. Full design history in `docs/RenderDebug.md` §6; this
section is the *authoring reference*.

### 5.1 Object model

- **`App::ShaderProgram`** — one program for one pipeline stage.
  Properties: `Stage` ("material", "water", "volume", "post"),
  `Dialect` (BGFX_SC / GLSL), `VertexProgram`, `FragmentProgram`
  (source text), `Blend` (Default / Alpha / Additive), `DepthWrite`,
  `Enabled` (false = skipped wherever its Shader resolves — the
  toggle for an effect's optional companion programs), plus
  user-added `Param_<Name>` dynamic properties (§5.5).
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
| `water` | `fs_fc_water`, the water-surface program — and binding one **activates** the water treatment (the target becomes a water body as if `Render_Water` were set: surface routing, scene copy, planar reflection, back depth, medium exemptions) | `ViewWaterSurface` | everything the stock water body leaves untouched; with the water pass set inactive (hidden-line, water shading disabled) the body renders stock |
| `volume` | the fire-channel medium of the body's slot — a per-point **medium function** (field + ramp), not a raymarch; binding one **activates** the fire treatment (proxy volume raymarches, geometry not drawn). The engine reassembles the shared volumetric raymarch / extinction / reflection-media programs with the user functions dispatched for the slot | `ViewVolGen`, `ViewVolApply`, `ViewReflMedia` | the march itself, temporal accumulation, cross-media compositing; with volumetrics inactive (no Shadow draw style) the body renders stock. Browser tier: the snapshot (v24) ships the assembled splice variants with server-compiled binaries; the viewer assembles the same sources locally and adopts the shipped entry by source match — stock flame stands in until the async compile republishes |
| `particle` | the mesh program of generated particle seed quads (§5.8 layout; the program's `Emitter*` properties drive seed generation per target on Object-scope bindings and per matched occurrence on Instance-scope bindings). Lives in its own cache and survives the outer-wins user-shader merge-down, so it coexists with the effect's main program | beauty passes, like `material` | like `material`; never the effect's main program |
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

**Water fragment program** (bind with an **Object-scope** Appearance —
a water body is whole-object by nature; Instance/Element bindings of a
water-stage program fall back to standard rendering)

```glsl
$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
#include "fc_user_water.sh"
void main() { gl_FragColor = fcWaterFragment(v_normal, v_vpos); }
```

The identity form reproduces the stock water surface exactly.
`fc_water_surface.sh` (which the helper includes) declares every
uniform and sampler the engine records with the surface draw —
refraction scene copy, planar reflection, water back depth, shadow
map, front volumetric — so custom effects can combine
`fcWaterShadeFragment` with their own `Param_*` uniforms or sample the
inputs directly. While a compile is pending or failed the stock
surface stands in, and the body *stays* a water body.

**Volume medium program** (bind with an **Object-scope** Appearance;
the body becomes an emissive volume — the fire channel)

```glsl
float fcMediumField(vec3 wp)   // temperature-like scalar in [0,1]
{
    return fcStockFireField(FC_MEDIUM_SLOT, wp);
}
vec3 fcMediumRamp(float t)     // radiance color of a field value
{
    return fcStockFireRamp(FC_MEDIUM_SLOT, t);
}
```

Unlike the other stages this is **not a whole program**: the source
defines exactly these two functions (plus any helpers/`uniform`
declarations) and is spliced into the engine's volumetric shaders —
the engine keeps owning the ray integration, soot extinction, temporal
accumulation and cross-media compositing (the Blender/Godot volume
model). No `$input`/`main()`/`#include` lines. `FC_MEDIUM_SLOT` is the
body's medium slot; every `fc_volume.sh` helper (`fcStockFireField`,
`fcStockFireRamp`, `cloudNoise`, …) is available, so the identity form
above reproduces the stock flame byte-exact. `wp` is world-space;
field animation rides the engine's flame clock through
`fcStockFireField`, or `u_fcTime` for custom motion. With several
distinct user media in one scene the sources share one translation
unit — keep helper names unique.

Defining **`fcMediumScatter`** instead selects the **scattering
channel**: the body becomes a fountain body (flow frame from the
placement, splash rings on an underlying water surface) whose spray
density the function supplies —

```glsl
float fcMediumScatter(vec3 wp)  // scattering density at a point
{
    return fcStockCloudField(FC_MEDIUM_SLOT, wp);
}
```

— the identity form reproducing the stock fountain byte-exact. The
channel is chosen by which contract functions the source defines:
`fcMediumScatter` = scatter/fountain, `fcMediumField` +
`fcMediumRamp` = emissive/fire.

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
| `u_params` | vec4 | `.x` per-vertex color, `.y` lighting on, `.z` two-sided, `.w` constant NDC depth bias — glPolygonOffset's `units * r` term |
| `u_polyOffset` | vec4 | `.x` glPolygonOffset's `factor` (0 = no slope term), `.y` ceiling on the depth gradient the slope term tracks (NDC depth per NDC screen unit) |
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

**Stateful emitters.** *Implemented.* Position-as-`f(seed, t)` cannot
express anything whose next frame depends on the last: a collision, a
respawn, a force field, drag. Giving particles state normally means
compute shaders and storage buffers — which the browser tier does not
have (WebGL2 is GLES 3.0; compute is 3.1), and a simulation that only
runs on the desktop is a different effect on every other target. So
the state lives where every tier can write it: **a ping-pong pair of
RGBA32F render targets, advanced by an ordinary fragment pass.** One
texel is one particle, one fragment program is the whole solver, and
desktop and browser run the identical program.

- **State**: two attachments per buffer — `s_pstate0` = xyz position
  in the emitter's model space + age, `s_pstate1` = xyz velocity +
  lifetime. Grid is `min(count, 256)` wide. Point-sampled; the state
  is data, not an image.
- **Step**: `App::ShaderProgram.SimulateProgram`, a fragment program
  run over the grid once per **fixed** step (`EmitterRate`, default
  60/s). It reads the previous state and writes the next through
  `fc_particle.sh` (`fcParticleLoad`/`fcParticleStore`,
  `fcParticleSpawn`, `fcParticleStep`). A fixed step is what makes the
  motion identical across machines; a frame too slow to afford its
  steps lets the simulation fall behind rather than stretching them,
  because a stretched step is a different simulation.
- **Impacts**: a step may *report* as well as remember. A third
  attachment carries what the step struck — `fcParticleHit(pos,
  strength)` written through `fcParticleStoreHit`, or
  `fcParticleNoImpact` for a step that struck nothing. It is a per-step
  event, not a state, so a droplet reports on the step that reaches the
  pool and not for as long as it floats there. The engine turns the
  per-particle reports into per-*place* ones (below), which is the form
  a water surface can read: a surface has no way to visit fourteen
  hundred droplets per pixel. The report travels as a macro argument
  rather than through a variable the header owns, because a mutable
  file-scope global does not survive the runtime translation of a user
  shader — the program silently fails to build and the emitter falls
  back to its stateless stage, which looks exactly like an emitter that
  draws nothing.
- **Impact map**: one texel per cell of the water's world footprint,
  holding the most recent hit there (where, when, how hard) — an
  RGBA32F target the splat pass (`vs/fs_fc_pimpact`) writes one
  quad-per-particle into, immediately after the steps and before
  anything draws. Deliberately never blended, and cleared only when
  what it holds stops being about anywhere — an emitter replaying its
  history from a reset, or a footprint that moved. Otherwise a record
  is a standing statement that something struck this place at this
  time: a newer hit in the same cell simply overwrites it, and an
  expired one ages out on its own when the surface compares it against
  the clock. Clearing on the reset is what keeps a frozen frame a pure
  function of the warm-up with water in the scene as well as without. The exact world position rides in the texel rather than
  being implied by its address, so a ring is centred on the hit and not
  on the cell that caught it. The water surface raises its rings from
  it (§5.11, `Render_WaterImpactStrength`/`Life`) on top of whatever
  the ambient ripple field is doing — including nothing:
  `Render_WaterRippleType = None` stills that field and leaves the
  rings as the only motion, which is the pool that is glass until a
  droplet lands in it. The impacts are events and the ripple types are
  patterns, so they are separate switches; `Render_WaterWaveStrength`
  remains the amplitude both are measured in. `RenderDebug_ViewMode
  = 10` shows the map itself, which separates "nothing was reported"
  from "reported in the wrong place" from "the surface fails to show
  what is there".
- **Draw**: the beauty vertex stage reads the same texel by vertex
  texture fetch (`fcParticleUV(a_normal.z)` → `fcParticleLoad`), so
  the look and the motion stay in step without either knowing how the
  other works.
- **Reset**: the stock `fs_fc_psim_init` seeds birth state
  deterministically from the emitter seed, staggering ages over one
  lifetime so the emitter is in full flow at t = 0. Deliberately not
  user code — a reset a step program could get wrong would take the
  determinism guarantee with it.
- **Clock rate**: `EmitterTimeScale` (default 1) is how fast the
  emitter's clock runs against the wall clock. It scales the *target*
  the simulation is asked to reach, never the step: each step still
  advances the same slice of the trajectory, there are simply more of
  them per second. So the shape of the motion is untouched — a jet's
  apex is `v²/2g`, which the clock does not enter — and only its pace
  changes. It exists because doing that by hand means scaling launch,
  gravity (by `k²`), drag, lifetime, stagger and the step rate in
  concert, and getting one of them wrong changes the shape too. The
  budget still applies: `k × EmitterRate` steps a second have to fit
  in `kParticleSteps` per frame, or the emitter falls behind. Editing
  the scale live does not reset the state — the lag allowance (also
  scaled) absorbs the jump, so the emitter resumes at the new pace.
- **Freeze-frame**: `EmitterWarmup` seconds are simulated from the
  reset before a frozen frame draws, in steps the same length as the
  live ones, over as many frames as the per-frame budget needs (the
  view keeps reporting `animating()` until the warm-up lands). Frozen
  state is therefore a pure function of (seed, count, program,
  warm-up × time scale) — never of how long the session has been
  running — and a
  warm-up shorter than what the state already ran rewinds to the reset
  and replays. That is what keeps stateful effects golden-image
  comparable (`scripts/user_shader_particles_state.py`).
- **Identity**: state is keyed by `DrawCall::objectKey`, so two
  occurrences of one emitter simulate independently and a moved
  emitter keeps its particles. A changed step program, particle count
  or clock mode is a different simulation and resets.
- **Transport**: the step rides as the program's *second*
  `SoFragmentShader` (Coin's node triple has room for two sources, and
  this needs three) and as `UserShader::simulateSource`; the snapshot
  ships its viewer binary in `Compiled::simBin` (v39). The emitter's
  count/rate/warm-up/margin/time-scale reach the backend as the
  reserved `fc_emitter` parameter (two vec4s since the time scale),
  the same no-new-fields channel as `fc_state`. A reader older than a
  lane stops short of it and takes the default, which is what it would
  have used anyway.
- **Budget**: `kParticleSlots` (3) stateful emitters per view ×
  `kParticleSteps` (2) steps per frame, plus one shared view for the
  impact splat — every emitter and every step of the frame splat into
  the one map, so the whole of it costs a single id. This is bgfx
  view-id budget — see [§3.1](#31-the-view-id-budget); these ids are
  claimed only by a frame whose scene actually carries a stateful
  emitter. The state views come **first** in id order, before anything
  that draws.
- **Fallback**: without a color-renderable RGBA32F (a WebGL2 context
  lacking `EXT_color_buffer_float`), over the slot budget, or while
  the step program is still compiling, no state is bound and the
  vertex stage runs its stateless path. A stateful effect degrades to
  a stateless one; it never disappears and never draws black.

What this does *not* reach is neighbour queries — SPH and friends need
a sorted spatial hash, which is the one thing that genuinely wants
compute. Everything else a particle system does (gravity, drag,
collisions, forces, spawn/death) is expressible here.

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
motion, particles, particles-state; browser legs via headless Chromium
against a
serving backend). The golden-image harness
(`scripts/render-verify.sh`) restages captures byte-exact under
freeze-frame. Policy: every framework change lands with a suite, no
throwaway probes (`docs/RenderDebug.md` §0).

### 5.11 Effect library (design settled 2026-07-26, implemented)

The built-in water / fire / fountain effects are re-expressed as
pre-bundled user shaders — the real-world test of the whole framework
and the template for adding effects later. Decisions:

**Stage model — fixed named stages, graph-ready naming.** Two new
stages join `material`/`post`:

- **`water`** — the surface fragment program of a water body. When a
  bound draw carries a water-stage program, the engine runs the water
  pass set it owns today (planar-reflection re-render, scene-color
  copy, water-back depth) and binds their outputs
  (reflection/refraction/back-depth samplers) for the user program,
  which replaces `fs_fc_water` for that draw.
- **`volume`** — a per-point **medium function**, not a raymarch:
  fire/fountain shading lives in the shared half-res multi-slot
  volumetric raymarch (`fs_fc_volume.sc` — temporal accumulation,
  fire-over-water segment compositing), so there is no per-body
  program to swap. Following the industry shape (Blender volume
  closures, Godot `fog` shaders, Unreal/Unity volume materials: the
  engine owns the march, user code supplies density/emission at a
  sample point), a volume-stage program supplies a medium function
  (world position, time, body params → emission + extinction); the
  backend assembles a variant of the stock volumetric shader
  dispatching it for the bound body's slot through the user-compile
  cache. The stock fire and fountain media are re-expressed through
  the same seam, so the bundled effects reproduce stock byte-exact
  and cross-media compositing keeps working.

The engine keeps owning pass orchestration; a stage name is a *slot*
identifier and the helper-lib function API (`fc_user_water.sh`,
`fc_user_volume.sh`, alongside `fc_user_lighting.sh`) is the authoring
contract — engine-recorded uniforms stay semi-stable per §5.4. A
future generalization to user-declared passes (a render graph) may
come later; stage names and helper contracts are chosen so existing
effects would survive it as pre-wired slots.

**Activation = binding.** An `App::Appearance` binding a Shader with a
water-stage program *makes the target a water body*; no separate
switch. The legacy per-object `Render_Water`/`Render_Fire`/
`Render_Fountain` view properties remain as a parallel path; effect
tuning for the bundled effects moves to `Param_*` properties on the
ShaderProgram (overridable per binding as usual).

**Packaging — effect-package directories + factory.** Effects ship as
a resources `effects/` directory, one folder per effect: a manifest
(programs, stages, defaults, which programs start disabled) plus plain
`.sc` sources that `#include` the shipped helper libs. A user-level
effects directory is scanned the same way, so a user-authored effect
is just another folder. No bundled `.FCStd` library document.
*Implemented*: packages live at `src/Gui/Renderer/effects/<name>/`
(installed to `share/Renderer/effects`, user dir
`<appdata>/Renderer/effects` wins on name clashes); the factory is
`freecad.rendereffects` (`list_effects()` / `activate(name, targets)`
/ `deactivate(look)`); the manifest schema is documented in that
module. Manifest `viewProps` switch required boolean view toggles on
at activation (e.g. `Render_WaterSurface`, which defaults off).
Bundled: `water`, `fire`, `fountain`, `rain`, `sparks`, `waterjet` —
the first three byte-identical to their stock `Render_*` treatments,
the rest particle-first packages built on the same framework.

**Persistence — copy on activation.** Activating an effect
instantiates its `ShaderProgram`/`Shader` objects (and the binding
`Appearance`) *into the user's document*. Documents stay
self-contained and portable (shared files, other installs, the WASM
tier); picking up a newer bundled version is an explicit re-import,
never an implicit central upgrade. No XLink into a shipped library
document.

**Per-program toggle — `Enabled`.** *Implemented*:
`App::ShaderProgram.Enabled` (default true) is honored everywhere a
Shader's program list resolves — object/water/volume binding, post
lists, direct attachment, the demo preview — and toggling it re-poke's
the bindings. Each bundled effect ships its particle companion program
disabled by default; enabling the particle effect is flipping that
property.

**Particle companions — target-fit emitters.** *Implemented*: a
program with `EmitterCount > 0` is a particle companion (never the
effect's main program). Each such enabled program gets its own
seed-quad geometry generated per target (`buildEmitterSeedNodes`, the
§5.8 seed layout), fit to the target's bounding box scaled/offset by
`EmitterSpread`/`EmitterOffset` (in bbox-size units — spray sits on
the pool's top face, embers over the flame body). On an Object-scope
binding the seeds attach with the program in an own separator at the
target's view-provider root (so every instance of the target carries
them); on an Instance-scope binding they are generated per matched
occurrence — bounds from the occurrence chain's accumulated transform
— and attach at the occurrence's top-level scene instance, so only
the bound occurrence(s) emit. Element-scope bindings carry no
emitters (a face restriction has no emitter volume). `EmitterMargin` adds travel headroom, carried as data on the
reserved `fc_emitter` parameter rather than as geometry: the renderer
widens what it culls an emitter against, so billboards displaced out
of the seed box are not dropped once the anchors leave the view. It
deliberately does not enlarge the seed box, because framing, spawning
and streaming all want the tight box (§7). Bundled: `Embers` on
the fire effect, `WaterSpray` on the water effect — both additive,
depth-write off, stateless (`u_fcTime` + seed attributes), tuned via
`Param_Rise`/`Param_Size`.

Implementation order: water stage (identity program == stock water —
**done**), volume stage (identity == stock fire / stock fountain —
**done** for both the emissive and scattering channels), packages +
factory + `Enabled` (**done** — water, fire and fountain ship),
particle companions (**done** — Embers / WaterSpray / Droplets,
target-fit emitters). Rain, `sparks` and `waterjet` ship as
**particle-only** packages — no main-stage program at all, the enabled
emitters are the whole treatment, demonstrating that the particle
framework carries an effect by itself. The browser-tier splice transport is **done**
(snapshot v24: assembled variants + viewer binaries in the shader
table, adopted by source match). Instance-scope particle emitters are
**done** (occurrence-fit seeds, particle-only effects bind at
Instance scope too).

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
  prefix). They ride the same config spine as the
  `Render_*`/`RenderShadow_*` overrides.
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
  `Render_*/RenderShadow_*/RenderDebug_*/HiddenLine_*` props) -- enough to
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
  shows the artifacts listed in §5.3. Emitter culling headroom is
  handled by `EmitterMargin` (§5.11); a stateful emitter whose
  simulation carries particles beyond that margin can still be culled,
  because the margin is authored once and the state is not.
- A stateful emitter's spawn volume is the emitter geometry's own
  bounds inverse-transformed into model space (§5.8) — the seed box,
  and under a rotating model matrix the axis-aligned hull of it, which
  is bigger than the seed box but never smaller. `EmitterMargin` is
  not part of it: the headroom widens culling only, so spawning
  matches the `fcParticleSpawn` contract and a view fit frames what
  the emitter actually occupies rather than its travel allowance.
- Neighbour queries (SPH-class fluid) stay out of reach without
  compute shaders; see the §5.8 closing note.
- Stage names are fixed slots (`material`/`water`/`volume`/`particle`/
  `post`) — there is no user-declared pass or render graph; an effect
  cannot introduce a pass the engine does not already own.
- WBOIT draws cannot take a user fragment program (the OIT output
  contract is not a single color).
- Per-draw texture slots for user shaders (custom images) are not yet
  bindable from the document model.
- **Coin light state that never reaches a backend** (audited
  2026-08-14). The audit's one outright bug -- the scene light being
  gated on the shadow map -- is fixed, see `docs/CoinRetirement.md`
  3.4, and the ordinary lights now cross as `Render::ViewLightConfig`
  (`translateViewLightConfig`, up to Coin's own cap of eight, carried
  in the scene dump from v52). What is left:
  - ⚠️ **A light has to sit above the render-cache root.**
    `translateViewLightConfig` reads `SoLightElement` from the frame
    state at the renderer level, which sees only what was traversed
    *above* the capture root. A light a ViewProvider puts in the
    geometry graph is inside the captured subgraph and never appears
    there -- the same boundary that forces the Shadow style's light
    above the cache root (3.4). Carrying those would mean collecting
    lights during cache capture, which is a feature, not a fix.

    The real root is reachable from Python, so adding a light by hand
    is a two-liner -- note that `getSceneGraph()`, on either the view
    or the viewer, returns the `SoFCUnifiedSelection` *below* the
    capture point and is the wrong handle:

    ```python
    rm = Gui.ActiveDocument.ActiveView.getViewer().getSoRenderManager()
    root = rm.getSceneGraph()   # Separator: backlight, headlight,
                                # camera, viewerLightingRoot,
                                # SoFCUnifiedSelection, ...
    root.insertChild(light, 3)  # after the camera = world coordinates
    ```

    Insert *before* the camera and the light's direction is fixed in
    eye space and tracks it, which is exactly how the headlight is
    built; *after* it, position and direction are plain world space.
  - **`SoSpotLight` past the first, closed 2026-08-14.** The first one
    is claimed as the scene light (with cone, shadow map, sun disc and
    ground) and the walk stops there, so every spot after it is now an
    ordinary view light with its cone and no map -- as is a second
    `SoShadowDirectionalLight`, which falls through to the plain
    directional branch. Which node the scene light took is decided by
    the same walk in the same order under the same `on` filter, so the
    two translators agree without either seeing the other.

    The cone did **not** fit the two spare floats this section used to
    promise. A spot needs its axis as well as its position, which is
    three floats more than a slot holds, so `kind = 3` takes the
    **next slot whole** (axis in xyz, `kind = 4` marking it a
    continuation the shader shades as a light of zero colour). That
    keeps the uniform budget exactly where it was -- the alternative,
    a fourth per-light array, would have charged 8 vec4 of the 40
    spare to every frame for something a scene almost never has. A
    spot therefore costs two of the eight light slots, and the packer
    never starts one in the last.
  - **The specular 0.75, dropped 2026-08-14.** Measured against Coin at
    last, and it was exactly what it looked like: a pure specular ball
    peaked at **190** where GL's law saturates (255 x 0.75 = 191;
    Coin's own 237 is a Gouraud sample of a saturating highlight, since
    fixed-function shades the peak per vertex). The same measurement
    found a second half to it -- the term had no `f` gate, GL's rule
    that a highlight needs `N.L > 0`, and ran on `abs(dot(n, h))`, so a
    broad lobe carried the specular past the terminator: **4x** Coin's
    spill on to a ball's dark side at shininess 0.05 (+3300 px against
    +826). Both are fixed on the Coin-fed branches, which now run GL's
    equation as written: the peak reads 254 and the spill is gone
    entirely (the dark side returns to its unlit baseline, where Coin
    keeps its +826 of Gouraud smear). Highlights on strongly specular
    materials are a third brighter than before, and that is the
    correction, not a regression. Nothing moves on a stock appearance,
    whose specular colour is black.

    The **effect lights keep their 0.75** deliberately: a fire flame or
    a `Render_Light` bulb has no light node behind it and no GL term to
    match, so that weight is a tuned one rather than a parity claim.

  **The viewer's rig, closed 2026-08-14.** The fork's viewer had two
  lights (headlight, backlight) and no ambient node of its own, so
  `SoEnvironmentElement` always read Coin's default 0.2 grey. It now
  carries upstream's full rig: a **fill light** and an
  **`SoEnvironment`**, both under a `viewerLightingRoot` group that
  `setSceneGraph` inserts *after the camera* -- world space, but still
  above the render-cache root, which is what lets a backend see them
  (the bullet above). The fill light hangs in a `SoTransformSeparator`
  under an `SoRotation` slaved to the camera's orientation
  (`connectFrom`, re-slaved by `syncLightRotation` when `setCameraType`
  swaps the camera node), so its direction stays camera-relative while
  the geometry below is left alone. Preferences, all in the `View`
  group beside the existing ones: `EnableFillLight` (**off** by
  default in the fork, where upstream defaults it on -- the fork's
  reference renders are lit by one light), `FillLightColor`,
  `FillLightDirection`, `FillLightIntensity`, `AmbientLightColor`,
  `AmbientLightIntensity` (default 20, i.e. Coin's own 0.2, so the node
  alone changes nothing). The renderer side needed no change at all:
  `translateViewLightConfig` already took any plain directional light,
  the `getMatrix` unwinding already handled the transform separator,
  and the ambient already read `SoEnvironmentElement`.

  **Ambient, closed 2026-08-14.** `Material::ambient` used to cross the
  bridge, get keyed into the bgfx material key, get streamed -- and
  then be dropped, because `fcShadeFragment` spent a literal
  `0.2 * base` instead. That fraction-of-the-diffuse floor has no
  counterpart in Coin, whose ambient is the material's ambient colour
  times `LIGHT_MODEL_AMBIENT` (`SoEnvironment`, default 0.2 grey),
  added outright. The two coincide only when a material's ambient
  equals its diffuse.

  ⚠️ The fix is a pair, not a single term: the `0.2 / 0.8` split was
  *tuned* -- the low diffuse weight paid for the high ambient, so the
  two errors cancelled on lit surfaces and the divergence only showed
  where light did not reach. Correcting the ambient alone would have
  darkened every render. With Coin's ambient and a full-weight diffuse,
  measured against cache-0 on the same scene (mean colour of the
  object's pixels):

  | | Coin | bgfx before | bgfx after |
  |---|---|---|---|
  | default material, headlight off | 10 | 40 | 10 |
  | default material, headlight on | 133 | 138 | 133 |
  | red ambient / grey diffuse, off | (50,0,0) | (30,30,30) | (49,0,0) |
  | red ambient / grey diffuse, on | (142,93,93) | -- | (142,93,93) |

  A feed that carries no Coin lighting (`ViewLightConfig::fed` false --
  an old scene dump, a consumer predating v52) keeps the legacy floor
  and its 0.8 diffuse weight, so old dumps render as they were drawn.
  The PBR branch is untouched: image-based irradiance replaces the
  ambient term there, which is the glTF semantics it follows.
