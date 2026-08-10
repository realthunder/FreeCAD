# Retiring the Coin render path

Audit and staged plan for making the bgfx backend the only render path a
user ever sees, while keeping the Coin one alive as a hidden debug
comparison. Measurements taken 2026-08-09 against
`build/conda-debug-occt801`.

## 1. What is actually being retired

"Coin" is four separate things here, and only one of them is on the
table:

| role | who does it | retirable now |
| --- | --- | --- |
| scene graph + traversal + state | Coin | no — it is what *feeds* the backend |
| geometry GL rasterization | `SoFCRenderer` fixed-function pass | **yes — already skipped** |
| picking | `SoRayPickAction` | no — the backend does no picking |
| everything else in the graph (background, overlays, draggers) | Coin GL | partly |

The frame is not "Coin or bgfx". `View3DInventorViewer::renderScene()`
calls `renderer->render()` **first**, then always runs the full Coin
traversal (`inherited::actualRedraw()`). What makes the backend the
renderer is that `SoFCRenderer::render()` returns immediately when
`external->canSkipInternal()` (`renderOk && hasScene && !_deinit`), so
Coin emits no *geometry*. Background, foreground superimposition and the
corner axis cross are suppressed explicitly for backend frames; draggers
and anything the cache did not claim still draw through Coin GL on top.

One thing that makes the coverage question easier than expected: the
render cache captures geometry with an `SoCallbackAction` whose triangle
/ line / point callbacks are registered for **every type derived from
`SoShape`** (`SoType::getAllDerivedFrom`), not from a whitelist. Mesh,
Points and third-party workbench shapes are therefore captured
generically.

## 2. Gating today

Two independent switches, both off by default:

- `View/RenderCache` defaults to **0**; the backend needs **3**
  (`ViewParams::isUsingRenderer()`).
- `View/Render/Type` defaults to **`Default`**, which explicitly selects
  the plain GL pipeline (`setRendererType`).

A render-cache change reaches every viewer including split views
(`View3DSettings` holds the whole viewer list). A *type* change reaches
only `View3DInventor` views — `RenderParams::onRenderParamChanged` uses
`foreach3DViewer`, which does not enumerate `SplitView3DInventor`'s
viewers. Minor, but it means changing only the type leaves split views
on the old backend.

## 3. Audit results

Probes: `~/works/sw/fcad-probes/` (`audit_probe.py`, `audit2_probe.py`,
`stereo_probe.py`, `capture_bg_probe.py`; runner `audit_run.sh`), each
running both legs in one process — backend on (cache 3) and off
(cache 0) — so only the render path differs.

| capability | Coin | bgfx | evidence |
| --- | --- | --- | --- |
| 8 draw styles (As Is … Tessellation) | ok | **ok** | colour-signature match per style, screen grabs |
| picking (`getObjectInfo`) | ok | **ok** | same object/face/point both legs |
| vector export / print | ok | **ok** | 452457 vs 452254 bytes |
| screenshot — `GrabFramebuffer` | ok | **ok** | ink 14400 |
| screenshot — `FramebufferObject` | ok | **ok** (was blank) | ink 0.2544 vs 0.2572, §3.1 |
| screenshot — `CoinOffscreenRenderer` | ok | **ok** (was blank) | identical ink to the FBO path, §3.1 |
| anaglyph stereo | blank | blank | blank on **both** legs — not attributable to the backend |
| missing/broken shader pack | n/a | falls back to Coin | `shaderFailed` → `destroy()` |
| backend init failure | n/a | falls back to Coin | `RendererFactory::create()` returns null |

### 3.1 The one blocking gap: screenshots — closed

With the backend active, `Std_ViewScreenShot` produced a **blank image**
(fixed by "Gui: screenshots render through the backend"). The chain
was:

1. `savePicture()` forces `saveMethod = "FramebufferObject"` whenever
   `selectionRoot->getRenderManager()` is non-null — which is exactly
   `getRenderCache() == 3`, i.e. whenever the backend can be running.
2. That path lands in `renderToFramebuffer()`, which builds its **own**
   `SoBoxSelectionRenderAction` and applies it to the scene graph. The
   backend is never invoked — `_pimpl->renderer` is not referenced
   anywhere in the offscreen or export paths.
3. During that traversal `SoFCRenderer::render()` still sees
   `canSkipInternal() == true` (left true by the last on-screen frame)
   and returns without drawing.

So nobody drew the geometry. `GrabFramebuffer` worked only because it
reads back the on-screen buffer the backend already drew into.

`renderToFramebuffer()` now asks the backend for the frame first, the
way `renderScene()` does, and composites the Coin traversal over it —
background suppressed, foreground and axis cross left to the backend's
overlay feeds, exactly as on screen. Two things separate a capture from
an on-screen frame, and `Render::Renderer::renderOffscreen()` exists for
them:

- **Size.** The backend sizes its view from the host widget every frame;
  while a capture asks for its own size it sizes from that instead, so
  the shot is *rendered* at its resolution rather than a widget-sized
  frame scaled into it. The next on-screen frame sees the mismatch and
  sizes the view back on its own — no reset call to forget.
- **Target.** `BGFXView::blit()` transfers into whatever framebuffer is
  bound, but `QOpenGLWidget::makeCurrent()` — which the frame's context
  dance calls — binds the widget's. The frame now remembers the binding
  it was asked under and restores it before the blit. On screen that is
  the widget's framebuffer either way, which is why on-screen frames are
  pixel-identical across the change (`render-verify` demo-lights, 15/15).

`CoinOffscreenRenderer` needed no decision of its own: `savePicture()`
already redirects everything but `GrabFramebuffer` to the FBO path
whenever the render manager exists, so under render cache 3 that method
never runs — it now measures identically to `FramebufferObject` because
it *is* that path.

The background comes from the same feed the on-screen frame builds, so
all three of `saveImage`'s background choices work: a flat colour, the
viewer's current gradient, and transparency — the last needed the
frame's clear colour to stop forcing alpha opaque, which only a capture
ever reads.

Verified with `~/works/sw/fcad-probes/audit_probe.py`, `audit2_probe.py`
and `capture_bg_probe.py` (runner: `audit_run.sh`): a 400x300 shot of
the two-solid scene gives ink 0.2572 against Coin's 0.2544, with the ink
bounding boxes within three pixels of each other; `FramebufferObject`
and `CoinOffscreenRenderer` both read 5615 where they read 0 before; and
the flat / gradient / transparent / native-size rows match the Coin leg
within measurement. ⚠️ the probe *body* finishes long before the process
does — a stereo run leaves one spinning — so drive them through
`audit_run.sh`, which caps both the wall clock and the log.

### 3.2 What this audit did **not** cover

Stated so the table is not read as more than it is. All of it ran
headless under llvmpipe on one small two-solid scene:

- workbench-specific scene graphs — TechDraw, FEM result meshes, Draft
  working plane, Assembly (Sketcher edit mode: **now covered**, §3.3)
- the shadow light manipulator (the transform dragger: §3.3)
- dimension and annotation text (clipping planes, selection and
  preselection highlight: §3.3)
- large models (the case the backend exists for), and real-GPU
  behaviour — llvmpipe hides device-precision problems both ways
- VR (`View3DInventorRiftViewer`), quad-buffer stereo
- the "Coin still draws it on top" set: whether anything the cache does
  not claim looks right composited over backend output

### 3.3 Stage 1: the edit paths

`edit_audit_probe.py` — selection highlight, preselection highlight,
Sketcher edit mode, the transform dragger, a global clipping plane.

**First, a correction to the method.** There are *three* render paths
here, not two, and §3's table compared the wrong pair:

| leg | setting | who draws the geometry |
| --- | --- | --- |
| `bgfx` | cache 3 + a backend type | the backend |
| `glr` | cache 3 + type `Default` | `SoFCRenderer`, the render cache's own GL renderer |
| `coin` | cache 0 | plain Coin traversal, no render cache at all |

The backend replaces **glr**, not **coin**: they share the render cache
feed and the ViewParams resolved off it. Measuring against cache 0
charges the backend for everything the *cache* does differently.

Method: within each leg, grab the viewport before and after switching
the feature on and report the fraction of pixels that changed. Cross-leg
histograms cannot answer "did the highlight appear" — the paths
antialias differently — but a within-leg delta can.

| case | bgfx | glr | coin | reading |
| --- | --- | --- | --- | --- |
| selection highlight | 0.159 | 0.144 | 0.133 | all three draw it; bgfx and glr images agree to 0.0035 when both legs are in the same state |
| preselection highlight | 0.0055 | 0.0055 | 0.092 | bgfx **identical to glr**. Both outline the face without filling it, which is what `ShowPreSelectedFaceOutline` + `NoPreSelFaceHighlightWithOutline` (both default true) ask for; cache 0 fills it instead. A cache-vs-no-cache difference, not a backend one. |
| Sketcher edit mode | 0.251 | 0.331 | — | both draw the edit-mode scene |
| transform dragger | 0.088 | 0.055 | 0.279 | bgfx and glr agree to 0.0078 in matched state |
| clipping plane | 0.005 | 0.097 | — | **the one difference that indicts the backend** — see below |

**The clipping plane.** glr draws a hatched section cap where the plane
cuts; the backend draws nothing at all. Worth qualifying before it is
called a gap: in this probe *no leg actually clipped the solid* — the
box stayed whole on all three while `hasClippingPlane()` reported true —
so the probe's use of `toggleClippingPlane` is not exercising what a
user's section view does. What is solid is the difference: given the
same scene state, one path drew a cap and the other did not.

**A bug found along the way, and it is not the backend's.** Entering and
leaving Transform edit mode (`Std_TransformManip`) leaves the object
drawn **see-through** on *both* render-cache legs — bgfx 0.117, glr
0.131 change against the frame before, while cache 0 is unaffected at
0.027. `ViewObject.Transparency` reads 0 throughout, and setting it 1
then 0 again does not heal the picture: the document says opaque and the
cache keeps drawing transparent. `dragger_stale_probe.py` reproduces it
with a fresh document per leg.

⚠️ Harness notes, all of which cost a run: cases leak into each other
(Sketcher leaves the camera on the sketch plane, Transform leaves the
object see-through) and legs run in sequence, so a leak crosses legs and
reads as a render-path difference — build a **fresh document per leg**
and close it at the end, or the next leg's MDI view makes the grab pick
a different widget entirely (a 99% image difference). And closing a
document that has been in Sketcher edit mode segfaulted the run once,
inside a `SketcherGui` item delegate under `QStyledItemDelegate::
sizeHint` — on the cache-0 leg, so nothing to do with the render path,
but it is why the coin leg's last two rows are missing above.

### 3.4 The Shadow draw style: what it is actually holding up

Stage 4 below used to read "with shadows now a Shading checkbox,
`Shadow` in the exclusive list is redundant for renderer users". That
premise is wrong, and in a way worth recording: `Render_Shadow` gates
the shadow **map**; the draw style supplies the **light**.

The chain:

- `overrideMode == "Shadow"` dispatches to `Private::activateShadow()`
  (`View3DInventorViewer.cpp:2225`, body at `:2398`).
- `activateShadow()` builds an `SoShadowGroup` holding an
  `SoFCDirectionalLight` or `SoFCSpotLight` — with their draggers —
  reparents `pcViewProviderRoot` underneath it, adds the ground-plane
  group, and selects a *sub* display mode from `Shadow_DisplayMode`
  (Flat Lines / Shaded / As Is / Hidden Line).
- The backend does not read its light from the material feed. It
  resolves it from the traversal state:
  `RendererBridge::translateLightConfig` walks
  `SoLightElement::getLights(state)` and accepts **only**
  `SoShadowDirectionalLight` or `SoSpotLight`
  (`SoFCRendererBridge.cpp:1476`). Its own comment says why — "the
  Shadow draw style's light lives above the render-cache traversal
  root".
- `Render_Shadow` is a per-view bool (`View3DInventorViewer.cpp:4263`)
  that decides only whether the map is drawn.

So with the draw style gone the backend has no scene light at all: the
viewer headlight is a plain `SoDirectionalLight`, which that filter
rejects by type. Everything keyed off the light — shadows, volumetric
shafts, sun disc, ground reflection — has nothing to key off. It is not
a redundant menu entry; it is where the light comes from.

**The persistence surface, for when the removal does happen.** Three
stores, and one hazard that turns out not to bite:

1. `View3DInventor::DrawStyle`, an `App::PropertyEnumeration`
   (`View3DInventor.cpp:101`).
2. The camera blob. `GetCamera` appends `## overrideMode: <mode>` to the
   serialized camera (`:461`); `SetCamera` parses it back and assigns
   `DrawStyle` (`:716`-`723`). Not an independent store on restore — it
   *feeds* the property — but it is what sits inside a saved document's
   camera string, so a migration keyed on the property covers it only if
   it runs after that parse.
3. `App::SavedView` objects. `ViewProviderSavedView` captures and
   re-applies a `DrawStyle` enum alongside the captured `Shadow_*`
   properties; its `finishRestoring()` re-supplies `drawStyleNames()` to
   the enum, which is the tell that the names are not persisted with it.

⚠️ **The enum persists as an index, not a string.**
`PropertyEnumeration::Save` writes `<Integer value="N"/>`
(`App/PropertyStandard.cpp:406`). Dropping a name from
`drawStyleNames()` therefore renumbers every entry after it and would
silently change the draw style of every saved document and every
`SavedView`. What saves us is position, not care: `Shadow` is index 8,
the **last** entry (`ViewParams.cpp:6459`), so removing it shifts
nothing. That is a property of the current list order — so the order
must not be tidied before or during this work, and the migration should
assert the index rather than trust it.

The `Shadow_*` dynamic properties are a fourth thing to place: group
`Shadow`, materialized lazily by `_shadowParam`
(`View3DInventorViewer.cpp:376`), carrying DisplayMode, SpotLight,
ground colour and transparency and the rest. They need mapping onto
`Render_*` equivalents or they are left orphaned on the view.

Two defects found while establishing the above, both recorded in
`docs/HANDOFF_ShadingAndDrawStyle.md` §2 and **both since closed**:

- `Shadow_ShowGround` was never created when a backend is active: the
  call that materializes it sat behind `!renderer &&`, so the short
  circuit skipped it. The global preference still reached the backend
  through the bridge's fallback; only the per-view override was
  unreachable (`5e49f8bdd9`).
- The backend's shadow ground is roughly twice the extent of glr's —
  0.9927 of the frame against 0.4791 on one sphere and one cylinder,
  glr ending at a horizon where the backend reaches every edge. The
  bridge notes it "sizes its ground from the scene bounding box only",
  which is where the difference most likely is. Sizing and placement
  brought to parity in `d4edf90458`; the pixel disagreement that survived
  it turned out to be the quad being *clipped*, not sized wrong, and is
  §1c.

⚠️ Both were invisible for as long as the comparison leg was cache 0,
which has no shadow support at all: its shadow frame measures identical
to its flat one. §3.3's warning about comparing the wrong pair applies
to shadows more sharply than to anything else in this document.

There is already a migration of exactly this shape to copy:
`activateShadow()` reads a legacy `FlatLines` property off the
`App::Document`, converts it into `Shadow_DisplayMode`, and calls
`doc->removeDynamicProperty("Shadow_FlatLines")` (`:2415`-`2420`).

## 4. Plan

Ordered so that nothing user-visible regresses at any step.

**Stage 0 — close the screenshot gap. DONE** (§3.1): the backend draws
the capture, at the capture's own resolution, into the caller's
framebuffer, for a flat, gradient or transparent background alike.
`CoinOffscreenRenderer` turned out to need nothing — it is already
redirected to that path. Left open, and small: the `sample` argument
still builds a multisampled Qt FBO the backend's own resolve then
blits into, which is a pass nobody needs.

**Stage 1 — extend the audit to §3.2. First pass done** (§3.3): Sketcher
edit mode, the draggers, and the selection/preselection highlights all
match the GL renderer. Two things are open out of it — the backend draws
no section cap for a clipping plane, and Transform edit mode leaves both
render-cache paths drawing the object see-through (that one predates the
backend and is the cache's, not the backend's). Still unvisited from
§3.2: TechDraw, FEM, Draft, Assembly, annotation text, large models and
real-GPU behaviour.

**Stage 1a — clear the Coin warnings from the console. DONE.** The
survey judged that none of the three was a correctness bug. **Two of
them were**, and both were being read as cosmetic precisely because the
console was too noisy to look at twice — which is the case for the
stage, not against it. Measured on the real-GPU desktop leg
(`renderer-desktop.sh`, demo-lights, Mesa d3d12 / RTX 3070 Ti): four
`Coin warning` lines a session before, none after.

Each was located by breaking on `SoDebugError::postWarning` under gdb
rather than by matching the message to a plausible call site — worth the
five minutes twice over, since two of the three were not what they
looked like.

- `SbSphere::circumscribe(): The box is empty` —
  `NavigationStyle::findBoundingSphere`, from `setSceneGraph` during
  viewer construction, before any document. **Not cosmetic.**
  `SbSphere`'s default constructor leaves centre and radius
  uninitialized and `circumscribe()` returns without setting them, so
  `NavigationStyle::boundingSphere` stayed uninitialized —
  `reorientCamera` reads it to place an orthographic camera's clip
  planes. A *release* Coin is worse than the debug one that warns: the
  empty-box check is `COIN_DEBUG`-only, so it takes a centre from the
  inverted empty bounds and yields a NaN. Now set to a degenerate sphere
  at the origin (`3ddfb5d3ae`).
- `SoGLLineWidthElement: 2.0 outside [1.0, 1.0]` — from
  `SoFCRendererP::applyMaterial`, i.e. the **glr** path, not the
  backend. ⚠️ **The plan's own fix for this one is void.** It said to
  clamp against `GL_ALIASED_LINE_WIDTH_RANGE` instead; measured through
  gdb in a live context, this driver reports `[1, 1]` for the aliased
  range *and* the smooth one, with `GL_LINE_SMOOTH` disabled. No clamp
  site anywhere can make a wide line draw here. So the warning is
  accurate, unactionable and permanent: Coin now keeps it for a genuine
  overrun and drops it when the implementation offers a single width
  (`coin e7ffbe41c`). The clamp is untouched — wide lines still do not
  draw wide on the fixed-function path, and the backend, which draws
  lines as geometry, remains the only thing that honours the width.
- `SoGLSLShaderParameter: 'baseimage' not found in program` ×4 — **not a
  missing declaration.** The generated blur program declares and uses
  it; dumping the source at the failure point showed every Gaussian tap
  multiplied by `0.000000` or `-0.000000`, so the compiler folded the
  program to a constant and the driver dropped the sampler as inactive.
  `SoShadowGroupP::binomial()` returned `int`. The filter asks for the
  central coefficient of `n = 2*size + 4`, which leaves `int` at size 15
  (C(34,17) = 2333606220) and is 2.4e24 at size 40 — and
  `ShadowSmoothBorder` **defaults to 40**, so the shipped configuration
  sat in the overflowed range and Coin's shadow-map blur output was a
  constant. Returning `double` restores it (`coin 4635a6c61`): tap sums
  go from 0.233 to 1.000 at size 15 and from -0.000 to 1.000 at size 40,
  and nothing below size 15 changes.

  `fcad-probes/shadow_blur_probe.py` (8/8) measures the result on the
  glr leg: against an unfiltered control the penumbra widens from 9.97%
  to 14.72% of the receiver. ⚠️ Do not expect size 40 to look much
  softer than size 14 — binomial weights have width `sqrt(n)/2`, so 40
  taps is 1.6× wider than 14, and the outer taps weigh ~1e-22. ⚠️ The
  probe's first two passes used `saveImage` and read a 0.00 difference
  between *every* smoothing size: the shadow map is an `SoSceneTexture2`
  with its own FBO, and nesting that inside a capture's offscreen target
  flattens the thing under test. `grabFramebuffer`, per §1b.

  The warning fired while the **backend** was drawing, which is the
  standing observation: Coin's shadow machinery is still built and run
  underneath a backend frame, and that is the cost stage 4c removes.

**Stage 1c — auto clipping did not account for what the backend draws
outside the scene graph. DONE.** Found while bringing the shadow ground
to parity, and it was the *only* difference left between the two grounds
once sizing and placement matched. Measured on one 20mm cube, same
camera:

| ground | bgfx near/far | glr near/far | quads agree |
| --- | --- | --- | --- |
| small (scale 0.6) | 240.73 / 277.00 | 238.34 / 279.97 | yes, 0 px |
| auto (scale 2.0) | **240.73 / 277.00** | 205.68 / 312.70 | no, 49 px |
| explicit 60 x 25 | **240.73 / 277.00** | 202.79 / 315.59 | no, 56 px |
| explicit position | 191.26 / 310.38 | 191.26 / 310.38 | yes, 0 px |
| tilted | 206.30 / 317.11 | 206.30 / 317.11 | yes, 0 px |

`240.73 / 277.00` is the cube's own bounds — so in those legs the
backend's ground contributed *nothing* to the fit, and its far and near
corners were clipped away. The Coin quad is a scene-graph node and is
counted; the backend's is drawn outside the graph and reaches the bounds
only through `View3DInventorViewer::onGetBoundingBox`, which
`SoFCUnifiedSelection::getBoundingBox` calls during the
`SoGetBoundingBoxAction` that `SoRenderManagerP::setClippingPlanes`
applies.

**Diagnosed 2026-08-10.** The frame-ordering suspicion was right, and it
is only half of it. `clip_bounds_probe.py` applies the same action to
the same root the render manager holds and prints the box beside the
planes, so the two halves separate: the backend's box is the model
outright, `((-10,-10,0),(10,10,20))`, while glr's is the quad,
`((-40,-40,-1),(40,40,20))`. Running it under a gdb breakpoint on each
link of the chain, with the probe marking its legs on fd 2 so the counts
can be attributed, gives the mechanism:

1. `setClippingPlanes` applies its `SoGetBoundingBoxAction` on **every**
   render — Coin does not gate that.
2. But the traversal is served by an ancestor `SoSeparator`'s bounding
   box cache and never descends, so `SoFCUnifiedSelection::getBoundingBox`
   — the only route to `onGetBoundingBox`, and so the only route by
   which the backend's ground can reach the bounds — is reached just
   **once per leg**. That ancestor is the `SoShadowGroup`
   `activateShadow()` wraps the scene root in: it derives from
   `SoSeparator` and caches like one. Not the superscene above it, which
   Quarter builds with `boundingBoxCaching` **OFF**
   (`Quarter/QuarterWidget.cpp:657`) and whose camera invalidates any
   open cache anyway (`SoCamera::getBoundingBox`). So the draw style that
   has a ground is also the one that puts a caching separator in the way.
3. That one time is the frame straight after `setRendererType`, when the
   renderer has been built but no traversal has run yet, so
   `lightconf` is still default: `valid` false, `ground` false.
   `LightConfig::groundQuad` refuses on the first test and the ground is
   not added. The light config is pushed by the render-cache traversal,
   i.e. *during* the frame — after auto clipping has already asked.
4. Nothing the backend draws can invalidate a Coin cache, so nothing
   ever asks again. Four rounds of moving the camera and 48
   `updateGui()`s later the planes have not moved (240.75 / 277.01) and
   not one further bounding-box traversal has occurred.

So it is not that the auto-sized ground is handled worse than an
explicit one. **The backend's ground never reaches auto clipping at
all.** ⚠️ Which means §3.4's reading of the parity table needs
qualifying: the explicit-position and tilted rows agree with glr *to the
last decimal*, which is not what a second, independent computation of
the same quad looks like — those legs are almost certainly measuring
Coin's own quad still in the graph from the preceding glr shot, not the
backend's contribution. Do not take them as evidence that the path works
in those cases.

**Fixed 2026-08-10**, and *not* the way the diagnosis proposed. That
called for two halves — make the single query see a populated light
config, and invalidate the Coin cache whenever what the backend draws
changes. The second half is unnecessary, and the route to it was a trap
worth recording: the node that would have to be touched is the selection
root, and `SoFCRenderCacheManager::traverse` gates its capture on
`root->getNodeId()`, which `SoNode::notify` bumps for every node the
notification passes through. Touching it to move a clip plane re-captures
the whole scene feed — on the large models the backend exists for, the
expensive traversal, once per bounds change.

Instead the bounds are reported from a place no cache can answer for: an
`SoCallback` added directly under the superscene
(`Private::addRendererBoundsNode`), which is asked on every traversal
because that separator does not cache. It calls the same
`onGetBoundingBox`, so there is still one computation of the bounds and
one place that folds in `LightConfig::groundQuad`; reaching it twice —
once there, once through `SoFCUnifiedSelection` for traversals applied
below — is a union of one box with itself. Nothing has to notice a
change, so nothing feeds a redraw from inside a redraw either.

The ordering half of the diagnosis then takes care of itself: the query
happens every frame, so the frame after the render-cache traversal
pushes the light config already clips to the ground.

`clip_bounds_probe.py` 5/5 — the backend's box goes from the cube's own
`((-10,-10,0),(10,10,20))` to the quad's `((-40,-40,-1),(40,40,20))`, its
planes from 240.73 / 277.00 to glr's 205.68 / 312.70, and the "asked
again later" and "left drawing" checks stay put. `ground_parity_probe.py`
10/10: every row of the table above now clips identically on both paths,
and the auto and explicit-size quads land on the same pixels (0 px, from
49 and 56) — the pixel disagreement *was* the clipping, the far corners
of the quad falling behind the far plane. `renderer_light_probe.py`
10/10.

What this does **not** settle is the doubt raised above about the rows
that already agreed: whether the backend leg's *pixels* are its own quad
or Coin's, left in the graph by the preceding leg, is a question about
what is drawn, and clipping is not what would decide it. What is now
established is on the bounds side — `clip_bounds_probe.py` runs its
backend leg **first**, and that leg's box moved from the cube's to the
quad's across this change, with no glr leg before it to leave anything
behind.

Still open, and the same shape from the other side: an **"invisible"
show-on-top object is clipped by auto clipping** — it does not contribute
to the bounds it is then judged against. Reported 2026-08-10; untouched
by the above, which only moved where the *backend's* bounds are reported.

⚠️ **Not reproduced**, and the negative result is worth as much as the
report until the missing condition is found.
`fcad-probes/ontop_clip_probe.py` builds two 20mm cubes 300mm apart along
the view direction, hides the far one, and reads the clip planes, the
bounding box the render manager is given, and a per-half framebuffer
difference. With the far cube hidden and nothing on top the far plane
sits at 510.9 — the near cube alone — so its absence is measurable. Four
ways of putting it on top all move the plane to 811.8 and draw it, on all
three render paths (30/30):

| on top by | bgfx | glr | coin |
| --- | --- | --- | --- |
| visible, unselected (control) | 811.8 | 811.8 | 811.8 |
| hidden, whole object selected | 811.8 | 811.8 | 811.8 |
| hidden, one face selected | 811.8 | 811.8 | 811.8 |
| hidden, `Std_ToggleShowOnTop`, selection then cleared | 811.8 | 811.8 | 811.8 |

The last row is the one that exercises `checkGroupOnTop`'s `alt` branch —
under a render cache the ordinary selection route returns before it,
so that command is the *only* caller of
`SoFCRenderCacheManager::addSelection` for on-top. Whatever the reported
case is, it is none of these four, and it is not backend-specific.
So the missing condition is in the scene rather than the mechanism: a
hidden **parent** (Link, group, assembly) rather than a hidden leaf, or a
perspective camera, where `setClippingPlanes` clamps the near plane to
`farval / 2^bits` and can cut an on-top object near the eye *after*
counting it — a different defect wearing the same description.

**Stage 1b — the draggers, and Coin reaching into the backend's
framebuffer.** Draggers are the largest thing still drawn by Coin GL on
top of the backend's frame, and the Tessellation defect showed what that
exposes them to: Coin's `HIDDEN_LINE` mode cleared colour and depth
under a frame the backend had already rendered, ignoring the
`clearwindow`/`clearzbuffer` arguments it was passed. That one is fixed
where the mode is chosen, but the general shape — a Coin path that can
stomp a buffer the backend owns — is worth closing rather than meeting
again. Check the shadow light manipulator, the Transform manipulator and
a Sketcher drag, ⚠️ **on screen via `grabFramebuffer`**: `saveImage`
renders offscreen, never composites, and is blind to the entire class.

**Stage 2 — flip the defaults.** `RenderCache` 3 and a real `Type` as
shipped defaults, with a one-time migration for existing user configs.
Keep both parameters working exactly as now.

**Stage 3 — hide the switches.** Remove render cache and renderer type
from the preferences UI; keep the parameters as the debug/A-B route
(`ViewParams`/`RenderParams` are still settable from the console and by
`--user-cfg`, which is what `scripts/render-verify.sh` already uses).
The Coin path stays fully functional and fully tested — it just stops
being something a user can wander into.

**Stage 4 — the scene light, then the draw style** (§3.4). Not a menu
cleanup. `Shadow` is the only thing that puts a light in the graph the
backend will accept, so it cannot be removed until the renderer owns
one. In order:

- **4a — the renderer owns its light. DONE.** `Render::LightConfig` is
  built from `Render_Light*` view properties — direction, colour,
  intensity, spot, position, cone, drop-off — with `RenderParams`
  supplying the global defaults.

  Two decisions worth keeping:

  - **A gate, `Render_Light`, default off.** Handing the backend a light
    unconditionally would relight every scene in every draw style. While
    off, nothing about the frame changes.
  - **The traversal light still wins.** The `Render_*` light is built
    only when `SoLightElement` offers none, so the Shadow draw style
    behaves exactly as before and the two coexist, as this stage
    intended, rather than swapping over in one commit.

  What this unblocks is the point: in the **Shaded** style, with no
  Shadow style anywhere, the scene is lit, grounded and casting a shadow
  map from a light nothing in the graph provides.
  `fcad-probes/renderer_light_probe.py`, 10/10 — light-with-no-draw-style
  (mean pixel diff 111), direction (26), spot vs directional (40), cone
  angle (63), `Render_Shadow` dropping the map (111), and the Shadow
  style still winning (0.00).

  ⚠️ `Render_Shadow` defaults **on**, so a test that switches it on
  measures nothing. Switch it off to see the map.
- **4b — the ground moves to the backend.** Partly there already
  (`Render::LightConfig::ground`); the rest is the Coin geometry in
  `pcShadowGroundGroup`, which today also has to carry its own
  `SoPolygonOffset` to match the one every Part shape has.
- **4c — decide the plain-Coin path.** `SoShadowGroup` is Coin's only
  shadow implementation, so a cache-0 user loses shadows outright. Under
  this document's premise that is acceptable — but it is a decision to
  take deliberately, not a refactor to fall into.
- **4d — map `Shadow_*` onto `Render_*` and migrate.** All three stores
  in §3.4: the property, the camera blob, and `SavedView`. Follow the
  `Shadow_FlatLines` precedent already in `activateShadow()`.
- **4e — drop `Shadow` from `drawStyleNames()`.** Safe only because it
  is the last index; assert that rather than assume it.

Not in scope, and not close: Coin as scene graph, traversal and picking.
Replacing that is a different project — the backend has no picking at all
and the whole feed is built on Coin traversal.
