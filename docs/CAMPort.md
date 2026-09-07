# CAM (Path) workbench port: plan and ledger

Ruling (user, 2026-08-23): port upstream's CAM workbench into this fork.
**Phase 3 (Clipper2) is required, not optional.**

This file is the single place the CAM port is tracked. It carries the
plan, the Clipper1 -> Clipper2 reference the migration needs, and the
ledger of what has landed.

## What is being ported

This fork's `src/Mod/Path` is upstream Path as of merge-base
`a662fbb2ff` (2023-12-31, 0.22-dev). Upstream renamed the module to
`src/Mod/CAM` and has moved a long way since.

| | fork `src/Mod/Path` | upstream `src/Mod/CAM` |
|---|---|---|
| files | 555 | 948 |
| LOC (py+cpp+h) | ~96k | ~207k |
| upstream commits touching it since merge-base | -- | 1710 |

The Python tree alone is +61,502 / -22,200 across 333 files, with 162
new files: a rewritten tool-library/toolbit system, a new `Machine/`
subsystem, a reworked post-processor architecture, and a test suite that
doubled (56 -> 118 files).

## Why this costs far less than those numbers

- **The fork has barely touched Path.** Excluding the libarea move, the
  fork-local delta is 53 files / +1,181 lines, and only **four** of them
  are Python: `InitGui.py` (a `create_group` callback), `Path/Op/Adaptive.py`
  (one line, `import libarea as area`), and two `hasattr(obj, 'Tool')`
  guard patches in `Path/Tool/Controller.py` and `Path/Tool/Gui/Controller.py`.
  So ~84k lines of Python are pristine upstream 0.22 and can be replaced
  wholesale.
- **The module is isolated.** Nothing outside `src/Mod/Path` references it
  except `Tools/updatecrowdin.py`, `Tools/updatets.py`, and a stale comment
  in `Area/App/ParamsHelper.h`.
- **The FEM port is the precedent.** `f3bb79474b` took upstream FEM
  wholesale (1,018 files, +414,941). The adaptation was ~208 lines across
  31 files in five commits, then four bugfix commits to green. Two days.
- **Two blockers are already cleared.** Upstream replaced every `*Py.xml`
  with `.pyi`; this fork adopted that generator in `8418a8fae6` (37 `.pyi`
  in tree). Upstream's new `tsp_solver` needs pybind11; the fork already
  has `SetupPybind11.cmake` and uses it in `Area/PyArea` and `MeshPart`.
- **`Path.Area` is already bridged.** `src/Mod/Path/App/AppPath.cpp:72`
  re-registers `AreaLib::AreaPy::Type` into the `Path` module as `"Area"`,
  so the six `Path.Area(` call sites in upstream CAM keep resolving even
  though Area lives in `src/Mod/Area`. No shim work needed.

## The constraint that shapes the plan

`libarea` is **not CAM's private library any more**. The fork's
ifcopenshell (`realthunder/IfcOpenShell`, branch `LinkVibe`) links it via
`find_package(libarea CONFIG QUIET)` and uses fork-only API:

    ifcopenshell/src/ifcgeom/kernels/opencascade/boolean_utils_2d.cpp
      :245  CurveFromClipperPath(...)          fork-added export
      :395  CArea::m_fit_circles = ...         fork-added parameter
      :483  CurveToClipperPath(...)            fork-added export
      :490  ClipperLib::PolyTree solution      drives Clipper1 DIRECTLY
      :496  Execute(ctDifference, ..., pftNonZero, pftNonZero)
      :509  ClipperLib::ClosedPathsFromPolyTree(...)

`CurveToClipperPath` / `CurveFromClipperPath` have zero consumers inside
fcad -- they exist for this. Two copies of Clipper in one process is the
exact thing the split was done to prevent (see the `libarea` memory).

Consequence: **`src/Mod/Area` and the external libarea package stay.**
Upstream's CAM tree keeps `Area.*`, `AreaParams.h`, `ParamsHelper.h` and
`libarea/` inside the module; those are stripped from anything ported here.

## Phases

Execution order is **3, then 1, then 2** -- phase 3 is the foundation,
and it makes phase 2 nearly free.

### Phase 3 -- migrate this fork's Clipper1 to Clipper2 (REQUIRED, first)

Upstream's `CAM/App/Area.cpp` is now Clipper2-typed: `AreaParams.h`
includes `clipper2/clipper.h` and `myParams.JoinType` is a
`Clipper2Lib::JoinType`. This fork's `Mod/Area/App/Area.cpp` is
Clipper1-typed. Every future port of upstream Area work would otherwise
be a hand translation. That, not any single Clipper2 feature, is why this
phase is required.

Clipper2 is **not on conda-forge** (neither is Clipper1). It is vendored
inside libarea exactly as `clipper.cpp` is today, and its headers are
installed alongside, so ifcopenshell can keep driving it directly.

Clipper1 stays vendored beside it. `Mod/Area/PyArea/Adaptive.cpp` (14
ClipperLib references) is not migrated -- upstream has not migrated
theirs either, and their `Adaptive.cpp` uses both libraries at once.

### Phase 1 -- port CAM, without touching Clipper

Take upstream CAM wholesale at a pinned commit, one commit, FEM-style.
Strip `Area.*`, `AreaParams.h`, `ParamsHelper.h` and `libarea/` from the
ported tree. Adapt to fork core; re-apply the four Python patches and the
C++ set; put `PathSimulator/AppGL` behind a CMake option that defaults
off (see below). Green gate is `TestCAMApp`.

The only CAM Python that reaches libarea is `Adaptive.py`'s
`import libarea as area`, which this fork already patches.

### Phase 2 -- take upstream's Area.cpp work

Upstream has +1,422 / -726 (whitespace-ignored) in `Area.cpp` since the
merge base, including OCCT `ConnectWiresToWires` / `ConnectEdgesToWires`
bug workarounds and slicer fixes. After phase 3 these port as ordinary
diffs rather than translations.

## Fork-local work that must survive

C++ files where both sides moved (everything else is one-sided and
re-applies mechanically):

| file | fork | upstream (-w) |
|---|---|---|
| `Gui/ViewProviderPath.cpp` | +169 -51 | +280 -111 |
| `App/Path.cpp` | +66 -18 | +230 -106 |
| `App/AppPathPy.cpp` | +52 -42 | +212 -87 |

`App/AreaToolpath.cpp` / `.h` (455 lines) is a pure fork addition with no
upstream counterpart; it drops in but depends on `Mod/Area`.

Fork libarea delta versus upstream pre-migration:

| file | fork changed lines | collides with phase 3? |
|---|---|---|
| `Curve.cpp` | 443 (arc fitting, bisection, circles) | no |
| `AreaClipper.cpp` | 43 | yes -- the one real overlap |
| `Area.h` | 34 | yes (signatures) |
| `Curve.h` / `Area.cpp` / `Box2D.h` | 22 / 17 / 11 | minor |

Of the 43 colliding lines: `m_clipper_scale = 1e7` survives as a scalar;
the whole-circle `AddVertex` and the `MakeLoop` zero-turn guard survive
as retypes; `CurveTo/FromClipperPath` are a signature retype. One
genuinely conflicts -- the fork's **fill-rule parameters on `Subtract`
and `Union`**, where upstream went the other way and hard-coded
`FillRule::EvenOdd`. Reinstate them on upstream's `Clip()`, which still
takes rules.

## Clipper1 -> Clipper2 reference

Upstream did this in one squashed commit, `7618bec88a` (merged
2026-05-12): 33 files, +519 / -443, of which `AreaClipper.cpp` is 387.
It is largely a mechanical type swap.

| Clipper1 | Clipper2 |
|---|---|
| `IntPoint` / `Path` / `Paths` | `Point64` / `Path64` / `Paths64` |
| `DoubleAreaPoint` (hand-rolled) | `PointD` (native) |
| `Clipper` | `Clipper64` |
| `AddPath(p, ptSubject, closed)` | `AddSubject(paths)` / `AddOpenSubject` / `AddClip` |
| `Execute(ctUnion, sol, pftNonZero, pftNonZero)` | `Execute(ClipType::Union, FillRule::NonZero, sol)` |
| `PolyTree` + `ClosedPathsFromPolyTree` | `Execute(op, rule, closed, open)` |
| `CleanPolygon(p, dist)` | `SimplifyPath(p, eps, is_closed)` |
| `StrictlySimple(b)` | gone |

Note libarea's own `Point` collides with Clipper2's, so libarea's types
move into `namespace heeks` (upstream's choice; follow it).

### What Clipper2 adds

- **Robustness.** Clipper1 needed `StrictlySimple` because its core could
  emit self-intersecting output. Clipper2 removed the concept -- every
  `StrictlySimple` call was deleted with nothing replacing it.
- **Native double API**: `PointD` / `PathD` / `PathsD` beside `Point64`.
  Clipper1 was integer-only, which is why libarea had to invent
  `DoubleAreaPoint` and the `m_clipper_scale` lattice.
- **New operations**: `RectClip` / `RectClipLines`, `TrimCollinear`,
  `SimplifyPath` / `SimplifyPaths`, `InflatePaths`, Minkowski.
- **Scoped enums** (`enum class`). `JoinType` gained `Bevel`.
- C++17 required (a non-issue here).

### What Clipper2 drops

`StrictlySimple`, and `CleanPolygon` (distance-based vertex merge). The
replacement `SimplifyPath` is Ramer-Douglas-Peucker -- a different
algorithm, not a rename.

### Z-coordinates are not the differentiator

Clipper1 has `use_xyz`: this fork's copy has it commented out at
`clipper.hpp:44`, upstream **uncommented it** at line 45. Upstream's
adaptive rest-machining Z-tracking runs on the Clipper1 side. Clipper2
merely makes it a supported build mode (`CLIPPER2_USINGZ`).

libarea does not need Z: neither `CArea` nor ifcopenshell uses it, so
vendor Clipper2 **without** `USINGZ`. If it is ever enabled it must be a
PUBLIC compile definition on the `area` target, because `USINGZ` changes
`Point64`'s layout and a consumer compiled without it would disagree
about the ABI.

> **This was overtaken by the adaptive port.** Upstream's reworked adaptive
> clearing tags its input points with an index and reads the index back off
> the clip results, on both Clipper sides -- so libarea 0.3.0 turns on
> `use_xyz` and, as foreseen above, `USINGZ` as a PUBLIC definition, and
> bumps SOVERSION to 2. `CArea` pays eight bytes a point for something it
> does not use. Upstream avoids that by building a second Clipper2 with
> `USINGZ` and linking only that into the adaptive code; that does not work
> here, where `CArea` is on Clipper2 as well, because the two builds share
> the `Clipper2Lib` namespace and one process would hold both under one set
> of symbols. IfcOpenShell has to be rebuilt against it.

### Three upstream defects -- do not import these

1. **`Simplify` is dead code upstream.** `m_clipper_simple` is declared
   (`Area.h:59`), defined (`Area.cpp:19`), exposed as a FreeCAD/Python
   parameter (`AreaParams.h:125`), and set to `True` by live code at
   `PathScripts/PathUtils.py:440` -- and read by nothing. The
   `StrictlySimple` calls went away with no replacement.
2. **`CleanDistance` silently changed meaning.** `AreaClipper.cpp:371`
   now calls `SimplifyPath(p, m_clipper_clean_distance, is_closed)`.
   Same property name, same stored value in existing documents, different
   geometry out.
3. **The enum parameters read back as the wrong value.** This one changes
   toolpaths. `subject_fill`, `clip_fill`, `join_type` and `end_type` are
   declared `enum2`, which is an `App::PropertyEnumeration` -- it stores a
   *presentation index*. Upstream's migration writes the qualified Clipper2
   values into the parameter list and casts that index straight to the
   enumerator, but Clipper2 numbers its enumerators in a different order
   than the list presents them:

   | list index | shown | Clipper2 enumerator at that number |
   |---|---|---|
   | 0 | NonZero | `FillRule::EvenOdd` |
   | 0 | Round | `JoinType::Square` |
   | 0 | OpenRound | `EndType::Polygon` |

   Measured on a built tree with upstream's scheme in place: a fresh
   `Path::FeatureArea` came up with `SubjectFill` reading **EvenOdd** where
   the default says NonZero, `JoinType` reading **Miter** where it says
   Round, and `EndType` reading **Butt** where it says OpenRound. The
   property editor also offered `"Clipper2Lib::FillRule::NonZero"` as the
   label, because the list is stringified to build the enumeration.

   The fix here is `src/Mod/Area/App/ClipperEnums.h`: give each Clipper2
   value a flat, paste-friendly alias so the original `PARAM_ENUM_CONVERT`
   machinery -- which converts index to value through a generated switch,
   and is correct by construction -- keeps working. The list order is what
   documents persist, so it does not change:

   | index | JoinType | EndType | FillRule |
   |---|---|---|---|
   | 0 | Round | OpenRound -> `Round` | NonZero |
   | 1 | Square | ClosedPolygon -> `Polygon` | EvenOdd |
   | 2 | Miter | ClosedLine -> `Joined` | Positive |
   | 3 | -- | OpenSquare -> `Square` | Negative |
   | 4 | -- | OpenButt -> `Butt` | -- |

   Clipper2's new `JoinType::Bevel` is deliberately not offered: adding it
   anywhere but the end would renumber what documents already hold.

## Phase 3 blast radius

`~/works/sw/libarea` (external package):
- `AreaClipper.cpp` -- the migration target
- `Area.h` -- public API signatures
- `CMakeLists.txt` -- vendor Clipper2, keep Clipper1, bump SOVERSION

fcad `src/Mod/Area`:
- `App/Area.cpp` -- 6 ClipperLib references
- `App/AreaParams.h` -- 8
- `PyArea/PythonStuff.cpp` -- 1
- `PyArea/Adaptive.cpp` / `Adaptive.hpp` -- 14, **not migrated** (stays Clipper1)

`~/works/sw/ifcopenshell`:
- `src/ifcgeom/kernels/opencascade/boolean_utils_2d.cpp` -- drives Clipper
  directly (PolyTree, ClosedPathsFromPolyTree, ctDifference, pftNonZero)

## Packaging consequences

The libarea public API changes, so:
- **SOVERSION 0 -> 1.** Anything linked against the old soname needs a
  reconfigure, not just a rebuild -- the imported target's path is read at
  CMake configure time.
- `realthunder/libarea-feedstock` (branch `master`, rattler-build
  `recipe.yaml`) builds from the git tag; it needs a version bump.
- libarea installs into the conda env prefix, **not** a sibling install
  dir, so that FreeCAD's Area module and ifcopenshell share one copy.

## PathSimulator/AppGL

Upstream added `PathSimulator/AppGL`, 7,815 lines of hand-written desktop
OpenGL with its own `Dummy3DViewer`, `OpenGlWrapper`, `GlUtils` and
`DlgCAMSimulator`, added unconditionally in `PathSimulator/CMakeLists.txt`.
That runs against this project's renderer direction (no hard-coded
desktop-GL assumptions). It is a self-contained subdirectory: put it
behind a CMake option defaulting off, and revisit it as renderer work,
not as part of this port.

## Other new dependencies

`lazy_loader` (35 uses in upstream CAM Python) is not in the conda env.
It needs installing, plus a `freecad-rt-feedstock` change for releases.

## Ledger

**Phases 3, 1 and 2 are done.** What is left of the gate is not Area work;
see "Where phase 2 stands" below.

| date | phase | repo | commit | what |
|---|---|---|---|---|
| 2026-08-23 | -- | fcad | `fbf4fded22` | this document |
| 2026-08-23 | 3 | libarea | `3f02bed` | vendor Clipper2 beside Clipper1, SOVERSION 1 |
| 2026-08-23 | 3 | libarea | `b8ae05b` | move CArea onto Clipper2 |
| 2026-08-23 | 3 | libarea | `f9d09f7` | golden-value test for the clipping operations |
| 2026-08-23 | 3 | fcad | `73fbf0c250` | Mod/Area follows, with ClipperEnums.h |
| 2026-08-23 | -- | fcad | `67a64914fb` | correct the enum-index claim above |
| 2026-08-23 | 3 | ifcopenshell | `f1d7b3130` | the 2D subtraction follows |
| 2026-08-23 | 3 | fcad | `15074c1fed` | say what Simplify and CleanDistance now do |
| 2026-08-23 | 3 | libarea-feedstock | `658974d` | libarea 0.2.0 |
| 2026-08-23 | 3 | fcad | `e5c59ec710` | profile test was pinning Clipper1's rounding |
| 2026-08-23 | 1 | fcad | `d4d6c2f4bb` | take upstream's CAM wholesale, at 11bee82d6c |
| 2026-08-23 | 1 | fcad | `14ed3e19a3` | wire the module into this fork's build |
| 2026-08-23 | 1 | fcad | `c202b3a337` | adapt the ported tree to this fork's core |
| 2026-08-23 | 1 | fcad | `712e7f7f0d` | restore this fork's own CAM work on it |
| 2026-08-23 | 1 | fcad | `9ddfcd71e8` | expose ParameterGrp::RenameGrp to Python |
| 2026-08-23 | 1 | fcad | `5b308e9506` | let Python call libarea Subtract and Union |
| 2026-08-23 | 1 | fcad | `121b01661f` | run the ported tree against this fork's runtime |
| 2026-08-23 | 2 | fcad | `e00436d5ee` | Part: OCCT bug 1330 wrapper for ShapeAnalysis_FreeBounds |
| 2026-08-23 | 2 | fcad | `eec88438e8` | Part: CrossSection accepts the tolerance its own cut adds |
| 2026-08-23 | 2 | fcad | `065eb8defa` | Area: slice sections from the right side of the plane |
| 2026-08-23 | 2 | fcad | `912e40dbec` | Area: no edge split outside its own parameter range |
| 2026-08-23 | 2 | fcad | `a7d0212889` | Area: rest machining measures what the tool can reach |
| 2026-08-23 | 2 | fcad | `f784942bc7` | Area: narrow a stepover that would leave material |
| 2026-08-23 | 2 | fcad | `fba9f6b5d5` | App: let a dynamic property be renamed |
| 2026-08-24 | 2 | fcad | `72682ea528` | revert `28c2fe5f54`, restore stays as it was |
| 2026-08-24 | 2 | fcad | `34ae7cdf40` | CAM: the slicer tests ask for their recompute |

### What was verified

- libarea's own suite: 14 cases, all pass. Ten of the eleven operations
  compared bit-identical against the Clipper1 build; the arc case moved
  closer to the exact answer.
- The feedstock's consumer test (the arc round trip, which is the whole
  point of the library) builds and passes against the installed 0.2.0.
- fcad builds clean, all targets.
- The four Area enum properties still read NonZero, NonZero, Round,
  OpenRound with their original labels; every index round trips through
  the Python parameters; an outward offset gives round corners at
  JoinType index 0 and mitred at index 2.
- IfcOpenShell's OpenCASCADE geometry kernel compiles and links.
- `TestPathApp`: 458 tests, 0 failures. The 40 errors are pre-existing
  and unrelated -- 38 are `NameError: pythonopen` in the post-processor
  scripts, 2 are `TestPathOpUtil` hitting a null shape out of
  `makEWires`. Both sets fail identically without this work.

### What phase 1 turned out to need

Beyond the recipe: the module is `src/Mod/CAM` now, so `BUILD_PATH` became
`BUILD_CAM` everywhere; CAM's `tsp_solver` forced `FREECAD_USE_PYBIND11` on,
which in turn switched `Mod/Area/PyArea` from its boost-python wrapper to
`pyarea.cpp` and exposed that `Subtract` and `Union` were uncallable from
Python there; `PathSimulator/AppGL` went behind `BUILD_CAM_SIMULATOR_GL`
(off), which it needed anyway since it wants a `Gui/MDIViewWithCamera.h` this
fork does not have; and three upstream-only core APIs had to be met --
`ParameterGrp.RenameGroup` (bound, the C++ was already here),
`Base::TimeTracker` (one debug line, dropped) and
`FreeCAD.ApplicationDirectories` (versioned config directories, which this
fork does not have, so the two callers fall back to the single user
directory).

`getClearedArea` moved to the CAM side as `Path::clearedAreaFromPath`, the
same call `Area::toPath` had already made: upstream made it a static on
`Area` taking a `Toolpath*`, and the area engine here deliberately knows
nothing about toolpaths. `Mod/Area` gained no new public API for it.

### Where phase 1 stands

`TestCAMApp`: **1343 tests, 17 failures, 9 errors, 44 skipped, 5 expected
failures**, all 26 in two groups, neither of them loose ends of the port:

1. **Adaptive clearing (15).** Upstream's `Adaptive2d.Execute` takes a fourth
   `clearedArea` argument for rest machining; this fork's `Adaptive.cpp` is
   the older three-argument one, which is the piece this plan already parked
   ("migrating adaptive clearing is its own piece of work"). Five error out
   on the signature, ten fail on the path it consequently does not produce.
2. **Geometry off by one lattice step (11).** `TestPathProfile` test01 wants
   `X23.54` and gets `X23.55`, the pocket tests get zero loops where they
   want one, and `TestSlicer` gets 3 Z depths where it wants 4. The area
   parameters are not the cause -- accuracy and clipper scale have the same
   defaults on both sides. This is upstream's op code running against an
   `Area.cpp` that has not had upstream's +1,422/-726 applied to it yet,
   slicer fixes included. That is exactly phase 2.

Neither group blocks the module: it builds, imports, and 1317 of its tests
pass.

Two things worth knowing about running the suite:

- Redirecting its stdout to a file makes it die partway with `OSError: [Errno
  9] Bad file descriptor` -- a Sanity postprocessor test closes the
  descriptor unittest is writing to. Give it a tty (`script -qec ...
  /dev/null`) and the whole suite runs.
- A stale `Mod/Path` directory left in a build tree from before the rename
  shadows the new `Mod/CAM/Path` package, and the module then fails to import
  with a misleading `No module named 'Path.Tool.assets'`. Delete it.

### Still open

- Pushed (2026-08-23): **libarea** (with the annotated tag `v0.2.0` the
  recipe builds from), **libarea-feedstock**, and **IfcOpenShell**. Only
  **fcad** is still committed-but-unpushed.
- The dev environment was migrated with it: IfcOpenShell was rebuilt and
  reinstalled into the conda prefix so it links `libarea.so.1`, and
  `libarea.so.0` was then removed. Leaving both sonames in place is the
  two-Clippers-in-one-process hazard the library split exists to prevent
  -- `CArea`'s tolerances are static mutable state both consumers write.
- **`boolean_subtraction_2d_using_area` has not been shown to run.** The
  symbol contract is proven exact (the kernel's undefined
  `CurveTo/FromClipperPath` carry `Clipper2Lib::Point<long>` and
  `libarea.so.1` defines those), the kernel loads, and a synthetic wall
  with an opening builds geometry -- but nothing confirmed that branch
  was taken rather than the 3D kernel fallback.

  **Narrowed on 2026-09-07, on Windows with the fork's packaged build 13.**
  What is now settled is that the path is *built and linked*:
  `dumpbin /imports` on `ifcopenshell_geometry_kernel_opencascade.dll` shows
  it importing `area.dll` and pulling `CArea::m_accuracy`, `m_units`,
  `m_clipper_simple`, `m_clipper_clean_distance`, `m_fit_arcs` and
  `m_fit_circles` from it, alongside `CArea::append`, `CCurve::Offset` and
  the Clipper2 entry points. That answers "is it wired up". It does not
  answer "is it taken".

  **Two things not to mistake for evidence, both tried here.** First, the
  Python surface is a *setting*, not a function -- `boolean-attempt-2d-area`
  (default True) and `boolean-area-2d-fit-circles`, both fork-only. Testing
  `hasattr(ifcopenshell.geom, 'boolean_subtraction_2d_using_area')` returns
  False on a build that has the capability, so that check answers the wrong
  question. Second, **toggling the setting proves nothing on a model the
  path does not reach.** A 4.0 x 3.0 x 0.2 wall with a rectangular opening
  gives identical geometry and identical time either way (16 verts / 32
  faces; medians 0.0039 s to 0.0043 s over twelve interleaved iterations
  each). Measure it single-shot and the first call's warm-up shows as a
  10x difference that is not real.

  **What the logger does show.** With `turn_on_detailed_logging()` and
  verbosity at `LOG_NOTICE`, five cases -- interior hole, notch flush with
  the far end, notch flush with the bottom, notches at both ends, and nine
  flush operands at once -- all report `GEO136`/`GEO137` (operands are
  extrusions), `GEO123` (intersecting boundaries) and `GEO140` **Processed
  fully in 2D**, and none of them emits any diagnostic attributable to the
  libarea branch. So the 2D specialisation is entered every time and
  handles the work; the deliberate attempts to make the 2D *builder* fail
  and fall through to the area path did not succeed.

  **Which log codes actually discriminate.** `GEO140` "Processed fully in
  2D" does **not**: `boolean_utils.cpp:1304` fires it whenever the 2D
  specialisation left nothing for the 3D kernel, whichever of the builder or
  the area path produced each face. It is the code you see in the good case
  and it tells you nothing about which path ran. The ones that do are
  `GEO404`, `GEO405`, `GEO406` and `GEO410` -- so it is the *absence* of
  those alongside `GEO140` that shows the builder won.

  **What "depth" means, since it is not what it sounds like.** `GEO157`
  ("Processing N operands as M slabs") is guarded by `cuts.size() > 2` at
  `boolean_utils.cpp:1140`, and `cuts` (1115-1127) collects only operand
  interval endpoints that fall *strictly inside* A's own extrusion interval.
  An opening that passes clean through the wall therefore contributes no
  cut at all, however many there are and wherever they sit in elevation:
  twelve through-openings at twelve different heights still give
  `cuts.size() == 2`, one 2D problem, and no `GEO157`. The slab
  decomposition exists for **blind pockets** -- the reveals and wall sweeps
  the comment at 1099-1112 names, which stop short of the far face.
  Confirmed here: through-openings never emit `GEO157`; pockets cut to a
  fraction of the wall thickness immediately do ("Processing 1 operands as
  2 slabs"). `GEO156` caps the whole thing at `max_slabs = 64`.

  **And the slab path still is not the area path.** With `GEO157` firing,
  the result is still `GEO140` with no `GEO404`/`405`/`406`/`410`, so the
  builder handles the slabs too. Every route tried so far -- interior hole,
  notches flush with the boundary, nine flush operands, twelve elevations,
  and blind pockets at two and three depths -- ends the same way.

  Closing this properly now needs the branch instrumented, or a model
  chosen from the code rather than guessed at. Note the log-capture trap:
  `logger.set_output()` wants C++ streams and rejects Python objects, and
  `FMT_INMEMORY` + `get_log()` silently returns nothing -- a self-test
  (emit a `notice()`, read it back) is worth doing before trusting a
  silent log, because an empty log otherwise reads as "the branch did not
  run" when it means "nothing was captured".
- ~~`ifcopenshell-feedstock` still builds the **upstream 0.8.0 tarball**,
  not the fork branch, and names no `libarea` dependency.~~ **Done
  (2026-09-07).** It is rewired onto the `LinkVibe` branch and floors
  `libarea >=0.3.2`; win-64 is published at 0.9.0alpha0 build 13 for
  py311 through py314, and osx-64 is still failing on a `create_shape`
  segfault through the alignment API. Three fixes were needed to build it
  on Windows at all: the bare `friend class iterator;` in
  `src/ifcgeom/element.h` that MSVC binds to `std::iterator` and rejects
  (C2990, fixed in `dc04c32b2`); six `LNK2019`s against libarea's static
  data members, which `WINDOWS_EXPORT_ALL_SYMBOLS` does not carry across
  and which libarea 0.3.2 fixes by annotating them `LIBAREA_DATA`; and a
  stream-offset bug in the mmap file reader (`01c11aa9a`, `3b0c07269`).
- `Simplify` is now inert (Clipper2 has no StrictlySimple). It is kept
  and says so; removing it is a separate decision about document
  compatibility.
- Clipper1 is still built inside libarea because
  `Mod/Area/PyArea/Adaptive.cpp` drives it directly. Migrating adaptive
  clearing is its own piece of work, and upstream has not done theirs
  either.

### Where phase 2 stands

`TestCAMApp`: **1343 tests, 12 failures, 5 errors**, down from 17 and 9. Of
the eleven non-adaptive problems phase 2 set out to clear, **nine are gone**
and the fifteen adaptive ones are untouched, as planned.

Cleared: the four `TestPathPocket` zero-loop cases, the four `TestPathHelix`
errors, and `TestSlicer.test_17748_cam_profile`. Only the first four were
geometry -- the rest were the two core gaps below.

What upstream's Area delta actually came to, once clang-format was normalised
away on both sides: **+400 / -281**, not the +1,422 / -726 the raw file diff
shows. Most of the rest was the Clipper2 migration phase 3 had already done,
`FC_TIME_*` instrumentation upstream deleted and this fork keeps, and the
`enum2` change the plan says not to adopt. Normalising with `clang-format`
before diffing is the trick that makes this reviewable; do it again next time.

Ported: the slicer plane-normal fix, the `CrossSection` face-tolerance fix,
both OCCT bug 1330 wrappers (new `Part/App/ShapeAnalysis_FreeBoundsFix.*`),
the WireJoiner edge-split clamp, the `getRestArea` rework, and `makeOffset`
gap detection with the new `ForceMaxStepover` parameter.

**Deliberately not ported: upstream's `FuzzyHelper` / `FCBRepAlgoAPI_*`.**
Upstream applies a global boolean fuzz by default across Part and CAM wraps
its slicing in `withBooleanFuzzy(0.0, ...)` to switch it *off*. This fork has
no such global -- its booleans take a fuzz per call and default to none -- so
the wrapper would set zero where zero already holds. The half that does
matter, letting the plane test allow for the tolerance the cut introduces, is
in `CrossSection` using plain `mkCut.FuzzyValue()`, which stays correct if
that subsystem ever arrives.

`LastStepover` and `PocketLastStepover` are gone with upstream's rewrite. Old
documents carrying either will report an unknown property on load; nothing in
the tree reads them any more.

### The two core-API gaps phase 1 did not find

Neither is Area work, and between them they hid the state of the gate.

1. **`renameProperty` did not exist here** (fixed, `fba9f6b5d5`). Fifteen
   call sites across CAM, BIM and Fem rename a property when restoring an
   older document, and every one of them threw `AttributeError` partway
   through its migration. For the CAM pockets that meant `ZigZagAngle` never
   became `Angle` and the operation could not execute at all. Upstream's
   transaction support and `signalRenameDynamicProperty` are not ported: this
   fork's property editor renames a property *group*, not a property, so the
   only callers are restore-time migrations.

2. **`obj.recompute()` does not recompute an object that is up to date.**
   Not a defect, and nothing is wrong with the object -- but it is why
   `TestSlicer.test_17748_cam_profile` read 3 Z depths: the assertion was
   measuring the toolpath **stored in the file**, because the operation
   never ran. Settled by having the test ask for the recompute
   (`34ae7cdf40`); both slicer tests now pass on what the operations
   actually produce, with the optimisation on.

   Compared against upstream at `11bee82d6c`, function by function:

   | | upstream | this fork |
   |---|---|---|
   | `afterRestore` purge | purges unless in `touchedObjs` | same |
   | object state after restore | `Up-to-date` | same |
   | walk gate | `if (obj->mustRecompute())` | same |
   | dependents in the walk | `enforceRecompute()` | `Enforce`+`Touch` status bits only |
   | `_recomputeFeature` | recomputes unconditionally | skips unless error, `_enforceRecompute`, restoring, or a touched non-output property |
   | `doc.recompute()` on the fixture | skips it | skips it |
   | `obj.recompute()` on the fixture | recomputes it | skips it |

   So the divergence is **one guard in `_recomputeFeature`**, and nothing
   else. Both sides agree the object is up to date; upstream's explicit
   single-object entry point simply does not ask.

   The guard is load-bearing where it was written. The walk deliberately
   marks dependents with status bits rather than calling
   `enforceRecompute()` -- there is a comment saying so, "in order to enable
   recomputation optimization (see `_recomputeFeature()`)" -- and the guard
   is the second filter that then drops the dependents whose inputs did not
   actually change. Upstream's answer to the same problem is its
   `fineGrained` per-property path. `Document::recomputeFeature()`, the
   explicit "recompute this one object" entry, reaches `_recomputeFeature`
   with no `mustRecompute()` gate in front of it and so inherits that
   filter as well.

   **Nothing is wrong with the object.** Its inputs are unchanged; only the
   code that turns them into a toolpath was fixed, and FreeCAD tracks no
   such staleness. The test was written against upstream's unconditional
   explicit recompute, so the test is what had to say what it meant.

   The fork's behaviour is left exactly as it was, deliberately. Two things
   were tried first and both were wrong -- do not revisit either:
   - Making `Document::recomputeFeature()` enforce. Force belongs opt-in,
     and `obj.enforceRecompute()` is already there for it.
   - Keeping the migration's touch alive through restore (`28c2fe5f54`,
     reverted in `72682ea528`). That makes recompute-after-restore
     implicit: any touch in `onDocumentRestored`, benign ones included,
     would mark the object and, through `addRecomputeObject`, the document
     with it, so opening an old file would recompute it. Recompute after
     restore must be asked for -- `Document::addRecomputeObject` is the API
     for a migration that knows it invalidated the result.

   ! The trap the tests fell into is worth remembering on its own: **a test
   that opens a fixture, calls `obj.recompute()` and asserts on the result
   is reading what was saved in the file.** Only these two did it; the other
   CAM tests that open a fixture recompute the document over objects they
   have just changed.

### The two geometry failures were one binding bug

Both were `Path.ClipperJoinType*`, exported by phase 1 as Clipper2's own
enumerator values (`AppPathPy.cpp`). The only reader is Profile, which puts
them in an Area call's `JoinType` -- a `PropertyEnumeration`, whose value is
the position in the list `AREA_PARAMS_OFFSET_CONF` declares, Round, Square,
Miter. Clipper2 numbers its four Square, Bevel, Round, Miter. So Round asked
for Miter, Square asked for Round, and Miter asked for a fourth entry that is
not in the list.

Upstream writes the qualified Clipper values into the parameter list itself,
which makes the stored index the enumerator's own number and the two agree.
This fork keeps the names in the list and converts (`ClipperEnums.h` says
why), so the constants have to be the position: `Area::JoinType*`, out of the
same list. Fixed in `d57800311b`.

- `TestPathProfile.test01` profiled a rounded rectangle with a miter join.
  The corner radius came out 13.495 instead of 13.5 and the tangent points
  0.05 off -- which read as "off by one lattice step" and sent the first look
  at it into the Area geometry, where nothing was wrong.
- `TestPathOpenProfile.test02` profiles two open edges and wants the round
  join between them. A miter join gave it a sharp corner: two moves where it
  expects three.

The probe that found it swapped the op's `areaOpAreaParams` for the one the
pre-port fork used, one difference at a time. Worth reusing: the failing
values came back exactly, from Python, with no rebuild.

### Adaptive clearing: upstream's rework, taken wholesale

`Adaptive.cpp` and `Adaptive.hpp` here were upstream's, unmodified -- the
fork has only ever moved them. Upstream has since rewritten most of the
algorithm, and the fourth `clearedArea` argument was only the visible edge
of it: rest machining from a cleared area handed in, a helix ramp that
searches between a target and a minimum diameter, link paths found rather
than assumed, finishing passes only over what is not already clear, and an
area total plus seven warning flags on every output. The CAM workbench taken
in phase 1 is the caller of all of that.

So both files were taken wholesale again at `11bee82d6c` (`890aafb36c`),
with one edit -- the include line, which reads the two Clippers out of the
libarea package. The bindings in `pyarea.cpp` grew the fourth argument, the
two helix diameters, and the output's `ClearedArea`, `clipperScale` and
warning flags. Nothing else needed adapting: the file has no FreeCAD
dependencies, and with the two Z switches defined it compiled unchanged.

The dependency work is the real cost, and it is written up under
"Z-coordinates are not the differentiator" above: libarea 0.3.0, SOVERSION
2, IfcOpenShell rebuilt. `src/Mod/Area/CMakeLists.txt` asks for 0.3.0 by
version, because an older one does not merely lack a feature -- it disagrees
about the layout of a point.

### Where the port stands

**`TestCAMApp` is green: 1343 tests, no failures, no errors** (44 skipped, 5
expected failures). It was 17 failures and 9 errors after phase 1, then 12
and 5, then 10 and 5 once the join type was fixed -- every one of those 15
adaptive.

`Mod/Area`'s public surface still did not grow. `getClearedArea` left it
altogether -- upstream's replacement takes a `Toolpath*`, and phase 1 had
already put that on the CAM side as `Path::clearedAreaFromPath`.

Run the suite under a tty (`script -qec ... /dev/null`) or it dies partway.

Still open, all of it listed under "Still open" further up: the ifcopenshell
feedstock, the 2D subtraction never having been shown to run, and `Simplify`
being inert.
