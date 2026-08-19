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
