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
  under load when the harness restaged).
- **View provider properties**, which no chokepoint above can see: every
  one is in App, and a `ViewProvider` is a separate `PropertyContainer`.
  This is right rather than merely convenient -- they are presentation,
  the class the live view exists to keep usable, and `RestoreDrainGuard`
  already draws the same line from the other side ("replayed view work
  must not modify the document").
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
