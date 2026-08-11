# Closing the core-API gap with upstream, starting with what FEM needs

Status: plan, 2026-08-11. Nothing in stages 1-3 has been done yet.
Driver: [FemPortEvaluation.md](./FemPortEvaluation.md), which found that the
cost of porting upstream's FEM is not in FEM but in core refactors this fork
predates.

## 0. Policy

**Keep this fork's behaviour.** Where upstream's newer API is a different
*name or location* for the same behaviour, adopt upstream's shape so future
syncs get cheaper. Where upstream's API change would alter behaviour,
persistence, or a fork feature, do not take it silently -- it goes on the
consult list in section 5.

**Adopt the name, keep an alias.** Every item in stage 2 can be landed as
"move/rename to upstream's spelling, leave a thin alias at the old one".
That makes each step a small diff instead of a 200-file sweep, keeps every
existing fork call site compiling untouched, and still means upstream code
drops in. The call-site sweep becomes optional cleanup, done when
convenient, not a precondition. This is what makes the gap-closing gradual.

The cost of that choice is two spellings coexisting. The rule that keeps it
from becoming permanent debt: **new and ported code uses the upstream
spelling**; the fork spelling is frozen and only ever removed, never added
to.

## 1. Two pleasant surprises worth stating first

**The Console architecture is already upstream's.** The fork has
`LogStyle`, `IntendedRecipient`, `ContentType`, the notifier overloads,
`DeveloperWarning`/`UserWarning`, and already formats with `fmt::sprintf`.
Upstream's `Console().log(...)` and our `Console().Log(...)` are the same
machinery with different capitalisation. The 158 FEM sites and 2235 fork
sites are a pure rename, not a behaviour question.

**`App::Color` is content-identical to upstream's `Base::Color`.** Our
`src/App/Color.h` has drifted by exactly 4 added lines since the merge base,
and upstream's API is a strict superset of ours. The move to `Base` is a
file move, not a rewrite.

## 2. Stage 1 -- additive, no fork behaviour touched

Nothing here changes an existing API. Each is independently landable.

| item | what | size |
|---|---|---|
| 1a | Port `Base::TimeElapsed` into `Base/TimeInfo.h` | ~30 lines, additive. Upstream added it after the merge base; we do not have it (32 FEM uses in 3 files) |
| 1b | Add `App::PropertyStiffnessDensity`, `App::PropertyMoment` | two unit-typed properties in `App/PropertyUnits`, additive |
| 1c | Retype FEM's 5 `fastsignals` uses to `boost::signals2` | no core change; see below |
| 1d | Adapt FEM's 2 `App::DatumElement` checks to `Part::Datum` | no core change; see below |

**1c, why not vendor FastSignals.** Upstream vendored
`src/3rdParty/FastSignals` and its core signals hand back
`fastsignals::connection`. Ours hand back `boost::signals2::connection`.
FEM only ever *stores* the connection type, so retyping five members costs
nothing, whereas vendoring the library without also converting the fork's
signals would give us a type nothing in our core returns. Revisit only if
the fork ever wants upstream's signal implementation for its own sake.

**1d, why not port `App::Datums` now.** Upstream promoted datum features to
the App layer (`App::DatumElement`, local coordinate systems). FEM touches
it in exactly two places, both `isDerivedFrom` checks that also test
`Part::Datum`, which we do have. Adapting the two sites preserves fork
behaviour exactly. Porting `App::Datums` properly is a feature-level change
reaching into PartDesign and is listed in section 5.

## 3. Stage 2 -- upstream's names and locations, aliases at the old ones

Ordered by ascending reach, so each lands and gets a full build before the
next. Every one of these is behaviour-preserving by construction: the alias
keeps old call sites bound to the same entity.

| step | change | fork call sites | alias left behind |
|---|---|---|---|
| 2a | `Console().Log/Message/Warning/Error/...` -> lowercase | 2235 in 433 files | keep capitalised inline forwarders |
| 2b | `src/Gui/Selection*.h` -> `src/Gui/Selection/` | 150 + 50 includes | forwarding headers at the old paths |
| 2c | `ViewProviderPythonFeature` -> `ViewProviderFeaturePython` (file and classes) | 233 in 57 files | `using` aliases for the old names |
| 2d | `App::Color` -> `Base::Color` | 843 in 202 files | `namespace App { using Color = Base::Color; }` |

Notes that matter when doing them:

- **2a** is the biggest FEM win (158 of the census sites) for the smallest
  real change: twelve inline forwarders. Do it first despite being the
  largest sweep on paper, because with the alias approach the sweep is not
  part of the step.
- **2b** upstream also adds `SelectionColors.h` and `BoxSelection.h` in that
  directory, which we have no equivalent for. Create the directory and move
  what we have; do not invent the two new files.
- **2c** is a rename of both the header and the class names
  (`ViewProviderPythonFeatureT` -> `ViewProviderFeaturePythonT`, etc.). A
  shim proved during the evaluation that FEM's 23 uses need nothing else.
- **2d** verify the shared methods behave identically before deleting
  anything; the API is a superset but that is a signature check, not a
  semantic one. `PropertyColor`, `PropertyColorList` and the material
  classes all reference it.

**Verification for each step**: a full build of the eval tree plus the
existing probe suites. Because these are aliases, a step that compiles is
very close to a step that is correct -- but 2d in particular should also get
a render-path check (`ground_parity_probe.py`, an audit leg) since colour
flows into the render cache.

## 4. Stage 3 -- fork-side conflicts: keep the fork, adapt FEM

Here the fork is the one that diverged, so upstream's FEM must bend to us.
All four are FEM-side edits with **no core change**, which is exactly what
the policy asks for.

- `ComplexGeoData::getElementTypes()` returns `const std::vector<const char*>&`
  here (our `75a7f709d8`), by value upstream. Keep ours -- it avoids a copy
  in element-mapping paths. Adapt `FemMesh`'s override to return a reference
  to a static table.
- `App::Property::isSame` is **pure virtual** here, and upstream gives it a
  default body comparing type and `getMemSize`. Keep ours pure: it forces
  every property to answer the question deliberately, and all 23 fork
  implementations already do. Implement `isSame` in FEM's
  `PropertyPostDataObject`.
- `signalHighlightObject` lives on `Application` here (`3418f1be72`), on
  `Gui::Document` upstream. Keep ours; adapt FEM's one use.
- `ViewProviderFemPostPipeline::acceptReorderingObjects` overrides nothing
  here. Adapt FEM.

## 5. Consult -- cannot be done while keeping fork behaviour

These three are why this document ends with questions rather than steps.

### 5.1 ShapeAppearance

**Scope correction.** An earlier draft of this section described this as
Part's `DiffuseColor` being renamed. It is considerably larger. The property
lives on **`Gui::ViewProviderGeometryObject`, the base class**, and upstream
removed *two* properties from it, not one:

| ours (base class) | upstream |
|---|---|
| `App::PropertyColor ShapeColor` | removed |
| `App::PropertyMaterial ShapeMaterial` | removed |
| -- | `App::PropertyMaterialList ShapeAppearance` |

Part's per-face `App::PropertyColorList DiffuseColor` is then subsumed into
the same list. So three properties expressing appearance at different
granularities collapse into one list-of-materials, on the class every
geometry view provider derives from -- 18 direct derivatives in our tree
plus everything under Part, not just `ViewProviderPartExt`.

Fork-wide reach of what it displaces:

| symbol | hits | files |
|---|---|---|
| `ShapeColor` | 260 | 106 |
| `DiffuseColor` | 282 | 57 |
| `ShapeMaterial` | 83 | 17 |

**The rationale** (David Carter, `495a96a0f5`, 2024-03-17, part of the 1.0
Materials rework): "The ShapeColor attribute is replaced by a ShapeAppearance
attribute. This is a material list that describes all appearance properties,
not just diffuse color. As a list it can be used for all elements of a
shape, such as edges and faces." The follow-up `8b5a3b1124` that removed
`DiffuseColor` outright adds: "Lays the foundation for future texture
support."

The motivation is coherent: per-face colour was only ever *diffuse* colour,
so specular, shininess and transparency could not vary per face, and the
view-side appearance was disconnected from the new physical-material system.

**What it cost upstream**, which is the part worth weighing: 33 commits
touched appearance in the following nine months, 11 of them fixes or
reverts, spanning PartDesign datum features and shape binders, STEP import
colour-per-face, transparency handling, CAM, Draft, BIM and DXF import --
including issue #15027, "ShapeAppearance does not handle transparency per
face correctly", not fixed until 2024-06-29. Most of the cleanup was done by
wmayer rather than the author.

**Compatibility they built**, which is genuinely careful: old documents
migrate through `handleChangedPropertyName` (`ShapeColor` and
`ShapeMaterial` are converted on restore); Part keeps a hidden
`_diffuseColor` to restore per-face colours asynchronously; and Python
macros still see `DiffuseColor` and `ShapeColor` because
`ViewProviderPartExtPyImp` and `ViewProviderGeometryObjectPyImp` emulate
them as custom attributes over `ShapeAppearance`.

**Why it is still a consult item here.** It collides with fork-only
properties sitting right beside it in `ViewProviderExt.h` --
`MappedColors`, `MapFaceColor`, `MapLineColor`, `MapPointColor`,
`MapTransparency` -- and with the Appearance LinkGroup design. It is a
document-format change to user data. And nothing on this fork's roadmap
asks for per-face materials.

Good news: the bgfx path does not read the property (the one
`setDiffuseColorOverride` hit in `SoFCRenderCache.cpp` is a Coin state
element, unrelated), so this is a data-model question, not a renderer one.

**What `ShapeAppearance` is not.** Calling it "per-face materials" is wrong
and an earlier draft of this section said exactly that. Upstream splits the
two cleanly, and their physical model agrees with the obvious objection that
a solid is made of one material:

| | property | type | cardinality |
|---|---|---|---|
| physical, App side | `Part::Feature::ShapeMaterial` | `Materials::PropertyMaterial` | **one per feature** |
| visual, Gui side | `ViewProviderGeometryObject::ShapeAppearance` | `App::PropertyMaterialList` | one per face |

`App::Material` is the OpenGL/Coin visual record -- ambient, diffuse,
specular and emissive colour plus shininess and transparency. It carries no
density, no modulus, nothing physical. So the per-face list is per-face
*finish*, not per-face substance, and it is the direct descendant of the
per-face `DiffuseColor` this fork already has.

**So the real question is narrower than it first looks:** should a per-face
appearance entry be a colour (4 floats, what we have) or a full visual
material (18 floats)? The widening buys per-face specular, shininess,
ambient and emissive.

The split itself is standard practice, not an invention: Fusion 360,
SolidWorks, Inventor, NX and Onshape all carry one *physical* material per
body or component (density, modulus, driving mass properties and
simulation) while letting *appearance* be overridden down to the face. None
of them assign physical material per face -- that would make mass
properties meaningless. Upstream's two-property split matches this exactly.

**But the import-fidelity argument for the widening does not survive
checking, and an earlier draft of this section asserted it.** Measured in
the OCCT 8.0.1 source:

- `STEPCAFControl_Reader.cxx` contains **zero** references to
  `VisMaterialTool`. Every use of its `STEPConstruct_RenderingProperties`
  reduces to `GetRGBAColor()`, and what reaches the XCAF document is
  `XCAFDoc_ColorTool::SetColor` -- **colour plus alpha, nothing else**.
- OCCT *can* represent more: `STEPConstruct_RenderingProperties` converts
  to and from `XCAFDoc_VisMaterialCommon`, and STEP's
  `surface_style_rendering` carries reflectance and shininess. The reader
  simply does not keep it.
- FreeCAD reads `XCAFDoc_VisMaterial` in exactly one place,
  `ReaderGltf.cpp`. `ImportOCAF.cpp` and `ImportOCAF2.cpp` use the colour
  tool only.

So for STEP, the dominant import path, per-face fidelity means colour and
transparency -- which this fork already stores and already renders. Only
glTF benefits from the widening.

**And the rendering capability is not what is being bought either.** We
already have it: `ViewProviderPartExt::setHighlightedFaces` (ViewProviderExt.cpp:1547)
sets `SoMaterialBinding::PER_PART` and fills the diffuse, ambient, specular
and emissive arrays per face today. Upstream's contribution is wiring that
path to a persistent, user-facing property, not teaching Coin to draw it.

What it costs is 4.5x the per-face storage, the three-into-one migration on
a base class, and that regression tail.

**Question:** keep `ShapeColor` / `ShapeMaterial` / `DiffuseColor` and adapt
FEM's 37 sites to them, or take the widening? Adapting FEM is much the
cheaper side, and per-face *colour* already covers the common cases; per-face
specular is rare in practice.

### 5.2 The Materials module

Upstream FEM does `import Materials` in 13 files. Our `src/Mod/Material` is
the December-2023 vintage: python module named `Material`, `.xml` bindings.
Upstream's is renamed to `Materials`, has diverged by 346 files / +25515
lines, and gained `ExternalManager`, `Library`, `MaterialFilter`.

There is no alias trick here: it is a second module port of real size, and
material cards are user data.

**Question:** is a Materials port in scope at all? If not, FEM's material
task panels have to be held at our vintage, which means diverging from
upstream FEM in the one place upstream FEM changed most.

### 5.3 App::Datums

Section 1d adapts FEM's two sites and needs nothing. But if the fork wants
upstream's datum model itself (App-level datum elements, local coordinate
systems), that is a feature port touching PartDesign, and it should be
decided on its own merits rather than as FEM collateral.

## 6. What this buys beyond FEM

Stages 1-3 are worth doing even if the FEM port never happens. Every one of
them removes a permanent source of friction from *every* future upstream
merge: today a cherry-pick from upstream fails on Console capitalisation,
`Base::Color`, and the Selection path before it ever reaches the change you
wanted. Stage 2 in particular converts a class of merge conflicts into
nothing.

Stage 2a alone (twelve forwarders) makes any upstream file that logs
compile here unchanged.
