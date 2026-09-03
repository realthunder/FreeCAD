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

## 14. When the library moves: the stand-in, and what it does not change

Sec 5 leaves a stock card's content out of the document on the reasoning that
any installation holding the library can produce it again. That reasoning is
true only while the library does not move, and it moved: retuning the default
appearance changed what the `Default` card contains, so its content hash
changed with it, and every document saved before that names a hash no installed
card answers to. One IFC building (13642 objects sharing that one card) is the
case that made it visible.

Three things answer it, and they are deliberately separate:

- **The content is carried now.** `DocumentParams::SaveMaterialCards` defaults
  to true, so `storesContent()` writes stock cards too. It costs one copy per
  document however many objects share it, and it takes the whole class of
  failure off the table for anything saved from here on.
- **A hash miss falls back to the uuid.** Once the archive is drained and the
  blob was not in it -- the point at which "not read yet" becomes "not there"
  -- `blobUnavailable()` offers the referrer a stand-in, and `PropertyMaterial`
  answers with the library's card for the uuid sitting in the same XML element.
  The value stays *unresolved* on purpose: `contentHash()` keeps answering with
  the hash the file recorded, so a re-save writes the reference the document
  was saved with rather than whatever this installation happens to hold.
- **The warning names the blob once.** `dispatchPending()` groups by hash and
  reports how many properties wanted it and how many stood in, because one
  absent blob is one defect in the document however many properties name it.

**What a hash miss never was, measured:** it does not change what is drawn. The
drawn appearance is the view provider's own `ShapeAppearance`, restored from
`GuiDocument.xml` independently of the card, and restore assigns the card
without signalling (`assign()` takes no `aboutToSetValue`/`hasSetValue`, so
opening a document does not mark it modified), which is exactly what stops
`ViewProviderGeometryObject::updateData` from pulling the card's appearance
over it. Verified with one document opened twice, once with a resolvable hash
and once with the hash doctored to a value nothing answers: the app-side card
reads `Copper-230` both times, and the drawn appearance is identical both times
(diffuse `0.800 0.800 0.902`, shininess `0.3729`). What a miss costs is the
material's identity -- what the dialog reports, and what a re-save would write
-- which is what the stand-in restores.

**! The property must be the type it claims to be.** `Part::Feature`'s
`ShapeMaterial` is a `Materials::PropertyMaterial`, and the Materials module
registers its types in its own Python init. A session that never imports it --
any `FreeCADCmd` script -- left that type id at `badType`, so restoring
`type="Materials::PropertyMaterial"` was a type mismatch:
`PropertyContainer::Restore` hands a mismatch to `handleChangedPropertyType`,
whose default says nothing and does nothing. The card was dropped in silence,
the property kept the `Default` it was constructed with, and the next save
wrote that default over what the document said -- a headless re-save destroying
the assignment it was never told it had lost.

That hole is in the restore path, not in Materials. FreeCAD already loads the
module a saved type name points at: `Base::Type::importModule` cuts the prefix
off `Materials::PropertyMaterial` and imports `Materials`, and every other
restore path asks for it -- `Document::addObject` for object types,
`DynamicProperty::_addDynamicProperty` for dynamic properties, both through
`getTypeIfDerivedFrom(..., bLoadModule = true)`. Static properties were the
exception: `PropertyContainer::Restore` compares the saved type name against
the property's own type id as a string and never resolves it, so nothing asked
for the module. It now asks, and only when the property cannot name its own
type -- a genuine type change still goes to `handleChangedPropertyType` exactly
as before. The same hole applied to every property class owned by a module
other than its object's, `Mesh::PropertyMaterial` among them.

`Part::Box` needed the same line separately: it keeps a hand-written copy of
that loop for the ancient `l`/`w`/`h` migration, and a copy does not inherit a
fix. It is the only such copy in the tree -- `Part2DObject` and `Circle`
delegate to the base -- but it is worth knowing that a primitive can restore
by different code than every other object, because a test written around
`Part::Box` measures that code and not the common one. This one did, for an
hour.

## 15. Following the card: one flag, and the two panels stop looking alike

> Designed 2026-08-30 with the base-and-overrides storage of
> `ShapeAppearanceDesign.md` sec 12. **The flag (15.3, 15.4, 15.7), the
> storage under it and both panels (15.5) are built**, the context-menu
> form of Reset to material with them; the legacy mirror's status bit
> (15.6) is not.

### 15.1 What is wrong

The Material and Appearance context-menu entries both open a panel whose
centre is `MaterialTreeWidget` over the same library, filtered differently
and labelled "Materials" both times. Nothing says that one picks the card
(density, strength: what mass, FEM and CAM read) and the other a look.

Underneath, the card-to-look coupling is Inventor's rule -- assigning a
card seeds the look, a look the user set outranks it, a later card change
does not overwrite it -- but its state is a runtime member,
`ViewProviderGeometryObject::materialAppearance`, never saved. A reopened
document compares against a default-constructed value, so the look stops
following its card after the first save. There is no way to see whether a
look is the card's, and no way back to it.

### 15.2 Prior art

Every mechanical CAD that has both concepts ships two entries, and the ones
that get it right make the STATE visible:

| System | Physical | Look | Follow state | Way back |
| --- | --- | --- | --- | --- |
| Inventor | Material | Appearance | drop-down, default "As Material" | Clear Overrides |
| Fusion | Physical Material | Appearance | override listed "In This Design" | Remove Appearance Override |
| SolidWorks | Material | Appearance (stacked: face > feature > body > part > material's) | position in the stack | Remove Appearance |
| Creo | Material | Appearance Gallery | copied into the part | Master Appearance |
| Onshape, NX | Material (density only) | colour command | none: not linked | -- |

Inventor split the two properties in 2013 because users changed Material
to get a colour and corrupted mass properties; "As Material" is its answer
and is the one adopted here. Onshape and NX, which do not link them, carry
standing requests for a material-linked default colour.

No surveyed system treats an object's colour as an edit of its material.
All of Inventor, Fusion, Revit and SolidWorks do treat editing the card's
own appearance asset as a material edit, and so does sec 4 here: a changed
`AppearanceModels` section changes the content hash. That fires only from
the card editor, never from the Appearance panel.

### 15.3 The flag

`App::PropertyMaterialList` gains `FollowMaterial`, a bool in the list's
data block beside `pbr`:

- **true**: SETTING the object's card writes the card's look, read through
  `GeoFeature::getMaterialAppearance()`, into the BASE
  (`ShapeAppearanceDesign.md` sec 12.2) -- and the overriding faces, which
  are stored, are re-applied over the new base. Per-face overrides and
  following are therefore compatible: an imported part with three painted
  faces can be assigned Aluminium and keep its three faces.
- **false**: setting a new card changes nothing visible.

**The flag gates the moment a card is SET, and nothing else.** Not restore:
a restore is the file's own record landing, and what the file says this
object looks like -- its appearance, and the `Render_*` properties beside
it -- is what it gets. So a user may change any appearance, save, and have
it come back as it was. `applyMaterialAppearance()` stands itself down
while `App::Document::isAnyRestoring()`, which covers all three of its
doors (`attach`, the card's `updateData`, and the panel actions replayed by
a load), and `finishRestoring` no longer re-takes the card at all.

Default: true for a fresh object that carries a card, false for one that
does not (there is nothing to follow). Any whole-object write from the
Appearance panel, the property editor or Python sets it false; choosing
"As material" sets it true and re-derives. A per-face write leaves it
alone, because a per-face write does not touch the base.

`materialAppearance` and the equality heuristic around it go. The card's
`Render_*` properties (glass) follow the same flag: applied while
following, kept while not.

### 15.4 Migration

A restored list without the flag gets it derived once, in
`finishRestoring`, when both the card and the appearance are in hand: base
equal to the card's look (or to the default while the card has a look) ->
true; anything else -> false. A document without a card restores false.
The derivation decides what a later card set does; it never touches the
restored look, so guessing it wrong costs nothing until the user assigns a
card, at which point they have said which look they want.
Schema 4 cannot state the flag, so a schema-4 save followed by a reopen
runs the same derivation; that is lossless in every case but a look the
user set to exactly the card's, which reads as following, and is the
answer the old heuristic gave too.

### 15.4a What the code does with the base while following

One deviation from 15.3, and it is a deliberate one: the base **is** stored
while following, not left empty and re-derived from nothing. Storing it is
what lets a restore be authoritative: a document whose card library is not
installed, or whose card has been deleted, opens looking like itself
instead of default grey, and nothing has to re-derive a look at load time.
It costs nothing -- the base is the storage.

A card edited on disk between sessions therefore reaches a following object
at the next card set rather than at the next open. That is the price of the
rule above, and it is the right way round: a look the user saved outranks a
card that moved while the document was closed.

The flag DEFAULTS TO TRUE, which is what "a fresh object that carries a
card follows it" means: nobody has chosen this look yet, so the card may.
Defaulting it in the storage rather than raising it in the view provider's
constructor is what keeps the class default -- and with it the elision of
an untouched appearance -- intact. A whole-LIST assignment does not end the
follow either: an import states one look per face and says nothing about
which card the object wears, and it is exactly the imported part with three
painted faces that has to be able to take Aluminium and keep them.

`App::MaterialList::followMaterial()` is the one base write that does not
end the follow; every other one calls `endFollow()`. The flag rides an XML
attribute beside `pbr`, and in the stream form a flags byte inside the base
run rather than a bit of the sixteen-bit field mask -- the run is present
for anything the flag could be about, and the mask has no bits to spare.

The view provider is where the card is read: `applyMaterialAppearance()`
takes it (when following, or when the base has never been touched, which is
how a fresh object with a card starts following one), and
`deriveFollowMaterial()` runs the 15.4 derivation once at
`finishRestoring` -- the flag only, deciding what the NEXT card set does,
never the look. The old runtime member
`ViewProviderGeometryObject::materialAppearance` is gone.

### 15.5 The panels

**Appearance** (`Std_SetAppearance`), when the selection carries a
`Materials::PropertyMaterial`:

1. A material picker at the top, the same widget and filter as the
   Material panel, editing that property.
2. Below it a list labelled Appearance whose first entry is **As
   material**, followed by the appearance cards (`requireAppearance`; no
   "All materials" tab, so physical cards and hatch patterns never appear
   in a look picker). Selecting a card writes the base and clears the flag;
   As material sets it.
3. The colour, transparency, finish and per-face controls as today. Any of
   them clears the flag. The per-face control writes overrides.
4. A status line -- "Appearance: as material Steel", "Custom", "Custom, 3
   faces painted" -- and **Reset to material** beside it, which is also a
   context-menu command shown only while it applies (the sync commands'
   rule, sec 13.5).

Without a card, the panel is what it is today minus the "All materials"
tab, and the list reads Appearance rather than Materials.

**Material** (`Std_SetMaterial`) stays the lean panel, with a preview of
the card's look and one line stating what assigning will do: apply the
look, or keep the custom one (with the same Reset).

**What was built.** The Appearance panel's own list is a LOOK picker: the
"All materials" tab is gone, so physical cards and hatch patterns never
appear in it, and the group reads Appearance rather than Material.
Selecting one of its cards writes the BASE -- so the painted faces survive
it -- and ends the follow, because a look chosen here outranks the card's.
Under it sits the status line and **Reset to material**, shown only while
it applies. The card picker is a group of its own above, the Material
panel's widget and filter, and it is HIDDEN unless the selection carries a
`Materials::PropertyMaterial`: an object without a card sees the panel it
always saw, one row shorter.

**What was built next (2026-09-01), once the panel was looked at.** The
three leftovers are in, and looking at the panel found a fourth thing
worth fixing.

- **"As material" is the look list's first row**, above the libraries and
  above Favorites and Recent both. It is not a card: it carries the
  sentinel `as-material` where a card carries its UUID, and choosing it
  emits `leadingEntrySelected()` rather than `materialSelected()`. The
  mechanism is `MaterialTreeWidget::setLeadingEntry()`, so any list that
  needs a default answer of its own can have one.
- **The list's selection now says what the status line says.** It was set
  from the look's `uuid`, and since `App::Material::operator==` treats a
  shared uuid as identity (the card is the identity, the colours are a
  rendering of it), a look that followed its card and one that had been
  hand-edited both named the same card: the picker read "Steel" over a
  status line reading "Custom". While the look follows, the selection is
  the top row instead.
- **The status line moves on its own.** It was refreshed only when the
  panel was opened, so a colour set while it was on screen left "As
  material Steel" standing over a look that no longer followed anything. A
  `ShapeAppearance` change now refreshes the whole group.
- **A programmatic selection is no longer reported as a person's choice.**
  `MaterialTreeWidget::setMaterial()` blocks the selection model while it
  selects. Showing what the object currently looks like could otherwise be
  answered by that look being written straight back at it -- which is a
  loop, and the refresh above would have closed it.
- **Reset to material has three doors now**: the button, that row, and
  `Material_ResetAppearance` in the Tree and View context menus, offered
  only while it applies -- the sync commands' rule of 13.5. The act itself
  moved to one place,
  `ViewProviderGeometryObject::resetAppearanceToMaterial()`, with
  `canResetAppearanceToMaterial()` as the question all three ask.
- **The Material panel states what assigning will do**: the card's
  rendered icon, and one line -- "Assigning also applies this card's
  appearance." while the object follows its card, "The appearance was set
  by hand and is kept as it is." once it does not -- with the same Reset
  beside it, on the same shown-only-while-it-applies rule.

### 15.6 The legacy mirror

The view provider's `App::PropertyMaterial ShapeMaterial` is a
compatibility mirror of `ShapeAppearance[0]` that shares its name with the
card property on the object. It gets a new status bit,
`App::Property::Legacy` (bit 19), stays Hidden, and its doc string says
what it is and what to read instead. The property editor draws a Legacy row
in red italic when "Show all" is on, so a reader who finds it knows not to
build on it. `ShapeColor` and `Transparency` are mirrors too, but they are
the names every script uses and stay as they are.

**What was built.** `App::Property::Legacy` is bit 19, set on the view
provider's `ShapeMaterial` beside the Hidden it already carried, and its
doc string now says what it is and what to read instead. The property
editor draws a Legacy row in red italic, which is what a reader sees when
"Show all" turns it up; the bit is spelled `"Legacy"` to
`setPropertyStatus`, like every other one. `ShapeColor` and `Transparency`
are mirrors too and stay as they are: they are the names every script uses.

### 15.7 Python

`vp.ShapeAppearance.FollowMaterial`, read-write, on the same live view that
carries `PBR`, `Base` and `Overrides`. Setting it true re-derives the base
from the card at once; setting it false stores the current base.

### 15.8 What this deliberately does not do

- No per-field overrides (colour followed, gloss custom). An override is
  the whole base, as in every surveyed system; a face is the unit below
  that.
- No merged panel. The assignment panels stay two, as everywhere else; the
  card EDITOR is where the two halves share a window, and it already does.
- No change to what makes a card `Diverged` (sec 13.1).

## 16. A set of files: the multi-file referrer (2026-09-02)

The question that opened this section was asked of the MaterialX card
(`CyclesIntegration.md` sec 6.13): a document names its image maps by
filename, so what makes a `.FCStd` holding one self-contained? Answering
it turned out to answer a larger one about cards themselves, and the two
share a mechanism.

### 16.1 What "shared data" is, in three layers

They were being conflated, which is what made the earlier bundle proposal
feel heavy:

1. **The bytes** -- the images, and the document itself. Content-addressed,
   immutable, shared by whoever refers to them. `App::FileBlobManager`
   already owns this layer; it needs nothing.
2. **The reference** -- which blobs a thing needs, and under what NAME the
   referring content asks for one. Only a property can hold this, because
   a property is what makes `collectBlobs()` run and the bytes get an
   archive entry. This is the layer that was missing.
3. **The derived artifact** -- the parsed document, the generated shader,
   the shared Coin node. Runtime dedup, already placed in
   `ViewProviderAppearance` (sec 6.13 decision 1a). Not storage, and it
   must not be given any.

The blob manager already states the rule layer 2 needs: "Names belong to
the referring property, which persists its own file name and original
path, so any number of properties can share one blob while each keeps the
name the user gave it" (`FileBlobManager.h`).

### 16.2 `App::PropertyFileIncludedList` (built)

A named set of files. `PropertyFileIncluded` holds one; this holds any
number, each under the name its referring content uses to ask for one. An
entry is `{name, original, hash, blob}`:

- **name** is the key -- what the document calls the file. Unique within
  the property, and the thing a consumer joins on.
- **original** is provenance ONLY. Nothing resolves against it: an
  absolute path that resolves on the authoring machine points at nothing
  on the next one, which is the defect the type exists to close.
- **hash** is known from the moment the entry exists, including for an
  entry restored from a document whose bytes have not arrived yet.
- **blob** is the content, null while a restore is pending.

`filePath(name)` is the resolver, and it answers a REAL path in the
transient directory -- so every consumer downstream goes on opening
files, and neither the raster path nor the path tracer has to learn what
a blob is. A name that is absent and a name whose content did not arrive
both answer empty, because a consumer must treat them alike.

Three things worth not re-deriving:

- **The restore leg needed no extension.** `_pending` is a
  `vector<pair<hash, referrer*>>`, so one property may queue several
  hashes, and `assignRestoredBlob()` tells them apart by `FileBlob::hash()`
  on the handle it is given. Every entry naming that hash takes it: one
  file may be known under two names.
- **The referrer is per entry**, not per property, or every map of a
  material would land under one name. The archive shows
  `blobs/obj.Files.brass_color.png` -- object, property, entry,
  extension -- which is what an unpacked project should say.
- **There is no pre-store spelling**, so `blobContentNeedsStore()` answers
  true whenever there is anything to write and the save is offered the
  schema its content needs rather than dropping it silently.

`setValue()` exists only because the `ADD_PROPERTY` macros initialize by
calling it; the default value of a set of files is the empty set.

### 16.3 Why a card is one flattened file, and what else it could be

The card is stored as a single blob holding `getCanonicalForm()`, and
that came from four places, none of which requires it:

1. The stored unit mirrors the LIBRARY unit -- a `.FCMat` is one file, so
   a stored card is byte-for-byte a library card.
2. Identity is a SHA1 over one byte string, and the whole sync machinery
   of sec 13 is a comparison of that one hash.
3. Blob creation is path-based (`insertFile`/`adoptFile`), so "write one
   file, adopt it" needed no new manager API.
4. The flattening of sec 4.1 is deliberate -- but it is about the
   INHERITANCE chain, never about attachments.

The alternative is a **tree**: the canonical form names its files by
content hash instead of embedding them, and the card hashes over that
manifest. Identity stays one hash and still covers the content
transitively -- change an image, its hash changes, the manifest changes,
the card hash changes. Git's tree object, USD, OCI images and glTF with
external buffers all resolve it this way, and MaterialX shipping a
document beside an `Images/` folder is the same shape one level down.

What the tree buys: dedup of a shared map across cards and across routes;
raw bytes instead of the base64 an `Image` value writes into the YAML
(`MaterialValue.cpp`), which costs a third extra and can never share; and
an edit to one map rewriting one blob instead of the whole card.

It also dissolves the strongest concrete argument against putting a
MaterialX bundle in `App::Material` -- that a card-assigned object would
store the document twice, once inside the card's blob and once as the
bundle's, the two byte strings differing so the hashes differ. Under a
tree the document is a child blob named by its own hash and the two
routes land on one entry. The remaining arguments against the bundle are
judgment, not arithmetic: the copy weight on a value copied per face, and
a node graph not being a look.

### 16.4 The flattened form stays first class (the user's ruling, 2026-09-02)

Not a legacy or export-only path. A self-contained `.FCMat` is wanted for
**copy and paste of a material between objects**, which is a planned
feature and for which one flattened byte string is exactly the right
clipboard payload. So the two forms both stand: the tree is the
in-document form, the flattened card is the interchange form, and there
is a flatten on the way out and a split on the way in. That second
serialization path is the real cost of the tree, and it is paid for by
something other than export.

### 16.5 Order of work (the user's ruling, 2026-09-02)

The multi-file referrer is built FIRST, in App, so the card and the
shader program share one mechanism rather than the shader program getting
a private one and the card discovering it needs the twin later. Then
`App::ShaderProgram` gains one: its look IS its document, so it wants no
colour slots and no `App::Material` -- its knobs are already the `Param_*`
dynamic properties and its sources already blob-backed strings, and a set
of files is the only thing it lacks.

### 16.6 What `App::ShaderProgram` does with it (built 2026-09-02)

`Images` -- one `PropertyFileIncludedList` -- and two hooks in the view
provider, which is the side that can read a document.

**Keeping it in step.** `syncDocumentImages()` runs where
`syncDocumentInterface()` already does, on a source change, and follows
the same rule: a name the document refers to is stored, a name it no
longer refers to is dropped, and a name already held is LEFT ALONE. That
last one is the whole point -- its bytes are the stored ones, and
re-reading them off this machine's disk would undo the travelling on the
machine that has them. It does not run while a document is being read:
what the property holds then is what the archive gave it, and importing
over that would touch the document just for being opened.

**The key is what the document SAYS.** `Render::MaterialX::imageReferences()`
answers with the value plus its inherited `fileprefix` chain and nothing
else -- no search path, so it is a function of the text and the same
string on every machine. That is what makes it usable as the key of a
stored set of files; a resolved absolute path is not, being an answer
about one machine's disk. MaterialX's own examples are written in exactly
the shape that needs this: `standard_surface_brass_tiled.mtlx` states
`fileprefix="../../../Images/"` on the document and `brass_color.jpg` on
the input, and neither half alone is what it means by the file.

**Handing it over.** `Render::MaterialX::substituteImages()` gives back
the document with each carried reference replaced by the path its stored
file is at, and `syncShaderNodes()` pushes THAT into the Coin node. The
`fileprefix` attributes are dropped with the names they qualified, or
whoever resolves the result next prepends the prefix to an absolute path.
Nothing below the node changes: the capture, the generator and the path
tracer go on opening files, and none of them learns what a blob is. The
substitution is memoized on the text plus the stored files, because a
binding rebuild syncs every clone of a program and only those two inputs
change the answer.

**Read the carried document, not the raw one.** `validateDocument()`
stores first, then substitutes, then inspects. Inspecting the raw text
instead makes a document opened on a machine that never had the
originals report every travelled image as missing -- which it did, until
this order was fixed.

**What this does NOT close.** A document authored inline with RELATIVE
image names has no source URI to resolve them against, so there is
nothing to store and nothing travels. The spelling that works is a
document whose names resolve when it is set: absolute names inline, or
the file route, which resolves them against the file. Closing the
relative-inline case is the import step sec 6.13 of
`CyclesIntegration.md` already calls for -- read the file, flatten its
filenames, store the result inline -- and that belongs with the "Edit
shader" button, not here.

## 17. The MaterialX card: what was settled (2026-09-02)

**Terminology (ruled 2026-09-02).** The thing a card carries is a **shader
graph** -- everywhere a user can see it, in the model's field names and in
this document. "MaterialX" names the format and appears in tooltips and
descriptions, the way "Blender" and "Unity" say "shader graph" and reserve
the format's name for the fine print. "Document" is the FreeCAD document
from here on; the MaterialX API's own `mx::Document`, and the
`App::MaterialXDocument` manifest type that wraps its identity, keep their
names because that is what they wrap. The model field is
`MaterialXShaderGraph` and the canonical block's key is `ShaderGraph:`; the
loader still reads the `Document:` key those files were first written with.

The card itself is designed in `CyclesIntegration.md` sec 6.13. This
section records what a later discussion settled around it: the STORAGE
shape, the rename set that has to go ahead of it, the audit that priced
both, and the order to build them in. Section 16 is what was built
before it, and this continues from it.

**Status (2026-09-02, later the same day): steps 1 to 4 of 17.10 are
built** -- `8b704052c7` (`App::MaterialAppearance`), `84b9b1d258`
(`App::AppearanceList`), `1477c97f57` (`App::PropertyAppearanceList`,
with the frozen `<MaterialList>` element) and `6dad71e400`
(`App::ShaderBinding` / `Gui::ViewProviderShaderBinding`). Each was
verified the way 17.10 asks: ctest 463/463 and the showcase round trip
at schema 5 and 4. Step 5, `App::FileSet` (17.3), is built too
(same commit as this note): `PropertyFileIncludedList` delegates to it, its test file
passes unmodified, and a pasted entry still waiting for content now
re-queues for it instead of staying a hash a save would write without
the bytes. Steps 6 and 7 are built as well, with one deviation from 17.4
worth knowing: the appearance value does NOT hold a `FileSet` or a pointer
to one. It holds ONE string, `MaterialAppearance::materialx`, the content
hash of a MANIFEST -- `App::MaterialXDocument`, a small text naming the
document entry and every file by its stated name and content hash, stored
as a blob itself. That is the tree object of 16.3, and it makes the field
a string column exactly like `uuid`: the list holds the manifest blob and
follows it to its children for noting, pruning and restore, the stream
form spends bit 15 on the escape (17.9) with a self-contained run that
carries the base value ahead of the column, and the XML form has a
self-describing `m` key. Identity is over the hashes because the manifest
is (17.6). The card side is the "Shader Graph Rendering" appearance model
(`MaterialXRendering.yml`): `MaterialXDocument`, `MaterialXNames` and the
library-form `MaterialXFiles` under `materialx/` at the library root; the
canonical form writes a `MaterialX:` block of names and hashes and skips
the paths, the loader hashes a library card's files at load and reads the
block back for a stored card, and `Materials::PropertyMaterial` puts the
files and the manifest into the store on assignment and re-requests them
on restore. Step 8 is built (2026-09-02, the commit after this note): a
manifest hash on the base appearance becomes ONE shared `SoShaderProgram`
per document and hash, kept in a registry beside the binding machinery
(`ViewProviderShaderBinding::acquireMaterialXNode` / `releaseMaterialXNode`,
refcounted), built from the stored blobs -- the document text with its
image names rewritten to the blobs' paths on this machine (16.6) -- and
inserted at the head of the wearing object's root where a Scope=Object
binding puts its node, so the capture callback routes it into that
object's cache. `ViewProviderGeometryObject::updateMaterialXNode()` runs
first in `updateRenderMaterial()`, which both an appearance edit and the
post-restore pass reach; the node is null while any blob is still on its
way, and the post-restore pass asks again. Verified with a GUI probe: two
boxes wearing one card share one node (same node id), a third box wears
none, switching a box to Steel takes its node out and leaves the other's,
save and reopen brings it back, and the raster path translated the
probe's `standard_surface` with an `image` node to OpenPBR and drew it.
Step 9 (per-face column, Edit Shader Graph) is not started.

The precedence step 8 left open is RULED and BUILT: when an object wears
a card AND is the target of a Scope=Object binding, the EXPLICIT BINDING
WINS -- the same "outer shader wins" rule the cache already applies
across link levels. Both nodes go at the head of the same view provider
root and the cache's `setUserShader` keeps the LAST material-stage
program traversed, so the winner is simply the one at the higher child
index. That made the answer depend on assignment order: the card
assigned after the binding showed the binding, the binding created after
the card showed the card, and a restore landed wherever its order landed
(measured: the restore order is binding-then-card, so the card won).

One inserter now knows the other, and only one had to. The card node
keeps going in at index 0 (`ViewProviderGeometryObject::
updateMaterialXNode`), which puts it IN FRONT of a binding node already
there -- the losing end, on purpose.
`ViewProviderShaderBinding::applyDirectBindings` is the side that moved:
it asks the target for `getMaterialXNode()` (a new public accessor on
`ViewProviderGeometryObject`) and, when it finds that node in the root,
inserts behind it instead of at 0.

Verified by a probe that classifies every `SoShaderProgram` child of the
root by its fragment source -- the card's is the MaterialX shader graph, the
binding's a marked GLSL fragment -- and asserts the binding sits at the
higher index. Ten assertions across both assignment orders and a save +
reopen, all passing; with the insert index put back to 0 the same probe
fails card-first and reopen, which is what says it measures the thing.

Two more restore doors turned up while doing step 4, both now closed in
`6dad71e400` and worth knowing about for any future rename of an OBJECT
or view provider type: the shared-defaults block was keyed by the type
name the file states and looked up by the live name, so after a rename
its elided defaults were silently not pasted; and `Gui::Document`
compared the saved view provider name against the object's default as
strings, warning and recreating once per object. Renaming an object type
also means FOUR aliases, not two: the `...Python` variants are
registered types that appear in documents. A pre-existing crash on
every document close (a QPointer member released twice) was found by the
round-trip probe and fixed separately in `bae44f6134`.

### 17.1 The names

The user ruled this set. It closes the confusion recorded in
`material-name-collisions`: three classes called Material meaning three
different things, and a property family that had already been renamed
around the value it holds.

| Now | Becomes | Why |
| --- | --- | --- |
| `App::Material` | `App::MaterialAppearance` | It is a look, and every accessor already calls it that: `GeoFeature::getMaterialAppearance()`, `Materials::Material::getMaterialAppearance()`, `ViewProviderGeometryObject::applyMaterialAppearance()`. |
| `App::MaterialList` | `App::AppearanceList` | Follows the value. |
| `App::PropertyMaterialList` | `App::PropertyAppearanceList` | Pairs with `App::PropertyAppearance` and with the already-renamed `PropertyAppearanceListItem`. |
| `App::Appearance` | `App::ShaderBinding` | It is a `LinkGroup` that binds shader programs to targets, not a look. Joins `App::Shader` and `App::ShaderProgram` by name. |
| `Gui::ViewProviderAppearance` | `Gui::ViewProviderShaderBinding` | Follows its object. |

For the record, because it was asked and the answer was not obvious:
`App::Appearance` had NOT been renamed to anything before this.
`git log --all -S"AppearanceBinder"` finds nothing and the string appears
nowhere in the tree. The rename pass of 2026-09-02 was three commits and
touched only the property side: `4ae36071e6` (the legacy-name mechanism),
`adcbc3d3c4` (`App::PropertyMaterial` -> `App::PropertyAppearance`) and
`cb39c7c61d` (`Gui::PropertyShapeMaterial` -> `PropertyShapeAppearance`
plus the two editor items).

Scale, measured: `App::Material` 275 hits in 41 files, `MaterialList`
292 in 16, `PropertyMaterialList` 436 in 28, `App::Appearance` 32 in 10,
`ViewProviderAppearance` 40 in 6.

### 17.2 Three format couplings, not one

A renamed type reaches the file format by three separate doors, and only
the first was closed in 2026-09-02.

**The `type=` attribute.** `Base::Type::addLegacyName` (`4ae36071e6`).
Needed by `PropertyMaterialList`, `App::Appearance` and
`Gui::ViewProviderAppearance`, all three of which appear as type names in
`data/examples/render/materialx-showcase.FCStd`. Not needed by
`App::Material` or `App::MaterialList`: they are plain value classes with
no type registration at all.

**The XML ELEMENT name.** `PropertyLists::xmlName()` (`App/Property.cpp:527`)
derives the element from the LIVE type name: it takes
`getTypeId().getName()`, strips the namespace and a leading `Property`.
`Save` writes that element and `Restore` searches for it. So renaming
`App::PropertyMaterialList` would write `<AppearanceList>` and then look
for `<AppearanceList>` in every file that says `<MaterialList>` -- and
the property would restore EMPTY, with one console line to show for it.
Verified against a real document: `materialx-showcase.FCStd` holds
`<MaterialList count="1" follow="1" fields="1">` under
`type="App::PropertyMaterialList"`. **The rename must override
`xmlName()` to return the frozen `"MaterialList"`**, which is the same
decision `adcbc3d3c4` made when it froze the `<PropertyMaterial>` element.
`addLegacyName` does not help here: it resolves type names, not elements.

**The Python-visible name.** `App.Material` is public API and in use --
Draft (`view_layer`, `utils`, `gui_setstyle`, `layer`), BIM
(`ArchReference`, `ArchWindow`, `ArchBuildingPart`, `ifc_tools`,
`importDAE`, `importSH3DHelper`), the Material tests and fork scripts --
so `MaterialPy.xml` moves its `Twin`/`TwinPointer` to the new class and
PINS `PythonName` to `App.Material`. The C++ class renames; the Python
type does not. `App.MaterialList` is fork-only; keep the name and expose
`App.AppearanceList` as an alias rather than break fork macros.

### 17.3 `App::FileSet`, the value behind a named set of files

`App::PropertyFileIncludedList` (sec 16.2) holds its `Entry` list and all
of the blob logic inside the property. Three carriers now want that same
value -- `App::ShaderProgram::Images`, a material card, and an
appearance -- and only one of them is a property, so the value comes out
into `src/App/FileSet.{h,cpp}`:

- `Entry {name, original, hash, blob}`, `find`, `filePath`, `hashes`.
- Mutators that take a `FileBlobManager&` EXPLICITLY. A value has no
  container to find the owning document from, and defaulting to the
  process-wide store would silently strand content outside the document
  that is about to save it.
- `save` / `restore` with a caller-chosen element name, `referrerFor`,
  `collectBlobs`, `assignRestoredBlob`, `holdsEveryNamedBlob`,
  `getMemSize`, `operator==` on name and hash (never provenance).

The property keeps what only a property can do: `blobManager()`, the
`_pendingManager` queue, `awaitBlob`/`cancelPending`, the
`aboutToSetValue`/`hasSetValue` pair, and the Python dict conversion. A
`using Entry = FileSet::Entry;` keeps the eight call sites in
`ViewProviderShaderObject.cpp` compiling untouched.

It also removes a real duplication: the referrer stem/extension naming is
written twice today, once in `collectBlobs` and again in `Save`.

The element name stays `"FileIncludedList"`, so the output is
byte-identical, and **the acceptance criterion is that
`tests/src/App/PropertyFileIncludedList.cpp` passes unmodified**.

### 17.4 The payload IS a set of files

The first cut of this design had the MaterialX payload as document TEXT
plus images, which raised a question about what to do with a card's
base64 image entries at assignment time. The user's correction removes
the question: **the card carries the same `FileSet`, and the document is
one of its entries.**

So `App::MaterialXDocument` is a thin thing over `App::FileSet` -- the
set, which entry is the document, and the memoized parse/generation that
sec 6.13's node registry keys on. Assignment copies HANDLES, not bytes;
both the card property and the appearance are blob referrers naming the
same hashes; `FileBlobManager` holds one copy per document however many
objects wear the card. Nothing is base64 anywhere along that path, and
nothing is decoded at assignment or at draw.

### 17.5 The three forms

- **In a library, on disk:** a `materialx/` sub-directory at the LIBRARY
  root (not per card directory, so cards under `Appearance/` and
  `Standard/` share one copy of a map), holding the document and its maps
  under DESCRIPTIVE names. No hashes on disk: this is what a card author
  edits, diffs and version-controls. This would be the shipped library's
  first sidecar convention -- no card names a relative file today.
- **In a document:** the card names its files by CONTENT HASH and the
  bytes are blobs in the document's store, shared with the appearance's
  `FileSet`. Nothing inlined, nothing stored twice.
- **On the clipboard, and for export:** the flattened `.FCMat` with
  everything base64 inside it, generated by inlining from those blobs at
  copy time and split back into blobs on paste. This form stays first
  class (sec 16.4): it is the only one that can cross a document or an
  application boundary, and copy/paste of a material between objects is
  a planned feature.

### 17.6 Identity is computed over the hashes

The canonical form -- what a card's content hash is computed over -- must
be the hash-naming form, not the library spelling. Compute it over the
library spelling and editing `brushed_aluminium.mtlx` in place leaves the
card's hash unchanged, so "Update from library" and the
diverged/current comparison of sec 13 both go blind to a look that
actually changed. Over the hashes, a changed map produces a changed card
hash transitively.

The consequence is deliberate: **the on-disk library file is not
canonical.** Loading resolves the descriptive names and hashes the bytes.
That is the same shape MaterialX's own `fileprefix` flattening already
has -- what is written for humans is not what identity is computed over.

### 17.7 Everything is carried into the document

Already decided and already implemented for cards, and the reasoning
transfers to a MaterialX shader graph and its maps unchanged.

`411da2d10d` ("Material: carry the stock cards too, because the library
moves"): stock cards used to be left out, since the hash said which card
it was and any installation with the same library could produce the
content again. That holds only while a shipped library is a fixed point,
and it is not one -- `7f5a3b7d49` retuned the default appearance, which
changed the Default card's content and so its hash, and every document
written before it named a hash no installed card answered to. A hash miss
does not fall back to the uuid, so the material was lost outright: the
King building lost it on 13642 objects and said so 13642 times.

`DocumentParams::SaveMaterialCards` defaults true and
`PropertyMaterial::storesContent()` returns true for every resolved card.
What makes that affordable is the per-document dedup rather than the
omission: `ensureBlob()` looks the content hash up in the store first, so
those 13642 objects add one 670-byte entry between them, and re-importing
King grew the file from 102.3MB to 102.4MB.

So: **a MaterialX shader graph and its maps are carried into the document on
assignment, always, deduped by content.** A look that silently disappears
when someone retunes a shipped card is the failure this argument was
written from.

### 17.8 Where it rides, and why

On the appearance value (`App::MaterialAppearance`), not in a property of
its own. The deciding fact is that the bridge already exists and is
BY VALUE: `GeoFeature::getMaterialAppearance()` returns one
(`GeoFeature.h:198`), and `ViewProviderGeometryObject::applyMaterialAppearance()`
writes it with `ShapeAppearance.followMaterial(card)`
(`ViewProviderGeometryObject.cpp:1319-1332`). A card's document reaches
the view provider through plumbing that is already there, per-face
MaterialX falls out of the palette column the texture field already uses,
and the follow rule of sec 15 needs no new rule.

A dedicated property instead would need a FOURTH `GeoFeature` virtual
plus mirrors of `applyMaterialAppearance`, `canResetAppearanceToMaterial`,
`resetAppearanceToMaterial` and `deriveFollowMaterial` -- four code paths
that would have to stay in step with the follow rule forever.

Two costs that were checked and are not costs:

- The render side reads `ShapeAppearance` PER FIELD -- `getMetallic(i)`,
  `getRoughness(i)`, `getImagePath(i)`, `getFinishes()` -- and never
  composes a whole material per face. A shared value on the appearance
  costs a pointer per STORED material and a refcount per value copy, not
  one per face read. There are no `sizeof` or `memcpy` layout assumptions
  on the class anywhere in App or Gui.
- The shape is not novel: `Materials::PropertyMaterial` already holds
  `std::shared_ptr<const Material>` (`Mod/Material/App/PropertyMaterial.h:254`).

`App::ShaderProgram` keeps the dedicated property it was given in
`581001f441` -- its look IS its document, so it wants no colour slots --
and it carries the same type, so there is one serialization
implementation either way. That is what putting `save`/`restore` on the
value buys.

### 17.9 What the audit found, before anything is written

- **`operator==` gates the whole card path.** `applyMaterialAppearance()`
  returns early on `card == App::Material()`. A card stating ONLY a
  document and no colours would compare equal to the default and be
  silently ignored. The field must be in `operator==`.
- **`MaterialList::setBase` compares and copies field by field in THREE
  places** -- the early-out compare, the write, and `setMaterialType`'s
  save/restore (`MaterialList.cpp:1049-1094`). Miss one and the write is
  silently elided or the value is dropped on a type change. This is the
  fork's known elided-no-op-write failure mode.
- **The per-field stream form has one bit left.** `FieldFollow = 1<<14`
  is the last used bit of a `uint16_t` mask. Better to spend bit 15 on an
  ESCAPE -- an extension mask in its own run, extended runs flat after
  it -- than on this one field. It is safe because `RestoreDocFile` gives
  the property its own archive file, so trailing bytes an older build
  never reads are harmless.
- **`RunBase` cannot be extended.** Its payload is read positionally with
  no length bound (`PropertyStandard.cpp:4822-4845`); the run head's byte
  length is used only by `skipRun` for runs the reader does not know. A
  new build reading an old file would read past the end of that run. The
  new field therefore needs a SELF-CONTAINED run carrying the base value,
  the palette and the index -- which also serves the uniform case, where
  the base is written as a one-entry palette exactly as the texture field
  already does.
- **`App::PropertyAppearance`, the single-value property, saves only
  colours and the finish** -- not the texture, and so not this either.
  The list property is the storage of record; leaving the single one
  consistent with `texture` is the smaller surprise.
- **A layering constraint decides where the import step lives.**
  `Render::MaterialX::imageReferences()` and `substituteImages()` are in
  `libFreeCADRenderer` (`Gui/Renderer/MaterialXSupport.h:216,232`), which
  the Materials module cannot call. So the card READ path must be
  parse-free: the entry names are baked by the authoring/import step,
  which is Gui-side, and `App::MaterialXDocument` holds files and never
  parses anything.

### 17.10 Build order

1. **`App::Material` -> `App::MaterialAppearance`.** No type registration
   to alias; `MaterialPy.xml` moves its twin and pins `PythonName`;
   `Material.{h,cpp}` become `MaterialAppearance.{h,cpp}` and 41 files
   follow the include.
2. **`App::MaterialList` -> `App::AppearanceList`**, Python name kept
   with an alias for the new one.
3. **`App::PropertyMaterialList` -> `App::PropertyAppearanceList`**, with
   `addLegacyName` AND the frozen `xmlName()` of 17.2. The test file
   renames with it.
4. **`App::Appearance` -> `App::ShaderBinding`** and
   `Gui::ViewProviderAppearance` -> `Gui::ViewProviderShaderBinding`,
   both with `addLegacyName`. The type-init ORDER must not move: the
   shader family registers after `Link`/`LinkGroup` in
   `App/Application.cpp` and after `ViewProviderLink` in
   `Gui/Application.cpp`, or startup asserts on `badType`. Update the
   bundled `src/Ext/freecad/rendereffects/__init__.py`, which names the
   type as a string, and the two scripts that do.
5. **`App::FileSet`** extracted, `PropertyFileIncludedList` delegating.
6. **`App::MaterialXDocument`** over the set, plus the stream-form escape
   of 17.9.
7. **The field on the appearance value** -- `operator==`, all three
   `setBase` lists, the XML key, `getMemSize`, the Python dict -- then
   the card side: the `materialx/` directory convention, the read in
   `Materials::Material::getMaterialAppearance()`, and one more filter in
   `DlgDisplayPropertiesImp::setupFilters()`.
8. **Gui**: the shared node registry with copy-on-write, beside
   `applyDirectBindings()` and `rebuildAllBindings()` (sec 6.13
   decision 1a).
9. **Per-face column**, and the "Edit Shader Graph" button with the Gui-side
   read-flatten-store import that bakes the entry names and closes the
   relative-inline case of sec 16.6.

Steps 1 to 4 are mechanical and independent of the rest; step 5 is
independent of all of them and is load-bearing for three carriers, so it
may be pulled forward.

**What to verify after each of 1 to 4**: `ctest` (463/463 today) and the
Python suite; then the round trip that matters -- open
`data/examples/render/materialx-showcase.FCStd` (18 shader bindings, 18
appearance lists), confirm the looks survive, re-save at schema 5 AND at
schema 4, since `Writer::typeName` writes the former name at 4, and
reopen both. Grep the string literals as well as the symbols: `.py`,
`.csv`, `.ui` and `.xml` name these types as text, and those failures are
silent.

### 17.11 Step 9: the per-face column and the Edit Shader Graph button

Step 9 is the last of 17.10 and the only one still open. Researched
2026-09-02; the terrain below is measured from the code, the rulings it
asks for are NOT made.

**The storage is already finished, which changes the shape of the
work.** A document set per face is not a thing to be added: it is
stored, saved and restored today. `AppearanceList` carries `materialx`
as a sparse override column beside `uuid`, with the full accessor set
(`getMaterialXs`, `getMaterialX(idx)`, `setMaterialX(idx, ...)`,
`variesInMaterialX`, `hasMaterialX`), `PropertyAppearanceList` forwards
all of it, `materialXHashes()` walks the base AND the overrides so a
save notes every manifest a face names, and two tests cover the round
trip in both the XML and the stream encoding. What is missing is
entirely on the CONSUMER side: nothing outside `App` calls
`getMaterialXs()` at all. `ViewProviderGeometryObject::
updateMaterialXNode()` reads `getBase().materialx` and builds ONE shared
program node for the whole object.

Until step 9 lands that gap now says so. A per-face column that no
renderer draws is a stored value the picture disagrees with, so
`updateMaterialXNode()` warns once, on the edit that starts the
variation, instead of dropping it in silence.

**Why the three existing palettes do not answer this.** Finish, frame
and face texture are all done the same way: a palette on the
`SoFCRenderMaterial` node, a per-face index array beside it, the index
packed into the material vertex stream, and one shader that reads the
index and branches on DATA. The face texture is the closest relative and
the most instructive: it sidesteps the problem by making the variation a
sampler LAYER in a 2D array rather than a program, and it samples
outside the divergent branch because a texture read in divergent control
flow has no defined derivatives. None of that generalises to a document,
because a MaterialX shader graph is not data the mesh shader can index. It
IS the shader.

**The raster path binds one program per draw.** A material-stage user
shader is stamped on a render cache, merged down into child caches with
the outer one winning, carried as a single pointer on the draw's
material, and swapped in once per submit. Program identity is also part
of the batch key. There is nothing per-triangle anywhere on this path.

**Cycles already does the per-triangle part, and would still need
work.** Its draw translation builds a shader VARIANT for every finish,
frame and layer combination the draw's triangles actually name, memoizes
the packing to a slot, and writes a per-triangle slot array that is
exactly Cycles' own material-slot mechanism. But a MaterialX user shader
SHORT-CIRCUITS that: one document replaces the whole surface for the
draw, before the variant loop runs. So even the backend that has slots
is handed one document per draw today.

**Three ways to close it, and the recommendation.**

1. **Split the draw by slot.** Group the triangles of a shape by the
   document their face names and submit one draw per group, each with
   its own program. This is the direct analogue of what Cycles does, and
   it needs no shader change and no new vertex attribute: the slot is
   known on the CPU from the part index. The cost is draw count, bounded
   by the number of DISTINCT documents on the object, which is small for
   the same reason the other palettes are capped. The batch key already
   treats a different program as a different batch, so this works with
   the grain of the renderer rather than against it.
2. **One merged program that branches.** Generate a fragment program
   holding every document's material function and switch on the slot.
   This keeps one draw, and it is the option the derivative constraint
   argues hardest against: every document's image reads would sit in
   divergent control flow. It also multiplies compile time and register
   pressure by the palette size for a case that is rare.
3. **Refuse it.** Keep one document per object, and make a varying
   column an error rather than a warning.

Option 1 is the recommendation. It matches the engine's existing
batching, it reuses the packing Cycles already proved, and it leaves the
mesh shader alone.

**What it touches**, in order: a palette of documents built beside the
face texture palette in the view provider; a per-face index published on
`SoFCRenderMaterial` the way `faceTextureIndices` is; the reserved
fourth byte of the material vertex stream, which exists for exactly this
kind of use and is written as zero today; a shader palette on the render
cache's material beside the single `usershader` pointer; and the draw
split in the backend. On the Cycles side, feeding the per-face documents
into the variant loop instead of short-circuiting ahead of it.

**The Edit Shader Graph button is the smaller half and is nearly specified.**
The bundled render-effects module is the precedent for the whole shape:
it instantiates `App::ShaderProgram` objects plus an `App::Shader` and
binds them with an `App::ShaderBinding`, and it has a deactivate that
takes the binding and removes the objects it made. Materializing a card
is the same three objects seeded from the card's document instead of
from an effect package, and un-materializing is that deactivate. The
precedence rule built earlier today is what makes the result behave:
the materialized binding beats the card the object still wears, so the
object does not have to stop wearing it for the edit to show.

**The relative-inline case closes for free, in one ordering.** Section
16.6 leaves open a document authored inline with relative image names,
which has no source URI to resolve them against. The import does not
need to rewrite the text: the stored set is keyed by what the document
SAYS, and `imageReferences()` already resolves relative names when it is
given the file's path. So the import reads the file, stores each image
under its stated name with the FILE as the anchor, and only THEN sets
the text inline. The sync that runs on that source change finds every
name already carried and leaves it alone, which is the rule it was
written with. Store first, set second: the other order stores nothing.

**Ruled 2026-09-02**, all four, and the build order that follows:

- **Option 1 is the design, and it is NOT built yet.** The storage
  round-trips, the consumer gap warns instead of dropping, and nothing
  outside App reads the column: that is a stable, honest state. The
  draw split across five layers plus the Cycles variant loop waits for
  a real per-face shader graph to ask for it. Option 3 is rejected: it
  throws away finished storage to save a warning.
- **The command is "Edit Shader Graph...", in the appearance panel beside the
  look list**, shown only while the selected object wears a card that
  carries a MaterialX shader graph (the 13.5 rule). "Materialize" stays an
  internal word. No tree context-menu entry in the first cut.
- **Reversible, and un-materializing needs no card logic.** The object
  never stops wearing the card while materialized (the binding merely
  wins), so removing the three objects makes the card's look reappear
  by construction. Leaving the object bare would mean also clearing the
  card, which is a separate action that already exists. Un-materialize
  confirms first when the program text differs from the card's shader graph,
  because it discards edits; writing edits back to the card is a later
  feature, not part of step 9.
- **The shader graph is one more field of the card's appearance value and
  the follow rule applies unchanged.** A card SET on a following object
  brings whatever the card carries, shader graph included; a library edit
  to the card's shader graph pushes nothing, exactly as a library edit to its
  colour pushes nothing. One card, one follow rule. Re-setting the card
  on a materialized object changes nothing visible, because the binding
  still wins.

So step 9 as built is the Edit Shader Graph button alone, with the
store-first/set-second import ordering that closes 16.6.

### 17.12 Making a shader card, and moving one between objects (ruled 2026-09-02)

Three rulings, then the terrain, then what gets built.

**Ruled:** (a) the library carries **MaterialX shader graphs only**. The
storage is shader-agnostic (a FileSet on the appearance value would carry a
second model with a language field just as well), but a raw GLSL/bgfx program
is one backend's, and MaterialX is the one form both the raster path and
Cycles compile. Raw programs keep the home they have, `App::ShaderProgram`
plus an `App::ShaderBinding` in the document. (b) Copy and paste of a
material rides the **system clipboard under a mime type of its own**, not an
in-process static: paste across documents and across FreeCAD instances then
comes for free, and a target that lacks the blobs stores them the way an
import does. (c) **Bindings are left out of the first copy/paste cut.** A
binding is a document object with a target list; copying one means adding a
target in the same document and cloning it across documents, and that wants
a ruling of its own.

**What exists.** Making a card is already there: the Materials editor has
New, Inherit (a child card that remembers its parent's UUID and, on save,
writes only the models and properties that differ from it --
`Material::saveModels`, `saveInherited`) and Save into a user library through
`MaterialSave`. On the object side `Material_SaveToLibrary` (13.5) takes the
card an object wears into a library. So "clone a card and change it" is a
supported flow today.

**What is missing, measured.** A shader card is a card with the Shader Graph
Rendering model and three fields the editor presents raw: a string that must
equal one of the names, a list that must match what the graph SAYS its files
are called, and a file list of paths under the library's `materialx/`. Three
invariants a user would have to know by heart. And the library save does not
keep its side of the bargain: `MaterialLibraryLocal::saveMaterial` writes the
YAML and nothing else, while `resolveMaterialXFiles` accepts an absolute path
where a library-relative one belongs. A hand-authored shader card therefore
works on the box it was made on and dangles everywhere else.

**What gets built, in order, each its own commit:**

1. **The card-side picker.** Adding the Shader Graph Rendering model in the
   editor (or pressing "Use shader graph..." beside its fields) asks for a
   `.mtlx`. The editor reads it (`App::MaterialXDocument::readFile`,
   `imageReferences()` resolved against the file's path), fills the graph
   name and the names list from what the file says, and records where each
   file is. The user types nothing. The library save then does what it
   never did: copies the set into `<library>/materialx/<card>/` and writes
   the file list relative to it, on the App side
   (`MaterialLibraryLocal::saveMaterial`) so the Python route gets it too.
   Names stay the graph's own; only the paths move.
2. **The Edit Shader Graph button** (17.11), which materializes the card's
   graph on the object for editing.
3. **Save to Library from a materialized object.** `Material_SaveToLibrary`
   already exists; when the object's winning binding carries a program that
   differs from the card's graph, the save writes the edited text back as a
   new inherited card (the card it wore is the parent). This is the return
   leg that makes "assign a shader" an in-model workflow rather than a
   library chore: Inherit, pick a graph, Edit, Save.
4. **Copy Material / Paste Material.** Two commands in the Tree and View
   context menus under the 13.5 rule: Copy shown only while the selection
   carries a card or a look, Paste only while the clipboard holds one. What
   an object HAS is three layers -- the card (`ShapeMaterial`: uuid, content,
   file set), the look (`ShapeAppearance`: base, per-face overrides, the
   follow flag) and its bindings -- and the payload carries the first two:
   the card's canonical YAML plus its blobs, and the appearance list as the
   document serializes it. Paste sets the card (under `FollowMaterial` that
   pulls the look by itself), then, if the source was Custom, applies the
   custom base and the per-face overrides. A multi-selection pastes onto
   every object; a FACE sub-selection pastes as a per-face override, which
   the palette already carries.

**Built 2026-09-02:** items 1 to 3 (`7d53670386`, `17b2921449` and the
save-back commit after it), with `TestShaderGraph.py` driving the
primitives headless. The primitive behind item 3 is
`ShaderGraph::cardFromEdit`, exposed as `Materials.shaderGraphCard`; one
trap it met: the program's text is already a blob in the store (the
property is a `PropertyStringIncluded`), held in memory with no file
behind its path until the document is saved, so the text is written to
the document's transient directory instead and placed from there. Item 4
followed the same day: `Materials::Clipboard::{pack, apply}` under the
mime type `application/x-freecad-material`, a line-oriented container of
the card's canonical YAML, the look's XML (written with force-XML at the
current schema, read back through a property lent the target's
container) and the bytes of every file either names by hash;
`Material_Copy` / `Material_Paste` in the context menus under the 13.5
rule, and `Materials.packMaterial` / `applyMaterial` for scripts and
`TestMaterialClipboard.py`. Its trap: a store keeps a blob only while a
handle holds it, so the handles of the files a paste inserts must live
until the card and the look have taken hold of theirs.

### 17.13 Next: a graph with many surfaces, and the look that assigns them (ruled 2026-09-02)

MaterialX's chess set is one document with fifteen `surfacematerial`s,
forty-three images and a `<look>` assigning each material to a mesh of
`chess_set.glb` by name. Today both engines take the FIRST surface shader
of the FIRST material node (`openPbrSurface()`, `Interpreter::run()`) and
nothing reads a look, so every object wearing that card is a black
bishop. The images are not the problem: the raster cap counts the layers
of the CHOSEN shader's graph (four per piece), and Cycles has no cap.

Two workstreams, ruled in this order:

1. **A surface name on the graph** -- BUILT, below.
2. **A look-reading importer, in the Import module** -- BUILT, below.
   Beside `ReaderGltf.cpp` in `src/Mod/Import/App` with its Gui half in
   `ImportGui`. A glb (or other mesh bundle) imported beside a `.mtlx`
   that carries a `<look>` gets a card per `materialassign`, and each
   object made from a named mesh wears the card the look assigns to that
   name.

#### The surface name, as built

One string, carried the whole way down and named the same at every
layer. The word is deliberately not "material": that is the card.

| Layer | Where it rides |
| --- | --- |
| Card | `MaterialXSurface`, shown as "Surface" (MaterialXRendering.yml) |
| Canonical form | `MaterialX: Surface:`, written only when named |
| Manifest | a `surface <name>` line (`App::MaterialXDocument::surface`) |
| Object | `App::ShaderProgram::Surface` |
| Scene graph | `SoShaderObject::sourceSurface` (Coin fork) |
| Capture | `Render::UserShader::surface`, snapshot v72 |
| Consumers | the third argument of `inspect()`, `generate()`, `buildMaterialXSurface()` |

**Empty means the first surface the graph states**, which is what a
single-material graph has, and both the manifest line and the canonical
key are written only when a name is there. So every card, document and
snapshot written before this existed hashes to what it hashed before and
renders what it rendered before. Nothing migrated.

**Identity is where the fifteen cards come from.** The surface is part
of the manifest, so it is part of the manifest hash, so it is part of
the card's content hash. The chess set is fifteen cards over ONE shared
set of files -- the graph and its forty-three images are stored once and
the cards differ in this one string. The renderers key on it too: the
raster variant cache and the path tracer's shader map both fold it into
the document's identity, so two surfaces of one document are two
programs and not one program drawn twice.

**A name resolves however it was written down.** `surfaceIndex()` tries
the `surfacematerial` node's namepath first -- what a `<look>`'s
`material=` attribute says, and what the picker offers -- then its bare
name, then the shader node's own two spellings. A name the document does
not state is refused with the list of the ones it does, rather than
silently rendering the first.

**The selection survives the OpenPBR translation** because it is kept as
an INDEX into `surfaceShaders()`, not as a node: the translation
replaces every shader node in the document, and document order is what
comes through it. Both consumers now read one implementation of "which
surface, translated" (`Render::MaterialX::openPbrSurface`); the path
tracer's own copy of it is gone.

**How it was verified.** `MaterialXGen_tests_run` generates all fifteen
chess-set materials off the vendored `.mtlx`, each with at most five
image layers and more than five distinct shader sources -- fifteen
materials, not one drawn fifteen times. `TestShaderGraph` covers the
card, the manifest identity and the materialized program. The hop no
unit test can see -- the field added to the Coin fork -- is
`fcad-probes/surface_run.sh`: two cards over one graph become two
`sourceSurface` values in the live scene graph.

**What still costs the whole document**: the capture decodes every image
the document names, not just the chosen surface's
(`DocumentInfo::images` walks the whole tree). The generated shader
samples only the chosen graph's layers -- the two lists are joined on the
resolved path -- so the picture and the sixteen-layer cap are right; the
decode is a superset, cached per path, and paid once per document rather
than once per card.

#### The look, as built (2026-09-03)

Importing `chess_set.glb` and then `standard_surface_chess_set.mtlx` onto
it gives fifteen objects, each wearing its own surface of ONE shader
graph. Three pieces, split by what each is allowed to know:

| Piece | Where | What it answers |
| --- | --- | --- |
| `Render::MaterialX::looks()` | the renderer, beside `imageReferences()` | what the document SAYS: its looks, and each assignment's material and geometry |
| `Import::ReaderLook` | `src/Mod/Import/App`, beside `ReaderGltf.cpp` | the FreeCAD half: which object answers to a name, what card it comes to wear |
| `ImportGui::readLook()` | `src/Mod/Import/Gui` | the glue: read the file, ask the renderer, hand it over as plain data |

**A `.mtlx` is an importable file type** (`Import/Init.py`) that makes no
objects. It dresses the ones the document already has, and importing it
into an empty document is refused rather than answered with a new one --
which is what a look IS: a statement about geometry that is elsewhere.
The other route is a sidecar: importing `asset.glb` reads `asset.mtlx`
beside it, if there is one, onto exactly the objects that import made.
Exactly that name; a directory's other documents are not this file's
materials.

**The App half takes data, not a library.** MaterialX is carried by the
renderer and the App tier does not link it (sec 17.9), which is why the
Gui half exists at all: it is fifty lines that read the file and call
`looks()` and `imageReferences()`. `Import::ReaderLook` names no MaterialX
type, so the matching and the card building are testable without it -- and
a console `Import.insert()` of a `.mtlx` says the format is not supported
rather than half-doing it.

**Matching is by name, and the name is the object's Label.** A geometry
string is a list of MaterialX geometry paths, so each is tried whole and
then by its last element (`/root/Bishop_B` answers to `Bishop_B`), with
`*` and `?` matching as a glob and the whole string having to match --
`Pawn_*` is the pieces whose names begin that way, not the ones that
contain it. The internal name is tried after the label, for a document
whose labels were since edited. The first assignment that answers wins,
which is the order the look states them in, and assignments that matched
nothing are reported with a count rather than passed over.

**Fifteen cards, one file set.** Each assignment becomes one card whose
`MaterialXSurface` is what the look's `material=` says -- copied straight
through, because `surfaceIndex()` resolves a `surfacematerial` namepath
first (item 1). The names and files are the same list for all of them, so
the document's blob store keeps ONE copy of the graph and one of each of
the forty-three images, and the cards differ in one string. That is the
shape item 1 was built for, arriving from a real asset rather than from a
test.

**Two things had to be fixed under it before there was a picture.**

1. **Rebuilding a glTF mesh as B-Rep is now a parameter, and off.**
   `ReaderGltf` rebuilt B-Rep geometry from the facets of every mesh --
   unconditionally upstream, and here for every mesh whose glTF material
   was untextured, which the chess set's is, since its images are named
   by the MaterialX document and not by the glb. The rebuild goes through
   points and facets, so the UVs the look shades through are destroyed by
   it, and on 1.5M triangles the sew did not finish in **fifteen
   minutes** (measured; killed at the timeout). Without it the same
   import takes **0.2 seconds**. It is not the chess set's size that does
   it either: `shaderball.glb`, 88 thousand triangles, imports in **0.18
   seconds** as read and did not finish rebuilding in fifteen minutes
   either -- and that is upstream's path with upstream's settings, the
   same facets-to-B-Rep pass followed by `sewShape()` and
   `removeSplitter()`.

   glTF is a mesh format, so the rebuild is a translation with a price on
   both sides: it drops the UVs and the authored normals, and it buys a
   shape with edges and vertices, which is what CAD work selects and
   dimensions. That is a choice, not a default, so `GltfRebuildBRep`
   (Mod/Import) states it: **None (0), the default** -- every mesh
   arrives as read; **Auto (1)** -- rebuilt only where nothing needs what
   the rebuild would drop, which asks the material for a texture and then
   the triangulation for UV nodes; **All (2)** -- always, which is
   upstream FreeCAD's behaviour.
2. **A MaterialX graph needs the unit-0 stand-in.** The shapes generate
   texture coordinates -- and the render cache captures them -- only while
   a texture UNIT IS ENABLED, which is why a bump-only or per-face-imaged
   object already plants a 1x1 white `SoTexture2`. A document's image
   nodes read `texcoord`, which is the mesh's own coordinates in both
   backends (the generated raster code samples `v_texcoord0`, the path
   tracer emits a texture coordinate node over `ATTR_STD_UV`), and with
   no unit enabled there were none: every map came out as its corner
   texel, which is the whole chess set as flat grey paint. An appearance
   stating a MaterialX document now asks for the stand-in like the other
   two (`ViewProviderGeometryObject::updateRenderTexture`).

**How it was verified.** `fcad-probes/look_run.sh` is the deliverable and
the check: it imports the real `chess_set.glb`, imports the real
`standard_surface_chess_set.mtlx` onto it, asserts that fifteen objects
wear fifteen different surfaces of one shader graph, and takes a picture
with each backend. `Import_tests_run` (new suite) covers what a geometry
string means -- the list, the path tail, the globs, and that a name never
answers to a name it merely contains. ctest 473/473, `FreeCADCmd -t 0`
2659 OK.

**One difference between the pictures, measured and left open.** The
raster picture is the asset as authored: dark board, black pieces, green
felt. The path tracer draws the same materials -- the same maps, the same
felt, the same checker -- about five times brighter and with much less
contrast. Not the look, and not MaterialX: a PLAIN 0.18 grey box is
already 1.8x brighter in the path tracer than in the raster path on this
scene, so the two engines' lighting does not agree to begin with and this
asset, which is mostly dark, shows it at its worst. Whether they should
agree, and which is right, is a question for the engines and not for the
importer.

**Settled against a renderer that is not ours (2026-09-03).** Blender
5.0.1 is installed at `~/works/sw/blender` and carries the Cycles we
embed (our tree is `v5.0.0-461-gfdd7227b8`), so it is the SAME path
tracer on a scene we did not build: a disagreement is our translation or
our scene setup, and an agreement puts the fault in the other backend.
`fcad-probes/blender_ref.py` renders it headless, `blender -b -P`, with
its own Python -- there is no cp312 `bpy` wheel and our conda Python is
3.12, so the wheel route named above was not the cheap one after all.

#### The scene whose answer is known

The measurement is not made on the chess set. It is made on the one
scene that needs no reference at all, and the reference is then used to
confirm it: a convex body of known albedo under a CONSTANT environment.

Under an environment of one radiance L in every direction, every point of
a convex body sees the hemisphere over its own normal, the irradiance is
`pi*L` whichever way that normal points, and a Lambertian surface leaves
`albedo*L` -- **with no shading at all, and no camera, orientation or
geometry in the answer**. With L = 1 and an authored grey of 0.5, whose
linear albedo is `srgbToLinear(0.5) = 0.2140`, the body must come out at
0.2140 linear, which encodes back to exactly 0.5: byte **128**. A picture
that is not flat, or not 128, is an engine's error, readable off the byte.

The environment is a Radiance `.hdr` of one value written by the probe
(`loadRadianceImage` reads a flat scanline; `PBREnvImage` takes the
path), so all three engines are lit by THE SAME FILE and nothing is
described twice.

Blender confirms the closed form outright: **128.0** flat for a Diffuse
BSDF, **133** for a Principled dielectric at roughness 1 -- that extra
being the dielectric specular a plain appearance also has.

#### What the three engines answer

A ladder in the environment's radiance, one grey box, no sun, no ambient
occlusion (`fcad-probes/envsweep_probe.py`, all values linear):

| L | raster | our Cycles |
| --- | --- | --- |
| 1.0 | 0.2874 | 0.2346 |
| 0.5 | 0.1714 | 0.1170 |
| 0.25 | 0.1144 | 0.0578 |
| 0.125 | 0.0865 | 0.0296 |
| 0.0 | **0.0578** | **0.0000** |

Both are straight lines in L, which is what a correct engine has to be.
Fitted:

| engine | slope | intercept |
| --- | --- | --- |
| raster | 0.2296 | **+0.0574** |
| our Cycles | 0.2346 | -0.0002 |
| Blender (Principled, r=1) | 0.2307 | -- |

**The environment response of both engines is right**: three slopes
within one per cent of each other, and our path tracer matches Blender to
within a byte in every cell of a roughness sweep as well (r = 1, 0.5,
0.1). What is wrong is the raster's INTERCEPT. With a black environment,
no sun and no lights, the raster still draws the box at byte 68 and the
path tracer draws black.

#### Two things the path tracer is never told

Taking the candidates away one at a time at radiance zero
(`fcad-probes/envzero_probe.py`) splits that intercept in two:

| what is left on | linear |
| --- | --- |
| headlight + scene ambient | 0.0578 |
| scene ambient only | 0.0075 |
| headlight only | 0.0503 |
| neither | 0.0000 |

1. **The viewer's headlight, 0.0503.** The raster shades with the view
   lights -- headlight, backlight, fill light, up to `MaxViewLights` of
   them (`fcViewLight` in `fc_mesh_lighting.sh`). `Cycles::SceneInput`
   carries one `LightConfig`, the single configurable scene light, and
   has no `ViewLightConfig` member at all: the headlight has no
   representation in the path tracer's scene and cannot be translated.
   (The flatness of the term is not evidence of an ambient, which is what
   it looked like at first: in an isometric view all three visible faces
   of a cube make the same angle with the view axis, so a headlight IS
   flat there.)
2. **The scene ambient, 0.0075.** Coin's `LIGHT_MODEL_AMBIENT`, 20 per
   cent grey by default, which the raster adds as a uniform-radiance
   environment beside the real one and deliberately outside
   `envIntensity` (`fc_mesh_lighting.sh`). The size is exactly right for
   what it is -- `decodeSRGB(0.2) * 0.2296 = 0.0076` measured 0.0075, so
   the decode is not the bug -- and the path tracer simply does not have
   it. `ambient` does not appear in `CyclesScene.cpp`.

#### And the one that reverses the sign

Neither of those explains the chess set, where the path tracer is the
BRIGHTER one. Walking the same grey box from the closed-form scene to a
stock viewport (`fcad-probes/envscene_probe.py`) finds the step that
does:

| leg | raster | our Cycles | ratio |
| --- | --- | --- | --- |
| constant environment | 0.2874 | 0.2346 | 0.82x |
| the built-in preset | 0.2346 | 0.1651 | 0.70x |
| ... and GTAO | 0.2346 | 0.1651 | 0.70x |
| ... and the sun with its shadow | 0.2789 | 0.1651 | 0.59x |
| **`Render_PBR` off (the default)** | **0.1274** | **0.1651** | **1.30x** |

**`Render_PBR` defaults to false, and the path tracer does not read it.**
The raster has two shading branches and only one of them is lit by the
environment: with the flag off it draws Blinn-Phong, lit by the headlight
and the ambient and nothing else. The Cycles translation consults
`pbr.enabled` in exactly ONE place in the whole of `CyclesScene.cpp` --
`background->set_transparent(!(pbr.enabled && pbr.envBackground))`, which
decides whether the environment is SEEN, not whether it LIGHTS. So
`translateWorld` builds the world from the environment either way and
`resolveSurface` builds a physical BSDF either way.

In a stock viewport the two engines are therefore not drawing the same
scene at all: **the raster is lit by a headlight and the path tracer by a
room.** That is the sign reversal, it is exactly the configuration the
chess set was pictured in, and on an asset that is mostly black -- where
the environment is nearly all of the light there is -- it is the whole of
the five times.

#### Ruled, and built (2026-09-03)

**A path tracer is reached for because it is physical.** Honouring a
raster shading facade would defeat the point of asking for it, so the
path tracer does not take orders from `Render_PBR` and never did -- what
was wrong was that this had never been stated, and one comment in
`View3DInventorViewer.cpp` claimed the opposite ("the still keeps
matching what the raster view honours"). That comment now states the
policy instead.

The policy, which the code already implemented for everything except the
three things below:

> The path tracer is given what the scene IS -- the environment,
> exposure and output transform, the scene light, document lights,
> bump, section planes, background. It is given nothing that describes
> how the RASTER pipeline fakes something: GTAO, cavity, matcap, bloom,
> volumetrics are absent from `Cycles::SceneInput` and stay absent.

Blender draws the same line: Solid mode's studio lights are
camera-attached and never appear in a render.

**And `Realistic` joins the path tracer's tier.** The raster's three
shading models are not one thing with settings, they are two tiers:

| model | lit by | may it ever show an unlit model? |
| --- | --- | --- |
| Classic | the headlight, the scene ambient, document lights | **never** -- that is what it is for |
| Matcap | its camera-fixed studio | **never** -- the studio IS the shading |
| Realistic | the environment, the scene light, document lights | yes, if the scene is dark |
| External (Cycles) | the same, traced | yes, if the scene is dark |

So the three findings resolve as:

1. **`Render_PBR`: not honoured, and now said so.** No code change
   beyond the comment.
2. **The headlight, backlight and fill light: not translated, and now
   not added to Realistic either.** They are camera-attached viewing
   aids -- `ViewLight::eyeSpace`, which the bridge already computes and
   already documents as "exactly what makes it a headlight". A
   document's own lights are the scene and are kept, in both tiers.
3. **The scene ambient: the same.** Coin's `LIGHT_MODEL_AMBIENT` is a
   Phong-era global fudge that belongs to Classic.

The change is `BGFXFrame.cpp`, at the point the view lights and the
ambient are transferred to the view: gated on `pbrActive` -- the same
flag the shader branches on, so a frame that asked for Realistic and
fell back because the environment could not be built keeps the lights it
is about to shade with -- eye-space lights are skipped and the ambient
is not fed.

**Measured, before and after,** on the radiance ladder (linear):

| | slope | intercept |
| --- | --- | --- |
| raster, before | 0.2296 | **+0.0574** |
| raster, after | 0.2273 | **-0.0000** |
| our Cycles | 0.2346 | -0.0002 |
| Blender (Principled, r=1) | 0.2307 | -- |

The floor is gone: with a black environment, no sun and no lights the
raster now draws byte **0** where it drew 68, and it tracks the path
tracer within one or two bytes at every radiance on the ladder.
`fcad-probes/shadingfloor_probe.py` is the assertion that the other two
models did NOT lose theirs -- Classic 99.7 and Matcap 108.4 on the same
black scene, Realistic 3.0 (the edges; the surface is 0).

**`Render_Shadow` is no longer read by the path tracer.** By its own
documentation it is "the shadow MAP cast by the Shadow display style's
scene light ... a convenience switch to drop shadows without leaving the
Shadow display style" -- a cost and technique knob, the same class as
GTAO. A path tracer has no shadow map; its shadow is what happens when a
shadow ray meets the model, so switching it off does not simplify the
picture, it makes the light pass through solid matter.
`SceneTranslator::translateLight` now always casts.

#### Not changed, and why

- **GTAO stays in Realistic.** It is not a fake to be removed but the
  raster's approximation of what the path tracer integrates exactly;
  dropping it would make Realistic less like Cycles, not more.
- **Cavity and bloom stay.** Stylistic, off by default, and a deliberate
  act to enable.
- **A "match my viewport" still**, if it is ever wanted, needs a knob of
  its own rather than being smuggled through `Render_PBR`. Named here,
  not built.

Items 2 to 4 of the earlier list (GTAO against path-traced GI, the
`standard_surface` to OpenPBR translation, the image colour spaces) are
NOT ruled out by any of this: the measurements above were made on a plain
appearance, so they say nothing about a MaterialX document. They are
simply no longer the first suspects -- the scene disagreed before any
document was involved, exactly as the plain-box 1.8x said it would. What
they should now be measured against is a Realistic view, which is the
tier that is supposed to agree.

### 17.14 The library keeps a file once per CARD, and that is now wrong (open, 2026-09-03)

Item 1 made fifteen cards over one shared set of files the normal shape
of an asset. The LIBRARY does not store them that way, and the gap is
measured, not suspected.

`MaterialLibraryLocal::saveMaterial` derives `cardDir` from the card's
own path with `.FCMat` stripped, and `Material::placeMaterialXFiles`
COPIES every one of the card's files into
`<library>/materialx/<cardDir>/`. One directory per card, a full copy in
each. The chess set's images are 16 MB; fifteen cards of it put **240 MB
in the library for one asset**, and editing one image means replacing it
fifteen times or having the cards disagree.

Nothing about the card format forces this. Identity is already computed
over the files' CONTENT hashes and the paths are explicitly not part of
it (sec 17.6), and a card carried INTO a document already shares
perfectly -- the blob store is content-addressed, so the fifteen cards
there hold one copy of each image and fifteen small manifests. The
library is the one tier that still stores by name and by owner.

**What next session has to decide** (the user's ask, 2026-09-03): the
library storage layout that lets one shader graph file be shared by many
cards. The obvious shape is to make the library's `materialx/` directory
content-addressed the way the document's blob store is, but that is a
guess and the alternatives are real -- a per-ASSET directory that several
cards name, a shared pool with reference counting, or leaving the file
layout alone and only teaching the save side not to re-copy bytes it
already placed. Whatever is picked has to answer:

- What happens to the libraries that already have per-card directories.
  Cards state their files relative to `materialx/`, so old cards must go
  on resolving.
- What deletes a file when the last card naming it goes away, and what
  happens when nothing does.
- Whether a human browsing the library still sees `bishop_black_base_color.jpg`
  or a hash. The current layout was chosen partly so an unpacked library
  reads like files, which is the same argument the blob store's
  `BlobReferrer` naming answers on the document side.
- Whether the answer is the same for the shipped libraries and the user's
  writable one.

#### The recommendation (2026-09-03, offered -- not ruled)

Three shapes are possible: a content-addressed pool mirroring the
document's blob store; a per-ASSET directory that several cards name; or
leaving the layout alone and only teaching the save side not to re-copy
bytes it already placed.

**The per-asset directory.** The unit that is actually shared is a
document and its images -- an asset -- and that unit already has a name
every card over it states: the `.mtlx` file's own base name, in
`MaterialXShaderGraph`. So `cardDir` becomes an asset directory, and
`placeMaterialXFiles` skips a file the destination already holds with the
same content hash. Fifteen chess cards then cost one copy, and the
fifteenth card is free rather than 16 MB.

The four questions of 17.14 answer themselves under it:

- **Old libraries** need no migration at all. A card states its files
  relative to `materialx/` and nothing rewrites a card that is not being
  saved, so a per-card directory written last week goes on resolving.
- **Deletion** never happens on card delete -- another card may name the
  same file. An explicit sweep over the library, which already loads every
  card, removes what nothing names. Explicit, not automatic.
- **Browsing** still shows `bishop_black_base_color.jpg`, under a
  directory named for the asset. That is the argument the current layout
  was chosen on, and it is exactly what a content-addressed pool would
  spend.
- **Shipped versus writable**: shipped libraries are read only and resolve
  by stated path, so the rule is only about the writable one.

The one thing it must get right is that two different assets can be called
`standard_surface_chess_set.mtlx`. Identity is over content hashes (17.6),
so placement compares: a destination file that exists with a DIFFERENT
hash is a different asset and the directory takes a suffix.

A pool is exact and needs no collision rule, but the extra sharing it buys
over this -- two unrelated assets holding the same image file -- is rare,
and it costs the legibility the library is for.

### 17.15 An sRGB image map reached the path tracer encoded twice (fixed, 2026-09-03)

Item 4 of 17.13's list, and the last thing left between the two engines
on a MaterialX document. Session 9 took the LIGHTING difference out of
the way, so the chess set's remaining disagreement -- Cycles brighter
AND much lower contrast than the raster -- had to be inside the MaterialX
path. Brightness and collapsed contrast together is what reading an sRGB
map as linear looks like, and it was.

**The scene answers in arithmetic, not by eye.** One box under a
CONSTANT environment of radiance 1, wearing an OpenPBR surface with
`specular_weight` 0, is flat at exactly the base colour. Two legs over
ONE document, differing only in how that colour is written:

- **flat** -- `base_color` stated as the constant 0.21586
- **imaged** -- `base_color` from a solid byte-128 PNG, `srgb_texture`

Byte 128 IS sRGB 0.502, and `srgbToLinear(0.502)` = 0.21586, so the two
legs are the same surface written two ways. The imaged/flat ratio then
names the defect with no reference renderer needed -- the control is
inside the same engine, which is what makes this cheaper than the
Blender leg the lighting arc needed:

| ratio | what it means | byte |
| --- | --- | --- |
| 1.00 | the decode is right | 128 |
| 2.32 | no decode at all (the sRGB byte used as linear) | 188 |
| 3.41 | decoded, then encoded again | 224 |

**Measured: 3.418, against 3.413 predicted.** Cycles drew the flat leg
at byte 128 -- the closed-form answer exactly -- and the imaged leg at
223. The raster drew both legs alike, so it was never the one at fault.

**The cause is one ustring.** `CyclesMaterialX.cpp` declared an
`srgb_texture` file `ccl::u_colorspace_srgb`. That is a space to CONVERT
FROM, and Cycles handles it inconsistently:
`ColorSpaceManager::to_scene_linear` deliberately leaves the pixels
sRGB-encoded for it (it forces `compress_as_srgb`, and with scene linear
at Rec.709 the processor is null, so the ONLY thing that runs is a
`color_linear_to_srgb` on data that was already sRGB), while
`ImageMetaData::finalize` never sets the `is_compressible_as_srgb` that
would put `NODE_IMAGE_COMPRESS_AS_SRGB` on the node and have the kernel
decode it. Encoded on the way in, never decoded on the way out.

The fix is the spelling the rest of the engine already uses, and which
`PixelImage` in `CyclesScene.cpp` documents at length for the
non-MaterialX textures: `u_colorspace_scene_linear_srgb`. The bytes stay
bytes and the kernel decodes per sample. The two differ only in
primaries -- `u_colorspace_srgb` is Rec.709 whatever scene linear is --
and scene linear IS Rec.709 here, because nothing sets an OpenColorIO
config and every other colour the engine is handed is already Rec.709.

`fcad-probes/mtlximage_run.sh` is the measurement. After the fix both
engines read 1.000.

**On the asset that reported it.** `fcad-probes/look_run.sh` renders the
chess set in both engines; over a 4x4 grid of the frame (mean byte):

| | raster | Cycles before | Cycles after |
| --- | --- | --- | --- |
| whole frame | 112.3 | 135.5 | **117.6** |
| darkest cell | 45.3 | 145.1 | **70.1** |
| range | 45..169 | 91..169 | **70..169** |

The board's dark cells were the whole complaint -- 45 in the raster and
145 under the path tracer, which is why one picture was the asset as
authored and the other pale grey-green throughout. Every edge cell now
agrees within a byte or two (91.2/90.8, 124.6/124.0, 136.6/135.6,
169.2/169.2).

What is left is in the DARK interior cells only, where Cycles still sits
20 to 25 bytes above the raster. That is the shape of bounce light the
raster only approximates, so it belongs to item 2 of 17.13 -- GTAO
against path-traced GI -- and not to colour management.

**Two traps the probe itself walked into**, worth keeping:

- **A document must state `colorspace="lin_rec709"` on its root**, as
  every authored one does (all 36 in the vendored examples). MaterialX's
  `DefaultColorManagementSystem` only inserts a decode when it knows the
  working space to decode TO, so a document without it makes the raster
  leg measure the probe rather than the engine. The first run "found" a
  raster defect that was the missing attribute.
- **The mean of a centred patch is not the body's colour.** The
  environment behind the box is white at radiance 1, and a patch that
  catches any of it reads low or high with no sign that it did. The mode
  of the frame with the background excluded is the answer, and the share
  it covers says whether the reading is worth anything.

**What is left, now measured rather than suspected.** On this scene the
raster draws 132-133 where the closed form and the path tracer both say
128 -- about 1.07x, reproducible across runs, with the two legs equal to
within a byte. That is a MaterialX-path brightness residual with no image
involved, and it is the next thing in this arc. (One run in four read 144:
a frame captured before it settled, not a third value.)

### 17.16 The environment picture was read mirrored (fixed, 2026-09-03)

Found while making the three-way picture that 17.15 called for. It is
not a MaterialX matter at all -- it moves every render this engine has
ever made from an image-based environment.

`Render::sampleEnvImage` mapped an equirectangular picture as
`u = 0.5 + atan2(d.y, d.x) / 2pi`. Cycles' kernel spells the same thing
`direction_to_equirectangular`, `u = 0.5 - atan2(d.y, d.x) / 2pi`, and
so does Blender and everything else that loads one of these files. So an
HDRI came in **horizontally mirrored**: the background faced the wrong
way, and so did every reflection in it.

**Why nothing caught it.** That function is the ONLY environment sampler
behind both engines -- the raster builds its cubemap through it
(`BGFXViewEnv.cpp`) and the path tracer bakes its equirect through it
(`bakeEnvironment`, which documents the Cycles layout it writes and then
feeds it directions that come back mirrored). Our two engines therefore
agreed with each other exactly while both disagreed with the world, and
no internal comparison could see it. The lighting arc could not have
seen it either: every measurement there was made under a CONSTANT
environment, which is mirror-invariant.

**How it was proved, before changing anything.** Blender was lit from a
visibly different direction on the same HDR with a matched camera.
Sweeping a Z rotation of Blender's environment could not fix that -- the
best of eight angles was a mean absolute error of 23.6 bytes, and the
curve was shallow, which is the signature of a mirror rather than an
offset. Mirroring Blender's environment gave **4.4 at rotation 0**,
against 45.9 unmirrored. So the difference was a flip, and the flip was
ours.

**After the fix** the environment is oriented the same way in all
three, which is what this section is about and all it establishes.

It does NOT establish that the three agree. A whole-frame mean absolute
error of 3.83 bytes was reported here first and it was a bad number:
the background is the same HDR drawn as a backdrop in every leg, it
fills most of the frame, and once it matches it swamps everything the
model does. Measured on the BOARD REGION instead -- per channel, mean
byte:

| | R | G | B | mean abs. diff vs Blender |
| --- | --- | --- | --- | --- |
| raster (bgfx) | 85.7 | 77.1 | 64.8 | **1.76** |
| our Cycles | 68.7 | 62.2 | 55.5 | **15.50** |
| Blender Cycles | 85.9 | 79.3 | 67.7 | -- |

**Our path tracer is the outlier, not the raster.** It draws the model
about 20 per cent darker than either of the other two, and relatively
bluer (R/B 1.238 against Blender's 1.269 and the raster's 1.323). The
raster and Blender are within two bytes a channel.

The sharp part of that: our Cycles matches Blender on the BACKGROUND --
a direct view of the environment -- while being far darker on the model,
which is the environment used as LIGHT. Same picture, two paths, only
one of them agrees. That is where 17.18 starts.

This is also the second time in one session that a mean over the wrong
region gave a confident wrong answer; the first is in 17.15. A frame
statistic is only an answer when the thing being measured is what fills
the frame.

`fcad-probes/chess3_run.sh` reports the camera and bounding box;
`blender_chess.py` reproduces the framing from them and takes a rotation
and a mirror flag for exactly this test; `chess3_sheet.py` lays the three
side by side.

**A trap worth keeping: an orthographic camera makes a path tracer draw
a flat background.** All its rays are parallel, so the environment is
sampled in one direction and the background is one colour. Blender's
first picture was uniform brown for that reason and nothing else. When
the background is part of what is being compared, use perspective.

### 17.17 What GTAO does and does not do against path-traced GI (measured, 2026-09-03)

Item 2 of 17.13's list. Measured, not closed: the answer is a gap in
what the raster models, not a defect to fix in an afternoon.

The scene is a TRENCH -- a floor between two parallel walls, one albedo
0.5 throughout, under a constant environment of radiance 1 -- looked at
straight down, so the CENTRE of the frame is the centre of the floor in
both engines whatever aspect ratio each chose. Wall height is the sweep.
The walls do two opposite things: they block the environment over part
of the floor's hemisphere, and they bounce light back into it. Cycles
does both; the raster's only occlusion is the screen-space AO term.

Floor brightness as a fraction of the open floor (`gi_run.sh`):

| | h=10 | h=20 | h=40 |
| --- | --- | --- | --- |
| Cycles | x0.80 | x0.58 | x0.36 |
| raster, AO off | x1.00 | x1.00 | x1.00 |
| raster, SSAO | x1.00 | x1.00 | x1.00 |
| raster, GTAO | x0.99 | x1.00 | x1.00 |

Both camera types, because screen-space AO reconstructs view-space
positions from depth and that is where the two projections differ: the
orthographic and perspective rows agree, so it is not that.

**The environment is not occluded by geometry at all.** The AO=off row
is flat at x1.00 -- a 40mm wall beside a 20mm trench changes the floor by
nothing -- so the whole of the raster's occlusion is the AO term, and the
AO term did not reach 10mm in this scene either.

**AO is not inert**, which is the part that keeps this honest: on the
chess set the AO buffer runs the full 0 to 255 (`CHESS_AO_BUFFER=1`
dumps it through `RenderDebug_ViewMode` 3). It is a contact-scale
darkening and it behaves like one. Why it read nothing at 10mm with
`AORadius` explicitly set to 20mm is a loose end of its own, and the
radius is where to start.

So the honest summary: **the raster has no medium-range occlusion of the
environment and no bounce at all**; AO adds contact darkening and is not
a stand-in for either. On the chess set that shows as the raster running
a little BRIGHTER than the path tracer (93.5 against 89.9 in 17.16),
which is the direction this predicts.

**Two traps this cost.** `AOMethod` defaults to 0 -- classic SSAO -- so a
run that just switches `AO` on is not testing GTAO; and `AORadius`
defaults to 0, meaning a fraction of the scene size. State both. And the
per-view `Render_*` properties are seeded from the preferences ONCE,
when the backend is selected, so an AO setting has to be in place
BEFORE the document exists.

#### A texture-upload race, seen once

While the chess set was rendered under CPU contention (the GI sweep was
running beside it), the raster drew several pieces a shaded red --
(155, 36, 40) -- that appears in NO texture the document names: every
base colour in `chess_set/` averages greenish-grey or cream. Run alone
the same probe draws 12 red pixels instead of 632, and those are the
axis cross. So a MaterialX draw can sample a texture layer that has not
finished uploading, and it shows under load. Not chased further; recorded
here because it will be hard to recognise the second time.

### 17.18 Next: our path tracer is the odd one out on the model (resolved in 17.19, 2026-09-03)

Ruled by the user, 2026-09-03: "our cycles renders a lot different than
blender's, in fact our raster gives a closer look (at least in colour
tone) to blender than our cycles. Chase all the problems in next
session."

The measurement is in 17.16: on the board region our Cycles is about 20
per cent darker than both the raster and Blender's Cycles, which agree
with each other to under two bytes a channel.

**The lead, and it is a sharp one.** The BACKGROUND agrees between our
Cycles and Blender -- that is the environment viewed directly. The MODEL
does not -- that is the same environment used as light. So the
suspicion is that the environment reaches the shading path at a
different strength (or through a different decode) than it reaches the
background path, inside our translation. `bakeEnvironment` and the world
shader it feeds are where to look.

**What to rule out first, in this order:**

1. **The environment as light versus as backdrop.** Compare a single
   diffuse patch under the HDR in our Cycles and in Blender, with the
   background suppressed in both, so nothing but the irradiance is being
   compared. The constant-environment probes already agree
   (`envsweep_probe.py`), so whatever this is, it is about an IMAGE
   environment and not about the tracer.
2. **The material model.** `blender_chess.py` builds a Principled BSDF
   directly from the document's base colour, metalness, roughness and
   normal. Our engine translates standard_surface -> OpenPBR -> ccl
   nodes. Those are NOT the same surface, so part of the gap may be the
   translation rather than the lighting -- and that is item 3 of 17.13's
   list, still open. The Blender leg is an approximation and must not be
   treated as ground truth for material response until this is separated
   out.
3. **Whether the raster is right for the wrong reason.** The raster has
   no occlusion of the environment and no bounce (17.17), which makes it
   brighter; if it lands on Blender's tone anyway, one of those errors
   may be cancelling another.

**Also open, from the same three-way picture:** the raster frames the
model slightly SMALLER than either path tracer at the same reported
camera, although the aspect ratios match -- so its perspective
projection and the `heightAngle` it reports are not quite the same
thing. And the texture-upload race in 17.17.

### 17.19 Why the path tracer was the outlier: three translation defects and a missing light (fixed, 2026-09-03)

Session 11 started with an audit of the whole MaterialX-to-Cycles path
-- `CyclesMaterialX.cpp`, the shared `openPbrSurface` in
`MaterialXSupport.cpp` that BOTH consumers run, and the world shader in
`CyclesScene.cpp` -- and then measured each suspicion with a closed-form
probe. None of the four defects was in the tracer or in the node
interpreter. Three were in how the OpenPBR translation is driven, and
those hit the raster too; the fourth was a light the scene never had.

**The numbers, board region, mean byte per channel** (same rectangle as
17.16, `fcad-probes/chess3_board.py`):

| | R | G | B | mean abs. diff vs Blender | R/B |
| --- | --- | --- | --- | --- | --- |
| Blender Cycles | 72.9 | 73.5 | 65.5 | -- | 1.113 |
| raster, before | 70.3 | 69.3 | 60.5 | 3.90 | 1.163 |
| raster, after | 75.2 | 75.1 | 66.0 | 1.52 | 1.141 |
| our Cycles, before | 48.1 | 48.4 | 47.3 | **22.68** | 1.018 |
| our Cycles, after the translation fixes | 51.8 | 52.8 | 51.9 | 18.44 | 0.999 |
| our Cycles, after the background light | 69.3 | 69.7 | 61.7 | **3.69** | 1.124 |

The whole-frame 4x4 grid of `chess3_sheet.py` now reads 93.1 / 46.2 /
144.4 for ours against Blender's 93.6 / 46.2 / 144.4, and the fireflies
are gone. What is left on the model is the Blender LEG's approximation,
below, not ours.

#### 1. `translateAllMaterials` threw on a document stating two models

MaterialX's `translateAllMaterials` walks every material in the
document and throws "category is already open_pbr_surface" on the first
one that is already OpenPBR. A document carrying one OpenPBR surface
beside a standard_surface therefore rendered NEITHER, in both
consumers -- the failure is reported per document, and the report was
the raster's "does not translate to OpenPBR" with the OTHER shader's
name in it. Found because the first probe of this session put its
control (an OpenPBR flat) and its subject (a standard_surface) in one
document, and both legs came out black at byte 34.

Fixed: `openPbrSurface` translates the CHOSEN shader alone
(`translateShader`). One document, many surfaces is the chess set's
shape (17.13), and nothing says they all state the same model.

#### 2. An unstated input took the TRANSLATION nodedef's default

`ShaderTranslator::connectTranslationInputs` forwards only the inputs
the shader STATES onto its translation node; anything unstated is read
by the consumer off the translation nodedef's own defaults, and those
are not the source model's. The two disagree on exactly two inputs:

| input | `ND_standard_surface_surfaceshader` | `ND_standard_surface_to_open_pbr_surface` |
| --- | --- | --- |
| `base` | 1.0 | **0.8** (Arnold's old default) |
| `base_color` | 0.8 grey | white |

The chess set states no `base`, so every piece rendered at 0.8 of its
albedo in BOTH engines -- `fcad-probes/mtlxbase_run.sh`: a
standard_surface at the closed-form colour read byte 115 where 128 was
due, and stating `base="1.0"` gave 128. That is the 20 per cent of
17.18. It also answers 17.18's third question: the raster was "right"
against Blender because 0.8 x (no occlusion, no bounce) happened to
land near Blender's tone -- one error cancelling another, exactly the
suspicion.

Fixed: before translating, every unstated input of the source nodedef
that the translation nodedef accepts is stated at the source's own
default. That is what an unstated input MEANS, so nothing else moves:
`anUnstatedInputKeepsItsOwnModelsDefaultThroughTranslation` pins it as
"base unstated generates the same code as base 1.0, and not as 0.8".

#### 3. The translation dropped every normal map

The translation nodedef has no socket for `normal`, `tangent` or
`coat_normal` (nor for the rotations and `coat_affect_color`, which
OpenPBR genuinely lacks), and the translator then REMOVES every input of
the source, forwarded or not. Every standard_surface normal map -- one
per chess piece -- was lost on the floor in both consumers, while the
Blender leg applied it. OpenPBR has the same three sockets
(`geometry_normal`, `geometry_tangent`, `geometry_coat_normal`), so
they are carried across by attribute copy after the translation.

The path tracer needs nothing more: Cycles derives the UV tangents a
tangent-space normal map wants by itself (`Mesh::update_tangents`, run
for every mesh, with mikktspace falling back to the face normal on a
flat face -- the assert there is compiled out of the RelWithDebInfo
tree, which carries `-DNDEBUG` in its CMake flags). The chess set now
reports 58 images instead of 43. **The raster does not consume
`geometry_normal` at all** (`OpenPbrInputs::FIELDS` has no entry), so
a MaterialX normal map still does nothing there -- an open raster gap,
recorded and not built.

#### 4. The scene had no background LIGHT

`translateWorld` built the world shader -- the baked equirect through a
`BackgroundNode` -- and nothing else. Cycles samples the world as a
light only when the scene holds a `BackgroundLight` with `use_mis`
(`LightManager::device_update_background`: "no background light found,
signal renderer to skip sampling"); Blender creates one for every world
by default. Without it the environment is reached only when a BSDF
sample happens to point at it, which for a diffuse surface under
`san_giuseppe_bridge.hdr` means almost never: that picture holds **43
per cent of its irradiance in texels above radiance 64**, peaking at
35,000, and a cosine sample finds the disc about once in a hundred
thousand. Unbiased in float, and useless in a frame -- the hit lands in
ONE pixel that clips at white (the fireflies), and the model renders
without its sun: darker, and BLUER, because the sun is the warm half of
that sky (R/B 1.02 against Blender's 1.11). The backdrop is a camera
ray and never sampled, which is why it agreed all along -- 17.18's
"same picture, two paths, only one agrees", with the answer being that
one path was never sampled at all.

Fixed: one `ccl::BackgroundLight` in its own object, made once, MIS on,
`map_resolution` 0 so the importance map is sized from the baked
equirect; it follows the world shader through `Shader::tag_update`.
The kernel's clamp was never the mechanism, for the record: a
one-bounce background hit is clamped as DIRECT (`bounce - 1`,
`film/light_passes.h`), and direct clamping is off.

#### What the remaining difference is, and whose it is

The kings and queens carry `subsurface` from a scattering map (mean
218/255), and the white pawn HEADS are glass (`transmission` 1,
`transmission_color` 1, 1, 0.828). `blender_chess.py` reads a base
colour, a metalness, a roughness and a normal and nothing else, so its
kings are opaque and its pawn heads are white balls, while ours scatter
and refract as the document says. That is the Blender leg's
approximation, stated in its own docstring, and it accounts for the
pieces that still look different in the crop. One fidelity note on our
side: OpenPBR's `subsurface_radius` is a length in scene units, and
this scene is in millimetres, so the document's 0.003 is a radius a
thousand times smaller than the metre-scale author meant -- harmless
here, worth a unit scale later.

#### The method, again

Every finding was measured with the 17.15 method -- a scene whose
answer is known in closed form, with its own control in the same
engine: `mtlxbase_probe.py` (base stated / unstated), `mtlxdata_probe.py`
(metalness from L-mode JPEG and PNG, a flat normal map: all exactly
128, so the data maps were never the problem), and a small RGBE reader
for the HDR's irradiance distribution. The one reference render was the
Blender leg, rerun unmirrored (`... 0 0`) now that the engine reads the
HDR the right way round (17.16); its camera came out identical to
session 10's to six decimals.

Two traps for the record. A probe document that mixes models measured
defect 1, not the engine (both legs black) -- a probe's control and
subject must be legal together. And "the two engines agree" proved
nothing here either: they agreed on 0.8 because they run the same
translation, which is why the audit had to read the translator and not
just the two consumers of it.

**Still open** from 17.18's list: the raster's constant 1.07x on a flat
OpenPBR surface (every probe leg reads 132-133 for 128), its smaller
framing at the same reported camera, its `geometry_normal` gap above,
the texture-upload race of 17.17, and the AO radius question.

### 17.20 The reference stops flattering the opaque reading (2026-09-03)

17.19 ended on "the remaining difference is the Blender leg's": its
`blender_chess.py` built each Principled BSDF from base colour,
metalness, roughness and normal only, so its white pawn heads were
white balls and its kings and queens were opaque, while the document
states `transmission` 1 on the heads and a `subsurface` map on the
royals. The leg now wires what the document states:

- `transmission` -> Transmission Weight, `specular_IOR` -> IOR (1.5,
  standard_surface's default, stated so the leg does not drift).
  Principled has no transmission colour socket. With
  `transmission_depth` 0 -- the default, and what the chess set has --
  standard_surface tints the transmitted light AT THE SURFACE, which
  is exactly what Principled's Base Color does to its refraction, so
  the tint is folded into Base Color (a stated depth becomes a
  `Volume Absorption` node instead, sigma = -ln(colour)/depth per
  channel, and Base Color is left alone).
- `subsurface` -> Subsurface Weight, `subsurface_radius` -> Subsurface
  Radius, `subsurface_scale` -> Subsurface Scale. Blender folded the
  subsurface colour into Base Color in 4.0; this document states
  `subsurface_color` = the base colour map, so nothing is lost. The
  scale is a scene-unit length and Blender's scene is the glTF's
  metres, so the document's 0.003 is the 3 mm its author meant there
  (ours is in millimetres -- the unit-scale note of 17.19 stands).

One re-render, same camera (`... 0 0`), against the 17.19 frames of
the other two legs (`/tmp/chess3-lobes`), `chess3_board.py`:

    leg            R      G      B     MAD    R/B
    raster       75.2   75.1   66.0    6.04  1.141
    our Cycles   69.3   69.7   61.7    0.83  1.124
    Blender      68.3   69.0   60.9    0.00  1.122

Blender's board region came down 4.6 bytes once its pawn heads refract
and its royals scatter, and the two path tracers now sit **0.83 bytes**
apart (17.19: 3.69), R/B 1.124 against 1.122; the whole-frame grid was
already byte-identical. The crop agrees by eye: the heads refract the
board through themselves in both, the royals have the same soft edge.
The raster is now the leg that is 6 bytes light, and the biggest single
reason is visible in its frame: its pawn heads are still white balls,
because `fc_openpbr.sh` has no transmission lobe (ShaderDesign.md 3.8,
"transmission is the glass pass's business") and nothing yet hands a
MaterialX surface to that pass. That is the next section.

### 17.21 A MaterialX glass reaches the raster glass pass (2026-09-03)

The raster had no route for a transmissive MaterialX surface. Its
OpenPBR shader has no transmission lobe by design ("a rasterizer
cannot refract through geometry: transmission is the glass pass's
business", `fc_openpbr.sh`, ShaderDesign.md 3.8), `OpenPbrInputs::FIELDS`
never queried `transmission_*`, and nothing handed such a surface to
the glass pass -- so the chess set's pawn heads, `transmission` 1 in the
document and glass in both path tracers (17.20), were opaque white
balls in the raster. The glass pass itself, meanwhile, already did
everything wanted: screen-space refraction, Fresnel reflection,
per-channel Beer-Lambert over the body's front/back interval, a
tinted shadow. It just took its inputs from `Render_Glass`.

#### Where the decision is made, and why there

Everything downstream of `Render::Material::glass` is generic: the pass
activation (`hasGlassBody`), the per-draw claim (`surfGlass`) that takes
the draw OUT of the opaque mesh pass, the medium exemptions, the shadow
tint route, the instancing exclusion and the snapshot all key on that
one flag and never on how it became true; `submitGlassSurface` reads
only the material's colour, IOR, density and roughness. So the route
is a PRODUCER-side question, and it is answered where `Render_Glass`
is already answered -- the render-cache bridge's material conversion
(`SoFCRendererBridge.cpp`): a draw wearing a "material"-stage MaterialX
shader whose surface states transmission gets `glass` set and the four
fields filled from the document, and every consumer, the streamed
viewer included, sees an ordinary glass draw. Cycles is untouched by
it: its MaterialX path builds the shader from the document's own graph
(`materialXShader`) and never consults the flag. `Render_Glass` on the
same shape wins over the document -- it is the user's own word on this
shape, the document is its material's.

What the document states is resolved ONCE per document and surface, in
the shared inspection (`Render::MaterialX::inspect`,
`DocumentInfo::transmission`), off the OpenPBR surface AFTER the
translation both engines render, so a standard_surface `transmission`
and an OpenPBR `transmission_weight` answer alike. The reading is
FLAT -- what a pass that samples nothing per fragment can make of an
input -- by a walk that follows the same connection kinds the
interpreter evaluates (interface socket, nodegraph output, node)
through the `dot` nodes and graph outputs the translation leaves
between the surface and what the document wrote, and stops at a
constant, at one image feeding the input directly, or at anything else.
The rules that fall out:

- `transmission_weight` a constant of one half or more: a glass body.
  A MAPPED weight cannot be read flat: the surface stays opaque, as it
  did before there was a route, and a warning says so.
- `transmission_color`, linear as the document states it, is the body
  colour. Never decoded: `Material::glasscolor` beside the authored
  `diffuse`, with `glassmtlx` saying which the pass reads. Mapped:
  white, with a warning.
- `transmission_depth` decides what the colour MEANS (OpenPBR). Over a
  positive depth it is what survives that path length: density
  `1 / depth` into the pass's existing `sigma = density * (1 - colour)`,
  which is the very closure the tracer's absorption volume applies to
  the same numbers. At ZERO it is a tint applied once at the surface,
  whatever the thickness: a new `u_glassTint` multiplied into the
  transmitted light after the absorption, with the body absorbing
  nothing. A zero density is therefore NOT "automatic" for a MaterialX
  glass, where `Render_Glass` with no density means `3 / diagonal`.
- `specular_ior` is the IOR, `specular_roughness` the roughness. The
  pawn heads' roughness is a MAP; the pass has one roughness per draw.
  The capture holds the decoded pixels already (the map is one of the
  document's images), so the map's MEAN stands in -- 0.235 for the
  pawns -- which follows the document where the model's default (0.3)
  would ignore it. The inspection names the file; the bridge averages.
- The shadow tint reads the same two colours (absorb x tint), so a
  tinted glass casts its tint whichever meaning the depth gave it.

The snapshot carries the two new fields (v73, chunk 15); an older
snapshot has only `Render_Glass` bodies, which is what those builds drew.

#### What it does not do, and the next increment

The pass draws the body FLAT: one colour, one roughness, the mesh's own
normal. A MaterialX glass with a mapped colour, a normal map, or a
partial weight is not expressed -- a weight below one half is opaque,
one above is fully glass; there is no diffuse share in the glass shader
to mix against. The real second increment is a glass variant of the
generated material function: `fs_fc_glass.sc` spliced with
`FC_USER_MATERIAL` like the mesh stage is, reading `m.specularRoughness`,
the normal and new transmission fields of `FcOpenPbr` per fragment.
That needs a texture-coordinate varying the glass pairing does not
carry, the image array unit beside the pass's seven samplers, and a
second entry in the user-program cache. Built: 17.22.

#### Two things the route exposed in the path tracer

Reading the document flat for the raster read it for Cycles too, and
the tracer had two gaps on the same inputs, fixed in the same change:

1. With no depth it DROPPED `transmission_color` -- it connected the
   weight and added an absorption volume only for a stated depth. The
   pawn heads' `1, 1, 0.828` never reached it. Principled tints its
   refraction by Base Color and has no socket for a surface tint, so
   it folds in there: `base * mix(1, colour, weight)` -- exact for a
   fully transmissive body, tinting the diffuse share too for a partial
   one, which is what Blender's own importers accept and what 17.20's
   Blender leg does.
2. It read the depth as a LITERAL on the surface node. A translated
   standard_surface carries every input as a connection through the
   translation graph, so every depth a standard_surface document stated
   read as 0. Now read through `flatConstant`, the same walk.

#### Measured

The 17.20 frames, re-run for the raster and our Cycles with the route in
(`/tmp/chess3-glass`, Blender's frame unchanged), `chess3_board.py`:

    leg            R      G      B     MAD    R/B      (17.20)
    raster       70.4   70.6   61.4    1.38  1.146     (6.04)
    our Cycles   68.3   69.1   61.0    0.07  1.119     (0.83)
    Blender      68.3   69.0   60.9    0.00  1.122

The raster's board region came down from 6.0 to **1.4 bytes** off the
reference, and the crop shows why: its pawn heads refract the board
through themselves now, where before they were white balls. The two
path tracers sit **0.07 bytes** apart -- the last 0.8 was the tracer
dropping the heads' depth-0 tint (gap 1 above); with it applied the
whole-frame grid stays byte-identical (93.1 / 46.2 / 144.4) and the
board region agrees to a tenth of a byte. What is left in the raster's
1.4 is its own list from 17.19 -- the constant 1.07x, the smaller
framing, no `geometry_normal` -- none of it glass.

Seven tests in `MaterialXGen_tests_run` (45/45) pin the flat reading: a
stated OpenPBR transmission, the opaque default, a standard_surface
transmission through the translation, a stated depth through it (the
literal read's 0), a mapped weight refused with its warning, a mapped
roughness naming its image, and the chess set's pawn head against its
pawn body. The first walker stopped at the translation NODE, whose
`dot` nodes live in the nodedef's implementation graph and not in the
document, and read every translated document as unstated -- three of
the seven failed on it, which is what they are for.

### 17.22 A textured MaterialX glass (2026-09-03)

17.21 drew a MaterialX glass FLAT: one colour, one roughness, the
mesh's own normal, a weight that was either opaque or all glass. The
chess set's pawn heads state their roughness as a MAP (mean 0.235),
and a glass with a mapped tint or a normal map had no way to show it.
The second increment recorded there is now built: the glass body
stage is spliced with the document's generated material function the
way the mesh stage is, and reads the surface per fragment.

#### The splice

`fs_fc_glass.sc` is now a varying list around `fc_glass_fs.sh`, the
body, exactly as `fs_fc_mesh.sc` is around `fc_mesh_fs.sh`. The
backend's per-document variant (`materialXVariant`) assembles TWO
sources from ONE generation: the mesh splice it always made, and the
same prologue over `fc_glass_fs.sh` (`MaterialXVariant::glassSource`).
Both define `FC_USER_MATERIAL`, both pair with `vs_fc_mesh_tex`, both
carry the same varying list, so a document generates once and compiles
twice. `getUserProgram` takes which splice is wanted (`UserSplice`);
the program cache keys on the assembled source, so the two are two
entries. `submitGlassSurface` asks for the glass splice when the draw
is a MaterialX glass (`Material::glassmtlx` with its `usershader`),
pushes the document's parameters and its image array like the mesh
submit does, binds the mesh's texture coordinates on stream 2 under the
identity texture matrix, and draws with it -- and with the flat
program otherwise: while the compile is pending, when it failed, and
on the viewer tier, whose snapshot ships the mesh splice only and
whose `getUserProgram` answers invalid for the glass one rather than
hand the glass pass a program that lights an opaque body. The body is
glass either way; what changes is whether it is the document's glass
or the flat reading of it.

Inside the body, under `FC_USER_MATERIAL`: the geometry is filled, the
defaults stated, `fcUserMaterialInputs` run, and then the pass's five
inputs come from the struct instead of the uniforms -- IOR from
`specularIor`, roughness from `specularRoughness` (read BEFORE the
clamp: its 0.05 floor is for a microfacet lobe under a delta light,
and a polished pane is not frosted), and the colour by the same rule
17.21 gave the flat route: a positive `transmissionDepth` makes it the
absorption colour at density `1 / depth`, a zero one makes it the
surface tint. New, because a flat pass could not express it: a
`transmissionWeight` below one mixes a diffuse share of the base colour
(the environment's coarse irradiance under `baseColor`, the lookup the
frosted scatter already uses) into the transmitted light before the
Fresnel reflection covers both; and the document's `geometryNormal`,
when it states one, replaces the mesh normal for the refraction
direction and the reflection alike. `u_glassParams.w`, the frame's
depth-reject flag, is the one uniform still read.

#### What the struct gained, and what it fixed on the mesh stage

`FcOpenPbr` carries `transmissionWeight`, `transmissionColor`,
`transmissionDepth` and `geometryNormal` now, with the spec defaults
(0, white, 0) and ZERO for the normal, meaning unstated; the generator's
field table emits the four inputs. The mesh lighting ignores the
transmission -- the header of `fc_openpbr.sh` still says why -- so a
partial weight below the claim threshold shades as it did. The normal
it does consume: `fcOpenPbrShadingNormal` turns a stated world normal
into view space on the mesh's side of the surface (a two-sided draw has
flipped its normal toward the viewer already, and a map painted for the
front is reflected into that hemisphere on the back rather than
trusted), and the OpenPBR branch takes it right after the document has
spoken, before the frame, the lights and the environment lookups. That
closes 17.19's "the raster does not consume `geometry_normal` at all":
a MaterialX normal map, carried across the translation since 17.19,
finally moves a mesh-stage surface too.

For the map to mean anything the graph needs a tangent frame, and the
one `FcMtlxGeom` handed over was the VIEW's x axis in world space --
every normal map read in camera space. `fcMtlxGeomFill`, shared by
both stages now, builds the frame the bump path always did (the
cotangent-frame trick on screen-space derivatives of position against
the uv), orthogonalized, and hands over the derived bitangent as its
own field (`bitangentWorld`, which the generator used to synthesize as
`cross(n, t)` -- a mirrored uv flips it, and a map painted for that uv
expects the flip). With no uv the frame falls back to the view axis,
orthogonalized, so an untextured draw still has a basis.

Which exposed the stream. A generated material pairs with the textured
vertex stage whatever the draw's own texturing says, and the mesh
submit bound stream 2 only for a draw the TEXTURE path had set up (a
Coin texture, a bump or data map, a per-face palette on the mesh's
uv). A plain UV-mapped shape wearing a MaterialX image material had the
attribute unbound: it read its constant, and every map sampled one
texel. Not seen on the chess set, whose glTF pieces carry per-face
images and so took the textured path; `bindMeshTexCoord` now binds the
mesh's coordinates under the identity matrix for both splices whenever
the texture path did not.

#### Measured

The 17.21 frames re-run for the raster and our Cycles with the splice
in (`/tmp/chess3-tex`, Blender's frame unchanged, `CHESS_SETTLE=90` so
the asynchronous compiles land before the capture), `chess3_board.py`:

    leg            R      G      B     MAD    R/B      (17.21)
    raster       70.1   70.3   61.1    1.06  1.147     (1.38)
    our Cycles   68.3   69.1   61.0    0.07  1.119     (0.07)
    Blender      68.3   69.0   60.9    0.00  1.122

The raster's board region came down from 1.38 to **1.06 bytes** off the
reference. A small step, and a small step is exactly what has to be
scrutinized ([[frame-statistic-wrong-region]]): three things say it is
the splice and not the fallback. The compile cache holds TWO glass
splices with binaries (the white and the black pawn heads); the frame
diff against 17.21 is exactly the sixteen pawn heads and nothing else
on the board; and the pieces' own change is where the normal maps are
-- up to 90 bytes on back-row pixels, small because the chess set's
maps are subtle (a standard deviation of ten levels, a few degrees of
tilt) and the pieces are a few dozen pixels across at the dump's size.
What is left in the raster's 1.06 is 17.19's list -- the constant
1.07x, the smaller framing -- with `geometry_normal` now struck off it.

One trap on the way. The runtime user-shader compile includes from a
COPY of the shader directory that CMake globs at configure time, and a
brand-new include file is not in a standing build tree's copy: the
first run's glass splices failed with "Cannot open include file
fc_glass_fs.sh", the flat program stood in exactly as designed, and the
probe reported PROBE OK over a measurement of the fallback. A
fallback that draws something plausible hides its own absence; the
compile cache (a `.sc` with no `.bin` beside it) and the run log's
"user shader compile failed" are what say which program drew. The
globs carry `CONFIGURE_DEPENDS` since: the build re-checks the shader
directory and reconfigures itself when a file is added or removed,
proved by a scratch `.sh` appearing in the copy on the next build and
an unchanged tree not reconfiguring.

Three tests in `MaterialXGen_tests_run`: the transmission inputs reach
the generated function as literals; a mapped transmission colour is an
image fetch there, not a literal; a stated normal is emitted, not a
literal, and the frame it is expressed in names the geometry's tangent
and bitangent.

### 17.23 The viewer tier draws the document's MaterialX (2026-09-03)

The browser viewer drew none of it. A MaterialX document naming an
image was never shipped to it at all -- the server-side compile
declined the document because its pixels did not travel, and the stock
appearance stood in (6.12's last paragraph) -- and a glass document
reached it as a flat glass body, because the shipped variant had one
slot, the mesh splice, and the viewer's program lookup answered invalid
for the glass one rather than hand the glass pass a program that lights
an opaque body. The chess set, which is both, was a set of grey pieces
with white balls for pawn heads in the browser and the picture of 17.22
on the desktop. And the viewer had not linked since 6.12: the shared
image-binding code called the desktop's generator-side variant and a
sampler helper that lived in the desktop-only block, so the wasm build
failed at the link -- nobody had built it in the two days since.

#### What travels, and who does the join

The two halves of an image binding are produced by sides that cannot
see each other (6.12): the generator knows which layer of the program's
array each file is, the capture has the pixels. The desktop joins them
per draw, on the path (`BGFXView::pushUserImages`). A viewer tier has
the capture's half -- once the images travel -- and no generator at
all, so the join has to be made for it, once, before the shader leaves.

That is the ship hook. `SceneSnapshot::shaderBins`, which appended the
server-compiled binaries to a copy of the compiled list, is now
`shipShader`: it is handed the COPY of the shader that travels and makes
it whole -- the binaries as before, and now the layout: `Image::layer`
on each image (resolved against the generator's list by path, -1 for a
file the generated code does not read), and the array's sampler name
and unit on the shader (`imageSampler`, `imageUnit`), which only the
generator knows. The desktop's own object is never written: it keeps
-1 and empty, and joins through its variant as it did.

The snapshot (v74, shader chunk revision 16) writes, after everything
the shader carried, a glass splice binary per compiled variant
(`Compiled::glassBin`), then the sampler, the unit, and the images --
each one its path, its layer and an ordinary texture record, which
under the streaming transport is the header inline and the pixels
deferred by content key. So a map is fetched once, cached in the
viewer's blob store like a material's texture, and shared with any
other shader or material naming the same pixels (the loader's texture
memo is keyed on the content). A bundled capture carries the pixels
inline in the shader entry and stays self-contained. An older snapshot
reads as before: no images, no glass binary, which is exactly what
those builds shipped.

The glass splice is compiled for the viewer targets only when the
surface claims a glass body (`UserShader::glass.claimed`, the capture's
flat reading of 17.21): the glass pass is the only consumer, and a
compile per document per target is not spent on a surface that will
never wear it. Like the particle state step, a pending glass compile
holds the whole variant back rather than shipping a mesh splice whose
glass half is missing -- the flat pass would draw a different picture,
and a republish on the compile's landing is the ordinary path.

#### The viewer's side

`getUserProgram` on the standalone tier pairs a MaterialX program with
`vs_fc_mesh_tex`, whatever stock stage the caller named -- the same
substitution the desktop makes, and a necessary one: both splices were
assembled over the textured varying list, and bgfx links only on an
exact varying match ([[bgfx-varying-exact-match]]). Before this the
viewer would have paired an imageless document's mesh splice with
`vs_fc_mesh` and silently drawn nothing new; no one had noticed because
no image document had ever reached it and the imageless ones were never
tried there. The glass splice resolves from `glassBin` under the same
profile match, and a variant without one answers invalid, so the flat
program stands in exactly as it does on the desktop while a compile is
pending. `pushUserImages` on that tier stacks the shipped images by
their layer into one array on the shipped unit under the shipped
sampler name; the sampler-handle helper moved out of the desktop-only
block so both tiers make one on first use.

Nothing on the viewer knows MaterialX exists. It receives a fragment
binary, a vertex-stage name, a sampler name, a unit, and layered
pixels -- which is the shape every other asset already has, and the
reason the generator never has to be ported to the browser.

#### What the first browser frame said

With the splices and the images travelling, real Chrome drew the set
from the shipped programs in seventy-five seconds -- and drew it wrong:
a flat grey board, one white knight, and two black pieces a saturated
red. Two minutes in, the board had its checker and the marble was
marble, and the two red pieces were still red. The request log said
why: forty-three blobs of 12,582,912 bytes each. A 2k map is 400 kB as
the JPEG the document names and 12 MB as the RGB the capture decoded
it into, and `writeTexture` shipped the pixels -- which is what every
material texture had always done, and had never been asked to do
forty-three times over. Half a gigabyte took thirty seconds on
loopback, the viewer's blob store (512 MB) started evicting what it
had just fetched, its heap reached 1.8 GB, and the arrays that stack a
document's maps had stopped waiting long before any of it landed.

That last part is the red. An array built while its layers are in
flight stands in with white, and it was re-examined once a frame for
120 frames -- a bound written against a decode in flight, which lands
in a few frames, not a network. Past it the white stayed for good. A
white base colour is a white piece (the knight in the first frame); a
white base colour AND a white metalness map is a mirror, and a mirror
of that environment is the orange wall behind the board, which is what
the two metal-mapped black pieces were reflecting. Pieces whose maps
happened to arrive inside the bound came out right, so the picture
looked like a per-material bug and was a scheduling one.

Both are fixed at the root rather than by widening the bound:

- **The transport ships the file.** `TextureImage::encoded` holds the
  bytes the producer decoded a map from, when they are a JPEG or a PNG
  (decided by signature, `ImageDecode.h`), and `writeTexture` sends
  those in place of the pixels (v75, a flag after the sample kind; the
  content key is the key of the file bytes). The reader decodes on
  arrival through the one stb_image the renderer now compiles
  (`ImageDecode.cpp`, shared with the viewer's streamed-frame decoder,
  which had its own copy). The bridge keeps the file beside the pixels
  for every image it decodes through Qt -- a document's maps, a ground
  texture, a bump map alike -- so the chess set's maps are 20 MB on the
  wire where they were 540. The browser tier decodes under a cap of
  1024 a side, halving box-filtered (the mip it would have drawn at
  that size), because its whole scene lives in one heap; the desktop
  keeps what the file says.
- **An array is rebuilt when a layer it waited on arrives**, and only
  then: `GpuTextureArray` remembers WHICH layers were not usable when
  it was built and re-examines those slots of the palette handed in on
  each draw, so a map landing after any number of frames still
  replaces its white, and a file that will never decode never wakes a
  rebuild. The frame bound is gone. The first cut remembered the image
  OBJECTS instead of the slots, and one browser run in two came up
  with no map at all: a streamed republish (the backend republishes as
  each compile lands) re-parses a shader chunk into fresh texture
  objects under the same ids, the array's key is the ids, and so the
  array kept watching the first publish's objects -- whose fetches had
  been retired with that publish -- while the current objects held
  the pixels. Which run failed depended on whether a republish fell
  between the first draw and the last arrival.
- **A texture in flight belongs to the model, not to the publish that
  named it.** With the slots fixed, one run in two still came up with
  a flat board and half the pieces plain, and the reason is a level
  below the arrays. The viewer keeps an object's draws from the
  publish that last carried them (`applySceneObjects`), and a delta
  that leaves an object alone leaves those draws holding the FIRST
  parse's texture objects. Meanwhile the delta re-parses every chunk
  it does name into fresh objects, and when it is committed the
  superseded snapshot's outstanding fetches go with it. A map whose
  bytes were in flight at that moment was filled into an object
  nothing drew, or into none at all -- and every publish the backend
  makes as its compiles land is such a moment. Two things fix it, and
  both are the consumer's: a texture memo across publishes
  (`SceneSnapshot::textureMemo`, weak, by content key) so a re-parse
  hands back the object the model's draws already hold, and the
  carry-over that already preserved geometry ladders across a delta
  (`carryLadders`) now carries a texture fetch still outstanding whose
  key the fresh publish does not name. A texture entry is marked as
  such (`DeferredChunk::texture`) because it is the one kind of fill
  that writes only its own object and may run against any snapshot; a
  group's or a material's writes into its snapshot's tables and may
  not. The fill is idempotent, so a texture two snapshots both wait on
  is filled by whichever lands first.

#### Verification

Real Chrome (WebGL2 on ANGLE over D3D12, `scripts/wasm-chrome.js`)
against a fresh headless serve of the chess set
(`fcad-probes/chess_serve.py` under `renderer-serve.sh`), forty-five
seconds after load, twice from the same fresh serve because the
failures above were one run in two:

    run   heap     last blob   maps    board     pawn heads   red pieces
    1     1318 MB  --          none    flat      dark balls   2  (raw pixels, 75 s)
    2     1832 MB  31 s        most    checker   glass        2  (raw pixels, 120 s)
    3      373 MB  9.6 s       none    flat      glass        0  (encoded, first array rule)
    4      538 MB  --          all     checker   glass        0  (same bundle as 3)
    5      448 MB  10.9 s      half    flat      glass        0  (slot rule, first after serve)
    7      448 MB  12.8 s      all     checker   glass        0  (memo + carry, first after serve)
    8      448 MB  12.2 s      all     checker   glass        0

The last two frames are the set as the desktop draws it: the marble on
every piece, the checker on the board, the pawn heads glass and pale,
the black bishop and queen black -- from forty-three JPEG blobs
totalling twenty megabytes where run 2 had pulled five hundred and
forty of raw pixels, and with the browser's heap a quarter of what it
was. The glass splices drew: the two `fc_glass_fs` sources in the
compile cache carry `300_es` binaries beside their `140` and desktop
ones, and the heads refract the board where run 1's stood in flat.

Tests: `SceneDump_tests_run` 43 (five new -- a MaterialX shader ships
its images, layout and glass binary through the manifest layout and
inline through the bundled one; an encoded texture travels as its file
and decodes back to its pixels both ways; a texture in flight is shared
across two parses and filled by whichever publish lands first);
`MaterialXGen_tests_run` 48/48; `ctest` 473/473.

What this leaves open on the viewer tier: a document's maps decode at
1024 a side there, a constant chosen for the heap and not a setting;
`kDecodeMaxSide` is where a per-device policy would go. And a scene
whose maps are PNGs with alpha or greyscale files travels them as
files too, but a Radiance environment still travels as floats -- a 2k
HDR is the one 12 MB blob left in the request log.

### 17.24 The raster's constant 1.07x was the specular lobe's edge (fixed, 2026-09-03)

17.15 left one number on the raster: a flat OpenPBR surface with
`specular_weight` 0 under the constant environment drew byte 132-133
where the closed form and the path tracer both say 128 -- linear 0.2307
for 0.2159, a factor of 1.069, reproducible, the same in every leg of
`mtlxbase_probe.py` and in every run. It carried through 17.21 and
17.22 as "17.19's list" and was the last of the raster's known
brightness residuals with no image involved.

**Where it was.** The surface's specular lobe is a dielectric at IOR
1.5 whose strength `specular_weight` sets by lowering the IOR toward
air (OpenPBR PR #247). Under the punctual lights the raster has that
exactly: `fcPbrFresnelDielectricMod` builds the modified IOR, and at
weight 0 it is air, and the Fresnel reflectance is zero at every
angle. Against the ENVIRONMENT the lobe is not integrated but read off
the split-sum fit, `f0 * A + fEdge * B`, where `fEdge` is the
reflectance the grazing half of the fit carries -- Schlick's F90, which
`fc_openpbr.sh` passed as one. `f0` was scaled by the weight and the
edge was not, so a surface stating no specular at all still took the
whole of `B` from the environment. The same term entered twice, with
opposite signs and unequal sizes: the diffuse slab weight is `1 -
specAlbedo`, so the diffuse LOST `rho * B`, and the specular
environment term GAINED `B`, for a net `B * (1 - rho)`.

**The number matched before anything was changed.** At the probe's
camera the mode face sits at an isometric `ndv` of 0.577; the fit's
`B` there at the default roughness 0.3 is 0.0200, and `0.0200 * (1 -
0.2159)` = 0.0157 over 0.2159 is 0.2316 linear -- byte 132.4, which is
the 132-133 measured. A neighbouring face at `ndv` 0.5 predicts 135
and one at 0.7 predicts 130, which is the spread the frames show.

**The fix** is one helper, `fcOpenPbrSpecEnvAlbedo`, which both the
slab weights and the environment term now call: the edge reflectance
is scaled by the same weight as `f0`, `min(specular_weight, 1)`. That
is the convention `KHR_materials_specular` states for the same knob
(its factor scales F0 and F90 alike), and it is exact at the two ends
-- a full-strength dielectric is unchanged, a weight of 0 removes the
lobe at every angle, as the punctual path and Cycles already did. In
between it is Schlick's approximation of a lowered IOR, which is what
the fit was already assuming for the rest of the curve. The stock CAD
surface states `specular_weight` 1, so nothing outside a MaterialX
document that lowers it changes by a bit.

Measured after, `fcad-probes/mtlxbase_run.sh`, the same three legs
(the shader assets rebuilt; the runtime splice cache keys on a content
hash of the include tree, so the old binaries missed by themselves):

| leg | raster before | raster after | Cycles |
| --- | --- | --- | --- |
| Flat (open_pbr_surface) | 132 | **128** | 128 |
| Std (standard_surface, base unstated) | 133 | **128** | 128 |
| StdBase (standard_surface, base 1.0) | 133 | **128** | 128 |

Byte 128 is the closed form exactly, in every leg of both engines.

This touches only a document that LOWERS `specular_weight`. The chess
set's standard_surface pieces state the default specular of 1, so its
17.22 board-region number is not expected to move, and was not rerun.

**The other item on 17.19's list, "the smaller framing at the same
reported camera", is the probe and not the engine.** The raster leg is
the viewport's own dump, 602x326 under the 1024x768 xvfb screen, and
the Cycles leg is rendered at 640x480; both cameras keep the vertical
extent, so the wider frame shows the same box smaller by the ratio of
the aspects, 1.85 over 1.33. `chess3_probe.py` already renders the path
tracer at the raster dump's aspect for exactly this reason, and the
"body share" column of `mtlxbase_probe.py` cannot be read across the
two engines at all: the raster body is flat, so its share is coverage,
while the path tracer's is the share of pixels at the mode byte under
64 spp of noise. Struck off; nothing on 17.19's list remains.

### 17.25 The shader graph editor: port MaterialX's, host it on bgfx (ruled 2026-09-03)

The view half of 17.11. Researched and ruled the same day; the design,
interfaces, file map and build order live in `docs/ShaderGraphEditor.md`,
and this entry keeps only the terrain and the rulings.

**Why.** "Edit Shader Graph..." materializes the card's document and
then leaves the user a `PropertyStringItem` -- a one-line `ExpLineEdit`
-- over a whole XML document; `ViewProviderShaderProgram` has no
`doubleClicked`. The button promises an editor the fork lacks.

**What MaterialX ships.** `source/MaterialXGraphEditor/` (1.39.5, off
in our build): 8.1k lines, of which `Graph.cpp` (4.7k) is the editor
proper and `RenderView` (1.4k) a complete GL viewer on
`MaterialXRenderGlsl` (which we build OFF). Dependencies are Dear
ImGui and `imgui-node-editor` (nested submodules, unfetched here),
GLFW and Glad. The coupling to its viewer is a surface of about ten
methods, and it edits one `mx::Document` in place with XML in and out
-- which is our program's text-in-a-property contract exactly.

**Ruled:**

- Port the editor as a COPY into `src/Gui/Renderer/GraphEditor/`;
  neither rewrite it in Qt (QuiltiX's shape) nor launch the stock
  executable on a temp file.
- Draw its ImGui with bgfx's own copy (1.92.8) and bgfx's ImGui
  renderer, inside a `QOpenGLWidget` through the standalone
  `Render::DrawSurface` the CAM simulator and the TechDraw page use.
  Not `imgui_impl_opengl3`: no desktop-GL assumption in a new
  subsystem.
- The preview is OUR engine's (the material icon's transient sphere
  through `renderOffscreen`), so it shows what the viewport shows;
  `RenderView`, `MaterialXRender`, Glad and GLFW are dropped.
- The editor is a view of an `App::ShaderProgram`. The button
  materializes if needed and opens it; "Revert Shader Graph" becomes
  its own button; double-clicking a MATERIALX-dialect program in the
  tree opens the same view.
- Hosted the way a TechDraw page and a spreadsheet are: through
  `Gui::ViewPlacement::place(view, Category::DocView, doc)`, whose
  default for a document view is **Split** -- a cell beside the 3D
  view -- with the user's `View/OpenView` preference and the Alt
  inversion deciding otherwise. Never a hard-coded MDI tab.
- A one-day spike (ImGui version skew; a focus-taking bgfx widget
  beside the 3D views) precedes the port. Coding starts in the next
  session.
