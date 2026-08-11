# Draw submission: getting the frame's draws onto the GPU

Status: **plan**, plus the instruments it needs. Built so far:
`render instancing:` (what batching already collapses), `render passes:`
(per-pass CPU/GPU cost), and `FC_NO_AUDIT=1` in the harness. No
submission route has been changed yet.

**IMPORTANT: the baseline was corrected on 2026-08-11 and the prize shrank: the
frame is 6485 draws / 10.8 ms submit / 54.8 ms, not 30577 / 32 / 76. See
"The measured problem" and "RESOLVED: the suspect baseline was the
harness measuring itself".**

This is the workstream that follows occlusion culling, and it starts
where `docs/FarFieldProxies.md` §12.19 ends: both sides of box-based
occlusion are measured and both are exhausted. Ten times the occluders
bought 3.1% (§12.16); the tightest possible occludee bound — every
triangle and segment asked separately, at a cost no frame could pay —
buys 8.9% (§12.19). What is left is not culling.

## The measured problem

**CORRECTED 2026-08-11. The numbers this document was scoped against
were wrong twice over, and both errors inflated the prize.** The
superseded headline read "30577 draws, 32 ms submit, 76 ms frame". That
sample was taken (a) with the **cull audit on**, which adds the entire
17727-row scene list to the frame as a second rasterization, and (b)
**before the scene converged**, while occlusion culling was still
resolving. Neither is a frame a user ever renders.

The corrected baseline, read from the converged tail of two audit-off
runs (`clean_run.out` / `clean2.out`, 10 windows each, deterministic
draw counts), against the same rack model and camera:

| | audit ON (converged) | **audit OFF (converged)** | the instrument |
|---|---|---|---|
| draws | 24213 | **6485** | +17728 |
| frame | 80.41 ms | **54.78 ms** | +25.6 ms |
| submit | 28.70 ms | **10.77 ms** | +17.9 ms |
| gpu | 36.16 ms | **19.64 ms** | +16.5 ms |
| prims | 74.1 M | **33.5 M** (5165/draw) | |

(medians of 10 converged windows; audit-off submit spread 10.47-11.04,
gpu 19.50-19.81, frame 53.99-55.95.)

**The audit's cost is exactly the scene draw list**: 6485 + 17727 =
24212, against 24213 measured. That is the id pass re-rendering every
row, and it was on by default -- `cull_audit.py` hard-coded
`RenderDebug_CullAudit = True` until `64cb4090d9`. So **every timing
this workstream has ever quoted included it.**

### What the correction does to the plan

**Draw submission is 20% of the frame, not 42%.**

- submit **10.8 ms** of a **54.8 ms** frame;
- gpu 19.6 ms;
- and `cpuTimeFrame` minus the backend's submit leaves **~44 ms that is
  neither**. bgfx's own field names are explicit: `cpuTimeFrame` is "CPU
  time between two `bgfx::frame` calls" (the whole application frame),
  while `cpuTimeBegin/End` is "**render thread** CPU submit" -- the
  backend issuing GL calls. Our 466-line `submit()`, the cull, the
  instance grouping and Coin's composite all live in the ~44 ms, and
  **nothing currently measures any of it.**

**So the largest item in the frame is the one with no instrument on
it**, and phases 1 and 2 below both target that unmeasured bucket rather
than the 10.8 ms the `render passes:` line accounts for.

**TRAP: the per-draw GPU claim needs re-testing at this operating point.**
The draw-call note measured GPU cost as per-draw *state*, not fill or
triangles. The converged frame pushes **33.5 M primitives across 6485
draws -- 5165 per draw**, about 1.7 Gtri/s over 19.6 ms, which is near
this box's raster throughput. If the GPU half is geometry-bound here,
then merging draws (phase 3) does nothing for it and fewer *triangles*
(far-field proxies / LOD) is the lever. That is a measurement, not a
conclusion -- but it must be taken before phase 3 is justified on GPU
grounds.

WARNING: nothing here needed building to find out. **Read `render frame:`
before designing anything, and state which convergence state and which
audit setting a number came from** -- Phase 0 exists because this
document was first written without it, and this correction exists
because the numbers that replaced the estimate carried neither label.

## ⭐⭐ The constraint that shapes everything: one route, both tiers

**The same route must run on desktop and in the browser.** This is a
direction decision, not a technical one — running FreeCAD from one
codebase across browser, mobile and desktop is the project's first goal,
and a submission path that exists only on desktop forks the thing the
project is for.

What that rules out, and why each is genuinely unavailable rather than
merely inconvenient:

All of the following was **verified against the vendored tree**
(`src/3rdParty/bgfx`, bgfx `e3c8f29c` 2026-07-23) rather than assumed:

- ⛔ **Compute-shader culling on WebGL2.** WebGL2 is GLES 3.0;
  `BGFX_CAPS_COMPUTE` needs GLES >= 3.1 (`renderer_gl.cpp`), and the wasm
  build pins `BGFX_OPENGLES_VERSION 30`. Permanent, not a version away.
- ⛔ **Multi-draw indirect on WebGL2.** `BGFX_CAPS_DRAW_INDIRECT` requires
  `ARB/EXT_multi_draw_indirect`; none exist in WebGL2. bgfx does have a
  real `glMultiDrawElementsIndirect` path — gated off on web.
- ⛔ **`WEBGL_multi_draw`** exists in browsers but **bgfx does not expose
  it anywhere** (no such extension string in the tree). Adding it would
  be a web-only path, which is the fork this constraint forbids.
- ⛔⛔ **`bgfx::Encoder` multi-threading on web.** `BGFX_CONFIG_MULTITHREADED`
  is hard-coded to 0 for Emscripten, `maxEncoders` is clamped to 1, and
  `bx`'s thread/mutex/semaphore bodies are compiled out entirely
  (`BX_CONFIG_SUPPORTS_THREADING` 0). It cannot be forced on. Our own
  `src/3rdParty/bgfx/CMakeLists.txt` already disables it for Emscripten.
- ⛔ **`gl_InstanceID` / `bgfx::setInstanceCount`.** `BGFX_CAPS_VERTEX_ID`
  is *explicitly excluded* on Emscripten, so the attribute-free
  auto-instancing route is closed; a per-object index must ride a real
  `i_dataN` attribute.
- ⛔ **Uniform buffers.** bgfx's GL backend has no UBO path at all —
  uniforms are `glUniform4fv` per uniform per draw. And bgfx's own source
  warns that uniform *arrays* are much slower under Wasm because they
  marshal across to JS. ⇒ **a per-object data texture read with
  `texelFetch`, not a uniform array.**

### ⚠️ Correction: bgfx WebGPU *is* usable from Emscripten now

This document first asserted that bgfx's WebGPU backend is "Dawn-native
and unusable through Emscripten". **That has been false since
2026-06-28** — Emscripten support is merged upstream (bgfx PR #3795),
`renderer_webgpu.cpp` carries a real emdawnwebgpu path, and our own
`bgfx.cmake` already links `--use-port=emdawnwebgpu`. The backend is one
of bgfx's most actively developed. The claim was stale, and it is
repeated in several other docs (`RendererPlan.md`, `RoadMap.md`,
`ViewerUIResearch.md`) which should be corrected as they are touched.

**It changes less than it sounds like, and the reason is specific:**

- ⛔ **Multi-draw indirect is still not available on the web.** It is not
  core WebGPU — only a Chrome flag behind `enable-unsafe-webgpu` — and
  bgfx *emulates* it there by looping N `drawIndirect` calls. That is N
  encoder calls and N draws: **no reduction in the cost unit at all.**
- ⇒ WebGPU would not have helped the thing this document is about.
- ⭐ What it *does* unlock, uniquely, is **compute shaders**, which WebGL2
  can never have — meaning GPU-side culling that compacts an instance
  buffer into one instanced draw, needing no MDI.
- ⚠️ But adopting it means **two** browser paths and two shader packs
  (WGSL + ESSL): iOS < 26, Android < 12 and Firefox-on-Linux still need
  WebGL2. That cuts directly against the one-route constraint, so it is a
  *separate, time-boxed spike*, not part of this plan.

⇒ Everything below is built out of what **both** tiers have today:
instanced arrays (`BGFX_CAPS_INSTANCING` is unconditionally true on
GLES3), `texelFetch`, and ordinary indexed draws.

## The param

`Render_SubmitRoute` (`RenderParams.py` → `RenderDebug`-style view
property override, same pattern as `Render_Occlusion*`):

| value | route | runs on |
|---|---|---|
| 0 | **Classic** — today's per-draw submit | desktop + web |
| 1 | **Encoded** — the same submits, spread over bgfx encoders | desktop + web (1 encoder) |
| 2 | **Batched** — merged geometry, per-object data indexed | desktop + web |

⭐ The point of the param is not a desktop/web switch — every value must
work on both. It is so the routes can be *measured against each other on
one camera in one run*, the way `cull_audit.py` measures occlusion arms,
and so a regression has a one-property bisect.

## Phase 0 -- where the frame's time goes (mostly answered now)

⛔⛔ **The rule this workstream has already paid for twice**: §12.16 built
a mechanism on an unchecked premise, and §12.19's first run divided by
the wrong denominator. The ~24 ms figure is a *per-draw microbenchmark
rate multiplied by a draw count*. Before any of it is optimized, measure
the real frame:

**Corrected** (`render frame:`, converged + audit off): **6485 draws,
1.66 us submit and 3.03 us GPU per draw, 10.8 ms backend submit against
a 54.8 ms frame.** The heading's "32 ms" was the harness measuring
itself; see the RESOLVED section.

Still open, and each of these redirects the phases below:

1. **Which passes the draws belong to. ANSWERED.**: with the audit
   off, `opaque` is essentially the whole frame -- 9.5 ms of the 10.4 ms
   backend CPU and 18.2 ms of the 18.4 ms GPU. Everything else
   (background, sectioncap, the numbered overlay passes) is under 0.5 ms
   apiece. There is **no secondary pass worth deleting**: the hoped-for
   "a pass contributing thousands of draws for a small visual effect"
   does not exist in this scene. Shadow, AO and OIT are not live on this
   camera.

   ` render passes (top 8 of 14, totals 10.19/18.38): opaque 9.31/18.17
   pass80 0.47/0.03 background 0.17/0.07 pass81 0.15/0.09 ... `
2. **NOW THE PRIORITY -- the ~44 ms nothing measures.** `cpuTimeFrame`
   54.8 ms minus the 10.8 ms backend render-thread submit leaves ~44 ms
   in our own code and Coin's: the cull, `buildInstanceGroups()`, the
   466-line `submit()` per-draw C++, the six `setUniform`,
   `setTransform`, and `SoGLRenderAction` compositing on top. Phases 1
   and 2 both target this bucket blind. **Break it down before building
   either.**
3. The **fill / line / point** mix -- sec 12.19 found ~2787 point and ~2800
   line draws among 8388 rows, and they do not cost the same.

**This still points away from phase 3, and now more sharply**: the
backend submit it would reduce is 10.8 ms of 54.8, and at 5165
primitives per draw the GPU half may be geometry-bound rather than
draw-bound. Measure (2) and the geometry-bound question before choosing.

### Phase 0.5 — how much does the instancing we already have collapse?

⚠️ An earlier draft of this plan listed a bug here: "instancing never
engages at render cache 3 when Render Type is Default". **That was fixed
on 2026-08-07** (`a06f4c2938`, an ancestor of this branch) — the gate in
`ViewProviderExt.cpp::shapeInstancingActive()` asks the live backend
(`Render::Renderer::activeCount()` / `instancingHint()`) instead of the
preference string, with an observer rebuilding Part visuals when a
backend attaches. There is no bug to fix here.

The open question is the *quantitative* one, and nothing currently
answers it: **there is no instancing readout anywhere in the renderer.**
The benchmark log reports `scene consumed: 5954 draws, 1242 meshes` at
load and `indexed 17727 of 17727 draws` at cull time, and nothing in
between says how many submits the two instancing layers actually
collapse:

- **Part-side (Coin/TShape) instancing** — `shapeInstancingActive()`,
  identical tessellations shared across placements;
- **renderer-side grouping** — `buildInstanceGroups()`, identical
  geometry content *and* identical material folded into one instanced
  submit.

✅ **BUILT AND MEASURED** — `render instancing:`, on the frame-timing
cadence:

```
render instancing: 960 groups (495 usable, 465 singleton) over 17727 rows
  | 495 submits replaced 5432 draws | refused 0 groups / 0 draws
  | thinned to one: 0 draws
```

⭐⭐ **Instancing is working, and it is already the biggest lever in the
renderer**: 495 instanced submits stand in for 5432 scene rows. Nothing
is refused by the backend, and no group is thinned to a single visible
member by culling.

What that leaves, and it is the honest scope of everything below:

- **465 singleton groups** — geometry that occurs once. No instancing
  layer can ever batch these; only *merging distinct meshes* (phase 3)
  can.
- **~12300 of the 17727 rows are in no usable group at all.** That, not
  the 5432, is phase 3's target.
- The residual is still large: 30577 draws a frame after all of it.

⭐ Worth stating plainly because it nearly cost a phase: the note that
recorded the old bug also recorded its fix, in a later section, and an
earlier draft of this plan proposed re-fixing it. Read the whole record
before planning work against it — and prefer a readout to a belief.

## Phase 1 — Encoded: spread the same submits across cores

⚠️⚠️ **Demoted, and the honest label is desktop-only.** The plan first
claimed the browser would "run the identical code with one encoder". It
would — `begin()` returns a valid encoder and `end()` is a no-op — but
the verification found this buys the browser *exactly nothing*:
`BGFX_CONFIG_MULTITHREADED` is 0 on Emscripten, `maxEncoders` is clamped
to 1, `bx`'s threading is compiled out, and bgfx's free functions
(`bgfx::submit` and friends) **already route to encoder 0**. So adopting
`Encoder` is a no-op on web by construction.

**Demoted again by the 2026-08-11 correction.** The claim here was "32 ms
of desktop CPU divided by 8 cores is the single largest number on this
page". It was not 32 ms, it is **10.8 ms**, and that 10.8 ms is the
**backend render-thread** time (`cpuTimeBegin/End`), which is not what
`Encoder` parallelizes -- Encoder splits the *API-side* building of the
draw-item array, which lives in the unmeasured ~44 ms. So this phase's
prize is unknown rather than large, and it cannot be sized until the
~44 ms is broken down. It remains desktop-only, and it still does nothing
for the GPU half on either tier.

- N encoders, one per worker, partitioning the draw list by contiguous
  range; reuse `Render::physicalCoreCount()`/`occluderWorkers()` so the
  renderer sizes its pools one way.
- ⚠️ **Ordering is the whole risk.** bgfx sorts by view and sort key, so
  encoder assignment must not change order within a view. Sorted
  transparency depends on per-draw depth keys — keep it on one encoder
  until proven safe.
- ⚠️ Do **not** try to force `BGFX_CONFIG_MULTITHREADED` on for
  Emscripten: it compiles references to `bx::Mutex`/`Thread` that link to
  nothing.

**Gate**: **pixel-identical** to Classic, and the submit timer falls by
roughly the worker count on desktop. ⚠️ Read timings from a min/med/max
spread, never a last sample (§12.14, corrected in §12.15).

## Phase 2 — do less per draw (the phase that helps the browser)

Phase 1 buys the browser nothing, because the browser has one thread.
This one helps both.

- **Cache the recipe.** Most of what the 466-line `submit()` computes per
  draw is a pure function of the draw and does not change between frames:
  program selection, texture routing, the bgfx state word, unpacked
  colors. Compute once into a `SubmitRecipe`, invalidate on the scene
  version that already exists (`cullSceneVersion`).
- **Collapse the uniform sets.** Six `setUniform` calls per draw become
  one vec4-array set.
- ⭐ This is the *incremental-by-default* shape the project already
  applies elsewhere: never rebuild per frame what changed once.

**Gate**: pixel-identical, and measured on **both** tiers — the browser
number is the one this phase exists for.

## Phase 3 — Batched: merge distinct meshes into one draw

The structural fix, and the only one that *removes* per-draw cost instead
of dividing it. Phases 1-2 make each draw cheaper or more parallel;
17727 draws remain 17727 draws.

- **Megabuffer per state bucket.** Concatenate the vertices and indices
  of many distinct meshes into one large VB/IB, keyed by the state that
  cannot vary within a draw (program, textures, blend, depth, cull).
- **Per-vertex object id.** One extra integer attribute naming which
  object a vertex belongs to.
- **Per-object data indexed, not bound.** Model matrix, diffuse, flags in
  a **data texture read with `texelFetch`** — verified available in
  bgfx's ESSL path, and integer samplers (`USAMPLER2D`) with it. ⛔ **Not
  a uniform array**: bgfx's GL backend has no UBO support at all, and its
  own source warns uniform arrays marshal Wasm→JS and are much slower
  there. ⇒ This is §12.17's "materials must become bindless first", in
  the one form both tiers support.
- ⚠️ **The object index must ride a real instance attribute** (`i_dataN`,
  always `vec4 float` — there is no integer instance attribute, so pack
  the index as a float, exact to 2^24). `gl_InstanceID` is unavailable:
  `BGFX_CAPS_VERTEX_ID` is explicitly excluded on Emscripten.
- ⚠️ **Two hard budgets**: instance data is capped at 16 vec4 (256-byte
  stride), and instance attributes consume ordinary vertex attribute
  slots — WebGL2 guarantees only **16 total**, and a 4x4 matrix alone is
  4 of them. A design that wants matrix + color + index + mesh attributes
  has to be counted against 16 before it is written.
- **Culling has to keep working.** The cull mask removes *objects*, and a
  batch is one draw, so either compact the index buffer per frame from
  the visible set (a memcpy of ranges into a transient IB) or collapse
  hidden objects in the vertex shader (`w = 0`). ⚠️ The second wastes
  vertex work and is what makes a naive batcher slower than what it
  replaced; measure both, and remember the occlusion pass exists to
  *remove* work, not to move it to the GPU.
- ⛔ **Not every draw qualifies.** Clip planes, user shaders, water /
  glass / fire / cloud, on-top, autozoom, per-face outline — the same
  exclusions `instancableDraw()` already lists. Those keep the Classic
  route. **Measure the qualifying share before building**: if it is 60%
  of draws, the ceiling of this phase is 60%.

**Gate**: pixel-identical, the draw count actually falls, and the frame
is faster on both tiers.

## Phase 4 — the two free levers (try before any of the above)

Both are bgfx-native, cost no portability, and were found by reading the
vendored backend rather than by design:

- **`bgfx::setViewMode(Sequential)`.** The default sorts draws by
  program/state to minimize backend state changes. Our scene is already
  grouped by material, so the sort may be pure cost — or it may be
  earning its keep. One call, measurable in an afternoon, either way.
- **The `setTransform` cache-index form.** `setTransform(mtx)` returns a
  cache index that `setTransform(cache, num)` re-references; we call the
  copying form everywhere, paying a 64-byte matrix copy per draw.
  Instanced groups and repeated placements are the obvious users.

⭐ These belong first in the order of work: they are hours, they help
both tiers, and they change the baseline every later phase is measured
against.

## Traps carried in from the occlusion work

- ⚠️⚠️ **A pixel compare needs a converged scene.** The same file measures
  0 / 108 / 616 differing pixels on framing and timing alone. Converge,
  re-fit, then compare.
- ⚠️ **Never bench while a build runs**, and rebuild **all** targets after
  a header change — a stale sibling segfaulted a test binary once.
- ⚠️ Delete the harness output file before a re-run: an
  `until grep DONE` waiter fires instantly on the previous run's DONE.
- ⭐ Counts here should be deterministic like the culler's; only timings
  need the min/median/max treatment.

## RESOLVED: the suspect baseline was the harness measuring itself

**Settled 2026-08-11 from the existing run logs -- no new run, no new
instrument.** The question was why `ViewDebugScene` cost ~15 ms CPU and
~16 ms GPU in runs where "nothing should have turned it on":

```
render passes (cpu/gpu ms, top 8 of 16, totals 26.46/34.99):
  debugscene 15.41/16.52   opaque 9.95/18.26   pass79 0.57/0.03  ...
```

**The cull audit turned it on, and the harness turned the audit on.**
`scripts/cull_audit.py` hard-coded `RenderDebug_CullAudit = True` until
`64cb4090d9`; `idPassRender` is `(viewMode == 11 || cullAudit)`, so the
id pass ran in **every frame of every run ever taken**. The note that
recorded the question also recorded, one section later, the `FC_NO_AUDIT=1`
that fixes it -- the flag had simply never been exercised.

The claim in the superseded text that "the run had the cull audit off and
produced zero audit lines" was **false**: the log for that run carries 84
`render cull audit:` lines. Nothing about the attribution was wrong. The
instrument was correct and was reporting a pass that was genuinely running.

Proof, from the two runs side by side (converged tails): turning the
audit off removes **17728 draws**, which is the 17727-row scene draw list
to within one, and takes the frame from 80.4 ms to 54.8 ms. The table at
the top of this document is that comparison.

**What this cost, and the rule it earns:**

- Every number in the first draft of this plan was inflated ~3x on
  submit and ~2x on GPU.
- **A default-on instrument is worse than no instrument.** The audit
  defaulted on because it was the only consumer of the harness; the
  moment a second consumer (frame timing) appeared, the default became a
  silent multiplier on every number the second consumer produced.
- **`os.environ.get("FC_NO_AUDIT", "") in ("", "0")` reads as "off by
  default" and means the opposite** -- unset gives `""`, which is in the
  tuple, which enables the audit. The polarity of an env-var default is
  worth reading twice.
- The earlier fix in this family was real and is kept: per-view stats are
  resolved to a pass **in the frame the sample was taken**, never by raw
  bgfx view id, because `idMap` is rebuilt each frame from the live set.

⚠️ The instrument already had one bug of this family and it is fixed:
per-view stats were accumulated by **raw bgfx view id**, but `idMap` is
rebuilt every frame from which passes are live, so a window mixing
frames with different live sets attributed one pass's milliseconds to
another. It now resolves id → pass **in the frame the sample was taken**
and only for passes whose mark is set.

`scripts/cull_audit.py` gained **`FC_NO_AUDIT=1`** for exactly this
reason: the audit re-renders every scene draw into the id image, so a
frame timing taken with it on is a measurement of the measuring
apparatus. Cull numbers need it on; frame timings need it off; the two
cannot come from one row.

## Backends: what Vulkan would and would not bring

Researched against the vendored tree (`renderer_vk.cpp` /
`renderer_gl.cpp`). **Verdict: do not add the Vulkan backend now.
Uncomment nothing at the `typeMap` entry.**

### The blocker is worse than "unsupported"

`renderer_vk.cpp` reinterprets `platformData.context` as a **`VkDevice`**:

```c
if (NULL != g_platformData.context) { m_device = m_externalDevice = (VkDevice)g_platformData.context; }
```

We put a GLX context handle in that field. Under `RendererType::Vulkan`
that is a type-confused pointer — immediate UB, not a graceful failure.
And bgfx implements **no GL↔VK interop** (no `EXT_memory_object` /
`external_memory` in either backend), so the whole export/import/sync
path would be ours to write and maintain against upstream.

### bgfx forfeits the Vulkan wins that matter

- **One render thread, every backend.** No `std::thread`, no
  `vkCmdExecuteCommands`, no secondary command buffers in
  `renderer_vk.cpp`; even `sort()` runs inside the backend.
  `bgfx::Encoder` parallelizes only the building of the unsorted
  draw-item array — **Vulkan does not change what Encoder can do.**
- **No bindless.** `VK_EXT_descriptor_indexing` is not used anywhere.
  The one feature that would collapse thousands of material-varied draws
  into a few indirect batches is not implemented.
- ⇒ The two largest wins in NVIDIA's CAD comparison — multi-threaded
  recording and command-buffer *reuse* — are architecturally
  unavailable; bgfx rebuilds the command stream every frame.

### The caps delta is ~nothing on our hardware

GL 4.6 on this box already exposes indirect draw, indirect count and
compute. Vulkan adds variable-rate shading and external textures, and
nothing that reduces draw cost.

### The per-draw mechanism difference is real, and second-order

GL commits uniforms through a **hash-map lookup per 4-float register**
and rebinds attributes per draw; Vulkan memcpys into a scratch buffer
and reuses descriptor sets via **dynamic offsets**. Estimated **2-3x on
the submit half** — 32 ms → ~12-18 ms. ⚠️ That multiplier is *inference*
from mechanism plus NVIDIA's hand-written CAD sample (7.8 → 1.8 ms at
44k draws); no bgfx-specific VK-vs-GL draw-cost benchmark appears to
exist. Vulkan also **adds** ~15 MB/frame of uniform writes, because it
re-uploads whole constant blocks where GL skips unchanged uniforms.

### Interop, if it were ever wanted

Vulkan offscreen + `VK_KHR_external_memory_fd` → `GL_EXT_memory_object_fd`
works on **Linux+NVIDIA** (the extensions are present on this box) and
has a Windows path. ⛔ **macOS is dead**: Apple GL is frozen at 4.1 and
never shipped `EXT_external_objects`.

### ⇒ When Vulkan becomes cheap

When Coin no longer composites into the viewport — i.e. when the
renderer owns the whole view — the blocker evaporates and Vulkan is a
small incremental step. It should be a *consequence* of that project,
not its justification. Keep the `spirv` shader profile building
meanwhile so the option stays cheap to exercise.

## ⭐ The free lever nobody has pulled

`bgfx::renderFrame()` is called immediately before `bgfx::Init` **on the
same thread**, which puts bgfx in **single-threaded mode**: no API/render
thread overlap, so our ~30 ms of submission and the GUI thread are
serialized today. Moving the render thread off the GUI thread overlaps
them. Backend-independent, cheap, and it improves the GL path we already
ship. ⚠️ Verify what it does to the Qt/Coin co-existence — Coin issues GL
on the GUI thread, and that is exactly the constraint that made
single-threaded mode the safe default in the first place.
