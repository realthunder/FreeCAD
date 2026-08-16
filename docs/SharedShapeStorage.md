# Shared Shape Storage (plan)

This is a plan, not a build log: nothing here is implemented. It is
about what a `.FCStd` does to geometry that two objects share.

In memory, two objects can hold the *same* `TopoDS_TShape` -- one copy,
one tessellation, one set of GPU buffers, one instancing entry. A save
and reopen destroys that: each object gets a private copy of the same
geometry, stored, read and meshed once per object.

The case that drives this is not two objects that happen to be equal.
It is an **array or compound feature built from its children**: the
parent's shape is a compound whose leaves *are* the children's shapes.
That sharing is exact and universal in the model tree, and it is lost on
every reload.

Everything here is gated on **`doc.SaveSchemaVersion = 6`** (user
ruling, 2026-08-16), the fork's compact format. Nothing outside schema 6
changes, so there is no backward-compatibility burden: old files load by
the old path, and a schema-6 file was never readable by a build without
the fork's format anyway.

Related reading: `docs/DocumentLoad.md` (the load cost this feeds, and
the compact-format work that shares *property blocks* -- a different
thing), `docs/TShapeRenderCache.md` (the render-side sharing that
already compensates for part of the loss), `docs/ProgressiveLoading.md`
(the interleaved zip read this must keep working),
`docs/IncrementalPublish.md`.

## 1. What is measured

Measured 2026-08-16 on the current tree, headless `FreeCADCmd`.
`isPartner` is OCCT TShape identity, ignoring location.

### 1.1 The parent/child case

A `Part::Compound` over 20 `Part::Feature` children, each child a
different torus:

    before save : parent leaf 0 IS child 0 shape -> True
    after reopen: parent leaf 0 IS child 0 shape -> FALSE

    Compound.Shape.brp      42786 raw /  2305 stored
    20 child members        40294 raw /  7268 stored
    shape members total     83080 raw /  9573 stored = 64% of the
                                                       14974-byte file

The same geometry is in the file twice: once inside the parent's
compound, once per child. After the reload the parent's leaves are
private copies, so the document holds two tessellations, two sets of
nodes and two `_InstGeomTable` entries for one piece of geometry.

Note also that the parent's single member compresses far better than
the twenty small ones (2305 for 42786 bytes, against 7268 for 40294):
deflate runs per member, so splitting geometry across members costs
compression as well as duplication.

### 1.2 Sharing survives inside a member, never across members

Four objects whose `Shape` is the *same* compound of 120 leaves over 30
distinct TShapes:

| | intra-object (within one member) | cross-object (between members) |
|---|---|---|
| before save | True | **True** |
| after reopen | True | **False** |

**Inside one member the BRep format already does the whole job** -- it
writes each TShape once and reconstructs identity on read. That is why
a compound whose leaves repeat a TShape still instances after a reload.
The problem is entirely at the member boundary. This is the fact the
design below leans on: put shapes that share into the *same member* and
OCCT dedups them, at every level, for free.

### 1.3 The trap for any dedup-by-content scheme

Same four sharers, placed identically vs placed apart:

    placements EQUAL     : 4 members, 1 distinct content by hash
                           duplication = 65.9% of the .FCStd
    placements DIFFERENT : 4 members, 4 distinct hashes
                           raw 78081 / 78305 / 78305 / 78306

The geometry is identical and the TShape is shared in memory, but the
bytes differ: **each object's placement is baked into the shape it
writes** (the 224-byte spread is the location record). Hashing stored
bytes therefore matches only sharers that sit at the same placement --
which in a real assembly is the case that does not occur. And a hash of
whole members can never see *sub-shape* sharing at all, which is the
case that matters. Content hashing is not the mechanism; it is at best a
later add-on (sec 5).

## 2. Where the identity is dropped

- `PropertyPartShape::Save` calls `writer.addFile(getFileName(...))` and
  `SaveDocFile` writes that one property's shape. **One member per
  property, always.**
- `Base::Writer::addFile` only *uniquifies names* (`FileNameSet`). It
  has no notion of content, and two properties cannot name one member.
- `PropertyPartShape::RestoreDocFile` does a bare `importBrep` per
  property into a fresh `TopoShape`, with no cross-property state.

So the loss is structural, not a bug: the format has no way to say "my
geometry is the one over there", and the writer has no way to put two
properties' geometry in one place.

Costs, in the order they are paid: stored bytes and lost compression
(sec 1.1), an `importBrep` per object at load, a tessellation per object, a
`_InstGeomTable` entry per object -- the instance key is
`(tshape, orientation, deflection, angular deflection)`, so distinct
TShapes never group however identical their geometry -- and the node
memory behind each.

What already compensates, so it is not double-counted:
`docs/TShapeRenderCache.md` keys render caches by content hash, so GPU
buffers and draw batching already dedup after a reload. What is lost is
the **CPU** side: file bytes, restore time, tessellation, node memory,
and one capture instead of N.

## 3. The design: cluster members

**Write shapes that share geometry into one BRep member, and let OCCT's
own shape table do the deduplication.**

A member holding `Compound(parentShape, child0, child1, ... childN)`
writes every TShape once, at every level, with per-instance locations --
so it subsumes the placement trap of sec 1.3 and catches sub-shape sharing
the hash scheme cannot see. On read, one `importBrep` materializes the
lot with identity intact, and each property takes its own sub-shape out.

    <Part cluster="Cluster0.brp" index="3" ElementMap="..."/>

Three parts to build.

### 3.1 Save: group, then write once

1. **Detect** at `beforeSave` time. Walk each `PropertyPartShape`'s
   shape shallowly -- the shape itself, and recursively the children of
   compounds only, not into solids -- collecting `TopoDS_TShape`
   pointers. Union-find over "shares at least one TShape" gives the
   clusters. Pointer identity is free; no hashing, no geometry compare.
   Detection may be **shallow even though the dedup is complete**: it
   only decides *grouping*, and once two shapes are in one member OCCT
   dedups everything they share, however deep.
2. **Write** one member per cluster, holding a compound of the cluster's
   shapes in a fixed order. The first property of the cluster owns the
   member; the others record `cluster` + `index` and add no member of
   their own.
3. **Order** clusters so a cluster's member precedes the properties that
   reference it, which keeps the interleaved read resolving forward-only
   in the normal case.

Singleton clusters keep exactly today's behaviour: one member, one
property, no reference.

### 3.2 Restore: materialize once, hand out sub-shapes

The owner's `RestoreDocFile` imports the member and publishes the
materialized compound into a per-document restore context keyed by
member name. A referencing property takes `SubShapes[index]` from it --
the same TShape, so identity is reconstructed exactly.

Ordering has two directions and both must work:

- reference read *after* the member: resolve immediately from the
  context;
- reference read *before* it (partial or reordered reads): register a
  pending resolve and settle it when the member arrives, with a final
  sweep at the end of the file pass as the backstop.

The context is dropped when the document finishes loading; it holds
`TopoShape`s, so nothing is copied.

### 3.3 What stays per property

Element maps and the string hasher stay in their own members, one per
property. Sharing geometry must not share element maps -- two objects
over one TShape can carry different mapped names, and that is exactly
what the TNP machinery relies on.

## 4. Risks and open questions

- **Cluster snowball.** Connected components are naturally small (a
  parent and its children), but a chain -- a fusion of children, then an
  array of the fusion, then a compound of those -- can grow one. A large
  cluster is read as a unit, which cuts against `ProgressiveLoading.md`
  and makes partial loads over-read. Cap a cluster by member count or
  bytes and split it, accepting partial dedup at the cut; log the cut,
  so a silent truncation never reads as full dedup.
- **Partial documents.** `App::Document::PartialDoc` restores a subset;
  a cluster member may carry shapes for objects that are not being
  loaded. They are dropped after extraction -- correct, but it is read
  work the partial load did not ask for. Another argument for small
  clusters.
- **Aliasing after restore.** Sharing a TShape means a triangulation
  built for one object is seen by the other. That is already true before
  a save, so it is not new behaviour -- but it is new *after* a reload,
  and the ladder's per-source rungs (`docs/SceneStreaming.md` #13)
  should be re-read with that in mind. A recompute produces a new
  TShape, so an edit cannot corrupt a sharer.
- **Where the analysis hooks.** The grouping must run before any
  property writes. `PropertyPartShape::beforeSave` and the document's
  save path are the candidates; the writer needs a save-time context to
  answer "which cluster am I in, and do I own it".
- **The owner-writes-the-cluster shortcut.** Making the first property
  write the whole cluster avoids inventing a document-level persistence
  object, but it means one property's `SaveDocFile` writes another
  object's geometry. If that turns out to tangle ownership, a small
  `ShapeCluster` persistence object registered with the writer is the
  fallback.

## 5. Later, optional: content hashing for unshared duplicates

Two objects can hold *equal* geometry with no shared TShape -- separately
imported copies, flattened copies. Clustering never groups them, because
there is nothing to detect. A content hash over the member bytes would,
but only when their placements agree (sec 1.3), so it is worth building only
after the cluster work is measured, and only if such duplicates prove
common. Note the render side already deduplicates these on the GPU.

## 6. How it will be judged

- `isPartner` across a save/reopen round trip: parent leaf vs child
  shape must be **True** after reopen (it is False today, sec 1.1).
- File size on the same document, schema 6 with and without clustering.
  Expect the child members to disappear into the parent's: 9573 stored
  bytes of shape members in the measured case, of which 7268 is the
  duplicate half.
- `docs/DocumentLoad.md`'s open timings on `MiSTer_imported.FCStd`, with
  the shape-restore split already logged there (`import` vs `setValue`).
- Tessellation and instancing counters after a reload:
  `_InstGeomTable` entry count and the `instancing 0.0NNs` line, with
  `scripts/demo-instanced.py` and `scripts/demo-inst-release.py` as the
  scenes that reach that path.
