# Porting the upstream FEM workbench: an evaluation

Status: evaluation, measured 2026-08-11, **re-measured 2026-08-17
(section 10)**. No FEM port has been done.
Companion to [CoinRetirement.md](./CoinRetirement.md) section 3.6, whose
workbench audit could not reach FEM.

## 1. The short answer

Three separate questions were tangled together under "port FEM". They have
different answers:

1. **Can a dev build have FEM at all?** It can now. The in-tree SMESH is
   fixed and builds against OCCT 8.0.1 (section 3). This was the actual
   blocker and it is closed.
2. **Is the fork's FEM broken?** No. FEM ships in the released packages and
   always has. It is off only in the local dev presets (section 2).
3. **Should the fork replace its FEM with upstream's?** Not as a FEM
   project. The cost is not in FEM: upstream's FEM is written against core
   refactors the fork predates (Console renamed, `App::Color` moved to
   `Base::Color`, `ShapeAppearance`, the `Gui/Selection/` move, a new
   `Materials` module). Porting FEM alone means either dragging those
   core-wide changes in, or holding a FEM that speaks a dialect the rest of
   the fork does not. Recommendation in section 8.

## 2. Where FEM actually stands

`BUILD_FEM` defaults to `ON` (`InitializeFreeCADBuildOptions.cmake:104`),
and `freecad-rt-feedstock/recipe/build.sh` builds with
`FREECAD_USE_EXTERNAL_SMESH=ON`, `BUILD_FEM_NETGEN=ON`, vtk 9.2.6 and the
conda `smesh` package. **So released packages contain a working FEM.**

What is off is FEM in the local dev presets (`BUILD_FEM=OFF` in every user
preset). That is why the section 3.6 audit has an FEM-shaped hole, and why
the `fem-mesh` and `fem-post` probe cases in `fcad-probes/wb_audit_probe.py`
have never run.

FEM renders through Coin, not VTK: there is no `vtkRenderer` anywhere in
`src/Mod/Fem`; `ViewProviderFemPostObject` runs a `vtkGeometryFilter` and
copies the result into `SoCoordinate3` / `SoIndexedFaceSet` /
`SoIndexedLineSet`. VTK is a data pipeline only, so FEM results reach the
bgfx backend through the render cache like any other geometry.

## 3. What was fixed (in-tree SMESH on OCCT 8.0.1)

Measured before: with `BUILD_FEM=ON` against OCCT 8.0.1, salomesmesh failed
**10 targets on 19 errors**, all of them things OCCT 8.0 deleted
(`Standard_Stream.hxx`, `Bnd_B3d.hxx`, `Bnd_B2d.hxx`, the `TColStd` and
`MeshVS` typedef aliases, `TopTools_MapOfShape`,
`Standard_Failure::DynamicType`, changed `MeshVS` virtual signatures).

Upstream had already done that work properly, version-guarded, in one merge
(`057d51f846`, PR #29904). Our copy had drifted 2.5 years behind and we were
part-way through reinventing it (`8c820dd3c0`). Taking upstream's copy
instead:

| | targets failed | errors |
|---|---|---|
| our salomesmesh, OCCT 8.0.1 | 10 | 19 |
| upstream's salomesmesh | 1 | 0 (link only) |
| plus `TKExpress` linked | 0 | 0 |

The one remaining failure was a link error, not drift: OCCT 8.0 moved the
`Expr` and `ExprIntrp` packages out of `TKMath` into a `TKExpress` toolkit,
and SMESH parses segment-distribution expressions with them.

Commits: `ee74491988` (TKExpress), `a59278f5ce` (SMESH sync), `5dd43b6320`
(drop an upstream debug print). The sync also brings VTK 9.3 polyhedral-cell
support our copy lacked. Two of our own changes were deliberately dropped
(upstream supersedes the boost 1.85 fix with `std::filesystem`, and
supersedes the OCCT 8 header work); one was re-applied on top (NETGEN
dumping its output file into the drive root on Windows, `1368167c03`).

Only the Linux non-NETGEN configuration was compiled. The Windows and NETGEN
paths are guarded but unbuilt here.

**This also removes the reason to build `smesh-feedstock`.** External smesh
was blocked because occt's `run_exports` pin is `x.x.x` and no published
smesh covers 8.0.1; the in-tree route now works instead.

## 4. How far apart the two FEMs are

Merge base `a662fbb2ff` (2023-12-27); upstream tip `512e91aad0` (2026-07-27).

| | files | insertions | deletions |
|---|---|---|---|
| our delta to `src/Mod/Fem` | 43 | 215 | 256 |
| upstream delta (no `.ts`) | 974 | 164465 | 54066 |

Our 43 files are almost all fork-wide sweeps (unicode, OCCT version bumps,
the binding-generator change) plus about seven genuinely FEM-specific fixes.
**The fork has no FEM investment to protect.** A port is a replace, not a
merge.

## 5. The adaptation surface

The decisive measurement is not diff size, it is whether upstream's FEM
compiles against this fork's core. Method: syntax-only compile of upstream's
FEM sources with the real flags the eval tree uses for our own FEM, with
upstream's Fem tree prepended so FEM's own headers resolve to upstream's
copies (`scratchpad/fem_census.py`).

| | TUs | clean | failed |
|---|---|---|---|
| upstream `Fem/App` | 50 | 13 | 37 |
| upstream `Fem/Gui` | 88 | 9 | 79 |

The failures are **not 116 unrelated problems**. They collapse into a short
list of upstream-wide refactors this fork predates:

| what | sites | files | nature |
|---|---|---|---|
| `Console().log/error/message/warning` (we have `Log`/`Error`/...) | 158 | many | mechanical, or 4 alias methods in core |
| `App::Color` moved to `Base::Color` | 56 | 7 | mechanical, but cascades into ~200 template errors |
| `ShapeAppearance` on `ViewProviderPart` | 37 | 13 | real Part/Gui feature port |
| `Base::TimeElapsed` | 32 | 3 | small core addition |
| `Gui/Selection/` include move | 28 | 25 | mechanical (proved: a forwarding shim clears all of them) |
| `originalPoint/Line/FaceColors` | 24 | 2 | `ViewProviderPart` members we lack |
| `ViewProviderPythonFeature` renamed `ViewProviderFeaturePython` | 23 | 16 | mechanical (shim clears it) |
| `fastsignals` | 5 | 4 | vendor `3rdParty/FastSignals`, or alias to boost::signals2 |
| `App/Datums.h` | 2 | 2 | real new core feature |

Plus a handful of fork-versus-upstream conflicts where the **fork** is the
one that diverged, so upstream's FEM must be adapted to us:

- `ComplexGeoData::getElementTypes()` returns `const std::vector<const char*>&`
  here (our `75a7f709d8`) and by value upstream, so `FemMesh`'s override
  conflicts.
- `App::Property::isSame` is pure virtual here, so upstream's
  `PropertyPostDataObject` is abstract and every ported property class must
  implement it.
- `ViewProviderFemPostPipeline::acceptReorderingObjects` overrides nothing here.
- `Gui::Document::signalHighlightObject` moved to `Application` here (`3418f1be72`).
- `App::PropertyStiffnessDensity` and `App::PropertyMoment` do not exist here.

Caveats on these numbers, stated because they cut in the optimistic
direction: the census stops at the first fatal error per TU, so deeper
errors stay hidden and the counts are **lower bounds**. The Gui figure is
additionally depressed by `ui_*.h` and `moc_*.cpp` that a real build would
generate; a shim run cut Gui's missing-header failures from 77 to 46 and
raised its visible API errors from 2 to 33, which is the shape of what
further unmasking would do.

### 5.1 The Materials coupling

Upstream FEM does `import Materials` in 13 files and `MatGui` in 2. This
fork has the December-2023 vintage of that module: python module named
`Material`, not `Materials`, and `.xml` bindings rather than upstream's
`.pyi`. Upstream's has diverged by 346 files / +25515 lines and gained
`ExternalManager`, `Library`, `MaterialFilter` and the rest.

So porting FEM drags in porting a second module of comparable size. This is
the single biggest hidden cost in the "just copy `src/Mod/Fem`" framing.

WARNING WARNING **Measured 2026-08-18, and this paragraph is wrong.**
Materials really has diverged 346 files / +25515 lines, but FEM's
*dependency on that divergence* is eight Python names, and this fork
already has six of them. The whole surface, enumerated from upstream's
sources:

| what upstream FEM calls | this fork |
|---|---|
| `Materials.MaterialManager()`, `.Materials`, `.getMaterial(uuid)` | have (`MaterialManagerPy.xml`) |
| `Material.Properties` | have (`MaterialPy.xml`) |
| `Materials.ModelManager().Models` | have (`ModelManagerPy.xml`) |
| `Materials.UUIDs()`, `.Fluid` | have (`UUIDsPy.xml`) |
| `Materials.MaterialFilter()`, `.RequiredModels` | MISSING |
| `MatGui.MaterialTreeWidget` + 4 attributes | MISSING |

Three further facts shrink it. **The dependency is Python-only** -- no
upstream FEM source includes any Material header and FEM's CMakeLists
links neither library. **The module rename is one line** --
`SET_PYTHON_PREFIX_SUFFIX(Material)` here versus `(Materials)` upstream;
our C++ namespace is already `Materials` and our Gui module is already
`MatGui`. And **the two missing pieces are small** -- `MaterialFilter`
plus `MaterialFilterOptions` is about 490 lines including bindings, and
`MaterialTreeWidget` is one widget. Of the 14 importing files, 11 are
`femexamples/` scripts; only three are load-bearing.

* **Where the real Materials coupling lives is not FEM -- it is Part.**
Counted across all of upstream: Part is the *only* module outside
Material that includes a Material header, and the only one that links
the libraries (`Mod/Part/App` -> `Materials`, `Mod/Part/Gui` ->
`MatGui`). `PartFeature.h:29` includes `PropertyMaterial.h` to declare
`Materials::PropertyMaterial ShapeMaterial` at line 77, so every consumer
of that header inherits the dependency. FEM's 14 files are all Python and
all shallow; Part's three are a compile-time coupling. CAM is the only
other consumer at all (2 files, Python).

! **The six matches above are name-level.** The attributes and methods
exist; return shapes, key types and semantics are unchecked, and that is
where drift would hide.

Python also wants `vtkmodules` (39 uses across 15 files), which needs
`BUILD_FEM_VTK_PYTHON` and the VTK python wrappers.

## 6. What a port would buy

Real and substantial, for FEM users: 121 new python modules and 26 new App
files, including a reworked post-processing pipeline (branch filters,
1D/2D extractors, histogram, line plot, table, glyph filter), new
constraints (rigid body, electromagnetic, electric charge density), netgen
and mesh-refinement objects (distance, tfcurve/tfsurface/tfvolume), a
reworked solver object layer for CalculiX/Elmer/Z88, and 49 new test files.

It buys nothing for the stated project direction (renderer, browser/mobile,
large-model performance, out-of-process OCCT). FEM is not on that path.

## 7. Options

**A. Do nothing.** Releases keep their working FEM. The audit hole stays.
Cost: zero. This is no longer the cheapest useful option, because B is now
nearly free.

**B. Turn FEM on in the dev presets (recommended).** The blocker is gone
(section 3). Cost: a `.conda/fem-deps` prefix (exists, 1.6G, Qt-free) and
build time. Buys: the section 3.6 audit can finally run its `fem-mesh` and
`fem-post` cases, and dev builds match what ships.

**C. Port upstream FEM, adapting it to the fork's core.** Rewrite the ~350
mechanical sites to the fork's dialect, port `Materials`, `App/Datums`, the
small property types, `ShapeAppearance` and the `ViewProviderPart` colour
members. Buys section 6. Costs: the Materials module port is the hard half;
and the result is a permanent divergence that makes the *next* FEM sync
just as expensive, because nothing here moves the fork toward upstream's
core.

**D. Adopt the core refactors fork-wide, then port FEM.** Rename Console,
move `App::Color` to `Base::Color`, move `Gui/Selection/`, vendor
FastSignals. Each is individually mechanical but touches the whole tree, and
`Base::Color` alone cascades. This is the honest version of C: it makes the
fork cheaper to sync with upstream forever, and it is a core project, not a
FEM one.

## 8. Recommendation

Do **B** now, and treat **C/D** as a separate decision that is not really
about FEM.

B closes the audit gap, is already unblocked, and costs only build time. It
also gives the tree in which any later port would be tested.

C is poor value judged as a FEM project: the fork has no FEM users pulling
for it, FEM is off the roadmap, and the expensive part (Materials, the core
refactors) is not FEM work. If upstream FEM's post-processing is wanted for
its own sake, say so explicitly and scope it as D, where the core-refactor
work pays for itself across every future upstream sync, not just FEM.

## 9. Reproducing this

Isolated eval tree, so the primary tree is untouched (it deliberately keeps
`fem-deps` out of `CMAKE_PREFIX_PATH`, passing `VTK_DIR`,
`MEDFILE_ROOT_DIR` and `HDF5_ROOT` explicitly):

    scratchpad/fem-eval-configure.sh          # -> build/fem-eval
    .conda/run.sh ninja -C build/fem-eval SMDS Driver DriverSTL DriverDAT \
        DriverUNV SMESHDS SMESH MEFISTO2 StdMeshers

salomesmesh depends only on OCCT, VTK, boost and MED, so the SMESH question
can be settled without building any of FreeCAD.

The compile census:

    git archive upstream/main src/Mod/Fem | tar -x -C <tmp>
    SHIM=1 .conda/run.sh python3 scratchpad/fem_census.py App   # or Gui

Traps worth keeping:

- The ninja compile command is **ccache-prefixed**, and it carries
  `-MD -MT <obj> -MF <obj>.d`. Strip those as pairs; dropping only the
  token that ends in `.o` unpairs `-MT`, and the leftover `.d` path becomes
  an input file, which gcc reports as "D compiler not installed on this
  system".
- A census that stops at the first missing header measures include paths,
  not API drift. Shim the mechanical renames first, then re-read the
  numbers.

## 10. Re-evaluation, 2026-08-17

Six days after the evaluation the fork adopted four of the core refactors
section 5 named -- the console names (`74b713cae0`), the colour class
moving to `Base` (`935bccd118`), `Base::TimeElapsed` and the two FEM
units (`40c93de2aa`), and the `ShapeAppearance` work that ran through
2026-08-13. Those were taken up partly *because* this evaluation named
them. So the question is whether the port got cheaper.

Same method, same upstream tip (`512e91aad0`), so the delta is the
fork's alone. The original harness did not survive; it was rebuilt from
section 9, which means row counts are comparable **in kind**, not
digit-for-digit.

### 10.1 The pass/fail split did not move at all

| | TUs | clean 08-11 | clean 08-17 |
|---|---|---|---|
| upstream `Fem/App` | 50 | 13 | 13 |
| upstream `Fem/Gui` | 88 | 9 | 8 |

That is the first finding, and it is a warning about the metric rather
than about the work: **one surviving error fails a translation unit**, so
TU pass/fail cannot see progress until the *last* blocker in a file is
gone. What moved is the composition of the errors.

### 10.2 What closed

| row | 08-11 | 08-17 | what is left |
|---|---|---|---|
| `Console().log/error/message/warning` | 158 | **0** | nothing |
| `Base::TimeElapsed` | 32 | **0** | nothing |
| `PropertyMoment` / `PropertyStiffnessDensity` | absent | **present** | nothing |
| `App::Color` -> `Base::Color` | 56 | **6** | `Base::color_traits` only |
| `ShapeAppearance` | 37 | **19** | convenience overloads, e.g. `PropertyMaterialList::setDiffuseColor(float, float, float)` |

The two partial rows are the interesting ones: what remains of a 56-site
and a 37-site row is an adapter template and a handful of overloads. The
`ShapeAppearance` port did the hard half -- the property, its per-face
semantics and its storage -- and upstream FEM now fails against it only
where it wants a spelling we did not add.

### 10.3 What closing them revealed

Section 5 warned its counts were **lower bounds**, because the census
stops at the first fatal error per TU. This is what that looks like when
the top rows come off: three upstream-wide refactors that were behind
them, invisible until now.

| newly visible | errors | files | shape |
|---|---|---|---|
| `XMLReader::getAttribute<T>()` templated | 20 | 3 | ours is `getAttribute` + `getAttributeAsFloat/AsInteger` |
| `addObject<T>()` / `getExtension<T>()` templated | 28 | 9 | ours take a type name and return a base pointer |
| `Base::Quantity` strings as `std::string` | 4 | 1 | ours return `QString` |
| `Gui::PropertyEditor::FrameOption` and the `PropertyItem` API | most of Gui's 215 "other" | 44 | editor drift, not yet unpicked |

So the adaptation surface did not shrink by the ~280 sites the closed
rows represent. It shrank by those, and grew back by what they hid. This
is the honest shape of a port against a diverged core: each layer of
mechanical fixes exposes the next one, and only a real build (not a
syntax census) ever sees the bottom.

### 10.4 What has not moved

- **`fastsignals`**: 51 sites (17 App, 34 Gui) against section 5's 5 --
  the same unmasking. Still one vendored header or one alias to
  `boost::signals2`.
- **The `Gui/Selection/` move and the `ViewProviderFeaturePython`
  rename**: still mechanical, and the census proves it by curing both
  with forwarding headers. (WARNING: the aliases need upstream's exact
  names -- `ViewProviderFeaturePythonT`, namespace `fastsignals` -- and a
  shim that guesses them wrong reports the rename as an API failure.)
- **`App/Datums.h`**: still a real new core feature, still 2 files.
- **The fork-side divergences**: `getElementTypes` by const-ref (18
  files), `Property::isSame` pure virtual (16 errors, and it makes
  upstream's `PropertyPostDataObject` and therefore `FemMesh` abstract),
  `signalHighlightObject`. These were decisions, not chores -- and the
  first two were **decided on 2026-08-17: keep the fork's signature in
  both cases, no core change.** So they are now FEM-side chores after all:
  a port adapts `FemMesh`'s override to return a reference to a static
  table, and implements `isSame` on every ported property class.
- ! **The `Materials` module: unchanged.** Still the December-2023
  vintage, and upstream's moved another 32 files / +1589 lines in the six
  days since. This was named the single biggest hidden cost, and none of
  it has been paid.
  WARNING **Superseded 2026-08-18** -- see the measurement in section 5.1.
  The module's divergence is real, but FEM needs eight names from it and
  the fork has six. "A second module of comparable size" measures the
  module, not the dependency.

### 10.5 The target moved too

Upstream advanced 315 commits in those six days; `src/Mod/Fem` alone
changed 57 files (+33709 / -22842). A port is a snapshot of a moving
tree, and the sync cost recurs -- which is the argument *for* adopting
the core refactors on their own merit, and *against* treating a FEM copy
as a one-off.

### 10.6 Gui's raw count still cannot be read as API drift

205 of Gui's errors, in 14 files, are our own generated `ui_*.h` missing
widget members upstream's `.ui` files declare, plus a handful of
`moc_*.cpp` and generated `*Py.h`. A real build generates those from
upstream's sources. The census now buckets them as artifacts; before
believing any Gui number, check which bucket it is in.

### 10.7 Revised recommendation

**Unchanged in direction, and now with evidence behind it.**

- **B (turn FEM on in the dev presets) was recommended and has not been
  done** -- `BUILD_FEM` is `OFF` in all three user presets. It is still
  cheap and still closes the audit hole. Do it or drop it explicitly.
- **The core-refactor adoption is working and should continue on its own
  merit.** Four rows closed in two days, each independently useful to the
  fork, each making every future upstream sync cheaper. The next ones are
  named in 10.3 and are the same kind of change: `getAttribute<T>`,
  `addObject<T>`/`getExtension<T>`, the `Base` string types, the
  `fastsignals` header, the `Gui/Selection` move.
- **Do not start the FEM port.** The decisive cost has not moved at all:
  `Materials` is untouched, and it is a second module of comparable size.
  FEM remains off the roadmap and the fork still has no FEM investment.
  WARNING **The stated reason is wrong, measured 2026-08-18** (section
  5.1): FEM's dependency on `Materials` is eight Python names, six of
  which the fork already has. The recommendation may still hold -- FEM is
  off the roadmap and upstream's FEM is 1018 files -- but it can no longer
  rest on the Materials coupling, which is the cheap part.
- If it is ever wanted, the sequence the measurements imply is: finish
  the mechanical core rows -> settle the two fork divergences
  (`getElementTypes`, `isSame`) -> port `Materials` -> then, and only
  then, FEM.
