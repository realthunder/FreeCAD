# ShapeAppearance, compatible with upstream but not laid out like it

Status: stage 1 (storage, the ShapeAppearance property, the compatibility
names, restore migration) landed 2026-08-12 with the alpha convention
flip of 7.9 following 2026-08-13. Stage 2 (Coin carries per-face
material to the render cache, ABI intact) landed 2026-08-13, verified by
tests/src/Gui/RenderCacheMaterial.cpp on both the extended and the stock
Coin. Stages 3-5 are design.
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

The existing one is kept exactly, byte for byte, and written at **schema 5**
-- the default, the one every other FreeCAD can read: `count="N"` and then
one line per entry of four packed colours, shininess and transparency, or
the same sequence in a doc file. It is written and read **sequentially by
index**, so it streams straight out of per-field arrays without
materialising anything.

At **schema 6** -- the fork's compact format, which already writes a root
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
written form. This one has two written forms, and at schema 5 a list that is
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
- **The C surface is `coin_lazyex_*`** (install + abi_version + one
  getter per field over an opaque `SoState*`), bound by
  `Gui::CoinLazyElementEx` with dlsym at `SoFCDB::init`. The element
  class header stays internal to the coin tree; nothing in FreeCAD
  includes it.
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
  the Ex element through `Gui::CoinLazyElementEx` at `open()` and
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
- The compatible encodings (schema < 6 stream and inline XML) cannot
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
- **bgfx, per-face leg**: NOT LANDED. Per-face metallic cannot ride
  SoMaterial (SbColor drops alpha at the node), so it needs
  metallic/roughness arrays on the coin fork's Ex element, baked into
  the existing streams (the emissive stream's low byte is a constant FF
  today = free; the specular low byte carries roughness at the same
  8-bit quantization shininess uses). Until then a per-face PBR
  appearance renders with entry-0 uniform PBR plus derived-Phong
  per-face streams.
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
- **The Python read side (landed 2026-08-13)**: the mode rides the
  material VALUES. `App::Material` carries a `pbr` tag -- part of its
  equality, not of its storage -- and `getMaterial()` stamps the list's
  mode on every value it hands out, so `ShapeAppearance[0].PBR` answers,
  and `Metallic`/`Roughness` read mode-aware off the value (a Phong
  value has no metals and derives its roughness). Assignment adopts the
  materials' tags: a tuple read from one object carries its mode to the
  next, a plain `Material()` list states Phong, mixing modes in one
  assignment is a TypeError, and the dict's explicit `PBR` key wins over
  the tags ({"PBR": x} alone stays the raw reinterpret). A partial
  (indexed) write must match the list's mode. `Material(PBR=True, ...)`
  seeds the PBR defaults -- white tint, metallic 0, roughness 0.5 --
  before the explicit keywords land, because inheriting the Phong
  default specular would spell full metal.
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
