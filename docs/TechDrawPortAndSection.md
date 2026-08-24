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
