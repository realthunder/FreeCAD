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

Build a spatial hierarchy over the assembly's parts — the scene already
has the tree and the per-object world bounds that streaming selection
uses. For each interior node, generate a **proxy**: one merged,
decimated representation of everything beneath it, with one material
set and no per-part state.

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
- **Popping.** Switching a subtree between proxy and parts is visible.
  The streaming scheduler already carries hysteresis for exactly this
  class of decision; a dither or fade across a few frames is the
  fallback if hysteresis alone reads badly.
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
3. Node hierarchy and merged-mesh proxies (§5.1) on the desktop, as
   rungs above the object, with the part table of §6.
4. Cut selection by screen-space error with hysteresis, reusing the
   selection machinery of `docs/SceneStreaming.md` §7.
5. Streaming the same rungs to remote viewers — which needs no format
   work if the naming of §7 holds, since a proxy is a chunk like any
   other.
6. View-dependent volumetric proxies (§5.2), only where §5.1 is shown
   to fail.
