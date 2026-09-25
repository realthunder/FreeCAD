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

## 14. Restored content is served from a copy of the archive (2026-09-14)

### 14.1 What was wrong

A restore gave every blob entry a file of its own in the transient directory.
On Linux that walk is free (2.37 s for MiSTer, `docs/SharedShapeStorage.md`
sec 12.3). On a company-managed Windows laptop whose filesystem is monitored
it was the whole open: `MiSTer.FCStd` (17800 objects, 5401 `blobs/` entries,
87.3 MB inflated) took **409 s** headless, of which 407 s was `readFiles()`.
Each entry cost a staging-file create, a read back to hash it, a rename and two
permission changes -- about 61 ms of filesystem round trips.

`be256877c1` hashed the entry in memory and wrote it once, straight to its
place, which took the open to 54 s. It could not take it further: all 5401
entries are distinct content, so every one still needed a file create, and a
file create is exactly what this filesystem charges for.

### 14.2 Measured before building

The same 5401 entries, in FreeCAD's real transient root on the laptop
(`getUserCachePath()`), one pass each (`archbench.py`):

| backing | create | read all, random order |
| --- | --- | --- |
| copy the `.FCStd` as it is, inflate on read | 0.05 s | 0.26 s |
| repack as a stored (uncompressed) zip | 0.97 s | 0.07 s |
| raw pack file, own index | 0.33 s | 0.06 s |
| one file per entry (what the store did) | 23.23 s | 11.98 s |

Copying the archive unchanged wins outright: nothing to write but one
sequential file, and inflating costs about 0.05 ms an entry, which a BRep parse
dwarfs. The stored and raw variants read faster but pay for it on every open,
and add a second format for nothing.

### 14.3 The design

- **`restoreFromArchive()`** copies the document archive into the store
  directory (`blobs/<uuid>.FCStd`), indexes its central directory itself --
  zip64 included, which zipios cannot read -- and inflates and hashes every
  `blobs/` entry through one handle. Identity is still what the archive holds,
  never what an entry name claims. Each blob is created **archived**: an
  archive and an entry index, no path.
- **A copy, not the original**, because the next save replaces the file the
  document was opened from while its content is still referred to.
- **`FileBlob::read()`** returns the bytes from the file or from the copy.
  `parseBlob()` parses from them, and `writeBlobs()` writes an archived blob's
  entry from them, so neither opening nor saving a document gives any blob a
  file.
- **`FileBlob::path()` writes the file on first call** (`materialize()`), for
  the consumers that really need one: a `PropertyFileIncluded` value handed to
  Python, a MaterialX document, a material card. `hasExtension()` answers
  without writing one. Code that repairs stale paths (`relocate()`,
  `relocatedPath()`, the included-file `Save()` and `ensureBlob()`) skips
  archived blobs, which have no path to go stale.
- **The copy lives as long as a blob refers to it** (`shared_ptr` from each
  blob) and deletes itself after the last one goes.
- **One handle, closed around directory moves.** Windows refuses to rename or
  remove a directory with a file open in it, so the handle is closed after the
  ingest, and `closeArchives()` runs before the Uid rename in
  `Document::onChanged()` and before `~Document` removes the transient
  directory. The next read opens it again.
- **Switch:** `ArchiveBlobStore` (Preferences/Document, on). It needs a
  random-access reader (`ArchiveRandomAccess`) and a `blobs/Content.xml` in the
  archive; without either, or when the copy fails, the entries are read one
  file each as before.

### 14.4 The ingest must happen when the entries used to arrive

The first build did the copy in `beginRestore()`, before `Document.xml` is
parsed. Every blob was then already in the store when its referrer restored,
and `PropertyPartShape::Restore()` ends by asking for its shape (the element
map version check) -- which a blob that is present answers with a parse. That
undid the lazy load `ensureRestored()` exists for: MiSTer's XML data phase went
from 0.6 s to 3.9 s, with 92 "slow property restore" lines instead of 3.

So `beginRestore()` only notes the archive, and the copy is made when the
archive handler is offered `blobs/Content.xml`. `ZipFileReader::readFiles()`
drains unregistered entries before registered ones, and the index is written
ahead of the content, so that is the moment the content always arrived. Once
the copy serves, the name predicate refuses the remaining blob entries, and a
random-access reader does not even open them.

### 14.5 Measured

MiSTer, headless (`FreeCADCmd`), `build/win-relwithdebinfo-801`, the laptop
above. "Parse" is touching every `Shape` after the open, which is the lazy
parse paid in full.

| store | open | of which files | parse all shapes | files in store |
| --- | --- | --- | --- | --- |
| a file per entry, staged (before `be256877c1`) | 409 s | 407 s | -- | 5399 |
| a file per entry, in-memory hash (`ArchiveBlobStore` off) | 40.4 s | 36.5 s | 36.2 s | 5399 |
| archive copy, ingest in `beginRestore()` (sec 14.4) | 6.1 s | 0.3 s | 21.4 s | 1 |
| **archive copy, ingest at the index entry (as built)** | **2.4 s** | **0.29 s** | **22.9 s** | **1** |

As built, the open is 2.4 s: 1.9 s of XML, of which create is 1.33 s and
property data 0.56 s -- back to what it was before the store was touched,
with the same 3 slow-restore lines -- and 0.29 s for the file phase, which is
the copy (0.05 s) and inflating and hashing all 5400 entries (0.24 s). The
3.7 s the premature ingest added to the open is gone from it, and reappears
in the parse, where it belongs.

In the GUI, with the settings the 2026-09-13/14 runs used (`scripts/render-bench.py`,
`bgfx - OpenGL`, 1280x720, no vsync, settle 900 s / quiet 5 s):

| store | load | settle | frame | scene |
| --- | --- | --- | --- | --- |
| a file per entry, staged (2026-09-14) | 504.7 s | 9.1 s | 252.8 ms | 45867 draws, 18.16 M prims |
| archive copy (as built) | **97.3 s** | 8.7 s | 220.3 ms | 45867 draws, 18.16 M prims |

The same scene, drawn as fast, and 407 s -- the file phase -- off the load.
What is left of the 97 s is not the store: the headless open plus a parse of
every shape is 25 s, so the other ~70 s is the GUI side building 17058
visuals, and that is the next thing to chase for this document.

Chased in DocumentLoad.md sec 16: every restored visual was built twice, and
the main window re-tested every command about once a second while the objects
were created. With both fixed the same bench loads in 75.4 s.

(The 54 s of sec 14.1 and the 40.4 s here are the same code on different
runs; this box's file-create cost varies run to run.) The parse is faster out
of the copy too: one open handle, where the files cost an open each.

`FileBlobs` covers it in `BlobArchiveStoreCases`: no file per blob on reopen,
`path()` writing only the one asked for, binary and leading-whitespace content,
a save over the original, save-as moving the copy with the directory, close
removing the directory, the last referrer taking the copy with it, a copy
across documents, the switch off, and a shape parsed out of the copy.

## 15. A pack store: no file per blob (design, 2026-09-24)

Status: **design, not built.** Asked for by the user on 2026-09-24 after a
recovery cleanup froze the GUI for about two minutes. To be coordinated with
the `Transaction` branch (`docs/TransactionLog.md`, developed by the `oplab`
session), which adds far more referrers to this store than everything else
put together.

### 15.1 What is wrong

Section 14 took the file-per-blob cost out of **opening** a document. It is
still paid everywhere else a blob is made, and on the company-managed Windows
laptop every file is a round trip to the filesystem monitor. Measured there
in `getUserCachePath()`: create+write+close+chmod about 11.8 ms, open about
1.4 ms (sec 14.2), and delete about 9 ms -- 25460 files in 939 leftover
transient directories took 233 s to remove on 2026-09-24.

Where loose blob files still come from:

- **Save.** `PropertyPartShape::storeBlob()` writes every shape whose blob is
  not current to a file of its own (`uniquePath("shape.brp")`, then
  `adoptFile()`). The first save of an imported model is therefore one file
  per shape, and the files stay for the rest of the session. One leftover
  directory held 16474 `.brp` files (122 MB): a saved MiSTer import.
- **Included files made in the session**: imports, materials, MaterialX
  manifests, TechDraw templates -- every `insertFile()` / `adoptFile()` /
  `adoptBytes()` call site.
- **The transaction log, as designed.** `docs/TransactionLog.md` sec 13.2 and
  23.1 keep values over a threshold (64 KB to start) as files in this store,
  and version snapshots add more. Every commit that touches a large shape adds
  a file, and a long session with many versions multiplies them. Reverse
  deltas (23.2) shrink the bytes, not the number of files.

What the loose files then cost, besides the creates:

- **Deleting them.** Blob files are read-only (`writeNewFile()` and
  `adoptFile()` set `ReadOnly`). `Base::FileInfo::deleteDirectoryRecursive()`
  clears the flag first, but the recovery dialog's "Cleanup..."
  (`DocumentRecoveryCleaner::clearDirectory()`) does not, so on Windows every
  delete fails. It ran 16474 failing deletes on the GUI thread -- the freeze --
  and then removed the lock file anyway, turning the directory into a
  permanent orphan: the startup scan only finds directories through a lock
  file.
- **Orphans nobody lists.** Only the GUI creates the instance lock
  (`Gui::Application::runApplication`), so a `FreeCADCmd` or test process that
  dies leaves transient directories no scan will ever find. 868 of the 938
  orphans found on 2026-09-24 were empty directories of that kind.

The recovery bugs are fixed separately; they are bugs whatever the store
does. This section is about the store.

### 15.2 The design

A blob stops being a file. It becomes an entry in an index -- **hash to
(segment, offset, length, encoding)** -- and its bytes live in one of a few
large files.

- **Segments.** Append-only files in the store directory,
  `blobs/seg-NNNN.pack`, rolled over at a size limit (start at 256 MB). One
  handle is open for appending; the others are opened for reading on demand
  and closed around directory moves, exactly as the archive copy is (sec
  14.3). A blob is written once, at the end of the current segment, and never
  rewritten in place.
- **The index in SQLite.** The `Transaction` branch already puts a SQLite
  database in the transient directory (`history/log.db`, WAL,
  `synchronous=NORMAL`). The blob index is a table in it -- or in a
  `blobs/index.db` of the same form if the log is not built yet -- keyed by
  hash, with segment, offset, length, encoding (`raw`, `zstd`) and extension.
  Small blobs, under a threshold to be measured (sec 15.4), are stored inline
  in the row and never touch a segment.
- **The archive copy is a segment.** Section 14's `blobs/<uuid>.FCStd` already
  is one: a single file whose entries are found through its own index. It
  becomes a read-only segment whose entries happen to be deflated zip members,
  so restore, save and the log all use one mechanism.
- **Save.** `storeBlob()` serialises the shape into memory, hashes it and
  appends it (deduplicated by hash, as now). `writeBlobs()` streams each entry
  from its segment into the zip, the way it already streams an archived blob.
  No file per shape.
- **Real files only on demand.** `FileBlob::path()` keeps `materialize()`: a
  consumer that needs a path -- Python, MaterialX, a material card, an
  external editor -- gets a file written under `blobs/materialized/`. They are
  few, and they go with the document through `deleteDirectoryRecursive()`,
  which handles read-only.
- **Lifetime.** `FileBlobHandle` refcounting stays as it is. When a blob's
  last referrer goes, its index row is dropped and its range becomes dead space
  in its segment.
- **Compaction.** A segment whose dead fraction passes a threshold (start at
  50%) is rewritten: live ranges copied to the current segment, their index
  rows updated in one SQLite transaction, then the old segment deleted. It runs
  on a worker thread, never the GUI's. The log's collector
  (`docs/TransactionLog.md` sec 23.5) marks dead entities and feeds the same
  pass.
- **Crashes and orphans.** A transient directory becomes a few segments, an
  index and a handful of materialized files, so a leftover costs a few deletes
  instead of tens of thousands. The index database is also where session-mode
  crash recovery (`docs/TransactionLog.md` sec 13.3) finds the blobs of the
  log's tail.
- **Browser and mobile.** Segment files and SQLite both work on OPFS (SQLite
  has an official WASM build with OPFS persistence), so nothing here closes off
  the WASM tier.

### 15.3 What it changes for the transaction log

`docs/TransactionLog.md` sec 23.1 says where an entity's bytes live is "a
backend detail chosen by size: small inline in `data`, large as a file in the
document's `FileBlobManager`". With this store the large case is a range in a
segment, not a file. Two consequences:

- **The file-versus-inline threshold goes away.** Section 13.1 cites SQLite's
  guidance that blobs up to about 100 KB are faster inside the database than as
  files. On this laptop the crossover would be far higher, since a file costs
  milliseconds. With a pack store no value is ever a loose file, so the only
  choice left is inline row versus segment range, which is about SQLite page
  churn, not the filesystem.
- **One store, one collector.** Entities and blobs share the index and the
  segment space; trimming (23.5) and blob refcounting both end in the same
  compaction pass.

### 15.4 To measure before building (phase 0)

On the laptop, in `getUserCachePath()`, with MiSTer's 5401 blobs and the
16474-shape import, as sec 14.2 measured with `archbench.py`:

| backing | create | read all, random order | delete the store |
| --- | --- | --- | --- |
| a file per blob (today) | | | |
| everything inline in SQLite | | | |
| SQLite index plus segment files | | | |

Also: the inline threshold for SQLite rows (page churn under WAL), the
segment roll size, and the cost of a compaction pass over a 1 GB segment.
Linux and macOS rows too: the design must not make the case that is free there
slower.

### 15.5 Open questions

- Whether the blob index lives in the log's database or in its own. Shared
  means one transaction covers a log commit and the blobs it names; separate
  means the store works without the log. Decide with the `Transaction` branch.
- Encoding: shapes are text BREP by default and compress about 4:1 with zstd
  (`docs/TransactionLog.md` sec 20.1). Compress in the segment, or keep them raw
  so a range can be handed to a reader without inflating?
- Whether a save could make a copy of the **saved archive** the new read-only
  segment, as restore does, instead of appending the shapes it just wrote.
  Save and restore would then be fully symmetric, at the cost of one file copy
  per save.

### 15.7 As ruled (user, 2026-09-25) -- built on branch Transaction

Settled with the user in the `Transaction` session, which builds it (the
log is the store's largest client). Where this disagrees with 15.2-15.5,
this wins. The other session's refinements of 15.1-15.6 (`804c13dfb8`, not
in this repository yet) merge in when the branches meet.

**The answers to 15.5.**

- The index is its own database, `blobs/index.db`: the log replaces its own
  database at run time (an embedded copy is adopted, `docs/TransactionLog.md`
  16.4) and would take a blob index with it.
- New content is **zstd**. What a segment holds is a zip member, so each
  member carries its own method: zstd (zip method 93) for content this
  store writes, deflate for members that arrived in an opened archive.
- The third question -- adopt the saved archive as a segment -- is withdrawn:
  it would move blobs, and under the scheme below nothing ever moves.

**Segments are zip files, and a blob never leaves its segment.** Members
are named by content hash. The index maps a hash to its segment id (with
extension and size) and nothing else: where a member sits is the
segment's own central directory's business. Since a blob stays where it
was first written, an index row never changes after it is made. The
archive copy of an opened document (sec 14) is simply the first segment.

**A segment is rewritten, never modified: generations.** Appending new
members and dropping dead ones is one pass:

1. Stream segment N's live members, raw -- a member record is copied as
   bytes, never inflated or re-compressed -- into a temporary file, append
   the new members, write the central directory, flush.
2. Rename the temporary file to `seg-N.<g+1>`.
3. Try to delete `seg-N.<g>`. If it cannot be deleted -- on Windows, a file
   another handle has open -- leave it.

A reader never sees a file change under it: whoever holds `seg-N.<g>` keeps
reading it, its offsets valid, because generations are immutable once
named. New opens take the highest generation on disk; inside the process
the writer bumps a generation counter, and a reader that needs a member
newer than its generation reopens. Old generations and leftover temporary
files are deleted when they can be -- at the next rewrite, at close, on
open. A crash leaves at worst an incomplete temporary file, which is never
a generation and is deleted on open. Local headers carry the member sizes
(no data descriptors), so a segment can still be rebuilt by a scan if a
central directory is ever found missing.

**Repack rides the rewrite.** Every rewrite drops the dead members it
passes over, so a segment that is written to shrinks as it goes, and new
content can go into a shrunken segment. A segment nothing is written to
is rewritten once its dead fraction passes a threshold; a segment with
nothing live is deleted. Segments are never merged, so their number falls
only as they empty -- measured before any merge is considered.

**Appends are batched.** A rewrite copies the segment, so new content is
written per batch -- a commit group, a save, a snapshot -- one rewrite per
segment per batch, not per blob; segments are capped (64 MB to start,
measured) and a new one opened when the current is full.

**Order of writes.** The segment first, the index second; on open the two
are reconciled: an index row naming a member that is gone was a dead
blob's and is dropped; a member no row names is dead space for the next
rewrite.

**Save.** At schema 5 the document's blob entries are copied raw from the
segments into the FCStd -- zstd members stay zstd, so a blob is compressed
once in its life and a save re-compresses nothing. That needs a writer
that can add a member as raw bytes, which `zipios::ZipOutputStream` cannot;
ours sits beside the random-access reader of sec 14, which gains the
zstd method. Schema 4 is upstream's format and has no blob manager: each
property writes its own entry, reading through `FileBlob::read()` (which
decodes), and zipios deflates it, as today. The price of zstd members:
schema 5 is readable only by this fork already, and a stock zip tool may
not open a schema 5 file's blob entries.

**Unchanged.** `FileBlobHandle` as reference and refcount as lifetime,
`read()` returning bytes wherever they live, `adoptBytes()` storing by
hash, `path()` materialising on demand (15.6 on the other side). The
transaction log relies on nothing else (`docs/TransactionLog.md` 23.1,
note of 2026-09-24).

**Next: phase 0 (15.4) on this box** -- file per blob, everything in SQLite,
and this layout, for create, random read and deleting the store, plus the
rewrite cost at the cap -- then the laptop's rows, which motivated all of
it.

### 15.8 No index database; merges through an in-memory redirection (user, 2026-09-25)

Refines 15.7, and where they disagree this wins.

**The central directories are the index.** A segment is a zip whose
central directory lists its members by name, and a member's name is
`<hash>.<ext>`: that is the hash-to-segment mapping, the extension, and
(from the directory) the size and the method. On open the store reads the
directories of its few segments -- sec 14 already reads the opened
archive's this way -- and builds the hash-to-segment map in memory; every
write updates it in memory. Liveness was never persisted: it is the
handles' refcount, in memory. After a crash every member counts as live
until the restored document has claimed what it references; the next
rewrite drops the rest. `blobs/index.db` of 15.7 is therefore withdrawn:
nothing it would hold is not already in the segments or in memory.

**Nothing persisted names a segment.** The segments' directories name
members by hash; the saved FCStd names blob entries without segments; the
transaction log names a blob by its hash (`enc = file` is "the store's copy
of this hash", `docs/TransactionLog.md` 23.16). Where a blob lives is always
answered by looking, never by a stored pointer.

**Merges are allowed, occasionally.** Several segments each under a small
live size (a quarter of the cap to start) are merged on the worker thread,
never during a save: their live members are streamed raw into a new
segment through the usual temporary file and rename (15.7), then the old
segments are deleted if they can be.

**The redirection is in memory only.** A live `FileBlob` remembers its
segment so that a read needs no lookup -- which is what a merge would have
to rewrite blob by blob. Instead a handle names a *logical* segment, and a
small table maps logical segments to the file that holds them now; a merge
of A and B into C sets `A -> C` and `B -> C`, two entries whatever the
number of blobs, under the lock the log's worker respects. The table is
for handles alive in this process and dies with them: on the next open
there are no handles to redirect, and the directories say where every
blob is. So it is never saved, and it needs no crash safety of its own:

| Crash | On disk | Next open |
| --- | --- | --- |
| while C is the temporary file | A, B, an incomplete temporary | the temporary is deleted; blobs found in A and B |
| after C is renamed, before A and B go | A, B, C, all complete | each merged blob found twice, identical bytes (content-addressed); C, the newest, is chosen; A's and B's copies are dead space for their next rewrite |
| after A and B are deleted | C | blobs found in C |

The same rule covers a generation that could not be deleted (15.7): a
duplicate is harmless because it is the same bytes, and the newest copy
wins.

**Phase 0 changes accordingly:** file per blob against this layout; the
SQLite leg goes with the index database.

**When the directories are read, and duplicates** (user, 2026-09-25).
Only a recovery reads more than one: a normal open starts a fresh store
whose single segment is the archive copy (sec 14), whose directory is read
already, and within a session the map is kept in memory as segments are
written, never rescanned. Recovery reopens a leftover transient directory
and reads each segment's directory -- one seek to the end and one read,
about 80 bytes a member (some 400 KB for MiSTer's 5400 blobs), a few
segments at the cap. Segment numbers only grow, and a merge's output gets
a higher number than its inputs; building the map, a hash already seen
keeps the higher-numbered segment, one comparison per duplicate. (Not the
segment with more members: a later rewrite may leave the merged segment
with fewer.) The same pass finishes a merge the crash interrupted: a
segment whose every member is held by a higher-numbered one, or referenced
by nothing the recovery restores, is deleted there and then.

**The one ordering rule between two files.** The log and the store are two
files, and the hazard between them is a log row naming a blob whose bytes
never reached the disk. So a blob's segment generation is renamed and
flushed (the directory entry with it) before any log row that names the
blob commits. The log's writer already stores a value's blob entities
before it appends the row; the rule adds one flush there, and only when a
commit brings a new blob.

### 15.9 Phase 0 on Linux (2026-09-25)

`scripts/archbench.py` with the pack store's legs: `segments` (zstd members
at level 3 in zip segments, each written as a temporary file, flushed and
renamed; open = read the central directories; a generation rewrite with
half the members dead and 5 % new; a merge) and `save` (the blob half of
a save: deflating every blob at the save's level 3, against copying the
zstd members raw). Run on this box's ext4 home, in `~/.cache/FreeCAD`,
twice, identical to the millisecond. MiSTer is not on this box, so the
document is synthetic at its scale: 5402 Part solids (boxes, holes,
fillets), one blob each, 53.1 MB raw, 12.1 MB deflated in the archive.

| leg | create s | index s | read all, random s | delete the store s |
| --- | --- | --- | --- | --- |
| A archive copy, inflate on read | 0.00 | 0.015 | 0.12 | -- |
| D file per blob (today) | 0.09 | -- | 0.03 | 0.036 |
| E segments, zstd | 0.27 | 0.002 | 0.10 | 0.000 |

- zstd level 3 holds the blobs in **10.8 MB**, against 12.1 MB deflated in
  the archive and 12.7 MB at the save's deflate level 3.
- **Save: 0.46 s deflating every blob, 0.02 s copying the zstd members
  raw.** The compression E pays at create (most of its 0.27 s) is the
  compression a save pays today every time, paid once per blob instead.
- A generation rewrite of a 10.8 MB segment, half dead and 5 % new: 0.015 s;
  of a 2 MB segment (a 2 MB cap, 6 segments): 0.005 s; a merge of two
  quartered 2 MB segments: 0.003 s.
- Reads cost zstd decoding, 0.10 s for all 5402, less than inflating the
  archive copy (0.12 s); a BRep parse dwarfs either.

Linux does not have the problem: a file per blob costs 0.09 s to write and
0.036 s to delete here, against 23 s and ~9 ms a file on the monitored
laptop (14.2, 15.1). What this run shows is that the pack store does not
make Linux slower where it matters -- save drops by 0.44 s, deleting the
store to nothing -- and costs it compression once per new blob. The rows
that decide are the laptop's, on MiSTer, with the same script (legs
`files,segments,save`).

**The laptop's rows** (2026-09-25, the `PartDesignPort` session on the
monitored Windows laptop, `archbench.py` at `205c1dd511` with the split
leg G of 15.10, in `getUserCachePath()`, conda Python 3.12). MiSTer: 5401
entries, 87.3 MB raw, 19.0 MB deflated. Two runs at the 64 MB cap agreed:

| leg | create s | index s | read all, random s | delete the store s |
| --- | --- | --- | --- | --- |
| D file per blob (today) | 16.24 | -- | 13.1-15.9 | 14.1 |
| E segments, zstd | 1.00 | 0.012 | 0.30 | 0.005 |
| G split on open (64 MB cap: 1 segment) | 0.82-0.86 | 0.032 | 0.28 | 0.010 |

- zstd holds MiSTer in 16.1 MB against 19.0 MB deflated.
- **Save's blob half: 1.75-2.25 s deflating, 0.48-0.51 s copying zstd
  members raw.**
- The split on open: 0.56-0.61 s for the raw copy, 0.82-0.86 s with the
  decode and hash the restore pays anyway, against 0.04 s for the single
  copy of sec 14. Against the 2.4 s open of 14.5 that is about a third more.
- **A stall in the second segment write.** The rewrite of seg-0 took
  10.3 s, the merge 10.7 s, and at a 16 MB cap the split's second segment
  11 s. Timed inside the writer: the write and the fsync take milliseconds,
  and about 10 s goes to close plus rename, not scaling with size (a
  5-member segment took 2.2 s). The first segment in a run is always fast.
  **Pinned down by bisection on the laptop:** renaming a file that is a zip
  of under about 10 MB (between 9.9 and 10.7 MB) blocks the rename for
  3-11 s, growing with the member count (5 members 3 s, 270 5 s, 2700 or
  more about 10 s) -- presumably the endpoint scanner unpacking archives
  under a size cap. Over the cap a rename costs 0.02 s, a non-zip of the
  same size 0.05 s, and a junk prefix before the zip does not hide it.
  **The same bytes written straight to their final name are never
  charged**, and reading them afterwards is no slower (0.15 s for every
  member). Close is always free. So the store writes a generation to its
  final name, not through a `.tmp` and a rename (15.11).

### 15.10 Names, and the opened archive split on open (user, 2026-09-25) -- design finished

**Names.** A segment is `blobs/seg-<N>.<g>` in the store directory: `N` the
segment number, growing per store and never reused (what makes "the
newest wins" of 15.8 hold), `g` its generation, growing from 1. A write in
progress is `seg-<N>.<g+1>.tmp` and is renamed when complete, so a file
whose extension is a bare number is always a complete segment and a
`.tmp` is debris. Opening a store takes the highest `g` of each `N` and
deletes the lower generations and the `.tmp` files it can.

**The opened archive is split on open.** Sec 14 copies the opened
document into the store as one file, which would make it one giant
segment -- far over the cap for a large document, and a rewrite of all of
it whenever its dead fraction made it worth shrinking. Instead the open
splits the archive's blob members into capped segments, `seg-1.1` to
`seg-K.1`, copying each member raw, never inflating it. It writes the same
bytes the copy does (fewer: the XML entries stay out) plus K file creates,
about 8 for 500 MB; every segment is then capped, so every rewrite,
shrink and merge is bounded, and the store has one kind of segment. The
members that arrive this way keep their deflate method beside the zstd
members written later. The reason sec 14 copies at all -- the next save
replaces the file whose content is still referred to -- is met the same
way.

**Phase 0 adds a row:** splitting MiSTer into capped segments on open,
against the single copy (0.05 s on the laptop, 14.2).

**The design is finished.** Implementation starts in the next session, in
the order of the plan: the laptop's phase 0 rows (with the split row
added), then the store -- the raw-member zip writer, the zstd method in
the reader, segments by generation with the in-memory map and
redirection, the split on open, the schema 5 save copying members raw,
repack and merge, and the ordering rule against the log.

### 15.11 As built (2026-09-25)

Built on branch `Transaction` in the 15.10 order. What the design left open,
and how it was settled:

- **zstd in zipios, not only in the store.** zipios' forward reader throws
  on a method it does not know, and it throws in `getNextEntry()`, so one
  zstd member would have lost every entry after it. `ZipInputStreambuf`
  decodes method 93 by streaming (`ZSTANDARD` in `StorageMethod`), so the
  forward-only walk, the random-access reader and anything else reading an
  archive through zipios reads a schema 5 file. zstd is optional in the
  build (`FC_HAVE_ZSTD`, now found by Base as well as App): without it new
  members are deflated, and a zstd member cannot be read.
- **The raw-member writer is zipios' own.**
  `ZipOutputStream::putRawEntry()` writes a whole member whose bytes are
  already compressed, headers from the caller's method, CRC and size, and
  `Base::Writer::putRawEntry()` exposes it: `ZipWriter` takes it (below
  4 GB), every other writer answers false and is handed the content
  decoded -- a project directory gets plain files as before.
- **Segments are written by the store's own zip writer**
  (`SegmentWriter`): no data descriptors, one fixed timestamp so the same
  members make the same bytes, at most 65535 members (no zip64 records),
  `fflush` + `fsync` (`_commit` on Windows), and on POSIX the directory
  fsynced for the new entry. Every segment write is durable, so the
  ordering rule of 15.8 reduces to "flush the batch before the row".
- **No `.tmp` and no rename -- a departure from 15.7 and 15.10, forced by
  the laptop** (15.9: renaming a zip under about 10 MB costs it 3-11 s),
  and accepted by the user (2026-09-25).
  A generation is written straight to `seg-<N>.<g>`. The rename bought no
  atomicity: the name has never existed, and no reader opens a generation
  before the store installs it, which is after the file is complete and
  flushed. What changes is the crash rule: a file whose name ends in a
  bare number may now be incomplete, so recovery takes a segment only if
  its end-of-central-directory record is there and the directory it points
  at checks out, and deletes the rest as it deleted a `.tmp`.
- **What a blob knows.** `FileBlob` keeps `_packed` and a logical segment
  number, 0 while its member is only in memory. The manager keeps the
  hash-to-segment map of 15.8 (`_where`, live and dead members alike), the
  live segments by number, the merge redirection, and the batch
  (`_unflushed`, members compressed at adopt time). Content the store
  still has -- on disk or in memory -- is taken back by hash without a
  write, so a shape changed and changed back, or an undo past the
  content's release, costs nothing.
- **Batches.** A save (after its blob entries), the end of a restore, a
  segment's worth gathered in memory, and the transaction log's commit
  (`makeDurable()`, one flush for all the blobs a commit names). The batch
  goes into the segment last written while it has room, whose next
  generation drops its dead members on the way; the rest into new
  segments.
- **Maintenance on a worker thread** (`FileBlobManager::workerLoop`),
  started the first time a segment member dies, settling 200 ms so a burst
  of deletes is one pass, and standing aside while a save writes blobs.
  It deletes segments with nothing live, rewrites those more than half
  dead, and merges those under a quarter of the cap into a new, higher
  number. `Document` holds segment writes across the transient directory
  rename (after closing the log's store, whose worker writes here) and
  stops the worker before deleting the directory.
- **Loose files remain** for content over a quarter of the cap
  (`BlobSegmentSize`, 64 MB, so 16 MB), for the store with no document
  (`defaultManager()`), and with `ArchiveBlobStore` off. `adoptFile()`
  packs the content and keeps the file it was handed as the blob's own:
  its callers usually want a path next, and the file exists already.
  `insertFile()` packs and writes nothing. Shapes are serialised in memory
  (`PropertyPartShape::storeBlob()` -> `adoptBytes()`), so the first save
  of an import is no longer a file per shape.
- **The split on open** reads the opened archive in place, decodes and
  hashes each blob member as the restore always did, and hands the member
  raw to the batch, which writes capped segments as they fill.
- **Tests read zstd members through `Mod/Test/ArchiveMembers.py`**: Python
  3.12's zipfile cannot (3.14's can, and the helper defers to it).

**Measured on this box (Linux, ext4)**, headless, the synthetic 5402-solid
document of 15.9, two runs each, the pack store against `ArchiveBlobStore`
off (a file per blob -- sec 14's single archive copy is gone):

| step | pack store | a file per blob |
| --- | --- | --- |
| open | 1.27-1.29 s, 1 file in the store | 1.37-1.40 s, 5400 files |
| parse every shape | 4.28-4.43 s | 4.05-4.08 s |
| save-as | 0.66-0.73 s | 1.07-1.09 s |
| first save of an import (5402 new shapes) | 2.68-2.70 s, 2 files | 2.79-2.83 s, 5401 files |
| save it again | 1.08-1.12 s | 1.43-1.49 s |
| close both documents | 0.23 s | 0.39-0.40 s |

The parse pays about 0.3 s for inflating out of the segment where Linux
reads a plain file for nothing; the monitored laptop is the other way
round by two orders (15.9). Everything a file per blob costs in creates and
deletes is gone, and the import's first save compresses every shape with
zstd and still finishes sooner. The second file after a save is a material
card, which comes in through `adoptFile()` and keeps its file. The laptop's
rows, on MiSTer in the real application, are the next measurement.

**Measured on the Windows laptop (the dwin session, 2026-09-25)**,
headless, `MiSTer.FCStd` (17800 objects, 17058 shapes), `Transaction` at
`6c97f88c83` against OCCT `84c8abc4da`, a fresh `FREECAD_USER_HOME` per
run, `ArchiveBlobStore` set before the open, seconds, two runs each:

| step | pack store | a file per blob |
| --- | --- | --- |
| open | 65.4 / 58.6, 1 file | 90.8 / 83.2, 5399 files |
| parse every shape | 2.8 / 2.9 | 18.3 / 16.4 |
| save-as | 8.5 / 9.7, 2 files | 71.4 / 61.4, 5400 files |
| save it again | 8.3 / 9.2 | 29.6 / 26.4 |
| close | 0.19 / 0.18 | 35.8 / 29.6 |

The saved file is about 20.9 MB either way, and the transient directory is
gone after every close. A same-day baseline of the main build (sec 14's
single archive copy, OCCT `d58f0d2fa5`) gave open 59.4, parse 2.6,
save-as 9.0, save 8.8, close 0.18 with its store on, and 93.7 / 28.7 /
88.1 / 43.2 / 41.2 with it off. **The pack store matches sec 14's archive
store at every step and beats a file per blob everywhere** -- save-as
about 7x, close about 170x. The one-minute open is not the store's: the
main build opens as slowly. It differs from sec 14.5 (open 2.4 s, parse
22.9 s) because the shape work now happens inside the open, on both
branches; not chased.

The GUI leg, `render-bench.py` at sec 14.5's settings (bgfx on OpenGL,
1280x720, no vsync), store on, one run each: the pack store loads in
31.9 s, settles in 9.4 s (10 frames) and draws a frame in 217 ms; the
archive store 34.3 s, 8.8 s (9 frames), 221 ms -- the same scene (45903
draws, 17.9 M primitives), the difference inside one run's noise. The
headless open taking about 60 s on both builds, twice the whole GUI load,
is serial work in the `FreeCADCmd` path that sec 14.5 did not see; open.

Not built yet: the recovery pass of 15.8 (reading several segments'
directories, the newest number winning, finishing an interrupted merge) --
that is phase 7 with the log's session recovery; a store opens fresh
today and numbers its segments past any it finds.
