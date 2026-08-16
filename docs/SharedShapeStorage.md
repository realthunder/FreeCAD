# Shared Shape Storage (plan)

This is a plan, not a build log: nothing here is implemented. It is
about what a `.FCStd` does to geometry that two objects share.

In memory, two objects can hold the *same* `TopoDS_TShape` -- one
tessellation, one set of GPU buffers, one instancing entry. A save and
reopen destroys that: each object gets its own copy of the same
geometry, stored, read and meshed once per object. This document
states the measurement, explains where the identity is dropped, and
lays out the options with a recommended phasing.

Related reading: `docs/DocumentLoad.md` (the load cost this feeds, and
the compact-format work that shares *property blocks* -- a different
thing), `docs/TShapeRenderCache.md` (the render-side sharing that
already compensates for part of the loss), `docs/ProgressiveLoading.md`
(the interleaved zip read any format change has to keep working),
`docs/IncrementalPublish.md`.

## 1. What is measured

Round trip through `saveAs` + `openDocument`, four `Part::Feature`
objects whose `Shape` is the *same* compound of 120 leaves over 30
distinct TShapes (`isPartner` is OCCT TShape identity, ignoring
location). Measured 2026-08-16 on the current tree, headless
`FreeCADCmd`:

| | intra-object sharing | cross-object sharing |
|---|---|---|
| before save | True | **True** |
| after reopen | True | **False** |

**Intra-object sharing survives for free.** The BRep format writes each
TShape once per file and reconstructs identity on read, so a compound
whose leaves repeat a TShape still instances after a reload. That is
what `buildInstanced` qualifies on, so instanced rendering survives a
reload today.

**Cross-object sharing is lost.** Nothing reconstructs identity between
two files.

What the duplication costs in the file, same scene, sharers placed
identically so the members are comparable:

    4 shape members, 1 distinct by content
    duplicate content: 234243 raw bytes, 11394 STORED bytes
    -- 65.9% of the 17296-byte .FCStd

The zip does **not** absorb it: each member is deflated on its own, so
four copies of one shape cost four times the stored bytes.

And a trap for any "dedup by content" design -- the same scene with the
sharers at *different* placements:

    4 shape members, 4 distinct by content
    raw sizes 78081 / 78305 / 78305 / 78306

The geometry is identical and the TShape is shared in memory, but the
bytes differ, because **each object's placement is baked into the shape
it writes**. The 224-byte spread is the location record. A hash of the
stored bytes therefore matches only sharers that happen to sit at the
same placement -- which, in an imported assembly, is exactly the case
that does not occur.

## 2. Where the identity is dropped

- `PropertyPartShape::Save` calls `writer.addFile(getFileName(...))`
  and `SaveDocFile` writes that one property's shape with
  `exportBrep`/`exportBinary`. One member per property, always.
- `Base::Writer::addFile` only *uniquifies names* (`FileNameSet`); it
  has no notion of content, and no two properties can name the same
  member.
- `PropertyPartShape::RestoreDocFile` does a bare `importBrep` per
  property into a fresh `TopoShape`, with no cache and no cross-property
  state. Two members holding the same geometry produce two TShapes.

So the loss is structural, not a bug: the format has no way to say "my
geometry is the one over there".

Costs of the loss, in the order they are paid: stored bytes (measured
above), `importBrep` per object at load, a tessellation per object, a
`_InstGeomTable` entry per object -- the instance key is
`(tshape, orientation, deflection, angular deflection)`, so distinct
TShapes never group however identical their geometry -- and the node
memory behind each.

## 3. What already compensates, so it is not double-counted

`docs/TShapeRenderCache.md` keys render caches by **content hash**, and
that already batches byte-identical caches -- "imported duplicates,
flattened copies without a shared TShape". GPU buffers and draw
batching therefore already dedup after a reload.

What is lost is the **CPU** side: file bytes, restore time,
tessellation work, node memory, and one capture instead of N.

## 4. Options

### A. Rediscover on restore, by content

Hash each member's bytes as it is read; on a hit, reuse the already
materialized TShape instead of importing again.

- No format change, and **old files benefit**.
- Progressive/interleaved load is unaffected: every member is still
  read independently.
- Costs one hash of bytes already in hand -- negligible beside
  `importBrep`.
- **But** it only catches byte-identical members, which the measurement
  above says means "sharers at the same placement". On its own it would
  do little for a real assembly.
- Saves nothing in the file.

### B. Write the shape in its own frame (enabler)

Export the shape with its top-level location stripped and record that
location in the XML instead; `Restore` re-applies it.

This is what makes duplicates byte-identical in the general case, so it
is the enabler for both A and C. It is a small, local format change
(`PropertyPartShape::Save`/`Restore`/`SaveDocFile`/`RestoreDocFile`),
gated like the compact format so old files still load unchanged.

Note the element map and hasher stay per-property, in their own
members: sharing geometry does not share element maps, and it must not.

### C. Record the sharing in the file

With B in place, a save can keep a `hash -> member name` map and, for
the second and later sharers, write a reference (`sharedFile="..."`)
rather than a member. That is the storage win (65.9% in the
measurement).

The reader resolves a reference by materializing the referenced member
-- on demand if the interleaved read has not reached it yet -- and
takes a reference to that TShape.

### D. Cluster members (the powerful, costlier variant)

Write every object that shares any sub-TShape into **one** BRep member
as a compound, and have each property record its index into it. OCCT's
own shape table then dedups at *sub-shape* level, which A/C never can:
two objects whose different compounds happen to share some leaves (the
repeated-fastener case) are only caught here.

The cost is granularity: a cluster is read as a unit, which cuts
against `docs/ProgressiveLoading.md`, and partial document loading gets
coarser. Worth building only if measurement shows cross-object
*sub-shape* sharing is common in real assemblies -- which is not yet
measured.

### E. Do nothing

Defensible for the render path alone, since content-hash keying already
recovers the GPU side. Not defensible for load time and memory on large
assemblies, which is where the project is heading.

## 5. Recommendation and phasing

1. **B first** -- write the shape in its own frame. Nothing else works
   in the general case without it, and it is the smallest change.
   Gate it on the schema version; keep the old path for reading.
   Measure: duplicates in an imported assembly must collapse to one
   content hash.
2. **A on top of B** -- restore-time content-keyed materialization. This
   is where the CPU win lands: one import, one tessellation, one
   instancing entry. Measure against `docs/DocumentLoad.md`'s baseline
   and against the instancing counters (`instancing 0.0NNs`,
   `_InstGeomTable` entry count) with `scripts/demo-instanced.py`.
3. **C** -- write-side dedup, for the file-size win. Independent of 2,
   and it needs the on-demand resolve in the reader.
4. **D** only if a measurement of real assemblies justifies it.

Each step is separately measurable and separately shippable, which is
the point of the order.

## 6. Risks and open questions

- **Aliasing after restore.** Sharing a TShape means a triangulation
  built for one object is seen by the other. That is already true
  before a save, so it is not new behaviour -- but it is new *after* a
  reload, and the ladder's per-source rungs
  (`docs/SceneStreaming.md` #13) should be re-read with that in mind.
- **Backward compatibility.** Old `.FCStd` files must load unchanged
  (`handleChangedPropertyName`/`Restore` discipline in `CLAUDE.md`), and
  a file written by the new path must be readable by a build without
  it, or the schema gate must refuse to write it.
- **Partial documents.** `App::Document::PartialDoc` restores a subset;
  a reference into a member that the partial load never reads has to
  resolve or degrade cleanly.
- **Was this written before?** The user recalls adding content-sharing
  code long ago. It is not in the tree -- `PropertyTopoShape.cpp`,
  `Writer::addFile` and the history were searched for shar*/dedup over
  shape/brep/tshape, and the nearest hits are the compact-format commits
  ("a file that shares defaults"), which share default *property
  blocks* under `doc.SaveSchemaVersion=6`, not geometry. Worth
  confirming before assuming it was lost rather than never written.
