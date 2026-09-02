# Sandboxing the expression engine's Python (before the sheet goes to wasm)

Ordered 2026-08-29, broadening docs/SpreadsheetRemote.md. The user's
sequencing: **do not bring the spreadsheet to the browser until the
expression engine's Python interop is sandboxed.** The sheet is the
tightest expression consumer; bringing the sheet to wasm means
bringing expression evaluation to wasm; if that evaluation's Python
runs in a wasm sandbox, the same sandbox is (a) a real security fix on
desktop and (b) the thing that safely ships to the viewer. This is a
design pass -- nothing here is built.

Revised 2026-08-29 after a code-verified re-evaluation: the concrete
native-module bridge plan, and the carriers this design does not
close. Revised again 2026-08-30 (user order): the SECURITY MODEL is
now defined FIRST and is fully user-controllable -- sec 3,
principals / permissions / grants on the browser model, replacing
the earlier two-tier wording throughout -- and sec 8 evaluates
minimizing, ultimately abandoning, native CPython. Renumbered: the
bridge keeps sec 7; boundary/marshalling/runtime are now 4/5/6;
carriers/sequencing/open questions are 9/10/11.

This is **not new direction.** RoadMap workstream 3 already names it:
"Run untrusted embedded document Python through a Pyodide/WASM
sandbox -- fixes a real security hole (opening a malicious `.FCStd`
can currently execute arbitrary code)... The same WASM modules can run
out-of-process on desktop via a native runtime, unifying the sandbox
and browser stories." Status there: not started. ComputeBoundaries
sec 2 adds that a Python feature's interpreter "must be co-resident in
the worker -- which is compatible with running that worker as a
Pyodide/WASM sandbox." This doc turns that line item into a concrete
boundary.

## 1. What the engine actually is (ground truth from the code)

FreeCAD's expression engine is a **hand-written C++ interpreter**
(`src/App/Expression.cpp`, ~238KB; grammar in `ExpressionParser.y`),
not a call into Python's `eval`. It parses a Python-like language to
its own AST and walks it. The design intent, from the author's
write-up (assembly3 wiki, "Expression and Spreadsheet"): "For security
reason, the FreeCAD expression classes and parser are refactored to
interpret the script by itself" -- deliberately avoiding `eval()` so
an attacker cannot reach system modules through it.

**But the interpreter is coupled to a live in-process CPython at the
value level, not just for scripting.** The decisive fact:

- `Expression::getValueAsAny()` is `pyObjectToAny(getPyValue(...))`
  under `PyGILStateLocker` (`Expression.cpp:1614`). There is **no
  pure-C++ evaluation path.** Every expression result, including
  `1+2`, is a CPython object.
- Arithmetic itself runs through the CPython number protocol: `calc()`
  uses `PyNumber_Negative`, `PyObject_RichCompareBool`,
  `PySequence_Contains`; numbers are `Py::Float`/`Py::Long`
  (`Expression.cpp:2015`, 569). So even a spreadsheet cell that is
  pure numeric arithmetic holds the GIL and touches the interpreter.
- `getPyValue` has ~130 call sites in `Expression.cpp` alone; it is
  the spine of evaluation, not an edge case.

On top of that value layer sit the genuinely powerful, genuinely
dangerous features (all in `Expression.cpp` / `ObjectIdentifier.cpp`):

- **Module import**: `import_py(name)` and `import`/`from` statements
  go through `ImportModules::getModule` ->
  `PyImport_ImportModule` (`Expression.cpp:2513`, 7381).
- **Callable execution**: any `func()` reaching a Python callable
  passes `CallableExpression::securityCheck` /
  `ImportModules::checkCallable` before it runs (`Expression.cpp:4888`,
  2615).
- **Attribute / value access**: `ObjectIdentifier::getPyValue`
  (`ObjectIdentifier.cpp`) pulls live Python objects out of document
  objects -- `Box.Volume`, `obj.Proxy.foo`, arbitrary `obj.attr`.
- **Pseudo-properties**: `_self`, `_app`, `_gui`, `_shape`, `_pla`,
  `_matrix` (`ObjectIdentifier::getPseudoProperties`, .cpp:1596)
  return live Python wrappers of C++ objects -- `_app` is the
  Application, `_gui` is the GUI command surface, `_shape` is a
  `Part.TopoShape` over a live OCCT solid.

## 2. Why the current sandbox is the concern

The protection today is a **name-matched allowlist enforced inside the
same interpreter it is trying to contain** -- the classic confused
deputy. Two layers:

1. A **denylist of builtins**: `eval`, `execfile`, `exec`,
   `__import__`, `file`, `open`, `input`, `setattr` are blocked
   (`CallableExpression::securityCheck`, `Expression.cpp:4888`).
2. An **allowlist of modules** matched by `__module__` string prefix:
   `builtins, FreeCAD, App, Gui, Base, Units, Part, PartDesign,
   Sketcher, Spreadsheet, collections, math, re, freecad.fc_cadquery`,
   plus user-configured entries (`ImportModules`, `Expression.cpp:2513`;
   `checkCallable` walks `__module__`/`__self__`/`inspect.getmodule`
   to attribute a callable to a module name, .cpp:2615).

The structural problems, in order of severity:

- **Shared interpreter, shared address space.** The filter runs *in*
  the CPython it guards. Isolation depends entirely on the filter
  being complete; there is no wall behind it. A missed path is a full
  escape (host filesystem, network, memory, other documents).
- **Attribution is guesswork with give-up branches.** `checkCallable`
  has a cascade of fallbacks for naming a callable's module and, when
  all fail, denies with "Access denied of callable in unknown module."
  The many branches are the attack surface: `__module__` is a plain
  attribute, `functools.partial`/`__wrapped__`/C-accelerated
  callables/bound methods obscure origin, and a whitelisted module is
  a launch pad -- `App.getDocument()` reaches every scripted object's
  `Proxy` (arbitrary document Python), `Part`/`Sketcher` expose C++
  objects whose methods and `__class__`/`__globals__`/`gc` neighbours
  were never enumerated.
- **The author already flagged the ceiling.** The wiki: the engine
  "can now call into other Python code, which may have unknown
  security risk," and lists future mitigations (signatures,
  no-scripting/no-import/no-callable modes). Those are more
  name-based knobs on the same in-process model.

The threat is concrete and named in the RoadMap: **opening a
downloaded `.FCStd` recomputes its expressions and can execute
arbitrary code.** Expressions are stored in the file; recompute runs
them; the allowlist is the only thing between a malicious document and
the host.

## 3. The security model: principals, permissions, grants

Added 2026-08-30 (user order): define the boundary BEFORE the
mechanism, and make it fully user-controllable -- grant as
first-class as deny, on the browser security model. The 2026-08-29
text had only a binary trust split ("untrusted document vs trusted
session"); this section replaces it. The browser analogy is used
precisely, not decoratively: wasm guest = sandboxed renderer, host
bridge dispatcher = browser kernel, principal = origin, grant flow
= permission prompt, per-document permission panel = site settings.

### 3.1 Principals (who is asking)

Every bridge op executes on behalf of exactly one principal:

- **`document:<hash>`** -- code carried by a document: expressions,
  and (sec 8, rung 2) embedded payloads and scripted-object code.
  Identity is CONTENT-ADDRESSED: a hash over the document's
  code-bearing payload (all expression strings plus embedded
  scripts, canonicalized -- sec 11), not the file path and not the
  document `Uid`. The hash is the credential: grants bind to it, so
  tampering with a trusted file voids its grants and re-prompts,
  and copying a trusted document's `Uid` into a hostile file gains
  nothing. `Uid` and path are display metadata only.
- **`session`** -- what the user runs by hand: console input,
  macros invoked by explicit action. Wider DEFAULTS (the user is
  the operator), but the same enumerated permissions, the same
  revocability, and eventually the same sandbox (sec 8, rung 3).
- **`addon:<name>`** -- installed workbenches and extensions,
  trusted the way a browser trusts an extension: explicitly, per
  addon, and less than the kernel. Addons run native today (sec 8)
  but are principals from day one, so their grants are visible and
  revocable in the same panel as everything else.
- The C++ core is not a principal; it is the kernel that enforces
  the model.

### 3.2 Permissions (what can be asked)

Named, coarse, human-explainable -- the browser lesson: users
reason about "camera" and "location", not about syscalls. The
catalog gates CATEGORIES; the sec 7.5 annotation tables enumerate
the exact members inside each category. v1 catalog, with defaults
per principal class:

  permission       document   session   addon
  ---------------  --------   -------   -----
  doc.read.self    ALLOW      ALLOW     ALLOW
  doc.write.self   ALLOW (a)  ALLOW     ALLOW
  doc.foreign      PROMPT     ALLOW     ALLOW
  geom.call        ALLOW (b)  ALLOW     ALLOW
  app.query        PROMPT     ALLOW     ALLOW
  gui              DENY (c)   ALLOW     ALLOW
  host.import:<m>  PROMPT     PROMPT    ALLOW
  unsafe.getattr   DENY       PROMPT    ALLOW
  fs / net         not offered in v1 (no op exists to gate)

  (a) expressions write only their declared result through the
      engine; `doc.write.self` exists for rung-2 Proxy execute().
  (b) compute over the document's own shapes carries no ambient
      authority; its cost is bounded by fuel (7.8), not permission.
  (c) not even promptable for documents -- there is no legitimate
      "a file drives the GUI" flow. Session and addons keep it.

`doc.read.self` is the same-origin rule: a document touching its
own objects is a page touching its own DOM -- always allowed, zero
prompt noise. `doc.foreign` is the cross-origin wall (today's
`_app.getDocument(other)`). `host.import:<m>` is PER MODULE and
replaces the old user-configured allowlist entries with grants the
user actually sees, scopes, and can revoke.

### 3.3 Grant lifecycle (what the allowlist could never express)

Each (principal, permission) resolves to ALLOW, DENY, or PROMPT.
A PROMPT answer is recorded at one of three scopes, exactly the
browser's: **once** (this evaluation), **session**, or **always**
(persisted in user preferences, keyed by the principal identity --
for documents, the content hash).

Prompt mechanics follow the popup blocker, not the modal dialog:

- An op pending a grant FAILS FAST with a structured
  `PermissionNeeded{principal, permission, target}`; the evaluation
  surfaces it as a cell/object error carrying that payload.
  Recompute NEVER blocks on a prompt (a modal mid-recompute is
  also a known harness killer on this box).
- The UI shows a passive indicator -- this model's blocked-popup
  icon -- on the affected cell/object and in the per-document
  permission panel: what asked, for what, when. Granting re-runs
  the evaluation.
- Document code cannot spam prompts: grant REQUESTS surface only
  through the panel/indicator, and the grant dialog itself opens
  on user gesture.
- Headless/CLI mirrors browser flags: a policy file plus
  `--grant doc.foreign` style switches; the default is DENY plus a
  structured audit line, never a hang.

The user-facing surface: a "Document permissions" panel (site
settings analog) listing every grant of the open document, each
revocable; a global settings page for per-permission defaults
("always ask" / "block all"); an audit log of every ALLOW/DENY
decision. Grant, deny, inspect, revoke -- all four verbs exist,
and all four are the user's.

### 3.4 Enforcement point (the kernel rule)

All checks run HOST-SIDE in the bridge dispatcher, never in the
guest -- Chromium's split exactly: a fully compromised renderer
still cannot skip the kernel's IPC checks. Two layers in order,
both in the dispatcher: the sec 7.5 schema check (does this op
exist for this type at all -- generated, absent-means-deny), then
the permission check (may THIS principal use it NOW -- the grant
store). Every op carries its principal; there is no ambient
principal. Quotas (fuel, memory, op depth -- 7.8) are accounted
per principal -- the tab-throttling analog -- and are limits, not
permissions: they bound cost, never capability.

## 4. The boundary that actually closes it

**Replace "name-filtering inside the interpreter" with "memory and
syscall isolation by construction."** Run the Python that expressions
touch inside a **wasm sandbox** (CPython-on-wasm / Pyodide-class
image). The sandbox gets no host filesystem, no network, no host
memory, and no module it was not given -- not because a filter says so
but because the wasm/WASI boundary grants none. The host then
*chooses* what to expose across the boundary. Security becomes a
capability question ("what did the host bridge in?") instead of a
completeness question ("did the denylist miss anything?").

Because there is no pure-C++ value path (sec 1), the boundary cannot
be drawn narrowly at "only user `func()` calls." Two architectures are
honest; the recommendation is B, with A as an optional accelerator.

### A. De-Pythonize the arithmetic/units core (optional, later)

Give the interpreter a real C++ value path -- evaluate
numbers/units/booleans/cell-refs on `App::any` + `Base::Quantity`
without CPython -- so the arithmetic majority of cells never enters an
interpreter at all, shrinking the sandbox surface to genuine Python
operations. Upside: a fast path and a small sandbox. Downside: a large
behaviour-preservation refactor (Python richcompare, sequence
containment, numpy interop, mixed-type coercion all currently come
free from CPython); risk of subtle numeric/relational drift in files
in the field. Not required to close the hole; worth it only as an
optimization once B exists.

### B. Relocate the Python value layer into the sandbox (primary)

The expression evaluator's Python -- the value layer and every
`func()`/`import_py`/callable -- runs against a **sandboxed CPython**.
OCCT, the `Document`, and the C++ object graph **stay in the host**.
Document objects are exposed to the sandbox as **host proxies** through
an object-operation protocol: the sandbox never holds a real
`TopoDS_Shape` or a real `Document` pointer; it holds a handle, and
each attribute read or method call on that handle marshals back to the
host, where a **policy** decides whether to answer.

Concretely, the small set of chokepoints from sec 1 stop calling
CPython directly and call the sandbox interface instead:

- `ImportModules::getModule` -> "ask the sandbox to import in its own
  `sys.modules`"; the host allowlist becomes the sandbox's *image
  contents plus policy*, not a string match (concretely: sec 7.4's
  rings -- the named modules are native, so "import" means
  "install a generated facade").
- `CallableExpression` executing a Python callable -> "run this call
  in the sandbox"; arguments marshal across (sec 5); the whole
  `checkCallable` name-attribution machinery is **deleted** -- there
  is nothing to attribute, the callable simply cannot reach the host
  except through bridged proxies -- PROVIDED the bridge itself is a
  closed, typed op set (sec 7.1); a bridge that forwards generic
  `getattr` would rebuild the confused deputy at the boundary.
- `ObjectIdentifier::getPyValue` on a Python-backed attribute ->
  "resolve this attribute on host proxy H"; the host answers the
  read from the C++ property system and returns data or another
  proxy -- never by running a generic `getattr` on a live PyObject
  (sec 7.5). Host-side Python attribute drill-down (`obj.Proxy.foo`)
  is denied to the document principal by default (secs 3.2, 7.1).

This is exactly ComputeBoundaries' "Python + the module co-resident in
the worker": the expression sandbox is **the first and smallest
co-resident worker** -- it hosts the Python value layer and reaches
geometry through the object protocol. The protocol it needs is the
same "semantic, schema'd, transactional object-operation protocol"
the process-per-document goal converges on (ComputeBoundaries sec 7),
so this is plumbing the roadmap wants anyway, not a detour.

## 5. The marshalling contract (the core design work)

What crosses the boundary during one evaluation splits in two:

- **By value (serialize):** numbers, strings, booleans, `Quantity`/
  units, lists/dicts of those. Cheap, and enough for the whole
  cell-references-cell arithmetic world.
- **By reference (host proxy):** document objects, `TopoShape`s,
  placements/matrices backed by C++, and the pseudo-properties
  `_self`/`_app`/`_gui`/`_shape`/`_pla`/`_matrix`. These are live host
  objects; they cross as opaque handles. Reads/calls on a handle
  round-trip to the host and are policy-checked there.

Consequences to design deliberately:

- **Round-trip cost.** `A1:C9` of `=Box.Volume/2` becomes many
  host<->sandbox hops. Mitigations: keep values-by-value on the fast
  path; batch/cache proxy reads within a recompute; and note that
  cells referencing only other cells never produce a proxy at all.
  (Architecture A, if taken, removes the interpreter from those cells
  entirely.)
- **Results cross back too (the reverse contract).** Today
  `pyObjectToAny` can hand the host a live PyObject as an expression
  result, and properties will store it. Under the sandbox that
  object lives in another interpreter. v1 rule: a result must
  marshal by value or be a host proxy passed through unchanged;
  anything else is an evaluation error, not a silent wrapper.
  Details and rationale: sec 7.7.
- **Policy is a capability, and it can be *tighter* than today.**
  `_gui` -- the entire GUI command surface -- should almost certainly
  be **denied** to sandboxed document expressions; the current
  allowlist cannot express that. Same for `_app.getDocument(other)`
  reaching sibling documents. The sandbox lets the surface *shrink*,
  not just relocate. Sec 3.2 formalizes both: `gui` is DENY (not
  even promptable) for document principals; `doc.foreign` is
  PROMPT.
- **Principals become expressible.** A downloaded `.FCStd`'s embedded
  expressions and the user's own live-session macros are not equally
  trusted; sec 3.1 gives each its own principal with its own default
  grants and its own revocable grant record -- a dimension the
  binary allowlist has no way to state, and one the earlier "two
  tiers" wording under-specified.
- **Geometry authoring stays in the host (v1 scope line).** If a
  sandboxed callable wants to *build* a shape (cadquery), it would
  need OCCT inside the sandbox -- which pulls in the entire kernel and
  is out of scope. v1: the sandbox reads properties and calls
  host-side methods through proxies; it does not author B-rep. This is
  what keeps the sandbox image small and OCCT in one place.

## 6. Runtime choice

The isolation must come from **wasm/WASI memory+syscall confinement**,
not from restricting the Python dialect.

- **CPython-on-wasm (Pyodide-class image): recommended.** Preserves
  exact Python semantics (the value layer and numpy keep working),
  RoadMap-named, browser-proven. (`freecad.fc_cadquery` is NOT a
  justification: it authors B-rep through OCCT bindings, which sec 5
  keeps host-side -- cadquery-in-expressions is out of v1 scope
  under ANY runtime, so it argues nothing here.) Heavy (multi-MB +
  interpreter startup), but the weight buys semantic fidelity, and the
  isolation is the wasm boundary, not a neutered interpreter -- which
  is precisely the point: stop trying to make Python safe from inside,
  deny it the host from outside.
- **MicroPython/WAMR: rejected for v1.** Small and fast, but an
  incomplete stdlib and non-CPython semantics would break the
  value-layer behaviour files in the field rely on. Viable only if
  a reduced expression dialect were acceptable -- it is not, given
  what already ships.
- **Subinterpreters / free-threaded CPython (PEP 684/703): not a
  security boundary.** They solve the GIL for parallelism
  (ComputeBoundaries) but share the address space -- no isolation.
  Orthogonal to this problem.
- **Desktop host = a wasm runtime (Wasmtime / WAMR + WASI); browser
  host = the browser's own wasm engine.** One image, two hosts -- the
  RoadMap's "unify the sandbox and browser stories." The desktop
  security fix and the viewer's evaluator are literally the same
  artifact.

**Where this landed (2026-09-02).**  The WASI image under wasmtime is
built and is the shipping default, as above.  But the desktop now has a
SECOND runtime behind the same `ImageHost` seam: **pyodide (CPython on
emscripten) inside a bare V8** that FreeCAD embeds itself, from the
`v8-embed` conda package built for this purpose on all five platforms.
The user's decision of 2026-09-02 was to build it rather than keep
pyodide "for the browser only": the image slice is the same source
compiled as a pyodide extension wheel, the 69 seam gtests and the
456-file corpus gate pass on both runtimes, and the confinement comes
from what a bare engine lacks (no `process`, `require`, filesystem,
network or module loader exist to name) rather than from a denylist.
The preference `Expression/Sandbox:Runtime` selects `wasi` or
`pyodide`.  What pyodide buys is the ecosystem (prebuilt numpy and the
rest, loaded through the same scoped reader) and a path to Python
workbenches in the browser; what it costs is a 1.5 s cold start and
~3x the per-expression time.  Design record and build log:
`docs/PyodideHost.md`; the measurements that changed the earlier
"do not build V8 by hand" verdict: `docs/ExpressionImage.md`,
"Pyodide-on-node, benchmarked and probed".

## 7. Interfacing native modules with the sandbox (added 2026-08-29)

Sec 4 says "host proxies through an object-operation protocol";
this section is the concrete plan. The decisive observation: the
current allowlist's named modules -- `FreeCAD`, `App`, `Gui`,
`Part`, `Sketcher`, `Spreadsheet` -- are C++ extension modules.
None of them can be imported inside a wasm CPython. "The sandbox
imports Part" can only ever mean "the sandbox gets a facade whose
every member forwards across the boundary" -- so HOW that facade is
built is the security design, not an implementation detail.

### 7.1 The invariant: a closed, typed op set -- never getattr

The host side of the bridge is a fixed dispatcher over enumerated,
typed operations. It never services a generic attribute read or a
generic call on an arbitrary PyObject. The reason:
`obj.Proxy.__init__.__globals__['os'].system` is a chain of proxy
reads, each one individually "a real read on a real object,"
ending in a call the host would execute. The wasm wall makes
in-sandbox escape worthless, but the danger was never that Python
runs in the sandbox -- it is which host object graph the bridge
will walk ON THE HOST, on request. A generic forwarder rebuilds the
confused deputy at the boundary; a closed op table cannot walk
anywhere it was not explicitly given.

Consequence, stated as scope: host-side Python attribute drill-down
(`obj.Proxy.foo`, sec 1's attribute-access bullet) is the
`unsafe.getattr` permission (sec 3.2): DENY for document principals
-- answering it means executing host CPython over attacker-chosen
names. Files in the field that rely on it lose that capability BY
DESIGN; the session principal may hold it as an explicit, revocable
grant (sec 3.3). This is the one deliberate compatibility break in
the design.

### 7.2 Where the evaluator runs (settling what sec 4 left implicit)

Two placements for the C++ AST walker were on the table:

- **Host-walk (rejected):** the interpreter stays host-side and
  every value-layer operation (each `calc()`, each `Py::Float`)
  crosses into the sandbox. That is a crossing per OPERATOR -- and
  in the browser tier those crossings would be network round trips.
  Dead on both counts.
- **Image-walk (chosen):** compile the expression core itself --
  parser (`ExpressionParser.y`), AST walker (`Expression.cpp`),
  `Base::Quantity`/units -- into the sandbox image, linked against
  the sandboxed CPython exactly as it links the host CPython today.
  One crossing per evaluation request, plus one per external
  reference the bindings pack (7.3) did not pre-resolve. The same
  image is the desktop sandbox (wasmtime) and the browser evaluator
  -- the RoadMap's "same artifact" line made literal. And
  semantics are preserved BY CONSTRUCTION: the same C++ calls the
  same CPython C API, wasm builds of both, which shrinks sec 11's
  migration risk from reimplementation drift to environment drift.

The honest cost, and the main Phase 1 engineering risk:
`Expression.cpp` is coupled to the whole of App today
(`ObjectIdentifier` resolves live `DocumentObject`s,
`ImportModules` observes `ParameterGrp`, console logging, unit
preferences). Phase 0 must carve an `ExpressionCore` build target
behind seams: identifier resolution against a `DocumentAdapter`
interface (host build: the real `Document`; image build: bindings
pack + bridge ops), preferences and logging injected. The seam list
is a Phase 0 deliverable. If the carve-out proves intractable, the
fallback is a host-walk variant with aggressive batching -- but it
forfeits the browser-local story, so the seam work is the plan of
record.

### 7.3 The bindings pack: pre-resolve, do not call back

The host can enumerate an expression's external references without
evaluating it -- `Expression::getIdentifiers()`/`getDeps()`
(`Expression.h:223`) exist and already drive dependency tracking.
So an evaluation request ships `{source, context, bindings}`, where
the bindings pack carries each identifier's current value: by value
where the type allows, as a typed handle where it does not. Cells
that reference cells, and `Box.Volume`-class property reads -- the
overwhelming majority -- then evaluate with ZERO mid-eval
crossings. Mid-eval bridge ops remain only for what cannot be
pre-known: dynamic subscripts, drill-down past a handle, calls on
handles. The host memoizes `(handle, member)` per recompute so a
column of identical references costs one marshal.

### 7.4 The image in three rings

- **Ring 0 -- in-image native, no host access by construction.**
  CPython(wasm); the expression core (7.2); and the `Base` math
  bindings compiled INTO the image -- `VectorPy`, `RotationPy`,
  `PlacementPy`, `MatrixPy`, `BoundBoxPy`, `QuantityPy`, `UnitPy`
  (all declared in `src/Base/*Py.xml`, all pure math with no
  Document/OCCT dependency). Placement/rotation/matrix chains --
  the hot math of real expressions -- run entirely in-image with
  native semantics and zero crossings. Plus the pure stdlib subset
  (`math`, `re`, `collections`, `json`). numpy rides here if the
  image packaging allows (Pyodide: yes; a WASI numpy wheel is the
  riskiest packaging item -- prove it early or scope numpy to
  Pyodide-hosted tiers first).
- **Ring 1 -- generated facades for host-resident types.**
  `Document`, `DocumentObject`, `TopoShape`, and the module-level
  functions of `Part`/`Sketcher`/etc. Every member forwards a typed
  op with a handle; membership is generated and annotated (7.5).
  Geometry authoring stays out of v1 (sec 5), so the initial `Part`
  facade is near-empty -- that is correct, not a gap.
- **Ring 2 -- absent.** Everything else. `os`, `socket`, `ctypes`
  do not exist in the image and no WASI grant supplies them.
  Absence is enforced by image contents plus zero capability
  grants, never by a filter.

### 7.5 Facades and dispatch are GENERATED from the Py XMLs

The fork already declares every native binding in machine-readable
form: the `FooPy.xml` files feeding `generate_from_xml`
(`cMake/FreeCadMacros.cmake:177`; generator under
`src/Tools/generateBase`). Extend that generator with a second
output pair per type:

- `FooProxy.py` -- the in-image class: exactly the declared
  members, each forwarding `(handle, member, args)`.
- `FooDispatch.inc` -- the host-side dispatch table over exactly
  the declared members; an unknown member is a protocol error,
  never a `getattr`.

Plus ONE new per-member annotation in the XML, e.g.
`<Sandbox tier="value|handle|call"/>` -- **absent means DENY.**
New bindings are therefore unreachable-until-annotated by default,
and the security review of the whole bridge collapses to "diff the
annotations." Phase 0's audit seeds the initial set, expected
small: property reads dominate real expressions, plus a handful of
`TopoShape`/`Placement` methods.

Two hard rules inside the dispatcher:

- **The read path never enters host Python.** `read_prop` is
  answered from the C++ property system (`getPropertyByName` ->
  typed marshal; pseudo-properties via `getPseudoProperties`,
  permission-gated per principal -- `_gui` is DENY for document
  principals, sec 3.2). Host CPython executes ONLY for members
  explicitly annotated `call`.
- **Arguments validate against the XML-declared signature** --
  values or handles of the declared type only; a sandbox callable
  is never a valid argument (no host-ward callbacks in v1).
- **Every op carries its principal and passes the sec 3 permission
  check** after the schema check -- the annotation tables say what
  CAN be asked at all; the grant store says what THIS principal may
  ask NOW. Both checks are host-side (sec 3.4).

### 7.6 Wire ops, values, handles

The complete v1 op set: `eval {source, ctx, bindings}`,
`read_prop {h, name}`, `get_attr {h, name}` (facade members only),
`call {h|module, member, args}`, `get_item {h, i}`, `len {h}`,
`release {[h...]}`. Value encoding: tagged binary (CBOR-class) over
null / bool / int / float / str / bytes / list / dict /
quantity{value, unit dims} / vector / rotation / placement /
matrix / handle{id, type}. This is the first shipped slice of
ComputeBoundaries sec 7's object protocol, on purpose.

Handles live in a per-evaluation-context host table: id -> {strong
ref, type tag, tier}; ids random 64-bit; every op validates both id
and expected type tag. The whole table drops when the recompute
transaction ends -- no cross-recompute retention, hence no stale
pointers and no cross-boundary GC protocol; in-image proxies may
batch `release()` early, but end-of-transaction is the guarantee.
Handles are meaningless outside the context that minted them.

### 7.7 Results crossing back (the reverse contract sec 5 lacked)

Today `pyObjectToAny` can hand the host a live PyObject as a
result, and some properties will store it. Under the sandbox that
object lives in another interpreter. v1 rule: **a result must
marshal by value (the sec 5 by-value set; a numpy array flattens to
list/bytes) or be a host handle passed through unchanged. Anything
else is an evaluation error,** not a silent wrapper -- host-held
references to sandbox objects would need cross-boundary GC and a
save-format story, both deliberately excluded. Phase 0 deliverable
(b) audits files in the field for expressions this rule would break
(expected: rare).

### 7.8 What the boundary gives for free

Per-evaluation CPU budget (wasmtime fuel / epoch interruption): a
runaway whitelisted callable currently hangs recompute forever;
under the sandbox it dies at the budget and surfaces as a cell
error. Memory cap per instance. Zero WASI grants in v1 (no fs, no
net, no env). And a parallelism door ComputeBoundaries wants
anyway: one sandbox per document means expression evaluation stops
sharing one GIL -- the first concrete step toward parallel
recompute.

Consistency rule: bridge ops are answered on the host main thread
against current document state, inside the recompute transaction
that issued the evaluation; the dispatcher never triggers a nested
recompute and never runs the event loop. Depth- and queue-capped.
(Not a new hazard -- in-process reads see the same mid-recompute
state today -- but the boundary makes the rule explicit.)

## 8. Minimizing native CPython: the retreat ladder

Added 2026-08-30 (user order): evaluate shrinking the native
in-process CPython to a minimum -- and abandoning it entirely if
possible. The honest frame first: the SECURITY line and the
MINIMIZATION line are different. Security requires that no
document-derived code ever executes on a native interpreter --
delivered at rung 2 below. Everything past rung 2 buys uniformity
(one Python, one story), browser-tier parity, GIL-free
parallelism, and a smaller trusted computing base: worth having,
but engineering, not threat model.

What the native CPython actually serves today (this tree, counted
2026-08-30):

- The expression value layer (sec 1) -- already moving into the
  image (7.2).
- Document-embedded Python: `PropertyPythonObject` payloads and
  scripted objects -- `FeaturePython` carries its script as a
  `PropertyPythonObject Proxy` member (`FeaturePython.h:389`)
  whose execute()/onChanged() dispatch into Proxy attributes.
- Session scripting: the Python console, macros.
- Workbench init and logic: 32 `Init.py` + 31 `InitGui.py` under
  `src/Mod`, plus the pure-Python workbenches (Draft, Arch/BIM).
- The binding surface itself: 213 `*Py.xml` types with 257
  hand-written `*PyImp.cpp` bodies against the CPython C API.
- GUI toolkit bindings: PySide6 and pivy are native extension
  modules over LIVE Qt/Coin objects -- 533 `.py` files under
  `src/Mod` reference PySide, 70 reference pivy.

The ladder -- each rung independently shippable, each strictly
reducing what native CPython does:

- **Rung 0 (this design):** expressions evaluate in the image.
  Native CPython still runs annotated `call` members and
  everything below.
- **Rung 1 -- native dispatch for `call` members.** Retarget the
  annotated method set from "invoke the host binding via the
  Python C API" to direct C++ dispatch generated from the same
  XML. After this NO bridge op executes host Python at all --
  document principals never touch native CPython even indirectly.
  The cost is honest but bounded: the `*PyImp.cpp` bodies are
  hand-written against `Py::Object`, so each ANNOTATED member
  needs a native retarget -- member by member, and only the small
  annotated set, never the whole 257-file surface.
- **Rung 2 -- document-embedded Python moves in.** Scripted-object
  Proxy code and `PropertyPythonObject` payloads run and LIVE in
  the document's sandbox instance; the host keeps only the opaque
  serialized form. `FeaturePython::execute()` becomes a bridge
  call INTO the guest -- ComputeBoundaries' co-resident worker,
  now for real features. This is the rung that closes sec 9's
  `PropertyPythonObject` restore carrier: restore stops
  instantiating host Python objects entirely. Chattiness is
  bounded the same way as expressions: bindings-pack
  pre-resolution plus per-recompute memoization. Class code from
  an installed addon runs as `addon:<name>`; class code carried
  by the file runs as `document:<hash>` -- sec 3 already tells
  them apart. (Refined in 8.2: rung 2 splits by principal --
  addon-class execution may stay native until rung 4.)
- **Rung 3 -- session scripting moves in.** Console and macros
  run in a session-principal sandbox with wide grants. Pure
  FreeCAD-API macros port silently; macros that import
  PySide/pivy declare themselves gui-native and stay on the host
  interpreter at addon-grade trust -- a legacy set that shrinks.
- **Rung 4 -- Python workbenches.** App-side logic (Draft
  geometry and friends) is rung-3-shaped work. The hard residue
  is Gui-side Python: PySide6/pivy bind live Qt/Coin objects, and
  proxying a widget toolkit through a capability bridge is NOT
  viable -- per-event chattiness, callbacks into the guest,
  event-loop ownership, object identity. Two honest paths, both
  compatible with this fork's direction: (a) a native-CPython GUI
  ISLAND that runs only installed addon code, never document code
  -- Python stays as a GUI implementation detail with
  addon-principal accounting; (b) eliminate Python GUI glue over
  time -- the browser tier already has: its chrome is TSX/DOM,
  and it ships neither PySide nor pivy.

**Verdict on abandoning native CPython entirely: possible, and
the browser tier proves the end-state exists -- it already runs
with zero native CPython, its only Python being the sandboxed
image.** On desktop the end-state arrives exactly when the GUI
island empties -- when no shipped addon still needs PySide/pivy
-- a long-horizon deprecation this arc must NOT gate on. What the
arc should do FIRST is the App-core severance (8.1): a
`FREECAD_NO_NATIVE_PYTHON` configuration of the App layer (kernel
+ bridge + image, no libpython link) is the headless-server build
ComputeBoundaries wants anyway, and it turns "did we really cut
it" into a link error instead of an argument. It would also
retire the pivy/site-packages class of environment traps on that
path.

Practicalities that make the ladder affordable:

- Instance-per-principal needs fast spawn: pre-initialized wasm
  memory snapshots (wizer / Pyodide snapshot) are the zygote
  analog -- interpreter startup is paid once, at image build
  time.
- Per-principal fuel/memory quotas (7.8, 3.4) already bound cost,
  so more resident interpreters never mean unbounded footprint.

### 8.1 The App build first? Yes -- the severance IS the audit

Question 2026-08-30: should no-native-Python-in-App be the first
aim? Yes, under a precise definition. The target is NOT "no native
Python in the process"; it is: **the App core (`src/Base` +
`src/App`) links without libpython, and every Python C API use in
the App tier moves into a separate bindings library**
(`FreeCADAppPy`: the generated `*Py` types, the `*PyImp` bodies
UNCHANGED, `PropertyPythonObject` payload handling, `FeaturePython`
dispatch, `DocumentObserverPython`). The core keeps a narrow
provider interface -- `getPyObject()` and friends return through a
registry the bindings lib fills when loaded -- so the desktop
build behaves exactly as today with one more shared lib, and the
worker/headless build simply never loads it.

Why first, not last:

- **Chasing the severed build's link errors is the Phase 0
  chokepoint audit performed by the compiler.** Every site sec 1
  enumerates by hand becomes a hard error that must route through
  a seam -- nothing gets missed by being unglamorous.
- **It is the ratchet.** Once CI builds the severed configuration,
  rungs 1-2 cannot silently regress; a new naked CPython call in
  App is a build break, not a review catch.
- **It is an artifact the roadmap wants anyway**: the OCCT worker
  process (ComputeBoundaries) should never carry an interpreter.

Scope honesty: the invasive edge is the `getPyObject()` surface --
virtuals returning `PyObject*` across the tree get type-erased
behind the provider registry. The `src/Mod` App halves (Part's
`TopoShapePy` and friends) are a second wave with the same
mechanics. In the severed build the expression evaluator has no
native fallback -- it requires the image; the desktop build keeps
both until Phase 1 lands. And until rung 3, `FreeCADCmd`'s
script/test runner still loads the bindings lib: the severed build
is the WORKER artifact, not the CLI.

### 8.2 Coexistence with the GUI island: clean, one honest seam

Can sandboxed App Python and native GUI Python coexist? Yes --
because 8.1 is BUILD layering, not process separation. In the
desktop build the native interpreter moves its home: it is owned
by the GUI island and the bindings lib, both of which link the App
core and call the same C++ they call today. `InitGui.py`, PySide
task panels, pivy scene code run unchanged; `import FreeCAD`
resolves to the bindings lib. Three consequences to state now, not
discover later:

- **Enforcement asymmetry.** Island Python calls C++ directly and
  BYPASSES the bridge dispatcher -- sec 3's checks cannot be
  enforced on it, only attributed. The island is trusted at
  install time, like today; the model's hard guarantees apply to
  sandboxed principals. This is sound because the threat model is
  document-derived code, which never runs on the island.
- **Two interpreters, meeting only at the bridge.** The island's
  CPython and the wasm instances share no objects. Once rung 2
  puts a Proxy into a sandbox, island code that reads `obj.Proxy`
  (Draft does, pervasively) gets a REVERSE proxy: a native stub
  whose attribute reads and calls cross into the guest through
  the same dispatcher, same op set, same accounting. That reverse
  stub is the ONE new mechanism coexistence requires. (The island
  releases its GIL around bridge calls into a guest.)
- **Rung 2 splits by principal (refinement).** In a normal
  scripted object the Proxy CLASS is installed addon code; the
  document contributes only serialized STATE. So rung 2's security
  payload is precisely: (a) restore-time class lookup and
  instantiation constrained to REGISTERED addon classes -- this
  alone retires sec 9's arbitrary-import carrier -- and (b)
  genuinely document-carried code running in the document sandbox.
  Addon Proxy EXECUTION may stay on the island until rung 4,
  which keeps today's `obj.Proxy`/`vobj.Proxy` pairs (one addon
  module, App and Gui halves sharing state) in ONE interpreter
  instead of splitting an addon across two worlds prematurely.

## 9. What this does NOT close (added 2026-08-29)

Expressions are one carrier of "opening a malicious `.FCStd` runs
code" -- not the only one, and the others fire EARLIER:

- `PropertyPythonObject::Restore`
  (`src/App/PropertyPythonObject.cpp:379`) calls
  `PyImport_ImportModule(<the document's "module" attribute>)` at
  restore time -- top-level code of any importable module, chosen
  by the file. The legacy pickle branch below it is worse:
  `PyObject_CallObject(mod.getAttr(cls))` -- a document-chosen
  zero-argument callable from any importable module.
- `Base::Type::importModule` (`Type.cpp:85`) auto-imports a module
  derived from a document object's type string.

Both run at document open, before any expression evaluates, and the
expression sandbox never sees them. Phase 1's acceptance test
therefore proves the EXPRESSION carrier closed; RoadMap ws3 as a
whole additionally needs a restore-time execution audit
(`PropertyPythonObject`, type auto-import, anything else that
imports or calls during restore). That audit is a separate line
item, not part of this design -- but this doc must not claim the
whole hole closed while a file can still run code without ever
touching an expression.

Sec 8's rung 2 is the designed closure for the first carrier: once
document-embedded payloads live in the document's sandbox, restore
stops instantiating host Python objects at all. Until that rung
ships, the audit above stands.

## 10. Sequencing (this is the point of doing it first)

STATUS 2026-08-31: Phase 1 is DONE (the desktop sandbox, its
acceptance test included) and Phase 2 is DONE for its first consumer.
The same image loads and evaluates in a real browser, with its own WASI
implementation and its own acceptance page (docs/ExpressionImage.md
"The browser tier"), and the browser spreadsheet uses it to evaluate a
formula locally while the host's recompute is still in flight
(docs/SpreadsheetRemote.md sec 6) -- pure-data cells with no host at
all, exactly as this section describes. The mid-eval bridge stays
deliberately unattached in the browser; the pack-first design is what
makes that sufficient.

STATUS 2026-08-30: Phase 0 is DONE -- the audits, the frozen
contracts, the measurement, and the architecture-A verdict are in
docs/ExpressionSandboxPhase0.md (rigs: scripts/expr-phase0/).

- **Phase 0 -- boundary audit + marshalling contract.** Enumerate
  every site where the C++ interpreter enters CPython (the sec 1
  chokepoints: `getModule`, callable execution, `getPyValue` on
  Python-backed attrs, pseudo-property wrappers). Fix the by-value vs
  by-proxy type split. Deliverable: the object-protocol slice the
  sandbox needs -- reusable by process-per-document. Three more
  deliverables (added by the re-evaluation): (a) the
  annotated-member audit -- which type/module members expressions in
  real files actually touch, seeding the sec 7.5 tables; (b) the
  reverse-result audit -- which expressions produce results that are
  not by-value-marshalable (sec 7.7; expected rare); (c) THE
  MEASUREMENT, promoted from the open questions: native value layer
  vs CPython-wasm on a large real sheet AND a bound-property-heavy
  assembly, before any bridge code exists -- architecture A's
  position (Phase 1 prerequisite vs later accelerator) is an OUTPUT
  of that number, not an opinion. Also the `ExpressionCore` seam
  list (sec 7.2). And (d): freeze the v1 permission catalog, the
  grant-store schema, and the document-hash canonicalization
  (sec 3).
- **Phase 1 -- desktop sandbox = the security fix.** Stand up the
  CPython-wasm image under a desktop wasm runtime; route the
  chokepoints through it; delete the name-matched allowlist in favour
  of the sec 3 permission service (grant store, non-modal prompt
  surface, headless policy flags). **Acceptance test:** a `.FCStd` whose
  expression does `open('/etc/passwd')`, `import os`, or
  `App.getDocument().Objects[0].Proxy...` cannot reach the host FS,
  network, or memory; AND the grant side works: a `doc.foreign`
  expression fails with `PermissionNeeded`, succeeds after the
  grant, fails again after revoke -- verified in the harness, not
  argued. This closes the EXPRESSION carrier of ws3 -- not all of
  ws3; sec 9 lists the restore-time carriers that remain outside
  this design.
- **Phase 2 -- browser reuse.** Ship the same image in the viewer.
  Pure-data sheets (cells referencing cells) evaluate locally in the
  sandbox with no host at all; a cell that references `Box.Volume`
  proxies to the host over the existing control channel -- the same
  boundary as desktop. Now docs/SpreadsheetRemote.md's `sheet.set`
  lands on a sandbox that is already the security boundary, and the
  viewer can evaluate formulas locally where the data allows.
  Authority rule: host recompute stays the source of truth; local
  evaluation is latency-hiding preview, reconciled when the host's
  `sheet.changed` lands -- two evaluators, one truth.
- **Beyond -- sec 8's ladder, rung by rung.** Rung 1 (native
  dispatch for `call` members) and rung 2 (document Proxy code into
  the sandbox) are the next security payloads; rung 2 also retires
  sec 9's first restore-time carrier. The App-core severance (8.1)
  starts alongside Phase 1; once its build is in CI it keeps rungs
  1-2 from regressing.

The ordering the user asked for is right: the sandbox is a
prerequisite of the sheet-in-wasm work, not a parallel track. Built
first, it turns "ship the spreadsheet to the browser" from "expose the
current in-process Python hole to the web" into "run the already-
sandboxed evaluator in one more host."

## 11. Open questions to resolve before building

- **Proxy granularity vs recompute cost** -- is per-attribute
  round-tripping fast enough for a large sheet, or is architecture A
  (C++ value path) a Phase 1 prerequisite rather than a later
  optimization? Measure on a real geometry-referencing sheet before
  committing. (Promoted to Phase 0 deliverable (c); the bindings
  pack, sec 7.3, is the designed answer to the hop count -- the
  measurement decides whether it suffices.)
- **Interpreter lifecycle** -- one sandbox per document? per
  recompute? pooled? The wasm interpreter's startup cost sets
  this. Direction after sec 8: one instance per principal, from a
  pre-initialized wasm memory snapshot (the zygote analog); open
  remainder: the snapshot tooling.
- **Pseudo-property policy defaults** -- sec 3.2 fixes the category
  defaults (`gui` DENY, `app.query` PROMPT for document principals);
  remaining: the per-member annotations for `_self`/`_shape`/
  `_pla`/`_matrix`.
- **`dbind`/`href`/`getvar`** and the other engine builtins (the wiki
  lists them; `getvar` is already disabled for security,
  `Expression.cpp:3309`) -- audit each against the proxy model.
- **Migration** -- old files carry expressions written against the
  in-process semantics; the sandbox must evaluate them identically or
  the change breaks existing documents. This is the compatibility
  constraint that makes CPython-on-wasm (not MicroPython) non-
  negotiable. Sec 7.2's image-walk narrows the residual risk to
  environment drift (wasm float behaviour, CPython version) -- the
  same C++ evaluator runs against the same CPython, wasm builds of
  both. Secs 7.1/7.7 name the two DELIBERATE breaks: `obj.Proxy`
  drill-down for document principals, and non-marshalable results.
- **Document-hash canonicalization** -- exactly which bytes feed the
  principal hash (expression strings, embedded payloads) and how it
  stays stable across a no-op resave. A wrong answer means grants
  that randomly expire, or that survive tampering.
- **Prompt UX** -- the blocked-cell affordance, the per-document
  permission panel, the user-gesture rule for grant requests; a
  prompt must never modal-block a recompute.
- **Addon principal granularity** -- per addon, per Python module,
  or per workbench? Sets how rung-3/4 grants are keyed (sec 8).
