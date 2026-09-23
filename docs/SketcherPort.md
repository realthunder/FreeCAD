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

**Where the ledger stands (2026-09-23).** 1149 rows, of which 323 are open
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
fork does not have -- its colours live in `SketcherSettingsColors.ui` --
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
