# Input Properties -- Independent Parameters and the Input Recompute Stratum

Status: design. Phase 0 and 1 implemented; phase 2 pending.

This document defines `Property::Input`, a property status meaning "this property is
never written by the owning object's `execute()`". It is the foundation for reading a
container's parameters from its own children without a dependency cycle, and for a
dedicated recompute stratum that settles all parameters before any geometry runs.

Companion to [ComputeBoundaries.md](./ComputeBoundaries.md), whose out-of-process goal
this design directly serves (section 10).

## 1. The problem

A `Part` holds a parameter, say `Part.Length`, and a `Cube` inside that `Part` wants to
read it: `Cube.Length = Part.Length`. That is a cycle. `Part` already depends on `Cube`
through its group, so an expression edge back from `Cube` to `Part` closes the loop and
the cycle checker rejects it.

The pattern is not exotic; it is what people mean by a parametric assembly. It is the
motivating case for upstream FreeCAD's FEP-0010 (variant parts) and it is the reason
`hiddenref()` exists in this fork.

The cycle is spurious. `Part.Length` is a user-set parameter. Nothing about `Part`'s own
recompute produces it. `Cube` does not need to wait for `Part` to recompute in order to
read a value that `Part` never computes. The dependency is real for *value propagation*
and false for *ordering*.

Nothing in the current model can say that, so the two are conflated and the cycle stands.

## 2. What we already have, and why it is not enough

This fork has three mechanisms that overlap this space.

**Value-equality short-circuit.** `Property::aboutToSetValue()` snapshots the old value
via `copyBeforeChange()`; `Property::hasSetValue()` compares with `isSame()` and does not
touch when the value is unchanged. Gated on `DocumentParams::getOptimizeRecompute()`,
default true. This is what makes an unchanged recompute cheap, and it is strictly
stronger than a structural dependency filter for that purpose.

**Revision-based link freshness.** `DocumentObject::_revision` advances only on a genuine
change to a non-Output property. `PropertyLink::isTouched()` compares the linked object's
revision against the one stamped at the last `purgeTouched()`; `PropertyLinkSub` and
`PropertyXLink` compare a revision per sub-object hop. This gives dependency granularity
*below* the object.

**`hiddenref()`.** An expression-level escape hatch: the reference creates no dependency
edge at all. `PropertyXLinkContainer::updateDeps()` skips `_addBackLink()` for hidden
deps, and cross-document hidden deps get `LinkScope::Hidden`, whose `isTouched()` is
always false. In place of the edge, `PropertyExpressionEngine::hasSetValue()` installs
`signalChanged` connections and `updateHiddenReference()` re-evaluates and writes the
bound value eagerly, the moment the source changes.

`hiddenref()` solves the cycle. What it does not do:

- It is declared per reference, by the consumer, inside expression text. The producer
  cannot state the invariant once for all consumers.
- It asserts nothing that can be checked. The author is promising that the target is a
  user input; nothing verifies it. Point it at a computed property and it still works,
  silently, with no ordering guarantee.
- It removes the edge outright, so there is no recompute propagation and no revision
  check. Cross-document, a change made while the referring document is closed is not
  detected on reopen.
- The eager write lands at signal time, so if the source does change during a recompute
  the write can be observed by an object that has already recomputed.

Input properties address exactly those four points. `hiddenref()` is not replaced; see
section 9.

## 3. The concept

An **input property** is one whose value is not produced by the document. It is set by
the user, by script, or -- under the closure rule of section 4 -- computed from other
input properties. It is never written by the owning object's `execute()`.

The name follows upstream's `Prop_Input`. "Independent property" is more accurate and is
the term to keep in mind when reading this: the property is independent of the object it
happens to live on. You should regard it as though it were defined elsewhere.

Two things follow, and they are the whole design:

1. Because no `execute()` writes it, no consumer needs to be ordered after the owner.
   The ordering edge can be dropped. The cycle disappears.
2. Because that is a claim about the producer, it can be **enforced**. A write during the
   object phase that actually changes the value is a hard error. This is the part
   upstream does not do, and it is what turns a promise into an invariant that the rest
   of the design can rely on.

## 4. Definition and the closure rule

A property may be marked input if either:

- it carries no expression, or
- it carries an expression whose every referenced property is itself an input property.

The second clause makes input-ness closed under expression composition. Input properties
therefore form a sub-DAG with no incoming edges from computed properties. That sub-DAG
can be driven to a fixed point before any `execute()` runs, and it will not move again
for the rest of the recompute.

A reference wrapped in `hiddenref()` counts towards the closure only when its target is
itself an input property; the wrapper is then redundant and the reference is treated as
an ordinary in-stratum reference. A `hiddenref()` to a computed property does not count,
and disqualifies the referring property from being marked input. Without that rule the
input value could change during the object phase and the stratification would not hold.

`dbind()` is excluded from the closure entirely; see section 9.

The closure is checked when an expression is set on an input property, and when a
property is marked input. It is *not* re-checked when a referenced property loses its
input status, because that status is a runtime bit any script may clear; see section 7.

## 5. The input stratum

A new phase runs at the head of `Document::recompute()`, after the `Recomputing` status
locker and `signalBeforeRecompute()`, and **before** `getDependencyList()` builds the
object list. It must precede the object topological sort, or touches raised by the phase
would not be visible to it and a pure parameter edit would produce an empty work list.

The phase:

1. Collects the dirty input properties.
2. Topologically sorts them over the input sub-DAG. Ordering is among input properties
   only; the object graph plays no part.
3. Evaluates each in order, writing through the normal property setters so that
   `isSame()` filtering applies and an unchanged result costs nothing downstream.
4. For every input property whose value actually changed, touches its non-input
   referrers so they enter the object phase.

Step 4 needs a propagation-only record of "which properties reference this input
property". It carries no ordering implication -- ordering inside the stratum is handled
by step 2, and ordering outside it does not exist by construction. It is scoped to input
properties, so it is a small structure, unlike a dependency graph over every property in
the document.

The phase must also run after restore and after undo/redo, before the first object
recompute.

Evaluation is restricted to the dirty subset. A large parameter set does not cost a full
re-evaluation on every recompute.

### 5.1 Cycle detection inside the stratum

Removing the object-level edges for these references makes the existing cycle check in
`PropertyExpressionEngine::validateExpression()` blind to them. `A.x = B.y` together with
`B.y = A.x`, both marked input, is a cycle nothing currently sees.

The input sub-DAG needs its own cycle check, run when an expression is set on an input
property. It is a small graph and the check is cheap.

## 6. Enforcement

The claim "no `execute()` writes this" is checked, not trusted.

The window is precise. `DocumentObject::recompute()` already wraps `execute()` and
`executeExtensions()` in `ObjectStatusLocker<ObjectStatus>(App::Recompute, this)`. The
enforcement window is "some object is inside that locker", not "this object is", and not
"the document is recomputing":

- the owner's own `execute()` writes the property -- error;
- a *different* object's `execute()` writes it -- error, because the eager and stratified
  guarantees both assume the value cannot move mid-pass;
- the input stratum writes it -- allowed, no object is inside `execute()`;
- the user or a script writes it outside recompute -- allowed.

A per-thread depth counter maintained alongside the existing locker gives that predicate
exactly.

On violation the property does two things: it latches the violation on the owning object,
and it throws `Base::RuntimeError`.

Both are needed. `AtomicPropertyChange::~AtomicPropertyChange()` calls `hasSetValue()`
inside a `try`/`catch(...)` that reports and swallows -- it must not throw from a
destructor -- so every list-type property and everything using the atomic-change pattern
would downgrade a bare throw to a console message. `Document::_recomputeFeature()` reads
the latch immediately after `Feat->recompute()` returns and converts it into a
`DocumentObjectExecReturn` error, so the violation surfaces as a hard recompute error on
the offending object no matter which write path swallowed the exception.

The check is value-based, matching the rest of our recompute model: it fires only when
`isSame()` reports an actual change. A touch that does not change the value is not a
violation of "the value will not change". To make this independent of the recompute
optimization preference, `aboutToSetValue()` takes the `copyBeforeChange()` snapshot for
an input property whenever the enforcement window is open, regardless of
`getOptimizeRecompute()`.

The error is unconditional. There is no parameter gate. The flag is new, so no existing
document is exposed, and a silent stale read is the failure mode the design exists to
eliminate. If an object is miscoded and misuses the flag, user code can clear the status
as an ad-hoc workaround -- which is possible precisely because the status is a runtime bit
rather than a static property type.

## 7. Changing the status

`Property::Input` is a runtime status bit, settable from Python and from the property
editor. Clearing it on a property that input-bound expressions reference invalidates their
closure retroactively.

Policy:

- The property editor issues a hard warning before clearing the status on a property that
  has referrers.
- Script and C++ callers may do it silently. FreeCAD's convention is that the GUI protects
  and the API trusts.

This is safe because the recompute-time enforcement of section 6 is the real backstop.
Misuse surfaces as a loud error at the next recompute, not as a quietly wrong model.
Answering "does this property have referrers?" for the warning costs nothing extra: it is
the same propagation record that step 4 of section 5 maintains.

## 8. Propagation and staleness

Within the stratum, ordering handles propagation. Outside it, step 4 of section 5 touches
non-input referrers between the two phases -- ordered, and before the object topological
sort, so no object can observe a mid-pass change.

Cross-document input dependencies are excluded in phase 2 of the implementation. Routing
them through the same path as hidden deps would set `LinkScope::Hidden` on the XLink and
lose the revision-based staleness check that ordinary cross-document dependencies get, so
a change made while the referring document was closed would not be detected on reopen.
The motivating case -- a container's parameters read by its own children -- is
same-document by construction, so this costs nothing now. Cross-document references keep
the ordinary edge and the revision check.

## 9. Coexistence with hiddenref and dbind

**Existing `hiddenref()` behaviour does not change.** No in-tree usage is migrated and no
semantics are altered. It keeps its own path: no backlink, no ordering, eager
`signalChanged` rebinding through `updateHiddenReference()`. Documents and scripts that
use it continue to work exactly as before.

The two mechanisms then divide cleanly by what they claim:

- **`Input`** -- enforced, ordered, stratified. The right answer when the target really is
  a parameter. Use it for new work.
- **`hiddenref()`** -- an explicitly unchecked escape hatch for reading a *computed* value
  without ordering. It remains the only option when the target is genuinely computed,
  which is a thing the input model cannot express by construction.

In-tree, `Mod/Spreadsheet`'s configuration tables (`DlgSheetConf.cpp`) are the case that
would map onto `Input`, and cross-sheet range binding (`DlgBindSheet.cpp`, target
`Spreadsheet2.cells`, a computed `PropertySheet`) is the case that never can. Neither is
being changed here.

`hiddenref()` also hides whole-object references --
`PropertyExpressionEngine::hasSetValue()` connects `slotChangedObject` when the dependency
names no property. `Input` is per property and has no object-level equivalent, which is a
second reason the hatch stays.

**`dbind()` stays outside the stratum.** Double binding implies hiding:
`FunctionExpression::_visit()` and `CallableExpression::_visit()` construct
`HiddenReference ref(ftype == HREF || ftype == HIDDEN_REF || ftype == DBIND)`, so a
`dbind` argument is collected as a hidden identifier. More importantly a double binding is
a cycle by construction -- A reads B and B is written back from A -- and a topologically
ordered stratum has nowhere to put that. It keeps the eager signal-driven fixed point it
has today, guarded by the existing `busy` re-entrancy flag. A `dbind` to an input property
is sound, because the write-back lands at signal time and never inside `execute()`, but it
does not join the ordered stratum.

`LinkScope::Hidden` on link properties is a different mechanism that shares the word
"hidden". It concerns a link property not creating an edge, not an expression reading a
property. It is out of scope and unaffected.

## 10. Relation to the out-of-process goal

The input stratum is pure: parameters only, no geometry, no OCCT. It resolves in-process
and is then frozen for the whole object phase.

That is exactly the property [ComputeBoundaries.md](./ComputeBoundaries.md) wants when
`execute()` is farmed out to separate processes. Inputs settled before dispatch means no
cross-process invalidation mid-pass, and the parameter layer -- which is cheap, chatty,
and latency-sensitive -- never has to cross the boundary at all.

## 11. API and persistence

`Property::Status::Input` takes bit 18, which was free between `UserEdit = 17` and
`PropStaticBegin = 21`.

There is deliberately no static `Prop_Input` property type. `Property::getType()`
reconstructs the type from the mirror bits `PropReadOnly` through `PropOutput` (21 to 27)
and `PropStaticEnd` is 28, which collides with `User1`; adding a mirror bit would mean
shifting `User1` through `User4`, and `PropertyContainer` persists `getStatus()` as an
integer, so old files carrying `User1` would read back as the new bit. Upstream took that
shift. We do not.

The consequence is that `Input` is settable at runtime only:

    part.setPropertyStatus("Length", ["Input"])

which is the same spelling upstream uses, and is what the use case needs -- a container's
parameters are user-added dynamic properties. `ADD_PROPERTY_TYPE(..., Prop_Input, ...)` is
not available. If a built-in property ever needs it, widen `StatusBits` to 64 and add the
mirror bit then.

Bit 18 was never written before, so old documents read as not-input, and new documents are
harmless on older builds, which ignore the bit.

`DocumentObject::isInputProperty(const Property*)` and the `std::string` overload match
upstream's signatures so their tests port with minimal edits.

## 12. What this does not change

- `hiddenref()`, `dbind()`, and `LinkScope::Hidden` semantics.
- The value-equality short-circuit and the revision mechanism. Both continue to do the
  heavy lifting for ordinary dependencies; input properties are a separate, narrow axis.
- The object-level dependency graph. No parallel property-level graph over all properties
  is introduced. The only new structure is the input sub-DAG and the propagation record,
  both scoped to input properties.
- Persistence of any existing property status.

## 13. Phasing

- **Phase 0 -- the flag.** `Property::Status::Input`, Python status name, and
  `DocumentObject::isInputProperty()`. No behaviour change.
- **Phase 1 -- enforcement.** The execute-depth counter, the snapshot in
  `aboutToSetValue()`, the latch-and-throw in `hasSetValue()`, and the conversion to a
  recompute error in `Document::_recomputeFeature()`. Testable on its own: marking a
  property input and having `execute()` write it produces a recompute error, and nothing
  else changes.
- **Phase 2 -- the stratum.** The input sub-DAG, the closure check, the in-stratum cycle
  check, the new recompute phase, the propagation record, and dropping the ordering edge
  for in-stratum references. Same-document only.
- **Phase 3 -- surface.** Property editor indication and the status-change warning.

## 14. Relation to upstream FEP-0010 phase 1

Upstream's PR 25603 introduces `Prop_Input` alongside a full property-level dependency
graph (`DepEdge`, `_inListProp`, `_outListProp`) maintained in parallel with the object
graph. We take the flag and not the graph.

Reasons, briefly:

- Their fine-grained propagation targets the case our value-equality short-circuit already
  covers, and covers less of it: they still recompute when a depended-on property is
  written with an unchanged value, and we do not.
- The parallel graph carries two `std::string` per edge and is rebuilt uncached whenever
  options are non-zero. On a large assembly that is real memory and a second traversal per
  recompute, against this fork's large-model goals.
- Their `Prop_Input` is unenforced. `isInputProperty()` is consulted in exactly four places
  upstream, none of which check that `execute()` honours the claim. That is the gap this
  design closes, and closing it is what makes the ordered stratum sound.
- Their rule permits the expression phase to write an input property. Combined with a
  dropped ordering edge that reintroduces the hazard the flag was meant to remove: an
  input property written during one object's recompute can be observed by another object
  that has already recomputed, and the two-pass net in `Document::recompute()` does not
  reliably catch it because the second pass restarts at the first touched index and does
  not revisit lower-indexed objects. Our stratum removes the hazard instead of racing it.
