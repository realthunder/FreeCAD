# Development Environment (Ubuntu 24.04 / WSL2)

How this box is set up to build and debug the realthunder FreeCAD fork together with
its companion forks. Two parallel stacks exist; **the conda stack is the primary one**
because it is the only way to get a matched Qt6 + PySide6 on Ubuntu 24.04.

## Repositories

| Repo | Path | Branch | Role |
|---|---|---|---|
| FreeCAD fork | `~/works/sw/fcad` | `LinkMerge` | main project |
| OCCT fork | `~/works/sw/occt` | `dev` | geometry kernel (locally patched) |
| Coin3D fork | `~/works/sw/coin` | `master` | scene graph |
| pivy 0.6.10 | `~/works/sw/pivy` | tag `0.6.10` (detached) | Python bindings for Coin (locally patched) |
| feedstocks | `~/works/sw/*-feedstock` | — | conda distribution recipes (still Qt5/PySide2) |

Local patches in dependency checkouts (needed, do not discard):
- `pivy/interfaces/CMakeLists.txt` — `INSTALL_RPATH` extended with `${CMAKE_INSTALL_RPATH}`
  so `_coin.so` finds our locally-built libCoin without `LD_LIBRARY_PATH`.
- `occt/src/StdPrs/StdPrs_BRepFont.cxx` — `auto` for `FT_Outline::tags` (type changed
  from `char*` to `unsigned char*` in newer freetype).

## Primary stack: conda (Qt 6.10 + PySide6)

### Why

Ubuntu 24.04 ships no PySide6 at all; pip wheels for Python 3.12 start at PySide6 6.6,
which cannot pair with the distro's Qt 6.4.2 (in-process SONAME collision). conda-forge
is the one channel with a matched Qt6/PySide6/shiboken6 for py3.12. All our components
are built inside the env with conda's compilers so there is exactly one ABI in the
process — no mixed-toolchain loader mysteries. Our components are still full `-g`
Debug builds; only the prebuilt deps (Qt, Python, boost, …) are release.

### Layout

- **miniforge**: `~/miniforge3` (deliberately NOT activated in `.bashrc`).
- **env**: `~/works/sw/fcad/.conda/freecad` — the path the repo's `conda-linux*`
  CMake presets hardcode. Gitignored.
- **Solved/pinned versions**: Qt 6.10.1 + PySide6/shiboken6 6.10.1 (pinned in
  `<env>/conda-meta/pinned`), Python 3.12, gcc 15.2, cmake 4.2, boost 1.85, eigen 5.0.1.
- **Wrapper script**: `~/works/sw/fcad/.conda/run.sh` — activates the env and strips
  `-O2`/`-DNDEBUG` from conda's release-tuned `CFLAGS/CXXFLAGS` (they would silently turn
  `CMAKE_BUILD_TYPE=Debug` into optimized-debug) while keeping the rest — the
  `-isystem $PREFIX/include` part is required (e.g. `GL/gl.h`). `LDFLAGS` is kept
  untouched (carries the env rpath wiring). Prefix any build/run/debug command with it:

  ```sh
  ~/works/sw/fcad/.conda/run.sh <command...>
  ```

### Recreating the env

```sh
~/miniforge3/bin/mamba create -y -p ~/works/sw/fcad/.conda/freecad \
  gcc_linux-64 gxx_linux-64 cmake ninja make swig pkg-config \
  qt6-main pyside6 \
  python=3.12 boost-cpp eigen xerces-c zlib yaml-cpp rapidjson freeimage freetype \
  expat libgl-devel libglx-devel libopengl-devel libegl-devel xorg-libxmu xorg-libxi \
  fmt pybind11 numpy matplotlib-base
printf 'qt6-main ==6.10.1\npyside6 ==6.10.1\n' > ~/works/sw/fcad/.conda/freecad/conda-meta/pinned
```

Notes on non-obvious packages: `expat` (not just `libexpat`) so Coin's
`USE_EXTERNAL_EXPAT` finds headers; the `libgl*/libegl*-devel` set provides GL headers
and libs the conda toolchain uses instead of the system's; `fmt`/`pybind11` are wanted
by the repo's `conda` preset (`FREECAD_USE_EXTERNAL_FMT`, `FREECAD_USE_PYBIND11`).

**Env quirk**: conda's pyside6 CMake config mis-anchors its data paths. After any
pyside6 (re)install, recreate:

```sh
cd ~/works/sw/fcad/.conda/freecad
ln -sfn share/PySide6/typesystems typesystems
ln -sfn share/PySide6/glue glue
```

### Building the dependencies (conda stack)

Build dirs / installs are parallel to the system stack and never collide:
`<repo>/build_conda_debug` → `<repo>/install/conda-debug`.

```sh
RUN=~/works/sw/fcad/.conda/run.sh

# OCCT — POLICY shim needed under cmake 4; $ORIGIN rpath is REQUIRED
# (OCCT installs libs with empty RUNPATH otherwise, and RUNPATH is not
# transitive: Part.so finds libTKPart, but libTKPart can't find libTKXDE)
$RUN cmake -S ~/works/sw/occt -B ~/works/sw/occt/build_conda_debug -G Ninja \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DINSTALL_DIR=$HOME/works/sw/occt/install/conda-debug \
  -DCMAKE_INSTALL_RPATH='$ORIGIN' \
  -DBUILD_LIBRARY_TYPE=Shared -DBUILD_MODULE_Draw=OFF \
  -DUSE_TBB=OFF -DUSE_VTK=OFF -DUSE_DRACO=OFF \
  -DUSE_FREETYPE=ON -DUSE_FREEIMAGE=ON -DUSE_RAPIDJSON=ON \
  -DBUILD_RELEASE_DISABLE_EXCEPTIONS=OFF
$RUN cmake --build ~/works/sw/occt/build_conda_debug && $RUN cmake --install ~/works/sw/occt/build_conda_debug

# Coin
$RUN cmake -S ~/works/sw/coin -B ~/works/sw/coin/build_conda_debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX=$HOME/works/sw/coin/install/conda-debug \
  -DUSE_EXTERNAL_EXPAT=ON -DSIMAGE_RUNTIME_LINKING=ON \
  -DCOIN_BUILD_TESTS=OFF -DCOIN_BUILD_DOCUMENTATION=OFF
$RUN cmake --build ~/works/sw/coin/build_conda_debug && $RUN cmake --install ~/works/sw/coin/build_conda_debug

# pivy — installs directly into the env's site-packages (no PYTHONPATH needed)
$RUN cmake -S ~/works/sw/pivy -B ~/works/sw/pivy/build_conda_debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=$HOME/works/sw/coin/install/conda-debug \
  -DCMAKE_INSTALL_RPATH=$HOME/works/sw/coin/install/conda-debug/lib \
  -DPython_EXECUTABLE=$HOME/works/sw/fcad/.conda/freecad/bin/python
$RUN cmake --build ~/works/sw/pivy/build_conda_debug && $RUN cmake --install ~/works/sw/pivy/build_conda_debug
```

### Building FreeCAD (conda stack)

The user preset `conda-debug-local` (in `CMakeUserPresets.json`, gitignored) inherits
the repo's `conda-linux-debug` preset and overrides: build dir `build/conda-debug`,
`CMAKE_PREFIX_PATH`/`OCC_INCLUDE_DIR` pointing at the local `install/conda-debug`
prefixes, `CMAKE_POLICY_VERSION_MINIMUM=3.5` (for bgfx's old cmake_minimum_required
under cmake 4), `BUILD_BGFX=ON`, and `BUILD_FEM/BUILD_WEB/FREECAD_USE_PCL/`
`FREECAD_USE_EXTERNAL_SMESH/ENABLE_DEVELOPER_TESTS` OFF (avoids VTK/netgen/WebEngine/
PCL/smesh packages; enable selectively when needed — conda-forge now has qt6-webengine).

```sh
RUN=~/works/sw/fcad/.conda/run.sh
cd ~/works/sw/fcad
$RUN cmake --preset conda-debug-local
$RUN cmake --build build/conda-debug          # ninja, add -j N to limit parallelism
```

### Running & debugging

```sh
RUN=~/works/sw/fcad/.conda/run.sh
$RUN ~/works/sw/fcad/build/conda-debug/bin/FreeCAD       # GUI (WSLg)
$RUN ~/works/sw/fcad/build/conda-debug/bin/FreeCADCmd    # headless
$RUN gdb --args ~/works/sw/fcad/build/conda-debug/bin/FreeCADCmd script.py
```

- All of fcad/OCCT/Coin have full debug info; gdb breakpoints resolve with source lines
  (e.g. `break App::Document::recompute`). Qt/PySide internals have no symbols (release
  conda packages) — same as with any distro Qt.
- WSLg prints `MESA: error: ZINK: failed to choose pdev` at GUI start — harmless
  fallback noise.
- pivy is importable by the env python directly: `python -c "from pivy import coin"`.

### Quick verification after rebuilds

```sh
# headless kernel sanity (expects volume 500 and "SMOKE OK" pattern)
$RUN build/conda-debug/bin/FreeCADCmd /path/to/smoke.py
# GUI + PySide6: launch and confirm no "No module named 'PySide6'" in output,
# Draft/Arch/Assembly/AddonManager appear in the workbench selector
```

## Fallback stack: system gcc + apt Qt 6.4.2

Kept intact and working, but **PySide6 is impossible here** (see above) — Python
workbenches don't load. Useful as a second opinion against a very different
Qt/compiler generation.

- apt deps: qt6-{base,base-private,svg,tools}-dev, qt6-tools-dev-tools, qt6-l10n-tools,
  libxerces-c-dev, libeigen3-dev, boost dev libs incl. libboost-python-dev,
  libyaml-cpp-dev, libfreeimage-dev, rapidjson-dev, GL/X11 dev, swig, cmake, ninja.
- Builds: `<repo>/build_debug` → `<repo>/install/debug` (OCCT configured with
  `-DCMAKE_INSTALL_RPATH='$ORIGIN'` — same transitivity reason as above).
- fcad preset: `debug-local` (inherits `debug`, Makefiles, `build/debug`,
  `FREECAD_QT_VERSION=6`, BUILD_BGFX=ON, BUILD_FEM/WEB=OFF). `sh src/make.sh -j8` works.
- Runtime needs `PYTHONPATH=$HOME/works/sw/pivy/install/debug/lib/python3.12/site-packages`
  for pivy.

## Porting state / caveats

The first builds required a Qt5→Qt6 port (~30 files: QRegExp, QTextCodec,
QDesktopWidget, QtPlatformHeaders→QNativeInterface in `BGFXRenderer.cpp`,
OpenGLWidgets linkage in `SetupQt.cmake`, …) plus a Qt 6.10 / gcc 15 / boost 1.85 /
eigen 5 / shiboken 6.10 round (`copy_options`, `QIcon` forward decl,
`QGenericReturnArgument` removal, `PythonWrapper.cpp` TypeInitStruct shim,
`SetupEigen.cmake` CONFIG-first, `src/CMakeLists.txt` gates the `boost_fix` overlay to
boost < 1.85). As of writing these are **uncommitted** in the fcad and occt working trees.

Open items:
- Verify Path workbench Area/Voronoi behavior with vanilla boost ≥ 1.85 geometry
  (the `boost_fix` header overlay that used to patch it is disabled there).
- The freecad-rt and pivy feedstocks now default to Qt6 (qt6-main/pyside6, jinja
  `qt` variable, 2026-07), but no Qt6 distribution image has been built yet — the
  qt6 variant needs a new source tag containing the Qt6/toolchain port commits
  (the pinned tag predates them).
