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
substitutes the camera basis scaled to world units per screen pixel --
`SbViewVolume::getWorldToScreenScale` over the viewport width, which
carries the anchor's own depth through the perspective divide -- instead
of calling the node's `GLRender`. `pixelScale` when set, the backend's
glyph factor when not (`SoAutoZoomTranslation::DefaultPixelScale`, so
the two paths read one number). Measured on the label of the table
above: the delta a label adds to its frame went from **18042 pixels to
459**, which is what the backend and cache 0 both read, to the pixel.

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
being the wrong one of the three on the first of them.

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
