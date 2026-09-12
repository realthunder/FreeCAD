# The proxy chain: document programs extend native objects

**[planned 2026-09-12; RULED 2026-09-12 on the five decisions, sec 5; the view list named `ViewProxyExp`; RULED "write the document first, start coding in next session" -- nothing built, P0 is the next session's first item]**

The user's design, stated 2026-09-12 after the document program (docs/
Sandbox.md 7.17) was found lacking against the spreadsheet-as-object
model (cells as attributes and methods, aliases as the exposed
interface, a copy as an instance, typing the missing piece):

> Check how FeaturePythonT works.  This is how fcad lets user python
> code extend fcad native objects.  I want to extend that to allow a
> document program to extend objects as well.  Use a plain XLinkList
> property (say ProxyExp), and add code in FeaturePythonT /
> ViewProviderFeaturePythonT to resolve at runtime overriding methods
> in the linked objects.  The code makes no assumption about the type
> of the linked object, only that it exposes a method fitting the
> naming and calling signature, e.g. expExecute(self, obj), where self
> is the linked object and obj is the overriding feature; check the
> return of expExecute to decide whether to call the next chained
> object -- an effect similar to multiple inheritance -- and the chain
> continues to the actual Proxy.  This makes the pattern agnostic to
> what can proxy a feature: user python code or a spreadsheet, or
> both, static or at runtime.  Also refactor the featurepython /
> viewproviderfeaturepython code with cog, since it is boilerplate;
> keep it efficient (the caching that is already there).

This file is the plan.  docs/Sandbox.md 7.21 points here; the variant
Link idea recorded the same day is docs/VariantLink.md, a parallel
thread.

## 1. What FeaturePythonT is today

`App::FeaturePythonT<FeatureT>` (`src/App/FeaturePython.h`, 399 lines;
`FeaturePython.cpp`, 708) adds one property, `Proxy`
(`PropertyPythonObject`), to any DocumentObject subclass and overrides
22 virtuals to consult it first.  `FeaturePythonImp` holds the
machinery:

- **The hook list** is an X-macro, `FC_PY_FEATURE_PYTHON`, expanded
  three times: `Py::Object py_<hook>` members, a `FlagCalling_<hook>` /
  `FlagAllowRecursive_<hook>` bitset, and the destructor's release.
- **The cache**: `init(pyobj)` runs once when `Proxy` changes
  (`onChanged(&Proxy)`), resolving every hook with `FC_PY_GetCallable`
  (`Base/Interpreter.h:59`, attribute present and callable) and reading
  `__allow_recursive_<hook>` beside it.  A hook call is then one
  `isNone()` test, one bitset test, one GIL lock and one `pyCall`.
- **Two calling forms**: `has__object__` (the old Proxy style with an
  `__object__` attribute, no `obj` argument) and the modern
  `hook(self, obj, ...)`.
- **The return protocol, per hook**: `execute` returns False or raises
  `NotImplementedError` for "not handled" and the template falls
  through to `FeatureT::execute()`; `hasChildElement`,
  `canLinkProperties`, `allowDuplicateLabel`, `redirectSubName` map a
  bool to `Accepted`/`Rejected` and absence to `NotImplemented`;
  `isElementVisible*`, `setElementVisible` use -2 for "not handled",
  `canLoadPartial` -1; `getSubObject`, `getSubObjects`,
  `getLinkedObject` decode a tuple; the notification hooks
  (`onBeforeChange`, `onChanged`, `onDocumentRestored`,
  `unsetupObject`) return nothing and are always followed by the base
  call.  Any other Python exception is reported, never propagated,
  except `execute`'s, which becomes the recompute error.
- **The Python side**: `FeaturePythonPyT<FeaturePyT>`
  (`FeaturePythonPyImp.h/.inl`) gives the object a `dict_methods`
  dictionary, so `obj.f = function` stores a method BOUND TO THE
  OBJECT, and `_getattr` searches it before the base type's attributes
  (properties included).

`Gui::ViewProviderFeaturePythonT<ViewProviderT>`
(`src/Gui/ViewProviderFeaturePython.h`, 740 lines; `.cpp`, 1483) is the
same design over 46 view hooks (`FC_PY_VIEW_OBJECT`): icon, tool tip,
claimChildren, the selection and picking hooks, edit modes, drag and
drop, display modes, restore notifications.  Its Imp holds the `Proxy`
by reference and the same `init`/cache/flags.

Instantiation count (grep, 2026-09-12): `FeaturePythonT<...>` 55
distinct bases, `ViewProviderFeaturePythonT<...>` 50 (each explicitly
instantiated in its module's `.cpp`; upstream's old names are aliases,
docs/UpstreamCoreSync.md sec 3 step 2c).  Every workbench's scripted
object goes through this code, so the refactor's gate is "nothing
observable changes": the full Python suite (2628) and C++ suite (453),
docs/Testing.md.

## 2. The chain

### 2.1 The property

`ProxyExp` (`App::PropertyXLinkList`) on `FeaturePythonT`, added in the
constructor beside `Proxy`, scope Global so a linked object may live in
another file.  The link property does the bookkeeping the design
needs for free: the linked objects are dependencies (a change there
recomputes the feature; the DAG orders them first), a deleted object
drops out of the list, and an external file loads on demand and
reports itself missing the way every XLink does.  A file saved before
the property exists restores with an empty list.

Two lists, BOTH on the App object **[RULED 2026-09-12, revised the
same day: first "separate list just like separate Proxy for feature
and view object", then "add another list property in App feature
python to hold view side exp proxy"]**: `ProxyExp` for the App hooks
and `ViewProxyExp` for the view hooks, both on `FeaturePythonT`, both
saved in `Document.xml`.  `ViewProviderFeaturePythonT` holds no list;
it reads its object's `ViewProxyExp` and resolves the view hooks on
those linked objects.  The App list never serves a view hook and the
view list never serves an App hook.

The names: `ProxyExp` for the App hooks, `ViewProxyExp` for the view
hooks **[RULED 2026-09-12: "Make the name ViewProxyExp"]**.

**Why on the App object, and what it avoids.**  Every link property
class assumes a `DocumentObject` container (`PropertyLinkList::
Restore` throws "Container is not a document object", `PropertyXLink::
Restore` asserts it, `PropertyXLink::setValue` refuses with "invalid
container", `PropertyLinks.cpp:4013`; no view provider in the tree
holds a link property), so a list on the view provider would have
meant teaching the link classes a view-provider owner (~60 lines and
a Gui-installed resolver).  On the App object the list is an ordinary
XLinkList: nothing in `PropertyLinks.cpp` changes.  Further: the
definition survives a headless round trip (a view-provider property
lives in `GuiDocument.xml`, written only when a Gui document exists);
every view provider of the object -- the primary and the split-view
secondaries this fork has (`SecondaryView`, docs/SplitViews.md) --
reads one list instead of each carrying a copy; and a scripted
`getViewProviderName` that picks a non-Python view provider simply
leaves the list unread, which is the same silent outcome `Proxy`'s
view hooks have today.

**The problems it brings, and the answer to each.**

1. *The dependency edge.*  An XLinkList on the App object makes its
   targets DEPENDENCIES of the feature (out-list, back-links, the
   recompute order).  For `ProxyExp` that is wanted: the type is
   recomputed before its instances and an edit to a method recomputes
   them.  For `ViewProxyExp` it is wrong twice over: a change to an
   icon method would mark the feature for a geometry recompute, and a
   view-extension sheet that reads the feature by name in a cell (a
   label from `Box.Length`) would close a cycle -- feature -> sheet
   through the list, sheet -> feature through the cell -- and the
   recompute refuses it.  The answer is the scope the link classes
   already have: `LinkScope::Hidden`, which keeps a link out of the
   out-list and the back-links (`PropertyLinks.cpp:861`, `:871`,
   `:1295`; the guards at `:705`, `:917`, `:968`) while still saving,
   restoring, loading an external file and breaking on delete.  A
   view-side link is a reference, not a dependency; the view provider
   already follows the object through `updateData`.  With Hidden
   scope the same sheet may reference the feature freely.  The
   Hidden scope is also why `copyObject` with dependencies does not
   drag the view sheet along -- the type is not duplicated -- whereas
   `ProxyExp`'s Global scope duplicates it as any dependency copy
   does; that asymmetry is stated in the doc string.
2. *A view-only change touching the feature.*  Setting or editing
   `ViewProxyExp` would touch the object and schedule a recompute.
   `Prop_NoRecompute` (`PropertyContainer.h:55`) is the existing flag:
   the property saves, undoes and notifies, and does not touch.
3. *Telling the view provider.*  A change to `ViewProxyExp` has to
   rebuild the view chain and, when the view `Proxy` is None, run the
   deferred attach (2.3).  No new signal is needed:
   `ViewProviderDocumentObject::updateData(prop)` receives every App
   property change (`ViewProviderDocumentObject.cpp:1386`), so the
   template's `updateData` tests `prop == &obj->ViewProxyExp` before
   the chain runs, as its `onChanged` tests `&Proxy` today.  At
   restore the App properties land before the view provider attaches,
   so the view provider's own `attach`/`finishRestoring` sees the
   restored list and attaches on it when the Proxy is None.  A view
   provider created for an object whose list was set earlier (a
   document opened in the GUI later, a secondary view) reads it at
   attach for the same reason.
4. *The invalidation of 2.4* gains one trigger on the view side:
   `updateData(&ViewProxyExp)`, beside the generation counter.
5. *The property editor* shows both lists in the Data tab, where a
   view-side list is unfamiliar; the doc string names its purpose,
   and the group is "Base" beside `Proxy`.  Undo covers both lists
   through the document transaction, which a view-provider property
   would have reached through the Gui document's own recording.

No problem was found that needs a change outside `FeaturePythonT`,
`ViewProviderFeaturePythonT` and their Imps.

### 2.2 Resolution: what "exposes a method" means

For a linked object `L` and a hook `H`, in this order **[RULED
2026-09-12: "Proxy first, like the rest"]**:

1. `getattr(L.Proxy, "exp" + Hook)` when `L` has a `Proxy` property
   holding an object -- a Python-scripted object that chooses to
   extend others, found the way `FeaturePythonImp::init` finds the
   object's own hooks.
2. else `getattr(L.pyobject, "exp" + Hook)` -- through the object's
   own Python wrapper.  This finds a dynamic property (a **spreadsheet
   alias** whose cell is a `lambda` or `def`: `Sheet.cpp:797`
   `setObjectProperty` exposes it as a `PropertyPythonObject`), a
   function stored on the object at runtime (`L.expExecute = f`,
   bound to `L` by `FeaturePythonPyT::_setattr`), or any callable
   attribute a C++ type provides.

A non-callable is skipped.  `L`'s type is never inspected: a Sheet, a
FeaturePython, an ExpressionLibrary (7.17 D2, once its module's
bindings are exposed as properties -- one small addition there), a
guest stand-in (rung 2), or a plain C++ object with a
`PropertyPythonObject` all qualify by the same rule.

**The names [elaborated 2026-09-12, decision 1].**  The property is
`ProxyExp` on both sides, the user's name (read "the Proxy
extensions"; it is a list of objects, not of expressions, and the
doc string says so).  A method is the hook's name with its first
letter upper-cased behind a prefix.  Two prefixes are needed even
with two lists: a linked object is ONE Python namespace, and three
hook names exist on both sides -- `onChanged` (a feature property
changed / a view property changed), `editProperty` and `attach` (the
view provider's `attach(vobj)` against `App::DocumentObject`'s
`attach`, not a FeaturePython hook today but a name the App side may
grow) -- so a sheet that extends both a feature and its view provider
must be able to say which `onChanged` it means.  The rule is uniform
rather than special-casing the three: App hooks are `exp` + Hook
(`expExecute`, `expMustExecute`, `expOnChanged`, `expGetSubObject`),
view hooks are `expView` + Hook (`expViewGetIcon`,
`expViewClaimChildren`, `expViewOnChanged`, `expViewAttach`).  The
alternative considered, a single `exp` prefix with the view side
reading `expOnChanged` from the VIEW list and the App side from the
APP list, would make the same object mean two things depending on
which list it sits in; rejected for that reason.  The prefix strings
are the one place the table of sec 3 spells them, so a rename later
is one line.

### 2.3 The call and the return

The callable receives what the Proxy hook receives in its modern
form: `(obj, *hook_args)`, with `obj` the overriding feature's Python
object (for a view hook, `vobj`).  What `self` is depends on the
binding the callable came with, exactly as the user stated it: a
Proxy method has `self = L.Proxy`; a function stored on `L` is bound
to `L`; a spreadsheet cell's callable has no `self` and sees the
sheet's cells as its frame, which is the sheet's own object model.
The `has__object__` form is the Proxy's only; a link is always called
with `obj`.

The return protocol per hook is the Proxy's, unchanged: the same
decoding, the same "not handled" signals (`NotImplementedError`
raised, or the hook's own value for it: `execute` False,
`isElementVisible` -2, ...), the same error reporting.  "Not handled"
moves to the next element of the chain; the chain is the linked
objects in `ProxyExp` order, then the `Proxy`, then the C++ base.  A
handled result stops the chain -- the multiple-inheritance effect the
user asked for, method resolution left to right.

**Notification hooks: the status today (decision 2, read 2026-09-12).**
These are the hooks whose Proxy call has no result the C++ side
checks.  Where the base call sits relative to the Proxy call, per
hook, as the templates have it:

    side  hook                 order today                 return    guard
    ----  -------------------  --------------------------  --------  ------------
    App   onBeforeChange       BASE first, then Proxy      ignored   isNone only
    App   onChanged            Proxy, then base            ignored   isNone only
    App   onDocumentRestored   Proxy, then base            ignored   FlagCalling
    App   unsetupObject        Proxy, then base            ignored   FlagCalling
    App   setupObject          base, then Imp (a no-op)    --        --
    View  attach               deferred: the template only records
                               pcObject; the first onChanged(&Proxy)
                               with a Proxy set calls Proxy attach,
                               then base attach, then touches Label,
                               restates DisplayMode, updateView
                                                           ignored   FlagCalling
    View  updateData           Proxy, then base            ignored   isNone only
    View  onChanged            (the Proxy-swap block), then
                               Proxy, then base            ignored   isNone only
    View  startRestoring       base, then Imp (a no-op)    --        --
    View  finishRestoring      Proxy, then base; a None
                               Proxy shows the object and
                               sets Proxy = 1 instead      ignored   FlagCalling

Two hooks look like notifications but are value hooks and stay in
the value protocol: App `onBeforeChangeLabel` (a returned string is
the new label and the base is skipped; None falls through) and App
`getElementMapVersion` (the base computes, the Proxy may replace the
string).  `execute` is a value hook (False or NotImplementedError
means not handled).

**The chain rule for notifications, proposed for the ruling:** every
element is called in chain order -- the links, then the Proxy -- and
a link returning True stops the chain before the Proxy (an explicit
"consumed"; None, the usual return, continues).  The base call keeps
its per-hook position from the table (before, for `onBeforeChange`;
after, for the rest).  The view `attach` deferral stays as it is:
the chain's `expViewAttach` calls happen where the Proxy's does, at
the first `onChanged(&Proxy)`, and ALSO at the first `onChanged(
&ProxyExp)` when the Proxy is None -- a view provider extended by a
sheet and nothing else must still attach.  `finishRestoring`'s
None-Proxy branch (show, `Proxy = 1`) runs only when the chain is
empty too.

Recursion guards: the per-hook `FlagCalling` bit covers the whole
chain (one entry per hook, as now); `__allow_recursive_<hook>` is read
from the object the callable was found on, per element.

### 2.4 The cache, and when it is stale

The chain is resolved once per hook and cached: a small vector of
`Py::Object` in chain order, built lazily on first use.  The Proxy's
own `py_<hook>` stays as it is and is the last element, so an object
with an empty `ProxyExp` runs exactly today's code behind one
`empty()` test -- zero cost for the objects that do not use the
feature, which is nearly all of them.

Invalidation:

- `ProxyExp` changed (`onChanged`), `Proxy` changed (`init`, as now),
  `onDocumentRestored` (links resolve late; nothing is resolved during
  restore -- lazy build guarantees it, the explicit invalidate makes it
  obvious).
- A linked object's DEFINITION changed under a feature that did not:
  a cell re-typed on the sheet, a Proxy replaced on the linked object,
  a function stored or deleted on it.  No signal reaches the feature
  for those, and a per-feature observer on every linked object is too
  heavy for thousands of features.  The design is a process-wide
  **generation counter** (`App::ProxyChain::generation()`), bumped
  from the three places a definition can change --
  `PropertyPythonObject::hasSetValue`, `PropertySheet::hasSetValue`,
  `FeaturePythonPyT::_setattr` storing into `dict_methods` -- and
  stored in the cache when it is built.  A hook call compares one
  integer; a mismatch rebuilds that feature's chains on the next use.
  Bumps are rare (an edit) against hook calls (thousands per tree
  repaint), so the compare is the whole steady-state cost.

Cost of a resolved chain call: one vector walk over `n` links plus
the Proxy, each element the same `pyCall` as today.

### 2.5 The sandbox

Where a linked object's callable is an `ExpressionPy` (a cell `def`
or `lambda`, `Expression.cpp:6541 makeFunc`) or a guest stand-in's
method (`ExpressionGuestProxy.h`, `gmethod`), the call is already
what the sandbox does for a Proxy hook: `obj` crosses as a handle,
the code runs under the principal of the document whose text it is
(the linked object's document: 1.5's taint rule, and what 7.17 (c)
does for a library's module), a same-document write to `obj` is
`doc.write.self` (3.2's same-origin rule), a cross-file one is the
`doc.foreign` prompt, once.  Routed evaluation of a `def` cell needs
7.17 D1 (function objects in the image); natively it works today.

This is how "the document program extends objects" lands without a
class in the expression language: a sheet, or a library, carries the
methods; the feature links to it; the chain calls them.  7.17's
carrier question (a text library versus a typed sheet) becomes a
choice of what to link, and both work through one mechanism.  Typing,
the piece the user had not found: the `ProxyExp` link IS the type --
instances share the definition by linking to it, a cell added to the
sheet reaches every instance, and a sheet may itself link onward.

## 3. The refactor: functions first, cog for the table only

**[RULED 2026-09-12: "do not abuse cog, only duplicate generated code
if it cannot be efficiently done by a function"; "generated at build
time, we'll migrate other cog generated param code in the future and
this is a start"]**

### 3.1 What a function can do

Both Imp classes are one pattern per hook: call check, GIL lock, an
args tuple with `obj` first, `pyCall`, a result mapping, the
`NotImplementedError` catch, the error report.  That pattern is a
C++ template, not generated text:

    // one entry per hook, in the Imp: the chain of resolved callables
    // (links in ProxyExp order, then the Proxy's), the two flags
    struct HookSlot { std::vector<Py::Object> chain; Py::Object proxy; ... };

    // the call: marshal C++ arguments to a tuple (toPy overloads for
    // const char*, int, bool, double, DocumentObject*, Matrix4D, a
    // vector of strings), walk the chain, decode each result with
    // `decode` (returns "handled" or not), report or rethrow per the
    // hook's exception policy
    template <class Decode, class... Args>
    bool callHook(Hook hook, Decode&& decode, Args&&... args) const;

with a handful of decoders shared by many hooks (`decodeHandledBool`,
`decodeValueT`, `decodeIntSentinel(-2)`, `decodeNotify`) and the
irregular hooks (`getSubObject`'s tuple, `getIcon`'s pixmap,
`getDetailPath`, `dropObjectEx`, ...) passing their own lambda.  The
`has__object__` form is a flag `callHook` reads to drop `obj` from
the tuple for the Proxy element only.  The regular Imp bodies become
one line each; the irregular ones keep their decoding and lose their
boilerplate; the template overrides in the two headers stay
hand-written -- they are the C++ signatures, and a function cannot
write a signature.

What stays as it is: the per-hook cache (now the slot's `chain` plus
the Proxy's `Py::Object`), the bitset guards, one GIL lock per call,
`__allow_recursive_`, `FeaturePythonPyT` (untouched but for the
generation bump of 2.4).

### 3.2 What only a generator can do

The hook TABLE: the enum of hooks, the flag bits, the `py_` /
`HookSlot` members, the Proxy attribute names and the two prefixed
extension names per hook (`"execute"`, `"expExecute"`;
`"getIcon"`, `"expViewGetIcon"`), and `init()`'s resolution loop
over them.  Today that is the `FC_PY_ELEMENT` X-macro expanded three
times; a generator emits it once from `src/App/FeaturePythonHooks.py`
(the table: name, side, the `__object__` form, the exception policy)
into a small include per side, and NOTHING else.  Rough size: the
table ~80 lines, the two generated includes ~150 lines each, against
~900 lines of pattern bodies that become ~150 lines of template.

### 3.3 Build-time generation, the first of its kind

Generated in the build tree, never committed, as `generate_from_xml`
does (`cMake/FreeCadMacros.cmake:177`): a macro `generate_from_cog(
<template> <output>)` beside it that runs `${PYTHON_EXECUTABLE} -m
cogapp -d -o <build>/<output> <template>` as a custom command with
the template and the `.py` table as dependencies, plus the
`execute_process` at configure time that assures the file exists
before the first build.  The template is a `.cog.h` file holding the
`[[[cog ]]]` block; the generated header lands in
`${CMAKE_CURRENT_BINARY_DIR}` and the hand-written headers include
it by name.  `cogapp` becomes a configure-time requirement: `find`
it with `${PYTHON_EXECUTABLE} -c "import cogapp"` and fail the
configure with the install line when missing (`pip install cogapp`
into `.conda/freecad`; it is in neither Python on this box today).
The feedstocks (`freecad-rt-feedstock`, and the Windows build) add
`cogapp` to their build requirements.  This is the start of the
migration the user named: the 47 files with in-place `[[[cog`
blocks (`PartParams`, `SketchObjectPyImp`, ...) move to the same
macro one at a time later, their generated output leaving the tree.

## 4. Stages

    P0  the refactor: callHook and the decoders (3.1), the regular
        bodies collapsed onto it, the irregular bodies on it with
        their own decoders; the hook table and generate_from_cog
        (3.2, 3.3) replacing the X-macros, cogapp installed in the
        env and required by the configure; no ProxyExp yet.  Gate:
        both suites green; a scratch diff of the calls each hook
        makes (a Python Proxy logging every hook, native, before and
        after) identical.                       one session
    P1  ProxyExp and the chain on the App side: the property, the
        resolution of 2.2, the walk of 2.3 in every App hook, the
        cache and the generation counter of 2.4.  Gate: a new Test
        module `FeaturePythonChain` -- a FeaturePython with no Proxy
        and a Sheet in ProxyExp whose alias cells `expExecute` /
        `expOnChanged` are lambdas: the sheet's execute runs and its
        onChanged sees the property name; a Python-scripted object as
        the link, its Proxy's expExecute called with (obj); two links,
        the first raising NotImplementedError, the second handling;
        the Proxy last; a cell re-typed on the sheet is picked up on
        the next recompute (the generation counter); save, reopen,
        the chain restored; a link in another file (XLink), pinned
        by the file's own machinery; `ProxyExp` emptied returns the
        object to today's path.        one session
    P2  the view side: ViewProxyExp on FeaturePythonT (Hidden scope,
        Prop_NoRecompute), read by ViewProviderFeaturePythonT through
        updateData, the same walk in the view hooks, expViewAttach
        at the deferred attach.  Gate: getIcon, claimChildren,
        getToolTip and onChanged from a sheet, in
        `FeaturePythonChain`'s GUI half; a sheet in ViewProxyExp
        reading the feature by name in a cell (no cycle, no touch);
        the list saved headless, reopened in the GUI, an external
        file resolving.                                  one session
    P3  the sandbox: the P1 gate routed (needs 7.17 D1 for a `def`
        cell); the audit line names the linked object's document; a
        cross-file link's write prompts doc.foreign once.  Sits
        inside 7.17's build, not before it.

Lines: P0 the template and decoders ~150, the table ~80, the two
generated includes ~300 (in the build tree), the macro ~40, against
~900 lines of pattern bodies removed; P1 ~250 (the property, the
resolver, the cache, the counter, three bump sites) plus ~200 of test;
P2 ~120 (the view list and its updateData hook) plus ~100 of test.

## 5. The rulings (2026-09-12)

1. `ProxyExp` on both sides; `exp` + Hook for App hooks, `expView` +
   Hook for view hooks -- elaborated in 2.2.
2. Notification hooks: the status today is the table in 2.3; the
   rule proposed there (every element, a True stops before the
   Proxy, the base keeps its position) is the plan's default, not
   overruled when the document was closed for coding.
3. **Separate lists**, `ProxyExp` and `ViewProxyExp`, BOTH on the
   App object (revised the same day); the view list Hidden scope and
   Prop_NoRecompute, the problems and answers in 2.1.
4. **Proxy first** on the linked object, then its own attributes --
   "like the rest".
5. **Generated at build time** -- the first `generate_from_cog`
   user; the in-place cog files migrate later (3.3).  And cog is for
   the table only; the bodies are a function (3.1).

## 6. What this changes in docs/Sandbox.md 7.17

D1 stands and is the prerequisite for the routed case.  D2's carrier
is no longer the only way a document supplies methods: a sheet with
alias'd callables is one today, natively, and the chain makes it an
object's extension.  The typed-sheet discussion of 2026-09-11 (a
`Type` link and prototype delegation) is subsumed: `ProxyExp` is that
link, and delegation is the chain.  7.17 is re-sized after P1, with
the flange example written as a sheet-extended `Part::Feature` beside
the library form.
