# Expression sandbox -- Phase 0 report (boundary audit + contracts + measurement)

> **Superseded 2026-09-03 by `docs/Sandbox.md`**, the consolidated
> sandbox reference audited against the code. This file is kept as the
> historical record; `Sandbox.md` sec 14 says which of its sections
> moved where and which statements here are stale. Do not update this
> file; update `Sandbox.md`.


Date: 2026-08-30. Branch: SecurePython. This document is the Phase 0
deliverable set defined by docs/ExpressionSandbox.md sec 10: (1) the
boundary audit, (2) the ExpressionCore seam list, (a) the
annotated-member audit, (b) the reverse-result audit, (c) THE
MEASUREMENT with its architecture verdict, and (d) the frozen v1
permission catalog, grant-store schema, and document-hash
canonicalization. Section numbers of the design doc are cited as
"ES sec N".

Everything below is code-verified against this tree (file:line cited)
or measured on this box (RelWithDebInfo conda-relwithdebinfo-801
build, OCCT 8.0.1). The expression corpus used for (a)/(b) is every
.FCStd under ~/works: 456 documents, 7628 stored expressions, 362
unique strings (extraction and analysis rigs are reproducible;
see sec 3.1).

## 1. Boundary audit: where the interpreter enters CPython

Ground truth first: **the parser and lexer are already CPython-free.**
ExpressionParser.y, ExpressionParser.l, the generated .tab.cc, and
ExpressionTokenizer.* contain zero Py references; all CPython contact
is in the AST walker (Expression.cpp) and identifier resolution
(ObjectIdentifier.cpp). The ES sec 7.2 image-walk can lift the front
end untouched.

There is no non-Python AST node: ExpressionParser.h declares 33
_getPyValue overrides; every node type evaluates to a PyObject.

### 1.1 The chokepoints (complete; everything else is downstream)

  C1  Expression.cpp:1614-1648   getValueAsAny / getPyValue /
      _getPyValue -- THE evaluation entry pair.
  C2  Expression.cpp:2015-2142   calc() -- all binary/unary arithmetic
      and all comparisons (PyNumber_*, PyObject_RichCompareBool,
      PySequence_Contains, PyUnicode fast paths). Replacing calc()
      alone covers every operator in the language.
  C3  Expression.cpp:591-693     pyObjectToAny / pyObjectFromAny /
      pyToQuantity / pyFromQuantity / expressionFromPy -- the whole
      value marshal, five functions.
  C4  Expression.cpp:2588-2599   ImportModules::getModule -- all
      policy-checked imports. PLUS three out-of-band importers that
      BYPASS the allowlist: EvalFrame::getBuiltin (1177, imports and
      caches the whole builtins dict), checkCallable's inspect import
      (2620), and ObjectIdentifier's GET_MODULE macro
      (ObjectIdentifier.cpp:2068-2078, expanded for FreeCAD,
      FreeCADGui, Part, freecad.fc_cadquery, re, builtins, math,
      collections).
  C5  Expression.cpp:5108-5115   Py::Callable::apply -- the single
      user-callable invocation site, gated by securityCheck (5064).
  C6  ObjectIdentifier.cpp:2052-2290  access() -- all identifier
      value resolution, including the generic drill-down loop at
      2264-2288.
  C7  ObjectIdentifier.cpp:734-812    Component::get/set/del -- the
      generic getattr/setattr/delattr and all subscripting
      (PyObject_GetAttr at 739 IS `obj.Proxy.foo`).
  C8  ObjectIdentifier.cpp:2068-2119  GET_MODULE + PseudoShape
      materialization (calls Part.getShape on a live object).
  C9  Expression.cpp:3999-4032   PyIterable -- all iteration
      (7 construction sites: unpack, *args x2, comprehensions,
      list splat, for-statements, prepareCommands).
  C10 Expression.cpp:1174-1191, 1230-1237  getBuiltin + Python-mode
      name binding -- the builtins surface ("#@pybegin" toggles it,
      6337-6341).
  C11 Expression.cpp:7286-7316, 1095-1101  exception construction and
      except-clause matching -- try/except is implemented ON TOP of
      CPython exception type objects (PyObject_IsSubclass).
  C12 Mod/Spreadsheet/App/Sheet.cpp:796-816  setObjectProperty -- the
      one place an evaluation result is persisted as a live PyObject
      property (App::PropertyPythonObject; call sites Sheet.cpp:881,
      903).

getPyValue call sites outside Expression.cpp (each one is a consumer
the reroute must keep working): DocumentObjectPyImp.cpp:373
(evalExpression -- a direct scripting entry into the whole engine),
Gui/ExpressionCompleter.cpp:1497/1554/1700 (the completer EVALUATES
identifiers, including pseudo-properties, while the user types),
Mod/Spreadsheet/App/Cell.cpp:1459-1845 (edit-mode callbacks: a cell
can invoke a user callable), Cell.cpp:1956, Sheet.cpp:881/903,
PropertySheet.cpp:2348 (double-binding write-back).

### 1.2 The by-value / by-proxy split as the code has it today

pyObjectToAny (Expression.cpp:591-622), branch order:
None -> empty any; QuantityPy -> Base::Quantity; PyFloat -> double;
PyLong -> long; PyUnicode -> std::string; **everything else -> a
GIL-guarded held PyObject** (PyObjectWrapper, 520-548).

So the by-value set today is exactly {Quantity, double, long,
string}. Notable: bool lands in the PyLong branch and LOSES its
identity (True becomes long 1); list/dict/tuple -- everything a
structured cell holds -- cross as live PyObjects; Vector, Matrix,
Placement, Rotation cross as live PyObjects despite being pure Base
math. The reverse direction (_pyObjectFromAny, 560-583) accepts more
inbound types (int, float, bool, const char*) than the forward
direction ever produces.

Consequence for the ES sec 5/7.6 wire contract (confirmed): the wire
value set MUST add bool, list/dict/tuple-of-values, and the Base math
types (vector/rotation/placement/matrix/boundbox) as first-class
by-value encodings, else nearly every structured result degrades to a
handle. The ES sec 7.6 tagged encoding already lists these; this
audit confirms none can be dropped.

### 1.3 Wire-protocol additions Phase 0 discovered

Two things ES sec 7.6's op set does not cover, found by the audit:

- **Exception types cross the boundary.** try/except matching runs
  PyObject_IsSubclass against CPython exception type objects
  (C11), and JumpStatement re-raises stored exception instances
  (6402-6410). The value encoding needs an error type: at minimum
  {exc_type_name, message, (optional) value payload}, with the
  in-image CPython supplying real exception semantics and only the
  SURFACED result (cell error) marshalling out. Host-side code that
  catches specific Python exception types from expression evaluation
  must switch to matching the marshalled type name.
- **RangeExpression calls back into the owner object.**
  getRange (Expression.cpp:5646-5673) invokes
  owner.getCellFromAlias(...) through Python mid-resolution. Under
  the image-walk this is either a bindings-pack pre-resolution (the
  host can enumerate ranges before eval -- preferred) or one typed
  bridge op (resolve_alias). Not a generic call.

### 1.4 What the current filter structurally cannot see (exact)

Recorded because it is the concrete justification for ES sec 2, now
enumerated rather than argued:

- The builtin denylist is EIGHT names (Expression.cpp:4892-4903):
  eval, execfile, exec, __import__, file, open, input, setattr.
  Everything else in builtins is reachable and attributed to module
  "builtins" -- which is in the default allowlist (2516). getattr,
  globals, vars, compile, type, __build_class__, breakpoint are all
  callable today.
- `_py` (PseudoBuiltins, ObjectIdentifier.cpp:2097-2099) hands over
  the whole builtins module without consulting ImportModules at all;
  same for every GET_MODULE pseudo-property (C8).
- Attribute reads are unchecked unless the base object is a module
  (securityCheck overload guard, Expression.cpp:4929). The
  obj.Proxy.__init__.__globals__ walk is a sequence of unchecked
  generic reads (C7). The intended defence -- a module check on
  traversal results -- exists but is COMMENTED OUT
  (ObjectIdentifier.cpp:762).
- Callable attribution trusts attacker-settable metadata: __module__,
  __self__, tp_name, inspect.getmodule (Expression.cpp:2616-2765).
  Any object that spoofs __module__ = "math" is attributed to math.
- The allow-verdict cache is keyed on raw (PyObject*, PyTypeObject*)
  address pairs (2422); a freed denied callable's address can be
  reused by an allowed object (acknowledged in the comment at
  2418-2420, only mitigated).
- Expression-defined callables (ExpressionPy) bypass securityCheck
  wholesale (4914), and ExpressionPy installs tp_call
  (ExpressionPyImp.cpp:57) -- host CPython can call back INTO the
  walker, the reverse edge ES sec 7.7 must refuse.
- The module allowlist is exact-match on import (2588) but
  PREFIX-match on dotted boundaries for callables (2751-2765):
  allowing "Part" allows "Part.anything.deeper".

Latent non-security bugs noticed in passing (not fixed here, both
pre-existing): calc()'s OP_MOD non-inplace branch also calls
PyNumber_InPlaceRemainder (Expression.cpp:2133-2135), and
ObjectIdentifier::Component::del falls through to an unconditional
throw after a successful delete (ObjectIdentifier.cpp:811).

## 2. ExpressionCore seam list (ES sec 7.2 deliverable)

Headline: the carve-out is tractable and well-bounded. The grammar is
core-clean already; ~7800 lines of Expression.cpp have ~15 concrete
Document/Property touch sites; nearly all Document coupling
concentrates in FOUR functions of ObjectIdentifier.cpp (resolve,
resolveProperty, access, getDocumentObject). Zero OCCT includes, zero
Gui:: symbols, zero out-of-tree Expression subclasses (all 18+ node
types live in ExpressionParser.h; nothing in src/Mod subclasses
Expression or ExpressionVisitor). PropertyExpressionEngine stays
host-side by design. 43 files include Expression.h/ExpressionParser.h
-- that is the blast radius of signature changes, arguing for typedef
seams over raw substitution.

The seams (host build backs them with the real objects; image build
with the bindings pack + bridge ops):

  S1  DocumentAdapter -- identifier resolution. All in
      ObjectIdentifier.cpp: resolve() 1049-1204,
      getDocumentObject() 989-1038 (name-then-label linear scan),
      getDocument() 1205-1252 (GetApplication().getDocument +
      label scan = the Doc#Obj cross-document path),
      resolveProperty() 1651-1775 (getSubObject walk,
      getPropertyByName, pseudo-property sentinel). ResolveResults
      (ObjectIdentifier.h:481-491) stores raw Document*/
      DocumentObject*/Property* -- becomes opaque handles.
      ObjectIdentifier.h already forward-declares all App types;
      the header is clean, the coupling is .cpp + member types.
      Adapter surface: resolveDocument, resolveObject,
      resolveSubObject, resolveProperty, isRemoved,
      objectInternalName, objectLabel, documentName, ownerDocument.
  S2  Value read path -- access() (ObjectIdentifier.cpp:2052-2295)
      and the pseudo-property table (1596-1641: a static
      PropertyContainer with 15 addDynamicProperty calls -- becomes
      a constexpr name->enum table). _pla/_matrix/__pla/__matrix
      reduce to DocumentAdapter::accumulatedMatrix(obj, subname,
      includeLinks) -> Base::Matrix4D, which cuts the App::Link
      dependency (2160-2163) out of the core entirely. _shape/_self/
      _ref/_app/_part/_gui/_cq route to Ring-1 facades or are absent
      per policy. Read/write leaf: getPathValue/getPyPathValue/
      setPyPathValue become adapter readPath/writePath.
  S3  Dependency tracking / write-back / document maintenance.
      ExpressionDeps and Dependencies are keyed on raw
      DocumentObject* (Expression.h:56, ObjectIdentifier.h:394) --
      re-key on handles/{doc,obj} names. getDep currently EVALUATES
      the access chain under the GIL to discover dependencies
      (ObjectIdentifier.cpp:1305-1321) -- split a structural
      (resolve-only) enumeration off the Python path; the host needs
      it anyway to build the ES sec 7.3 bindings pack without
      evaluating. adjustLinks/updateElementReference/importSubNames/
      replaceObject/moveCells/offsetCells/transposeCells are
      document-maintenance, never evaluation: compile OUT of the
      core into a host-only ExpressionDocumentOps TU. That single
      move drops App/PropertyLinks.h from Expression.h (its only
      reason is ExpressionModifier + getPropertyLink,
      Expression.h:65-111).
  S4  App::Application -- exactly three call sites total:
      Expression.cpp:2539 (preferences, folds into S5) and
      ObjectIdentifier.cpp:1213/1219 (document lookup, folds into
      S1). Then #include <App/Application.h> is deletable.
  S5  ModulePolicy -- ImportModules (Expression.cpp:2513-2787) is a
      ParameterGrp observer singleton over
      "BaseApp/Preferences/Expression/PyModules" with a hardcoded
      18-entry default list. Becomes an injected interface
      {isModuleAllowed, isCallableAllowed, lockPolicy,
      onPolicyChanged}; in the image it is a frozen capability set
      -- the module either exists in the image or does not (ES sec
      7.4 Ring 2 absence made structural). ImportParamLock's
      property (a called function cannot rewrite policy under
      itself) must be preserved. Under the ES sec 3 model this whole
      interface is subsumed by the permission service; the seam is
      how the legacy path keeps working until then.
  S6  Units -- Base::Unit/Quantity are genuinely Ring-0-ready
      (Quantity parses via its own generated lexer). Two snags:
      Quantity reads UnitsApi::getDecimals()/schemaTranslate, whose
      state is file-scope statics PUSHED by Application/Gui startup
      (UnitsApi.cpp:66-69) -- so inject a UnitsConfig {schema,
      decimals} with the eval request; and UnitsApi.cpp's Qt
      translation calls (getDescription) split to a host-only TU.
  S7  Logging -- FC_LOG_INSTANCE file-statics + ~12 emission sites;
      injected sink. Base/Tools.h drags QString into both core TUs
      for one escapeEncodeString call -- localize it.
  S8  GUI -- structurally absent. Only the strings "FreeCADGui"/
      "Gui" in the default allowlist and the _gui pseudo-property.
  S9  OCCT/Part/Mod -- absent. ObjectIdentifier.cpp's
      ComplexGeoData.h include is stale (no symbol used) and
      deletable today.
  S10 Singletons and globals the image must own explicitly:
      Base::Interpreter().getVariable (one site, 3305, feeding the
      already-disabled getvar -- delete); Base::Sequencer()
      .checkAbort in loops (6613 -- becomes wasm fuel, strictly
      better); 20+ Base::Type registrations currently init'ed from
      Application.cpp:2456-2481 (core needs its own initTypes entry
      point); the non-reentrant parser statics (Context::busy,
      ScannerBuffer), the global EvalFrame stack, the PyObjectNode
      cache, _DocumentMap, the XMLReader static (persistence:
      compile-out, the image receives parsed source).

Target composition -- IN the core: Expression.cpp/.h (minus S3
maintenance), ExpressionParser.y/.l/.h + generated, ObjectIdentifier
(minus persistence + link ops), App/Range (already Base-clean),
Base Unit/Quantity + the seven *Py math bindings, Base Exception/
BaseClass/Type, PyCXX. OUT (host-only): ExpressionTokenizer (Qt),
ExpressionVisitors.h, ExpressionPyImp.cpp, PropertyExpressionEngine,
maintenance ops, persistence, UnitsApi Qt parts.

Cheap wins available now, independent of the sandbox: delete the
stale ComplexGeoData include; constexpr pseudo-property table;
structural getDep; localized escapeEncodeString.

## 3. Deliverable (a): the annotated-member audit (real-file usage)

### 3.1 Corpus

Extractor + analyzer rigs (stdlib-only Python) walk every .FCStd
under ~/works, read Document.xml/GuiDocument.xml from the zip, and
collect PropertyExpressionEngine <Expression expression=...> entries
plus spreadsheet <Cell content="=..."> cells. Result: 456 documents,
7628 expressions, 362 unique after dedup (the King IFC model
contributes 1742 x 4 copies; the CNC machine project ~200 across a
federated multi-file set; scanner.FCStd 91; CAM/Path fixtures the
long tail). Non-ASCII object names (Cyrillic) appear in real files
and the identifier path must keep handling them.

### 3.2 What real expressions actually touch

Unique-expression classification (one expression can hit several):
member reads 302, engine-builtin calls 57, foreign-document
references 69 (nearly all the CNC set: cross-file config tables --
ES sec 3.2's doc.foreign PROMPT will be exercised by real users, it
is not a theoretical case), pure literal/unit arithmetic 34,
pseudo-property use 1, dotted module call 1.

Functions called, entire corpus: hiddenref (29), dbind (17), str
(10), tuple (7), trunc (6), tan (4), atan (2), sum (2), vector (1),
ceil (1), plus ONE _math.degrees and ONE Vector.getAngle. No
import_py, no eval, no #@pybegin, no user module callables, no
.Proxy drill-down ANYWHERE in 456 real documents. The ES sec 7.1
deliberate break (deny unsafe.getattr to documents) breaks zero
files in this corpus.

Members read (top, with binding counts across all copies):
Constraints[i] (sketcher driven-constraint binding), Configuration
(assembly config enums), Length/Angle/Offset/Height/Width (quantity
properties), Placement/Rotation/Base and .x/.y/.z leaves,
cells[range] (sheet ranges), .Shape.BoundBox.{ZMin,XLength,YLength,
ZLength} (King: 5808 bindings -- BIM elevation/overall-size
bindings), one .Shape.Wire1.Edge1.Length (scanner: sub-shape
drill-down to a geometric property).

### 3.3 Seed tier annotations (ES sec 7.5 <Sandbox tier=.../>)

  value:  every numeric/bool/string/enum property read the corpus
          shows (PropertyQuantity/Float/Integer/Bool/String/
          Enumeration), Placement/Rotation/Vector/Matrix properties
          (wire has them by value), BoundBox (six floats),
          Constraints[i] value reads, sheet cell reads.
  handle: DocumentObject, Document, TopoShape (.Shape), Shape
          subelements (Wire1/Edge1 -- resolved host-side, only the
          requested leaf value crosses).
  call:   Sheet.getCellFromAlias (or bindings-pack pre-resolution,
          sec 1.3); nothing else observed. Vector.getAngle and
          math.degrees are Ring-0 in-image and need no annotation.

Everything else stays unannotated = DENY, per ES sec 7.5. The
observed surface is as small as the design hoped: the initial
generated facade set is Document/DocumentObject/TopoShape reads plus
one call.

## 4. Deliverable (b): the reverse-result audit

Rule being audited (ES sec 7.7): a result must marshal by value or be
a host handle passed through; anything else is an eval error.

Corpus verdict: **zero stored expressions in 456 documents produce a
non-marshalable final result.** Every engine binding lands in a
scalar/quantity/string/enum property; every .Shape/.BoundBox use is
an INTERMEDIATE hop ending in a float. The expected-rare answer is
in fact never, for this corpus.

Two engine mechanisms need explicit treatment (they are the reason
the rule needs stating at all):

- Spreadsheet structured cells: Sheet::setObjectProperty (C12)
  persists a non-scalar result as PropertyPythonObject. Corpus
  instances: cells[range] reads and tuple(...) results whose
  elements are all values -- marshal as list/tuple-of-values, fine.
  A cell result that holds a live object (possible today, unused in
  the corpus) becomes an eval error under the rule, matching the
  design.
- dbind()/hiddenref()/tuple(.cells,...) produce engine-internal
  binding objects consumed by PropertySheet double-binding
  (PropertySheet.cpp:2348) and Cell edit callbacks. These are
  engine builtins, not marshalled results; Phase 1 must keep their
  evaluation inside one evaluator (they never cross the wire as
  values). Assembly3's dbind-heavy files (scanner: 73 bindings) are
  the compatibility gate here.

## 5. Deliverable (c): THE MEASUREMENT and the architecture verdict

Rigs: (i) FreeCADCmd bench on this box -- per-class evalExpression
(parse+eval) loops, 10k-cell synthetic arithmetic sheet, 2k-cell
property-referencing sheet, and all 73 stored expressions of the real
scanner.FCStd assembly evaluated in place; (ii) twin CPython
workloads native (box python 3.8.10) vs CPython-on-wasm (pyodide
0.29/Python 3.13 under node 19 -- version skew noted where it
matters); (iii) the JS<->wasm hop cost.

Native engine (RelWithDebInfo, this box):

  arith 1+2*3-4/5 (parse+eval)          2.2 us
  quantity 10mm+5mm*2                   2.4 us
  sin(30deg)                            2.5 us
  Box.Height (property read)            4.9 us
  Box.Placement.Rotation.Angle          3.1 us
  vector(1,2,3).Length                  4.1 us
  Box.Shape.Volume                    175   us   <-- .Shape access
  Box.Shape.BoundBox.ZMin             183   us   <-- dominates: the
      TopoShapePy materialization, in-process, TODAY
  10k-cell arithmetic sheet recompute  13.2 us/cell (10.2 cached-AST)
  2k-cell Box.Shape.Volume sheet      180   us/cell
  scanner.FCStd all 73 real exprs       8.1 us mean, 29 us max

CPython-wasm vs native CPython (same workloads):

  float ops / attr+method              ~1.0-1.2x  (parity)
  math.sin                              2.3x
  precompiled eval dispatch            13x   (0.08 -> 1.02 us;
      inflated by 3.8-vs-3.13 skew, treat as upper bound)
  compile+eval per call                 5.4x (3.1 -> 16.8 us)
  JS->wasm-python eval request         22 us round trip
  wasm-python -> JS host hop            3.0 us per op
  pyodide cold start                 1523 ms

Projection for the image-walk design (C++ evaluator + CPython
compiled to wasm; wasm compute penalty bounded by the measured
1x-5.4x band, call it 2-3x for AOT-compiled C++):

- Arithmetic majority: ~2.2 us -> ~5-7 us per cell-class eval, one
  boundary crossing per EVALUATION REQUEST (batchable per
  recompute). A 10k-cell sheet projects to ~0.3-0.5 s sandboxed vs
  0.13 s today. Acceptable for the security payoff; batching eval
  requests amortizes crossings to noise.
- Property-referencing expressions: a bridge hop costs 3.0 us in the
  WORST tier (browser); the bindings pack (ES sec 7.3) pre-resolves
  the common case to zero mid-eval hops. Against real read costs --
  4.9 us for a plain property, 175+ us for anything through .Shape,
  8.1 us mean for real assembly expressions -- proxy overhead is
  second-order. The King-class .Shape.BoundBox binding (5808 cells,
  ~1 s of expression time IN-PROCESS today) is dominated by host-side
  TopoShape materialization that the sandbox does not add to; if
  anything the handle protocol can answer BoundBox from the host
  cache without building a TopoShapePy at all.

VERDICT (the output ES sec 10 asked for): **Architecture A (the C++
value path) is NOT a Phase 1 prerequisite.** Proxy round-trip cost
does not endanger the design anywhere the corpus reaches; the
bindings pack suffices. A stays what ES sec 4 called it -- an
optional later accelerator (and the .Shape read path is a better
optimization target than arithmetic ever was).

Second finding: the 1.5 s cold start settles the interpreter
lifecycle question's direction (ES sec 11) -- per-recompute instances
are ruled out; one instance per principal from a pre-initialized
snapshot (zygote) is required, exactly as ES sec 8 anticipated.

## 6. Deliverable (d): frozen v1 contracts

### 6.1 Permission catalog v1 (FROZEN)

The ES sec 3.2 catalog is adopted as-is:

  permission       document   session   addon
  ---------------  --------   -------   -----
  doc.read.self    ALLOW      ALLOW     ALLOW
  doc.write.self   ALLOW      ALLOW     ALLOW
  doc.foreign      PROMPT     ALLOW     ALLOW
  geom.call        ALLOW      ALLOW     ALLOW
  app.query        PROMPT     ALLOW     ALLOW
  gui              DENY(np)   ALLOW     ALLOW    np = not promptable
  host.import:<m>  PROMPT     PROMPT    ALLOW
  unsafe.getattr   DENY       PROMPT    ALLOW
  fs / net         not offered in v1

Pseudo-property -> permission mapping (closes the ES sec 11 open
item, informed by the corpus: only _pla and _math are used in the
field, so tight defaults break nothing):

  _pla/_matrix/__pla/__matrix -> doc.read.self (adapter returns a
      matrix; no live object crosses)
  _ref/_self                  -> _ref doc.read.self (handle pair);
      _self drill-down IS unsafe.getattr
  _shape                      -> geom.call (host-side getShape,
      handle result)
  _app                        -> app.query
  _gui                        -> gui
  _math/_re/_coll/_py         -> none needed: math/re/collections and
      the sandboxed builtins are Ring-0 IN-IMAGE modules; the
      in-image builtins never include open/exec/eval-of-host --
      isolation by image contents, not permission
  _part/_cq                   -> host.import:Part /
      host.import:freecad.fc_cadquery (near-empty v1 facades)
  engine builtins (dbind, hiddenref, str, tuple, trunc, tan, ...,
      list_*, mscale, ...) -> no permission: pure in-image evaluator
      machinery. getvar stays disabled; hasvar becomes DENY too (it
      leaks host-global existence, Expression.cpp:3305).

Quotas (fuel, memory, op depth/queue) are per principal and are
limits, not permissions (ES sec 3.4).

### 6.2 Grant-store schema v1 (FROZEN)

Persisted grants live OUTSIDE document files, in the user config
area: <UserAppData>/security/grants.json (a sibling audit.log holds
the append-only decision log; "once"/"session" scoped answers are
in-memory only and never touch disk).

  {
    "version": 1,
    "defaults": { "<permission>": "allow|deny|prompt", ... },
    "grants": [
      {
        "principal": "document:sha256:<64 hex>" | "session"
                     | "addon:<name>",
        "permission": "doc.foreign" | "host.import:<module>" | ...,
        "target": "*" | "<doc-name>" | "<module>",
        "decision": "allow" | "deny",
        "scope": "always",
        "granted_utc": "<ISO 8601>",
        "display": { "label": "<file base name>",
                      "path": "<last seen path>" }
      }
    ]
  }

Rules: display.* is metadata only, never matched; lookup key is
(principal, permission, target) with target "*" as fallback; an
explicit deny outranks any allow; schema version bumps are
migrate-forward only. Headless: --grant <permission>[:<target>]
flags and an optional --policy <file> (same schema); default is DENY
plus one structured audit line per decision, never a hang (ES sec
3.3).

### 6.3 Document-hash canonicalization v1 (FROZEN)

principal id = "document:sha256:" + SHA-256 over the UTF-8 byte
string:

  "fcexpr-v1\0"
  + for each code-carrying item, SORTED lexicographically by its
    canonical form, joined by "\0":
      - every PropertyExpressionEngine entry, as
        "e\x1F<expression string>"    (path EXCLUDED -- see below)
      - every spreadsheet formula cell, as
        "c\x1F<expression string>"    (address EXCLUDED)
      - (rung 2 forward-compat) every embedded script payload, as
        "s\x1F<module.class>\x1F<code bytes>"

Decisions and why:

- Only the CODE strings feed the hash -- not paths, addresses, Uid,
  labels, timestamps, geometry, or file layout. Moving a bound
  expression to another property, or a formula to another cell,
  KEEPS the grant (the code the user vetted is unchanged); editing
  any expression VOIDS all grants and re-prompts. This is the
  tamper-voids-grants property ES sec 3.1 requires, with the fewest
  spurious expirations.
- Sorting makes the hash independent of serialization order, so a
  no-op resave (object reorder, xml rewrite) cannot expire grants.
  Duplicate strings collapse (sorted-set semantics): adding a second
  copy of an already-granted expression does not re-prompt.
- Expression strings hash in their PERSISTED form (the exported
  form with internal names), so a document restored and resaved
  unchanged hashes identically even where display form differs.
- The version prefix allows the recipe to evolve without old grants
  silently matching a new scheme.

### 6.4 The object-protocol slice (the reusable deliverable)

ES sec 7.6's op set is confirmed with these Phase 0 amendments:
add resolve_alias (or pre-resolve ranges in the bindings pack, sec
1.3); add the error/exception-type wire encoding (sec 1.3); the
value encoding must carry bool as bool (sec 1.2's identity loss is a
bug to NOT reproduce), plus list/dict/tuple-of-values, quantity,
vector/rotation/placement/matrix/boundbox, and handle{id,type}.
Handles remain per-recompute-transaction (ES sec 7.6). This slice is
the first shipped piece of the ComputeBoundaries sec 7 protocol.

## 7. Where this leaves the sequencing

Phase 0 is complete. Phase 1 (desktop sandbox = the security fix)
starts with, in order:

1. The S1-S3 seam mechanics (typedef the owner/deps types, split
   maintenance ops out, structural getDep) -- pure host refactor,
   testable against the existing suites, no behavior change.
2. ExpressionCore build target compiling host-side first (the ES
   sec 8.1 severed-App build is the same audit done by the linker).
3. The permission service (catalog/store/audit per sec 6 above) in
   front of the EXISTING chokepoints -- the grant model can ship
   and be exercised before the wasm image exists, replacing the
   PyModules allowlist.
4. The wasm image (CPython + core + Ring 0), wasmtime host, bridge
   dispatcher, bindings pack; acceptance per ES sec 10 including
   the grant/revoke cycle.

Open questions RESOLVED by this phase: proxy-cost (A not required);
interpreter lifecycle direction (snapshot per principal, cold start
1.5 s measured); pseudo-property defaults (sec 6.1); hash
canonicalization (sec 6.3). Still open: prompt UX detail, addon
principal granularity, the WASI numpy packaging risk, snapshot
tooling.
