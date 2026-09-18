# An append-only transaction log, and undo as a forward operation

Status (2026-09-18): **base idea recorded, not designed and not
scheduled.** Written down so the next discussion starts from something.
Nothing here is committed to; the open questions at the end are the real
content.

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

These are the design, and none of them is settled.

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
