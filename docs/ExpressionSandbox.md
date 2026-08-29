# Sandboxing the expression engine's Python (before the sheet goes to wasm)

Ordered 2026-08-29, broadening docs/SpreadsheetRemote.md. The user's
sequencing: **do not bring the spreadsheet to the browser until the
expression engine's Python interop is sandboxed.** The sheet is the
tightest expression consumer; bringing the sheet to wasm means
bringing expression evaluation to wasm; if that evaluation's Python
runs in a wasm sandbox, the same sandbox is (a) a real security fix on
desktop and (b) the thing that safely ships to the viewer. This is a
design pass -- nothing here is built.

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

## 3. The boundary that actually closes it

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
  contents plus policy*, not a string match.
- `CallableExpression` executing a Python callable -> "run this call
  in the sandbox"; arguments marshal across (sec 4); the whole
  `checkCallable` name-attribution machinery is **deleted** -- there
  is nothing to attribute, the callable simply cannot reach the host
  except through bridged proxies.
- `ObjectIdentifier::getPyValue` on a Python-backed attribute ->
  "resolve this attribute on host proxy H"; the host runs the real
  read on the real object and returns data or another proxy.

This is exactly ComputeBoundaries' "Python + the module co-resident in
the worker": the expression sandbox is **the first and smallest
co-resident worker** -- it hosts the Python value layer and reaches
geometry through the object protocol. The protocol it needs is the
same "semantic, schema'd, transactional object-operation protocol"
the process-per-document goal converges on (ComputeBoundaries sec 7),
so this is plumbing the roadmap wants anyway, not a detour.

## 4. The marshalling contract (the core design work)

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
- **Policy is a capability, and it can be *tighter* than today.**
  `_gui` -- the entire GUI command surface -- should almost certainly
  be **denied** to sandboxed document expressions; the current
  allowlist cannot express that. Same for `_app.getDocument(other)`
  reaching sibling documents. The sandbox lets the surface *shrink*,
  not just relocate.
- **Trust tiers become expressible.** A downloaded `.FCStd`'s embedded
  expressions and the user's own live-session macros are not equally
  trusted. A capability policy can run untrusted-document expressions
  locked down while granting the user's own session more -- a
  dimension the binary allowlist has no way to state.
- **Geometry authoring stays in the host (v1 scope line).** If a
  sandboxed callable wants to *build* a shape (cadquery), it would
  need OCCT inside the sandbox -- which pulls in the entire kernel and
  is out of scope. v1: the sandbox reads properties and calls
  host-side methods through proxies; it does not author B-rep. This is
  what keeps the sandbox image small and OCCT in one place.

## 5. Runtime choice

The isolation must come from **wasm/WASI memory+syscall confinement**,
not from restricting the Python dialect.

- **CPython-on-wasm (Pyodide-class image): recommended.** Preserves
  exact Python semantics (the value layer, numpy, `freecad.fc_cadquery`
  all keep working), RoadMap-named, browser-proven. Heavy (multi-MB +
  interpreter startup), but the weight buys semantic fidelity, and the
  isolation is the wasm boundary, not a neutered interpreter -- which
  is precisely the point: stop trying to make Python safe from inside,
  deny it the host from outside.
- **MicroPython/WAMR: rejected for v1.** Small and fast, but an
  incomplete stdlib and non-CPython semantics would break shipped
  whitelisted modules (cadquery) and the value-layer behaviour files
  in the field rely on. Viable only if a reduced expression dialect
  were acceptable -- it is not, given what already ships.
- **Subinterpreters / free-threaded CPython (PEP 684/703): not a
  security boundary.** They solve the GIL for parallelism
  (ComputeBoundaries) but share the address space -- no isolation.
  Orthogonal to this problem.
- **Desktop host = a wasm runtime (Wasmtime / WAMR + WASI); browser
  host = the browser's own wasm engine.** One image, two hosts -- the
  RoadMap's "unify the sandbox and browser stories." The desktop
  security fix and the viewer's evaluator are literally the same
  artifact.

## 6. Sequencing (this is the point of doing it first)

- **Phase 0 -- boundary audit + marshalling contract.** Enumerate
  every site where the C++ interpreter enters CPython (the sec 1
  chokepoints: `getModule`, callable execution, `getPyValue` on
  Python-backed attrs, pseudo-property wrappers). Fix the by-value vs
  by-proxy type split. Deliverable: the object-protocol slice the
  sandbox needs -- reusable by process-per-document.
- **Phase 1 -- desktop sandbox = the security fix.** Stand up the
  CPython-wasm image under a desktop wasm runtime; route the
  chokepoints through it; delete the name-matched allowlist in favour
  of the capability policy. **Acceptance test:** a `.FCStd` whose
  expression does `open('/etc/passwd')`, `import os`, or
  `App.getDocument().Objects[0].Proxy...` cannot reach the host FS,
  network, or memory -- verified, not argued. This closes RoadMap
  ws3.
- **Phase 2 -- browser reuse.** Ship the same image in the viewer.
  Pure-data sheets (cells referencing cells) evaluate locally in the
  sandbox with no host at all; a cell that references `Box.Volume`
  proxies to the host over the existing control channel -- the same
  boundary as desktop. Now docs/SpreadsheetRemote.md's `sheet.set`
  lands on a sandbox that is already the security boundary, and the
  viewer can evaluate formulas locally where the data allows.

The ordering the user asked for is right: the sandbox is a
prerequisite of the sheet-in-wasm work, not a parallel track. Built
first, it turns "ship the spreadsheet to the browser" from "expose the
current in-process Python hole to the web" into "run the already-
sandboxed evaluator in one more host."

## 7. Open questions to resolve before building

- **Proxy granularity vs recompute cost** -- is per-attribute
  round-tripping fast enough for a large sheet, or is architecture A
  (C++ value path) a Phase 1 prerequisite rather than a later
  optimization? Measure on a real geometry-referencing sheet before
  committing.
- **Interpreter lifecycle** -- one sandbox per document? per
  recompute? pooled? The wasm interpreter's startup cost sets this.
- **Pseudo-property policy defaults** -- exact allow/deny for
  `_self`/`_app`/`_gui` under the untrusted-document tier.
- **`dbind`/`href`/`getvar`** and the other engine builtins (the wiki
  lists them; `getvar` is already disabled for security,
  `Expression.cpp:3309`) -- audit each against the proxy model.
- **Migration** -- old files carry expressions written against the
  in-process semantics; the sandbox must evaluate them identically or
  the change breaks existing documents. This is the compatibility
  constraint that makes CPython-on-wasm (not MicroPython) non-
  negotiable.
