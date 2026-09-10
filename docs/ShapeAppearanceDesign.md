# ShapeAppearance, compatible with upstream but not laid out like it

Status: stages 1-5 have landed (storage and the property 2026-08-12 with
the alpha convention flip of 7.9 following; Coin carriage, bgfx
rendering, glTF and STEP 2026-08-13), and section 8's PBR mode with
them, per-face streams included. Section 9, surface finish, is design.
Context: [UpstreamCoreSync.md](./UpstreamCoreSync.md) section 5.1, which
records why the property exists and what it cost upstream.

## 1. What we are committing to

Adopt upstream's `ShapeAppearance`: its **public API**, its **Python
behaviour**, and its **document format**. Other CAD does separate physical
material per body from appearance per face, and per-face appearance is a
real requirement.

Do **not** adopt its **storage layout**. Upstream stores an array of whole
materials; we store per-field arrays. Everything above the storage looks
identical to upstream, so upstream code and upstream documents work
unchanged.

## 2. Why the layout is worth diverging on

Two material classes are in play and they are not the same size. Upstream's
is not 18 floats:

| member | bytes |
|---|---|
| 4 x `Color` (ambient, diffuse, specular, emissive) | 64 |
| `shininess`, `transparency` | 8 |
| `MaterialType` | 4 (+4 padding) |
| `image`, `imagePath`, `uuid` (3 x `std::string`) | 96 |
| **total** | **~176**, plus a heap allocation per non-empty string |

The three strings were ported from upstream on 2026-08-12 (section 7.3), so
a whole material is now **11x** a colour rather than the 5x it was before --
and on every object in every existing document all three are empty. That is
the case the per-field layout is built for: an empty field is size 0 and
costs nothing at all, where an array of whole materials pays 96 bytes an
entry to store nothing.

And on the path that actually produces per-face data -- STEP import -- 
OCCT supplies only colour and alpha (see UpstreamCoreSync section 5.1), so
every one of those materials carries four default colours, two default
floats and three empty strings it will never use.

## 3. The storage

Per-field arrays, each independently sized 0, 1 or N:

    class PropertyMaterialList {
        int _count;                     // logical entry count N
        std::vector<Color> _ambient;    // size 0, 1, or N
        std::vector<Color> _diffuse;
        std::vector<Color> _specular;
        std::vector<Color> _emissive;
        std::vector<float> _shininess;
        std::vector<float> _transparency;
        std::vector<std::string> _image, _imagePath, _uuid;
        std::vector<int8_t> _type;      // Material::MaterialType
    };

`_type` is there because `App::Material::operator==` compares it, so a list
that dropped it would fail to give back what was put into it. It has never
been persisted -- no material list in any FreeCAD writes it -- and it stays
that way.

`_image`, `_imagePath` and `_uuid` arrived with the material port and behave
like every other field: empty on every object today, one element when a
whole object shares a texture, N when faces differ.

with the cardinality convention:

- **0** -- every entry holds the field's default; costs nothing
- **1** -- uniform across all entries; costs one element
- **N** -- genuinely per-entry

`_diffuse` **is** the old `DiffuseColor` list. Migrating a pre-existing
document moves that vector across as-is; there is no conversion and no
widening, which is the compatibility half of this design and the efficiency
half at the same time.

### 3.1 What it costs, by case

For a 10,000-face solid, against storing this fork's 80-byte material whole:

| case | whole materials | this design |
|---|---|---|
| uniform appearance | 800 KB | **16 B** (one colour, the only field that differs) |
| STEP import, colour + alpha per face | 800 KB | **160 KB** (`_diffuse` alone -- its alpha IS the transparency, rest empty) |
| full per-face materials (glTF) | 800 KB | 800 KB |

The uniform row is the one that matters most in practice and it is not a
rounding difference: a field that is the same everywhere is stored once, so
an object's appearance costs what its appearance is, not what its face count
is.

The uniform case also drops the redundancy we have today, where
`ShapeColor`, `ShapeMaterial` and `Transparency` hold overlapping state that
`onChanged` keeps in sync by hand (`ViewProviderGeometryObject.cpp:154-175`).

## 4. The compatibility contract, measured

### 4.1 Python

Every upstream usage is index, iterate, or whole-assign:

    vobj.ShapeAppearance = (material,)          # assign a sequence
    mat = vobj.ShapeAppearance[0]               # index -> a Material copy
    wood = vobj.ShapeAppearance[0].DiffuseColor # index then field
    [m.DiffuseColor for m in vobj.ShapeAppearance]   # iterate

None of it requires contiguous material storage. Upstream's own
`getPyObject` already builds a `Py::Tuple` of **freshly copied** `MaterialPy`
objects, so Python never sees internal storage and in-place mutation of
`[0]` never wrote back even upstream. We construct each `Material` from the
field arrays on access -- same observable behaviour, same cost.

Since the alpha convention flip (section 7.9) the tuples agree with upstream
1.1 numerically as well: a colour's fourth component is an opacity. The one
divergence is deliberate: a Material's `DiffuseColor[3]` here reads
`1 - Transparency` -- the truthful value -- where upstream reports the
vestigial 1.0 their migration parked there. And one guard: a `DiffuseColor`
assignment whose alphas are ALL exactly 0.0 -- the pre-flip spelling of
opaque, which now spells invisible -- is read as opaque with a once-per-run
warning, unless the object already was fully transparent
(`PropertyDiffuseColor::guardLegacyAlpha`). Mixed and partial alphas pass
through untouched.

### 4.2 C++

Accessor usage across upstream's tree, counted:

| accessor | sites |
|---|---|
| `setDiffuseColor` | 38 |
| `getDiffuseColor` | 34 |
| `getValues` | 13 |
| `setValues` | 11 |
| `getTransparency` | 11 |
| `setDiffuseColors` | 7 |
| `getDiffuseColors` / `setValue` | 5 each |
| everything else | <= 4 each |

The API is dominated by per-field access, which maps to this layout
directly and gets *cheaper*. Only `getValues`/`setValues` want a whole
material vector -- 24 sites, all enumerable.

⭐ **`getValues()` is gone from this property, deliberately.** Not
deprecated, not made expensive: removed, so that the compiler names every
caller. Every way of keeping it is worse than not having it. A member cache
undoes the layout -- the first caller grows a 10,000 entry list from 200 KB
to 800 KB and holds it until the next write. A shared scratch buffer, the
`FC_STATIC` idiom this fork uses for exactly this shape of problem
(`PropertyLinkBase::isSame`), makes `a.getValues() == b.getValues()` quietly
compare one list against itself -- a silent wrong answer, and note that
`isSame` needed *two* buffers for precisely that reason. Returning by value
is correct but silently expensive at the call sites that look cheapest.

`setValues` stays: composing is the caller's problem, decomposing is ours.
Reads are `getMaterial(i)` for one whole entry, or the per-field accessors,
which touch nothing that is not already stored. The fork's three call sites
were converted with the change and all three got *simpler* -- two of them
wanted diffuse and transparency and nothing else. **This applies to
`PropertyMaterialList` alone**; every other list property keeps
`getValues()` exactly as it was.

### 4.3 Document format: two encodings, chosen by schema

> **Renumbered, 2026-08-16.** The compact format was folded from schema
> 6 into schema 5, and the schema 5 it replaced (shared included-file
> blobs) was never readable by an unaware reader either. **4 is
> upstream's format, 5 is this fork's**, and the default cap is 4. The
> numbers in this document have been renumbered to match; an earlier
> reading of it will say 6 where this says 5, and 5 where this says 4.

The existing one is kept exactly, byte for byte, and written at **schema 4**
-- the default, the one every other FreeCAD can read: `count="N"` and then
one line per entry of four packed colours, shininess and transparency, or
the same sequence in a doc file. It is written and read **sequentially by
index**, so it streams straight out of per-field arrays without
materialising anything.

At **schema 5** -- the fork's compact format, which already writes a root
element no other reader accepts -- each field is written once, at whatever
length it actually has: a `fields="1"` attribute and one line per non-empty
field in XML, a field mask and one length-prefixed run per field in a doc
file, where a count of `0xffffffff` stands in the place a legacy entry count
would occupy and cannot be mistaken for one. A uniform 2,000 entry list goes
from tens of kilobytes to about thirty bytes.

⚠️ **The inline-list rule has to be told which one is coming.**
`PropertyLists::Save` weighs `getMemSize()` against
`DocumentParams::InlineListSize` to decide between an inline list and an
archive entry, which is right for every list whose stored form *is* its
written form. This one has two written forms, and at schema 4 a list that is
16 bytes in memory is a quarter of a megabyte on the way out -- inlined
into Document.xml on the strength of the wrong number. Hence a new
`PropertyLists::getSaveSize(writer)` hook, defaulting to `getMemSize()`,
which this property answers per schema.

### 4.4 Back-compatibility we must keep

- `handleChangedPropertyName` converting `ShapeColor` and `ShapeMaterial` on
  restore, as upstream does.
- Old `DiffuseColor` restoring into `_diffuse`.
- The Python emulation of `DiffuseColor` and `ShapeColor` as custom
  attributes (upstream does this in `ViewProviderPartExtPyImp` and
  `ViewProviderGeometryObjectPyImp`), so existing macros keep working.
- Our own `MappedColors`, `MapFaceColor`, `MapLineColor`, `MapPointColor`,
  `MapTransparency` must keep behaving as they do now. This is the part with
  no upstream reference and it needs its own review.

## 5. Risks to settle before writing code

1. ~~**`getValues()` returns `const std::vector<Material>&`.**~~
   **SETTLED, by removing it** -- see the starred paragraph in section 4.2.
   The property does not inherit `PropertyListsT<Material>` (that base owns
   a concrete `_lValueList` and its readers are non-virtual); it implements
   `PropertyLists` directly, and the whole-list read is gone rather than
   faked. The 13 upstream call sites become 13 compile errors on the day
   upstream code is ported, which is the intended outcome.
2. ~~**Coin binding with mixed cardinality.**~~ **RESOLVED, and in our
   favour** -- see section 5.1 below. No expansion is ever required.
3. **The render cache.** `SoFCRenderCache` dedupes and hashes materials;
   per-face variance affects merge opportunities. Per-face colour already
   does this, so the question is whether widening changes it -- measure,
   do not assume.
4. Upstream's `Material` carries `image`/`imagePath`/`uuid` for texture and
   material-card identity. We keep the fields for format compatibility;
   whether the fork uses them is a separate decision.

### 5.1 Coin cannot vary four of the six fields per face at all

Settled by reading the Coin source, and it needs no probe because the
signature alone proves it:

    SoLazyElement::setMaterials(SoState*, SoNode*, uint32_t bitmask,
                                SoColorPacker*,
                                const SbColor* diffuse, int numdiffuse,
                                const float*   transp,  int numtransp,
                                const SbColor& ambient,
                                const SbColor& emissive,
                                const SbColor& specular,
                                float shininess, ...)

**Diffuse colour and transparency are pointer-plus-count. Ambient,
emissive, specular and shininess are single values** -- a `const SbColor&`
cannot carry N of anything. And `SoMaterial::doAction` passes exactly
`this->ambientColor[0]`, `this->emissiveColor[0]`, `this->specularColor[0]`
and `SbClamp(this->shininess[0], ...)` (`src/nodes/SoMaterial.cpp:530-537`).

So under `SoMaterialBinding::PER_PART`, Coin varies **only diffuse colour
and transparency** per part. Upstream's `ViewProviderPartExt` fills all six
`SoMaterial` fields with N entries; four of those arrays are stored, saved
to the document, uploaded to Coin, and then read at index 0 only.

Our own bgfx path inherits the same shape rather than escaping it:
`SoFCRenderCache` reads `SoLazyElement::getDiffuse(state, 0)` and the scalar
`getAmbient`/`getSpecular`/`getEmissive`/`getShininess`
(`SoFCRenderCache.cpp:442-451`), and its material record holds one
`uint32_t` each for diffuse, ambient, emissive and specular plus a single
`shininess`. Per-face variation there is carried as packed colours, exactly
as in Coin.

**Consequences for this design, all good:**

- The per-field layout maps 1:1 onto what both renderers consume. `_diffuse`
  and `_transparency` feed the pointer-plus-count parameters; the other four
  arrays are naturally size 1 and feed the scalar parameters. There is no
  expansion path to write, at bind time or anywhere else.
- In practice `_ambient`, `_specular`, `_emissive`, `_shininess`,
  `_image`, `_imagePath` and `_uuid` will be size 0 or 1 for every object,
  because nothing can render them per face. The realistic storage cost of
  `ShapeAppearance` on this design is therefore **exactly today's
  `DiffuseColor` plus a transparency array**.
- It also means per-face specular and shininess are not a feature we would
  be giving up by not copying upstream's layout. Nobody has it. Rendering it
  would need changes in Coin (which we fork) or in the bgfx backend, and
  that is a separate piece of work with its own justification.

### 5.2 No exporter consumes the extra fields either

An earlier draft of this document justified keeping the full material
per face partly on glTF export round-tripping it. That is wrong, and the
two halves have to be separated:

- **OCCT can.** `XCAFPrs_Style` carries a whole
  `XCAFDoc_VisMaterial` (`Material()` / `SetMaterial()`), and
  `RWGltf_CafWriter` merges faces into primitives keyed by style
  (`NCollection_DataMap<XCAFPrs_Style, Handle(RWGltf_GltfFace)>`), which is
  exactly how per-face materials are expressed in glTF, since glTF binds one
  material per primitive rather than per triangle.
- **FreeCAD does not feed it.** `ExportOCAF2` only ever calls
  `aColorTool->SetColor(...)`, and outside `ReaderGltf.cpp` there is no
  reference to `XCAFDoc_VisMaterial` or `VisMaterialTool` anywhere in
  `src/Mod/Import`. Export emits per-face *colours* into the colour tool.
  The OCCT capability sits unused.

So the tally for the four non-diffuse fields, per face: Coin does not render
them, the bgfx backend does not render them, STEP import never produces
them, and glTF export does not write them. The only things that consume a
full per-face material today are the appearance UI and the property's own
round-trip through the document.

### 5.3 Upstream knows, and has it filed as a bug

Not merely known -- reported, and still open. Issue #15181, "ShapeAppearance
does not handle emissive color per face correctly", describes the symptom
precisely: *the emissive colour of the first material in the list is used
for the whole object*. That is `emissiveColor[0]` in
`SoMaterial::doAction`, observed from the outside. Related: #14940 ("set
appearance per face is confusing"), #14938, #15170.

The forum answer to the same question is blunter: only diffuse is kept per
face, and there is no way to set the others per face from Python without
modifying the Coin node by hand.

Worth noting for us specifically: upstream **cannot** fix this cheaply,
because they consume Coin as an external dependency. We fork Coin. If
per-face specular or emissive is ever wanted here, extending
`SoLazyElement` to take those as pointer-plus-count -- the way it already
does for diffuse and transparency -- is open to this fork and closed to
them. That is an argument for the per-field layout on its own: the day
those become renderable, `_specular` and `_emissive` grow to N and nothing
above the storage changes.

## 6. The plan

Five stages, in the order they must land. Each is independently useful and
independently verifiable; none breaks the document format, which stays
upstream's throughout.

The invariant that holds it together: **storage is per-field arrays sized
0, 1 or N, and each stage simply lets more of those arrays legitimately
reach N.** Stage 1 makes N storable, stage 2 makes it survive traversal,
stage 3 makes it visible, stages 4 and 5 make it arrive and leave through
file formats. Nothing above the storage layer changes shape again after
stage 1.

### Stage 1 -- storage

`App::PropertyMaterialList` with the layout in section 3: the full upstream
API, upstream's save/restore format, per-field arrays underneath.

- Do not inherit `PropertyListsT<Material>` (risk 1): it owns a concrete
  `_lValueList` and `getValues()` is non-virtual. Implement the interface
  directly and serve `getValues()` from a `mutable` cache built on demand,
  dropped on write.
- `ShapeAppearance` on `Gui::ViewProviderGeometryObject`;
  `ShapeColor`/`ShapeMaterial` become compatibility accessors over it, which
  also retires the three-way hand-written sync at
  `ViewProviderGeometryObject.cpp:154-175`.
- `ViewProviderPartExt`: `DiffuseColor` becomes a view onto `_diffuse`.
  Reconcile the fork's `MappedColors` / `Map*Color` / `MapTransparency`.
- Restore-time migration (`handleChangedPropertyName`) plus the Python
  emulation of `DiffuseColor` and `ShapeColor`.

Verifiable without any view provider or GPU: unit tests for cardinality
transitions (0 -> 1 -> N and back), plus a document round-trip against a
file written by upstream FreeCAD.

**Landed 2026-08-12: the storage half.** `App::PropertyMaterialList` is the
layout above, with the per-field accessors, the two encodings of section
4.3, the `getSaveSize` hook and `tests/src/App/PropertyMaterialList.cpp`.
Normalisation is **lazy** -- a write marks the fields possibly denormal and
anything that compares or serialises asks for the normal form first -- so a
loop setting one entry at a time does not rescan the list on every step.
Growing a field only materialises it when the arriving value disagrees with
the one already there, which keeps an import that appends identically
coloured faces linear. The `DiffuseColor` accessor of 1.1 followed the same
day; what is left of this stage is the rest of the restore-time migration
(7.7 item 4).

**Landed 2026-08-12: the property.** `ShapeAppearance` replaces
`ShapeMaterial` on `ViewProviderGeometryObject` and, separately, on
`ViewProviderLink`, which is not a geometry object and carried its own copy
-- both had to move together because they share `setElementColorsTo`, whose
signature is now upstream's `PropertyMaterialList*`. `ShapeColor` and
`Transparency` stay their own properties but stop being a third overlapping
store: `onChanged` mirrors them off entry 0, and the Coin node is fed by one
`setCoinAppearance()`, only when the appearance holds a single entry.
`handleChangedPropertyName` folds an old document's `ShapeMaterial` in.

Three things this turned up that the plan had not:

- The blast radius is **~35 C++ sites in 12 files**, not the handful a
  member-access grep suggests. `(->|\.)ShapeMaterial` finds members *named*
  ShapeMaterial and misses bare `ShapeMaterial.getValue()`, misses
  `&ShapeMaterial` in `prop ==` comparisons, and a `grep -v pcShapeMaterial`
  filter silently drops every line carrying both names. Count with `grep -w`.
- **Two sites generated Python that assigned `ShapeMaterial.DiffuseColor`.**
  A material list hands Python a copy of each entry (section 4.1), so
  assigning into `[0]` compiles, runs and does nothing. Reads are fine as
  `ShapeAppearance[0].DiffuseColor`; the write now goes through the property
  in C++, as upstream's `CommandFeat.cpp` does.
- **Both material dialogs reach the property by name**
  (`getPropertyByName("ShapeMaterial")` plus a `dynamic_cast`). Those compile
  clean after a rename and silently edit nothing. Renaming a property means
  grepping the string, not just the symbol.

#### 1.1 `DiffuseColor` as a real accessor, not a copy

`DiffuseColor` stays a genuine `App::PropertyColorList` -- so the property
system, the editor, persistence and every existing C++ call site keep
working -- but a derived class redirects its storage into
`ShapeAppearance`:

    class PropertyDiffuseColor: public App::PropertyColorList {
        PropertyMaterialList* appearance;   // set by the view provider
        // readers and writers forward to appearance's _diffuse
    };

⭐ **And no new virtuals in `PropertyListsT`.** An earlier draft of this
section proposed making `getValues()`, `getValue()` and `operator[]`
virtual so that a derived colour list could redirect them. That is
inconsistent with the decision in section 4.2 -- having just refused to hand
back a reference to storage that is not there, it would be odd to add a
vtable slot to *every* list property in the tree so that one property can do
exactly that -- and it is also unnecessary:

- `DiffuseColor` is reached as `vp->DiffuseColor`, whose static type is the
  derived class, so the derived readers **hide** the base ones by name and
  no virtual dispatch is involved at any of the fork's 22 call sites.
- Everything that *does* go through a base pointer -- `Save`, `Restore`,
  `getMemSize`, `Copy`, `Paste`, `getPyObject`, `getSize` -- is already
  virtual, so persistence, undo and Python need nothing.
- The one remaining base-pointer reader is the property editor's
  `PropertyColorListItem` -- which, checked when this landed, **this fork
  does not have**. A colour list answers `getEditorName()` with the empty
  string, and `createPropertyItem` skips a property that does, so
  `DiffuseColor` has never had a row to hide. The editor question was
  upstream's, not ours.

Before writing it, grep for `PropertyColorList*` and
`freecad_dynamic_cast<...PropertyColorList>` and confirm the list is empty.
If some site genuinely needs base-pointer reads, that site is the argument
for a virtual -- not the design.

⭐ **And note which half of a base pointer is dangerous.** A third site
turned up beyond the two in 7.2 -- `ViewProviderTransformed` picks
`LineColorArray` or `DiffuseColor` into one `PropertyColorList*` -- and it
needed no change, because it only *writes*. Every write the base offers
funnels through the one virtual `setValues(ListT&&)`: `setValue(colour)`,
`setValue(list)`, `setValues(list)` and both Python paths all reach it, so
overriding that single function redirects all of them, through a base
pointer or not. It is reads that go to the wrong vector.

⭐ **This is where the per-field layout pays off a second time.** `_diffuse`
is literally a `std::vector<Base::Color>`, which is exactly the type
`PropertyColorList::getValues()` must return -- so the override hands back a
reference to the real storage. No cache, no materialisation, no second copy.
**Upstream cannot do this**: from an array of whole materials they would
have to synthesise a `vector<Color>` on every call, which is precisely why
they deleted the property and emulated it in Python only.

Our fork's C++ use is all direct member access and all of it survives:
`getValues` 22, `setValues` 20, `setValue` 2, `getSize` 1, plus one
`find`/`end` pair iterating the returned vector.

Details: route writes through the appearance's setters so change
notification is keyed on `ShapeAppearance` and the existing update logic
runs; and hide `DiffuseColor` from the property editor so one datum does not
appear as two rows.

**Landed 2026-08-12** as `PartGui::PropertyDiffuseColor` (`ce688581e7`),
with the two base-pointer sites restructured into an `ElementColors`
accessor in the same commit. Four things the plan had not:

- ⭐⭐ **A list's XML element is named after its type.** `PropertyLists::
  xmlName()` derives the element name from the type name with the `Property`
  prefix stripped, so a `PartGui::PropertyDiffuseColor` writes
  `<DiffuseColor>` where every older document says `<ColorList>` -- and
  `PropertyLists::Restore` reads the element **by name**, so every older
  document's colours would have been dropped, with a green build and no
  error. `xmlName()` is overridden back to `ColorList`. Any property that
  changes type has this problem; `ShapeColor` and `ShapeMaterial` escaped it
  only because `PropertyColor`/`PropertyMaterial` hard-code their element
  names.
- ⭐ **The property writes no values at all.** Its colours are the
  appearance's and the appearance saves them; writing them here too would
  double the largest thing a per-face import stores, for a value the restore
  would overwrite anyway (both names come back, and `ShapeAppearance` sorts
  after `DiffuseColor`). `Save` emits the empty-list element -- which is
  also what distinguishes a document written by this code from an older one
  whose `DiffuseColor` carries the values -- and `Restore` skips
  `PropertyLists::Restore`'s "no values means clear the list" branch, which
  here would empty the appearance about to be restored into.
- ⭐ **The restore of an older document must go through the property
  itself.** The type changed, so those documents arrive at
  `handleChangedPropertyType` (new override on `ViewProviderPartExt`), and a
  colour list past the inline threshold lives in its own archive member --
  read long after that hook returns, into whatever pointer was handed to
  `reader.addFile`. A stand-in `PropertyColorList` on the stack is dangling
  by then. Both encodings are covered by the probe.
- **An empty assignment is not an empty field.** `DiffuseColor.setValue()`
  has always meant "every face back to the object colour", while an empty
  field in the appearance means the *default material's* colour -- a
  different colour whenever the object has one of its own. The property
  therefore also knows `ShapeColor`, and an empty write becomes a uniform
  write of it.

And one behaviour that had to change with it, in `Gui` rather than here
(`9ce0a9d7a0`): entry 0 of a per-face appearance is one face, not the
object. Refreshing `ShapeColor` off it made `ViewProviderPartExt` collapse
the very colours that had just been applied, and `ShapeMaterial`'s
write-back replaced the whole list with one material -- which is exactly
what an old document does on restore, where `ShapeMaterial` arrives after
`DiffuseColor`. The mirrors now run only while the diffuse field is uniform,
and a whole-object material folds into the list instead of replacing it.

#### 1.2 Reading upstream files, and not pretending ours are readable

Two directions, and only one of them is a requirement:

- **We must read theirs.** Upstream and pre-1.0 documents are user data.
  Keep the whole restore path: upstream's `MaterialList` doc-file, the old
  `ShapeColor`/`ShapeMaterial` conversion via `handleChangedPropertyName`,
  and an old `DiffuseColor` colour list restoring into `_diffuse`.
- **They need not read ours.** The dedup schema in 1.3 is a fork format and
  is *not* upstream compatible. An earlier draft of this section proposed
  emitting a plain `DiffuseColor` colour list so current upstream could load
  our files, having verified that it would
  (`handleChangedPropertyName` accepts a `DiffuseColor` element of type
  `App::PropertyColorList`). That is no longer a design goal, so the save
  side is free to pick whatever encoding is cheapest for us.

⚠️ **Restore ordering still matters**, for reading upstream files.
Upstream needed `finishRestoring()` because `ShapeAppearance` is restored
*after* `DiffuseColor` and would otherwise overwrite it with its single
colour. Per-field storage replaces that workaround with a rule: restoring a
one-entry material writes size 1 into each field array and **must not
clobber a `_diffuse` that already holds N entries**. Test both file orders.

#### 1.3 Hook the shared-default scheme (it is on origin, not in this tree)

⚠️ Two earlier drafts of this section guessed at the wrong mechanism --
first `FileBlobManager`, then the `Render_*` dynamic-property pattern.
The real one is a workstream on `origin/LinkVibe` that this working tree
does not yet contain: `253a30b627`, `7a1f0a1a47`, `9f7c473d12`,
`2dfeea061f`, `6fa6cdd7b8`, `c2e1fffaf8`, `c7fce04db8`, `4306c09d65`,
`fd4437dd04`.

**How it works.** `App::SharedDefaults` records, per class, what each
eligible property of a fresh stand-in object serialises to at canonical
settings -- the file's schema and version, XML forced, no indentation.
Those recorded bytes are written once into a `<Defaults>` block inside
`<ObjectData>`, and every object is written as the difference. A property
is elided only when **its own serialisation is byte-identical to the
recorded bytes**, with status agreeing.

⭐ The equivalence is deliberately *the file*, not `isSame()`. An earlier
version elided by `isSame()`, which made every implementation load-bearing
for the format -- "one that answers same more loosely than Save() writes is
silent data loss" -- and two such bugs were found in the safe direction
before the rule changed. Restore fidelity now reduces to XML parse
fidelity.

**What appearance must do:**

1. **Opt in.** `Property::canShareDefault()` defaults to *no*; a type must
   opt in before a save may leave it out. Colours, materials and their
   lists already opt in, so our `PropertyMaterialList` must too --
   deliberately, not by inheriting an accident.
2. ⭐ **Serialise canonically.** Because elision compares *bytes*, two
   logically-equal appearances must produce identical bytes or the elision
   silently misses. That promotes the cardinality convention from an
   optimisation to a **correctness requirement**: always collapse a uniform
   field to size 1, never emit a size-N run of identical values, keep field
   order fixed. Normalise on write, not on read.
3. **Stay inline.** `4306c09d65` is precisely our case: after the view
   providers stopped writing defaults, **1831 archive entries were left
   carrying eight bytes each -- a `DiffuseColor` holding one colour**, on
   every object whose colour differs from its class default. An archive
   member costs "around 190 bytes before any content" plus one more thing
   for the reader to open. `PropertyLists::Save` now writes a list inline
   when `getMemSize()` fits `DocumentParams::InlineListSize` (64 bytes by
   default). So our per-field lists must report `getMemSize()` honestly and
   will then ride that path for free.

**This is the third independent argument for per-field lists, and the
sharpest.** An interleaved material list fails all three at once: one entry
is ~168 bytes, so it is over the 64-byte inline threshold immediately and
always costs a full archive entry; and because there is one blob to compare,
a single differing field makes the whole record differ from the class
default and nothing is elided. Per field, a lone non-default diffuse colour
is 16 bytes -- inline, no archive entry -- while every other field stays
byte-identical to the default block and disappears.

### Stage 2 -- Coin carries the information through, ABI intact

**Scope: make Coin a faithful carrier, not a renderer of per-face
materials.** Coin's own GL path may keep using index 0 for the four
non-diffuse fields; the bgfx backend is the work horse. What matters is that
the per-face arrays survive traversal and reach the render-cache callback.

Today they do not. `SoFCRenderCache` reads
`SoLazyElement::getDiffuse(state, 0)` and the scalar `getAmbient` /
`getSpecular` / `getEmissive` / `getShininess`
(`SoFCRenderCache.cpp:442-451`) because that is all `SoLazyElement` offers.
**The information is destroyed at the element boundary, not at the
renderer.**

**Do it without breaking ABI**, following the fork-extension pattern:

1. A new derived element, `SoLazyElementEx : SoGLLazyElement`, holding
   pointer-plus-count for ambient, specular, emissive and shininess
   alongside the inherited diffuse and transparency arrays. Existing class
   layouts are untouched.
2. Exported C entry points: an **install** call that registers the type and
   enables it on the relevant actions, plus one accessor per extra field,
   over opaque `SoState*` handles and plain arrays.

   Note there is **no per-instance factory and no destroyer**. Elements are
   not ref-counted and are not consumer-allocated: `SoElement` is a plain
   class, not `SoBase`-derived, and `SoState` creates exactly one instance
   per enabled element through `type.createInstance()`
   (`SoState.cpp:166`) and deletes them all in its destructor (`:191`). So
   the state owns them cradle to grave, and the consumer only ever *reads
   and writes* through the accessors. (Ref-counting would be the relevant
   rule if we ever added an `Ex` **node**, since those derive from
   `SoBase`.)
3. FreeCAD **looks those symbols up at runtime**. Present -> read per-face
   materials; absent -> exactly today's behaviour against stock Coin.

Why this is sound in Coin, verified rather than assumed:

- `PRIVATE_SOELEMENT_INIT` (`SoSubElement.h:132`) sets
  `classStackIndex = _parent_::getClassStackIndex()`, so a **derived element
  occupies the base's stack slot**.
- `SO_ENABLE(action, element)` -> `enableElement(typeId, stackIndex)`
  chooses which concrete class fills that slot, per action.
- **`SoGLLazyElement` is the in-tree precedent**: it is precisely a derived
  element substituted for `SoLazyElement` at the same stack index.
- Coin already exports C entry points over opaque handles (`COIN_DLL_API`,
  `cc_glglue_instance` and friends), so this follows its convention.

Consequences, all good: **no Coin ABI break, so no pivy rebuild and no
feedstock lockstep**, and one FreeCAD binary keeps working against stock
Coin. The cost is that the C surface, once published, has to stay stable --
so keep it small, and add a `..._abiVersion()` so the consumer can negotiate
a feature level rather than mere presence.

Coin-side writing: `SoMaterial`'s fields are already multi-value
(`SoMFColor`, `SoMFFloat`), so the node can already hold the data; only
`doAction`'s transfer collapses it to `[0]`. Teach it to pass whole fields
when the element in the state is an `Ex`. That is a body change, not a
layout change.

Deliberately **not** in scope: `SoGLLazyElement`'s send path. Per-face
diffuse is cheap there because it is a vertex attribute (`glColor4ub` under
`glColorMaterial`), whereas ambient, specular and emissive go through
`glMaterialfv` and shininess through `glMaterialf`, which are GL *state*
changes -- one per face would break batching, VBOs and display lists. That
is why Open Inventor only ever indexed diffuse, and it is a fixed-function
limitation we have no reason to fight. If Coin's own renderer ever needs it,
the right shape is to group faces into runs sharing a material and emit one
`glMaterialfv` set per run, the trick OCCT's glTF writer already uses.

Verify by reading the values back through the callback, not by looking at a
picture: Coin's own output is expected to be unchanged at this stage. Also
verify the fallback, by running against a stock Coin with the symbols
absent.

**Landed 2026-08-13** (coin `883456a76e` + the FreeCAD capture), verified
both legs by `tests/src/Gui/RenderCacheMaterial.cpp` -- a
`SoFCRenderCacheManager::traverse()` build over a PER_FACE scene with the
materials read back out of `getVertexCaches()`, no GL context anywhere,
plus a run against the pre-change libCoin for the fallback leg.
Deviations from the sketch above, found by reading the consumer first:

- **`SoLazyElementEx` derives from `SoLazyElement`, not
  `SoGLLazyElement`, and is enabled on `SoCallbackAction` only.** The
  render cache is built by a callback-action traversal, where the
  element at the lazy stack index is plain `SoLazyElement`; the GL
  substitution never runs there, and enabling a GL element on a non-GL
  action would be wrong. `SoEnabledElementsList::enable` replaces a slot
  when the new type derives from the occupant, and `SoAction` recreates
  its state when the global enable counter moves, so installing after
  actions already exist is sound.
- **The element is a plain public class.** It began as a C surface
  (`coin_lazyex_*`) bound with dlsym, so that one FreeCAD binary could
  still run against a stock Coin; the fork's binary rename to `CoinRT`
  (2026-08-14) ended that, since FreeCAD now refuses to start against
  anything but the fork anyway. Since **2026-08-15** the header is
  installed as `Inventor/elements/SoLazyElementEx.h`, FreeCAD calls
  `SoLazyElementEx::install()` at `SoFCDB::init` and reads the arrays
  off `SoLazyElementEx::getInstance(state)` -- an ordinary link-time
  dependency, with the type check that used to live behind the C getters
  now in `getInstance()`.
- **Staleness is handled in the element, not the consumer.** The `Ex`
  overrides of the scalar `set*Elt` virtuals drop the matching array, so
  a later scalar writer (`SoVRMLMaterial`, a bare `setAmbient`) cannot
  leave an earlier node's per-face array visible past it, and
  `setMaterialsEx` change-detects per field on the owning node's id.
- **Capture rides the existing points, no new flags.** The four arrays
  land in `SoFCRenderCache::Material` as `COWVector`s (`ambients` /
  `emissives` / `speculars` / `shininesses`, packed like their scalars,
  empty unless genuinely per-face): captured in `Material::init(state)`
  for state inherited from above the cache, and at the end of both
  `setMaterial()` overloads -- post callbacks run after `doAction`, so
  the element is already up to date there, with override and
  inheritance semantics resolved, which is why no per-field flag test is
  repeated. In `mergeMaterial` each array travels with its scalar under
  the same `canSetMaterial` test, and the arrays extend `operator<` so a
  per-face material never merges into a batch keyed on different
  arrays. A uniform object's Material compares bit-identically to
  before this stage, asserted by the test.
- Read-only accessors added for the readback: `SoFCRenderer::getScene()`
  and `SoFCRenderCacheManager::getSceneCache()`.

### Stage 3 -- bgfx renders it

Where per-face material is actually cheap: no GL state changes, just a
material index per primitive resolved in the shader.

- `SoFCRenderCache::Material` currently holds one `uint32_t` each for
  diffuse, ambient, emissive and specular plus a single `shininess`. Extend
  it to carry an optional per-part material index, keeping the single-value
  case exactly as it is so uniform objects change neither in size nor in
  code path.
- Feed the arrays picked up in stage 2 into the backend as a material table
  plus per-primitive indices. The existing per-face colour machinery and the
  TShape instancing work in `docs/TShapeRenderCache.md` are the model to
  follow.
- ⚠️ Re-check merge and dedup behaviour in the render cache (risk 3):
  per-face colour already fragments it, so measure whether widening changes
  anything rather than assuming it does.
- Verify with the A/B harness against the glr and Coin legs, remembering
  that only bgfx is expected to show per-face specular.

**Landed 2026-08-13**, verified headless by the stage-3 tests in
`tests/src/Gui/RenderCacheMaterial.cpp` (stream baked and split per
face, bridge flags the draw, uniform and same-valued-array scenes stay
scalar) plus a GUI render probe. Deviations from the sketch above, each
forced by reading the consumers:

- **Values are baked as a vertex stream, not a table plus per-primitive
  index.** Three code facts closed that door: the vertex dedup key
  (`SoFCVertexCacheP::Vertex`) has no part id, so vertices are shared
  *across* faces and any downstream expansion by part table would be
  ambiguous -- the resolution has to join the bake and the key, exactly
  like diffuse; WebGL2 has no `gl_PrimitiveID`, so a "per-primitive
  index resolved in the shader" arrives as a vertex attribute anyway;
  and the backend's GPU mesh cache is keyed by `cacheId`, so a stream
  must be a pure function of cache content, which a Material-side table
  is not (one shared cache can sit under two material contexts). Baked
  values also keep vertices shared between faces whose material *tuples*
  match, where an index would split them.
- **The stream is 8 bytes per vertex, not 16: ambient stays
  scalar-only.** No bgfx shading term reads `Material::ambient` (the
  headlight/PBR paths have no material-ambient; AO modulates light, not
  material), so a per-face ambient would be dead weight in every vertex.
  The stage-2 capture still carries the ambient array on the render-cache
  Material for any future consumer. Layout: rgba8 emissive, then rgb8
  specular with shininess (0..1) quantized into the alpha -- the same
  slot `u_matSpecular.w` already uses.
- **`SoFCVertexCache` does the bake** (`getMaterialArray()`), reading
  the Ex element at `open()` and
  resolving per `getMaterialIndex()` in the triangle callback, with the
  `colorpervertex` divergence pattern (`matpervertex`: capture enabled
  only when an array is genuinely present, array allocated only when
  resolved values actually diverge). Line/point vertices hold zeros --
  their draws never shade with these fields -- which costs a split only
  where a line vertex would have deduped against a face vertex.
- **Draw-level selection, not batch-level.** `Render::Material` gains
  one bool (`perfacematerial`), set per draw in the bridge: whole
  triangle draws of a stream-carrying mesh with the Material arrays
  still authoritative shade from the stream; partial (single-face)
  draws resolve `arrays[partidx]` into their scalars at translate time.
  "Authoritative" is the stage-2 invariant -- an override that replaces
  a scalar drops its array -- which the two highlight-tint sites in
  `buildHighlightCache` now honor too (`emissives.reset()` after
  writing the tint), so a preselected face keeps its per-face specular
  but shows the tint emissive.
- **The backend rides the color-stream pattern, minus the fallback
  buffer.** A fourth vertex stream (`MatVertex`: `Color1` emissive,
  `Color2` specular+shininess) bound only when the mesh carries one;
  the flag travels in the previously unused `u_matEmissive.w`, and the
  shader resolves `mix(scalar, stream, flag)` once -- no program
  permutations. Draws without the stream leave the attributes unbound,
  which bgfx's GL path resolves to the constant default attribute --
  finite values the mix multiplies out (checked in
  `renderer_gl.cpp`'s `bindAttributesEnd`). `fcShadeFragment` gained
  the resolved emissive/specular as parameters, with a trailing
  overload keeping the exact signature user material-stage shaders
  were written against (they shade per-face draws with the scalars).
- **Per-face-material draws are excluded from instancing**
  (`instancableDraw`), consistent with the TShape policy that already
  flattens per-face material divergence out of sharing (see
  docs/TShapeRenderCache.md §5) -- groups of two would never form.
- **Streaming**: the mesh chunk carries the stream behind a new flags
  bit and Material the flag byte (`kVersion` 47, `kChunkVersion` 5).
  The wasm viewer links the same backend, so nothing browser-specific
  changed. Generated coarse levels (MeshSimplify) drop the stream; the
  submit falls back to scalars when the upload is absent, so coarse
  rungs degrade to the uniform look instead of breaking.
- Known limits, accepted: under PBR the auto-roughness derives from the
  scalar shininess per draw (per-face shininess only shades the
  Blinn-Phong paths); per-face shininess is quantized to 8 bits (a step
  of 0.5 in GL-exponent terms at the top of the range); merge behaviour
  (risk 3) is unchanged for uniform scenes by the stage-2 bit-identical
  guarantee, and a per-face-material batch fragments exactly as a
  per-face-colour one already does.

### Stage 4 -- glTF, both directions

The format and OCCT both support this fully; only FreeCAD's wiring is
missing.

- **Import.** `ReaderGltf.cpp:112` already fetches the
  `XCAFDoc_VisMaterial` per face label, then keeps only `BaseColor()` and
  writes it into the colour tool -- its own comment explains why: *"the
  ImportOCAF(2) class expects color labels. Thus, the material labels are
  converted into color labels."* After stage 1 that downgrade is no longer
  necessary: carry the material through into `ShapeAppearance`.
- **Export.** Nothing in `src/Mod/Import` outside `ReaderGltf.cpp` so much
  as mentions `XCAFDoc_VisMaterial`; `ExportOCAF2` only calls
  `aColorTool->SetColor(...)`. Populate `XCAFDoc_VisMaterialTool` from
  `ShapeAppearance`. `RWGltf_CafWriter` then does the rest for free: it
  merges faces into primitives keyed by `XCAFPrs_Style`
  (`NCollection_DataMap<XCAFPrs_Style, Handle(RWGltf_GltfFace)>`), which is
  exactly how glTF expresses per-face materials, since it binds one material
  per primitive.
- Verify by round-tripping a multi-material glTF out and back in, comparing
  materials per face rather than pixels.

**Landed 2026-08-13** (`5d49c2cde7` + `08ce05a07d` + `f9c4c1d667`),
verified by a GUI probe: a hand-built three-primitive glTF (shared
metallic/roughness so nothing splits, per-primitive base colour and
emissive) imports to a single feature whose `ShapeAppearance` varies per
face, reaches the Coin material arrays, renders per-face in the bgfx leg
(distinct emissive channels; the plain Coin leg shows the uniform
`emissive[0]`, by design), and survives a glb export/import round trip
with diffuse and emissive bit-exact. CesiumMilkTruck still imports
through the split path with its textures. Deviations from the sketch:

- **The producer came first, and it was the missing half of stage 3.**
  Nothing had ever fed the per-face `SoMaterial` arrays from a real
  document: every apply site pushed `DiffuseColor`'s colour vector, and
  the whole-material overload of `setHighlightedFaces` had no caller.
  `applyShapeAppearance()` now dispatches on `variesOnlyInDiffuse()`;
  the material overload gained the per-face shininess/transparency
  arrays it never filled, and the divergence check that blocks
  instancing now includes shininess. The colour path pushes the
  non-diffuse scalars from entry 0 unconditionally (compare-and-set) --
  the base class only does it for a single-ENTRY appearance, so a
  multi-entry appearance with uniform extra fields would otherwise
  never reach the node.
- **Import is gated on variance, not presence.** Every glTF material
  converts to common (Phong) fields, so carrying them wholesale would
  re-skin every glTF import with OCCT's conversion of its uniform
  material. `scanFaceMaterials` builds the per-face vector but keeps it
  only when a field beyond diffuse *varies* across the faces; a uniform
  appearance keeps the colour path (and the whole-object Render_* PBR
  properties keep carrying the uniform case, as before). Diffuse and
  transparency ride the already-resolved colour labels; the other
  fields come from `ConvertToCommonMaterial()`.
- **The split path stays.** Materials differing in factors or textures
  still split one feature per material group -- Render_* is per-object
  and that was the point of the split -- but the common fields, above
  all emissive, which is deliberately not part of the grouping key, now
  ride per face *within* a group instead of being dropped.
- **Export inherits the object's PBR into the per-face materials.** One
  `XCAFDoc_VisMaterial` per distinct appearance entry, attached to face
  sub-shape labels (colour labels unchanged alongside); when the object
  carries Render_* PBR, its factors and textures are copied into each
  face material with base colour and emissive overridden, so a per-face
  appearance does not silently strip the textures.
- Known limits, accepted: specular does not round-trip exactly (a
  Common-only material is converted Phong-to-PBR on write and back on
  read; dominant channel and magnitude survive, the exact value does
  not -- exactness would need KHR_materials_specular, which OCCT's
  glTF code does not know at all); ambient is OCCT's conversion
  default (glTF has no ambient), so a mesh with materials on some
  faces and none on others reads as ambient variance and takes the
  material path harmlessly.
- **Lifted 2026-08-13: uniform emissive.** The variance gate first
  shipped dropping a uniform emissive (all faces one lit material) to
  avoid re-skinning ordinary imports. That lost light with no other
  channel to carry it -- emissive is the one common field with an
  unambiguous default (black) whose non-default value is always
  meaningful. The gate now also accepts a uniform lit emissive:
  import falls back to the shape label's material when no face label
  carries one (a single-primitive mesh, or a whole-object style
  merged by our own exporter), the Gui importer still collapses the
  uniform list to a single entry, and export writes one visualization
  material on the object label whenever entry 0's emissive is lit
  even though the appearance varies only in diffuse. Uniform specular
  and shininess still keep the colour path -- they only re-skin what
  the default look approximates. Verified: emissive 0.9/0.3/0.1
  imports and round-trips bit-exact through glb.

### Stage 5 -- STEP, both directions

Feasible on both ends. OCCT already has every primitive; the reader simply
discards what it builds.

- **Import.** `STEPCAFControl_Reader` constructs a
  `STEPConstruct_RenderingProperties` per style and then keeps only
  `GetRGBAColor()`. That class already offers `IsMaterialConvertible()` and
  `CreateXCAFMaterial()`, which returns an `XCAFDoc_VisMaterialCommon`. Call
  them and populate `XCAFDoc_VisMaterialTool` instead of collapsing to a
  colour. This is an OCCT fork change and a good upstreaming candidate.
- **Export.** `STEPCAFControl_Writer` already takes a `theVisMaterialMode`
  and writes `STEPConstruct_RenderingProperties` built from
  `aStyle.Material()` when it is set. Mostly a matter of feeding styles that
  carry materials and enabling the mode.
- STEP's `surface_style_rendering` carries the reflectance and shininess
  this needs, so the format is not the constraint.
- ⚠️ Keep the fork ABI-compatible with upstream OCCT here, per the standing
  rule; this change is additive to a reader body, so it should not need new
  members.
- Verify against a STEP file exported from another CAD package with per-face
  finishes, not only against our own output.

**Landed 2026-08-13** (occt `482e433ba5` + `4eb39a55d1` on LinkVibe-801,
fcad WriterStep commit), verified by a GUI probe: a box with six
per-face materials exports six `surface_style_reflectance_ambient_
diffuse_specular` entities and re-imports with per-face shininess and
specular factor exact; a textually mutated copy (exponents rewritten,
foreign header) parses to the mutated values, so the reader takes
foreign-authored files, not just our own bytes; a plain colored STEP
(synth_small) imports unchanged. The FreeCAD import side needed
nothing: the reader populates `XCAFDoc_VisMaterialTool` and stage 4's
`scanFaceMaterials` picks it up. Deviations from the sketch:

- **The reader gate is "any reflectance property", not
  `IsMaterialConvertible()`.** That predicate demands all five
  properties at once, which real writers rarely emit together;
  `CreateXCAFMaterial()` fills what is missing with defaults. A style
  carrying only colour+transparency still collapses to a colour, as
  before.
- **The writer needed two upstream bug fixes, not just the mode.**
  `SetVisualMaterialMode(true)` acted on nothing: the style settings
  collection never called `XCAFPrs_Style::SetMaterial` (document
  materials were invisible to it), and the two reflectance entities
  were half-registered in `RWStepAP214_ReadWriteModule` -- readable in
  the switch but with no keyword constants, no typenum binds, no
  `StepType` case and no `WriteStep` case, so they serialized as
  `?()`. Both fixed; both are upstreaming candidates.
- **`Init(Common)` now always defines the full reflectance model.**
  It used to define each property behind ratio heuristics while
  `CreateRenderingProperties()` writes the specular block only when
  every property is defined at once -- most materials silently lost
  specular and shininess on write. The explicit specular colour is
  kept, so specular round-trips exactly; ambient degrades to a factor
  of diffuse (STEP's model).
- Known limits, accepted: STEP's reflectance model has no emissive, so
  per-face emissive does not survive STEP (glTF carries it); ambient
  is a scalar factor of the surface colour; all OCCT changes are
  ABI-compatible body changes (a static helper and string constants,
  no new members).

### What lands when

Stage 1 alone is worth having: it removes the redundant property trio, and
costs less memory than what we do today. Stage 2 is small and additive.
Stage 3 is the first stage a user can see. Stages 4 and 5 are what make the
data come from and go somewhere other than our own documents -- and stage 5
is the one that fixes the case that motivated all of this, an imported
solid whose faces carry real finishes.

## 7. What has to be settled before the rest of stage 1 lands

An audit of where this fork and upstream differ in ways that reach
appearance, done after the storage landed and before `ShapeAppearance`
itself. Each item gets a verdict, and only two of them are blocking.

### 7.1 Do `Base::Color` first -- and it has a trap

⛔ **Blocking, and it is the reason to do UpstreamCoreSync 2d before
anything else here.** Every line of this work is spelled in colours. Landing
`ShapeAppearance` first means writing `App::Color` into new code and
rewriting it days later.

The plan calls 2d a file move plus
`namespace App { using Color = Base::Color; }`, and for 836 of the 843 sites
it is. The other seven are **forward declarations**:

    src/Gui/ViewProvider.h            src/Mod/Part/App/TopoShape.h
    src/Mod/Mesh/App/Importer.h       src/Mod/Mesh/Gui/ViewProvider.h
    src/Mod/Points/App/PointsFeature.h
    src/Mod/TechDraw/App/DrawHatch.h  src/Mod/TechDraw/App/Preferences.h

each saying `namespace App { class Color; }`. A using-alias and a class
declaration of the same name cannot coexist, so those seven move to
`namespace Base` in the same commit or nothing compiles. Cheap, but it is
not the zero the plan implies.

### 7.2 Two sites read a colour list through a base pointer

⛔ **Blocking for 1.1**, and it is the counterexample the design asked for.
Section 1.1 argues no virtual reader is needed because `DiffuseColor` is
always reached through its own static type. Two sites in
`src/Mod/Part/Gui/ViewProviderExt.cpp` (around lines 2119 and 2255) do the
opposite deliberately: they pick one of `DiffuseColor`, `LineColorArray` and
`PointColorArray` by element type into a single `PropertyColorList*` and
read through it. Under a `PropertyDiffuseColor` whose storage lives
elsewhere, that reads an empty base vector -- silently, for faces only.

Both are in one file and both already switch on `TopAbs_ShapeEnum`, so the
verdict is **restructure the two sites, not virtualise the readers**:
`getValues()` and `operator[]` are inlined in link resolution and other hot
paths, and paying an indirect call across every list property in the tree to
spare one file a branch is the wrong trade. The compiler will find both
sites -- `auto prop = &vp->DiffuseColor` stops accepting the other two
assignments the moment the type changes.

✅ **Done 2026-08-12** (`ce688581e7`): both now go through an
`ElementColors` accessor that switches on the element type they already had.
A third site (`ViewProviderTransformed`) does the same thing and needed no
change -- it only writes, and writes are virtual all the way down (1.1).

### 7.3 The material port -- done, minus the half that changes behaviour

✅ **Done 2026-08-12, and it was right to split it.** `App::Material` now
has upstream's `image`, `imagePath` and `uuid`, and their equality rule that
two appearances naming the same card are the same appearance whatever their
colours say -- inert until something sets a uuid. Their `MaterialList` doc
file at `version="3"` turned out to be, byte for byte, our own first pass --
count, then four packed colours, shininess and transparency per entry --
followed by a **second pass** of three length-prefixed strings per material.
So this fork now reads their files, strings included, and writes that shape
itself the moment an appearance has a texture or a card to name. A test
builds those bytes by hand rather than through our own writer, because the
claim is about their layout, not ours.

⛔ **Not taken, and it needs a decision of its own: upstream's default
material is a different material.** Theirs is `shininess 0.9` and type
`DEFAULT`; ours is `0.2` and `STEEL` colours under `USER_DEFINED`. Taking it
would change the look of every object that has never had its appearance set
-- and, now that a size-0 field means "the default", it would also change
which appearances serialise to nothing and therefore which ones the
shared-default scheme elides. That is a fork behaviour change wearing an
additive port's clothes, and policy says it does not ride in silently
(UpstreamCoreSync section 0).

Also not taken: `Material::getDefaultAppearance()`, which reads View
preferences directly. This fork already does that through `ViewParams` in
`ViewProviderGeometryObject`; porting it would put a second, disagreeing
copy of the same policy in `App`.

### 7.4 `uuid` points into a Materials module we do not have

**Not blocking; keep it opaque.** `uuid` ties an appearance to a material
card in upstream's Materials module, which this fork has at the
December-2023 vintage against upstream's 346-file divergence
(UpstreamCoreSync 5.2). Carry the string through the format and do not wire
it to anything until that module question is answered on its own.

### 7.5 The fork-only half has no upstream reference

**Not blocking, but it is the actual work of 1.1.** `MappedColors`,
`MapFaceColor`, `MapLineColor`, `MapPointColor` and `MapTransparency` are
ours alone, and `ViewProviderPartExt::onChanged` already defers to
`DiffuseColor` during restore *because* the order in which `DiffuseColor`
and `ShapeColor` come back depends on whether the colour list was written
inline or into its own file. Storage that answers to both names has to keep
that behaviour exactly; it is the one part of stage 1 with nothing to copy
from.

### 7.6 Settled, listed so they are not re-opened

- `App::Property::isSame` is pure virtual in this fork where upstream gives
  it a default body. Every fork property answers it deliberately, including
  the new material list. No action.
- The `ShapeColor` / `ShapeMaterial` / `Transparency` trio kept in step by
  hand in `ViewProviderGeometryObject::onChanged` is retired by
  `ShapeAppearance`, not worked around.
- Coin cannot vary four of the six fields per face (section 5.1) and no
  exporter consumes them (5.2). Both were settled by reading the consumer
  and neither has changed.

### 7.7 Order

0. ~~Port upstream's material fields~~ -- done (7.3).
1. ~~UpstreamCoreSync 2d, `App::Color` -> `Base::Color`~~ -- done
   2026-08-12 (935bccd118), seven forward declarations included (7.1).
2. ~~`ShapeAppearance` on `ViewProviderGeometryObject`~~ -- done 2026-08-12
   (9ebb513d35), and on `ViewProviderLink` too, which the plan had not
   noticed carries its own material.
2b. ~~`ShapeColor` and `ShapeMaterial` as names over the appearance~~ --
   done 2026-08-12 (f3a127c351). Chosen over deleting them (upstream's
   shape) so the editor row, the C++ call sites and existing macros all
   keep working; storage is the appearance, the inherited value is a
   mirror of entry 0. Three restore traps, all silent, are recorded in
   section 7.8.
3. ~~`DiffuseColor` as an accessor (1.1)~~ -- done 2026-08-12
   (`ce688581e7`), with the base-pointer sites restructured in the same
   commit (7.2) and the fork-only mapping behaviour kept (7.5): the
   transparency in a face colour's alpha, the collapse on a `ShapeColor`
   write, and `updateColors`' `touch()` all behave as before, now keyed on
   `ShapeAppearance`. Stage 1's storage half is complete.
4. ~~Restore-time migration, including upstream's `version="3"` second pass
   as a lossy read (7.3)~~ -- done 2026-08-12. The second pass is read in
   full rather than lossily (7.3), an old document's `DiffuseColor` restores
   under both encodings, a `ShapeMaterial` arriving after it folds instead of
   replacing (`9ce0a9d7a0`), and the one direction that was still wrong --
   reading a document from **after** upstream inverted what a colour's alpha
   means -- is 7.9.

### 7.9 The alpha component: the fork adopted upstream's meaning

*Rewritten 2026-08-13; the first landing kept the fork's old convention and
converted 1.1 files the other way. That reading is gone.*

⭐ **Found reading their reader, not their writer.** Upstream carries
`requiresAlphaConversion` on four property classes and a
`readerRequiresAlphaConversion(reader)` that answers
`Base::getVersion(reader.ProgramVersion) < v1_1`: **before 1.1 a colour's
alpha component held transparency, and from 1.1 it holds opacity.**

The fork now means opacity too, everywhere in memory -- decided after the
audit found both conventions live in the tree at once (fork code writing
transparency, ported upstream code writing opacity, one type, no marker; see
the decision record in `Base/Color.h`). **Documents did not move**: every
file this fork writes still stores transparency in the alpha byte, readable
by every older build and by pre-1.1 upstream, so the conversion happens at
the boundary in BOTH directions -- Restore converts legacy files (upstream's
own rule), Save converts on the way out (`Base::writerAlphaIsOpacity`, false
while `PACKAGE_VERSION` is below 1.1, self-correcting the day it is not).
`Base::alphaIsOpacity` gates the read side, asked by the same four
properties upstream converts -- `PropertyColor`, `PropertyColorList`,
`PropertyMaterial`, `PropertyMaterialList` -- in both encodings each.

The legacy `DiffuseColor` element goes further: while the appearance varies
nothing but its diffuse field (`variesOnlyInDiffuse`), the element is written
**with its values**, alpha as transparency, so a pre-ShapeAppearance FreeCAD
still opens the file with its face colours. A per-face import pays for its
colours twice in exchange; when the appearance holds what a colour list
cannot say, the element is a placeholder and nothing lossy is written.

And the storage collapsed behind it: **there is no `_transparency` array**.
A face's transparency is `1 - _diffuse[i].a`, the invariant the two stores
used to break -- the container-detach hack in the Transparency handler and
the per-path choice of which store to render from both fell out. A whole
`App::Material` still carries both slots, so composition picks one: **the
transparency field wins** (their renderer reads only it; their migration
parks 1.0 in the alpha). On restore the winner depends on the era -- a
legacy file's two transparency-meaning slots merge by max (link override
lists kept the truth in the field, old colour lists in the alpha, synced
files in both), a 1.1 file's field is the sole truth -- see
`PropertyMaterialList::restoreValues`.

Three things this turned up, each of which would have failed silently:

- ⭐ **The gate has to fail closed, and upstream's cannot be reused as-is.**
  Their `getVersion` returns `v1_x` for any string it does not recognise,
  which is *newer* than every name it knows. That is the safe answer for
  their test (`< v1_1`) and the dangerous one for ours: `pre-0.14`, the
  stand-in a reader fills in when a document states no version at all, would
  classify as post-1.1 and every colour in the oldest files there are would
  come back inverted. `alphaIsOpacity` parses the leading `major.minor`
  numerically instead, so unrecognised means "do nothing" -- and a release
  newer than any table still answers yes.
- ⭐⭐ **The Gui document reader knew no version, and that is where all of
  these properties live.** `Gui::Document::RestoreDocFile` builds its own
  `XMLReader` over GuiDocument.xml and set `DocumentSchema` and `FileVersion`
  on it but never `ProgramVersion` -- which upstream does set, one line. Every
  appearance, colour and material is a view provider property, so without
  that line the whole gate reads an empty string. Note that the archive-entry
  paths would have worked anyway (a registered entry is served by the outer
  parser, which has the attribute), so this is another case of an encoding
  deciding whether a restore is correct.
- ⭐ **For the material list the conversion is a merge, not an inversion.**
  Upstream renders per-entry transparency out of the `transparency` field and
  ignores the diffuse alpha entirely (`setHighlightedFaces` reads
  `materials[i].transparency`), and their own pre-1.1 migration writes 1.0
  into that alpha for every entry -- so a 1.1 file's field is its truth and
  inverting the component instead would make every face opaque. A legacy
  file's truth may sit in either slot (see 7.9), hence the max-merge in
  `restoreValues`. The other three colours are inverted either way, being
  decorative on both sides but stored.

⭐⭐ **And the version was not the only thing missing: nothing derived the
compatibility names.** A 1.0-or-later upstream document states no
`ShapeColor`, no `ShapeMaterial` and no `Transparency` -- the appearance
replaced all three -- so those properties stay at whatever the constructor
left, and a blue half-transparent object opens with a grey ShapeColor and
Transparency 0 in the property editor. The mirror `onChanged` does is not
enough, measured rather than assumed: an appearance read from its own archive
entry arrives correct and leaves both stale, while the same write *after* the
restore mirrors normally. So `ViewProviderGeometryObject::finishRestoring`
re-derives them, once, after every value the file carries has landed --
`refreshAppearanceMirrors`, which is a no-op for every document this fork
wrote (all four are stated, all four already agree) and the whole migration
for one that has only the appearance. Transparency goes first: the reaction to
a ShapeColor write folds it into the diffuse alpha.

Upstream's hidden `_diffuseColor` -- a real `App::PropertyColorList` member,
never registered and never written, that `handleChangedPropertyName` restores
an old `DiffuseColor` element into so that `onChanged` can later split its
alpha out into `setTransparencies` -- has **no counterpart here and needs
none**. It exists because their storage cannot hold what that alpha means;
ours holds exactly that (the alpha IS the entry's opacity), so the element
restores straight into the property that names the appearance's diffuse
field (1.1), converted at the gate like every legacy colour.

The other direction still works for free: this fork's documents state
`0.22R<rev>` and store transparency in the alpha, so a 1.1 reader classifies
them as pre-1.1 and converts them correctly, without either side agreeing to
anything. `Base::writerAlphaIsOpacity` is what keeps that true -- the writer
converts back to the legacy bytes precisely so the stated version keeps
describing them. Both halves move together the day `PACKAGE_VERSION` reaches
1.1; the note in `Base/ProgramVersion.h` says how.

Verified by `fcad-probes/upstream_alpha_probe.py`, which crafts BOTH eras'
files rather than writing them: upstream's `version="3"` archive entry
packed by hand at 1.1, and the same shapes packed the legacy way under this
fork's own version. The sharpest check is convergence -- different bytes,
different conversion, the same in-memory appearance -- since there is no
longer any version whose colours are left alone. A version nothing
recognises must take the legacy reading (fail closed: unreadable means old),
and a round trip through this build's own writer must both return what was
set and leave transparency-in-alpha bytes on disk.

### 7.8 What restore actually does, measured

Written after 2b, because every one of these fails with a green build and
no error.

**`Transient` is not "save nothing, restore normally".**
`PropertyContainer::Restore` skips a transient property on **restore** too
(PropertyContainer.cpp:624), on the property's own status or the file's.
So keeping a name and marking it transient to avoid writing the datum
twice would discard the value in every existing document. There is no
save-only exclusion flag; the values are still written.

**Three routes, two empty stubs.** Per `<Property>` element the container
picks: name and type both match -> `prop->Restore()`; name matches and the
type does not -> `handleChangedPropertyType`; name not found ->
`handleChangedPropertyName`. Both hooks default to doing **nothing**, so a
retyped or renamed property loses its old value unless the class overrides
the right one. Retyping ShapeColor and ShapeMaterial routes them to the
first hook; ViewProviderLink, whose own ShapeMaterial was renamed away in
9ebb513d35, needs the second and had none -- upstream has that hook and it
had been missed.

**Save order is lexicographic by name**, because `PropertyContainer::Save`
iterates a `std::map<std::string, Property*>`. Confirmed from a real
GuiDocument.xml rather than from reading the writer:

    DiffuseColor < ShapeAppearance < ShapeColor < ShapeMaterial < Transparency

So the coarse value restores **after** the specific one, which is why
applying a single colour is refused when the appearance already holds per
face data. Note ShapeMaterial lands after ShapeColor, so the richer
material wins entry 0 -- the same outcome upstream's migration produces.

**The push cannot live in the derived setValue alone.** `setPyObject` and
`Paste` call `setValue` non-virtually, so a Python assignment updates the
mirror and never reaches the appearance. It belongs in `onChanged`, which
every write path reaches, guarded so the mirror-back cannot ping-pong.

**View provider properties are in GuiDocument.xml**, not Document.xml. A
probe that rewrites the wrong file changes nothing and still reports PASS,
which is how the first run of this probe lied.

Three more, found landing `DiffuseColor` (1.1):

**A list property's XML element is named after its type.** Renaming or
retyping a list renames the element, and the restore reads it by name, so
every older document loses that property in silence. Override `xmlName()`.

**A retyped property whose values live in their own archive member has to
restore through itself.** `handleChangedPropertyType` gets the reader
positioned at the element, but a `file=` element only registers a pointer;
the read happens after the whole XML pass. A stand-in on the stack is
dangling by then, and the values land in freed memory.

⭐ **The order alone does not protect per-face data -- the hooks have to.**
`ShapeMaterial` sorts after `DiffuseColor`, so on every old document it
arrives last and, before `9ce0a9d7a0`, replaced the whole appearance with
its single material. The inline case failed while the archive-member case
passed, purely because the member is read after the XML pass and so after
`ShapeMaterial` had done the damage: **an encoding that changes only *when*
a value arrives can be the difference between a passing and a failing
restore**, so a restore probe has to exercise both.

## 8. PBR mode: the same arrays, reinterpreted

Decided by the user 2026-08-13: extend `ShapeAppearance` with a bool that
toggles the list between the Phong reading it has always had and a PBR
reading. It stays efficient because of the storage design above -- no new
field arrays, no document format change. "Newer software reinterprets
those fields."

### 8.1 The mapping

| array        | Phong reading        | PBR reading                        |
|--------------|----------------------|------------------------------------|
| `_diffuse`   | diffuse + opacity    | base colour + opacity (unchanged)  |
| `_shininess` | shininess            | roughness, full float precision    |
| `_specular`  | specular colour      | F0 tint, metallic in the ALPHA     |
| `_emissive`  | emissive             | emissive (unchanged)               |
| `_ambient`   | ambient              | no PBR meaning, ignored            |

Metallic rides the specular alpha (user-picked over an ambient channel).
The legacy alpha-convention flip applies to every colour field's alpha
symmetrically on save and restore, so it round-trips; the one thing that
must never happen is special-casing that alpha out of the conversion.
8-bit quantization on the way through a document is accepted (glTF's own
factors are 8-bit textures at heart). Roughness in the float slot is
exact.

### 8.2 Mode defaults, and why they differ

The Phong default specular is STEEL-ish with alpha 1 -- read as PBR that
would spell a fully metallic surface. So in PBR mode an EMPTY `_specular`
field reads as white tint with metallic 0, and an empty `_shininess`
slot as roughness 0.5 (`specularDefault()` / `shininessDefault()`). The
collapse baselines follow the mode, deterministically, because the mode
is part of the serialized identity; growth (`setSize`, `setField` on an
empty list) fills with the mode's own defaults so a PBR list never
materializes metal it was not given.

### 8.3 Serialization

- Binary field stream: bit 10 of the `uint16` field mask (`FieldPBR`).
  A bit has no payload; old readers only test the bits they know.
- XML field form: a `pbr="1"` attribute beside `fields="1"`. An
  attribute is ignored by old readers; a new KEY LINE in the char stream
  would throw "unknown material field" on old fork builds.
- The compatible encodings (schema < 5 stream and inline XML) cannot
  carry the flag, so they are written as the PHONG DERIVATION
  (`getPhongMaterial()`): diffuse STAYS the base colour (the bgfx PBR
  path reads its base colour out of the diffuse slot, so zeroing a
  metal's diffuse would shade it black there -- and a Phong metal shown
  as a shiny colour is the better degradation anyway), specular =
  mix(0.04 * tint, base, metallic), shininess from the roughness by the
  inverse of the Blinn-Phong-to-GGX fit (`Material::roughnessToShininess`,
  saturating below roughness ~0.124). Old builds see the look the values
  most nearly mean; re-saving there keeps values, drops the mode.
- Every restore path that cannot state a mode resets it to Phong.
- `isSame`/`Copy`/`Paste` carry the bool; the mask bit / attribute keeps
  a PBR list from ever eliding against a same-valued Phong one under the
  shared-default scheme.

### 8.4 Consumers

- **Coin GL leg**: the SoMaterial arrays always carry the Phong reading
  (`applyShapeAppearance` and the base class push convert through
  `getPhongMaterial`), so plain GL display is sensible everywhere. With
  the diffuse staying the base colour, the cheap diffuse-only colour
  path serves PBR lists whose other fields are uniform -- metals
  included -- and the instancing color-variant path keeps working.
- **The mirrors stay raw**: `ShapeMaterial.mirrorValue(getMaterial(0))`
  writes back into the appearance on change; a derived mirror would
  quietly convert the stored values.
- **bgfx, uniform leg**: `updateRenderMaterial()` feeds the appearance's
  entry-0 metallic/roughness into the `SoFCRenderMaterial` capture node.
  A PBR-mode appearance BEATS the `Render_Metallic`/`Render_Roughness`
  dynamic properties (user decision: they predate it as the only way to
  state these; effect params -- water/glass/fire/... -- are untouched).
  The renderer's existing per-object override path does the rest
  unchanged. Roughness is floored at the shader clamp 0.02 because the
  renderer reads <= 0 as unset.
- **bgfx, per-face leg (landed 2026-08-13)**: the factor pair rides the
  per-vertex material stream's two spare alpha slots -- the metallic
  where the emissive alpha is a constant FF, the roughness where the
  specular alpha carries the shininess that the PBR shading branch does
  not read, at the 8-bit quantization that slot has always had. What it
  does NOT ride is the coin fork. The sketch here had the arrays
  extending `SoLazyElementEx`, but reading the consumer first says
  otherwise: metallic and roughness are not Coin material fields, they
  already come from a FreeCAD node (`SoFCRenderMaterial`, which carries
  the uniform pair), and FreeCAD already defines its own elements on
  the callback action (`SoFCDiffuseElement`). So the pair travels in
  `SoFCPbrElement`, a FreeCAD element written by that node's
  `doAction`, and the coin fork is untouched -- no coin rebuild, no
  pivy rebuild, no feedstock lockstep.
  - The producer is `updateRenderMaterial()`: a PBR appearance whose
    entries genuinely differ in the pair fills `metallics`/`roughnesses`
    on the node (each roughness floored at the shader clamp, like the
    scalar), and clears them when they do not -- so a uniform PBR
    object is what it was before. Indexing follows the APPEARANCE, not
    the face count, so a face past the end reads ENTRY 0, the padding
    rule `setHighlightedFaces` uses, rather than clamping to the last
    entry the way the colour arrays (which are as long as the shape has
    faces) do.
  - `SoFCVertexCache` reads that element beside the lazy element's
    arrays and bakes whichever reading applies. The pair alone
    allocates the stream -- an appearance can differ per face in
    nothing else -- and the existing divergence test still decides
    whether a stream is needed at all.
  - The draw states which reading its stream carries
    (`Material::perfacepbr`), taken from the cache that baked it rather
    than from the material, since the bake is the authority. The submit
    lifts `u_matEmissive.w` from a flag to three states (0 = scalars,
    1 = the Phong reading, 2 = the PBR one) and the fragment shader
    resolves the pair into its metal/rough factors. With PBR shading
    off for the frame that alpha would be read as a shininess, so the
    Phong branch converts the roughness back through the inverse of the
    Blinn-Phong-to-GGX fit -- the value the stored material's own Phong
    derivation holds.
  - A single-face draw carries no stream, so it resolves its face's
    pair into the scalars, out of `Material::metallics`/`roughnesses` --
    the array form captured beside the existing four.
  - Scene dump v48 carries the flag: an older viewer reads the stream
    the Phong way, which is the uniform-PBR look it showed before.
- **glTF, exact both ways (landed 2026-08-13)**: when every material a
  label uses has the PBR definition (glTF always), import stores the
  raw factors -- metallic into the specular alpha under a white tint,
  roughness into the shininess slot -- and the appearance goes PBR
  mode; a PBR list is always meaningful (no variance gate), since its
  factors have no colour-label channel. The emissive still reads
  through OCCT's Common conversion, keeping the stage-4 colour-space
  conventions and their verified round trip. Export writes the PBR
  part of the VisMaterial from the raw slots and the Common part from
  `pbrToPhong` -- which is what the STEP writer's reflectance model
  picks up, so STEP export converts to Phong with no code of its own.
  STEP import stays Phong mode (reflectance materials have no PBR
  definition). Found and fixed on the way: the untested Render_*
  export leg wrote the stored sRGB emissive floats straight into the
  linear glTF emissive factor -- every reimport would gamma-shift it.
- **The Python side (landed 2026-08-13)**: the mode rides the material
  VALUES. `App::Material` carries a `pbr` tag -- part of its equality,
  not of its storage -- and `getMaterial()` stamps the list's mode on
  every value it hands out, so `ShapeAppearance[0].PBR` answers, and
  `Metallic`/`Roughness` read mode-aware off the value (a Phong value
  has no metals and derives its roughness).

  On a value the mode is an ORDINARY ATTRIBUTE and its setter
  CONVERTS -- `Material::setPBR`, the value-level twin of the list's
  `convertPBR` -- so `mat.PBR = True` keeps the surface looking like
  itself. Writing a PBR quantity converts too: `mat.Metallic = 1`
  states a metal, and landing that number in the Phong slot it would
  otherwise occupy only to have the next conversion throw it away is
  worse than deciding the mode. Nothing demands the mode any more. The
  raw reinterpret stays reachable, because setting the mode a value is
  already in converts nothing: `Material(PBR=True)` then writing the
  slots states factors the caller already holds. In the constructor the
  mode applies FIRST whatever the keyword order, so the slots the other
  keywords name mean what they were written for.

  A LIST HOLDS ONE MODE, and a whole-list assignment states it through
  the material it starts with; every further entry is converted to that
  reading (`inMode`), as are an indexed write and a growth filler,
  which cannot restate a whole list's mode. An empty assignment states
  nothing and the mode it finds stands. So a tuple read from one object
  carries its mode to the next, a plain `Material()` list is Phong, and
  a list is never half one model and half the other. There is no dict
  spelling of the mode: it would only shadow the integer-keyed dict
  that every list property takes as an indexed write.

  Two C++ paths deliberately do NOT restate the mode, because they
  cannot express one. `applyWholeMaterial` stamps the appearance's
  current mode on what `ShapeMaterial` writes back -- the compatibility
  name is a plain material that sorts AFTER `ShapeAppearance`, so
  without this, restoring a PBR document would convert it to Phong on
  the way past. And the glTF importer tags the materials it builds
  (`mat.pbr = true` beside the raw factors) rather than relying on a
  separate `setPBR` call to arrive first.
- **The dialogs (landed 2026-08-13)**: the appearance editor
  (`DlgMaterialProperties`) grew a shading-model row that toggles
  Phong/PBR through the new `convertPBR()` -- unlike `setPBR()` it
  converts the stored values per entry so the look survives (toward
  Phong via `getPhongMaterial`, toward PBR via `Material::phongToPbr`:
  dielectric, roughness from the shininess fit; the specular colour is
  the one thing a round trip forgets). In PBR view the diffuse button is
  the base colour, the specular button the F0 tint, metallic/roughness
  are spin rows, and ambient/shininess hide. Edits apply as committed
  (spin steps, colour picks -- keyboard tracking off) so the 3D view
  answers live; OK keeps, Cancel restores per-property snapshots. Colour
  edits write rgb only (`setDiffuseRGB`/`setSpecularRGB`): the diffuse
  alpha is the opacity and the PBR specular alpha the metallic, which
  the old whole-colour writes silently wiped. The editor also handles a
  plain `PropertyMaterial` again (the colour-plot `TextureMaterial`
  path had been dead: every handler cast to the list type only), and
  the display dialog's preset combo now states Phong mode explicitly --
  a value write alone deliberately keeps the mode, so picking a preset
  on a PBR appearance used to leave the flag behind.

### 8.5 Landed 2026-08-13

Property core + conversions + dict setter + gtests: `75fbed77ef`
(8 new tests in `tests/src/App/PropertyMaterialList.cpp`, all 38
green). Coin GL leg derivation + uniform bgfx leg: `5759df0e46`
(three-box pixel probe: appearance-PBR == Render_* leg, != Phong leg).
glTF exact + STEP conversion: the commit following this doc's update;
verified by the updated probe battery in `~/works/sw/models/perface/`
(data/uniform/step-export/step-import/truck, all PASS -- data_probe now
asserts the raw factors exactly).

Python read side (value tags, MaterialPy PBR/Metallic/Roughness, the
assignment mode rules, convertPBR, the rgb-only field writes) and the
two dialogs: the commits following this doc's update. Verified three
ways: 41/41 gtests (3 new), a 16-check FreeCADCmd probe of the Python
bridge, and a 21-check xvfb GUI smoke that drives Std_SetAppearance's
real dialogs through a cancel-revert round and an OK-keep round.

Per-face streams (`SoFCPbrElement`, the bake, the draw flag, the
shader, dump v48): the commits following this doc's update. Verified on
the CPU side by 4 new tests in `tests/src/Gui/RenderCacheMaterial.cpp`
(9/9 green, none of them skipping against a stock libCoin -- this leg
does not use the fork) and, for the producer, by an 11-check xvfb scene
probe (`~/works/sw/models/perface/perface_pbr_scene_probe.py`) that
reads the node arrays a real view provider builds, across per-face,
uniform-PBR and Phong appearances.

**The GPU side is verified by a picture too, 2026-08-14.** It was held up
for a day by a harness fault that looked exactly like a renderer bug: in
this box's xvfb runs, render-cache mode 3 drew a frame that no material
change reached, on the modified and the unmodified tree alike. The cause
was not the renderer but the `user.cfg` every probe launcher copies --
`Matcap=1` with `MatcapTint=0` makes the bgfx shader draw one procedural
studio material over the entire scene, which Coin ignores, hence modes
0 and 2 looking right. With matcap forced off,
`~/works/sw/models/perface/perface_pbr_pixel_probe.py` reads face 1 =
75,12,11, face 3 = 181,29,27 and face 5 = 102,15,14, an exact match for
its three appearance entries, and the entries are far enough apart that
the control leg is live. No code changed. ⚠️ The lesson is a probe rule:
a pixel probe must STATE the scene-wide render preferences it depends
on, never inherit them.

## 9. Surface finish: a new field array

Decided by the user 2026-08-14. A machined surface finish -- knurled,
brushed, blasted, turned -- is authored on the appearance itself, App
side, as a **new per-field array** rather than as another reinterpretation
of the existing ones. Per-face falls out for free: the appearance is
already indexed per face, so a finish per face is a finish per entry and
no new indexing, no new property and no new restore path is invented for
it.

This section is the **data model only**. Nothing renders from it yet;
the render ladder is 9.7.

### 9.1 Why a new field rather than another reinterpretation

Section 8 got away with reinterpreting because every quantity PBR mode
needed was already a scalar or a colour in the layout. A finish is not.
It is a pattern **enum** plus three physical numbers, and there is no
room left: the diffuse alpha is the opacity, the specular alpha the
metallic, the shininess slot the roughness. `_ambient` is the only field
with no PBR meaning, and it is a colour -- three 8-bit channels and an
alpha, which can hold neither a pitch in millimetres nor an enum without
becoming a private code.

A second mode bit over the same arrays would also take the number of
readings a stored value can be in from two to four, in the editor, in
both exporters and in every conversion function. The per-field layout of
section 3 exists precisely so that adding a field is the cheap move: an
array nobody has set is size 0 and costs nothing, on every object in
every existing document.

### 9.2 The record on `App::Material`

    struct SurfaceFinish {
        enum Pattern : uint8_t {
            None = 0, Knurl, KnurlStraight, Brushed, Blasted, Turned
        };
        uint8_t pattern = None;
        float pitch = 0.0f;   // mm, feature spacing
        float depth = 0.0f;   // mm, peak to valley
        float angle = 0.0f;   // degrees, lay direction in the pattern frame

        bool operator==(const SurfaceFinish &) const;
        bool operator!=(const SurfaceFinish &) const;
        bool isSet() const { return pattern != None; }
    };

carried as a public `SurfaceFinish finish;` beside `shininess` and
`transparency`. Three choices worth stating:

- **Millimetres and degrees, plain floats.** Pitch and depth are
  physical: a 0.8 mm knurl is about 0.3 mm deep, a brushed lay is
  microns. Storing the honest quantity is what lets the renderer decide
  when a feature has dropped below the pixel footprint and should become
  roughness instead of a normal (9.7). No `Quantity` -- every other
  material field is a plain float and mm is the internal unit.
- **The default record is all zeros**, which matters more than it looks.
  It is what makes the array elide to empty for every existing document
  and every material nobody has given a finish, so the shared-default
  scheme of 1.3 keeps eliding byte-identically and this feature costs
  nothing on a file that does not use it.
- **`operator==` is load-bearing, not decoration.** `normalize()`
  collapses a field to size 1 by comparing elements, so the record must
  be comparable for the cardinality convention to work at all.

Integration with the existing value semantics:

- `Material::operator==` gains `finish == m.finish`. The `uuid`
  short-circuit above it still wins, unchanged.
- ⚠️ **`setType()` resets the finish to default.** It already rewrites
  every colour and both floats with the preset's, and a preset like
  STEEL states no finish; leaving a stale one behind is the same class
  of bug as the `getMaterial()` ordering trap already recorded there
  (the type goes on FIRST, then the fields).
- `pbrToPhong`, `phongToPbr` and `setPBR` **carry the finish through
  untouched**. A finish is a statement about the physical surface, not a
  reading of the shading slots; it is orthogonal to the mode exactly the
  way the emissive colour and the identity strings are.

### 9.3 The list field

    std::vector<SurfaceFinish> _finish;   // size 0, 1, or N

obeying the same 0/1/N convention as every other field, with the
accessors in the shape the others already have:

    const std::vector<SurfaceFinish> &getFinishes() const;
    SurfaceFinish getFinish(int idx) const;      // fieldAt, with the default
    void setFinishes(const std::vector<SurfaceFinish> &values);
    void setFinish(int idx, const SurfaceFinish &value);
    void setFinish(const SurfaceFinish &value);  // every entry
    bool hasFinish() const { return !_finish.empty(); }   // like hasTextureOrCard()

and folded into `getMaterial` (laid in AFTER `setType`, like the rest),
`setValues`, `set1Value`, `normalize`/`ensureNormalized`, `touchFields`,
`getMemSize`, `isSame`, `Copy`/`Paste`.

⭐ **One array of records, not four parallel scalar arrays.** The
precedent supports it -- `_ambient` holds a 16-byte `Color`, not four
float arrays -- and the four numbers genuinely co-vary: a face has one
finish specification, and it is hard to construct a real part where the
pitch varies per face while the pattern does not. Four arrays would
quadruple the accessors, the mask bits and the serialization keys to buy
an elision case that does not occur, and it would recreate by hand the
"both set or neither" pairing that `SoFCPbrElement` had to invent for
metallic and roughness (8.4). The cost of the choice is that a part
varying only the lay angle per face stores 16 bytes an entry instead of
4, which is the case nobody has.

Two existing methods need a decision rather than a mechanical edit:

- **`variesOnlyInDiffuse()` must answer false when `_finish` holds more
  than one entry.** It gates whether the lossy compatibility
  `DiffuseColor` copy is still honest (7.9), and a per-face finish is
  something a colour list cannot say. A *uniform* finish leaves it true,
  which is right: the compat copy only ever loses the finish, and
  nothing that consumes it understands finishes anyway.
- **`getPhongMaterial()` carries the finish through.** It derives the
  classic shading slots; the finish is not one of them.

### 9.4 Serialization, and the compatibility policy that governs it

⭐ **The policy, stated by the user 2026-08-14 and general to this fork,
not specific to this field.** This is a development version that has
never been published, so:

1. **We must read upstream's files.** Unchanged, and the whole restore
   path of 1.2 and 7.3 stays.
2. **We should be able to write a file upstream can open.** Lossless is
   better -- open in upstream, open back here, nothing lost. Where a
   datum cannot survive their format, keep the upstream-readable
   encoding readable and let the **default save be our own format**,
   which carries everything.
3. **We owe our own older builds nothing.** A document written today
   need not open in a build from last week. This retires the constraint
   that shaped 8.3, where PBR mode rode an XML *attribute* specifically
   because a new key line throws "unknown material field" on an older
   fork build. That throw is no longer a reason to bend a format -- but
   it is still a bug, and one that must be fixed before publication
   rather than after, for the readers that will exist then (9.4.2).

Note this sharpens 1.2 rather than contradicting it: 1.2 said upstream
need not read our *default* save, and that still holds -- what item 2
adds is that the compatible encoding must stay genuinely usable, not
merely present.

Against that, the three encodings:

- **Binary field stream (schema >= 5, the fork format).** A new
  `FieldFinish = 1 << 11` mask bit, and a run written as `pattern` in an
  `int8` plus the three floats per entry -- the same shape `_type`
  already uses for its int8 run. Where that run goes is 9.4.2.
- **XML field form (schema >= 5, or when a string must survive).** A new
  key `'f'`, one line, four tokens per entry. Under item 3 a plain key
  line is fine and no attribute trick is needed; what the line's leading
  number counts is 9.4.2.
- **The compatible encodings (schema < 5, the upstream-readable ones).**
  They cannot carry a finish and should not be made to. Upstream's stream
  is fixed -- count, four packed colours, shininess and transparency,
  then the `version="3"` pass of three strings -- and the only fields with
  spare room are `image`, `imagePath` and `uuid`, whose meaning is
  theirs. Smuggling a finish into a material-card identity would make two
  appearances that are not equal compare equal, since `operator==`
  short-circuits on `uuid` (7.3).

  ⚠️ **And "the default save carries it" was not true as written.**
  `SaveSchemaVersion` defaulted to **4** when this was written, and a
  compatible encoding has nowhere to put a finish, so one would have
  vanished from every ordinary document. (**Superseded 2026-08-20**: the
  default is now 5, but the conclusion below stands unchanged --
  `Document::Restore` keeps a restored file at its own schema, and a user
  can cap any document at 4, so the compatible encodings still have to
  carry a finish and a texture.)

  An earlier draft answered that by forcing the fork's own encoding
  whenever a list states a finish. That works and is lossless for us, but
  it pays for the finish with the whole file's readability, which is not
  the trade the policy asks for. **The answer is 9.4.1 instead: put the
  finish in a property of its own and change none of these encodings.**

`getMemSize()` and `getSaveSize()` pick up `_finish.size() *
sizeof(SurfaceFinish)`.

#### 9.4.1 A property of its own, and schema 4 becomes lossless both ways

⭐ **The user's answer, and it is better than anything the encodings can
do from the inside.** Pretend an older format kept the surface finish in
a property beside the appearance, and that this one folded it into the
material. Then write the file as though that migration were still in
progress: the material list exactly as upstream has always read it, and
the finish beside it under its own name, `ShapeFinish`
(`App::PropertySurfaceFinishList`).

**Why it works, verified rather than assumed.** `PropertyContainer::Restore`
looks each saved property up by name; a name it does not find goes to
`handleChangedPropertyName`, whose default body does nothing, and then
`readEndElement("Property")` scans forward to the matching end tag
(`Reader.cpp:330`), skipping whatever was inside -- a char stream, nested
elements, an archive entry reference, anything. **The document format is
built to ignore properties it does not know**, which is how properties get
removed across versions. So upstream opens the file, reads the material
list byte for byte as before, skips `ShapeFinish` whole, and has an
appearance with no finishes. We read both and end up with everything.

That is the lossless-both-ways case the policy asked for and that item 2
had written off: **one file, readable by upstream, and readable back by us
with nothing missing.** No encoding changed shape, no compatibility was
traded, and `saveXML` / `SaveDocFile` / `getSaveSize` are exactly what
they were before this feature.

The rules that make it safe:

- **It is not a second store.** `PropertySurfaceFinishList` holds a
  `PropertyMaterialList*` and reads and writes that list's `_finish`
  field; a finish IS part of a material, and two stores of one value is
  the mistake this property spent 7.9 removing. The same shape
  `PartGui::PropertyDiffuseColor` uses to be a name over the appearance's
  diffuse field (1.1). A `Copy()` for the undo stack has no appearance to
  name, so it carries detached values.
- **It writes values only below schema 5**, where the material encoding
  cannot state them. At 5 and above the appearance's own field form
  carries the finish and the companion writes an empty element, so no
  document ever holds the same finish twice.
- **An empty element means "nothing stated", never "clear it".** At
  schema 5 the appearance restores first -- `ShapeAppearance` sorts before
  `ShapeFinish`, and `ShapeColor` and `ShapeMaterial` after both -- with
  the finish already in it, and an empty companion must not wipe what it
  just got. Same rule `DiffuseColor` needed for the same reason (1.1).
- **Registered wherever `ShapeAppearance` is**: `ViewProviderGeometryObject`
  and `ViewProviderLink`, hidden in the editor like `ShapeMaterial`, since
  one datum must not be two rows.

#### 9.4.2 Forward compatibility, which is the half the policy does not cover

⭐ **Item 3 above is about the past; this is about the future, and it has
to be paid for now.** The policy says we owe our own older builds
nothing *today*, because there are none in the wild. The day there are,
every field added after that point has to be skippable by the readers
already shipped -- and neither encoding can do that as it stands.
Fixing both is free right now and impossible later, so it belongs in
this change rather than in the one that first needs it.

**The binary stream is positional.** A reader consumes the runs in bit
order, so it can only tolerate unknown fields that happen to sit at the
tail. That holds for exactly one round of additions: append field A,
then later field B, and a reader that knows A but not B still works only
because B is last. A reader that knows neither, meeting a file that has
both, is fine too. But it breaks the moment a field is ever inserted, or
two lines of development each claim "the next bit", and it gives no way
to drop a field.

So **length-prefix each run**: keep the mask exactly as it is (it still
carries the payload-free `FieldPBR` flag), and write each present field
as its byte length followed by its payload. A reader that does not know
a bit seeks past its run and continues, wherever the run sits. Writing
the length means buffering the run into a scratch `Base::OutputStream`
first, which is a few lines and costs 4 bytes per present field -- of
which there are under a dozen.

**The XML keyed form cannot be skipped at all**, and its `default:` case
throws `FileException("unknown material field")`. Two changes, both
small:

1. **Skip unknown keys** instead of throwing: read the leading number,
   discard that many whitespace tokens, continue. This is the change
   that makes every future field additive, and it is worthless unless it
   ships before the format is published.
2. ⭐ **Define the leading number as the count of TOKENS that follow, not
   of entries.** That sounds like a format change and is not: every
   field written today -- packed colours, floats, int8 types, hex
   strings -- writes exactly **one whitespace token per entry**, so for
   all of them the two readings are the same number and not one byte on
   the wire moves. It is what makes rule 1 work for a field whose
   records are wider than one token, which the finish is the first of.
   The reader divides by the stride it knows (4 for `'f'`) and applies
   the existing 0/1/N check to the result; the skipper needs no stride
   at all.

The alternative for the finish alone would be four separate one-token
keys, which needs neither change. It is rejected because it solves the
problem only for records that decompose into scalars, and the next field
may not -- while the two changes above solve it for every field this
format will ever carry.

Two things that are already forward-compatible and should stay that way:
**XML attributes** (an unknown attribute is ignored, which is why `pbr`
rides one) and **mask bits with no payload**. Prefer both for anything
that is a flag rather than a run.

### 9.5 Python

Following the `Metallic`/`Roughness` precedent on `MaterialPy.xml` --
scalar attributes rather than a dict:

- `Finish` -- the pattern by name (`""`, `"knurl"`, `"knurl-straight"`,
  `"brushed"`, `"blasted"`, `"turned"`), accepting an int as well on
  assignment.
- `FinishPitch`, `FinishDepth`, `FinishAngle` -- floats.

No new property-level API is needed for per-face authoring:
`ShapeAppearance` already hands out and takes back whole `Material`
objects (4.1), so a per-face finish is written the way a per-face colour
already is. Unlike the PBR flag (8.4) the finish does not interact with
the list's single mode, so there is no `inMode`-style conversion on
assignment: the record is copied verbatim whichever reading the list is
in.

### 9.6 Invariants

- **`pattern == None` means the other three are meaningless**, and the
  setters zero them, which is also what lets the record elide. A stored
  record is therefore always either wholly unset or wholly meaningful.
- **Setters clamp so that stored data is always renderable**: a non-None
  pattern floors the pitch at a small positive minimum and the depth at
  zero, and the angle wraps into [0, 180). Validating on the way in
  keeps the renderer from having to defend itself against a divide by
  zero it cannot report.
- **The finish survives every mode conversion** (9.2), in both the value
  and the list direction.

### 9.7 What this stage deliberately excludes

Everything that draws it, and it is a separate piece of work with its own
verification:

- The producer and carrier -- `updateRenderMaterial()`, new
  `SoFCRenderMaterial` fields, and a `SoFCFinishElement` following the
  `SoFCPbrElement` precedent (8.4), which is now the established way to
  move a per-face scalar stream that is not a Coin material field.
- The renderer ladder, in order: **per-object first** (four `Render_*`
  style knobs through the path that already carries `Render_Metallic`,
  which ships the whole shader library at almost no plumbing cost), then
  **per-face** through a finish palette indexed by a per-vertex byte
  rather than a fifth widening of the material stream, then **explicit
  per-face projection frames** computed at tessellation time from the
  OCCT surface type (plane gets its own axes, cylinder and cone their
  axis and circumference), which is what turns a brushed or turned lay
  from approximately right into manufacturing-correct.
- The pattern coordinate is **object space**, so a scaled or instanced
  copy keeps its finish attached to its geometry.
- ⛔ **The surface parametrization UV is dropped, not deferred.** OCCT's
  triangulation does carry UV nodes for every B-Rep face and
  `ViewProviderExt.cpp:4469` copies them only for mesh-only faces, so it
  is available -- but it is not metric (a cylinder's u is radians, a
  NURBS patch's is arbitrary), it costs 8 bytes a vertex, and the
  surfaces where it would beat an explicit frame are freeform faces
  nobody specifies a knurl on.
- ~~⚠️ **The property editor's material row, which is lossy already.**~~
  Fixed 2026-08-14, all four at once -- see 9.10.
- ~~Analytic filtering of the pattern against the pixel footprint~~ --
  landed with rung 1 instead (9.8): without it a 0.3 mm pitch aliases
  into moire the moment the part is zoomed to fit, which would have made
  every honest value undemonstrable. Still excluded: anisotropic GGX,
  which real brushed metal wants and which touches the shared lighting
  core for both the direct and the IBL terms; and any PMI or ISO 1302
  semantics, which is the rung above all of this.

### 9.8 Landed 2026-08-14: rung 1, the per-object finish draws

The producer, the carrier and the whole shader library, in the commits
following this doc's update. `App::SurfaceFinish` now travels
`ViewProviderGeometryObject::updateRenderMaterial` -> four new
`SoFCRenderMaterial` fields -> `SoFCRenderCache::Material` (and its
`operator<`) -> `SoFCRendererBridge` -> `Render::Material` ->
`u_finishParams` -> `bgfx/shaders/fc_finish.sh`. Scene dump v49 and
chunk revision 7 (materials ride content-keyed chunks, so the key has to
move with the layout or a cached chunk is misread). Reachable from the
Render settings task dialog, from a script, and from the appearance.

**Both sources, with the authored one winning.** The appearance's own
finish (entry 0) is the finish; the `Render_Finish` / `Render_FinishPitch`
/ `Render_FinishDepth` / `Render_FinishAngle` knobs are how an appearance
that carries none gets one -- the same precedence a PBR-mode appearance
has over `Render_Metallic` (8.4). `Render_Finish` reads an enumeration,
a plain string or the raw pattern value, so the dialog and a script do
not have to agree on a spelling. A stated pattern with no size gets the
size that pattern has on a real part (0.8 mm for a knurl), because the
alternative -- `normalize()`'s 1e-4 mm floor -- is a finish nobody can
see.

⭐ **Three decisions carried the shader.** (1) The pattern lives in
OBJECT space and rides two new varyings (`v_opos`/`v_onrm` =
`a_position`/`a_normal`, which makes the instanced path free, since
instances differ only in the transform applied after them). (2) The
normal is perturbed by **Mikkelsen's surface gradient**, fed from the
pattern's *analytic* object-space gradient through the chain rule
(`dot(g, dFdx(opos))`) -- no UV, no tangent frame, no per-draw matrix,
and the pattern is never differenced, so no crest is blurred to the 2x2
quad. (3) Filtering is not a polish step but the thing that makes
physical units usable: below ~2 px per feature the relief fades and its
lost slope variance becomes roughness (Toksvig), which the Phong path
receives through the shininess slot.

⚠️ **A fragment shader's `$input` list may be a strict SUBSET of the
vertex shader's `$output` list** -- `fs_fc_water`/`glass`/`groundrefl`/
`bloom_emit` all pair with `vs_fc_mesh` and ignore the new varyings.
The "bgfx requires the VS output list to exactly match the FS input
list" comment in `fc_mesh_vs.sh` only binds the other direction, and
that is what made adding varyings a local change rather than a sweep
through every mesh-paired shader.
⚠️ A NEW `*.sh` shader include only joins the build's dependency glob
after a cmake RE-CONFIGURE; edits to it alone would otherwise not
rebuild anything.

**Verified by picture** (`scripts/demo-finish.py`, which states
`Matcap=False` for the reason recorded in 8.5): five patterns, each
authored both ways, all distinct and correct; and the "as machined"
cylinders correctly show *only* the coarse patterns, the fine ones
having collapsed into roughness at that camera distance.

⚠️⚠️ **The first picture looked like two shader bugs and was neither.**
Brushed came out flat white and blasted came out as black-and-white
speckle. A depth sweep under two materials settled it: at metallic 0.9
and roughness 0.25 a flat face saturates against the studio
environment, and *a saturated highlight swallows relief* -- every tilt
the pattern applies still lands on white, so only the steepest patterns
(a knurl's 43 degree flanks) survived. Under a duller steel every
pattern reads. Nothing in the shader changed; the demo's material did,
and the one real defect the sweep did find was a default -- a blasted
crater as deep as a third of its width reads as lunar, so that ratio
went 0.35 -> 0.15. ⭐ The lesson generalises: **when verifying a shading
feature, the test material must not be at the top of its range**, or the
picture reports the material rather than the feature.

Known and correct at this rung: a straight knurl on a cylinder comes out
with CIRCUMFERENTIAL grooves, because triplanar projection does not know
the cylinder's axis. That is exactly what rung 3 (explicit per-face
frames from the OCCT surface type) is for.

### 9.9 Landed 2026-08-14: rung 2, the finish varies per face

A finish is four numbers, so the per-face form is four arrays -- three
more than the per-vertex material stream should carry, and the reason
9.7 called for a palette rather than a fifth widening. What landed is
exactly that: the distinct finishes an appearance holds become a
`Render::FinishPalette` (capped at `Render::MaxFinishPalette` = 8, which
is also the shader's `FC_FINISH_PALETTE`), and what travels per face is
one byte of index into it.

The route mirrors the per-face PBR pair one for one:
`ViewProviderGeometryObject::updateRenderMaterial` builds the palette and
the index array -> `SoFCRenderMaterial::finishPalette`/`finishIndices` ->
`SoFCFinishElement` (indices only) -> `SoFCVertexCache` bakes them into
the material stream's new third slot -> `SoFCRendererBridge` ->
`Render::Material::finishpalette` -> `u_finishParams[]` -> `fc_finish.sh`.

⭐ **The palette is the draw's, the index is the vertex's.** Only the
index has to reach the shape, so only the index needs an element; the
palette rides the draw material as a `shared_ptr<const FinishPalette>`,
which is the `usershader` precedent -- immutable, shared, and its
pointer is the batch key. A single-face draw carries no stream to index
with, so it resolves its face's entry into the material scalars instead
and drops the palette.

**Costs.** The material stream widens 8 -> 12 bytes a vertex (rgba8
emissive, rgb8 specular + shininess, then the index byte with three
reserved), for the per-face-material meshes that carry one at all --
bgfx allows four vertex streams and the mesh programs already use all
four, so the index rides the existing material stream rather than a
fifth. Scene dump v50 AND chunk revision 8: the stride is part of what
the content key hashes. `u_finishParams` becomes an array; a draw
without a palette uploads entry 0 alone, which is what an unbound index
attribute reads anyway.

**Two rules the pictures forced.**

- ⚠️ An appearance that states a finish ANYWHERE is now the authority
  for every face. Before, entry 0 being unset let the `Render_Finish*`
  knobs fill in an object-wide finish, which would contradict a palette
  whose entry 0 says "unfinished" -- and a face left bare in a per-face
  appearance is a statement, not a gap.
- ⚠️ A per-face finish disqualifies the instanced representation
  (`materialsUnrepresentable`). Instancing partitions colour variants
  and states ONE material per instance, so it would have kept the first
  face's finish and silently dropped the rest.

⚠️⚠️ **A picture of six patterns proves that they differ, not that each
one landed where it was authored.** The first per-face picture looked
wrong for an hour: several faces carried what looked like one pattern at
different scales, which is also exactly what "every vertex reads entry
0" looks like. Two things settled it -- `FACES_ONLY`, which finishes ONE
face and leaves the rest bare (only the top faces patterned, so the
index does name the face), and `FACES_DUMP`, which prints the palette
and index array off the node. ⭐ The general form: **a demo that varies
everything cannot localise a fault; keep a one-variable mode beside it.**
And a demo must key its faces by DIRECTION rather than by OCCT's face
numbering, which no reader of the picture can see -- in the object's own
frame, so a turned copy shows the other three.

**Verified by picture** (`scripts/demo-finish-faces.py`): one appearance,
six faces, six finishes -- concentric turning marks on one face,
knurl on the next, the unfinished face plainly plain. The knurled top
face reads as a groove train rather than a diamond grid at a grazing
angle, which is the analytic filter doing its job: the compressed axis
falls below two pixels a feature and becomes roughness while the other
survives.

Still excluded, unchanged from 9.7: rung 3 (explicit per-face frames
from the OCCT surface type), anisotropic GGX, and PMI semantics. The
Diligent backend ignores the finish entirely.

### 9.10 Landed 2026-08-14: the appearance row stops dropping things

`PropertyMaterialListItem` applies an edit by restating the whole value
through generated `App.Material(...)` text, and the text named six
fields. So picking a colour in that row reset everything else each
material held: the PBR mode (a PBR appearance came back Phong), the
metallic factor riding the specular alpha, the surface finish, and
`image`/`imagePath`/`uuid`. All four now travel through the widget's own
`Material` struct and are restated with the rest -- and `App.Material`
gained `Image`, `ImagePath` and `Uuid` (attributes and constructor
keywords) so the identity strings have a spelling to travel in at all.

⚠️ **`Base::Color::asValue<QColor>()` states rgb only.** The metallic
factor lives in the specular alpha, so it cannot ride the widget's
QColor and needs a float of its own beside it. The diffuse alpha needs
no such field: it is the opacity, which `Transparency` states and the
generated call applies after the colour.
⚠️ **`%g` writes a plain `0` for zero**, and `MaterialPy`'s setters take
a `Py::Float`, which refuses an int -- so a finish size of zero made the
whole generated call raise. The sizes print in fixed notation.

⭐⭐ **The check that caught it: assert the edit LANDED, not only that
nothing was lost.** The generated call raised for the reason above, so
nothing changed at all -- and every "field preserved" assertion passed,
because the property still held what it started with. A no-op verify
looks exactly like a perfect one. The probe now sets a colour the
material did not have and checks for it first: 10/10 with it, 9/10 the
moment the command breaks.

### 9.11 Landed 2026-08-14: rung 3, the finish is laid out in the face's own frame

Rungs 1 and 2 gave a face a finish. This one gives it a FRAME. The
pattern stops being projected triplanarly off the object-space normal
and is laid out in the surface's own coordinates: a plane's axes, or the
axis a cylinder or cone was turned about, read off the OCCT surface at
tessellation time. That is the difference between a knurl that looks
plausible from any angle and one that runs where the tool ran.

**What it fixes.** 9.8 recorded the defect in as many words: "a straight
knurl on a cylinder comes out with CIRCUMFERENTIAL grooves, because
triplanar projection does not know the cylinder's axis." It does now.
Turning marks likewise centred on the object origin rather than on the
axis that was turned, which on a part whose axis is anywhere else is not
a subtle error.

**The route**, which is the per-face pair's again with one difference
that decides everything else:

`ViewProviderPartExt::buildVisualNodes` (the tessellation loop, the last
place the analytic surface is in hand) -> `faceProjectionFrame` per face
-> a deduplicated palette in `SoFCRenderMaterial::framePalette` /
`frameIndices` -> `SoFCFinishElement`'s SECOND array -> `SoFCVertexCache`
-> `Render::FramePalette` -> `u_frameParams[]` -> `fc_finish.sh`.

KEY: **The frame is geometry, so it is indexed by the PART, not by the
material.** The finish index rides `getMaterialIndex()`, which is correct
for it -- a per-face finish only exists on a per-face appearance, and
that binds materials per part. A frame exists on any analytic shape,
including the ordinary uniformly-painted one, whose binding is OVERALL
and whose material index is therefore 0 at every vertex. Indexing frames
that way would have handed a whole knurled shaft its first face's frame.
`SoFaceDetail::getPartIndex()` is the honest source: `SoBrepFaceSet`
increments it per part whatever the binding is.

KEY: **It cost nothing per vertex.** The material stream's third slot had
the finish index in one byte and three reserved; the frame index took
the second. So the stride stays 12 bytes, the dump's mesh chunk layout
does not move, and a viewer older than this reads a zero there -- which
is the unframed frame, i.e. the projection it already had. Scene dump
v51 and chunk revision 9, both for the material record alone.

**Two decisions in the shader.**

KEY: (1) **The seam closes by construction.** A cylinder's angular
coordinate cuts at +-pi, and a pattern laid on the raw arc length would
mismatch across that cut by whatever fraction of a pitch the
circumference is not a whole number of. So the period is snapped: the
shader fits a WHOLE number of cycles round the reference radius and
measures the arc in those, which makes the jump at the cut an exact
multiple of the pitch. Real knurling tooling is chosen by the same
arithmetic and for the same reason. The analytic gradient (9.8) is what
makes this free -- the height field is never differenced, so the branch
cut cannot produce a derivative spike either.

KEY: (2) **Turning is fixed to the axis, not to the lay.** In a radial
frame a lay angle of zero runs the pattern along the axis, which is what
a knurl wants. A lathe's feed marks run round the work instead, whatever
else is stated, so `turned` in a radial frame becomes the straight-knurl
relief with the lay turned a quarter turn -- rather than teaching the
pattern library a second spelling of one groove train.

**Canonicalization is what keeps the palette small**, and it is the
reason a cap of 16 is not a limitation in practice. The origin's
component along the axis is dropped -- invisible for a plane (the
pattern is laid IN it) and merely the axial phase for a radial one -- and
the axis sign is fixed. So the coplanar faces of a bracket, the two
sides of a plate, and the coaxial cylinders of a stepped shaft each cost
ONE entry. A part that overflows sixteen has sixteen genuinely different
machining setups; its overflow falls back to entry 0, the way an
overflowing finish falls back to the object's own.

**Entry 0 is the first face's frame, not the unframed one.** The
tempting choice is the reverse -- reserve entry 0 for "no frame" so
every fallback degrades to triplanar. It is wrong, and the case that
shows it is the ordinary one: a shape every face of which is framed
alike states one index at every vertex, so the per-vertex stream
COLLAPSES (nothing varies, no array is allocated) and the draw reads
entry 0 alone. Reserving it for the unframed frame would have lost the
frame on exactly the parts that need it least ambiguously.

**Excluded, and why.** Instanced draws and the bounding-box stand-in
keep the triplanar projection: `buildVisualNodes` is a static builder
several paths share, and only the ones building a particular object's
nodes have a render material to write to. The frames themselves would be
valid for an instanced mesh -- they are object-space, and instances
differ only in the transform applied after -- so this is a plumbing gap,
not a design limit. Also unchanged from 9.7: anisotropic GGX, PMI
semantics, and the Diligent backend, which still ignores the finish
entirely.

**Verified by picture** (`scripts/demo-finish-frames.py`): a straight
knurl, a diamond knurl, an off-centre faced disc and a cone, in PBR
metal on real-GPU WSLg. With `FRAMES_OFF=1` -- the one-variable control,
which clears the frame palette off the render material and changes
nothing else -- the cylinder shows CIRCUMFERENTIAL bands, exactly the
defect 9.8 named; with frames on the grooves run along the axis, the
diamond wraps and closes, and the cone's pattern follows its taper.

KEY: **the index itself was checked, not only the pictures.** Every face
that came out looking right was a face whose frame index is ZERO -- the
lateral surfaces -- which is what they would read even if the index
never varied. What settles it is a shader that outputs the index as a
colour: lateral faces red (0, radial), caps green (1, planar), which is
the palette the producer built. Two real defects hid behind
plausible-looking pictures until that ran:

- WARNING: **the stream's second byte was written as a literal zero.**
  `appendMaterial` filled the third slot with the finish index and three
  constant zeros, so the frame index was dropped between the bake and
  the buffer. Everything upstream measured correct.
- WARNING: **frames must be indexed by the PART, not the material.** See
  above; a uniform appearance binds materials OVERALL and answers 0 at
  every vertex.

Two staging facts, both about the renderer rather than about frames:

- WARNING: **a FLAT face cannot show its relief under a smooth sky.** A
  plane's diffuse and specular response both vary only with the normal,
  and a small perturbation still lands on much the same environment, so
  the caps read as plain however deep the pattern is cut -- pattern,
  depth, tilt and material all changed with no effect on them. This is
  9.8's saturation lesson in its general form. It is why the demo's
  planar-frame case (the turned face centred on its axis) is NOT
  demonstrated by the PBR picture; `FRAMES_PBR=0` is the staging meant
  for it, and it currently blows out (see the next point).
- **PBR metal was underlit and the Phong path blew out** -- both
  closed 2026-08-14, and both were the lighting path rather than the
  finish, as suspected. The metal darkness was the PBR branch having
  no ambient term at all: `u_ambient` was read only by Blinn-Phong, so
  a metal -- which has no diffuse to be lit by -- had literally
  nothing but the environment, and `Render_PBREnvIntensity` 3 was
  standing in for the missing light while washing the relief out. The
  scene ambient now enters that branch as a uniform-radiance
  environment (`u_envAmbient`, added to the irradiance AND the
  prefiltered term, `docs/RenderEngine.md` 4). This demo reads better
  at `FRAMES_ENV=1` than it ever did at 3. The Phong blowout was the
  tuned `0.2 ambient / 0.8 diffuse` pair, replaced by Coin's ambient at
  full diffuse weight.

  WARNING: one blowout is not a renderer defect and is worth stating because
  every finish demo can walk into it: a **near-white Phong specular
  saturates a flat face to paper white** and swallows the relief. The
  obvious reading of "not a metal" -- specular colour near 1 -- is what
  does it. Give Phong the same `F0` the PBR branch computes,
  `mix(0.04, base, metallic)`, and the same plate shows its pattern.

### 9.12 The finishes under both shading models

`scripts/demo-finish-shading.py`. The three demos above are about what a
finish IS. This one is about what it costs the two shading models the
engine has, because a finish is a specular effect -- it perturbs the
shading normal and coarsens the highlight -- and that is precisely the
kind of authoring whose look does not carry between a
metallic/roughness BRDF with an environment and a single-lobe
Blinn-Phong under a headlight.

One chart, six columns by two rows, captured TWICE with `Render_PBR` on
and off and nothing else different between the frames
(`SHADING_SHOT=shot.png` writes `shot-pbr.png` and `shot-phong.png`):

- **Columns**: none (the control), knurl, knurl-straight, brushed,
  blasted, turned. The control is what makes the rest readable --
  whatever a column does that `none` does not is the finish, in that
  model.
- **Top row**: cylinders carrying the finish through the
  `Render_Finish*` view properties at the pattern's own default pitch,
  i.e. the size it really has on a part, which at a whole-part camera
  distance is mostly finer than a pixel.
- **Bottom row**: plates carrying it on the appearance itself at an
  exaggerated pitch, so the relief resolves and the pattern geometry is
  visible.

Two things the pair of pictures settles:

- **The sub-pixel half of a finish is NOT PBR-only.** What the filter
  fades out below Nyquist it adds to the roughness, and the Phong path
  converts its shininess to a roughness before the finish runs and back
  after (`fc_mesh_fs.sh`), so a finish coarsens a Phong highlight
  exactly as it coarsens a PBR one. The top row shows that with no
  resolved relief anywhere in it.
- **Both legs must be stated, or the A/B measures a default.** Each
  part carries a Phong specular/shininess AND a
  `Render_Metallic`/`Render_Roughness` pair describing the same
  surface: the shininess is the exact inverse of the shader's own
  shininess/roughness fit, and the specular colour is the same
  `mix(0.04, base, metallic)` F0 the PBR branch computes. Set only one
  of the two and the other model is being shown whatever the default
  happened to be.

## 10. Textures: the last fold into the appearance

Stage 9 moved the surface finish onto `App::Material`. This stage moves what
is left that belongs there: the texture maps, which today are `Render_*`
dynamic properties on the view provider. It also settles the two questions
that fold raises -- whether one property may hold several stored files, and
whether the list field should stay dense.

### 10.1 The test for eligibility, and what it selects

**The test, stated by the user 2026-08-19: does some file format carry this
per face?** Not "would per-face be imaginable" -- everything is imaginable --
but whether an interchange format the importer already reads states it per
material. A setting no format varies per face has nothing to round trip and
stays a view property, whatever the renderer could do with it.

Eligible, and therefore to be folded:

| Property | Where the format states it |
|---|---|
| `Render_BaseColorTexture` | glTF `pbrMetallicRoughness.baseColorTexture`; OCCT `XCAFDoc_VisMaterialPBR::BaseColorTexture` |
| `Render_MetallicRoughnessMap` | glTF `metallicRoughnessTexture`; OCCT `MetallicRoughnessTexture` |
| `Render_NormalMap` | glTF `normalTexture`; OCCT `NormalTexture` |
| `Render_EmissiveMap` | glTF `emissiveTexture`; OCCT `EmissiveTexture` |
| `Render_OcclusionMap` | glTF `occlusionTexture`; OCCT `OcclusionTexture` |
| `Render_TextureScale` / `Offset` / `Rotation` | glTF `KHR_texture_transform`, per texture reference inside the material |

**The proof is not the specification, it is what this codebase already pays.**
`ImportOCAF2::scanMaterialGroups` keys material groups on exactly these five
texture handles plus the metallic and roughness factors, and when they differ
across the faces of one shape it SPLITS the shape into one `Part::Feature`
per group under a container. That splitting exists for no other reason than
that the format is per primitive and the property is per object. Folding
these retires it: the faces stay one solid and the appearance carries what
made them differ. Nothing else in the tree is distorted by the gap this way,
which is why nothing else is on the list.

Not eligible, and staying on the view provider:

- `Render_Water`, `Render_Glass`, `Render_Cloud`, `Render_Fire`,
  `Render_Fountain` and their density/detail/speed/IOR knobs. These turn a
  CLOSED SHAPE into a volumetric medium; three of them do not render the body
  geometry at all. A volume has no faces to attach to, and no format states a
  per-face medium.
- `Render_Light` and its intensity/range/shadow flags. glTF puts lights on a
  NODE (`KHR_lights_punctual`), never on a material. Per-face emission is
  already `App::Material::emissiveColor`.
- `Render_CastShadow` / `Render_ReceiveShadow`. These map onto Coin's
  `SoShadowStyle`, a traversal-state node. No interchange format carries a
  per-material shadow flag.

Already folded: the PBR pair (8.1) and the finish (9). Note also that
`Render_Texture` is not a property -- it is the `strncmp` prefix in
`ViewProviderGeometryObject::updateRenderProperty` that catches the three
transform names -- and that everything on `View3DInventorViewer`
(`Render_AO*`, `Render_Occlusion*`, `Render_Level*`, `Render_PBR*`,
`Render_Matcap*`) is a per-VIEW parameter in a different namespace, never a
candidate.

### 10.2 The blob store already carries several files per property

A texture is a file, and files live in `App::FileBlobManager`. The fold needs
one property to hold five of them per entry, which no property does today:
both existing referrers, `PropertyFileIncluded` and `PropertyPartShape`, hold
a single `_blob`. The question is whether that is a limit or just a fact.

It is just a fact. Four of the five things a second blob would touch already
work:

1. **Storage and lifetime.** A blob is content addressed and handed out as a
   `shared_ptr`; nothing binds one to a single property. Sharing was the
   explicit point of the design -- before it, `~PropertyFileIncluded` deleted
   its file unconditionally and `Copy()` had to duplicate the bytes.
2. **Save collection.** `noteReferenced(blob, referrer)` is called once per
   blob and its referrer list is additive: "shared content is one file with
   several referrers".
3. **Naming.** `BlobReferrer::name` is supplied by the caller, and `planSave`
   de-collides with a numbering pinned by the previous index so it cannot
   migrate between saves. A caller with five slots passes five names
   (`Box.ShapeAppearance.normal`) and gets five readable files; it does not
   have to accept `Box.ShapeAppearance1.png`.
4. **The restore queue.** `_pending` is a `vector<pair<hash, prop*>>`, not a
   map keyed by property, so N entries for one property already fit, and
   `removePendingReferrer(prop)` withdraws all of them -- which is the
   correct behaviour for a dying multi-slot property.

**The fifth is the whole of the work, and it is a discipline rather than a
signature.** `BlobReferrerProperty::assignRestoredBlob(handle)` carries no
slot identity, and arrival order is NOT queue order: `addPendingReferrer`
serves a hash IMMEDIATELY when the content has already been read and queues
it otherwise, so a property asking for an unread h1 and then an already-read
h2 is handed h2 first. With one blob per property that reordering is
invisible. With five it is a silent mis-assignment -- the normal map arriving
in the occlusion slot.

The fix is not to add a cookie to the virtual. The property already knows
which hash belongs to which slot, because it deserialized them, and
`FileBlob::hash()` is public: **a multi-slot referrer resolves the slot by
content hash, never by arrival order.** Two slots holding the same content
share one hash and one blob, and both are assigned -- which is correct, and
makes the assignment idempotent, which is what lets a duplicated `_pending`
entry be harmless. `FileBlobManager` needs no change at all.

### 10.3 The record on `App::Material`

Follow the `SurfaceFinish` precedent (9.2) rather than adding eight loose
fields: one struct, because the members co-vary -- a texture set is one
statement about a surface -- and because the field mask bits, the accessors
and the serialized keys otherwise multiply to buy elision cases that do not
occur.

```cpp
struct AppExport SurfaceTexture
{
    enum Slot : uint8_t {
        BaseColor = 0, MetallicRoughness, Normal, Emissive, Occlusion,
        SlotCount
    };
    /// Content hash of the blob in each slot; empty = no map there.
    std::string maps[SlotCount];
    float scale[2] {1.0F, 1.0F};
    float offset[2] {0.0F, 0.0F};
    float rotation {0.0F};   /**< degrees */

    bool isSet() const;      /**< any slot occupied */
    void normalize();        /**< an unset record states nothing else */
};
```

The slots hold a CONTENT HASH, not a path: the property is the blob referrer
and the manager owns the bytes, exactly as `PropertyPartShape` does. The
default is all-empty, which is what lets the field elide out of both the
storage and the document the way the finish does.

**Reconciling upstream's fields.** `App::Material` already has `image`,
`imagePath` and `uuid`, and their comment says they exist for the day "a
reader does produce them -- glTF carries both a texture and a material
identity". They are upstream's, and `TextureRendering.yml` defines them as
`TextureImage` = "Embedded texture image" and `TexturePath` = "Path to file
... only used if Texture Image is unpopulated". So they model exactly one
texture, embedded or referenced. They stay as they are, and the base colour
slot maps onto them for the upstream-readable save: writing `imagePath` on
the way out and reading it into `BaseColor` on the way in. The other four
slots have nowhere to go in that encoding and are dropped by it, which is
what 9.4 item 2 permits.

### 10.4 The list field, and why it goes sparse

TODAY a `PropertyMaterialList` field is a `std::vector<T>` normalised to one
of exactly three lengths -- 0, 1 or `_count` -- and read through
`fieldAt(values, idx, def)`: empty is "every entry is the default", one is
"uniform", anything else is dense per entry. `collapseField` restores that
form after every edit.

**That is already sparse in the two cheap cases and falls off a cliff between
them.** All-default costs nothing and all-same costs one record, but the
moment ONE entry differs the field materialises `_count` of them. For a
colour that is right -- a per-face colour list is genuinely dense, it is the
common CAD case, and an index would be the size of the value it replaces. For
a texture set it is ruinous: one odd face in five thousand materialises five
thousand records of five strings each.

And the distribution is known, not guessed. glTF gives a mesh a HANDFUL of
materials; `scanMaterialGroups` already discovers exactly that grouping, and
today spends the discovery on splitting the shape. Low cardinality over many
faces is the case to store well.

So: **palette plus index, for the heavy fields only.**

```cpp
std::vector<SurfaceTexture> _texturePalette;  // distinct values
std::vector<uint16_t>       _textureIndex;    // one per entry
```

with the degenerate forms preserved so nothing regresses: an empty palette is
all-default, and a palette of one with an empty index is uniform. Only a
genuinely varying field pays for the index, and it pays two bytes rather than
a whole record.

This is not a new idea in the tree, it is the RENDER side's model moved down
to storage. `updateRenderMaterial` already builds a palette of distinct
finishes plus a per-face index for `SoFCFinishElement`, and rebuilds it from
the dense array on every update. A palette in storage is what that consumer
wanted in the first place.

Which fields change, and which do not:

- **Textures: palette + index.** Large values, low cardinality, and the
  importer already computes the grouping.
- **Colours, shininess, transparency, type: unchanged.** An index is the size
  of the value; per-face variation is the norm, not the exception.
- **Finish: leave dense for now, revisit.** 16 bytes a record is well short
  of the cliff, but it is the one other field whose consumer wants a palette.
  Migrating it is a pure optimisation and can follow once the texture palette
  has proved the encoding.

### 10.5 Serialization

The fork's binary field stream (schema >= 5) gains one field: a palette
count, the palette records, then the index run. 9.4 already lists
length-prefixed runs in that stream as a change that must land before
publication so later fields stay additive -- a palette field is precisely
that shape, so this stage should carry it rather than bolt a second
convention alongside.

The XML keyed form needs the same, under the unknown-key skip that 9.4 also
requires. The upstream-compatible encoding carries the base colour slot in
`imagePath` and nothing else, as 10.3 says.

Blobs are already handled: the property notes each distinct palette hash with
`noteReferenced` at save (one referrer name per slot, 10.2 item 3), and calls
`addPendingReferrer` per distinct hash on restore, resolving by hash when the
handles come back.

### 10.6 What this stage deliberately excludes

- **Per-face texture COORDINATES.** The maps are per face; the UVs are not,
  and the mesh stream is where those belong. Out of scope.
- **Migrating existing documents.** Per 9.4 item 3 we owe our own older
  builds nothing. The reader in `ViewProviderGeometryObject` keeps the
  `Render_*` texture path for scripted objects, as it now does for the PBR
  pair and the finish; nothing rewrites a document that has them.
- **Retiring `scanMaterialGroups`.** The split it performs becomes
  unnecessary, but removing it is a separate change with its own import
  regression surface, and it should not ride in with the storage work.

### 10.7 Landed 2026-08-19/20: the storage, the wire and the blobs

Five commits, in the order the implementation notes suggested except that
the blobs and the serialization swapped: the restore side of the blob
plumbing has nothing to hook into until the file states the hashes.

| Commit | What |
|---|---|
| `37efdecbfc` | `App::SurfaceTexture` on `App::Material` (10.3) |
| `6333553219` | the palette + index field on `PropertyMaterialList` (10.4) |
| `e1d5d0873c` | length-prefixed runs, and the texture in the fork's own two encodings (10.5) |
| `09291e1ceb` | the companion property for the schemas that cannot state one |
| `363f1f7c55` | the multi-blob referrer (10.2) |

**Three things the design did not anticipate.**

1. **`slots` is a macro.** Qt defines it as nothing, and this
   translation unit sees it, so `uint8_t slots = 0;` compiles to
   `uint8_t = 0;` and the error names `unsigned char`, not the variable.
   The identifier is `slotCount` throughout for that reason.

2. **9.4.2's "length-prefix each run" had only half landed.** What the
   finish work shipped was a run head of SHAPE plus entry count, which
   lets a reader step over a run whose FIELD it does not know but not one
   whose SHAPE it does not know -- there it stopped reading, losing
   everything behind it. A palette is a new shape, so the other half had
   to land with it: the head is now shape, BYTE LENGTH and count. Writing
   the length means buffering the payload into a scratch stream in the
   same mode and byte order, as 9.4.2 said it would.

   It also retires the idea of reserving mask bits for payload-free flags.
   A first attempt did reserve the top nibble and thereby swallowed
   `FieldTexture` at bit 12 -- the field read back as absent with no error
   anywhere. With a byte length there is nothing to reserve: **a flag
   added later writes a run of ZERO bytes**, which every reader steps over
   exactly as it steps over a field it does not know, so the presence of
   the bit stays the whole value and the 16 bits stay available to fields.

3. **A `pruneTextureBlobs()` on every `normalize()` is a use-after-free
   waiting to happen.** A caller must insert content before it can name
   the hash, so between `insertTextureFile()` and the write that names it
   there is always a handle no slot points at -- and normalize runs on any
   read, including one inside that window. The claim is therefore dropped
   only where the palette has just been stated IN FULL: the whole-list
   assignments and the restore.

**What 10.2 predicted and the code confirms.** `FileBlobManager` needed no
change at all. The five things a second blob would touch are the storage,
the save collection, the naming, the restore queue and the slot identity,
and only the last is work: `assignRestoredBlob` resolves by
`blob->hash()`, which makes it idempotent, which is what lets two slots
over one content both be served and a duplicated queue entry be harmless.
The collect passes in `App::Document` and `Gui::Document` grew one branch
each, because they dispatch on property type and an appearance is not a
`PropertyFileIncluded`.

**Still to do**, and none of it is storage: the importer and exporter onto
the new field, the UI, and the Python spelling. `Render_BaseColorTexture`
and its four siblings are still what `ViewProviderGeometryObject` reads, so
nothing yet writes a texture into an appearance except a script.

## 11. The appearance as a Python value

Landed 2026-08-20, in five commits. The storage work of section 10 left the
Python spelling as the last thing owed, and doing it properly turned out to
be a change to what a property hands Python at all -- so the pattern is
written up separately in `docs/PythonValueBindings.md` and this section
records what it means for the appearance.

### 11.1 What changed

`vp.ShapeAppearance` was a `Py::Tuple` of N freshly copied `MaterialPy`
objects (4.1 recorded that as "same observable behaviour, same cost"). It
is now one `App.MaterialList`:

| | before | after |
|---|---|---|
| reading it | N `Material` copies + N wrappers | a pointer |
| `[0].DiffuseColor = c` | silently did nothing | paints the face, records undo |
| `[0] = mat` | silently did nothing | paints the face |
| assigning it to another property | copied every field array | a pointer |
| an undo snapshot | deep copy of fourteen vectors | a pointer |

The value moved out of the property into `App::MaterialList`, a copy-on-write
value over `Base::COWValue`. `PropertyMaterialList` keeps the serialization,
the blob restore queue, the touch list and the change signalling, and
delegates the rest. Nothing about the storage layout or the encodings
changed: the 76 storage tests pass unmodified, byte-identical serialization
included.

### 11.2 The three states a script can be in

```python
a = vp.ShapeAppearance      # a LIVE VIEW: writing to it paints the object
b = a.copy()                # a VALUE sharing a's storage: writing paints nothing
vp.ShapeAppearance = b      # takes a share of b -- and b stays a value
```

Assigning a list into any property detaches it, which is what stops a view
from quietly becoming a view of two things. A view whose property dies --
document closed, object deleted -- keeps what it last saw and writes
nowhere; the property detaches every view it handed out on the way out.

### 11.3 The texture spelling

A slot holds the content hash of a file the blob store owns, so stating a
texture from Python is content in, then a slot naming it:

```python
h = a.insertTextureFile('/path/oak.png')     # -> the content hash
a.setTexture(0, 'basecolor', h)
n = a.setTextureFile(0, 'normal', '/path/n.png')   # both at once
a.setTextureTransform(0, Scale=(2, 2), Rotation=45)
a.getTexture(0)                              # {'basecolor': h, 'normal': n}
a.clearTexture(0)                            # every slot of that entry
```

The slot names are `TextureSlots`, and they are the same strings
`MaterialPy.Texture` uses and the saved files are named by. Every setter
takes the entry index or leaves it out, and leaving it out means EVERY
entry -- not the -1 an index would be mistaken for, since the getters count
negatives from the end the way Python does.

The per field accessors alongside them are the same eight fields the C++ API
has (the four colours, shininess, transparency, and the PBR pair, which
demands the mode).

The store the content goes into is the document's when the list is a view of
one of its properties, and the process-wide one otherwise. 10.2 asked
whether a blob minted by one manager can be saved by another: it can --
`noteReferenced()` keys the save set by hash and writes the bytes from
wherever the handle points -- so a list built in Python and assigned into a
document carries its content in with it, and no re-homing is needed.

WORTH KNOWING: content is archived at SCHEMA 5 and above. Schema 4 is
upstream's format, where the blob store does not exist, and a document saved
there keeps the hashes and drops the files -- a legitimate state the
property already handles (a hash with no content answers an empty path and
logs one warning per missing file on restore). A script that wants a texture
to survive a round trip sets `doc.SaveSchemaVersion = 5`. Mapping the base
colour slot onto upstream's `imagePath` for the schema 4 save, as 10.3
proposes, is still owed.

### 11.4 What this stage deliberately excludes

- **The renderer.** `ViewProviderGeometryObject` still reads
  `Render_BaseColorTexture` and its four siblings; nothing yet draws a
  texture that lives in an appearance. That is the next stage, and this one
  exists to make it verifiable -- a per-face texture can now be authored
  from a script.
- **The importer and the UI**, unchanged from 10.7's list.

## 12. The base entry and the overriding faces

> Designed 2026-08-30 with the follow-the-card flag of
> `MaterialStorage.md` sec 15. **Built 2026-08-30**; the flag itself is
> still ahead of it.

### 12.1 What is wrong

A per-face list is N entries and nothing else. Sec 1.1 had to rule that
entry 0 of such a list is one face and not the object, and everything since
has lived with the consequence: there is no entry that says what the object
looks like where no face says otherwise.

- The mirrors (`ShapeColor`, `Transparency`, the view provider's legacy
  `ShapeMaterial`) refresh only while the diffuse field is uniform, so on a
  per-face list they hold whatever the last uniform value was. The instanced
  colour path already reads `ShapeColor` as the base for the faces a short
  apply leaves unstated (`ViewProviderExt.cpp`, `applyInstancedFaceColors`).
  The base exists in practice; it is just not in the value.
- A whole-object edit cannot tell an override from the rest. Setting
  `ShapeColor` on a per-face list collapses every face to it, which is the
  right answer for a face that only ever held the body colour and the wrong
  one for the three faces the user painted.
- The importer writes N materials (`ImportOCAFGui::applyFaceMaterials`) with
  no notion of which is the body colour, and nothing downstream can recover
  it: a green board with five hundred gold pads is five hundred and one
  colours of equal standing.
- Picking an appearance card replaces the list (`DlgDisplayPropertiesImp::
  onMaterialSelected` carries the finish across by hand, because `setValue`
  would take it with the faces). Every whole-object write has to re-derive
  what a per-face list meant, and each does it differently.

### 12.2 The storage

`MaterialList::Data` becomes base + overrides, and the per-field arrays
of sec 3 shrink to the overriding faces:

    int count;                        // logical entry count N
    Material base;                    // the object's look, every field
    std::vector<uint32_t> overrides;  // sorted face indices holding their own
    std::vector<Color> diffuse;       // size 0 or overrides.size()
    ...                               // every other field the same

with one invariant:

    entry(i) == base                       for every i not in overrides
    entry(i).field == base.field           when that field's array is empty

The 0/1/N convention of sec 3 becomes **0 or |o|**: once the base exists,
"uniform" IS the base, so there is nothing for a one-element array to
say. An override array is empty when every overriding face has the base's
value for that field (three faces painted red keep the base's gloss, so
only `diffuse` has three entries), and |o| long otherwise. An overriding
face is one that differs from the base in at least one field.

Nothing dense is kept. The accessors resolve: `getValues()` and
`getDiffuseColors()` materialise N entries on read, `getMaterial(i)` and
`getPhongMaterial(i)` look `i` up in `overrides` (a binary search, or a
lazily built slot map). That is what every consumer already receives --
`applyShapeAppearance`, the exporters, the render cache and the Python
sequence all take vectors or ask by index -- so none of them change.
`variesOnlyInDiffuse()` becomes "every non-diffuse override array is
empty".

What a write means:

| Write | Touches |
| --- | --- |
| whole-object: `ShapeColor`, the colour widget, an appearance card, the card's look while following (sec 15) | `base` only; the overriding faces keep their values |
| per-face: `setMaterial(i, m)`, `setDiffuseColor(i, c)`, "Set appearance per face", the importer's per-face path | face `i`: added to `overrides` with its values, or REMOVED when the value equals `base` |
| clear overrides | drops `overrides` and every override array -- what a whole-object write does today, made an action of its own |

Normalisation: an override whose entry equals `base` is dropped; an
override array whose every entry equals the base's field is emptied. The
uniform list is the case `overrides` is empty, and costs one material
whatever N is. Sec 3.1's table becomes: uniform, 80 B; an import with
colour on k faces, 80 B + 20k B; full per-face materials, 80 B + 84N B.

`base` is a full `Material`, finish and texture included: a per-face
finish or image on a face is an override like a colour is, and the base
finish is what a face returns to when its override is cleared.

The `App::Link` precedent for this is `OverrideMaterialList`, a bool per
element beside `MaterialList`. Faces get a sorted index vector rather than a
bool list because the common per-face object (an import) overrides a few
faces of thousands, and because an index list serialises at its own length
(sec 4.3's `x` key already does).

### 12.3 Serialization

Schema 5 (the fork's format) writes the storage as it is:

- `b`: the base, as one entry's tokens;
- `o`: the overriding face indices, self-describing in the way the `x` and
  `f` keys of sec 4.3 are;
- the field lines of sec 4.3 at length 0 or |o|, in `o` order.

A 10,000-face import with three painted faces goes from about 160 KB of
diffuse to about a hundred bytes; a list where every face differs writes
the N values it writes today plus 4N bytes of indices. An earlier fork
build steps over `b` and `o` (sec 9.4.2) but then reads field lines
shorter than the entry count, so a file carrying them is not one it can
open -- the same standing every schema-5 file already has (sec 4.3), and
the reason the default cap is 4.

Schema 4 (upstream's) cannot state either key and keeps writing the dense
N entries, byte for byte as today: it is written sequentially by index and
resolves each entry as it goes, which is what the dense-array writer did
without noticing. A schema-4 file restores through the heuristic of 12.4,
which is also what every document written before this section gets.

### 12.4 Deriving a base where none was stored

The heuristic runs once, at restore of a per-face list without a `b` key
and at import, and never again for that list: it produces the stored base,
which from then on is the answer.

1. A uniform list is its own base with no overrides (the importer already
   collapses this case).
2. Otherwise, the mirror first: if the view provider's `ShapeColor` (with
   `Transparency`) names a value that occurs in the list, that is the base.
   On a document this fork wrote, the mirrors hold the last uniform value,
   which is what the object looked like before its faces were painted.
   On an import the mirrors hold the constructor's grey, which occurs in no
   imported list, so this step declines.
3. Otherwise the material covering the largest summed face AREA is the base,
   with entry count as the tie-break and as the fallback when no shape is at
   hand. Area, not count: the board-and-pads case is decided the wrong way
   by count, and a shape whose faces are all of a size is decided the same
   way by both. The area is available where the heuristic runs -- the shape
   is restored before its view provider finishes restoring, and the importer
   has it in hand.

The choice is recorded by being stored, so a later save at schema 5 does
not repeat it. A user who disagrees has the whole-object controls: setting
the object's colour writes the base, and the faces that were not overrides
follow.

### 12.5 Python

On `MaterialListPy`, the live view of sec 11:

- `Base` (rw, `Material`): the object's look. Writing it is the
  whole-object write of 12.2.
- `Overrides` (ro, tuple of int): the overriding face indices.
- `clearOverrides()` / `clearOverride(i)`.
- `setMaterial(i, m)` and the per-field setters keep their signatures and
  gain the per-face meaning of 12.2.

`FollowMaterial` is the third attribute, and belongs to sec 15 of
`MaterialStorage.md`: it decides whether SETTING the object's card writes
`Base`, not what an override is. It gates that moment only -- a restore
brings back the base the file stored, following or not.

### 12.6 Invariants

- `entry(i) == base` for every `i` not in `overrides`, and an empty
  override array reads as the base's field; normalisation enforces both
  and tests assert them.
- `overrides` is sorted, unique, and every index is `< count`.
- A list with `count <= 1` has no overrides.
- A whole-object write never changes an overriding entry. A per-face write
  never changes `base`.
- Consumers read through the resolving accessors only. Nothing in `Gui/`,
  the exporters or the renderer branches on `overrides`; the property
  editor and the two task panels are the only readers, and they read it to
  say which faces are painted.

### 12.7 What the code calls it

`App::MaterialList` holds `base`, `overrides` and one array per field at 0
or `|overrides|`, and the accessors come in two kinds where there was one:

- `getDiffuseOverrides()` and its nine siblings hand back the STORAGE -- 0
  or `|overrides|` values, in overrides order. `variesInDiffuse()` and its
  siblings are the same question asked cheaply, and are what the old
  `getDiffuseColors().size() <= 1` sites became.
- `getDiffuseColors()` and its siblings RESOLVE, by value, one value per
  entry. That is what the compatible encodings are written from, what the
  companion elements state, and what `PropertyDiffuseColor` -- the
  compatibility name -- hands out of a member it refills on every read.

The heuristic of 12.4 runs from three places, in the order a document meets
them: `ViewProviderGeometryObject::finishRestoring`, which offers the mirror
and, only when the mirror declines, the face areas from the new virtual
`getFaceWeights` (a per-face `BRepGProp` sweep is not free, and a document
this fork wrote answers from the mirror alone); `ImportOCAFGui::
applyFaceMaterials`, which has the shape in hand and always measures; and
`ensureBase()` from the two schema-5 writers, because that encoding STATES
the base and writing a default one would record an answer nobody gave.
Deriving is const and does not detach: it changes what is stored and not
what an entry resolves to.

The mirrors changed with it. `ShapeColor`, `Transparency` and the view
provider's legacy `ShapeMaterial` now follow the BASE unconditionally, where
before they refreshed only while the list was uniform and otherwise held
whatever the last uniform value had been (12.1). Every read that meant "the
object's look" and spelled it `getMaterial(0)` or `getTransparency(0)` reads
`getBase()` now.
