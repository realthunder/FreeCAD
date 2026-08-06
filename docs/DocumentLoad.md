# Document Load

This document is about opening a large `.FCStd`. The progressive STEP
import already brings a big assembly in with the view live and the
objects appearing as they arrive; a document load of the *same* model got
none of that. It blocked for half a minute and showed nothing until it
was done.

Related reading: `docs/IncrementalPublish.md` (the per-frame publish
cost, which the fill rate below runs into), `docs/SceneStreaming.md` §7
and §13 (the fidelity ladder and the desktop refine pool),
`docs/TShapeRenderCache.md`, `docs/RoadMap.md`.

## 1. Problem

Measured 2026-08-05 on the release stack (RTX 3060, VirtualGL + Xvfb),
opening `MiSTer_imported.FCStd` — 17800 objects, 48MB, saved from the
progressive STEP import of the 285MB MiSTer assembly. Three runs:
**32.3 / 32.7 / 33.3s**, peak RSS 4.7GB, and one **14.9s window in which
the event loop did not run at all**. The view painted 5 frames across the
whole open.

§4, §6, §7 and §8 are four separate fixes against this baseline: the
first moves the visual build off the blocking window (33s to 19.1s), the
second stops the file describing every view provider in full (19.1s to
13.0s), and the last two do the same for the objects themselves and stop
a one-colour list costing an archive entry (13.3s to 11.1s).

The first job was to find out what those 33 seconds are, because the
guess on record — that a load is publish-bound, and that finishing the
incremental-publish phases would therefore also be the load work — turned
out to be wrong.

## 2. Where the time goes, measured

| stage | s | % | logged as |
|---|---|---|---|
| `Document.xml` `<Objects>` — create each object | 3.04 | 9% | `restore <doc>: … create` |
| `Document.xml` `<ObjectData>` — restore properties | 2.94 | 9% | `… data` |
| rest of the XML pass | 1.20 | 4% | `xml` minus the two |
| `GuiDocument.xml` parse (**one** archive entry) | 5.39 | 17% | `readFiles … xml 1/5.4s` |
| BRep parse, 17058 `.brp` | 2.88 | 9% | `readFiles … brp` |
| 51174 near-empty `.bin` | 0.60 | 2% | `readFiles … bin` |
| archive stream advance | 1.08 | 3% | `readFiles … advance` |
| **visual build (`finishRestoring` → `updateVisual`)** | **10.92** | **34%** | `restore <doc> gui: … view providers` |
| showable/children refresh | 0.96 | 3% | `… refresh` |
| link/expression fixup and the rest | ~3.3 | 10% | `postprocess` minus the Gui pair |

Grouped: **XML 39%, visual build 34%, BRep 9%.** Two conclusions:

**The load is not publish-bound.** `RenderTiming` during the load reads
`translate=132ms backend=55ms` — noise against 32s. The publish path is
not what a load is waiting for.

**The visual build is tessellation, not publishing.**
`ViewProviderPartExt::finishRestoring()` ends in `updateVisual()`, which
meshes the shape. Splitting that stage further:
**7.6s of the 11.7s is `BRepMesh_IncrementalMesh`**, the remaining 4.1s
is building Coin nodes and bookkeeping.

⚠️ Coarsening does not help it. Forcing the whole process onto ladder
rung 2 (`FC_COARSE_TESSELLATION=2`) changed the stage by **0.006s** —
0.05%. The cost is per-face setup inside BRepMesh, not triangle count, so
the lever is not a cheaper mesh. It is not meshing 17058 shapes on the
main thread while the window is unusable.

## 3. What the archive is made of

Opening the 48MB file and counting, because two of the numbers above only
make sense with it:

- `Document.xml` **42.5MB** raw; `GuiDocument.xml` **87.7MB** — the view
  provider document is **twice** the size of the document itself, and it
  parses as a single archive entry.
- 17058 `.brp`, **237MB raw / 37.9MB zipped**, carrying no triangulation
  (`Triangulations 0`) — ASCII BRep, not the binary form.
- 51174 `.bin`, of which **50811 are exactly 8 bytes**: three empty color
  arrays per object (`DiffuseColor`, `LineColorArray`, `PointColorArray`).
  **75% of all archive entries carry nothing.**

## 4. Design: defer the visual build, hand it out in slices

The restore keeps doing what it does. What changes is that a shape asked
for its visual *during a restore* does not get it: `updateVisual()` parks
the ask on a queue and marks the shape visually touched. Once the load
lets go, the queue hands the builds back in slices bounded by
`ProgressiveLoadBudgetMS`, returning to the event loop between them, so
the window is up and painting while the model fills in.

Two details make it work rather than merely look like it works:

- **The bounding-box hook has to respect the parking.**
  `SoFCCoordinate3::getBoundingBox` builds a touched visual on demand, and
  the first repaint after a load traverses the whole scene — which built
  all 17058 visuals synchronously and gave the entire stall straight back.
  It now leaves parked visuals alone. Measured: without this the open was
  31.7s (the work simply moved to an untimed window), with it 19.1s.
- **The slice has to be worth a repaint.** Each slice is paid for with a
  redraw of a large scene, and at this size a redraw is expensive
  (`docs/IncrementalPublish.md` §1). At a 20ms budget the fill is paced by
  redraws, not by building, and had not drained after 45s. At 100ms —
  the default — it drains in 107 slices.

## 5. Result

| | before | after |
|---|---|---|
| open (bgfx, render cache 3) | 32.7s | **19.1s** |
| worst window with no event loop | 15.2s | **2.6s** |
| frames during load and settle | 19 | **89** |
| peak RSS | 4711MB | 4752MB |
| deferred fill after the open | — | 17058 visuals, 106 slices, 11.6s |

The rendered result is unchanged: `saveRenderDump(source='renderer')` of
the settled scene is **byte-identical** between the two paths, and
`getRenderStats()` agrees to the last decimal. The plain-Coin path (render
cache 0) was checked too — 17.8s open, correct render, queue drained.

⚠️ At the time of that comparison `saveImage` could not see what the
external backend drew — both paths captured a blank frame, which proves
nothing — so `saveRenderDump(source='renderer')` was used instead.
`saveImage` now routes through the same backend readback when a backend
is active, so either is a valid check — but they are not pixel-identical
to each other: an export leaves out the viewport chrome that a debug
capture keeps.

The total work is not reduced — it is moved off the blocking window.
Time-to-window is what changed, and that is the thing a user waits on.

## 6. Design: write a view provider once per class

> ⚠️ Partially superseded by §12: the comparison is now byte equality,
> not `isSame()`; eligibility is opt-in per property type; the format is
> chosen per document in the save dialog; and a file carrying blocks is
> rooted `<FCDocument>`, which old readers refuse loudly. The
> measurements and the `mustSave()` findings below still stand.

§3 says the view file is twice the size of the document and §2 says it
parses for 5.4s. The reason is that every view provider writes all 31 of
its properties whether or not it has moved any of them off what its
constructor produced. Counting `GuiDocument.xml` by property value:
**87% of it is properties holding the value the whole document holds**,
and of 540842 properties only about 5000 actually differ.

The first instinct — encode the same properties more cheaply, share
identical blocks — does not pay, and the measurement is what says so.
Splitting the pass into what the reader costs and what the property
costs (`PropertyContainer::restoreStats`, §11) gives **5.87s of which
4.33s is the values**. A standalone Xerces run over the same 87.7MB
scans it in **0.50s**. The text is not the problem. The properties have
to not be there.

**The shape.** `Gui::Document` builds one stand-in view provider per
class present in the document, writes its properties once as a
`<Defaults>` block inside `<ViewProviderData>`, and each view provider
then writes only what differs from it. `<ViewProviderData>` carries a
`Defaults` count so a reader that finds none never looks for the block,
which is what lets a file written either way be read by the same code.
The mechanism itself is generic —
`App::PropertyContainer::getSaveDefaults()` — so the App document can
adopt it later against `Document.xml`.

**Why the defaults are written and not implied.** View provider defaults
come from preferences: `ShapeColor` from `ViewParams`, `Deviation` from
`PartParams`, and so on. A file that simply left them out would take the
*opening* machine's preferences, so a document would change colour when
it moved between users. Recording them keeps the file self-describing.
It also costs nothing to apply: the reader restores the block into a
stand-in it builds the same way, compares against what that stand-in
held before, and pastes only the properties the record actually moved.
On a machine that agrees with the author — the normal case — that set is
empty and every view provider is defaulted for free.

**Two properties are always written**, via
`PropertyContainer::mustSave()`:

- `Visibility`. `finishRestoring()` falls back to the document object's
  own visibility when the file never mentioned it, and
  `startRestoring()` has already hidden the view provider — so leaving it
  out is not the same as writing the default.
- `DisplayMode`. Its enumeration is installed by `attach()`, and a
  stand-in built outside any document is never attached. ⚠️ Its
  `DisplayMode` therefore holds an *empty* enumeration, and pasting that
  over a real one loses the mode names for the whole document — the
  round-trip check caught exactly this, reading every `DisplayMode` back
  as `None`. A property on this list is excluded from the reader's delta
  as well as from the writer, for the same reason: the stand-in cannot
  speak for it.

**File format gating.** This is a format change, so it answers to the
mechanism that already exists for one. `App::Document` gained schema
version **6** — `getWritableSchemaVersions()` is now `{4, 5, 6}` — and
`buildDefaults` writes nothing when `writer.getSchemaVersion() < 6`. A
user who lowers the document's `SaveSchemaVersion` to keep it readable by
an older FreeCAD gets the pre-block file shape back, and the
`SaveViewProviderDefaults` preference cannot override that: the document's
declaration outranks the preference.

⚠️ **The Gui file's own `SchemaVersion` stays 1**, deliberately.
`Gui::Document::RestoreDocFile` gates its whole body on
`DocumentSchema == 1`, and so does every released build — so writing 2
would not mean "an older FreeCAD reads what it can", it would mean an
older FreeCAD reads *none* of the view file: no view providers, no
camera, no saved views. Left at 1, an older build walks past the
`<Defaults>` element it does not recognise and still restores every
property the file states per object — which is everything anyone actually
changed. Checked rather than assumed: `defaults_check.py` strips the
`Defaults` attribute from a written file, keeping the block, and confirms
that a reader blind to it loses **0** of the touched properties. What
should raise that number is a future change an old reader could
mis-parse rather than skip.

**A class needs three instances** before a block is written. A block is
one class's whole property set, so below that it costs more than the
instances can save — a nine-object document came out 4% *larger* before
this rule.

Result on the 17800 object document, re-saved and re-opened:

| | before | after |
|---|---|---|
| `GuiDocument.xml` | 87.7MB | **11.5MB** |
| archive entries | 68235 | **19255** |
| eight-byte colour entries | 50811 | **1831** |
| file on disk | 48MB | **39MB** |
| view provider pass | 540842 properties / 4.71s | **42233 / 0.61s** |
| archive stage (`files`) | 8.83s | **3.72s** |
| **open** | **18.9s** | **13.0s** |

Unchanged: worst stall 2.5s, 76 frames during load and settle, peak RSS
4748MB. **Every view provider property reads back identical across all
17800 objects, and the rendered frame differs in 0 of 480000 pixels.**

⚠️ This is a save-side change: it only helps documents saved after it,
and the baseline has to be re-saved before it can be re-measured. Turn it
off with `SaveViewProviderDefaults`.

## 7. Design: the same for `Document.xml`

> ⚠️ Partially superseded by §12, as §6 is.

§6 left `Document.xml` as the larger half of the XML — 42.5MB, an
`<ObjectData>` pass of 208816 properties for 2.97s — and the mechanism
it built was deliberately generic. The App document now uses it:
`App::Document::buildDefaults` builds one stand-in object per class,
`saveDefaults` writes its properties once as a `<Defaults>` block inside
`<ObjectData>`, and each object saves only its difference. Same schema
version (6), same gate shape, same `Defaults` count attribute a blind
reader walks past. Off with `SaveObjectDefaults`.

**⚠️ A stand-in object is not a view provider.** The Gui side got away
with building one outside any document; a `DocumentObject` cannot, and
every one of these was a crash or a silent no-op found by running the
check, not by reading:

- **Restoring into it notifies it.** `PropertyLinkList::Restore` →
  `setValues` → `Property::touch()` → the object's own `onChanged` →
  `LinkBaseExtension::update()`, which dereferences the document the
  stand-in has not got. `App::Document::isRestoringDefaults()` now says
  so, checked in `touch()` beside the two suppressions already there
  (`Transaction::isApplying`, `Document::isRemoving`). A stand-in stands
  for a class, not for anything in a document: there is nobody to notify.
- **Some properties cannot be in the block at all.** `PropertyPartShape`
  registers an archive entry of its own — a stand-in must never claim one
  — and its `Restore` dereferences the owner's document. That is what
  `mustSave()` is for, and `PropertyContainer::SaveDefaults()` keeps what
  it names out of the block on both sides. ⚠️ The obvious cheaper test,
  "a property that cannot say it is the same as itself", does **not**
  catch it: `PropertyComplexGeoData::isSame` short-circuits on self.
- ⚠️ **`setStatus(PropNoPersist)` is silently a no-op.**
  `Property::setStatusValue` masks that bit out along with `PropDynamic`,
  `PropReadOnly`, `PropTransient`, `PropOutput` and `PropHidden` — they
  are intrinsic to the declared type. A first attempt to keep properties
  out of the block that way changed nothing, and the crash repeated
  byte-identically, which reads exactly like a build that did not happen.

**Two comparisons were wrong, and both cost more than the feature.**

- ⭐⭐ **`PropertyString::isSame` compared `const char*` pointers**, not
  strings, because `getValue()` hands back a buffer address. Every pair
  of equal strings answered "different". It is the only property here
  that returns a pointer from `getValue()`, which is why it was the only
  one that could not recognise its own value. Pre-existing, and wider
  than this feature: `Property::hasSetValue()` uses `isSame` to skip
  no-op changes.
- ⭐ **The `Touched` bit blocked 255 of 411 elisions.** A stand-in is
  freshly built and a settled object has been purged, so the bit differed
  on nearly everything. It is also not preserved by a file:
  `PropertyContainer::Restore` applies the recorded status and *then*
  calls the property's own `Restore`, which touches it again. Comparing
  it refused elisions for a bit that does not survive either way, so the
  comparison now masks it.
- **The reader must compare live against live.** It first compared the
  restored block against `Property::Copy()` of the stand-in's values, and
  a detached copy has no container — so link properties compared by a
  scope they no longer knew and enumerations by a list they no longer
  had, and half of every object's properties came back "differs". Two
  stand-ins, one restored into and one left alone, is the same comparison
  the writer makes.

⭐ **`PropertyExpressionEngine` had to learn to compare.**
`PropertyExpressionContainer::isSame` declines unconditionally, so the
engine — empty on all but a handful of objects, and the second largest
elidable thing in the file — could never be left out. It now answers the
one question it can answer cheaply: two engines holding no expression are
the same engine. Anything else still declines.

Measured on the 17800 object document, re-saved and re-opened:

| | before | after |
|---|---|---|
| `Document.xml` | 42.5MB | **17.2MB** |
| `<ObjectData>` pass | 208816 properties / 3.00s | **57815 / 1.06s** |
| of which values | 2.33s | **0.80s** |
| `Document.xml` pass total | 6.40s | **4.46s** |
| restore total | 10.48s | **8.38s** |
| **open** | **13.3s** | **11.1s** |

**Every property of all 17800 objects reads back identical, and the
rendered frame differs in 0 of 480000 pixels.** Of the 194152 properties
offered, 153395 were left out and 40757 written because their value
really differs — none for status, none unknown to the block.

⚠️ **A document carries its own `SaveSchemaVersion` forward.** The
reference document came from an import made before schema 6 existed, so
it declares 5, and a writer honouring that declaration correctly writes
no block at all — App side *or* Gui side. Nothing looks wrong when that
happens: it saves, it reloads, it compares equal, and it measures
nothing. `resave_obj_probe.py` states the version it wants.

## 8. Design: a list too small to earn an archive entry

§6 left 1831 archive entries of **eight bytes** — a `DiffuseColor` of one
colour, on every object whose colour differs from its class default. The
defaults mechanism cannot fix those: the value genuinely differs, so it
has to be written. What is wrong is not that it is written but that
writing it costs a whole archive member: two zip headers, a name and a
directory record, around 190 bytes before any content, plus one more
thing for the reader to open and drain.

`PropertyLists::Save` now writes a list inline when its values fit in
`DocumentParams::InlineListSize` bytes (64 by default, on
`getMemSize()` rather than element count because the elements are of
wildly different sizes). The form is `count="N"`, which is what the
reader has always taken for a list that could not be streamed — so
nothing on the read side changes and no file written this way needs a
newer FreeCAD to read it. On the reference document that is **1831 tiny
entries → 0**, archive members 19255 → 17424.

The invariant it relies on is one the code already required: a list class
that returns true from `canSaveStream()` implements `saveXML()` too,
because `ForceXML` has always been able to demand it.

## 9. What this does not fix

- **The remaining 11s still blocks.** Nothing can appear before the
  objects exist, and the create pass, the property pass and the archive
  walk still all run to completion before the window is usable. The
  create pass is now the largest XML item at 3.13s and is untouched by
  either of these.
- **The reader's own overhead is now visible.** With the properties gone
  the view provider pass is 0.61s, but `Base::XMLReader` still rebuilds a
  `std::map<std::string,std::string>` of transcoded attributes per
  element. Measured standalone on the old 87.7MB file that costs 0.50s on
  top of a 0.50s scan; a vector of reused strings brought it to 0.10s.
  Unlike §6 this would help documents **already saved**.
- **ASCII BRep is untouched**: 17058 `.brp`, 237MB raw, 2.79s — now the
  largest single item in the archive stage. ⭐ `Document::PreferBinary`
  already switches this, so the first move is a re-save experiment rather
  than code.
- **Old documents keep the old cost.** §6, §7 and §8 are all save-side
  changes. A file saved before them still parses 540842 view provider
  properties and 208816 object properties, and still spends an archive
  entry on every one-colour list. ⚠️ And a document that declares an
  older `SaveSchemaVersion` keeps producing the old shape however new the
  build is — see the end of §7.
- **The fill rate is publish-bound**, even though the load is not: each
  slice pays for a redraw, so the incremental-publish work
  (`docs/IncrementalPublish.md`) is what would make the model fill in
  faster once the window is up.

## 10. Non-goals and risks

- **Parallel restore is not attempted here.** The archive is read through
  a forward-only `ZipInputStream`, and switching to
  `zipios::ZipFile`'s central-directory random access is what would let a
  shape be restored when its object appears rather than in archive order.
  Worth doing for that reason — but not for speed: the forward-only walk
  costs **1.08s**, 3% of the load.
- **A bounding box asked for during the fill is answered from what is
  built so far.** The queue drains in seconds and the scene self-heals,
  but a script that opens a document and immediately measures geometry
  through the scene graph can see a partially built answer. Turning
  `ProgressiveLoad` off restores the old behavior exactly.
- **No stand-ins.** A parked shape contributes nothing until its slice,
  rather than showing a bounding box. The stand-in machinery exists
  (`buildCoarseStandIn`, gated on `LiveImport`) but is aimed at single
  oversized parts — `CoarseDeferFaces` is 1000 faces — and this load's
  cost is thousands of small ones.

## 11. Instrumentation

All of it is log-level gated (`App`, `Base`, `Gui`, `Part` at `Log`), not
build flags, and all of it stays:

- `App::Document::restore` — the stage line. ⚠️ `after` reads 0 when
  *opening* a document: `Application::openDocuments` defers
  `afterRestore()` and times it separately as `postprocess`.
- `Application::openDocuments` — `external links`, `dependency sort`,
  `reload close`, `activate`, so the open's own total accounts for itself.
- `Base::ZipReader::readFiles` — entry count, stream `advance`, and parse
  time per file extension.
- `Gui::Document` — per-object `finishRestoring` against scene attach and
  the refresh, plus `Gui::ViewProvider::VisualBuildTime` /
  `VisualMeshTime`, the accumulators that separate a bulk fill's visual
  building from its meshing.
- `PartGui` — one line per drain of the deferred queue.
- `App::PropertyContainer::restoreStats` — properties restored, the time
  in them, and of that the time inside `Property::Restore()`. The two
  halves answer to different fixes and choosing between them needs the
  split; §6 exists because of this number. Read as a delta by
  `Document::readObjects` and by `Gui::Document`, which reports the view
  provider pass as its own line.
- `App::PropertyContainer::savedDefaults` — properties a save left out.
  Reported per document write, because a shared default block that
  quietly stops matching writes the whole file again and otherwise looks
  like nothing happened. Beside it, `savedDefaultsValue`,
  `savedDefaultsStatus` and `savedDefaultsUnknown` say why the rest were
  written: a real difference in value, a difference only in status, or a
  property the block never heard of. ⭐ That split is what found the
  `Touched` bit refusing 255 of 411 elisions — "it wrote everything
  again" on its own would not have said which test said no.

Harnesses, all under `~/works/sw/models/harnesses/` with `run_gpu.sh`:
`load_probe.py` (open, stall, frames, RSS), `defaults_check.py` (a small
document round-tripped with the option on and off), `resave_probe.py`
(re-save the big one, then compare every view provider property and the
rendered frame against the original).

⚠️ Traps these ran into, all of which produce a *passing-looking* run:

- **`perf` is unusable on this box** (the wrapper wants a kernel-tools
  package that needs root); stage timers or ablation instead.
- **Parameters persist to `user.cfg`.** A check whose last case turned
  `SaveViewProviderDefaults` off left it off for the next measurement,
  which then re-saved 540842 properties and reported success. A harness
  should *set* what it is testing, and put back what it changed.
- **A capture taken before the deferred fill drains** photographs how far
  the fill happened to get. Settle first — and compare decoded pixels,
  not file bytes, because two encodings of one picture need not match.

## 12. Redesign: the format holds its own guarantees

A review of §6–§8 as first shipped found the mechanism sound only
conditionally — correct exactly as far as every `isSame()` is exactly as
strict as `Save()`, silently wrong in every FreeCAD that predates it,
and elidable-by-default for every property type anyone would ever add.
Three requirements were then fixed, and the design reworked to hold each
one *by construction* rather than by audit:

1. **A future change of a class default must not affect restore.**
2. **A file in the new format must fail loudly in every older FreeCAD,
   upstream included.**
3. **Every save configuration (split, ForceXML, export, …) must stay
   correct, and the format is opt-in — never a default.**

**The equivalence is the file (req 1).** `App::SharedDefaults` records,
per class, what each eligible property of a fresh stand-in serializes to
at canonical settings (the file's schema and version, XML forced, no
indentation). The block written into the file is those recorded bytes
verbatim, and a property is elided only when its own serialization is
byte-identical to them, status agreeing mod `Touched`. The readers
decide the same way: block restored into one stand-in, a second built
fresh, both sides re-serialized through
`SharedDefaults::serializeForCompare` and byte-compared — re-serializing
the proto (rather than trusting the file's literal text) cancels the
formatting a parse-and-save round trip applies. What differs is pasted
whole, status first and value second as `Restore` orders it, each paste
behind its own try/catch, every property re-fetched by name after the
block restore. No `isSame()` is load-bearing anywhere; restore fidelity
reduces to XML parse fidelity, the same trust the per-object path always
had. A changed future default is exactly a byte difference, and byte
differences get pasted.

**Elision is opt-in per type (req 1 and the audit burden).**
`Property::canShareDefault()` defaults to **false** — every property is
effectively must-save until its type opts in. The whitelist is the
cheap, deterministic value types (scalars, strings, enumerations,
colours, materials, small lists, vector/placement, the link family, the
expression engine); `PropertyPersistentObject` and
`PropertyXLinkContainer` opt back out of branches that would have
inherited an opt-in. Shapes, Python objects, included files and UUIDs
stay out by doing nothing. `mustSave()` remains the container-level veto
for absence-sensitive properties (`Visibility`, `DisplayMode`), and
`SharedDefaults::eligible()` is the one test all three sides share.

**The root element is the compatibility statement (req 2).** A save that
resolves to schema 6 is rooted `<FCDocument>`; everything else keeps
`<Document>`. No released reader checks a schema number, but every one
of them — this fork's and upstream's — scans for `<Document>` first,
reaches the end of the stream, and throws. Verified both ways: a test
linked against this tree's `libFreeCADBase` (identical `Reader.cpp` to
the released builds) throws `End of document reached` at the first
`readElement`, and upstream's `Reader.cpp` (`readElement`, main branch)
throws `XMLParseException("End of document reached")` from the same
loop. The old "blind reader walks past the block" argument — and the
harness case that checked it — is retired: an old reader never gets that
far. The failure is loud but *cryptic* in already-shipped binaries;
that is the best reachable retroactively, and it is the accepted
trade. The name is deliberately version-less: `SchemaVersion` keeps
carrying versions, the root name only says "not for readers that
predate it".

**Schema is an outcome, chosen per document (req 3).** The
`SaveSchemaVersion` property is the user's cap, **default 5** — a fresh
document is readable everywhere until someone decides otherwise. The
one place to decide is the save dialog: a format row (standard/compact)
preselected from the document's own cap (or, for a never-saved document,
the `PreferCompactFormat` parameter holding the last choice made there),
with a red, bold, title-sized warning that stays on screen for as long
as compact is selected — deselected, never dismissed. Plain Save keeps
whatever the document decided. Each save then *resolves* the cap:

| configuration | outcome |
|---|---|
| cap 5 (default) | schema 5, `<Document>`, no blocks |
| cap 6, normal save | schema 6, `<FCDocument>`, App + Gui blocks |
| cap 6, split XML | schema 5 — per-object files have no block to share |
| `exportObjects` (clipboard/merge) | capped at 5 always — fragments travel |
| `dumpContent` (no configured writer) | `Save()` resolves the document's own answer onto the writer |
| ForceXML / InlineListSize / PreferBinary | orthogonal; comparison buffers are always forced-XML, a form mismatch merely forfeits elision |
| `SaveObjectDefaults` / `SaveViewProviderDefaults` prefs | **removed** — no machine preference outranks what a document promised |

**Checked** (2026-08-06, conda-debug): `objdefaults_check.py` PASS —
block + `FCDocument` at 6, neither at 5, a fresh document
indistinguishable from 5, zero round-trip diffs beyond the
`_LinkVersion` baseline, −34.5% `Document.xml` on the 29-object toy;
`defaults_check.py` PASS — −52.6% `GuiDocument.xml` at 6; `FreeCADCmd
-t Document` back at its exact known baseline after `dumpContent`
exposed the one regression (a writer nothing configured defaulting to
schema 0 — fixed in `Document::Save`).
