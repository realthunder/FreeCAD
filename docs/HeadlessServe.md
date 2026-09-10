# Headless serving — publishing a scene with no 3D view

Status: stages 1, 2a-2c and most of 2d are **implemented**; two open items are listed under
2d in §4. Stage 1 (a serving process does not rasterize) is released on `LinkVibe`; the
rest is on the branch. A document can now be served with no 3D view, no display and no GL
driver in the process — see §4, stage 2c.

Companions: [SceneStreaming.md](./SceneStreaming.md) §2.1 (what a serving process does per
frame, after stage 1), [ComputeBoundaries.md](./ComputeBoundaries.md) (the headless-engine
direction this is the rendering half of), [RenderEngine.md](./RenderEngine.md) §2 (the
snapshot the publish writes), [ThinClient.md](./ThinClient.md) §4.2 (the control channel).

---

## 1. Where stage 1 left it

A backend launched with `FC_BGFX_SERVE_SCENE=<port>` publishes the scene to remote viewers,
which draw it themselves on their own clock. Stage 1 established that such a process has no
reason to draw: `BGFXRendererP::render()` takes a publish-only exit right after the publish
block (`BGFXRenderer.cpp:8752`), before every GPU pass, whenever `localAudience()` is false.
The measured effect on the demo-fountain rig was 560% of a core down to 2.0%.

What stage 1 did *not* remove is the 3D view. The process still opens a `View3DInventor`,
still needs a `QOpenGLWidget` with a live GL context, still initializes bgfx on it, and under
Xvfb that means llvmpipe is loaded and a framebuffer is allocated for a window nobody
watches. Concretely, `render()` returns early at `BGFXRenderer.cpp:8323` if
`_BGFXLib.getView(widget, type)` fails, and again if `view->bgfxFbo` is invalid — so the
publish, which is entirely CPU work, is gated behind a GPU that is never asked to draw.

The user's ask is `App.openDocument(path, hidden=True)` — documented at
`src/App/ApplicationPy.cpp:107`, and it already works: `createView=false` reaches
`Gui::Application::slotNewDocument`, which skips `createView(View3DInventor)` at
`src/Gui/Application.cpp:949`. The document loads, recomputes, and is scriptable. It just
does not serve, because everything that produces a feed hangs off the view.

## 2. What the 3D view actually supplies

Five things, and it is worth separating them, because they are not equally hard to replace.

**(a) The scene graph root.** `View3DInventorViewer` owns `selectionRoot`
(`SoFCUnifiedSelection`), calls `setDocument()` on it, and hangs the document's view
providers beneath it. This is document state presented as a graph; nothing about it is
per-view except that the viewer is where it is currently constructed.

**(b) The traversal that fills the render caches.** `SoFCRenderCacheManager::render(action)`
(`SoFCRenderCacheManager.cpp:1217`) rebuilds the cache whenever the root's node id changes,
then pushes the result into `SoFCRenderer::setScene()`, which translates it and calls
`Renderer::setScene()` (`SoFCRenderer.cpp:1152`). **This is the important finding of the
design pass: that path is already GL-free.** The rebuild seeds an `SoFCRenderCache` from the
action's `SoState` and then traverses with an *`SoCallbackAction`*
(`PRIVATE(this)->action->apply(root)`) — `SoFCRenderCache::open()`
(`SoFCRenderCache.cpp:913`) reads Coin elements and makes no GL call, and the vertex caches
are built from `generatePrimitives`, on the CPU. The manager already has a drawing-free entry
point in `capture()` (`SoFCRenderCacheManager.cpp:1263`), written for overlay roots: "same
cache build as `render()`, but without any drawing".

So the feed is **push-based and CPU-side**. It is not produced by drawing a frame; it is
produced by a change to the graph, and drawing merely happens to be what currently calls it.

**(c) The per-frame config push.** The exception to (b). `SoFCRenderer::render(action)`
(`SoFCRenderer.cpp:2337-2384`) pushes ~20 configs — AO, PBR, water, bloom, user shaders,
light, hidden-line — into the backend on every frame. Most read from `externalview` (a
`View3DInventor *`, for its view properties) or from global parameters; only three read
`action->getState()`: `translateHiddenLineConfig`, `translateLightConfig` and
`translateAutoZoomScale`.

**(d) The camera, and the window size.** The snapshot carries `viewMatrix`, `projMatrix`,
`width`, `height` and `clearColor` (`BGFXRenderer.cpp:8462-8467`). Viewers navigate with
their own camera, so these are the *initial* framing a joining viewer adopts, not something
the stream needs to keep current.

**(e) The three handler installations**, all in `View3DInventorViewer::setRendererType()`
(`View3DInventorViewer.cpp:4002-4031`): the pick handler (marshals a viewer's world ray to
`pickAndSelect()` against this viewer's graph and camera), the control channel
(`installSceneControlHandler()` — which, checked, is *already* view-independent: 581 lines in
`SceneControl.cpp` with no reference to a viewer, working directly on the document from the
GUI thread), and the work notifier (`scheduleRedraw()`, so a finished level job gets
published).

**(f) The selection observer -- missed by this inventory, found 2026-09-06.** The viewer
is a `SelectionObserver`, and `View3DInventorViewer::onSelectionChanged` is what hands every
selection and preselection change of its document to the selection root as an
`SoFCSelectionAction` / `SoFCHighlightAction`; the root's `checkSelection` then feeds the
render cache manager, which is where the selection draws on the wire come from. The source
built its root, set its document and installed the pick handler, and a remote pick duly
landed in `Gui::Selection` on the GUI thread -- and no publish ever showed it, because
nothing told the root, and because the root's renderer branch bailed out without a viewer
to build the detail path from (`SoFCUnifiedSelection.cpp`, `beginDetailPath`: with a
viewer, the viewer's groups are prepended; with none, the provider's root is a direct
child of the selection root and nothing is). The measurement of `ThinClient.md` sec 8.1
found both; `SceneServeSource::Private::SelectionMirror` is the observer now, and
`tests/gui/serve-selection-echo.py` is the loop over a socket.

Plus the overlays — axis cross, navigation cube — which are viewer furniture captured through
`setExternalOverlay()`. A headless source has no business producing them; the viewer draws its
own.

## 3. The shape of the fix

Introduce `Gui::SceneServeSource`: a document-scoped, view-less publisher. One per served
document, created when `FC_BGFX_SERVE_SCENE` is set and the document has no 3D view.

```
SceneServeSource
  ├─ SoFCUnifiedSelection  root      — setDocument(), view providers beneath  (a)
  ├─ SoFCRenderCacheManager          — traverse() on change, no drawing        (b)
  ├─ Render::Renderer      renderer  — created in publish-only mode           (c,d)
  ├─ SoCamera              camera    — synthetic, for the joining framing      (d)
  └─ handlers: pick / control / work                                          (e)
```

Three changes make it possible, in this order.

### 3.1 A publish entry point on the renderer that does not touch the GPU

`Renderer::publish()` — everything `render()` does from its top down to the end of the publish
block, and nothing after it. In `BGFXRenderer` this means factoring the region
`BGFXRenderer.cpp:8403-8729` (`makeSnapshot` plus the serve block) out of `render()` so both
callers share it, and skipping the `getView()`/`bgfxFbo` gate entirely. The audit says this is
clean: `makeSnapshot` reads only CPU members (`scene`, `objectInfo`, `selections`,
`highlight`, `overlays`, the configs), and the one call inside it that looks like a GPU
dependency -- `_BGFXLib.viewerShaderBins()` (`BGFXRenderer.cpp:13654`; now `shipUserShader()`) -- is offline `shaderc`
invocation plus file reads, no bgfx device. `width`/`height` and `clearColor` become
parameters rather than view state.

This is also the point at which the `FC_BGFX_SERVE_DRAW=1` opt-in keeps working unchanged: a
process with a real view still calls `render()`, which still calls the shared publish.

The renderer factory takes a `QOpenGLWidget *` (`Renderer.h:1336`). A publish-only renderer
passes null, and must not initialize bgfx at all — `RendererFactory::create()` gains a mode
flag rather than relying on a null widget being handled everywhere by accident.

### 3.2 A traversal driven by change, not by a frame

`SceneServeSource::traverse()` builds the caches the way `capture()` does. The open question
it has to answer is what seeds the initial `SoState`, since `SoFCRenderCache(state, root)`
wants one and today it comes from an `SoGLRenderAction`. Two candidates:

- **Seed from the `SoCallbackAction`'s own state.** Truly headless — no GL library loaded, no
  Xvfb, runs on a server with no GPU at all. The risk is any element `open()` or the
  translate reads that only an `SoGLRenderAction` sets up; the audit found none, but that is
  a read of `open()`, not proof across every view provider.
- **Seed from an `SoGLRenderAction` bound to an offscreen context.** bgfx's desktop path
  already owns a `QOpenGLContext` + `QOffscreenSurface` (`BGFXRenderer.cpp:795-796`), so the
  machinery exists. Safe, but it keeps a GL driver in the process — which is most of what
  stage 2 is trying to shed.

Recommendation: build the first, keep the second as a `FC_SERVE_GL_STATE=1` fallback until the
first is proven on a real model. The whole value of stage 2 is a server that needs no GPU.

Scheduling: the traversal runs when the graph changes. `SoFCRenderCacheManager` already
detects that by node id, so the source needs only a coalescing trigger — a zero-timer armed
from the document's change signals and from the work notifier — instead of `scheduleRedraw()`.

### 3.3 New homes for the per-frame configs and the handlers

The config push (2c) moves to a `SceneServeSource::pushConfigs()` called before each publish.
The `externalview`-driven ones need a source of view properties with no `View3DInventor`;
the natural answer is that the serve source *is* that source, holding the same properties
(`Render_AO`, `Render_WaterAbsorption`, …) so the existing control-channel edits keep landing
somewhere. The three that read `action->getState()` need either a state to read or a documented
headless default — hidden-line and autozoom are draw-style settings that a headless publisher
can carry as plain values; the light config is the one that genuinely wants a traversal, and
it can be gathered by the same callback action that builds the caches.

Picking (`pickAndSelect`, `View3DInventorViewer.cpp:4039`) needs a camera as a traversed child
and the scene root — both of which the source has. It moves over nearly verbatim, using the
synthetic camera. The control channel needs no change at all beyond being installed from the
source. The work notifier arms the traversal trigger.

### 3.4 The camera

A headless source has no camera of its own to report. The proposal is to synthesize one at
first publish: an `SoOrthographicCamera` fitted to the scene bounding box (the cache manager
has `getBoundingBox()`), at a fixed isometric-ish orientation, with a default 1280×720
viewport. Every joining viewer already re-frames with its own `fitAll`, so this only has to be
sane, not correct. Making it settable over the control channel is a follow-up.

## 4. Staging

Each stage is separately verifiable and separately committable.

**2a — `Renderer::publish()`.** Factor the publish out of `render()`; no behavior change.
Verify: existing rig unchanged, `changeprobe.py` still sees a republish on a `Render_AO` edit,
`snapprobe.py` still decodes the frames.

**2b — publish-only renderer construction.** `RendererFactory::create()` with no widget and no
bgfx init, driven by a unit-ish test that constructs one, feeds it a hand-built `DrawCallList`
and asserts the snapshot serializes. This is where "no GPU in the process" is first provable —
check with `lsof`/`ldd` that no GL driver is mapped.

**2c — `SceneServeSource` with the GL-free traversal. Done.** The load-bearing stage.
Verified as specified: the same document published through a real serving viewer and through
the source, diffed with `fcscenediff` — 120 draws, 40 objects, 8310 vertices, every object
matched by content and every config block equal. Identical but for camera, viewport and
overlays, which differ by design. The harness that decides this is §4.1.

The seed question of §3.2 resolved to the first option, the truly headless one, and the audit
turned out to understate the case: `SoAction::getState()` builds a state on demand from the
action's default elements, so a plain `SoCallbackAction` supplies one with no context and no
drawable, and *every nested separator's cache is already opened against exactly that state
today*. The `FC_SERVE_GL_STATE=1` fallback was never needed and is not implemented.

Two things the frame loop owned had to move rather than be dropped: the per-frame config push
(§3.3), now `SoFCRenderer::pushExternalConfigs()`, run before the change check because editing
a config moves no node id; and the section hatch, shared out of the viewer. Without them the
scene matched but `background`, `aoconf`, `waterconf`, `preselconf`, `selconf` and `hatch` all
sat at defaults — the diff named them one by one.

⭐ **Measured with no display at all** (`DISPLAY` unset, `QT_QPA_PLATFORM=offscreen`, no
Xvfb): publishes the same scene, at 0.0% CPU, with **no GL library mapped into the process** —
no `libGL`, no `libEGL`, no `swrast`, no `llvmpipe`, no `dri`. That is the whole point of
stage 2, and it is what makes the acceptance test of 2d a formality rather than a hope.

**2d — handlers and the Python surface. Done.** Pick, control channel
and work notifier are installed by the source, and the entry point is `Gui.serveDocument(doc,
port)` — a non-zero port starts the stream server directly, so serving no longer depends on
`FC_BGFX_SERVE_SCENE`. Verified against a backend with **no Xvfb and no display**: the stream
decodes (`snapprobe.py`), and a control-channel property edit is accepted and republishes.

The per-view render overrides moved with it. Everything that reads them only ever calls
`getPropertyByName`, so the plumbing now takes an `App::PropertyContainer` and the source
holds its own — which is what makes the control channel's `view3d` subject answerable with no
view. It notifies the source on change, because with no frame loop an edit that nothing
listens for never reaches the wire.

**Closed item 1: the "flaky incremental push" was the probe, not the backend.** The symptom
was real — `changeprobe.py` alternated PASS/FAIL about 1 run in 3 — and the earlier reading of
it was wrong in a way worth recording, because it accused the one component that was innocent.
The delta is *never* dropped. Counting the raw bytes the watcher socket delivers settles it:
on a failing run the backend sends the same 691-byte delta it sends on a passing one, and the
probe fails to parse it.

The defect was in `frames()` (`snapprobe.py`), the helper both probes read the socket with. It
copied `ws.buf` into a local and never wrote the remainder back, so every byte it had read but
not yielded died with the generator. A caller that watches in windows — one before the edit,
one after — therefore resumed the second window mid-frame and parsed lengths out of payload
bytes, after which nothing it saw was ever a frame again. The trigger is timing: window 1 has
to expire while the 27 KB initial snapshot is still arriving (measured on a failing run: 23104
of 27071 bytes read), which is exactly the sort of thing that varies run to run. ⚠️ The
generator also has to consume before it yields, since the caller may abandon it at any yield.

Two lessons for anything else built on these probes. A framed protocol needs its read buffer
to live on the connection, not in whatever is parsing it this second — a lost partial frame
does not look like a lost frame, it looks like a backend that went quiet. And `snapprobe.py`
ran its `main()` at import, so every probe importing `frames` opened a second viewer and held
it for 20 s; that is fixed too (`if __name__ == "__main__"`). With both fixed, `changeprobe.py`
passes 10/10, and `changeprobe_bytes.py` is kept alongside it as the byte-accounting variant
that can tell "sent nothing" from "could not read it".

Nothing was wrong on the desktop path either, so there is no pre-existing server bug here to
carry forward.

**Closed item 2: coarse-first now asks whether the scene is served, not how serving started.**
`PartGui::coarseTessellationLevel()` tested `FC_BGFX_SERVE_SCENE` directly and so was blind to
`Gui.serveDocument(doc, port)`. It now calls a local `sceneServed()` —
`SceneStreamServer::running()`, *or* the env var on its own, since that variable names a port
the renderer has not necessarily bound yet (it starts the listener at its first publish, and a
document can be loaded, and tessellated, before any frame). The desktop refine arm further
down the file used the same test and moved with it. The per-view
`Render_CoarseTessellation` override now resolves through the active view *or* the serving
source's container, the way the control channel's `view3d` subject already does — headless
serving holds those properties with no view to hang them on.

Measured on `demo-varied` through `serve_then_build.py`, which opens the stream on an empty
hidden document and builds into it — the arrangement where the gate can act at all:

| arm | draws | objects | vertices |
| --- | ----- | ------- | -------- |
| served, gate open | 600 | 200 | **40350** |
| served, `FC_COARSE_TESSELLATION=-1` (what the closed gate did) | 600 | 200 | 638912 |
| built first, then served | 600 | 200 | 638912 |

⚠️ The third row is the part that is **inherent, not fixed**: geometry tessellated before
serving starts cannot be retroactively coarsened, and `fcscenediff` calls that arm *identical*
to the forced-exact one. Build-then-serve is exact either way; only serve-then-build gets the
ladder. `serve_source_varied.py` is the former and `serve_then_build.py` the latter — which
of the two a test uses decides what it can possibly measure.

⚠️ Capture note for both: this source publishes only on document change, so a dump taken a
fixed number of publishes in lands mid-build, at a different place in each arm. `serve_then_
build.py` nudges one object after the build to emit a run of publishes of the *finished*
scene; capture inside that run (`SETTLE=0 DUMP_DELAY=25`). A single-publish arm needs
`DUMP_DELAY=0` or it never dumps at all.

### 4.1 The diff harness

Built before the source it exists to check. Two parts: `fcscenediff`
(`src/Gui/Renderer/tools/scenediff.cpp`, built into the build tree, not installed) and
`dumprun.sh` in `~/works/sw/fcad-probes/`, which produces one dump from one FreeCAD run.

**Compare dumps, not the wire.** A published manifest is content-keyed and delta-encoded per
viewer and carries a session id and a publish version, so two processes legitimately differ
byte for byte. A dump written with no chunk sinks installed is monolithic and
self-contained — the only comparable form of a scene. It used to be reachable only from the
frame loop, so a publish-only renderer could never produce one; it now hangs off the snapshot
instead.

**What is compared, and how.** Not a list of fields: each draw is re-serialized alone through
the writer the format is defined by, and hashed, so a field added later is compared without
anyone remembering to add it. Normalized away first: `cacheId`, `textureId` and `sourceTag`
(process-local by construction), and draw order (draws are grouped on `objectKey` and each
group compared as a multiset). Excluded by design, and reportable with `--camera` /
`--overlays`: the camera, the viewport, `autozoomScale` — which is camera state, being
`getWorldToScreenScale` over the view volume — and the overlays, which are viewer furniture.

⚠️ **Capture both dumps with `FC_BGFX_DUMP_SCENE_SETTLE`** (`dumprun.sh` defaults it to 10
quiet frames). A document does not arrive all at once, and a dump taken a fixed number of
frames in records how far the build had got: three runs of one 40-object script produced
2159580, 2310620 and 2454636 bytes. A bundled dump carries no level information, so
`fcscenediff` cannot warn about it — getting the capture right is the only defence. Two
things the settle gate cannot be: "nothing is dirty" (the per-frame config push leaves
something dirty on nearly every frame) or a large frame count (an idle viewer stops drawing,
so it is never reached). It is a fingerprint of the draws, unchanged across frames. An
animated scene never settles, by construction.

⚠️ **The oracle must itself be serving**, i.e. launched with `FC_BGFX_SERVE_SCENE`, because
how much geometry a publisher publishes is decided by *demand*, and the two configurations
have different demand. Coarse-first is not a serving feature — `PartGui::
coarseTessellationLevel()` enables it for a desktop view too, whenever the render cache is in
renderer mode and the backend answers `drivesMeshLevels()`, which bgfx does. What a serving
process lacks is a local level plan: the planner in `BGFXRendererP::render()` is guarded by
`!FC_BGFX_SERVE_SCENE`, so the rungs it publishes are refined only where a *connected
viewer's* camera asks. With no viewer connected, nothing ever asks and they stay coarse.

Measured on the 40-object scene: a serving process publishes 8310 vertices where a desktop
viewer settles at 25595. Desktop refinement is tolerance-limited — it stops where the camera
cannot resolve the error — so its settled vertex count is a property of the framing, not a
constant: at level 0 it settles at 21043, never returning all the way to 25595.

Coarse-first **does** engage for geometry built while a document loads; an earlier guess here
that the gate could not be open that early was wrong, and testing it is what showed the
difference is demand. ⚠️ The test has to set the **parameter**, not `FC_COARSE_TESSELLATION`:
the environment variable returns before the gate is ever evaluated, so no env-forced run can
say anything about whether the gate opens. Setting the parameter to 0 and loading the
document produced exactly the scene the env-forced level 0 produces (21043 vertices,
structurally identical) — which it can only do by passing the gate. Note also that the
default level 2 is a no-op on this scene, because a rung already finer than a small shape's
exact tessellation coarsens nothing; a discriminator has to use a level that visibly bites.

So the comparison is serving-against-serving, where both sides have the same (absent) demand,
and a desktop dump is not a baseline for anything. Two independent serving processes were
confirmed to publish structurally identical scenes, which is the baseline 2c compares the
source against.

The harness was checked four ways before being trusted: two runs of one script compare
identical; a run with the identity counters deliberately offset (`churn_then_varied.py`)
still compares identical, so the normalization is what is doing the work; one added object is
reported as exactly one object and three draws; and a recolour is reported on the materials
alone, geometry matching to the vertex.

## 5. What this does not do

The source publishes; it does not render. There is deliberately no offscreen-image path here —
`saveRenderDump` on a serving process still needs a real view, and stage 1's `dumpPending`
exception stays as the way to get one. A headless *image* renderer is a different feature with
a different justification, and conflating them is what would drag a GPU back into the process.

Multi-document serving is out of scope: one source, one document, one port. The server is a
singleton (`SceneStreamServer::instance()`) and `beginPublish()` already arbitrates a single
publisher, so a second source would have to be a server change first. That server change is
now designed — see [MultiDocServe.md](./MultiDocServe.md).
