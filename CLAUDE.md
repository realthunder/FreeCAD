# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

This is the **realthunder fork of FreeCAD** (remote `realthunder/FreeCAD`, working branch `LinkMerge`) — an open-source parametric 3D CAD modeler. It historically pioneered the `App::Link` feature and the Topological Naming Problem (TNP) fix; **the TNP problem is now considered essentially solved and is no longer the active focus.** Treat this fork as a **frontier for feature exploration**, not as code that must strictly conform to upstream FreeCAD conventions.

**Project direction (what to optimize for):**
- Run FreeCAD across **browser, mobile, and traditional desktop** from one codebase.
- Better **user interaction**, higher **performance on large models**, and better **rendering**.
- **Near-term focus: the bgfx-based renderer** (`src/Gui/Renderer/`).
- **Architectural goal: run OCCT geometry operations in separate process(es)** for parallelism and crash isolation (greenfield — no scaffolding yet).

Underlying tech: **OpenCASCADE (OCCT)** geometry kernel, **Coin3D** (Open Inventor) scene graph, **Qt6** GUI, **Python** API, and **bgfx** for the new renderer.

**Direction & design docs** — read these before large architectural work:
- `docs/RoadMap.md` — the vision, workstreams (renderer, headless engine, WASM tier, AI-native interface), and phasing.
- `docs/ComputeBoundaries.md` — the headless-server + parallel-recompute + unified-protocol design.
- `docs/TShapeRenderCache.md` — TShape-level tessellation sharing, color variants, and backend instancing/GPU-buffer sharing (the renderer instancing architecture).

## Companion forks (sibling repos)

This project is developed together with local forks kept as sibling directories; the dev build links against their **local installed builds**:
- `~/works/sw/coin` — forked Coin3D (`realthunder/coin`, tracks `coin3d/coin`).
- `~/works/sw/occt` — forked OCCT (`realthunder/OCCT`, tracks upstream). Improving this kernel is an explicit project focus.
- `~/works/sw/{coin3d-feedstock, freecad-rt-feedstock, pivy-feedstock}` — conda-forge recipes used for distribution image releases.

When a change requires a matching Coin or OCCT change, expect to edit those repos too.

## Build (development)

**Read `docs/DevEnvironment.md` first** — it documents the two build stacks on this box in full (layouts, exact commands, quirks). Summary:

- **Primary: conda-based debug stack** (Qt 6.10.1 + PySide6 6.10.1, gcc 15) — the only way to get a matched Qt6+PySide6 on Ubuntu 24.04. Env at `.conda/freecad`; wrap every build/run/debug command with `~/works/sw/fcad/.conda/run.sh` (activates env, keeps builds truly Debug). Configure with the `conda-debug-local` user preset → `build/conda-debug` (Ninja). Local occt/coin debug installs live in `<repo>/install/conda-debug`.
- **Fallback: system gcc + apt Qt 6.4.2** — `debug-local` user preset → `build/debug` (Makefiles; `sh src/make.sh -j$(nproc)` works). No PySide6 possible here, so Python workbenches don't load. Local occt/coin installs in `<repo>/install/debug`.

Key points that apply to both:
- **Force Qt6**: `FREECAD_QT_VERSION` defaults to `Auto`, which prefers Qt5 if present. The user presets set `-DFREECAD_QT_VERSION=6`.
- **Dependencies** (Coin3D, OCCT, Qt, etc.) are found via `CMAKE_PREFIX_PATH` pointing at the local forks' install prefixes (the Coin/OCCT CMake config packages are resolved via `find_package(... CONFIG)`; see `cMake/FreeCAD_Helpers/SetupCoin3D.cmake` and `cMake/FindOCC.cmake`). `OCC_INCLUDE_DIR` overrides the manual fallback path.
- **OCCT installs need `-DCMAKE_INSTALL_RPATH='$ORIGIN'`** or its libs can't find each other at runtime (RUNPATH is not transitive).
Module inclusion is controlled by `BUILD_<MODULE>` options (`BUILD_GUI`, `BUILD_PART`, `BUILD_PART_DESIGN`, `BUILD_SKETCHER`, `BUILD_FEM`, `BUILD_TECHDRAW`, `BUILD_DRAFT`, `BUILD_ASSEMBLY`, …), defined in `cMake/FreeCAD_Helpers/InitializeFreeCADBuildOptions.cmake`. Disable modules you aren't touching to speed up iteration.

## The renderer (near-term focus)

Lives in `src/Gui/Renderer/`, built as the `FreeCADRenderer` shared lib (always linked into `Gui`):
- `Renderer.h` / `Renderer.cpp` — backend-agnostic interface (`Render::Renderer`, `RendererLib`) and the `RendererFactory` singleton. Backends self-register at static init and are selected at runtime by type string.
- `BGFXRenderer.cpp` — the **bgfx** backend (the one to develop). `DiligentRenderer.cpp` — a parallel DiligentEngine backend. Enabled with `-DBUILD_BGFX=ON` / `-DBUILD_DILIGENT=ON`.
- Vendored engines: `src/3rdParty/bgfx/` (git submodule, built SHARED), `src/3rdParty/Diligent/`. Shaders: `src/Gui/Renderer/bgfx/assets/shaders/`.

**Integration with Coin3D**: the renderer runs **alongside** Coin, not replacing it. In `src/Gui/View3DInventorViewer.cpp`, `setRendererType()` (~3163) creates the renderer (only when render-cache mode == 3, i.e. `ViewParams::getRenderCache()==3`), and `renderScene()` (~3181) calls `renderer->render(...)` **first** into the shared Qt GL context, then Coin's `SoGLRenderAction` composites the scene graph on top.

**Current state**: proof-of-concept — `BGFXRenderer` draws a hard-coded demo cube. The next major task is feeding real model geometry into bgfx, most naturally by bridging the existing Coin geometry caches in `src/Gui/Inventor/SoFCRenderCache*` / `SoFCRenderer.*`. Cross-API portability (OpenGL / Vulkan / Metal / WebGL — the browser/mobile story) is delegated to bgfx's runtime backend selection, not hand-written in FreeCAD.

## Architecture (big picture)

FreeCAD is layered; each layer is its own set of shared libs, and most modules split into an **App** (headless/data) part and a **Gui** (Qt/Coin view) part.

- **`src/Base/`** — foundation, no CAD knowledge: `Console`, `Exception`, vector/matrix/placement math, units/`Quantity`, `Persistence`/`Writer`/`Reader` (document serialization), `Parameter` (preferences), Python-binding helpers.
- **`src/App/`** — the document model, no GUI. A `Document` (a `.FCStd` = zipped XML + data) holds a DAG of **`DocumentObject`s**. Parametric behavior = typed **`Property`** members (registered via `ADD_PROPERTY*`) + `execute()` recompute. Subsystems: dependency graph + recompute engine (`Document.cpp`), expression engine (`Expression*`), `App::Link`/`LinkBaseExtension` (cross-object/document linking), the **Extension** mixin mechanism (`DocumentObjectExtension`), and the element-map/TNP core (`ComplexGeoData`, `ElementMap`, `MappedElement`, `StringHasher`).
- **`src/Gui/`** — Qt6 + Coin3D presentation. `ViewProvider*` render `DocumentObject`s into the Coin scene graph; `Command`s implement UI actions; `TaskView`/task panels drive interactive editing. The bgfx renderer module lives here (`Gui/Renderer/`).
- **`src/Mod/`** — workbenches (Part, PartDesign, Sketcher, Draft, Assembly, Fem, TechDraw, Mesh, Path, Spreadsheet, …), each typically `App/` + `Gui/`. `Part` wraps OCCT (`TopoShape`); `PartDesign` builds feature-based solids; `Sketcher` = constrained 2D geometry.
- **`src/Main/`** — entry points (`FreeCAD` GUI, `FreeCADCmd` console). **`src/Ext/`** — bundled Python. **`src/3rdParty/`** — vendored deps / submodules (`bgfx`, `OndselSolver`, Diligent).

### The parametric loop
Editing a `Property` "touches" its `DocumentObject`; document recompute walks the dirty DAG calling each object's `execute()` to rebuild its result (e.g. a `TopoShape`); `ViewProvider`s observe the change and update the 3D view. This property → touch → recompute → viewprovider cycle is the heart of the modeler — and OCCT `execute()` calls are what the out-of-process architecture goal aims to isolate.

### Python bindings are generated
Most C++ classes exposed to Python use a **three-file pattern**:
- `FooPy.xml` — declares the Python type/methods/attributes.
- `FooPyImp.cpp` — hand-written method bodies.
- `FooPy.h` / `FooPy.cpp` — **generated at build time** from the XML by `generate_from_xml(FooPy)` in the module's `CMakeLists.txt` (see `src/App/CMakeLists.txt`).

To change a Python API, edit the **`.xml`** and the **`PyImp.cpp`** — never the generated `FooPy.cpp`.

## Tests

- **C++ (GoogleTest)**: `tests/src/`, mirroring the source tree (`App/`, `Base/`, `Gui/`, `Mod/`, `Misc/`). Run via `ctest` in the build dir, or a test binary directly with `--gtest_filter='Suite.Case'`.
- **Python**: the `Test` workbench at `src/Mod/Test/` (`TestApp.py`, `Document.py`, `BaseTests.py`, …) plus per-module tests. Run with `FreeCADCmd -t <TestModule>` or, in the Python console, `import Test; Test.runTestApp()`.

## Code style & conventions

- **C++**: `.clang-format` (LLVM-based, 4-space indent). **Python**: `black`, line length **100**.
- **pre-commit** (`.pre-commit-config.yaml`) runs clang-format/black/whitespace fixes but only over an **allowlisted subset** of `src/` — much of the tree is exempt. Run `pre-commit run --files <changed>` before committing; still follow `.clang-format`/black for files outside the allowlist.
- `.git-blame-ignore-revs` lists bulk-reformat commits — use `git blame --ignore-revs-file .git-blame-ignore-revs`.
- **Commit messages**: `Area: summary`, where Area is the module abbreviated — `Gui:`, `App:`, `Part:`, `PD:` (PartDesign), `Sketcher:`, `TD:`/`Techdraw:`, `Sheet:` (Spreadsheet). Keep one problem per PR.

## Gotchas
- `Auto` Qt detection picks Qt5 when present — always force `-DFREECAD_QT_VERSION=6`.
- Never hand-edit generated `*Py.cpp`; change the `.xml`/`PyImp.cpp`.
- The bgfx renderer only runs when render-cache mode is set to the "renderer" (3) mode; otherwise the renderer is null and only Coin draws.
- Document persistence is versioned — when adding/renaming properties, keep backward-compatible restore (`handleChangedPropertyName`/`Restore` patterns) so old `.FCStd` files still load.
- Prefer solutions that keep browser/mobile portability open (e.g. don't hard-code desktop-GL assumptions in renderer work; let bgfx choose the backend).
