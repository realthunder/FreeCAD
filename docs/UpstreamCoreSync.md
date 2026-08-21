# Closing the core-API gap with upstream, starting with what FEM needs

Status: stage 1 and steps 2a and 2d done 2026-08-12; stage 1b (the additive
templates, section 2.1) done 2026-08-17, as are FastSignals (2.2) and the
`std::string` units API with its unit audit (2.3) and the colour-traits
routing (2.4); steps 2b and 2c done 2026-08-18, which completes stage 2;
stage 3's four items were re-checked against the tree 2026-08-18 and all
four already hold in the fork's own FEM (section 4.1), so stage 3 has no
edit left until the FEM port itself brings upstream's sources in. The two fork-side signature questions in section 4 were decided
2026-08-17: keep ours, both of them. **All four parked API questions are
now answered.**
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
- **1g** only the adapter was taken here. Upstream also routes
  `Color::setValue` and `Color::asValue` through it, which changes what
  ours do: theirs carry the alpha and round, ours dropped the alpha and
  truncated. That is a behaviour change to every colour button in the
  GUI, so it was not part of an additive step. **That routing was taken
  separately in section 2.4**, where the behaviour change is the subject
  rather than a side effect.
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

## 2.4 Colour conversion goes through the traits (done 2026-08-17)

The last of the four parked API questions, and the one the user asked to
have *verified* rather than merely adopted -- correctly, because taking it
unexamined would have made six TechDraw colours invisible.

`Color::setValue<T>`/`asValue<T>` now do what upstream's do:

|  | before | after |
|---|---|---|
| alpha | dropped, `asValue` always opaque | carried both directions |
| 8-bit conversion | `int(r * 255.0f)`, truncating | `std::lround(r * 255.0F)` |

Only `QColor` is ever used for `T` (72 `asValue<QColor>`, 24
`setValue<QColor>`), and `QColor(int,int,int,int)` exists, so the generic
`color_traits<T>::makeColor` fits without a new specialization.

**What rounding actually changes -- measured, because the obvious guess
is wrong.** It does *not* fix a widget round trip. For a colour whose
components came from an 8-bit value, `float(v)/255.0f * 255.0f`
truncates back to exactly `v` for **all 256 bytes**, so
`QColor` -> `Color` -> `QColor` was already the identity and still is
(verified across all 256 values and all 256 alphas).

Where the two disagree is colours whose floats did *not* come from a
byte -- set from Python, computed, interpolated, or read off a material.
There truncation and rounding differ for **500 of 1001** sampled values,
always by one step and always in the same direction: truncation lands
low. `0.5` became 127 and is now 128, `0.1` became 25 and is now 26. So
the change removes a systematic one-step-darker bias on every computed
colour, which is worth having, but it is not the dramatic fix it first
looks like.

### 2.4.1 WARNING WARNING The six defaults this would have made invisible

FreeCAD packs colour preferences as `0xRRGGBBAA`. Six TechDraw defaults
name an opaque colour but **omit the alpha byte**, so the packed value
says fully transparent. That was harmless only for as long as `asValue`
threw the alpha away:

| pref | default was | means | fixed to |
|---|---|---|---|
| `Tracker/TrackerColor` | `0xFF000000` | red, alpha 0 | `0xFF0000FF` |
| `Colors/TileColor` | `0x00000000` | black, alpha 0 | `0x000000FF` |
| `Colors/Background` (QGVPage, QGSPage) | `0x70707000` | grey, alpha 0 | `0x707070FF` |
| `Colors/Hatch` | `0x00FF0000` | green, alpha 0 | `0x00FF00FF` |
| `Colors/GeomHatch` | `0x00FF0000` | green, alpha 0 | `0x00FF00FF` |
| `Decorations/HighlightColor` | `0x00000000` | black, alpha 0 | `0x000000FF` |

All six reach `asValue<QColor>()`, directly or through a `PropertyColor`
default that a colour button then reads.

NOTE **Upstream has this bug.** Their `setPackedValue` is byte-identical
to ours and their defaults still read `0x70707000`, `0x00FF0000`,
`0x00000000` -- with the traits routing in place. They fixed exactly one
of them, `TrackerColor`, and changed its colour to blue while doing it.
So this is not a case of the fork being behind; it is a case of the fork
being *told to look*. The fix keeps each pref's intended RGB rather than
adopting upstream's new hue.

A seventh site is the same mistake reached by a different route:
`DlgProjectionOnSurface` set `LineColor`/`ShapeColor`/`PointColor` to
`0x8ae23400` -- opaque green with the alpha byte left off. It never goes
through a colour button, but `PropertyColorItem::decoration` fills the
property-editor swatch with whatever `asValue<QColor>()` returns, so the
swatch would have gone blank. Fixed to `0x8ae234ff`.

Two neighbouring families were deliberately **left alone**, because
nothing converts them with `asValue` and changing them would be an
unrelated behaviour change:

- `Part/Dimensions3dColor`, `DimensionsDeltaColor`,
  `DimensionsAngularColor` -- read as `SbColor(c.r, c.g, c.b)`, which
  never looks at alpha.
- `ViewParams` `AxisXColor`/`AxisYColor`/`AxisZColor` (`0xCC333300` and
  friends) -- stay a packed `unsigned long` all the way into
  `SoFCCSysDragger::setAxisColors` and never become a `QColor`. Their
  alpha byte is 0 too, and inconsistent with the `getPackedValue(0.0f)`
  Coin uses for the same dragger's own defaults, but that is a separate
  question about dragger colours.

### 2.4.2 What the GUI audit found, path by path

The instruction was to verify the colour, not just the conversion, so
every route from a widget to a `Base::Color` was checked:

- **Colour buttons** (`Gui::ColorButton`, `Gui::PrefColorButton`) --
  unchanged. `allowTransparency` defaults to *false*, and
  `PrefColorButton::restorePreferences` explicitly does
  `value.setAlpha(0xff)`, so every button hands back an opaque `QColor`
  exactly as before. This is what makes the 24 `setValue<QColor>` sites
  safe.
- **`TaskElementColors`** -- the one place that opens a
  `QColorDialog` with `ShowAlphaChannel`. It never used `setValue`: it
  builds `App::Color(c.redF(), c.greenF(), c.blueF(), c.alphaF())` by
  hand, so it already carried the alpha. Unchanged.
- **`Clipping`'s `backlightColor`** -- a `PrefColorButton` with
  `allowTransparency` unset, so alpha is forced to 255 before it reaches
  `setValue<QColor>`. Unchanged.
- ! **The material row** (`materialCall`) -- unchanged in behaviour, but
  it carried a comment saying "Not sc.a: the QColor above never carried
  one", which this change falsified. The workaround is still correct and
  stays: `mat.specularAlpha` and `mat.transparency` are full floats,
  while a `QColor` alpha is 8 bits, and the PBR metallic factor wants
  the precision. Only the comment changed.
- **Per-face and `DiffuseColor`** -- `asValue<QColor>()` now reports the
  stored alpha instead of always 255, which is the point; the editor
  swatch shows the real colour. Nothing had to change, given the
  document data is right.

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

  **Done 2026-08-18.** 18 files moved; forwarding headers left at the five
  old paths that have outside call sites (`Selection.h` 177 includers,
  `SelectionObject.h` 55, `SelectionFilter.h` 25, `SelectionView.h` 6,
  `SelectionFilterPy.h` 1). `SelectionObserverPython.h` got none: nothing
  outside includes it. Three things this step actually needed:
  - WARNING WARNING **a forwarding header must not reuse the moved header's
    include guard.** It would define the guard and then include the real
    file, whose `#ifndef` is now false, so every consumer would silently
    see an *empty* header. Each forwarder uses its own `*_FORWARD_H`.
  - `Selection/SelectionView.h` had to stop using upstream's quoted
    `"DockWindow.h"`. A quoted include resolves relative to the including
    file's own directory first, and that header is no longer a sibling.
    Invisible inside Gui, which has `-I src/Gui`; it broke `Mod/Part/Gui`
    on the first build. Upstream can keep the quoted form only because
    nothing outside Gui includes that header there.
  - the generated binding now lands in a subdirectory of the build tree, so
    `generate_from_xml` takes the path and
    `${CMAKE_CURRENT_BINARY_DIR}/Selection` joins the include dirs.

  NOTE **Not moved: the `SoFCSelection` / `SoFCUnifiedSelection` /
  `SoFCSelectionAction` / `SoFCSelectionContext` family**, which upstream
  also keeps in that directory. 92 more includers, and upstream's FEM only
  ever needs `Selection.h`, `SelectionObject.h` and `SelectionFilter.h`
  from here. A separate step if it is ever wanted.
- **2c** is a rename of both the header and the class names
  (`ViewProviderPythonFeatureT` -> `ViewProviderFeaturePythonT`, etc.). A
  shim proved during the evaluation that FEM's 23 uses need nothing else.

  **Done 2026-08-18**, and the shim was too optimistic: a *shim* only has
  to satisfy the compiler, and two of the three problems here are invisible
  to it. Note upstream's fourth name swaps the words the other way, to
  `ViewProviderGeometryPython`.
  - WARNING WARNING **The class name is also a runtime STRING.** View
    providers are created by name -- from `getViewProviderName()`, from a
    document's `ViewType` attribute when it overrides the default, and from
    Python's `addObject(..., viewType=...)`. Renaming the class changed what
    `PROPERTY_SOURCE_TEMPLATE` registers, so `App/FeaturePython.cpp` and
    `Mod/Fem/App/FemAnalysis.cpp`, which return the old spelling as a
    literal, stopped resolving. **The build was completely clean and every
    `App::FeaturePython` silently had `ViewObject == None`.** Only a runtime
    probe finds this.
  - So the old spellings are also registered as type names, with
    `Base::Type::createType(<new>::getClassTypeId(), "<old name>",
    &<new>::create)`. An old name then still produces a view provider, and
    the instance reports the new type as its own. Verified both ways round,
    with a bogus name as the control.
  - WARNING **An alias template is not a template-name everywhere the real
    one is.** Deriving from it is fine; it **cannot be explicitly
    instantiated**. The 30 `template class <MOD>Export
    ViewProviderFeaturePythonT<X>;` lines across 25 files therefore name the
    real template -- correct anyway, since an explicit instantiation is a
    definition rather than a use. It cannot be forward-declared either;
    nothing does today.

  NOTE Persistence was checked rather than assumed: `GuiDocument.xml` stores
  `<ViewProvider name="...">` with the *object* name only, and
  `Document.xml` writes a `ViewType` attribute **only when it differs from
  the object's default**. So ordinary documents re-derive the class name on
  load and are unaffected; the stored-override case is what the type-name
  aliases above cover.
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

### 4.1 Checked against the tree 2026-08-18: all four already hold in-tree

The four above are instructions for the *ported* sources. Against the FEM
this fork actually carries they were re-checked, and **every one is already
satisfied** -- so stage 3 has no edit to make until upstream's FEM lands.

| item | state in `src/Mod/Fem` |
|---|---|
| `getElementTypes()` by const-ref | `FemMesh.cpp:2653` already returns a reference to a function-local `static` table -- exactly the prescribed adaptation |
| `isSame` pure virtual | `PropertyPostDataObject.h:86` and `FemMesh.h:71` both define it; signatures match the two pure bases (`Property.h:320`, `ComplexGeoData.h:373`), so they really do override |
| `signalHighlightObject` on `Application` | `Gui/ActiveAnalysisObserver.cpp:78` already calls it through `Gui::Application::Instance` |
| `acceptReorderingObjects` | absent from the fork's FEM entirely; the fork's own hook is `ViewProvider::canReorderObject` (`Gui/ViewProvider.h:418`), which no FEM view provider overrides |

WARNING WARNING **This table is not evidence that stage 3 is discharged.**
The FEM in this tree is the Dec-2023 fork FEM -- merge base `a662fbb2ff`,
our delta since 50 files / +253 -294, upstream's 1018 files / +414859
-164906. The four items above are adaptations *upstream's* `FemMesh`,
`PropertyPostDataObject` and `ViewProviderFemPostPipeline` will need, and
none of those sources are here. That our own older versions happen to
satisfy the same four constraints shows what the adaptation looks like; it
does not do the adaptation. **All four remain real work whenever the port
happens.** The only claim section 4.1 supports is the narrow one: there is
no edit to make in *this* tree today.

NOTE Separately, and for a different reason: `BUILD_FEM=OFF` in all four
user presets, so the fork's own FEM has not been compiled against the core
that stage 2's four renames moved under it -- and 2c's runtime-string trap
landed in `Mod/Fem/App/FemAnalysis.cpp` specifically. FEM *ships* (the
feedstock builds `BUILD_FEM=ON`), so that is a live regression hole in a
released module. It is the standing "Option B" of
[FemPortEvaluation.md](./FemPortEvaluation.md) -- worth doing on its own
merit, but it verifies the fork's FEM, not the port, and not this table.

## 5. Consult -- cannot be done while keeping fork behaviour

These are why this document ends with questions rather than steps. 5.1
has since been decided and is kept for its record; 5.4 was added
2026-08-18 and is the live one.

### 5.1 ShapeAppearance -- DECIDED 2026-08-13, adopted

WARNING WARNING **This section is kept for its research, but it is no
longer an open question and its tables below describe a tree that no
longer exists.** The fork adopted the consolidation on 2026-08-13 (the
per-face appearance workstream). `Gui::ViewProviderGeometryObject` today
declares exactly three properties -- `Transparency` (line 147),
`ShapeAppearance` (154) and `BoundingBox` (157). `ShapeColor` and
`ShapeMaterial` are gone as properties; `ShapeColor` survives as a facade
whose storage *is* `ShapeAppearance` (see the comment at line 43). So the
"ours" column of the table below, and the "Why it is still a consult item
here" paragraph, argue against a change that has since been made. Read
the rest as the record of why it was made, not as a pending decision.

** **`ShapeMaterial` names two different things, and it is easy to
conflate them.** Everything in this section is about the **rendering**
material -- `App::Material`, ambient/diffuse/specular/shininess, living
on the **view provider**. The fork's removed `ShapeMaterial` was an
`App::PropertyMaterial`, that same rendering type. Upstream *also* has a
`ShapeMaterial`, but it is `Materials::PropertyMaterial` holding a
`Materials::Material` -- the **physical** material card (density,
Young's modulus, thermal conductivity), declared on **`Part::Feature`**
at `PartFeature.h:77`. Same identifier, different type, different
concept, different layer. Section 5.4 is about that one; this section is
not.

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

### 5.2 The Materials module -- PORTED 2026-08-18

**No longer a consult item.** `src/Mod/Material` was replaced wholesale
with upstream's tree and builds and loads here. What made it safe was that
nothing in this tree consumed it -- an island, so the blast radius was the
module itself. The core side cost four additive APIs
(`App::CleanupProcess`, `App/TransactionDefs.h`,
`Base::Color::get/setPackedRGB`, `Gui::QtTools::deleteKeySequence`) plus a
`TaskDialog::addTaskBox` overload; the rest was six module-side
adaptations to this fork's APIs. The original text follows.

Upstream FEM does `import Materials` in 13 files. Our `src/Mod/Material` is
the December-2023 vintage: python module named `Material`, `.xml` bindings.
Upstream's is renamed to `Materials`, has diverged by 346 files / +25515
lines, and gained `ExternalManager`, `Library`, `MaterialFilter`.

There is no alias trick here: it is a second module port of real size, and
material cards are user data.

**Question:** is a Materials port in scope at all? If not, FEM's material
task panels have to be held at our vintage, which means diverging from
upstream FEM in the one place upstream FEM changed most.

### 5.2.1 Exercised in a real GUI 2026-08-18

Everything above compiled and loaded, but none of the GUI half had ever
run. It has now, on the desktop against the real GPU (WSLg, Mesa d3d12,
bgfx renderer in render-cache mode 3), driven through the in-process MCP
debug console.

What holds:

- The appearance dialog contract. `DlgMaterialPropertiesImp` was reached
  from the fork's Display properties panel with two objects selected:
  edits land live on every selected view provider (shininess 0.2 -> 0.8,
  diffuse -> red, on both objects, with the 3D view answering), Cancel
  restores the snapshot exactly, OK keeps, and ShapeColor follows the
  diffuse colour.
- `TaskDialog::addTaskBox(icon, widget)` -- the icon shows on the task box
  header of both Inspect panels.
- `MatGui.MaterialTreeWidget` constructs from Python, and the widget
  inside the preferences page populates (System library, 98 leaves) and
  drives its line edit on selection.
- The Materials editor opens, loads a card, renders the appearance preview
  sphere, and adds a model through the model selector.
- `ImageEdit`'s file dialog: the fork's `";;"` filter string parses into
  the two filters it should ("Image files (*.jpg *.jpeg *.png *.bmp)",
  "All files (*)").

Two defects found and fixed:

- The module registered a second `Std_SetAppearance`. The fork already
  owns that command, the first registration wins, and the module's copy
  only logged "duplicate command Std_SetAppearance" on every load. That
  was first fixed by not registering it, and then settled properly by
  taking upstream's migration -- see 5.2.2.
- `PrefMaterialTreeWidget` never opted into the fork's auto-apply default
  the way every other preference widget does from its constructor, so the
  Default Material page alone deferred its write to the OK button.

### 5.2.2 The appearance dialog migration, taken 2026-08-18

The duplicate command was the visible half of a move this fork had never
taken. Upstream deleted `src/Gui/DlgDisplayProperties*` and the Gui-side
`Std_SetAppearance` when it moved that dialog into the Material module;
only `DlgMaterialProperties` stayed in Gui. Reaching the rest of the
application from inside a module is done with a `WorkbenchManipulator`
that injects `Std_SetMaterial` and `Std_SetAppearance` into the menus,
and `PartGui` does `import MatGui` so the module is loaded in an ordinary
session.

The fork now mirrors that. Gui lost the dialog, the command, its two
hard-coded menu entries and its build entries. The surviving dialog is
upstream's, with the fork's controls grafted back on -- material preset
list, shape colour button, the four colour-mapping check boxes, the
recompute preference, fractional point size and line width -- keeping
upstream's material-library picker beside them, so an appearance can come
from a material card as well as from the preset list or the editor. The
dockable variant was dropped: its call site had been `#if 0` for as long
as the fork has had it, and upstream's dialog has no such mode.

One fork-side adaptation was needed. Upstream's context-menu anchors do
not exist in this fork's menus (no `Std_RandomColor`, and
`Std_TreeSelection` sits inside the Selection submenu), so both context
recipients now anchor in front of `Std_RenderSettings`, which is what
followed `Std_SetAppearance` before the move. The menu bar keeps
upstream's `Std_ToggleNavigation` anchor, which lands in the same place
here.

Verified in a running GUI: the command exists at startup, every grafted
control drives its property on all selected objects, the library picker
sets `ShapeAppearance`, the custom appearance editor still edits live and
reverts on Cancel, and the View menu reads ToggleNavigation, SetMaterial,
SetAppearance, RenderSettings. The context-menu injection follows the
same helper but was not triggered from the harness.

Left standing on purpose:

- `Std_SetMaterial` is registered and opens, but nothing in this fork
  carries a `Materials::PropertyMaterial` named `ShapeMaterial`, so the
  panel is inert. That is section 5.4's open question, not a defect.
- `QLayout::addChildLayout: layout QHBoxLayout "" already has a parent`
  on every `MaterialTreeWidget` construction. The layout code is verbatim
  upstream, so this is an upstream cosmetic bug, not a port artifact.

### 5.3 App::Datums

Section 1d adapts FEM's two sites and needs nothing. But if the fork wants
upstream's datum model itself (App-level datum elements, local coordinate
systems), that is a feature port touching PartDesign, and it should be
decided on its own merits rather than as FEM collateral.

### 5.4 A physical material on Part::Feature -- the question this document never asked

Added 2026-08-18. Sections 5.1 and 5.2 between them discuss the *rendering*
material and the Materials *module*, and neither asks the question that
actually decides whether the module matters to this fork: **should a
`Part::Feature` carry a physical material?**

Upstream says yes. `PartFeature.h:29` includes
`<Mod/Material/App/PropertyMaterial.h>` and line 77 declares
`Materials::PropertyMaterial ShapeMaterial` -- a `Materials::Material`
card (density, Young's modulus, thermal conductivity) on the **data
object**. This fork has no equivalent: `Part::Feature` carries no material
property, and neither `Mod/Part/App` nor `Mod/Part/Gui` links `Materials`
or `MatGui`.

X **This is not section 5.1 and does not collide with it.** Upstream's
`ShapeAppearance` sits on `Gui::ViewProviderGeometryObject` exactly where
ours does, with the comment `// May be different from material` marking
the two as deliberately independent. Rendering appearance and physical
material are orthogonal; upstream carries both. What we lack is the
physical one -- a missing feature, not a design conflict. (The one place
they touch is `Materials::PropertyMaterial::setValue(const App::Material&)`:
a material card can also carry appearance models, so assigning one can
drive the rendering appearance. That is what upstream's comment hedges.)

**What makes it a consult item rather than a step:**
- It is a **feature decision, not an API alignment.** Nothing on the
  roadmap (`docs/RoadMap.md`) asks for physical materials -- the roadmap
  is renderer, headless engine, WASM tier, AI-native interface.
- It is a **document-format change to user data**: a new property on every
  `Part::Feature`.
- It makes the **geometry module depend on the material module at header
  level**. Every consumer of `PartFeature.h` inherits it.
- It is the **only reason this fork would need upstream's Materials
  divergence at all.** FEM's dependency is eight Python names, six of
  which we already have (`FemPortEvaluation.md` section 5.1). Part's is a
  compile-time coupling. Counted across all of upstream, Part is the *only*
  module outside Material that includes a Material header or links the
  libraries; CAM is the only other consumer of any kind (2 Python files).

So the ordering implied by the measurements is the reverse of the one in
`FemPortEvaluation.md`: the Materials module is not a prerequisite dragged
in by FEM, it is a prerequisite of *this* feature. Decide the feature
first; the module port follows from it or is not needed.

### 5.5 The divergences that compile and then fail at runtime (found 2026-08-21)

Stages 1-4 are about what compiles. The FEM port cleared the compiler and
then found a second class of divergence: code that builds cleanly here and
does the wrong thing at run time, because a fork optimisation changed a
semantic upstream's code relies on. All four below were found by running
upstream's own FEM test suites (90 App tests, 3 GUI tests) -- none of them
would have been caught by reading the sources.

**A no-op property write does not notify.** `Property::hasSetValue` compares
against the pre-change snapshot and, if the value is unchanged, returns
before `touch()` -- so no `onEarlyChange`, no `onChanged`, no
`signalChanged`. It is on by default (DocumentParams OptimizeRecompute).
Upstream always notifies. Any ported code that initialises state from its
own `onChanged` will silently skip that work whenever the value written
happens to equal what is already there -- and since defaults are the values
most likely to be written, the failure hides until someone uses the one face
whose normal is (0,0,1). Symptom in FEM: a reversed force constraint that
did not reverse. The fix belongs in the ported code: derive the state
directly rather than waiting to be told.

**A property write in a constructor closes the property table.** The write
reaches `findProperty`/`getPropertyList`, which calls `PropertyData::merge`,
and after that merge `PropertyData::addProperty` refuses to add a new static
property. So every `ADD_PROPERTY` must come before the first write to any
property. Upstream has no such rule and writes wherever it reads well.
Symptom: `Cannot add static property 'X'` -- and only in a debug build. A
release build takes the `!parentMerged` early-out, skips the registration,
and the property simply does not exist. Scan a ported module for it: look
inside each constructor for a `setValue`-style call followed by any
`ADD_PROPERTY`. Note that `setEnums`, `setConstraints` and `setScope` do not
notify and so do not merge.

**A document's view providers are not there when open() returns.** This fork
loads progressively: `Gui::Document` parks the view providers and a drain
builds them a slice at a time between returns to the event loop. Upstream
builds them during the load. A script that opens a document and reads
`obj.ViewObject` gets `None` here, and one `updateGui()` only runs one slice,
so the number of turns needed scales with the document. `Gui.Document`
has `flushLoad()` for this, and `getObject()` flushes on its own.

**Base.PropertyError did not exist.** Upstream raises it for a missing
property and its `onDocumentRestored` migrations catch it by name; the
`except` clause itself then raised `AttributeError` mid-restore and cost the
object its view provider. Now defined, deriving from `AttributeError` on
both sides so old catches still work.

The lesson for the next module port: **compiling is about half of it.** Budget
for running the module's own test suite, and expect the failures there to be
about fork optimisations, not about API names.

### 5.6 The units schemas become data (done 2026-08-21)

Eight hand-written schema classes here, one class driven by a specification
table upstream. Ported wholesale: `UnitsConvData.h`, `UnitsSchemasSpecs.h`,
`UnitsSchemasData.h`, `UnitsSchemas.cpp/.h` and the `UnitsSchema` engine, and
sixteen files deleted. Net about 975 lines gone.

What made it safe to take: upstream's table covers **every one** of the 42 unit
types this fork's schemas special-cased, and adds nine more -- and the rules
themselves are a faithful transcription, checked row by row on Internal's
length ladder. The schema numbering is identical, so the `UserSchema`
preference and `Units.setSchema(int)` keep meaning what they meant.

Kept on purpose: the `UnitSystem` enum, the whole `UnitsApi` public surface
(none of its 47 calling files changed), and `UnitsApi::getDescription`'s own
strings, which is why `Base/Translation.h` did not have to come too -- the
schema data's descriptions are left untranslated and unused.

ICU arrives with upstream's number formatting. It is free here in the sense
that matters: Qt already links it on both build stacks and pulls it into the
conda-forge package, so nothing new loads at run time. It is not free for a
future WASM tier, where `Base` would need ICU built for emscripten -- worth
knowing before that work starts.

* **A new unit is now one table row**, which is the point. The three
electromagnetic units added the same day had no display rule under the old
schemas and fell through to the composed SI string; upstream's table already
had them.

**Verify a change like this by measurement.** 10,260 renderings -- 57 unit
types x 10 schemas x 18 magnitudes -- dumped before and after and diffed.
1,199 differed and every one was accounted for: 462 a unit now scaled that was
not before, 439 the factor's last bits (upstream computes it from constants;
the string is identical), 182 upstream dropping the space before a degree or
inch mark, 116 number text -- of which 98 follow from psi being corrected from
6.894744825494 to the true 6.894757293168361, 2 are ICU rounding a tie to even
where Qt rounded away from zero, and the rest are the DMS and feet-inches
special functions. Nothing lost, one long-standing numeric error fixed.

## 6. What this buys beyond FEM

Stages 1-3 are worth doing even if the FEM port never happens. Every one of
them removes a permanent source of friction from *every* future upstream
merge: today a cherry-pick from upstream fails on Console capitalisation,
`Base::Color`, and the Selection path before it ever reaches the change you
wanted. Stage 2 in particular converts a class of merge conflicts into
nothing.

Stage 2a alone (twelve forwarders) makes any upstream file that logs
compile here unchanged.
