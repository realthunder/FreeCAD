# Test suites and their status

Status as of **2026-09-02**, measured on `build/conda-relwithdebinfo-801`
(OCCT 8.0.1). Both suites are green.

*** **The suites run on the RelWithDebInfo tree.** `conda-relwithdebinfo-801`
-> `build/conda-relwithdebinfo-801` is the standard build and the one every
test run uses. `conda-debug-local` -> `build/conda-debug-occt801` is for
debugger sessions only. An earlier revision of this page named the debug tree
as "the primary tree"; that was wrong.

| Suite | Result |
|---|---|
| Python (`FreeCADCmd -t 0`) | **2630 tests, OK** -- 0 failures, 0 errors, 50 skipped, 6 expected failures (2026-09-09) |
| C++ (`ctest`, `ENABLE_DEVELOPER_TESTS=ON`) | **522 of 522 passing**, 0 failures, 7 ctest entries disabled |

Two traps when running the suites (2026-09-09): give the Python suite and
`Tests_run` **separate `FREECAD_USER_HOME`s** if they run at the same time
-- the expression routing suites in `Tests_run` flip
`Expression/Sandbox:Evaluate` in the shared `user.cfg` while they run, and
the Python suite then restores every Proxy through the sandbox guest (46
failures that vanish alone); and the home directory must **exist** before
the run, or FreeCAD falls back to the real one.  `Tests_run` gained
`ProxyImport.*` (7) and four `TypeImport.*` cases on 2026-09-09 (the
Proxy import rule, docs/Sandbox.md sec 11 item 1); the per-binary counts
below predate that.

**Read the python total as a checksum on the build, not just on the code.**
A short count means a module is missing rather than a test failing, and the
run still says OK, so nothing draws attention to it. Two seen so far:
**2538** is a tree with `BUILD_FEM=OFF` (all of `TestFemApp`, 90 tests), and
**1197** is a tree with a stale pre-rename `Mod/Path` shadowing `Mod/CAM`
(`TestCAMApp`, 1343). Both were build trees whose caches predated the
setting that was supposed to fix them -- see the preset-vs-cache trap in
`CLAUDE.md` and the stale-module sweep in `docs/DevEnvironment.md`.

The C++ side had never been fully green before this date: four of its
targets did not link at all. What was actually wrong with each is recorded
in section 6.

## 1. Running them

### Python

    cd build/conda-relwithdebinfo-801
    mkdir -p /tmp/fchome
    QT_QPA_PLATFORM=offscreen PYTHONPATH=$HOME/works/sw/pyifc \
      FREECAD_USER_HOME=/tmp/fchome \
      script -qec "~/works/sw/fcad/.conda/run.sh ./bin/FreeCADCmd -t 0" /dev/null > pytest.log

**Every part of that line is load-bearing** (verified 2026-08-30 by leaving
each out):

- `script -qec ... /dev/null`: `FreeCADCmd -t 0` needs a pty; without one a
  CAM sanity test dies on `[Errno 9] Bad file descriptor` and takes the rest
  of the run down with it.
- `PYTHONPATH=$HOME/works/sw/pyifc`: supplies ifcopenshell (the fork's own
  0.9 source build, staged there and RPATH-linked into
  `~/works/sw/ifcopenshell-build-801`). `TestArch` imports it at module
  level, so without it the whole ~280-test module collapses into one loader
  error and the run shrinks to ~2349 tests. Do NOT fix this by
  `conda install ifcopenshell` into `.conda/freecad`: the packaged build
  depends on a conda `occt`, which must never enter the dev prefix (see the
  smesh discussion in `docs/DevEnvironment.md`).
- `FREECAD_USER_HOME=/tmp/fchome` (any scratch dir): keeps user-installed
  addons out of the headless run. `~/.FreeCAD/Mod/FreeCAD_SketchArch` calls
  `FreeCADGui.addCommand` at import time, which errors ~117 Arch tests
  under `FreeCADCmd`.
- The env also needs `pyyaml` and `ply` (CAM and FEM import them at module
  level); they are pip-installed into `.conda/freecad` (2026-08-30) --
  pip, not conda, so nothing else in the env is re-solved.

A single module instead of everything: `FreeCADCmd -t TestPartApp`, or from
the Python console `import Test; Test.runTestApp()`.

### C++

The suites are built only when `ENABLE_DEVELOPER_TESTS` is on:

    cmake -S . -B build/conda-relwithdebinfo-801 -DENABLE_DEVELOPER_TESTS=ON
    ~/works/sw/fcad/.conda/run.sh cmake --build build/conda-relwithdebinfo-801
    cd build/conda-relwithdebinfo-801 && ~/works/sw/fcad/.conda/run.sh ctest

**The flag is safe to leave on.** It used to break a plain `ninja` and there
was a standing rule to keep it off; that rule is retired, every target
builds. It costs build time and nothing else.

`ctest -j` is safe too, and is exercised -- see section 5.

**Set `QT_QPA_PLATFORM=offscreen` for a headless run.** One suite,
`QuantitySpinBox_Tests_run`, is a QtTest that constructs widgets, so with no
`DISPLAY` it aborts on "could not connect to display" and ctest reports
452/453. `offscreen` is enough -- it needs a platform plugin, not a GPU:

    QT_QPA_PLATFORM=offscreen ~/works/sw/fcad/.conda/run.sh ctest -j6

One binary directly, which is the fastest loop while working on a suite:

    ./tests/src/Mod/Part/TopoShapeEx_tests_run --gtest_filter='*makEBoolean*'

## 2. Why ctest says 522 and the binaries add up to 1389

Both numbers are right; they count different things.

`gtest_discover_tests` is applied to the six executables in the
`TestExecutables` list, so each of *their* cases becomes its own ctest entry
and its own process. Every other suite is registered with a plain
`add_test(NAME X COMMAND X)`, so the whole binary is one entry.

    496 expanded cases (Tests_run 393, Material 39, Part 38, Sketcher 18,
                        Mesh 7, Points 1)
    +  7 disabled entries (Tests_run's six ExpressionImageBenchTest benches,
                           Part_tests_run's DISABLED_testHistory)
    + 26 whole-binary entries
    = 529 registered, 522 run

Counting individual test cases instead, across all 32 binaries, gives
**1389 passing** (1336 gtest cases plus the 53 QtTest cases of
`InventorBuilder_Tests_run` and `QuantitySpinBox_Tests_run`).

## 3. The C++ suites

| Binary | Cases | Notes |
|---|---|---|
| `Tests_run` | 393 | The legacy suite: Base and App (incl. the ExpressionSecurity contract cases and the ExpressionImageHost suite; +6 disabled benches, section 4) |
| `src/App/Toponaming_tests_run` | 256 | Element map, MappedName, IndexedName |
| `src/App/PropertyMaterialList_tests_run` | 103 | |
| `src/Mod/Part/TopoShapeEx_tests_run` | 86 | +3 disabled, section 4 |
| `src/Gui/SceneLadder_tests_run` | 64 | |
| `src/Base/InventorBuilder_Tests_run` | 48 | QtTest |
| `src/Gui/MaskedOcclusion_tests_run` | 41 | |
| `src/Gui/SceneDump_tests_run` | 39 | |
| `Part_tests_run` | 38 | +1 disabled, section 4 |
| `Material_tests_run` | 39 | |
| `src/Gui/MeshSimplify_tests_run` | 33 | |
| `src/Gui/ProxyHierarchy_tests_run` | 26 | |
| `src/Base/COWData_tests_run` | 22 | |
| `src/Gui/OcclusionCull_tests_run` | 22 | |
| `src/Gui/OccluderMesh_tests_run` | 19 | |
| `Sketcher_tests_run` | 18 | |
| `src/App/DeferredLoad_tests_run` | 15 | |
| `src/Gui/DrainCursor_tests_run` | 14 | |
| `src/Base/Sequencer_tests_run` | 13 | |
| `src/Gui/RenderCacheMaterial_tests_run` | 13 | |
| `src/Mod/Part/ShapeRefSet_tests_run` | 13 | |
| `src/Gui/CullBenefit_tests_run` | 11 | |
| `src/Gui/ProxyStore_tests_run` | 11 | |
| `src/Base/ProgramVersion_tests_run` | 10 | |
| `Mesh_tests_run` | 7 | |
| `src/App/RestoreDrain_tests_run` | 7 | |
| `src/Base/Stream_tests_run` | 6 | |
| `src/Gui/RenderProperties_tests_run` | 6 | A card's Render_* properties, stated on a document object: a view provider needs the whole GUI |
| `src/Base/PyObjectTracking_tests_run` | 5 | |
| `src/Gui/PublishOnly_tests_run` | 5 | |
| `src/Gui/QuantitySpinBox_Tests_run` | 5 | QtTest, +3 skipped, section 4 |
| `Points_tests_run` | 1 | |

## 4. What is deliberately not run, and why

Seven cases. **None of them is a known defect**; each is a place where this
fork decided something different from upstream, or a case whose subject the
fork has retired.

### Ten disabled

- `Tests_run` carries six `ExpressionImageBenchTest.DISABLED_Bench*` cases
  (`tests/src/App/ExpressionImageHost.cpp`). They are timing benches for the
  expression sandbox -- native engine, image round trip, bridge hop,
  instantiation, breakdown, transport floor -- not correctness tests; they
  print microseconds and assert nothing a pass/fail run cares about. Run
  them on purpose with `--gtest_also_run_disabled_tests
  --gtest_filter='ExpressionImageBenchTest.*'`; the numbers they produced
  are recorded in `docs/Sandbox.md` sec 8.1.
- `TopoShapeEx_tests_run` carries three, each ruled an accepted difference
  by phase 4 of the topological-naming harvest rather than a bug. The reason
  is written above each case; in short:
  - `DISABLED_makERevolve` -- our `makERevolve` ends with `fix()`, added
    because `BRepPrimAPI_MakeRevol` can produce a surface with reversed
    parameters (realthunder/FreeCAD#559). Upstream has no such call, and
    that `fix()` is what stamps the extra `;:M;MAK` step on one face name.
    Adapt the expectation if the case is ever wanted; do not remove the fix.
  - `DISABLED_makEBSplineFace` -- the two implementations genuinely differ.
    Ours matches edges geometrically and carries names through `makESHAPE`
    with a tagged face; upstream maps by index and ends in
    `setElementComboName` with an untagged one, which is why its expectation
    has no tag postfixes despite tagged inputs. Upstream's own source marks
    that path "TODO: Is this correct?".
  - `DISABLED_makEOffset2D` -- the input wire is a rectangle, so it has four
    edges. Our map names all four and their four vertices; upstream's
    expectation names two of each and carries a doubled `;OFF` step. Ours is
    the more complete map.

  Background: `docs/UpstreamNameMap.md`.
- `Part_tests_run`'s `FeaturePartCommonTest.DISABLED_testHistory` asks for a
  `ShapeHistory` this fork never writes. `PropertyShapeHistory` is retiring
  in favour of topological naming: every site that builds one sits under
  `FC_NO_ELEMENT_MAP`, a macro nothing in the build defines, so `History` is
  declared and always empty. The Gui side was brought in line too -- the
  colour-inheritance blocks in `ViewProviderBoolean`, `ViewProviderMirror`,
  `ViewProviderCompound` and `ViewProviderRuledSurface` are under the same
  macro. The equivalent assertions now live on the element map, in
  `TopoShapeEx_tests_run`.

### Three skipped

`QuantitySpinBox_Tests_run`'s `test_SimpleBaseUnit`, `test_UnitInNumerator`
and `test_UnitInDenominator` assert upstream's contract, not a gap here.

Upstream rewrote `QuantitySpinBoxPrivate::validateAndInterpret` into a regex
pre-processing pass that reverses the input string, matches units against a
second unit table spelled backwards, textually rewrites forms like `1/10mm`,
and then accepts whatever parses within range -- **dropping the dimension
check**, so an upstream Length field will take `5 kg`.

This fork parses instead of rewriting: `Base::Quantity::parse` first, then
`App::ExpressionParser::parse` plus `isSimpleExpression`, and it checks the
parsed dimension against the field's own unit. The box in those three cases
is default constructed, so it has no unit and nothing dimensional is
acceptable to it. Set a unit and this fork takes the same arithmetic --
which is what `test_ArithmeticInTheFieldsOwnUnit` and
`test_WrongDimensionIsRejected` were added to state. **Do not port
upstream's rewrite.**

### Python skips

49 skipped and 6 expected failures, none of them ours to fix:

| Reason | Count |
|---|---|
| `OpenCamLib not available` | 33 |
| `GUI tests require FreeCAD GUI mode` | 3 |
| assorted upstream `FIXME` / "not yet implemented" / "unstable on some platforms" | the rest |

The expected failures are all in `CAMTests.TestOpenSBPPost` and the
post-processor sanity checks.

## 5. Two traps worth knowing before adding a test

**Give a test that touches the filesystem its own directory.**
`gtest_discover_tests` runs every case of the expanded suites as a separate
process, so under `ctest -j` several cases of the same fixture are live at
once. `ReaderTest` used a fixed `unit_test_Reader.xml` under
`temp_directory_path()` and raced itself: one process writing or removing
the file while another read it. It passed serially every time, which is what
made it look like noise. Use `tests::TempDirectory` (`tests/src/TempDirectory.h`),
which creates a unique directory and removes it on destruction. The full
suite now runs clean over five consecutive `-j8` runs.

**Initialise the Application if you touch a Property.** This fork's
`Property::touch()` asks `GetApplication().isClosingAll()` before doing
anything, and `GetApplication()` hands back a null reference until the
Application exists. `tests/src/App/Property.cpp` did not, and segfaulted.
Call `tests::initApplication()` from a fixture's `SetUpTestSuite`, the way
the other App suites do.

## 6. What the four blocked targets actually were

Recorded because every diagnosis previously written down was wrong, and the
pattern repeats: **a link error under `tests/` usually means the target did
not inherit a directory-scoped setting, not that the code is stale.**

- **`Tests_run`** -- two compile errors, both fork divergence.
  `getUniqueName("Body", {})` is ambiguous because this fork added a second
  overload taking a generator. And `tests/src/App/Expression.cpp` built
  expression trees by hand, which cannot work here: every expression
  constructor is protected behind a static `create()`, and the operator enum
  lives in `Expression.cpp` rather than the header, so
  `OperatorExpression::UNIT` does not exist to name. Both go through
  `Expression::parse` now.
- **`QuantitySpinBox_Tests_run`** -- the units-schema port replaced
  `Base::Quantity`'s QString constructor with a `std::string` one.
- **`Part_tests_run`** -- `TopoShape::getElementTypeAndIndex` was declared in
  `TopoShape.h` and defined nowhere; and `tests/.../TopoShapeCache.cpp` tests
  upstream's `Part::TopoShapeCache`, whose sources are commented out of
  `src/Mod/Part/App/CMakeLists.txt` because this fork keeps its own cache
  file-local inside `TopoShapeEx.cpp`.
- **`Sketcher_tests_run`** -- **not the Sketcher port**, which is what it had
  been deferred for. All 23 OCCT libraries came back `cannot find -lTKFillet`:
  the target links `Sketcher.so`, whose interface names them, but `Sketcher`
  gets its library path from a `link_directories()` in
  `src/Mod/Sketcher/App`, which does not reach a target declared under
  `tests/`. One `target_link_directories` call, the same fix
  `tests/src/Mod/Part/CMakeLists.txt` already carried.

Two live bugs surfaced the moment those suites first ran, both since fixed:
a pointer-to-member laundered through a `void *` in `MaterialListPyImp`
(a two-word member pointer through a one-word slot, so `this` came back
corrupted -- it hit every colour and float setter on a `MaterialList` from
Python), and the missing `initApplication` described in section 5.
