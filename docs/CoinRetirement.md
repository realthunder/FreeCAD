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
`stereo_probe.py`), each running both legs in one process — backend on
(cache 3) and off (cache 0) — so only the render path differs.

| capability | Coin | bgfx | evidence |
| --- | --- | --- | --- |
| 8 draw styles (As Is … Tessellation) | ok | **ok** | colour-signature match per style, screen grabs |
| picking (`getObjectInfo`) | ok | **ok** | same object/face/point both legs |
| vector export / print | ok | **ok** | 452457 vs 452254 bytes |
| screenshot — `GrabFramebuffer` | ok | **ok** | ink 14400 |
| screenshot — `FramebufferObject` | ok | **BLANK** | ink 0.2519 → **0.0000** |
| screenshot — `CoinOffscreenRenderer` | ok | **BLANK** | ink 0 |
| anaglyph stereo | blank | blank | blank on **both** legs — not attributable to the backend |
| missing/broken shader pack | n/a | falls back to Coin | `shaderFailed` → `destroy()` |
| backend init failure | n/a | falls back to Coin | `RendererFactory::create()` returns null |

### 3.1 The one blocking gap: screenshots

With the backend active, `Std_ViewScreenShot` produces a **blank image**.
The chain:

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

So nobody draws the geometry. `GrabFramebuffer` works only because it
reads back the on-screen buffer the backend already drew into.

The fix is small and the mechanism is already there: `BGFXView::blit()`
reads the current `GL_FRAMEBUFFER_BINDING` and blits into it, so
`renderToFramebuffer()` can invoke the backend after binding the FBO, the
way `renderScene()` does. Rendering at a size other than the widget's
needs the backend targets resized for the shot, otherwise the blit scales.

### 3.2 What this audit did **not** cover

Stated so the table is not read as more than it is. All of it ran
headless under llvmpipe on one small two-solid scene:

- workbench-specific scene graphs — Sketcher edit mode, TechDraw, FEM
  result meshes, Draft working plane, Assembly
- draggers / manipulators, the shadow light manipulator
- clipping planes and section views, selection and preselection
  highlight, dimension and annotation text
- large models (the case the backend exists for), and real-GPU
  behaviour — llvmpipe hides device-precision problems both ways
- VR (`View3DInventorRiftViewer`), quad-buffer stereo
- the "Coin still draws it on top" set: whether anything the cache does
  not claim looks right composited over backend output

## 4. Plan

Ordered so that nothing user-visible regresses at any step.

**Stage 0 — close the screenshot gap.** Invoke the backend from
`renderToFramebuffer()`; resize its targets for off-size captures. Also
decide what `CoinOffscreenRenderer` should do — probably refuse and fall
back rather than silently return blank. Blocking: cannot default the
backend on while screenshots are blank.

**Stage 1 — extend the audit to §3.2.** Especially Sketcher edit mode
and draggers, which are the paths most likely to be drawing through Coin
GL on top and to look wrong when they do.

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
