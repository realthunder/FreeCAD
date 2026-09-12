# The proxy chain: document programs extend native objects

**[planned 2026-09-12, awaiting the user's review; nothing built]**

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

One list, on the App object.  The view provider has no list of its
own: `ViewProviderFeaturePythonT` reads its object's `ProxyExp` and
resolves the view hooks on the same linked objects.  The document
carries the definition; a headless run and a GUI run see one list.

### 2.2 Resolution: what "exposes a method" means

For a linked object `L` and a hook `H`, in this order:

1. `getattr(L.pyobject, "exp" + Hook)` -- through the object's own
   Python wrapper.  This finds a dynamic property (a **spreadsheet
   alias** whose cell is a `lambda` or `def`: `Sheet.cpp:797`
   `setObjectProperty` exposes it as a `PropertyPythonObject`), a
   function stored on the object at runtime (`L.expExecute = f`,
   bound to `L` by `FeaturePythonPyT::_setattr`), or any callable
   attribute a C++ type provides.
2. else `getattr(L.Proxy, "exp" + Hook)` when `L` has a `Proxy` -- a
   Python-scripted object that chooses to extend others.

A non-callable is skipped.  `L`'s type is never inspected: a Sheet, a
FeaturePython, an ExpressionLibrary (7.17 D2, once its module's
bindings are exposed as properties -- one small addition there), a
guest stand-in (rung 2), or a plain C++ object with a
`PropertyPythonObject` all qualify by the same rule.

The names: `exp` + the hook's name with its first letter upper-cased
for App hooks (`expExecute`, `expOnChanged`, `expGetSubObject`),
`expView` + the same for view hooks (`expViewGetIcon`,
`expViewClaimChildren`, `expViewOnChanged`).  Two prefixes because
`onChanged`, `editProperty` and `attach` exist on both sides.

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

Notification hooks (`onBeforeChange`, `onChanged`,
`onDocumentRestored`, `unsetupObject`; view: `attach`, `updateData`,
`onChanged`, `startRestoring`, `finishRestoring`) have no result to
check natively.  Proposed: every element is called, links then Proxy,
unless one returns True, which stops the chain (an explicit "consumed";
None, the usual return, continues).  The base call always follows, as
today.  **Decision for the user** (sec 5, item 2).

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

## 3. The cog refactor

### 3.1 Why and what

Both Imp classes are one pattern per hook: call check, GIL lock, an
args tuple with `obj` first, `pyCall`, a result mapping, the
`NotImplementedError` catch, the error report.  The template overrides
are a second pattern: call the Imp, dispatch on its return kind to the
base.  The X-macros generate the declarations but every body is typed
by hand -- 22 + 46 of them -- and the chain would mean touching every
one.  cog is already the tree's generator for exactly this kind of
table (`Mod/Part/App/PartParams.py` over `PartParams.h/.cpp`,
`Tools/params_utils.py`; inline blocks in
`Mod/Sketcher/App/SketchObjectPyImp.cpp`), with the generated output
COMMITTED so the build never runs cog.

One spec, `src/App/FeaturePythonHooks.py`: a table of hooks, each
with its C++ signature, its arguments and their Python converters
(string, int, bool, object, matrix, ...), its return kind (`bool`
handled/not, `ValueT`, `int` with a sentinel, `tuple` decoded by a
custom body, `notify`), its default when not handled, the `__object__`
form if it has one, and `side` App or View.  Hooks whose body is
irregular are marked `custom` and keep their hand-written body outside
the generated block: App `getSubObject`, `getSubObjects`,
`getLinkedObject`, `getElementMapVersion`, `redirectSubName`; View
`getIcon`, `getExtraIcons`, `getElement`, `getDetail`, `getDetailPath`,
`getSelectionShape`, `setEdit`/`unsetEdit`/`setEditViewer`/
`unsetEditViewer`, `iconMouseEvent`, `setupContextMenu`,
`getDisplayModes`, `setDisplayMode`, `dropObjectEx`,
`getLinkedViewProvider`, `getDropPrefix`.  Roughly 14 of 22 App hooks
and 26 of 46 view hooks are regular.

Generated, inside `[[[cog ... ]]]` blocks in the four existing files
(no new source files; the diff stays reviewable in place):

- the Imp declarations for the regular hooks, the `py_<hook>` members
  and the flags enum (replacing the three X-macro expansions), the
  destructor's release, `init()`;
- the regular Imp bodies, each with the chain walk of 2.3 at its one
  call site;
- for the custom hooks, the chain walk only, as a helper the
  hand-written body calls (`callChain(hook, args, decode)`), so the
  chain is in one place for every hook;
- the template overrides, from the return kind.

Kept exactly: `has__object__`, `__allow_recursive_`, the bitset
guards, the one GIL lock per call, the per-hook `Py::Object` cache,
`FeaturePythonPyT` untouched but for the generation bump.

### 3.2 Tooling

`cogapp` is in neither `.conda/freecad` nor the system Python (checked
2026-09-12): `pip install cogapp` into the env, and the generated
output committed as PartParams does.  `scripts/cog-check.sh` runs
`python -m cogapp --check` over the four files (plus the Params files
already in the tree) so a stale generation fails loudly; the doc
header in each block names the regenerate command, as PartParams.h
does.

## 4. Stages

    P0  the cog scaffold: FeaturePythonHooks.py, the blocks replacing
        the X-macros and the regular bodies in the four files, the
        custom bodies untouched, no ProxyExp yet.  Gate: both suites
        green; a diff of the preprocessed Imp against HEAD's shows
        the same calls in the same order (a scratch check, not kept).
                                                        one session
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
    P2  the view side: expView* on the object's list, the same walk in
        the view hooks.  Gate: getIcon, claimChildren, getToolTip and
        onChanged from a sheet, in `FeaturePythonChain`'s GUI half.
                                                        one session
    P3  the sandbox: the P1 gate routed (needs 7.17 D1 for a `def`
        cell); the audit line names the linked object's document; a
        cross-file link's write prompts doc.foreign once.  Sits
        inside 7.17's build, not before it.

Lines: P0 the spec ~200, the generated output replacing ~900 lines of
bodies with ~900 generated ones (the win is the single table and the
single chain site, not the count); P1 ~250 (the property, the
resolver, the cache, the counter, three bump sites) plus ~200 of test;
P2 ~120 plus ~100 of test.

## 5. Decisions for the user

1. The property name `ProxyExp` and the prefixes `exp` (App) /
   `expView` (view provider) -- or one prefix with a rule for the
   three colliding names.
2. Notification hooks: call every element and let a True stop the
   chain (proposed), or first-found only like the value hooks.
3. One list on the App object shared by the view provider (proposed),
   or a second list on the view provider for view-only overrides.
4. Resolution order: the object's own attribute (alias, stored
   function, C++ attribute) before its `Proxy` (proposed), or Proxy
   first.
5. Generated code committed, with the check script (proposed, the
   Params precedent), or generated at build time (would make cogapp a
   build dependency for every platform, including the feedstocks).

## 6. What this changes in docs/Sandbox.md 7.17

D1 stands and is the prerequisite for the routed case.  D2's carrier
is no longer the only way a document supplies methods: a sheet with
alias'd callables is one today, natively, and the chain makes it an
object's extension.  The typed-sheet discussion of 2026-09-11 (a
`Type` link and prototype delegation) is subsumed: `ProxyExp` is that
link, and delegation is the chain.  7.17 is re-sized after P1, with
the flange example written as a sheet-extended `Part::Feature` beside
the library form.
