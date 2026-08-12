# ShapeAppearance, compatible with upstream but not laid out like it

Status: stage 1 storage implemented 2026-08-12; the rest is design.
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

| member | this fork | upstream 1.0 |
|---|---|---|
| 4 x `Color` (ambient, diffuse, specular, emissive) | 64 | 64 |
| `shininess`, `transparency` | 8 | 8 |
| `MaterialType` | 4 (+4 padding) | -- |
| `image`, `imagePath`, `uuid` (3 x `std::string`) | -- | 96 |
| **total** | **80** | **~168**, plus a heap allocation per non-empty string |

This fork still has the pre-1.0 `App::Material`, so the immediate multiple
against a `Color`'s 16 bytes is **5x**, not the 10x that upstream's would
cost. The direction of the argument does not change and the ceiling only
rises: adopting upstream's material -- which the format compatibility in
section 4.3 eventually asks for -- doubles the per-entry cost again, while
the per-field layout absorbs it by leaving the three string fields at size
zero on every object that has no texture.

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
        std::vector<int8_t> _type;      // Material::MaterialType
    };

`_type` is there because `App::Material::operator==` compares it, so a list
that dropped it would fail to give back what was put into it. It has never
been persisted -- no material list in any FreeCAD writes it -- and it stays
that way. Upstream's `_image`, `_imagePath` and `_uuid` join the list
unchanged in shape on the day this fork takes upstream's material.

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
| STEP import, colour + alpha per face | 800 KB | **200 KB** (`_diffuse` 160 KB + `_transparency` 40 KB, rest empty) |
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
coloured faces linear. Still to do in this stage: `ShapeAppearance` itself,
the `DiffuseColor` accessor of 1.1, and the restore-time migration.

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
  `PropertyColorListItem`, and section 1.1 already hides `DiffuseColor` from
  the editor so a single datum does not appear as two rows.

Before writing it, grep for `PropertyColorList*` and
`freecad_dynamic_cast<...PropertyColorList>` and confirm the list is empty.
If some site genuinely needs base-pointer reads, that site is the argument
for a virtual -- not the design.

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

### 7.3 Our `App::Material` is not upstream's, and their file says so

**Not blocking, but it caps what "read upstream's documents" can mean.**
Upstream's material is ours plus `image`, `imagePath`, `uuid` and a static
`getDefaultAppearance()`. Their `MaterialList` doc file at `version="3"` is,
byte for byte, our legacy first pass -- count, then four packed colours,
shininess and transparency per entry -- followed by a **second pass** of
three length-prefixed strings per material. They also keep `Version_0` and
`Version_2` readers and a negative-integer sentinel where a count would be.

So reading their file is a small change: honour the element's `version`
attribute and consume the second pass. Until `App::Material` has the three
fields, consuming is all it can do, and the read is lossy. Say so rather
than claim compatibility. When the fields do arrive, the per-field layout
takes them at size 0 on every object that has no texture -- which is nearly
all of them.

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

1. UpstreamCoreSync 2d, `App::Color` -> `Base::Color`, seven forward
   declarations included (7.1).
2. `ShapeAppearance` on `ViewProviderGeometryObject`, retiring the trio.
3. `DiffuseColor` as an accessor (1.1), with the two base-pointer sites
   restructured first (7.2) and the fork-only mapping behaviour preserved
   (7.5).
4. Restore-time migration, including upstream's `version="3"` second pass
   as a lossy read (7.3).
