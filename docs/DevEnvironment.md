# Development Environment

How the development boxes are set up to build and debug the realthunder FreeCAD fork
together with its companion forks.

- **Linux (Ubuntu 24.04 / WSL2)** — two parallel stacks; **the conda stack is the
  primary one** because it is the only way to get a matched Qt6 + PySide6 on
  Ubuntu 24.04. This is what the bulk of this document describes.
- **Windows 10 + MSVC 2022** — [Windows stack](#windows-stack-msvc-2022--conda).
  Same conda approach, but the debug/release CRT split forces every component to be
  a non-Debug configuration, and a dozen Windows-only source fixes are required.
- **macOS 12 (Intel) + conda** -- [macOS stack](#macos-stack-intel-macos-12--conda).
  Same conda approach again. Qt stops at 6.11.1 there, `xcrun` leaks
  `/usr/local/include` onto the compiler's search path, and libc++ finds missing
  includes that libstdc++ hid.

## Repositories

**`LinkVibe` is the working branch** across every realthunder fork below, each
tracking `origin/LinkVibe` and pushed. It was branched from the fork's prior tip
(FreeCAD `LinkMerge`, OCCT `dev`, Coin `master`) and now carries this session's
Qt6 / toolchain / mcp_console work.

| Repo | Path | Branch (→ remote) | Role |
|---|---|---|---|
| FreeCAD fork | `~/works/sw/fcad` | `LinkVibe` → `realthunder/FreeCAD` | main project |
| OCCT fork | `~/works/sw/occt` | `LinkVibe-801` → `realthunder/OCCT` | geometry kernel (patched), **the version FreeCAD builds against**; `LinkVibe` is the same fork on 7.7.2 |
| Coin3D fork | `~/works/sw/coin` | `LinkVibe` → `realthunder/coin` | scene graph |
| pivy 0.6.10 | `~/works/sw/pivy` | `rt-0.6.10` (local; origin is upstream `coin3d/pivy`) | Coin Python bindings (patched) |
| freecad-rt-feedstock | `~/works/sw/freecad-rt-feedstock` | `LinkVibe` → `realthunder/...` | FreeCAD conda recipe (Qt6, builds from FreeCAD `LinkVibe`) |
| pivy-feedstock | `~/works/sw/pivy-feedstock` | `LinkVibe` → `realthunder/...` | pivy conda recipe (carries the rpath patch) |

Paths above are the Linux box; the Windows box mirrors the same set of repos and
branches under a different root — see [Layout](#layout-1) in the Windows section.
The macOS box builds the whole fork and is green as of 2026-09-07 (`ctest`
478 of 478); the bring-up, written for a session starting from a blank
machine, is `PlatformVerification.md` section 4, and what the tree itself
needed is `SceneServerPort.md` 7.7.

Fork-local patches, now committed on their `LinkVibe` branches (don't discard):
- `pivy/interfaces/CMakeLists.txt` — `INSTALL_RPATH` extended with `${CMAKE_INSTALL_RPATH}`
  so `_coin.so` finds our locally-built libCoin without `LD_LIBRARY_PATH`
  (pivy commit on `rt-0.6.10`; also shipped as a pivy-feedstock patch).
- `occt/src/StdPrs/StdPrs_BRepFont.cxx` — `auto` for `FT_Outline::tags` (type changed
  from `char*` to `unsigned char*` in newer freetype).
- `occt` `NCollection_IncAllocator.cxx` and `Aspect_VKeySet.cxx` -- `#include <mutex>`
  for `std::lock_guard`. Only libc++ needs it, so only macOS found it; harmless and
  correct everywhere (`LinkVibe-801`).
- `OndselSolver` `PiecewiseFunction.cpp`, `Polynomial.cpp`, `Sum.cpp` -- `#include
  <iterator>` for `std::back_inserter`, the same libc++ gap. The submodule now points
  at `realthunder/OndselSolver` branch `LinkVibe` (the fork was made for this), with
  the fix rebased onto upstream `main` at 458510d. Upstream still has it wrong in
  three of the four files that call `back_inserter`; only `Product.cpp` includes
  `<iterator>`, and by accident rather than by fix.
- `bgfx` `renderer_mtl.cpp` -- do not build the screenshot blit pipeline when there
  is no swap chain (`realthunder/bgfx` `master`). Headless Metal otherwise aborts
  inside `bgfx::init()`, which is any offscreen Metal user's problem and not
  specific to this tree.

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
- **Solved/pinned versions**: Qt 6.11.2 + PySide6/shiboken6 6.11.2 and vtk 9.6.2
  (all pinned in `<env>/conda-meta/pinned`), Python 3.12, gcc 15.2, cmake 4.2,
  boost 1.90, eigen 5.0.1. The Qt/boost/vtk trio deliberately tracks the stack
  conda-forge builds smesh and FreeCAD against -- see the FEM section for why
  they cannot be chosen independently.
- **Wrapper script**: `~/works/sw/fcad/.conda/run.sh` — activates the env and strips
  `-O2`/`-DNDEBUG` from conda's release-tuned `CFLAGS/CXXFLAGS` (they would silently turn
  `CMAKE_BUILD_TYPE=Debug` into optimized-debug) while keeping the rest — the
  `-isystem $PREFIX/include` part is required (e.g. `GL/gl.h`). `LDFLAGS` is kept
  untouched (carries the env rpath wiring). Prefix any build/run/debug command with it:

  ```sh
  ~/works/sw/fcad/.conda/run.sh <command...>
  ```

  **`run.sh` does not put conda's compiler on `PATH` as `g++`.** Plain
  `g++` and `gcc` still resolve to the system Ubuntu toolchain (13.3);
  the tree is built with `$PREFIX/bin/x86_64-conda-linux-gnu-c++` (15.2),
  which is what `CMAKE_CXX_COMPILER` names. That only matters for
  hand-built scratch binaries that LINK the tree's own static libraries:
  compiled with the system `g++` they build and link without a
  complaint, then segfault deep inside library code with the
  instruction pointer somewhere in BSS -- which reads exactly like a
  defect in the library rather than in the harness. Use
  `$(grep CMAKE_CXX_COMPILER: <build>/CMakeCache.txt)` for such a
  binary, or build it through CMake.

### Recreating the env

```sh
~/miniforge3/bin/mamba create -y -p ~/works/sw/fcad/.conda/freecad \
  gcc_linux-64 gxx_linux-64 cmake ninja make swig pkg-config \
  qt6-main=6.11.2 pyside6=6.11.2 \
  python=3.12 libboost-devel eigen xerces-c zlib yaml-cpp rapidjson freeimage freetype \
  expat libgl-devel libglx-devel libopengl-devel libegl-devel xorg-libxmu xorg-libxi \
  fmt pybind11 numpy matplotlib-base
printf 'qt6-main ==6.11.2\npyside6 ==6.11.2\nvtk-base ==9.6.2\nvtk-io-ffmpeg ==9.6.2\n' \
  > ~/works/sw/fcad/.conda/freecad/conda-meta/pinned
```

Two things the create line deliberately leaves out, because neither may be
solved normally in this env: `smesh` (see
[FEM](#fem-and-the-external-smesh-it-links)) and `ifcopenshell` (see
[IfcOpenShell](#ifcopenshell-for-archbim)). Both link OCCT, and both must
arrive without conda-forge's `occt`.

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

### The Python packages the create line does not install

**None of the above is what FreeCAD's own Python needs at run time**, and
that gap is invisible to everything the C++ side measures: `FreeCADCmd`
links no Coin and imports no workbench, the gtest suites are C++, and
`ctest` therefore passes in full on a box where half the workbenches
cannot be imported. It is the Python suite (`FreeCADCmd -t 0`, which
needs a pty) and the GUI that find out.

Measured on the macOS env 2026-09-07 by importing each module under
`FreeCADCmd`; the same audit is worth running on any new box. Import
name on the left, conda-forge package where it differs:

| Module | Package | What needs it |
|---|---|---|
| `pivy` | **built from source** -- see below | 52 files across BIM (17), Draft (10) and CAM (8), and any script that does `from pivy import coin`. Not a conda package for this stack |
| `typing_extensions` | `typing_extensions` | `src/Ext/freecad/deprecation.py`, reached from `DraftVecUtils`, `Arch.py` and `Path/Log.py`: **without it Draft, Arch and importDXF do not import at all** |
| `ply` | `ply` | the OpenSCAD parser, Fem |
| `yaml` | `pyyaml` | CAM (6 files), Material (3), Fem |
| `requests` | `requests` | Addon Manager, BIM |
| `defusedxml` | `defusedxml` | Addon Manager |
| `git` | `gitpython` | Addon Manager (2 files) |
| `shapefile` | `pyshp` | BIM site import |
| `pysolar`, `ladybug` | `pysolar`, `ladybug-core` | BIM solar/energy tools |
| `opencamlib` | `opencamlib` | CAM, optional -- the module degrades without it |
| `debugpy` | `debugpy` | remote Python debugging, optional |
| `six`, `lark`, `numpy`, `matplotlib`, `PIL`, `packaging` | -- | already present: dependencies of the create line |

`requirements.txt` at the repo root is the upstream list and pins
versions for a pip install; `conda/environment.devenv.yml` is upstream's
conda list and includes `pivy`, `ply`, `pyyaml` and `six` -- neither is
what this stack installs, because both bring their own Coin and OCCT.
Take the names from them, the versions from the solver.

`ifcopenshell`, `smesh` and `libarea` are three more run-time
dependencies and each has its own section below, because none of them
may be solved normally in this env.

### pivy is built, not installed

pivy is the exception in that table and the reason it is not in any
create line: **conda-forge's `pivy` links conda-forge's `coin3d`**, and
this stack runs the fork's Coin (`libCoinRT`). One `libCoinRT` SONAME is
resolved per process, so a conda pivy would pull a second Coin into a
process that already has ours. It is built from `~/works/sw/pivy`
(`rt-0.6.10`) against the Coin install the stack uses -- the recipe is
in [Building the dependencies](#building-the-dependencies-conda-stack),
along with the warning about one pivy not being able to serve both the
release and the debug stack.

### Packages from the realthunder channel

`libarea` (FreeCAD's Area module and, in the same process, ifcopenshell)
is a package, not a local build:

```sh
conda install -p ~/works/sw/fcad/.conda/freecad -c realthunder libarea
```

Unlike OCCT and Coin it goes **into the env prefix**, not a sibling
install dir: both consumers link it, and two copies would mean two
`ClipperLib`s in one process, which is the thing splitting it out was
meant to prevent.

It used to be built by hand from `~/works/sw/libarea` straight into the
prefix. That is obsolete as of 2026-08-25, when the feedstock started
building on all four platforms and published 0.3.1; a hand-install leaves
files conda does not know about, and this box had accumulated two
sonames' worth of them. If `.conda/freecad/lib/libarea.so.*` exists with
no matching `conda-meta/libarea-*.json`, that is the old arrangement:
delete those files (`lib/libarea.so*`, `include/libarea`,
`lib/cmake/libarea`) before installing the package over them.

To confirm the env is in the state this section describes -- one libarea,
owned by conda, and every consumer on it:

```sh
P=~/works/sw/fcad/.conda/freecad
ls $P/conda-meta/libarea-*.json          # must exist: the package is the owner
ls $P/lib/libarea.so*                    # only the sonames that .json lists
# every consumer resolves to that one file, and to the same soname
for f in build/conda-relwithdebinfo-801/Mod/Area/*.so \
         $P/lib/libifcopenshell.geometry.writer.so; do
    echo "$f"; ldd "$f" | grep libarea
done
```

Two different sonames in that last output is the two-Clippers hazard, and a
consumer built against the older one needs a **reconfigure**, not just a
rebuild -- the imported target's path is read at configure time.

### Building the dependencies (conda stack)

Build dirs / installs are parallel to the system stack and never collide:
`<repo>/build_conda_debug` → `<repo>/install/conda-debug`.

**Both OCCT installs on this box are 8.0.1**, from `LinkVibe-801`:
`install/conda-debug-801` (what the debug FreeCAD links) and
`install/conda-relwithdebinfo-801` (the standard stack, where anything
version-guarded is built and measured). The recipe below makes the debug one;
swap `CMAKE_BUILD_TYPE` and `INSTALL_DIR` for the other.

```sh
RUN=~/works/sw/fcad/.conda/run.sh

# OCCT — POLICY shim needed under cmake 4; $ORIGIN rpath is REQUIRED
# (OCCT installs libs with empty RUNPATH otherwise, and RUNPATH is not
# transitive: Part.so finds libTKPart, but libTKPart can't find libTKXDE)
git -C ~/works/sw/occt switch LinkVibe-801
$RUN cmake -S ~/works/sw/occt -B ~/works/sw/occt/build_conda_debug_801 -G Ninja \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DINSTALL_DIR=$HOME/works/sw/occt/install/conda-debug-801 \
  -DCMAKE_INSTALL_RPATH='$ORIGIN' \
  -DBUILD_LIBRARY_TYPE=Shared -DBUILD_MODULE_Draw=OFF \
  -DUSE_TBB=OFF -DUSE_VTK=OFF -DUSE_DRACO=OFF \
  -DUSE_FREETYPE=ON -DUSE_FREEIMAGE=ON -DUSE_RAPIDJSON=ON \
  -DBUILD_RELEASE_DISABLE_EXCEPTIONS=OFF
$RUN cmake --build ~/works/sw/occt/build_conda_debug_801 \
  && $RUN cmake --install ~/works/sw/occt/build_conda_debug_801

# Coin
$RUN cmake -S ~/works/sw/coin -B ~/works/sw/coin/build_conda_debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX=$HOME/works/sw/coin/install/conda-debug \
  -DUSE_EXTERNAL_EXPAT=ON -DSIMAGE_RUNTIME_LINKING=ON \
  -DCOIN_BUILD_TESTS=OFF -DCOIN_BUILD_DOCUMENTATION=OFF
$RUN cmake --build ~/works/sw/coin/build_conda_debug && $RUN cmake --install ~/works/sw/coin/build_conda_debug

# pivy (debug) -- its OWN prefix, NOT site-packages; see the warning below
$RUN cmake -S ~/works/sw/pivy -B ~/works/sw/pivy/build_conda_debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=$HOME/works/sw/coin/install/conda-debug \
  -DCMAKE_INSTALL_RPATH=$HOME/works/sw/coin/install/conda-debug/lib \
  -DPIVY_Python_SITEARCH=$HOME/works/sw/pivy/install/conda-debug \
  -DPython_EXECUTABLE=$HOME/works/sw/fcad/.conda/freecad/bin/python
$RUN cmake --build ~/works/sw/pivy/build_conda_debug && $RUN cmake --install ~/works/sw/pivy/build_conda_debug
```

*** **One pivy cannot serve both stacks, and installing to site-packages makes
them fight.** pivy's `_coin.so` links `libCoinRT.so.80`, and the loader resolves
that SONAME **once per process**. The release and debug Coin installs both
provide it. Since pivy's install destination defaults to the env's single
`site-packages`, whichever configuration was installed **last** silently wins
for every stack -- which is why the pivy in `.conda/freecad` was found built
against `coin/install/conda-relwithdebinfo` even though this recipe names
`conda-debug`.

`PIVY_Python_SITEARCH` (default: `Python_SITEARCH`) is the lever. Keep the
**release** pivy in site-packages, where the standard build finds it with no
PYTHONPATH, and give the **debug** pivy its own prefix, selected explicitly:

```sh
PYTHONPATH=$HOME/works/sw/pivy/install/conda-debug $RUN <debug FreeCAD or python>
```

*** **In a headless session pivy alone decides which Coin loads.**
`FreeCADCmd` does not link Coin itself, so nothing else pulls the SONAME in --
the first `from pivy import coin` settles it for the process. Demonstrated on
the debug FreeCAD build, 2026-08-28: **with** the PYTHONPATH above it maps
`coin/install/conda-debug` and `occt/install/conda-debug-801`, one of each;
**without** it, the same debug binary silently ran on the RelWithDebInfo Coin
through the site-packages pivy. It does not fail, it just is not the stack you
think you are debugging.

Verified 2026-08-28 -- each pivy loads the Coin it was built against, and a
`FreeCADCmd` session that imports pivy maps exactly one `libCoinRT`:

```sh
python -c "from pivy import coin; print([l.split()[-1] for l in \
  open('/proc/self/maps') if 'libCoinRT' in l])"
# site-packages  -> .../coin/install/conda-relwithdebinfo/lib/libCoinRT.so.80.0.6
# PYTHONPATH set -> .../coin/install/conda-debug/lib/libCoinRT.so.80.0.6
```

*** **The debug Coin install goes stale invisibly.** Nothing linked
`coin/install/conda-debug` for two weeks while only the RelWithDebInfo stack was
built, so it sat at 2026-08-14 and was missing `SoLazyElementEx.h`; the first
debug FreeCAD build since then failed on `SoFCVertexCache.cpp` and
`SoFCRenderCache.cpp`. Rebuild Coin **and** pivy debug before trusting a debug
FreeCAD build -- a missing fork header is the signature.

### Building FreeCAD (conda stack)

*** **The standard build is the RelWithDebInfo one, and so is every test run.**
`conda-relwithdebinfo-801` -> `build/conda-relwithdebinfo-801` is the tree that
gets built, tested and measured. The debug preset below exists for debugger
sessions; it is not what the suites run on, and a claim about "the primary
tree" that names a debug dir is wrong.

```sh
RUN=~/works/sw/fcad/.conda/run.sh
cd ~/works/sw/fcad
$RUN cmake --preset conda-relwithdebinfo-801
$RUN cmake --build build/conda-relwithdebinfo-801   # ninja, add -j N to limit parallelism
```

It inherits the repo's `conda-linux-release` preset and points
`CMAKE_PREFIX_PATH`/`OCC_INCLUDE_DIR` at `occt/install/conda-relwithdebinfo-801`
(OCCT 8.0.1) and `coin/install/conda-relwithdebinfo`, with
`CMAKE_POLICY_VERSION_MINIMUM=3.5` (for bgfx's old cmake_minimum_required under
cmake 4), `BUILD_BGFX=ON` and `ENABLE_DEVELOPER_TESTS=ON`.

The debug preset `conda-debug-local` (in `CMakeUserPresets.json`, gitignored)
inherits `conda-linux-debug` and overrides: build dir
`build/conda-debug-occt801`, `CMAKE_PREFIX_PATH`/`OCC_INCLUDE_DIR` pointing at
the local `occt/install/conda-debug-801` (OCCT 8.0.1) and
`coin/install/conda-debug` prefixes, plus the same policy shim and `BUILD_BGFX`.

```sh
$RUN cmake --preset conda-debug-local
$RUN cmake --build build/conda-debug-occt801
```

Built and verified 2026-08-28: clean build, `ctest` 445/445, and a headless
run mapping exactly one Coin (`install/conda-debug`) and one OCCT
(`install/conda-debug-801`). Testing on it is not the routine -- that stays on
the RelWithDebInfo tree -- but the stack is known-good rather than assumed.
Building it needs the debug **Coin and pivy** prefixes to be current first; see
the two warnings in the dependency section.

**Both stacks are OCCT 8.0.1; there is no 7.7.2 on this box any more.** The
frozen 7.7.2 prefixes (`occt/install/conda-debug`,
`occt/install/conda-relwithdebinfo`) and the FreeCAD trees that linked them
(`build/conda-debug`, `build/conda-relwithdebinfo`) were deleted on 2026-08-28
and 2026-08-29, together with their OCCT build dirs and the pre-conda
`install/debug` + `build_debug` stacks in occt, coin and pivy -- about 35GB in
all. The only trees that exist now are the ones this document names: the two
`*-801` FreeCAD trees plus `build/wasm`, and the `conda-*` prefixes of occt,
coin and pivy. They had stopped being a
usable compile check well before that: `Mod/Part/App/ShapeRefSet.cpp` calls
`BRepTools_ShapeSet::Curves2d()` and siblings that exist only on occt
`LinkVibe-801`, unguarded, so 7.7.2 could not compile `Mod/Part` at all. 7.7.2
is frozen -- see `docs/Backport772.md`; if the guarded paths ever need checking
again, rebuild the prefix from the recipe above with `-DCMAKE_BUILD_TYPE` to
taste and the `LinkVibe` branch.

Sources still carry `OCC_VERSION_HEX` guards (features that need the 8.0.1 fork
-- parallel healing, streamed STEP transfer -- fall back to a one-shot path on
7.7.2), but nothing on this box compiles the 7.7.2 side of them any more, so a
change to a guarded path is not compile-checked here. `Mod/Part` could not build
on 7.7.2 even before the prefixes were deleted.

### `BUILD_WEB` defaults OFF (2026-09-08)

`BUILD_WEB` used to default ON, which is a distro assumption: it takes Qt
WebEngine, and no conda stack in this document has WebEngine sitting there
ready to be found. The failure was not a missing workbench either --
`SetupQt.cmake` appends `WebEngineWidgets` to the component list and every
component goes through `find_package(... REQUIRED)`, so leaving the default
alone made a **fresh configure fail outright** on any env created from the
recipes here.

What conda actually offers, on the three boxes:

| | WebEngine |
|---|---|
| Linux (`.conda/freecad`) | not in `qt6-main`; `qt6-webengine` is a separate package, one release behind it |
| Windows (`.conda\freecad`) | same split, and installed only by pinning `qt6-main`/`pyside6`/`qt6-webengine` to a matching 6.10.2 set and relaxing the WebEngine config packages by hand -- see "No toolbars at startup" |
| macOS (osx-64) | **no `qt6-webengine` package at all** -- see the macOS section |

`pyside6` in these envs is built without `QtWebEngineWidgets` in every case, so
even where the C++ side links, the Python side of Web/Help/AddonManager does
not come with it.

So the option is now opt-in: `option(BUILD_WEB ... OFF)` in
`cMake/FreeCAD_Helpers/InitializeFreeCADBuildOptions.cmake`. Turn it ON
deliberately, on a stack that has WebEngine. Nothing else moves when it is off
-- `BUILD_START` stopped depending on it with the Start rewrite, and the only
other consumers are `src/Mod/CMakeLists.txt` and the macOS bundle's Qt
deployment. The presets on all three boxes already set it OFF explicitly; that
line is now redundant rather than load-bearing, and is kept so the value is
visible in the preset.

### Cycles (path-traced renderer)

**Cycles is OFF unless you ask for it.** `BUILD_CYCLES` defaults OFF and neither
preset sets it, so a plain `cmake --preset conda-debug-local` gives a tree with
no path tracer in it. There is deliberately no Cycles preset: the OptiX and CUDA
prefixes are machine-specific, so the flags are stated on the command line.

Why the integration looks the way it does -- why Blender's precompiled bundle is
refused, which feature options are on, what works on which GPU -- is
`docs/CyclesIntegration.md` sections 3 and 4. This section is only the recipe.

**The dependencies live in `.conda/freecad` itself.** `openimageio` 3.1.15.0,
`embree` 4.4.1 and `openimagedenoise` 2.5.1, joining the `openexr` 3.4.15 and
`tbb` 2023.0.0 that were already there and are shared with OCCT:

```sh
conda install -p ~/works/sw/fcad/.conda/freecad -c conda-forge \
    openimageio embree openimagedenoise
```

`docs/CyclesIntegration.md` sec 3.1 says to solve new packages in a scratch env
first, and names a separate prefix as the fallback if the solve turns hostile.
It did not: the solve moved none of `qt6-main`, `pyside6`, `boost`, `tbb` or
`openexr`, so no separate prefix was needed. Check that again before adding
anything else here -- the precedent for why is `cgal-cpp` silently downgrading
boost and breaking every binary in the env.

**nvcc gets its own prefix, and it has to.** `.conda/freecad` pins
`cuda-version` 13.3, while Cycles' runtime check wants 10.2 <= CUDA < 13:

```sh
conda create -p ~/works/sw/fcad/.conda/cuda-129 -c conda-forge \
    cuda-nvcc=12.9 cuda-cudart-dev=12.9
```

**OptiX** is headers-only, cloned from `NVIDIA/optix-dev` (9.1.0) to
`~/works/sw/optix-dev`. It builds and links here but does not RUN on WSL2 --
`libnvoptix.so.1` is a decoy shim; see sec 4.1 of the Cycles doc before spending
any time on it.

Configure and build:

```sh
RUN=~/works/sw/fcad/.conda/run.sh
cd ~/works/sw/fcad
$RUN cmake --preset conda-debug-local -DBUILD_CYCLES=ON \
    -DOPTIX_ROOT_DIR=$HOME/works/sw/optix-dev \
    -DCYCLES_RUNTIME_OPTIX_ROOT_DIR=$HOME/works/sw/optix-dev
$RUN cmake --build build/conda-debug-occt801
```

(Same flags on `conda-relwithdebinfo-801` for the standard tree. Cycles is a
heavy build; that is why it does not default on.)

#### CUDA_BIN_PATH is a RUN-time variable, and forgetting it looks like a bug

```sh
export CUDA_BIN_PATH=$HOME/works/sw/fcad/.conda/cuda-129/bin
```

Cycles compiles its CUDA kernels at runtime by shelling out to nvcc, which it
finds through `CUDA_BIN_PATH` (cuew), and `device/cuda/device.cpp` reports the
CUDA device as available **only if that lookup succeeds**. Without the variable
CUDA does not fail loudly -- it simply vanishes from the device list, in the
standalone and in tree alike, which reads as a broken build rather than a
missing environment variable. `.conda/run.sh` does not set it: run.sh is
thirteen lines and knows nothing about CUDA. It must also reach a **serving**
process, not just an interactive one.

Kernels are compiled from **installed** source (`path_get("source")` resolves
next to the binary), so a build-dir binary finds nothing until `cmake --install`
has run; the results cache under `~/.cache/cycles/kernels`. The first CUDA
kernel compile takes about 297s. `WITH_CYCLES_CUDA_BINARIES` stays OFF --
precompiling the kernel binaries is by a wide margin the slowest part of a
Cycles build.

Verify, in the Python console:

```python
Gui.cyclesDevices()          # must list CPU *and* CUDA -- CPU only => CUDA_BIN_PATH
Gui.cyclesRenderTest("/tmp/cycles.png", 640, 480, 64, "CUDA")
```

Then the real path, on a 3D view: `view.cyclesRender(path, width, height,
samples, device)`, which returns the translation report.


### FEM, and the external SMESH it links

**FEM is part of the standard dev build.** Both conda presets set `BUILD_FEM=ON`
plus `FREECAD_USE_EXTERNAL_SMESH=ON` and `BUILD_FEM_NETGEN=ON`. (`BUILD_FEM`
defaults ON in the repo anyway -- the user presets used to override it OFF, and
that override is why the FEM suites sat out every local test run.) The separate
`build/fem-eval` tree the FEM port used is retired.

Everything FEM needs lives in `.conda/freecad` itself -- there is **no separate
dependency prefix**, and the presets pass no VTK/SMESH/MEDFile/HDF5 paths at
all. That only works because the env now matches the stack these packages are
built for, which is upstream's stack:

| | version | why |
|---|---|---|
| `qt6-main` / `pyside6` | 6.11.2 | what conda-forge builds vtk 9.6.2 against |
| `libboost` | 1.90 | smesh's imported targets name `Boost::*`; 1.85 could not satisfy them |
| `vtk-base` / `vtk-io-ffmpeg` | **9.6.2, pinned** | see below -- do not let this drift |
| `smesh` | 9.9.0.0 `he923b5a_27` from **realthunder** | our fork's feedstock; conda-forge has no occt 8.x build |

```sh
mamba install -p ~/works/sw/fcad/.conda/freecad -c conda-forge \
  qt6-main=6.11.2 pyside6=6.11.2 libboost-devel tbb-devel \
  "vtk-base==9.6.2" "vtk-io-ffmpeg==9.6.2" libmed hdf5 libxml2-devel
mamba install -p ~/works/sw/fcad/.conda/freecad --no-deps realthunder::smesh
```

*** **Two gotchas when running this on a box that is behind.** Both cost a
false start on 2026-08-28:

- **`conda-meta/pinned` blocks its own upgrade.** If the file still pins the
  old Qt (`qt6-main ==6.10.1`), the solve fails with "qt6-main =6.11.2 is not
  installable because it conflicts with any installable versions previously
  reported". Rewrite `pinned` to the four pins above *first*, then install.
- **`mamba repoquery` served a stale conda-forge index** and reported no
  realthunder builds even under `--override-channels -c realthunder`, which
  reads as "the package is gone". `conda search --override-channels -c
  realthunder smesh` showed them immediately. Trust `conda search` here.

Pass `--override-channels -c conda-forge`; without it the solve pulls in
`repo.anaconda.com`. Dry-run it (`--dry-run`) before committing: the
transaction should **remove exactly one package**, `boost-cpp 1.85.0`,
superseded by libboost 1.90, and must leave freetype/freeimage/libstdcxx/gcc/
python alone -- that is what lets the existing OCCT and Coin installs survive
the upgrade instead of needing a rebuild.

*** **`smesh` must be installed with `--no-deps`.** It depends on conda-forge's
`occt`, and letting that in puts a second OCCT in the env with the *same*
SONAMEs (`libTK*.so.8.0`) as our fork's local install -- whichever the loader
reaches first wins, and the fork's features would vanish silently. With
`--no-deps` the env carries no occt at all and smesh's `libTK*.so.8.0` are
resolved by our own 8.0.1 build. Everything else smesh needs (vtk, boost, tbb)
is installed explicitly above.

*** **Why VTK is pinned to 9.6.2 when conda-forge is already on 9.7.0.** Not a
workaround, and not smesh's recipe -- which asks for a bare `vtk`. conda-forge's
`occt 8.0.1` ships two variants, and the VTK-enabled one
(`all_hbdb0871_200`, built 2026-07-31) carries a run-export pin
`vtk-base >=9.6.2,<9.6.3`. smesh builds against occt, gets the `all_` variant,
and inherits that pin; its own `run_exports` then passes it to us. Nothing
downstream of `occt-all` can move to 9.7 until conda-forge rebuilds occt. This
is also why upstream's own freecad 1.1.3 sits on vtk 9.6.2. The pins are
recorded in `.conda/freecad/conda-meta/pinned`:

```
qt6-main ==6.11.2
pyside6 ==6.11.2
vtk-base ==9.6.2
vtk-io-ffmpeg ==9.6.2
```

*** **The lever, half pulled.** FreeCAD uses none of OCCT's VTK bridge -- no
`IVtk*` anywhere in `src/`, and `FindOCC.cmake` never links `TKIVtk` -- and our
local OCCT builds already pass `-DUSE_VTK=OFF`, so a **novtk** occt on the
`realthunder` channel should cut the vtk constraint out of our stack.

Both halves now exist: `occt 8.0.1` is published for linux-64, linux-aarch64,
osx-64, osx-arm64 and win-64 in an `all_` and a `novtk_` variant, and `smesh`
build 27 is built against the novtk one. **The vtk 9.6.2 pin still stands
anyway**, because smesh links VTK itself: its record names `vtk-base
>=9.6.2,<9.6.3` and `vtk-io-ffmpeg >=9.6.2,<9.6.3` directly, not through occt.
What the novtk variant removed is occt's *contribution* to that pin, not the
pin. Keep `conda-meta/pinned` as it is until smesh itself moves.

*** **And it did not make the env solvable again.** With `occt >=8.0.1,<8.0.2`
now satisfiable from the channel, a plain `conda install` into this env no
longer fails to solve -- it succeeds, and quietly installs a **second** OCCT
beside the fork's local build, which is the exact hazard the `@EXPLICIT` rule
was written to avoid. The rule survives; only its reason changed, from "the
solve cannot succeed" to "the solve now succeeds and does the wrong thing".

`gmsh` and CalculiX are found through **preferences, not `PATH`**
(`Mod/Fem/Gmsh:gmshBinaryPath`, `Mod/Fem/Ccx:ccxBinaryPath`); set them with
`ParamGet(...).SetString`, never by hand-editing `user.cfg`.

*** **Do not "fix" a FEM test failure before checking the build tree for stale
files.** `fc_copy_sources` copies but never deletes, so a tree that once built
an older FEM keeps modules and test data the sources dropped years ago. That
alone produced 5 reds in one run: four z88 cases comparing against a `51.txt`
the writer stopped emitting, and an Elmer proxy assert satisfied by a leftover
`femsolver/elmer/solver.py`. The check is one diff:

```sh
diff <(cd src/Mod/Fem && find . -name '*.py' | sort) \
     <(cd build/conda-relwithdebinfo-801/Mod/Fem && find . -name '*.py' | grep -v __pycache__ | sort)
```

Anything present only on the build side is stale: `rm -rf <build>/Mod/Fem` and
rebuild.

#### What the external-SMESH switch cost

The external-SMESH path had never been exercised in this fork. Six defects, all
fixed -- three in `cMake/FindSMESH.cmake`:

- `find_package(VTK REQUIRED)` with no components requires **every** module VTK
  was built with, and 9.6's `xdmf3` demands `Boost 1.90 EXACT`. It now asks for
  one module; `SetupSalomeSMESH()` finds the real list immediately after.
- It built its include list as a single string spanning source lines, so each
  entry carried a newline and a tab and reached the compiler as
  `-I"<sourcedir><tab><realpath>"`.
- A packaged SMESH exports imported targets naming `Boost::filesystem`,
  `Boost::regex`, `Boost::serialization` and `Boost::thread`, and CMake rejects
  the config outright if those targets do not exist yet. Upstream survives only
  because VTK's config happens to run its own `find_package(Boost <exact>)`
  first -- an accident that holds only while the env's Boost is exactly the one
  VTK was built against. `FindSMESH.cmake` now asks for those four directly.

two in `SetupSalomeSMESH()`, both the same shape (the bundled SMESH is a CMake
target carrying VTK in its interface; an external one is plain library paths
carrying nothing):

- VTK 9 dropped `VTK_INCLUDE_DIRS`, which `Fem` and `MeshPart` still list, so
  MeshPart compiled SMESH headers without `vtkType.h`. Recovered from
  `VTK::CommonCore`.
- No VTK libraries reached the link, so `Fem.so` failed on `vtkPolyData::New()`,
  `vtkPythonUtil` and the module registrars. The available component list is now
  appended to `EXTERNAL_SMESH_LIBS` as `VTK::<component>` targets.

and one shared by both paths:

- `SetupSalomeSMESH()` hardcodes the bundled SMESH version 7.7.1, so when the
  version does not come from a config every `#if SMESH_VERSION_MAJOR >= 9` in
  `src/Mod/Fem` compiled its pre-9 branch against a 9.x SMESH. The version is
  now read from the package's `SMESHConfig.cmake`, and the hardcoded block
  applies only to the bundled sources.

`SetupBoost()` also still asked for the **`system`** component, which Boost 1.90
no longer exports (it is header-only, and nothing in `src/` includes
`boost/system`). Upstream dropped it already; so have we.

#### Netgen

Two different consumers, easy to confuse:

- **SMESH's `NETGENPlugin`** (C++, in-process; the legacy "FEM mesh from shape
  by Netgen"). Bundled in our smesh package as `libNETGENPlugin.so` against a
  private `libnglib4smesh.so`, with no `netgen` package dependency. Covered.
- **`femmesh/netgentools.py`** (upstream's newer mesher, with its own task panel
  and preferences page). It runs out-of-process like gmsh -- `QProcess` on a
  generated script -- but the interpreter defaults to **FreeCAD's own python**,
  so the `netgen` **python package** must be importable from `.conda/freecad`.
  conda-forge's netgen pins `occt >=8.0.0,<8.0.1`, which occt 8.0.1 cannot
  satisfy (measured: the solve fails), so a single-environment install needs the
  same feedstock fork gmsh got. Note upstream's own conda package does **not**
  depend on netgen either, so shipping without it is no worse than upstream;
  the `NetgenPythonPath` preference points it at a prefix of its own, exactly as
  `gmshBinaryPath` does for gmsh.

### IfcOpenShell, for Arch/BIM

**IfcOpenShell is part of the standard dev env**, the same way FEM is. Without it
the BIM workbench loads but every IFC path in it is dead: `nativeifc` imports,
the import/export commands appear, and the first one that touches a file raises
`No module named 'ifcopenshell'`. It is a Python package, so nothing in the
build depends on it -- which is exactly why it kept being left out.

*** **Whatever supplies it must not bring an `occt` with it.** IfcOpenShell
links OCCT, and every packaged build depends on the conda-forge `occt`. Letting
that in puts a second OCCT in the prefix under the *same* library names as our
fork's local install, and the process then runs whichever the loader reached
first -- the same hazard the smesh section describes, and the reason smesh is
installed with no `occt` either. The two boxes answer it differently:

| | what supplies it | 2D booleans via libarea |
|---|---|---|
| Linux | the fork built from source into the conda prefix | yes |
| Windows (`D:\Zheng.Lei\sw`) | conda-forge `ifcopenshell`, installed without `occt` | no |
| Windows (`D:\works\sw`) | the fork's own win-64 package, off a GitHub release | yes |

That Windows row is what the env still carries; the fork now builds there too,
see [The fork on Windows](#the-fork-on-windows-2026-09-07-it-builds-and-what-it-took)
below for what it took and what is left before the package can replace it.

On Linux it is the fork (`realthunder/IfcOpenShell`, branch `LinkVibe`),
rebuilt into the conda prefix so it links `libarea.so.2` -- see the ledger in
`docs/CAMPort.md`, which is also where the one open question about that path
lives.

On Windows there is no fork build, and the packaged one is upstream 0.8.5:

```bat
:: ifc_explicit.txt -- @EXPLICIT, then the URLs conda solved for, minus occt:
::   cgal-cpp geos gflags gmp mpfr rocksdb shapely ifcopenshell
conda install -p <env> -y --file ifc_explicit.txt
```

Get that URL list by solving `ifcopenshell` into a **throwaway env** that
carries this env's python and pins, then subtracting what is already installed
and dropping `occt` (and the `vtk` metapackage, which only enters through
occt's `all_` variant). The explicit-file form is required for the reason the
smesh section gives -- no plain `conda install` solves in this env any more.

**The version skew is real and it works.** conda-forge builds ifcopenshell
0.8.5 against occt **8.0.0**; our fork is 8.0.1. With no occt in the prefix its
`TK*.dll` imports are answered by the fork build `FreeCAD.exe` has already
loaded, and the whole path holds -- `ifcopenshell.geom` imports (that is the
half that links OCCT; the bare `import ifcopenshell` does not), and an
`IfcExtrudedAreaSolid` tessellates through it. Measured, not assumed; a symbol
mismatch would show as "the specified procedure could not be found". Re-check
it after either side moves.

What a box gives up by taking the packaged upstream build is the fork's 2D
boolean path -- `boolean_subtraction_2d_using_area`, the one that goes through
libarea rather than the 3D kernel.

**That is no longer a standing gap.** `ifcopenshell-feedstock` has since been
rewired onto the `LinkVibe` branch and now names a `libarea >=0.3.2`
dependency, and win-64 packages exist at 0.9.0alpha0 build 13 for py311
through py314. A box that can reach the channel installs them from it; a box
that cannot takes the same file off a GitHub release -- see
[IfcOpenShell](#ifcopenshell) in the Windows section, which is also where the
checks that actually prove the path is wired are written down. What remains
open in `docs/CAMPort.md` is narrower than it was: not whether the path is
built and linked, but whether it is ever *taken* at runtime.

#### The fork on Windows (2026-09-07): it builds, and what it took

The feedstock rewiring above is done -- `ifcopenshell-feedstock` builds the
fork (`0.9.0alpha0`, `fork_rev` on `LinkVibe`) and depends on `libarea`, so
the 2D path is in the package. A win-64 build now completes 546/546 with no
compiler error, the SWIG wrapper links, and `ifcopenshell.geom` tessellates an
`IfcExtrudedAreaSolid` to 8 verts. Three things were in the way, and only the
first was in the fork's own source:

- **`src/ifcgeom/element.h`** had a bare `friend class iterator;` with no prior
  declaration of `ifcopenshell::geom::iterator` in the header, so MSVC bound
  the name to `std::iterator` (reached through `<algorithm>`/`<memory>`) and
  refused to redeclare a class template as a plain class -- C2990. gcc and
  clang invent a new `geom::iterator` instead, which is why a fork only ever
  compiled on Linux carried it. `tree.h:40` already had the forward
  declaration. Fixed in `dc04c32b2`; it was the ONLY source change MSVC needed
  across all 501 objects.
- **The MSVC toolset**, see the Prerequisites note below.
- **libarea's Windows DLL not exporting its static data members**, see the
  libarea section below.

*** **The GitHub tarball cannot be extracted on Windows without elevation.**
`src/bonsai/.../templates/projects/*.ifc` are git symlinks, and creating those
needs Developer Mode or an elevated session -- CI's runner has it, a desktop
session does not, and rattler-build fails with a bare "failed to unpack". For a
local `rattler-build debug setup`, point the recipe's first source at a clone
next door (`path: ../../ifcopenshell`) instead; git's own fallback writes those
symlinks as ordinary files. Revert before committing.

*** **`rattler-build debug` is the way to iterate here, not CI.** `debug setup`
builds the host/build envs and the work tree, then `conda_build.bat` in the
work dir re-runs the script; make the recipe script's `mkdir build` idempotent
and raise its `ninja install -j 1` (CI's runner has 7 GB; this box has 64) and
add `-k 0` so one pass collects every error instead of stopping at the first.
That turns a 7-minute-per-error CI loop into a local one.

### An optimized stack, for measuring anything

The debug stack is unusable for performance work: a large STEP import runs ~6x slower,
which turns a single experiment into an afternoon. Build the dependencies a second time
as `RelWithDebInfo` (optimized, still symbolized for gdb and callgrind) into parallel
prefixes, and configure FreeCAD with the `conda-relwithdebinfo-801` user preset
(`build/conda-relwithdebinfo-801`):

```sh
RUN=~/works/sw/fcad/.conda/run.sh

# OCCT and Coin: same recipes as above, only the build type and prefixes differ
#   -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   -DINSTALL_DIR=$HOME/works/sw/occt/install/conda-relwithdebinfo-801  (OCCT, LinkVibe-801)
#   -DCMAKE_INSTALL_PREFIX=$HOME/works/sw/coin/install/conda-relwithdebinfo  (Coin)
# then rebuild pivy against the release Coin (same recipe, new prefix).

$RUN cmake --preset conda-relwithdebinfo-801
$RUN cmake --build build/conda-relwithdebinfo-801 -j 14
```

**Measure on the same OCCT the debug stack uses.** The streamed STEP transfer and
parallel healing only exist on the 8.0.1 fork; against 7.7.2 they compile, but fall
back to the one-shot path, so a timing taken there measures a different importer.
`conda-relwithdebinfo-local` (OCCT 7.7.2, `build/conda-relwithdebinfo`) is kept only
as the before-picture for that comparison.

**Build every dependency optimized, not just OCCT.** Leaving Coin in debug is the easy
mistake, and a costly one: in a progressive-import profile ~77% of the instructions
execute inside libCoin, so a `-O0` Coin distorts every measurement and makes inlineable
one-line math (`SbVec3f::operator[]`, `SbBox3f::extendBy`) look like a dominant cost.

**Switching an existing build dir to a new dependency prefix needs more than
`CMAKE_PREFIX_PATH`.** `find_package` results are cached, so `Coin_DIR`,
`COIN3D_INCLUDE_DIRS` and `COIN3D_LIBRARIES` keep pointing at the old prefix and the
build silently links the previous library. Set them explicitly:

```sh
NEW=$HOME/works/sw/coin/install/conda-relwithdebinfo
$RUN cmake -B build/conda-relwithdebinfo-801 \
  -DCoin_DIR=$NEW/lib/cmake/Coin-4.0.6 \
  -DCOIN3D_INCLUDE_DIRS=$NEW/include -DCOIN3D_LIBRARIES=$NEW/lib/libCoinRT.so
```

(The fork's binary is `libCoinRT` since 2026-08-14 -- deliberately renamed away
from stock Coin's soname because the fork's ABI diverges; FreeCAD refuses to
start on a fork ABI mismatch, see `coin_fork_abi()` in the coin repo.)

**The optimized stack goes stale silently, and it fails one layer at a time.**
The debug stack is rebuilt daily, so it stays honest; the optimized one is only
touched when somebody measures something, which can be weeks apart. In between,
the fork moves and its two dependency *installs* do not. Nothing warns you --
the tree simply fails to compile, and the error names a FreeCAD source file even
though the fault is in a dependency install, which reads like a source bug and
is not one.

Budget for the whole chain rather than the first error: each fix uncovers the
next one, because the build cannot reach the later failure until the earlier
file compiles. All three of these were hit in sequence on 2026-08-21, and all
three dated to the same early-August measurement:

| symptom | actually wrong |
|---|---|
| `src/Base/BaseClass.h: error: expected constructor, destructor, or type conversion before '(' token` on `requires(...)`, plus `warning: identifier 'requires' is a keyword in C++20` | the build dir is configured for C++17 |
| `src/Mod/Part/App/ShapeRefSet.cpp: error: 'const class BRepTools_ShapeSet' has no member named 'Curves2d'` (`declared private here`) | stale OCCT install -- the fork makes those accessors public |
| `src/Gui/Application.cpp: fatal error: Inventor/CoinFork.h: No such file or directory` | stale Coin install -- `CoinFork.h` is a fork addition |

**A cached CMake variable survives `cmake --preset`.** The first one above is not
fixed by reconfiguring: `BUILD_ENABLE_CXX_STD` is a cache entry, so a plain
re-run of the preset keeps whatever it already had (the debug tree had `C++20`,
the optimized tree still `C++17`). Pass it explicitly, and confirm in the
generated ninja file rather than in the cache:

```sh
$RUN cmake -S . -B build/conda-relwithdebinfo-801 -DBUILD_ENABLE_CXX_STD=C++20
grep -m1 -oE '\-std=gnu\+\+[0-9]+' build/conda-relwithdebinfo-801/build.ninja   # want gnu++20
```

**Changing the standard forces a FULL rebuild, never a targeted one.** C++17 and
C++20 objects are not ABI-compatible here, so building only the few libraries a
probe needs links new code against stale siblings -- the failure mode is heap
corruption at runtime, not a link error.

Cheapest staleness check before starting, since the installs carry no version
marker -- compare each install against the date of its repo's HEAD. Read the
newest file *inside* the prefix, never the prefix itself: `cmake --install`
overwrites files in place, which adds no directory entry, so the prefix keeps
the mtime of the day it was first created and a current install reports as
weeks old. (Checked 2026-08-21 on the Windows prefixes, which both dated
themselves to Aug 6 while holding files from Aug 15 and Aug 19.)

```sh
newest() { find "$1" -type f -exec stat -c '%y %n' {} + | sort -r | head -1; }
newest ~/works/sw/occt/install/conda-relwithdebinfo-801
newest ~/works/sw/coin/install/conda-relwithdebinfo
git -C ~/works/sw/occt log -1 --format=%cd; git -C ~/works/sw/coin log -1 --format=%cd
```

An install whose newest file predates its repo's last commit is suspect. Refresh
in dependency order -- OCCT, then Coin, then FreeCAD -- each with its own
`--target install`; ccache keeps the FreeCAD objects already compiled before the
failure, so resuming after each fix is much cheaper than the first build was.

**Verify before trusting a number**, every time — this is the check that catches the two
traps above:

```sh
ldd build/conda-relwithdebinfo-801/lib/libFreeCADGui.so | grep -i coin
```

Also note `-DCMAKE_DISABLE_FIND_PACKAGE_Spnav=TRUE` in the preset: a system `libspnav`
gets detected but its headers are not on the conda sysroot's search path, so the
3Dconnexion source fails to compile without it.

### Running & debugging

```sh
RUN=~/works/sw/fcad/.conda/run.sh
$RUN ~/works/sw/fcad/build/conda-relwithdebinfo-801/bin/FreeCAD     # GUI (WSLg)
$RUN ~/works/sw/fcad/build/conda-relwithdebinfo-801/bin/FreeCADCmd  # headless
# gdb: use the debug tree, which carries unoptimized frames.
# PYTHONPATH is REQUIRED here -- it selects the pivy built against the debug
# Coin. Without it the debug binary pulls the release Coin through pivy.
PYTHONPATH=$HOME/works/sw/pivy/install/conda-debug \
  $RUN gdb --args ~/works/sw/fcad/build/conda-debug-occt801/bin/FreeCADCmd script.py
```

- All of fcad/OCCT/Coin have full debug info; gdb breakpoints resolve with source lines
  (e.g. `break App::Document::recompute`). Qt/PySide internals have no symbols (release
  conda packages) — same as with any distro Qt.
- WSLg prints `MESA: error: ZINK: failed to choose pdev` at GUI start — harmless
  fallback noise.
- pivy is importable by the env python directly: `python -c "from pivy import coin"`.
- **Combo box popups leave a stale image under WSLg Wayland.** Pick an entry and the
  drop-down list stays painted on screen; because Qt positions a non-editable combo's
  popup over the combo itself, the next click lands on the combo underneath and reopens
  it, so the list looks like it refuses to dismiss. It only clears when something else
  repaints that region. The popup is genuinely gone -- the widget reports hidden, its
  QWindow reports hidden, `activePopupWidget()` is null and nothing holds a grab -- so
  no application code is involved. A 40-line PySide6 dialog with one QComboBox
  reproduces it, and the same build on `QT_QPA_PLATFORM=xcb` does not, which puts it in
  the Qt Wayland plugin or WSLg's weston. Run the GUI with `QT_QPA_PLATFORM=xcb` to
  avoid it; hardware GL is unaffected (both plugins report `D3D12 (AMD Radeon(TM)
  Graphics)`). Measured against WSLg 1.0.73.2, Qt 6.10.1. `scripts/combo_popup_watch.py`
  is the instrument -- it separates a widget's own visibility from its platform
  window's, which is what tells a live popup from a leftover image.
  **FreeCAD now does this for you**: `preAppSetup()` puts `QT_QPA_PLATFORM=xcb`
  when it detects WSL and `QT_QPA_PLATFORM` is unset. Override with an explicit
  `QT_QPA_PLATFORM=wayland`, or `BaseApp/Preferences/General/PreferXcbOnWsl=false`
  -- so it can be dropped when WSLg fixes the underlying bug. Note Qt's
  `-platform` switch is NOT a way out: FreeCAD's own option parser rejects it and
  the process exits before Qt sees it. The platform plugin has no bearing on GL:
  measured on both plugins, a bare launch gets llvmpipe and the d3d12 driver env
  gets D3D12, with `MESA_D3D12_DEFAULT_ADAPTER_NAME` deciding the adapter
  (unset picks the AMD iGPU; `=NVIDIA` picks the RTX 3070 Ti).

### Quick verification after rebuilds

```sh
# headless kernel sanity (expects volume 500 and "SMOKE OK" pattern)
$RUN build/conda-relwithdebinfo-801/bin/FreeCADCmd /path/to/smoke.py
# GUI + PySide6: launch and confirm no "No module named 'PySide6'" in output,
# Draft/BIM/Assembly/AddonManager appear in the workbench selector
```

#### A renamed module leaves its old self behind in the build tree

`Mod/Path` became `Mod/CAM` and `Mod/Arch` became `Mod/BIM`, but cmake only
ever writes into a build tree -- it never removes what an earlier configure
put there. So a tree that predates a rename keeps a complete copy of the old
module, and that copy is still on `sys.path`: it registers a phantom
`PathWorkbench`/`ArchWorkbench` in the workbench selector, and it **shadows
the new module** -- `import Path` resolved to the dead `Mod/Path/Path` rather
than `Mod/CAM/Path`, and failed on an `undefined symbol` from a `.so` built
months ago. The obsolete `Mod/Path` also carried `area.so` and
`libarea-native.so` from when libarea was vendored, which is a second copy of
Clipper in the process -- the thing the libarea package exists to prevent.

Nothing warns about it. Check for it directly:

```sh
b=build/conda-relwithdebinfo-801
for sub in Mod src/Mod share/Mod; do
    for d in $b/$sub/*/; do
        n=$(basename "$d")
        [ "$n" = CMakeFiles ] || [ -d "src/Mod/$n" ] || echo "STALE $b/$sub/$n"
    done
done
```

Delete what it names, in all three of `Mod/`, `src/Mod/` and `share/Mod/`.
The cache keeps the matching dead option too (`BUILD_PATH`, `BUILD_ARCH` --
no `CMakeLists.txt` defines either any more); drop those two lines and their
comment from `CMakeCache.txt` and reconfigure.

## MCP debug console (AI agent access)

`freecad.mcp_console` (`src/Ext/freecad/mcp_console/`) exposes the running FreeCAD
process to an AI agent over the Model Context Protocol. It serves a single
`run_python` tool via FastMCP's Streamable-HTTP transport; each call is marshalled
onto FreeCAD's Qt main thread (the `Web::AppServer` pattern) so document/OCCT/Coin
work is safe. The interpreter session is persistent (a REPL), captures
stdout/stderr, returns the last expression's value, and reports exceptions as text.

Runtime dependency: the `mcp` Python package in the active interpreter
(`pip install mcp`, or `conda install mcp` from conda-forge; add to the feedstock
host/run deps for distribution).

**Both major versions of `mcp` are supported.** The high-level server class moved,
and so did where the bind address goes, so `_make_server()` picks whichever is
installed:

| `mcp` | class | host/port |
|---|---|---|
| 1.x | `mcp.server.fastmcp.FastMCP` | constructor |
| 2.x | `mcp.server.MCPServer` (`FastMCP` removed) | `run()` |

Both expose the same `tool(name=..., description=...)` decorator and default the
endpoint to `/mcp`, so the rest of the module is version-agnostic. Verified on
1.28.1 and 2.0.0: server constructed, served over Streamable HTTP, `initialize`
answered 200.

Start it from FreeCAD's Python console (main thread):

```python
from freecad import mcp_console
mcp_console.start()          # -> http://127.0.0.1:8765/mcp
```

Point an MCP client (Claude Code, etc.) at that URL. The single tool is
intentional: the whole FreeCAD API is already Python-reachable, so the tool's
description teaches the agent the entry points (`App`, `Gui`, `App.ActiveDocument`,
`dir()`/`help()`) rather than wrapping operations as extra tools.

Conformance is verified end-to-end (initialize / tools list+call with input &
output schema, structured content, main-thread execution, and driving the live
GUI to build a `Part::Box` and read its OCCT volume).

**`127.0.0.1:8765` is ambiguous on a box that also runs WSL2 in mirrored networking
mode.** A FreeCAD started inside the distro serves its console on the same loopback
address the Windows side sees, so a Windows client connects to the *Linux* process and
everything looks normal — same tool, same API, plausible answers — until a path gives it
away (`os.getcwd()` returning `/home/...`, or a screenshot that "saved" successfully and
does not exist on disk). `Get-NetTCPConnection -LocalPort 8765` lists no owning Windows
process in that case, which is the tell. Start the Windows console on its own port
(`mcp_console.start(port=8766)`) whenever both stacks may be live, and verify with
`App.getHomePath()` before trusting a session.

**Screenshots need an unlocked desktop.** `Gui.getMainWindow().grab()` renders the widget
tree, so menus, toolbars, panels and the report view come out fine with the session
locked — but the 3D view's GL surface is not composited and the viewport comes back as a
faint ghost under *both* the default GL and bgfx backends. That is a capture artifact,
not a render defect; the discriminator is a console line like
`bgfx: scene consumed: 3 draws, 3 meshes`, which says the backend really did draw.

## Fallback stack: system gcc + apt Qt 6.4.2

**Not usable as it stands, 2026-08-28.** Two independent reasons: its OCCT is
**7.7.2**, which can no longer compile `Mod/Part` at all
(`ShapeRefSet.cpp` calls 8.0.1-only `BRepTools_ShapeSet` accessors, unguarded)
and is frozen anyway; and **PySide6 is impossible here**, so Python workbenches
do not load. The `debug-local` preset was dropped from `CMakeUserPresets.json`
on 2026-08-28 -- it pointed at `occt/install/debug` and `coin/install/debug`,
neither of which was ever built in this checkout, so it could not configure and
was purely a trap. The notes below are kept as the recipe if the stack is ever
wanted again, against an 8.0.1 OCCT.

- apt deps: qt6-{base,base-private,svg,tools}-dev, qt6-tools-dev-tools, qt6-l10n-tools,
  libxerces-c-dev, libeigen3-dev, boost dev libs incl. libboost-python-dev,
  libyaml-cpp-dev, libfreeimage-dev, rapidjson-dev, GL/X11 dev, swig, cmake, ninja.
- Builds: `<repo>/build_debug` → `<repo>/install/debug` (OCCT 7.7.2 here, configured with
  `-DCMAKE_INSTALL_RPATH='$ORIGIN'` — same transitivity reason as above).
- fcad preset: none any more. It was `debug-local` (inherit `debug`, Makefiles,
  `build/debug`, `FREECAD_QT_VERSION=6`, BUILD_BGFX=ON, BUILD_FEM/WEB=OFF);
  recreate it if the stack comes back. `sh src/make.sh -j8` was the build.
- Runtime needs `PYTHONPATH=$HOME/works/sw/pivy/install/debug/lib/python3.12/site-packages`
  for pivy.

## Windows stack (MSVC 2022 + conda)

Same idea as the Linux conda stack — build the forks against a conda-forge Qt6 —
but Windows imposes constraints Linux does not, and the tree had never been built
in this configuration. Read the two "why" subsections before deviating from the
recipe; most of it is forced rather than chosen.

### Why RelWithDebInfo, not Debug

**This is the constraint that shapes everything else.** MSVC has separate debug and
release C runtimes: `/MDd` defines `_DEBUG`, which sets `_ITERATOR_DEBUG_LEVEL=2`,
while `/MD` leaves it 0. The compiler stamps `detect_mismatch` records for
`RuntimeLibrary` and `_ITERATOR_DEBUG_LEVEL` into every object, so objects and
libraries built against the two cannot be linked into one binary (`LNK2038`) — and
even if they could, two CRTs means two heaps.

conda-forge's Qt6, PySide6 and shiboken6 are release-CRT binaries. Verify with
`dumpbin /dependents` — `MSVCP140.dll` is release, `MSVCP140D.dll` + `ucrtbased.dll`
are debug. So a debug FreeCAD/OCCT/Coin cannot link against them, and rebuilding
Qt6+PySide6 in the debug CRT is not realistic. Every component — OCCT, Coin, FreeCAD
— is therefore **RelWithDebInfo**: optimized, release CRT, full PDBs.

Linux has no equivalent problem (glibc has no debug/release split), which is why the
primary Linux stack can be a true `-O0 -g` debug build and this one cannot. The
practical cost is the usual optimized-build debugging experience: inlined frames and
`<value optimized out>` locals.

### Layout

Everything lives on the fast/large drive; keep the near-full system drive out of it.
**Paths in this section are relative to the repo**, so nothing here names the
directory the forks are kept under: `.conda\freecad` is inside the repo,
`..\occt` is the sibling clone, `..\tools` the sibling scripts directory.
Commands are written to be run from the repo root. `.conda\run.cmd` resolves
the same layout for itself through its `SW_ROOT`, which is the one place an
absolute root is written down.

| What | Path |
|---|---|
| miniforge | `..\miniforge3` |
| conda env | `.conda\freecad` |
| FreeCAD fork | `.` (branch `LinkVibe`) |
| OCCT fork | `..\occt` (branch `LinkVibe-801`) |
| Coin fork | `..\coin` (branch `LinkVibe`) |
| OCCT install | `..\occt\install\win-relwithdebinfo-801` |
| Coin install | `..\install\coin-win-relwithdebinfo` |
| FreeCAD build | `build\win-relwithdebinfo-801` |
| dev shell | `.conda\run.cmd` |

The env prefix is **not** arbitrary: the repo's `conda-windows` preset hardcodes
`${sourceDir}/.conda/freecad`. `.conda/` is already covered by the repo's `.*`
ignore rule.

**Coin cannot install to `<repo>/install/...` on Windows.** The Coin source tree
contains a file named `INSTALL` (autotools documentation), and on a case-insensitive
filesystem CMake cannot create a directory beside it — the install step fails with
"cannot make directory". Hence the out-of-tree Coin prefix. OCCT has no such file
and keeps the Linux-style in-repo layout.

### Prerequisites

- **VS 2022 Build Tools** with the MSVC v143 x64 toolset and a Windows SDK. Note
  `vswhere -property a,b` accepts only ONE property and returns nothing when given
  two — do not conclude from that that MSVC is missing. Check
  `<VS>\VC\Tools\MSVC\<ver>\bin\Hostx64\x64\cl.exe`, and look under
  `D:\Program Files (x86)\` too, not just `Program Files`.

  *** **The toolset must be 14.44 or newer (VS 17.14).** conda-forge's
  `vs2022_win-64` activation asks vswhere for
  `Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64` and, not finding it,
  falls back to `Microsoft.VCToolsVersion.default.txt` **without saying so**. On
  an older toolset a conda-forge **static** library fails to link with unresolved
  externals for MSVC's vectorized STL helpers -- `__std_search_1`,
  `__std_find_end_1`, `__std_remove_8`, `__std_find_first_of_trivial_pos_1`,
  `__std_find_last_of_trivial_pos_1`. Those live in the separately-compiled STL,
  so an older `msvcprt.lib` cannot answer calls a 14.44 compile emitted, and no
  compiler flag works around it. FreeCAD, OCCT and Coin built fine on 14.42 for
  months because they link everything through import libraries, which keeps the
  newer STL calls inside the vendor's DLL; `rocksdb`, shipped as a static lib and
  pulled in by IfcOpenShell, is what surfaced it. Reaching for a shared variant
  is not an escape -- conda-forge's `rocksdb-shared.dll` exports only the C API.

  *** **Updating VS deletes the old toolset**, so every build tree configured
  before the update holds a `CMAKE_CXX_COMPILER` path that no longer exists and
  dies with "is not a full path to an existing compiler tool". Clear
  `CMakeCache.txt` and `CMakeFiles/` in each (`build\...`, `..\occt\build\...`,
  `..\coin\build\...`, `..\pivy\build\...`); the build scripts re-specify their
  full configure, so nothing is lost. Everything rebuilds regardless -- ninja puts
  the compiler path in every command line -- so deleting the tree outright costs
  the same and also clears stale artefacts from renamed targets.
- **Miniforge** (not Miniconda — see the channel note below). Installs unattended with
  `Miniforge3-Windows-x86_64.exe /InstallationType=JustMe /RegisterPython=0 /AddToPath=0
  /S /D=<prefix>` (`/D` last, unquoted).
- Git for Windows, and optionally the gh CLI (the winget MSI needs elevation; the
  portable zip from the GitHub releases page needs none). Set `user.name`/`user.email`
  before the first commit — a fresh box has neither, and the failure only surfaces at
  `git commit`.
- **`git submodule update --init --recursive` right after cloning.** A plain clone has
  empty `src/3rdParty/bgfx`, `src/3rdParty/OndselSolver` and `src/3rdParty/vg-renderer`,
  and configure fails on all three ("does not contain a CMakeLists.txt file"). bgfx
  pulls three nested submodules of its own (bgfx, bimg, bx). **`vg-renderer` is easy to
  miss** because it arrived after the others: it is unconditional inside the
  `BUILD_BGFX` block of `src/3rdParty/CMakeLists.txt`, so a working tree that predates
  it configures fine until the next pull and then stops. `src/3rdParty/cycles` is the
  one submodule to leave alone unless you want the path tracer -- `BUILD_CYCLES`
  defaults OFF and the tree does not need it.
- **If an older Miniconda is also installed**, it exports `CONDA_EXE` into the ambient
  environment, and `conda.bat activate` honours a pre-set `CONDA_EXE`: the *old* conda
  then generates the activation script in its old format, the new `_conda_activate.bat`
  parses it into nothing, and activation reports errorlevel 0 while setting **no
  variable at all** — `cmake`/`python` silently resolve to VS's copies or the Store stub.
  `run.cmd` pins `CONDA_EXE`/`CONDA_PYTHON_EXE` to miniforge and then asserts
  `CONDA_PREFIX`.

### conda env

Community channels only — no Anaconda-hosted ones. Miniforge is the conda-forge
project's own installer and ships no Anaconda Distribution components; `prefix.dev`
serves conda-forge from independent infrastructure. On the first box
`conda.anaconda.org` and `repo.anaconda.com` were unreachable entirely (Cloudflare
IPs refusing TCP 443, IPv6-first DNS → `WinError 10051`), which forced the issue.
`mirrored_channels` in `.condarc` is **not honored** by conda 26.3.2 — it still
resolved to `conda.anaconda.org` — so give the channel URL directly.

```bat
:: install-scoped .condarc at <miniforge>\.condarc
::   channels: [https://prefix.dev/conda-forge]
::   channel_priority: strict
::   pkgs_dirs / envs_dirs pointed at the big drive
conda create -y -p .conda\freecad ^
  --override-channels -c https://prefix.dev/conda-forge ^
  python=3.12 qt6-main=6.11.2 pyside6=6.11.2 qt6-webengine=6.11.2 ^
  cmake ninja swig pkg-config ^
  libboost-devel=1.90 eigen xerces-c zlib yaml-cpp rapidjson freeimage freetype expat ^
  fmt pybind11 numpy matplotlib-base ^
  tbb-devel "vtk-base==9.6.2" "vtk-io-ffmpeg==9.6.2" libmed hdf5 libxml2-devel lazy_loader ^
  lark pyyaml
```

**`pyyaml` is there for the tests, and leaving it out costs 1343 of them
silently.** `Mod/CAM`'s tool-bit serializers `import yaml` at module scope, so
without it `TestCAMApp` does not import -- and `FreeCADCmd -t 0` reports that as
one loader error among thousands of passes rather than as a missing module.
Read the total, as the note at the top of `docs/Testing.md` says. It is in the
env on the Linux box only because something else pulled it in.

**`lark` is there for the build, not the runtime**, which is why it is easy to leave
out of an env that otherwise looks complete. `src/Mod/BIM` generates its Arch SQL
parser as a build step -- `Resources/ArchSqlParserGenerator.py` over `ArchSql.lark`
-- and without the module that target stops the build with *"Error: The 'lark'
Python package is required to generate the parser"*. It fails late, thousands of
objects in, and nothing earlier hints at it.

The Qt/boost/vtk trio is the one the Linux stack pins, and for the same reason:
it is the stack conda-forge builds smesh against. See the Linux
[FEM section](#fem-and-the-external-smesh-it-links) for why the three cannot be
chosen independently. Pin them in `<env>\conda-meta\pinned` so a later
`conda install` cannot bump them:

```
qt6-main ==6.11.2
qt6-webengine ==6.11.2
pyside6 ==6.11.2
vtk-base ==9.6.2
vtk-io-ffmpeg ==9.6.2
python ==3.12.*
```

**Boost 1.85 used to be forced here; it no longer is.** `SetupBoost.cmake`
required the `system` component, which has been header-only since 1.69 and which
current Boost ships neither as a library nor as a CMake config, so `find_package`
failed outright. That request is gone, and the stack moved to 1.90 with the rest
of the FEM dependencies.

**PySide6 quirk, the Windows form of the one the Linux stack has.**
`PySide6Config.cmake` computes `PACKAGE_PREFIX_DIR` as `<env>\Library` and then
`set_and_check`s `Library\typesystems` and `Library\glue`, which conda installs under
`Library\share\PySide6\`. Create **directory junctions** (`mklink /J`) — junctions
need neither admin rights nor developer mode, unlike symlinks:

```bat
mklink /J "<env>\Library\typesystems" "<env>\Library\share\PySide6\typesystems"
mklink /J "<env>\Library\glue"        "<env>\Library\share\PySide6\glue"
```

Redo them after any pyside6 reinstall -- a plain version bump keeps them, because
the junction targets under `share\PySide6\` survive it.

### SMESH: install it without letting conda resolve `occt`

`smesh` comes from the **realthunder** channel (conda-forge has no occt 8.x
build), and it has to arrive without its `occt` dependency: conda-forge's occt
would put a second OCCT in the env carrying the same `TK*.dll` names as our
fork's local install, and whichever the loader reaches first wins.

*** **`--no-deps` does not do this.** Neither conda 26.3 nor mamba 2.5 skips the
solve for it -- both still refuse with "smesh requires occt >=8.0.1, which does
not exist". What does bypass the solver is an `@EXPLICIT` spec file: it installs
exactly the listed URLs, additively, and writes a normal `conda-meta` record.

```bat
:: smesh_explicit.txt
::   @EXPLICIT
::   https://conda.anaconda.org/realthunder/win-64/smesh-9.9.0.0-hc741a3d_27.conda
conda install -p <env> -y --file smesh_explicit.txt
```

Check afterwards that `<env>\Library\bin` carries `SMESH.dll` and friends and
**no** `TK*.dll`. The consequence to remember: the installed record still names
the `occt` dependency, so a later plain `conda install` into this env has to be
given the same explicit-file treatment.

`cMake/FindSMESH.cmake` then finds it with no extra hints -- the env's `Library`
is already first on `CMAKE_PREFIX_PATH`. Two things about that path are worth
knowing:

- Its component list used to be spelled `lib<name>.so`, which no Windows package
  has. It goes through `find_library` now, so the same code picks up `SMESH.lib`
  here and `libSMESH.so` on Linux.
- `SetupSalomeSMESH()` used to hand SMESH's include directories to
  `include_directories()` at the top level, so every translation unit in the
  project compiled with smesh's `Kernel` directory ahead of `src/`. On a
  case-insensitive filesystem that directory's `utilities.h` answers Gui's
  `#include <Utilities.h>`, and the build dies in `StyleParameters/Parser.cpp`
  on the `pthread.h` that SALOME header wants. The global call is gone; Fem,
  Fem/Gui and MeshPart -- the only three consumers -- already list the
  directories themselves, which is also how the bundled SMESH path has always
  worked.
- `BUILD_FEM_NETGEN=ON` is answered by the `NETGENPlugin` the smesh package
  bundles, so the configure line `Could NOT find Netgen (missing: Netgen_DIR)`
  is expected and harmless -- it refers to a standalone Netgen this build does
  not use.

*** **The netgen path needs `pthreads-win32` in the env.** The `NETGENPlugin_*`
headers pull SALOME's `utilities.h`, and its `LocalTraceBufferPool.hxx` includes
`<pthread.h>` and `<semaphore.h>` unconditionally -- FreeCAD's own bundled SMESH
carries a trimmed `utilities.h` and never hits this, which is why no Windows
build saw it before. Two files need it, `Fem/App/FemMeshShapeNetgenObject.cpp`
and `MeshPart/App/Mesher.cpp`. Nothing calls a pthread function, so the headers
alone settle it and no library reaches the link line:

```bat
:: pthreads_explicit.txt
::   @EXPLICIT
::   https://prefix.dev/conda-forge/win-64/pthreads-win32-2.9.1-hfa6e2cd_3.tar.bz2
conda install -p <env> -y --file pthreads_explicit.txt
```

The explicit-file form is used here for the reason given above: the env's smesh
record names an `occt` no channel can supply, so every plain `conda install`
into it now fails to solve.

### IfcOpenShell

Also part of the standard env, and it carries the same do-not-bring-an-occt
rule. The recipe and what the version skew costs are in
[IfcOpenShell, for Arch/BIM](#ifcopenshell-for-archbim) with the rest of it.

**As of 2026-09-07 this box runs the fork, not conda-forge's upstream.** The
table in that section says Windows gets conda-forge `ifcopenshell` and no 2D
booleans via libarea; on `D:\works\sw` that is no longer true. The env carries
`ifcopenshell 0.9.0alpha0 py312h41c9591_13`, built from
`realthunder/IfcOpenShell` branch `LinkVibe`, which needed three fixes before
it would build on win-64 at all: a bare `friend class iterator;` in
`src/ifcgeom/element.h` that MSVC binds to `std::iterator` and rejects
(C2990), six `LNK2019`s against libarea's static data members (see
[libarea](#libarea-and-fetching-a-realthunder-package-when-anacondaorg-is-blocked)
below), and a stream-offset bug in the mmap file reader.

**It arrived over a GitHub release, because the channel is unreachable here.**
That is the general escape hatch on a box with no anaconda.org and no relay
host: anything the fork's channel publishes can be attached to a release on the
repo that produced it and fetched with plain `curl`. `github.com` and
`objects.githubusercontent.com` both answer here. Verify the download against
the channel's own sha256 rather than trusting the transfer.

Install it with a one-line `@EXPLICIT` file naming the local `.conda`, for the
reason the [SMESH section](#smesh-install-it-without-letting-conda-resolve-occt)
gives -- and note that a single-package `@EXPLICIT` file is *also* how the
do-not-bring-an-occt rule is kept: conda links exactly the file named and
resolves nothing, so the package's `occt >=8.0.1` dependency never pulls a
kernel in. Its 22 recorded dependencies are informational in that mode, but
they still have to be satisfied by hand or the module will not load; on this
box twenty already were, `occt` is deliberately absent, and two were not
available at all -- see
[prefix.dev is a partial conda-forge](#prefixdev-is-a-partial-conda-forge-not-a-stale-one).

**What to check afterwards, and what not to bother checking.** `Library\bin`
must still hold no `TK*.dll` and `conda-meta` no `occt` record; `import
ifcopenshell.geom` must be silent, which is the half that links OCCT and so
proves the `.pth` `add_dll_directory` entries still resolve it to our own
8.0.1. To confirm the fork's 2D path is actually wired, read the import table
rather than the Python namespace:

```bat
.conda\run.cmd cmd /c "dumpbin /imports Library\bin\ifcopenshell_geometry_kernel_opencascade.dll"
```

It should list `area.dll` and import `?m_accuracy@CArea@@2NA` and its five
siblings from it. **Do not test for `boolean_subtraction_2d_using_area` in
Python** -- it is not exposed there, `hasattr` is False on both platforms, and
concluding the capability is missing from that is the wrong answer. The Python
surface of the feature is a *setting*, `boolean-attempt-2d-area` (default
True), alongside `boolean-area-2d-fit-circles`; both are fork-only, confirmed
by diffing `src/ifcgeom/conversion_settings.h` between `LinkVibe` and
upstream's `v0.9.0`, where the token appears four times and zero times
respectively.

### libarea, and fetching a realthunder package when anaconda.org is blocked

**A clean Windows configure now fails without `libarea`.** `src/Mod/Area` stopped
vendoring it (`9278814534`) and asks for the package outright --
`find_package(libarea 0.3.0 CONFIG REQUIRED)`, the version being where both Clippers
grew a Z member and changed the layout of a point. `BUILD_AREA` defaults **ON**, and
`CheckInterModuleDependencies.cmake` makes `BUILD_CAM` and `BUILD_BIM` require it, so
this is not a module you quietly skip. The failure is a plain CMake error at
`src/Mod/Area/CMakeLists.txt:7`, before a single object compiles.

It is **not** in the `conda create` line above because it does not come from
conda-forge: like `smesh`, it lives only on the **realthunder** channel.

*** **Install the package into the env; do not build it locally.** That is what
the Linux box does, and this box moved onto it on 2026-09-07 -- `@EXPLICIT` into
`.conda\freecad`, the local prefix dropped from `CMakeUserPresets.json`'s
`CMAKE_PREFIX_PATH`, `LIBAREA_BIN` dropped from `.conda\run.cmd`, and
`..\tools\build-fcad.cmd` staging `area.dll` out of the env. Two copies of
libarea in one process means two `ClipperLib`s, which is the thing splitting it
out was meant to prevent.

*** **Take 0.3.2 or newer.** Up to 0.3.1 the Windows DLL exported no static
**data** members: `WINDOWS_EXPORT_ALL_SYMBOLS`, which libarea leans on because
nothing in its headers was annotated, does not carry data across on its own --
the consumer has to say `dllimport`, or the compiler emits a direct reference
where the import library offers only the `__imp_` form. A consumer linked every
method of `CArea` and none of `m_accuracy`, `m_units`, `m_fit_arcs`,
`m_fit_circles`, `m_clipper_simple`, `m_clipper_clean_distance` or
`Point::tolerance`. Invisible on Linux (ELF exports data and functions alike) and
invisible to FreeCAD, which references none of them; IfcOpenShell's 2D boolean
path was the first consumer to hit it. Fixed in libarea 0.3.2 with
`AreaExport.h` / `LIBAREA_DATA`.

*** **On a network that cannot reach anaconda.org, that channel is unreachable and
`conda` has no way to it.** Both A records for `conda.anaconda.org` (Cloudflare)
refuse TCP 443 here, while DNS resolves fine -- so it presents as a hang and then a
connect error, not as a name error. `prefix.dev` mirrors conda-forge only; it does
not carry this fork's packages.

**Measured reachable again on 2026-09-07** from this box: repodata and a package
both answered HTTP 200 in well under a second, and `conda search --override-channels
-c realthunder` resolved normally. So try the channel directly before reaching for
the workaround below -- but check rather than assume, in either direction: whether
it works is a property of the network you are on, not of the box.

The route that works is to fetch the file somewhere with reachability and install it
locally. Any host will do; this one uses the Linode in `~/.ssh/config`. Note that
the `.ssh` directory in the Windows profile is a **symlink into WSL**, so Git Bash
cannot read it -- go through `wsl.exe -e sh -c '...'`:

```sh
# list what the channel has, and pick the newest build
ssh linode "curl -sSk https://conda.anaconda.org/realthunder/win-64/repodata.json -o /tmp/rt.json"
# fetch it, and read its sha256 out of that repodata to check against
ssh linode "cd /tmp && curl -sSk -O https://conda.anaconda.org/realthunder/win-64/<pkg>.conda && sha256sum <pkg>.conda"
# into the sibling dl/ directory, named the way WSL sees it
scp linode:/tmp/<pkg>.conda "$(wslpath -a ../dl)/"
```

`-k` is needed only because that host is an old Ubuntu with a stale CA bundle.
**A `curl: (60)` there is certificate verification, not a block** -- do not read it
as the channel being unreachable from the proxy as well.

Then install it **without a solve**, for the reason the SMESH section gives: any
plain `conda install` into this env consults the dead channel. An `@EXPLICIT` file
accepts local paths and still writes a proper `conda-meta` record, so the package
ends up owned by conda rather than being the untracked hand-install the Linux
[libarea section](#packages-from-the-realthunder-channel) warns to clean up:

```bat
:: libarea_explicit.txt
::   @EXPLICIT
::   file:///<abs>/dl/libarea-0.3.1-h50a38c3_0.conda#<sha256>
::   -- a file:// URL cannot be relative; <abs> is the sibling dl/ spelled out
conda install -p <env> -y --file libarea_explicit.txt
```

Confirm afterwards that the env has `Library/lib/cmake/libarea/libareaConfig.cmake`
-- that is what `find_package` resolves -- alongside `Library/bin/area.dll` and
`Library/lib/area.lib`. The Windows package depends only on `vc`/`vc14_runtime`/
`ucrt`, so unlike `smesh` it brings no `occt` question with it.

**When there is no host with reachability either, build it.** libarea is its
own repository -- `realthunder/libarea`, which GitHub serves even where
anaconda.org is refused -- and it is a plain CMake project with Clipper built
in and no dependency of its own, so the source route costs a minute:

```bat
git clone https://github.com/realthunder/libarea.git D:\works\sw\libarea
.conda\run.cmd cmake -S D:/works/sw/libarea -B D:/works/sw/libarea/build/win-relwithdebinfo ^
    -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_SHARED_LIBS=ON ^
    -DCMAKE_INSTALL_PREFIX=D:/works/sw/fcad/.conda/freecad/Library
.conda\run.cmd cmake --build   D:/works/sw/libarea/build/win-relwithdebinfo
.conda\run.cmd cmake --install D:/works/sw/libarea/build/win-relwithdebinfo
```

The prefix is the env's `Library` for the reason the Linux section gives -- one
libarea in the process, not one per consumer. What this gives up is exactly
what the `@EXPLICIT` form was chosen for: **conda does not own these files**,
so this is the untracked hand-install the Linux
[libarea section](#packages-from-the-realthunder-channel) says to clean up
before laying the package over it. Delete `Library\bin\area.dll`,
`Library\lib\area.lib`, `Library\lib\cmake\libarea` and
`Library\include\libarea` first if the channel ever becomes reachable.

`smesh` has no such fallback -- so a box in this position builds with
`BUILD_FEM=OFF`, which is what the preset table below already has. IfcOpenShell
has a third route: not conda-forge, which carries only upstream, but a GitHub
release asset off the fork's own repo -- see [IfcOpenShell](#ifcopenshell)
above.

**libarea must be at 0.3.2 or newer if IfcOpenShell's 2D path is in play.**
0.3.1 leaned entirely on `WINDOWS_EXPORT_ALL_SYMBOLS`, which carries functions
across but **not static data**: the import library offers only the `__imp_`
form while the consumer emits a direct reference, so a consumer that reads
`CArea::m_accuracy`, `m_units`, `m_clipper_simple`, `m_clipper_clean_distance`,
`m_fit_arcs`, `m_fit_circles` or `Point::tolerance` fails to link with
`LNK2019` while every *method* of the same class resolves. ELF has no such
split, which is why it was invisible on Linux and macOS. 0.3.2 annotates the
seventeen static data members with `LIBAREA_DATA` and leaves the functions to
the automatic export.

FreeCAD itself is unaffected either way -- across `src/` the only mention of
any of those names is a comment in `src/Mod/Area/App/Area.cpp`, so moving to
0.3.2 is a **relink, not a recompile**, and rebuilding the `Area` target is
enough to confirm it. Verified on 2026-09-07: `Area.pyd` relinks with no
`LNK2019`, and the C++ suite stays at 477 of 477.

**IfcOpenShell without a throwaway env.** The recipe in
[IfcOpenShell, for Arch/BIM](#ifcopenshell-for-archbim) says to solve into a
scratch env and subtract what is already installed; a **dry run against the
real env** answers the same question and cannot be wrong about the second half
of it:

```bat
conda install -p .conda\freecad --override-channels -c https://prefix.dev/conda-forge ^
    ifcopenshell --dry-run --json > ifc-dryrun.json
```

Its `actions.LINK` list is the packages to fetch -- 11 of them here. Drop
`occt` and the `vtk` metapackage, turn the rest into
`<base_url>/<platform>/<dist_name>.conda` lines under an `@EXPLICIT` header,
and install that file. The URL has to be assembled because the dry-run records
carry neither one nor a checksum; `.conda` is the extension for everything
current on conda-forge. Confirm afterwards that `Library\bin` still holds no
`TK*.dll`, and that `import ifcopenshell.geom` works -- that is the half which
links OCCT, and here it resolves against the fork's kernel through the
`add_dll_directory` entries of the `.pth` below.

### prefix.dev is a partial conda-forge, not a stale one

Found 2026-09-07, and it is the failure mode nobody predicts. On a box with no
anaconda.org, `https://prefix.dev/conda-forge` is the conda-forge substitute
this document has used throughout -- but it does **not** carry everything
conda-forge does, and the gap is not a uniform lag you can wait out. Installing
the fork's `ifcopenshell` needed `cgal-cpp >=6.2.1` and `libxml2-16 >=2.15.4`;
prefix.dev's win-64 ceiling was `cgal-cpp 6.2` and `libxml2-16 2.15.3`, one
patch release short in both cases, while conda-forge proper had both.

**A package built against a current conda-forge can therefore be simply
unsatisfiable here.** The near-miss is the trap: two versions that close reads
as "this env is behind, upgrade it", and no amount of upgrading helps because
the mirror has no newer file to give. Before concluding an env is out of date,
check the *mirror's* ceiling:

```bat
conda search --override-channels -c https://prefix.dev/conda-forge --subdir win-64 <pkg>
```

**Mirrors that do carry it, checked from this box:**

| mirror | `/anaconda/cloud/conda-forge` |
|---|---|
| `mirrors.tuna.tsinghua.edu.cn` | 200, has both |
| `mirrors.ustc.edu.cn` | 200, has both |
| `mirror.nju.edu.cn` | 200, has both |
| `mirrors.aliyun.com` | 404 on that path |

**Verify a mirror rather than trusting it** -- it is a third party in the
dependency chain, and "fresher" and "different" look identical from the
outside. Two checks, both cheap:

1. **Fidelity.** Download a package the mirror and prefix.dev *both* carry at
   the same build string, from each, and compare the bytes. On 2026-09-07
   `cgal-cpp-6.2-h29dcab7_0.conda` and `libxml2-16-2.15.3-h3cfd58e_1.conda`
   hashed identically from TUNA and prefix.dev, which is what shows TUNA serves
   genuine conda-forge artifacts rather than repackaged ones.
2. **Corroboration.** For the packages you actually need -- the ones prefix.dev
   cannot supply, so there is nothing to compare against -- fetch from two or
   three mirrors independently and confirm they agree. That does not defend
   against a compromised upstream, but it rules out any single mirror having
   tampered.

**One trap in the libxml2 family**, because it half-installs silently:
`libxml2-16 2.15.4` ships two builds, `h3cfd58e_0` (with `icu >=78.3`) and
`h692994f_0` (without), and the metapackage build has to match the one you
pick -- `libxml2-2.15.4-he095d88_0` pins `h3cfd58e_0`, while `h661ae93_0` pins
`h692994f_0`. This env runs the icu flavour, so it is the `he095d88_0` set,
and `libxml2-devel` follows the same split.

### The dev shell: `.conda\run.cmd`

The analogue of `.conda/run.sh`. It resolves the VS install with `vswhere`, calls
`vcvars64.bat`, activates the conda env — **in that order**, so the env's
cmake/ninja/python win over the copies bundled inside VS — prepends the OCCT and
Coin binary directories to `PATH`, then runs whatever you pass it. Bare, it opens an
interactive prompt.

```bat
.conda\run.cmd cmake --preset win-relwithdebinfo-local
.conda\run.cmd cmake --build build\win-relwithdebinfo-801 -- -j N
```

The `PATH` entries are not optional: **Windows has no rpath**, so the locally built
OCCT and Coin DLLs must be findable or `FreeCAD.exe` dies at load with a bare
`0xc0000135`. This is the Windows counterpart of the `$ORIGIN` problem the OCCT
section above describes.

Two things `run.cmd` works around, both worth knowing if you rewrite it:

- `%ProgramFiles(x86)%` expands at **parse** time, so the `)` in `(x86)` closes any
  enclosing `(` group early — inside `for /f ... in (...)` or a parenthesised `if`
  this silently breaks the line. `run.cmd` uses `goto` labels and a temp file instead.
- `vcvars64.bat` prints `'vswhere.exe' is not recognized` when VS is on a different
  drive from the VS Installer, because VsDevCmd looks for vswhere beside the VS
  install. Harmless; `run.cmd` silences it by putting the Installer directory on
  `PATH` first.

**Calling it from PowerShell: quote every `-D` whose value contains a dot.** Windows
PowerShell 5.1 splits an unquoted native-command argument at the first `.` after an
`=`, and passes the halves as two arguments:

```
-DA=3.5      ->  '-DA=3'  '.5'
-DB=a.b      ->  '-DB=a'  '.b'
-DC=Release  ->  '-DC=Release'      (no dot, survives)
"-DA=3.5"    ->  '-DA=3.5'          (quoted, survives)
```

Every path here has a dot in it (`.conda`, and whatever the root is called), as do version-valued
variables, so this hits constantly — and it is nasty because CMake usually accepts
the truncated value and only complains about the orphaned `.5` as an *"Ignoring extra
path from command line"* warning, tens of lines above whatever eventually fails. The
symptom looks like a defect in the project being configured. `cmd`, and the
`.bat`-style blocks in this document, are unaffected.

### Building OCCT and Coin

Same recipes as the Linux conda stack, with `-DCMAKE_BUILD_TYPE=RelWithDebInfo`,
`-D3RDPARTY_DIR=<env>\Library` for OCCT so it finds freetype/freeimage/rapidjson,
and the prefixes from the layout table. No `CMAKE_INSTALL_RPATH` — meaningless here.

**OCCT's output directory carries a per-configuration suffix**: `win64/vc14/bin`
and `lib` for Release, `bind`/`libd` for Debug, **`bini`/`libi` for RelWithDebInfo**.
This bites in three places — the runtime `PATH` in `run.cmd`, the link line (below),
and any script that hardcodes `lib`.

**`INSTALL_DIR` is sticky.** OCCT derives its internal `CMAKE_INSTALL_PREFIX` from
`INSTALL_DIR` on the *first* configure only; passing a new value later is silently
ignored. Changing it means a fresh build tree.

### Building FreeCAD

User preset `win-relwithdebinfo-local` (in the gitignored `CMakeUserPresets.json`)
inherits `conda-windows-release` and overrides:

| Cache variable | Why |
|---|---|
| `CMAKE_BUILD_TYPE=RelWithDebInfo` | the CRT constraint above |
| `CMAKE_PREFIX_PATH`, `OCC_INCLUDE_DIR` | point at the local OCCT/Coin installs instead of conda packages |
| `OCCT_CMAKE_FALLBACK=OFF` | **required** — see below |
| `BUILD_BGFX=ON` | the renderer |
| `FREECAD_USE_PCL=OFF` | same trim as the Linux local preset |
| `BUILD_WEB=ON` | the env above installs a matched `qt6-webengine`, so Web/Help/AddonManager build; the version-skew dance later in this section is only for an env already pinned to an older Qt. It costs the Web module's targets and a `-DQTWEBENGINE` on every FreeCADGui TU, so decide before the cold build rather than after |
| `BUILD_FEM/FREECAD_USE_EXTERNAL_SMESH=OFF` | Windows only -- both are **ON** on Linux now |
| `ENABLE_DEVELOPER_TESTS=ON` | as on Linux since 2026-09-04; see "Running the C++ (GoogleTest) suites" |

**`OCCT_CMAKE_FALLBACK` must be OFF.** The repo's `conda` preset turns it ON, which
skips `find_package(OpenCASCADE CONFIG)` in favour of a hand-rolled search. That
search does not know about the `libi` suffix, so `find_library` yields
`OCC_LIBRARY-NOTFOUND`, `OCC_LIBRARY_DIR` ends up empty, the
`link_directories(${OCC_LIBRARY_DIR})` in ~35 module `CMakeLists.txt` become no-ops,
and the bare `TKFillet.lib` has no `/LIBPATH`. **Configure still succeeds** and even
prints a plausible OCCT version and library directory; the failure only appears at a
link step thousands of targets later. Our OCCT ships
`cmake/OpenCASCADEConfig.cmake`, which resolves every toolkit to an absolute path.

**bgfx is STATIC on Windows**, unlike the SHARED build used on Linux, and
`src/3rdParty/CMakeLists.txt` now selects that per platform. bgfx applies its
export macro only to the C99 API, so a bgfx DLL exports the C entry points and not
one C++ symbol (verified: 209 exports, zero mangled). Every C++ consumer —
`BGFXRenderer.cpp`, and bgfx's own `example-common`, `geometryv` and `texturev` —
then fails to link with ~78 unresolved externals, all of them C++ (`bgfx::init`,
`bgfx::frame`, `bgfx::Init::Init`) while the C entry points resolve fine. That
asymmetry is the fingerprint of bgfx-as-DLL; ELF default visibility hides it on
Linux.

**Do not try to fix this from the preset**, and note that a *plain* variable does not
work either — `src/3rdParty/CMakeLists.txt` sets it as a **FORCEd cache entry** for
exactly that reason. bgfx's own `CMakeLists.txt` runs
`set(BGFX_LIBRARY_TYPE "SHARED" CACHE STRING "Linking type for library")`, and creating
a cache entry *removes any normal variable of that name from the calling scope*. So on
a fresh cache the plain value set two lines earlier is discarded and the SHARED default
wins; it only appears to work where a STATIC cache entry already exists from an earlier
`-D`, which is why the second Windows box hit the unresolved-symbol wall above on its
first build (fixed in `34f146d508`). A preset/`-D` override is separately useless: it is
what the FORCE now overrides.

```bat
.conda\run.cmd cmake --preset win-relwithdebinfo-local
.conda\run.cmd cmake --build build\win-relwithdebinfo-801 -- -j N
```

**The console code page decides whether incremental builds are correct.** On a
Chinese Windows, `cl.exe` writes its `/showIncludes` lines as *注意: 包含文件:* in
the **console output code page** (CP936), while CMake writes the same string into
`CMakeFiles\rules.ninja` as `msvc_deps_prefix` in **UTF-8**. When the two encodings
disagree ninja matches none of those lines, which has two consequences — one loud,
one silent:

- every compile dumps ~2000 `/showIncludes` lines into the build output (a full
  build log goes from a few thousand lines to ~170 000), and
- **ninja records zero header dependencies for that object.** `ninja -t deps <obj>`
  reports `#deps 0, ... (VALID)`, so the object is only ever rebuilt when its own
  `.cpp` changes. Edit or pull a header and every consumer stays stale.

Nothing fails at the time. The first symptom arrives days later as an unresolved
external at a link step — e.g. `Mesh.pyd` demanding a two-argument
`Base::SequencerLauncher` constructor after a third parameter was added to it, because
`Exporter.cpp.obj` was four days old and had never heard of the change.

`run.cmd` therefore does `chcp 65001` before anything else, which makes the compiler's
bytes match what CMake wrote. **A build launched outside `run.cmd`, or through a
wrapper that gives `cmd.exe` no console (`Start-Process -RedirectStandardOutput`),
re-opens the hole** — redirect inside the command (`cmd /c "... > log 2>&1"`) instead.
`VSLANG=1033` is the other documented fix and is cleaner, but it needs the English
language pack, which this VS Build Tools install does not have — setting it changes
nothing on a Chinese-only install.

To check an existing build tree, and to repair one:

```bat
:: how many objects carry no dependency information at all
ninja -t deps > deps.txt          :: run in the build dir
findstr /c:"#deps 0," deps.txt | find /c /v ""
```

Delete exactly those objects and rebuild; the rest of the tree is sound, so this is
much cheaper than wiping the build directory. On the first box that was 1361 of 3553
objects — all of `src/Gui`, TechDraw, PartDesign, Mesh, Part, Sketcher — against only
30 in bgfx, whose dependency records had happened to be captured from a UTF-8 console.

**Choosing N.** On a 12-thread / 16 GB box, `-j 8` produced
`fatal error C1060: compiler is out of heap space` on OCCT-heavy translation units
(`src/Mod/Part`), and `-j 4` was needed. Peak per-TU memory is roughly 1.5 GB for the
heaviest files — `src/Gui` and anything including OCCT headers — so budget by RAM,
not by core count. A machine with more memory can and should go wider; that limit is
not inherent to the tree. `FREECAD_USE_PCH=OFF` (inherited from the `conda` preset)
makes this worse and is worth revisiting.

On **this** box -- 16 threads, 64 GB -- memory is not the limit and interactivity
is: ninja's default (cores + 2, so 18 concurrent `cl.exe`) makes the machine
unusable while a build runs. 6 is the cap here, and it is a budget for the whole
box rather than per build -- when two builds could overlap, run them one after the
other rather than six each.

It is the **default** now, set in two places because they cover different ways of
starting a build:

- `.conda\run.cmd` sets `CMAKE_BUILD_PARALLEL_LEVEL=6`, which is what
  `cmake --build` uses when given no `-j`. It does nothing for a bare `ninja`,
  which never reads that variable.
- The user preset sets `CMAKE_JOB_POOLS=compile=6;link=6` together with
  `CMAKE_JOB_POOL_COMPILE`/`CMAKE_JOB_POOL_LINK`, which CMake writes into
  `CMakeFiles/rules.ninja` as `pool compile` / `depth = 6`. That binds the build
  edges themselves, so it holds however ninja is started.

An explicit `-j N` still wins over the first; nothing but reconfiguring changes the
pool. Measured on a bare `ninja`, whose own default here is 18, sampling running
`cl.exe` every 200 ms through an 86-edge build: 6 concurrent, never more.

**The number is per box, and RAM is what sets it.** A second Windows box --
28 threads but only 32 GB -- runs 12, in the same two places, and a cold build
of the whole tree (7765 edges, `BUILD_WEB=ON`, bgfx's shader tools included)
took about 75 minutes with no `C1060`. Copy the structure, not the 6.

Note also that `BGFX_BUILD_TOOLS_SHADER=ON` drags in **tint/Dawn** from bgfx's
3rdparty tree — hundreds of heavy C++ TUs that dwarf FreeCAD's own code. It is needed
to compile shaders (`ninja Renderer_assets`), but it is the single largest
contributor to a cold Windows build.

### When processes start and then never run: endpoint security

On a corporate-managed box, budget for a failure that looks like nothing in
this document: a process starts and simply does not proceed. What it looks
like, and how to recognise it rather than chase it:

- one thread, `ThreadState Wait`, `WaitReason UserRequest` or `LpcReply`
- **cumulative CPU near zero** and staying there -- this is the tell that
  separates it from a slow build step
- `Stop-Process -Force` on it alone does nothing; it dies when its parent does

It is not deterministic and not tied to one program. Seen here on `ninja.exe`
launched by a nested `try_compile` (`ninja -t restat`, the tiny invocation
CMake makes inside every compile feature test), on freshly linked test
executables, and on an installer under `start /wait`. Meanwhile a 5666-edge
OCCT build and a 7765-edge FreeCAD build ran through without one.

This box carries **Trend Micro Apex One** -- `TMiACAgentSvc` (Application
Control) and `TMBMServer` (Unauthorized Change Prevention) -- plus a Check
Point endpoint client, on top of Defender. An agent that adjudicates process
creation over an LPC to its service is exactly the shape of that wait, and a
just-linked unsigned binary is exactly what it holds. **The real fix is an IT
exclusion for the tree and the env**; without one, drive the two long steps
through a watchdog, because both resume:

- **configure**: CMake writes a `try_compile` result into `CMakeCache.txt`
  only when the test finishes, so an interrupted attempt is simply retried and
  every completed one is kept. Killing cmake and re-running the preset is
  monotonic progress, not a fresh start. It took two attempts here.
- **build**: ninja keeps every finished object. Restarting is free.

`..\tools\configure-fcad.ps1` and `build-fcad.ps1` are that loop:
launch, poll the log's mtime, and if it has not moved for 150 s (configure) or
300 s (build), kill that attempt's process tree and start again. Scope the kill
to descendants of the attempt: killing every `cmake.exe` and `ninja.exe` on
the box takes out whatever else it is building.

**Stamp the log at the start of every attempt.** The age of the log is the
only stall signal these have, and the log is reused across runs -- so a log
left over from yesterday is already past the threshold before the new attempt
has written its first line, and the very first poll kills a perfectly healthy
process. That is not theoretical: it cost three configure attempts here on
2026-09-08, each reported as `stalled 127,349s` -- a 35-hour-old log, not a
stall. Both scripts now set `(Get-Item $Log).LastWriteTime = Get-Date` before
launching. Neither script is in version control, so this is the record of it.

Do not reach for these on a box without the problem; a stall watchdog will
eventually misfire on a genuinely slow link.

### The wrapper scripts in `..\tools`

Everything above is the underlying command. Day to day these wrap it, and they carry
the flags that are easy to forget; `build-fcad-cycles.cmd` is the Cycles variant
covered in the next section.

| Script | What it does |
|---|---|
| `build-fcad.cmd [jobs]` | configure from the user preset, then build. **Jobs default to 6.** |
| `run_cdb.ps1` | launch FreeCAD under `cdb` in one reused console |
| `mcp_run.py <script.py>` | run Python inside the *running* FreeCAD over MCP |
| `run-cycles.cmd` | `run.cmd` plus the two variables Cycles' GPU devices need |
| `build-occt.cmd` / `build-coin.cmd` | the dependency recipes above, with the suffixes and prefixes filled in |
| `build-libarea.cmd` / `build-pivy.cmd` | the two from-source packages, with the swig-421 prefix wired in |
| `configure-fcad.ps1` / `build-fcad.ps1` | the stall-watchdog forms, for a box with the endpoint-security problem |
| `ctest-fcad.cmd` | `cd` to the build tree and `ctest` through `run.cmd` |
| `ctest-fcad-cleantmp.cmd` / `pytest-fcad-pty.cmd` | the same with `TMP` redirected, and the Python suite under a ConPTY -- see `docs/Testing.md` |

**`build-fcad.cmd`** exists because `run.cmd` alone hid cmake failures -- it
propagates the exit code, printing `BUILD-FCAD: CONFIGURE FAILED` or
`BUILD-FCAD: BUILD FAILED` and ending with `BUILD-FCAD: OK`, which is the string
worth grepping a log for. It also stages `area.dll` beside `FreeCAD.exe` after every
build: the libarea package's DLL is only ever imported by `.pyd` modules, and Python
3.8+ does not search `PATH` for those -- only the app directory, `system32` and
`os.add_dll_directory()` entries.

**`run_cdb.ps1`** owns ONE console for the whole session, so do not hand-roll a
`Start-Process` line. Plain invocation launches or relaunches, `-Status` prints the
console/cdb/FreeCAD pids, `-Stop` closes it. It refuses with `BUSY` while FreeCAD is
up, so quit the app first. Inside the console you get a real prompt: `fcad`
relaunches, Up recalls it. The cdb flags it carries are `-server tcp:port=9310`
(first, so `cdb -remote tcp:server=localhost,port=9310` can attach later), `-G` so
the exit-time break cannot freeze the box, `-lines`, `-cf cdb_arm.cmd` to arm the
crash dumps, and `-logo dbg\cdb_freecad.log`.

Two traps. **A reused console runs the launch command it was born with** --
`Start-FreeCAD` and its argument array are defined when the console starts, so after
editing `run_cdb.ps1`, or passing `-UserHome`/`-StartupScript`, which change those
arguments, a plain relaunch prints `REUSED` and silently launches the *old* command
line. `-Stop` first. And a process created by a debugger gets the NT debug heap
unless `_NO_DEBUG_HEAP=1` is set, which makes OCCT crawl in `free()`; the script sets
it, which is half of why it exists.

**`mcp_run.py`** talks to the MCP console that the running FreeCAD brings up itself,
from `DocumentParams MCPServerAutoStart`. Two things about the port: this build
listens on **8791**, not the 8765 default, because on this mirrored-networking box
the WSL2 FreeCAD answers 8765 *and* 8766 on the Windows `127.0.0.1` -- and
`mcp_run.py`'s own default is 8766, so **`FCAD_MCP_URL` has to be set**. A probe
against the wrong port hangs in retries rather than erroring, which reads exactly
like the app having failed to start. If the port was already taken FreeCAD takes the
next free one and says so, as a console warning and in the Tools -> MCP server
tooltip, so read the port there rather than assuming it.

```bat
set FCAD_MCP_URL=http://127.0.0.1:8791/mcp
.conda\run.cmd python ..\tools\mcp_run.py probe.py
```

Pass a **script file**, not `-c "code"`: nested through `cmd /c ".conda\run.cmd ..."`
the quoting is stripped and the console gets a `SyntaxError` on an unterminated
string. Confirm identity before believing any session -- `App.getHomePath()` must
start with `D:/` for the Windows build, or you are driving the WSL one.

### Cycles on Windows -- where OptiX and HIP can actually be tested

The recipe above under "Cycles (path-traced renderer)" is the Linux one. This
section is the Windows counterpart, and it exists for a reason beyond
completeness: **the WSL2 box and this one are the same laptop**, so the two
devices `docs/CyclesIntegration.md` sec 4.1 and 4.2 record as unreachable --
OptiX, because WSL2's `libnvoptix.so.1` is a 10KB decoy shim, and HIP, because
WSL2 exposes no AMD userspace at all -- are reachable from the Windows side of
it. Sec 4.2 says as much: testing AMD "needs a native Windows build (the
Adrenalin driver carries the HIP runtime)". This is that build.

`BUILD_CYCLES` defaults OFF here too and no preset sets it. The flags live in
`..\tools\build-fcad-cycles.cmd`; drive it from a `.cmd` rather than
the shell, because PowerShell mangles a dotted `-D` value.

**Dependencies go into `.conda\freecad`**, same as Linux:

```cmd
conda install -p .conda\freecad -c conda-forge ^
    openimageio embree openimagedenoise
```

Run the sec 3.1 gate first (`--dry-run`) and read the plan. It passed here: 14
new packages, 85.6 MB, and **no** movement in `qt6-main`, `pyside6`, `boost` or
`tbb`. Two updates, both benign -- `openssl` 3.6.3 -> 3.6.4, and `openexr`
3.4.13 -> **3.4.15**, which converges Windows onto the openexr the Linux env was
already running against OCCT.

**nvcc, in its own prefix** -- and note the path, which is not the Linux one:

```cmd
conda create -p .conda\cuda-129 -c conda-forge ^
    cuda-nvcc=12.9 cuda-cudart-dev=12.9
```

conda puts nvcc in **`Library\bin`**, not `bin`, so `CUDA_BIN_PATH` is
`.conda\cuda-129\Library\bin`. Everything sec 4.1 says about
that variable applies unchanged: get it wrong and CUDA does not fail, it just
vanishes from the device list.

**OptiX** headers to `..\optix-dev` (`NVIDIA/optix-dev`, 9.1.0), the
same clone as Linux. The runtime differs and this is the good news: `nvoptix.dll`
here is the real **62MB** library in the driver store
(`System32\DriverStore\FileRepository\nvam.inf_amd64_*\`), not a shim. It does
not need to be in `System32` -- `optix_stubs.h` walks the driver store through
`optixLoadWindowsDllFromName("nvoptix.dll")`.

**HIP needs AMD's HIP SDK, and PATH is the only lever that works.** Install the
HIP SDK for Windows (6.4 here, `C:\Program Files\AMD\ROCm\6.4`). It does **not**
need a driver upgrade: HIP 6.4's runtime enumerates a 2023-era Adrenalin driver
fine, verified with the SDK's own `hipInfo.exe` before building anything. But
the installer sets `HIP_PATH` and leaves the SDK's `bin` **off** `PATH`, and
both halves of what Cycles needs go through `PATH`:

- `hipew` loads the runtime by **bare name** (`hipew.c`, `WIN_DRIVER`), and the
  default build is hipew6, so it wants `amdhip64_6.dll` -- which the SDK ships
  and the driver does not. The driver's `System32\amdhip64.dll` is the HIP-5
  name, and `WITH_HIP_SDK_5` is a bare `#ifdef` with no `option()` behind it, so
  it is not a flag you can pass.
- `hipewCompilerPath()` finds the compiler with `where hipcc`. Setting
  `HIP_ROCCLR_HOME` instead does **not** work: it `stat()`s `<root>/bin/hipcc`,
  and ROCm 6.4 ships only `hipcc.bat`, `hipcc.exe` and `hipcc.pl`.

`..\tools\run-cycles.cmd` sets `CUDA_BIN_PATH` and prepends the ROCm
bin, then calls `run.cmd`. Neither belongs in `run.cmd` itself, for the reason
the Linux section gives for `CUDA_BIN_PATH`.

Measured 2026-08-28, `cyclesRenderTest` at 640x480 / 64 samples:

| Device | Hardware | Cold | Warm |
| --- | --- | --- | --- |
| CPU | Ryzen 9 6900HS | 1.6s | 1.4s |
| CUDA | RTX 3070 Ti Laptop | 430.2s | 0.5s |
| OptiX | RTX 3070 Ti Laptop | 9.0s | 0.7s |
| HIP | Radeon 680M, gfx1035 | 195.1s | 2.0s |

**Only the warm column is a performance number.** The cold one is dominated by
one-time kernel compiles that then cache: CUDA's 430s matches the ~297s sec 4.1
records for a cold compile, and OptiX's 9s is it reusing what CUDA had just
built, not a faster compile. **The cache is not where the Linux section says**
-- `path_cache_get()` has no XDG branch on Windows, so it is `cache\kernels`
beside the binary (`build\win-relwithdebinfo-801\bin`), and the way to arrange a
cold compile here is to move that directory aside, not to set a variable. Move
it rather than delete it: a cold CUDA compile is five minutes.

Warm, the ordering is the one sec 4.2 predicts. CUDA and OptiX come in around
3x the CPU, and **HIP loses to the CPU** -- a 680M on shared system memory
against eight Zen3+ cores with Embree. That is the expected result, not a broken
HIP port; the AMD path is here for coverage of discrete Radeons. At this scene
size all four are fast enough that the numbers are rough.

All four devices render, and the four PNGs have four distinct checksums -- so
the device argument is honoured rather than quietly falling back to CPU. The
matching file sizes (158231/158231/158230/158231) are just PNG compressing four
near-identical images of one scene, not evidence of a fallback.

#### The second box (`D:\works\sw`): an older driver, and the two things that must match it

Everything above was measured on `D:\Zheng.Lei\sw`. The other Windows box builds
Cycles too, as of 2026-09-08, and needed no source changes -- but **two versions
that section pins have to be chosen against the driver, not copied**, and
getting either wrong fails in a way that does not name the cause.

The hardware differs: an **NVIDIA RTX 2000 Ada** and an Intel iGPU, so CUDA and
OptiX are in play and **HIP is not** -- there is no AMD adapter for it to
enumerate, and the whole ROCm half of this section is inapplicable. The driver
is **566.24 (2024-11-22)**, considerably older than the one above.

**1. The OptiX headers must match the driver's `nvoptix.dll`, not the newest
release.** The runtime always comes out of the driver store, so
`CYCLES_RUNTIME_OPTIX_ROOT_DIR` cannot change it -- both variables only ever
point at headers. Here the driver ships OptiX **8.1.0**:

```bat
powershell -c "(Get-ChildItem C:\Windows\System32\DriverStore\FileRepository -Recurse -Filter nvoptix.dll | Select-Object -First 1).VersionInfo.FileVersion"
```

Build against the 9.1.0 the recipe above names and `cyclesRenderTest` dies with
an **access violation** (`0xC0000005`), not with
`OPTIX_ERROR_UNSUPPORTED_ABI_VERSION` as OptiX's own design intends. Checking
out `v8.1.0` in `optix-dev` turns the crash into a clean `RuntimeError`. Worth
knowing generally: an ABI mismatch here presents as "OptiX is broken", not as
"your driver is too old".

**2. nvcc must emit PTX the driver's OptiX compiler can read.** Cycles compiles
the OptiX kernel to PTX **at run time** with whatever `CUDA_BIN_PATH` points
at, and hands it to `optixModuleCreate`. nvcc **12.9** emits `.version 8.8`,
which needs an r575+ driver; on 566.24 the module is rejected with
`OPTIX_ERROR_INTERNAL_COMPILER_ERROR`, quoting a `.ptx` path that is perfectly
valid. So this box pins the runtime toolkit a release back:

```bat
conda create -p D:\works\sw\fcad\.conda\cuda-126 -c conda-forge ^
    cuda-nvcc=12.6 cuda-cudart-dev=12.6
```

Read the ISA off the generated kernel to check -- `.version` in
`build\win-relwithdebinfo-801\bin\cache\kernels\*.ptx`. **CPU and CUDA are
insensitive to both of these**; only OptiX is, which is why a build can look
fine on two devices out of three. Note also that the cached `.ptx` is keyed on
the kernel source and flags, **not** on the toolkit or header version, so it is
reused across exactly the changes you are trying to test -- clear
`bin\cache\kernels` between them or you will measure the old artifact.

Measured 2026-09-08, `cyclesRenderTest` at 640x480 / 64 samples, RTX 2000 Ada +
i7-13850HX:

| Device | Warm | Cold (first kernel compile) |
| --- | --- | --- |
| CPU | 0.6s | -- |
| CUDA | 0.9s | 1.6s |
| OptiX | 1.4s | **183.8s** |

Three distinct PNG checksums, so the device argument is honoured rather than
falling back to CPU. The 183.8s is the only true cold figure here -- the others
were taken with the kernel cache already populated, which is the easy mistake
to make when reading these numbers back.

Both wrapper scripts referenced above now exist on this box as well:
`build-fcad-cycles.cmd` (configure with `BUILD_CYCLES=ON` plus the two OptiX
paths, then build) and `run-cycles.cmd` (`CUDA_BIN_PATH` and nothing else --
no ROCm, for the reason given above).

**Not isolated:** both changes were made before OptiX rendered, and the header
pin alone was only shown to convert the crash into a clean error. Whether 9.1.0
headers with nvcc 12.6 would also work was not tested. Pinning both to the
driver is the right practice regardless.

#### The one source fix Windows needed

`BUILD_CYCLES=ON` did not link: `FreeCADRenderer.dll` died with `LNK1104` on a
bare `tbb12.lib`. The oneTBB headers auto-link on MSVC (`_config.h`:
`#pragma comment(lib, "tbb12.lib")`), so every object that includes them carries
a DEFAULTLIB directive holding just the file name -- and `cycles_embed` never
propagates TBB, because `cycles_external_libraries_append()` has no TBB entry.
Linux never sees it; the pragma is MSVC-only. Fixed by linking `TBB::tbb` (the
environment's own config package, which points at `tbb12.lib`) PUBLIC into
`FreeCADRendererCycles`. Not `${TBB_LIBRARY}`: Cycles' bundled `FindTBB`
resolves that to the legacy `tbb.lib`, which has no DLL beside it.

That link sits behind `if(TARGET TBB::tbb)`, and **the target has to be made to
exist**, which is the second half of the fix (`0f27186b52`). The only
`find_package(TBB)` in the tree is Cycles' own `external_libs.cmake`, and it resolves
through the bundled `FindTBB` in MODULE mode -- which sets `TBB_LIBRARY` and
`TBB_INCLUDE_DIR` and defines no imported target at all. So on a tree where nothing
else happens to have loaded TBB's config package the guard is simply false, the link
never happens, and the `LNK1104` above comes back with nothing to explain it: a false
`if()` reports nothing, so it reads as a missing library rather than a skipped line.
`src/Gui/Renderer/CMakeLists.txt` therefore calls `find_package(TBB CONFIG QUIET)`
ahead of the guard; `TBB_DIR` pointing at the env's `Library/lib/cmake/TBB` in
`CMakeCache.txt` is how you tell it took. This also means **`tbb-devel` is required
for a Cycles build**, not just `tbb` -- the config package and `tbb12.lib` ship in
it, and Cycles' own `find_package(TBB REQUIRED)` fails the configure without it.

Watch for this shape generally -- a bare library name in an MSVC link error
usually comes out of an object's auto-link pragma, not out of CMake. `tbb12.lib`
appears ten times in this tree's `build.ninja` and every one is a full path.

#### Verifying it: not through `setupWithoutGUI()`, and the two traps under that

To call `Gui.cyclesDevices()` the Gui application has to exist -- under
`FreeCADCmd` a bare `import FreeCADGui` gives the stub module, and
`cyclesDevices` is not on it. The obvious move is `FreeCADGui.setupWithoutGUI()`,
which is `Gui::Application(false)` and makes no window. **Until it was fixed it
aborted, and had done since 2021.** It no longer aborts, but it still does not
bind the Application methods -- `Gui::Application(false)` does not add them to
the module -- so it is not the route to `cyclesDevices()` either. Run the script
under `FreeCAD.exe`, where a real `QApplication` exists. (`showMainWindow()`
under `FreeCADCmd` does bind them, but paints an unstyled window that never
responds, because `FreeCADCmd` runs no event loop. Do not use it.)

Both bugs below are fixed now. They are written down because the *shape* of the
second one will disguise the next abort in this tree just as well, and because
neither is Windows-specific. What used to happen:

```
FreeCADGui_setupWithoutGUI            [Main/FreeCADGuiPy.cpp @ 201]
Gui::Application::Application         [Gui/Application.cpp @ 688]
Gui::ApplicationP::ApplicationP       [Gui/Application.cpp @ 226]
Gui::CommandManager::CommandManager   [Gui/Command.cpp @ 2155]
CmdMacroPreselectCommands::instance   [Gui/Command.cpp @ 2134]
CmdMacroPreselectCommands::{ctor}     [Gui/Command.cpp @ 2095]
Qt6Widgets!QMenu::QMenu               <-- a QWidget, with no QApplication
Qt6Core!QMessageLogger::fatal
FreeCADGui!messageHandler             [Gui/Application.cpp @ 2308] -> abort()
```

`CmdMacroPreselectCommands` held `QMenu _menu` **by value**, and
`CommandManager`'s constructor creates that command unconditionally, so building
the Gui application at all constructed a QWidget. With no `QApplication` Qt calls
`qFatal`. The line dates to `227866ffd9` (2021-05-18). Fixed by creating the
menu on demand; every caller of it runs only once a GUI is up.

**The second half was worse than the first, and it is what you actually
saw.** `Application.cpp`'s `segmentation_fault_handler` **throws** on `SIGABRT`
(`THROWM(Base::AbnormalProgramTermination, ...)`). Throwing out of a signal
handler reached through `abort()` lands the exception in a `noexcept` frame, so
`terminate()` calls `abort()`, which raises `SIGABRT`, which re-enters the
handler -- forever. Each turn of the loop also runs `printBacktrace` ->
`StackWalker::LoadModules` -> dbghelp, reloading PDBs, so the process climbs
through hundreds of MB and looks like a **hang** rather than a crash. Two runs of
the same binary presented as "segfault" and as "hang"; they were the same fault.

Fixed by leaving the disposition at `SIG_DFL` for `SIGABRT` -- the arm that
throws -- so the second `SIGABRT` ends the process the default way, after one
backtrace. `SIGSEGV` keeps its re-arm, which a separate fix added on 2026-08-12
so that a second fault in a session is still reported.

The technique is worth keeping even so, because any *other* abort loop would
look the same. When something appears to hang while growing in memory, attach
and look for `segmentation_fault_handler` repeating up the stack -- and to find
the *original* fault, breakpoint its first entry
(`bu FreeCADApp!segmentation_fault_handler`) rather than reading the top of the
stack, which is all recursion. `sxe av` will not catch it: the first event is
not an access violation.

### Running the C++ (GoogleTest) suites

**`docs/Testing.md`, "C++ on Windows", is authoritative** -- what passes, what
each of the five Windows-only breaks was, and how the DLL path reaches the
tests. The recipe:

```cmd
.conda\run.cmd cmake --preset win-relwithdebinfo-local -DENABLE_DEVELOPER_TESTS=ON
.conda\run.cmd cmake --build build\win-relwithdebinfo-801 -- -j 6
..\tools\ctest-fcad.cmd -j 6
```

472 of 472 pass as of 2026-09-04, in 22s. googletest is vendored
(`tests/lib`), so nothing is fetched.

`ENABLE_DEVELOPER_TESTS` is now **ON** in `CMakeUserPresets.json` here, as it
is on Linux, and it is safe to leave on -- every target builds. Two things
that were true and are not any more: the flag had to be forced onto an
existing tree (`cacheVariables` only seed a fresh configure, so the
`-D` above is what reaches a standing cache), and the test exes needed the
build's `bin` on `PATH` by hand. The build carries that itself now, per
target, so `ctest` works from the build directory with nothing added;
`ctest-fcad.cmd` is only the `cd` and the `run.cmd`.

### Building pivy

Draft and Arch import `pivy.coin` at load time, so without pivy those workbenches
fail to register. There is no pivy source checkout in the layout table by default —
clone one beside the others:

```bat
git clone --depth 1 --branch 0.6.10 https://github.com/coin3d/pivy.git ..\pivy
```

0.6.10 is the version `pivy-feedstock` packages. The feedstock's two patches do not
both apply here: `extend_install_rpath.patch` is meaningless on Windows, while
`windows_cmake_install_path_fix.patch` (upstream `fc622b3b`, one
`file(TO_CMAKE_PATH ...)` on `PIVY_Python_SITEARCH`) **is** needed — `Python_SITEARCH`
comes back with backslashes and the `install(DESTINATION)` that consumes it is not
path-normalised. Apply it to the checkout.

```bat
.conda\run.cmd cmake -G Ninja -B ..\pivy\build\win-relwithdebinfo ^
    -S ..\pivy ^
    -D CMAKE_BUILD_TYPE=RelWithDebInfo ^
    -D CMAKE_PREFIX_PATH=../install/coin-win-relwithdebinfo ^
    -D CMAKE_MODULE_LINKER_FLAGS=/LIBPATH:.conda/freecad/libs ^
    -D DISABLE_SWIG_WARNINGS=ON
.conda\run.cmd cmake --build   ..\pivy\build\win-relwithdebinfo
.conda\run.cmd cmake --install ..\pivy\build\win-relwithdebinfo
```

Install destinations are absolute (`PIVY_Python_SITEARCH`), so `CMAKE_INSTALL_PREFIX`
is irrelevant and the module lands in the env's `Lib\site-packages\pivy` directly.
Two things differ from the feedstock's `bld.bat`:

- **The Python import library must be findable.** `interfaces/CMakeLists.txt` links
  `${Python_LIBRARIES}` only on the `elseif(WIN32)` (i.e. MinGW) branch; the MSVC
  branch just sets `/bigobj` and relies on the `#pragma comment(lib, "python312.lib")`
  that `Python.h` emits, which needs the directory on the linker search path.
  conda-build gets that from the activation script's `LIB`; `run.cmd` does not set it,
  so the `/LIBPATH` above supplies it (`LNK1104: cannot open file 'python312.lib'`
  otherwise). It has to be `CMAKE_MODULE_LINKER_FLAGS` — the SWIG target is a MODULE.
- **SoQt is not built here**, so `find_package(SoQt CONFIG)` (not `REQUIRED`) misses
  and `pivy.gui.soqt` is skipped. FreeCAD only ever imports `pivy.coin`, so this
  costs nothing; `PIVY_USE_QT6` is irrelevant while SoQt is absent.
- **SWIG has to be older than 4.3, and it cannot be the env's.** pivy's
  typemaps still use the Python 2 spellings -- `PyInt_FromLong`,
  `PyInt_AsLong`, `PyString_Check` -- which SWIG defined as macros over the
  Python 3 API in `pyhead.swg` until **4.3.0 dropped Python 2 support and
  removed them**. Against a newer SWIG the generated `coinPYTHON_wrap.cxx`
  compiles into a wall of `error C3861: 'PyInt_FromLong': identifier not
  found`, which reads as a broken checkout rather than a tool version. The env
  carries 4.5.0, and a downgrade in place is not available -- swig 4.2.1 wants
  a `pcre2` older than `qt6-main` 6.11.2 allows, so the solve fails outright.
  Give it its own prefix and name it explicitly, the way `CUDA_BIN_PATH` gets
  its own:

  ```bat
  conda create -y -p D:\works\sw\fcad\.conda\swig-421 ^
      --override-channels -c https://prefix.dev/conda-forge swig=4.2.1
  :: then add to the configure line
  ::   -DSWIG_EXECUTABLE=D:/works/sw/fcad/.conda/swig-421/Library/bin/swig.exe
  ```

  Nothing else in the tree runs SWIG -- `src/Base/swigpyrun.cpp` is checked in
  -- so the env's own copy exists only to satisfy the `find_package(SWIG)` in
  the configure summary, and its version does not matter.

`_coin.pyd` links `Coin4.lib` and needs `Coin4.dll` at runtime, which is the same
no-rpath problem as everything else — the `.pth` in the section below already covers
it, and `run.cmd`'s `PATH` covers a plain `python -c "from pivy import coin"`.

### Windows-only source fixes

The tree had never been built on Windows in this configuration — Qt6 (the port so far
was GCC-only), conda's shared release dependencies (LibPack shipped static ones), and
the RelWithDebInfo configuration were all new. Every one of these is a compile- or
link-time failure, so a Windows build in CI would catch the lot without running
anything.

Committed to the OCCT fork (`4af0655f9a` on `LinkVibe-801`):

- `XSControl_Reader::InitializeMissingParameters()` needed `Standard_EXPORT`.
  `db4c5ac51c` moved the deferred-transfer API into `STEPControl_Reader` and made the
  helper protected, but the two classes live in different toolkits (TKXSBase vs
  TKDESTEP), so the call crosses a DLL boundary. ELF default visibility exports it
  regardless; MSVC does not.

In the FreeCAD tree (all committed since; the list is kept for the reasoning):

| File | Fix |
|---|---|
| `cMake/FreeCAD_Helpers/SetGlobalCompilerAndLinkerSettings.cmake` | bare `/NODEFAULTLIB` discarded **all** default libraries including the CRT, while the replacement library list contains none — nothing C++ could link. Now names the flavours to exclude. |
| same | `/Zm150 /bigobj` were attached to Release and Debug only, so RelWithDebInfo/MinSizeRel could not compile `App/Document.cpp` (`C1128`). Now set for every configuration. |
| same | Qt6 forces `-permissive-` on MSVC consumers via `Qt6::Platform`, which implies `/Zc:strictStrings` and rejects `char *s = "literal"` — the documented CPython idiom for `kwlist`, used in 17 places. Overridden by appending `/Zc:strictStrings-` **to Qt's own interface list**: a linked target's interface options are emitted after anything `CMAKE_CXX_FLAGS` or `add_compile_options()` can place, and the last `/Zc` wins. |
| `src/App/CMakeLists.txt`, `src/Base/CMakeLists.txt` | `BOOST_DYN_LINK` is not a Boost macro and never did anything; `BOOST_ALL_DYN_LINK` is what was meant. Without it Boost omits `__declspec(dllimport)`, and while function calls still resolve through import thunks, **data** symbols do not — linking against a shared Boost fails on `boost::program_options::arg`. Static-Boost LibPack builds never noticed. |
| `src/Gui/Renderer/CMakeLists.txt` | link `opengl32` on Windows. `Qt6::OpenGL` hands consumers libGL on Linux but nothing equivalent here. |
| `src/Gui/Renderer/BGFXRenderer.cpp` | `NOMINMAX` and `#undef near` / `#undef far` before `windows.h` (the latter are empty 16-bit-era macros that silently eat the type in `float far = ...`); 21 framebuffer calls routed through `QOpenGLExtraFunctions`, since opengl32 exports only GL 1.1. `QOpenGLFunctions` is not enough — `glBlitFramebuffer` is in the Extra set. |
| `src/Gui/Renderer/SceneLadder.cpp` | `NOMINMAX` before `windows.h`. |
| `src/Gui/GLPainter.h` | forward-declared Coin node types used as `CoinPtr<>` members; `~intrusive_ptr<T>` needs `T` complete to convert to `SoBase*`. Now includes them. Latent on Linux too — it only works there by luck of include order. |
| `src/Gui/Application.cpp` | `QtPlatformHeaders/QWindowsWindowFunctions` was removed in Qt6. **Behaviour change:** the fullscreen workaround it provided is now Qt5-only. Qt6's equivalent is `QNativeInterface::Private::QWindowsWindow`, reachable only through a private QPA header and `Qt6::GuiPrivate` — an ABI-unstable dependency for a cosmetic fix. Re-check whether Qt6 still hides the menu in fullscreen on an OpenGL window before deciding. |
| `src/Mod/Part/App/AppPartPy.cpp` | `LoadLibrary("TKBRep.dll")` → `LoadLibraryA`. The build defines `UNICODE`, so the unsuffixed macro is `LoadLibraryW` and rejects a narrow literal — this Windows-only branch cannot ever have compiled. |
| `src/3rdParty/CMakeLists.txt` | build bgfx STATIC on Windows. It forced `SHARED` for every platform, and because that is a plain variable it also shadows any cache override — see the bgfx note above. |
| `src/Mod/Web/Gui/BrowserView.cpp` | `WebView::contextMenuEvent()` used `r.linkUrl()` in the view-source branch, but on Qt6 `r` is a pointer (`lastContextMenuRequest()`), dereferenced correctly forty lines earlier. The line sits under `#if defined(QTWEBENGINE)` with no version guard, so it only ever compiled against Qt5. Now uses the `linkUrl` local the function already computed for both branches — same value, no behaviour change. **Not Windows-specific**: any Qt6 build with `BUILD_WEB=ON` hits it; nothing had built Web on Qt6 before. |
| `src/Mod/Material/App/CMakeLists.txt` | link `yaml-cpp::yaml-cpp` instead of `${YAML_CPP_LIBRARIES}`, which is the bare string `"yaml-cpp"`; `YAML_CPP_LIBRARY_DIR` is not set by yaml-cpp's config, so the `link_directories()` beside it is a no-op and the bare name has no search path (`LNK1104`). Also made the unconditional `-DYAML_CPP_STATIC_DEFINE` conditional on the imported target actually being static — conda's yaml-cpp is shared, and the define suppresses the `dllimport` attributes its API needs. |

Unfixed, noticed in passing: `AppPartPy.cpp:380` formats a `size_t` hash with `%x`,
truncating it on any 64-bit target (`C4477`). Equally wrong on Linux; changing it
would alter generated feature labels on both platforms, so it is left alone.

### Python cannot find the OCCT/Coin DLLs — `PATH` is not enough

`run.cmd` puts the OCCT and Coin binary directories on `PATH`, which is what
`FreeCAD.exe` and `FreeCADCmd.exe` themselves need. It does **not** help
`import Part`: since Python 3.8 extension modules are loaded with
`LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`, which deliberately excludes `PATH`. The symptom
is a startup that otherwise looks healthy — banner, version, embedded interpreter all
fine — followed by

```
ImportError: DLL load failed while importing Part: the specified module could not be found
```

A shipped FreeCAD never hits this because every DLL sits next to the executable. For
a dev tree the dependencies live in their own install prefixes, so tell the
interpreter about them once, with a `.pth` in the env's site-packages
(`<env>\Lib\site-packages\fcad-dev-dlls.pth`, one line):

```python
import os; [os.add_dll_directory(p) for p in (r"D:\...\occt\install\win-relwithdebinfo-801\win64\vc14\bini", r"D:\...\install\coin-win-relwithdebinfo\bin") if os.path.isdir(p)]
```

`.pth` lines beginning with `import` are executed at interpreter startup, so this
covers `FreeCADCmd`, the GUI, and any plain `python` in the env. The `isdir` guard
matters: `os.add_dll_directory()` raises on a missing path, which would otherwise
break interpreter startup entirely. Copying the dependency DLLs into `build\bin`
alongside the executables works too and is closer to the shipped layout, at the cost
of duplicating them after every OCCT or Coin rebuild.

### No toolbars at startup — historical, fixed by the Start rewrite

> **Resolved.** Start is now the QtWidgets implementation ported from upstream, so it no
> longer pulls in Web/Chromium and `BUILD_START` no longer depends on `BUILD_WEB`. Start
> is not a workbench any more either — it is an MDI view opened by a `Start_Start`
> command — so `Config["StartWorkbench"]` names `PartDesignWorkbench`, a workbench that
> actually exists, and step 3 below can no longer happen. `StartMigrator.py` rewrites an
> existing profile on first run. The rest of this section is kept because the *shape* of
> the failure recurs: a `REQUIRES_MODS` dependency turning a module off while
> `CMakeCache.txt` still claims it is on.
>
> WebEngine is still needed for Web/Help/AddonManager, so the version-skewed
> `qt6-webengine` install below still applies — it is just no longer what stands between
> you and a usable GUI.

A GUI that comes up with **no toolbars at all**, a menu bar of only File/Edit/View/Help,
and no workbench selector is not a broken build. It is `NoneWorkbench`, and the chain
that gets you there is worth knowing because nothing in it prints a warning at the point
it matters:

1. `BUILD_WEB=OFF` (the preset's original value — conda's `qt6-main` does **not** include
   WebEngine, so it could not have been ON).
2. `CheckInterModuleDependencies.cmake:39` — `REQUIRES_MODS(BUILD_START BUILD_WEB)` — turns
   `BUILD_START` off. It does so with `set(... PARENT_SCOPE)`, so **`CMakeCache.txt` still
   says `BUILD_START:BOOL=ON`** while `add_subdirectory(Start)` never runs. The only trace
   is one `-- BUILD_START requires BUILD_WEB to be ON` line in the configure output.
3. `MainGui.cpp:196` sets `Config["StartWorkbench"] = "StartWorkbench"`, and
   `Gui/Application.cpp:2553` starts that workbench. Its own guard for "the auto workbench
   is not visible" (2569) falls back to *the same* missing name, so it cannot recover.
4. `NoneWorkbench::setupToolBars()` (`Gui/Workbench.cpp:1013`) returns an empty root.

Escape hatch without rebuilding: **View → Workbench** still lists everything that did
build, or set `AutoloadModule` to e.g. `PartDesignWorkbench` under
`BaseApp/Preferences/General` in `user.cfg` (with FreeCAD closed — it rewrites the file on
exit).

To actually get the Start page, `Mod/Web/Gui` must build: `src/Mod/Web/CMakeLists.txt:9`
gates it on `QtWebEngineWidgets_FOUND`, and the non-WebEngine branch of `BrowserView.h`
falls back to QtWebKit's `QWebView`, which does not exist in Qt6. So WebEngine is not
optional here.

**On a fresh env, skip all of this and install a matched set.** conda-forge now carries
`qt6-main`, `pyside6` **and** `qt6-webengine` at 6.10.2, so asking for all three at once
gives a consistent env with no `@EXPLICIT` spec file and no patched `*Dependencies.cmake`
— this is the "clean 6.10.2 bump" the last paragraph of this section recommends, and it
is what the second Windows box was built with. The rest of this subsection applies only
to an env already pinned to 6.10.1.

**Installing it.** conda-forge splits WebEngine out of `qt6-main`, and its oldest build is
**6.10.2** while a 6.10.1-pinned env cannot take it (`conda-meta/pinned`). Rather than bump
the whole Qt stack — which rewrites every Qt header and forces a rebuild of all of Gui and
every module's Gui lib — install the one package against the older Qt. Qt patch releases
are binary-compatible, and this one verifiably is:

```bat
:: 1. its leaf dependencies, solved normally
conda install -p .conda\freecad --override-channels -c https://prefix.dev/conda-forge ^
    snappy minizip libevent re2 libre2-11

:: 2. the package itself. --no-deps does NOT help: conda still SOLVES the dependency and
::    fails on qt6-main 6.10.2. An @EXPLICIT spec file skips the solver entirely and still
::    registers the package in conda-meta.
::      @EXPLICIT
::      https://prefix.dev/conda-forge/win-64/qt6-webengine-6.10.2-pl5321h04170d5_0.conda#<md5>
conda install -p .conda\freecad --file webengine-explicit.txt
```

Then relax the version requests in the WebEngine CMake config packages, which demand their
Qt dependencies at the exact build version (`Qt6Quick;6.10.2`) and otherwise fail configure
with "dependency Qt6Quick could not be found":

```bash
cd .conda/freecad/Library/lib/cmake
sed -i.bak-6102 's/;6\.10\.2/;6.10.1/g' Qt6WebEngine*/*Dependencies.cmake
```

Only version *requests* change; no binary is touched. Verify the real compatibility claim
separately — a missing entry point would show up here, not at configure time:

```bat
.conda\run.cmd python -c "import ctypes,os; os.add_dll_directory(os.path.join(os.environ['CONDA_PREFIX'],'Library','bin')); [ctypes.WinDLL(d) for d in ('Qt6WebEngineCore.dll','Qt6WebEngineWidgets.dll')]"
```

Finally set `BUILD_WEB=ON` in `CMakeUserPresets.json` and rebuild. Expect ~700 targets, not
just Web and Start: `src/Gui/CMakeLists.txt:84` adds `-DQTWEBENGINE` once WebEngine is
found, which changes every FreeCADGui translation unit's command line.

Three things that are *not* problems:

- **No `qt.conf` anywhere in the env.** Qt6Core resolves its own relocatable prefix from the
  DLL path, so it finds `QtWebEngineProcess.exe` in `Library\lib\qt6\` and the resource paks
  in `Library\share\qt6\resources\` without help. Confirmation that the page really rendered
  is a live `QtWebEngineProcess` child process, not just a window.
- **conda's `pyside6` has no `QtWebEngineWidgets` module** (it was built without WebEngine).
  The Start page does not care — it is C++ (`Mod/Start/Gui/Workbench.cpp:82` calls
  `WebGui.openBrowserWindow`). The Python users of it, `Mod/Help/Help.py:243` and
  `Mod/AddonManager/package_details.py`, are both inside `try/except` and fall back to
  `WebGui`.
- **This env is now version-skewed on purpose.** A later `conda update qt6-main` reverts the
  patched `*Dependencies.cmake` files (`.bak-6102` backups sit beside them) and is the point
  to do the clean 6.10.2 bump instead.

### Debugging

**RelWithDebInfo debugs properly** — this is worth stating because the CRT constraint
at the top of this section reads like a compromise, and for debugging it mostly is
not one. Every component carries full private PDBs: 113 in the FreeCAD build tree, 51
for OCCT, plus Coin and pivy. Verified end to end:

```
lm vm FreeCADApp   ->  FreeCADApp C (private pdb symbols)
x FreeCADApp!App::Document::recompute
   FreeCADApp!App::Document::recompute(class std::vector<App::DocumentObject *,...> *, bool, bool *, int)
```

What is lost is the usual optimized-build tax — frames collapsed into their callers by
inlining, `<value optimized out>` locals, stepping that jumps around. When one area
gets sticky, buy the stepping back for that area alone:

```cmake
target_compile_options(FreeCADGui PRIVATE $<$<CONFIG:RelWithDebInfo>:/Od /Ob0>)
```

That changes optimization only, not the runtime library, so it still links against
conda's release-CRT Qt6. `/Ob0` is the important half — inlining, not `/O2`, is what
makes the stacks confusing. **Leave `/DNDEBUG` alone**: OCCT and Coin were compiled
with it, and their headers inline into our translation units, so flipping it for
FreeCAD only invites ODR mismatches.

#### Getting a debugger

VS 2022 **BuildTools** ships no debugger — no `devenv`, and the Windows SDK's
"Debugging Tools" feature is not installed either, so there is no `cdb`, `windbg` or
`gflags` anywhere. WinDbg installs without elevation:

```bat
winget install --id Microsoft.WinDbg --source winget
```

The GUI is then `WinDbgX.exe`, on `PATH` through the WindowsApps alias. The console
debugger `cdb.exe` ships in the same package but **cannot be executed where it is
installed** — WindowsApps ACLs deny execution with "Access is denied" even though the
path reads fine. Copy the package's `amd64\` directory somewhere ordinary
(`..\tools\dbg\`) and run it from there. Worth doing regardless of the
GUI: `cdb` takes a command file, which is what makes debugging scriptable from a
non-interactive shell.

```bat
:: dbg.txt:  sxe ld:FreeCADApp / g / .reload /f FreeCADApp.dll / lm vm FreeCADApp / k / q
.conda\run.cmd ..\tools\dbg\cdb.exe -cf dbg.txt ^
    build\win-relwithdebinfo-801\bin\FreeCADCmd.exe script.py
```

**Launch through `run.cmd`.** A debugger started outside it hands the child no
OCCT/Coin `PATH`, and the process dies at load with a bare `0xc0000135` before any of
this matters — the same problem the two sections above describe, arriving through a
new door.

**Arming crash dumps: two traps that make the arming silently do nothing.** The
GUI run keeps a standing arm file (`..\tools\dbg\arm_freecad.cmd`,
passed as `-cf`, ending in `g`) that dumps on the usual exception filters. Both
of these were live defects, found 2026-08-12 when an access violation left no
dump and no stack:

- **cdb strips backslashes inside a quoted `-c`/`-c2` command string.** A dump
  path written `..\tools\dbg\dumps\fcad.dmp` is stored with each backslash
  eaten or read as an escape -- `..<TAB>oolsdbgdumpsfcad.dmp`, since `\t` is a
  tab -- and no dump is ever written. Doubling
  the backslashes does not help. **Use forward slashes**, and read the setting
  back with `sx` afterwards — printing the stored command is the only way to
  see the mangling.
- **An access violation never reaches second chance in the GUI.**
  `App/Application.cpp` (`segmentation_fault_handler`, `my_se_translator_filter`)
  turns it into `Base::AccessViolation`, which `Gui/GuiApplication.cpp`
  (`GUIApplication::notify`) catches and reports as the "Access violation" /
  "Illegal storage access!" message box. The app handles it, so a `sxn -c2`
  arming never fires, and by the time the box is on screen the C++ throw has
  unwound the faulting frames. Trap it on **first** chance instead, passing it
  on afterwards so behaviour is unchanged:

      sxe -c ".exr -1;r;kv 100;.dump /ma /u ../tools/dbg/dumps/fcad_av.dmp;gn" av

Verify the command path end to end before trusting it: `sxn -c ".echo TEST" eh`,
resume, confirm the echo lands, then `sxn -c "" eh` to clear. To arm a process
that is already running, connect a remote client with `-c "$$<file"` and then
`DebugBreakProcess` the target — queued `-c` commands only run once the debugger
has control. `sx` settings are engine-global, so they survive the client exiting.

#### Four things that will waste your time

- **Launch with `_NO_DEBUG_HEAP=1`, or the app crawls.** A process *created by* a debugger
  gets the NT debug heap: `NtGlobalFlag` comes up `0x70` (heap tail check, free check,
  parameter validation), so every allocation and free walks validation lists. OCCT boolean
  operations churn through huge numbers of small blocks, and the cost lands where you least
  expect it — a PartDesign pattern recompute sat in
  `~IntTools_Context → RtlDebugFreeHeap → RtlpFindEntry`, i.e. burning its time in `free()`,
  not in geometry. It looks exactly like a hang: unresponsive window, no progress, plenty of
  CPU. Set the variable in the environment the debugger inherits, and confirm with
  `!peb` — `BeingDebugged: Yes` with `NtGlobalFlag: 0` is what you want. Attaching to an
  already-running process never enables it, so attach-after-launch works too.
- **Do not break on all C++ exceptions.** OCCT throws `Standard_Failure` as ordinary
  control flow and FreeCAD catches it; `sxe eh` drowns you in first-chance stops that
  mean nothing. Break at the specific throw site instead.
- **conda's Qt6 and PySide6 ship no PDBs**, so any stack that passes through them is
  opaque. Nothing to do about it short of building Qt yourself.
- **Python frames do not decode.** A crash reached from a Draft or Part script shows
  as a wall of `_PyEval_EvalFrameDefault`. Mixed Python/C++ stacks need Visual Studio
  Community with the Python workload; WinDbg cannot do it.

#### What the release CRT costs at runtime

No CRT debug heap and no checked iterators (`_ITERATOR_DEBUG_LEVEL` is 0), so
use-after-free and heap corruption stay silent until they crash somewhere unrelated.
(The *NT* debug heap is a different thing and the debugger switches it on regardless —
see the previous section. It validates block headers, so it catches some corruption, but
it costs far too much to leave on while working.)
Do **not** try to set `_ITERATOR_DEBUG_LEVEL=1` to get the checks back: it changes
container layout, and conda's prebuilt boost and Qt cannot be rebuilt to match. The
two substitutes that work on a release build are PageHeap (needs the SDK's `gflags`,
which the WinDbg package does not include, or the IFEO registry keys — both admin) and
MSVC's ASan, `/fsanitize=address`, which is compatible with `/MD` and with
RelWithDebInfo. Neither has been tried here yet.

### Current state / what is not done yet

- OCCT `LinkVibe-801` and Coin `LinkVibe` build and install cleanly; both are on the
  release CRT (`dumpbin` verified) and match conda's Qt6.
- **The full FreeCAD build completes**: `FreeCADBase`, `FreeCADApp`, `FreeCADGui`,
  `FreeCADRenderer`, `FreeCAD.exe`, `FreeCADCmd.exe` and 34 module `.pyd`.
  (`ninja -n` always reports ~44 pending targets afterwards — `version_check` is an
  always-dirty custom command that reruns git every invocation. Not a failure.)
- **Headless smoke passes.** `FreeCADCmd smoke.py` creating a 10×10×5 `Part::Box`
  reports `volume 499.9999999999999` and a `Solid` TopoShape, i.e. the document /
  recompute engine drives our OCCT through the Python layer correctly.
  One gotcha writing such scripts: a trailing `sys.exit()` can truncate the output.
  `sys.stdout` in `FreeCADCmd` is an ordinary `_io.TextIOWrapper` (FreeCAD does *not*
  replace it headlessly), so it is block-buffered when redirected to a file, while
  the C++ `Base::Console` writes to the handle directly — the two interleave, and an
  early exit can skip Python's flush. End with `sys.stdout.flush()` instead.
- **The GUI launches**, with PySide6 6.10.1 / Qt 6.10.1 loading in-process. The
  renderer path (render-cache mode 3) is still unexercised.
- **pivy 0.6.10 builds and installs** against our Coin, per the section above.
  `from pivy import coin` reports `SIM Coin 4.0.6rt` both in a bare env `python` and
  inside `FreeCADCmd`, and `Draft.make_line` produces a shape of the right length —
  so Draft/Arch load.
- **The Start page is upstream's QtWidgets one**, ported over the old Python/HTML page.
  Verified on this box: the page renders, the First Start wizard comes up (it is gated
  on `FirstStart2024`, default true, and re-openable from the footer button), recent
  files show thumbnails, and **no `QtWebEngineProcess` is spawned** — that last one is
  the check that the Chromium dependency really is gone, since a window alone proves
  nothing. Three fork behaviours the upstream page lacks were restored on top:
  directory-saved projects are listed, the tooltip carries the file info the old MRU
  tooltip had (including the path), and the list follows the MRU instead of being read
  once at construction.

**When testing the GUI, turn autosave off first** (`BaseApp/Preferences/Document` →
`AutoSaveEnabled`, with FreeCAD closed). A session that is killed or closed with an
open document leaves recovery data behind, and the *next* launch puts a modal Document
Recovery dialog over the window — which is exactly what you were trying to look at.

## macOS stack (Intel, macOS 12 + conda)

Brought up 2026-09-06 to answer stage 5 of `SceneServerPort.md`, on macOS
12.7.6 (Monterey) on Intel. `PlatformVerification.md` section 4 is the brief that
was written for that session, from a blank machine; this section is what the box
actually turned out to need, and where the two disagree this one is what was built.

Same shape as the Linux conda stack: forks built against a conda-forge Qt6, one
ABI in the process, our components RelWithDebInfo. The differences are below.

### Prerequisites

- **Xcode**, for the SDK and `ld` only -- the compiler is conda's clang. This box
  has Xcode 12.5 with **SDK 11.3**, and that is enough: conda supplies clang 23.1.0
  *and its own libc++*, so the SDK's ancient libc++ never enters the build. C++20,
  Boost.Beast, a dual-stack `v6_only(false)` socket and Qt6 were all verified
  against it. Do not update Xcode on speculation; if bgfx's Objective-C++ Metal
  backend ever needs a newer SDK, macOS 12.7 can take Xcode 14.2 (SDK 13.1).
- **Miniforge** at `~/miniforge3`, installed with `-b` so it does not touch the
  shell -- as on Linux, it is deliberately not activated in `.bash_profile`.
  Not Miniconda: same channel reason as the Windows section. If a Miniconda is
  already installed, remove it (`conda init --reverse bash`, then delete the
  tree) and drop `defaults` from `~/.condarc`, or its Anaconda-hosted channel
  gets into every solve.

### The env

The Linux create line with `clang_osx-64 clangxx_osx-64` in place of the gcc
packages, and the X11/GL packages dropped (macOS has neither; Qt uses Cocoa,
bgfx uses Metal). On Apple silicon use `clang_osx-arm64 clangxx_osx-arm64`.

```sh
~/miniforge3/bin/mamba create -y -p ~/works/sw/fcad/.conda/freecad \
  clang_osx-64 clangxx_osx-64 cmake ninja make swig pkg-config \
  qt6-main=6.11.1 pyside6=6.11.1 \
  python=3.12 libboost-devel eigen xerces-c zlib yaml-cpp rapidjson freeimage freetype \
  expat fmt pybind11 numpy matplotlib-base lark
printf 'qt6-main ==6.11.1\npyside6 ==6.11.1\npython ==3.12.*\n' \
  > ~/works/sw/fcad/.conda/freecad/conda-meta/pinned
cd ~/works/sw/fcad/.conda/freecad
ln -sfn share/PySide6/typesystems typesystems
ln -sfn share/PySide6/glue glue
~/miniforge3/bin/conda install -p ~/works/sw/fcad/.conda/freecad -c realthunder libarea
```

### FEM, IfcOpenShell and PCL on macOS

Brought up 2026-09-08, after the box had run for two days with
`BUILD_FEM=OFF`. Everything FEM needs exists for osx-64:

```sh
mamba install -p ~/works/sw/fcad/.conda/freecad -c conda-forge \
  vtk=9.6.2 hdf5 libmed tbb-devel libxml2-devel pcl
```

Then pin vtk in `conda-meta/pinned` (`vtk`, `vtk-base` and
`vtk-io-ffmpeg`, all `==9.6.2`) for the reason the Linux FEM section
gives, and note what that install does to the compiler stack: **vtk
takes libboost from 1.92 down to 1.90 and fmt from 12.2 to 12.1**, so
the whole tree needs rebuilding after it. OCCT and Coin do not -- neither
links boost.

*** **`smesh` will not solve in this env, and `--no-deps` does not help.**
`mamba install --no-deps realthunder::smesh` fails with "not installable
because it conflicts with any installable versions previously reported"
-- the same wall the Windows section hits. Use the explicit form, which
skips the solver outright:

```sh
printf '@EXPLICIT\nhttps://conda.anaconda.org/realthunder/osx-64/smesh-9.9.0.0-h5c7f151_27.conda\n' \
  > /tmp/smesh_explicit.txt
conda install -p ~/works/sw/fcad/.conda/freecad -y --file /tmp/smesh_explicit.txt
```

`conda search --override-channels -c realthunder smesh` is what lists the
builds (9.9.0.0 `h5c7f151_27` is the osx-64 one); trust it over
`mamba repoquery`, as the Linux section says. After it, `conda list`
must show **no `occt`**: smesh's own `libTK*.8.0.dylib` are then answered
by the fork's 8.0.1 install, which is the whole point of `--no-deps`.

`BUILD_FEM_NETGEN=ON` needs nothing more -- the plugin ships inside that
smesh package (`libNETGENPlugin.dylib`) and `FindSMESH.cmake` picks it
up. The report line "NETGEN: not enabled" refers to the *standalone*
netgen find, and is expected with external SMESH. The netgen **python**
package stays out here for the same reason as on Linux: it pins
conda-forge's occt.

**IfcOpenShell needs one macOS-only step the other boxes do not.** The
package installs the Windows way -- an explicit file of the URLs a
throwaway solve picked, minus `occt` (constrain that solve with this
env's `hdf5=1.14.6`, `libboost=1.90` and `vtk-base=9.6.2`, or it picks a
build against hdf5 2.2 and drags 17 packages instead of 7; the seven are
`cgal-cpp`, `geos`, `gflags`, `mpfr`, `rocksdb`, `shapely` and
`ifcopenshell` itself). But where Linux resolves `libTK*.so.8.0` by
SONAME and Windows by `PATH`, **macOS resolves by install name**:
`_ifcopenshell_wrapper` asks for `@rpath/libTKMath.8.0.dylib` with a
single rpath of `@loader_path/../../..`, which is the env's `lib` -- and
this env deliberately has no OCCT in it. Add the fork's:

```sh
install_name_tool -add_rpath \
  $HOME/works/sw/occt/install/conda-relwithdebinfo-801/lib \
  ~/works/sw/fcad/.conda/freecad/lib/python3.12/site-packages/ifcopenshell/_ifcopenshell_wrapper.cpython-312-darwin.so
```

It warns that the code signature is invalidated; the module loads
anyway. Re-do it after any reinstall of the package. Verified
2026-09-08: `ifcopenshell.geom` -- the half that links OCCT -- imports,
and an `IfcCartesianPoint` round-trips, against our 8.0.1 where
conda-forge built the package for 8.0.0. The same version skew the
Windows section measured.

*** **`BUILD_WEB` cannot be turned on here.** conda-forge ships no
QtWebEngine for osx-64 at all (`qt6-webengine` does not exist as a
package, and this pyside6 has no `QtWebEngineWidgets`), so the Addon
Manager's "README data will display as text-only" warning is the
permanent state on this box.

*** **Qt is 6.11.1 here, not the 6.11.2 the Linux stack pins.** conda-forge's
`qt6-main=6.11.2` for osx-64 depends on `moltenvk >=1.4.2`, which requires
`__osx >=14.0`; on macOS 12 the solve fails outright. 6.11.1 is the newest pair
that resolves. The Linux pin exists to track the stack conda-forge builds smesh
and vtk against; here the smesh and vtk builds that osx-64 offers install
against 6.11.1 without complaint (see the FEM section above). A box on
macOS 14+ can use 6.11.2 and should.

`libarea` has an osx-64 build (0.3.1, `__osx >=11.0`), so `BUILD_AREA` stays on.

*** **This env was brought up with none of the run-time Python packages
in [the table above](#the-python-packages-the-create-line-does-not-install),
pivy included, and `~/works/sw/pivy` was not even cloned.** Audited
2026-09-07: absent were `pivy`, `typing_extensions`, `ply`, `yaml`,
`requests`, `defusedxml`, `git`, `shapefile`, `pysolar`, `ladybug`,
`opencamlib` and `debugpy`. **Draft, Arch and importDXF did not import
at all** (`typing_extensions`, through
`src/Ext/freecad/deprecation.py`), and nothing that touches the Coin
scene graph from Python ran (`pivy`) -- which is how the chess render
scene came to fail here with a null `ActiveDocument`
(`RenderDebug.md` 5.2c).

Nothing noticed it for three days because the box was brought up for
stage 5, which is headless serving and the C++ suites: `ctest` was 478
of 478 with all of that missing, and the Python suite
(`FreeCADCmd -t 0`) has never been run here. **Run the audit on a new
box before trusting a green ctest**; the probe is fifteen lines and
lives in `Testing.md`.

All of it is installed as of 2026-09-08:

```sh
RUN=~/works/sw/fcad/.conda/run.sh
mamba install -p ~/works/sw/fcad/.conda/freecad -c conda-forge \
  typing_extensions ply pyyaml requests defusedxml gitpython pyshp \
  olefile markdown pygments debugpy pysolar
$RUN python -m pip install ladybug-core   # not on conda-forge
```

`opencamlib` is deliberately left out: it can only be solved by taking
libboost back to 1.90 -- which the FEM section below then does anyway,
so it is worth another try when CAM's toolpath work needs it.

### pivy on macOS, and the SWIG that stopped building it

pivy is a source build here as everywhere (the fork, `rt-0.6.10`),
against the Coin this stack links, with `@loader_path` where Linux uses
`$ORIGIN`. This box installs the release Coin **out of tree**, to
`~/works/sw/install/coin-mac-relwithdebinfo`:

```sh
RUN=~/works/sw/fcad/.conda/run.sh
git clone -b rt-0.6.10 https://github.com/realthunder/pivy ~/works/sw/pivy
$RUN cmake -S ~/works/sw/pivy -B ~/works/sw/pivy/build_conda_rwdi -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=$HOME/works/sw/install/coin-mac-relwithdebinfo \
  -DCMAKE_INSTALL_RPATH=$HOME/works/sw/install/coin-mac-relwithdebinfo/lib \
  -DPython_EXECUTABLE=$HOME/works/sw/fcad/.conda/freecad/bin/python
$RUN cmake --build ~/works/sw/pivy/build_conda_rwdi \
  && $RUN cmake --install ~/works/sw/pivy/build_conda_rwdi
```

*** **The first build of it failed, and the cause is waiting for every
other box: SWIG 4.3 dropped Python 2.** pivy's interfaces are written in
the Python 2 C API (`PyInt_FromLong`, `PyString_Check`,
`PyString_AsString`), which compiled only because SWIG supplied
compatibility macros for those names in `Lib/python/pyhead.swg`. SWIG
4.3 removed that block along with Python 2 support, so from that release
the generated wrapper does not compile at all -- 20 errors in
`coinPYTHON_wrap.cxx` before the error limit stopped it, on this env's
swig 4.5.1. Downgrading swig is not an option: `swig=4.2.1` solves by
**removing qt6-main and pyside6** and taking clang back three major
versions.

The fork's answer is `pivy` commit *Drop Python 2* on `rt-0.6.10`: every
`#ifdef PY_2` branch gone in favour of the Python 3 branch already
beside it, the bare Python 2 spellings renamed to what SWIG's macros
meant (`PyLong_*`, and `PyBytes_Check` for the one `PyString_Check` in
`SbImage.i`), `interfaces/coin2.i` and `soqt2.i` -- the Python 2 module
variants -- deleted, and `setup.py` always passing `-py3`. It builds
clean on swig 4.5.1 and is what the other boxes should take when their
swig moves.

*** **ninja does not re-run swig when an interface file changes.** The
dependency is not tracked, so an edit to `interfaces/*.i` recompiles the
*old* wrapper and the same errors come back. Delete
`build_conda_rwdi/pivy/coinPYTHON_wrap.cxx` to force regeneration.

### `run.sh`, and the xcrun `CPATH` trap

`run.sh` is the Linux one plus one line, and that line is not optional:

```sh
unset CPATH CPLUS_INCLUDE_PATH C_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH
```

**`xcrun` exports `CPATH=/usr/local/include` and `LIBRARY_PATH=/usr/local/lib`,
and `/usr/bin/python3` is an xcrun shim** -- so is anything else that goes
through `xcrun`. A build launched from one inherits both. `CPATH` is searched
*as if `-I`*, which puts it ahead of every `-isystem` path, and `-isystem` is
exactly how CMake hands over Qt6 and Boost. On a box with Homebrew in
`/usr/local` the results are:

- Homebrew **Qt 5.15.2** shadows conda's Qt 6.11.1. This one is loud: moc files
  generated by Qt6's moc meet Qt5 headers and the compile dies with "Qt major
  version not 6 or 7".
- Homebrew **Boost 1.78** shadows conda's Boost 1.92. This one is silent. It
  builds clean and links against conda's libraries, and the mismatch only shows
  up at runtime. It reached 392 Coin translation units and 44 FreeCAD ones
  before it was noticed.

OCCT is immune because it takes its dependencies through explicit `-I`, which
outranks `CPATH`. To audit a tree after the fact, ask ninja rather than looking
for `.d` files (it folds them into a binary database):

```sh
~/works/sw/fcad/.conda/run.sh ninja -C <build dir> -t deps | grep -c /usr/local/include
```

Zero is the only acceptable answer. Anything else means the objects were built
against the wrong headers and the tree needs deleting, not rebuilding.

### OCCT and Coin

The Linux recipes with `RelWithDebInfo` and `@loader_path` where Linux has
`$ORIGIN` (OCCT installs its libraries with no rpath otherwise, and rpath is not
transitive). **Coin must install out of tree**: the filesystem is
case-insensitive by default and the Coin source tree has a file named `INSTALL`,
so `<coin repo>/install` collides. OCCT has no such collision and installs in
tree as on Linux.

```sh
RUN=~/works/sw/fcad/.conda/run.sh
$RUN cmake -S ~/works/sw/occt -B ~/works/sw/occt/build_conda_rwdi_801 -G Ninja \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DINSTALL_DIR=$HOME/works/sw/occt/install/conda-relwithdebinfo-801 \
  -DCMAKE_INSTALL_RPATH='@loader_path' \
  -DBUILD_LIBRARY_TYPE=Shared -DBUILD_MODULE_Draw=OFF \
  -DUSE_TBB=OFF -DUSE_VTK=OFF -DUSE_DRACO=OFF \
  -DUSE_FREETYPE=ON -DUSE_FREEIMAGE=ON -DUSE_RAPIDJSON=ON \
  -DBUILD_RELEASE_DISABLE_EXCEPTIONS=OFF

$RUN cmake -S ~/works/sw/coin -B ~/works/sw/coin/build_conda_rwdi -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=$HOME/works/sw/install/coin-mac-relwithdebinfo \
  -DUSE_EXTERNAL_EXPAT=ON -DSIMAGE_RUNTIME_LINKING=ON \
  -DCOIN_BUILD_TESTS=OFF -DCOIN_BUILD_DOCUMENTATION=OFF
```

Both built with no configure work beyond the above. The fork installs
**`libCoinRT.dylib`**, not `libCoin.dylib` -- the preset below must name the
real file.

### FreeCAD

A local preset in the gitignored `CMakeUserPresets.json`, inheriting the repo's
`conda-macos-release`. It overrides `cmakeExecutable` because that preset points
at `conda/cmake.sh`, which runs `mamba run -n freecad cmake` -- a *named* env,
which this stack is not. `OCCT_CMAKE_FALLBACK=OFF` is the Windows lesson: our
OCCT ships `OpenCASCADEConfig.cmake`, and the fallback's hand-rolled search does
not know local layouts. The three `CMAKE_CXX_FLAGS` entries are the feedstock's
Darwin branch (`_LIBCPP_DISABLE_AVAILABILITY`, `BOOST_NO_CXX98_FUNCTION_BASE`,
`-Wno-enum-constexpr-conversion`).

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "mac-relwithdebinfo-801",
      "inherits": "conda-macos-release",
      "cmakeExecutable": "${sourceDir}/.conda/freecad/bin/cmake",
      "binaryDir": "${sourceDir}/build/mac-relwithdebinfo-801",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "FREECAD_QT_VERSION": "6",
        "CMAKE_POLICY_VERSION_MINIMUM": "3.5",
        "CMAKE_CXX_FLAGS": "-D_LIBCPP_DISABLE_AVAILABILITY -DBOOST_NO_CXX98_FUNCTION_BASE -Wno-enum-constexpr-conversion",
        "CMAKE_PREFIX_PATH": "$env{HOME}/works/sw/occt/install/conda-relwithdebinfo-801;$env{HOME}/works/sw/install/coin-mac-relwithdebinfo;${sourceDir}/.conda/freecad",
        "CMAKE_LIBRARY_PATH": "$env{HOME}/works/sw/occt/install/conda-relwithdebinfo-801/lib;${sourceDir}/.conda/freecad/lib",
        "OCC_INCLUDE_DIR": "$env{HOME}/works/sw/occt/install/conda-relwithdebinfo-801/include/opencascade",
        "OCCT_CMAKE_FALLBACK": "OFF",
        "Coin_DIR": "$env{HOME}/works/sw/install/coin-mac-relwithdebinfo/lib/cmake/Coin-4.0.6",
        "COIN3D_INCLUDE_DIRS": "$env{HOME}/works/sw/install/coin-mac-relwithdebinfo/include",
        "COIN3D_LIBRARIES": "$env{HOME}/works/sw/install/coin-mac-relwithdebinfo/lib/libCoinRT.dylib",
        "CMAKE_DISABLE_FIND_PACKAGE_Spnav": "TRUE",
        "FREECAD_USE_3DCONNEXION": "OFF",
        "BUILD_BGFX": "ON",
        "BUILD_FEM": "ON",
        "FREECAD_USE_EXTERNAL_SMESH": "ON",
        "BUILD_FEM_NETGEN": "ON",
        "BUILD_WEB": "OFF",
        "FREECAD_USE_PCL": "ON",
        "ENABLE_DEVELOPER_TESTS": "ON"
      }
    }
  ]
}
```

FEM and PCL were `OFF` in this preset until 2026-09-08 and `BUILD_WEB`
still is -- there is no QtWebEngine for osx-64. Remember that a preset
edit does not reach a tree that already exists (CLAUDE.md): force the
four flags on the command line, or delete the tree.

Configure found everything first time: OCC 8.0.1, Coin3D 4.0.6, Qt/PySide6
6.11.1, Boost 1.92 (1.90 since FEM), Python 3.12.14. `Looking for GL/gl.h - not found` is
expected and harmless -- macOS spells it `OpenGL/gl.h`.

### What macOS needed in source, and what it did not

Four fixes, all committed, and three of them one defect wearing three hats:
**libc++ does not include transitively where libstdc++ does**, so headers the
Linux build never had to name are missing. That class of bug cannot be found on
Linux at all.

| Fix | What |
|---|---|
| `occt` `NCollection_IncAllocator.cxx`, `Aspect_VKeySet.cxx` | `<mutex>` for `std::lock_guard`; the headers include `<shared_mutex>`, which declares `shared_mutex` and `shared_lock` but not `lock_guard` |
| `imgui-node-editor` `crude_json.cpp` | `<exception>` for `std::terminate` |
| `src/Gui/Renderer/BGFXRendererP.h` | the `BX_PLATFORM_OSX` branch called `get_nswindow_from_nsview()`, **defined nowhere** -- it had never been compiled. bgfx's Metal backend sorts out NSView/NSWindow/CAMetalLayer itself, and Qt's `winId()` is an NSView*, so it is passed straight through |
| `src/Gui/Renderer/CMakeLists.txt` | link `vg-renderer` before `example-common`: both vendor fontstash, and Apple's `ld` errors on the 32 duplicate symbols where GNU ld silently takes the first |

The OCCT patch joins the fork-local list at the top of this document.

A fifth arrived on 2026-09-07, from running the GUI rather than from building
it: `src/Gui/MacSymbolIconCompat.mm`. Qt 6.7 and later draw
`QStyle::standardIcon()` on a Mac as an SF Symbol, and the engine applies
`+[NSImageSymbolConfiguration configurationPreferringMonochrome]` -- macOS 13
API -- with no availability guard, so on macOS 12 every paint of a toolbar's
overflow button threw an Objective-C exception. The file adds the method at
image load when the OS lacks it. Symptom and verification: `Testing.md`,
"Toolbar paints threw on macOS 12".

**What did not need touching**: the Beast/Asio transport, exactly as predicted.
macOS is a BSD socket platform, `v6_only(false)` is honoured and `::1` is
present, and `SceneServerWire_tests_run` passes 17 of 17 with `listensOnIPv6Too`
running rather than skipping. See `SceneServerPort.md` section 7.5.

### Build

Everything at `-j 4` on this box (4 cores, 8 GB; heavier parallelism swaps).

```sh
RUN=~/works/sw/fcad/.conda/run.sh
$RUN cmake --preset mac-relwithdebinfo-801
$RUN cmake --build build/mac-relwithdebinfo-801 -j 4 \
    --target SceneServerWire_tests_run PublishOnly_tests_run
```

Use `-- -k 0` on a first build after a change of toolchain: ninja then collects
every error in one pass instead of stopping at the first, which on a
2500-target dependency is the difference between one cycle and ten.

## Regenerating the bundled material icons

Two scripts draw the icons that ship in `MatGui`. Both write into
`src/Mod/Material/Gui/Resources/icons/materials`, and both rewrite their own block of
`Material.qrc` between a pair of marker comments. The workflow is to run one and
commit whatever changed.

| Script | Draws | Needs |
|---|---|---|
| `scripts/material-icons.py` | `Look_<digest>.png`, one per distinct appearance | FreeCAD and a renderer (`MatGui.renderMaterialIcon`) |
| `scripts/pattern-icons.py` | `Pattern_<name>.png`, one per hatch card | only PySide -- it reads the cards and draws with QPainter |

```bat
.conda\run.cmd python scripts\pattern-icons.py
```

The file name is not decoration, it is how the icon is found again. Each generator's
naming function has a C++ counterpart in `MaterialIcons` -- `resource_name()` against
`patternResourceName()`, `shared_name()` against `sharedResourceName()` -- and the
two must agree exactly, because the C++ side resolves one name at a time and never
sees the whole set. Change one and you have to change the other.

**A generated name has to be unique case-INSENSITIVELY.** Pattern names come from the
cards, and the bundle legitimately holds two that differ only in case: the PAT
`Square`, a line definition `DrawGeomHatch` turns into real geometry, and the SVG
`square`, a tile `DrawHatch` fills with. They coexist upstream because they sit in
different libraries. Flattened into one icons directory with the case preserved they
became `Pattern_Square.png` and `Pattern_square.png` -- two tracked paths differing
only in case, which is fine on Linux and is ONE file on Windows and on a default
macOS checkout. Two things follow, and neither announces itself:

- git materialises whichever it writes last, so the other path reports permanently
  modified and the tree can never be clean;
- Windows keeps a file's existing casing when it overwrites, so drawing `square`
  wrote into the `Pattern_Square.png` already sitting there, and the stale sweep --
  which lists the directory and drops whatever no card claims -- then deleted it as
  an unclaimed name. That run shipped 31 swatches for 32 cards, and reported 32.

A name carrying any uppercase therefore takes a six-hex sha1 of itself
(`Pattern_Square-82810c.png`), which case folding cannot collapse. Keep that property
for any new generated icon set. To check the whole tree for the general fault:

```bash
git ls-files | awk '{l=tolower($0); if (l in s) print s[l], "<->", $0; s[l]=$0}'
```

**`Material.qrc` is `text` in `.gitattributes`**, so git stores it with LF and checks
it out with the platform's ending -- CRLF on Windows. Both generators match their
block markers with the file's own ending for that reason. Matching a bare newline
finds nothing there, and the "first run has no block yet" branch then appends a
second block instead of replacing the first, leaving the stale names in the qrc
beside the new ones. The scripts still report the count they drew, which is right --
it is the qrc that ends up with twice as many entries as there are icons.

## Porting state / caveats

The first builds required a Qt5→Qt6 port (~30 files: QRegExp, QTextCodec,
QDesktopWidget, QtPlatformHeaders→QNativeInterface in `BGFXRenderer.cpp`,
OpenGLWidgets linkage in `SetupQt.cmake`, …) plus a Qt 6.10 / gcc 15 / boost 1.85 /
eigen 5 / shiboken 6.10 round (`copy_options`, `QIcon` forward decl,
`QGenericReturnArgument` removal, `PythonWrapper.cpp` TypeInitStruct shim,
`SetupEigen.cmake` CONFIG-first, `src/CMakeLists.txt` gates the `boost_fix` overlay to
boost < 1.85). **All committed and pushed on the `LinkVibe` branches** (fcad + occt).

Open items:
- Verify Path workbench Area/Voronoi behavior with vanilla boost ≥ 1.85 geometry
  (the `boost_fix` header overlay that used to patch it is disabled there).
- The freecad-rt and pivy feedstocks default to Qt6 (qt6-main/pyside6, jinja `qt`
  variable); the freecad-rt `LinkVibe` branch builds directly from the FreeCAD
  `LinkVibe` git branch (`git_rev`), so a Qt6 conda image can be built now. `main`
  still builds from a pinned tag that predates the Qt6 port.
- `mcp` was added to the freecad-rt-feedstock run deps (needed by the mcp_console).
