# Development Environment

How the development boxes are set up to build and debug the realthunder FreeCAD fork
together with its companion forks.

- **Linux (Ubuntu 24.04 / WSL2)** — two parallel stacks; **the conda stack is the
  primary one** because it is the only way to get a matched Qt6 + PySide6 on
  Ubuntu 24.04. This is what the bulk of this document describes.
- **Windows 10 + MSVC 2022** — [Windows stack](#windows-stack-msvc-2022--conda).
  Same conda approach, but the debug/release CRT split forces every component to be
  a non-Debug configuration, and a dozen Windows-only source fixes are required.

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

Fork-local patches, now committed on their `LinkVibe` branches (don't discard):
- `pivy/interfaces/CMakeLists.txt` — `INSTALL_RPATH` extended with `${CMAKE_INSTALL_RPATH}`
  so `_coin.so` finds our locally-built libCoin without `LD_LIBRARY_PATH`
  (pivy commit on `rt-0.6.10`; also shipped as a pivy-feedstock patch).
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

**OCCT is built from `LinkVibe-801` (OCCT 8.0.1) into `install/conda-debug-801`** —
that is what FreeCAD links. The 7.7.2 build below (`LinkVibe` → `install/conda-debug`)
is kept only to compile-check the version-guarded fallback paths; see
[Building FreeCAD](#building-freecad-conda-stack). Swap branch and `INSTALL_DIR`
to build either.

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
the repo's `conda-linux-debug` preset and overrides: build dir
`build/conda-debug-occt801`, `CMAKE_PREFIX_PATH`/`OCC_INCLUDE_DIR` pointing at the
local `install/conda-debug-801` (OCCT) and `install/conda-debug` (Coin) prefixes,
`CMAKE_POLICY_VERSION_MINIMUM=3.5` (for bgfx's old cmake_minimum_required
under cmake 4), `BUILD_BGFX=ON`, and `BUILD_FEM/BUILD_WEB/FREECAD_USE_PCL/`
`FREECAD_USE_EXTERNAL_SMESH/ENABLE_DEVELOPER_TESTS` OFF (avoids VTK/netgen/WebEngine/
PCL/smesh packages; enable selectively when needed — conda-forge now has qt6-webengine).

```sh
RUN=~/works/sw/fcad/.conda/run.sh
cd ~/works/sw/fcad
$RUN cmake --preset conda-debug-local
$RUN cmake --build build/conda-debug-occt801   # ninja, add -j N to limit parallelism
```

Sources build against both OCCT versions (`OCC_VERSION_HEX` guards; features that
need the 8.0.1 fork — parallel healing, streamed STEP transfer — fall back to the
one-shot path on 7.7.2). The `conda-debug-occt772` preset builds the same tree
against `install/conda-debug` in `build/conda-debug` to keep that compile-checked;
run it after touching anything version-guarded:

```sh
$RUN cmake --preset conda-debug-occt772
$RUN cmake --build build/conda-debug --target Import ImportGui
```

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
$RUN ~/works/sw/fcad/build/conda-debug-occt801/bin/FreeCAD     # GUI (WSLg)
$RUN ~/works/sw/fcad/build/conda-debug-occt801/bin/FreeCADCmd  # headless
$RUN gdb --args ~/works/sw/fcad/build/conda-debug-occt801/bin/FreeCADCmd script.py
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
$RUN build/conda-debug-occt801/bin/FreeCADCmd /path/to/smoke.py
# GUI + PySide6: launch and confirm no "No module named 'PySide6'" in output,
# Draft/Arch/Assembly/AddonManager appear in the workbench selector
```

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

Kept intact and working, but **PySide6 is impossible here** (see above) — Python
workbenches don't load. Useful as a second opinion against a very different
Qt/compiler generation.

- apt deps: qt6-{base,base-private,svg,tools}-dev, qt6-tools-dev-tools, qt6-l10n-tools,
  libxerces-c-dev, libeigen3-dev, boost dev libs incl. libboost-python-dev,
  libyaml-cpp-dev, libfreeimage-dev, rapidjson-dev, GL/X11 dev, swig, cmake, ninja.
- Builds: `<repo>/build_debug` → `<repo>/install/debug` (OCCT 7.7.2 here, configured with
  `-DCMAKE_INSTALL_RPATH='$ORIGIN'` — same transitivity reason as above).
- fcad preset: `debug-local` (inherits `debug`, Makefiles, `build/debug`,
  `FREECAD_QT_VERSION=6`, BUILD_BGFX=ON, BUILD_FEM/WEB=OFF). `sh src/make.sh -j8` works.
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
Paths below are from the first Windows box — adjust the root, keep the structure.

| What | Path |
|---|---|
| miniforge | `D:\Zheng.Lei\sw\miniforge3` |
| conda env | `D:\Zheng.Lei\sw\fcad\.conda\freecad` |
| FreeCAD fork | `D:\Zheng.Lei\sw\fcad` (branch `LinkVibe`) |
| OCCT fork | `D:\Zheng.Lei\sw\occt` (branch `LinkVibe-801`) |
| Coin fork | `D:\Zheng.Lei\sw\coin` (branch `LinkVibe`) |
| OCCT install | `D:\Zheng.Lei\sw\occt\install\win-relwithdebinfo-801` |
| Coin install | `D:\Zheng.Lei\sw\install\coin-win-relwithdebinfo` |
| FreeCAD build | `D:\Zheng.Lei\sw\fcad\build\win-relwithdebinfo-801` |
| dev shell | `D:\Zheng.Lei\sw\fcad\.conda\run.cmd` |

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
- **Miniforge** (not Miniconda — see the channel note below). Installs unattended with
  `Miniforge3-Windows-x86_64.exe /InstallationType=JustMe /RegisterPython=0 /AddToPath=0
  /S /D=<prefix>` (`/D` last, unquoted).
- Git for Windows, and optionally the gh CLI (the winget MSI needs elevation; the
  portable zip from the GitHub releases page needs none). Set `user.name`/`user.email`
  before the first commit — a fresh box has neither, and the failure only surfaces at
  `git commit`.
- **`git submodule update --init --recursive` right after cloning.** A plain clone has
  empty `src/3rdParty/bgfx` and `src/3rdParty/OndselSolver`, and configure fails on both
  ("does not contain a CMakeLists.txt file"). bgfx pulls three nested submodules of its
  own (bgfx, bimg, bx).
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
conda create -y -p D:\Zheng.Lei\sw\fcad\.conda\freecad ^
  --override-channels -c https://prefix.dev/conda-forge ^
  python=3.12 qt6-main=6.10.1 pyside6=6.10.1 ^
  cmake ninja swig pkg-config ^
  libboost-devel=1.85 eigen xerces-c zlib yaml-cpp rapidjson freeimage freetype expat ^
  fmt pybind11 numpy matplotlib-base
```

Pin `qt6-main`, `pyside6` and the three `libboost*` packages in
`<env>\conda-meta\pinned` so a later `conda install` cannot bump them.

**Boost must be 1.85, not current.** `SetupBoost.cmake` requires the `system`
component, but Boost.System has been header-only since 1.69 and 1.91 ships no
`boost_system` library and no CMake config for it, so `find_package` fails outright.
1.85 also matches the Linux box.

**PySide6 quirk, the Windows form of the one the Linux stack has.**
`PySide6Config.cmake` computes `PACKAGE_PREFIX_DIR` as `<env>\Library` and then
`set_and_check`s `Library\typesystems` and `Library\glue`, which conda installs under
`Library\share\PySide6\`. Create **directory junctions** (`mklink /J`) — junctions
need neither admin rights nor developer mode, unlike symlinks:

```bat
mklink /J "<env>\Library\typesystems" "<env>\Library\share\PySide6\typesystems"
mklink /J "<env>\Library\glue"        "<env>\Library\share\PySide6\glue"
```

Redo them after any pyside6 reinstall.

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

Every path here has a dot in it (`Zheng.Lei`, `.conda`), as do version-valued
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
| `BUILD_FEM/BUILD_WEB/FREECAD_USE_PCL/FREECAD_USE_EXTERNAL_SMESH/ENABLE_DEVELOPER_TESTS=OFF` | same trims as the Linux local preset |

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

Note also that `BGFX_BUILD_TOOLS_SHADER=ON` drags in **tint/Dawn** from bgfx's
3rdparty tree — hundreds of heavy C++ TUs that dwarf FreeCAD's own code. It is needed
to compile shaders (`ninja Renderer_assets`), but it is the single largest
contributor to a cold Windows build.

### Running the C++ (GoogleTest) suites

`ENABLE_DEVELOPER_TESTS` is **OFF** in this build dir, as in the Linux presets, so
`tests/` is not configured at all and `ninja Tests_run` answers *unknown target*.
Turning it on costs one configure and no rebuild of what is already there:

```cmd
run.cmd cmake -S . -B build\win-relwithdebinfo-801 -DENABLE_DEVELOPER_TESTS=ON
run.cmd cmake --build build\win-relwithdebinfo-801 --target <suite> -j 4
```

googletest is vendored (`tests/lib`), so nothing is fetched. Two things to know:

- **The shared `Tests_run` suite does not link here**, for reasons that have
  nothing to do with whatever you are testing: `tests/src/Base/Reader.cpp` names
  `xercesc_3_2` while the conda env ships 3.3, and `tests/src/App/Expression.cpp`
  uses `UnitExpression`/`OperatorExpression::UNIT` as they no longer are. Build a
  focused executable instead (`DeferredLoad_tests_run`, `RestoreDrain_tests_run`,
  …) — that is part of why those exist. `-- -k 0` gets ninja past the two broken
  translation units if you only want a compile check of your own.
- **The test exes need `bin` on `PATH`.** They are built into
  `build\...\tests\src\App\`, not next to `FreeCADApp.dll`, and `run.cmd` does not
  add the build's `bin` (it adds the dependency prefixes). Without it the process
  dies before `main()` with no output at all:

  ```cmd
  set PATH=D:\Zheng.Lei\sw\fcad\build\win-relwithdebinfo-801\bin;%PATH%
  run.cmd build\win-relwithdebinfo-801\tests\src\App\RestoreDrain_tests_run.exe
  ```

Put `ENABLE_DEVELOPER_TESTS` back to `OFF` afterwards, or a plain
`cmake --build` of everything fails on those same two files.

### Building pivy

Draft and Arch import `pivy.coin` at load time, so without pivy those workbenches
fail to register. There is no pivy source checkout in the layout table by default —
clone one beside the others:

```bat
git clone --depth 1 --branch 0.6.10 https://github.com/coin3d/pivy.git D:\Zheng.Lei\sw\pivy
```

0.6.10 is the version `pivy-feedstock` packages. The feedstock's two patches do not
both apply here: `extend_install_rpath.patch` is meaningless on Windows, while
`windows_cmake_install_path_fix.patch` (upstream `fc622b3b`, one
`file(TO_CMAKE_PATH ...)` on `PIVY_Python_SITEARCH`) **is** needed — `Python_SITEARCH`
comes back with backslashes and the `install(DESTINATION)` that consumes it is not
path-normalised. Apply it to the checkout.

```bat
.conda\run.cmd cmake -G Ninja -B D:\Zheng.Lei\sw\pivy\build\win-relwithdebinfo ^
    -S D:\Zheng.Lei\sw\pivy ^
    -D CMAKE_BUILD_TYPE=RelWithDebInfo ^
    -D CMAKE_PREFIX_PATH=D:/Zheng.Lei/sw/install/coin-win-relwithdebinfo ^
    -D CMAKE_MODULE_LINKER_FLAGS=/LIBPATH:D:/Zheng.Lei/sw/fcad/.conda/freecad/libs ^
    -D DISABLE_SWIG_WARNINGS=ON
.conda\run.cmd cmake --build   D:\Zheng.Lei\sw\pivy\build\win-relwithdebinfo
.conda\run.cmd cmake --install D:\Zheng.Lei\sw\pivy\build\win-relwithdebinfo
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
(`D:\Zheng.Lei\sw\tools\dbg\`) and run it from there. Worth doing regardless of the
GUI: `cdb` takes a command file, which is what makes debugging scriptable from a
non-interactive shell.

```bat
:: dbg.txt:  sxe ld:FreeCADApp / g / .reload /f FreeCADApp.dll / lm vm FreeCADApp / k / q
.conda\run.cmd D:\Zheng.Lei\sw\tools\dbg\cdb.exe -cf dbg.txt ^
    build\win-relwithdebinfo-801\bin\FreeCADCmd.exe script.py
```

**Launch through `run.cmd`.** A debugger started outside it hands the child no
OCCT/Coin `PATH`, and the process dies at load with a bare `0xc0000135` before any of
this matters — the same problem the two sections above describe, arriving through a
new door.

**Arming crash dumps: two traps that make the arming silently do nothing.** The
GUI run keeps a standing arm file (`D:\Zheng.Lei\sw\tools\dbg\arm_freecad.cmd`,
passed as `-cf`, ending in `g`) that dumps on the usual exception filters. Both
of these were live defects, found 2026-08-12 when an access violation left no
dump and no stack:

- **cdb strips backslashes inside a quoted `-c`/`-c2` command string.** A dump
  path written `D:\Zheng.Lei\sw\tools\dbg\dumps\fcad.dmp` is stored as
  `D:Zheng.Leisw\toolsdbgdumpsfcad.dmp`, and no dump is ever written. Doubling
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

      sxe -c ".exr -1;r;kv 100;.dump /ma /u D:/Zheng.Lei/sw/tools/dbg/dumps/fcad_av.dmp;gn" av

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
