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

## 3. The design: one central store, addressed by position

**Write every shape of the document into one BRep member, record each
property's byte position in it, and restore by seeking to that
position.** (User proposal, 2026-08-16.) The obvious worry -- that a
central store defeats partial loading -- turns out to be mostly
removable, because OCCT's binary format is random-access by
construction. That is worth establishing first, since the whole design
rests on it.

### 3.1 What OCCT actually supports

`BinTools_ShapeReader` / `BinTools_ShapeWriter` (OCCT 7.6+) write
"topology in binary format without grouping of objects by types and
**using relative positions in a file as references**". A repeated
sub-shape is emitted as a reference to an absolute stream position; the
reader hits `IsReference()`, does `GoTo(pos)` -- a plain `seekg` --
reads there, seeks back, and caches the result in a
`position -> TopoDS_Shape` map.

Two paths that look like they would do the same do **not**:
`BinTools::Read` and FreeCAD's `TopoShape::importBinary` use the older
`BinTools_ShapeSet`, a table grouped by type that is read in full before
any shape can be picked out; the ASCII BRep path
(`TopTools_ShapeSet::Read`) likewise reads the entire table and only
then indexes into it. Neither can restore one shape without restoring
all of them. Switching the central store to `BinTools_ShapeWriter` is
therefore part of the work, not an implementation detail.

The reader and writer are present on the frozen 7.7.2 branch as well as
on 8.0.1, so a file written this way is not 8.0.1-only.

### 3.2 Measured on a probe, not assumed

A standalone program (`scripts/bintools_partial_probe.cxx`) writing a
parent compound plus its 6 children -- the parent's leaves being located
copies of the children's TShapes -- against the same OCCT the build
links:

    central store: 4552 bytes for the parent + 6 children
      parent at 0, children at 4522 4527 4532 4537 4542 4547
    one member per property would be 8464 bytes (1.86x)
    selective read of child 4 alone: ok
    parent leaf 0 IsPartner child 0: TRUE   (same TShape pointer: yes)
    control, one reader per property: IsPartner FALSE

Three things are settled by that:

1. **The duplication disappears.** Each child costs **5 bytes** after
   the parent (4522 -> 4527 -> ...): its record is a pure reference into
   geometry already written. 1.86x smaller here; in general the saving
   is whatever fraction was duplicated.
2. **Selective restore works.** Seek into the middle of the store, read
   one shape, and only what that shape's reference graph needs is
   parsed. Partial loading is not defeated.
3. **The reader instance is the identity domain.** One reader across the
   document restores `IsPartner TRUE` and the *same TShape pointer*; a
   fresh reader per property restores `IsPartner FALSE` -- the loss we
   have today, reproduced exactly. So the document restore must hold one
   `BinTools_ShapeReader` alive for the whole pass. This is the single
   most important implementation constraint here.

### 3.3 Save

One member per document; each `PropertyPartShape` writes through the
shared writer and records `tellp()` before its own `Write`:

    <Part store="Shapes.bin" pos="4522" ElementMap="..."/>

No grouping analysis, no hashing, no union-find -- the writer's own
position map does the deduplication, at every level, including the
sub-shape sharing that whole-member hashing can never see (sec 1.3).
A shape already written costs a reference record.

### 3.4 Restore

Hold one `BinTools_ShapeReader` and one seekable stream over the store
for the document's restore pass. Each property seeks to its recorded
position and reads; shared sub-shapes resolve to already-restored
TShapes through the reader's map. Ordering does not matter -- a
reference is an absolute position, so a property can be restored before
or after any other.

**The one real constraint is seekability.** A zip member is deflated and
cannot be seeked, so one of:

- **inflate the store into memory once**, and seek in that buffer. Keeps
  compression; a partial load then pays inflate over the whole store but
  parses only what it needs -- and parsing, not inflating, is what
  dominates (`docs/DocumentLoad.md`). Simplest, and the default choice.
- **store the member uncompressed** and read the `.FCStd` directly at
  the member's data offset -- true random access, no inflate, at the
  cost of compression on the geometry.
- **chunk the store** into several members, which bounds both the
  inflate and the memory, and doubles as the guard below.

## 4. Chunking the store

One member for a 17800-object document is one very large member. Chunking
splits it, and it is worth doing carefully because it is also the answer
to inflate cost, to memory, and to failure isolation.

### 4.1 Why a chunk boundary costs nothing structurally

Two properties of the format, both read out of the OCCT source rather
than assumed:

- **The writer never seeks.** `BinTools_OStream` takes `tellp()` once at
  construction and counts its own position from there; there is no
  `seekp` anywhere. Writing is strictly append-only.
- **References are backward deltas**, not absolute offsets:
  `WriteReference` stores `myPosition - thePosition`, encoded in 1, 2, 4
  or 8 bytes according to magnitude (`Reference8/16/32/64`). The reader
  turns it back into an absolute offset and seeks there.

So the store is an append-only byte stream whose internal links all
point backwards, and the byte distance is what they cost. Cutting that
stream into pieces is **pagination**, not a format change -- provided
the reader is handed something that behaves like the original stream.
Note the second point twice over: a near reference costs one byte and a
far one costs eight, so write order is not only a partial-load question
(sec 4.4) but a file-size one.

### 4.2 Variant A -- independent chunks

A separate writer per chunk. Simple, no stream plumbing, and each member
is self-contained. The cost is that **sharing is lost at every
boundary**: a shape in chunk 2 cannot reference geometry in chunk 1, so
it is written again. Worth keeping in mind as the fallback if the
plumbing below proves fiddly, but it reintroduces exactly the
duplication this document is about, in proportion to how often sharers
land in different chunks.

### 4.3 Variant B -- one logical stream over N members (recommended)

Keep **one** writer and **one** reader, and put a stream in front of
them that spans the chunks:

- **Write**: an output `streambuf` that starts a new zip member every N
  bytes. `tellp()` keeps returning the logical position, so the writer
  is unaware, and recorded positions stay in one space.
- **Read**: an input `streambuf` over the chunk table that maps a logical
  offset to (chunk, offset within it), inflating a chunk on first touch
  and caching it. `seekg`/`tellg` are the only operations the reader
  needs, and they are the two the streambuf implements.

Neither OCCT class changes, because the writer only appends and the
reader only seeks. Dedup stays exact across the whole document while the
resident bytes stay bounded.

### 4.4 Write order is what makes a partial load cheap

References point backwards only, so **what a shape needs is always
written before it**. That single fact decides the ordering rule:

- **Children before parents** (dependency order). A leaf object's
  geometry is then complete within its own chunk, and the compound or
  array above it is references only -- five bytes each, measured in
  sec 3.2. Restoring one child touches one chunk. The parent's own
  restore necessarily reaches its children's chunks, which is honest:
  its shape *is* their geometry.
- **Group by subtree**, so a partial load of a branch touches a short
  run of chunks rather than a scattering, and so the deltas stay short
  enough to encode in one or two bytes.

The measurable that says whether the ordering is any good is
**chunks touched over chunks total** for a partial load; it should be
logged, not inferred.

### 4.5 Progressive loading keeps working

Because references are backward-only and chunks arrive in write order,
**every chunk is restorable the moment it arrives** -- nothing in it can
depend on bytes that have not been read yet. The interleaved loader of
`docs/ProgressiveLoading.md` keeps its shape; its unit changes from "one
member per property" to "one chunk carrying many shapes", which is
fewer, larger reads.

### 4.6 The chunk table, and what a property records

A property records one logical position, and nothing about chunking:

    <Part store="Shapes" pos="4522" ElementMap="..."/>

The document carries the table -- chunk count, and per chunk the logical
start, the stored length and a checksum. Changing the chunk size, or
splitting differently on the next save, never touches a property entry.

### 4.7 Sizing, and the cost of getting it wrong

Two forces pull against each other, and one of them is already measured
(sec 1.1): twenty small members cost **7268** stored bytes where a single
member of comparable raw size cost **2305**, because deflate runs per
member and restarts its dictionary each time. So chunks want to be large
enough to compress well and few enough to keep the table small, but small
enough that a partial load does not inflate the document to read one
object. Order of megabytes is the region to start in; make it a
parameter, measure both ends, and log the choice.

### 4.8 Memory

An LRU over inflated chunks with a byte cap. The reference pattern
argues that this behaves: the format spends one byte on a near reference
and eight on a far one, so a store written in dependency order is
dominated by short backward jumps that stay inside the current or
previous chunk.

Note separately that the reader's `position -> shape` map holds every
shape it has restored for as long as the reader lives, which is the
whole restore pass. That is the point -- it is what reconstructs
identity -- but it means the map, not just the chunk cache, is the
retention to watch on a large document.

### 4.9 The uncompressed alternative

Chunks can be stored uncompressed and the `.FCStd` mapped directly. Then
paging is the operating system's page cache, there is no inflate at all,
and random access is genuinely random. The cost is file size on the
geometry, which is exactly what this document is otherwise trying to
reduce -- so it belongs as an option for workflows that reopen huge
assemblies far more often than they ship them, not as the default.

### 4.10 Failure isolation

A single central member turns a truncated file into a document with no
geometry at all, where today it would lose one object. Per-chunk length
and checksum bound that: damage costs the shapes stored in that chunk
and whatever references into it, and the rest of the document still
opens.

## 5. Risks and open questions

- **One reader for the whole restore.** Measured above: without it the
  sharing is not reconstructed at all. It has to survive the document's
  whole file pass, and be dropped after -- it holds every restored
  shape.
- **Progressive and partial loads.** `docs/ProgressiveLoading.md`
  interleaves member reads with object creation; a central store changes
  the unit of that work to a chunk (sec 4.5). The argument that it still
  fits is structural -- references point backwards only -- but the cost
  must be measured against the existing load timings, not assumed.
- **Triangulation and flags.** `BinTools_ShapeSetBase` carries the
  with-triangles and with-normals flags; the store must keep FreeCAD's
  current choice, or files silently grow.
- **Element maps stay per property.** Sharing geometry must not share
  element maps -- two objects over one TShape can carry different mapped
  names, and the TNP machinery relies on that.
- **Aliasing after restore.** A triangulation built for one object is
  seen by its sharers. Already true before a save, new after a reload;
  the ladder's per-source rungs (`docs/SceneStreaming.md` #13) should be
  re-read with that in mind. A recompute makes a new TShape, so an edit
  cannot corrupt a sharer.
- **Failure mode.** A corrupt or truncated store loses every shape at
  once, where today it would lose one; sec 4.10 bounds it to a chunk.
- **Chunking is where this gets built or botched.** The streambuf pair
  of sec 4.3 is the only genuinely new machinery in the design, and
  variant A (sec 4.2) silently reintroduces the duplication if it is
  chosen for expedience. Whichever is built, log the chunk count, the
  chunks touched per load, and the dedup actually achieved -- a silent
  fallback to per-chunk dedup would look exactly like success.

## 6. Later, optional: content hashing for unshared duplicates

Two objects can hold *equal* geometry with no shared TShape -- separately
imported copies, flattened copies. The position mechanism never groups
them, because there is nothing to reference. A content hash would, but
only when their placements agree (sec 1.3), so it is worth building only
after this is measured, and only if such duplicates prove common. The
render side already deduplicates these on the GPU.

## 7. How it will be judged

- `isPartner` across a save/reopen round trip: parent leaf vs child
  shape must be **True** after reopen (it is False today, sec 1.1).
- File size on the same document, schema 6 with and without the store.
  Expect the child members to collapse into references: 9573 stored
  bytes of shape members in the measured case, of which 7268 is the
  duplicate half.
- `docs/DocumentLoad.md`'s open timings on `MiSTer_imported.FCStd`, with
  the shape-restore split already logged there (`import` vs `setValue`),
  and a partial-load case to prove selective restore pays off.
- Tessellation and instancing counters after a reload: `_InstGeomTable`
  entry count and the `instancing 0.0NNs` line, with
  `scripts/demo-instanced.py` and `scripts/demo-inst-release.py` as the
  scenes that reach that path.
