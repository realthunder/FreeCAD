# An append-only transaction log, and undo as a forward operation

Status (2026-09-22): **design complete; implementation starts with
phase 0.** Development is on branch `Transaction`. Sections 1 to 7 are the idea as recorded on 2026-09-18 and
are kept as written; the design that answers them starts at section 8,
and section 8.1 is the summary; sections 16 and 17 (versions, branches)
were decided on 2026-09-22 and section 18 lists what the audit of the
same day left open. Phase 0 (section 15) is a measurement and comes
before any store is written.

## 1. The idea (user, 2026-09-18)

Follow Onshape's model, and git's:

- **Record every transaction.** The history is a log, not a stack.
- **Undo is a new transaction** that applies the reverse of an earlier
  one, appended to the same log. It is not a pop, and it does not erase
  anything.
- **Persist the log**, in the document's transient directory.
- **Possibly use git as the store**, rather than inventing one.

The attraction is that undo stops being a special mode the document is
put into and becomes an ordinary edit. Everything that is true of a
normal change -- it can be recorded, replayed, streamed to a client,
attributed to whoever made it -- becomes true of undo for free.

## 2. Where the current model actually stands

Measured, 2026-09-18, so the idea is discussed against the code rather
than against an impression of it.

- **One open transaction per document.** `DocumentP::activeUndoTransaction`
  is a single pointer, and `Document::_openTransaction` begins with
  `if (d->activeUndoTransaction) { _commitTransaction(true); }`. Opening a
  second transaction on a document force-commits the first. Upstream's
  `f4665aa7b5` ("Core: support multiple active transactions") does not
  change this -- its plurality is across *documents*, not within one.
- **The active transaction is process-global.** `_activeTransactionID` and
  `_activeTransactionGuard` are `App::Application` members, so a process
  serving several documents shares one.
- **A transaction stores whole old values.** `TransactionObject::setProperty`
  does `data.property = pcProp->Copy()` -- a full copy of the property's
  value before the change, per changed property. For a sketch that is the
  entire geometry list on a one-vertex move.
- **The stack is bounded and in memory.** `UndoMaxStackSize` defaults to 20.
  `Transaction` and `TransactionObject` inherit `Base::Persistence`, but
  `Save()` and `Restore()` are `assert(0)` stubs, so nothing is ever
  written. The history dies with the session.
- **The memory limit does not work.** `TransactionObject::getMemSize()`
  returns `0`, so `UndoMemSize` accounts nothing. Worth knowing before
  anyone cites it as a reason the log would be too big: the current model
  already pays the copy cost and simply throws the copies away.
- **A transient directory already exists.** `Document::TransientDir` and
  `getTransientDirectoryName(uuid, filename)`, already used for blobs.

So the copy cost the idea is often assumed to introduce is mostly already
being paid. What is new is keeping the copies, ordering them, and being
able to invert them.

## 3. What would change

- **Undo becomes append-only.** Today undo pops a transaction and pushes
  it onto a redo stack; a new edit after an undo discards the redo stack.
  Under the log, nothing is discarded -- the "discarded" branch is simply
  no longer the tip. That is the git analogy doing real work, and it is
  where redo stops being a separate mechanism.
- **Undo becomes attributable and streamable.** A shared session
  (`docs/ThinClient.md` 8.11) has several clients on one undo stack. "Undo
  someone else's change" is ill-defined against a stack and well-defined
  against a log: it is a new forward transaction, and it streams to every
  client like any other. This is the strongest argument for the idea in
  this fork, and it is the reason Onshape can do it at all.
- **History could outlive the session**, if the transient directory is
  kept. The user's framing says transient, which makes the immediate goal
  *unbounded undo within a session* rather than durable history. Those are
  different features and it is worth being deliberate about which one is
  being built.

## 4. Why this is the multi-client substrate (user, 2026-09-18)

The user's framing, and it reaches further than undo: **this is what would
eventually unlock simultaneous editing by several clients.**

That is not a new argument bolted on -- it answers the one
`docs/ThinClient.md` 8.11 already made. The reason the fork chose shared
sessions (all clients mirroring one desktop session) over per-client
sessions was explicitly *not* view plumbing:

> the parts that make it hard are not view plumbing but the document:
> per-user undo over one history, and merging two sessions' writes into
> one sketch whose geometry is written back as whole arrays.

Both of those are log problems.

- **Per-user undo over one history** is exactly what undo-as-a-forward-
  transaction gives. Against a stack, "undo my change when someone else
  has committed after me" has no meaning. Against a log it is an ordinary
  append, and it streams to every client like any other edit.
- **Merging two sessions' writes** is the harder half, and it is what
  decides the unit question in section 5 -- and decides it hard.

**A property-delta log gives undo. It does not give merge.** Two clients
each writing the whole `Geometry` array is last-writer-wins, whatever the
store underneath. Merge needs *operations* -- "add a line from A to B",
"move vertex 3 of Sketch001" -- that can be transformed or rebased against
concurrent ones. So if concurrent editing is the goal, the unit is
operations, and that has to be settled before anything is built:
retrofitting operations onto a property log is a rewrite, not a
refinement.

Two things make this more reachable here than it sounds.

**FreeCAD already emits an operation log.** `MacroManager::addLine` is
called for every `Gui::cmdAppObjectArgs` / `doCommand`, and its `LineType`
already separates `App` ("effects only the document and Application") from
`Gui`. Every GUI-initiated document change is already recorded, at
operation granularity, in a form complete enough to rebuild the document.
It is not a sound state log -- it refers to objects by name, depends on
application state, is not invertible, and has no ordering guarantee across
sessions -- but it is strong evidence that the operation level is
reachable, and it is the obvious prototyping shortcut.

**This fork already has the identity machinery that operation transform
needs.** An operation has to name what it acts on, and that name has to
survive someone else's concurrent edit. That is the topological naming
problem, and it is the thing this fork solved first: the element map,
`StringHasher`, and -- inside a sketch specifically --
`SketchGeometryExtension::getId()`, a persistent geometry id independent
of the array index. Most kernels cannot say "this edge" across an edit at
all; here it is already the normal way of speaking.

**One trap the git analogy sets.** Live co-editing and branch/merge are
two different machines. Git *conflicts*; it does not transform. A store
modelled on git is a reasonable idea; a concurrency model modelled on
git's merge probably is not, because the interesting case is two people in
the same sketch at the same time, which is an ordering problem rather than
a three-way-merge problem. What Onshape actually does for each -- live
co-editing versus its branching workflow -- is the Onshape question in section 5, and the
answer likely differs between them.

## 5. Open questions

These are the design. They were open on 2026-09-18; each is ruled on in
section 8.1, which points at the section that argues it.

1. **What is a transaction made of?** Today: whole old property values.
   Inversion wants a delta in both directions. Property-level deltas are
   easy for scalars and hard for the things that matter here -- geometry
   lists, constraint lists, `TopoShape`, the element map -- which are
   written back as whole arrays. Recording a one-vertex move as "the whole
   geometry list, twice" is the naive answer and probably the wrong one.
2. **What is the unit -- a property write, or a user operation?** A
   property log is mechanical and complete. An operation log ("move vertex
   3 of Sketch001") is small, invertible by construction, and replayable,
   but requires every operation to be expressible and every feature to
   cooperate. Onshape is, as far as I know, closer to the second; that
   needs checking rather than assuming (see 6).
3. **Inputs only, or outputs too?** A document's state is inputs plus
   whatever recompute produced. Logging only inputs is small but makes
   restoring a state a *replay*, which needs recompute to be deterministic
   -- and OCCT is not obviously deterministic across versions or builds.
   Logging outputs is correct and large. A hybrid (inputs always, outputs
   cached) is the likely answer and needs its invalidation rule written
   down.
4. **Structural changes.** Object creation and deletion, renaming,
   re-linking, document-level operations, and cross-document transactions
   sharing an id. Inverting "delete" means keeping the object; inverting
   "rename" interacts with the element map and with links held by name.
5. **Is git the store, or the analogy?** Content addressing and dedup come
   free and suit whole-array values well -- two sketch states differing by
   one vertex share nothing at the file level but everything at the blob
   level only if the serialisation is stable and chunked. Against: a
   FreeCAD document is one zipped XML plus blobs, which is a single opaque
   file to git; making git useful means choosing a *different* on-disk
   shape for the log than the document's own. Also: process cost per
   transaction, a repo per open document in a transient directory, and
   what happens on crash. Worth prototyping the store separately from the
   undo semantics -- the two decisions are independent.
6. **What Onshape actually does.** Recorded here as the stated inspiration,
   not as a description. Before committing to a model, read up on how its
   history, branching and merging actually work, and on what it refuses to
   do -- the refusals are usually the load-bearing part.
7. **Interaction with out-of-process geometry** (`docs/ComputeBoundaries.md`).
   If OCCT work moves to another process, the log is the obvious sync unit
   between them, which argues for the operation-level form in 2.
8. **Migration.** The current undo has 20 steps, no persistence and a
   working-enough feel. Any replacement has to be at least as fast for the
   common case (one property, one object) or it will be felt on every
   edit.

## 6. What this is not

Not a fix for the case that prompted the discussion: a modeless task
dialog holding a transaction while the user edits a property elsewhere,
which force-commits the dialog's transaction and splits its edits across
two undo steps. That is a smaller, separate problem. The narrow fix is to
hold a transaction lock for the dialog's lifetime rather than its call
stack (`App::TransactionLocker` exists upstream; the fork's
`AutoTransaction::setEnable(false)` in `Control::showDialog` is scoped to
the call stack and does not cover it). If that case starts biting, fix it
there rather than waiting for this.

Also not a reason to take upstream's `f4665aa7b5`. That was evaluated on
2026-09-18 and declined for the Sketcher port
(`docs/SketcherPort.md` section 7): it does not fix the splitting, it
removes a guard the fork currently has, and it costs 245 files.

## 7. Related

- `docs/ThinClient.md` 8.11, 8.12 -- the shared-session model, one undo
  stack per document, and the process-global chrome inventory.
- `docs/ComputeBoundaries.md` -- out-of-process geometry.
- `docs/FileBlobsManager.md` -- the existing transient-directory store.

## 8. Design (2026-09-21)

This section answers section 5. It was written after a code survey of
the current transaction machinery and a web survey of stores and
precedents (section 19). Where it overrules something said earlier in
this document, it says so.

### 8.1 The rulings in one place

| Question (section 5) | Ruling |
| --- | --- |
| 1. What is a transaction made of | Ops carrying a *before* and an *after* value reference. Values are content-addressed, so undo/redo store nothing new. |
| 2. Property write or user operation | Property level now: create, remove, set, plus dynamic-property add/remove. The envelope reserves room for semantic ops and delta encodings; the macro lines ride along as an annotation. |
| 3. Inputs or outputs | Inputs always. A value written by an object's own `execute()` is *derived* and goes to an evictable cache tier, never to the op log; it reaches durable storage only inside a named version's snapshot (16.1). An import carries its source file, which makes the shapes it assigns derived too (10.1). |
| 4. Structural changes | Create and remove carry a whole-object snapshot. The internal name and id never change, so rename is a `Label` set. |
| 5. Git | Analogy only. The store is **SQLite** (decided 2026-09-22), behind an interface. |
| 6. Onshape | **The blueprint** (decided 2026-09-22): versions as immutable snapshots, external links pinned to a version, history per document. Section 16. |
| 7. Out-of-process geometry | The recompute pseudo-transaction (section 11) is the seam. |
| 8. Migration | The in-memory `Transaction` stays as the hot path; the log is written at commit. Section 14. |
| -- Versions, branches | Not asked in section 5; decided 2026-09-22. Sections 16 and 17. |

### 8.2 What the old Transaction becomes

The user's framing (2026-09-21): the basic, atomic operations are object
creation, object removal and property setting, and that is all. The old
`Transaction` survives as the *grouping and naming* of those operations,
for browsing.

The code already has this shape. `Transaction` has exactly four entry
points -- `addObjectNew`, `addObjectDel`, `addObjectChange`,
`addOrRemoveProperty` (`src/App/Transactions.h:84-88`) -- and an ordered
list of `TransactionObject`s. `Transaction::Save` and `Restore` are
`assert(0)` stubs. **The log is those stubs implemented against a
store.** That is the whole of the core change, and it is why this can be
built without touching a single feature or command.

One atom has to be added to the user's three: **dynamic property add and
remove**. It is structural, it is already the fourth hook, and a `set`
on a property the replayer has never heard of cannot be applied.

## 9. The record model

### 9.1 Granularity: every change, at transaction resolution

"Record each and every property change" is taken to mean: **no change to
the document escapes the log** -- not: every intermediate write is kept.
A sketch drag writes `Geometry` hundreds of times inside one transaction;
only the value before the first write and the value after the last one
mean anything. `TransactionObject::setProperty` already keeps exactly
that (the first `Copy()` per property), and `AtomicPropertyChange`
already folds a nested list edit into one before/after pair. Figma makes
the same cut for the same reason: its journal coalesces entries because
property-level last-writer-wins makes the intermediates dead weight.

So the writer runs at **commit**, walks the transaction's objects in
their recorded order, and emits one op per touched property. Nothing is
serialised during the drag.

What does change is coverage. Today a write is recorded only if a
transaction is open or the application has an active transaction name
(`Document::_checkTransaction`, `src/App/Document.cpp:435`); a script
that sets a property without opening a transaction leaves no trace. With
the log enabled, `_checkTransaction` opens an **implicit transaction**
instead of returning. Implicit transactions are flagged, carry their
origin (`python`, `gui`, `recompute`, `restore`), and are closed when
the *invocation* that opened them returns (user, 2026-09-22): the old
`Transaction` is only a user- and browser-friendly way of grouping ops,
and any invocation boundary groups equally well -- a GUI command, a
Python call into the document from the console or a macro, a recompute,
a restore. Every op made inside the invocation is grouped under it
automatically, and the group closes when it returns, so there is no
dependence on an event loop and the rule is the same in the GUI, in
`FreeCADCmd` and in a headless server. Whether the undo UI shows them is
a UI policy, not a log one.

Known leaks to close or accept, from the survey:

- Writes through a non-const reference to a property's internals that
  never call `aboutToSetValue`. Those are already bugs for undo; the log
  inherits them and a debug-build audit (compare value hash at commit
  against the last logged hash for untouched properties of touched
  objects) will find them.
- `hasSetValue` short-circuits when the new value `isSame` as the old.
  The before-copy has been taken by then. Harmless here: before and
  after hash the same, and the writer drops ops whose two references are
  equal.
- View-provider properties are recorded only under
  `ViewObjectTransaction` / `recordViewObjectChange`. The log follows
  the same switch and marks the op's container kind (`app` or `gui`).

### 9.2 Ops

| Op | Target | Payload |
| --- | --- | --- |
| `create` | object id, name, type | *after*: whole-object snapshot |
| `remove` | object id, name, type | *before*: whole-object snapshot |
| `set` | container, property name | *before* and *after* value refs |
| `addprop` | container, property name | type, group, doc, attr (the dynamic-property metadata) |
| `delprop` | container, property name | the same, plus *before* value ref |

- **Container** is `doc` (the document's own properties), `obj:<id>` or
  `view:<id>`. Objects are named by `DocumentObject::getID()` -- a
  per-document `long` that is persisted and never reused -- with the
  internal name recorded on `create` for readability. Sub-object identity
  inside a value (a sketch's geometry id, a mapped element name) is the
  value's business, not the log's.
- **Whole-object snapshot** is what `Document::exportObjects` writes for
  one object: type, name, id, extensions, dynamic properties and every
  persisted property. It is what makes `remove` invertible after the
  live object the in-memory transaction holds is long gone.
- An object created and removed inside one transaction produces no ops,
  as it produces no `TransactionObject` today.

### 9.3 Values

A value is what `Property::Save` writes: the XML fragment, captured with
a `Base::StringWriter`-style writer, plus whatever the property hands to
`writer.addFile()` (a `.brp`/`.bin`, an element map, a hasher table),
captured as attachments. No new serialiser: every property that can be
saved can be logged, on day one, and every old value can be restored by
the `Restore` that already exists.

Values are **content-addressed** (hash of the canonical bytes) and
stored once. This does three jobs:

1. **Undo and redo are free.** Undo writes ops whose *after* is the
   earlier op's *before*; both already exist.
2. **Conflict detection is a comparison.** "Can transaction T still be
   undone?" is "is each op's *after* ref equal to the property's current
   ref?". That is the well-defined rule per-user undo needs (section 12).
3. **Truncation is safe.** Every op carries both refs, so dropping the
   old end of the log never breaks the ops that remain, and the nearest
   version (16.1) is always the anchor. There is no replay-from-genesis
   and no baseline to protect.

The cost is that the first touch of a property writes two values. After
that, one.

**The whole-array problem** (section 5, question 1) is *not* solved by
this and the design does not pretend otherwise. A one-vertex move logs
the whole `Geometry` list. What makes that tolerable for now: it is what
the in-memory undo already copies, it compresses well, and it is written
once per transaction rather than per mouse move. What keeps the door
open: each value row has an `enc` column. `full` is the only encoding at
first; `delta:<codec>` against a named base value is the planned second,
with per-type codecs where they pay -- sketch geometry and constraints
keyed by `SketchGeometryExtension::getId()` first. A generic byte-level
delta (zstd `--patch-from`, content-defined chunking) is cheaper to try
and should be measured before any per-type codec is written; the survey's
caveat is that text BREP renumbers and may not dedup at all.

### 9.4 What the property log does and does not buy

Section 4 said a property-delta log gives undo and not merge. That
stands, with one refinement from the survey: **Figma ships multiplayer
on exactly this model** -- `Map<ObjectID, Map<Property, Value>>`,
last-writer-wins per property, order defined by the server. Two clients
editing *different properties or different objects* merge trivially
under it. The loss is confined to two clients inside the *same* array
property at once, which is the two-people-in-one-sketch case. That case
needs sub-property ops, and the route to them is the delta codecs of
9.3 -- a geometry-id-keyed sketch delta *is* an operation -- not a
second log. So the property log is the substrate, and operations arrive
as encodings inside it rather than as a rewrite of it. This softens
section 4's "rewrite, not a refinement".

Each transaction also carries the `MacroManager` `App` lines issued
while it was open, as a `script` annotation. It costs nothing, it makes
the browser readable ("what did this step *do*"), and it is the raw
material for the semantic protocol of `docs/ComputeBoundaries.md`
section 6. It is an annotation: nothing replays it.

## 10. The filter: what about Shape

The user asked for a predefined filter, with `Shape` as the example, and
for the pros and cons.

**For logging outputs:** restoring a past state is exact and needs no
recompute; it is immune to OCCT giving a different answer on another
version or build; undo stays instant however old the step; a history
browser can show the geometry of any step.

**Against:** a recompute rewrites the shape of every dependent object,
megabytes each, plus an element map and a hasher table per shape, on
every edit -- the log becomes a multiple of the document per
transaction; serialising BREP on the commit path is the latency section
5 question 8 forbids; and all of it is derivable. Onshape, the stated
model, stores none of it: regeneration results are a cache.

**A filter by property name or type is wrong, though.** `Part::Feature`
declares `Shape` with a bare `ADD_PROPERTY`
(`src/Mod/Part/App/PartFeature.cpp:221`), with no `Prop_Output`. For an
imported STEP body, or `obj.Shape = s` from Python, the shape is the
*input* -- there is no `execute()` that can regenerate it. Filter it and
the log cannot reproduce the document.

**The rule instead: a value is derived if it was written by its own
object's `execute()`.** Mechanically: the write happens while the owner
`isRecomputing()` (`ObjectStatus::Recompute` / `Recompute2`). That
classifies correctly with no list to maintain: a Pad's `Shape` is
derived, an imported body's `Shape` is an input, and so is any output
property of any workbench nobody has heard of. `Prop_Output` /
`Property::Output` are honoured as an additional hint; `Transient` and
`PropNoPersist` properties are never logged, as they are never saved.

Derived values get three possible treatments, a per-document setting:

| Policy | Derived values | Use |
| --- | --- | --- |
| `none` | not recorded; the op notes that the property changed | smallest log |
| `cache` (default) | written to the evictable tier, under the one eviction budget of 16.3 (bytes and count, weighted by the cost to regenerate); the op holds the ref, and a missing ref means "recompute" | normal use |
| `full` | logged durably like inputs | archival; a model that must survive a kernel change exactly |

This is the hybrid section 5 question 3 predicted, and the invalidation
rule it asked for is simple because a cached value is a historical fact
rather than a prediction: it never goes stale, it only goes missing.
When it is missing, the state is reached by recompute, and the recompute
record (section 11) says whether the kernel that would do it matches the
kernel that did.

**Undo speed does not regress.** The in-memory `Transaction` objects are
kept for the last `UndoMaxStackSize` steps exactly as now, as the hot
path. A `TopoShape` copy there is a shared `TShape` handle, not a deep
copy. Undo inside the window applies them as today, outputs included,
with no recompute; only a *cold* undo, beyond the window or from a past
session, takes inputs from the log and recomputes.

### 10.1 Imports: a source on the transaction (user, 2026-09-21)

Ruling: **the transaction work is purely additive. Nothing in the
document model changes** -- no new import feature, no `execute()` added
to anything, no property retyped. An idea floated in discussion, to make
imported shapes derived by giving them a file-holding import object, is
withdrawn on that ground.

The import case is handled in the record instead. Onshape keeps the
imported file and treats it as the definition; here **the import
transaction carries the original file, and every shape assigned in that
same transaction is then derivable** and goes to the cache tier like a
Pad's shape, not to the durable log.

- **A transaction may carry a *source*:** one or more files, stored in
  the blob store by hash (the same file imported twice is stored once),
  and the *recipe* that consumed them -- the importer module, the call,
  and a snapshot of the import preferences that steer the translator,
  since those are hidden inputs otherwise.
- **Who attaches it.** `Gui::Application::importFrom`
  (`src/Gui/Application.cpp:826`) is where every GUI import becomes
  `Module.insert(path, doc)`, so one hook there covers every importer
  with no per-importer change. A Python API exposes the same call for
  scripts and importers that want it.
- **What it demotes.** Only the blob-sized values written in a
  source-bearing transaction -- shapes, meshes, point sets. The ops
  themselves stay durable, and so do their small values: the `create`
  ops with id, name and type, placements, labels, colours, links. The
  structure of an import is in the log in full; only its geometry is
  left to the file.
- **Cold restore** of such a transaction therefore applies the ops as
  usual, and for each shape ref missing from the cache re-runs the
  recipe **into a scratch document** and takes the shapes from there.
  The real document is never the target of a replayed importer. The
  environment stamp says when the translator is not the one that made
  the original.
- **Matching is by a stamp the importer provides, never by name**
  (user, 2026-09-21). Names cannot match: the original import landed
  in a document that already had objects, so its `Body` may have become
  `Body003`, while the scratch document hands out `Body`. Copy/paste has
  the same problem and solves it with the `id` attribute and a name map
  (`Document::readObjects`, `reader.addName`). Nor is the object id
  reliable here: it comes from a counter that any temporary object
  advances, so it only matches if the importer behaves identically, and
  that cannot be promised. What is needed is an identity **intrinsic
  to the file or the importer**, and each importer has one:
  - STEP and IGES through OCAF (`ImportOCAF2`): every object comes from
    a `TDF_Label`, and `TDF_Tool::Entry(label)` gives its path
    (`0:1:1:5`), already printed by `Tools.cpp:102`. Beneath it, the
    STEP entity itself: `XSControl_TransferReader::EntityFromShapeResult`
    and `StepData_StepModel::IdentLabel` give the `#123` written in the
    file, and STEP product entities carry `PRODUCT.id` and
    `PRODUCT_DEFINITION.id`, which authoring systems set to their own
    part numbers. The entry is the fallback, the entity id is preferred.
  - IFC: `GlobalId`, a GUID on every product by the standard.
  - glTF, OBJ, DXF, meshes: node/object index or name in the file.
  - Anything else: the position of the object in the importer's
    creation order, the weakest form, still deterministic for a given
    importer and file.
- **The stamp is a dynamic property on the created object**
  (`ImportId`, a string, group `Import`, hidden), set by the importer
  through a small helper, and therefore recorded by the log's own
  `addprop`/`set` ops with no special channel. It is set on every
  import, history on or off (user, 2026-09-22; a preference turns it
  off), so that a history started later from the file (16.6) can still
  match the imports made before. An object the importer does not stamp
  is not matched, and its shape stays a logged input -- the safe side
  again, and it means importers can gain stamping one at a time.
- **Replay** runs the recipe into the scratch document, reads the
  stamps off the objects it produced, and pairs them with the recorded
  `create` ops. The type is checked; a stamp the log has and the replay
  lacks fails the restore of that value loudly instead of guessing.
- **The object id still has to agree**, for a second reason, which is
  why the harvested shape cannot simply be copied across: **the object
  id is the shape's `Tag`**, and it is written into every mapped element
  name. So the scratch document is seeded with the recorded
  `getLastObjectId()` as a best effort, and where the ids nevertheless
  differ the shape is re-tagged to the recorded id on the way in -- the
  element map is rebuilt by tag, which the TNP machinery already does
  for a shape whose owner changes.
- The stamp is also what an "update import" command would need later,
  and what would let two imports of two revisions of one file be
  correlated in the browser. That is not designed here.
- **Only objects created in the same transaction are demoted.** An
  importer that assigns a shape to an object that already existed has
  no counterpart in a scratch document; that value stays a logged
  input.
- **No source, no demotion.** `obj.Shape = s` from a script, or an
  importer called directly without attaching a source, stays an input
  and is logged in full. The fallback is always the safe side.
- A shape assigned to the same object in a *later* transaction is an
  ordinary input again.

Consequence for the cache: a large assembly takes minutes to translate,
and an eviction that goes by size alone would throw exactly those
shapes out first. Eviction weighs bytes against the cost to regenerate,
which the log already knows -- the recompute record has per-object
duration, and the import transaction records its own.

## 11. Pseudo transactions

Rows in the transaction table that group no ops, or no user ops. They
make the log a history rather than a diff list.

- **`recompute`** (the user's proposal). Written at
  `Document::signalRecomputed`. Carries: an **environment ref** (below),
  the objects recomputed in order, per-object status and error text,
  duration, and the refs of derived values under the `cache`/`full`
  policies. It is the record that lets a later reader say "this state
  was produced by OCCT 8.0.1; you are on 8.1.0; the cached shape is what
  the author saw, a recompute may differ". It is also the natural unit
  for out-of-process geometry: a recompute record is a job description
  going out and a result coming back, which is the sync point section 5
  question 7 asked about. `docs/ComputeBoundaries.md` 3.4 already
  requires the kernel version in any content key; this is where it
  lives.
- **`session`**. Open and close. User, host, platform, and the
  environment.
- **`environment`**. Stored once, referenced by id: FreeCAD version and
  `BuildRevisionHash`, `OCC_VERSION`, Python, Qt, SMESH and the rest of
  what `App::Application::Config()` already holds, plus the versions of
  the loaded modules. Nothing per-document records the OCCT version
  today.
- **`save`**. Every save creates an unnamed **version** (16.3) and
  the `save` row records which, plus the file path. The version row
  holds **a hash of the saved `Document.xml`** (user, 2026-09-21), taken
  from the bytes as they stream into the writer; a file is matched to a
  log by hashing its `Document.xml` on load and looking the hash up
  among the versions. The hash is of that entry and not of the whole
  archive because in `embedded` mode the log is inside the archive it
  would be describing; and it is enough, because any program that saves
  the document rewrites `Document.xml`. Save As continues the same
  history -- the document's `Uid` and log are unchanged, only the path
  in the `save` row differs -- and it is "Save a copy without history"
  (13.3) that starts a file with none.
- **`version`, `branch`, `merge`, `trim`**. The version and branch
  operations of sections 16 and 17, each one row so the browser shows
  who did what when.
- **`undo` / `redo`**. Ordinary transactions whose ops are an inverse,
  with `inverts = <seq>`.
- **`restore`, `import`, `merge`**. Bulk structural events, recorded as
  one transaction with origin set, so a 10 000-object import is one
  entry in the browser. An `import` carries its source file and recipe
  (section 10.1).

## 12. Undo over the log

- **Undo of T** is a new transaction whose ops are T's in reverse order
  with before and after swapped, `inverts = T`. It reuses value refs and
  stores nothing new. Redo is the undo of the undo.
- **The linear undo the user knows is unchanged.** The undo and redo
  stacks become lists of log sequence numbers. A new edit after an undo
  clears the redo *stack*; the log keeps everything, and the abandoned
  redo run is browsable and restorable. It is *not* a branch in the
  sense of section 17 -- those are made on purpose -- but any point in
  it can become one.
- **The hot path and the log agree.** An undo inside the in-memory
  window applies the kept `Transaction` copies as today, and the log
  still receives the inverse transaction; the two describe the same
  state change and the log's is the durable one.
- **Selective and per-user undo** (undo T when it is not the tip) is
  allowed exactly when every op in T passes the 9.3 check -- its *after*
  ref is still the property's current ref, its created objects still
  exist, its removed objects' names and ids are still free. Otherwise it
  is refused with the conflicting ops named. No transformation is
  attempted. This is Onshape's stated behaviour, and "refuse" is the
  honest first version.
- **Restore to sequence N** is distinct from undo, as in Onshape: one
  new transaction that sets every property differing between now and N.
  When N has a snapshot (section 16) the restore is a checkout of it,
  which is what makes rollback instant; otherwise it is the nearest
  older snapshot plus a replay of the ops up to N.
- **Cross-document transactions.** The fork shares a transaction id
  across documents (`Application::setActiveTransaction`). Each document
  keeps its own log; the shared id is recorded as a global transaction
  uuid so a browser can correlate them, and undo fans out as
  `closeActiveTransaction` does today.

## 13. The store

### 13.1 Survey result

| Candidate | Verdict | Why |
| --- | --- | --- |
| **SQLite** (WAL) | **chosen** | A log transaction maps onto a SQL transaction, so crash atomicity is inherited, not written. The browser's questions -- by object, by property, by user, by time -- are queries. Public domain; already in `.conda/freecad` (3.53.4) and on every platform; an official WASM build with OPFS persistence. Blobs up to about 100 KB are faster inside it than as files. |
| Custom segment file (length + CRC frames) | fallback, behind the same interface | Simplest to stream and to port. But the index, torn-write recovery, compaction and every inspection tool become ours. Its one real advantage -- the record is the wire frame -- is weak here, since the wire format is the JSON-schema protocol of `docs/ComputeBoundaries.md`, produced from rows either way. |
| git / libgit2 | **rejected** | A loose object per value means a file per property write; periodic repack/gc in a transient directory; zlib + SHA-1 on the commit path; GPLv2-with-exception to vendor; and the recurring lesson that git as a database does not work out. Branching, the part worth having, is a `parent` column. Git stays the analogy. |
| LMDB | rejected | mmap with a pre-declared map size, files that never shrink, no credible WASM path; key-value only, so the queries become ours. |
| RocksDB / LevelDB | rejected | LSM with compaction threads and a directory of files, tuned for servers; heavy; no WASM target. The access pattern here is append and scan. |
| Appending to the `.FCStd` zip | rejected as a live log | The central directory is at the end of a zip, so every append rewrites it and a crash in between leaves an archive ordinary readers reject. Zip is the save-time container only. |

SQLite's session extension (changesets) was looked at and is the wrong
layer: it diffs tables, and the document is not in tables.

So, to the user's question "use database?": yes, an embedded one, as the
index and container of the log -- not as the document model.

### 13.2 Layout

Per document, in its transient directory (revised 2026-09-22 with
section 16):

```
<transient>/history/log.db          this document's log: transactions, ops,
                                    versions, branches. SQLite, WAL,
                                    synchronous=NORMAL
<transient>/blobs/<sha1>.<ext>      the document's one blob store (16.2),
                                    shared by every version of it
```

Values under a threshold (start at 64 KB, tune by measurement) live in
the log's `value` table, compressed. Larger ones -- shapes, meshes,
images, `Document.xml` snapshots -- go to the **`App::FileBlobManager`
store that already exists** (`docs/FileBlobsManager.md`): immutable,
SHA-1 addressed, refcounted, and already taught to write itself into an
`.FCStd`; one per history by 16.2. The log adds new kinds of referrer to it
and no second blob store. The document's transient directory is deleted
when the document closes (`Document::~Document`), which is what makes
`session` mode session-scoped and is why anything meant to outlive the
session -- `embedded`, or the proposed `local` of 13.3 -- lives
elsewhere.

Schema sketch:

```
meta(key, value)                  schema version, log uuid, document Uid
environment(id, json)
session(id, env, user, host, opened, closed)
branch(id, name, from_version, head_seq, id_stride, created, closed)
txn(seq PRIMARY KEY, parent, merge_from, branch, gtid, session, kind,
    origin, name, inverts, time, script, source)
op(txn, idx, op, ckind, cid, cname, prop, vbefore, vafter, derived)
value(id PRIMARY KEY, hash UNIQUE, enc, base, size, tier, data, blob)
version(num PRIMARY KEY, uuid, branch, kind, name, seq, env, docxml_hash,
        schema, created)
manifest(version, entry, blob)
```

`txn.parent` is what makes it a tree rather than a list: normally
`seq - 1`, something else after a restore or on a branch (section 17).
`value.tier` separates `durable` from `cache` (section 10) so eviction
is one `DELETE`.

Compression: zstd is in the env but only transitively; zlib is already
linked. Decide by the phase-0 measurement, not by taste. Decided: zstd, section 20.2.

All of this sits behind an `App::TransactionStore` interface (append
transaction, read transaction, get/put value, truncate, export). The
document never sees SQLite. A browser client does not hold a log at all
-- the server does -- so WASM viability is insurance, not a requirement.

### 13.3 In the FCStd or not

The user asked whether to offer storing the transactions in the
`.FCStd`. **Yes, as an option, off by default**, in three modes set per
document (with a preference for the default):

| Mode | Where | Gives |
| --- | --- | --- |
| `session` (default) | transient directory only; gone when the document closes | unbounded undo, the browser, versions and branches within the session, and -- new -- crash recovery by replaying the log's tail over the last save |
| `local` (proposed in the audit, 2026-09-22) | `<user-data>/history/<Uid>/`, the log and its blobs | the same, kept across sessions on this machine without touching the file; the natural home for a user's private branches of a file they do not own |
| `embedded` | inside the `.FCStd` | durable history that travels with the file; what pinned links (16.5) need |
| `off` | nowhere | today's behaviour exactly -- **withdrawn 2026-09-22, section 22.1: the log is not optional** |

`local` is not the user's ruling; it fell out of the audit once
`session` was seen to die with the transient directory. It is recorded
as proposed.

A sidecar file next to the document was considered and dropped: sidecars
get separated from their documents, and a directory-mode project already
gives the same thing for anyone who wants history outside the archive.

Mechanics of `embedded` (revised 2026-09-22, section 16.4): on save,
take a consistent copy of the log (`VACUUM INTO`, which also compacts),
apply the retention policy, and hand it to a **document-level dynamic
`PropertyFileIncluded`**, so it travels as an ordinary property's file.
Large values ride the blob manager's existing entries. On load the
property's file is copied out to the transient directory and the live
log continues from it.

Compatibility: a `PropertyFileIncluded` is restored and re-saved by any
FreeCAD, so the embedded log **survives a round trip through a FreeCAD
that knows nothing about it** -- which is what lets a pinned external
link (16.5) find it -- and for the same reason it can go stale behind
edits made there. The hash guard is therefore load-bearing, not a
nicety: every version row records the hash of its `Document.xml`, and a
file whose `Document.xml` hashes to no version in the embedded log has
been edited elsewhere. Its log is set aside and its history restarts
from a fresh snapshot of the file as found.

The costs, which are why it is opt-in:

- **Size.** An `.FCStd` is rewritten whole on every save, so the history
  is recopied every time. Retention is therefore part of the feature,
  not a later nicety: a budget by size, transaction count or age, under
  which the oldest transactions are dropped (safe, by 9.3). Op-level
  cache values are never embedded unless the policy is `full`; derived
  shapes travel only inside the snapshots of the versions retention
  keeps (16.4), since a version is a complete file.
- **Privacy.** Word's tracked changes are the cautionary tale: history
  that travels with a file leaks what the author deleted, and who they
  are. Hence: off by default; a visible indicator that a document
  carries history; **"Save a copy without history"**; and user and host
  recorded only if a preference says so. Onshape's refusal to delete
  history is a property of a hosted service and is not copied.

## 14. Performance and migration

The bar is section 5 question 8: one property on one object must not get
slower to the touch.

- Nothing is serialised while a transaction is open. The cost is at
  commit, over the properties touched, and the copies being serialised
  are ones the undo system already made.
- The first version commits synchronously, and **phase 0 measures that
  before anything else is built**: bytes and milliseconds per commit for
  a scalar edit, a 1000-element sketch edit, a Pad edit under each
  derived-value policy, and a large import. If the sketch case is over a
  few milliseconds, serialisation of the already-detached copies moves
  to a writer thread (properties holding Python objects stay on the main
  thread for the GIL), with `log.db` as the queue's durable end.
- `TransactionObject::getMemSize()` returning 0 gets fixed on the way
  past; the hot window should be bounded by memory that is actually
  counted.
- `UndoMode == 0` continues to mean no in-memory undo. The log has its
  own switch (`off`), so a batch script can still run with neither.
  **Withdrawn 2026-09-22 (section 22.1)**: the log is the undo system
  and is not switchable; the `TransactionLog` preference of section 21
  is a staging switch for the build only.

## 15. Phases

Consolidated 2026-09-22 after sections 16 and 17 were decided.

0. **Measure.** Instrument commit to serialise and hash what it would
   log; no store. Decides the inline threshold, the compressor, and
   whether the writer thread is needed. Also measure byte-level delta
   and chunking on real BREP and real sketches, and the cost of a
   snapshot (16.1) on a large document. **Done 2026-09-22, section 20**: zstd,
   64 KB inline, the zstd prefix delta as the generic encoding, a writer
   thread that serialises detached copies only, attachments hashed on
   their own.
1. **Store, writer, versions.** `App::TransactionStore`, the SQLite
   backend, values through `Property::Save`, implicit transactions, the
   derived rule, the pseudo transactions; versions' blobs in the
   document's blob manager (16.2); unnamed versions on save and on the cadence (16.3);
   history initialised from any file (16.6). `session` mode. Undo
   behaviour untouched. Python read API and gtest coverage that a log
   replays to an identical document and that a checkout equals a load. **In progress, section 21**: store,
   writer, implicit transactions, derived rule, Python read API, the
   environment / session / recompute records and the replay test are
   built (2026-09-22); versions, the save record, history from a file and
   the writer thread are not.
2. **Browser.** A history panel: transactions and versions by name,
   time, origin; ops per transaction; filter by object and property;
   the script annotation; "restore to here". **Re-ordered 2026-09-22
   (section 22.2)**: built as a dockable panel alongside phase 1 from
   now, as the verification tool for every record added, and grown into
   the log manager as later phases land.
3. **Undo over the log.** Undo as a forward transaction, stacks as
   sequence numbers, cold undo past the hot window as checkout plus
   replay, selective undo with the refuse rule. This *replaces* the
   undo system rather than sitting beside it (22.1).
4. **Named versions, embedding, branches.** Named versions and the
   eviction budget; `PropertyHistory`, `Version`, `Branch`; `embedded`
   (and `local`, if adopted) modes; retention; save-without-history;
   the indicator; branch create and switch (17.1, 17.2); trimming
   (16.7).
5. **Pinned links.** The `version` attribute on `PropertyXLink`,
   checkout of a linked version, the fallback and its status (16.5).
   The first feature that needs history to travel between files.
6. **Merge.** Three-way property merge, the diff and conflict picker,
   the `merge` transaction (17.3).
7. **Recovery and streaming.** Crash recovery from the log tail over the
   last version, which *replaces* the autosave / recovery-file
   machinery (22.1); the transaction as the unit streamed to thin clients;
   per-user undo in a shared session (`docs/ThinClient.md` 8.11).
8. **Deltas.** `enc = delta`, generic first, then the sketch codec --
   the point at which two clients in one sketch, or two branches, stop
   conflicting on the whole array.

## 16. Versions (user, 2026-09-22)

Decided, and this section overrides anything above it that disagrees:
**SQLite is the back store, and Onshape is the blueprint.** The log of
sections 9 to 12 stays as the fine-grained record; what is added is the
coarse one, *versions*, and the link between documents that they make
possible.

### 16.1 A version is a snapshot of the FCStd

A version is a **materialised `.FCStd` inside the store**: the set of
entries the archive would hold -- `Document.xml`, `GuiDocument.xml`,
every `.brp`/`.bin`/`.Map`/`.Table`, the thumbnail -- each stored as a
content-addressed blob, plus a manifest row mapping entry name to blob
hash. Since 2026-08-17 the shape files already *are* blobs in
`App::FileBlobManager` (`docs/FileBlobsManager.md` 13.7,
`docs/SharedShapeStorage.md`), named `Box.Shape.brp` and referenced by
hash from `Document.xml`, so a manifest is mostly hashes of blobs that
exist already; taking a snapshot is a save of `Document.xml` and
`GuiDocument.xml` into the store plus a list. It is cheap enough to do
often, which is what 16.3's cadence relies on. Writing the archive out
is a copy of the entries; loading a version is `Document::restore` fed
from the manifest instead of a zip.
Nothing about the document format changes, and no new loader is
written: a version *is* a saved file, held apart.

Two consequences of "the snapshot is the save format":

- **Rollback is instant.** Restoring a version is a load, which is the
  path every document already takes on open, not a walk of inverse ops.
  The ops between two versions are still there, for browsing, for undo
  within a step, and for replay from the nearest older snapshot to a
  point that has none.
- **Shape outputs are in the snapshot.** A saved file carries every
  object's `Shape`, so a version does too, which is what makes checkout
  exact without a recompute. That is the `cache` tier of section 10 by
  another name, and it inherits its rule: derived blobs are evictable
  (16.3), the definition is not. A named version keeps its shapes for
  as long as it exists, which is the one place derived values are
  durable.
- **The view state is in the snapshot but not in the checkout.**
  `GuiDocument.xml` is in the manifest so that a version is a complete
  file, but checking a version out into an open document keeps the
  current cameras and view settings; the user asked for an earlier
  model, not an earlier viewpoint.
- **Partial documents are never snapshotted.** A partially loaded
  document (`PartialDoc`) does not hold its full content, and a snapshot
  of it would be a truncated file presented as a version. Versions are
  taken only from fully loaded documents; ops are still recorded.

### 16.2 One blob store per history

Corrected 2026-09-22: an earlier draft read the user's "global" as
per-user, one store for every document on the machine. **It means one
blob manager per history**: every version and snapshot of a document
stores its blobs in that document's one `App::FileBlobManager` rather
than each version carrying its own set. The store stays per document,
as `FileBlobManager.h` argues it should ("documents are meant to move
into separate processes"), and nothing is shared across documents.

What it buys is the dedup that matters: two versions differing by one
edit share every unchanged `.brp`, a shape reverted and re-made is
stored once, the STEP file behind an import (10.1) is stored once
however many times its transaction is replayed. What does not dedup is
`Document.xml`, which changes on every save; acceptable, the definition
is small next to the geometry. The same manager answers 9.3's values
and 10.1's import sources, so there is one blob store per document in
the design, not one per version and not one per purpose.

Mechanically it is the existing manager with more referrers. Shape files
are already blobs in it (16.1); a version's manifest holds handles on
them, so a blob a later save no longer needs stays alive while any
retained version names it. Garbage collection is the manager's existing
refcounting: a blob goes when no property, no retained manifest and no
retained op value holds it. The `Content.xml` index and the FCStd save
path are unchanged; on save the manager writes the blobs the *file*
needs as it does now, and `PropertyHistory` (16.4) is what asks it to
also write the ones the *embedded history* needs.

Where the blobs live follows the mode (13.3): in the document's
transient directory for `session`, alongside the log for `local`, in
the archive for `embedded`.

### 16.3 Two kinds of version

| Kind | Made by | Evictable | Purpose |
| --- | --- | --- | --- |
| **unnamed** | automatically -- every save, and on a cadence between saves (every N transactions or T seconds, preference) | yes, under a user-configurable limit (count and bytes; oldest first, weighted by the cost to regenerate as in 10.1) | instant rollback; crash recovery; the anchor a cold undo replays from |
| **named** | the user, explicitly ("pin"/"create version"); or implicitly when another document links to this one at a version (16.5) | never, by the store; deletable only by the user, and refused while an external link is known to pin it | stable references; releases; what travels in the embedded log by default |

A version has a **number**, a per-document monotonic integer, and a
uuid; numbers are what people and link properties use, the uuid is what
catches two unrelated documents both having a "version 7". The store
records for each version its number, kind, name, the log sequence it
corresponds to, the environment ref, the hash of its `Document.xml`,
and its manifest. The `save` pseudo transaction of section 11 becomes
"create an unnamed version"; a save is one of the cadences.

Eviction removes the version row and manifest; blobs go when nothing
else reaches them. Eviction never removes ops: the fine-grained log is
bounded by its own retention (13.3), and a stretch of history whose
snapshots have all been evicted is still restorable, slowly, by replay
from the nearest surviving one.

### 16.4 What the FCStd carries

Both are **document-level dynamic properties**, so the document model is
untouched and a FreeCAD that does not know them round-trips them as
plain data:

- **`History`** (`App::PropertyHistory`, hidden): the embedded copy
  of the log database, present only in `embedded` mode. **Not a plain
  `PropertyFileIncluded`** (user, 2026-09-22): it is a
  `BlobReferrerProperty` that holds the log database as one blob *and*
  a handle on every blob the retained versions' manifests name, and in
  `collectBlobs` it notes all of them to the holding document's blob
  manager. The manager then writes into the archive exactly the
  historical shapes the embedded history needs and nothing else, with
  the existing skip and prune applying to them unchanged; on restore
  the same handles are handed back and re-registered in the store. This
  is the mechanism the manager already has for a blob that references
  other blobs -- a shared shape file names the file it reads, and the
  index records blob-to-blob edges for it (`docs/FileBlobsManager.md`
  13.7). The history blob is one more such file with more edges. It
  needs one small extension of the referrer interface: a referrer that
  is restored several blobs, where today `assignRestoredBlob` hands
  over one.
  Which versions' blobs come along is the retention policy: by default
  the named versions and the current one, with unnamed ones dropped.
- **`Version`** (`App::PropertyString`, read-only): the number of the
  version this file *is*. Written on every save. On open it names the
  version row to compare the `Document.xml` hash against; a match means
  the file and its log agree, a mismatch means someone else saved this
  file (13.3) and the log is set aside.

The physical file is always a complete, standalone document. The
embedded log is an addition to it, never something it depends on.

### 16.5 External links pinned to a version

Today `PropertyXLink` writes `file`, `stamp` (the linked document's
`LastModifiedDate`), `name` and `resolve` (`src/App/PropertyLinks.cpp:4464`);
a link always resolves to the live state of the other file. It gains a
**`version` attribute**, absent by default. When present:

1. Open the physical file's embedded `History` (16.4). Find the version
   row by number, check the uuid if the link recorded one.
2. **Check it out** into a transient directory -- materialise the
   manifest -- and load that as the linked document, under a distinct
   internal name (`Part@v17`), read-only, with `Version` telling it what
   it is. Two links to two versions of one file are two documents.
3. **Fall back to the physical file** if anything at all goes wrong: no
   embedded log, no such version, a missing blob, a hash mismatch, a
   load error. The fallback is reported (a link status the tree shows),
   not silent, and the link keeps its pin so a later open can retry.
4. A link with no `version` behaves exactly as today. Nothing existing
   changes behaviour.

Pinning a link names the version in the linked document, if it was not
named already (16.3), so the linked document's own eviction cannot pull
it away. The linked document cannot know every file that pins it, so
"refused while known to pin" is best effort, and the fallback in 3 is
what makes a broken pin survivable.

This is Onshape's rule -- a cross-document reference points to an
immutable version, never a workspace -- made optional. Its consequence
is the one Onshape gets for free: **a pinned link never takes part in a
cross-document transaction**, because the document it sees is
read-only. The shared-transaction-id fan-out of section 12 applies only
to live links, and the partial-undo problem it carries (a member
document closed, or saved and reopened elsewhere, while the transaction
is undone) is detected by the global uuid appearing in one log and not
the other, reported, and not repaired. A user who wants Onshape's
guarantee pins the link; a user who wants live propagation keeps it
live and accepts the current semantics.

"Update to a newer version" is a change of the `version` attribute,
recorded as an ordinary `set` op in the linking document, which is
exactly how Onshape makes reference updates explicit and undoable.

### 16.6 Starting a history from any FCStd (user, 2026-09-22)

A history can be **initialised from any `.FCStd` that has none**, at any
time: on open, on demand from the browser, or when the user first turns
`session`/`embedded` mode on for a document that predates the feature.
Nothing about the file has to be prepared for it.

- Version 1 is a snapshot of the file as found: its `Document.xml`,
  `GuiDocument.xml` and blobs go into the store under their hashes, the
  manifest is written, `main` is created with its head there, and the
  environment row records what opened it. From then on the file is like
  any other -- the ops start at the next transaction.
- The same path is the **recovery path** for a log that has been set
  aside (13.3, 16.4): a file whose `Document.xml` matches no version in
  its embedded history was edited elsewhere, and the answer is to start
  again from the file as it is now. The old history is kept as a closed
  branch, `main` restarts at a fresh version 1 with `from_version`
  pointing at the last version the old log knew, so a browser can still
  show what came before the gap even though the ops across it are
  unknown.
- Files from upstream FreeCAD and from the fork's older releases are the
  same case. The restore path already tolerates every schema the reader
  knows, and a snapshot is whatever the loader produced, re-saved into
  the store; the manifest records the schema it came from.
- A file saved by a FreeCAD with the log switched `off` has, at most,
  stale `Version`/`Branch`/`History` properties. The hash guard treats
  them as it treats any mismatch: set aside, restart.

### 16.7 Trimming (user, 2026-09-22)

History costs storage, and the user decides what to keep. Trimming is
explicit, is itself recorded (a `trim` pseudo transaction naming what
was removed and why), and never touches the current state of any
branch.

| Operation | Removes | Keeps |
| --- | --- | --- |
| **Trim a branch** | its ops and unnamed versions older than a chosen version, or all of them up to its head | the head version (named if it was not), and every named version on it |
| **Delete a branch** | the branch row and everything reachable only from it | versions that are pinned by a link, merged into another branch (`merge_from`), or that another branch was created from -- those are kept as named versions and the delete is refused if the user asks for them too, with the referrers listed |
| **Squash** | the ops between two versions on a branch | the two versions, with one synthetic transaction between them whose ops are the net property delta, so undo across the squashed span still works as a single step |
| **Evict** (automatic, 16.3) | unnamed versions over the limit, and blobs nothing reaches | everything named |

Blobs go when nothing references them (16.2): no property of the
document, no retained manifest, no retained op value. Because every
version shares the one store, trimming frees only what no surviving
version still names, which is the point of sharing it.

Trimming is safe by 9.3 -- every remaining op carries both its
references, and every remaining version is a complete file -- so
nothing that stays becomes unreadable by what goes. What is lost is the
ability to replay *through* the trimmed span at op resolution; the
versions that bracket it remain as checkouts.

In `embedded` mode the retention policy of 13.3 is an automatic trim
applied to the copy written into the file, not to the live store, so a
lean file can be handed out while the author keeps the full history.

## 17. Branches (user, 2026-09-22)

The history supports branches. Onshape's vocabulary is used: a
**workspace** is a branch, a mutable line of history with a tip; a
**version** (section 16) is an immutable point on one. The design below
keeps everything per document, as Onshape does, and keeps the additive
rule: no branch machinery touches the document model, and a document
that never branches sees nothing new.

### 17.1 Model

- Every document has at least one branch, `main`, created with its
  history. `txn.parent` (13.2) was already a tree; a branch is a **named
  tip** into it, `branch(id, name, from_version, head_seq, id_stride,
  created, closed)`, and every transaction and version row records the branch it
  was made on.
- **A branch is created from a version**, any kind. Branching names the
  version if it was unnamed (16.3), for the same reason a pinning link
  does: the fork point must survive eviction. Branching from a point that
  has no snapshot means first materialising one there by replay from the
  nearest older snapshot -- allowed, and it is what makes "branch from
  this step in the browser" work.
- **An open document is on exactly one branch.** Switching branch is a
  checkout of the other branch's head (a load, 16.1), after the current
  branch's tip is snapshotted so nothing is lost. The document's `Version`
  property (16.4) gains a sibling **`Branch`** naming the branch the file
  is on; a plain FreeCAD round-trips both as strings.
- **Undo is unchanged and is not branching.** Undo appends inverse
  transactions to the *current* branch (section 12); the redo stack that
  a new edit abandons stays browsable as ops on that branch, and is not
  promoted to a branch of its own. Branches are created only on purpose.
  This is Onshape's split too: undo is per-user and linear, branching is
  explicit.
- **Two documents open on two branches of one file** is the same case
  as two versions of one file (16.5): two `App::Document`s under
  distinct names, both fed from one store. Each is a writer on its own
  branch (17.5); nothing is read-only.

### 17.2 Object identity across branches

The trap: object ids come from a per-document counter
(`++lastObjectId`), and two branches that fork at id 56 will both
create object 57. Names collide the same way (`Body001` on both). The
element map keys on the id as the shape `Tag`, so a collision is not
cosmetic.

- **Prevention.** Creating a branch moves the new branch's counter
  forward by a large random stride (`setLastObjectId` exists), so ids
  allocated on different branches do not overlap in practice. The stride
  is recorded on the branch row. Names are not protected this way and are
  expected to collide.
- **Repair at merge.** Anything that collides anyway is renamed and
  re-id'd on the incoming side by the same machinery import uses: the
  name map of `Document::readObjects` for names, and the re-tag of 10.1
  for ids. Links inside the incoming ops are rewritten through the map.
  The stride makes this the rare path, the map makes it a correct one.

### 17.3 Merge

Onshape merges between workspaces with a visual diff and the user
choosing which changes to keep; it does not transform, and it does not
merge geometry. Ours is the same shape, and it is the git *conflict*
model of section 4, not the co-editing model -- the two were separated
there for exactly this reason.

- **Three-way, at the property level.** Base is the common ancestor
  version; ours is the current head; theirs is the other branch's head.
  For every (container, property): changed on one side only -> take
  it; changed identically on both -> take it; changed differently on
  both -> **conflict**, the user picks, default ours. Objects created on
  one side are added (17.2); removed on one side and untouched on the
  other -> removed; removed on one side and changed on the other ->
  conflict.
- **Definitions only.** Derived values (section 10) are not merged: a
  merged definition is recomputed, and the recompute record says on
  what. Where recompute fails on the merged state, that is reported as
  the merge's result, not hidden -- the same as an edit that fails.
- **The result is one transaction**, kind `merge`, on the receiving
  branch, with `parent` the receiving head and a second parent
  `merge_from` naming the version merged in (which is named, 16.3, if
  it was not). Its ops are ordinary `set`/`create`/`remove`, so **a
  merge is undone like any other transaction**, and it streams, is
  attributed, and is browsable like any other. The conflict choices are
  recorded on the transaction as an annotation so a reviewer can see
  what was decided.
- **Whole-array properties conflict as wholes.** Two branches editing
  one sketch is a conflict on `Geometry`, resolved by picking a side.
  That is the section 9.3 limit again, and the sketch delta codec of
  phase 8 is what would turn it into a per-element merge later; nothing
  here prevents that, and nothing here waits for it.

### 17.4 What the FCStd carries, and links

- The physical file is one branch's state, named by `Branch` and
  `Version`. The embedded `History` (16.4) carries every branch's rows
  by default, subject to retention; the `PropertyHistory` referrer
  collects the blobs of retained versions on every branch, so a file
  handed to someone else brings its branches with it.
- **External links pin versions, never branches**, as in 16.5 and as in
  Onshape. A live (unpinned) link sees whatever branch the linked
  document is currently open on, which is today's behaviour and is
  unchanged; a document that wants stability pins.
- Branches are per document. There is no cross-document branch, and
  a "branch this assembly and all its parts" operation is a UI
  convenience over per-document branches plus pinned links, not a store
  concept.

### 17.5 Concurrent writers (user, 2026-09-22)

**Every writer works on its own branch.** Two processes on one file, two
clients in one shared session (`docs/ThinClient.md` 8.11), two documents
open on one history: each gets a branch, created from the head it
started at, and its transactions go there. Nobody writes to another
writer's branch, so there is no lock to take on the log.

Then:

- **Auto-merge after each operation.** When a writer's invocation
  returns (9.1), its branch is merged into the shared head by the
  three-way merge of 17.3. Two writers on different objects or
  different properties merge without conflict, which is the common case
  and the one the property log was built for (9.4).
- **Auto-trim on a clean merge.** A writer branch whose merge had no
  conflict is trimmed away (16.7) as soon as it is merged: its ops are
  already on the shared head as the merge transaction, and the branch
  itself was scaffolding. The shared history reads as a single line of
  transactions, one per operation, attributed to whoever made it.
- **A conflict keeps the branch.** If the merge conflicts -- both edited
  the same sketch, one removed what the other changed -- the writer's
  branch stays, the writer is told, and the conflict picker of 17.3
  resolves it. Until then the writer keeps working on its branch, so a
  conflict costs nobody their edits and blocks nobody else.
- **Per-user undo** is now ordinary: a writer undoes its own last
  operation by appending the inverse to its branch and merging that, and
  the 9.3 check says whether the inverse still applies.

A writer that is a client's view carries more than ops: its view state
and its edit session ride its branch, which makes a session resumable
and browsable (user, 2026-09-25; `docs/MultiViewEdit.md` sec 10, which
also amends the auto-trim above for such a branch).

This is where the section 4 argument lands. The log gives merge for
everything but the same array property edited at once; that case
surfaces as a conflict rather than as last-writer-wins, which is the
correct outcome until the phase 8 delta codecs make it a per-element
merge.

## 18. Audit rulings (2026-09-22)

The audit of 2026-09-22 raised seven items; the user ruled on them the
same day and the rulings are folded in above. Kept here so the
questions are not re-asked.

1. **Copies are clones** -- yes: two files with one log uuid that meet
   again (one links to, or merges from, the other) are two branches of
   one history and merge by 17.3.
2. **"Global" blob store** -- misread; it is one store per history
   (16.2). No per-user store, so no cross-process index and no
   "clear history store" command.
3. **Two writers** -- each on its own branch, auto-merged and
   auto-trimmed (17.5). Nothing is read-only.
4. **Transitive pins** -- handled as today: the `stamp` on a live
   `PropertyXLink` differs on open, the assembly is marked pending
   recompute, and the recompute creates ordinary new transactions. No
   "pin transitively".
5. **Expression-driven properties** -- classed derived, correct, and in
   the test set for the derived rule.
6. **Implicit transactions** -- grouped by invocation, not by event-loop
   turn (9.1).
7. **`ImportId` without history** -- the user asked why it should be
   conditional at all. The reason given was the additive rule (do not
   change a document's content when history is off). Against it: the
   stamp is a hidden string, it is provenance a user may want anyway,
   and a history initialised later from the file (16.6) can then match
   imports made *before* history was on. Ruled (user, 2026-09-22):
   **set it always**, with a preference to turn it off.

## 19. Survey sources (2026-09-21)

- Onshape: microversions are immutable and store the definition change
  plus a parent ref; regeneration results are a rebuildable cache; undo
  is a per-user forward inverse; restore is separate; history cannot be
  deleted. <https://www.onshape.com/en/blog/under-the-hood-how-collaboration-works>,
  <https://www.onshape.com/en/blog/git-style-version-control-cad-data-management>
- Figma: property-level last-writer-wins, server-ordered; checkpoints
  plus a sequence-numbered journal, coalesced.
  <https://www.figma.com/blog/how-figmas-multiplayer-technology-works/>,
  <https://www.figma.com/blog/making-multiplayer-more-reliable/>
- SQLite: <https://sqlite.org/appfileformat.html>,
  <https://www.sqlite.org/fasterthanfs.html>,
  <https://sqlite.org/intern-v-extern-blob.html>,
  <https://sqlite.org/sessionintro.html>,
  <https://sqlite.org/wasm/doc/trunk/persistence.md>,
  <https://sqlite.org/undoredo.html> (its undo log is a TEMP table -- a
  demonstration, not persisted).
- Git as a database:
  <https://nesbitt.io/2025/12/24/package-managers-keep-using-git-as-a-database.html>
- Nobody in the desktop field persists undo: Photoshop, Krita, Blender
  (memfile) and SolidWorks are session-only; Fusion's timeline is the
  feature list, not an undo log. Persistent history is a hosted-service
  feature so far.
- CRDT stores (Automerge, Yjs) keep full history and pay for it in
  tombstones that cannot be collected; noted as the reason not to start
  there. <https://automerge.org/automerge-binary-format-spec/>,
  <https://docs.yjs.dev/api/document-updates>
- FastCDC: <https://www.usenix.org/system/files/conference/atc16/atc16-paper-xia.pdf>
- Not verified in this survey and not relied on: LMDB and RocksDB WASM
  status beyond the absence of an official target; per-commit cost of
  libgit2 (no benchmark found).

## 20. Phase 0 results (2026-09-22)

The measurement harness of section 15 phase 0 is built and run. What it
is: `App::TransactionMeasure` (`src/App/TransactionMeasure.{h,cpp}`),
hooked into `Document::_commitTransaction` behind one static bool. On
every commit it walks the transaction the way the writer of section 9
will -- one op per object created, removed or changed, one value per
property before and after -- and serialises each value through
`Property::Save` into memory (the XML fragment plus every `addFile`
attachment), hashes it, records whether the hash was seen before in the
session, compresses it with zlib and zstd, and for a `set` computes the
zstd prefix delta and a content-defined-chunk diff of after against
before. Nothing is stored. Driven by `scripts/measure-transactions.py`
under `FreeCADCmd` (`TXN_MEASURE_CSV=out.csv`), summarised by
`scripts/summarize-transaction-measure.py`; `App.startTransactionMeasure`
/ `stopTransactionMeasure` / `markTransactionMeasure` and the
`FC_TXN_MEASURE` environment variable turn it on anywhere, and
`FC_TXN_MEASURE_DUMP=<dir>` keeps every distinct value's bytes. The
derived flag is recorded where the design said it must be, at the first
write, in `TransactionObject::setProperty`
(`owner->isRecomputing()`), so it is real, not reconstructed.

Two gtests (`tests/src/App/TransactionMeasure.cpp`) pin the walk: create /
set / remove produce the right ops with the right hashes, a value set
back to an earlier one is seen, a write that changes nothing hashes the
same on both sides.

### 20.1 The numbers

macOS box, RelWithDebInfo, OCCT 8.0.1, text BREP (the document's
`PreferBinary` default). Bytes are of values not seen before in the
session, split by the derived rule; `t/txn` is the whole added cost of
a commit (the baseline run of the same script, measurement off, commits
in 0.0 ms everywhere except the 2 MB move, 2 ms).

| scenario | txns | ops | noop | input B | derived B | zlib | zstd3 | patch | t/txn ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| scalar (Box.Length, 5 edits) | 5 | 13 | 5 | 80 | 15.6K | 0.28 | 0.30 | 0.05 | 0.95 |
| sketch create, 1000 lines + 1000 constraints | 1 | 1 | 0 | 991K | 0 | 0.05 | 0.03 | - | 36 |
| sketch move one point, x3 | 3 | 21 | 12 | 1.6M | 1.4M | 0.06 | 0.03 | 0.00 | 61 |
| sketch drag, 200 intermediate writes | 1 | 7 | 4 | 400K | 491K | 0.06 | 0.03 | 0.00 | 59 |
| sketch, 10 lines, move one point | 2 | 8 | 4 | 24K | 6K | 0.15 | 0.15 | 0.03 | 0.9 |
| Pad.Length, 5 edits | 5 | 40 | 15 | 278 | 76K | 0.29 | 0.29 | 0.04 | 1.9 |
| Pad.Length under a fillet, x3 | 3 | 39 | 15 | 264 | 140K | 0.26 | 0.26 | 0.05 | 5.6 |
| import Schenkel.stp (590 KB STEP) | 1 | 1 | 0 | 357K | 0 | 0.22 | 0.21 | - | 42 |
| import a 4 400-face solid (1.9 MB BREP) | 1 | 1 | 0 | 1.9M | 0 | 0.14 | 0.14 | - | 469 |
| `obj.Shape = s` from Python, same solid | 1 | 1 | 0 | 1.8M | 0 | 0.14 | 0.13 | - | 327 |
| Placement change on that object | 1 | 3 | 1 | 3.7M | 0 | 0.14 | 0.13 | 0.00 | 601 |
| snapshot: saveCopy of the 18-object result | | | | 805K zip, 1.05 MB Document.xml | | | | | 2 850 |

`noop` is set ops whose before and after hash the same; the writer drops
them. `patch` is the zstd prefix delta over the raw after value: 121
bytes for a one-point move in a 409 KB sketch geometry list, 293 bytes
for a 1.9 MB shape moved by its placement.

Where the time goes, per commit: a scalar edit is 1 ms, of which 0.7 ms
is exporting the box's BREP (the derived `Shape`, 2 KB) and 2 us is the
`Length` op itself. A sketch move is 60 ms: `Shape` (251 KB BREP) 28 ms
**twice**, `Geometry` (409 KB XML) 7 ms twice, `Constraints` (300 KB
XML, unchanged) 5.5 ms twice, and hashing plus compression under 5 ms in
all. The 2 MB import is 470 ms, of which 390 ms is the BREP export. The
placement change is 600 ms: the same 1.9 MB BREP exported twice, with
the geometry attachment byte-identical both times (the location lives in
the XML at schema 5) and only the 80-byte XML fragment different.

### 20.2 Decisions

1. **Compressor: zstd.** On BREP text it matches zlib's ratio (0.13 vs
   0.14) at 8-10x the speed (7.5 ms vs 62 ms on 1.9 MB); on sketch XML
   it halves zlib's output (6.4 KB vs 11.9 KB on 409 KB) at a quarter of
   the time. Level 3; level 1 costs 10-15 percent in ratio on XML and
   nothing on BREP, and is the fallback if a value over 1 MB ever has to
   compress on the main thread. zstd is in the env as a transitive
   dependency; it becomes a direct one. The App CMake finds it and
   defines `FC_HAVE_ZSTD` already.
2. **Inline threshold: 64 KB compressed, unchanged.** Everything the
   scenarios produced compresses under that except the imports (260 KB
   for the 1.9 MB solid, 75 KB for Schenkel), which are blobs anyway.
3. **Delta: the zstd prefix delta (`--patch-from`) is the generic
   encoding, and it is enough.** 409 KB -> 121 bytes on a sketch edit,
   1.9 MB -> 293 bytes on a move, 2.8 KB -> 117 bytes on a Pad. The
   sketch codec of section 9.3 stays where it is, wanted for merge (a
   geometry-id-keyed op), not for size. Content-defined chunking is
   dropped for these values: 25 of 25 chunks of the sketch geometry were
   new after a one-point move, because the solver perturbs the last
   digits of every line and text BREP renumbers, exactly as the survey
   warned. The 4 ms it costs is the only thing it delivers.
4. **The writer thread is needed, and the rule for what it may touch is
   simpler than section 14 assumed.** The sketch case is 60 ms per
   commit, twenty times the budget; even with the derived `Shape` on the
   cache tier and the fixes below it stays at 7-9 ms for the `Geometry`
   value alone. But the *after* value of a set is the *before* value of
   the next transaction to touch the property, and that before is a
   detached `Copy()` the undo system already makes. So the writer
   serialises **only detached copies**: an op's before ref is resolved
   from the copy the in-memory transaction holds; its after ref is left
   pending and resolved when the property is next copied (the next
   transaction's first write) or when a version snapshot is taken --
   whichever comes first. The live property is never read off the main
   thread and never copied for the log's sake. A pending after ref on
   the head transaction is the one case the reader must handle: the head
   state is the live document, which is what it would read anyway. The
   copies must outlive their transaction's eviction from the undo stack
   until serialised (a shared handle, not the raw pointer).
5. **Values are two-part: the XML fragment and each attachment hashed
   on its own.** The placement change shows why -- the 1.9 MB geometry
   was byte-identical before and after and was stored twice, because the
   hash covered fragment plus attachment together. With the attachment
   content-addressed separately (which is what `FileBlobManager`
   already does for it at save time), a move writes an 80-byte fragment
   and one ref. The op's value ref then names a fragment row whose
   attachments are blob refs.
6. **Do not re-serialise what is known unchanged.** Two fixes, both
   cheap: (a) `Property::isSame(copy)` before serialising a set's two
   sides -- `Constraints` cost 11 ms per sketch edit and was never
   different; (b) let a property hand the writer a content hash it
   already holds, so `PropertyPartShape`, which keeps its `_blob` handle
   while the shape is unchanged (`makeBlob`), answers a ref instead of a
   390 ms export. Both fold into decision 4: with only detached copies
   serialised, and unchanged ones skipped, the sketch edit's synchronous
   cost is the `Copy()` the undo system already pays.
7. **A create snapshot is composed of per-property values, not a
   monolithic blob.** The 1.8 MB before value of the placement change
   was "unseen" although the object had been created two commits
   earlier: the create op stored the object as one snapshot, so the
   property-level value had no row. With the snapshot being a manifest
   of per-property refs, the first set on any property of a new object
   finds its before already stored, and `remove` is the same manifest.
8. **A version snapshot (16.1) is a save**: 2.85 s and 805 KB for an
   18-object document holding two 2 MB solids and a 1000-line sketch,
   1.05 MB of it `Document.xml`. On the cadence of 16.3 that is a
   background job, not a commit-path one, and section 16.1's "a version
   is a snapshot of the FCStd" is confirmed as affordable only off the
   main thread -- or, once decision 5 is in, as the manifest of refs the
   log already holds plus a `Document.xml`, which is the 1 MB part.

### 20.3 Two observations, not decisions

- `Part::Primitive::onChanged` recomputes on the spot, so a `Box.Length`
  set from Python rewrites `Shape` under `isRecomputing()` before the
  document is ever told to recompute. The derived rule classified it
  correctly (`derived=1`); it is noted because the "no recompute in the
  transaction" variant of the scalar case turned out to measure the same
  thing as the recompute one.
- `Sketcher::SketchObject` writes `InvalidShape`, `InternalShape`,
  `ExternalGeo`, `FullyConstrained` and `Constraints` on every solve,
  five of the seven ops per edit, four of them no-ops by hash. The
  `hasSetValue` short-circuit of 9.1 does not catch them because they are
  written through `setValues`/list assignment. Harmless to the log (they
  are dropped at commit), a cost to the undo copies (300 KB of
  constraints copied per drag), and a candidate for the debug-build
  audit.

## 21. Phase 1 as built (2026-09-22)

What exists on branch `Transaction` after the first day of phase 1, and
where it departs from or sharpens the design above.

**Files.** `src/App/TransactionStore.h` (the interface and the row
structs), `TransactionStoreSQLite.cpp` (the one backend),
`TransactionValue.{h,cpp}` (`captureValue`, `restoreValue`, `hashBytes`:
the serialiser the measurement of section 20 and the log share),
`TransactionLog.{h,cpp}` (the writer on the document),
`tests/src/App/TransactionLog.cpp`. The document reaches its log through
`Document::getTransactionLog()`, made lazily on the first commit after
the mode is set; Python reads it through `Document.getTransactionLog()`,
`getTransactionOps(seq)`, `getTransactionValue(ref)` and
`resolveTransactionLog()`.

**Schema as built** (13.2 sketched it; this is what the store creates):

```
meta(key, value)
environment(id, json UNIQUE)
session(id, env, user, host, opened, closed)
txn(seq PRIMARY KEY, parent, id, kind, origin, name, time, script, session)
op(txn, idx, op, ckind, cid, cname, ctype, prop, ptype, meta,
   vbefore, vafter, derived, PRIMARY KEY(txn, idx))
value(hash PRIMARY KEY, enc, tier, size, data, attach)
version(num PRIMARY KEY, uuid, branch, kind, name, seq, env, docxml_hash,
        schema, created)
manifest(version, entry, hash, source, PRIMARY KEY(version, entry))
```

`value.attach` is the attachment list, one `hash name` per line; an
attachment is a value row of its own (20.2 decision 5). `enc` is `raw`
or `zstd` (level 3 above 128 bytes). `version` and `manifest` arrived
with the save record (below); no branch table yet.

**The writer, as built.**

- Only detached copies are serialised at commit (20.2 decision 4). A
  set's before ref is the undo copy; its after ref is written empty and
  resolved by `TransactionStore::resolveAfter` when the property is next
  copied, when its object is removed, when a dynamic property is
  deleted, or by `TransactionLog::resolvePending()`, which serialises
  the live values behind every pending ref (what a snapshot will call
  first). Pending refs are keyed by `Property::getID()` and looked up
  again by object id and property name, so a property gone without a
  remove op is dropped rather than dereferenced.
- A copy that `isSame()` as the live property is not serialised unless
  an earlier op is waiting on it, in which case it is serialised only to
  resolve that op (decision 6a). Decision 6b, the property-supplied
  hash, is built for the shape (below).
- A `create` is the op followed by one pending `set` per persisted
  property (and an `addprop` with the metadata for each dynamic one);
  a `remove` is one resolved `set` (before only) per property followed
  by the op (decision 7). Replay therefore needs one rule: a property's
  state is the latest non-empty ref in op order, and an object exists
  between its `create` and its `remove`.
- Derived values (section 10) follow `TransactionLogDerived`: `none`
  writes the op with no refs and leaves nothing pending, `cache` stores
  under tier `cache`, `full` under `durable`. No eviction yet.
- View-provider containers are logged with `cid = -1` and never
  resolved by `resolvePending()`.

**Implicit transactions, as built** (9.1). `Transaction::Implicit` and
`Transaction::Origin`. `Document::transactionsWanted()` is "undo on or
log on"; with the log on and no application transaction active,
`_checkTransaction` / `_addOrRemoveProperty` open `<implicit origin>`.
The invocation boundary is `Application::InvocationScope`, a nesting
RAII guard whose outermost destructor commits the implicit transaction
of every document; scopes stand at a console line, a macro, a script
file and a recompute (origin `recompute`, so `execute()` writes group
under one transaction). A GUI command is already an `AutoTransaction`
scope. An explicit open, a save and a close commit an implicit
transaction too. With undo off the commit logs the transaction and
deletes it; with undo on it is an undo step, and whether the undo menu
shows it is left to the UI. The writes that set up a new document
(`Document::Initializing`, cleared at the end of
`Application::newDocument`) open none.

**Pseudo transactions, as built** (section 11). `environment` rows hold
a JSON of `Application::Config()` (version, revision hash and branch,
`OCC_VERSION`, Python, Qt, system); a `session` row is opened when the
log is made and closed with the document, naming user and host only
under `TransactionLogIdentity` (off). The `recompute` record is a
transaction of kind `recompute` with no ops whose `script` column holds
`{env, seconds, objects:[{id, name, error?}]}`, written after the
implicit transaction of the recompute's own writes. `restore`,
`import`, `undo`/`redo` rows are not built (`restore` is, below).

**The save record and the unnamed version, as built** (2026-09-22,
sections 11 and 16.3). `Document::save` taps the bytes of
`Document.xml` as they stream into the archive -- `Base::Writer::beginTap`
/ `endTap`, a forwarding `streambuf` swapped onto the writer's stream, so
nothing is serialised twice -- and once the entries are written calls
`TransactionLog::onSave(path, docXml, blobs, schema)`. That resolves
every pending after ref (the snapshot rule), stores `Document.xml` as a
durable value (with no attachments its ref *is* the SHA-1 of the bytes),
appends a `version` row (`num, uuid, branch=main, kind=unnamed, name,
seq=lastSeq, env, docxml_hash, schema, created`) with a `manifest`
(`Document.xml` -> that value; each collected blob as
`<hash>.<ext>` -> the blob store, 16.2), and then a `save` transaction
with no ops whose `script` is `{version, docxml, blobs, path}`. `truncate`
keeps every value a manifest names. `TransactionStore` gained
`addVersion`, `versions`, `getVersion`, `findVersion(docxml_hash)` and
`manifest`; Python reads them through `Document.getTransactionVersions()`.
`GuiDocument.xml` joined the manifest the next day, and the cadence
between saves with it, eviction, and the checkout (below).

**The history initialised from a file, as built** (2026-09-22, section
16.6). `Document::restore(const char*)` taps `Document.xml` on its way
into the parser -- `Base::Reader::beginTap` / `endTap`, the input mirror
of the writer's tap, installed on the `Base::Reader` before the
`XMLReader` takes its first chunk, since the XML reader's constructor
already parses. `endTap` drains what the parser left of the entry into
the sink before handing the stream back, so the bytes are the whole
entry whether or not Xerces read the trailing newline; it is called in
`restore(XMLReader&)` right after `Document::Restore`, ahead of
`readFiles()`, because the forward-only `ZipReader` moves off the entry
there. Once `readFiles()` has the blobs in the store the document calls
`TransactionLog::onRestore(path, docXml, blobs, schema)`, which shares
`snapshot()` with `onSave`: version 1 at `seq = 0` with the same manifest
shape, then a `restore` transaction whose script is
`{version, docxml, blobs, schema, path}`. Only an empty store is
initialised; a session store always is, and a store with history at
restore is the embedded mode's hash guard to build (16.4), so it warns
and leaves it. Not tapped: a partial load (16.1); not snapshotted: a
`Document.xml` the loader could not read (`RestoreError`). One
consequence of opening the store before the parse: the restored `Uid`
renames the transient directory the store sits in, and SQLite finds
its WAL sidecar by path, so `Document::onChanged(Uid)` calls
`TransactionLog::closeStore()` across the rename and `reopenStore()`
after it (the document drops the log if that fails). The seventh gtest
opens a saved file through both archive readers and checks the
version's value is byte-identical to the entry, the `restore` row is
the only transaction, the store's path is under the renamed directory,
and the next commit's ops follow it.

**The writer thread, as built** (2026-09-22, 20.2 decision 4). One
worker per log, started with it. The commit path on the main thread does
what needs the live document -- walks the transaction, decides which
copies are worth serialising (`isSame` against the live property, the
derived rule), numbers the transaction (`t.seq = ++_nextSeq`; the store's
`append` and `addVersion` honour a preset `seq`/`num`, so the pending
map is keyed to real sequence numbers at once) -- and posts one job: the
`LogTransaction`, its `LogOp`s, and a `ValueTask` per copy, each holding a
`shared_ptr<const Property>` to the copy, the tier, the op whose before
ref it fills and the earlier op whose after ref it resolves. The worker
runs jobs in order: `captureValue` under a `CaptureConfig` taken from the
document at open (schema, `PreferBinary`) so it never reads the document,
`putValue` (hash, zstd), `resolveAfter`, then `append`. Shared ownership
is `TransactionObject::PropData::shared`: the log takes the transaction's
own copy into it at commit, so the copy outlives the transaction's
deletion (undo off) or eviction until written, and the transaction stops
deleting a copy that is shared. Values the transaction does not hold --
the properties of a removed object, and the live values `resolvePending`
snapshots -- are `Copy()`d on the main thread, the way the undo system
copies, and the copy is what the worker gets. Reads never touch the
store directly: `store()` hands out a `FlushingStore` that waits for the
queue to drain before every call, so a reference kept across commits
(the panel, Python, the tests) stays correct; `readValue` flushes too;
`closeStore` and the destructor flush and join. A commit therefore costs
the walk, the `Copy()`s the undo system already pays, and a queue push.
The eighth gtest commits twenty 200 KB edits with undo off -- the
transactions are deleted at commit -- and reads every before value back
in order, then resolves the head.

**The property-supplied hash, as built** (2026-09-23, 20.2 decision
6b). `BlobReferrerProperty::contentBlob()` -- the blob holding the
property's whole content as it stands, or null -- and the capture
writer's `BlobRef` mode. `PropertyPartShape::Copy()` now carries `_blob`
(and `_blobMotion`) with the value, so the copy the undo system takes at
the first write after a save knows the file its geometry was written to;
`Save` under `BlobRef` writes `<Part ... hash="..."/>` and nothing else
(no export, no note to any manager), and the worker, on seeing a
`contentBlob()`, holds the handle in the log's `_blobs` so the document's
one store (16.2) keeps the file for as long as the log lives. A shape
that changed since its save has no blob (`dropBlob`) and exports as
before, on the worker. What it buys in practice: in an edit-save cadence
no shape is exported for the log at all -- the before copy holds the
blob, and the after ref resolves at the save's `resolvePending`, after
`makeBlob` has run. Not yet (done in 23.16): a blob held by the log surviving log
truncation or eviction (it is released with the log), and other
referrers (`PropertyFileIncluded` could answer the same way). The Part
gtest `TransactionLogShapeTest` checks an unsaved shape's value carries
the geometry and a saved one's is the hash fragment alone, under 512
bytes, with the file still in the store after the shape changed.

**Mode.** `TransactionLog` is a preference, 0 (off) by default and 1 for
`session`. The writer thread now exists; the mode stays a staging switch
until the log is the undo system (22.1). `local` and `embedded` are not
built.

**The browser panel, as built** (2026-09-22, section 22.2).
`Gui::DockWnd::TransactionLogView` (`src/Gui/TransactionLogView.{h,cpp}`),
registered as `Std_TransactionLogView`, in the bottom dock area of the
standard workbench, hidden by default (View -> Panels). It follows the
active document and refreshes on `signalCommitTransaction`,
`signalRecomputed`, undo and redo (deferred to the event loop, so the
recompute record written after `signalRecomputed` is seen). Three panes:
the transaction rows (seq, kind, origin, name, time, parent; the script
annotation as tooltip and on the context menu), the ops of the selected
transaction (container, property, type, before/after refs with
`pending` shown for an unresolved after, derived), and the value behind
the selected op (after, then before, fragment and attachment sizes) --
or, for a row with no ops, its `script` payload, which is how the
recompute and save records read. A filter box matches any column or the
script; "Resolve pending" calls `resolvePending()`. The status line
gives transactions, versions, pending refs, session and the store path.
Read-only in this cut; the manager operations arrive with their phases.

**Tests.** Six gtests (the sixth: a save makes one unnamed version whose
`Document.xml` value is byte-identical to the archive entry and hashes
to `docxml_hash`, `findVersion` matches it, the `save` row follows it,
pending refs are 0 after it, and `truncate` keeps its value). Five first: ops and refs of create/set/remove and their
resolution; an unchanged write logs nothing; a log of creates, sets, a
dynamic property and a remove replays into a fresh document whose
properties serialise byte-identical; implicit transactions group by
invocation with undo off and undo with it on; the session and recompute
records. The Python `Document` suite is unchanged with the log off. With
the log on, three of its undo cases see the open implicit transaction in
`UndoNames` -- the log-on semantics, not a bug, and the reason the mode
is a preference the suite does not set.

**The XML entries in the manifest, as built** (2026-09-23, revised the
same day). First cut: the Gui document tapped its own entry in
`SaveDocFile` / `RestoreDocFile` and handed the bytes to the App
document. Replaced when the checkout showed the gap: with `SplitXML` on,
every object's data is an entry of its own (`Obj.xml`), served by the
same `writeFiles()` / `readFiles()` loops, and a version without them is
not a file. So the tap is generic: `Base::Writer::setEntrySink` and
`Base::XMLReader::setEntrySink` hand every entry those loops serve to a
sink by name -- the writers serve each through `Writer::writeEntry`, the
readers through `XMLReader::serveEntry`, each tapping the entry's stream
around `SaveDocFile` / `RestoreDocFile` (draining, on the reader) when a
sink is set and costing nothing when none is. The document sets the sink
for the span of a save, a snapshot or a tapped restore and passes what
it collected, `Document.xml` first, as the `Entries` of `onSave` /
`onSnapshot` / `onRestore`; `snapshot()` stores each as a durable value
and lists each in the manifest, `docxml_hash` being the first's. Blob
entries are the manager's and never come this way. Verified in the GUI
on both paths: the manifest hashes of `Document.xml` and
`GuiDocument.xml` match the archive's, saved and opened.

**The versions pane, as built** (2026-09-23). The panel's first pane is
a tab widget -- Transactions, Versions -- and its second a stack that
follows it: the ops of the selected transaction, or the manifest of the
selected version (entry, source, hash). The versions rows show num,
kind, name, branch, seq, schema, created, the `Document.xml` hash and
the entry count, with the uuid as tooltip; they are rebuilt whole when
the count moves. Selecting a manifest row shows the entry's bytes (a
`value`) or the blob's path in the document's store (a `blob`) in the
value pane. Read-only still; naming, restore-to-here and trimming
arrive with their phases.

**The snapshot between saves, as built** (2026-09-23, 16.3 and 22.1).
`Document::snapshotToLog()` is a save's serialisation with the archive
left out -- what `AutoSaver` did for the recovery file, which it is to
replace: a `Base::NullWriter` (a writer whose sink discards) configured
like a save, `beginSave` + `collectFileBlobs()` so the changed shapes
are written into the store, `Document::Save` tapped into `Document.xml`,
`signalSaveDocument` + `writeFiles()` so the Gui entry comes through
its own tap, then `TransactionLog::onSnapshot` -- `snapshot()` again,
with a `snapshot` record -- and the manifest from `collected()`. Refused
inside an open transaction, during a restore, on a partial document and
while one is in progress. Taken on demand
(`Document.snapshotTransactionLog()`, the panel's Snapshot button) and
on the cadence: `TransactionLogSnapshotTransactions` (every N commits
since the last version) and `TransactionLogSnapshotSeconds` (the first
commit T seconds after it), both 0 -- off -- by default while the mode
is a staging switch; a save or a restore restarts the count. It runs on
the main thread at the end of the commit, which is the cost of a save's
object pass; decision 8's "background job" is not possible for
`Document.xml`, which only the main thread can produce, so the cadence
is the knob. The ninth gtest takes one on demand and checks the value
holds the document, then sets N=3 and counts the versions seven commits
make.

**Eviction, as built** (2026-09-23, 16.3). `TransactionStore::evictVersion`
removes a version row and its manifest, then the values nothing refers
to -- the same collection `truncate` runs, now shared as `collectValues`
and fixed to read every line of a value's attachment list (it read the
first only, so a value with two attachments would have lost the second
on truncation). The policy is `TransactionLog::evictVersions`, run by
the worker after every `addVersion`: with `TransactionLogKeepVersions`
> 0, the oldest unnamed versions beyond that count go; a named version
never, and never the newest. 0 (the default) keeps all. Count only for
now; the byte limit and the regeneration-cost weighting of 16.3 wait
for the tiers to carry a cost. The blobs a manifest names are not held
by the log yet (16.2), so eviction frees value rows only. The tenth
gtest keeps 2 across 4 snapshots and checks the survivors, their
values, and that no op's value went.

**The checkout, as built** (2026-09-23, 16.1). `Document::restoreVersion(num)`
materialises the version as an unpacked project under
`<transient>/history/checkout/` -- each `value` entry written from the
store, each blob copied from the document's blob store into `blobs/`,
which a directory restore reads by content -- and calls `restore(dir)`,
the path a file takes on open, so "rollback is a load" holds literally.
A `checkout` record (`{version}`) follows, the pending after refs of
the state that was left are dropped (they described values that are
gone), the cadence restarts, and the undo stack is cleared as by any
restore. The restore's own tap stays off during a checkout (it would
have taken a "version 1" of a version). Python
`Document.restoreTransactionVersion(num)`; the panel's versions rows
offer "Restore to version N" on their context menu and reload through
`signalFinishRestoreDocument`. Departures from 16.1 to settle later:
`GuiDocument.xml` is checked out with the rest, so the cameras come
back with the model (leaving it out would reset the view providers'
colours and visibility, which is worse); a missing blob throws rather
than falling back; nothing marks the document modified. The eleventh
gtest goes back to version 1, forward to 2, and edits between.

Phase 1 is complete with this, and the browser panel shows every record
it writes.

**Named versions, as built** (2026-09-23, 16.3).
`TransactionStore::nameVersion(num, name)` sets a version's kind to
`named` with the name, or back to `unnamed` with an empty one; eviction
skips named versions (the limit is read on the main thread when the
snapshot job is posted, so a test or a preference change cannot race
the worker). Python `Document.nameTransactionVersion(num, name)`; the
panel's versions context menu offers "Name version N...", "Rename" and
"Make unnamed". No record is written for the naming: the version row
carries it. Refusing to delete a version an external link pins (16.5)
waits for the links. The eviction gtest names a version and sees it
survive a limit of 1.

What remains of the versions story is the embedded mode (16.4,
`PropertyHistory`); phase 3 (undo over the log) starts from the
checkout.

**The embedded mode, plan** (2026-09-23, before building). Two things
the survey of the code settled:

- No referrer-interface extension is needed: `FileBlobManager::_pending`
  is a list of (hash, referrer) pairs and `assignRestoredBlob` is called
  once per blob, so a referrer restored several blobs tells them apart
  by `blob->hash()`. 16.4's "one small extension" is withdrawn.
- **The hash guard cannot be the `Document.xml` hash** for the version
  a save makes: the embedded database is a blob, and blobs are collected
  and fixed before `Document.xml` streams, so the copy inside the file
  cannot know the hash of the `Document.xml` it is inside. The guard is a
  **save id** instead: a uuid minted by the save, written into the
  embedded copy's `meta` (`save_id`) and into the document's `Version`
  property (`"<num> <uuid>"`, `num` the version this save becomes),
  together with the `LastModifiedDate` the save stamps. On open the two
  sides agree when the ids match and the date matches; a FreeCAD that
  knows nothing of the log round-trips both properties unchanged but
  restamps `LastModifiedDate`, which is what catches an edit made there.
  A match means the live log continues from the embedded copy and the
  file as found becomes version `num` (its `Document.xml` hash is taken
  then, as 16.6 does); a mismatch sets the copy aside and starts a
  history from the file as found (16.6), the closed-branch bookkeeping
  waiting for branches.

The pieces, in order: `App::PropertyHistory` (a `BlobReferrerProperty`
holding the database blob and a handle per blob the retained manifests
name, with each one's extension; `Save` writes hashes only, at schema 5;
`collectBlobs` notes them all); `TransactionStore::copyTo` (`VACUUM
INTO`) and the retention applied on the copy (unnamed versions and
`cache`-tier values dropped, ops kept under the 13.3 budget); the save
path (mode 2: flush, copy, retain, adopt the copy as a blob, set the
document-level dynamic `History` and `Version` properties before the
collect pass); the open path (the guard, then `TransactionLog` adopting
the copy as its store before the on-open snapshot); "save a copy without
history"; the panel's mode indicator.

**The embedded mode, as built** (2026-09-23, the first four pieces).
`PropertyHistory` as planned (`src/App/PropertyHistory.{h,cpp}`; a
`<History db="..." count="n">` element of `<Blob hash ext/>` children at
schema 5, `<History/>` below it, where the lack of a store means no
history travels -- which is "save a copy without history" for a save at
schema 4, the explicit command still to come). `TransactionStore::copyTo`
(`VACUUM INTO`) and `dropTier`; `TransactionLog::embed(saveDate)` flushes,
copies, evicts every unnamed version from the copy, drops the cache
tier, and stamps `meta` with `save_id`, `save_date` and
`version_counter` (the number this save becomes; `openStore` takes the
counter into `_nextVersion` so an adopted copy with every version
dropped still numbers on). `Document::embedHistory`, called from
`save(writer)` just before the collect pass when the mode is 2, adopts
the copy into the blob store and sets the two dynamic properties --
`NoModify`, so setting them opens no transaction -- or empties `History`
when the mode is not 2, so a stale copy never rides along. On open,
`Document::adoptEmbeddedHistory` runs after `readFiles()` and before
the on-open snapshot: it opens the copy read-only, compares its
`save_id` with `Version`'s and its `save_date` with `LastModifiedDate`,
and on agreement `TransactionLog::adoptStore` replaces the live store
with the copy (a new session row in it); the on-open snapshot then
becomes the version the counter names. A mismatch warns and leaves the
fresh history to start from the file as found (16.6). The twelfth gtest
saves in mode 2 with a named version, opens a copy elsewhere, finds the
ops and the named version, restores it, then saves in mode 1 and finds
the history gone. Verified in the GUI: `Version "2 <uuid>"`, the copy
continuing with 19 transactions in session 2, the named version
restored from the file. Two things to settle: the history `.db` blob is
a document blob like any other, so a version taken after an embedded
save lists it in its manifest (harmless, but each embedded save adds
one); and the closed-branch bookkeeping of 16.6 on a mismatch waits for
branches. Then, the same day: `Document::saveCopy(path, withHistory)`
-- `Document.saveCopy(path, False)` from Python -- writes the copy with
`History` emptied and puts the live document's property back once the
copy is written, so the file the author keeps still carries the
history; the gtest checks the copy has no `.db` entry. The panel's
status line names the mode (`[session]` / `[embedded]`). A menu entry
for the copy waits for the Gui command set of phase 2.

## 22. The end state, and the browser as a development tool (user, 2026-09-22)

Two rulings, recorded before phase 1 continues. Where they disagree with
sections 12, 13.3, 14 or 15, they win.

### 22.1 The log replaces autosave and undo/redo; it is not a setting

**The final aim is that the transaction log *is* the undo/redo system
and *is* the autosave / recovery system.** Neither of those is a user
setting, and so neither is the log: there is no `off` mode, no
`UndoMode == 0`-style switch that runs a document without it, and no
preference that decides whether a document is logged. Every open
document has a log, the way it has a transaction stack today. What
section 14 said about "the log has its own switch (`off`), so a batch
script can still run with neither" is withdrawn; what 13.3 listed as an
`off` mode is gone.

What follows from it:

- **Undo/redo are the log's undo (section 12)** in the end state -- the
  in-memory `Transaction` copies remain as the hot window that makes the
  common case fast, but they are an implementation cache of the log,
  not a parallel system with its own on/off. The phase-3 work is not
  "undo over the log as an alternative"; it is the replacement.
- **Autosave is the log.** Today's `AutoSave`/recovery-file machinery
  (a periodic copy of the whole document into the recovery directory) is
  replaced by the unnamed-version cadence of 16.3 plus replay of the
  log's tail on the next open (phase 7). The user-facing setting that
  survives is at most the cadence; the existence of recovery is not
  optional.
- **The log's cost therefore has to be acceptable for everybody**, not
  just for users who opted in. This is why the writer thread (20.2
  decision 4) moves from "when the sketch case is over a few
  milliseconds" to a hard prerequisite of turning the log on by default,
  and why the `TransactionLog` preference of section 21 is a *staging*
  switch that exists only while the log is being built: it comes out
  once the log is the undo system.
- **The only user choice is where the history lives when the file is
  saved**: bundle the log with the file (`embedded`, 13.3 / 16.4) or
  not (`session` -- or `local`, if adopted -- with the "save a copy
  without history" and the privacy rules of 13.3 unchanged). That is a
  per-document choice with a preference for the default. It decides
  what travels, never whether the log runs.

### 22.2 The log browser is built along the way, as a dockable panel

The phase-2 browser is **not deferred until phase 1 is complete**. It
is developed alongside the store, from now, as a **dockable panel** in
`Gui` (a `QDockWidget` in the family of the combo view / selection view
/ report view -- see `Gui/DockWindowManager`), and it serves two
purposes at once:

- **A final deliverable**: the history panel of section 15 phase 2 --
  transactions and versions by name, time, origin and kind; ops per
  transaction with their container, property and value refs; filter by
  object and property; the script annotation; the environment / session
  / recompute records; and, as the phases land them, "restore to here",
  version naming, branch switching and the manager operations (trim,
  retention, embed or not, save without history).
- **Verification during development**: the first thing to look at when
  a record is wrong. Every phase-1 item from here on (the `save` record,
  unnamed versions, history from a file, the writer thread) ships with
  the panel able to show what it wrote, so that the log is inspected in
  the running application and not only through the Python read API and
  the gtests. Where a gtest asserts a row, the panel shows the same row.

Consequences for the build order: the panel's first cut -- a read-only
list of transactions with an ops detail view over the section 21 Python
read API (`getTransactionLog`, `getTransactionOps`,
`getTransactionValue`) -- is the next Gui work item, before the `save`
record, and it grows a column or a view with each record added. It is
a *manager* as well as a browser: the operations that change the log
(restore, name a version, trim, switch branch, choose embedding) live
on it once the phases that define them exist, and not in scattered
menu entries.


## 23. Entities (user, 2026-09-23)

Decided after phase 1, before phase 3, and this section overrides 9.3's
"full is the only encoding", 16.1's "a version is a materialised FCStd
inside the store" and 16.2's split between value rows and the blob
manager where they disagree. The goal is all three cost centres of
phase 1 at once: the whole-array op values, the `Document.xml` and
`Obj.xml` a version duplicates, and the main-thread object pass a
snapshot costs.

### 23.1 One kind of thing in the store

Everything the log holds bytes for is an **entity**: a property's
`Save` fragment, the file a property hands to `addFile`, a shape blob,
`Document.xml` and `GuiDocument.xml`, and the composites of 23.3.

```
entity(hash PRIMARY KEY, kind, enc, base, tier, size, data)
ref(entity, target, role, name, PRIMARY KEY(entity, role, name, target))
```

- `hash` is the identity: SHA-1 of the *full* content (for a property
  value, the fragment and its ordered attachment list, as now). It never
  depends on how the row happens to be stored, so a re-encode is an
  in-place rewrite of `enc`/`base`/`data` and nothing that names the
  entity moves.
- `kind`: `prop`, `attach`, `blob`, `xml`, `skeleton`, `composite`.
- `enc`: `raw`, `zstd`, or `delta` with `base` naming the entity the
  patch applies to. `zstd --patch-from` is the codec; nothing per type
  until a measurement says a type needs one.
- `ref` is every edge, one kind of edge for the collector: an
  attachment (`role=attach`, `name` the file name), a delta base
  (`role=base`), a composite's parts (`role=part`, `name` the property).
  The old `attach` column and the manifest's `source` distinction fold
  into it.
- Where the bytes live is a backend detail chosen by size: small inline
  in `data`, large as a file in the document's `FileBlobManager`, which
  becomes the entity store's file backend rather than a separate world
  the manifest points into. This is what closes 16.2's open item: the
  log holds the blobs a version names, through `ref`, the same way it
  holds everything else.

  *As built (note of 2026-09-24):* the "large as a file" branch was never
  built. Every entity the log makes is inline in `entity.data` (raw, zstd
  or a patch), whatever its size; `enc = file` only ever names a blob a
  save or a snapshot already made (23.16). The files the log causes are
  the snapshot's `storeBlob`s, the ones a cold undo re-adopts (24.3), the
  embedded copy and the checkout directory. `docs/FileBlobsManager.md`
  sec 15 (branch PartDesignPort, `debe8e263f`, design only) proposes a
  pack store under `FileBlobManager` -- blobs as ranges in segment files
  behind a SQLite index -- which takes the per-file cost out of those
  too; the log relies only on the handle, `read()` and `adoptBytes()`.

### 23.2 The policy: newest full, older as reverse delta

One rule for every kind: **the newest entity in a chain is stored full;
an older one is a reverse delta against the newer one that superseded
it; a chain is re-anchored with a full entity when the hop count to a
full one reaches K, or when the patch exceeds a fraction of the full.**
The hop count is counted across kinds -- a `prop` based on a `skeleton`
based on another is two hops -- or the bound means nothing. Named
versions are full anchors, always.

Direction was the decision, because it is what makes trimming cheap or
expensive:

- Forward (new = patch on old), the first draft: evicting version N --
  and eviction is oldest-unnamed-first, the common case -- leaves N+1's
  rows with a dangling base, so either N's rows stay alive and the trim
  frees nothing, or N+1's changed rows are re-encoded full on every
  eviction. And the newest version, the one crash recovery and embed
  want, sits at the end of the longest chain.
- Reverse (old = patch on new), RCS and git-pack style: taking version
  N+1 re-encodes N's changed entities against N+1's, on the worker, with
  N still full at that moment so nothing is reconstructed. Evicting the
  oldest is dropping a leaf; the newest is always full; a middle
  eviction leaves the survivor's base alive as an orphan anchor, which
  is not waste (those are the bytes the survivor needs) and which the
  K-bound keeps rare. A rebase pass is an optimisation, not a
  correctness need.

The fraction rule is what lets blobs in without a special case: text
BREP renumbers, and when `patch-from` finds nothing the patch exceeds
the fraction and the row stays full. Measure before assuming a blob
delta ever fires.

For op values the same rule reads: a `set`'s after is what the live
document holds and what the next op's before will be, so it is the full
one; the before it superseded becomes a patch against it.

### 23.3 Composites: every container's XML is a skeleton plus parts

Under `SplitXML` (the default, `Document.cpp:1051`; the Gui side splits
per view provider too, `<Name>.Gui.xml`) every XML entry of the archive
is one `PropertyContainer`'s serialisation: the document, an object, the
Gui document, a view provider. Each is written by one loop,
`PropertyContainer::Save` (`PropertyContainer.cpp:322`), plus what the
container writes around it -- the `<Properties Count>` element, the
`<_Property>` status rows, the `<Property name type status>` wrapper
with the dynamic-property metadata, and for `Document.xml` the document
attributes and the `<Objects>` list. So one mechanism serves all four:

```
composite = skeleton entity + ordered [ (property name, part entity) ... ]
```

The **skeleton** is the container's serialisation with each property
body replaced by a positional placeholder; the **parts** are the
property entities the log already has. The XML bytes are never stored:
checkout materialises them by substitution and hands `restore(dir)` an
ordinary file, so "a version is a saved file" (16.1) still holds -- it
is just never stored as one. Two things follow from the placeholders
being positional rather than hashes:

- The skeleton changes only when the *meta* changes -- a property
  added, a status bit, a doc string, an object added to the document. A
  snapshot that only moved values hashes the same skeleton, and its
  reverse-delta chain has almost nothing in it.
- Status lives in the skeleton, which every snapshot re-captures by
  running the loop without the bodies (a few hundred bytes per object,
  no `Save` calls). No status op is needed.

**The hook.** `Base::Writer` gains a `PropertySink`, sibling of the
entry sink, consulted inside the loop for each property while the loop
still writes the wrapper:

```
lookup(container, name, prop) -> hash   // the log has this value: skip Save
store(container, name, bytes, files) -> hash   // Save ran: keep the bytes
```

Two modes of one sink. **Record**, a real save: every property is
serialised as now, the bytes go to the archive *and* through a
per-property tap into `store`; the version taken at save is a composite
for free, and this replaces the entry-level tap of section 21.
**Compose**, a snapshot into the `NullWriter`: `lookup` hits for every
property the log has current and its `Save` is not called; only the
misses -- derived properties under `none`, anything untouched since
version 1 whose hash the previous composite cannot supply -- run `Save`
and `store`. `addFile` during a stored `Save` becomes the part's
`attach` refs; under `BlobRef` the shape's `<Part hash>` fragment
already is one.

**Shared defaults stay.** The `<Defaults>` block (`docs/DocumentLoad.md`
6 and 7) is not a storage dedup, which the log now does by hash; it is
the reader's cost -- 540842 properties to 42233, 4.71s to 0.61s, open
18.9s to 13.0s on the 17800-object document -- and both files the log
produces, the save and the checkout, are read by that reader. What
changes: in compose mode the elision test (`serializeForCompare`,
`PropertyContainer.cpp:375`) is exactly the `Save` compose mode skips,
so `SharedDefaults::Entry` records the hash of its `content` and the
test becomes status, `memSize`, hash. `build()` serialises at canonical
settings (forced XML, no indentation) while the log captures under
`CaptureConfig`; the capture of an eligible property is made canonical
so the two hashes are the same bytes. The elision then also applies at
checkout, so a materialised version loads as fast as a saved file.

### 23.4 What a snapshot costs now

On the main thread: `Copy()` of the pending properties, which
`resolvePending` already paid; the skeleton loop per container; `Save`
of the parts the log has not got. Everything else -- hashing, delta,
re-anchoring, the store writes -- is the worker's. Decision 8's
"background job" is reached for everything but the copies, which is as
far as it can go while only the main thread can read the document.

### 23.5 Trimming under entities

One collector, following `ref` edges from the roots (op refs, version
composites), replaces `collectValues` and the manager's own refcount
for what the log holds. Then:

- Evicting the oldest version drops its composites; parts nothing else
  reaches go. Reverse deltas mean nothing points at them as a base.
- Evicting a middle version: a part it held that a delta elsewhere uses
  as `base` stays as an orphan anchor. K bounds how many.
- Op truncation: a `prop` entity survives while any op, composite or
  delta references it.
- `embed()`: named versions are full anchors, so dropping the unnamed
  ones is a pure delete and the embedded copy is the anchors plus the
  ops under the 13.3 budget.

### 23.6 Escaping `aboutToSetValue` is a defect (user, 2026-09-23)

A write that reaches a property's value without `aboutToSetValue` was
a bug before the log existed: it is invisible to undo, to `touch()` and
so to recompute, to `signalChangedObject` and so to the view provider,
to expression dependents and to the modified flag. A composed version
only adds one more reader that notices. The ruling: **the value is
immutable except through the property; every path that violates that
is fixed at the site, never accommodated by relaxing a guard.** The
guards exist to find the sites:

1. **Compile time, by construction.** The public accessors are already
   const-correct (`getValues()` returns `const ListT&` everywhere, no
   public non-const reference to internals in `src/App`). What is not:
   `_lValueList` / `_lValue` are `protected`, and 20 files outside
   `src/App/Property*` write them (Part `PropertyGeometryList` and
   `PropertyTopoShape[List]`, Sketcher `PropertyConstraintList`,
   TechDraw's four cosmetic lists, Mesh, Points, `ViewProviderExt`,
   `PropertyVisualLayerList`). They go `private`, and the one way to a
   non-const reference is a guard -- `AtomicPropertyChange` already
   exists and is `friend`ed as `atomic_change` in these classes --
   whose constructor is `aboutToSetValue` and destructor `hasSetValue`.
   Forgetting it does not compile. Second, `PropertyGeometryList` hands
   out `const std::vector<Geometry*>&` and a `Geometry*` is mutable;
   Sketcher edits through it (16 `const_cast`s and direct writes). The
   element becomes `const Geometry*`, with non-const geometry only
   through the same guard; Sketcher is the biggest whole-array property
   and the one place a leak would corrupt a composed version silently.
2. **Run time, the OCCT lock.** `TopoDS_TShape::Bit_Locked`
   (`TopoDS_TShape.hxx:80`), per TShape, enforced: `BRep_Builder` throws
   `TopoDS_LockedShape` from 31 sites (`MakeFace`/`MakeEdge`, every
   `Update*`, `Range`, `Continuity`, `SameParameter`, `SameRange`,
   `Degenerated`, `NaturalRestriction`), and `Bit_Free` guards topology
   through `TopoDS_Builder::Add`/`Remove`. FreeCAD uses neither flag
   today. But `Locked` also refuses `UpdateFace(face, Poly_Triangulation)`
   and the edge-polygon `UpdateEdge` overloads, which is what `BRepMesh`
   writes through, so a shape locked as a value could not be displayed.
   Rather than change what `Locked` means (user, 2026-09-23: keep
   upstream's flag intact), the fork on `LinkVibe-801` adds a bit of its
   own: **`Bit_Immutable` (0x1000), `TopoDS_TShape::Immutable()` and
   `TopoDS_Shape::Immutable()`**. Every geometry, tolerance, range, flag
   and topology setter that checks `Locked()` checks `Immutable()` too
   (25 sites in `BRep_Builder`, `BRepTools::UpdateFaceUVPoints`, and
   `TopoDS_Builder::Add`/`Remove` throwing `TopoDS_FrozenShape`); the six
   cache setters -- face triangulation, `Poly_Polygon3D`,
   `Poly_PolygonOnTriangulation` (one and two), `Poly_Polygon2D` (one and
   two) -- do not. No temporary unlock anywhere: the flag is set once by
   `PropertyPartShape::setValue` (step 5), walking every TShape since the
   flag is not recursive, and never cleared. The triangulation is a
   cache, not the value: every writer passes `withTriangles = false`
   (`TopoShape.cpp:934`, `PropertyTopoShape.cpp:1365`), so it never
   reaches a blob or a hash; and it cannot go stale, being a pure
   function of the frozen geometry, the tolerances and the requested
   deflection, the last of which `BRepMesh_IncrementalMesh` already
   checks and remeshes for. `BRepTools::Clean` through the same setters
   is a cache drop.
3. **Run time, the audit.** `TransactionLogVerify` (on in debug builds)
   makes compose mode serialise anyway and compare the hash; a mismatch
   names the container and property. This is the net for what neither
   tier can reach: a `Py::Object` a script mutates in place, a
   `TopoDS_Shape` edited through a `const_cast`, and any other cast.

Some existing code will start throwing or failing to compile when the
guards go in. That is the bug list, not collateral damage.

### 23.7 Build order

1. The `entity` and `ref` tables, `value`/`attach` migrated onto them,
   the reader resolving delta chains, the one collector.
2. Reverse delta at `putValue` for `prop` and `xml`, K and the fraction
   as preferences (`TransactionLogDeltaHops`, `TransactionLogDeltaRatio`);
   measured on a sketch drag sequence.
3. The `PropertySink`, skeletons and composites; the composed snapshot;
   the hash-based defaults elision; the verify switch; the byte-identity
   gtest (a composed entry equals the archive's).
4. The private members and the guard (23.6 tier 1), then
   `PropertyGeometryList`'s element type as its own commit.
5. `PropertyPartShape::setValue` setting `Immutable` on every TShape
   (the fork side is built, 23.8).
6. Blobs through the entity store, deltas gated on the measurement
   (as built, 23.16).

### 23.8 Steps 1 and 2 as built (2026-09-23)

**The entity table.** `entity(hash, kind, enc, base, tier, size, data)`
and `ref(entity, target, role, name, seq)` replace `value` and its
`attach` column; a schema-1 store migrates on open (`migrateValues`,
the attachment lines become `attach` refs, manifest `source='value'`
becomes `entity`). `putEntity` writes the row and its refs in one SQL
transaction and adds a `base` ref for a delta; `reencodeEntity` swaps
`enc`/`base`/`data` under the same hash and replaces the `base` ref;
`basedOn` lists the deltas on an entity. The collector is one recursive
CTE from the op refs and the manifest entries over every role of edge,
so an attachment of an attachment, a delta's base and a composite's
part are all held the same way; `dropTier` additionally spares a row a
surviving delta is based on. The codec is zstd's patch-from
(`ZSTD_CCtx_refPrefix` with the window sized to the base, long-distance
matching on; `deltaEncode` / `deltaDecode`), and `readBytes` decodes a
chain through its bases with a depth guard. The gtest
`deltaChainReadsAndHolds` covers the codec, a two-hop chain and the
collector keeping both bases while a manifest roots the oldest.

**The policy.** `TransactionLog::supersede(older, newer)`, on the
worker: no-op when hops are 0, the older is already a delta, under 128
bytes, or not a `prop`/`xml` (attachments and blobs wait for step 6, 23.16);
refuses a loop (the newer must not decode through the older) and a
chain that would exceed `TransactionLogDeltaHops` (default 8) below the
older; encodes and keeps the patch only under
`TransactionLogDeltaRatio` percent (default 50) of the older's stored
size. Both preferences are read on the main thread in `post()` into
atomics, so the worker never touches `DocumentParams`. Two call sites:
`writeValues`, when a task resolves an earlier op's after -- that op's
before is the older of the pair (`getOp` fetches it); and the snapshot
job, which matches the previous version's entity entries by name and
supersedes each whose hash changed, after `addVersion`. The gtest
`reverseDeltasFollowSupersession` edits a 5000-element list six times
with hops=3 and finds the run re-anchored, every delta under a quarter
of its full size, every value reading back at its full length, the
older of two versions' `Document.xml` a patch on the newer's, and the
checkout of the older correct.

One consequence the eviction gtest now states: a named version whose
`Document.xml` is a patch toward the next version's keeps that next
entity alive as its anchor after the next version is evicted (23.5's
orphan). Not measured yet: the ratio on real sketch geometry and BREP,
which is what steps 2's and 6's gates are for.

**The OCCT side of step 5, as built** (2026-09-23, occt commit
5d38403287 on `LinkVibe-801`). `Bit_Immutable` as 23.6 describes;
`TopoDS_TShape.hxx`, `TopoDS_Shape.hxx`, `BRep_Builder.cxx` (25 sites),
`BRepTools.cxx`, `TopoDS_Builder.cxx`. The Part gtest
`ImmutableShapeTest.meshesButRefusesEdits` sets the flag on every
TShape of a box, meshes it with `BRepMesh_IncrementalMesh` and finds
the triangulation, then sees `UpdateFace(tol)`, `UpdateEdge(tol)`,
`Range` throw `TopoDS_LockedShape` and `Add`/`Remove` throw
`TopoDS_FrozenShape`, with `Locked()` still false. One thing to watch
when `PropertyPartShape::setValue` starts setting it: a mesher that
"fixes" a shape on the way -- `BRepMesh_ShapeTool::CheckAndUpdateFlags`
writes `SameParameter`/`SameRange` when it finds them wrong -- will now
throw instead. That is 23.6's rule in action (the fix belongs where the
shape was made), not a reason to widen the carve-out.

### 23.9 Step 3 as built (2026-09-23)

**The capture.** `Base::Writer` captures an entry between `beginCapture()`
and `endCapture(name)` -- `writeEntry()` does it for every file entry,
`Document::save` and `snapshotToLog` for `Document.xml` -- as a
`Base::EntryCapture`: a list of segments, `Text` bytes, a `Count` where a
container wrote its `<Properties Count=` (the base count of non-part
elements and the container's name), and a `Part` per property with the
`<Property ...>` wrapper in `open`, the body in `text`, `</Property>` in
`close`, the property id as `key`, and the default's hash in `elide` when
the cheap tests (type, status, memSize) say it may be elided. The entry
sink now receives the capture, not bytes; `EntryCapture::bytes()` is the
entry byte for byte. A part's body is written at indentation 0, so its
bytes are what `captureValue` produces for the same property and the two
hash the same; the wrapper keeps the file's indentation. A part begun
inside another part's body (a container a property writes) is not a
part, and its marks are ignored.

**The sink.** `Base::PropertySink` has `claim(container, name, prop, key)`,
`verify()`, `composes()`. `PropertyContainer::Save` calls
`writer.beginPart` before each non-transient property, `beginBody` /
`endBody` around its `Save`, `endPart` after the close; a claimed
property's `Save` is skipped unless the sink verifies. Under a composing
sink the pre-loop elision serialises nothing: an eligible property whose
type, status and memSize agree with the default stays in with the
default's hash (`SharedDefaults::Entry::hash`, set by `build()`), and the
worker leaves it out when its part hashes the same.
`TransactionLog::beginSnapshot(compose)` resolves the pending refs, then
hands the writer `TransactionLog::Sink`: record mode claims nothing;
compose mode claims a key that is in `_recorded` (the main thread's set
of property ids whose newest value the worker holds -- every copy posted
with its key, every part a snapshot stored; removed by a `set` whose
value is not kept, cleared by a checkout, and by `onUndoRedo` for what
an undo or redo touched, since undo is not an op until sec 12) and not
in `_pending`.

**One format.** A saved part, a captured op value and a composed miss
have to be the same bytes or the log holds three copies of one value.
The archive save (`Document::save` with an archive) writes under the
writer's defaults -- file version 1, XML not forced, no split -- and the
`ZipWriter` stream's `fixed` float format; the directory save alone sets
file version 2 and the `ForceXML`/`SplitXML` properties. The snapshot's
`NullWriter` and `captureValue`'s writer were configured like the
directory save and their streams were not `fixed`, so nothing matched.
Now both take the archive's configuration, and every writer's stream
(`StringWriter`, `NullWriter`, `FileWriter`, the capture) is `fixed` like
the `ZipWriter`'s: a float writes as `1.0000000000000000` everywhere,
the `<Defaults>` block included, where the `StringWriter` used to write
`1`. A file's bytes change only in that block and in directory saves;
every reader parses both.

**The composite.** On the worker, `putComposite` resolves each part -- a
claimed one from `_hashById` (a claim the worker cannot honour throws,
and the snapshot fails by name; a verified one whose fresh hash differs
is reported as 23.6's defect and the fresh bytes win), a miss stored as a
`prop` entity -- drops the parts equal to their `elide`, fixes the counts,
builds the skeleton (a `skeleton` entity) and the composite: a text of
`skeleton <hash>`, `c <container>`, `p <offset> <hash> <name>` lines, a
`composite` entity hashed over that text with a `skeleton` ref and one
`part` ref per distinct part hash. The manifest names the composite;
`LogVersion::docxml_hash` is the SHA-1 of the entry's bytes when they
were all in hand (record mode -- a file on disk still matches its
version) and the composite's hash otherwise. `readValue` of a composite
composes it (`composeEntry`), so the checkout, the panel and the Python
API read a version as before. An entry with no parts (as read at restore)
stays an `xml` entity. Supersession runs one level down too: the previous
version's composite, skeleton and parts (matched by container and name)
toward this version's; `supersede` accepts `skeleton` and `composite`.
`dropTier` now spares what a manifest reaches through refs, since a
version's parts are held through its composite.

**Verify switch.** `TransactionLogVerify` (off; always on under
`FC_DEBUG`). Gtest `composedSnapshotIsTheFile`: a non-split save with a
defaults block is a composite whose composition is the archive entry and
whose `Obj.Integer` part is the op's after value, with the untouched
object's defaults elided; a snapshot with nothing changed composes the
same bytes under verification; a change replaces one part and keeps the
skeleton; checkouts of both are right; a snapshot after an undo carries
the undone value.

Not done here: attachments of a part (`addFile` during a property's
`Save`) are archive entries of their own and stay manifest entries, so a
part's hash is its fragment alone; at schema 5 the shapes are blobs and
the lists over `InlineListSize` are the ones that write one. Not measured: the composite's size on the
17800-object document, and what compose mode saves on its main thread.

### 23.10 Step 4, first half, as built (2026-09-23)

**The private list.** `PropertyListsT::_lValueList` is private. Reads go
through `getValues()` (about 340 sites rewritten, in `src/App` and the
Mesh, Points, Part, Sketcher Gui modules); the one non-const path is
`mutableValues(atomic_change& guard)`, protected, which marks the guard
first, so a write is bracketed by `aboutToSetValue()` and
`hasSetValue()` whatever the caller forgets, and a write without a guard
does not compile. The twenty or so write sites were the `Copy()` bodies
(now guard + `mutableValues` on the fresh copy: a container-less
`hasSetValue` is a `Touched` bit and nothing else), `PropertyLinkList`'s
two `setSize` overloads, the rotate loops of the Mesh and Points
curvature and normal lists (already under a guard, now through it), and
`PropertyDiffuseColor::setAppearance`, which cleared the base list in the
view provider's constructor: guarded, that clear was the first
`hasSetValue` the view provider ever saw, before its Coin nodes existed
(`ADD_PROPERTY` writes its default before attaching the container, so it
signals nothing), and `applyShapeAppearance` crashed the golden render
tests. The write is gone: `DiffuseColor`'s `ADD_PROPERTY` default is the
empty list now. `PropertyMap` and
`PropertyLinkSubList` keep their own `_lValueList`: they are not
`PropertyListsT`, and they were never written from outside.

**The hand-written lists.** `PropertyGeometryList`, `PropertyTopoShapeList`,
`PropertyShapeHistory`, Sketcher's `PropertyConstraintList` and TechDraw's
four cosmetic lists own their vectors, already private. Their escapes
were the seven `setSize` overrides, which resized without
`aboutToSetValue` (`PropertyListsBase::setSize` is what Python reaches),
now bracketed and a no-op at the same size; and
`PropertyGeometryList::swapValues`, which swapped without one and had no
caller, removed.

**The bug it found.** `_PropertyVectorList::restoreStream` read the
stream into the *live* list -- empty at restore, so it read nothing --
and then `setValues` the zero-filled `values` it had sized: every normal
list restored from a file stream (Mesh and Points `PropertyNormalList`)
came back as zeros. The compile error on the now-const `getValues()` was
the finder; the loop reads into `values` now.

The second half, `PropertyGeometryList`'s element type, is 23.11.

### 23.11 Step 4, second half, as built (2026-09-23)

**The const element.** `PropertyGeometryList` holds
`std::vector<const Geometry*>`; `getValues()` and `operator[]` hand out
const pointers, `setValues` takes const-element vectors (a `Geometry*`
overload converts for a caller holding what it just made), and the one
non-const door is `mutableValue(atomic_change& guard, idx)`, which marks
the guard first -- the property owns the geometry, so the cast lives
there and nowhere else. The `std::vector<Geometry*>` declarations in
Part, Sketcher and Sandbox (31 files) became const-element, except the ones that
hold geometry the caller made and still has to write: `Sketch::extractGeometry`,
`getSymmetric`, `replaceGeometries`' input, the trim and split arcs, and
the symmetry handler's. Two things had made the element type toothless
and went with it: `Geometry::mirror/rotate/scale/transform/translate`
were const (the handle is a pointer, so a const geometry could be moved
in place) and are not any more; and a transient extension -- the solver
diagnosis, the migration marker -- is not the value (never saved, not
compared by `hasSameExtensions`, in no hash), so
`setTransientExtension`/`deleteTransientExtension` are const doors that
refuse a persistent extension.

**What the compiler found.** Every site that wrote a property's geometry
behind `aboutToSetValue`, fixed at the site as 23.6 rules:

- `SketchObject::extend` on an arc called `setRange` on the held
  geometry: no undo copy, no touch, no recompute. It edits a copy and
  `set1Value`s it.
- `addCopy` in move-only mode wrote the held geometry and then set the
  same pointers back, so the undo copy was taken after the move. It
  moves copies.
- The constraint state on geometry -- `InternalType`, `Blocked`, both
  saved -- was written in place by `addGeometryState`,
  `removeGeometryState` (both `const`) and `synchroniseGeometryState`.
  They go through the guard, marking it only when the state differs, so
  adding a Block constraint is now a Geometry op as well as a Constraints
  op, and undo restores both.
- `onChanged(Geometry)` assigned missing ids and applied the migration
  in place; `onChanged(ExternalGeo)` cleared Detached, fixed duplicate
  ids, migrated refs; `updateGeometryRefs` corrected refs and reset
  `RefIndex` in place and then set the same pointers back. All through
  the guard; a change nested inside `hasSetValue` folds into the change
  being signalled (`signalCounter`), so nothing recurses.
- `rebuildExternalGeometry` cleared `Sync` and set `Missing` on every
  held external geometry before setting the values. The fresh geometry
  it made is written directly, a held one is copied first, and only
  when a flag actually changes.
- Eight clone-then-write sites (`setExternalFlags`, `detachExternal`,
  `fixExternalGeometry`, construction toggling, `setGeometryId(s)`, the
  id swap command, ...) cloned into the const slot and then wrote through
  the const pointer; they write the clone.
- `SketchObject::Save`, a `const` method, wrote `RefIndex` into every
  external geometry (-1, or the export index) through a `const_cast` of
  the property, so an export changed the value it was saving. The index
  now rides on the writer: `ExternalGeometryExtension::ExportRefIndex`,
  installed by `Save` for the writer it is given while `isExporting()`,
  is what `saveAttributes`/`preSave` consult when the extension holds no
  index of its own. The reset to -1 is gone -- a restored `RefIndex` is
  reset by `updateGeometryRefs`, through the guard.
- `ViewProviderSketch::draw` attached a `ViewProviderSketchGeometryExtension`
  to the sketch's B-spline pole circles to remember the factor it draws
  them at, arguing it was representation only. The extension is a
  `GeometryPersistenceExtension`: it was saved with the geometry, and
  `updateSolverExtension` put it on the solver's copy so that the next
  solve wrote it into the property. The factor lives in
  `EditData::PoleScaleFactor` by GeoId now, refilled by every `draw()`
  and read through `poleScaleFactor()`, which still honours a factor a
  geometry from an older file carries.

Gtests in `tests/src/Mod/Sketcher/App/SketchObjectChanges.cpp`: an
extended arc is a new element of a touched property; a Block constraint
touches Geometry when added and when removed, a Horizontal one does not;
a save writes no `RefIndex` and touches nothing, an `ExportRefIndex` on
the writer makes it write the index, and the writer forgets it after.
Sketcher 121/121, Part `ImmutableShapeTest` (against the refreshed OCCT
install, which had predated the flag on this box), log and property
gtests green; ctest at the 13 guest-image failures that are not the
log's; Python `TestSketcherApp`, `TestPartApp`, `Document`,
`MeshTestsApp` OK.

### 23.12 Step 5 as built, behind a switch (2026-09-24)

**The freeze.** Both `PropertyPartShape::setValue` overloads mark every
TShape of the new value `Immutable` (`freeze` in `PropertyTopoShape.cpp`),
children before their parent so that a marked TShape stands for its
subtree and a sub-shape shared with an earlier value is not walked again.
Restore, the blob and store serves and `Paste` all arrive through
`setValue`. `transformGeometry` was the one write that did not: it
rewrote `_Shape` in place and never updated `_ShapeNoName`; it now makes
the transformed shape from the held one and sets it. The freeze runs only
under `PartParams` `ImmutableShapeValues`, **off by default**, for the
reason below. Gtests in `tests/src/Mod/Part/App/ImmutableShape.cpp`:
every TShape frozen after a set (both overloads, a compound sharing a
frozen solid), the switch off leaving the value alone, `transformGeometry`
a new frozen value that touches the owner.

**The OCCT side, beyond 23.8.** `BOPAlgo_PaveFiller::SetNonDestructive`
switches a boolean to non-destructive mode for a `Locked` argument; it
now does so for an `Immutable` one too, or every boolean on a property's
shape would tolerance-fix its arguments in place (gtest
`ImmutableShapeTest.booleanGoesNonDestructive`).

**What turning it on hits.** Measured with a throw-logging preload over
both suites (`TopoDS_LockedShape`/`FrozenShape`, backtraced at the throw,
so a caller that swallows the exception is counted too): ctest 795/797
(`FeaturePartFuseTest.testRefine`, whose refine fails silently, and
`SketchObjectTest.testAddExternalCurvedFaceFromTheSide`), Python 80
failures and 50 errors (BIM 53, CAM 38, PartDesign 29, FEM 3, Sketcher 3,
Draft 2, Part 2), 335 throws in the Python run:

| Throws | Setter | Reached from |
|---|---|---|
| 300 | `SameRange` (the reset of a forced `SameParameter`) | `BRepLib_MakeFace(wire)`: `Part.Face`, `FaceMakerBullseye`, `FaceMakerCheese`, `ModelRefine`, `BRepOffsetAPI_MakeOffset` |
| 12 | (in `BRepLib_MakeWire::Add`) | `Part.Wire` |
| 6 | `UpdateEdge` with a pcurve | `BRepSweep_Prism`/`Revol`, `ThruSections`, `BRepFill_Generator`, `BRepOffset_MakeOffset`, `ChFi3d`, `HLRTopoBRep_OutLiner` |
| 5 | `SameParameter` | as above |

None of it is FreeCAD misusing a value: OCCT keeps an edge's pcurve for
every face the edge bounds inside the edge, so building topology on
shared edges completes them in place. The same happens without the
freeze and changes the value's bytes: a wire's BREP is unchanged by a
planar `Part.Face` (a pcurve on a plane is computed on demand and never
stored) except for the `Free` bit, but extruding that face adds the
side cylinder and two pcurves on it to the wire's own BREP (695 -> 1046
bytes). So a saved shape depends on what was later built from it.

Also found: `BRepLib`'s `UpdShTol` raises a vertex tolerance through
`BRep_TVertex` directly, not `BRep_Builder`, so tier 2 never sees it;
only tier 3 would.

**Open (user, 2026-09-24).** Chosen first: copy-on-write in the fork --
an algorithm that must change an `Immutable` edge or vertex copies it
into its result and reports the copy in its history. Then reopened by the
pcurve findings: FreeCAD's STEP export writes no pcurves by default
(`WriteSurfaceCurveMode` 0, overriding OCCT's On) and the reader rebuilds
them, so a pcurve is derivable, like the triangulation -- though not bit
for bit on a non-planar surface, and the edge tolerance is measured
against it. The alternative on the table: a pcurve for a surface the
edge has none for is a cache and may be added to an `Immutable` edge;
the BREP writer drops pcurves whose surface is not a face of the shape
being written, and normalises the contextual `Free` bit, so the value's
bytes stop depending on its consumers; a forced `SameParameter` on an
`Immutable` edge verifies instead of resetting; copy-on-write only where
a new face needs a real change, such as a larger tolerance. It keeps the
edges shared, so element names cannot move, where copies would have to
be carried through every mapper. Decided for the cache (user,
2026-09-24); as built in 23.13.

### 23.13 Pcurves as a cache, and the freeze on by default (2026-09-24)

The ruling of 23.12: what a consumer adds to an edge it builds on is
derived data, not the edge's value, and the value's bytes do not carry
it. Everything below is the OCCT fork (`LinkVibe-801`) unless it says
FreeCAD.

**What an Immutable TShape takes** (`BRep_Builder`, the note at the top
of `BRep_Builder.cxx`):

- A call that changes nothing passes without a write: a tolerance at or
  under the one held, a flag or a range already so, a vertex restated
  at its own point (within the round trip through its location, 16 ulp
  of the coordinates). This is what most of the 335 throws were --
  `BRepLib_MakeFace`'s forced `SameParameter` reset, a wire merge that
  keeps a vertex where it is.
- A representation for a surface the edge or vertex has none on yet: a
  pcurve (`UpdateEdge` with a 2D curve), the regularity between two faces
  (`Continuity`), a vertex's parameters on a new face or on a pcurve
  (`UpdateVertex`). It is marked `BRep_CurveRepresentation::IsCache`, and
  a cache may be replaced, removed or re-ranged afterwards -- a full
  revolution puts one pcurve on its surface and then replaces it with
  the seam pair. Never with a tolerance the edge would have to grow to.
- Anything else throws, as before. The TShape's own tolerance setters
  (`BRep_TVertex`/`TEdge`/`TFace::Tolerance`, `UpdateTolerance`) now
  check too: `BRepLib` raised vertex tolerances there directly, past
  every builder check (23.12). `BRepTools::UpdateFaceUVPoints` is let
  through: the UV points are the pcurve evaluated at its range.

**Algorithms that would change a frozen input:**

- `BRepLib::SameParameter` forced, on an Immutable edge whose flags are
  set, checks every stored pcurve against the 3D curve within the edge
  tolerance (`pcurvesWithinTolerance`, `BRepLib_ValidateEdge`) instead of
  resetting the flags; an edge that fails goes on and is refused.
- `BRepLib::UpdateTolerances` leaves a frozen vertex that already covers
  its edges alone: its `2*Epsilon` padding grew every such vertex by an
  ulp on the first pass.
- `BRepOffsetAPI_ThruSections` goes to its own `SetMutableInput(false)`
  when a section is Immutable, as the pave filler goes non-destructive
  (23.12).
- `BRepLib_MakeWire` is the one real copy-on-write: a merge that has to
  move or widen a frozen vertex (`thawVertex`) gives the wire a copy of
  it, and copies of the edges built so far that use it; the input keeps
  its own.

**Stable bytes.** `BRepTools_ShapeSet` and `BinTools_ShapeSet` gain
`SetStableBytes`: a representation on a surface no face of the written
shape carries is left out (a pcurve, a regularity, a vertex parameter on
one), and `Free`, `Modified`, `Checked` are written as 1, 1, 0 --
`Free` flipped when the shape was added into a face somewhere else. The
own surfaces are collected by `Add()`, or by `AddOwnSurfaces()` for a
writer that fills the tables itself: FreeCAD's `ShapeRefSet` does, and
without it every pcurve read as foreign and `ShapeStorage` lost face
curves and volumes on reload. FreeCAD sets it for storage writes
(`TopoShape::applyStorageOptions`) under `DocumentParams`
`StableShapeBytes`, on by default; `ShapeRefSet` writes its own flag
bytes and follows it. Under it, borrowing an edge or a vertex from
another object's file (`BorrowBelowFace` 2, 4) is masked off: the lender
leaves out the curves the borrower's faces need. Face-level borrowing
is unaffected. A cache representation on the written shape's own
surface (a consumer adding a vertex parameter on a frozen face's plane)
is still written: named here, not measured (measured in 23.14).

**The default.** `OCCT_EXT_VERSION` in `TopTools.cxx`, reported by
`SetFuncShowTopoShape`, is 2 with this work. FreeCAD's
`initOCCTExtension()` looks the symbol up in the process first
(`dlsym(RTLD_DEFAULT)`; the `libTKBRep.so` dlopen it had is a
development symlink and never the macOS name), and `PartParams`
`ImmutableShapeValues` defaults to `initOCCTExtension() >= 2`: on with
the fork, off without it, and whatever the user set otherwise.

**Measured**, both suites with the freeze on and a throw-logging preload
(23.12's method): ctest 802/802, Python 2902 OK, no
`TopoDS_LockedShape`/`FrozenShape` thrown in the Python run; the ctest
throws are the gtests' own. From 130 failures and 335 throws. Gtests in
`ImmutableShape.cpp`: a face and a prism built on a frozen wire leave
its stored bytes as they were; only a cache pcurve is replaced; a
no-change write passes and a change throws, the vertex's own setter
included; a wire merge copies a frozen vertex and leaves the input's;
the default follows the loaded kernel. Python
`ShapeStorage.ShapeStableBytesCases`: a face stores the same bytes
whether or not something was extruded from its own edges, and with
`StableShapeBytes` off it does not -- the control. `Part::Extrusion`
is no test of this: it extrudes a copy.

### 23.14 The residual measured, and four throws the suites missed (2026-09-24)

**The measurement.** A probe, not committed, hashed each shape value's
stored bytes (`exportBrep` with the storage options) when
`PropertyPartShape::setValue` froze it, and again when the value was
replaced or its property destroyed; the root location is stripped
first, since a Placement edit moves `_Shape`'s location in place and
storage writes the location as an attribute. Python suite: 2904 OK, and
the three values that moved are all in `ShapeStableBytesCases`' control,
which switches `StableShapeBytes` off on purpose. ctest: 802/802, none. A
sweep of 7 shapes by 26 consumers built on each frozen value's own
sub-shapes (extrude, revolve, fillet, chamfer, thickness, offset, the
booleans, section, slice, refine, loft, sweep, fix, tessellate, check,
split, project, a face on the value's wire): none -- and 20 with
`StableShapeBytes` off, so the probe sees. By hand: a prism cap restating
an internal vertex's (u,v) on its generating face writes nothing (the
face already had them, and an unchanged restatement passes), and
`ShapeFix` in place on a frozen planar face moves nothing with
`DedupShapePCurves` on or off. That option, on by default, also leaves
out a later pcurve on a frozen face's own plane that equals the
projection, the likeliest producer. So the residual as 23.13 named it is
reached by nothing the suites or the sweep do.

**What the sweep did find: four throws, freeze regressions.** Each case
works with the freeze off (torus thickness fails there too, `NotDone`,
but with the freeze it threw first, at the sphere's site):

| Case | Site | Why |
|---|---|---|
| `makeThickness` on a sphere, a torus | `BRepOffset_Inter3d::ContextIntByArc` -> `UpdateVertex` | puts the input's vertices, INTERNAL, on the edges it extends |
| `Part.Face` on a prism face's outer wire | `BRepLib::UpdateTolerances` in `BRepLib_MakeFace` | the face takes the largest edge tolerance; the others would grow to it, by 2e-17 |
| revolving a torus face | `ShapeFix_Wire::FixShifted` -> `ReplacePCurve`, from `TopoShape::fix` in `makERevolve` | an in-place fix of a result whose cap is the frozen face |

Without the freeze the last two write into the input value: the wire's
tolerances, the torus face's own pcurve.

**Vertex parameters are caches too** (fork). An Immutable vertex takes a
parameter on a curve, pcurve or surface it has none on yet, marked
`BRep_PointRepresentation::IsCache`; a cache may be rewritten, an own
parameter only restated (`UpdatePoints` decides, for every
`UpdateVertex`). `checkImmutableVertexOnEdge` keeps the tolerance and the
frozen edge's ends.

**And that reached the residual for real.** The extended edge shares the
input edge's curve and pcurves, handle for handle, so a frozen sphere's
pole vertices gained parameters on the sphere's own seam curve and
pcurves -- own surface, own handles -- and its bytes moved (the thick
solid gtest). The writer rule is now structural (`BRepTools_ShapeSet::
OwnGeometry`, both formats): a vertex parameter on a curve or pcurve is
written only where an edge of the written shape holds the vertex INTERNAL
or EXTERNAL and carries that curve. `BRep_Tool::Parameter` reads an end
vertex's parameter from the edge's range and never a point
representation, so nothing else is ever read. Exact, and indifferent to
when the parameter arrived. A parameter on a surface keeps the surface
rule: `BRep_Tool::Parameters` reads one for any vertex. What is left of
the residual is a pcurve, a regularity or a vertex (u,v) added after the
freeze on the written shape's own surface -- still not observed. If it
ever is, the answer is timing, not flags: the freeze would adopt the
caches on a face's surface as its own, and the writer drop a cache no
frozen face adopted.

**The face tolerance** (superseded by 23.15: it made the same input give
a different face with the freeze on and off, and fixed one caller of a
refusal that is in `UpdateTolerances`) (fork, `BRepLib_MakeFace(wire)`): a face needs to
cover how far its wire is from the surface, not the largest edge
tolerance, which is `FindSurface`'s convention. On a wire with frozen
edges it takes the smallest frozen edge tolerance when that covers
`1.2 * ToleranceReached()`, and nothing frozen has to grow. When it does
not, the face keeps the convention and the frozen edge refuses.

**`TopoShape::fix()`** (FreeCAD) fixes a copy, then the shape itself in
place to keep its sharing. A shape with a frozen part keeps the fixed
copy instead: the in-place pass would repair the value too.

Gtests in `ImmutableShape.cpp`: a frozen vertex takes a cache parameter
on a new curve and rewrites it, while its own is only restated, and the
edge's bytes stay; a parameter on a new edge's pcurve on a frozen face's
own surface leaves the face's bytes; a face on a frozen wire of unequal
tolerances grows none of them; a thick solid from a frozen sphere leaves
its bytes; a revolved frozen torus face is left alone by `fix()`.
Suites: ctest 807/807, Python 2904 OK; under the probe and the
throw-logging preload, no `LockedShape` thrown, and in the Python run
only the control's three values moved.

### 23.15 Copy-on-write, and a thawed copy keeps its name (2026-09-24)

23.14's face tolerance is reverted (fork `fc92bc3592`). The ruling of
23.12 stands: where a new shape needs a real change to a frozen part,
such as a larger tolerance, the algorithm copies it. The question the
copy raises is naming. An element included as it is gets the name of
direct inclusion; a copy would get a Modified name at best, and none at
all where the maker has no history: `BRepBuilderAPI_MakeWire` and
`MakeFace` report none (their `Modified()` is `BRepBuilderAPI_MakeShape`'s
empty list), `FaceMaker::postBuild` maps by direct inclusion only, and
`wiresFromSortedRuns` follows `mkWire.Edge()`, the one edge just added.
Giving those makers a history would add members to classes FreeCAD
builds on the stack.

**The thawed copy** (fork). `TopoDS_TShape::Thawed()`, bit 13 of the
state word (reserved until now, inline accessors: no layout change),
marks a copy an algorithm made of an Immutable TShape it would otherwise
have changed in place, or of a container of such a copy. It stands for
that TShape. `TopoDS_TShape::Thaw(copy, original)` sets it and records
the original in a side table, mutex-guarded, which
`TopoDS_TShape::ThawedFrom(copy)` reads; the copy holds its original
until the copy's destructor, now out of line, drops the entry. Copies are
rare, so the table stays small, and the flag is all a TShape carries.

**Where copies are made.**

- `BRepLib::UpdateTolerances` and the forced `SameParameter`, in place
  (`IsMutableInput`): a frozen edge, vertex or face whose tolerance has
  to grow is copied and the copy grown (`UpdShTol`). `putThawed` then
  puts the copies into the shape being updated, in place: a container
  that is not frozen takes them among its children in the same order
  (the element indices depend on it); a frozen container on the way is
  copied and thawed in turn. The root must not be frozen: that stays a
  refusal. A vertex copy takes its point representations too, as new
  objects -- `EmptyCopy` drops them, and sharing them would let the
  copy's edits reach the frozen original.
- `BRepLib_MakeWire::thawVertex` (23.13) marks its vertex copy and the
  copies of the edges already in the wire that use it.

**Naming** (FreeCAD). `TopoShape::mapSubElement` gives an element of the
result that is a thawed copy the name of the first original along its
`ThawedFrom` chain that the input has -- the name direct inclusion
gives, which is what the element keeps with the freeze off. It is a flag
test per result element per input; the chain is followed only for a
flagged one. This covers the earlier edges `thawVertex` copies, which
`wiresFromSortedRuns` does not see.

Gtests in `ImmutableShape.cpp`: a face on a frozen wire of unequal
tolerances holds a thawed copy of the wire and of the three edges that
grew, the wire as it was; its element names, vertices, edges and face,
are those of the same construction unfrozen; the wire merge's vertex
and edge copies are thawed copies of what they replace.
With the new pass disabled, the naming case fails on every edge and
vertex, so it tests what it says. Suites: ctest 808/808, Python 2904 OK,
no `LockedShape` thrown in the Python run; the four cases of 23.14 behave
as with the freeze off, through copies now.

### 23.16 Step 6 as built: blobs through the entity store (2026-09-24)

**Three bugs first.** Measured before building, each with a script
against the build as it stood:

- A version's blobs were listed in its manifest but held by nothing the
  log owned. With undo off and derived values unlogged, a box saved, its
  length changed and saved again: checking out version 1 threw
  "blob ... of version 1 is not in the store". The log held a blob only
  when an op's copy happened to name it (decision 6b), and released it
  only with the log.
- A composed snapshot (23.9) claimed a shape property whose newest value
  the log held -- a detached copy's capture, `<Part file="Property.brp"/>`
  with the geometry as an attachment, because the copy had no file yet.
  The part it put into `Document.xml` named an attachment no version
  carries: after one length edit, checking out either snapshot failed with
  "shape is invalid". A shape's file is made in `beforeSave` (`makeBlob`),
  which runs after the sink has claimed, and its Save writes the hasher
  index only the live property in its document knows, so no copy can
  produce its part.
- `PropertyFileIncluded` (and every other referrer but the shape) wrote
  `<FileIncluded hash=.../>` into a capture at schema 5, held by nothing
  the log owned; and its Save noted the blob into the document's save set
  from the log's worker thread, so a save running at the time could carry
  an extra entry.

**Blob entities.** A file of the document's `FileBlobManager` is an entity
of kind `blob`, `enc` `file`, its extension as `data`: the bytes are the
file, which the log holds (`_blobs`, a handle per entity stored as
`file`). `putBlob` makes the row, or makes a delta row full again when
the content comes back (a shape changed and changed back: the newest is
full). `readBytes` reads a `file` entity through the handle. Schema 3 of
the store; a schema-2 store migrates its `source='blob'` manifest rows
onto entities (size unknown, 0, which keeps them out of the delta policy).
The manifest names entities only.

**Names.** A version's blobs are listed under the names a save gives them
inside `blobs/` -- `Box.Shape.brp` -- from `FileBlobManager::collectedEntries()`
(`planSave` with no previous index) for a save or a snapshot, and from
`restoredEntries()` (the entry name each blob arrived under) for the
history started from a file. The name is what pairs a property's file in
version N with its file in N+1, exactly as `Document.xml` is paired, so
the snapshot job supersedes every entry whose hash changed under the same
name.

**What a value names.** `BlobRecorder`, thread-local in the blob manager:
while one lives, `noteReferenced` records into it instead of the save
set. `captureValue` runs every Save under one, so the referrers' "noted
again here" convention tells the capture exactly which files its fragment
names (`CapturedValue::blobs`) and the save set is never touched from the
worker. The appearance list did not note its textures in Save and does
now. A shape notes in `beforeSave`, which a capture does not run: its
`contentBlob()` is added. `putValue` stores each as a blob entity and a
`blob` ref of the value (an edge, not part of the value's hash, which
stays the fragment and its attachments).

A shape file that borrows geometry names the files it borrows from in its
`Files` table (`docs/SharedShapeStorage.md` 11.5), and cannot be read
without them. `FileBlob::sources()` reads that table through a reader
the owning module registers per extension (`ShapeRefSet::fileTable` for
`brp`, registered at Part's load); `putSources` stores each source as a
blob entity with a `blob` ref from the borrower, once per blob per log.
Only for a blob a value names: a version's manifest is closed by
construction (a save writes every file its files read), so the thousands
of blobs a restore lists are not read for it.

**The claim.** The sink never claims a blob referrer; its part is the
snapshot's own serialisation, which costs the Save a real save pays (the
file itself was written by `beforeSave` either way).

**Collection.** The collector already follows every role of edge, so a
blob lives while a manifest or an op value reaches it, directly or
through a borrower. After anything that removes or re-encodes entities --
eviction, truncation, `dropTier`, a delta -- `releaseBlobs` lets go of the
handle of every blob no longer stored as `file`, and the blob manager
deletes the file when nothing else holds it. The embedded copy (16.4)
carries every blob it still stores as a file, an op value's as well as a
kept version's; the ones kept as deltas travel inside the database.
`adoptStore` takes a handle on each from the document's store, where
`PropertyHistory` restored them.

**Deltas, and the measurement that gated them.** Zstd `--patch-from` at
level 3, a shape file against its successor, as a percent of the older
file compressed on its own (the `TransactionLogDeltaRatio` test), on the
blob a save writes after each edit:

| Sequence | Raw bytes | Compressed | Patch | Percent |
|---|---|---|---|---|
| PartDesign pad, length +1 (5 steps) | 2570 | 605 | 53 | 8.7 |
| plate with 30 holes, drag one hole (4) | 20709 | 2457 | 178 | 7.2 |
| same plate, pad thickness +1 (3) | 20709 | 2456 | 168 | 6.8 |
| box minus cylinder, move the cylinder (5) | 3485 | 791 | 41 | 5.2 |
| filleted box, radius +0.5 (4) | 14367 | 2507 | 1136 | 45 |
| plate gains a hole per step (4) | 2578-4402 | 606-926 | 286-439 | 41-47 |

A parameter edit patches to under a tenth; a change of topology or a
fillet to just under the default 50 %. So blobs and attachments join the
policy: `supersede` accepts `attach` and `blob`, compares a `file`
entity's patch against its compressed size (computed there, since the
file is not compressed where it lies), and on success releases the file.
A value's files follow the value: superseding one `prop` by another
supersedes their attachments by name and their blobs when each names one,
so an edit sequence of an unsaved shape -- whose op values carry the
geometry as an attachment -- chains too.

**Checkout** writes every entry from the log: a blob held as a file is
copied, one kept as a delta is decoded.

**The panel** shows, per manifest entry, the entity's kind and encoding,
and for a blob its size and the held file or the patch it is kept as.

Gtests: `TransactionLogTest.blobsAreEntitiesTheLogHolds` (a
`PropertyFileIncluded` with undo off: the version's blob is a `file`
entity under `Obj.File.txt`; one changed line makes it a patch under a
tenth of its size toward the next version's, its file gone from the
document's store; the op values' `blob` refs; both versions check out;
unrelated content stays a file; eviction and truncation drop the entities
and the log lets go of the file). `TransactionLogShapeTest.composedSnapshotChecksOutAChangedShape`
(two length edits, three versions, each older `Box.Shape.brp` a patch on
the next, all three check out with the right volume) and
`borrowedFilesAreHeld` (a compound's file names its two parts' files; the
op value holding it gives the blob entity a `blob` ref to each).

## 24. Phase 3: undo over the log (user, 2026-09-24)

Designed after step 6, from a survey of `Document::undo`/`redo` and the
log as it stands. Section 12 is the intent; this is how it is built, and
where the two disagree this section wins. Everything here runs behind the
`TransactionLog` staging switch (22.1): with it off, undo is exactly what
it was.

### 24.1 Rulings

| Question | Ruling |
| --- | --- |
| Which transactions are undo steps | A recompute a command runs inside its own transaction is part of that step -- no special case, it already is today. A recompute with no transaction open is an implicit `recompute` transaction (closed when the recompute returns, as built in section 21) and is an undo step of its own. With the log on, `TransactionOnRecompute` -- the setting that decides whether `Std_Refresh` opens a transaction, off by default because a recompute after an undo cleared the redo stack -- is ignored: under the log the redo run a recompute clears is still in the log, browsable and restorable (section 12). Other implicit transactions (console, macro) are steps too. |
| Cold undo (past the in-memory window) | The inverse ops applied onto the live document, property by property. Other objects, the view and the selection are untouched. Checkout plus replay is not the mechanism. |
| A derived value the cold undo needs is gone from the cache | Restore the inputs, touch the object, leave it to recompute -- the state of a document with an out-of-date feature. Never refused for this. |
| Checkout | Becomes a forward transaction in this phase (section 12's "restore to N"): the properties that differ are set, the objects that differ created or removed, in one undoable transaction. It no longer reloads the document or clears the undo stack. |

### 24.2 The hot path writes its inverse (24.a)

`Document::undo` already builds the inverse while it applies T: the
record it opens before `apply` collects the writes the undo makes, and
becomes the redo step. Its *before* copies are T's *after* values, its
*afters* are T's *befores* -- section 12's inverse, made by the undo
system as it stands. So the undo is logged by committing that record
through `onCommit`, as kind `undo` with `inverts = T.seq`; redo likewise
as kind `redo` inverting the undo's row. Content addressing makes every
value a hit: nothing new is stored, the worker only hashes.

- `txn` gains `inverts` (store schema 4; 0 for an ordinary transaction).
- `Transaction` learns its row: `LogSeq`, set by `onCommit` (0 when the
  commit wrote no row).
- Derived is inherited. The write-site rule (the owner is recomputing)
  says an undo's writes are inputs, which would log a Pad's `Shape`
  durably the moment it is undone. The inverse's op is derived when the
  op it inverts was: the record takes each property's flag from T's
  record, both keyed by property id.
- `onUndoRedo` goes: an undo is now an ordinary commit and the
  held-current bookkeeping (23.3) follows it the way it follows any.

### 24.3 The stacks become sequence numbers (24.b)

The undo and redo stacks hold log rows; each entry keeps its in-memory
`Transaction` while it is inside the hot window (`UndoMaxStackSize`, now
the size of that window and not the depth of undo). An entry past the
window drops its `Transaction` and keeps its seq, its id and its name.
Undo depth is the log's since the document was opened or started its
history.

A **cold undo** reads T's ops and applies them reversed, in reverse
order, through the same record the hot path opens, so it is logged the
same way:

- `set`: the property restored from `vbefore` (`restoreValue`). A derived
  op whose value is gone from the cache tier: skipped, the object
  touched (24.1).
- `create`: the object removed. `remove`: the object recreated under its
  recorded type, name and id -- the one new primitive, since `_addObject`
  hands out a fresh id and the id is the shape's `Tag` -- then its
  properties restored from the `set`s that precede the op.
- `addprop` / `delprop`: the reverse, the metadata from `meta`.
- View-provider ops are logged with `cid = -1` today and cannot be
  reached cold; they gain the owner's id and a cold undo applies them
  through the Gui document.
- `restoreValue` of a value naming a blob the log keeps as a delta
  re-adopts the decoded bytes into the document's blob store (the item
  left open by 23.16).

### 24.4 Selective undo (24.c)

Undo of T that is not the tip: allowed when every op passes the section
12 check -- a `set`'s *after* is the property's current hash, a created
object still exists, a removed object's name and id are free -- and
refused otherwise with the conflicting ops named. The current hash is
what the worker already keeps per property (`_hashById`), after the
pending refs are resolved. Python first; the panel's context menu once
it works.

### 24.5 Restore to N as a transaction (24.d)

Version N's composites (23.3) list every container's parts by property;
the head's are what a composed snapshot of now would list. The
difference is the transaction: properties whose part hashes differ are
set from the log's entity, objects in N and not now are recreated
(24.3's primitive, type and name from N's skeleton), objects now and
not in N removed. One transaction of kind `restore` naming N, undoable
like any. A version with no parts (the one taken from the file on open,
stored as bytes) is cut into parts first.

### 24.6 24.a and 24.b as built (2026-09-24)

**24.a, the hot path.** As 24.2 planned. `txn.inverts` is store schema 4
(an older store gains the column on open). `onCommit` returns the row's
seq, which the document keeps on the transaction (`Transaction::LogSeq`);
`Document::logInverse` commits the record an undo or redo opened, after
`Transaction::inheritDerived` copied the applied step's derived flags onto
it; `onUndoRedo` is gone. The panel shows an `Inverts` column and
`getTransactionLog()` an `inverts` key. `TransactionOnRecompute` needs no
code: with the log on, a recompute outside any transaction opens an
implicit `recompute` transaction that the recompute itself commits
(section 21), an undo step whether the setting is on (then it is named
`Recompute`) or off. Gtest `undoAndRedoAreLoggedAsInverses`: the undo's
refs are the edit's swapped, the redo's the edit's again, derived stays
derived through both, and a bare recompute is a step. Suites with 24.a
alone: ctest 812/812, Python 2904 OK.

**24.b, cold undo.** `_trimHotWindow` replaces the stack limit: with the
log, a step past `UndoMaxStackSize` becomes a stub (`Transaction::Cold`,
`coldCopy`: id, name, origin, `LogSeq`); a step with no row is deleted as
before. The undo stack only. Its steps are deleted oldest first, the order
`clearUndos` relies on -- a removed object is owned by the last step that
names it -- and the far end of the redo stack is its newest, so the redo
stack stays hot and unbounded (it is no longer than the undos made).

Undoing or redoing a stub reverts its row. `_prepareRevert` reads the row
(`TransactionLog::readRevert`: ops, and every before value by hash) and
checks it against the document before anything moves: a removed object's
id and name free and its type loadable, a created object still there, a
set's owner there or about to be recreated, a non-derived value present.
Any failure refuses the undo with the reasons named, and a multi-step
`undo(id)` stops there. `_applyRevert` then runs in passes over the ops
reversed, so no value naming an object restores before the object exists:
the removed objects recreated (`_Id` preset, which `DocumentP::addObject`
keeps when free; their dynamic properties added back from the metadata
the remove's sets now carry), the dynamic properties the row removed, every
value (`restoreValue`; a derived value the log lacks touches the owner),
the dynamic properties it added, the objects it created. The writes land
in the record the undo opened, which becomes the hot redo step and is
logged as in 24.a, derived flags taken from the row's ops. A hot undo
leaves the undone object touched; a cold one matches it.

`readRevert` makes every blob a value names a file of the document's store
again (`restoreBlob`): one kept as a delta is decoded and adopted, held as
a file once more, recursively for the files it borrows. That needs the
blob's extension, which a delta row no longer carries, so a value's `blob`
edge (and a borrower's) now names it. This closes step 6's open item.

View-provider ops are skipped cold (logged at `FC_LOG`): their container
has no id in the log yet. A cold-recreated object's view provider starts
from defaults.

**Three defects the cold path found**, all of the log as it stood, and
all fixed:

- *A use-after-free on the worker.* The worker serialises copies after the
  commit that queued them. A copy of a link to an object that a removal
  step owns outlives that object when the step is deleted -- evicted from
  the stack, or at once with undo off -- and `getExportName` then reads
  freed memory. The 20-step eviction could always do it; a window of 2
  made it certain. `Document::_deleteTransaction` drains the queue first
  when the step destroys objects (`Transaction::destroysObjects`), which
  only a removal step does.
- *A link to a removed object logged empty.* Serialised on the worker
  after the commit, a link's target has left the document and
  `getExportName` has no name for it. Links are now captured on the main
  thread as their task is made (`ValueTask::captureNow`), under
  `CaptureNames` -- a thread-local scope giving each object the transaction
  removed the name the transaction recorded, which `getExportName` of a
  detached object consults.
- *The op that cleared such a link dropped.* `PropertyLinkBase::isSame`
  compares through `getLinks()`, which leaves out a detached target, so
  "links to the removed B" equals "links to nothing" and the commit took
  the write for a no-change. The log compares links by what it would
  write for them (`sameForLog`).

Gtests: `coldUndoRevertsFromTheLog` (a window of 2 under eight steps -- a
link, a dynamic property, a removal, four edits: undo all, redo all, undo
all again, which reverts `redo` rows cold; ids, names, the dynamic
property's group and value, the link, at every step) and Part
`coldUndoReadoptsADeltaBlob` (a saved box's file superseded into a delta
and released; the cold undo decodes it back into the store, the shape is
the saved one with no recompute, and the redo returns the newest).

### 24.7 24.c, selective undo, as built (2026-09-24)

`Document::undoLogged(seq)`, Python `Document.undoTransactionFromLog(seq)`,
and "Undo row N" on the panel's transaction rows. It commits an open
transaction, resolves the pending afters, and reads the row as a cold undo
does (24.6); the checks are the cold undo's plus the refuse rule, and the
apply is the cold undo's passes -- but into a new transaction, not an undo
record: the redo stack is cleared as by any edit, the transaction is named
`Undo <name>`, logged as kind `undo` inverting the row
(`Transaction::LogKind`, `Inverts`), and is an undo step itself.

**The refuse rule, as checked.** For every property the row left in a
state -- its last `set`'s after, or gone for a `delprop` -- the newest op
on that property in any later row (`TransactionStore::lastOpOn`, on the
`op(cid, prop)` index) must have left it in the same state. No later op
passes; a later op that changed it and a later one that changed it back
(an undo and a redo) passes, since the refs are content hashes. What that
covers without a rule of its own: the row already undone (the undo's op
left the before), a created object edited since (its pending sets were
resolved to what it had then), a removed object recreated since (the id
check). Derived properties are neither checked nor restored -- the next
recompute rewrites them, and a selective undo has no business with a
feature's output -- and their owners are touched.

Gtest `selectiveUndoRefusesWhatChangedSince`: an older edit undone while
a later one to another property stays; the same row refused once undone;
a row whose property changed since refused with nothing moved and no row
written; the selective undo undone like any step; a created object edited
since not uncreated; a derived value left alone and its owner touched.

### 24.8 24.d, restore to a version as a transaction, as built (2026-09-24)

`Document::restoreVersion(num)` -- Python `restoreTransactionVersion`, the
panel's "Restore to version N" -- no longer reloads the document. It
materialises the version as before (`_materialiseVersion`, every entry
now read as bytes: a blob through `read()`, never copied by `path()`, as
`docs/FileBlobsManager.md` sec 15.6 asks of byte-only callers), reads it
into a hidden scratch document (`newDocument(..., tempDoc)`, no log of its
own -- `DocumentP::noLog` -- no undo, `FileName` set to the checkout
directory so its transient directory, named from Uid and FileName, stays
apart from this one's), and `_applyVersion` makes this document what the
scratch one is, into a new transaction of kind `restore` named `Restore
version N [name]`. The scratch document is closed and the active document
handed back. A version the document already is records nothing.

The passes are 24.3's, matched by object id: an object the version lacks
-- or has under another type or name -- is removed; one it has is
recreated under its id and name; per container, the dynamic properties
the version lacks are removed and those it has added with their metadata;
then every persisted property whose captured value differs from the
version's is restored from the version's capture. The blobs such a value
names (and the ones they borrow from) are copied into this document's
store by bytes first, and held for the pass: a handle nobody holds is
deleted at once, before the referrer restored next can find it. The
document's own properties are restored except where it lives and who it
is (`FileName`, `TransientDir`, `Uid`, `Id`, the `History`/`Version` the
log keeps, and the created/modified stamps). Last, an object the version
had untouched is purged of the touches the restore made.

Two consequences of "the document is not reloaded": the objects that stay
are the same C++ objects, so pointers, selection and the view survive;
and view providers are not restored -- GuiDocument.xml's colours and
visibility are the Gui's, which a forward restore through App does not
reach (a restored object that is new gets defaults). That is left open;
the old reload restored them. The `checkout` record and
`TransactionLog::onCheckout` are gone: the `restore` transaction is the
record.

**`restoreValue` records the change.** Found by the blob gtest:
`PropertyFileIncluded::Restore` of a `hash=` fragment only asks the
manager for the blob -- it calls neither `aboutToSetValue` nor
`hasSetValue`, rightly for a load -- so restored into a live property the
change was in no transaction and told nobody. `restoreValue` now brackets
every restore with the pair (`PropertyValueRestorer`, a friend of
`Property`); nested calls from a `Restore` that does call `setValue` are
harmless. This covers cold and selective undo too.

### 24.9 View providers (user, 2026-09-24)

Left open by 24.6 and 24.8, and settled by the user's pointer to the
setting that already decides it: **view-provider state is undo state
exactly when `ViewObjectTransaction` says so.** That setting (or a command
flagged `ViewTransaction`) is what lets a view provider's change open a
transaction; inside an open transaction the change is recorded either
way. So the log follows what the undo system records, and the restore
follows the setting:

- **Logged under the owner.** `TransactionalObject::getTransactionOwner()`,
  which `ViewProviderDocumentObject` answers with its object: a view op's
  `cid` is the object's id (it was `-1`), and `resolvePending()` resolves a
  view op's pending after too (`Pending::view`).
- **Reached from App.** `Document::setViewResolver`, which the Gui registers
  at start (and clears at exit) with `Application::getViewProvider`;
  `Document::viewOf(obj)`. Without a Gui (FreeCADCmd) there is none, and
  view ops are left alone, as are ops logged before this change (`cid` -1).
- **Cold and selective undo** apply a row's view ops like any other --
  checked (the object there or recreated by the same revert, the value in
  the log), applied in the value pass through the resolver, and checked by
  the refuse rule. A view provider's create and remove follow its object's.
- **Restore to a version** restores the view providers' properties,
  compared against the scratch document's (which the Gui restored from the
  version's GuiDocument.xml), when `ViewObjectTransaction` is on. Off, view
  state is not undo state and the restore leaves it as it is.

Gtest `coldUndoReachesViewProviders`, with a stand-in view provider (a
`TransactionalObject` with an owner, its record type registered as the Gui
registers `TransactionViewProvider`) and `ViewObjectTransaction` on: the op
is `view` under the owner's id; a cold undo, a redo and a selective undo
of the view change. Real view providers need the GUI:
`scripts/transaction-log-view-check.py`, run at GUI start in a fresh user
home --

    cd build/conda-relwithdebinfo-801
    QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-view \
      VIEWCHECK_OUT=/tmp/viewcheck.txt ~/works/sw/fcad/.conda/run.sh \
      ./bin/FreeCAD ~/works/sw/fcad/scripts/transaction-log-view-check.py

-- a box's colour changed, pushed past a window of 2 and undone cold,
redone; restored to the version before the colour, the restore undone.
14 checks, all PASS on 2026-09-24.

### 24.10 Every saved view-provider property, with the log on (user, 2026-09-24)

24.9 made view-provider state undo state when `ViewObjectTransaction` says
so. The user then asked for the log to capture view-object changes
regardless of the settings -- "colours are important in CAD". A web survey
of how other tools draw the line (Fusion, Onshape, NX, Inventor, Creo,
CATIA 3DEXPERIENCE, SolidWorks, Rhino, KiCad, Figma, Photoshop; forum and
snippet evidence mostly, vendor forums refusing to load) found:

- No tool classifies by *who* made the change (user or application). They
  classify by *kind of state*: persisted design data (appearance, and in
  Fusion and Onshape visibility) is a transaction and undoable; transient
  or per-user view state (camera, isolate, temporary transparency) is not
  recorded at all. Fusion's forum answer: "any change that needs to be
  persisted is a transaction".
- The camera is in no tool's model undo; SolidWorks and Rhino give it a
  separate "undo view" stack.
- No CAD tool has a switch like `ViewObjectTransaction`; Photoshop's "Make
  Layer Visibility Changes Undoable" is the one analogue found.
- Changes the application makes itself are governed by behaviour
  preferences (Fusion's "Auto hide sketch on feature creation"), not by
  undo rules; NX's invisible undo marks are the only mechanism found for
  keeping internal steps out of the user's list.

**Ruling.** With the log on, a view provider's saved property is document
data like an object's: a change to it is recorded whatever
`ViewObjectTransaction` says -- inside an open transaction as before, and
otherwise in an implicit one of its own. A property that is never saved
(`Prop_Transient`, `Prop_NoPersist`, `Property::Transient`) is not. With
the log off nothing changes. Where application code writes view state
outside any command and does not put it back, the fix is at that site --
grouped into its command, or kept out of the record -- as 23.6 rules for
escaping writes, not a setting. What a task dialog hides and shows again
inside its transaction costs nothing: a set whose before and after are
the same is not logged (sec 9.1).

**The camera is not this.** Considered as a view property logged in
log-only rows, then withdrawn by the user the same day: bundled with
property ops it would cause trouble. It gets its own mechanism later -- a
session-only undo/redo stack for view configuration, in the spirit of
SolidWorks' and Rhino's "undo view". The ad hoc `<Camera>` XML of
GuiDocument.xml stays as it is until then.

*Later (user, 2026-09-25):* the 3D view's own properties (`DrawStyle`,
`ShadingType`, render and light overrides, `ObjectDisplayModes`,
`OnTopObjects`) are not logged either -- a view is a property container
but not a transactional one -- and are to be handled the same way: the
camera becomes a view property after all, and all view state gets a
per-view, session-only change stack. Object visibility is to become view
state too, with the App visibility as the seed. Recorded as design in
`docs/MultiViewEdit.md`, with the edit session moving from per document to
per view.

**GUI events outside any command.** A tree checkbox, a property-editor
path that sets no application transaction, a signal handler: an implicit
transaction opened there has no invocation to return from (sec 21). The
Gui now registers `Document::setImplicitCloser`, called when an implicit
transaction opens at invocation depth 0; it posts one zero-delay timer
that commits the implicit transactions once control is back in the event
loop -- one event, one step. Without a Gui the old rule stands (the next
invocation, explicit open, save or close commits it).

**As built (2026-09-24).** `Document::_checkTransaction`: with the log on, a
view provider's property opens a transaction unless it is never saved or
`DerivedViewWrites` is active; with the log off, `ViewObjectTransaction`
and `AutoTransaction::recordViewObjectChange()` decide as before.
`Document::setImplicitCloser`, registered by `Gui::Application`, commits a
depth-0 implicit transaction on the next event-loop turn.

**The audit.** `scripts/transaction-log-view-audit.py`, run like the view
check of 24.9 (`AUDIT_OUT` for its report), drives the GUI through
Part_Box, visibility toggles, colour by property and by Std_RandomColor,
the fit and isometric views, a PartDesign body, sketch and pad, edits
entered the way the tree's double click enters them (an application
transaction kept open while the edit lasts) and bare `setEdit` from a
script, undo, redo, hide/show selection, selectability and a save, and
lists every row with its view and data ops. What it found:

- Commands and deliberate writes come out as one step each, named after
  the command, or `<implicit>` for a script's or console's bare write.
- An edit entered as the tree enters it costs nothing: what it hides it
  shows again inside the same transaction, and a set whose before and
  after are the same is not logged. A script's bare `setEdit`/`resetEdit`
  writes `TempoVis` and visibility in steps of its own -- the script's
  writes, logged as such.
- **The one site that needed a fix:** `ViewProviderOriginGroupExtension::
  updateOriginSize` refits a body's origin (`Size` of the origin, its
  planes and axes -- seven view providers) after the model changes, from
  a deferred call outside any transaction: a view-only step the user never
  made. It now runs under `App::DerivedViewWrites`.
- The tree's visibility (eye) and unselectable icons write `Visibility`
  and `Selectable` with no command. With the log on each click now opens a
  named transaction ("Toggle visibility", "Toggle selectability"); off,
  exactly as before -- an `AutoTransaction` even without a name commits
  the active transaction when it goes, which could close an edit session,
  so it is not constructed then. Alt+click (show on top) changes a 3D
  view's property, not a view provider's, and is not touched.
- Rows of App data the audit also shows (`TreeRank` on a bare `setEdit`, a
  shape cache on fit, the document's stamps at save) are App writes the
  log has recorded since section 21; not this section's.

**Two defects the audit found**, both latent since section 21 and reached
only once view providers were recorded, where their Python proxies live:

- `PropertyPythonObject::saveObject` dereferenced its container, which the
  copy the undo system takes does not have: any change to a
  `FeaturePython` object's `Proxy` would have crashed the log's worker. It
  now writes `object`/`vobject` from the attributes alone on a copy.
- A Python value was serialised on the worker thread, and its copy
  released there: Python is the main thread's. `ValueTask::captureNow`
  captures a `PropertyPythonObject` on the main thread, as it does links,
  and drops the copy there.

Gtests: `viewChangesAreLoggedWhateverTheSetting` (the stand-in view
provider of 24.9 with `ViewObjectTransaction` off: a bare change opens an
implicit transaction, logged, undone and redone; under `DerivedViewWrites`
none opens) and `pythonObjectValuesAreCapturedOnTheMainThread` (a
`FeaturePython` Proxy replaced twice; the before value names the first
class).

### 24.11 Both suites with the log on (2026-09-25)

The gate section 22.1 sets for making the log the default: both suites
run with `TransactionLog=1` (a fresh `FREECAD_USER_HOME` whose `user.cfg`
sets it, so no real configuration keeps it). First run: ctest 818/818;
the Python suite crashed in `CAMTests.TestPathHelix` -- a SIGSEGV in
`Document::recompute`.

**The defect.** The recompute record (sec 21) was read from
`topoSortedObjects` *after* the recompute committed its implicit
transaction. With undo off that commit deletes the transaction, and with
it any object the transaction owned as removed -- which `topoSortedObjects`
still pointed at. The record is now read first and the commit follows;
the rows keep their order. A removal made inside an `execute()` is
deferred past the recompute's end and lands in an implicit transaction
logged after the record. Gtest `recomputeRecordSurvivesARemovalWithUndoOff`
(a Python feature that removes another object when it executes, undo off).

Second run, with the fix: **Python 2904 OK** (50 skipped, 6 expected
failures -- the log-off numbers) **and ctest 818/818 before the new case**.
Nothing else broke: the log on is the suites' behaviour too.

### 24.12 The recompute record, read before any observer (2026-09-25)

Both suites with the log on again, after the blob pack store
(`docs/FileBlobsManager.md` sec 15.11): the Python suite crashed in
`CAMTests.TestPathHelix` once more, a SIGSEGV in `Document::recompute` --
with the pack store on and with it off.

**The defect** is the one of 24.11, one step earlier. The record was read
after `signalRecomputed`, and an observer of that signal may delete what
was recomputed: the CAM test's Python observer clears the document from
`slotRecomputedDocument` (`removeObjectsFromDocument`), and with undo off
the objects go at once while `topoSortedObjects` still points at them. The
24.11 run passed only because the freed memory still read as an object.
The record is now read before the signal, as the last thing the recompute
does; it is still logged after the implicit transaction of the derived
writes. Gtest `recomputeRecordSurvivesAnObserverRemovingWithUndoOff`.

**Not from this change, and not chased:** `OmniControl_Tests_run`
(`test_documentReachOfEditOps`) fails with the log on, pack store on or
off: it creates an object, turns undo on, commits one transaction and
expects one undo step, and gets two. Not investigated; presumably the
creation becomes an implicit transaction of its own with the log on
(24.10), in which case the test states the log-off rule. It is main-thread
undo bookkeeping, which nothing in this change touches.

**The Python suite with the log on does not reproduce 24.11's "2904 OK".**
Past the crash it gives 8 failures on the tip. Two are FileBlobs cases
that expect content to die, which the log keeps as history; they now skip
with the log on. Six are not from this work:
`Document.DocumentObserverCases` `testDocument`, `testObject` and
`testUndoDisabledDocument` (a `DocOpenTransaction` signal before
`DocBeforeChange`, left-over signals), `Document.UndoRedoCases`
`testUndo` and `testUndoClear` (`UndoNames` holding `<implicit>`), and
`materialtests.TestMaterialSync.testUpdateFromLibraryUndoes`. `FreeCADCmd`
rebuilt at `e1959312dc`, before any of this, fails the three observer
cases the same way; that run then crashes in a later case of a
half-rebuilt tree, so the other three were not seen there. All six look
like the implicit transactions of 24.10 in a process with no GUI to close
them at the event loop. Open, not chased here: whether the tests state the
log-off rule or the log should keep the implicit ones out of what the
tests read.

### 24.13 The six log-on failures chased (2026-09-25)

The six Python cases of 24.12 and `OmniControl_Tests_run` come down to
three defects and one ruling.

**An implicit transaction outlived the undo mode it was opened in.** A
write with undo off and the log on opens an implicit transaction for the
log. In a process with no GUI and no invocation scope around the caller --
`FreeCADCmd -c`, `-t`, FreeCAD imported as a Python module -- nothing
closes it until an explicit open, a save, a close or the end of a
recompute. Turning undo on before then left it open, and the next commit
made it an undo step: writes made with undo off became undoable.
`UndoRedoCases.testUndo`/`testUndoClear` saw it in `UndoNames` straight
after `UndoMode = 1`; `OmniControl.test_documentReachOfEditOps` got a
create and a rename as two steps where it asked for one. Fixed in
`Document::setUndoMode`: a change of mode commits an implicit transaction
first, under the mode it was opened in. Gtest
`implicitTransactionClosesWhenUndoModeChanges`.

**Reading a material wrote it.** `obj.ShapeMaterial.getPhysicalValue(...)`
marked `ShapeMaterial` changed: the returned `MaterialPy` is bound to the
property, and the generated wrapper of any method not declared const calls
`startNotify()`, which writes the value back through the property. With
the log off it only touched the object on every read; with it on, the
write opened an implicit transaction and `doc.undo()` undid that instead
of "Update material" (`TestMaterialSync.testUpdateFromLibraryUndoes`). The
read methods of `Material.pyi` -- `has*`, `is*Complete`, `get*Value`,
`keys`, `values` -- are now `@constmethod`.

**An implicit transaction was mirrored into the active document.**
`_openTransaction` opens a `-> name` transaction in the active document
when it opens one elsewhere, so that an undo there reaches both. For an
implicit transaction the mirror was not implicit itself, so neither the
invocation's end nor the GUI's closer committed it: a bare write on a
document that is not the active one left an empty `-> <implicit>` open in
the active one, and later an empty step in its undo list
(`DocumentObserverCases.testDocument` saw its `DocOpenTransaction`).
Implicit transactions already stay out of the application's transaction
at commit (`closeActiveTransaction` is not called for them), so they are
now not mirrored at all. Gtest
`implicitTransactionIsNotMirroredIntoTheActiveDocument`.

**What an observer sees of an implicit transaction (user ruling,
2026-09-25).** Two cases still differed from the log-off rule, both from
implicit transactions at invocation depth 0 with no GUI to close them:

- `DocumentObserverCases.testDocument`: a bare `Doc1.Comment = ...` opens
  an implicit transaction, so `DocOpenTransaction` arrives before
  `DocBeforeChange`.
- `DocumentObserverCases.testObject`: the implicit transaction a bare
  `addObject` opened is still open at `Doc1.recompute()`, whose scope
  commits it on return -- `DocCommitTransaction` after `DocRecomputed`.
  The recompute's own writes join that transaction rather than making a
  step of their own.

Offered: commit such a left-open transaction at the START of a recompute,
so the recompute is a step of its own as 24.1 has it; or hide implicit
transactions from observers. **Ruled: neither.** Implicit transactions
signal open and commit like any other transaction, the grouping stays as
it is -- edits left open at depth 0 and the recompute that follows them are
one step -- and the two tests expect those signals when the log is on
(`transactionLogIsOn()` in `Document.py`). `testUndoDisabledDocument`
failed only because `testDocument` stopped before closing its documents.

**Suites.** Log off: Python 2911 OK, ctest 822/822 (+2 gtests). Log on:
Python **2911 OK** (52 skipped: the 50 plus the two FileBlobs cases that
skip with the log on) and ctest **822/822** -- the gate of 22.1 is met.

**A trap in the log-on ctest run.** `ctest -j6` in a `FREECAD_USER_HOME`
whose `user.cfg` sets `TransactionLog=1` does not keep it: several test
processes load and save the same `user.cfg` at once, one of them saves a
stripped copy, and the tests after it run with the log off. Run alone,
none of the 832 entries loses the setting; `-j6` lost it every time. So a
log-on ctest run is `-j1` (about 20 minutes), with a check afterwards that
`user.cfg` still sets it. The log-on ctest figures of 24.11 and 24.12 came
from `-j6` runs and were at most partly log-on; the `OmniControl`
failure 24.12 saw was real all the same.

## 25. Phase 7: crash recovery (design, 2026-09-25)

Phase 7 as section 15 lists it is recovery *and* streaming; this section
is the recovery half only -- the log's tail replayed over the last version,
replacing the autosave / recovery-file machinery (22.1). Streaming and
per-user undo in a shared session are later.

### 25.1 What exists (survey, 2026-09-25)

- **What a crash leaves.** `~Document` deletes the transient directory
  (`<cache>/<Exe>_Doc_<Uid>_<hash6>_<pid>`); a crash does not, so
  `history/log.db` and `blobs/` are left behind, next to the Gui's lock
  file `<cache>/<Exe>_<pid>.lock`. The session row stays `closed = 0`.
- **The Gui's recovery** (`DocumentRecovery.cpp`) finds dead processes by
  the lock file it can lock, globs their `_Doc_*_<pid>` directories and
  keeps only those with `fc_recovery_file.xml` -- a log-on directory with
  no AutoSaver file is not seen, and one with both is recovered from the
  AutoSaver copy alone. Restore is `openDocuments` of the recovery file
  under the original `FileName`, then the recovery files are moved into
  the new document's transient directory and the old one is removed.
- **What the log holds at a crash.** Every committed SQL transaction
  (WAL, `synchronous=NORMAL`: safe against a process crash; the last few
  can roll back on a power loss, consistently). Not held: jobs still in
  the worker's queue, and -- the large one -- **every pending after ref**
  (20.2 decision 4): the newest value of each property changed since it
  was last copied or snapshotted exists only in memory. The op is there
  with `vafter` empty.
- **Anchors.** A version is taken at save, at open (version 1 of the
  file as read) and on the cadence of 16.3, which defaults to off, and
  whose seconds rule never fires before a first save or open. A new
  document never saved has no version at all.
- **Machinery.** `_materialiseVersion` rebuilds a version's files from
  the log alone; `restore(dir)` reads them; `_applyVersion` makes a live
  document equal a scratch one. Rows apply only *reversed*
  (`_prepareRevert`/`_applyRevert`); forward replay exists only in a
  gtest (`replaysToIdenticalDocument`). Nothing opens a `log.db` by path;
  `adoptStore` (embedded mode) takes one over for a live document.
- **Blobs.** The rule "a blob's segment is durable before a row naming it
  commits" is built (`makeDurable` in `writeValues`, `snapshot`,
  `putBlob`), so no committed row names a lost blob; batches not yet
  flushed are lost with the rows that would have named them. Opening a
  store directory that has segments only numbers new ones past them; the
  15.8/15.11 recovery pass (EOCD check, newest number wins, finish a
  merge, delete the incomplete) is not built.

### 25.2 Proposed shape

1. **Bound the loss: resolve pending afters when the document goes
   quiet.** After a commit, a debounced idle job (Gui: the event loop
   idle for about 1 s; headless: the invocation's end) calls
   `resolvePending()`, whose main-thread cost is the `Copy()` of each
   pending property and whose serialisation is the worker's. A crash
   then loses at most the transactions since the last quiet moment. The
   cost is a second copy of a heavy property that is edited, left, and
   edited again (the sketch geometry of 20.2), paid in idle time.
2. **Anchor: the newest version, or an empty document.** A document
   with no version replays from nothing -- every object's `create` op
   and its sets are in the log.
3. **Replay forward, a clean prefix.** Walk the `txn.parent` chain from
   the head back to the anchor's seq and apply each row forward by
   after values -- `_applyRevert`'s passes mirrored (create objects, add
   dynamic properties, restore after values, remove dynamic properties,
   remove objects). Undo, redo and restore rows are rows like any other.
   The replay stops before the first row with a non-derived after still
   pending, so the recovered document is a state the user actually had.
   Derived values not recorded follow 24.1's ruling for cold undo:
   missing derived -> the object is touched.
4. **The recovered document keeps its history.** Recovery creates the
   document from the anchor (materialised, `restore(dir)` under the
   original `FileName` and label), applies the tail, and takes over the
   old `history/` and `blobs/` -- moved into the new transient directory
   as the dialog moves recovery files today, the store reopened as
   `adoptStore` does, the crashed session closed with a mark, a new one
   opened. Undo and the log browser reach across the crash. The document
   is left modified, as today.
5. **The blob store sweep runs here** (15.8/15.11): read each segment's
   directory, keep a segment only if its EOCD and central directory
   check out, the highest segment number wins a hash seen twice, a
   segment wholly superseded or unreferenced is deleted. Loose blob
   files are taken as they are.
6. **The Gui finds log-on directories.** `checkDocumentDirs` keeps a
   directory with `history/log.db` as a candidate; label, file name and
   last-change time come from the log (new `meta` keys written at open
   and on rename), so no `fc_recovery_file.xml` is needed; "Overage" is
   the project file newer than the last row. With the log on, AutoSaver
   writes nothing for that document (22.1: autosave is the log); with
   the staging switch off, nothing changes.
7. **A replay length bound.** A long session with no save replays
   everything since open. The unnamed-version cadence of 16.3 is what
   bounds it; its defaults (both 0 today) and the seconds rule's start
   (a new document counts from its creation) need setting.

### 25.3 Rulings (user, 2026-09-25)

| Question | Ruling |
| --- | --- |
| Bounding the loss of pending afters | **Stream every commit to the background thread** (user: "can we stream every commit to a background thread for persistence") rather than resolving when idle. Built as 25.4 below: every committed transaction is durable once the worker's queue drains. |
| What the recovered document is | **It keeps its history**: anchor plus tail, under the original file name and label, with the old log and blob store taken over, so undo and the browser reach across the crash (25.2 item 4). |
| AutoSaver with the log on | **Replaced for log-on documents**: AutoSaver writes nothing for a document with a log; the Gui recovery finds `history/log.db`. Unchanged with the staging switch off. |
| The snapshot cadence | **A setting for the transaction threshold** (`TransactionLogSnapshotTransactions`, now with a default) **and the existing autosave interval** (`AutoSaveTimeout`, `AutoSaveEnabled`) for time, replacing `TransactionLogSnapshotSeconds`; the clock of a new document starts at its creation. |

### 25.4 Streaming the after values

The obstacle 20.2 decision 4 set: the worker serialises only detached
copies and never reads a live property. So at commit, on the main thread,
the log takes a `Copy()` of every property whose after ref the commit
leaves pending -- the same copy `resolvePending()` takes -- and posts it
with the transaction's job; the after ref resolves when the job runs, and
nothing is left pending past a commit.

Paid for by moving the copy, not adding one: the next transaction to
write that property would `Copy()` it for its undo before value, and that
copy is the same value, because any change in between passes through
`aboutToSetValue` first. The log therefore keeps the commit's after copy
(shared, as decision 4's copies already are) in a cache keyed by property
id, and the first `aboutToSetValue` on the property takes it: a
transaction recording the write adopts it as its before copy instead of
copying, and any other write drops it. Steady state: one copy per edit
cycle, as before, taken at commit instead of at the next edit. Only the
properties a commit *set* are cached; the sets a `create` implies (every
property of a new object) are copied and serialised but not kept, so the
cache is the working set of edited properties, not the document.

What a crash can still lose: the jobs in the worker's queue (the last
commit or two) and, on a power loss, the last SQL transactions WAL with
`synchronous=NORMAL` has not synced -- both a consistent prefix.

### 25.5 Build order

1. **7.a** Streaming (25.4), with the cache; both suites, the log on, and
   a gtest that no ref is pending after a commit drains.
2. **7.b** The cadence: `TransactionLogSnapshotTransactions` default 200;
   time from `AutoSaveTimeout`/`AutoSaveEnabled`; a new document's clock
   from creation; `TransactionLogSnapshotSeconds` gone.
3. **7.c** Forward replay and the App recovery entry point: a document
   from a leftover transient directory's anchor plus tail, the old store
   taken over, the crashed session closed; label and file name kept in
   `meta`. Python binding; gtests that recover a copied directory and
   compare every property with the original.
4. **7.d** The blob store's recovery sweep (25.2 item 5).
5. **7.e** The Gui: the recovery dialog finds log-on directories;
   AutoSaver stands aside for log-on documents; a GUI check that kills a
   process mid-session and recovers it.

### 25.6 7.a and 7.b as built (2026-09-25)

**7.a, streaming.** `TransactionLog::onCommit` copies, on the main thread,
the live value behind every after ref the commit would have left pending
-- the sets of a `create`, of an added dynamic property, and of a changed
property -- and hands the copies to the same job as the transaction's
before values; `ValueTask::afterIndex` fills the op's `vafter` before the
rows are appended, so the transaction's rows and all its values are one
SQL transaction and one flush of the blob store, and the op's before is
superseded by its after there and then (23.2). `_pending` stays empty
after a commit; `resolvePending()` remains for what a commit does not
cover. `FC_TXNLOG_NO_STREAM` restores the old behaviour, for measuring.

The copy of a changed property is kept in `App::TransactionCopyCache`
(`Transactions.h`), keyed by property id, with the hash the worker wrote
it under and the live status at commit (`Touched` cleared).
`TransactionObject::setProperty` takes it for the first write after the
commit when the type and status still match, as its undo before value,
and the worker then reuses the hash instead of serialising the value
again; `Property::aboutToSetValue` drops what is left once the write's
recording is done, and `~Property` drops a gone property's. A version
(save, snapshot, restore) drops the log's entries: after it a fresh copy
of a shape names its blob, where a kept one carries the exported bytes,
and the first edit after a save should log the small reference.

Five defects the first runs found, all fixed:

- **The copy's status was read.** `Property::getStatus()` is virtual in
  effect -- a link's reads `isTouched()`, which reads the linked objects
  -- and a detached copy does not keep those alive: a SIGSEGV in
  `SketcherTests` (`delExternal`). The status compared is now the live
  property's, recorded at commit; nothing asks the copy.
- **Copies died on the worker.** A copy of an expression engine is a
  `PropertyExpressionContainer`, which registers itself in a list the
  main thread walks (`slotRelabelDocument`); releasing the last share on
  the worker raced it ("pure virtual method called", SIGABRT). The
  worker now retires every copy it has written to `_retired`, and the
  main thread releases them at its next post or flush. The race existed
  before for a transaction trimmed while its copies were queued; streaming
  made it common, because a create's after copies are the job's alone.
- **The directory went before the log.** `~Document` shut the blob store
  down and deleted the transient directory, and only then destroyed the
  log, whose queue a commit's values now often still fill: segments were
  written into a deleted directory ("Blob store: cannot write
  .../seg-1.1"). The log is now ended first (`noLog` set, so nothing
  makes another).
- **A flush held the GIL.** That failure hung the Python suite: `~Document`
  holds the GIL around the log's end, the worker's `FC_ERR` went to the
  console, which `FreeCADCmd` redirects to Python, and the main thread
  waited for the worker while holding what the worker waited for.
  `TransactionLog::flush()` now releases the GIL while it waits, since any
  message from the worker could meet a flush called from Python.
- **The process ended under the worker.** A process may end with documents
  open (the Part gtests never close theirs); no `~TransactionLog` runs
  then, and a worker still exporting a shape met OCCT's statics being
  destroyed -- a SIGSEGV in `GeomTools_CurveSet::PrintCurve` after the
  tests passed. Every log registers itself (`liveLogs`), and an `atexit`
  handler, registered with the first log and so run before the
  destructors of the libraries loaded earlier, drains and joins every
  worker; a stopped log drops what is posted after.

**The check.** `FC_TXNLOG_CHECK_COPIES` compares every adopted copy with
the live property and reports `stale log copy of ...` when both `isSame`
and `isSameContent` say they differ -- a write that escaped
`aboutToSetValue` (23.6), which an adopted copy would otherwise turn into
an undo that restores the value before the escape. Links are not
compared (`isSame` reads their targets). `isSame` alone gave four false
reports: `PropertyPath::isSame` answers false always, and
`PropertyTopoShapeList::Copy` deep-copies its shapes (it also leaked a
`TopoDS_Shape` per shape per copy; fixed).

**Measured** (this box, `FreeCADCmd`, log on, median of 10 edits after 2,
two runs each): moving one vertex of a 1000-line sketch, the write that
takes the undo copy went from 10.0-10.3 ms to 8.9 ms and the commit from
3.3 ms to 4.1-4.4 ms -- the copy moved from the next edit to the commit,
and the edit cycle is unchanged at about 13 ms. A `Box.Length` edit: 0.8
ms either way.

**7.b, the cadence.** `TransactionLogSnapshotTransactions` defaults to 200.
`TransactionLogSnapshotSeconds` is gone; the time rule is the autosave
interval, `AutoSaveEnabled` and `AutoSaveTimeout` (minutes, 15), now in
`DocumentParams` beside the Gui's reading of the same keys. The clock
starts when the document's log is made, so a document never saved or
opened gets versions too.

### 25.7 7.c and 7.d as built (2026-09-25)

**7.d, the blob store's sweep.** `FileBlobManager::recoverStore()` reads
what a crashed session left in the blob directory: per segment number the
newest generation whose central directory opens (`BlobArchive` throws on
one that does not) is kept, older generations and files cut short are
deleted, and names no writer of this store makes (`.tmp` of older builds)
too; `_segments` and `_where` are rebuilt with the highest number winning
a hash seen twice; loose files are hashed. Nothing is live then:
`recovered(hash)` hands back a handle -- packed, on the segment that holds
it, or the loose file -- and `endRecovery()` deletes the loose files
nobody took and schedules maintenance, whose first pass deletes a segment
with nothing live and rewrites one mostly dead. Gtest
`blobStoreRecoversALeftoverDirectory` (two generations of one segment, a
segment cut in half, a loose file).

**7.c, the document.** `Application::recoverDocument(transientDir)`
(Python `FreeCAD.recoverDocument`) reads the label and file name the log
keeps in `meta` (`TransactionLog::noteIdentity`, written when the log is
made and whenever `Label` or `FileName` changes), makes a new document
under them with undo on -- the history carries on whatever a new document
gets -- and calls `Document::recoverFromLog`:

1. `TransactionLog::recover` moves `history/log.db` in with its WAL (the
   rows committed and not checkpointed), moves the blob files in and runs
   the sweep, takes a handle on every blob the log names, closes the
   sessions the crashed process left open and opens its own.
2. The anchor is the newest version: materialised into the old directory
   -- the restore renames this document's -- and restored as an open reads
   a file, with the log's own snapshot of it suppressed (`checkingOut`).
   With no version the document starts empty; every object's create is in
   the log.
3. `_replayLog` folds the rows after the anchor's seq into the end state --
   objects by id with name and type, dynamic properties with their
   metadata, the newest after value of each property -- and applies it in
   the passes of a cold undo, forward: create, add and remove dynamic
   properties, restore the values, set the touched state, remove. The
   touched state is the session's: an object a recompute record names
   after its last input change is purged of the touches the restored
   inputs made, and one edited since, or whose derived values the log did
   not keep, is touched. A derived write alone proves nothing -- a
   primitive rewrites its shape as its input changes and stays touched
   (20.3) -- so only the record counts. A row with a non-derived set whose after
   never reached the log (not a removal's) ends the replay before it.
   Nothing written is a transaction (`DocumentP::replaying` makes
   `transactionsWanted()` false).
4. `_rebuildUndoFromLog` gives the undo and redo stacks the session had,
   as cold stubs: a row with ops is a step and clears redo; an `undo` row
   naming the undo top moves it to redo under the undo row's seq, a `redo`
   row naming the redo top moves it back; a selective undo is a step.
5. A `recover` row records the source, the anchor, the rows replayed and
   the crashed sessions; `endRecovery()` runs; the old directory goes.

Gtests `recoversACrashedSessionFromItsLog` (an anchor snapshot, then a
set, a create with a dynamic property, a create and a remove, an undo:
values, ids, the dynamic property, the removed object, both stacks, redo
and two undos across the crash) and `recoversShapesFromTheLeftoverStore`
(Part: a saved box as the anchor, a length change and a new cylinder in
the tail, the box's height edited and not recomputed; shapes read back
from the pack store, the cylinder clean and the box touched).

**Suites.** Python 2911 OK and ctest 825/825, with the log on (`-j1`,
`FC_TXNLOG_CHECK_COPIES` set: no stale copy, no crash) and off; run
before the touched-state rule above, which only recovery reaches, and
which the 46 log gtests cover.

### 25.8 7.e, the Gui, as built (2026-09-25)

**The recovery dialog.** With the log on, `DocumentRecoveryFinder` keeps a
dead process's transient directory that holds `history/log.db` as a
candidate, and `getRecoveryInfo` describes it from the log's `meta`
(`TransactionLog::readRecoveryMeta`: label, file name), preferring the log
to any AutoSaver file beside it; it is out of date ("Overage", not listed)
when the project file was saved after the log's last write (`log.db` or its
WAL). "Start Recovery" calls `App::Application::recoverDocument` for such
an entry -- the old directory goes with it -- and marks the document
modified; entries of recovery files take the old path. With the log off,
nothing changes.

**AutoSaver stands aside.** With the log on it writes nothing (22.1: the
log is the autosave); its timer and settings stay for the log-off case,
and `AutoSaveTimeout`/`AutoSaveEnabled` are the log's time cadence (7.b).

**The check.** `scripts/transaction-log-recovery-check.py`, two GUI runs in
one fresh user home:

    cd build/conda-relwithdebinfo-801
    for ph in crash recover; do
      QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-rc \
        RECOVERCHECK_OUT=/tmp/rc/out.txt RECOVERCHECK_PHASE=$ph \
        ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
        ~/works/sw/fcad/scripts/transaction-log-recovery-check.py
    done

The first saves a box, changes its length and colour, adds a cylinder, and
kills itself with SIGKILL; the second finds the start-up recovery dialog,
starts the recovery and checks the length, both shapes' volumes, the
colour (a view op, replayed through the Gui's resolver), the cylinder's
view provider, the modified flag, the camera, the undo names exactly as
the killed session had them, the old directory gone, and undo and redo
across the crash. 15 checks, all PASS on 2026-09-25.

**What the view state comes back as.** Every saved view-provider property
is in both halves: in the anchor's `GuiDocument.xml` and, since 24.10, as
logged ops, so it comes back as the session left it. The camera is in
`GuiDocument.xml` too, so in every version, but it is not a logged
property (24.10: its own session-only stack, `docs/MultiViewEdit.md`): it
comes back as the anchor had it -- the check sets a camera, saves, moves
the camera, and gets the saved one back. The camera moves since the last
version are the one thing a recovery loses, bounded by the cadence (7.b).

Phase 7's recovery half is built. A crashed session's lock file is left
for the next start's scan to remove, as before.

## 26. Phase 4, the rest: branches (design, 2026-09-25)

Section 15's phase 4 is named versions, embedding, branches and trimming.
Named versions and the embedded mode are built (section 21); this section
is what is left -- branch create and switch (17.1, 17.2), the `Branch`
property, the closed branch of 16.6, and trimming (16.7). Merge (17.3) and
concurrent writers (17.5) stay phase 6 and later.

### 26.1 What exists (survey, 2026-09-25)

- **The log is a line.** Every row's `parent` is the previous seq
  (`t.parent = _nextSeq` at all four append sites in `TransactionLog.cpp`);
  `txn` has no branch column and there is no `branch` table. The
  `version` table has had a `branch` column since phase 1, always `main`.
- **Every walk is linear.** `_replayLog(after)` (recovery's forward
  replay) and `_rebuildUndoFromLog()` read `store.transactions()` in seq
  order; the panel lists rows by seq; `lastOpOn` (the refuse rule) looks
  at every later row. On a tree each of these must walk the current
  branch's `parent` chain instead.
- **The switch's parts are built.** `_materialiseVersion(num)` rebuilds a
  version's files from the log; `restoreVersion` reads them into a hidden
  scratch document and `_applyVersion` makes the live document equal it in
  place -- objects matched by id, pointers, selection and view kept (24.8);
  `_replayLog` brings a version forward to any later row (25.7);
  `_rebuildUndoFromLog` makes cold stubs of a row sequence (25.7).
- **Object ids.** `lastObjectId` starts at a random 10..5000 and a restore
  sets it to the highest id read. Ids are `long`, 32 bits on Windows, and
  are the shape `Tag`.
- **The embedded guard's mismatch** (16.4 as built) warns and discards the
  copy: the fresh history starts from the file as found, and the old one
  is gone -- 16.6's closed branch was left for this phase. `Version` is
  `"<num> <save id>"`, set only in embedded mode.
- **Version numbers are a primary key**, and links (16.5) will pin them;
  16.6's "`main` restarts at a fresh version 1" cannot happen in one table.

### 26.2 Proposed shape

1. **Store schema 5.** `branch(id, name UNIQUE, from_version, from_seq,
   head_seq, id_base, created, closed)`; `txn.branch` (the branch id, `main`
   = 1 for every existing row). An older store gains both on open.
   `parent` becomes the current branch's head, not the previous seq; seq
   stays one counter for the whole log. The branch's `head_seq` moves with
   every row appended to it.
2. **Chain walks.** A store call `chain(head, stopAt)` returning the seqs
   from a head back to a row; `_replayLog`, `_rebuildUndoFromLog`, the
   refuse rule's `lastOpOn` and the panel go through it. A log that never
   branches walks exactly what it walks today.
3. **Create.** `createBranch(name, version)`: the fork version is named if
   it was not (17.1); from a row with no version, one is materialised by
   replay from the nearest older version on that row's chain. The new
   branch's ids start at `id_base` = the fork's highest id plus a random
   stride of 2^16..2^20 (17.2; ~2000 branches fit in 31 bits). Creating
   switches to the new branch; from the current head that is only a
   pointer move -- the document does not change.
4. **Switch.** In place, as restore to a version is (24.8), not a reload:
   the tip left is snapshotted (an unnamed version on it, 17.1), the
   arriving branch's newest version at or before its head is materialised
   into a scratch document, brought to the head by `_replayLog` along its
   chain, and `_applyVersion` makes the live document equal it -- view
   providers included, since with the log on every saved view property is
   recorded (24.10), which `_applyVersion`'s ViewObjectTransaction test
   predates. The apply writes no ops (the arriving branch's content did
   not change); a `switch` record with no ops goes on the arriving branch
   naming the one left. The log's per-property state (`_pending`,
   `_recorded`, the copy cache, `_hashById`) is reset, the next snapshot
   composes nothing it has not read. `lastObjectId` becomes the larger of
   the head's highest id and the branch's `id_base`.
5. **Eviction keeps each branch's newest version**, so a switch never
   replays more than its own tail.
6. **The file.** `Branch` (a document-level dynamic `PropertyString`,
   `NoModify`, beside `Version`) names the branch the file is; on open with
   an adopted history the log continues on it. The embedded copy carries
   every branch (VACUUM INTO copies them; 17.4).
7. **The closed branch of 16.6.** On a guard mismatch the copy is adopted
   anyway: its branches are closed, its `main` renamed `main@<save date>`,
   a new `main` starts with the file as found as its first version --
   numbered on from the copy's counter, `from_version` the copy's last one
   -- and a `restore` root row (parent 0: the gap has no ancestry).
8. **Python and the panel.** `getTransactionBranches()`,
   `createTransactionBranch(name, version=0, seq=0)`,
   `switchTransactionBranch(name)`, `renameTransactionBranch(old, new)`;
   `getTransactionLog()` rows gain `branch`. The panel: the branch in the
   status line with a switcher, rows of the current chain by default and
   all branches on a toggle, "Branch from version N..." and "Branch from
   here..." in the context menus.
9. **Trimming** (16.7) last: trim a branch, delete a branch, squash -- each
   a `trim` record naming what went -- over the existing `truncate` /
   collector machinery, generalised from "every row below seq" to "the rows
   only this branch reaches".

### 26.3 Build order (proposed)

1. **4.a** Store schema 5, `parent` as the branch head, chain walks; both suites
   unchanged (a log that never branches must not notice).
2. **4.b** Create and switch, App and Python; gtests that branch, edit
   both sides, switch back and forth and compare every property with the
   branch's head, ids never colliding, undo per branch.
3. **4.c** `Branch` property, the embedded round trip on a branch, the
   closed branch of 16.6.
4. **4.d** The panel.
5. **4.e** Trimming.

### 26.4 Rulings (user, 2026-09-25)

| Question | Ruling |
| --- | --- |
| Is a switch an undo step | **No.** Each branch keeps its own undo stack; a switch is a record with no ops on the arriving branch, and Ctrl+Z after it undoes that branch's last edit. |
| How far back undo reaches after a switch | **Back to this open**, today's limit: the arriving branch's rows written since the document was opened, as cold stubs. Older rows stay reachable through the browser (selective undo, restore). |
| Version numbers on 16.6's restart | **Numbering continues**: versions stay one per-document sequence and the new `main`'s first version takes the next number. 16.6's "restarts at a fresh version 1" is withdrawn. |

### 26.5 4.a and 4.b as built (2026-09-25)

**4.a, the store.** Store schema 5: `branch(id, name UNIQUE, from_version,
from_seq, head_seq, id_base, last_id, created, closed)`, `txn.branch`, and
`version.branch` now the branch's id where it was the text `main`. An older
store gains all of it on open, every row and version on `main` (id 1) with
its head the newest row. `append` moves the branch's head inside the same
SQL transaction as the row. `TransactionStore::chain(head, from)` is a
recursive CTE over `parent`, the rows of one branch oldest first;
`lastOpOn` takes a head and looks only at that chain. The log numbers every
row through `TransactionLog::number`: `parent` is the current branch's
head, not the previous seq, and the branch is kept in `meta` (`branch`) so a
recovery continues on it. The walks that were linear go through the chain:
`_replayLog`, `_rebuildUndoFromLog`, recovery's anchor (the newest version
on the chain), and the selective-undo refuse rule, which now also refuses a
row that is not on the current branch.

One defect the migration found on the way: **a read-only open wrote.**
The embedded guard (16.4) opens the copy inside the file read-only, as a
blob file, and the store's open ran its schema steps regardless -- harmless
while every step was a no-op at the current schema, fatal the moment a
step wrote (the new `main` row did: "attempt to write a readonly
database"). Worse, an embedded file saved at schema 4 would have hit the
`ALTER TABLE` and had its history set aside. A read-only store is now read
as it is: no journal mode set, no table made, no schema moved; the guard
reads only `meta`, which every schema has, and the store the log adopts is
a writable copy, migrated when it opens. Gtest `schema4StoreMovesOntoMain`
covers both opens.

**4.b, create and switch.** `Document::createBranch(name, version, seq)`
and `Document::switchBranch(name)`, over four helpers:

- `_leaveBranch` resolves the pending afters, snapshots the tip unless it
  already is a version -- only records (save, snapshot, switch, branch)
  after the chain's newest version -- and keeps the branch's `last_id`.
- `_checkoutHead` makes the document the state at the log's head, in place
  and unrecorded (`replaying`): the newest version on the chain read into a
  scratch document (`_readVersion`, factored out of `restoreVersion`) and
  applied by `_applyVersion` with the view providers (its new `views`
  argument), then `_replayLog` over the rows after it. A chain with no
  version starts from no objects.
- `TransactionLog::forgetLiveValues` after it: the pending map, the held
  set (`_recorded`), `_hashById` and the commit copies kept for the next
  edit all described the state left, and the next snapshot serialises
  afresh.
- `_arriveOnBranch` sets the id counter to the largest of the branch's
  `last_id`, its `id_base` and any id the checkout brought, and rebuilds
  the stacks from the chain's rows after `undoFloor` -- the log's last seq
  when the document was opened (26.4) -- as cold stubs.

A switch clears both stacks first (their steps are the other branch's),
then `setBranch`, checkout, forget, arrive, and a `switch` record on the
branch arrived on. A create from the current head changes nothing in the
document and keeps the stacks hot -- the chain behind the new head is the
one they were made on -- and only moves the log to the new branch; from a
version or a row behind the head it checks that point out like a switch.
The fork version is named (`branch <name>`) if it was not; a row with no
version gets one, snapshotted once the document is there. `id_base` is the
largest id or base any branch has, plus a random 2^16..2^20. A `branch`
record opens the new branch. Eviction keeps each branch's newest version,
so a switch only replays its own tail.

Python: `getTransactionBranches()`, `createTransactionBranch(name,
version=0, seq=0)`, `switchTransactionBranch(name)`,
`renameTransactionBranch(name, newName)`; `getTransactionLog()` and
`getTransactionVersions()` rows name their branch.

Gtests: `branchesAreChainsInTheStore`, `schema4StoreMovesOntoMain`,
`branchesSwitchInPlace` (create at the head keeps the steps; main and side
edited apart, switched back and forth -- the same C++ objects, values, ids,
each branch's own undo names, cold undo and redo on a branch, the side's
id counter going on), `branchFromAnOlderVersion`,
`evictionKeepsEachBranchsNewest`, `recoveryContinuesOnTheBranch`. All 35
log gtests pass, with `FC_TXNLOG_CHECK_COPIES` too. With
`TransactionLogVerify` on, `evictsUnnamedVersions` and
`reverseDeltasFollowSupersession` fail: under verification a composed
snapshot hashes its full bytes, so `docxml_hash` names no stored entity,
which those two assume. That is the tests, not branches, and not chased.
A Part box through the Python API: its shape comes back from the version on
each switch, 1000 and 3000 mm^3, without a recompute.

### 26.6 4.c as built (2026-09-25)

**`Branch`.** An embedded save sets a third document-level dynamic
property beside `History` and `Version`: `Branch` (`PropertyString`,
hidden, read-only, `NoModify`), the name of the branch the file is. The log
itself does not need it -- the copy's `meta` names the current branch, and
`openStore` continues on it -- so it is for whoever reads the file, a
FreeCAD that knows no log included. A restore to a version leaves it alone,
with `History` and `Version`.

**The embedded copy keeps each branch's newest version.** The retention of
16.4 dropped every unnamed version from the copy, which would have dropped
every branch's tip snapshot and left a switch in the file opened elsewhere
replaying from the fork. The copy now keeps the newest version of each
branch as well as the named ones -- the eviction rule of 26.2 item 5.

**The closed branch of 16.6.** A guard mismatch no longer discards the
copy. `TransactionLog::adoptClosed` adopts it as `adoptStore` does, closes
every branch in it, renames its `main` to `main@<save date>` (`#2`, `#3`
if taken), opens a new, empty `main` from the copy's last version, and
puts the log on it; the on-open snapshot then makes the file as found the
new `main`'s first version, its `restore` row a root (`parent` 0). The
number it takes is past the copy's `version_counter` -- the number the save
that wrote the file took in the store it came from, which the file edited
since is not. A closed branch cannot be switched to (`switchBranch`
throws, pointing at branching from one of its versions); branching from
its versions works.

Python cases `Document.TransactionBranchCases`: the branch travels in the
file (saved on `side`, opened on `side`, switched to `main` there), and a
file whose `LastModifiedDate` was restamped elsewhere opens with three
branches -- two closed, `main@...` among them -- its first `main` version
numbered past the save's, rooted at 0, the closed branch refused, and a
branch revived from the fork version.

### 26.7 4.d, the panel, as built (2026-09-25)

A second bar on the log panel: **Branch:** a switcher listing every branch,
the closed ones marked and disabled, whose choice calls `switchBranch`; a
**Branch...** button making a branch from the current head; and **All
branches**, off by default, so the transaction list shows the current
branch's chain only. The list gains a Branch column; the filter text and
the chain decide together which rows show, recomputed on every refresh, so
a switch re-filters rows already listed. "Branch from here..." on a
transaction row and "Branch from version N..." on a version ask a name and
call `createBranch` with the row or the version. The status line names the
branch beside the mode (`[session, branch side]`). **Rename...** (added
with 4.e) renames the branch shown in the switcher through
`Document::renameBranch` -- Python `renameTransactionBranch` goes the same
way -- which refuses an empty or taken name, keeps the file's `Branch`
property in step when it is the current branch, and emits the signal so
the panel reloads.

**The graph** (user, 2026-09-25: "display branching like git UI", newest
first by default). The transaction list is newest first, and its first
column draws the history as `git log --graph` does: a node per row on a
lane, each lane waiting for the parent of the row above it, lanes that wait
for the same row converging on it -- a fork, seen from its branches, and
what a merge's second parent (phase 6) will draw the same way. It is laid
out over the rows shown (`layoutGraph`, after every visibility change):
a row's graph parent is its nearest shown ancestor, so the filter text,
the single-branch view and **Hide records** keep it connected. Lanes are
coloured by branch; a record (a row with no ops: recompute, snapshot,
switch, branch, trim) is greyed with a hollow node, and **Hide records**
leaves only the changes. Labels right of the lanes, as git shows refs:
each branch's head on its nearest shown row (the current one bold; in the
single-branch view only its own, since another's would slide down to the
fork and read as ending there), and each version as `vN name` on the row
it was taken at. The graph is a pane of its own left of the list, in a
splitter (user, 2026-09-25): a second view on the list's model and
selection, showing only the graph column, with its own horizontal scroll
so lanes and labels can outgrow it without moving the list, and folded
away by dragging the splitter shut. The two scroll together vertically,
rows are the list's height (the delegate sizes a row from a text cell),
both keep a horizontal bar so their viewports are the same height, hidden
rows are mirrored, and the graph's right-click is the list's menu. The Gui
check verifies the order, the pane, and that each scrolls the other; the
drawing itself is looked at in screenshots.

A switch commits nothing, so none of the panel's refresh triggers fired.
`App::Document::signalSwitchBranch` is emitted after a switch and after a
create; the panel refreshes on it, and it is there for the rest of the Gui
(undo actions, the tree) to follow a switch too.

**The Gui check.** `scripts/transaction-log-branch-check.py`, one GUI run
in a fresh user home:

    cd build/conda-relwithdebinfo-801
    QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-bc \
      BRANCHCHECK_OUT=/tmp/bc/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
      ~/works/sw/fcad/scripts/transaction-log-branch-check.py

A red box on `main`; on `side` it is made longer and blue, and a green
cylinder is added (its colour set outside any transaction, an implicit
step). Switching to `main` and back checks the length, the volume from the
version's shape, both colours -- the view providers following the switch,
including one on an object the switch recreated -- the cylinder's absence
and return, and each branch's own undo steps. Then the panel: both
branches listed, `side` current, `main`-only rows hidden, and a switch made
through the switcher, after which the box is red and short again and the
rows re-filtered. 19 checks, all PASS on 2026-09-25; the script puts the
log setting back before it exits.

### 26.8 4.e, trimming, as built (2026-09-25)

The three operations of 16.7, each refused in the middle of something else
(`_checkBranchable`), each followed by `signalBranchesChanged` (the Gui
signal of 26.7, renamed from `signalSwitchBranch` now that a trim or a
delete also changes the history the panel shows; the panel reloads on it,
since rows can go as well as come).

- **Trim** -- `Document::trimBranch(name, version)`, Python
  `trimTransactionBranch`, the panel's "Trim <branch> to version N...".
  The rows of the branch's chain up to and including the version's row go,
  except those any other branch's chain holds; so do the unnamed versions
  taken at the rows that go, except one a branch forked from. The version
  kept is named (`trim <branch>`) if it was not. Without a version, the one
  at the head -- made now for the current branch; another branch's tip
  snapshot is already there (26.2 item 5), and a head with no version is
  refused. A `trim` record names what went.
- **Delete** -- `Document::deleteBranch(name)`, `deleteTransactionBranch`,
  the panel's "Delete..." (with a confirmation). Not the branch the
  document is on. The branch row, the rows of its chain no other chain
  holds, and its versions and any taken at those rows, except a version a
  branch forked from, which stays (named, since forking named it) with the
  deleted branch's id on it. A `trim` record.
- **Squash** -- `Document::squashVersions(from, to)`,
  `squashTransactionVersions`, Python only for now. The row `to` names is
  rewritten in place (`TransactionStore::replaceTransactions`) as one
  `squash` transaction whose parent is `from`'s row, and the rows between
  go, so no seq changes and whatever follows `to` -- rows, a branch forked
  at it -- still follows it. Refused when `from` is not behind `to` on one
  history, when a branch forks strictly between them, or when a named
  version sits between them; the unnamed ones between go. Its ops are the
  net change, in the shapes a commit writes (so a cold undo reverts it and a
  replay applies it like any row): an object born and gone inside leaves
  nothing; one born inside is a `create` then a set per property, a dynamic
  one's `addprop` first; one there before and gone after is a set per
  property carrying its before (and a dynamic one's metadata) then the
  `remove`; every other property is its before at the start and after at
  the end -- a `set` when both ends have it and they differ, `addprop` and a
  set when only the end does, `delprop` when only the start does.

Three things the operations needed of what was there:

- **A trimmed chain's base.** After a trim a branch's chain ends at a row
  whose parent is gone, and the version that anchors what is left is at
  that missing row. The lookups of the newest version on a chain -- the
  checkout, the tip test of `_leaveBranch`, recovery's anchor -- take
  `chainPoints`: 0, every row of the chain, and each row's parent.
- **Branch ids are not reused.** SQLite hands out the highest integer key
  again once its row is deleted, and a version kept from a deleted branch
  still carries that id. `addBranch` numbers past every id the branch,
  txn and version tables name.
- **The stacks follow.** A trim or squash that touches the current
  branch's history rebuilds the undo and redo stacks from what is left
  (cold stubs, back to the open, 26.4); the hot steps go with the rows they
  named.

Gtests `trimAndDeleteBranches` (ancestry another branch holds survives a
trim; a delete takes the branch's own rows and tip version but not the
fork; a trim to v1 then to the head; the document still branches from v1
and switches back; the `trim` records) and `squashFoldsTheNetChange` (an
edit, an object born and removed, a dynamic property added, an object
created, one removed; the refusals for a reversed pair, a named version
between, and a fork between; the squash row's ops by kind, nothing for the
object born and gone; the squash undone cold and redone, ids and values
back). 37 log gtests pass. The Gui check of 26.7 now ends by deleting
`side`: the panel reloads to `main` alone with no `side` rows, and the box
is as it was -- 23 checks, all PASS.

**Open: an intermittent hang.** Twice, a run of the log gtests hung at the
start of `blobsAreEntitiesTheLogHolds` -- before its first save printed
anything, so in `setUndoMode(0)`, the first commits, or
`PropertyFileIncluded::setValue` adopting a file into the blob store while
the log's worker makes segments durable. It did not recur in some 360 runs
since -- 150 sequential, 150 six at a time, the rest in the foreground --
and was never caught with a stack. It is not known whether it predates
section 26; the test uses nothing of branches but the row numbering. The
suites' ctest now runs with `--timeout 300`, so a recurrence fails by name
instead of stalling.

Phase 4 is built: named versions and the embedded mode (21), branches,
the file's branch, the panel, trimming (26).

## 27. Phase 5: pinned links (design, 2026-09-25)

Section 15's phase 5 is the `version` attribute on `PropertyXLink`, the
checkout of a linked version, and the fallback with its status (16.5);
17.4 adds that links pin versions, never branches. This section is the
survey, the proposed shape and the questions to rule on before building.

### 27.1 What exists (survey, 2026-09-25)

- **The link.** `PropertyXLink::Save` writes `file`, `stamp` (the linked
  document's `LastModifiedDate`), `name`, `resolve`, `partial` and the
  subs (`src/App/PropertyLinks.cpp` ~4420). `Restore` reads them and
  `setValue(file, name, subs, shadows)` defers the object lookup to a
  `DocInfo`.
- **One document per file.** `DocInfo` is keyed by the absolute path
  (`_DocInfoMap`); `init` attaches the open document whose `FileName`
  matches, else `addPendingDocument` opens it, and `attach` looks the
  objects up by name. `Application::getDocumentByPath` and
  `openDocumentPrivate` refuse a second document for one path. A document
  can already be opened from one place under another `FileName`:
  `openDocumentPrivate(FileName, propFileName, ...)` is what crash
  recovery uses.
- **Versions.** A version row has a per-document `num` and a `uuid`
  (`LogVersion`); `nameVersion` makes it `named`, which eviction
  (`TransactionLogKeepVersions`) never takes. Since 26.4 numbers are one
  sequence per document even across 16.6's closed branches, so a number
  a link recorded keeps meaning the same snapshot after the file is
  edited elsewhere and its history adopted as closed.
- **What travels.** Only `TransactionLog=2` (embedded) puts a history in
  the file: `History` holds the database and the blobs of the named
  versions and the current one (16.4). In `session` mode nothing outlives
  the transient directory, so nothing can be pinned across files. A save
  with the log off or in `session` mode **empties** a `History` it
  finds (`Document::embedHistory`), so one such save of the linked file
  drops every version a link pins.
- **Reading a version** needs the document's own log:
  `_materialiseVersion` goes through `TransactionLog::readValue` /
  `readBytes`, which read the entity from the store and a `file` entity
  from the document's `FileBlobManager`, decode `zstd` and `delta`
  chains and compose `composite` entries. None of that needs a live log,
  only a store, a blob lookup and the composer -- but it is on
  `TransactionLog`, which exists only with the log on
  (`getTransactionLog()`), and an embedded history is adopted only then.
  `PropertyHistory` restores its blobs into the document's blob manager
  in every mode.
- **A version as a document.** `_readVersion` materialises into a
  directory and restores a hidden scratch document from it (`noLog`,
  undo off, `FileName` the directory). There is no read-only document:
  the nearest things are `TempDoc` (no save prompt), `SkipRecompute`, and
  the `LiveImport` status the Gui checks to gate mutating commands.
- **Relative paths.** A link's `file` is resolved against its owner
  document's `FileName` directory (`DocInfo::getDocPath`). A version
  restored with `FileName` = its checkout directory would resolve its own
  relative links against the wrong place.

### 27.2 Proposed shape

1. **The attribute.** `<XLink ... version="17" vuuid="..."/>`, both
   absent on an unpinned link, which saves and behaves exactly as today
   (16.5 item 4). The pin is part of the property's value: `isSame`,
   `Copy`/`Paste` and the log's capture carry it, so changing or clearing
   it is an ordinary `set` op, undoable (16.5, last paragraph). A
   FreeCAD without phase 5 ignores the attributes and resolves live.
2. **Identity.** A pinned link's `DocInfo` is keyed by
   `<absolute path>@v<num>`, and the document it attaches to has exactly
   that as its `FileName`. Its directory is the file's directory, so the
   version's own relative links resolve as the file's do;
   `getDocumentByPath` of the plain path never finds it, nor the reverse.
   Two links pinned to v17 of one file share one document; v17 and v18
   are two documents (16.5 item 2). Its name and label are
   `Part@v17` (label `Part (v17)`).
3. **A reader, not a log.** The decode half of `TransactionLog` --
   `readBytes`, `readValue`, `composeEntry`, `_materialiseVersion` --
   moves into an `App::VersionReader` over a `TransactionStore` and a blob
   lookup. `TransactionLog` uses it for its own store; a pinned link uses
   it over the linked file's embedded copy, opened read-only, with the
   blobs its `History` restored. Resolving a pin then does not need the
   log switched on in the session that opens the linking file.
4. **Resolution.** `Application::openVersion(path, num, uuid)`, reached
   from `DocInfo` for a pinned link:
   - the file is open with a live log holding version `num` whose uuid
     matches: read from that log (it may hold versions named since the
     file was last saved);
   - otherwise the file's embedded `History`, from the file as loaded
     (27.3 Q1);
   - the version is materialised into the new document's own transient
     directory and restored from there, so it dies with the document.
5. **The version document.** `noLog`, undo off, `TempDoc`,
   `SkipRecompute` (the snapshot carries its shapes, 16.1; a recompute
   could only change them), and a new status `ReadOnly`: opening a
   transaction on it throws, the Gui gates mutating commands on it as it
   does for `LiveImport`, Save refuses. It takes part in no transaction,
   so the cross-document fan-out of section 12 never reaches it (16.5).
6. **The fallback.** Anything that fails -- no history in the file, no
   such version, a uuid mismatch, a missing blob, a load error -- resolves
   the link to the physical file exactly as an unpinned one, keeps the pin
   in the property, and records why: a new link flag `LinkPinFallback`
   plus the reason, which the tree shows as an overlay and a tooltip
   (16.5 item 3). A later open retries.
7. **Pinning.** `pinLink(version)` names that version in the linked
   document's live log if it was not (16.3, name `pin`), and the linked
   document becomes modified: the pin reaches the file only when the
   linked file is saved in embedded mode (27.3 Q3). Clearing a pin is the
   same property write with no version.
8. **Python and the Gui.** `DocumentObject.pinLink(prop, version=0)` (0:
   the version the linked file is, i.e. its `Version` property) and
   `unpinLink(prop)`; `getLinkPin(prop)` -> `(num, uuid, fallbackReason)`.
   The Gui: "Pin to version..." / "Unpin" on a link's context menu, a
   picker listing the linked file's named versions and the one it is; the
   fallback overlay.

### 27.3 Questions

| # | Question | Proposal |
| --- | --- | --- |
| Q1 | Where a closed linked file's versions are read from | **The file loaded, as today**, its `History` read by `VersionReader`: a pinned link still loads the physical file once. A headless reader straight from the zip (the `History` entry and the blobs, no document) is a later step, for an assembly of pinned parts that should not load the live parts at all. |
| Q2 | Must a pin resolve with the log off | **Yes** -- reading a file should not require logging it. That is what 27.2 item 3 buys. |
| Q3 | What a pin names by default, and when it reaches the file | **The version the linked file is on disk** (its `Version`), named in the live log, the linked document marked modified so closing it prompts a save. Pinning the unsaved live state means saving the linked file first ("Save and pin"). A pin to a version that is never saved as named breaks at the linked file's next save by someone else and falls back, which 16.5 calls best effort. |
| Q4 | A log-off or `session` save of the linked file empties `History` | **Keep it instead**, stale: the next embedded open finds the guard mismatch and adopts it as closed branches (16.6, 26.2 item 7), whose version numbers pins still name. Emptying it silently breaks every pin on the file. |
| Q5 | How read-only the version document is | **Gated, not locked**: transactions refused, Gui commands gated, save refused. A Python script writing a property directly is not stopped; a property-level lock would touch every setter. |
| Q6 | Live links inside a version document | **Resolved live**, as they were when the version was taken, and shown as live -- a version of an assembly with live links is then not reproducible. Onshape avoids this by making every reference in a version a pin; the equivalent here is "pin all" at pin time, a later convenience. |
| Q7 | Save As of a version document | **Allowed**: it writes a standalone file of that version, which is then an ordinary document, unpinned from anything. |

### 27.4 Build order (proposed)

1. **5.a** `VersionReader`, the decode moved out of `TransactionLog`; both
   suites unchanged, and a gtest that a version materialised by the reader
   over an embedded copy equals one materialised by the log.
2. **5.b** The attribute: save/restore round trip, `isSame`, copy, the
   `set` op and its undo; Python `pinLink`/`unpinLink`/`getLinkPin`.
3. **5.c** Resolution: `<path>@v<num>` identity, `openVersion`, the
   `ReadOnly` version document, the fallback with its reason; gtests that
   pin v1, edit and save the linked file, reopen, and see v1; two pins two
   documents; each fallback cause; with the log off.
4. **5.d** Pinning names the version; Q4's kept `History`.
5. **5.e** The Gui: context menu, picker, label, fallback overlay; a GUI
   check script.

### 27.5 Rulings (user, 2026-09-26)

These override 27.2-27.4 where they disagree.

1. **The log is always on**; that is the aim of this branch. What the user
   chooses is whether a save writes the history into the file. A document
   another file pins is saved with its history whatever that choice is.
2. **A version that fails to resolve opens the live document**, as an
   unpinned link does today, with a warning.
3. **One log per physical file**, where today it is one document per
   physical file. Several versions of a file, from its one log, can be open
   at once; one version only once. A version opened that is not the tip of
   a branch gets a branch made implicitly, so any change to it is kept.
4. **Read-only like a partial document.** A partially loaded document is
   already read-only (`Document::save` and `recompute` refuse it, Save All
   skips it, the tree offers reload). Versions use the same mechanism, but
   the Gui offers to save anyway, with a warning.

### 27.6 The questions again

| # | Was | Now |
| --- | --- | --- |
| Q1 | where a closed file's versions come from | **From the file's log, with no document.** Under ruling 3 the log belongs to the file, not to a document, so it is opened from the file's `History` entry and blobs straight from the archive. No live document is loaded to resolve a pin. |
| Q2 | a pin with the log off | **Gone** (ruling 1). The log-off mode stays only as a hidden switch for A/B checks. |
| Q3 | what a pin names, and when it reaches the file | Pinning **records a pin row in the linked file's log** (version, linking file, linking document uuid) and names the version. A save of a file whose log holds pin rows, or that a loaded link pins, **writes its history whatever the preference is** (ruling 1). The row only reaches disk when the linked file is saved, so pinning **opens the linked document fully** (reloading it if it was partial) **and marks it modified**. Default pin: the version the file is on disk. |
| Q4 | a save with the log off empties `History` | **Mostly gone.** The log is always on. A save without history is a choice the user makes, and it is refused for a file with pins. |
| Q5 | how read-only a version is | **Ruling 4**: the partial-document gate, plus a Gui save-with-warning. |
| Q6 | live links inside a version | Unchanged: **resolved live**, and shown as live. |
| Q7 | Save As of a version | **Allowed**, as for any document: the new file gets a copy of the log with this document's branch current, and the old file keeps its own log. |
| new | when the implicit branch is made | **At the first change**, so opening a version only to look leaves nothing behind. It is named `<branch>@v<num>`, with a suffix if the name is taken. |
| new | the default for writing history into the file | **Write it.** Pins and history are the point of this branch; "Save without history" stays as a per-save choice. |

### 27.7 Revised shape

**The per-file log.** `App::FileHistory` is a process registry entry keyed
by the canonical path. It owns what the file's documents share: the
`TransactionStore`, the log's worker, the sequence counter, the branch
table, and the **blob manager**. That is 16.2's "one blob manager per
history" made literal. Documents hold a reference to it, and it lives while
any document of the file is open, or while a pin still needs it.
`TransactionLog` stays per document as the cursor on one branch: that
document's pending values, its recorded state, its undo floor and stacks.
`FileBlobManager` and the transient directory move from the document to
the `FileHistory`. That makes this the largest step. It also removes
copying: a version document restored from its own file's log takes
handles on blobs the store already holds.

**Git's worktree rule.** A branch is checked out by at most one open
document. Every open document of a file is either:

- a **branch document**, on a branch it holds -- the live document is one;
- or a **version document**, at a version, holding no branch until it
  changes.

The rules follow from that:

- Opening a version that some document already *is* (a version document
  of it, or a branch document at that version with no change since)
  returns that document.
- A version document's first change makes it a branch document. If the
  version is the tip of a branch nobody holds, it takes that branch.
  Otherwise it gets the implicit branch `<branch>@v<num>`.
- An in-place switch (phase 4) to a branch another document holds is
  refused, and the refusal names that document.

**Identity.** Unchanged from 27.2: `<abs path>@v<num>` is the version
document's `FileName` and its `DocInfo` key. The directory is the file's,
so the version's own relative links resolve as the file's do. Once a
version document becomes a branch document it keeps that name.

**Read-only.** A new status, `VersionDoc`, rides on every `PartialDoc`
check that is about saving: App `save()` refuses it and Save All skips
it. The Gui's Save asks instead: "This is version 17 of Part.FCStd, and
other documents pin it. Saving writes it over Part.FCStd, as branch
X." A saved change leaves the pins naming v17, and the dialog offers to
re-pin them to the new version. Recompute is **not** refused: unlike a
partial document, a version document is complete, and its snapshot
already carries the shapes, so nothing needs a recompute until something
changes.

**Resolution.** A pinned `DocInfo` asks the `FileHistory` of the path,
opening it from the archive if no document of the file is open, for
version `num` with a matching uuid, then opens or returns the version
document. On any failure it falls back to the physical file, as an
unpinned link would, and warns once per link per open (ruling 2). The pin
stays in the property, so a later open retries.

**Opening a version from the panel.** "Open version N" joins "restore to
here" and "branch from here" on the panel, using the same path as a pin.

### 27.8 Build order (revised)

1. **5.a Always on.** `TransactionLog` defaults on. A preference sets
   whether a save writes history (default: yes), and 0 is kept as the
   hidden switch. The suites run with it on, which is already green
   (24.13).
2. **5.b `FileHistory`.** The store, worker, sequence, branches, blob
   manager and transient directory move from the document to the per-file
   registry, and `TransactionLog` becomes the per-document cursor. With one
   document per file nothing changes: both suites unchanged, and the
   recovery and branch checks pass.
3. **5.c Several documents per file.** Version documents, the worktree
   rule, the implicit branch at the first change, `VersionDoc` on the
   partial-document save gate, and "Open version N" in App, Python and the
   panel. Gtests: two versions open, each opened once; edit a non-tip
   version and it branches; edit a free tip and it continues that branch;
   a switch onto a held branch is refused; closing one document leaves the
   other's log working.
4. **5.d Opening a log from the archive**, with no document: the
   `History` entry and its blobs read from the zip into a `FileHistory`.
5. **5.e The pin.** The `version`/`vuuid` attributes as a `set` op; pin
   rows in the linked log; a save forced to write history when the file
   is pinned; resolution and the fallback warning; and Python `pinLink`,
   `unpinLink` and `getLinkPin`. Gtests: pin v1, edit and save the linked
   file, reopen, still see v1; two pins share one document; each fallback
   cause; the save-without-history refusal.
6. **5.f The Gui.** Pin/unpin and the version picker on a link, the
   version document's label and save-with-warning, re-pin on save, and a
   GUI check script.

### 27.9 5.a as built: the log on by default (2026-09-26)

`TransactionLog` defaults to **2**: the log is on and a save writes its
history into the file. 1 keeps the history out of files; the log is still
on. 0 switches the log off, kept for A/B checks. No preference page carries
it yet.

**Save and Save As were undo steps, with the log on.** Nothing had
noticed because every earlier log-on run used mode 1. Save As writes
`FileName` and `Label`, a save writes `TipName`, `LastModifiedDate` and
`LastModifiedBy`, and an embedded save adds and sets `History`, `Version`
and `Branch`. With the log on, a write outside a transaction opens an
implicit one (24.13), so every save left an `<implicit>` step on the undo
stack, and undoing it after a Save As cleared the document's file name.
These writes are now **bookkeeping**. A flag (`DocumentP::bookkeeping`) is
held around them, and while it is held:

- `Document::onBeforeChange` neither opens nor records a transaction;
- `_addOrRemoveProperty` records nothing;
- `onChanged` tells the log to forget what it held for the property
  (`TransactionLog::forgetValue`), so the next composed snapshot serialises
  it afresh instead of claiming a stale part (23.3).

The flag covers only the document's own properties at those sites. An
object that writes during a save still opens its transaction, as before.
Branch rename (26) writes `Branch` under the same flag. The gtest that
counted the implicit step (`embeddedHistoryRoundTrips`) now counts one row
fewer. `Document.TransactionBranchCases.testSaveWithHistoryIsNoUndoStep` is
the new case.

**Tests.** Cases that read what an archive carries -- `FileBlobs`,
`ShapeStorage`, `TestMaterialBlobs` -- now meet the embedded history's
entries: the log database, and the blobs of the versions it keeps.
They are about the document's own content, so they take
`ArchiveMembers.HistoryLeftOut()` in `setUp`, which sets mode 1 when the
mode is 2, and release it in `tearDown`. `DocumentObserverCases.testSave`
skips the signals for `History`, `Version` and `Branch`. The GUI tests now
run with the log too: `serve-undo-redo.py` clears the undo stack after its
set-up, which is an implicit step with the log on.

Gates: Python 2914 OK and ctest 834/834 in a fresh `FREECAD_USER_HOME`,
which is now a log-on run; Python 2914 OK with `TransactionLog=0`.

### 27.10 5.b as built: the file history (2026-09-26)

`App::FileHistory` (`src/App/FileHistory.h`) is what the documents of one
file share. In this step it holds:

- **the blob manager.** `Document::getFileBlobManager()` is now
  `getFileHistory().blobs()`. `FileBlobManager` takes the history where it
  took the document, and resolves its directory through it. The document
  was only ever used for that directory, and for the "is a document store"
  test of the pack store.
- **the directory**: its home document's transient directory. The history
  follows it when a restore renames it after the file's Uid, through
  `Document::onChanged(TransientDir)`. The history removes the directory
  when the last document holding it lets go. `~Document` removes its
  transient directory itself only when that directory is not the history's.
- **the registry**: the canonical path of the file maps to its history
  (`FileHistory::find`), kept in step with `FileName`. Registering a path
  another live history holds is refused with a warning (one file, one
  history).

The log object itself stays per document. Its shared half -- the store,
the worker, the counters, the entity functions -- moves in 5.c, where a
second document on one file first needs it, so the split lands with the
cases that test it. The log's paths (`history/`, the recovery's
`blobs/`) go through the history's directory now.

With one document per file nothing changes, and the gates say so: Python
2914 OK, ctest 835/835 (+1, `fileHistoryIsTheFilesAndLivesInItsDirectory`),
the recovery GUI check 15 PASS, and the branch GUI check 27 PASS.

### 27.11 5.c, first half: the log's core in the file history (2026-09-26)

**The split.** `TransactionLogCore`, defined in `TransactionLog.cpp` and
owned by the `FileHistory` (`logCore()`), is the half of the log the
documents of a file share:

- the store and its path;
- the environment and the session;
- the sequence and version counters;
- the held blobs (`_blobs`, `_sourced`) and `_hashById`;
- the delta policy;
- the worker thread with its queue, and the retired copies;
- `liveLogs`.

`TransactionLog` is now one document's **cursor**: its branch and head,
`_pending`, `_recorded`, `_misses`, the snapshot sink, and the capture
configuration. It reaches the shared state through `_c`. The first
document's log makes the core (`coreOf`), and the core opens the store in
the history's directory. `~TransactionLog` flushes the jobs that name it
and no longer stops the worker; `~FileHistory` destroys the core before it
shuts the blob store down. File-level operations -- `adoptStore`,
`adoptClosed`, `recover`, `closeStore`/`reopenStore` -- still go through a
cursor, and act on the core.

**Found on the way: a switch or a restore renamed the document.**
`_applyVersion` copies every document property but those
`keptOnRestore` lists, and `Label` was not on the list. The version is
read into a scratch document called `VersionRestore`, and that name is
what a branch switch or a restore to a version gave the live document.
It predates this phase (26, 24.8). Nothing had noticed, because the
branch check reads objects rather than the label. `Label` is kept now;
`TransactionBranchCases.testSwitchAndRestoreKeepTheLabel` checks it. It
showed up as the recovery check failing: the branch check exits with
`os._exit`, and its leftover directory, whose log `meta` said
`VersionRestore`, was listed by the next run's recovery dialog.

**The GUI checks share `~/.cache/FreeCAD/Cache`**, where every crashed or
`os._exit`-ed session leaves its directory, and the recovery dialog lists
every log there. Run them with their own `XDG_CACHE_HOME`, one per check,
or a leftover from an earlier run fails "it lists the crashed document".

Gates: Python 2915 OK, ctest 835/835, recovery check 15 PASS, branch
check 27 PASS.

### 27.12 5.c, second half: several documents per file (2026-09-26)

**Opening a version.** `Document::openVersion(num)` opens a version of the
file (Python `openTransactionVersion(num, createView=True)`, and "Open
version N" on the log panel's version menu) as a document of its own:

- `VersionDoc` status;
- `FileName` `<file>@v<num>`, label `<label> (v<num>)`;
- on the file's `FileHistory`: the same store, worker and blob manager;
- restored from the version materialised under the history's
  `history/open-v<num>`, with `checkingOut` set, so the restore neither
  starts nor adopts a history, and the directory removed after.

Its log is a cursor **detached** at the version (`TransactionLog(doc,
&version)`: branch 0, head = the version's seq). The undo mode is the
source document's; the undo floor is the log's end when it opened. The
file stays registered as itself: a `VersionDoc` never re-registers the
history under its own `FileName`. `getTransactionCursor()` reports branch,
head, `detached`, `version`, and the file's documents.

**One version, one document.** `TransactionLog::documentAt(version)`
returns the document that *is* the version: a cursor whose head is the
version's row, or whose branch moved past it only by records -- save,
snapshot, switch (`unchangedSince`). `openVersion` returns that document
instead of opening another. So a live document that has not changed since
it saved is what opening its saved version gives. `theFilesLogOutlivesItsFirstDocument`
states this, then edits to move the document off the version.

**The first change** is the first row the cursor numbers (`number()` ->
`ensureBranch()`):

- If the branch the version was taken on is open, no document holds it,
  and nothing but records follows the version on it, the version document
  continues that branch, from its head.
- Otherwise a branch of its own is made, named `<branch>@v<num>` (with
  `#2`... when taken), forked at the version, which is named if it was
  not. Its `branch` record precedes the row that made it.

The ids the new branch hands out start at a base `openVersion` reserved:
a stride above every branch's `idBase`/`lastId` and every open document's
last id, because the first change may create an object before the branch
exists.

A recompute of a detached document writes no record and makes no branch.
An implicit recompute *transaction*, which writes derived values, is a
change like any other.

**Git's worktree rule.** The core records which cursor holds each branch
(`_holders`). A document's log holds the branch it opens on; `setBranch`,
and so a switch or a create, refuses a branch another document holds, and
the refusal names that document; closing a document lets its branch go.
A document that opens on a branch already held -- the file reopened while
a version document continues its branch -- warns and stays detached at the
head, branching at its first change. Only a branch document writes the
store's `meta` branch, which is the branch the file reopens on.

**Read-only like a partial document** (27.5 ruling 4):

- App `save()` refuses a `VersionDoc`, and Python raises "the document is
  a version of a file".
- In the Gui, `VersionDoc` joins `PartialDoc` in Save All, in saving
  dependents, in the schema-upgrade offer, and in the close prompts: a
  version document is never asked to be saved.
- Its changes are rows on its branch in the file's log, which travel with
  the file when a branch document of it saves with history.
- Save-with-warning is 5.f.

**Joining a history** (`_joinHistory`). A document can have a log and a
history of its own before its maker is back: the Gui's log panel asks the
new, active document for its log. The GUI version check found this as a
SIGSEGV. `openVersion` then replaced the history, and freed the core the
log's cursor still referred to. Joining drops the document's own log and
history first, and recreates its transient directory, which went with that
history. `_readVersion`'s scratch document drops a log it was given the
same way.

The GUI check found one more thing: `openVersion`'s own writes of the
version document's `FileName` and `Label` opened an implicit transaction,
which the Gui committed at the next event-loop turn. So in the Gui, merely
opening a version made a branch. They are bookkeeping (27.9) now.

**A file reopened** while another document of it is open joins the
registered history (`getFileHistory()` looks the path up). Its restore
then neither snapshots nor adopts (`joinedHistory`).

Tests:

- gtests: `versionDocumentsShareTheFilesLog`,
  `aVersionBranchesAtItsFirstChange`,
  `aFreeTipIsContinuedAndAHeldBranchIsNotSwitchedTo`,
  `theFilesLogOutlivesItsFirstDocument`;
- Python: `TransactionBranchCases.testOpenVersionIsADocumentOfItsOwn`;
- GUI: `scripts/transaction-log-version-check.py`.

Gates:

- Python 2916 OK;
- ctest 839/839;
- recovery check 15 PASS;
- branch check 27 PASS;
- version check 17 PASS.

Each GUI check runs with its own `XDG_CACHE_HOME`.

### 27.13 5.d as built: a file's log from its archive (2026-09-26)

**The core, all of it.** 27.11 moved the log's shared data into
`TransactionLogCore`; this step moves the functions over that data too,
and publishes the class in `TransactionLog.h`:

- the entity functions -- `putValue`, `putBlob`, `putSources`, `putBytes`,
  `putComposite`, `composeEntry`, `supersede`, `chainBelow`, `readBytes`,
  `readValue`, `restoreBlob`, `readRevert`, `evictVersions`;
- the flushing store, `embed`, and the held blobs;
- the queries over the file's documents;
- adopting an embedded copy (`adoptEmbedded`, `closeAdopted`);
- the worker's half of a snapshot (`postVersion`).

The document's `TransactionLog` forwards its public reads.
`Document::_materialiseVersion` became a static
`materialiseVersion(core, num, dir)`, and `Document::openVersion` a
static `openFileVersion(history, num, createView, from)`: neither needs a
document of the file any more.

**`FileHistory::openFile(path)`** gives the registered history if a
document of the file has one. Otherwise it reads the history out of the
archive:

1. The document's own properties are read out of `Document.xml`, before
   `<Objects`: the root's `SchemaVersion`, `Label`, `Version`,
   `LastModifiedDate`, and the `History` element's `db` hash. The root is
   `FCDocument` in this fork and `Document` upstream. A file whose
   `History` is empty has none to open.
2. A history of its own, in a directory named as a document's transient
   directory is (`<exe>_Doc_<uuid>_<hash>_<pid>`), so a crash with only
   this history open is found by recovery. It is registered under the
   path, and has no home.
3. The archive's blobs are split into its store (`splitArchive`, the
   pack store's split-on-open, 15.10); the database is found by hash.
4. The guard of 16.4 on the save id and date: when they match, the copy is
   adopted; when not, it is adopted with its branches closed and a new
   `main`, as 16.6 does (`closeAdopted`).
5. **The file as found is recorded as a version** (`recordFile`), on the
   branch the copy names -- what `onRestore` does for a document opened
   from the file.

Step 5 has to happen because the embedded copy cannot hold the file's own
version: the copy is part of the file, so a version of the file inside it
would have to name its own hash. A save embeds the log as it was before
the save's version, stamps the counter the save's version takes, and every
open re-derives that version from the file's bytes. `openFile` does the
same from the archive's entries -- `Document.xml`, and every other entry
that is neither a blob nor a thumbnail -- and the blobs under their archive
names. `fileVersion()` is its number, which is the number in the file's
`Version` property when the guard matched.

**The file opened later** joins the registered history (27.12), so its
restore neither adopts nor records, and its cursor is on the branch the
copy names, at the recorded version.

**Python.** `FreeCAD.openFileVersion(path, num, createView=True)` opens
version `num` of any file, open or not. The path is checked as a host read
(`checkHostPath`), as opening the file is.

**Open ends:**

- A version document opened from the archive at the file's own version,
  and the file itself opened afterwards, are two documents at one version.
  The live document is a branch document, the other is detached. 27.5's
  "one version once" holds for version documents, not between a version
  document and the branch document of the same state.
- The closed-branches path of step 4 is not exercised by a test. It is
  `adoptClosed`'s own logic, moved.

Tests: gtest `aClosedFilesHistoryIsReadFromTheArchive`; Python
`TransactionBranchCases.testOpenAClosedFilesVersion`.

Gates:

- Python 2917 OK;
- ctest 840/840;
- recovery check 15 PASS;
- branch check 27 PASS;
- version check 17 PASS.

### 27.14 5.e as built: the pin, App half (2026-09-26)

**The attribute.** `PropertyXLink` carries `_pinVersion` and `_pinUuid`.
They are saved as `version="N" vuuid="..."` on `<XLink>`, and only when
the link is pinned, so a FreeCAD that does not know them resolves the file.
They are read back by `Restore`, and carried by `copyTo`/`Paste` (set
before the value resolves) and compared by `isSame`. Changing a pin is
therefore an ordinary undoable write, and the log records it as a `set`.

**Resolution** (`DocInfo::get`):

- A pinned link's `DocInfo` is keyed `<absolute file>@v<num>`; its
  `myPath` stays the file's, so the relative path a link saves is the
  file's.
- Before keying, the version is checked: `FileHistory::findVersion`
  opens the file's history (the registered one, or read from the archive,
  27.13), then looks for the row and its uuid. The history is kept by the
  `DocInfo` until the version's document has it.
- When the check fails, the link warns once per file and version and
  resolves to the file as an unpinned link does (27.5 ruling 2).
  `pinFellBack()` says so, and the pin stays for the next open.
- A link set to an object of a version document -- whose `FileName` is
  `<file>@v<num>` -- is pinned to that version (`FileHistory::splitVersion`).
  That is what makes Paste and undo keep a pin with no more code.

**Opening `<file>@v<num>`.** `Application::openDocumentPrivate` treats
such a name as "version `num` of the file" and opens it with
`Document::openFileVersion`. So the pending-document machinery of a
restore needs nothing new. `openDocuments` does not run `afterRestore`
again on a version document, which its opening restored whole.

**A pin always gets a version document.** The first run of the pin test
found the live document of the linked file handed to the pin: it had not
changed since its save, so it *was* the version, and "one version, one
document" (27.12) returned it -- and the pin would then have followed its
next edit. `documentAt(version, versionDocsOnly)`: the pin's open looks
only at version documents. This refines 27.12. A branch document that is
at a version is not "that version opened", and a pin, or anything else
that must not move, never takes one.

**Pinning** (`Document::pinLink(link, version)`; Python
`obj.pinLink(prop, version=0)`, `obj.unpinLink(prop)`,
`obj.getLinkPin(prop)` -> `(version, uuid, fellBack)` or None):

- version 0 pins what the linked document is: a version document's own
  version, or else the file on disk (its `Version`, 27.6 Q3);
- the version is named `pinned` if it was not, so the linked file's
  eviction keeps it;
- the linked store's `meta` `pins` gains a line `<num> TAB <linking file>
  TAB <linking document uid>`;
- every branch document of the linked file is marked modified with a
  `Comment.touch()` -- the convention `DocInfo` uses for a stamp change --
  so the pin reaches the file with its next save.

**The forced save** (27.5 ruling 1). `embedHistory` writes the history
when the preference says so, or when `Document::isPinned()`: the log's
`meta` has pins, or a loaded link is pinned to a version of the file
(`PropertyXLink::getPinsTo`). A copy saved without history
(`saveCopy(..., False)`) is still one.

Tests: Python `TransactionBranchCases.testPinnedLinkKeepsItsVersion` pins
a link and then:

- edits and saves the linked file with the preference off, which keeps
  its history;
- reopens the assembly with the part closed, and still sees the version;
- unpins, which shows the live file.

`testPinThatCannotBeFoundShowsTheFile` replaces the part with a copy
saved without history, and checks that the pin falls back to the file and
stays set.

**Open: undoing an unpin does not bring the pin back.** After
`unpinLink` and `undo()`, `getLinkPin` is None. The before-copy carries
the pin (`copyTo`), and `Paste` sets it before resolving, so the suspect is
the path undo takes with the log on. Either it restores through the
captured XML -- a detached copy of an `XLink` saves `file=""`, having no
`_pcLink` and a cleared `filePath` -- or it takes another route that loses
the fields. Not chased; the case was taken out of the test, and it is the
first item for the next session.

Gates:

- Python 2919 OK;
- ctest 840/840;
- recovery check 15 PASS;
- branch check 27 PASS;
- version check 17 PASS.

Not done yet (5.f): the Gui -- pin/unpin and a version picker on a link,
the label of a version document in the tree, save-with-warning for a
version document, and re-pinning after one is saved -- and a GUI check.

### 27.15 The open item of 27.14: a document opened mid-transaction (2026-09-26)

**Undoing an unpin works; the test was wrong.** The reopened assembly
had `UndoMode` 0, the default for a document opened in FreeCADCmd, so
`undo()` had no step to undo. With `UndoMode` 1 the pin comes back on
undo and goes again on redo, and the test now checks both.

**The chase found a real defect.** Pinning left two undo steps, `pin`
and an `<implicit>` one after it. Unpinning a link whose file was
closed did the same. In both cases the link's write opens a document:
the version document for a pin, the file itself for an unpin. The
new document's first writes joined the transaction the assembly had
open, the application's active one:

- `Label`, set by `Application::newDocument`;
- `FileName`, set by `openDocumentPrivate` before the restore.

Its restore then calls `clearUndos()`, which commits its transaction
with notification, and `closeActiveTransaction` commits every document
holding that id, the assembly with it. The rest of the link's write
found no transaction and opened an implicit one.

Neither write joined anything with the log off, because a new document
in FreeCADCmd has undo off. With the log on, `transactionsWanted()` is
true whatever the undo mode, so this happens to every document made or
opened while another has a transaction open.

**The fix** is at both sites, as for 27.9's saves: who a document is
is not a change to it.

- `transactionsWanted()` is false while the document is `Initializing`,
  which covers `newDocument`'s `Label`.
- `openDocumentPrivate` writes `FileName` as bookkeeping, as
  `openFileVersion` already did.

Test: `testOpeningADocumentKeepsAnotherOnesTransaction` makes one
document and opens another inside a transaction, then checks that it is
one step and that it undoes whole. It and the pin test both fail
without the fix.

Gates:

- Python 2920 OK;
- ctest 840/840;
- recovery check 15 PASS;
- branch check 27 PASS;
- version check 17 PASS.

### 27.16 5.f plan: the Gui (2026-09-26)

27.7 and 27.8 set what 5.f does. These are the details they leave open,
with the default taken for each.

**Pin and unpin are link commands.** `Std_LinkPin` ("Pin to version...")
and `Std_LinkUnpin` join the link menu and `Std_LinkActions`.

- They apply to a selected object whose link property is a
  `PropertyXLink` to another saved file: an `App::Link`'s
  `LinkedObject`, or any link extension's linked-object property.
- The picker lists the linked file's versions from its log: number,
  name, branch and date. It starts on the current pin, or else on the
  version the file is on disk.
- Several selected links to the same file are pinned together, in one
  transaction. Links to other files in the selection are left alone.

**Pinning opens the file's own document** when none is open (27.6 Q3).
As built, `pinLink` only marks an open branch document modified. A pin
made while the link shows a version document, with the file itself
closed, would put its row in a store that no save writes. `pinLink` now
opens the file, in App, so Python gets it too.

**Save with warning.** The Gui's Save of a version document asks first:

> This is version N of F [, which other documents pin]. Saving writes
> it over F, as branch B.

A new App call, `Document::saveVersionAsFile()`, does the write:

- the cursor takes a branch (`ensureBranch`) if it has none, so a
  version saved unchanged continues a free tip or makes `<branch>@v<num>`;
- the file is written from this document, with history under the usual
  rules;
- the store's `meta` branch becomes this branch, so the file reopens on
  it;
- the document keeps its name `<file>@v<num>` (27.7, Identity);
- the call returns the version the save became.

Save All still skips version documents.

**Re-pin.** After such a save, the Gui lists the loaded links pinned to
version N of F and offers to re-pin them to the new version, one
transaction per owning document. Pins in files that are not loaded keep
naming N.

**The question this leaves (default taken, not ruled).** F's own
document may be open while a version of it is saved over F. The default
is that the save goes ahead, and the warning names that document. F on
disk is then this version's branch, and the open document keeps its own
branch and changes; whichever saves last writes F. That is git's
worktree rule again, applied to the one file both write.

**GUI check** `scripts/transaction-log-pin-check.py`. Message boxes and
the picker are answered by the script. It covers:

- pin through the command, and see the version document;
- unpin, undo and redo;
- save a version document through the Gui with the warning, and re-pin;
- Save All skips the version document.

### 27.17 5.f as built: the Gui (2026-09-26)

27.16 is built as planned. **Phase 5 is complete.**

**App.**

- `Document::saveVersionAsFile()` (Python `doc.saveVersionAsFile()`)
  takes the branch first (`TransactionLog::takeBranch()`), stamps as
  `save()` does, writes the file and returns the version the save became.
- The embedded copy now carries the **saving document's branch** in its
  `meta` (`TransactionLogCore::embed(date, branch)`). Before this, the
  file reopened on whichever branch the store last named -- the one the
  last branch document switched to. A version document never sets that,
  so its file would have reopened on the wrong branch.
  `testSaveAVersionAsTheFile` shows both cases.
- `pinLink` opens the file's own document when only version documents of
  it are open, and keeps the active document.

**Gui.**

- `Std_LinkPin` and its picker (`QDialog` "Std_LinkPin", a tree of the
  file's versions, newest first), and `Std_LinkUnpin`. Both are in the
  link menu.
- `Gui::Document::save` of a version document shows the warning and
  calls `saveVersionAsFile`, then offers the re-pin as a "Re-pin links"
  command.

**Found by the GUI check, fixed.**

1. **A load a command starts was guarded against that command.** The Gui
   marks every loading document `LiveImport`
   (`Application::refreshLiveLoad`), and `UserEditGuard` refuses a
   running command's writes to such a document. `Std_LinkPin` opened the
   version document inside the command, so its restore aborted at the
   first object, and the link showed an empty document. The pin from
   Python, outside a command, worked.

   `refreshLiveLoad` now remembers a load that started while a command
   runs (`isUserEditing()`), and does not claim it while the command
   lasts. It is the command's own doing, like the import into an open
   document that was already exempt. A link's target file, opened by a
   command, went the same way before pins.
2. **A re-pin was undone by the document's name.** `PropertyXLink::setValue(file, name)`
   found the object through the `DocInfo` for `<file>@v3`, then passed it
   to `setValue(object)`. That call derived a `DocInfo` again from the
   document's `FileName`, and the version document saved as its file is
   still named `<file>@v1` (27.7), so the link was pinned back to v1. The
   `DocInfo` found is now handed over.
3. **A cancelled command left an empty undo step.** While the picker's
   modal loop ran, the Gui's action update asked `Part_CrossSections`
   whether it was active. That built the selected link's shape, and
   `Part::PropertyShapeCache` added its cache to the link as a dynamic
   `Prop_NoPersist` property. `_addOrRemoveProperty` opened the running
   command's transaction for it, and the command committed that as a
   step. A property never saved no longer *opens* a transaction; if one
   is open, it is still recorded, as before. `Prop_Transient` is not
   exempt, because its name and type are saved. Any modal command with a
   link selected did this. Test: `testAnUnsavedPropertyOpensNoTransaction`.

**Two things seen, not chased:**

- `FREECAD_USER_HOME` does not keep the GUI off `~/.config/FreeCAD`.
  The GUI checks read the real `user.cfg`, which here has
  `TransactionLog=1`, so a GUI save left the history out. The pin
  check sets the preference in-process. It exits with `os._exit`, so the
  setting is never written back.
- `ViewProviderDocumentObjectPy::getObject` dereferences a null object:
  a Python Gui observer that reads `vp.Object` during a view provider's
  construction crashes the process.

GUI check: `scripts/transaction-log-pin-check.py`, run with its own
`XDG_CACHE_HOME`.

Gates:

- Python 2922 OK, with the log on and with it off (`TransactionLog=0`);
- ctest 840/840;
- recovery check 15 PASS;
- branch check 27 PASS;
- version check 17 PASS;
- pin check 25 PASS.

### 27.18 Rulings and what comes next (user, 2026-09-26)

**Ruling.** The default in 27.16 stands. A version saved over its file
while the file's own document is open goes ahead, the warning names that
document, and whichever is saved last is what the file holds.

**Next, in order:**

1. **The crash in 27.17.** `ViewProviderDocumentObjectPy::getObject`
   dereferences a null object when a Python Gui observer reads
   `vp.Object` during a view provider's construction.
2. **A local link pinned to another version.** The link is to an object
   of its own document, but pinned to a different version of that
   document's file. The version should be loaded as a version document,
   as for any pin, and the link should behave as an external link to it.
   Today `pinLink` refuses ("a pin needs a link to another file"), and
   `PropertyXLink` treats a same-document target as local. Design it
   first: what the link saves, how it resolves on open, what undo does,
   and what the version document's changes do to the file's own document.

**Afterwards: state that belongs to the file, not to one version.**
Several counters and tables are per document, so each version document
and branch has its own copy. When two branches meet in a merge (phase
6), those copies collide. Making them **file scope** -- one per
`FileHistory`, shared by every document of the file -- removes the
collisions at the source.

1. **The last object id** (`DocumentP::lastObjectId`). Two branches both
   hand out the next id. Today they are kept apart by a stride per
   branch (`idBase`, `branchStride()`, sec 17.2). A file-scope counter
   makes the stride unnecessary.
2. **The string hasher used for element names** (`DocumentP::Hasher`).
   Each version hashes the same strings to different ids, so the element
   maps of the same shape in two branches disagree, and merging them means
   re-hashing.
3. **An object name table** (`getUniqueObjectName`). Two branches that
   each add a `Pad` both get `Pad001`. A merge must then rename one of
   them, along with every expression, link and subname that refers to it.
   The fix is a file-scope table of name -> object id, which every
   document of the file allocates from:
   - a name is given to one object only, in any version or branch;
   - the same object keeps its name in every version;
   - a deleted object's name stays taken, so an undo or an old version
     can bring the object back under its name, and a merge never finds
     two objects with one name.

   With item 1, the pair (id, name) identifies an object across the whole
   file. Labels, where duplicates are not allowed, are the same case in a
   weaker form.
4. **Id counters kept inside objects**, for example Sketcher's
   `geoLastId`. Two branches editing the same sketch mint the same
   geometry ids, and constraints refer to geometry by id. This is object
   state, not document state, so the fix is different: the counter would
   have to be kept per object, but across the file.
5. Follows from 1 with nothing more: the `Tag` in element maps, which is
   the object id.

Already file scope: the log's sequence and version counters, the branch
table, the blob manager and the transient directory (27.10, 27.11).

### 27.19 The crash of 27.17: a view provider announced before its object (2026-09-26)

Item 1 of 27.18. The constructor of `ViewProviderDocumentObject` sets
`Selectable` from the preferences. The property change reached
`ViewProvider::onBeforeChange`/`onChanged`, which announced it through
`Application::signalBeforeChangeObject`/`signalChangedObject` -- before
`Gui::Document::slotNewObject` had attached the view provider to its
object. A Python Gui observer was handed a view provider with no object,
and `vp.Object` dereferenced null. Any Gui observer with a change slot
crashed on the first object made.

It did a second thing, silently: the observer's `getPyObject()` ran while
the base class was being constructed, so it made a
`ViewProviderDocumentObjectPy`, and the view provider kept that wrapper.
Every derived class makes its own type only `if (!pyViewObject)`, so an
`App::Link`'s `ViewObject` stayed the base type, without the methods of
`ViewProviderLink`, for as long as it lived.

The fix is at the signal: `ViewProvider::announcesChanges()`, true by
default, and false for a `ViewProviderDocumentObject` until it has its
object. A change before then is not announced; the observers learn of the
view provider from `signalNewObject` after the attach, as before. The
getter also returns None for a view provider with no object rather than
crash, since nothing else stops Python from asking.

Tests: `ViewProviderHooks.ViewProviderObserverTest` (GUI, through
`scripts/sandbox-gui-gate.py`): every change an observer sees while an
`App::Link` is made carries the object, and the view provider is a
`ViewProviderLink` to the observer and afterwards. Before the fix the
first case killed the process.

### 27.20 A local link pinned to another version: proposed design (2026-09-26)

Item 2 of 27.18. A link in a document names an object of the same
document, pinned to a version of the document's own file. Nothing of
this is built; the questions at the end need rulings first.

**What it is for.** Seeing, or reusing, an earlier state of the same
model beside the current one: an old variant of a part placed next to
the new, a reference copy to measure a change against, a frozen
sub-assembly while the rest moves on. Today the only way is to save a
copy of the file and link to that.

**What exists (survey).**

- `pinLink` refuses a link whose target is in the owner's document
  ("a pin needs a link to another file"), and so does `setPin`.
- `PropertyXLink` treats a same-document target as local: no `DocInfo`,
  `file=""` in the save, and `Restore` resolves `file=""` by name in the
  owner's document.
- `DocInfo::get` keys a pinned link by `<absolute file>@v<num>`
  (27.14). A key with the owner's own file and a version is therefore
  already a different key from the owner's document, and would open a
  version document as for any pin. The one guard in the way is "make
  sure to attach only external object": it returns a `DocInfo` whose
  document is the owner's without registering the link -- which is what
  a *failed* self-pin should do (it falls back to the live object, 27.5
  ruling 2).
- A version document's `FileName` is `<file>@v<num>`; a link set to one
  of its objects is pinned by `DocInfo::get` through `splitVersion`
  (27.14). So setting a link to an object of a version document of the
  owner's own file already takes the external path in `setValue` -- the
  two documents differ -- and is pinned. What is missing is making that
  deliberate, and the save form.

**Proposed shape.**

1. **Save form.** `<XLink file="" version="N" vuuid="..." name="Box"/>`:
   an empty `file` with a pin means *this document's own file*. Not the
   file's name: a Save As would then leave the link on the old file's
   version, while the history travels with the new file (the embedded
   log is the document's). A FreeCAD that does not know `version` reads
   `file=""` as a local link and shows the live object, which is the
   fallback of 27.5 ruling 2 anyway.
2. **Restore.** `file=""` with a pin resolves through
   `DocInfo::get(<owner's file>, ...)`, where the owner's file is its
   `FileName` with any `@v<num>` split off -- so a link inside a version
   document resolves against the same file. The version document is
   opened after the owner's restore, by the pending-document machinery
   (the history is registered by then, 27.12). A failed pin warns once
   and resolves to the object in the owner's document.
3. **Pinning.** `pinLink(link, N)` on a local link: allowed when the
   document is saved with history. `N = 0` means the version the file is
   on disk (27.6 Q3), as for another file. The version is named
   `pinned`, the `pins` meta line names the file itself, and the owner
   is the one document marked modified. The link's value moves to the
   same-named object of the version document; if the version has no
   object of that name the pin is refused.
4. **Unpinning** goes back to the object of that name in the owner's
   document, or clears the link if there is none (it was deleted after
   the version).
5. **Undo.** Pin and unpin are writes of the property, as today; with
   `file=""` meaning "own file", a before-copy restored from its XML
   comes back pinned to the same version, which also closes the
   restore-from-XML doubt of 27.14 for this case.
6. **Cycles** cannot recurse: each version opens once (27.12), and a
   link inside version `N` pinned to `N` finds its own document (the
   guard above) and is local there.
7. **The version document is read-only** (27.5 ruling 4), as for any
   pin. It shows in the tree as a document of its own, `<label> (vN)`.

**What the version document's changes do to the file's own document.**
A version document takes no edits; the only way its state reaches the
file is the Gui's "save the version as its file" (27.16, 27.18: last
save wins). For a self-pin that save would overwrite the very document
that holds the link, which is always open, and whose next save
overwrites it back. Q3 below.

**Questions.**

- **Q1. Scope of a local pin.** Only `PropertyXLink` (App::Link's
  `LinkedObject`, and every property that is an `XLink`), as for
  external pins; `PropertyLink`/`PropertyLinkSub` (Part features'
  `Base`, PartDesign's `Profile`, expressions) stay unpinnable. Proposed:
  yes, XLink only.
- **Q2. The save form**: `file=""` + `version` (proposed, Save-As
  proof), or the file's own relative name (what an external pin writes)?
- **Q3. Saving a self-pinned version as the file.** (a) refuse, and
  offer "restore this version into the document" instead -- the forward
  `restore` transaction of 24.d, undoable, then an ordinary save; (b) the
  27.18 rule unchanged; (c) warn and go ahead. Proposed: (a) whenever
  the file's own document is open, which would change 27.18's ruling for
  external pins too -- or (a) for self-pins only, if 27.18 should stand.
- **Q4. Pinning to the current version.** A pin to the version the
  document was last saved as is a snapshot of the saved state: later
  edits do not reach the linked copy. Allowed (proposed), or refused as
  surprising?
- **Q5. Closing.** When the last link pinned to a version document is
  unpinned or deleted, close the version document (proposed, for local
  pins and external ones alike -- it has no other reason to be open
  unless the user opened it), or leave it open?

### 27.21 Rulings on 27.20, and Q3 set out (user, 2026-09-26)

**Ruled.**

- **Q1: XLink only.** Only a `PropertyXLink` can be pinned to a version
  of its own file, as for another file.
- **Q2: `file=""` + `version`** is the save form of a self-pin.
- **Q4: allowed.** A pin to the version the file was last saved as is a
  frozen copy of the saved state.
- **Q5: ask.** When the last link pinned to a version document goes --
  unpinned, deleted, or re-pinned elsewhere -- the Gui asks whether to
  close the version document, with "remember my choice" kept as a
  preference (Ask / Close / Keep open, default Ask). It applies to
  self-pins and pins to other files alike. With no Gui to ask (App,
  Python, headless), the preference decides, and Ask means keep open. A
  version document the user opened by hand, not through a pin, is never
  offered for closing.

**Q3 set out: saving a self-pinned version as the file.** Asked for
detail before a ruling.

The case. `Foo.FCStd` is open as its document D, on branch `main`, at
v7 with some unsaved edits. A link L in D is pinned to v3, so the
version document V3 (`Foo.FCStd@v3`) is open. V3 may have been edited,
which gave it the implicit branch `main@v3` (27.7).

What 27.16/27.18 do today when the user saves V3 (Save, answered yes):

1. `saveVersionAsFile` writes V3 over `Foo.FCStd` as version v8 on
   `main@v3`, and sets the file's `meta` branch to `main@v3`: the file
   now reopens as v3 plus V3's edits.
2. The save's stamp change touches every document linking to V3 --
   D, through L -- so D is marked modified.
3. D is unchanged in memory: still `main`, v7 plus its edits.
4. D's next save -- which its modified mark all but guarantees, at the
   latest when it is closed -- writes D over the file as v9 on `main`,
   and the file reopens on `main` again. V3's save survives only as a
   row in the history.

For a pin to another file this is a corner: it needs that file's own
document to be open too. For a self-pin it is **every time**, because the
document holding the pin is the file's own document, and it is always
open. And the save's own side effect (step 2) arranges for it to be
overwritten. The user's evident intent -- "this old version is what I
want the file to be" -- lasts until the next Ctrl+S.

Three ways to go:

- **(a) Restore into D instead.** The save is refused for a self-pin,
  and the Gui offers "Restore this version into Foo". D runs the forward
  `restore` transaction of 24.d with V3's state as the source -- V3's
  version, or V3's current state if it has been edited. It is ONE undo
  step in D, on `main`; then the user saves D as usual. The history stays
  linear: v8 on `main` is v3's content. If L was added after v3 it goes
  with the restore, and so does the pin (Q5's prompt follows). Needs:
  `restoreVersion` taking a document's current state as its source, not
  only a version number (the scratch document of 24.d is replaced by
  V3). Cost: small. What is lost: nothing -- `main@v3`'s edits stay rows
  in the history.
- **(b) 27.18 unchanged.** Save goes ahead, warning names D, last save
  wins. No new code. The warning would appear on every save of a
  self-pinned version, and the result is undone by D's next save unless
  the user closes D without saving.
- **(c) D follows the save.** After V3 is written over the file, D
  switches in place to `main@v3` (26: a switch keeps each branch's
  changes and stacks), V3 hands the branch over and goes back to being a
  detached, read-only v3. D and the file then agree, and `main` keeps
  D's unsaved edits as rows. More git-like -- the file moved to a
  branch -- but a switch is not an undo step (26.4), so the user cannot
  Ctrl+Z out of it, and the worktree hand-over is new code.

**Proposed: (a) for self-pins.** It says what the user means, it is
undoable, and it keeps one writer to the file. Whether pins to other
files should get the same offer when the file's own document is open is
a separate choice: (a) there too, or 27.18's rule stays for them. Either
way the offer could carry the same Ask / always-restore / always-save
preference as Q5, so a user who wants 27.18's behaviour can have it.

**Q3 restated (same day).** The user asked whether saving V3 changes
anything in D, since a version is immutable. It does not: v3 is a row
that never changes, L goes on showing v3, and D in memory is untouched.
The only thing the save writes is the *file*, `Foo.FCStd`, which is also
D's file. So Q3 is only "which of the two decides what the file holds on
disk" -- 27.18's question, which for a self-pin comes up every time
because D is always open. Saving an unedited V3 over the file means "make
the file v3 again", which is what a restore into D does, undoably.

**Found while checking: a pinned version document takes edits, and the
pin shows them.** Probe (FreeCADCmd, a pin to another file): pin a link
to v1, then set `Integer = 99` on the object in the version document. The
edit is accepted (27.7: the first change makes it a branch document on
`<branch>@v1`), recompute runs, and the link -- still pinned to v1 --
shows 99. The version is immutable; the version *document* is not, and
the pin follows the document. 27.14 says a pin never takes a branch
document, but a version document becomes one under the pin. This holds
for every pin, not only self-pins. Two ways to close it:

- **(i)** a version document that a pin shows refuses edits;
  editing that version means opening it again, which gives a document
  of its own;
- **(ii)** ruling 3 stands (the version document may be edited, on its
  implicit branch), but at its first change it stops being what the
  pins show: the pinned links re-resolve to a fresh, unedited document
  of the same version. 27.12's "one version, opened once" still holds,
  since the edited document is a branch document from then on.

Proposed: (ii). It keeps ruling 3 and makes a pin mean the version
itself.

### 27.22 Ruling: pinned versions are frozen; save to the log only (user, 2026-09-26)

**Ruled.**

1. **(i): a pinned version is frozen.** A version can be open at most
   twice in one FreeCAD: once as the **frozen** instance that pins show,
   which refuses every edit, and once as an **editable** instance, which
   the user opens to work on (ruling 3: it branches at its first
   change). A flag tells the two apart.
2. **Save to the log only.** An editable document is offered a save that
   records a version in the file's history without changing the
   outward-facing document -- what the file opens as.

**Proposed shape (for the user's OK).**

*The flag and the names.*

- A new `Document` status `FrozenVersion`, set only on the instance a
  pin opens. `VersionDoc` stays on both instances (the save gates of
  27.7 apply to both).
- Pins resolve to the frozen instance only: `documentAt(version,
  frozenOnly)`, and `DocInfo::init` matches a pinned key only against a
  document with the flag. The user's `openVersion` (log panel, Python)
  returns the editable instance only.
- Both instances keep `FileName` `<file>@v<num>` (27.7, Identity), so
  relative links inside either resolve as the file's do. The label
  tells them apart in the tree: `<label> (v<num>, pinned)` for the frozen
  one, `<label> (v<num>)` as today for the editable one.
- A link *set* to an object of the editable instance (drag, Python) is a
  link to a working copy, not to a version. Proposed: it pins to the
  version that instance started from and shows the frozen instance, and
  says so in the report view -- a working copy has no identity a file
  could name until it is saved (item 2).

*What frozen refuses.* Every change to the document's data: a property
write on any object (checked at `DocumentObject::onBeforeChange`, so it
throws before anything changes), adding, removing or renaming objects,
recompute, undo/redo, transactions, branch switch and restore. The Gui's
commands gray out through the same test. View state (visibility, colors,
camera) stays free, and is never logged for a frozen document: it is a
view of a version, not a change to it.

*Save to the log only* (`Document::saveToLog()`, Python
`doc.saveToLog()`, Gui "Save to History"):

- the log records a version of the document's branch -- exactly what a
  save records (27.9), with the same bookkeeping writes -- and the
  branch head moves to it;
- the file's document members are **not** rewritten: what the file opens
  as, its `Version` on disk and its `meta` branch all stay;
- the history has one durable home, the file (27.10): so the archive is
  rewritten with its document members copied raw (`putRawEntry`, 15.11)
  and the history members new. A log-only save with no history in the
  file (preference off) is refused with that reason;
- the document stays marked modified, since the file still does not
  hold what it shows;
- available for any editable document of a saved file: the file's own
  document and an editable version instance alike. For an editable
  version instance, the Save dialog of 27.16 gains it as the default
  button: **Save to History** / Save over File / Cancel.

That also settles Q3 for the ordinary case: saving a version's work no
longer means writing over the file. "Save over File" keeps 27.18's rule
(last save wins); a self-pinned version is frozen now and cannot be saved
at all.

*Undo.* Save to the log only is bookkeeping, not an undo step, as a save
is (27.9).

**Build order.** (1) `FrozenVersion`: status, the edit refusal, pins to
the frozen instance only, `openVersion` to the editable one, the probe of
27.21 as a test; (2) self-pins per 27.20/27.21 (Q1, Q2, Q4); (3)
`saveToLog`; (4) the Gui: labels, gray-out, Save to History, the Q5
close prompt and its preference; a GUI check.

### 27.23 Ruling: names carry the branch; links to a working copy are live (user, 2026-09-26)

**Ruled.**

1. **An editable instance is named `<doc>@<branch>@v<num>`.** Editing is
   always on a branch, so the name says which, and the version tail is
   the version it last saved (or started from); a save -- to the file or
   to the log only -- moves the tail. The full form also disambiguates an
   implicit branch named after its starting version: `Foo@main@v3@v3`
   is branch `main@v3` at v3, and after a save `Foo@main@v3@v8`.
2. **A link to an editable instance is live.** No automatic pin (27.22's
   proposal withdrawn). Pinning is only ever the user's act; when the
   user pins a link to an editable document, that document is saved first
   to get a concrete version number, and the pin names it.

**Proposed detail.**

*Names.* One parser, `FileHistory::parseName(name) -> {file, branch,
version}`, replaces `splitVersion` at its 9 call sites. The file is the
longest prefix that is an existing file or a registered history's path;
the remainder is `@v<num>` (a version: the frozen instance) or
`@<branch>@v<num>` (an editable instance). Everything between the file
and the last `@v<num>` is the branch, so branch names may contain `@v`.

| Instance | `FileName` | Label |
| --- | --- | --- |
| frozen (pins) | `<file>@v<num>` (unchanged) | `<label>@v<num>` |
| editable | `<file>@<branch>@v<num>` | `<label>@<branch>@v<num>` |
| the file's own document | `<file>` (unchanged) | `<label>` (unchanged) |

- An editable instance with no change yet has no branch yet (27.6: at
  the first change). It is named for the branch it will take -- the
  branch whose free tip it is, or the implicit `<branch>@v<num>` -- and
  renamed if creating the branch has to add a `#2` suffix.
- The file's own document keeps its label: that label is the user's and
  is saved in the file. Proposed: the tree shows its branch and version
  as a suffix only while the file has more than one branch (Qa).

*Live links to an editable instance.* The XLink saves the branch, not a
version: `file="Foo.FCStd" branch="main@v3"` (`file=""` when it is the
owner's own file, as for a self-pin, 27.21 Q2). A FreeCAD that does not
know `branch` resolves the file. Resolution keys on (file, branch), not
on the name string, whose version tail moves:

- the document holding that branch, whichever it is (27.12's worktree
  rule: at most one) -- so if the file's own document switches onto the
  branch, the link follows it there;
- else the branch's head opened as an editable instance;
- else (the branch was deleted or trimmed) the file, with a warning, as
  for a failed pin (27.5 ruling 2).

A link set to an editable instance that has no branch yet makes it take
its branch then: the link has to save a name.

*The user pins a link to an editable document.* If the document has
changed since its version, it is saved **to the log only** (27.22) --
the concrete version number without touching the file -- and the pin
names that version; with no change, the pin names the version it is at.
The frozen instance opens and the link moves to it. The same holds for
a link to the file's own document.

**Question left.** Qa: the file's own document in the tree -- label
unchanged always, or a `@<branch>@v<num>` suffix while the file has more
than one branch (proposed)?

The build order of 27.22 stands; names (`parseName`, labels) join step
1, live branch links join step 2.

### 27.24 Ruling: Qa, and the tip form (user, 2026-09-26)

- **Qa: with the suffix.** The file's own document shows
  `@<branch>@v<num>` after its label in the tree while the file has more
  than one branch.
- **The version may be omitted to mean the tip**, and the name then ends
  in `@`: `<doc>@<branch>@`. `parseName` accepts it (`version` 0, branch
  set). It is the stable form of a live link's identity (27.23), whose
  version tail would otherwise move with every save: a live link keys on
  `<file>@<branch>@`, and opening that name finds the document holding
  the branch, or opens the branch's head as an editable instance.

Grammar, after the file: `@v<num>` (a version, frozen), `@<branch>@v<num>`
(an editable instance at a version), `@<branch>@` (a branch's tip).

### 27.25 Where a shape is not shared, and how to close each (2026-09-26)

Asked by the user after the check of 27.24: every document of one file
shares one blob manager (27.10); `ShapeParseCache`
(`src/Mod/Part/App/PropertyTopoShape.cpp:234`) is keyed on the blob
object, so the file's documents share each parsed `TopoDS_TShape`, and
through it the triangulation (BRepMesh writes it into the TShape, and
PartGui's tables key on the TShape). Where that breaks, cheapest fix
first:

**1. The scratch document (restore to a version, branch switch,
`_checkoutHead`).** `_readVersion` materialises the whole version into a
checkout directory -- every blob read from the store (deltas decoded) and
written to disk -- then restores it into a new document with its own
history, so its own blob manager (`getFileHistory` joins by `FileName`,
and the scratch's is the checkout directory). The Gui gives it a
`Gui::Document` and a view provider per object (`slotNewDocument` does
that for every document). `_applyVersion` then captures every property of
every object on both sides, and a shape's `Save` calls `ensureRestored()`:
**every shape of the version is parsed again, only to be compared by its
hash and thrown away.** Cost grows with the model, not with the change.

It is not necessary. The log already holds the path between any two
states: the rows. Cold undo (24.b) applies a row's inverse to the live
document, and `_replayLog` (25.7) applies rows forward. So:

- **restore to version N on the same branch** = the net inverse of the
  rows from the head back to N, folded per property (the fold of 26.8's
  squash), applied as one forward `restore` transaction;
- **switch to another branch** = the inverse fold back to the common
  ancestor, then the forward fold of the other branch's rows to its head;
- only the properties that changed between the two points are written;
  every other value, shape and mesh is left exactly as it is -- not even
  compared.

The snapshot path stays only as the fallback where rows are missing:
trimmed history (26.8), and any chain that crosses a version with no
rows before it (which cases those are is to be checked, not assumed). Where it runs, it
should (a) join the file's history, so it finds live blobs and parsed
shapes, and (b) have no Gui document (a scratch status that
`Gui::Application::slotNewDocument` skips; view values come from
`GuiDocument.xml` as the version holds them).

**2. Materialising a version to open it (`openFileVersion`).** The
version document joins the history, so its shapes share -- but every blob
is still written to the checkout directory and hashed again by
`insertFile` on the way back in. Fix: materialise only the XML entries,
and hand the manager each blob by hash (`find`, or `adoptBytes` from the
store for one it does not hold) before the restore. No disk round trip,
no rehash. Same fix for the fallback of item 1.

**3. A shape restored with a motion** (`locatedForRestore`,
`PropertyTopoShape.cpp:427`): a blob shared by several objects that
differ by a rigid motion (SharedShapeStorage) is copied with
`BRepBuilderAPI_Transform(copy=True)`, because the top location must stay
the object's placement (a `Located` shape broke 1146 links). The copy
drops the sharing all the way down, and its mesh. Fix: a **shallow
move** -- a new top `TShape` (`EmptyCopied`) whose children are the
shared children `Moved(motion)`. The top location is untouched, the
shape type and the element names are the same, and every face and edge
below the top is the shared `TShape`, with its triangulation.

What the top node is decides what the move costs, because a node's own
geometry lives in the node, and the move has to change it:

- *compound, solid, shell, wire*: the node holds nothing but its list of
  children, each with a location. The move is in the new list; nothing
  is lost.
- *edge*: its curves, pcurves and 3D polygon are representations, each
  with its own location (`BRep_Tool::Polygon3D` returns
  `E.Location() * GC->Location()`). The new edge node carries the same
  curve and polygon handles with the motion folded into those
  locations; nothing is lost but a few small representation objects.
- *face*: the new face node keeps the same surface handle with the
  motion in its location, and its wires are the shared ones, moved. But
  its triangulation cannot come along: `BRep_Tool::Triangulation` applies
  only the face's own `Location()` on top of the nodes, so the nodes are
  stored with the face node's surface location already in them. Moving
  the surface location makes them wrong. That one face is meshed again
  (or its nodes transformed into a new `Poly_Triangulation`); everything
  under it stays shared.
- *vertex*: a point stored by value; there is nothing to share.

So only an object whose whole shape is a single face loses anything, and
only that face's mesh.

**4. Two files with the same bytes.** Two managers, two blob objects, two
parses. The comment above `ShapeParseCache` says sharing a TShape across
documents "is not this cache's decision to make"; that was true while a
shape could be changed in place. With shape values frozen (23.13) it can
not be, so the cache can key on the content hash when
`ImmutableShapeValues` is on, and on the blob object otherwise. The
comment is stale in any case: a blob belongs to a file's history now, not
to one document (27.10).

**5. A blob every document let go of.** The cache holds weak entries, so
closing a version document and opening it again, or switching away from
a branch and back, parses again. Fix: keep the last few released parses
(bounded by size) alive. Item 1 removes most of the cases; this catches
the rest.

**6. Coin nodes and vertex arrays** are per view provider unless
instancing applies (render-cache mode 3 with `ShapeInstancing`); the
triangulation under them is shared, and the bgfx backend shares GPU
buffers by a content hash of the mesh (docs/TShapeRenderCache.md). Out
of this thread's scope.

**Proposed order.** 4 and the stale comment (small, isolated), 2, then 1
(rows instead of the scratch -- the largest, and it touches restore and
switch, both gated by the branch and version checks), then 3 and 5. Not
measured yet: how much of a restore's time the scratch path costs on a
real model, and whether PartGui meshes the scratch document's shapes
through its view providers. The first step of 1 is that measurement.

**Ruled (user, same day):** the order above, after the frozen and naming
work of 27.22-27.24.

### 27.26 Step 1 as built: frozen versions, names, live branch links (2026-09-26)

Step 1 of 27.22 with the names of 27.23-27.24. Live branch links, planned
for step 2, came in here: without them a link to an editable instance
resolved to the file's own document (its name no longer parses as a pin),
and `setValue` asserts that the found document is the object's.

**Names.** `FileHistory::parseName(name, parts)` reads the three forms
after the file -- `@v<num>`, `@<branch>@v<num>`, `@<branch>@` -- taking
the file as the longest prefix that exists or is a registered history's
path, so a branch named `main@v3` parses. A file that is gone and has no
history open falls back to the frozen form only, as before. `splitVersion`
wraps it: the file, and the version for the frozen form, -1 for the other
two. Every caller was checked: the ones that only want the file are
unchanged; `DocInfo::get`, the Gui's version save and `Application`'s open
use the parts.

`Document::refreshVersionNames()` names every version document of the
file: `<file>@v<num>` / `<label>@v<num>` for the frozen instance,
`<file>@<branch>@v<num>` / `<label>@<branch>@v<num>` for an editable one,
where the branch is the one it holds or, before its first change, the one
it will take (`TransactionLog::branchName()`, from `planBranch()`, which is
`ensureBranch()`'s choice split out with no side effects). It runs after a
branch is made (including the implicit one), renamed, switched to,
trimmed, deleted or squashed, and after a save moves the version tail.
`saveVersionAsFile` writes the label the document is named after into the
file, not the decorated one.

**Frozen.** `Document::FrozenVersion`, set by `openFileVersion(...,
frozen)` after the restore, with undo mode 0. `checkNotFrozen(what)`
throws, except during a restore, a checkout or bookkeeping. Called at:

- `DocumentObject::onBeforeChange`, so a property write throws before
  anything changes -- except `Visibility`, `TreeRank` and `ViewObject`
  (the same three `checkUserEdit` exempts, for the same reasons) and
  properties never saved;
- `addObject` (both), `addObjects`, `removeObject`, `removeObjects`;
- `restoreVersion`, `createBranch`, `switchBranch`, `saveVersionAsFile`.

`recompute` returns 0 for a frozen document, and the recompute loop skips
a frozen document's objects when another document's recompute reaches
them through a link. The Gui's Save of a frozen document says why and
saves nothing.

`TransactionLogCore::documentAt(version, frozen)` finds only frozen
instances for a pin and never one otherwise; so a version has at most one
of each. The editable instance of the version a file's own document is at
is that document (27.12) -- a second, separate editable instance exists
only for a version the file has moved on from.

**Live branch links.** `PropertyXLink::_liveBranch`, saved as
`branch="..."` when not pinned, read back by `Restore`, carried by
`copyTo`/`Paste`/`isSame`. `DocInfo::get` sets it from an editable
instance's name and keys the `DocInfo` `<file>@<branch>@`;
`DocInfo::isDocument` then matches whichever document holds the branch
(`holderOf`), in `init`, `slotFinishRestoreDocument` and
`restoreDocument`. A save of the holder no longer re-keys a version's or
a branch's `DocInfo` (its key is not a document name). Setting a link to
an object of the file's own document clears the branch; pinning clears
it too, so unpinning shows the file. A link set to an editable instance
with no branch yet makes it take its branch (`takeBranch`).

`Application::openDocumentPrivate` hands any parsed name to
`Document::openFileBranch(history, branch, num)`: no branch -> the frozen
instance; branch and version -> the editable instance at that version;
branch only -> the holder, or the branch's newest version opened
editable and switched to the branch in place.

`pinLink` on a link to an editable instance pins the version its name
ends in when nothing changed since, and otherwise refuses ("save it
first") until step 3's save to the log does that save itself.

Tests: gtests `aNameSaysVersionOrBranch`, `aPinnedVersionIsFrozen`;
Python `TransactionBranchCases.testPinnedVersionIsFrozen` (frozen edits
refused, the editable instance separate once the file moves on, a live
link saved with `branch=` and found again through the holder after the
assembly is reopened). The version and pin GUI checks follow the new
names; the pin check now edits and saves the editable instance, and
checks that the frozen one refuses an edit and a Gui save.

Gates:

- Python 2923 OK;
- ctest 842/842;
- recovery check 15 PASS;
- branch check 27 PASS;
- version check 18 PASS;
- pin check 28 PASS.

### 27.27 Step 2 as built: a local link pinned to its own file (2026-09-26)

27.20 with the rulings of 27.21 (Q1 XLink only, Q2 `file=""`, Q4 allowed).

- **Pinning.** `Document::pinLink` no longer refuses a link into its own
  document. It pins the owner's file; version 0 is the version on disk.
  For a self-pin the version's frozen instance is opened first and the
  pin refused if it has no object of the link's name. `setPin` takes the
  owner's file for a local link; unpinning a self-pin makes the link local
  again, to the object of that name or to none. The Gui's Pin command
  offers local links too.
- **Saved** with no file: `<XLink file="" ... version="N" vuuid="..."/>`,
  and a live branch of the own file the same way with `branch=`. Whether
  the file is the owner's own is decided when the link resolves
  (`_selfFile`, set by `DocInfo::get`), not at save time: after a Save As
  the paths differ, and the link must still save as "this file". A copy
  exported to another document gets the absolute path, as before.
- **Restored** against the owner's file -- a version document's file for
  a link inside one. Not in `Restore` itself: the document's embedded
  history is read after its objects (`adoptEmbeddedHistory`), and a pin
  resolved earlier finds a store with no such version and falls back. It
  is named in `Restore` and resolved in `PropertyXLink::afterRestore`,
  where the version's frozen instance is opened as a pending document.
- **The frozen check lets link machinery through.** Closing the linked
  document detaches a frozen version's links (`LinkDetached`), and
  reopening it restores them (`LinkRestoring`); an object being destroyed
  or removed is not changed either. None of these is a change to the
  version. Found by the test's tearDown: the pinned version (which itself
  carried the self-pinned link to v1) threw while the documents closed.
- A version document of a file whose history has no label is labelled
  after the file's name.

After a Save As the live link still shows the old file's frozen instance
(same content -- the history is a copy) until the document is reopened,
when `file=""` resolves against the new file.

Test: Python `TransactionBranchCases.testLocalLinkPinnedToItsOwnVersion`
-- pin, `file=""` in the saved XML, the pin after a reopen and after a
Save As, unpin/undo/redo, a pin to the version on disk that later edits do
not reach, and a pin refused for a version without the object.

Gates: Python 2924 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28 PASS.

### 27.28 Step 3 as built: save to the log only (2026-09-26)

`Document::saveToLog(name)`; Python `doc.saveToLog(name='Saved to
history')`. For any editable document of a saved file -- the file's own
document or an editable version instance; refused for a frozen one, for a
document with no file, and when the file does not carry its history
(preference not 2 and no pins).

1. The file's guard is read from its `Document.xml`: the save id in
   `Version` and `LastModifiedDate`. A file with no history element or no
   save id is refused ("save it with its history first").
2. Every document of the file flushes what is still parked in the archive
   (as `saveToFile` does), and the implicit transaction is committed.
3. The cursor takes its branch; a snapshot (transaction kind `history`)
   records the version, which is **named** (`name`) at once: a version the
   file does not hold must travel in its history, and embed() keeps only
   named versions and each branch's newest.
4. The history copy is made with `TransactionLog::embedForFile(date,
   saveId)`: the file's own save id and date, so the guard on open still
   matches, and the store's `meta` branch left as it is -- the file
   reopens on the branch it did.
5. The archive is written again (`FileBlobManager::rewriteArchive`) to a
   temporary file beside it and moved over it: every member copied as it
   is stored, not decoded, except `Document.xml`, whose `<History>`
   element alone is replaced; the new database and the kept blobs the
   archive lacks are appended, named by hash. Members under `blobs/` are
   identified by content on open (`restoreFromArchive` hashes each), so
   names do not matter, and `Content.xml` is left as it was.
6. The document's `Version` is not touched -- it is the version the file
   is -- and an editable instance's name takes the new version as its tail.

**The old history database is kept in the file.** The first cut replaced
it where it was, and restoring the saved version failed: the version's
capture names it. Every capture includes the document's own `History`
property, so a kept version keeps the history database of its time.

**Found: a file carries its previous history database.** The same
capture does it on every ordinary save. Probe: five saves of a one-object
document, each file with two `.db` members -- `History.db` and the one
before it (94 KB + 122 KB after the second save, 151 KB + 167 KB after
the fifth). It predates today. The fix belongs in the capture: the
document's own `History` is bookkeeping (`keptOnRestore` already skips
it), so no version should hold its database. Next, as its own commit.

Not done: members of the old history that nothing references any more
are not dropped by a log-only save; the next ordinary save writes the
archive from scratch and leaves them out.

Test: Python `TransactionBranchCases.testSaveToTheLogOnly` -- the model
in the file unchanged (Document.xml without its History element compared
byte for byte), the version named and kept, reopened: the file's value,
the version in its history, no closed branches (the guard held), and a
restore to it; refused with the preference off.

Gates: Python 2925 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28 PASS.

### 27.29 A version no longer holds the history (2026-09-26)

The defect found in 27.28. Every version's blobs included the ones only the
document's own `History` property names -- its database and the blobs it
keeps -- so a kept version kept the history of its time, and each file
carried the history database before its own. The probe of 27.28: two
`.db` members after every save.

A version holds what the document refers to:

- **save and snapshot** take `FileBlobManager::versionEntries()`, the
  collected entries less those whose every referrer is `0:History` (the
  document-level History property, as `referrerOf` names it);
- **open, and a closed file's history read from its archive** (27.13),
  where no referrer is known, filter with
  `TransactionLog::versionBlobs(entries, blobs)`: a hash that appears in
  Document.xml's History element and in no other XML entry of the version
  is not the model's.

`History` was already kept out of what a restore to a version applies
(`keptOnRestore`); a version's Document.xml still names the database, which
a materialised version then lacks, and a version document's History reads
empty -- which is what it should be.

Test: Python `TransactionBranchCases.testAFileCarriesOneHistory` -- four
saves, and a reopen and a save, each file with the one member
`blobs/History.db`. It failed before the fix (two members).

Gates: Python 2926 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28 PASS.

### 27.30 Step 4 as built: the Gui (2026-09-26)

- **Tree labels.** The file's own document shows `@<branch>@v<num>` after
  its label while the file has more than one open branch -- the branch it
  is on and the version it is on disk (its `Version`); refreshed on
  `signalBranchesChanged` and whenever the label is (a save clears the
  modified mark). A frozen version is shown in italics. Version documents
  carry their names in their labels already (27.23).
- **Read-only.** The property editor shows every property of an object of
  a frozen version read-only, but `Visibility` (`PropertyItem::updateData`).
  The App refuses the write anyway (27.26); this only says so up front.
- **Save to History.** `Std_SaveToHistory` in the File menu after Save a
  Copy, for any editable document of a saved file with a log; it records
  the call as a macro line (`App.getDocument(...).saveToLog()`). A version
  document's Save now offers **Save to History** (the default), **Save over
  File** (the standard Save button, renamed: the 27.16 path, warning and
  re-pin offer unchanged) and Cancel. A version document saved to history
  is no longer marked modified -- its history holds what it shows, and a
  close would otherwise ask again forever; the file's own document stays
  modified, since its file still differs.
- **Closing a pinned version** (27.21 Q5). A frozen version opened *for a
  pin* -- through a link resolving (`openDocumentPrivate` with object
  names) or by `pinLink` -- has status `OpenedForPin`; one opened by hand
  has not and is never offered. When the last `DocInfo` pinned to it goes
  (`DocInfo::deinit`: unpinned, deleted, re-pinned, or the linking
  document closed; not while closing everything), the document emits
  `signalPinsReleased`. The Gui looks again on the next event loop turn --
  an undo in the same step may pin it again -- and then, per
  `DocumentParams ClosePinnedVersion` (0 ask, 1 close, 2 keep), closes it,
  keeps it, or asks: "No link pins '<label>' any more. Close it?", Close /
  Keep Open, with "Remember my choice", which sets the preference.

**Deviation from the 27.21 proposal:** with no Gui nothing closes, whatever
the preference. The App has no point at which closing a document from
inside the link machinery is safe, and nothing else to run it from. A
headless caller closes the document itself.

GUI check: `scripts/transaction-log-frozen-check.py` (own
`XDG_CACHE_HOME`): the tree suffix appears with a second branch, the pinned
version's label and italics, Save to History from the command (a named
version, the file's model unchanged) and not offered for a frozen version,
a version document's Save to History (not modified after, its name's tail
moved, the pin unchanged), and the close prompt: asked, closed, remembered,
then closed without asking. 16 PASS.

The pin check (`scripts/transaction-log-pin-check.py`) now sets
`ClosePinnedVersion` to 2: it unpins, and the close prompt is not what it
checks -- without it the run hung on an unanswered dialog.

**Seen, not chased:** the first `saveAs` of a new document holding a
`Part::Box` warns "embedded history of <doc>: blob 1e39ec8b... of a named
version is not in the store" (Document.cpp, embedHistory). Same hash every
run; present in the previous session's pin-check log too, so it predates
this work.

Gates: Python 2926 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28, frozen 16 PASS.

### 27.31 27.25 item 4 as built: one parse for the same bytes in two files (2026-09-26)

`ShapeParseCache` (`src/Mod/Part/App/PropertyTopoShape.cpp`) keeps a second
index, content hash -> a blob whose parse it holds. `get()` misses by blob
object, then, when `PartParams ImmutableShapeValues` is on, finds another
file's parse of the same bytes by its hash and enters it under this blob
too -- so it lives while either file holds it. With shape values not
frozen nothing changes: a TShape could then be changed in place, and each
file parses its own. The sweep rebuilds the hash index from the entries it
keeps. The stale comment is rewritten: a blob belongs to a file's history,
shared by all of the file's documents (27.10), not to one document.

Test: `ShapeStorage.ShapeBlobCases.testTwoFilesWithTheSameGeometryShareItsParse`
-- a file and a byte copy of it, opened together, share one TShape with the
freeze on; a second copy opened with it off does not.

Gates: Python 2927 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28, frozen 16 PASS.

### 27.32 27.25 item 2 as built: a version opened without the disk round trip (2026-09-26)

`Document::materialiseVersion(log, num, dir, blobsInStore)`. With
`blobsInStore` and a version written under schema 5 -- whose Document.xml
names every blob by its hash -- a blob entry is not written to the
checkout directory: one the file's store has live is left to whoever holds
it, and one it has not is made live by `TransactionLogCore::restoreBlob`
(decoded from a delta if need be, then held by the log). The restore finds
each by hash when its property asks. Only the XML entries go to disk.
`openFileVersion` passes it; the scratch document of a restore or a switch
(`_readVersion`) and a crash recovery do not -- the scratch has a store of
its own (item 1 removes it), and a recovery rebuilds the store it would
read from. A schema-4 version still goes to disk whole: it names its files.
`TransactionLogCore::liveBlob(hash)` is new; `materialiseVersion` logs how
many entries it wrote and how many blobs it took from the store.

Measured on five boxes, version 1 opened after the boxes changed: "1
entries written, 6 blobs from the store" (Document.xml; the five shapes
and the stock material card).

Gates: Python 2927 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28, frozen 16 PASS.

### 27.33 27.25 item 1, first step: what the scratch document costs (2026-09-26)

Restore to a version that differs from the document by one label, through
the scratch document (`restoreTransactionVersion`), against undoing that
restore (the same change, applied from the undo stack):

| model | open | restore via scratch | again | undo |
| --- | --- | --- | --- | --- |
| issue360_fillet_spike.FCStd (7 MB, 148 objects, 126 shapes; headless) | 2.90 s | 3.91 s | 7.07 s | 0.02 s |
| 150 cylinders + 150 boxes, generated; headless | -- | 0.11 s | 0.10 s | 0.000 s |
| the same in the GUI (offscreen) | -- | 0.14 s | 0.14 s | 0.002 s |

- On a real model the scratch path costs more than opening the file: every
  shape of the version is parsed again, and the second restore, with two
  generations of values to capture and compare, costs more still.
- Primitives are cheap to parse, so the generated model shows the fixed
  cost only. The GUI adds about a third -- a Gui document and a view
  provider per object for the scratch -- and nothing that looks like
  meshing: 150 cylinders tessellated would show.
- The same fillet model would not open in the GUI here: it needs the
  Assembly3 module, which this build lacks, and the open stopped on a
  dialog. The GUI column is the generated model's.

Also measured: `saveToLog` on the 7 MB model, 1.41 s -- the snapshot, and
the archive rewrite, which decodes and hashes every blob member to know
what the archive holds. `Content.xml` or the History element's own list
could answer most of that without the decode; not done.

### 27.34 27.25 item 1 as built: restore and switch through the rows (2026-09-26)

`Document::_moveAlongLog(fromHead, toSeq, views)` takes the document from
the state at log row `fromHead` to the state at `toSeq` by the rows
between them, and nothing else:

1. **Where the chains meet**: the newest point of `toSeq`'s chain -- the
   row itself or a row's parent -- that is on `fromHead`'s. Both tails from
   there must be whole: a chain a trim cut starts at a row whose parent is
   gone, and then there is no way through the rows.
2. **The fold** (`LogFold`): the tail of `fromHead`'s chain taken back,
   newest row first and each row's ops in reverse, every op to its
   before -- an object a row created is gone, one it removed is there with
   the values the row's sets had before, a dynamic property it added is
   gone -- then the tail of `toSeq`'s chain taken forward, as the crash
   replay folds (25.7). The result is where every object, dynamic property
   and value touched on the way ends.
3. **Applied by difference**, in the passes of a cold undo: the objects
   there at the end that are missing, the dynamic properties, then each
   value whose captured form differs from the document's (the comparison
   `_applyVersion` made), its blobs made live first
   (`TransactionLog::restoreBlobsOf`, what `readRevert` does for a cold
   undo); objects whose derived values the log did not keep are touched,
   and one whose derived values came back with its inputs is not; then
   what is gone at the end. A move that changes nothing writes nothing, so
   a restore to the version the document already is leaves no step.

It declines, with nothing written, when the chains do not meet, a forward
row has a value that never reached the log, a value cannot be read, or the
path crosses an open's record (an ops-less `restore` row) whose file is not
what the rows before it add up to -- its Document.xml differs from the
previous version's on the chain. A save to the log only (27.28) leaves
exactly that: the file reopens older than its history's tip. The first cut
reverted row by row and missed it; `testSaveToTheLogOnly` found it.
Reverting row by row also recorded writes that netted to nothing, which
`restoresAVersion` found ("already version 2: no step"): hence the fold.

Callers: `restoreVersion` (views only under ViewObjectTransaction, as
before) inside its one `restore` transaction; `_checkoutHead(fromHead)`
for `switchBranch` and for `createBranch` from an older version, under
`replaying` as before, views included. When it declines, the old path
runs: the version read whole into the scratch document and its difference
applied. The scratch path stays for that: trimmed history, and a file
reopened behind its history.

`_replayLog` takes an explicit head and a strict mode, and makes each
value's blobs live before restoring it: a blob the log keeps as a delta is
not live until decoded, which a crash recovery never met because it
recovers the whole store first.

Measured on the model of 27.33:

| | before | now |
| --- | --- | --- |
| restore to va | 3.91 s | 0.078 s |
| restore to va again | 7.07 s | 0.166 s |

Test: Python `TransactionBranchCases.testRestoreAndSwitchGoThroughTheRows`
-- a restore and its undo, a branch made from an older version, switches
both ways; no document is made on the way, and values and objects are the
target's.

Gates: Python 2928 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28, frozen 16 PASS.

### 27.35 27.25 items 3 and 5 as built (2026-09-26)

**Item 3, the shallow move.** `locatedForRestore` moves a shape restored
with a motion by `shallowMove()` (`PropertyTopoShape.cpp`): a new top node
(`EmptyCopied`, with the top's Closed, Orientable, Infinite and Convex
flags) whose children are the shared children, each `Moved(motion)`. The
top location stays the object's Placement; every face and edge below is
the original's TShape, triangulation included. A compound, compsolid,
solid, shell or wire top takes it; a face, edge or vertex top, and a motion
that scales (OCCT refuses a scaled location), are copied as before -- the
edge case could share too (27.25), not done.

Test: `ShapeStorage.ShapeCongruenceCases.testMovedInstancesShareTheirFaces`
-- two instances of one part, stored once and restored with two motions:
their tops are their own, every face is one TShape, the volume is right.
The congruence cases that were there (positions, placements, distinct parts
kept apart) pass unchanged.

**Item 5, released parses kept.** `ShapeParseCache` entries carry their
content hash and a use tick. With shape values frozen:

- `get()` finds a parse by hash even when its blob has been released
  (expired) -- a file closed and opened again, a branch switched away from
  and back -- and takes it over under the new blob;
- the sweep keeps the 32 most recently used released entries and drops the
  rest (with shape values not frozen, it drops them all, as before).

Every lookup by hash now checks the entry's own hash: a released blob's
address can be a new blob's, and item 4's index could then have handed out
another file's parse. That was a real hole in 27.31, closed here.

Test: `ShapeStorage.ShapeBlobCases.testAReleasedParseIsKeptForAWhile` -- a
file opened, closed and opened again gives the same TShape.

Gates: Python 2930 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28, frozen 16 PASS.

### 27.36 Next (user, 2026-09-26)

Before the file-scope state of 27.18, chase what was seen and not chased:

1. The first `saveAs` of a new document holding a `Part::Box` warns
   "embedded history of <doc>: blob 1e39ec8b... of a named version is not
   in the store" (27.30). Predates this work.
2. `saveToLog` decodes and hashes every blob member of the archive to learn
   what it holds (27.28, 27.33: 1.4 s on a 7 MB file); `Content.xml` or the
   History element's own list could answer without the decode.
3. A shallow move of a shape whose top is an edge copies it, though its
   curves and 3D polygon could be shared (27.35).
4. Headless, a released pinned version never closes, whatever
   `ClosePinnedVersion` says (27.30) -- find a safe point, or rule the
   deviation.
5. A log-only save leaves members of the old history that nothing
   references in the file until the next ordinary save (27.28).

Then the file-scope state (27.18: last object id, string hasher, object
name table, per-object id counters), design first.

### 27.37 The seen issues of 27.36, chased (2026-09-26)

**1. "blob 1e39ec8b... of a named version is not in the store".** The blob
is the stock material card (675 bytes, `.FCMat`). The log captures a value
from a detached copy of the property (`ValueTask::copy`, on its worker), and
`PropertyMaterial::ensureBlob()` makes the card's blob on first use -- in
`blobManager()`, which for a property with no container was the
process-wide `FileBlobManager::defaultManager()`. So the op value of the
object's creation named a blob that lived in the system temp directory, not
in the file's store: `embedHistory` could not find it and left it out, and
`makeDurable` never saw it. Any file whose first saved transaction created
a Part feature carried a history naming a blob the file did not hold; a
version needing that card was then restorable only while the library had
the same stock card.

Fixed at the capture: `CaptureConfig` carries the document's blob manager,
`captureValue` hands it to its `BlobRecorder`, and
`FileBlobManager::managerFor(container)` -- the document's manager, else the
one a capture on this thread names, else the default -- replaces the seven
copies of the fallback (`PropertyMaterial`, `PropertyPartShape`,
`PropertyHistory`, `PropertyAppearanceList`, `PropertyFileIncluded`,
`PropertyStringIncluded`, `PropertyFileIncludedList`).

Test: Python `TransactionBranchCases.testTheHistoryCarriesEveryBlobItNames`
-- a `Part::Box` created in a transaction and saved; the blobs the embedded
history database holds as files equal the `<Blob>` list of the file's
History element.

**2 and 5. `saveToLog` reads the archive's index; the old history goes.**
`FileBlobManager::archiveBlobIndex(path)` answers what the archive holds
from its `blobs/Content.xml` (member -> hash and referrers), decoding and
hashing only a member the index does not list. A member whose every
referrer is the document's `History` (`FileBlobManager::historyReferrer()`,
`0:History`) and whose hash the new history does not keep is left out of
the rewrite (`rewriteArchive`'s new `drop` set) -- no version holds the
history since 27.29, so the old database is nothing's. What is added is
entered in the index, which is written again in place (it must stay ahead
of the content: reaching it is what makes a restore serve the whole
archive). In a file without an index -- schema 4, where nothing but the
history is under `blobs/` -- a member is the old history's when the old
History element names its hash and nothing else in `Document.xml` or
`GuiDocument.xml` does.

`saveToLog` logs its phases (`App` log level): on the 7 MB model of 27.33,
saved as schema 5 -- index 0.002 s, rewrite 0.022 s, snapshot 5.0 s. The
1.41 s of 27.33 was the file as found, **schema 4** (snapshot 1.3 s there).
The schema-5 snapshot costs what an ordinary schema-5 save of the same
document costs -- 4.9 s with the log off, 5.1 s on -- since it is a save
pass into a null writer; the time is the save path's, not the log's.
**Seen, not chased**: a label edit on this model costs a 5 s schema-5 save.

Tests: `testSaveToTheLogOnly` now also checks one `.db` member after the
save, the index listing exactly the archive's blob members, and the index
ahead of them. The index-less case was run by hand on the model: three
log-only saves, one database member after each, the history reopened with
all its versions.

**3. The shallow move of a face.** A face on top is moved by a new TFace
(`moveOwnSurface()`): `EmptyCopied()` takes the surface, the location and
the tolerance; the location becomes `motion * location`, the natural
restriction and the triangulations are the original's, and the wires are
added moved like any other top's children. A pcurve is looked up by the
surface and location its face's surface is at, and the motion cancels out
of that. Surface, triangulation, wires, edges and their curves are shared.

An edge on top was the other half of the item, and there is nothing to do:
a restore motion comes only from congruence at save
(`CongruenceIndex::find`), and `shapeCongruenceKey()` needs three vertices
to recover a frame. An edge has two at most, so an edge top is never
stored as a moved instance. A first cut that moved an edge's curve
representations was taken out as dead code.

Test: `ShapeStorage.ShapeCongruenceCases.testMovedFaceTopsShareTheirSurface`
-- a holed face in two places, stored once; reopened, the tops are distinct,
every wire is shared, every edge has its pcurve, area and position as
saved.

**4.** Not built: it needs a ruling, 27.38.

Gates: Python 2932 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28, frozen 16 PASS. The warning of item 1 appears nowhere in them (it
was in six suite tests and the pin check).

### 27.38 Proposed: closing a released pinned version with no Gui (2026-09-26)

With no Gui, `ClosePinnedVersion` 1 (close) does nothing: `signalPinsReleased`
fires from `DocInfo::deinit`, inside the link machinery, and closing a
document there pulls it out from under the caller (27.30). The Gui waits for
the next event loop turn; FreeCADCmd has no loop.

Proposed: the App keeps the released documents in a pending set when no one
handles the signal (the Gui connects, so the App knows), and drains it at the
end of the outermost of these, when no document is recomputing, restoring or
closing:

- `Application::closeDocument` -- the linking document closed, the common
  case;
- a transaction's commit or abort at depth 0 -- an unpin, a re-pin, a
  deleted link;
- undo and redo.

Each drain looks again (a later step may have pinned it again), and closes
only with the preference at 1; Ask stays keep, headless. A Python
`FreeCAD.closeReleasedVersions()` would let a script drain at a point of its
own. Waiting on the user.

### 27.39 Ruling and as built: a released pinned version closes with no Gui (2026-09-27)

**Ruling (user):** a frozen version no link pins is safe to close -- the
only question is where. Close it once the operation that released it has
ended, if it is still unpinned. The 27.30 deviation (no Gui, nothing
closes) is withdrawn.

**As built.** `App::OperationScope` marks one App operation that may let go
of a pin; `Application` counts how deep they nest. `DocInfo::deinit` notes
the released version (`Application::noteReleasedVersion`) as well as
emitting `signalPinsReleased`; when the outermost scope ends,
`Application::closeReleasedVersions()` looks at each again -- still frozen,
opened for pins, no link into it (`PropertyXLink::getDocumentInList`) --
and closes it with `ClosePinnedVersion` at 1. At 0 (ask) and 2 (keep) it
forgets them: with no one to ask, ask keeps. It does nothing while a
document restores, recomputes, or all are closing, and loops, since a
closed version may release the pins it held.

Scopes: `Application::closeDocument`, `openDocuments`,
`closeActiveTransaction`; `Document::commitTransaction`,
`abortTransaction`, `undo`, `redo`, `undoLogged`, `clearUndos`,
`removeObject`, `recompute`, `restoreVersion`, `switchBranch`,
`trimBranch`, `deleteBranch`, `pinLink`; `PropertyXLink::setPin` and
`setPyObject`. A release under none of them waits for the next scope to
end, or `FreeCAD.closeReleasedVersions()`, which a script may call at a
point of its own.

The Gui keeps its own handling -- it asks, and closes on the event loop --
and says so at start (`Application::setReleasedVersionsHandled(true)`), so
the App stands aside there.

**Found on the way: undo of an unpin lost the pin's document.** An undo
copy of a link names the linked object by its document's name
(`PropertyXLink::copyTo`) and dropped the file path. Once the pinned
version had been closed -- by hand before today, by this rule now -- the
undo's `Paste` warned "Document not found" and returned, after it had
already restored the pin fields: the link showed the live file while
`getLinkPin` reported the pin. The copy now keeps the file path, and
`Paste` resolves by file when the named document is gone, which opens the
version again. That open, inside the undo, then refused its own
`clearUndos` (`Transaction::isApplying()` is process-wide); a document with
nothing to clear now returns first.

Test: Python `TransactionBranchCases.testReleasedPinnedVersionClosesHeadless`
-- with close: an unpin closes it at once; the undo pins again and opens
it again, by file; a second pin keeps it open when the first goes;
deleting the last pinning link closes it; closing the linking document
closes it, and not the file's own document. With ask: an unpin keeps it,
and `closeReleasedVersions()` closes nothing.

Gates: Python 2933 OK; ctest 842/842; recovery 15, branch 27, version 18,
pin 28, frozen 16 PASS (the frozen check covers the Gui's prompt, unchanged).

### 27.40 File-scope state: survey and proposed design (2026-09-27)

The last item of 27.18: the counters and tables that are per document
today, so that each version document and branch of one file has its own
copy, made one per file.

**What exists.**

- *Object ids.* `DocumentP::addObject` hands out `++lastObjectId`. A new
  document starts it at a random 10..5000; `Document::readObjects` sets it
  to the largest id restored. Branches are kept apart by a stride:
  `openFileVersion` and `createBranch` start a branch `branchStride()`
  (2^16..2^20, random) above every branch's `idBase`/`lastId` and every
  open document, and `_leaveBranch` records the branch's `lastId`. Nothing
  records the counter itself.
- *The string hasher.* `DocumentP::Hasher`, one per document; a version
  document restores its own. `StringHasher::lastID()` is the largest id in
  the table plus one. The table holds a reference on every id it made and
  frees none within a session (`compact()` runs only from
  `setSaveAll(false)`), but a save writes only the marked ids and a restore
  reads back only those, so after a reopen the ids above the largest one
  the saved state uses are handed out again, to other strings.
- *Object names.* `getUniqueObjectName` checks the live `objectMap` only:
  a deleted object's name is free again at once.
- *Labels.* Unique among the live objects of one document
  (`PropertyString` for `Label`, unless `DuplicateLabels` or
  `allowDuplicateLabel()`).
- *Counters inside objects.* The survey of `src/Mod` found one that
  persists in effect: Sketcher's `geoLastId`. It is not saved; each
  restore sets it to the largest geometry id present (SketchObject.cpp
  ~1021, ~1149, ~1343). The other `++counter` sites are solver-local.
- *Already file scope:* the log's sequence and version counters, the
  branch table, the blob manager, the transient directory.

**Found: one branch reuses ids and names after a reopen.** Measured on
the default build (log on): add `Box` (id 1286) and `Box001` (1287),
delete `Box001`, save, close, reopen, add a cylinder -- it is `Box001`,
id 1287. So the log's chain has ops on cid 1287 for two different objects,
and an element-map tag 1287 in an old version names another object than
it does at the tip. The hasher does the same with string ids by reading of
the code (above). None of this needs a second branch; the stride never
covered it. What reads the log by cid (`lastOpOn`, the fold of 27.34, the
refuse rule of selective undo) can conflate the two objects; no failure
was chased.

**Proposed shape.** One allocator per file, held by `TransactionLogCore`
beside the sequence counters, written to the store's `meta` in the same
worker job as the append that first uses a value, and carried by the
embedded copy and `saveToLog` like the rest of the store. A document with
no history (log off, or saved without it) keeps a document-scope
allocator with the same interface. Two documents of the file open in one
process draw from one allocator, so neither can hand out a value the other
has.

1. **Object ids.** `lastObjectId` moves to the file. A store written before
   this starts the counter at the largest of: every branch's `idBase` and
   `lastId`, every cid in the `op` table, every open document's counter.
   New branches record `idBase` 0 and the stride goes; old strided
   branches keep their ids. The root element also gains a `LastId`
   attribute, read back as the larger of it and the largest id restored,
   so a file opened with the log off does not reuse either.
2. **The string hasher.** The file's history owns one `StringHasher`,
   which every document of the file uses as `d->Hasher`. It counts from a
   stored `_lastId` that never goes down (a `lastid` attribute on the
   `StringHasher` element, and the store's meta), not from the table's
   largest entry. A document restoring into it stages its table first:
   an id already present with the same string is shared; an absent id is
   inserted under its number. An id present with another string can only
   come from history written before this change: that document gets a
   private hasher, as every document has today, and the log says so. One
   case stays: a string the tip no longer used when the file was saved,
   minted again after the reopen under a new id, and then brought back
   under its old id by restoring an old version -- two ids for one string,
   which the table's unique string key does not allow. See Q2.
3. **The object name table.** A file-scope map name -> cid, in the store
   (a `name` table) and in memory. `getUniqueObjectName` treats a name as
   taken when a live object has it or the table gives it to another cid;
   the same cid may always take its own name back (undo, restore, a
   version). The name enters the table when `addObject` allocates it, not
   at commit, so two documents of the file cannot both hand out `Pad001`
   in between. A store written before this seeds the table from the
   `create` ops (cname, cid) and the open documents; an old collision (one
   name, two cids) keeps the newer cid and is left to the merge, as 17.2
   said. An import renames through `readObjects`' name map as it does now.
   Names are never freed, trimming included.
4. **Ids inside objects.** One file-scope counter for sketch geometry ids:
   the next id is one above the larger of the counter and the sketch's own
   largest id. Geometry ids need be unique only within a sketch, which one
   counter for every sketch of the file gives with no per-object table;
   they grow faster, and they are `long`. A sketch in a document with no
   history uses the same rule with a document counter. The restore repair
   of duplicate ids stays. Offered to other modules as
   `DocumentObject::allocateId(key)` -> the file's counter for `key`.
5. **Tags** follow from 1.

**Questions.**

- **Q1. Object ids without a log.** Add the `LastId` attribute (a file
  opened with the log off then never reuses an id either), or leave the
  log-off case as it is today? Recommended: add it; it is one attribute and
  closes the single-branch defect for every file.
- **Q2. One string at two ids.** (a) Keep the file's whole string table in
  the store -- every id ever handed out, the union of every version's --
  and look a string up there before minting, so a string always gets its
  old id back; costs a store lookup on every miss (element map building
  is hot) or an in-memory index of every string's hash. (b) Allow the
  second id as an entry reachable by number only; the two versions then
  name the same element differently, which is the rare leftover of today's
  problem, not a wrong answer. Recommended: (b) now, with a count of how
  often it happens, and (a) only if the count says so.
- **Q3. Names never freed, undo included.** Add `Pad001`, undo, add again:
  today `Pad001` again, with the table `Pad002`, because the undone create
  is still in the log and a redo must be able to bring it back. This is
  the visible change of the whole section, on the commonest path.
  Recommended: accept -- it is what "deleted names stay taken" means.
- **Q4. Labels.** (a) Leave them document scope and let the merge resolve a
  clash -- suffix the incoming label and rewrite the `<<label>>`
  references, which relabelling already does. (b) The same rule as names:
  a label once given stays its object's, in every version and branch, so a
  label the user types can come out suffixed because a deleted object once
  had it. Recommended: (a); labels are the user's text, and (b) makes
  typing one surprising.
- **Q5. One geometry-id counter per file, or one per object** (a table
  keyed by (cid, key))? Recommended: one per file (4 above); per object
  only if some module needs dense ids.
- **Q6. Two processes on one file** (17.5) would need the allocator in the
  store itself -- ids reserved in blocks. Nothing runs two processes on one
  store today. Recommended: keep every allocation behind the one allocator
  so blocks can come later, and not build them now.

**To check while building.** The log's worker serialises detached copies
(sec 20); if a shape's element map is written there, its `StringID` marks
are set off the main thread while a save on the main thread clears them.
With one hasher per file that race spans documents; today it spans one.

**Build order (proposed).** Each step with the gates (Python, ctest, the
five GUI checks) and a test of its own.

1. Object ids: the file counter, `LastId`, the stride retired, old stores
   seeded. Test: the reopen case above, and two branches and two version
   documents of one file never sharing an id.
2. The name table. Test: the reopen case gives `Box002`; two branches
   adding a `Pad` get `Pad001` and `Pad002`; undo, restore and a version
   take their own names back.
3. The shared hasher, per Q2. Test: one shape in two branches has the same
   element map; a pre-change file with colliding tables falls back to a
   private hasher.
4. The geometry-id counter. Test: a sketch edited in two branches mints no
   id twice; a reopen does not reuse a deleted geometry's id.
5. Labels, per Q4.

### 27.41 Rulings on 27.40 (user, 2026-09-27)

- **Q1: yes.** The root element carries `LastId`.
- **Q2: (b).** A string brought back under its old id while the table has
  it under a new one keeps both, the old id reachable by number only; the
  log counts the cases.
- **Q3: the name table is one-to-one, object id <-> name.** Restoring a
  deleted object -- undo, a version, a branch -- always gives back its
  exact id and name. What follows, and is the visible change: a *new*
  object never takes a name the table gives another id, so add `Pad001`,
  undo, add a new pad gives `Pad002` (today `Pad001`).
- **Q4: (a).** Labels stay document scope; the merge resolves a clash.
- **Q5: per object.** A file-scope map object id -> last geometry id, in
  place of one counter for every sketch.
- **Q6: delayed.** Every allocation goes through the one allocator; blocks
  from the store are not built.

### 27.42 Step 1 as built: one object id counter per file (2026-09-27)

`FileHistory` holds the counter (`lastObjectId`, `noteObjectId`,
`nextObjectId`). `DocumentP::addObject` takes a new object's id from it when
the document has a history, from its own counter otherwise, and notes every
id it places -- restored, undone, folded back -- into both, so neither
counter hands out an id in use. A document that gets or joins its history
later notes what it made before (`_noteObjectsInHistory`).

Where the counter comes from on open, the largest of:
- the root element's new `LastId` attribute (Q1), written by every save --
  so a file opened with the log off does not reuse an id either;
- the largest id restored;
- from the store (`TransactionLogCore::openStore`): the embedded copy's
  meta `last_object_id` (written by `embed`), the largest cid of any op
  (`TransactionStore::maxObjectId`), and every branch's `idBase`/`lastId`.

`readObjects` no longer borrows `lastObjectId` to place a restored object
under its saved id; it passes the id in `DocumentP::restoringId`, which a
shared counter needs.

The stride is gone: `branchStride`, `TransactionLog::setIdBase`, the id
arithmetic of `openFileVersion`, `createBranch` and `_arriveOnBranch`. A new
branch row has `idBase` 0, and the branch records drop `id_base`. Old
strided branches keep their ids, and their bases still raise the counter.
`_leaveBranch` still records the branch's `lastId`.

Tests: Python `TransactionBranchCases.testReopenHandsOutNoDeletedId` -- the
case of 27.40, with the log off and on, and a branch and main adding one
object each get increasing ids from one counter. C++: the branch tests
assert `idBase` 0 and that main's next id is past the side branch's; the
version test that the live document's next id is past the version
document's.

### 27.43 Step 2 as built: the file's object name table (2026-09-27)

`FileHistory` holds the table, one to one (`objectIdOfName`,
`noteObjectName`, `objectNames`): the first pairing of a name or an id
stays, so a pair from history written before the table -- one name on two
branches with two ids -- leaves the table as it is, for the merge.

Every add registers its (name, id) -- the three `addObject` paths,
`addObjects` and `_addObject` -- and a document that gets or joins its
history registers what it has (`_noteObjectsInHistory`).
`getUniqueObjectName(name, id)` gains the id: for a new object (0) a name
the table gives any object is taken, and the uniquing numbers past the
table's names as well as the document's; an object coming back under its
id -- undo, redo, `readObjects` (its `restoringId`), the folds of 27.34 --
keeps its name whenever the document has it free, whatever the table says.
So a restore never renames. `addObjects` reserves the table's names too.

Stored as the store's `objname(cid, name)`, written into every embedded
copy (`embed`). On open (`openStore`) the table is seeded from `objname`,
then from every `create` op newest first -- which covers what came after
the last copy, and a store that predates the table.

Test: Python `TransactionBranchCases.testObjectNamesAreFileScope` -- undo
and redo bring `Box001` back with its id; a new object after them is
`Box002`, after a reopen `Box003`; `side` and `main` each adding a `Pad` get
`Pad` and `Pad001`; the table travels in the file (`Pad002` after a save
and reopen on `side`).

Gates (steps 1 and 2): Python 2935 OK; ctest 842/842; recovery 15, branch
27, version 18, pin 28, frozen 16 PASS.
