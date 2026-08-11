# Draw submission: getting 17727 draws onto the GPU

Status: **plan**. Nothing here is built yet.

This is the workstream that follows occlusion culling, and it starts
where `docs/FarFieldProxies.md` §12.19 ends: both sides of box-based
occlusion are measured and both are exhausted. Ten times the occluders
bought 3.1% (§12.16); the tightest possible occludee bound — every
triangle and segment asked separately, at a cost no frame could pay —
buys 8.9% (§12.19). What is left is not culling.

## The measured problem

⭐ **Measured, not estimated — and the estimate was wrong in both
directions.** An earlier draft of this document multiplied "17727 draws"
by a per-draw microbenchmark rate and called it ~24 ms. 17727 is the
size of the *scene draw list*, not the number of draws a frame submits:
each row goes to several views (color, prepass, shadow, on-top,
outline). The renderer's own `render frame:` line has carried the real
numbers all along:

```
draws 30577 prims 81141172 (2654/draw) | per-draw submit 1.06us gpu 1.26us
frame 76.28ms  submit 32.34ms  gpu 38.56ms
```

⇒ **30577 draws, 32 ms of CPU submission, in a 76 ms frame.** Two things
follow that the estimate hid:

- the draw count is **1.7x** what was assumed, and
- ⭐⭐ **the GPU's per-draw cost is the larger half** (1.26 us x 30577 =
  ~38 ms). The draw-call note already measured that this is per-draw
  *state*, not fill or triangle count. So removing a draw is worth
  ~2.3 us across both processors, and a change that only makes CPU
  submission cheaper leaves the bigger half on the table.

⚠️ Nothing here needed building to find out. **Read `render frame:`
before designing anything** — Phase 0 below exists because this document
was first written without it.

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

## Phase 0 — where the 32 ms goes (partly answered already)

⛔⛔ **The rule this workstream has already paid for twice**: §12.16 built
a mechanism on an unchecked premise, and §12.19's first run divided by
the wrong denominator. The ~24 ms figure is a *per-draw microbenchmark
rate multiplied by a draw count*. Before any of it is optimized, measure
the real frame:

✅ **Already known** (`render frame:`): 30577 draws, 1.06 us submit and
1.26 us GPU per draw, 32 ms submit against a 76 ms frame.

Still open, and each of these redirects the phases below:

1. **Which passes the 30577 draws belong to.** 8388 scene rows survive
   the cull, so the draws are ~3.6x the rows: color, AO prepass, shadow
   caster, on-top, outline. ⭐ A pass that contributes thousands of draws
   for a small visual effect is a cheaper win than any of phase 1-3, and
   nothing currently attributes draws to passes.

   ⭐⭐ **And it needs no instrumentation.** bgfx already collects
   per-view **CPU submit time and GPU time** (`bgfx::Stats::viewStats`,
   `numViews`), gated behind `BGFX_DEBUG_PROFILER` — set it with
   `bgfx::setDebug()` while `RenderDebug_Timing` is on and print the
   views sorted by cost. We call neither `setDebug` nor `setViewName`
   today, so the only work is the flag, a name per view, and a readout.
   That is a couple of hours and it answers "which pass costs what" for
   *both* processors — do it before choosing any phase below.
2. **The split inside our own submit**: bgfx `submit()` itself vs the
   per-draw C++ before it (material unpack, texture routing, state
   assembly) vs the six `setUniform` calls vs `setTransform`.
3. The **fill / line / point** mix — §12.19 found ~2787 point and ~2800
   line draws among 8388 rows, and they do not cost the same.

⛔ **This can still kill phase 3**: if most of the CPU is (2)'s
per-draw C++ rather than bgfx, doing less per draw beats merging draws —
and if the frame is GPU-bound on per-draw state, only *fewer draws*
helps, which points the other way. Measure before choosing.

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

It is still worth doing — 32 ms of desktop CPU divided by 8 cores is the
single largest number on this page — but it must be described as what it
is: a desktop-tier speedup that leaves the browser exactly where it was,
and it does nothing about the ~38 ms **GPU** per-draw half on either
tier.

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
