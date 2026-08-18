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

***The premise below is false under a face, and sec 12.4 measured what that
costs.*** A face keys its edges' 2D curves on the `Geom_Surface` object it
carries, and an edge keys its vertices' parameters on its curve; those
identities do not survive the other file being parsed separately. A reference
may cross into a compound, a compsolid or a solid and nothing else, which
leaves 2.1% of the shape bytes on a real model rather than the 1.70x this
section goes on to price. Read sec 12.4 before this.

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

- ~~Restore cost is unmeasured~~ -- measured in sec 12.4 and there is no
  penalty: open and full parse are unchanged, because the files opened
  transitively are the ones the document reads anyway and each is parsed once.
- ~~Deterministic visit order has to be established~~ -- it is `objectArray`,
  the order `Document::Save` already walks for `beforeSave`.
- Whether the **hasher** is invariant under location stripping. The element map
  is not a concern -- see sec 7, it is `IndexedName`-based.
- Export of a subset must inline what it cannot carry: a reference to a file
  outside the export set has to become geometry again.

## 12. Build order for the external-reference design

Four steps plus an optional fifth. Each is separately shippable, each has a
gate with a number already measured to compare against, and the order puts the
infrastructure first so the format change lands on something that works.

### 12.1 Step 1: the blob manager's content index and stable names -- DONE

**Shipped 2026-08-17.** `docs/FileBlobsManager.md` sec 13 is the specification
and sec 13.8 records the three decisions it left open, all of which the built
form answers: the previous index is read from the directory being written
rather than remembered (a save-as would otherwise let another directory's index
vouch for a file here), a restore claims the index but does not parse it
(deferred read by name is step 3's, and that is what would need it), and
40-hex names are pruned as the one exception to "nothing outside the previous
index", because they are what a pre-index save wrote and nothing else can
identify them afterwards. Names are `Object.Property.ext` from
`Property::getFileName()`, now public so the manager spells them the same way a
property that writes its own file does -- which is what keeps step 3 from
renaming every shape file on its first save.

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

### 12.2 Step 2: location canonicalization -- DONE

**Shipped 2026-08-17.** The shape is written with its top-level location
stripped and the location spelled as a `loc=` attribute on `<Part/>`, next to
the existing `store=`/`pos=`/`file=` fork (`PropertyTopoShape.cpp`).

The attribute is the 3x4 of the transformation, row major, scale included --
`loc="1 0 0 11 0 1 0 -22.5 0 0 1 3.25"`. Each number is the shortest text that
reads back as the very same double (`std::to_chars`), so round values stay
round in a diff and an inexact restore -- which would be a different shape --
cannot happen. `gp_Trsf::SetValues` reads it back.

Gated on `writer.getSchemaVersion() >= 5`, the same gate as the store, and for
a harder reason than taste: an older reader ignores `loc=` and would announce
the geometry at the identity, which zeroes every placement (below). Every
writer that strips is a writer whose reader puts it back -- the export writer
(clipboard, `copyObject`) carries no schema at all and so never strips.

***The trap*** was sec 11.4's, and the fix is the one it named:
`Feature::shouldApplyPlacement()` is `isRecomputing()`, so outside a recompute
-- restore included -- a `Shape` change overwrites `Placement` from the shape's
own transform. The location goes back on before `setValue()` announces
anything, at all three places geometry can arrive: inline in `Restore()`, the
archive member in `RestoreDocFile()`, and the store in `serveFromStore()`. That
last one is the one that bites, because a stored shape is served on first use,
long after the placement was restored.

*Found while building it.* `TopLoc_Location::IsIdentity()` answers whether
there is a datum, not whether it moves anything: setting a placement of zero
builds a location holding an identity transformation, and every `Part::Feature`
whose placement was ever touched carries one. So the geometry is stripped
whenever the location is non-empty, and the attribute is written only when the
location is not *exactly* the identity -- otherwise a box that was never placed
and one placed at zero serialize differently, and the attribute says nothing.

*Where the geometry actually is*, which the gate had to be measured against: a
directory project writes ASCII BRep inline in each object's XML (`FileVersion`
2 and `ForceXML` 3 are set only for the non-archive writer -- that inline ASCII
is what the directory format exists for), while an archive puts every shape in
the store as one blob. The `file=` member is the recovery writer's path.

Gate: `src/Mod/Test/ShapeStorage.py`, 9 cases, `FreeCADCmd -t ShapeStorage`.
Placements and element maps survive a round trip and the document comes back
up-to-date; serving a stored shape after the open does not zero the placement;
an identity placement writes no attribute; moving an object leaves the stored
geometry byte-identical through both writers (which is the point, and which the
step-1 hash skip then turns into no write at all); two documents differing only
in placement store identical geometry; schema 4 is unchanged and writes no
attribute; and a cross-document copy still arrives placed.

Still owed: the confirmation on `scanner.FCStd` -- 14 groups, 36 objects,
875496 bytes collapsible, measured in sec 11.4. That payoff is only *realized*
once each shape is its own content-addressed blob (step 3); inside one store
the equal-but-unshared duplicates are still written out one by one.

### 12.3 Step 3: shape files through the blob manager -- DONE

**Shipped 2026-08-17.** `PropertyPartShape` is an ordinary blob referrer: its
geometry is one file named `Box.Shape.brp` after the property that owns it,
addressed by content, skipped when unchanged, pruned when orphaned, and listed
in `blobs/Content.xml` with every property that refers to it. The pending
channel is keyed on `App::BlobReferrerProperty`, which `PropertyFileIncluded`
and `PropertyPartShape` both implement.

**No save writes a central store any more**, so its write path is gone --
`writesStore`, `prepare` and `collect` with it. What is left reads the
documents that have one, and the first save moves them out. This is a
correction to sec 12.6's "gated to archives": there is no case left in which a
store would be written, and unreachable code is worse than deleted code.

*Sharing comes back, and more of it.* Two objects over one TShape serialize to
identical bytes once the location is canonicalized out (step 12.2), so they are
one file -- and so are two equal parts that never shared anything in memory,
which the store could not collapse at all. A parse cache keyed on the blob then
makes one parse serve every referrer, so they come back as one TShape. That is
sec 12.5's first half, pulled forward because without it this step would have
*lost* the sharing the store provided.

*The blob is held for as long as it still describes the value*, which is what
makes a save write nothing for a shape that did not change. A move keeps it:
the shape differs only in a location the file does not carry.

***Deferred read by name turned out not to be needed.*** The reasoning was that
shapes dominate the entry count, so draining them at open would be ruinous.
Content addressing removes the premise: 17800 shapes became 10873 files, and
the walk over them is *cheaper* than the old one over 68235 registered entries.
Entries are still drained at open; making them lazy is an optimization now, not
a prerequisite.

**Measured** on `MiSTer_imported.FCStd`, 17743 objects carrying a shape:

| | entries | bytes | open | full parse |
|---|---|---|---|---|
| as it was (`file=` per property) | 68235 | 49880864 | 4.06s | 15.50s |
| as blobs | 10874 | **25256372** | **2.37s** | 15.28s |

Half the bytes, a sixth of the entries, and the open is 1.7x faster rather than
unchanged. The parse is still deferred -- both columns pay the same ~15s the
first time every shape is touched, so nothing was read at open. Re-saving cost
8.91s.

***A behaviour change worth knowing about.*** Restoring TShape sharing means
two objects whose geometry *and* placement are identical come back as the same
`TopoDS_Shape`. Anything that enumerates sub-elements through a
`TopTools_IndexedMapOfShape` -- `Shape.Faces`, `Shape.Solids` -- then reports
them once instead of twice. On this document one `App::Part` aggregate went
from 415239 faces to 403899. **No geometry is lost**: the compound still holds
every child and its volume is unchanged; it is the enumeration that collapses.
The central store had the same property, and sec 12.5 asks for more of it.

Gate: `src/Mod/Test/ShapeStorage.py`, 17 cases (`ShapeBlobCases` added),
`FreeCADCmd -t ShapeStorage`; `FileBlobs` unchanged at 64.

***A latent corruption this uncovered.*** Restored content was copied with
`entry >> to.rdbuf()` -- a formatted extraction, whose sentry skips leading
whitespace, so any included file starting with whitespace came back short.
ASCII BRep starts with a newline, and content addressing turned a quiet
corruption into a hash that no longer matched what the document referred to:
the property was served nothing at all. `PropertyFileIncluded::RestoreDocFile`
had the same line and the same bug. Several other places in the tree still use
the idiom (`ProjectFile`, `VRMLObject`, `PropertyPythonObject`) and were left
alone.

### 12.4 Step 4: the external reference format -- BUILT, REVERTED, RESTORED

(Reverted on the 2.1% below, then restored behind a switch once that number
turned out to be about the model rather than the format -- see sec 12.7.)

**Built and reverted 2026-08-17** (`353f31b62a`, `4baa4757d0`, `3f409a216c`,
`db474bfafd`, reverted by `1a4b34e640`). It works and it is correct; it is
reverted for what it is worth once it is correct -- 2.1% of the shape bytes on
`scanner.FCStd`, against the 1.70x sec 11.8 predicted. The commits are on the
branch and revert cleanly back in, and are the thing to build the next attempt
on. Everything below is what it was and what it measured.

A shape file may name other files at its head and then, wherever a sub-shape
is listed, say the sub-shape is in one of them:

```
CASCADE Topology V1, (c) Matra-Datavision
Files 2                  <- absent when nothing is borrowed
8f1c...                  <- content hash of the file borrowed from
a30b...
Locations 3
TShapes 17
+12 3                    <- as today: shape 12 of this file, location 3
E1 +7 3                  <- new: shape 7 of file 1, location 3
```

`Part::ShapeRefSet` writes and reads it, `Part::ShapeOwnerTable` is what the
save accumulates as it walks. **No OCCT change was needed**, but it is not an
extension either: `TopTools_ShapeSet::Add`, `Write(S,OS)` and `Read(S,IS)` are
not virtual and the shape map is private, so those three are reimplemented --
over `BRepTools_ShapeSet`'s geometry hooks, every one of which *is* public and
virtual, so the geometry encoding is still exactly OCCT's.

*The index in an `E` token is forward; the ordinary token keeps OCCT's reverse
one.* Sec 12.4 as planned said the encoding "becomes a forward index".
Converting the local one as well would make every borrowing file's local part
differ from what OCCT writes, for nothing: a reverse index is relative to a
shape count, and within one file that is self-contained, because the reader has
read `TShapes N` before it reads any token. Only the external index has to be
forward, because this file cannot state another file's shape count.

**Three departures from sec 11.6 and 11.7, each forced.**

***A reference names a file by its content hash, not by the name it is saved
under.*** Sec 11.6 wrote the name. A save cannot know it: names are assigned by
`FileBlobManager::planSave()` after every property has been written, and can be
pinned or suffixed by the previous index. The hash is known when the file is
made, is already what `Save()` writes as `hash=`, and `find()` already resolves
it. The cost is 40 hex characters in the table instead of a readable name; the
git behaviour is the same either way, because a changed target rewrites its
referrers regardless.

***A file is kept only when the plan also matches, not just the shape.*** A
file's bytes are its geometry *and* what the save decided to borrow, so an
unchanged shape is not on its own a reason to keep the file written for it: an
object that borrowed from a file which has since gone would otherwise keep a
reference to a file this save does not write, and the geometry would be
unreachable on the next open. `ShapeRefSet::plan()` states what a file borrows,
order-independently, and is produced both by writing a file and by parsing one
-- so a reopened document knows what its own files say and does not have to
rewrite all of them to find out.

***Parked referrers are not forced to load, because nothing stays parked
through a save.*** Sec 11.7's rule 2, and the blob-to-blob edges that were to
identify which files it applies to, are not built. `PropertyPartShape::
beforeSave` opens with `ensureRestored()`, so a save materializes every shape
before the analysis sees it, and the parked case sec 11.7 reasons about does
not arise. That also removes the reason for the edges: a file this save does
not write cannot still be referenced, because the plan check rewrites whatever
referred to it.

***The save generation is counted across the process, not per document.*** The
owner table has to be thrown away between saves and there is no end-of-save
signal, so `FileBlobManager::saveGeneration()` is what says the last one is
over. Counting per document is wrong in a way that is invisible until it bites:
a closed document's address is handed straight back to the next one, so a new
document's first save matches a stale table built for a dead one -- whose
TShape addresses have since been freed and reissued, so the lookups answer
confidently and wrongly. Three suite cases failed on exactly that.

***What a reference cannot cross, and what it cost.*** Sec 11.5's premise --
"where a sub-shape is already stored in another object's file, write a
reference to that file instead of the geometry" -- **does not hold below a
face**. A face keys its edges' 2D curves on the `Geom_Surface` object it
carries, and an edge keys its vertices' parameters on its curve. Those are
identities of objects in one file's tables, and identity does not survive the
other file being parsed separately: the borrowed edge comes back with pcurves
naming a surface that is not this face's, `BRep_Tool::CurveOnSurface` finds
nothing, and whatever projects the shape dereferences a null 2D curve.

Measured on `scanner.FCStd` saved as a directory and reopened: **124 faces
with edges carrying no curve on them, and 127 shapes invalid where 4 were
invalid before**. TechDraw then segfaulted inside OCCT 8.0.1 HLR on exactly
that, which is how it surfaced; with borrowing disabled the same path was
clean. A wire and a shell are the other half of it -- they are sewn, so half
their faces borrowed and half stored gives two edges everywhere the model has
one, and that alone still left 119 shapes invalid.

So each of the four is borrowed whole or stored whole, and what stays
borrowable is what sits directly in a compound, a compsolid or a solid --
whole parts of an assembly, which is the sharing sec 8 exists for.

**Measured** on `scanner.FCStd` (606 objects, forced full recompute), against
the same build with borrowing disabled, which is exactly what sec 12.3 writes:

| | 12.3, one file per object | 12.4, references |
|---|---|---|
| shape files | 332 | 332 |
| files borrowing nothing | 332 | 306 |
| distinct sub-shapes borrowed | 0 | 36 |
| raw shape bytes | 18670216 | **18276205** |
| deflated shape bytes | 3806881 | **3735837** |
| whole archive | 4569978 | 4498940 |

and, with TechDraw parked so its own OCCT 8.0.1 crash is not what is timed:

| | 12.3 | 12.4 |
|---|---|---|
| blob bytes | 18999246 | 18605235 |
| open | 0.561s | 0.563s |
| full parse of every shape | 2.392s | 2.417s |
| faces restored | 90686 | 90686 |
| faces missing a curve / invalid shapes | 0 / 4 | 0 / 4 |

***Sec 11.9's one real risk did not materialize.*** Restore cost was the
direction this could lose -- reading one object now transitively opens the
files it borrows from, so fewer bytes but more opens. There is no penalty:
open and full parse are unchanged inside noise, because the files opened
transitively are the ones the document reads anyway and the parse cache means
each is parsed once.

***Sec 11.8's prediction is not reachable in this format, and was never going
to be.*** It predicted 15431769 raw, 1.70x, from a probe that walked top-down
emitting a reference at the first sub-shape an earlier object owned. Its own
type breakdown says what those were: **Edge 5479, Face 2367, Vertex 335**.
Every one of those is a reference this cannot make. Run without the safety
rule the prediction lands almost exactly -- 15453310 raw, 0.14% out, 3224998
deflated, 1.694x and 1.356x against sec 11.8's 26181094 / 4373466, and 1.018x
of ideal ASCII dedup against 1.02x predicted -- and it corrupts the document.
**Sec 11.8's numbers should be read as what a scheme with shared geometry
tables could reach, not as what per-file references can.**

What is left is **2.1% raw and 1.9% deflated on this document**, which is a
PartDesign model: 36 references, because a feature chain shares faces and
edges, and those are exactly what cannot be crossed. A model that repeats
whole parts -- an assembly, the 20-torus scene of sec 10.6 -- is where the
remaining mechanism pays, and that is not measured here.

***Whole shapes are shared, not referenced.*** An object whose entire root
TShape another file holds takes that file's blob rather than writing a
reference to it. The first cut of this step did write the reference, and it
cost more than its size: 332 files became 362, and files with more than one
referrer fell from 43 to 14, so 29 files that had been shared were written
twice over. Borrowing pays below a root, where there is geometry to leave out;
at the root it is pure indirection, and content addressing already had it
right.

**Gate**: `src/Mod/Test/ShapeStorage.py`, 24 cases (`ShapeRefCases` added),
`FreeCADCmd -t ShapeStorage`; `FileBlobs` unchanged at 64; and the format
itself in `tests/src/Mod/Part/App/ShapeRefSet.cpp`, 6 cases in their own
executable, including **a file that borrows nothing being byte-identical to
what `TopoShape::exportBrep()` writes**. `Part_tests_run` still does not link
on this tree for reasons that predate this, but one of them was that a target
declared in `tests/` never saw OCCT's library path -- that part is fixed.

***Two pre-existing faults this uncovered, neither fixed here.***
`Part::PropertyTopoShapeList::Restore` reads the `file` attribute
unconditionally, so the inline form a directory project writes throws
`XML Attribute: 'file' not found` -- 119 TechDraw dimensions lost their
`SavedGeometry` on a directory save of `scanner.FCStd`, and the file is
untouched by any of this work. And TechDraw's HLR **segfaults inside OCCT
8.0.1** on this model when its views recompute, which is why the measurements
above leave TechDraw alone.

***What would recover it, and is not built.*** The whole loss is that each
file has its own geometry tables, so a surface parsed twice is two objects. A
format where a file could also reference another file's *geometry* -- "surface
7 of file 1" in a face's record, "curve 3 of file 1" in an edge's -- would put
every association back inside one identity domain and make the sub-face
references safe again. That is a much larger change than this step: it means
reimplementing `BRepTools_ShapeSet::WriteGeometry` and `ReadGeometry` per
shape type rather than delegating to them, which is where all of OCCT's curve
representations, regularity, tolerances and triangulation live. Sec 11.8's
1.70x is the prize for it.

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

- **The central store stays as sec 10 shipped it** -- superseded by sec 12.3,
  which took its write path out because nothing could reach it any more. What
  remains reads the documents that have one. Steps 2 and 3 of sec 9 are not
  done and now never will be.
- **The per-component store**, which sec 11 reached before external references
  and which introduced partial referring, is dropped.
- **A random-access ASCII format.** Positional addressing existed to pull one
  shape out of a monolith; with one file per object there is no monolith. The
  three obstacles in `TopTools_ShapeSet` stop mattering rather than needing to
  be fixed.

### 12.7 The format restored behind a switch, and what it is worth by model shape

Step 12.4 was reverted on a single number -- 2.1% on `scanner.FCStd`. That
number is a property of the model, not of the format, and the revert made it
hard to check which. It is back (`ShareStoredSubShapes`, on by default). With
the switch off, `PropertyPartShape::saveBlob` passes no owner table, so nothing
is borrowed and nothing is published and every file holds its whole shape --
byte for byte what sec 12.3 wrote. **The A/B is now one build, not two**, and
the arms reproduce the figures recorded above exactly (332 files, 36
references, 18276205 raw, 3735837 deflated against 18670216 / 3806881).

The switch gates the write path only. A file that names another file still
resolves it whatever the setting: a format switch may stop producing a
construct, never stop reading one.

***What the safety rule is worth, by what the document is made of.*** Same
build, same probe, borrowing off against on:

| | `scanner.FCStd` | assembly of compounds | `MiSTer` |
|---|---|---|---|
| what it is | PartDesign, 606 objects | 40 parts, 8 sub-assemblies, 1 top | imported STEP, 18142 objects |
| references | 36 | 48 | **0** |
| raw, off | 18670216 | 2432234 | 117934869 |
| raw, on | 18276205 | 807253 | 117934869 |
| **raw** | 1.02x | **3.01x** | **1.00x** |
| deflated | 1.02x | 2.54x | 1.00x |

So the mechanism pays where a document *stores* a parent whose shape is a
compound of other objects' whole solids -- `Part::Compound`, `SubShapeBinder`.
scanner has 36 such places; the synthetic scene is made of nothing else and
reaches 3.01x, which is its ceiling of three copies collapsing to one.

***An imported assembly is not that shape, and this corrects sec 12.4.*** Sec
12.4 supposed "a model that repeats whole parts -- an assembly -- is where the
remaining mechanism pays". `MiSTer` is exactly that and emits **no references
at all**. `App::Link` and `App::Part` store no shape, so an imported or linked
assembly has no parent object holding its children's TShapes; its repetition is
whole-*shape* repetition, and content addressing already collapsed it in sec
12.3 -- 18142 objects to 10872 files.

! Measure this on an **ASCII** save. References are an extension of the ASCII
format and of nothing else, so `saveBlob` returns early under `BinaryBrep` and
a document with `PreferBinary` set shares nothing whatever the switch says.
Read binary first and the null result looks like a fact about the model.

### 12.8 The ceiling for cross-file geometry, measured

Sec 12.4 closes by naming cross-file geometry references as what would recover
the sub-face sharing the safety rule forbids, with sec 11.8's 1.70x as the
prize. That prize was quoted against the pre-12.3 format. Against the baseline
that ships, the ceiling is worth measuring directly rather than inferring, and
it does not need the format to exist: take a borrowing-off directory save, read
each file's three geometry tables, and count the entries whose encodings repeat
across files.

**Byte-identical encoding is the right identity test, not a convenient one.**
What a reference has to guarantee is that the borrowing file ends up holding
the object the borrowed sub-shape's pcurves name. Identical encodings parse to
equal geometry, so referencing one for the other is sound whether or not they
were one object when written.

| `scanner.FCStd`, 332 files, 18670216 shape bytes | entries | distinct | bytes | distinct | duplicate |
|---|---|---|---|---|---|
| Surfaces | 8497 | 5140 | 3185468 | 1829259 | 42.6% |
| Curves | 20312 | 15585 | 3460004 | 2593347 | 25.0% |
| Curve2ds | 25603 | 5688 | 4287305 | 1933887 | 54.9% |

| `MiSTer`, 10872 files, 117934869 shape bytes | entries | distinct | bytes | distinct | duplicate |
|---|---|---|---|---|---|
| Surfaces | 70321 | 38792 | 20000374 | 10325309 | 48.4% |
| Curves | 221160 | 113049 | 31988822 | 15602076 | 51.2% |
| Curve2ds | 85955 | 27060 | 26168673 | 12148469 | 53.6% |

***It splits in two, and only one half needs a new format.*** A
`GeomTools_SurfaceSet` keys on the handle, not on the value, so a single file
can write the same encoding twice by itself:

| | scanner | `MiSTer` | assembly |
|---|---|---|---|
| geometry tables, share of all shape bytes | 58.6% | 66.3% | 42.5% |
| duplicate geometry, share of all shape bytes | 24.5% | 34.0% | 37.0% |
| -- within one file, **needs no cross-file format** | **8.9%** | **5.9%** | |
| -- across files, what a reference buys | **15.6%** | **28.1%** | |

So a cheap win sits underneath the expensive one, and the per-table split says
which part of it is safe to take:

| within-file duplicate bytes | scanner | `MiSTer` |
|---|---|---|
| Surfaces | 123717 | 685419 |
| Curves | 110227 | 2288557 |
| **Curve2ds** | **1425884** | **3960055** |

It is nearly all pcurves -- 86% of it on scanner, 57% on `MiSTer` -- and the
pcurve table is the one that can be keyed by value without argument. A pcurve is
reached through the **surface** an edge's representation names, never by the
identity of the 2D curve object, so two representations sharing one
`Geom2d_Curve` cannot be told apart from two holding equal copies. That does not
hold for the other two tables: a vertex's parameter is keyed on its edge's
curve, and an edge's pcurve on its face's surface, so merging equal-but-distinct
objects there would make exactly those lookups ambiguous -- the defect class
that produced the HLR crash in sec 12.7's footnote. They are left alone.

**Done, in the OCCT fork.** `GeomTools_Curve2dSet::Add` returns the index of an
entry that would be written identically, keyed by a hash of the written form
with a full comparison to settle a collision. `Read()` still fills the map
directly, so an existing file whose table does hold equal entries reads back
with its indices intact. On `scanner.FCStd`, with the reference format on
either way:

| | before | after |
|---|---|---|
| raw shape bytes | 18276205 | **16839759** (-7.86%) |
| deflated | 3735837 | **3637241** (-2.64%) |
| files / references | 332 / 36 | 332 / 36, unchanged |
| faces / missing a curve / invalid | 90686 / 0 / 4 | 90686 / 0 / 4, unchanged |

The last row is the one that matters, and it did not come free. The first cut
of this deduplicated the table correctly and then wrote every shape record with
index 0, because the records are written through `Index()`, which looks up by
handle -- and a merged curve is not in the map, its twin is. The saved project
lost **25443 face pcurves** and gained 217 invalid shapes, and the reference
format's own counts were unchanged throughout, which is exactly how a defect
like this hides.

Two things the cross-file half is not. It is not the whole prize: the rest of a
file is locations and the TShape records, and the records collapse too wherever
a sub-shape can be borrowed. And it is not bytes only -- the same duplication is
paid again in memory and in tessellation every time the document is opened.

***What it would take.*** Sec 12.4 guessed "reimplementing
`BRepTools_ShapeSet::WriteGeometry` and `ReadGeometry` per shape type". It is
less than that. The per-shape records only ever emit *indices* into the three
tables, so they can keep delegating; what has to change is how a **table** is
written and read -- an entry becomes either an inline definition or a name of
another file's entry. `GeomTools_SurfaceSet` and its two siblings already carry
`Add()`, `Index()`, element access and now `Extent()`, which is all a table
writer needs. The blocker was that `BRepTools_ShapeSet` kept `mySurfaces`,
`myCurves` and `myCurves2d` private with no accessors; the fork now exposes
them.

### 12.9 The larger prize this uncovered: the same part written again elsewhere

Content addressing (sec 12.3) dedups equal **bytes**. A STEP exporter that bakes
each instance's transform into its coordinates defeats it outright: the same
part at twenty places is twenty distinct contents and the storage pays for all
twenty. Sec 12.2 strips a location carried *beside* the geometry; it cannot
strip one that was multiplied *into* it.

Measured on `MiSTer` -- an assembly exported by exactly such a tool -- by
grouping every stored shape on quantities a rigid motion cannot change: volume,
area, and the sorted spectra of face areas and edge lengths. Rotation-proof,
unlike a bounding box, and matching a whole sorted spectrum rather than totals
makes a coincidental match unlikely.

| | |
|---|---|
| objects with a stored shape | 17058 |
| distinct contents, after content addressing | 10872 (**1.57x**) |
| distinct shapes by rigid-motion invariants | **1446** |
| files repeating a shape already stored | 9426 of 10872 |
| **removable bytes** | 33686314 of 117934869 = **28.6%** |

The largest single case is a **2129-face part stored twice**, as two distinct
files of about 3.5 MB each, differing only in where they sit.

***This is a different mechanism from cross-file geometry, and the two barely
overlap.*** Two congruent parts at different positions encode every surface
differently, so none of their bytes appear in sec 12.8's cross-file duplication;
that 28.1% comes from geometry genuinely written identically in two files
(coincident mating planes, and the per-edge and per-face objects this document
is full of, which repeat their parent's geometry). The prizes are close to
additive.

***What it would take, and why it is harder than it looks.*** Content
addressing needs only a hash. This needs the **transform recovered** between two
instances before either can be stored once and placed twice, and the invariants
above only propose candidates -- they do not prove congruence, and they say
nothing about which rigid motion maps one to the other. A mirrored instance
shares the same invariants and is not a rigid motion at all. Against that, the
payoff is not only bytes: a document that resolves to 1446 shapes instead of
10872 tessellates 1446 times, which is the sec 11.8 point about the prize not
being bytes, in a much stronger form than a PartDesign model shows.

***Confirmed by recovering the motion, not left as a signature.*** Each shape is
canonicalized by its own vertex cloud -- centroid, then the principal axes -- so
the motion between two instances is one frame composed with the other's inverse.
The axes carry a sign ambiguity and, for a symmetric part, a real degeneracy, so
every axis order and sign that is a rotation is tried; only determinant +1
frames are built, which rejects a mirrored instance rather than miscounting it.
Over the 80 heaviest groups, 35685356 bytes:

    confirmed                73 of 80 groups
    removable in them        26680213 bytes = 74.8% of those examined
    deviations               1e-12 to 1e-16, i.e. exact

So the signature over-groups a little -- 7 groups in 80 do not survive -- and
the whole-document 28.6% should be read as an upper bound of roughly a quarter,
not as a settled figure.

***And it is two phenomena, not one.*** Of the confirmed bytes:

| | groups | bytes |
|---|---|---|
| genuinely moved -- the baked transform | 58 | 20763789 |
| **same place, differing only in the digits** | 15 | **5916424** |

The second was not expected. The largest case in the document is a 2129-face
part in two files of about 3.5 MB, at **translation 0 and rotation 0**: 1438 of
114139 lines differ, all of them trailing digits of `Curve2ds` coefficients at
about 1e-13. The same part was written twice with its pcurves recomputed
independently. No transform is involved, so this fifth of the prize looked much
cheaper to take than the other four.

**It is not.** Sec 12.11 measured that reading and withdraws it: the two files
do not hold the same numbers, and no comparison of encodings can merge them.

### 12.10 Can the pcurves simply be dropped and recomputed?

A pcurve *can* be computed on demand, and OCCT already does it -- for planes
only. `BRep_Tool::CurveOnSurface` walks the edge's representations, and when it
finds none it falls through to `CurveOnPlane`, which projects the 3D curve onto
the plane and returns the result. For any other surface it returns null. That
null is not a hypothetical: it is what produced the HLR segfault fixed in occt
`da16badbb5`.

***The plane half of the prize is already taken.*** Measured over the 687 shape
files inside `scanner.FCStd`:

| | |
|---|---|
| edge-on-face pairs | 95255 |
| pcurve stored in the file | 27023 (28.4%) |
| computed on demand, planar | **68232 (71.6%)** |
| no pcurve at all | 0 |

Seven of every ten incidences already store nothing. What is left is the curved
faces, and those have no on-demand path in OCCT at all.

***Except that the file does still carry some planar pcurves, and those are
free.*** Of the 27023 stored, **6353 sit on a planar face** -- geometry the
kernel would have projected itself. They are 5879 table entries, **540958 bytes,
1.9% of all shape bytes**. Dropping only those, over the same 20 parts:

| | before | after |
|---|---|---|
| bytes | 12172161 | **11941495 (-1.9%)** |
| triangles | 130903 | **130903, identical** |
| parts `BRepCheck_Analyzer` rejects | 0 | **0** |
| worst deviation from the 3D curve | 1.371e-03 | **1.371e-03, unchanged** |

Nothing is rebuilt, because nothing needs to be: `CurveOnPlane` answers for them
at 1.1 us a call. This is the one part of the idea that survives -- exact, safe,
no format change -- and it is worth 1.9%, not 16.6%. It is also model-shaped:
`MiSTer` stores **zero** planar pcurves, so an imported assembly gains nothing.

***What the remaining pcurves cost to store.*** The `Curve2ds` table is
**4668449 bytes, 16.6% of all shape bytes** and 39.4% of the three geometry
tables. Removing the pcurves removes their per-edge records too, so a part
written without them is smaller than the table alone suggests -- over the 20
largest shapes in the model, **12172161 bytes fall to 9754099, -19.9%**.

! **That figure was first published as -28.4%, measured against a shape that
was not the file.** The two arms need independent `TShape`s, and the obvious way
to get them -- `BRepBuilderAPI_Copy` -- **materialises pcurves the file never
held**: over six parts, 1912 stored pcurves become 8479 and the written size
grows 25.1%. Both arms were then measured against that inflated baseline.
Reading the file a second time is the only faithful way to get an independent
shape, and every number in this section is from that. The correctness results
below are unaffected, being about geometry rather than bytes.

***What they cost to do without.*** Three things, measured by stripping every
pcurve from those 20 parts and asking OCCT to put them back
(`ShapeBuild_Edge::RemovePCurve`, then `ShapeFix_Edge::FixAddPCurve`, which
projects the 3D curve):

| | |
|---|---|
| pcurves projected back | 11024 |
| time to project | 0.85s, **77 us each** |
| worst deviation from the 3D curve, as stored | 1.371e-03 |
| worst deviation, recomputed | **9.335e-03** |
| pairs whose recomputed pcurve exceeds the edge tolerance | **430** |
| parts `BRepCheck_Analyzer` rejects, before / after | 0 / **14 of 20** |
| triangles meshed: stored / stripped / recomputed | 130903 / **43069** / 123242 |

The triangle row is the one that decides it for a viewer. **A shape with no
pcurves does not mesh** -- a third of the triangles survive, and a helix drops
from 8260 to 20. FreeCAD tessellates every shape it opens, so on-demand here
means on-load, for everything, not for what an algorithm happens to touch.

Handing the whole part to `ShapeFix_Shape` afterwards recovers the mesh
(131555 triangles) and takes 14 of the 20 rejected parts down to 6 -- for
**14.36s over 20 parts**, against 0.85s for the projection itself. That is the
real price: a projected pcurve is not the stored one, and healing the difference
costs about 0.7s per part while still leaving parts broken.

***And two kinds of pcurve can never be recomputed at all.***

- **A degenerate edge has no 3D curve** -- its pcurve is its only geometry.
  There are 251 in this model, and nothing can project them back.
- **A seam carries two pcurves on one surface**, and one projection cannot say
  which is which. OCCT's own healer does not try: the seam branch of
  `FixAddPCurve` copies the projected curve, translates it by the period, and
  says so in the source -- *"On ne sait pas laquelle est Forward. Au PIF."*
  1704 pairs in this model are seams.

***The bytes sit exactly where the recomputation is worst.*** By kind of curve,
the pcurve table is:

| | entries | bytes |
|---|---|---|
| `Geom2d_BSplineCurve` | 3696 | **3368235 (72.1%)** |
| `Geom2d_Line` | 23177 | 740499 (15.9%) |
| `Geom2d_TrimmedCurve` | 2932 | 451870 (9.7%) |
| `Geom2d_Circle` + `Geom2d_Ellipse` | 1525 | 107063 (2.3%) |

72% of the weight is BSpline pcurves -- the ones that exist only because a
projection was approximated once already. The analytic pcurves, which projection
does reproduce, are nearly free to store. Sec 12.9 saw the same thing from the
other side: one part stored twice at the same place, differing only in the
trailing digits of its `Curve2ds` coefficients, because the two copies had their
pcurves computed independently.

**Verdict: not taken.** 16.6% of shape bytes, in exchange for a load path that
either meshes wrong or pays 77 us per pcurve plus healing, and a geometry that
no longer round-trips. The dedup of sec 12.8 gets a share of the same bytes with
none of that, because a merged pcurve is the identical curve rather than a
recomputed one.

***Why the duplicates were there to be merged.*** Not by design.
`GeomTools_Curve2dSet` keys its map on the handle, so a curve computed twice is
written twice; OCCT never compares geometry by value anywhere on write. Nothing
in the kernel reads meaning into the identity of a pcurve object:

- No code compares pcurve handles to decide anything -- a pcurve is found
  through the **surface** an edge's representation names.
- OCCT itself puts one `Geom2d_Curve` into both pcurves of a seam
  (`LocOpe_WiresOnShape.cxx`, `c2dff = C2d; c2dfr = C2d;`), so a shared pcurve
  object is a state the kernel produces on its own.
- Sites that shift a pcurve copy first -- `c2d->Copy()` in `ShapeFix_Edge`,
  `Translated()` rather than `Translate()` in `LocOpe` -- and the in-place
  `Translate()` calls operate on curves freshly projected, which no edge holds
  yet.
- The file format has always permitted two records to name one table index, and
  `Read()` fills the map directly from the table.

The risk in merging was never the sharing. It was `Index()`, which is asked for
a curve that is no longer in the map -- the defect measured in sec 12.8, where
25443 face pcurves were silently emptied.

### 12.11 The cheap fifth is not cheap, and the planar pcurve is not worth caching

Two follow-ups from sec 12.9 and sec 12.10, both measured, both negative. They
are recorded because each one removes a plausible piece of work.

***A tolerant content hash buys 0.1%, not 5.2%.*** Sec 12.9 read the 15
same-place groups as differing only in trailing digits, which suggested the
blob manager could take them with a rounded content key instead of the
transform recovery the other 58 groups need. Measured over `MiSTer`'s 10872
shape files by re-printing every number in the file at N significant digits and
hashing that:

| key | distinct files | removable |
|---|---|---|
| exact content hash | 10872 | 0 |
| rounded to 15 digits | 10872 | 0 |
| rounded to 13 digits | 10872 | 0 |
| rounded to 12 digits | 10839 | 65353 (0.1%) |
| rounded to 10 digits | 10835 | **80346 (0.1%)** |

37 groups merge that exact hashing does not, none of them over-merged -- and
they are all small shells worth a few kilobytes each. **The 5916424 bytes are
not among them.**

The largest case says why. Its two files, `Solid1489` and `Solid1263`, 3393786
and 3393707 bytes:

- **They do not hold the same count of numbers** -- 144538 against 144539. Two
  independently computed BSpline pcurves can describe the same curve with
  different poles and knots, so the encodings are not even the same shape of
  object.
- Near-zero terms **flip sign** (`-1.195e-14` against `1.151e-14`), which is a
  relative difference of 100% that no rounding can absorb -- however small the
  numbers are, and they are noise around zero.

***But relative difference is the wrong yardstick, and it overstated the
disagreement.*** OCCT judges in absolute model units: `Precision::Confusion` is
**1e-7**, `Precision::PConfusion` **1e-9**, `Precision::Angular` **1e-12**.
Measured that way over the 719 differing lines of that pair, the worst absolute
difference is **5.0e-12**, on a parameter near pi
(`3.14159278256479` against `3.14159278256979`). That is four to five orders
*inside* linear confusion, inside parametric confusion too, and about 5x the
angular tolerance on one value. ***The two files are the same shape by OCCT's
own measure***, which is what sec 12.9's transform recovery already said at
1e-12 to 1e-16, and it is the ground on which merging them is sound.

What defeats a rounded key is therefore not disagreement about the geometry. It
is that **the encodings are not comparable at all**: different pole and knot
counts, and noise around zero that no digit count can normalise.

Sec 12.9's 1e-13 was right about the size of the disagreement and wrong about
what could be done with it. So there is **one job here, not two**: every
congruent group, moved or not, has to be settled by comparing geometry within a
tolerance -- which is what the invariants plus a recovered motion already do,
deviations 1e-12 to 1e-16 -- and never by comparing what was written. That also
settles a design question the split had left open: the instance that borrows a
stored shape receives geometry that differs from its own recompute by at most
5.0e-12 in absolute terms, which is inside every tolerance OCCT applies to it
bar the angular one. The storage is entitled to call them the same shape; it is
not entitled to call the files equal.

***The planar pcurve costs 1.1 us, so caching it is not worth building.*** Sec
12.10 noted that `CurveOnPlane` re-projects on every call and caches nothing,
and that 76.3% of `MiSTer`'s incidences go that way -- which looked like a load
cost worth removing. Timed over all 362873 incidences:

| | calls | total | each |
|---|---|---|---|
| pcurve stored in the file | 85955 | 0.00s | list walk |
| projected onto the plane | 276918 | **0.31s** | **1.1 us** |

A plane projection of a line or a circle is analytic, not an approximation. One
full sweep of the largest model in the collection spends a third of a second
there. **Not taken.**

### 12.12 Congruent-instance dedup, as built

Sec 12.9 measured the prize and sec 12.11 settled the ground it stands on: two
instances of one part agree to 5.0e-12 in absolute terms, far inside
`Precision::Confusion`, so storing one in place of the other is sound -- while
their encodings cannot be compared at all, not holding the same count of
numbers. What follows is what it took to build.

***No format change.*** `Part::CongruenceIndex` is a per-save index, keyed on
the save generation exactly as the owner table is. A shape that matches one
already written borrows its file, and the motion between them is written as one
new attribute on the property, `motion`. A document that has no repeats writes
nothing new, and an older reader that ignores the attribute reads geometry that
is simply in the wrong place rather than geometry it cannot parse -- which is
why the setting is a document one and not a schema.

***The motion is baked into the geometry on restore.*** This is the part that
had to be learned. The first version composed the motion into the location the
property already writes, which costs nothing and is wrong: a restored shape's
location **is** the object's `Placement`, which is model data. An `App::Link`
that replaces its source's placement with its own reads exactly that, and 1146
links in `MiSTer` drew their geometry where the instance it had been borrowed
from sits. The geometry was right and the model was wrong, which is the worse
of the two failures. Baking costs a copy of the geometry per borrowed instance
-- so this saves file bytes, not memory, and not tessellation.

***What it takes to be sure two shapes are the same part.*** Each of these was
added because the one before it let something through:

| check | what it catches |
|---|---|
| sub-shape counts, vertex radii spectrum | the key; proposes candidates |
| area and volume | an approximation standing in for what it approximates |
| every vertex, in index order | a different part with the same invariants |
| every edge midpoint, in index order | same corners, different edges |
| surface type per face | an exact cylinder against a spline copy of it |
| **surface samples per face** | **same boundary, different interior** |

The last row is the one that surprised. Two patches sharing a boundary can
share every vertex, every edge midpoint, their area **and** their volume, and
still bulge differently: a pair in this model differs by half its depth that
way. A boundary does not determine an interior, and neither do the invariants.

***Sub-shape order is the correspondence, deliberately.*** The motion is
recovered from three spread vertices taken by index and checked against every
vertex by index. That makes the check safe for more than geometry: the
borrowing object keeps its own element map, and those names resolve through
sub-shape indices. An instance whose topology is ordered differently fails to
match and is written out in full.

***Measured on `MiSTer_binary.FCStd`***, saved to a directory in ASCII:

| | off | on |
|---|---|---|
| shape files | 10872 | **5204** |
| raw shape bytes | 113940802 | **85347161 (-25.1%)** |
| save | 9.28s | 26.10s |

| over 894 sampled shapes | with sharing | control, sharing off |
|---|---|---|
| worst relative volume change | 8.4e-11 | 3.1e-14 |
| worst relative area change | 1.1e-09 | 1.1e-14 |
| worst centre-of-mass shift | 4.9e-10 | 5.0e-14 |

The acceptance is a hundredth of `Precision::Confusion`. At the full 1e-7 the
worst centre of mass moved **6.1e-8** and the saving was 27.0%; 1.9 points of
byte saving is a cheap price for two orders of fidelity, and the same trade sec
12.10 made for the pcurves.

! **A bounding box cannot check this work.** OCCT estimates one from surface
poles, so it is not invariant under a rigid motion: a shape stored once and
moved back reports a box differing by 1.4mm while its meshed surface agrees
with the original to **1.8e-15**. Two hours went into a corruption that was not
there. Volume, area and centre of mass are what to compare.

***Still open.*** The save costs 17s more on 18142 objects, which is the
invariants and the verification, unoptimized -- the obvious cut is to compute
area and volume once per shape and keep them beside the owner table rather than
per candidate pair. And 5204 files against the 1446 distinct shapes sec 12.9
counted says most of the remaining repeats are ordered differently, which the
index-order correspondence refuses by design.

### 12.13 Cross-file geometry, as built

Sec 12.8 measured the ceiling and named what it would take: not the per-shape
records, which only ever emit positions in the three tables, but a **table**
writer, so that an entry can be either a definition or the name of another
file's entry. That is what this is.

```
CASCADE Topology V1, (c) Matra-Datavision
Files 1
89987ecc1e35ca9da307a250f47dbd4c0a9fe1f8   <- as sec 12.4: the file named
Locations 0
Curves 12
E1 1                                       <- new: curve 1 of file 1
1 5 0 0 0 0 1                              <- as today: the curve, written out
...
```

`ShapeRefSet` overrides `WriteGeometry`/`ReadGeometry` for the three tables and
delegates everything else, including every per-shape record, to OCCT. A table
nothing was borrowed into is written by `GeomTools_*Set::Write` itself, so a
file that shares nothing is still byte for byte what it always was -- the same
claim sec 12.4 makes for sub-shapes, and it is a test case.

***What an entry is keyed on: the bytes it is written as.*** Not the handle. A
surface two files share in memory is a shared sub-shape and never reaches this;
what this catches is two files each holding their own object for the same
plane, which sec 12.8 measured as about half of every table. So `build()` ends
by printing each entry, hashing the text, and asking the save's owner table
whether an earlier file already writes it. The text is kept and written out
where no one does, so nothing is printed twice and what was offered to later
files cannot drift from what was written.

***One entry of another file may be named once only, and this is not an
optimization.*** Two entries of one file that are written alike are two
objects and have to stay two: a face keys its edges' 2D curves on the surface
object it carries, so one surface reaching two faces makes exactly the lookup
those keys exist to settle ambiguous -- the defect class that produced the HLR
crash in sec 12.7's footnote. It is also what keeps the table positional: the
reader adds what it is handed, and a repeated object lands on the position it
already has and shifts every entry after it. The reader checks that anyway and
inserts a copy if it ever happens, because a file this build did not write can
say anything.

***Measured on `scanner.FCStd`***, 606 objects, forced full recompute, saved
to a directory in ASCII, against the same build with the setting off -- which
is exactly what ships:

| | off | on |
|---|---|---|
| shape files | 318 | 320 |
| files naming another | 21 | **294** |
| geometry entries named | 0 | **8683** of 36018 |
| raw shape bytes | 16225469 | **13791271 (-15.0%)** |
| deflated shape bytes | 3534418 | **2880309 (-18.5%)** |
| whole archive | 4295291 | 3641599 |
| save to directory | 1.24s | 1.36s |
| reopen / full parse | 1.27s / 78.5s | 1.28s / 78.2s |
| faces / missing a curve / invalid | 90686 / 0 / 4 | 90686 / 0 / 4 |

The last row is the gate, and the last column of it is unchanged. **15.0%
against the 15.6% sec 12.8 predicted for the cross-file half**, on a document
whose sub-shape references are worth 2.1%; and it compresses better than it
stores, because what is left after the duplicates are named is less like
itself.

There is no restore penalty, exactly as in sec 12.4 and for the same reason:
the files pulled in transitively are the ones the document reads anyway, and
the parse cache means each is parsed once. The 78s is the per-face pcurve walk
the check does, not the parse.

***Two files more, not fewer.*** Content addressing merges two objects whose
bytes match. Two equal shapes now write different bytes -- the first defines
its geometry, the second names it -- so a pair that used to be one file becomes
two. It cost two files out of 318 here, and it is bounded in practice because
congruent-instance dedup (sec 12.12) recognizes equal parts before either file
is written and shares the file outright. With that setting off, this one gives
a little of its saving back.

***A bug this uncovered, and it was not in the new code.*** A restored
property drops everything that describes its file -- the plan, and the motion
sec 12.12 writes -- inside `setValue`, through `dropBlob`; only the handle was
put back. A stale empty plan reads as *this file borrows nothing*, so a save
that now has nothing to borrow left the file alone **with its references still
in it**, naming a file that save did not write. It was invisible while every
plan was non-empty: it only made a reopened document rewrite every borrowing
file, which looks like nothing at all. `ShapeGeometryCases` has the case that
catches it, and the motion is restored with the same fix -- a shared instance
whose file is kept must write again the motion it was restored with.

***Why it is off by default.*** A file now depends on another file for its
*geometry* and not only for whole sub-shapes, so what one lost file costs is
larger. Nothing dangles -- the plan check rewrites a file whose target this
save does not write, which is the case above -- but the setting is what says
whether a project wants that dependency at all.

### 12.14 Cross-file geometry at assembly scale, and the file it costs

`scanner.FCStd` is a PartDesign model. `MiSTer` is what sec 12.9 called the
assembly-shaped case, and it is where sec 12.8 predicted the most -- 28.1%.
Measured on the build that ships this, both arms saved to a directory in
ASCII, congruent-instance dedup on in both:

| | off | on |
|---|---|---|
| shape files | 5204 | 5205 |
| files naming another | 0 | 2533 |
| geometry entries named | 0 | **69252** |
| raw shape bytes | 85347161 | **71348929 (-16.4%)** |
| deflated shape bytes | 15923269 | **13369001 (-16.0%)** |
| whole archive | 17789824 | 15235582 |
| save to directory | 26.5s | 28.1s |
| reopen / full parse | 1.7s / 310.3s | 1.7s / 310.1s |
| faces / missing a curve / invalid | 1127011 / 0 / 23 | 1127012 / 0 / 23 |

***16.4%, not the 28.1% sec 12.8 predicted, and the difference is not a
disappointment.*** That measurement predates congruent-instance dedup, which
had not yet removed 5668 of this model's 10872 shape files. What sec 12.8
counted as cross-file duplicate geometry was in large part whole parts written
again, and those files no longer exist to hold duplicates. The two savings
overlap; they do not add.

***The content rule, and what it is worth.*** The first cut of this produced
**6841 files** against the 5204 the setting-off arm writes -- because content
addressing merges two objects whose bytes match, and two equal parts stop
having matching bytes as soon as the first defines its geometry and the second
names it. A file that writes its geometry out in full now says so, keyed on
that geometry as it would be written, and a later file that would write the
same thing writes it out in full too. That took 6841 back to 5205 and took the
raw bytes down with it, from 72040089 to 71348929: the pair costs one file
where it used to cost two, which is worth more than the entries the second one
would have named. On `scanner.FCStd` the rule changes nothing at all -- 318
files to 320 either way, byte for byte -- because a PartDesign model's equal
parts are already one file before the geometry is looked at. This is an
assembly's problem.

***The one file it still costs.*** A file that itself names another's entries
cannot offer its content, because it is not the bytes a later file writing in
full would produce. On `MiSTer` exactly one pair falls in that hole:
`Shell892` names one file, its twin `Shell6303` names two, so the two differ
and the store keeps both where it used to keep one. Closing it means a later
file reproducing an earlier one's references rather than deciding its own,
which is a larger change than the file it saves.

***What the reopen says, object by object.*** The totals differ by one face,
and a total that moves is exactly what sec 12.10's lesson says not to accept.
Dumped per object in both arms -- 18085 shapes, faces, edges and validity --
the two agree everywhere except the top-level compound, which has one more face
and two more edges with sharing on. That is the extra file: `Shell6303` is a
one-face shell, and where the two arms share a file the compound sees one
TShape instead of two. No shape is null, none throws, and the invalid count is
23 in both arms, as it is with the setting off.

! **Under an address-space cap, running out looks like a crash.** The first of
these runs was capped at 12GB and died with SIGSEGV in the middle of the
reopen, which reads as a defect in the format being tested. The same run at
24GB completes. Materializing every shape of this model costs about 25GB, so
the cap has to allow for that -- and a crash under a cap should be reproduced
without one before it is believed.
