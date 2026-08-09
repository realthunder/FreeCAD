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

**Stage 2 — flip the defaults.** `RenderCache` 3 and a real `Type` as
shipped defaults, with a one-time migration for existing user configs.
Keep both parameters working exactly as now.

**Stage 3 — hide the switches.** Remove render cache and renderer type
from the preferences UI; keep the parameters as the debug/A-B route
(`ViewParams`/`RenderParams` are still settable from the console and by
`--user-cfg`, which is what `scripts/render-verify.sh` already uses).
The Coin path stays fully functional and fully tested — it just stops
being something a user can wander into.

**Stage 4 — the draw style list.** With shadows now a Shading checkbox,
`Shadow` in the exclusive list is redundant for renderer users; retiring
it needs a document-compatibility story (`DrawStyle` is persisted as a
string).

Not in scope, and not close: Coin as scene graph, traversal and picking.
Replacing that is a different project — the backend has no picking at all
and the whole feed is built on Coin traversal.
