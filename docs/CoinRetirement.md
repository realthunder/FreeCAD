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
  working plane, Assembly (Sketcher edit mode: **now covered**, §3.3;
  Draft, annotation text, Mesh, Points, Assembly **and FEM**: **now
  covered**, §3.6 -- this bullet is closed; the defect it found was
  rotated labels on non-scene-oriented overlays, since fixed)
- the shadow light manipulator (the transform dragger: §3.3)
- dimension and annotation text (clipping planes, selection and
  preselection highlight: §3.3)
- large models (the case the backend exists for), and real-GPU
  behaviour — llvmpipe hides device-precision problems both ways
  (real GPU: **now covered for §3.6's cases**, Mesa d3d12 on an RTX
  3070 Ti, same readings as llvmpipe)
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

**The clipping plane — RESOLVED, and the audit row above is wrong.** The
qualification it carried was the important part: in that probe *no leg
actually clipped the solid* — the box stayed whole on all three while
`hasClippingPlane()` reported true — so it compared two unclipped frames
and "the backend draws no cap" was an artifact of that. With a plane
that really cuts (`section_cap_probe.py`, which asserts the cut before
comparing), the backend draws the cap, hatch and all.

What was real: the backend also drew the hidden geometry's inside
corners back over its own cap — a Y across the cut face, 283 px where
glr is flat. The cap was innocent. **Cavity shading is a fullscreen
multiply over the depth+normal prepass target** (`aoNormalZ`), which
the cap never entered, so it darkened creases belonging to the geometry
the cap hides. Fixed by rendering the cap into the prepass too, in its
own Sequential view: `ViewAOPrepassCap`. GTAO reads that target as well
and gains the same correction. The cut face now carries a ridge along
its rim, which is cavity seeing a real surface rather than leaking
through one.

⚠️ The diagnosis cost three wrong turns worth recording, all of the same
shape — *testing one gate at a time on the draw that was not at fault*:
forcing the cap's depth test to ALWAYS, then its stencil test, then both,
each changed nothing or almost nothing. What localized it was moving the
cap draw to a late view, which fixed 227 of 283 pixels: an ordering
symptom points at a **later pass reading a stale buffer**, not at the
draw's own state.

**A bug found along the way, and it is not the backend's.** Entering and
leaving Transform edit mode (`Std_TransformManip`) leaves the object
drawn **see-through** on *both* render-cache legs — bgfx 0.117, glr
0.131 change against the frame before, while cache 0 is unaffected at
0.027. `ViewObject.Transparency` reads 0 throughout, and setting it 1
then 0 again does not heal the picture: the document says opaque and the
cache keeps drawing transparent. `dragger_stale_probe.py` reproduces it
with a fresh document per leg.

Stage 1b measured it again on the real GPU and it is worse there than
these llvmpipe numbers suggest, in a way that also separates the paths:
glr loses the faces **outright** (0.904 of the scene reads as background
after leaving edit mode) while the backend keeps them at 0.758. Leaving
edit mode heals neither.

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

⚠️ **The backend had it backwards: it gated the light on the map**
(found by the Coin-to-bgfx lighting audit, fixed 2026-08-14). The
sentence this section opens with is the design, and the bridge
implements it -- `LightConfig::shadow` is a field beside `valid`, and
`translateLightConfig`'s own comment calls `Render_Shadow` "a
convenience toggle to drop the shadow map while keeping the scene lit".
The bgfx backend did the opposite. Every light uniform (direction,
colour, intensity, spot position and cone) was computed *inside* the
shadow-camera block in `BGFXFrame.cpp`, three levels deep in a gate on
`m_shadow && bboxValid && lightconf.valid && lightconf.shadow`, and the
shaders read one `shadowFrame` flag for both "a map exists" and "a
scene light is fed". Three ways to lose the light, none of them about
lighting:

- `Render_Shadow` off -- the documented case, which did the one thing
  its documentation promises it will not do.
- A GPU whose caps carry no filterable shadow format: `m_shadow` is
  only "shadow resources exist", so the scene silently lit by headlight
  alone on the devices least able to afford being wrong about it.
- A scene with no extent to fit a light camera to (`bboxValid`, or a
  zero diagonal).

In all three the frame fell back to the fixed white view-axis
headlight. Split into `lightFrame` (a scene light is fed, from
`LightConfig::valid` alone) and `shadowFrame` (a map was rendered),
with the light resolved before and independently of the shadow camera.
The sun disc moved to `lightFrame` too -- the sun belongs where the
light is, with or without a map.

Verified by staging a pure RED light through the `Render_Light`
parameter path with `Render_Shadow` toggled and nothing else moving:
before, the shadow-off leg had **zero** red pixels (all-grey headlight);
after, the geometry is red-lit and only the shadow and its ground quad
go away. A coloured light is what makes the two outcomes impossible to
confuse -- the headlight is white, so "lit" and "lit by the scene
light" separate on hue rather than on brightness.

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

### 3.5 The display style menu: what a row is allowed to do

The draw style list and the shading switches are the user's whole
interface to everything above, and this stage rebuilt them (checkboxes
on `QWidgetAction` rows, then radio buttons, plus the cavity radius
slider). The rule that came out of it, from the user, is worth stating
before anyone "fixes" it again:

> A row built as a `QWidgetAction` (the display style radios, the
> shading checkboxes) does **not** dismiss the menu. Trying Shaded
> against Flat Lines is what the list is for. A normal menu item keeps
> Qt's own behaviour and dismisses.

So the menu's staying up under a widget row is the feature, not a
regression in Qt's handling of embedded widgets. A first report was
read here as the opposite ("rows should dismiss like ordinary items"),
built over three commits and taken back out again (`c6f73aef90`); the
net change to the menu is none.

WARNING: **never hide a popup by hand to dismiss it.** `QMenu::hide()`,
or hiding the chain of parent popups, is only half of
`QMenuPrivate::hideUpToMenuBar`, which also clears the menu bar's
current action and leaves keyboard mode. Without that half the menu bar
goes on believing its popup is up, so it **swallows the next click on
it** and opens only on the one after. If a row ever must dismiss
itself, hand the popup a synthetic mouse press at a point *outside its
own rect*: QMenu answers that with `hideUpToMenuBar` itself, all of it.

The menu machinery around these rows produced five separate defects,
all found by pulling on "why is there no tooltip on the cavity row" and
all fixed: tooltips set on a `QWidgetAction` never render (they belong
on the row widget, `62d8eb5566`); a button's `toggled` wired to both
`setChecked` and `toggled` ran every command **twice**, which is why
entering Shadow always raised the light manipulator (`d6032d90cc`);
`V,1` to `V,9` were dead because `fillGroupMenu` adds an action to the
menu only when it is *not* checkable, so the actions belonged to no
widget and Qt delivers `QEvent::Shortcut` only to associated actions
(`a104c949f3`); `Action::setChecked` emitted `actionChecked` only on a
change while an exclusive `QActionGroup` unchecks its siblings itself,
leaving a stale tick that made that entry unclickable, a ticked radio
emitting nothing (`3371b53ab0`); and the repeat shortcut that toggles
the manipulator needs `QActionGroup::ExclusionPolicy::ExclusiveOptional`,
because `QAction::activate` silently drops a trigger on the checked
member of a strictly exclusive group (`09fc56d3be`).

WARNING: **no harness here can see a menu grab bug.** Three were
measured against a build the user could see was broken, and all three
passed; `docs/RenderDebug.md` section 5.1 records what they were. For
menu behaviour the user is the only oracle; say that instead of
reporting a pass.

### 3.6 Stage 1c: the workbench scene graphs

`wb_audit_probe.py` -- the rest of the first bullet of 3.2. Eleven cases,
the same three legs in one process, the same within-leg method: grab,
add the feature, grab, report the fraction of pixels that changed.

| case | what it exercises | bgfx | glr | coin |
| --- | --- | --- | --- | --- |
| Draft Wire | line geometry through a Python view provider | 0.0030 | 0.0029 | 0.0024 |
| Draft Text | screen-space text | 0.0007 | 0.0005 | 0.0025 |
| Draft Dimension | leader lines plus annotation text | 0.1875 | 0.1873 | 0.1869 |
| Draft grid | the working-plane tracker | 0.1455 | 0.1456 | 0.1456 |
| App::AnnotationLabel | `SoFrameLabel`, a Qt-drawn `SoImage` | 0.0029 | **0.1760** | 0.0028 |
| Mesh::Feature | `SoFCMeshObjectShape`, a shape that draws itself with raw GL | 0.0198 | 0.0192 | 0.0192 |
| Points::Feature | point sets | 0.0080 | 0.0084 | 0.0084 |
| Assembly + grounded joint | Assembly's own `SoSwitchMarker` | 0.0253 | 0.0202 | 0.0202 |
| `Fem::FemMeshObject` | FEM's mesh view provider: faces, edges, node markers | 0.0020 | 0.0022 | 0.0022 |
| `Fem::FemPostPipeline` | a VTK unstructured grid turned into Coin geometry, plus FEM's colour bar | 0.0010 | 0.0264 | 0.0264 |

**The backend draws all of them**, and cross-leg it agrees with glr
everywhere but one row: bgfx against glr sits at its 0.066 floor (what
the two paths' shading differs by on the bare box) for every case,
0.0778 for the mesh and 0.0839 for the assembly, and **0.2423 for the
annotation label**.

**The second disagreement is FEM's colour bar, and most of it is the
audit measuring itself.** The two FEM rows had to run against
`build/fem-eval`, since the primary tree is configured `BUILD_FEM=OFF`
(`FC_BIN` points `audit_run.sh` at another tree).

`Fem::FemMeshObject` passes: the same footprint as cache 0 (x 443-580,
y 149-266) and 1220 changed pixels against its 1446, which is shading.
It is an `SoIndexedFaceSet` like any other and it reaches the backend
like any other.

`Fem::FemPostPipeline` reads 0.0010 on the backend against 0.0264 on
both Coin legs. Banding the changed pixels by column:

| band | cache 0 | bgfx |
| --- | --- | --- |
| the geometry, x 400-600 | 725 px | 629 px |
| the colour bar, x 880-1024 | 16348 px | 0 px |

The geometry is drawn (629 against 725 is the usual shading
difference). The colour bar reads zero -- but **that is the capture
path, not the renderer**. `grab()` is `saveImage`, i.e. the image-export
path, and an export on the backend deliberately drops the viewport
chrome: `BGFXFrame.cpp` skips every overlay feed whose anchor is not
`sceneCamera`, and the foreground feed that carries the colour bar is
one of those. The Coin legs have no such rule -- their offscreen render
just traverses the whole foreground root -- so the same call keeps the
bar there. The 16348-against-0 is that asymmetry, not missing geometry.

Measured through `saveRenderDump`, which keeps the frame as staged, the
backend draws the bar: gradient quad and value labels both
(`colorbar_probe.py`).

**The real defect the bar exposed was its labels**, and it was in shared
billboard code rather than in anything FEM. Screen-aligning a billboard
substitutes the scene camera's basis for the model matrix's upper 3x3.
That is only a screen-alignment when the view the draw is submitted into
carries the scene rotation too, so the two cancel: the main scene, a
`sceneCamera` overlay, and an `orientFromScene` overlay (the axis cross,
the NaviCube labels -- which is why those looked right and nothing else
had caught this). The foreground feed is a fixed orthographic camera
with no scene rotation to cancel, so the substitution *tilted* the
labels by the camera instead: under the isometric audit camera the
values were rotated about 60 degrees. The basis is now the identity for
an overlay that is not scene-oriented, and the labels are horizontal.

**The export now keeps the bar.** It did not at first: the chrome rule
skipped every feed that was not `sceneCamera`, which swept in the
foreground feed along with the navigation cube. That was broader than
the rule the chrome-skip describes itself by -- "the corner-anchored and
pixel-space feeds are the navigation cube, the corner axis cross and
on-screen text" -- so the test is now that description rather than an
approximation of it. A feed is chrome when it is corner-anchored or
pixel-space; a full-viewport, non-pixel-space feed is the foreground
root, i.e. the front-root graphs view providers publish, and that is
where every scalar legend lives (FEM's, Mesh curvature's,
Inspection's). A legend is what makes the exported colours mean
anything, and the Coin path this stands in for always kept it.

Both halves are measured. `colorbar_probe.py`: the exported colour-bar
band goes from 0 to 1086 changed pixels, against 1124 on screen.
`view_lifecycle_probe.py`: the export still drops the navigation cube,
6700 pixels of it, while touching 0 pixels of the model.

TechDraw is not in the table because it is not a 3D path at all: a page
is its own `QGraphicsView` over `QGI*` items painted by QPainter, so
there is no before/after 3D frame to take. The case that remains is a
liveness check, and it passes identically on all three legs: an A4 page
with its title block and the box view comes up and reads ink 0.2006 on
each. Auditing the page renderer, and judging whether it could be drawn
by the backend, is separate work.

**The one disagreement, and the backend is not the one that is wrong.**
On the annotation label bgfx reads 0.0029 and cache 0 reads 0.0028 --
the same small screen-space label in the same place -- while glr reads
0.1760 and differs from cache 0 by 0.1761. glr draws the label as a
world-space billboard several times the size of the 20 mm box.

The mechanism is in the capture companion. `SoImage` draws in screen
space, but its `generatePrimitives()` emits a quad sized in model units
for the capture-time view, so `SoFCRenderCacheManager::preImage()`
substitutes a companion instead (`SoFCRenderCacheManager.cpp:2229`): the
image as a REPLACE texture on a quad **in native pixel units**, under an
`SoAutoZoomTranslation` with `billboard TRUE` and `pixelScale 1`. Those
two fields are read in exactly one place, `SoFCRendererBridge.cpp:870`,
which feeds the **backend** -- `BGFXRendererP.h:1783` turns `pixelscale`
into on-screen pixels per emitted unit every frame. glr instead replays
the node itself (`SoFCRenderer.cpp:1555`, `info.node->GLRender(action)`),
and `SoAutoZoomTranslation::doAction` honours only `scaleFactor`, whose
default 0 short-circuits `getScaleFactor()` to 1.0. So the quad's pixel
coordinates are used as world coordinates and the model rotation is
kept: a roughly 400-unit billboard lying in the model plane.

This is not one label. It is every `SoImage` on the glr path, Sketcher's
constraint icons included -- which is worth re-reading 3.3's Sketcher
row against (bgfx 0.251, glr 0.331; the larger number is glr's, and
oversized icons would produce exactly that).

Two places could fix it, and the obvious one is the trap:

- `SoAutoZoomTranslation::doAction` could honour `billboard` and
  `pixelScale`, which is where the field's documented contract lives.
  But `doAction` is also what the **capture** traversal runs
  (`callback()`), so a camera-dependent rotation and scale would be
  baked into the static vertex cache -- the precise failure the
  companion exists to avoid, and the reason the comment above
  `SoFCImageQuad` was written.
- `SoFCRendererP`'s autozoom replay loop could carry the backend's
  per-frame math instead, overwriting the accumulated matrix's 3x3 with
  the camera basis scaled by `pixelScale` world-units-per-pixel. That is
  the symmetric place: the bridge does it for the backend, this does it
  for glr, and nothing capture-side moves.

**Done 2026-08-16, the second way** (`SoFCRendererP::applyBillboard`).
The replay loop asks each autozoom entry whether it is a billboard, and
substitutes the camera basis scaled to world units per screen pixel,
instead of calling the node's `GLRender`. `pixelScale` when set, the
backend's glyph factor when not (`SoAutoZoomTranslation::DefaultPixelScale`,
so the two paths read one number). Measured on the label of the table
above: the delta a label adds to its frame went from **18042 pixels to
459**, which is what the backend and cache 0 both read, to the pixel.

The scale itself took two tries, and the first one is the trap.
`SbViewVolume::getWorldToScreenScale` answers a *nearby* question -- the
world radius of a SPHERE covering a given screen radius -- and its
perspective form is a tangent construction, linear only in the small and
drifting off-axis. Asked for the viewport width it made a corner label
half again too large under a perspective camera; asked for one pixel it
was still 8% small. An orthographic camera showed neither, which is why
the first version passed. It now evaluates the backend's own expression
element for element (`2d/(P[1][1]*H)`, the depth term dropped for an
orthographic projection), so the two paths size a billboard by one piece
of arithmetic rather than by two that agree in the middle of the view.

### The backend's billboard is not oversized -- what that measurement was

The same run read the backend's label at 1572 blue pixels against cache
0's 1494 and glr's 1519, and that was carried forward as "the backend's
billboard is ~5% large in area under a perspective camera". It is not.
Measured again as an EXTENT rather than a count, with one camera pinned
across all three legs:

| | cache 0 | backend |
|---|---|---|
| font 12 | 98x29 | 99x30 |
| font 48 | 361x84 | 362x85 |

The box stays **one pixel** larger in each dimension while the label
grows 3.7x, and it reads the same orthographic and perspective, centre
of the view and corner. A 5% scale error would be 18 pixels at font 48.
The blue-pixel excess meanwhile *falls* with size, 5.2% to 2.9% -- which
is how a constant-width antialiased edge behaves (it scales with the
perimeter) and the opposite of how a scale error behaves. So the
backend's quad is the same size as Coin's blit and about half a pixel
softer at its border, and the count that said otherwise was measuring
the border.

⚠️ Two harness lessons, both of which produced confident wrong numbers
here. **A thresholded pixel count is not a size** -- it moves with edge
filtering as readily as with scale, and squaring it into an "area"
turns a one-pixel border into 5%. **Amplify before believing a few
percent**: the same label at 4x the font separates a scale error, which
grows with it, from a quantisation, which does not. And `fitAll` inside
a leg frames the legs differently -- 40 pixels apart here -- which under
a perspective camera changes the label's depth and therefore its size,
so the camera must be captured once and re-pinned before every grab.

**Two numbers per case, because the capture is not the frame.** Every
row above is measured twice, `saveImage` and a screen grab, and one case
disagrees: under cache 0 the Draft grid is absent from `saveImage`
(0.0000) while the screen shows it (0.1456). The grid is a viewer-level
tracker rather than document geometry; under cache 3 both legs capture
it. So 3.1's finding -- that `saveImage` and the on-screen frame agree
-- holds for what the render cache feeds and not for everything the
viewer draws. The Dimension row inherits the same effect, because
creating a Draft dimension switches the grid on behind it.

**A bug found on the way, and it is not the renderer's.** The Draft
cases could not run at all: creating any Draft object in the GUI asks
for the SVG hatch patterns, and `importSVG.getContents` raised
`NameError: name 'pythonopen' is not defined`. All five Draft importers
saved the builtin `open` behind a guard that names the Python 2 spelling
of `open.__module__`; on Python 3 it reads `_io`, so the guard has been
false for the whole Python 3 era and `pythonopen` was never assigned.
Fixed in `8fcb8cd196` (24 patterns load where the call raised); the same
dead guard sits at 38 more sites across Arch, Idf, Spreadsheet,
Material, OpenSCAD and the Path post-processors, recorded separately.

Harness notes, each of which cost a run:

- **The 3D grabs are `saveImage`, not screen grabs.** These cases
  provoke Coin errors, the Report view raises itself over the viewport,
  and a screen grab cropped to the viewport rect then contains a text
  panel -- which put the same phantom drift on five consecutive cases.
  Clearing the five `checkShowReportViewOn*` parameters did **not** stop
  it. `grabmethod_probe.py` measures `saveImage` against the on-screen
  frame as identical (0.0000 on both cache-3 legs), so the audit uses
  it, and every line carries a `drift` column -- the difference between
  the case's own base grab and the leg's first frame -- so a leak shows
  up as a number instead of poisoning the case silently.
- **The Draft grid is stateful and `Draft_ToggleGrid` is blind.**
  Creating any Draft object turns the grid on, so by the grid case it
  was already lit: the toggle switched it off, and the tidy-up switched
  it back on for every case after. Read the state from
  `Gui.Snapper.grid.Visible` and drive it, do not toggle.
- **`metaObject()->className()` carries the C++ namespace.** The
  TechDraw page answers to `TechDrawGui::QGVPage`; matching `QGVPage`
  reported "no page window found" while the window was plainly up.
- The first frame after a leg switch is not the leg's frame. The bgfx
  leg's first grab was an empty gradient; grab twice and keep the
  second.

**Run on the real GPU as well**, which is another 3.2 bullet: the same
probe through `scripts/renderer-desktop.sh` on WSLg Wayland with Mesa
d3d12, `BGFX Renderer: D3D12 (NVIDIA GeForce RTX 3070 Ti Laptop GPU)`.
It says the same thing. The deltas are all smaller because the desktop
viewport is larger, so the same feature covers a smaller fraction of it,
but the shape of the table is unchanged and llvmpipe hid nothing:

| case | bgfx | glr | coin |
| --- | --- | --- | --- |
| Draft Wire | 0.0021 | 0.0009 | 0.0008 |
| Draft Text | 0.0007 | 0.0003 | 0.0017 |
| Draft Dimension | 0.0560 | 0.0552 | 0.0038 |
| Draft grid | 0.0528 | 0.0523 | 0.0000 |
| App::AnnotationLabel | 0.0017 | **0.1683** | 0.0011 |
| Mesh::Feature | 0.0146 | 0.0140 | 0.0140 |
| Points::Feature | 0.0035 | 0.0033 | 0.0033 |
| Assembly + grounded joint | 0.0186 | 0.0143 | 0.0143 |

bgfx against glr floors at 0.0496 and stays there for every case except
the annotation label at 0.2179; glr against cache 0 is 0.0000 or near it
everywhere except that label (0.1685) and the two grid-carrying rows.
The label picture at desktop resolution is the same billboard, only
sharper.

Two things the Wayland leg cannot do, neither of which costs a result:
screen grabs come back invalid (`scr=-1.0000`) because a Wayland client
cannot grab the screen, and the TechDraw page therefore measures 0x0.
The `saveImage` numbers -- the ones the table is built from -- are
unaffected, and the headless leg already answered TechDraw.

Still unvisited from 3.2 after this: **FEM** (`BUILD_FEM=OFF` in this
tree, so it was not measurable here), large models, and VR /
quad-buffer stereo.

### 3.7 Can this default fail on a machine that only cache 0 would suit?

Asked before making the path a default rather than a choice
(2026-08-16). Two separate questions, because the default has two
layers: the backend, and the render cache under it.

**The backend layer is supposed to degrade to the cache's own GL
renderer, and by construction it does:** `renderOk` is false at every
frame entry and set true only by a frame that finished, so
`canSkipInternal()` is false, `SoFCRenderer::render()` does not return
early, and `renderScene()` clears and un-suppresses Coin's background
whenever `render()` returned false. Nothing skips Coin on a claim the
backend has not made good on.

**Two failures were injected to check that, rather than read**, and
between them they found one real defect and one that is still open:

| what fails | how it was tested | what the user gets |
| --- | --- | --- |
| the shader pack cannot be loaded | `FC_BGFX_SHADER_DIR` at an empty directory | the screen is **correct** -- the cache's GL renderer draws it, gradient and axis cross included. Every **screenshot** came back black; fixed 2026-08-16, below |
| the driver reports OpenGL 2.1 | `MESA_GL_VERSION_OVERRIDE=2.1` | was a **SIGSEGV** inside bgfx program creation (`Program create: GL23: GL21, GL22`); the device is refused up front now, and the cache's GL renderer draws |
| no GL context / no native handle | code read only (`prepare()` returns false, the view is never created) | the cache's GL renderer draws |
| `bgfx::init` refuses the device | code read only | the cache's GL renderer draws |
| the build has no backend at all | `selectRenderPath()` resolves `Type` to `Default` | plain GL, no warnings |

**The screenshot defect, and why it looked like a black window.**
`QOpenGLWidget::makeCurrent()` binds the *widget's* framebuffer. The
frame's success path knows this and restores the caller's binding before
the blit; its bail paths called `makeCurrent()` and did not. So a frame
that failed handed Coin the widget's framebuffer no matter what the
caller had bound: on screen that is the right one, which is why the
window looked perfect, and a screenshot's capture target was left empty
-- black, through all three of `FramebufferObject`, `GrabFramebuffer`
and `CoinOffscreenRenderer`. The binding is now read once at the top of
the frame and restored by every bail as well as by the blit.

⚠️ **Read the measurement, not the window.** This was first written up
here as "a black viewport" because `saveImage` came back black and a
screenshot is how these probes see. `QWidget::grab()` of the 3D view --
which composites what is actually on screen -- showed the model the
whole time. A capture path is not a witness to the screen when the
capture path is the thing that is broken.

**The OpenGL floor is 3.1, and it is checked now.** The desktop shader
pack is compiled at GLSL 1.40 (`-p 140` in `BGFXShaders.cmake`), so a GL
2.1 class driver -- pre-Sandy-Bridge Intel, indirect GLX over `ssh -X`,
some RDP and VM stacks, ancient Mesa -- reaches bgfx's GL21 path with
shaders it cannot compile. bgfx does not refuse such a device; it
crashed building the first program, taking the session with it, and none
of the fallbacks above were reached because nothing had failed *yet*.
`prepare()` now compares the created context's version against 3.1 and
refuses below it, once (a device does not grow a version, and every
frame asks again), which lands on the same fallback as every other
failure here.

The leg that found it is an artificial cap -- the driver keeps every
entry point and only reports an older version -- so it is not proof of
what a genuine old GPU does. It is a fair proxy for the mechanism, and
the guard is written against the reported version, which is what such a
machine reports too.

**The cache layer is the real exposure**, because it now applies to
everyone including those machines, and it is not driver-dependent but
behavioural. What is known to differ from cache 0:

- ~~**`SoCube` with `SoDrawStyle::LINES` is not drawn** by the render
  cache~~ -- **not true any more, measured 2026-08-16.** The cache's GL
  renderer draws it; what it dropped was the **line pattern**, because
  the width and stipple were applied only to draws typed
  `Material::Line`, and a cube in `SoDrawStyle::LINES` is a *triangle*
  draw handed to `glPolygonMode`. So the geometry check's dashed box
  came out solid, not absent. Fixed, and the 2022 workaround in
  `TaskCheckGeometry.cpp` (which omitted the box under render cache 3)
  is gone with it -- the box is back on every path.
  The **backend** was the one drawing it wrong, and worse: it took
  every `SoDrawStyle::LINES` for the **Tessellation display mode** and
  filled the faces in the background colour, so the box hid the shape it
  was drawn around. The two are now told apart by whether the style
  arrived as a scene-wide *override*, which is what the display mode is
  and a lone `SoDrawStyle` node is not (`Material::drawstyleoverride`,
  scene dump v57). Measured: the delta the box adds went 39437 -> 1566
  pixels on the backend, against 463 on both other paths.
  That last gap was the backend drawing the box's **triangulation**:
  `submitTessellation` had one segment per triangle index position, so
  every face carried a diagonal, and the pattern never reached it
  because the bridge only put `linepattern` on a `Material::Line`. Both
  fixed 2026-08-16. GL hands `LINES` to `glPolygonMode` and the shapes
  that arrive this way are `SoFCVertexCache`'s **glrender** shapes
  (`SoCube`, `SoText2`, `SoFCBoundingBox`) -- ones Coin replays as its
  own polygons -- so a diagonal is an artefact of triangulating them and
  GL never draws one. `GpuGeometry::ensureCreaseEdges` drops the edges
  shared by two coplanar triangles and keeps everything else, so a
  tessellated curve is unchanged, and its per-triangle offsets keep a
  partial index range mapping onto an instance range. The display MODE
  keeps every triangle edge: there the tessellation is the thing being
  shown. Delta 1954 -> 859 against cache 0's 498; the rest is dash phase
  and the quad line's antialiasing, and at a threshold that admits an
  antialiased dash the two cover 999 and 898 pixels of the same lines.
- ~~**`SoImage` capture companions** are drawn by the cache's GL
  renderer at pixel coordinates read as world units (3.6)~~ -- **fixed
  2026-08-16**, see the end of 3.6. The GL pass carries the backend's
  per-frame billboard math now, and reads what cache 0 reads.
- **Preselection highlight** outlines the face where cache 0 fills it
  (3.3). By design, and configurable, but it is a visible difference.
- **Anaglyph stereo** is blank -- on both legs, so not attributable to
  this default, but it is not a reason to think cache 0 is safer.
- **Not measured anywhere:** VR (`View3DInventorRiftViewer`),
  quad-buffer stereo, FEM result meshes, large models on GPUs other
  than the two tested.

So the honest answer is that no machine is known where cache 0 works
and cache 3 does not; what a machine can lose is the backend, and what
it falls back to had two drawing defects of its own -- both fixed
2026-08-16, on the reasoning that the fallback is no longer a path a
user chose. Both were measured against cache 0 on the same frame rather
than argued from the code, and the same measurement caught the backend
being the wrong one of the three on the first of them -- twice over, as
it turned out: the fill, and then the triangulation underneath it. The
third thing that measurement appeared to catch, an oversized billboard
on the backend, did not survive being measured a second way (3.6).

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
match the GL renderer. Both things it left open are now closed, and
neither was what the audit said it was: the backend does draw a section
cap (see below), and Transform edit mode does not leave the object
see-through for a user — that reading came from a probe holding its own
call stack, so the task dialog was never deleted and the object stayed
registered on top.

**Stage 1c — the workbench scene graphs. DONE** (§3.6): Draft (wire,
text, dimension, working-plane grid), `App::AnnotationLabel`, Mesh,
Points and Assembly all draw on the backend, and cross-leg it agrees
with glr on every one of them. The single disagreement indicted **glr**,
not the backend: it drew `SoImage` capture companions at pixel
coordinates read as world units, because `billboard`/`pixelScale` were
consumed only by the bridge that feeds the backend (fixed 2026-08-16,
end of 3.6). TechDraw turned out
not to be a 3D path at all. Measured on llvmpipe **and** on the real GPU
(Mesa d3d12, RTX 3070 Ti), which read the same. Still unvisited from
§3.2: FEM (not built in this tree) and large models.

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
framebuffer. DONE, nothing found.** Draggers are the largest thing still
drawn by Coin GL on top of the backend's frame, and the Tessellation
defect showed what that exposes them to: Coin's `HIDDEN_LINE` mode
cleared colour and depth under a frame the backend had already rendered,
ignoring the `clearwindow`/`clearzbuffer` arguments it was passed. That
one is fixed where the mode is chosen; this stage asked whether anything
else of that shape is left.

`fcad-probes/dragger_composite_probe.py`, 25/25 on both legs -- xvfb
/ llvmpipe and the real GPU (`renderer-desktop.sh`, Mesa d3d12 / RTX
3070 Ti). Everything is `grabFramebuffer()`: `saveImage` renders
offscreen, never composites, and is blind to the entire class. The
numbers below are the real-GPU leg.

| case | what is measured | bgfx | glr |
| --- | --- | --- | --- |
| Transform manipulator | dragger pixels, against the post-edit frame | 0.0079 | 0.0067 |
| ... with an occluder in front | occluded / free | 1.000 | 0.978 |
| shadow light manipulator | dragger pixels | 0.0010 | 0.0010 |
| ... with an occluder in front | occluded / free | 0.829 | 0.782 |
| Sketcher edit mode | ink in the window, in edit mode | 0.0133 | 0.0070 |
| injected Coin bar | occluded / free | **0.620** | **0.620** |
| frame cleared under the overlay | share of the scene lost | 0.0000 | 0.0000 |

Nothing clears or overwrites what the backend drew: the "lost" column is
zero in every case on both paths. Three findings are worth keeping:

- **The draggers cannot answer the depth question at all.** Both of them
  draw over everything, on *both* paths -- the glr leg proves it is by
  design and not a backend gap, since there the occluder is certainly in
  the depth buffer and the manipulator still shows through it. So the
  probe injects a shape that does take part in the depth test: a bar
  through the middle of the box, added to the **superscene**, which the
  render cache does not capture, so Coin draws it itself. With the
  backend's frame underneath, Coin hides the middle of the bar and draws
  the two ends -- and the occluded fraction is **0.620 on both paths, to
  three decimals, on llvmpipe and on the real GPU alike**. The depth
  `glBlitFramebuffer` in `BGFXView::blit` therefore delivers Coin the
  same depth Coin would have computed itself, MSAA resolve included.
- **The composite surface is smaller than it looks, and the backend
  draws most of the draggers itself.** `SoFCRenderer::render` returns
  early whenever the backend reports `canSkipInternal()`, so everything
  the render cache captures is drawn by the backend, dragger geometry
  included. Measured rather than inferred, with
  `fcad-probes/dragger_owner_probe.py`: `saveRenderDump(source=
  "renderer")` reads the bgfx framebuffer back *before* Coin composites,
  so what is in the window and not in that readback is Coin's.

  | overlay | window | backend framebuffer | drawn by |
  | --- | --- | --- | --- |
  | Transform manipulator | 0.0120 | 0.0116 | the backend |
  | Sketcher edit geometry | present | present | the backend |
  | shadow light manipulator | 0.0015 | **0.0000** | Coin, on top |

  The Transform dragger's arrows, rings and handles are backend
  geometry, and so is the whole Sketcher edit scene -- edit lines, point
  markers and the plane cross-hairs are all in the pre-composite
  readback. The shadow light manipulator is the exception, and for the
  reason 3.4 already gives: its light node sits *above* the render-cache
  traversal root, so neither the light nor its dragger is in the
  backend's feed. What is left for Coin, then, is that manipulator, the
  NaviCube, the axis cross, dimension text and the rubber band --
  overlays that ignore depth by design, plus the NaviCube's own
  `glClear(GL_DEPTH_BUFFER_BIT)`, which is last in the frame and takes
  nothing with it.
- WARNING: **the Transform-edit transparency defect is worse than 3.3
  recorded, and worse on glr.** Leaving edit mode does not heal it, and on the real
  GPU the glr path loses the faces *outright* (0.904 of the scene reads
  as background afterwards) while the backend keeps them at 0.758. On
  llvmpipe the two paths do not separate at all (0.667 and 0.636), which
  is why this needed the real-GPU leg. It is still the render cache's, not
  the backend's -- but it is the reason this probe takes its Transform
  baseline *after* leaving edit mode: measured against the pre-edit
  frame, the defect is three quarters of the difference and the dragger
  is lost inside it.

**Stage 2 — flip the defaults. DONE** (2026-08-16), and further than
this line asked: the render path is not a persisted setting at all.
`RenderParams::selectRenderPath()`, called once from
`Gui::Application::initApplication` before anything reads either key,
sets `RenderCache` to 3 and `Type` to the engine's backend, **overriding
whatever the configuration carries**. So no migration is needed for
existing configs, a machine that once wrote a bad choice does not keep
it, and a `Type` naming a backend this build does not have cannot
survive into it -- the type is resolved against
`RendererFactory::types()`, falling back to `Default` when nothing is
registered (a build without `BUILD_BGFX`), so it never asks for a
backend nobody can create.

Runtime changes work exactly as before: setting either parameter from
the console or a script re-selects the path for that session, which is
what an A/B comparison needs. Nothing in the tree selected it any other
way -- every `scripts/demo-*.py` sets `RenderCache` at runtime, and
`render-verify.sh`'s `--user-cfg` is an empty isolation config, not a
path selector -- so no harness changed. ⚠️ What no longer works is
selecting the path *from* a config file, since startup overrides it.

The `View` preference-pack template carried `RenderCache` and `UseVBO`,
which would have imposed the author's render path and driver switch on
whoever applied the pack -- the travel rule of `docs/ViewSettings.md` 2,
in a place that predates it. Both are out of the template.

One prerequisite of this stage is already in: while the backend was
opt-in, it was acceptable for it to be built by the first 3D view and
for that view to cost a second. As a default it is not: a first New
Document took 1.4 s. The backend now comes up **during the splash**
instead, documented in `docs/RenderEngine.md` under "Startup and backend
lifetime", which brings the first document to 0.236 s and, as a
deliberate side effect, keeps bgfx alive for the whole session rather
than shutting it down with the last 3D view. The same work fixed a
defect that would have hit every user on this default: Qt 6.4+ recreates
a top-level's native window on its first `QOpenGLWidget`, so the main
window vanished and came back on the first document.

**Stage 3 — hide the switches. DONE** (2026-08-16). The render cache,
renderer type, `UseVBO` and `TransparentObjectRenderType` are off the
3D View preference page. The parameters remain the debug/A-B route,
from the console or a script (not `--user-cfg` -- see stage 2). The
Coin path stays fully functional and fully tested; it just stops being
something a user can wander into.

The one place the UI still moves the render cache is the Clipping
panel's offer to switch to the renderer when the section fill needs it.
Under this default it is unreachable in an ordinary session and only
answers a developer who switched the path off at runtime, which is
still coherent, so it stays.

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
- **4b -- the ground moves to the backend. DONE** (2026-08-16). The
  Coin geometry in `pcShadowGroundGroup` is gone: the `SoFaceSet` and
  its `SoCoordinate3`, the light model, the shape hints, the two
  texture-coordinate nodes and the `SoPolygonOffset` that existed only
  to match the one `PartGui::ViewProviderPartExt` puts on nearly every
  shape. `Render::LightConfig` is the only ground now.

  Two things the removal turned up, and neither was the geometry:

  - **The port had never seen two of the ground's properties, and both
    default on.** `ShadowGroundShading` and `ShadowGroundBackFaceCull`
    are the ones Coin states as nodes *above* the quad -- an
    `SoLightModel` and an `SoShapeHints` -- rather than on the quad
    itself, so the bridge, which walks the ground's own properties,
    never carried them. The backend's ground was lit and two-sided
    whatever they said: a camera below the ground plane was shut out by
    a grey slab where Coin let it look straight through. Both are in
    `LightConfig` now (`groundShading`, `groundBackFaceCull`), read by
    `translateLightConfig`, applied in `submitShadowGround` as the
    lighting and two-sided bits of `u_params` plus a `BGFX_STATE_CULL_CW`
    -- the `mat.ccw` case of every other cull site, since `groundQuad`
    winds its corners counter-clockwise about +Z. Streamed at
    `SceneDump` v58, so the browser tier gets them too.
  - **Creating those properties is now a job of its own.** The bridge
    only *reads* the view; `_shadowParam` is what brings a property into
    being, and it was being called incidentally, while the Coin nodes
    were configured. With the nodes gone the whole `Shadow_Ground*`
    family would never have been created, and a per-view override could
    not have been set at all -- assigning one from Python fails rather
    than creating it -- while the global preference kept reaching the
    backend and hid the hole. That is the same trap `Shadow_ShowGround`
    fell into once already (sec 3.4). `materializeGroundParams()` is the
    one place that creates them now, called from `activateShadow()`.

  `fcad-probes/ground_backend_probe.py`, 15/15: the family
  materializes; the quad draws in the ground colour property and
  carries its shadow (stddev 33.7 of the green channel);
  `GroundShading` off flattens it to the property colour outright (mean
  203.6 against 0.8 x 255 = 204, stddev 10.1); from below,
  `GroundBackFaceCull` on
  leaves **0** ground pixels and shows the box behind it, off fills
  12383 and hides it; explicit half extents resize it; transparency 1
  removes it. `renderer_light_probe.py` 10/10 and
  `clip_bounds_probe.py` 5/5 are unchanged by this.

  What the Coin paths lose is the ground: glr and cache 0 draw the
  scene and its shadow *map* with nothing to receive it. That is stage
  4c's accepted cost arriving one stage early, and the probe asserts it
  rather than leaving it to be discovered. Two consequences worth
  writing down:

  - `ground_parity_probe.py` is **retired** -- its whole method is a
    bgfx-vs-glr comparison of two grounds, and there is one ground now.
    Its header says so and points here.
  - glr's clip planes still fit a ground it does not draw (measured:
    `clip_bounds_probe.py`'s glr rows are unchanged). `onGetBoundingBox`
    is gated on the renderer, so the number is a cached traversal
    answering, not a live report -- the same caching separator sec 1c
    documents. Harmless, and on a developer path: the planes are wider
    than the drawn scene needs, never tighter.
- **4c -- the plain-Coin path. DECIDED 2026-08-16: acceptable.**
  `SoShadowGroup` is Coin's only shadow implementation, so a cache-0
  user loses shadows outright, and that is the accepted cost. Taken
  deliberately, which is all this stage asked for; it is not a refactor
  to fall into, and it is not one to keep re-opening either. Nothing
  else in stage 4 is gated on it now.

  What the decision rests on is stages 2 and 3: the render path is no
  longer a persisted setting and no longer reachable from the
  preferences, so cache 0 is a developer's A-B route rather than
  somewhere a user can end up. Losing shadows there costs a user
  nothing, because no user is there.
- **4d -- map `Shadow_*` onto the render properties, and migrate. DONE**
  (2026-08-16). The draw style's per-view family is gone; what a
  document carries now is what reads it.

  **The map.** Two destinations, because the light already had one:

  - The **light** goes to the `Render_Light*` properties stage 4a
    built -- deliberately, "to match the `Shadow_*` shape a later stage
    has to migrate from", defaults included, which is why this half is
    a rename and not a translation. `Shadow_SpotLight` ->
    `Render_LightSpot`, `SpotLightPosition` -> `LightPosition`,
    `SpotLightCutOffAngle` -> `LightCutOffAngle` (degrees on both
    sides), `SpotLightDropOffRate` -> `LightDropOffRate`, plus
    direction, colour and intensity.
  - The **shadow map and its ground** keep their names under a new
    prefix: `RenderShadow_<Name>`, group **"Render Shadow"**. The
    property editor drops a `<group without spaces>_` prefix from what
    it shows (`PropertyModel.cpp` `setPropertyItemName`), so stating
    the group is the whole of grouping -- `RenderShadow_Epsilon`
    displays as "Epsilon" under a "Render Shadow" heading, and
    `_containerProperty` now strips the spaces when it builds the name.
    That is the pattern for splitting the rest of the `Render` group
    later.

  Three pairs have different property types -- the style constrained
  its floats and stated its cone as an `App::PropertyAngle`, the
  renderer's are plain floats of the same units -- so the migration
  carries the *value*, not the property. Both sides of every numeric
  pair derive from `PropertyFloat` or `PropertyInteger`, which makes
  that two lines rather than a table of casts.

  **The custom-parameter rule follows the prefix.** A `RenderDebug_`
  property outside the fixed list is a shader uniform (sec 2.5 of
  `docs/RenderDebug.md`); `RenderShadow_` now works the same way,
  excluding the names `shadowRenderPropertyNames()` lists -- those are
  settings the engine reads itself, and each would otherwise upload a
  uniform nobody declares.

  **All three stores of sec 3.4, plus a fourth.** `migrateShadowProperties`
  runs on `View3DInventor::Restore` (the property), on
  `ViewProviderSavedView::finishRestoring` (the saved view's own copy,
  which has to be converted *there* because the enum persists as an
  index and the index outlives the entry) and on `apply()` (what it
  hands a view). The camera blob is the fourth: `setCamera` parses
  `## overrideMode: Shadow` and used to assign it straight to
  `DrawStyle`, which after 4e would name nothing -- it now calls
  `applyLegacyShadowStyle` instead, the same "light on, map on" the
  restore path uses, leaving the display style the view's own.

  A `DrawStyle` of "Shadow" becomes **the display style it wrapped**
  (`Shadow_DisplayMode`) with `Render_Light` and `Render_Shadow` on.

  `fcad-probes/shadow_migrate_probe.py`, 22/22, on two legs. A
  synthetic round trip -- write the old names with distinctive values,
  save, reopen -- is the only way to check that a value survives, and
  it covers all three type-converting pairs. The second leg is this
  repository's own `data/examples/render/effects-showcase.FCStd`, which
  really does carry `DrawStyle` index 8 and twenty `Shadow_*`
  properties: it comes back with none of them, all twenty under their
  new names, `DrawStyle` = "As Is" (its `Shadow_DisplayMode` was 2) and
  the renderer's light on. A document that never used the style
  restores untouched, light still off.

  **What is deliberately left, and for whom.** The global `View/Shadow*`
  preferences do not move: they are still the fallback defaults, and
  their preference-page section is still the Draw styles one. That is a
  stage of its own *after* 4e, since 4e is what makes the page section
  obsolete. One consequence is already visible and is the reason to
  schedule it: a machine whose config carries `View/ShadowLightIntensity`
  no longer has it reach the light, because the light's default is
  `Render/LightIntensity` now. Per-view and per-document values migrate;
  a *global* preference does not. (Measured, not deduced: the ground
  probe's lit means moved 217.3 -> 207.2 across this stage on a config
  carrying 0.9 there, and the probe now states the light itself so its
  numbers are reproducible.)

  Also left until 4e, because `activateShadow()` still reads them:
  `Shadow_DisplayMode` (spent by the migration, but the live style still
  materializes it), `Shadow_BoundBoxScale`, `Shadow_MaxDistance` and
  `Shadow_TransparentShadow` -- Coin light-camera and shadow-group
  settings with no counterpart in the backend, which fits its own light
  camera. They go with the style that reads them.

  ! While this stage stands alone, picking "Shadow" from the menu no
  longer survives a round trip: the document is converted on reopen.
  That is the intent -- 4e removes the entry -- but it is a live
  behaviour change, not only a file-format one.
- **4e -- drop `Shadow` from `drawStyleNames()`. DONE** (2026-08-16),
  and with it everything the entry was the only caller of: the
  `SoShadowGroup`, both `SoFC*Light` nodes with their draggers,
  `activateShadow`/`deactivateShadow`, the sub display mode, the
  bounding-box-scale/max-distance/transparent-shadow knobs, the
  shadow-cache redraw workaround with its timer and slot, and the
  light manipulator with its repeat-press shortcut. `View3DInventorViewer`
  loses about 380 lines.

  **The index, asserted rather than assumed -- and then gated.**
  `App::PropertyEnumeration` persists as an index, so a document
  written before this holds `DrawStyle = 8`, one past the end of the
  list this build has. Recognizing it is only sound while `Shadow` was
  the last entry, so `migrateShadowProperties` counts
  `drawStyleNames()` and treats index 8 as the legacy Shadow **only
  while the list is exactly 8 long**. A gate rather than an assertion,
  because the two failure modes are not symmetric: if a style is added
  later, failing closed leaves an old document unmigrated, while
  failing open would restyle a new one. `ViewParams.py` says so where
  the list is written.

  **The Shading panel's "Shadows" switch is the replacement, and it is
  a better one.** It sets `Render_Light` (and `Render_Shadow` with it)
  and leaves the display style alone -- the whole point of the move.
  The old switch had to host the style you were in and hand it back on
  the way out, because the draw style occupied the same slot as
  "Flat Lines". Now shadows compose with whatever you are looking at,
  the way the other shading switches do.

  What goes with the style and is **not** replaced: the light
  manipulator (a dragger on a Coin light node; the backend's light has
  no scene-graph presence), and shadows on the plain Coin paths --
  stage 4c, decided and accepted.

  ! `Std_DrawStyleShadow` no longer exists as a command, so a
  keyboard shortcut or toolbar customization naming it is dropped by
  Qt's own "unknown command" path. `V,9` is free.

  Five preference-page entries went with it -- `ShadowUpdateGround`,
  `ShadowDisplayMode`, `ShadowBoundBoxScale`, `ShadowMaxDistance`,
  `ShadowTransparentShadow` -- because they configured the Coin shadow
  group and its light camera and now drive nothing. The *keys* stay in
  `ViewParams` (`ShadowDisplayMode` is still what a pre-4d migration
  reads as its default display style); it is the UI that goes, a
  preference nobody can act on being worse than none. The rest of that
  section is the renderer's defaults and moves with the deferred
  preference stage.

  **Three defects this stage's probes caught, all of them silent.**
  Worth recording as a class: each was a *name or field that something
  else keys on*, and each failed by doing nothing rather than by
  breaking.

  - !! **An enum index that no longer has a name reads back as -1,
    not as itself.** `Enumeration::getInt()` answers -1 for any index
    it cannot name, so the migration's `getValue() == 8` test never
    fired once 4e removed the entry -- and the repository's own example
    document restored holding an unnameable style (`DrawStyle` reads
    `None` from Python) while every property around it migrated
    correctly. What is observable is the *invalid state*: entries
    present, no valid index. The same trap then bit one level down --
    `Shadow_DisplayMode` restores with no names either, so it read as
    -1 and every document would have migrated to the first sub mode
    instead of its own (measured: "Flat Lines" where the file said
    "As Is"). Supplying the names first is the fix, as
    `ViewProviderSavedView::finishRestoring` already does for the draw
    style; `Enumeration::setEnums` keeps a stored index when the old
    list was empty.
    ! A probe asserting "the style is no longer Shadow" **passed**
    through both of those. `None` is not `"Shadow"`. Assert the value
    it should BE.
  - ! **`LightConfig::operator==` had not learned 4b's two new
    fields**, so `BGFXRenderer::setLightConfig` saw no change and never
    marked the scene dirty: `GroundShading` and `GroundBackFaceCull`
    read correctly and changed nothing. It worked while the Shadow draw
    style existed because a `Shadow_*` property change re-applied the
    whole override, which dirtied the scene by another route -- so the
    defect shipped inert in 4b and surfaced only when 4e removed that
    path. A new `LightConfig` field belongs in three places, and the
    struct now says so: the stream, the layout assert, and the
    comparison.
  - **Prefix tests do not match a longer prefix.**
    `onViewPropertyChanged` keys on `Render_`, which
    `RenderShadow_GroundShading` does not start with, so editing one of
    the migrated properties scheduled no redraw. Same for the render
    capture's sidecar property list (`View3DInventorPyImp`). Both now
    name the new prefix -- the cost of a prefix taxonomy, and the
    reason `shadowRenderPropertyNames()` exists as one list rather than
    a scattering of string tests.

Not in scope, and not close: Coin as scene graph, traversal and picking.
Replacing that is a different project — the backend has no picking at all
and the whole feed is built on Coin traversal.

## 5. Evaluated and not taken: one capture root to catch everything

Stage 1b left an obvious-looking follow-on: if what Coin still draws is
what sits *outside* the render cache's capture, raise the capture root up
the graph until nothing is outside it. Evaluated 2026-08-11, and the
answer is no -- not because it is hard, but because it buys almost
nothing, costs a per-frame full re-traverse, and works against the reason
the captures are split up in the first place. Recorded here so it is not
re-proposed from the same premise.

**The layout, measured** (`fcad-probes/capture_root_probe.py`, 6/6). The
children of Quarter's superscene, in order:

```
superscene (SoSeparator, renderCaching OFF, boundingBoxCaching OFF)
  DirectionalLight       backlight
  DirectionalLight       headlight, tracks the camera
  OrthographicCamera     the view camera
  SoFCUnifiedSelection   <- the capture root today
  Callback               renderer bounds (1c)
  Group                  aux root: on-top group, editing root, dimensions
```

**1. The node-id gate makes a higher root re-capture on every camera
move.** `SoFCRenderCacheManager::render` gates the capture on
`sceneid != path->getTail()->getNodeId()`, and `SoNode::notify` bumps the
unique id of every node a notification passes through. The camera and the
headlight are siblings of the capture root, so a higher root sees both:

| event | superscene id | capture root id |
| --- | --- | --- |
| one camera move | 1557 -> 1560 | 1556 -> 1556 |
| one geometry change | 1560 -> 2056 | 1556 -> 2051 |
| five camera moves | 10 bumps | none |

Two bumps per camera move, so orbiting would re-traverse the whole scene
every frame -- on exactly the models the backend exists for. The
headlight would do it independently, tracking the camera.

**2. There is no traversal cost to win back.**
`SoFCUnifiedSelection::GLRenderBelowPath` calls the manager and returns
*without descending* when it renders, so Coin's per-frame walk already
stops at the capture root. Raising the root moves that stop one level up
and saves nothing.

**3. There is almost nothing left to catch.** Stage 1b measured who draws
what: among nodes in the graph, only the **shadow light manipulator** is
still Coin-drawn. The aux root's contents already reach the backend --
the on-top group through `addSelection`, the editing graph and the
dimensions through their own overlay captures. Everything else Coin
draws (NaviCube, axis cross, fps readout, graphics items, rubber band,
datum labels) is not scene-graph content under the superscene at all; it
is raw GL in `renderScene`, mirrored by the nine `OverlayCapture` feeds.
A graph root cannot catch drawing that is not in the graph.

**4. The split is invalidation partitioning, not accidental
fragmentation.** Each capture root is its own dirty domain with its own
id gate. One root would couple a 60 Hz Sketcher drag to a full
re-traverse of the entire scene, where today that drag re-captures only
the small editing graph. The pressure is therefore toward *more* roots,
not fewer, until invalidation stops being node-id-based
(`docs/IncrementalPublish.md`, where `publishdelta` already lives).

**What to do instead**, depending on which goal the proposal was serving:

- *"Nothing should be drawn by Coin"* -- add one more `OverlayCapture`
  for the shadow light manipulator, mirroring `editingCapture`: a sibling
  root fed as a scene-camera overlay, no graph surgery. WARNING: it has a
  scheduled expiry -- stage 4e deletes the Shadow draw style and that
  manipulator with it. When `Render_Light` gets a manipulator of its own,
  putting it under the editing root captures it for free, and this stops
  being a question.
- *"Retire Coin's frame"* -- the lever is the **trigger**, not the root.
  `SceneServeSource` already runs the capture standalone
  (`manager->traverse(root, viewport)`) outside any GL traversal. Doing
  the same on the desktop, before `renderer->render()`, would remove the
  one-frame lag that `needsRedraw()` + `scheduleRedraw()` currently
  papers over -- the feed is built *after* the backend has drawn the
  frame it belongs to -- and would make Coin's GL pass optional rather
  than structural. That is the change worth designing.

Revisit the raise only after invalidation is no longer node-id-based. Its
whole cost is in that gate.
