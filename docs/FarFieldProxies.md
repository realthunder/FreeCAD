# Far-Field Assembly Proxies

This document describes a design for drawing very large assemblies at a
cost that follows what the camera can distinguish rather than what the
document contains: a group of distant parts is drawn as one thing, and
the cut between "one thing" and "every part" moves with the view.

Related reading: `docs/SceneStreaming.md` (§6 the fidelity ladder, §7
level of detail — this is an extension of both), `docs/IncrementalPublish.md`
(the complementary problem), `docs/TShapeRenderCache.md` (tessellation
sharing and instancing), `docs/ComputeBoundaries.md` (where generation
jobs belong), `docs/RoadMap.md`.

Status: design. Nothing here is implemented, and §9 states what should
be measured before any of it is.

## 1. Problem

A 20000-part assembly is expensive in a way that is not about triangles.
Every part is a render-cache entry, a draw entry, a draw call, a
material, a bounding box and an identity, and it costs all of that
whether it fills the screen or four pixels of it. The measurement in
`docs/IncrementalPublish.md` §4 puts a number on one part of this:
publishing a frame costs ~22µs **per object**, flat, regardless of the
object's size.

The consequence is that a camera pulled back to see a whole machine —
the view in which the model is least detailed — is the view in which
the renderer works hardest. Hundreds of parts each contribute a few
pixels and a full object's overhead.

Two scaling problems hide here, and they need different fixes:

- **Cost per change** — republishing N objects because one changed.
  That is `docs/IncrementalPublish.md`.
- **Cost per frame** — touching N objects when the view can only
  resolve a fraction of them. That is this document.

They compose: incremental publish makes edits cheap, proxies make the
steady state cheap.

## 2. What this is not

`docs/SceneStreaming.md` §6 deliberately *removed* a proxy mechanism.
The earlier stand-in design gave a pending box its own parallel draw
list, its own lifetime and an invariant written specially to stop a
placeholder being mistaken for the mesh it stood for; the ladder
replaced all of it with "a box is simply a mesh" — its own content key,
its own identity, refined by the same code path as any update, and able
to run *backwards* under memory pressure.

Nothing here reintroduces that. A far-field proxy is not a placeholder
for one object waiting to become itself. It is **a rung on the same
ladder whose owner is a group node instead of a leaf object**, and it
inherits every property that made the ladder work:

- Identity is structural — the proxy has its own content key, so it
  never claims to be the geometry it stands for.
- It carries real materials, so a coarse view reads as the model.
- Selecting it is an ordinary rung selection; replacing it is an
  ordinary update.
- It runs backwards: memory pressure walks a subtree *up* to its proxy
  rather than choosing between holding geometry and showing a hole.

The ladder today runs from an object's bounding box, through decimated
levels, to its exact mesh. This adds rungs *above* the object, where
one rung serves many objects at once.

## 3. The mechanism

Build a spatial hierarchy over the assembly's parts, and generate for
each interior node a **proxy**: one merged, decimated representation of
everything beneath it, with one material set and no per-part state.

**The hierarchy is graded by extent, not taken from the document
tree.** An assembly's tree groups by function, not by location — a
"fasteners" group can span the whole machine — so it is useless as a
culling structure and worse as a proxy structure, since a node that is
spatially everywhere has no meaningful projected size. What is built
instead is an ordinary spatial hierarchy over per-object world bounds
(median or SAH split), subdivided until node extent falls below a
target, so that for any camera there exists a level whose nodes project
to roughly the size the cut wants.

**And it is graded *finely*, which is a requirement rather than a
tuning choice** — see §8: the size of a node is the size of a pop.

At runtime, choose a *cut* through the tree by projected screen-space
error, exactly as level selection already chooses a rung per object.
Near the camera the cut is deep — individual parts at their exact
geometry, which is what editing and picking need. Far away it is
shallow: one proxy where two hundred parts were.

The property being bought: **drawn primitives and draw entries become a
function of screen coverage, not of model size.**

## 4. What to borrow, and what not to

**From Nanite: the cut, not the machinery.** Nanite solves the inverse
of this problem — one asset of ten million triangles — with a cluster
DAG, a software rasterizer and a visibility buffer. A CAD assembly is
twenty thousand assets of five hundred triangles. Splitting *below* the
part buys little when parts are already small; the entire win is
aggregation *above* the part. So take the idea of a cut through a
hierarchy chosen by screen-space error, and the treatment of a node as
the unit of culling and streaming. Leave the DAG, the software raster
and the visibility buffer.

**And take its answer to popping, which is not a blending trick** — §8.

**From Far Voxels (Gobbetti & Marton, 2005): what the proxy is when a
surface is the wrong answer.** For a node whose contents mutually
occlude — three hundred fasteners behind a panel — a merged surface
mesh approximates badly, and a volumetric sample set with
view-dependent appearance approximates well. This is the second
representation in §5, and it is worth having only where §5's first
representation visibly fails.

**Not splats.** `docs/RoadMap.md` aside, the reasoning is recorded
because the question recurs: a gaussian cloud gives up exact
silhouettes, imposes a global depth sort that opaque CAD geometry does
not currently pay, and has no notion of a face boundary by
construction. A proxy built from geometry keeps all three.

## 5. The representation, cheapest first

1. **Merged decimated mesh per node.** Reuse `MeshSimplify.cpp` —
   already a grid-clustering decimator whose representatives come from
   a shared grid, which is what keeps adjacent decimated surfaces sewn
   together. Materials resolve per cluster from the source parts. Works
   with the z-buffer, the depth prepass, shadows and section capping
   unchanged, and needs no new shader. The expectation is that this is
   most of the win.
2. **Surfel or voxel proxy with view-dependent shading.** For nodes
   where (1) produces visible mush. More build cost and its own shader;
   justified only against a real model that shows the failure.
3. **Impostors and billboards.** Cheapest, and wrong under rotation.
   Not worth it in a renderer that can afford (1).

## 6. Identity, picking and the element map

A merged proxy spans several objects, which is exactly what
`MeshSimplify` currently refuses to do within an object: it never welds
across element boundaries, so a decimated mesh carries a faithful
element map and answers sub-element queries (`docs/SceneStreaming.md`
§7). Merging across objects cannot preserve *element* identity that
cheaply, but it does not have to preserve it at all.

**Picking follows the rung** — the precedent is already set. A proxy
answers at object granularity, and it can do that honestly by carrying
a part table keyed by source object, built for free during the merge
because the merge knows which object each cluster came from. So a click
on a distant subassembly still names the part it hit.

**The table must key by scene path, not by object pointer.** Selection
in this codebase is path-based — the `SoFCSelectionRoot` chain
compressed into a `NodeKey`, which the thin-client identity work has to
capture at `push()` time (`docs/ThinClient.md`). A proxy that resolves
a cluster to a `DocumentObject` alone would not line up with what the
selection system and the element map believe, particularly for a part
that appears through several links. Storing the same key the selection
system already uses is the difference between this integrating and this
producing a class of subtle identity bugs.

Finer hierarchy nodes (§3) help here rather than hurt: smaller merged
meshes mean tighter part tables and fewer candidates per ray hit.

Sub-element queries do not degrade, because **the proxy is a rendering
substitute only**. The exact geometry still exists in the document and
still answers; a ray test against the spatial hierarchy resolves to an
exact object and then to its exact shape. This is the structural
difference from any approximation that *replaces* the model: nothing
here ever has to reconstruct a face from an approximation, because the
face was never thrown away.

Anything a proxy cannot represent honestly is excluded from it and
drawn exactly — see §8.

## 7. Naming, caching and invalidation

`docs/SceneStreaming.md` §7 names a level by what would produce it:
its source geometry's content identity plus the level index, never by
what it will contain. A node proxy generalizes that directly — the
node's identity is derived from its children's identities and
placements, so:

- Changing one part changes its identity, which changes every ancestor
  node's identity, which names a *different* proxy. No chunk ever
  mutates, no invalidation message is needed, and a viewer that still
  holds the old proxy holds something that is still internally
  consistent.
- Two assemblies containing the same subassembly share its proxies, the
  same way `docs/TShapeRenderCache.md` shares tessellation per TShape.
- On the desktop, "generate proxy for node N at level L" is a job
  cached per (child identity set, level) — the same shape as the
  per-`(TShape, level)` tessellation cache, and the same argument for
  building it behind the seam rather than in the viewer.

Generation is work, not bytes, so it goes through the request path §7
already defines for unbuilt levels, and its natural home is the
out-of-process geometry queue of `docs/ComputeBoundaries.md`.

## 8. Difficulties, and the decisions they need

- **When to build.** Proxies must be generated off the GUI thread and
  must never stall a recompute. A rebuild triggered mid-edit would be
  worse than having no proxy: the natural policy is to build on idle
  after a document settles, and to let an ancestor whose proxy is stale
  simply fall back to drawing its children until the new one arrives.
  This falls out of the ladder — a missing rung is not an error.
- **Popping — decided by the hierarchy, not by a fade.** See §8.1; it
  is the constraint that shapes §3, so it is written out rather than
  listed.
- **Shadows and the prepass.** §6's rules for stand-in draws were
  decided on the same grounds and should be inherited rather than
  re-argued: occupancy behaviour (depth) yes, shading behaviour
  (shadows, section capping, hidden-line, outline) no — with the
  difference that a decimated proxy is a much better occupancy
  approximation than a bounding box, so its exclusions can be looser.
  Whether a proxy should cast shadows is the one genuinely new
  question, and it is a judgement to make against a real model.
- **Transparency and section views.** A proxy of transparent parts is
  wrong, and a section plane cutting a proxy caps an approximation.
  Exclude transparent parts and section-intersecting nodes from
  proxying and draw them exactly; the cut is per node, so this is a
  local exclusion, not a global one.
- **Memory.** Proxies are additional data, and they belong inside the
  existing byte budget (`SceneLadder.h`) rather than beside it. They
  should pay for themselves: a resident proxy is only worth its bytes
  while it is replacing residents that are larger.

### 8.1 Popping is a granularity problem

Nanite is the useful reference here, and what it teaches is that the
answer is not a blending trick. Its transitions are invisible because
they are *small*, *local* and *unsynchronised*: the switching unit is a
group of ~128-triangle clusters rather than a whole mesh, group
boundaries are locked during simplification so mixed-level cuts stay
crack-free, error is forced monotonic up the DAG so each group decides
independently and the cut is still globally consistent, and the error
threshold is about one pixel per edge — so the geometry that changes at
a switch moves by roughly a pixel, somewhere in the middle of a
surface, at a moment when no neighbour is switching. Temporal AA
absorbs the residue. Unreal's *classic* static-mesh LOD, which switches
a whole mesh at once, is the one that needs dithered transitions.

A whole-subtree proxy switch is structurally the classic case, not the
Nanite case, and three things follow.

**Hysteresis does not solve this.** It prevents oscillation at a
threshold; it does nothing about the visible snap of a single crossing.
It is still wanted, for the problem it does solve.

**We cannot buy our way out with a tighter threshold.** A proxy
standing in for two hundred parts differs from them by far more than a
pixel at any memory budget worth having. Nanite's sub-pixel property
comes from the *unit* being small, not from the threshold being tight.

**So the hierarchy has to be graded finely** (§3): many small nodes
rather than a few large ones, so switches are local, staggered across
siblings, and each one changes a small part of the image. This is the
reason §3 specifies a target extent instead of following the document
tree, and it is a correctness-of-appearance requirement rather than a
tuning knob.

What remains after that is a cross-fade for the residual, and it is
worth knowing the cost before assuming it: this renderer has no
full-scene temporal AA. Temporal accumulation exists per effect
(volumetric raymarch, AO) without reprojection, so there is no free
resolve to dissolve a screen-door dither. A short alpha fade drawing
both representations for a few frames is the more likely fit, and it is
bounded — only nodes actually crossing their threshold pay it. A third
option costs nothing when it is available: switch a node while it is
off-screen or occluded.

### 8.2 Selection must never change a proxy's contents

This is a hard rule, and the reasoning is worth keeping because the
tempting design fails catastrophically rather than mildly.

A proxy is named by the identities of its children (§7). If
highlighting a part excluded it from its proxy — to avoid drawing it
twice, or to show it at full fidelity — the node's contents would
change, minting a new key and cascading a rebuild up the ancestor
chain. Preselection changes on **every mouse move**. That design
re-merges and re-decimates subtrees at hover rate.

**Proxies are therefore immutable with respect to selection state**,
which is also how the renderer already works: `buildHighlightCache`
produces a *separate* cache drawn on top and never edits the base
scene. Highlight is additive. Two regimes cover the cases:

**Few objects — hover, click, up to `MaxOnTopSelections`.** Build the
exact highlight geometry on demand for those objects only and draw it
on top; the proxy underneath keeps drawing its approximate version,
unhighlighted. The cost is one object's tessellation (a fetch, on a
thin client, so the highlight can land a frame late — "picking follows
the rung" again). The artifact is a silhouette mismatch: the exact
highlight does not perfectly cover the proxy's version of that part, so
a sliver of unhighlighted surface can show. At proxy distances that is
a pixel or two, and the existing selection outline both hides it and
reads as deliberate.

**Many objects — select-all, a filter selecting thousands.** Build no
exact geometry at all. **Tint in place**: the part table of §6 already
tags every cluster with its source object, so the shader recolors the
clusters belonging to selected objects. No rebuild, no exclusion, no
exact geometry, and it scales to the whole assembly.

That second regime is the stronger argument for the part table. It is
not only a picking mechanism — it is what lets selection state be
expressed *inside* a proxy, which is what protects content-addressed
identity from a hover-rate rebuild.

One consequence to accept rather than fix: with whole-object-on-top
selection, a highlighted part inside a proxy is drawn twice, once
approximately in place and once exactly on top. That is correct, but it
does mean on-top mode costs more at proxy distance than it does today.

## 9. What would justify starting

Two numbers, neither of which exists yet, both obtainable from
`RenderDebug_Timing` plus a frame breakdown on a real 10-20k-part
assembly:

1. **What fraction of a steady-state frame is per-object overhead**
   rather than rasterization. The per-object publish cost is measured
   (~22µs/object); the per-object *draw* cost is not.
2. **How many parts are sub-pixel, or nearly so, at a typical viewing
   distance** of a whole assembly. If most parts at a normal camera are
   already tiny, the cut pays immediately. If large assemblies are
   usually inspected part by part up close, this pays far less than
   incremental publish does, and should wait.

Measuring these honestly matters more than starting: this is a larger
workstream than incremental publish, and it is the one that would be
easiest to justify by intuition and hardest to justify by data.

### 9.2 measured (MiSTer Express, 17800 objects, 2026-08-03)

`RenderDebug_Coverage` (§2.4b of `docs/RenderDebug.md`) on a 1920x1200
viewport, real GPU, camera isometric and fitted to the whole assembly,
then walked in by fixed factors:

| camera | on screen | <=1px | <=4px | share <=4px | off screen |
|---|---|---|---|---|---|
| whole assembly | 19362 | 4261 | 12935 | **66.8%** | 0 |
| 2x | 19333 | 1570 | 9764 | 50.5% | 29 |
| 4x | 18705 | 589 | 4261 | 22.8% | 657 |
| 8x | 16087 | 352 | 1474 | 9.2% | 3275 |
| 16x | 1532 | 4 | 4 | 0.3% | 17830 |

**The second measurement passes, and not marginally.** At the camera
this document is about, two thirds of what the renderer draws is four
pixels or less, and 22% of it is literally sub-pixel — 4261 objects
each paying for a cache entry, a draw entry, a material and an identity
to produce less than one pixel. Only 301 objects (1.6%) exceed 64px.

The walk in is the same finding from the other side: by 16x, 92% of the
assembly is off screen, which is the population a cut stops touching
rather than draws smaller.

Counted per drawn *instance* (19362) rather than per document object
(17800) — instances are what pay the per-object cost. The 1562 gap is
not yet accounted for and is worth a look before the number is leaned
on hard.

**Measurement 1 is still missing**, and it is the one that decides the
size of the prize: this says how many objects are too small to resolve,
not what fraction of a frame they cost. A small model is no substitute
either — FGC-9_MkII (150 parts) has *nothing* under 4px at its own fitted
camera, so this effect only exists at assembly scale.

### 9.1 Rough effort

Estimates, for a desktop-first version, to be treated as the shape of
the work rather than a schedule:

| piece | estimate |
|---|---|
| spatial hierarchy, incrementally updatable, stable node identity | 3-5 days |
| proxy generation (merge, decimate, part table, material resolve) plus its job plumbing and cache | 1-1.5 weeks |
| cut selection by projected error, with the `SceneLadder` budget | 3-5 days |
| picking integration (part table by scene path, ray fallback, on-demand exact geometry) | 3-5 days |
| highlight regimes of §8.2 including in-proxy tint | 2-4 days |
| **total, desktop only** | **4-7 weeks** |

Streaming the same rungs to remote viewers is on top of that, and is
mostly format and scheduling rather than new design (§10 step 5).

**The fine grading of §3 is a small part of this — 1.5-2 weeks
marginal — and almost none of it is the splitting rule.** The splitting
predicate is a few lines; a spatial hierarchy is needed either way. The
cost is what the node count does downstream. Leaves of ~16 parts rather
than ~500 across a 20000-part assembly means on the order of 2500
proxies rather than 80, so:

- **Generation throughput becomes a scheduler problem**, not a loop:
  batching, prioritisation by likely visibility, idle-time execution
  that yields to recompute.
- **Chunk count becomes a streaming problem, and there is a measured
  precedent for it going wrong**: the mobile blank-page pause was the
  first commit waiting on ~200 group manifests over a cold link
  (`docs/SceneStreaming.md`, and the thin-client work that fixed it).
  Thousands of small proxy chunks would reproduce it. Bundling the
  coarse levels into single chunks is the mitigation, and it is format
  work that should be designed in rather than retrofitted.
- **Total bytes barely move.** If each level halves the triangle count,
  the sum over levels is a geometric series of roughly 1.5-2x the base
  data whatever the granularity. Fine grading multiplies the number of
  objects, not the volume — which is why the cost lands on schedulers
  and chunk counts rather than on memory.

## 10. Sequencing

Incremental publish comes first, and not only because it is smaller.
Its per-child slice model is precisely the structure a proxy needs:
drawing a node as a proxy is "replace these children's slices with one
slice", which is a delta the aggregate already knows how to express.
Building proxies against today's whole-scene republish would mean
building the same bookkeeping twice.

The phase order, then:

1. `docs/IncrementalPublish.md` phases 2-6.
2. The two measurements in §9.
3. Node hierarchy graded by extent (§3) and merged-mesh proxies (§5.1)
   on the desktop, as rungs above the object, with the part table of
   §6 keyed by scene path.
4. Cut selection by screen-space error with hysteresis, reusing the
   selection machinery of `docs/SceneStreaming.md` §7, and the
   transition treatment of §8.1.
5. Streaming the same rungs to remote viewers. The naming of §7 means
   no new format design, but the chunk count of §9.1 does need
   bundling of coarse levels, which is format work.
6. View-dependent volumetric proxies (§5.2), only where §5.1 is shown
   to fail.

## 11. Implementation plan

The sequencing of §10 stands; this states what each step touches in the
code, and how each one is shown to work. Phase 0 is the gate of §9 and
is the only part that can conclude "do not build this".

### 11.0 The two measurements (small, and first)

**0a — the sub-pixel histogram (§9.2).** Nearly free: `PlanBoxes::sight()`
in `SceneLadder.cpp` already computes a projected `diagPx` for every draw
on the refine pass. The same arithmetic, bucketed over all draws and
logged once a second, answers "how many parts can this camera not
resolve". Delivered as `RenderDebug_Coverage`, a `RenderDebug_*` view
property per §2 of `docs/RenderDebug.md` — the policy there forbids a
throwaway macro for exactly this kind of question.

**0b — per-object overhead against rasterization (§9.1).**
`Gui/RenderTiming.h` already splits a frame into six exclusive stages,
but its last stage mixes draw submission with fill. Separating them does
not need finer instrumentation, it needs an ablation: sweep object count
at a fixed camera (slope = cost per object), then sweep resolution at a
fixed object count (slope = cost per pixel). `Render_EffectResolution`
already scales fill-bound passes and gives the second sweep for free.

**The gate.** Proceed only if per-object cost dominates a steady-state
frame *and* a whole-assembly camera leaves most parts at a few pixels.
If large assemblies are usually inspected up close, `IncrementalPublish`
is worth more and this should wait.

### 11.1 Where each phase lands

| phase | new/changed | note |
|---|---|---|
| 1 hierarchy | **new** `Gui/Renderer/ProxyHierarchy.{h,cpp}` | plain floats, no bgfx/Coin/OCCT — the discipline `SceneLadder.h` already keeps, so both tiers can share the policy |
| 2 generation | `Gui/Renderer/MeshSimplify.*`, refine pool | merge N transformed meshes, then decimate |
| 3 the cut | `Gui/Renderer/SceneLadder.cpp` | beside `planMeshRefines`, sharing `PlanBoxes` |
| 4 drawing | `Gui/Inventor/SoFCRendererBridge.cpp`, `BGFXRenderer.cpp` | needs the per-child slices of `IncrementalPublish` phase 4 |
| 5 picking/highlight | `ProxyHierarchy`, selection path, shaders | the tint regime is the shader work |
| 6 transitions | phase 3's selection | fade only for nodes actually crossing |
| 7 streaming | wire format | bundling, per §9.1 |

### 11.2 What the code already gives us

`simplifyMesh()` is a better starting point than §5.1 claims. It is a
grid-clustering decimator that keeps adjacent surfaces sewn (coincident
boundary vertices fall in one cell), and it already carries **element
tables as `(start, count)` runs preserved index for index**. The part
table of §6 is that same mechanism with one run per *source object*
instead of per element — so the part table is close to free, and the
merge only has to record which object each input run came from.

The identity story likewise reduces to existing practice: a node's key
is a hash over its children's content keys and placements (§7), which is
the `(TShape, level)` cache of `docs/TShapeRenderCache.md` with a
different key.

### 11.3 The invariant to write down first

Two ladders now overlap. Every object today picks its own rung through
`planMeshRefines`; with proxies, an ancestor drawing a proxy must
*supersede* its children. The two must not both draw:

> **No object may be drawn while any ancestor node is drawing a proxy.**

The cut decides *who* draws; the per-object ladder decides *at what
fidelity*, below the cut only. This is cheap to assert in phase 3 and
expensive to discover in phase 4, so it is asserted from the start —
in the same spirit as §8.2's rule about selection.

### 11.4 Risks this plan carries

- **Phase 5's tint needs renderer plumbing that does not exist.**
  Highlighting today builds a *separate* cache drawn on top
  (`buildHighlightCache`) and never touches the base scene. Tinting
  inside a proxy means a per-cluster attribute plus a selection buffer
  the shader reads. That is new, and the 2-4 day estimate of §9.1 should
  be re-checked against `docs/ShaderDesign.md` before it is trusted.
- **Merging across materials.** §5.1 assumes materials resolve per
  cluster; a node whose parts carry many distinct materials either
  fragments the proxy into many draws — losing the win — or averages
  them and looks wrong. The material count per node is a property worth
  measuring in phase 1, before phase 2 is designed around it.
- **Generation throughput** is a scheduler problem at the node counts
  fine grading implies (§9.1), not a loop.
