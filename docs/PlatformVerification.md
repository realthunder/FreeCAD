# Stage 5 of the scene server port: verify on Windows and macOS

This is a brief for a Claude session on a Windows or a macOS box. It says
what stage 5 of `SceneServerPort.md` has to establish, how to establish it,
what to record, and -- for macOS, where nothing is set up -- how to get from
a blank machine to a build that can answer the question. Read `CLAUDE.md`
first: it carries the rules that hold on every box (ASCII only in anything
you write, `Area:` commit prefixes, one problem per commit, never push
unless told in the conversation you are in, ask before an ABI break). The
box you are on has none of the Linux session's memory; everything it knew
that matters here is in this file or in the documents it names.

Related: `SceneServerPort.md` (the port, stages 0 to 4 as built on Linux),
`DevEnvironment.md` (the Linux and Windows stacks in full), `Testing.md`
(how the suites run, what is skipped and why).

## 1. What stage 5 is

Before the port, the serving tier (`src/Gui/Renderer/SceneServer.cpp`,
`Render::SceneStreamServer`) was POSIX-only: on Windows it compiled as a
stub that returned false, and on macOS it most likely did not compile at
all (`MSG_NOSIGNAL`, a Linux extension). Stage 2 replaced the transport
with Boost.Beast on Asio and deleted the platform guard; stages 3 and 4
built on that. All of it was written, built and tested on one Linux box.
The claim that Windows and macOS build the same source and pass the same
suite has never been checked, because no such box was available and no CI
in this repository builds either platform.

Stage 5 is that check. It is verification, not construction: if the code
is right, the stage is a green suite and a paragraph in the doc; if it is
not, the stage is whatever fixes it needs, each as its own commit.

### 1.1 The commits you need

The stage 4 code is commit `3c4cdabbc6` ("Gui: the scene server's
cloud-readiness items (SceneServerPort stage 4)") on `LinkVibe`, on top of
`764794fcf4` (the headless selection echo) and fourteen earlier unpushed
commits. **If `git log --oneline -20` on your box does not show
`3c4cdabbc6`, stop and ask for the branch to be pushed.** Do not try to
reconstruct the change from this document.

## 2. What to establish

In order. Each item is a fact to write down, not a box to tick.

1. **The renderer library and the wire suite compile.** `FreeCADRenderer`
   (`src/Gui/Renderer/`) and `SceneServerWire_tests_run`
   (`tests/src/Gui/SceneServerWire.cpp`). The one platform-specific line
   left in the server is the Asio-first include order at the top of
   `SceneServer.cpp`, which Boost documents as a Windows requirement and
   which has never been compiled on Windows. On macOS the whole Beast
   instantiation is untried.
2. **The wire suite passes: 17 cases.** It starts a listener on a free port
   and drives it over real sockets with a Beast client. Two cases carry
   platform meaning:
   - `listensOnIPv6Too` connects on `::1`. It skips itself
     (`GTEST_SKIP`) only when the host cannot bind `::1` at all; on a box
     with IPv6 loopback it must run and pass. Windows and macOS both have
     `::1`, so a skip there is a finding, not a pass.
   - `handshakeThenHelloThenSnapshot` expects the peer as `127.0.0.1:` --
     that is the v4-mapped normalisation of the dual-stack listener
     (stage 4 item 1) being checked on your platform. If the roster shows
     `::ffff:127.0.0.1`, Asio's `is_v4_mapped` path differs there.
   The other stage 4 cases (`theCapCountsUsersNotAddresses`,
   `aChunkedPostBodyIsRead`, `anOversizeControlFrameEndsTheConnection`)
   are pure protocol and should behave identically; if one does not, the
   difference is Beast's, and worth writing down exactly.
3. ~~**`PublishOnly_tests_run` (macOS only, see 4.6).**~~ **Done
   2026-09-06.** Its last case read `/proc/self/maps` and skipped on
   macOS; it now enumerates the loaded images through
   `_dyld_image_count()` / `_dyld_get_image_name()`, and the suite is 5
   of 5 there. What it refuses on macOS is not what this brief guessed --
   `SceneServerPort.md` 7.6.
4. **The whole tree, if it builds.** The full `ctest` (Windows: 472 of 472
   as of 2026-09-04, in `Testing.md`; Linux: 485 of 485 as of 2026-09-06).
   Not required for the stage; required before calling the platform
   "green".
5. **The headless echo test, by hand.** `tests/gui/serve-selection-echo.py`
   is registered in ctest only where `xvfb-run` exists, so it does not
   register on Windows or macOS. It needs no display of its own -- the
   document is hidden and the serve is headless -- so run it directly:

   ```sh
   mkdir -p /tmp/gt-echo
   GT_OUT=/tmp/gt-echo GT_RESULT=/tmp/gt-echo/result.txt \
       <FreeCAD binary> tests/gui/serve-selection-echo.py
   cat /tmp/gt-echo/result.txt     # eight PASS lines and DONE
   ```

   (On Windows, `set GT_OUT=...` and `set GT_RESULT=...` before the
   `run.cmd` line.) The `echo ms` figures are the click-to-selection-delta
   numbers of `ThinClient.md` section 8.1 on your platform; note them.

### 2.1 What to record

Add a section **7.5 "Stage 5, as verified"** to `SceneServerPort.md` with
one row per platform: OS and version, compiler, Boost version, the wire
suite result (17 of 17, or which case and why), whether `listensOnIPv6Too`
ran or skipped, the full ctest count if run, and the echo test's numbers.
Strike the stage 5 bullet in section 7 and item 1 of section 8 when both
platforms are in. Add the platform's C++ count to `Testing.md` where the
Windows count already sits. Anything that had to change in source is its
own `Gui:` commit with the failure it fixed in the message; the doc rows
are a `Docs:` commit.

## 3. Windows

**Done 2026-09-06.** The result is `SceneServerPort.md` section 7.5: the
suite is 17 of 17, `listensOnIPv6Too` ran, and the only source change the
platform asked for was `_WIN32_WINNT` before the Asio include. What
follows is the recipe it used, kept for the next Windows session.

The box is set up: `DevEnvironment.md`, "Windows stack (MSVC 2022 +
conda)", is the reference, and `Testing.md`, "C++ on Windows", is
authoritative for the suites. In short: preset `win-relwithdebinfo-local`
(local `CMakeUserPresets.json`), tree `build\win-relwithdebinfo-801`,
everything RelWithDebInfo because conda's Qt is on the release CRT, and
`ENABLE_DEVELOPER_TESTS` already ON there.

```cmd
git pull                           :: must land 3c4cdabbc6 (section 1.1)
.conda\run.cmd cmake --build build\win-relwithdebinfo-801 -- -j 6
.conda\run.cmd build\win-relwithdebinfo-801\tests\src\Gui\SceneServerWire_tests_run.exe
D:\works\sw\tools\ctest-fcad.cmd -j 6
```

The first build after the pull reconfigures by itself: the pull adds
`tests/gui/serve-selection-echo.py` to `tests/gui/CMakeLists.txt`, and
that file's guard (`xvfb-run`) keeps the GUI tests unregistered on
Windows, which is expected. `ninja -n` reporting ~44 pending targets after
a build is the always-dirty `version_check`, not a failure.

Things to watch, none of them known to bite yet:

- `SceneServerWire_tests_run` links `ws2_32` and `mswsock` on Windows
  (`tests/src/Gui/CMakeLists.txt`); the library itself links nothing
  socket-specific because Asio pulls Winsock through its headers. An
  unresolved `WSA*` symbol at link time means the library needs the same
  two libs on its own link line.
- `v6_only(false)` on the acceptor is supported since Vista. The listener
  falls back to a v4 socket if opening the v6 one fails, so a Windows box
  with IPv6 disabled at the adapter passes the suite with the IPv6 case
  skipped -- and then the skip is the environment's, and the row in 7.5
  should say so.
- `anOversizeControlFrameEndsTheConnection` writes eight header bytes and
  126 payload bytes underneath Beast with `net::write` on the raw socket.
  Nothing Windows-specific, but it is the one case that bypasses Beast's
  client, so if the suite hangs there rather than failing, the server did
  not close on the protocol error.

## 4. macOS, from a blank machine

Nothing has ever been built on macOS in this fork. The feedstock
(`realthunder/freecad-rt-feedstock`, `recipe/build.sh`) has a Darwin
branch, so upstream FreeCAD on conda-forge's stack is known to build there;
this fork adds bgfx, the Coin and OCCT forks, and the renderer, none of
which has. Budget the bring-up as a day, and the stage 5 answer as an
afternoon after it.

The shape is the Linux conda stack (`DevEnvironment.md`, "Primary stack:
conda") with the substitutions below. Where this document and that one
disagree about a macOS detail, this one is the one written for macOS; where
this one is silent, that one is the reference. Every command is meant to
be run from a plain shell, not from inside an activated env -- the
`run.sh` wrapper activates per command, as on Linux.

### 4.1 Prerequisites

- **Xcode Command Line Tools**: `xcode-select --install` (the compiler in
  the env is conda's clang, but the SDK and `ld` come from here). Check
  with `xcrun --show-sdk-path`.
- **Miniforge** at `~/miniforge3` (the path `run.sh` assumes). Pick the
  installer for `uname -m`: `arm64` on Apple silicon, `x86_64` on Intel.
  Not Miniconda: the Windows section of `DevEnvironment.md` explains the
  channel reason, and it holds here.
- **git**; `gh` optional. Set `user.name` and `user.email` before the
  first commit.
- **A case-sensitive filesystem is not required, but the default is
  case-insensitive**, and two things follow (both already known from
  Windows): Coin cannot install into `<coin repo>/install/` because the
  source tree has a file named `INSTALL` (use an out-of-tree prefix, as
  in 4.4), and if `git status` shows a permanently modified icon file
  right after the clone, that is two tracked paths differing only in case
  (`DevEnvironment.md`, "Regenerating the bundled material icons"; it
  was fixed by renaming, so an appearance of it is news).

### 4.2 Repositories

Siblings under `~/works/sw/`, the branches `CLAUDE.md` names:

```sh
mkdir -p ~/works/sw && cd ~/works/sw
git clone -b LinkVibe     git@github.com:realthunder/FreeCAD.git fcad
git clone -b LinkVibe-801 git@github.com:realthunder/OCCT.git    occt
git clone -b LinkVibe     git@github.com:realthunder/coin.git    coin
cd fcad
# Every submodule except cycles (the path tracer is BUILD_CYCLES=OFF and
# large). bgfx has three nested ones of its own; vg-renderer is easy to
# miss and configure fails without it.
git submodule update --init --recursive \
    $(git submodule status | awk '{print $2}' | grep -v cycles)
scripts/install-hooks.sh          # the ASCII post-commit hook, once per clone
git -C ../occt checkout LinkVibe-801   # 8.0.1 is the only target; LinkVibe is frozen 7.7.2
```

If `3c4cdabbc6` is not in `git log`, see 1.1.

### 4.3 The conda env

The Linux create line, with the macOS compiler packages in place of the
Linux ones and the X11/GL packages dropped (macOS has neither X11 nor a
GL to install; Qt uses Cocoa and Metal, bgfx uses Metal). On Intel use
`clang_osx-64 clangxx_osx-64`.

```sh
~/miniforge3/bin/mamba create -y -p ~/works/sw/fcad/.conda/freecad \
  clang_osx-arm64 clangxx_osx-arm64 cmake ninja make swig pkg-config \
  qt6-main=6.11.1 pyside6=6.11.1 \
  python=3.12 libboost-devel eigen xerces-c zlib yaml-cpp rapidjson freeimage freetype \
  expat fmt pybind11 numpy matplotlib-base lark
printf 'qt6-main ==6.11.1\npyside6 ==6.11.1\npython ==3.12.*\n' \
  > ~/works/sw/fcad/.conda/freecad/conda-meta/pinned
cd ~/works/sw/fcad/.conda/freecad
ln -sfn share/PySide6/typesystems typesystems    # the pyside6 CMake-config quirk, as on Linux
ln -sfn share/PySide6/glue glue
```

`lark` is a build-time need of `src/Mod/BIM` that fails thousands of
objects in when missing (Windows found it). `libarea` comes from the
`realthunder` channel, which has built it for osx since 2026-08-25:

```sh
~/miniforge3/bin/conda install -p ~/works/sw/fcad/.conda/freecad -c realthunder libarea
```

If no osx build of it is there, configure with `-DBUILD_AREA=OFF`; CAM and
BIM go with it (`CheckInterModuleDependencies.cmake`), which is fine for
stage 5. **FEM stays OFF on macOS for this stage** (`BUILD_FEM=OFF`,
`FREECAD_USE_EXTERNAL_SMESH=OFF`): the external SMESH is a Linux package
of ours, and FEM is not what stage 5 asks about. The same for
`FREECAD_USE_PCL=OFF` and `BUILD_WEB=OFF`, as on every local preset.

`run.sh` is the Linux one, unchanged -- `~/miniforge3` is Miniforge's
default on macOS too:

```sh
cp ~/works/sw/fcad/.conda/run.sh /tmp/run.sh.bak 2>/dev/null   # it is gitignored; recreate if absent
cat > ~/works/sw/fcad/.conda/run.sh <<'EOF'
#!/bin/bash
set -e
source "$HOME/miniforge3/etc/profile.d/conda.sh"
conda activate "$HOME/works/sw/fcad/.conda/freecad"
export CFLAGS="${CFLAGS//-O2/} "
export CXXFLAGS="${CXXFLAGS//-O2/} "
export CFLAGS="${CFLAGS//-DNDEBUG/}"
export CXXFLAGS="${CXXFLAGS//-DNDEBUG/}"
exec "$@"
EOF
chmod +x ~/works/sw/fcad/.conda/run.sh
```

The strip of `-O2` only matters for a Debug tree; the RelWithDebInfo
builds below set their own optimisation.

*** **Corrections, from the box that ran this (2026-09-06).** Three things
above are wrong as written, and are right in `DevEnvironment.md`, "macOS
stack":
- **Qt 6.11.2 does not install on macOS 12** -- its `moltenvk` wants
  `__osx >=14.0`. The create line and the pin now say 6.11.1. A macOS 14+
  box can and should use 6.11.2.
- **conda's activation exports no `MACOSX_DEPLOYMENT_TARGET`,
  `CONDA_BUILD_SYSROOT` or `CMAKE_OSX_SYSROOT`** with these compiler
  packages. There is nothing to leave; the SDK is found through `xcrun`.
- **`run.sh` must `unset CPATH CPLUS_INCLUDE_PATH C_INCLUDE_PATH
  OBJC_INCLUDE_PATH LIBRARY_PATH`.** `xcrun` exports
  `CPATH=/usr/local/include`, and `/usr/bin/python3` is an xcrun shim, so
  any build launched through one inherits it. `CPATH` is searched as if
  `-I` and therefore beats every `-isystem`, which is how CMake passes Qt6
  and Boost: Homebrew's Qt5 then shadows conda's Qt6 (loudly) and
  Homebrew's Boost shadows conda's (silently, an ABI mismatch).

### 4.4 OCCT and Coin

The Linux recipes with `RelWithDebInfo`, `@loader_path` where Linux has
`$ORIGIN` (OCCT installs libraries with no rpath otherwise, and rpath is
not transitive), and Coin out of tree:

```sh
RUN=~/works/sw/fcad/.conda/run.sh

$RUN cmake -S ~/works/sw/occt -B ~/works/sw/occt/build_conda_rwdi_801 -G Ninja \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DINSTALL_DIR=$HOME/works/sw/occt/install/conda-relwithdebinfo-801 \
  -DCMAKE_INSTALL_RPATH='@loader_path' \
  -DBUILD_LIBRARY_TYPE=Shared -DBUILD_MODULE_Draw=OFF \
  -DUSE_TBB=OFF -DUSE_VTK=OFF -DUSE_DRACO=OFF \
  -DUSE_FREETYPE=ON -DUSE_FREEIMAGE=ON -DUSE_RAPIDJSON=ON \
  -DBUILD_RELEASE_DISABLE_EXCEPTIONS=OFF
$RUN cmake --build ~/works/sw/occt/build_conda_rwdi_801 \
  && $RUN cmake --install ~/works/sw/occt/build_conda_rwdi_801

$RUN cmake -S ~/works/sw/coin -B ~/works/sw/coin/build_conda_rwdi -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=$HOME/works/sw/install/coin-mac-relwithdebinfo \
  -DUSE_EXTERNAL_EXPAT=ON -DSIMAGE_RUNTIME_LINKING=ON \
  -DCOIN_BUILD_TESTS=OFF -DCOIN_BUILD_DOCUMENTATION=OFF
$RUN cmake --build ~/works/sw/coin/build_conda_rwdi \
  && $RUN cmake --install ~/works/sw/coin/build_conda_rwdi
```

Two OCCT traps from Windows apply verbatim: `INSTALL_DIR` is read on the
first configure only (a new value later means a fresh build tree), and if
OCCT does not find freetype/freeimage/rapidjson through conda's include
wiring, `-D3RDPARTY_DIR=$HOME/works/sw/fcad/.conda/freecad` points it at
them. The one fork-local patch OCCT carries (`StdPrs_BRepFont.cxx`,
freetype's `tags` type) is on the branch already.

pivy is not needed for stage 5. If the full GUI is wanted later, its
recipe is in `DevEnvironment.md`, "Building the dependencies", with
`CMAKE_PREFIX_PATH` at the Coin prefix above.

### 4.5 FreeCAD

A local preset in the gitignored `CMakeUserPresets.json`. Two things
differ from the Linux one beyond paths: the repo's `conda-macos` preset
sets `cmakeExecutable` to `conda/cmake.sh`, which runs `mamba run -n
freecad cmake` -- a *named* env, which this stack is not -- so the user
preset overrides it with the env's own cmake; and the feedstock's Darwin
branch adds three compiler flags that upstream needed on macOS
(`_LIBCPP_DISABLE_AVAILABILITY`, `BOOST_NO_CXX98_FUNCTION_BASE`, and
`-Wno-enum-constexpr-conversion` for boost's numeric_conversion), carried
here through `CMAKE_CXX_FLAGS`.

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
        "BUILD_FEM": "OFF",
        "FREECAD_USE_EXTERNAL_SMESH": "OFF",
        "BUILD_WEB": "OFF",
        "FREECAD_USE_PCL": "OFF",
        "ENABLE_DEVELOPER_TESTS": "ON"
      }
    }
  ]
}
```

`OCCT_CMAKE_FALLBACK=OFF` is the Windows lesson: the repo's `conda`
preset turns the fallback ON, and the fallback's hand-rolled search does
not know local OCCT layouts, so configure succeeds and the link fails
thousands of targets later. Our OCCT ships `OpenCASCADEConfig.cmake`; use
it.

```sh
cd ~/works/sw/fcad
$RUN cmake --preset mac-relwithdebinfo-801
# The stage 5 verdict first: two targets, minutes not hours.
$RUN cmake --build build/mac-relwithdebinfo-801 \
    --target SceneServerWire_tests_run PublishOnly_tests_run
$RUN build/mac-relwithdebinfo-801/tests/src/Gui/SceneServerWire_tests_run
$RUN build/mac-relwithdebinfo-801/tests/src/Gui/PublishOnly_tests_run
# Then everything.
$RUN cmake --build build/mac-relwithdebinfo-801
QT_QPA_PLATFORM=offscreen $RUN ctest --test-dir build/mac-relwithdebinfo-801 -j6
```

Configure needs every dependency found even for the two-target build --
OCCT, Coin, Qt, PySide6 -- because the tree is one CMake project; the
two-target build only saves compile time. That is still the right order:
a stage 5 answer in an hour, the full tree afterwards.

### 4.6 What is expected to break on macOS, and what is not

*** **What actually happened (2026-09-06), against the list below.** Item 1
skipped rather than failed -- the case already guards on `/proc/self/maps`.
The dyld port that closes it landed the same day (`SceneServerPort.md` 7.6),
and it does not refuse the names item 1 names. Item 2 did not happen:
bgfx built SHARED and its C++ symbols resolved. Items 3 to 5 were not
reached, because only the two stage 5 targets were built. What did break was
not on this list: three missing includes that only libc++ needs, a
`BX_PLATFORM_OSX` branch calling a function that exists nowhere, and Apple
`ld` rejecting duplicate fontstash symbols. See `SceneServerPort.md` 7.5.

Known, in likely order of appearance:

1. **`PublishOnly_tests_run`, last case**: `/proc/self/maps` (section 2,
   item 3). Port it with `mach-o/dyld.h`; keep the assertion the same
   (no GL, EGL, Metal or driver image mapped -- on macOS the names to
   refuse are `OpenGL.framework`, `Metal.framework`, `GLEngine`, `AppleGVA`
   and `/System/Library/Extensions/`), and say in the commit which images
   a publish-only process does map, because that is the finding.
   **Done, and the finding overruled half of that list**: dyld maps
   `OpenGL.framework` and `Metal.framework` at launch as plain load-command
   dependencies of the renderer, so refusing them would fail the case on
   every mac. What is refused is the renderer plugin behind them --
   `GLEngine`, `*GLDriver`/`*MTLDriver`, `/System/Library/Extensions/`,
   `AppleGVA`. Image list and reasoning: `SceneServerPort.md` 7.6.
2. **bgfx as a shared library.** `src/3rdParty/CMakeLists.txt` builds bgfx
   STATIC on Windows (a DLL exports only the C API) and SHARED everywhere
   else. Whether a Mach-O dylib built with conda's flags exports the C++
   `bgfx::` symbols depends on visibility flags; ~78 unresolved
   `bgfx::init`-style symbols at the `FreeCADRenderer` link is the
   fingerprint, and the fix is `if (WIN32 OR APPLE)` on that one line.
3. **The Metal shader pack.** `src/Gui/Renderer/CMakeLists.txt` adds the
   `metal` profile on Apple, compiled by the in-tree `shaderc`. It does not
   affect stage 5 (the wire suite draws nothing) but it is on the path to
   a full build; a shaderc error naming `metal` is that.
4. **`sysctlbyname`, `dyld`, `.mm` files**: the tree already has Darwin
   branches for CPU counting (`FarFieldProxies.md`) and the ImGui file
   browser (`ShaderGraphEditor.md`); they have never been compiled. Expect
   small fixes, each its own commit.
5. **Apple GL is frozen at 4.1.** Irrelevant to the server and to bgfx,
   which picks Metal; relevant if you are tempted to judge the desktop
   viewport's rendering while you are there. Do not: that is not stage 5,
   and `DrawSubmission.md` records that the GL interop path is dead on
   macOS by design.

Not expected to break: the Asio/Beast transport itself. macOS is a BSD
socket platform like Linux, `v6_only(false)` is honoured, `::1` is
present by default, and `pthread` is the link line the test already has
for UNIX. If the wire suite fails on macOS the failure is interesting
precisely because it was not predicted; record it in full.

### 4.7 If the full configure is out of reach

If OCCT or Coin will not build on the box in the time you have, the stage
5 verdict can still be reached by hand, at the cost of not being the
tree's own build:

```sh
$RUN clang++ -std=c++20 -I src -I src/Gui/Renderer -I src/3rdParty/bgfx/bimg/3rdparty \
    -I build/mac-relwithdebinfo-801/src \
    -fsyntax-only src/Gui/Renderer/SceneServer.cpp
```

compiles the server translation unit against conda's Boost and Qt alone
(the `-I build/...` needs a configure that got as far as generating
`FCConfig.h`; without one, expect that header to be the first error). A
clean `-fsyntax-only` says the Beast instantiation and the include order
are fine on Apple clang; it says nothing about the link or the runtime,
and the 7.5 row must say it was a syntax check, not the suite.

## 5. Reporting back

The Linux session that wrote this will pick up from `SceneServerPort.md`
section 7.5 and `Testing.md`, so those two files are the report. Beyond
them:

- A source fix that is not platform-guarded changes Linux too; say so in
  the commit message, and run the wire suite on Linux before it is
  merged (the Linux box will, on pull).
- Do not push. Commits stay local until the person you are working with
  says otherwise, on every box.
- The next Linux session is on `ThinClient.md` stage 1 (the view-less
  viewer context), not on the port; nothing in stage 5 should wait for
  it, and nothing in it should wait for stage 5.
