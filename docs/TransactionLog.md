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
between saves with it (below); not yet: eviction, and the checkout that
reads a manifest back.

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
`makeBlob` has run. Not yet: a blob held by the log surviving log
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

**`GuiDocument.xml` in the manifest, as built** (2026-09-23). No Base
change was needed after all: the Gui document writes and reads its entry
through `SaveDocFile(Base::Writer&)` / `RestoreDocFile(Base::Reader&)`,
which hold the stream, so a `GuiEntryTap` guard in each begins the tap
before the first byte (before the `XMLReader` is built, on restore) and
ends it -- draining, on restore -- when the function returns, then hands
the bytes to `App::Document::noteFileEntry`. The App document arms
`wantsFileEntries()` for the span of a save or a tapped restore and
passes what it was given, `Document.xml` first, as the `Entries` of
`onSave` / `onRestore`; `snapshot()` stores each as a durable value and
lists each in the manifest, `docxml_hash` being the first's. Only the
main entry: split view files (`SplitXML`) are their own entries and are
not in the manifest yet. Verified in the GUI on both paths: the manifest
hashes of both entries match the archive's, saved and opened.

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

**Next.** Eviction of unnamed versions (16.3), and the checkout that
reads a manifest back (16.1).

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

