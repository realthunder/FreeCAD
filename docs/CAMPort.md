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

**Phase 3 is done.** Phases 1 and 2 have not started; phase 1 is the
next piece of work.

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
  was taken rather than the 3D kernel fallback. Closing it needs a real
  IFC model with openings (none on this box) or a direct link-test
  against the kernel.
- `ifcopenshell-feedstock` still builds the **upstream 0.8.0 tarball**,
  not the fork branch, and names no `libarea` dependency -- so the fork's
  2D area path is in no released package. Rewiring it (source -> the
  `LinkVibe` branch, add `libarea >=0.2.0`) is a separate job.
- `Simplify` is now inert (Clipper2 has no StrictlySimple). It is kept
  and says so; removing it is a separate decision about document
  compatibility.
- Clipper1 is still built inside libarea because
  `Mod/Area/PyArea/Adaptive.cpp` drives it directly. Migrating adaptive
  clearing is its own piece of work, and upstream has not done theirs
  either.

### Next

**Phase 1: port CAM** (user, 2026-08-23: start it next session). Nothing
in it depends on a Clipper decision any more. The recipe is in the Phase 1
section above; the FEM port (`f3bb79474b` and the eight commits after it)
is the worked example, and `TestCAMApp` is the green gate.
