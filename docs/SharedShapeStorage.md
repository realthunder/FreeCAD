# Shared Shape Storage

Sections 1 to 8 are the plan this was designed from, and they still read
as one. **Step 1 (sec 9.1) is built and measured; sec 10 is what it
became.** Steps 2 and 3 are not started.

It is about what a `.FCStd` does to geometry that two objects share.

In memory, two objects can hold the *same* `TopoDS_TShape` -- one copy,
one tessellation, one set of GPU buffers, one instancing entry. A save
and reopen destroys that: each object gets a private copy of the same
geometry, stored, read and meshed once per object.

The case that drives this is not two objects that happen to be equal.
It is an **array or compound feature built from its children**: the
parent's shape is a compound whose leaves *are* the children's shapes.
That sharing is exact and universal in the model tree, and it is lost on
every reload.

Everything here is gated on **`doc.SaveSchemaVersion = 5`** (user
ruling, 2026-08-16), the fork's compact format. Nothing outside schema 5
changes, so there is no backward-compatibility burden: old files load by
the old path, and a schema-5 file was never readable by a build without
the fork's format anyway.

> **Renumbered, 2026-08-16.** This was built as schema 6 over a schema 5
> that meant "shared included-file blobs". Neither shipped, and that 5
> was never compatible either -- an older reader drops its embedded
> content silently -- so the two were folded into one fork format:
> **4 is upstream's, 5 is this fork's**. The numbers below have been
> renumbered to match. The pre-merge baseline the measurements compare
> against is now written "no store": the fork format with the store
> unused, which is still what a save produces whenever the store cannot
> be used (sec 10.5), so those comparisons stand unchanged.

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

**The new writer and reader are used at schema 5 and above, and nowhere
else.** Below that, a property keeps its own member and the existing
`exportBrep` / `exportBinary` paths are untouched -- not deprecated, not
routed through a compatibility shim. Two consequences worth stating
plainly: a schema-5 document written by this build is byte-comparable
with one written before it, and the reading code keeps both paths for
good, because old files never migrate themselves.

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

### 4.2 A chunk is the dedup unit AND the parallel unit

The obvious way to chunk -- cut every N bytes -- makes chunks that
reference each other, and a chunk that references another cannot be read
without it. Cutting somewhere else removes that entirely:

**Put shapes that share into the same chunk, and cut only where nothing
is shared.** Sharing is a graph over properties; its connected
components are, by construction, groups that share nothing with each
other. A chunk built out of whole components therefore has **no
references leaving it**, which buys three things at once:

- **Dedup is complete** within every component, and there is nothing to
  lose between them -- a boundary that crosses no sharing costs nothing.
- **Chunks are independent**, so each can be read by its own
  `BinTools_ShapeReader` with **no shared mutable state**. That is what
  keeps restore parallelizable (sec 5), which one central stream would
  have foreclosed.
- **Partial loads read only the chunks they need**, with no chance of a
  reference dragging in another.

Components are usually tiny -- a parent and its children -- so chunks
**pack** several whole components up to a size target rather than
holding one each. Packing is lossless in both directions: components do
not share, so grouping them cannot lose dedup, and a reader still needs
only its own chunk.

Two ways this degrades, both gracefully and both worth logging:

- **A component larger than the cap** must be split, and the sharing
  across that one cut is lost. Log the cut and the bytes it cost.
- **Sharing the detector missed.** Detection walks each shape and,
  recursively, the children of compounds -- it will not see two objects
  that share a single face deep inside unrelated solids. A missed edge
  puts two sharers in different chunks, so they are stored separately:
  exactly today's behaviour, for that pair only. The detector can be
  deepened later without changing the format.

### 4.3 The alternative, if cross-chunk references are ever wanted

Keeping **one** writer and reader over a stream that spans the chunks --
an output `streambuf` that starts a new member every N bytes, an input
one that maps a logical offset to (chunk, offset) and inflates on first
touch -- would allow references to cross boundaries. Neither OCCT class
would need changing, because the writer only appends and the reader only
seeks.

It is written down because it is the natural design if chunking is done
by size, and because it is the fallback for a component too large to fit
a chunk. It is **not** the recommendation: it reintroduces the shared
mutable state that makes restore serial, to buy dedup between things
that by definition do not share.

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

With independent chunks there is no paging cache to size: a chunk is
inflated, parsed, and its buffer released. What is resident at once is
one inflated chunk per worker, which is another reason the size target
matters.

The retention to watch is the reader's `position -> shape` map, which
holds every shape it has restored for as long as the reader lives. That
is the point -- it is what reconstructs identity -- but it means a
reader must not outlive its chunk. One reader per chunk, dropped when
the chunk is done, bounds it; the single-stream variant of sec 4.3 would
hold every shape in the document at once.

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

## 5. Load time, and the parallelism this must not foreclose

### 5.1 What actually runs today

Worth stating precisely, because the design hinges on it:

- **The restore loop is sequential.** `ZipFileReader::readFiles` walks
  the registered entries and calls `RestoreDocFile` inline; the deferred
  half (`Document::serveDeferredFiles`) runs on the main thread in timed
  slices. No thread pool touches shape restore.
- **But the archive reader was built for concurrency.** `ZipFileReader`
  indexes the central directory and opens each entry as an independent
  stream -- its own contract says entries "can in principle be read
  concurrently (every `openEntry()` owns its own file handle)". Today
  that capability is spent on reading *out of order* (deferred entries
  reopened long after the walk), not on reading at once.
- **OCCT's readers are single-threaded internally.** There is no
  `OSD_Parallel` in `BinTools`, `BRepTools_ShapeSet` or
  `TopTools_ShapeSet`; the only `OSD_Parallel` in the Part module is
  `SetUseOcctThreads` for algorithms.

So parallel shape reading is an **affordance the code deliberately
has and does not yet use**. A design that took it away would be
spending something real.

### 5.2 What the duplication actually costs, by stage

From `docs/DocumentLoad.md` on `MiSTer_imported.FCStd` (17800 objects),
against the 32.7s baseline:

| stage | s | what duplication does to it |
|---|---|---|
| BRep parse, 17058 `.brp` | 2.88 | parsed once per copy |
| visual build, of which `BRepMesh_IncrementalMesh` | 10.92 / **7.6** | **meshed once per copy** |

The tax is paid mostly in **tessellation**, not parsing -- and that is
the asymmetry that decides this whole question. Restoring identity
removes a duplicate's mesh as well as its parse, while parallel reading
can only ever recover the parse. (Against the 11.1s load the four fixes
in that document produced, the 2.88s parse is a larger share, ~26%, but
the ordering between the two stages is unchanged.)

### 5.3 The trade, stated as a number

Let `f` be the fraction of shape bytes that are duplicates, and `P` the
speedup a future parallel restore would get on the parse stage. A store
that serialized restore would cost `(1 - f)` of today's parse time where
a parallel per-member restore would cost `1/P` of it. Serializing is the
worse trade whenever

    1 - f > 1/P

which at 8 workers means duplication would have to exceed **87.5%**
before a serial store broke even on that stage alone. It will not.

That is the argument against the single-stream variant (sec 4.3), and it
is why the recommended chunking (sec 4.2) cuts only where nothing is
shared: **chunks with no references leaving them can be restored
concurrently, one `BinTools_ShapeReader` each, with no shared mutable
state** -- exactly as independently as today's one-member-per-property
layout, and with fewer, larger units of work.

### 5.4 What chunking does to a worker pool

Two constraints the size target has to satisfy at once, and they pull
opposite ways:

- **Enough chunks to fill the pool.** 17058 tiny members become a few
  dozen chunks; if that number drops near the core count, the tail
  dominates. Size the chunks so their count stays a small multiple of
  `hardware_concurrency`, and prefer lowering the target over shipping a
  handful of huge chunks.
- **Balanced chunks, not merely capped ones.** A chunk is a serial unit,
  so one oversized chunk is a straggler that sets the wall time however
  many workers are free. Pack components into chunks by decreasing size
  (greedy longest-first) rather than in document order.

Both are measurable and neither is guessable: log chunk count, chunk
byte spread, and the slowest chunk's share of the restore.

### 5.5 Net expectation

- Parse work falls by the duplicate fraction, and stays parallelizable
  at chunk granularity.
- Tessellation work falls by the same fraction -- the larger prize, and
  one no scheduling change can substitute for.
- Fewer, larger reads replace 17058 small ones, which also recovers the
  compression the small members lose (sec 4.7).
- The risk is concentrated in one place: chunk sizing and balance, which
  is why sec 5.4 is instrumented rather than tuned by intuition.

## 6. Risks and open questions

- **One reader per chunk, and the chunk is its whole world.** Measured
  in sec 3.2: a reader that does not span the sharers does not
  reconstruct the sharing at all. With chunking by sharing component
  (sec 4.2) a reader spans exactly one chunk, which is both sufficient
  and the reason restore stays parallel; get the chunk assignment wrong
  and the sharing is silently lost instead of loudly broken.
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
- **Chunk assignment is where this gets built or botched.** Everything
  else is bookkeeping; the sharing-component analysis and the packing
  are the only genuinely new machinery. A detector that misses an edge,
  or a packer that splits a component, loses dedup silently -- the file
  still loads, and nothing says it stored the geometry twice. Log the
  chunk count and byte spread, the components split and what that cost,
  the chunks touched per load, and the dedup actually achieved.
- **Restore parallelism is an affordance, not a fact** (sec 5.1). It
  exists in `ZipFileReader` and is unused. If it is never taken up, the
  argument in sec 5.3 is moot and a single stream would have been
  simpler -- so the sizing work of sec 5.4 should not be done ahead of
  the pool that would consume it.

## 7. Content hashing for unshared duplicates -- measured, and mostly free

Two objects can hold *equal* geometry with no shared TShape -- separately
imported copies, flattened copies. The position mechanism never groups
them, because there is nothing to reference. A content hash would, but
only when their placements agree (sec 1.3). That was left here as
"worth building only after this is measured"; sec 11.4 measured it.

**They are common, and they are fasteners.** On `scanner.FCStd`, 14 groups
covering 36 objects and 875496 bytes -- `Screw001-004`, `Screw005-007`,
`Nut-Nut003` -- parts copied rather than linked, so each carries its own
TShape. **9 of the 14 groups share nothing today.**

**The cheap mechanism is the file layer, not a geometric algorithm.** Once the
top-level location is out of the bytes (sec 11.4), equal solids serialize to
equal bytes, so they are one blob in the store; the content hash is already
computed for storage, and a `hash -> TopoShape` parse cache makes one parse
serve every referrer. Storage dedup becomes runtime dedup at no additional
cost, and it needs no comparison of any kind.

For shapes that do not arrive that way -- a legacy file, or a recompute result
-- a detection pass over what is in memory costs **0.8s for 246 objects and
18088761 bytes**: serialize each with its location stripped, hash, group.

***Do not pre-filter by geometric invariants.*** The obvious optimization is
backwards: computing `Volume` and `Area` over the same 246 shapes took
**3.342s**, four times the cost of simply serializing all of them, and the
filter is loose anyway -- 89 candidates narrowing to 36 confirmed, 53 false
positives. Serialization is the cheap operation here; integration is not.

**Unification itself is sound and cheap.** Taking the canonical shape and
re-applying the duplicate's placement yields a shape sharing the canonical
TShape:

    Screw002/003/004 against Screw001
      partner with canonical  True      (they shared nothing before: False)
      dVolume 0.0   dBBox 0.0   faces 27/27   edges 71/71
      after assignment: placement preserved, each object keeps its own
      cost: 0.0496s for three objects

**The element map is not in the way** (user, 2026-08-17). `Data::ElementMap`
maps `MappedName -> IndexedName` -- `Face1`, `Edge2` -- so it is coupled to the
shape only through sub-shape ordering, and byte-identical geometry preserves
that exactly. Two identical solids from different histories keep their own
different names, and both stay valid.

The one thing to get right is not the map but the *cache*. `TopoShapeCache`
(`TopoShapeCache.h:61`) is a per-`TopoShape`-instance `shared_ptr`, built lazily
by `initCache()` (`TopoShapeEx.cpp:604`), not a registry keyed on the TShape --
two independently built `TopoShape`s over one TShape get separate caches. But it
holds `cachedElementMap`, existing so "other TopoShape instances with the same
Cache can reuse the map once generated", and `TopoShape(const TopoShape&)`
forwards to `operator=` (`TopoShapeEx.cpp:667`). So building the substitute by
copying the canonical `TopoShape` would carry that cache, and its element map,
onto the duplicate.

The existing API already avoids it:

    dup.setShape(canon.getShape().Located(loc), /*resetElementMap=*/false);

which keeps the duplicate's own element map, flushes only if the cache was
touched by the incoming shape, and re-inits that instance's own cache
(`TopoShapeEx.cpp:673`). Note `TopoShapeExpansion.cpp` carries a second
definition of `setShape` and is commented out of the build
(`src/Mod/Part/App/CMakeLists.txt:545`); `TopoShapeEx.cpp` is the live one.

What is left to settle is only that the substitution does not touch the
objects, or opening a document would mark it modified.

## 8. How it will be judged

- `isPartner` across a save/reopen round trip: parent leaf vs child
  shape must be **True** after reopen (it is False today, sec 1.1).
- File size on the same document, schema 5 with and without the store.
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

## 9. Build order (superseded)

**Step 1 shipped (sec 10); steps 2 and 3 are superseded by sec 12.** Chunking
by sharing component and parallel restore were answers to problems a central
store creates, and sec 11 removes the central store. Kept because step 2's
component analysis is the same union-find that sec 11.5 considered and rejected,
and because the reasoning is what sec 11 argues against.

Three steps, each separately measurable and separately shippable. The
order is chosen so the format lands first and the two things that could
be got wrong -- chunk assignment, and threading -- come after something
that already works.

### 9.1 Step 1: the round trip, one chunk, no analysis

The whole mechanism end to end with the store as a single chunk. No
component analysis, no packing, no parallelism.

- `PropertyPartShape::Save` at schema >= 6 writes through a
  document-level store writer and records its logical position;
  the XML entry becomes `<Part store="Shapes0" pos="N" .../>`.
- Below schema 5 nothing changes -- same code path, same bytes.
- Restore holds one `BinTools_ShapeReader` over the store and serves
  each property at its position.

Decide first, because everything else sits on it: **where the store
lives while it is being built.** The property needs its position during
the XML pass, so the store is filled then and emitted as a member
afterwards -- either buffered in memory (simple; the whole store
resident at save) or spooled to a temp file (bounded; more plumbing).

Two pieces of existing machinery to reuse rather than reinvent: the
store member must be written **first** in the archive so it precedes
every property that references it, and `Document::restoreDeferredFile`
already serves a property from an archive entry long after the walk --
"seek to a position in the store" is the same shape of operation, so the
progressive path should fall out rather than be rebuilt.

Gate: `isPartner` True after reopen on the sec 1.1 scene; the file
smaller by the duplicate half; old files load unchanged.

Touches: `src/Mod/Part/App/PropertyTopoShape.cpp`, `src/Base/Writer.*`,
`src/Base/Reader.*`, `src/App/Document.cpp`.

### 9.2 Step 2: chunking by sharing component

The analysis and the packing (sec 4.2): collect TShape pointers per
property, union-find into components, pack components greedily
longest-first into chunks with a size target, one member each, one
reader each.

Gate: dedup unchanged from step 1 (nothing lost to a boundary), plus the
logging sec 6 asks for -- chunk count and byte spread, components split
and what that cost, chunks touched per load.

### 9.3 Step 3: parallel restore

One reader per chunk on a worker pool. Note the split before starting:
parsing a chunk is the parallel part, but handing the result to a
property mutates the document, so the parse fans out and the
`setValue` half stays serialized.

Gate: `docs/DocumentLoad.md`'s open timings on `MiSTer_imported.FCStd`,
with the slowest chunk's share of the restore reported -- sec 5.4 is the
thing being tested here, not the parallelism as such.

## 10. Step 1 as built

Built 2026-08-16. The design of sec 3 survives intact -- one store per
document, addressed by byte position, one reader as the identity domain --
and everything below is either a decision the plan left open or something
the code said that the plan had wrong.

### 10.1 The store is a document property

    Part::PropertyShapeStore : App::PropertyFileIncluded   (name "ShapeStore")

The store rides on a **dynamic property of the document**, created the
first time a save uses one and never taken off again (sec 10.5).

That answers three of the plan's open questions at once, which is why it
was chosen over the alternatives considered (a writer attachment, and a
document-level `PropertyPartShape` holding a compound of every shape):

- **Where the store lives while it is built** (sec 9.1): a spool file in
  the document's transient directory, adopted by the property when the
  collect finishes. Bounded memory, no whole-store buffer.
- **Seekability** (sec 3.4): after a restore the content is a real
  read-only file in the transient directory, kept alive by the property's
  blob handle. Random access with no inflate, for the life of the
  document -- better than the "inflate into memory once" the plan had
  picked as its default.
- **Module boundaries**: `PropertyFileIncluded` is an App type carrying an
  opaque file, so App never learns what is in it. Only Part touches OCCT.

A `PropertyPartShape` holding a compound would have been less code and
would have deduplicated just as well (sec 1.2), but `BinTools_ShapeSet`
is a type-grouped table read in full, so selective restore and the
deferred shape load would both have gone.

### 10.2 Where the shapes are collected

    Document::Save
      for each object: beforeSave(writer)    <- shapes ensureRestored()
      beforeSave(writer)                     <- ShapeStore::beforeSave COLLECTS
      PropertyContainer::Save                <- the store writes its blob hash
      writeObjects                           <- <Part store=".." pos="N"/>

The collect runs in the store property's own `beforeSave`, which is the
only window where every shape is present and no object has been written
yet -- so a property can still be told where its geometry will be. This
is what `Property::beforeSave()` gaining a `Base::Writer&` is for: the
pre-save pass could not otherwise see the schema the save resolved.

### 10.3 A recorded position must be a REFERENCE record

The one thing the plan got wrong, and it silently costs the whole point.

`BinTools_ShapeReader` consults its `position -> shape` map **only when it
reads a reference**. Read a shape record directly at its own position and
it re-parses into a *new* `TShape`, however many times it has already
built that exact shape -- so a child read directly after the compound
above it had already materialised it comes back as an unrelated copy, and
`isPartner` is false. Measured: that is exactly what the first build did.

So every shape is written **twice**, and the position recorded is the
**second** one. The first write puts the geometry down; the second is
therefore always a reference record, and every read enters the branch
that goes through the map. The extra record is a handful of bytes.

The probe of sec 3.2 did not catch this because it wrote the parent
first, which made the children references by construction.

### 10.4 Serving is lazy, and must not start too early

A property records its position and stays restore-pending; the shape is
read the first time the value is asked for, through the `ensureRestored()`
that already existed for deferred archive entries. So the deferred load
survives at schema 5, and it needs no archive index at all.

One ordering trap, found by a stack overflow rather than by reading:
**the XML pass asks for the shape itself.** `PropertyPartShape::Restore`
ends in an element-map version check that reaches `getComplexData()`, and
at that moment the store's blob has not been drained from the archive.
The rule is therefore to stay pending until the store has content -- the
property then reads as the null shape it is, which is exactly what a
deferred archive entry reads as at the same moment, because its own
pending flag is not armed until the same drain.

### 10.5 The store property is never removed

Below schema 5 the property **stays on the document and writes itself out
empty** (user ruling, 2026-08-16, replacing a first version that removed
it). The property holds the handle that keeps the store file alive, and a
save is exactly when shapes are still being served out of it one object at
a time -- so removing it there strands the geometry of every property the
pre-save pass has not reached yet, silently. Writing nothing keeps the
store out of the file just as well, and costs nothing that matters.

What keeps a store out of a schema-4 file is therefore three things, not
one, and the third only showed up once the property stopped being removed:

- **`Save()` writes the empty form.** It nulls `_blob` around a call to the
  base class (holding a handle, so the file survives) rather than
  hand-writing the XML, so the empty form stays whatever that schema and
  writer call empty.
- **`FileBlobManager::dropReferenced()`**, the missing counterpart to
  `noteReferenced`. `Document::collectFileBlobs()` runs before a single
  property is written and notes every blob it can see, so the archive came
  out carrying the whole store as an entry the document had no way to
  reach -- the geometry in the file twice, which is what this document
  exists to stop. A property that then decides to write no content says so.
- **The wrapper element remains**, and cannot be removed. The
  `<Property name="ShapeStore" .../>` element is written by
  `PropertyContainer::Save`, not by `Save()`, and the only mechanism that
  suppresses it -- `PropNoPersist` -- is deliberately immutable at runtime
  (`Property::setStatusValue` masks those bits back to their old value). So
  a schema-4 file written from a document that used a store earlier in the
  session carries one empty property element. A document that never used a
  store is unaffected.

One further gate, unrelated to any of that:
**`Base::Writer::supportsSharedStore()`**, true only for `ZipWriter`.
Autosave's `RecoveryWriter` keeps a file per property and rewrites only
what changed; a store would have made every autosave cycle rewrite the
document's entire geometry.

Nothing needs to distinguish a document save from an export any more. An
export is capped at schema 4, so it creates no store, and with removal
gone there is nothing for it to destroy either -- the `Document::Saving`
status bit the first version needed was deleted with it.

### 10.6 Measured

The sec 1.1 scene, a `Part::Compound` over 20 torus children:

| | file | shape bytes stored |
|---|---|---|
| no store, ASCII BRep | 14368 | 8971 |
| no store, binary BRep | 14471 | 9049 |
| **schema 5, store** | **4308** | **1007** |

    isPartner, parent leaf 0 vs child 0
      before save      : True
      no store reopen  : False      (unchanged, sec 1.1)
      with store       : True

Two things worth reading off that table. The win is **entirely dedup**:
going binary on its own costs 78 bytes, and the store then takes 9049
stored shape bytes down to 1007, 9.0x. And it beats the plan's own
prediction of "smaller by the duplicate half" because both halves
collapse -- the compound member held all 20 tori again, and it is now
references.

Also checked: a schema-5 save leaves the document untouched; a second
save in the same session replaces the store; dropping to schema 4 takes
the property off the document and out of the file, and the per-property
members come back; serving a shape does not touch its object.

## 11. The version-control question, and the turn to external file references

Everything above designs one central store per document, and sec 10 built it.
That store is deliberately **off for directory saves** -- `Base::Writer::
supportsSharedStore()` is true only for `ZipWriter` -- because a single file
holding all geometry was assumed to be the worst possible artifact for version
control, which is what save-as-directory exists for (`docs/FileBlobsManager.md`
sec 13, and the ASCII BRep default that shares the same motivation).

That assumption was tested. One half held and the other did not -- and the
design that came out the far end keeps one file per object and expresses
sharing as a reference between files, with no store at all. Sec 12 is the build
order for it; sec 9 below is the superseded plan for the central store, of
whose three steps only the first was built.

### 11.1 Sub-shape sharing inside one model history

The sec 1.1 scene is a compound over its children, which is the obvious
parent/child case. The question left open was whether *sequential* features
share as well -- whether a Pocket's solid reuses the Pad's faces rather than
copying them.

Measured on a PartDesign body, `AdditiveBox` plus 11 `SubtractiveCylinder`
features, each feature persisting its own full solid. Sub-shapes of step N+1
that are the same TShape as one in step N:

```
  Pad       -> Pocket      Face   4/7    Edge  12/15   Vert   8/10
  Pocket    -> Pocket001   Face   5/8    Edge  15/18   Vert  10/12
  ...
  Pocket009 -> Pocket010   Face  14/17   Edge  42/45   Vert  28/30
  first     -> last        Face   4/17
```

Each pocket adds three faces and keeps every other one as the identical TShape.
The sharing is **chained rather than global** -- first against last is only
4/17 -- which is precisely what a position-addressed store dedups transitively
and what a pairwise comparison would miss. It is not a PartDesign artifact: a
plain `Part::Cut` shares 4/7 faces, 12/15 edges and 8/10 vertices with its base.

### 11.2 What each strategy captures

Same document, whole-file figures from the archive:

| | raw | deflated |
|---|---|---|
| schema 4, one `.brp` per property | 374306 | 29502 |
| schema 5, central store | 42082 | 7842 |
| whole-file content hashing | 12 distinct hashes over 12 shapes, i.e. nothing | |

**8.9x raw, and per-file content hashing captures none of it.** This settles a
question that was open in `docs/FileBlobsManager.md` sec 12: routing shape
files through the blob manager gives equal-copy dedup and incremental save, but
it is not a substitute for the store, because the redundancy inside a model
history is not equal copies.

### 11.3 Two corrections to what this document assumed

**Part of the store's compressed win is stream sharing, not dedup.** On the 12
feature shapes alone: 19861 bytes deflated separately, **10175 deflated as one
stream with no dedup at all**, against the store's 7842. So roughly 2x of the
archive's 3.8x compressed advantage is simply one zip member instead of twelve.
Sec 4.7 worried that many small members deflate worse; that is the same effect
seen from the other side, and it is larger than the worry implied.

**A single large file is not the worse git artifact.** Both layouts committed
to a real repository, `.git/objects` measured, then one feature edited in the
middle of the chain and committed again:

| | working tree | packed, commit 1 | packed, commit 2 | growth |
|---|---|---|---|---|
| 12 ASCII `.brp` | 131578 | 28384 | 34986 | **+6602** |
| one store file | 46178 | 22781 | 23291 | **+510** |

Git's delta compression recovers most of the raw 2.85x on the first commit --
1.25x packed -- but on the edit the store costs **13x less**, because the
downstream features are references rather than fresh copies of the changed
geometry. The store is the cheaper artifact per commit, not the more expensive
one.

Two conditions on that. It holds only while the store's byte layout is
**deterministic across saves**; if shape order shuffles, the delta explodes,
and that is not currently guaranteed. And it is one synthetic body -- it wants
confirming on a real model. What a binary store does still lose in a directory
is real but different from what was assumed: readable diffs, per-object
granularity in `git status`, and any hope of a textual merge.

### 11.4 Location canonicalization

The store only dedups what is **already shared in memory**. Twelve
independently built identical boxes (`isPartner` False) store at 42588 raw --
no dedup at all; twelve objects over one shared TShape store at 4726.

Whole-file hashing cannot see them either, because the placement is baked into
the bytes (sec 1.3) -- *unless* the top-level location is stripped into the XML
first. Measured: four different placements of one box then serialize to one
identical 2766-byte file, one hash. `Part::Feature::onChanged` already keeps
`Placement` and the shape's transform in lockstep in both directions
(`PartFeature.cpp:1304`), so for a Feature the location in the `.brp` is pure
redundancy; other containers would write it as an attribute on the `<Part/>`
element.

Worth doing on its own merits, independent of dedup: today moving one object
rewrites its entire shape file, and after this it is a one-line XML diff.

**It stays necessary after sec 11.5, for two reasons references cannot cover.**

*Movement churn*, which needs no measurement: moving an object leaves its
geometry identical and only its location different, so no reference is emitted
and the whole file is rewritten. With the placement in the XML the shape file is
byte-identical and the blob manager's hash skip does not rewrite it at all.

*Duplicates with distinct TShapes*: a reference fires only on shared TShapes, so
two independently computed identical solids are each written in full. Measured
on `scanner.FCStd`, over the 246 objects that actually persist a
`Part::PropertyPartShape`:

| | distinct | duplicate groups | bytes collapsible |
|---|---|---|---|
| as written today | 241 of 246 | 3 | 163508 |
| location stripped | 222 of 246 | 15 | **876358** |

5.4x more duplication becomes visible to whole-file hashing, 4.8% of the
18088761 bytes of serialized geometry -- and the groups are **fasteners**:
Screw001-004, Screw005-007, Nut-Nut003, each a set of identical parts that were
copied rather than linked, so each carries its own TShape. That is a permanent
feature of mechanical assemblies rather than a quirk of this document.

***The restore trap.*** `Feature::shouldApplyPlacement()` is just
`isRecomputing()` (`PartFeature.cpp:1343`), so outside a recompute -- **including
during restore** -- a change to `Shape` runs the else branch at
`PartFeature.cpp:1321` and overwrites `Placement` *from the shape's transform*.
Strip the location without re-applying it inside `PropertyPartShape::Restore`
before `hasSetValue()`, and every placement in the document silently becomes
identity.

Trap for anyone re-running this: **`Shape.copy()` bakes the transform into the
geometry**. The copy still reports the old `Placement`, and resetting it leaves
absolute coordinates in the file. Setting `Placement` on the shape itself is a
pure `TopLoc_Location`. A first probe "refuted" canonicalization purely from
this mistake.
### 11.5 The design this points to: external file references

A component store dedups by putting several objects' geometry in one file and
addressing it by byte position. There is a second way to get the same sharing
that keeps one file per object: **where a sub-shape is already stored in
another object's file, write a reference to that file instead of the
geometry.**

That is strictly better for everything this fork cares about:

- **Partial referring becomes full referring.** A shape file holds a handle on
  another whole blob and names a sub-shape inside it, so lifetime is ordinary
  refcounting again -- no joint ownership, no regeneration rules, no frozen
  component, no size cap, and none of sec 11.6's four rules.
- **ASCII survives**, because a reference is a name plus an index, not a byte
  position. The store cannot be ASCII by construction (sec 3.1); this can.
- **No random access is needed.** Positional addressing existed to pull one
  shape out of a monolith. With one file per object, files are small and are
  read whole, so the ASCII format's three structural obstacles -- geometry in
  front-loaded index-addressed tables, sub-shape references encoded as
  *reverse* indices that depend on the total shape count
  (`TopTools_ShapeSet::Write`), and a counted read loop -- stop mattering.
  They are only fatal for a store.
- **No component analysis**, no union-find, no packing, no blast radius.
- **Most files stay standard BRep**, openable by any OCCT tool.

### 11.6 The format extension

Because each file is read whole, the only thing missing from the existing
ASCII format is a way to name another file. A `Files` table at the head and one
new reference token:

```
Files 2
Pad.Shape.brp
Pocket001.Shape.brp
...
TShapes 17
...
+12 3        <- as today: shape 12 of this file, location 3
E1 7 3       <- new: shape 7 of file 1, location 3
```

The reader resolves a file reference by asking the blob manager for that name,
parsing it whole, caching it, and taking `Shape(index)`. Because that yields
the same `TopoDS_Shape`, sharing is restored by construction -- no positional
identity domain and no single long-lived reader held open for the document
(sec 3.2's constraint, and the reader-retention problem it created, both go
away).

A file that references nothing is byte-identical to a standard BRep file, so
the extension is only present where sharing actually is.

### 11.7 Ownership, settled by recomputing it every save

Each shared TShape must live in exactly one file, and something has to decide
which. Recomputing that assignment from scratch on every save, in a
deterministic order, makes it a pure function of the current model -- and that
one decision collapses most of the questions this design otherwise raises.
Ownership drift, cascading rewrites, dangling references and ownership transfer
on delete are all consequences of carrying an assignment forward; with a fresh
pass there is nothing to carry.

Two things survive it.

**Cost.** A full pass re-serializes all geometry every save, which is the
incremental-save win decentralizing was supposed to buy. Separable: run the
*analysis* every save -- walking TShapes and mapping pointers is cheap next to
serialization -- and still write only files whose content changed.

**Parked shapes**, and this is the part that makes the approach work at all: a
parked shape (`DeferShapeLoad`, never parsed) **has no TShapes in memory, so
nothing in the document can be sharing with it.** A parked object cannot
participate in any sharing decision, its file is unchanged by definition, and
it is inert in the analysis without any special case.

The residue is a parked object whose file *references* a file that changed. If
the referenced object is a dependency, changing it touches the referrer, which
must recompute and therefore cannot still be parked -- self-resolving. What is
left is unrelated sharing between objects with no dependency between them,
where the content file's blob-to-blob edges name exactly which parked files to
load.

So the whole problem set reduces to two rules:

1. **The visit order must be deterministic** -- document order, or dependency
   order. Otherwise files churn between saves for nothing, and sec 11.3's git
   behaviour depends on a stable layout.
2. **A changed file forces its parked referrers to load.**

### 11.8 Measured

Each object's shape walked top-down, emitting one reference at the first
sub-shape already owned by an earlier object and not descending further --
which is what the writer would do, so these are the real counts. Size is
estimated as the store's payload re-priced as ASCII (the measured per-document
ASCII/binary ratio) plus a measured 208-byte file header and a 20-byte
reference token, which is sound because both schemes write each distinct TShape
exactly once.

| | compound over 20 tori | PartDesign chain, 12 features | `scanner.FCStd` |
|---|---|---|---|
| files | 21 | 19 | 377 (of 606 objects) |
| distinct TShapes | 141 | 444 | 71185 |
| references emitted | 20 | 301 | 8408 |
| files needing none | 20 of 21 | 4 of 19 | 102 of 377 (27%) |
| max references in one file | 20 | 33 | 5129, median 1 |
| reference types | Solid 20 | Edge 200, Face 101 | Edge 5479, Face 2367, Vertex 335, Compound 100, Solid 78, Wire 49 |
| per-file ASCII, raw | 74327 | 374306 | 26181094 |
| per-file binary, raw | 40363 | 216552 | 13977247 |
| store, raw | 15367 | 42082 | 8106888 |
| store payload as ASCII | 28275 | 72802 | 15185193 |
| **external-ref estimate** | **33065** | **82709** | **15431769** |
| against per-file ASCII | 2.25x smaller | 4.5x smaller | 1.70x smaller |
| against ideal ASCII dedup | 1.17x larger | 1.14x larger | 1.02x larger |

Four things to read off it.

**The scheme captures nearly all of the available dedup** -- 98% on the real
document -- because it shares the store's invariant of writing each distinct
TShape once. What it gives up against the store is not dedup, it is ASCII:
1.87x on this document.

**On a real model, more of the store's win is binary than dedup.** 26181094
ASCII against 13977247 binary is 1.87x for the encoding; 13977247 against
8106888 is **1.72x for the dedup**. The headline 3.2x is the product, and the
two halves are separable -- which the 20-torus scene, built to maximize
sharing, entirely hides.

**Reference counts stay legible, and the exception is diagnosable.** The
median file needs **one** reference and 102 of 377 need none. The distribution
is then a long tail -- 153, 134, 118, 116 -- and one outlier: **`PolarPattern003`
at 5129**, followed by `Boolean001` at 153. The type mix there (Edge 5479, Face
2367 across the document) says why: a PartDesign pattern fuses its copies into
the body, so the result shares thousands of individual edges and faces with its
source rather than whole located solids. Patterns and booleans are the shape of
the tail, and a per-file reference cap that falls back to inlining beyond it
would trade bytes for readability in exactly those files.

**Compressed, the gap to the store is small.** Raw bytes are what a directory
project costs and what git's working tree holds, but a `.FCStd` pays deflated
bytes, and deflate eats most of the encoding difference:

| | deflated |
|---|---|
| per-file ASCII, today (687 entries) | 4373466 |
| **external-ref, full sharing** | **~3200000 to 3306000** |
| binary central store | 2599202 |

Derived from ratios measured on this document: one-stream ASCII costs **1.23x**
over binary compressed, against 1.87x raw, and the many-member penalty at this
scale is only **1.03x** -- not the 1.95x the twelve-file scene suggested, which
was an artifact of tiny members. So full sharing in ASCII is **1.35x smaller
than the file the fork writes today**, and about 600 KB larger than a binary
store on a 3 MB document.

Two things that comparison must not be allowed to hide. The store file's
headline advantage is not all store: its non-shape payload also fell from
770182 to 426882 deflated, which is the schema-5 compact property encodings.
And **the prize is not bytes at all** -- 42% of this document's serialized
geometry is duplicate (26181094 inline against 15185193 deduped), so restoring
sharing removes that fraction from memory and from tessellation.

**No per-file reference cap.** An earlier draft of this section proposed
falling back to inlining beyond some reference count, to keep a file readable.
The numbers refute it: `PolarPattern003.Shape.brp` is **2207068 bytes raw /
300418 deflated** today, the second largest entry in the document, and a cap
would preserve that rather than collapse it -- roughly 7% of the compressed
shape payload spent on one file, plus the loss of sharing for 5129 sub-shapes.
The readability argument fails too: 5129 reference lines is on the order of
80 KB of text, against 2.2 MB of coordinates. The referenced file is smaller
*and* more legible.

The counts above are from a run whose candidate scan effectively did not cap
(15 lookups hit a 4096 limit). An earlier run capped at 256 hit it 31454 times
and under-detected sharing -- 80808 distinct TShapes instead of 71185 -- while
producing the same reference total and the same size estimate to within 80
bytes.

### 11.9 Still open

- Restore cost is unmeasured, and it is the one direction where this design
  could lose: reading one object now transitively opens the files it
  references, so `PolarPattern003` pulls in everything it borrows from. Fewer
  total bytes, more opens. Measurable as soon as there is a reader.
- Deterministic visit order has to be established, not assumed (sec 11.7).
- Whether the **hasher** is invariant under location stripping. The element map
  is not a concern -- see sec 7, it is `IndexedName`-based.
- Export of a subset must inline what it cannot carry: a reference to a file
  outside the export set has to become geometry again.

## 12. Build order for the external-reference design

Four steps plus an optional fifth. Each is separately shippable, each has a
gate with a number already measured to compare against, and the order puts the
infrastructure first so the format change lands on something that works.

### 12.1 Step 1: the blob manager's content index and stable names

No format change, no shape work. `docs/FileBlobsManager.md` sec 13 is the
specification: `blobs/Content.xml`, uuid names in the transient directory,
derived `Object.Property.ext` names when saved, the `<objectId>:<Object>.<Property>`
generation token, collision suffixes pinned in the index, and pruning of names
the previous index listed.

***The incremental skip must change in the same commit.*** `exists(dir +
blob->hash())` (`FileBlobManager.cpp:279`) is sound only while the name is the
content; it becomes a comparison against the hash the previous index recorded
for that name, or changed content silently keeps its old bytes.

Gate: `src/Mod/Test/FileBlobs.py` extended -- a name survives close, reopen and
edit; changed content under a stable name actually changes on disk; a collision
suffix does not migrate between saves; orphans are pruned and nothing outside
the previous index is touched; saving an unmodified directory project twice
leaves every file byte-identical.

### 12.2 Step 2: location canonicalization

Independent of everything else and worth shipping on its own: write the shape
with its top-level location stripped and the placement as an attribute on
`<Part/>`, next to the existing `store=`/`pos=`/`file=` fork
(`PropertyTopoShape.cpp:445`).

***The trap*** is sec 11.4's: `Feature::shouldApplyPlacement()` is
`isRecomputing()`, so during restore a `Shape` change overwrites `Placement`
from the shape's own transform. The location must be re-applied inside
`PropertyPartShape::Restore` before `hasSetValue()`.

Gate: placements survive a round trip on a document with placed parts; moving
one object leaves its shape file byte-identical (which is the point, and which
the step-1 skip then turns into no write at all); and on `scanner.FCStd` the
fastener groups collapse -- 14 groups, 36 objects, 875496 bytes, measured in
sec 11.4.

### 12.3 Step 3: shape files through the blob manager

`PropertyPartShape` becomes an ordinary blob referrer: named `Box.Shape.brp`,
skipped by hash, pruned, indexed. This is where `addPendingReferrer` and the
collect pass stop being `PropertyFileIncluded`-shaped.

**Deferred read by name is required, not optional.** Shapes dominate the entry
count, so the manager must leave an entry unread and serve it on demand rather
than draining everything in `readFiles`; the content index is what makes that
possible, since names and hashes are known before any content is touched.

Gate: `docs/DocumentLoad.md`'s open timings on `MiSTer_imported.FCStd` unchanged,
and `DeferShapeLoad` still reads no geometry at open.

### 12.4 Step 4: the external reference format

The writer emits a `Files` table and an `E<file> <index> <loc>` token for a
sub-shape already owned by an earlier object's file (sec 11.6); the reader
resolves it through the blob manager, parses that file whole, caches it, and
takes `Shape(index)`. The reverse-index encoding becomes a forward index so a
token means something on its own.

Ownership is recomputed from scratch every save in a deterministic order
(sec 11.7). Parked objects are inert; a changed file forces its parked referrers
to load, which the index's blob-to-blob edges identify.

Gate, all against sec 11.8's measurements of `scanner.FCStd`:

- sharing is restored -- `isPartner` true across the reference, and the
  20-torus scene's parent leaf against child 0 as in sec 10.6;
- **8408 references over 377 files**, 102 of them needing none, `PolarPattern003`
  at 5129 -- a materially different count means the ownership pass disagrees
  with the probe;
- **~15.4 MB raw / ~3.2-3.3 MB deflated** against 26181094 / 4373466 today;
- a file that references nothing is byte-identical to what `BRepTools::Write`
  produces, so most of a project stays standard BRep;
- two consecutive saves of an unmodified document produce identical files,
  which is what the deterministic order buys and what sec 11.3's git result
  depends on.

### 12.5 Step 5, optional: unifying restored duplicates

Mostly free after steps 1-3: equal solids are equal files, so a
`hash -> TopoShape` parse cache makes one parse serve every referrer. The
in-memory pass for legacy files and recompute results is sec 7 -- serialize
location-stripped, hash, group, then
`setShape(canon.getShape().Located(loc), /*resetElementMap=*/false)`. Do not
pre-filter on `Volume`/`Area`; it is four times slower than serializing
everything.

Gate: the pass must not touch the objects, or opening a document marks it
modified.

### 12.6 What is deliberately not being built

- **The central store stays as sec 10 shipped it**, gated to archives. It is
  not extended, and steps 2 and 3 of sec 9 are not done.
- **The per-component store**, which sec 11 reached before external references
  and which introduced partial referring, is dropped.
- **A random-access ASCII format.** Positional addressing existed to pull one
  shape out of a monolith; with one file per object there is no monolith. The
  three obstacles in `TopTools_ShapeSet` stop mattering rather than needing to
  be fixed.
