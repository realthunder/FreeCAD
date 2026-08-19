# Worker-side vertex-cache emission at landing

Status: shipped 2026-08-14, verify-mode clean (12/12 exact across
face/edge/point sets on forced descent landings; the one disagreement
found was a real Coin bug -- empty polylines in
SoIndexedLineSet::generatePrimitives -- fixed in the fork).

**2026-08-16: emission WIDENED to the inline path.** As shipped, the
only producer was the pooled fill, which takes landing-pump items alone
(`inLandingPump()`, at or above `Render_VisualFillMinFaces` = 2000,
never the load-time drain). Nothing else emitted, so an ordinary load
registered nothing and every publish reported `0 adopted of N offered,
N no entry` -- measured 720/720 on a 240-cylinder grid and across 296
rack publishes at two budgets. `updateVisual`'s epilogue now emits from
the final node arrays as well, which is where every inline path arrives
(ordinary rebuild, load drain, coarse rung, stand-in).

**2026-08-16: an appearance change now MOVES the stamp.** With emission
widened, the grid still reported `240 stale SoBrepFaceSet` -- one per
object, every one. `applyShapeAppearance` runs a `SoUpdateVBOAction`
over the face set and `SoBrepFaceSet::doAction` answers it with
`touch()`, so the node id moved and the entry registered against the
old id was dropped at pickup. Only the STAMP was stale there: the
appearance apply writes the material nodes, while the arrays the
content mirrors are untouched and everything colour-dependent is
re-derived from the live elements at adoption. `restamp(node)` moves
the stamp instead of losing the arrays; the rebuild prologue runs the
same action and is deliberately NOT restamped, because there the
geometry really is about to be replaced.

Measured on the grid, same scene and same 1ms budget:

    before widening   15 publishes, 677 deferred,   0 adopted of 720
    after widening     1 publish,   220 deferred, 480 adopted of 720
    after restamp      1 publish,     0 deferred, 720 adopted of 720

Verify arm on the same scene: **720 verified, 0 mismatches**. Follows the paint-sampler diagnosis: publish
capture (SoFCRenderCacheManager re-running generatePrimitives over every
changed shape) is ~50% of storm paints, and the capture budget
(Render_CaptureBudgetMS) only spreads that cost across frames. This
feature removes it for the shapes that dominate the storm: the pooled
fill (Render_VisualFillOnPool) already computes every array the capture
would re-derive, on a worker.

## The idea

The fill worker (fillVisualArrays) emits, next to the Coin node arrays,
the CONTENT of the three SoFCVertexCache objects the next publish would
otherwise capture by traversal (faceset, lineset, nodeset each get their
own cache keyed on the shape node). The landing registers that content
on the nodes; the next SoFCRenderCacheManagerP::preShape ADOPTS it --
element capture and validity exactly as today, but the per-triangle
generatePrimitives + hash-dedup walk is replaced by installing the
prebuilt arrays.

Threading contract: workers touch ONLY plain detached structures
(std::vector of SbVec3f/int32_t -- value types). The SoFCVertexCache
object itself (an SoCache, Coin refcounting) is constructed and
installed on the GUI thread. COIN_THREADSAFE stays OFF.

## What the worker emits (per drawable)

Mirrors what generatePrimitives + addTriangle/addLine/addPoint + close
produce, in the same order, so the verify mode can compare byte-for-byte:

- Triangles (from the fill's verts/norms/texcoords + faceIndex +
  partIndex): the first-seen-order dedup of (vertex, normal, texcoord)
  triples -> vertex/normal/texcoord arrays + triangle index array +
  per-face part offsets. Part tables derived at close() --
  partcenters, nonflatparts -- ride along.
- Lines (from lineIndex): per -1-separated polyline run r, consecutive
  index pairs with line part r -> line index array + part offsets.
- Points: the vertex list in order.

Phase A scope: UNIFORM COLOR ONLY (SoLazyElement numdiffuse <= 1 and
numtransp <= 1, i.e. colorpervertex = 0). The dedup is then purely
geometric and no baked color array is emitted; firstcolor/hastransp are
stamped at adoption from the live lazy element. Per-face-colored
shapes, multitexture beyond a trivial unit 0, markers on the point set,
and anything else outside the contract falls back to traversal capture.
(Per-face colors need per-vertex baking with vertex duplication --
decimated meshes share vertices across faces -- deferred to phase B.)

## Adoption (GUI thread, inside preShape)

A static one-shot registry on SoFCVertexCache maps shape node ->
prebuilt content, stamped with the node id AT REGISTRATION (the landing
registers after its last field write on that node; any later touch
changes the id and voids the entry). preShape, after the existing
valid-cache lookup and before the capture-budget deferral:

1. entry present and entry.nodeid == node->getNodeId(), else fall
   through to normal capture;
2. construct SoFCVertexCache(state, node, prev) as today (ctor reads
   seam/highlight/shapeInfo fields by name; prev/proto seeding and
   selnodeid stamping stay exactly as the traversal path);
3. open(state) -- captures SoFCDiffuseElement ids and the lazy element
   exactly as today, so invalidation semantics are unchanged;
4. CHECK the live elements against the contract (numdiffuse/numtransp
   <= 1, no multitexture units beyond the trivial case, no bumpmap);
   on failure: proceed with normal traversal capture (the cache is
   already open -- the fallback is literally "do nothing special");
5. install the prebuilt arrays, close(state), addChildCache, PRUNE.
   The registry entry is consumed either way.

The adoption replaces work the capture budget was rationing, so an
adopted shape does not count against Render_CaptureBudgetMS.

## Verification

Render_WorkerVertexCache: 0 = off, 1 = on, 2 = verify. Verify adopts
AND runs the traversal capture, compares vertex/index/part arrays and
the color/transparency stamps, logs any mismatch, and keeps the
traversal result (same pattern as RenderCacheMeshReuse 2).

## Dependencies

Runs entirely in fcad; no Coin change needed. The later off-thread
aggregation step (cross-thread cache ref/unref) is what needs the
fork's atomic-refcount patch, gated on coin_fork_features()
("atomic-refcount") -- not this.
