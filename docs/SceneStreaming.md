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
                              ├─ mesh keys    ──►  L2  mesh chunk
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
meshes:    [ key ]        ← the level that spares geometry from appearance churn
materials: [ key ]
textures:  [ key ]
shaders:   [ key ]
draws:     [ compact draw record ]
```

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
not scale — a viewer cannot block on 20 MB before drawing anything — and object
bucketing makes the weaker rule safe:

> An object is submitted to the backend only when its manifest **and** every
> chunk it names are in hand.

A partially arrived scene is then a scene with *fewer objects*, never one holding
placeholder geometry. That distinction matters because the backend keys GPU
uploads on `cacheId`/`textureId`: a placeholder uploaded under an id and filled
in later would never be re-uploaded.

Because L0 carries per-object bounding boxes and is small, the viewer has a
spatial index *before* it has any geometry. Fetch order follows the view frustum,
off-screen objects can be deferred entirely, and eviction can be distance-based —
which is also the natural hook for the occlusion-culling work.

## 7. Server and viewer state

**Server.** One chunk store, key → bytes, plus the manifests of the last K
publishes. A chunk is retained while any retained manifest names it; the
generation-rolling already implemented for texture blobs generalizes directly.

**Viewer.** The keyed store built for v26 is type-agnostic and generalizes
unchanged: resident cache → IndexedDB → network, writing back what it fetched.
Meshes and materials therefore survive a page reload, so a returning viewer
reconstructs a large scene from a small root delta plus local reads.

## 8. Invariants

1. A chunk's key is the SHA-1 of its bytes and depends on nothing else. Two
   chunks with the same key are interchangeable, forever.
2. No chunk references anything by global index (§3).
3. An object is drawn only when complete (§6); no placeholder is ever handed to
   a backend that keys uploads by id.
4. The server answers any chunk request from its store alone, with no per-viewer
   state.
5. A bundled capture is self-contained: chunking is a property of the transport,
   never of the format (as v26 already establishes for textures).
6. Content keys are computed once per distinct content, never per publish (§9).

## 9. The producer-side requirement (main risk)

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

## 10. Phasing

| Phase | Change | Demo payload |
| --- | --- | ---: |
| — | today | 425 KB |
| 1a | hatch image → content key | 155 KB |
| 1b | meshes → content keys, batched pull | ~35 KB |
| 1c | camera out of the scene payload | orbit stops republishing |
| 2 | L0/L1/L2 manifests, delta sync, material dedup | ~1 KB steady state |
| 3 | frustum-ordered fetch, distance eviction | large models usable |

Phase 1 is the v26 pattern extended to two more section types and needs no
protocol restructure; 1a alone is 64% of the payload. Phase 2 is where the
complexity lands, and it is what the thin client needs — the phases before it
shrink a small scene, but only the manifest tree makes a *big* model tractable.

## 11. Open questions

- **`objectKey == 0`** means "unknown" (`Renderer.h:1011`). Those draws need a
  fallback bucket, or they collide into a single perpetually-dirty chunk.
- **Manifest history depth** — how stale a viewer may be before a full resync,
  and what that costs in server memory.
- **Very large single objects.** The mesh level bounds geometry churn, but an
  object with tens of thousands of draws still re-sends its whole draw list when
  one draw changes. Sub-bucketing the draw list may be needed; measure first.
- **Root manifest at very large object counts.** 100k objects make even the
  delta's object list non-trivial; paging the object list into content-addressed
  pages is the escape, if measurement demands it.
- **Overlay feeds** (NaviCube, axis cross) are static and per-viewer, not per
  document; they may belong in the viewer bundle rather than the stream at all.
