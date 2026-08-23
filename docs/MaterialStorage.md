# Material storage -- content-addressed cards in the document

Status: design agreed 2026-08-19 (user). Sec 10 steps 1 to 5 are built, tested
and landed: the canonical writer with its content hash, the generalized
collect walk, `PropertyMaterial` on blobs, the preset index, and the explicit
sync commands. Step 6, folding appearance and texture properties onto the same
mechanism, is the open one. The `Materials::PropertyMaterial ShapeMaterial` property on
`Part::Feature` is built and green, as a faithful port of upstream; everything
below replaces how that property *stores* its value, not what it means.

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

**4.4 Quantities are written in the reader's unit schema.** The third one, and
the one that survives every precaution above. `MaterialValue::getYAMLString`
renders a quantity with `Base::Quantity::getUserString()`, which is
`UnitsApi::schemaTranslate` -- the *user's* unit schema, a preference. The same
card written by a user working in imperial units is different bytes, and that
schema's decimal count rounds the value on the way out, so it is not only a
different hash but a lossy one. `Array2D` and `Array3D` render every cell the
same way.

The canonical writer renders a quantity as its internal value plus
`Unit::getString()` -- `"7.9e-06 kg/mm^3"` -- at the shortest precision that
reads back as the same double. That depends on the value alone.

A plain `Float` property has a smaller version of the same problem from the
other end: a value read from a card is a `float`, a value set through the API
may be a `double`, and widening the first gives `0.30000001192092896` where
the second gives `0.3`. Floats are written at the shortest precision that
reads back as the same *float*, so both routes to the same number agree.

**4.5 The canonical form, exactly.** It is ordinary `.FCMat` YAML, so
`MaterialLoader` reads it with no new parser, but nothing outside the card's
own models and values reaches it:

| Written | Not written |
| --- | --- |
| `General: UUID` -- the *nil* uuid, always | the card's uuid, name, author, license |
| `Description`, `SourceURL`, `ReferenceSource`, `Tags` (sorted) | the `# File created by <version>` header |
| `Inherits:` keyed by the parent uuid | library, directory, filename |
| `Models:` / `AppearanceModels:`, keyed and sorted by model uuid | |
| each model's values, in property-name order | any null value |

Three choices in that table are not obvious.

The nil uuid is there because `MaterialLoader::getMaterialFromYAML` requires a
`General/UUID` node, so the slot cannot simply be dropped; filling it with the
nil uuid also reads correctly as "this card's identity is not in this file".

The model blocks are keyed by model *uuid* rather than by the model's name,
which is what upstream's writer uses. The name is a lookup in the installed
model library, so it makes the bytes depend on the installation; the loader
reads the `UUID` inside the block and ignores the key entirely. For the same
reason the properties written are the ones the card holds, each grouped under
the model it names (`MaterialProperty::getModelUUID`), rather than the ones
the installed model *declares* -- a card whose model this installation lacks
still writes its values out.

`Description`, `SourceURL`, `ReferenceSource` and `Tags` are inside the hash
even though they are arguably provenance, because unlike name and uuid they
have nowhere else to travel: the property carries the display name, and
anything else left out of the bytes is simply lost on the round trip through a
blob. Two cards that differ only in their description are then different
content, which is the harmless direction to be wrong in.

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
re-established across every open document.

**Revised 2026-08-23: the save half of this is off by default.** A preset's
content was originally left out on save, on the reasoning that any
installation holding the library can produce it again. That is true only
while the library does not move, and it moved: `7f5a3b7d49` retuned the
default appearance, which changed the Default card's content and so its
hash. Every document written before it names a hash no installed card
answers to -- and because `Restore()` treats the hash as authoritative and
reaches the uuid branch only when the hash attribute is *absent*, the miss
loses the material outright instead of degrading to the uuid that is
sitting unchanged in the same element. One IFC building lost the material
on 13642 objects that way.

A shipped library is not a fixed point, so a document may not be built on
the assumption that it is. `DocumentParams::SaveMaterialCards`, default
**true**, writes every card including the stock ones; set it false for the
old behaviour. What makes this affordable is the per-document dedup, not
the omission: identical cards are stored once, so a model whose objects all
share one card carries that card once whatever the object count.

Embedding beyond that one copy is still only paid for by documents that
actually carry a custom or edited card, which is exactly where it is
needed.

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

**The save leg was not, and now is.** `Document::collectFileBlobs` walked
properties and did `Base::freecad_dynamic_cast<PropertyFileIncluded>(prop)`,
so a blob held by any other property class was never collected and never
written -- silently, since nothing complains about content no entry was
written for. Done: `BlobReferrerProperty` gained
`collectBlobs(manager, object)`, both walks (App and the Gui view tier) test
for that interface instead, and an owner notes its own content so it keeps
control of the extension and the referrer name. `PropertyPartShape`
implements it as nothing on purpose -- it notes at write time, when the
writer's ASCII-or-binary choice is known -- and says so at the override.

**Blob creation is path-based**, not bytes-based: `insertFile(srcPath, ext)`,
`adoptFile(path, ext)`, `newBlobPath(ext)`, `hashFile(path)`. So a material is
serialized to `newBlobPath(".FCMat")` and then `adoptFile`d, which moves it
into the store under its content hash. No new manager API is required.

## 9. Invariants

1. The hash covers canonical model-and-value bytes only. Provenance never
   enters it (sec 4.3).
2. Model uuids are sorted before writing. An unsorted write is a silent
   sharing bug, not a cosmetic one (sec 4.2).
3. `saveInherited` is false for blob serialization, always (sec 4.1). In
   practice the canonical writer never calls it: it writes every value the
   card holds, and the `Inherits:` block from `_parentUuid` alone.
4. A parsed card reachable from a property is `const`. Editing allocates.
5. Restore never silently substitutes Default. Unresolved means a placeholder
   that keeps uuid and name (sec 7).
6. A document written at schema <= 4 carries upstream's uuid-only form and
   stays readable by upstream.
7. Everything `FileBlobsManager.md` sec 10 already requires of blob lifetime
   applies unchanged -- in particular, no handle is destroyed while the
   manager mutex is held.
8. Nothing installation-dependent enters the bytes: not a unit schema, not a
   locale, not the writing version, not the installed model or material
   libraries (sec 4.4, 4.5).

## 10. Build order

1. **Canonical writer plus hash.** DONE. `Material::saveCanonical` /
   `getCanonicalForm` / `getContentHash` (SHA-1 hex, the same hash
   `FileBlobManager` computes for a file of those bytes), exposed to Python as
   `Material.CanonicalForm` and `Material.ContentHash`. Guarded by
   `src/Mod/Material/materialtests/TestMaterialCanonical.py`: the hash is the
   SHA-1 of the form, model uuids come out sorted, a rename or a new author
   does not move it, an edited value does, and every unit schema in
   `listSchemas()` gives one hash. The QSet order fix (sec 4.2) also applies
   to the ordinary library writer, and to the tag list.

   Verified beyond the suite, 2026-08-19: all 215 shipped cards hashed in
   three separate processes give three identical lists, and 215 distinct
   hashes -- no collisions. Written to a file and read back through
   `MaterialLoader`, a card re-canonicalizes to the same hash, so the form is
   a fixed point and not merely deterministic; `sha1sum` of that file equals
   `ContentHash`, which is the agreement with `FileBlobManager::hashFile`
   step 3 rests on.
2. **Generalize the collect walk** (sec 8) so a non-`PropertyFileIncluded`
   property can own a blob. No behaviour change for existing documents. DONE.
   Regression evidence: `scripts/file-blob-verify.sh all` passes both legs
   (the view tier is the second walk), the `ShapeStorage` suite passes 34 of
   34, and `-t Document` fails exactly the nine long-standing names.
3. **`PropertyMaterial` on blobs**: handle plus `shared_ptr<const Material>`
   plus provenance; save/restore across all three restore cases of sec 7.
   DONE. One correction to sec 7's order: presets are checked *before* the
   blob is waited for, because a document that used a stock card does not
   carry it and a pending referrer for content the archive does not hold is
   never called back. The placeholder is therefore installed up front, so
   content that never arrives leaves the recorded uuid and name rather than
   an empty value.

   One case the design did not cover: **an unresolved value must survive a
   re-save.** The placeholder is a card like any other, so hashing and storing
   it would replace the document's reference to the real card with a reference
   to the note about it -- opening a file on the wrong machine would be what
   destroyed what it said. The property keeps the hash the document recorded
   and writes that back instead.
4. **Preset index** by content hash at startup; skip writing preset blobs.
   DONE, and it turned out to be a prerequisite for step 3 rather than a
   later optimization: `Part::Feature` assigns the Default card in its
   constructor, so without the index every Part object in every schema-5
   document stored a copy of it. The `ShapeStorage` suite caught it -- nine
   failures over archive entry counts, all of which went away when the index
   landed. The index is built on first use rather than at startup (47 ms for
   215 cards in a debug build) and dropped by `MaterialManager::refresh()`.
5. **Explicit sync commands** ("update from library", "save to library").
   DONE, sec 13. `PropertyMaterial::libraryStatus()` with the five states,
   `updateFromLibrary()`, `saveToLibrary()`, the same three as Python module
   functions, and `Material_UpdateFromLibrary` / `Material_SaveToLibrary` in
   the Tree and View context menus. Guarded by
   `materialtests/TestMaterialSync.py` (17 cases) and smoked under xvfb for
   the parts a suite cannot reach: command gating, the undo entry, and two
   objects sharing one card writing the library once.

   One thing the design did not say, found by testing it: **the library writer
   rounded values away.** It rendered every quantity through
   `Quantity::getUserString()`, so a card holding 7854.321 kg/m^3 was written
   as 7854.32 and read back as different content -- "save to library" could
   never converge, and every document holding that card would report a
   divergence nobody made. The writer now keeps the schema's unit and asks the
   parser how much precision reads the value back unchanged. This is sec 4.4's
   trap again, in the one writer sec 4 did not have to touch, and the reason
   `testTheLibraryWriterKeepsWhatTheHashMeasures` sits in the canonical
   suite.
6. Only then: consider whether appearance and texture properties fold onto
   the same mechanism (the general reading of decision 3).

## 11. How this will be judged

All six now hold, each with a case in
`src/Mod/Material/materialtests/TestMaterialBlobs.py`:

- A document assigned a custom material, opened on an installation without
  that card, reports the material by name with its physical values intact.
  This is the acceptance test; before this work it silently reported Default.
  `testCardOpensWhereItIsNotInstalled`.
- An upstream-written document with a uuid-only material still opens.
  `testUpstreamUuidOnlyDocumentStillOpens`.
- A document written at schema 4 still carries upstream's exact form.
  `testSchemaFourWritesUpstreamsForm` -- that it then opens *in* upstream is
  not testable here.
- N objects sharing one material hold one `Material` instance, not N.
  `testObjectsSharingACardShareOneInstance`, asserted through
  `Materials.cardCacheSize()`: twenty objects, one card.
- A document using only stock materials is not measurably larger than today.
  `testStockCardIsNotCarried`; measured, 200 boxes with the standard steel
  card give 3 archive entries and 9.5 KB, with no material blob at all.
- The same document saved twice produces byte-identical blob entries.
  `testSavingTwiceWritesTheSameBytes`.

And one the list did not have: opening a document whose card cannot be
resolved, and saving it there, leaves the reference the document recorded
intact (`testUnresolvedCardSurvivesAResave`).

## 12. Open questions

- ~~Should an *edited* preset (same uuid, different hash) keep the preset's
  uuid or mint a new one?~~ **Settled 2026-08-19 (user): keep the uuid, and
  let the hash carry the difference.** That is what sec 6's table assumes and
  what the code already does. The consequence step 5 has to honour: an edited
  preset stays pointed at the library card it came from, so "update from
  library" always has an anchor to pull from and "save to library" always has
  one to write back to -- which is the deliberate sync sec 2 says every other
  system requires. The two are still told apart by content, never by uuid.
- Is the placeholder card of sec 7 case 3 a real `Material` with empty models,
  or a distinct state the UI can render differently? Built as the first:
  `PropertyMaterial::isUnresolved()` is the distinct state, and no UI reads it
  yet. A distinct state is better for the user and more work.
- Cross-document dedup of material blobs is the same best-effort tier as
  `FileBlobsManager.md` sec 10 describes for files, and is not in scope here.

---

## 13. Explicit sync -- the two commands (step 5)

Sec 2's guarantee is that a library edit never reaches an old document. Its
cost is drift: the library moves on, the document does not, and nothing closes
the gap. Every system in that table pays the same cost and answers it the same
way -- a deliberate user action. Inventor has the Style Library Manager;
Onshape makes you re-apply the material. This is that action, in the two
directions a user can want it: pull the library's version into the document,
or push the document's version back to the library.

### 13.1 The five states, and where a command means something

The uuid is what makes this possible at all (sec 6): it says "this came from
library card X" and survives an edit, because an edited preset keeps the
preset's uuid and lets the hash carry the difference (sec 12, settled). So
compare what the property holds against what the library holds under that
uuid, and there are five answers.

| State | What it means | Update from library | Save to library |
| --- | --- | --- | --- |
| `NoCard` | nothing is assigned | -- | -- |
| `Unanchored` | a card with no uuid: it never came from a library | -- | needs a target |
| `Absent` | the uuid names no installed card | -- | needs a target |
| `Current` | the library holds exactly this content | nothing to do | nothing to do |
| `Diverged` | the library holds other content under this uuid | yes | yes, if writable |

`Diverged` is the second row of sec 6's table -- "someone edited their library"
-- and the only row where the two identities disagree in a way a user can act
on. The state is computed by content, never by uuid alone: a card the user
renamed, or one whose author changed, is still `Current`, because none of that
is in the hash (sec 4).

A card that could not be resolved at all (sec 7 case 3, `isUnresolved()`) is
not a sixth state. It is orthogonal: the recorded hash is still what the state
is computed from, so an unresolved card whose uuid the library does have reads
`Diverged`, and updating it from the library is the relink that makes the
object whole again. That is the strongest case for the command existing --
opening a document from someone else, finding a card missing, and having the
one action that fixes it.

### 13.2 Update from library

Takes the library's current card as the property's value. It is an ordinary
property assignment: it touches the object, goes through the transaction, and
undoes. Nothing else is special -- the new content is hashed, stored and shared
by the same path any assignment takes, and if the library card is a stock one
the document stops carrying content at all.

It refuses in every state but `Diverged`, and says so rather than silently
doing nothing.

### 13.3 Save to library

Writes the property's card over the library card of the same uuid, in place, at
the path that card already occupies. The uuid is kept (`overwrite=true`,
`saveAsCopy=false`), which is what makes the operation the inverse of 13.2
rather than a way to litter the library with near-duplicates.

Where there is no library card to write over -- `Unanchored` or `Absent` -- the
operation needs a target, which the App layer has no business inventing. It
fails there, and the GUI falls back to the existing save dialog
(`MatGui::MaterialSave`), which is already the way a card gets a library, a
folder and a filename. The dialog may mint a new uuid, so what comes back is
re-anchored onto the property: otherwise the document would still point at the
card it came from and the same question would be asked again next time.

The same fallback covers a case the state table does not, because it is not a
state of the card but of its library: **the stock cards live in `System`, which
is read only.** An edited preset is `Diverged` and can never be written back in
place, so in practice this is the common path, not the exception.

Overwriting a library card is the one destructive half of this feature: other
documents anchored to that uuid will read `Diverged` afterwards. That is
correct -- it is exactly what the library having changed means -- but it is why
the GUI confirms first, as the save dialog already does for its own overwrite.

### 13.4 What this deliberately does not do

- No automatic sync, at open or at any other time. That is the whole argument
  of sec 2 and the reason SolidWorks is the outlier.
- No cross-document sweep. The commands act on a selection, and a document is
  a selection of its objects.
- No notification beyond what restore already prints. Deciding whether a
  divergence deserves a tree decoration is a UI question, and a passive
  indicator is compatible with all of the above; it is simply not this step.

### 13.5 Where it lives

The primitive is on the property -- `libraryStatus()`, `updateFromLibrary()`,
`saveToLibrary()` -- because the property is what holds all three of the card,
the uuid and the hash. It is reachable from Python as module-level
`Materials.libraryStatus/updateFromLibrary/saveToLibrary(obj, property)`, which
is what the tests drive; properties have no Python object of their own, so the
object plus the property name is the address.

The GUI adds two commands next to `Std_SetMaterial`, and the workbench
manipulator puts them in the Tree and View context menus only when the current
selection has a material they apply to -- a command that is greyed out nine
times in ten is clutter in a menu that is already long.
