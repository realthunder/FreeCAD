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

## 4. Step 0: measure the split before designing further

The four aggregation steps are not equally expensive, and the design
below is only worth its complexity for the ones that are. An earlier
attempt to profile this path produced conclusions that had to be
retracted, because the "release" build was linked against a debug
Coin (`docs/DevEnvironment.md` records the trap and the `ldd` check
that catches it).

Before implementing, add a per-stage timing readout as a real runtime
knob rather than temporary instrumentation: a `RenderDebug_Timing`
parameter that accumulates the four stage costs and reports them
through the existing render-debug readout (`docs/RenderDebug.md`).
One instrumented import then says which stages to attack, and the same
knob keeps reporting after the work lands — the numbers above should
never again come from a build whose configuration is assumed rather
than checked.

## 5. Design: per-child slices

The change set is available for free. `preSeparator` already knows, for
each child it visits, whether it reused a cache or built a new one.
Recording that decision turns the traversal into a diff.

**Persistent aggregate.** Keep the root cache and the structures
derived from it alive across frames instead of discarding them whenever
the root node id moves. `sceneid` stops being a rebuild trigger and
becomes a *staleness* signal that opens a delta pass.

**Slices.** Every child separator owns a slice of each aggregate: its
range of the flattened vertex-cache map, its range of `drawentries`,
and its range of backend draw calls. Publishing a change means removing
that child's slices and splicing in new ones. `SoFCRenderer` already
maps `CacheKey -> draw entry indices` in `cachetable`, which is the
removal hook; `drawentries` is a vector, so indices must stay stable
across removals — a free list with tombstones, compacted when the
fraction of dead entries crosses a threshold, rather than an erase that
renumbers everything.

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

### 6.2 Ordering: open

The flattened order carries meaning: transparency sorting and the
on-top passes depend on it. Per-child contiguous slices plus an ordered
child index preserves determinism for the common case, but a child
whose transparency classification flips has to *move* between lists
rather than patch in place, and the on-top lists are keyed
independently of the scene order.

This is not yet decided. The options are (a) slices ordered by child
with re-sorting confined to the affected lists, or (b) keeping the
sorted structures whole and rebuilding only those, on the theory that
sorting N entries is far cheaper than deriving them. The measurement in
§4 informs the choice: if the sorted-list rebuild is a small fraction
of the frame, (b) is much less machinery for nearly the same win.

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

1. `RenderDebug_Timing` stage split (§4), and a baseline import profile.
2. Persistent root aggregate with a recorded change set, still
   publishing whole-scene: no behavior change, but the diff exists and
   can be asserted against.
3. Incremental `drawentries` with slice bookkeeping and the free list.
4. `updateScene` delta on the `Renderer` interface, plus incremental
   instance grouping/bbox in the bgfx backend.
5. Verification mode (§7) and the equivalence runs; then revisit the
   throttle default, which should be able to loosen considerably.
