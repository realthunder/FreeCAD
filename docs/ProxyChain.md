# The proxy chain: document programs extend native objects

**[planned 2026-09-12; RULED 2026-09-12 on the five decisions, sec 5; the view list named `ViewProxyExp`; **P0 BUILT 2026-09-12**, sec 4.1 -- the refactor and the generator; **P1 BUILT 2026-09-12**, sec 4.3 -- `ProxyExp` and the App-side chain, 17 gate cases in `src/Mod/Test/FeaturePythonChain.py`; **P2 BUILT 2026-09-13**, sec 4.4 -- `ViewProxyExp` and the view-side chain, 17 gate cases in `src/Mod/Test/ViewProviderChain.py`; **sec 4.5, 2026-09-13** -- what P1 does not deliver: an edited method does not recompute its instances; RULED the same day (fine-grained per-cell, never the coarse counter), with the function-body dependency trap recorded there; **BUILT 2026-09-13**, sec 4.6, as 7.17 D2's first item, 8 gate cases; P3, the sandbox, sits inside 7.17's build]**

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
        after) identical.                  DONE 2026-09-12, sec 4.1
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
        object to today's path.            DONE 2026-09-12, sec 4.3
    P2  the view side: ViewProxyExp on FeaturePythonT (Hidden scope,
        Prop_NoRecompute), read by ViewProviderFeaturePythonT through
        updateData, the same walk in the view hooks, expViewAttach
        at the deferred attach.  Gate: getIcon, claimChildren,
        getToolTip and onChanged from a sheet, in
        `FeaturePythonChain`'s GUI half; a sheet in ViewProxyExp
        reading the feature by name in a cell (no cycle, no touch);
        the list saved headless, reopened in the GUI, an external
        file resolving.                     DONE 2026-09-13, sec 4.4
    P3  the sandbox: the P1 gate routed (needs 7.17 D1 for a `def`
        cell); the audit line names the linked object's document; a
        cross-file link's write prompts doc.foreign once.  Sits
        inside 7.17's build, not before it.

### 4.1 P0 as built (2026-09-12)

The shape the plan asked for, with four things the audit of the 68 bodies
turned up that the plan had not anticipated.

**The files.**  `src/App/FeaturePythonHooks.py` is the table (22 App hooks,
46 view hooks, four facts each).  `src/App/FeaturePythonHook.h/.cpp` is the
machinery: `App::PyHookImp`, the base both Imps now derive from, holding the
resolved slots, `init()`, `canCallHook()` and the `callHook()` template, plus
the `pyHookArg` marshalling overloads and three shared decoders
(`pyHookDecodeNotify`, `pyHookDecodeValueT`, `pyHookDecodeInt`).  The two
templates `FeaturePythonHookApp.cog.h` and `src/Gui/FeaturePythonHookView.cog.h`
emit the hook enum and the `PyHookDef` table into the BUILD tree; nothing
generated is committed.  `generate_from_cog()` sits beside `generate_from_xml`
in `cMake/FreeCadMacros.cmake`, and `SetupPython` now fails the configure when
`cogapp` is missing.

Net: `FeaturePython.cpp` 708 -> 440 lines, `ViewProviderFeaturePython.cpp`
1483 -> 887, against ~330 lines of new machinery and ~170 of table -- and the
two headers lost the X-macro blocks (`FC_PY_ELEMENT_DEFINE`,
`FC_PY_ELEMENT_INIT`, `FC_PY_ELEMENT_FLAG`, `_FC_PY_CALL_CHECK` are gone from
the tree; `SelectionObserverPython.h` defines its own `FC_PY_ELEMENT` and was
never a user of these).

**Four facts the table had to carry that sec 3.2 did not list.**

1. *Where the owner goes* is not one rule but three, and the view side is the
   irregular one.  `PyHookSelf::None` -- the callable never receives the owner
   -- covers 28 of the 46 view hooks (`getIcon`, `claimChildren`, `isShow`,
   `canDropObjectEx`, ...), because a view Proxy method's own `self` IS the
   proxy and upstream never settled on passing `vobj` as well.  `Modern` is
   the familiar "owner first, dropped in the `__object__` form".  `Always`
   passes it in both forms.  The App side has one `None` hook, `editProperty`,
   which takes only the property name.
2. *Not every hook is recursion-guarded, and guarding them all would be a
   bug.*  Five hooks -- App `onBeforeChange`, `onBeforeChangeLabel`,
   `onChanged`, view `updateData`, `onChanged` -- check only whether the
   callable is absent.  They have no `FlagCalling` bit because a nested call
   is their normal case: setting property B inside `onChanged(A)` re-enters
   `onChanged`, and a guard would silently drop it.  The table carries a
   `guarded` flag for this and `callHook` honours it.
3. *"Failed" is a third answer, not a shade of "not handled".*  A reported
   Python error means something different per hook from what
   NotImplementedError means: `getSubObject` answers "handled, with a null
   object", `isElementVisible` answers -1 where "not handled" is -2,
   `canLoadPartial` 0 against -1, `useNewSelectionModel` and
   `canAddToSceneGraph` answer Accepted where absence answers NotImplemented.
   So `callHook` returns `PyHookState{NotHandled, Handled, Failed}` and the
   body maps Failed itself.  Three error policies ride in the table:
   `Report` (48 App + 43 view hooks), `ReportThrow` (App `execute`, view
   `canDropObjectEx`) and `Throw` (App `skipRecompute`, view `dropObject`,
   `dropObjectEx`).
4. *One hook's owner is a different object.*  View `updateData` passes the
   DOCUMENT OBJECT as its first argument, not the view provider.  That is why
   `hookSelf(int hook)` is a virtual taking the hook rather than a stored
   object: the Gui override answers with `object->getObject()` for that one
   enumerator and the view provider for the rest.

**Two behaviour changes, both deliberate.**

- `iconMouseEvent` was broken and now works.  It built a one-element tuple and
  then wrote a second item into it, so every call raised IndexError, was
  reported, and returned Rejected -- the hook had never reached a Python view
  provider.  It is now the two arguments it always meant: `(event, tag)`.
- `mustExecute` no longer reports a NotImplementedError.  It was the one hook
  without the NotImplementedError catch, so raising it there printed an error;
  `callHook` treats NotImplementedError as "not handled" everywhere, which is
  the protocol sec 2.3 states.  The answer the caller gets is unchanged.

Three pre-existing oddities were preserved rather than fixed, since the gate
is "nothing observable changes": `getExtraIcons` still drops a bare
`(tag, pixmap)` pair on the floor (it fills a local and returns without
assigning); `getDetailPath` still deletes a null pointer on one branch; and
`ViewProviderFeaturePythonT::canReorderObject` called `imp->canReplaceObject`.
The last was a real bug and got its own commit, below.

**The gate.**  Python 2688 tests OK (0 failures, 0 errors, 50 skipped, 6
expected failures) and C++ 605/605, both matching the pre-change baseline of
`docs/Testing.md`.  The suites run headless, so they do not touch the view
side at all; the view half was checked with a scratch Gui session
(`QT_QPA_PLATFORM=offscreen`) driving a view Proxy that logs every hook, which
confirmed the argument shape of each: `getIcon()` with none, `dragObject(vobj,
obj)`, `dropObjectEx(vobj, obj, owner, subname, elements)`,
`canDropObjectEx(obj, owner, subname, elements)`, `getDisplayModes(vobj)` and
`updateData(obj, prop)` with the document object.  The App half was checked
the same way under `FreeCADCmd`.

### 4.2 The reorder query asked the wrong hook (fixed 2026-09-12)

`ViewProviderFeaturePythonT::canReorderObject` switched on
`imp->canReplaceObject(obj, before)`, a copy-paste from the
`canReplaceObject` override directly above it. It arrived with 638f3bbf20
(2021-12-05), the commit that added drag-and-drop reordering, so it had
never worked.

Two consequences, and the second is worse than the first. A scripted view
provider's `canReorderObject` was dead: the Imp method is fully implemented
and resolved on the Proxy at init, and nothing called it. And
`canReplaceObject` answered in its place, with a reorder's arguments -- a
Proxy answering true there silently authorised reorders it was never asked
about, false blocked them.

It guards the real operation, not only the drag preview: `Tree.cpp:3279`
asks it to decide whether a drag lands as a reorder, and
`ViewProvider::reorderObjects` (`ViewProvider.cpp:877`) loops over it as the
precondition before performing one. Note the asymmetry that hid it -- the
verb was wired correctly, `reorderObjects` does call `imp->reorderObjects`,
so only the permission question was misrouted.

Nothing in the tree could see it. No bundled Python view provider defines
either hook, so the Imp returned NotImplemented either way and the C++ base
answered, which is what a correct call also produces. It was live only for
out-of-tree workbenches and addons, which is presumably why it sat four
years.

The fix is one identifier. The cover is new: `src/Mod/Test/ViewProviderHooks.py`,
run through `scripts/sandbox-gui-gate.py` (docs/Testing.md sec 1), which is
the first test in the tree to touch `ViewProviderFeaturePythonImp` at all --
both suites are headless and reach none of it. Eight cases: three on the
reorder and replace queries, five pinning the three `PyHookSelf` forms of
sec 4.1, including `updateData`'s document object. Verified against a build
with the bug restored: the two reorder cases fail there (the query returns
false, and a `canReplaceObject`-only Proxy answers true) and the other six
pass, so the module guards the refactor as well as the fix.

### 4.3 P1 as built (2026-09-12)

The App-side chain, and five things the build had to settle that sec 2 left
open.

**The files.**  `src/App/FeaturePython.h`: `ProxyExp`, an
`App::PropertyXLinkList` beside `Proxy`, Global scope, group "Base", its doc
string naming the naming convention and the dependency it creates; the
template hands the list to the Imp from `onChanged(&ProxyExp)` and again from
`onDocumentRestored` (the links resolve late, so the list means nothing
before that).  `src/App/FeaturePythonHook.h/.cpp`: the chain itself --
`HookSlot` grew a `std::vector<ChainEntry>`, `setHookExtensions()` takes the
list, `resolveChain()` is the resolution of sec 2.2 and `ensureChain()` the
invalidation of sec 2.4, and `callHook` walks the chain before the Proxy.
`App::ProxyChain::generation()` / `bump()` is the process-wide counter, bumped
from the three places sec 2.4 named: `PropertyPythonObject::hasSetValue` (new
override), `PropertySheet::hasSetValue` (one line at the top) and
`FeaturePythonPyT::_setattr` where it stores into or deletes from
`dict_methods`.

Net: ~190 lines of machinery, ~20 of property and wiring, 3 bump sites, and
`src/Mod/Test/FeaturePythonChain.py` (~380) for the gate.

**The gate.**  `FreeCADCmd -t FeaturePythonChain`, 17 cases, every item P1
asked for: the property and its dependency edge; a scripted object's Proxy,
a function stored on a plain object and a spreadsheet whose alias cells are
lambdas, all resolving through the one rule (and the Proxy asked first when
an object offers both); two links with the first declining and the second
handling, the Proxy spared; every link declining and the Proxy answering; a
value hook's sentinel reaching the C++ base; a notification reaching every
element; the generation counter through a replaced Proxy, a replaced stored
function and a re-typed cell, and through a cleared cell, whose property is
removed rather than set (that one rides on `PropertySheet::hasSetValue`); a
deleted link dropping out; save and reopen; an XLink into another file; and
`ProxyExp` emptied returning the object to the P0 path.  Both suites stay at
their `docs/Testing.md` baseline.

**Five things sec 2 left open.**

1. *The notification rule needs a fifth generated fact.*  Sec 2.3's rule --
   every element called, a True stopping the chain before the Proxy -- cannot
   be carried by the decoder, which is where the protocol lives for every
   other hook.  `pyHookDecodeNotify` is used by three hooks that are NOT
   notifications and whose callers DO read the answer: view `attach` (a call
   means "touch Label"), `dragObject` and `dropObject` (a call means
   Accepted).  Teaching that decoder to answer "not handled" would have
   changed all three.  So the table carries `notify`, true for the nine hooks
   of sec 2.3's table (App `onBeforeChange`, `onChanged`,
   `onDocumentRestored`, `unsetupObject`; view `attach`, `updateData`,
   `onChanged`, `startRestoring`, `finishRestoring`), and `callHook` reads it:
   those walk every element and stop only on an explicit True from a link.
   With an empty chain a notify hook behaves exactly as it did.
2. *A chain element is always told which object it is extending.*  Sec 2.3
   says the callable receives `(obj, *hook_args)`, but 29 of the 68 hooks are
   `PyHookSelf::None` -- their Proxy method is passed nothing, its own `self`
   being the proxy.  A ProxyExp element is not the proxy: it is a separate
   object serving possibly many features, and without the owner it cannot do
   anything at all.  So `callHook` passes the owner to every chain element
   regardless of the hook's `self` rule, and honours `PyHookSelf` (and the
   `__object__` form) for the Proxy element only.  On the App side this
   affects one hook, `editProperty`, whose extension form is therefore
   `expEditProperty(obj, propName)` against the Proxy's
   `editProperty(propName)`; on the view side it will be the difference
   between `getIcon()` and `expViewGetIcon(vobj)`.
3. *The walk has to survive what it calls.*  A hook's callable may do
   anything, including something that rebuilds the very chain it is walking
   (store a function on a linked object, re-type a cell -- both bump the
   generation).  The walk therefore indexes rather than iterates and copies
   the entry before the call, which is one reference count against a Python
   call; and `~PyHookImp` clears the chains under the GIL, beside the Proxy
   slots, because a vector of `Py::Object` destroyed with the rest of the
   object would drop its references without it.
4. *`canCallHook` had to learn the chain.*  The bodies with work to do before
   a call -- a matrix, a pivy pointer, a property name -- ask it first, and it
   answered from the Proxy alone; an object extended by a sheet and carrying
   no Proxy would have been turned away before the chain was ever consulted.
   It now resolves the chain and reports whether any element is callable.
5. *A sentinel "not handled" has to be known by the decoder, not only by the
   caller.*  Four App hooks answer "not mine" with a number rather than an
   exception -- `isElementVisible`, `isElementVisibleEx`, `setElementVisible`
   with -2, `canLoadPartial` with -1 -- and the templates in
   `FeaturePython.h` are what read it.  Inside a chain that is too late: the
   shared `pyHookDecodeInt` reported every integer as handled, so the FIRST
   link returning -2 stopped the walk and the hook fell to the C++ base with
   a later link never asked.  The gate case caught it.  `pyHookDecodeInt(out,
   notHandled)` now takes the sentinel and the caller keeps reading the
   number, unchanged.

**The cost of not using it.**  An object with an empty `ProxyExp` -- which is
every scripted object in every existing file -- pays one `empty()` test per
hook call, then runs the P0 path unchanged.  Nothing is resolved, no
generation is compared, and no vector is touched.

### 4.4 P2 as built (2026-09-13)

The view-side chain, and five things the build had to settle -- two of them
latent bugs in code older than this work.

**The files.**  `src/App/FeaturePython.h`: `ViewProxyExp`, a second
`App::PropertyXLinkList` beside `ProxyExp`, group "Base",
`App::Prop_NoRecompute`, `LinkScope::Hidden`, its doc string naming the
`expView` convention and the asymmetry with `ProxyExp` (a reference, not a
dependency; a copy with dependencies does not take it along).  The App
template does nothing else with it -- the view provider hears about it the
way it hears about every other App property.
`src/Gui/ViewProviderFeaturePython.h/.cpp`: `readHookExtensions()` on the Imp
reads the list off the object and hands it to `PyHookImp::setHookExtensions`,
`isHookExtensionProperty()` answers whether a property IS that list, and the
template pulls at the three moments the list can become real -- `attach`
(the object may already carry one: a document opened in the GUI, a secondary
view), `updateData` when the property is the list, and `finishRestoring`
(the links resolve late).  The deferred attach came out of `onChanged(&Proxy)`
into `attachDeferred()` and now has a second caller.
`src/App/PropertyLinks.cpp`: finding 2 below.

Net: ~20 lines of property, ~45 of view-provider machinery, 16 in
`PropertyLinks.cpp`, and `src/Mod/Test/ViewProviderChain.py` (~390) plus one
headless case in `FeaturePythonChain.py`.

**The gate.**  17 cases in `ViewProviderChain`, run with `ViewProviderHooks`
through `scripts/sandbox-gui-gate.py` (`RESULT OK`, 25 of 25): the property
and the two things Hidden scope buys (no edge either way, no touch); an icon,
a list, a string and a notification hook answered from an extension, each
told which view provider it is extending; chain order with the first element
declining; every element declining and the view Proxy answering; the empty
list running the P0 path; `ProxyExp` never answering a view hook; the
deferred attach for an object with no view Proxy at all, pinned by
`expViewAttach`; save and reopen; an XLink into another file; and a
spreadsheet whose alias cell is a lambda, re-typed and picked up through the
generation counter, plus one reading the feature back by name in a cell --
the case Hidden scope exists for.  Both suites are at their `docs/Testing.md`
baseline: Python 2706 OK (2705 plus the new headless case), C++ 605 of 605.

**Five things the build had to settle.**

1. *`Prop_NoRecompute` does not stop the touch.*  Sec 2.1 problem 2 reads it
   as "the property saves, undoes and notifies, and does not touch", and that
   is not what the flag does: `DocumentObject::onChanged` (`:1057`) tests it
   only to withhold `ObjectStatus::Enforce`, several lines after it has
   already set `ObjectStatus::Touch`.  For a `FeaturePythonT` that is the
   whole game -- its `mustExecute()` answers 1 on `isTouched()` -- so an edit
   to the icon list would have recomputed the geometry after all.  The flag
   that withholds the touch is the `Property::Output` STATUS, and
   `DocumentObject` already answers this exact question that way for
   `Visibility` (`DocumentObject.cpp:101`).  `ViewProxyExp` carries both: the
   type flag says what it is, the status makes it so.

2. *Hidden scope did not reach the elements of a list.*  `LinkScope::Hidden`
   is read by the element, not by the list: `PropertyXLinkSubList` keeps a
   `PropertyXLinkSub` per link, and `setValue`, `restoreLink` and `resetLink`
   each consult their OWN `_pcScope` before adding the back-link.  Children
   are built with `new PropertyXLinkSub(allowPartial, this)` and the
   constructor copied only the container, so a Hidden list still left the
   feature in every extension's `InList` -- and `Document::recompute`
   (`Document.cpp:4388`) walks exactly that list to set `Enforce` on
   everything referencing what it just rebuilt.  The dependency the Hidden
   scope was chosen to avoid would have arrived by the back door.  The fix is
   in the child constructor and inherits Hidden alone (Local against Global
   on a child is read by nothing, and copying it would be a change with no
   caller).  Nothing in the tree could have seen this: no `PropertyXLinkSubList`
   or `PropertyXLinkList` had ever been given a non-default scope -- the four
   Hidden links that exist are single `PropertyXLink`s, and
   `PropertyXLinkContainer` sets Hidden on each of its child xlinks by hand
   (`PropertyLinks.cpp:5876`), which is the same lesson written out the long
   way.

3. *A Hidden list reads back EMPTY through `getValues()`.*  `PropertyXLinkSubList::
   getValues()` is `getLinks(objs)` with `all` defaulted to false, and false
   is precisely the argument Hidden scope answers with nothing.  So the first
   build resolved a chain of zero elements for every object while Python
   showed the list correctly -- the Python getter takes another path -- and
   every view hook fell through to the Proxy as if nothing had been linked.
   The read is `getLinks(objs, true)`; `ProxyExp` is Global and never had the
   problem.  This is the trap for anyone who gives a link list Hidden scope
   next.

4. *An element is always passed the owner -- and for `updateData` the owner is
   the document object.*  Sec 4.3 finding 2 made the chain pass the owner even
   for the 28 view hooks whose Proxy method is passed nothing, so the view
   forms are `expViewGetIcon(vobj)`, `expViewClaimChildren(vobj)`,
   `expViewGetDropPrefix(vobj)` against the Proxy's no-argument `getIcon()`.
   `hookSelf` decides what that owner is, and it is not overridden for the
   chain: `expViewUpdateData` receives the DOCUMENT object, exactly where the
   Proxy's `updateData` receives it (sec 4.1 finding 4).  An element gets what
   the Proxy would get in that position, which is the rule that keeps the two
   forms readable side by side.

5. *The attach the template defers needed a second trigger, and
   `finishRestoring` had to stand down.*  A view provider with no Proxy never
   attaches: `attach(obj)` only records the object and the real attach waits
   for the first `onChanged(&Proxy)`.  An object extended by a sheet alone has
   no Proxy to wait for, so `attachDeferred()` also runs from
   `updateData(&ViewProxyExp)` once the list is non-empty.  And
   `finishRestoring`'s None-Proxy branch -- show the object, set `Proxy = 1`,
   which pokes `onChanged` into attaching -- would have invented a Proxy for
   an object that deliberately has none; it now runs only when the chain is
   empty, and the template attaches such an object itself.  Restore is where
   this matters, and the fork restores view providers lazily (the drain in
   `Gui/Document.cpp`), so a reopened document has no view provider at all
   until the event loop turns: the two persistence cases call
   `FreeCADGui.updateGui()`, and it is `finishRestoring` that then supplies
   the chain.

**Two notes for the next reader.**  `getToolTip`, the string hook the plan
named for the gate, is not reachable from Python -- no `ViewProviderPy`
method exposes it -- so the gate pins the string protocol through
`getDropPrefix`, which is the same decoder and is exposed as `DropPrefix`.
And an extension added to a view provider that has ALREADY attached does not
receive `expViewAttach`: re-running the attach would run the Proxy's a second
time.  Set the list with, or before, the Proxy.

Lines: P0 the template and decoders ~150, the table ~80, the two
generated includes ~300 (in the build tree), the macro ~40, against
~900 lines of pattern bodies removed; P1 ~250 (the property, the
resolver, the cache, the counter, three bump sites) plus ~200 of test;
P2 ~80 (the view list, the view provider's pull, the deferred attach)
and 16 in PropertyLinks.cpp, plus ~400 of test.

### 4.5 What P1 does not deliver: the edited method (found 2026-09-13)

Sec 2.1 problem 1 says of `ProxyExp`'s Global scope: "the type is recomputed
before its instances and an edit to a method recomputes them."  Half of that
is true.  Found while probing the sheet-extended flange for docs/Sandbox.md
7.17's re-sizing, on the build of P2.

A `Part::FeaturePython` with a `Spreadsheet::Sheet` in `ProxyExp`, the
sheet's alias'd `expExecute` cell holding a `def` that writes `obj.Shape`:
the shape builds, two instances share one sheet, save and reopen work, and a
parameter change rebuilds -- all as designed.  Then the METHOD is edited in
the cell and the document recomputed, and the instances do not move.  Not
stale-by-one, not a cache: an instrumented method (a counter incremented in
the cell) shows the instance's `execute` is never entered.

The edge is not the problem.  `Sheet.InList` is `[Flange]`, a document
observer reports `Sheet` recomputed and then `Flange`, and the sheet's cell
does hold the new function object by then -- calling it by hand from Python
builds the new shape.

**How a link propagates at all, since that is what this turns on.**  Every
`DocumentObject` carries `_revision`, bumped in `DocumentObject::onChanged`
(`DocumentObject.cpp:1045`) whenever a non-`Output` property of it is touched.
A link property reports ITSELF touched by comparing revisions:
`PropertyLink::isTouched()` and `PropertyLinkList::isTouched()`
(`PropertyLinks.cpp:789`, `:1107`) are `linkRevision(target) != _revision`,
against the revision each stored at its last `purgeTouched()`.
`PropertyXLinkSubList::isTouched()` (`:5269`), which is what `ProxyExp`
inherits, aggregates its children's revision checks -- and returns false
outright for a Hidden scope, so `ViewProxyExp` reports nothing by
construction, which is correct for a list that is a reference and not a
dependency.  `Property::testStatus(bits, mask)` substitutes that VIRTUAL
`isTouched()` whenever `Touched` appears in the bits or the mask, and the
recompute optimization in `Document::_recomputeFeature` (`Document.cpp:4673`)
-- a feature not in error, `_enforceRecompute` false, `OptimizeRecompute` on,
skipped unless one of its own properties is touched -- asks exactly
`testPropertyStatus(Property::Touched, mask)`.  So the chain's dependency is
carried by the link's revision check, and it works: probed, replacing the
linked object's `Proxy` recomputes every instance that links it.

**The two definition changes that move no revision.**

1. *A `Spreadsheet::Sheet` pins its revision.*  `Sheet::getRevision()` is
   `return 0`, a literal, with the comment "Fix the object revision to reduce
   effect of recomputation time" (`Sheet.h:91`).  So NO link of any kind ever
   sees a sheet change -- probed with a plain `PropertyLink` and with an
   ordinary `PropertyXLinkList`, both as blind as `ProxyExp`.  It is the
   spreadsheet's deliberate bargain: a sheet is recomputed on every cell edit,
   and its consumers are meant to be driven by the expression engine's own
   cell dependencies rather than by the link.  A method in a cell is a
   consumer that bargain did not foresee.
2. *A function stored on the linked object from Python.*  `L.expExecute = f`
   lands in `dict_methods` through `FeaturePythonPyT::_setattr`; it is not a
   Property, nothing is touched, no revision moves.

Those two are precisely two of the three places sec 2.4 bumps the generation
counter from.  The third, `PropertyPythonObject::hasSetValue` -- a `Proxy`
replaced on the linked object -- is the one that already propagates, because
it IS a property change.  The counter, written for the cache, turns out to
enumerate exactly the definition changes the link cannot report.

**The fix as RULED (2026-09-13).**  Three coarse answers present themselves
and all three are the same mistake: make `Sheet::getRevision()` real; walk the
carrier's `getInList()` from `ProxyChain::bump()` and touch each `ProxyExp`
owner; or answer `mustExecute()` straight from the process-wide generation
counter.  Every one of them recomputes EVERY instance when ANY cell of the
sheet moves, because `PropertySheet::hasSetValue()` is the sheet's property
notification and carries no cell identity -- and the counter is coarser still,
a process-wide integer that an unrelated document's `obj.Proxy = ...` bumps.
That is exactly the cost `Sheet::getRevision()` was pinned to 0 to avoid
**[RULED by the user: "there is a reason sheet does that ... an unrelated cell
change will cause the proxied feature get recomputed.  The assumption is the
deps with sheet needs fine grained property level dependency, which is what
expression engine does"]**.

The contract the pin enforces is `ObjectIdentifier::isTouched()`:
`result.resolvedProperty->isTouched()`.  A dependency on a sheet is a
dependency on ONE CELL's property, and the object-level revision shortcut is
disabled so that nothing can bypass the finer test.  A sheet is one object
holding thousands of independent values; the object is the wrong unit.  The
chain's dependency is likewise not "the sheet" but "the value of the attribute
`expExecute`", which is one cell's property.

So: a value-level comparison, gated by the counter -- the shape the engine
already uses, where `Enforce` gets an object into `_recomputeFeature` and only
a value that really moved goes further.

1. `ChainEntry` gains an IDENTITY beside its `callable`: `__func__` when the
   attribute has one, else the callable itself.  `FC_PY_GetCallable` is
   `PyObject_GetAttrString`, so the Proxy path hands back a freshly built
   bound method on every access and raw identity would differ forever
   (measured: two `getattr`s give different objects, the same `__func__`);
   the sheet path returns the cell property's stored `ExpressionPy` and is
   already stable.
2. `FeaturePythonT::execute()` snapshots, after a successful run, the
   generation and the identity vector of the `execute` hook's chain.
3. `FeaturePythonT::mustExecute()` answers: empty `ProxyExp` -> 0, as today and
   free; generation equal to the snapshot -> 0, one integer, the common case;
   otherwise re-resolve (which `ensureChain` was going to do anyway) and
   compare identities, 1 only if THIS feature's definition moved.

Only the `execute` hook is watched.  An `expViewGetIcon` or an `expOnChanged`
re-typed on the same sheet must not rebuild geometry.

Why that is per-cell, measured 2026-09-13: `Sheet::execute()` recomputes
`cells.getDirty()` and their transitive dependents only (`Sheet.cpp:1154`,
`:1165-1190`), so an unrelated cell edit never re-evaluates the method cell and
its `PropertyPythonObject` still holds the SAME function object -- identities
compare equal, nothing recomputes.  Re-type the method cell and it is dirty,
re-evaluated into a new `ExpressionPy`, and the instances follow.

Rejected with it: the property-level form -- recording the source `Property*`
and asking `prop->isTouched()`, literally what `ObjectIdentifier` does.
ORDERING kills it: the carrier recomputes first and `Document::recompute`
calls `obj->purgeTouched()` immediately after, clearing its property flags
before the instance's `mustExecute` is ever asked; the engine does not hit
this because it never consults the flag at recompute time, it re-evaluates and
compares values.  COVERAGE kills it too: a function stored from Python lives
in `dict_methods` and is backed by no property at all.

**THE TRAP THIS LEAVES, AND IT IS NOT OURS TO CLOSE.**  **A function body's
identifiers are NOT dependencies, and the body reads them LIVE when it runs.**
`VariableExpression::_getIdentifiers` returns early while `_FunctionDepth` is
non-zero (`Expression.cpp:4163`), and `LambdaExpression::_visit` raises that
depth around `body->visit(v)` (`:6529`) -- so nothing a `def` or `lambda` body
references is registered as a dependency of the cell holding it.  Yet
`LambdaExpression::isTouched()` descends into the body with no such guard, and
the body resolves its names against the sheet's live frame at CALL time.
Measured: a cell `=def m(obj): return r * 2` with `r` = A1 returns 10, A1 is
changed to 9, the cell is NOT re-evaluated, the function object is the SAME
object -- and it now returns 18.  **So a method that reads sibling cells
changes behaviour with nothing observable changing: no revision, no touched
property, no new function object.  No mechanism in the engine can see it, and
the identity comparison above will not either.**

This is deliberate and it predates the chain: it is true of ANY spreadsheet
function, called from any cell, and a body's names may be its own parameters
or locals, so registering them would hang spurious dependencies -- and cycles
-- off the cell.  Widening it would be the same class of mistake as
un-pinning `getRevision()`.  The adjacent published statement is the `href()`
passage in the Assembly3 wiki (*Expression and Spreadsheet*): a reference
deliberately hidden from dependency checking, "may result in unstable
recomputing order, and thus given unexpected result", safe as a rule of thumb
only when it refers to something the user edits rather than something the
object calculates.  The rationale for the function-body case is not written
anywhere -- the wiki was searched, all 85 revisions, and `_FunctionDepth`
arrived inside `3008b30596` ("Expression: refactor for better performance",
2018-11-05) with no note.

**The idiom that follows, and it belongs in the tutorial.**  **A method's
inputs come through `obj` -- the instance's own properties -- never through
the sheet's frame.**  The chain already hands every element the owner (4.3
finding 2), so the method has everything it needs.  A shared constant that
genuinely wants to live in a cell is bound on the INSTANCE with an ordinary
expression (`obj.Pitch = Sheet.pitch`), which IS dependency-tracked -- measured:
a plain cell `=r * 2` follows A1 correctly while the `def` cells do not.
Parameters kept in the type's own sheet are a shared mutable global with no
working dependency, so the 7.17 probe case that put them there is the
ANTI-PATTERN, not the model.

**Two things found beside it.**

`DocumentObject::_revision` is declared `int _revision;`
(`DocumentObject.h:853`) and set by no constructor -- the body of
`DocumentObject::DocumentObject()` registers properties and nothing else --
so a fresh object's revision is indeterminate.  Twelve `App::FeaturePython`
objects created in one document read `[37, 0, 32374, 0, 32374, 32374, 32374,
0, 0, 0, 0, 0]`: three distinct values tracking heap reuse, where an
initialised member would give twelve zeroes.  The link properties do
initialise theirs (`int _revision{0}`, `PropertyLinks.h:701`, `:976`), so
this is an inconsistency rather than a design.

It has never surfaced because the number is never used AS a number.  It is
never serialized, never ordered, never compared against a constant: the only
comparison in the tree is `linkRevision(target) != stored`, where `stored`
was captured from that same object by `purgeTouched()`.  Stable garbage
compares exactly as well as zero.  And the one moment the initial value is
read -- a link's first `isTouched()`, before any `purgeTouched()` has
snapshotted -- fails SAFE: the link's own `_revision` is a defined 0, the
target's is garbage, so the compare says "touched" and the dependent
recomputes once more than it needed to.  A missed recompute would need the
garbage to be exactly 0 with the target already changed, and `purgeTouched()`
closes that window at the end of the first recompute.  So: real undefined
behaviour, no observable consequence, and the Python `Revision` attribute
unreadable for anything (which is how it was spotted).  One initialiser fixes
it, but `DocumentObject.h` is included nearly everywhere -- carry it in the
D2 build, which touches `FeaturePython.h` anyway, rather than paying a
whole-tree rebuild for a cosmetic correction.

`OptimizeRecompute` is a PERSISTED user parameter: setting it False from a
probe writes `user.cfg` and silently changes every later run on the box,
which is how the first reading of this bug came to be wrong.

Sized as a build item of 7.17 D2 (docs/Sandbox.md).  **BUILT 2026-09-13, sec 4.6**
[approved 2026-09-13: "implement it in next session, with tests"].  The gate,
all of it with `OptimizeRecompute` at its default ON -- with it off every case
passes for the wrong reason:

    1  the method re-typed in its cell, a plain doc.recompute(), every
       instance rebuilt
    2  two instances on one carrier, both rebuilt, each with its own
       parameters
    3  an UNRELATED cell of the same sheet edited -> no instance recomputes
       (the whole point of the ruling)
    4  a plain spreadsheet regression, no ProxyExp anywhere: an unrelated
       cell touched leaves the other cells' consumers alone -- the property-
       level dependency the sheet's pinned revision exists to protect, and
       what a coarse fix would have broken
    5  the carrier's Proxy replaced, and a function re-stored from Python
       (L.expExecute = f) -- the two non-sheet definition changes
    6  an expViewGetIcon re-typed on the same sheet -> NO geometry recompute
    7  the method cell cleared (the dynamic property is REMOVED, 4.3) ->
       one recompute, the chain shortens, no crash
    8  an empty ProxyExp -> the P0 path, nothing resolved, nothing compared

`FeaturePythonChain`'s existing sheet case does not catch any of this because
it calls the hook directly after the recompute rather than relying on the
recompute to call it.  Case 4 is the regression that matters most: it is not
about the chain at all, and it is the one a future coarse "fix" would trip.

### 4.6 The recompute as built (2026-09-13)

Built to 4.5's design, and it holds.  27 cases in `FeaturePythonChain` (the
eight of 4.5 plus the existing nineteen), 2715 Python OK, ctest 605/605
offscreen, the view gate 25/25.

**Where the code went.**  `ChainEntry` gains an `identity` beside its
`callable`, filled in `resolveChain()`: `__func__` where the attribute has
one, else the callable itself.  `HookSlot` gains a `snapshot` -- the identity
vector as of the last run, its generation, and a valid flag -- kept BESIDE the
chain and not in it, because a generation bump clears the chain and the
snapshot is what the rebuilt chain is compared against.  Two members on
`PyHookImp`: `snapshotChain(hook)`, which `FeaturePythonImp::execute()` calls
after every run, and `chainDefinitionChanged(hook)`, which answers empty ->
false, generation equal -> false, else re-resolve and compare pointers.  On a
comparison that finds nothing moved, the snapshot's generation is advanced so
the next query is one integer again; on one that finds a change, it is
deliberately NOT advanced -- the answer has to stay the same until the run
that acts on it, and it is that run which records the new definition.

The first look at a chain seeds the baseline and answers "unchanged".
Answering "changed" there would recompute every chained feature once on load,
and the restore already forces the recompute that is really needed.

**The correction to 4.5's step 3: `mustExecute()` is not the gate that
matters.**  4.5 put the whole answer in `FeaturePythonT::mustExecute()`.  That
is necessary but not sufficient, and for the sheet case -- the case the whole
section is about -- it is not even the one that fires.  `Document::recompute`
sets `ObjectStatus::Enforce` on every object in the InList of anything it
recomputed (`Document.cpp:4396`), and the carrier is in the instance's InList.
So the sheet's own recompute pushes the instance into `_recomputeFeature`
regardless of what `mustExecute()` said, and it is stopped one level further
in, by the optimization at `Document.cpp:4673`: not in error, `_enforceRecompute`
false (a DIFFERENT flag from `ObjectStatus::Enforce`), no touched property --
`ProxyExp.isTouched()` is false because the sheet's revision is pinned -- and
then `skipRecompute()` returning its default true.  **That is the gate the
edit has to pass**, so `FeaturePythonT::skipRecompute()` asks
`chainExecuteChanged()` first and returns false when the definition moved.

`mustExecute()` still earns its line: it is the gate for the change that
recomputes NOTHING first, `L.expExecute = f` from Python, where no Enforce bit
is ever set and `mustRecompute()` is the only thing asked.  Both are cheap and
both are needed; neither alone passes the eight cases.

**Why `__func__` alone is enough, though it cannot see a replaced Proxy
INSTANCE.**  `MarkerProxy(1).expExecute` and `MarkerProxy(2).expExecute` share
one `__func__`, so the comparison calls them equal while the behaviour
differs.  That is not a hole: a Proxy is a `PropertyPythonObject`, replacing it
is a property change on the carrier, the carrier's `_revision` moves, and
`ProxyExp.isTouched()` reports it -- `doRecompute` is already true before
`skipRecompute()` is asked.  The comparison only has to cover what the link
cannot report, and the two things the link cannot report -- a cell re-typed,
a function stored from Python -- both change the function itself.  Comparing
`__self__` as well would be strictly more precise and buy nothing.

**What the tests needed, and four things learned writing them.**

1. *A `def` in a cell aliases the cell to the function's own name.*
   `=def expExecute(obj): obj.Marker = 10` needs no `setAlias` at all, and
   a `setAlias` to something else is overwritten the next time the cell is
   set.  That is the idiom for the tutorial: the hook name is stated once,
   where the method is.
2. *A cell `def` needs a trailing newline.*  The grammar's `suite` is
   `simple_stmt NEWLINE` (`ExpressionParser.y:378`), so `=def m(obj): ...`
   without one is not an expression at all -- the sheet keeps it as a plain
   string and the attribute is not callable.  `lambda` has no such need.
3. *A method cannot call back into Python to record itself.*
   `=lambda obj: obj.Proxy.record(obj)` is refused with "Permission denied:
   unsafe.getattr", which is the sandbox doing its job.  `=lambda obj: 1 == 2`
   does return a real Python `False`, which is the protocol's "not mine", so
   an element can decline and let the one behind it run.
4. *A counter written on the feature cannot measure a NON-recompute.*  Writing
   a property to record the call is itself a touch, and the next recompute
   then follows from that rather than from what the case is testing -- two
   cases failed exactly this way on the first run, reading 10 where they had
   just written 0.  Nor is `doc.recompute()`'s return usable: `++objectCount`
   happens before `_recomputeFeature` (`Document.cpp:4364`), which then skips,
   so a skipped object is counted.  What works is a chain element in FRONT of
   the one under test that records the call and returns False -- an execute
   counter that writes nothing.

**Gate, all eight cases green with `OptimizeRecompute` at its default ON** (the
cases force it and put it back exactly as found -- it is a PERSISTED user
parameter): the method re-typed rebuilds the instance; two instances on one
carrier rebuild with their own inputs; an unrelated cell of the same sheet
recomputes no instance, and the negative is shown not to be vacuous by
re-typing the method cell in the same test; the plain-spreadsheet regression,
no ProxyExp anywhere, follows its own cell only; the carrier's Proxy replaced
and a function re-stored from Python both rebuild; an `expViewGetIcon`
re-typed on the same sheet rebuilds nothing; the method cell cleared shortens
the chain in one recompute and then settles; an empty `ProxyExp` is untouched
by a generation bump from elsewhere in the process.

**One thing about running the gate.**  `ctest` with no `QT_QPA_PLATFORM` picks
up `xcb` from `DISPLAY` on this box, and `FormWidgets_Tests_run`'s
`test_panelMirror` and `test_panelMirrorItems` then fail: the mirror waits for
the platform's repaint (7.19 M2) and the window is never exposed under WSLg's
X server.  `QT_QPA_PLATFORM=offscreen`, which docs/Testing.md already
prescribes, is 605/605.  Nothing to do with the chain -- but it looks exactly
like a regression if the variable is forgotten.

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
link, and delegation is the chain.

**DONE 2026-09-13**: 7.17 is re-sized, with the flange written as a
sheet-extended `Part::FeaturePython` beside the library form and
probed against it (19.6 ms per parameter change, against 21.3 ms for
the same flange as a host-Python `Proxy` -- the engine language costs
what host Python costs, because both wait on the same OCCT calls).
The re-sizing promotes D1 from that section's first stage to the
precondition of both carriers (a cell's `def` is a function object
and meets the image's refusal exactly as a library's would), folds
P3 into D2, and adds the `mustExecute` fix of sec 4.5 as D2's first
item.
