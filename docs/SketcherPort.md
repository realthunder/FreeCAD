# Sketcher: picking upstream fixes and features

Status (2026-09-18): phases 0 and 1 done; phase 2 (App) done -- the
standalone fixes, the internal faces fix, the trim/split take, the fillet,
the external faces (with element history through `Part::HLRProjector`),
the symmetric and projected-circle picks, and the Gui-coupled App features
(fillet on a crossed vertex, carbon copy of a scripted sketch, scale keeps
geometry ids, sketch autoscale) are in (section 6a). Of the listed features,
datums projection and defining externals were already here. The remaining
open App rows were marked n/a in bulk (section 6a).

**The groups and Text feature family is done** (section 6b): geometry
groups, the group command, group dragging, the Text tool and copy/paste of
groups.

**Phase 3 (Gui) is under way** (section 7). `Gui/ToolHandler` is in, on
the user's ruling of 2026-09-18 that reverses decision 4's deferral of the
hints framework -- the core of it was already in the fork and only that
one class was missing. `DrawSketchHandler.{h,cpp}` is the keystone the
tool handler files need, and its group B (the auto-constraint search) is
in with the three fixes that ride on it, and so is the snap mask and the
deferred `SnapHandle` -- which was the last thing standing between the
port and the ~30 tool handler files. The auto-constraint family now has a
GUI harness that drives a tool through a served mirror, which is the only
way to get real preselection under test -- and building it turned up a
defect of its own: **every Sketcher tool misplaced every point a browser
put down anywhere but the middle of the canvas**, fixed in `8a4b30bc83`
on the user's ruling to take it before continuing.

Upstream's `f4665aa7b5` ("Core: support multiple active transactions") was
evaluated and **declined**; `docs/TransactionLog.md` records why, and the
direction the user wants instead.

**Where the ledger stands (2026-09-25).** 1149 rows, of which 278 are open
and undecided, down from 503 over three sessions of reading blobs rather
than commits. First the 33 files the handler resyncs touched: 21 are
identical to upstream's tip modulo whitespace, closing 74 rows at once
(section 7, "The origin marker"). Then the same idea over the whole tree,
per row instead of per file -- is every line a commit added already in the
fork's file, and every line it removed already gone -- which closed 55
more and turned up five picks -- a whole feature the fork was missing
and two live crashes -- and put a size on the two families left open
(section 7, "The sweep over every file" onwards). One of those two
families, the constraint-tool hints, has since been taken whole --
thirteen rows, two of which the per-file sweep could not see (section
7a). Of what is left, `Gui/ViewProviderSketch.cpp` carries the most, and
the `EditMode*` family is n/a by decision 3.

**Where the ledger stands (2026-10-03, session 122).** No row is undecided.
The last 53 were read family by family and ruled on with the user
("The ledger, session 122", before section 8): 24 were here already or do
not apply, 9 declined, 20 taken or adapted. What is left of the port is not
in the ledger: the rows marked `deferred` (upstream's Sketcher GUI test
files, whose ground the fork's `tests/gui` covers) and whatever upstream
has added after `bd6be559e8`.

Branch `SketcherPort` off `RemoteEdit`
`b7dbdd191d`. Upstream reference: `upstream/main` `bd6be559e8`
(2026-09-12).

The ledger is [SketcherPort-ledger.tsv](./SketcherPort-ledger.tsv): one row per
upstream commit, with a `decision` column filled in as picks land.

## 1. Decisions (user, 2026-09-15)

1. Work on branch `SketcherPort`.
2. **Split `SketchObject.cpp` along upstream's five-file boundaries**, as a
   move-only commit before any App pick, so upstream hunks land in files of
   the same name.
3. **Do not adopt `EditModeCoinManager`.** The fork keeps its monolithic
   `ViewProviderSketch.cpp`; its in-edit highlight is planned to move to the
   mode-3 render-time highlight, and the served/browser editing tier depends
   on the current structure. Upstream rendering and preselection fixes that
   target `EditMode*` files are adapted by hand where the bug exists here, and
   marked n/a otherwise.
4. **Take every small and medium feature.** The large ones -- the Text tool
   (new geometry type, fonts, persistence) and the context-aware hints
   framework (needs upstream's core input-hint machinery) -- are deferred and
   decided separately.
   **Superseded for the Text tool** by the ruling of 2026-09-17: take the
   groups and Text family whole (section 6b). The hints framework stays
   deferred.

## 2. How far apart the two sides are

- Merge base `a662fbb2ff` (2023-12-31).
- `ca04ac80a1` (2025-03-23, "Sketcher: sync with upstream") is a **squashed,
  partial** sync of 38 files. Because it was squashed, `git cherry` finds no
  upstream commit as already applied, and the baseline has to be decided per
  file:

  | files | upstream state they match |
  |---|---|
  | `DrawSketchHandlerTranslate.h`, `Scale.h`, `Symmetry.h` | `2c485cf998` (2025-02-23), blob-identical |
  | `DrawSketchController.h` | `7412fce041` (2025-02-14), blob-identical at the sync |
  | `DrawSketchDefaultWidgetController.h`, `Utils.*`, `Offset.h`, `Polygon.h` | ~2024-11-15 |
  | other `DrawSketchHandler*.h` | ~2025-02-11 |
  | `CommandConstraints.cpp`, `CommandSketcherTools.cpp` | ~2024-11-25 .. 2024-12-06 |
  | `ViewProviderSketch.*`, `Workbench.cpp` | partially, ~2025-02/03 |
  | `PythonConverter.cpp` | `d621f59a88` (2025-01-24) |
  | `SketchObject.*` | partially, ~2024-10/11 |
  | everything else, **including `planegcs/`** | the merge base (2023-12) |

  "~" rows are nearest matches by diff size, not blob identity.
- Upstream since the merge base: ~1150 non-merge commits touch Sketcher code.
- `planegcs/`: the fork changed 2 lines since the merge base; upstream has 53
  commits. It is close to a clean take.
- Structural drift upstream: `SketchObject.cpp` split into five files
  (2026-03-12, `7d39a9d59a` `e1815f10ff` `eda87a7efe` `8efe9964e7`
  `79aca8950f`); `.xml` bindings replaced by `.pyi` (2025-03/04); whole-tree
  reformat `25c3ba7338` (2025-11-11); the `EditMode*` Coin managers are
  compiled upstream and commented out of the fork's `Gui/CMakeLists.txt`.
- `057d51f846` ("Merge pull request #29904") shows as +144k lines in a
  numstat walk but its Sketcher diff against both parents is empty. Ignore it.

## 3. The ledger

Generated by walking `git log --no-merges --full-history
a662fbb2ff..upstream/main -- src/Mod/Sketcher tests/src/Mod/Sketcher` and
dropping commits that touch only translations. Columns:

- `kind`: a guess from the subject line (fix / feature / refactor / noise / ?).
  It is a sorting aid, not a verdict.
- `fwd` / `rev`: whether the commit's patch `git apply --check`s forward or in
  reverse against the branch at ledger creation.
- `status`:
  - `have(rev)` -- the patch reverse-applies, so the change is already here;
  - `have(sync)` -- every file it touches is one of the 38 synced files and
    the commit predates that file's baseline;
  - `partial(sync)` -- some of its files are covered by the sync;
  - `n/a(uncompiled)` -- it touches only sources the fork does not build
    (`EditMode*.cpp`, `ViewProviderSketchCoinAttorney.cpp`);
  - `open` -- to be triaged.
- `decision`: `taken <hash>`, `adapted <hash>`, `have`, `n/a`, `superseded`,
  `declined <why>`, `deferred`.

At creation: 834 open (by kind: 290 fix, 166 feature, 105 refactor, 56 noise,
217 unclassified), 152 have(sync), 23 have(rev), 74 partial(sync),
65 n/a(uncompiled). 138 applied cleanly forward.

Commit messages for picks: `Sketcher: <summary> (upstream <hash>)`.

## 4. The SketchObject.cpp split

Move-only. Each top-level item of the fork's file went to the file upstream
keeps it in. Fork-only functions went next to their nearest upstream
relative:

| function | file | reason |
|---|---|---|
| `getInternalElementMap` | `SketchObject.cpp` | element naming lives there |
| `getCoincidenceGroups`, `getAllCoincidentPoints` | `SketchObjectConstraints.cpp` | upstream's place |
| `setConstraintExpression`, `reverseAngleConstraintExpression` | `SketchObjectConstraints.cpp` | constraint expressions |
| `movePoint` | `SketchObjectOperations.cpp` | upstream's `moveGeometries` |
| `transferFilletConstraints`, `simplifyBSpline` | `SketchObjectOperations.cpp` | fillet, B-spline operations |
| `toggleConstructions` | `SketchObjectGeometry.cpp` | upstream's `toggleConstruction` |
| `toggleFreeze`, `toggleIntersection`, `detachExternal`, `checkSubNames` | `SketchObjectExternal.cpp` | external geometry |

File-local helpers each have exactly one user file, so no shared header was
needed: the projection helpers, `fitArcs` and `checkOutList` went to
`SketchObjectExternal.cpp`; `GeoHistory`, `hasSketchMarker`, `checkMigration`
and `SketchExport` stay in `SketchObject.cpp`.

Each new file carries the full original include preamble; trimming includes
is a separate later commit, the way upstream's `[5/5]` was. The moved code is
wrapped in `// clang-format off` / `on` (Sketcher is in the pre-commit
allowlist). Files stay CRLF. The generator asserted that the non-blank lines
of the five files are exactly the original's.

## 5. Tests

The fork has 3 Python Sketcher test files and ~300 lines of C++ tests;
upstream has 15 Python test files and ~3500 lines of C++ tests. Porting
upstream's tests first gives a net, and a failing test points at a missing
fix.

- **Python, App-level: ported** (`TestSketcherSolver` 10 -> 24 tests,
  `TestSketchFillet`, `TestSketchExpression`, `TestSketchValidateCoincidents`,
  `TestSketchCarbonCopyReverseMapping`, `TestSketchInternalFaces`,
  `TestSketcherEllipse`; 94 tests). Upstream's versions replace the fork's
  three files whole -- the fork had not changed them since the merge base.
  The first run was 13 failures and 16 errors. Two were fork defects, fixed
  in their own commits:
  - `SketchObject.moveGeometry` did not exist. Upstream renamed `movePoint`;
    the fork now has both spellings. This alone was failing `testBoxCase`,
    `testSlotCase` and `testBlockConstraintEllipse`, which the fork's own
    copies had covered.
  - `addExternal("Box", "Edge1")` never worked. The object form was parsed
    first with `"O|O"`, which accepts any two arguments, so the documented
    string form was unreachable; the dead branch also passed two pointers for
    one `O`. The string form now comes first and also takes upstream's
    trailing `intersection` bool.

  A third defect surfaced only in the full suite, and is not Sketcher's:
  `Application::newDocument()` renamed a new document's *label* whenever any
  other document was open (`Base::Tools::getUniqueName()` always returns a
  new name; the label path never checked for a clash first). Upstream's
  `TestSketcherEllipse` leaves its documents open, so CAM's
  `TestFileNameGenerator`, which runs later and finds its document by label,
  failed nine cases. Fixed in `App`, with
  `DocumentBasicCases.testNewDocumentLabelOnlyRenamedOnClash`.

  The `addExternal` fix alone made five more pass (`testProjectEllipse3`, `5`,
  `7`, and both orientation-migration tests -- those two only because the
  fork has no signed constraints yet; they stay as guards for phase 1). The
  remaining 21 are marked `@unittest.expectedFailure` with a
  `# Pending upstream <hash>` comment. The marker comes off when that pick
  lands:

  | upstream | what | tests |
  |---|---|---|
  | `82ec32f9e9` | MissingVerticalHorizontal false positive | 1 |
  | `9d7073ce7b` | geometry extension on a point | 1 |
  | `6d06b61c7e` | fillet keeps the corner by splitting (**behaviour change**: unconnected lines no longer fillet) | 3 in `TestSketchFillet` |
  | `0939408c21` | internal faces of self-intersecting B-splines | 3 in `TestSketchInternalFaces` |
  | `e06290557d` | ellipse projection | 4 in `TestSketcherEllipse` |
  | `176ef6da4e` | Carbon Copy reverse mapping, `setAllowUnaligned` | 1 |
  | 2024-05 validate/degenerate cases | `detectDegeneratedGeometries`, `evaluateConstraints` bindings; delete constraints to external | 3 in `TestSketchValidateCoincidents` |
- **C++: ported with the trim/split take** (`9b75379eb1`): upstream's
  `tests/src/Mod/Sketcher` is a superset of the fork's 18 tests, 107 tests.
  99 pass; 8 were `DISABLED_` with a `// Pending upstream <hash>` comment, the
  C++ counterpart of the Python markers (see "Trim, split and internal
  geometry" in 6a). As of 2026-09-17 one is left
  (`testReverseAngleConstraintToSupplementaryExpressionFunction`, the phase 3
  angle cluster); the six symmetric ones came on with `addSymmetric` below.
- **Python, Gui** (`TestOnViewParameterGui`, preselection, constraint
  commands): written against `EditModeCoinManager` behaviour; evaluate one by
  one in phase 3.

## 6. Phase 1: the solver and constraint model

Taken whole from upstream `bd6be559e8`: `planegcs/`, `Sketch.cpp/.h`,
`Constraint.cpp/.h`, `ConstraintPyImp.cpp`, `GeoEnum.cpp/.h`. A take rather
than picks, because the fork had changed these files by a few lines since the
merge base while upstream has 53 solver commits.

**Why the constraint model comes too.** Upstream's `Sketch.cpp` needs a
per-constraint `Orientation` (signed distances and tangent sides) and the
`Group`/`Text` constraint types. File compatibility holds: the fork's
`ConstraintType` and `InternalAlignmentType` are strict prefixes of upstream's
(upstream appended `Group = 20`, `Text = 21`), and the attributes upstream
writes that the fork did not -- `ElementIds`, `ElementPositions`, `IsVisible`,
`MetaData`, `Orientation` -- are all optional on restore. The legacy
`First`/`FirstPos`/... members stay authoritative for element indices 0-2
(`SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS`), so the fork's direct writes to
them cannot desynchronise `elements`.

**Old files.** A constraint restored without `Orientation` is "side unknown",
and the solver derives the side from the current geometry: point-line uses the
point's signed distance, circle-line the centre's signed distance and radius,
tangent the centre's side, circle-circle stays undecided. That is the old
unsigned behaviour. Filling orientations in on restore
(`SketchObject::migrateConstraintOrientations`, upstream `e1e72c9195`) lives in
`SketchObject` and landed in phase 2 (section 6a). Upstream's handling of stored
negative circle-line distances (`c968effe26`) is solver code and came with this
take; its test came with the phase-0 test port.

**Compile probe.** Taking the files and building the `Sketcher` target left 11
distinct errors, all at fork call sites:

| error | adaptation |
|---|---|
| `initMove(geo, pos, fine)`, `initBSplinePieceMove(..., fine)` | upstream removed the dead `fine`; the fork's `SketchObject` keeps its parameter and stops passing it on |
| `Sketch::movePoint` gone | `moveGeometry()`, in `SketchObject.h`, `SketchObjectOperations.cpp`, `SketchPyImp.cpp` |
| `GCS::SolveStatus` is an `enum class` | `static_cast<int>` where `SketchObject`, `SketchPy` and `CommandConstraints` read it; the values (0-3) did not change, so `SketchObject`'s `int` results and Python are unchanged. Upstream's `SketchSolveStatus` is a later pick (`d8cf415e4f`) |
| `Base::unreachable` missing | added to `Base/Tools.h`, as upstream has it (own commit) |
| `ConstraintPy::getLabelDistance/getLabelPosition` undeclared | read-only `LabelDistance`, `LabelPosition` in `ConstraintPy.xml` |

**Verified.** Build OK; ctest 667/667; Python 2851 OK. The take alone made
four of the five signed-distance tests pass (their markers came off in the
same commit), leaving 17 expected failures in the ported Sketcher tests.
The nine `.FCStd` files in the repository that contain sketches, all saved
before the take, were recorded as re-solving with no sketch moving
(tolerance 1e-6) and no solve failing. That record does not reproduce; see
"Old-file check, corrected" in section 6a.

Fork changes re-applied on the taken files: `THROWM` at the four throw sites in
`Sketch.cpp` (`b3694d32b6`), `GeoElementId::operator<` instead of the
`std::less` specialisation (`1054a3cae2`). Already upstream: the
`boost/random.hpp` include (`76e2047902`), the `h_norm` initialisers
(`ebe4fd9eb5`).

## 6a. Phase 2: App picks

### Signed constraint orientations (`0f5a36bfc8`)

The `SketchObject` half of `3c8a254356` and `e1e72c9195`, plus the tangent
orientation that reached upstream through merge #29904, ported from upstream's
tip rather than replayed commit by commit:

- `setOrientation()` runs in `addConstraint`, `addConstraints`, `setDriving`,
  `setActive` and `toggleActive`;
- `restoreFinished()` calls `migrateConstraintOrientations()` after the
  external geometry is rebuilt or accepted;
- `addSymmetric` re-derives the side of a copied two-element constraint;
- `rebuildExternalGeometry` re-derives constraints on a projected line that
  came back reversed.

`testCircleToCircleDistanceOriented` passes and its marker is gone.

**Fork difference.** Upstream edits the owned constraints in place and then
calls `Constraints.setValues()`. The fork's `Property::hasSetValue()` compares
against a snapshot taken inside that call, which already carries an in-place
edit, so the write would be dropped as a no-op (`docs/UpstreamCoreSync.md`
5.5). The migration and the reversal pass set clones of only the constraints
that change, and a sketch with nothing to migrate is not written to.

As upstream, every Tangent gets an orientation, endpoint-to-endpoint ones
included; the solver reads it only for edge-to-edge tangency.

**Verified.** Build OK; ctest 667/667; Python 2851 OK (22 expected failures);
`TestSketcherApp` 94 OK (16 expected failures).

### Old-file check, corrected

The phase-1 record in section 6 does not reproduce. The check script stopped
at the first file: `ArchDetail.FCStd` `Sketch003` has 13 constraints and no
geometry, and `solve()` throws from `Sketch::checkGeoId`. Rerun with the script
catching each sketch, the phase-1 library and the orientation pick show the
same three problems, none of them a regression:

- `ArchDetail.FCStd` `Sketch002` moves 278 mm on any solve. Its stored geometry
  disagrees with its stored external geometry: `DistanceX` 0 ties an edge at
  x = -30 to an external line at x = -268. It has no Distance or Tangent
  constraint, so no orientation is involved.
- `ArchDetail.FCStd` `Sketch003` throws in `solve()`.
- `CAMTests/Drilling_1.FCStd` `Sketch` solves when opened alone, and returns -1
  after some numbers of unrelated solves in the same process. After N solves
  of a trivial sketch, the phase-1 library fails for N = 3, 4, 7-10 and 12, the
  orientation pick for N = 9, 10 and 12. The solve result depends on process
  history. The likely cause is the planegcs containers keyed by pointer
  (`p2c`, `c2p` in `GCS.h`), whose order follows heap addresses; not yet traced.
  A multi-document check therefore cannot pin one solve status on the change
  under test -- rerun the sketch alone and sweep N on both libraries.

### Solve results as an enum (`b4d1b55b6c`, upstream `d8cf415e4f`)

`SketchObject::solve`, `setDatum`, `movePoint`, `trim` and `extend` return
`SketchSolveStatus` (declared in `SketchAnalysis.h`) instead of `int`; the
enum keeps the old integers, 0 and -1 to -6, so Python sees the same numbers.
`lastSolverStatus`, `getLastSolverStatus()` and `moveTemporaryPoint()` carry
`GCS::SolveStatus`, which the phase-1 take had made an `enum class` and the
fork had been casting to `int`. `SketchAnalysis::solvesketch` and `execute()`
read the enum.

Adapted rather than applied: the fork still has `movePoint` and
`moveTemporaryPoint` where upstream has `moveGeometries` and
`moveGeometriesTemporary`, and its pre-refactor `trim`. `extend()` on an
unsupported geometry type still reports an error, as the fork's `-1` did
(upstream's later initialiser makes it `Success`).

**Upstream defect, not taken.** At upstream `bd6be559e8`,
`ViewProviderSketch::onDelete` and `SketchObject::setTextAndFont` test
`status == SketchSolveStatus::Success` where the code before `d8cf415e4f`
tested for failure, so the "solve failed" branch runs on success. The fork's
`onDelete` ignores the result and the fork has no Text constraint.

**Verified.** Build OK; ctest 667/667; Python 2851 OK (22 expected failures);
`TestSketcherApp` 94 OK (16 expected failures).

### The dead `fine` parameter (`cd7ff82e17`, upstream `a502762119`)

`initTemporaryMove` and `initTemporaryBSplinePieceMove` lose the parameter the
solver has ignored since `796c9d79d4`; the phase-1 take had already removed it
from `Sketch`. `ViewProviderSketch` stops passing `false`. Upstream's if-ladder
to `switch` rewrites in `setDatum` and `setTextAndFont` are not taken.

**Verified.** Build OK; ctest 667/667; Python 2851 OK (22 expected failures);
`TestSketcherApp` 94 OK (16 expected failures).

### Standalone App fixes (`c53ba6a814` .. `619abdfac4`)

The open App rows of kind `fix` were triaged by script first
(`git show -U0` per commit; each non-trivial added or removed line looked up,
whitespace-insensitive, in the fork's file, `SketchObject.cpp` meaning all five
split files), then by reading every diff against the fork's code. Nineteen
picks, one commit each:

| upstream | fork | what |
|---|---|---|
| `ab5b53e972` | `c53ba6a814` | `fitArcs` middle parameter initialised |
| `cc802c341d` | `4e6df1319a` | throw, not assert, on an ExternalGeometry size mismatch |
| `8ea5075385` | `e4352d774d` | PythonConverter: DistanceX/Y on one vertex keeps its position |
| `c4317f88f8` | `3bfb748012` | `removeAxesAlignment` read past its index list |
| `fc82d71c15` | `7b2b094b38` | mirrored arcs with symmetry constraints no longer come out redundant |
| `884192e005` | `bec74e1472` | `port_reversedExternalArcs` leak when only analysing |
| 5 Coverity moves | `36a52b0ac2` | move instead of copy |
| `e06290557d` | `f7c3d7bff4` | ellipse projecting to a circle divided by zero (4 markers off) |
| `82ec32f9e9` | `798e88bfd5` | missing vertical/horizontal detection skips active constraints (1 marker off) |
| `d0b98703c0` | `ae323bf5a9` | a projected edge collapsing to a line keeps one y (adapted, below) |
| `5b43180899` | `30b9838f3c` | recompute uses the configured default solver |
| `d2637ec881` | `e8315e9a39` | shape built from the solved geometry |
| `27dd14174e` | `814e0c8f30` | a defining external point is a vertex of the shape |
| `a49d106807` | `329342b6bc` | a circle's seam vertex resolves outside edit mode |
| `c3805ecf4a` | `708be44c03` | module-specific precompiled-header guards |
| `176ef6da4e` | `92e5004b51` | Carbon Copy from a flipped sketch mirrors the copy (adapted, below; 1 marker off) |
| `2032a9d844` | `8550ffdc64` | PythonConverter writes B-spline knots, multiplicities and weights |
| `d9910ce9bc` | `9ffed788d9` | PythonConverter writes geometry with 8 decimals |
| `9d7073ce7b` | `9b9c4d4a3a` | Part: a point's Python object keeps its extensions (1 marker off) |
| `52935f8249` | `619abdfac4` | Python bindings for degenerate geometry and constraint validation (3 markers off) |

`2032a9d844` was not in the fix list: the fork's converter wrote a B-spline as
`Part.BSplineCurve(poles, None, None, periodic, degree, None, False)`, dropping
knots, multiplicities and weights, and turned up while adapting `d9910ce9bc`.
Its upstream history is behind the grafted root `057d51f846`, so pickaxe
searches past July 2026 find only that commit; the ledger's subjects found it.

**Verified**, detached, on the combined tree: build OK; ctest 667/667; Python
2851 OK (50 skipped, 12 expected failures, down from 22); `TestSketcherApp` 94
OK (6 expected failures, down from 16). After the first 14 picks the same run
gave 17 and 11. The old-file check shows only the known cases (section 6a,
"Old-file check, corrected"): `CAMTests/Drilling_1.FCStd` `Sketch` came out
moved by 0.0045 in the multi-file run where the previous run had -1, and alone
it solves; a sweep of N unrelated solves before it fails at N = 1, 3 and 9 --
history dependence, which the phase-1 baseline library showed too (moved
0.00178 in one run).

**Two upstream defects not carried over.**

- `d0b98703c0` averages the two y values of a line measured from a bounding
  box *after* rotating the points back into the sketch frame. That flattens
  every such line whose plane is not aligned with the sketch axes. The fork
  averages in the rotated frame, where the line runs along x. Checked: a
  circle in a vertical plane turned 30 deg projects to a line along its trace
  (cross product 5.6e-17), of length 10, centred on the circle.
- `176ef6da4e` mirrors a carbon-copied geometry about the sketch Placement's
  position and rotated axes, but the geometry is in sketch coordinates. For a
  flipped sketch offset along its normal (a sketch on the underside of a pad)
  that lifts the copy off the sketch plane: offset 10 puts it at local z = 20.
  Upstream's test fixture has every Placement at the origin. The fork mirrors
  about the local origin and axes. Checked: a flipped sketch at z = 10 gets the
  copy at local z = 0, matching the source in global XY, still solving. The
  fork also copies a source constraint's own expression when it does not
  depend on the source sketch; that path gets the same sign correction.

**Also checked by probe** (not covered by any suite): a defining external
vertex gives one shape vertex and a clean recompute; `;g1v1;SKT.Vertex1` on a
circle resolves; a rational non-uniform B-spline round-trips through
`toPythonCommands` with identical knots, multiplicities and weights and poles
within 3e-9.

**Decided without a pick** (ledger `decision` column has the reason):
already here -- `3f79626799` `a36a60d29d` `885b8cf1de` `331e51cdfc`
`1c67ab7be2` `13e7952ccc` `28b62eb52b`; not applicable to the fork's code --
`858e59fabf` `cf8ad66373` `760091bc8a` `46a11b6538` `1881686e72`
`3321b13218` `6ddb0165ff` `4c8fadd68d` `e89d849bb7` `70d11e33dc`
`78e2a12d2d` `358942b771` `832a0653fa` `cf93c33359`; superseded by
`6456f287c4` -- `cebcb7f66c` `f2e57b4eb4`.

Declined:

- `a9942be046` removes `if (!Geometry.isSame(tmp))` from `solve()`. That check
  is the fork's own (`db62f72188`, to avoid touching the sketch while editing);
  `isSame` compares within `Precision::Confusion()` plus the extensions, and
  upstream gave no failing case.
- `db8c90b788` formats the partially-redundant warning with the sketch name.
  The notifier already names the sketch, and a formatted string no longer
  matches its translation.
- `0fbb300fe4` (comment removal), `6adebe348e` (`std::move` of a returned
  value).

**Still open on the App side.**

- ~~**Face selection** for external geometry (`1c514f5a15`, `74aafcee75`)~~:
  taken 2026-09-17, see "External faces" below.
- `83d14b785e`: the App crash path is not in the fork's `delExternalPrivate`
  (it sets the values once); the Gui half and the `delExternals` binding are
  phase 3.
- The `SketchAnalysis` refactor series of 2024-05-28.

### External faces (`1c514f5a15`, `74aafcee75`)

The fork's `rebuildExternalGeometry` refused a face parallel or oblique to the
sketch and any non-planar face, and turned a perpendicular planar face into a
line clamped at 10000 either way. Now, as upstream: a planar face projects
each of its edges through the edge path; a perpendicular one collapses those
projections into one segment spanning them, with the old line only when
nothing straight came out (an `App::Plane` has no finite edges); a non-planar
face goes through an HLR projection along the sketch normal (`projectShape`,
ported) and its edges through the converter the edge path already had, split
out as `importProjected`. `74aafcee75`: a reference whose geometries are all
defining keeps the flag on geometries a rebuild adds to it; a reference being
added still takes the caller's flag, which upstream's version overwrites.

**Behaviour gate.** The perpendicular segment moves the endpoints of a line
existing sketches were built against, so `SketchObject` now has a hidden
`_Version` (integer, 0 when absent from the file, 1 for a new sketch): a
sketch restored without it keeps the long line. Decided by the user
2026-09-17, on the `SubShapeBinder::_Version` pattern.

Tests: `testAddExternalIncreasesCount` enabled (the parallel top face of a box
gives four edges), plus the perpendicular face as one segment, the same on a
version-0 sketch as the long line, and a cylinder side face projecting its
circle.

**Element history through the projection (2026-09-17).** `HLRBRep_HLRToShape`
returns bare compounds, so the face path had only the output order to hand
out external ids by, and the flatten/dedupe pass above can change that order:
a constraint on external id N silently moved to another curve. The
projection now runs through `Part::HLRProjector` (`TopoShape::makEHLR`,
`Part.Shape.makeHLR` in Python): the same OCCT algorithm with HLRToShape's
traversal replayed so the source of every edge is kept, an input edge for an
ordinary projected edge and the face for a silhouette the algorithm invented
(the outliner rebuilds faces as empty copies, so the silhouette-to-face link
is read from `HLRTopoBRep_Data`, keyed by the original face). The result is
named through `makESHAPE` with op code `HLR`, a fragment index telling apart
the pieces hiding cuts one source into. The Sketcher stores that name per
external geometry (`ExternalGeometryExtension::RefElement`, saved only when
set) and keys the id on it: a named geometry takes the id of its namesake in
the reference; unnamed ones, and named ones with no namesake (the first
rebuild of a sketch saved before the names), take the ids of the unnamed
geometries in order, the positional rule these always had; ids nothing claims
are deleted. A reference without an element map (an `App::Plane`) stays
positional. Seams are still left out, as upstream does.

**The edge path names too (2026-09-17, follow-up).** A planar face, a wire
and a plain edge reference go through `importEdge`, which used to take a
bare `TopoDS_Shape` and name nothing. Each edge now comes as the sub-shape
of the reference with its element map (`importNamedEdge`): the geometries
it projects to are named after its mapped name, with a `;<k>` suffix when
the normal projection breaks one edge into several pieces, so a notch cut
into a box's top face reorders the face's edges without moving the ids of
the ones that survive. The one segment a perpendicular planar face collapses
into is nobody's edge and stays unnamed, positional; so does the output of
the intersection option (a section has no map) and any reference without a
map (an `App::Plane`). The tests are `TestSketchExternalGeometry`.

TechDraw's `GeometryObject::projectShape` runs on the same class as of the
same day (`docs/TopoNamingEnhance.md` 8.5.2); its private copy of the exact
traversal is gone, the polygon one stays.

### Internal faces: WireJoiner kept (`aa31511fbd`)

Upstream replaced WireJoiner + `FaceMakerRing` in `buildInternals()` with a
new `Part::FaceMakerBuildFace` (`24ab301685`: BOPAlgo splits the edges and
builds the faces), then fixed that for self-intersecting B-splines and
dangling edges (`0939408c21`). Decision (user, 2026-09-15): keep WireJoiner
and make it pass upstream's `TestSketchInternalFaces`. It already passed
every overlap case the new face maker was written for (three and four
overlapping circles, crosses, T-junctions, dangling chains); only the three
self-intersecting B-spline tests failed. The figure-8 came out as one face of
143.2 instead of two of 71.6.

The figure-8 is interpolated from its crossing, so it starts and ends there,
and two places dropped a crossing on the edge's own end point:

- `checkSelfIntersection()` called
  `ShapeAnalysis_Wire::CheckSelfIntersectingEdge()`, which ignores any
  crossing within vertex tolerance of the edge's vertices. It now runs
  `Geom2dInt_GInter` on the pcurve itself, as that function does, and keeps
  every crossing whose parameter is not an end.
- `splitEdges()` dropped the first and last split parameter whenever its point
  was within tolerance of the end point, which erased the crossing again.

Both now ask `isEndParam()`: the point is at the end point *and* the curve
between the two is shorter than twice the tolerance. A loop back to the end
point is far longer. An open B-spline passing through its own start point hit
the same two defects and got no internal shape at all;
`testBSplineLoopThroughStartPoint` covers it (a fork test in the upstream
file).

Naming does not shift. A/B of the old and new `Part.so` over the 37 sketches
in the repo's `.FCStd` files, each sketch's geometry copied into a fresh
document without constraints and `MakeInternals` on: face, edge and wire
counts, areas and the whole `InternalShape` element map are identical once
the string hasher ids are masked. Copying the geometry matters: re-solving the
files themselves made Drilling_1's `Sketch` differ between the two libraries,
and a dummy-solve sweep showed that is the solver's history dependence (see
"Old-file check, corrected"), on both libraries alike. `24ab301685` is
declined, `0939408c21` adapted.

**Verified.** Full build OK; ctest 667/667; Python 2852 OK (50 skipped, 9
expected failures, down from 12); `TestSketcherApp` 95 OK (3 expected
failures, down from 6; the 3 left are the fillet markers).

### Trim, split and internal geometry: taken whole (`d8d462426e`)

Upstream restructured `trim`, `split`, `join`, the B-spline knot operations,
`transferConstraints`, `delConstraintOnPoint` and the internal geometry
operations (Ajinkya Dahale, 2024-11 .. 2026), fixed them on top, and moved the
deletion API to `DeleteOptions` (`eab485656f`). The fork had none of it, so the
fixes did not apply as picks. Decision (user, 2026-09-15): take the cluster
whole, as the solver was in phase 1.

**What the fork stood to lose.** A body-by-body comparison of the merge base,
the fork and upstream's tip showed the fork's own changes inside these
functions were few and already upstream's: geometry ids (`generateId` on
trim's middle piece -- now `replaceGeometries`, which copies the old id to the
first piece and generates the rest; `copyId` in the knot operations), a
negative GeoId in `delGeometry` forwarding to `delExternal`, `SketchSolveStatus`
results. Upstream's `generateId` is the fork's with its `goto`s turned into
lambdas.

**Taken** from `upstream/main` `bd6be559e8`, by a script that replaces the
fork's definitions with upstream's regions in the same split files
(scratchpad `probe_take.py`):

| file | functions |
|---|---|
| `SketchObjectOperations.cpp` | `delGeometries` (with the iterator template), `replaceGeometries`, `extend`, `seekTrimPoints` + the trim helpers, `trim`, `split`, `join`, `modifyBSplineKnotMultiplicity`, `insertBSplineKnot` |
| `SketchObjectGeometry.cpp` | `isClosedCurve`, `hasInternalGeometry`, `delGeometry`, `delGeometriesExclusiveList`, `deleteAllGeometry`, `exposeInternalGeometry` with its per-type specialisations and `addAndCleanup`, the `deleteUnusedInternalGeometry` family |
| `SketchObjectConstraints.cpp` | `deleteAllConstraints`, `delConstraint(s)`, `delConstraintOnPoint`, `transferConstraints`, `getConstraintAfterDeletingGeo`, `changeConstraintAfterDeletingGeo`, `deriveConstraintsForPieces`, `getDirectlyCoincidentPoints`, `autoRemoveRedundants` |

**Adapted.**

- `delConstraintsToExternal` keeps the fork's body: it removes only
  constraints to *linked* external geometry, where upstream's removes
  constraints to any. It gains the options and solves unless `NoSolve`, like
  `delConstraints`.
- `delExternal` accepts a GeoId as well as an external index, as upstream's
  does. The taken `delGeometries` passes negative GeoIds on, and with the
  fork's index-only version a selection holding an external geometry failed
  to delete (upstream's C++ `testDelExternalReducesCount`; a Python probe of
  `delGeometries([0, -3])` deletes both).
- `moveGeometry` forwards to `movePoint` for `extend`; upstream's rename (and
  `moveGeometries`) is its own pick.
- Python: `delGeometry`, `delGeometries`, `deleteAllGeometry` and
  `delConstraint` take an optional `noSolve`, `trim` an optional
  `includeAxes`, `join` an optional `continuity`. **Upstream defect not
  copied:** upstream's `delGeometry` binding passes the solve option alone,
  which drops `IncludeInternalGeometry` from the default, so deleting an
  ellipse from Python no longer deletes its foci. The fork keeps it.
  `delGeometry`'s name form stays; its cog block was edited with the template
  call and `cogapp --check` is clean.
- `SketchAnalysis` and the fork's `transferFilletConstraints` passed `false`
  (solve without updating the geometry); that is `DeleteOption::NoFlag`
  under the new guard, which is also what upstream's `SketchAnalysis` passes.
- The Gui trimming preview passes `includeSketchAxes = false`; upstream's
  "include axes" tool widget option is phase 3.
- `getConstraintIndices` is `const`, for the `const`
  `getDirectlyCoincidentPoints`.

Left out, still open: the `generateId`, `addExternal`, `buildShape`,
`delAllExternal` and `toggleExternalGeometryFlag` "WIP refactor" rows, the
`addSymmetric` rows, `getPointForGeometry`, and the Gui rows (trimming
handler, `DrawSketchDefaultHandler`, `TaskSketcherElements`). The ledger marks
the cluster: rows wholly inside the taken functions `taken`, rows partly
inside `partial` with the rest named, test-only rows `taken` by the tests
commit.

**Tests** (`9b75379eb1`): upstream's C++ tests, 107, replace the fork's 18
(a strict subset). `getElementName` returns `std::pair` in the fork (first the
new style name), so one test reads `first`/`second`. 8 were `DISABLED_`
pending picks that were not taken: 6 `addSymmetric` tests (`e1a431d5ee`
`14280cdbf7` `bc3c0dc19a` `451072f0d7` `28f5e823d3`), the supplementary
angle of a function expression (`8b06bca68a` builds it as an AST and keeps
the unit), and a face parallel to the sketch as external geometry
(`1c514f5a15` `74aafcee75`, since taken, 7 remain). Committed with `NO_STRIP_NONASCII=1`: the degree
signs are unit strings the tests compare against.

**Verified.** Full build OK; ctest 748/748 (+81 from upstream's C++ tests, 16
entries disabled, 8 of them the pending upstream tests); Python 2852 OK (50 skipped,
9 expected failures, unchanged); `TestSketcherApp` 95 OK (3 expected
failures, unchanged). `Sketcher_tests_run` 99 passed, 8 disabled.

### Fillet: the corner kept by splitting (`c2924c2fa4`, upstream `6d06b61c7e`, adapted)

Upstream's `fillet` with `createCorner` used to leave a point at the old
corner and move the corner's constraints onto it
(`transferFilletConstraints`). `6d06b61c7e` splits each curve at its tangent
point instead, turns the piece towards the corner into construction, and lets
`split` carry the constraints over. Taken from upstream's tip:
`chooseFilletsEdges`, both `fillet` overloads, the `cornerPoint` output of
`Part::createFilletGeometry` and `PropertyGeometryList::swapValues`;
`transferFilletConstraints` is gone. The fork's `createFilletGeometry` was
upstream's already.

**Curves that do not meet** (decided with the user, 2026-09-15). Upstream now
refuses to fillet two curves without a coincident point. The fork keeps
filleting them, and keeps their corner too, per curve:

- a curve that reaches its tangent point is split there, as for a connected
  corner -- this covers crossing curves;
- a line that stops short of it is extended to the tangent point, and a
  construction line from there to where the curves would meet stands in for
  the missing piece, joined by a tangent constraint. The constraints on the
  moving end, and length or equality constraints on the line, are removed, as
  upstream did for unconnected curves;
- any other curve that stops short gets only that removal: a trimmed curve's
  `closestParameter` clamps to its range, so the missing piece has no safe
  parameters.

`testUnconnected` holds the fork expectation instead of upstream's
`ValueError`.

**Upstream defects not taken**, both found by probing, not by reading:

- *The curves did not keep their ids.* After the split, upstream's swap left
  the construction corner pieces at the curves' indices, and for a corner at a
  curve's start also with that curve's geometry id (probe: a V of `g1`, `g2`
  came out with `g2` on a construction piece and the kept piece as `g5`), so
  references to the curve landed on construction geometry. `split()` already
  leaves the kept piece first when the corner is at the curve's end; with the
  corner at the start the two pieces are now swapped with their geometry ids
  -- from clones, since a write equal to the value before it does not notify.
  Upstream's own comment says the curve should keep its id.
- *A chamfer flipped.* Upstream gave the chamfer line the fillet arc's end
  positions in its coincidences, so the next solve flipped the line, and the
  whole corner with it, whenever the arc was not reversed. The line's own start
  and end are used, as the fork's chamfer did.

`testCreateCornerKeepsIds` and `testChamferCornerStaysPut` cover them. The
PartDesign TNP test whose volume upstream changed with this commit
(7400 -> 8533.33) has no copy in the fork.

**Verified.** Full build OK; ctest 748/748; Python 2854 OK (50 skipped, 6
expected failures, 3 fewer); `TestSketcherApp` 97 OK (+2 tests, no expected
failures left); `Sketcher_tests_run` 99 passed, 8 disabled.

### Symmetric copies with constraints (upstream `28f5e823d3`, taken whole)

Upstream's `addSymmetric` at its tip replaces the fork's (which had the
name-clearing, the arc perturbation and the signed-orientation reset
already). With the "add symmetric constraints" option it now copies only
the topological constraints (Coincident, and endpoint-to-endpoint Tangent
or Perpendicular, which it downgrades to Coincident since the symmetries
lock the angle), stitches a point that lies on the mirror line or point
with a Coincident instead of a singular Symmetric, and gives a shared
vertex one symmetry constraint through the coincidence groups instead of
one per edge. It also carries `e1a431d5ee` (element access through
`getElement`/`setElement`) and `14280cdbf7`. The six `DISABLED_` gtests
in `SketchObjectSymmetric.cpp` are on and pass (110/110); the Python
suite is unchanged (103 OK).

### A projected circle is full by its parameter range (upstream `d0c7ab7d62`, adapted)

Upstream's commit adds Bezier and Offset curves to `processEdge2`; the fork's
`importProjected` already converts any other curve type to a B-spline
(`GeomCurve::toBSpline`), and a probe with an offset arc, a Bezier and an
offset B-spline imports all three. What is taken is the other half: a circle
or ellipse is full when its parameter range spans its period, not when its
ends happen to lie within tolerance, so an arc of 360 deg minus a hair
projects as an arc. Applied at the three ellipse-projection sites of
`importEdge`; the plain circle site already had a length test.

### Gui-coupled App features (user ruling, 2026-09-17: take them all)

Six rows whose App half is useless without its Gui half. Taken one per
commit, the Gui half checked against served editing (a handler run
without a tool session touches no widget; the camera is left alone when
it is a client's, `ViewerContext::cameraIsRemote`).

- **Fillet on a crossed vertex** (`d55aa0f80b`, upstream `15d9e14851`):
  the App half, `chooseFilletsEdges`, came with the fillet take; the tool's
  two paths now run it, so a corner crossed by construction lines fillets.
- **Carbon copy of a scripted sketch** (`2b103def75`, upstream
  `74e7df9676`, adapted): `isDerivedFrom<SketchObject>()` at both sites in
  place of upstream's type-name string. Probed.
- **Scale keeps geometry ids** (`ea6ba1f918`, upstream `c8bddd2f2b`,
  adapted): `setGeometryIds` (Python: a list of `(GeoId, id)` pairs) and
  the Scale tool giving the scaled copies the originals' ids when it
  deletes them, so the fork's id-based element names follow the geometry.
  Upstream's helper re-cloned from the last match on every pair and leaked
  the earlier clones; the fork clones once and validates the indices first.
- **Sketch autoscale** (`60befe30bc`, upstream `353c4eca55` + `b0dcce6c66` +
  `29eeab3624`, with the later fixes `9ecb62c8f6`, `7c4131c4ef`,
  `6a0d59b0c1`, `918547f876` folded in, and the label offset rule of
  `4b84834112` moot since the fork has no label setter): setting the first
  scale-defining dimension of a sketch scales the whole sketch about its
  origin to match and the camera with it. Pieces: `NavigationStyle::scale`
  / `View3DInventorViewer::scale`; `getDatum`,
  `getSingleScaleDefiningConstraint` (angles, weights and refraction
  ratios excluded -- upstream excluded only angles), `hasBlockConstraint`;
  `DrawSketchHandlerScale::make_centerScale` and `DrawSketchHandler::
  setSketchGui` (a handler run without a tool session), the constraint
  remap written the robust way (`offsetGeoID` answers `GeoUndef` for a
  geometry outside the selection, and every branch requires its references)
  rather than the first version's; `SketcherGui::centerScale(vp, factor)`
  in `CommandSketcherTools.h` takes the sketch's own view provider and
  scales its edit viewer only when the camera is local; the
  `AutoScaleMode` preference (default: when no scale feature is visible)
  on the Sketcher General page; `EditDatumDialog::performAutoScale`
  resolves the sketch's own Gui document rather than the active one and
  requires the sketch to be in edit. Not autoscaled: a sketch with
  external geometry or a Block constraint, a datum that is not the single
  scale-defining one, a factor that is zero, infinite or one.
- **Datums projection** (upstream `f3643af82b`): n/a -- upstream added
  `Part::DatumLine`/`DatumPoint`, classes the fork does not have; its
  PartDesign datums are `Part::Datum` and project through `getShape()`
  already (probed: a datum line and point become a line and a point).
- **Defining externals** (upstream `f3c79302c4`): have -- the fork's own
  `addExternal(..., defining)`, `toggleConstruction` on an `ExternalEdge`
  and the Defining colour predate it.
- **Copy/paste of groups** (upstream `926e93c314`): deferred -- a
  converter refactor plus `isGroupHandle`/`getGroupGeometries`, which need
  the groups feature; it goes with the Text tool (section 1).

**A crash found by the probe, not by the feature** (`796e3085d9`): a
script that ends one edit, closes the document and enters another edit
before returning to the event loop crashed in `ConstraintItem::data`,
twice in two runs. The task dialog is deleted later than it is removed,
and a relayout in between (a workbench switch repolishes every child)
reads every constraint and element item, each dereferencing a sketch
that was gone. `TaskDlgEditSketch::closed()`, the synchronous hook of the
removal, now empties both lists. The sequence passes 2/2 afterwards, a
4x re-entry loop 3/3.

**Verified.** `TestSketcherApp` 103 OK; `Sketcher_tests_run` 110 passed;
`ctest` 759/759. Autoscale probed in a GUI session under Xvfb
(scratchpad `autoscale_probe.py`): the first DistanceX on a freehand
rectangle scales the geometry and the camera by the same factor
(3.33x), all ten constraints kept, fully constrained; Never, a visible
Box, external geometry and a zero datum leave the geometry to the solver.

### The App noise rows, marked in bulk

The 70 open App-only rows left after the picks were read once each by
subject and diff: six SketchAnalysis refactors, upstream's five-way split
of `SketchObject.cpp` (the fork did its own, section 4), the `.pyi`
bindings migration and its follow-ups (the fork keeps the `.xml`
bindings), the fillet refactor the fillet take superseded, thirty
refactor/cleanup commits, ten warning/typo/formatting commits, two
commits that came from the fork itself, two already here (the facade
warnings are commented out; the trim delete flag came with the trim take),
and one declined: the MakeInternals tooltip, since the fork's property
also splits edges and makes closed-wire faces and its text says so. No
open App-only row is undecided; phase 3 (Gui) is next.

### External projection: probed, nothing to take

`0aed23ca81` (#19582, a B-spline from another sketch could not be projected),
`0921ed2969` and `ec24bd8c21` (#19831, intersection with complex surfaces, and
a split B-spline edge on a parallel plane) fix code upstream had by then moved
into free `processEdge`/`projectShape` helpers; the fork keeps projection
inline in `rebuildExternalGeometry`, so the diffs do not apply. Probed instead,
against the fork (scratchpad `projprobe.py`, `projprobe2.py`):

| case | result |
|---|---|
| B-spline from another sketch, same plane and a parallel plane 7 up | a B-spline spanning x 0..15, as the source |
| circle onto a sketch tilted 40 deg | an ellipse, semi-minor 3.064 = 4 cos 40 |
| intersection of a sketch tilted 30 deg with a cylinder face | an ellipse, semi-axes 5 and 5.774 = 5 / cos 30 |
| intersection with a B-spline saddle surface | its four diagonal branches |
| an edge using the middle third of a B-spline's range, onto a parallel plane | only that third (x 6.67..13.33), not the whole curve |
| a planar B-spline seen edge-on by a perpendicular sketch | one line segment, x 0..20 |

All correct. The fork already moves a planar edge onto a parallel sketch plane
instead of projecting it -- the OCC failure `0921ed2969` works around -- and
adds a retry for OCC 7.7's projection. The three rows are marked `have`. The one
real gap found on the way is face selection, above.

## 6b. Phase 4: the groups and Text feature family

Taken on the user's ruling of 2026-09-17, which overrides the deferral in
section 1 item 4. Eleven commits, `915703db1c..67c4419033`.

### What a group is

A `Group` constraint binds a set of geometries to a construction line, the
first element of the constraint, which is the group's handle. The solver
leaves the members out of the system and disables every constraint on them;
after each solve it applies the handle's own translation, rotation and scale
to them, so the group moves as one rigid body and is positioned and sized by
constraining the handle. A `Text` constraint is a group whose members are the
curves of a rendered string, with the string, the font name and whether the
handle gives the height or the width carried in the constraint's metadata.

The whole solver side arrived with the phase-1 take: `Sketch.cpp` is
byte-identical to upstream for groups, including `captureGroupStates` and
`applyGroupTransformations`, and so is `Constraint.*` with its `Group` and
`Text` enum values and text accessors. `Part::makeTextWires` and
`Part::transformAndConvertToGeometry` were already here from the Part
geometry port. What was missing was everything above them.

### The picks

| commit | what |
|---|---|
| `915703db1c` | App: `isInGroup`, `isGroupHandle`, `getGroupHandleIfInGroup`, `getGroupGeometries`, and the multi-element move `moveGeometries` with its temporary-drag pair, in Python too |
| `9425073eda` | a group is drawn as a dashed box round its members |
| `f2968b34f2` | dragging moves every selected geometry, and a group through its handle |
| `23fdf17d92` | `Sketcher_ConstrainGroup`, and `addListConstraint`, shared with the Text tool |
| `3e275d5e8d` | a constraint on grouped geometry is shown inactive |
| `4aed4d4941` | the elements list shows a group by its handle and hides its members |
| `36dafcc2da` | a member's vertices carry no marker; its edge takes the handle's colour |
| `fa18b2a096` | a constraint with fewer than three elements can be saved |
| `5a085a3bd0` | App: `setTextAndFont`, with `TestSketcherText.py` |
| `5de405b45b` | the tool widget gains line edits |
| `cd3fa731c3` | the Text tool: handler, dialog, command, icons, panels |
| `67c4419033` | copy and paste of groups |

### Upstream defects not carried over

- `setTextAndFont` puts the old text and font back on the constraint when the
  solve **succeeds**, which reverts what the caller just set for a text that
  has no geometry yet. It also leaks the constraint it builds, since
  `addConstraint` clones, and `release()`s the geometry it generated although
  `addGeometry` copies.
- `Constraint::Save` asks `getElement` for indices 0 to 2 to write the legacy
  `First`/`Second`/`Third` attributes. A Group or Text constraint can hold
  fewer, and `getElement` throws. This is not only a failed save:
  `Property::isSameContent` serialises to compare, so the throw came out of
  `hasSetValue` on any write to the constraint list.
- The escape path of the drag refactor calls `commitDragMove` with the initial
  position, which for a non-relative drag is an absolute move to the origin.
  `cancelDragMove` here does a relative move of zero, as the old code did.
- `initDragging` tests the **preselected** geometry for internal alignment
  instead of the selected one, and drags a group's handle once per selected
  member.
- The clipboard renumbering walks the selection and rewrites element ids in
  place, so a geometry whose id equals another's new index is renumbered twice.

### Fork adaptations

- Upstream's `EditModeGeometryCoinConverter`, `EditModeGeometryCoinManager`
  and `EditModeConstraintCoinManager` are not compiled here (section 1 item
  3), so their three group changes went into the monolithic
  `ViewProviderSketch`: the dashed box in `rebuildConstraintsVisual` and
  `draw`, the member colouring in `updateColor`, and the hidden vertices as
  one marker index per point with `SoMarkerSet::NONE` for a member's, rather
  than leaving those points out of the Coin maps while building them.
- The elements panel is the fork's own `QTreeWidget` version, so upstream's
  diff does not apply at all; the group labelling, the hidden members and the
  "convert to geometry" action are written against it. It follows the view
  provider's constraints-changed signal as well, because the sketch object's
  elements-changed signal is not raised for constraints here, and it rebuilds
  only when the set of handles and members differs from what it was built for.
- The Text handler drops upstream's input-hint table, which needs core
  machinery the fork does not have, seeks and renders auto constraints in the
  fork's two steps, and uses `doChangeDrawSketchHandlerMode` and the `isSet`
  flag of an on-view parameter.
- The tool widget's new `WidgetLineEdits` template parameter sits where
  upstream puts it, so upstream's own handler changes go on applying; all
  fourteen handlers name it as `WidgetLineEdits<0, ...>`.
- Filter values `Group` and `Text` are inserted at 12 and 13 as upstream has
  them, which shifts the ones above `Block`. A saved multi-filter selection
  comes back shifted by two once.
- The Text tool takes no shortcut: "G, T" is Trim Edge's (upstream
  `99c2f19dfc`).

### Not part of this family

`19a082b63c` and `6d5800c862` say "groups" in their subjects but are about
**combined constraint icons**, several constraint icons drawn in one box, not
about sketch groups. `19a082b63c` rewrites `detectPreselectionConstr` to find
the picked icon among all the separator's children instead of assuming two,
and `6d5800c862` adds the bounds check that rewrite needs. The fork's own
`detectPreselectionConstr` already bounds-checks the second icon but keeps the
two-icon assumption, so the "more than two icons in one box picks the wrong
constraint" half is still open. Phase 3.

`83d91d61c6` and `69930bc58d` are about command groups in the toolbar, not
sketch groups. `c13ea2fa6d` fixes a typo in a menu string this fork words
differently.

### Verified

Build OK; ctest 762/762; Python 2883 OK (50 skipped, 6 expected failures);
`Sketcher_tests_run` 113; `TestSketcherApp` 109.

GUI probes under Xvfb, all passing: entering and leaving edit on a sketch
with a group; the group command over three edges, with the handle placed on
the bounding box and the members following the handle; the elements list
hiding the members and naming the handle "Group"; the marker indices going to
`-1` for exactly the four vertices of two grouped lines; the Text tool
activating with its text field and 49 fonts found; the Text constraint hidden
from the constraints list; copying a lone handle taking all three lines and
pasting them as a second group.

**Not covered by a probe:** the interactive drag itself. Synthetic mouse
events produce no preselection in this harness, and the sketch's edit scene
graph hangs on the viewer's aux root rather than the view provider, so a
drag cannot be driven from a script. The drag was exercised at the level
below it instead, through `moveGeometries` on the handle.

## 7. Phase 3: the Gui

Started 2026-09-18. Tools and commands first, as the phase list says.

### What the tool handler files actually are

The `DrawSketchHandler*.h` family looked like the hardest part of this
phase and is the easiest: the fork carries **almost no local adaptation
in them**. Diffed against upstream's tip, what the fork "has and upstream
does not" is nearly all *stale upstream* -- `#ifndef` guards where
upstream moved to `#pragma once`, the `geometryCreationMode` extern
upstream deleted, `Gui::Command::openCommand` where upstream added a
handler-level helper, `seekAutoConstraint` + `renderSuggestConstraintsCursor`
where upstream merged the pair into `seekAndRenderAutoConstraint`. The
fork-only API is small and lives in the base, not the handlers:
`allowExternalPick`, `allowExternalDocument`, `inSequence`, `toggle`.

So these files can be brought forward far more cheaply than commit by
commit -- but not yet. Upstream's tip handlers override `getToolHints()`,
and they take `mouseMove(SnapManager::SnapHandle)` rather than
`mouseMove(Base::Vector2d)`. Both are base-class questions, so
`DrawSketchHandler.{h,cpp}` is the keystone and comes first.

### The hints framework: the deferral was stale (user ruling, 2026-09-18)

Decision 4 deferred the context-aware hints because they "need upstream's
core input-hint machinery". That machinery is **already here**, ported for
the Python workbenches by `19bb5c42a8`: `src/Gui/InputHint.h` is
byte-identical to upstream, and `InputHintWidget`, `MainWindow::showHints`
/ `hideHints` and the Python binding all came with it. Nothing in C++ was
consuming it; the hint bar was reachable only from `MainWindowPy`.

The one missing piece was `Gui/ToolHandler`, upstream's extraction of the
cursor and activation lifecycle out of `DrawSketchHandler` -- which is
where `getToolHints()` lives. The fork's `DrawSketchHandler` already had
every member of it, one for one. Asked, the user ruled: **port it now**.

`cf85a2ce91` does that (`52ffab90e5`, adapted). The fork's own bodies
move across as they stand, so the view-less adaptations survive:

| upstream | here | why |
|---|---|---|
| `getViewer()` returns `View3DInventorViewer*` from the active window | `virtual`, returns `Gui::ViewerContext*` | a tool belongs to one edit session in one view; "which window is active" has no answer in a process serving several browsers (ThinClient 8.3). The base keeps the active-window lookup for a toolbar-started tool; `DrawSketchHandler` overrides it. |
| `activate()` fails when the view has no cursor widget | keys off the viewer | a client's mirror has no widget, and the cursor is chrome -- the DOM layer's (8.7) |
| `devicePixelRatio()` from the widget | from the `ViewerContext` | the client's, stated over the wire |
| `unsetCursor`/`applyCursor` protected | public on `DrawSketchHandler` | `ViewProviderSketch` restores the tool cursor after a preselection, and the selection gate when it changes |

`signalToolChanged()` moved into `preActivated()`, the first activation
hook, which keeps the order it had. `preActivated` is overridden only by
`DrawSketchHandler`, upstream and here, so nothing can skip it.

`GuiSketchEditRoot` already covered this end to end -- it activates the
line tool inside an edit session and reads the cursor off the 3D view,
which is a direct test that the handler found a view through the new base
rather than purging itself.

### Picks so far

- `372d84ca21` -- `~CurveConverter` no longer detaches from the parameter
  manager (`c14e735c20`, taken). Its only instance is the function-static
  in `drawEdit`, destroyed after `main()` returns, so the detach was
  undefined behaviour at exit. The fork had the bug exactly.
- `4c310bbc1d` -- the line mid-point auto-constraint (`64054d13c4` +
  `280452bcc8`, adapted). Three sites had to agree and none of them had
  it: `seekAutoConstraint` saying `Symmetric` near the middle,
  `suggestedConstraintsPixmaps` having an icon for it, and **both**
  consumers (`createAutoConstraints` and `DrawSketchDefaultHandler`)
  writing the three-element constraint. Upstream's restyling of the
  surrounding loops was not carried, so the diff is the feature.

### A harness for the auto-constraint family

An auto-constraint is only suggested for **preselected** geometry, and
synthetic Qt mouse events preselect nothing here
(the same wall the group drag hit in 6b). A served mirror's pointer does:
the client states a camera and a move at a computed pixel preselects in
that client's own mirror, which is what `seekAutoConstraint` reads.

`tests/gui/sketch-midpoint-autoconstraint.py`
(`GuiSketchMidpointAutoConstraint_tests_run`) is built on that and is the
harness the rest of the family needs -- seven more open rows touch
`seekAutoConstraint` alone (`596fa2856b`, `ed45e20768`, `f9f76a2516`,
`07de249ec7`, `387d25c219`, `b71a54d9cc`, `ec298e9e9a`). It draws a line
starting at the middle of an existing one and again a quarter along it,
so the middle is *discriminated* rather than always answered. Checked
against the removed code: without the pick the first click leaves
`PointOnObject` and the test fails on that line.

Two wire details it cost to learn: the key code on an `'E'` frame is an
**X11 keysym** (Escape is `0xff1b`), not a Qt key -- a Qt key overflows
the `u16` and the client thread dies inside `struct.pack`; and on-view
parameters have to be switched off
(`Mod/Sketcher/Tools`/`OnViewParameterVisibility` = 0) or an OVP takes
the click before the auto-constraint is ever consulted.

### The keystone, and what it decomposes into

`DrawSketchHandler.{h,cpp}` is the file every handler inherits from, so it
comes before the handlers. Comparing the member lists settles how big that
really is: the fork has exactly **one** member upstream lacks -- `getViewer`,
the thin-client hook -- and what it is missing falls into three groups that
do not depend on each other:

| group | size | what | needed by |
|---|---|---|---|
| A | ~56 lines | `openCommand`/`commitCommand`/`abortCommand` wrappers, `isConstructionMode`, `getAutoConstraintSearchDistance` | nearly every handler's text |
| B | ~550 lines | the `seekAutoConstraint` decomposition, plus `seekAndRenderAutoConstraint` | every handler |
| C | ~578 lines | the directional-hint subsystem (line-extension, tangent, parallel/perpendicular, hover timer) | **only** Line, LineSet and Point |

The useful part is that **C is not a prerequisite**. Only three handlers
touch it, so the minimum that unblocks resyncing the other ~30 is A + B +
the `mouseMove(SnapManager::SnapHandle)` signature -- and `SnapManager` is
already here, 73+/90- from upstream's.

Group A is not free either, and both of its costs are worth knowing: the
command wrappers want upstream's **multiple active transactions**
(`f4665aa7b5`, a Core change -- `Gui::Document::openCommand` returning an
id and `Gui::Command::commitCommand(int)`), and `isConstructionMode` wants
**per-sketch GeometryCreationMode** (`9a1020929e`), which touches all 30
handler files to delete an `extern` the resync deletes anyway. So A belongs
*with* the resync, not before it, and its members would be dead code until
the handlers call them.

### B: the auto-constraint search (`d879adcf9e`)

Taken for the three fixes that rode in on upstream's split, not for the
split itself:

- **tangency wins over alignment** (`ed45e20768`) -- the tangency pass runs
  first and suppresses the alignment one. A vertical line drawn tangent to
  a circle used to get BOTH a Vertical and a Tangent.
- **the tangency search speaks GeoIds** (`b71a54d9cc`).
  `getCompleteGeometry()` appends external geometry in REVERSE, so the loop
  index is not a GeoId. The fork's own conversion,
  `getHighestCurveIndex() - tangId`, has the direction wrong: with one
  projected circle it yields **-1, the X axis**, so the tangency was written
  against entirely the wrong geometry.
- **no Tangent without a direction** (`596fa2856b`), and the fork's axis
  clause `... || ((HAxis||VAxis) && fabs(cosangle) < 0.1)` goes, its right
  half being unreachable under the left.

Two things upstream has in these functions were deliberately left, each
because its consumer is not here yet: the endpoint-tangency handling
(`tanPos`, `removeCoincidentConstraint`), which upstream compensates for in
`generateOneAutoConstraintFromSuggestion` -- taking the seek half alone
would drop a Coincident and put nothing in its place; and the
parallel/perpendicular branch and tangent-hint early-outs, which are
group C and inert without it.

### The aspect bug this turned up (`8a4b30bc83`)

Writing the tangency test, a click aimed at x = 25 landed at x = 33.36. The
same pixel column gave x = 4 when the click hit geometry and x = 5.30 when
it did not: **the pick path and the projection path disagreed by exactly the
aspect ratio**, in one run, on one camera.

`ViewProviderSketch::getProjectingLine` had inlined a copy of
`View3DInventorViewer::getNormalizedPosition`, aspect correction and all.
That correction is right for a desktop viewer and only for one -- nothing
sets its `SoCamera::aspectRatio`, so the frustum is square and the pixel has
to be stretched into it. A mirror's camera states the client's real aspect,
so correcting again multiplies it in twice.
`MirrorViewer::normalizedPosition` had said so in a comment since the mirror
was written; the Sketcher was not asking. It asks now:
`getNormalizedPosition` is a `ViewerContext` virtual beside the rest of the
view-less camera math.

**Every Sketcher tool misplaced every point a browser put down anywhere but
the middle of the canvas**, and nothing caught it because the error is
exactly zero at the centre -- which is where `serve-mirror-edit`,
`serve-onview-params` and `serve-shared-edit` all click. The lesson
generalises past this bug: a probe that only clicks the middle of the
viewport cannot see a scale error.

`tests/gui/serve-sketch-click-placement.py` is the guard. A residual
remains and is not this defect: y lands about 0.12 sketch units (~1.4 px)
low, consistently and in both directions -- an origin convention, not a
scale.

### The snap mask and the deferred handle (`42a58c4533`, `de12a44256`, `261b3e0b92`)

The last keystone piece, and the one that lets the handler files move:
upstream's handlers all declare `mouseMove(SnapManager::SnapHandle)`, and a
pure virtual's signature cannot change for some overrides and not others.

Taken in three steps so each is verifiable on its own:

1. **The mask.** `snap()` takes a `SnapType` (Angle | Point | Edge | Grid)
   and the signatures go to upstream's value-in/value-out shape. That is
   what makes a *partial* snap expressible, and with it came the behaviour
   fix: **an axis is no longer a full snap.** Hovering the X axis used to
   set `y = 0` and return true, so `snap()` returned there and the grid
   step never ran -- x stayed wherever the pointer was, which is the one
   thing a user hovering an axis with grid snapping on does not want
   (`72d021108f`, its tail `9f2b0f910b`, and `129d64dd87`'s two
   initialisations).
2. **The handle.** `mouseMove` takes the raw position plus the manager
   instead of a pre-snapped position; all 19 overrides gain one
   `compute()` line with the default mask, so it is behaviour-neutral by
   construction. `ViewProviderSketch::mouseMove` builds one handle per
   event and each consumer computes for itself. A handle with **no
   manager** computes to itself, which is how an already-decided position
   enters -- `DrawSketchController`'s four calls (an on-view parameter or
   a typed coordinate, which must not be snapped again) and
   `DrawSketchHandlerLineSet`'s two self-calls.
3. **The one tool that wants a narrower mask.** The dimension tool drops
   `SnapType::Edge`: snapping its label onto an edge makes an angle
   constraint jump to the other side as the pointer crosses it
   (upstream #24150). That is the whole point of the mechanism, and it is
   the only non-default mask upstream has.

The button paths and the drag initialisation still snap eagerly through
`snapPoint()`. Nothing needs them deferred and changing them would be
untested churn.

Only the **Gui** half of `62c222c211` is taken. Its App hunk is an
unrelated `PointPos` fix in `reverseAngleConstraintToSupplementary`,
bundled in the same PR, and upstream rewrote that function again in
`8b06bca68a` -- it stays with the angle-expression cluster.

`tests/gui/sketch-axis-grid-snap.py`
(`GuiSketchAxisGridSnap_tests_run`) covers step 1 end to end and, after
step 2, exercises the handle path as well: grid on, grid size 10, first
point clicked at world (21, 0), which is on the X axis and one unit from
the grid line at 20. Before, the line starts at x = 20.96, the raw
pointer position; after, at 20. Step 3 is **not** covered -- see its
commit message for why.

### Construction mode belongs to the sketch (`9a1020929e`)

The first piece of the handler resync, taken ahead of the handler files
because it is what deletes `extern GeometryCreationMode
geometryCreationMode` from twenty-six of them -- and the resync deletes
that line anyway.

`geometryCreationMode` was a single global defined in
`CommandCreateGeo.cpp`, so "is the next line construction geometry?" had
one answer for every sketch in every document in the process. Turn it on
to draw a couple of guide lines in one sketch, leave it, open another,
and the first line drawn there was construction too. The state is a
member of `ViewProviderSketch` now, and
`DrawSketchHandler::isConstructionMode()` -- which is where upstream's
handler text asks -- forwards to the sketch the tool is editing.

Adaptations:

- The toolbar icon has no state of its own to show any more, so it
  follows whichever sketch is in edit: four `Gui::Application` signals
  (`signalActiveDocument`, `signalNewDocument`, `signalInEdit`,
  `signalResetEdit`) drive `updateCommands("ToggleConstruction", ...)`.
  Upstream open-codes the in-edit lookup at each of them; the fork
  already had it as `getInactiveHandlerEditModeSketchViewProvider`.
- Upstream's `drawEdit` hunk is **n/a**. The fork's
  `ViewProviderSketch::drawEdit` predates the `EditModeCoinManager`
  split: it fills the coordinate and material nodes itself with a single
  `CreateCurveColor` and never consults the mode.
- Six controller `configureToolWidget` bodies say
  `handler->isConstructionMode()`, the member being reachable there
  through `friend ControllerT`.

`tests/gui/sketch-construction-mode.py`
(`GuiSketchConstructionMode_tests_run`) is the guard, and it is built so
that the global fails it: construction toggled on in sketch A and a line
drawn, a line drawn in B without the toggle being touched, then A
re-entered and drawn in again. A's two lines are construction, B's one is
not, and the re-entry shows the mode travelled with the sketch rather
than being reset by leaving edit. The lines are drawn by the Line tool
over the wire, because the mode is only read where a tool creates
geometry -- `addGeometry` from Python takes the flag as an argument. The
toggle is run on the desktop side: `Sketcher_ToggleConstruction` is not
on the browser-safe command allowlist, and widening that list is gated on
the modal-dialog question, not on this.

### The handler resync, first six files

The method: take `git show upstream/main:<path>` whole, then put back a
**named** set of fork invariants. On the six files that do not touch the
controller state machine it works as advertised --

| file | was | now |
|---|---|---|
| Symmetry | +43 -78 | *byte-identical to upstream* |
| Splitting | +34 -67 | +3 -6 |
| Extend | +68 -97 | +3 -6 |
| Fillet | +59 -110 | +3 -6 |
| Trimming | +88 -190 | +3 -6 |
| CarbonCopy | +72 -82 | +38 -6 |

-- 364+/624- of divergence down to 50+/30-, and what is left is legible
instead of being spread through every function as reformatting.

The invariants, for the next twenty:

1. `<Gui/Selection/X.h>` -> `<Gui/X.h>`; this fork has no `Gui/Selection/`
   directory.
2. the six selection gates derive from `SketcherSelectionFilterGate`, not
   `Gui::SelectionFilterGate`. It holds `object` for them and overrides
   `restoreCursor()` to put the tool's cursor back.
3. whatever thin-client work the tool carries -- see below.

Everything else upstream's text needs is already here: `Gui::ToolHandler`
and `getToolHints()` (`cf85a2ce91`), the transaction wrappers and
`seekAndRenderAutoConstraint` (`57dc241e3c`), `isConstructionMode`
(`5ffec208cd`), `mouseMove(SnapManager::SnapHandle)` (`de12a44256`),
`Base::Tools::isNullOrEmpty`, `isDerivedFrom<T>()`, `getObject<T>()`,
`Gui::lookupHints`, `ToolHandler::updateHint`, and C++20 for `using enum`
and `std::numbers`. One line had to be added to `DrawSketchHandler.h`:
`#include <Gui/InputHint.h>`, which upstream's base header has and this
fork's did not -- `Gui/ToolHandler.h` only forward-declares the type, so a
handler that builds a hint list needs the definition.

**The trap, and what caught it.** Invariant 3 is not optional and does not
announce itself: a wholesale take compiles perfectly with the fork's
thin-client work deleted. `DrawSketchHandlerCarbonCopy.h` lost six things
that way -- `sketchgui->sessionSelection()` in three places,
`setSessionSelectionEnabled(true)` where upstream reaches for the active
window's viewer, the `gateOn` member the destructor needs, and
`Gui::ViewerContext::currentKeyboardModifiers()` where upstream asks
`QApplication` (a replayed event carries its own modifiers,
docs/ThinClient.md 8.11) -- and the only signal was
`GuiServeExternalPick_tests_run` failing its carbon-copy half. So before
overwriting a file, and again before committing it:

    git show HEAD:<path> | grep -nE "sessionSelection|\
    setSessionSelectionEnabled|ViewerContext|getViewer|allowExternal|\
    inSequence|toggle\(|gateOn|ThinClient"
    git diff -w HEAD:<path> <path> | grep "^-"

Of these six only CarbonCopy hit, so the check is cheap.

Line endings are the other mechanical trap: `.gitattributes` here is
`* -text`, "whatever a file already has stays put", and upstream's blobs
are LF while most of these files are CRLF. Convert on write, or the commit
reads as a whole-file rewrite and the non-ASCII hook sees every line as
added.

### The controller framework is a second keystone (user ruling, 2026-09-18)

Section 7's "A + B + the SnapHandle signature unblocks the other ~30" was
incomplete. **Nineteen** of the twenty-seven handler files also call a
controller API this fork does not have: `hasFinishedEditing`,
`setNextState`, `computeNextDrawSketchHandlerMode`. It is not a rename --
upstream splits "a value is set" from "the user finished entering it",
which is what drives Enter-cycling between on-view parameters and
Ctrl+Enter to accept them all.

That subsystem is where this fork's view-less on-view parameters live
(docs/ThinClient.md 8.7, guarded by `GuiServeOnViewParams_tests_run`), so
taking it is not free. Asked, the user ruled: **take the controller
framework first**, as its own unit --
`DrawSketchController.h` (+115 -211), `DrawSketchDefaultHandler.h`
(+363 -287), `DrawSketchDefaultWidgetController.h` (+75 -70),
`DrawSketchControllableHandler.h` (+20 -52) and
`SketcherToolDefaultWidget.{h,cpp}` (+139 -132) -- rather than
hand-translating upstream's spelling back in nineteen files and paying it
again at the next resync.

Still outside both groups: `DrawSketchHandlerExternal.h` (**and it stays
outside -- see "External.h is not a resync" below**), and
`DrawSketchHandlerLine.h`,
`LineSet.h` and `Point.h`, which want group C's directional hints.
`DrawSketchHandlerBSplineByInterpolation.h` is fork-only and has no
upstream blob at all.

### The framework resync, and the rest of the family (`b4b8f10a3b`)

The ruling above, carried out. `+6194 -12516` against upstream across the
handler family became `+806 -1511`, and **nineteen files are byte-identical
to upstream**: Arc, ArcOfEllipse, ArcOfHyperbola, ArcOfParabola, ArcSlot,
BSpline, Circle, Ellipse, Offset, Polygon, Rectangle, Rotate, Scale, Slot,
Text, Translate, `DrawSketchDefaultWidgetController.h` and
`SketcherToolDefaultWidget.{h,cpp}`. What is left is five named invariants,
listed in the commit message and in the resync section above.

`EditableDatumLabel` was **added to, not resynced** (`4d210d07f8`): the
fork rebuilt it on `Gui::ViewerContext` so a view with no widget can stream
the entry box to a client, and it carries that whole API --
`getAnchorPoint`, `getText`, `getSelection`, `sendKeyEvent`,
`notifyChanged`. That divergence is about *where the box renders*;
upstream's feature is about *what it does with a keypress*, so only the
second was taken. The browser gets it for nothing: `sendKeyEvent` goes
through `QApplication::sendEvent`, which runs the receiver's filters. Not
taken: the lock icon, which needs `QuantitySpinBox::addIconSpace` and
`getMargin` and has nowhere to be drawn on a mirror -- `setLockedAppearance`
keeps the state and paints nothing.

What the compiler turned up as genuinely missing, each added rather than
worked around: the three auto-constraint helpers
`DrawSketchDefaultHandler` builds through; `cancelCurrentAction`;
`Base::Unit::One`; `DrawSketchKeyboardManager::resetMode`;
`Constraint2LinesByAngle`; `SketchObject`'s four label accessors; and
upstream's `SketcherTransformationExpressionHelper`, a new file pair.
`areColinear` took upstream's spelling.

**The direction of the endpoint-tangency hazard matters.** Section 7 warns
that taking the *seek* half alone would drop a Coincident and put nothing
in its place. Taking `generateOneAutoConstraintFromSuggestion` -- the
*generate* half -- alone is safe in the other direction: it only upgrades
a Coincident when a matching Tangent is already in the set, which is a
no-op for what this fork currently suggests.

### LineSet: upstream had the same cycle, on a key

The tool-bar `toggle()` looked at first like a fork feature the resync
would destroy. It is not: upstream has **the identical six-mode cycle**,
bound to `M` in `registerPressedKey`, and this fork had dropped the `M`
path when it added `toggle()`. So the body is factored into
`cycleSegmentAndTransitionMode()` and both triggers call it -- `M` exactly
as upstream guards it (`previousCurve != -1`), and `toggle()` as the fork
added it, with `geom` allowed to be null because a button press need not
have a previous curve.

Upstream's file carries a **second** polyline as well: a controller-based
`DrawSketchHandlerPolyLine` on `Sketcher_CreatePolyline`, with the classic
handler demoted to `Sketcher_CreatePolylineLegacy`. The whole file was
taken, so the new tool is present but unused here; adopting it is a
one-line command change and a separate decision.

### The tool mode became a command (`90f0e23eac`)

**`M` was hardcoded and that was a defect** (user, 2026-09-18). It was the
raw Coin constant `SoKeyboardEvent::M`, tested inside `registerPressedKey`,
which `ViewProviderSketch` feeds straight from the viewport: not a
`Gui::Command`, so no shortcut-editor entry, no rebinding, no tool bar, and
no way for a client without key bindings to reach it -- which is why
`toggle()` exists at all.

**Four** handlers hardcoded it, not three: `DrawSketchDefaultHandler` (the
construction-method machine), `DrawSketchHandlerLineSet` (segment and
transition), the dimension tool in `CommandConstraints.cpp` (which of the
constraints the selection allows), and
`DrawSketchHandlerBSplineByInterpolation` (the knot multiplicity at the
last point).

The handler answers the question now -- `canIterateToolMode()` and
`iterateToolMode()` on the base, each override keeping the guard its key
branch had -- and `Sketcher_NextToolMode` asks, with `M` as an ordinary
accelerator. The conflict question was the user's to answer and they did:
this fork has `Gui::ShortcutManager`, which arbitrates accelerators
dynamically by priority, and `Command.cpp:272` records that the static
check was retired for it.

Two things worth keeping:

- The B-spline case is a **dialog**, not a mode, and sits oddly under that
  name. Leaving it on the raw key would have meant the new accelerator
  silently swallowing it, so it moved with the rest. It is also why the
  command is **not** on the browser-safe list in `Gui/SceneControl.cpp`: a
  modal on the GUI thread of a serving process stops serving everyone.
- It **looks** like a behaviour change for unit entry and is not. With an
  on-view parameter focused, `DrawSketchKeyboardManager` only claims keys
  for the spin box for a short window after a digit, so an `m` typed
  outside that window already reached the view provider and cycled the
  mode. What changes is that it is consistent, and that a user who does not
  want it can now clear the shortcut.

`tests/gui/sketch-next-tool-mode.py`
(`GuiSketchNextToolMode_tests_run`) reads the claim through geometry, which
is the only place the mode is visible: with a line already drawn the
polyline's cycle runs Line/Free -> Perpendicular_L -> Tangent ->
Arc/Tangent, so three runs of the command turn the next segment into an
arc, while the same run's first segment stays a line as the control.

### Rows closed without a pick

- `19a082b63c` "Fix constraint selection in groups" -- **n/a**. Its subject
  says groups; it is about combined constraint icons. Three changes, none
  of which applies: generalising the icon lookup from "first or second" to
  a child-index scan is a no-op, because a constraint separator holds at
  most two icons *in both trees* -- `ConstraintNodePosition` is identical
  upstream and here, and every branch of the fork's separator builder adds
  either 4 or 7 children. The coordinate rework is the fork's own fix
  already, and the fork's version is the one that works for a view-less
  mirror (it projects to viewport device pixels rather than the widget's
  logical size, which is no unit at all for a client). And upstream's new
  `dynamic_cast<SoDatumLabel*>` gate would *break* the fork: Group/Text and
  Symmetric separators rely on the "no icon id found -> the separator's own
  index" fallback it replaces.

### Group C: the directional auto-constraint hints

The keystone's last group, and with it `DrawSketchHandler.{h,cpp}`
themselves. Group C is the subsystem that shows a user *where* a
constraint the tool is about to suggest would come from, before they
click: the dashed prolongation of a line the cursor has run past, and the
parallel / perpendicular reference lines of whatever the pointer has been
resting on. It also snaps the cursor onto those lines, which is why five
sites in the resynced handlers had to be commented out with "group C,
which owns it, is not ported yet" until it arrived.

It came in three pieces.

**The edit scene had to learn to draw them.** Upstream's drawing lives in
`EditModeCoinManager`, which this fork predates, so the two primitives
were written against the fork's own `EditData`: a
`LineExtensionAutoConstraintHint` line set and a
`ParallelPerpendicularHint` line set, each in its own unpickable
separator on the information layer, each dashed with the `0x0f0f` pattern
the fork already uses there. The parallel/perpendicular set binds one
colour per line (`SoMaterialBinding::PER_FACE`) so that the line the
cursor is currently aligned with can be lit in `InformationColor` while
the others stay in a new muted `DirectionalHintColor`; upstream reaches
for its grid colour, which has no counterpart here.

`isLineExtensionAutoConstraintHintVisible` is the one that needed real
thought rather than translation. Upstream asks `getActiveView()` for a
`View3DInventor`, then its `QGLWidget` for a width and height. Both
questions are meaningless for a mirror -- "which window is active" has no
useful answer in a process serving several browsers, and a mirror has no
widget at all (docs/ThinClient.md sec 8.3). The fork's version asks
`editViewer()` for the session's viewer and takes the size from its
**viewport region**, which a mirror answers with what the browser stated
over the wire. The sketch's editing placement still has to be applied
before projecting, exactly as upstream's `getScreenCoordinates` does.

**`DrawSketchHandler.{h,cpp}` were resynced whole**, the same method as
the nineteen handler files before them: take `git show upstream/main:` and
put the fork's invariants back. Group C is most of what the fork was
missing, so the numbers are the largest of the phase -- the header went
from 412 differing lines against upstream to **114**, the source from
about 2800 to **92**. Neither can ever be byte-identical, because six
things in them are the fork:

- the `PreCompiled.h` include block (upstream dropped the PCH here);
- `getViewer()`, which names the edit session's view rather than the
  active window, and `SketcherSelectionFilterGate`;
- the constraint icons on the cursor are sized by
  `ToolHandler::devicePixelRatio()`, the *client's* ratio, where upstream
  now hardcodes 16 px;
- `openCommand`/`commitCommand`/`abortCommand` forward to the
  `Gui::Command` statics. This is the ruling against `f4665aa7b5` still
  standing, and it has one visible consequence: upstream's new
  `deactivate()` aborts any transaction still open, which is safe there
  only because its `abortCommand()` names the handler's own id. Aborting
  blindly here would take a transaction something else opened, so that
  line was not taken. For the same reason the two `closeAndRecompute(
  currentTransactionID, ...)` calls in `createAutoConstraints` are not
  taken either -- this fork's `makeTangentTo*viaNewPoint` helpers commit
  or abort on their own, which upstream's no longer do;
- `moveConstraint` without `OffsetMode`, and `setOriginPointMarker` left
  out: both are separate upstream features, not group C.

`Base::toRadians` and `toDegrees` became `constexpr` -- the fork's own
templates, not upstream's tightened pair that refuses a deduced integer.
Three group C call sites need them in a constant expression and nothing
else changes.

**The consumers.** All five commented-out sites were re-enabled, and two
`getStartPointOfCurrentSegment` overrides came across with them --
`Line.h` and `LineSet.h` are the tools that can say where the segment
being drawn starts, which is what the endpoint parallel/perpendicular
hint hangs off, and without them `updateParallelPerpendicularEndpointHint`
can never fire. The payoff is that **`DrawSketchHandlerLine.h`,
`DrawSketchHandlerPoint.h` and `DrawSketchControllableHandler.h` are now
byte-identical to upstream**, one more than the handoff predicted.

`tests/gui/sketch-line-extension-autoconstraint.py`
(`GuiSketchLineExtensionAutoConstraint_tests_run`) guards the half of
group C that can be read without pixels. A click *past* a segment's end,
where nothing is drawn, picks up a PointOnObject to that segment and is
placed exactly on its prolongation -- and nothing else in the
auto-constraint search can speak out there, so the constraint can only
have come from group C. It is driven over the wire because the
visibility gate above is the piece that had to be rewritten, and only a
mirror exercises it.

Two numbers in that test were learned by writing it wrong first, and are
worth knowing before writing another. The search distance is
`0.1 * getScaleFactor()`, and that factor is a world length proportional
to what the camera shows, so **the band is a fixed ~`0.002 * VH` pixels
at any zoom** -- 1.2 px at the 800x600 the other sketch tests state,
which no integer pixel can be relied on to land inside. The test states
2400x1800 for a 3.6 px band. And a control click must be kept off the
prolongation of the line the *previous* case drew: put it there and
group C snaps it to that line instead, quite correctly, and the control
measures nothing.

### The `pixel_of()` off-by-one, chased and closed

Writing that test also showed `pixel_of()`'s y landing one pixel below
where the mirror put it, x agreeing exactly. It was worked around then
and **diagnosed since: the helper was wrong, not the mirror.**

A canvas reports a pixel from the top left and Coin's viewport origin is
the bottom left, so the client's y is flipped on the way in. Both paths
spell that flip the same way -- `windowsize[1] - p.y() - 1` in
`Gui/Quarter/Mouse.cpp`, `size[1] - 1 - input.y` in `MirrorViewer.cpp`
-- and both then normalize by dividing by the viewport height, so
**the served tier and the desktop agree about a pixel**; the y branch of
the desktop's aspect correction is a no-op in landscape, and x is never
flipped, which is exactly why only y was off. The helper inverted that
as `VH/2 - y*scale`, dropping the `-1`, and so was out by one pixel
everywhere and by one pixel only.

The fix is that `-1`, in all seven copies of the helper across
`tests/gui`. `sketch-line-extension-autoconstraint.py` now asserts it
rather than tolerating it: an unsnapped point *is* the clicked pixel
unprojected, so the residual between where the tool put it and what
`world_y_of_pixel()` predicts measures the helper against the mirror's
own arithmetic. It was exactly `-1.0` px before and is `-0.0000` px
after.

### External.h is not a resync, and the datum types were the real pick

The ledger above expected `DrawSketchHandlerExternal.h` to be resynced
once `App/Datums.h` existed. Reading it after the Datums port landed
says otherwise, and it is worth recording so the 417-line gap is not
mistaken for drift again.

**The fork's file is a much larger implementation than upstream's** --
382 lines against 265, and the difference is features, not spelling:
cross-document external geometry through `Part.importExternalObject`,
`attachExternal` for reattaching existing external geometry,
`SketchAutoTransparentPick` with its `ParameterGrp` observer, Alt-key
whole-object selection, the task panel's face-pick gate, element-map
naming through `Data::IndexedName`, and the thin-client session
selection (docs/ThinClient.md 8.11 item 3). Upstream has none of
it. Taking upstream's blob would delete all of it, and nothing would
fail to compile.

What the Datums port did unblock is much smaller: **`App::Point` can be
external geometry.** Only that one type, and the first version of this
change was wrong about why, so the shape of it is worth stating
exactly.

**Origin axes and planes already worked.**
`Part::Feature::getTopoShape()` synthesizes a shape for them --
`PartFeature.cpp` builds a static infinite edge for an `App::Line` and a
static infinite face for an `App::Plane`, placed by the resolution
matrix -- so `Part::Feature::getShape()` returns an Edge for `Y_Axis`
and a Face for `XY_Plane`. Both ends of the external-geometry path were
therefore already satisfied for them, the selection gate's shape check
included. **`App::Point` has no such case**, so it alone came back null
and could not be referenced.

So the projection builds a vertex for `App::Point` and nothing else; an
`App::Line` keeps going through `getTopoShape`, which is also **what
carries its element map** (`refTopoShape`, "the same with its element
map, for what names its projection"). Intercepting `App::Line` here to
build the edge by hand looks harmless and is not: it leaves
`refTopoShape` null and the projection loses the names it should
inherit. `Part::DatumPoint` rides along, being an `App::Point`;
`Part::DatumLine` already worked, being an `App::Line`.

**There are three shape-synthesis mechanisms for datums here, and it is
worth knowing which one a type uses before deciding it has no shape:**

| type | where the shape comes from |
|---|---|
| `App::Line`, `App::Plane` | `Part::Feature::getTopoShape()`, lazily -- `PartFeature.cpp` returns a static infinite edge / face placed by the resolution matrix |
| `Part::Datum` subclasses (PartDesign's Plane, Line, Point, CoordinateSystem) | the object's own `makeShape()`, eagerly, from the constructor and `onDocumentRestored()`, stored in the `Shape` property. Its comment says why: "used by the Sketcher... to avoid a dependency of Sketcher on the PartDesign module" |
| `App::Point` | nowhere -- which is the whole of what this change adds |

Measured with `Part.getShape()`: Face/Edge/Vertex for every
`PartDesign::*` datum and for `App::Line`/`App::Plane`, null only for
`App::Point`. Note `Part::Datum` is **not** an `App::DatumElement`, so
the gate change below cannot affect PartDesign's datums at all.

The `Part::Datum*` types added by the Part half of the Datums port
inherit the first mechanism, not the second: `Part::DatumPlane` and
`Part::DatumLine` are an `App::Plane` and an `App::Line` and so already
had shapes, while `Part::DatumPoint` is an `App::Point` and is the one
that needed the new branch. Verified: `Part::DatumPlane` and
`PartDesign::Plane` project identically (three line segments onto a
perpendicular sketch, and the same refusal when coplanar), and
`Part::DatumPoint` projects a point.

The selection gate's change is correspondingly narrow: a shape that is
null is accepted, instead of rejected with "No shape", **only** when the
object is an `App::DatumElement`, and that object is then taken whole.
Everything with a shape -- including origin planes -- still goes through
the type classification, which is what sets `sSubName` and so what makes
the task panel's face-pick gate apply to them. Widening the bypass to
every datum element by type silently turns that gate off for origin
planes.

Read `getBasePoint()`/`getDirection()` and not `Placement` when adding
to this: a datum inside a placed `LocalCoordinateSystem` carries that
placement too, and for an `Origin` the two agree.

Tests: `testOriginAxisIsExternalGeometry`,
`testOriginPointIsExternalGeometry` and
`testOriginExternalsSurviveSaveAndLoad` in
`TestSketchExternalGeometry.py` -- the last because external geometry is
re-projected on restore, so the new branches have to hold there too.

### LineSet is closed too: the resync already happened

The ledger carried `DrawSketchHandlerLineSet.h` as the one tool handler
still genuinely open, on the strength of its size: 196 differing lines
against upstream, 117 fork-only to 79 upstream-only, and no thin-client
marker anywhere in it. That profile reads as drift. It is not.

**The file was taken whole in `b4b8f10a3b`**, the controller-framework
resync -- upstream's second, controller-based `DrawSketchHandlerPolyLine`
is sitting in it, which is the proof. What `90f0e23eac` and `338b27fea2`
then put back on top is the divergence being counted.

Measured rather than read, the 196 lines are almost all spelling. Against
upstream's tip blob, ignoring whitespace, the file is 48 fork-only lines
to 10 upstream-only; stripping whitespace entirely, so that a reflowed
line cannot hide a real one, 45 to 10. The remaining 69 lines on each
side are upstream's 2025-11-11 reformat.

**All ten upstream-only lines are one edit, and it is the inverse of a
fork change**: they re-inline `cycleSegmentAndTransitionMode()` back into
a `registerPressedKey` override that tests the raw `SoKeyboardEvent::M`.
Six are the signature, the `Mode == STATUS_SEEK_Second && ... &&
previousCurve != -1` guard, the unguarded `geom` lookup and the `else`
that forwards to the base; the other four are the fork's four
`if (geom && geom->is<Part::GeomArcOfCircle>())` tests minus the null
guard. There is no upstream fix and no upstream feature in this file that
the fork does not have -- the tip-blob comparison proves it for the whole
history at once, which is stronger than walking the log.

So taking the blob would buy nothing and would delete, without a compile
error, every override being counted as drift:

- `cycleSegmentAndTransitionMode()`, the cycle body the two triggers share;
- `toggle()`, so that pressing the polyline button again cycles the mode
  instead of restarting the tool -- the route a client with no key
  bindings has;
- `canIterateToolMode()` / `iterateToolMode()`, which is how
  `Sketcher_NextToolMode` reaches the cycle and how its menu entry knows
  to grey out;
- the `geom &&` guards and the `dirVec` fallback, which are what make the
  button route safe when there is no previous curve. The M route cannot
  reach a null `geom`, because `canIterateToolMode()` carries upstream's
  `previousCurve != -1` guard unchanged.

Nothing is lost on the key path: `M` is still the default, now as
`sAccel` on `Sketcher_NextToolMode` rather than a hardcoded constant, and
the handler no longer overrides `registerPressedKey` at all, which is what
upstream's `else` branch did by hand.

The one thing here that is still open is a decision, not a resync:
upstream's `DrawSketchHandlerPolyLine` is compiled but never instantiated,
since `CmdSketcherCreatePolyline::activated()` still constructs
`DrawSketchHandlerLineSet`. Adopting it is the one-line command change
noted above, and upstream pairs it with a `Sketcher_CreatePolylineLegacy`
command that this fork does not register.

**The lesson is [[measure-the-before-state]] applied to a diff**: a line
count is not a baseline. The marker grep from the CarbonCopy lesson would
have caught this one too -- `toggle(` is on its list -- and the count was
believed over the grep.

### The Gui rename pair: names taken, sizing kept

The datums port left three Gui names open, deferred because
`Gui::ViewProviderDatum` was taken here by a live class -- the abstract
extents base whose only subclasses were PartDesign's, and which
`ViewProviderOriginGroupExtension` reached into to size datums. The ruling
was to follow upstream and keep backward compatibility as far as it goes.

**The premise turned out to be wrong in a way that matters.** Upstream's
classes are not the fork's classes renamed; they are re-implementations,
and taking them wholesale would have deleted behaviour with no compile
error -- the same shape of trap as the LineSet line count above.

What the comparison found, at upstream's tip:

- `Gui::ViewProviderDatum` drops the `Size` property for a screen-space
  `SoShapeScale`. `active` defaults to `true` and `updateScale` recomputes
  per frame: `nsize = scaleFactor / viewportWidthPixels`, then
  `sf = vv.getWorldToScreenScale(center, nsize)` at the datum's own world
  position. The size is a constant fraction of the viewport, independent of
  zoom and of the model, and `scaleFactor` comes from one global preference
  (`LocalCoordinateSystemSize`, default 1.0) rather than from anything
  per-origin.
- `Gui::ViewProviderCoordinateSystem` drops `Size` and `Margin` and rebases
  onto `ViewProviderGeoFeatureGroup`.
- `ViewProviderOriginGroupExtension.cpp` is 102 lines upstream. The whole
  `updateOriginSize` chain is gone, which is the consequence of the first
  two, not a separate decision.

**Two of those cannot be taken here.** The landed App half made
`App::LocalCoordinateSystem` a plain `GeoFeature`, deliberately not
inheriting `GeoFeatureGroupExtension` publicly, because `App::Origin`
carries the fork's private `OriginExtension` instead. There is no object
model under `ViewProviderGeoFeatureGroup` to rebase onto. And the
screen-space default is tuned for upstream's UX, where an origin is a
transient per-edit aid shown one at a time -- which is what
`setTemporaryVisibility`, `setTemporaryScale` and `setPlaneLabelVisibility`
are for. This is the Link fork: nested Links and Parts mean many
coordinate systems visible at once and persistently, and constant
on-screen size draws every one of them identically whatever it belongs to,
so a small bracket and a large frame get the same gizmo and two nearby
bodies get two interpenetrating plane-triples. Sizing each origin to its
own group's bbox is also the mainstream CAD convention: reference planes
scale to the model, and constant screen size is what manipulators do.

So the pair took **upstream's names and upstream's file layout, and kept
the fork's sizing**. Three commits:

- `4ae4f29d11` dissolves the extents base into
  `PartDesignGui::ViewProviderDatum`, as upstream did, freeing the name.
  `updateOriginSize` keeps working without it: the datums already size
  themselves against the same model from their own `updateData`, so the
  `setExtents` push goes away, and the datums-as-base-points rule is kept
  by testing the object rather than the view provider -- `Part::Datum` by
  name, because Part sits above Gui in the link order, and its base point
  is its placement position. The type is looked up per call, since a
  cached one would stay bad for the process's life if Part happened not to
  be loaded on the first pass.
- `b81c3dcc29` renames `ViewProviderOriginFeature` to `ViewProviderDatum`.
- The third renames `ViewProviderOrigin` to
  `ViewProviderCoordinateSystem`.

Backward compatibility is kept at every point it can be:
`Base::Type::addLegacyName` for both renamed view providers, so a
`GuiDocument.xml` written before the rename still restores; shim headers
left at `ViewProviderOriginFeature.h` and `ViewProviderOrigin.h`;
`getOriginFeatureRoot()` kept as a forwarder to upstream's
`getDatumRoot()`. `Size` and `Margin` survive, so no saved value is
dropped and no macro breaks. The one thing that cannot be aliased is the
`Gui::ViewProviderDatum` name itself, which now denotes a different class
than it did -- flagged, not papered over.

Deliberately not taken, and separable:

- upstream's screen-space `SoShapeScale` default. The fork's own
  `SoShapeScale` has its auto-scale body `#if 0`'d out ("Auto scale is now
  done with SoAutoZoomTranslation"), so wiring it in with `active=false`
  would have been inert code that also silently forces scale to
  `(1,1,1)`; it is left for a real decision with a preference behind it.
- upstream's `setTemporaryVisibility(DatumElements)` bitmask. It is a pure
  API-shape change across 16 call sites in Part and PartDesign with no
  behavioural gain, and keeping `(bool axis, bool planes)` also keeps
  out-of-tree callers compiling.

### The origin marker, and what a tip-blob sweep is worth (`16908241f0`)

The ledger's 503 open rows were never 503 pieces of work. A resync that
takes a file whole closes every upstream commit that ever touched it, and
closes them silently -- the rows stay open because nobody walked them.

So the LineSet reading was applied as a sweep: for each of the 33 files
the four resync commits touched, diff the fork's blob against upstream's
tip ignoring whitespace. **21 come out identical, no line either way**,
which settles their whole history at once. Those 21 carry 74 open rows,
48 of them fixes, now marked have. The remaining 12 each have a handful of
upstream-only lines:

| file | upstream-only | what they are |
|---|---|---|
| `DrawSketchKeyboardManager.{h,cpp}` | 48 / 7 | **genuinely open**, below |
| `DrawSketchHandler.cpp` | 42 | group A's transaction ids, declined; and below |
| `DrawSketchHandlerCarbonCopy.h` | 21 | the gate base, and the fork's scoped selection |
| `DrawSketchHandlerLineSet.h` | 10 | read and closed, section above |
| `DrawSketchHandler{Trimming,Splitting,Fillet,Extend}.h` | 6 each | the gate base; closed |
| `DrawSketchHandler.h` | 6 | read, below |
| `DrawSketchController.h` | 6 | the thin client's `ViewerContext`, deliberate |
| `DrawSketchDefaultHandler.h` | 3 | the M key inlined, the inverse of `90f0e23eac` |

Five of those are one reading between them. `Trimming`, `Splitting`,
`Fillet`, `Extend` and `CarbonCopy` differ from upstream in exactly two
ways: upstream's `Gui/Selection/SelectionFilter.h`, a path this fork has
not moved to, and each one's selection gate deriving from the fork's
`SketcherSelectionFilterGate` instead of carrying its own `object` member.
Neither is an upstream fix, so their **8 open rows are closed**, four of
them fixes and one a feature.

`DrawSketchHandler.cpp`'s 42 are group A's `currentTransactionID` threaded
through `openCommand`/`commitCommand`/`abortCommand` -- waiting on
`f4665aa7b5`, declined -- plus one thing worth a probe: upstream's
`deactivate()` aborts any transaction the tool left open and recomputes,
"else we have acces violation because preselection still referenced the
removed bspline points". This fork deliberately does not abort, because
its `abortCommand()` would take a transaction something else opened.

**Probed rather than argued.** `DrawSketchHandlerBSpline::activated()`
calls `openCommand()` before any click, so the prediction was a transaction
left open by activating the tool and pressing Escape. It is not:
`HasPendingTransaction` and `getActiveTransaction()` both read false and
`None` throughout -- before the tool, while it is running, and after
Escape, for the B-spline and the line tool alike. The fork opens nothing
until a change is actually made, so there is nothing to leak. What this
does *not* cover is escaping a tool that has already placed geometry;
there the handler's own `abortCommand()` calls are what run.

**`DrawSketchKeyboardManager` is the one genuinely open file of the
twelve**, and it is a real pick rather than drift. Upstream has since
gained: `Alt`+key passing a keystroke to the viewer for navigation
*without* permanently switching the destination away from the tool's input
(it strips the modifier and forwards), `QKeySequence::Paste` detection, and
Enter/Return/Tab switching back to camera control. The fork's file is the
older shape plus its own `vpViewer->sendKeyEvent()` indirection, which is
what lets a mirror replay a key as the Coin event it arrived as. An
adaptation has to keep that indirection and can take the rest.

### The keyboard manager, resynced (`98d6930279`)

Six upstream commits touch this file and none of them is drift:
`fe89807f53` (Delete on macOS), `6664907bd5` (backspace resets an on-view
parameter), `bd07c8a214` (Tab), `ebd770e025` (the OVP refactor),
`5b0ac59255` (KeyRelease as well as KeyPress) and `f07195c198` (reset on
click). Three are already here in part -- the fork has the KeyRelease line,
Backspace and Delete in its detect list, and `resetMode()` with both of its
handler-side callers, since the polyline and B-spline handlers are
upstream's own code after the resync.

What was missing is `ebd770e025`'s substance:

- **Alt+key goes to the viewer without moving the destination.** In
  `DSHControl` the modifier is stripped and the bare key forwarded, so the
  view can be turned in the middle of typing a dimension and the box still
  has the keys afterwards. There was no way to do that before: a digit
  typed with Alt held was simply typed.
- **`QKeySequence::Paste`** is recognised, so Ctrl+V reaches the entry box
  rather than being read as a navigation key.
- **Enter, Return and Tab** hand the keys back to the camera explicitly.
- **Backspace and Delete** are matched by key *sequence* as well as by key.

Taken as a blob with the fork's two deltas put back, the same way the tool
handlers were: `vpViewer` is a `Gui::ViewerContext` rather than a
`View3DInventorViewer`, and a key goes back to the scene through
`sendKeyEvent()`.

**The timer goes with it.** It was upstream's, from 2023, and the OVP
refactor removed it: rather than the mode falling back to the camera two
seconds after the last keystroke, it is handed back on Enter, Return, Tab
or an explicit `resetMode()`. Nothing here called `setTimeOut()` or
`timeOut()`, and the manager is reached from nowhere else -- it is an event
filter installed on the on-view parameter's spin box, and nothing reads its
mode from outside.

**What is not tested, and why.** The Alt and Paste paths themselves. They
need an on-view parameter holding the keyboard focus, and on the desktop no
box is focused without real pointer movement -- after a tool starts the
widget with focus is the viewer, which a probe confirmed, with
`OnViewParameterVisibility` at 2 and no spin box visible at all.
`serve-onview-params.py` already drives the whole OVP flow over the wire,
including a key frame carrying a digit, so that is where the test belongs;
it needs the frame format to carry a modifier.

`DrawSketchHandler.h`'s six are three things: upstream's
`Gui/Selection/Selection.h` path, which this fork has not moved;
`currentTransactionID` and the `OffsetMode` on `moveConstraint`, which
belong to group A's command wrappers and wait on `f4665aa7b5` -- declined;
and `setOriginPointMarker`, which is a real gap.

**The pick.** `16908241f0` is two independent fixes in one commit, and
only one of them is missing here. The on-view-parameter half -- seeding a
parameter at the previous cursor position before activating it, so the
label does not flash at the origin, and not re-activating one already
active -- is **already in this fork, line for line**. Only the origin
marker is new: for as long as a drawing tool is running, the sketch origin
is drawn as an outline (`CIRCLE_LINE`) instead of filled, so that it reads
as somewhere to snap to rather than as another vertex, and is put back on
deactivate.

Upstream can do that by naming a node: `EditModeCoinManager` keeps
`OriginPointSet` and `OriginPointSetOccluded` apart from the rest, and the
fix flips their single `markerIndex`. **This fork has no such node** --
decision 3 keeps the monolithic `ViewProviderSketch`, where the origin is
simply point 0 of the one `SoMarkerSet` every vertex shares. So the fix
does not apply textually at all.

It applies structurally, though, and cheaply, because the groups feature
already taught that marker set to speak per point: a sketch holding a
group gives `markerIndex` one value per vertex so a member's vertices can
be `NONE`. The origin wanting a marker of its own is the same shape, so
`setOriginPointMarker()` switches the field to per-point form and writes
index 0, the draw path keeps it that way, and `updateInventorNodeSizes()`
-- which collapses the field back to one value -- puts it back after.

What could silently do nothing here is the per-point switch, so the test
reads the field itself: `[50]` with no tool up, `[40, 50, 50]` with one,
`[50, 50, 50]` after Escape. Without the pick the middle reading is `[50]`,
which is what the test scores before it is applied
([[measure-the-before-state]]). `tests/gui/sketch-origin-marker.py`,
`GuiSketchOriginMarker_tests_run`.

Two harness notes. There is **no Python call that purges a tool handler**,
and `resetEdit()` is no substitute because leaving edit mode tears the
scene down and proves nothing about the restore -- so the test sends a
synthetic `Escape`, which reaches Coin where a synthetic mouse event would
preselect nothing. And the viewer widget is matched on `Quarter` **or**
`View3DInventorViewer`: matching only the first finds nothing here, and a
helper that returns false silently reads as a failed restore.

### The sweep over every file, and the three picks it found

The previous sweep covered the 33 files the four handler resyncs had
touched. This one covers **every file of the Sketcher tree**, and it asks
a sharper question than blob identity.

Blob identity is too strict. The fork's file is allowed to carry things
upstream's does not -- that is what a fork is -- so "no line either way"
throws away every file that has fork work in it, which is most of the
interesting ones. The question that actually settles a row is per row,
not per file: **is every line this commit added present in the fork's
file, and is every line it removed absent?** Normalise whitespace away,
ignore lines too short or too punctuation-heavy to mean anything, and
the answer is mechanical. Over the 414 open rows: 23 came out HAVE, 21
touch only files the fork does not have at all, 14 are pure deletions
the additions-test cannot judge, and 356 are genuinely open.

Then read them, because the mechanical answer has one failure mode worth
knowing. The test is file-global: for a one-line change it can match the
added line somewhere else in the file and call it applied. `2deee96cab`
("fix inverted null check in `purgeHandler`") is exactly that -- it came
out HAVE, and the real reason the fork is fine is that its `purgeHandler`
is four lines with no `editDoc` in them. Right answer, wrong evidence.
All 23 were read; all 23 hold, for one reason or another.

The 14 deletions were read the other way round, by grepping the fork for
what upstream removed. Six are gone already (`_USE_MATH_DEFINES`, the
`PreCompiled.h` include in the transformation helper, an unused
`SketchObject*`, two `printf`s, the `doSetVisible` transaction, the text
dialog's dangling `openCommand`). One, `7db3f901fd`, upstream **undid**:
its tip calls `Workbench::leaveEditMode()` from `unsetEdit` again, which
is what the fork does, so the row is superseded rather than open.

**55 rows closed, 414 -> 359.** And three picks fell out of it:

| pick | upstream | what it was |
|---|---|---|
| `205fd5b037` | `833e9cc8ab` | the deactivated-constraint colour default |
| `8b71884fef` | `7909815002` | twelve settings-page strings that described the wrong thing |
| `1b50af663b` | `ec298e9e9a` +6 | auto-constraints for a drag |

The colour one is the kind of defect only a sweep finds. The colour page
offered `127,127,127` for `DeactivatedConstrDimColor` while both C++
defaults are `0.8f` grey, `#CCCCCC`. A `PrefColorButton`'s `color`
property is what it falls back to when the parameter is unset, so opening
the page and pressing OK persisted a colour the sketch had never drawn
with. Upstream fixed it on `SketcherSettingsAppearance.ui`, a file this
fork does not have -- its colours lived in `SketcherSettingsColors.ui` then --
which is precisely why the row had stayed open and unread.

### Auto-constraints for a drag (`ec298e9e9a` and six more)

Seven rows, one feature, and the fork had none of it. A drawing tool has
always suggested the constraint that would pin the point it is about to
place; dragging an existing point offered nothing, so a point dropped on
top of its neighbour looked coincident and was constrained to nothing.

The seven commits all converge on one pair of files that exists only
upstream, so the pair was **taken at upstream's tip** -- which is the
whole family at once, follow-up fixes included -- and only the hooks
adapted. What made that cheap is that the fork already had the hard
half: group B's auto-constraint search, and with it
`generateOneAutoConstraintFromSuggestion`,
`filterRedundantAutoConstraints` and `addGeneratedAutoConstraints`, plus
`2c2f1d7db7`'s move of the search out of `DrawSketchDefaultHandler.h`.
Even `ec298e9e9a`'s own `DrawSketchHandler.cpp` half -- the null-icon
guard and the empty-pixmap fallback -- was already here.

Three adaptations:

- the dragged elements live in `edit->Dragged`, inside the `EditData`
  the fork keeps in the `.cpp` rather than upstream's `drag` member in
  the header, so the handler lives there too and goes away with the
  edit session;
- the handler reaches the view through `Gui::ViewerContext`, not
  upstream's `getCursorWidget()`, so a client's mirror -- which has no
  widget -- simply keeps its cursor;
- `initDragging` has several paths that fill `edit->Dragged` and then
  give up with `STATUS_NONE` (a B-spline pole the solver cannot move, a
  non-rational B-spline). Upstream arms the handler before those, which
  leaves the tool cursor on a view that is not dragging anything. Here
  `beginDragAutoConstraints()` is called from the two exits that really
  are a live drag.

`doDragStep` now reports whether the temporary move succeeded, so a step
the solver refused clears the suggestion instead of suggesting against a
position the geometry never took.

**The dwell timer is the thing to respect when testing it.**
`DragAutoConstraintDelay` (400 ms, `Mod/Sketcher/General`) is restarted
by every mouse move, so sweeping a point across the drawing on the way
somewhere else proposes nothing -- and a test that drags and releases
immediately sees no constraint and reads as a failure of the feature.
`tests/gui/sketch-drag-autoconstraint.py` drives a real drag through a
served mirror, because a drag starts from a PRESELECTED vertex and
synthetic mouse events preselect nothing: move, press, one move consumed
by `initDragging`, one move to the target, hold 1.2 s, release. It
asserts one `Coincident` between exactly the two points, and none at all
for the same drag into empty space. Scored against the unpatched build
first: the drag lands identically there and only the `Coincident`
assertion fails, which is what makes it a test of the pick rather than
of the harness ([[measure-the-before-state]]).
`GuiSketchDragAutoConstraint_tests_run`.

### What the sweep deliberately did not close

Two things it found are decisions rather than work, and both are left
open on purpose.

**Upstream's Sketcher GUI test suite.** `GuiTestCase.py`,
`TestOnViewParameterGui.py`, `TestConstraintPreselectionGui.py`,
`TestExternalFacePreselection.py`, `TestPlacementUpdate.py` and
`TestConstraintCommandsGui.py` do not exist here; the fork's GUI tests
are `tests/gui`, driven through a served mirror, which is a different
harness answering a different question (it can preselect; upstream's
cannot reach a browser). Nine rows are the maintenance of tests we do
not have, marked deferred rather than n/a because adopting the suite is
a real option, not an impossibility.

**`c2592271e8`, "remove edit tools from toolbar".** Upstream deleted the
"Sketcher Edit Tools" toolbar -- Grid, Snap, RenderingOrder -- because
those controls moved into its edit overlay. In this fork
`"Sketcher edit tools"` is a live member of `editModeToolbarNames()`, so
taking the commit would remove three buttons with nothing standing in
for them. It stays open as a UI decision.

The rest of what the deletions turned up is noise with no behaviour in
it and is left in the ledger as such: a duplicated `#ifndef` in
`PreCompiled.h`, an outdated comment in `TaskDlgEditSketch.h`, an unused
`posId2`, three dead `__GNUC__ <= 4` guards.

### Two crashes the triage walked into

Having a mechanical answer for "is this already applied" made it cheap
to ask the opposite question, which turned out to be the more useful
one: **is the code this commit fixes even here?** Index every line of
the fork's Sketcher tree, then for each open row check whether the lines
it removed, and the context around them, appear anywhere in that index.
A row with no anchor at all is a fix with no subject -- upstream code the
fork never had.

That found 29 such rows, and two of them are the opposite of n/a: the
fork has the bug, written differently, and upstream's commit is the
signpost rather than the patch.

**`CmdSketcherSnap::isActive()` dereferenced a null action.**
`a479197f0b` and `c828c5d1d1` are titled "Remove unused snap icons and
fix SIGSEGV"; the icon removal is what makes them look inapplicable
here, because this fork's toolbar buttons still show state. The SIGSEGV
half applies exactly. Three commands -- Grid, Snap, RenderingOrder --
swap their icon from `isActive()`, which the command framework calls for
every registered command on every update. `getAction()` is documented to
return null when nothing has put the command on a toolbar or in a menu,
and Grid and RenderingOrder both check it. Snap did not:

    #0  libc
    #1  Gui::Action::setIcon(QIcon const&)
    #2  CmdSketcherSnap::isActive()

It hides behind a default: entering sketch edit mode normally activates
the Sketcher workbench, and building that workbench's toolbars is what
creates the actions. Clear the sketch's "Editing workbench" preference,
edit from another workbench, and the first command update takes the
process down. `2d9e21778b`, with
`tests/gui/sketch-toolbar-command-no-action.py` covering all three --
scored against the unfixed build, where Grid passes and Snap never
reports.

**A knot command crashed on a selection that is not geometry.**
`2aa8f133f3` guards one call site of an activation predicate this fork
does not have, which is why the row read as inapplicable.

    #0  libc
    #1  SketcherGui::isBsplineKnotOrEndPoint(...)
    #2  CmdSketcherIncreaseKnotMultiplicity::activated(int)

`getIdsFromName()` knows Edge, Vertex, ExternalEdge, RootPoint, H_Axis
and V_Axis, and leaves `GeoUndef` for anything else a sketch can have --
a constraint, a face. `getGeometry()` answers null for an id out of
range, and `isBsplineKnotOrEndPoint()` dereferenced it. Both knot
commands read their selection straight into that helper with no check of
the sub-element kind, so selecting a constraint and picking "Increase
knot multiplicity" from the menu segfaulted. The guard goes in the
helper rather than at the call sites, because every caller here arrives
from a raw selection. `ae5238e143`,
`tests/gui/sketch-knot-command-nongeometry.py`.

**What the method is good for, and what it is not.** Searching the whole
tree rather than the named file is what made both of these findable --
`87651cdd4c` reads as unapplied against `DrawSketchDefaultHandler.h` and
is plainly there in `DrawSketchHandler.cpp`, because upstream moved the
code and the fork took the move. But the same looseness makes a positive
answer weak: of eight rows the tree-wide index called applied, hand
reading kept two. `561e521817`'s `#ifndef NOMINMAX`, `4b589088f6`'s
`<limits>` includes, `ecbe21ca03`'s `Q_UNUSED` and half of
`084003e361`'s SPDX headers are all absent here; the index had matched
those lines somewhere else entirely. **Absence is evidence; presence is
a hint.** Every closed row in this sweep was read.

### The two families that were left open, and how they closed

The constraint-tool hints, sized here as eleven rows, were taken the
next session and are section 7a below.

**The icon refresh, closed 2026-09-22** -- six rows, sized as one art
decision and settled as one, by rendering the pairs rather than reading
the diffs: fork against upstream at 96 px and at the 24 and 16 px the
icons are actually used at.

**One of the six was not an art question at all.** `24fe47830e` redrew
nothing: it swapped the hyperbola's and parabola's start and end art,
which had been on the wrong points. Settled against the KERNEL, not
upstream's opinion -- build each curve with its branch opening up and
the normal out of the screen, which is what the icons draw, and ask the
arc for its own points:

    hyperbola  start=(1.18, 3.09)  end=(-1.18, 3.09)
    parabola   start=(1.00, 0.25)  end=(-1.00, 0.25)

Start is the RIGHT arm for both, and this fork's start icons greened the
left one; the elliptical arc's icons next door already followed that
convention. This fork's `..._End_Point.svg` was byte-for-byte upstream's
`..._Start_Point.svg`, so exchanging the two files' contents lands on
upstream's pairing without taking any redrawn art (`684e30ada0`).
**The lesson is cheap to state: "cosmetic" is a guess until someone
renders the art.**

**The other five: upstream's art adopted** (user, 2026-09-22), for the
eight files this fork actually has -- the toggle-construction pair, the
carbon copy pair, the regular-polygon pair, `Sketcher_Intersection.svg`
and the external-geometry cursor. Checked, not assumed: the cursor still
carries the `id="crosshair"` group stroked `#ffffff`, which is the key
this fork's `setSvgCursor` recolours (the fork's old file had a second
`#ffffff`, but it was Inkscape's `pagecolor` metadata, not paint); and
every adopted file renders under QtSvg, which is a narrower SVG than the
rsvg used to preview them.

**Four files of `f0ba161bdf` were NOT taken**, and they are why that row
is marked taken-in-part: `Sketcher_Projection.svg`, its `_Constr`,
`Sketcher_Intersection_Constr.svg` and
`Sketcher_Pointer_External_Intersection.svg` do not exist here, are in
no `.qrc` and are referenced by nothing. They are art for upstream's
split of external geometry into projection and intersection, which this
fork has not made; adopting them would be adopting a feature's shape
through its icons.

**A consequence to be aware of**: this fork pairs
`Sketcher_Intersection` with its own `Sketcher_IntersectionDefining`
(defining versus non-defining external geometry -- not upstream's
`_Constr`, which is construction mode). The two were the same drawing in
two colours. Upstream's intersection icon is a different drawing
entirely -- a solid cut by a plane -- so the pair is no longer a pair.
Left as it is: inventing a matching "defining" variant is authoring art,
not adopting it.

## 7b. ViewProviderSketch.cpp: the first pass (session 88)

`Gui/ViewProviderSketch.cpp` is the largest block left -- **71 of the
332 open rows** when this started. The mechanical test that closed 55
rows tree-wide closes **one** here, and that one is a false positive:
the file has diverged too far for line presence to mean anything, and
these are the rows earlier sweeps could not settle. So they are being
read.

**Nine settled, 71 -> 62, and one of them was a live defect.**

| row | verdict |
|---|---|
| `dc2aec50d4` + `4b50d72769` | **taken** `90f8e7513a` -- the defect below |
| `22120fa597` | have: `DrawSketchHandler.cpp:2126` already calls `Base::toDegrees`; `ViewProviderSketch::getRotation` does not exist here |
| `df08c0ce8d` | have: the fork reads `LeaveSketchWithEscape` directly (`ViewProviderSketch.cpp:7799`) and has no ParameterObserver entry to carry the old key |
| `d806b4e5f3` | have: `Gui/Document.cpp:628` computes `_editingTransform` for EVERY view provider at setEdit, and 1220 keeps it live -- more general than upstream's per-VP call |
| `94c486184c` | n/a: no `editingCancelled` member here; it belongs to the cancel-tool family (`189d86ee53`) |
| `3fa5f1c236` | n/a: the fork includes neither TopTools header |
| `69b04222cc` | n/a: a fix on the bounding-box rework (`5587b48a0f`) the fork does not have |
| `4b589088f6` | partial: of its nine files only `App/PropertyConstraintList.cpp` uses `numeric_limits` here and still lacks the include |

### The defect: entering a sketch showed the datum it is attached to

`ViewProviderSketch::setEdit` runs a TempoVis snippet that, with
ShowSupport on, shows what the sketch is attached to -- except the datum
it is mapped onto, which is its own support and would only clutter the
view it is being edited in. The exclusion is **by class name**, and it
named `PartDesign::Plane` alone.

After the datums port an origin plane is an `App::Plane`, so the test
stopped matching and the origin came up with every sketch attached to
one -- which is most of them. Measured before the fix: a sketch mapped
to `XY_Plane` leaves `setEdit` with that plane visible.

**Taken, in two steps, and the second one corrects the first.**
`90f8e7513a` kept the fork's older `PartDesign::Plane` exclusion beside
the two new ones, reasoning that dropping it "would start showing those
instead -- the same bug with the other class". **That was wrong**, and
`198f28df07` drops it.

Upstream's tip excludes `App::Plane` and `App::LocalCoordinateSystem`
and nothing else, so a datum plane the USER made IS shown when a sketch
attached to it is edited. That is a choice, not a leftover of the datums
move: upstream still defines `PartDesign::Plane` as a `Part::Datum` and
still creates it from the Datum Plane command (`Command.cpp:227`),
exactly as this fork does (`:290`), so the class is live in both trees.
`dc2aec50d4`'s message settles the intent -- the special handling "was
mishandled after move to core datums", so it was always aimed at the
ORIGIN planes, and `PartDesign::Plane` was the pre-datums spelling of
that rather than a second intent.

**The general trap, and it is the one that matters for the PartDesign
port.** A conservative adaptation that "changes nothing else" can still
be a silent behavioural divergence, and a cherry-pick port works by
asking whether what a commit did is already true here -- so an
undocumented difference makes that question answer itself wrongly later.
Before preserving fork behaviour on the strength of "it compiles and
nothing else moved", check whether upstream's replacement was FORCED by
a type move or CHOSEN: read the commit message, and check whether the
old type is still created.

Guarded by `tests/gui/sketch-support-visibility.py`, **ctest 780 -> 781**,
which pins BOTH halves -- origin hidden, user datum shown. The pair is
the point: without the second, the fix could be "exclude everything",
which hides the origin too and passes the first. Scored both ways --
with the `PartDesign::Plane` exclusion restored the datum check fails
and the origin check still passes, so neither claim is vacuous.
A one-line fix would normally speak for itself; this one gets a test
because **a class rename that silently disables a type test leaves
nothing behind to notice** -- no error, no warning, just a condition
that is never true again. That failure mode is the general lesson of
this row, and it is not specific to the Sketcher.

### What the remaining 62 look like

The pattern to expect, and the reason the mechanical test is useless
here: most are **fixes on top of upstream refactors the fork never
took** -- the Ctrl+A/select-all family, the screen-space preselection
family, the bounding-box rework, the cancel tool, the placement
preview. Judged one by one they each read as "open"; judged by family
they are one decision each, about whether to take the parent. **Size
the family before reading its rows** -- the hints family (section 7a)
made the same point from the other direction.

### The grid family (session 89): 62 -> 59, and a dead preference page

| row | verdict |
|---|---|
| `1ec1f1506e` | have: the fork's button path snaps eagerly (`snapPoint()` in `mouseButtonPressed`) before `pressButton`/`releaseButton`. Upstream's #25076 came from deferring that path to a handle, which the fork deliberately did not do (section 7, the snap-handle pick) |
| `03bc80c060` + `a357868691` | **taken** `d7bf1c3754`, as one design: grid on and solid for a new sketch, 60% transparent so solid lines stay quiet |

**The family's real finding was not in any row.** Sizing the transparency
pick meant finding where the Sketcher hands its grid preferences to
`PartGui::ViewProviderGridExtension` -- and nothing did. The extension
reads no preference itself; upstream calls its setters from
`ViewProviderSketch`'s ParameterObserver, this fork's view provider has
none, and the 2023 merge that brought the extension in (`68326945dd`)
kept the extension and dropped the calls. So the whole **Grid display**
preference page -- pattern, width and colour for minor and major lines,
subdivisions, the auto-spacing threshold -- wrote values no code read.
`"GridLinePattern"` even sat in `OnChange`'s redraw list, restarting a
timer that never touches the grid. Fixed first, on its own, in
`6603c453f6` (`updateGridParameters()`, at `setEdit` and live from
`OnChange`); the same merge had also left `GridAuto` defaulting off
while the page showed it ticked, restored with the pick.

The general point is the one section 7b keeps making: a merge that
resolves a file by keeping the fork's side can drop the *callers* of a
new mechanism while keeping the mechanism, and a setter nobody calls
fails as silently as a type test nobody matches. Grep the setters'
callers, not just their definitions.

Guarded by `tests/gui/sketch-grid-preferences.py`
(`GuiSketchGridPreferences_tests_run`, ctest 781 -> 782), which reads the
grid's Coin nodes. Scored before each commit: 7 of 9 checks fail before
the wiring, and the pick's 5 new ones fail before the pick. Checked in
pixels too: 60% draws distinct from both opaque and invisible, in render
cache mode 0 and in mode 3 alike. **Correction**: `d7bf1c3754`'s message
says the grid composites "exactly as in mode 0" with the renderer on.
That was measured wrongly -- **the fork's default `RenderCache` is 3**, so
the "mode 0" leg was mode 3 too. Re-measured with `RenderCache` forced to
0: transparency works there as well (the 60% grid is visibly fainter),
but the two modes do NOT draw the grid identically (4816 vs 8082 changed
pixels per step). A probe that means mode 0 must SET it, and should
confirm it: `saveRenderDump` raises when no renderer is active.
**Trap met on the way**:
a GUI test's configuration directory outlives a run, so a preference the
last run stored reads as a default in the next; the test clears its
entries at both ends.

### HiDPI (session 89): 59 -> 57

`418c09899b` + `37b4560893`, **adapted** `cfad3e76c4`. Upstream's fix
lives in `EditModeCoinManager`, which the fork does not build, so it is
taken as its end state at upstream's tip inside the fork's own
`initParams()`. The defect was live: sizes were scaled by logical DPI /
96, and Qt 6 keeps that near 96 on a scaled screen and puts the scale in
the device pixel ratio, so on a 200% screen every edit-mode size -- line
widths, points, markers, Coin text, icons, datum labels -- was half.
Scaled by `ViewScalingFactor * ratio` now; at ratio 1 and 96 dpi nothing
moves.

Two fork-specific points. **Datum labels were then switched to
upstream's sizing** by user ruling, after an on-screen comparison
(`50af4ee463`): `SoDatumLabel` takes points, and the fork had handed it
the pixel value, drawing labels a third larger (14 pt vs 11 at the
default font and 96 dpi). And **the ratio comes from the edit viewer**, so a
served client's mirror answers with that client's -- the browser's
drawing buffer is in device pixels, so a 2x phone was drawing sketch
lines at half width as well.

`37b4560893`'s re-size on a screen move hangs off the viewer's
`devicePixelRatioChanged`, through the same timer a preference change
uses; untested, since xvfb fixes the ratio for the process. Guarded by
`tests/gui/sketch-hidpi-sizes.py` at `QT_SCALE_FACTOR=2`
(`GuiSketchHiDpiSizes_tests_run`, ctest 783), which checks the ratio
first -- without it every other check is vacuous.

### The placement preview (session 89): 57 -> 55, nothing to take

`4cd81136b6` added a live preview of the sketch's placement during an
edit; `26723cf209` withdrew it a week later, together with `d806b4e5f3`
(#25478), in PR #26554. Net upstream: nothing. **Superseded / n/a**, and
the feature is already true here anyway -- `Gui/Document.cpp` re-derives
the editing transform whenever any placement property of an edited
object changes, for every view provider, not just the sketch's.
`26723cf209`'s one surviving change moves a `ShapeMaterial` exclusion in
`updateData`; no Part view provider in this fork reads `ShapeMaterial`.
Upstream's `TestPlacementUpdate.py` survives the revert but is no longer
imported by `TestSketcherApp.py`; not taken.

**The PR's third commit, `450789e77b`, has no ledger row** -- it touches
only `src/Gui/Document.cpp`, outside the ledger's scope -- but it is the
replacement fix for #13852, so it was checked here. Upstream moves
`resetIfEditing()` after the edit reference is deduced from the
selection, because the outgoing sketch's `unsetEdit` re-selects itself
and clobbered the selection the incoming one was picked through. The
fork has the same order and the same re-select, and is **not** affected,
measured: sketch A in edit, then sketch B picked through an `App::Link`
rotated 180 deg about X -- the switched edit gets exactly the transform B
gets on its own. The fork's `setEdit` asks the selection *context*
first, and that still names the Link path after the re-select.

### Select All and bulk selection (session 93): 55 -> 48, and a crash

Seven named rows -- select-all (`3b76d77ed8`, `95840a79d3`, `061e185e7f`,
`ca8bfc6180`, `e278d22d42`), the bulk-selection speed-up `0fa707c523` and
its box-selection follow-up `af053f19f5` -- plus `887c8d3bdf` from the
subject search, and the core half of `b9db90ea20`, which the ledger had
marked have(sync) on its Sketcher files alone. Five fork commits:

- `26ce6d0b25` **A bulk selection reached no observer of a sketch edit.**
  `Selection().addSelections()` pauses notification, and past
  `MaxSelectionNotification` (100) the core replaces the queued adds with
  one `SetSelection`, "re-read the selection". `ViewProviderSketch` and
  both task panels ignored it: 200 of 200 selected, 0 coloured, 0 rows.
  All three now re-read -- the instance they *observe*
  (`SelectionObserver::observedSelection()`, new), since a served edit's
  view provider listens to its client's instance, not `Gui::Selection()`.
- `4d7427e0ff` **A pre-existing SIGSEGV**: `sketchClosed()` cleared the
  elements tree but not `itemMap`, and the panel keeps observing until it
  is deleted. Re-edit the same sketch in one event-loop turn, select an
  edge, crash. Found because the re-read walks every `itemMap` entry.
- `e5ea2e9118` Box selection as one `addSelections()` batch. Measured,
  release to selection: 600 elements 0.314 s -> 0.032 s, 3000 elements
  3.738 s -> 0.120 s; the old cost grew with the square of the count
  (gdb stack samples: the elements tree's per-item `setSelected`, the
  selection stack copying the whole selection per add, the sketch's
  per-item recolour).
- `7a31d66c3b` The spreadsheet keeps Ctrl+A (upstream `4f4e9244e6`), taken
  first because of the next one.
- `745518e1e8` Select All: Ctrl+A bound to `Std_SelectAll` (upstream bound
  it in `3b76d77ed8`, outside the ledger's paths), the command asks the
  edited view provider first and is `AlterSelection` only (with the
  default `AlterDoc` the sketch's task dialog disables it), and
  `ViewProviderSketch::selectAll()` at upstream's end state.

**The fork route, not upstream's.** Upstream buffers in every observer
(`selectionBuffering`, a selection buffer flushed on a timer in each panel
-- and `887c8d3bdf` is that buffer's dangling-pointer crash). The fork's
core already coalesces a paused batch; the observers only had to honour
it. So no timers were imported, and `887c8d3bdf` is n/a.

Adapted: `selectAll()` asks the sketch for each element's start/end/mid
vertex index instead of counting vertices per geometry type (what
`e278d22d42` had to fix, and the fork's text geometry is not in that
list). Not taken: `061e185e7f`'s timer handing focus back to a list after
a click -- a QTest click keeps focus in both lists here, measured.

Guards: `GuiSketchBulkSelection_tests_run` (batches and a synthetic desktop
box drag, counted by a Python selection observer: 600 single adds before,
one SetSelection after), `GuiSketchReEditSelect_tests_run`,
`GuiSketchSelectAll_tests_run`, `GuiSpreadsheetSelectAll_tests_run`.

Two lessons. **A row's decision is only as wide as the ledger's path
filter**: `b9db90ea20` read have(sync), and `3b76d77ed8` read as touching
one file, while both carried a core half the fork lacked -- the second
one a global shortcut. List a family's commits with `--stat` over the
whole tree. And **a synthetic desktop box drag works** (QMouseEvents sent
to the `View3DInventorViewer`, pixels from `view.getPointOnViewport`, y
flipped), unlike synthetic preselection; keep the geometry off the axes
and hide other sketches, or the press lands on something.

Left open: clicking a row of the elements list with synthetic QTest
input selected nothing (the constraints list did). The panel acts on the
row it saw through hover (`itemEntered`), so this may be the known limit
of synthetic hover, not a defect -- not established either way.

### The elements list: the fork's own, extended (session 93): 308 -> 300

**User ruling (2026-09-25): keep the fork's elements list, with one icon per
row; do not port upstream's list, whose rows carry an icon per part.** What
upstream's list can do that the fork's could not was added to the fork's
list instead. Seven commits:

- `1434da42d1` **A geometry on a hidden visual layer is not drawn.** The
  fork stored a layer per geometry and `VisualLayerList` per sketch, and
  its `draw()` read neither: a layer changed nothing on screen. Hidden
  geometry is now not drawn, not picked, not boxed and not taken by Select
  All from the view. The vertex-to-point map defaulted to 0 -- the root
  point's slot -- for a vertex not drawn; it is -1 now and six readers
  check.
- `739c19f67f` The row checkbox: ticked shown, unticked the hidden layer,
  one transaction, applied a turn later (the change rebuilds the list that
  is delivering the checkbox's signal).
- `e390940d01` **Mode** becomes upstream's checkable filter -- kinds
  (Normal, Construction, Internal, External) and nine geometry types,
  combined -- in the Mode button's pop-up, stored in upstream's
  `ElementFilterState` parameter bit for bit.
- `fc0df4590b` **The single icon is a drop-down button**: it lists the
  parts of that element with upstream's per-part icons and picks one (Ctrl
  adds); the icon and the Name column follow the part selected last. The
  global Type combo, its Z key and "Auto-switch to Edge" are retired.
  Rebuilding the scene selection from the rows now pushes every selected
  part, not the first. Both panels stop observing the selection when the
  edit dialog closes.
- `2ebaa7626d` Icon size is a preference, `ElementIconSize` (32 px), and
  the arrow has a strip of its own left of the icon.
- `78f0b1ef1e` **Layer 1 is drawn dashed.** Upstream does not: its
  `EditModeGeometryCoinManager` only switches coin layers on and off, and
  `VisualLayer::getLinePattern()` has no caller, so upstream's
  "discontinuous line layer" draws solid. The fork's curves are now two
  `SoIndexedLineSet`s over one coordinate and material list, the second
  under the layer's pattern.
- `a0dab1d6f6` The list's context menu: Layer > Layer 0 (solid), Layer 1
  (dashed), Hidden.

Ledger: the rows that are upstream's list itself are declined citing the
ruling (`122f163d0c`, `da6a4fe57b`, `b46ba096b2`, `8fd9c19013`); the
checkbox fixes are adapted (`2fac012226`, `34b6b36547`, `6bed2e663e`);
`00f547d67c` is n/a. The 17 elements-panel rows still open are general --
Qt warnings, texts, auto-scroll, selection speed -- and go with their own
families.

### The crash fixes (session 94): 48 -> 45, and one crash of the fork's own

Three upstream rows fix crashes in `ViewProviderSketch.cpp`. One was live
here, one latent, one not reproduced; the test written for the first
found a fourth crash that was the fork's own.

| row | verdict |
|---|---|
| `16aff10544` | **adapted** `79e570b5b4` -- undo with the button held, below |
| `d3d6459484` | **taken** `ecf0c288e7`. The listener was a raw pointer, never initialised and dangling after `unsetEdit()`. Latent here: `unsetEdit()` only follows a `setEdit()` that created it |
| `955efa639e` | **adapted** `2407af8624`: the catch sits in `setEditViewer()`, where the fork runs the first solve. Escaping from there skipped `attachViewer()`, the base call and the caller's event callback. No failing sketch reproduced |

**Undo with the button held.** A press or drag holds what it acts on by
index (the preselected point, edge or constraint, then `Dragged` and
`DragConstraintSet`), and an undo can remove that element. Measured
before the fix: a label drag whose constraint was undone away hit a
SIGSEGV in `moveConstraint()` on the next move, because the id is read
from the list unchecked. A point drag whose line was undone away threw
on every move. Its release then opened a command that aborted, and
opening it had already cleared the redo stack, so the line could not be
redone. Upstream resets the drag in the undo/redo slots. The fork's
version also drops an unmoved press (the SELECT modes act on the
preselected index at the next move or release). It also closes a label
drag's active transaction, whose id would otherwise stay set and stop
the next label drag from opening its own.

**The fork's own crash** (`06cc7a7a87`). After a label drag lands, its
constraint stays preselected. Undoing that constraint away runs a solve
inside the undo, and that solve redraws before any slot can prune the
id sets. The result was a SIGSEGV in `updateVirtualSpace()` and, with
that guarded, a second one in `updateColor()`. Both were measured, and
both now skip ids outside the list. Upstream's `updateVirtualSpace()`
has no (pre)selection override at all, so this is fork code.

Guarded by `tests/gui/sketch-undo-during-drag.py`
(`GuiSketchUndoDuringDrag_tests_run`). It was scored per fix: with the
reset disabled it fails the redo check and then crashes in
`moveConstraint()`; with the guards reverted it crashes in
`updateVirtualSpace()`.

**Harness notes, reusable.** It is a desktop test, not a served one.
Setting the preselection through `Gui.Selection.setPreselection()` fills
the view provider's preselect state, as a list hover does, and a press
then starts the drag with no pick. That sidesteps two problems:
synthetic moves preselect nothing, and datum labels are hard to hit over
the mirror. A label's pick box is sized in world units by whichever view
last drew it, so the desktop's zoom decides how big it is on a client.
It is also lost to an axis running under it. After a drag lands, wait
about 0.5 s: its recompute redraws off a timer and drops a preselection
set before it, and the next drag then silently never starts. A drag in
progress shows only in the edit scene's `SoCoordinate3` nodes, because
`Geometry` is written on release.

### Box selection (session 94): 45 -> 40

| row | verdict |
|---|---|
| `9bff63e38d` | **taken** `08c58fabce`, for parity. The box's redraw now draws the object's geometry, not the solver's copy. Upstream's symptom (construction lines turning solid) cannot happen here, measured: the fork's toggle command solves on the way out, construction is a colour read off the object, and `draw()` does not recolour while the mode is still the rubber band's. All the flag decides here is draw order |
| `39329e547f` + `e469eb5ccb` | **adapted** `dbf819f658`. A right press during a box already cancelled it here (the both-buttons branch), but the right release then found the edit idle and opened the context menu, measured. The block flag is set where the fork cancels and cleared by the next right press |
| `7b85239093` | **adapted** `8b24f4785f`: blue and solid left to right (window), green and dashed right to left (touch). Uses upstream's `StyleParameters.h`, resolved through `Gui::Application`'s manager. The fork's `Rubberband` carries both colour and stipple into its Coin overlay, so mode 3 draws them too |
| `a5bf17b144` | **adapted** `d15b8e026a` (core half, taken on request), **have** (Sketcher half). The Sketcher half restores a gate's forbidden cursor when the pointer leaves geometry, which the fork already does: `blockedPreselection` plus `rmvPreselect()`. The core half is below |

Guarded by `tests/gui/sketch-box-selection.py`
(`GuiSketchBoxSelection_tests_run`). The menu and colour checks fail
without their fixes. The construction check passes either way and
guards what the user sees.

**Box selection under a selection gate** (`d15b8e026a`, core). The
fork's box already selected only what an active gate allows: it adds
each pick through `addSelection()`, which asks the gate. But every
element turned away counted as a refused click, with a status bar
message, the forbidden cursor and a beep. That was 15 refusals for one
box over a Part box under a vertex-only gate, counted with a Python gate
that counts what it refuses. Now `addSelections()` sets `gateQuiet` for
its batch, a refusal under it returns without a word (as upstream's
batch does), and the box command adds its picks as one batch.

Upstream's `getGatedTypes()` and `getFirstVertexFromSubElement()` were
not taken. Upstream's box stopped at the first element type with a hit,
so a vertex filter needed them to reach vertices at all. The fork's box
visits every type and finds all 8. Measured on 400 boxes (2400 faces):
the gated box takes 0.08 s against 0.9 s ungated, so skipping types
could save a fraction of 0.08 s. Guarded by
`tests/gui/box-selection-gate.py` (`GuiBoxSelectionGate_tests_run`).

**Harness traps.**
- A box pressed within the double-click interval of the last click is
  not a box. The test waits 0.8 s before each one.
- The both-buttons cancel reads `QApplication::mouseButtons()`, which
  `sendEvent()` never updates. `QTest` mouse events go through the
  window system and do update it.
- A context menu's `exec()` is modal, so a timer closes and records any
  popup.
- Check that the test's coordinates are on screen: the first empty box
  sat below the fitted view. Events there still work, but a picture of
  it shows nothing.

### Arc labels and drags (session 95): 40 -> 36

| row | verdict |
|---|---|
| `646b4381f9` | **adapted** `01fd3490ea`. Ledgered `partial(sync)`, but the `ARCLENGTH` datum type was not here at all: an arc length constraint drew an EMPTY label, measured, and dragging it went down the radius code, which rewrote its `LabelPosition` (0 -> 30.09). Taken at upstream's end state (`calculateArcLengthGeometry`, large-arc case included), fed from the fork's own `drawConstraints` |
| `f3e1e6cec0` | **adapted** `01fd3490ea`. An arc's length and angle labels drag by the cursor's distance along the arc's middle direction, negative past the centre. The fork's angle drawing opened its number gap from `r` with its sign, which closes the gap once `r` is negative; both paths use `abs(r)` now |
| `7bcaa766de` | **taken** `01fd3490ea`, the factor with it |
| `df867a25b2` | **adapted** `01fd3490ea`, the arc case: an arc angle's end lines run back to the arc, through the centre when the label is past it. Ledgered `n/a(uncompiled)` -- see below |
| `2cd45b07f7` | **adapted** `e8f9e4ac1d`. A selected arc grabbed by its centre dragged its rim, and a conic grabbed by its edge jumped its centre to the cursor, both measured. Not "rigid", whatever the upstream comment says: the solver holds only the centre (the same `initMove` upstream), so the radius grows on a centre drag, selected or not; what is restored is the unselected behaviour |
| `eb61ee36a6` | **have**. The fork's press handler snaps `x, y` for the release too, and `setRelative()` snaps the start |

**The number sits outside the arc, in pixels.** Upstream puts the arc
length's number one text height beyond the dimension arc, in world
units. The capture for the backend has no camera, so a screen distance
cannot become a world one there. The glyph quad is emitted in native
pixels behind the anchor, so it is shifted in its own y by the glyph's
height (`textShift`); GL, the pick and the bounding box have a view and
use `imgHeight`.

**`n/a(uncompiled)` hid a missing feature.** `EditModeConstraintCoinManager.cpp`
is not compiled here (decision 3): the fork draws constraints in
`ViewProviderSketch::drawConstraints`. But a commit that changes what
upstream's manager DRAWS is a change the fork's drawing may lack too.
`df867a25b2` was one. So are the line cases of the same work: the fork's
`SoDatumLabel` reads an angle's end-line lengths (`param4`, `param5`, the
`74dd736e3c` half that is here), but `drawConstraints` set them only for
an arc, so a line or line-line angle label kept the pixel-minimum ticks
(`827781ab3f`, `dca00ec80e`). Taken after the family in `4b4e56c5d6`, see
"Line angle end lines" below. The other n/a rows under that file deserve
the same reading.

**Found on the way: a label never followed its constraint under bgfx.**
`4711c5578f`. The capture reads a datum label through companion shapes
that are not below it, and no field change reached them: set a DistanceX
from 60 to 30 and "60 mm" stayed drawn across the old 60 units. Every
drawing check above failed for that reason first. Guarded by
`tests/gui/sketch-datum-follows-mode3.py`.

Guarded by `tests/gui/sketch-arc-labels.py` (12 checks, 11 fail before,
also run in mode 0) and `tests/gui/sketch-drag-arc-conic.py` (3 of 4 fail
before).

### Line angle end lines (session 96)

| row | verdict |
|---|---|
| `827781ab3f` | **adapted** `4b4e56c5d6`. Each line of a line-line angle gets an end line from the label's arc to its far end (the line inside the arc) or its near end (beyond it); none where the arc crosses the line. Directions normalised as upstream |
| `dca00ec80e` | **adapted** `4b4e56c5d6`. A single line's angle is from the horizontal through its middle: that reference is drawn from the middle to the arc, and the line's own end line back to its end when the arc is past it |

Both rows stay `n/a(uncompiled)` in the ledger (the file is not built
here), so the open count does not move: 288. The feed is all that was
missing -- both leader paths already read `param4`/`param5`.

Guarded by `tests/gui/sketch-line-angle-labels.py` (5 end-line checks, all
5 fail before in mode 3 and in mode 0). Its geometry sits 15 above the
sketch's X axis: the axis is drawn in a red the label check cannot tell
from a label's, and a first version on y = 0 passed its horizontal
probes before the fix.

### Screen-space preselection (session 96): 36 -> 31

Upstream reworked the edit-mode hover pick over ten commits: a constraint
anywhere on the ray first (`2f3161f312`), then a screen-space scan that
projects every point and every curve polyline vertex to the viewport on
each mouse move and takes the nearest (`b8b8a3e2a0`, `96ab8a5be3`, and the
n/a `57650b8067`, `400f6b3ac5`), a click that re-detects and so needed the
hover's result cached to agree with it (`9efe08b33b`), and finally typed
candidates resolved by a priority table (`b178a7aede`, `93feb3ce51`,
`b9368b17a8`). It lives in `EditModeCoinManager`, which is not built here.

What it is for is written down in upstream's
`SketcherTests/TestConstraintPreselectionGui.py`: a vertex beats a label
over it, a curve beats a dimension line over it, a dimension's number
beats the curve or the axis under it, and the PointOnObject icon of issue
25840 keeps its hit area on a slightly tilted view. That file is taken
unchanged and run on the fork's own pick, through upstream's probe
`SketcherGui.getActiveSketchPreselection()` (`882d030012`; it needed
`setLabelDistance`/`setLabelPosition` from Python, `b9b3ddbf57`):

| mode | before | after `b8c15f572f` |
|---|---|---|
| 0 (GL) | 5/5 | 5/5 |
| 3 (bgfx, default) | 3/5 | 5/5 |

**The failure was not the priority rules.** In mode 3 a dimension's
number could not be picked at all -- a lone distance label with nothing
under it was not found within 48 px. `SoDatumLabel`'s ray pick is the box
of the number, sized by `imgWidth`/`imgHeight`, and only `GLRender` sized
them for the view; in the render cache modes it never runs. The pick now
sizes the box for the view it picks in (`b8c15f572f`). Also measured:
the fork's pick already takes the element nearest on screen -- two
vertices, and two parallel lines, 8 px apart switch at the midpoint to
the pixel.

| row | verdict |
|---|---|
| `2f3161f312`, `b8b8a3e2a0`, `9efe08b33b`, `96ab8a5be3` | **declined**, user ruling: the fork keeps its own pick. It meets upstream's own tests, and the scan costs a projection of every point and curve vertex per mouse move on the sketches this fork is tuned for |
| `57650b8067`, `400f6b3ac5`, `b178a7aede`, `93feb3ce51`, `b9368b17a8` | **declined** with them (n/a anyway: `EditModeCoinManager` only) |
| `2ba97bf783` | **n/a**, user ruling: it extends upstream's `Std_ClarifySelection`; the fork has `Std_PickGeometry` instead |

Guarded by `tests/gui/sketch-preselection-upstream.py`
(`GuiSketchPreselectionUpstream_tests_run`), upstream's file in mode 3.

### Double click and cancel (session 96): 31 -> 48, after reopening 20

| row | verdict |
|---|---|
| `6db820a580`, `a9bff78974` | **adapted** `2239784e9b`. Ledgered `have(sync)`, but ABSENT: a double click on an edge only logged. Now it selects the wire the edge is part of, on the release (a new `STATUS_SELECT_Wire`, as upstream), and a second one deselects it. Upstream rescans every remaining edge after each one it joins; here endpoints are bucketed by position and the wire walked once |
| `0b1187b2cd` | **adapted** `2239784e9b`: external edges join the wire |
| `321a782eff` | **adapted** `5cf7e29e3b`. Double clicking the sketch in edit set its edit again, which dropped the selection; it now aligns the view |
| `189d86ee53` | **adapted** `494cb6f23f`, `7630034293`. Ok/Cancel in place of Close, `Sketcher_CancelSketch`, a Leave drop-down on the edit tool bar. Cancel reverts by **undo** to where the history stood when the edit began (user ruling), so it can be redone; upstream restores a copy taken on entry. The history keeps `MaxUndoSize` (20) steps, so for a longer edit, or with undo off, the copy is restored instead, in one "Cancel sketch editing" step (user ruling). The copy costs 11 ms on the largest corpus sketch. Not taken: the panel's widget reorder, and the `Base/Reader.cpp` change only upstream's restore needs |
| `facca5c426` | **adapted** `494cb6f23f`: Esc presses Ok, through the new `TaskDialog::roleOnEscape` |
| `8d3c8076b2` | **superseded**: upstream reverted it in `facca5c426` |

Guarded by `tests/gui/sketch-double-click.py` (3 of 6 fail before) and
`tests/gui/sketch-cancel-edit.py` (11 checks).

**`have(sync)` is wrong for `Gui/ViewProviderSketch.cpp`.** The status
means "every file it touches was synced and it predates that file's
baseline", but the fork kept its own view provider. Of the 28 rows so
marked, the lines each one added were looked for in the fork's file
(whitespace aside): 6 are there (88-100%: `0bef2e927b`, `e135f68e8a`,
`4ca8e3b283`, `33abd923b3`, `b64e3e750f`, `b9db90ea20`) and stay; 2 are
taken above; the other 20 are reopened as `open`, undecided -- 18 with
0-30% of their lines here (`4164919e58`, `a38e73135e`, `9961f2949a`,
`7075e3c1d5`, `e7c11a01be`, `5c7d287f6b`, `8def94e6f8`, `22a98d81f0`,
`a1487106ab`, `5f74b4b299`, `e260cf5c8a`, `3da4b59b37`, `fd28d94f6a`,
`fbd7f7090c`, `1eb8496aae`, `738a044f3c`, `7f984811e8`, `fa61131590`),
and the compiler-warning pair `51a01b9e2b`, `d92267c6a7`. Absent lines may
still be a change the fork made its own way; that is the triage.


**Harness notes.** `Constraint.LabelDistance` is read-only from Python,
so the test places labels by dragging them. A label drag that lands
leaves the label preselected and drawn in the preselection colour, which
a red-pixel check reads as nothing: clear it with
`Gui.Selection.clearPreselection()` (there is no `removePreselection`).

### The twenty reopened rows (session 97): 48 -> 27, with `97e7b9d1f2`

Each row's lines were looked for in the fork's own terms, not as text:
the fork's view provider is its own design, so a missing line is only a
lead.

| row | verdict |
|---|---|
| `4164919e58` | **have**, the fork's own way: `generateContextMenu` folds the preselection into the selection (Shift keeps the rest), counts `ExternalEdge` as an edge, and offers Copy/Cut/Paste, and Paste on the empty menu |
| `a38e73135e` | **have**, the fork's own way: `moveConstraint` clones and `set1Value()`s inside a "Drag Constraint" transaction opened on the first move, so undo restores the label; `sketch-undo-during-drag.py` |
| `3da4b59b37`, `51a01b9e2b`, `d92267c6a7` | **have**: `convertSubName` in `SEL_PARAMS`; the warning fixes are here or need `_DEBUG` with `NDEBUG` |
| `738a044f3c` | **have** (Sketcher part): `attach`/`onChanged` already call `ViewProvider2DObject`. ShowPlane itself is a Part/Gui feature, outside this ledger |
| `9961f2949a`, `7075e3c1d5`, `1eb8496aae`, `fa61131590` | **n/a**: no camera sensor; the fork tests `FirstPos` before the swap upstream fixed; no object freeze; no parentless box in `setEdit` |
| `e7c11a01be`, `5c7d287f6b`, `fd28d94f6a`, `7f984811e8` | **superseded**: the headlight and draw-style switch upstream removed again in `b07caa732e` (Revert #14386 and #16378); the fork never had it |
| `5f74b4b299` | **adapted** `b386a561f5`: the lock around the `TempoVis` read only, not over the whole function and its solve |
| `22a98d81f0` | **adapted** `3b5af6dfe9`: an edit entered from the tree left the keyboard there and Escape did nothing (measured); `setEditViewer` focuses its view through `ViewerContext`, which a mirror ignores. Upstream's second half, focus after a purged tool, was never lost here (measured) and is not taken |
| `a1487106ab` | **taken** `3b5af6dfe9`: measured, a click with the line tool put the edit cursor back until the next move |
| `8def94e6f8`, `e260cf5c8a`, `97e7b9d1f2` | **adapted** `51d863d806`, see below |
| `fbd7f7090c` | **n/a** (session 98, user ruling): the in-edit highlight overlay made a hover echo cost 0.035 ms, so the twice-per-hover repaint it removes is harmless; see below |

Guarded by `tests/gui/sketch-focus-cursor.py` (2 of 6 fail before) and
`tests/gui/sketch-auto-color.py` (23 checks).

**AutoColor.** A sketch's edge and vertex colours follow
`SketchEdgeColor`/`SketchVertexColor` and stay out of the file, so a
sketch drawn on a dark theme is not stuck with its colours on a light one.
Four things differ from upstream:

- **Six properties, not two.** A colour is kept three times here, the
  colour, the per-element array and the material, and each is written
  when the colour is. Upstream marks only the colour Transient, and its
  file still carries the other two.
- **Which file it was is read off the colours, not the Touched bit.**
  Upstream asks whether restoring touched `AutoColor`. Here writing the
  value a property already holds is silent (`Property::hasSetValue`, the
  recompute optimisation), and a file's "on" equals the constructor's, so
  only an "off" is heard -- and the shared defaults block, which pastes an
  elided "on", is silent the same way. What does show is the colours'
  status: the restore gives each recorded property the status its file
  saved, a Transient property is never left to the defaults block, and
  the colours were saved Transient exactly when automatic.
- **A preference change is not a modification.** `NoModify` on the
  colours is not enough: every view provider change also touches the
  object's `ViewObject`, which the Gui document counts as one. The update
  puts the flag back as it found it; nothing else changes there.
- **The face colour follows the fork's preference** (session 113, user
  ruling, `94d76ec9c2`). Upstream drives `ShapeAppearance` from
  `SketchFaceColor`; here it is the fork's `FaceColor` with the fork's 50%
  transparent blue as default, not upstream's orange (`8a6f859a57`). All
  five stores join the automatic set (`ShapeColor`, `Transparency`,
  `ShapeAppearance`, `DiffuseColor`, `ShapeMaterial`). A file from before
  saved its face colour: if that is neither the default nor the current
  preference it was set by hand, and AutoColor goes off so the file keeps
  it -- upstream would follow the preference and drop it on the next save.
- **Automatic colours are display only** (`be034d08f9`, user ruling). With
  Part's colour mapping on (`MapLineColor`/`MapPointColor`, off by
  default) a shape made from a sketch copied the sketch's edge colour into
  its own file. `ViewProviderPartExt::mapsElementColors()` lets a view
  provider decline; a sketch does while AutoColor is on. Faces never
  mapped: a sketch's `Shape` has none. Upstream has no colour mapping.

A preference change reaches every sketch through one `ParamHandlers`
delayed handler; the edit-time observer is attached only while editing.

**The hover cost, and why `fbd7f7090c` waits.** Measured on the largest
corpus sketch (Sketch028 of shirma_s_vitrazhom_N7, 1428 geometries, 2168
constraints): one hover change's `SetPreselect` echo costs 13.2 ms, 0.03
ms on a 3-geometry control, and `perf` puts 98% of it in `updateColor()`
-- which runs twice per hover change here, once from the echo and once
from `mouseMove`. Half of `updateColor()` was a quadratic this port
introduced (`3e275d5e8d`): `isConstraintActiveInSketch()` scanned every
constraint for each element of each constraint, looking for groups the
sketch does not have; fixed in `a7ddac3021` (13.2 ms to 4.7 ms per hover change, `SketchObjectTest.groupQueriesFollowConstraintChanges`). The rest is
`updateColor()` itself rewriting every colour on a hover. That is what
the in-edit highlight move (evaluated 2026-09-10: draw the
(pre)selection at render time, as the rest of the shapes do) removes
outright, so `fbd7f7090c`'s echo is left to it rather than patched.

**The highlight overlay (session 98).** The move was re-scoped once its
route was checked: the edit graph is not under the selection root but is
captured into the backend by its own overlay manager (`editingCapture`,
overlay id 7), so the unified selection's highlight cannot reach it, and
that manager's `setHighlight` would overwrite the model's. The ruling:
first a Coin-side move that works in every render-cache mode (option C),
then a mode-3 highlight overlay on top of it for per-view highlight
(option B, below).

C: the four highlight sets (`Selected`/`PreSelected` x `Curve`/`Point`)
already drew the highlighted elements on top, but indexed into the
geometry's own coordinates and material, so `updateColor()` had to rewrite
every colour and every layer (z) to change one. They now draw copies of
the highlighted vertices, lifted to the highlight layer, from coordinates
and a three-colour material of their own, and are unpickable.
`updateColor()` is split into `updateBaseColor()` (semantic colours and
layers, run by every caller that was not a selection change, and again
when the camera turns to the other side of the sketch) and
`updateHighlight()` (the overlays, the constraints whose highlight
changed, and `updateVirtualSpace()`, which now writes only on a change).
`onSelectionChanged` and `mouseMove`'s preselection call only the second.
The point selection helpers keep state only.

On the way: the three defects of the 2026-09-10 evaluation were two
(`f6dea571ce`, the third already gone), and
`getGroupHandleIfInGroup()` was the sibling of the `isInGroup()`
quadratic (`257e490b44`), 94% of what was left of a hover. Sketch028's
hover echo: 4.6 ms before, 1.26 ms with C alone, 0.035 ms with both; the
3-geometry control 0.03 -> 0.014 ms. Guarded by
`tests/gui/sketch-highlight-overlay.py` (a hover leaves the geometry's
nodes alone; the overlay colours; only the changed constraint's label is
written; the backend draws the hovered edge in the preselection colour --
4 of its checks fail before) and `tests/gui/sketch-highlight-notify.py`.
`sketch-bulk-selection.py` and `sketch-visual-layers.py` read the
highlight from the overlays now, not from `CurvesMaterials`.

B (session 111): in render cache mode 3 the preselection of the geometry
itself -- the curve, the vertex, the axis under the pointer, the dragged
element -- is the hovering view's own highlight. The sketch hands it to
`ViewerContext::setEditingHighlight` as one `SoFCDetail` per shape node
and colour (the solid and dashed curve sets, the point set, the cross);
the view's editing capture takes those elements from the scene it
already captured (`SoFCRenderCacheManager::setHighlights`, no traversal)
and its renderer feeds them to the backend as an overlay of their own,
`OverlayEditHighlight` = 10, past the edit graph's 7. A view that cannot
-- outside mode 3, a served mirror -- returns false, and the sets carry
the preselection as before, in every view. A preselection from outside
any view (the tree, the task panel) goes to every view of the session. A
preselected constraint stayed a colour write in the graph until s112 (see
below). Modes 0-2 are unchanged (user ruling).

The draw-order question the session opened with turned out to be moot
in mode 3: the old sets' copies were already drawn over constraint icons
and datum label text there, whatever their layers say (a label never
covered even the plain edge). The overlay keeps that. Whether the layers
put the highlight under an icon in modes 0-2 was not measured.

`updateHighlight()` now writes a highlight set's indices and coordinates
only when they change: it rewrote all of them, empty ones included, on
every call, and in mode 3 each write made the whole edit graph be
captured again.

Measured on Sketch028 (1428 geometries, 2168 constraints; llvmpipe, a
802x543 view): a hover plus the frame it asks for cost a plain frame +35
ms before (the edit overlay captured again), +1 ms now; the hover echo is
0.085 ms (0.04 before). A first version took the highlight by traversing
a path to each node with the capture manager's action, as `setHighlight`
does: it left every later frame 12 ms dearer, the same draws each ~6 us
more, for a cause not found -- taking the elements from the captured scene
does not.

Does a main-scene hover, which still goes through `setHighlight` that way,
leave the same residue? Measured 2026-10-01, phased (frames, one hover and
clear, frames, ten more, frames), corpus `portal_2.FCStd` (239 objects,
218 draws): no. On d3d12 (RTX 3070 Ti) the frames before and after hold
8.5-9.0 ms and 3.1-3.3 us a draw. The same probe on Sketch028 in edit on
d3d12 holds too, 8.6 us a draw either side. On llvmpipe what is left is
+2.4 and +2.8 ms a frame after one hover (about 2%) against -0.3 and -0.9
ms without one, so it lives in llvmpipe's state, not in the submit our
code does. TRAP: `Gui.Selection.setPreselection(obj, "Face1")` on an
object inside a Body highlights nothing in mode 3 -- `beginDetailPath`
needs the path from the top: preselect `obj.Parents[0]` with the subname.
`FC_BGFX_DEBUG_FEED=1` prints a `bgfx feed hl` line per highlight that
reaches the backend.

The 307 more draws after the first hover (1822 -> 2129, both codes) were
not a cost of the hover: they were the sketch's 103 datum labels, which
had never been drawn. On entering edit, `drawConstraintIcons()` merged
1321 Horizontal icons that fall on one spot of the fitted view into one
image 44879 pixels wide; Coin keeps an image size in shorts, the width
wrapped negative, `SbImage::setValue` asked for 2^64 bytes, and the new
handler's `Base::MemoryException` left `draw()` before `updateColor()`.
So `updateVirtualSpace()` never enabled the constraint switchboard, and
no constraint -- label or icon -- was in the scene, in any render mode,
until the first highlight pass. `sendConstraintIconToCoin()` now crops
an image past 32767 pixels (what is cut lies past any screen). Guarded
by `tests/gui/sketch-merged-icon-overflow.py` (5000 icons on one spot,
40000 pixels here: the label is in the scene right after entering edit;
fails before).

Why one image could get that wide: the icon grouping is transitive (an
icon joins a group when it is within an icon's size of ANY member), so a
chain of neighbours is one group however far it runs, and a group is one
image -- a row per type, the icon and every member's label in one line.
And an unnamed constraint of a single-icon type, whose label is empty,
still reserved a ", " of width: Sketch028's 44879 pixels were nearly all
blank. Now an empty label takes no room, a two-icon constraint's number
shows once, the labels read in constraint order, and a row wraps them:
View/ConstraintIconLabelsPerLine (10) to a line, at most
View/ConstraintIconLabelLines (3) lines -- both on the Sketcher Display
preferences page, and a change redraws an open edit -- with the last slot
"+N", whose box picks the constraints it stands for (the icon still picks
every constraint of its type in the group). Picking a merged icon: a box
the point is inside now outranks boxes it is only within the pick radius
of (a click on one wrapped label took the lines above and below), and a
blank spot picks nothing, as upstream (it picked the constraint whose
node the merge was drawn on). Guarded by
`tests/gui/sketch-merged-icon-labels.py` (50 named Horizontal on one
spot, swept with the pick probe: the icon picks 50, 29 labels one each,
one box the other 21; set to 5 and 2 during the edit, 9 and a "+41").

`drawConstraintIcons()` reports an exception and returns (`1744c224a1`),
so a throw there can no longer skip `updateColor()` and hide every
constraint; checked by forcing one in a scratch build.

**Icons on one spot are laid out, not merged** (s112, user ruling: rid of
the merge, but place the icons as if they merged, keep the two settings,
and a "+N" for what does not fit). A merged image has one material, so a
hover could not colour one constraint of it and its colours were baked
into its pixels -- in the way of a per-view constraint highlight. Now each
constraint keeps its own icon. A group -- the icons within the merge
distance of the one it starts from, no longer a transitive chain -- is
laid out from that first icon's place, ordered by type then number:
View/ConstraintIconLabelsPerLine to a line, at most
View/ConstraintIconLabelLines lines (the Display page calls them icons per
line and lines now), an eighth of an icon apart. Past that the last slot
is a "+N" drawn in the first left-over icon's node, whose SoInfo names
every left-over constraint, so a click on it picks them through the
ordinary list in the SoInfo; the left-overs' own images are cleared.

The layout offset is in pixels: `SoZoomTranslation::pixelOffset` (new),
turned into model units per render on the Coin path and carried by
`SoFCZoomOffsetElement` to the image quad in mode 3. Put in zoom units (a
50th of the view's height) instead, the grid stretched or overlapped once
the view was resized after the layout. The layout writes the icon's
translations, which `draw()` owns, so the view provider keeps what it
wrote (`iconLayout`) and lays out again from `draw()`'s values. Gone with
the merge: the merged-box pick, its nearest-or-union choice
(`65545338e4`, which only served it) and the transitive-chain fix's
merged groups (`576ff93c00`).

Guarded by `tests/gui/sketch-icon-layout.py` (50 named Horizontal on one
spot: 29 icons and a "+21", none overlapping, every constraint drawn or in
the "+N" once; 9 and a "+41" at 5 and 2 -- before, one image naming all 50),
`sketch-merged-icon-labels.py` (each icon picks its own constraint, the
"+N" the rest, no spot two -- before, the icon picked all 50) and
`sketch-merged-icon-overflow.py` (5000 on one spot: the "+4971").

**A preselected constraint is the view's own highlight too** (s112). Where
every view the preselection is for can draw it (mode 3, not a served
mirror: `ViewerContext::canEditingHighlight`), the sketch no longer writes
the preselection colour into the label, the icon or the points a
coincidence holds. It hands the hovering view a highlight item with a
*path* to the constraint's node (`SoFCRenderCacheManager::HighlightItem::
path`): the editing capture's manager captures that path once
(`pathCache`, shared with `setHighlight`) and shows all of it in the
preselection colour, the way a preselected object is shown on top; a
coincidence's points and an alignment's curve go as elements, as the
geometry does. The icons no longer take the preselection colour
(`iconPreselect`), and a hover redraws them only when that changes. The
view a constraint preselection came from is tracked with the geometry's
(`trackPreselectSource`). The selection stays in the graph, every view.

What made the label text and the icons answer a highlight at all: mode 3
drew them as textures that replace the fragment colour, the colour baked
into the pixels, so the highlight's colour was ignored and the copy drawn
on top looked the same. A one-colour image is now captured as its alpha,
white, modulated by that colour (`a9abceb7b9`); the label's text by its
own material. The baked pixels had been blended with their colour
premultiplied, which darkened their edges a second time: the text reads
the label's colour now. Modes 0-2 draw the baked images as before.

Guarded by `tests/gui/sketch-constraint-highlight-view.py`: preselecting a
label, an icon and a coincidence leaves the edit graph's nodes alone; the
label's text and the icon take the preselection colour, the coincident
point too; with two views, the pointer over the icon colours it in that
view and not the other. The two-view check uses the icon, written while a
datum label seemed not to pick after tiling two views.

That was two holes in the label's pick box (`c4f305c018`), both in
`SoDatumLabel`, neither about views. The distance, diameter and angle
boxes were emitted as a QUAD in the corner order lower-left, upper-left,
lower-right, upper-right -- a bowtie: Coin cuts a quad into (v0,v1,v2) and
(v0,v2,v3), and the triangle from the upper corners down to the centre
never picked. The probe point was the label's centre, the apex of that
hole, in every view. Same order as a TRIANGLE_STRIP now, as upstream emits
it since #29904. And a diameter's bounding box held only points on its
dimension line: no height. Coin culls a ray pick by a shape's cached
bounding box only while that cache is valid, which depends on what
traversed last, so the number picked 17 pixels high in one run and 10 --
the pick radius around the line -- in the next. Its text corners are in
the box now. `tests/gui/sketch-datum-label-pick.py` sweeps each label pixel
by pixel with the bounding boxes cached for the camera (a
`SoGetBoundingBoxAction` over the render manager's scene graph before each
probe): the hits fill their rectangle, and it is the size of the text
(`QFontMetrics` of the label's font). Before: fill 0.74-0.77; the diameter
10 pixels high.

A preselected or selected constraint also highlighted the sketch's own
shape (`5451ba3327`). `getDetailPath("ConstraintN")` fell through to the
Part view provider, found no element of that name and resolved to the
whole object; in mode 3 the edit keeps the shape under its root, hidden
per view, so the main highlight slot drew it for nothing. Nothing showed
in the editing view (0 changed pixels at the vertices the constraint does
not touch). The "rings" on the endpoints a smoothed crop suggested were
the preselected extension lines, drawn on top, crossing the endpoint dots.
A constraint name resolves to no path now.

`renderConstrIcon` lost the merged icon's parameters -- a label list with
a colour each, label boxes, the text's descent, the per-line wrap
(`d62c877aff`); every caller passes one label since icons on one spot are
laid out rather than merged. The icon images hash the same before and
after.

Guarded by `tests/gui/sketch-highlight-view.py` (a hover writes no node
of the edit graph; the sets hold the selection only; the pointer's
preselection shows in its own view and not a second one, one from outside
any view in both; the hovered edge, a vertex and an axis in the
preselection colour, over the icon and the label it crosses). Against the
old code the node, set and per-view checks fail; the crossing check passed
there too. `sketch-highlight-overlay.py` reads its set checks per mode, and
`sketch-visual-layers.py` reads a hover from the preselection rather than
the set. A synthetic pointer move DOES preselect in sketch edit now (this
test drives two views with it), on llvmpipe and d3d12.

`sketch-drag-arc-conic.py` is flaky, independently of this work: a drag
that never starts, a different one each time, 2 of 10 runs without C and
2 of 9 with it.
`sketch-arc-labels.py` failed once the same way under `ctest -j 6`
(2026-10-01: the angle label's drag past the centre never started) and
passed 3 of 3 alone.

**Harness notes.** To make a file from before a property existed, taking
its `<Property>` out of `GuiDocument.xml` is not enough: the reader loops
over a record's `<Properties Count="N">`, so each record that loses one
must say one fewer, or it reads on into the next record and that view
provider silently restores nothing. An App-level `doc.saveAs()` leaves
the Gui document's `Modified` flag set; `Std_Save` clears both.
`sketch-hidpi-sizes.py` needs `QT_SCALE_FACTOR=2`, which its ctest
entry sets and a hand-run loop does not.

### The later rows (sessions 113-114): 27 -> 3

The open `ViewProviderSketch.cpp` rows from 2025-07 on, read one at a time
against the fork's own code, each measured before it was changed.

| row | verdict |
|---|---|
| `c14d6f8848`, `0a45527b8b` | **adapted** `983d83aa45`: a new sketch's `PointSize` takes `View/DefaultShapePointSize`, 4 where unset -- read from the group, since `ViewParams`' own default is 2. `sketch-new-point-size.py` (6 with the preference; was 4) |
| `5961651547` | **adapted** `b1bfb15d5b`: a sketch holding only external geometry is not "Empty sketch". `sketch-empty-message.py` reads the task panel label |
| `a7b501c95c` | **adapted** `1be540f2a9`: the context menu offers "Change value" for one dimension alone; a Horizontal offered it and the dialog did nothing. `sketch-context-menu-value.py` right-clicks with the selection set |
| `36786d4794` | **adapted** `bb3b05857f`: deleting solves once (`noSolve` on each delete, the final solve redraws on failure); the dimension tool skips its solve on leaving in mode FIRST. gdb solve counter: a delete 7 -> 2 solves, Escape 1 -> 0. The label-release half is not needed: measured 0 solves here |
| `00c3422c1f` | **adapted** `ed86b7af3e`: hovering an expression-driven constraint shows the expression as the view's tooltip; one update in `mouseMove`, on the hovering view's widget only (a mirror has none). `sketch-expression-tooltip.py` |
| `93abfc4fa4` | **adapted** `4e6197113a`: a LIVE CRASH here -- with a sketch in Transform edit every Sketcher tool was active and `Sketcher_CreateLine` segfaulted in `deactivateHandler()`. The gates ask `isInEditMode()`. `sketch-transform-edit-tools.py` |
| `4bdaa0180a` | **have**: every mode write goes through `setSketchMode`, which calls `updateActions()` |
| `de3de7624a` | **have**: the fork's internal view is a `ViewProviderPart` with Lighting "Two side" |
| `e2346dabd6`, `bf009d41e4` | **n/a**: upstream's port of this fork's internal faces, and a fix to its own `SoSketchFaces` node path; the fork resolves faces through `pInternalView` |
| `16a836743a` | **n/a**: `slotSolverUpdate` has no edit-view gate here |
| `289411f51c`, `8c1d03ccb4` | **n/a**: an include for a core header change, and a line serving a Core `NavigationStyle` change |
| `2da7c9ff17`, `566a724c26` | **adapted** `a3e4beb17f` (session 114): a tool's preview takes the edge colour, or the construction colour in construction mode, and a toggle recolours it at once. Colour only -- the fork patterns curves by visual layer and a new curve goes to layer 0 in either mode. `CreateLineColor` reads nothing now and its button is gone. `sketch-preview-construction-color.py` (0/4 before) |
| `8a6872e69d` | **adapted** `3982c0e4d6` (session 114): `signalConstraintAdded`, and the view in edit scales a new Distance/DistanceX/DistanceY label still at the default 10 to 2 x its scale factor, however the constraint was made. `finishDatumConstraint` keeps scaling every datum type -- upstream dropped that line, leaving its radius/diameter/angle labels at 10 mm whatever the zoom, which the commit does not mention. `sketch-distance-label-scale.py` (6/9 failed before: 10 against 0.447) |
| `aa785f78d6` | **adapted** `24982ee5f9` (session 114): the Dimension tool's preview label is pulled back by 1% of the view's width plus height, measured on the session's editing view (a served client's own). `OffsetMode` without `using enum`. `sketch-dimension-label-offset.py` (label 6.004 under a pointer at 6.0 before, 4.972 after) |
| `387d25c219` | **have**: the line-extension hint and its PointOnObject snap came in with group C (`338b27fea2`); every function the commit adds is here, guarded by `sketch-line-extension-autoconstraint.py` |
| `9ce1cae190` | **n/a**: the invalid projections it guards cannot reach the fork's sketch. A mirror exists only once its client has stated a camera (`mirrorFor`), the wire refuses a non-finite, zero-size or zero-extent camera, a desktop view always has one, and a view parallel to the plane already throws `ZeroDivisionError` to callers that catch it. The no-camera early return in `getProjectingLine` leaves the line uninitialised, but nothing reaches it |
| `35f151d99e` | **adapted** `56aa886d28` (session 114, user ruling): the panel is Core's `TaskSolverMessages` (already here from the Assembly port), "Sketch Edit", with a settings menu: auto-update and the toolbar's grid, snap and rendering order widgets, built by `addViewSettingsActions` (`Command.h`) because the fork keeps those classes inside `Command.cpp`. State `empty_sketch` -> `empty` in the eight stylesheets. `sketch-solver-panel-settings.py` (no Core panel before) |
| `5587b48a0f` | **adapted** `8013767cd2` (session 114, user ruling; the Core half is the fork's own): an edit element's box by geometry id, composed with the editing placement as upstream does (`aaf94ad58c` corrected the first version, which used the sketch's own Placement on a wrong argument that a container's placement counts twice: a container's walk passes transform=false with the matrix already global, and asked directly only the editing placement knows the edited occurrence, through a Link too); the fit on entering edit behind `Mod/Sketcher/General/FitSketchOnEdit`, off by default, orientation set directly so an animated turn cannot outlive the fit. `sketch-view-fit-edit.py` (5/7 failed before; 10 checks now, with direct and Link queries). Found on the way, Core: an element's box dropped its object's own placement (`9ad0f2098b`), and a point's fit zeroed the zoom (`d15ea789aa`), `view-selection-point.py` |
| `6321ac28a3` | **declined**: the fork draws a drag from the solved sketch (`draw(true)` extracts it), so `moveConstraint` reading the same is what is on screen; reading the object instead measured no faster (about 18 ms a move on 2000 lines, the redraw dominates) |

`8a6f859a57` was ruled after: the faces follow the preference as upstream's
do, with the fork's default colour (`94d76ec9c2`, see AutoColor above).

Left open then, each a decision or larger than a row: the annotation pick
priority (`a2468774d3`), the broken-external report (`07b2d9973d`), the
resetEdit lifecycle (`e6d3f9d6db`). Session 115 took them up:

- `07b2d9973d`, **adapted** `8cdfeb6711`. Upstream merges a warning into
  the sketch's icon and adds a tooltip hook to the tree. The fork's tree
  already carries state marks as extra icons beside the item, each with its
  own tooltip (`getExtraIcons`, `getToolTip` by icon tag), so the sketch
  adds the Warning icon while an external geometry that has a reference is
  flagged Missing, and refreshes the item only when that state flips.
  `sketch-missing-external-tree.py` reads the icon from the tree's model: a
  box edge as external geometry, the box turned into a cylinder and back
  (width 192, 256, 192; it never changed before).
- `a2468774d3`, **probed, put to the user**. A box, a sketch on its top
  face with internal faces on: inside the sketch's region the frame shows
  the SKETCH's face (pixel (83,195,194) against the box's (58,210,58)) and
  a click selects the BOX's `Face6`. The pick list holds both at the same
  depth, the box first. Two things differ from upstream. The fork's
  coincident-pick loop (`SoFCUnifiedSelection.cpp`, `getPickedList`) stops
  at the first hit of another view provider, so it only ever prefers an
  edge over a face of the SAME object; and the sketch's face is in front
  only because it is drawn later at equal depth, not because anything says
  so. A fix has a drawing half and a picking half, both in Core.
  **Ruled 2026-10-02, to build next**: the internal-face view gets a small
  polygon offset toward the viewer, so it is in front by rule in Coin and
  bgfx alike; a view provider can declare itself an overlay on coplanar
  geometry, and among hits at one point with one priority the overlay
  wins, across objects. With internal faces on, the solid's face is then
  picked outside the sketch's outline only -- upstream's trade as well.
- `e6d3f9d6db`, **n/a until Core has it**: it moves the sketch's task
  dialog onto `TaskDialog::setAutoCloseOnResetEdit`, which is not here. The
  fork's `unsetEdit` closes the dialog itself, and no defect was shown.

**The arc-label "flake" is a clock.** `sketch-arc-labels.py` and
`sketch-drag-arc-conic.py` fail now and then with a drag that never
starts. Measured: this WSL2 box's wall clock steps back about 0.97 s every
32 s; Quarter stamps mouse events with `getTimeOfDay()`, so a press about
a second after the last reads as 0.2 s after it, and
`NavigationStyle::processClickEvent` holds it as a double click until the
release. The rate swings with the phase between a run and the step period
-- it once made a correct commit look guilty (12 of 12 passed without it,
then a run without it failed the same way). Fixed on the user's ruling in
`e35e9990b4`: `processClickEvent` measures on `std::chrono::steady_clock`.
A vertex dragged once every 1.1 s for 75 s lost 3 drags of 68 to 3 clock
steps before, none after.
It is not the only cause: `sketch-drag-arc-conic.py` failed once more
under `ctest -j6` in session 114, after the fix (the second drag never
started; 6 of 6 alone). Its two presses are 1.4 s apart on the steady
clock, so the double-click hold cannot be it. Chased in session 115 and
not reproduced: 24 of 24 passed with eight copies running at once, and it
passed in that session's full `ctest -j6`.

### The partial(sync) rows (session 115): 21 read

The 21 rows the ledger files as `partial(sync)` that name
`ViewProviderSketch.cpp` -- commits older than the squashed sync, which the
fork's own file may or may not have seen. Each hunk was read against the
file; the line-presence count only chose the order.

| row | verdict |
|---|---|
| `476089a2ad` | **have**: the contextual menu is `generateContextMenu`, reshaped since by `8145eed95f` and `a7b501c95c`; it offers the fork's own `Sketcher_CreateFillet` and `Sketcher_ExternalCmds` |
| `df7e783513`, `8145eed95f`, `b92bda03da`, `2ea8a633ac` | **have**: the `STATUS_SELECT_Wire` case and the end point count; Horizontal before Vertical in every branch; `moveGeometriesTemporary`; `Quantity::parse` on a `std::string` |
| `4a486b21ed` | **have**: an external edge is preselected, selected and box-selected by its `ExternalEdge` name |
| `e15646d158` | **have**: `slotSolverUpdate` on the sketch's `signalSolverUpdate`, connected before the first solve of `setEdit` |
| `a72a63232a` | **have**: `App::Color` is an alias of `Base::Color` |
| `a8ae56e06a` | **adapted** `efa015725d`: the edit snippet reads `AttachmentSupport`. The fork keeps both properties and copies each into the other, so nothing changes on screen |
| `d9fc266772` | **taken** `b726b26547`: two of the five linked solver messages had a trailing space inside `tr()` and three had none; all five are the bare text with the space appended, at upstream's tip wording ("Under-constrained") |
| `4b589088f6` | **taken** `112b7ef540` (an `open` row with a partial note): `<limits>` in `App/PropertyConstraintList.cpp` |
| `4e8f3f0381`, `b07caa732e` | **superseded** / **n/a**: the draw-style switch on entering edit and its removal; the fork never had it (as `e7c11a01be` above) |
| `6ca8b2daae` | **n/a**: there is no overlay mark to redraw; the fork swaps the whole tree icon from `FullyConstrained` |
| `51c6dbd3e3` | **declined**: `activateHandler` taking a `unique_ptr` is a signature change over nine files with no behaviour in it. `SketcherGui::ActivateHandler` owns the new handler from its first line, so the early returns leak nothing |
| `5839134e95`, `7a5a3d1ffc`, `dd6aa9f3c7`, `ac788df608`, `34881bc82e` | **n/a** for this file: comment typos in lines the fork does not have, MDI type tests the fork replaced with `Gui::ViewerContext`, a menu entry the fork groups under `Sketcher_ExternalCmds`, a spelling of `std::find` |
| `5969df37f4` | **adapted** `d7fcfb4e4b` (user: yes), without upstream's camera sensor. Upstream stretches the two axes to the viewport on every camera change; here the edit geometry is one drawing shared by every view and every served client, so nothing in it may depend on one camera. Each axis is a polyline stepped by decades out to 1e7 -- a single line that long has its ends eight orders past a close view, beyond single precision, while a decade step keeps the piece a view cuts within a factor of ten -- and stays under its `SoSkipBoundingGroup`, so a fit still frames the sketch. `sketch-axes-reach.py` (the axes ended at 148.4; picks 3 m out found nothing) |

`646b4381f9`, the 21st, was adapted in session 95 (ARCLENGTH).

### CommandConstraints.cpp (session 115): 36 rows, 2 left

Every undecided row naming `Gui/CommandConstraints.cpp`, fixes first. The
file is LF, unlike most of the module.

| row | verdict |
|---|---|
| `e38154474a`, `084379651a` | **adapted** `42931f6f46`: a coincidence that would fold an element onto a point is refused. The two ways into the command checked different things: started on a selection it joined the two ends of one line (the solver then says "Both points are equal"), as a tool it refused any two points of one element, a B-spline's ends included, and neither saw an end joined to a point already on the other end. Both ask `isCoincidentSelectionValid` now. `sketch-coincident-same-element.py` (4 of 8 failed) |
| `1050996387` | **adapted** `5a621c16c0`: upstream's guard is on its tool being given one edge twice, which adds nothing here -- a second pick of a selected edge does not advance the tool. What did add a constraint, and still does upstream: an arc selected with its OWN end point wrote `Tangent(arc, start, arc)`. Refused, with upstream's message. `sketch-tangent-self.py` |
| `bc3c0dc19a` | **adapted** `6c0dcb8578`: the symmetric constraint takes an element by its two ends -- a line, an arc of any conic, an open B-spline -- with a symmetry line, an axis or a point. On the fork's own sequences, four added. Found with it: the tool took a circle with a point and wrote a constraint between ends a circle does not have. Hints: "pick symmetry line or point" after an edge, "pick edge" after an axis. `sketch-symmetric-element.py` (8 of 14 failed) |
| `71eed18cb1`, `abf9762abb` | **have**: the fork's sequences already take the root picked first and three points |
| `36dd4b983c` | **taken** `1d00ac7d19`: the Dimension tool counts coincident points once, so a corner's two vertices and a third point are two points |
| `76a84f63ab` | **taken** `8be2333ea9`: a horizontal or vertical line keeps the distance along its own axis wherever the pointer goes. `sketch-dimension-tool-points.py` guards both (3 of 7 failed) |
| `b9155035fe` | **adapted** `bb5fcd8cd4`: constraint values, and what the datum dialog writes, went through `"%f"` -- 0.000158101832 became 0.000158. Upstream's `"%.8g"` mends the small values and is WORSE than `"%f"` above 100 (1234.123456789 -> 1234.1235), so the fork writes `"%.15g"`. The right angle of two perpendicular lines is taken with it. The tool handlers and `Utils.cpp` still write `"%f"`, as upstream's do. `sketch-constraint-value-precision.py` (5 of 5 failed, relative errors up to 6e-4) |
| `9fc40b33de` | **taken** `536a207770`: the one string that still differed |
| `a7251a6c3a`, `3d2419effc`, `7d21d9edb8`, `6a1afdc4e2`, `c0c6df10ec` | **have** |
| `aa785f78d6`, `8a6872e69d` | **adapted** in session 114 (`24982ee5f9`, `3982c0e4d6`); the ledger had not been told |
| `75c8749189`, `a283855697` | **declined**: tooltips the fork words itself |
| `f4665aa7b5` | **declined** (user ruling) |
| `08381b1d18`, `ed770bf849` | **n/a**: they follow a `pixmapFromSvg` that sets the device pixel ratio. The fork's returns device pixels, and the tool cursor is painted in them |
| `651cefde4d`, `08c9a191e2`, `12a69fe296`, `65c6614081`, `f932c7e4e0`, `50f029edd4`, `8aa50c4380`, `65466d580b` | **n/a** for this file: spellings of the same call, a warning cleanup, two Core header moves |

Ruled and taken after that (user, 2026-10-02):

| row | verdict |
|---|---|
| `fe7c1d18be` | **declined** (user: "keep ours"). Measured: a running Line tool survives a constraint command applied to a selection. Escape ends a tool, and the fork re-runs a command to toggle its tool (`90f0e23eac`), which upstream's release at the top of every constraint command would undo |
| `3d87975faf` | **adapted** `ee6be8f6a7`: a new Distance / DistanceX / DistanceY label goes below both points, beside both (the upper point's side), or up and left of an aligned one, by the view-scaled label distance the fork already gives it. `moveConstraint` is public here, so no attorney class. `sketch-distance-label-side.py` (4 of 6 failed: a downhill line's horizontal label sat between its ends) |
| `9663cf8dd4` | **adapted** `21feb2a554`: a new angle's arc goes past the nearer end of two lines that do not reach their crossing, an arc's own angle outside the arc. `sketch-angle-label-place.py` (4 of 5 failed: both at the default radius) |
| `129c7d4d03` | **adapted** `705f37e446`: `SelArc` / `SelExternalArc`, given to a pick only when the running tool asks for them, on both ways a pick reaches the tool (the release in the view, and the selection change a served client sends). The selection gate dropped any type mask of 256 or more -- a guard sized for the old types, which made the angle tool accept nothing once the new bits were in. `sketch-angle-tool-arc.py` |

Still open:

- `999fed9c4e` (user: take it). Not a port: upstream builds it on its
  `ExternalSelection` gate and finds a reference again by comparing
  sub-names. The fork's external tool is its own and does more
  (`DrawSketchHandlerExternal.h`: sub-object paths, mapped element names,
  import across documents, whole-object picks), so the constraint tools
  have to go through THAT path. A design, to be agreed first.
- `0c34c93fe4`. Both constraint tools select by the sketch itself; the
  fork's panels select through the edited occurrence's path
  (`selectElement`). It works, because the sketch resolves the object, but
  a removal by the other path would not match. Wants a click-flow test
  inside a container before it changes.

  **n/a** (session 117), by that test, `sketch-constraint-tool-select-path.py`:
  a sketch edited inside an `App::Part`, the line clicked with no tool,
  then picked and unpicked in the Dimension tool and in Parallel. Every
  entry is on the Part's path whichever way it was made, and what one way
  selected the other unselects. The fork's selection puts an object on its
  top parent's path for every add, removal and query
  (`SelectionSingleton::checkTopParent`); upstream's does not, which is
  what its commit works around in the tool.

### The coplanar pick, and Command.cpp (session 116)

**`a2468774d3`, adapted** `8fdf3f3325` (Gui) + `84db00d6d5` (Sketcher), as
ruled. The probe written first said more than session 115's had:

- square to the camera the frame showed the BOX's face inside the sketch's
  outline, not the sketch's. Both faces carried the same polygon offset
  (the internal-face view sits under the sketch's own face root and
  inherited its (1, 1)), so the tie was real and drawing order settled it;
- the single pick was the box's face at every pose, but not where the
  ledger pointed. With hidden-line selection on top (the default) the pass
  over the on-top objects leaves the ray pick action gathering every hit,
  so the single pick is the gathered list cut at the first hit of another
  object (`getPickedInfo`); only with that option off does the action keep
  one hit as it goes (`afterPick`). The pick LIST already held the sketch's
  face first.

What was built: `SoFCUnifiedSelection::setCoplanarOverlay(node)` declares a
node an overlay, and of two hits of one priority at one depth the one under
a declared node wins, in the gather, in `afterPick` and in the list
post-process. `PartGui::ViewProviderPartExt::setCoplanarOverlay()` gives a
view's faces a polygon offset of (0.5, 0) -- behind an edge (none), in
front of an ordinary face (1, 1); the constant half is zero because a depth
buffer cannot resolve half a unit -- and declares its root. The sketch's
internal-face view is one. `sketch-face-on-solid-pick.py`: three poses,
both pick paths, 85 claims; run by hand with render cache 0 as well, where
Coin draws alone, with the same result.

**Not changed, and a question**: an edge lying in a face loses an exact
depth tie to that face. This is every object's behaviour, not the
sketch's: an edge wins only where it is the NEARER hit, so in a view square
to a face the edge is picked from outside the face only, and on a slanted
face from one side of the edge. A sketch's outline lying on a solid's face
has no outside, so square on it cannot be picked at all (the test records
what a pick on the outline returns, without claiming it). A rule that
would mend it for all objects: a hit that lies in the plane of the kept
face hit is not behind it, and an edge or a vertex there wins within the
pick radius. That changes what a pick near any edge returns, so it waits
for a ruling.

**Command.cpp**, the undecided rows:

| row | verdict |
|---|---|
| `17c3286e52` | **adapted** `40747494e0`: New Sketch with a group selected made no sketch at all (the attacher was asked what a group can carry). The group is now where the sketch goes. Upstream takes a plain group; an `App::Part` counts here too, a body does not. `sketch-new-in-group.py` |
| `93173ba797` | **taken** `7403797c4b`: Attach Sketch leaves the selected sketches out of its list, so a sketch cannot be attached to itself. `sketch-attach-not-itself.py` reads the dialogs |
| `d34081b9fe` | **adapted** `0a5dd8f2f5`: Merge Sketches gave the merged sketch no external geometry and moved external ids as if they were the sketch's own (a point on an external edge ended on the vertical axis). References are carried over, ids go to the merged sketch's id for the same reference -- matched by the reference string each projected geometry carries, where upstream rebuilds names -- and a constraint that cannot follow is dropped with a warning. In the body when every source is in one. `sketch-merge-external.py` |
| `e12deea20e` | **superseded** by `d34081b9fe` |
| `64029d3a5b` | **have**: `updateIcon` is guarded |
| `2f2787611c` | **n/a**: the fork's View Section is `toggleViewSection()` in C++ and names no `ActiveSketch` |
| `cfd1cdfb36` | **n/a** for this file: New Sketch finds the group from the support as a document object, with no `Part::Feature` cast |
| `3164ee1849` | **have**, the fork's way: the list is sorted, by name, latest first |
| `7b22027b90` | **n/a** for this file: a spelling of the same call |
| `ccb28af4a1` | **adapted** `56aa886d28` with `35f151d99e` (session 114): the settings menu on the solver panel |

Left for one decision: the wording rows (`46e2c45e2e`, `beb66d3cfb`,
`764b9cca0e`, `57ee6870b4`, `4dbdd1031d`, `f7f3c18e52`, `67b3f4e143`,
`b2c51665a2`, `dee977f98f`). Each changes a source string, and a changed
source string loses its translation until the translation files are taken
with it; so they go together with a translation resync or not at all.
`6eecd08f7c` (a Qt deprecation) and `3c1358da10` (the Datums header name)
are not read yet.

Read (session 117): `3c1358da10` **taken** `8c5430ed73` (one include);
`6eecd08f7c` **taken** `5986b18091`, its Sketcher part -- six connections,
this tree builds against Qt 6.11 where `stateChanged` warns. The strings
`d7074e36be` passed over because they are written across several literals:
`71768887ff`, 11 of 41 are upstream's wording now, the rest are the fork's
own or have no upstream form.

**`999fed9c4e`, the design put to the user.** What upstream does: the
constraint tools' gate lets an edge or a vertex of ANOTHER object through
while the step accepts an external edge or a vertex; the selection it
causes is turned into external geometry on the spot (`addExternal(name,
sub)`, found again by comparing the object and the sub-name), and the tool
goes on as if `ExternalEdgeN` had been picked. It reaches the viewer by the
active window.

The fork's external tool does more and differently
(`DrawSketchHandlerExternal.h`), and every piece of it is needed here:

1. *Reaching the pick.* `allowExternalPick()` on the handler lifts the
   edit's exclusive pick, `setSessionSelectionEnabled(true)` turns each
   view of the session back on, and the gate goes on
   `sessionSelection()`. The constraint handlers would answer
   `allowExternalPick()` from the step they are on (true only while it
   accepts `SelExternalEdge`, `SelExternalArc` or `SelVertex`), and their
   gate would hand anything outside the sketch to `ExternalSelection`,
   narrowed to edges and vertices -- no faces, no wires, no whole objects,
   no intersection.
2. *Making the reference.* One helper, taken out of
   `DrawSketchHandlerExternal::onSelectionChanged` and used by both:
   `addExternal(Part.importExternalObject(<picked path>, <editing
   context>))`, which is what carries sub-object paths, mapped element
   names and another document or body (through a binder). It returns the
   new external ids, found by the reference string as Merge Sketches now
   does; a reference the sketch already has is reused, not added again.
3. *Going on.* A vertex is `PointPos::start` of the new point; an edge is
   classified as an arc or not from the projected geometry; then the same
   code as a pick of `ExternalEdgeN`.

Open, each for the user:

- *Which tools.* Upstream changes the generic handler too, so every
  constraint command that lists an external edge takes one from outside,
  not only Dimension. Proposed: both, as upstream.
- *Undo.* The generic tools open their transaction when the constraint is
  made; the reference is made a step earlier. Proposed: one undo step for
  both -- the tool opens the transaction at the reference and the
  constraint joins it; a tool left before the constraint aborts it, so no
  stray external geometry stays. Dimension already runs inside one
  transaction and restarts it on a mode change; the references made so far
  are made again after a restart, as upstream does.
- *Another body or document.* The external tool makes a binder without
  asking. Proposed: the same here, since it is the same act.

**Ruled after that (user, 2026-10-02), and what was built:**

- *The edge in a face*, agreed: `ed018fd5d8`. What lies in the plane of the
  face hit first is not behind it; an edge or a vertex there takes the pick
  within the pick radius, whichever object it belongs to. The probe
  corrected the paragraph above first: with hidden-line selection on top
  (the default) an object's OWN edges and vertices already won, because the
  gathered list is searched for a better hit of the same object. The loss
  was across objects (the sketch's outline on a solid's face) and, with the
  option off, for an object's own elements too. `pick-edge-in-face.py`
  (4 of 24 failed, all with the option off); `sketch-face-on-solid-pick.py`
  now claims the sketch's edge on the outline.
- *The wording rows*, "take, and translation sync". Measured before
  anything was touched: of the fork's 1381 translatable strings 457 had no
  entry in the fork's own (2023-12) translation files and 726 had none in
  upstream's catalogue, so taking upstream's files alone would have lost
  about 270 translations. The nine rows are follow-ups of upstream's
  rewording of the whole workbench (`cf082f7642`), whose code the fork had
  and whose strings it had not. So the wording went first, `d7074e36be`:
  the menu text and tooltip of every command class both sides have, paired
  by class and member (220 strings), and 168 other strings whose upstream
  form is the same string reworded, read pair by pair. Kept: the
  Intersection command's text (it toggles here), tool hints, the element
  panel's layer names, texts that say what only the fork does. Then the
  files, `33f8ec9f15`: upstream's 49 at `bd6be559e8`. 365 strings are left
  without an upstream entry, the fork's own; German covers 1116 of 1388.
  `46e2c45e2e` (the three menu classes name their translation context) is
  `72831da284`.
- *`999fed9c4e`*: one undo step for the reference and the constraint,
  aborted when the tool is left before the constraint; a binder made
  without asking for another body or document. Which tools -- Dimension
  alone, or the eighteen individual constraint commands as well -- was
  asked back and is open. If both: a step whose picks are all external is
  refused, which upstream does not do.

**`999fed9c4e`, built as the user redirected it** (2026-10-02):
`6de565629c` (Gui) + `e041fdf88e` (Sketcher). Not "which tools take outside
picks always" but a MODE the user switches, stacked on the tool:

- While the Dimension tool or a constraint command runs,
  `Sketcher_External`, `Sketcher_Defining` and the two intersection
  commands do not replace it. `ActivateHandler` already asks the running
  handler `toggle(next)` (the polyline tool's hook); the constraint
  handlers answer it for a `DrawSketchHandlerExternal` by switching
  outside picking on in that command's flavour, off on a second press.
- One setting for all of them, `Mod/Sketcher/General/ConstraintExternalPick`
  (0 off, 1 external, 2 defining, 3 intersection, 4 intersection
  defining), read when a constraint tool starts.
- With it on the handler answers `allowExternalPick()`, the session's
  views select again, and the gate hands anything outside the sketch to
  `ExternalSelection` -- for a constraint command only at a step that takes
  an external edge or a vertex, and for both tools not while the sketch has
  something of its own under the pointer. The pick becomes external
  geometry by `addExternalFromPick()`, the External tool's own call taken
  out of it, and the tool goes on with the geometry made.

What it took in Core: a click the selection gate refuses is no longer
claimed by the selection node (upstream's commit has the same fix), and
the view passes over the object in edit when that object lets the view
pick around it -- it picked the sketch by the shape the sketch keeps in the
scene and took the click the sketch was about to handle.

Found on the way, each of which cost a build:

- the sketch publishes its own hover THROUGH the selection gate, so a gate
  cannot tell the sketch's pick of its element from the view's;
- selection notifications arrive after the call that caused them returns:
  a flag held around `rmvSelection()` is down again by the time the tool
  hears of it;
- the view clears the selection before it selects what was clicked, and a
  constraint command read every clear as "start over";
- a new external geometry's element name does not resolve until the
  sketch's shape is rebuilt, so the tool is advanced directly, not by
  selecting `ExternalEdgeN`;
- `ExternalGeometryFacade::getRefIndex()` is for copy and paste only; new
  geometry is found by comparing the reference strings before and after.

Undo as ruled: one step for the reference and the constraint
(`AutoTransaction::setEnable(false)` keeps the reference's transaction
open past its event, `setEnable(true)` lets the command's own open and
commit fold into it), gone if the tool is left, started over or the
command makes nothing of the picks. The Dimension tool makes its
references again after each of its aborts.

`sketch-constraint-external-pick.py`, real clicks.

**The open ends, closed (session 117, 2026-10-02).**

- *A sign on screen*, `24823906ab`: the tool's cursor carries, right of
  the crosshair and above the tool's own icon, the icon of the command
  that switched the mode on, so it tells the flavour too. Both tools built
  the same cursor inline; it is `StackedExternalPick::cursor()` now, set
  from `applyExternalPick()`, which the start and every toggle go through.
  A served client sees none of it: a tool's cursor does not travel, and
  neither does the hint bar.
- *The lost highlight*, `5252481f4f`, was two defects. The view clears the
  selection to select the outside element, and the sequence's picks went
  with it: they are selected again once the outside one is taken
  (`StackedExternalPick::reselect`; a constraint command knows the echo of
  its own re-selection by name, and its gate lets the steps behind
  through for the time of the call). And the Dimension tool lost its
  picks WITHOUT any outside picking: it starts its transaction over at
  every pick and every change of mode, and the sketch clears the selection
  whenever a transaction is aborted (`c1285d73725`, a crash fix -- kept).
  Of two lines picked for an angle only the second was selected, since the
  tool was ported. It selects what it holds again after each restart.
- *A served run*, `5f04a04793`, `serve-constraint-external-pick.py`: a
  constraint command is not on the browser's command list
  (`isBrowserSafeCommand`, held narrow over modal dialogs), so a browser
  cannot start one; it can join one. The desktop starts Parallel, the
  client's `Sketcher_External` -- which is on the list -- toggles the
  running tool, and its clicks, picked in its own mirror, make the
  reference and the constraint in one undo step. Both ways round (the
  desktop's session, and one the client began); both passed as built.
- *Clicked through*: every constraint command with an outside pick where
  its sequences take one, the Dimension tool, a vertex, a face, two
  outside elements, both intersection flavours, a pick in another body.
  Three defects, each fixed and in the test:
  - `e4f3a6a5be` -- a step offered anything outside the sketch as soon as
    it took an external edge OR a point, so a vertex became external
    geometry at a step that takes edges only. The gate is asked per
    element; with the intersection flavour it is what the cut gives that
    counts (an edge a point, a face edges).
  - `bab35c499d` -- a face picked in the Dimension tool and a click on
    empty space committed an undo step holding the outline and no
    dimension. The turn is committed only if a constraint was made.
  - `5715cde254` -- with the mode on, a click on empty space was left to
    the view whenever anything was preselected, the sketch's own things
    included; a diameter's label follows the pointer, so a circle's
    dimension could not be ended. Only something outside the sketch counts
    (`outsidePreselected`).

  What the sweep found working: one reference and the constraint in one
  undo step for Coincident, PointOnObject, the three Distances,
  Horizontal/Vertical, Perpendicular (either order), Tangent, Equal, Angle,
  Symmetric (three picks), Radius/Diameter on an outside circle and Lock on
  an outside vertex (reference constraints, the element being fixed); two
  outside elements refused by Parallel with nothing left, and taken by
  Angle as a reference angle; a face's outline left in the sketch and one
  piece of it then picked; a section line from a face under either
  intersection flavour; a binder made for another body's edge and undone
  with the constraint in the one step.

Left for a ruling -- both ruled 2026-10-02, see "The rulings of session
117, built (session 118)" below:

- *An edge cut by the sketch plane comes back twice.*
  `rebuildExternalGeometry` skips the projection of a FACE when it
  intersects, but projects an EDGE and adds the cut as well. For an edge
  normal to the plane that is the same point twice, so the tools see
  "several pieces" and make no constraint (the plain External
  intersection tool leaves two coincident points too); for a slanted edge
  it is the projected line plus the cut point. Dropping the projection
  would renumber the external geometry of files that already intersect
  edges.
- *What a browser is shown.* No cursor and no hint bar reach a client, so
  it has no sign of the mode; its tool bar mirror could show the external
  command checked. And whether the constraint commands that open no dialog
  go on the browser's command list.

### The rulings of session 117, built (session 118)

- *An edge taken by intersection is its cut alone* (`878becdec5`). The
  projection is dropped, by the sketch's hidden `_Version`: a new sketch is
  version 2, a sketch restored at 0 or 1 keeps projection and cut, since its
  constraints count on those geometries. Measured, sketch at z = 5: upright
  edge Point, Point -> Point; slanted Line, Point -> Point; lying in the
  plane Line, Line -> Line; an edge that never meets the plane was its
  projection and is refused now, as a face is. The Coincident tool with
  `Sketcher_Intersection` on takes an upright edge of a box as one point
  and makes its constraint.
- *A browser may start the constraint commands that open no dialog*
  (`5f761d0de6`). Sixteen by name; the dimensional ones, the datum editor and
  Snell's law stay off for their modal dialog. The list is in
  docs/ThinClient.md 8.7. Ruled the same day and built: `Sketcher_Defining`
  and the two intersection commands join `Sketcher_External` on the list
  -- they activate the same handler, and without them a browser could
  switch outside picking on in one flavour only -- and while a constraint
  tool runs with outside picking on, the command of the flavour in force
  is checkable and checked, and no other (`StackedExternalPick::
  showOnCommands`). That is what a browser is shown of the mode: its tool
  bar mirror carries an action's checked state, and the cursor's sign does
  not travel. The four are one group button, and a group's face is not
  drawn pressed for a checked member: the tick is on the drop-down's
  entry, on the desktop and in the browser alike. The commands are
  checkable only while checked, since outside these tools they are plain
  commands with no state. One trap on the way: `QAction::setCheckable` and
  `setChecked` emit `toggled`, which is how a checkable command is RUN, so
  setting the mark from the tool ran the command, which set the mark --
  the recursion ended in a segfault. The signals are blocked for the
  change.
- *The wall clock* (`dbc440f64c`): the spin after a rotation, a click
  against a hold in the navigation styles, and the hover pick's delay are
  measured on the steady clock. docs/Testing.md has the test that makes the
  step on demand.

### The preference pages (session 118)

Thirty-seven undecided rows name `SketcherSettings.cpp` or one of the
pages' `.ui` files. Twenty-three decided, fourteen left for a ruling. The
file is 569 lines here against 935 at upstream's tip, so it was read by
family, and the `.ui` files compared widget by widget against the tip
(which settled every text-only row at once).

| row | verdict |
|---|---|
| `4f429e3288` | **adapted** `e3ad693750`: upstream asks for a restart when the dimensioning mode, the unified coincident tool or the horizontal/vertical group changes. Here the workbench installs its tool bars again when the page is saved -- it did so for the dimensioning mode alone, the other two did nothing until the next start |
| `8ae1d9bbde`, `255949134f`, `2d5d1397a9`, `0814df7488`, `3d0aaeb616` | **taken** `e3ad693750`: the line group's check box. The workbench read `Commands/UnifiedLineCommands` already, and named `Sketcher_CompLine`, which was never brought over: set by hand, the option took BOTH line commands off the bar. The group is here now. Default off, as the workbench has it (upstream's is on) |
| `2e390f1543` | **n/a**: it syncs the restart check's property; the page compares the stored options before and after the save |
| `09209436d2` | **taken** `86b99936a3`: "Reset page" takes back the four settings the page stores by hand. `AutoScaleMode` is one more than upstream's list |
| `00228821d0`, `b4de78d3d7`, `ab9188a5dc`, `a00fe1e886` | **adapted** `2cb495d103`: the grid page's part -- line pattern icons painted from the palette at the device pixel ratio, upstream's seven patterns. Measured with light text: three entries, black, before |
| `d2491541e1`, `2903f480ae`, `880335a0f2`, `ee2f327a96` | **taken** `40b24b1000` |
| `a77f96ea86`, `5b59d94d55`, `98712d228b`, `1c591cd43a` | **have**: after `40b24b1000` no widget both sides have differs in its text, bar two kept on purpose (below) |
| `9189abe69b` | **have**: both readers default to "when no scale feature is visible" |
| `35700db40e` | **n/a**: "always add external geometry as reference". Here that is decided by the command (`Sketcher_External` or `Sketcher_Defining`), not by the construction mode |
| `21b56fe3fa` | **have**: the override is here; the rest is member order in a closed file and the font page |

Found on the way, no row for either:

- The coincident option's check box was unchecked by default while the
  workbench's default is the unified tool. With nothing set the page showed
  it off, and OK with nothing touched wrote `UnifiedCoincident = false`.
- The re-install put a button the bar did not have at the END of the bar:
  the tool bar manager keeps what a bar has and appends the rest
  (`ToolBarManager::setup`, deliberately, against flicker). Changing the
  dimensioning mode had this before today. The page empties the two bars
  its options decide before the re-install. A move of the button instead is
  not safe: a group's drop-down is set on the tool button when the action
  is added, and a move makes a new button.

Kept as the fork has them: the scaling mode's tool tip (it describes this
fork's rule) and the internal geometry check box (the feature here makes
more than faces).

Features the fork's own drawing code has to grow, none of them a port of
lines:

- *Line pattern and width by geometry type, and the Appearance page*
  (`b140feabaf`, 1572 lines; then `f5da655429` points coloured by
  construction state and the vertex colour removed, `e2f998f301` external
  defining solid / non-defining dashed in one colour, `411cdadf49` and
  `1155182ac3` a colour, pattern and width for external defining geometry,
  `c2d6248bc7` dimensional constraint line style, `90ca7a30d9` axis line
  width, `efec2c6795` the page's icon brush). Upstream draws through
  `EditModeCoinManager`, which is not compiled here; the fork has one
  curve style at 3 px and one dashed style.
- *Constraint symbol size* (`eef738b312`, `dc22fb4b9b`): a preference for
  the icon size, which here follows the font size.
- *Label font face* (`b9a89bada1`, `e992fef709`): a font box with a
  preview and a missing-glyph check.
- *Axis transparency* (`cda241dbd0`): the axes drawn through geometry in
  front of them, at a second transparency.

Ruled 2026-10-02: the four feature families are all to be taken (done,
"The appearance families (session 119)" below), and the defaults are upstream's -- **taken** `86c389b583`: Make
Internals on for new sketches (`be1d53cf5f`), dimension names shown, the
line group on. The line group's row above says "default off"; that held
for one day.

### The tip comparison over every open row (session 118)

For each undecided row, the files the commit really touches -- from the
commit, the ledger's file column is cut short -- were compared with
upstream's tip, whitespace aside. Eighteen rows touch only files that are
the tip here (`1d7b156fa3`): the default handler's family, the on-view
parameters of the three conic arc tools, the transform expression helper.
`DrawSketchDefaultHandler.h` differs from the tip by one thing, the tool
mode being a command (`90f0e23eac`).

Two more rows, and what they led to:

| row | verdict |
|---|---|
| `6dda56117a` | **superseded** `4574a91ba3`. It draws the curve while knots are placed, in `DrawSketchHandlerBSplineByInterpolation.h` -- a handler upstream later deleted, folding interpolation into the unified B-spline handler. The fork had the unified handler at the tip and still started the old one for the two "from knots" commands: no tool widget, no on-view parameters, no hints, a polygon for a preview. They start the unified handler now |
| `aab4bf329a` | **n/a**: an enum of the uncompiled information overlay converter |

Found by the test for that switch, no row for it -- `666865e05f`: **a tool's
click did not land where the pointer was.** On a press the sketch took
the 3D point of whatever the pick radius reached as the click's position,
for any hit (the fork's `1b87d4f072`, so that a drag starts on the curve
it grabs; upstream does it for a vertex only). A tool's own preview is
under the pointer and is picked like anything else, so the B-spline tool's
next point went ON its preview, three pixels short, while its mouse move
drew it at the pointer. Line and polyline were exact with the same
clicks, which is why it went unseen. A tool gets the pointer's place now,
or the vertex under it; a drag keeps the hit on the curve.

161 rows are left undecided. About a hundred of them touch at least one file that
differs from the tip in substance and need reading; the rest also name
files the fork does not have, or the uncompiled `EditMode*` sources.

### TaskSketcherConstraints.cpp (session 117)

Twenty-six undecided rows, all decided. Read by the DECISION column of the
ledger, not its status column: eleven more rows of this file read "open"
there and had been decided in earlier sessions.

| row | verdict |
|---|---|
| `67f8852697` | **taken** `2ebb3f89ee`: the filter's "Named" entry was never asked for. Measured: checked alone, it listed none of two named constraints |
| `ee1af2748a` | **taken** `e799d8c8e7`: `specialFilterMode` was read before it was ever set |
| `2d5d8ab86c` | **taken** `0a2a7b90d2`: a double click on a geometric constraint's row edits its name. Its focus guard belongs to the refocus timer of `061e185e7f`, not taken here |
| `766ee41b55`, `c0d47c5ecd` | **adapted** `44d7dfc55d`: a name is an identifier or empty. Measured before: "My Width", "a'b", "1st" and two blanks were all taken, and an emptied name did nothing. One thing more than upstream: a row whose text was refused gets its edit text back, or the next click on its check box asks for the same name again |
| `9cd3b31067`, `46ec53f4da`, `498968b89c`, `33d1d80555` | **adapted** `f7f5460d62` (App) + `26688bd419` (Gui), as an end state. See below |
| `6f90c5ea61` | **adapted** `d787c7275d`: the selection-following filters update once per batch, and only while the filter box is checked; both of them, where upstream defers one. 60 edges selected over 399 constraints: 0.29 s -> 0.225 s (0.22 s with no filter) |
| `54d235f8a5` | **taken** `fd35bc7263`: an unnamed constraint is listed by number and type, "7-Distance" |
| `0e1a9786e8`, `d5eda6def3`, `b8b90871a9` | **taken** `c789f0dea6`: "Delete All" and "Delete by Filter", with the Python `delConstraints` they need |
| `ecd591450c`, `0e24e121eb`, `4eb57fb50d`, `34881bc82e`, `ae76f89759`, `9d5e68b184`, `23537d97d7`, `4a770767d3` | **taken** `073246bef7` (+ `557d82ecb6`): one line or a few each, nothing changing what the panel does here. The 24 px icon size is what the style gave already (rows 26 px before and after) |
| `6eecd08f7c` | **taken** `5986b18091`, with Command.cpp's part |
| `a1f5d36584`, `e9f2e8fe92`, `69058376e6` | **have**, the fork's way |
| `0ee3c9f8e6`, `631ab0e7a4` | **n/a**: Base still has the conversion functions; Core's external icon theme is not here |

**"Show only filtered constraints".** The option was to draw only what the
list shows. Measured before, with "Named" alone checked: the four
filtered-out constraints were MOVED into the other virtual space -- the
user's own arrangement, and part of the document -- in two undo steps; the
list was not filtered; nothing was hidden in the view, because the write
did not reach the drawing; and nothing came back when the option was
switched off. Upstream's cluster gives a constraint a visibility of its
own (`Constraint::isVisible`, here since the take of `Constraint.*` and
set by nothing) and the panel sets that:

- `SketchObject::setVisibility(index or list, bool)`, C++ and Python,
  which writes nothing when every constraint is as asked already;
- the edit drawing honours it at the two places it decides a constraint is
  shown, its switch and its icon; a constraint selected or under the
  pointer is still drawn, as one in the other virtual space is;
- no transaction: hiding by a filter is not something to undo;
- switched off -- by the menu entry or by the preference, which only moved
  the check mark before -- everything is shown again;
- the filter's stored state moves to `SelectedConstraintFilters`, whose
  default leaves the two special filters out.

Upstream writes every constraint's visibility at every change of the
constraints, and a write is such a change; here only the constraints that
differ are written.

On the way, `d0e614d579`: any change of a row, a rename included, ended
with an "Update constraint's virtual space" command whether the check box
said anything new or not -- a rename left three undo steps, one now.

`tests/gui/sketch-constraint-panel.py` drives the panel's widgets for all
of it (38 checks); `TestSketcherSolver.testConstraintVisibility` and
`testDelConstraints` the two Python methods.

Seen and left: `renameConstraint` takes "Constraint9" as a name for
another constraint (the generated-name form is not refused).

### DrawSketchController.h (session 117)

Fourteen undecided rows, none of them open in fact. The fork's file is
upstream's at `bd6be559e8` but for one deliberate difference -- the view an
on-view parameter is made for is a `Gui::ViewerContext`, desktop or mirror
-- and every handler header those rows name is identical to upstream's
tip. A tip comparison settles a file's whole history at once; the rows'
other files were read where they differ (`DrawSketchDefaultHandler.h`: the
tool mode is a command here, the Escape handling is in;
`DrawSketchHandler.cpp`: the transaction ids declined with `f4665aa7b5`).

One thing the adaptation had dropped: upstream returns from
`initNOnViewParameters` when the document is not in edit, and the fork
asked the application for the edit document and used it unchecked. The
guard is back.

Kept, a look: `8bf54ad82f` greys the deactivated dimension colour further
(0.8 -> 0.5). The files it changes are not the fork's; the default is
`ViewProviderSketch`'s own here.

## 7a. The constraint-tool hints (session 85)

Thirteen rows, not the eleven the sweep sized: `580d538798`, the commit
that started the family, and `cda0d1201c` were open too and only turn up
by searching the ledger's subjects for "hint" rather than by reading the
one file's history.

    580d538798 17533deb50 871ee4ca32 582eae5ba3 8b36da6782 584472f779
    dbd72f9c60 a940181998   -> Gui: the constraint tools' pick hints
    b82408c545 cda0d1201c 1ac117f1c8 99f27f1a56 ea6469a7d7
                            -> Gui: the dimension tool's mode hints

Taken as an end state, not replayed. The file is `fork+2354 up-2547`, so
the thirteen commits would not have applied in sequence; and since the
fork had *no* hints here, there was no partial state to reconcile -- the
only question was what the finished feature should say, which is a
question about this fork's tools, not about upstream's diffs.

**The phrases were decided against `allowedSelSequences`, not copied.**
Each command declares the selection sequences it accepts; those
sequences say exactly which states the tool can be in and what it will
accept next. Reading them per command, rather than trusting upstream's
table, is what made the hints true here and found four things:

- **Upstream's table has unreachable rows.** Distance X/Y step 1 offers
  "pick second point or edge", but a single edge is dimensioned the
  moment it is picked (`{SelEdge}` is a one-element sequence, and the
  handler applies a sequence as soon as one completes), so step 1 is
  only ever reached from a first point. Its "place dimension" branch
  cannot run in either tree. Same for the "optional tangent point" and
  "optional perpendicular point" rows: every three-element sequence for
  those tools has a vertex in position 0 or 1, so the branch that would
  print them is shadowed. Those rows, and the constants that served only
  them, are not carried over.
- **Upstream's hint for tangent is wrong for upstream's own sequences.**
  After a first point, tangent accepts another point -- tangency through
  two endpoints, `{SelVertexOrRoot, SelVertex}` -- as well as an edge.
  "Pick first edge" names only one of them. Here it says "pick edge or
  second point".
- **The fork's sequences differ where upstream added tools it has not.**
  Upstream's angle takes a lone arc (`{SelArc}`) and its symmetric takes
  two edges; neither is here. Both are separate ledger rows, and neither
  changes a hint, but a table copied wholesale would have been written
  against them.
- **Point on object had no first-pick hint at all** in the first draft,
  because upstream's special case for it starts at step 1 and the
  fallback table has no row for the command. The test found it.

**The dimension tool's mode hints had to be rebuilt, not ported.** They
name the constraint the mode key would make next, and three things here
are not upstream's:

- the key is `Sketcher_NextToolMode`, a command (`90f0e23eac`), not a
  raw `SoKeyboardEvent::M` inside `registerPressedKey`, so the refresh
  hangs off `iterateToolMode()`. Both it and the hint now ask one
  `nextConstraint()`, so the tool and the bar cannot disagree;
- **the hint is computed while the tool's own preview is in the
  document.** `isHorizontalVerticalBlock()` answers "this line is
  horizontal" about the Horizontal the tool itself just previewed, so
  the mode hint vanished one press into the cycle. `cstrIndexes` is what
  this tool created; discounting it restores the question that was meant
  to be asked, which is what the line was before the tool started;
- **silence is a hint too.** A line that really is already horizontal,
  vertical or blocked has those three modes refused by `makeCts_1Line`,
  which resets the cycle instead; an equality between a line and an axis
  is refused the same way. Upstream promises the mode anyway. Here the
  mode line is empty, which is the true statement.

One more defect, independent of the port: `ToolHandler::activate()`
shows the hints *before* the activation hook, so a tool started on a
selection -- which the dimension tool usually is -- described an empty
selection until something else refreshed the bar.

**Guard**: `tests/gui/sketch-constraint-hints.py`, which reads the
rendered hint bar (`Gui::InputHintWidget` is a QLabel holding HTML) and
drives the sequences through `Gui.Selection`, since the constraint
handler advances on selection changes and needs no synthetic mouse.
Scored against the unpatched build first: 13 of its 16 checks fail
there, all twenty tools silent.

**Still not answered, and it is not this pick's job**: hints go to
`Gui::getMainWindow()->showHints()`, the desktop status bar. A browser
client's mirror has no main window, so a served session's hints are
drawn on the host and nowhere else. That is true of all ten drawing
handlers already; putting hints on the wire is a thin-client item, not
a port one.

### The appearance families (session 119)

Ruled "take all" on 2026-10-02. None is a port of lines: upstream draws a
sketch in edit through `EditModeCoinManager` and its two helpers, which are
not compiled here, so each family is the fork's own drawing code taught the
same preferences -- upstream's names, groups and defaults, so a
configuration moves between the two.

**Family 1: a line's width and pattern by what it is, and the page.**

| row | verdict |
|---|---|
| `b140feabaf`, `1155182ac3` | **adapted** `f436f45950`: normal, construction, internal alignment, external and defining external geometry each have a width and a pattern (`Mod/Sketcher/View`: `EdgeWidth`/`EdgePattern`, `Construction...`, `Internal...`, `External...`, `ExternalDefining...`), and so has the information layer. `draw()` sorts the curves into one indexed line set per class over the one coordinate and material list, each under its own draw style, where it had one solid and one dashed set |
| `f5da655429` | **adapted** `1848e8782b`: a point is coloured as what it belongs to -- the ends of a normal curve in the curve colour, every other point (a centre, any point of construction geometry) in the construction colour, an external geometry's in its own, the origin as a fully constrained element. `EditedVertexColor` and `FullyConstraintConstructionPointColor` are read no more and have no button |
| `e2f998f301`, `411cdadf49` | **adapted** `c340ac4113`: defining external geometry has `View/ExternalDefiningColor`, the external colour until set; what tells it from the rest is the line, solid against dashed. The fork's three states (frozen, detached, missing) keep their colours, a defining one lighter as before. `View/InformationColor` is read now too: the page has had upstream's button since, and nothing read it |
| `c2d6248bc7` | **adapted** `82fa9adadf`: `SoDatumLabel::linePattern`, and a dimension's leaders take `DimensionalConstraintLineWidth`/`Pattern`. The label draws twice here -- by hand in `GLRender`, and through a companion node for the render cache -- so the pattern is a line stipple in the one and a connected `SoDrawStyle` field in the other |
| `90ca7a30d9` | **taken** `d77a80324f`: `AxisLineWidth`/`AxisLinePattern` on the two axes, and the grid leaves out the line that would lie on an axis (`Part` grid extension) |
| `efec2c6795` | **n/a**: upstream shows a label of its own to read the style sheet's text colour for the line type icons. The pages here paint them from their own palette when the style reaches them (`2cb495d103`, measured with light text) |
| the page | **taken** `d658410b21`: `SketcherSettingsColors` is `SketcherSettingsAppearance`, upstream's form with the fork's three external state colours added, the tool preview colour left out (a preview is coloured by what it draws, `a3e4beb17f`) and the face colour kept on the fork's preference. "Reset page" takes back the eight line types, which the page stores by hand |

How the classes and the visual layers meet: a layer with a pattern of its
own (layer 1, "dashed") still wins. A class has two sets, and a curve on a
patterned layer goes to the second, which has the class's width and the
layer's pattern. Upstream never reads a layer's pattern.

What a user sees change with nothing set, all of it upstream's defaults:

- a curve is 2 pixels wide; it was 3;
- construction, internal alignment and external geometry are dashed
  (`0xFCFC`, drawn at twice its length); defining external geometry and
  normal geometry are solid;
- the information layer's lines are dashed;
- vertices are no longer red: an end point has its curve's colour;
- a tool's preview has the width and pattern of what it is drawing.

Mode 3 draws all of it: the render cache carries a draw style's pattern
and its scale factor to the backend. `tests/gui/sketch-line-styles.py`
samples the backend's frame along two lines -- the normal one is lit over
all of its length, the construction one over 0.75 of it, the twelve set
bits of sixteen.

**Family 2: constraint symbol size.**

| row | verdict |
|---|---|
| `eef738b312`, `dc22fb4b9b` | **adapted** `5678fce295`: `View/ConstraintSymbolSize`, on the Display page. The size was 0.8 of the label font's and followed it; unset it is the application font's height, as upstream has it, so a symbol is a quarter larger than it was until someone sets it. Here it is still multiplied by the device pixel ratio, which upstream's last version of the line dropped. The page shows the font's height while the preference is unset, not the 15 the form was drawn with |

**Family 3: the label's font.**

| row | verdict |
|---|---|
| `b9a89bada1` | **adapted** `ce59ec6a15`: `View/EditSketcherFontName`, a font box on the Display page with a preview in the view's colours and a list of the glyphs a label can show that the font lacks. Not taken: the label's default font name going from "Helvetica" to "osifont" -- neither tree registers that font for the application outside TechDraw, so the name resolves by fallback either way, and changing it here would move every label for nothing. And one thing done differently: a font box always holds some font, so upstream's page stores one on the first OK whether or not anybody chose it. Here the preference is written once a font was chosen, or one is stored already |
| `e992fef709` | **have**: the tool tip came over in its corrected wording |

The two font files of that commit are a newer osifont for `data/examples`
and the Sketcher's resources; neither file is in this tree (TechDraw
carries its own copy), so there is nothing to update.

**Family 4: axis transparency.**

| row | verdict |
|---|---|
| `cda241dbd0` | **adapted** `4bcb25919d`: `Mod/Sketcher/General/AxisTransparency` (30 percent by default) on the two axes, and its spin box on the Display page. The other half is not applicable: upstream draws the axes a second time with the depth test reversed, at `OccludedAxisTransparency`, so that they show through a solid in front of the sketch plane. Here nothing in front hides them to begin with -- the edit graph is an overlay drawn over the model (measured: a box above the sketch plane, seen from the top, has both axes drawn across it) |

Found on the way: the view's highlight kept a highlighted primitive's own
transparency. Right for a face; a hovered axis at 30 percent was drawn in a
mix of the preselection colour and the background, and
`sketch-highlight-view.py` said so. A highlighted line or point is opaque
now (`78daf4e506`, in the highlight cache, for every object).

Tests: `sketch-line-styles.py` (family 1), `sketch-display-settings.py`
(families 2 to 4), and the Appearance page in `sketcher-preferences.py`.
Full ctest 878 of 878.

### A datum's value edited in place

Ruling 4 of 2026-10-02 asked whether a dimension's value could be typed
at its label in a browser instead of in a modal dialog.

**Ruled 2026-10-02 and built** ("1 commit. 2 in place. 3 all at once. 4 on
enter"): a click elsewhere applies a valid value; in place is the default
on the desktop too; several dimensions get all their boxes at once with
Tab between them; a value is applied on Enter, with no preview while
typing. What follows is the proposal as it was put; this is what stands:

- `SketcherGui::editDatums()` (EditDatumDialog.h) is the one way to ask
  for a value. All five sites call it. In place -- when the view the event
  came through has no widgets, or `Mod/Sketcher/General/EditDatumInPlace`
  is on, which is its default and a check box on the Display page -- it
  starts a `DatumEditSession`: one `Gui::EditableDatumLabel` per
  constraint, each standing at its constraint's own label
  (`setAnchorLabel`). Otherwise, and for a reference or an
  expression-driven value, the modal dialog as before. "Edit Value" in the
  context menu is the full dialog wherever there can be one.
- Enter applies every box as it stands in one transaction; Escape applies
  none and aborts the open command; Tab and Shift+Tab move between the
  boxes; a press of the first button elsewhere in the view applies, and is
  used up by that together with its release. A box that holds no value
  keeps the session open on Enter and makes a click elsewhere a cancel.
- The callers return before the value is in. The Dimension tool goes on
  from a continuation (`afterDatums`); the view provider ends a session
  without calling back when the tool or the edit goes away, and the tool
  gets no mouse event while one runs.
- `EditDatumDialog::exec` refuses for a view without widgets, whoever
  calls it. With that the dimensional commands and
  `Sketcher_ChangeDimensionConstraint` are on the browser's command list;
  Snell's law is not.

Three things the build found:

- **A command's transaction is closed when the command returns.** The
  dialog never returned before the value was in, so the constraint and its
  value were one undo step by accident of modality. In place they were
  two, and Escape had nothing left to abort: the constraint stayed. The
  session keeps the transaction open past the command's scope
  (`App::AutoTransaction::setEnable(false)`, what entering an edit does).
- **A box's `value()` is the last committed value**, not what is typed:
  with keyboard tracking off the typed text waits in a cache for the box's
  own Enter handling, which the session's filter runs ahead of. The
  session reads the text.
- **A key off the wire ends the session from inside the label it is
  delivered to** (`sendKeyEvent`). Destroying the labels there was a use
  after free, a crash on the first Enter a client sent. They are taken off
  the screen at once and deleted later.

The first dimension of a freehand sketch still scales the whole sketch
(the auto scale), in place as in the dialog -- with one value only: the
scaling drops what it cannot scale, and the other boxes' numbers would
name other constraints.

Tests: `tests/gui/sketch-datum-in-place.py` (a command on a selection, the
panel, the Dimension tool, Escape, text that is no value, a click
elsewhere, the two cases that go to the dialog) and
`tests/gui/serve-datum-in-place.py` (a client starts
`Sketcher_ConstrainDistance`, picks, is shown the box in its "onview"
push, types through key frames; one undo step; no dialog on the host).
Not covered by a test: several boxes at once and Tab between them --
nothing a test can drive makes two driving dimensions in one go. And not
scored against the build before: the old binary was gone by the time the
tests were written; with the preference off the desktop test still shows
the dialog opening, which is the old behaviour.

**What is there today.** A constraint's value is edited in one place, the
modal `EditDatumDialog`, here and at upstream's tip, opened from five
sites: the constraint commands on a selection (`finishDatumConstraint`),
the Dimension tool's `finalizeCommand`, a double click on a label
(`ViewProviderSketch::editDoubleClicked`), an activated row of the
constraints panel, and `Sketcher_ChangeDimensionConstraint`. What is in
line is something else: the on-view parameters of a drawing tool
(`Gui::EditableDatumLabel`), and those already reach a browser -- the
"onview" push, the `onViewFocus` op and the key frames of
docs/ThinClient.md 8.7. The modal dialog is the reason the dimensional
commands are off the browser's command list: it blocks the GUI thread of
a process that serves several clients, with nobody at the host to close
it.

**The proposal: the entry box of 8.7 at the constraint's own label.**
An `EditableDatumLabel` in the acting view, its `SoDatumLabel` given the
constraint's type, points and label parameters so that it stands exactly
where the constraint's label is drawn, the constraint's own label hidden
for the extent of the edit (the same switch a virtual space uses). The
box holds the value selected, as the dialog does. On the desktop it is
the `QuantitySpinBox` over the view; on a mirror it is the unshown box
whose text is streamed, and the client draws and places it with the code
it already has. No new wire message, no new client code: the set of
on-view parameters simply has one member while a datum is edited.

- Enter commits: the same `setDatum` in the same "Edit sketch datum"
  transaction, the same first-dimension auto scale, the same history
  entry. The commit is taken out of the dialog into one function that
  both call, so the two cannot drift.
- Escape cancels. Where the constraint was just made (a command or the
  Dimension tool), that aborts the creation, as the dialog's Cancel does.
- The units, the parser and the validation are the spin box's, so
  "10 mm", "1 in" and "2*3" behave as in the dialog.
- Which view: the one the event came from. Two clients of a shared
  session each edit their own datum in their own view.

**What changes shape: the callers are synchronous and this is not.**
All five sites run `exec()` and read the result on the next line; the
Dimension tool loops over the constraints it made and stops at the first
rejected one. A box in the view returns at once. So the editor takes a
continuation -- `editDatum(view, index, done)` with `done(accepted)` --
and each site moves what followed `exec()` into it. The Dimension tool's
loop becomes "edit the next one from `done`"; continuous mode restarts
the tool from the last `done`. This is the one part that is real work,
and the part to test hardest (undo steps counted before and after for
every site).

**The dialog's other three fields, and where each goes.**

| field | proposal |
|---|---|
| name | not in the box. The constraints panel renames in place already (its context menu and F2); the dialog stays reachable on the desktop for those who want both at once |
| reference check box | not in the box. `Sketcher_ToggleDrivingConstraint` does it and is on the browser's list already |
| expression | the box is not bound to the property, so "=" does not open the formula editor. A constraint that HAS an expression is not edited in place: the desktop opens the dialog as today, a browser is told the value is driven by an expression |

**Desktop too, or browsers only?** Proposed: one preference, in place by
default for a view without a widget (it has no alternative), and the
user's choice on the desktop, default the dialog as today -- so nothing
changes for a desktop user until they ask, and `Edit Value` in the
context menu keeps opening the full dialog either way. Prior art is on
the side of in place: Onshape, Fusion and SolidWorks all put a small
value box at the dimension when it is placed or double clicked, with
name and expression elsewhere. `ShowDialogOnDistanceConstraint` keeps
its meaning under either: whether a new dimension asks for its value at
all.

**The guarantee a served process needs** is not the box but the absence
of the dialog: `EditDatumDialog::exec` refuses when the acting view is a
mirror, whatever called it, and says so in the report view. With that
in, the dimensional commands and `Sketcher_ChangeDimensionConstraint`
join the browser's command list; Snell's law keeps its own dialog and
stays off.

**Questions for the ruling.**

1. A click elsewhere while the box is open: commit what is typed (a
   spreadsheet's rule, and what a touch user expects), or cancel (the
   dialog's rule for a click on its close button)? Proposed: commit a
   valid value, cancel an invalid one.
2. The desktop default: dialog (proposed) or in place?
3. Several dimensions made at once (the Dimension tool on a rectangle's
   two sides): one box after another, as the dialogs come today
   (proposed), or all boxes at once with Tab between them, as a drawing
   tool's parameters work?
4. A live preview while typing -- the sketch re-solved on each valid
   value, put back on Escape -- or the value applied on Enter only, as
   the dialog does (proposed for the first cut)?

**Tests it would come with**: a desktop one (double click, type, Enter;
Escape; a new dimension; the Dimension tool's two-constraint case; an
expression-driven datum falls to the dialog), and a served one (the
client starts `Sketcher_ConstrainDistance`, picks, receives the box in
its "onview" push, types through key frames, Enter sets the datum in one
undo step; the host shows no dialog -- measured first on today's build,
where the dialog opens on the host and the test must fail).

### One editor for a constraint's value, all of it (session 120, design)

Asked at the end of session 119 (2026-10-03): "migrate all constraint
value editing with in place editor including expression and reference
driving. Current expression editor already has an in place mode. You can
reference that implementation. Build a 'super' editor that can do
everything. Mirror that in browser. Also since the editor got complex,
lets use one editor only and use tab to move the editor among multiple
constraints. Also when moving you need to consider the extra space of the
editor to not obscure. Also check cases when in the middle of editing what
will happen if someone undo".

**Status: ruled 2026-10-03 and built; see the end of this section.**

#### Measured first: an undo while a box is open

A probe against the session 119 build (one box per constraint, held by
constraint INDEX). Each case opens a box, does the thing, then types a
value and presses Enter:

| case | what happens today |
|---|---|
| a new dimension, then Std_Undo (menu, tool bar) | `Document::undo` commits the open creation and undoes it: the constraint is gone, the box stays open over nothing. Enter applies nothing; the only word is in the notification area |
| Ctrl+Z typed into the box | the line edit keeps it (its own text undo); the document is not touched |
| an existing dimension, then an undo that puts a deleted constraint back BELOW it | **the wrong constraint is written.** The box was on Edge2's 50; the undo put Edge1's constraint back at index 0; Enter wrote 77 into Edge1's, without a word |
| an existing dimension, then an undo that removes it | the box stays; Enter does nothing (notification) |
| a redo that appends a constraint; a recompute | the box stays and is still right |

The cause is the index. `ViewProviderSketch::slotUndoDocument` already
drops a drag for the same reason (`cancelInteractionOnUndoRedo`, upstream
16aff10544, where an index past the end crashed `moveConstraint`), and
nothing does the same for the datum session. The third row is a defect
in what session 119 shipped, whatever becomes of the editor.

#### Prior art

- **Onshape**: a double click on a dimension makes its number an entry
  field, Enter applies. Expressions in the same field, variables as
  `#name`. Driving/driven is the dimension's context menu, not the field.
- **SolidWorks**: the Modify box at the dimension; an `=` in it starts an
  equation, with a type-ahead list of names and functions.
- **Fusion**: one field takes a value or an expression, autocompletes
  parameter names, and Tab moves the focus to the next dimension.
  Typing `name=value` there names the dimension as it sets it.
- **FreeCAD itself**: `DlgExpressionInput` in its frameless mode (no
  system background, a proxy widget for the mouse, placed over the spin
  box that opened it by `adjustPosition`), opened by `=` in a bound spin
  box: a text field with the completer, a result line under it that says
  what the expression gives or why it cannot (re-evaluated 300 ms after
  the last key, function calls disabled while editing), Discard, and Enter
  to accept.

#### The proposal

**1. One editor, holding everything the dialog held.**

- **The value line** is one field. FreeCAD's own rule decides what it
  holds: text that starts with `=` is an expression, anything else a
  value, parsed with the units and arithmetic the spin box takes today.
  Typing `=` turns the line into an `ExpressionTextEdit`, with its
  completer, and opens a **result line** under it: the value the
  expression gives, or why it gives none. That is `DlgExpressionInput`'s
  validation taken whole (`validateExpression`, the unit check, the
  function-call disabler), not a second copy. Deleting the `=` turns
  the line back into a value, and applying a value then removes the
  expression, which is what the dialog's Discard did. A constraint that
  has an expression opens with the line showing it.
- **Driving or reference** is a toggle at the start of the line, drawn
  with `Sketcher_ToggleDrivingConstraint`'s icon. A reference opens
  showing its measured value, greyed. Typing a value or an `=` makes it
  driving, the dialog's rule (`datumChanged`, `formEditorOpened`).
- **The name** is a small header row over the line, showing the name or
  a greyed "name" when there is none. A click or F2 moves the keys there.
  The name is checked as now (`checkConstraintName`) and applied with
  the value.
- Weight and Snell's ratio take a plain number in the same editor, so
  Snell's law can join the browser's command list.

**2. One editor that Tab moves.** One transaction runs from the start of
the session to its end. For a new constraint that is the transaction of
the command that made it, as now. For an existing constraint it is
"Edit sketch datum", opened at the first change.

- **Tab applies the entry to the sketch and moves on.** The value,
  expression, driving flag and name go into that transaction (the sketch
  solves, the labels move), and the editor moves to the next constraint;
  Shift+Tab moves to the previous one. Ruling 4 stands as it was meant:
  there is no preview while typing. Tab is a commit point, as it is for
  a drawing tool's parameter.
- **Enter** applies the current entry and commits: the whole session is
  ONE undo step. **Escape** aborts the transaction, so everything the
  session applied goes, a new constraint included. **A click elsewhere**
  is Enter (ruling 1). Text that is no value stops Tab and Enter, and
  the result line says why.
- The other way -- keep every value pending and apply all of them on
  Enter -- would need the labels to show numbers the sketch does not have
  yet, and would put the solver's verdict at the end, where one value
  that cannot be met spoils all the others. Applying on Tab costs one
  thing: the geometry moves between two Tabs.
- **Which constraints Tab visits.** When the editor is opened for
  several constraints (the Dimension tool's two), it visits those. When
  it is opened for one (a double click, the panel, Edit Value), it visits
  every dimensional constraint whose label the view shows, in constraint
  order, starting from that one. Labels off the screen are skipped.
- The auto scale of a freehand sketch's first dimension happens when that
  dimension is applied. The session follows its constraints by tag (next
  item), so the constraints the scaling drops do not throw it off.

**3. Where the editor goes, so that it hides nothing it edits.** The
value line sits over the number it edits, as the box does today: that is
what "in place" means. Everything else -- the header, the result line --
grows AWAY from the constraint's geometry:

- "Away" is a direction on the screen: from the label's foot on the
  dimension (the point of the dimension line under the text; for a radius
  or diameter, the point on the arc) to the text's centre. The extra rows
  go on that side of the line, in an order that keeps the line nearest to
  the dimension. For a dimension whose text sits to the side (a vertical
  distance), the rows go below.
- If the editor would leave the view on that side, it flips to the other
  side, and only then is it clamped into the view.
- The editor is placed again whenever it changes size (the result line
  appears), the camera moves (the sensor it has), or Tab moves it.
- Covering the constraint's own geometry and dimension line is what this
  avoids. Other constraints' labels may still be covered: they are not
  tracked.
- A browser applies the same rule itself: the push carries the foot as a
  second world point beside the anchor, and the client projects both on
  every frame. Placement is the client's job (ThinClient.md 8.7).

**4. The browser.** The editor travels in the "onview" push as a
parameter of a kind of its own. Everything in it is display state: the
line's text and selection, which field has the keys (the line or the
name), the mode (value or expression), the result line and its level
(value, warning, error), the driving flag, the name, the visible
completions and the current one, and the foot. Keys go up as `'E'`
frames to whichever field has them, as now. The rest is one op,
`onViewAction {index, action, arg}`, for the toggle, a click into the
name, and a click on a completion. The client draws a DOM editor with the
same layout and implements none of its behaviour (8.7's rule).

- **The completer is the one hard part.** `QCompleter` shows its list
  in a popup widget, and the keys that drive the list go through that
  popup. A mirror shows no widget, so on a mirror the server needs
  `ExpressionTextEdit` to handle Up, Down and Enter for the completion
  itself, and the push carries the rows of the list.
- Not the panel mirror (Sandbox.md 7.19). It reflects a task dialog of a
  document to every subscriber, it writes back through setters
  (`textEdited`), so the `=` handling and the completer would be passed
  by, and it has no anchor in the 3D view.

**5. An undo while the editor is open.**

- **Std_Undo or Std_Redo during a session ends the session as Escape
  does, and does nothing older.** That is a spreadsheet's rule: while a
  cell is being edited, undo cancels the edit and goes no further back.
  The work done before the session needs a second undo. To build it,
  `Gui::Document::undo`/`redo` ask the view provider in edit first: one
  virtual, false by default, which the sketch answers while a session
  runs.
- **An undo the session cannot catch first** -- a Python `doc.undo()`,
  another client of a shared session -- reaches it only afterwards,
  through `slotUndoDocument`/`slotRedoDocument`. If the session's
  transaction was open, `Document::undo` has committed it and undone it:
  the document has moved past the session, and the session ends. If
  nothing was applied yet, the session follows its constraints by TAG
  (`Constraint::tag` survives an undo; the expression engine uses it for
  the same reason). A constraint that has gone leaves the cycle, and if it
  was the one being edited, the session ends.
- **Following by tag fixes the wrong-constraint defect** in the table
  above, and it can go in before the rest, as its own commit, with the
  probe turned into a test.

**Questions for the ruling.**

1. Tab applies as it moves, with the whole session one undo step and
   Escape taking all of it back (proposed), or values held pending until
   Enter?
2. An editor opened on one existing dimension: should Tab visit every
   dimension the view shows (proposed), or only the set it was opened for
   (for one constraint, Tab does nothing)?
3. The name: a header row, reached by a click or F2 (proposed), or
   Fusion's `Name = value` typed into the line?
4. Std_Undo while editing: end the edit and nothing more (proposed), or
   end it and also undo the step before it?
5. The modal dialog: does it stay for desktop users who turn
   `EditDatumInPlace` off (proposed), or is it removed together with the
   preference?
6. A key for the driving toggle (Ctrl+Shift+D, say), or the mouse
   alone?
7. Fix the wrong-constraint defect now, on its own, before the editor
   (proposed)?


**Ruled 2026-10-03: all seven as proposed; the toggle gets a key.** The
user added: the browser already has completion logic -- reuse it. As
built:

- **Undo first** (`Gui::ViewProvider::undoRedoInEdit`, asked by
  `Gui::Document::undo`/`redo` before anything is undone, so Std_Undo and a
  client's `undo` op both reach it; the sketch answers it while an entry
  runs, as Escape). An undo it cannot catch reaches
  `DatumEditSession::documentRewound` after the redraw: the editor follows
  its constraint by `Constraint::getTag()` (new, read-only), or ends when the
  constraint is gone or the undo took the entry's transaction. Test
  `sketch-datum-undo.py`, written from the probe and failing on the session
  119 build. The same run found a **segfault**: `DatumEditSession::start`
  looked the labels up, then ended the previous session -- whose apply
  redraws and can free them. Reordered.
- **`Gui::DatumValueEditor`**: one `ExpressionLineEdit` with the lead '='
  (the spreadsheet cell's rule: completion only after '='), parsed and
  formatted by a never-shown `QuantitySpinBox`; a result line fed by
  `Gui::Dialog::checkExpression`, the formula editor's validation taken out
  of `DlgExpressionInput::onTimer` so both judge alike (one difference:
  with the completer open a half-typed name leaves OK disabled rather than
  as it was); the driving toggle (Ctrl+Shift+D -- the command's own "K, X"
  is letters, which the line takes); the name row (F2). Function calls are
  disabled only around the evaluation of the typed text: the dialog held
  the global disabler while shown, which an editor that applies on Tab
  would have held across its own recomputes.
- **Placement**: `SoDatumLabel::getLabelAwayDirection()` -- across the
  dimension line on the text's side, along the radius, along the angle's
  bisector, from the arc's centre. The line stands over the number, the
  other rows go to that side (up only when the geometry is clearly below
  the text), flip when they would leave the view, then clamp.
- **The session** (`DatumEditSession`, rewritten): one editor; Tab applies
  into the one transaction and moves; the cycle is the set given, or every
  dimension the view shows; an App transaction is made active at the start
  ("Edit sketch datum" unless one is), opened for the document lazily so
  an entry that changes nothing leaves no step. "Edit Value" is in place
  too; the dialog is reached only with `EditDatumInPlace` off.
- **Snell's law** draws an icon, not a label: the editor stands at the
  refraction point (`Target::point`), with no toggle. The command makes the
  constraint with the last ratio given (the dialog's history, read and
  written through a never-shown `PrefQuantitySpinBox`; 1 when there is
  none) and hands it to the editor. On the browser's command list now.
- **The browser**: `Gui::OnViewEntry` is the seam the view's registry and
  the mirror use (`EditableDatumLabel` and `DatumValueEditor` both stand on
  it). The push gains `ax/ay/az` (after the anchor, before the text, where
  the viewer's scanner looks), `kind`, and for a datum `field`, `expr`,
  `result`, `level`, `driving`, `nameShown`, `name`, `nameSel`, `obj`; the
  uplink gains `onViewAction` (`toggle`, `field`, `replace`). The completion
  list is the client's: `pathcomplete.ts`, the omni box's object and
  property completion moved out of `omni.tsx` so both use it; a taken row
  goes up as `replace`. **Found on the way**: the push numbered boxes by
  their place among the SHOWN ones and `onViewFocus` looked them up in the
  whole set, so a tool that hid a parameter made a tap focus the wrong box.
  The push now carries the registry index.

Two defects the tests found on the way:

- **The auto scale split the entry in two.** Editing the one dimension of
  a freehand sketch scales it (`performDatumAutoScale` -> `centerScale`),
  and the scaler opens and commits a command of its own, "Scale
  geometries": `openCommand` with a transaction active commits it and
  starts another unless an enabled `App::AutoTransaction` is on the stack.
  Session 119's apply had one around it; an entry that stays open across
  Tabs cannot. The scaler takes `inTransaction` now (set when it is part of
  a larger operation, which `centerScale` always is) and opens and commits
  nothing.
- **'=' left the unit behind.** The line opens with the NUMBER selected
  (as the dialog's box), so '=' typed over it gave "= mm", and a taken
  completion "=Sketch mm". Found by the browser drive, which types; the
  desktop test had set the text. '=' at the start of a value, or over a
  selection that starts there, now makes the whole line "=".

Tests: `sketch-datum-editor.py` (26: one editor, Tab and Shift+Tab apply
and move, one undo step, Escape takes back what Tab applied, '=', an
expression set and removed, one that cannot be bound, Ctrl+Shift+D,
typing into a reference, F2 and the name, placement off the measured line,
Snell's law), `sketch-datum-undo.py` (15), `sketch-datum-in-place.py` (16,
an expression now opens in the editor), `serve-datum-in-place.py` (16: the
datum fields, an expression and the toggle through `onViewAction`; the
refused-command check moved to `Sketcher_MapSketch`), and by hand, in a
real Chrome, `serve-datum-browser.py` (13, `scripts/datum-drive.js`: the
DOM editor, kept off the line, the client's completion offering `Sketch`
and taken through the server, the toggle, Escape).

**A property's members, completed in the browser (session 121).** What
was left of the editor: typing `=Sketch.Constraints.` offered nothing in a
browser, while the desktop's completer lists the named constraints. The
desktop asks the property (`Property::getPaths`, which
`PropertyConstraintList` answers with its named constraints, a vector with
`x`/`y`/`z`, a placement with `Base.x` ... `Rotation.Angle`); the browser
completes from the property descriptors `getProperties` sends, and those
named the property and stopped. Two ways to close it were weighed: a new
op asking one property's paths when `Obj.Prop.` is typed, or the members
in the descriptor. The second was built. Every `getPaths` in the tree is a
short list of strings, so the descriptor of an object's property gains
`members` (`ThinClient.md`, the property descriptor) and the reply the
client already holds for `Sketch.` answers `Sketch.Constraints.` too: no
op, no round trip, no second cache.

- `Gui/SceneControl.cpp`, `describeProperty`: `members`, the sub-paths as
  `ObjectIdentifier::getSubPathStr` spells them -- the string the desktop's
  completer shows. Only for a document object's properties: an expression
  names no others.
- `web/src/pathcomplete.ts`: `rows()` takes an `ExprPath`. With it the
  path goes on past the property (`Sketch.Constraints.Wi`,
  `Box.Placement.Base.`), the object being the first name and then down
  while the next names a sub-object; and the properties of the object the
  expression is on need no object in front (`Constraints.Width`), or the
  grammar's own leading dot (`.Constraints.Width`; `.5` stays a number). A
  name that is no identifier completes as `Constraints[<<a name>>]`, the
  bracket in the dot's place. Without the argument nothing changed: the
  omni box opens a property's editor, and a member leads to none.
- `web/src/onview.tsx`: the editor passes its sketch as the expression's
  object. It names constraints itself and moves from one to the next, so
  the descriptors are asked again each time it moves
  (`forgetProperties`). And the lit row is the first again when the token
  changes -- it stayed where the last list had left it.

Before: `serve-datum-browser.py`, extended first, failed its four new
checks on the build as it was (`rows: []` after `=Sketch.Constraints.` and
after `=Constraints.`). Not done: a member's value beside its name in the
list (the desktop shows none either), and members of a view provider's
properties, which no expression can name.

**Escape, ruled 2026-10-03 after the build** ("make escape equal to enter.
for a vim user like me, I hate escape means anything other than escape",
then "make that a setting default to escape instead of undo"): Escape in
the value editor leaves it as Enter does -- what is typed is applied, the
entry committed; text that is no value keeps it open, as Enter does.
Taking an entry back is an undo: Std_Undo while it runs (the whole entry,
a new dimension with it, nothing older), or one undo after, the entry
being one step. The preference `Mod/Sketcher/General/DatumEscapeTakesBack`
(Display page, off by default) makes Escape the cancel it was. A
completion list open over the line still closes on Escape: that is the
list's, as vim's own completion menu.

### The ledger, session 121: 145 rows to 54

**What "undecided" counts.** A row is undecided when its status is `open`
or `partial` and its decision column is empty: 145 at the start of the
session. Counting the empty decision column alone gives 357, because the
`have` and `n/a` statuses never needed a decision -- the two numbers that
earlier notes quoted (359 and 161) were these two counts.

**The method: relative to upstream's tip, not to the commit.** The older
sweep asked of each line a commit added whether the fork has it. Most of
these rows are old, and upstream has since rewritten much of what they
wrote, so "missing here" was mostly "gone there too". Three measures per
row instead:

- per added line: here; or missing here and still at the tip (a real
  absence); or gone at the tip as well (superseded, says nothing);
- per removed line: still here while gone at the tip (stale);
- per file the row touches: its distance from the tip's file, whitespace
  aside. `DrawSketchHandlerSlot.h`, `...Arc.h`, `...Translate.h` and
  fourteen more are the tip's blob; `DrawSketchHandler.cpp` is the tip but
  for the fork's transaction, viewer and pixel-ratio deltas.

42 rows came out with nothing missing and nothing stale.

**Two things the numbers do not say, both met here.**

- *A missing line is not a missing behaviour.* `efb10e1b28`'s null check,
  `a0847c22c7`'s `std::remainder` and `5f90e988a0`'s id map all read
  "missing" and are all here, in the fork's own words. Each was read.
- *"Superseded upstream" is not "kept here" in a file that has diverged.*
  `a7e1760bfb` (the Elements panel no longer redraws the whole list per
  selected element) has no line left at the tip, and the fork's panel,
  2449 lines from the tip, batches selections its own way. Whether it has
  the quadratic redraw is a question about the fork's code. So a clean
  score decided a row only where the row is a test, a text, a build change
  or noise, or its files sit at the tip; fix and feature rows in diverged
  files were read or left.

**Decided: 91.** 41 by the sweep (`have` or `superseded`; the 42 less
`a7e1760bfb`), 4 already decided in this document and never written to the ledger (`387d25c219`,
`9ce1cae190`, `5587b48a0f`, `9a1020929e`), 16 build rows and 5 Core rows
as `n/a` with the missing facility named and checked (no
`target_compile_warn_error`, no `disable_occt8_deprecation_warnings`, no
`FrameOption`, no `associateToObject3dView`, `createEditor` without a
`std::function`), and the rest read one by one.

**What reading found.**

- `55c36e8c03`, declined in an earlier session on principle, **was a bug
  here, and a deeper one** (`f67388f543`). The B-spline tools open their
  command in `activated()`, inside the tool bar command, and
  `Gui::Command` closes the open transaction when it returns. On the first
  use of the tool the points went into the document with no transaction:
  cancelling after one point left its circle, with nothing to undo it.
  `DrawSketchHandler::openCommand()` takes the transaction out of the
  enclosing command's hands and remembers its id; `deactivate()` aborts
  that one and no other. The polyline opens its command the same way.
- The test for it **crashed**: with continuous mode off, finishing a
  B-spline by a right click ran `finish()` twice, the second time on the
  deleted handler (`ed7668fbf0`). Upstream's text has the same second call
  in both tools.
- `7432ce131f` (`0cfd91c04d`): Toggle Construction stopped at an ellipse's
  axis and left the rest of the selection untoggled.
- `e828c5da4d` (`d2e9f8559d`): a named reference dimension was editable in
  the property editor.
- `ca4660167e` (`60c324cbcf`): `*.ttc` font collections.
- Three icons the commands name and the resources lacked (`3b504408f6`):
  the periodic B-spline by interpolation had none at all.
- Six rows without behaviour in one commit (`e626f51909`).

Tests: `sketch-bspline-cancel.py` (23), `sketch-toggle-construction-
internal.py` (6), `sketch-constraint-property-readonly.py` (4), each
scored against the tree before its change.

**Left: 54, by family.** Each needs the file read as the constraints panel
was in session 117, not a sweep.

| Family | Rows | What is known |
| --- | --- | --- |
| `CommandSketcherTools.cpp` | 7 | copy/cut/paste (`fd2e35b7eb`) is mostly here; related constraints for non-edges (`732501d89d`), the leak fix (`5268aa43db`) and two cleanups are not scored |
| `TaskSketcherElements.cpp` | 7 | the panel is 2449 lines from the tip; the selection speedup, the hover-during-rename fix, clearing the selection from an empty click, the context menu |
| `CommandCreateGeo.cpp` | 6 | the group command class (`d18a48ddb1`), the file's rearrangement, the line group, the polyline shortcut |
| `SketchAnalysis` and the validation panel | 6 | three refactors, `5696ee821c` (#14240), `37f0ad43f9` (validation cannot be scripted) |
| Topological naming, which began in this fork | 7 | `71870bd6f3`, `ecf7e51ab3`, `55acedb83d`, `38c6d842f2`, `4aaf72dcc2`, `0bddc51805`, `27ca64a201`: upstream's import of the fork's own code, to be compared as a whole |
| `EditDatumDialog.cpp` | 5 | the dialog is the fallback since the value editor of session 120; the radius/diameter switch (`c9041132f9`) is the one feature |
| Large features nearly all here | 6 | chamfer (`b3fe5bba28`: one line missing, two stale), symmetry (`e4213fc10f`: eleven stale lines), intersection externals (the fork's own; two icons absent), the perpendicular hint lines, offset with external input, the angle expression as an AST |
| Others | 10 | `155edc0f53` (isActive), `6e1826295b` (a test hook the fork's copy of the test calls and does not have), `94d39087d3` (the External tool shows no hint), strings, two refactors |

**Ruled the same day** ("Keep the name"), `bb19c35c18`, which leaves 53.

- The tool bar and menu stay "Sketcher visual" (upstream: "Visual
  Helpers", `945ba15e18`): a tool bar's name is also the key its place is
  saved under. What that row fixed on upstream's side -- the translation
  marker not matching the name -- was true here too: the marker block
  listed "Sketcher virtual space", a tool bar that does not exist. It
  names this one now.
- `Sketcher_ViewSketchGroup` is the fork's own (`3b620b713e`, 2022): the
  drop-down of "Align View to Sketch" and "View sketch bottom". Upstream
  has the first command only, with the icon `Sketcher_ViewSketch`. The
  group named an icon of its own, `Sketcher_ViewSketchGroup`, that no file
  ever was: a group command shows the icon of one of its members and needs
  none (ruled the same day; none of the fork's other group commands names
  one). `bb19c35c18` first pointed the name at upstream's icon, which was
  the wrong answer to a line that should not be there; the line is gone.

### The ledger, session 122: the last 53 rows

Decided with the user, family by family: each row read against the fork's
code, laid out with a recommendation, and ruled on (2026-10-03). No row of
the ledger is undecided now.

| Outcome | Rows |
| --- | --- |
| have, or n/a | 24 |
| declined | 9 |
| taken or adapted | 20, in 13 commits |

**Have or n/a, 24.** All seven topological naming rows: upstream's import
of this fork's own code, and the one loop upstream fixed on the way in (an
erase while iterating) is written with a saved next iterator here. The
chamfer, symmetry and offset rows, whose upstream tests run here; the group
command class (the fork's groups were `Gui::GroupCommand` already); copy,
cut and paste; the Elements panel's "quadratic redraw" (`a7e1760bfb`: the
fork's panel finds the row in a map and touches that row alone) and its
context menu, which is built from the commands.

*A note of the session before was wrong:* `6e1826295b`'s hook,
`SketcherGui.getActiveSketchPreselection`, is here (`AppSketcherGui.cpp`)
and its test runs in ctest.

**Declined, 9.** By earlier rulings: the tool bar's rename (`3e32ea5dd4`;
asked whether upstream's Core adds the workbench's name to a tool bar's --
it does not: the string given to `setCommand()` is the object name, the
title's source and the key the bar's visibility is saved under, on both
sides, and upstream migrates nothing); a fifth variant of upstream's
per-part icons (`aa26d9ff8a`); the polyline's shortcut (`855bce62cd`,
`c71c15c009`: it is M here). As no behaviour: the file's reordering
(`f9d6609687`), review-comment style (`1cfb85a71f`), name-parsing helpers
(`f4134951e5`). By the user, the finer `isActive` (`155edc0f53`): a greyed
button in place of the message the command gives already, for a walk over
the selection per command at every state update. And `913c30429c`, which
reads as a refactor and is not one: `!isDimensionless()` is true of an
invalid quantity, `isQuantity()` is not.

**Taken.** Each measured before its change.

- `732501d89d` (`eb3e1a7f71`): Select Associated Constraints looked at
  names beginning with "Edge" only; an end point or an axis selected
  nothing.
- `5268aa43db`, `fe8d2845ea` (`2f749e8b18`): the clipboard copy never freed
  its clones. 63.5 MB over 30 copies of 3000 lines; under 1 MB after.
- `1d4a09366c` (`a90371e71c`): hovering the Elements list took the keyboard
  from a text being typed. Here that includes the value editor at a
  dimension's label, which takes a lost focus as "done".
- `98d64f9939` (`777535259e`): **not upstream's defect.** A press on the
  empty part of the list cleared the selection here already -- unless the
  pointer had rested on a row. The list toggled the row last *entered*,
  whichever row the press was on: a press with no move before it (a tap, a
  click forwarded from a browser) selected nothing, and a press on the
  empty part selected the row hovered last. The view now says which row a
  press is on.
- `581dee4d48` (`9ffada9e3e`): the icon's name only. The list entries the
  commit adds are not needed here -- a group hands the construction mode
  on to its members -- and `sketch-construction-icons.py` is that
  measurement, kept.
- The six `SketchAnalysis` and validation rows (`659ef5ee0d`): four files
  in which the fork had nothing of its own, taken at upstream's tip. The
  validation panel's fixes are Python commands on the sketch now, so a
  macro keeps them.
- `8b06bca68a` (`64fffd18d4`): the supplement of an angle given by
  `atan(0.03)` came out as `180 - atan(0.03)`, a number minus an angle.
  Upstream builds the expression as a tree; this fork's tree keeps its
  operator codes private, so the unit is that of the *evaluated* value and
  "180 - x" is undone only when a re-parse `isSame()` -- which also stops
  `180 - 60 + 5` being read as a supplement. No upstream Sketcher test is
  left disabled.
- `94d39087d3` (`5ee1ec9a1b`): the External tool's hint, for each of the
  fork's flavours.
- `bfe1295b6b` (`53ed49bc4f`): the helper lines were drawn already; the
  check box that turns them off was missing.
- Four rows without behaviour (`f71163ef6e`).

**Found on the way, not a row** (`f5c3e9b5ab`): a clipboard copy grew with
the square of the selection -- an id table rebuilt per constraint, a linear
search per constraint element, and `PythonConverter` formatting each whole
list anew per element. 0.93 s for 4000 lines before, 0.023 s after.

**The radius/diameter switch** (`c9041132f9`; ruled: "add to in-place
editor"). Upstream has two radio buttons in its datum dialog and writes
the constraint's type on the live constraint. Here:

- `SketchObject::setDiameter(index, state)`, Python too (`744d2a7d45`):
  the constraint changes kind in place -- index, name, driving kept -- and
  the circle keeps its size, the value doubled or halved with the kind. A
  value an expression gives is left to the expression, which then gives
  the other measure.
- `Gui::DatumValueEditor` (`bf40d0fee9`) can switch a value between two
  measures, generically: a target names the two and the factor between
  them. A button beside the driving toggle, Ctrl+Shift+R. While nothing
  has been typed the number is restated (5 mm as a radius reads 10 mm as a
  diameter); a number that was typed is left as typed, and so is an
  expression. The push to a client states the measure and its name, and
  `onViewAction` takes `measure`.
- The Sketcher's session (`8acf9665c8`) names Radius and Diameter with the
  factor 2 and applies the kind with the value, in the one undo step.

  This differs from upstream in one visible way: there a radius of 5
  switched and accepted untouched becomes a diameter of 5, half the
  circle; here it becomes a diameter of 10, the same circle.

Tests: `sketch-datum-measure.py` (24), `serve-datum-measure.py` (10),
`SketcherTests/TestSketchRadiusDiameter.py` (7), and by hand
`serve-datum-measure-browser.py` in a real Chrome (10).

**Verified** at `f5c3e9b5ab`: full build, ctest 897/897 (885 before, plus
ten GUI tests and two C++ cases), `FreeCADCmd -t 0` 2896 OK (2889 before,
plus the seven `setDiameter` cases).

**Not checked.** A file saved by *upstream* with intersection externals:
upstream keeps the kind in an `ExternalTypes` property this fork does not
read. Keyboard navigation in the Elements list (arrow keys change the
list's selection with no press): it goes through the same "row last
entered" logic the press no longer does.

### The two unchecked items (session 123)

**Keyboard navigation in the Elements list** -- `52e60fd1f9`. Probed
before anything was changed, and the probe did not find what was expected
(the hovered row toggled by an arrow key). It found this:

| what | before | now |
|---|---|---|
| Down, Up, End | nothing: the selection stays where it was | the row the key moves to, alone |
| Shift+Down, Shift+Up | the row last pressed is DESELECTED | the selection grows and shrinks by a row |
| a press on one of two selected rows | both deselected | the pressed row stays |
| Shift+press | sometimes the two end rows alone | the rows in between |
| a press on the only selected row | stays selected | the same |

One cause. When the list's selection changed, the panel toggled a
remembered row -- the row last pressed (`777535259e`) or entered -- and
never read which rows the list had selected. A plain arrow was inert by
accident: the list reports that change twice, so the row was toggled off
and on again. Shift+arrow reports once.

The rows' edge flags are read from the list's own selection now, and the
remembered row, the index before it and the `rowPressed` signal are gone.
A row's highlight is written only when it disagrees with its flag:
`QTreeWidgetItem::setSelected()` makes the list commit the range a
Shift+arrow is extending, and Shift+arrow the other way then takes nothing
back (found by the test, after the first version of the fix).

Home does not reach the list: it is the application's shortcut for the
home view. Left as it is.

Test `sketch-elements-keys.py`, 19 checks, 9 failing on the sources before
the change (rebuilt for the score).

**Verified** at `52e60fd1f9`: full build, ctest 898/898 (897 before, plus
this test). `FreeCADCmd -t 0` was not run: the change is in SketcherGui,
which that binary does not load.

**A file saved by upstream** -- measured, NOT built: it needs a ruling.
There is no upstream build on this box, so the file was forged: a fork
sketch saved, then its `Document.xml` rewritten to what upstream's tip
(`bd6be559e8`) writes -- the kind of each external in `ExternalTypes`, no
`Intersection` flag on the geometry (upstream's extension knows five
flags, the fork's sixth is this one), no `_Version`. Scripts:
`~/works/sw/fcad-probes/exttypes/` (`make.py`, `forge.py`, `open.py`).

The file opens and looks right: the external geometry is stored, and it is
shown as stored. It goes wrong at the first rebuild of the externals --
here, the box made 2 longer:

| external | a fork file | the upstream-style file |
|---|---|---|
| an edge, by intersection | the point, moved with the edge | a LINE, the edge's projection; the sketch point coincident with it jumps to the line's start |
| a face, by intersection | the cut line, moved | the face's projection |
| a face square to the sketch, by plain projection | a segment 10 long | a line 20000 long; a point on its end goes to y = -9995 |

No error, no warning, the solver reports success. Two causes:

1. The kind is not read. Upstream keeps it beside the links, the fork on
   the geometry; neither reads the other's.
2. The sketch is taken for an old one. `_Version` is absent from an
   upstream file, so it restores as 0, and 0 means "built before the fork
   changed this": the 20000 long line, an edge's projection kept beside
   its cut. But `ExternalTypes` exists since upstream 1.1.0 (`0e5e071d72`,
   2024-11-08), which is after upstream's own change to the square face
   (`1c514f5a15`): a sketch that carries the property was built the way
   version 2 builds. The third row needs no intersection at all.

   (That upstream 1.1 writes the short segment is taken from the port of
   `1c514f5a15`, not from running upstream.)

How the kinds map:

| upstream | it builds | the fork's word for it |
|---|---|---|
| 0 projection | the projection | no flag |
| 1 intersection | the cut alone | the `Intersection` flag, at `_Version` 2 |
| 2 both | the projection, then the cut | none: a reference is one or the other (an edge at `_Version` < 2 happens to be both) |

The same holds the other way round, by reading upstream's code, not by
running it: upstream carries the fork's sixth flag along without knowing
it, and finds no `ExternalTypes` -- a fork file's intersections are
projections there.

Put to the user:

- **Read only.** `handleChangedPropertyName` takes `ExternalTypes` when the
  restore meets it; `onDocumentRestored` sets the flag on the geometry of
  every reference of kind 1 and gives the sketch `_Version` 2. About 50
  lines, and it touches no file the fork wrote, which never has the
  property.
- **Kind 2** needs a place: a second flag ("also projected") read by the
  rebuild, so that a reference can be both. Without it a kind 2 reference
  loses one half or the other.
- **Both ways.** A real `ExternalTypes` property, written from the flags
  on save, so that upstream reads the fork's files right. Then the read
  side is not a hook but the property itself, and when the property was in
  the file it rules over the flags, which upstream does not maintain.

The backward-compatible restore rule of `CLAUDE.md` is about the fork's own
files; whether upstream's are to open right here, and the fork's there, is
the question.

## 8. Phases

0. Groundwork: ledger, the split, the App-level Python tests.
1. `planegcs/`: take the directory at upstream's tip, adapt `Sketch.cpp`
   (enum solve results, removed parameters). Check old files for the signed
   point-line / circle-line distance migration before committing.
2. App fixes, commit by commit.
3. Gui fixes: tools and commands first; rendering/preselection adapted or n/a.
   Every tool change is checked against served/browser editing and the
   widget-less on-view parameters.
4. Small and medium features.

Verification for every commit: build `conda-relwithdebinfo-801`, full
`ctest`, `FreeCADCmd -t 0`; solver and persistence changes also open old
sketch files; tool changes also run the served-edit GUI tests.
