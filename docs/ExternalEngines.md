# External render engines

Design evaluation and plan for accepting external render engines --
Cycles is the concrete target -- and for what "Blender compatible"
should and should not mean in this fork.

Status: direction settled in discussion 2026-08-24; no code, no build
work started. Related: `docs/RoadMap.md` (workstreams this slots into),
`docs/RenderEngine.md` (the bgfx engine), `docs/CAMSimRenderPort.md`
(the plugin draw API this document leans on for its Python tier),
`docs/ComputeBoundaries.md` (the server tier section 6 lines up with).


## 1. The ambition, and the finding

The ambition: let FreeCAD accept serious third-party rendering
technology -- the way Blender's ecosystem attaches engines like Cycles,
LuxCore or Octane -- rather than every visual capability being written
in-tree.

The finding, up front: **"accept Cycles" is feasible and fits this
fork's architecture unusually well, but a Blender-compatible API is the
wrong way to get it.** The two goals decouple. Cycles has already been
adopted by a non-Blender CAD host (Rhino) without a line of Blender API
emulation, and that is the recipe to copy. A narrow slice of Blender
compatibility is still worth building -- the drawing-API workalike in
section 7 -- but it serves plugin authors, not engine vendors.


## 2. What "Blender's API" actually is

Three layers, with very different clonability:

- **`bpy`, the data model** -- scene, objects, materials as node trees,
  the depsgraph, RNA introspection. This is what external engine
  *addons* (LuxCore, Octane, ProRender) actually read: their Python
  glue walks Blender's data and feeds their engine. It is enormous,
  welded to Blender's DNA structs, and has never been successfully
  cloned. A compatibility surface deep enough to run those addons
  unmodified is a non-starter -- and anything shallower runs none of
  them, because the addons' assumptions (node-tree materials, Blender's
  object model, depsgraph update streams) live above the API calls.
- **`bpy.types.RenderEngine`** -- the host-side socket
  (`view_update`/`view_draw`, a display driver for the result image).
  Small, but it is the plug, not the appliance: implementing it buys
  nothing unless the engine's Blender-specific translator also runs,
  and Cycles' translator (`intern/cycles/blender/`) reads `bpy` by
  design. Useless to us verbatim -- but its *shape* is worth copying,
  and section 5 does.
- **The `gpu` module** -- small, self-contained, backend-agnostic
  immediate/batch drawing for addons. The one layer where
  compatibility is both achievable and worth something (section 7).

The conclusion of the layering: engine vendors do not integrate against
a standard API; they integrate against each host, once, through the
host's scene access and display hooks. What a host can do is make that
one integration cheap. That is the design target.


## 3. The precedent: Rhino, not Blender

Cycles was relicensed to **Apache 2.0 in 2021 specifically so hosts
other than Blender could adopt it** (license-compatible with this
project's LGPL). The adoptions that followed prove the recipe:

- **Rhino** (McNeel): Cycles is the "Raytraced" viewport mode since
  Rhino 6, integrated via their own C wrapper (ccycles) and their own
  scene translator. No Blender API anywhere.
- **Poser** ("SuperFly"): the same pattern.

The recipe, in four parts:

1. Build Cycles standalone -- it is deliberately kept
   host-independent, with a C++ session API (`ccl::Session`,
   `ccl::Scene`); upstream ships no official C API (McNeel maintains
   their own wrapper).
2. Write a translator: host scene -> `ccl::Scene` (meshes, Principled
   BSDF materials, lights, camera, world).
3. Display the progressively refining result in the host viewport,
   compositing host overlays (edges, gizmos, selection) on top.
4. Push incremental updates as the user edits, so the path tracer
   restarts only what changed.


## 4. Why this fork is unusually well placed

Point by point against that recipe:

- **The engine socket already exists, with two plugs in it.**
  `Render::Renderer` + `RendererLib` + `RendererFactory` registration,
  selected by type string at runtime (`Renderer.h`, `Renderer.cpp`).
  Exactly two backends register today -- `BGFXRendererLib`
  (`BGFXRenderer.cpp`) and `DiligentRendererLib`
  (`DiligentRenderer.cpp`) -- so a `CyclesRenderer` is the **third
  backend behind an existing, proven interface**, not a new
  architecture. (The Coin render-cache GL path is not a factory
  backend; it is the fallback that draws when no backend claims the
  view.)
- **The translator's input already exists.** Every backend is fed
  `setScene(DrawCallList)` from the Coin render caches: cacheId-keyed
  tessellations (`MeshData`) with `generation` counters -- precisely
  the identity-plus-dirty tracking an incremental path tracer needs to
  restart only what changed. `SceneDump` is already a versioned,
  serialized scene snapshot. No new scene-extraction machinery is
  required; Rhino had to build this part, we get it from the feed.
- **Materials map nearly one to one.** The fork's appearance system is
  PBR-first: metallic/roughness, per-face materials, IBL environments,
  metal presets on measured F0, and material colours stored linear
  (the pipeline is colour-managed end to end -- decode in, encode
  out). That is a direct mapping onto Cycles' Principled BSDF and
  world shader; Cycles likewise works linear with an output transform
  on display. Legacy-Phong hosts have a semantic gap here; we no
  longer do.
- **Compositing is a solved pattern here.** The bgfx backend renders
  first and Coin composites everything the backend does not claim on
  top (`View3DInventorViewer::renderScene`). A path tracer does not
  draw CAD edges or manipulators; a Cycles backend takes the same
  contract -- it paints the shaded scene, the existing overlay stack
  draws lines and gizmos over it. Picking stays Coin-side and is
  untouched.
- **Progressive refinement fits the frame loop we already run.** The
  idle temporal accumulation work established the "keep improving the
  image while the camera is parked, repaint on each improvement"
  cadence and its host contract. A path tracer is the same loop with
  samples instead of TAA frames.
- **It reaches the server tier naturally.** Cycles will never run in a
  browser, and does not need to: on the headless serving rig a Cycles
  backend does *pixel* streaming (send frames) where bgfx does *scene*
  streaming (send geometry). A raytraced mode in the thin client, with
  the heavy compute server-side, is squarely the
  `docs/ComputeBoundaries.md` direction.


## 5. The design: Cycles as the third RendererLib backend

A `CyclesRendererLib` registering "cycles" with the factory, selected
like any backend. Pieces:

- **Session ownership.** One `ccl::Session` per active raytraced view.
  Device selection (CPU / CUDA / OptiX / HIP / Metal / oneAPI) is a
  render parameter, defaulting to Cycles' own preference order.
- **Scene translation.** `setScene(DrawCallList)` -> `ccl::Scene`.
  Meshes from `MeshData` arrays keyed by `cacheId` (re-upload only on
  `generation` change); per-draw transforms from the DrawCalls;
  materials from the PBR appearance (Principled BSDF); the IBL
  environment as the world shader; the fork's bulb/directional lights
  as Cycles lights. Per-face materials translate as Cycles
  per-triangle shader indices -- the `materials` stream already
  carries the palette index per vertex.
- **Display.** Cycles renders into its display buffer; the backend
  blits the latest tonemapped result into the widget framebuffer each
  `render()` call and requests another repaint while unconverged --
  the idle-accumulation host contract. Denoising via OpenImageDenoise
  once sampling crosses a threshold, like every modern Cycles host.
- **Incremental sync.** Camera moves reset sampling only; a transform
  change updates one object; a `generation` bump re-uploads one mesh;
  a material edit updates one shader. This is where the integration
  effort concentrates (it is where Rhino's did), and where the feed's
  identity model pays off.
- **Interface accommodations** in `Render::Renderer` -- modest and
  contained: an async "result improved, repaint" signal (the
  idle-accumulation mechanism generalized), and a way for a backend to
  declare "progressive: composite my latest image, do not expect a
  complete frame per call". Nothing else in the interface assumes
  rasterization.

Explicitly not in scope: replacing the interactive engine. This is a
view *mode*, like Rhino's Raytraced mode -- bgfx remains the
interactive renderer, and the near-term focus stays the bgfx
workstream.


## 6. The honest costs

- **Build infrastructure is the long pole.** Cycles brings
  OpenImageIO, OpenColorIO, Embree, TBB, optionally OpenVDB and OSL,
  OpenImageDenoise, plus per-vendor GPU toolchains if we ship GPU
  kernels. Most dependencies exist on conda-forge; there is no
  ready-made Cycles package, so this is a feedstock project of at
  least the SMESH/VTK magnitude, probably larger. It is also the
  cheapest thing to de-risk first: a spike that builds Cycles
  standalone against our conda stack proves feasibility before any
  interface work.
- **Incremental sync is real engineering.** Naive full-scene rebuilds
  on every edit would make the mode feel broken on large models.
- **A second scene consumer hardens the feed contract.** The
  DrawCallList/MeshData contract gets its first consumer that is not
  our own code; ambiguities we tolerate internally become bugs.
  (This is also a benefit -- the same hardening serves the plugin
  API.)
- **Vendoring and tracking.** Cycles moves with Blender's release
  cadence; we would pin and update deliberately, as with every
  vendored engine.


## 7. The Blender-compatible piece that IS worth building

One layer, later, on top of the plugin draw API from
`docs/CAMSimRenderPort.md`: a **`gpu`-module workalike** for the Python
plugin tier -- `GPUShader`, `GPUBatch`, `GPUVertBuf`/`GPUIndexBuf`,
`GPUTexture`/`GPUFrameBuffer`/`GPUOffScreen`, `gpu.state`, possibly
name-compatible built-in shaders (`from_builtin('UNIFORM_COLOR')`,
`'POLYLINE_UNIFORM_COLOR'`, ...). The facade's surface already matches
it almost one to one, which is no accident -- both were sized by what
overlay-class visuals actually need.

What it buys: the *drawing half* of Blender's addon ecosystem becomes
portable with mechanical changes, and FreeCAD plugin authors get an
API with ten years of documentation, examples and answered questions
behind its shape. What it does not buy: running Blender addons
unmodified -- their `bpy` half does not port, and we do not pretend it
does.

Blender's own history is the design guidance: their raw-GL addon
surface (`bgl`) died when Metal/Vulkan backends arrived, and the
backend-agnostic `gpu` module is what survived. Our facade starts on
the surviving side of that line.


## 8. Alternative considered: USD/Hydra

Implementing a Hydra scene delegate would open the door to every Hydra
render delegate (RenderMan, Arnold, ProRender, Storm, Embree). It is
the industry-standard "accept any renderer" socket -- more so than
anything Blender-shaped -- and the honest answer if "many engines" ever
outranks "Cycles specifically".

Deferred, not rejected: the USD dependency is enormous; the
Cycles-on-Hydra path (hdCycles) has a shaky maintenance history, so
Hydra would not even be the best route to the named target; and a
Hydra delegate would sit beside a lean scene feed we already own and
already stream. If a second or third engine is ever wanted, re-open
this section first -- one Hydra delegate then beats per-engine
translators.


## 9. Sequencing

1. **Now: nothing.** The CAM simulator port and its facade
   (`docs/CAMSimRenderPort.md`) proceed unchanged; Cycles does not
   alter stage 1, it raises the stakes of getting the facade's shape
   right.
2. **Spike (any time, independent):** build Cycles standalone against
   the conda stack, render its XML example scene on this box's GPU.
   Proves the long pole. No FreeCAD code.
3. **Backend skeleton:** `CyclesRendererLib` registering with the
   factory; static scene translation (geometry + materials + camera,
   full rebuild on change); blit-and-refine display. A first
   raytraced frame of a real document.
4. **Incremental sync + denoise + parameters:** the mode becomes
   usable on large models.
5. **Server tier:** pixel streaming from a headless Cycles backend to
   the thin client.
6. **Python `gpu` workalike:** after the plugin draw API has settled
   through the CAM simulator port (its own doc's stage 2).
