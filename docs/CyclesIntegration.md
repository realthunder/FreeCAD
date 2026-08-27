# Cycles integration

Bringing Blender's Cycles path tracer into the fork as a vendored
renderer, feeding a 3D view that draws nothing of its own but the
selection highlight. **Nothing is built yet** -- this is the plan and
the rulings, written 2026-08-27; work starts next session.

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
- **GPU acceleration targets both NVIDIA and AMD.** NVIDIA is testable
  on this box, AMD is a ship target only. Section 4.
- **The view draws only the selection highlight.** Cycles' image is the
  base layer; no document geometry is drawn by the host. Section 5.

Deliberately NOT decided yet: whether the Cycles view is a mode on an
existing 3D view or its own view type (section 5.4), and whether
picking eventually moves to a Cycles id pass (section 5.5).


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

### 4.1 NVIDIA -- available and testable on this box

Verified 2026-08-27 on the WSL2 dev box:

- `/usr/lib/wsl/lib` carries **`libcuda.so.1` and `libnvoptix.so.1`**.
  The OptiX runtime is passed through to WSL2, which was the open
  question.
- The GPU is an **RTX 3070 Ti Laptop, compute capability 8.6 (Ampere),
  8GB VRAM**, driver 592.00. Ampere has RT cores, so the OptiX device
  gets hardware ray traversal and will comfortably beat the plain CUDA
  device. 8GB is the real constraint on scene size.

To BUILD the kernels, two things are missing and must be added:

- **nvcc.** conda-forge has `cuda-nvcc` (13.3 at survey time).
  **Pin it deliberately.** Cycles' CUDA kernel build is version-picky
  and Blender pins a specific CUDA in its bundle; taking whatever is
  latest is how this breaks.
- **OptiX SDK headers** (`OPTIX_ROOT_DIR`). These are **not on
  conda-forge** -- NVIDIA licence. Either the headers-only SDK
  download, or lift them out of Blender's `lib/linux_x64` bundle, which
  ships them. This is the one place where Blender's bundle is still
  useful to us.

Keep `WITH_CYCLES_CUDA_BINARIES=OFF` at first (it is already the
default). Compiling the CUDA and OptiX kernel binaries is by a wide
margin the slowest part of a Cycles build.

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
drawn. The host draws **no document geometry**. Cycles' image is
blitted as the base layer, and the selection highlight goes on top.

This is a large simplification and it is the reason the design is
tractable. It means **Cycles geometry and bgfx geometry never have to
interleave by depth inside one frame** -- the problem the CAM simulator
had to solve in `docs/CAMSimRenderPort.md` section 8. Here the
path-traced image is simply the bottom layer.

### 5.2 The host contract already exists

`Render::FrameConsumer` (`src/Gui/Renderer/Renderer.h:2497`) is exactly
the hook: `framePasses()`, `overlayPasses()`, and
`drawFrame(DrawSurface &)`, drawing inside a 3D view's frame on pass
ids from that view's own block. `7b148e9cdf` already established that a
frame consumer may put its image into the host's depth.

Cycles emits a float buffer, not draw calls, so the consumer is thin:
buffer -> texture -> one blit pass. Most of the work is in getting the
buffer there safely, not in drawing it.

**The threading rule is the first bug waiting to happen.**
`drawFrame()`'s contract forbids crossing the frame boundary: no bgfx
frame of its own, no resize, no repaint request. But Cycles runs its
session on its own threads and calls the `OutputDriver` from one of
them. So the handoff must be a lock-and-copy (or an ownership swap)
into a staging buffer done OUTSIDE `drawFrame`, with `drawFrame` only
consuming what is already resident and uploaded.

### 5.3 Both highlight routes

Ruled: support both, and let the cheap one be the default.

- **On-top route (cheap).** The highlight is drawn in the overlay run,
  ignoring occlusion. No depth data leaves Cycles, no reconciliation
  needed, and it works the moment the colour blit works. Highlights are
  frequently drawn on top anyway, so this is a legitimate end state,
  not just a stepping stone.
- **Depth route (correct).** Cycles emits its **Depth AOV** alongside
  the colour pass; that depth is blitted into the host depth and the
  highlight depth-tests against it, so a highlight behind geometry is
  properly hidden.

  The known cost is depth-range reconciliation. The CAM simulator hit
  precisely this: its near/far came from the stock size rather than the
  camera, so its depth lived in a different space from the host's.
  Cycles' depth is a distance in camera space, so it must be mapped
  into the host's projection before it means anything. Budget for it.

Build the on-top route first; it unblocks everything else and is the
fallback if the depth mapping fights back.

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
continue functioning while the view draws no geometry.

A later upgrade, not a requirement: Cycles' **Object Index /
Cryptomatte** pass would give pixel-accurate picking straight out of
the render, with no CPU ray cast at all.


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
   same scene on OptiX and compare against the CPU image and timing.
6. Enable `WITH_CYCLES_DEVICE_HIP` and confirm it BUILDS. Do not expect
   it to run here (section 4.2); record that it is unexercised.

Phase 2 -- into the tree, no FreeCAD scene yet.

7. Submodule at `src/3rdParty/cycles`; a `BUILD_CYCLES` option
   defaulting **OFF** (bgfx and the CAM simulator default ON now, but
   Cycles carries a heavy build and has to earn that).
8. A minimal renderer unit that renders a hard-coded scene to a buffer,
   driven from Python and saved to a file. This proves threading,
   lifetime and teardown with no translation layer in the way.

Phase 3 -- scene translation (section 6).

9. Render cache -> Mesh/Object/Camera/Background, geometry first.
10. Materials, per-face slots, environment.

Phase 4 -- the viewport.

11. Progressive `OutputDriver` -> staging buffer -> texture ->
    `FrameConsumer` blit, on-top highlight route (section 5.3).
12. Depth AOV and the depth-correct route.
13. Cancel-on-camera-move, sample and time budget, denoise on.

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
  composited (the CAM simulator's stock-derived near/far). Section 5.3.
- **`drawFrame` is inside someone else's frame** -- no frame of its
  own, no resize, no repaint. Section 5.2.
- A **slow AMD iGPU number** read as a broken HIP port. Section 4.2.
