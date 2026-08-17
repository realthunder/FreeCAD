# Closing the core-API gap with upstream, starting with what FEM needs

Status: stage 1 and steps 2a and 2d done 2026-08-12; stage 1b (the additive
templates, section 2.1) done 2026-08-17, as are FastSignals (2.2) and the
`std::string` units API with its unit audit (2.3); 2b and 2c still to do;
stage 3 waits on the FEM port itself. The two fork-side signature
questions in section 4 were decided 2026-08-17: keep ours, both of them.
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

## 2.1 Stage 1b -- the additive templates (done 2026-08-17)

The second round of additive work, and unlike stage 1 it is not FEM-shaped
at all: these are core APIs upstream grew that we can simply *also* have.
Every one is a new overload or a new name beside an existing one, so no
fork call site changes and nothing can regress. The FEM census
([FemPortEvaluation.md](./FemPortEvaluation.md) section 10.3) is what named
them, because closing stage 1's rows unmasked them -- but the reason to do
them is the fork's own sync cost, not FEM.

| item | what | where | existing sites it sits beside |
|---|---|---|---|
| 1e | `XMLReader::getAttribute<T>()` in six instantiations, plus the generic and enum forms | `Base/Reader.h`, `Base/Reader.cpp` | 230 `getAttribute` + 498 `getAttributeAs*` |
| 1f | `Document::addObject<T>()`, `countObjectsOfType<T>()`, `GroupExtension::addObject<T>()`, `ExtensionContainer::getExtension<T>()` | `App/Document.h`, `App/GroupExtension.h`, `App/ExtensionContainer.h` | 2472 `addObject(`, 114 `getExtensionByType<` |
| 1g | `Base::color_traits` and its `Base::Color` specialization | `Base/Color.h` | -- |
| 1h | `PropertyMaterialList` float and packed-rgba colour setters, both arities, plus the no-argument getters | `App/PropertyStandard.h` | -- |
| 1i | `fastsignals` as a name for `boost::signals2` | `src/fastsignals/signal.h`, included from `App/Property.h` and `Gui/ViewProvider.h` | -- |

Not one of those sites changed.

**Why nothing can resolve differently.** A non-template wins overload
resolution against an equally good template, and none of the new templates
can deduce `T` from the arguments -- a caller has to name it. So
`getAttribute("Name")` and `addObject("Part::Feature", "Box")` still reach
the overloads they always did. The new setters and getters differ in
arity or parameter type from every existing one.

Notes that matter when reading them:

- **1e** upstream declares the instantiated set with a `requires` clause
  over a private `instantiated<T>` variable template and defines it out of
  line, so an unsupported `T` is a compile error at the call site rather
  than a link error at the end of the build. That is worth copying and is
  copied. The bodies are ours: they go through `findAttribute()`, and the
  throwing form raises through `_FC_READER_THROW`, which carries the file
  and line the fork's readers report.
- ! **1e, one deliberate divergence.** Upstream's `bool` cast is
  `value != "0"`, which makes the string `"false"` read as **true**. That
  is safe upstream only because nothing spelled that way goes through it:
  `PropertyBool` writes `"true"`/`"false"` and restores it by comparing the
  string itself. Our `Writer` spells bools the same way, so ours honours
  both words. Upstream's `"1"`/`"0"` attributes read identically.
- **1f** upstream's template says `T::getClassName()`, a `consteval` string
  its `PROPERTY_HEADER_WITH_OVERRIDE` macro emits, guarded by static
  assertions about namespace-versus-directory. We have no such member and
  adding one is a change to every property container rather than an
  addition, so ours uses `T::getClassTypeId().getName()` -- the same
  string, and the one the non-template overload looks up anyway.
- **1g** only the adapter is taken. Upstream also routes `Color::setValue`
  and `Color::asValue` through it, which would change what ours do: theirs
  carry the alpha and round, ours drop the alpha and truncate. That is a
  behaviour change to every colour button in the GUI, so it is not part of
  an additive step. Left as a separate decision.
- **1h** the no-argument getters read entry 0. Upstream indexes element 0
  of a list it does not check, so an empty property is undefined behaviour
  there; ours go through the indexed getter, which answers with the field
  default when the array is short.
- **1i** supersedes 1c above, which proposed retyping FEM's five uses
  instead. The census showed the real number is 51 (17 App, 34 Gui) once
  the rows above it came off, and almost none of them include anything for
  it -- upstream's core headers put the name where every DocumentObject-
  side file picks it up transitively. So the header goes where ours already
  include `boost/signals2.hpp`, and the alias makes the two names one
  namespace: ported code gets exactly the connection type our core returns,
  in both directions, with nothing rewritten.

! **1i was superseded within the day: the fork took the library itself.**
The alias described above no longer exists. See section 2.2.

**Verification.** A full build of `build/conda-debug-occt801`, plus
syntax-only checks that assert the resolved return type of every existing
call shape and of every new one -- that is what "not one of those sites
changed" rests on, since a silent re-resolution is the only way an
additive overload can hurt -- plus the FEM census
(`scratchpad/fem_census.py`, `--rev 512e91aad0`) before and after.

Every bucket these five were chosen to close is gone:

| bucket | App before | Gui before | after |
|---|---|---|---|
| `fastsignals` | 17 errors / 17 files | 29 / 29 | 0 |
| `XMLReader getAttribute<T>` | 17 / 1 | -- | 0 |
| `addObject<T>` / `getExtension<T>` | 9 / 4 | 5 / 2 | 0 |
| `Base::color_traits` | -- | 6 / 2 | 0 |
| `ShapeAppearance` | -- | 5 / 2 | 0 |

Upstream's `Fem/App` goes from 13 TUs compiling clean to 29 of 50. The
census shim supplies a `fastsignals/signal.h` of its own and was present
in *both* runs, so that row moved because of the transitive include, not
the shim.

What is left is what the evaluation predicted and none of it is additive:
`getElementTypes` and `isSame` (section 4), `App/Datums.h`, the `Quantity`
strings, the `Gui::PropertyEditor` drift, and the generated-file
artifacts. Gui's clean count does not move at all, because every one of
its TUs also fails on those.

## 2.2 Taking FastSignals itself (done 2026-08-17)

Section 2.1 item 1i made `fastsignals` another name for `boost::signals2`,
and argued that adopting the library for its own sake was a separate
decision. That decision was then taken: the fork's signals **are**
FastSignals now, `src/3rdParty/FastSignals` is vendored as upstream has it,
and `boost::signals2` is gone from `src/` entirely.

**The measurement that justified it.** Not the library's own benchmark --
ours, on the shape our signals actually have,
`signal<void(const A&, const B&)>`, which is what `App::Document` fires per
object per property change. Both sides compiled `-O2`:

| slots | boost ns/emit | fastsignals ns/emit | speedup |
|---|---|---|---|
| 0 | 18.8 | 5.9 | 3.2x |
| 1 | 45.7 | 13.4 | 3.4x |
| 4 | 87.2 | 47.4 | 1.8x |
| 8 | 141.6 | 93.5 | 1.5x |
| 16 | 259.2 | 199.1 | 1.3x |
| connect+disconnect, 1 slot | 128.7 | 47.2 | 2.7x |
| connect+disconnect, 8 slots | 113.2 | 50.0 | 2.3x |

The gain is largest where the per-emission overhead dominates, which is
where our signals live: most fire to a handful of observers. Connect cost
matters on document load, where every object wires its observers.

! **A benchmark trap worth naming, because the first run said the exact
opposite -- 3x SLOWER.** Boost.Signals2 is header-only, so it is compiled
with the *calling* translation unit's flags; FastSignals is a library, so it
is compiled with its own. Measuring a `-O2` benchmark against our Debug
build of the library compares `-O0` code with `-O2` code and says nothing
about either. Compile both sides the same way or throw the number away.

**Two changes to the vendored copy**, both marked `[FreeCAD fork addition]`
in place so a future re-sync sees them:

- **A front connect.** `Gui::Document` connects to `signalNewObject` ahead
  of every other observer on purpose (`2d693c0423`, "signal new object in
  Gui::Document first before others"), and FastSignals had no ordering
  control at all. Its iteration algorithm rests on the id array being
  sorted ascending, so the id space is split at 2^32: front slots count
  down from it, ordinary slots count up. A new front id is then smaller
  than every id present, which is what lets the insert at the beginning
  keep the array sorted. `remove()` and `get_next_slot()` only ever assumed
  "sorted", never "starts at 1", so neither changed.
- **A local named `slots`**, which Qt `#define`s to nothing. Every
  translation unit that reached a Qt header before `signal.h` stopped
  parsing inside it. Upstream survives this on include-order luck; a header
  does not get to assume include order.

! **The one semantic difference, and it is a real one: blocking is decided
at connect time.** Boost.Signals2 can block any connection whenever it
likes. FastSignals wraps a blockable slot in a check when it is connected,
so the ability has to be asked for -- `connect(slot, advanced_tag {})` --
and the connection has to be stored as `advanced_connection` (or
`advanced_scoped_connection`; the plain scoped type deliberately refuses an
advanced connection rather than silently dropping the block). Thirteen
sites block a connection, and `Base::ConnectionBlocker` now takes the
advanced type, so a connection that forgot to opt in is a compile error
rather than a block that quietly does nothing. Converted: the two in
`Gui::Document`, the parameter-change connections in `Action`,
`DockWindowManager`, `MainWindow`, `PropertyView` and `ToolBarManager`,
`TaskSketcherConstraints`, `TaskSectionView`, `TaskOrthoViews`,
`App::TextDocument::connectText`, and the four registrations in
`SketcherToolDefaultWidget`.

**`src/boost_signals2.hpp` is gone.** It was a fork chokepoint header, a
workaround for a boost >= 1.74 deprecation warning, included by 53 files.
The workaround is moot and the name would now be a lie, so the 53 includes
name the library directly.

**What the migration guide warned about and what actually happened.**
Boost.Signals2 pulls in a great deal of the standard library transitively;
FastSignals does not. Across the whole tree that cost exactly one missing
`#include <list>`, in `App/Application.h`. Nothing used `connect_extended`,
`slot::track`, `track_foreign`, a custom combiner, or a non-void signal, so
none of the genuine incompatibilities applied -- all 181 signals are
`void(...)`.

**Verification.** A full build; the vendored library's own suites (265
assertions in 55 cases, plus the concurrency stress test); seven ordering
tests for the front connect under ASan and UBSan; and the runtime probe
that drives a document through `App::DocumentObserver`, which is a pure
signal consumer, so a dropped signal shows up as a missing callback.

## 2.3 The units API becomes `std::string` (done 2026-08-17)

Chosen by the user as the next task after 2.2, and the only one of the four
parked API questions that was a real break rather than a keep-ours. The
reason to take it is direction, not FEM: it is upstream getting Qt out of
`Base`, which is where the WASM tier needs to go anyway.

**What changed.** Every string on the units path:

| API | was | now |
|---|---|---|
| `Quantity::getUserString` (3 overloads), `getSafeUserString` | `QString` | `std::string` |
| `Quantity::parse`, `Quantity(double, unit)` | `QString` | `std::string` |
| `Unit::getString`, `getTypeString`, `Unit(expr)` | `QString` | `std::string` |
| `Unit::getStdString` | -- | gone, it *is* `getString` now |
| `UnitsApi::schemaTranslate`, `toString`, `toNumber`, `getDescription` | `QString` | `std::string` |
| `UnitsSchema::schemaTranslate`, `toLocale`, `getAngleUnit` | `QString` | `std::string` |

That is `Quantity.h`, `Unit.h`, `UnitsApi.h` and `UnitsSchema.h` plus the
seven schema subclasses, and it leaves all four **headers Qt-free**.

**Deliberately NOT done: Qt is still there in the `.cpp`.** Upstream went
further and replaced `QLocale` number formatting with ICU, and rewrote the
seven schema subclasses into the data-driven `UnitsSchemasSpecs` tables.
That is a separate workstream with a new dependency, so here `QLocale`
stays as an implementation detail of `UnitsSchema::toLocale` and
`UnitsApi::toNumber`. Note `QString::arg(double, ...)` formats in the C
locale, so keeping it *preserves* the documented "C locale" contract of
`toString`/`toNumber` exactly -- reimplementing it was the riskier option.

**Traps this specific change carries.**

1. WARNING `getDescription` is a *translated* string. Its `tr()` came from
   `Q_DECLARE_TR_FUNCTIONS(UnitsApi)`, whose context is the bare
   `"UnitsApi"` -- exactly what `QCoreApplication::translate("UnitsApi",
   ...)` produces, so both the runtime lookup and the existing
   `Base_*.ts` entries keep matching. It is written with
   `QT_TRANSLATE_NOOP` so lupdate still sees the literals.
2. WARNING WARNING Two of those literals contain `m2`/`m3`. They are **marked
   `nonascii-ok`**, because the post-commit ASCII hook rewrites
   non-ASCII on lines a commit adds, and transliterating a translated
   source string silently unmatches it from every `.ts` file.
3. The caller sweep is 58 files. The two primitives dialogs alone were
   173 sites, all `X->value().getSafeUserString()` inside a `QString`;
   they now go through a `safeQuantityQString()` helper, which is
   **upstream's own answer** to the same problem, so that code stays
   merge-aligned.
4. NOTE A scripted sweep is the right tool here, but a regex whose left
   edge is `\w+` **matches mid-token** and spliced
   `QString::fromStdString(` into the middle of identifiers in
   `PropertyItem.cpp` (`Base::QString`, `uQString`/`nit`). Anchor on
   `(?<![\w:.>])`, and grep for `[A-Za-z0-9_]QString::fromStdString`
   afterwards -- it finds exactly that damage.

### 2.3.1 The unit audit that came with it

Asked for alongside the string change: which units has upstream added
since the fork, and are they reachable in *expressions* here?

The two trees resolve units in completely different ways, and the fork's
is the better one to add to. Upstream spells every unit as its own
**lexer rule** in `Base/Quantity.l`. The fork has a single catch-all rule
that defers to `Quantity::fromUnitString`, so the `Quantity::unitInfo()`
table is the one source of truth -- and `UnitExpression::create` resolves
through `Quantity::getUnitInfo()` off that same table. **A row added to
`unitInfo()` is therefore live in the expression engine for free**; there
is no second list to update.

Diffing upstream's 131 lexer spellings against our 130 table rows:

- **10 spellings were missing, now added**: `nA`, `uA`, `nmol`, `umol`,
  `nW`, `uW`, `mT`, plus a micro-sign row beside each of `uA`, `umol`
  and `uW` (the fork's convention is one row per accepted spelling, with
  `alias` pointing at the ASCII one). They need 7 new constants:
  `NanoAmpere`, `MicroAmpere`, `NanoMole`, `MicroMole`, `NanoWatt`,
  `MicroWatt`, `MilliTesla`.
- **No unit family is missing beyond those.** A second diff of the
  `const Quantity Quantity::X(...)` definitions found only four
  apparent gaps (`Degree`, `Torr`, `mTorr`, `yTorr`) and all four exist
  here, merely line-wrapped past the regex.
- 9 spellings are **fork-only and stay**: `inch`, the `N/m` family,
  `kmh`, `km/h`, `mi/h`.
- `mph` maps to `Quantity::MPH` here and `MilePerHour` upstream. Naming
  only, same unit.

WARNING `NanoWatt` is `1e-3` and `MicroWatt` is `1.0` -- these look wrong and
are right. The table is in internal units (mm, kg, s), where
`Watt == 1e+6`. Both trees agree on that scale, so upstream's numbers
transfer unchanged.

WARNING `"\xC2\xB5" "A"` is written as **two literals on purpose**: `A` is
a hex digit, so `"\xC2\xB5A"` would be eaten as one escape sequence. The
existing micro-farad row already had to do this. The micro-mole and
micro-watt rows do not, because `m` and `W` are not hex digits.

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

  WARNING: **done 2026-08-12, and the alias does not cover everything.** Seven
  headers forward-declare `namespace App { class Color; }` -- Gui's
  `ViewProvider.h`, Part's `TopoShape.h`, Mesh's `Importer.h` and
  `Gui/ViewProvider.h`, Points' `PointsFeature.h`, TechDraw's `DrawHatch.h`
  and `Preferences.h`. A using-alias and a class declaration of the same
  name cannot coexist, so those seven declare `Base::Color` instead and say
  `Base::Color` in their own signatures. The other 836 sites needed nothing.
  Note this fork's `App::Color` is *not* upstream's `Base::Color`: upstream's
  has grown a `color_traits` template and more besides. This step moved ours
  and left the content alone; adopting their additions is separate.

**Verification for each step**: a full build of the eval tree plus the
existing probe suites. Because these are aliases, a step that compiles is
very close to a step that is correct -- but 2d in particular should also get
a render-path check (`ground_parity_probe.py`, an audit leg) since colour
flows into the render cache.

## 4. Stage 3 -- fork-side conflicts: keep the fork, adapt FEM

Here the fork is the one that diverged, so upstream's FEM must bend to us.
All four are FEM-side edits with **no core change**, which is exactly what
the policy asks for.

The first two were open questions until **2026-08-17, when the user decided
both: keep ours, no core change.** They are settled; a later session should
not reopen them, only honour the FEM-side consequence recorded with each.

- `ComplexGeoData::getElementTypes()` returns `const std::vector<const char*>&`
  here (our `75a7f709d8`), by value upstream. **DECIDED: keep ours** -- it
  avoids a copy in element-mapping paths. Adapt `FemMesh`'s override to
  return a reference to a static table.
- `App::Property::isSame` is **pure virtual** here, and upstream gives it a
  default body comparing type and `getMemSize`. **DECIDED: keep ours pure.**
  It forces every property to answer the question deliberately, and all 23
  fork implementations already do. The consequence is that upstream's
  `PropertyPostDataObject` is *abstract* here, so a FEM port must implement
  `isSame` on it -- it will not compile otherwise.
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
already fill the same arrays: `ViewProviderPartExt::setHighlightedFaces`
(ViewProviderExt.cpp:1547) sets `SoMaterialBinding::PER_PART` and fills the
diffuse, ambient, specular and emissive arrays per face today. Upstream's
contribution is wiring that path to a persistent, user-facing property, not
teaching Coin to draw it.

**Filling those arrays is not the same as drawing them, and both this
document and the analysis behind it originally conflated the two.** Coin
consumes only diffuse colour and transparency per face; ambient, specular,
emissive and shininess are read once, at index 0. See
[ShapeAppearanceDesign.md](./ShapeAppearanceDesign.md) section 5.1 for the
proof. So neither side renders per-face specular -- upstream fills four
arrays that the lazy element never reads per index.

What it costs is 4.5x the per-face storage, the three-into-one migration on
a base class, and that regression tail.

### 5.1.1 Is it wasteful? Only in the per-face case

`App::Material` is 18 floats (four colours plus shininess and transparency)
against `App::Color`'s 4, so a 10k-face imported solid carries roughly
720 KB of appearance instead of 160 KB, in memory and in the saved
document. On STEP import 14 of those 18 floats per face are defaults,
because the reader only ever supplies colour and alpha. That is the real
inefficiency and it is worth naming.

But the uniform case, which is nearly every object, goes the other way, and
our own tree is the reason. `Gui::ViewProviderGeometryObject` here carries
**three overlapping properties** -- `ShapeColor`, `ShapeMaterial` and
`Transparency` -- holding duplicated state that `onChanged` has to keep
manually in sync: setting `ShapeColor` writes into
`ShapeMaterial.diffuseColor`, and `ShapeMaterial`'s transparency writes back
into `Transparency` (`ViewProviderGeometryObject.cpp:154-175`). One
`ShapeAppearance` entry is both smaller than that pair and free of the sync
obligation. Upstream's unification is a simplification for the common case;
it is only the per-face list that bloats.

Upstream also only binds `PER_PART` when the list actually has more than one
entry, so the cost is not paid by objects with a uniform appearance.

### 5.1.2 Did anyone object?

Not at design time, as far as the record shows -- the change rode in with the
1.0 Materials work, which was broadly wanted. The objections arrived
afterwards as a long tail of bug reports, which is the more honest measure of
the disruption:

- #14938 DiffuseColor ignores transparency
- #14414 DiffuseColor and ShapeColor applied inconsistently
- #15170 changing the whole object's ShapeAppearance no longer clears
  per-face overrides -- a behaviour regression against 0.21
- #19048 colour components rounded when changing ShapeAppearance
- #15027 per-face transparency handled incorrectly
- #18152 glTF alpha channel misinterpretation
- #20213 line and point colours *still* use RGBT rather than RGBA
- #23444 Link MaterialOverride does not override line and point colours

Third-party macros ended up testing for both spellings, `ShapeColor` for
older versions and `ShapeAppearance` for 0.22 on. #20213 and #23444 indicate
the migration is still not finished two years on -- which is the strongest
argument against this fork taking it on as FEM collateral.

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
