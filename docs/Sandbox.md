# The Python sandbox: consolidated reference

Consolidated on **2026-09-03** from six documents written between
2026-08-29 and 2026-09-03 -- `ExpressionSandbox.md`,
`ExpressionSandboxPhase0.md`, `ExpressionImage.md`, `PyodideHost.md`,
`SandboxNetwork.md`, `SandboxGui.md` -- after an audit of every concrete
claim in them against the code at `dfd9336c91` on branch `SecurePython`.
Those six stay in the tree as the historical record (each carries a
banner pointing here); this document is the one to read and the one to
keep current.  Sec 14 says which section of which source went where and
what the audit found stale.

Status tags used throughout: **[built]** exists in the tree and is
tested; **[measured]** a number taken on this box under the stated
conditions; **[designed]** written down, not built; **[decided]** a
user decision, quoted where the wording matters.

## 0. At a glance

    area                            status      where
    ------------------------------  ----------  ------------------------------------------
    principals, permissions, grants  built       src/App/ExpressionSecurity*.{h,cpp}
    enforcement at the chokepoints   built       Expression.cpp, ObjectIdentifier.cpp
    permissions panel + padlock      built       src/Gui/DlgDocumentPermissions.{h,cpp}
    the image (engine in wasm)       built       src/App/ExpressionImage/
    the wire (ops, tags, handles)    built       src/App/ExpressionImage/FcxWire.h
    generated facades                built       src/Tools/bindings/generateSandboxFacades.py
    the router (expressions, sheet)  built       src/App/ExpressionEvaluator.{h,cpp}, gated OFF
    pyodide runtime (default)        built       src/App/ExpressionPyodideRuntime.cpp, PyodideHost/
    WASI runtime (reference)         built       src/App/ExpressionWasmtimeRuntime.cpp, off by default
    time budget (both runtimes)      built       ExpressionPyodideRuntime.cpp, ExpressionWasmtimeRuntime.cpp
    bootstrap + package set          built       src/App/ExpressionPyodide.cpp, src/Ext/freecad/pyodide/
    missing-package offer            built       pkg.missing op, Permission::PkgInstall, the panel
    corpus gate                      built       scripts/expr-switchover/
    porting linter (GUI)             built       scripts/sandbox_gui_lint.py
    routing ON by default            not yet     preference Expression/Sandbox:Evaluate
    network capability               designed    sec 6
    GUI protocol, mirror, widgets    designed    sec 7
    rungs 1-4 of the ladder          designed    sec 1.3
    memory ceiling for a guest       open        sec 13

## 1. Direction

### 1.1 Why

The expression engine is a hand-written C++ interpreter with no pure
C++ evaluation path: every value is a CPython object (`getValueAsAny()`
is `pyObjectToAny(getPyValue())`, about 128 call sites in
`Expression.cpp`).  Its old protection was a name-matched allowlist run
inside the interpreter it guarded -- a confused deputy: an eight-name
builtin denylist plus a module allowlist by `__module__` prefix, with
the module check on attribute-traversal results commented out.  The
threat the roadmap names is concrete: opening a malicious `.FCStd` runs
code at recompute.

The direction, ordered by the user (2026-08-29): sandbox the engine's
Python before the spreadsheet ships to the browser; define the security
model (who is asking, what can be asked, how the user grants) BEFORE
the mechanism, with grant as first-class as deny (2026-08-30).

### 1.2 The end state **[decided 2026-09-03]**

"Our final goal is to run everything Python in pyodide."  One
preference, `Python/Runtime = native | pyodide`, moves every rung that
is ready into the sandbox; a rung that is not ready falls back to
native with an audit line, never silently.  Pyodide is THE runtime
FreeCAD ships and develops; the WASI image is the reference
implementation, built only on request.

### 1.3 The ladder

Each rung ships alone; each is strictly more Python in the sandbox.

- **Rung 0, expressions** **[built]**: both spreadsheet modes route,
  the corpus gate passes, the budget works.  Routing is preference-gated
  OFF by default (sec 3.5).
- **Rung 1, native dispatch for `call` members** **[designed]**: the
  annotated method set retargeted from Python C API calls to generated
  C++ dispatch, so document principals never touch native CPython even
  indirectly.
- **Rung 2, document-embedded Python** **[designed]**: scripted-object
  `Proxy` code lives in the document's guest; `execute()` is a bridge
  call.  Needs instance-per-principal with a fast spawn, the
  asynchronous loop primitive, and `PropertyPythonObject` restore that
  never instantiates host objects.  `FeaturePython::execute()` calls the
  host Proxy today (`src/App/FeaturePython.h:201`); nothing routes a
  Proxy that lives in a guest.
- **Rung 3, session scripting** **[designed]**: console and macros in a
  session-principal guest with wide grants; needs the bindings pack to
  cover the App-level API, user packages (sec 5), session network (sec
  6).
- **Rung 4, Python workbenches** **[designed]**: App-side logic is rung
  3 work; the GUI residue is sec 7.

The App-core severance (`FREECAD_NO_NATIVE_PYTHON`, an App that does not
link libpython) is the argument-by-compiler for rung 1: chasing link
errors IS the chokepoint audit.  Not started; `ExpressionCore` exists
only as an OBJECT library folded into `FreeCADApp`
(`src/App/CMakeLists.txt:580-601`), explicitly a stepping stone.

### 1.4 Standing rulings

- Expressions are DOCUMENT CODE: gate by principal only, never carve
  out a carrier (2026-09-03).  An expression, a Proxy, an embedded
  script all run as `document:<hash>`.
- Pre-resolve host-side, then FAIL CLOSED: no silent native fallback
  when the image cannot evaluate (2026-08-31).
- Routing stays OFF by default until the corpus regression, not
  judgement, says otherwise (2026-08-31).
- wasmtime stays a SHARED library and a conda package
  (`wasmtime-capi`), decided on the multi-process OCCT direction and on
  more Python moving into the image, not on size (2026-09-01).
- `doCommand` is permission-controlled: runs under the caller's
  principal, never escalates, gated by `gui.doCommand` (2026-09-03).
- The GUI island (native PySide/pivy code) is attributed, not enforced:
  the threat model is document-derived code, which never runs there.

## 2. The security model **[built]**

Modeled on browser origins: wasm guest = sandboxed renderer, host
dispatcher = browser kernel, principal = origin, grant flow = permission
prompt, per-document panel = site settings, PROMPT scoping =
popup-blocker (once / session / always), quotas = tab throttling.

### 2.1 Principals

Three classes (`ExpressionSecurity.h`, `PrincipalClass`):
`document:sha256:<hex>`, `session`, `addon:<name>`.  The C++ core is
not a principal.  A document's identity is content-addressed:
`DocumentHashBuilder` collects every expression, cell and script code
string into a sorted set, joins them with `\x1F` under the prefix
`"fcexpr-v1\0"`, and SHA-256s the result.  Tampering with a trusted
file voids its grants; copying a trusted file's Uid into a hostile one
gains nothing.  Console and script evaluation run as `session`
(`DocumentObjectPyImp.cpp:373` pushes a `Runtime::Scope("session")`).

### 2.2 The catalog (frozen v1, plus one action)

`catalogDefault()` in `ExpressionSecurity.cpp`; addons default to ALLOW
across the board.

    permission        document   session   addon   notes
    ----------------  --------   -------   -----   -------------------------------
    doc.read.self     ALLOW      ALLOW     ALLOW   same-origin rule
    doc.write.self    ALLOW      ALLOW     ALLOW   write_prop + write family, owner only (3.2)
    doc.foreign       PROMPT     ALLOW     ALLOW   the cross-origin wall
    geom.call         ALLOW      ALLOW     ALLOW   cost bounded by budget, not grant
    app.query         PROMPT     ALLOW     ALLOW
    prefs.read        ALLOW      ALLOW     ALLOW   the parameter store, read only, through a
                                                   curated reader (draftutils.params); added
                                                   2026-09-04, user ruling (7.6 G1c decision 2)
    gui               DENY (np)  ALLOW     ALLOW   the only non-promptable cell
    host.import:<m>   PROMPT     PROMPT    ALLOW   per module
    unsafe.getattr    DENY       PROMPT    ALLOW   host-side Python attribute walks
    pkg.install:<p>   PROMPT     PROMPT    PROMPT  an ACTION, never a grant (sec 5.5)
    gui.doCommand     DENY       ALLOW     PROMPT  designed (sec 7), not in the enum yet
    net.*             --         --        --      designed (sec 6), not in the enum yet

The one deliberate compatibility break: `unsafe.getattr` (the
`obj.Proxy.foo` drill-down through a live host object) is DENY for
documents by design.  The corpus (sec 8.3) has zero such uses.
`host.import` classifies modules: `math`, `re`, `_sre`, `collections`,
`builtins` are free (ring 0); `FreeCAD`/`App` is `app.query`;
`FreeCADGui`/`Gui` is `gui`; everything else is
`host.import:<name>` (`ExpressionSecurityRuntime.h:342-344`).

### 2.3 Grants

- Scopes: once, session, always.  `PermissionNeeded` fails fast; a
  recompute is never blocked on a prompt.  A denial crosses the wire as
  a plain `Base::RuntimeError`, not `PermissionNeededException` (tests
  rely on the difference).
- Store: `<UserAppData>/security/grants.json` (schema v1) with a sibling
  `audit.log`; `GrantStore`, `AuditLog` in `ExpressionSecurity.h`.
- Headless: `--grant <permission>[:<target>]` (repeatable) and
  `--policy <file>` (a grants.json used instead of the user store),
  `Application.cpp:2586-2587`.
- Preference `Expression/Security:Enforce` (default true) switches
  enforcement off for rigs that measure parity, not policy.
- Python: `FreeCAD.ExpressionSecurity` -- `principal`, `grant`, `revoke`,
  `grants`, `pending`, `principalOf`, `resolve`, `clearOnce`,
  `clearPending`, `enforced`.

### 2.4 Enforcement point

Host-side only, never in the guest: `ExpressionSecurityRuntime.h`
compiles every `check*` to a no-op under `FC_EXPR_IMAGE`, so the guest
cannot contain enforcement even by mistake.  The permission service was
wired into the old chokepoints rather than replacing them:
`ImportModules::getModule` calls `checkModuleImport`
(`Expression.cpp:2375`), `CallableExpression::securityCheck` keeps the
eight-name denylist and adds `checkCallablePermission`
(`Expression.cpp:4490-4530`), `Component::get` calls `checkGetattr`
right above the still-commented-out module check it supersedes
(`ObjectIdentifier.cpp:653-671`).  `getvar`/`hasvar` and
`Base::Interpreter().getVariable` were deleted, not disabled.  Every op
over the wire carries its principal and passes the schema check, then
the permission check, on the host.

### 2.5 The panel and the padlock **[built 2026-08-30/31]**

`Gui::Dialog::DlgDocumentPermissions` (591 lines): per-document grants
with the four verbs (grant, deny, inspect, revoke), once/session/always
scopes, a `PermissionIndicator` and a `SandboxIndicator` status-bar
padlock whose click toggles routing, warns when routing is on with no
runtime present, and offers the pyodide runtime download when none is
installed.  A `pkg.install:<name>` row's allow button runs the
installer (sec 5.5).  Any roadmap text that still lists "the
permissions panel" as future work is stale; the remaining panel work is
adding rows for the network permissions.

## 3. Architecture **[built]**

### 3.1 The boundary

Architecture B: the Python value layer moves into the sandbox; OCCT and
the Document stay host-side.  Architecture A (a C++ value path that
de-Pythonizes arithmetic) was measured NOT to be a prerequisite (sec
8.1).  The AST walker itself is compiled into the image ("image-walk";
"host-walk", one crossing per operator, was rejected).

`ExpressionCore` (`Expression.cpp`, `ObjectIdentifier.cpp`, `Range.cpp`)
compiles into the image behind `FC_EXPR_IMAGE` against an adapter world
(`ExpressionImage/FcxDocument.{h,cpp}`) that keeps the host's class
names (`App::Document`, `DocumentObject`, `Property*`,
`PropertyContainer`, `PropertyLinkBase`, about fifteen methods) so
`ObjectIdentifier.h` needs no change.  Host-only code -- the security
chokepoints, `DocumentObjectPy`/`ExpressionPy` type checks, the
maintenance overrides of `VariableExpression`/`RangeExpression` -- is
compiled out.  One source list, `ExpressionImage/ImageSources.cmake`,
feeds both guests (the WASI reactor and the pyodide extension module).

Three rings: ring 0 is in-image native (CPython, the core, Base math
bindings, a stdlib slice); ring 1 is generated facades for host types;
ring 2 is absent (`os`, `socket`, `ctypes`: no grant supplies them).

### 3.2 The wire (`FcxWire.h`)

Ops: `eval`, `exec`, `read_prop`, `get_attr`, `call`, `get_item`, `len`,
`bool`, `str`, `release`, `resolve_alias`, `pkg.missing`, `write_prop`,
`mod_call`, `mod_get`.  `bool` and `str` (2026-09-04) are the proxy's
`__bool__` and `__str__`: natively every object is truthy unless its
type says otherwise (`if plane:` -- with only `__len__` on the proxy the
host answered "no len()" and Draft's plane tests died), and Draft
compares curves by `str()`; both are answered by the C type's own slot,
and a heap type (a Python-defined `__bool__`/`__str__` is host code)
rides the unsafe gate.  Two more wire rules the draftgeoutils gate
forced: a module facade's class object crosses as a type reference
(`{"t":"ty","q":"Part.Edge"}`, decoded on the host to the declared
object -- `shape.ancestorsOfType(v, Part.Edge)`), and this fork's
`Part.ShapeList` (what `Shape.Edges` returns, a sequence type of its
own) crosses as a plain list of handles, the classic shape Draft's
slicing and concatenation expect.  The module facades (2026-09-04, `MODULE_FACADES` in the
generator, sec 3.3): the guest's `Part` module is a closed list of
names -- 30 callables (`LineSegment`, `makePolygon`, `Face`, ...), one
constant (`OCC_VERSION`), one exception (`OCCError`) -- built at init
into the guest's `sys.modules`; a callable is `{op:"mod_call", m:
"Part.LineSegment", a, k}` run on the host with handle arguments
dereferenced, its result crossing by value or as a handle with the
annotated facade above it; a constant is read once over `mod_get`; the
exception is a guest-local class the bridge raises whenever a host
reply names it (`raiseFromReply` consults the facades' `EXCEPTIONS`
after the builtins).  Each module's table row names the catalog
permission its ops are checked against (`ModuleMember::permission`,
2026-09-04): `Part` is `geom.call`, a decision -- a curated constructor
list is a geometry call, where `_part` (an arbitrary import) is
`host.import`; `draftutils.params` (`get_param`, `get_param_view`,
the two names Draft's App side reads preferences through) is
`prefs.read` (2.2; `app.query` until 2026-09-04, when a document
object's `Wire.__init__` would have prompted for `MakeFaceMode`),
answered by the host's own
Draft reader by value -- the bundled wheel (5.6) leaves
`draftutils/params.py` out, and the facade sits in `sys.modules` before
the wheel's `draftutils` package is imported, so `from draftutils
import params` finds it.  An undeclared name is not on the guest
module at all (AttributeError), and a forged `mod_call` is a protocol
error.  `exec` (2026-09-04,
host->guest, `ImageHost::exec(source, module)`) runs statements; with a
module name the source becomes that module in the guest's `sys.modules`
(created and registered before it runs, bound to its parent package
when dotted, removed on failure) -- how a test pushes a harness or a
source under study into the guest; workbench code itself boots as a
bundled wheel (5.6).  Value tags: `quantity`,
`vec`, `rot`, `pla`, `mat`, `bb`, `h` (handle), `tup` (a tuple crosses
as a tuple, the first corpus-gate finding).  Reply shape `{ok, val}` or
`{ok:false, exc, msg}`.  The one write op is `write_prop` (2026-09-04,
`{op, h, a: name, v: value}`, the proxy's `__setattr__`): the handle
must be the EVALUATION OWNER -- `HandleTable::owner()`, set by
`evalExpression` and by a raw `eval` that names one; rung 2 writes self
and nothing else -- under `doc.write.self`, then the C++ property
system's `setPyObject` (typed; a shape re-maps its element map), never
host Python.  `Immutable` refuses, as native `setattr` does; `ReadOnly`
is the editor's status and writes through it succeed natively, so they
do here too (the gate is parity, not extra policy).  The value
decodes through the handle table, so a host object crossing back as a
handle dereferences to the live object (a `PropertyLink` takes the
object, not its wire face).  `addProperty`, `removeProperty`,
`setPropertyStatus` are declared `call` members (DocumentObjectPy.xml,
PropertyContainerPy.xml -- the facade chain now reaches
PropertyContainer through ExtensionContainer) and ride the `call` op
behind the same owner-only gate.  Refusals are `PermissionError`, not
`ProtocolError`: the request is well-formed, the principal is not
allowed.  The read path never enters host Python
(`read_prop` is answered from the C++ property system); host CPython
runs only for members annotated `call`.  Arguments validate against the
XML-declared signature; a sandbox callable is never a valid argument.
Rung 2 (2026-09-04, G1c, sec 7.6): two more host->guest ops and one
value.  `proxy_new {mod, cls, a, alloc?}` imports a class in the guest
and constructs it -- `obj.Proxy = self` inside `__init__` is caught by
the proxy's `__setattr__`, which registers the instance in the guest's
registry and sends `write_prop Proxy` with the descriptor
`{"t":"gproxy", id, mod, cls, hooks}`; the host decodes that into the
STAND-IN (`ExpressionGuestProxy.h`), one per guest proxy, and the
Proxy property holds it.  `proxy_call {id, m, a, k?}` runs hook `m` of
the registered instance with the decoded arguments (the object rides
as a handle) and returns the result by value; the stand-in's hook
attributes are forwarders that do exactly this, and the OWNER of the
call is the document object in the first argument, so the hook may
write it.  `gproxy` also crosses host->guest as `{"t":"gproxy", id}`
(an `obj.Proxy` read resolves to the guest instance) and any
host->guest request may carry `"pd":[ids]`, stand-ins that died.  A
handle whose object carries extensions names their facades in `"ext"`
(`Part.AttachExtension` on a Draft Wire); the guest composes the proxy
class from the type's facade plus theirs, and the host looks a member
up on the container's extension types after the type's own MRO, since
extension methods are injected per instance and never appear in it.
Round trips NEST: a guest hook's `write_prop` runs the host's
`onChanged`, whose hook is a `proxy_call` inside the pending bridge op
(`ImageHost::Private::Transaction` keeps a depth, restores the outer
owner, flushes releases and performs a requested reset only at the
outermost level; wasmtime enters the store through the CALLER's
context while nested, the pyodide runtime arms the budget once).

The fixed layout (2026-09-04, step 7 of the coding order): a bare
`read_prop`/`get_attr` with a name -- 2738 of the 3575 hops in the
draftgeoutils gate -- skips CBOR on both sides.  Request `0xF1`, op
byte, handle u64, name (u16 length + UTF-8); reply `0xF2`, kind byte,
then a float64, a bool, an int64, a string or a Vector inline -- or
kind 0 and the CBOR reply as before for any other value and every
error.  The guest takes it when no releases are queued (those ride
"r" on a CBOR request); the host recognises the magic byte, which no
CBOR document of this wire begins with.  Everything else is CBOR
exactly as before; the two forms share the dispatcher.

Handles are transaction-scoped and decoded BEFORE `clearHandles()`.
Releases never cross as an op (2026-09-04, step 6 of the coding
order): a proxy's `__del__` queues its id in the guest, and the queue
rides as `"r"` on the next guest->host request or on the evaluation's
own reply -- the host releases them before the op, or after the trip,
under the deferral below.  Before this, 58 percent of all hops in the
draftgeoutils gate were single releases (4977 of ~8550 for 116 calls).
Releases are deferred for one transaction and applied at the start of
the next: the spreadsheet idiom `tuple(.cells, <<B4>>, <<ZZ4>>)` returns
a tuple whose first element IS a host object, and the image destroys
its proxies as the evaluation unwinds -- before the host decodes the
reply -- so an eager release turned the handle into a stale id.  The
flush DECREFs under the GIL, only when an interpreter exists
(`e2969a8503`; this paragraph existed only in that commit message
before).

The bindings pack: identifiers are pre-resolved host-side
(`getIdentifiers()`/`getDeps()`) and shipped with the request as values
or handles, so a pack hit costs zero crossings; on a miss the real
`resolve()`/`access()` run against the adapter world.  Failures ship
too (`binderrs`), but only for identifiers naming a document other than
the owner's -- a blanket "ship every failure" would break in-eval bound
variables (`a = Width + 1; a * 2`).  The pack resolves the WHOLE
identifier host-side, which is why link placements, `getSubObject`
walks, `_shape`, `_self` and indexed group access evaluate identically
native and routed.

### 3.3 Generated facades

`src/Tools/bindings/generateSandboxFacades.py` reads
`<Sandbox tier="value|handle|call"/>` on the `*Py.xml` members and
emits ONE `FcxDispatch.inc` (host) and ONE `FcxFacades.inc` (a Python
source string for the image) from the same list, so the two guests
cannot drift.  Absent means DENY: a new binding is unreachable until
annotated, and the security review of the bridge is "diff the
annotations".  Until 2026-09-04 that was 14 members in five XMLs; G1
step 5 grew it to 239 members across 50 facades in 50 XMLs, the
Draft App-side surface of sec 7.6 (`--surface`): the nine TopoShape
bindings, Geometry/Curve/Surface and the concrete curves and surfaces
Draft touches, Geometry extensions, BaseClass (`TypeId`,
`isDerivedFrom`), Document/DocumentObject/PropertyContainer reads and
the write family.  Value classes (Vector, Rotation, Placement, Matrix,
BoundBox, Quantity) need no annotation: they cross by value and exist
in the guest.  The list lives ONLY in the generator (`ANNOTATED_XMLS`,
fathers first -- an XML with no annotation of its own, Persistence or
TrimmedCurve, is listed so the Father chain reaches through it); the
two CMake custom commands take their DEPENDS from `--list-xmls` so
nothing drifts.

Two traps the growth uncovered.  A facade's key must be the RUNTIME
`tp_name`, and Part renames all nine TopoShape types at module init
(`AppPart.cpp`: TopoShapePy becomes `Part.Shape`, then `Part.Edge`,
`Part.Solid`, ...), so the XML-derived `Part.TopoShape` never matched
a live shape and every shape fell through to `read_prop` -- that was
the caveat recorded in the crossing analysis, not a python-mode quirk;
`RUNTIME_TYPE_NAMES` in the generator carries the nine, and
`merge_same_key` folds any genuine collision into one facade (the
union of members; a member the object lacks raises AttributeError
from the host as natively).  And a facade is verified only by real
guest Python on a real object
(`ExpressionImageEvalTest.partSurfaceOnHandles`), never by reading
the XML.

### 3.4 The evaluation seam

`src/App/ExpressionEvaluator.{h,cpp}` is the single seam, deliberately
NOT in `ExpressionCore` and deliberately at the CALLERS rather than
inside `getPyValue` (the 12x hop cost makes per-node routing absurd).
Re-entrancy during an image evaluation runs natively (thread-local
guard); the AST is not re-parsed host-side.  `SandboxStatus` /
`sandboxStatus()` (`ExpressionEvaluator.h:72-96`) is a cheap preference
read plus two stats; `confined()` is host built, image present, routing
enabled.  `ImageHost::evalCount()` counts crossings; `ImageHost::stats()`
(2026-09-04) counts them by kind -- evaluations, handles minted, and
every guest->host op by wire name -- with `resetStats()` to zero them.
That is the instrument for G1a: run a Draft or Arch `execute()` in the
guest, read `FreeCAD.ExpressionSandbox.stats()['ops']`, and the
read_prop/get_attr/call counts say where a snapshot op would pay
(sec 8.1) before one is designed.

### 3.5 The router: what is routed

Preference `Expression/Sandbox:Evaluate`, **OFF by default**.  Routed:
expression evaluation, the spreadsheet through the single seam
`PropertySheet::eval` (and its `evalPy` twin) -- python mode CROSSES
rather than being refused, since python mode is a lexer state plus
builtins visibility, both in the image -- with `OptionCallFrame` /
`OptionPythonMode` crossing in the request; paste-as-value,
`setContent`, `EditQuantity`, `Cell::getPyValue` in EditNormal,
`DlgSheetConf` range validation.  NOT routed: the GUI live expression
editors (`DlgExpressionInput`, `SpinBox`, `InputField`, `PropertyItem`)
-- policy-gated as session, not confined; a named future slice.
Writes were never the router's problem: `PropertyExpressionEngine::
execute` sets the path on the host after the value returns.

Python: `FreeCAD.ExpressionSandbox` -- `routed`, `setRouting`,
`available`, `imageInfo`, `evaluate`, `evaluateNative`, `evalCount`,
`stats`, `resetStats`, `reset`, `proxyNew`, `proxyInfo` (rung 2, 3.2),
`pyodideReleases`, `pyodideLayout`, `pyodideVerify`,
`pyodideAbi`; constants `OptionCallFrame`, `OptionPythonMode`.

## 4. Runtimes **[built]**

`ImageRuntime` (`ExpressionImageRuntime.h`) is the seam; two
implementations register by name.  Selection (`runtimeChoice()`,
`ExpressionImageHost.cpp:69-83`): preference `Expression/Sandbox:Runtime`,
else environment `FCX_RUNTIME`, else `"pyodide"` when the pyodide host
is compiled in (the normal build), else `"wasi"`.

### 4.1 pyodide on a bare V8 (the default) **[built 2026-09-02/03]**

Three pieces: the engine is `v8-embed`'s `libv8.so` (V8 14.6, built
out of node 26.6.0's tree by `realthunder/v8-embed-feedstock` as a bare
engine: no node modules, no default context, five conda platforms plus
Windows from source); the shim is `src/App/PyodideHost/host_shim.js`
(291 lines), the minimal d8-shape surface pyodide's loader needs --
`console`, `read`/`readbuffer`/`load`, `os.system` (a string-matched
RNG hook), `crypto.*`, `performance.now`, timers and `queueMicrotask`,
`TextEncoder`/`TextDecoder`, `URL`, `btoa`/`atob`, `self` -- and
NOTHING else: no `process`, `require`, `module`, `fs`, `fetch`,
`XMLHttpRequest`, `Worker`, `WebSocket`, `MessageChannel`; the native
surface is `PyodideRuntime` (`ExpressionPyodideRuntime.cpp`), which
installs `readText`/`readBytes`/`randomBytes`/`now`/`print` on
`__fcx_host` plus the dynamic-import and import-meta callbacks.
Confinement is by construction: what the guest can reach is what the
shim defines.  `PyodideProbe.cc` is the standalone probe binary only.

Boot and call: `pyodide_glue.js` provides `__fcx_boot(root, wheel,
packagesDir, names)`, `__fcx_link()`, `__fcx_setInterrupt`,
`__fcx_teardown`.  The transport (2026-09-04) is the wasi reactor's
own shape on both sides, with no JavaScript in the path: guest->host is
the pair of wasm imports `fcx_host_call(ptr, len) -> reply length` /
`fcx_host_fetch(dst, cap)` (ImageBridge.cpp, module `env` for the side
module, `fcx` for the reactor), installed as V8 natives and merged into
the main module's symbol table with `Module.mergeLibSymbols` BEFORE
the wheel loads, so the dynamic linker binds the side module's imports
to them; the native reads the request out of `Module.HEAPU8` in place.
Host->guest is the side module's `fcx_alloc`/`fcx_call`/`fcx_free`
exports (ImageModule.cpp, `EMSCRIPTEN_KEEPALIVE` plus
`-sEXPORTED_FUNCTIONS`), taken from `Module.LDSO.loadedLibsByName`
and called from C++ as wasm functions.  `HEAPU8` is re-read per use:
memory growth replaces it.  The earlier shapes -- a Python callable
returning bytes (a proxy per crossing, 40 us of floor), then two
bytearrays viewed through `PyProxy.getBuffer()` -- are gone, along with
`set_host`, `set_host_buffered`, `buffers`, `grow_request`,
`call_len` and `__fcx_call`.  The guest wheel
`fcx_image-<ver>-cp314-cp314-pyodide_2026_0_wasm32.whl` is
`_fcx_image`, the same `ImageSources.cmake` compiled by the pyodide
toolchain (emsdk 5.0.3, pyodide-build 0.39.0) with `-fPIC
-fwasm-exceptions -sSUPPORT_LONGJMP=wasm -fvisibility=hidden -flto`,
linked `-sSIDE_MODULE=2 -sWASM_BIGINT`; it must be an `add_executable`
with a `.so` suffix, not `add_library(MODULE)`, which CMake's Emscripten
platform would archive with `emar`.  `-fvisibility=hidden -flto` cut the
wheel 427 to 368 KB and the hop cost by a third.

The time budget (both runtimes): two stages, a soft interrupt through
CPython's emscripten signal handling on a shared `int32` buffer, then a
hard `Isolate::TerminateExecution` (wasmtime: epoch interruption);
`Expression/Sandbox:BudgetMs` 5000, `GraceMs` 1000; outcomes
`Interrupted` / `Terminated`; `Watchdog` in
`ExpressionPyodideRuntime.cpp:389-517`.  Budget overhead: WASI transport
floor 2.6 to 3.1 us; pyodide inside run-to-run noise.

Still open: a memory ceiling for a guest (no `MAXIMUM_MEMORY` handling).

### 4.2 The WASI image (the reference) **[built 2026-08-30/31, frozen]**

Built only with `BUILD_EXPR_WASI_RUNTIME` (default OFF), fixed when
broken, never extended.  Toolchain: wasi-sdk 33 (the floor:
exceptions-enabled libc++) to 34 (the ceiling for CPython's
`wasm32-wasi` target name), wasmtime v48.0.1 CLI and C API, CPython
3.12.13 from `Tools/wasm/wasm_build.py wasi`.  `ExpressionImage/
CMakeLists.txt` is a standalone cross project; `CMAKE_BUILD_TYPE`
defaults to Release -- an early build left it empty, ran -O0, and every
number taken before that fix was 3 to 6x too slow (sec 8.1).  Flags:
`-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false`,
`-mexec-model=reactor`, `-DFC_NO_QT`, `-DBOOST_DISABLE_THREADS
-DBOOST_SYSTEM_DISABLE_THREADS`.  Real in the image: Type, BaseClass,
Vector3D, Rotation, Matrix, Placement, DualQuaternion, Quantity, Unit,
Exception, PyObjectBase, GeometryPyCXX, the seven `*PyImp.cpp` binding
TUs, PyCXX; stubbed in `ImageStubs.cpp`: console to stderr, the
internal unit scheme only, a trace counter.

Transport: `fcx.host_call` / `fcx.host_fetch`, a two-call
size-then-fetch under wasmtime linker module `fcx`; image side
`_fcx.op(name, id, ...)` with a `HostHandle` proxy; host side
`HandleTable` and `dispatchHostOp`, permission checks before the object
is touched.

Packaging (`c105d058e6`): `tools/pack_image.py` strips the 34 MB build
(28 MB of it DWARF and names) to 10.9 MB (3.1 MB gzip) plus a 16-file
320 KB stdlib slice, and emits the desktop layout `<datadir>/Fcx/
{fcx_image.wasm, Lib/}` from the same list; `install(...)` rules
exist.  Resolution (`WasmtimeRuntime::resolve`): `configure()`, then
preferences `ImagePath`/`StdlibPath`, then `FCX_IMAGE`/`FCX_STDLIB`,
then `<datadir>/Fcx`.  The compiled-module cache lives in `<user
cache>/ExpressionSandbox/<name>-<FNV-1a of the image path>[-budget]
.cwasm` (the image directory is read-only after an install); a
precompiled module instantiates in about 19 ms against about 600 ms of
JIT.  `cMake/FindWasmtime.cmake` greps `wasmtime/config.h` for
`WASMTIME_CONFIG_PROP(void, wasm_exceptions`; `libwasmtime.so` has no
SONAME, so the imported target sets `IMPORTED_NO_SONAME`, which CMake
honours only on a target it knows is SHARED; `FREECAD_BUNDLE_WASMTIME`
defaults ON when the library is outside the prefix and the system
directories.

Two traps from the build: two `nlohmann::json` copies (3.11.2 vendored,
3.12.0 conda) collide in exported symbols with `json` in the signature;
and the v1 catalog ALLOWs addon principals, so a test that expects a
refusal must use a document principal.

### 4.3 The browser tier **[built 2026-08-31, on the WASI image]**

The stripped image runs in the page under pack-first evaluation with
the bridge deliberately NOT attached (a synchronous import cannot await
a Promise); a formula being typed is previewed without a host round
trip.  Needs the exnref exception proposal: Firefox 131+ and Chrome
137+ ship it, Chrome 123 needs `--experimental-wasm-exnref`.  Numbers
(Firefox 136, -O0 image, never re-taken on Release): fetch 128 ms,
compile and instantiate 268 ms, init 31 ms, 140 us per evaluation;
page-load-to-preview 1042 ms dominated by the 10.9 MB fetch.  The
browser tier has not moved to pyodide; where its packages would come
from is sec 6.4 territory, designed only.

### 4.4 Rejected: pyodide hosted by node **[measured 2026-09-01]**

Conda does ship an embeddable engine (`nodejs`, `libnode.so`, 71 MB)
and the boundary is fast (host hop 3.0 us against the WASI bridge's
then-quoted 31.6 us -- really ~7 us, sec 8.1), but a node-hosted
pyodide cannot be confined by subtraction:
after `del globalThis.process`, `js.Function('return
import("node:fs")')()` is still a full read-write escape, because
dynamic `import()` is a V8 realm intrinsic; node's permission model
blocks `process.binding`, which pyodide's own loader uses.  Hence the
bare engine of sec 4.1.  One correction worth keeping: node's JS module
loader and emscripten's `WebAssembly.instantiate` wheel linking are
orthogonal, so "no wheels ever" was the WASI image's own limit, never
pyodide's.

## 5. Bootstrap and packages **[built 2026-09-03]**

### 5.1 Layout

    <user app data>/Pyodide/
        current                 one line: the version that boots
        <version>/              pyodide.js pyodide.mjs pyodide.asm.mjs
                                pyodide.asm.wasm python_stdlib.zip
                                pyodide-lock.json package.json
        packages/               the user's wheels under lock-file names
            manifest.json
    <datadir>/Pyodide/wheels/   fcx_image-<ver>-cp314-cp314-pyodide_<abi>_wasm32.whl

`App::ExpressionSandbox::Pyodide` (`ExpressionPyodide.{h,cpp}`) is the
single owner of these facts: `releases()`, `layout()`,
`verifyDirectory()`, `scopePath()`, the lock lookups,
`missingImport()`.  Overrides: preferences `PyodideUserDir`,
`PyodidePackages`; environment `FCX_PYODIDE_USER`,
`FCX_PYODIDE_PACKAGES`.

### 5.2 Resolve order

Runtime directory: explicit `configure()`, preference `PyodideDir`,
`FCX_PYODIDE`, the user-data `current` marker, `<datadir>/Pyodide`.
Wheel: explicit, `PyodideWheel`, `FCX_PYODIDE_WHEEL`, an
`fcx_image-*.whl` beside the runtime, the shipped wheel whose ABI tag
matches (`wheelForAbi`), `<stdlib>/fcx_image.whl`.  A wheel whose tag
does not match the runtime's `abi_version` is refused at boot.

### 5.3 The pinned table

One entry: version `314.0.6`, ABI `2026_0`, CPython 3.14.2; the six
runtime files with sha256 (npm CDN and GitHub core tarball agree byte
for byte); the core tarball `pyodide-core-314.0.6.tar.bz2` with its
hash.  `initialize()` verifies version and every file hash (about 14
MB, tens of milliseconds, once per instance) and REFUSES anything else;
`FCX_PYODIDE_UNPINNED=1` or preference `PyodideUnpinned` turns the
refusal into a warning that repeats on every boot.

### 5.4 Scoping, and the one trap

The reader and the module loader are confined to three canonical roots
-- the runtime directory, the wheel's directory, the package set --
compared component by component after following symlinks,
case-insensitively on Windows, understanding `file:///C:/x`; never one
widened parent.  **Pyodide's shell-mode `resolvePath` is the
IDENTITY**: `packageBaseUrl` never reaches the reader and the loader
asks for a lock-file wheel by its bare file name, so a relative name
resolves against the runtime directory first and the package set
second (`scopePath`'s `fallbacks`), existing entries only.  A dev tree
stages numpy beside the runtime, so a test that boots from the staged
directory passes vacuously; the offer test boots from a copy holding
only the pinned files.

### 5.5 The installer and the offer

`freecad.pyodide` (`src/Ext/freecad/pyodide/__init__.py`), host Python:
`releases()`, `layout()`, `default_version()`,
`install_runtime(version=None, source="github", progress=None,
make_current=True)` with sources `github` (the 6.8 MB core tarball,
verified, six files read out by name), `jsdelivr` (npm, file by file)
or a local tarball/directory; `list_runtimes()`, `remove_runtime()`,
`set_current()`; `install_package(name, source="index", progress=None,
requested_by="session")` resolving `name` and its `depends` against the
active runtime's lock, depth first, fetching from
`https://cdn.jsdelivr.net/pyodide/v<ver>/full/<file>` or a local
mirror, verifying sha256, writing atomically, rewriting
`manifest.json` (`version`, `runtime`, `abi`, `packages`,
`load_order`); `remove_package()`, `list_packages()`, `manifest()`;
exceptions `RuntimeUnsupported`, `DownloadError`, `VerificationError`.
Downloads go through the Addon Manager's `NetworkManager` with a GUI,
`urllib` headless.  Nothing runs at startup.  A name not in the lock
raises "PyPI packages are not supported yet".

Boot loads `fcx_image` then `loadPackage(names)` in manifest order; a
manifest for another ABI is ignored with a warning.  The offer: a
`sys.meta_path` finder appended LAST asks the host one op,
`pkg.missing {a: name}`; the host answers from the lock's import map
(304 names): installed -> `ImageHost::scheduleReset()` and the next
evaluation boots with the package; else
`Runtime::requestPending(PkgInstall, name)` records a pending request,
audited as a prompt.  The panel's allow button runs
`freecad.pyodide.install_package(name)`, resets the sandbox
(`App::ExpressionSandbox::resetSandbox()`), and recomputes the blocked
objects -- the same re-run path a grant takes.  Not built: in-place
install into a running guest (P2), PyPI sources (PEP 783
`pyemscripten_<abi>` wheels), the pre-run import scan (dropped: an
`import` fails at the guest's import with the same offer).

### 5.6 Bundled wheels **[built 2026-09-04]**

FreeCAD's own workbench code reaches the guest as pure-Python wheels
beside the fcx_image wheel: `<datadir>/Pyodide/wheels/<dist>-<ver>-py3-
none-any.whl` (the dev tree's `<datadir>/Pyodide` is scanned too),
`Layout::bundled`, one per distribution name, sorted.  The boot loads
them by absolute path after `fcx_image` and before the user's package
set, their directories joining the reader's roots; a wheel that fails
to load is reported on the guest's stderr and skipped, as a package is.
Nothing is installed per user and nothing is downloaded: the wheel is
a build product, matched to the FreeCAD that ships it.

The first is `fcx_draft`, Draft's App side (7.6): `DraftVecUtils`,
`DraftGeomUtils`, `WorkingPlane`, and the `draftgeoutils`, `draftutils`,
`draftfunctions`, `draftmake`, `draftobjects` packages UNMODIFIED from
`src/Mod/Draft` (the CMake lists in `src/Mod/Draft/CMakeLists.txt`,
target `DraftSandboxWheel`, so a source edit repacks), minus
`draftutils/params.py` (a facade, 3.2) and the two support modules
that import `FreeCADGui` unconditionally (`todo`,
`init_draft_statusbar`: GUI-side, reached only under `GuiUp`); plus
what they import that the
guest lacks -- the vendored `lazy_loader`, `freecad.deprecation`
(falling back to `warnings.deprecated` where `typing_extensions` is
absent), and the guest shims under `src/App/ExpressionImage/shims/`:
a `PySide` package carrying the names Draft's App side reaches it
through and nothing that draws (`QT_TRANSLATE_NOOP`,
`QCoreApplication.translate` as the identity, `QTimer.singleShot`
running its callable now, `QLocale().decimalPoint()`), and empty
`Draft_rc`/`Arch_rc` resource modules.  The packer,
`src/Tools/bindings/packSandboxWheel.py`, needs python3 only and is
deterministic (sorted entries, fixed timestamps), so an unchanged
source leaves a byte-identical wheel.  WASI's reference image has no
package loader; bundled wheels are the pyodide runtime's.  Cost: the
first evaluation (the boot) measures 1.80-1.82 s with the 336 KB
wheel and 1.77-1.85 s without (three runs each, FreeCADCmd, routing
ON) -- nothing; importing all 105 modules in the guest is a separate
2.9 s gate step and happens only when workbench code asks for them.
Measure with `setRouting(True)` first: `evaluate()` with routing off
never boots the guest, and `available()` does not either.

Ground truth: a guest has no sockets by structural absence; the shim
defines no `fetch`, `XMLHttpRequest` or `WebSocket`.  The threat that
network adds is exfiltration, server-side request forgery against the
user's LAN, and beaconing, so the gate is by destination.

### 6.1 Shape

Two injected primitives -- a synchronous `XMLHttpRequest` (what
`urllib3`'s emscripten backend and `requests` use) and an asynchronous
`fetch` completing through the pumped task queue -- both forwarded as
one bridge op, `net.http.request`, to a host `NetClient` behind an
interface (leaning libcurl; Qt Network the fallback if the certificate
store check fails on the Windows and macOS conda builds), after
`NetPolicy::check`.  Redirects re-check every hop; the resolved address
is checked, not only the name.

### 6.2 Grammar and built-in denials

Targets are origin patterns `[scheme://]host[:port]`: a hostname
matches itself; `*.example.com` matches subdomains, never the apex; a
bare `*` is refused (the wide grants are the only "everything"); port
omitted means the scheme's default only; scheme omitted means `https`,
`http` written out.  Deny entries use the same grammar and always win.
Always denied, openable only by an exact-address user allow: loopback,
private ranges (10/8, 172.16/12, 192.168/16, fc00::/7), link-local and
the cloud metadata address 169.254.169.254, unspecified and multicast,
and any destination whose RESOLVED address lands there (DNS rebinding).

### 6.3 Defaults and limits

    permission          document    session   addon
    ------------------  ---------   -------   ----------------
    net.http:<origin>   PROMPT (a)  ALLOW     PROMPT, "always"
    net.http.any        DENY        ALLOW     PROMPT
    net.http.local      DENY        PROMPT    PROMPT (b)
    net.ws:<origin>     N4          N4        N4
    net.socket          DENY        N4        N4

    (a) through the panel, never a document-opened dialog; GET/HEAD
        unless widened.  (b) never persisted past the session.

Limits, not permissions: a 64 MiB response cap, timeouts from the
remaining budget, one in-flight synchronous request per guest, per
principal request and byte counters with a passive-indicator threshold;
`Outcome::Terminated` cancels in-flight requests.  Grants live in the
existing `GrantStore` (generic over `Permission`, no schema change);
one audit line per verdict.  Documents never get network by default;
an addon declares its origins in a manifest the Addon Manager can show.

### 6.4 The browser tier

The same policy runs in the page's shim as defense in depth; the
browser's CORS decides what connects; whether the served-document
server should proxy allowed origins is a separate design.

## 7. GUI for sandboxed Python **[designed 2026-09-03; the linter built]**

The user's framing: "since our final goal is to run everything Python in
pyodide, eventually we'll need to find a way to expose GUI function ...
my current thought is to have some kind of bridge, or PySide shim, so
that we don't expose PySide to user code directly."  The test named:
port Draft and BIM (Arch is folded into BIM in this fork).

### 7.1 The decision: FreeCAD's API, not Qt's

A shim that speaks Qt's API is one synchronous hop per property read
(Electron removed exactly that as its `remote` module), is a denylist
over the process, has to be re-implemented over DOM for the browser,
and preserves accidents like building Coin nodes to draw a dimension.
The shim speaks FreeCAD's API, generated as facades with a permission
per op (`gui`: DENY document / ALLOW session and addons):

- **U1 Registration** (data): `addCommand`, `addWorkbench`, icon and
  language paths, preference pages.
- **U2 Host services** (one op each): message/question/input dialogs,
  the file dialog (an `fs` grant), cursor, clipboard (grant),
  `defer(ms, callback)`, hints, plus the ops the linter found missing:
  `openUrl` (a grant), a status-bar message, a theme query,
  `preferences.show`, and composed icons `icon.swatch(color, shape)`
  and `icon.overlay(base, badge)`.
- **U3 Forms**: sec 7.3.
- **U4 Selection, view, edit**: `Selection.*` with observers as
  events, camera get/set, pick queries, `setEdit`/`resetEdit`,
  `runCommand`.  `doCommand(src)` is an eval primitive: it runs under
  the CALLER's principal in the caller's guest, never escalates, and is
  gated by `gui.doCommand` -- DENY document (not promptable), ALLOW
  session, PROMPT addon persisted per addon -- with an audit line
  carrying a hash of the source **[decided]**.
- **U5 The scene**: sec 7.2.
- **U6 Tools**: the 3D event stream (Coin events serialized to guest
  callbacks, coalesced per frame) and the snapper.  The snapper's move
  to C++ stands on cost (its per-move work is geometry queries against
  host shapes) but is deferred: no longer a correctness blocker.
- **U7 Compatibility**: a `PySide`-named module in the guest covering
  the data-level subset (translate, icon references, `Qt` enums, the
  static dialog calls, `QTimer.singleShot`, value types), and the
  porting linter.

### 7.2 The scene: pivy in the guest, mirrored **[decided: "can we just port pivy over to wasm in full? I don't see any security problem with that"]**

Coin and `pivy.coin` compile into the guest; Draft builds real Coin
graphs there; the host replicates them into its scene through a
mirror, field changes coalesced per frame, the widget protocol of 7.3
as the wire (a node is a model whose traits are its fields; the model
classes are generated from Coin's field introspection the way pythreejs
generates from three.js).  All 555 pivy uses in Draft and BIM port
unedited, including the ghost trackers' `writeInventor -> SoInput`
strings, and desktop rendering does not change because the host scene
holds the same nodes.  The user's reading on security is right: Coin
inside the guest exposes nothing; the boundary is the mirror reader,
which enforces a node-type allowlist (nothing carrying a file path or
code: `SoFile`, `SoTexture2` by filename, `SoImage`, `SoWWWInline`,
`SoShaderObject`, `SoCallback`, VRML script and inline nodes), quotas
on node counts and inline texture bytes, a structured wire of type
name plus typed fields (never Coin's Inventor parser on untrusted text
on the host), and a check that a selection node's document path belongs
to the principal.  Reads of the HOST scene (`getSceneGraph`, camera
node, pick, bounding-box and search actions, about 30 uses) stay ops;
FreeCAD's own node types created by name (`SoBrepEdgeSet` x9,
`SoFCSelection`, `SoDatumLabel`, `SoSkipBoundingGroup`) get guest
stand-ins.  Cost: the Coin fork has no emscripten support (Boost
headers, OpenGL, X11, with GLX and EGL behind options); native pivy is
34 MB with symbols, so the guest grows by roughly 15 to 20 MB of wasm.
Probe A (sec 11) decides between real Coin and a Coin-shaped model
library; both keep the mirror.

### 7.3 Forms: the Jupyter widget protocol **[decided: "the Jupyter + pythreejs pair is very fitting for us. I'd like to grow on it"]**

U3's wire is the Jupyter widget protocol (comm open, state as key-value
diffs, binary buffers, custom messages; about forty core models
specified attribute by attribute; two model-side implementations,
ipywidgets in Python and xwidgets in C++).  Guest side: ipywidgets
unchanged in the package set plus a comm shim of about a hundred lines
over the bridge (`comm.create_comm` / `get_comm_manager`, as xeus and
JupyterLite's pyodide kernel do; JupyterLite already runs ipywidgets on
pyodide from a Web Worker).  Host side: widget managers we write -- Qt
first -- plus a FreeCAD widget module registered on both sides for
quantity inputs, a selection input (Fusion 360's `SelectionCommandInput`
shape), color buttons, and a tree/table model widget with typed cells
(the QStandardItem panels of BIM).  `.ui` stays the AUTHORING format:
the guest parses it for object names and classes, the host manager
reads the same file for layout (Qt through `uic`, exact); the U7
subset gives models Qt-flavored accessors so the 25 `loadUi` panels
port with little edit.  Only host-registered model names render; a
widget package's own JavaScript is never loaded; an unknown model is a
labeled placeholder.  No webview escape hatch for now (if the browser
tier ever needs one, MCP Apps is the shape).  Caveats: traitlets is
heavier per attribute than a hand-rolled model; the `HTML` and `Output`
widgets are browser-shaped, native managers support a subset.

### 7.4 The toolkit transition **[decided]**

No Python toolkit has both a Qt and an imgui backend (Dear PyGui,
imgui_bundle, pyimgui are imgui only and immediate mode; Toga has the
shape but neither backend; Slint's royalty-free license requires
attribution and is GPLv3 otherwise).  The transition goes through the
managers: the Qt manager first, then a Qt-free manager over bgfx for the
same models, drawn in the 3D viewer and identical in the browser viewer
-- the first shipped step of retiring Qt.  For its renderer RmlUi is
preferred over Dear ImGui (the ipywidgets `Layout` model is CSS flexbox,
which RmlUi implements; imgui would need Yoga plus a retained walker;
imgui is vendored under bgfx, RmlUi is not); decided on a measured
prototype when that manager is built.

### 7.5 What the linter found (`scripts/sandbox_gui_lint.py`)

Run with `.conda/freecad/bin/python3` (Draft and BIM use `match`;
system Python is 3.8).  It buckets every PySide/pivy/FreeCADGui use in
Draft and BIM by longest prefix in a table that is the design above as
an allowlist; `--surface` reports the App-side API members a set of
files uses against the `<Sandbox>` annotations; `--list FILE`, `--all`,
`--no-edit`, `--ui`, `--json`, `--self-test` (25-use fixture).  Live
numbers, 467 files (Draft 243, BIM 224; 19 + 49 `.ui`):

    bucket         uses   files   meaning
    -------------  -----  -----   -------------------------------------
    subset         2187    189    U7 covers it, no edit
    U1              335    169
    U2              310     75    includes composed icons, openUrl, status, theme
    U3             1504    125    forms, including the tree/table model widget
    U4              846    192
    U4.doCommand    247     47
    U5              503     29    Coin, mirrored: ports unedited
    U6              243     62    event callbacks, the snapper
    unmapped        107     33    the residue

    files: clean 189; no edit 118 (112 subset-only, 6 mirror-only);
    form or tool work 127 (10 of them also mirrored); with residue 33.
    Files needing an edit 160 of 467; uses needing an edit 1854, of
    which 1504 are forms.

The residue is event filters and synthetic events (19), BIM state hung
on the `Gui` module (11), filesystem calls (9), rich text in ArchReport
(8), host-scene reads (about 20 across handles, actions and cameras),
an MDI-area walk in WorkingPlane (4), Quarter handles (4), this fork's
live-import switches (4): rewrites in the files that own them or
host-scene query ops, no protocol gap.  The work list is led by forms:
`ArchPrecast.py` (108 U3), `ArchReport.py`, `DraftGui.py`,
`ArchComponent.py`, `ArchCoveringGui.py`, `ArchWindow.py`.  The `.ui`
subset the managers must render: 41 widget classes, 16 of them
FreeCAD's own `Gui::` widgets, the `Pref*` family a third of custom
instances.

Draft's App side (`draftobjects`, `draftgeoutils`, `draftfunctions`,
`draftmake`, 97 files) reaches the GUI through exactly three names:
`QT_TRANSLATE_NOOP`, `QTimer.singleShot`, `getViewDirection`.  BIM's
`Arch*.py` files hold object, view provider and panel together; G1
loads them whole with the subset present.

### 7.6 G1 sized: Draft's App side in the guest

`--surface` over that App side: 187 distinct declared members, 4231
reads (TopoShape 41 used / 5 annotated, GeometryCurve 18, BoundBox 17,
TopoShapeEdge 17, Vector 17, DocumentObject 13 / 2, Rotation 13,
Document 12 / 2, Placement 12, ...), 32 `Part` module names (374 uses),
16 `FreeCAD` names (692 uses, `Vector` 332).  Status 2026-09-04: the
`write_prop` op and the write family are BUILT (3.2); the 187-member
surface is ANNOTATED (3.3: 239 members, 50 facades) and proved on
handles from the guest (`partSurfaceOnHandles`: 31 expressions over a
box and its sub-shapes, curves and surfaces -- handle-tier attributes,
value-tier reads, declared calls with handle arguments dereferenced,
booleans between two handles -- each equal to the host's own answer on
both runtimes).  The `Part` module facade is BUILT (3.2: 30 callables,
`OCC_VERSION`, `OCCError`; gate `partModuleFacade`, 14 constructions
and reads equal to native on both runtimes, the exception mapped).
**G1a gate PASSED 2026-09-04** (`draftgeoutilsOnHandles`): the 16
geometry modules of `draftgeoutils` (all but `geo_arrays`, which
makes document objects) pushed into the guest UNMODIFIED through
`exec` and registered on the host from the same source, 116 calls
covering every public function the fixtures can feed, on 14 host-made
shapes bound as handles, each answer normalised (shapes to kind,
measures and counts; values rounded) and equal as text on both
runtimes; 11 of the 116 fail identically on both sides (curve-only
functions fed an edge, the deprecated `sortEdges`, a `NameError` in
Draft's own `get_spline_normal`, `removeSplitter` on a clean box) --
parity, not a sandbox gap.  What the gate forced into the guest is
listed in 3.2 and above (`bool`/`str` ops, type references,
`ShapeList` as list, `GuiUp`/`Console`/`Base`/unit constants).
The traffic it measured, 116 calls: `release` 4977, `get_attr` 2738,
`call` 587, `mod_call` 153, `bool` 50, `read_prop` 29, `str` 18 --
about 74 hops per call, and 58 percent of them were releases of
proxies the guest let go one at a time.  Step 6 of the coding order
made releases ride the next request or the reply (3.2): the same 116
calls now make 3575 hops -- `get_attr` 2738, `call` 587, `mod_call`
153, `bool` 50, `read_prop` 29, `str` 18, `release` 0 -- 31 per call,
~0.2 ms of crossing per call at sec 8.1's hop cost, and the per-eval
pack cost lost its release hop (8.1).  What remains is dominated by
`get_attr` on shapes and curves; a snapshot op would target exactly
those reads, and this is the count to size it from.
**G1b BUILT 2026-09-04** (`draftWheelInGuest`): Draft's App side boots
with the pyodide guest as the bundled `fcx_draft` wheel (5.6) -- no
`exec`, no stubs; every module of the wheel imports in the guest (the
gate found one unguarded view-provider import in `draftmake/
make_polygon.py`, fixed in Draft the way its siblings guard theirs),
the preference reads answer from the host through the
`draftutils.params` facade (3.2) equal to the host's own, and the G1a
calls agree from the wheel.  Still missing: a `FreeCAD` module facade for `ActiveDocument`
(69 uses, all in `draftmake`/`draftfunctions` -- the make_* commands,
not an `execute()`), rung 2, and a local-wheel source in
`install_package`.  Document-level writes (`addObject` 76 uses,
`removeObject`) are NOT declared: they are what Draft's make_* commands
do, not what an `execute()` does, and rung 2 writes self only -- a
decision to revisit with G1c.  NOT missing, checked 2026-09-04: the
guest-native value classes.  The
in-image `FreeCAD` module has carried `Vector`, `Rotation`,
`Placement`, `Matrix`, `BoundBox` and `Units.Quantity`/`Unit` since the
core carve, and since the draftgeoutils gate also what workbench code
reads before doing geometry: `GuiUp` (0), a `Console` whose `Print*`
write to the guest's stderr, `FreeCAD.Base` (the same classes and
exception types, for `from FreeCAD import Base`), and the unit
constants `Units.Radian`, `Units.Metre`, ... from `Quantity::unitInfo()`
as the host's Units module carries them -- none of it crosses, all of
it lives in the
core carve (ImageDispatch.cpp), so the 1.6 k vector operations never
hop.  Gate passed on both runtimes
(`ExpressionImageEvalTest.draftVecUtilsInGuestZeroHops`): DraftVecUtils
pushed into the guest UNMODIFIED through the `exec` op (its
`draftutils`/`freecad.deprecation` imports stubbed -- those pull PySide
and the parameter store, G1b's loader problem), all 23 public functions
evaluated in the guest and on the host from the same source, results
within 1e-12 (wasm libm included), `stats()` showing zero bridge ops
and zero handles.  Plan: G1a the surface
(gate: every public `draftgeoutils` function run in the guest on
handles, results equal to native); G1b the Draft wheel and the loader;
G1c rung 2 for one principal (gate: one Draft Wire's `execute()`
byte-identical); G1d the Draft and BIM test documents recomputed with
routing ON.  Three decisions pending before G1a: annotate as `call` now
(host CPython) with rung 1 later; Draft in the addon guest rather than
a per-document guest; geometry values bound to the wasm value layer
rather than pure Python (proposal: wasm, because byte-identical shapes
is the gate).

**G1c SIZED 2026-09-04: the Proxy dispatch.**  What exists: a scripted
object's `Proxy` is a host Python instance; `FeaturePythonT::execute()`
(`FeaturePython.h:201`) calls `FeaturePythonImp::execute()`, which
calls `Proxy.execute(obj)` through the Python C API, one of the 22
hooks in `FC_PY_FEATURE_PYTHON` (`execute`, `mustExecute`,
`onBeforeChange`, `onChanged`, `onDocumentRestored`, ...); `imp->init`
re-reads which hooks the Proxy defines whenever the `Proxy` property
changes (`FeaturePython.h:363`).  The Proxy is installed by the class
itself (`obj.Proxy = self`, `DraftObject.__init__`) and persisted by
`PropertyPythonObject` as `<Python module=".." class="..">` plus the
`dumps()` JSON; `Restore` imports the module and allocates the class ON
THE HOST (`PropertyPythonObject.cpp:379-390`) -- the sec 13 gap.
Draft's `Wire.execute()` (`draftobjects/wire.py:124`) reads 14
properties, calls `Part.LineSegment().toShape()`, `Part.Wire()`,
`obj.positionBySupport()`, writes `Shape`, `Area`, `Length`,
`Placement`, `Start`, `End`, and relies on `self.props_changed`, a list
`onChanged` fills and `execute` clears -- so the hooks are one
stateful object, not one function.

*Mechanism (proposed):* the Proxy instance lives in the guest (the
addon guest that already boots the `fcx_draft` wheel); the host's
`Proxy` property holds a STAND-IN.

1. Guest: a registry (host-assigned proxy id -> instance).
   `HostHandle.__setattr__('Proxy', inst)` registers `inst` and sends
   `write_prop Proxy` with a new wire value `{"t":"gproxy", "id",
   "mod", "cls", "hooks":[...]}`, `hooks` = the `FC_PY_FEATURE_PYTHON`
   names the class defines plus `dumps`/`loads`.  Two host->guest ops:
   `proxy_call {id, m, a:[owner handle, ...], owner_h, owner_fc}` (the
   hook call; the object crosses as a fresh transaction handle, exactly
   the `execute(self, obj)` signature) and `proxy_new {id, mod, cls,
   a:[handle]}` (construction; with `alloc` only `cls.__new__`, for
   Restore); a dead stand-in's id rides the release queue.
2. Host: an `App::ExpressionSandbox::GuestProxy` type whose
   `tp_getattro` answers exactly the declared hook names with bound
   forwarders -- `FC_PY_GetCallable` finds only those, so a hook the
   class does not define costs nothing (Wire: `execute`, `onChanged`,
   `onDocumentRestored`, `dumps`, `loads`).  Its `__module__` and
   `__class__.__name__` report the GUEST class, so `Save` writes the
   same `<Python module="draftobjects.wire" class="Wire">` a native
   session writes and the file stays readable by an unrouted FreeCAD.
   `Restore` with routing on: if the guest can import the module (one
   cached round trip per module name) build the stand-in, `proxy_new`
   alloc, forward `loads`; else refuse -- fail closed, no native import
   of a document-chosen module name.  `FeaturePythonImp` itself does
   not change: the stand-in raises the guest's exception type and
   message, and `FeaturePythonT::execute` already turns that into a
   `DocumentObjectExecReturn`.
3. RE-ENTRANCY, the real step.  A guest `execute()` writing `obj.Shape`
   runs the host's `onChanged` INSIDE the `write_prop` op, and that
   hook is in the guest too: a nested round trip while the guest is
   suspended in `fcx_host_call`.  Deferring the hook is not an option
   -- Draft's `props_changed_store`/`props_changed_clear` ordering is
   what `execute` relies on -- so the round trip must nest.  Both
   runtimes can: V8's `Locker` is re-entrant and a wasm export may be
   called from a native callback; wasmtime must be entered through the
   CALLER's context inside a host callback (`wasmtime_caller_context`),
   so the runtime keeps a current-context stack; the single
   `pendingReply` slot is safe because every nested op fetches before
   the outer reply is parked.  `ImageHost` needs a transaction stack
   (owner, deferral, security scope), releases flushed at the outermost
   end only, the budget watchdog armed once.
4. Extension methods.  `obj.positionBySupport()` belongs to
   `Part::AttachExtension` and is injected per INSTANCE
   (`ExtensionContainerPyImp.cpp:138-150`), invisible to the type-MRO
   walk of `facadeMemberLookup`; the guest facade class does not have
   it either.  Annotate `AttachExtensionPy.xml`; the host lookup falls
   back over the container's extension Python types; a handle carries
   its extension facade keys (`"ext":[...]`) and the guest composes the
   proxy class from facade plus extension mixins, cached per
   combination.

*Cost, counted on the 3-point open Wire fixture:* about 45 bridge hops
(read_prop ~24, write_prop 7, get_attr ~4, call 3, mod_call 3, bool 2)
at 5-13 us = 0.3-0.5 ms, plus 7 nested `onChanged` dispatches (one per
write) at ~0.1 ms each and their bodies (`Start`/`End`: ~4 hops), plus
the `execute` dispatch itself (~0.1 ms) -- roughly **1.3-1.8 ms of
crossing per Wire `execute()`** against ~0.15 ms native, so about +1.5
s on a 1000-wire recompute; and ~0.1 ms per property write during a
`make_*` (5-8 writes each).  Writes and hooks dominate, not attribute
reads, so the snapshot op does not help here; batching writes cannot
either without breaking the hook order.  G1d measures the real number.

*Steps, each shipping alone:* (a) `GuestProxy` stand-in and the `Save`
side; (b) the guest registry, `proxy_new`/`proxy_call`, the `Proxy`
setattr; (c) nested transactions on both runtimes; (d) extension
facades; (e) the gate; (f) the `Restore` route.  Gate: a Wire made
natively and one whose Proxy lives in the guest, from the same points:
BRep strings equal, `Points`/`Start`/`End`/`Length`/`Area` equal,
`Shape` arrived through `write_prop`, the saved `<Python>` element
equal.

*Decisions this leaves to the user* -- **both RULED 2026-09-04: (1)
"yes", Document-level writes stay undeclared for rung 2; (2) "allow
read only for params anywhere" = the `prefs.read` row of 2.2, ALLOW for
every principal class, and `draftutils.params` rides it (3.2); the
gate runs under a plain document principal with no grant.**
1. Document-level writes.  Recommendation: stay undeclared for rung 2
   (an `execute()` writes self).  Facts: no `draftobjects` `execute()`
   creates or removes objects; in BIM, `ArchStairs.execute` adds and
   removes its `RailingWire` objects and `ArchReference.execute` removes
   -- those two fail with routing ON until declared, G1d's list.
2. The principal of wheel code.  Ruling 1.4 says a Proxy runs as the
   document; `draftutils.params` is `app.query` = PROMPT for documents,
   and `Wire.__init__` reads `MakeFaceMode` -- a prompt per Draft object
   is not acceptable.  The G1b gate never met this: `host.eval` without
   an owner has no principal scope.  Options: a read-only preference
   class (`app.prefs`, ALLOW for documents; the facade reads Draft's own
   groups only) or wheel code running as `addon:draft` even when called
   for a document object.  Recommendation: the former; the ruling
   stands.

**G1c BUILT 2026-09-04, steps (a)-(e); (f) the Restore route remains.**
Built as sized (3.2): the guest registry and `proxy_new`/`proxy_call`,
the `GuestProxy` stand-in (a per-class heap type named after the guest
class, so `Save` is untouched and writes `<Python module=
"draftobjects.wire" class="Wire">`), nested transactions on both
runtimes, extension facades (`AttachExtensionPy.xml` annotated;
`"ext"` on handles).  `FeaturePythonImp` did not change: the stand-in
exposes exactly the hooks the guest class defines plus `dumps`/`loads`
(for a class without its own, the guest answers as the host would
natively: `__getstate__`/`__setstate__` when defined, else the instance
`__dict__`).  Gates: `guestProxyHooksNest` (both runtimes) -- a class
pushed by `exec`, constructed in the guest, its `execute()` doubling a
property through `write_prop` while the host's `onChanged` reaches the
guest again INSIDE that op, the log order `Proxy, Width, execute,
Width, after` exactly the native order, `dumps`/`loads` through the
stand-in; `draftWireInGuest` (pyodide) -- `Draft.make_wire` natively
against a `Part::FeaturePython` twin whose `Wire` Proxy was constructed
in the guest from the bundled wheel, the same three points written
from the host: **BRep byte-identical**, `Points`/`Start`/`End`/
`Length`/`Area`/`Closed`/`MakeFace`/Proxy module, class and `dumps()`
equal, the saved `<Python>` element equal, the Shape arrived through
`write_prop`.  Measured on that Wire: 11 proxy calls (construction,
the hooks the host fired for the writes, `execute`, its nested
`onChanged`s, two `dumps`) and 62 bridge hops -- `read_prop` 25,
`call` 15, `write_prop` 8, `get_attr` 6, `mod_call` 6, `bool` 2 --
against the ~45 hops + 7 nested calls sized above: the `call` count is
the `addProperty` calls of `Wire.__init__` (12) that the sizing left
out, the rest lands where estimated.  Still open from the sizing: (f)
`PropertyPythonObject::Restore` still imports the module on the host
(sec 13); the two decisions above; `obj.Proxy.<attr>` reads from HOST
Python (Draft's `get_type` reads `Proxy.Type`) have no forwarder yet --
a stand-in `__getattr__` over a `proxy_get` op when G1d needs it; a
guest reset orphans live stand-ins (their hooks raise
`ReferenceError`, never a silent new instance).

### 7.7 Decisions, numbered

1. `.ui` as the form language -- amended: the authoring format; the
   runtime model is the widget tree (7.3).
2. The snapper to C++ -- stands on cost, deferred.
3. Dimension view provider to C++ -- WITHDRAWN (7.2).
4. `doCommand` permission-controlled, never escalates -- stands.
5. G1 before G2 -- stands.
6. pivy in the guest, the scene mirrored (7.2).
7. The Jupyter widget protocol is U3's wire, ipywidgets the guest
   library (7.3).
8. The toolkit transition goes through the managers (7.4).

### 7.8 Prior art

    design                      pattern                                   backs
    --------------------------  ----------------------------------------  -----------
    VS Code extension host      contribution points, thin service API,    U1, U2, U4,
                                TreeDataProvider, webview escape hatch,   tree model
                                remote extension host
    Zed extensions              "describes what to render as data, Zed    U3
                                renders natively"; events return patches;
                                GPU access rejected
    Figma plugins               QuickJS in wasm after the Realms shim      runtime,
                                proved unsafe; typed document API; UI     network
                                iframe; manifest network allowlist
    Jupyter widgets, JupyterLite kernel models, front-end views, state   U3 wire,
                                diffs over comm; ipywidgets on pyodide    browser tier
    pythreejs, xthreejs         a scene graph as widget models, classes   the mirror
                                generated from a per-class config
    xwidgets, euporie           C++ model side; a non-DOM renderer        host managers
    Cash App Redwood, Zipline   logic in a QuickJS guest, schema-generated U3 wire,
                                widget protocol, native renderers         versioning
    Office.js                   proxy objects queue, one sync flushes     batching
    Fusion 360 command inputs   typed inputs, host renders, events        widget module
    Onshape FeatureScript       annotated parameters, generated dialog    same
    Adaptive Cards, Lyft Canvas declarative schema per host; versioning;  schema,
                                unknown-component fallback                fallback
    Unity UI Toolkit            retained UXML, one renderer everywhere    .ui authoring
    MCP Apps                    sandboxed iframe + JSON-RPC, no backchannel escape hatch
    Blender layout API          neutral vocabulary but draw() per redraw  why retained
                                in process with full access

## 8. Measurements

All on this box (6 cores, `conda-relwithdebinfo-801`); the bench gtests
(`ExpressionImageBenchTest`) are `DISABLED_*` and run by hand, so
these numbers are not CI-checked and can drift.

### 8.1 Evaluation cost

    what                                  native   WASI (Release)   pyodide (final)
    ------------------------------------  -------  ---------------  ---------------
    transport floor                       --       2.68 us          5.4 us
    wire floor (eval "1")                 --       12.8-15.7 us     22.2 us (was 30.6-37.6)
    parse + eval arithmetic               2.03 us  13.36 us (6.6x)  18.6 us (was 23.6)
    one property read                     ~3.0 us  20.92 us (7.1x)  29.7 us (was 37.1)
    one bridge hop (marginal, read_prop)  --       5.4 us (was 7.1-7.6)  4.4 us (was 5.9-7.4)
    pack per eval (export+proxy+release)  --       +19 us (was +23) +23 us (was +44, +27)
    instantiate + first eval              --       14 ms (.cwasm)   ~1.5-1.7 s

The pyodide "was" figures are the bytearray/getBuffer transport; the
current ones are the short-circuit of 2026-09-04 (sec 4.1: wasm
imports bound to V8 natives, exports called from C++), which took the
JavaScript out of both directions.  It cut the round trip by about 9
us and the per-eval pack cost by 17 us; the marginal hop itself moved
within noise (~7 us either way), because what remains of a hop is the
guest's CPython `__getattr__` + `_fcx.op` + CBOR (~3 us) and the host
dispatch (GIL, handle table, property read, security, encode, ~3-4
us), not the crossing.  The fixed-layout codec (step 7, sec 3.2) then
took its predicted 1-2 us: the marginal read_prop hop is 5.4 us on
WASI and 4.4 us on pyodide (2026-09-04, same bench).  What is left is
CPython on both ends; the transport is done.
    native Shape.Volume / BoundBox.ZMin   179/183 us (mass properties, not
                                          materialization: a TopoShapePy
                                          attribute read is ~1 us native)

The hop row was re-taken on 2026-09-04.  Every earlier figure for it
(WASI +31.6, pyodide +136.8 / +72.1 / +78-80 us) came from a bench that
reused ONE bindings pack across iterations: the guest copies the pack
into per-eval globals and releases the handle when those die, the host
flushes that release on the next call, so every iteration after the
first timed a stale-handle ERROR round trip.  The bench now builds a
fresh pack per iteration, asserts success inside the loop, and reports
the marginal hop as a slope over 1 and 4 reads of `o.Width` (the proxy
does not cache: `__getattr__` crosses every time).  The pack row is
what a raw `eval` with one object binding adds over the wire floor and
includes the release hop.
Pyodide's stages for the first two rows: first transport 40.5 / 89.0 us;
buffered 8.4 / 39.0 us; after `-fvisibility=hidden -flto` 5.4 / 30.6
us, "about 2x the WASI image".  The verdict from the WASI numbers:
architecture A is not a prerequisite; a 10k-cell arithmetic sheet
recompute projects to 0.25-0.31 s from 0.13 s today.  Every number
taken before the Release fix (2026-08-31) was 3 to 6x too slow; the
browser 140 us figure is one of those and was never re-taken.

### 8.2 The corpus gate

`scripts/expr-switchover/{corpus_regression.py, run_gate.sh,
summarize_gate.py, packaging_check.py}`: one `FreeCADCmd` subprocess
per file with a hard timeout (a single 7.7.2-to-8.0.1 forced-recompute
file can stall a whole-corpus sweep for ten minutes), enforcement OFF
in the rig (parity, not policy), paths passed through the environment
(real corpus paths contain quotes and backslashes; `FreeCADCmd` exits 0
when the script raised, so a missing summary file is the failure
signal).  Final state on WASI (2026-08-31): 372 files, 335 expressions
compared, 0 differ, 2 both-error with matching text, 2 timed out;
`scanner.FCStd`'s 73 expressions all match.  On pyodide (2026-09-02):
350 files, 334 compared, 332 same, 0 differ, 2 both-error, 1 timed
out -- the same result set.  Two parity gaps the gate could not see
(literals crossing at 15 digits, error text) were found and closed on
2026-09-02 (`f062e51e80`).

### 8.3 The corpus

456 documents under `~/works`, 7628 expressions, 362 unique strings
(2026-08-30): member reads 302, engine-builtin calls 57, foreign-document
references 69, pure arithmetic 34, one pseudo-property, one dotted
module call, zero `.Proxy` drill-down, zero `import_py`, zero `eval`.
Function frequency: `hiddenref` 29, `dbind` 17, `str` 10, `tuple` 7,
`trunc` 6, `tan` 4, `atan` 2, `sum` 2, `vector`, `ceil`, one
`_math.degrees`, one `Vector.getAngle`.  The King IFC model contributes
1742 x 4 (its `.Shape.BoundBox` bindings, 5808 cells, are host-side
TopoShape materialization the sandbox does not add to -- a handle
protocol could answer BoundBox from a host cache without building a
TopoShapePy at all); the CNC project contributes nearly all foreign
references; scanner.FCStd holds the only sub-shape drill-down.
Non-ASCII object names occur in real files.  Rig:
`scripts/expr-phase0/`.

## 9. Tests

    file                                          cases   covers
    --------------------------------------------  -----   ----------------------------------
    tests/src/App/ExpressionSecurity.cpp            11    catalog, hash, grant store
    tests/src/App/ExpressionSecurityRuntime.cpp      9    resolve, scopes, pending, audit
    tests/src/App/ExpressionImageHost.cpp           62    acceptance 6, bench 6 (disabled),
                                                          bridge 8, budget 4, eval 20 (the
                                                          G1a, G1b and G1c gates among them),
                                                          host 9, routing 9
    tests/src/App/ExpressionPyodide.cpp             10    layout, verify, scoping, offer
    src/Mod/Test/SandboxPyodide.py                   2    the offer end to end
    src/Mod/Spreadsheet/TestSpreadsheet*.py          --   run with routing ON for parity

The acceptance harness opens a real saved-and-reopened `.FCStd` under a
real `document:sha256` principal and runs hostile expressions through
every layer.  Suites green at `fd14ba2878`: C++ 536/536, Python 2630.
Every gtest and the corpus gate select a runtime per process through
`FCX_RUNTIME`.

## 10. Configuration reference

Preferences under `User parameter:BaseApp/Preferences/Expression/`:

    Sandbox:Runtime          "pyodide" | "wasi" (default: pyodide when built)
    Sandbox:Evaluate         route evaluation through the image (default OFF)
    Sandbox:BudgetMs         5000        Sandbox:GraceMs   1000
    Sandbox:ImagePath        Sandbox:StdlibPath          (WASI)
    Sandbox:PyodideDir       Sandbox:PyodideWheel        Sandbox:PyodideUserDir
    Sandbox:PyodidePackages  Sandbox:PyodideUnpinned
    Security:Enforce         default true

Environment: `FCX_RUNTIME`, `FCX_IMAGE`, `FCX_STDLIB`, `FCX_PYODIDE`,
`FCX_PYODIDE_WHEEL`, `FCX_PYODIDE_USER`, `FCX_PYODIDE_PACKAGES`,
`FCX_PYODIDE_UNPINNED`; rig-only `FCX_GATE_ONLY`, `FCX_SHEET_REPORT`,
`FCX_REPO`, `FCX_PROBE_*`.

CMake (`cMake/FreeCAD_Helpers/InitializeFreeCADBuildOptions.cmake`
:151-177): `BUILD_EXPR_PYODIDE_HOST` (default `v8-embed_FOUND`),
`BUILD_EXPR_WASI_RUNTIME` (OFF, never turns itself on),
`BUILD_EXPR_IMAGE_HOST` (ON iff either runtime; ON with none is a
configure error), `FREECAD_PYODIDE_DIR` (bundle a distribution in a dev
tree), `FREECAD_FCX_IMAGE_WHEEL` (ship the wheel under
`<datadir>/Pyodide/wheels/`), `FREECAD_BUNDLE_WASMTIME`.  With
`BUILD_EXPR_PYODIDE_HOST` and `BUILD_DRAFT`, target `DraftSandboxWheel`
packs `fcx_draft-<ver>-py3-none-any.whl` into the same wheels
directory (5.6).

Command line: `--grant <permission>[:<target>]`, `--policy <file>`.

## 11. Roadmap, one list

In order; each step ships alone.  Done: Phase 0 audit (2026-08-30),
Phase 1 image and router (2026-08-31), the pyodide runtime and budget
(2026-09-02), N0 demotion, P1 bootstrap and offer, G0 linter
(2026-09-03).

1. **G1** -- Draft's App side in the guest, four stages (7.6): G1a
   the surface, G1b the wheel and loader, G1c rung 2 for one principal
   (one Draft Wire `execute()` byte-identical from a guest Proxy) DONE
   2026-09-04 but for the Restore route, its two decisions ruled the
   same day; G1d the Draft and BIM test documents with routing ON
   remains.
2. **G2** -- U1 + U2 + U7: Draft and BIM register from the guest; the
   subset shim.  No dependency on G1; in parallel if hands allow.
3. **Probe A** -- Coin and pivy to wasm: compile the Coin fork with emcc
   (no GL, threads, fonts), build `pivy.coin` with the guest toolchain,
   time the boot and a 10 k-node graph.  **Probe B** -- ipywidgets in
   the package set, the comm shim, five widgets rendered in Qt from a
   guest script; boot cost and per-event latency.
4. **P2** -- in-place install into a running guest (the sec 9.3 probe of
   `SandboxNetwork.md`: does a wheel with compiled extensions import
   synchronously without `loadPackage`?).  Moved after G1: nothing
   built depends on it and a fresh guest boots in about 1.5 s.
5. **N1** -- `NetPolicy`, `NetClient`, `net.http.request`, the
   `XMLHttpRequest` shim, the user-level allow/deny preferences, the
   audit line.  Test: `requests.get` against a local server from a
   session guest, denied from a document guest, redirect hops.
6. **PyPI sources** for `install_package`, written once against N1's
   client.
7. **G3** -- U3 over the widget protocol: the Qt manager, the FreeCAD
   widget module, the `.ui` loader on both sides.  Gate: every Draft
   and BIM task panel opens from the guest and round-trips its fields.
8. **G4** -- the mirror: generated Coin models, the reader with its
   allowlist and quotas, host-scene query ops, stand-ins, the event
   stream.  Gate: the Draft test documents render identically (pixel
   compare on a converged scene).
9. **N2** -- `fetch` and the loop primitive.  **N3** -- network rows in
   the existing permissions panel, the addon manifest at install time.
   **N4** -- WebSocket (`net.ws:<origin>`), and `SOCKFS` under its own
   `net.socket` permission.
10. **G5** -- the snapper in C++, when measured to matter.  **G7** --
    the Qt-free manager over bgfx (no Draft/BIM gate of its own).
11. **Rung 1** -- generated C++ dispatch for `call` members; the
    App-core severance.  **Rung 2** -- per-document guests.
12. **N5 / G6 / the switch** -- `Python/Runtime = pyodide`, Draft and BIM
    with an empty GUI island, the full Draft and BIM suites green with
    routing ON, the browser drawing a Draft line end to end.

Not on the roadmap: a socket-level host capability, UDP, listening
sockets, any network for the reference image, a webview escape hatch.

## 12. Traps

- A `FREECAD_USER_HOME` pointing at a nonexistent directory is silently
  ignored and writes the REAL `user.cfg`; an ad-hoc gate run once left
  `Enforce=0` there.
- The image directory is read-only after an install: the compiled
  module cache must live under `<user cache>`; its file name hashes the
  full image path (collision if two images share a base name).
- `libwasmtime.so` has no SONAME: `IMPORTED_NO_SONAME` is honoured only
  on a target CMake knows is SHARED.
- Two `nlohmann::json` copies collide on exported symbols carrying
  `json` in the signature.
- The v1 catalog ALLOWs addons: a refusal test needs a document
  principal.
- Pyodide's shell-mode `resolvePath` is the identity (5.4); a dev tree
  staging numpy beside the runtime makes an offer test pass vacuously.
- `ImageHost::evalExpression` must hold its `Security::Runtime::Scope`
  across the round trip, or a mid-evaluation bridge op (`pkg.missing`)
  has no principal unless an outer entry pushed one.
- The pyodide guest module must be `add_executable` with a `.so`
  suffix; `add_library(MODULE)` becomes an `emar` archive.
- Windows and macOS path handling in the pyodide scoping is by
  construction only; nothing ran there.
- `pre-commit` and `black` are not on this box's PATH.
- The WASI stdlib slice has no `importlib`: guest prelude code imports
  with `__import__` and walks dotted names by hand.
- `FeaturePythonT::Proxy` is private; tests reach it through
  `getPropertyByName("Proxy")`.  A `PropertyFloat::setValue` with the
  value already held fires no `onChanged`.
- A stand-in decoded twice must be the SAME object (the `write_prop
  Proxy` path and the `proxy_new` reply both carry the descriptor): a
  duplicate dying would drop a proxy still in use, hence the live table
  in `ExpressionGuestProxy.cpp`.
- Two pre-existing, non-security bugs noted in passing and not fixed:
  `calc()`'s non-inplace `OP_MOD` branch also calls
  `PyNumber_InPlaceRemainder`; `ObjectIdentifier::Component::del`
  falls through to an unconditional throw after a successful delete.

## 13. Known gaps and open questions

- Not closed by the sandbox: `PropertyPythonObject::Restore`'s
  `PyImport_ImportModule` on a document-chosen module name
  (`PropertyPythonObject.cpp:379`) and `Base::Type::importModule`'s
  type-string import (`Type.cpp:85`), both at document OPEN, before any
  expression runs.  Rung 2 is where they close: G1c built the stand-in
  and `proxy_new alloc` for it (7.6); the `Restore` route itself --
  routing on, module the guest can import, stand-in instead of a host
  import, fail closed otherwise -- is G1c step (f), not built.
- The GUI live expression editors evaluate as session, unconfined.
- No memory ceiling for a guest.
- Addon principal granularity (per addon, per file?) is still open.
- Whether a document network grant may ever be "always" (a
  content-hashed identity makes it safe against tampering; a
  long-lived grant is still a long-lived channel).
- The PyPI resolver: host-side against PyPI's JSON API, or micropip in
  the guest with a host-fed index.
- Probe A's outcome (real Coin in the guest or a Coin-shaped model
  library); whether the mirror reader is the widget manager with a
  second model family; widget-protocol versioning between a shipped
  guest wheel and an older host (the manager refuses a newer major);
  the browser-only tier's rendering of the mirrored subset.
- The audit log's retention and where the Report view shows it; the
  addon manifest format.

## 14. Sources and what the audit found

    source                        what moved here                    stale in the source
    ----------------------------  ---------------------------------  ---------------------------------
    ExpressionSandbox.md          secs 1, 2, 3 (model), 4-7 (arch),  opening line "nothing here is
                                  8 (ladder), 9 (gaps), rulings      built"; getvar "disabled" (deleted);
                                                                     generator output names; secs 10-11
                                                                     spent
    ExpressionSandboxPhase0.md    frozen contracts (2.1-2.3),        every file:line (Phase 1 rewrote
                                  corpus (8.3), measurement verdict  the cited functions); "commented
                                                                     out" module check is superseded
    ExpressionImage.md            4.2, 4.3, 4.4, 3.2, 3.4, 3.5,      last line "WASI stays the shipping
                                  8.1, 8.2, traps                    default"; two dangling references
                                                                     to a section that never existed
                                                                     (now 3.2's deferred release);
                                                                     option line numbers; test counts
                                                                     (7 -> 8, 44 -> 59)
    PyodideHost.md                4.1, 5, 8.1                        sec 9 "else wasi"; "to become
                                                                     PyodideHost.cc"; three installer
                                                                     functions and two parameters
                                                                     undocumented
    SandboxNetwork.md             6, 1.2, 11                         N3 describes the panel as unbuilt
    SandboxGui.md                 7                                  sec 1 survey counts (grep, not
                                                                     the linter); sec 8 first-pass
                                                                     numbers no longer reproduce;
                                                                     sec 10.5 rollup omitted 10 files
    SpreadsheetRemote.md sec 6    4.3 (browser preview numbers)      140 us is a WASI -O0 number

## 15. References

- Electron `remote` deprecation: https://github.com/electron/electron/issues/21408
- Qt Remote Objects: https://doc.qt.io/qt-6/qtremoteobjects-index.html
- VS Code: https://code.visualstudio.com/api/references/contribution-points,
  https://code.visualstudio.com/api/extension-guides/tree-view,
  https://code.visualstudio.com/api/extension-guides/webview
- Zed custom rendering: https://github.com/zed-industries/zed/discussions/37270;
  https://zed.dev/blog/zed-decoded-extensions
- Figma plugin security: https://www.figma.com/blog/an-update-on-plugin-security/;
  https://www.figma.com/plugin-docs/manifest/
- Jupyter widgets: https://github.com/jupyter-widgets/ipywidgets/blob/main/packages/schema/messages.md;
  https://ipywidgets.readthedocs.io/en/latest/examples/Widget%20Low%20Level.html;
  https://github.com/ipython/comm; https://github.com/jupyter-widgets/ipywidgets/issues/3209
- JupyterLite: https://jupyterlite.readthedocs.io/en/stable/howto/configure/kernels.html;
  https://github.com/jupyterlite/pyodide-kernel
- pythreejs: https://github.com/jupyter-widgets/pythreejs/blob/master/CONTRIBUTING.md;
  xwidgets: https://github.com/jupyter-xeus/xwidgets; euporie:
  https://euporie.readthedocs.io/en/latest/apps/console.html;
  qtconsole issue #382: https://github.com/jupyter/qtconsole/issues/382
- Cash App: https://code.cash.app/native-ui-and-multiplatform-compose-with-redwood;
  https://code.cash.app/zipline; https://github.com/cashapp/redwood/releases
- Office.js: https://learn.microsoft.com/en-us/office/dev/add-ins/develop/application-specific-api-model
- Fusion 360: https://help.autodesk.com/cloudhelp/ENU/Fusion-360-API/files/CommandInputs_UM.htm;
  Onshape: https://cad.onshape.com/FsDoc/uispec.html
- Server-driven UI: https://medium.com/@aubreyhaskett/server-driven-ui-what-airbnb-netflix-and-lyft-learned-building-dynamic-mobile-experiences-20e346265305;
  Unity UI Toolkit: https://docs.unity3d.com/6000.3/Documentation/Manual/ui-systems/introduction-ui-toolkit.html
- MCP Apps: https://modelcontextprotocol.io/extensions/apps/overview
- RmlUi: https://github.com/mikke89/RmlUi; Yoga: https://github.com/react/yoga;
  Dear ImGui bindings: https://github.com/ocornut/imgui/wiki/Bindings;
  Slint license: https://github.com/slint-ui/slint/blob/master/LICENSE.md
- Deno permissions: https://docs.deno.com/runtime/reference/permissions/;
  Node permission model: https://nodejs.org/api/permissions.html;
  Spin outbound HTTP: https://spinframework.dev/v3/http-outbound;
  pyodide.http: https://pyodide.org/en/stable/usage/api/python-api/http.html;
  urllib3 emscripten: https://urllib3.readthedocs.io/en/stable/reference/contrib/emscripten.html;
  Emscripten networking: https://emscripten.org/docs/porting/networking.html
- pyodide releases: https://github.com/pyodide/pyodide/releases;
  v8-embed: `~/works/sw/v8-embed-feedstock`
