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
