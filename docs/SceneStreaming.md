# Scene streaming — content-addressed delta publishing

Status: phases 1, 2, 3 and 4a implemented; 4b and 5 are specification. The leaf tier is done — textures (snapshot v26,
`c4337904f9`), the hatch image (v27, `ebddfc0fdd`), mesh chunks with batched
pull (v28) and the deduplicated material table (v29-v31) — and so is the
manifest tree (§4): the scene is cut into content-keyed groups by `objectKey`,
with materials and shaders keyed individually (v33). That took the reference
scene from **425 KB to 1.4 KB** per publish. Of the delta sync (§5), the format
and the consumer are done (v34), the versioning it rests on is done (v35: a
publish is numbered by its producer and names the backend run that numbered it,
which both transports state back on request), and so is the publishing half —
the server keeps a bounded history and narrows the published root to what each
viewer is actually missing, taking the 200-object benchmark from 17,146 B to
1,027 B a publish. §6, progressive application, has been respecified around a
**fidelity ladder** — a draw names the best rung it holds, from a synthesised
bounding box through LOD levels to the full mesh, and climbs it through the
ordinary update path — which subsumes what §7 had kept separate as a future LOD
mechanism. It is implemented: a publish is drawn while it is still arriving, and
a mesh that has not landed is a box on its bounds rather than a hole. §6's
prioritisation is implemented too (phase 4a): chunks carry the objects that want
them, and the viewer fetches them in the order its own camera implies, so the
visible part of a model loads first. The ladder also runs *backwards* (phase
4b): a viewer holds its geometry to a budget by giving payloads back, and what
it gives back is drawn at the rung below rather than as a hole, so a model
larger than memory is a model drawn coarse in the distance. What remains is
LOD, and making the per-arrival assembly incremental.

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
what lets the viewer act on a mesh it does not yet have — stand a box in for it
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
difference, and a bounded history decides when that is possible.

```
viewer  → GET /scene?v=<version>&s=<session>       ← on the upgrade request
server  → root payload: { manifestVersion, sessionId, inline state,
                          objects changed since <version> }
        + the chunks new in this publish (optimistic push)
viewer  → { cmd: "getChunks", keys: [...] }        ← batched, one round trip
server  → chunk payloads
```

**What the viewer holds is stated in the request, not in a hello.** It is
available at handshake time, so the push loop can act on it without a round
trip, and it is the same query the polling transport already used. A viewer
that reconnects still current therefore costs no payload at all.

**A version means nothing without the run that issued it.** The backend
restarting resets the counter, so a viewer carrying v12 across a restart would
otherwise be told it is current, or worse, be handed a delta based on a publish
that never happened. Every root names its `sessionId` (v35) and every request
echoes it back; a mismatch means the viewer holds nothing. Its *chunk store*
survives, though — keys are content hashes, so the new run republishes the same
bytes under the same keys and the resync lands on an almost-warm cache.

**The producer publishes a full root once; the server narrows it per viewer.**
A viewer is routinely more than one publish behind — publishes fire on change
and several can land inside one push tick, since the loop sends the current
scene rather than every version of it. So a delta against the previous publish
would seldom be applicable, and encoding one per viewer would mean serializing
the scene once per connection.

Neither is necessary, because a full root and a delta differ *only* in which
objects their object list names. The writer records where that list sits
(`RootSpans`), and the server replaces it: it keeps a bounded ring of what each
recent publish changed, merges the entries a viewer missed by last-wins on
`objectKey`, and splices the result into the payload it already holds. The
outcome is byte-identical to what the serializer would have written from the
scene — which is asserted, not assumed.

The merge names only live chunks, because the last write for an object is its
current key. So a viewer catching up is never sent chasing a chunk that has
since been retired, and the two-generation blob roll stays correct as it is.

The ring depth is therefore a bandwidth choice and never a correctness one: a
viewer that has fallen out of it, or holds nothing, or came from another
session, gets the payload whole — the same bytes, minus the narrowing.

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

The monolithic `SceneDump` serializer stays as the ultimate fallback and remains
the format of bundled `.fcsd` captures, which must be self-contained and so
carry neither a version nor a session.

Measured on `scripts/demo-many.py` at 200 objects, where an edit moves one of
them: the full root is 17,146 B and a viewer that is current is served 1,027 B —
**94% less**, and flat in the number of objects it did not touch rather than
79 B each.

## 6. Progressive application

Today a publish applies only once every key it names has resolved
(`resolvePending` → `commitResolved`, `wasm/main.cpp:2433`). That does not
scale: a viewer cannot block on 20 MB before it draws anything.

The fix is not a second rendering path for things that have not arrived. It is
to notice that **which mesh a draw names is a choice, not a given**, and that
the manifest already tells the viewer enough to make a poorer choice while it
waits for a better one. Progressive display is then not a mode — it is the
ordinary scene, built from the ordinary manifest, refined by the ordinary
update path.

### The ladder

Every mesh entry defines a ladder of drawable stand-ins, coarsest first:

```
[ unit box @ mesh bbox ]  <  [ lod0 ]  <  …  <  [ full mesh ]
```

One rule, applied where the scene feed is built:

> **Draw the best rung that is resident. Ask for a better one. Rebuild the
> draw when it lands.**

The bottom rung is free. A unit box is the same 24 vertices for every mesh in
every scene, so it is one chunk, one key, one GPU upload for the whole session;
the per-mesh variation is the bbox transform, which the draw record already
carries. The producer never sends it and the viewer never fetches it — it is
synthesised from the bounding box L1 carries anyway (§4).

So the two manifest levels give three fidelities, each a strict refinement of
the last:

1. **L0 only** — the object's own bbox is known. One box per object.
2. **L1 in hand** — every mesh bbox is known. The object's single box resolves
   into one box per mesh, so its silhouette is roughly right long before any
   geometry exists.
3. **Mesh chunks arrive** — each box is replaced by real geometry, one mesh at
   a time, in whatever order the prioritiser chose.

A useful consequence: the initial camera fit runs off L0 bounding boxes, so the
view frames the model correctly *before* the first triangle exists and does not
lurch as geometry streams in. Today's `applySnapshot(fit)` fits to loaded
geometry and would re-fit repeatedly under streaming.

### Boxes are meshes, not proxies

The earlier design for this section made a pending box a *proxy*: a parallel
draw list with its own lifetime, its own exclusions, and an invariant written
specially to stop a placeholder being mistaken for the mesh it stood for. The
ladder removes all of it, because a box is simply a mesh:

- **Identity is structural.** The box has its own content key, so the
  backend-facing id derived from that key is the box's id and never the
  pending mesh's. A backend that keys GPU uploads by id cannot be handed a
  placeholder to fill in later, because nothing ever claims to be the thing it
  is standing in for. What was invariant 4 is now a consequence of the format
  rather than a rule to obey.
- **It carries the object's real material.** The coarse scene reads as the
  model in the right colours, not as grey scaffolding, and no separate
  "pending" appearance needs designing.
- **Refinement is a normal update.** Replacing a rung is the same operation a
  delta performs on a changed object (§5), through the same code.
- **It runs backwards.** Under memory pressure a mesh descends the ladder
  instead of vanishing — an eviction policy the proxy design could not express
  at all.

The one thing a box must still announce is that it is a stand-in, so a draw
carries a single **`standIn` bit**. It buys three behaviours and no machinery:

**Depth, yes.** Stand-ins write depth, including in the prepass. A bbox is a
conservative bound of the geometry it replaces, so writing depth keeps the
scene behind a coarse object from showing through it and then popping when it
resolves — the model reads as solid throughout loading, which is the whole
point of drawing anything. The cost is that a box over-occludes: transiently it
hides slightly more than the real mesh will.

**Shadows, no.** A box casts a box-shaped shadow, which is not conservative in
any useful sense and reads as a modelling error rather than as progress. The
same applies to section capping, hidden-line and outline passes: those are
shading, not occupancy.

**Tinting, optional.** The bit is also what a "show me what is still loading"
view mode would key on. Off by default — the material is the better answer.

> **Coupling to settle in implementation.** AO derives from the depth prepass,
> so keeping stand-ins in it means AO sees them and darkens their edges unless
> they are masked out — the `standIn` bit as a stencil write, or an id channel
> the AO resolve tests. Whether the transient darkening is worth masking is a
> judgement to make against a real model; the mask is cheap, so the default
> should be to exclude them and relax it only if it looks better.

### Picking follows the rung

A box has no correspondence to the elements of the mesh it replaces, so it
cannot answer an element query — but it can answer truthfully at a coarser
grain. **Picking a stand-in selects the whole object**, never a sub-element:
that is exactly what the box knows, and it is what a user reaching for a
half-loaded model means anyway.

Precision therefore follows the rung, and there is never a wrong answer, only a
coarser one:

| Rung | Pick resolves to |
| --- | --- |
| unit box | the object |
| LOD mesh | the object today; sub-elements once a level carries an element map (§7) |
| full mesh | sub-elements, as now |

Selection state is held per object and per element path, so a selection made
against a box stays valid when the real mesh lands — the object key does not
change, only what can be named beneath it.

### Timing

A mesh that arrives quickly should not flash a box for one frame. That is not a
special case either: it is a **rung eligibility** rule applied where the choice
is made. The box rung becomes eligible only after its mesh has been outstanding
for a grace period, and a rung change cross-fades rather than cuts. Both are
user-configurable view parameters in the `Render_*` family:

| Parameter | Meaning |
| --- | --- |
| `Render_CoarseGeometry` | use coarse rungs at all (default on); off = draw nothing until the real mesh lands, today's behaviour |
| `Render_CoarseGrace` | ms a mesh may be outstanding before its box becomes eligible |
| `Render_CoarseFade` | ms to cross-fade when a draw changes rung |

Defaults want tuning against a real model on a real link rather than being
guessed here; the point of making them parameters is that the tuning does not
need a rebuild.

### The trigger is chunk arrival

Refinement needs no invented event. A chunk landing is already an event the
fetch layer raises (`blobResolved`, `wasm/main.cpp:2079`/`2201`); what is
missing is only the ability to answer *what did that chunk change*.

So `SceneObjectModel` maintains a **reverse index, key → the objects that
reference it**, built as it merges manifests. Arrival marks those objects
dirty; the dirty set is drained once per frame and each object's draws are
rebuilt by the same per-object path a delta uses, splicing its slot rather than
re-serialising the feed. The index is needed for eviction regardless, so the
ladder is not what pays for it.

> **As built.** The reverse index exists as of phase 4a, but as the fetch
> order's input rather than as an assembly optimization: the assembly pass
> still rebuilds the whole feed from the model on every arrival, and
> measured against `?noprogressive`, the per-arrival work in aggregate is
> what separates a 7.5 s load from its 1.4 s of bytes. Direct
> instrumentation has since split that aggregate: **the assembly pass
> itself is ~150 ms of a 5.4 s load** — the bulk of the progressive
> overhead sits elsewhere in the per-arrival path, around the apply and
> re-upload. Incremental assembly therefore remains worth building but is
> sequenced *after* phase 5 (LOD), not ahead of it (decided 2026-07-29).
> See §11 phase 4a.

Two consequences for the consumer, and they are the only behavioural changes:

- **Commit early.** `commitResolved` must run on the first useful arrival with
  coarse rungs in place, and again as rungs improve, instead of waiting for
  `resolvePending` to report nothing outstanding.
- **Coalesce.** Rebuilding on every arrival is O(scene) × O(chunks). Dirty
  objects accumulate and the rebuild runs once per frame.

### Prioritisation

Because L0 is small and carries bounding boxes, the viewer holds a spatial
index *before* it holds any geometry. Fetch order follows the view frustum —
nearest and largest-on-screen first — off-screen objects can be deferred
entirely, and eviction can be distance-based. This is also the natural hook for
the occlusion-culling work.

Two things this needs that are not obvious from that sentence, both learned by
building it (§11 phase 4a):

- **A chunk is wanted by objects, plural.** Content addressing means one
  material backs a whole scene and one mesh backs every instance of a part, so
  a chunk's priority is the best of the objects that reference it. Taking the
  first — whichever manifest happened to name it — ranks a chunk the scene is
  waiting on by the least important object that wears it.
- **The view's own chunks are a barrier, not a priority.** The overlays — the
  navigation cube, the axis cross — belong to no object, so nothing about the
  camera ranks them and they simply come first. Sorting them first is not
  enough: that decides only the order requests are *issued* in, and a window
  that issues sixty-four at once has them all sharing the link, so half a
  megabyte of cube arrives behind a share of tens of megabytes of model. So
  nothing the model owns is asked for while anything the view owns is
  outstanding.
- **Priority is per byte, not per chunk.** What a fetch order allocates is the
  next byte, and the appearance layer costs a thousandth of what the geometry
  does while lifting a whole rung. Ranked by projected size alone, the colour
  of the entire model queues behind the geometry of its nearest few objects.

Note where that decision lives: **with the viewer**. The producer publishes the
full manifest, which is only keys and therefore cheap, and each viewer climbs
the ladder on its own camera. A producer that instead published coarse-then-fine
would have to hold a refinement sequence per viewer — several viewers looking
at different parts of a model want different orders — which is exactly the
per-viewer server state invariant 7 forbids.

## 7. Level of detail (future)

LOD is not a second mechanism. It is the middle rungs of §6's ladder, and the
consumer logic is already written: pick the best resident rung, fetch better,
rebuild on arrival. What changes is only where a rung comes from — the box is
synthesised by the viewer, an LOD mesh is generated by the producer — and that
difference stops at the format.

A mesh entry generalises from one key to a list, coarsest first:

```
meshes: [ bbox[6], lods: [ { key, error } ] ]
```

No format break is needed to get here: per-mesh bounding boxes already exist
for the box rung, and content addressing already makes each variant an
independent immutable chunk. The viewer picks a level from the bbox's projected
size and its budget, fetches that chunk, and may refine later — cached and
evicted like any other chunk, with no invalidation, because no chunk ever
changes. Switching level costs nothing on the wire that was not already paid: a
level the viewer kept is a key it already holds.

What `error` measures and how a level is chosen are deliberately left open
until there is a real model to tune against; the likely answer is that both
become `Render_*` parameters like the timings above, so the policy is
adjustable without a rebuild.

**Levels are generated on demand, which the format has to admit.** Tessellating
every object at N deviations on every publish is exactly the cost §10 warns
about, moved from the network to the CPU and multiplied. But a level nobody has
generated has no bytes, therefore no key (invariant 1), and therefore cannot be
asked for the way every other payload is: a `key` request is a request for
*bytes*, and this is a request for *work*. The two cannot share a channel.

Three things follow, and 4c has the consumer-side shape of them already
(`Render::LevelRequest`, `RungProvider::generate`):

1. **A level entry's key is optional.** Present means fetchable now; absent
   means declared possible but unbuilt. `lods: [ { error, key? } ]`.

   > **As built (5b, v36).** The ladder rides the mesh *reference*: a
   > deferred mesh entry is a list of levels, coarsest first, each
   > `{error, key?}`, closed by the exact mesh at error 0, always keyed.
   > `error` is relative to the mesh's own diagonal, matching the
   > generator's construction (level L clusters on cells of
   > diagonal/(8<<L), so its error is 1/(8<<L)) — no per-mesh bbox needed,
   > since the draws already carry world bounds and a relative error
   > converts at selection time. Meshes over 64 KB declare two coarser
   > levels ahead of any generator existing; the consumer records the
   > ladder on the deferred entry (`DeferredChunk::levels`) and still
   > fetches the finest built level, so behaviour is unchanged until
   > selection (5d) consults it. The chunk layout version rose with it
   > (`kChunkVersion` 3), so every key changed and no cached chunk can be
   > misread across the format change.
2. **A level is named by what would produce it** — its source geometry's
   content identity plus the level index — not by what it will contain. That
   token is stable across publishes and identical for every viewer wanting it.
3. **The answer arrives as an ordinary publish.** Generation completes, the key
   is announced in the next root delta (§5), and from there it is a chunk like
   any other.

Invariant 7 needs amending for this: "no per-viewer state" survives intact,
because the request is keyed by content and idempotent, but "answers from its
store alone" does not — the server gains a work queue. That queue is the
natural client of `docs/ComputeBoundaries.md`'s out-of-process geometry.

The desktop gets this for free rather than as a second mechanism: there,
"generate level L" *is* a tessellation job at a deviation, cached per
`(TShape, level)` — a direct extension of the sharing in
`docs/TShapeRenderCache.md`. Which is the argument for doing LOD once, behind
the seam, rather than in the viewer first and porting it after.

The real work LOD adds is producer-side rather than protocol: generating the
variants (OCCT tessellation at several deviations, or decimation), and
extending the TShape-level tessellation sharing in `docs/TShapeRenderCache.md`
to cache per level. That work has a natural trigger of its own — a refinement
job finishing is a real event, not a synthesised one — which is why producer
side refinement is right for LOD and wrong for boxes: republishing when
tessellation improves is a publish like any other (§5), and the out-of-process
geometry work in `docs/ComputeBoundaries.md` is where those jobs will run.

Two things fall out for free once the rungs are real meshes rather than boxes:

- **Element picking on a level.** An LOD mesh that carries an element map can
  answer sub-element queries, so picking precision improves with fidelity
  instead of stepping from "the object" straight to "everything" (§6). Whether
  a decimated mesh can carry a faithful element map was the open part, and the
  answer is yes, by construction rather than by reconstruction
  (`MeshSimplify.cpp`): clustering never welds across element boundaries —
  vertices gather per face, per edge, per point element — so every output
  primitive belongs to exactly one source element and the part tables carry
  over index for index, an element that collapsed entirely keeping its slot as
  an empty range. What keeps the elements sewn together anyway is that a
  representative's *position* comes from a grid the whole mesh shares: the
  coincident soup vertices two adjacent faces both carry land in the same cell
  and read back the same average, so decimated faces still meet bitwise where
  their tessellations met. Normals average within one element only, so a
  crease stays a crease — sharper shading than global welding gave, not just
  equal. Sub-element selection is what CAD operations run on, which is why
  this is built into the generator rather than deferred with the metric.
- **Eviction becomes graceful.** Memory pressure walks a draw down the ladder
  to a cheaper level and finally to its box, rather than choosing between
  holding geometry and showing a hole.

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
   then it names a coarser rung of the same ladder, and **a stand-in never
   carries the identity of the mesh it stands in for**: it is a chunk with its
   own key, so the backend id derived from that key is its own and a
   placeholder can never be filled in later under the pending mesh's id.
5. A stand-in states occupancy, not shading: it writes depth (including the
   prepass) and is excluded from shadow casting, section capping, hidden-line
   and outline passes (§6).
6. Fidelity never lies about identity. A stand-in picks as the whole object; a
   rung answers sub-element queries only if it carries an element map (§6, §7).
7. The server answers any chunk request from its store alone, with no per-viewer
   state.
8. A bundled capture is self-contained: chunking is a property of the transport,
   never of the format (as v26 already establishes for textures).
9. Content keys are computed once per distinct content, never per publish (§10).

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
| 2b-2 | root delta-encoded, history + resync (**format + viewer done**, v34) | ~1 KB steady state |
| 2b-3 | commit early, assemble per arrival (**done**) | the model draws while it arrives |
| 3 | the box rung: per-mesh submission, `standIn` bit, coarse picking (**done**) | model appears while it loads |
| 4a | reverse index, frustum-ordered bounded fetch (**done**) | the visible part of a model loads first |
| 4b | ladder-descending eviction (**done**) | a model larger than memory |
| 4c | policy to shared code, adaptive budget, acquisition seam (**done**) | one ladder for both tiers |
| 5a | the decimation generator, element maps preserved (**done**) | a middle rung exists to build |
| 5b | the level ladder in the format: declared levels, optional keys (**done**, v36) | a level can exist before it is generated |
| 5c | generation on demand: `LevelRequest` → producer work queue (§7) | the middle rungs get bytes |
| 5d | level selection, and eviction descending level by level | large models *fast* |

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
| 2b-3 | commit early and assemble on every arrival | the model draws while it is still arriving |

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

### 2b-2 — the delta, so far

The format and the consumer are done; the publisher does not yet send a delta.

**Snapshot v34** gives the manifest root a `manifestVersion` and the
`baseVersion` its object list is encoded against. With `baseVersion` 0 the list
is complete; otherwise it carries the objects that changed and the keys of the
ones that went away, and nothing else. An object counts as changed exactly when
its group manifest key changed — the key covers the whole group, bounding box
included, so there is nothing else that could have moved. Both lists are sorted
by `objectKey`, which makes the diff one linear pass.

**Objects are now ordered by identity, not by first appearance.** A delta names
only what moved, so the consumer reassembles the feed from what it already
holds, and the order has to be one both sides reach without being told it —
and the same whether a scene arrived as one root or as a root and a chain of
deltas. This changes the draw order the backend sees; the backend sorts draws
for rendering anyway, and the reference scene renders identically.

**The consumer holds a `SceneObjectModel` across publishes**, because a snapshot
is one publish and a delta describes only part of one. `applySceneObjects()`
merges a publish into it and rebuilds the scene feed; it refuses a delta against
a version the model does not hold, since the objects such a delta says nothing
about are exactly the ones it assumes are already right. The viewer then asks
for a full root rather than applying anything.

What is left is the publishing half: the producer deciding full versus delta and
keeping the previous object list to diff against, and the server keeping the
bounded history §5 describes — the last full root plus the deltas since it — so
that a viewer connecting or resyncing mid-chain can be brought up to date
without the producer having to republish. Until that lands the publisher writes
full roots only, which every consumer can apply, so the wire is correct and
merely no smaller than 2b-1 left it.

### 3 — the box rung, as built

The ladder landed as §6 describes it, and it cost less than the section
allows for, because three of the mechanisms it budgets for turned out to be
consequences of what was already there rather than things to build:

- **The per-mesh bounds were already on the wire.** A draw record carries
  world-space `bboxMin`/`bboxMax`, so the middle rung needed no format change
  at all — no snapshot version, no new chunk type. The rung is chosen where
  the feed is assembled (`appendAtBestRung`, SceneDump.cpp).
- **Refinement needed no trigger.** 2b-3 already re-runs the assembly pass on
  every arrival and rebuilds the feed from the model, so a rung improves by
  the same path a delta takes. The reverse index §6 names is therefore *not*
  what makes the ladder work; it is a cost optimization (the rebuild is
  O(scene) per arrival) and is what eviction will need, so it stays with
  phase 4 where it is paid for.
- **Picking followed the rung by itself.** Element lookup resolves through
  the mesh's part tables, an empty table already means the whole object, and
  a synthesized box has no part table. No branch on `standIn` was needed.

Two rungs exist below the real mesh rather than one, and they are exactly the
two manifest levels: the root alone gives one box per object in the default
appearance, and the group manifest resolves it into one box per mesh in the
object's own colours. The first is what makes a model appear at all on a cold
load — without it a viewer holding the root still shows nothing until the
first group manifests and material tables land.

**One box per mesh, not per draw.** A mesh is named once per face part, and
every one of those draws carries the bounds of the same geometry, so a box
apiece is one silhouette drawn many times over. Only surfaces stand in: a box
over an edge or vertex mesh adds a second, solid silhouette and reads as
neither the wireframe nor the shape.

**The dropped passes live in the material, not in the backend.** `standIn` is
carried for picking and for a future "what is still loading" tint, but the
three behaviours §6 asks for are expressed by what the synthesized material
says — cast bit cleared, outline/hidden-line/capping cleared, depth and
shadow *receipt* kept. The caster predicate alone appears at eight sites in
the bgfx backend, and a rule stated once where the box is built cannot be
forgotten at one of them.

A pleasant accident: every stand-in shares one mesh id, so the backend's
existing instancing path collapses the whole coarse scene into few draws.

Measured on `scripts/demo-varied.py` at 200 objects (7 MB of geometry) with
the page throttled, `?stream` reporting each pass: the model appears at once
as 200 boxes, correctly framed, then climbs monotonically to 600 draws with
none coarse. It never shows less than it knows.

**Not done, and deliberately.** The §6 timing parameters
(`Render_CoarseGeometry`, `Render_CoarseGrace`, `Render_CoarseFade`) are not
implemented: a grace period and a cross-fade are worth tuning against a real
model over a real link, and on loopback every rung is invisible anyway. The
AO coupling §6 flags — stand-ins in the depth prepass darkening their own
edges — has not been judged against a real model either.

### 4a — frustum-ordered fetch, as built

The ordering itself is the small part. What the phase turned out to be about
is that **a fetch order only exists if the fetch is bounded**, and every
mistake below is some version of paying too much for that bound.

Ask for everything the moment the root names it — which is what phases 1-3
did — and the sort decides nothing: the requests are all issued in the same
tick and arrive in whatever order the network answers them. So the viewer
keeps a window of outstanding requests and re-sorts what is left every time
one lands. Three properties of that window were each measured the hard way,
on `scripts/demo-varied.py` at 200 objects (60 MB of chunks) over loopback,
against the same scene fetched with no ordering at all (5.3 s):

- **Count requests, not bytes.** A request is a socket and a round trip
  whatever it carries; counting bytes throttles a scene of large chunks and
  leaves a scene of small ones unbounded. Windows of 8, 32 and 64 requests
  took the scene 65 s, 17 s and 7.7 s — but see below for what that is
  actually measuring.
- **Never cut a batch short.** Stopping mid-fill sends quarter-full requests,
  which costs round trips *and* arrivals — each arrival re-runs assembly — so
  the window closes only between batches, and whatever is left half-packed is
  flushed at the end of the round rather than left to the deferred flush.
- **Do not wait on a timer.** That deferred flush is a `setTimeout(0)`, which
  a browser that considers the page backgrounded clamps to a second. With a
  window in front of the queue that second is paid once per batch instead of
  once per load: 4.6 s against 58 s, from one call to `flushBatch`.

**What that window sweep was actually measuring.** Not the fetch. The
`?nofetchorder` and `?noprogressive` flags exist to take one variable out at
a time, and with the display held back the same three windows fetch the
scene in **1.61 s, 1.53 s and 1.41 s** — the bound costs almost nothing, and
the ordered fetch at 1.41 s is the unordered fetch's 1.44 s. The eightfold
spread appears only with the display on, because **every arrival
re-assembles the feed and re-uploads it, and a wider window folds more
arrivals into one pass**. The four corners, on `demo-varied.py` at 200
objects over loopback:

| | usable (whole model in colour) | fully refined |
| --- | ---: | ---: |
| ordered, progressive | 2.5 s | 7.5 s |
| `?nofetchorder` | 0.24 s | 3.1 s |
| `?noprogressive` | — | 1.4 s |
| both | — | 1.4 s |

Two things follow. The window is wide for coalescing rather than for
throughput, so it can be narrowed — sharpening the order — exactly as far as
the per-arrival cost of showing a scene is brought down. And **that cost,
not the fetch, is what a streamed load spends its time on**: the
per-arrival path of §6. An earlier reading that put it at 116 ms was taken
over twenty passes on a throttled link and did not include the apply; it
was wrong. Measured directly afterwards, the O(scene) assembly pass itself
is ~150 ms of a 5.4 s load — the remainder of the per-arrival cost is the
apply and re-upload around it, which is what "incremental" has to reach to
matter, and why that work is sequenced behind phase 5 rather than ahead of
it.

The concurrency ordering gives up is real but small: 1.41 s against 1.44 s
unordered. That is the trade, and it is the right way round for what the
phase is for — a model small enough to finish in a second does not need a
fetch order, and one that does not finish in five minutes is the case where
fetching the visible part first is the whole difference.

**Priority is projected size per byte, over the best owner.** Two corrections
to the obvious formula, both of which were bugs first (§6):

- Ranked by the object that happened to name a chunk first, 10 of the scene's
  40 deduplicated materials sat behind small distant objects, and since a
  draw is only taken once its appearance is resident, *every* object in the
  scene stayed a grey box for the whole load. The index therefore records
  every object that references a chunk, not the first.
- Ranked by projected size alone, each object arrived complete in turn and
  the rest of the model stayed at its bottom rung: the appearance layer,
  which is a few hundred bytes per material and lifts every object that wears
  it, queued behind hundred-kilobyte meshes. Dividing by size put the whole
  model in its own colours at 0.2 s instead of >20 s, and it needs no notion
  of what a chunk contains — the appearance layer simply *is* the small one,
  which is the same size-decides-policy rule the deferred list already runs on.

**Overlays are fetched to completion before any of the model.** They belong
to no object, so the ordering rule has nothing to say about them beyond
putting them first — and measured, first was not soon enough: twenty seconds
into a throttled load the model was a third refined and there was still no
navigation cube and no axis cross on screen. Ranking decides issue order, and
sixty-four simultaneous requests share the link whatever order they went out
in. Making the view's own chunks a barrier — no owned chunk asked for while an
unowned one is outstanding — puts the cube, the buttons and the cross up at
ten seconds instead, with the model behind them at its colour rung. It costs
nothing on a fast link (unchanged at 0.25 s to full colour, 7.2 s to fully
refined) because what it defers is a fixed few hundred kilobytes, and the
model's bottom rung is a box per object that the root alone already draws.

**The hold degrades rather than deadlocks.** A request that never completes
has nothing else in flight to notice it, so an unconditional barrier would
leave the model at its box rung for the rest of the session. It is released
after a grace period — measured from the last time *the view's own* chunks
made progress, which is the difference between a slow link and a stalled
one: a link slow enough to take seconds per batch keeps its cube first,
while a fetch that has produced nothing at all gives the model its bandwidth
back. Two things that were wrong before they were tested against a hung
request: the deadline has to schedule its own wake-up, since the arrival
that would have renewed it is exactly what is missing; and progress on the
*model* must not renew it, or the chunks a lapsed grace just released renew
the hold they escaped and the load stutters instead of stepping aside once.
Verified by hanging the first chunk request — which the barrier guarantees
is the view's — and watching the release fire once at six seconds with the
geometry refining a moment later.

**The index is recorded, not reconstructed.** A group manifest is the only
chunk named with an object beside it; everything below one — its meshes, its
materials, and through those their textures and shaders — is named while
something of that object's is being parsed, so the loader attributes them as
the references are read. A material chunk hands its owners to the textures it
names when it is parsed, which is the one case that cannot be attributed at
deferral time: the material is read long after the group that asked for it.

**The camera has to be pointing at the model before the first request.** On a
cold load the first ordering decision is over every object in the scene, and
it is made while the viewer is still holding nothing but the root — so the
fit runs when a publish is *staged*, off the boxes the root named, and a
`?cam=` parameter is consumed there rather than at the first commit.

Two defects fell out of fetching the overlays early, both of which had been
latent since progressive apply (2b-3) and were only ever hidden by the
overlays arriving last:

- **A texture whose pixels have not arrived is not a texture.** The bgfx
  upload expanded `width x height x components` bytes out of an empty vector
  — an out-of-bounds read, on screen as noise over the navigation cube. It
  now uploads a 1x1 white stand-in and remembers to replace it, which is the
  behaviour the deferral already promised for a texture that fails.
- **A feed with no coarse rung waits.** Overlay, selection and highlight
  draws are taken once and have no box to fall back to, so they are handed
  over only when the materials and textures they name are resident. The
  keyless draws are deliberately not among them: they stand in on their own
  bounds like any object, and holding them back would show less than the
  viewer knows.

**Benchmark flags.** `?nofetchorder` asks for every payload the moment it is
named, in publish order, as the viewer did before this phase; `?noprogressive`
holds the display back until the publish is whole, as it did before 2b-3.
They are deliberately independent — a benchmark that moves both at once
measures neither — and between them they separate "when is it usable" from
"when is it finished" from "how fast did the bytes arrive".

**Not done:** the frustum's stronger form — deferring off-screen objects
entirely rather than ranking them last. And the incremental assembly the
table above now points at.

### 4b — ladder-descending eviction, as built

A budget on resident geometry, and the ladder as the way to stay inside it: a
draw whose mesh is given back is drawn at the box on its bounds, which is the
rung it was on while that mesh was still in flight. There is no eviction state
and no invalidation — `release()` empties the mesh, the entry is re-armed with
the fill it resolved with, and the next assembly makes the same choice it made
before the payload ever arrived.

Three things had to be added around that, and each was a bug first.

- ⭐ **A payload given back must not be asked for again until it is worth
  materially more than when it was let go.** The first cut evicted anything
  scoring below the incoming chunk, which converges but does not settle: the
  chunk just released is now outstanding, is worth more than *something* still
  resident, and takes its place — round after round. Measured on the
  200-object scene under an 8 MB budget: **35,186 releases in forty seconds,
  22.5 s of it inside the fetch**. Recording the score each payload was
  released at, and demanding the same margin to re-ask for it, fixes it:
  **129 releases, 153 ms**, 123 of them during an orbit, which is the feature
  working.
  ⚠️ The version in between said the new information was "the camera moved at
  all", which is far too weak — a continuous zoom is a new camera several times
  a second, so the ping-pong returned as a churn of hundreds of kilobytes
  re-fetched, re-parsed and re-freed per second, and on a phone-sized canvas it
  **exhausted the wasm heap outright** (`memory access out of bounds` inside
  `malloc`). A release is reversed by value, never by motion.
- **Plan the eviction, then carry it out, once per round.** Per-candidate
  eviction sorted the resident set for every one of a few hundred outstanding
  chunks, which was most of that 22.5 s. And a half-done eviction is the worst
  of both — geometry given back and nothing fetched with the room it made — so
  the prefix of victims cheap enough to displace is measured before any of it
  is released.
- **A margin, not just an ordering.** `kEvictMargin` (1.25) is what makes two
  payloads a fraction of a percent apart stop swapping places on every
  re-projection.

⭐ **What to keep is not what to fetch next.** Residency first borrowed the
fetch order's score, which is value *per byte* — right for bandwidth, where the
next byte should go where it lifts the most (§6, and it is what colours a whole
model in a fraction of a second), and wrong for memory, where per byte a budget
buys many cheap distant meshes in preference to the one large near mesh the
user is looking at. Measured on the 200-object scene held to 8 MB, the refused
set was precisely the detailed geometry wherever it was: **494 resident chunks
averaging 16 KB against 146 refused averaging 146 KB**, so the foreground kept
its boxes while the distance was fully modelled. Ranked instead by the
projected size of the best owner, with no division by bytes: **234 resident
averaging 35 KB against 406 refused averaging 52 KB** — size no longer decides
membership, distance does.

Only geometry is weighed either way: a manifest or a material refused for want
of memory would strand every object under it at a rung it cannot leave.

**Both rankings are the same formula with one knob**: the best owner's
projected size, over the payload's bytes raised to a weight. 1 is strictly per
byte, 0 ignores size. Tunable per load as `?fetchweight=` / `?keepweight=`,
since the right value depends on the model and the link.

| | default | why |
| --- | ---: | --- |
| fetch | 0.5 | colour first, but not at any price |
| keep | 0 | what the camera sees, whatever it weighs |

The fetch default is a *square root* rather than the full per-byte discount it
started as. Per byte, a payload a thousand times smaller is a thousand times
preferred — far more than "colour first" needs, and it queues a large near mesh
behind every trivial distant one, which is boxes in the foreground of a
half-loaded model. Measured on the 200-object scene, the model leaves its
default colours at **255 ms at 0.5 against 261 ms per byte** — no cost — while
at **0 it takes 5406 ms**, the whole load, which is precisely the failure the
per-byte rule was written to prevent. Total load time is unchanged across all
three.

⚠️ The knob is **steep** for keeping: payload sizes span three orders of
magnitude and projected sizes about one, so `keepweight=0.5` already ranks
nearly as size does (492 resident averaging 16 KB, i.e. the per-byte
behaviour). The useful range for what to hold is nearer 0 to 0.3.

Two consequences worth naming:

- **The camera has to be able to pump the fetch.** While a scene is arriving,
  every arrival re-sorts the queue for free. At the budget that stops — the
  chunks left are exactly the ones worth less than what is resident, so nothing
  is asked for and nothing arrives to reconsider them. Turning to face them is
  what changes the answer, so the frame pumps a round when the camera has
  moved (rate-limited to 200 ms; an orbit is a hundred frames of continuous
  change and a round costs a sort).
- **A scene held back by the budget is not a scene still loading.** The
  indicator would otherwise say "loading" forever at the fraction the budget
  allows. Once nothing is in flight and everything left was refused, that is as
  much of the model as this viewer holds at once, and the bar clears.

⭐ **A budget turns a lossy failure into a fatal one, so the fetch must
recover from silence.** A key marked in flight stayed so forever if its
request neither succeeded nor failed — a backgrounded page, a stalled local
store, a dropped mobile connection — and such a key is one the fetch never
asks for again. That used to cost only those chunks. With a budget its bytes
count as already spent, so a handful of ghosts consume the whole allowance:
measured on a phone, **349 of 382 payloads unaskable with zero requests
actually in flight**, holding almost no geometry while refusing everything for
want of room, every object grey. Requests now carry the time they were made
and are presumed lost after thirty seconds.

And ⚠️ **the recovery cannot be scheduled by the thing it is waiting for** —
the same lesson as the overlay barrier's grace (phase 4a), in a second place. A
round of fetching is driven by an arrival, so a load whose requests have all
stalled has no arrival to drive the round that would notice. **Two hung
requests reproduce a permanently grey model**, budget or no budget. While
anything is outstanding the viewer now wakes on a timer and looks;
`scratchpad/hangtest.js` hangs the first N chunk requests via CDP and asserts
the load still completes (200 draws all coarse → 417 draws, 0 outstanding).

A read that hangs is usually the local store, and asking it again just hangs
again, so the first timeout gives the store up for the session and lets the
network answer.

GPU memory follows for free: the backend already drops mesh and geometry
buffers unused for two frames (`BGFXRenderer.cpp`), so a rung descended on the
CPU releases its upload without eviction having to reach across the interface.

**A parsed payload is not held twice.** The bytes a chunk arrived as and the
geometry they were read into are two copies of the same thing, and the second
is the one the budget bounds — so a viewer told to hold 8 MB was really holding
that plus every byte it had ever downloaded, the payload cache having a
192 MB budget of its own. The bytes are finished with when the fill returns and
the local store still has them, so they go.

Budget default 320 MB, `?membudget=<MB>` to say otherwise — which is the only
way to exercise any of this, since a real budget is larger than a demo scene.
(Both of those changed in 4c below: the budget now fits itself to the device,
and `?membudget=` pins it.)

### 4c — one ladder for two tiers, as built

Phases 3 through 4b were written into the WASM viewer, where the desktop
cannot reach them. But nothing about ranking a payload by what the camera can
see of the objects that want it, or giving one back when memory is full, is a
statement about a network: a large model exhausts a desktop GPU exactly as it
exhausts a phone. Only *acquisition* differs.

So the policy moved to `Renderer/SceneLadder.{h,cpp}` — `LadderView`,
`RungRanker` (was the viewer's `FetchOrder`), `Evictor`, `kEvictMargin` — with
no `bx`, no emscripten and no Qt in it, and plain float arithmetic rather than
a math library's vectors, because two tiers should not have to agree on a
vector type to share a policy. The viewer keeps only what a viewer can answer:
where its camera is, and where to look up an object's bounds, which it alone
knows is in two places because a staged publish announces bounds before the
object is applied. `FetchOrder::chunk` became `RungRanker::acquisition`, since
on the desktop the thing being ranked is not a download.

`RungProvider` is the seam acquisition sits behind: begin an acquisition, say
how much is outstanding and how much may be, flush what is packed but unsent.
Nothing returns a payload — acquisition completes by the chunk's fill running —
so a provider answering from a local cache and one answering over a link are
the same to the caller. It also carries an advisory `cancel(key)`, declared
before any caller exists, because the seam is the part that fossilizes: a
fetch already in flight costs nothing to ignore, but the desktop's
acquisition is *work* — a tessellation a camera move can make pointless
mid-job — and that cost model has to be expressible from the desktop tier's
first implementation.

**The budget stopped being a constant.** 320 MB was chosen against a desktop
and applied unchanged to a phone, where it is less a budget than a way to be
killed later. It now starts from what the platform will say (wasm growth cap,
`GlobalMemoryStatusEx`, `hw.memsize`, `_SC_PHYS_PAGES`, plus
`navigator.deviceMemory` where it exists) and then *measures*: the heartbeat
already reporting the heap and the resident payloads feeds both back, and a
heap ceiling converts into a payload budget through a factor observed on the
device rather than assumed. The platform query is only a starting guess —
`deviceMemory` is Chrome and Android only, absent on Safari where the ceiling
matters most, and what a tab may hold is decided by the device, the other fifty
tabs and the OS.

Three attempts, because the estimator is easy to get wrong in ways that look
right:

- **Charging the raw payload cache to geometry.** It is held about one for one,
  so folding it in reports expansion where there is only a download cache.
- **A ratio from the origin** — `(heap now − heap when empty) / payload`. The
  heap before geometry is not the fixed cost of running: the backend's buffers
  and the textures are created as the first payloads land, and charging that
  one-off to the few megabytes then resident reported a factor of six. What a
  budget needs is the cost of the *next* megabyte, which is a slope.
- **A slope that never samples.** Re-anchoring every heartbeat left each ~1 MB
  delta under the significance threshold, so the gap never accumulated to reach
  it and the expansion sat at zero for a whole load while appearing merely
  quiet. The anchor moves only when a sample is drawn from it.
- **An average of per-sample slopes, under slab growth** (the fourth attempt,
  found in review). The heap grows in slabs, so between slabs every sample
  reads "the payloads cost nothing" and drags a blended estimate toward its
  floor, while the slab arrives as one clamped spike that cannot pull it
  back — a systematic under-estimate, which is a budget aimed past the wall.
  The slope is now the **ratio of two decayed sums** (payload growth and net
  heap growth over a window of scene growth), so a slab's bytes count
  whenever they land, against the whole window's payload rather than one
  sample's. Asserted by a test that watches the running estimate *through* a
  slabbed load — the dip between slabs is the failure, and the estimate's end
  value can land near truth by phase luck (the first version of the test
  passed with the old estimator for exactly that reason).

It converges downward from the clamp — erring toward holding less until the
device proves otherwise, which is the direction to be wrong in. The 200-object
scene is too small to settle it (~14 MB of payload); a real model is what will.

Two ceiling changes from the same review. A mobile browser that will not say
how much memory it has (`deviceMemory` is absent on exactly Safari) now
starts from a **conservative mobile ceiling** rather than a share of the wasm
growth cap — a phone whose heap may grow to 2 GB does not have 2 GB to give,
and the OS kills the tab with no signal the process could observe. And
`MemoryBudget::observeCeiling()` exists as the seam for a *measured* wall: an
allocation that fails with the heap at some size is a direct observation of
where the ceiling really is, monotonically lowering, cutting the budget at
once. Honestly: no wasm caller exists yet — with aborting malloc there is
nothing left to call it from — the intended caller is the desktop tier,
where a caught `bad_alloc` is exactly this observation. The Safari gap
(jetsam kills below any observable threshold) remains open; the conservative
start is its mitigation.

GPU memory is still unmeasured on both tiers: `bgfx::getStats()` reports
`gpuMemoryUsed`/`gpuMemoryMax` where the backend supports it and is called
nowhere. On the desktop that is more likely than system RAM to be the binding
constraint, so the budget there is currently watching the wrong number.

## 12. Open questions

- **Manifest history depth** — how stale a viewer may be before a full resync,
  and what that costs in server memory.
- **Very large single objects.** The mesh level bounds *geometry* churn and
  decouples submission from it, but an object with tens of thousands of draws
  still re-sends its whole draw list when one draw changes. Sub-bucketing the
  draw list may be needed; measure first.
- **Masking stand-ins out of AO** (§6) — the one live consequence of keeping
  them in the depth prepass. Stencil bit or id channel; decide by looking at it.
- **Coarse-rung default timings** — grace and fade are parameters (§6), but
  their defaults still want tuning against a real model over a real link.
- **Rebuild granularity under a chunk storm** (§6) — a per-frame dirty drain
  bounds the rebuild to once per frame, but a frame in which thousands of
  objects go dirty still rebuilds thousands of slots. Whether that needs a
  budget per frame is a measurement, not a guess.
- **LOD error metric and budget** (§7) — deferred; likely parameters too.
- **Element maps on decimated levels** — answered: the generator clusters per
  element and the part tables carry over (§7). The refinement subsets carry
  too, each by the rule its meaning allows: nonFlatParts and solidParts mark
  source faces (properties invariant under decimation) and are re-emitted as
  maximal runs over the surviving triangles; the seam filter follows the weld
  with **non-seam winning** where a seam and a non-seam edge merge, erring
  toward showing a line. The one watch item: capping assumes closed geometry
  and clustering does not preserve watertightness, so a cap cut through a
  decimated solid can be rough until the full mesh lands — judge against a
  real model.
- **Root manifest at very large object counts.** 100k objects make even the
  delta's object list non-trivial; paging the object list into content-addressed
  pages is the escape, if measurement demands it.
- **Overlay feeds** (NaviCube, axis cross) are static and per-viewer, not per
  document; they may belong in the viewer bundle rather than the stream at all.
