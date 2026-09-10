# TechDraw: porting upstream, and GPU-assisted sectioning

Status: evaluation, measured 2026-08-24 against `upstream/main`
`2bb00c9186` (2026-08-20) and this fork at `4b5977c92b`. Nothing has been
ported and nothing has been changed. Companion to
[FemPortEvaluation.md](./FemPortEvaluation.md), which asked the same
question about FEM and got a very different answer.

Two questions are answered here. They turn out to be related, because the
section code is the same on both sides of the fork, so whichever tree wins
the first question inherits the second.

## 1. The short answers

1. **Can upstream's TechDraw be built against this fork's core?**
   Yes, and cheaply. This is the opposite of the FEM result. A
   syntax-only compile of all 96 upstream `TechDraw/App` sources against
   the fork's core produced **zero unexplained errors** -- every failure
   traces to one of six small, named API gaps. The 142 `TechDraw/Gui`
   sources add about four more. The core-dialect problem that killed the
   FEM port (Console renamed, `App::Color` moved, `Gui/Selection/`)
   **has already been solved in this fork**; TechDraw pays almost nothing
   for it.

2. **Is the port therefore cheap?** No. The cost is not the core, it is
   the fork's own TechDraw: **177 files, +4339/-3100 lines** of real
   interaction work that upstream does not have and that would have to be
   re-applied by hand. That is 5x the delta the Draft port had to carry.
   Recommendation in section 6: **port selectively, not wholesale.**

3. **Can bgfx / GPU section rendering accelerate OCCT sectioning?**
   Not the geometry -- a rasterizer cannot produce a BRep, and everything
   downstream of a section (TechDraw's vector page, hatching, dimensions)
   consumes exact geometry. What it can do is decouple *when* the exact
   geometry is needed, and that half **already works today**
   (section 8). The measured acceleration is somewhere else entirely and
   is not a GPU: the cut loop cuts every solid in the model against the
   tool, including the ones the plane never touches. Classifying first is
   **6.0x** on a 400-solid assembly (section 10).

## 2. Sizing the divergence

There is no merge base with upstream -- the fork's history was rebuilt.
But the base trick from [bim-port-survey] works here too: the last
upstream merge `bcaa82d71a` has upstream's 2023-12-31 tree
`a662fbb2ff` as its second parent, and that object is still reachable.
Diff three-way against it and the two evolutions separate.

Translations, `.qm`, `.svg` and `.png` excluded throughout (they are
+68k lines of the raw number and tell you nothing):

| | files | lines |
| --- | --- | --- |
| TechDraw, fork (BASE..OURS) | **177** | +4339 / -3100 |
| TechDraw, upstream (BASE..THEIRS) | 582 | +36831 / -22704 |

Files the fork touched that upstream did not: **zero**. Same as Draft and
Arch. Every port is a file-level transplant with the fork's delta
re-applied on top; there is no 3-way merge and no cherry-pick.

The fork's side is 100 commits. It is not drive-by maintenance -- it is a
coherent body of interaction work:

- section-line hover, preselection and dragging (`QGISectionLine`,
  `QGIDecoration` child-item highlight, +340/-110 and +158/-1)
- selection-highlight handling reworked across the whole Gui
  (`QGIView`, `QGIPrimPath`, `QGIEdge`, `QGIWeldSymbol`, `QGIHighlight`)
- `App::Link` and sub-object support in the shape extractor
- background operation via `Base::SequencerLauncher` -- upstream still
  has no progress reporting on the section cut at all
- projection-group drag, per-view parentage, view-inside-view
- the projected-spline collapse fix
- MDI/overlay integration (`MDIViewPage` +299/-162, `QGSPage` +173/-221)

## 3. The compile probe (method, and why the first run lied)

Stage upstream's tree, then build an **overlay** `src/` of symlinks --
every directory pointing at the fork, except `Mod/TechDraw` pointing at
upstream's. Compile each upstream `.cpp` with `-fsyntax-only` using the
flags ninja records for the real target, with the overlay as the include
root.

The overlay matters. The first run put the fork's `src/` on the include
path directly and reported **thousands** of errors -- 528 `redefinition`,
152 `abstract type`, 78 `scoped/unscoped mismatch in enum`. All of it was
one artifact: FreeCAD headers use `#pragma once`, which is path-keyed, so
`#include <Mod/TechDraw/App/Geometry.h>` pulled in the *fork's* copy
alongside the quoted upstream one and every class was defined twice.
With the overlay the same sweep drops to 5 errors per file.

> A probe that reports thousands of errors is usually measuring its own
> configuration. The signal here only appeared once the include graph had
> exactly one TechDraw in it.

Two residual artifact classes remain and are excluded from the counts
below, because a real port regenerates both:

- the build tree's generated `*Py.h` come from the **fork's** `.xml`,
  while upstream has moved to `.pyi` -- hence the 7
  `no declaration matches ...Py::getX` errors
- the build tree's generated `ui_*.h` come from the **fork's** `.ui`
  files -- hence every `Ui_X has no member named Y` and the
  `QGroupBox has not been declared` family in the Gui half

### Result

| | files | clean | failing only on the named gaps | unexplained |
| --- | --- | --- | --- | --- |
| `TechDraw/App` | 96 | 20 | **76** | **0** |
| `TechDraw/Gui` | 142 | 23 | 106 | 13 (mostly `.ui` noise, ~4 real) |

## 4. The complete blocker list

This is the whole cost of making upstream's TechDraw compile here.

**Fork must gain (upstream APIs we lack):**

| gap | hits | size |
| --- | --- | --- |
| `App::DocumentObject::canRecomputeOnWorker()` | 155 | one virtual, default `false` |
| `Gui::Command::openActiveDocumentCommand()` | 29 | helper |
| `Gui::Command::commitCommand(int)` / `abortCommand(int)` | 35 | transaction-id overloads |
| `App::TransactionName`, `App::TransactionCloseMode` | 4 | two enums |
| `Gui::MDIViewPy` public dtor, `getattr`, `create` | 22 | access + 2 methods |
| `Gui::BaseView::onMsg(const char*)` (upstream dropped the 2nd arg) | 16 | signature |
| `Gui::FileDialog::FilterList` | 8 | one typedef |
| `Gui/ToolHandler.h` | 2 | 428 lines, self-contained |
| `Gui/Navigation/NavigationStyle.h` | 1 | include-path move |
| `Gui/PreferencePages/DlgSettingsPDF.h` | 1 | one page |
| `Gui::View3DInventorViewer::RenderIntent` | 1 | one enum |
| `Base::Color::fromValue`, `Base::UnitsApi::toUnicodeSuperscript` | 3 | two statics |
| `Part::ShapeOption` | 3 | one enum |
| `Mod/Part/App/FCBRepAlgoAPI_{Cut,Common,Fuse,Section}` | 22 | ~90 lines each + a base |
| `Mod/Measure/App/ShapeFinder` | 3 | 582 lines |
| `App::PropertyMap::getItemPath`, `QLineEdit` expression binding | ~6 | small |

**Upstream must gain (a fork API it does not implement):**

| gap | hits | size |
| --- | --- | --- |
| `App::Property::isSame()` is pure virtual here; upstream's `PropertyCenterLineList`, `PropertyCosmeticEdgeList`, `PropertyCosmeticVertexList`, `PropertyGeomFormatList` do not implement it | 524 | four element-wise comparisons |

**Trivial:** `App::Color` is a `using` alias for `Base::Color` here, and
upstream forward-declares it as `class` (21 hits).

Note what is *absent* from that list. No `Materials` module dependency,
no `ShapeAppearance`, no `App::Color`-to-`Base::Color` migration, and the
830 lowercase `Console().warning()` calls compile unchanged -- the fork
already has the lowercase aliases. Those four are exactly what made the
FEM port not worth doing, and TechDraw does not hit any of them.

## 5. What each side would gain and lose

**Upstream has that we lack** (new files, so easy to spot):

- `DrawBrokenView` -- break views, a genuinely missing feature
- `DimensionAutoCorrect` -- dimension repair
- `LineFormat` / `Tag` -- the line-format and tag refactor
- `CommandAlign`, `QGIBreakLine`, `QGIDatumLabel`
- `QGVNavStyleSolidWorks` -- another page navigation style
- `TechDrawHandler` on `Gui::ToolHandler` -- upstream's interactive
  tool-state refactor
- `.pyi` stubs throughout, and `ConversionUtilities/` migration scripts

**We have that upstream lacks:** everything in section 2's list. The two
that matter most and would be most painful to lose are the **selection
and preselection highlight rework**, which touches nearly every `QGI*`
class, and **`App::Link` / sub-object support**, which is the fork's
whole reason for existing. Upstream's answer to the second is
`Measure::ShapeFinder`, a different design for the same problem -- so
this is not a merge, it is a choice between two implementations.

## 6. Recommendation

**Do not transplant wholesale.** Unlike Draft, where the fork's delta was
33 files and upstream's evolution was overwhelming, here the fork's delta
is 177 files of interaction work that upstream has no equivalent for, and
that would have to be re-derived against a Gui whose selection plumbing
upstream also rewrote (`TechDrawHandler`). The two rewrites collide
head-on.

**Do port selectively.** The blocker list in section 4 is small enough
that individual upstream features can be pulled across one at a time:

1. `Gui/ToolHandler.h` + `canRecomputeOnWorker` + the `Gui::Command`
   transaction overloads. Small, useful beyond TechDraw, and unblock
   most of the rest. Do these first regardless.
2. `DrawBrokenView` -- self-contained new files, needs only
   `FCBRepAlgoAPI_Cut`. The clearest single win.
3. `LineFormat` / `Tag`, `CommandAlign`, `QGVNavStyleSolidWorks` --
   independent of the selection rework.
4. `DimensionAutoCorrect` + `Measure::ShapeFinder` -- **only after**
   deciding whether ShapeFinder or the fork's Link resolution is the
   sub-element story. Do not take both.

Leave the `QGI*` selection layer alone. That is the fork's, it works, and
upstream's version solves a problem the fork solved differently.

## 7. What sectioning costs today

`DrawViewSection::doSectionCut` (`src/Mod/TechDraw/App/DrawViewSection.cpp:504`):

    TopExp_Explorer expl(params.baseShape, TopAbs_SOLID);
    for (; expl.More(); expl.Next()) {
        BRepAlgoAPI_Cut mkCut(s, params.cuttingTool, pi->Start());
        ...
    }

Three things about that loop:

- **Every solid is cut**, with no test for whether the cutting tool can
  possibly reach it. The tool is a prism covering one half-space, sized
  from the shape's bounding-box diagonal, so on any real assembly most
  solids are entirely on one side of it and the boolean is doing work
  whose answer is "unchanged" or "gone".
- It is **sequential**. It runs inside a single `QtConcurrent::run`, so
  the whole model is cut on one core.
- It is preceded by `BRepBuilderAPI_Copy` of the entire base shape on
  every recompute.

Upstream's copy of this function has the same loop with the same absence
of classification, so the port question does not affect this section.
(Upstream does wrap the boolean in `FCBRepAlgoAPI_Cut`, which adds a
fuzzy-tolerance retry, and it has no progress reporting -- the fork's
`SequencerLauncher` work is ahead here.)

The 3D view has a second, unrelated path: `Part/Gui/SectionCutting.cpp`
builds a real `Part::Cut` object and calls `recomputeFeature()`
synchronously on the main thread as the user drags a slider. Its own
source comments the consequence at line 1100 -- *"we disable the sliders
because for assemblies it will takes ages to do several dozen
recomputes"*.

## 8. What the renderer already does

`Gui/Clipping.cpp` puts `SoClipPlane` nodes in the scene graph.
`SoFCRendererBridge.cpp:1155` translates them into
`Render::Material::clipplanes`, and `BGFXFrame.cpp:6387`
(`submitSectionCaps`) draws stencil-parity section caps for them --
front/back face counting to find where the plane is inside a solid, then
a cap quad masked by the stencil, with hatch texture, transparent and
opaque buckets, and a feed into the AO/depth prepass so the passes
downstream stop shading what the cap hides.

**So the GPU half of "preview instantly, compute exactly later" is
already built and working.** It produces pixels, in a pass whose cost is
a handful of extra draws, and it is correct for the thing it is for.

What it cannot produce is geometry. There is no vector outline, no face,
no edge, nothing to hatch along, dimension against, export to SVG/DXF, or
feed to another boolean.

## 9. What a GPU can and cannot do here

Ruled in:

- **Preview during interaction.** Already built (section 8).
- **Decoupling.** Show the stencil cap while the plane moves; run the
  exact OCCT cut when it settles. The fork already has the machinery for
  the second half -- `DrawViewSection` runs its cut on
  `QtConcurrent::run` behind a `SequencerLauncher` with a cancel path.
  `Part/Gui/SectionCutting.cpp` does not, and that is where the "ages"
  comment lives.
- **Tessellation-plane intersection** for a fast approximate section
  polyline (for a draft-quality TechDraw view, or hatching preview).
  Cheap, trivially parallel -- but note this is a *mesh* operation, not
  a GPU one. It runs fine on the CPU and the meshes are already in the
  render cache.

Ruled out:

- **Replacing the boolean.** A rasterizer has no exact representation to
  hand back.
- **Feeding OCCT seed geometry from the framebuffer.** OCCT cannot
  consume an approximate cap polygon as exact input; you would be
  re-fitting surfaces to pixels.

Ruled out **by measurement**, which is the interesting one:

- **GPU-assisted HLR.** The idea is sound in principle -- hidden-line
  removal is a visibility problem and a depth buffer answers visibility
  -- and it is the natural GPU play in TechDraw, since HLR is the other
  half of a section view's cost. But the premise is that OCCT's HLR
  scales badly with mutual occlusion, and here it does not. Stacking N
  perforated solids along the view axis so each occludes all the ones
  behind it:

  | N | faces | HLR | per solid |
  | --- | --- | --- | --- |
  | 10 | 130 | 0.017 s | 1.7 ms |
  | 20 | 260 | 0.033 s | 1.6 ms |
  | 40 | 520 | 0.071 s | 1.8 ms |
  | 80 | 1040 | 0.150 s | 1.9 ms |

  Linear across 8x, and HLR came in at **0.4-0.5x the cost of the cut**
  on the same shapes. There is no quadratic blow-up to rescue. A GPU
  visibility pass would trade exactness for a constant factor on the
  cheaper of the two stages. **Not worth building.**

  Caveat: these are boxes with cylindrical holes. Curved-face silhouette
  extraction (`Contap`, the code path behind the OCCT 8.0.1 HLR segfault
  fixed in `da16badbb5`) is heavier and was not exercised. The
  conclusion holds for the shape class measured, not for all shapes.

## 10. The measured win, and it is not a GPU

Cutting a solid the plane never touches is not free. Against a
TechDraw-shaped tool (half-space prism, 4000 mm):

| case | simple solid (7 faces) | perforated + filleted (13 faces) |
| --- | --- | --- |
| straddles the plane | 2.20 ms | 4.60 ms |
| entirely inside the tool (removed) | 0.93 ms | 1.46 ms |
| entirely on the keep side (untouched) | 0.51 ms | 0.76 ms |
| **bounding-box classify instead** | **0.012 ms** | **0.031 ms** |

OCCT does bail out early -- an untouched solid is 4x cheaper than a real
cut, not equal to it -- but it still costs **25-47x** what deciding the
question costs, and the gap grows with face count.

End to end, on a 400-solid grid assembly with the section plane crossing
5% of it (20 straddle, 180 kept whole, 200 removed):

| | time |
| --- | --- |
| cut every solid (what the code does today) | 0.404 s |
| classify by bounding box, cut only the straddlers | 0.068 s |
| | **6.0x** |

The classification is **exact, not approximate**, which is what makes it
safe: a bounding box entirely on the keep side of the plane guarantees
the solid is untouched, and one entirely inside the tool guarantees it is
removed. Only a straddling box needs the boolean. Nothing is traded away.

The same test with a degenerate distribution (all 400 solids on the keep
side, zero straddlers) gives 25.3x. That number is not representative and
is recorded here only so it is not mistaken for the headline.

## 11. Proposal, in priority order

1. **Classify before cutting**, in `DrawViewSection::doSectionCut` and in
   `Part::Cut`'s equivalent. Measured 6.0x, exact, maybe 30 lines. There
   is no argument against it.
2. **Parallelise the straddlers.** Once classification has reduced the
   list to the few solids that actually intersect, run those booleans
   concurrently instead of sequentially inside one worker. This composes
   with (1) -- it is (1) that makes the list short enough to be worth a
   thread pool.
3. **Drop the unconditional `BRepBuilderAPI_Copy`** of the whole base
   shape per recompute, or narrow it to the straddlers.
4. **Give `Part/Gui/SectionCutting.cpp` the treatment `DrawViewSection`
   already has**: stencil cap while the slider moves (free, already
   rendering), exact `Part::Cut` on a worker when it settles, with the
   `SequencerLauncher` cancel path so a new drag aborts the old cut.
   This is the "eg in background" of the original question, and it is an
   architecture change rather than an algorithm one.
5. Tessellation-plane section polylines for a draft-quality preview --
   only if (1)-(4) leave a visible gap. They may not.

Item 4 is also the natural first customer for the out-of-process OCCT
goal in [RoadMap.md](./RoadMap.md): a section cut is a large, isolated,
cancellable, crash-prone kernel call whose result is consumed
asynchronously. It is a better shape for that work than the recompute
engine as a whole.

## 12. What was not measured

- No real assembly. Every number above is synthetic (boxes, cylinders,
  fillets) on the `conda-relwithdebinfo-801` tree. The 6.0x depends on
  the straddling fraction, which on a real model is a property of where
  the user put the plane.
- The Gui compile probe could not run `moc` or `uic`, so its 13
  unexplained files are an upper bound; about 4 are real and the rest is
  stale generated `ui_*.h`.
- Nothing was measured against upstream's actual TechDraw at runtime --
  the probe proves it compiles, not that it works. The gates would be
  `src/Mod/TechDraw/TDTest/` and the fork's own section-line
  interaction, which upstream has no test for.
- `DrawComplexSection` was not examined separately; it has its own
  cutting-tool construction and the fork has +127/-95 in it.

## 13. Addendum (2026-08-24): deeper verification, and what the doc missed

Sections 13-15 were added after a second review pass: re-reading the
code paths named above, reading the OCCT sources they call, and
surveying what commercial CAD does for the same problems. The original
conclusions all survived; four things were missing.

**The per-solid cut loop is deliberate, and must survive any refactor.**
`doSectionCut` comments why it cuts each solid individually: "to avoid
issues where a compound BaseShape does not cut correctly". One
degenerate solid poisons only its own result. OCCT does offer the
tempting shortcut of a single multi-argument boolean
(`SetArguments`/`SetTools`), whose pave filler prunes non-intersecting
sub-shape pairs through a bounding-box tree and would deliver much of
the classify win for free -- but it gives up exactly that fault
isolation. The right shape stays: classify per solid (section 10), cut
only the straddlers, each in its own `BRepAlgoAPI_Cut`.

**`SetRunParallel` is one line away and never set.** `BRepAlgoAPI_Algo`
publicly re-exposes `BOPAlgo_Options::SetRunParallel`
(occt `BRepAlgoAPI_Algo.hxx`), and the pave filler fans its stages out
over a thread pool when it is on. TechDraw never enables it. For the
few straddlers that are genuinely expensive this is internal
parallelism inside one cut, complementing proposal item 2's
across-solid parallelism (which wins when straddlers are many and
small).

**TechDraw loads the whole model into HLR as ONE shape.**
`GeometryObject::projectShape` calls `brep_hlr->Add(inShape)` once with
the full compound. Inside OCCT, `HLRBRep_InternalAlgo::Hide()` is a
pairwise loop over *loaded* shapes with a very fast fixed-point minmax
rejection per pair (`HLRBRep_InternalAlgo.cxx`, the `0x80008000` test)
-- machinery that never fires when n=1. Loading per-solid would let
OCCT skip whole non-overlapping pairs, and the `Hide(i, j)` group for a
fixed `i` writes only shape i's edge status, so it is also the natural
parallelization seam if HLR ever does become the bottleneck. Two
caveats: the shared `HLRBRep_Data` makes that parallelism nontrivial,
and section 9's measurement says HLR is not the bottleneck today. OCCT
`TKHLR` itself contains zero uses of `OSD_Parallel` -- the algorithm is
entirely single-threaded as shipped. Record the seam; do not build it
yet.

**The draft tier already exists and the doc ignored it.** `DrawViewPart`
has a `CoarseView` property; `GeometryObject::projectShapeWithPolygonAlgo`
runs `HLRBRep_PolyAlgo`, the tessellation-based HLR. Notably the coarse
path runs *synchronously* (it is fast) while the exact path goes to the
`QFutureWatcher` worker (`DrawViewPart.cpp` ~342-369). Proposal item 5
is therefore half built, and the missing half is wiring, not machinery:
show the coarse result immediately, swap in the exact one when the
worker finishes.

## 14. Prior art: how commercial CAD handles the same problems

The industry converged on exactly the two-tier architecture sections
8-9 grope toward, which both validates the proposal and names the UX.

- **Autodesk Inventor, "raster views"** (shipping since ~2012): a new
  drawing view appears immediately as a raster image (green corner
  glyphs), a background process per view computes the precise vector
  geometry, the view swaps to precise when done, and annotation is
  allowed meanwhile. This is "preview instantly, compute exactly later"
  as a product feature. With both tiers already in `DrawViewPart`
  (section 13), this fork is a wiring change away from the same UX.
- **SolidWorks**: two view qualities (draft = tessellation, high =
  exact), and since 2020 **Detailing Mode**: a drawing opens with no
  model data loaded at all -- the view geometry is cached in the
  drawing file, so a large-assembly drawing opens in seconds and
  annotates without recompute. TechDraw's `KeepUpdated` gate skips the
  recompute but does not persist the projection; the lesson is to
  **persist the projected 2D geometry in the document** so display
  never needs the kernel. That serialized vector page is also exactly
  what the browser/streaming tier would want.
- **Onshape**: drawings are computed server-side, asynchronously --
  proposal item 4's "first customer for out-of-process OCCT" framing is
  Onshape's production architecture.
- **The HLR market**: most vendors license **Siemens D-Cubed HLM**
  rather than writing HLR (Autodesk, PTC, Onshape, Shapr3D, IronCAD,
  ZWCAD are on Siemens' licensee list). HLM is a CPU component:
  exact, tolerant and **faceted** geometry in one engine, with
  visible/hidden/silhouette/outline segments and face-region output for
  hatching. Two readings: (a) nobody ships GPU HLR for dimensioned
  drawings -- exactness and associativity force a geometric algorithm,
  independently confirming section 9's measured refutation; (b) faceted
  input as a first-class citizen means a mesh tier is considered good
  enough for real drawings when tolerances are handled -- `CoarseView`
  is respectable, not a hack. (The academic side agrees: Benard &
  Hertzmann's "Line Drawings from 3D Models" covers exact-topology
  contour visibility on meshes; Blender's Line Art modifier ships it.)
- **Graphics-only sections**: SolidWorks offers graphics-only section
  views (GPU clip + cap, recommended for very large models) beside
  geometric ones; Fusion 360's section analysis is graphics-only, full
  stop. The stencil caps of section 8 are that tier, already built.

Amendments this makes to the section 11 proposal:

6. **Raster-first views** (Inventor pattern): render the `CoarseView`
   projection immediately on every view update, swap in exact HLR when
   the worker delivers. Mark the view as provisional while coarse.
7. **Persist view geometry** (SolidWorks Detailing Mode lesson): store
   the projected page in the `.FCStd` so opening a drawing runs zero
   kernel work, and the streaming tier can serve pages without OCCT.
8. Record the OCCT seams (this section and 13) so later work does not
   rediscover them: `SetRunParallel`, per-solid `Add()` into HLR, and
   the reason the per-solid cut loop exists.

## 15. The 2D page itself: what Qt rendering costs, and whether bgfx should draw it

The question behind the question: TechDraw's output is a
`QGraphicsScene` drawn by Qt's raster paint engine. Is that a problem
on large drawings, and is a bgfx 2D backend worth building?

### What the code does today

- One `QGraphicsItem` per edge, vertex and face
  (`QGIViewPart::drawViewPart`); a `QGIViewPart` is a
  `QGraphicsItemGroup` of them. A section view of a large assembly is
  tens of thousands of items.
- `QGIPrimPath::paint` is a straight `painter->drawPath(m_path)` --
  every repaint re-strokes every visible path through the CPU raster
  engine, antialiased (`QGVPage` sets `QPainter::Antialiasing`).
- Items are `NoCache` (`QGIView.cpp:78`), and the Native (raster)
  viewport is `FullViewportUpdate` (`QGVPage.cpp:361`) -- so *any* item
  update, e.g. a hover highlight, repaints the entire viewport. The
  OpenGL viewport branch gets `SmartViewportUpdate` but is disabled
  with the comment "gives rotten quality, don't use this" (the Qt GL
  paint engine has no decent AA without MSAA).
- All of it runs on the GUI thread.

### Measured (offscreen, conda Qt 6.10.1, 1920x1200, AA on, one
`QGraphicsPathItem` per path, each path 3 line segments + 1 cubic)

| paths | bare QPainter | scene render, first | scene render, warm | warm us/item |
| --- | --- | --- | --- | --- |
| 1000 | 5.9 ms | 9.9 ms | 2.8 ms | 2.8 |
| 5000 | 27.6 ms | 52.6 ms | 14.6 ms | 2.9 |
| 20000 | 103.7 ms | 271.6 ms | 67.5 ms | 3.4 |
| 50000 | 220.7 ms | 830.8 ms | 181.5 ms | 3.6 |

Roughly **3-4 us per item per full repaint**, single-threaded, and the
real items are heavier than this floor (per-item pens, cosmetic width
scaling, custom paint). So: a 5k-edge page repaints in ~15 ms and is
fine; 20k is ~70 ms (14 fps) and marginal; 50k is ~180 ms (5 fps) and
fails -- and with `FullViewportUpdate`, *hovering the cursor over one
edge* pays the full number. The failure regime is exactly the
large-assembly drawings this project cares about.

### The cheap fixes are Qt-side, not GPU

1. **Stop repainting the world per hover.** `FullViewportUpdate` was
   chosen for the raster path; Qt's default is `MinimalViewportUpdate`,
   which repaints only the changed items' rects. If Full was a
   workaround for stale bounding rects (the shape/bounding-rect caching
   in `QGIPrimPath` hints at such a history), fixing the invalidation
   is worth it: it turns hover/selection from O(page) into O(item).
   This is the single highest-leverage change and costs one line plus
   debugging.
2. **Cache what does not change.** During pan/zoom the page content is
   static; `QGraphicsView` already caches the background layer only.
   Group-level pixmap caching (per `QGIViewPart`) would make pan free
   and zoom a scale-blit until the transform settles, at the price of
   re-rendering the pixmap on zoom end. Per-item
   `DeviceCoordinateCache` at 50k items would thrash memory; the group
   is the right granularity.

### Is bgfx 2D worth it?

**Not for desktop performance alone** -- the Qt-side fixes above
recover interactivity for far less work, and 180 ms for a full 50k-edge
re-render only hurts when it happens per interaction.

**Yes on strategic grounds, for the same reason the 3D renderer
exists**: a `QGraphicsScene` cannot run in the browser/mobile tier at
all. A TechDraw page today is unreachable from the wasm viewer. The
assets are already in place:

- the vendored bgfx tree carries the **NanoVG port**
  (`src/3rdParty/bgfx/bgfx/examples/common/nanovg`) -- paths, fills,
  strokes and fontstash text on any bgfx backend; `vg-renderer` is a
  maintained alternative in the same family;
- the 3D renderer already draws antialiased lines at model scale from
  retained buffers -- a drawing page is the trivial special case
  (orthographic, fixed z, no occlusion);
- a page is *retained* geometry: tessellate once per view update, then
  every frame is a handful of draws. The per-frame CPU tessellation
  that limits NanoVG-style immediate-mode use does not apply.

The shape of the work, when the browser tier needs drawings: keep the
`QGraphicsScene` as the desktop interaction and annotation layer (it is
where 100 commits of the fork's selection work lives), and add a
renderer page backend fed by the same projected geometry --
the 2D analog of "Coin stays, the renderer draws alongside". Combined
with amendment 7 (persisted view geometry), a drawing page becomes a
streamable document like the 3D scene: serve the vector page, render it
with NanoVG-on-bgfx in the browser, annotate on the desktop.

What was not measured here: the real `QGIViewPart` per-item paint cost
(the numbers above are a synthetic floor), and no profiling of an
actual large TechDraw page -- no such document exists on this box yet.
The 6.0x/HLR numbers of sections 9-10 are unaffected.

## 16. Decision and implementation plan (2026-08-24): vg-renderer is vendored

Decision (user): the 2D vector engine is **vg-renderer**, vendored as a
git submodule from the project's own fork, like bgfx:

    src/3rdParty/vg-renderer  ->  https://github.com/realthunder/vg-renderer.git
    pinned at d4568242 ("Merge pull request #50 ... update_simd", 2026-05)

### Why vg-renderer over the vendored NanoVG

Both are bgfx-friendly fontstash-based vector engines with a
NanoVG-shaped API. The workload -- a retained page of tens of
thousands of strokes -- decides it:

- **Command lists with tessellation caching**: record a path once,
  replay without re-tessellating. NanoVG is strictly immediate-mode
  and re-flattens every path every frame, which is the same
  O(N)-per-frame CPU tax section 15 measured in Qt.
- **Batching**: same-state paths merge into few draw calls, with
  specialized shader programs (solid / gradient / image / stencil).
  NanoVG is one uber-shader with per-draw uniform pressure.
- **Stencil clip in/out**: TechDraw detail views crop to a circle.
- Alive upstream (last commit 2026-05, contributors besides the
  author); BSD-2-Clause; libtess2 + fontstash + stb_truetype vendored
  inside, so no new external dependencies.

Known gaps, accepted with workarounds: **no polygon holes** (hatching
is generated geometry anyway, and the user-supplied indexed-triangle
API covers holed face fills), no font blur / letter-spacing, no skew.
NanoVG stays in the tree regardless (it rides the bgfx submodule) but
nothing of ours will depend on it.

### Compatibility probe (already run, 2026-08-24)

`-fsyntax-only` of `src/vg.cpp`, `path.cpp`, `stroker.cpp` against the
vendored bgfx/bx headers found exactly two issues, both build-level,
zero API drift in the code itself:

1. `BX_CONFIG_DEBUG` must be defined by the including build script
   (bgfx.cmake does the same dance).
2. The checked-in `src/shaders/*.bin.h` were baked by an **older
   shaderc**: our bgfx's `embedded_shader.h` expects `_dxbc`-suffixed
   arrays, the baked ones still carry `_dx9`/`_dx11`. The `.sc`
   sources ship, so the fix is to **regenerate the embedded shaders
   with the in-tree shaderc** and commit them to the fork. The baked
   set already includes `essl`, so the wasm tier is covered once
   regenerated.

### Implementation plan (next session starts here)

**M0 -- build.** Wire the submodule into CMake: compile `src/*.cpp`,
`src/libs/*.cpp`, `src/libtess2/*.c` into (or beside) the
`FreeCADRenderer` lib when `BUILD_BGFX=ON`; define `BX_CONFIG_DEBUG`
per config; regenerate the embedded shaders (fold into the
`Renderer_assets` shaderc flow or a one-shot committed to the fork).
Smoke test: draw paths + text into an offscreen bgfx target headless
-- no display, no Qt.

**M1 -- the engine layer.** Two new translation units in
`src/Gui/Renderer/`:

- `Vg2D*`: context lifecycle around `vg::createContext`, one bgfx view
  per page surface, frame begin/end, DPI and view transform.
- `Page2D*`: the retained page store. Items keyed by stable ids
  (edge / face / vertex / annotation / decoration), each item one vg
  command list; damage-driven re-record, never rebuild-the-world
  (the project's incremental-by-default policy applies). **Zoom
  bands**: a cached tessellation is valid within a scale band
  (factor-of-2 to start); crossing a band re-records, so AA fringe
  width and flattening tolerance stay honest. Verify early how
  command-list replay behaves under a changed transform -- this is
  the one vg-renderer property section 15 flagged as unverified.
  Text through fontstash at screen-space sizes, re-rasterized per
  band; SDF only if banding artifacts show.

**M2 -- the TechDraw feed.** Extract `DrawViewPart` results
(`GeometryObject` BaseGeom edges, vertices, faces) into `Page2D`.
Hatch and holed face fills arrive pre-generated (lines / indexed
triangles), sidestepping the no-holes gap. Desktop host for
verification: render-to-texture composited the way the 3D renderer
already composites with Coin; `QGSPage` keeps owning interaction and
annotation editing -- this milestone renders, it does not select.
Templates (SVG) are out of scope here; rasterize or skip until a
vector story is designed.

**M3 -- the wire.** Serialize `Page2D` (this is amendment 7's
persisted view geometry, now with a concrete consumer) over the
SceneServer channel as journal/delta updates; the wasm viewer renders
it with the same vg code on the essl backend. A drawing page becomes
a streamable document like the 3D scene.

**Non-goals of this arc**: replacing `QGraphicsScene` interaction on
desktop, GPU HLR (refuted, section 9), and the section-cut work
(items 1-4, an independent arc). The Qt-side repaint fixes of
section 15 remain worth doing on their own and do not conflict.

## 17. Implementation status (2026-08-24): M0 and M1 are built and measured

**M0 landed** (`585020be66`; vg fork `a3c0800`). vg-renderer compiles
as a static lib beside bgfx and links into `FreeCADRenderer`;
`fcvgsmoke` draws paths + text into an offscreen frame buffer with
bgfx brought up headless -- PASS on Vulkan and OpenGL, NVIDIA, no
display. Traps that cost time, so they are written down: headless
bgfx in our fork *requires* a 0x0 resolution with all-null platform
data (a non-zero size fails init with no message); the embedded
shaders were rebaked (`src/shaders/rebake.sh` in the fork) with
glsl/essl/spv/wgsl/mtl, and consumers defined
`BGFX_PLATFORM_SUPPORTS_DXBC=0/_DXIL=0` because a Linux host cannot
produce the two Direct3D profiles -- which was wrong on Windows, where
bgfx picks Direct3D 11 by default and draws with a substituted program
instead of failing; both profiles are baked in since 2026-09-06
(vg fork `134c460`, `docs/Testing.md`, "The vg smokes on Windows"); GL read-back is bottom-up
(`caps->originBottomLeft`).

**M1 landed** (`19a3d3632c`, `a00bb0e51e`): `Vg2D` (context + font
registry) and `Page2D` (the retained store) in `src/Gui/Renderer/`.
Items hold their content as a position-independent op buffer in page
coordinates -- the retained representation, the damage re-record
source, and (M3) the wire format -- replayed into one Cacheable vg
command list each.

**The section-15 open question is answered from vg's source, then
confirmed by test**: the command-list cache keys on the state's
average scale with an *exact equality* check
(`submitCommandList`/`updateState`; translation and rotation do not
enter `m_AvgScale`). Replay under a changed scale therefore cannot
happen -- but any continuous zoom through the vg transform would
re-tessellate every list every frame. So the zoom bands moved down a
level: `Page2D` keeps only the power-of-two band scale in the vg
state and applies pan, rotation and the bounded residual
(1/sqrt(2)..sqrt(2)) in the bgfx view matrix. Within a band, vg
replays cached tessellation for every unchanged item; on a crossing,
vg itself re-records each cache at the new scale. Item re-records
happen only on damage. Rotation is clockwise-positive on screen (Qt
convention); bx's rotZ turns the other way, so the sign flips at the
matrix.

**Measured** (fcvgsmoke --bench, RTX 3060, Vulkan, offscreen 640x480,
two-segment stroke items):

    items   record-all   replay (unchanged/pan/zoom)   band crossing
    2k      11.0 ms      0.3-0.5 ms                    0.7 ms
    20k     27.0 ms      2.1-3.0 ms                    5.3 ms
    50k     52.1 ms      5.6-6.9 ms                    13.2 ms

Replay is 0.11-0.26 us/item against the 3-4 us/item Qt raster tax of
section 15 -- ~25x -- and, unlike Qt's FullViewportUpdate, an
unchanged frame does not rescale with hover or selection churn at
all. Command list slots take the whole uint16 handle space (65534; a
page beyond that drops items and counts them in
`Counters::droppedItems` -- found by the 20k bench crashing at the
old 16384 cap with `VG_CHECK` compiled out).

Correctness gate: `fcvgsmoke --page2d` drives one retained page
through identity / pan / in-band zoom / band crossing / 90deg
rotation / damage / removal with pixel probes at view-transformed
positions and counter assertions; 24 checks, PASS on both backends.

**Next: M2**, the TechDraw feed, per the plan above.

## 18. Implementation status (2026-08-24, later): M2 renders real pages

**The feed** (`6ca012ba5c`, hardening `50ed5a5cfd`):
`TechDrawGui::PageFeed` converts DrawViewParts to Page2D items --
edges/vertices/faces, ids hashed from the view's document name so a
re-fed view damages exactly its own items. Generic/bezier/bspline
keep exact form, circles stay native vg circles, arcs/ellipses are
discretized from the projected occEdge for now (native arc ops are a
listed refinement). Faces are per-wire closed contours stitched with
the Qt path builder's nearest-endpoint heuristic and filled even-odd
-- **vg's libtess2 even-odd fill over multiple subpaths does holes
correctly** (fcvgsmoke stage h), so the 'no polygon holes' gap in
section 16 is refuted and the indexed-triangle fallback is not needed
for face fills.

**Hosts.** `TechDrawGui.renderPageVg(page, path, [w,h])` renders a
page offscreen and returns feed counters + `pendingViews` (HLR and
face extraction are worker-thread async; pump the event loop until it
reaches 0). The interactive tier (`cac5bf845b`): parameter
`Mod/TechDraw/General/PageRendererVg` makes `QGVPage::drawBackground`
draw the vg page under the Qt scene at the QGraphicsView transform --
verified in the running GUI that the vg layer alone reproduces the
page at the Qt items' exact positions.

**What feeding real data taught** (all fixed): vg::polyline appends
and null-derefs without a prior moveTo when VG_CHECK is compiled out;
op-buffer payloads must be copied out before vg (alignment, wasm);
never bgfx::renderFrame() in a process whose 3D renderer owns the
device -- share via init()'s clean refusal; bgfx auto-select under a
virtual X server lands on Mesa swrast and crashes, so the offscreen
path defaults to Vulkan on Linux (FC_PAGE2D_RENDERER overrides).

**M2 remainder**: per-view damage hooks (feed on geometry-changed
signals instead of per-paint while pending), hatches, cosmetic
edges/centerlines, dimensions/annotations/balloons, templates, then
the real compositor (GL-context sharing instead of readback+QImage).
M3 (the SceneServer wire) untouched.

## 19. Implementation status (2026-08-24, later still): damage hooks, formats, hatches

Three items of the M2 remainder landed and are verified on the real
GPU (RTX 3060 via VirtualGL egl0 + Xvfb; NVIDIA banner checked in the
run log).

**Per-view damage hooks** (`QGVPage`). The preview no longer re-feeds
the page per paint while views compute. Each tracked `DrawViewPart`'s
`signalGuiPaint` -- fired on the GUI thread when HLR lands, when faces
land, and on repaint-worthy property changes (hatch and cosmetic edits
included: their view providers call `requestPaint()` on the parent
view) -- marks exactly that view dirty; the next paint feeds only the
dirty views. Two cases the signal does not cover: X/Y moves purge
their touch on the App side and signal nothing, so the track caches
the fed position and a paint-time compare catches them; a change in
the view set itself (add / remove / reorder, detected by an ordered
hash of the view names) rebuilds the tracked set and resets the
retained page wholesale -- the rare case, and the only correct answer
for deletion, whose items no re-feed would ever visit.

**Edge formats and view styling** (`PageFeed`). The feed now resolves
per-edge appearance the way `QGIViewPart::drawAllEdges` does: cosmetic
edge and centerline `LineFormat`s, `GeomFormat` overrides, the
hidden-line pen and `HiddenWidth`, the iso-line width, the
`showThisEdge` class-visibility matrix, `ShowAllEdges`, and the view
provider's `LineWidth` / `FaceColor` / `FaceTransparency`. Vertex dots
and arc center-mark crosses follow `drawAllVertexes` (LineWidth x
VertexScale sizing, CoarseView / frame gates, `ArcCenterMarks`). Dash
patterns come from the same `LineGenerator` pens the Qt tier
constructs, converted to page-unit run lengths and walked over the
flattened curve by the feed, because vg has no dashing of its own.

**Geometric hatches** (`PageFeed`). A face claimed by a
`DrawGeomHatch` emits its `getTrimmedLines()` line sets as one
`Decoration` item per face (drawn between fills and edges by Kind
order), colored and weighted from `ViewProviderGeomHatch`. PAT dash
specifications draw solid for now; SVG/bitmap `DrawHatch` fills are
not represented yet and leave the plain face fill.

**A Qt-tier defect this exposed, then fixed**: the Qt page drew
every edge solid. `QGIPrimPath::setTools()` overwrites the pen style
with `m_styleCurrent` right before painting, and nothing set
`m_styleCurrent` for part-view edges (`setHiddenEdge` has no caller),
so every dashed pen `LineGenerator` builds -- hidden lines, cosmetic
styles, ISO patterns -- was silently discarded. Fixed by making
`setLinePen` adopt the pen's style into `m_styleCurrent` (the
`setStyle` in `setTools` is then a same-value no-op, which Qt
guarantees preserves the pen's custom dash pattern) in `QGIEdge` and
the three decorations that take `LineGenerator` pens
(`QGICenterLine`, `QGISectionLine`, `QGIHighlight`); the latter two
composites also now hand the pen itself to their `QGIEdge` sub-item,
which previously received only a style enum and so could never carry
a custom pattern. `setStyle()`-only callers still override, since the
style forward stays after the pen forward. Verified: the Qt tier now
draws the test's dashed cosmetic line identically to vg.

**Verification** (scratchpad td_vgtest3.py pattern): a page with two
views of a holed box, a geometric hatch, a dashed cosmetic line and
hidden lines, driven under xvfb+VirtualGL. Offscreen render probes
hatch/cosmetic/edge ink and dash gaps; the interactive tier is
compared pure-Qt vs pure-vg (same viewport, items hidden for the vg
grab) -- ink counts within 40% and geometry bboxes within 12px, the
label rows excluded (annotations are not fed yet); damage is exercised
live by a model edit (hatch ink changes), an X move (position compare
path), and a view deletion (no stale ink). 14/14 checks pass.

WARNING -- test-harness trap that cost half a session: PySide6's
`QGraphicsScene.items()` / `childItems()` return wrappers for
C++-owned `QGraphicsItem`s (not QObjects), and when those temporary
wrappers are garbage-collected PySide deletes the C++ items -- the
whole Qt item tier silently vanishes from the scene. Any test script
touching scene items must keep every returned list referenced for the
process lifetime.

**M2 remainder now**: dimensions/annotations/balloons, templates, then
the real compositor (GL-context sharing instead of readback+QImage).
M3 (the SceneServer wire) untouched.

## 20. Implementation status (2026-08-24, later still): the annotation tier

Dimensions, balloons, annotations -- and with them leaders, weld
symbols, and every other view whose drawing is Qt-side layout -- now
reach the vg page. Verified 19/19 on the real GPU (RTX 3060 via
VirtualGL egl0 + Xvfb, NVIDIA banner checked).

**The design call: capture, not port.** `QGIViewDimension` alone is
2700 lines of ISO/ASME placement math, and the Qt tier remains the
interaction owner either way; porting it would have created a second
layout implementation that drifts. Instead `PageFeed::feedViewCapture`
converts the *already laid-out* QGraphicsItem subtree of a non-part
view into one `Annotation` item: path items become path ops, text
documents become per-line, per-fragment fontstash text runs. The Qt
tier stays the single source of layout truth; the vg tier renders its
result. Consequences worth knowing:

- `QGIPrimPath` in this fork derives from plain `QGraphicsItem` and
  draws from its own pen/brush members; new `currentPen()` /
  `currentBrush()` accessors (setTools() applied) expose what paint()
  would use. The dash-pattern preservation fix of `544320bbdf` is what
  makes the captured pens carry their patterns.
- Text: Qt's `pixelSize` sets the em square, fontstash (stb_truetype)
  scales so ascent-descent equals the size -- the capture hands vg the
  Qt line height (`QFontMetricsF` ascent+descent), or glyphs come out
  ~30% small. Rich text (annotations are HTML) keeps per-fragment
  fonts and colors; a fragment format only carries what CSS set, so it
  is merged over the item font. Rotated labels (ISO vertical
  dimensions, `Rotation` on annotations) wrap their runs in the new
  PushTransform/PopTransform ops.
- Fonts: the four shipped TechDraw TTFs register with the engine under
  family keys ("osifont", "y14.5-2009", "y14.5-freecad", an italic
  variant); any other family falls back to osifont. Vg2D retains font
  bytes for the process life, so registration is legal before any GPU
  context exists and survives context rebuilds.
- Cosmetic width-0 pens (balloon leaders and bubbles arrive this way)
  have no retained-page analog of "one device pixel at any zoom"; they
  map to the ISO 0.35mm line.
- Visibility is judged relative to the captured root
  (`isVisibleTo`), never absolutely: a host that hides the Qt items to
  photograph the vg layer alone must not read "empty drawing". The
  flip side stands as a design note for M3: a *hidden* Qt item stops
  laying itself out, so the pure-vg future needs the Qt tier laid out
  but not painted, not deleted.
- Part views additionally capture their `QGIDecoration` children
  (section lines, detail highlights, view center lines) as one
  Decoration item -- Qt-side drawings with no App-side geometry that
  the App-data feed could never see.

**Damage**: the QGVPage preview tracks every `DrawView` through the
same signalGuiPaint + fed-position-compare scheme that covered parts;
a dirty annotation view re-captures on the next paint, by which time
the Qt tier has redrawn it.

**The never-shown page trap** (offscreen host): a dimension's QGI is
created the instant `page.addView()` fires -- *before* its references
are assigned -- so `findParent` fails and the item sits unparented at
the scene origin, drawing the whole dimension in the page's bottom
left. The shown path repairs this in `fixOrphans` -> `setViewParents`;
`renderPageVg` now populates missing items *and* calls
`setViewParents()` after. The test asserts dimension ink at the
measured view's position to pin this.

**Diagnosis knob**: `FC_PAGE2D_TRACE=1` logs every item the capture
walker visits (type, scene rect, parent, bytes so far) and every text
run (string, size, anchor, font key).

**Not fed yet**: SVG-sourced content -- templates, `DrawViewSymbol`,
`DrawViewSpreadsheet` (its cells are rects+lines+text generated as an
SVG string App-side; the plan is native ops from the cell model, or it
rides the template/image story), `DrawViewImage`, SVG/bitmap `DrawHatch`
fills. Part-view frames/captions/labels are deliberately Qt-only.

**Verification** (scratchpad td_vgtest4.py): a box view + red
annotation + green balloon + blue dimension. Offscreen (page never
shown): ink of each color at its expected page position, dimension ink
at the *view* (the unparented regression), rotation grows the red
bbox. Interactive: pure-Qt vs pure-vg grabs, red/blue/green centroids
within 15px (blue compared inside the view region -- the Qt tier draws
template title-block fields blue). Damage: label move via the position
compare, balloon text edit via the signal (pixel-diff proof), view
deletion via the structure rebuild. Edits run with Qt items visible --
they are the layout source -- then items hide for the vg-only grab.

**M2 remainder now**: templates (the SVG story, likely an image op +
band-scale rasterization -- which would also cover symbols,
spreadsheets, images and bitmap hatches), then the real compositor
(GL-context sharing instead of readback+QImage). M3 (the SceneServer
wire) untouched.

## 21. Implementation status (2026-08-24, later still): the template and image tier

Templates, `DrawViewSymbol`, `DrawViewImage` and spreadsheet views now
reach the vg page as rasterized images. Verified 20/20 on the real GPU
(RTX 3060 via VirtualGL egl0 + Xvfb, NVIDIA banner checked): offscreen
ink probes for the template frame, title block, a red SVG symbol and a
green view image at their page positions; interactive pure-Qt vs
pure-vg centroids within 15px and title-block ink within 40 percent;
template damage (editable-text edit) proven by pixel diff; a zoom-band
crossing re-rasterizes without incident.

**The engine side.** `Page2D` gains an image registry:
`setImage(id, w, h, rgba, repeat)` copies and retains straight-alpha
RGBA8 pixels -- like fonts, registration is legal before any GPU
context exists, and the pixels follow every context rebuild. A new
`Image` op (id + page rect, 24 payload bytes) replays as
`vg::createImagePattern` + rect fill; vg's image patterns are
frame-transient and both the direct and the cached command-list replay
recreate them with local-handle remapping, so pattern fills cache like
everything else. The one wrinkle is that a recorded command list bakes
the `vg::ImageHandle` value: the registry keeps an *image epoch*,
bumped whenever a handle is created or destroyed, and items whose ops
referenced an image (resolved or not) re-record when the epoch moved.
Same-size pixel damage takes `vg::updateImage` in place -- no epoch
move, recorded lists stay valid. Uploads are counted
(`Counters::imageUploads`, exposed by `renderPageVg`). Vg2D's context
now allows 256 images and 256 per-frame image patterns.

**The feed side.** `PageFeed::feedTemplate` rasterizes the page's
`DrawSVGTemplate` (`processTemplate()`, the same processed SVG string
the Qt tier loads) with `QSvgRenderer` into the registry and records a
single sheet-rect image item on layer 0; views now feed on layers >= 1.
The raster scale is the zoom band, so the sheet re-sharpens as the
user zooms; the interactive host folds band + template name +
editable-text values into a stamp and re-feeds on change, the
offscreen host rasterizes at its fit zoom. The capture walker gained
`QGraphicsSvgItem` and `QGraphicsPixmapItem` branches: symbols,
spreadsheet views and view images rasterize at their on-page size
(capped 2048) into per-view image ids ('i'/'j' tags, stale tail
purged on re-capture) and record under the item's scene transform, so
rotation and scale ride the transform ops.

**The Qt crash found on the way.** Rasterizing the *stock TechDraw
templates* above roughly 3x their SVG default size segfaults inside
Qt's raster engine: FreeType returns `Raster_Overflow` for a glyph
("render glyph failed err=62") and `QFontEngineFT::loadGlyph` then
dereferences the failed glyph. Reproduced standalone with nothing but
`QSvgRenderer` + `QImage` at Qt 6.10.1; the trigger is the templates'
Inkscape `line-height:0%` text constructs, and the exact threshold is
glyph-cache-state dependent (removing one unrelated `<text>` moves
it). The feed therefore never asks QSvgRenderer for more than 2x the
SVG's own default size (template and captured SVGs alike) -- beyond
that zoom the template goes gently soft instead of the process dying.
The Qt tier itself shares this hazard in principle
(`QGraphicsSvgItem` at deep zoom); not addressed here.

**Not fed yet**: SVG/bitmap `DrawHatch` face fills (the registry and
the repeat-sampling flag are ready for them; the face path fill with
an image pattern is the missing feed code). Frames/captions stay
Qt-only. The spreadsheet view rides the capture's SVG branch (its
QGI is an SVG item); native cell ops remain a possible later upgrade.

**M2 remainder now**: the real compositor (GL-context sharing instead
of readback+QImage). Then M3, the SceneServer wire -- for which image
items already retain their pixels, so the wire story is bytes we
already hold.

## 22. Decision (2026-08-24): vectorizing shaded 3D views -- hybrid first

Today a shaded/rendered 3D view lands on a page as a raster screenshot
(ActiveView -> `DrawViewImage`). The orthographic part views are
already vector -- OCCT exact HLR emits real curves -- so this concerns
only the shaded-picture path. Prior art, for the record:

- SolidWorks / Inventor / NX: wireframe and hidden-line drawing views
  are vector (exact or tessellated HLR); *shaded* views are raster
  embeds at a configurable DPI, normally **hybrid** -- raster shading
  underlay with the vector HLR edges drawn on top, so prints and PDF
  keep crisp edges over the shading.
- AutoCAD: plots viewports vector for wireframe/hidden visual styles,
  raster the moment shading/rendering is involved.
- Full-vector shading (each visible face region a filled polygon via
  2D booleans of the projected faces in depth order, or PDF Gouraud
  triangle meshes) exists in export pipelines but is rare in
  mainstream CAD.

**User decision (2026-08-24): do the hybrid first; the full-vector
flat-shaded approach stays a future enhancement.**

The hybrid, sketched: one view object that pairs (a) a shaded raster
of the model at the view's *orthographic* camera -- the underlay,
which on the vg page is exactly the Page2D image op shipped in section
21, and on the Qt tier an image item under the edge items -- with (b)
the exact-HLR vector edge overlay a `DrawViewPart` already computes
for that same projection. Registration is the whole game: the shaded
capture must be rendered with the same orthographic camera, scale and
crop as the HLR projection, not grabbed from an interactive viewport;
the bgfx renderer's offscreen path can produce that deterministically.
Resolution rides the zoom band like the template raster.

The future enhancement, deliberately deferred: full-vector flat-shaded
views -- project the visible faces, subtract in depth order (painter's
algorithm) with 2D booleans, fill each surviving region with its
face's shaded flat color. The standing rule that planar 2D booleans go
through the libarea/Clipper stack makes this fork unusually well
placed for it; PDF Gouraud shading could later refine flat color into
smooth gradients on export.

**Sequencing:** this arc queues *after* the current order finishes
(the real compositor, then the M3 SceneServer wire).

## 23. Implementation status (2026-08-24, later still): the real compositor

The readback+QImage preview is retired from the interactive path: the
vg page now renders into a persistent texture on the shared bgfx
device and a textured blit composites it into the QGVPage viewport --
no per-frame GPU->CPU->GPU round trip. Verified 16/16 (plus the
previous suite's 20/20 re-run through it) on the RTX 3060.

**How it composites.** The 3D renderer already solved this problem:
its bgfx GL device lives on a Qt-created QOpenGLContext share-grouped
with `QOpenGLContext::globalShareContext()`, so its textures are
visible to every Qt GL widget, and each frame does a context dance
(widget out, device in for `bgfx::frame()`, widget back) because the
single-threaded GL device executes its frame in whichever context is
current. The page compositor rides exactly that arrangement:

- `RendererLib` grows three hooks -- `deviceSharesQtGL()` /
  `deviceMakeCurrent()` / `deviceDoneCurrent()` -- surfaced as
  `RendererFactory` statics; the bgfx lib answers from `_BGFXLib`.
  `deviceSharesQtGL` requires the *running* backend to be OpenGL
  (`bgfx::getRendererType()`), not just the lib's `currentType`: a
  lost init race against an already-up headless Vulkan device leaves
  `currentType` stale, and trusting it would hand a Vulkan handle to a
  GL blitter.
- `Page2D::renderToTexture(w, h)` renders into a page-owned
  color+depth target (recreated on resize, forgotten on device
  generation change), clears it transparent, pumps the device frame
  under the device's context, and returns
  `bgfx::getInternal(color)` -- the native GL texture id. vg's
  src-alpha blend over a transparent clear accumulates premultiplied
  alpha, so the host composites with (ONE, ONE_MINUS_SRC_ALPHA).
- `QGVPage::drawVgPreview` switches its viewport to QOpenGLWidget
  (queued -- the decision is taken inside the old viewport's paint),
  warms the device if nothing did (`RendererFactory::warmup("bgfx -
  OpenGL")` -- in practice the application startup warmup has already
  paid this), and inside `beginNativePainting` blits the page texture
  with a `QOpenGLTextureBlitter` under the scene items. Preference
  `PageRendererVgComposite` (default on) gates it; every missing
  precondition falls back to the readback path -- except a failed
  frame on a shared GL device, which skips the frame rather than pump
  `renderOffscreen`'s `bgfx::frame()` into the widget's context.
- `renderOffscreen` itself now runs its frames under the device
  context when the device is the shared-GL one, which is what a GUI
  process has after startup warmup -- previously its frames executed
  against whatever context happened to be current.

**Portability note (the wasm tier).** The compositor is deliberately a
thin desktop-only wrapper: `render(viewId, w, h)` remains the whole
portable engine (pure bgfx + vg, no Qt), which is what the
standalone/wasm viewer will drive directly on a backbuffer view in M3
-- there is nothing to composite there, and the device hooks compile
to `return false` under FC_RENDERER_STANDALONE.

**Test notes** (td_vgtest6.py, and td_vgtest5.py updated): a
QOpenGLWidget viewport defeats both `QWidget::grab()` (renders via
QWidget::render, missing GL content) and PySide's wrapper (the
viewport surfaces as bare QWidget; shiboken wrapInstance returns the
existing wrapper, so grabFramebuffer stays unreachable) -- grabs read
the real window pixels via `QScreen::grabWindow` and crop the
viewport.

**M2 is now complete.** Remaining before M3: nothing engine-side; the
wire (serialize Page2D items + images + fonts over SceneServer to the
wasm viewer) is next.

## 24. Design (2026-08-24): M3, the wire -- a page as a streamable document

The goal restated from section 16: a drawing page becomes a streamable
document like the 3D scene -- the desktop (or a headless server)
publishes the page's retained Page2D content over SceneStreamServer,
and the wasm viewer renders it with the same vg code on the essl
backend. The survey that produced this design covered three seams: the
server's delta machinery, the wasm viewer's payload path, and the
TechDraw damage cadence. The findings dictate almost everything.

### 24.1 The transport is reused whole, because the splice parses nothing

`spliceObjectDelta` (SceneDump.cpp) -- the function the server uses to
catch up a viewer that is behind -- makes exactly five assumptions
about a payload: a u64 baseVersion field at `spans.baseVersionAt`
(before the list), the object-list section occupying exactly
`[listBegin, listEnd)` in `writeObjectSection`'s encoding, everything
outside those regions independent of the base version (it is copied
verbatim), entries keyed so the server's blob store can answer them
inline (v37), and `baseVersion != 0`. It never reads the magic, the
version, or any other byte of the payload.

So the page payload is its own format under its own magic (`FCPD`,
version 1), and the server -- publish, per-viewer catch-up, blob
store, history window, grants, document groups -- is reused with zero
changes. The cost of admission is that the page's item list must use
the exact scene entry codec. To keep that from drifting, the codec is
exported from SceneDump.cpp as a `writeObjectSection` /
`readObjectSection` pair (the writer wraps the internal template the
scene root already uses; the reader is new, written next to it in the
same file) instead of being duplicated in the page serializer.

### 24.2 The payload

All little-endian, `str` = u32 length + bytes, mirroring SceneDump:

```
u32  magic 'FCPD'          u32  version (1)
u64  manifestVersion
u64  baseVersion           <- spans.baseVersionAt
u64  sessionId
f32  pageWidth, pageHeight (Rez units)
fonts:  u32 n x [ str name, str blobKey, u32 size ]
images: u32 n x [ u64 imageId, u16 w, u16 h, u8 repeat,
                  str blobKey, u32 size ]
items:                     <- spans.listBegin
    the writeObjectSection encoding: removed ids, then entries
    [ u64 itemId, f32 bbox[6], four empty info strings, u8 incomplete,
      group ref (str chunkKey + u32 size),
      delta form only: u8 flag + optional inline chunk bytes ]
                           <- spans.listEnd  (nothing follows)
```

The font and image tables ride before the list, so a spliced delta
carries the *latest* tables verbatim -- the same rule as the 3D root's
inline volatile state, and correct for the same reason: they are
idempotent to re-apply, and small (a page has four fonts and a handful
of rasters). Everything bulky is behind content keys.

Three chunk kinds, all content-addressed (sha1, like every blob):

- **Item chunk**: u8 kind, u32 layer, then the item's op buffer
  verbatim -- the position-independent page-coordinate representation
  M1 built is the wire format, as designed. Kind or layer changes
  re-key the chunk, which is correct: the entry is the delta unit.
- **Image chunk**: the raw RGBA8 pixels (dimensions ride in the root's
  image table). Identical re-rasters dedup by content.
- **Font chunk**: the font file bytes. Vg2D already retains them.

The entry's `info` strings stay empty in v1 (the picker, when it
comes, will want the owning view's identity there); `bbox` carries the
item's page-space bounds with z = 0, computed conservatively from the
op buffer's coordinates -- what lets a viewer cull or prioritize items
it has not fetched. The entry key IS the chunk key: that identity is
what makes the server's inline-delta catch-up (v37) work unchanged.

### 24.3 The publisher

A `PageServeSource` in TechDrawGui, mirroring Gui's SceneServeSource,
exposed as `TechDrawGui.servePage(page, port=0)`. Its document group
is `<document>:<page>` (label: the page's Label; ':' because the
name rides a `?doc=` query where '#' would start a URL fragment), so a 3D serve of the
same document and any number of served pages coexist -- each group has
one publisher claim, and the survey confirmed a shared group would
lock the second claimant out.

- **Feed**: it owns a headless Page2D and feeds it the renderPageVg
  way -- populate the ViewProviderPage's QGSPage without any widget
  (addChildrenToPage / redrawAllViews / setViewParents) so the
  annotation tier has laid-out Qt items to capture, then
  PageFeed::feedPage. The template rasterizes at a fixed scale 2.0
  (the interactive path's 2x-of-default cap; a viewer-driven re-raster
  is future work, noted in 24.6).
- **Damage**: per-view `DrawView::signalGuiPaint` marks the view
  dirty; `DrawPage::signalGuiPaint` plus a structure hash covers
  add/remove; the X/Y compare and the template stamp mirror
  QGVPage::drawVgPreview. All of it coalesces into a single-shot
  zero-interval timer -- crucially, the queued hop also orders the
  re-feed AFTER ViewProviderDrawingView::onGuiRepaint has refreshed
  the Qt layout the capture tier photographs.
- **Publish**: re-feed dirty views into the Page2D, take its change
  journal (new: Page2D::takeChanges -- setItem/removeItem/setImage/
  removeImage record ids since the last take, so only damaged items
  are re-hashed), rebuild changed entries, `diffObjectLists` against
  the previous publish's list, retain-or-publish every named chunk
  (the chunkBlobs discipline: `retainBlob || publishBlob`, inside the
  serialization pass, every publish, every key -- a missed retain
  404s two publishes later), then `beginPublish`/`publish`. A deleted
  view's items arrive as explicit removals via the per-kind
  contiguous-id sweep the feed already guarantees.

### 24.4 The viewer

The wasm viewer routes on magic at the single funnel every transport
passes through (applyScenePayload, right after the u64 stream-version
prefix): `FCSD` parses as today, `FCPD` enters the page path.

- **Store**: a viewer-side Page2D plus the id -> key tables from the
  root. A full root (baseVersion 0) lists every item: ids the viewer
  holds that the list does not name are removed -- same contract as
  the 3D object model. Chunks resolve through the existing kind-blind
  blob machinery (requestBlob -> IndexedDB -> GET /blob or the POST
  /blobs batch) untouched; the page drain hooks in beside
  resolvePending, and sweepStore learns the page's live keys so the
  budget sweep does not collect them. Inline v37 delta bytes ingest
  directly, store included. Fonts land through a new byte-based
  Page2D::registerFont overload (Vg2D::loadFont already takes bytes;
  re-registering a name is already a no-op). Applying an item takes a
  new setItem overload accepting the raw op buffer (replay is already
  bounds-checked; a truncated buffer draws what fits).
- **Render**: a page-mode branch in mainLoop -- skip the 3D
  renderer's frame, set view rect + clear on a fixed id in the top
  view-id granule the renderer already reserves for Page2D, call
  page->render(viewId, w, h), then bgfx::frame(). The one wrinkle the
  survey found: the bgfx device in this tier comes up inside
  BGFXRenderer's first render, so page mode keeps routing through the
  3D render until the device exists, then branches. Pan/zoom/touch
  handlers get an early page-mode branch driving Page2D::View (fit on
  first payload: zoom = min(w/sheetW, h/sheetH), panY = zoom*sheetH,
  the renderPageVg framing); devicePixelRatio rides s_dpr.
- **Switch/reset**: the sessionId-change seam in applyScenePayload is
  the one place that knows a document switch happened; it resets the
  page store exactly as it resets the object model, and payload
  arrival decides the mode (a scene payload leaves page mode, a page
  payload enters it).
- **Version skew**: sceneSnapshotVersion answers 0 for a foreign
  magic, so the reload probe cannot see a newer page format. The page
  branch carries its own probe (page version > built -> reload with
  its own bust string), and the hello grows a "page" field so the
  backend's mismatch check covers both formats.

### 24.5 Build

vg-renderer enters the wasm viewer as a local target in
wasm/CMakeLists.txt (the 3rdParty block is Qt-shaped and cannot be
included): the 11 C/C++ sources, PUBLIC include, DXBC/DXIL=0 defines,
linked with bx/bgfx -- its shaders are committed embedded C arrays
carrying essl, so the WebGL backend needs no shaderc work at all.
Page2D.cpp and Vg2D.cpp join the fcviewer sources. This also repairs
the wasm build, which commit c857d4dda3 broke: BGFXRenderer.cpp now
includes Vg2D.h and calls Vg2D::shutdown() unconditionally, and the
wasm target has neither the header path nor the source.

### 24.6 Open questions carried forward

- Template sharpness: the served raster is fixed at scale 2.0; a
  viewer zooming past it gets softness. The fix -- a control op asking
  the publisher to re-rasterize at a named band, answered by the next
  publish re-keying the image chunk -- fits the protocol as designed,
  and simply is not v1.
- Interaction: picks and semantic ops on page items (select a
  dimension, edit its value) are the thin-client vocabulary of
  docs/ThinClient.md applied to 2D; the entry info strings are
  reserved for exactly that.
- A page served without any Gui document (FreeCADCmd) still needs the
  QGSPage population step for annotations, which needs the Gui layer;
  the geometry tier alone would serve. Same boundary the 3D headless
  serve has (docs/ComputeBoundaries.md).

## 25. Implementation status (2026-08-24, later still): M3 is built -- a page streams to the browser

The wire of section 24 is implemented end to end and verified: a
TechDraw page served from a desktop (or Xvfb) FreeCAD renders live in
the wasm viewer over the scene stream, and an edit arrives as a delta.

**What landed, by commit:**

- `f081c893b8` -- the FCPD payload. SceneDump exports the object-
  section codec (`writeObjectSection`/`readObjectSection`, kept beside
  the splice in the same file so the bytes cannot drift), and
  Page2DWire rides it. Page2D grew the wire entry points: the damage
  journal (`takeChanges`), raw-op `setItem`, item/image enumeration,
  byte-based fonts, and `opsBounds`. Round-trip verified including the
  critical case: a delta produced by the server's own
  `spliceObjectDelta` on a page payload parses back with removals,
  changed entries and inline chunk bytes.
- `c08fa4e4f2` -- the publisher. `TechDrawGui.servePage(page, port)`
  creates a PageServe: its own document group `<doc>:<page>` (`:`
  because `#` starts a URL fragment in the `?doc=` query -- and note
  the server does not URL-decode query values, so the name must be
  sent unencoded), a headless Page2D fed the renderPageVg way and
  damaged the drawVgPreview way, publishes coalesced through a queued
  timer. Verified in Xvfb 18/18: full root, every blob answering its
  sha1 key, 204 while unchanged, a view move as a partial delta with
  inline bytes, a view delete as an explicit removal.
- `d49c59e5ce` -- the viewer. FCPD routes at the single payload
  funnel; page mode drives `Page2D::render` on a backbuffer view id in
  the reserved top granule, with pan/wheel-about-cursor/pinch, a
  viewer-drawn paper sheet (id ~0, below the template by insertion
  order), double-click refit, session-change reset at the same seam as
  the 3D model, page live keys in the store sweep, and a "page" field
  in the hello. vg-renderer joins the wasm build as a local target
  (embedded essl shaders; no shaderc work) -- also repairing the
  viewer build the compositor commit had broken.

**End-to-end (headless Chrome against the Xvfb publisher):** the
browser applies `page v1, 13 items named`, fetches 16 chunks through
the batch endpoint, and the canvas shows the sheet -- ink counts
within 3% of the desktop `renderPageVg` reference. After `view.X += 60`
on the publisher, the browser applies v2 and the view's ink moved by
exactly the predicted 222 px (600 Rez x 0.370 zoom) while the
template ink stayed put, pixel-diff confirmed.

**Traps for the next session:**

- Chrome `--headless=new --screenshot --timeout=N` shoots at the page
  load event, long before any scene arrives -- every such shot shows
  the empty viewer. `--virtual-time-budget=30000` is what lets the
  fetch, the WS payload and the apply all run before the shot.
- An emcmake configure under the conda env inherits `-march=nocona`
  in CFLAGS/CXXFLAGS and breaks the wasm cross-compile; clear them
  (the env is still needed for its python >= 3.10, which emscripten
  requires). The vite inspector bundle needs the address-space cap
  raised (node wasm allocation vs limited.sh's 24GB RLIMIT_AS).
- `urllib.parse.quote` percent-encodes `:`; the server compares query
  values literally. Send group names raw.

**Open, deliberately (24.6):** template sharpness is fixed at serve
scale 2.0; page picking/ops (the entry info strings are reserved);
in-browser verification of the delta-specific viewer branches (base-
mismatch resync, removals) rides the shared apply path and the wire
smoke, not a dedicated browser test.

## 26. Design (2026-08-24): the shaded-view hybrid -- raster underlay under exact-HLR edges

Section 22 recorded the decision: a shaded drawing view is a hybrid,
the way SolidWorks/Inventor/NX plot one -- a shaded raster underlay
with the vector exact-HLR edges drawn crisp on top. This section is
the working design for building it.

### 26.1 Object model: properties on DrawViewPart, not a new type

The hybrid is not a new view type; it is an appearance of the view
`DrawViewPart` already is. The HLR edge overlay it needs is exactly
what the view computes today, for the same Source, Direction,
XDirection, Scale and Rotation. So the underlay hangs off
`DrawViewPart` itself, and section views and detail views inherit the
capability for free later (v1 targets plain part views; sections cut
their source, so their underlay needs the cut solid and stays off
until wired):

- `Shaded` (`App::PropertyBool`, default false) -- turn the underlay
  on.
- `UnderlayImage` (`App::PropertyFileIncluded`, `Prop_Output`) -- the
  captured PNG, persisted in the document the way
  `DrawViewImage::ImageIncluded` is. `Prop_Output` because the
  capture is *derived* state written back by the Gui tier: writing it
  must not re-touch the view, or every capture would schedule the HLR
  it was triggered by.
- `UnderlayResolution` (`App::PropertyFloat`, px per page mm, default
  10.0 -- about 254 dpi on paper) -- capture density.
- `UnderlayRect` (`App::PropertyFloatList`, `Prop_Output`, hidden) --
  registration: `[x, y, w, h]` of the raster in the view's own 2D
  coordinate system (mm, centroid-origin, +Y up, pre-invertY), written
  by the capture beside the pixels.

A restored document carries the last capture in the property, so the
page shows a shaded view immediately -- in FreeCADCmd-less viewers,
in a PageServe publisher before any recompute, and in the browser --
and a Gui session re-captures only when the projection actually
changes.

### 26.2 The capture: a private Coin scene, not a viewport grab

Section 22 sketched the capture on the bgfx offscreen path. First
recon amends that: `Renderer::renderOffscreen` renders "an ordinary
frame with the same feeds" -- the whole visible scene, plus the
selection, preselection and overlay feeds, under whatever AO/bloom/
temporal state the interactive view is in. Making that deterministic
for a drawing would need a per-capture object include-filter in the
Renderer API plus feed stripping, and it only exists at all when the
user runs render-cache mode 3 with a backend selected. The underlay
must not depend on any of that.

So the first build captures from a **private Coin scene**:

    SoSeparator
      SoOrthographicCamera   (from getProjectionCS + Rotation)
      SoDirectionalLight     (headlight, camera-aligned)
      <ViewProvider roots of the resolved Source objects>

rendered by `SoFCOffscreenRenderer` at `UnderlayResolution`, RGBA
with a fully transparent background, PNG'd into `UnderlayImage`.
Nothing else can leak in: no selection highlight, no other objects,
no navigation chrome, no temporal state. It works in every GUI build,
with or without the bgfx renderer. The seam is one function --
`captureShadedUnderlay(DrawViewPart*) -> QImage + rect` -- and the
bgfx-quality capture (PBR materials, matcap, AO baked into the
underlay) remains the recorded follow-up behind the same seam, which
is when the per-capture object filter earns its place in the
Renderer API.

### 26.3 Registration, the whole game

`makeGeometryForShape` fixes the coordinate contract: the HLR input
is the source shape *centered on its centroid* in
`getProjectionCS()`, *scaled* by Scale, then *rotated* by Rotation
about the CS axis; the projected 2D geometry therefore lives in a
centroid-origin mm system aligned with the CS's XDirection/
YDirection. The capture mirrors each step exactly:

- Camera: orthographic, aimed along -Direction, right/up = the CS
  XDirection/YDirection rotated by Rotation about the axis (the exact
  mirror of `centerScaleRotate`'s `rotateShape`).
- Window: the bounding box, in those camera coordinates, of the
  centered *unscaled* source shape -- computed from the same source
  shape the HLR consumed, not from scene graph bounds. `UnderlayRect`
  is that window times Scale.
- Raster size: window extents times Scale times
  `UnderlayResolution`, so one raster pixel is a fixed fraction of a
  page mm regardless of model size.

The Qt item then draws the pixmap at `UnderlayRect` (invertY applied
like every other view child) with no further transform, and the HLR
edges land on the raster's silhouette boundary by construction. The
verification test asserts exactly that, in pixels, on the real GPU.
Perspective views (`Perspective`/`Focus`) are out of v1's scope: the
underlay stays off for them (a matching SoPerspectiveCamera is
possible later, but registration against perspective HLR needs its
own math and its own test).

### 26.4 Trigger and cadence

The capture belongs to the Gui tier (ViewProviderViewPart), runs when
the view's HLR geometry has rebuilt (the same signal QGIViewPart
redraws on -- after async HLR completes, never racing it), and is
guarded by a capture key: source objects' geometry stamp + Direction
+ XDirection + Scale + Rotation + resolution + the source colors'
hash. Same key, no capture, no property write -- a repaint must not
dirty the document or touch the disk. `Prop_Output` keeps the write
from re-touching the view when a capture does happen.

### 26.5 Display tiers

- **Qt page**: `QGIViewPart` gains a pixmap child (reusing
  `QGCustomImage`) at the lowest zValue of the view -- under faces,
  hatches and edges -- fed from `UnderlayImage` bytes at
  `UnderlayRect`.
- **vg page**: free by construction -- the feed's capture walker
  already rasterizes `QGraphicsPixmapItem` children into Page2D image
  ops in encounter order (section 21), and the underlay is just one
  more, layered below the view's stroke items.
- **The wire**: image items already stream as content-addressed RGBA
  chunks (section 24), so a served page carries the underlay with
  zero new wire code.

### 26.6 Milestones

- S1: the four properties on `DrawViewPart`, restore-compatible.
- S2: the capture helper + ViewProvider trigger.
- S3: the `QGIViewPart` underlay item; real-GPU registration test
  (silhouette-on-boundary within ~2 px, Scale and Rotation cases,
  restore-without-recapture case).
- S4: vg feed + served-page verification; implementation-status
  section.

## 27. Implementation status (2026-08-24, later still): the shaded-view hybrid is built

Section 26's design is implemented and verified on the real GPU; the
hybrid works on the Qt page, the vg page, and in the browser over the
M3 wire. Commits: `5363ca92d9` (properties), `d8b6a2da56` (capture),
`2a54a5d705` (Qt display), `db805002fa` (vg feed + wire), after
`d8529cb747` (the design) and `9bcf4aac72` (an incidental Part
portability fix found trying the 7.7.2 debug tree, which remains
unbuildable -- ShapeRefSet needs the 801 fork's BRepTools_ShapeSet
accessors).

What landed, by tier:

- **DrawViewPart** carries `Shaded`, `UnderlayImage`
  (PropertyFileIncluded, Prop_Output), `UnderlayResolution` (px per
  page mm, default 10) and `UnderlayRect` (view-2D mm, centroid
  origin, +Y up). mustExecute's whitelist means a Shaded toggle never
  re-runs HLR.
- **ShadedUnderlay** (TechDrawGui) renders the Source ViewProvider
  roots in a private Coin scene -- SoOrthographicCamera on
  `getRotatedCS`, window from geometry bounds around
  `getOriginalCentroid`, `SoFCSwitch::switchOverride` so 3D-hidden
  sources still capture, white-at-alpha-0 background -- through
  SoQtOffscreenRenderer, and writes back only when the picture
  really changed.
- **QGIViewPart** re-validates the capture on draw and shows the
  pixmap at UnderlayRect; **feedViewPart** emits it as a Kind::Face
  image item. Both tiers treat Shaded as "the shading is the fill":
  the plain face fill is skipped while the underlay is active
  (hatches still draw above it, by z / kind order). The wire needed
  zero changes -- the underlay is one more image chunk.

Verified (VirtualGL egl0 + Xvfb, NVIDIA banner checked; scripts
td_shaded1/2 in the session scratchpad): 19/19 Qt-tier checks --
rect/px math against known geometry, capture content and orientation
(green boss top-right), silhouette-boundary probes (the HLR edges lie
on the raster boundary within 6 px at 3 px/mm) for the plain, 30deg-
rotated and reopened page, no-rewrite idempotency across redraws,
Shaded-off sweep, save/reopen restore -- and 9/9 vg-tier checks
including qt/vg shade-region parity and the renderPageVg registration
probes. Headless Chrome against a PageServe publisher shows the
shaded view under the vector edges in the wasm viewer.

Traps burned in this build, for the record:

- `SoQtOffscreenRenderer`'s default internal texture format is a
  float FBO (`GL_RGB32F_ARB`) and reads back as full-frame dithered
  garbage on this stack (NVIDIA 3060 via VirtualGL EGL, and plain
  probes reproduce it standalone) -- ask for `GL_RGBA8`.
- Bounds and centroids that touch triangulation are not stable
  across a session: `BRepBndLib::Add(useTriangulation=true)` and
  `ShapeUtils::findCentroid` (AddOptimal on triangulation) both move
  once the 3D view tessellates. The capture uses geometry bounds and
  the HLR's own saved centroid. Note the corollary: **TechDraw's own
  projection drifts ~0.02mm between sessions** for the same reason
  (the HLR centroid is recomputed on open at whatever tessellation
  state exists), so "the same document produces the same page" is
  only true to that tolerance.
- GL renders of an unchanged scene are not byte-stable (channel
  jitter up to 16 counts on curved-face gradients between render
  paths); byte-equality is the wrong idempotency guard for any
  write-back-a-render scheme -- compare with a tolerance.
- Chrome's `--virtual-time-budget` only works for the wasm viewer
  when the profile has the wasm cached: on a cold profile virtual
  time expires inside the download and every shot is the loading
  screen. Prime the cache with one real-time run (`--timeout`), then
  shoot with virtual time on the same `--user-data-dir`.

Open, deliberately:

- Sections and details are guarded out of the capture (the underlay
  must render the cut/clipped shape); wiring them means feeding the
  section's cut solid through the same camera math.
- Perspective views stay unshaded (sec 26.3).
- The capture quality upgrade -- bgfx offscreen with a per-capture
  object filter for PBR/AO-grade underlays -- remains the recorded
  follow-up behind the `ShadedUnderlay::capture` seam.
- Face selection highlight sits under the underlay on the Qt tier
  (the fill is suppressed, but QGIFace's hover/select painting is
  too); if this bothers in practice, highlight-state faces should
  re-raise above the raster.
- Sources nested in transformed containers capture untransformed
  (ViewProvider root == world only for top-level objects).

## 28. Implementation status (2026-08-25): section 27's open list is closed

All five open items above are done, in commits `6575fbd643`
(sections/details), `61e103abe9` (face highlight), `6c74e12d6b`
(nested sources -- a test verdict, no code), `61727f1d1b`
(perspective), `30a0fd9bdd` (bgfx-quality capture). Every claim below
was verified on the real GPU (VirtualGL egl0 + Xvfb, NVIDIA banner
checked; rigs td_shaded3/5/6/7/8 in the session scratchpad beside the
original td_shaded1/2), and the ortho, section/detail and perspective
rigs pass together against the final tree.

- **Sections and details** (25 Qt-tier + 8 vg-tier checks): the
  capture resolves the DERIVED shape -- `getCutShapeRaw()` for a
  section, `getDetailShape()` for a detail -- and renders its
  triangulation as an indexed face set (Coin crease normals, the first
  source's ShapeColor), since the cut solid exists nowhere in the 3D
  scene. The camera math needed almost nothing: a section's
  `getRotatedCS()`/`getOriginalCentroid()` give the right frame by
  virtual dispatch; a detail mirrors detailExec (base view's
  projection CS, the anchor lifted to R3 as the 2D origin, its own
  Rotation). Aligned complex sections (centerShapeXY, no centroid
  chain) stay unshaded. Display side, the cut-surface base fill goes
  transparent while the underlay is active -- in a straight-on section
  the whole view IS the cut face, and the opaque fill was burying the
  raster; hatch lines still draw above it.
- **Face highlight** raises to FACE + 6 (above the raster at FACE + 5,
  below hatches) on pre-select/select and drops back on normal;
  the select of a face flips its region to the selection color above
  the raster and restores exactly on clear.
- **Nested-container sources REGISTER** -- the sec 27 worry is
  refuted by probes: a source nested in a placed App::Part (both HLR
  shape and VP root carry only the object's own placement), an
  App::Part source (both carry the container transform), and an
  App::Link source (both carry the link placement) all hold
  registration. The stale capture comment said otherwise and is gone.
- **Perspective views** (14 checks): the HLR's perspective projector
  is an eye at +Focus over the centroid plane projecting the SCALED
  shape (u = x*f/(f-z)), i.e. Focus/scale from the unscaled side. The
  capture mirrors it with an asymmetric SoFrustumCamera whose section
  at the centroid plane is exactly the registration rect; the rect
  comes from the projected corners of the view-frame bounds (central
  projection preserves convexity in front of the eye). A shape
  reaching the eye stays unshaded. Registration holds through Focus
  changes, Rotation, and perspective sections.
- **bgfx-quality capture** (18 checks): `Renderer::setCaptureFilter`
  restricts renderOffscreen to the named objects' draws (resolved
  through the resident objectInfo table) with selection/highlight/
  overlay feeds stripped, a flat white background, the default
  camera-aligned headlight, and settle frames for temporal
  convergence -- all swapped in and restored around the frame, with
  drawListVersion untouched so the mesh collector's keep-set stays
  the full scene's. ShadedUnderlay prefers it whenever a 3D view of
  the source document runs a backend
  (TechDraw/General/ShadedUnderlayBackend, default on) and the same
  camera volume hands over as matrices; refusals fall back to the
  Coin capture.

Traps burned in this round:

- A test section whose plane is PARALLEL to the base view's projection
  plane throws `gp_Dir zero norm` drawing the section line on the base
  view -- real sections cut perpendicular to the base view.
- The engine's finished frame carries no meaningful alpha:
  `QOpenGLFramebufferObject::toImage()` premultiplies and zeroed every
  color under alpha 0. Read the pixels directly and key the exact
  white clear -- and re-bind the capture FBO first, because the
  engine's frame leaves its own readback FBO on the GL READ binding.
- The viewer feeds the backend's lights for the INTERACTIVE camera;
  under the capture camera they point anywhere and the model rendered
  near-black. The default ViewLightConfig (fixed camera-aligned
  headlight) is the correct capture state.
- Render-cache mode 3 evicts a hidden object's caches; with a backend
  attached, `SoFCSwitch::switchOverride` renders BLANK for a 3D-hidden
  source (nothing behind the switch to draw). Hidden sources now
  tessellate into the Coin capture directly, with their own
  ShapeColor.
- The vg tier ignores per-view Visibility -- isolating one view by
  hiding the others works on the Qt scene only; on renderPageVg crop
  analysis windows around the views' page positions instead.

Still open, known and accepted: the derived-shape (section/detail)
capture shades uniformly with one source color (per-face fidelity for
CUT shapes would need the cut solid fed to the backend); aligned
complex sections stay unshaded; the vg tier still has no section-face
(hatch) items of its own.

## 29. Implementation status (2026-08-25): the vg-page parity gaps are closed

The "finish the vg renderer" order: the recorded parity gaps of the vg
page against the Qt page (secs 19, 21, 28) are closed. What landed, all
in `PageFeed.cpp` unless noted:

- **SVG/bitmap DrawHatch fills.** A hatched face rasterizes its tiled
  fill clipped to the face outline into the page's image registry
  (`emitHatchRaster`) and emits it as an image op inside the face's
  own 'f' item, over the plain fill -- QGIFace keeps its solid base
  fill (`m_fillDef = SolidPattern`) under every hatch mode, so the vg
  feed does too. SVG tiles mirror `buildSvgHatch`: 64x64-unit tiles at
  `HatchScale`, rotated about the face center, shifted by
  `HatchOffset`, recolored by replacing the pattern's
  `stroke:`/`stroke="` declaration of `#000000` with `HatchColor`.
  Bitmaps mirror `BitmapFill`: the pixmap pre-rotated, applied as a
  texture brush anchored at the view origin. The raster image shares
  the face item's id; the item sweep removes both.
- **PAT dash specs.** Geom hatch line sets walk their DashSpec like
  `PATPathMaker`: signed cells (mark >= 0 / space < 0, a zero dot one
  pen-width long -- with the Qt tier's double-Rez pen width kept
  bug-compatible), decoded at the pattern scale, phased from
  `getPatternStartPoint` with the offset/stub re-phasing logic.
- **The PAT y trap (real bug fixed).** `getTrimmedLines` geometry is in
  the y-UP projection space, unlike the stored (inverted) view
  geometry: PAT line y NEGATES onto the page, exactly as
  `PATPathMaker::dashedPPath` does. The old feed drew hatch lines at
  +y -- verified mirrored off an off-center face (td_vgtest7's
  two-box view; the earlier centered-face rigs could not see it).
- **Section cut-surface faces.** `DrawViewSection::getTDFaceGeometry`
  feeds as 's' (fill, `Kind::Face`) + 'S' (PAT lines, Decoration)
  items mirroring `QGIViewSection::drawSectionFace`: cut color fill
  under every CutSurfaceDisplay mode (alpha 0 while the shaded
  underlay is active), SvgHatch through the same rasterizer (section
  properties: `SvgIncluded`, `HatchScale/Rotation/Offset`, VP
  `HatchColor`), PatHatch through `getDrawableLines` + the dash
  walker, face-outline strokes per `showSectionEdges`. Z-order:
  Kind::Face items draw in feed sequence, so the view feeds underlay
  -> fills -> section faces and re-adds (remove-then-set) each feed to
  keep that sequence fresh -- ZVALUE::SECTIONFACE semantics without a
  new Kind.
- **Per-view Visibility.** A hidden view (`vp->isShow()` false) feeds
  nothing and sweeps everything it ever fed. QGVPage's paint-time
  compare (QGVPage.cpp/.h) now also tracks `fedVisible` next to
  fedX/fedY -- a Visibility toggle signals nothing, same as X/Y moves.
  The sec-28 trap ("crop by page position, never hide views") is
  retired.
- **Detail matting.** QGIMatting is a plain QGraphicsItemGroup, not a
  QGIDecoration -- the decoration capture missed it. It now captures
  into its own 'm' item at `Kind::Annotation` (ZVALUE::MATTING is
  above edges). Section lines and detail highlights were already
  captured (QGIDecoration children); verified, not assumed.

**Verified 31/31** on the RTX 3060 (VirtualGL egl0 + Xvfb, NVIDIA
banner checked), td_vgtest7.py in the session scratchpad: offscreen
renderPageVg probes (hatch ink, dash gap runs, per-face position
ground truth on off-center faces, section fills in all three modes,
matting ring circumference 36/36, hide/reshow), and Qt-vs-vg viewport
compares per color class (ink counts, centroids/bboxes within 15px)
through the GL compositor, plus interactive damage checks (Visibility
toggle both ways, hatch VP recolor).

Traps for the next session:
- GL-viewport grabs in tests: `QWidget::grab()` misses GL content and
  PySide wraps the viewport as a bare QWidget (no `grabFramebuffer`)
  -- use `QScreen::grabWindow(win.winId())` and crop the viewport
  rect (sec 23's trap; it bit again).
- Thin hatch strokes (hatch45R's ~1-unit lines) anti-alias to pale
  tints over a colored base fill -- color-probe rigs need
  `HatchScale >= 3` or tolerant predicates.
- The template's editable-text underlines (blue click affordances) are
  Qt-editing aids and are deliberately NOT fed; exclude the title
  block from color parity windows.
- The shipped FCPAT.pat has no dashed pattern -- rigs write their own
  (`*VGDASH` / `0, 0,0, 0,5, 5,-3`).

Still open, known and accepted: view frames and labels are Qt-only
(editing chrome); the derived-shape capture limitations of sec 28
stand.

## 30. Implementation status (2026-08-25): per-face colors for the derived-shape capture

The per-face section capture order: the section/detail shaded underlay
no longer shades the whole cut/clipped shape with the first source's
color. `ShadedUnderlay.cpp` grows a geometric face->color resolver
(`DerivedColorResolver`) and `buildMeshNode` grows a per-face-indexed
material palette (`SoMaterialBinding::PER_FACE_INDEXED`, one material
index per triangle, palette of unique colors).

**Mechanism -- geometry, not history.** The cut runs in a worker
through plain `BRepAlgoAPI_Cut` (no element map survives it), and the
recorded index mapping (piece i of the cut compound -> source solid i)
is broken by both `TrimAfterCut` (the second pass re-cuts the whole
compound, restructuring it) and per-solid cut failures (skipped
pieces shift the order). Instead, every face of the derived shape is
classified by one exact probe point -- the UV centroid of its first
triangle evaluated on the actual surface (mesh nodes sit a deflection
off curved faces, far beyond tolerance):

- probe ON a source shape's boundary (`BRepExtrema_DistShapeShape`
  boundary solution within tolerance): a surviving piece of a source
  face. Color = that source face's `DiffuseColor` entry when the VP
  carries a per-face list matching the face map, else the source's
  `ShapeColor`.
- probe strictly INSIDE a source solid: a face born of the cut.
  Color = the section VP's `CutSurfaceColor` when `CutSurfaceDisplay`
  is "Color" (matching what the unshaded raster shows there), else
  the owning solid's body color (Hide and both hatch modes -- the
  hatch draws above the underlay).
- sources are the BaseView chain's (the cut input is the base view's
  source shape, in the global frame all the way down a section
  chain); per object, `Part::Feature::getShape` in the same frame the
  HLR input was built from. 2D sources are skipped (no solids).

**The OCCT trap that cost the first run:** `BRepExtrema_DistShapeShape`
reports a point inside a solid as distance ZERO -- an "inner
solution", not a boundary hit. Without checking `InnerSolution()`,
every cut-born face classifies as on-boundary and takes the owner
color; the cut color never appears. The resolver treats an inner
solution as the born-of-cut signal directly (no separate
`BRepClass3d_SolidClassifier` pass needed).

Frames: a section's `getCutShapeRaw` is saved before prepareShape's
move/scale/rotate, so probes map to the sources identity. A detail's
`getDetailShape` lives in the base view's rotated-then-centered frame;
the resolver inverts that (unrotate by the base `Rotation` about the
projection CS axis, uncenter by the re-derived bbox centroid -- the
unfused compound has the identical bbox as the fused input, so the
centroid matches without paying the fuse) with a loosened 0.1mm
tolerance for the known triangulation-state bbox drift; at worst a
face degrades to its owner's uniform color, never to a wrong owner's.
Skipped (uniform shade as before): a detail whose base is itself a
section or detail, a section with a detail in its BaseView chain, and
derived shapes beyond 500 faces (a distance query per face). The
hidden-source tessellation path reuses the resolver for per-face
`DiffuseColor` on its own shape (identity frame, only built when a
per-face list exists).

**Verified 24/24** on the RTX 3060 (VirtualGL egl0 + Xvfb, NVIDIA
banner checked), td_shaded9.py in the session scratchpad: a red
sphere poking through the cut plane (red annulus around the
cut-colored disc, bbox containment), a kept-side green box (body
color), a per-face DiffuseColor box (+X face magenta shows, cyan
faces must not), stacking bands, a two-source detail (both colors,
green above red), CutSurfaceColor recolor blue -> yellow re-captures
and follows, CutSurfaceDisplay "Hide" restores the owner-color disc.
td_shaded3 (sec 28's sections/details rig) re-passes 0-fail --
default SvgHatch display keeps owner-colored cut faces, unchanged.

Trap for the next session: the section view CS can flip the image
vertically vs the base view (higher-z geometry landing lower in the
capture) -- assert band ORDER agnostically (middle-between-outers),
not absolute up/down.

Still open, known and accepted: aligned complex sections stay
unshaded; the bgfx-grade capture still excludes derived shapes (the cut solid exists nowhere
in the backend scene; feeding it as a transient capture scene remains
the far option).

## 31. Implementation status (2026-08-25): the underlay gap closure -- stored frames, prepared-space capture, aligned sections, backend derived capture

The order: close ALL of section 30's accepted gaps. All four are
closed, verified 0-fail on the RTX 3060 (VirtualGL egl0 + Xvfb,
NVIDIA banner checked per run): the new rigs td_shaded10 (chains,
aligned, >500 faces) and td_shaded11 (backend derived capture), plus
td_shaded9 and td_shaded3 re-passing as regressions.

### 31.1 Gap 1: the exact build-time frame is stored, not re-derived

Every derived view now records the transform mapping its derived
shape back to the global source frame, composed at build launch from
the same parameters the worker consumes and committed in the async
callbacks BESIDE the shapes they describe (an input recomputed
mid-cut cannot desynchronize them). Transient, like the shapes: they
rebuild on recompute/restore.

- `DrawViewSection`: `m_cutFrame` (`m_cutShapeRaw` -> global,
  resolved through `getShapeToCutFrame()`, which mirrors
  `getShapeToCut()`'s branches through the BaseView chain) and
  `m_preparedFrame` (`m_preparedShape` -> global, carrying the
  1/Scale factor), committed in `prepareShape`.
- `DrawViewDetail`: `m_detailFrame` and `m_scaledFrame`, composed in
  `detailExec`, committed in `onMakeDetailFinished`.
- `DrawViewPart::getShapeForDetailFrame()` (virtual) hands a base
  view's frame to a detail on it; the section override composes
  uncentering and unrotation onto `m_cutFrame`; the complex-section
  override answers FALSE for the Aligned strategy (the unfolded
  fiction is not one rigid move).

`ShadedUnderlay`'s resolver reads the stored frame instead of
re-deriving base rotation + bbox centroid: the 0.1mm drift tolerance
is gone (uniform 1e-3 now), and detail-of-section, detail-of-detail
and section-with-detail-ancestor chains all resolve per-face colors.
A detail's cut-born faces take the NEAREST section ancestor's
CutSurfaceColor when that section displays "Color" -- a detail of a
section shows the same cut faces its base colors.

### 31.2 Gaps 2+3: prepared-space capture and the persistent per-view scene

The derived-shape capture no longer renders the raw cut/detail shape
through re-derived camera math: it renders the PREPARED shape -- the
exact HLR input (`m_preparedShape` / `m_scaledShape`), already
centered, scaled and rotated into projection space -- under a
straight-on camera (`getProjectionCS()` / `m_viewAxis`) with the 2D
origin at the CS origin and window scale 1. Registration is by
construction: whatever fiction prepareShape built is what HLR
projected. That closes gap 3 for free -- an aligned complex section's
prepared shape IS its unfolded fiction, so it captures like any
other section (uniform color: no rigid frame exists for probes).

The scene is PERSISTENT per view (`ShadedUnderlay.cpp`'s
`PreparedScene` cache, keyed on the view, evicted on
`signalDeletedObject`): tessellation and per-face classification run
once per geometry change (staleness = the prepared shape's TShape
identity + the source set), and each capture only resolves CURRENT
colors from the cached classification records
(`FaceClassRec`: on-source-face / on-source-boundary / cut-born /
unclaimed) and updates the palette -- so a recolor or a
CutSurfaceDisplay toggle follows without a single distance query
(the grid rig's recolor: 60s-timeout before, 1.5s after). The
former hard 500-face cap became the `ShadedUnderlayMaxFaces`
preference (default 5000); a 600-face compound-grid section resolves
full per-face colors with zero fallback.

### 31.3 Gap 4: the backend renders the dedicated scene

`Render::Renderer::setCaptureScene(DrawCallList&&)` /
`clearCaptureScene()`: a transient supplied scene that stands in for
the resident scene feed during `renderOffscreen`, through the same
swap-and-restore body as the section-26.2 object filter
(`BGFXRenderer::renderSwappedScene`, refactored out of
`renderFiltered`). No `drawListVersion` bump: supplied meshes upload
on demand at submission like the highlight feed's and TTL-collect
after the capture; resident buffers stay under the keep-set.

`ShadedUnderlay::captureSceneViaBackend` builds the feed from the
dedicated scene exactly the way the interactive view is fed: a
private headless-seeded `SoFCRenderCacheManager::traverse` over the
scene root, `RendererBridge::translate` on the resulting vertex-cache
map, then the shared FBO/readback dance (refactored into
`renderBackendFrame`). Per-face colors survive the pipeline (the
vertex cache captures per-vertex diffuse via the primitive material
index). Refusals fall back to the Coin offscreen render as before.

### 31.4 Traps this session paid for (do not rediscover)

- `BRepExtrema_DistShapeShape` reports an inner solution ONLY against
  a SOLID. Probed against a COMPOUND of solids it returns the
  distance to the nearest boundary -- every cut-born face of a
  compound source reads unclaimed (1mm to the wall, over tolerance),
  and the whole section shades fallback. The resolver explodes each
  source into per-solid probe targets (bbox-pruned) for exactly this
  reason.
- A `DrawComplexSection` created Aligned FROM THE START (scripting)
  had a null `m_toolFaceShape`: only the Offset path's
  `makeCuttingTool` built it, and `BRepBuilderAPI_Copy` on the null
  shape threw, killing the aligned-pieces job ("failed to make
  alignedPieces"). The GUI flow masked it (the dialog's first
  recompute runs while ProjectionStrategy still defaults to Offset).
  Fixed: the aligned branch builds the tool when missing. Scripted
  aligned sections ALSO need `XDirection` set explicitly --
  `SectionDirection` "Aligned" reads the CS from the properties, and
  an unset XDirection is a zero vector that throws gp_Dir at
  recompute.
- A detail-of-section consumes the section's ASYNC cut result; a
  recompute that ran before the cut landed just misses, and nothing
  re-triggers it. Rigs must re-touch the detail after the base
  section's underlay confirms the cut is in.
- Rig isolation: `RenderCache=3` and the Render Type PERSIST in user
  parameters across rig runs -- a "Coin-path" rig inherits the
  backend from an earlier backend rig unless it resets
  `View/RenderCache` itself.
- Classification totals are logged per build ("classified N faces
  (a source, b cut-born, c fallback)") -- read them before theorizing
  about wrong colors: all-fallback means the probes miss (frame),
  source-but-wrong-color means palette resolution.

## 32. Implementation status (2026-08-25): DrawBrokenView is ported

The selective port recommended in section 6 has begun: `DrawBrokenView`
(item 2 of the list) is in, App + Gui + command.  Notes and decisions:

- **No FCBRepAlgoAPI port was needed.** Upstream's own
  `DrawBrokenView::apply1Break` had already retreated to plain
  `BRepAlgoAPI_Cut` (their comment: FCBRepAlgoAPI refuses to cut
  compounds, github issue 27414), so the `FCBRepAlgoAPI_Cut.h` include
  was vestigial and is dropped.  The section-4 blocker list overstated
  this one.
- **Shims added** (all verbatim ports of small upstream helpers):
  `DrawUtil::closestBasisOriented` / `maskDirection` /
  `coordinateForDirection`, `ShapeUtils::edgesAreParallel`,
  `ShapeExtractor::isSketchObject`, `pointPair::scale`,
  `Preferences::BreakLineStyle`.  `Preferences::BreakType` returns
  `int` here rather than upstream's `DrawBrokenView::BreakType` --
  including DrawBrokenView.h from Preferences.h for one default value
  was not worth the header coupling; the single use site casts.
- **Python binding** rewritten from upstream's `.pyi` to this tree's
  `DrawBrokenViewPy.xml` + PyImp pattern (3 methods:
  `mapPoint3dToView`, `mapPoint2dFromView`, `getCompressedCenter`).
- **The dimension remap** (true distances across breaks) lands in this
  fork's `DrawViewDimension::getDimValue` projected-Distance branch;
  upstream had refactored the same logic into `getProjectedDimValue`,
  which does not exist here.
- **QGIBreakLine** keeps the fork's per-class Qt type convention
  (`QGraphicsItem::UserType + 178`), not upstream's `QGIUserTypes`
  enum, and `drawBreakLines` uses the scaled accessors
  (`hiddenWidthScaled`) per the per-view LineScale rule.
- **ShadedUnderlay: broken views are guarded out** (capture returns
  false).  The generic branch would render the UNBROKEN sources -- the
  projection no longer matches the 3D scene -- and the compressed shape
  is not kept in prepared space, so the derived-scene path cannot
  register either without new plumbing.  If an underlay is ever wanted
  here: run `centerScaleRotate` over `m_compressedShape` with
  `m_saveCentroid`/Scale/Rotation and take the section branch.  Also
  the natural reason there is no vg/PageFeed change: break-line
  decorations feed through the generic QGIDecoration capture.
- The command is written in this tree's idiom (CmdTechDrawView's
  selection loop + cmdAppDocument/cmdAppObjectArgs), with upstream's
  NoResolve second pass to dig break sketches out of Body subnames.
  Upstream's `checkDirectionVsBasis` helper does not exist here and is
  not needed: DrawBrokenView snaps directions with
  `closestBasisOriented` internally.

Verified: headless smoke (box 100mm, sketch break 30..70, Gap 10 ->
compressed span exactly 70.0, `mapPoint2dFromView` round-trip exactly
100.0, 8 coarse-HLR edges = two rectangles); GUI smoke under
xvfb+vglrun (command registered, QGIBreakLine item present in the page
scene, survives a BreakLineType flip).

Still open from the section 6 list: `LineFormat`/`Tag`, `CommandAlign`,
`QGVNavStyleSolidWorks` (item 3), and the `DimensionAutoCorrect` /
`ShapeFinder` decision (item 4).  The `isSame()` chore (section 4) did
not arise: it bites only when adopting upstream's property-list
classes, and this port keeps the fork's.

## 33. Re-evaluation (2026-08-26): the section 6 plan, re-measured

Section 6 was written on 2026-08-24 against upstream `2bb00c9186` and
fork `4b5977c92b`.  Since then the fork gained 188 commits (31 of them
in TechDraw) and upstream gained 114 (10 in TechDraw).  The plan does
not survive unchanged: **item 4, ranked last and gated on an
architectural decision, is now the cheapest remaining work, and item 1,
"do this first regardless", has lost its only customer.**

### 33.1 The numbers, re-measured

Same BASE trick (`a662fbb2ff`), same exclusions:

| | files | lines | commits |
| --- | --- | --- | --- |
| TechDraw, fork, 2026-08-24 | 177 | +4339 / -3100 | 100 |
| TechDraw, fork, **now** | **204** | **+11497 / -3126** | **159** |
| TechDraw, upstream, now | 582 | +37168 / -22718 | -- |

The fork's added lines nearly tripled.  Almost all of it is four new
subsystems, and for the first time there are files the fork has that
upstream does not:

| file | added |
| --- | --- |
| `Gui/PageFeed.cpp` | 1755 |
| `Gui/ShadedUnderlay.cpp` | 1384 |
| `App/DrawBrokenView.cpp` | 1228 |
| `Gui/PageServe.cpp` | 540 |

`DrawBrokenView` came *from* upstream (section 32).  The other three are
the vg-renderer page feed, the WebSocket page publisher and the shaded
raster underlay -- fork-only, and they change the porting rules below.

### 33.2 Blocker list: what closed on its own

Four entries of the section 4 list are gone without anyone porting them:

- `App::TransactionName`, `App::TransactionCloseMode` -- **present**, in
  `src/App/TransactionDefs.h`, landed by `ea62a190fd` (the additive core
  APIs the Material port needed).
- `Base::Color::fromValue` -- **present**.
- `ShapeExtractor::getLocatedShape`, `stripInfiniteShapes`,
  `isSketchObject` -- **present**, added by the DrawBrokenView port.
- `Mod/Part/App/FCBRepAlgoAPI_*` -- **not needed**; section 32 showed
  upstream itself cuts with plain `BRepAlgoAPI_Cut` and the include was
  vestigial.

`Part::ShapeOption` is still absent and still does not matter: every
upstream call is
`getShape(obj, ShapeOption::ResolveLink | ShapeOption::Transform)`, and
the fork's `Part::Feature::getShape` already defaults `resolveLink=true,
transform=true`.  The call site becomes `getShape(obj)`.

### 33.3 Item 4 dissolves into a shim -- promote it to first

Section 6 gated `DimensionAutoCorrect` behind "decide whether ShapeFinder
or the fork's Link resolution is the sub-element story".  Measured, that
decision does not exist.  The entire `ShapeFinder` surface upstream
TechDraw touches is **three statics**:

| upstream | fork answer |
| --- | --- |
| `ShapeFinder::getLocatedShape(obj, subName)` | `Part::Feature::getShape(obj, subName)` -- the fork resolves link and transform by default |
| `ShapeFinder::stripInfiniteShapes` | `ShapeExtractor::stripInfiniteShapes`, already present |
| `ShapeFinder::getLastTerm(subName)` | last `.`-segment helper, ~5 lines |

So: **do not port `ShapeFinder` (582 lines) and do not decide anything.**
Shim two functions and the fork's own Link resolution *is* the
implementation, which was always the answer we wanted.

Better still, the dependency stack is already here and the fork has not
touched it -- a transplant with nothing to re-apply:

| file | fork delta vs BASE |
| --- | --- |
| `App/DimensionReferences.{h,cpp}` | +2/-2 |
| `App/GeometryMatcher.{h,cpp}` | untouched |
| `App/DimensionGeometry.{h,cpp}` | +9/-0 |
| `App/DrawViewDimension.{h,cpp}` | +62/-46, all mechanical |

And this is not a missing feature, it is an **older generation of a
feature the fork already ships**.  The fork has `SavedGeometry` +
`updateSavedGeometry` / `compareSavedGeometry` / `fixExactMatch` inline
in `DrawViewDimension`, driven by the same `GeometryMatcher` and gated
by the same `Preferences::autoCorrectDimRefs()`.  Upstream lifted that
into a 687-line `DimensionAutoCorrect` and extended it with **similar**
(not just exact) matching, in 2D and 3D, plus `fixBrokenReferences`.
The wiring into `DrawViewDimension` is 8 touchpoints, one new
`BoxCorners` property, and one member.

That makes it a targeted graft, not a wholesale file adoption -- upstream
also rewrote `DrawViewDimension.cpp` by +801/-1090 for unrelated reasons
and none of that has to come along.

The fork's `+62/-46` to re-apply on top: `Prop_Output` -> `Prop_None` on
the format properties, `throw` -> `THROWM`, and the `DrawBrokenView`
dimension remap in `getDimValue`.

### 33.4 Item 3 splits: one keep, two drops

- **`CommandAlign`** (183 lines) -- **keep**.  Aligns views by rotating
  so two picked vertices land horizontal or vertical.  Needs only
  `Gui/Selection`, `DrawUtil`, `QGIView`, `ViewProviderViewPart` -- no
  `ToolHandler`, no selection-layer contact.  Draws nothing, so no vg or
  underlay work.  Cheapest real feature on the list.
- **`QGVNavStyleSolidWorks`** (173 lines) -- **keep, low priority**.
  Purely additive alongside 11 identical siblings, input-only, so again
  no vg or underlay work.  Note it is a two-part port: it pairs with
  upstream's `Gui/Navigation/SolidWorksNavigationStyle.cpp`, which the
  fork also lacks (and the fork keeps nav styles flat in `src/Gui/`, not
  in a `Navigation/` subdir).
- **`LineFormat` / `Tag`** -- **drop**.  Re-read, these are code
  organization, not features.  `LineFormat` is the fork's class moved out
  of `Cosmetic.h` into its own file; the only functional gain is a
  "current line format" global (`getCurrentLineFormat` and friends) and a
  5-arg constructor.  `Tag` is a uuid base class extracted from
  `CosmeticEdge` / `CenterLine` / `Geometry` -- it carries `Save`/
  `Restore`, so adopting it is a persistence change to three
  document-bearing classes for zero user-visible gain.  `Cosmetic.h` is
  fork-heavy and `PageFeed` now consumes `LineFormat`.  Not worth it.

`QGIDatumLabel` gets the same verdict as `Tag`: upstream split it out of
`QGIViewDimension.h`, where the fork still has it.  A split that collides
with both the fork's QGI rework and the vg dimension capture, for no
feature.  **Drop.**

### 33.5 Item 1 loses its customer -- demote it

Section 6 said to do `Gui/ToolHandler.h` + `canRecomputeOnWorker` + the
`Gui::Command` transaction overloads "first regardless".  Two of those
four unblock nothing now: `TransactionDefs.h` landed on its own (33.2),
and `ToolHandler`'s only TechDraw consumer is `TechDrawHandler`, which
is upstream's rewrite of the selection layer -- explicitly scope OUT.
None of the three items kept above needs any of it.

**Demote to: port on demand.**  It stops being a prerequisite and becomes
a cost of whichever future feature actually calls for it.

### 33.6 Two new rules, from what the fork built since

**Every ported Gui item now has a second renderer to satisfy.**  A
TechDraw page is drawn twice here -- Qt `QGraphicsScene`, and the vg
feed via `PageFeed` for the browser tier -- and `DrawViewPart`
descendants additionally have to answer to `ShadedUnderlay`.  So the
port checklist grew a row:

| what the item does | vg / underlay cost |
| --- | --- |
| App only (`DimensionAutoCorrect`) | none |
| input only (`QGVNavStyleSolidWorks`) | none |
| a command that draws nothing (`CommandAlign`) | none |
| a new `QGI*` decoration (`QGIBreakLine`, done) | rides the generic `QGIDecoration` capture |
| a new `DrawViewPart` descendant (`DrawBrokenView`, done) | needs an explicit `ShadedUnderlay` decision -- it was guarded out |
| **re-shaping an existing `QGI*`** (`QGIDatumLabel`) | **touches `PageFeed`; avoid** |

All three kept items sit in the free rows.  That is not a coincidence --
it is the same property that made them cheap in the first place.

**The selection layer is diverging faster, not slower.**  All ten
upstream TechDraw commits since the evaluation land in the files the
fork rewrote hardest:

| upstream commit | file | fork delta on that file |
| --- | --- | --- |
| `bcc15c7632` context-aware context menus (+281) | `MDIViewPage.cpp` | +299/-162, plus split-view integration |
| `da103eb753` selection helpers -> templates/lambdas | `MDIViewPage.cpp` | same |
| `3356db3290` balloon selection and hover | `QGIPrimPath.cpp` | +111/-44 |
| `f217f35957` hover fires twice | `QGIViewBalloon.cpp` | fork reworked hover |
| `5e469ce2fd` view frame visibility on selection | `QGIView.cpp` | +93/-30 |

`MDIViewPage`, `QGSPage` and `QGVPage` are now *also* wired into the
split-view `ViewArea` canvas, so they are doubly forked.  Section 6's
"leave the `QGI*` selection layer alone" holds harder than when it was
written.  The one upstream item there worth wanting is the context-aware
context menu (`bcc15c7632`) -- flagged, not scheduled.

### 33.7 The revised order

1. **`DimensionAutoCorrect`** -- see section 34: measured, the class is
   mostly hollow and what is worth taking is the canonical-frame fix.
   **Built 2026-08-26.**
2. **`CommandAlign`** -- additive command, no renderer contact.
   **Built 2026-08-26.**
3. **`QGVNavStyleSolidWorks`** + `Gui` `SolidWorksNavigationStyle` --
   additive, input only, lowest value.
4. On demand only: `ToolHandler`, `canRecomputeOnWorker`, the
   `Gui::Command` transaction overloads.

Dropped from the plan: `LineFormat`, `Tag`, `QGIDatumLabel`,
`ShapeFinder`.  Still scope OUT: the `QGI*` selection layer, upstream's
`TechDrawHandler`, wholesale adoption.


## 34. Implementation status (2026-08-26): items 1 and 2 of 33.7 are built,
and item 1 turned out to be a bug fix

Section 33.3 promoted `DimensionAutoCorrect` to first on the strength of a
file-level reading -- 687 lines, "similar-geometry matching, 3D matching,
broken-reference repair".  Reading the implementation corrects that, and
the correction is worth more than the original claim.

### 34.1 What upstream's DimensionAutoCorrect actually contains

**All four `findSimilar*` methods are unimplemented stubs.** So are
`searchViewForSimilarEdge` and the `exact` parameter threaded through
`searchObjForVert` / `searchObjForEdge`.  Each returns false (or an empty
reference) after a "not implemented yet" comment.  Phase 2 was designed
and never written.

**`BoxCorners` is written and never read.** `saveFeatureBox()` fills it
from the view bounding box; `getSavedBox()` reads it and has no callers.
It is scaffolding for the phase that does not exist, and adopting it would
add a persisted property to every dimension in every document for nothing.

So the class is the fork's own exact-match logic, re-housed, plus a 3d
object cache and a per-reference state vector.  **The fork already ships
the exact-match logic** -- `SavedGeometry` + `updateSavedGeometry` /
`compareSavedGeometry` / `fixExactMatch`, driven by the same
`GeometryMatcher` and gated by the same `Preferences::autoCorrectDimRefs()`
-- with `handleNoExactMatch()` sitting where upstream's phase 2 would go,
carrying the comment "this is where we insert the clever logic".  Both
sides stopped at the same place.

### 34.2 The one thing in there that matters, and it is a bug

Upstream's `ReferenceEntry::asCanonicalTopoShape` removes the view
**rotation** as well as the scale.  The fork's `asTopoShape` removes only
the scale.  That is not a refactor, it is a defect:

> Saved reference geometry carried the view rotation it was captured at,
> and was compared against display geometry carrying the rotation the view
> has now.  Rotate a view and every reference looks changed.

Measured, `100x100` square view, dimension on `(Vertex0, Vertex1)`:

| | references | value |
| --- | --- | --- |
| at rotation 0 | `(Vertex0, Vertex1)` | 100.000000 |
| after rotating 90 deg | **`(Vertex1, Vertex3)`** | 100.000000 |

The failed comparison sends it into the exact-match recovery, and on a
symmetric shape a rotated corner sits exactly where a different corner
used to be -- so the recovery "repairs" the dimension onto geometry the
user never picked, and leaves it there.  The value survives only because
the square is symmetric.

Fixed in `37cb56d3a0` by comparing in a canonical frame (unscaled and
unrotated) when saving, when comparing, and when searching for a
replacement.

**Trap:** do not port `asCanonicalTopoShape` verbatim.  Upstream's
`asTopoShape` returns raw display geometry and the canonical form removes
the scale; the fork's `asTopoShape` has already removed it.  Copying the
helper across unscales twice.

**Trap:** the stored frame negates Y relative to the Python accessors.
`getVertexBySelection` reporting `(-50, -50)` corresponds to `(-50, 50)`
in `SavedGeometry`.  A test that constructs saved geometry from accessor
values without the flip matches neither frame and looks like a broken fix.
That cost one debugging round.

### 34.3 Migrating documents written before the fix

There is no version stamp on `SavedGeometry`, so
`migrateSavedGeometryFrame()` tests the frame itself: an entry that fails
to match canonically but matches once the rotation is put back was written
by the old code and is rewritten in place; one that matches canonically is
already current; one that matches neither is a genuine change and is left
to the normal path.  Exact, lossless, and it needs no new property.

**Trap:** `execute()` also runs while the document is still restoring,
against a view that has not projected yet. The first version marked the
migration done on that pass, spent the one chance a document gets, and let
the wrong-corner repoint through anyway.  The migration now reports
whether it could evaluate, and is only marked done once the view has
geometry.

### 34.4 CommandAlign

Ported in `89c3734af6`, +390/-0 across 12 files, nothing removed and
nothing in the selection layer touched.  Supporting helpers are new entry
points beside existing ones: `DrawUtil::getIndexFromName` over a list and
`isGeomTypeConsistent`, `QGIVertex::toVector2d` and
`vector2dBetweenPoints`, `QGIView::getObjects<T>`, three
`DrawGuiUtil::rotateToAlign` overloads.

Two deliberate deviations: the rotation is wrapped in
`openCommand`/`commitCommand` (upstream sets the property bare, so its
version leaves no undo entry), and the selected sub-elements are found by
searching the selection for the view rather than taking entry 0, because a
Link selection resolves to an object that need not be first.

**Trap:** `CoarseView` suppresses `QGIVertex` items entirely
(`QGIViewPart::showVertices` returns false for it), so the two-vertex path
cannot fire in a coarse view.  That is upstream behaviour, not a port
artifact -- but it means a headless-style smoke with `CoarseView=True`
tests nothing here and hangs on the warning modal.

### 34.5 Test state

Rigs in the session scratchpad, following the DrawBrokenView convention of
not committing them: `align_smoke.py` (20/20), `dimref_suite.py`
(rotation invariance across 90/45/180/-30, plus a genuine renumbering that
must still be recovered), `mig_write.py` + `mig_read.py` (the two-process
old-format migration).  All green under Xvfb on the real GPU.

### 34.6 Where this leaves the plan

Item 1 of 33.7 is **done as a bug fix, not as a class adoption**.  If
upstream ever implements the phase-2 stubs, the class becomes worth
revisiting -- and the fork's `handleNoExactMatch()` is the hook it would
land on.  Until then there is nothing there to take.

Item 2 is done.  **Item 3 (`QGVNavStyleSolidWorks` + the `Gui`
`SolidWorksNavigationStyle` the fork also lacks) is the only thing left on
the revised order**, and it is the lowest-value entry on it.

## 35. Implementation status (2026-08-26): item 3 is built -- the port is complete

`QGVNavStyleSolidWorks` and the `Gui` `SolidWorksNavigationStyle` are in.
That closes the revised order of 33.7; nothing is left on it.

### 35.1 Both halves are sibling adaptations, not verbatim copies

Diffing upstream's two SolidWorks files against upstream's Blender files
shows SolidWorks **is** the Blender style with pan and zoom swapped onto
the other modifier:

| | Blender | SolidWorks |
| --- | --- | --- |
| MMB | rotate | rotate |
| CTRL + MMB | zoom | **pan** |
| SHIFT + MMB | pan | **zoom** |
| LMB + RMB | pan | (no such binding) |

So each file was generated from **the fork's own sibling** and given that
delta, rather than copied from upstream.  This matters because the fork's
nav styles are an older upstream revision: upstream's
`SolidWorksNavigationStyle.cpp` calls `updateSelectionStartPosition()`,
`handleSelectionDragMotion()` and `setupPanningPlane()`, none of which
exist here, and its `QGVNavStyleSolidWorks` uses `Base::Console().message`
(lower case) and no `PreCompiled.h`.  A verbatim port compiles nowhere.

Deviations from upstream, each deliberate:

- **TRAP: Upstream's per-style `hasPanned/hasDragged/hasZoomed` reset block is
  NOT ported.** The fork already resets those three centrally, in
  `NavigationStyle::setViewingMode()` when the mode becomes `IDLE`
  (`NavigationStyle.cpp:1367`).  Copying the block in would be dead
  duplication.
- **The `default:` reset-to-IDLE in the combo table IS ported**, unlike
  the fork's older siblings which still have a bare `break;`.  Without it,
  releasing the middle button while the modifier is still held leaves the
  style stuck in `PANNING`/`ZOOMING` and the next bare mouse move keeps
  panning.  The fork's Blender/Revit still carry that bug; this file does
  not, and 35.3 tests exactly that.
- The two `newmode = SELECTION` assignments the fork's Blender makes in
  the pan/zoom motion branches are dropped, as upstream dropped them --
  the combo table below re-derives the mode from the buttons on every
  event, so they never survived anyway.
- `lookAtPoint()`'s fork behaviour is **kept** (the fork's returns bool and
  falls back to `panToCenter`; upstream's returns void), as is the fork's
  inline panning-plane setup and its `lockButton1` handling.
- **TRAP: Upstream's `QGVNavStyleSolidWorks::handleMouseReleaseEvent` nests
  the `zoomingActive` stop inside `if (panningActive)`**, so a Shift + MMB
  zoom never ends on button release -- only on Shift release.  Fixed here
  by using the flat per-button structure the fork's `QGVNavStyleOCC`
  already uses.

### 35.2 Registration is five files, and the preferences UI is automatic

`src/Gui/`: the class in `NavigationStyle.h`, the new
`SolidWorksNavigationStyle.cpp`, one line in `CMakeLists.txt`, and
`SolidWorksNavigationStyle::init()` in `SoFCDB.cpp`.  `src/Mod/TechDraw/Gui/`:
the new pair, two lines of `CMakeLists.txt`, and the include plus
`find("SolidWorks")` branch in `QGVPage::setNavigationStyle`.

No UI file needs editing: `UserNavigationStyle::getUserFriendlyNames()`
enumerates `Base::Type::getAllDerivedFrom(UserNavigationStyle)` and strips
the namespace and the `NavigationStyle` suffix, so registering the type is
what makes "SolidWorks" appear in Preferences.  The page picks the style up
live -- `QGVPage`'s `ParameterGrp` observer calls `setNavigationStyle()` on
any `NavigationStyle` parameter change.

The `Mod/Tux` navigation indicator is a separate hardcoded `a0..a10` menu
that the type system does not reach.  It was extended on request -- see
35.4.  Left alone, a style missing from it degrades to its "Undefined"
entry, so this is polish rather than a functional gap.

### 35.3 Test state: 16/16, each gesture run against the sibling as a control

`nav_smoke.py` in the session scratchpad (not committed, per the
DrawBrokenView convention), on the real GPU (RTX 3060 verified in the log)
under VirtualGL + Xvfb.  Every gesture runs twice, once under
`Gui::BlenderNavigationStyle` and once under SolidWorks: **the Blender row
is the control** -- if it does not show its own documented bindings then
event injection is broken and the SolidWorks row proves nothing.

3D half, classified from the camera (orientation -> rotate, ortho height
-> zoom, position -> pan): both styles rotate on MMB; Blender zooms on
CTRL and pans on SHIFT, SolidWorks pans on CTRL and zooms on SHIFT.  Plus
the stuck-mode guard of 35.1: after releasing MMB with CTRL still held, two
further bare mouse moves must move the camera by nothing.

Page half, classified from the `QGVPage` transform and scrollbars: plain
MMB pans under SolidWorks and does nothing under Blender; SHIFT + MMB
zooms under SolidWorks and pans under Blender; CTRL + MMB pans under
SolidWorks and does nothing under Blender.

Two traps paid for in that rig:

- **TRAP: The QGV styles read `QGuiApplication`'s GLOBAL mouse-button and
  modifier state, not the event's.**  A synthetic `QMouseEvent` through
  `sendEvent` therefore drives nothing -- the handler sees no buttons.
  `QTest` goes through the window-system interface and does set that
  state, but `QTest::mouseMove` carries no modifiers and **resets the
  global modifier to none**.  The recipe that works: `QTest.mousePress` to
  set the buttons, `QTest.keyPress(Key_Shift, ShiftModifier)` to set the
  modifier, then deliver the moves with `sendEvent`, which leaves the
  global state alone.  (The 3D half has no such problem: `syncModifierKeys`
  reads the `SoEvent`, which Quarter fills from the Qt event.)
- **TRAP: Scaling a page view past ~3x segfaults** in the Qt FreeType raster
  engine on the stock template's `line-height:0%` text (`err=62`) -- the
  known trap, nothing to do with navigation.  Hide the template item first,
  and note it only sticks **after** the page view exists; setting it before
  `page.ViewObject.show()` is overridden when the view is built.

### 35.4 The Tux indicator, and a correction about its icons

Added on request: the navigation indicator now carries a SolidWorks entry,
sitting alphabetically between Revit and TinkerCAD.

**Correction to what 35.2 first said.**  The claim that the fork's Tux
"ships TinkerCAD referencing four icons that are not in its icon
directory" is **wrong**.  The files are indeed absent, but `Tux.qrc` maps
the names onto sibling artwork with `alias=`:

    <file alias="icons/NavigationTinkerCAD_Select.svg">icons/NavigationOpenCascade_Select.svg</file>

So nothing in that menu is broken, and the fork already has an established
way to give a style an entry without drawing new artwork.  That route was
available here too and was **not** taken: upstream's generic mouse icons
name the actual modifier (`ShiftMiddle`, `CtrlMiddle`), whereas aliasing a
sibling would have shown Blender's or OpenCascade's artwork under
SolidWorks' bindings, where the modifiers differ -- a misleading picture is
worse than an extra file.

What went in: seven upstream icons (`NavigationSolidWorks_{dark,light}.svg`
for the menu, and the five `Navigation_Mouse_{Left,Scroll,Middle,
ShiftMiddle,CtrlMiddle}.svg` the tooltip draws), seven `Tux.qrc` lines, and
fifteen lines of `NavigationIndicatorGui.py` -- the action, its place in
the menu, the tooltip, and the tooltip-toggle line.  The tooltip markup is
upstream's, which needed no adaptation: it already builds from the same
`text01..text10` strings this file defines, and ends with the same
rotation-focus line the fork's own Revit tooltip uses.  SolidWorks is the
only entry using the generic set; the other ten keep their per-style
gesture icons.

Notes for anyone touching this again:

- `Tux_rc.py` is **generated** from `Tux.qrc` by `PYSIDE_WRAP_RC`, so a new
  icon needs `ninja Tux` before it exists at runtime.  Editing the qrc
  alone changes nothing in a built tree.
- The menu order is the `menu.addAction` order, not the `aN` numbering, so
  a new entry can be appended as `a11` and still display in the right
  place.  `setCurrent()` iterates `gStyle.actions()` and needs no edit.
- Since the fork's indicator hardcodes `_dark.svg` (it has no theme switch,
  unlike upstream's `StyleSheetType`), the `_light` icon is carried for
  parity with its ten siblings but is not referenced.

`tux_smoke.py` in the session scratchpad, 10/10 on the real GPU: the action
exists, is labelled, its menu icon resolves, it sits between Revit and
TinkerCAD, its tooltip names the style and **every one of its five `<img>`
sources resolves to a non-null pixmap**, and selecting the style makes it
the menu's default action, hides the "Undefined" entry and moves the
indicator's icon.  The image check is the one that matters: a tooltip
referencing a name absent from the resource fails silently at render time.
