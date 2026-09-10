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
| Python (`FreeCADCmd -t 0`) | **2688 tests, OK** -- 0 failures, 0 errors, 50 skipped, 6 expected failures (2026-09-10, after the RemoteEdit merge) |
| C++ (`ctest`, `ENABLE_DEVELOPER_TESTS=ON`) | **605 of 605 passing** (2026-09-10), 0 failures, 7 ctest entries disabled -- 60 of them need the sandbox guest runtime: in a FRESH `FREECAD_USER_HOME` pass `FCX_PYODIDE=$HOME/.local/share/FreeCAD/Pyodide/314.0.6` or they fail with "expression sandbox image is not available" |
| C++ on Windows (`build/win-relwithdebinfo-801`) | **477 of 477 passing** (2026-09-06), 1 disabled -- see "C++ on Windows" |
| C++ on macOS (`build/mac-relwithdebinfo-801`) | **478 of 478 passing** (2026-09-07), 1 disabled -- see "C++ on macOS" |

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

#### Audit the Python environment first, on a new box

A green `ctest` says nothing about the Python side: `FreeCADCmd` links no
Coin and imports no workbench, and every gtest suite is C++. A box can be
478 of 478 with Draft, Arch and importDXF unable to import at all -- which
is what the macOS box was for three days, missing `typing_extensions` and
`pivy` (`DevEnvironment.md`, "The Python packages the create line does not
install"). Run this before the suite, as `FreeCADCmd audit.py`; it writes
to a file because a script's stdout does not reach the console there:

```python
import FreeCAD
out = open("/tmp/deps.txt", "w")
for m in ("pivy", "typing_extensions", "ply", "yaml", "requests",
          "defusedxml", "git", "shapefile", "pysolar", "ladybug",
          "opencamlib", "debugpy", "six", "lark", "numpy", "matplotlib",
          "Draft", "Arch", "BIM", "TechDraw", "Material", "importDXF"):
    try:
        __import__(m)
        out.write("ok      %s\n" % m)
    except Exception as e:
        out.write("MISSING %-20s (%s)\n" % (m, e))
out.close()
```

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

### C++ on macOS

Green: **478 of 478** on 2026-09-07, 51.6 s with `-j 4`, one entry
disabled -- the same `FeaturePartCommonTest.testHistory` as everywhere
else -- on macOS 12.7.6 Intel with conda clang 23.1.0. That is measured
on the tree merged with the Windows box's work, so the three platforms
are green on the same source.

It took eleven fixes to build at all and four more to pass;
`SceneServerPort.md` 7.7 has the whole bring-up. In short: two
tests leaning on a fast box and a real `/tmp`, and the two vg smokes,
which aborted inside `bgfx::init()` because bgfx builds its screenshot
blit pipeline against a swap chain that a headless Metal context does not
have. That last one is an engine bug, fixed in the fork, and both smokes
now pass on Metal.

The two SceneServerPort stage 5 targets, which were all that had been
built before the tree was:

- `SceneServerWire_tests_run` -- **17 of 17 passing**, `listensOnIPv6Too`
  running rather than skipping.
- `PublishOnly_tests_run` -- **5 of 5 passing** since the dyld port
  (2026-09-06). It was 4 passing and 1 skipped: `noGraphicsDeviceIsCreated`
  read `/proc/self/maps`, which macOS does not have, and the case guarded
  itself. It now enumerates the loaded images through `<mach-o/dyld.h>`,
  and refuses the macOS renderer plugins rather than the frameworks dyld
  maps at launch anyway -- `SceneServerPort.md` 7.6 has the image list.

The stack, and the four source fixes the two stage 5 targets needed, are in
`DevEnvironment.md`, "macOS stack"; the eleven the rest of the tree needed
are in `SceneServerPort.md` 7.7. The GUI tests do not register there for
the same reason as on Windows: their guard looks for `xvfb-run`. Run by
hand, `GuiServeSelectionEcho_tests_run` passes -- eight PASS lines and
`DONE`, 2026-09-07; see "The GUI tests" below for the command.

### C++ on Windows

Green there too as of **2026-09-04**: **472 of 472 passing** on
`build/win-relwithdebinfo-801`, 473 registered, the same one entry disabled.
22s with `-j 6`, 89s serial.

That is 473 against the 453 above, and the difference is the date rather than
the platform: 444 expanded cases and 29 whole-binary entries here, against 427
and 26 on 2026-08-28, from suites added since (`MaterialXGen_tests_run` alone
is 48 cases). Section 2 explains why the two kinds of entry count differently.

**2026-09-06: 477 of 477**, 478 registered, 27s with `-j 6`. The scene
server's own new suite is among them and passes -- `SceneServerWire_tests_run`,
17 cases over real sockets, the Windows half of stage 5 of
`SceneServerPort.md` (section 7.5 there). Two vg smokes arrived with the same
pull and both failed on this platform the first time they ran, for reasons
that were about bgfx on Windows rather than about the code under test. Both
are fixed, and both are worth reading before trusting a Windows render
result: below.

    D:\works\sw\tools\ctest-fcad.cmd -j 6

which is only `cd` plus `run.cmd`; a plain `.conda\run.cmd ctest` from the
build directory is the same thing. `QT_QPA_PLATFORM` is a Linux concern --
there is a desktop here and the QtTest suite uses it.

**`ENABLE_DEVELOPER_TESTS` is OFF in the Windows preset and has to be forced
into a tree that already exists**, because `cacheVariables` only seed a fresh
configure (the trap `CLAUDE.md` describes). `CMakeUserPresets.json` is a local
file, so flipping it there is per-box:

    .conda\run.cmd cmake --preset win-relwithdebinfo-local -DENABLE_DEVELOPER_TESTS=ON

Five things were in the way, none of them a stale test, and all five are
fixed. Four were MSVC being stricter or Windows exporting less:

- `Toponaming_tests_run` -- `return {}` for a `Base::BoundBox3d`. Its
  six-bound constructor is `explicit` (with defaults for every argument), and
  MSVC will not use an explicit constructor for an empty braced return.
- `TopoShapeEx_tests_run` -- `std::numbers::pi` with no `<numbers>`, which
  libstdc++ hands over through another header.
- `Material_tests_run` -- `MaterialLoader::getMaterialFromFile` unresolved:
  `MaterialLoader` is not an exported class, so nothing carried that member
  out of the DLL. It has `MaterialsExport` of its own now.
- `RenderCacheMapBench_tests_run` -- `SoFCRenderCache::_Material::init`
  unresolved. `SoFCRenderCache` is `GuiExport`, but **a nested class is not
  exported with its enclosing one**, so the member needs its own.

The fifth was the suites finding nothing to run:

- `MaterialXGen_tests_run` failed all 48 cases with "the MaterialX data
  library is missing from this install". The build handed it
  `${CMAKE_BINARY_DIR}/share/Renderer/`, and `CMAKE_INSTALL_DATADIR` -- what
  actually stages that library -- is **`data`** on Windows. The test's guard
  does not catch this: `dataLibraryPath()` returns a path whether or not
  anything is there, so a wrong root fails loudly rather than skipping, which
  is the right way round.

**And `ctest` itself could not run a single case.** Windows has no rpath, so a
test exe finds `FreeCADApp.dll` in the build's `bin`, and `Part.pyd` under
`Mod\Part`, only through `PATH`; without them it dies at load with
`0xc0000135`, which ctest reports as an abnormal exit with no output -- it
reads as the test crashing rather than as the loader never getting there. And
`PRE_TEST` discovery hits it first, so one failed discovery aborted the run
before anything else was tried.

`tests/CMakeLists.txt` now carries the path per target, from
`TARGET_RUNTIME_DLL_DIRS` -- every directory that target's own dependencies
live in, so there is no list to maintain. It goes in two ways, and they are
not interchangeable:

- The `add_test()` suites take it as the test property
  `ENVIRONMENT_MODIFICATION`, one `PATH=path_list_prepend:` entry per
  directory, separated with `$<SEMICOLON>`. Not an escaped `\;`: the property
  is written into `CTestTestfile.cmake` as a bracket argument, where a
  backslash is literal, and ctest then reads the whole thing as **one**
  modification -- prepending `PATH=path_list_prepend:` to `PATH` as if it
  were a directory.
- The six `gtest_discover_tests()` suites need the path one step earlier, for
  the discovery run, and only `TEST_LAUNCHER` (CMake 3.29) prefixes that
  command. It gets one `--modify` per directory, because GoogleTest carries
  the launcher as a CMake list through a `-D` round trip that splits on `;`.
  Set it **before** `gtest_discover_tests()`: the module reads the property
  when it is called, not when the build is generated.

Do not merge the two. A launcher on an `add_test()` target arrives as one
argument, semicolons and all, which prepends `--modify` and the rest to `PATH`
as though they were directories and leaves the real ones out -- and the tests
that happen to need only their first directory still pass, so it looks like it
works.

## 2. Why ctest says 453 and the binaries add up to 1305
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

### The render tests (`tests/render/`)

Not gtest binaries and not in the table above: these are driven scripts,
and they are the only tests in the tree that put pixels on a surface.
Everything else called "render" here tests the cache, the view properties
or the generated shader source with no GL context at all -- and
`PublishOnly_tests_run` asserts outright that no driver is mapped.

Four run in a default `ctest`:

| Test | What | Cost |
|---|---|---|
| `RenderSmokeVg_tests_run` | `fcvgsmoke`, bgfx headless offscreen, ink checked per primitive | 0.3 s |
| `RenderSmokePage2D_tests_run` | the retained `Page2D` scenario in the same binary | 0.3 s |
| `RenderGoldenRaster_tests_run` | a staged scene under xvfb vs blessed reference images, per pipeline stage | 28 s (macOS 20 s) |
| `RenderGoldenRasterFlat_tests_run` | the same scene and stages with the environment lighting the model but not drawn | 28 s (macOS 19 s) |

A golden belongs to one backend, so the macOS leg runs against the
`-metal` sets (`RenderDebug.md` 5.2b) and registers nothing where one
has not been blessed.

The path-traced and real-document ones are opt-in, because they are
slower and because a label alone cannot hold them back:

    cmake -DFC_RENDER_HEAVY_TESTS=ON <build> && ctest -L render-heavy

The chess pair no longer needs Cycles: it registers with the traced leg
where `BUILD_CYCLES` is on and without it where the golden set holds no
traced frame, which is what makes the raster chess leg -- the only test
that exercises MaterialX, map binding and the texture path -- gate on a
box that cannot path trace. Verified on macOS 12 / Metal 2026-09-07:
`RenderGoldenChess_tests_run` 37.5 s, `RenderGoldenChessFlat_tests_run`
35.4 s, both against the freshly blessed `chess-metal` sets.

The reference images live in a separate repository
(`realthunder/fcad-render-refs`), the submodule at `tests/render/refs`
(`git submodule update --init tests/render/refs`); when it is not
checked out the golden tests are **skipped, not failed**. Full design, the reblessing procedure and the
traps: `docs/RenderDebug.md` section 5.2 -- 5.2a for the defect the
chess set found on its first day (a capture taken while a material was
still compiling), which is why a frame dump now waits for a complete
frame, and 5.2c for the three portability faults its first Metal run
found, one of which put a scene with no environment in it through the
harness without a word.

#### The vg smokes on Windows

Both failed here when they first ran (2026-09-06), for two unrelated
reasons, neither a defect in what they test. Both are fixed the same
day; what they were is worth keeping, because each is a way for a
Windows build to be quietly wrong rather than loudly broken.

- `RenderSmokeVg_tests_run` lost **one band of five**: `gradient-fill`
  read 0 ink where Linux reads 21600. The cause is the backend bgfx
  picks. On Windows it auto-selects **Direct3D 11**, and vg-renderer's
  embedded shaders have no Direct3D profile at all: they are baked by
  `src/3rdParty/vg-renderer/src/shaders/rebake.sh` on a Linux host into
  glsl, essl, spirv, wgsl and metal, and `src/3rdParty/CMakeLists.txt`
  then forces `BGFX_PLATFORM_SUPPORTS_DXBC=0` and `_DXIL=0` on the
  target with the comment that "the bgfx backend never runs on Direct3D
  anyway". On Windows it does, by default. So
  `bgfx::createEmbeddedShader` finds no entry for `Direct3D11`, all four
  of vg's programs come back invalid -- and **bgfx does not refuse the
  draw**: `submit()` substitutes program handle 0 for an invalid one
  (`bgfx.cpp:1558`), so the frame is drawn by an unrelated program and
  comes out looking almost right. The gradient band is where the
  substitution shows. Run the same binary with `--renderer vk` and it
  passes 5 of 5 on the same box, which was the proof.

  **Fixed 2026-09-06** where it was wrong, in the shader pack: the
  eight `.bin.h` files are rebaked with `dxbc` (s_5_0) and `dxil`
  (s_6_0) added, by the same in-tree `shaderc` -- run on Windows, which
  is where it can produce them -- and the two `SUPPORTS` overrides are
  gone from `src/3rdParty/CMakeLists.txt`. The five existing arrays come
  out byte for byte identical, so the change is purely additive; the
  vg-renderer fork carries it (`134c460`). The Direct3D 11 frame is now
  identical to the Vulkan one, band for band, 51118 ink pixels against
  the 31277 the substituted program was drawing.

  Two things worth taking from it. `submit()`'s substitution means an
  invalid program is not a visible failure but a *different picture*, so
  "it drew something" is not evidence that the shaders loaded. And a
  shader pack is only as portable as the host that baked it: a Linux
  rebake silently drops both Direct3D profiles again.
- `RenderSmokePage2D_tests_run` **crashed**, on Direct3D 11 and on
  Vulkan alike: an access violation in `bx::alloc` inlined into
  `bgfx::makeRef`, inside `FreeCADRenderer.dll`, with the allocator
  pointer null. That is bgfx's global allocator in a copy of bgfx that
  was never initialised. Windows is the platform where
  `src/3rdParty/CMakeLists.txt` builds bgfx **STATIC** (a bgfx DLL
  exports only the C API, so a C++ consumer cannot link one), and
  `fcvgsmoke` linked `FreeCADRenderer` *and* `bgfx` -- so the exe and
  the DLL each held their own copy of bgfx's globals. `bgfx::init` ran
  in the exe's copy; `Page2D` and `Vg2D` live in the DLL and used the
  DLL's, where `g_allocator` was still null. The raw-vg scenario did
  not hit it because vg-renderer is linked into the exe too. On Linux
  bgfx is SHARED and there is one copy, which is why the same source
  never failed there.

  **Fixed 2026-09-06** by making the tool self-contained: `Page2D.cpp`
  and `Vg2D.cpp` are compiled into `fcvgsmoke` and `${Library}` is off
  its link line, which is what the target's own comment always claimed.
  Both files depend on nothing but bgfx, bx, vg and the STL, and
  `FreeCADRenderer_STATIC` is the headers' switch for being compiled
  outside the DLL; the three `RendererFactory` device hooks `Page2D`
  calls live in a Qt translation unit, so the tool defines them as the
  answers an empty `RendererLib` registry gives. The test passes on
  Direct3D 11.

  The general fix -- one copy of bgfx for everybody, as on Linux --
  would be a bgfx DLL, and it needs `WINDOWS_EXPORT_ALL_SYMBOLS` on
  that target because bgfx's own export macro reaches only its C99 API.
  Worth doing when a second Windows executable wants bgfx; today
  exactly one does, and it does not need to share.


### The GUI tests (`tests/gui/`)

Driven scripts as well, but with no pixels under test: each is a Python
file handed to the FreeCAD binary under xvfb by `scripts/gui-test.sh`
(isolated `user.cfg` and XDG directories, `WAYLAND_DISPLAY` unset so the
window cannot land on the desktop, `timeout` around the whole process
group), which judges it by the result file the script writes -- PASS and
FAIL lines ending in `DONE` -- and reports the exit status on its own
line. They are for the class of defect a gtest binary cannot reach: the
event loop, the tree widget, a command's guard scope and a document
being filled, all live at once.

| Test | What | Cost |
|---|---|---|
| `GuiLiveImportNestedLoop_tests_run` | the live-import nested-loop crash (`docs/DocumentLoad.md` sec 15.2): a command pumps a nested event loop while the chess set is still importing, so the tree populates inside the user-edit guard | 25 s |
| `GuiServeSelectionEcho_tests_run` | a remote pick on a headless serve source (`Gui.serveDocument`) comes back as a scene push (`docs/ThinClient.md` sec 8.9 step 0): a raw-socket client in a thread sends `'P'` rays and times the frame back; also that a no-change pick pushes nothing and a `'B'` batch pushes one frame | 10 s |

The table is the two oldest; `tests/gui/CMakeLists.txt` is the list that is
current.

**Two of them are not registered, and are meant not to be**:
`camera-uplink-browser.py` and `serve-edit-browser.py` drive a real Chrome
through the built WASM viewer, so they need three things this repository does
not carry -- `build/wasm`, a `puppeteer-core` install, and a Chrome binary --
and they skip rather than fail when any is missing. Registering them would
put a test in the ctest count that says SKIP on every box but this one, which
is a worse lie than an unregistered test. Run them by hand:

    PUPPETEER_PATH=~/works/sw/fcad-probes/node_modules/puppeteer-core \
    CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \
    scripts/gui-test.sh tests/gui/serve-edit-browser.py /tmp/edit-web \
        --timeout 600

`EDIT_REAL=1` (`CAMUP_REAL=1` for the other) moves it off headless
swiftshader onto the WSLg desktop's real GPU; neither is judged by pixels, so
either tier answers. `serve-edit-browser.py` is worth the trouble: it found
three crashes on its first run that the synthetic socket client next to it
could not reach, because a browser sends a *stream* of pointer moves and the
paths that break are the ones a single event never gets to
(`docs/ThinClient.md` sec 8.9 step 4).

**On Windows they do not register**, and cannot: `tests/gui/CMakeLists.txt`
wants `xvfb-run` and `.conda/run.sh`, and the box has neither. Run one by
hand instead -- nothing in these scripts needs a display of its own, only a
running application:

    powershell -File D:\works\sw\tools\run_cdb.ps1 -UserHome <dir> -StartupScript <wrap.py>

where `wrap.py` is three lines that set `GT_OUT` and `GT_RESULT` in
`os.environ` and `exec` the test file in its own globals. `-UserHome` is the
isolated configuration `gui-test.sh` builds with XDG variables, and the cdb
console is where a crash leaves a stack. The verdict is the result file, as
it is on Linux: PASS lines and `DONE`. `GuiServeSelectionEcho_tests_run` was
run this way for stage 5 of `SceneServerPort.md` (section 7.5 there).

**On macOS they do not register either** -- the same `xvfb-run` guard -- but
there the box has `.conda/run.sh` and a window server, so a hand run is the
driver minus xvfb and `timeout`:

    OUT=/tmp/gt-echo; mkdir -p "$OUT/.iso/cache" "$OUT/.iso/config"
    XDG_CACHE_HOME=$OUT/.iso/cache XDG_CONFIG_HOME=$OUT/.iso/config \
    GT_OUT=$OUT GT_RESULT=$OUT/result.txt \
    .conda/run.sh build/mac-relwithdebinfo-801/bin/FreeCAD \
        --user-cfg "$OUT/.iso/user.cfg" tests/gui/serve-selection-echo.py

`GuiServeSelectionEcho_tests_run` passes that way, 2026-09-07: eight PASS
lines and `DONE`. Its run log used to be unreadable for a reason that had
nothing to do with the test -- see "Toolbar paints threw on macOS 12" below,
now fixed; the log is 16 lines.

That one exists because the render goldens found the crash by accident
under load and then had to stop finding it: a golden must not animate,
and the ten-frame animated fit was the window. The test opens the same
window on purpose -- a command registered by the script pumps a
`QEventLoop` for three seconds, invoked from a timer that waits for the
document to carry `LiveImport` -- and it checks that the window was
actually reached rather than merely that nothing crashed: the tree
created items inside the loop and rewrote `TreeRank` doing so, which is
the write the guard judges. That last one is not free: the tree only
renumbers once it has connected the document's change signal, at the end
of its first tick over that document, so the script seeds the document
with one object and waits for the tree to show it before importing. The
first version did not, populated and connected in the same tick inside
the loop, wrote no rank, and passed with the exemption reverted -- which
is why the rewrite is asserted. The second version wrote the rank and
still passed reverted: the load's own claim on `LiveImport` ends with
its visual drain, and an idle box drains the chess set before the tick,
so the document was no longer live at the write -- the reason the crash
needed three heavy tests in parallel to show. So the script holds the
document live through `Gui.setLiveImport` around the import, the way a
Python progressive importer does, and asserts the document was still
live when the loop ended. Proven by reverting the `TreeRank` exemption
in `App::Document::checkUserEdit`: the run then reports the refusal at
`Face.TreeRank` and dies in `DocumentObjectItem::getParentItem` on the
next tick, the original crash frame for frame. The same run found a
second refusal the fix had not covered, the origin group's resize timer
reaching the guard as `Origin.ViewObject` (`docs/DocumentLoad.md` sec
15.2), which is now the third identity exemption.

Registration needs `FreeCADMain`, `FreeCADGui`, `xvfb-run` and
`.conda/run.sh`; the tree says so at configure time when one is missing.
The chess asset comes from the MaterialX submodule, so without that
checkout the test is not registered.

#### TechDraw's real tests are in the Gui module list

`FreeCADCmd -t TestTechDrawApp` is six cases and none of them projects
geometry.  The five suites that do -- `DrawViewPartTest`,
`DrawViewSectionTest`, `DrawViewDetailTest`, `DrawViewDimensionTest`
and `DrawStoredGeometryTest` -- are imported by `TestTechDrawGui`,
because each waits for the HLR threads on a `QEventLoop` driven by a
`QTimer`.  Console mode dispatches neither, so `FreeCADCmd -t TestTechDrawGui` **hangs** instead of
failing: no output, and it has to be killed.

They need a running application, not a display of their own, so on
Windows they run the same way as the two registered GUI tests above: a
startup script under `run_cdb.ps1 -UserHome <dir> -StartupScript <file>`
that loads both module lists into one `unittest` suite from a
`QTimer.singleShot(0, ...)`, writes PASS/FAIL and `DONE` to a result
file, and quits.  14 tests, 30 s (11 until
`docs/TechDrawStoredGeometry.md` added three).  That is the gate every
TechDraw change in `docs/TopoNamingEnhance.md` section 8 is measured
against.

### Toolbar paints threw on macOS 12

**Found 2026-09-07 by the echo test's log, fixed the same day** --
`src/Gui/MacSymbolIconCompat.mm`, which carries the full explanation.
Kept here because the symptom is what a test run shows you.

Every GUI run on the macOS box used to log a burst of this over the
first second, around thirty times:

    +[NSImageSymbolConfiguration configurationPreferringMonochrome]:
        unrecognized selector sent to class 0x...
    ===== CAUGHT ... unknown exception =====
      event type 12, receiver QToolBarExtension 'qt_toolbar_ext_button'
    QBackingStore::endPaint() called with active painter
    QPaintDevice: Cannot destroy paint device that is being painted

and write a `crash-*.log` under `~/Library/Application Support/FreeCAD/`
holding nothing but those catches. It was not the test's, and not this
tree's: a bare GUI start with a script that only closes the main window
produced the same 28 of them. Event type 12 is `QEvent::Paint`, so what
threw was the *paint* of a toolbar's overflow button and of a tool
button's menu arrow -- Qt 6.7 and later resolve `QStyle::standardIcon()`
on a Mac to an SF Symbol (`SP_ToolBarHorizontalExtensionButton` is
`chevron.forward.2`) and render it at paint through `QAppleIconEngine`,
whose `configuredImage()` calls a macOS 13 class method with no
availability guard. The Objective-C exception unwound out of
`QWidget::event`, `GUIApplication::notify` caught it as an unknown
exception, and the painter it left open is what the two Qt warnings were
about. The `>>` overflow chevrons never drew.

The fix supplies the missing class method, at image load and only when
the running system lacks it, as an empty symbol configuration -- which is
a verified no-op on the merge and says what macOS 12 does anyway. After
it: no exceptions, no crash log, the chevrons draw, and the echo test's
run log is 16 lines instead of 263.

Whether Qt itself has been fixed is a separate question, and this asks
it -- bare PySide6 loads no FreeCAD library, so our fix is not in it:

    .conda/run.sh python -c 'import sys; from PySide6 import QtWidgets; \
        a=QtWidgets.QApplication(sys.argv); \
        a.style().standardIcon(QtWidgets.QStyle.StandardPixmap. \
        SP_ToolBarHorizontalExtensionButton).pixmap(16,16)'

Today it aborts with the uncaught `NSInvalidArgumentException`. A Qt that
guards the call would return silently instead, and the compat file would
then be dead weight rather than wrong -- it keys off the OS, not off Qt,
so it already adds nothing on macOS 13 and later.

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
