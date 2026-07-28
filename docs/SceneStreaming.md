# Scene streaming — content-addressed delta publishing

Status: phase 1 implemented, phase 2 onward is specification. The leaf tier is
done — textures (snapshot v26, `c4337904f9`), the hatch image (v27, `ebddfc0fdd`)
mesh chunks with batched pull (v28), and the deduplicated material table
(v29-v31) are all content-addressed and served out of band, which took the
reference scene from 425 KB to 10.8 KB per publish. The
manifest tree and delta sync (§4-§5) are not built yet.

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

**Which of the two a payload gets is decided by its size, and by nothing else.**
Not by what it holds: the viewer's fetch path does not know a texture from a
mesh, and the loader does not tell it. The line is one batch's worth
(`kRequestBytes`) — a payload that would fill a request by itself gains nothing
from sharing one, and gains the browser's own caching by taking a plain GET. So
a small texture rides a batch and a large mesh takes its own request, both
without a rule naming either.

Two things follow, and both are requirements on the format rather than on the
viewer. **Every deferred payload states its size in the stream** — including a
texture, which before v33 wrote a zero there, because a consumer cannot choose a
policy for a payload whose size it has to guess. And **whether a missing payload
is survivable is the payload's own answer**: a consumer that cannot obtain one
offers the entry nothing (`fill(snap, nullptr, 0)`) and takes its verdict — a
texture clears its pending flag and says yes, since the draw renders untextured;
geometry says no and the snapshot is withheld. The alternative — a consumer that
knows textures are optional — is the same kind-knowledge creeping back in
through the error path.

Measured on `scripts/demo-water.py`, a first load resolves as 15 batched
requests and 3 individual ones, the latter being the genuinely large images.

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
"pending" colour. Disabling it degrades to drawing nothing, which is today's
behaviour.

**Proxies participate in depth**, including the depth prepass. A bbox is a
conservative bound of the geometry it stands for, so writing depth keeps the
scene behind a pending object from showing through it and then popping when it
resolves — the model reads as solid throughout loading, which is the whole point
of drawing proxies at all. The cost is that a box over-occludes: transiently it
hides slightly more than the real mesh will.

They stay **excluded** from shadow casting, section capping, hidden-line,
outline passes and picking — those are shading and interaction, not occupancy,
and a box in them reads as a modelling error rather than as progress.

> **Coupling to settle in implementation.** AO derives from the depth prepass,
> so putting proxies in it means AO sees them and darkens their edges unless
> they are masked out (a stencil bit, or an id channel the AO resolve tests).
> Whether that transient darkening is worth masking is a judgement to make
> against a real model; the mask is cheap, so the default should be to exclude
> them and relax it only if it looks better.

The invariant that makes all of this safe is that a proxy is **never submitted
under the identity of the mesh it stands in for**. It is a separate draw with
its own mesh and its own id. The backend keys GPU uploads on that id and would
never re-upload a placeholder that was filled in later; the proxy sidesteps that
entirely by never claiming to be the thing it is waiting for. When the real mesh
lands, the proxy draw is dropped and the real draw submitted.

### Timing

A mesh that arrives quickly should not flash a box for one frame. A proxy
therefore appears only after a **grace period**, and fades rather than cutting
when it is replaced. Both are user-configurable view parameters in the
`Render_*` family, alongside enable and colour:

| Parameter | Meaning |
| --- | --- |
| `Render_ProxyPending` | draw proxies at all (default on) |
| `Render_ProxyGrace` | ms a mesh may be outstanding before its proxy appears |
| `Render_ProxyFade` | ms to cross-fade a proxy out when its mesh lands |
| `Render_ProxyColor` | the reserved "pending" colour |

Defaults want tuning against a real model on a real link rather than being
guessed here; the point of making them parameters is that the tuning does not
need a rebuild.

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

What `error` measures and how a level is chosen are deliberately left open until
there is a real model to tune against; the likely answer is that both become
`Render_*` parameters like the proxy timings above, so the policy is adjustable
without a rebuild.

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
| 1a | hatch image → content key (**done**, v27) | 155 KB |
| 1b | meshes → content keys, batched pull (**done**, v28) | ~35 KB |
| 1c | camera out of the scene payload | **not needed, see below** |
| 2a | material dedup (**done**, v29) | 27.7 KB |
| 2a′ | trimmed records + table out of band (**done**, v30/v31) | 10.8 KB |
| 2b-1 | draw groups → content-keyed chunks, leaves keyed (**done**, v33) | 1.4 KB |
| 2b-2 | root delta-encoded, history + resync | ~1 KB steady state |
| 2b-3 | progressive fidelity off the manifest boxes | frames before geometry |
| 3 | mesh-complete submission + bbox proxies | model appears while it loads |
| 4 | frustum-ordered fetch, distance eviction | large models usable |
| 5 | LOD variants per mesh (§7) | large models *fast* |

Phase 1 is the v26 pattern extended to two more section types and needs no
protocol restructure; 1a alone is 64% of the payload.

**1a landed as snapshot v27**: the hatch image simply joins the texture table,
so it inherits the v26 content key, the deferred flag and the `GET /blob?key=`
path rather than getting a mechanism of its own — the same reason it was missed
in the first place is that it sat *outside* the table as a raw blob. Measured on
`scripts/demo-water.py`: **424,799 B → 154,867 B**, the hatch section down to the
4 bytes of its table index, its 270,000-byte payload fetched once and cached.
A bundled `.fcsd` capture sets no blob sink and so stays self-contained.

**1b landed as snapshot v28** — measured **154,867 B → 38,601 B** on the same
scene, mesh table down from 119,749 B to 3,483 B. Three things make it work:

- **`cacheId` leaves the hashed bytes.** It is emitted first by `writeMesh` and
  is a bare counter (`SoFCVertexCache.cpp:252`), so hashing the chunk verbatim
  would mint a fresh key every re-tessellation, identical output or not.
- **The key is memoized on `cacheId` anyway** — as a *memo* key it is exactly
  right, because a cacheId names one content for the life of the process. A hit
  costs no serialization, no hash and no copy; a re-tessellation misses and is
  hashed once. This is what keeps the cost proportional to changed geometry
  instead of moving it from the network onto the CPU. The memo spans two
  publishes and expires when `retainBlob()` reports the blob gone, so it can
  never name something the server has dropped.
- **Batched pull.** 58 chunks over individual requests would be mostly round
  trips, so keys are packed into batches up to a byte budget
  (`kRequestBytes`, 256 KB) and a filled batch is issued while the next one
  fills. A chunk larger than the budget is not a special case: it simply ends up
  alone in its batch. Textures keep their own `GET /blob?key=` — one large
  resource per request is what an image wants, and it stays individually
  cacheable by the browser, which a POSTed batch is not.

Content addressing also dedups geometry that `cacheId` never could: this scene's
**71 mesh entries carry only 58 distinct contents** (18% redundant), and both the
fetch and the GPU upload collapse onto one id per key. Viewer-side ids are
assigned per key from a private high range, so they cannot collide with the ids a
bundled snapshot carries inline.

### 1c is not needed

Phase 1c assumed the camera in the payload made orbiting republish. Measured, it
does not, and the phase was dropped rather than implemented:

- **Nothing consumes it.** The viewer navigates client-side and never reads
  `viewMatrix`/`projMatrix`; it even recomputes `autozoomScale` from its own
  camera each frame (`wasm/main.cpp:1524`), overriding the snapshot's. The block
  is 140 B, 0.5% of the payload.
- **Camera motion does not mark the scene dirty.** The camera arrives as
  `render()` arguments; no setter touches `sceneDirty`, and the publish gate is
  `dirtyChanged`.
- **Idle does not republish at all** — zero version bumps over 20 s on the
  animated demo scene, and two consecutive payloads are byte-identical. The
  earlier "two publishes differ in 28 bytes" reading was a settling artifact of
  startup, not steady-state churn.

One camera-derived republish path does exist and is worth remembering:
`setAutoZoomScale` marks the scene dirty when the scale changes *and* the feed
holds autozoom draws, so zooming (not orbiting) can republish the whole draw
table for a screen-space size the viewer discards. Worth decoupling from the
publish gate if it ever shows up in practice.

### 2a — material dedup (v29)

Materials move into a table and draws carry an index, exactly as meshes,
textures and shaders already did. Equality is the **serialized bytes**: exact by
construction, and it cannot drift out of step with the format the way a
hand-written comparison over ~60 fields would. Measured on the same scene,
38 distinct materials back 71 draws: **38,601 B → 27,669 B**, scene draws
8,176 → 2,128 B and overlay draws 24,702 → 6,390 B.

That made the table the largest section, so the two things §1 named were done
with it:

**Trimming the records (v30).** The format documents three matrices as valid
only when a companion flag says so — a draw's model matrix, a material's texture
matrix, an autozoom entry's matrix — and wrote all of them unconditionally.
Each is now preceded by its flag and written only when it means something, and a
material writes the clip planes it uses instead of all `MaxClipPlanes` slots.
The table fell from 13,428 B to **7,412 B** (353 → 195 B per material) and the
draws shrank again. The skipped matrices are not default-initialized in
`Renderer.h`, so the reader fills them with the identity rather than leaving a
consumer that ignores the flag reading noise.

**Serving the table out of band (v31).** Deduplication makes the table the same
kind of object as a mesh chunk — large, shared, and unchanged between most
publishes — so it is written as its content key and served like one. The whole
table is a single blob rather than one per material: at ~195 B a material, a
40-byte key plus a request would barely be an improvement. It is rebuilt and
hashed every publish (that cost is unavoidable, the table has to be built to
know it is unchanged), but its bytes only leave the process when the hash moves,
because the publisher calls `retainBlob()` first.

Two rules keep the caches honest, both learned from a browser profile that
failed permanently while incognito worked (the store outlives any reload, so
nothing short of clearing it recovers):

- **The key covers the format.** Each chunk starts with a layout version
  (v32) that is part of the hashed bytes, so changing a chunk's layout changes
  every key and no cached payload can be read by a parser that disagrees with
  it. A cached payload that still fails to parse means the store is stale or
  damaged, so the viewer clears all of it and reloads — it is a pure
  optimization, and a half-trusted cache is worse than none.
- **A snapshot missing any payload is never applied.** Draws of a mesh that
  never arrived still point at it, and the backend reaches a mesh through the
  shadow, outline and segment-instancing paths as well as the guarded submit
  one. Textures are the exception, since a draw renders untextured.

Because the table can now arrive after the draws that use it, a draw carries its
table index (`DrawCall::materialIndex`, load side only) and the fill takes the
snapshot **at call time** — a staged snapshot is moved before it is applied, and
a captured pointer would not survive that.

Together: **27,669 B → 10,834 B**. The reference scene is now 3,483 B of mesh
keys, 3,126 B of overlay draws, 1,936 B of scene draws and 1,752 B of texture
keys — no section is dominant, and what remains is the per-draw record itself,
which is what §4's compact draw record and the manifest tree address. Phase 2 is where the
complexity lands, and it is what the thin client needs — the phases before it
shrink a small scene, but only the manifest tree makes a *big* model tractable.

### 2b — the manifest tree, in three slices

2b is where the format stops being one document with tables in it and becomes a
tree of independently addressed chunks. That is too much to land in one step, and
the three pieces have genuinely different risk, so they are separated:

| Slice | Change | What it buys |
| --- | --- | --- |
| 2b-1 | draw groups become content-keyed chunks; leaves keyed individually | an unchanged object costs 53 B a publish |
| 2b-2 | root delta-encoded against the version the viewer holds, with history and resync | steady state independent of model size |
| 2b-3 | progressive fidelity off the L0/L1 boxes | the model frames and roughs in before geometry lands |

**The unit is a draw group, not an object.** The scene feed is grouped by
`objectKey`, but selection, highlight and overlay feeds are draw lists too, and
none of them wants a mechanism of its own. So one encoding — *local key lists
plus draws that index into them* (§4's L1) — is used in two placements: written
out of band under its content key (a scene object, an overlay), or inline
(selection and highlight, which are per-viewer, volatile and a handful of draws
after a pick — a chunk and a round trip for them would cost more than they
carry). The placement is the only difference; the bytes are the same.

**Keyless draws stay inline.** `objectKey == 0` means the producer could not
name the draw, so those draws have no identity to be cached under and no
grouping that survives a republish. Bucketing them together would build exactly
the perpetually-dirty chunk §3 warns about — one chunk that changes whenever any
unnamed draw does, invalidating all of them. They are written inline in the root
instead, which costs their full record every publish and is the honest price of
not knowing what they are. If a real model turns out to have many, the fix is to
name them at the producer, not to bucket them here.

**Materials become individually keyed, undoing v31's single table.** The whole
table as one blob was right while draws indexed into it globally; under §3 it is
exactly wrong, because inserting one material renumbers the indices in every
object manifest that follows it and invalidates every one of them. So a material
becomes a leaf like a mesh: its own content key, pulled through the same batch
path (at ~195 B it is squarely a *many and small* payload). Shaders follow for
the same reason — and they carry compiled binaries, so their dedup matters more
than their count suggests. Textures already work this way. The v31 table blob
survives only for the bundled-snapshot path, which has no keys at all.

The cost of individual keys is 44 B of reference per leaf against 195 B of
material, which pays for itself the moment two objects share a material and pays
enormously the moment an object is unchanged. The count of round trips does not
grow: leaves are batched by byte budget, and a batch is one request whether it
holds one key or four hundred.

**2b-1 landed as snapshot v33.** The format now has *two* layouts, chosen by a
flag after the version and, on the writer's side, by whether it was given
anywhere to put chunks: the monolithic one is untouched and is what a bundled
`.fcsd` capture stays, and the manifest one is what the stream uses. Measured on
`scripts/demo-water.py`: **10,834 B → 1,404 B**, with the draws, the materials,
the shaders and the overlays all behind content keys and the root reduced to the
volatile config block plus one 53-byte entry per object.

Three things are worth recording about the implementation:

- **The two layouts share their code, not their shape.** Every record that
  references something it does not contain — a material naming a texture, a
  draw naming a mesh, the config block naming the environment image — goes
  through a small reference indirection (`RefWriter`/`RefReader`), and only that
  indirection differs between the layouts. Without it the config block alone
  would have had to be written twice and would have drifted.
- **A leaf is described where it is used.** With no global texture table, an
  image's ~70-byte header repeats in each material that names it. Its *payload*
  does not: the writer memoizes what it has handed over this publish, so the
  pixels leave the renderer once however many materials mention them.
- **The staging is indexed, not appended.** A group's draws land at the slot the
  root named them at and a `finalize()` pass stitches them into the feeds, so
  the order a backend sees is the producer's order and not the order chunks
  happened to come back in — which would otherwise vary run to run and make
  pixel comparison meaningless.

`tests/src/Gui/SceneDump.cpp` covers both layouts headlessly (no GL context, no
document): round trips, and the two properties the phase exists for — an
unchanged scene republishes without minting a key, and repainting one object
mints exactly its own manifest and its new material, touching no other object's
chunk and no mesh at all.

**What 2b-1 does not do** is delta-encode the root. Every publish still names
every object, and `scripts/demo-many.py` is what says how much that costs:
a grid of `COUNT` boxes whose geometry deduplicates to a handful of keys, so
whatever remains scales with the object count and nothing else. Measured at
`COUNT=200`, the root is **17,118 B — 79 B per object**, and two consecutive
publishes of an untouched scene are byte-identical. The entry is bigger than
§4's 53 B estimate because 44 of those bytes are the manifest key as 40 hex
characters plus its size; storing the digest raw would take a quarter off, and
delta-encoding removes it entirely, which is why that is the phase and this is
only the measurement.

Extrapolated, 10k objects cost ~770 KB **per publish** — on every selection
pick. That is the whole reason 2b-2 exists. Nothing *behind* those names moves
unless it changed. That
split is deliberate: 2b-1 is a serializer change with a mechanical viewer
counterpart, while 2b-2 adds server-side history and a resync path, which is
where the protocol can actually go wrong. Landing them together would make a
sync bug indistinguishable from a chunking bug.

## 12. Open questions

- **Manifest history depth** — how stale a viewer may be before a full resync,
  and what that costs in server memory.
- **Very large single objects.** The mesh level bounds *geometry* churn and
  decouples submission from it, but an object with tens of thousands of draws
  still re-sends its whole draw list when one draw changes. Sub-bucketing the
  draw list may be needed; measure first.
- **Masking proxies out of AO** (§6) — the one live consequence of putting them
  in the depth prepass. Stencil bit or id channel; decide by looking at it.
- **Proxy default timings** — grace and fade are parameters (§6), but their
  defaults still want tuning against a real model over a real link.
- **LOD error metric and budget** (§7) — deferred; likely parameters too.
- **Root manifest at very large object counts.** 100k objects make even the
  delta's object list non-trivial; paging the object list into content-addressed
  pages is the escape, if measurement demands it.
- **Overlay feeds** (NaviCube, axis cross) are static and per-viewer, not per
  document; they may belong in the viewer bundle rather than the stream at all.
