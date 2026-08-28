# Cycles integration

Bringing Blender's Cycles path tracer into the fork as a vendored
renderer, feeding a 3D view that draws nothing of its own but the
selection highlight. Written 2026-08-27 as the plan and the rulings;
phases 0-3 are built (section 8 carries the record per phase, section
6.2 the translation as it stands). Phase 4, the viewport, is next.

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

Open. Two shapes, both viable:

- A **render mode toggled on an existing 3D view** -- fewest moving
  parts, and the camera is already the right one.
- A **Cycles cell beside the normal view**, with a linked camera, using
  the split-view/ViewArea canvas that just landed
  (`docs/SplitViews.md`). Model on the left, path-traced preview on the
  right. This falls out of the placement work almost for free and is
  the nicer product; it is worth prototyping once the mode works.

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
  `SceneTranslator`, and starts it. A restated scene tears the session
  down and builds another -- the device setup and a full translation
  per change. Incremental updates keyed the way the mesh map already
  is (cacheId + generation) are the later step; the key is the cache's
  own contract, so nothing in the translation has to change for it.
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
  roughness; `Render_Light` bodies and unlit draws become emission;
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
caps use.

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
(sections 5.7, 5.8 and 5.9).

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
tier over the existing stream (section 7).


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
