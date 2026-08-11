# ShapeAppearance, compatible with upstream but not laid out like it

Status: design, 2026-08-11. Decision taken: adopt it. Not yet implemented.
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

`App::Material` is not 18 floats. It is:

| member | bytes |
|---|---|
| 4 x `Base::Color` (ambient, diffuse, specular, emissive) | 64 |
| `shininess`, `transparency` | 8 |
| `image`, `imagePath`, `uuid` (3 x `std::string`) | 96 |
| **total** | **~168** |

plus a heap allocation per non-empty string. Against `Base::Color`'s 16
bytes, per-face appearance is roughly **10x** the cost of the per-face
colour list it replaces, not the 4.5x an earlier estimate assumed.

And on the path that actually produces per-face data -- STEP import -- 
OCCT supplies only colour and alpha (see UpstreamCoreSync section 5.1), so
every one of those materials carries four default colours, two default
floats and three empty strings it will never use.

## 3. The storage

Per-field arrays, each independently sized 0, 1 or N:

    class PropertyMaterialList {
        int _count;                          // logical entry count N
        std::vector<Base::Color> _ambient;   // size 0, 1, or N
        std::vector<Base::Color> _diffuse;
        std::vector<Base::Color> _specular;
        std::vector<Base::Color> _emissive;
        std::vector<float>       _shininess;
        std::vector<float>       _transparency;
        std::vector<std::string> _image, _imagePath, _uuid;
    };

with the cardinality convention:

- **0** -- every entry holds the field's default; costs nothing
- **1** -- uniform across all entries; costs one element
- **N** -- genuinely per-entry

`_diffuse` **is** the old `DiffuseColor` list. Migrating a pre-existing
document moves that vector across as-is; there is no conversion and no
widening, which is the compatibility half of this design and the efficiency
half at the same time.

### 3.1 What it costs, by case

For a 10,000-face solid:

| case | upstream | this design |
|---|---|---|
| uniform appearance | ~168 B | ~100 B (one element per touched field) |
| STEP import, colour + alpha per face | ~1.68 MB | **~200 KB** (`_diffuse` 160 KB + `_transparency` 40 KB, rest empty) |
| full per-face materials (glTF) | ~1.68 MB | ~1.68 MB |

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

### 4.3 Document format

Unchanged from upstream: `<MaterialList file="..." version="3"/>` plus a
binary doc-file holding a count, then per material the four packed colours,
shininess and transparency, then a second pass over all materials writing
`image`, `imagePath` and `uuid`. That layout is written and read
**sequentially by index**, so it streams straight out of per-field arrays
without materialising anything.

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

1. **`getValues()` returns `const std::vector<Material>&`.** It is
   non-virtual on `PropertyListsT<T>`, which owns a concrete
   `_lValueList`. So we cannot inherit that base and still control layout:
   implement the interface directly, and serve `getValues()` from a
   `mutable` cache built on demand and dropped on any write. 13 upstream
   call sites; our own code should use the field accessors instead. Confirm
   no caller holds the reference across a mutation.
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

## 6. Staging

1. ~~Probe risk 2~~ -- answered from the Coin source (section 5.1); the
   layout needs no expansion path.
2. `App::PropertyMaterialList` with the layout above, full upstream API,
   upstream save/restore format. Unit-testable without any view provider.
3. `ShapeAppearance` on `Gui::ViewProviderGeometryObject`, with
   `ShapeColor`/`ShapeMaterial` kept as compatibility accessors over it, and
   the restore-time migration.
4. `ViewProviderPartExt`: `DiffuseColor` becomes a view onto `_diffuse`;
   reconcile the fork's Map* properties.
5. Python emulation of `DiffuseColor`/`ShapeColor`, then run the existing
   probe suites plus a document round-trip against an upstream-written file.
