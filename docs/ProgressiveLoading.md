# Progressive Loading

This document is the map of the progressive-loading design as a whole:
what principle every loading path follows, how the pieces divide the
work, and where each piece's detailed design lives. The deep documents
are [DocumentLoad.md](DocumentLoad.md) (opening an `.FCStd`),
[IncrementalPublish.md](IncrementalPublish.md) (the render-cache delta
that keeps a growing scene cheap), and
[SceneStreaming.md](SceneStreaming.md) (the remote viewer). This one
exists so that the next loading-shaped problem starts from the shared
architecture instead of rediscovering it.

## 1. The principle

A load used to be a wall: nothing appeared until everything was read,
parsed, built and drawn. The MiSTer reference document (18142 objects,
17k shape entries, 272M triangles at full tessellation) opened in 32.7
seconds, all of it spent before the user saw anything.

Progressive loading replaces the wall with one rule applied at every
layer:

> **Do now only what is needed for the next thing the user sees;
> park everything else where it can be found again; pay for the
> remainder in budgeted slices behind a live window — and converge to
> a state indistinguishable from the eager path.**

Four corollaries recur in every subsystem, and are worth naming
because they are the design:

- **Parking must be findable and cancelable.** Whatever is deferred is
  recorded by *name* (never by pointer) with enough index to serve it
  later on demand; anything that would overwrite a parked value
  cancels it instead of racing it.
- **Every consumer faults in what it touches.** Deferral is invisible
  to code that asks: accessors serve the parked value on first real
  use. Correctness never depends on the background fill having run.
- **Slices are budgeted by wall clock, not by count.** Background work
  runs on the event loop in time-boxed slices
  (`ProgressiveLoadBudgetMS`), so frame delivery and input never lose
  to the backlog.
- **Convergence is gated, not assumed.** The end state must be
  byte-identical (property dumps) and pixel-identical (converged
  frame, saved camera) to the eager load. Every stage shipped only
  after such a gate on the real GPU.

## 2. The document open, end to end

Opening an `.FCStd` now runs the following pipeline. Stages 1–4 are
the blocking part (the "window" the user waits for); stages 5–7 run
behind the live window.

```
 1. zip central directory  ──  Base::ZipFileReader: index once, every
    (ArchiveRandomAccess)      entry its own positioned stream
 2. Document.xml           ──  object creation + property data; compact
    (SaveSchemaVersion 5)      format elides class defaults per class
 3. archive walk           ──  registered entries served in registration
                               order; shape entries PARKED, not read
    (DeferShapeLoad)
 4. GuiDocument.xml        ──  captured whole, not applied; window opens
    (deferred VP restore)      here — this is the end of the wait
 ─────────────────────────────────────────────────────────────────────
 5. drain, phase zero      ──  Document::serveDeferredFiles(): parked
                               shapes read in budgeted slices, before
                               any consumer exists
 6. drain, phases 1..n     ──  view providers created (one pass), then
                               their records applied in slices
 7. progressive visual fill──  visuals built coarse-first in slices,
                               refined as the budget allows
```

Each stage's design and measurements are in
[DocumentLoad.md](DocumentLoad.md): the format work in §6–§8 and §12,
the deferred view providers in §13, the random-access archive and the
deferred shapes in §14. The net effect on MiSTer: open window
32.7s → **~2.0–2.3s**, converged frame 0 px against the eager load,
peak RSS no higher and ~1.1GB lower during the window.

Two ordering rules in this pipeline were bought with real debugging
time and must survive refactors:

- **Shapes serve before consumers exist** (stage 5 before 6). Serving
  lazily from inside record application works but replays a
  per-object change notification whose GUI fan-out costs minutes at
  17k objects; served in phase zero, no notification is needed at
  all.
- **The drain reproduces the eager order** — every record applied in
  stage 6 reads a shape that is already there, so the visual dynamics
  (colors, bounding boxes, selection) are byte-for-byte those of the
  eager load. Deferral changes *when*, never *what*.

**Every stage is per document.** Documents load one at a time only by
accident: a second file opened while the first still drains, a
reference document pulled in by a link, a reload. Each stage therefore
keys its state by document and decides per document — the serve
backlog is the document's own (`Document::hasDeferredFiles()`), the
view provider drain belongs to its `Gui::Document`, and the visual
queue is one queue per document, walked with the slice budget split
evenly between the documents that can use it.

That was not free to learn. The visual queue was originally one global
deque, and every decision in a slice — whether to build at all,
whether to run a serve phase — was made from whichever object happened
to be at its head. One document still restoring therefore stalled
every other document's fill, a document with parked shapes but nothing
queued got no serve phase at all (its archive index stayed open and
its entries faulted in one at a time forever), and the counters mixed
loads so the completion line described neither. The tree had the same
shape of bug one level up: `TreeWidget::onUpdateStatus` froze item
updates for *all* documents while *any* one drained. Both now hold
back exactly the draining document and let the rest proceed.

The console (`FreeCADCmd`) runs the same stages 1–3 and then relies
purely on per-access fault-in: a headless process never pays for
shapes nobody asks for, and a save asks for all of them while the
source archive is still on disk.

## 3. Progressive import

STEP import is the same principle pointed at creation instead of
restore: objects enter the document incrementally while the view stays
live, stand-in boxes and pooled coarse tessellations give every object
an immediate visual, and refinement follows behind. What makes this
affordable is that a frame costs what the *new* object costs, not what
the scene costs — the render-cache publish path is a delta, spliced
per changed object. That delta design, its measurements, and the
instancing/mesh-reuse registries it leans on are
[IncrementalPublish.md](IncrementalPublish.md); the shared-leaf
instancing architecture underneath is
[TShapeRenderCache.md](TShapeRenderCache.md).

The two halves of that are separable, and the IFC import takes only the
second. Its product loop is not sliced -- it holds the thread and pumps
events from inside `Base.ProgressIndicator.next()` -- but a loop that
pumps has input to deliver, and the only reason it was unusable is that
the indicator swallowed it. `Gui.setLiveImport(doc, True)` hands the
view back for the duration: `Gui::LiveViewInteraction` makes both input
filters except mouse events aimed at a 3D view, `WaitCursorRestorer`
lifts the cursor, and `App::Document::LiveImport` makes
`Gui::Command::invoke()` refuse every `AlterDoc` command with "The
document is busy importing, please wait...". Live view, inert document.
The exception stays narrow on purpose: keys are still blocked so Escape
cancels, and context menus stay shut so a right-drag orbits rather than
offering commands.

It is worth being clear about what that does *not* buy. Interaction is
sampled at the loop's pumping interval, so a single slow product blocks
for as long as its geometry takes -- one King wall was once 157 s inside
a single OCCT boolean. Slicing the loop would not fix that either; only
moving the geometry off the thread would
([ComputeBoundaries.md](ComputeBoundaries.md)).

That sampling interval is worth owning explicitly. Left to itself the
loop only reaches the event loop through `Base.ProgressIndicator.next()`,
which pumps on the bar's own 200 ms *update* throttle -- a repaint
cadence, chosen for what a bar redraw costs, standing in for an input
cadence. Measured on a 144-product IFC file in the GUI, that is nine
turns of the event loop in 2.55 s: about three and a half a second to
someone orbiting the model. `Gui.pumpLiveImport()` offers a turn per
item instead, throttled on `ViewParams::LiveImportPumpInterval`
(50 ms), and the same import gives 40 turns in 2.70 s -- 4.4x the
interaction for 6% of the wall clock. Pumping at every item (interval 0)
reaches 124 turns but costs 25%, which is what the default is chosen
against. The pump is a no-op unless `setLiveImport()` is in effect: an
import that did not ask for a live view never has events run behind its
back.

Two things follow from a loop the user can reach. The document can be
closed under it, so each item checks that the document it started in is
still open rather than building the rest of the file into whatever is
active next. And Escape now means something the importer can act on:
`Base.ProgressIndicator.next()` raises `Base.FreeCADAbort` rather than a
bare `RuntimeError`, which is the difference between "the user pressed
Escape" and "this step failed", and which the interpreter already turns
back into a `Base::AbortException` that `Command::invoke()` swallows
without an error dialog. The IFC importer catches it, stops making
products, and still applies the layers, relationships and colours that
belong to the products it did make -- a smaller model rather than an
unrelated one, inside the caller's transaction, so one undo takes the
whole import back.

## 4. The remote viewer

The browser/mobile viewer receives the scene as a content-addressed
delta stream and applies it progressively through a coarse-to-fine
ladder with its own budgets — the same corollaries (parking by key,
budgeted application, gated convergence) negotiated over a wire. That
design is [SceneStreaming.md](SceneStreaming.md) and is deliberately
not duplicated here.

## 5. Progress: counters that workers bump, a UI that polls

Progressive loading multiplies the number of things that are "in
progress" at once — an archive walk, a shape serve, a visual fill,
soon parallel recomputes — and the old indicator was built for exactly
one. Its costs were also the wrong shape: a push per item through a
mutex into an event-pumping GUI update, which at one point made the
progress indicator itself **17× the cost of the work it was
reporting** (DocumentLoad.md §14). The reporting layer was therefore
redesigned around the opposite flow:

> **Workers bump lock-free counters; the UI polls them on its own
> cadence.**

The pieces, all in `Base/Sequencer.{h,cpp}` and
`Gui/ProgressBar.{h,cpp}`:

- **`Base::SequencerLauncher` is cheap and thread-safe to tick.** Its
  counters are atomics; a worker-thread `next()` that does not have to
  drive the indicator (another sequence is on top, or the indicator is
  poll-driven) is a relaxed increment and nothing else. One launcher
  may be shared by many threads — the intended shape for a future
  parallel recompute (one launcher for the job, every worker ticking
  it), and for out-of-process geometry servers (a client-side proxy
  launcher fed by protocol messages).
- **`Base::SequencerManager` snapshots all running sequences.** One
  entry per thread-hierarchy: the outermost launcher is the root,
  nested launchers follow with a depth, capped at a configurable
  number of levels (`ProgressDetailLevels`, default 5). Only roots
  feed the consolidated total, so nesting can never inflate progress;
  parallel roots sum. Consolidation work happens in the reader, at
  snapshot time — workers never pay for it.

  Two later rules decide *which* root the bar shows, and both were
  bought by a save that reported the wrong sequence entirely. Nesting is
  inferred from registration order, which is right for a sequence
  started inside another's call stack and wrong for one that merely
  *overlaps* a sliced sequence -- a `KeepInteractive` launcher lives
  across returns to the event loop, so a save starting while the
  progressive fill still drained was bucketed as the fill's child and
  vanished from the bar. A sequence that is a job of its own says so
  (`SequencerLauncher::setStandalone()`) and gets its own bucket; the
  rule stays narrow deliberately, because a per-item indicator really is
  nested inside the drain slice that created it, and must keep being
  drawn that way. Then **when anything blocking is running, only
  blocking roots are counted**: what the user waits on is whatever holds
  the thread, and the background sequences beside it stay visible in the
  detail popup without diluting the number. The snapshot names its own
  `lead` so the status text cannot describe one sequence while the bar
  counts another -- which is exactly what the save did, reading
  `Saving document...` over the fill's numbers, advertising three and a
  half minutes remaining for thirty seconds of work.
- **The status bar polls.** A 200ms timer drives the bar's range and
  value from the consolidated snapshot; push-path writes are
  suppressed while it runs. Hovering the bar opens a live popup: one
  ticking progress bar per sequence, nested rows indented under their
  root, a Total row when roots run in parallel.
- **Per-item sequences are free.** Code that stacks a launcher per
  work item — one brep-import indicator per served shape — no longer
  restarts the top indicator on construction, and the GUI teardown is
  debounced behind a 200ms grace period, so back-to-back start/stop
  cycles reuse the engaged indicator instead of thrashing the wait
  cursor, event filter and status bar. This is what allowed the OCCT
  brep-read indicator to come back on the restore path: the serve
  phase owns a `Loading shapes...` sequence across its slices, and a
  read long enough to matter shows its OCCT scope ("Surfaces...",
  "Shapes") nested beneath it. Reads shorter than the abort-check
  window (500ms) never surface — by design; their progress *is* the
  serve counter.
- **A sliced sequence reports without taking the window.** "Started on
  the main thread" has always meant "owns the GUI thread until it
  ends", and the indicator answers it by grabbing input: wait cursor,
  the application event filter swallowing clicks and keys, and the 3D
  viewer's own filter dropping navigation. A sequence that spans
  event-loop turns is the opposite case — the `Loading shapes...`
  sequence lives across every slice of the drain, and the whole point
  of the drain is that the window stays usable — so it is launched
  `SequencerLauncher::KeepInteractive`. The blocking answer also comes
  from the launcher's *owner* thread rather than from whoever promoted
  it, or a worker's sequence promoted by the main thread would claim
  the main thread's input.

The **save** reports through the same layer, and had to be taught to:
a 13758-object building took over 200 seconds to write with the GUI
thread held and the status bar empty, and the compression is only some
15 of those seconds -- the rest is serialising the objects and writing
their shapes. `App::Document::saveToFile()` owns **one** launcher for
the whole save and lends it to the writer (`Base::Writer::setProgress()`)
for the phases the writer owns. One launcher, because reporting has two
different notions of "the current sequence" and a save straddled them:
the *top* launcher drives the status text and the remaining-time
estimate, while the consolidated bar sums the *roots* -- the outermost
launcher per thread. A save of a freshly opened document begins by
flushing the parked shapes, which on MiSTer is 17058 entries and some 25
of a 30 second save, and the drain's own sequence was still the root
across all of it. So the text read `Saving document...` while the
numbers and the estimate were the drain's, and a 30 second save
advertised three and a half minutes remaining. The flush is now inside
the save's sequence, and `flushDeferredFiles()` retires whatever drain
sequence a slice left running -- it is about to take that backlog away
anyway.

Its phases each restate the total as they learn their own size: the
flush and the five passes over the objects are known when the save
starts, the blob entries when the table has been planned, and the file
loop reads its own list, which is not built until the objects are
written and which grows while it is walked. Measured on MiSTer: 17058
flushed + 5 x 18142 object steps + 5204 blob entries = 112972, over a
29 s save that splits as

    collect 0.17s   beforeSave 23.06s   hasher ~0   document props ~0
    writeObjects 0.39s (deps 0.035 / headers 0.020 / data 0.32)
    blobs 2.72s   files 0.0s

Where a save's time goes is not where anyone would look for it, and
finding out took four measured attempts. The file loop -- the obvious
place to put a save indicator -- writes **zero** entries on this
document; every shape goes through the blob table. `writeObjects()`, the
next obvious candidate and the one that actually emits the XML, is
**0.4 seconds**. Nearly the whole save is the `beforeSave()` pass in
`Document::Save()`, where each object settles what it is about to write
and a document-wide store collects it: 23 of 29 seconds, 79% of the
save, in a loop that reported nothing. Guessing which phase to
instrument produced a bar that was still for two thirds of every save;
only splitting the wall clock across the phases found it.

This launcher stays `BlockInput` on purpose -- unlike the drain, a save
really does own the document until it ends.

Two reporting facts that cost gate iterations, recorded so they are
not rediscovered: in OCCT ≥ 7.5 an indicator's text is the *scope
name*, not the legacy "Reading BREP file..." string; and a
converged-scene probe cannot see a silent phase — gate on the
pipeline's own completion lines (`progressive load <doc>: N visuals
...`, `deferred serve slice <doc>: ... 0 pending`), not on frame
agreement. Both name their document: every stage of the pipeline is
per document, and two loads overlap often enough that an unlabelled
line describes neither.

## 6. Parameters

| Parameter | Group | Default | Governs |
|---|---|---|---|
| `ArchiveRandomAccess` | Document | on | central-directory archive reader (stage 1) |
| `SaveSchemaVersion` | per document | 5 (a restored document: its file's own) | 5 is the fork format: default elision, shared blobs, shape store (stage 2); 4 is upstream's |
| `DeferShapeLoad` | Document | **on** | park shape entries, serve on demand (stages 3, 5) |
| `ProgressiveLoadBudgetMS` | View/Render | slice budget | drain and fill slice length (stages 5–7) |
| `ProgressDetailLevels` | General | 5 | nesting depth shown in the progress popup |

Every stage keeps a fallback: the forward-only zip reader, eager shape
restore (`DeferShapeLoad` off), eager view-provider restore, and the
schema-versioned format all remain selectable, and old files load
unchanged.

## 7. Invariants

1. **Correctness never waits for the background.** Any code path that
   touches a deferred value serves it first; the fill is an
   optimization of *when*, never a precondition of *what*.
2. **Parked state is keyed by name and cancelable**; a writer cancels
   the parked value rather than racing it, and a deleted owner
   invalidates its entry instead of dangling.
3. **The archive must not be rewritten externally while entries are
   parked** — the one durability trade of on-demand reading
   (DocumentLoad.md §14). Our own save is safe because it says so:
   `Document::saveToFile()` calls `flushDeferredFiles()` before writing.
   Leaving it to the property accessors is not enough — a `Transient`
   property is skipped by the save and would carry its parked entry
   across the rename.
4. **Slices never exceed their budget by design**, only by the
   granularity of one item; anything long inside an item (a giant
   brep) reports through the progress layer instead of blocking
   silently.
5. **Deferred work may not modify the document.** What runs behind the
   window is the file's own record being replayed, and a load that
   ends with the document needing a recompute has changed *what*, not
   just *when*. The eager path was owed nothing here because
   `afterRestore()` purged each object before announcing it and a
   recompute purges its own; work replayed later lands past both, so
   the drain states the rule instead of inheriting it —
   `App::Document::RestoreDrainGuard`, and one line naming whatever
   tried (DocumentLoad.md §13).
6. **Every behavior change gates against the eager path** on the real
   GPU: 0 property diffs, 0 px at convergence with the saved camera,
   and the pipeline's own completion lines as the arbiter of "done".
7. **No stage may make one document's progress depend on another's.**
   State is keyed by document, gates are asked of the document being
   worked on, and a shared budget is split between the documents that
   can use it rather than handed to whichever is first.

## 8. Where this goes next

The same skeleton is expected to carry: parallel shape reads in the
serve phase (every archive entry already owns its file handle),
worker-thread and out-of-process recomputes reporting through shared
launchers ([ComputeBoundaries.md](ComputeBoundaries.md)), and lazy
blob fetch through the same parked-entry index
([FileBlobsManager.md](FileBlobsManager.md)). Each arrives as a new
producer for an existing stage, not a new architecture.
