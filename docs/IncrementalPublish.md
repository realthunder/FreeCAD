# Incremental Publish

This document describes the design for turning the render-cache publish
path from a whole-scene rebuild into a delta: when one object changes,
the frame should cost what that object costs, not what the scene costs.

The motivating measurement is progressive STEP import, where the
document grows by thousands of objects while the view stays live, but
the payoff is general — animation, dragging, any parametric edit of one
feature in a large assembly, and the WASM viewer's publish feed all pay
the same per-frame whole-scene price today.

Related reading: `docs/TShapeRenderCache.md` (the cache and instancing
architecture this builds on), `docs/RenderEngine.md`,
`docs/SceneStreaming.md` (the publish feed to remote viewers),
`docs/RoadMap.md` (workstreams).

## 1. Problem

Measured 2026-08-02 on the release stack (RTX 3060), importing a
generated 6002-object assembly STEP progressively. Medians of three
runs each for the first two rows; the floor is the same import with the
3D view hidden, so Qt delivers no paint events at all:

| configuration | import time | frames | redraw tax over floor |
|---|---|---|---|
| view redrawing freely | 487.8s | ~1700 | +243.4s (50% of wall time) |
| redraw budget at 25% | 316.8s | 393 | +72.4s (23%) |
| redraw budget at 10% | 271.7s | 131 | +27.3s (10%) |
| floor: no frames at all | 244.4s | 0 | — |

Half the wall clock of an unthrottled progressive import is spent
redrawing — on an import whose OCCT read phase (~172s, untouched by any
of this) is included in every row. But the interesting number is the
frame count: an unthrottled view managed only ~3.5 frames per second
across the import, because at 6002 objects a single frame costs on the
order of 100ms. Frames are not too numerous. They are too expensive,
and they get more expensive as the model grows.

That is the defect. Drawing a frame after one new object appears
re-derives the entire scene's publish state. Frame cost is O(N) in the
number of objects, when the change was O(1).

`ViewParams::LiveImportRedrawBudget` (the throttle behind the two
budgeted rows) bounds the damage by drawing fewer frames, and it does
so predictably: the measured redraw tax lands on its target to within a
couple of points, so `total = floor / (1 - budget)` predicts the whole
table (271.6s predicted against 271.7s measured at 10%). That
predictability is the point — it prices the trade — but it is still a
trade, of the very thing a progressive import exists to provide:
seeing the model arrive. Making frames cheap removes the trade
instead of pricing it.

## 2. What a frame does today

`SoFCRenderCacheManager::render()`
(`src/Gui/Inventor/SoFCRenderCacheManager.cpp:1216`) compares one
number against the scene root: `sceneid != path->getTail()->getNodeId()`
(line 1224). Coin bumps a node id on any change anywhere beneath the
node, so a single new object fails this test, and everything below runs:

1. **Root cache rebuild.** A fresh `SoFCRenderCache` is constructed for
   the root and an `SoCallbackAction` re-traverses the scene.
2. **Flatten.** `getVertexCaches(true)`
   (`SoFCRenderCache.cpp:1488`) merges every child cache's contribution
   into one `Material -> VertexCacheArray` map. The result is memoized
   per cache object — but the root cache object is new this frame, so
   the memo never survives.
3. **Draw entries.** `SoFCRenderer::setScene()`
   (`SoFCRenderer.cpp:1036`) clears `drawentries`, `cachetable`, the
   opaque/transparent/on-top lists and the scene bbox, then re-pushes
   an entry per vertex-cache entry.
4. **Translate.** `RendererBridge::translate()`
   (`SoFCRendererBridge.cpp:688`) walks the whole map again and builds
   a fresh `Render::DrawCallList`, re-resolving materials and textures.
5. **Backend.** `BGFXRenderer::setScene()`
   (`src/Gui/Renderer/BGFXRenderer.cpp:11427`) takes the list, rebuilds
   instance groups, recomputes the bbox, marks the feed dirty and marks
   the level plan dirty.

## 3. What is already incremental

Geometry is. `SoFCRenderCacheManagerP::preSeparator()`
(`SoFCRenderCacheManager.cpp:1288`) keeps a `CacheSensor` per separator
node holding that node's caches; if a child's cache still matches the
node's id and `isValid(state)` (line 1314), the traversal hands the
cached result to the parent (`addChildCache`) and returns `PRUNE`.
Unchanged objects are never re-tessellated and never re-walked below
their separator.

GPU buffers are too. The bgfx backend keys meshes by `cacheId` in a
persistent table (`meshes[data.cacheId]`,
`BGFXRenderer.cpp:3494`), so a full `setScene` does not re-upload
vertex data for geometry it has already seen.

So the waste is neither tessellation nor upload. It is **aggregation**:
steps 2 through 5 above rebuild whole-scene structures from parts that
mostly did not change. That is a narrower and more tractable problem
than it first appears.

## 4. Where the time goes, measured

The aggregation steps are not equally expensive, and the design below
is only worth its complexity for the ones that are. An earlier attempt
to profile this path produced conclusions that had to be retracted,
because the "release" build was linked against a debug Coin
(`docs/DevEnvironment.md` records the trap and the `ldd` check that
catches it). So the instrumentation is permanent rather than
improvised: the `RenderDebug_Timing` view property times each stage
*exclusively* (a nested stage is subtracted from its parent) and logs
one summary line per second, which over a progressive import gives the
growth of each stage against the object count instead of one average.

Measured over the same 6002-object import, unthrottled, cost of
publishing one frame:

| objects | traverse | flatten | entries | translate | backend | total |
|---|---|---|---|---|---|---|
| 525 | 4.5 | 0.0 | 0.0 | 0.0 | 0.0 | 4.5ms |
| 2500 | 5.4 | 11.8 | 1.4 | 15.0 | 5.8 | 39.4ms |
| 4200 | 9.8 | 25.6 | 3.2 | 33.0 | 15.0 | 86.6ms |
| 5900 | 13.5 | 36.2 | 5.0 | 49.5 | 25.5 | 129.8ms |

Linear in the object count, at roughly 22µs per object per frame, and
the shares at the end are **translate 38%, flatten 27%, backend 19%,
traverse 10%, entries 3%**.

Two consequences for the design. The work worth attacking is
translate + flatten + backend, which together are 84% of a publish.
And the draw-entry build — the stage whose incremental version needs
the most machinery, because vector indices have to stay stable — is
3%, which settles §6.2 below.

## 4a. What a publish changes, measured

Phase 2 records the change set without acting on it: `ScenePublishDelta`
(`src/Gui/Inventor/ScenePublishDelta.cpp`) keeps the previous scene cache
alive and matches the rebuilt one's children against it, so every publish
can state what it changed. `RenderDebug_Delta` logs the counts once a
second, and the `delta` stage of `RenderDebug_Timing` prices the matching
itself.

Two things had to be got right for the diff to mean anything.

**A child is not identified by its cache alone.** The transform and the
material a child was captured under live in the *parent's* entry, not in
the child, so an untouched child cache beneath a moved parent transform
is a changed contribution. Entries are therefore matched whole: same
child object, same matrix and reset/identity flags, and equivalent
material.

**Material equivalence is bucket equivalence.** The flattened map is a
`flat_map` keyed by `Material` under `_Material::operator<`, which
compares every field that distinguishes one draw bucket from another. Two
materials that are neither less than the other are the same bucket, which
is all the flatten does with a captured material — so that ordering,
rather than a hand-written field-by-field equality, is the right
predicate. It compares a different set of fields per material type and
the flatten re-types a captured material to the geometry it finds, so
only the triangle branch (the widest) is trusted; captured materials are
always triangles today, and anything else is reported as changed.

**The scene root is not where the change set lives.** The first version
of this diffed the scene cache's own children, which §5 assumed was the
useful unit. Measured, it is degenerate: a document imported from a STEP
assembly hangs under a single container, so the scene cache has **one
child**, and every publish reports it as replaced. `scene=1 children +1
-1`, publish after publish, on an import where 95% of the model did not
move.

So the change set follows the hierarchy instead. A rebuilt cache is the
same node's cache one publish later, and `SoFCRenderCache` now keeps the
cache it replaced (`takePreviousCache()`) until the publish takes it as
that node closes — so every rebuilt cache is diffed against its
predecessor, and the traversal's pruning means only the changed paths are
ever diffed at all.

Measured over the 6002-object progressive import, per publish near the
end (medians of the last ten one-second lines):

| | value |
|---|---|
| caches diffed | ~86 |
| children added | ~167 |
| children dropped | ~85 |
| children reused | ~6000 (**96% unchanged**) |
| separators pruned by the traversal | ~5800 reused / ~167 rebuilt |
| cost of the diff itself | 3ms of a 102ms publish (2.9%) |

That is the phase-2 answer: for 3% of a publish, the publish can state
that 96% of what it is about to re-derive did not change. The stages that
re-derive it — flatten 28ms, translate 41ms, backend 14ms, 83ms of the
102 — are what phases 3 to 5 spend that knowledge on.

The same measurement also sets the granularity of those phases. A leaf
change deep in a group replaces the *group's* entry in its parent, so
slicing only at the top would still re-derive the whole group; the diff
has to be applied where it was recorded, at each rebuilt cache, with each
cache splicing its own memoized map. A probe over a 20-object document
confirms each case in isolation: recolouring one object reports one child
replaced, hiding it reports one dropped, moving a group reports the
group's entry replaced with its ten children untouched, and moving an
object inside the group replaces the group's entry while 31 of 35
separators below are still reused.

## 4b. Where the flatten's time goes, measured

Phase 3 set out to splice the flattened map per child. Splitting the
stage by level first says that would have missed the cost entirely.
`RenderDebug_Timing` now reports the top level (`flatten`) apart from
everything below it (`flattensub`), and on the same 6002-object import:

| stage | ms | calls |
|---|---|---|
| flatten (depth 0) | 2 | 2 |
| flattensub (below) | 28 | **6003** |

One call per object in the scene, on every publish. The cause is three
lines down from the merge loop: a parent frees each child's map as soon
as it has copied it up (`freeCacheMap`, unless `cacheHint >= 2`), so
every object re-derives its own map next frame even though the traversal
handed its cache back untouched. The memo exists; it is thrown away.

`ViewParams::RenderCacheKeepMax` (32 entries) keeps the small ones. Big
maps are the copies of whole subtrees, and dropping those is what bounds
memory, so they still go.

| | calls | flatten total | import | peak RSS |
|---|---|---|---|---|
| keep none (old) | 6003 | 28ms | 127.0s | 2775MB |
| keep ≤32 entries | ~150 | ~20ms | 119.6s | 2790MB |

Forty times fewer flattens, and the import is 5.8% faster for 15MB.

**What the remaining 20ms was.** Not the copying. The scene root moves
the same ~6000 entries out of a single bucket for 2ms; the container
pays ~20ms for the identical entries because it merges them out of
~6000 separate child maps. So the stage is priced **per child, at
roughly ten times what an entry costs to copy**.

The paragraph that used to stand here guessed the reason was the
per-child material work, and said to profile before optimizing for it.
Profiling says the guess named the smaller half. What separates a
parent that pays 10x from one that does not is whether it is an
`SoFCSelectionRoot`: the scene root is a plain `SoSeparator`, so it
skips two things the flatten does per child entry under a selection
root — composing a `CacheKey` (`key->push(selnode)` + `append`) and
asking that key for a secondary selection context.

Hanging the same 6000 children off the scene root instead of a
container isolates exactly that difference, and nothing else about the
merge:

| parent of the 6000 children | merge |
|---|---|
| `App::Part` container (a selection root) | **21ms** (+3ms for the root to re-copy) |
| the scene root (no selection root) | **9ms** |

So **~57% of the container merge was selection-key work** and ~43% is
the material work. Callgrind agrees on where that 57% sits — collection
scoped to `SoFCRenderer::setScene` over 10 publishes of a 2000-object
synthetic, on the unmodified binary: `getSecondaryContext` 5.2M Ir plus
the `getLastNode` it calls 7.4M, against `mergeMaterial` 10.6M and
`_Material`'s copy constructor 23.4M.

`getLastNode()` decodes the key's last id and looks it up in a global
map **under a mutex**, and it is asked once per child entry only to
answer "no" — no scene without element colours or partial rendering has
a secondary context at all. `SoFCSelectionRoot::hasSecondaryContext()`
answers that from a counter maintained where `contextMap2` is added to,
erased from, and destroyed (the only three places it changes).
**Container merge 21ms → 15ms, −30%**, medians of 6 publishes each.

Two notes on what this cost, and did not:

- **Memoizing the key `Origin` per node was tried and dropped.**
  `NodeKey::push()` derives the origin of the node being pushed, which
  allocates an `Origin` and casts the view provider once per child, and
  `append()` then discards it whenever the child names an object of its
  own — so caching it on the node looked free. Measured on its own it
  moved the merge from 21ms to 21ms. Not shipped: the change would have
  added a cached member and an invalidation obligation for nothing.
- **Verification gap.** The short-circuit is the kind of global fast
  path that wants an end-to-end test, and it could not get one:
  `ViewProvider::partialRender()` — the API that creates a secondary
  context — produced no change in the rendered frame on the *unmodified*
  build either, in plain Coin (render cache 0), in `SoFCRenderer`'s GL
  path (2) and in the external backend (3) alike. Whether that is a
  defect or a misuse of the API was not determined; either way the probe
  cannot distinguish a working short-circuit from a broken one. What the
  change rests on instead is that `contextMap2` is private to one file
  and has exactly three mutation sites, all of which maintain the
  counter, so a zero count provably means every `contextMap2` is empty —
  which is the condition `getSecondaryContext` was testing for anyway.

An incremental flatten still has to stop redoing the remaining work for
children that did not change, and to splice it needs the predecessor's
map to survive — which is exactly what the keep policy denies to maps
that size. Lifting that bound for one generation is the next question,
and it is a memory question: every level kept is another copy of its
whole subtree.

**What the splice had to beat** was the material work alone, and the
profile says it is not `mergeMaterial` so much as the map itself:
`flat_tree::insert_unique` calls `_Material::operator<` 268k times over
10 publishes at ~144 Ir a comparison, because the comparator walks a
struct carrying a dozen COW maps. A child whose contribution did not
change should not be re-inserted at all.

## 4c. The splice, measured

`RenderCacheIncremental` (default 0, off). With it on, a rebuilt cache
inherits the map its predecessor built and derives only the children
that predecessor did not hold.

The map turns out to have **4 buckets holding 18000 entries** at 6000
objects — three entries per child, for its triangles, lines and points —
which `RenderDebug_Timing` now reports as `map=buckets/entries`. That
number decided the design: with so few buckets the entries of one child
sit in a handful of contiguous runs, so a child's contribution can be
recorded as a run per bucket and *copied* next publish. Children are
walked in order, so the spliced map is the map a wholesale merge would
have built, entry for entry — which is what makes it checkable.

| scene | rebuild | splice |
|---|---|---|
| 6000 objects under one container | 15ms / 4 calls | **10ms / 3 calls** |
| 7334 objects, 4 levels of containers | 26-29ms / 205 calls | **7ms / 9 calls** |

The nested case is where it matters, and the call counts say why: a deep
tree re-derives an intermediate map at every level on every publish, and
the splice removes all but the levels that actually changed. Together
with §4b the container merge is **21ms → 10ms flat, 26-29ms → 7ms
nested**.

**Memory.** Big maps stop being dropped, because the splice needs the
predecessor's alive — which is exactly what `RenderCacheKeepMax`
existed to bound, so this is the cost to watch. Peak RSS **+16MB** flat
and **+36MB (+1.7%)** over four levels: every level retains its whole
subtree's entries, and that is linear in depth as expected rather than
surprising.

**Verification (§7).** `RenderCacheIncremental 2` re-derives every child
whose run was copied and compares it against what was copied, entry for
entry — cache, transform, key, merge counts and the bucket's material.
That is the stale sliver stated directly: a child that changed but whose
slice did not. Zero disagreements over a static 6000-object scene, over
a probe that adds, deletes, reorders, moves, hides and recolours
children of a container, and over a whole 6002-object progressive
import. The rendered transcripts at 0 and 1 are identical step for step,
on a probe where 10 of 12 steps move the frame.

**Still off by default.** The evidence above is this workstream's own
harnesses; the fork's own 272M-triangle gate has never completed a run
(see the note at the end of §4b about what the import harness can and
cannot show). Turning it on wants that run first — the memory figure is
the one to watch, since it grows with hierarchy depth.

### 4c-i. The match, asked once

The splice matched this publish's children against the previous
publish's — and `ScenePublishDelta` had already done exactly that
matching, over the same two caches, in the `Delta` stage of the same
publish. Both built a hash of the previous children and ran a material
comparison per child; at 6000 children under one container that was most
of what the splice still cost.

The change set now records the match (`lastMatch()`) and `postSeparator`
hands it to the new cache together with the map to splice from. The
flatten no longer builds the index at all — it reads the answer, checks
it is the right size and in range, and copies.

| scene | splice, matching itself | splice, match handed over |
|---|---|---|
| 6000 objects under one container | 8-9ms / 3 calls | **5ms / 3 calls** |
| 7334 objects, 4 levels of containers | 7-9ms / 9 calls | 7-9ms / 9 calls |

Both columns are from one A/B on one build of the same machine, which is
why the flat baseline reads 8-9ms rather than the 10ms in the table
above. The `Delta` stage stays flat at 4ms across the pair, so the work
is *gone* rather than moved into the stage that now does it for both.

The nested case does not move, and should not: ten children per cache is
an index not worth building either way, so there was nothing there to
share. The saving is a property of wide caches, and a wide cache is what
an imported assembly is.

**Memory.** One `int` per child of each rebuilt cache, held from
`postSeparator` until that cache's flatten — one publish, and at most
the scene's child count: **24KB at 6000 objects**. Peak RSS over three
baseline runs (2049.7 / 2072.6 / 2067.4 MB) and two after (2076.5 /
2072.2 MB) overlaps, so the measurement agrees with the bound.

**Unrelated finding from the same profile.** `translateCache()` called
`getenv("FC_BGFX_DEBUG_FEED")` once per translated cache — 66k lookups,
**5.8% of the whole publish**, for a debug print that is off. Hoisted to
a `static const`; it belongs to the translate stage (phase 4).

**What this does not show up in, and why.** The synth6000 progressive
import is unchanged: 119.7 / 120.1 / 121.9 / 126.6 / 133.1s, median
121.9s, against 119.6s before. It could not have shown up. The import
paints ~75 frames, so 6ms off a publish is ~0.45s of a two-minute
import, and the redraw throttle spends a *budget* rather than a fixed
cost — a cheaper publish buys more frames (73 → 75-78) at the same
share of the import, it does not shorten it. The stage timer is where
this change is visible, and the models it matters for are the ones
where the flatten dominates a frame the user is waiting on: every
redraw that rebuilds the scene cache on a large assembly, not the
import.

⚠️ **That spread is also a caution about the row above.** Five runs of
one build vary by 13.4s (11%) on this harness, which is larger than the
127.0 → 119.6s difference the keep-policy row attributes to
`RenderCacheKeepMax` from one run each. The stage numbers in that table
(6003 → ~150 calls, 28ms → 20ms) are measured and reproducible; **the
−5.8% import figure should be read as unconfirmed** until someone runs
it n≥5 both ways.

**Equivalence.** A memo that outlives its publish can serve a stale
frame, because the flatten reads selection state that no cache rebuild
announces. Checked through `getRenderStats()` — the render backend's own
target — over selection, recolour, hide, show, move and a sibling
change: the transcripts at `RenderCacheKeepMax` 0 and 32 are identical
step for step, and every step moves the frame, so the probe is sensitive
to what it is asserting. Note that `saveImage()` cannot be used for
this: it captures the composite and came back pixel-identical whether
the object was visible or not.

## 4d. The translate stage, ablated

With the flatten dealt with, `translate` was the biggest stage by far —
41-45ms of a ~100ms publish at 6000 objects. Where in it was not known,
and the one guess on record was wrong, so the stage was **ablated**: one
build, four `getenv` gates, each skipping a block, one run each.

| skipped | translate |
|---|---|
| nothing | 38-43ms |
| the per-cache mesh translation | **13-16ms** |
| the face/edge/vertex part tables | 35ms |
| the object-info resolution | 32-36ms |
| the draw-call struct build | 31-36ms |

Skipping the mesh translation *and* the part tables came to 15-16ms, the
same as skipping the mesh translation alone — the dummy mesh takes the
part tables with it — so the split is **translateCache ~20ms, part
tables ~6ms, everything else ~15ms**.

⭐ **The lesson about instruction counts.** Phase 4 was queued to start
on `getBoundingBox`, which callgrind put at 123.6M Ir, **13% of a
publish**, computed twice per entry. It was: both the draw-entry build
and the translate asked the same entry the same question, and the entry
now memoizes it. In wall time that is **~2ms, ~2%** — the Ir share of
eight corner transforms badly overstates what they cost on hardware that
pipelines them. The thing worth 26ms was not visible as a large Ir
share at all. **Ablate for share-of-time; use callgrind to find
candidates, not to size them.**

### 4d-i. Reusing a translated mesh — SHIPPED, DEFAULT ON

`RenderCacheMeshReuse` (0 off, 1 reuse, 2 reuse+verify; default 1).
Translations are kept, keyed by cache id and held **weakly**: a mesh
lives exactly as long as some draw list still refers to it, so the memo
neither keeps a vertex cache alive nor needs reconciling against the
scene.

⚠️ **A vertex cache object is stable; its arrays are not.** The memo was
first written to assume a closed cache never changes, and the verify
mode immediately said otherwise: **200 identical boxes converge onto a
single shared vertex array some publishes after each had its own** (the
CPU-side array dedup). A memo of raw pointers is only sound if it
checks, so a reused mesh is compared against what the cache holds now —
a dozen inline pointer reads, against the two allocations a translation
costs. Pointer equality *is* identity here, because the mesh holds a
reference to the storage it pinned, so that storage cannot have been
freed and reallocated at the same address. Partial caches (a selection's
single-face subset) compact their indices into the mesh itself and are
never reused.

| | translate | backend |
|---|---|---|
| 6000 objects, flat | 41ms → **24-26ms** | 11-14ms → **2ms** |
| 7334 objects, 4 levels | 42-45ms → **26-28ms** | 13-15ms → **2ms** |

The backend drop is **reported as observed, not attributed**: both of its
content hashes are already keyed by cache id, so what it stopped doing
on stable mesh objects has not been traced.

**Memory.** Peak RSS 8-25MB higher (0.4-1.2%), depending on run
conditions, and it **does not grow with the number of publishes** —
against the same baseline the gap is 8.5MB at one publish and 9.4MB at
twelve. One generation of meshes kept alive, not an accumulation.

**Verification.** `RenderCacheMeshReuse 2` re-translates every reused
mesh and compares field for field: zero disagreements. The
add/delete/reorder/move/hide/recolour/select transcript is byte-identical
at 0, 1 and 2 *and* to the build before the change. A new probe
(`geom_probe.py`) covers what the others do not — geometry being
**replaced**: resize a box, change a cylinder's deviation, swap a
feature's shape, edit a shape two features share, then set it back to an
equal one. Agrees step for step, all 8 steps moving the frame.

### 4d-ii. The registry lock, and where translate stands now

Reusing a mesh has to check the one thing that is not a property of the
cache — the rung its source has published since — and that check called
`MeshSourceRegistry::publishedError()`, which takes the registry's lock,
once per mesh per publish. **4-5ms of a 6000-object publish spent being
told nothing had been registered at all.** The registry now carries a
generation bumped by `add()` and `remove()` (the only writers), a mesh
records the generation its answer was read under, and an unmoved
generation skips the lookup. Read the generation *before* the answer:
the other order can stamp an answer as current that already was not.
**translate 24ms → 20-21ms**, which is what ablating the lookup away
entirely had predicted.

This is the third time the same shape of bug has been the answer in this
document — `getenv` per translated cache (5.8%), a mutex + global map
lookup per child entry (57% of the container merge), and now a mutex per
mesh. ⭐ **Per-item calls into a shared registry are worth grepping for
before profiling anything cleverer.**

**Where the stage stands**, 6000 objects, splice and reuse on: a publish
is ~46ms — traverse 8, delta 4, flatten 2, flattensub 5, entries 4-5,
**translate 20-21**, backend 2.

⭐ **The next target, measured: the object-info resolution is 8-9ms of
the 20-21ms translate** — now the biggest single item in the biggest
stage. Ablated in two halves: the `App` document/object lookups plus
label and type reads are **4-5ms**, and building the map that carries
them is **3-4ms**. Memoizing only the resolution would take the first
half and leave the second, because the map is rebuilt and handed over
wholesale every publish whether or not anything in it changed. Both
halves want the same thing, and it is what §8.4 already calls for: the
identity map should ride the **`updateScene` delta** rather than be
rebuilt to be moved. Note the one field that is genuinely volatile — a
label can change without touching the scene graph — so whatever carries
it needs an invalidation, not just a memo.

## 5. Design: per-child slices

The change set is available for free. `preSeparator` already knows, for
each child it visits, whether it reused a cache or built a new one.
Recording that decision turns the traversal into a diff.

**Persistent aggregate.** Keep the root cache and the structures
derived from it alive across frames instead of discarding them whenever
the root node id moves. `sceneid` stops being a rebuild trigger and
becomes a *staleness* signal that opens a delta pass.

**Slices.** Every child separator owns a slice of the flattened
vertex-cache map and of the backend draw calls; publishing a change
means removing that child's slices and splicing in new ones. Draw
entries and the sorted lists are deliberately *not* sliced — they are
rebuilt wholesale from the maintained map, per §6.2, which removes the
index-stability problem entirely.

**At every level, not just the top.** §4a measures why: the scene cache
holds one container child in an imported assembly, so slicing only there
buys nothing. Each rebuilt cache inherits its predecessor's memoized map
and splices the children the change set's diff of that same pair reports
(§4c-i), which makes the work proportional to the changed paths — the
traversal already prunes everything else.

**Backend delta.** The `Render::Renderer` interface grows an explicit
delta entry point (`updateScene(added, removed)`) alongside `setScene`.
The alternative — keep `setScene` and let the backend diff draw-call
keys — leaves the O(N) CPU list build in place, which is one of the
costs being removed, so the explicit delta is preferred. `setScene`
remains for the initial publish and for any caller that genuinely
replaced everything. Backend-side work that is currently whole-scene
per publish (instance grouping, bbox, level-plan invalidation) has to
become incremental with it, or it simply inherits the O(N).

## 6. Decisions

### 6.1 Draw-call merging: suppressed during a live import

`getVertexCaches` can merge draw calls across children by material
(`SoFCRenderCache.cpp:1769`), gated on `ViewParams::RenderCacheMergeCount`.
This is a deliberately global optimization — it fuses entries from
*different* objects — and it is exactly what a per-child slice model
cannot express: a merged entry belongs to no single child, so no single
child can invalidate it.

**Decision (2026-08-02): merging is disabled while
`App::Document::LiveImport` is set**, and the scene is re-published once
at the end of the import, where merging runs normally over the finished
model. During an import the merge is worth little anyway — it optimizes
steady-state draw submission, while an importing scene is rebuilt
continuously — and this keeps the delta path free of entries with no
owner.

Worth noting for scope: `RenderCacheMergeCount` defaults to 0, so
merging is off unless a user turns it on. The suppression matters for
correctness of the delta path, not for the common case.

### 6.2 Ordering: the sorted lists stay whole

The flattened order carries meaning: transparency sorting and the
on-top passes depend on it. Per-child contiguous slices would preserve
determinism, but a child whose transparency classification flips has to
*move* between lists rather than patch in place, and the on-top lists
are keyed independently of the scene order — machinery that only pays
if deriving those lists is expensive.

It is not. §4 measures the whole draw-entry stage, sorted lists
included, at 3% of a publish and 5ms at 6000 objects. **Decision: keep
`drawentries` and the sorted lists as a wholesale rebuild** from the
incrementally maintained vertex-cache map, and spend the complexity on
flatten, translate and the backend instead. No free list, no
tombstones, no index stability requirement — the structure that needed
them is the one not worth making incremental.

This should be revisited only if the rebuild stops being cheap; the
`RenderDebug_Timing` line reports it continuously, so that would show
up rather than being assumed.

## 7. Non-goals and risks

- **Not a Coin scene-graph change.** The Coin graph, its sensors and
  the per-separator caches stay exactly as they are; only the
  aggregation layer above them changes.
- **Correctness is a diff problem.** The failure mode of an
  incremental publish is a stale sliver — an object that changed but
  whose slice did not. Any implementation needs a verification mode
  that rebuilds the scene wholesale and compares against the
  incrementally maintained state, run over the import equivalence
  suites that already exist for progressive import.
- **The redraw budget stays.** Even with O(changed) frames, a scene
  can change faster than it can be drawn; the budget throttle
  (`ViewParams::LiveImportRedrawBudget`) remains the backstop, but it
  should stop being the thing that makes large imports usable.

## 8. Phasing

1. ~~`RenderDebug_Timing` stage split, and a baseline import profile.~~
   Done — the numbers are §4, and they set the order of what follows.
2. ~~Persistent root aggregate with a recorded change set, still
   publishing whole-scene: no behavior change, but the diff exists and
   can be asserted against.~~ Done — `ScenePublishDelta`, §4a. The diff
   turned out to belong to the hierarchy rather than the scene root, and
   it costs 3% of a publish to record.
3. ~~Incremental flatten — the maintained vertex-cache map (27%), with
   draw entries rebuilt from it wholesale (§6.2).~~ Done, in three
   steps: §4b keeps the per-object maps that were being thrown away
   every publish (28ms → 20ms), then removes the per-child
   secondary-context lookup a selection root was paying for every one of
   its children (20ms → 15ms), then §4c splices a rebuilt cache's map
   from its predecessor's (15ms → 10ms flat, 26-29ms → 7ms over four
   levels), and §4c-i stops the splice re-deriving the child match the
   change set made in the same publish (10 → 5ms flat, nested unchanged).
   Off by default pending the large-model run.
4. Incremental translate (38%): §4d ablated the stage and found most of
   it was not incremental *work* at all but repetition — every vertex
   cache re-translated into a backend mesh every publish. Keeping those
   translations took translate 41-45ms → 24-28ms and the backend stage
   11-15ms → 2ms (§4d-i), on by default. What remains for this phase is
   the original plan: per-child draw-call slices and an `updateScene`
   delta on the `Renderer` interface, so the list is not rebuilt
   wholesale to be handed over.
5. Incremental backend bookkeeping (19%): instance groups, bbox and
   level-plan invalidation, which otherwise inherit the O(N).
6. Verification mode (§7) and the equivalence runs; then revisit the
   throttle default, which should be able to loosen considerably.
