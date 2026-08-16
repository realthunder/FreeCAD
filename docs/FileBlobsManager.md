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
| `FileBlob` | One immutable, content-addressed file on disk |
| `FileBlobHandle` | `shared_ptr<FileBlob>` — a reference; the count *is* the refcount |
| `FileBlobManager` | Per-`App::Document` store, owns the mapping hash → blob |

**A blob is pure content.** Its identity is the SHA-1 of its bytes and nothing
else; it lives at `<transient>/blobs/<sha1>` with no name on disk. Blobs are
immutable — changing a property's file always produces a *new* blob.

**Names live on the property**, not the blob: `_BaseFileName` (the name it
saves under) and `_OriginalName` (where it came from), both persisted. Any
number of properties share one stored file while each keeps its own naming.
Two sites used to read a name off the storage path and now ask the property
instead: `Drawing::FeaturePage` (template-by-name fallback) and
`Gui::DlgEditFileIncludePropertyExternal` (names the copy handed to an external
editor). Every other consumer passes the path to a loader that sniffs format
from content, so a hash-named file is fine.

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
stacked above it. **4 is upstream's format and the default cap; 5 is this
fork's**, and a document written at 5 is rooted `<FCDocument>`, so a reader
that does not know the format refuses it outright instead of half-reading it.
The blob table is unchanged by the merge -- the gate still reads `>= 5`; it now
means "the fork format" rather than claiming to mean something weaker.

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
blobs/<sha1>            <- one entry per distinct content
blobs/<sha1>            
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
2. **Collect broadcast.** Walk every reachable `PropertyFileIncluded` and
   `noteReferenced()` it: App objects and the document, plus a Gui leg
   (`signalCollectFiles`) covering view providers **and** `MDIView`s, which is
   what puts a view-only blob such as the environment image into the save set.
   The broadcast is **scoped**: a full document for `save`/autosave, the
   exported subset only for `exportObjects`, so a clipboard buffer never carries
   blobs belonging to unrelated objects.
3. Stream `Document.xml`. Properties write hash/name/original; they register
   nothing.
4. `manager.writeBlobs(writer)` — one `putNextEntry("blobs/<hash>")` plus
   content per collected blob, in hash order so a given document always
   produces the same archive. It is a no-op below schema 5, where the
   properties carry self-contained copies of their own and these entries would
   be weight no reader ever asks for.
5. Everything else as before (`signalSaveDocument`, `writeFiles`).

Discovery is by traversal, not by liveness: blobs referenced *only* by an undo
transaction or the clipboard are live in the manager but not reachable from a
property, so they are correctly not written.

In directory mode the manager skips a blob whose target file already exists —
content addressing makes existence proof of equality. That is what makes
autosave cheap: today every autosave tick rewrites every embedded file, mirroring
what `RecoveryWriter::shouldWrite` already does for ordinary property files.

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
save options of §6.1 over both writers, and the export/import path via
`copyObject`. `scripts/file-blob-verify.sh` adds the GUI
legs that need a `View3DInventor`: the embedded environment image round-trip and
the assertion that it saves as a hash rather than base64. See those files for
the case-by-case matrix.

## 12. Future work

The save options are done (§6.1) and so is the browser-tier fetch (below). The
rest is staged -- worth doing, not scheduled.

- **Any file save through the manager, not just included files.** The store is
  already type-agnostic (`insertFile(path) → handle`, hash identity, refcounted
  lifetime); what is still `PropertyFileIncluded`-shaped is the referrer side:
  `addPendingReferrer` takes that concrete type, and the collect pass filters on
  it. Generalizing means a small referrer interface -- take the handle, and
  withdraw on destruction -- which the collect pass and dispatch use instead.
  Names stay on the consumer, as `_BaseFileName` does today, so one stored file
  can serve referrers that each call it something different. The thing to settle
  first is identity for generated content: a shape's bytes are what would be
  hashed, so the writer's mode becomes part of the address (`BinaryBrep` and
  ASCII hash differently). §6.1 settles what the existing options mean on this
  path, which is the groundwork for that.
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
