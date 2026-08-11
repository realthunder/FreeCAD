# Draw submission: getting 17727 draws onto the GPU

Status: **plan**. Nothing here is built yet.

This is the workstream that follows occlusion culling, and it starts
where `docs/FarFieldProxies.md` §12.19 ends: both sides of box-based
occlusion are measured and both are exhausted. Ten times the occluders
bought 3.1% (§12.16); the tightest possible occludee bound — every
triangle and segment asked separately, at a cost no frame could pay —
buys 8.9% (§12.19). What is left is not culling.

## The measured problem

On the rack model (5455 objects) a culled frame still submits **17727
draws**. At the per-draw CPU cost measured for this renderer (~1.2-1.5 us
of submit, see the draw-call-cost note) that is **~24 ms of CPU**, spent
before the GPU has done anything. It is two orders of magnitude more than
what remains in the occlusion question, and it is the frame's ceiling.

## ⭐⭐ The constraint that shapes everything: one route, both tiers

**The same route must run on desktop and in the browser.** This is a
direction decision, not a technical one — running FreeCAD from one
codebase across browser, mobile and desktop is the project's first goal,
and a submission path that exists only on desktop forks the thing the
project is for.

What that rules out, and why each is genuinely unavailable rather than
merely inconvenient:

- ⛔ **Compute-shader culling / GPU-built draw lists.** No compute
  shaders in WebGL2. This is the standard modern answer and it is not
  available to us.
- ⛔ **Multi-draw indirect.** `glMultiDrawElementsIndirect` is GL 4.3 /
  GLES 3.1; WebGL2 is GLES 3.0. bgfx reports `BGFX_CAPS_DRAW_INDIRECT`
  false there.
- ⛔ **bgfx's WebGPU backend.** Dawn-native, unusable through Emscripten.
- ⚠️ `WEBGL_multi_draw` exists in browsers but bgfx does not expose it.

⇒ Everything below is built out of what **both** tiers have: instanced
arrays, uniform buffers, `texelFetch`, and ordinary indexed draws.

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

## Phase 0 — find out where the 24 ms actually goes (measure, don't build)

⛔⛔ **The rule this workstream has already paid for twice**: §12.16 built
a mechanism on an unchecked premise, and §12.19's first run divided by
the wrong denominator. The ~24 ms figure is a *per-draw microbenchmark
rate multiplied by a draw count*. Before any of it is optimized, measure
the real frame:

1. Wall time of the scene submit loop, on the rack model, at the audit's
   camera — as its own timer, not inferred.
2. Split it: (a) bgfx `submit()` itself, (b) our per-draw C++ before it
   (material unpack, texture routing, state assembly), (c) the six
   `setUniform` calls per draw, (d) `setTransform`/buffer binds.
3. The share of the 17727 draws that are **fill** vs **line** vs
   **point** — §12.19 found 2787 point and ~2800 line draws among 8388
   drawn rows, and they do not all cost the same.

**This decides which phase is worth doing**, and it can kill phase 3
outright: if most of the time is (b)/(c) rather than (a), then doing less
per draw beats spreading or merging the draws.

### Phase 0.5 — the instancing that already exists and does not engage

`buildInstanceGroups()` already batches *identical geometry with
identical material* into one instanced submit, and there is a standing
bug: **instancing never engages at render cache 3 when Render Type is
"Default"** (see the two-doc render note). Worth fixing and measuring
before building anything new — it may already be leaving a large batch
win on the table, and it changes the draw count phase 3 is aimed at.

## Phase 1 — Encoded: spread the same submits across cores

bgfx supports multi-threaded submission through `bgfx::Encoder`. The
renderer uses **none** today: every draw goes through the implicit
single-threaded encoder.

- N encoders, one per worker, partitioning the draw list by contiguous
  range; reuse `Render::physicalCoreCount()`/`occluderWorkers()` from
  §12.18 so the whole renderer sizes its pools one way.
- ⚠️ **Ordering is the whole risk.** bgfx sorts by view and sort key, so
  encoder assignment must not change the order within a view. Sorted
  transparency depends on per-draw depth keys — keep that pass on a
  single encoder until it is proven safe.
- ⚠️ Needs `BGFX_CONFIG_MULTITHREADED`; check `caps->limits.maxEncoders`.
  Set the define from `src/3rdParty/CMakeLists.txt` via
  `target_compile_definitions`, the way §12.11 raised
  `BGFX_CONFIG_MAX_OCCLUSION_QUERIES` — **no submodule edit**.
- **Web runs the identical code with one encoder** (the wasm build has no
  pthreads), so this is not a fork: it is the same route with a worker
  count of one. That is exactly the shape §12.18's core counting already
  has.

**Gate**: the frame must be **pixel-identical** to Classic (the cull
audit's own standard), and the submit timer must fall by roughly the
worker count on desktop. ⚠️ Read timings from a min/median/max spread,
never a last sample — this workstream published a 5% claim off single
samples once (§12.14, corrected in §12.15).

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
  a data texture read with `texelFetch` (core in GLES3/WebGL2, and bgfx's
  shader layer exposes it) or a UBO array. ⇒ This is the "materials must
  become bindless first" step §12.17 named as the prerequisite — done in
  the one form both tiers support.
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
