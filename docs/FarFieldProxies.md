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

- **Assignment by size, and by size alone.** An instance's level is the
  finest whose cell edge is still at least its extent; its cell is then
  chosen by its *centre*, and the cell is **loose** — it owns whatever
  is centred in it, which may reach half a cell beyond its nominal
  bounds. Since an instance placed at level `L` is no larger than a
  level-`L` cell, that half-cell skirt always contains it.

  This is what makes a flat assembly's outlier harmless: a frame member
  spanning the machine lands at a coarse node and is drawn exactly,
  because its screen size is at least its node's and the cut therefore
  never asks to proxy something visually significant. A node's proxy
  covers its own residents *and* everything below it.

  ⚠️ **Position must never decide fidelity.** The natural-sounding
  alternative — assign to the finest cell that contains the box *whole*
  — was written first and measured wrong: on a uniform lattice it
  stranded **28% of the parts** at coarse levels purely for straddling
  a grid line, where nothing ever aggregated them and they drew exactly
  at every tolerance. Fixing it cut the draw count at one tolerance
  from 2784 to 928 (§11.1a).
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
forced to descend (§8). One parameter, two correctness properties —
and, measured, a third pulling the same way rather than against it
(§11.1a).

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

⚠️ That ruling is about **3D gaussian splatting**, and it does not
extend to a surfel or a voxel. A gaussian cloud is a global, sorted,
semi-transparent representation fitted to images; an opaque voxel with
a normal distribution is a depth-writing primitive derived from the
geometry, keeps its silhouette, and needs no sort. §4.1 is why the
difference matters.

### 4.1 The aggregate-detail literature, and what it settles

Phase 2 measured that decimation *deletes* members smaller than a cell
(§11.1c). That is not a property of the decimator this code happens to
use — it is a named problem with a settled diagnosis, and the
literature answers several questions this document had left open.

**It has a name.** Cook et al. (SIGGRAPH 2007) separate **element
detail** — complexity within one object — from **aggregate detail** —
complexity arising from the *number* of objects, and observe that
simplification handles the first well and the second badly. Epic states
it for Nanite without hedging: aggregate geometry "breaks Nanite's
level of detail", because the method relies on merging small triangles
into larger ones, which works "for continuous surfaces… but not for
aggregate geometry that from a distance appears more like a partially
opaque cloud than a solid surface".

That sentence is the diagnosis. Below a pixel, a field of disjoint
parts stops behaving like a surface and starts behaving like a
partially transparent volume. Simplification preserves surfaces; what
has to be preserved is **coverage and the distribution of normals**. No
choice of decimator and no grid resolution changes that, which is why
§11.1c's deletion cannot be tuned away.

**Four families of answer, and where each lands for us.**

1. *Compensate statistically and keep the mesh.* Cook's stochastic
   simplification removes a random subset of elements and alters the
   survivors — chiefly enlarging them — so the aggregate appearance
   survives. Epic confirms this was Nanite's pre-voxel approach,
   "artificially adding surface area to surviving triangles", and now
   calls it "a workaround rather than a solution". Cheap for us, keeps
   the part table, and ⚠️ validated for *stochastic* detail. A CAD
   assembly is regular — bolt circles, fin arrays, connector banks —
   where deleting a random half and fattening the rest is far more
   visible than it is in foliage.
2. *Switch representation to voxels carrying directional appearance.*
   Far Voxels (§4) is the massive-model original: leaves hold triangle
   chunks, **inner nodes are discretised into cubical voxels**, each
   holding a view-dependent appearance fitted by ray-casting the full
   resolution model. Nanite Voxels (UE 5.8, experimental) is the modern
   restatement — disconnected geometry voxelised into clusters of at
   most 128 4×4×4 bricks, rasterised by a separate path, depth-bucketed
   front to back.
3. *Point and surfel hierarchies.* The CRS4 massive-model survey lists
   these precisely because they avoid maintaining topology across
   disconnected components, and recommends **hybrids** — meshes for
   prominent parts, points or voxels for small ones — switching by
   screen-space coverage rather than by uniform simplification.
4. *Better representatives inside the cluster.* Lindstrom (SIGGRAPH
   2000) extends Rossignac-Borrel clustering by accumulating an **error
   quadric** per cell and solving for the representative's position
   instead of averaging it. It does not address deletion — a closed
   sub-cell body still collapses — but it improves everything that
   survives, in one pass, and it is a local change to code we already
   have.

**⭐ The switch criterion is the one §11.1c said we lack.** Nanite's
build chooses voxels over triangles "if doing so would result in lower
error than using triangles". That comparison is only meaningful with an
error metric that can score a representation which *dropped* geometry —
which vertex displacement cannot, since it is silent on what is no
longer there. So the literature independently requires what §11.1c
concluded from measurement: the stored per-node error has to see
deletion.

**⭐ The normal problem has a specific answer.** §11.1c notes that a
closed part inside one cell contributes vertex normals summing to zero,
and that the decimator then substitutes an arbitrary unit vector. The
right object is not an average normal but a **normal distribution**:
SGGX (Heitz et al. 2015) parameterises one by projected area as an
ellipsoid, specifically so that it can be linearly filtered and
prefiltered — exactly the operation aggregation needs and averaging
fails at. Nanite Voxels likewise store a normal distribution per voxel,
at the cost of extra per-voxel data. This is needed by *any* of
options 2-4 above, so it is not a cost that distinguishes them.

**What the CAD industry ships, for contrast.** JT (ISO 14306) stores an
arbitrary number of tessellated LODs **per part**, selected by Range
LOD nodes — element detail only, no aggregation across parts. And
massive-model practice at aircraft scale is **visibility-guided
rendering**: identify the small subset of part occurrences that can
affect the image, rather than approximate the rest. So the shipped
answer to aggregate detail is largely *culling*, and the research
answer is *voxels*. Both are relevant here, and they are not
alternatives — see §10 on ordering.

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

### 10.1 ⭐ Decided: occlusion culling comes first

§4.1 records that massive-model practice answers aggregate detail
mainly by **visibility-guided rendering** — finding the small subset of
parts that can affect the image — rather than by approximating the
rest. That is a different workstream with an attractive property this
one does not have: it is *exact*. Nothing about the image changes, so
there is no representation to choose, no deletion to compensate, no
identity to preserve and no invalidation to design.

It would also reuse phase 1 unchanged. §3 already says the hierarchy is
"better described as the renderer's spatial index than as the LOD
hierarchy: level of detail is one consumer, and frustum culling,
streaming priority and pick acceleration are others" — occlusion
culling is that third consumer, and `ProxyHierarchy` is already built
and tested for it.

The two compose rather than compete: culling removes what cannot be
seen, proxies merge what can be seen but is too small to resolve. But
the composition has an order. **A merged proxy is a worse culling unit
than the parts it replaced** — one blob spanning a cell is visible
whenever any part of it is, where the parts individually might all have
been hidden. Culling then proxying composes; proxying then culling
weakens the culling.

**Decided (2026-08-09): culling first.** The benchmark decides it as
much as the argument does: a **2U rack server assembly** (964 MB STEP,
3.5× the MiSTer Express model of §11.1b) is precisely the shape that
hides most of itself — an enclosed chassis whose interior is invisible
from any outside view.

⚠️ **It is a different scene from everything measured so far.** Phase
0's 66.8% sub-pixel coverage and 6.85 µs per object, and phase 1's
42893 instances, all belong to MiSTer Express; none of them transfer,
and the occluded fraction has no prior to compare against. That is a
feature for a culling measurement — a model that hides most of itself
is the one worth measuring — but it means MiSTer should be run beside
it rather than replaced by it, so that a difference between the two can
be attributed to the model rather than to the change.

Two measurements open that workstream, and neither gates whether to do
it — they size it and say what to build:

- **How many drawn instances contribute no pixel at all.** The same
  shape as §11.1's readouts: cheap, once a second, reported not
  inferred. On an enclosed assembly this should be most of the model.
- **Whether the per-object cost of §9.1 is CPU submission or GPU
  per-draw state.** 6.85 µs is too large for either to be assumed, and
  the frame-rate ablation that produced it cannot separate them. It
  decides the mechanism: a scheme that still submits the draw saves
  nothing if the cost is submission. `bgfx::getStats()` already reports
  `cpuTimeFrame` against `gpuTimeBegin/End`, so this is nearly free and
  should be read before anything is built.

⚠️ **No compute shader is required, and the compute route is the one to
avoid.** Hardware occlusion queries issued per *hierarchy node* rather
than per object (the CHC++ shape, over the index §3 already provides)
are supported by bgfx — `BGFX_CAPS_OCCLUSION_QUERY` — across GL, D3D11,
Vulkan, Metal and WebGL2. A CPU-side software occlusion buffer tested
against node bounds needs no GPU feature at all and behaves identically
in WASM. A GPU-driven HZB with compute and indirect draws is faster and
is unavailable in WebGL2, which would fork the desktop and browser
paths — the one thing `CLAUDE.md` asks renderer work not to do.

### 10.2 measured: what a draw costs, and on which side

`RenderDebug_Timing` now reports the backend's half of the frame beside
the pipeline stages: the render thread's submission time against the
GPU's, and the same pair per draw call. Real RTX 3060, monitor off,
1863×1064, both models converged.

| | server assembly | MiSTer Express |
|---|---|---|
| objects / instances | 5455 / 17727 | 18142 / 42893 |
| draws | 12849 | **41670** |
| primitives | **40.6 M** (3158/draw) | 4.40 M (105/draw) |
| CPU submit | 15.6 ms | 62.1 ms |
| GPU | 21.5 ms | 62.6 ms |
| frame | 50 ms | 137 ms |
| **per draw** | **1.22 µs CPU + 1.68 µs GPU** | **1.49 µs CPU + 1.50 µs GPU** |

**The answer to §10.1's second question is "both, in nearly equal
measure".** Submission and drawing cost about the same, ~1.2–1.5 µs
each per draw call, and the two together account for three quarters of
the frame.

⭐ **And the GPU's share is per-draw state, not pixels and not
triangles.** Two ablations say so, and they are independent:

- **Resolution.** The same camera at 476×255 rasterizes **16× fewer
  pixels** and costs the GPU *nothing less* — 21.5 → 24.8 ms on the
  server model, 62.6 → 61.8 ms on MiSTer. Fill is not where the time
  goes, at any resolution these models are viewed at.
- **Primitives per draw.** The two models differ **30-fold** in
  triangles per draw (3158 against 105) and their per-draw GPU cost
  differs by 12%. Fitting `gpu = a·draws + b·prims` over the pair gives
  **a ≈ 1.50 µs per draw** and **b ≈ 0.06 ns per primitive**: geometry
  is 0.4% of MiSTer's GPU time and 11% of the server's.

⭐ It also explains §9.1's 6.85 µs per object, from a different
instrument. MiSTer issues 41670 draws for 18142 objects — 2.30 draws
each — and 2.30 × 2.99 µs = **6.87 µs per object**. A frame-rate
ablation and a backend timer agree to within a percent, which is the
best evidence either of them is measuring what it claims.

**What it decides.** A culling scheme has to remove the *draw call*, and
it has to do it **before submission**:

- A GPU-side conditional render — issuing the draw and letting the
  hardware reject it — leaves the CPU's 1.2–1.5 µs untouched, so it
  cannot reach more than about half the cost.
- Nothing is gained by simplifying geometry that stays submitted: at
  0.06 ns per primitive, deleting *every* triangle of MiSTer's scene
  would return 0.4% of its GPU time.
- Both this workstream and far-field proxies (§5.1) therefore act on
  the same axis — draw count — which is why they compose, and why
  §10.1's ordering argument matters rather than being a preference.

⚠️ **A harness trap that invalidated two earlier runs, and possibly
older ones.** The probe reported "1920×1200" while the scene rendered
at **400×300**: `mw.showMaximized()` under xvfb has no window manager to
honour it, and `subWindowList()[0]` is the Start page rather than the 3D
view, so maximizing it *shrinks* the viewer to its default. Nothing in
the run said so. The frame line now prints the size the scene was
actually rasterized at — taken from the view's own framebuffer, since
`bgfx::Stats::width` is the default backbuffer, which on the desktop
tier is a dummy nothing draws into and sits at its init size forever.
⚠️ **§11.1b's cut numbers were taken with the same harness pattern and
should be re-read before being relied on**: the cut's tolerance is
scaled by the viewport height, so a 300-pixel-tall viewport makes "64px"
mean a fifth of the screen rather than a twentieth.

### 10.3 measured: how much of a frame could not have reached the screen

`RenderDebug_Occlusion` is the mechanism §10.1 proposes, run without
acting on its answers: the spatial index's node bounds rasterized
against the opaque depth under hardware occlusion queries, 256 per
frame until the partition has been walked, with every instance
attributed to the **highest** node that rejects it. Same cameras and
GPU as §10.2.

| whole-assembly camera | server assembly | MiSTer Express |
|---|---|---|
| instances | 17727 | 42893 |
| **hidden** | **17715 (99.9%)** | **42877 (99.96%)** |
| still drawn | 12 | 16 |
| nodes / walked in | 1377 / 6 batches | 2728 / 11 batches |
| nodes doing the rejecting | **8** | **6** |
| index build | 11.6 ms | 27.5 ms |

**Both models hide essentially all of themselves**, and both do it the
same way: the root is visible, every one of its children is not.

⭐ **Why it is so extreme, and what that qualifies.** Assignment by size
(§3.2) keeps a large instance high in the tree, so the chassis panels —
the only things actually in view — are *root residents*, while
everything they enclose is distributed among the children. A dozen
parts are visible and seventeen thousand are behind them. The corollary
is that this ceiling belongs to *this camera and this visibility state*:
hide the case, as any workflow that wants to see the interior does, and
the saving is gone. What the number establishes is the size of the prize
on the whole-assembly view of an enclosed assembly, which is the view
these models are opened in.

⭐⭐ **Eight nodes decide it.** The descent stops at the highest rejecting
node, so 99.9% of the server model's instances are removed by **eight**
box tests. That is the argument for node-level culling over per-object
culling stated as a measurement rather than as a preference: per-object
testing would issue 17727 queries to learn what eight of them already
say, and §10.2 has just established that the per-draw cost is what the
frame is made of.

**Read it as a floor.** Node bounds are loose, the boxes are padded
outwards (§below), boxes the near plane clips are counted visible, and
untested nodes are counted visible. Every approximation runs towards
"visible".

**And it is not stuck at that answer.** A perspective camera placed at
the model's centre — inside the chassis, which an orthographic zoom
never achieves, since it shrinks the view height without moving the
camera — reports something entirely different on the same document:

| server assembly | whole-assembly camera | camera inside |
|---|---|---|
| hidden (occluded) | 17715 (**99.9%**) | 5931 (**33.5%**) |
| off screen | 0 | 8562 (48.3%) |
| **still drawn** | **12 (0.1%)** | **3234 (18.2%)** |
| nodes tested | 1377 | 483 |
| nodes doing the rejecting | 8 | 57 |
| nodes the near plane exempts | 1 | 115 |

⭐ This is the reading to plan against, not the 99.9%. Inside the model
occlusion still removes a third of the instances and the frustum removes
half again, leaving **18%** to draw — a 5.5× reduction rather than a
1500× one, won by 57 node tests instead of 8, with 115 nodes exempted
because the camera stands inside their bounds. Both numbers are real;
they are the two ends of the range a viewer moves through, and a design
that only pays off at one end is not worth building.

The same shape appears in the synthetic check
(`occlusion_smoke.py`, a wall with parts behind it): **99.5% hidden from
the front, 6.7% from the rear**. One camera cannot tell a working depth
test from a probe stuck at "everything is hidden"; two can.

⚠️ **Three ways a box query answers confidently and wrongly**, all three
of which produced plausible numbers before being found:

1. **A test box must be padded outwards.** A node's bounds are the union
   of its contents', so a box face coincides *exactly* with a real
   surface whenever a part has a flat face at its own extreme — in CAD
   the common case, not an edge case. At equal depth the two disagree in
   the last bit, and where the box loses, LEQUAL rejects every fragment
   and the node calls itself hidden while in plain view. Un-padded, this
   reported 99% of the server model hidden *including its root*.
2. **A box the near plane clips cannot be tested at all** — its front
   faces are gone and the rest are hidden by its own contents. A camera
   fitted to the model puts the near plane on the whole-model box, so
   this is the normal case. Judged on the padded box, since padding is
   what pushes it through.
3. **The probe belongs directly after the opaque bucket.** Placed at the
   end of the frame the root read hidden; moved before the OIT resolve,
   the water and glass surfaces and the copies that force a multisample
   resolve, it read visible. It is also the correct occluder set:
   transparent draws write no depth, so nothing later adds an occluder.

⭐ Hence the guard the readout now carries: **a root reported hidden is
refused, not reported.** Its box contains every drawn thing, so it
cannot be hidden while the frame draws anything, and the descent would
otherwise render that as a spectacular "100% hidden". Each of the three
bugs above was caught by it or by the two-sided smoke test, and none of
them by looking at the number.

### 10.4 what the two measurements decide together

- **Cull, and cull on the CPU.** §10.2: the draw call is the unit and
  submission is half its cost, so the draw has to be removed before it
  is issued. §10.3: on the camera that matters there is almost nothing
  left to issue.
- **Per node, not per object.** Eight tests do the work of seventeen
  thousand, and the index that supports them is already built (§3).
- **The index build is the open cost**, 12–28 ms, unchanged from
  §11.1b's finding: a per-frame rebuild is not viable and the
  incremental index is now required by two workstreams rather than one.
- ⚠️ **Hidden from this camera is not the same as removable.** Shadow
  casters (`ViewShadow` renders from the light) and the ground/planar
  reflection (`ViewGroundRefl` re-renders the scene mirrored) can both
  show geometry the eye cannot see. Culling must be per *pass*, not per
  frame — the measurement above is of the eye pass only.
- **Design for 5×, not for 1500×.** The whole-assembly reading is the
  advertisement; the in-model reading (18% still drawn, 57 rejecting
  nodes, 115 nodes the near plane exempts) is the working case. It is
  the one that decides whether the per-frame cost of maintaining
  visibility is affordable, and the one where nodes the camera stands
  inside stop being answerable at all.

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
| 2 generation | `Gui/Renderer/MeshSimplify.*`, **new** `Gui/Renderer/ProxyStore.{h,cpp}` | merge N transformed meshes per (cell, material), then decimate; bottom-up per sec 7.1, kept per node id -- built, see 11.1c-e |
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

### 11.1a Phase 1 as built, and what it corrected

`Gui/Renderer/ProxyHierarchy.{h,cpp}` with tests in
`tests/src/Gui/ProxyHierarchy.cpp`. `BoxSight`/`sightBounds` moved out
of `SceneLadder.cpp` into the new header so that the cut and the
coverage histogram share one projection rather than two copies of it,
and `materialIdentity()` was added beside `writeMaterial` in
`SceneDump.cpp` — equality by serialized bytes, so a field added to the
material format cannot silently merge two buckets and overstate the
win.

⚠️ **The numbers below are a synthetic 20×20×20 lattice of 8000 equal
parts, not a real assembly.** They are the right shape to reason about
the partition and useless for sizing the win — that is what the
`RenderDebug_*` readout on MiSTer Express is for, and it has not been
taken yet. Phase 0's own lesson applies: only assembly scale exhibits
the effect, and a model that does not have the shape cannot show it.

Two things the measurement corrected, both of which had been written
down as reasoning and were wrong:

1. **Strict containment stranded a quarter of the model** (§3.2, now
   fixed). Assigning each instance to the finest cell containing its box
   whole meant a part's *position* decided its level: 28% of an
   otherwise uniform lattice sat at coarse levels because they crossed a
   grid line, and nothing ever aggregated them. Draws at a fixed
   tolerance: **2784 → 928**. Size-only assignment with loose,
   centre-placed cells is the fix, and the general rule it stands for is
   that position must not decide fidelity.
2. **`K` does not trade against the draw count** — §11.4 had claimed the
   pop wants it small and the draw count wants it large. Measured at a
   fixed tolerance: `K` = 4, 8, 16 → **624** draws; 32, 64 → **928**;
   128 → **8000**. Smaller is better or equal throughout, because a leaf
   that misses the tolerance dumps *all* its residents as exact draws,
   where a deeper tree offers a finer level to stop on. So `K` may be
   chosen for the size of a pop and the cost of a forced descent alone.
   Its real counter-pressure is the one §9.1 already names: node count
   becomes generation throughput and chunk count.

One property worth knowing before phase 3 tunes anything: the draw
count against tolerance is a **step function**, and its floor is not a
property of the cut but of the tree. Below the tolerance the deepest
level projects at, there is nothing left to stop on and everything
draws exactly — which is the same observation as (2) from the other
side.

### 11.1b The measurement, on MiSTer Express

`MiSTer_objdefaults.FCStd`, 18142 objects, real RTX 3060 (VirtualGL EGL
over Xvfb, monitor off), whole-assembly camera after the progressive
load converged. **42893 drawn instances**, 2728 nodes, depth 12, 37
material buckets, partition build **28-30 ms**.

⚠⚠ **Re-measured, and the first table was wrong.** It was taken at a
viewport of **400×300**, not the 1920×1200 it recorded: the probe called
`mw.showMaximized()`, which under Xvfb has no window manager to honour
it, and nothing in the run said so (§10.2 has the full trap). The cut
scales its tolerance by the viewport height, so every row was labelled
with a tolerance ~3.5× too small. Corrected, at **1863×1064**:

| tolerance | draws | proxy | exact | vs 42893 | as first published |
|---|---|---|---|---|---|
| 1px | 41975 | 48 | 41927 | 1.02× | — |
| 4px | 38667 | 643 | 38024 | 1.11× | 24567 (1.75×) |
| 16px | 23907 | 3286 | 20621 | 1.79× | 11718 (3.66×) |
| 64px | **11240** | 1532 | 9708 | **3.8×** | 2277 (**18.8×**) |

⭐ **The old numbers are not noise; they are the same curve shifted
along the tolerance axis by the viewport ratio.** The corrected 4px row
reproduces the old 1px row, the corrected 16px row the old 4px row, and
so on: the old "64px" was really a tolerance of about **227 px** — a
blob a fifth of the screen high.

**The rack server says the same, more so.** Same correction, same
camera discipline, 17727 instances over 1377 nodes and only 4 material
buckets (build 20 ms):

| tolerance | 1px | 4px | 16px | 64px |
|---|---|---|---|---|
| draws | 16755 | 16680 | 15035 | **7831** |
| vs 17727 | 1.06× | 1.06× | 1.18× | **2.26×** |

Its first look, taken at the same broken 400×300, read 813 draws at 64px
— **21.8×**. Two models, two scenes, the same order-of-magnitude
overstatement, and the same explanation.

⚠⚠ **That changes the case this document was arguing.** The 18.8× that
motivated phase 2 is available only at a coarseness no viewer would
accept. At tolerances a user would not notice the cut aggregates
**1.1× at 4px and 3.8× at 64px** — real, but a different proposition
from an order of magnitude, and one that has to be weighed against
§11.1c's finding that a proxy also *deletes* sub-cell geometry. Set
beside §10.3, where eight occlusion tests remove 99.9% of the same
model exactly, the ordering decision of §10.1 looks better than it did
when it was made on argument alone.

⚠️⚠️ **The tolerance axis here is node *extent*, not proxy *error*, and
the difference is most of the answer.** §3.3 selects by "projected
screen-space error", but no proxy exists yet to have an error, so phase
1 stands in the node's projected extent — which asks that the whole
merged blob be smaller than the tolerance. That is far stricter than
what a proxy actually commits: a cell of twenty screws spanning 64px
merges into a mesh whose *decimation* error is a pixel or two. So the
operating point is not the 4px row; it is wherever a decimated
(cell, material) proxy's error lands relative to its extent, and that
ratio is unmeasured. **Phase 2's first job is to measure it**, because
it decides which row of this table is the operating point. It has since
been measured, and the answer is that no single ratio converts the
table — §11.1c.

⚠️ **The coverage histogram in the same run disagrees with §9.2**: about
11-13% of on-screen objects at or under 4px, against phase 0's 66.8%,
over 9563 drawable objects rather than 19362. Phase 0 measured a *STEP
import*; this is a *saved document*, and the two are evidently not the
same scene. Until that is reconciled the ratios above belong to this
scene only — and since a scene with less sub-pixel content has less to
aggregate, they most likely understate the case rather than flatter it.

Two numbers that need no such caveat:

- **Materials per cell are ~2.9**, against 37 document-wide, at the
  depths a cut actually stops on (L5-L10). §11.4 worried that the
  material fan-out of §5.1 might give the win back; measured, it costs
  about 3× the node count, not 37×.
- **28 ms to build the partition** over 42893 instances. Phase 3 cannot
  rebuild it on the plan's schedule at that price, so the index has to
  update incrementally — which the positional node identity of §3.2 was
  already chosen to allow, and which is now a requirement rather than a
  nicety.

### 11.1b-prims RE-GATED ON PRIMITIVES -- the prize is 2.6x smaller

XX **Every row above is in draws, and draws were then measured to cost
nothing on this scene.** `docs/DrawSubmission.md`: the frame is 20.4 ms
against 19.8 ms of GPU, and a quarter of the pixels buys 3.3% while
per-primitive cost holds -- the frame is GPU-bound and the GPU is
geometry-bound. A cut that removes draws without removing primitives
buys exactly nothing here.

So `selectCut` now counts both. Rack server, native, swap interval 0,
converged, viewport 1791x880, 17727 instances over 1377 nodes, 4
buckets, build 12-19 ms. 88 reports, byte-identical -- the cut is
deterministic:

| tolerance | draws | vs 17727 | prims exact | prims covered | **covered %** |
|---|---|---|---|---|---|
| 1px | 17604 | 1.01x | 27.11M | 0.06M | **0%** |
| 4px | 14214 | 1.25x | 26.13M | 1.04M | **4%** |
| 16px | 8265 | 2.14x | 23.57M | 3.60M | **13%** |
| 64px | **4457** | **3.98x** | 19.26M | **7.92M** | **29%** |

!! **This is the TIME axis only.** Primitives bound what the cut saves
the GPU per frame. What it saves in *residency* is a different number --
covered BYTES, not covered primitives -- and residency is what decides
whether a model opens at all rather than how fast it draws. The bytes
column below is that axis; neither judges the other.

**At 64px the cut removes 75% of the draws and 29% of the primitives.**
That is the number the phase-2 case has to be made on, and it is 2.6x
smaller than the one it was made on. It is also a **ceiling**: the proxy
that replaces those 7.92M primitives has primitives of its own, which
come off the saving and cannot be known without generating (sec 11.1c).

#### And on the residency axis: 21%, less than the primitive axis

Same run, same tolerances. Bytes are counted per **distinct mesh**, and
a mesh frees only where no exact instance still references it -- summing
over covered instances would overstate it several times, since 495
instanced submits stand in for 5432 rows here:

| tolerance | held | **freed** | share | meshes freed |
|---|---|---|---|---|
| 1px | 259.8MB | 0.1MB | 0% | 87 |
| 4px | 258.5MB | 1.4MB | 1% | 738 |
| 16px | 249.9MB | 10.0MB | 4% | 1532 |
| 64px | 204.7MB | **55.1MB** | **21%** | 2463 |

* **The memory axis is WORSE than the time axis, not better** (21%
against 29%), and the reason is sharing: a covered instance whose mesh
still has one exact user frees nothing. The guess that per-mesh overhead
would push residency past the primitive share was wrong -- 2463 fewer
buffers is under a megabyte of handles against 55MB of payload.

!! **Neither axis can be judged on this model.** Its entire resident
geometry is **260MB**. On the card these numbers were taken on that is
2% of VRAM, so the scene is nowhere near the regime where residency
decides anything, and 21% of nothing is nothing. The memory case for
aggregation has to be made on a model that actually strains a device --
which is the case the mechanism exists for, and which no measurement in
this document has ever used.

** **The bigger finding is the other column.** At the most aggressive
tolerance measured, **19.26M of 27.17M on-screen primitives -- 71% --
are in objects too large on screen to merge at all.** Far-field
aggregation cannot reach them by construction: sec 3.2 assigns by size, so
a big object is a resident of a high node and draws exactly. **The
larger pool is per-object simplification of near geometry, not
far-field merging of distant geometry**, and that is a different
mechanism from the one this document plans.

!! **An unexplained denominator, and the shares above are the table's,
not the frame's.** The instance table accounts for **27.17M** on-screen
primitives (constant across tolerances, as it must be); the same frame
renders **39.60M**. The 1.46x gap is not diagnosed -- candidates are a
second pass over the same geometry, and instanced rows counted once here
and per-instance there. Against the frame, 64px covers 7.92/39.60 =
**20%**. **Do not quote 29% as a share of the frame until the gap is
closed** -- this workstream has already published one share against the
wrong denominator (sec 12.19).

! The draw column differs from sec 11.1b's rack table (4457 vs 7831 at
64px) because the viewport is shorter (880 vs 1064 px). The tolerance is
scaled by viewport height, so the same px tolerance is a coarser world
tolerance here. Not a contradiction; a reminder that no row of any of
these tables means anything without its viewport.

### 11.1c What a proxy commits, generated and measured

`RenderDebug_ProxyGen`, same document, same machine and the same
converged whole-assembly camera as §11.1b — 42893 instances, 163 nodes
on the 64px cut. It samples 24 of those nodes, merges each (cell,
material) group for real, and decimates the merge at three grids: the
node's own cell divided by 4, 8 and 16. Powers of two, so that every
level's decimation grid stays a refinement of the level above it
(§3.2). 38 merges, 0.20 M source triangles.

| grid | err/extent mean | worst | tri | area kept | members lost | proxy verts |
|---|---|---|---|---|---|---|
| cell/4 | 0.137 | 0.268 | 207× fewer | 46% | 1657/1870 | 591 |
| cell/8 | 0.084 | 0.340 | 65× fewer | 58% | 1438/1870 | 1755 |
| cell/16 | 0.050 | 0.240 | 28× fewer | 82% | 1009/1870 | 3757 |

The error is per-vertex displacement against the representative each
vertex collapsed onto, which *bounds* the surface deviation rather than
approximating it: a triangle's three corners each move by at most that
much and every point of the triangle is an affine combination of them.

#### The conversion §11.1b asked for does not exist as a number

At cell/8 the mean node commits 8.4% of its extent and the worst commits
34%. Applied to the 64px row that is 5px of error for a typical node and
22px for the worst one, and the two answers sit on opposite sides of any
tolerance worth choosing. **A single ratio cannot convert the table**;
the spread within one camera is fivefold, and it is not noise but shape
— a node holding one long bracket and a node holding forty screws
decimate nothing alike.

What that argues for is what §3.3 said in the first place: the cut
descends by *the node's own* projected error. Phase 1 stood the extent in
for it because nothing had been generated yet, and the honest reading of
§11.1b is now that its rows are neither 3.7× nor 18.8× but a
distribution — and that phase 3 must read a measured error off each
node rather than scaling a global constant.

⚠️ **But not `ProxyMeshStats::maxError` on its own.** An earlier draft
of this section said generation already computes the number and it
costs a field rather than a pass. It does not: displacement is bounded
by one cell whether the proxy is faithful or *empty*, so a cut steering
by it would rank a hole as well as — sometimes better than — a good
proxy, and would preferentially stop on exactly the nodes it should
descend past. The stored error has to account for what is missing, as a
one-sided Hausdorff distance from the source surface to the proxy
(unbounded where the proxy has nothing) or as displacement plus an
area-loss term. §4.1 arrives at the same requirement from the
literature: Nanite's build compares voxel error against triangle error,
which is not a comparison displacement can express.

⭐ Worth noting because it was not obvious: **the ratio is a property of
the geometry and the grid, not of the camera.** Measured at 4× zoom, on
a cut of 512 nodes instead of 163 and a different set of members
sampled, cell/8 gives a mean of 0.0835 against 0.0844 — a 1% difference
where the camera moved 4×. So it can be measured once, at generation
time, and stored on the node, which is exactly what the previous
paragraph needs.

#### ⚠️⚠️ Decimation deletes; it does not shrink

The finding that changes the representation rather than the parameters.
At cell/8, **77% of the members came back empty** — nothing in the proxy
stands for them at all — and 42% of the surface area is gone. (Not the
same 42%: a surviving member also loses area to its own decimation. The
two numbers bound the effect from either side.) Clustering has no
mechanism to keep a member smaller than a cell: every one of its
triangles has three corners in one cell, so every one is degenerate and
is dropped. A field of small parts therefore vanishes as a body while
every error the run reports stays inside the tolerance — the
displacement metric cannot see a deletion, which is why the area
retained is reported beside it.

The sharpest form of it is the refusal count: a group that produces no
triangles at all yields **no proxy**, and that is not rare at a coarse
grid. **9 of 38 groups at cell/4**, 2 of 38 at cell/8, none at cell/16.
A refusal is a proxy-sized hole where a subassembly should be, not a
cell-sized one — the displacement bound applies to surfaces that
survive and says nothing about a region where none did. (The readout
does not distinguish the three reasons `simplifyMesh` can refuse; at a
coarse grid it is total collapse, but that is inference rather than
measurement.)

Even at cell/16, which costs 28× rather than 65×, a fifth of the area is
gone and half the members with it. §5(2) said a surfel or voxel proxy
was "justified only against a real model that shows the failure"; this
is that model, and the failure is not the mush §5 anticipated but
absence. It also suggests something cheaper than surfels first: the
merge already knows *exactly which* members collapsed, as the empty
slots of its part table, and the renderer already synthesises a box for
geometry that is not there (`DrawCall::standIn`, the bottom rung of
docs/SceneStreaming.md §6). A box per collapsed member, appended after
the decimation, carries the mass with no new shader and no new pass.

The last column says what that would cost and which way to bound it.
Standing in **per collapsed member** costs 12 triangles each — at cell/8,
1438 × 12 = 17 k triangles against a proxy of 3 k, five times the proxy
itself. Standing in **per occupied cell** is bounded by the proxy's own
vertex count instead, because clustering already emits a representative
for every occupied cell whether or not a triangle survived on it: 1755
at cell/8, and 591 against 1657 collapsed members at cell/4 — the
tighter bound exactly where the deletion is worst, and bounded by the
grid rather than by how many parts happen to be in the cell. That is
Far Voxels' argument arrived at from the other end, and it is the one to
build first.

#### Two numbers that were expected to be worse

- **Generation is cheap.** 38 proxies over 0.20 M source triangles cost
  8 ms to merge and 13 ms to decimate at cell/8 — about 0.5 ms per
  proxy, on one thread, in a debug readout that was not written for
  speed, and the merge is paid once however many rungs are cut from it.
  Against §9.1's worry about generation throughput this is not the
  bottleneck it was budgeted as.
- **The instancing gate does not bind here — but this model cannot
  test it.** §7.1 warned that merging 500 instances of one screw
  expands what instancing shares today. Over the sampled groups it is a
  1.05× effect, and the reason is visible in the scene-wide counts the
  same readout prints: MiSTer Express draws its triangles from **as
  many distinct geometries as it has draws** (8328 meshes behind 8479
  triangle draws, and the same 8328 counted by source node rather than
  by cache id, so the sharing is not merely hidden below the key). A
  STEP import gives every occurrence its own shape. So the gate is
  *untested* rather than shown not to bind, and it stays in the code —
  a Link-heavy or fastener-heavy assembly is the model that would
  exercise it, and there is no reason to hold phase 2 for one.

#### What this measurement does not cover

24 of 163 nodes, stride-sampled, and the readout reports both numbers
along with everything it skipped for the triangle budget. Groups with a
single member are not proxied at all (56 of them here), and lines and
points are their own material buckets and stay exact (48 here) — a line
proxy is a separate question this does not touch.

### 11.1d Decided: the deleted mass is carried per cell, not per member

`RenderDebug_ProxyGen` now generates both candidate stand-ins on every
merge it makes and reports what each cost and what each committed, so
11.1c's open question is answered by the model rather than by the
argument. Same machine, same document, same converged whole-assembly
camera (8479 triangle draws over 8328 distinct meshes, 140-163 nodes on
the 64px cut), 19 reports aggregated at camera A and 8 at 4x zoom;
29161 members sampled in the far field.

A stand-in is an axis-aligned box, 12 triangles, appended after the
decimation -- what `DrawCall::standIn` already draws for geometry that
has not arrived. The two candidates:

- **per collapsed member**: one box around each member the decimation
  dropped;
- **per occupied cell**: one box per cell of the decimation grid that
  holds dropped content, sized to the content in that cell rather than
  to the cell.

Both are **conservative covers of the same point set** -- every
deleted vertex lands inside a box either way -- which is what makes them
comparable at all: the triangles are the price, and the volume and area
are how loosely each one holds the same content. Neither can win by
covering less of the model.

**Far field (camera A, the operating point).**

| grid | members lost | per-member boxes / tri | per-cell boxes / tri | per-cell volume | per-cell area | per-cell names |
|---|---|---|---|---|---|---|
| cell/4 | 25106 (86%) | 25106 / 301 k | 5974 / 72 k (0.24x) | 1.61x | 0.76x | 1222 (5%) |
| cell/8 | 21866 (75%) | 21866 / 262 k | 9220 / 111 k (0.42x) | 0.50x | 0.47x | 2026 (9%) |
| cell/16 | 16173 (55%) | 16173 / 194 k | 12159 / 146 k (0.75x) | 0.77x | 0.63x | 2447 (15%) |

The volume and area columns are per-cell as a fraction of per-member;
under 1.0 means the per-cell cover is the tighter of the two.

**The verdict is per cell, and it is not close at the operating grid.**
At cell/8 it costs 2.4x fewer triangles *and* commits half the volume
and half the surface area. That is the result 11.1c did not predict: the
expectation was that per-cell would buy its cheapness by filling the
gaps between the parts it aggregates, and instead it is tighter on both
counts, because the loss runs the other way too -- a member whose shape
is nothing like its bounding box (a long diagonal bracket, a bent clip)
is over-covered by the one box per-member gives it, and the grid cuts
that box into pieces that follow the part.

!! The gap-filling does show up, but only at the coarsest grid: at
cell/4 per-cell commits **1.61x the volume** while still holding less
surface area. A cell there is a quarter of the node, so a box that
unions whatever is in one fills real space between real parts. It is an
argument against standing in at grids coarser than cell/8, not against
the representation.

**It is close to area-preserving, which is the property that
matters.** Section 4.1's diagnosis is that a far field stops behaving
like a surface and starts behaving like a partially opaque cloud, so
what has to survive is coverage. At cell/8 the decimation deletes
2.25e4 of surface area and the per-cell boxes put back 1.81e4 of it --
0.81x, slightly under. Per-member puts back 3.85e4, **1.7x the area it
is standing in for**: it does not restore the missing coverage, it
overshoots it, and pays 2.4x more triangles to do so.

**Near field (camera B, 4x zoom).** The ordering reverses on cost and
holds on tightness: per-cell costs **1.2x to 5.8x more boxes** (its
count is bounded by the grid, not by the member count, so a cut node
with few deletions still pays for every cell it spans) while committing
0.13x-0.46x the volume. This is the regime where per-member is the
cheaper of the two -- and it is also the regime where deletion barely
happens: 255 members lost at cell/16 against 16173 in the far field. The
absolute cost of being wrong here is 18 k triangles against a scene of
several million.

**What per cell costs, stated plainly: identity.** A box that stands in
for one member sits in that member's part slot and a pick still names
the part it hit; a cell holding several members can honestly name none
of them, and the far field is crowded -- **per-cell names 5-15% of the
deleted members against per-member's 100%**. Section 6's invariant is
kept either way (a pick resolves to no object, never to the wrong one),
but per cell it resolves to nothing far more often. This is the same
trade the rung ladder already makes and it is judged acceptable at a
distance where the whole node is 64 px; near the camera, where a user
picks, the deletion is small and the cut has descended past the node
anyway.

**The structural argument, which the measurement does not see and
which points the same way.** Section 7.1 builds a parent's proxy from
its children's, so whatever carries the deleted mass has to compose the
same way. Per cell it does: the grids nest, so eight child cells fall in
one parent cell and their boxes union into one, and the parent's
stand-in count stays bounded by the parent's own grid. Per member it
does not: a parent inherits every member deleted anywhere below it, so
the stand-in table grows with the subtree while the proxy it accompanies
shrinks -- exactly the wrong shape, and the bottom-up construction would
have baked it into every level.

! **What neither representation is: cheap.** At cell/8 the per-cell
stand-ins cost 1.29x the proxy's own triangles, so a node's real cost is
the proxy plus a bit more than the proxy again. The aggregation still
wins by a wide margin against the source, but phase 3's cut has to price
a node at proxy + stand-in, not at proxy, and 11.1c's error term has to
account for the stand-in as geometry that is present rather than
missing.

! Two things this does not settle. The boxes are opaque, so the
"partially opaque cloud" of section 4.1 is approximated by a solid at
the coverage the boxes happen to have; a normal distribution per cell
(SGGX, section 4.1) is the next refinement and it is orthogonal to which
unit the stand-in is keyed on. And a box is not a voxel: this measures
the cheap end of section 5(2), not Far Voxels itself.

### 11.1e Built: generation bottom-up, and what it costs against from source

`ProxyStore` (`Gui/Renderer/ProxyStore.{h,cpp}`) generates a subtree
post-order and keeps the result keyed by ProxyNode::id. A node merges
its own residents together with what its children already built --
their proxies and the boxes standing in for what those children deleted
-- and `ProxyGenOptions::fromSource` keeps the old behaviour as a
control, because "bottom-up is cheaper" is a claim about this
codebase's numbers rather than a citation. `RenderDebug_ProxyGen`
reports both over the nodes a cut stops on.

MiSTer Express, converged, camera A, four sampled cut nodes covering
122 generated nodes over a six-level chain:

| | merged | time | entries | drawn: proxy + stand-in | worst err/extent | area kept |
|---|---|---|---|---|---|---|
| bottom-up | 0.05 Mtri | 9 ms | 127 | 6004 + 2436 = 8440 | 0.151 | 59% |
| from source | 0.11 Mtri | 18 ms | 129 | 6020 + 9060 = 15080 | 0.135 | 70% |

**Generation halves**, which is section 7.1's claim and the shape it
predicted: each level merges a decimation of the level below, so the
total is a geometric series rather than the source counted once per
level. Six levels deep it is 2.2x here rather than an order of
magnitude, because most of the cost is the leaves either way -- the
ratio grows with depth, and depth grows with model size.

**It also halves what the node draws, which was not predicted.** The
proxy triangles are the same to within a rounding (6004 against 6020 --
the same merge, one level of decimation apart), and the whole
difference is the stand-ins: **2436 against 9060, 3.7x fewer**. That is
the nesting of 11.1d paying a second time. Built bottom-up, a child's
boxes enter the parent's merge as geometry and re-aggregate onto the
parent's coarser grid; generated from source, every level re-derives
the same fine boxes from the same deleted members and carries all of
them. So the representation that made the deleted mass affordable per
level is also what stops it accumulating between levels.

**What it costs is 12% more error**: 0.151 against 0.135 as a fraction
of extent, from decimating a decimation. Against halving both the
generation cost and the drawn triangles that is a good trade, and it
is bounded by construction rather than by luck -- the stored error is
the worst input plus this level's displacement, so it is a bound, and
monotone up the tree (which is what section 8.1 needs for nodes to
decide independently).

The area kept falls too, 59% against 70%. Part of that is definitional:
a coarser box holds less surface than the several boxes it replaces,
which is what aggregation is. Part of it is real, and it is the reason
the stored error has to be read beside the area rather than alone.

!! **One gap, inherent to the rule.** A material bucket whose members
are spread one per child never accumulates: each child refuses it as a
single member, so the parent sees nothing to merge. Measured as 127
entries against the control's 129. Nothing is lost visually -- those
instances draw exactly, which is what the cut does with anything it has
no proxy for -- but the aggregation is not available there. Passing a
refused bucket's residents up to the parent would close it, and that is
machinery for phase 3 rather than a defect in what is built.

A related case did have to be fixed: a node whose bucket holds exactly
one *child proxy* refused under the single-object rule (minMerge),
which broke the chain wherever a subtree narrowed. Coarsening a lone
child proxy onto this level's grid is exactly what a level is for, so
the rule now applies to source members only.

### 11.1f Built and measured: the cut reads the error, and pays for the proxy

Two stand-ins went into phase 1 because nothing had been generated yet,
and both were flagged where they were used: the descent judged a node by
its projected *extent* rather than by an error it did not have, and the
saving was quoted as what a proxy replaces because nobody knew what one
costs. `ProxyStore` removes both. `ProxyNodeCost` carries the measured
error ratio and the drawn primitives per node, `selectCut` takes it, and
`RenderDebug_ProxyGen` generates the whole partition and sweeps
tolerances over both rules.

MiSTer Express, converged, whole-assembly camera, 1863x1064. The store
is 1802 nodes and 2259 entries, 1.95 Mtri merged in 292 ms, holding
151 k proxy and 182 k stand-in triangles.

| tol | extent: draws (proxy) | extent covers | error: draws (proxy) | error covers | proxies cost | drawn | net |
|---|---|---|---|---|---|---|---|
| 4px | 36717 (1188) | 31.8 k prims | 13071 (482) | 886 k | 76 k | 1.82 M of 2.63 M | **1.44x** |
| 16px | 19290 (2914) | 761 k | 6600 (241) | 1.14 M | 49 k | 1.56 M of 2.66 M | **1.70x** |
| 64px | 9581 (1490) | 983 k | 3444 (177) | 2.03 M | 40 k | 0.80 M of 2.79 M | **3.48x** |

**The measured error is what makes a usable tolerance aggregate at
all.** At 4 px -- an error a viewer has no way to see -- the extent rule
aggregates 32 k primitives out of 2.6 M, which is 1.2% of the frame and
is why 11.1b read as a weak case. The same tolerance read as *error*
aggregates 886 k, and the frame drops by 1.44x. The rule did not change;
what changed is that a node is now judged by what it commits rather than
by how big it is, and 11.1c already showed those differ by a factor of
five within one camera.

**Draws fall by the same rule, not despite it**: 3444 against 9581 at
64 px, 13071 against 36717 at 4 px. A frontier that stops higher issues
fewer of both.

**A proxy is cheap; what stays exact is not.** The proxies the error cut
stops on cost 40-76 k primitives to stand for 0.9-2.0 M -- 4% at 64 px,
9% at 4 px. So the ceiling on the win is set almost entirely by the
geometry the cut still draws exactly, which is the near half of the
model, and not by what aggregation costs. That is the opposite of the
worry in 11.4, and it says where the next work is: it is not worth
tuning the proxies, it is worth asking why 1.8 M primitives at 4 px are
still exact. 11.1g asked, and the answer is that they are near field --
and the same instrument found that this table overstates its own
saving, because it counts edges under a stopped node as covered by a
proxy that never had them.

!! **Three things this table is not.** The denominator moves between
rows: a cut that stops higher culls at node granularity, so a coarse
stop keeps some off-screen members its finer counterpart would have
dropped, and each row is read against its own visible total rather than
against a fixed one. The units are mixed: `exactPrims` counts whatever
a draw issues, so line segments and points are in it, while a proxy is
triangles only -- lines and points stay exact by design (11.1c). And
410 buckets were skipped as single-member, which is the gap of 11.1e at
scale: those branches never aggregate, so the table understates what a
complete generation would give.

### 11.1g Measured: the exact mass is near field, and the edges are the ceiling

11.1f ended by saying it was not worth tuning the proxies, it was worth
asking why 1.8 M primitives at 4 px are still drawn exactly. The
instrument for that is `ProxyHierarchy::explainExact`: every exact
instance is a resident of a node the descent went past, so the reason
belongs to the node -- its error was resolvable, nothing was generated
for it, or it held too few members to be worth a proxy -- crossed with
how large the instance itself projects, in multiples of the tolerance.
`ProxyCut` records which node each exact instance came from, because the
reason cannot be recovered afterwards without searching the partition
again.

MiSTer Express, converged, whole-assembly camera, same store as 11.1f
(1802 nodes, 2259 entries, 410 buckets skipped as single-member).

| tol | exact | resolvable | no proxy | too few members | <= 1x tol | > 16x tol |
|---|---|---|---|---|---|---|
| 4px | 2.00 M / 35925 inst | 1.91 M (95%) | 0.09 M (5%) | 0 | 0.04 M | 1.44 M |
| 16px | 1.87 M / 34909 | 1.79 M (96%) | 0.08 M (4%) | 0 | 0.32 M | 0.66 M |
| 64px | 1.58 M / 34207 | 1.52 M (96%) | 0.06 M (4%) | 0 | 0.41 M | 0.08 M |

**The answer to 11.1f's question is that the exact mass is near field,
and it is near field correctly.** 95-96% of it sits under a node whose
own measured error the camera can resolve, which is the one reason that
is not a gap. At 4 px, 1.44 M of the 2.00 M is carried by instances
projecting to more than *sixteen times* the tolerance -- 64 px and up on
a 1064 px viewport -- and no cut should touch those. The hoped-for
bug-shaped cause is 4-5% and never larger, so the 410 single-member
buckets of 11.1e are worth closing for tidiness and not for the frame.

#### KEY: the same instrument found the table above it was overstating

Asked from the other side -- what does a stopped node count as covered
that no generated proxy draws -- the readout answered 17-27% of the
covered figure, and **99% of it was edges**. A cut covers everything
below a node it stops on; generation accepts triangles only (11.1c: a
decimated edge is not an edge), so an edge under a stopped node was
being counted as removed while still having to be issued. The saving
11.1f quoted was gross in a second way nobody had noticed.

`ProxyInstance::mergeable` closes it: set from the same rule
`ProxyStore::build` applies, read by `selectCut`, which now walks a
stopped node's subtree and puts what no proxy can stand for into the
exact list beside the proxy rather than under it. `verifyCut` reads
"its covering proxy" the same strict way. It costs a subtree walk per
stopped node -- which phase 4 has to do anyway to issue those draws.

| tol | 11.1f drawn / net | corrected drawn / net | 11.1f draws | corrected draws |
|---|---|---|---|---|
| 4px | 1.82 M, **1.80x** | 2.13 M of 3.31 M, **1.56x** | 13300 (497 proxy) | 36574 (649) |
| 16px | 1.55 M, **2.17x** | 1.93 M of 3.31 M, **1.72x** | 6600 (241) | 35193 (284) |
| 64px | 1.01 M, **3.34x** | 1.62 M of 3.37 M, **2.08x** | 3444 (177) | 34388 (181) |

The primitive win survives, smaller: 1.56x to 2.08x rather than 1.80x
to 3.34x. **The draw win does not survive at all.** 11.1f reported
draws falling from 9581 to 3444 at 64 px; corrected, the same cut
issues 34388, because 30823 edge draws below stopped nodes were being
counted as absorbed by proxies that never had them. What is left
uncovered after the fix is the generation gap proper, and it is 0.2-1.8%
-- 50 to 73 instances.

#### KEY: what this makes the next question

The far field's ceiling is **un-aggregatable edge geometry**, not proxy
cost and not the generation gap. It shows up twice in the same table.
At 64 px, 0.41 M of the 1.58 M still exact is carried by instances
projecting to *less* than the tolerance -- 30723 of them, almost all
edges, each too small for a viewer to resolve and none of them
mergeable. And the draw count, which the cut was built to reduce, is
now dominated by the same population: 34388 draws of which 181 are
proxies.

So an aggregating far field has to say something about edges. Three
candidates were listed here, in the order they should be tried -- and
11.1h then found that the first of them is already written as the
element contract, so it needed no new rule at all. The list is kept
because the other two remain the answer for a *floating* edge, which
no contract gates:

1. **Drop them below a tolerance.** An edge whose whole part projects to
   under a pixel of error contributes a darkening, not a line. Cheapest
   to build and the appearance question is the whole of the risk --
   which makes it a pixel comparison, and 12.21's injected-failure
   harness is the shape of that test.
2. **Merge the line sets per (cell, material) the way triangles are
   merged.** No decimation, just concatenation into one buffer: it
   removes draws without removing primitives, and 11.1b-prims already
   established that draws alone are not what costs on this scene -- so
   this is the option that helps least on the axis that matters.
3. **Decimate them as polylines**, which is the only one that removes
   primitives, and the only one that needs a simplifier that does not
   exist yet.

None of this changes phase 3's remaining work (hysteresis on a retained
cut, incremental index update, wiring into `SceneLadder.cpp`), and all
of it is measured against a cut that now reports what it would actually
issue.

### 11.1h Built: the far field does not need an edge rule, it needs the element contract

11.1g ended with three candidates for the edge ceiling and picked
"drop them below a tolerance" as the cheapest, which would have meant a
new knob, a new threshold and a fresh argument about appearance. None
of that is necessary. **The rule is already written, already shipped
and already ruled on** (docs/SceneStreaming.md 13b, the element
contract):

> An attached LINE set draws only while its object's face set is shown
> and memory allows. An attached POINT set draws only while its object's
> line set is shown. A FLOATING point or line set ranks WITH THE FACES;
> it is the object, and it is never gated.

and the half of it that decides this case is the coarseness half, in
`gatedForMemory`:

> An edge set describes the shape its faces approximate, so drawing it
> over a rough rung decorates geometry that is not the answer yet.

**A proxy is the coarsest rung there is.** An attached edge below a
node the cut stopped on is decorating a shape that has been merged into
a cluster with twenty others; the contract already says that edge does
not draw. A floating edge -- a sketch, a datum line, a wire, or a set
the producer has not classified -- ranks with the faces and must still
be drawn exactly, because nothing else on screen would show it.

So `ProxyInstance` carries the producer's `attachedOnly` bit, and
`selectCut` applies the contract's own dependency, per object and in
two passes: whose faces did a proxy take, then, of everything below a
stopped node, gate the attached sets belonging to those objects and
draw the rest. `verifyCut` names the third outcome explicitly and
recomputes it from the rule rather than reading the cut's counters, so
a member the cut silently dropped still counts as a violation.

MiSTer Express, converged, same store and camera as 11.1f and 11.1g.

| tol | 11.1f: net / draws | 11.1g corrected: net / draws | with the contract: net / draws | gated |
|---|---|---|---|---|
| 4px | 1.80x / 13300 | 1.56x / 36574 | **1.83x** / 21814 (526 proxy) | 14519 sets, 251 k prims |
| 16px | 2.17x / 6600 | 1.72x / 35193 | **2.16x** / 17315 (258) | 17830, 354 k |
| 64px | 3.34x / 3444 | 2.08x / 34388 | **3.31x** / 14270 (181) | 20333, 601 k |

**The number 11.1f reported comes back, and this time it is real.** It
was 3.34x at 64 px because the edges quietly disappeared from the
accounting; it is 3.31x now because they quietly disappear from the
*frame*, on a rule that predates this workstream and that a user has
already accepted for memory pressure and for coarse rungs. The
difference between the two middle columns -- 2.08x and 3.31x -- is
what the contract is worth here: 601 k primitives at 64 px.

#### !! The wiring this obliges phase 4 to get right

The gate decides "shown" from the draws present in the frame, and a
proxied object contributes none:

    if (d.material.type == Render::Material::Line) {
        if (!objectsWithTriangles.count(d.objectKey)) {
            const bool late = incompleteObjects.count(d.objectKey) != 0;
            return late;          // not late => NOT gated => it DRAWS
        }
        const bool coarse = objectsCoarseFaces.count(d.objectKey) != 0;
        return dropLines || coarse;
    }

An object whose faces a proxy took has no face draw in the scene, so it
takes the first branch, reads as "absent, not late", and hits the
**display-mode exemption** -- the rule that stops a genuine Wireframe
or Points object from showing nothing. The edge would then draw, which
is the exact opposite of what the cut planned, and the frame would look
like 11.1g while the plan reported 11.1h.

The fix is not a new branch. A stopped node must register every object
it covers into **both** `objectsWithTriangles` and
`objectsCoarseFaces`: its faces *are* present and they *are* coarse,
which is true, and the existing rule then produces the right answer with
its existing order, its existing stagger and its existing on-top and
highlight exemptions.

#### What is left of the edge ceiling, and it is a draw-count question

Primitives are no longer where edges hurt: what still draws exactly
below a stopped node is 34 k primitives at 64 px, against the 601 k the
contract gates. But those 34 k arrive as **10813 draws of about three
primitives each**, and they are three quarters of the 14270 draws the
whole cut issues. They are the sets the contract calls floating -- and
`attachedOnly` is false *by default*, meaning "the producer has not
classified this", so an unclassified set and a genuine sketch line are
indistinguishable in the flag (13b records the same trap for the load
storm). Whether that population is really floating, or merely
unclassified, is the next thing worth measuring, and it is a question
about the producer rather than about the far field.

**MEASURED in 11.1i: they are genuinely floating.** The producer
judged them and found real free edges; omission is four occurrences.
What fails is not the classifier but its all-or-nothing granularity --
one free edge exempts a shape's whole edge set, and 71% of the
face-owning objects on screen are that mixed case.

### 11.1i Answered: the leftover draws are GENUINELY floating, and the assumption that fails is granularity

11.1h ended by asking whether the 10813 tiny draws a stopped node
leaves behind are really floating or merely unclassified, and said the
answer decides where the fix lives -- in the producer, or nowhere.
**They are genuinely floating.** The producer looked, and found real
free edges. The two roads to `false` by omission are measured, and
they are empty.

Three probes had to agree, because no one of them can answer it alone.

**1. The producer's own judgement** (`nothingFloats`, Render_LevelDebug,
counts are the last power-of-two logged so read them as "at least"):

| what the classifier said | times |
|---|---|
| EDGE/FACE **a real floating element** | **>= 8192** |
| EDGE/FACE ancestor map EMPTY -- cannot judge | 4 |
| VERTEX/EDGE a real floating element | 4 |

**2. What arrived at the bridge**, per node type -- which says whether
anything reached the renderer that PartGui never classified at all:

| drawable | state | times |
|---|---|---|
| SoBrepPointSet | attached | >= 16384 |
| SoBrepEdgeSet | attached | >= 8192 |
| SoBrepEdgeSet | **unattached/unjudged** | **>= 8192** |
| SoBrepPointSet | unattached/unjudged | 4 |
| IndexedLineSet | NO attachedOnly FIELD | 4 |

**3. The cut's own census** (`reportProxyFloating`), MiSTer Express,
converged, isometric fit, three runs agreeing to within two instances:

| tol | no faces at all | faces still exact | would gate | of those: bit FALSE | straddle |
|---|---|---|---|---|---|
| 4px | 6205 (39%) | 3104 (19%) | 6644 (42%) | 4998 | 3201 |
| 16px | 6205 (45%) | 1148 (8%) | 6411 (47%) | 5388 | 2041 |
| 64px | 6205 (50%) | 408 (3%) | **5826 (47%)** | **5532** | 592 |

Read together they close it. The unattached edge sets track the "real
floating element" count one for one; the unjudgeable empty map fires
four times, and the only non-PartGui line producer that reached the
renderer at all -- a plain Coin `IndexedLineSet`, which could never
have carried the field -- fires four times, so **omission is not a
measurable population**. Points are
almost entirely attached, so the residue is an edge story and not a
point story. And 5532 of the 5826 carry the bit `false` outright
rather than by inference -- the straddle case, an object with faces
below a stopped node whose own set the descent reached by another
route, is 592 and was counted precisely so it could not be mistaken
for a producer failure.

The census's own validity was checked rather than assumed, because
every row of it rests on a line draw pairing with its own faces
through `objectKey`, and two mechanisms could have broken that
silently -- the instanced path builds the face group and the edge group
as separate `SoFCSelectionRoot`s, and a merged draw-call entry is given
a synthetic key. Model-wide: **9563 distinct keys, 7647 own faces,
1916 own none**, and the 6205 "no faces at all" draws come from
exactly those 1916 keys. The pairing holds; the face-less objects are
real.

#### What actually fails is the ALL-OR-NOTHING rule, not the classifier

`nothingFloats` is deliberately per drawable, and says so:

> All or nothing per drawable, deliberately: objects are in practice
> either all floating or none, so a per-element subset would buy
> nothing measurable and cost an index permutation -- and the
> coordinate order is the picking identity.

**On this model that assumption is false.** The would-gate population
is by construction objects that own faces, and 5532 of them at 64 px
have an edge set marked floating -- so **5456 of the 7647 face-owning
objects on screen, 71%, are the mixed case the rule says does not
happen in practice**. One free edge in a shape exempts that shape's
entire edge set from the contract, and the far field then has to draw
all of it exactly.

So the fix 11.1h hoped for -- a producer that stopped short, costing
nothing to finish -- is not available. The lever that remains is
per-element granularity, and the producer has already priced it: an
index permutation, against a coordinate order that IS the picking
identity (`getCoordinateIndex() - startIndex + 1`). A per-edge attached
mask with the order left alone is the shape that avoids the
permutation, and it is a producer-side design question, not a
far-field one.

#### What it is worth, on the axis this workstream is gated on

!! **2.3% of primitives, 44% of draws.** The would-gate draws are
68096 primitives at 64 px against 2.91M visible. On the primitive axis
that 11.1b-prims re-gated everything onto, this is nearly nothing, and
by that reading 11.1h's residue is the floor and candidates 2 and 3 of
11.1g (merge line sets per cell, decimate as polylines) are the only
remaining levers -- which is what 11.1h predicted for this branch.

!! **But the reason draws were dismissed does not transfer to these
draws, and that should not be quietly inherited.** 11.1b-prims
dismissed draw count because the rack frame was GPU-bound and the GPU
was geometry-bound -- measured at roughly 1540 primitives per draw.
This population is **11.7 primitives per draw**, which is the opposite
regime: nearly pure per-draw overhead, where `docs/DrawSubmission.md`
measures 0.294 us/draw of our own C++ plus 0.859 us/draw of
`bgfx::frame`. Whether removing 5532 such draws is worth measurable
frame time on this scene HAS NOT BEEN MEASURED here, and the existing
"draws cost nothing" result is not evidence about it either way. That
measurement is the honest next step before anyone spends a session on
either lever.

**MEASURED in 11.1j: a draw is NOT free here.** Removing 8487 tiny
draws (2.5% of the primitives) took 21% off the frame, at 2.63 us per
draw -- and the submit and GPU components land within 3% of
DrawSubmission.md's independently fitted per-draw costs. The far
field's own addressable share extrapolates to ~14% of the frame.

#### A caveat on comparing these rows with 11.1h's

This session's converged scene is **2.91M visible primitives** where
11.1h's was 3.3M, and the cut correspondingly reads 5.38x net / 12700
draws at 64 px against 11.1h's 3.31x / 14270. Same model, same
viewport, same camera, and the run is deterministic -- 12 consecutive
report sets byte-identical, and three separate runs agreeing. The
difference is not explained, and most likely a different converged
mesh-ladder state; until it is, **the composition percentages above
travel and the absolute ratios do not**. Nothing in this section is
read across the two.

### 11.1j MEASURED: a draw is NOT free in this regime -- 8487 tiny draws are 21% of the frame

11.1i ended by refusing to inherit 11.1b-prims' dismissal of draw
count, on the grounds that it was measured at ~1540 primitives per
draw and the far-field residue is ~12. That refusal is now a
measurement, and **the dismissal does not transfer.**

`Render_TinyElementCutoff` suppresses every line and point draw at or
below N primitives, floating sets included -- which no existing gate
could do, the element contract never gating a floating set by design.
MiSTer Express, converged, real GPU (RTX 3060) over Xvfb, viewport
1607x788, `Render_DebugTiming` reading the engine's own frame line.
Run **A/B/A/B alternating**, not A-then-B: the ladder keeps working
and a single crossing would credit scene drift to the knob.

| phase | draws | prims | prims/draw | frame ms | submit ms | gpu ms |
|---|---|---|---|---|---|---|
| cutoff 0 | 16057 | 2030501 | 126 | 102.94 | 37.26 | 38.09 |
| cutoff 24 | 7570 | 1979367 | 261 | 84.02 | 27.25 | 27.29 |
| cutoff 0 | 16057 | 2030501 | 126 | 109.01 | 37.14 | 37.02 |
| cutoff 24 | 7570 | 1979367 | 261 | 83.33 | 27.09 | 27.13 |

The repeat is the control and it holds: draw and primitive counts are
identical within each setting, and the two crossings agree.

**Removing 8487 draws -- 2.5% of the primitives -- took 22.3 ms off a
106 ms frame, 21%.** The draws removed averaged 6.0 primitives each.
There is no geometry in that saving to speak of; it is draw count.

#### The per-draw cost, and why it should be believed

| axis | delta | per removed draw | independently measured in DrawSubmission.md |
|---|---|---|---|
| whole frame | 22.30 ms | **2.63 us** | -- |
| submit | 10.03 ms | 1.18 us | 0.294 (our C++) + 0.859 (bgfx::frame) = **1.15** |
| gpu | 10.35 ms | 1.22 us | **1.25**-1.72 us/draw |

The submit and GPU figures were arrived at here by removing draws from
a real scene, and there by curve-fitting a different model on a
different camera. They agree to within 3% and 3%. That is the
strongest reason to trust the headline.

!! The three rows are components measured on overlapping windows, not
addends -- `submit`, `gpu` and `bgfx::frame` all read close to each
other on this scene, and `cpu ours` reads 0.00 with `outside`
absorbing the whole frame in this configuration. **Do not sum them.**
The frame delta is the number.

#### What this is worth to the far field, and what it is not

!! **The cutoff prices the REGIME, not the fix.** It removes 8487
draws scene-wide; the far field's own addressable population is the
5532 sets of 11.1i whose faces a proxy already took and whose entire
edge set is exempt only because one edge in the shape floats. At the
measured 2.63 us/draw that is **about 14.5 ms, roughly 14% of the
frame** -- a linear extrapolation from the marginal cost, and flagged
as one.

The other half of the residue is not addressable and should not be
counted: the 6205 draws over 1916 keys that own no faces at all are
floating by construction, and the contract's ruling on those is not a
performance trade -- nothing else on screen would show them.

**What this changes in the plan.** 11.1b-prims' judgement that "a cut
that removes draws without removing primitives buys exactly nothing
here" is now known to be regime-specific, and the far field lives in
the other regime. Candidates 2 and 3 of 11.1g -- merge line sets per
cell, decimate as polylines -- are therefore worth their cost after
all, and so is the per-edge attached mask of 11.1i, which is the only
one of the three that removes draws with no change to what is on
screen. None of them is priced yet; this section says only that the
axis they act on is real, which is what 11.1i said had to be
established first.

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
- ~~**`K` is load-bearing in two directions.**~~ **Retired by
  measurement** (§11.1a). This had said the pop wants `K` small and the
  draw count wants it large, so the two might not be satisfiable
  together. The draw count also wants it small — a leaf that misses the
  tolerance dumps every one of its residents as an exact draw — so `K`
  may be chosen for the size of a pop and the cost of a forced descent
  alone. What is left of the risk is §9.1's, and it was already written
  there: small leaves mean many nodes, and many nodes are a generation
  and chunk-count problem.
- **Generation throughput** is a scheduler problem at the node counts
  fine grading implies (§9.1), not a loop.

## 12. Occlusion culling, as built

§10.4 decided this workstream and said what it had to be: cull on the
**CPU before submission**, **per index node** rather than per object,
and **per pass** rather than per frame. This section is what was built
against that, and what it measures.

Code: `Gui/Renderer/OcclusionCull.{h,cpp}` (the policy, plain float, no
backend), its wiring in `BGFXRenderer.cpp`, tests in
`tests/src/Gui/OcclusionCull.cpp`. Knobs: `Render_Occlusion` and the
five `Render_Occlusion*` tuning parameters, per `docs/RenderDebug.md`
§1 — a runtime property, not a recompile.

### 12.1 The mechanism

CHC++'s shape over the `ProxyHierarchy` of §3, with one deliberate
simplification. Per frame:

1. Read the answers the previous frame's tests produced. **Never
   blocking**: a query whose frame has not landed keeps its handle and
   is read next time, because stalling for it would trade the frame
   time this exists to save for a pipeline bubble.
2. Walk the index against the camera. A node the frustum rejects is
   skipped and left to the per-draw frustum mask that already exists. A
   node believed hidden has its **whole subtree masked and is not
   descended** — that is the entire economy of testing per node. A node
   believed visible draws its own residents and its children are asked
   in turn.
3. Offer tests: every hidden node (a test is its only way back) and
   visible nodes whose verdict has aged past `visibleTtl`.
4. Submit the offered boxes into `ViewOcclusionProbe`, the view that
   sits immediately after `ViewOpaque`.

The simplification is that queries are **not** interleaved with the
traversal to test against a partially filled depth buffer. Every test is
asked once, of the finished opaque depth. That is both simpler and the
only placement that answers correctly — asked at the end of the frame
the root itself reads hidden, because everything after the opaque
bucket rebinds or resolves the scene framebuffer (§10.2, and the
history of `RenderDebug_Occlusion`).

The mask is **additive into the frustum mask the renderer already
computes**, and only the eye passes consult it. That is what makes the
culling per pass without any new plumbing: shadow casters deliberately
re-submit culled draws (off-screen geometry still casts into view) and
the mirrored ground reflection never reads the mask at all.

### 12.2 What the design is defending against

Being wrong here is asymmetric. A node drawn when it could have been
skipped costs frame time; a node skipped when it should have drawn is
**missing geometry**, and the resulting frame is fast — which is
exactly how this failure disguises itself as a result. Three guards
follow from that, and each is a test in
`tests/src/Gui/OcclusionCull.cpp`:

- ⭐ **A hidden root is refused.** The root's box contains every drawn
  thing, so a frame that put one pixel on screen has a visible root.
  Acted on, it blanks the model. It is counted (`rootrefused`) rather
  than silently corrected, because a non-zero count means the box test
  is not answering, not that the scene is hidden.
- ⚠️ **Starvation reverts to visible.** A hidden node is re-tested every
  frame and the answer is its only way back, so if answers stop arriving
  — no query handles, a dropped batch — `maxHiddenFrames` returns the
  geometry. Ageing is measured from the last *answer*, never from the
  last offer, so answers that keep confirming a node is hidden keep it
  hidden indefinitely and nothing flickers.
- ⚠️ **Re-tests may not consume the whole budget.** They would otherwise
  starve discovery completely: the moment the number of hidden nodes
  reaches the budget, every slot goes to confirming what is already
  known and the culling freezes at exactly `budget` nodes however much
  of the model is in fact hidden. A quarter of the slots is reserved,
  and a cursor rotates the re-tests.

Verdicts never survive a rebuild of the index: node identity is
positional and stable, but node *indices* are not, and a verdict applied
to the wrong node hides geometry that was never tested. One frame of
drawing everything is the correct price.

Two kinds of draw are excluded from the index entirely, both already
exempt from frustum culling for the same reason: **autozoom** draws
rebuild their model matrix per frame, so their fed bounds describe where
they were, and **emitters** draw outside their own bounds.

### 12.3 The index build

The open cost §10.4 named. The index is partitioned from the draw list
and rebuilt **when that list changes**, never per frame — a scene
version stamped in `setScene`. On the measured assemblies the build is
12–28 ms, which is affordable per publish and would not be per frame.
The incremental index that §10.4 asks for is still owed; what this
change does is make its absence cost a publish rather than a frame.

### 12.4 measured (real GPU, monitor off, 1863x1064, both models)

Harness `~/works/sw/models/cull_probe.py`, medians over ~14 reported
seconds per phase. Phases A/B and C/D are the *same camera* with culling
off and on. The test boxes are draws too and are counted in the draw
column, so every figure below is net of them.

**Server assembly** (5455 objects, 17952 instances):

| | draws | submit | gpu | frame |
|---|---|---|---|---|
| whole assembly, off | 12849 | 15.9 ms | 21.7 ms | 204.2 ms |
| whole assembly, **on** | **630** | **3.0 ms** | **5.0 ms** | **53.4 ms** |
| camera inside, off | 1959 | 3.8 ms | 5.2 ms | 30.7 ms |
| camera inside, **on** | **284** | **2.5 ms** | **4.5 ms** | **29.8 ms** |

**MiSTer Express** (18142 objects):

| | draws | submit | gpu | frame |
|---|---|---|---|---|
| whole assembly, off | 41670 | 62.0 ms | 62.0 ms | 138.5 ms |
| whole assembly, **on** | **797** | **3.2 ms** | **3.3 ms** | **46.1 ms** |
| camera inside, off | 2441 | 6.0 ms | 6.2 ms | 38.0 ms |
| camera inside, **on** | **179** | **2.4 ms** | **3.1 ms** | **32.4 ms** |

Three things worth reading off these, before §12.5 takes most of it
back:

- ⭐ **The off rows reproduce §10.2 exactly** — 12849 draws at
  15.9/21.7 ms, and 41670 at 62.0/62.0 ms — which is what says the two
  runs are measuring the same thing that measurement did.
- ⭐ **Submit and GPU fall together**, by 5× and 4× on the server, 19×
  and 19× on MiSTer. That is §10.2's finding confirmed from the other
  direction: the draw call is the unit on *both* sides, so removing it
  removes both halves.
- ⚠️ **The frame does not follow the draws.** Draws fall 20× and 52×,
  frame time only 3.8× and 3.0×, and at the in-model camera the frame
  barely moves at all (30.7 → 29.8 ms) though the draws fall 7×. A
  floor of roughly 25-45 ms remains that is neither submission nor GPU
  — on the in-model rows, submit + gpu is under 8 ms of a 30 ms frame.
  **Culling ends the draw-bound regime and exposes a different
  bottleneck**, and that floor, not the draw count, is now the thing to
  measure. Note this also means the in-model camera — §10.3's working
  case — gets almost nothing from culling *today*, because it was
  already off the draw-bound part of the curve.

### 12.5 ⛔ measured: it deletes visible geometry, and the probe never said so

**The mechanism over-culls.** Rendering the server assembly twice from
one fixed converged camera, culling off then on, and comparing the two
images (`~/works/sw/models/cull_image.py`):

| | pixels differing | > 64 levels | worst |
|---|---|---|---|
| whole assembly | 22205 of 1.44 M (**1.54%**) | 8400 (0.58%) | 142 |

That is not antialiasing on a silhouette. Magnified, the two frames
show what it is: the large side panel survives, and the vertical rails,
the front brackets and the whole bottom row of modules — geometry
plainly *in front of* and beside the panel, not behind it — are gone.
Culling is currently fast **and wrong**, so `Render_Occlusion` stays
default off.

⭐⭐ **The important part is where the fault is not.** Exempting on-top
draws — the one candidate that was certain to be a real bug, since an
on-top draw ignores the depth test and is visible however occluded its
geometry is — moved the figure only from 1.5420% to 1.5165%. And the
partition is sound: a node's content bounds do contain its whole
subtree (asserted now in `tests/src/Gui/OcclusionCull.cpp`, and it is
the one assumption a box test cannot survive losing). What is left is
the box test itself, which the culler shares with
`RenderDebug_Occlusion` — `submitOcclusionBoxes` plus `sightBounds` and
`boxReachesNearPlane`.

⚠️⚠️ **So §10.3's headline is not trustworthy either.** The probe
measured 99.9% of the server's instances hidden at the whole-assembly
camera; acting on those same verdicts removes structure the eye can
plainly see. The probe was believed because it was internally
consistent, two-sided against a synthetic wall scene, and guarded
against the impossible-root case — and none of that could catch an
over-confident verdict, **because a measurement that does not act
cannot be checked against the picture.** The image comparison is the
check that was missing, and it should have existed in §10.3.

Where to look next, in order:
1. **What the depth buffer holds when the boxes rasterize.** The
   verdicts are over-confident in one direction only, which is the
   signature of testing against depth that contains more than the
   opaque scene the eye sees.
2. **The near-plane exemption's reach.** `rootpx` was measured at 0 on
   the wall smoke scene while the root was plainly visible — the
   impossible-root guard caught it, but a *non*-root node failing the
   same way is culled silently, and would look exactly like this.
3. Only then the policy in `OcclusionCull.cpp`, which the unit tests
   already cover and which the on-top result suggests is not at fault.

### 12.6 measured: three faults, and where the remaining one lives

§12.5 left an ordered list of suspects. Two of the three named there
were real, a third was found underneath them, and none of them was the
policy in `OcclusionCull.cpp`:

**1. The test boxes inherited a depth bias from the last line draw.**
The boxes were submitted with the mesh program, whose vertex shader
reads `u_params.w` as an NDC depth bias — and a bgfx uniform keeps
whatever the last draw that set it left in it. The last scene draw
before the probe view is routinely a *line* draw, where `u_params.w` is
not a bias at all but the on-top dim alpha, normally **1.0**. Inherited,
that pushes every test box a whole NDC unit away from the viewer, past
the far plane, so the box rasterizes nothing and the node reports itself
hidden however plainly it is in view. Three other mesh-program pairings
in `BGFXRenderer.cpp` already zero `u_params` for exactly this reason;
this one did not. The boxes now use the flat program, whose vertex
shader transforms the position and nothing else, and set `u_params`
explicitly anyway.

⭐ This is also why the synthetic wall smoke scene never reproduced the
failure: triangles only, no line draw, so the inherited bias there was
0. **A smoke test whose scene is simpler than the real one can be
two-sided, guarded and green while the mechanism is broken.**

**2. The box was not a box.** Corners are bit-encoded — x from bit 0, y
from bit 1, z from bit 2 — so a face's four corners run 0,1,3,2 around
its rim, not 0,1,2,3. The index table was written the second, natural-
looking way. The result rasterized **3.5 of the box's 6 faces**: the two
x-facing faces were absent entirely, replaced by two diagonal
cross-sections through the interior, and the two z-facing faces were a
quarter short each. It draws something from every angle, so it always
answered; what it answered with, over much of the box's footprint, was
an *interior* surface — deeper than the front face it stands in for, so
LEQUAL rejects it and the node reads hidden. Padding cannot rescue this,
which is why the residue survived the first fix. The table now lives in
`Render::occlusionBoxIndices()` and is checked as a property (every
triangle on a face plane, two per face, total area equal to the box's
surface) rather than transcribed at the call site.

**3. Padding in the model's units cannot answer a question about the
depth buffer's.** `padFraction` is a fraction of the box's own diagonal,
so a small part lying flush on a large panel gets a small pad — at a
distance where one depth step is far larger. Both surfaces quantize to
the same stored value, the tie goes whichever way the rasterizer rounds,
and the node reports itself hidden. `depthQuantumPad()` adds the missing
term: `Render_OcclusionDepthPad` steps of the 24-bit depth buffer,
converted to a world distance at the box's nearest corner
(`z^2/|P[14]|`). Both terms are needed and neither substitutes for the
other.

#### ⭐⭐⭐ And the measurement was reading a moving target

Fixing the above moved the differing-pixel count around without ever
settling — 1.54% → 0.75% → 0.48%, but also *worse* at a larger pad than
a smaller one, and a figure that would not reproduce between runs. The
control that explained it is the one that changes nothing:

| control | pixels differing |
|---|---|
| off → off, two captures 30 s apart | **0** |
| **on → on, two captures 30 s apart** | **14101 (0.98%)** |

**The culled image never stopped changing.** Two frames of the same
scene, same camera, same settings, thirty seconds apart, differed as
much as culling-off differed from culling-on — so every row of the pad
sweep was one sample of an oscillating system, and the ordering between
them was noise. The per-second readout confirms it directly: with the
index never rebuilt (constant 11.8 ms build), `instances hidden` swings
between **9303 and 17715** of 17727, and at the extreme **8 nodes hide
all but 12 instances**. That is the model blinking, not a static
over-cull.

⚠️ Generalise: **an off→on comparison cannot be read until on→on is
zero.** §12.5 paired the measurement with an image; it did not pair the
image with a control, and a moving target answers every question
plausibly.

#### The fault is re-testing a node whose own geometry is drawn

A node is tested by rasterizing its box against the opaque depth. For a
node that is *currently drawn*, that depth was written by the node's own
contents, and the test is a tie it can lose. Lose it and the node is
culled; culled, it stops writing depth, so the next test — now against
whatever lies behind — passes, and it comes back. Period-two
oscillation, one node at a time, hundreds at once.

`visibleTtl` is exactly the frequency of those re-tests, which makes it
the discriminator. Raised out of reach (10^6 frames), so that every node
is tested once and only nodes that are *not* drawn are ever re-tested:

| | pixels differing | instances hidden | drawn |
|---|---|---|---|
| on → on, 30 s apart | **0** | 15777 | 1950 |
| **off → on, same camera** | **0** | 15777 of 17727 | 1950 |

⭐⭐ **Pixel-identical to the unculled frame, and stable, while removing
89% of the instances.** The box test, with the three fixes above, is
sound: every node's first test is right. The entire remaining error is
in what re-testing does, and it is a property of the loop — the depth
buffer is built from the culled frame and the culling is derived from
the depth buffer — not of the box.

⛔ An unreachable `visibleTtl` is not the fix, only the proof: nothing
would ever be re-tested, so a camera that moves would freeze the culling
at whatever it discovered first. It errs safe (a stale *visible* verdict
draws geometry that could have been skipped; it never deletes any), but
it stops the mechanism from tracking. What is owed is a re-test that
cannot lose the tie against the node's own contents — the candidates
being to test against a depth buffer that includes the previously
visible set (CHC++'s actual arrangement, which this deliberately
simplified away in §12.1), or to exempt a node from re-test while its
own residents are the frontmost thing inside its box.

`Render_Occlusion` stays default off until that is closed.

### 12.7 measured: confirmations are not the fix, and why

§12.6 named the re-test loop as the open fault and hysteresis as the
first candidate: require `hiddenConfirm` consecutive answers of "no
pixels" before acting, on the theory that a verdict issued against one
frame's depth and read against a later one produces answers that
alternate. It is built (`Render_OcclusionConfirm`, default 2, with the
oscillator and the one-visible-answer-wins asymmetry under test in
`tests/src/Gui/OcclusionCull.cpp`) and it **does not fix it**.

The response surface, server assembly, same fixed converged camera, each
row an adjacent pair with the off→off control reading 0:

| confirmations | visible lifetime | on→on differing px | severe |
|---|---|---|---|
| 2 | 6 (default) | 13714 (0.95%) | 3960 |
| **6** | 6 | **6191 (0.43%)** | 1705 |
| 2 | **60** | **1666 (0.12%)** | 248 |
| any | 10^6 (never re-tested) | **0** | 0 |

Two things follow, and together they say what the fault is not:

- ⭐ **Damage is proportional to the number of re-tests.** Ten times
  fewer tests is roughly eight times less damage; no tests at all is
  none. Each re-test carries a small chance of a false "hidden" on a
  node whose geometry is drawn, and a false hidden persists, so the
  damage accumulates with the count of opportunities rather than
  settling anywhere.
- ⭐⭐ **The false answers come in runs, not singly.** Tripling the
  confirmations only halved the damage — nothing like the p^n an
  independent per-test error would give. A node that answers falsely
  once tends to answer falsely again for a stretch, which means it
  enters a *state* rather than catching noise, and hysteresis can only
  ever dilute that.

⇒ Both knobs reduce exposure and neither addresses the cause, so
neither is a shipping answer: `visibleTtl` traded high enough to matter
is `visibleTtl` too high to track a camera. What is still owed is the
per-test failure itself — the condition under which a node whose
residents are drawn reports no pixels, repeatedly — and the instrument
that finds it is the one this section did not build: for a node the
culler is about to skip, ask whether its residents actually contributed
a pixel, and report the disagreement. Every measurement so far has
compared *pictures*; this one would compare a verdict against the
geometry it claims to stand for.

`Render_Occlusion` stays default off.

### 12.8 what the field does, and the three options this leaves

Researched 2026-08-10, after §12.7 established that the fault is in the
re-test loop rather than in the box.

**The state of the art is closed to us for now.** Everyone has converged
on two-pass HZB culling with GPU-driven indirect draws: draw last
frame's visible set, build a depth mip pyramid, run a compute thread per
object against it, draw the survivors indirectly with no CPU round-trip.
NVIDIA's `gl_occlusion_culling` sample measures the payoff on 17576
objects — almost exactly this scene — at **5286 µs of CPU frame time
down to 494 µs** when the same culling moves from readback to
MultiDrawIndirect. It needs compute and indirect draws, which WebGL2
does not have, which is what §10.4 already decided.

⚠️ WebGPU is at roughly **85% global browser support** (Chrome 113+,
Safari 26+, iOS Safari 26+, Chrome Android 151+; Firefox still not on by
default), so that route is opening — but **not through this codebase**:
bgfx's WebGPU backend was re-implemented in January 2026 and is Dawn
*native only*, unusable through Emscripten, with its author's verdict
that WebGPU "isn't quite ready yet". Revisit in a year; do not design
around it now.

**The ID-buffer idea is a shipped technique.** NVIDIA calls it *raster
culling*: draw with colour writes off and let fragments passing the
depth test set `visible[objectid] = 1`. Fyrox ships a tile-based variant
with two details worth taking:

- ⭐ **Collapse each tile to one value with a logical OR before reading
  back**, so the readback is at tile resolution rather than pixel
  resolution. That answers §12.7's bandwidth objection without the
  sub-pixel loss plain downsampling would cause — an OR keeps anything
  that touched any pixel in the tile.
- ⭐ **Objects with no cached visibility default to visible.** The same
  asymmetry this design keeps enforcing by policy, made structural.

**And the literature already names our fix.** NVIDIA's *Temporal Current
Frame* method draws last frame's visible set first to prime the depth
buffer and only then tests the rest against it. Its stated purpose is to
draw each object exactly once; its side effect is that the occluder set
stops churning between a test being issued and its answer being used —
which is §12.7's fault exactly. This is the priming step §12.1
deliberately dropped. Note also that Unreal still ships per-actor
hardware occlusion queries as its *default* dynamic method, reads them
back a frame later, and documents the same popping under camera motion;
its HZB variant is explicitly *more conservative — fewer objects
culled*. Nobody claims the query approach is exact.

**The no-compute answer the field actually uses is a CPU software
rasterizer.** Intel's Masked Software Occlusion Culling (HPG 2016, open
source) culls **98% of what a full-resolution depth buffer would** at 3x
the speed of prior work, with very low memory — and, in its own words,
*"doesn't introduce any latency into the system"* and *"supports
interleaving occluder rasterization and occlusion queries without
penalty"*. Godot 4 ships this shape (occluders rasterized to a
low-resolution CPU buffer via Embree). ⭐⭐ Read those two clauses
against §12.6 and §12.7: **no latency means no stale verdicts, and
interleaving means no feedback between what was culled and what the
depth buffer holds. Both of our failure modes are absent by
construction rather than by policy.** It is SIMD, and WASM has SIMD128,
so it is the only option that behaves identically on desktop, mobile
and browser — which is what CLAUDE.md asks renderer work to preserve.
The cost is CPU time in a frame that is already CPU-heavy, against a
saving of ~12000 draws at ~1.2 µs each.

#### The plan this leaves, in order

1. **The per-instance ID debug mode**, whatever else happens: nothing
   else gives ground truth, and every option below has to be verified
   against something. Exact 24-bit `drawIndex + 1`, per *instance* (the
   granularity the cull mask uses), no MSAA, no shading.
2. **Add the priming step** to the mechanism that exists — draw the
   previously-visible set, test against that. Small, and it is the
   canonical remedy for the one fault still open.
3. **Then choose the oracle on measurement**: ID feedback (near-free if
   the ID rides as an MRT on the opaque pass, cheap to read back with
   the tile-OR, but keeps a frame of latency and leans on WebGL2
   readback, which is its weak point) against masked software occlusion
   (costs CPU, but deletes latency, feedback, the query pool and the
   whole policy layer, and is identical in the browser).

Given that this workstream's pain has been latency and feedback rather
than culling accuracy, the software rasterizer is the stronger long-term
bet and the ID buffer is both the near-term auditor and the thing that
proves whichever oracle wins.

### 12.9 measured: the audit, and what it corrects

Step 1 of the plan above is built: `RenderDebug_ViewMode = 11` renders the
per-instance id image (docs/RenderDebug.md §2.3b) and
`RenderDebug_CullAudit` reads it back and intersects it with the cull mask.
Harness `scripts/cull_audit.py` (it lived outside the repository
until section 12.15 promoted it); server assembly, real GPU with
the monitor off, 1863x1064, one fixed converged camera, each row reporting
**both** measurements of the same frames.

⚠️ The harness re-runs `ViewFit` *after* convergence. The first fit frames
whatever geometry had arrived, and a smoke run left the finished model
covering 3% of the viewport — which silently divides every coverage number
by thirty. Same family as the 400x300-reporting-1920x1200 trap of §10.2.

⚠️⚠️ **Every over-cull figure in this table is a single sample, and §12.10
shows the distribution behind it is enormous** — the ttl 60 row alone ranges
from 0 to 64712 px across one thirty-second window. Read the two zero rows,
which are stable by construction; do not read the last two as magnitudes,
and do not read the ordering between them.

| visible ttl | confirmations | instances hidden | audit: over-cull (⚠️ one sample) | picture: differing px |
|---|---|---|---|---|
| 10^6 | **1** | 818 of 17727 | **0 px, 0 rows** | **0** |
| 10^6 | 2 | **0** | 0 px | 0 |
| 60 | 2 | 5202 | 36629 px, 161 rows | 4555 |
| 6 | 2 | 15012 | 44415 px, 404 rows | 23514 |

#### ⭐⭐ The first row is the validation, and it is the point

818 instances masked, and *both* instruments independently report exactly
zero. An audit that inflated whenever anything was masked — the failure
mode that would make every other row worthless — would have shown it here
across 818 opportunities. It did not. Together with the culling-off control
(nothing masked ⇒ nothing reported) and the empty-image guard, that is what
licenses reading the rows below.

#### ⭐⭐ The correction: §12.7's last row hides nothing

That table's "any confirmations / ttl 10^6 / 0 px" row is 0 px **because
the culler removes nothing there**, not because it removes 89% of the model
exactly. With `visibleTtl` out of reach a node is never asked a second
time, and `hiddenConfirm` requires consecutive hidden answers — so at
confirm ≥ 2 no node can ever accumulate two, and the mechanism is
structurally incapable of culling. Measured: `instances hidden 0`, `tests
offered 0`. At confirm 1 the same lifetime hides 818 instances (4.6%),
still pixel-exact.

⇒ **The "pixel-exact while hiding 89% of instances" claim pairs a pixel
count from one configuration with a hidden count from another.** The
honest statement is the first row: a first-test-only policy is exact, and
it hides 4.6%.

#### ⭐ The audit is a strictly stronger claim than a picture diff

It reads 8x the picture at ttl 60 and 1.9x at ttl 6, always larger, always
moving the same direction. That is expected rather than contradictory: a
draw removed from in front of *another* draw of similar colour is a proven
violation of the invariant culling claims — that it removes only geometry
which could not have contributed a pixel — while barely moving a pixel. A
dense assembly of nested and coincident parts is mostly that case. The
picture measures how much the image changed; the audit measures how much
of what reached the screen was deleted, which is the thing being promised.

#### ⭐ The damage is a few large draws, and they now have names

At ttl 60 one draw (`Compound016`, 12845 px) is 35% of the total over-cull,
and the top five are 65% of it. Every earlier measurement could only say
"4555 pixels differ somewhere". This is the difference the instrument was
built for: the next question is answerable — why does *that* node's box
report no pixels while that draw is on screen — where "0.32% of the frame
changed" led nowhere.

#### ⭐ The headroom, free from the same histogram

With culling off, **16913 of 17727 drawn rows own no pixel at all (95.4%)**
— submitted, rasterized, and contributing nothing to the final image. At
~1.2-1.5 µs of CPU submission plus ~1.5-1.7 µs of GPU time per draw
(§10.2), that is the size of the prize, measured directly rather than
inferred from box tests. Even the most aggressive row above (ttl 6) still
leaves 2305 of 2715 surviving draws invisible.

#### What this does not settle

The per-test failure of §12.7 is still unexplained — the audit localizes it
to named draws but does not say why their nodes' boxes answer wrongly.
Steps 2 and 3 of §12.8 (the priming pass, then choosing between ID feedback
and masked software occlusion) are unchanged, and now have something to be
verified against. `Render_Occlusion` stays default off.

⇒ §12.10 revisits both: step 2 turns out to be already built, and the
over-cull figures above turn out to be single samples of a wide
distribution.

### 12.10 measured: which verdict deleted the row, and two corrections

The audit of §12.9 names the *draws* the culling wrongly removed. This step
makes it name the **verdict**: `cull()` fills a parallel `cullOwner` giving,
for every masked row, the node whose hidden answer cut it, and the readout
snapshots every node's visibility state with the id image the way the mask
is already snapshotted. `RenderDebug_CullAudit` prints a second line
(format in docs/RenderDebug.md §2.4e). Same harness, same server assembly,
same fixed converged camera, 1863x1064, real GPU with the monitor off.

It was built to close §12.7's open question — *the condition under which a
node whose residents are drawn reports no pixels, repeatedly*. It does not
close it. It produced two corrections instead, and both are worth more than
the answer would have been.

#### ⛔⛔ Correction 1: the obvious discriminator is a tautology

§12.6 explains the per-test failure as a **drawn** node losing a depth tie
against its own contents. The obvious confirmation is to record, per
verdict, whether the test that produced it was offered while the node was
being drawn — and the first version of this instrument did exactly that,
and reported **100.0% drawn / 0.0% hidden** on every row.

It has to. `mustTest` is filled inside `if (st.hidden)` and `mayTest` in the
visible branch, so **only an already-hidden node is ever offered from the
hidden set**, and the answer that *first* sets `hidden` can only ever be the
answer to a drawn-kind offer. The number is 100% for every scene, every
camera and every configuration — including all the ones where §12.6's
explanation is false. It is the same shape as the whole-model box reading
"hidden": an output that cannot come out any other way, wearing the
appearance of a strong result.

⚠️ Generalise, and it is the third time in this section: **before reading a
measurement, ask what its other outcome would have looked like.** §12.9's
validation row was licensed by exactly that question and passed it — 818
masked instances, 0 reported, where an inflating instrument had 818 chances
to show itself. This field could not have failed. It was removed rather than
reported, and replaced by what a lying query and a stale one actually differ
in: the **age** of the deciding answer, and how many times the node has
**entered** the hidden state.

#### ⚠️⚠️ Correction 2: §12.9's table quotes one sample of an oscillator

Re-running the same harness against the same build, camera and settings did
not reproduce §12.9's numbers, and not by a little. So the harness now reads
*every* audit line in the settle window instead of the last one:

| visible ttl | confirmations | over-cull px, per sample across ~30 samples | quoted in §12.9 |
|---|---|---|---|
| 60 | 2 | min **0** · median **572** · max **64712** | 36629 |
| 6 | 2 | min **0** · median **17525** · max **46056** | 44415 |

Framing is identical across all of it — the culling-off control reports
`163364 covered px` and `16913 of 17727 drawn-but-invisible` in every run,
to the pixel, which rules out the 3%-of-the-viewport trap and any difference
in what had loaded. Only *which frame was read* differs.

⭐ **The trap survived being moved to a better instrument.** §12.6's rule was
written about pictures — an off→on comparison cannot be read until on→on is
zero — and the audit exists because pictures name nothing. But the audit
samples the same oscillating system, so one audit line is worth no more per
sample than one capture was.

⭐⭐ And the damage it does is not merely noise around a true value: **ttl 60's
maximum (64712) is larger than ttl 6's (46056)**, while its median is thirty
times smaller. §12.7's "damage is proportional to the number of re-tests"
survives in the *median* and is invisible — even inverted — in a single
sample. That response surface was single samples.

#### What the attribution does establish

Summed over every sample in the window:

| visible ttl | over-cull from a verdict ≤2 frames old | from nodes that have flipped ≥3× | cut by the frustum, not occlusion |
|---|---|---|---|
| 60 | 205041 px (**99.5%**) | 47404 px (23.0%) | **0 px** |
| 6 | 481752 px (**94.1%**) | 437956 px (85.5%) | **0 px** |

- ⭐⭐ **The over-cull is not stale verdicts.** 94–99.5% of it comes from
  answers less than two frames old, against a camera that never moved. The
  "the query was right when it was taken and the world moved underneath it"
  explanation needs time it did not have ⇒ **the query is answering wrongly**,
  which is the per-test failure §12.7 could not establish from pictures.
- **The frustum is clean.** Not one over-culled pixel in any row belongs to
  the other contributor to the same mask, so nothing here is a fix waiting to
  be credited to the wrong mechanism.
- **Flipping tracks the re-test rate** — 23% of the damage at ttl 60 against
  85.5% at ttl 6, with the worst nodes entering the hidden state 16–22 times
  in a thirty-second window. §12.7 inferred "the false answers come in runs"
  from the shape of a confirmation sweep; this measures it as a state.
- ⭐ **The cost of a wrong verdict scales with where in the tree it is taken,
  and the evidence required does not.** At ttl 6 the worst node is `L1
  res12 sub1938` and owns 17305 of 25947 px — 67% of the row from one
  verdict — and the top four all stand for ≥1938 instances at levels 1-3. A
  node speaking for two thousand instances needs exactly the same two
  confirmations as one speaking for twelve.

#### ⭐⭐ And the priming pass is already what this renderer does

Step 2 of §12.8 — *draw the previously visible set first, test against that*
— was the next thing to build. It has been built since §12.1. The mask is
computed at the top of the frame from the verdicts in hand; the opaque pass
draws the set that survives it, which **is** the previously visible set; and
the boxes are rasterized against exactly that depth, because
`ViewOcclusionProbe` sits immediately after `ViewOpaque`/`ViewSelection` in
the view order and transparent geometry writes no depth.

What NVIDIA's *Temporal Current Frame* adds beyond that is not a different
depth buffer. It is a **second draw pass inside the same frame** for the
objects the test brings back — which needs the query resolved without a CPU
round trip (compute plus indirect draws, which WebGL2 does not have, §10.4),
and which fixes objects appearing a frame *late*. That is under-draw. This
fault is over-cull. ⇒ **Priming would not have moved it**, and building it
first would have cost a session to find that out.

#### What is still owed

Why a box that bounds drawn, visible residents returns zero samples is still
unexplained. What the attribution adds is where to stop looking: the failure
is in **re-tests of nodes that are currently drawn** — the one configuration
in which a hardware query cannot be asked at all without the node's own
contents already in the depth buffer it will be answered against, because
the query happens after the pass that wrote them. That is not a property of
the box, the padding or the policy but of *when* the question can be asked,
and it is what §12.8's remaining option removes: Intel's masked software
occlusion culling advertises interleaved occluder rasterization and queries,
which is what lets a node be asked *before* its own geometry is added — what
CHC++ gets from front-to-back traversal and an end-of-frame hardware query
cannot have at any padding.

⛔ Named so it is not mistaken for a fix: the damage concentrates in nodes
with large subtrees, so requiring evidence in proportion to what a verdict
would delete would cut the worst of it. That is exposure reduction of the
kind §12.7 already measured and rejected — hysteresis dilutes a failure that
comes in runs; it does not remove it.

`Render_Occlusion` stays default off.

### 12.11 measured: the query handles were misused, and that was not the fault

§12.10 left one hypothesis ahead of every other, and it was an API misuse
rather than anything about boxes or depth. It has now been fixed and
measured. ⛔ **It is not the fault.** The fix is worth keeping — it is a real
defect, and it is what makes the rest of this section provable — but the
over-cull it was supposed to explain is still there.

#### The misuse, which was real

**A bgfx occlusion query handle is an object's identity, not a slot to
rent.** In the vendored source, `m_occlusion[idx] = INT32_MIN` — the value
`getResult()` reports as `NoResult` — is written in exactly one place:
`createOcclusionQuery()`. Nothing resets it on submit, not the frame swap
and not the backend's own `begin`. So **after a handle's first result lands,
`getResult()` never returns `NoResult` again**; it returns whatever is in
the slot. bgfx's own guidance says the same thing — *"you should always keep
the same occlusion handle for the same mesh/object"* (bkaradzic,
[discussion #2498](https://github.com/bkaradzic/bgfx/discussions/2498)) —
and `examples/26-occlusion` allocates one handle per cube at init and
indexes it by object forever.

The culler did the opposite: 128 anonymous handles, reassigned per frame as
answers were consumed. A handle whose new query had not yet resolved
therefore still held the **previous occupant's** verdict, and the standard
"if `NoResult`, keep the slot and read it next frame" guard could never fire
to catch it. Silently, too — `BGFX_CONFIG_DEBUG_OCCLUSION` asserts only that
a handle is not used twice within a *single* frame; cross-frame reuse trips
nothing.

⭐ It also fitted the evidence better than anything else on the list:
pixel-exact at ttl 10⁶ (the pool cycles slowly), damage rising with the
re-test rate (= the recycle rate), false answers arriving in runs, verdicts
fresh against a camera that never moved, and the damage weighted by what the
*receiving* node deletes rather than by anything about the node that was
tested.

#### What was built

One handle created per test and destroyed when its answer is read, for the
culler and for the §10.1 probe both. `NoResult` now means precisely "this
query has not landed", and nothing else can have written the slot, because
creating a handle also invalidates any in-flight query still holding that
index. Three consequences of making the guard work, none of them optional:

- **Leases have to expire.** A query that never lands would now hold its
  handle forever and leave its node `pending`, the one state the walk never
  re-offers. While handles were pooled, nothing ever *looked* stuck, because
  a lost query still read as answered — with somebody else's answer.
- **Leases have to be dropped on an index rebuild.** A lease names a node by
  index and a rebuild renumbers them; the culler's own rule is that verdicts
  never survive a rebuild, and the questions in flight are no different.
- **The handle budget has to grow.** bgfx defers the free to the end of the
  frame, so the live count is the tests in flight plus a frame's worth of
  released ones. `BGFX_CONFIG_MAX_OCCLUSION_QUERIES` goes 256 → 2048, and
  both consumers now size their appetite from
  `caps->limits.maxOcclusionQueries` (culler ≤ ½, probe ≤ ¼) so that a
  backend built without it culls *less* instead of answering wrongly.

The probe's wait was a no-op for the same reason and got the same fix: after
its first cycle none of its reused handles ever reported `NoResult` again,
so it read each batch before the batch had answered. ⚠️ Not exercised by the
run below, which drives only the culler — the §10.1/§12.4 probe numbers were
taken with that defect present and have not been re-taken.

#### ⛔ measured: the medians do not collapse

Same harness, same model, same camera, same two rows, medians over a ~30
sample window (`scripts/cull_audit.py`, `FC_ROWS="60/2,6/2"`):

| visible ttl | over-cull px, §12.10 | over-cull px, with leases |
|---|---|---|
| 60 | min 0 · med **572** · max 64712 | min 0 · med **237** · max 49279 |
| 6 | min 0 · med **17525** · max 46056 | min 0 · med **16924** · max 79530 |

The gate §12.10 set was the medians, and **ttl 6 is flat**: 17525 → 16924, a
3.4% move on the row with thirty times the damage and by far the steadier
signal. ttl 60's median falls 2.4×, but it is a median of a 0–49279 spread
in one window, and its maximum stays the same order — the exact quantity
§12.10's Correction 2 warned cannot be read from one window. ⇒ **No material
change.** The control is unchanged to the pixel (`163364 covered px`,
`16913 of 17727 drawn-but-invisible`, over-cull 0 across 29 samples), so
this is not a framing difference.

The attribution is also unchanged in character: **97.2%** of the ttl 6
over-cull comes from verdicts ≤2 frames old (§12.10: 94.1%), 92.1% from
nodes that have flipped ≥3× (85.5%), and **0 px** from the frustum.

Handle accounting across both rows: `expired 0 refused 0`, with the culler
sitting at `inflight 128 held 128` once ttl 6 saturates its budget. So the
new machinery is not itself losing tests, and the pool is not short.

#### ⭐⭐ What this buys, which is not nothing

The negative result is worth more than the hypothesis was, because it
removes the last way to explain the central observation away:

> Every worst node reports `lastpx0 age1f` — a box that rasterized **zero
> samples**, answered **one frame ago**, for a node whose contents are on
> screen in the id image.

Before the leases, that zero could always have been somebody else's zero,
read out of a recycled slot. It cannot be now: the handle was created for
that box and destroyed after that read, and no other query can have written
it. **The box genuinely returns no samples against the depth buffer while
what it bounds is visible.** That is §12.6's account — a node re-tested
after the pass that wrote its own contents loses the depth comparison
against itself — now standing on a measurement instead of an inference, and
it is a property of *when the question can be asked*, which no amount of
padding, hysteresis or freshness policy reaches.

⇒ §12.8's step 3 is no longer deferred, and the choice it offered is
settled: **Intel's masked software occlusion rasterizer**, whose whole point
is that occluders and queries interleave, so a node can be asked *before*
its own geometry joins the depth buffer. The ID-feedback alternative is not
a candidate — it answers the same question at the same moment in the frame.

`Render_Occlusion` stays default off.

### 12.12 built: the oracle moved to the CPU

Section 12.11 settled the choice and this builds it: Intel's masked software
occlusion structure, as `Gui/Renderer/MaskedOcclusion.h`, driving the
same `ProxyHierarchy` index the hardware path walks.
`Render_OcclusionSoftware` selects it; `Render_Occlusion` still gates
both and is still default off.

#### What the structure is

Per 8x4 block of pixels: **two depth values and a 32-bit coverage mask**
saying which of the two each pixel belongs to. 12 bytes per 32 pixels, so
a full 1863x1064 buffer is ~744 KB rather than the 8 MB of a real depth
buffer. The second layer is what makes it beat a plain hierarchical-Z
minimum -- a single conservative minimum per block is destroyed by one
distant fragment, where a partially covered block can hold the incoming
surface separately until its coverage completes and it is promoted to the
block's floor.

Depth is stored as a quantity that is **affine in screen space and larger
when nearer**: 1/w under a perspective projection, -z_ndc under an
orthographic one. Both are affine, which is what lets a triangle's depth
over a block be a plane equation rather than a per-pixel divide, and
having both means nothing downstream asks which projection it is looking
at.

#### KEY: The one invariant, and why it is one-sided

> For every pixel, the depth stored is **no nearer** than the true
> nearest surface there.

Every heuristic in the merge may throw occlusion away and each of them
does; none may invent it. Kept, the mechanism can only fail by *drawing
something it could have skipped* -- and over-culling is the entire failure
history of section 12.5 through section 12.11. So the tests assert an inequality
against an independently written full-resolution depth buffer rather than
comparing an image: 27 cases, including random scenes checked pixel by
pixel, and the query-level form of the same claim (a box reported hidden
must have every pixel of its rect already covered by something nearer in
the reference).

WARNING: Two of them exist only because a passing one-sided test is also what a
buffer that rasterized *nothing* produces. `expectConservative` returns
the pixel count it checked and every caller asserts on it, and the pass
has a control row -- the same scene with the occluder removed must cull
**nothing at all**. That is the control section 12.5's image comparisons lacked
and section 12.9 had to correct after the fact.

#### KEY: What disappears, which is most of the value

The software walk keeps **no state between frames** and has **no policy
layer**:

| the hardware path needs | why | software path |
|---|---|---|
| `hiddenConfirm` streaks | verdicts arrive stale | -- |
| `visibleTtl` | re-test frequency vs. cost | -- |
| `maxHiddenFrames` fail-safe | answers may stop arriving | -- |
| query pool, leases, expiry | handles are object identity | -- |
| `padFraction` + `depthPadLsb` | box must beat its own surface | -- |
| `budget`, `offercursor` | tests are a scarce resource | -- |

Every one of those exists to survive an answer that arrives one to two
frames after the question. The answer here is used where it is computed.
The padding goes for a different reason worth stating separately: a tie
answers **visible** by construction (`testRect` compares strictly), and
the block floor is already a conservative under-estimate -- so a node
whose own geometry is the only thing in the buffer *cannot* hide itself,
which is exactly the failure section 12.6 diagnosed and section 12.11 measured.

#### Occluders are real triangles, chosen by screen size

Only draws that write the depth the eye sees: opaque triangle draws, not
transparent surfaces, not on-top overlays, not lines, and **not
stand-ins** (a stand-in does write depth, so it genuinely occludes the
frame it appears in -- but it is larger than the mesh it replaces, and
under-culling for the few frames it is up is the cheaper mistake). They
are ranked by projected bounding-box diagonal and rasterized largest
first until `Render_OcclusionOccluderTris` runs out, so what the budget
drops is what would have hidden least -- and the drops are **counted**,
because a silent cap reads as "this scene does not occlude" when what
happened is "we did not look".

WARNING: **`Render_OcclusionResolution` defaults to 1 and reducing it can
over-cull.** A coarse pixel is marked covered when an occluder reaches
its centre, but it stands for several real pixels and the ones the
occluder missed are claimed with it. The literature runs reduced and
accepts this; given three sections spent on deleted geometry, here it is
a measurement and not a setting. Correct reduction needs coverage sampled
over the coarse pixel's whole footprint, which is not built.

#### KEY: measured: exact, and too expensive

Real GPU, monitor off, 1863x1064, `server_imported.FCStd` (5455 objects,
17727 drawn instances), whole-assembly camera, 29-30 samples per row,
both rows in the same run against the same framing.

| | software (section 12.12) | hardware, ttl 6 confirm 2 |
|---|---|---|
| over-cull, median of 29 | **0 px** | 21212 px |
| over-cull, min / max | **0 / 0** | 0 / 90458 |
| over-culled rows | **0 of 7974 masked** | 423 of 14607 |
| picture, off->on | **0 of 1440000 px** | 34702 px (2.41%) |
| instances hidden | 7974 (45.0%) | 14607 (82.4%) |
| nodes hidden / visited | 203 / 760 | 87 / 364 |
| CPU per frame | **raster 21-29 ms**, walk 0.44 ms | -- |

KEY: **The correctness claim holds, and this is the first configuration in
this section that is both exact and actually culling.** section 12.9's
correction was that the one pixel-exact row hid *nothing* -- it was exact
because it was structurally incapable of culling. This one deletes 7974
of 17727 instances and still differs from the un-culled image in **zero
pixels**, over 29 samples, with `rootrefused 0` and `nearclip 0`. The
audit and the picture agree, which they did not for any hardware row.

STOP: **And it does not pay.** Removing 7974 draws saves ~10-12 ms of CPU
submission at section 10.2's 1.2-1.5 us; the occluder pass costs 26 ms of it.
Net CPU loss of ~14-16 ms per frame, every frame, and the variance is
small (21.3-28.9 ms across 30 samples) so this is the cost and not a
sampling artefact. **The walk is free** -- 0.44 ms for 760 node tests,
stable to a hundredth of a millisecond -- so *all* of the cost is
rasterizing occluders and none of it is the mechanism.

#### KEY: Why it costs that, which is not "scalar code"

Two numbers from the same row say it, and they point the same way:

- **68% of the rasterized triangles never touch a pixel.** 249998
  triangles submitted, **79314 drawn, 170684 culled** -- sub-pixel or
  off-buffer, discarded after paying for their transform and screen-space
  setup. A CAD tessellation at full detail is mostly triangles smaller
  than the pixel grid it is being rasterized onto.
- **97% of the candidate occluders never got in.** 1322 draws qualified;
  the 250k triangle budget was consumed by **37 of them**, at ~6757
  triangles each, and **1285 were dropped**. Admitting them all at that
  detail would be ~8.9M triangles.

So the buffer is simultaneously *too detailed* (two thirds of the work
discarded) and *too incomplete* (most of the model's depth missing) -- and
the incompleteness is why it hides 45% where the scene's ceiling is
95.4%: `drawn-but-invisible` was still **8939 of the 9753** instances it
left drawn. That gap is not a limit of the mechanism; it is occluders
that were never rasterized. KEY: The `occludersDropped` counter earned
itself here: without it this row reads as "the software oracle culls half
as well", when what it says is "it was shown a twenty-seventh of the
model".

WARNING: Note what this rules out. Vectorizing would attack the 26 ms by some
constant -- SIMD128 is 4 lanes, so at absolute best ~6.5 ms -- while
leaving both ratios exactly as they are. It is the wrong lever to pull
first.

#### KEY: measured again: parallel, and what the cost actually is

The 26 ms above was one thread and a rasterizer that paid full setup for
every triangle. Both were wrong to leave, and fixing them moved it a
long way -- same model, same camera, same 1863x1064, ~30 samples a row:

| | workers | raster | hidden | over-cull |
|---|---|---|---|---|
| as first measured | 1 | 26 ms | 7974 | 0 px |
| reject-first, serial | 1 | 21.5 ms | 7974 | 0 px |
| + 14 workers, whole draws | 14 | 14.0 ms | 7974 | 0 px |
| + chunked draws | 14 | **9.73 ms** | 7974 | 0 px |

KEY: **Every configuration is still pixel-exact.** Reject reordering,
reciprocals, fourteen-way parallelism and a lossy shard merge, and the
audit still says 0 over-culled rows and 0 px over ~30 samples in each.

#### STOP: Two predictions, both wrong, both corrected by the clock

Worth recording as method rather than as result, because this section
has now made the same class of mistake three times:

1. **"Removing 7974 draws saves 10-12 ms."** Estimated from section 10.2's
   1.2-1.5 us per draw. The frame log had the real answer: submit
   30.9 -> 26.0 ms, i.e. **4.9 ms**, with the GPU essentially unmoved
   (38.5 -> 38.0). Out by 2x. => *Never price a saving from a per-draw
   constant when the frame timer is already running.*
2. **"The serial merge is the bottleneck."** Parallelizing it bought
   1.7 ms of 15.7. The real cause was granularity -- 37 draws over 14
   workers, differing in triangle count by an order of magnitude, so the
   frame waited on the largest single mesh. Chunking the index range
   took it to 9.73 ms. => *Amdahl's residual names a quantity, not a
   culprit; it took splitting the counter to find which.*

#### KEY: Where the time goes, now that the counter says

`offbuf 0, subpx 170684, degen 0` -- of 249998 triangles rasterized,
79314 are drawn and **every single discarded one is sub-pixel**. None
are off the buffer, which in hindsight is forced: occluders are selected
*by projected size*, so they are all on screen by construction. The
clip-space outcode reject added for them therefore buys nothing here and
is kept only for cameras that do put an occluder off screen.

So two thirds of the pass is transform, project and reject on triangles
smaller than a pixel -- a uniform, branch-free workload. That is the
shape SIMD is for, and it is also exactly what coarser occluder geometry
would delete outright rather than merely speed up.

#### The order to try things in

KEY: **Decided (2026-08-10): SIMD first.** The ordering below had coarse
occluders ahead of it, on the argument that deleting the sub-pixel work
beats accelerating it. That still holds as an argument; the decision
went the other way, and the counter split is what makes it defensible --
the 68% is now a *measured*, branch-free, uniformly-shaped workload
rather than a guess about where the time goes.

1. KEY: **SIMD the transform and projection.** Well targeted rather than
   speculative: it is 68% of the work, branch-free, and the block
   layout was built for it (8x4 blocks, coverage exactly one 32-bit
   word, four to a 128-bit lane). WARNING: **The blocker is precision, and it
   is self-inflicted**: this file is `double` throughout because a
   near-plane-clipped triangle projects to screen coordinates in the
   millions and float cancellation there sets coverage bits the triangle
   never reached. SIMD128 holds 2 doubles but 4 floats, so the 4x needs
   a float fast path with a guard band and a double fallback for clipped
   geometry. The measurement supports it: **clipped 0** on this camera.
   WARNING: The gate is not speed: over-cull must stay at **0 px**. Every
   configuration measured so far is exact, and a float path that costs
   even a few pixels is a regression rather than a trade.
2. KEY: **Coarser occluder geometry.** Deletes the sub-pixel work instead
   of accelerating it, and admits far more occluders inside the same
   budget. WARNING: Must be an *inner* hull: a decimation that moves a surface
   **towards** the camera invents occlusion and breaks section 12.12's
   invariant, which error-minimising decimation (`MeshSimplify.cpp`)
   does not promise. WARNING: And note the ablation above -- 40x the budget
   bought 8.5 points of culling -- so this is about *cost*, not about
   closing the 45%-vs-95.4% gap.
3. **Wire section 12.13's estimator into the renderer.** Built and tested, not
   yet connected: it needs the per-frame CPU time and to gate the mask.
4. **Reuse the buffer while the camera is static.** Exact, and does not
   reintroduce section 12.6 (that was intra-frame ordering, not a fixed camera).

`Render_Occlusion` stays default off, and `Render_OcclusionSoftware`
with it.

### 12.13 built: deciding by experiment whether to cull at all

Everything above prices one camera on one model. Nothing about it
transfers: what a draw costs to submit depends on its mesh, how much a
frame occludes depends on whether the eye is inside a chassis or looking
at a silhouette, and what the occluder pass costs depends on how many
triangles the occluders carry. Section 12.12 measured the same mechanism saving
4.9 ms while costing 26 ms to decide, and then 9.7 ms after two rounds
of work -- the *sign* of that trade changed under optimisation, on a
fixed scene.

`CullBenefitEstimator` therefore decides it at runtime, per scene, and
re-decides as the scene changes. Guthe et al. (2006) reached the same
conclusion for hardware queries and answered it the same way: measure
the machine rather than assume it, because occlusion queries "may still
reduce efficiency compared to simple view frustum culling, especially in
cases of low depth complexity".

KEY: **It models nothing.** There is no microseconds-per-draw constant,
no occlusion-probability estimate and no calibration table -- reasoning
from a per-draw constant is exactly how the saving got estimated at
10-12 ms when the clock said 4.9. It runs an A/B experiment on real
frames: alternate arms, discard the warm-up after each switch (the
switch itself perturbs the frames that follow it), compare **medians**
over 24 frames, re-probe every 20 s.

WARNING: **Two thresholds, not one.** Turning culling on demands a 5% gain;
leaving it on needs only 1%. A single threshold at the noise floor flips
every probe, and a mechanism that rebuilds the draw set every twenty
seconds is worse than one that never culls -- section 12.7 measured what
oscillation costs here. Having established nothing, it does not cull:
the untested direction has to be the one that draws too much.

11 tests, timings fed in directly -- no GL context, no scene, no clock.

### 12.14 built: the vector pre-pass, and why a float path is admissible

Section 12.12 ended with a decision -- SIMD before coarse occluders -- and a
blocker: `MaskedOcclusion.cpp` is `double` throughout because a
near-plane-clipped triangle projects to screen coordinates in the
millions, where float cancellation in the edge equations sets coverage
bits the triangle never reached. SIMD128 holds four floats but only two
doubles, so the 4x needs float, and float is the one thing that file had
argued it could not have.

KEY: **The way out is structural, not numerical.** The vector code does not
rasterize. It transforms and projects four triangles at a time in float,
and its only output is a verdict per lane: *this triangle covers no
pixel* -- discarded there -- or *anything else*, in which case the
triangle is handed to the identical double path as before, recomputed
from the original vertices. So the float arithmetic decides **how much
work is skipped and nothing else**:

- a lane it gets wrong in one direction wastes the exact path's time;
- a lane it gets wrong in the other loses one sub-pixel triangle's
  occlusion, which is under-culling, which this mechanism is always
  allowed to do;
- and there is no third direction, because the float path never writes
  to the buffer.

That is why the precision question that blocked SIMD does not arise: the
guard band, the epsilon and the backend's rounding are all *culling
quality* parameters. Near-plane crossings and anything projecting beyond
the guard band are handed over unjudged and counted (`guarded`).

WARNING: **128 bits, and no runtime dispatch** (`Gui/Renderer/Simd4.h`). AVX2
would double the desktop throughput and split the browser tier onto a
different code path, and a software occlusion buffer that behaves
differently in Chrome is not the mechanism measured here. SSE2, NEON,
WASM SIMD128, and a scalar fallback that gives the same answers.

#### measured: the stage, isolated (synthetic, one thread)

250000 triangles into a 1863x1064 buffer, best of 7 runs on an idle box,
composition varied deliberately to separate the two effects. Run-to-run
spread is about 3%, and the whole table was taken twice:

| scene | scalar | vector | |
|---|---|---|---|
| all sub-pixel -- the reject stage alone | 13.49 ms | **3.35 ms** | **4.03x** |
| none sub-pixel -- survivors only | 81.41 ms | 84.09 ms | **+3.3%** |
| 68/32, the benchmark's composition | 36.71 ms | 30.97 ms | **-16%** |

KEY: Read the three rows together, because the middle one is the price of
the top one. The pre-pass hits its theoretical ceiling exactly -- four
lanes, 4.03x -- on the work it was built for. Survivors pay for it
twice, being transformed once in float to be judged and once in double to
be drawn, and that costs 3.3% (about 11 ns per surviving triangle). The
mixed row is what the machine does on this synthetic scene.

WARNING: **-16%, not -75%, and that much of the gap is not a
disappointment -- it is the answer to a different question than the one
section 12.12 asked.** The 68% figure is a share of *triangles*, and the
pre-pass makes that share nearly free; but a drawn triangle costs far
more than a rejected one, so 68% of the triangles were never 68% of the
milliseconds. Sizing a speedup by a population count is the same error as
pricing a saving by a per-draw constant (section 12.12, twice), in a
different currency.

#### measured: on the real model

`server_imported.FCStd`, 5455 objects, 1863x1064. WARNING: **Read from the
distribution over the window, not from the last line.** The first
version of this section quoted -5.4%, off one sample per arm, and one
sample of this quantity is worthless: two runs of an *identical*
configuration reported 9.00 ms and 4.31 ms. The audit now reports
min/med/max of every timing field over its ~30 frames, for the same
reason it already did for the over-cull pixels.

| row | raster min | raster med | over-cull |
|---|---|---|---|
| 1 worker, scalar | 22.77 ms | 27.35 ms | **0 px** |
| 1 worker, vector | **18.25 ms** | **23.50 ms** | **0 px** |
| | **-19.8%** | **-14.1%** | |
| 14 workers, scalar | 5.03 ms | 7.73 ms | **0 px** |
| 14 workers, vector | **4.27 ms** | **6.19 ms** | **0 px** |
| | **-15.1%** | **-19.9%** | |

Composition identical to section 12.12's: 249998 triangles offered, 79314
drawn, `offbuf 0 subpx 170684 degen 0`, `clipped 0`. Of the 170684
discards the pre-pass judged **164568** and declined **none**
(`guarded 0`).

KEY: So the real model agrees with the synthetic scene after all --
15-20% against 16% -- and the "unexplained discrepancy" this section
was first written around was the instrument, not the mechanism. It is
left in the record because the mistake is the reusable part: **a
measurement whose spread is 2x cannot report a 5% effect**, and nothing
about the readout said so until the spread was printed beside it.

#### KEY: what the counters prove, and it is stronger than a tolerance

Across the synthetic runs -- 750000 triangles -- and again on the real
model, `trianglesDrawn` and `blocksUpdated` are **identical** in both
arms. Not close: equal. On `server_imported.FCStd` that is 79314 drawn
and 154134 blocks with the pre-pass on and off, the same 7974 instances
hidden by the same 203 nodes, **0 px over-culled** by the audit and
**0 of 1440000 pixels** different in the picture. Every triangle the
rasterizer would have drawn reached it, and the buffer it produced is
the same buffer.

The only movement anywhere is three synthetic triangles that the exact
path classified `offbuf` and the pre-pass classified `subpx`, which is
the epsilon-grown bounding box landing just inside the buffer edge; both
buckets are discards and neither reaches a pixel.

That is also the shape of the unit tests. The central one is not a
tolerance but an equality: same scene, pre-pass on and off, every pixel
of the two buffers compared exactly, over eight random scenes of mixed
scale. Plus: partial batches (a mesh is not a multiple of four -- the tail
must be rasterized, not dropped), near-plane crossings handed over rather
than guessed at, the lane-mask bit order, and floor rounding towards
minus infinity on the two backends that have no instruction for it.
37 tests.

### 12.15 measured: what the occluder pass is actually made of

Two sessions running had improved a component of this pass and found the
frame barely moved, so the pass got timed by phase instead of by
argument. `MaskedCullStats` now carries `selectMs`, `shardMs`, `mergeMs`,
the *slowest worker's* share of each, and the sum of every worker's
rasterization -- reported, so that no term has to be attributed by
subtraction.

#### The decomposition, 14 workers, vector pre-pass on (min over 30 frames)

| term | ms | share |
|---|---|---|
| **raster, total** | **4.27** | |
| select -- size, clear, project every candidate's bounds, sort, cut budget | 0.22 | 5% |
| phase 1 wall -- shard clear + rasterize | 3.56 | 83% |
| ... of which the slowest worker's rasterization | 2.96 | 69% |
| ... of which the slowest worker's shard clear | 0.05 | 1% |
| ... leaving spawn, join and scheduling | ~0.55 | 13% |
| phase 2 wall -- merging 14 shards | 0.43 | 10% |
| ... of which the slowest worker's merge range | 0.12 | 3% |

KEY: **There is no missing term.** The pass is the rasterization, plus
about 0.9 ms of thread spawn/join across two phases and 0.4 ms of merge.
The buffer clear that section 12.12 worried about is 0.05 ms; the candidate
selection that projects 1322 bounding boxes is 0.22 ms. The earlier
"2.4x out of 14 workers, where did the rest go" was arithmetic on two
single samples and did not survive the median.

#### KEY: what actually limits the scaling -- 8 cores, not 16

| workers | raster | slowest worker's raster | sum of all workers | per-worker throughput |
|---|---|---|---|---|
| 1 | 18.25 | 17.99 | 17.99 | 1.00 |
| 2 | 9.75 | 9.28 | 18.52 | 0.97 |
| 4 | 8.69 | 8.08 | 24.70 | 0.73 |
| 6 | 5.83 | 5.10 | 22.56 | 0.80 |
| 8 | 4.42 | 3.60 | 23.85 | 0.75 |
| 14 | 4.27 | 2.96 | 32.81 | 0.55 |

The last column is the finding. The same 250000 triangles cost 17.99 ms
of CPU on one thread and **32.81 ms spread over fourteen**: each worker
runs at little over half the speed it runs at alone. This box is a Ryzen
7 5700G -- **8 physical cores, 16 logical** -- and the pass asks for
`hardware_concurrency() - 2` = 14, so six cores are running two workers
each. 8 physical cores under 14 threads predicts 0.57; measured 0.55.
The rest of the degradation (0.97 at two workers, 0.75 at eight) is the
private shards: 744 KB each, so eight of them are 6 MB and fourteen are
10.4 MB against a 16 MB L3, on top of streaming the vertices.

WARNING: **Past eight workers the wall clock is flat and the CPU bill is
not.** 8 -> 14 workers moves the raster from 4.42 ms to 4.27 ms while
burning 37% more CPU, in the middle of a frame that also has 17727 draws
to submit. `hardware_concurrency() - 2` counts SMT siblings as cores and
is the wrong shape of default; the right one needs a physical core count,
which is not portable, so it is written down here rather than guessed at
in code.

Against the physical ceiling the pass is doing well: the rasterization
phase goes 17.99 -> 2.96 ms, **6.1x on 8 cores**.

#### The harness is now in the repository

`scripts/cull_audit.py`, with a recipe in `scripts/README.md`. It had
been sitting outside the tree for six sections, which is why the
single-sample readout above survived as long as it did: an instrument
nobody can review is an instrument nobody reviews. The two traps that
cost the most are written into its header rather than left to be
rediscovered -- give every field of a software row explicitly, because
nothing resets the view properties between rows; and read the spread,
never the last line.

#### Still owed, in the order this section leaves it

1. **Coarse occluder geometry.** Now the clearly-largest lever, and the
   only one that attacks the 69%: the pass *is* its rasterization, and
   97% of candidate occluders never got into the buffer at all (1285 of
   1322 dropped by the budget, section 12.12). WARNING: Must be an *inner*
   hull -- a decimation that moves a surface towards the camera invents
   occlusion.
   ⛔ **Built and measured in section 12.16, and this reading of it was
   wrong.** The dropped candidates were dropped in ranking order and the
   ranking is right: admitting ten times as many occluders moved the
   culling by 3%. The budget was never the binding constraint.
2. **Screen-space binning instead of private shards.** Would delete the
   merge (0.43 ms), one of the two spawn rounds, and most of the
   per-worker throughput loss, since a worker owning a band of the screen
   touches 1/N of the buffer rather than all of a private copy. Intel
   ships both modes for exactly this trade.
3. **A worker count that counts cores.** See above -- worth roughly a
   third of the pass's CPU at no wall-clock cost.
4. **Wire `CullBenefitEstimator` into the renderer** (section 12.13).
5. **Reuse the buffer while the camera is static.**

### 12.16 built: coarse occluders, and how an approximate one is made safe

Item 1 above, built. `Gui/Renderer/OccluderMesh.h` holds a cache of
decimated **hulls** keyed by mesh content id, and the occluder pass
rasterizes a draw's hull in place of its mesh wherever it has one.
`Render_OcclusionCoarse` and four tuning knobs beside it; off by default
until the numbers below say otherwise.

#### The construction, and what makes it admissible

The hulls are built by the vertex clustering already in the tree
(`MeshSimplify.h`) from the meshes the renderer is already holding -- no
OCCT, no Part, no shape. That is what makes this the cheap first step:
if it works, no new geometry machinery is needed anywhere.

WARNING: **Clustering is not an inner hull and does not claim to be.** It
minimizes displacement and says nothing about its sign: a chord across a
convex surface lies inside it, and the same chord across a concave one
bulges *out*, towards the camera, inventing occlusion. Three sections of
this workstream were spent removing exactly that failure.

KEY: **What rescues it is that the error is bounded, and the bound is
reported.** Every vertex moves onto the average of its cell, and every
point of a triangle is an affine combination of its corners, so no point
of the hull is further than `maxDisplacement` from a point of the
surface. A hull that then **recedes** by that distance along the view
direction cannot be nearer than the surface it stands for.

The recede is applied as a translation on the occluder's model matrix,
which also shrinks its silhouette slightly -- every point moves radially
towards the principal point, so a receded hull covers a subset of what it
covered before. What the bound does *not* cover is a lateral bulge at a
silhouette: a hull point displaced sideways can cover a pixel the surface
misses. Its magnitude is one displacement, and whether that costs pixels
is a question for the audit's 0-pixel gate rather than for an argument.

WARNING: `occluderRecede()` derives the direction from the matrices
instead of assuming a handedness -- the clip w of a point in front of the
camera is positive and grows with distance, and `w = proj[11] * z_view`,
so the step that increases distance has the sign of `proj[11]` (and of
`proj[10]` for an orthographic projection, which has no such w). The
opposite sign would pull every hull towards the camera. Tested against
both conventions and both projection kinds.

An unprompted finding from writing that test: **the occluder pass as a
whole is right-handed only.** `sightBounds` reads a box's depth as
`-vz`, so a left-handed view matrix makes every candidate report
`Offscreen` and the pass rasterizes nothing -- it fails safe, and
silently. Written down rather than fixed: nothing in the pipeline
currently hands it one.

#### What it costs to hold

`SimplifyOptions::trianglesOnly` was added for this caller: positions and
triangles, no normals, no colours, no edges, no points. The depth
rasterizer reads three positions per triangle and nothing else, and a CAD
tessellation's edge set is comparable in size to its surface. The flag
changes what the rung *contains*, not where it sits -- the triangles it
emits are identical to the full path's, which a test asserts, so a hull
stays comparable with the level the same code publishes for display.

Hulls are built a few per frame on the pass's own workers, in ranking
order, so the largest occluders get theirs first; the cache is LRU under
a byte cap and keyed by content id *with the generation in the entry*, so
a ladder rung landing in an existing mesh replaces its hull rather than
accumulating one per rung.

WARNING: A coarse row is not readable until its hulls are built. The
readout carries `pending` for exactly this reason, and `cull_audit.py`
prints it beside every coarse row: non-zero means the row measured a
warm-up.

#### measured: the gate passes, and the lever is not where it was thought

Rack model, 5455 objects, 17727 draws, one fixed camera, 1863x1064,
28-29 audit samples per row, hulls warm (`pending 0`). Every row:
**over-cull 0 px** at min, median and max, and **0 of 1440000 pixels
differ** from the same frames un-culled. The hulls are admissible.

| budget | occluders | hidden | nodes hidden | raster med | sum raster med |
|---|---|---|---|---|---|
| meshes 250k | 37 of 1322 | 7974 | 203 | 7.38 | 57.9 |
| hulls 250k | **373** | 8220 (+3.1%) | 230 | 11.60 | 109.2 |
| hulls 100k | 297 | 8139 (+2.1%) | 228 | 7.02 | 57.9 |
| hulls 50k | 142 | 7986 (+0.2%) | 229 | 7.38 | 47.1 |
| hulls 250k, bias 0 | 373 | 8733 (+9.5%) | 248 | 11.61 | 114.3 |

⭐ **The hidden counts have no spread at all** -- 7974/7974/7974 across 28
samples, and the same for every other row. The CPU oracle is
frame-to-frame deterministic on a static camera, which the hardware-query
path never was (§12.6 measured two captures of one static scene differing
in 14101 pixels). So these comparisons are exact, and unusually for this
workstream the timings beside them are the *only* part needing the
spread treatment. They get it: raster's min-to-max is roughly 2x within
every row, so the 100k row being "faster than baseline" is **not** a
claim this measurement can carry -- those two distributions overlap
almost entirely. Doubling, at 250k, is.

KEY: **The mechanism does exactly what it was built to do.** Ten times the
occluders enter the buffer (37 -> 373), 6.35 million triangles per frame
are not rasterized because a hull stood in for a mesh, and the triangles
that *are* rasterized are far more useful: 87% of a hull's cover a pixel
against 32% of a mesh's, so the buffer receives 2.75x the drawn triangles
(218125 against 79314) and 2.1x the block writes for one 250000-triangle
budget.

⛔ **And it barely hides more.** +3.1% against a ceiling that would need
+112%, for roughly double the rasterization. The pass was already
break-even (§12.12: culling saves 4.9 ms of submission), so at equal
budget the feature costs more than it buys. It ships **off**.

The budget rows say what it *is* good for, which is not what it was
built for: at 100k the hulls keep the whole culling gain and the timing
becomes indistinguishable from the 250k mesh baseline, and at 50k they
match the baseline's culling for 19% less CPU across the workers. A hull
is a cheaper way to buy the occlusion the pass already had -- not a way
to buy more of it.

KEY: **Which refutes the premise §12.15 left this on.** "1285 of 1322
candidate occluders never got in" read like a mechanism starved of
occluders. It was not: the dropped candidates were dropped in *ranking
order*, and the ranking is right -- the first 37 draws were already doing
substantially all of the hiding, and admitting 336 more moved the result
by 3%. The budget was never the binding constraint. What made the budget
*look* binding was that a full-detail mesh spends 68% of its triangles on
sub-pixel geometry -- but discarding those is cheap, which is what §12.14's
vector pre-pass is for, so the waste was never costing what it appeared to.

#### KEY: where the headroom actually is

The number that survives every arm: after culling, **8180 to 8939 of the
still-submitted draws reach no pixel** -- 91% of what is drawn -- and that
figure barely moves as the occluders improve tenfold. The depth buffer is
not what is limiting the culling.

The suspect the same readout names is **granularity**: the walk tests
*nodes*, and a node is skipped only if its whole box is hidden. 738-772
nodes are tested and 203-248 come back hidden; the hierarchy holds 1377
nodes for 17727 draws, so a node averages thirteen. A better occluder
cannot help a node holding one visible draw and forty invisible ones --
only a finer test can. Note that the occluders improving tenfold moved
nodes-hidden by 27 (203 -> 230) while the draws behind those nodes moved
by 246: the node is the unit that is failing to resolve.

`ProxyParams::maxPerCell` is 32 and is **not** a runtime parameter -- the
renderer builds the hierarchy with the defaults. Exposing it and
measuring hidden-against-node-size is the next step, and it is a far
cheaper one than this was. §8.1 warns in the opposite direction (a node
is also the size of a pop), so the two want different values and the
measurement is what says whether one number can serve both.

#### What the bias costs

Unbiased hulls hide 8733 against the receded 8220 -- the safety margin
gives up about 6% of the culling -- and on this camera they too over-cull
0 px. WARNING: That is not a licence to default the bias off. The bound is
what makes the hull *provably* unable to claim to be nearer than its
surface; a camera that finds a concave bulge would over-cull without it,
and this workstream has already spent three sections on geometry deleted
by an occlusion test that was right on the cameras it was tried on.

### 12.17 built: asking the question per object instead of per group

Section 12.16 ended by naming the limit: the walk tests *nodes*, a node is
skipped only when all of it is hidden, and 91% of what a cull still
submits reaches no pixel. This is that gap closed at the point it opens.

`MaskedCullConfig::testInstances` (`Render_OcclusionPerInstance`). When a
node answers visible, its residents are collected rather than waved
through, and after the descent each is tested against the same buffer
with its own box.

KEY: **Everything that made this cheap was already there.** The test is
read-only against a buffer nothing writes to any more, one box per
instance, one distinct draw row each (`proxyInstances` emits one instance
per row) -- so the tests are independent, need no lock, and run on the
worker threads the rasterization already spawns. There is no new state,
no verdict carried across a frame, and nothing the backend has to
support: the reason section 12.12 moved the oracle to the CPU is the same
reason this costs a loop rather than an architecture.

Two properties keep it one-directional, which is the only thing that
matters here:

- an instance test is the *same* conservative test the node got, so it
  can only ever answer `Occluded` where the pixels really are covered;
- a self-occlusion tie answers **visible** (`MaskedDepth::testRect`),
  which is what lets an instance be tested against a buffer its own
  surface is already in -- the failure that sank the hardware path
  (section 12.6) cannot arise here.

**The redundancy that is worth skipping.** A node holding one instance
and nothing below it has that instance's box for its content box, so its
test *is* the instance's test and re-asking spends a projection to learn
what is already known. Counted as `instancesRedundant`. WARNING: This is
also why a test harness with `maxPerCell = 1` measures nothing -- every
leaf is then a single instance, the node walk already *is* a per-instance
walk, and the pass correctly finds nothing left to ask. The first draft
of the unit test did exactly that and reported a working mechanism
hiding nothing.

#### Why this is not the same as lowering `maxPerCell`

A finer partition would also test smaller groups, and it is the obvious
alternative. It is the worse one, in both directions at once: it makes
the tree deeper and the descent more expensive for *every* frame, and
section 8.1 wants nodes **large** because a node is the size of a pop when
the far-field proxies of this document switch. This mode leaves the tree
alone -- coarse, cheap to walk, good at pruning whole subtrees -- and
pays a flat per-instance test only for what survives the walk. The two
knobs are independent, and only this one is free of the pop question.

#### measured: 17% more hidden for 0.4 ms, and it over-culls nothing

Same rack model, camera and instrument as section 12.16; 28-30 audit
samples per row. Every row **over-cull 0 px** at min, median and max, and
**0 of 1440000 pixels differ**.

| | hidden | nodes hidden | occluders | walk med | still invisible |
|---|---|---|---|---|---|
| per node | 7974 | 203 | 37 | 0.48 | 8939 of 9753 (91.7%) |
| per **instance** | **9345** (+17.2%) | 203 | 37 | 0.90 | 7568 of 8382 (90.3%) |
| per instance + hulls | 9501 (+19.1%) | 230 | 373 | 0.93 | 7412 of 8226 (90.1%) |

⭐⭐ **Nodes hidden, occluders admitted and triangles rasterized are
identical between the first two rows.** Granularity is the only variable,
and it is worth **1371 draws** -- every one of them a draw whose own box
is provably covered, sitting in a group that had answered visible. The
per-instance pass costs **0.42 ms** of the 9752 tests it ran.

Against section 12.16's arm, at one camera: **+17.2% for 0.42 ms** where
ten times the occluders bought +3.1% for 3.4 ms. Roughly five times the
benefit at an eighth of the cost. Priced by section 12.12's own measured
rate for what a hidden draw saves in submission (4.9 ms per 7974), the
1371 extra are worth ~0.84 ms against 0.42 ms spent -- the first change
in this section that pays for itself rather than breaking even.

⇒ **On by default.** It cannot be less correct than the node test it
refines: a draw is skipped when its own box is covered, rather than when
its neighbours' collectively are. WARNING: One camera on one model, like
every number in this section.

The hulls of section 12.16 add +156 on top of this and still cost their
3.4 ms, which does not change their verdict.

#### KEY: what is left, and it is the query volume now

7568 draws still reach no pixel while being submitted -- 90% of what is
drawn, barely moved. But the reason has changed, and the new one is
visible in the arithmetic: 9752 instances were tested individually and
only 1371 came back hidden. **These draws are individually invisible and
their bounding boxes are individually not covered.** A box is a loose
stand-in for a thin bracket or an L-shaped chassis panel: the geometry
reaches no pixel, the box does.

So the limit has moved from the *granularity* of the query to the
*volume* being queried, which is a different fix and a known one -- test
the occludee's own geometry rather than its box. That is what the coarse
hulls built in section 12.16 already are, and testing a few hundred hull
triangles instead of six box faces is the standard occludee-geometry
trade. It is the first use for them that their measurement supports.

### 12.18 built: a worker count that counts cores

Item 3 of section 12.15's list. The pass asked for
`hardware_concurrency() - 2`, which is 14 on this box -- and this box has
**8 physical cores and 16 logical**, so six cores were running two
workers each. Measured there: per-worker throughput 0.55 at fourteen
workers (8/14 predicts 0.57), the wall clock **flat** past eight, and the
CPU bill a third higher, in the middle of a frame that also has
thousands of draws to submit.

Section 12.15 declined to fix it because "a portable physical core count
does not exist and hw/2 would halve a non-SMT machine". Both halves of
that are true and neither is an argument for guessing: `physicalCoreCount()`
asks the platform, and answers 0 when it will not say.

- **Linux**: the number of *distinct* `topology/thread_siblings_list`
  values under `/sys/devices/system/cpu`. Sibling sets rather than
  `core_id` values, because a core id is only unique within its package.
- **macOS**: `sysctlbyname("hw.physicalcpu")`.
- **Windows**: `GetLogicalProcessorInformationEx(RelationProcessorCore)`.
- **Emscripten and anything else**: 0, and the caller keeps the old
  `hardware_concurrency() - 2`. Over-subscribing costs CPU;
  under-subscribing costs wall clock, which is the worse of the two.

Cached after the first call -- the Linux path reads sysfs and this is
asked once a frame.

KEY: **Automatic is now one worker per physical core, which is the whole
machine and not a share of it.** The pass runs `work(0)` on the calling
thread and spawns the rest, so the submitting thread is blocked in here
for the duration and its core is not doing anything else. Eight workers
on eight cores is eight threads on eight cores.

The same count now serves the per-instance pass of section 12.17, which
had grown its own copy of the old formula.

#### measured: 27% less CPU, and 0.4 ms more wall clock

Same model and camera, per-instance testing on, 28-29 samples per row.
The automatic row reports `8 threads` and is identical to the explicit
eight-thread row in everything it culls.

| threads | hidden | nodes hidden | sum raster min/med | raster min/med | over-cull |
|---|---|---|---|---|---|
| 14 (old default) | 9345 | 203 | 32.68 / 56.24 | 4.27 / 7.68 | 0 px |
| **0 = auto = 8** | 9339 | 207 | **24.01** / 41.90 | 4.68 / 8.04 | 0 px |
| 8 (explicit) | 9339 | 207 | 23.85 / 47.51 | 4.72 / 8.13 | 0 px |

⚠️ **Read the minima here, not the medians.** The two eight-thread rows
are the *same configuration* and their sum-raster medians are 41.90 and
47.51 -- a 13% disagreement between identical arms, which is the same
2x-spread problem section 12.15 was corrected for. Their minima agree to
0.7% (24.01, 23.85), so that is the number the comparison can carry.

**The trade, stated the way it came out rather than the way it was
predicted:** aggregate worker CPU falls **27%** (32.68 -> 23.9 ms), and
the wall clock rises about **0.4 ms** (4.27 -> 4.7 ms min). Section 12.15
called the wall clock "flat past eight workers"; at 9.6% it is not quite
flat, and the honest description is that eight workers buy a third of
the machine back for a small share of the pass. In a frame that is CPU
bound on submitting 17727 draws, nine milliseconds of worker time
returned to the other cores is worth 0.4 ms on this one -- but it is a
trade, not a free win, and `Render_OcclusionThreads` still overrides it.

⭐ Culling is unchanged to within six draws (9345 against 9339), and the
difference goes the way the merge predicts: **fewer shards lose less**,
so eight workers hide *four more nodes* (207 against 203) and therefore
leave six fewer instances for the per-instance pass to catch.

### 12.19 measured: the occludee bound is not what is left

Section 12.17 ended by naming the next suspect. After per-instance
testing, 7568 draws -- 90% of everything a culled frame still submits --
reach no pixel, while each of them was tested individually and answered
visible. So the geometry is hidden and the box around it is not, and the
obvious reading is that the box is too loose a stand-in: a world AABB is
the axis-aligned box *of an oriented box* for any rotated part, inflated
once by the rotation and again by the projection to a screen rectangle.

⭐⭐ **This section is the measurement taken before that was built.**
Section 12.16 built a mechanism on an unchecked premise and the premise
was wrong; the rule that came out of it is that a number saying work was
*discarded* is not evidence the work mattered. The same rule applies to a
bound that *looks* loose.

#### the instrument: three arms against one image

`RenderDebug_CullBounds`, on the cull audit's frame only. Every row the
cull left drawn is re-asked against the same occluder buffer, three ways,
and **nothing is culled by any of it** -- the verdicts are counted
against the id image of section 12.9 and thrown away:

- **control** -- the world AABB that ships, run again here;
- **OBB corners** -- the mesh's own local box through the draw's model
  matrix, so the rotation is not paid for twice
  (`MaskedDepth::projectPoints` folds the matrix exactly as `rasterize`
  folds it for occluders, which is why a query about a mesh's vertices
  lands where occluders built from those vertices landed);
- **per primitive** -- every triangle, and every line segment, asked on
  its own. ⚠️ Not shippable: section 12.17 costed occludee geometry at
  9.75M triangles of query rasterization. It is here as the **ceiling**,
  because nothing asked about an occludee can beat asking about its
  geometry.

Each arm is monotone in the one above it -- a triangle's hull lies inside
the OBB, whose hull lies inside the AABB, and `testRect` answers Occluded
for a subset rect at a no-nearer depth whenever it does for the enclosing
one. So an arm that adds nothing to its predecessor is a *proven* dead
end rather than an unlucky sample.

**Two counters decide whether the readout may be believed at all**, and
both were added after a first run that could not have been read without
them:

- **the control is not optional.** A row reaching this diagnostic
  survived the cull, which covers both "tested and answered visible" and
  "never asked". Without re-running the shipping box here, a coverage gap
  would be published as a tightness win. It measured **0**, so every row
  below really was tested.
- **RISK** -- rows an arm would cull that own pixels. It must be zero. An
  arm is conservative on paper until the id image has been asked, and
  this workstream has twice shipped a box test that answered hidden for
  things plainly on screen (sections 12.6, 12.10).

#### ⛔⛔ measured: the tight bound buys 8 draws, the ceiling buys 457

Rack model, 5455 objects, one camera, per-instance testing on, 72 audit
samples. Every arm is **flat across all 72** (the CPU oracle is
deterministic, section 12.16), **RISK 0 everywhere**, picture 0 of
1440000 px differ.

| arm | rows it would cull | over control | share of judged invisible |
|---|---|---|---|
| control (world AABB) | 0 | -- | -- |
| OBB corners | **8** | 8 | 0.16% |
| per primitive (ceiling) | **457** | 457 | **8.9%** |

8388 rows drawn, 7574 of them invisible, 5157 of those judged; 1968681
primitives asked in 27-38 ms.

⛔ **The oriented box is a no-op, and the geometry says why it had to
be.** The screen rect of the projected OBB corners can only beat the
AABB's rect by the amount perspective inflates the AABB's extra corners
-- under an orthographic camera the two rects are *identical*, because
the extremum of screen x over a point set is the extremum of world x when
x maps to x. The "inflated twice" story was wrong: the second inflation
is the rect, and the rect is the same rect. 8 rows out of 5157.

⛔⛔ **And the ceiling refutes the whole family.** Asking about every
triangle and every segment of every drawn object -- the tightest question
that can be asked of an occludee, at a cost no frame could pay -- flips
**8.9%** of the invisible rows. The other 91% are invisible for a reason
no occludee-side refinement can reach: they are behind geometry that is
not in the occluder buffer. Priced at section 12.12's measured rate for
what a hidden draw saves, 457 draws are worth **~0.3-0.6 ms**.

#### ⚠️⚠️ the denominator, and how the first run got it wrong

The first run of this diagnostic judged **2803 of 8388** drawn rows and
divided its result by all 7574 invisible ones, reporting a 3.0% ceiling.
The skipped 5585 were **line draws**: the arms asked only about
triangles. A CAD frame submits each object's edges as well as its faces,
so the line draws were not a rounding error -- they were the majority of
what was left, and they are the *tighter* half of the problem, since a
segment is a far smaller thing to ask about than the box around a
wireframe. Including them doubled the ceiling (228 -> 457) and halved the
denominator's dishonesty (7574 -> 5157 judged).

⭐⭐ **A diagnostic that skips part of its input reports the mechanism as
weak when the mechanism was never asked.** The readout now prints
`invisible (N of them judged)` and divides by that, and prints what it
skipped and why. Point draws are still skipped, on purpose: a point is a
*sprite*, and its vertex is not its footprint, so it is the one arm here
that could answer hidden for something on screen.

#### KEY: what this decides

Both sides of box-based occlusion are now measured and both are
exhausted on this camera: ten times the occluders bought 3.1% (section
12.16), and the tightest possible occludee bound buys 8.9%. **The
remaining prize is not culling, it is submission** -- ~1.2-1.5 us of CPU
per draw against 17727 draws is ~24 ms, which is two orders of magnitude
more than what is left in the occlusion question.

⚠️ That is a decision, not a task, and the plan for it is
`docs/DrawSubmission.md`. ⚠️ One camera on one model, like every number
in this section.

⚠️ **Correction to what this paragraph first claimed.** It said bgfx's
WebGPU backend is "Dawn-native and unusable through Emscripten". That was
true when written and has been **false since 2026-06-28**: Emscripten
support is merged upstream (bgfx PR #3795), is in the vendored tree, and
our own `bgfx.cmake` already links `--use-port=emdawnwebgpu`. It does not
change this section's conclusion — multi-draw indirect is still not
available on the web, and bgfx *emulates* it there by looping N indirect
draws, which saves no draw calls at all. See `docs/DrawSubmission.md` for
what it does and does not unlock.

### 12.20 built: occlusion becomes a memory mechanism, and the default oracle

Three changes from the 2026-08-12 review of this workstream, the third
being the seam the whole section had been building toward without
naming it.

#### the visible switch now selects the oracle that holds still

`Render_Occlusion` used to enable the hardware-query path -- the one
sections 12.6-12.11 refuted -- while the exact CPU oracle sat behind a
second, hidden property. `Render_OcclusionSoftware` now defaults ON, so
turning occlusion on gets the masked depth buffer; and the comments
that still credited `hiddenConfirm` with stability record the
refutation instead (confirmations DILUTE the flapping and were measured
not to remove it -- false answers come in runs, and the damage tracks
the re-test count, which no confirmation can reach). The query path
stays selectable for measurement, as everything refuted here does.

#### a clipped solid may not occlude

`occludes()` accepted every opaque depth-writing triangle draw --
including draws with clip planes, which the CPU rasterizer does not
have. A section-cut housing therefore stamped its FULL surface into the
buffer, and the interior geometry the cut exposes was proven "behind" a
wall the eye sees through: the one arm of this pass that deletes pixels
rather than culling less. Clipped draws are now refused as occluders
outright -- under-culling while a section plane is active is the cheap
direction to be wrong in.

#### occlusion feeds the downgrade sweep's free tier

The user directive of 2026-08-11 -- "the gain from occlusion is not
only about speed, but also gpu memory" -- now has its mechanism. The
demote pass's only visibility signal was the frustum box test, so an
enclosed assembly's interior priced its downgrade as VISIBLE error the
camera literally cannot see, and under a budget that error was paid in
quality somewhere that shows.

The backend folds the finished cull mask per source: a streak of
consecutive rendered frames in which EVERY draw carrying a tag was
culled. A source past `Render_OcclusionDemoteStreak` (default 8, 0
disables) joins `planMeshDemotes`' free tier beside the offscreen ones,
and its error never enters `acceptedErrorPx` -- an error nobody can see
must not raise the tolerance the climb runs at. Four design decisions,
each load-bearing:

- **software oracle only.** Its verdicts are exact per frame; the query
  path's period-2 flapping fed into demotes would become an upload per
  flip -- the 12.6 failure translated into memory churn.
- **downgrade sweep only, not the CPU demote.** A downgrade keeps the
  exact mesh in CPU RAM, so a verdict the camera later overturns costs
  one re-upload; the demote sweep would pay a re-tessellation for the
  same mistake. The feed widens to demotes only if measurement asks.
- **the streak is the hysteresis.** The verdict is camera-dependent
  even when exact, and the ladder's own history (13c.3) says what an
  unhysteresed demote/refine pair costs.
- **fold-generation guard.** A tag is a raw address that can be reused;
  an entry answers only at the current fold, so a dead source's streak
  cannot transfer to its successor.

Built and unit-tested (the occluded tier frees without raising the
accepted error; occluded bytes cover a deficit before any visible
error is traded). /!\ NOT yet measured live: the converged-arm harness
runs with occlusion off, and the whole-assembly camera it uses is the
flattering one anyway. The honest measurement is the in-model camera of
10.3 with a pinned budget, comparing accepted error at equal memory
with the feed on and off. Related accident worth measuring beside it:
an occlusion-culled draw already stops advancing `lastUsed`, so with
shadows off its GPU buffers are collected after 2 frames TODAY,
unmanaged, with re-upload churn as the failure mode -- the feed is the
governed replacement for that accident.

### 12.21 measured: the injected-failure audit, and what a user can reach

Sections 12.4-12.20 measure the mechanism working. This asks the other
question, the one a default-off feature needs answered before anyone
turns it on: what does a **broken** part of it do to the picture?
`scripts/occlusion_inject_probe.py`, which needs neither the rack model
nor a GPU -- a wall, 64 boxes behind it that no correct frame shows, and
four green marker boxes that every correct frame shows, three of them
standing against the background with nothing in front of them. Losing
green is deleted geometry, and nothing else in the scene can make it.

Headless llvmpipe, 1024x768, one grab per row, `Render_Occlusion` on for
every row but the first. **Drift 0 px**, and the reference re-grabbed at
the end is identical to the first.

| injected | changed px | green | culled |
|---|---|---|---|
| (reference, occlusion off) | -- | 718 | -- |
| nothing -- defaults | **0** | 718 | 192 hidden of 207 |
| `OcclusionResolution` 4 | **0** | 718 | 168 hidden |
| `OcclusionOccluderTris` 1 | **0** | 718 | **0 hidden** |
| `OcclusionSimd` off | **0** | 718 | 192 hidden |
| `OcclusionThreads` 1 | **0** | 718 | 192 hidden |
| `OcclusionCoarse` on | **0** | 718 | 192 hidden |
| `OcclusionPerInstance` off | **0** | 718 | 174 hidden |
| `OcclusionSoftware` off | 587 | **171** | 0 hidden (see below) |
| clip plane, occlusion on | **0** | 718 | 0 hidden |

Read the right-hand column first: a row that culled nothing proves
nothing, and one did. **`OcclusionOccluderTris` 1 hides 0 of 207** --
which is the documented behaviour (an occluder that was not rasterized
hides nothing) and is why that row's 0 changed pixels is not evidence of
correctness, only of the failure being in the safe direction. Every
other software row culls 168-192 instances of 207 and changes **not one
pixel**, which is the claim 12.12 makes, reproduced at a scale anyone
can run.

The **clip-plane row is the 12.20 guard, working**: with the wall cut
away the grid behind it is in plain view (1448 magenta px), occlusion on
leaves it there, and the culling drops to 0 hidden because the clipped
solid is refused as an occluder outright.

#### the one row that deletes geometry, and it is a knob a user can flip

`Render_OcclusionSoftware` off -- the hardware-query oracle 12.6-12.11
refuted, kept selectable for measurement -- lost **547 of 718** marker
pixels: three boxes standing against the background, nothing in front of
them, gone.

The stats line for that frame said `instances hidden 0`, which is not
the contradiction it looks like. One grab per row cannot see a verdict
that **flaps**, so the probe's second half grabs a run of consecutive
frames:

| oracle | green, ten consecutive frames |
|---|---|
| occlusion off | 718 718 718 718 718 718 718 718 718 718 |
| software | 718 718 718 718 718 718 718 718 718 718 |
| **hardware** | 718 718 **171 171 367 536** 718 718 718 718 |
| hardware, `OcclusionConfirm` 6 | 718 718 718 718 718 718 718 718 718 718 |

That is 12.6's oscillation on a five-object scene, and the "hidden 0"
line was simply logged on one of the frames the mask was empty -- a
second run of the same script caught the other phase, `hidden 27 drawn
180`, which is the mask and nothing else doing this. The confirmations
row is the same dilution 12.7 measured, not a fix: ten frames is a
window it survived, not a proof it holds.

**So the exposure of turning this on is:** the shipping configuration
deletes nothing here under seven separate injections, and the one
setting that deletes geometry is the one whose own documentation says it
is kept for measurement. Nothing found argues for changing the default,
in either direction -- but `Render_OcclusionSoftware` is a plain boolean
in the render parameters, and a user who flips it gets flickering holes
rather than a slower frame.
