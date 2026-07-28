# Scene streaming — content-addressed delta publishing

Status: design. The leaf tier for textures is implemented (snapshot v26,
`c4337904f9`); everything else here is specification.

Companions: [RenderEngine.md](./RenderEngine.md) §2 (the snapshot format and the
tiers that consume it), [ThinClient.md](./ThinClient.md) (the UI layer this
feeds), [FileBlobsManager.md](./FileBlobsManager.md) (the same content-addressing
model applied to the document archive), [ComputeBoundaries.md](./ComputeBoundaries.md).

This document specifies how the scene reaches the browser tier for models too
large to send whole: what is addressed by content, how it is chunked, how a
publish becomes a delta, and what the viewer is allowed to draw before it has
everything.

---

## 1. What is wrong

The scene server publishes one monolithic `SceneDump` buffer and broadcasts it
whenever any feed changes. Measured on `scripts/demo-water.py` (a small scene —
18 scene draws, 7 objects), with a parser that consumes the payload exactly:

| Section | Bytes | Share |
| --- | ---: | ---: |
| hatch image | 270,012 | 63.6% |
| meshes (71) | 119,749 | 28.2% |
| overlay draws (53) | 24,702 | 5.8% |
| scene draws (18) | 8,176 | 1.9% |
| textures (22, keys only since v26) | 1,676 | 0.4% |
| configs | 270 | 0.1% |
| camera | 140 | 0.0% |
| selection draws (**0**) | 4 | 0.0% |
| highlight draws (**0**) | 5 | 0.0% |
| **total** | **424,799** | |

Two consecutive publishes of that scene differ in **28 bytes** — a few animated
config floats and the camera matrices. Everything else is re-sent unchanged, and
71/71 meshes are byte-identical.

Three facts set the design:

- **Selection costs nothing.** Preselection and selection highlighting are built
  client-side; the backend ships empty feeds (4 and 5 bytes). Selection is not a
  republish driver and needs no protocol.
- **The per-draw cost dominates at scale.** A draw is 463 B, of which 347 B is
  its `Material` — written **inline per draw, never deduplicated**. In this
  scene 38 of 71 materials are distinct (46% redundant); an assembly whose
  thousands of parts share a dozen appearances approaches total redundancy.
  Extrapolated to ~50k draws that is ~22 MB per publish before a single mesh.
- **Identity is provenance, not content.** `MeshData::cacheId` comes from
  `SoFCVertexCache`, where it is `cacheid(++VertexCacheId)` — a bare counter
  (`SoFCVertexCache.cpp:252`). A recompute that yields byte-identical
  tessellation still mints a new id, so the viewer re-receives it and the
  desktop redundantly re-uploads it. `TextureImage::textureId` has the same
  property. This is precisely the mistake `FileBlobsManager.md` §1 records
  fixing in the document tier.

Wasted format capacity compounds it: `writeMaterial` always writes 96 B of clip
planes (all `MaxClipPlanes` slots regardless of `numclipplanes`) and 64 B of
texture matrix even when untextured, and `writeDraw` always writes a 64 B model
matrix despite carrying an `identity` flag.

## 2. The model

The scene becomes a **tree of content-addressed chunks** with a small mutable
root. A chunk's key is the SHA-1 of its bytes; a chunk is immutable, so a cached
copy is valid forever and no invalidation protocol exists.

```
L0  root manifest        mutable, per publish, delta-encoded
      └─ objects[]  ──►  L1  object manifest      content-addressed
                              ├─ mesh keys+bbox ►  L2  mesh chunk
                              ├─ material keys──►  L2  material chunk
                              ├─ texture keys ──►  L2  texture blob   (v26, done)
                              ├─ shader keys  ──►  L2  shader chunk
                              └─ compact draws  (inline)
```

The **object** is the delta unit, and `DrawCall::objectKey` is already the right
identifier: a content hash of the scene-graph node path (`Renderer.h:1011`), so
it is stable across publishes and distinguishes Link *occurrences*, not just
document objects. Bucketing by it makes the wire's unit of change equal the
document's unit of edit: recompute one object, one chunk changes.

The **mesh level inside the object** is what keeps that true for large objects.
An object manifest *names* its meshes rather than containing them, so an object
with thousands of draws does not re-send its geometry when only its placement or
appearance moved — and, conversely, a re-tessellation replaces one mesh chunk
without disturbing the draws around it.

The mesh is also the **submission** unit (§6): the object decides what is fetched
and what is invalidated, but drawing waits on individual meshes, so a large
assembly reveals its parts as they land instead of appearing all at once.

## 3. Reference discipline: keys, never indices

**Every cross-chunk reference is a content key. No chunk references anything by
position in a global table.**

This is the load-bearing rule. The monolithic format indexes into flat mesh,
texture and shader tables; if an object manifest inherited that, then inserting
one object anywhere would renumber the tables, change the bytes of every object
manifest, and invalidate the entire cache while nothing about those objects had
changed. Index-based referencing manufactures exactly the false invalidation
this design exists to avoid.

The consequence is that the root manifest carries **no global tables**. The
viewer discovers the key set as the union of what the object manifests name, and
deduplication happens naturally in its store because identical content has an
identical key.

## 4. Wire format

### L0 — root manifest

Sent on every publish; delta-encoded against the version the viewer holds.

```
magic, formatVersion, manifestVersion u64
inline volatile state:
    camera (view/proj/viewport/clear), background, hidden-line, section,
    AO, PBR, bump, light, volumetric, water, bloom, presel/sel styling,
    debug config, autozoom/effect/ssao resolution        (~410 B total)
hatch:    key
objects:  [ objectKey u64, objectManifestKey, bbox[6], flags ]
overlays: [ id, anchor, objectManifestKey ]
```

Per-object entry ≈ 53 B. A full manifest for 10k objects is ~530 KB, which is
why §5 delta-encodes it; the full form is only for a first connect or a resync.

### L1 — object manifest (content-addressed)

```
objectKey, bbox[6]
meshes:    [ key, bbox[6] ]   ← the level that spares geometry from appearance churn
materials: [ key ]
textures:  [ key ]
shaders:   [ key ]
draws:     [ compact draw record ]
```

Mesh entries carry a bounding box for the same reason object entries do: it is
what lets the viewer act on a mesh it does not yet have — draw a proxy for it
(§6), order its fetch by what is on screen, and later choose a level of detail
for it (§7). The box is known to the producer for free; the mesh chunk it
describes may be megabytes.

**The mesh key covers the geometry, not the provenance.** `writeMesh` emits
`cacheId` as its first field, and `cacheId` is a bare counter that changes on
every re-tessellation *including* ones that reproduce identical geometry (§1).
Hashing the chunk verbatim would therefore mint a new key for unchanged bytes and
defeat the entire design. So `cacheId` is excluded from the mesh chunk, and the
viewer derives the id it hands the backend from the key itself. Identical
geometry then collapses onto one GPU upload — across recomputes, across
sessions, and across objects that happen to share a shape.

A compact draw record references its mesh/material/texture/shader by **index
into this manifest's own key lists** — local indices, so the chunk stays
self-contained and its key depends on nothing outside it. Fields become
conditional: model matrix only when `!identity`, texture matrix only when
textured, clip planes only `numclipplanes` of them. With the material lifted out,
a draw record is roughly 120 B rather than 463 B.

### L2 — leaves

Mesh chunk = the existing `writeMesh` payload. Material chunk = `writeMaterial`
minus the texture/shader references (which move to keys). Texture blob = as
shipped in v26. Shader chunk = `writeUserShader`. Hatch = raw RGBA plus its
dimensions.

## 5. Sync

The viewer states the manifest version it holds; the server answers with the
difference and a bounded history decides when that is possible.

```
viewer  → { cmd: "hello", manifest: <version or 0> }
server  → root delta: { manifestVersion, inline state,
                        objects: { added[], changed[], removed[] } }
        + the chunks new in this publish (optimistic push)
viewer  → { cmd: "getChunks", keys: [...] }        ← batched, one round trip
server  → chunk payloads
```

**Push the delta, pull the gaps.** The server knows what changed because it built
both manifests, so appending the new chunks to the broadcast costs a round trip
of nothing in the common case. Any viewer that is nonetheless missing something —
reconnected, evicted, joined mid-session — pulls it. Pull is what makes the
protocol correct; push is what makes it fast. The server never tracks per-viewer
state: a chunk request is idempotent and answerable from the store alone.

**Pulls are batched.** One request carrying N keys, one reply frame. This is not
an optimization but a correctness-of-cost issue: the 71 meshes of the demo scene
as 71 individual round trips would be slower than sending 120 KB inline. Few and
huge (textures, hatch) stay well served by individual `GET /blob?key=`, which
also inherits browser HTTP caching; many and small go through the batched
WebSocket path.

When a viewer's manifest version has fallen out of the server's history, it is
told to resync: it re-fetches a full root manifest. The monolithic `SceneDump`
serializer is retained as the ultimate fallback (`getFull`) and remains the
format of bundled `.fcsd` captures, which must stay self-contained.

## 6. Progressive application

The v26 texture tier applies a snapshot only once every key resolves. That does
not scale — a viewer cannot block on 20 MB before drawing anything. The rule is
therefore per **mesh**, the finest unit that is independently drawable:

> A draw is submitted to the backend when the mesh chunk it names, and the
> material/texture/shader chunks it names, are in hand. Anything not yet
> complete is drawn as a **bounding-box proxy** instead.

Mesh granularity rather than object granularity matters because an object is not
an atom: an assembly component with a hundred parts should reveal them as they
land, not wait for its slowest chunk. Nothing about the object level is lost —
it is still the delta unit and the fetch bucket — but it is no longer the
submission unit.

### Progressive refinement

The two manifest levels give three fidelities for free, each a strict refinement
of the last:

1. **L0 only** — the object's own bbox is known. One proxy box per object.
2. **L1 in hand** — every mesh bbox is known. The single box resolves into one
   proxy per mesh, so the object's silhouette is roughly right long before any
   geometry arrives.
3. **Mesh chunks arrive** — each proxy is replaced by real geometry, one mesh at
   a time, in whatever order the fetch prioritiser chose.

A useful consequence: the initial camera fit runs off L0 bounding boxes, so the
view frames the model correctly *before* the first triangle exists and does not
lurch as geometry streams in. Today's `applySnapshot(fit)` fits to loaded
geometry and would re-fit repeatedly under streaming.

### Pending proxies

A proxy is real geometry — a unit box, instanced per pending mesh with its bbox
as the transform — carrying a distinct visual cue so it never reads as part of
the model: unlit, translucent fill with a brighter wireframe edge, in a reserved
"pending" colour. It should be configurable, and disabling it degrades to
drawing nothing, which is the current behaviour.

Proxies are **excluded** from shadow casting, AO, section capping, hidden-line
and outline passes, and from picking — they are progress indication, not
geometry, and letting them into those passes would make loading visibly corrupt
the shading of everything around them.

The invariant that makes this safe is that a proxy is **never submitted under
the identity of the mesh it stands in for**. It is a separate draw with its own
mesh and its own id. The backend keys GPU uploads on that id and would never
re-upload a placeholder that was filled in later; the proxy sidesteps that
entirely by never claiming to be the thing it is waiting for. When the real mesh
lands, the proxy draw is dropped and the real draw submitted.

### Prioritisation

Because L0 is small and carries bounding boxes, the viewer holds a spatial index
*before* it holds any geometry. Fetch order follows the view frustum — nearest
and largest-on-screen first — off-screen objects can be deferred entirely, and
eviction can be distance-based. This is also the natural hook for the
occlusion-culling work.

## 7. Level of detail (future)

The design leaves room for LOD without a format break, and the pieces it needs
are already here: per-mesh bounding boxes to estimate projected screen size, and
content addressing to make each variant an independent immutable chunk.

A mesh entry generalises from one key to a list, coarsest first:

```
meshes: [ bbox[6], lods: [ { key, error } ] ]
```

The viewer picks a level from the bbox's projected size and its budget, fetches
that chunk, and may refine later — each level is just another content-addressed
chunk, cached and evicted like any other, with no invalidation because none of
them ever change.

Seen this way the pending proxy of §6 is simply the coarsest level of the same
continuum — box, then coarse mesh, then full geometry — and the same
prioritiser drives all of it. What LOD adds is producer-side work rather than
protocol: generating the variants (OCCT tessellation at several deviations, or
decimation), and extending the TShape-level tessellation sharing in
`docs/TShapeRenderCache.md` to cache per level. Switching level costs nothing on
the wire that was not already paid, because a level the viewer has kept is a key
it already holds.

## 8. Server and viewer state

**Server.** One chunk store, key → bytes, plus the manifests of the last K
publishes. A chunk is retained while any retained manifest names it; the
generation-rolling already implemented for texture blobs generalizes directly.

**Viewer.** The keyed store built for v26 is type-agnostic and generalizes
unchanged: resident cache → IndexedDB → network, writing back what it fetched.
Meshes and materials therefore survive a page reload, so a returning viewer
reconstructs a large scene from a small root delta plus local reads.

## 9. Invariants

1. A chunk's key is the SHA-1 of its bytes and depends on nothing else. Two
   chunks with the same key are interchangeable, forever.
2. A key covers content, never provenance: `cacheId` is excluded from a mesh
   chunk and the backend-facing id is derived from the key (§4). Hashing a
   counter would make every re-tessellation a cache miss.
3. No chunk references anything by global index (§3).
4. A draw is submitted only when every chunk it names is in hand (§6). Until
   then it is a bbox proxy, and **a proxy never carries the identity of the mesh
   it stands in for** — a backend that keys GPU uploads by id would never
   re-upload a placeholder filled in later.
5. Proxies are progress indication, not geometry: excluded from shadows, AO,
   section capping, hidden-line, outlines and picking (§6).
6. The server answers any chunk request from its store alone, with no per-viewer
   state.
7. A bundled capture is self-contained: chunking is a property of the transport,
   never of the format (as v26 already establishes for textures).
8. Content keys are computed once per distinct content, never per publish (§10).

## 10. The producer-side requirement (main risk)

Delta publishing on the wire is worthless if the producer rebuilds and rehashes
the whole scene each publish — the cost merely moves from network to CPU.

Two facts make this real. `RendererBridge::translate` holds its mesh map as a
**function-local** (`SoFCRendererBridge.cpp:681`), so `MeshData` objects are
rebuilt per translation and a key memoized on the object pointer would never
hit; the memo must be keyed on `cacheId`, which lives on the persistent
`SoFCVertexCache` and never repeats. And L1 manifests must not be rebuilt for
untouched objects, which requires **per-object dirty tracking** on the producer
side — the renderer currently knows only a scene-wide `dirtyChanged`.

Settling that tracking is a prerequisite, not an optimization. Until it exists,
hashing ~10k object manifests per publish is the bottleneck the design was meant
to remove.

## 11. Phasing

| Phase | Change | Demo payload |
| --- | --- | ---: |
| — | today | 425 KB |
| 1a | hatch image → content key | 155 KB |
| 1b | meshes → content keys, batched pull | ~35 KB |
| 1c | camera out of the scene payload | orbit stops republishing |
| 2 | L0/L1/L2 manifests, delta sync, material dedup | ~1 KB steady state |
| 3 | mesh-complete submission + bbox proxies | model appears while it loads |
| 4 | frustum-ordered fetch, distance eviction | large models usable |
| 5 | LOD variants per mesh (§7) | large models *fast* |

Phase 1 is the v26 pattern extended to two more section types and needs no
protocol restructure; 1a alone is 64% of the payload. Phase 2 is where the
complexity lands, and it is what the thin client needs — the phases before it
shrink a small scene, but only the manifest tree makes a *big* model tractable.

## 12. Open questions

- **`objectKey == 0`** means "unknown" (`Renderer.h:1011`). Those draws need a
  fallback bucket, or they collide into a single perpetually-dirty chunk.
- **Manifest history depth** — how stale a viewer may be before a full resync,
  and what that costs in server memory.
- **Very large single objects.** The mesh level bounds *geometry* churn and
  decouples submission from it, but an object with tens of thousands of draws
  still re-sends its whole draw list when one draw changes. Sub-bucketing the
  draw list may be needed; measure first.
- **Proxy churn.** A mesh that arrives quickly should not flash a proxy for one
  frame. A short grace period before a proxy appears (and a fade when it is
  replaced) is probably wanted; needs to be tuned against a real model, not
  guessed.
- **Do proxies belong in the depth prepass?** Excluding them keeps effects
  honest, but means geometry behind a pending object shows through it. Either
  reading is defensible; decide by looking at it.
- **LOD error metric and budget** (§7) — what `error` means (screen-space
  deviation is the usual choice) and whether the level is chosen per mesh or
  solved globally against a triangle budget.
- **Root manifest at very large object counts.** 100k objects make even the
  delta's object list non-trivial; paging the object list into content-addressed
  pages is the escape, if measurement demands it.
- **Overlay feeds** (NaviCube, axis cross) are static and per-viewer, not per
  document; they may belong in the viewer bundle rather than the stream at all.
