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
and splices the children its own diff reports, which makes the work
proportional to the changed paths — the traversal already prunes
everything else.

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
3. Incremental flatten — the maintained vertex-cache map (27%), with
   draw entries rebuilt from it wholesale (§6.2).
4. Incremental translate (38%): per-child draw-call slices, and an
   `updateScene` delta on the `Renderer` interface so the list is not
   rebuilt to be handed over.
5. Incremental backend bookkeeping (19%): instance groups, bbox and
   level-plan invalidation, which otherwise inherit the O(N).
6. Verification mode (§7) and the equivalence runs; then revisit the
   throttle default, which should be able to loosen considerably.
