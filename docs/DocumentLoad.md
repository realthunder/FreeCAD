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
version **5** -- `getWritableSchemaVersions()` is now `{4, 5}` -- and
`buildDefaults` writes nothing when `writer.getSchemaVersion() < 5`. A
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
reference document came from an import made before the compact format
existed, so it declares the older version, and a writer honouring that
declaration correctly writes
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
  either of these — §13 is what finally moves it.
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

- **Parallel restore is not attempted here.** It is however no longer
  blocked on the archive: `Base::ZipFileReader` (the `ArchiveRandomAccess`
  parameter, default on) indexes the zip central directory once and opens
  every entry as an independent stream, so registered files are served in
  registration order whatever their archive order, an entry can be
  reopened after the walk, and entries could be read concurrently — each
  open owns its own file handle. The forward-only `ZipReader` remains as
  the fallback. This was never about speed (the forward-only walk cost
  **1.08s**, 3% of the load, and the walk itself is unchanged); it is the
  enabler for serving a shape when its object needs it rather than when
  the archive gets around to it.
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

> **Renumbered, 2026-08-16.** The compact format was built as schema 6
> on top of a schema 5 that meant "shared included-file blobs". Neither
> was ever released, and schema 5 was never actually compatible: an
> older reader takes its `hash=` attributes for no attribute it knows
> and drops the file's embedded content without a word (verified
> against upstream `main`'s `PropertyFileIncluded::Restore`, which has
> only `file=` and `data=` branches). Two fork-only formats, one of
> them lying about it, are one format. So 6 was folded into 5:
> **4 is upstream's format, 5 is this fork's** -- blobs, default blocks
> and the shared shape store together, under `<FCDocument>`. Read every
> "schema 6" below as 5, and every "schema 5" as 4, except where a
> measurement names the pre-merge blob-only shape, which the new
> numbering has no name for.

**The root element is the compatibility statement (req 2).** A save that
resolves to schema 5 is rooted `<FCDocument>`; everything else keeps
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
`SaveSchemaVersion` property is the user's cap. At 4 nothing of this
fork's format is written, included files included, so a document capped
there is readable everywhere. The one place to decide is the save
dialog: a format row (standard/compact) preselected from the document's
own cap, which for a never-saved document the `PreferCompactFormat`
parameter (the last choice made there) can hold back but not raise.

**The default is 5 -- this fork's format (2026-08-20, user ruling).**
A document created here is written the way this build writes documents,
and what that costs is *stated* rather than avoided: `Gui::Document`
warns explicitly, once, with a "do not warn again" checkbox. Two things
keep that from becoming a silent conversion:

- **A restored document keeps the format its file was written in.**
  `Document::Restore` sets the cap from the file's own `SchemaVersion`
  *before* the property block is read, so a file that records
  `SaveSchemaVersion` still overrides it, and one written before the
  property existed -- or by upstream FreeCAD -- comes back at 4 instead
  of inheriting today's default. Opening an old document and pressing
  Ctrl+S does not change its format.
- **A save that would drop content asks first.** See
  `confirmSchemaUpgrade()` below.

`exportObjects` is still capped at 4 whatever the document says: a
fragment travels.

**Two prompts carry what the format costs**, both in `Gui::Document`,
both after the file name is in:

- `confirmCompactFormat()` -- fires when a save resolves to compact and
  says outright that no other FreeCAD will open the file, with
  *Save compact* / *Use standard format* / *Cancel* and a
  **Do not warn again** checkbox (parameter `WarnCompactFormat`,
  default true). This replaced a red, bold, title-sized label inside the
  file dialog that appeared and disappeared with the radio button: a
  heading that flickers is not a warning, and with compact now
  preselected nobody would ever have toggled it into view.
- `confirmSchemaUpgrade()` -- fires when a save resolves to **standard**
  and the document holds content only the store can carry, offering
  *Use compact format* / *Save anyway* / *Cancel*. It runs on the plain
  Save path too, which is the case that matters: a document saved once
  at 4 and given a texture afterwards never opens the dialog again.
  "Save anyway" is remembered for that document for the session.

The scan behind the second one asks the properties, not the appearance:
`App::BlobReferrerProperty::blobContentNeedsStore()` is false by default
-- `PropertyFileIncluded` writes its own copy below 5, a shape property
writes the shape the old way and forfeits sharing rather than data --
and `PropertyMaterialList` overrides it with `hasTexture()`, being the
one referrer whose content has no schema 4 spelling. Plain Save keeps
whatever the document decided otherwise. Each save then *resolves* the
cap:

| configuration | outcome |
|---|---|
| cap 4 (default) | schema 4, `<Document>`, no blocks, no blobs, no store |
| cap 5, normal save | schema 5, `<FCDocument>`, App + Gui blocks, blobs, store |
| cap 5, split XML | schema 5 -- blobs and store, but no block: a per-object file must not depend on a document-wide record it did not ask for (a third object of a class would rewrite the other two, which is the diff a directory layout exists to not have) |
| `exportObjects` (clipboard/merge) | capped at 4 always -- fragments travel |
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

## 13. Design: defer the view providers themselves

§4 moved the visual build off the blocking window; the create pass and
the Gui XML pass stayed inside it. The per-stage split (§11's
`slotNewObject` line) put the create pass at 2.9s, of which `addObject`
2.78s — and a console load does the same file's `addObject` in 0.26s.
The difference is the view provider each object gets built inline:
instantiation 1.0s, attach (with its Python binding) 0.86s, the
`updateView` property sweep 0.42s — plus the Gui XML property pass
(0.69s) and the per-object `finishRestoring` (0.25s) later in the load.
None of it can show anything before the load finishes, so all of it now
leaves the window.

Under `ProgressiveLoad`, a restore builds no view providers at all:

- `slotNewObject` during a restore records nothing and returns. The
  data pass loses its per-property Gui observers with it (0.87s→0.52s).
- `RestoreDocFile` still parses `GuiDocument.xml` inside the load —
  tree expansion, cameras, saved views, split files and archive order
  all keep their exact old shape — but each `<ViewProvider>` element is
  *captured* rather than restored: `Base::XMLReader::captureElement()`
  re-serializes the subtree, escaped as the writer escaped it, into one
  parked buffer. A captured element that references an archive entry
  (` file="` — exact for attributes, since the capture re-escapes
  quotes) cannot be parked: the forward walk consumes entries in
  registration order, so that view provider restores eagerly and hands
  its file requests to the archive reader.
- After the load lets go, a drain walks one progressive reader over the
  parked buffer in `ProgressiveLoadBudgetMS` slices, **three phases,
  because a restore's correctness lives in its macro-order**: phase one
  creates every view provider; phase two replays every record through
  the ordinary `readObject()`; phase three runs the held-back
  `updateView` sweep and `finishRestoring()` over the whole set, and
  defaults whatever the file never described (an `App::Part`'s origin
  built in `afterRestore`, above all). Then the showable/children
  refresh runs once. ⚠️ Both separations are load-bearing: finishing a
  link element inside phase two settles it on the record's stale
  visibility (the link web only corrects it once whole — measured as
  12499 objects hidden instead of 63, and a resave then writes the
  lie); and sweeping before a record lets the visual build once and be
  recolored.
- **Phases one and three walk a snapshot, not the object array**
  (`Gui::DrainCursor`). Slices return to the event loop, so the
  document is live between them and the user can delete or create
  objects with half the view providers still parked. An index into
  `getObjects()` does not survive that: a deletion shifts every later
  element down one and the walk steps over an object, which in phase
  three means a view provider left with `Gui::isRestoring` set and no
  mode switch — loaded, and refusing to show. The cursor records the
  object *names* once, when the drain starts, and resolves each on
  arrival: a deleted object is skipped, one created afterwards is
  deliberately absent (it got its view provider from `slotNewObject()`
  at creation and must not be restored twice). Same by-name rule as
  everything else the load parks.
- Each slice presents itself as a restore: the document's `Restoring`
  status plus `App::Document::RestoringScopeGuard`, a scope that
  answers `isAnyRestoring()` with true, so attach keeps its hands off
  the restored visibility and everything keyed on the global flag
  treats the replay as the record-reading it is. Saves, exports and
  imports flush the drain synchronously; a closing document drops it.
  A record that cannot be read gives up on the record only: the parked
  buffer goes and phase three still runs over every object, which is
  what "falls back to defaults" has to mean — a view provider abandoned
  mid-drain is one that never shows.
- **A slice may not modify the document, and now says so in one
  place.** Phase three drops the view provider's restore status before
  sweeping its properties, because with the guards on the handlers
  render nothing at all — so they run here as they never ran eagerly:
  past the purge. Eagerly `afterRestore()` purged each object right
  before announcing it finished, and a recompute purges its own, so a
  handler that writes back while it renders — a page template noting
  the size of the SVG it just parsed — cost the eager path nothing.
  The drain replays that same handler where no purge follows, and the
  write sticks: the document needs a recompute because it was opened.
  Marking the objects restoring again is not the alternative (that is
  the state in which they render nothing); the scope says the other
  half instead, *render, but do not write*. Every slice runs inside
  `App::Document::RestoreDrainGuard` — status bit `RestoreDrain`,
  honored by both touch paths of `DocumentObject` — and a change made
  in it does not touch its object but does record its name. Chasing
  these one workbench at a time does not converge, so the drain's
  completion line names what tried: `N document changes suppressed
  while replaying the view providers (Template.Width, ...)`. Writing
  on a render is still a bug of its own — the three found this way are
  below — and that line is what points at the next one without a
  debugger.
- **The rest of the Gui stays as quiet as the eager window kept it.**
  The tree does not connect its change signals or build items while the
  drain runs (`TreeWidget::onUpdateStatus` treats a draining document
  as still restoring — otherwise `setupTreeRank` renumbers every object
  the drain announces, and each item takes every per-property signal
  the replay emits: an icon rebuild per `InvalidShape` alone cost 3.4ms
  × 18142). The §4 visual queue waits for the drain too, so each
  visual builds exactly once, from restored properties, on nodes the
  sweep never has to walk populated.

**What the drain flushed out** (each found by a self-selecting
reporter that stays in the code — the slow-element and slow-property
lines, then a slow-updateData line):

- `Gui::ColorUpdater` pays `getInListEx` plus a whole-document
  dependency sort per registered color change (~25ms × 672 here). The
  eager path was exempt only by accident: a freshly restored object is
  still `Touched`, which `addObject` skips — the drain runs after the
  touch purge. It now skips during any restore on purpose.
- `ViewProviderLink::setOverrideMode` dereferenced the linked object's
  view provider without a null check — never survivable before only
  because links weren't restored at create-pass time.
- `TouchOnColorChange` objects were touched by the replay with no
  `afterRestore` purge left to clean it, so a document opened already
  modified and close prompted to save. The touch now skips during any
  restore (the eager path's touch was purged — net effect identical).

The last three were found the same way, once a 606-object TechDraw
document (`scanner.FCStd`) was drained and its touched set diffed
against a serial load's — 214 objects against 204, the extra ten being
eight `DrawSVGTemplate`s and two `App::Link`s, and nothing else:

- `DrawSVGTemplate::processTemplate()` wrote `Width`/`Height`/
  `Orientation` back on every render, from the SVG it had just parsed.
  A cache refresh, not an edit, so it renders inside an
  `ObjectStatus::NoTouch` locker now — which also fixes the eager-path
  bug that merely *opening* a page marked the document modified. The
  `onChanged(&Template)` caller keeps its touch: there the user really
  did pick a different template file.
- `App::Link::_LinkTouched` was declared `Prop_Hidden|Prop_NoPersist`.
  Its own comment says the value is never read, only its change, as a
  view provider notification — but without `Prop_Output` every
  notification also touched the link.
- The `signalChanged` lambda in `Link::monitorOnChangeCopyObjects()`
  had no guards at all, while the copy-on-change *source* watcher in
  `update()` doing the same job guards on `isAnyRestoring()`,
  `NoTouch` and `Prop_Output`. It now has the same three.

**Result** (MiSTer_objdefaults, RTX 3060, cache 3): open **10.6s →
5.0s**, first paint 0.1–0.3s after open, worst stall ≤1.1s (was 2.5s
at 10.6s open). The drain completes in 47 slices / 4.7s — instantiate
0.97, attach 1.72, replay 0.77, finish 0.70 — and the §4 visual fill
follows (~20s). Settled RSS unchanged. Round-trip: 0 property diffs
on both gates, and a per-object census of App visibility, view
provider visibility, mode switch and display mode across all 18142
objects is identical between the source and a resaved reload — as is
the rendered frame, to the pixel, under plain Coin (cache 0) and
under the eager path.

✅ **Resolved (was: "renderer-side two-document miss")**: the second
document opened under the bgfx backend deterministically missed ~115
parts (39039 px, stable to the pixel). The renderer was innocent, and
so was "two documents" as such: `deferVisualForLoad()` gated only on
`App::Document::Restoring`, which the drain clears **between its
slices** — while the visual queue itself refuses to build in that
same window (`runDeferredVisualSlice` also checks
`isRestoringViewProviders()`). A direct `updateVisual` landing in a
gap (an event on the camera-fit path) built the visual into a
half-staged subtree, where the drain's remaining staging left it
built Coin-side but never drawn — the very interleaving §4 serializes
against, escaping through the one unguarded entry. It surfaced only on
the *second* document because a resave permutes the GuiDocument.xml
record order (writer maps iterate deterministically per input — hence
the pixel-identical failures), and only some orders line the drain
gaps up with the camera events. The fix makes `deferVisualForLoad()`
apply the queue's own gate, restoring "visuals after drain"
unconditionally; the resave gate then round-trips 0 px under cache 3
progressive, eager, and Coin alike.

**What it trades**: between the open returning and the drain
finishing, `getViewProvider()` answers null and `obj.ViewObject` is
not yet bound — the same window live STEP import already has. A script
that opens a document and immediately drives view providers wants
`ProgressiveLoad` off, which restores the old behavior exactly.

## 14. Design: read the archive on demand, and shapes only when asked

Two pieces, one enabling the other.

**`Base::ZipFileReader` (the `ArchiveRandomAccess` parameter, default
on).** The restore used to read the archive through one forward-only
`ZipInputStream`, which required the entries to sit in registration
order and inflated past every entry nobody read. The new reader parses
the zip central directory once, then opens every entry as its own
positioned stream: registered files are served in registration order
whatever their archive order, an entry can be reopened *after* the
walk, and entries could be read concurrently — each open owns its own
file handle. The included-file handler gained a name-only filter
(`Base::XMLReader::ArchiveFilter`) so this walk does not pay an open
for entries the handler would refuse. The forward-only reader remains
the fallback for anything the indexer cannot digest. Not a speed
change (§10) — an ordering change.

**Deferred shape restore (the `DeferShapeLoad` parameter, default on
since the real-GPU gate passed).** With random access in hand, the walk no longer *has*
to read the shapes before the document opens. A property that opts in
(`App::Property::canDeferRestore()`, so far only `PropertyPartShape`)
has its entry **parked**: recorded by name in the document
(`{object, property} → entry`, names not pointers, so a deleted
object invalidates its entry instead of dangling), with the archive
index kept alive behind it. Every accessor that touches the shape —
`getValue`, `getShape`, `getComplexData`, bounding box, transforms,
`Copy`, the save family — first runs `ensureRestored()`, which serves
the parked entry through `Document::restoreDeferredFile()`: reopen the
entry, `RestoreDocFile()` under an `ObjectStatus::Restore` guard, and
purge the touch it would otherwise leave. `setValue` **cancels** a
parked entry instead — the archived value lost the race and must never
overwrite the new one, not even from `flushDeferredFiles()`.

In the GUI the serve is **phase zero of the deferred view provider
drain** (§13): `Document::serveDeferredFiles()` runs in budgeted
slices before any view provider record is applied, so everything
after it — the records (which read shapes for color application), the
visual fill — runs with every shape present, byte-for-byte the eager
load's dynamics. The serve owns a `Loading shapes...` sequence that
lives across its slices, so the status bar carries shapes-served over
the backlog, with each shape's own read indicator nested beneath it in
the progress popup (see
[ProgressiveLoading.md](ProgressiveLoading.md) §5). The per-access fault-in stays as the backstop, and is
the *only* mechanism in a console process, which therefore never pays
for shapes nobody asks for; a save asks for all of them
(`beforeSave`/`Save`/`SaveDocFile` fault in), which flushes the lot
while the original archive is still on disk.

Three findings from gating this on the real GPU, kept for the next
person who touches the order:

- **Serving lazily from inside the drain works but costs minutes**:
  each record application faulted its shape in one at a time, and a
  per-shape `importBrep` *with the default progress indicator* cost
  ~2.7ms outside a running sequencer (start/stop + event pumping)
  against 0.16ms for the parse itself. The cost class was later fixed
  at the source — per-item sequences no longer restart the top
  indicator, and the GUI teardown is debounced behind a grace period
  (ProgressiveLoading.md §5) — after which the restore path got its
  indicator back with the serve timings unchanged (console open
  0.86s / save-all-pending 12.0s, GUI window 2.3s, re-gated).
- **A per-serve change notification is a trap**: replaying
  `signalChangedObject` per served shape fans out to per-object GUI
  listeners and multiplies into minutes across 17k objects. Serving
  before the consumers exist (phase zero) needs no notification at
  all.
- **The converged-scene rule cannot see a silent phase**: while
  shapes serve, nothing paints, so two consecutive frames agree and a
  probe declares convergence with the fill still queued behind the
  serve. Gate on the drain's own completion log line
  (`progressive load: N visuals ...`), not on frame agreement alone.

Measured on the MiSTer reference (RTX 3060, bgfx, cache 3): window at
**2.0–2.3s** against 5.2s, all shapes served by ~4.7s (2.6s of serve
work — equal to the eager walk's read), visual fill unchanged
(17058 visuals / ~11.3s), converged frame **pixel-identical (0 px)**
to the eager load, peak RSS equal at completion and ~1.1GB lower
during the load window.

What it trades:

- **The file must not be rewritten externally while entries are
  parked.** The index holds offsets, not content; our own save is safe
  because `saveToFile()` calls `flushDeferredFiles()` before it writes
  anything, but another process rewriting the open file breaks pending
  serves — they log and leave the shape empty. The old reader read
  everything up front and did not care.

  The flush is explicit, not a side effect of the writing: the property
  accessors fault in only what the save actually visits, and
  `PropertyContainer::beforeSave()` drops `Transient` and
  `PropNoPersist` properties before calling `beforeSave()` on them, so
  such a property would keep its parked entry across the rename that
  moves the original archive to a backup — and then read a stale offset
  out of the file that took its name. It also closes the index's own
  handle on the archive, which on Windows can fail that rename outright.
- **A serve that cannot open its entry gives up quietly.** It runs
  inside an event-loop slice and inside arbitrary const accessors, so a
  truncated, replaced or deleted archive is logged and the value left
  empty, never thrown from a timer callback.
- `Feature::onDocumentRestored()` checks shape content **when the
  shape arrives** (`restoreShapeContents()` from `ensureRestored()`)
  rather than at restore time.
- `getMemSize()` deliberately does not fault in — memory accounting is
  not a use.

## 15. Design: the GUI stays usable while the document fills

sec 4, sec 13 and sec 14 all move work out of the blocking open and into slices
the event loop drives. That buys nothing on its own if the user cannot
act during those slices, and they could not: the mouse was dead for the
whole load. Two separate mechanisms held it, and neither was aimed at a
load.

### 15.1 The input claim outlives the phase that made it

`ProgressBar::eventFilter` is installed on the application and swallows
press (with a beep), release, move, double click, enter, leave, native
gesture and context menu while a sequence runs. Its only exemption is
`Gui::LiveViewInteraction`, which until now was constructed solely by
`Gui.setLiveImport()` -- an *import* feature. A load never engaged it.

The claim also outlives its phase. Only a **blocking** sequence installs
the filter (`d->filterHeld`), and the teardown that releases it is
deferred behind a grace timer that any following sequence re-arms. A
load's blocking open is followed immediately by `KeepInteractive`
sequences -- the view-provider drain of sec 13, then "Building visuals..."
of sec 4 -- so a filter taken for a phase of seconds is held for the entire
load. `KeepInteractive` correctly declines to install one but cannot
release one already held.

WARNING -- The wheel worked throughout, which reads like a deliberate exemption
and is not one: `QEvent::Wheel` appears in neither the swallow list nor
the press case and falls through the `default:` arm. Nothing exempted
navigation.

**`Gui::Application::refreshLiveLoad()`** engages the same pair an import
does -- `LiveViewInteraction` for the pointer, and
`App::Document::LiveImport` to protect the half-filled document. It
recomputes from live state rather than counting up and down, so a call
too many is free and a load that dies without finishing is repaired by
the next one; a stuck claim would otherwise refuse every altering command
for the rest of the session. It is called at the load's start, its
finish, the end of the view-provider drain, and from
`setBuildingVisuals()`, which is the only notice this side gets that the
visual drain is over.

WARNING -- The starting document is passed **explicitly**: App emits
`signalStartRestoreDocument` one line *before* `setStatus(Restoring,
true)`, so at that call site the status bits do not yet say what is true.
A document already live for an import is left alone -- not ours to set,
so not ours to clear.

### 15.2 A command is judged by what it changes, not what it declares

With the pointer through, the document needs protecting from what the
pointer can reach. The first gate read `Command::eType`, and that is not
evidence: it defaults to `AlterDoc|Alter3DView|AlterSelection`, so most
commands claim to alter the document whether or not they touch it, and
**138 commands overwrite it with a bare `ForEdit`** and escaped the gate
entirely -- `Std_Delete` among them, which is how an object came to be
deleted in the middle of an import. The gate was simultaneously too
strict, refusing commands that only look, and too loose, admitting one
that deletes.

So the question is asked where the answer is certain: **at the write.**
`App::Document::UserEditGuard` is alive while a command runs, and
`checkUserEdit()` throws if a document carrying `LiveImport` is changed,
naming the object and property -- which the command itself could not have
done. It is checked at the four places a change actually happens:

| site | covers |
|---|---|
| `DocumentObject::touch()` | direct touches |
| the property write in `DocumentObject::onChanged()` | every property assignment |
| `Document::addObject()` (both overloads) | creation |
| `Document::removeObject()` / `removeObjects()` | deletion |

The last two carry the weight: neither goes through `touch()`, so they
are what covers delete, cut, paste and duplicate -- **and every
third-party command**, which a list of names never will.

**What stays allowed, deliberately:**

- **`Visibility`.** Showing and hiding is looking, and it is what a user
  reaches for while watching a model arrive. Exempted **by identity**,
  not by its `Property::Output` status: `Shape` carries `Output` too, and
  assigning a shape *is* an edit. There are two Visibility properties --
  the object's and the view provider's -- and one exemption covers both,
  because the view provider mirrors its own onto the object.
- **`TreeRank`.** The tree view's own ordering bookkeeping, written by
  the tree as it populates, from its own timer, never by a command.
  Exempted by identity like Visibility, because a command that runs a
  nested event loop -- the animated view fit `ImportGui.insert` runs
  while the load is still live, or a modal dialog -- lets that timer
  fire inside its own guard scope. Before the exemption the refusal
  unwound `DocumentItem::createNewItem` between `rootItem` being set
  and the item being inserted, and the next tick crashed in
  `DocumentObjectItem::getParentItem` (the chess-flat render golden
  with three heavy tests in parallel, 2026-09-05; the same run's chess
  golden diverged by camera for the same reason, the fit still animating
  under load when the harness restaged). Pinned twice: the exemption
  itself by `DocumentTest.liveImportUserEditExemptsTreeRankByIdentity`,
  and the whole chain -- import, guard, nested loop, tree timer, the
  tick after -- by `GuiLiveImportNestedLoop_tests_run`
  (`tests/gui/live-import-nested-loop.py`), which opens the window on
  purpose now that the goldens no longer animate through it.
- **View provider properties.** Every chokepoint above is in App and a
  `ViewProvider` is a separate `PropertyContainer`, so none of them sees
  the write itself -- but the write does arrive, one step removed:
  `ViewProviderDocumentObject::onChanged` touches the object's
  **`ViewObject`** mirror so the document notices presentation changing,
  and that touch reaches `checkUserEdit`. `ViewObject` is exempted by
  identity, the third of the three, because presentation is the class
  the live view exists to keep usable, and `RestoreDrainGuard` already
  draws the same line from the other side ("replayed view work must not
  modify the document"). Found by `GuiLiveImportNestedLoop_tests_run` on
  its first green-looking run (2026-09-06): the origin group's 300 ms
  resize timer fired inside the nested loop, wrote the origin's `Size`,
  and the command was aborted at `Origin.ViewObject`.
- **`isPerformingTransaction()`** -- undo, redo and rollback take an edit
  away rather than make one.

**What a write-time guard cannot cover is still refused by name**, and
the list is eight rather than most of the application: `Std_Undo` and
`Std_Redo`, because a bulk transaction replay left half-done by a throw
is worse than one never started; and `Std_Refresh`, `Std_Revert`,
`Std_Quit`, `Std_MergeProjects`, `Std_Import`, `Std_ProjectUtil`, which
damage the document without changing an object at all.

NOTE -- **Saving is deliberately not on that list.** `Gui::Document::Save()`
calls `flushDeferredRestore()`, which runs the sec 13 drain to completion
before writing -- so a save during the drain is already safe. That flush
is a no-op only while `Restoring` is set, which is the narrower window
`Std_Save*` is refused in.

A refusal explains itself in the report view, not only in a status-bar
hint that is gone in three seconds, and it distinguishes a load from an
import: both set `LiveImport`, so a user opening a file was being told
the document was "busy importing", which was not true.

### 15.3 Three traps this design walked into

- WARNING (severe) -- **A throw during stack unwinding terminates the process.** The
  guard first wrapped `_invoke()`, which contains the `AutoTransaction`.
  A refusal unwound into that transaction's rollback, whose own
  `removeObject()` hit the still-live guard and threw again. Fixed twice
  over: the guard is declared **after** the committer, so it is destroyed
  **before** it, and rollback is exempt outright.
- WARNING -- **`Prop_Output` is the wrong filter for "is this an edit".** It
  covers `Shape` as well as `Visibility`. The check therefore runs on
  every property write, with the one exemption named by identity.
- WARNING -- **No exception type escapes a command's own `catch (...)`.**
  `Std_Delete` catches `Base::Exception` and raises a modal "Delete
  failed" carrying this message. That is the command's doing and no
  choice of exception avoids it; `AbortException` is used so that
  `_invoke()` itself adds no second dialog. A modal **hangs an
  `xvfb` run** -- a harness must dismiss modals
  (`QApplication.activeModalWidget().close()` on a timer) or the test
  looks like an infinite loop.

### 15.4 Verified

With the document live: `Std_Undo` refused by name; `Std_Delete` trapped
at `removeObject` with both objects still present, and working again the
moment `LiveImport` cleared; `Std_ToggleVisibility` allowed, flipping the
object's `Visibility` and the view provider's together; a view-provider
property change allowed. The pointer half was confirmed by hand on the
real GPU -- mouse buttons responsive during a load, which was the symptom
this section started from.

## 16. Measured: what a GUI load of MiSTer spends after the blob store (Windows, 2026-09-15)

After the archive-backed blob store (FileBlobsManager.md sec 14) the headless
open of `MiSTer.FCStd` (17800 objects, 17058 shapes) is 2.4 s, and the GUI
load through `scripts/render-bench.py` was still 97 s (bgfx OpenGL, 1280x720,
`ProgressiveLoad` off, which is what the bench forces). A wrapper around the
bench turned on the App, Gui and Part logs and captured the restore lines; with
that logging on the same load reads 112-118 s.

| stage (logging on) | s |
|---|---|
| App restore | 24.7 |
| -- of which the `<Objects>` create pass | 19.3 |
| -- -- of which `addObject` | 4.9 |
| -- -- of which the progress sequencer's event pumps (new `[sequencer]` term) | 14.3 |
| Gui view providers (`finishRestoring`, inline) | 79.0 |
| -- of which visual build | 73.8, **34116 builds for 17058 shapes** |
| -- -- of which BRepMesh | 38.2 |
| refresh | 1.7 |

Two defects, both fixed.

### 16.1 Every restored visual was built twice

A call stack taken on the second build of each object (a dbghelp walk behind
an environment variable, in a scratch build) gave the same chain every time:
`Gui::Document::slotFinishRestoreObject` -> `ViewProviderDocumentObject::
finishRestoring` sets `Visibility` -> `ViewProviderPartExt::onChanged` ->
`updateVisual` -> `shapeStillMissing()` -> `PropertyPartShape::getValue` ->
`ensureRestored` -> `serveFromBlob` -> `setValue` -> the change notification ->
`updateData(Shape)` -> **`updateVisual`, nested, builds** -> back in the outer
call, which then **builds the same shape again**.

The serve announcing its value is by design (serveFromBlob and serveFromStore
both `setValue` under the object's Restore status), so the fix is on the Gui
side: while a view provider faults its own shape in, a nested `updateVisual`
for that same view provider stays touched and returns. The outer call, whose
serve has finished by then, makes the only build.

WARNING -- the first version of the fix skipped the *outer* build instead, and
it changed the scene: 45903 draws / 17.88 M triangles against 45867 / 18.16 M
in every earlier run (2026-09-14 twice, 2026-09-15). The guard version gives
the same 45903 / 17.88 M. So it is not that one of the two builds was the wrong
one: **the second build is not idempotent.** It meshes over what the first left
on TShapes that other objects share, and any single build of the document draws
the 45903 / 17.88 M scene. Settled coverage is identical in all of them (90468
px).

Compared as pixels: one frame each from a fixed camera after the timed frames,
old behaviour against the fix, same settings -- **157 of 921600 pixels differ
(0.017%)**, 74 of them by more than 8 levels and 4 by more than 96, all inside
one 295x193 region; average colour equal to three decimals. A tessellation
difference at a few edges, not geometry gained or lost.

### 16.2 The command refresh ran inside the create pass

The create loop's `seqRestore.next()` pumps the event loop at most every
200 ms, and those pumps cost 14 s -- but the new `restore <doc> gui live:`
line said only 1.6 s of it was frames. `GUIApplication`'s `SlowDispatchTrace`
(armed by `Render/LevelDebug` + `LevelSlowBuildMS`) named the rest: the main
window's `activityTimer`, 760 ms per firing, about once a second. It runs
`MainWindow::_updateActions()` -> `CommandManager::testActive()`, which asks
every command whether it is active, and each new object re-arms it.

Nothing a command could do is allowed while a document restores (sec 15.2), so
`_updateActions()` now does nothing while `App::Application::isRestoring()`,
leaving its timer running so the pass happens on the first tick after.

NOTE -- `testActive()` is ~830 ms per pass on this document **after** the load
too. That is an interactivity problem, not a load-time one, and is not
addressed here.

### 16.3 Result

Same settings, logging on; the old build behaviour restored for the A/B by a
temporary switch, so each fix is measured with the other held fixed:

| | neither | command refresh gated | both |
|---|---|---|---|
| App restore | 24.7 s | 10.0 s | **7.7 s** (create 5.3, sequencer 1.1) |
| event pumps during the restore | 17.1 s | 1.4 s | **1.4 s** |
| visual builds | 34116 | 34116 | **17058** |
| visual build / of which mesh | 73.8 s / 38.2 s | 78.4 s / 38.9 s | **47.4 s / 22.8 s** |
| load | 112.6 s | 101.6 s | **66.6 s** |
| draws / triangles | 45867 / 18.16 M | 45867 / 18.16 M | 45903 / 17.88 M |

The gate is 11 s of the load, the single build 35 s.

Without the logging, in the configuration of the 97.3 s bench run
(FileBlobsManager.md sec 14): **load 75.4 s**, settle 9.6 s, frame 238 ms,
settled coverage 90468 px as before. One run each; loads on this box move by
5-10 s between runs of the same binary (the logged "neither" column above read
112.6 s and 117.7 s on two runs), so the like-for-like figure is the table's.

## 17. Measured: scheduling a sensor per node was quadratic (Windows, 2026-09-15)

`FC_LEVEL_DEBUG` turns on the per-build split of `ViewProviderPartExt::
updateVisual`. Over the 17058 builds of the sec 16.3 load it read: mesh 23.1 s,
**prologue 13.1 s**, traversal 8.8 s, highlight 0.4 s, unattributed 1.8 s. The
prologue is the three Coin actions each build applies before refilling
(`SoUpdateVBOAction`, then selection and highlight clears), about 0.8 ms each.

### 17.1 Skipping the prologue moved the cost, it did not remove it

On a node never filled the actions have nothing to discard, so a trial skipped
all three when the sets were pristine. The skip fired on every build (prologue
0.02 s, scene identical), and the visual build stayed at 48 s: **traversal rose
from 8.8 s to 22.7 s.** Something about 0.8 ms per node was paid by whichever
code touched the node first.

### 17.2 What that something is

160 stacks of the main thread, taken by `cdb` attached to the running load and
broken in every 300 ms with `DebugBreakProcess` (a `sxe -c "~0 kc; g" bpe`
handler dumps and resumes). Of the 143 inside `updateVisual`, 49 were in
`SoDelayQueueSensor::schedule`, reached from a field write's notification:

| samples | where |
|---|---|
| 26 | Coin `SoSensorManager::insertDelaySensor`: a linear scan for the sorted insertion point |
| 23 | Quarter `SensorManager::sensorQueueChanged`: `QTimer::start`/`setInterval` on a running timer -> `killTimer` -> `QCoreApplicationPrivate::removePostedTimerEvent` |
| 6 | Coin `processDelayQueue`: `SbList::remove(0)` while a progress pump drained the queue |

Every shape node carries a delay-queue sensor (the render cache's
`VCacheSensor`, the faceset's `partIndexSensor`). The first notification of the
node schedules it; later ones find it scheduled and return at once -- which is
why the cost follows the first touch, and why `SoUpdateVBOAction`'s `touch()`
used to pay it. During a restore nothing drains the queue between the sequencer's
pumps, so it holds thousands of entries, and each insert scanned from the front
(all data sensors share one priority, so the new entry always belongs at the
end) and restarted two Qt timers. (An earlier env-gated split had charged ~10 s
of the prologue to the actions' destructors; the samples do not support that.)

### 17.3 Fix

- Coin fork, `SoSensorManager::insertDelaySensor`: append when the new priority
  is no smaller than the last entry's, otherwise bisect for the same position.
  The queue is kept sorted (`setPriority` reschedules a queued sensor), so the
  order is exactly the scan's.
- `src/Gui/Quarter/SensorManager.cpp`: the idle timer is started only if it is
  not running, and the timer-queue timer is restarted only for an earlier
  deadline. A later deadline fires early, finds nothing due and re-arms.

The prologue skip was dropped: with the fix it is worth 0.4 s.

Follow-up: the render cache's node sensor (`NodeSensor` in
`SoFCRenderCacheManager.cpp`) exists only for `dyingReference()` and has no
callback, so every schedule of it was a queue entry whose trigger did nothing.
It now swallows `notify()` (`SoBase::destroy()` calls `dyingReference()`
directly). With the two fixes above already in, this measured **no further
change** -- visual build 27.1 s, traversal 3.3 s, clean load 43.0 s against
44.3 s (within run noise), frame pixel-identical. It removes waste, not time.

### 17.4 Result

Same bench configuration as sec 16.3; the split runs are logged:

| | before (skip trial) | fix + skip | fix, no skip |
|---|---|---|---|
| visual build | 48.2 s | **27.2 s** | 27.4 s |
| -- traversal / prologue / mesh | 22.7 / 0.0 / 23.2 s | 3.4 / 0.0 / 22.0 s | 3.4 / 0.4 / 22.0 s |
| load | 67.5 s | **45.0 s** | 45.0 s |
| draws / triangles, settled px | 45903 / 17.88 M, 90468 | same | same |

Unlogged: **load 42.4 s** (fix + skip) and 44.3 s (the committed tree, no
skip) against sec 16.3's 75.4 s. The committed tree's fixed-camera frame is
pixel-identical to the frame before any of this (max channel difference 0). The document close after the
bench also rebuilds every visual once; that pass fell from ~12 s to 0.4 s.
Meshing is now four fifths of the visual build.

## 18. Design: the load tessellates in parallel, ahead of the drain (2026-09-16)

Sec 17 left meshing as four fifths of the visual build. It is the last big
serial phase of a load: the drain builds one shape at a time on the GUI
thread, because that is where the display nodes are.

### 18.1 OCCT's own parallelism is already on, and cannot scale here

`ViewProviderPartExt::updateVisual` has always asked for
`IMeshTools_Parameters::InParallel`, and BRepMesh does use it -- it splits
ONE shape over its faces (`BRepMesh_FaceDiscret`, `BRepMesh_EdgeDiscret`,
each an `OSD_Parallel::For` over the model's faces or edges). On a model
made of thousands of small parts there is nothing there to split. Measured
over the 38.6s visual-build window of the MiSTer load, by sampling every
thread's CPU time: **2.04 cores of 28**, the main thread 30.1s and all 28
pool threads together 48.6s of CPU to buy that. It is still worth keeping
-- turning it off costs 4.8s of a 22.0s mesh term -- but the scaling has to
come from somewhere else.

Meshing DIFFERENT shapes at once is what scales, and that is this section:
`src/Mod/Part/Gui/PreMesh.cpp`, behind `Render_PreMeshOnLoad`.

### 18.2 Where it hooks, and what it meshes

At the first slice of the progressive visual drain that may work -- every
shape served, nothing built yet -- the document's parked shapes are handed
to workers, each shape meshed WHOLE and single-threaded (`InParallel` off:
the split is across shapes now, and nesting the two only oversubscribes).
The drain's own BRepMesh call then finds the mesh resident and skips it
(`Render_MeshSkipRedundant`).

NOT inside the restore: serving a later object's shape early makes
`Feature::onDocumentRestored` run `restoreShapeContents()` on top of the
serve's own work (sec 14).

### 18.3 Two rules, and both are load-bearing

**The ask has to match, so the claim carries the GEOMETRY box.** The
display deflection derives from the shape's bounding box, and
`BRepBndLib::Add` defaults to preferring a resident triangulation over the
geometry, enlarging the box by `T->Deflection() + tolerance`. So a
pre-meshed shape measures BIGGER than it did: the build would ask for
something coarser than what is resident, and the redundancy check refuses
a finer resident mesh by default -- `Render_MeshSkipFinerResident` is off,
and for a measured reason. The call would then re-tessellate exactly what
the pre-mesh had just built, and the load would pay twice. Every claim
therefore carries the box measured BEFORE any triangulation existed, and
`updateVisual` derives its ask from that box instead of measuring again.

**A shape being meshed must not be touched.** BRepMesh writes the
triangulation into the TShape. A claim is IN FLIGHT until its worker has
published it, and a build that lands on such a shape parks itself the way
the load parks one; the drain then moves on to the next slice rather than
walking a queue whose every item is in flight. A bounds question about
an unbuilt visual (`ViewProviderPartExt::_getBoundingBox`, which must not
build) used to read the shape anyway -- `BRepBndLib::Add` reads the
resident triangulation, and even with `useTriangulation` off it fetches
each face's handle. It now answers from the claim's geometry box while
the claim is in flight (`preMeshBox`); a reopen of 300 finely meshed tori
asked about every object in a loop answered 56 of its questions that way.

Excluded, on the principle that a doubt excludes: roots sharing a face or
an edge TShape with another root (two workers would write one
triangulation, and their asks may differ -- both go, which is why nothing
is submitted until the whole batch is known), instancing candidates (an
instanced build shares one tessellation and asks per member), and shapes
big enough to take a stand-in, whose coarse mesh the refine pool delivers
at a deflection decided there.

Claims are dropped once every queue the drain serves is empty. A claim
outliving its load is a bounding box keyed on a TShape address that a
closed document may free and a later allocation reuse; until then each
claim also pins its own shape.

**A claim covers the faces and edges of its shape, not its root alone**
(sec 18.8, 2026-10-07). A mesher writes into faces and edges, and a shape
that was never in the batch can be made of the faces of one that is. The
build and the bounds question ask about the shape and everything it is
made of (`preMeshInFlight(shape)`, `waitPreMesh(shape)`), and a shape
made of faces in flight is not given a worker of its own.

**The PUBLISHED claims, that is. One still in flight stays** (sec 18.7,
2026-10-07): "every queue is empty" says that no build will ask again,
and says nothing about whether the batch is done. Such a claim goes on
answering as in flight, is held by its batch as well as by the map, and
is taken out by its worker when it publishes. A TShape in flight is not
given to a second worker either.

### 18.4 Result

MiSTer, 17058 solids, the DEFAULT path (`ProgressiveLoad` on, coarse rung
2), one build, A/B by the parameter alone:

| | off | on |
|---|---|---|
| drain's visual build | 22.4s / 159 slices | **15.8s / 91 slices** |
| pre-mesh batch | -- | 7171 shapes in 3.7s wall |
| frame at convergence | reference | **pixel-identical** |

And with the split reporter, against the same load before this: the GUI
thread's REAL tessellations fall from **7578 (16.0s) to 403 (5.0s)** --
validated-only calls unchanged at ~8766, so 7175 asks were answered by
geometry the workers had already meshed -- the mesh term from 17.2s to
6.2s, the whole visual build from 26.3s to 13.6s, and the settled frame
from 69-73s to 56-58s. A per-shape audit over 17054 objects found zero
triangle-count differences, and the same 5342 objects ending at the exact
rung.

### 18.5 The measurement trap this walked into twice

The first comparison showed 1843 pixels differing in one small region and
1% fewer primitives, and two baseline runs were pixel-identical to each
other -- which looked like proof that the difference was the change rather
than run variance. It was neither. A per-shape audit named seven objects
built at `lvl 2` with the pre-mesh and `lvl -1` without it, and the build
timeline explained why: without it those seven are built TWICE, coarse at
t=72.8 and again at t=76.5 at the exact deviation, the second build being
the fidelity ladder's refine landing. With the pre-mesh the load finishes
~12s sooner, so the bench captured BEFORE that refine landed. The two
baselines agreed with each other only because both were equally slow.

Captured after the ladder converges in both arms (a 30s quiet window
instead of 5s), the frames are pixel-identical and every triangle count
agrees. **A faster load moves the capture, not the mesh** -- any A/B of
load speed against a picture has to let the ladder settle in both arms,
and primitive totals still carry a few thousand of refine variance where
the frame does not.

### 18.6 The load with no drain to hook

Sec 18.2 hooks the batch to the first slice of the progressive visual
drain. With `ProgressiveLoad` off there is no drain at all: every visual
is built inside the restore, one per object, as App signals them from
`afterRestore`'s dependency-sorted walk. That load got nothing from the
above, and it is the configuration every measurement in this document is
taken in (`scripts/render-bench.py` forces the preference off).

**Where it hooks.** The latest moment still ahead of the meshing is the
FIRST of those signals, so the batch is submitted from
`ViewProviderPartExt::finishRestoring()` -- once per load, and a no-op
on the progressive path. By then every object exists and the archive's
file phase has run, which is what makes the shapes readable without
forcing a serve of anything.

**Why reading every shape there is safe, and the one case it is not.**
The read serves each parked shape ahead of that object's own
`onDocumentRestored`. Two things carry it. The serve announces its value
(`serveFromStore` calls `setValue`), and that notification reaches the
object's own view provider -- whose `updateVisual` returns at once while
the view provider is still flagged `Gui::isRestoring`, which every one
of them is except the single object currently being finish-restored. So
the read costs a serve and refuses a build, which is the same serve that
object's own build would have paid for later.

The case it is not safe is shape contents. `Feature::onDocumentRestored`
skips `restoreShapeContents()` only while the shape is still pending,
and `ensureRestored()` runs it when the shape arrives -- so serving
early makes it run TWICE, once on the serve and again from that object's
own restore (sec 14). The collector therefore refuses any object
carrying a shape-contents or shape-content-owner property, which is the
only thing a second expansion could damage. A doubt excludes, as
everywhere else in this batch.

**Parking is not available here, so the build waits.** The in-flight
rule of sec 18.3 parks a build whose shape a worker still owns. On this
path there is nowhere to park it TO: the build is running inside the
restore, and the drain's slice machinery checks only document
eligibility, not the preference -- so a parked build would land after
the restore and quietly turn a synchronous load into a partly
progressive one. An open that returns with the document still arriving
is the one thing `ProgressiveLoad` off rules out. So the gate waits for
the worker instead (`waitPreMesh`) and then builds exactly as it would
have; the GUI thread has nothing else to do inside such a load. Parking
remains the fallback if the wait's backstop ever trips.

**Claims are dropped at the end of the restore.** With no drain, nothing
would have called `clearPreMeshClaims()` and the claims would outlive
their load -- a bounding box keyed on a TShape address a closed document
may free. They are cleared from `signalFinishRestoreDocument`, which App
emits after the per-object walk, and from `signalDeleteDocument` for the
load that never finished; in both cases only when no drain owes anything,
since claims are global and the drain owns them where there is one.

Measured on the MiSTer reference, 17058 solids, A/B by the parameter
alone (7172 of 17057 shapes submitted):

| | off | on |
|---|---|---|
| load, unlogged | 46.8s | **31.5s** |
| load, with the split reporter | 52.0s | 33.4s |
| visual build of the restore | 30.9s | **17.3s** |
| of which mesh | 24.8s | **12.0s** |
| traversal / prologue | 3.8s / 0.4s | 3.5s / 0.3s |
| frame | reference | **pixel-identical** |

The frame is identical at every threshold (45903 draws in both arms,
primitive totals 10 apart out of 17.88M -- refine variance, sec 18.5).
Neither arm logged a `progressive load` line, which is the check that
the load stayed synchronous and nothing parked.

The batch itself, from the line this path now reports for it: **7172 of
7172 claimed shapes meshed in 6.5s of wall time, none failed.** That is
longer than the 3.7s the same batch takes on the progressive path (sec
18.4), and the ask is why -- `CoarseTessellation` is -1 here, so every
shape is meshed at the full display deviation rather than at a coarse
rung.

Read the table as a range, not as constants. A third pre-mesh run put the
load at 35.3s with a 14.5s mesh term against leg b's 33.4s and 12.0s, so
what this buys on the bench path is 11-15s of a 47s load depending on the
run -- the arms differ by more than the reporter's own overhead does.

Mesh is still 12-14s of it, and still the bulk of the visual build. The
batch covers 7172 of the 17057 shapes -- the rest are refused for sharing
a face or an edge TShape with another root -- so the GUI thread goes on
tessellating everything the collector would not claim. That remainder is
the next thing to attack here, not the hook.

### 18.7 The heap corrupted while a live import is meshed (found and fixed 2026-10-07)

FIXED, `ba4a70c6a5`. The first half of this section is as it was written
the day the fault was found and not understood; what it turned out to be
follows it.

**What was seen.** `tests/gui/live-import-nested-loop.py`
(`GuiLiveImportNestedLoop_tests_run`) died in about one run of twenty,
after passing its checks or before it has made one, at a different place
each time: `QRegion::cleanUp` under `QWidgetPrivate::getOpaqueChildren`,
`QRegion::operator+=` in `QWidgetRepaintManager::paintAndFlush`,
`malloc(): smallbin double linked list corrupted` under a `QPen`, and
`free(): invalid pointer` freeing a local vector in
`BGFXView::submitBackground`. One failure in the full `ctest` of
2026-10-07 at `50560fcf27` (963 of 964); alone, 1 run in 18, and under
gdb 1 in 3 and 2 in 31. None of the 341 crash logs on the box before
that day has Qt's region code as its innermost frames.

**What it is.** Not four faults: one heap, damaged. In the last of them
the vector's block lies INSIDE a block malloc holds as free -- a free
chunk of 0x150 bytes begins 0x60 bytes ahead of it and covers it, and the
vector's own header holds free-list pointers. That is a block freed while
somebody still owned it, not a buffer overrun. The free chunk still held
six doubles of float precision, a box about 460 by 22 by 147: the chess
set's scale.

**Where it comes from.** With glibc's malloc checker on the FreeCAD
process the run stops at the fault and not at a victim, on the FIRST run,
every time, on a pre-mesh worker:

    PartGui::BatchFunctor::operator()        src/Mod/Part/Gui/PreMesh.cpp:123
    BRepMesh_IncrementalMesh::Perform        BRepMesh_IncrementalMesh.cxx:80
    BRepMesh_Context::~BRepMesh_Context
    IMeshTools_Context::~IMeshTools_Context  (a handle<IMeshTools_ModelAlgo>)
    free(): invalid pointer

with seven other workers inside `BRepMesh_IncrementalMesh` at that
instant. The second capture stopped in the same destructor on a
segmentation fault, the context's algorithm objects already holding heap
addresses where their vtables belong. With `Render/PreMeshOnLoad` off the
same test passes 4 runs of 4 under the checker, eight checks each.

**What it was: a claim freed under its worker.** The six doubles were the
answer all along -- a `Claim` holds a `Bnd_Box`, and the damaged free
chunk was a freed claim. `clearPreMeshClaims` said in its own comment
that "the entries themselves are not freed here", an in-flight batch
still pointing at them, and then cleared a map of `unique_ptr<Claim>`,
which frees every one. Each worker went on to publish: `claim->done =
true`, one byte, 88 bytes into a 96-byte block the allocator had by then
given to somebody else. Who that is was not traced. My reading of the two
pictures: under the checker, which keeps no per-thread cache, the block
is handed straight to another worker for one of the mesher's own small
objects, and that is where it stopped; without it the block goes back to
the thread that freed it, the GUI thread, and the victim is whatever Qt
or the renderer allocates next in that size.

Measured with three lines of `fprintf` in `PreMesh.cpp` (not kept), 4
runs of 4, the same every time:

    15 asks, "no claim"            the import builds the 15 shapes itself
    submit: 15 items               the drain's first slice hands the same 15 over
    clear: 15 claims, 15 IN FLIGHT the same slice, its queue popped empty
    15 publishes after the clear   each into a freed claim
    15 asks, "no claim"            later rebuilds, told the shapes are free

So it was not a rare interleaving. Every run of this test wrote fifteen
stray bytes, and one run in twenty something died of one. **The drain
clears when "no build is going to ask again", and that is not "the batch
is done"**: these fifteen were parked and then built before the drain's
first slice, so the slice submitted them, popped each one as
parked-but-no-longer-touched without building it, and found its queues
empty with every worker still running. Who built them, from a breakpoint
on the first of those asks: `ImportGui.insert` itself. It creates its
objects under the `Restoring` guard, which parks their visuals, and when
the guard is gone runs `finishRestoring` on each new object
(`AppImportGuiPy.cpp:643`), which builds it on the spot and leaves it on
the drain's queue. **Not the import's view fit**, which is what I wrote
in the two commit messages (`ba4a70c6a5`, `fbd65293ac`) before looking;
the code comments say it right since the commit after them.

**Ruled out on the way: two threads on one shape.** That was this
section's own guess (a sharing below the root, a GUI thread reading what
a worker writes). A preloaded wrapper round the two
`BRepMesh_IncrementalMesh` constructors that mesh -- it records the face
and edge TShapes each call owns while it runs and reports an overlap
between threads -- saw 45 mesh calls and 0 overlaps, 3 runs of 3, and
again after the fix. It answers for meshing against meshing only: a
reader of a shape a worker writes is not something it sees. **No
sanitizer build was made**; nothing here needed one.

**What is changed.**

- A claim is shared between the map and the batch that publishes it
  (`shared_ptr`), so no clear can free one under a worker.
- `clearPreMeshClaims` drops the PUBLISHED claims. One still in flight
  stays in the map and answers as in flight -- `preMeshInFlight`,
  `preMeshBox`, `waitPreMesh` -- until its worker publishes it, and that
  worker takes it out then if a clear has passed since its batch was
  submitted (`publishLocked`). Before, the shape answered "not claimed"
  from the clear on, and the GUI thread was free to build a shape a
  worker was writing: the second of the two rules of sec 18.3, broken
  whenever a clear met a batch.
- `submitPreMesh` leaves a TShape that is already in flight to the worker
  that has it. Two batches on one TShape were two writers of one
  triangulation, and the first to publish released the shape with the
  other still writing it.
- The drain submits a parked shape only if it is still to be built
  (`fbd65293ac`): "15 of 15 parked shapes submitted" and then "0 of 15
  visuals" built became "0 of 15 parked shapes submitted". A plain
  progressive reopen of twelve solids is as it was, "12 of 12 parked
  shapes submitted", twelve meshed, the drain builds them (read from the
  log lines; it has no test of its own).

**Measured.**

| | before | after |
|---|---|---|
| `PreMesh_tests_run` (new: `tests/src/Mod/Part/Gui/PreMesh.cpp`, three cases) | 1 of 3 | 3 of 3 |
| a shape still being meshed, asked about after a clear | "not claimed", and the wait returns with the worker still meshing | in flight, its box from the claim, the wait ends at the publish |
| the same TShape submitted while in flight | claimed twice, two workers | claimed once |
| the live-import test under the malloc checker | 3 runs of 3 dead on a pre-mesh worker inside BRepMesh, no check made | 5 of 5 clean, eight checks each, and 3 of 3 with `fbd65293ac` |
| the live-import test, plain | about 1 run in 20 dead | 20 of 20 |
| claims in flight at the clear, publishes into freed claims | 15 and 15, every run | none freed |

**Not covered.** The unit test pins what a clear and a second submit do
to a claim in flight; the write into freed memory itself is seen only
under the checker, which no test runs. A GUI thread READING a shape a
worker writes, other than through a build or a bounds question, was not
looked for. And the batch is still a detached thread: a process that
exits under one was not looked at. **Both were looked at the same night:
sec 18.8.**

**To run the checker** (twenty seconds, no rebuild):

    env LD_PRELOAD=/lib/x86_64-linux-gnu/libc_malloc_debug.so.0 MALLOC_CHECK_=3 \
      build/conda-relwithdebinfo-801/bin/FreeCAD --user-cfg <dir>/user.cfg \
      tests/gui/live-import-nested-loop.py

under `xvfb-run` with `QT_QPA_PLATFORM=xcb`, `GT_OUT` and `GT_RESULT` set
as `scripts/gui-test.sh` sets them, and the conda wrapper in FRONT of
`env`, so that FreeCAD alone gets the checker. Under gdb the two are `set
environment` lines.

### 18.8 Who else touches a claimed shape, and a process that leaves under a batch (2026-10-07)

The two things sec 18.7 named as not looked at. Both turned out to hold
defects, three of them not the pre-mesh's at all.

**The instrument** (scratch, not in the tree; an `LD_PRELOAD` library):
wrappers round the mesher's constructors that mesh, round
`PartGui::submitPreMesh`, and round 32 of `BRep_Tool`'s readers of what a
mesher writes -- a face's triangulations, an edge's list of curve
representations, which polygons are appended to and removed from. It
knows which faces and edges are CLAIMED (from a submit until the worker
of that shape is done) and which are BEING MESHED and by which thread,
and reports any other thread that reads or starts a mesher on one, once
per distinct stack, with a count at exit. Three things about it that cost
time: FreeCAD's modules come in by `dlopen`, so `dlsym(RTLD_NEXT)` finds
nothing and the real functions are taken from the library's own handle;
OCCT's libraries call their own exported functions through the GOT, so
calls made INSIDE OCCT are seen too; and **a mesh call made with OCCT's
own parallelism has helper threads of OCCT's pool reading for it** -- the
first version took those for a second party and reported 24 "overlaps"
that were one mesher and its helpers. A second party is the main thread,
or a thread that is itself inside a mesh call. `MESHCLASH_HOLD_MS=n`
makes each pre-mesh worker wait n ms before it meshes, which stretches a
batch from a fraction of a second to as long as is wanted; the wrapper's
own state is never freed, so that it adds nothing of its own at an exit.

A batch at its own speed is short -- 400 solids in 0.18 s, 3000 in about
a second -- so most of what follows was first seen stretched and then
looked for at the batch's own speed.

**1. FreeCAD's own code, nobody touching anything: clean on independent
shapes.** 400 solids, the batch stretched to 5.3 s, the open returning
after 0.5 s: 50845 reads checked while shapes were claimed, none of a
claimed shape.

**2. The load's own drain meshed shapes whose faces were claimed. FIXED,
`5ae3607abd`.** A claim was keyed on its shape's own TShape and a build
asked about that. A shape that is not in the batch can be made of the
faces of one that is, and the collector's "both go" (sec 18.3) answers
only for two shapes it has both read -- a compound under instancing, a
shape it may not read yet, it passes over BEFORE that test, and the
partner stays in the batch. One such object over twelve primitives, the
batch stretched:

| the object | submitted | the drain, on the GUI thread, with its faces claimed |
|---|---|---|
| a `Part::Compound` over four of them | 12 of 13 | meshes it; 17 distinct reading paths |
| a `Part::MultiFuse` of three that do not touch | 12 of 13 | meshes it; 17 |
| a feature holding a compound of three others' shapes | 12 of 13 | meshes it; 17 |
| a `Part::Cut` of a box by one of them | 13 of 14 | meshes it; 15 |
| a shell made of two others' faces | 12 of 13 | meshes it; 11 |
| a feature holding another's very shape | 11 of 13 | nothing: both were left out, as sec 18.3 says |

And at the batch's own speed: a document of 3000 solids with compounds
over some, three loads of three, one mesher call and 1869 reads of
claimed faces and edges, 80 ms into a batch of one second
(`runDeferredVisualSlice`, `updateVisual`, `captureVisualFill`,
`BRepMesh_IncrementalMesh`; before it `meshingBounds`). How long a batch
must run for the drain to get there, on 300 solids with the compounds
first: at 0.13 to 0.19 s nothing, at 0.36 s a first read 269 ms in, at one
second a mesher call 387 ms in. **The two were never seen inside one face
at one moment** -- not in seven loads of two 3000-object documents, one of
them laid out to make it likely. Nothing kept them apart but where the
shapes sat in the queue.

A claim now holds its faces and edges while in flight; the build's gate
and the bounds question ask about a shape and everything it is made of,
and the bounds question waits for the workers where the shape has no
claim of its own to answer from; a shape made of faces in flight is not
handed to a second worker. After: none, in all six kinds and in the three
loads, every visual built.

**3. OPEN: what is not a build does not ask.** While shapes were claimed,
each of these on an object late in the batch:

| the action | a claimed shape is |
|---|---|
| the view fitted to everything, or to the selection | not touched |
| an object selected (`Gui.Selection`), a face of it selected | not touched |
| hidden and shown; its Deviation, colour, transparency, Placement changed | not touched |
| `ViewObject.getBoundingBox()` | not touched (answered from the claim) |
| a parameter changed and the document recomputed | not touched (a new shape) |
| **an object clicked in the tree** | **read, and MESHED on the GUI thread** |
| `doc.copyObject` | read (`PropertyPartShape::SaveDocFile`, `exportBrep`) |
| the document saved | read (`ShapeRefSet::add`) |
| a `Part::Cut` made of two claimed objects and recomputed | read, 88 paths (the boolean) |
| a script: `obj.Shape.BoundBox`, `.Volume` | read |
| a script: `obj.Shape.tessellate` | read and meshed |
| an object exported to STEP | read, 17 paths |

The click is FreeCAD's own doing with every setting at its default: the
tree's sync view has the view follow the selection (`onItemSelectionChanged`,
`ViewSelectionExtend`, `viewObjects`, `checkElementIntersection`), which
asks the shape for its faces (`TopoShape::getFacesFromSubElement`,
`getDomains`), and `getDomains` MESHES a shape that has no triangulation
(`meshShape`). The rest are the user's or a script's: by construction
nothing stands between them and a shape a worker is writing, since the
claims live in PartGui and these read through Part. What a reader risks
depends on what the worker is doing to that face at that instant, and is
worst where the shape already carried a mesh the worker replaces; none of
it was seen to fail, and none of it was made to. **Not fixed: it is a
design question** -- one gate in Part that every reader of a
`PropertyPartShape` passes and that waits for a claim in flight, against
the pre-mesh meshing a private twin of each shape and handing the
triangulations over on the GUI thread, which removes the rule instead of
enforcing it.

**Ruled 2026-10-08: the private twin** (the user: "Private copy sounds
good. It even works out of process."), not built. His question with it --
does OCCT let a mesh made on another shape be installed on this one --
measured with a scratch program on a model of planes, a cylinder, a
sphere and a torus with their seams, edges shared between faces, and one
solid held at two places:

- **Yes.** A `Poly_Triangulation` holds nodes, UV nodes, triangles and a
  deflection and refers to no face; a `Poly_PolygonOnTriangulation` holds
  node indices and refers to nothing. What ties them to a shape is the
  entry on the face and on the edge, which `BRep_Builder::UpdateFace` and
  `UpdateEdge` write for whichever face and edge they are handed. Moved
  from a twin onto the original: 6419 triangles, 3462 nodes and 38 edge
  polygons, the same as a mesh made on the original; `BRepTools::
  Triangulation` finds it complete; the mesher asked for the same mesh
  leaves all 19 triangulations as installed, and asked for a finer one
  replaces all 19.
- **The same across a BREP write and read**, the receiver installing
  COPIES of the mesh objects and pairing faces and edges by index alone --
  the stand-in for another process. The read-back shape has its faces in
  the original's order, which every saved document's element names
  already rest on.
- What makes it right: the twin is made without copying the geometry, so
  its faces lie on the original's own surface and curve objects (19 of
  19), and UV nodes mean the same thing on both; and an edge's polygons go
  over together with the triangulation they index.
- **The mesh then lives where it lives today**, one triangulation on the
  face, shared by every shape that shares the face. Nothing is kept beside
  the shape and nothing is held twice once the twin is dropped.
- **OCCT's own copier is not the twin to use.** `BRepBuilderAPI_Copy`
  makes a solid held at two places into two solids: 19 distinct faces
  where the original has 10, and 6874 triangles meshed for 6419 kept. The
  BREP round trip keeps them one. The twin wants a copy of its own, one
  new TShape for each of the original's, which is also the pairing.
- The fork's level builder already meshes such copies on the pool
  (`MeshLevelBuild.cpp`, `meshedCopy`) and reads arrays off them; the
  hand-over is the new part.
- Not tried: a face that is a triangulation and no surface (a glTF
  import), a free edge's `Poly_Polygon3D` (the model has none), a face
  holding several triangulations, and what making the twins costs the GUI
  thread at the submit.

**4. A document closed from inside a drain slice. FIXED, `3c7c8bdcbf`; not
the pre-mesh's.** Found by closing the document during a batch to see the
process leave. A slice reports through a progress sequence, the progress
bar runs events from inside the slice, and a document closed by one of
them -- a script's timer -- had its queue erased from the map the slice
was walking and the sequence destroyed under the call into it. A scripted
`closeDocument` 200 ms into the drain of 3000 solids, nine runs: six dead
in `runDeferredVisualSlice` (`App::Application::getDocument` on a freed
name, `std::_Rb_tree_increment`, a freed string written to the log;
"double free or corruption" in a run that also left), two hung, the GUI
thread at 109 % in the walk over the map, and one that lived. (The commit
and the first version of the test's header say "of six runs four dead and
one hung", which is five of those nine counted wrong.) With
`Render/PreMeshOnLoad` off: one dead and two hung of four. The user's own ways to close -- the window, the
application's quit, `Std_CloseActiveWindow` -- are not acted on while a
load's progress bar runs, silently, and work after it; which check turns
them down was not looked into. A close that comes while a slice is on the
stack now empties the queue and counts itself on it; the slice reads the
count before and after the events, resolves what it popped only after
them, and lets go. `tests/gui/close-during-visual-drain.py`
(`GuiCloseDuringVisualDrain_tests_run`), nine closes in nine loads in one
process: three runs of three dead or hung before any check before, 4 of 4
checks in four of four after.

**5. A process that leaves under a running batch. FIXED, `806ab94621`.**
With the batch stretched to span the exit -- the document closed by
script 200 ms in, then `QCoreApplication.exit` -- nine runs of nine died
on a pre-mesh worker in `Geom2dAdaptor_Curve::load` under `BRepMesh`, one
of them hung as well: the workers ran the mesher while the process took
OCCT's static data down. At the batch's own speed the workers were done
before the process got that far, 12 exits of 12 clean. `stopPreMesh`,
called from `QCoreApplication::aboutToQuit`: a worker starts no shape it
has not started and finishes the one it is on, the claims of the rest are
published unmeshed, and it returns with no batch running. After: nine of
nine clean, three of them under the malloc checker, the wrapper counting
over 2000 of the 2669 shapes not started at the exit and no mesher call
in progress.

**6. `QCoreApplication.exit` with a document open. FIXED, `052fea82aa`;
nothing to do with a load.** The control for 5 showed it: exit ends the
event loop without the main window being asked to close, the main window
-- a local of `runApplication` -- went with the documents' views in it,
the last view of each closed its document from its destructor, and
closing a document asks the main window that is being destroyed for its
active view. A segmentation fault on the way out with a box in a document
and nothing else, three runs of three, and nine of nine during a load.
The documents are now closed where the loop returns, as a closing main
window closes them. `tests/gui/exit-with-open-document.py`
(`GuiExitWithOpenDocument_tests_run`): status 1 in three of three before,
0 in four of four after.

**Seen and left.** Under the hold the drain turns one slice per turn of
the event loop while everything left on its queue is in flight -- 278753
slices and 3.98 s of the GUI thread in a batch of 5.3 s. At the batch's
own speed it is 14 slices for 3000 visuals, so it is the hold's doing and
not a finding; a load whose batch really ran for seconds would show it.

## 19. Progressive load against eager (2026-09-29)

ProgressiveLoad (sec 13, default on since `d53ba63848`) builds a restored
document's view providers after the App load, in slices: phase one creates
every view provider, phase two replays its GuiDocument.xml record, phase
three sweeps the properties and runs `finishRestoring`. That changes two
things an eager load relied on without saying so: the ORDER things are
created in (App properties are complete before any view provider exists,
so a container claims children that have none yet), and the SIGNALS (the
replay raises no per-property Gui change, so whatever an eager load put
right on `slotChangedObject` is not put right). It had already lost a
Part's children on reopen for seven weeks (`002c5c1d7f`). This section is
the systematic pass that asked what else.

**The method: a differential.** `tests/gui/progressive-load-diff.py` opens
each file eagerly (ProgressiveLoad off, the reference), progressively N
times -- which view provider a load reaches first follows allocation
order, so one green run proves little -- and eagerly again, the noise
floor (an item the two eager opens disagree on is not judged). Each open
waits for the drain and for the level ladder's idle refinement, then
records, for every document the file brings in:

- per view provider: Visibility, isVisible, isShowable, the display
  switch, display mode, claimChildren, ClaimedChildren and ClaimedBy,
  element colours, bounding box (to 1% of its size);
- the scene: every node path to every SoFCSelectionRoot as the chain of
  objects whose roots it passes -- per OCCURRENCE, since a name-only probe
  once called a half-broken scene green; past 300 objects, the depths of
  each root's occurrences (pivy casts every node it returns through a
  linear type lookup, and the chains cost 130 s a snapshot on a
  653-object file); past `PL_PATHS_LIMIT` roots (1500) not at all -- links
  and binders multiply occurrences, and one search over a 494-object file
  with 92 SubShapeBinders ran for half an hour, so the user files were
  run with the limit at 300;
- a pick grid from one camera, an edge or vertex hit not judged (a
  boundary cell a pixel flips);
- the backend's frame, forced fresh after the camera is set (a frame
  staged before it came back once, NaviCube and unlit faces included).

The generated corpus: children created after their Part and before it,
nested Parts and Links to Links, a LinkGroup with a hidden element, link
arrays, a PartDesign Body with a Link to it, groups with a hidden, a
wireframe, a face-coloured and a transparent member, booleans, a sketch
attached to a face, 327 objects in interleaved creation order, and a
cross-document pair -- placements off identity throughout, identity being
what hid the lost Part children. Then operations made while the drain
still runs, against the same made after an eager open: an edit and
recompute, a delete, a move between Parts, an undo, hides, a recompute of
everything, a revert, and closes during the drain.

**Found and fixed** (each reproduced on every progressive run):

1. **A shape built twice.** `updateVisual`'s read of a blob-held shape is
   the fault-in, and the landing shape's notification builds the visual
   inside that read; the outer call built it again. Every blob-held shape
   of every progressive load (docs/CoinRetirement.md 5.27, `2b184941bf`).
2. **A Body's Origin in no occurrence of the scene.** A container
   rebuilding its 3D children while a child's view provider was being
   created -- registered with the Gui document, not yet announced to the
   application -- cached the child as claimed, and a claim through a
   LinkView (a GeoFeatureGroup's) looks it up application-wide and left
   it out. Every later rebuild compared the claim equal. The Body's
   Origin, and with it the planes a sketch edit shows, were nowhere.
   `Document::handleChildren3D` now leaves an unannounced child out and
   claims it again once `slotNewObject` has announced it.
3. **A secondary view never built.** The drain's queue names objects and
   finds the object's own view provider, so a second one attached to the
   same object -- a sketch's internal faces, a PartDesign feature's
   add/sub preview or suppressed shape -- stayed parked, and the
   bounding-box hook leaves a parked visual alone: a sketch's internal
   face view kept a stray point at the sketch origin, which its bounding
   box and every fit took in. Secondary views (`Gui::SecondaryView`) are
   no longer parked.
4. **The origin size never recomputed, then infinite.** The size follows
   the content through `updateData`, which the replay never raises: a
   progressive Body kept the size saved with it. Scheduling the update
   from `finishRestoring` exposed a latent defect -- a SubShapeBinder
   bound to a datum plane reports +-1e100, and the origin planes came out
   with a Size of inf and NaN geometry. `updateOriginSize` now skips an
   unbounded box, which an eager load could hit as well.
5. **A hide during the drain undone.** Phase three's sweep pushes a view
   provider's restored Visibility onto its object, so a Part or a Link
   hidden while the drain ran came back visible (a box did not). A
   visibility set outside the drain's own slices is kept and re-applied
   after the object's `finishRestoring`.
6. **Document data changed by opening it.** An image plane's view
   provider writes the image's own size into `XSize`/`YSize` unless it is
   restoring, and phase three sweeps with the restore status dropped: six
   reference images of a user file came out 5-14 times smaller.
   `RestoreDrainGuard` suppressed the touch but not the write, so a save
   for any other reason would have kept it; its report
   (`progressive restore X: N document changes suppressed ...`) named
   them. The plane now also refuses during `RestoreDrain`.
7. **An image plane untextured after an EAGER open** -- the one defect
   the differential found on the other side. The image is a file included
   in the archive, which an eager load writes out after the objects are
   restored, so the load at restore found no file; the drain, replaying
   later, had it. `finishRestoring` now loads the texture, and only the
   texture (the size is the saved one).

8. **A Link to an image plane drew nothing.** A view provider whose
   record names an archive entry or a blob is restored inside the
   blocking window (`restoreCapturedViewProvider`, the content is held
   open only that long) -- and was then finished by the App's
   finish-restore signal, before any other view provider existed. The
   Link linked nothing, and, no longer restoring, phase three passed it
   over. Three user files lost Links that way (one a Body under a Part).
   It is now left restoring for phase three while the document parks its
   view providers.

Commits: `284429b4cc` (2), `1759f7a3f2` (5), `1aa780f7de` (3),
`6fd064cd5f` (4), `836ba8aa2a` (6), `76a0345254` (7), `62172a0e88` (8);
(1) is `2b184941bf`.

**The drain now runs in the eager order (was: records before updates).**
An eager load raises every property's update as Document.xml restores it
(`DocumentObject::onChanged` -> `signalChangedObject` ->
`Gui::Document::slotChangedObject` -> `updateData`), with the view
providers not yet restoring -- `Gui::Document::Restore` marks them only at
`signalRestoreDocument` -- and reads the GuiDocument.xml records after all
the data files: the saved Gui state overrules whatever a handler did on an
update. The drain replayed the records in phase two and swept the updates
in phase three, so the handler overruled the file: a `Part::MultiFuse`
hides its inputs on an update, and the Body a user had shown again came
back hidden. Guarding each such handler (every boolean, every feature that
hides its base, every Python feature) is a list that never closes; the
drain now has four phases in the eager order -- create, sweep (the
restoring flag down, as for the eager updates), records, finish (and,
since `1c12f663cc`, the updates `afterRestore()` raised after the records
were read, replayed per object in the finish phase). Two more pieces were
needed before the user files agreed:

- a view provider restored at once (sec 19 item 8) had its record before
  the sweep, and in older files that is most of them: a colour array saved
  as `DiffuseColor.bin` makes a record name an archive entry. Its record
  XML is kept and replayed in phase three like the parked ones -- the XML,
  not copies by property name, because a legacy name (`ShapeColor`)
  migrates to another property on restore and a copy undid the migration;
  what only an archive entry holds cannot be read twice, so those
  properties are copied as the sweep begins (after phase zero has served
  the entries) and pasted after the replay; and the Python proxies are
  held across it and put back -- restored twice, a proxy is a new
  instance, a view provider attaches its proxy once, and a Draft array
  whose new proxy had no `self.Object` claimed none of its children;
- `ViewProviderPartExt::updateColors` maps a boolean's colours from its
  inputs on a shape change, and returned only while the DOCUMENT restores.
  A shape served after the load (a deferred entry, a blob faulted in by
  the visual build) lands under the OBJECT's Restore status instead, and
  the mapping overwrote the saved colour; it now returns then too.

**The user files.** 73 distinct FCStd from `~/works/sw/bug_reports`, two
progressive runs each, scene paths compared up to 300 objects. After the
fixes above, 61 of the 71 that opened are identical apart from derived
sizes, and two more differ in one run of two only (a pick, a frame);
the Link losses (8) and the image sizes (6) came from here. What is
left was almost all the ordering finding above: objects hidden after a
progressive open that eager leaves shown (Draft wires, a fusion that is
another boolean's input, a placement feature's input -- four files) and a
face colour taken from an input (a Mirroring, a MultiFuse). After the
reorder, the files that still differed were run again (22, same-named
siblings included): every
visibility, claim and colour difference is gone; what remains is derived
sizes, a wireframe's line-level pixels in one file (0.5-0.8%, a coarse
first tessellation not refined within the 3 s wait), a Body's bounding
box wider in one file (and in one copy of another) and small pixel-only
differences in two. Examined (2026-09-29, session 108):

- **The wider Body was a datum plane sized by the load's timing, in both
  modes.** A PartDesign datum in Automatic resize mode sizes itself to
  the visible content of its container and writes the result into its
  own `Length`/`Width` -- App data -- and during a load it asked only
  from its own `updateData`, when the features restored after it had no
  visual yet. A plane saved at 312 x 11.9 opened at 150 x 10 eagerly and
  175 x 50 progressively; the Body's box, and the origin sized over the
  datums, followed. Neither was the saved size, so the "derived size"
  entry below was partly a defect. `finishRestoring` now queues the
  datum, and one deferred pass sizes every queued datum once no document
  restores or recomputes, then re-sizes every origin enclosing it,
  innermost first (`a112b87e67`, `656837b508`; before the second, a
  Part's origin over the Body came out 50 in six opens and 60 in four).
- **An unbounded shape's box outlived its build.** The same file's other
  copy has a SubShapeBinder of a Part's origin plane -- an infinite face,
  drawn as a +-50 patch -- with a datum plane attached to it.
  `ViewProviderPartExt::_getBoundingBox` answers from the shape while the
  visual is unbuilt (so a bounds question never tessellates), which for
  that face is +-1e100; the bounding-box cache is cleared only by
  property changes, and the drain's build makes none, so after a
  progressive open the binder still said +-1e100 and the datum refused to
  size over it. An unbounded shape now takes the building path
  (`b7fea11c63`).
- **The pixel-only files were an overwrite of the file's tessellation
  settings, not the ladder's timing** (the first reading here, that the
  level ladder had not refined yet, was wrong: the coordinate counts
  hold still from the load through 30 s idle at the capture camera, two
  eager opens agree exactly, and progressive differs the same way every
  time). The shape-instancing gate re-runs
  `ViewProviderPartExt::reload()` on every Part view provider whenever a
  view's renderer attaches or goes away, and `reload()` wrote the
  Deviation and AngularDeflection preferences (0.2, 28.65 deg) into
  every object whose own values differed. karniz_gostinaya saves 0.5 and
  5 deg per object: eagerly the overwrite came before the exact mesh
  (a Pocket 4342 coordinates), progressively after it, and the mesher
  keeps a finer mesh it finds resident (8616); the first open of a
  process happened to keep the file's values. Opening a document -- any view
  opening or closing -- silently replaced the user's per-object
  settings. Now only a change of a tessellation preference writes them;
  the gate rebuilds a visual whose representation (instanced or flat)
  no longer matches it, which the overwrite had been standing in for:
  an object already at the preference values kept the instanced build
  under plain Coin. The minimum preferences still bound a file's values
  where the mesh is made (a cylinder saved at 0.001 / 0.5 deg meshes
  exactly as one at the minimum). Test: `part-tessellation-reload.py`
  (4 of 7 fail before). karniz now agrees across all three opens; analoy
  keeps a 0.1% residue (a sketch at 86754 against 86661 coordinates),
  not chased.
- **Record order after a migration.** A visible origin whose axes a
  2021 build saved at its planes' size (27; the current rule draws them
  1.5 times longer) opened at 40.5 eagerly and 27 progressively. Eagerly,
  `App::Origin::onDocumentRestored()` migrates the origin point into
  `OriginFeatures` during `App::Document::afterRestore`, AFTER
  GuiDocument.xml was read, and that update re-applies the rule over the
  axes' records; the drain folded the same update into its phase-two
  sweep, before the records, which then won. A change made after the
  document's records were parked, while the App load still runs and the
  object has no view provider, is now noted by name and replayed in phase
  four just before that object is finished -- `afterRestore()`'s own
  per-object order (`1c12f663cc`; test: `datum-size-after-open.py`, an
  origin saved without its point, progressive 3/3 wrong before).

After it, the whole set again (73 files, two progressive runs each): 69
identical apart from derived sizes; the four left are the tessellation
pixels above (error, analoy, karniz_gostinaya,
InvoluteTemplate_01.04.23) -- no state, claim, colour, box or pick
difference in any file.
- **The hidden Parts' origins were sized while the visuals were still
  being built.** A ChineseWindlass copy's hidden Parts, and the Bodies
  in them, opened with origins of 200 x 270 eagerly and 227 x 284
  progressively -- the same 227 x 284 for different Parts. The origin
  sizing timer, and the automatic datums' pass after it, waited out the
  restore and any recompute but not the visual build that follows a
  progressive drain, so they ran in the middle of it. An unbuilt visual
  answers a bounds question from its shape
  (`ViewProviderPartExt::_getBoundingBox`, above), and a curved shape
  nothing has meshed yet answers the box of its poles: the helix that
  draws 207.8 x 277.5 read 226.97 x 284.27 -- exactly its box without
  a triangulation -- and two Parts whose helices have that same
  unmeshed box (an additive and a subtractive one) got the same wrong
  size. Nothing sized them again once the build was
  done. Both passes now wait for
  `Gui::Application::isBuildingVisuals()` too, through one rule,
  `ViewProviderOriginGroupExtension::sizingMustWait()`. All twelve
  origins of the file now agree across the two modes. Test: the helix
  scene of `datum-size-after-open.py`, which saves the origins at a
  size no sizing computes and watches when the open replaces it (3/3
  progressive opens sized mid-build before the fix). The scene only
  needs the build to be running when the timer fires; whether the
  helix is still unmeshed then is a race with the refine pool, which
  in the user file the helix lost and in a small test file it wins.

**The full rerun after these fixes** (73 files, two progressive runs
each, session 108): 66 identical apart from derived sizes. Of the seven
left, one was new -- a PartDesign `Mirrored` whose box read z 0..16 (its
shape) in some progressive opens and z -31..39 (its built visual) in the
rest and in every eager one: the shortcut's answer, cached while the
visual was parked, outlived the drain's build, which changes no property
and so never cleared the cache. A drain slice that builds anything now
clears it (`0a22d41452`; 10 progressive opens of 10 agree). The built
visual's box is larger than its shape's because the feature is
placed with a rotation: Coin boxes the points in the local frame and
transforms that box, so the world box is the axis-aligned box of a
rotated box -- the local box's eight corners, transformed, give the
reported x -36.31..36.31, z -30.98..38.98 to the hundredth. OCCT
boxes the located shape itself, so the shortcut's answer is tight.
Both are valid bounds; any rotated shape answers the tighter one
before its visual is built and the looser one after. Not a defect. The other six: the legacy origin
axes above (since fixed); line-level tessellation pixels in four files (0.7-2%; in
two of them the two eager opens agree exactly and progressive differs
consistently, so it is the curves' level at capture, not noise); and one
pick cell in one run of one file (a Loft against the Loft beside it),
not confirmed.

**The rerun after session 109's fixes** (the origin sizing, the
tessellation overwrite; 73 files, two progressive runs each): 71
identical. analoy, karniz_gostinaya and InvoluteTemplate agree now; the
two left differ in pixels only, the same way in both progressive runs:
- `error` -- the 0.1% tessellation residue analoy had (a sketch at 86754
  against 86661 coordinates), not chased.
- `FC0.21.1_Lead_Screw_12.12.23`, new in the list, and three defects
  under it:
  - **A LinkStage3 file's colours were read inverted** (since
    `139376f184`, 2026-08-12). A colour's alpha means opacity from
    upstream 1.1 on and a transparency before, and the reader decides by
    the release in the document's ProgramVersion. LinkStage3 numbered its
    builds by date (`2023.131R26244`), which read as release 2023.131 --
    past 1.1 -- so an old-convention list went unconverted and a face
    saved opaque came back fully transparent (Transparency 100). 46 of the
    73 files carry a date version. A year for a major number now reads as
    the old convention (`Base::alphaIsOpacity`, unit test
    `ProgramVersion.aLinkStage3DateIsTransparency`).
  - **An eager open switched a binder's colour mapping off.**
    `ViewProviderSubShapeBinder::onChanged` ends a Map*Color when its
    colour is set, and properties restore in name order, so reading
    ShapeColor after MapFaceColor switched it off; only UseBinderStyle had
    the restore guard. Now none of the four react to a restore or an
    undo (test: `binder-map-color-restore.py`).
  - **An instanced object drew a uniform colour list at the wrong
    transparency** -- not a load defect. With the colours read right, the
    progressive frame showed a Lattice `Populate` (a compound of repeated
    solids, so built instanced) with its faces fully transparent while
    its DiffuseColor and Transparency said opaque; before the fix above
    it was the other way round. The uniform branch of
    `ViewProviderPartExt::applyInstancedFaceColors` gave the Coin
    material the colour's alpha as its transparency, where an alpha is an
    opacity (`Base::Color::transparency()` converts; the flat path and
    the per-instance override materials did). Any uniform list set on an
    instanced object drew inverted, live as much as on open; the user
    file's eager open escaped only because its colours landed before the
    instanced representation was built. Test:
    `instanced-face-transparency.py` (all four claims fail before the
    fix, the eager open included).

**The rerun after session 110's fix** (the instanced colour; 73 files,
two progressive runs each): 71 identical, both Lead Screw files among
them. The two left differ in pixels only:
- `analoy` -- 0.6% in this run, identical in the next, where its two
  eager opens differed from each other: the noise floor.
- `error`, the one from `2022-10-11_w_52240` -- not a load-mode
  difference: **every tessellation parameter depended on the mesh the
  shape already carried.** Its wire compound `layer_1001` (92 B-spline
  edges) is built coarse first and refined in every open; the exact
  mesh was 86569 coordinates in a process's first eager open and in
  every progressive one, 86662 in its later eager opens, 86597 when the
  climb started from rung 0, and all agreed with coarse-first off. The
  deflections -- the display one, the exact one a coarse-first build
  registers for the refine, a rung's, the texture frame -- came from a
  `BRepBndLib::Add` box, and that reads any triangulation or 3D polygon
  the shape holds. A traced open showed the refine meshing at 1.762003
  when the build that armed it boxed the bare geometry and at 1.758533
  when a second coarse build ran over the coarse polygons (their box
  0.2% smaller): 93 more points. OCCT's mesher reuses a resident
  polygon within 10% of the ask and checks no angle, so the display
  rebuild after it (asking 1.758934 off the exact polygons) kept
  whichever came first. The same box keyed the instance table, so one
  leaf TShape could key apart by what had meshed it before. The box
  now comes from the geometry alone (`PartGui::meshingBounds`; a face
  with no surface and an edge with no curve still give their mesh),
  kept per shape in the ladder state because it costs about 13 us a
  face against the mesh box's near nothing -- 16 ms for a 1253-face
  fusion. On the four largest corpus shapes with faces the two boxes
  agree within 4 parts in a million, so ordinary solids keep their
  deflection; loose-pole
  B-splines move. All four opens of the file now agree, the sketch
  beside it too (its 287 against 272 node was the same effect). Test:
  `meshing-bounds-history.py` -- a premeshed and a fresh copy of the
  same B-splines, 810 against 780 points before the fix.
The harness numbered its captures by file name, and the corpus has
seven `error.FCStd`: each overwrote the last one's images, so the
failing file's pictures showed a file that passed. Numbered now.

**The rerun after the meshing box fix** (73 files, two progressive runs
each): all 73 identical, no eager-against-eager noise either.

Two files did not open at all within 400 s, eagerly
or progressively (`LS3_Lead_Screw_Mach_02_12.12.23`, and its sibling was
skipped with it): stuck in `BRepTools::Read` under
`App::Document::restoreDeferredFile`, a load defect of its own -- an OCCT
8.0 regression: `GeomTools::GetReal` reads a real through a 32-byte buffer
(256 in 7.7.2), and the file's datum line wrote its +-2e100 range in fixed
notation, 101 digits; split, every later field came from the wrong token
and the reader spun forever. Fixed in the OCCT fork (`310bfaf34f`,
`LinkVibe-801`); both files now open. Test: Part
`RegressionTests.test_read_brep_with_a_long_fixed_notation_real`, which
hangs before the fix. The 9.5 s eager and 5.9 s progressive first
measured for them was not the files: it was each being the first
document a fresh test process opened, and `gui-test.sh` gives every run
an empty `XDG_CACHE_HOME`, so Mesa (llvmpipe under xvfb) compiled every
shader variant from nothing. Opened second in a process they take
0.47-0.58 s, App-only 0.3 s; opened first in launches that share one
cache directory, 8.4, 6.1, 2.9 and then 0.85 s, the cache filling with
the variants their materials need. What stays on a warm cache is about
1 s of first render -- programs the startup warm-up's one frame does
not reach -- on llvmpipe, which says little about a GPU.

**Not defects, and why the test does not judge them:**
- a coarse first tessellation (27 against 62 points on a circle) that the
  level ladder refines on idle -- waited out;
- the size of a datum or an origin feature: derived from the content at
  whatever moment it was last asked for (272 against 286 on the same
  file) -- reported apart, not judged. Partly a defect after all: an
  automatic datum was sized mid-load in BOTH modes, and is now sized once
  the load is over (above); what still differs is listed there;
- an eager open and another eager open differing in a frame's pixels
  (transparency) -- the noise floor.

In the generated corpus those derived sizes come out the same every time,
so there the test does judge them (it caught (4)); only for files handed
in with `PL_FILES` are they reported apart. A non-finite bounding box is
judged everywhere.

**The harness's own traps**, each of which once made a run lie:
the first render of a process took 4.9 s inside ONE `processEvents`
call, and a wait that measured only elapsed time ended right after it,
with the post-load sizing timers still pending -- the first eager open
agreed with no later one and was set aside as noise; the wait now also
runs until 1 s after the last slow event call.
`vp.isShow()` does not exist, and one `try` around all the reads blanked
every field after it (show, showable, switch, mode, claims) -- the first
runs compared none of them; each read is now guarded on its own. A file
written by an older release asks, modally, whether to recompute for
migration, which held a run for its whole timeout: the test turns
`WarnRecomputeOnRestore` off and dismisses any other modal dialog on a
timer, logging it.

**Test.** `tests/gui/progressive-load-diff.py` (registered,
`GuiProgressiveLoadDiff_tests_run`, ~5 min): 12 corpus files x 2
progressive runs, 7 operations during the drain, 3 closes during it and a
whole reopen after -- 23/23; with a fusion whose input the user showed
again (the ordering finding), 24/24; with a datum plane over a binder of
an origin plane (`s_unbounded`), 25/25, and that scene FAILS on the old
order (the input hidden, 35 pick cells hitting the fusion). Before-state (the six fixes above reverted,
(1) kept): body FAILS (the Origin's 16 scene paths, 6 origin sizes, the
sketch's internal view), sketch FAILS (the stray origin point), image
FAILS (the overwritten size, and the eager frame untextured), and the
hide during the drain FAILS (two Parts and a Link visible again); the
other 19 pass both ways. (4)'s guard alone was seen before it existed:
the size fix without it gave a user file's origin planes Size = inf.
The datum sizing has its own test, `tests/gui/datum-size-after-open.py`
(`GuiDatumSizeAfterOpen`, 27 checks): a plane saved against a Pad drawn
after it (10 x 10 eagerly and 50 x 50 progressively before the fix,
60 x 30 saved), and a plane over an unbounded binder with 300 boxes
ahead of it in the visual queue, its box asked during the drain, five
opens per mode (one progressive open in three failed before).
