# Porting upstream's BIM workbench

Status: plan, nothing ported yet. Measured 2026-08-20 against
`upstream/main` `dc2ed9d586` (2026-08-18) and this fork at `0a6a5a7468`.

## Why bother

This fork still ships `src/Mod/Arch` from the 0.22 line. Upstream has no
Arch at all any more -- its files live in `src/Mod/BIM`, together with
the BIM addon's command set and, more importantly, **NativeIFC**, which
treats the IFC file as the model rather than importing it.

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

## What upstream actually has

`src/Mod/BIM`: 578 files, 224 Python modules, 53.5 MB, and **zero C++**.
It holds every `Arch*.py` we have, plus `bimcommands/` (83 modules),
`nativeifc/` (21), `importers/` (17), `utils/`, `geometry/`, `Presets/`,
`Dice3DS/` and `Resources/`. It is wired as `BUILD_BIM ->
add_subdirectory(BIM)`, a plain file-install list plus `PYSIDE_WRAP_RC`
for `Resources/Arch.qrc`.

## Seven measurements that shape the plan

### 1. There is no merge base

`git merge-base HEAD upstream/main` is **empty**. The fork's history was
rebuilt at some point, so not one commit is shared even though the trees
are obviously related. There is no 3-way merge and no cherry-pick from
upstream: every port is a file-level transplant whose diff we write
ourselves.

### 2. BIM is pure Python

No `.cpp`, no `.h`. Nothing in a port can reach OCCT, Coin or the bgfx
renderer. The worst failure mode is a workbench that does not load.

### 3. The import surface is nearly clean already

Upstream's BIM was staged on `sys.path` and all 177 non-test modules
imported under this fork's own build:

| run | modules importing clean |
| --- | --- |
| console (`FreeCADCmd`) | 30 / 177 |
| GUI (xvfb) | 44 / 177 |
| GUI + `Presets` staged + `freecad.deprecation` supplied | **109 / 177** |

Two of the three big blockers were staging, not code: BIM reads its
presets from `<resource>/Mod/BIM/Presets`, and `freecad.deprecation` is
one 87-line file upstream has in `src/Ext/freecad/` and we do not.

What is left, all identified:

- **30 modules**: `Line.__init__() got an unexpected keyword argument
  'mode'`. The one real API drift found so far -- upstream
  `draftguitools.gui_lines.Line(mode="line")` against our
  `Line(wiremode=False)`.
- **23**: `generated_sql_parser`, which upstream generates at build time.
  A build rule to port.
- **2**: `create_pip_call` missing from our AddonManager's
  `addonmanager_utilities`.
- 8 were ifcopenshell missing from that particular run's environment
  (fine in the console run), and 5 are harness artifacts (`Init`,
  `InitGui`, `utils.*`).

### 4. The Draft dependency is narrow at the module level and drifted inside

BIM imports nine `draft*` modules -- `draftutils.{params,messages,todo,
translate,groups}`, `draftguitools`, `draftguitools.gui_stretch`,
`draftviewproviders`, `draftutils` -- and **all nine exist here**, as do
`Draft`, `DraftGeomUtils`, `DraftVecUtils` and `WorkingPlane`. But our
Draft differs from upstream's by 244 Python files, +25658/-16567, and the
`Line` signature above is what that drift looks like in practice. Module
presence is not API compatibility.

### 5. Upstream's legacy IFC importer is behind us

`src/Mod/BIM/importers/importIFC.py` still calls
`settings.set(settings.USE_BREP_DATA, True)` and friends -- the
pre-0.8 ifcopenshell API. The four fixes committed here on 2026-08-19
(`f0e8efb6ac` MeshPart, `04810d888e` the 0.8 settings API, `0a6a5a7468`
body representation + the `only` list mutation, and the `by_type` tuple
fix) are **not** in upstream, and their importer has the same bugs.

Do not port `importers/importIFC.py` or `importers/importIFCHelper.py`:
it would regress what works today. Send ours upstream instead.

### 6. NativeIFC already speaks the ifcopenshell we have

`nativeifc/ifc_generator.py` branches on
`hasattr(settings, "ITERATOR_OUTPUT")`. Against the 0.9.0alpha wrapper
built here that is True, and both `"ITERATOR_OUTPUT"` and
`"CONTEXT_IDENTIFIERS"` are accepted by it. Upstream targets 0.8; 0.8.5
is also packaged here if a pinned version turns out to matter.

### 7. Upstream fixed the thing that cost us nine hours

`computeAreas` is now an `AreaCalculator` class with `Part.OCCError`
handling, the `MaxComputeAreas` guard and an explicit workaround for
TechDraw's "crazy edges" (longer than 9999.9 mm). Ours is the older
inline version, and its failure -- `Part.Wire(...)` "returned a result
with an exception set" -- is exactly what that workaround is for.

## The plan

Four stages. Each is independently useful, each ends in a measurement,
and the fork keeps working after every one of them.

### Stage 1 -- backport `AreaCalculator` (hours)

Port upstream's `AreaCalculator` into our `ArchComponent.py`, with the
OCCError handling and the crazy-edge workaround. Nothing else moves.

Gate: re-run the parametric import on 300 and 2000 products of the King
model. Expect the 4168 exceptions to go to about zero and the recompute
share of the run to collapse. This alone may make `MERGE_MODE_ARCH=0`
usable, and it is the first concrete item of the "improve IFC import
speed" work.

### Stage 2 -- NativeIFC as an additional importer (days)

Bring in `nativeifc/`, `ifc_objects.py`, `ifc_viewproviders.py`, the few
`bimcommands` they need, `freecad/deprecation.py`, the `Presets`
staging, and a `create_pip_call` shim -- as a **new** module that does
not replace Arch. Arch stays exactly as it is; the two coexist.

Gate: open the 155 MB King IFC through NativeIFC and measure
time-to-usable against today's baseline of 599 s to import plus 138 s to
save. NativeIFC defers geometry, so the interesting number is how long
before the tree is browsable.

### Stage 3 -- a Draft compatibility shim, then the rest of BIM (weeks)

Start with `gui_lines.Line(mode=)`. Every further drift found at runtime
goes in the same shim, so the cost of tracking upstream stays visible in
one file rather than smeared across 224 modules. Then bring BIM's
`Arch*.py` in as a parallel module, still not replacing Arch.

The alternative -- porting upstream Draft wholesale -- means giving up
the fork's Draft work (Link support among it) and is not on this plan.

### Stage 4 -- decide whether Arch goes away

Only worth answering after stages 1-3 show how deep the drift really is.

## Fork deltas any transplant must re-apply

- **App::Link / ArchLink integration.** The fork's reason for existing;
  upstream BIM knows nothing about it.
- **The BuildingPart child-visibility rule** (`4de4b667bc`) and the
  group/visibility semantics that go with it.
- **The appearance change** (`9ebb513d35`, "give a view provider an
  appearance, and retire ShapeMaterial"). Upstream BIM touches
  `Transparency` 228 times, `DiffuseColor` 184, `ShapeColor` 86 and
  `ShapeAppearance` 40, so this is the largest single collision surface
  in the view providers.
- **The PySide shim** (`220f297e94`): the fork imports Qt through it,
  upstream imports PySide directly.
- **The four IFC importer fixes** listed in point 5.

## What not to port

- `importers/importIFC.py` and `importers/importIFCHelper.py` -- behind
  our own tree (point 5).
- Upstream Draft (point 4, and the fork's Draft carries Link work).
- Anything requiring a core API this fork does not have; the smoke test
  above is how that gets discovered, module by module, rather than
  assumed.

## Reproducing the smoke test

    git archive upstream/main src/Mod/BIM | tar x -C <scratch>
    # sys.path.insert(0, <scratch>/src/Mod/BIM) inside the script, so
    # upstream's Arch*.py win over ours, then import every module and
    # bucket the exceptions by type and message.
    env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb xvfb-run -a \
        .conda/run.sh .conda/limited.sh \
        ./build/conda-relwithdebinfo-801/bin/FreeCAD <script>

Presets are found by symlinking the staged tree to
`build/<dir>/share/Mod/BIM`; `freecad.deprecation` by appending a
directory to `freecad.__path__`.
