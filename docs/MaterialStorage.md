# Material storage -- content-addressed cards in the document

Status: design agreed 2026-08-19 (user), implementation not started. The
`Materials::PropertyMaterial ShapeMaterial` property on `Part::Feature` is
built and green, as a faithful port of upstream; everything below replaces how
that property *stores* its value, not what it means.

Related: `docs/FileBlobsManager.md` (the blob store this builds on, and whose
save-side collect walk sec 8 generalizes), `docs/SharedShapeStorage.md` (the
dedup precedent, and the trap sec 4 inherits from it),
`docs/ShapeAppearanceDesign.md` (the per-face appearance tier, which this does
not replace and must not disturb).

---

## 1. What is wrong today

`Materials::PropertyMaterial` gets both halves of the problem backwards: it
duplicates what is identical at runtime, and refuses to store what is unique
on disk.

**Runtime: a deep copy per object.** The property holds `Material _material`
by value, and `Material`'s copy constructor copies element by element. Each
property copy runs `MaterialProperty prop(it.second)` and then wraps it in a
fresh `make_shared`, and `MaterialProperty::copyValuePtr` clones the value
outright, with explicit cases that duplicate whole `Array2D` and `Array3D`
tables. Nothing is shared. Measured on the shipped library: 215 cards, mean
13.9 properties, up to 33 (Copper-955, 27 physical + 6 appearance). Five
hundred steel brackets are five hundred complete copies of the Steel card.

The shared instances already exist and are deliberately thrown away:
`MaterialManager::_materialMap` holds `shared_ptr<Material>`, and
`defaultMaterial()` returns one. The property API forces the dereference --
`ADD_PROPERTY_TYPE(ShapeMaterial, (*mat), ...)`.

**Disk: a reference that is not guaranteed to resolve.**

    Save:    <PropertyMaterial uuid="..."/>
    Restore: setValue(*MaterialManager::getManager().getMaterial(uuid))

`MaterialManagerLocal::getMaterial` throws `MaterialNotFound` for an unknown
uuid. `PropertyContainer::Restore` catches per property and logs, so the
document still opens and the material **silently becomes Default**.

Reproduced 2026-08-19, not merely argued: a document was saved with Steel
assigned, the uuid in `Document.xml` was rewritten to one this installation
does not have, and the reopened document reported `Default` with nothing
surfaced to the user.

**What is actually lost.** Narrower than it first appears, and worse for it.
`ShapeAppearance` is a view property with `Prop_None` that stores colours,
finish and texture paths **by value** (`PropertyStandard.cpp` never mentions
`MaterialManager`), so the object still *looks* right on the far machine. What
is lost is the card: identity plus every physical property. Nothing looks
wrong, so there is no cue that a downstream mass or feeds-and-speeds figure is
now reading Default instead of 6061-T6.

**Upstream knows.** Issue 10902, "[Problem] Embedded materials", asks for
exactly this -- embed cards in documents, export them back out, create
in-document materials. Open since 2023-10-02, no assignee, no branch, no PR,
across the whole 1.0 and 1.1 cycles. Issue 10174 is a closed one-paragraph
placeholder that defers the design to a forum thread.

## 2. What other CAD does, and why the policy is not a judgment call

| System | Where the material lives | Do library edits reach old documents? |
| --- | --- | --- |
| Inventor | local copy cached in the document | No. Explicit Style Library Manager sync |
| Onshape | snapshot at assign time | No. The material must be re-applied |
| Fusion | "In This Design" scope, in the design file | No. Library is a separate scope |
| SolidWorks | reference into the library | Link breaks when the library is absent |

Inventor documents the consequence as intended behaviour, in a support article
titled "After editing Material or Appearance definition, the existing Inventor
documents do not contain the changed values", and states that a document moved
to a project with **no style library at all** continues to use its cached
styles. Onshape states that assigned parts "are not updated with any changes
made to the library".

Three of four converge, and two document it deliberately:

1. Copy into the document at assign time; the document is self-sufficient.
2. The document's copy wins; a library edit never mutates an old document.
3. Sync is an explicit user action, never automatic.

SolidWorks is the outlier and has precisely FreeCAD's failure mode. So
"embedded wins" is settled by prior art rather than taste.

## 3. The model

One material becomes one immutable, content-addressed blob, and every referrer
shares one parsed instance of it.

| Piece | Role |
| --- | --- |
| canonical `.FCMat` bytes | the stored content, hashed, held by `FileBlobManager` |
| `FileBlobHandle` | the reference; the shared_ptr count *is* the refcount |
| `shared_ptr<const Material>` | the parsed card, one instance per distinct content |
| uuid + name on the property | provenance, sec 6 |

`PropertyMaterial` holds a blob handle, a parsed handle, and the provenance
fields. `const` on the parsed card is what makes copy-on-write honest: no
referrer can mutate a shared card, so an edit allocates a new `Material`, a
new serialization and a new hash. Copy becomes a refcount increment, exactly
as `PropertyFileIncluded::Copy()` already is.

A `hash -> shared_ptr<const Material>` parse cache makes storage dedup into
runtime dedup at no extra cost -- one parse serves every referrer. This is the
mechanism `SharedShapeStorage.md` sec 7 reached for shapes, and it applies
more cleanly here (sec 4).

**Upstream already has the archive format, and it streams.**

    void Material::save(QTextStream& stream, bool overwrite, bool saveAsCopy, bool saveInherited);

The `.FCMat` YAML writer takes a stream; `MaterialLibraryLocal::saveMaterial`
is only a file wrapper around it. Read-back is
`MaterialLoader::getMaterialFromPath(library, path)`, and blobs are files on
disk, so the round trip needs no new format and no new parser.

## 4. Canonical serialization -- the trap this design lives or dies on

Content addressing is worth nothing if the same card serializes to different
bytes. Three things make that happen today.

**4.1 `saveInherited` means the opposite of what it reads like.** With it
true, the writer resolves the parent and omits everything matching it:

    if (!inherited || !parentProperty || (*property != *parentProperty)) { ...write... }

That is a delta which cannot be read without its parent -- the exact
dependency portability must not have. Blob serialization passes
**`saveInherited = false`**, writing every model and every non-null property
explicitly. Keeping the `Inherits:` block is harmless and preserves
provenance: if the parent is absent, `MaterialLoader::dereference` logs and
returns, and our values are already explicit.

Cards held from the `MaterialManager` are already flattened -- `dereference()`
walks the parent chain and copies models and values into the child where the
child is null -- so a blob written this way is self-contained without pulling
in ancestors.

**4.2 Model order is not stable across processes.** `saveModels` iterates

    for (auto& itm : _physicalUuids)     // QSet<QString>

and `_physicalUuids` / `_appearanceUuids` are `QSet`s. Qt randomizes QHash
seeding per process, so the same card serializes with its models in a
different order on every run: different bytes, different hash, sharing
destroyed, and the store gaining a duplicate per session. **The canonical
writer must sort the model uuids.** The property loop below it is already
fine, since `Model` iterates a `std::map`.

**4.3 Provenance must be outside the hashed bytes.** `saveMaterial` mutates
`setName`, `setLibrary` and `setDirectory` before writing, and the same card
obtained from two libraries would otherwise fail to dedup. Agreed 2026-08-19:
the hash covers **models and values only**. Name, uuid, author, license,
library, directory and filename are excluded, and the display name travels on
the property, matching `FileBlobsManager.md`'s rule that "names live on the
property, not the blob".

**Why this is cleaner than the shape case.** `SharedShapeStorage.md` sec 1.3
found content hashing only matched sharers whose incidental state agreed --
each object's placement is baked into the bytes it writes, so four identical
parts at four placements gave four hashes. A material card has no placement
and no geometry: once canonicalized it is pure data, so hashing is exact
rather than best-effort.

## 5. Bundled presets re-establish sharing across documents

Ship the stock cards in the same canonical archive form and index them by
content hash at startup, into the same `hash -> shared_ptr<const Material>`
cache.

On restore, a document blob whose hash matches a preset resolves to the
preset's existing instance: no parse, no second copy in memory, and sharing
re-established across every open document. On save, a blob whose hash is a
known preset need not be written at all, so a document using only stock
materials costs about zero extra bytes -- the common case pays nothing.

This is the piece that makes "always embed" affordable. Embedding is then only
paid for by documents that actually carry a custom or edited card, which is
exactly where it is needed.

## 6. What uuid is for once it stops being the key

The content hash answers "is this the same material?". The uuid keeps three
jobs it is still the only answer to:

1. **Relink anchor.** "This came from library card X" is what makes an
   explicit "update from library" / "save to library" possible -- the
   deliberate sync that Inventor and Onshape both require (sec 2).
2. **Upstream compatibility.** Upstream's `Restore` reads `uuid=` and nothing
   else. Keep writing it and our documents degrade gracefully there; drop it
   and they silently get Default.
3. **Model identity is untouched.** `_physicalUuids` name *schemas*, a
   separate namespace, and stay essential.

The two identities are allowed to disagree, and each disagreement means
something:

| | Same content hash | Different content hash |
| --- | --- | --- |
| **Same uuid** | the ordinary case; share | someone edited their library. Do not share; this is the case Inventor documents as intended |
| **Different uuid** | a card copied under a new name; share the content anyway | unrelated materials |

## 7. Document format and the schema gate

`FileBlobsManager.md` sec 3 establishes that schema 5 is this fork's format,
not a compatible middle rung, and that a schema-5 document is rooted
`<FCDocument>` so an unaware reader refuses it outright rather than
half-reading it. Materials sit on the same gate.

| Schema | What the property writes |
| --- | --- |
| <= 4 | `<PropertyMaterial uuid="..."/>` -- upstream's exact form |
| >= 5 | `<PropertyMaterial hash=".." uuid=".." name=".."/>` |

**Restore must accept both, and this is a hard requirement (user,
2026-08-19).** Reading an upstream document with a uuid and no hash has to
work:

1. `hash` present -> take the blob, parse or hit the cache. Authoritative.
2. no `hash`, `uuid` present -> resolve against the library, as today.
3. neither resolves -> a placeholder card that **retains the uuid and name**,
   so the assignment is visible, reportable, and can relink if the library
   later appears.

Case 3 is the whole point: the failure mode being removed is not "the material
is missing", it is "the material is missing and nothing says so". Silent
revert to Default must not survive this work.

## 8. Generalizing the blob mechanism (decision 3, 2026-08-19)

Materials are not to get a private store. Everything goes through the blob
manager on the same property-level mechanism, so the work is partly to
generalize what is there.

**The restore leg is already general.** `BlobReferrerProperty` is a pure
interface -- `virtual void assignRestoredBlob(const FileBlobHandle&)` -- and
`addPendingReferrer(hash, BlobReferrerProperty*)` is keyed on it, not on
`PropertyFileIncluded`. A new blob-backed property implements the interface
and the restore path already works.

**The save leg is not.** `Document::collectFileBlobs` walks properties and
does `Base::freecad_dynamic_cast<PropertyFileIncluded>(prop)`, so a blob held
by any other property class is never collected and never written. That single
cast is the generalization point: it must test for a blob-owning interface
instead of a concrete class, and `PropertyFileIncluded` and `PropertyMaterial`
both implement it.

**Blob creation is path-based**, not bytes-based: `insertFile(srcPath, ext)`,
`adoptFile(path, ext)`, `newBlobPath(ext)`, `hashFile(path)`. So a material is
serialized to `newBlobPath(".FCMat")` and then `adoptFile`d, which moves it
into the store under its content hash. No new manager API is required.

## 9. Invariants

1. The hash covers canonical model-and-value bytes only. Provenance never
   enters it (sec 4.3).
2. Model uuids are sorted before writing. An unsorted write is a silent
   sharing bug, not a cosmetic one (sec 4.2).
3. `saveInherited` is false for blob serialization, always (sec 4.1).
4. A parsed card reachable from a property is `const`. Editing allocates.
5. Restore never silently substitutes Default. Unresolved means a placeholder
   that keeps uuid and name (sec 7).
6. A document written at schema <= 4 carries upstream's uuid-only form and
   stays readable by upstream.
7. Everything `FileBlobsManager.md` sec 10 already requires of blob lifetime
   applies unchanged -- in particular, no handle is destroyed while the
   manager mutex is held.

## 10. Build order

1. **Canonical writer plus hash.** `Material` -> canonical bytes, sorted
   models, provenance excluded. Test: same card, two processes, one hash;
   an edited value changes it; a renamed card does not.
2. **Generalize the collect walk** (sec 8) so a non-`PropertyFileIncluded`
   property can own a blob. No behaviour change for existing documents.
3. **`PropertyMaterial` on blobs**: handle plus `shared_ptr<const Material>`
   plus provenance; save/restore across all three restore cases of sec 7.
4. **Preset index** by content hash at startup; skip writing preset blobs.
5. **Explicit sync commands** ("update from library", "save to library").
6. Only then: consider whether appearance and texture properties fold onto
   the same mechanism (the general reading of decision 3).

## 11. How this will be judged

- A document assigned a custom material, opened on an installation without
  that card, reports the material by name with its physical values intact.
  This is the acceptance test; today it silently reports Default.
- An upstream-written document with a uuid-only material still opens.
- A document written at schema 4 still opens in upstream FreeCAD.
- N objects sharing one material hold one `Material` instance, not N.
- A document using only stock materials is not measurably larger than today.
- The same document saved twice produces byte-identical blob entries.

## 12. Open questions

- Should an *edited* preset (same uuid, different hash) keep the preset's uuid
  or mint a new one? Keeping it preserves the relink anchor; minting one makes
  the two distinguishable without comparing content. Leaning: keep the uuid,
  and let the hash carry the difference (sec 6's table already assumes this).
- Is the placeholder card of sec 7 case 3 a real `Material` with empty models,
  or a distinct state the UI can render differently? A distinct state is
  better for the user and more work.
- Cross-document dedup of material blobs is the same best-effort tier as
  `FileBlobsManager.md` sec 10 describes for files, and is not in scope here.
