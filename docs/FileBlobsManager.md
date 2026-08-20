# Included-file blobs — reference-counted document file storage

Status: design agreed, landing in stages. Phases 1/1b/schema-gate are committed
(`959f96893c`, `950dca371e`, `6a0a2ad81a`, `c7aad34c13`); the archive rework
described in §5–§7 replaces the archive layout that `c7aad34c13` introduced.

This document covers `App::PropertyFileIncluded` — the property that embeds a
file (a texture, a TechDraw template, an imported material, an environment
image) into a `.FCStd`. It describes the storage model, the archive format, the
save/restore protocol, and the invariants a change here must not break.

Related: `docs/ComputeBoundaries.md` (per-document process isolation, which is
why ownership is per document), `docs/RoadMap.md` (the browser tier, which is
why content addressing matters).

---

## 1. What was wrong

Before this work, each `PropertyFileIncluded` owned a private copy of its file
in the document's transient directory, and `~PropertyFileIncluded` deleted that
file unconditionally. Everything else followed from that one line:

- **No sharing.** Two properties holding the same 50 MB image cost 100 MB on
  disk and two archive entries, because a shared file would be deleted twice.
- **Copy meant copying bytes.** `Property::Copy()` — which the undo stack calls
  on every touch — duplicated the whole file. Toggling a texture flag on an
  object with a large embedded image wrote 50 MB to disk per undo step.
- **The `Status::User2` rename trick.** Because a copy could not share the
  file, undo had to rename files around behind the property's back to keep
  lifetimes straight. It existed only to paper over the missing refcount.
- **Identity was provenance, not content.** `isSame()` compared the original
  path, which is runtime-only state and empty after a restore.

## 2. The model

Three types in `src/App/FileBlobManager.{h,cpp}`:

| Type | Role |
| --- | --- |
| `FileBlob` | One immutable file on disk, identified by the hash of its bytes |
| `FileBlobHandle` | `shared_ptr<FileBlob>` — a reference; the count *is* the refcount |
| `FileBlobManager` | Per-`App::Document` store, owns the mapping hash → blob |

**A blob is pure content.** Its identity is the SHA-1 of its bytes and nothing
else; it lives at `<transient>/blobs/<uuid>.<ext>`, under a name nothing outside
the store may depend on -- the extension is kept only because the path is handed
to whatever consumes the content, and a file with no extension is a file some
viewers refuse. Blobs are immutable -- changing a property's file always produces
a *new* blob. What a blob is called when a document is **saved** is a separate
question with a separate answer, sec 13.

**Names live on the property**, not the blob: `_BaseFileName` (the name it
saves under) and `_OriginalName` (where it came from), both persisted. Any
number of properties share one stored file while each keeps its own naming.
Two sites used to read a name off the storage path and now ask the property
instead: `Drawing::FeaturePage` (template-by-name fallback) and
`Gui::DlgEditFileIncludePropertyExternal` (names the copy handed to an external
editor). Every other consumer passes the path to a loader that sniffs format
from content, so a uuid-named file is fine.

**Ownership is per document, not per application.** A document knows all its own
referrers, so its counts need no cross-process locking — which matters because
documents are meant to move into separate processes (`docs/ComputeBoundaries.md`).
Cross-document de-duplication is a separate, best-effort tier (§10), and losing
it costs disk, never data. Hard links were tried and rejected: not portable.

Consequences already landed: `~PropertyFileIncluded` is `= default`, `Copy()` is
a refcount increment costing zero I/O, and the `Status::User2` trick is deleted.

## 3. Document format and the schema gate

`Document::SaveSchemaVersion` (property, group "Format") selects the version a
document is written as. Writable versions are an **explicit list**
(`Document::getWritableSchemaVersions()`, currently `{4, 5}`), not a range — a
version is writable only where the writer can still produce that exact shape.

- **Schema ≤ 4** — the historical form. Each property writes its own archive
  entry and `<FileIncluded file="..."/>`. Content is duplicated per property.
- **Schema ≥ 5** — the blob form. The manager writes one entry per distinct
  content and the property writes only
  `<FileIncluded hash=".." name=".." original=".."/>`.

WARNING: **Schema 5 is this fork's format, not a compatible middle rung** (settled
2026-08-16). Nothing but this fork knows the `hash` attribute: upstream's
`PropertyFileIncluded::Restore` has a `file=` branch and a `data=` branch and
nothing else, so an unaware reader opens the document, finds neither, and
leaves every embedded file **empty without a word** -- the `blobs/` entries sit
unclaimed beside it. That is exactly the silent-misread failure the compact
format was designed to make impossible, which is why the compact format (built
as schema 6, and no more released than this was) was folded into 5 rather than
stacked above it. **4 is upstream's format; 5 is this fork's, and the
default for a document created here (2026-08-20) -- a restored document
keeps the format its file was written in**, and a document written at 5 is rooted `<FCDocument>`, so a reader
that does not know the format refuses it outright instead of half-reading it.
The blob table is unchanged by the merge -- the gate still reads `>= 5`; it now
means "the fork format" rather than claiming to mean something weaker.

### 3.1 What the user is asked, and when

The format is chosen in the save dialog (`Gui::Document::saveAs`), and since
2026-08-20 a **new document is compact**: `SaveSchemaVersion` defaults to 5 and
the dialog opens on it (`PreferCompactFormat`, also true by default, can hold a
new document back to standard but never raises a cap something lowered on
purpose). A document restored from a file keeps that file's own format, so this
is about documents created here, not documents opened here. Two message boxes state what the
choice costs, both raised after the file name is in so that neither is a
heading nobody reads:

| prompt | fires when | offers |
|---|---|---|
| `confirmCompactFormat()` | the save resolves to schema 5 | Save compact / Use standard format / Cancel, plus **Do not warn again** (`WarnCompactFormat`, default true) |
| `confirmSchemaUpgrade()` | the save resolves to schema 4 **and** the document holds content only the store carries | Use compact format / Save anyway / Cancel, plus do-not-ask-again for that document (session only) |

The second one also runs on the **plain Save** path, which is the case that
made it necessary: plain Save never touches `SaveSchemaVersion`, so a document
saved once at 4 and given a texture afterwards would drop the image on every
save without a word.

What "content only the store carries" means is answered by the property, not
by the caller: `App::BlobReferrerProperty::blobContentNeedsStore()` is **false
by default**, because most referrers have a schema 4 spelling that keeps the
data -- `PropertyFileIncluded` writes its own copy of the file, a shape
property writes the shape the old way and forfeits sharing rather than
content. `PropertyMaterialList` overrides it with `hasTexture()`: the maps
themselves ride a companion element below 5 (`docs/ShapeAppearanceDesign.md`
10.5), the bytes behind them live in the store and nowhere else. A new
referrer with the same problem overrides the same hook and both prompts pick
it up; `Gui::Document`'s scan walks the document, its objects and their view
providers and asks nothing else.

The gate is `Base::Writer::getSchemaVersion()`, which only the document-level
save paths set. Anything serializing a property standalone (§9) leaves it unset
and keeps the self-contained form, which still round-trips.

## 4. Postmortem: why the first schema-5 layout was wrong

`c7aad34c13` put blob entries through the normal `Writer::addFile` /
`writeFiles()` channel and registered them all at the end of the save, after
`signalSaveDocument`. That is unsound, and it silently dropped data.

`Base::ZipReader::readFiles` (`src/Base/Reader.cpp:760`) is a **forward-only
merge scan**: it walks zip entries in order, advancing a cursor through the
list of registered consumers, and never goes back. Its contract is that *zip
order equals read-side registration order*. But:

- **write**: all hashes appended last — after every `.brp`, after `GuiDocument.xml`
- **read**: each hash registered inline as `Document.xml` is parsed, interleaved
  with the `.brp` entries (`PropertyFile.cpp:382`)

Those orders disagree whenever any other file entry follows the blob-owning
property, so the cursor runs past the blob entries and they are never read.
Minimal reproduction — one file property, then a `Part::Box`:

```
ZIP ORDER: ['Document.xml', 'Box.Shape.brp', '<hash>']
RESTORED PATH: ''          # content silently lost
```

The earlier tests passed only because their layouts happened to put nothing
after the blob property. **Any fix that keeps blobs in the `FileList` channel
inherits this ordering contract.** So the design below takes them out of it.

## 5. Archive layout

Blobs do not use the `FileList` / `writeFiles()` channel at all. The manager
writes them itself, with its own `putNextEntry`, **immediately after
`Document.xml`** and before anything else is registered:

```
Document.xml            <- object data; properties carry hashes only
blobs/Content.xml       <- the index: name, hash, referrers (sec 13)
blobs/Box.Image.png     <- one entry per distinct content, named after its referrer
blobs/Cyl.Image.png
<object>.Shape.brp      <- the ordinary FileList entries, unchanged
GuiDocument.xml
<object>.GuiDocument.xml
thumbnails/Thumbnail.png
```

Two properties (in either tier) referring to the same content produce **one**
entry. The ordinary entries keep exactly the order and semantics they had
before schema 5, so §4's contract is satisfied trivially: blobs are no longer
part of it.

Directory mode (`FileWriter` / `FileReader`, used by autosave recovery and by
`Document.xml`-in-a-folder projects) is the same layout as plain files under
`blobs/`, where ordering has no meaning at all.

This is one of two places the table can go; §6.1 covers the other, a base64
`<Blobs>` element inside `Document.xml` for a save that was asked for pure XML.
The table is the same either way, and so is everything that refers to it.

## 6. Save protocol

Three call sites drive a document-level save, and all three get the same
wiring, because all three have an archive *and* a document-scoped manager:

| Site | Anchor |
| --- | --- |
| `App::Document::save` | after `Document::Save(writer)`, before `signalSaveDocument` |
| `Gui::AutoSaver::saveDocument` | after `doc->Save(writer)`, before `doc->signalSaveDocument` |
| `App::Document::exportObjects` | after `writeObjects(obj, writer)`, before `signalExportObjects` |

Sequence:

1. `manager.beginSave()` — clear the collected set.
2. **Collect broadcast.** Walk every reachable property that owns blob
   content -- anything implementing `App::BlobReferrerProperty`, which the walk
   asks via `collectBlobs()` rather than testing for a concrete class -- and
   `noteReferenced()` it: App objects and the document, plus a Gui leg
   (`signalCollectFiles`) covering view providers **and** `MDIView`s, which is
   what puts a view-only blob such as the environment image into the save set.
   The broadcast is **scoped**: a full document for `save`/autosave, the
   exported subset only for `exportObjects`, so a clipboard buffer never carries
   blobs belonging to unrelated objects.
3. Stream `Document.xml`. Properties write hash/name/original; they register
   nothing.
4. `manager.writeBlobs(writer)` -- `blobs/Content.xml`, then one
   `putNextEntry("blobs/<name>")` plus content per collected blob, in name
   order so a given document always produces the same archive. The name comes
   from the referrers collected in step 2; sec 13 is the whole of it. It is a
   no-op below schema 5, where the properties carry self-contained copies of
   their own and these entries would be weight no reader ever asks for.
5. Everything else as before (`signalSaveDocument`, `writeFiles`).

Discovery is by traversal, not by liveness: blobs referenced *only* by an undo
transaction or the clipboard are live in the manager but not reachable from a
property, so they are correctly not written.

In directory mode the manager skips a blob the previous index recorded under
the same name with the same hash, the file still being there. That is what
makes autosave cheap: before any of this, every autosave tick rewrote every
embedded file, mirroring what `RecoveryWriter::shouldWrite` already does for
ordinary property files. Existence alone was proof while the name *was* the
content, and is proof of nothing now (sec 13.5).

## 6.1 The document's save options

`ForceXML`, `SplitXML` and `PreferBinary` are document properties that steer the
writer. Only `PreferBinary` reaches a packed project; the other two are applied
by `Document::save` on the directory path alone, which is what their property
descriptions have always said.

| Option | Effect on the blob path |
| --- | --- |
| `ForceXML` > 3 | The table moves *into* `Document.xml` as base64, ahead of the object data. No archive entries. |
| `ForceXML` <= 3 | The table is archive entries, as in §5. Level 3 is the default. |
| `SplitXML` | Nothing. Per-object XML files are written *after* the blobs, and the up-front collect pass is what covers them. |
| `PreferBinary` | Nothing. Stored content is opaque bytes; the option chooses a format for generated data, and a file has no format to choose. |

**Only the place changes, never the sharing.** A demand for pure XML is a
demand about *where* content is written, not a reason to give up one copy per
distinct content. So the manager writes the same table in a different place:

```xml
<Document SchemaVersion="5" ... StringHasher="1" Blobs="1">
  <Blobs Count="2">
    <Blob hash="<sha1>" size="4096">…base64…</Blob>
    <Blob hash="<sha1>" size="8192">…base64…</Blob>
  </Blobs>
  <StringHasher .../>
  <Properties …>   <!-- everything that can refer to a hash comes after -->
```

The element sits at the head of the document element, ahead of the document's
own properties, the objects, and the view tier in `GuiDocument.xml` — so §4's
ordering question never arises here either. Its presence is announced by a
`Blobs` attribute on `<Document>`, the same way `StringHasher` is, because the
reader has to know whether to read past it. Restore stores each blob under the
hash of what actually arrived and holds it, and the referrers that follow
resolve immediately through the ordinary pending/dispatch path (§7).

**Referrers do not care.** `PropertyFileIncluded::Save` writes a hash whenever
the schema is 5 or later and never asks how the content travels. That is what
lets a `View3D` property work unchanged: it serializes through a `StringWriter`
that sets `ForceXML` to 4 itself (§8), and its content is in the enclosing
save's table either way. `beginSave(writer)` picks the format once —
`None` below schema 5, `InlineXml` above `ForceXML` 3, `Entries` otherwise —
and `writeBlobs()` / `writeInlineBlobs()` each do nothing unless it is theirs.

Two things had to be repaired before the inline form worked at all, both older
than this store and both reachable from anything else that writes one:

- `Base::base64_encoder::write()` reported only the bytes it had encoded, not
  the ones it had taken, so boost re-sent the tail it was holding and the filter
  encoded it twice. Content whose length was not a multiple of three came out
  corrupt — one byte too many at `% 3 == 1`, badly mangled at `% 3 == 2`. It now
  reports everything it consumed. `tests/src/Base/Base64Filter.cpp` checks the
  filter against `base64_encode` for every length up to 200.
- `Base::FileWriter::putNextEntry()` opened the new entry's stream without
  closing the previous one, which silently fails and leaves everything written
  to it going nowhere. `writeFiles()` closed between entries itself, so nothing
  hit this until the manager wrote two entries in a row: every directory save —
  and every uncompressed autosave recovery — dropped its blob content and
  reloaded with empty properties. It now closes first.

**Directory mode** has one more wrinkle of its own. An unpacked project is read
before `Document.xml` restores the `Uid`, and the `Uid` is what names the
transient directory — so the store is filled under one directory and the
document then renames it. The content moves with the directory and keeps its
hash; only the recorded paths go stale, which `relocate()` repairs from the
rename in `Document::onChanged`. The packed path never sees this: there the
entries are read during `readFiles()`, long after the `Uid` has settled.

## 7. Restore protocol

The manager restores content and *hands it to* the properties; properties never
poll and never bind lazily.

1. `Document.xml` is parsed. Each `PropertyFileIncluded::Restore()` reads its
   hash/name/original and calls `manager.addPendingReferrer(hash, this)`.
2. `reader.readFiles()` starts. Before the merge scan, the manager **drains the
   leading `blobs/…` entries** — their names are already in hand from
   `getNextEntry()`, so no zipios change is needed — and stores each under its
   hash.
3. The manager **dispatches**: every pending referrer is handed its handle.
   Nothing is left unresolved.
4. The merge scan runs over the remaining entries exactly as before. Everything
   parsed from here on — `GuiDocument.xml`, view-provider properties, per-object
   Gui files — finds its blob already in the store and acquires it directly in
   its own `Restore()`.

**The temp hold.** The manager keeps a strong reference to everything it
restores, so content cannot die between arriving and being claimed. It is
released once no further referrer can appear:

| Path | Release point |
| --- | --- |
| Document open | after `signalFinishRestoreDocument` returns, in `Document::afterRestore()` |
| Object import / paste | when the import completes (`afterRestore(objArray)` overload) |

The document-open point is chosen because `Gui::Document::slotFinishRestoreDocument`
is what replays the embedded View3D strings (§8) — the last moment a property
can appear. On the partial-reload path `afterRestore` returns *before* that
signal, so the hold survives into the reload.

Anything still held at release is a blob no property claimed — in practice an
object whose module was missing, so it was skipped on load. It is released, and
its file deleted: re-saving such a document already drops that object's data.

**Lifetime rule.** A pending referrer can be destroyed before dispatch (object
creation fails, partial load skips it), so `~PropertyFileIncluded` deregisters
itself from the pending list.

**The release point is a deadline, and a lazily restoring tier has to answer to
it** (added 2026-08-17, after this bit data). Nothing dispatches after
`endRestore()`, so a property that first reads its `hash=` later gets no
content, writes `hash=""` on the next save, and the content leaves the file for
good. That is what progressive load did to a view provider: `Gui::Document`
parks each `<ViewProvider>` element verbatim and drains it in slices *after* the
open, so a `Render_*` included file on a view provider was lost on every open of
a schema-5 document.

The guard for it already existed and was one attribute short. Parking is refused
for a record naming an archive entry (` file="`), because the forward walk
consumes those during the load; it is now refused for one naming a blob
(` hash="`) as well, for the same reason at the same boundary -- such a view
provider is restored inside the load by `restoreCapturedViewProvider()`, exactly
as the eager path would have. The cost is that a view provider with an embedded
file does not get parked, which is a rounding error: they are textures and
environment images, not the thousands of default records parking exists for.

`addPendingReferrer()` now **logs** a referrer arriving after the release point
instead of queueing it onto a list nobody will read again. The bug above was
invisible for exactly that reason, and it is what any future instance of this
shape will run into first.

Audited at the same time, and clear: deferred *archive entries*
(`Document::serveDeferredFiles`, which outlive the view-provider drain) cannot
carry a file property at schema 5, because such a property registers no entry;
the `View3DInventor` string replay (sec 8) runs from `signalFinishRestoreDocument`,
i.e. inside the hold; and `PropertyShapeStore` is the only subclass, restored
with the document. The parked view-provider record was the one tier past the
deadline.

## 8. The view tier

View-side properties reach the same per-document manager, with two wrinkles:

- **`View3DInventor` properties are serialized into a string**, embedded as a
  character stream inside `GuiDocument.xml` (`Gui/Document.cpp:1930`) and
  replayed from memory at finish-restore. `StringWriter::writeFiles()` throws on
  any file entry and the replay happens after the archive is consumed, so these
  properties can never register an entry themselves. They do not need to: the
  save-time collect broadcast covers `MDIView`s, and at replay time the content
  is already in the store, so `Restore()` acquires it immediately. The nested
  `StringWriter` is given the outer writer's schema version so the property
  emits the hash form; without it, the property falls back to inline base64.
- **Store resolution already works**: `Gui::BaseView` derives from
  `App::PropertyContainer` and overrides `getOwnerDocument()` (`Gui/View.h:87`),
  so `PropertyFileIncluded::blobManager()` resolves a View3D property to the
  owning document's store, not the process-wide fallback.

Ordinary `ViewProviderDocumentObject` properties (e.g. the texture images added
by `TaskRenderSettings`) need nothing special: they already resolve to the App
document, and they are parsed from `GuiDocument.xml`, i.e. after the blobs land.

## 9. What does not participate

Single-property serialization with no archive and no document — the static
`StringWriter` in `Property::isSameContent()` (`Property.cpp:402`) and
`Base::PersistencePyImp`. There is nothing to hand a blob to, so these keep the
self-contained inline form. This is a structural limit, not a compatibility
choice.

`PropertyFileIncluded` does not reach either of them. `isSame()` is overridden
(`PropertyFile.cpp:471`) and costs no I/O: names, then blob pointer identity —
the common case, since `Copy()` shares the handle — then the cached content
hashes. `isSameContent()` is a non-virtual opt-in used only by
`PropertyConstraintList` and `PropertyTrajectory`. Content-based identity is
also what makes the recompute-skip correct across a restore: the old `isSame()`
compared `_OriginalName`, which is runtime-only and empty after loading, so a
restored property compared unequal to itself.

## 10. Invariants

1. A blob's file is deleted exactly when its last handle goes away, never
   earlier and never twice. `release()` must tolerate a *replacement* blob
   already owning the path (a `weak_ptr` reports expired before `~FileBlob`
   runs).
2. `~FileBlobManager` must detach all blobs (`_owner = nullptr`) before its
   members die, or a blob destroyed during teardown calls `release()` on a
   half-destroyed manager. This crashed once.
2b. **No handle may be destroyed while `_mutex` is held.** `~FileBlob` calls
   `release()`, which takes that same non-recursive mutex, so dropping the last
   reference under the lock deadlocks the thread against itself — a single
   thread parked in `futex_do_wait`. This is not hypothetical: `beginSave()`
   clearing `_saveSet` did exactly that whenever a property had replaced its
   content since the previous save, and it hung every second save of such a
   document. Containers that may hold the last reference are swapped into a
   local declared *before* the lock guard, so they die after it is released.
3. Restored content always has an owner: the temp hold covers the window
   between the archive read and the last possible referrer.
4. Blobs are immutable; the stored file is read-only.
5. `saveAs` changes `TransientDir`, so every stored path goes stale and must be
   repaired (`repath`) in blob layout, not the old flat one. The repair belongs
   to the **manager**, immediately before it writes: the property-side repair
   in `PropertyFileIncluded::Save()` only runs in time for App-tier properties,
   since the view tier is written after the content is.
6. One archive entry per distinct content, ever.

## 11. Test coverage

`src/Mod/Test/FileBlobs.py` (headless, `FreeCADCmd -t FileBlobs`) covers
storage/dedup, refcount lifetime, undo/redo, persistence round-trips including
the §4 ordering regression, archive shape, schema-4 fallback, `saveAs`, the
save options of sec 6.1 over both writers, the naming and pruning of sec 13
(`BlobNamingCases`: a name across close/reopen/edit, changed content actually
reaching disk, the naming referrer moving when its object goes, orphans pruned
and strays left alone, an unmodified re-save leaving the blob layer
byte-identical, and a pre-index directory project still opening), and the
export/import path via `copyObject`. `scripts/file-blob-verify.sh` adds the GUI
legs that need a `View3DInventor`: the embedded environment image round-trip and
the assertion that it saves as a hash rather than base64. See those files for
the case-by-case matrix.

## 12. Future work

The save options are done (§6.1) and so is the browser-tier fetch (below). The
rest is staged -- worth doing, not scheduled.

- ~~**Any file save through the manager, not just included files.**~~ Done.
  The store was already type-agnostic (`insertFile(path) -> handle`, hash
  identity, refcounted lifetime) and the pending queue was already keyed on
  `App::BlobReferrerProperty`; the collect pass was the last place holding a
  concrete `PropertyFileIncluded` cast, so it now asks that same interface
  (`collectBlobs()`) instead. An owner notes its own content, which is what
  lets it choose the extension and the referrer name -- names stay on the
  consumer, as `_BaseFileName` does today, so one stored file can serve
  referrers that each call it something different. `PropertyPartShape`
  implements it as nothing on purpose: it notes at write time, when the
  writer's mode (`BinaryBrep` or ASCII, which hash differently) is finally
  known. The first non-file owner is the material card,
  `docs/MaterialStorage.md` sec 8.
- **Cross-document dedup tier.** An application-level, content-addressed,
  append-only cache in its own directory (never inside a document's transient
  dir, which is wiped on close), referenced by copy where linking is
  unavailable. Best-effort by construction.
- ~~**Lazy blob fetch in the browser tier.**~~ **Done**, for the renderer's
  streamed textures: a snapshot names them by content key and the viewer
  fetches each once from `GET /blob?key=`, caching it in memory and in
  IndexedDB (`docs/RenderEngine.md` §2, "Out-of-band texture payloads").
  That tier addresses *rendered* content, which reaches the viewer through
  `SceneDump` rather than through the document archive; wiring the document's
  own blobs to the same fetch belongs with the thin client
  (`docs/ThinClient.md`), which is what would carry them.
- **RPC-shaped manager API** (`insert(path|bytes)→key`, `acquire(key)→handle`,
  `open(key)→stream`) for the out-of-process document goal; `getValue()→path`
  stays as the legacy accessor.

## 13. Stable names and the content index

Status: **built** (2026-08-17). This section supersedes the first bullet of
sec 12 and changes the archive layout of sec 5. It is step 1 of the build order
in `docs/SharedShapeStorage.md` sec 12, which sequences it against the shape
work that depends on it. Sec 13.8 records the three places the built form departs
from the design and why.

### 13.1 What is wrong with content-addressed entry names

Sec 2 makes a blob pure content, stored and written out under the SHA-1 of its
bytes. For a packed `.FCStd` that is invisible. For a project saved as a
directory -- which exists to be friendly to version control, and is why
`PreferBinary` defaults to false -- it is three separate problems:

- Every content edit is a **delete plus add of an opaque hex name**, never a
  modification, so no tool can follow a file across an edit.
- **Nothing prunes.** `writeBlobs()` only ever writes, so a project directory
  accumulates orphaned hex files forever and `git add -A` commits them.
- The incremental skip is `exists(dir + blob->hash())`, which is sound **only**
  because the name is the content.

### 13.2 Layout

The same shape in both places, and `blobs/` keeps its name:

```
<transient>/blobs/Content.xml        <- the index, inside blobs/
<transient>/blobs/<uuid>.<ext>       <- transient names are uuids
<project>/blobs/Content.xml
<project>/blobs/Box.Shape.brp        <- saved names are derived from the referrer
```

There are no per-property-type subdirectories.

Transient names are uuids because at `insertFile()` time there may be no
referrer at all -- a blob held only by an undo transaction or the clipboard --
and because stored files are read-only and held by absolute path, so renaming
them mid-session is churn for no gain. The extension is kept so a transient
file can still be opened without guessing. The transient `Content.xml` is a
diagnostic ledger, not load-bearing.

Saved names are derived, because that is what a human and a diff read, and
because a directory save already writes `Box.Shape.brp` today
(`Property::getFileName()` through `writer.addFile`). The two name spaces are
already decoupled: `writeBlobs()` streams content into an entry, so an entry
name never had to equal the file name.

### 13.3 The content file

```xml
<?xml version='1.0' encoding='utf-8'?>
<FileStore v="1">
  <F n="Box.Image.png" h="<sha1>" r="4213:Box.Image 4890:Cyl.Image"/>
  <F n="Cyl.Shape.brp" h="<sha1>" r="4300:Cyl.Shape"/>
</FileStore>
```

Three attributes, sorted by name, one element per line, so a content edit is a
one-line diff and two branches touching different parts merge textually. `n` is
the name inside `blobs/`, `h` the content hash, `r` the referrers as
`<objectId>:<Object>.<Property>` tokens -- safe unquoted, since both are
internal names.

Deliberately absent: the size, which the file itself knows, and the original
path and base file name, which the **property already persists** in
`Document.xml`. The index must not duplicate what the document holds; its job
is the one thing nothing else records, the name-to-content binding and who uses
it.

XML rather than JSON or a tabular format so that `encodeAttribute` handles
arbitrary user file names and `Base::XMLReader` parses it with no new
dependency.

### 13.4 Naming

The derived name is `Property::getFileName()` -- `Object.Property` -- plus the
extension. An object's internal `Name` is immutable (renaming in the tree
changes `Label`), so a derived name never churns while the object lives.

**Both halves of that stem are named by a person, so it is made portable before
it becomes a file** (`Base::Tools::portableFileName`, added 2026-08-18). An
object's name only has to be a Python identifier, which admits any length and
every stem Windows still reads as a device; a property's name admits the same.
A directory project writes the stem as a real file, so a name the file system
refuses loses the geometry with nothing but a line in the report view -- and
that is what it did: of three objects named 250 `x`, `CON`, and 90 CJK
characters, two came back with a null shape and no properties. The transform
replaces control, reserved and non-ASCII bytes with `_`, prefixes a reserved
device name, and over a 120-byte budget truncates and appends a digest of the
original so two long names stay distinct. The extension survives truncation, in
up to two parts, because what says what a file is here is often two (`.Gui.xml`
is dispatched on the whole of it). **A name that was already legal is returned
unchanged**, so no existing project's files move.

**A name belongs to its naming referrer**, defined as the live referrer with
the lowest object id, not to the referrer set as a whole. If that referrer goes
away the name is re-derived from the new lowest, which renames the file in the
same save -- git sees a pure rename, content unchanged.

That rule exists to close a specific hole. Internal names are reused after
deletion: `App::DocumentObject::_Id` is not (`DocumentP::addObject` only ever
increases `lastObjectId`, `removeObject` never lowers it, and restore re-mints
each object at its stored `id=`), but the name is. So a rule that kept a name
while *any* referrer survived would let a blob shared by `Box` and `Cyl` keep
the name `Box.Image.png` after `Box` is deleted, and a newly created `Box`
would then find its rightful name squatted on. With the rule above the two
generations can never be live in the same save, so the writer can assert name
uniqueness across the save set rather than resolve a conflict.

Shared content still collapses to **one** file with several referrers, so `r`
is a list. The generation token in `r` is what keeps the diff honest when a
path is reused across generations: the line goes from `4213:Box.Image` to
`4890:Box.Image`.

Residual collisions are rare by construction -- an App property and a
view-provider property of the same name on the same object need a tier marker
in the derived name, and referrers with no object fall back to a uuid. What is
left gets `-1` before the extension, **pinned in the content file**, because
`Writer::getUniqueFileName()` numbers by scan order and its suffix can
otherwise migrate between saves.

### 13.5 The skip, and pruning

***The incremental skip must change in the same commit as the naming.*** With
stable names, a file existing no longer implies its content matches. The skip
becomes: the previous content file names this file, its recorded hash equals
the blob's hash, and the file exists. Nothing else is proof.

A directory save reads the **previous** `blobs/Content.xml` before writing, and
unlinks names that it lists and the new one does not. Nothing outside the
previous index is ever touched, so a stray file a user put in the tree
survives.

Note what pruning is and is not for: git compares content, not timestamps, so
rewriting identical bytes was never visible in `git status`. The skip is a
save-time I/O win; the diff win comes from the stable names and from removing
orphans.

### 13.6 What does not change

**Identity stays the hash in `Document.xml`.** The content file is read by the
same archive handler, ahead of the blob entries, and only re-establishes the
name table so the *next* save is stable. If it is missing -- an older file, a
foreign file -- everything still restores and names are assigned fresh on the
next save, at the cost of a one-time rename.

Having the property reference the name instead, so that a content edit leaves
`Document.xml` untouched, was considered and deferred: it buys one line of XML
churn and makes the index load-bearing for restore.

### 13.7 Shape files, and what the index has to carry for them

**Built 2026-08-17** (`docs/SharedShapeStorage.md` sec 12.3). Shape properties
are referrers like any other, named `Box.Shape.brp`, with the skip and the
prune applying to them unchanged. The pending queue is keyed on
`App::BlobReferrerProperty` rather than on `PropertyFileIncluded`, which is all
the two have in common. Two consequences were predicted; the first did not
survive contact:

- ~~**Deferred read by name stops being optional.**~~ The premise was that
  shapes dominate the entry count, so draining them at open would be ruinous.
  Content addressing removes it: on `MiSTer_imported.FCStd` 17800 shapes became
  10873 files, and the walk over them is *cheaper* than the old one over 68235
  registered entries -- the open went from 4.06s to 2.37s. Entries are still
  drained at open. Making them lazy is an optimization, not a prerequisite.
- **Blobs reference other blobs.** A shape file that shares geometry with
  another object writes a reference to that object's *file* rather than a copy
  of the geometry (`docs/SharedShapeStorage.md` sec 11.5). That is still full
  referring -- the referrer holds a handle on the whole target blob and names a
  sub-shape inside it -- so lifetime is ordinary refcounting and none of the
  rules here change. What the index gains is **blob-to-blob edges**: alongside
  the property referrers in `r`, a `<F>` records which other files it reads.

  They pay for themselves twice. Pruning becomes correct in the presence of a
  file that no property names directly but another file needs. And they answer
  the one question the save cannot answer from memory: which *parked* shapes
  must be loaded because a file they reference is being rewritten. Everything
  else about ownership is recomputed from scratch on every save, which is what
  keeps the scheme free of carried-forward state.

***The bug routing shapes through here uncovered.*** `readBlobEntry()` copied
an archive entry with `entry >> to.rdbuf()`. That is a *formatted* extraction:
its sentry skips leading whitespace, so content beginning with any arrived
short. It had gone unnoticed because no included file the tests carried started
with whitespace -- and ASCII BRep starts with a newline. Content addressing is
what made it loud instead of quiet: the stored blob hashed to something other
than what `Document.xml` referred to, so the referrer was served nothing at all
rather than handed a corrupted file. `PropertyFileIncluded::RestoreDocFile`
carried the same line. Both now write the stream (`to << entry.rdbuf()`).

### 13.8 As built: three decisions the design did not settle

**The previous index is read from the target directory, not remembered.**
`writeBlobs()` reads `blobs/Content.xml` out of the directory it is about to
write. Remembering the index this document last wrote is unsound across a
save-as: the skip would then take a file the *other* directory's index vouched
for as proof about a same-named file here, and quietly keep content belonging to
whatever was there before. Reading the target also means nothing is carried
between saves, which is the property sec 11.7 of `docs/SharedShapeStorage.md`
wants of ownership generally. An archive has no previous state to read and so
rewrites every entry, which costs nothing it did not cost before.

**A restore does not parse the index.** It is claimed with the content --
`readBlobEntry()` returns early for it, so no other consumer is offered it --
and then dropped. Identity is the hash in `Document.xml`, the extension a
transient file needs comes off the entry name, and the names the next save has
to know are read from the directory it writes. Parsing it at restore would buy
one thing only: names known before content is touched, which is what step 3's
deferred read by name needs. That belongs to step 3. There is also no transient
`blobs/Content.xml`; the design called it diagnostic and not load-bearing, and
an unwritten diagnostic is one fewer file every consumer has to skip.

**One thing outside the previous index is pruned: 40-hex names.** Those are
what a save wrote before this section existed. They are ours by construction --
`blobs/` is written by nothing else -- and once the names have moved nothing
else can identify them, so an upgraded project would otherwise keep every
orphan it ever accumulated and `git add -A` would commit them. A file with any
other name that no index ever listed is left alone, which is the rule as
designed.

Two further notes on what the built form does *not* exercise:

- **Collision suffixes are unreachable today.** Two derived names can only
  collide if two referrers produce the same `Object.Property` -- and property
  names are unique per container, the two tiers are separated by the
  `.ViewObject` marker `getFullName()` already spells, and referrers that
  cannot be named fall back to a 40-hex hash that no derived name can equal.
  The `-N` numbering and its pinning are built and are what sec 13.4 describes,
  but the gate cannot reach them from the public API; the case they exist for
  is the one step 3 and the property tiers after it may reintroduce.
- **`Property::getFileName()` is now public.** The blob manager derives a
  referrer's name with it rather than reimplementing the spelling, which is
  what guarantees step 3's shape files keep the names `PropertyPartShape`
  already gives them instead of being renamed wholesale on the first save.
