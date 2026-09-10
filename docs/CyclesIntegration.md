# Cycles integration

Bringing Blender's Cycles path tracer into the fork as a vendored
renderer, feeding a 3D view that draws nothing of its own but the
selection highlight. Written 2026-08-27 as the plan and the rulings;
every phase of the plan is built (section 8 carries the record per
phase, section 6.2 the translation as it stands). Item 15's phase B,
the MaterialX bridge, is built through section 6.14; the material card
that carries a shader graph (section 6.13) was designed here and is
built under `docs/MaterialStorage.md`, which is where that work now
lives.

Related: `docs/RenderEngine.md` (the bgfx engine that hosts the blit),
`docs/CAMSimRenderPort.md` (sections 8 and 10 -- the borrowed-frame
contract Cycles reuses), `docs/TShapeRenderCache.md` (the geometry
source), `docs/ComputeBoundaries.md` (the out-of-process target),
`docs/SplitViews.md` (the side-by-side preview idea).


## 1. Decisions taken

Ruled 2026-08-27:

- **Dependencies come from conda, not from Blender's precompiled
  bundle.** `WITH_LIBS_PRECOMPILED=OFF`. Section 3.
- **The vendored branch tracks `main`**, not a release branch. The
  submodule SHA is the pin, so tracking a fast-moving branch costs
  nothing until we choose to bump. Section 2.
- **In process now, structured for out of process later.** The scene
  translation layer takes no Gui types, so it can be lifted into a
  server process without a rewrite. Section 7.
- **Both highlight routes are supported**, not one: the cheap on-top
  route and the depth-correct route. Section 5.3.
- **GPU acceleration targets both NVIDIA and AMD.** NVIDIA CUDA is
  testable on this box; OptiX and AMD HIP are ship targets only.
  Section 4.
- **The view draws only the selection highlight.** Cycles' image is the
  base layer; no shaded document geometry is drawn by the host.
  Section 5.
- **Selection stays with the host, picking and highlight both.** Depth
  for the highlight comes from a host depth-only prepass, never from
  Cycles' Depth pass. Ruled 2026-08-28 after checking what Blender does
  (section 5.6).

Deliberately NOT decided yet: whether the Cycles view is a mode on an
existing 3D view or its own view type (section 5.4).


## 2. The fork

`realthunder/cycles` is a fork of `blender/cycles` -- the **standalone**
Cycles repository, not the Blender monorepo. That distinction is what
makes this possible at all:

- **The licence is Apache 2.0**, not GPL. Linking it into an LGPL2+
  codebase is fine. Blender proper would not be.
- The standalone repo is built to be embedded: it already carries a
  `Session` / `Scene` / `OutputDriver` API and a `src/app` sample host.

State when surveyed (2026-08-27): default branch `main`, synced through
2026-08-19, about 21MB of source. Release branches exist through
`release/5.2`.

The fork carries one extra branch, `demo-tile`: a single commit
`ae9842a73` overriding `OIIOOutputDriver::update_render_tile` to dump
every progressive refinement as a numbered image. It is a probe that
progressive output works, it is 670 commits behind `main`, and it
renumbers files with a process-wide static counter. **Do not merge it.**
Keep it as the historical note that the progressive path was checked,
and write the real output driver fresh.

Vendoring shape, following bgfx: a submodule at `src/3rdParty/cycles`
pointing at `realthunder/cycles`, on a `LinkVibe` branch cut from
`main` (the fork has no `LinkVibe` branch yet -- create it).

**The `lib/*` submodules are safe.** Cycles' own `.gitmodules` marks
every precompiled library bundle (`lib/linux_x64` and friends) as
`update = none`. A recursive submodule init will NOT drag down the
multi-GB Git-LFS payload. Verify this holds after the first init rather
than trusting it.


### 2.1 The fork set the whole tree's MSVC flags (fixed 2026-09-10)

`src/cmake/configure_build.cmake` is Blender build code written for a
top-level project, and its `elseif(MSVC)` branch did
`set(CMAKE_CXX_FLAGS ... CACHE STRING ... FORCE)` on the GLOBAL flag
variables. Added with `add_subdirectory()`, that set the compiler flags
for **all of FreeCAD**, not for Cycles. It was not visible in any one
place: the cache held Cycles' string, FreeCAD's own
`SetGlobalCompilerAndLinkerSettings.cmake` appended to it on the next
configure, and the result compiled.

Three things it cost, none of them a preference:

- **`/J`** -- plain `char` UNSIGNED -- on every translation unit in the
  tree, while OCCT and Coin are built without it. Their headers inline
  into ours, so the same inline function was compiled two ways. Verified
  in their caches: OCCT's `CMAKE_CXX_FLAGS` is CMake's stock
  `/DWIN32 /D_WINDOWS /W3 /GR /EHsc`.
- **No `NDEBUG` in any configuration**, because Cycles' per-config
  strings omitted it and they replaced CMake's, which carry it. Same ODR
  mismatch, found first and fixed on its own (`193fa35dd`); measured
  afterwards and worth no frame time, `docs/RenderEngine.md` 7.10.
- **CMake's own `/DWIN32 /D_WINDOWS /GR` discarded.** Two `#ifdef WIN32`
  blocks in FreeCAD therefore took their non-Windows branch on Windows:
  `Main/MainGui.cpp` reopened the console streams with `freopen` instead
  of `_wfreopen`, which cannot name a path outside the code page, and
  `zipios++/ziphead.cpp` stamped every `.FCStd` central directory with
  the Unix writer version.

**The fix scopes rather than deletes.** Under `CYCLES_EMBEDDED` the same
flags go through `add_compile_options()`, which reaches that directory
and the subdirectories added after it and nothing else; the standalone
branch is untouched, `/DNDEBUG` included. The per-configuration strings
are not reproduced at all -- CMake's MSVC defaults match them term for
term but for `/MD`, which comes from `CMAKE_MSVC_RUNTIME_LIBRARY`, and
Cycles' own sources get `NDEBUG` from the per-config directory
`COMPILE_DEFINITIONS` property its top-level `CMakeLists.txt` sets.

**`/J` is not carried over even for Cycles.** Scoping it would move the
mismatch rather than remove it: the host compiles its own Cycles
translation units (`FreeCADRendererCycles`), which include these headers
along with Qt's, OCCT's and FreeCAD's, so `/J` on them would hand
unsigned `char` to a far larger surface than it fixed. Cycles cannot
depend on it in any case -- it builds and runs on x86-64 Linux, where
plain `char` is signed.

**Two flags FreeCAD was getting only by accident** came back on its own
terms in `SetGlobalCompilerAndLinkerSettings.cmake`: `/utf-8`, which is
load-bearing (the sources are UTF-8 without a BOM, over a thousand
tracked files carry non-ASCII, and the system code page here is 936),
and `/nologo`, which is not.

Reconfiguring is not enough by itself. `CACHE ... FORCE` wrote those ten
entries into `CMakeCache.txt`, and removing the code that wrote them
leaves them standing -- `cmake -U CMAKE_CXX_FLAGS -U CMAKE_C_FLAGS -U
CMAKE_{C,CXX}_FLAGS_{DEBUG,RELEASE,MINSIZEREL,RELWITHDEBINFO}` and then
configure, which lets CMake re-initialise them from the platform module.
**Verify the compile line, not the cache**; both have been wrong here.

## 3. Dependencies

**"Vendor like bgfx" cannot mean "self-contained like bgfx".** bgfx has
no external dependencies at all; `bgfx.cmake` wraps bx/bimg/bgfx and
builds from source in tree. Cycles is the opposite: even with every
optional feature off it needs OpenImageIO and TBB, and OpenImageIO
drags OpenEXR and an imaging stack behind it.

Blender solves this with a multi-GB precompiled Git-LFS bundle pinned
to a VFX platform year (`WITH_LIBS_PRECOMPILED=ON`, the default).
**We are not taking that route**, for three reasons, the first of which
is decisive:

1. **It ships its own TBB and OpenEXR, and this process already links
   TBB through OCCT.** Two TBB runtimes in one address space is a
   well-known route to silent corruption and exit-time crashes.
2. It is per-platform and multi-gigabyte, which the feedstock release
   path cannot carry.
3. It has no WASM story, so it buys nothing for the browser tier.

Confirmed available on conda-forge 2026-08-27:

| Package | Version seen | Role |
| --- | --- | --- |
| `openimageio` | 3.1.15.0 | mandatory |
| `embree` | 4.4.1 | CPU ray traversal, keep ON |
| `openimagedenoise` | 2.5.1 | keep ON, see below |
| `tbb`, `openexr` | already in `.conda/freecad` | mandatory, shared with OCCT |

Feature options for the first cut. Cycles defaults nearly everything
ON; most of it is dead weight here:

- **ON**: `WITH_CYCLES_EMBREE`, `WITH_CYCLES_OPENIMAGEDENOISE`.
  Denoising is not a luxury -- it is what makes a 30-sample interactive
  preview look finished rather than grainy, and it is the single
  highest-value option for a viewport.
- **OFF**: `WITH_CYCLES_OSL` (drags LLVM), `WITH_CYCLES_USD`,
  `WITH_CYCLES_HYDRA_RENDER_DELEGATE`, `WITH_CYCLES_ALEMBIC`,
  `WITH_CYCLES_OPENVDB`, `WITH_CYCLES_NANOVDB`,
  `WITH_CYCLES_OPENSUBDIV`, `WITH_CYCLES_STANDALONE_GUI`.

`WITH_CYCLES_OPENSUBDIV` is worth revisiting later -- CAD tessellation
is already explicit, so subdivision buys little, but it is the one
"off" that a future smooth-surface path might want back.

**The recipe -- what to install, how to configure, and the one environment
variable a CUDA run needs -- is `docs/DevEnvironment.md`, "Cycles (path-traced
renderer)".** This section is the reasoning behind it; that one is what to type.

### 3.1 Installing the deps is itself a hazard

**Solve the new packages in a scratch env first, never straight into
`.conda/freecad`.** The precedent is on record: a previous new-dependency
round had `cgal-cpp` silently downgrade boost and break every FreeCAD
binary in the environment. Before committing to the real env, check
that the solve does not move `qt6-main`, `pyside6`, `boost`, `tbb` or
`openexr`. The FEM stack already keeps its heavy dependencies in a
separate prefix (`.conda/fem-deps-801`) for exactly this reason, and
that is the fallback shape if the solve turns hostile.


## 4. GPU devices

### 4.1 NVIDIA -- CUDA testable on this box, OptiX is not

Verified 2026-08-28 on the WSL2 dev box (this corrects the 2026-08-27
survey, which read a filename as a capability):

- The GPU is an **RTX 3070 Ti Laptop, compute capability 8.6 (Ampere),
  8GB VRAM**, driver 592.00. 8GB is the real constraint on scene size.
- **CUDA works.** `libcuda.so.1` in `/usr/lib/wsl/lib` is the real
  thing; the CUDA device enumerates and renders. Measured on
  `examples/scene_monkey.xml` at 800x500: 64 samples CPU 4.3s / CUDA
  1.3s wall, 1024 samples CPU 15.7s / CUDA 2.9s -- about 5x, after a
  one-time 297s kernel compile.
- **OptiX does NOT work here, and the file that suggested it would is
  a decoy.** `/usr/lib/wsl/lib/libnvoptix.so.1` is a 10KB shim with
  SONAME `libnvoptix_loader.so.1` that exports five `dxcore_*` helpers
  and no OptiX entry point at all; `optixInit()` fails with
  `OPTIX_ERROR_ENTRY_SYMBOL_NOT_FOUND` (7805). The 48MB `nvoptix.bin`
  beside it in the driver store is not an ELF, and the shim's loader is
  literally `get_library_path` + `dlopen`, so it cannot load that
  either. NVIDIA does not support OptiX on WSL2. The known workaround
  (Mitsuba's docs) is to extract `libnvoptix.so.1` and
  `libnvidia-rtcore.so` from the version-matched *Linux* driver into
  `C:\Windows\System32\lxss\lib` -- an admin-level change on the
  Windows side that every driver update undoes. **Not done; OptiX is a
  build-and-ship target like HIP until the user rules otherwise.**
  The OptiX *device* builds and links fine; only the runtime is
  missing.

What was added to BUILD and RUN the kernels:

- **nvcc 12.9, in its own prefix `.conda/cuda-129`** (`cuda-nvcc=12.9`
  + `cuda-cudart-dev=12.9`, 217MB). It cannot go into `.conda/freecad`,
  which already pins `cuda-version 13.3`, and Cycles' runtime check
  (`device/cuda/device_impl.cpp`) wants 10.2 <= CUDA < 13 -- above that
  is only a logged warning, but Blender's supported range is the pin.
  At runtime Cycles shells out to nvcc found via **`CUDA_BIN_PATH`**
  (cuew), so the prefix's `bin` goes in that variable.
- **OptiX SDK headers from `NVIDIA/optix-dev`** (GitHub, headers-only,
  9.1.0; Cycles wants >= 8.0), cloned to `~/works/sw/optix-dev`. No
  bundle lift needed. `OPTIX_ROOT_DIR` at configure time, and
  `CYCLES_RUNTIME_OPTIX_ROOT_DIR` bakes the same path in for the
  runtime kernel compile.
- Kernels are compiled at runtime from **installed source**:
  `path_get("source")` resolves next to the binary, so a build-dir
  binary finds nothing -- `cmake --install` first, run `install/cycles`.
  In tree the build ships `source/{kernel,util}` to
  `<resource>/Renderer/cycles/` (custom target `Renderer_cycles_source`
  plus an install rule) and the unit hands `path_init()` that root.
- **The CUDA device is listed only when nvcc can be found.** With no
  precompiled binaries, `device/cuda/device.cpp` reports the device
  available only if `cuewCompilerPath()` succeeds, so without
  `CUDA_BIN_PATH` CUDA silently vanishes from the device list -- in
  the standalone and in tree alike. It is not a library-path matter.

Keep `WITH_CYCLES_CUDA_BINARIES=OFF` at first (it is already the
default). Compiling the CUDA and OptiX kernel binaries is by a wide
margin the slowest part of a Cycles build; the runtime compile caches
under `~/.cache/cycles/kernels`.

### 4.2 AMD -- a ship target, not a test target

The iGPU on this box is a **Radeon 680M (RDNA2, gfx1035)**, part of the
Ryzen 9 6900HS. The good news is that **`gfx1035` is in Cycles'
`CYCLES_HIP_BINARIES_ARCH` list**, so Cycles will build kernels for it;
Blender clearly targets APUs, since `gfx90c` and `gfx902` are in that
list too.

The blocker is the runtime, and on this box it is total:

- No `/opt/rocm`, no `hipcc`, no `libamdhip64` in `ldconfig`.
- **No AMD libraries at all in `/usr/lib/wsl/lib`.** WSL2's `/dev/dxg`
  passthrough is exposing only the NVIDIA userspace stack here.
- AMD does not ship a ROCm/HIP WSL runtime for Rembrandt APUs.

So `WITH_CYCLES_DEVICE_HIP` stays a build-and-ship target that this
environment cannot exercise. Testing AMD needs a native Windows build
(the Adrenalin driver carries the HIP runtime) or a Linux box with
ROCm and a supported discrete card.

Expectation to set honestly: even where it runs, a 680M sharing system
memory will most likely lose to this box's 8-core Zen3+ CPU with
Embree. **The AMD path's value is coverage for users with discrete
Radeons, not speed on this laptop.** Do not let a slow iGPU number read
as a broken HIP port.


## 5. Where it plugs in

### 5.1 The view draws only the highlight

The 3D view stays a real view: camera, navigation, selection and all
the interaction machinery are unchanged. What changes is what gets
drawn. The host draws **no shaded document geometry**. Cycles' image is
blitted as the base layer, and the selection highlight goes on top.
What the host still rasterizes is a **depth-only prepass** of the
scene (section 5.3) and whatever picking needs (section 5.5); neither
produces colour.

This is a large simplification and it is the reason the design is
tractable. It means **Cycles geometry and bgfx geometry never have to
interleave by depth inside one frame** -- the problem the CAM simulator
had to solve in `docs/CAMSimRenderPort.md` section 8. Here the
path-traced image is simply the bottom layer.

### 5.2 The host contract already exists

`Render::FrameConsumer` (`src/Gui/Renderer/Renderer.h:2497`) is exactly
the hook: `framePasses()`, `overlayPasses()`, and
`drawFrame(DrawSurface &)`, drawing inside a 3D view's frame on pass
ids from that view's own block. `7b148e9cdf` established that a frame
consumer may put its image into the host's depth; this consumer does
not need to (section 5.3), so its blit runs with depth test and depth
write both off -- the same quad Blender's display driver draws.

Cycles emits a float buffer, not draw calls, so the consumer is thin:
buffer -> texture -> one blit pass. Most of the work is in getting the
buffer there safely, not in drawing it.

**Use Cycles' `DisplayDriver`, not its `OutputDriver`, for the
viewport** (`src/session/display_driver.h`). `OutputDriver` is the
offline interface: a full-buffer callback when a tile finishes.
`DisplayDriver` is what Blender's viewport implements: the engine
calls `update_begin(params, width, height)` with the *effective*
resolution (the progressive resolution divider already applied, so a
coarse first pass needs no re-allocation), maps a half4 buffer the
host owns (`map_texture_buffer`), fills it from its render threads,
and `Session::draw()` -- called by the host, on the host's thread --
invokes `draw(params)`. That split is exactly the lock-and-copy the
paragraph below asks for, built into the interface; and its
`GraphicsInteropBuffer` is the later zero-copy path (a GL pixel
buffer object or Vulkan buffer the GPU device writes directly).

**The threading rule is the first bug waiting to happen.**
`drawFrame()`'s contract forbids crossing the frame boundary: no bgfx
frame of its own, no resize, no repaint request. But Cycles runs its
session on its own threads and calls the driver's update methods from
them. So the handoff must be a lock-and-copy (or an ownership swap)
into a staging buffer done OUTSIDE `drawFrame`, with `drawFrame` only
consuming what is already resident and uploaded.

### 5.3 Both highlight routes, one depth source

Ruled: support both, and let the cheap one be the default. Ruled
after (2026-08-28): **the depth the highlight tests against is the
host's own, from a depth-only prepass of the render cache -- not
Cycles' Depth pass.** Section 5.6 records why.

- **On-top route (cheap).** The highlight is drawn in the overlay run,
  ignoring occlusion. It works the moment the colour blit works.
  Highlights are frequently drawn on top anyway, so this is a
  legitimate end state, not just a stepping stone. This is what
  `ShowSelectionOnTop` already selects.
- **Depth route (correct).** The host rasterizes the scene depth-only
  (the mesh program already has this role: it is the AO block's
  prepass, `docs/RenderEngine.md`), the Cycles colour is blitted over
  it without touching depth, and the highlight depth-tests as it does
  today (`OutlineSpec::depthTest`). Nothing leaves Cycles but colour,
  so there is no depth-range reconciliation at all -- the trap the CAM
  simulator paid for (stock-derived near/far) cannot occur, because
  the depth was written by the host's own projection.

  Preferred shape of the depth route: not "hidden", but **dimmed** --
  Blender's outline shader draws the visible part of a selected
  outline at full alpha and the occluded part at a reduced alpha
  (`alpha_occlu`), in a single pass. That gives both rulings from one
  pipeline, with no mode switch.

- **Cost control.** The depth prepass need not re-run on every Cycles
  tile arrival; only camera and scene changes move it. Blender 2.93
  gated its prepass on exactly this (`update_depth` false on the
  no-rebuild redraws that progressive samples trigger). The idle
  temporal accumulation code already tells a camera-move frame from a
  refine frame, so the same signal serves here.

Build the on-top route first; it unblocks everything else.

### 5.4 Which view

Both shapes are built. The first one has its UI since 2026-08-29: the
**External shading model** -- the fourth value of the view's declared
`ShadingType` enum, with `ExternalRenderType` naming the engine and the
per-view `Cycles_*` properties (device / samples / time limit / denoise
/ pixel size) as the session options.
`View3DInventorViewer::syncExternalShading()` starts, restarts and
stops the session off those properties, and the Display style
popover's fourth radio plus its Settings... dialog drive them
(Gui/ShadingOptions.cpp). `Cycles_Device` is an enumeration built from
THIS machine's devices and re-mapped by NAME after every restore
(`Gui::remapCyclesDeviceProperty`), falling back to the first local
entry, so a document from a machine with different GPUs still renders.

- A **render mode toggled on an existing 3D view** -- fewest moving
  parts, and the camera is already the right one. This is what
  `view.cyclesViewport(...)` does (section 5.7), and what the External
  shading model wraps.
- A **Cycles cell beside the normal view**, with a linked camera, using
  the split-view/ViewArea canvas (`docs/SplitViews.md`). Model on the
  left, path-traced preview on the right. Section 5.11: the same call
  on a cell of a unified canvas path traces that cell alone, and
  `view.bindView(other)` links the cameras.

### 5.5 Picking

Picking still needs geometry, and it is nearly free: the scene has to
stay resident anyway to FEED Cycles, so Coin's CPU ray-pick keeps
working untouched. Nothing special is required for selection to
continue functioning while the view draws no shaded geometry.

Cycles' **Object Index / Cryptomatte** passes are NOT a picking route,
now or later: they are object- or material-level, and CAD selects
faces, edges and vertices. Blender does not pick from them either
(section 5.6).

### 5.6 What Blender does, and why the same answer holds here

Checked against the sources 2026-08-28 (v2.93 and current `main`):

- **Cycles supplies colour only.** `BlenderDisplayDriver::draw` blits
  a half-float texture as a quad (`GPU_SHADER_3D_IMAGE`, premultiplied
  alpha blend), with no depth test and no depth write, and the driver
  never uploads a depth texture. Blender has never composited its
  overlays against Cycles' Depth pass.
- **Overlay depth is rasterized by the viewport.** In 2.93 the
  external engine drew its own depth-only pass of every renderable
  object after `view_draw`. In current `main` the overlay engine owns
  it: `is_render_depth_available` is true only for Workbench and
  (unscaled) EEVEE, and otherwise it "clears the depth and renders a
  depth prepass". The external engine's leftover prepass is
  grease-pencil-only and carries the comment "should ultimately be
  replaced by render engine depth output" -- an aspiration never acted
  on.
- **The selection outline is one depth-aware pass.**
  `overlay_outline_detect_frag.glsl` edge-detects an id buffer of the
  selected objects and dims the result where
  `ref_depth > scene_depth + epsilon` (`alpha_occlu`).
- **Picking never touches the render engine.** The `select` draw
  engine rasterizes ids into an offscreen u32 buffer with its own
  shaders.

Why Cycles depth is the wrong thing to test a highlight against, and
worse for CAD than for Blender: it is **filtered** depth, averaged
over the pixel filter at silhouettes (exactly where the outline lives)
and smeared by depth of field; and it **lags**, because Cycles
restarts on every camera move, so during navigation the depth would
be the previous camera's at reduced resolution while the highlight is
drawn with the current one. A host prepass is camera-synchronous and
pixel-exact. The one legitimate case for engine depth is geometry the
host does not have -- displacement, subdivision -- and with
`WITH_CYCLES_OPENSUBDIV=OFF` and explicit CAD tessellation both sides
see the same mesh.


### 5.7 What the viewport does (phase 4 step 11, built 2026-08-28)

`src/Gui/Renderer/CyclesViewport.cpp` (`Render::Cycles::Viewport`,
declared in `CyclesRenderer.h`, in the same `FreeCADRendererCycles`
object library as the translation) is the `FrameConsumer` of section
5.2, built as that section describes and nothing more: the on-top
highlight route, no depth prepass, no incremental scene update.

- **The session.** `Viewport::setScene(SceneInput)` builds a fresh
  `ccl::Session` in interactive mode (`background = false`, the
  resolution divider on, the sample and time budget from
  `ViewportOptions`), translates the input with the phase-3
  `SceneTranslator`, and starts it. At step 11 a restated scene tore
  the session down and built another -- the device setup and a full
  translation per change; section 5.10 replaced that with a restate
  in place, keyed the way the mesh map already was (cacheId +
  generation), the cache's own contract.
- **The handoff.** The `DisplayDriver` is a staging buffer of `half4`
  sized to the full render, held under a mutex from `update_begin()`
  to `update_end()` -- Cycles' copy into it is a memcpy, so the host
  waits at most that long. The effective size (the divider applied)
  rides with the buffer and is the pitch Cycles fills it with.
  `update_end()` fires a redraw callback FROM the render thread; the
  viewer marshals it to its own thread with a queued
  `QMetaObject::invokeMethod` on the widget, which a widget on its way
  out drops with itself.
- **The blit.** `drawFrame()` takes the staged frame if it is newer,
  uploads it into an RGBA16F texture at the effective size (recreated
  when that changes, linear-filtered so the coarse pass reads
  smoothly), and draws the engine's own fullscreen triangle with
  `vs_fc_comp` + `fs_fc_cycles_blit` into `hostTarget()`: depth test
  and write off, one / one-minus-src-alpha (Cycles' combined pass is
  premultiplied). The frame is LINEAR and stays so for a
  colour-managed host target; a display-space target (no output
  transform) gets it encoded in the shader, gated on
  `hostLinearColor()` -- the section 6.1 rule, applied. One pass in
  the consumer's scene run: after the opaque geometry and the outline,
  before the transparent bucket.
- **The throttle.** `drawFrame()` calls `Session::draw()` (the
  driver's `draw()` does nothing -- the frame was consumed already)
  purely so the session counts a draw, because `ready_to_reset()` is
  true only once a frame was drawn after the last reset: Blender's
  rule, and what keeps a camera drag from cancelling every restart
  before a pixel shows. `setCamera()` restates the camera under the
  scene's mutex and resets the session when it may; otherwise the
  move is held and applied by the next `drawFrame()`, which the
  update that made the session ready has itself requested.
- **The feed.** `View3DInventorViewer::setCyclesViewport()` owns one
  `Viewport` per view; `Private::feedCyclesViewport()` runs just before
  `renderer->render()` each frame. It registers the consumer on the
  current backend whenever that is not the one registered (a backend
  can be swapped under a view), compares the backend's new
  `Renderer::sceneGeneration()` -- a counter every `setScene()` bumps,
  added for this -- and the translated PBR/output/light/background
  configs against what was last fed, and either re-translates the
  render cache into a new `SceneInput` or hands over the camera alone.
  The camera is the Coin camera's matrices, NOT `hostCamera()`: the
  host's projection carries the idle accumulator's jitter, which would
  read as a move every frame.
- **Facade additions** (`DrawDevice.h`), all general:
  `DrawTextureFormat::RGBA16F`, `updateTexture2D()`,
  `BlendMode::Premultiplied`.
- **Python.** `view.cyclesViewport(enable=True, device='CPU',
  samples=256, timeLimit=0.0, denoise=False)` and
  `view.cyclesViewportStatus()` (running, progress, the engine's
  status text, error, the translation report).

Verified 2026-08-28 (scratchpad `cycles_viewport_probe.py` under xvfb,
the phase-3 scene, 858x384): the live frame settled at the sample
budget agrees with `cyclesRender` of the same camera at the same size
to a mean of 0.9/255 over the middle of the frame (CPU 16 spp, CUDA
64 spp -- the GPU's frame arrives through the same map path); a
camera move restarts at the coarse divider and settles again; a
recolour restates the scene through the generation counter; turning
the viewport off puts the backend's own frame back. Two things the
grabs show that are step 12's, not defects of step 11: the host still
draws its own shaded geometry under the blit (wasted, and its
translucent bucket composites OVER the Cycles image, so a transparent
body reads doubly), and its edge lines sit on top as the overlay they
are meant to be.

Not done at step 11 (step 12 below took the first two): the sample/time
budget UI, cancel-on-move pixel size and denoise defaults (step 13 --
`denoise` is an option already, unexercised); a view on a ViewArea
canvas (the cell path does not run `feedCyclesViewport`); picking with
a session running was not probed.

### 5.8 The host's depth under the blit (phase 4 step 12, built 2026-08-28)

`Renderer::setExternalBaseLayer(bool)`, set by the viewer while a
Cycles viewport is registered, is the whole switch. It does not add a
pass: it changes what the passes the frame already has draw.

- **Scene triangles rasterize depth-only.** `BGFXView::submit()`
  turns a plain scene triangle's `PassNormal` into the existing
  `PassDepthOnly` (the on-top fills' prepass: colour writes off, depth
  test and write on), still in `ViewOpaque`; an instanced opaque group
  drops its colour writes the same way. A **transparent** scene
  triangle is not drawn at all -- the consumer's image carries its
  alpha, and its depth would hide what shows through it (an instanced
  transparent group is refused so its members reach that rule). The
  scene draws a selection HIDES (re-drawn as the selection) lay their
  depth down too, so a selected object still occludes the lines
  behind it.
- **Everything else the host draws in colour moves after the blit.**
  Lines and points, tessellation-style edges, and the non-on-top
  selection fills are routed to `ViewOnTop` -- sequential, after the
  consumer's scene run, before the highlight -- with their depth state
  intact, so a feature line depth-tests against the depth-only scene
  exactly as it did against the shaded one. On-top draws, the
  preselection and the on-top selections already lived there.
- **A depth-tested selection fill dims where hidden** instead of
  vanishing (section 5.3's preferred shape, Blender's `alpha_occlu`):
  the frame submits it once as `PassLineHidden` (depth test off,
  blended, alpha scaled by the hidden-line alpha or 0.4 when the
  material carries none -- the mesh program has no alpha ceiling, so
  the dim rides the material alpha) and then as it always did, depth
  tested, full colour over it.
- **The screen-space shading effects stand down**: AO, cavity, bloom,
  the ground reflection and the idle accumulation that refines them
  have no shaded image to work on. Shadows, the volumetric light
  shafts and the water/glass/cloud/fire passes are left alone: they
  run after the consumer's blit by construction (section 5.2's pass
  order) and composite over the path-traced image, which is what the
  doc's "skipped" volumetric bodies were always meant to do.

Verified 2026-08-28 (the step-11 probe plus a non-on-top selection
leg, `ShowSelectionOnTop` off): the sphere is now Cycles' translucent
one alone (the host's transparent bucket no longer composites over
it); feature lines draw over the image and are hidden behind the
bodies in front of them; the selected box's fill covers its visible
part and tints the cylinder in front of it at reduced alpha, its
hidden edges dimmed; the live-vs-offline mean rises from 0.9 to
2.8/255 purely from the lines the offline PNG does not draw. CPU and
CUDA alike; ctest 445/445.

Two things to know: the probe's exit with the viewport still ON
prints Cycles' guarded-allocator "MEMORY LEAK" report, because
`closeDocument` defers the view's deletion and `quit()` runs first,
so the session is never destroyed -- turning the viewport off before
exit (or letting the event loop run) leaves the report silent, and
the viewer's destructor does destroy the session. And section caps of
clipped solids still draw in colour BEFORE the blit (they are covered
by it), which is moot until clipping is translated (section 6.2's
list).

### 5.9 Budgets, denoise, pixel size (phase 4 step 13, built 2026-08-28)

What step 13 asked for was mostly already in the session's own
behaviour once step 11 ran it interactively; what remained was the
knobs and one setting that mattered:

- **Cancel on camera move** is `Session::reset()` on `setCamera()`
  (section 5.7), and the coarse restart is Cycles' resolution
  divider, on for the viewport session. `ViewportOptions::pixelSize`
  (`SessionParams::pixel_size`, Blender's preview pixel size) renders
  at 1/n resolution and scales up for a machine that needs it; 1 by
  default.
- **Budgets**: `samples` (256 by default) and `timeLimit` seconds
  (0 = none) on `ViewportOptions`, reachable from
  `view.cyclesViewport()`. No preference or UI yet -- section 5.4's
  "which view" question decides where those live.
- **Denoise is on by default**, and the setting that made that
  possible is the QUALITY. With the integrator's default
  (`DENOISER_QUALITY_HIGH`, accurate prefilter, on the CPU) the
  interactive scheduler's periodic denoise cost more than the samples
  between: CUDA reached 8 of 64 samples in 8 s where it finishes 64
  in 1.5 s undenoised, and the Debug CPU 10 of 16 in 15 s. Blender's
  viewport uses the fast quality and prefilter, and with those (and
  `denoise_use_gpu` left true, so OpenImageDenoise runs on the render
  device where it supports it and Cycles falls back to its CPU
  denoiser otherwise) CUDA settles all 64 samples inside the same
  8 s and the CPU its 16 inside 15 s. The offline `cyclesRender` is
  unchanged: no denoise, as before.

Verified 2026-08-28 with the step-11/12 probe and `denoise=True`:
the settled frame is clean at 16 spp on the CPU, and the
live-vs-offline mean moves from 2.8 to 3.5-5.0/255 -- the denoised
frame against a noisy 16-64 spp reference, which is the difference
one expects, not a defect.

### 5.10 Incremental scene updates (built 2026-08-28)

Every edit under a running viewport used to cost a new `ccl::Session`:
the CUDA context again, the kernels checked again, the whole draw list
translated again, and the render restarted from nothing. The
translator now lives as long as the session and RESTATES its scene in
place, the way Cycles' own hydra delegate edits a running session
(`src/hydra/geometry.inl`): hold `scene->mutex` (the session thread
takes it for its own update), create and delete nodes and set sockets,
tag, release, and `Session::reset()` -- the same reset a camera move
does, so the render restarts at the coarse divider on the scene it
already has on the device.

- **Reconciling the draws.** `SceneTranslator::translate()` is now the
  one entry for the first translation and every restate. It keeps the
  mesh map (key = cacheId, generation, index range, shader -- section
  6.2) and a list of placed instances, each under an instance key =
  its mesh key plus the draw's `objectKey` (the content hash of the
  node path that produced it, stable across restates). A restate
  marks every instance spare, walks the new draws, and for each takes
  a spare object of its key if there is one (two draws under one key
  take them in order), else creates one. The transform, colour and
  alpha go through the node sockets, which compare before they tag
  (`Node::set_if_different`), so a bolt that did not move costs no
  update at all; an object whose sockets did change is
  `tag_update()`ed. What no draw claimed is deleted -- objects first,
  then every mesh no instance references any more, because an object
  reads its geometry on the way out. A mesh whose content changed
  arrives under a new key (the generation, or a new cacheId), so it
  is built beside the old one and the old one is released the same
  pass. A shader cannot be deleted (Cycles does not support it), and
  "bounded by distinct surfaces" is not a bound when a section plane
  drags -- its coefficients key every shader they clip, so a drag
  mints keys without end. So a shader no live mesh references is
  RECYCLED after the pass instead: its graph is dropped (freeing its
  image handles) and the node is parked, and the next new key
  re-graphs a parked node before it creates one.
- **World and light.** `translateWorld()` remembers the `PBRConfig`
  and `OutputConfig` it baked from and returns early when they are
  equal; `translateLight()` the same with `LightConfig` plus, for a
  spot, the scene bounds its power was stated at. A changed light is
  remade (its type may change), object before light node.
- **The background blur is a camera-ray branch.** `Render_PBREnvBlur`
  asks for the environment to be drawn out of focus, which a path
  tracer cannot do by softening its world: that world is the light,
  and an environment texture has no lod to read. So above zero
  `translateWorld()` bakes the environment twice -- once at full size,
  once area-averaged down to `envBlurWidth()` -- and mixes the small
  one in on `Is Camera Ray`. Lighting, reflections and refractions
  read the sharp bake; only what is seen behind the model is soft,
  which is the same split the raster backend gets by reading a level
  of its background cubemap. Blender ships no control for this (its
  viewport Blur slider is the raster preview's only) and its users
  build the same graph by hand. Two caveats worth knowing: a camera
  ray stays one through a Transparent BSDF, so a see-through
  pass-through shows the soft backdrop; and the orthographic fan has
  to reach BOTH environment nodes, or the two halves of one sky land
  in different places.
- **Only a change resets.** `translate()` returns whether anything in
  the scene changed; `Viewport::setScene()` resets the session only
  then. `feedCyclesViewport()` runs before every frame and restates on
  any bump of the backend's scene generation, so most restates are
  no-ops (the counters `added`/`removed`/`restated`/`built`/`released`
  all zero) that leave the render refining untouched. A selection
  never reaches the translator at all -- it is a separate feed
  (`SoFCRenderer::feedExternal` restates the scene and ADDS the
  selections), so it costs no reset. The camera is treated alike:
  `translateCamera()` compares against the camera last stated and
  returns false when equal, and `applyCamera()` resets only on true.
- **One conservative reset, by the cache's own rule.** A `ShapeColor`
  edit is cheap: the colour rides the object, the mesh keeps its
  cacheId, one `restated` object, one reset. A `LineColor` edit is
  not: this fork copies the whole `SoFCVertexCache` for a line-colour
  variant, minting a new cacheId for the TRIANGLE arrays too, so the
  triangle mesh arrives under a new key and is rebuilt (`built 1`,
  `released 1`) and the render resets -- even though the lines it
  changed are the host's overlay the translation skips. Keying on the
  cache contract (cacheId + generation) cannot see that the new arrays
  are byte-identical to the old; a content hash could, at a cost per
  restate the common edit does not deserve. The reset is the honest
  answer, and rare in practice.
- **When a session is still torn down.** The first scene; a session
  that reported an error (the rebuild is the retry); and a flip of the
  output transform's colour management, because the translator decodes
  authored colours on its way in (section 6.1) and a managed and an
  unmanaged translation are different scenes.
- **Counters.** `ViewportStatus::sessions` (device set-ups) and
  `updates` (restates in place), in the Python status dict, so a probe
  can assert the session count stays at one.

### 5.11 A Cycles cell beside the normal view (built 2026-08-28)

The unified canvas (`docs/SplitViews.md` sec 13) draws every 3D cell
of a split view as one sub-view of ONE backend, in one
`Renderer::renderSubViews` frame. Turning the viewport on in a cell
did nothing there, for two reasons, and one of the three pieces the
plan named turned out to be already in place.

- **The feed ran only on the single-view frame.** `feedCyclesViewport`
  was called from `View3DInventorViewer::renderScene()`, which a
  canvas cell never runs -- `ViewAreaCanvas::paintGL` renders all the
  cells itself. So a cell's session was never given a scene or a
  camera.
- **The consumer was one per backend.** `setFrameConsumer` held a
  single consumer and a single `externalBase` flag, and the canvas
  shares one backend across its cells. Had the feed run, every cell
  of the canvas would have drawn the same path-traced image and given
  up its raster shading.
- **The blit was already per cell.** The plan's piece (b) -- scope the
  consumer's draw to the sub-view rect -- was free: `renderSubViews`
  renders each sub-view into that sub-view's BANK targets (sized to
  the cell, `captureWidth` as the sizing channel) and then blits the
  bank into the cell rect, and `FrameBind` hands the consumer the
  bank's framebuffer and size. `hostTarget()` inside a sub-view submit
  IS the cell.

What was built:

- **Consumer slots per sub-view.** `Renderer::setFrameConsumer`,
  `frameConsumerSurface` and `setExternalBaseLayer` take a `subView`
  (default 0, the implicit full-canvas sub-view every plain `render()`
  draws, so the CAM simulator and the single-view path are unchanged).
  The bgfx backend keeps a `std::map<int, ConsumerSlot>` -- consumer,
  surface, pass counts, external-base flag -- and the frame path
  resolves the slot of the submit in progress right where it swaps in
  the sub-view's bank (`selectConsumer` beside `selectSubView` in
  `BGFXFrame.cpp`), into the same members every downstream read
  already used, so the pass declarations, the depth-only rasterization
  and the bind all follow per cell without a use site changing.
  `dropSubView` drops the slot with the bank.
- **The feed from the canvas.** `ViewAreaCanvas::paintGL` calls
  `View3DInventorViewer::feedCanvasCyclesViewport` for every cell it
  draws, before `renderSubViews`, with the cell's camera at the cell's
  size, the frame's background, and the FEEDER viewer. The scene comes
  from the feeder's render cache: a non-feeder cell is hidden and
  never traverses (its own cache is empty or stale), while the feeder
  is exactly the traversal that stated the backend's scene -- so the
  generation counter the feed compares against describes the same
  draws. The camera and the render settings (PBR, output transform,
  light, background) are the cell's own; the consumer is registered
  under the cell's claim id.
- **Detach on every backend swap.** `Private::detachCyclesConsumer()`
  takes the consumer off the backend it is registered on, if that is
  still the one the viewer holds, and every path that replaces
  `renderer` calls it first: `adoptRenderer` (both joining a canvas
  and getting a backend back), `setRendererType`, and turning the
  viewport off. With a lone backend this was moot -- the registration
  died with the backend -- but a canvas's instance outlives the cell
  that leaves it, and would go on calling a consumer whose view is
  gone. The feed re-registers on the next frame whenever the host, the
  sub-view or the surface differ from what it registered
  (`cyclesSubView` beside `cyclesHost`).

Behaviour: a cell path traces under its own camera and its own
session while the cell beside it rasterizes; two cells may both path
trace; an edit restates every cell's session in place from the one
cache (section 5.10); turning a cell off returns it to the raster
frame; closing a cell -- session running -- tears its session down
with the view, and the survivor, handed its own backend, keeps
tracing on the single-view path.

Known gaps:

- `view.cyclesRender(...)` (the offline render, section 6.2) on a
  non-feeder cell reads that cell's own cache, which the canvas does
  not traverse. The probe takes its references through the feeder
  with the cell's camera copied in; a product command would want the
  same detour, or the offline path taught the feeder as the live one
  was.
- The cell's raster neighbours show the feeder's render settings (one
  backend, one config) while the Cycles cell renders its own. Same
  document, usually the same settings; noted, not addressed.
- No UI beyond the Python call and `bindView` for the linked camera:
  the "which view" question of section 5.4 is answered on the
  mechanism side only.

Verified under xvfb with a probe (scratchpad `cycles_cell_probe.py`,
`probe.sh <device> <spp> <tag>`; unified canvas on, NaviCube and
corner cross off, output transform sRGB): split right, the original
cell active and feeding, the new one on the right. The probe grabs the
canvas framebuffer, crops each cell's rect, and compares against an
offline `cyclesRender` of that cell's camera at the cell's size; the
non-feeder cell's reference goes through the feeder with the camera
copied by `copyFieldValues` (the `getCamera()`/`setCamera()` string
round trip reframes the view).

- CPU, 16 spp, 28 checks, 0 failures. Cycles on in the non-feeder
  cell alone: its frame matches the offline render at mean 0.04/255,
  the feeder cell is still the raster frame (mean 0.0 against the
  frame before), 5 objects from the feeder's cache, one session. Both
  cells on: 0.03 and 0.04. The right cell's camera moved to a top
  view: 0.03 both. An edit (the link moves): one `restated` object in
  each session, no new session, 3.1 and 2.6 (the sampling restarts on
  a different pattern). Right cell off: back to the raster frame
  (mean 28 against its traced frame), the left keeps its session.
  Closing the active cell with its session running: the survivor gets
  its own backend, the canvas stands down, and the lone view's
  session refines on the single-view path (5.3, the feature lines
  the offline render has none of).
- CUDA, 32 spp, the same 28 checks, 0 failures: 0.04 / 0.03 and 0.04
  / 0.02 and 0.03 / 3.0 and 2.4 / 0.03 for the lone view, every settle
  under a second.

Two probe traps, neither a code defect: the first runs failed every
compare at mean 13-17 with a black band under each cell -- the report
view had auto-raised on a console message and taken 30 px off the
canvas, and the fixed-size crops padded the missing rows; the
`OutputWindow/checkShowReportView*` prefs off cure it. And the canvas
frame draws no feature lines in these cells (raster or traced), so the
edge-over-blit route of section 5.8 is exercised only by the lone-view
step here.

### 5.12 Retiring a session off the calling thread (2026-09-04)

Releasing a Cycles session used to freeze the application, for as long
as a first-ever GPU kernel compile takes.

The chain is short. `~Session` cancels and then joins its own thread
unconditionally (`session/session.cpp`). The cancel calls
`device->cancel()`, whose base is empty and which only the Metal
backend overrides. And the CUDA backend's kernel load compiles with a
plain blocking `system("nvcc ...")` (`device/cuda/device_impl.cpp`),
which nothing can interrupt. So a session released while its thread is
inside that compile waits for nvcc to finish, and every release we make
was on the thread that asked: a closing 3D view, a preview switched
back to Raster, an editor closing, all of them the GUI thread.

The compile itself was never the problem. It has always run on the
session's own thread, which is why the pane keeps answering while it
runs. Only the teardown was synchronous.

A session is therefore not destroyed where it is released. It is handed
to a worker with the translator that states its scene (which points
into it, so the two travel together and go in that order), and the
caller returns at once. The worker destroys them one at a time, in the
order they were retired: two devices tearing down at once is not
something to ask of a driver.

Three things make that safe.

- **The driver must not hold the viewport.** The staging display driver
  woke the host through a lambda capturing `this`, which a retired
  session would call after its viewport was gone. It calls a
  `RedrawGate` held by shared pointer instead. `call()` runs under the
  gate's lock, so `close()` returning means no thread is still inside
  the callback and no later one will enter. The viewport closes the
  gate before it retires; each session gets its own.
- **The mutex and condition variables are never freed.** A detached
  worker parked in `wait()` for the life of the process hangs
  `pthread_cond_destroy` at exit, which is the trap already fixed once
  in `Part/Gui/MeshLevelSource.cpp`. They are leaked heap objects, as
  they are there.
- **The application drains on its way out.**
  `Render::Cycles::waitForRetiredSessions()`, called at the top of
  `Gui::Application::~Application`, waits for the queue to empty. Past
  that point the process starts unloading what the worker is still
  inside. It says so on the console rather than looking hung, which
  matters when it does have to wait.

What it costs: while one session is retiring, its replacement is
already alive, so two devices hold their memory for the overlap. On a
preview that is nothing; on two large scenes it is real, and it is the
price of not freezing.

Then the engine's own half of it, in our Cycles fork (`057c2c87b`):
the compile can be stopped, and a stopped one leaves nothing behind.

`Device::cancel()` exists upstream for exactly this -- its comment says
"cancel any long running device operations (e.g. shader compilations)"
-- but outside Metal nothing implemented it. `util::Subprocess` runs the
compiler as a child in a process group of its own (`posix_spawn` with
`POSIX_SPAWN_SETPGROUP`) and kills the group on cancel from another
thread. The group and not the process: nvcc drives cicc and ptxas, and
killing the shell or nvcc alone leaves the work running. The CUDA and
HIP devices own one and override `cancel()` (OptiX and HIP-RT inherit
it), `MultiDevice` forwards to its sub-devices, and a compile stopped
this way reports no error -- being asked to stop is not a failure.

**Windows takes the whole tree too, through a job object** (built
2026-09-04 on the Windows side of this same laptop -- sec 4.1 -- which
is where nvcc, the real `nvoptix.dll` and the HIP SDK all are).
`system()` gives the caller no handle to anything, so the command is
started by hand instead: `CreateProcessW` on `cmd /s /c`, **suspended**,
the process put in a job object created with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, then resumed and waited on by
handle, with `cancel()` calling `TerminateJobObject`. Suspended is not
a detail: a shell that got as far as starting nvcc would leave it
outside the job and out of the cancel's reach. `KILL_ON_JOB_CLOSE`
covers the paths that never reach a cancel as well -- whatever is still
in the job dies when `run()` drops the last handle to it.

Two things the sketch that stood here did not have:

- **`/s`.** It makes cmd's quoting rule the simple one -- strip the
  first and last quote, take the rest of the line verbatim. Every
  command that gets here carries quoted paths of its own, which the
  rule without `/s` counts and then takes apart.
- **The child is handed exactly the three standard handles**,
  duplicated inheritable and passed in a
  `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. `system()` ran the compiler on
  the caller's handles, and a compiler's diagnostics are the "see
  console for details" that a failed compile refers to, so they have to
  keep arriving wherever the application's output goes -- a redirected
  log included. The handle list keeps that without the other half of
  what `system()` does, which is to inherit everything inheritable in a
  process this size. Duplicating also keeps the list free of the
  repeats it rejects, since stdout and stderr are commonly one handle.

If the job cannot be created or joined, the command still runs; it runs
uncancellable, which is exactly what it did before, and says so.

A killable compile makes a half-written kernel likely rather than
merely possible, and the cache could not survive one: all three
backends wrote straight to the final path, and a cache hit is an
existence check, so a truncated file would be loaded forever after.
That was already reachable without any cancel, by two devices building
the same uncached kernel at once. They now compile to a path of the
process's own (`path_temp_for`) and move it onto the real name
(`path_rename`) once the compiler has succeeded, removing the temporary
on every failure. That half is platform-independent and covers Windows
as well.

Together these turn the drain above into a formality: the cancel that
`~Session` already issues now reaches the compiler, so the join it
waits on returns in about a second instead of at the end of the
compile.

Verified with `~/works/sw/fcad-probes/shader_graph_cold_teardown.{py,sh}`,
which gives the process its own `XDG_CACHE_HOME` so the compile is
guaranteed cold and the box's shared kernel cache -- a serving rig uses
it -- is never touched. With nvcc and cicc confirmed running, switching
the preview back to Raster (which stops the tracer and destroys its
viewport) left the GUI thread's own heartbeat timer with a longest gap
of 0.11 s against a 0.1 s interval, the editor still drawing and the
document still recomputing. Before the change the same release blocked
that thread for the whole compile.

With the engine's half in as well, the compilers were gone a second
after the release and the cache was left empty -- which is the torn
file not being written. A second, uninterrupted attempt compiled for
267 s, left exactly one `.cubin` and no temporary, and the traced frame
followed a second later with no tracer error. The quit was immediate;
the run before the cancel existed spent 274 s of its exit waiting for
the same compile.

What the probe measures deserves care. Its first version judged the
second compile by "a sphere is on the pane", which passed in one second
because the raster frame from the mode switch has one -- it proved
nothing at all. A compile is judged by the engine's own finish line and
by what the cache directory holds.

**Verified again on Windows** (2026-09-04, native `BUILD_CYCLES=ON`
build, FreeCAD under cdb; probes in
`D:\works\sw\fcad-probes\win_kernel_compile_cancel`). The cold cache is
arranged differently there and it is worth knowing: `path_cache_get()`
has no XDG branch on Windows, so the kernels land in `cache\kernels`
**beside the binary**, and a cold compile is arranged by moving that
directory aside rather than by setting a variable.

With `FreeCAD -> cmd -> nvcc -> cmd -> cicc` confirmed running,
`view.cyclesViewport(False)` returned in **0.003 s** and the GUI
thread's own heartbeat timer showed a longest gap of **0.124 s**
against its 0.1 s interval; every process in that tree was gone and the
cache directory was left empty -- no `.cubin`, no temporary. The
second, uninterrupted attempt compiled for **283 s**, left exactly one
19,005,168-byte `.cubin` and no temporary, and the session reached
"Rendering Done, Sample 16/16". A third cold compile released with the
quit right behind it put the process out in about a second with nothing
of the compiler left running: the drain covering the overlap, rather
than waiting out a compile.

**HIP was measured the same way and is the deeper tree.** hipcc is a
batch file, so the command is `call hipcc ...` and the job holds
`cmd -> hipcc -> cmd -> clang -> clang`. The release returned in
0.002 s, the heartbeat's longest gap was 0.12 s, and all five were
gone. Sec 4.2's point holds -- AMD is only reachable from the Windows
side of this box -- and this is the first work to have needed it.

The mechanism itself was taken separately, since a kernel compile is a
slow way to ask a small question: a test against the tree's
`cycles_util.lib` runs a command that starts a grandchild of its own
and appends to a log once a second. Cancelled once that grandchild was
demonstrably alive, `run()` returned 0.62 s into a command with a
minute to go, reported -1 rather than the kill's exit status, and
**the grandchild's log stopped
growing** -- which is the difference between taking the tree and
killing the process that was started. It also holds the quoting and the
inherited stdout above, and that closing the job takes what a finished
command left running.

Two traps for the next scripted quit here, both of which cost a
measurement. `mainWindow.close()` and, in Qt 6, `QApplication.quit()`
alike stop at the "Unsaved document" modal, which a script does not
see: an exit that "took 420 s" was that box waiting for an answer while
the compile it was meant to interrupt ran to completion. Close the
documents first (`App.closeDocument` discards). And a document that was
just saved is not therefore unmodified -- the view's own state touches
it again.

## 6. Scene translation

The bulk of the real work, and the fork is unusually well placed for
it because two earlier workstreams already did the hard part.

- **Geometry: `docs/TShapeRenderCache.md` maps 1:1 onto Cycles.** That
  cache already shares tessellation at TShape level and expresses
  instances as transforms over shared geometry. Cycles' own model is
  exactly Geometry (Mesh) plus Object (an instance with a transform).
  The dedup carries across; it does not have to be rebuilt.
- **Materials: the PBR work lands almost field-for-field on Principled
  BSDF.** The metallic/roughness reinterpretation of `ShapeAppearance`,
  the Khronos spec-gloss solve, and the measured-F0 metal presets are
  all already in the vocabulary Cycles speaks. Material translation is
  close to a field copy, not a research problem.
- **Per-face appearance and per-face texture already exist** and map
  onto Cycles' per-mesh shader slots.
- **The environment**: the engine already has an IBL environment; it
  becomes the Cycles background shader's environment texture.

So the translation layer is: TShape render cache -> Cycles Scene
(Mesh, Object, Shader, Light, Camera, Background).

### 6.1 The colour trap, already hit once

**The engine is colour managed and on by default, and Cycles outputs
LINEAR float.** Commit `b3f82b620b` was exactly this bug in the CAM
simulator -- its image was sRGB-encoded twice. The Cycles blit must
enter the existing output transform as linear and must not arrive
pre-encoded. Material colours likewise go to Cycles linear, matching
the rule already recorded for the bgfx path.


### 6.2 What the translation does (phase 3, built 2026-08-28)

`src/Gui/Renderer/CyclesScene.cpp` (`SceneTranslator`, internal header
`CyclesSceneP.h`) turns a `Render::Cycles::SceneInput` into a
`ccl::Scene`. The input is the backend-neutral contract every backend
already consumes -- `Render::DrawCallList` (MeshData + Material +
model transform), `PBRConfig`, `OutputConfig`, `LightConfig`,
`Background`, and the two GL-layout camera matrices `render()` takes
-- so the layer names no Gui type by construction (section 7). The
Gui side is `View3DInventorViewer::renderWithCycles()`: it translates
the view's render cache with `RendererBridge::translate` (the same
call `SoFCRenderer::feedExternal` makes for the bgfx backend), reads
the configs off the view's settings with the bridge's per-view
translators, takes the camera off the Coin camera, and hands the
result to `Render::Cycles::renderScene()`. Python:
`view.cyclesRender(path, width, height, samples, device)`, returning
the translation report (meshes, objects, shaders, triangles, skipped,
seconds).

- **Geometry.** Only filled triangle draws. Each distinct
  (cacheId, generation, index range, shader) becomes one `ccl::Mesh`
  and every draw of it one `ccl::Object` with the draw's transform,
  so the render cache's instancing carries across unchanged: a link
  of a body is a second Object on the first body's Mesh. Vertex
  normals ride `ATTR_STD_VERTEX_NORMAL` with smooth triangles; a
  cache without normals shades flat.
- **Colour lives on the Object.** The uniform shader graph reads
  `ObjectInfo.Color` / `ObjectInfo.Alpha` into the Principled BSDF's
  base colour and alpha, so colour variants of one mesh share both
  the mesh and the shader. That is the trick Blender uses for its
  object colour, and the same split the TShape render cache made on
  the way in (`docs/TShapeRenderCache.md`). Shaders are keyed on the
  rest -- metallic, roughness, emissive, glass IOR -- and deduplicated
  across the scene.
- **Per-vertex and per-face material** draws (`pervertexcolor`, the
  12-byte `MeshData::materials` stream) resolve their surface per
  vertex on the CPU and carry it as three vertex attributes
  (`fc_base`, `fc_pbr` = metallic/roughness/alpha, `fc_emissive`)
  read by ONE shared attribute shader. A vertex-painted mesh with a
  thousand colours is one shader, not a thousand.
- **Material arithmetic is the bgfx path's, ported.** The Khronos
  spec-gloss solve (`fcBaseFromSpecular`) where nothing states a
  metalness and `PBRConfig::fromSpecular` asks for it; the
  shininess-to-roughness mapping the frame selects
  (`BGFXView::setTriangleFrameState`, the odds transform or the GL
  exponent, then the fourth root); authored colours and the byte
  streams decoded to linear when the output transform is colour
  managed. `Render_Glass` becomes transmission at the stated IOR and
  roughness, and its tint an absorption volume on the shader's
  Volume output: Cycles' absorption closure weighs
  `(1 - Color) * Density`, which is the `sigma = density *
  (1 - diffuse)` Beer-Lambert of `fs_fc_glass.sc` exactly, so the
  object colour drives it and the density (`Render_GlassDensity`,
  per scene unit; `<= 0` = automatic, `3 / bounds diagonal` as in
  the bgfx glass pass) joins the shader key. The volume is not put
  through the section clip test -- reading the position would make
  it heterogeneous (ray marched) -- so a sectioned tinted body still
  absorbs over its removed part; `Render_Light` bodies and unlit
  draws become emission;
  `transparent` draws and per-vertex alpha go through the BSDF's
  alpha.
- **Environment.** The procedural presets were `BGFXView` members;
  they are now `Render::envRadianceProcedural` / `sampleEnvImage` in
  `Environment.h`, shared by the cube map and by Cycles. The Cycles
  side bakes the environment (preset or user picture) to an
  equirectangular float image served through a custom
  `ccl::ImageLoader` into an `EnvironmentTextureNode`, at
  `envIntensity`. The film's exposure is `OutputConfig::exposure`.
  The environment is the visible background only where the engine
  would show it -- PBR on AND `envBackground` (the flag defaults to
  true, but a Phong frame draws its gradient regardless); otherwise
  the film is transparent and the PNG
  writer composites over the view's flat or gradient background --
  the same composition the viewport will do (host background, Cycles
  blit over it).
- **Light.** `LightConfig` becomes a `SunLight` (strength = colour x
  intensity, half a degree of disc) or a `SpotLight` whose radiant
  power is the config's irradiance at the distance to the scene
  centre, so a spot lights the model as brightly as the sun would.
  Without a light the environment is the only illumination, which for
  a path tracer is the honest reading of a view with no light.
- **Camera.** Cycles looks down its own +Z; the camera matrix is
  `inverse(view) * scale(1, 1, -1)`, Blender's own flip. The
  projection is read back into the near/far clip and the VIEWPLANE
  rather than a field of view, which keeps off-centre projections (a
  boxZoom, a tiled capture) honest; perspective uses a 90 degree
  fov so the plane is stated in tangents, orthographic in camera
  units. `farclip` MUST be set: Cycles defaults it to 1e4, which is
  inside a millimetre-unit model.

**Skipped, and counted in the report:** lines and points (the host's
overlay, section 5.1), wireframe draw styles, on-top draws,
navigation gizmos (`skipbounds`), stand-in boxes, and the
water/cloud/fire/fountain bodies (volumes of the volumetric pass).
**Not yet translated:** textures (unit-0, per-face, bump, emissive,
metallic-roughness maps), surface finishes, section clip planes and
caps, and user shaders. Each is a later step of phase 3, not a
design gap: Cycles has an image texture node, a displacement path,
and the clipping can be done with the same mesh-cutting the section
caps use. Clip planes and caps landed after this was written
(sections 6.3 and 6.4), and the texture maps after that (section
6.5), the finishes (6.6) and the per-face palettes (6.7) -- phase 6
item 15's phase A (section 8) complete; user shaders are its phase B.

Verified 2026-08-28 (scratchpad `cycles_scene_probe.py` under xvfb: a
floor, a red box and an `App::Link` of it, a six-colour per-face box,
a Phong "gold" cylinder -- near-black diffuse, gold specular -- and a
half-transparent sphere): report `meshes 5, objects 6, shaders 3,
triangles 522, skipped 12` -- the link shares the box's mesh, the 12
skipped draws are the edge and vertex sets. The gold reads as metal,
the per-face colours arrive per face, the sphere is translucent, the
gradient background composites under the transparent film. Camera
checks: the sphere renders as a circle in both projections, the
orthographic sphere is 29 px for a 369.86 camera height over 384 rows
(as computed), and the floor spans the same pixel range as the live
viewport grab in both projections. CPU (Debug) 640x480 at 64 spp:
21 s; CUDA: 0.5 s, and the two agree to 4/255 at most. Trap on the
way: `view.saveImage(path, w, h)` is NOT a framing reference -- Coin's
offscreen render keeps the viewer camera's viewport adjustment, so at
another aspect its picture is squeezed; use `saveRenderDump(source=
'framebuffer')` at the viewport's own size.

Traps this step paid for:

- `ccl::Attribute` has no `data_float3()` in this Cycles; the accessor
  is `data_for_write<T>()`, and T must be the STORAGE type: a normal
  attribute stores `packed_normal`, any other float3 attribute
  `packed_float3` -- `float3` itself is 16 bytes on SSE and trips the
  element-size assert (SIGABRT in a debug build).
- The camera-invisible flag is `PATH_RAY_VISIBILITY_CAMERA`, not
  `PATH_RAY_CAMERA` (which is a different bit set).
- The kernel's equirectangular lookup is `u = 0.5 - atan2(y, x) /
  2pi`, `v = 1 - acos(z) / pi`, and a loader-supplied image has row 0
  at v = 0 -- the nadir. Bake with that or the sky ends up on the
  floor.
- `ImageMetaData` for a loader image: `channels = 4`, `type =
  IMAGE_DATA_TYPE_FLOAT4`, colorspace scene linear; the pixel buffer
  is then width x height x 4 floats, no `conform_pixels` needed.


### 6.3 Section clip planes (built 2026-08-28)

The first of the phase-3 leftovers listed above. A section in this
engine is not a post-process and not a stencil: **the shader graph
removes the clipped points**, so the section cuts for every ray --
camera, shadow, reflection, transmission -- at once.

`Material` already carries the section as up to six world-space plane
equations plus `clipconcave` (the bridge fills them from the
`SoClipPlane` nodes the traversal saw). The translation copies them
into a `SceneTranslator::Clip` and appends them to the SHADER key,
because Cycles has no clip-plane state to set: the planes are part of
what the shader IS. `connectSurface()` then wraps the closure --

- `GeometryNode`'s Position (world space, so the object transform
  needs no undoing) dotted with each plane normal, plus the plane's
  w: positive on the kept side;
- the readings folded with a `MINIMUM` (survive EVERY plane) or, in
  concave mode, a `MAXIMUM` (survive ONE) -- the GL parity the bgfx
  backend keeps, where SectionConcave renders one plane per pass and
  the picture is the union of half-spaces;
- `LESS_THAN 0` on the fold as the `Fac` of a `MixClosureNode`
  between the surface and a `TransparentBsdfNode`.

A transparent closure is the right eraser: it passes every ray
through unchanged, so the clipped part casts no shadow and shows in
no reflection, which is what the raster path's discarded fragment
also means. Geometry is untouched -- the report still counts one mesh
and one object for a clipped box -- so an edit to the section costs a
shader, not a retranslation, and the instancing is undisturbed.

Both Gui feeds (`renderWithCycles` and `feedCyclesViewport`) now read
the view's section style the way `SoFCRenderer` does --
`Gui::sectionStyle(settings, "Concave"/"NoOnTop", ViewParams...)` --
instead of passing a default-constructed `SectionOnTop`, which had
made a concave section trace as a convex one.

Verified 2026-08-28 (scratchpad `clip/probe.py` under xvfb, top-down
orthographic on one 40 mm green box, 320x320): unclipped 34074 green
pixels; one plane at x >= 20 leaves **0.500** of them and the centre
of mass moves right (159.5 -> 205.6); a second plane at y >= 20
leaves **0.251**; the same two in concave mode leave **0.751**. One
mesh, one object, one shader in every leg. CUDA agrees with CPU to
0.1% on every count.

Still not translated when this was written (the rest of the 6.2
list): textures, surface finishes, the section CAPS -- the cut is
honest but hollow, which is section 6.4 -- and user shaders. The caps
are section 6.4, the texture maps section 6.5; finishes and user
shaders are phase 6 item 15.


### 6.4 Section caps (built 2026-08-28)

Section 6.3 made the section cut; this fills it. The bgfx backend
caps a section with a **stencil**: mark the clipped solid's back
faces, draw a screen-aligned quad over the plane, keep the marked
pixels. A path tracer has no stencil and no screen-space pass to
hang one on -- so here the cap is **real geometry**, a cut face
translated into the scene like any other mesh.

**Which draws.** The eligibility is the bgfx backend's, so the two
engines cap the same things: a whole-object triangle draw
(`partIndex < 0`), sectioned (`numclipplanes > 0`), of a SOLID
(`Material::solidshape` or `MeshData::hasSolid` -- an open surface
has no inside to fill), with the style asking for a fill
(`SectionConfig::fill`, or concave mode). `SceneInput` carries a
`SectionConfig` for this, filled by both Gui feeds from
`RendererBridge::translateSectionConfig`.

**The cut face** (`buildCapTriangles`). Every triangle that crosses
the plane contributes one segment -- a vertex exactly on the plane
counts as kept, so a triangle yields either no crossing or exactly
two -- and those segments are the closed boundary of the cut region
in the plane's own 2D frame. The fill is a **trapezoidal sweep**:
sort the segment endpoints by height; between two consecutive
heights no segment begins or ends, so every span across that band is
bounded by two straight edges and the quad between them IS the
region. Exact, not a stair-step. Pairing the crossings **even-odd**
is what leaves a bore in the section open, and it asks nothing of
the mesh's winding -- which is why this needs no loop chaining, no
nesting test, no hole bridging and no ear clipping, the four things
that make a general polygon triangulator hard to get right. An
active-edge list keeps it linear in the segments rather than
quadratic. The triangles are wound for a geometric normal of -n:
the solid is on the plane's positive side, so the cut face looks the
other way.

**Where it is built.** In the mesh's OWN space, with the plane
pulled back through the model transform (`n_local = R^T n`,
`w_local = w + dot(n, t)`), so the cap rides the draw's transform
like the geometry it caps and two placements of one mesh keep their
own caps. The cap is a mesh + object like any other instance, keyed
so the restate of section 5.10 reconciles it with everything else: a
moved section plane replaces the cap meshes and leaves the model's
alone.

**The cap's own shader.** The fill colour is the raster path's --
`SectionConfig::fillInvert` and the same `invertCapColor`
complement -- and it rides the object, as every colour here does.
The surface is deliberately NOT the draw's: never emissive, never
transmissive and never metallic, because a glass cap would show
exactly nothing, which is the one thing a cap must not do. Only the
roughness carries over. The cap is clipped by the OTHER planes and
never by its own (that would put it on its own knife edge, where the
sign of a zero decides whether the fill exists); concave mode leaves
it unclipped, the state GL renders it in.

**Not translated**: the hatch texture (`hatchEnable`/`hatchScale`)
and `fillGroup`. The hatch is a raster device; grouping exists in
the stencil path so that touching solids share one cap quad, and
here each solid builds its own cut face, which needs no grouping to
be correct.

Verified 2026-08-28 (the same `clip/probe.py`, legs 5 and 6, CPU and
CUDA): a 40 mm box cut at z <= 20 shows from above a cut face
covering **1.014** of the unclipped silhouette with **0** green
pixels left -- the fill, not the top of the box; a torus (R 20, r 8)
cut through its equator shows an annulus of 31896 fill pixels with
**0** of them inside a third of the outer radius, so the bore the
even-odd pairing has to leave open is open, on a curved boundary.
Object and mesh counts follow the caps exactly: 1/1 unclipped, 2/2
with one plane, 3/3 with two.


### 6.5 Texture maps (phase 6 item 15, phase A, built 2026-08-29)

The first of the node-graph work of item 15 (section 8): the maps a
draw's material carries now sample in the shader graph instead of
being dropped. `SceneTranslator::Maps` (`CyclesSceneP.h`) resolves
them from the `Render::Material` -- the unit-0 picture with its GL
texture environment, the bump or normal map, the emissive and
metallic-roughness maps -- under the raster path's rule that a mesh
without texture coordinates carries none. `applyMaps` builds the
nodes on a `SurfaceLinks` set (base, alpha, metallic, roughness,
emission, normal), so the same code serves the uniform shader, where
the surface's own values arrive from ObjectInfo and the BSDF's
constants, and the attribute shader, where they arrive from the
per-vertex attributes; a map multiplies into whatever fed the socket
before it.

- **Pixels.** `PixelImage`, a `ccl::ImageLoader` over the
  `TextureImage` the render cache holds (the `BakedEnvironment`
  pattern of section 6.2): channels expanded to four, rows bottom-up
  as GL and Cycles' own loaders have them, alpha `channel_packed`
  (coverage, never premultiplied, never decoded), one upload per
  distinct image (`equals` by textureId and colour space). A PICTURE
  (base, emissive) declares `srgb` when the pipeline is colour
  managed so Cycles decodes it on upload, as `fc_mesh_fs.sh` decodes
  it per sample; a DATA map (bump, normal, metallic-roughness)
  declares `data` and is never decoded.
- **Coordinates.** `ATTR_STD_UV` per corner from `MeshData::texCoords`,
  with the GL texture matrix folded in on the CPU (a mapping node
  cannot express a shear; the matrix keys the mesh). A transformed
  texture is therefore a different `ccl::Mesh`, exactly as a
  different index range is.
- **Texture environment.** The four GL models as the fragment shader
  applies them: Modulate multiplies colour and alpha, Decal mixes by
  the texel alpha, Blend is `base * (1 - texel) + blendColor * texel`
  per channel, Replace takes the texel (and its alpha only when the
  source format has one). Alpha goes to the Principled Alpha, so the
  raster path's `discard` below 0.004 is the same transparency.
- **Emissive map.** Added to the emission colour AFTER the texture
  environment (glTF semantics): the constant emission times its
  strength folds into the addend and the strength becomes 1.
- **Metallic-roughness map.** Green multiplies the roughness, blue
  the metallic (glTF), through Separate Color + Math nodes.
- **Bump.** A one- or two-channel map is a height into a Bump node;
  Cycles differentiates the height graph itself. The raster path
  tilts the normal by `strength * dh/dUV`, Cycles by
  `distance * dh/dP` with P in object units, so the distance is the
  strength times the millimetres one UV unit spans -- the root of the
  ratio of the range's area in object space to its area in texture
  space (`Maps::uvScale`). A three- or four-channel map is a
  tangent-space normal map (Coin's, i.e. OpenGL's, convention); the
  tangents come from the UVs and the vertex normals, which Cycles
  derives on its own when the node asks, so a mesh without normals
  shades unbumped rather than wrongly. Parallax has no path-tracer
  meaning and is not read; `BumpConfig` rides `SceneInput::bump`
  from the three feeders for the strength.
- **Not translated, on purpose.** The occlusion map: a path tracer
  computes its own occlusion. The surface finishes are section 6.6,
  the per-face palettes section 6.7.

Verified 2026-08-29 (`maps/probe.py` under xvfb on the debug tree --
the only one with Cycles on -- top-down orthographic on a 40 mm box,
256x256, 32 spp, CPU and CUDA): a red/blue checker as
`Render_BaseColorTexture` lands its four cells where the raster
control frame has them (linear means 0.55/0.04/0.045 against
0.037/0.04/0.649), a green disc as `Render_EmissiveMap` reads 1.0
green at the centre over an unchanged checker, and a grayscale ramp
as `Render_NormalMap` darkens the lit top uniformly (0.55 -> 0.39) as
a constant tilt should. Two traps paid for on the way: Coin's texture
coordinates are `(s, t, r, q)` per vertex, not two floats -- read at
the wrong stride every corner sampled the (0, 0) texel and the box
rendered one flat colour; and a custom `ImageLoader` must write the
type the FINALIZED metadata asks for, since declaring `u_colorspace_srgb`
makes Cycles promote a byte image to half floats for a conversion pass
(bytes written into that buffer rendered the box invisible or as
streaks). `scene_linear_srgb` is the byte-sRGB fast path.


### 6.6 Surface finishes (phase 6 item 15, phase A, built 2026-08-29)

The machined finishes of `fc_finish.sh` -- knurl, straight knurl,
brushed, blasted, turned, laid out in the face's projection frame or
triplanarly without one -- path-trace as node graphs.
`SceneTranslator::Finish` (`CyclesSceneP.h`) resolves the draw's own
finish and frame from the `Render::Material` under the bgfx path's
gate (a known pattern, a positive pitch and depth); `applyFinish`
builds the graph and hands its height to a `BumpNode` on the
`SurfaceLinks` normal, after the maps, so the two compose the way they
do in the raster shader. The draw's own finish and frame, or one
face's out of the palettes (section 6.7).

- **Height, not gradient.** The raster shader evaluates the analytic
  GRADIENT of a height field and rotates the normal by Mikkelsen's
  surface gradient; Cycles' Bump node wants the HEIGHT and differences
  it against the ray differentials (three evaluations of the height
  subgraph, `ShaderGraph::refine_bump_nodes`, each `TextureCoordinate`
  offset by `Filter Width` times the differential). With `Distance`
  and `Strength` at 1 the node computes
  `normalize(|det| N - sign(det) surfgrad)`, which is `fcFinishPerturb`
  -- so a height in millimetres of object space is the raster perturb
  exactly, and the finish is a function of `TextureCoordinate.Object`
  (the object-space position with the bump offset carried through the
  inverse transform, i.e. `v_opos`) so an instanced or scaled copy
  keeps it.
- **Tables.** What the raster computes per fragment is baked once per
  process into data images (`TableImage`, float texels, `data`
  colour space, cubic interpolation for a continuous slope, repeat
  extension for periodicity): the groove profile -- `pi` times the
  integral of `sign(sin) |sin|^0.45` over one period, 1024 texels, so
  `depth * table(x / pitch)` has exactly the slope `fcFinishGroove`
  states -- and two value-noise tiles baked with `fcFinishHash` on the
  cell index, wrapped at the tile (256 cells x 32 texels, one lane per
  channel for the brushed finish's three noises; 64 x 64 cells x 8
  texels for the blasted craters). The lattices are statistically the
  shader's, not bit for bit (float32 `sin` of a large argument differs
  between libms), which is the claim the probe holds them to.
- **Patterns.** Knurl = half-depth profiles on the two 45 degree
  trains; straight = one train; brushed = `depth * (0.6 + nl(y/60p))
  * (0.7 n1(x/p) + 0.3 n2(x/0.37p))`; blasted = two 2-D octaves;
  turned = the profile over the radius. The lay rotation is applied
  in-graph to the projected coordinate.
- **Frames, in-graph.** Planar: two dot products against the frame's
  axes. Radial: `atan2` about the axis, the arc snapped to a whole
  number of periods round the reference radius (the seam argument of
  `fc_finish.sh`; a frame stating no radius keeps the shader's
  per-fragment snap, which the bump's finite difference would see
  where the analytic gradient did not -- no producer emits one), and
  turning remapped to a straight knurl a quarter turn over, as the
  shader does. Triplanar: three projections weighted by
  `max(|n| - 0.25, 0)^4` off `TextureCoordinate.Normal`, the
  object-space shading normal, which the bump offsets leave alone --
  the shader's chain rule treats the weights as constants too.
- **Not carried.** The pixel-footprint fade and its roughness hand-off:
  a path tracer supersamples what the raster had to filter.
- **Debug view 2.** The verification needed a picture in which the two
  engines can be compared exactly, and lighting is not it (Cycles'
  environment is nearly tilt-insensitive and its BSDF applies bump
  shadowing; the same 17 degree flank reads as a 9% luminance drop in
  the raster and 1% in Cycles). So Cycles honours the raster path's
  `DebugViewMode` 2 (docs/RenderDebug.md sec 2.3): `SceneInput::
  debugView`, fed by the three feeders from the same
  `RenderParams::getDebugViewMode()`, replaces every surface by an
  emission of the view-space shading normal as `n * 0.5 + 0.5` (a
  `VectorTransform` to camera space with z negated for the GL eye
  convention), written raw over black exactly as `fs_fc_debug.sc`
  writes it. Unfinished shapes agree between the engines to 8-bit
  precision (mean |dn| 0.0000 on a plane, 0.0007 on a cylinder,
  0.0008 on a sphere), which is also a check of the camera and
  normal conventions on curved surfaces.

Verified 2026-08-29 (`finish/probe.py` under xvfb on the debug tree,
`probe.sh <CPU|CUDA> <spp> <tag>`, in a config home of its own): every
finished leg's Cycles normal view is held against the ANALYTIC
perturbed normal, `fc_finish.sh`'s own arithmetic in numpy at every
pixel (`normalize(N - tangential(grad))`), as the difference from the
unfinished view. A 40 mm `Part::Plane` (planar frame): straight knurl
corr 0.999, RMS 0.1722 vs 0.1730, max 0.306 vs 0.300 (the designed
sin(atan(pi * 0.2 / 2)) = 0.299); straight at a 30 degree lay 0.999
on both axes; diamond knurl 0.999; turned 0.999; brushed and blasted
RMS within 11% and 3% of the analytic. A `Part::Cylinder` r 10 (radial
frame, Face1 the lateral face): straight knurl corr 1.000, RMS 0.1537
vs 0.1539; turned 0.97 / 0.999; diamond knurl 0.999, RMS 0.1186 vs
0.1187. A `Part::Sphere` (no analytic frame, triplanar): straight
0.985 / 0.997, blasted within 3%. CUDA reads the same numbers as the
CPU (0.301 / 0.533 at depth 0.2 / 0.4 on the tilt probe). Two things
found on the way: the raster's mode 2 is the PREPASS normal, without
the finish, so the raster frames serve as the framing check only;
and a producer bug -- removing a finish dropped the render material
node and with it the projection frames, and the once-only
`renderGeometryAsked` never asked for them again, so a finish restated
in the same session shaded triplanarly in both engines (fixed:
the flag resets with the node).


### 6.7 Per-face palettes (phase 6 item 15, phase A, built 2026-08-29)

A per-face appearance puts a finish, a projection frame and an image on
each face of one draw. The raster path, bound to one program and one
sampler per draw, carries them as PALETTES -- `FinishPalette`,
`FramePalette`, `TexturePalette` on the `Render::Material` -- with one
index per vertex in the material stream's third slot (bytes 8, 9, 10 of
`MeshData::materials`), and selects in the fragment shader out of
uniform arrays and a 2D array texture. A path tracer has no such
constraint, and Cycles has a native answer to "a different material per
face": a mesh's per-triangle SHADER SLOT (`Mesh::shader`, an index into
its `used_shaders`), the mechanism Blender's material slots ride. So
the translation does not port the palettes as palettes:

- **A shader variant per combination present.** `translateDraw` walks
  the draw's triangles once, reads the three indices off each
  triangle's first corner (all three carry the face's, which is also
  what the raster path assumes when it interpolates them), and for
  every distinct (finish, frame, layer) triple it meets builds one
  variant of the draw's own graph -- `uniformShader` or
  `attributeShader` as before -- with THAT entry's constants: the
  finish from `FinishPalette::Entry` through the same gate the scalars
  pass (`resolveFinish`), laid out in the face's `SurfaceFrame`; the
  image from `TexturePalette`, layer i being entry i - 1 and a layer
  past the palette its last entry, the raster's clamp. The triangle
  gets the variant's slot. A draw with nothing to index (no stream, or
  no palette to read into) is the one-variant case it always was, and
  a face whose index runs past a palette reads what the raster's
  zero-filled uniform array holds there: no finish, no frame.
- **Why this and not attributes.** The alternative -- per-corner
  attributes for the frame vectors and the pitch/depth/angle, and a
  compare-and-mix chain over the patterns present -- would have meant
  re-deriving `applyFinish` with link inputs (four height evaluations
  under a select chain, tripled by the bump's differentials) and could
  not have done the images at all without a chain of image nodes, one
  sample each per shading point. The variants reuse the verified,
  constant-folded graphs unchanged, at the cost of one shader per
  distinct entry combination. The shader cache (`shaders`, keyed on
  the finish's numbers and the frame's) shares variants across draws
  whose faces state the same frames, which instanced and coaxial parts
  do; an assembly of many differently-framed finished parts is many
  shaders, which Cycles compiles in parallel and Blender scenes carry
  by the thousand.
- **The face image** (`FaceImage`, `applyFaceImage`): a picture
  sampled like the base map (decoded when colour managed, alpha
  coverage) and MODULATING the base colour and alpha on top of whatever
  the unit-0 texture did, as `fc_mesh_fs.sh` has it. Its coordinates
  are `fcFrameTexUV` in-graph when the draw states a tile size
  (`Material::facetexscale` millimetres per tile): a planar face in the
  frame's axes; a turned one as (arc, z) with the arc snapped to a
  whole number of tiles round the reference radius; an unframed one
  off its dominant object axis, chosen outright (a coordinate cannot
  be blended the way the finish's height is) with selectors built from
  Math LESS_THAN nodes. Without a tile size the mesh's own UVs serve,
  the texture matrix folded in as for the maps (`ATTR_STD_UV` is now
  added for either need), and a mesh with none reads the corner texel.
- **Bookkeeping.** The mesh key lists every variant (the slots follow
  from them and the stream, which the cache generation names); a
  variant's shader key adds `FaceImage::key` beside the finish's; the
  caps keep the uniform surface with no image. `GraphOps` is the node
  vocabulary `applyFinish` had as lambdas, shared with the image
  builder, and `canonicalFrame` the frame orthonormalization both lay
  their coordinates in.

Verified 2026-08-29 (`palette/probe.py` under xvfb on the debug tree,
`probe.sh <CPU|CUDA> <spp> <tag>`, the finish probe's method: debug
view 2 against the analytic perturbed normal, per REGION of the
picture). A 40 mm box with a 20 mm pocket, top view, its ring face
turned (pitch 2, depth 0.2) and its floor straight-knurled (pitch 3,
depth 0.45) -- two finish entries, two frames, three shaders in the
report: ring corr 0.999 / 0.999, RMS 0.1723 vs 0.1735; floor corr
1.000, RMS 0.2478 vs 0.2488; the ring against the floor's finish
0.005. A cylinder r 10 with the lateral face straight-knurled in its
radial frame and the top face turned in its planar one: front view
corr 0.999 (RMS 0.152 vs 0.1525), top view 0.999 / 0.999. The same box
wearing a red/white checker on the ring and a blue/white one on the
floor at 10 mm per tile (beauty frames): each region's coloured
channel is flat (std 0.02 / 0.03) while the others carry the cells
(0.31 / 0.26), the cells correlate 0.97 with the analytic checker in
the face's frame and 0.99 with the raster frame. CUDA reads the same
numbers as the CPU. One trap paid: a PLANE's frame origin is its
surface's own location with the normal component dropped
(`faceProjectionFrame`), so the pocket floor -- the tool box's face at
(10, 10, 10) -- lays its pattern out from (10, 10), a third of a
period from the object origin the first analytic model assumed;
Cycles was right and the model was not.


### 6.8 MaterialX shader graphs (phase B step 0, built 2026-08-31)

Phase B's first step is the plumbing: the language vendored, its data
library shipped, and a document able to arrive on a shader object and
be told it is wrong. Nothing interprets a document yet -- that is step
1 for Cycles and step 2 for the raster path.

**Vendored** at `src/3rdParty/MaterialX`, upstream
`AcademySoftwareFoundation/MaterialX` pinned at v1.39.5, behind
`BUILD_MATERIALX` (ON, degraded into like `BUILD_BGFX` when the
submodule is absent, rather than asked for like `BUILD_CYCLES`: Core,
Format, GenShader, GenHw and GenGlsl have no external dependency and
build in ninety objects). Everything else is off -- Render and with it
the 134MB `resources/` install, the viewer, the graph editor, the
OSL/MDL/MSL/Slang back-ends, the Python and JavaScript bindings, the
tests. Three things the tree does to its enclosing project have to be
held off, and each would have been found the hard way:

- Its install rules name the EXPORT set and the package config file
  after `CMAKE_PROJECT_NAME`, which under `add_subdirectory` is
  FreeCAD -- it would have written a `FreeCADConfig.cmake` over ours.
  Every one of those rules is guarded by `NOT SKBUILD`, so declaring
  `SKBUILD` around the `add_subdirectory` suppresses the lot; its only
  other effects are on the Python bindings (off) and on RPATH
  (meaningless for a static archive).
- It FORCEs `CMAKE_INSTALL_PREFIX` to `<bindir>/installed` whenever
  the prefix was left at its default. Our presets pass one, so this
  would have gone unnoticed here and moved the whole install of a
  build that did not.
- FreeCAD turns `AUTOMOC` on globally, which otherwise runs moc over
  every MaterialX translation unit.

The `libraries/` tree -- 2.5MB of `.mtlx` node definitions, which both
consumers read at run time to resolve a document's node references --
ships as data at `<resource>/Renderer/materialx/libraries`, staged
into the build tree as well, the same arrangement as the Cycles kernel
source of section 4.1.

**A document reaches the renderer** the way a `.sc` program does. The
chain gained one value at each link, none of them a new mechanism:

- `App::ShaderProgram::Dialect` gains `MATERIALX`, making
  `FragmentProgram` the document XML. Only the `material` stage means
  anything with it.
- `SoShaderObject::SourceType` gains `MATERIALX` in the Coin fork
  (appended, registered by name, advertised as the `shader-materialx`
  feature tag; no layout change, so the fork ABI version stands).
  Coin's own GL pipeline skips it exactly as it skips `BGFX_SC`, and a
  `.mtlx` FILENAME resolves to it.
- `Render::UserShader` gains `dialect` and `sourcePath`, so a back-end
  can tell a document from shader text and knows where the document
  came from. They ride the snapshot at v70, and the shader chunk's own
  revision moves with it (13) -- the bytes moved, so a cached chunk
  from an older build would read the stage string out of the dialect
  byte.

A document may be stated as a PATH instead of inline, and that is not
a convenience: a real material names its images RELATIVE to its own
document (MaterialX's own examples reach `../../../Images/` through an
inherited `fileprefix`), so only the file route gives the consumer
something to resolve them against. `FragmentProgram` therefore reads
as a path when its first non-blank character is not `<` -- a document,
being XML, never looks like a path -- and the view provider sends it
down Coin's FILENAME route, whose `.mtlx` suffix resolves back to
MATERIALX. `loadDocument` then flattens every filename in one pass:
`fileprefix` is inherited, so no single element's value is the answer,
and the search path (the document's own directory, then the data
library) turns what is left into absolute paths. An image the document
names that is not there is a warning, not a refusal -- that map is
missing, the material still renders. This is open decision 2 answered
for now in the file direction; a self-contained inline document would
have to carry its images some other way (FileBlobs).

**Validation at load.** `Render::MaterialX::inspect()`
(`MaterialXSupport.cpp`) parses a document, imports the data library
so its node references resolve, runs MaterialX's own `validate()` and
then asks the one question a syntactically valid document can still
fail: does it describe a surface at all -- a material node, or a bare
surface shader. The view provider calls it wherever a program is
materialized, which for a stored document is document load, and
reports once per distinct text. `MaterialXSupport.h` names no
MaterialX type, so the rest of the tree sees the same header with or
without the library and a build without it says so; the typed API the
consumers will share is `MaterialXSupportP.h`.

Verified on MaterialX's own example materials through the GUI: the
OpenPBR, Standard Surface and glTF PBR samples each load and report
their surface node (`open_pbr_surface`, `standard_surface`,
`gltf_pbr`), and four negative legs each report the right thing --
malformed XML with the character offset, a document with no surface, a
dangling node reference, and a document on the `post` stage.


### 6.9 The MaterialX interpreter (phase B step 1, built 2026-08-31)

Cycles has a shader node vocabulary of its own, so a MaterialX shader graph
is INTERPRETED into it rather than compiled -- the route every engine
with such a vocabulary takes (Unreal's Interchange builds
material-function nodes, three.js' MaterialXLoader builds TSL nodes),
and it needs neither OSL nor any shading-language work.
`CyclesMaterialX.cpp`.

**Where it plugs in.** A `material`-stage user shader whose dialect is
MaterialX replaces the whole surface: `translateDraw` builds
`materialXShader` instead of the uniform or attribute shader, and the
draw's own colour, maps, finish and per-face palettes are not
consulted, because the document IS the material. The section clip
still applies -- that is a property of the scene, not of the material
-- so the closure still goes out through `connectSurface`. Shaders are
keyed on the document's identity (its file, or its text), so a
thousand draws sharing a material share one interpretation.

A document this build cannot interpret does not take the frame with
it: the draw renders its stock material and the reason is reported
once. The raster path stands down the same way for now, by the
opposite route -- `getUserProgram` and `viewerShaderBins` (since
renamed `shipUserShader`) refuse a non-text dialect at the one door
every stage's compile goes through,
so a document is never handed to shaderc, which would report a compile
error per material and draw nothing new. The raster splice is step 2.

**The value model.** Everything becomes a node, constants included:
Cycles folds a constant subexpression at graph build, so stating one
as a `ValueNode` costs nothing at render and saves the interpreter a
second, constant-only evaluation path. A value carries its MaterialX
type's component count, because Cycles has only float and float3
sockets and a `color4`'s fourth component has to ride beside the
triple.

**The node table** covers the stdlib pattern vocabulary: images
(`image`, `tiledimage`), geometry (`texcoord`, `position`, `normal`,
`tangent`, `bitangent`, `geomcolor`), the arithmetic and transcendental
nodes componentwise over scalars and triples, `clamp`/`remap`/
`smoothstep`, `mix` and the `if*` comparisons, `separate*`/`combine*`/
`extract`/`convert`, `normalmap`, the noises and the ramps. It is a
FLOOR, not the vocabulary: a node whose nodedef is implemented as a
MaterialX nodegraph -- which most compound library nodes and every one
of the shading-model translations are -- is interpreted by descending
into that graph with the node bound as its interface. `place2d`,
`hextiledimage` and the glTF image nodes come for free that way, and
the four example materials below needed no table entry beyond it.

**OpenPBR is the canonical surface.** `open_pbr_surface` maps onto
`PrincipledBsdfNode`, whose v2 sockets are OpenPBR parameters; every
other shading model arrives through MaterialX's OWN translation graphs
(`translateShader` on the chosen surface, with the source's unstated
defaults stated and its normal carried across -- MaterialStorage.md
17.19), so there is one shading model to be right
about and the rest is the library's business. Three places in the
mapping are a decision rather than a rename:

- Cycles' Specular IOR Level scales F0 by two (`f0 *= 2.0f *
  specular_ior_level`, `svm/closure.h`), so its neutral value is 0.5
  where OpenPBR's `specular_weight` neutral is 1: the mapping is
  `weight * 0.5`, not the identity a socket-name match suggests.
- `base_weight` scales the diffuse albedo and Cycles has no socket for
  it, so it folds into the base colour, which is what a weight of that
  kind means.
- `transmission_depth` becomes an absorption volume at density
  `1 / depth`, the Beer-Lambert closure the fork's glass materials
  already use (section 6.2). With NO depth, OpenPBR's colour is a tint
  applied once at the surface, which Principled has no socket for: it
  folds into Base Color as `base * mix(1, transmission_color,
  transmission_weight)`, exact for a fully transmissive body (the
  chess set's pawn heads) and tinting the diffuse share too for a
  partial one -- what Blender's own importers accept. The depth is
  read FLAT through the translation graph (`flatConstant`): a
  translated standard_surface carries every input as a connection, so
  the literal read this used to do said 0 for every stated depth.

Two traps the table exists to avoid, both of which yield a
plausible-looking wrong material rather than an error:

- **A missing input is not a zero.** MaterialX states an unconnected
  texture coordinate, normal or position as `defaultgeomprop` on the
  NODEDEF, not as a value. Reading it as the type's zero samples every
  texel at (0, 0) and renders one flat colour -- the same shape as the
  stride trap of section 6.5.
- **An image's colour space is not the document's.** A document's
  working space (`lin_rec709` on the root) is inherited by every
  element, so asking an input for its ACTIVE colour space answers
  "linear" for a normal or roughness map that states nothing. The
  file's own stated space is the answer where there is one, and the
  node's TYPE decides otherwise: a `color3`/`color4` image is colour,
  anything else is data.

**A shader cannot be taken back.** The first version of
`materialXShader` created the `ccl::Shader` and then deleted it when
the document failed to interpret. `Scene::delete_node(Shader *)` does
not do that -- "don't delete unused shaders, not supported", it only
clears the reference count -- so the graph-less shader stayed in
`scene->shaders` and the next device update dereferenced its null
graph (`ShaderManager::device_update_pre` -> `graph->output()`), an
abort inside the render thread. The graph is therefore built first and
the scene node created only once there is something to put in it. A
document that failed is also remembered, so the next restate does not
import the data library again to fail the same way.

**Verification** is DIFFERENTIAL, because a path-traced PBR pixel has
no closed form worth writing down: the same material is stated twice,
once as a document and once through the fork's own appearance
properties -- whose translation into the very same Principled sockets
phases 3 to 6 already verified -- and the two frames are held against
each other. `build/probes/mtlx/probe.sh <CPU|CUDA> <spp> <tag>`, on
the debug tree under xvfb, prints MTLX_RESULT. Thirteen legs, all
passing on CPU and CUDA:

- The base colour matches the control, and WHICH control it matches is
  the colour-space finding: 0.003 against a control stated as the sRGB
  ENCODING of the document's value, 0.053 against one stated raw. A
  MaterialX shader graph states linear colour; the fork's own properties
  state an encoded colour the colour-managed pipeline decodes on the
  way in (section 6.1's trap, from the other side).
- The document overrides the object's own colour (0.153 against the
  green the box is actually painted), so leg 1 was the document's
  doing and not the object's.
- `multiply(red, 0.5)` renders bit-identically to a literal half-red
  (the arithmetic is exact and Cycles is deterministic at a fixed
  sample count), and measurably apart from full red (0.047).
- Metalness changes the surface (0.042); emission adds blue (+0.285).
- A `standard_surface` document renders bit-identically to the OpenPBR
  one -- MaterialX's own translation graph lands exactly on the native
  mapping.
- An image graph paints the checker it names (21% reddish, 79% bluish
  against a blue-ish environment).
- A `disney_principled` document, which has no translation to OpenPBR,
  falls back to the stock material EXACTLY (0.0) rather than aborting
  or rendering black.
- MaterialX's own `open_pbr_default`, `open_pbr_carpaint`,
  `standard_surface_brass_tiled` and `standard_surface_wood_tiled`,
  stated as paths so their images resolve, each build a shader and
  render distinctly (0.12 to 0.17 from the stock material). Between
  them they exercise `tiledimage`, `normalmap`, `place2d` and the
  nodegraph expansion, and none of them reported an uninterpreted
  node.

Not yet covered: `gltf_pbr` and `UsdPreviewSurface` have no
translation TO OpenPBR in the library (only from `standard_surface`
to them), so they report and render stock. The render report's
`images` count reads 0 for a MaterialX image -- it counts the fork's
own texture uploads, and Cycles loads these from file itself.


### 6.10 The raster half: OpenPBR and the generator (phase B step 2, built 2026-08-31)

Cycles reads a MaterialX shader graph by interpreting it (section 6.9).
The rasterizer cannot: it has no node vocabulary, it has a shading
language. So the raster half is two pieces -- a surface model both
engines can describe, and a generator that turns a document into shader
text for it.

**The mesh shader shades an OpenPBR surface.** The PBR branch evaluated
a single metallic/roughness GGX lobe, which has no expression for a
coat or a fuzz and no relation to what the Cycles translator builds
beyond the two sockets they happen to share. It now evaluates OpenPBR
(ASWF v1.1), the model Cycles' Principled BSDF v2 is aligned to, so the
two engines describe ONE surface. `fc_openpbr.sh` carries it: the
diffuse (EON), dielectric specular, metal (F82-tint conductor), coat
and fuzz lobes, the slab weights that layer them, and the environment
terms. OpenPBR-viewer's rasterizer is the reference, with four
departures the file states in full:

- The lobe DIRECTIONAL ALBEDOS -- which the slab layering needs at
  every fragment and the environment terms reuse -- are the analytic
  split-sum fit the IBL path already used, not the reference's
  16-sample Monte Carlo. Sixteen GGX samples per lobe per fragment is
  not a viewport budget.
- No transmission and no subsurface lobe. A rasterizer cannot refract
  through geometry: transmission is the glass pass's business
  (docs/ShaderDesign.md 3.8), and a constant `transmission_weight` of
  one half or more is routed there as a glass body
  (docs/MaterialStorage.md sec 17.21), where the same generated
  material function runs per fragment (sec 17.22); a mapped weight,
  and subsurface, which has no raster route at all, degrade to
  diffuse.
- No anisotropy (no tangent frame on the untextured path, and an
  isotropic environment probe) and no thin film (rasterizable, but ~180
  lines of complex arithmetic on every mesh draw).
- None of the OpenPBR 1.2 extras -- specular haze, retroreflectivity,
  dispersion. These have no socket in Cycles' Principled either, so
  implementing them would move raster AWAY from parity.

The first two are RASTER limits, not Cycles ones: Cycles renders
transmission, subsurface, anisotropy and thin film properly, so a
document using them is a known divergence between the two engines
rather than a parity failure. Only the last group is absent from both.

**The inputs did not change.** A draw still arrives as a base colour, a
metalness and a roughness; the Khronos spec-gloss solve still stands in
where nothing authored a metalness; and the rest of OpenPBR's
parameters keep their spec defaults, which is exactly the stock CAD
surface. One change at a time: the shading model moved, the appearance
data did not, so the tuned presets still read as themselves. Measured
by A/B against the previous shaders on demo-pbr, same tree and frozen
frames: mean radiance ratio 0.9991, with the differences confined to
the spheres and strongest on the smooth non-metal highlight rims --
where the exact dielectric Fresnel replaces Schlick at f0 = 0.04 and
the diffuse gives up the energy the specular layer takes. The 1.2
direct-light gain the branch has carried since PBR arrived is kept for
the same reason, and is now named and explained rather than sitting
bare in the arithmetic.

**The generator** (`MaterialXGen.cpp`) emits a MATERIAL-INPUTS
function, not a program:

    void fcUserMaterialInputs(inout FcOpenPbr m, FcMtlxGeom g)

MaterialX's own hardware generator emits a complete lit shader -- its
lighting, its environment, its uniform blocks -- and none of that is
wanted, because the engine already owns shadows, IBL, the section clip
and the per-face palettes, and a document that replaced them would lose
every one. The document describes the SURFACE; the engine keeps the
lighting.

That shape falls out of one fact about the library: OpenPBR's reference
implementation IS a MaterialX nodegraph, so the stock generator emits a
call to it carrying every OpenPBR parameter and then 2100 lines of
closure. Registering an implementation of our own for that nodegraph --
`GenContext::addNodeImplementation`, which wins over expanding it --
leaves the pattern graph above generating exactly as MaterialX would,
with the whole standard library behind it, and never expands the
closure below. The default OpenPBR material comes out at 79 lines
instead of 2176. A stated constant folds to a literal rather than a
uniform nothing would write (`SHADER_INTERFACE_REDUCED`), so a value
change regenerates -- what a document leaves genuinely open becomes a
`Param_*` in step 3.

Three things the stock pixel stage does had to be redone rather than
inherited, and each was a failure before it was a decision:

- A vertex-data port is named by its SUBSTITUTION TOKEN
  (`$normalWorld`), not by the text the emitted code carries. The
  preamble is therefore driven off MaterialX's own vertex-data block
  and answers each name from the mesh shader's varyings; anything it
  cannot answer is reported and reads as zero.
- The geometry arrives as a struct PARAMETER. Read at file scope the
  varyings compile on GLSL and ESSL and fail on SPIR-V -- the same
  restriction the existing code records for `gl_FragCoord`.
- bgfx defines `M_PI` too, and not with identical text, which the
  preprocessor stops on; and the uv-transform token substitution the
  image nodes include by name is set by the stage this replaces.

**The splice** follows the volume stage's shape (docs/RenderEngine.md
sec 5.3): the stock header prototypes the function under
`FC_USER_MATERIAL`, the assembled variant defines it and appends the
generated source after the include, and without the define the whole
thing compiles to nothing. The variant is the stock TEXTURED mesh
fragment stage -- the one whose vertex stage carries a texture
coordinate -- without `TEXTURE` defined, so no texture environment is
applied over what the document states. It is generated and compiled
once per document identity, not per draw.

A generated material also FORCES the OpenPBR branch. A document is an
OpenPBR surface by construction, so it cannot shade through a matcap or
a Phong evaluation -- neither has anywhere to put what the document
states -- and a frame with PBR mode off carries no environment
intensity either, so a forced draw takes the environment at full
strength rather than rendering unlit.

**What the raster path cannot do it reports**, and the draw keeps its
stock appearance -- the sandboxed-failure rule of section 6.9. One case
as this section was written: a shading model with no translation to
OpenPBR (`UsdPreviewSurface` and the hair models translate to nothing).
An image node was the second, and is one no longer -- see section 6.12.
Cycles renders both properly, loading image files itself.

**Verification** is in three layers, because the chain is long:

- Ten unit tests (`tests/src/Gui/MaterialXGen.cpp`) pin the generator's
  contract. They earned their keep: the image guard first read
  MaterialX's own environment sampler, which is filename-typed too, and
  so refused every document ever written.
- Over MaterialX's own example materials, outside the tree, every
  document that generates was compiled through the in-tree shaderc on
  all three profiles (glsl, spirv, essl): 24 of the 50 examples, the
  other 26 being the reported cases above.
- `build/probes/mtlxraster/probe.sh` runs the whole chain from dialect
  to drawn pixel. Seven legs, all passing: a document overrides the
  object's own colour (0.115 against the green the box is painted);
  `multiply(red, 0.5)` renders BIT-IDENTICALLY to a literal half-red
  and measurably apart from full red; metalness changes the surface
  (0.066); a coat (0.022) and a fuzz (0.031) change it, and nothing but
  an OpenPBR evaluation could be drawing those; and a
  `UsdPreviewSurface` document falls back to the stock appearance
  EXACTLY (0.0).

Two traps that probe records. A user program compiles ASYNCHRONOUSLY
and the stock program stands in until it lands -- which draws exactly
the control frame, so a short settle measures "nothing changed" for
every leg and reads as a dead splice. And the config the probes copy
has MATCAP on, which is how the forced branch above came to be needed:
a document attached to an object rendered as though it were not there.

Not yet: `geometry_opacity` (a material cannot move a draw into the
transparent pass mid-frame). The `Param_*` interface is step 3, and is
section 6.11; images were the other gap here and are section 6.12,
which is also what moved the image case out of the reported list above.


### 6.11 The declared interface (phase B step 3, built 2026-08-31)

A material is not finished when it renders. Someone has to be able to
change it -- and a MaterialX shader graph is XML, so without an interface
the only way to move a number in one is to edit text and recompile a
shader. Step 3 is that interface: the document's own declared inputs
become `Param_*` dynamic properties on the `App::ShaderProgram`, bound
to both consumers.

**What counts as a parameter is what the document DECLARES.** MaterialX
has one construct for this and the documents in its own library all use
it: a node graph's `<input>` elements, which carry the value, the type,
and `uiname`/`uifolder` metadata for presenting it. Those are the
knobs. A value written on the surface shader node is a different thing
-- the document's statement about the surface, no more open to change
than the graph it is wired into -- and it stays the literal section
6.10 folds it into. So `standard_surface_marble_solid.mtlx` publishes
six parameters (Color 1, Color 2, Scale 1, Scale 2, Power, Octaves) and
`open_pbr_carpaint.mtlx`, which declares nothing, publishes none. An
input a graph declares but no node inside it names is left out too: a
knob wired to nothing is worse than no knob.

**The direction is reversed** from everything else in the user-shader
system (docs/RenderDebug.md sec 6.4), where the author declares a
`Param_*` property and writes a shader that reads the matching uniform.
Here the document is the authority: the properties are materialized
from it, one that is no longer declared is withdrawn, and an existing
property keeps its value, so re-reading a document is not a reset. Only
a MATERIALX-dialect program's parameters are managed this way -- a
hand-written `.sc` program's `Param_*` are its author's own and nothing
takes them away. A document that will not parse withdraws nothing
either: a document is edited in place, so it spends time unparsable on
the way from one valid state to the next, and taking the properties
away over that would take the user's values with them.

**The two consumers take a parameter in their own vocabulary**, which
is the same split as the document itself:

- Raster: one `uniform vec4 u_<name>` per parameter, read as its own
  type at the top of the generated function (`u_gain.x`,
  `u_tint.xyz`, `int(u_octaves.x)`). That is the vec4-lane packing
  every other user-shader parameter already travels in, so the value
  reaches the draw through the existing chain --
  property -> `SoShaderParameter` -> captured `UserShader::params` ->
  `pushUserParams` at the consuming submit -- and a `App::ShaderBinding`
  can override it per binding for free. The generated source is cached
  by the DOCUMENT's identity, so a parameter edit compiles nothing: it
  is a uniform write, live.
- Cycles: the value is written into the document before it is
  interpreted, and comes out the other side as a `ValueNode` in the
  shader graph. A path tracer has no uniforms; a parameter is part of
  the shader, so two parameter sets are two shaders and the shader
  cache keys on the values. The document's own identity stays separate
  from them, because a document that will not interpret will not
  interpret at any value.

**Publishing the interface on the raster side took the generator the
other way round.** MaterialX's `SHADER_INTERFACE_REDUCED`, which step 2
used, folds a graph's declared inputs into the code as numbers -- which
is exactly right for a constant and exactly wrong for a knob. The
interface type is now COMPLETE, which publishes every value the graph
did not connect, and the generator emits the declarations itself: a
published value that is one of the document's declared inputs becomes
the uniform, and everything else becomes a file-scope `const` with the
value MaterialX would have folded in. The generated code is therefore
unchanged for a document that declares nothing, which is what the seven
frozen probe legs of section 6.10 re-measure to the digit.

Three things fell out of the library that are worth keeping written
down:

- **A published uniform already names the declared input**, not the
  node input that reads it: MaterialX sets the port's path to the
  interface input when there is one. So the enumeration and the
  generator agree by namepath with no name matching anywhere, and a
  declared input read by a dozen nodes is one parameter reading one
  lane group -- where MaterialX's own viewer, which dedupes the other
  way round, would drive only the first of them.
- **The interface is read from the document as AUTHORED**, before the
  OpenPBR translation. A graph interface survives that translation
  untouched, so one enumeration answers for the property editor, the
  path tracer (which interprets the document as written) and the
  generator (which works on the translation) alike.
- **The surface node's own values are not declared at all.** They are
  published like everything else, but OpenPbrInputs emits them as
  literals and no generated line names them, so declaring them would
  add a few dozen dead globals per material -- in names like
  `base_color` and `specular_color`, at file scope, next to the mesh
  shader's own.
- **A graph input named as a surface input is FUSED with it.**
  MaterialX resolves a graph's interface names in the enclosing graph's
  socket namespace, and the surface's nodedef put every one of its own
  inputs in there first, so a graph declaring `base_color` never gets a
  socket of its own: the reads inside it go through the surface's
  socket. That is the obvious document to write, not an exotic one, and
  with the rule above it generated a name nothing declared. A published
  value is therefore matched to a declared input by namepath first and
  by NAME second -- safe precisely because the fusion is itself by
  name, so after it exactly one socket carries that name and every read
  of the declared input goes through it.

A declared input is named by the document, and its uniform is
`u_<name>` like every other shader parameter -- so a document declaring
an input named after one of the engine's own uniforms (`fcTime`, say)
generates a redeclaration, the compile fails, and the draw keeps its
stock appearance with the compiler's message reported. That is the
sandboxed-failure rule doing its job rather than a name check nobody
could keep current.

**The property carries the type the document states**: float, integer
and boolean; `color3`/`color4` as an `App::PropertyColor`;
`vector2`/`vector3` as an `App::PropertyVector`; `vector4` as a float
list. The document's `uiname`, `uifolder` and `doc` become the
property's tooltip. A colour is carried in the document's own colour
space, which is what the document itself states and what the generated
code (whose colour transforms sit downstream of the uniform) expects --
the property holds exactly the numbers the `.mtlx` text would.

**Verification.** Eighteen unit tests now (`tests/src/Gui/MaterialXGen.cpp`),
eight of them this step's: what the interface is, what it is not, that
one input read twice is one uniform, that it survives the translation,
that each type reads its lane as itself, and that an input named as
a surface input still binds. Over MaterialX's own
example materials, the same 24 of 50 generate as before and all of them
still compile through the in-tree shaderc on glsl, spirv and essl --
`standard_surface_marble_solid` now with six uniforms in it. Both
probes carry the chain end to end: `build/probes/mtlxraster/probe.sh`
grows five legs (the declared interface becomes exactly one property
carrying the document's value; a parameter edit reaches the draw with
no regeneration; a parameter value renders where the same value stated
in a document renders; a withdrawn declaration withdraws its property;
and the effect's demo preview shades with the document rather than with
its own DemoColor), and `build/probes/mtlx/probe.sh` grows a leg
holding the path-traced frames against two frames earlier legs already
rendered from documents stating the same colours: the declared default
lands on leg 1's red and the override on leg 3's literal half-red, both
to a mean absolute difference of 0.0, and the override moves the frame
by 0.0465. Twelve raster legs and sixteen path-traced ones, both PASS;
the seven raster legs that predate this step re-measure to the digit,
and ctest is 454/454.

`scripts/demo-materialx.py` is what this looks like from the outside:
seventeen documents from `scripts/materialx/`, each EMBEDDED in an
`App::ShaderProgram` and hung on its own ball, so the saved `.FCStd`
carries every material in it and opens the same anywhere. Two of the
seventeen declare an interface, and their balls come up with the knobs
in the property editor. Writing that demo is also what found the fused
name above.

Not yet, and the remaining piece of step 3: a material card able to
carry a `.mtlx`. Designed 2026-09-01 and recorded in section 6.13; not
built.


### 6.12 Images in the raster path (phase B, built 2026-09-01)

Twenty-six of MaterialX's fifty example materials name an image file,
and until now every one of them was refused by the raster path and left
to the path tracer. The refusal was honest -- an image node reaches the
generated code as a sampler, and nothing declared or bound one -- but it
is what a real material looks like: a photograph of a surface is the
usual way to state one.

**The join is the file path.** Two sides have to agree, and neither can
see the other. The GENERATOR alone knows the sampler names, because
MaterialX derives them from the node graph; the CAPTURE alone can decode
a file, because the render thread must not open one and a viewer tier
may have no filesystem at all. So the generator reports
`{sampler, unit, path}` per image and the capture reports
`{path, pixels}`, and `BGFXView::pushUserImages` joins the two lists on
the path before the draw. Neither side has to predict the other's
naming, and the path is what both of them already have.

**The pixels travel with the shader.** `UserShader::images` carries
decoded `TextureImage`s, which is the vehicle the whole engine already
uses: content-keyed, blob-stored, deferrable. `RendererBridge` decodes
them through the same `loadParamImage()` cache the environment and the
ground texture use -- so a map edited in another program is picked up,
and the document is only re-inspected when its text or its file changes
(parsing one means loading the standard data library behind it, which is
far too much to do per capture).

**Two things bgfx needed that MaterialX does not write.** Both were
found by compiling every generated example on all three profiles, and
neither shows up on a desktop GL run:

- The SIGNATURE was already right by luck. MaterialX 1.39 writes the
  sampler parameter through a token (`$texSamplerSignature`, default
  `sampler2D tex_sampler`), and bgfx `#define`s `sampler2D` to its
  `BgfxSampler2D` struct pair on the backends that split texture from
  sampler. So the stock token passes one of those by value, unchanged.
- The CALL was not. MaterialX writes the GLSL builtins `texture()` and
  `textureGrad()`; bgfx spells the portable forms `texture2D()` and
  `texture2DGrad()` and only defines them where the builtin is missing.
  The generated preamble therefore shims the other way, under exactly
  the condition `bgfx_shader.sh` switches on -- so a GLSL or ESSL build
  reads its own builtin and only the HLSL-family backends are rewritten.
  Without it the five image materials compiled on glsl and essl and
  failed on spirv alone, which no desktop probe would ever have shown.

**The unit budget is the real limit.** The generated function is spliced
into the stock mesh fragment stage, which declares samplers 0..10, and a
stateful particle emitter binds 11 and 12. That leaves 13, 14 and 15 of
the sixteen bgfx guarantees, so a document may claim three images and
one wanting more is refused whole -- reported, and drawn as its stock
appearance -- rather than drawn with some of its maps reading another
pass's texture. Three is enough for base colour, roughness and metallic,
which is what the library's tiled materials use; the chess set uses
exactly three. **A 2D array over one unit is what lifts this**, the way
the per-face palette (6.7) already holds many images on unit 10, and it
is the obvious next step if the cap starts to bite. An image the
document names but that is not on disk is not a refusal: that sampler is
left unbound and the map draws as the backend default, which the
generator warns about.

> Superseded the same week by **6.14**: the cap did start to bite, the
> images are the layers of one array now, and the two paragraphs above
> describe how it worked for one day. What is still true of them is the
> join (on the path) and how the pixels travel.

**The streaming tier does not get this yet.** `UserShader::images`
carries the pixels in memory, but `SceneDump` does not write them, so a
viewer with no filesystem would load a program whose samplers nothing
binds and draw every map as the backend default. So the server-side
compile (`viewerShaderBins`) declines a document that names images, and
that tier keeps the stock-appearance fallback it had before this
section. Carrying the images through the snapshot is the next step, and
the vehicle is already there: a `TextureImage` is content-keyed and
blob-stored, and the snapshot's texture table already deduplicates and
defers exactly these.

> Built two days later: the images travel with the shader (snapshot
> v74), joined to the program's array layers by the ship hook, and the
> glass splice travels beside the mesh one -- MaterialStorage.md 17.23.

**Verification.** Twenty-one unit tests
(`tests/src/Gui/MaterialXGen.cpp`), four of them new: an image becomes a
declared sampler, a missing file is said rather than refused, two images
never share a unit, and the portable-call shim is present. Over
MaterialX's own examples, outside the tree, generation now succeeds for
**29 of 50 where it was 24**, and all 29 compile through the in-tree
shaderc on glsl, spirv and essl -- 87 compiles, no failures. The
remaining 21 are the models that do not translate to OpenPBR
(`UsdPreviewSurface` and the hair shaders), which is 6.10's case and not
this one. `build/probes/mtlxraster/probe.sh` grows three legs that run
the whole chain to a drawn pixel: a document naming a file changes the
picture at all; swapping that file for one of another colour moves it
again, and the red file's frame is redder than the blue file's while the
blue file's is bluer, which is what proves the sampler reads THAT file
and not merely something; and the same colour stated as a literal draws
the same as stated as a file, to a mean absolute difference of 0.0001.
That last number also says what the colour management does, which is
nothing: the document states no colour space for the file, so its bytes
are read as authored and 230/255 is the 0.902 a literal would have been.

Writing those legs re-taught a trap this file already records. The first
cut asserted that a red image "draws reddest" -- a channel ORDERING over
the body mask -- and it failed. It was right to: the studio environment
is blue-ish, and at this roughness a LITERAL red draws with more blue
than red in the mean too, which is why the literal and the image agreed
to 0.0001 while both "failed". An absolute colour assertion over that
mask reads the environment. Assert one frame against another, where the
environment cancels.



### 6.13 A material card that carries a document (designed 2026-09-01, NOT built)

The last piece of phase B step 3, and the first one whose shape was
decided in discussion rather than found in the code. **Everything below
is a design record: none of it is built.**

**What it is for.** A MaterialX shader graph renders today only if someone
builds an `App::ShaderProgram` for it by hand and binds it through an
`App::ShaderBinding` -- which is what `scripts/demo-materialx.py` does
seventeen times. That is the author's route, not the user's. The user's
route is the material library: pick "Brushed Aluminium" out of a list
and have the object look like it. A card that carries a `.mtlx` is what
joins the two.

**Prior art.** Every system with a node-graph material separates the
GRAPH from the per-use values. Unreal states it most plainly: a Material
is the graph, compiled once, and a Material Instance is a thin set of
parameter overrides on top of it. Blender shares one node datablock
between all users and a per-object difference means a copy. USD binds a
material prim and overrides by referencing it and restating inputs. The
fork already has this split and did not have to invent it:
`ViewProviderShaderProgram` owns the library node built from the
program, and `ViewProviderAppearance::ownProgramNode()` builds a
per-binding CLONE with that binding's parameter overrides baked in. A
card-carried document slots into that; it does not get a scheme of its
own.

**Decision 1 (the user's, 2026-09-01): assignment creates nothing, and
an explicit command materializes.** Assigning the card stays what
assigning a card has always been -- a write to `ShapeMaterial` -- and
the view provider renders it, exactly as a glass card already grows
`Render_Glass*` dynamic properties without adding an object to the tree.
A separate command turns that into real `ShaderProgram` / `Shader` /
`Appearance` objects when someone wants to edit the graph or its knobs.
The common case leaves the tree clean; the power case stays open.

**Decision 1a (the user's): the node construction lives in
`ViewProviderAppearance`, for dedup, and the node struct is SHARED with
copy-on-write.** This is the part that keeps the two routes from
becoming two implementations:

- One Coin node structure per distinct card, not per object. Fifty
  objects carrying "Brushed Aluminium" share one `SoShaderProgram`
  triple, so there is one document parse, one generation and one
  compile -- and `materialXVariants` already keys its generation on
  document identity, so it agrees with this for free.
- It is built where the binding machinery already is.
  `ViewProviderAppearance` owns `applyDirectBindings()`, which inserts a
  program node at each target view provider's root, and `rebuildAllBindings()`,
  a per-document static coordinator. A registry of card-built nodes is a
  sibling of those, and `syncShaderNodes()` -- already shared between
  the program view provider and the per-binding clone -- is the one
  construction function all three routes call.
- **COW on edit.** The shared struct is immutable while it is shared. The
  materialize command copies it into the real document objects, seeded
  with what the card stated, and that object's binding switches from the
  shared node to its own. An edit therefore never reaches the other
  forty-nine objects, and nothing has to be copied until an edit happens.

**Decision 2 (the user's): the card carries the document EMBEDDED.**
Open decision 2 was answered in the file direction for the
`ShaderProgram` (6.8); for the CARD it is answered the other way. A
card is a library asset that gets copied into documents and passed
between machines, and a card that names a path is a card that breaks
when it travels.

Two things follow, and the first is cheaper than it first looked:

- **The card's own path is the resolution anchor.** A relative image
  name resolves against `dirname(the document's source URI)`, then the
  data library (`searchPath()`), and MaterialX's own examples are
  written that way -- `standard_surface_brass_tiled.mtlx` reaches its
  maps through `../../../Images/`. Inline text has no source URI, so
  that entry would vanish and every relative map would resolve to
  nothing. But a card IS a file: the `.FCMat` it was read from stands in
  as the source URI, and `loadDocument` already flattens every relative
  filename to an absolute path in one pass at load. Embedding does not
  lose the anchor, it moves it.
- **Travel needs the image bytes, not just resolvable paths.** An
  absolute path that resolves here points at nothing on another machine,
  so an embedded document is a self-contained material DESCRIPTION and
  not a self-contained material. The images have to ride along, and the
  machinery for that exists: `App::FileBlobManager` and
  `Document::collectFileBlobs()`. This is wiring, not new work -- and
  it is only needed for image-carrying cards. The seventeen documents
  the demo already embeds are imageless, which is why that reopen test
  passed without any of this.

**The card side.** A new appearance model beside `GlassRendering.yml`,
which is the precedent for a fork-authored model whose fields drive
rendering rather than colour. One field holding the document text; the
card format already carries `File` and `Image` typed fields
(`TextureRendering.yml` uses both), so a text field is not a new kind of
thing. A card carrying it must also reach the Appearance panel's look
list, which means one more filter beside "Basic appearance" and
"Texture appearance" in `DlgDisplayPropertiesImp::setupFilters()`.

**Where the parameters live** is the one question that needs no new
answer. The document's declared inputs become `Param_*` properties
(6.11); the card's copy states the defaults, and a per-object difference
is a per-binding override -- the layer `ownProgramNode()` already bakes.
Under decision 1 an unmaterialized object has no binding object to hang
an override on, so until it is materialized it wears the card's values;
wanting to change one is exactly the moment the materialize command is
for.

**What to settle before building.** How the follow-the-card rule of
`MaterialStorage.md` sec 15 extends to this -- a document is not an
`App::Material`, so `FollowMaterial` as written does not decide it. What
the materialize command is called and where it appears (the sync
commands' shown-only-while-it-applies rule, 13.5, is the precedent).
Whether materializing is reversible. And what the three-image cap of
6.12 means for a card: a library card wanting five maps is refused by
the raster path but rendered by Cycles, which is a confusing thing for a
LIBRARY to do and is the strongest argument yet for the 2D-array
generalisation.

> That last one is **answered**: 6.14 built the array, and a card may
> name sixteen images before the question arises again.

> **Storage settled 2026-09-02, in `MaterialStorage.md` sec 17.** The
> payload is one `App::FileSet` holding the document and its maps as
> content-addressed blobs, carried on the appearance value; a library
> keeps its files under descriptive names in a `materialx/`
> sub-directory and identity is computed over the hashes. That
> supersedes the "embedded" reading of decision 2 here -- the content
> still travels with the document, as blobs rather than as text inside
> the card -- and the flattened base64 form remains the clipboard and
> export spelling. Sec 17 also carries the rename set and the build
> order this lands in.

### 6.14 Many images on one unit (phase B, built 2026-09-01)

6.12 gave a document its images and, in the same breath, a cap of three
of them -- the mesh fragment stage this material function is spliced
into declares samplers 0..10 and a stateful particle emitter binds 11
and 12, so a sampler per image left exactly 13, 14 and 15. Three is a
base colour, a roughness and a metallic. It is not a base colour, a
roughness, a metallic, a normal and an occlusion, which is the ordinary
set; MaterialX's own `standard_surface_brick_procedural` names five and
was refused whole. A card carrying such a document (6.13) would have
been refused by the rasterizer and rendered by the path tracer, and a
library that behaves differently in the two engines is a library nobody
can trust.

**The images are the LAYERS of one array texture.** The mechanism is not
new: the per-face palette (6.7) already puts many images on unit 10 and
picks between them per triangle, and `GpuTextureArray` -- content-keyed,
resampled to the largest layer, mipped on the CPU -- is the same class
here with two arguments added (how many layers, and how large). The unit
count stops being what bounds a material; what bounds it now is the
array itself, at sixteen layers, and a document past that is still
refused whole rather than drawn with maps missing.

**A layer is per FILE, not per node.** A material reading one map as its
base colour and again as its coat colour costs one layer. That is worth
saying because it is what makes sixteen generous: the deduplication is
on the resolved absolute path, which is the same key the join already
used.

**Both spellings of a sampler had to move, and only one was obvious.**
MaterialX passes an image into its library functions as a `sampler2D`,
and it writes that parameter in two different places:

- the hand-written library functions (`mx_image_color3`,
  `mx_hextiledimage`, ...) take it through a token,
  `$texSamplerSignature`, which one substitution turns into
  `int tex_sampler`;
- the GENERATED nodegraph implementations (`NG_tiledimage_color3` and
  its kin) declare it through the TYPE SYNTAX for `filename` instead,
  which is still `sampler2D`.

Change one and not the other and the generated call has no matching
overload -- `mx_image_float(sampler2D, ...)` against a definition taking
`int`. Both are answered: the token substitution in `emitImageAccess`,
and a `ScalarTypeSyntax` for `Type::FILENAME` registered in the
generator's constructor. **The harness caught this immediately and a
desktop run never would have**, because it is a compile error in three
of the fifty examples and those three are exactly the tiled ones.

**The three builtins are answered as functions, not as macros.**
`texture()`, `textureLod()` and `textureGrad()` become `fcMtlxImage`,
`fcMtlxImageLod` and `fcMtlxImageGrad`, emitted just above the macros
that redirect the names -- so the bgfx spellings inside them expand
before the redirection reaches them. `textureGrad` is the one bgfx has
no array form of, so both vocabularies are written out under the same
condition `bgfx_shader.sh` switches on: the GLSL builtin takes an array
sampler directly, and the split-sampler backends reach the pair the way
bgfx's own wrappers do. A document naming NO image keeps the plain
`texture2D` shim of 6.12 and declares no sampler at all, so it claims no
unit and nothing about it changed.

**A missing map is now defined rather than incidental.** A file the
document names that is not on disk gets no layer; the generated code
carries -1 for it and the fetch answers black. Before, that sampler was
simply left unbound and read whatever the backend defaulted to -- or,
on a unit another draw had touched, that draw's texture.

**Two costs, both deliberate.** The layers of an array are all one size,
so every map is resampled onto the largest of them: a 2k albedo beside a
512 roughness makes the roughness a 2k layer. The ceiling is 2048 for a
material (the per-face palette keeps its 1024 -- a marking on a face is
not a surface), and a total byte budget halves the layers when a
document would otherwise ask for a quarter of a gigabyte. And an
incomplete array is rebuilt while it waits for its pixels, which is
right for a decode in flight and wrong forever for a file that will
never decode, so the rebuild is bounded at 120 tries.

**Verification.** Twenty-four unit tests
(`tests/src/Gui/MaterialXGen.cpp`), three new and four rewritten: an
image becomes a layer of one array, four images are four layers on one
unit, two nodes naming one file share a layer, more images than the
array holds is refused whole, and an imageless document claims no unit
at all. `ctest` 455/455. The standalone generator harness of 6.12 is rebuilt as
`build/probes/mtlxgen/harness.cpp` -- it generates every example in
MaterialX's own corpus, splices it exactly as `materialXVariant()` does
and runs the in-tree `shaderc` on glsl, spirv and essl. Generation now
succeeds for **30 of 50 where it was 29** (the new one is the
five-image brick), and **all 90 compiles pass**; the twenty refusals are
the models with no translation to OpenPBR, which is 6.10's case.
`build/probes/mtlxraster/probe.sh` grows three legs (18 now) to a drawn
pixel: a four-image document draws at all (0.046 against the literal
before it), swapping one of its four files moves the frame in that
file's direction (0.038, and the red frame IS redder while the blue one
is bluer, so the layers are not crossed), and one file read by two
nodes draws as that file -- to 0.0000.

Leg 15 was written twice, and the first cut is worth recording: it held
the four-image document against a four-image document differing only in
its COAT map and measured 0.0159, under the threshold. Nothing was
wrong; a coat at half weight is simply a small thing next to a base
colour. **A control frame has to differ in the thing being measured**,
which here is whether the document draws at all -- so the second cut
holds it against the red literal of leg 14 with the blue file as its
base.

## 7. Preparing for out of process

Cycles is a better candidate for process isolation than OCCT: it is
heavy, long running, crash-prone on GPU drivers, and it has no WASM
path, so browser clients can never run it locally and must be served
frames regardless. `docs/ComputeBoundaries.md` already sets out the
discipline.

In process now, but with one hard rule that costs nothing today and
saves the rewrite later:

**The scene translation layer takes no Gui types.** It reads the render
cache and produces a Cycles scene; it does not touch `View3DInventor`,
`ViewProvider`, Qt, or the bgfx backend. The Gui side owns only the
camera feed, the buffer handoff and the blit. When Cycles moves out of
process, the translation layer moves with it unchanged and the
transport slots in underneath.

The frame transport is the second half, and the streaming tier already
exists: `SceneServer` carries scene deltas to browser clients today,
and a rendered-frame stream is the same shape.


### 7.1 The served viewport (phase 5, built 2026-08-28)

A browser viewer asks the serving process to path trace what it is
looking at. The process runs a Cycles session for that connection, fed
with the served document's scene and THAT viewer's camera, and pushes
the refining frame down the same WebSocket as encoded images. The
viewer blits each frame under its own lines and highlight through the
very consumer route the desktop uses (sections 5.2, 5.3 and 5.8), so
the browser draws exactly what a desktop Cycles view draws: the
path-traced image as the base layer, the raster depth-only prepass
over it, feature lines and selection on top. Nothing about the
document, the pick or the property channel changes -- the frame is one
more thing the connection receives.

Rulings:

- **One session per connection, not per document.** The camera is the
  viewer's, and two viewers looking from two places are two renders.
  The cost is a device context per viewer (a CUDA context each), which
  is what the cap below counts. Who is admitted at all is still the
  grant list's answer, and a stream dies with its connection.
- **The frame rides the existing socket** as a new binary kind, not a
  new channel or an HTTP route: same door, same lifetime, in order
  with the scene deltas. A scene payload starts with a 64-bit version
  counter; a frame starts with the four bytes `FCCY`, which no version
  reaches.
- **Encoded as JPEG, composited on the server.** The film goes
  transparent where the environment is not seen (section 6.2), and
  JPEG carries no alpha, so the stream composites the frame over the
  document's background -- the same flat colour or vertical ramp the
  offline `renderScene` writes (section 6.1's `writePng`), the same
  code. The 8-bit frame is sRGB-encoded exactly when the scene is
  colour managed, and the viewer's blit decodes it back to linear
  when its host target is linear: the shader's second gate, the mirror
  of section 5.7's encode gate.
- **Camera from the viewer, scene from the publish path.** The viewer
  sends its camera as a control op whenever it moves (at most once per
  frame it draws); the source applies it on the connection thread --
  no document is touched, and the `Viewport` already holds a move
  until a frame has gone out (section 5.7's throttle). The scene comes
  from `SceneServeSource::publishNow()`, right after the traversal
  that builds the caches, through the same translate and the same
  generation-counter gate as the desktop feed. The two meet under the
  stream's own mutex.
- **Paced by an encoder thread per stream.** The driver's update wakes
  it, it takes the newest staged frame (which counts as a draw for
  the session's reset throttle), composites, encodes, sends. No more
  often than `minIntervalMs` (100 by default), except that the last
  frame of a render always goes; frames that arrive while one is being
  encoded coalesce -- the newest wins, nothing queues.
- **The blit is the same FrameConsumer, on both tiers.** The blit half
  of `CyclesViewport.cpp` moved into `FrameImageConsumer` (Renderer
  lib, engine-free), which the desktop viewport now uses for its half4
  frame and the browser for its decoded RGBA8. That needed the draw
  facade in the browser tier: `DrawDevice.cpp` and `BGFXDrawDevice.cpp`
  join the wasm build, and the `FC_RENDERER_STANDALONE` stub in
  `setFrameConsumer` is gone.
- **Size is the viewer's canvas, capped.** The op carries the canvas
  size in device pixels; the source scales it down to `maxPixels`
  (aspect kept) and renders there, the blit stretches. `pixelSize`
  (section 5.9) applies on top, as it does on the desktop.

The wire (`FrameStreamWire.h` is the one place the layout is spelled):

- viewer -> server, text: `{"op":"cycles","id":N,"action":"start",
  "device":"CPU","samples":256,"timeLimit":0,"denoise":true,
  "pixelSize":1,"quality":85,"maxPixels":2073600,"width":W,
  "height":H,"view":[16 floats],"proj":[16 floats]}` -> `{"id":N,
  "ok":true,"device":"CPU"}`; `"action":"stop"`; `"action":"devices"`
  -> `{"id":N,"ok":true,"devices":[{"type":..,"description":..}],
  "streams":N,"maxStreams":N}`;
  `"action":"status"` -> the `ViewportStatus` fields. Every one takes
  `"cell"` (default 0). Not refused for a view-only connection: a
  path-traced view mutates nothing.
  `{"op":"cycles.camera","view":[16],"proj":[16],"width":W,
  "height":H}` -- no id, no reply.
- server -> viewer, binary: `FCCY`, u8 version (1), u8 format (1 =
  JPEG), u8 flags (bit 0 = the render's last frame, bit 1 = sRGB
  encoded), u8 cell, u32 width, u32 height, u32 sequence, f32
  progress, u16 status length, the status text, then the image.
- server -> viewer, text, unsolicited: `{"op":"cycles","event":
  "error"|"stopped","message":...}` when the session failed or the
  source went away under it.

Files: `src/Gui/Renderer/CyclesStream.cpp` (`Render::Cycles::
FrameStream`, declared in `CyclesRenderer.h`, no-engine stub beside
`Viewport::create`'s); `FrameImageConsumer.{h,cpp}` and
`FrameStreamWire.h` (Renderer lib); `SceneServer` grew
`sendBinary`/`sendControl` to one connection, the connection id on a
`SceneControlRequest`, and a per-group client-closed handler;
`SceneServeSource` owns the streams and answers the ops; the viewer
side is in `wasm/main.cpp` (decode with bimg's stb_image, the
consumer, the camera op, `?cycles=<device>` to start one from the URL)
and a "Path trace" section in the web menu.

Verified 2026-08-28 (scratchpad `probe.sh`: one FreeCAD under xvfb
building a five-body scene and serving it with `Gui.serveDocument`, a
raw-WebSocket Python client, and headless Chrome 151 driven over CDP):

- Browser-free client, CPU 16 spp at 480x320: `devices` and `start`
  answered, 7 frames from 0.0 s to the Final flag at 1.8 s, sequence
  monotonic, the frame sRGB-flagged and at the asked size; the final
  frame agrees with `cyclesRender` of the same camera to a mean of
  5.3/255 over the middle (JPEG q85 of a 16 spp noise field -- the
  first frame arrives at the coarse divider, 160x107, and the blit
  stretches it); a camera nudge restarts at progress 0.19 and settles
  again (7 frames); `status` reports the session; after `stop` at most
  one frame that was already queued arrives. CUDA 32 spp: 2 frames in
  0.35 s.
- Browser, CUDA 256 spp at the canvas' 1100x757: `?cycles=CUDA` starts
  the stream once the scene is in, 27 frames, "Rendering Done, Sample
  256/256" 29 s after page load (session start included), the web
  menu showing "CUDA (100%)"; the screenshot is the path-traced image
  under the raster edge lines and the NaviCube, the raster view again
  after "Off".
- The desktop viewport through the moved blit: settles at 16 spp in
  6.4 s, live frame vs offline 5.7/255 with denoise off (the lines and
  the NaviCube are in the live frame), the raster frame 34/255 from
  the same reference.

Traps this cost: the wasm viewer had not LINKED since `885653e2c3`
(`Environment.cpp` was never added to its source list -- fixed here);
`CUDA_BIN_PATH` must reach the serving process or the device list is
CPU only (section 4.1); and two harness traps -- Chrome's
`PUT /json/new?<url>` takes its argument up to the first `&`, so the
page URL must be percent-encoded whole, and the cached Chrome wants
`LD_LIBRARY_PATH=.conda/freecad/lib` for `libasound`.

**A cell of a split layout** (built 2026-08-28, same day): the stream
is keyed by connection AND sub-view. Every `cycles` and `cycles.camera`
op carries `"cell"` (0 = the full canvas, else the chrome's cell id,
`SubViewFrame::id`), the frame header's byte 7 carries it back, the
source keeps one stream per (connection, cell) and drops them all when
the connection goes, and the viewer keeps one `CyclesCell` per traced
sub-view: its consumer is registered under that sub-view id, so the
blit lands in that cell's bank alone (section 5.11's slot), and its
camera goes out from the layout frame that draws the cell. A layout
push stops what it does not carry -- the full canvas when cells
appear, a vanished cell, every cell when the layout clears -- before
the banks go. The split chrome puts a `PT` select (off / the devices)
on each 3D cell's chip; the viewer menu's section stays the single
view's (the active cell follows the cursor and is never pushed to the
chrome, so a menu could not know which cell it meant). Verified with
the same rig: a two-cell layout pushed, cell 2 started on CUDA at its
550x757, 19 frames to "Rendering Done" in 14 s, cell 1 never touched,
the two halves 51/255 apart (raster left, traced right, each under
its own lines), the cell's stream stopped by clearing the layout.

**A cap on served sessions** (built 2026-09-04). A session is the most
expensive thing a viewer can ask of this process -- a device context,
the scene resident on that device, an encoder thread -- and anyone
admitted could ask for one per traced cell, as often as they liked, on
a box that may be hosting a live rig. `RenderParams::CyclesMaxStreams`
(4 by default; 0 or less means no cap) is how many may live at once,
across every served document and every connection. A start made when
the cap is reached is answered `{"ok":false,"code":"TooManyStreams"}`
with the count in its message, which the viewer already puts on its
status line and into the `fc:cycles` event, and the server logs a line
naming the connection and the cell. Nothing that is not a served
stream is counted or capped: the desktop views, the shader graph
editor's preview and the offline `cyclesRender` are not streams.

- **It counts devices, not objects.** `FrameStream` makes one slot in
  its base constructor -- so no implementation can forget to count,
  and a build without the engine has the same counter -- and shares
  it with its viewport, which copies it into every session it hands
  to the reaper (sec 5.12). The reaper drops it after the session is
  destroyed and never before. A stream that has stopped therefore
  keeps its place for as long as its device is still being torn down,
  which is the number that matters: the point of a cap on a machine
  is what the machine is carrying, not how many objects are alive.
  `FrameStream::liveCount()` reads it; `slotHandle()` is a handle on
  one slot that outlives its stream.
- **A restart frees its own slot first, and is forgiven it.** A
  `start` for a cell that is already tracing releases that cell's
  stream before it makes the replacement, where the two used to
  overlap -- and, since that slot is now held until the teardown
  finishes, hands the replacement a `StreamOptions::replacing` handle
  on it, which is forgiven once against the cap. Without the release
  a restart would double the cell's devices for no reason; without
  the forgiveness a viewer at the cap could not restart its own
  render at all -- the wait would be the whole teardown, minutes of
  it on the cold-kernel-compile case sec 5.12 is about. Exactly one
  slot is ever forgiven, and only while it is still held, so a viewer
  that restarts in a loop still leaves every earlier session counted
  and is refused at the cap. What it costs: a restart the engine then
  refuses (no such device) leaves the cell with no stream instead of
  the one it had, which is what the viewer is told.
- **Checked twice, deliberately.** The serve source checks before it
  creates, which is what produces the named code and the log line; the
  engine checks again inside `FrameStream::create`, under the same
  lock that constructs, which is the check two connections cannot race
  through for the same last slot. Serve ops run on the GUI thread, so
  today the second is a backstop rather than a live race.
- The `devices` action's reply carries `"streams"` and `"maxStreams"`
  as well, so a viewer or a probe can read the policy without
  provoking a refusal.

Verified 2026-09-04 under Xvfb, with `cycles_cap.sh` and its two arms
in `~/works/sw/fcad-probes` (a serving FreeCAD on a port of its own,
so a live serve is not touched, and a raw-WebSocket client asking for
one more stream than the cap allows -- each start a CPU session of one sample
at 128x96, so what is measured is the cap and not the machine). At a
cap of 2: the first two connections were taken and the third refused
with `TooManyStreams` and the message naming the limit, the count
still 2 after the refusal, the server logging the connection and the
cell; the `devices` reply reported `streams` 0 then 2; a restart of a
cell already tracing was taken at the cap and left the count at 2,
while a second cell of that same connection was refused; five
restarts in a row were all taken and left the count where it started,
which is the check that matters for the forgiveness -- a slot leaked
by it would shrink the server's capacity for good; and a `stop` and,
separately, a connection simply dropped each returned the slot and
let the refused viewer in, the count reading its new value 0.25 s and
0.51 s later, through the reaper rather than at the stop. At a cap of
0 all three were admitted. ctest 473/473 after the change.

What the probe cannot show is the hold itself: a CPU session tears
down in milliseconds, so the slot is back before the next message is
answered. The hold is for the case sec 5.12 is about -- a device
whose teardown takes minutes -- where the old count would have handed
the freed slot to another viewer while the first device was still
resident.

What this does NOT do yet: the interop
path (the GPU frame still crosses the CPU twice,
once into the staging buffer and once into the encoder); the
frame-push latency of the connection loop's 200 ms poll (a queued
frame waits for the loop's next tick); the offline `cyclesRender` for
a served document with no view (the stream is the only path-traced
output a hidden document has).


## 8. Build order

Phase 0 -- prove it builds and renders, standalone, outside the tree.

1. Create `LinkVibe` on `realthunder/cycles` from `main`. Leave
   `demo-tile` alone.
2. Solve `openimageio`, `embree`, `openimagedenoise` in a **scratch
   conda env** (section 3.1) and confirm nothing moves qt6/pyside6/
   boost/tbb/openexr. Only then touch `.conda/freecad`.
3. Build Cycles standalone out of tree with `WITH_LIBS_PRECOMPILED=OFF`
   and the option set from section 3. CPU only.
4. Render one of the bundled `examples/` scenes to a file. Phase 0 ends
   when an image comes out.

Phase 1 -- GPU devices, still standalone.

5. Add `cuda-nvcc` (pinned) and the OptiX headers; enable
   `WITH_CYCLES_DEVICE_CUDA` and `WITH_CYCLES_DEVICE_OPTIX`; render the
   same scene on CUDA and compare against the CPU image and timing.
   OptiX builds but cannot run here (section 4.1).
6. Enable `WITH_CYCLES_DEVICE_HIP` and confirm it BUILDS. Do not expect
   it to run here (section 4.2); record that it is unexercised.

Phase 2 -- into the tree, no FreeCAD scene yet. **Done 2026-08-28.**

7. Submodule at `src/3rdParty/cycles`; a `BUILD_CYCLES` option
   defaulting **OFF** (bgfx and the CAM simulator default ON now, but
   Cycles carries a heavy build and has to earn that). The tree comes
   in through the fork's `CYCLES_EMBEDDED` mode, which skips its
   executable, install and CTest and exports one `cycles_embed`
   INTERFACE target carrying the include directories, the
   `CCL_NAMESPACE_BEGIN` and `WITH_*` definitions the headers branch
   on, the SSE4.2 flags, and the library list. On the FreeCAD side the
   Cycles-facing code is its own object library
   (`FreeCADRendererCycles`), so nothing else in the renderer compiles
   under those flags.
8. A minimal renderer unit that renders a hard-coded scene to a buffer,
   driven from Python and saved to a file. This proves threading,
   lifetime and teardown with no translation layer in the way.
   `Gui.cyclesDevices()` and `Gui.cyclesRenderTest(path, width,
   height, samples, device)` in `src/Gui/Renderer/CyclesRenderer.cpp`:
   a cube on a floor under a uniform sky, CPU and CUDA both, clean
   exit. Two things the standalone could not show: a **debug build
   asserts when `SessionParams::denoise_device` is left unset** (a
   default `DeviceInfo` carries the CPU id with no description, and
   `session.cpp` compares it with the render device), and
   `DEVICE_MASK()` spells its cast unqualified, for use inside `ccl`
   only.

Phase 3 -- scene translation (section 6). **Steps 9 and 10 done
2026-08-28** (section 6.2); the texture, finish, clipping and user
shader translations remain.

9. Render cache -> Mesh/Object/Camera/Background, geometry first.
10. Materials, per-face slots, environment.

Phase 4 -- the viewport. **Done 2026-08-28**: steps 11, 12 and 13
(sections 5.7, 5.8 and 5.9), then the incremental scene update of
section 5.10 -- a restate under a running session edits its scene in
place instead of starting another -- and the Cycles cell of a split
view (section 5.11), which needed the frame consumer scoped per
sub-view.

11. `DisplayDriver` (section 5.2) -> staging buffer -> texture ->
    `FrameConsumer` blit, on-top highlight route (section 5.3).
12. Host depth prepass under the blit, and the dimmed-when-occluded
    highlight (section 5.3).
13. Cancel-on-camera-move, sample and time budget, denoise on. Follow
    Blender's navigation behaviour: restart at a coarse pixel size and
    refine, the way its `preview_pixel_size` does -- the crisp
    rasterized highlight over a blocky refining image is what makes
    that acceptable.

Phase 5 -- beyond the desktop: headless render served to the browser
tier over the existing stream. **Built 2026-08-28**, section 7.1.


Phase 6 -- queued, not started. Two items, in this order.

14. **Glass materials.** Glass exists as an effect but not as
    something a user can pick: `Render_Glass` and its IOR, density
    and roughness are per-object view properties the Render Settings
    panel adds on demand (`TaskRenderSettings.cpp`), and the
    Appearance library at
    `src/Mod/Material/Resources/Materials/Appearance/` has 23 presets
    and no glass among them -- every one of them states only
    `BasicRendering`, which has no glass field to state. So the step
    is two halves. First, appearance presets that carry glass (clear,
    frosted, tinted at least), which needs an appearance model
    holding the fork's glass fields -- upstream's Render Workbench
    model `Render Glass` (`Render.Glass.IOR`, `.Color`, `.Bump`, ...)
    is the obvious shape to follow rather than invent, though it has
    no density or roughness. Second, applying such a material has to
    reach the view: today nothing binds a material's fields to the
    `Render_*` properties, so a glass preset would arrive inert.
    Both backends read the result -- bgfx through the glass pass
    (`docs/ShaderDesign.md` 3.8), Cycles through the transmission it
    already builds in section 6.2 -- so nothing new is needed on the
    Cycles side beyond what a richer glass material states.

    DONE 2026-08-29. The model is the fork's own
    `Models/Rendering/GlassRendering.yml` (inheriting
    `BasicRendering`, because the absorption is tinted by the diffuse
    colour) rather than upstream's density-less `Render Glass`; a
    card states `Render_*` view properties through
    `Materials::Material::getRenderProperties()` and
    `Gui::applyMaterialRenderProperties()`; four presets (Clear,
    Frosted, Tinted, Acrylic) with icons rendered as glass. The one
    piece Cycles lacked -- it had dropped `Render_GlassDensity`, so
    Tinted Glass path traced untinted -- is the absorption volume of
    section 6.2.

15. **Bridge the shader system to Cycles through the node API.** The
    translator emits one fixed graph per material: a Principled BSDF
    with the arithmetic of section 6.2 baked into its sockets. That
    is why user shaders are still on the not-translated list -- there
    is no path from a shader the user authored to anything Cycles
    renders. Cycles' answer is its shader node graph
    (`ccl::ShaderGraph`, `ccl::ShaderNode`, connected by socket
    name), which is the same shape our own effects already have. The
    step is to translate our shader/material description into that
    graph instead of only into BSDF socket values, so an authored
    surface path-traces as what it is. Scope to settle when it
    starts: which of our shader system this covers -- the surface
    finishes and texture maps of section 6.2's not-translated list
    are node-graph work of exactly this kind, whereas a screen-space
    effect (SSAO, outlines) has no node-graph meaning and stays the
    raster path's. Trap already on record: **ccl connects sockets by
    NAME**, so a socket renamed between Cycles versions fails at
    graph build, not at compile.

    RULED 2026-08-29, in two phases. **Phase A**, the declarative
    facts a `Render::Material` carries -- texture maps, surface
    finishes, the per-face palettes -- become node graphs: the maps
    are section 6.5, the finishes section 6.6, the per-face palettes
    section 6.7 (all built 2026-08-29). **Phase B**, user shaders:
    freeform bgfx `.sc` text has no node-graph meaning, so the bridge
    is a THIRD, node-based authoring dialect -- MaterialX. The
    `post` stage needs no bridge: it already runs over the traced
    frame.

    **Phase B DESIGNED 2026-08-29** (discussion recorded here; work
    starts in a later session). The findings that shaped it:

    - A premise correction. An earlier draft of this item said
      Cycles' Hydra delegate "already maps MaterialX networks to
      ccl nodes". It does not: `src/hydra/material.cpp` maps
      `UsdPreviewSurface` (a five-entry parameter table) and USD
      nodes whose id starts with `cycles_`/`cycles:`, nothing else.
      Blender's route (the 2026 GSoC project) is MaterialX ShaderGen
      -> OSL -> `ccl::OSLNode`, which needs `WITH_OSL` (off in our
      build, a heavy extra dependency) and runs on CPU and OptiX
      only -- OptiX is a decoy on this box (section 4.1). Not our
      route.
    - Engines with a node vocabulary of their own INTERPRET a
      MaterialX graph into it rather than compiling it: Unreal's
      Interchange builds material-function nodes (Standard Surface,
      OpenPBR, UsdPreviewSurface), three.js' `MaterialXLoader`
      builds TSL nodes. Our translator already has that vocabulary
      (`GraphOps` in `CyclesScene.cpp`; `applyMaps`/`applyFinish`
      are hand-built graphs of the same kind).
    - ShaderGen is the code-generation route for raster. MaterialX
      1.39.5 (2026-05-22) emits GLSL, ESSL, Vulkan GLSL, MSL, WGSL,
      OSL, MDL; hardware generation is unified in `MaterialXGenHw`
      and designed to be subclassed for a new target (own `Syntax`,
      overridden emit methods, registered node implementations).
      But its raw GLSL cannot go through shaderc: bgfx uniforms are
      `Vec4`/`Mat3`/`Mat4`/`Sampler` only, and ShaderGen declares
      float/int/bool uniforms, uniform blocks and a `u_lightData[]`
      struct array.
    - The browser tier has no runtime compiler: the server compiles
      and ships binaries (RenderDebug.md sec 6.3). Whatever is
      generated is text through the existing hash-keyed shaderc
      cache; the browser needs no MaterialX (JsMaterialX exists, but
      is not needed).
    - OpenPBR vs MaterialX: different kinds of thing. MaterialX is
      the LANGUAGE (typed node graph, standard library, ShaderGen);
      OpenPBR (v1.1.1, 2026-04-17, ASWF) is a SHADING MODEL -- the
      parameters and the lobe layering -- in the same row as
      Standard Surface, glTF PBR and UsdPreviewSurface. OpenPBR's
      reference implementation is a MaterialX nodegraph shipped in
      MaterialX's `libraries/`, but the spec stands alone so a
      renderer without MaterialX implements it natively: Cycles'
      Principled BSDF v2 is OpenPBR-aligned, Blender exports OpenPBR
      as its MaterialX surface, Unreal Substrate imports it.
    - Packaging: conda-forge has no `materialx` package (checked).
      MaterialX is Apache-2.0, C++17; Core + Format + GenShader +
      GenGlsl have no external dependencies; the `libraries/`
      directory of `.mtlx` node definitions must ship as data, like
      the shader sources.
    - Prior art on the surface model: `portsmouth/OpenPBR-viewer`
      (MIT, 2026-03, by an OpenPBR co-author) is a three.js material
      demo -- ONE material as global uniforms, one glTF, no
      instancing, no material textures, no denoiser -- but carries
      two spec-tracked GLSL implementations of OpenPBR: a rasterizer
      (`glsl/rasterization/openpbr.frag.glsl`, ~1.3k lines) and a
      path-tracing BSDF (`glsl/pathtracing/openpbr_surface.glsl` +
      the per-lobe files, ~1.5k lines). The app is not reusable
      (three.js + JS); the shaders are.

    **The design.** MaterialX is the material DESCRIPTION; the
    engine keeps the lighting. A `.mtlx` document authors the
    surface inputs -- pattern nodes feeding one surface-shader node.
    **OpenPBR is the canonical surface model**; Standard Surface,
    glTF PBR and UsdPreviewSurface are accepted through MaterialX's
    own translation graphs, not mapped natively. Neither consumer
    uses MaterialX's lighting:

    - Raster: a `BgfxShaderGenerator : GlslShaderGenerator`
      (ESSL-flavoured syntax, uniforms packed into `vec4` lanes,
      samplers as `SAMPLER2D` slots, no light or environment code
      emitted) that generates only a MATERIAL-INPUTS function --
      the OpenPBR parameters -- spliced into the stock mesh shader
      the way the volume stage splices `fcMediumField` today
      (RenderEngine.md sec 5.3). Shadows, EVSM, SH/IBL, WBOIT,
      picking, section clip, per-face palettes stay untouched: the
      user program feeds the mesh program, it never replaces it. The
      mesh shader itself moves to an OpenPBR evaluation, with
      OpenPBR-viewer's rasterizer as the reference, replacing the
      Phong-to-Khronos-spec-gloss fit of today.
    - Cycles: an interpreter from the MaterialX graph to ccl nodes
      -- a table for the stdlib pattern nodes (image, noise, mix,
      math, separate/combine, texcoord, normalmap, ...) and the
      surface node mapped onto `PrincipledBsdfNode`, whose v2
      sockets are OpenPBR parameters, plus the emission/transparent/
      absorption pieces section 6.2 already builds. OpenPBR-viewer's
      path-tracing BSDF is the independent cross-check when raster
      and Cycles disagree. An unsupported node reports once and the
      material renders stock (the sandboxed-failure rule).
    - Document model: `Dialect` gains `MATERIALX`; the `.mtlx`
      text rides `FragmentProgram` (or an included file -- open
      decision 2); the graph's public inputs surface as `Param_*`
      dynamic properties in the REVERSE direction from today (the
      interface is read from the document, not declared by hand)
      and bind to both consumers -- vec4 lanes for raster,
      `ValueNode`s for Cycles. Only `Stage=material` accepts the
      dialect; `post`/`water`/`volume`/`particle` stay `.sc`.

    **Phasing**, each step verifiable with the probe harness of
    sections 6.5-6.7 (`build/conda-debug-occt801`, CPU + CUDA,
    raster control frames):

    0. Vendor MaterialX as a submodule (Core/Format/GenShader/
       GenGlsl only), ship `libraries/` as an asset,
       `Dialect=MATERIALX` validates at document load.
       **DONE 2026-08-31, section 6.8.**
    1. The Cycles interpreter, over MaterialX's own
       `resources/Materials/Examples` (OpenPBR + Standard Surface
       samples) -- first because it needs no shader-language work
       and the phase-A traps (colour spaces, stride, socket names)
       are fresh. **DONE 2026-08-31, section 6.9.**
    2. The OpenPBR mesh shader, then the bgfx ShaderGen target and
       the splice; parity against the Cycles frames the way the
       finish probe measures it. **DONE 2026-08-31, section 6.10.**
    3. Interface: public inputs -> `Param_*`, the demo preview, and
       a material card able to carry a `.mtlx` (the appearance model
       of MaterialStorage.md already has a file slot).
       **The interface and the preview are DONE 2026-08-31, section
       6.11**; the material card is not started.

    **Open decisions** (2, 4 and 5 are provisional; 1 and 3 are
    ruled): (1) OpenPBR canonical -- RULED yes. (2) Where the
    `.mtlx` lives: inline text (diff-able, what `.sc` does) vs an
    included file through FileBlobs (real materials reference image
    files relative to the `.mtlx`; the file route, or rewriting
    `file=` references to blobs). (3) Cycles route -- RULED
    interpreter, not OSL. (4) Phase A is NOT re-expressed in
    MaterialX for now; the interpreter shares `GraphOps` with it.
    (5) The raster splice covers material inputs only; no
    displacement stage (it would reopen the undisplaced-geometry
    problem of RenderEngine.md sec 5.3).

    **Ruled out for phase B, kept for the roadmap:** a client-side
    path tracer in the browser (OpenPBR-viewer's shape: BVH baked
    into textures, full-screen fragment tracing, progressive
    accumulation). It is complementary to the streamed Cycles
    viewport of section 7.1, not a replacement -- no backend, zero
    interaction latency, works from the static snapshot -- but it
    is bounded by texture-baked BVHs, has no top-level BVH for our
    instance-heavy scenes, no compute in WebGL2, no denoiser, and
    mobile GPUs are thermally limited; WebGPU is the honest target.
    See RoadMap.md, long term.


## 9. Traps carried forward

Each of these has already cost time somewhere in this tree:

- **Double colour encode** on a borrowed-frame image (`b3f82b620b`).
  Section 6.1.
- **Two TBB runtimes** in one process, if Blender's bundle is ever
  reconsidered. Section 3.
- **A conda solve that quietly downgrades boost** and breaks every
  binary in the env. Section 3.1.
- **Depth in the wrong space** when a second renderer's image is
  composited (the CAM simulator's stock-derived near/far). Avoided
  outright here by never importing depth. Section 5.3.
- **`drawFrame` is inside someone else's frame** -- no frame of its
  own, no resize, no repaint. Section 5.2.
- A **slow AMD iGPU number** read as a broken HIP port. Section 4.2.
- **A driver library's filename read as a capability**
  (`libnvoptix.so.1` on WSL2). Section 4.1.
- **A vendored tree setting the HOST's compiler flags.** Blender build
  code assumes it is the top-level project and `FORCE`s the global cache
  variables. Section 2.1.
