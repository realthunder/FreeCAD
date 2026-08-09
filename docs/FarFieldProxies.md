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

Status: design, with phase 0 measured and closed. Both gates of §9 pass
— 66.8% of a 17800-object assembly is under four pixels at its own
fitted camera, and 87% of a 139 ms frame is per-object cost rather than
rasterization (§9.2, §9.1). Phase 1 (§11.1) is the first piece that
builds anything.

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
each interior node a **proxy**: a merged, decimated representation of
everything beneath it, carrying no per-part state — one such mesh per
material the node actually contains (§5.1).

**The hierarchy is graded by extent, not taken from the document
tree.** An assembly's tree groups by function, not by location — a
"fasteners" group can span the whole machine — so it is useless as a
culling structure and worse as a proxy structure, since a node that is
spatially everywhere has no meaningful projected size.

The failure is worse than "suboptimal", and the case that shows it is
the common one rather than a pathology. A great many real assemblies
are *flat*: one holder with twenty thousand children directly beneath
it, because that is what an importer, a script or an impatient modeller
produces. Coupled to the document tree, such an assembly has exactly
**one** interior node, so the cut is binary — draw every part, or draw
the whole machine as a single blob — and there is no ladder at all. A
*well*-formed tree fails differently, by grouping things that are
nowhere near each other. Both failures have one root: the document tree
is an **ownership** structure (who contains what, which transform and
material apply, what a selection path names), and a cut needs a
**location** structure. Conflating them makes both worse.

So the rule is: never *depend* on document structure, and exploit it
only where it happens to be spatially compact and repeated (§7.1).

**And the hierarchy is graded *finely*, which is a requirement rather
than a tuning choice** — see §8.1: the size of a node is the size of a
pop.

The property being bought: **drawn primitives and draw entries become a
function of screen coverage, not of model size.**

### 3.1 What it is built from: an instance table, not a tree

The input is a flat table with one row per *drawn instance*:

```
objectKey       identity of the scene-graph node path (already per-occurrence)
bboxMin/Max     world bounds
model[16]       world transform, baked at merge time
materialBucket  index into the snapshot's material table
sourceTag       content identity of the mesh (the TShape-level key)
```

Every one of those is a field of `Render::DrawCall` (`Renderer.h`)
today. Nothing has to be plumbed, and nothing above `DrawCallList` is
consulted — no Coin node, no `ViewProvider`, no `DocumentObject`. This
is not a new discipline but the one the ladder already keeps:
`coverageHistogram()`, `planMeshRefines()` and `planMeshDemotes()` all
take a `DrawCallList` and nothing else (`SceneLadder.h`).

Two things follow from the unit being an *instance* rather than an
object. Phase 0 measured 19362 drawn instances against 17800 document
objects and could not account for the difference; here the question
does not arise, because instances are what pay the per-object cost and
instances are what the hierarchy partitions. And the same table is what
a thin client already receives over the wire, so a remote viewer builds
the identical hierarchy without knowing the document tree at all
(`docs/ThinClient.md`).

The generalisation worth stating: what is being built is not "the LOD
hierarchy" but **the renderer's spatial index**, with several consumers
— frustum and occlusion culling, the proxy cut, streaming priority,
pick acceleration. Level of detail is one of them.

### 3.2 The partition

A loose octree over world bounds, with two properties that matter more
than split quality:

- **Assignment by size.** An instance goes to the finest level whose
  cell contains its whole bounding box. This is what makes a flat
  assembly's outlier harmless: a frame member spanning the machine
  lands at a coarse node and is drawn exactly, because its screen size
  is at least its node's and the cut therefore never asks to proxy
  something visually significant. A node's proxy covers its own
  residents *and* everything below it.
- **Positional node identity: `(level, Morton cell)`.** Not a median or
  SAH split. A data-dependent split means one moved part reshuffles the
  partition and invalidates proxies for geometry that did not change;
  a quantised grid means a moved part touches exactly two cells, ids
  are identical on every machine and across sessions (so §7's cache is
  shareable rather than per-session), and "stable node identity" stops
  being a design problem.

Subdivide until **extent falls below a target, or the cell holds `K`
instances or fewer**. The instance cap is not a tuning knob: it is
simultaneously the bound on how much geometry a single switch changes
(§8.1) and the bound on how much is drawn exactly when the cut is
forced to descend (§8). One parameter, two correctness properties.

**The grids nest.** Level `L`'s decimation grid is an exact
subdivision of level `L−1`'s — shared origin, power-of-two — and the
grid is global per level rather than fitted to each node's own bounds,
so neighbouring cells at the same level snap identically. §7 draws the
generation consequence out of this.

### 3.3 The cut is per cell

At runtime, choose a *cut* through the tree by projected screen-space
error, exactly as level selection already chooses a rung per object.
Near the camera the cut is deep — individual parts at their exact
geometry, which is what editing and picking need. Far away it is
shallow: one proxy where two hundred parts were.

The cut is a **frontier, decided per cell**, not a single level chosen
globally: different branches stop at different depths, and neighbouring
cells routinely sit one or more levels apart. That costs nothing in
cracks, because a crack only matters within one connected surface and a
connected surface never spans two parts — the discontinuity between
parts was there before any merging (§6).

Two consequences:

- **The material split lives below the cut.** A cell's decision applies
  to all of its per-material proxies at once (§5.1). A cut that could
  proxy one material of a cell and not another would not be a
  partition.
- **Cut selection and proxy acquisition run on different clocks.** The
  descent is `O(frontier)` — a few hundred node tests, microseconds —
  so it runs per frame. *Acquiring* a proxy stays on `planLevels`'
  event schedule (camera settled, publish staged, budget moved). The
  existing ladder conflates the two only because per-object planning
  was never cheap enough to run per frame; a cut is. Hysteresis reuses
  the `kPlanDemoteMargin` semantics already in `SceneLadder.h` — split
  above the tolerance, merge back at half of it — rather than
  introducing a second constant.

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

1. **Merged decimated mesh per (cell, material).** Reuse
   `MeshSimplify.cpp` — already a grid-clustering decimator whose
   representatives come from a shared grid, which is what keeps
   adjacent decimated surfaces sewn together. Works with the z-buffer,
   the depth prepass, shadows and section capping unchanged, and needs
   no new shader. The expectation is that this is most of the win. See
   §5.1 for why the unit is a pair rather than a node.
2. **Surfel or voxel proxy with view-dependent shading.** For nodes
   where (1) produces visible mush. More build cost and its own shader;
   justified only against a real model that shows the failure.
3. **Impostors and billboards.** Cheapest, and wrong under rotation.
   Not worth it in a renderer that can afford (1).

### 5.1 The unit is (cell, material bucket), and averaging is not the answer

A node's parts do not share a material, and a spatial partition makes
that *worse* than the document tree would have: a functional group
tends to share an appearance, a spatial cell mixes whatever happens to
be there. The two obvious answers are both bad — fragmenting a proxy
into one draw per part gives the win back, and averaging the materials
produces a node that is the right shape in the wrong colour.

The answer is to make the material part of the partition. A proxy is
generated per **(cell, material bucket)**, so:

- every proxy carries exactly one material and is *exact* in
  appearance — nothing is averaged, and no new shader is needed;
- the win is 500 draws → the number of distinct buckets present in
  that cell, not → 1. On MiSTer Express, ~37 buckets exist across the
  whole document, so a cell of a few hundred parts should touch a
  handful;
- the cut stays a partition, because the material fan-out happens
  *below* the cut (§3.3) and never splits a cell's decision.

This reframes what phase 1 has to measure. The useful number is not
"materials per node" but **distinct material buckets per cell, per
level** — because summed over a cut that is the post-aggregation draw
count, which is the win itself, measurable before a single proxy has
been generated (§11.1).

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

### 7.1 Build bottom-up, from children's proxies

Because the grids nest (§3.2), a node's proxy is produced by merging
**its children's proxies** together with its own residents, rather than
by going back to source geometry. Three things fall out, and they are
the reason the nesting rule is stated as a rule:

- **Total generation is `O(leaf triangles)`, not `O(levels × leaves)`.**
  Each level costs a fraction of the one below it, the same geometric
  series §9.1 uses for bytes.
- **Error becomes monotonic up the tree for free.** §8.1 identifies
  that property as what lets each node decide independently while the
  cut stays globally consistent; built bottom-up it is a consequence of
  the construction rather than something to enforce afterwards.
- **A change costs a path, not a tree.** One edited part rebuilds its
  leaf cell from source; every ancestor re-merges from children that
  are already built. This is what makes §8's "a stale ancestor draws
  its children until the new one arrives" affordable at edit rate.

**The cost of partitioning in world space, stated plainly.** A
subassembly appearing twelve times would, under a document-tree
hierarchy, be merged once in local space and instanced twelve times.
Partitioned spatially, each occurrence falls in different cells and is
merged separately: generation cost and proxy bytes multiply by the
occurrence count. Two mitigations, neither of them phase 1-3 work:

- A node whose member set is exactly a repeated content group may carry
  a local-space representation plus a transform list. This is an
  extension point, deliberately not built until a model shows it
  paying.
- Merging is not free for heavily-instanced parts either: 500
  instances of one screw share a single vertex buffer today
  (`docs/TShapeRenderCache.md`), and a world-baked proxy holds 500
  copies. Decimation should erase this — at four pixels a screw *is* a
  box — but phase 2 needs an explicit gate: proxy only where the
  decimated triangle cost beats what instancing already achieves.

## 8. Difficulties, and the decisions they need

**First, a distinction that several of these turn on.** There are two
ways to keep something out of a proxy, and they differ by orders of
magnitude:

- **Cut-level** — force the frontier to descend past a cell so its
  members draw exactly. Costs a descent, is reversible on the next
  frame, and may depend on anything, including state that changes at
  interaction rate.
- **Content-level** — remove a member from a merge. This changes the
  node's member set, which mints a different key (§7) and cascades a
  rebuild up the ancestor chain.

From which: **a content-level exclusion may only depend on properties
that are stable at interaction rate.** Anything that changes while the
mouse moves — selection, preselection, edit state, a dragged section
plane — must be handled at the cut, or not at all. §8.2 is the sharp
case, and this rule is what generalises it.

- **When to build.** Proxies must be generated off the GUI thread and
  must never stall a recompute. A rebuild triggered mid-edit would be
  worse than having no proxy: the natural policy is to build on idle
  after a document settles, and to let an ancestor whose proxy is stale
  simply fall back to drawing its children until the new one arrives.
  This falls out of the ladder — a missing rung is not an error.
- **Editing needs no special case, and should not get one.** The
  tempting rule — exclude the object under edit — is unnecessary twice
  over. You edit at close range, and at close range the cut is already
  deep: a cell large enough to contain something you are looking at has
  enormous projected error, so its members are drawn exactly by the
  ordinary criterion. The remaining case is a far edit — from the tree,
  a spreadsheet, an expression, an undo, a script — and that is the
  bullet above: the changed content key makes the ancestors stale and a
  stale ancestor draws its children. That fires once, when the key
  changes, not per mouse move, so a drag descends the path at drag
  start and stays there with no interactive-rate work. What the cell
  then draws exactly is bounded by `K` (§3.2). The mechanism to force a
  descent exists anyway for the bullet below, so nothing is lost by
  declining to use it here.
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
- **Transparency and section views**, and which kind of exclusion each
  one is. A proxy of transparent parts is wrong, and a section plane
  cutting a proxy caps an approximation. Transparency is a content
  property and stable, so transparent parts are excluded at the
  **content** level — they are never merged in, and they draw exactly.
  A section plane is *dragged*, so section-intersecting nodes are
  excluded at the **cut** level: the frontier descends past them while
  the plane cuts them and returns when it does not. Getting these the
  wrong way round would re-merge a subtree at the rate the plane moves.
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
scene. Highlight is additive.

**The regime is chosen by which side of the cut the object is on** —
not by how many objects are selected:

- **Drawn exactly** (near side). Today's behaviour, unchanged: suppress
  the object's scene draw and draw the exact highlight on top.
- **Inside a proxy** (far side). **Tint in place.** The part table of
  §6 already tags every cluster with its source object, so the shader
  recolours the clusters belonging to selected objects. Nothing is
  suppressed, nothing is drawn twice, no exact tessellation is fetched,
  and it scales to the whole assembly.

A count-based split — few objects exact, thousands tinted — was the
earlier answer here, and the cut-side rule subsumes it: select-all is
mostly far, a hover is mostly near. It is also strictly better, because
it removes the two costs the count-based version had to accept. There
is no double draw, so on-top mode does not get more expensive at proxy
distance; and there is no silhouette mismatch, which was the exact
highlight failing to cover the proxy's coarser version of the same
part and leaking a sliver of untinted surface.

**One mechanism detail to make explicit rather than leave incidental.**
The suppression of a scene draw by its on-top highlight keys on
`DrawCall::objectKey` equality (`Renderer.h`). A proxy carries no draw
with a member's key, so the suppression *silently does nothing* — the
correct outcome falls out for free today. That is accidental
correctness, and it breaks the first time proxies gain member-keyed
sub-draws. The rule should be written into the suppression itself: a
proxy draw is never suppressed by a member's highlight.

This is the stronger argument for the part table. It is not only a
picking mechanism — it is what lets selection state be expressed
*inside* a proxy, which is what protects content-addressed identity
from a hover-rate rebuild.

### 8.3 The invariant these collapse to

§8's editing bullet, §8.2's selection rule and the section-plane case
are three statements of one thing:

> **A proxy is immutable with respect to all view state.** Selection,
> preselection and edit state are expressed *on top of* or *inside* a
> proxy — via the part table — never by changing what it contains.

Everything that varies at interaction rate acts on the cut or on the
shader; only content changes act on membership. Where a rule is needed
for a new kind of view state, this is the one to apply.

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

A small model is no substitute either — FGC-9_MkII (150 parts) has
*nothing* under 4px at its own fitted camera, so this effect only exists
at assembly scale.

### 9.1 measured (same model and camera)

Ablation rather than instrumentation, for a reason worth recording:
**`RenderTiming` cannot answer this question.** Its six stages instrument
the *publish* pipeline, which only runs when the scene changes; across a
static-camera redraw all six sum to 0.00-0.06 ms of a 139 ms frame and
the whole cost sits in `other`. Frame rate against visible object count
is the instrument that works.

Visible objects varied by stride at a fixed camera and viewport:

| visible objects | ms/frame |
|---|---|
| 17737 | 139.4 |
| 8869 | 81.9 |
| 4435 | 48.6 |
| 2218 | 33.1 |

Least squares over those four points: **6.85 us per object**, fixed cost
**18.8 ms**. At the full scene that is **121.5 ms of a 139.4 ms frame —
87% per-object**. A second independent run gave 6.89 us and 18.9 ms,
87% — the numbers reproduce.

**The control: fill, varied 64-fold at a fixed object count.** Hiding an
object also removes its pixels, so the slope above would conflate
per-object cost with fill if fill cost anything. Zooming out at a fixed
object count varies coverage while the viewport and the object count
stay exactly as they are:

| zoom out | fill | ms/frame |
|---|---|---|
| 1x | 1/1 | 140.0 |
| 2x | 1/4 | 139.9 |
| 4x | 1/16 | 140.0 |
| 8x | 1/64 | 139.8 |

**Sixty-four times less fill changes the frame by 0.2%.** Rasterization
is not a measurable part of this frame, so the slope is per-object cost
and the confound is measured at zero rather than argued away. It also
says what the 18.9 ms intercept is *not*: not fill, but the fixed
per-frame cost (clear, swap, effect passes, and the harness's own loop).

⚠️ Vary fill by *zooming*, not by resizing the window: under xvfb no
window manager honours a resize, and that version of the sweep came back
non-monotonic (144.7 / 56.8 / 149.8 / 56.2 ms) — an invalid measurement
that looks like a noisy one.

**Both gates of §9 are therefore met**: two thirds of the drawn objects
are beyond the camera's resolution, and 87% of the frame is the
per-object cost of drawing them. What remains before phase 1 is §10's
ordering, which is unchanged — `docs/IncrementalPublish.md` phases 2-6
first, because its per-child slices are the delta a proxy needs.

### 9.3 an unprompted finding: visibility costs 43 ms per object

Hiding the strides above took 384.1s for 8868 objects, then 192.3s for
4434, then 96.4s for 2217 — **43.3, 43.4, 43.5 ms per toggle**, and the
second run reproduced it (43.7, 45.0, 43.3). It does not fall as the
scene shrinks, so it is not simply the whole-scene republish of
`docs/IncrementalPublish.md` §2. Hiding a 500-part subassembly therefore
takes 21 seconds.

The cost is **entirely on the Gui side**: the same toggle on a headless
document costs **0.002 ms** and does not grow with object count (1000 to
8000 objects, `App::Part` container). So the App layer — the property,
`GroupExtension::slotChildChanged`, the `_GroupTouched` propagation — is
not involved, and neither is anything a headless import would exercise.
Unrelated to proxies, and worth its own look.

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
| 1 hierarchy | **new** `Gui/Renderer/ProxyHierarchy.{h,cpp}` | plain floats, no bgfx/Coin/OCCT — the discipline `SceneLadder.h` already keeps, so both tiers can share the policy. Input is the instance table of §3.1, projected from `DrawCallList` |
| 2 generation | `Gui/Renderer/MeshSimplify.*`, refine pool | merge N transformed meshes per (cell, material), then decimate; bottom-up per §7.1 |
| 3 the cut | `Gui/Renderer/SceneLadder.cpp` | beside `planMeshRefines`, sharing `PlanBoxes` |
| 4 drawing | `Gui/Inventor/SoFCRendererBridge.cpp`, `BGFXRenderer.cpp` | needs the per-child slices of `IncrementalPublish` phase 4 |
| 5 picking/highlight | `ProxyHierarchy`, selection path, shaders | in-proxy tint is now a phase-4 requirement, not a phase-5 nicety — see §11.4 |
| 6 transitions | phase 3's selection | fade only for nodes actually crossing |
| 7 streaming | wire format | bundling, per §9.1 |

**Phase 1 predicts the win without generating a single proxy**, and
that is the point of doing it first. Once the hierarchy and the descent
exist, count `(cells on the cut × distinct material buckets present in
each)` and compare it against the draws issued today — at the
whole-assembly camera, at 4× and at 16×. That sum *is* the
post-aggregation draw count (§5.1), so if it is not most of two orders
of magnitude below 19362, phase 2 is not worth starting. It reuses the
projection arithmetic `coverageHistogram()` already runs, and lands as
a `RenderDebug_*` view property per `docs/RenderDebug.md` §2 rather
than as throwaway instrumentation.

The same pass yields the two distributions that pick the parameters
`K` and the extent target: instances per cell, and material buckets per
cell, both per level.

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
`planMeshRefines`; with proxies, a node drawing a proxy must
*supersede* its members. The two must not both draw:

> **Every instance belongs to exactly one cell per level, and for each
> instance exactly one of {the instance, its covering proxy} draws.**

Stated over the document tree this would have been an ancestry test —
"no object may be drawn while any ancestor node is drawing a proxy" —
which is both more expensive and easy to express wrongly. Over a
partition (§3.2) it is a counting argument: walk each instance's cell
path, assert exactly one member of it is on the drawing side of the
frontier. `O(instances × depth)`, which is a debug-only pass and
therefore a `RenderDebug_*` property, off by default.

The cut decides *who* draws; the per-object ladder decides *at what
fidelity*, below the cut only. This is cheap to assert in phase 3 and
expensive to discover in phase 4, so it is asserted from the start —
in the same spirit as §8.2's rule about selection.

### 11.4 Risks this plan carries

- **In-proxy tint needs renderer plumbing that does not exist, and
  §8.2 promoted it.** Highlighting today builds a *separate* cache
  drawn on top (`buildHighlightCache`) and never touches the base
  scene. Tinting inside a proxy means a per-cluster attribute plus a
  selection buffer the shader reads. Under the cut-side rule this is
  how a proxied object is highlighted *at all*, so it is required by
  phase 4 rather than by phase 5, and the 2-4 day estimate of §9.1
  should be re-checked against `docs/ShaderDesign.md` before it is
  trusted. The interim, if phase 4 lands first, is the double draw the
  count-based regime used to accept: it works and merely costs more.
- **Merging across materials — the shape of this risk changed.** §5.1
  now makes the material part of the partition, so nothing is averaged
  and the failure mode is no longer "wrong colour" but "too many
  draws". That turns it from a design risk into a *measurement*: the
  buckets-per-cell distribution of §11.1, taken in phase 1, is what
  says whether the aggregation win survives the fan-out.
- **`K` is load-bearing in two directions.** §3.2 makes one parameter
  bound both the size of a pop (§8.1) and the amount drawn exactly when
  the cut is forced down (§8). Those two want it small and the draw
  count wants it large; if the phase-1 distributions show no value
  satisfying both, the pop treatment of §8.1 needs revisiting before
  phase 2, not after.
- **Generation throughput** is a scheduler problem at the node counts
  fine grading implies (§9.1), not a loop.
