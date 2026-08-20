# Porting upstream's Draft and BIM

Status: plan, nothing ported yet. Measured 2026-08-20 against
`upstream/main` `dc2ed9d586` (2026-08-18) and this fork at `0a6a5a7468`.

Supersedes the BIM-only first draft of this file, which got the Draft
question wrong; see "A correction" below.

## Why bother

This fork still ships `src/Mod/Arch` and a Draft from the 0.22 line.
Upstream has no Arch at all any more -- its files live in `src/Mod/BIM`,
together with the BIM addon's command set and, more importantly,
**NativeIFC**, which treats the IFC file as the model rather than
importing it.

The immediate reason is import cost. On
`Autodesk-Research_210-King.ifc` (155 MB, IFC2X3, 23578 products, 13638
with a representation, 6971 `IfcMappedItem`), measured here:

| path | result |
| --- | --- |
| Arch parametric objects (`MERGE_MODE_ARCH=0`, the default) | killed after **9 h**, 40% through `doc.recompute()`, 5206 objects failing to recompute |
| plain Part shapes (`MERGE_MODE_ARCH=2`) | **599 s**, 12298 objects, 12140 with a shape, 45227 solids, 4.93 GB peak, 171 MB `.FCStd` |

The 9 hours were not ifcopenshell (parse is 5 s) and not the product
loop. They were `ArchComponent.computeAreas()`, which runs a TechDraw HLR
projection per component; it threw 4168 times on this model.

## The baseline that makes all of this measurable

`git merge-base HEAD upstream/main` is **empty** -- the fork's history
was rebuilt, so not one commit is shared and there is no 3-way merge or
cherry-pick available. But the fork's last upstream merge, `bcaa82d71a`
(2024-04-26), has as its second parent `a662fbb2ff`, which is
**upstream's tree as of 2023-12-31**. That commit object is still in the
repository, so it serves as BASE for a proper three-way reading:

    BASE    a662fbb2ff   upstream, 2023-12-31
    OURS    HEAD
    THEIRS  upstream/main, 2026-08-18

Everything below is measured that way, excluding translations.

## A correction

The first draft of this plan said porting upstream Draft "means giving
up the fork's Draft work" and put it out of scope. That was read off the
ours-vs-theirs diff (244 files, +25658/-16567), which conflates two and
a half years of **upstream's own** evolution with the fork's changes.
Against BASE the picture is the opposite:

| | files | lines |
| --- | --- | --- |
| Draft, fork changes (BASE..OURS) | **33** | **+662 / -266** |
| Draft, upstream evolution (BASE..THEIRS) | 305 | +33370 / -23566 |
| Arch, fork changes (BASE..OURS) | **13** | **+1047 / -367** |

Files the fork touched that upstream did not: **zero**, in both. So the
whole re-application burden is 46 files and roughly 1700 added lines,
against tens of thousands of lines of upstream work we would be picking
up. Porting Draft is not the expensive half; it is the cheap half.

## What the fork actually owns

**Draft (33 files).** One coherent cluster plus odds and ends:

- Link-based arrays: `draftobjects/draftlink.py`,
  `draftviewproviders/view_draftlink.py` (74 new lines),
  `view_array.py`, `draftmake/make_{array,orthoarray,polararray,
  circulararray,patharray,pointarray}.py`,
  `drafttaskpanels/task_{ortho,polar,circular}array.py`,
  `draftgeoutils/geo_arrays.py`, the three array `.ui` files, and
  `draftguitools/gui_{patharray,pointarray,pathtwistedarray}.py`.
- `draftutils/groups.py` (+100) -- the group/visibility semantics.
- `draftutils/params.py` (108), `WorkingPlane.py` (-72 net),
  `draftguitools/gui_move.py` (109), `gui_trackers.py` (31).
- Small: the DXF/SVG/OCA/DWG/AirfoilDAT importers, `DraftGui.py`,
  `facebinder.py`, `view_point.py`, `draftutils/utils.py`.

**Arch (13 files).** Mostly two features and yesterday's fixes:
`importDAE.py` (722) and `importOBJ.py` (454), the facet-colour export
work, plus `preferences-dae.ui`; then `importIFC.py` (24),
`importIFCHelper.py` (75) and `importIFCmulticore.py` (13) -- the
ifcopenshell 0.8 fixes -- and single-digit edits to `InitGui.py`,
`ArchCommands.py`, `ArchComponent.py`, `ArchBuildingPart.py`.

Outside these two directories, the deltas any transplant has to respect
are core, not workbench: **App::Link/ArchLink**, the appearance change
`9ebb513d35` ("give a view provider an appearance, and retire
ShapeMaterial" -- upstream BIM touches `Transparency` 228 times,
`DiffuseColor` 184, `ShapeColor` 86, `ShapeAppearance` 40), the PySide
shim `220f297e94`, and `4de4b667bc`, which despite its subject line is
two lines in **`src/App/GroupExtension.cpp`** plus a test, not an Arch
change.

## What upstream has

**Draft**: 243 Python modules and 4 C++ files (`App/AppDraftUtils*`,
`DraftGlobal.h`) -- the same four we have.

**BIM**: 578 files, 224 Python modules, 53.5 MB, and **zero C++**. All
the `Arch*.py`, plus `bimcommands/` (83 modules), `nativeifc/` (21),
`importers/` (17), `utils/`, `geometry/`, `Presets/`, `Dice3DS/`,
`Resources/`. Wired as `BUILD_BIM -> add_subdirectory(BIM)`, a plain
file-install list plus `PYSIDE_WRAP_RC` for `Resources/Arch.qrc`.

## Both were smoke-tested against this fork's core

Each upstream tree was staged (`git archive upstream/main <dir> | tar
x`), put first on `sys.path`, and every module imported inside our own
build under xvfb. For Draft the already-imported `draft*` modules were
purged from `sys.modules` first, or FreeCAD's startup copy wins and the
test silently measures a mixture.

**Draft: 174 of 222 modules import clean, and the 48 failures have ONE
cause** -- `module 'FreeCADGui' has no attribute 'UserInput'`. That is
upstream's InputHint system: an enum registered from C++
(`registerUserInputEnumInPython` in `Application.cpp`) with
`InputHint`/`HintManager` wrappers in `FreeCADGuiInit.py`, backed by five
files in `src/Gui` (`InputHint.h`, `InputHintPy.{h,cpp}`,
`InputHintWidget.{h,cpp}`) that we do not have. Draft uses it 54 times,
almost all `Gui.UserInput.MouseLeft`. Nothing else in 222 modules
required a core API we lack.

**BIM: 109 of 177 modules import clean** once two staging gaps are
filled -- BIM reads presets from `<resource>/Mod/BIM/Presets`, and
`freecad/deprecation.py` is one 87-line upstream file we lack. What
remains is 30 modules failing on
`Line.__init__() got an unexpected keyword argument 'mode'` (upstream
`draftguitools.gui_lines.Line(mode="line")` against our
`Line(wiremode=False)`), 23 on a build-generated `generated_sql_parser`,
and 2 on `create_pip_call` missing from our AddonManager.

**That is the argument for doing them together**: BIM's one API drift is
upstream Draft's own signature. Port Draft first and it disappears
rather than needing a shim.

## Two more things worth knowing

**Upstream's legacy IFC importer is behind us.**
`src/Mod/BIM/importers/importIFC.py` still calls
`settings.set(settings.USE_BREP_DATA, True)` -- the pre-0.8 ifcopenshell
API. The four fixes committed here on 2026-08-19 (`f0e8efb6ac` MeshPart,
`04810d888e` the 0.8 settings API, `0a6a5a7468` body representation and
the `only` list mutation, and the `by_type` tuple fix) are not upstream,
and their importer has the same bugs. Their `importIFC.py` and
`importIFCHelper.py` are the one part of BIM that must be **merged**
with ours rather than taken; ours should go upstream as a PR.

**NativeIFC already speaks the ifcopenshell we have.**
`nativeifc/ifc_generator.py` branches on
`hasattr(settings, "ITERATOR_OUTPUT")`, which is True against the
0.9.0alpha wrapper built here, and both `"ITERATOR_OUTPUT"` and
`"CONTEXT_IDENTIFIERS"` are accepted. 0.8.5 is also packaged here if a
pinned version turns out to matter.

**Upstream fixed the thing that cost us nine hours.** `computeAreas` is
now an `AreaCalculator` class with `Part.OCCError` handling, the
`MaxComputeAreas` guard and an explicit workaround for TechDraw's "crazy
edges" (longer than 9999.9 mm).

## Status (2026-08-20)

All five stages are done and committed on `LinkVibe`.

| stage | commit | gate |
| --- | --- | --- |
| 0 AreaCalculator | `a96bf81a40` | King parametric import, 1300 s cap: `Part.Wire` failures 398 -> 0, total 993 -> 536 |
| 1 `Gui.UserInput` (ported) | `c160feaae7`, `19bb5c42a8` | upstream Draft modules importing 174/222 -> 220/222; the shim is gone, hints display |
| 2 Draft transplant | `9ea92ae952` | `TestDraft` 82 tests / 5 failing (was 67 / 1), `TestDraftGui` 38 / 1 |
| 3 Arch -> BIM | `82a8a4478d` | `TestArch` 280 tests / 7 failing, now 5; King import 473 objects, 298 solids, 8.7 s against 9.1 s |
| 4 NativeIFC | `d71c8a9fcd`, `a187adbd75`..`0d52fa4244` | King opens in 5 s and its building structure costs 18.2 s, against 635 s -- see below |

### Stage 1 -- the shim is gone

`19bb5c42a8` replaces the 190-line Python shim with upstream's real
subsystem: `InputHint.h`, `InputHintPy.{h,cpp}` and
`InputHintWidget.{h,cpp}` verbatim, and upstream's own Python
`InputHint`/`HintManager`, which call through to the main window rather
than discarding. Four adaptations, none in the hint code:
`InputHintWidget` takes `QLabel` rather than `StatusBarLabel` (a class
that exists to drive a status-bar item registry this fork lacks);
MainWindow places it with `statusBar()->addWidget()` since
`addStatusBarItem()` is likewise absent; and `Base::PyRegisterEnum` and
`BitmapFactoryInst::empty()`/`getMaximumDPR()` came across because this
fork had neither.

WARNING: `MainWindowPy::createWrapper()` copies a hard-coded list of
attribute names onto the PySide wrapper. A new `add_varargs_method` is
invisible from Python until its name is in that list --
`Gui.getMainWindow()` just returns a plain `QMainWindow`.

### Test standing, all four suites re-run 2026-08-20

| suite | tests | failing |
| --- | --- | --- |
| `TestDraft` | 82 | 5 |
| `TestDraftGui` | 38 | 1 |
| `TestArch` | 280 | 5 |
| `TestArchGui` | 41 | **0** |

`TestArchGui` was a registered suite the plan never gated -- an
omission, not a result. It had three failures when first run, and all
three are now fixed, none of them in ported code:

- `testImportSH3D` wanted MeshPart, which was not in the build at all.
  `BUILD_MESH_PART` defaults ON, but CheckInterModuleDependencies
  required `BUILD_SMESH`, which no user can set -- only `BUILD_FEM`
  turns it on -- so every build without FEM silently lost MeshPart, and
  with it FlatMesh and OpenSCAD. Upstream does not list `BUILD_SMESH`
  there; it enables SMESH whenever MeshPart is on, which here would pull
  in VTK for a mesher BIM does not use. Instead `Mesher.cpp`'s
  `createFrom(SMESH_Mesh*)`, the one definition left outside the
  `HAVE_SMESH` guards, is now guarded, so the module builds either way.
  `3bd39c9ba7`.
- `testBuildingPart` hit the fork's "Auto correct group member" error.
  `GroupExtension`'s single-group rule asked `isNonGeoGroup()`, which
  matches derived extensions, so `App::GroupExtensionPython` counted --
  and ArchBuildingPart carries it. Upstream asks for the exact
  extension. `c086cc5a03`. A plain
  `App::DocumentObjectGroup` still triggers the rule, a scripted
  container no longer does.
- `test_texture_scenegraph_structure` looked up a Coin node called
  "FlatRoot". Upstream's ViewProviderPartExt names its four
  display-mode roots and the face draw style; the fork named only the
  wireframe node. The nodes were there, unnamed.

Both core changes were checked by reverting the file and re-running:
`Document` stays at 110 tests / 9 failing and `TestPartApp` /
`TestPartGui` at 75 / 1 and 4 / 3, the same failures by name.

Supporting commits: `087a4d01a4` (task panel buttons as a PySide6 enum),
`6df7f94a36` and `d763bd8ee5` (`addProperty` keywords on the view
provider and the document), `21df0beab6` (`freecad.deprecation`),
`d5989ce60f` (`create_pip_call`).

### Stage 4 -- NativeIFC

`d71c8a9fcd` cleared the blocker: ifcopenshell 0.9 dropped
`entity_instance.wrapped_data`, which BIM used at 14 sites, and all of
them now go through version-tolerant helpers. Both NativeIFC tests pass.

What the King file measures, with `LoadOrphans` and the other optional
loads off:

| | NativeIFC | Arch importer |
| --- | --- | --- |
| parse 155 MB IFC | 4.7 s | 5 s |
| open, root object only (`strategy=0`) | 5.3 s | -- |
| **document usable** | **10 s**, saves to 5.5 KB | **599 s**, saves 171 MB in 138 s |
| open, building structure (`strategy=1`) | **18.2 s** for 294 objects | -- |

That last number was **635 s** when the stage was first measured, which
was worse than importing the entire model the old way. The whole of the
gap was one mechanism, and it is worth stating plainly because it is not
a per-object cost and it is not in the tree-building code at all:

**NativeIFC was writing to the IFC file while merely reading it, and
every write invalidates IfcOpenShell's inverse index.**

On a clean file an inverse lookup -- `IsDecomposedBy`,
`ContainsElements`, `LayerAssignments` -- is free, because IfcOpenShell
keeps an index. One `attribute.edit_attributes` call on King costs
1.28 s by itself and drops that index; the next lookups pay ~16 ms each
rebuilding it, and then it is free again until the next write. Interleave
N writes with the tree walk and the cost goes superlinear in file size,
which is exactly the shape that was measured (the same call on the 25 MB
NVW DCR-LOD200 model was 1.3 s for 99 objects with a flat profile).

Three separate paths were doing the writing:

- `create_object()` called `ifc_layers.add_layers()` for every object,
  ungated, though the bulk `load_layers()` pass it duplicates is gated on
  `LoadLayers` (default off). `add_to_layer()` then appended the
  *product* to the layer's `AssignedItems` -- where IFC does not keep
  layer membership, so the element was never already there and the write
  always fired, and `populate_layer()` cannot read a product back out of
  it either. `a187adbd75`.
- `ifc_export.get_placement()` used `0.001 / calculate_unit_scale()`
  where `importIFCHelper.getPlacement()` wants millimetres per file
  unit, `calculate_unit_scale() * 1000` -- the reciprocal, and the same
  factor the two explicit callers in that file already pass. The two
  agree for a file in millimetres, which is why it survived. King is in
  feet, so every object was placed at 1/92903 of where it belongs, the
  storey `Elevation` expression followed the bad `Placement.Base.z`, and
  33 storeys were rewritten in the file: `21.0` became
  `0.00022604211875090419`. `c839d86208`.
- `set_attribute()`'s `differs()` compared floats exactly, so a length
  that round-tripped through a millimetre property came back a few ULPs
  out and was written back. `a9886ce33e`.

Opening King and expanding it one level now performs **zero** API writes.

A fourth fix is independent of all that: `generate_geometry()` built the
element's decomposition before consulting `ShapeMode`, and discarded it
when nothing wanted a shape -- 12.2 s of the remaining 43.8 s, and
quadratic in subtree size besides, since `get_decomposed_elements()`
dedupes with `if el not in result` over a list. `4a9db86314`.

With `LoadLayers` on, the same expansion is 19.8 s, which before was not
reachable at all.

`09742e3e86` is a separate repair to `34d09e66cb`. Making `defer()` call
straight through without an event loop fixed the segfault at exit but
started running GUI-only work headless: `ifc_import.insert()` defers
`toggle_lock_off()`, which reaches `FreeCADGui.getMainWindow()`. So a
headless NativeIFC import raised. `set_menu()`, `set_button()` and
`on_activate()` now return early without a GUI, which takes
`nativeifc.ifc_selftest` from 15 errors + 1 failure of 20 to 2 + 2.

Verified unchanged by the whole series: `TestArch` 280 tests / 5 failing,
the same five as before. The four `ifc_selftest` failures left are
pre-existing gaps the crash was hiding -- an IFC2X3 file created with no
unit assignment (08c), object counts (09), a placement move that never
reaches the property (10), a missing `ExtrusionDepth` (11) -- and are
identical with and without these commits.

### `LoadOrphans`, which was the largest thing left

`load_orphans()` creates a FreeCAD object for every `IfcProduct` the
spatial tree does not reach, so that a file's contents are not silently
dropped. It ran after every import regardless of strategy, and defaulted
to on with no control anywhere -- not in `dialogImport.ui`, which
already carries checkboxes for its four siblings, and not in
`preferencesNativeIFC.ui`, which exposes sixteen other entries.

Re-measured after the fixes above, so this is volume and not the write
pathology -- it performs zero API writes:

| King, `insert(strategy=0)` | before | after |
| --- | --- | --- |
| `LoadOrphans` off | 5.2 s, 2 objects | 5.0 s, 2 objects |
| `LoadOrphans` on (the default) | **735.9 s, 10925 objects** | **5.0 s, 2 objects** |

Three things were wrong, and all three are now fixed.

**A port is not an orphan** (`e407f8611d`). `get_orphan_elements()` tests
`Decomposes`, `ContainedInStructure` and `VoidsElements`. IFC parents a
port elsewhere: IFC2X3 puts it on the port as `ContainedIn`, an
`IfcRelConnectsPortToElement`; IFC4 nests it. Neither was checked, so
**9824 of King's 10879 orphans were `IfcDistributionPort`s, every one
with a non-empty `ContainedIn`** -- and only 1055 of the 10879 had a
`Representation` at all, so most were connection nodes that can never
draw anything. Excluding `ContainedIn`/`Nests` takes King to **1055
orphans, all of which have a representation**. `get_children()` now
follows the same two relations from the other end, through `HasPorts`
and `IsNestedBy` and outside the `only_structure` branch, so what stops
being an orphan becomes reachable under its element instead of
vanishing.

**A root-only open must stay root-only** (`d71cf30009`). `load_orphans()`
is skipped at strategy 0, and the project's IFC context menu gains "Load
Orphan Objects" beside the other on-demand loaders, so nothing becomes
unreachable. Strategies 1 and 2 are unchanged; with the port fix in,
strategy 1 carries 1393 objects in 73.4 s.

**The knob is reachable** (`0d52fa4244`). `LoadOrphans` now appears in
both the import dialog and the preferences page.

Editing a `.ui` used not to regenerate `Arch_rc.py` on an incremental
build -- the resource target depended on `Arch.qrc` alone, which the
CMakeLists comment documented and worked around by telling you to touch
the `.qrc`. Fixed in `79df6c69bb`: `PYSIDE_WRAP_RC` now parses the
`.qrc` and depends on the files it lists, for all six generated
resources. `Draft_rc.py` turned out to have been silently stale.

### What the remaining test failures want, none of it in ported code

- `App::Document.settings()` -- per-document settings, a whole
  DocumentSettings class. Three Draft grid tests. Grid parameters fall
  back to preferences meanwhile.
- TechDraw templates under `Templates/ISO/`, and its pattern parser
  ignoring a DOS EOF marker. Three tests across both workbenches.
- The C++ DXF importer's pending-exception fix. One test.
- An MKS unit schema. Two BIM tests.
- `noElementMap` on `Part.makeFace`/`copy`/`fuse`, which this fork's
  Part API has no equivalent of, so a transient analysis face keeps its
  element map. One test.
- An Arch Report writing no spreadsheet cells. One test.

### Two things worth knowing before stage 4

**Upstream's BIM always goes multicore.** `MULTICORE` is computed as
`max(1, ifcMulticore)`, so the preference cannot turn it off, and
`importIFCmulticore` ignores the `only` argument -- a bounded import of
a few hundred products silently becomes a full one. Pass
`preferences["MULTICORE"] = 0` for a bounded run.

**Run the test suites against an empty `FREECAD_USER_HOME`.** A
SketchArch addon in the user profile calls `FreeCADGui.addCommand` at
import; in console mode that raises, and it took 110 of `TestArch`'s
280 tests down before the cause was visible.

## The plan

Five stages. Each ends in a measurement, and the fork keeps working
after every one.

### Stage 0 -- backport `AreaCalculator` (hours, independent)

Port upstream's `AreaCalculator` into our `ArchComponent.py`. Nothing
else moves. Gate: re-run the parametric import on 300 and 2000 King
products; expect the 4168 exceptions to go to about zero. Worth doing
first because it is small, it serves the import-speed work on its own,
and it is not invalidated by anything later.

### Stage 1 -- decide `Gui.UserInput`

Two ways, and the choice sets the ceiling for everything after it:

- **Port the InputHint subsystem** (5 files in `src/Gui` plus the enum
  registration and the widget wiring). Gives the on-screen hints and
  keeps upstream Draft unmodified. The cost is touching `src/Gui`, the
  fork's most diverged area.
- **Shim it**: a Python enum named `Gui.UserInput` plus a no-op
  `HintManager`. Cheap, keeps `src/Gui` untouched, and loses only a UX
  nicety.

Recommendation: shim first to unblock Stage 2, and port the real thing
later if the hints prove worth it. The shim is the kind of thing that
must be one file with a comment saying what it stands in for.

### Stage 2 -- transplant Draft (the cheap half)

Take upstream `src/Mod/Draft` wholesale, then re-apply the 33-file fork
delta listed above, Link arrays first. Gate: **upstream's own Draft test
suite**, `drafttests/` (20 test modules including `test_array.py`,
which is exactly where the fork's Link work lives) plus `TestDraft`. The
smoke test only proves imports; the suite is what proves runtime.

### Stage 3 -- transplant Arch as BIM

Take `src/Mod/BIM`, add `freecad/deprecation.py`, the `Presets`
install, the `generated_sql_parser` build rule and the `create_pip_call`
shim. Re-apply the 13-file Arch delta -- and **merge** rather than take
`importers/importIFC*.py`, keeping our ifcopenshell 0.8 fixes. Gate:
`TestArch`, then the King model imports at least as well as today's
599 s / 45227 solids.

### Stage 4 -- turn on NativeIFC

It comes with BIM; this stage is about making it the preferred path.
Gate: time to a browsable tree on the 155 MB King file, against today's
599 s import plus 138 s save.

## Cost and risk

Stage 0 is hours. Stages 1-2 are the bulk of the risk but the smallest
diff: one missing Gui API and 33 files to re-apply. Stage 3 is the
larger transplant but the better understood one, since BIM is pure
Python and cannot reach OCCT, Coin or the renderer -- its worst failure
mode is a workbench that does not load. Stage 4 is where the payoff is.

The risk this plan does **not** cover is runtime drift: 174 and 109
modules importing proves the API surface is close, not that behaviour
matches. That is what the two test suites are for, and why they are the
gate on every stage rather than a final check.

## Reproducing the smoke tests

    git archive upstream/main src/Mod/Draft | tar x -C <scratch>
    # in the script: purge sys.modules of Draft*/draft* first, then
    # sys.path.insert(0, <scratch>/src/Mod/Draft), then import every
    # module and bucket the exceptions by type and message.
    env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb xvfb-run -a \
        .conda/run.sh .conda/limited.sh \
        ./build/conda-relwithdebinfo-801/bin/FreeCAD <script>

For BIM, symlink the staged tree to `build/<dir>/share/Mod/BIM` so the
presets resolve, and append a directory holding `deprecation.py` to
`freecad.__path__`.
