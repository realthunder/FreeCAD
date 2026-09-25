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
