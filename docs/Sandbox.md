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
    GUI registration from the guest  built       src/Gui/SandboxGui.cpp, the guest FreeCADGui (7.9)
    Coin + pivy inside the guest     built       src/App/PyodideHost/pivy/, Coin's COIN_BUILD_GL_STUB (7.10)
    forms: ipywidgets over comm, Qt  built       src/App/ExpressionImage/widgets/ (guest), src/Ext/freecad/widgets/ (host), SandboxGui.cpp (7.3)
    forms: the Qt subset, .ui, panel built       G3a: freecad.widgets in the guest (the Qt classes as models), loadUi both sides, Control (7.11)
    forms: dialogs, item views, ...  built       G3b: exec_(), the tree/list/table family as rows, containers, the file chooser; the 40-file harness (7.11)
    host widget layer: core, Qt view built       H0: src/Gui/Fw/ (Fw:: models, FwQt:: backend, the store, FreeCADGui.FormWidgets), src/Tools/fwuic.py (7.12)
    native panels on the layer       sized       H1-H3: the first ports, the form-only majority, the item views; DOM walker later (7.4, 7.12)
    the session document (commands) built       S1: a workbench reaches every open document, live ActiveDocument, app.write, save, picker-blessed saveAs; S2: Gui.doCommand / addModule in the guest under gui.doCommand, Draft's commit and Arch_Site end to end; gate SandboxSessionDoc (7.13)
    routing ON by default            not yet     preference Expression/Sandbox:Evaluate
    host file / code chokepoints     designed    7.14: fs.read / fs.write / host.exec at the core's file and runFile primitives, keyed on the scope stack; closes Gui.runCommand("Std_RecentMacros") from a guest
    network capability               designed    sec 6
    GUI protocol, mirror, widgets    designed    sec 7 (U1, U3's wire and Qt manager, the guest's Coin are built)
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
    doc.write.self    ALLOW      ALLOW     ALLOW   write_prop + write family, the owner's
                                                   DOCUMENT: self = same origin (3.2; user
                                                   ruling 2026-09-05, owner-only retired)
    doc.foreign       PROMPT     ALLOW     ALLOW   the cross-origin wall
    geom.call         ALLOW      ALLOW     ALLOW   cost bounded by budget, not grant
    app.query         PROMPT     ALLOW     ALLOW
    prefs.read        ALLOW      ALLOW     ALLOW   the parameter store, read only, through a
                                                   curated reader (draftutils.params); added
                                                   2026-09-04, user ruling (7.6 G1c decision 2)
    prefs.write       DENY (np)  ALLOW     ALLOW   the parameter store, written through the
                                                   same facade (draftutils.params.set_param,
                                                   ParamGet(...).Set*): a task panel storing
                                                   the user's choice; added 2026-09-06 (7.11)
    gui               DENY (np)  ALLOW     ALLOW   non-promptable for a document, as prefs.write
    host.import:<m>   PROMPT     PROMPT    ALLOW   per module
    unsafe.getattr    DENY       PROMPT    ALLOW   host-side Python attribute walks
    pkg.install:<p>   PROMPT     PROMPT    PROMPT  an ACTION, never a grant (sec 5.5)
    gui.doCommand     DENY (np)  ALLOW     PROMPT  Gui.doCommand / doCommandGui / addModule from
                                                   the guest: the source runs IN the guest, the
                                                   host records the macro line and an audit line
                                                   (the source's sha256); the one row an addon
                                                   is prompted for; built 2026-09-07 (7.13, S2)
    app.write         DENY (np)  ALLOW     ALLOW   newDocument/closeDocument/setActiveDocument
                                                   (FcxWire app.new_doc/close_doc/set_active_doc);
                                                   ruled and built 2026-09-07 (7.13, S1)
    fs.read:<path>    DENY (np)  PROMPT    PROMPT  a host file read at the core's chokepoints
    fs.write:<path>   DENY (np)  PROMPT    PROMPT  (Gui::Application::open/importFrom/exportTo,
    host.exec:<path>  DENY (np)  PROMPT    PROMPT  openDocument, saveAs, MacroManager::run,
                                                   Interpreter::runFile), a picker-blessed path
                                                   passing; designed (7.14), not in the enum yet
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
`mod_call`, `mod_get`; and the `gui.*` family (2026-09-05, 7.9) --
`gui.cmd.add`, `gui.cmd.list`, `gui.wb.add`, `gui.wb.remove`, `gui.wb`,
`gui.wb.active`, `gui.wb.list`, `gui.icon_path`, `gui.lang_path`,
`gui.pref_page` -- which `App` does not answer: the bridge has an op
REGISTRY (`registerBridgeOps(prefix, handler)`, matched after the
built-in ops, inside the same GIL and exception net; request and reply
cross that seam as CBOR bytes, since a json in a signature does not link
between the two nlohmann copies, 12) and `Gui` registers
the family when its `Application` is constructed (`SandboxGui.cpp`),
every op checked against the catalog's `gui` permission.  `bool` and `str` (2026-09-04) are the proxy's
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
must belong to the EVALUATION OWNER'S DOCUMENT -- the owner itself
(`HandleTable::owner()`, set by `evalExpression` and by a raw `eval`
that names one), any object of its document, or that document -- under
`doc.write.self`, then the C++ property system's `setPyObject` (typed;
a shape re-maps its element map), never host Python.  "Self" is the
same-origin document, as 2.2 defines the principal: a document
rewriting its own objects is native behaviour (ArchStairs sets its
railings' `Base`, a PipeConnector its pipes' offsets, a Schedule its
Result sheet's cells), and the wall that matters is `doc.foreign`, an
object of another document.  Until 2026-09-05 the gate was OWNER ONLY
-- rung 2's "an execute() writes self" scoping, encoded as policy; the
user ruled it retired ("I thought it means an object can write anything
inside its own document"), and with it `Document.addObject`/
`removeObject` on the owner's own document are declared under the same
permission (7.6 decision 1 revised).  `Immutable` refuses, as native `setattr` does; `ReadOnly`
is the editor's status and writes through it succeed natively, so they
do here too (the gate is parity, not extra policy).  The value
decodes through the handle table, so a host object crossing back as a
handle dereferences to the live object (a `PropertyLink` takes the
object, not its wire face).  `addProperty`, `removeProperty`,
`setPropertyStatus` are declared `call` members (DocumentObjectPy.xml,
PropertyContainerPy.xml -- the facade chain now reaches
PropertyContainer through ExtensionContainer) and ride the `call` op
behind the same same-document gate (so do `configLinkProperty` and
`setLink` of the Link extension, G1d; `Document.addObject`/
`removeObject`; and the Sheet's cell writes `set`, `clear`, `clearAll`,
`mergeCells`, `splitCell`, `setStyle`, `setAlignment`, `setForeground`,
`setBackground`, `setColumnWidth`, `setRowHeight`, `setAlias`,
`setDisplayUnit`, `insert/removeRows/Columns`, `touchCells`,
`recomputeCells` -- Schedule and Report fill their Result, 2026-09-05).  A `write_prop` on a handle
that is NOT a property container -- a value object the transaction
holds: a shape the guest built, a property's copy -- is no document
write: it takes a DECLARED attribute of the type (the same closed
table as `get_attr`) through the type's own setter under `geom.call`,
a read-only attribute refusing natively (G1d, 2026-09-04: Draft's
WorkingPlaneProxy sets `Placement` on the plane it made).  Refusals are `PermissionError`, not
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
G1d (2026-09-04) added two host->guest ops and one value for the
HOST's side of a Proxy: `proxy_get {id, n}` reads attribute `n` of the
registered instance -- data comes back by value, a callable as
`{"t":"gmethod", id, n}`, which decodes to a forwarder bound like a
hook (calling it is a `proxy_call` with the object argument as owner);
`proxy_set {id, n, v}` writes a VALUE (a host object would cross as a
handle no transaction outlives, so the host refuses it with a
TypeError before the trip).  The stand-in's `getattr` answers its own
dict first (the hooks), refuses dunders and the hook names the
descriptor did not list without a trip, and sends everything else as
`proxy_get`, keeping a method it got back on the stand-in; its
`setattr` is `proxy_set`.  `proxy_new` also takes `k` (kwargs): the
construction dispatch (7.6, G1d) sends a class's real call.
Three more guest->host ops came with BIM (G1d): `ext {h}` re-reads a
handle's extension facades after the guest called `addExtension` on
it (the proxy recomposes its class); `active_doc` answers the guest's
`FreeCAD.ActiveDocument` with the transaction owner's `Document`
through `get_attr` on the owner's own handle (None outside a hook);
`pkg.missing` is unchanged.  And the WRITE-BACK: every value a handle
proxy hands out (`read_prop`, `get_attr`) is stamped as that
attribute of the proxy (`PyObjectBase::trackAttributeOf`, the
prelude's `_fcx.track`), so a nested write -- `obj.Placement.Base =
v`, ArchFrame's `profile.Placement.Rotation = rot` -- writes the whole
value back through the proxy's `__setattr__` = `write_prop`, exactly
the write-back native FreeCAD performs for every PyObjectBase an
attribute returns (`startNotify`, which now takes a generic setattr
for a parent that is not a PyObjectBase).  Before it, such a write
was silently lost in the guest and the Arch Frame's profiles never
turned.  Two more identity rules from the same corpus: the host mints
ONE handle id per object for the life of a transaction (a use count
per id, the entry going with the last release), and a proxy compares
and hashes by id, so `obj in o.Hosts` and `a == b` hold as they do
natively (ArchComponent found no window to subtract from its wall
before); and `touch` on any object of the OWNER'S document (ArchWindow
touches its host wall from its own execute so the wall subtracts the
opening in the same recompute) was the first same-document write
allowed -- the special case the 2026-09-05 ruling made the rule for
every write.

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

Durable document-object handles (2026-09-05, 7.6 "G1d CLOSED"): an id
lives one transaction, but a Proxy keeps document objects on itself
across hooks natively -- ArchReport's `self.spreadsheet`, the `self.obj
= obj` of several `onDocumentRestored`s -- and the table that minted
the id is cleared by every expression evaluation in between (a
Schedule's Result sheet has a numeric cell, and a numeric cell is an
expression), so the second routed recompute of the BIM corpus found
ArchReport's cached sheet stale.  Every handle to a DocumentObject or
Document now carries its key in `"k"`: `[document, name]`, or
`[document]` for the Document itself (the host's `encodeHostValue`, the
guest's `getPyObject` for the expression owner).  The guest proxy keeps
it in `_k`, compares and hashes by key when both sides have one (so a
cached object equals a fresh handle to it, as natively), and sends
every op through `_hop`: on a `ReferenceError` from a stale id it asks
`resolve {a: key}` once and retries with the fresh id the host replies
(an integer; one use minted for the caller, the proxy keeps its class).
A cached handle that arrives as an ARGUMENT -- `obj.Ref = self.other`,
a PropertyLink taking a kept object -- re-resolves the same way inside
the host's decode, no op of its own.  The reach of a key is exactly
what the guest already has: the evaluation owner's document under
`doc.read.self` (what `owner.Document.getObject(name)` gives); a key
into another document is a `PermissionError` (a key is names the guest
could make up, a handle a capability it was handed), a name no longer
in the document a `ReferenceError` (deleted natively too, the Proxy
holds a dead object).  A value object -- a shape, a curve -- has no
key and stays transaction-scoped (natively a shape kept across hooks is
a copy anyway).  Two more from the same fix: `clearHandles()` is a no-op
while a round trip is nested (an expression an outer hook's write
recomputed would otherwise drop the hook's own live arguments), and the
corpus gates recompute TWICE, since one recompute never meets a cached
handle.  Gate: `guestHandlesDurableAcrossHooks` (both runtimes).

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
The same preference routes a document object's saved Proxy at document
OPEN (`ExpressionSandbox::proxyRestoreRouted()`, the preference alone,
no image boot to answer it): `PropertyPythonObject::Restore` builds the
`<Python module=".." class="..">` instance in the guest and holds the
stand-in (7.6 G1c step (f)); a module the guest cannot serve FAILS
CLOSED.  A view provider's Proxy is not routed -- the Gui side is not
in the guest (G2).

Python: `FreeCAD.ExpressionSandbox` -- `routed`, `setRouting`,
`available`, `imageInfo`, `evaluate`, `evaluateNative`, `exec` (statements
in the guest as the session principal, optionally as a named module),
`evalCount`, `stats`, `resetStats`, `reset`, `proxyNew`, `proxyInfo` (rung 2, 3.2),
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
manifest for another ABI is ignored with a warning.  The offer: the
guest's error reply asks the host one op, `pkg.missing {a: name}`,
for a `ModuleNotFoundError` that leaves the guest UNCAUGHT (2026-09-05;
until then a `sys.meta_path` finder asked at every failed import, and
lark's optional `try: import regex` in BIM's generated parser -- `regex`
is in the lock -- recorded an install offer nobody asked for; stats
now count `pkg.missing:<name>`), and from the expression language's own
`import x` statement (Expression.cpp `ImportModules`, guest build only):
nothing in an expression can catch that failure, and the engine rethrows
it as a Base exception before the reply looks, so the question is asked
at the import site while it is still a `ModuleNotFoundError` -- the
finder retirement had silently lost this path; `SandboxPyodide`'s
python-mode `import numpy` test, not rerun that day, found it on
2026-09-05.  Both sites share `FcxImage::missingImportOffer`.  The host answers from the lock's import map
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

`fcx_draft` grew on 2026-09-04 (G1d, BIM): `Draft.py` itself and the
two view provider modules it imports unguarded
(`draftviewproviders.view_base`, `view_draftlink`; their GUI reach
sits under `GuiUp`, and BIM derives view provider classes from
`Draft.ViewProviderDraft` at module level).  The second wheel is
`fcx_bim` (target `BimSandboxWheel`, `src/Mod/BIM/CMakeLists.txt`):
the 43 Arch modules a scripted object's Proxy lives in, UNMODIFIED --
their GUI halves stay behind `if FreeCAD.GuiUp` -- without the
workbench init, the commands, the importers, nativeifc, the GUI-only
modules and the tests; plus the generated SQL parser, an `importers`
package shim (`Arch.py` imports `importDAE.triangulate` at module
level; the shim's raises), and BIM's `Presets` as DATA under
`fcx_resources/Mod/BIM/Presets` (the packer's `--add-data` keeps
every file), where the guest's `FreeCAD.getResourceDir()` resolves,
so `ArchIFCSchema`, `ArchProfile` and `ArchComponent` read their
presets at the path they build natively.  What BIM's App side reads
off `FreeCAD` at import or in a hook and the guest module now
carries: `ParamGet` (read only: every `Get*` is a `prefs.read` through
the `freecad.prefs` module facade, `src/Ext/freecad/prefs.py`; a
`Set*` raises), `Qt.translate`/`QT_TRANSLATE_NOOP` (identity),
`getResourceDir`/`getUserAppDataDir`/`getUserMacroDir` (the latter
two a path that does not exist), `addDocumentObserver`/
`removeDocumentObserver` (registered, never fired), and
`ActiveDocument` -- a module property (the module's class is swapped
for one with it) answered by the `active_doc` op: the transaction
owner's `Document` through `get_attr` on the owner's own handle, None
outside a hook.  The corpus is `bimtests.bim_test_objects` (one of
every Arch class made headless: 68 objects, 59 scripted), the gates
`bimTestObjectsReopenRouted` / `bimTestObjectsBuiltRouted` (7.6).

A COMPILED bundled wheel joined them on 2026-09-05 (7.10): `pivy`,
Coin and `pivy.coin` built for the guest by the standalone cross
project `src/App/PyodideHost/pivy/`, shipped through the
`FREECAD_PIVY_WHEEL` cache variable exactly as the fcx_image wheel is
(installed under `Pyodide/wheels/`, mirrored into the build tree).
The layout scan keeps compiled wheels apart (`Layout::bundledCompiled`,
one per ABI and distribution) and the runtime loads those of the
running fcx_image's ABI after the pure-Python wheels.  It costs about
0.2 s of boot.

Three more pure wheels joined on 2026-09-05 for the forms (7.3, Probe
B): `fcx_widgets` (target `WidgetsSandboxWheel`, packed from
`src/App/ExpressionImage/widgets/`: the `comm` shim over the bridge and
the `IPython` stand-in), and two THIRD-PARTY wheels bundled unchanged
-- `ipywidgets` 8.1.9 from PyPI (not in the pyodide lock) and
`traitlets` 5.14.3 from the pyodide distribution -- named by the
`FREECAD_BUNDLED_WHEELS` cache list and mirrored like the pivy wheel.
Nothing downloads at build time: `scripts/sandbox-fetch-wheels.py DIR`
fetches the two by pinned sha256 (docs/DevEnvironment.md).  Their
declared dependencies are not fetched: `comm` and `IPython` are the
shims, `widgetsnbextension`/`jupyterlab_widgets` are browser assets
ipywidgets never imports.  The three cost 0.02 s of boot (1.62 s
against 1.59 s, three runs each, FreeCADCmd) and nothing until a guest
imports them.

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

**PROBE B PASSED 2026-09-05, first complete run.**  The shape it
settled:

- **Guest**: ipywidgets 8.1.9 and traitlets UNMODIFIED as bundled
  wheels (5.6).  The comm shim is a drop-in `comm` package
  (`src/App/ExpressionImage/widgets/comm/`, the API of jupyter/comm
  0.2.3): `Comm.publish_msg` is one op, `gui.comm [msg_type, comm_id,
  target, data, metadata, buffers]` (bytes cross as CBOR binary), and
  `CommManager` registers itself ONCE as a guest proxy
  (`FreeCADGui._register_comm_manager`, hooks `host_msg`/`host_close`/
  `host_open`) so the host can drive it back.  ipywidgets imports
  `IPython` at import; the real one is 12 wheels, 2.5 MB, 400 modules
  and **1.53 s** of guest import for `get_ipython() -> None` and a
  `display`, so an `IPython` stand-in ships in the same wheel (0.001 s);
  its `display(widget)` shows the widget on the host, the Jupyter idiom
  kept.  `FreeCADGui.showWidget(w, title, where)` / `hideWidget(w)` are
  the explicit form (`gui.widget.show`/`hide` by model id; `where` is
  `panel`, the task panel, or `window`).
- **Host**: `freecad.widgets` (`src/Ext/freecad/widgets/`), the manager
  -- one `Model` per comm (state, live views), `comm_open`/`comm_msg`/
  `comm_close`, `update` applied as a diff to the views, `echo_update`
  ignored, `custom` handed to the views; back to the guest as
  `host_msg(comm_id, {method: update|custom, ...})` on the stand-in --
  and `freecad.widgets.qt`, the Qt manager: a `View` per (model,
  rendering) with `applying` set while the guest's state is written so
  Qt's signals do not echo it, `IPY_MODEL_` references resolved for
  boxes/layout/style, twenty core model names covered (sliders, number
  inputs, Text/Textarea, Button, Checkbox/ToggleButton, Dropdown/
  RadioButtons/Select, Label/HTML, the boxes; Layout's px sizes and
  visibility honored, the style models data), an unknown name a
  labeled placeholder.  A shown root is a `TaskPanel` whose OK/Cancel
  reach the guest as `{"event": "accept"|"reject"}` on the root model.
  The four `gui.comm*`/`gui.widget.*` ops live in `SandboxGui.cpp` and
  are the `gui` permission: a document principal's `IntSlider()` is
  refused at its comm open.
- **Measured** (`scripts/sandbox-widgets-probe.py`, Xvfb, RelWithDebInfo):

      guest import traitlets / ipywidgets    0.047 s / 0.13 s (269 modules)
      six widgets + a VBox                   20 models (layouts, styles),
                                             20 comm opens, 0.5 ms each, 9 ms
      shown as a task panel                  25 ms
      Qt slider step -> guest observer ->    0.22 ms per step: 1 proxy call,
        guest label -> Qt label              2 nested comm ops (echo + label)
      button click (custom, no reply)        0.017 ms
      guest write -> Qt slider               0.175 ms net of a 0.015 ms exec
      boot with the three wheels             +0.02 s

  Two facts worth keeping: every ipywidget opens three comms (its
  Layout and Style models come first), so a form is ~3x its visible
  widgets in models; and a guest's own trait write fires its own
  observers exactly as natively, while the host sends nothing back for
  it (the gate checks `sent` unchanged).  Gate `SandboxWidgets` (2
  cases, 9): the form built, every view's Qt state, both directions,
  OK as an event, window/hide, close and `close_all` dropping the host
  models, the document principal refused.

### 7.4 The toolkit transition **[decided 2026-09-06: two backends, Qt and DOM]**

No Python toolkit has both a Qt and an imgui backend (Dear PyGui,
imgui_bundle, pyimgui are imgui only and immediate mode; Toga has the
shape but neither backend; Slint's royalty-free license requires
attribution and is GPLv3 otherwise).  The transition goes through the
WIDGET LAYER -- a toolkit-neutral model per Qt class, Qt's property
names as the state, an op log for layouts, Qt-named signals -- and
that layer serves exactly two backends: **Qt on the desktop, the DOM
in the browser.**  No in-canvas UI on either tier.  The 2026-09-03
ruling here (a Qt-free manager over bgfx, RmlUi preferred over Dear
ImGui for its flexbox) is WITHDRAWN: G3a made the models Qt-shaped
(7.11), which took the flexbox argument away, and the sizing in 7.12
took the rest.  On the desktop an imgui panel costs accessibility and
the native look and buys nothing the Qt task panel does not already
do; in the browser an imgui panel with a hidden DOM input fixes only
typing, and task panels are mostly typed fields (7.12 has the
comparison; `docs/ThinClient.md` ruled the same for the viewer's
chrome, and its SolidJS inspector is the DOM path already working).

The layer has two halves with one vocabulary.  The GUEST half is
`freecad.widgets` (7.11, built): the models a sandboxed panel builds.
The HOST half (7.12, sized) is the same Qt-shaped classes in C++, so
NATIVE task panels port onto it and the Python Qt manager retires.
User ruling, 2026-09-06: "I still want host side abstraction of
widget layer like in the guest.  it's just that they now only need to
serve two backends.  for now just focus on desktop.  later when Dom
backend is in, all host/guest side task panel come into browser for
free."  Ordering: the host layer's core and first native port (H0,
H1) come BEFORE G3b, so G3b's views are written once, in C++.

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
   **REVISED 2026-09-05 (user ruling, "yes agree"):** `doc.write.self`
   means the owner's DOCUMENT -- the same-origin reading its name always
   had (2.2) -- so the write gate is same-document, not owner-only, and
   `Document.addObject`/`removeObject` on the owner's own document are
   declared under it (3.2).  The alternatives listed for the four
   non-owner writers (an "objects I own" relation; a prompting
   `doc.write.other`) existed only to preserve owner-only and fell
   away with it.  `moveObject` (crosses documents) stays undeclared.
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
out, the rest lands where estimated.
**(f) the Restore route BUILT 2026-09-04.**  With routing on
(`proxyRestoreRouted()`, the preference alone), `PropertyPythonObject::
Restore` on a document object sends `proxy_new alloc` -- the guest
imports the saved module name and allocates `cls.__new__(cls)`, no
`__init__`, what `PyType_GenericAlloc` is natively -- and holds the
stand-in, so `loads`, `onDocumentRestored` and every later hook cross
as they did for a Proxy constructed in the guest; the guest failing to
serve the module (import error, no such class, image unavailable)
leaves the object WITHOUT a Proxy and says so on the console -- never a
native import of a document-chosen name.  Off, the native path is
untouched.  Gates: `guestProxyRestoreRoute` (both runtimes) -- a probe
class present on both sides saved from a guest Proxy, reopened routed
= stand-in, the pre-save log back through `loads`, `execute` crossing
again; a second object whose Proxy names a host-only module reopens
routed with NO Proxy and unrouted with the native instance;
`draftWireRestoreInGuest` (pyodide) -- the G1c Wire saved, reopened
routed = stand-in with equal facts, recomputed BRep byte-identical to
the pre-save shape, reopened unrouted = a native `draftobjects.wire.
Wire` with the same shape.  Not routed: a view provider's Proxy (its
container is no document object; G2).
Still open from the sizing: the two decisions above (ruled, see the
list); `obj.Proxy.<attr>` reads from HOST
Python (Draft's `get_type` reads `Proxy.Type`) have no forwarder yet --
a stand-in `__getattr__` over a `proxy_get` op when G1d needs it; a
guest reset orphans live stand-ins (their hooks raise
`ReferenceError`, never a silent new instance).

**G1d SIZED AND OPENED 2026-09-04: the Draft test document.**  The
harness is `scripts/sandbox-reopen-routed.py` (run under the test rig,
docs/Testing.md sec 1): a document reopened NATIVELY, every object
touched, recomputed and snapshotted -- the honest baseline, since a
re-execute drifts on its own (Draft's Fillet does, natively) -- then
reopened with routing ON through the Restore route, touched,
recomputed, and one row per object: how the Proxy came back (guest
stand-in / host instance / none), shape hash equal or the max vertex
delta, recompute state.  With no argument it builds the Draft test
document, `drafttests.draft_test_objects` -- 111 objects, 70 of them
scripted (every Draft App-side class: lines, wires, fillets, arcs,
circles, ellipses, rectangles, polygons, splines, beziers, points,
facebinders, texts, dimensions, labels, ortho/polar/circular/path/
point arrays as objects and as Links, clones, Shape2DView, working
plane proxies, layers).  First run: all 70 Proxies came back as
stand-ins, none imported on the host, 62 shapes byte-identical, 4
not: Fillet (native drift, gone against the honest baseline),
Shape2DView (`import TechDraw` in the guest: no facade), WPProxy
(`p.Placement = obj.Placement` on the plane it made: the write gate
refused a write on a non-owner handle), Polygon; and the four Link
arrays logged `AttributeError: configLinkProperty` from
`DraftLink.onDocumentRestored` (the Link extension had no facade).
Built the same day: a `TechDraw` module facade (`projectEx`,
`project`, `projectToSVG`, `projectToDXF`, `makeGeomHatch`, under
`geom.call`); `write_prop` on a value handle -- a handle that is not
a property container (a shape the guest built, or a property's copy)
takes a write to a DECLARED attribute of its type through the type's
own setter under `geom.call`, the owner-only gate applying to document
objects alone (3.2); `LinkBaseExtensionPy.xml` annotated
(`configLinkProperty`, `getLinkExtProperty`, `getLinkExtPropertyName`,
`getLinkPropertyInfo`, `setLink`), `configLinkProperty` and `setLink`
in the write family.  Gate `draftTestObjectsReopenRouted` (pyodide):
111 objects, 70 stand-ins, 0 host imports, 0 unserved, 0 invalid, one
shape not byte-identical -- Polygon.  CAUSE FOUND 2026-09-04, by
comparing the bits of `cos`/`sin` of `k * 2pi/5` on the host and in
the guest (python-mode `import math` evaluated through `evaluate()`):
seven of the eight values agree to the bit, `cos(3 * 2pi/5)` does not
-- glibc returns the correctly rounded `-0x1.9e3779b97f4a9p-1`, the
guest's wasm libm (emscripten's musl, FreeBSD msun `cos`, under 1 ULP
but not correctly rounded) returns `...4a8p-1`, and the true value sits
0.03 ULP from the rounding midpoint, a hard case.  One vertex
coordinate of the pentagon is thus one ULP off (2.8e-14 at radius
250), and the line directions and plane origin BRepLib derives from
the points carry it.  Not fixable from the wheel: the guest's `math`
is the runtime's CPython against the runtime's libm; a correctly
rounded `cos` (CORE-MATH) would mean rebuilding pyodide's core, and
routing trig to the host would cost a hop per call.  The user ruled
identity is not required but 1e-9 is far too loose, so the gate now
measures the difference in ULPs of the object's largest coordinate
(`math.ulp(scale)`) and allows at most 4 -- the trig's one ULP plus a
product's own rounding; the harness prints the same number.  The
reference Polygon reads 1.0 ULP.  Traffic for the whole
document's routed recompute: 967 proxy calls, ~2500 hops (`read_prop`
1361, `call` 387, `get_attr` 316, `write_prop` 195, `mod_call` 190,
`bool` 44), 702 handles -- about 1.4 ms of crossing per scripted
object at 8.1's hop cost.
Still open for G1d: (1) BIM -- nothing of Arch is in the guest, so a
BIM document reopens routed with every Proxy failing closed (the
`bimtests/fixtures` file, one `ArchSite._Site`, does exactly that);
needs an `fcx_bim` wheel of BIM's App side plus a corpus the TestArch
suite does not leave behind (its tests delete their objects), then
the list already known: `ArchStairs`/`ArchReference` document-level
writes, 193 `Proxy.<attr>` host reads; (2) the Polygon ULP question
-- RESOLVED above (last-bit libm drift, gated at 4 ULP of the
coordinate); (3) the stand-in `__getattr__` for host reads of
`Proxy.Type` -- BUILT below; (4) the construction dispatch -- SIZED
AND BUILT below.

**G1d, the construction dispatch, SIZED 2026-09-04.**  The fact: a
Draft object MADE in a routed session got a host Proxy, because
`make_polygon` runs on the host and `Polygon(obj)` there constructs
the host class; only reopened documents routed.  Two designs were
sized.  (B) The make layer in the guest -- the host's `Draft` module a
facade, `make_*` running in the guest under the CALLER's principal:
needs document-level writes for that principal (`addObject`, ruled
undeclared for document principals; a user principal is a different
ruling), `App.ActiveDocument` in the guest, handle-returning
`addObject`, and the GuiUp tail of every `make_*` (view provider,
`format_object`, `select`) split off to run on the host after the
guest returns -- a rewrite of the make layer that belongs with G2's
view providers, not before it.  (A) The class boundary -- every
scripted object class's `__new__` asks the host whether the session
routes, and if so constructs the class IN THE GUEST through
`proxy_new` (`cls(*args, **kwargs)` there; its `obj.Proxy = self`
installs the stand-in) and returns the stand-in, which is not an
instance of the class, so Python skips the host `__init__`; the whole
`__init__` runs in the guest, `make_*` is untouched and still calls
`Polygon(obj)`.  The Draft App side has FIVE root classes
(`DraftObject`, `DraftAnnotation`, `Layer`, `LayerContainer`,
`WorkingPlaneProxy`); BIM adds `ArchIFC.IfcRoot` and a few plain
classes when its wheel comes.  A generic hook without touching the
roots does not exist: migrating the host instance at `obj.Proxy =
self` (allocate in the guest, `loads(dumps())`) loses every attribute
`__init__` sets AFTER that line, which is all of them (`self.Type =
tp` follows it in `DraftObject`).  Cost per construction: one
`proxy_new` plus the `__init__`'s own hops (addProperty writes), the
same traffic G1c measured for a Wire.  (A) is built; (B) remains the
route to "one switch" and supersedes (A) when the make layer moves.

**BUILT the same day** (commit after `7b9b4bf224`):
`FreeCAD.ExpressionSandbox.proxyConstruct(cls, *args, **kwargs)`
(`constructGuestProxy` in `ExpressionGuestProxy.cpp`): None when the
session does not route (the Evaluate preference alone, like the
Restore route) or the call is a bare `cls.__new__(cls)` (copy, pickle,
a native alloc) -- the class allocates natively; else the stand-in
(a document object first argument is the owner, `Array(None)` has
none: Draft's link arrays are installed by `addObject(..., attach=
True)`, whose `attach` call reaches the guest as a method read off
the stand-in; the guest registers the instance whether or not
`__init__` installed it), or an error when the guest cannot serve the
module or the class raised -- fail closed, as Restore does, never a
native retry (running `__init__` a second time would repeat its side
effects).  An extension added during `__init__` (WorkingPlaneProxy
adds `Part::AttachExtensionPython` and calls `changeAttacherType` in
the same breath) recomposes the guest proxy's class: `addExtension`
is followed by an `ext` op that re-reads the object's extension
facades.  `addExtension` and `changeAttacherType` joined the write
family (neither was declared before; nothing in a recompute reaches
them, construction does).
`draftobjects.base.new_proxy(cls, *args, **kwargs)` wraps it for the
five roots' `__new__`; the same source runs in the guest, where
`FreeCAD` has no `ExpressionSandbox` and the class allocates natively.
Host reads of Proxy attributes ride the stand-in's `getattr`/`setattr`
(3.2: `proxy_get`/`proxy_set`, methods as forwarders, no trip for a
dunder or an unlisted hook name); Draft reads `Proxy.Type` in 7 App-
side sites and calls `Proxy.getMovableChildren`/`.transform`/`.execute`
in a handful; Draft writes none, BIM writes 22 (`ifcfile`,
`svgcache`, ...).  Gates: `guestProxyAttrsAndConstruct` (both
runtimes: construction from a host class's `__new__`, reads by value,
class attribute, AttributeError, a method forwarder with the object
as owner, a write, a refused handle store, then a recompute through
the guest Proxy; 9 proxy calls), and `draftTestObjectsBuiltRouted`
(pyodide): the Draft test document BUILT with routing on -- every
`make_*` on the host, every Proxy constructed in the guest -- against
the native build, the same counts and the ULP bound as the reopen
gate.

**G1d, BIM, BUILT 2026-09-04 (commit after `72fb1159bc`).**  The
`fcx_bim` wheel (5.6) and the corpus `bimtests.bim_test_objects` (one
of every Arch class made headless: 68 objects, 59 scripted, all valid
natively; Reference alone skipped, it needs an external file), with
the same two gates as Draft's, parameterised (`CorpusGate` in the
test file): `bimTestObjectsReopenRouted` and `bimTestObjectsBuiltRouted`,
REPORTING rather than strict while the list below stands.  What the
gates found, in the order the errors surfaced, each fixed the same
day: `Draft.py` and the two view provider modules it imports were not
in the Draft wheel; `FreeCAD.Qt`, `ParamGet` (with `Get*s`),
`getResourceDir` (+ the presets as data under `fcx_resources`),
`addDocumentObserver`, `ActiveDocument` and the `Units.Length` unit
types missing from the guest module (5.6); undeclared members BIM's
`execute()` paths reach -- `State`, `InListRecursive`,
`OutListRecursive`, `Document.Restoring/Recomputing/Transacting/
getProgramVersion`, `getPropertyByName`, `getPropertyStatus`,
`getDocumentationOfProperty`, `getEnumerationsOfProperty`, TopoShape
`extrude/revolve/makeWires/reversed/isSame/isCoplanar/tessellate/
cleaned/isInside/importBrepFromString/dumps/writeInventor`,
`touch/purgeTouched/renameProperty/setExpression` (write family),
`isAttachedToDocument/getParent`, Sheet `get/getUsedRange/row/column/
getColumnWidth`, `Part.getSortedClusters/makeBox`, `Part.Precision`
(OCCT's compile-time tolerances, a guest class); nine plain Proxy
classes without a root (`_ArchMaterial`, `_ArchMaterialContainer`,
`_ArchMultiMaterial`, `_Axis`, `_AxisSystem`, `ArchGrid`,
`_SectionPlane`, `_ArchSchedule`, `_ArchReport`) given the `__new__`
hook beside `ArchIFC.IfcRoot`'s; and ONE real divergence: ArchFrame's
`profile.Placement.Rotation = rot` -- native FreeCAD writes a value an
attribute handed out back into its parent on a nested write
(`PyObjectBase::setAttributeOf` + `startNotify`), the guest's value
was detached, so the frame's profiles never turned (a 2000 mm
difference).  Now every value a handle proxy hands out is stamped as
that attribute of the proxy (`PyObjectBase::trackAttributeOf`, public
and static, from the prelude's `_fcx.track`), and the write-back is
the proxy's `__setattr__` = `write_prop`, on a value handle through
the type's own setter (3.2).  Result: reopen routed 59/59 Proxies in
the guest, invalid only Stairs, Schedule, Report; built routed 59/59,
plus the pipe Connector -- all four are writes to objects other than
the owner, at the time undeclared (13).  Traffic for the reopen: ~1100
proxy calls, ~9000 hops for 59 objects.  **2026-09-05: the four are
same-document writes, allowed by the revised decision 1 above; see
"G1d CLOSED" at the end of this section for the gates after it.**

**G1d CLOSED 2026-09-05: the BIM corpus gates are STRICT.**  Three
changes: (1) the write gate is same-document (3.2) and
`Document.addObject`/`removeObject` plus the Sheet's cell writes are
declared -- Stairs, PipeConnector and Report recompute routed at once;
(2) TechDraw's `findShapeOutline`/`makeGeomHatch` take
`allowCrazyEdge=True` as a keyword (a scoped `DrawUtil::
CrazyEdgeAllowance` the projector's `isCrazy` consults), and Arch's
area/perimeter computation, ArchTessellation and Draft's Hatch pass it
instead of writing the `Mod/TechDraw/debug` preference around the
call -- natively too, so a recompute never writes the parameter store
(`project`/`projectEx` never consult `isCrazy`; no keyword there);
(3) Schedule's last failure was not a write at all: its execute ends
in `save_ifc_props` -> `nativeifc.ifc_psets.edit_pset`, which natively
looks the object's IFC file up and returns when there is none, and
nothing of `nativeifc` (ifcopenshell) is in the guest -- the fcx_bim
wheel now carries a `nativeifc` shim (shims/README.md) that answers
that path the native way for a plain document and raises
`IfcUnavailableError` for an IFC-backed object.  Found on the way:
a guest exception reached the host as its last line only, so the
reply now carries the formatted traceback (`tb`, ImageResult::
traceback) and a failed hook raises message + traceback, as native
FreeCAD prints for a failed execute(); and `pkg.missing` is counted
per module name in the stats.  Gates: reopen routed 68 objects, 59
guest Proxies, 0 host, 0 invalid, differ Pipe001 at 0.25 ULP of its
largest coordinate (4.5e-13); built routed the same.  Traffic for the
reopen: 1218 proxy calls, ~11000 hops.
Found by recomputing the routed corpus a SECOND time, and CLOSED
2026-09-05: ArchReport caches its Result sheet on the Proxy
(`self.spreadsheet = o`) and the next execute reads it -- a HANDLE from
the previous transaction, gone with it -> `ReferenceError: stale host
handle` (ArchSchedule's same cache sits under a bare `except` and
silently returned None instead).  Caching a document object on a Proxy
across hooks is a native pattern; the fix is durable document-object
handles (3.2): a `[document, name]` key on every DocumentObject and
Document handle, re-resolved by the guest on a stale id through one
`resolve` op (owner's document only), `__eq__`/`__hash__` by key, and
a stale handle used as an argument re-resolved while decoding.  The
corpus gates now recompute twice (the harness too) and stay green:
Draft and BIM, reopened and built.

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
8. The toolkit transition goes through the widget layer, two
   backends: Qt on the desktop, the DOM in the browser (7.4).
9. The host half of the widget layer is built, in C++, and native
   task panels port onto it; no imgui on either tier (7.12).

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

### 7.9 G2 sized: Draft and BIM register from the guest **[sized 2026-09-05]**

G2 is U1 + U2 + U7 (7.1): the workbenches' `InitGui.py` runs in the
guest, their commands and workbenches exist on the host as stand-ins,
and the host services they reach at registration are ops.  The host
facts that size it (`src/Gui`, 2026-09-05):

- **A Python command** is `Gui::PythonCommand` (or `PythonGroupCommand`
  when the object has `GetCommands`) holding the object.  `GetResources()`
  is read ONCE at construction and again on a language change (a dict
  of strings and bools: MenuText, ToolTip, StatusTip, Pixmap, Accel,
  WhatsThis, CmdType, Checkable, Exclusive, DropDownMenu); `IsActive()`
  is polled on every UI update tick and must be a real bool (anything
  else, or an exception, reads as inactive); `Activated()` (or
  `Activated(index)` for a checkable one) on trigger; `GetCommands()`
  names, `GetDefaultCommand()` an index, `OnActionInit()` once,
  `CmdHelpURL()` on help.  Only `IsActive` is hot.  The command's group
  name is derived from the CALLER's file path (`Mod/<Group>/...`), and
  the optional "activation string" makes `Activated` a host-side command
  line that never calls Python.
- **A workbench handler** crosses strings only: `GetClassName()`,
  `Initialize()`, `Activated()`, `Deactivated()`, `ContextMenu(recipient)`;
  `MenuText`/`ToolTip`/`Icon` are read at registration; the host WRITES
  `__Workbench__` (the C++ workbench's own Python object) onto the
  handler after `GetClassName`, and the handler's `appendToolbar`,
  `appendMenu`, `appendContextMenu`, `appendCommandbar`, the removes and
  the lists all go through it.  `addWorkbench` accepts an instance or a
  subclass of `__main__.Workbench`, instantiates a class itself, and keys
  the registry by the CLASS NAME.
- **`InitGui.py`** is not imported: `FreeCADGuiInit.RunInitGuiPy` execs
  the file's text in its own scope, with `FreeCAD`, `App`, `Gui`,
  `FreeCADGui`, `Log`, `Err`, `Msg`, `Workbench` as globals, and a
  failure is logged, never fatal.
- **Resources.** `addIconPath(":/icons")` works because `import Draft_rc`
  just registered the compiled Qt resources in the HOST's resource
  system; a guest cannot register a qrc.  `addPreferencePage` has a
  `.ui`-path form (data: the host's `PrefPageUiProducer`) and a class
  form (the host instantiates a Python class -- U3, G3).
- **View providers** are NOT G2.  `ViewProviderFeaturePythonImp` binds 46
  hooks; `getIcon`, `getExtraIcons`, `setupContextMenu`, `iconMouseEvent`
  take or return Qt objects, `getElement`, `getElementPicked`,
  `getDetail`, `getDetailPath` take or return Coin objects, and `attach`
  is where a proxy builds Coin nodes (`vobj.addDisplayMode(node, mode)`).
  That is the mirror (G4, after Probe A); a view provider's Proxy stays
  native until then (13).
- **No C++ test constructs a `Gui::Application`**; the Gui-side gates are
  Python test modules run under the Gui binary (`FreeCAD -t <Module>`,
  the way `SandboxPyodide` already runs there), on Xvfb with the rig of
  docs/Testing.md (`env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb
  xvfb-run`; `offscreen` crashes in `QOpenGLWidget`).

**What Draft's `Initialize()` needs before it appends one toolbar:**
`import DraftTools`, which imports `DraftGui` (a `DraftToolBar()` of Qt
widgets is BUILT at import), `gui_snapper` (a `Snapper()` at import) and
`gui_trackers` (pivy at import) -- U3, U5 and U6 in the first three
lines.  BIM's `createTools` does the same through `DraftTools` and
`bimcommands`.  So G2 cannot gate on Draft's toolbars appearing: the
registration MECHANISM is G2a with its own gate, and G2b runs the real
`InitGui.py`s in the guest to measure how far they get -- that residue
is G3's list, not a G2 failure.

**G2a, the mechanism (build order):**

1. A guest `FreeCADGui` module in the prelude beside `FreeCAD`:
   `Workbench` base class (its `append*`/`remove*`/`list*` are one op
   `gui.wb {name, m, a}` on the workbench's REGISTERED name -- the guest
   never holds `__Workbench__`), `addCommand(name, obj, activation=None)`
   (the object registers as a guest proxy with the COMMAND hook list and
   crosses as its descriptor: `gui.cmd.add {name, desc, group,
   activation}`), `addWorkbench(cls | inst)` (instantiated in the guest,
   registered with the WORKBENCH hook list, MenuText/ToolTip/Icon read
   there: `gui.wb.add {name, desc, MenuText, ToolTip, Icon}`),
   `addIconPath`, `addLanguagePath`, `addPreferencePage(ui, group)` (the
   `.ui` form; a class is a TypeError until G3), `listCommands`,
   `listWorkbenches`, `getWorkbench` (the guest instance), `activeWorkbench`,
   `updateLocale` (no-op).  `FreeCAD.GuiUp` stays 0: the App side's
   `if App.GuiUp:` imports keep their meaning, there is no GUI in the
   guest.
2. A bridge op REGISTRY on the host (`registerBridgeOps(prefix,
   handler)` in `ExpressionImageBridge.h`, consulted by `dispatchHostOp`
   before "unknown bridge op"), so `Gui` owns every `gui.*` op without
   `App` linking `Gui`; `src/Gui/SandboxGui.cpp` registers them when
   `Gui::Application` is constructed.  Every `gui.*` op is
   `checkPermission(Permission::Gui)`: the catalog's DENY for a document
   (not promptable), ALLOW for session and addons.
3. The command object on the host IS the stand-in (`makeGuestProxy` of
   the descriptor): `new PythonCommand(name, standin)` or
   `PythonGroupCommand` when the descriptor lists `GetCommands`, the
   group name from the op.  The host's stand-in answers a missing
   command or workbench hook without a trip (`isHookName` learns the GUI
   names; the guest's `_proxy_register` takes the hook list), so the
   `hasattr("IsActive")` of every poll costs nothing.
4. The workbench on the host is a WRAPPER, `class GuestWorkbench(
   Workbench)` holding the stand-in and forwarding the five hooks: the
   host writes `__Workbench__` onto the handler, and a stand-in refuses
   host objects (3.2), so the wrapper takes it; `gui.wb` calls the
   wrapper's base-class method `m` (an allowlist: the fourteen
   `Workbench` methods) on the registered name.
5. Principal: registration and every forwarded hook run as `session`
   (the proxy call has no owner) -- the built-in workbenches.  An addon's
   principal (`addon:<name>`) is G2b's: the InitGui runner pushes the
   scope for the exec, and the stand-in then has to carry it for the
   hooks the host calls later.
6. `FreeCAD.ExpressionSandbox.exec(source, module)` on the host (the
   `exec` op the gtests already use), so a Python test can push a probe
   module.

**Gate G2a** (`SandboxGui`, a Test-workbench module under the Gui
binary): a probe module exec'd in the guest registers a workbench, a
plain command, a checkable command and a group; the host sees them in
`listCommands()`/`listWorkbenches()`; `Command.get(name).getInfo()`
equals the guest's `GetResources`; `IsActive` follows a guest flag;
`runCommand` crosses `Activated` (a guest counter); `activateWorkbench`
crosses `Initialize`, and the guest's `appendToolbar`/`appendMenu`
produce host toolbars (`listToolbars`, `getToolbarItems`); switching
away crosses `Deactivated`; a DOCUMENT principal calling
`FreeCADGui.addIconPath` is refused with `PermissionError`; with
routing off nothing of the above exists.

**G2a BUILT 2026-09-05** as sized, with three facts the build added:
the registry's request and reply cross App/Gui as CBOR bytes (a json
in a signature does not link between the two nlohmann copies, 12); the
registry is consulted BEFORE the handle ops resolve `h` (a `gui.*` op
carries no handle, and the dispatcher's stale-handle check came first);
and the host macro is scoped to `SandboxGui.cpp` alone
(`set_source_files_properties`), since a Gui-wide define means a full
Gui rebuild.  Gate `SandboxGui` (2 cases) green on the first complete
run: three commands and the workbench registered from the guest, the
resource dict read through the stand-in, `IsActive` following a guest
flag, `Activated` crossing for the plain and the checkable command,
`activateWorkbench` crossing `Initialize` (the toolbar and menu appended
from the guest exist on the host, and the guest's own `listToolbars`/
`getToolbarItems` read them back equal), `Activated` seeing itself as
the active workbench, `Deactivated` on switching back, a second
registration under the same names replacing the first, and a document
principal refused (`Permission denied: gui`).  A document-principal
`evaluate` that touched `FreeCADGui.activeWorkbench()` was refused too
-- the test had to read that fact from the guest's `Activated` hook,
which runs as session.  Suites after: pyodide 97/97, wasi 86 + 11
skipped.

**G2b MEASURED 2026-09-05, before the runner exists:** the real
`InitGui.py` of Draft and of BIM, exec'd unmodified in the guest with
the runner's globals (`FreeCAD`, `App`, `Gui`, `FreeCADGui`, `Workbench`,
`Log`, `Err`, `Msg`, an empty `FreeCAD.__unit_test__`), the native
workbench removed first:

    module level   Draft: OK -- 1 gui.wb.add, 4 gui.pref_page (the .ui
                   resources exist on the host because the native
                   Draft_rc had been imported at startup: the runner
                   needs gui.rc).  BIM: OK -- 1 gui.wb.add, 5
                   gui.pref_page, 4 mod_call (prefs).
    activation     Draft: the host activates it; Initialize() runs in
                   the guest and RETURNS AT ITS FIRST LINE -- `from pivy
                   import coin` fails, "Pivy not found, Draft Workbench
                   will be disabled" -- so no icon path, no language
                   path, no toolbar; Activated() and Deactivated() cross
                   and run (the WorkingPlane/grid_observer view
                   observers are GUI-only, warned about).  BIM:
                   Initialize() crosses gui.icon_path and gui.lang_path,
                   then `import DraftTools` -- not in any wheel --
                   ModuleNotFoundError.

So the first thing either `Initialize()` needs is not a form: it is
pivy (Draft's self-test) and `DraftTools` (which imports `DraftGui`'s
widgets and `gui_snapper` at module level).  That is Probe A and G3
in that order; the runner itself is a small switch and waits for
something to switch to.  A host `activateWorkbench` whose `Initialize`
raises ends in a modal "Workbench failure" box (`Application.cpp`
~1850) -- under Xvfb that is a hang, so a probe calls the guest's
`Initialize` through `exec` instead.

**G2b, the runner:** a module whose GUI side is bundled as a
wheel runs its `InitGui.py` in the guest instead of natively (the
`Evaluate` preference plus the wheel's presence; native otherwise), with
`gui.rc {module}` (the host imports `<Mod root>/<name>_rc.py`, compiled
Qt resources, the one host import G2 makes, data not code) and the
exec's principal.  A guest reset (a package install) kills every
registered stand-in (`ReferenceError` on the next hook), so the runner
re-runs the guest InitGui's after a reset, replacing the commands.
With the runner on, FreeCAD's start boots the guest (about 1.8 s).
Cost of the mechanism: one hop per `IsActive` poll per guest command
(some 60 visible commands times 5 us per tick), one hop per activation
plus whatever the command does -- U4 ops, G3 and later.

**G2b BUILT 2026-09-06**, as sized with four departures:

- **The switch is its own preference**, `Expression/Sandbox:
  InitGuiInGuest` (default OFF; the rig's `FCX_INITGUI_IN_GUEST=1|0`
  overrides it), not `Evaluate`: with the residue below, `Evaluate`
  alone would have taken Draft's status bar and BIM's tool bars away
  from everyone routing expressions.  It folds into `Evaluate` when
  the residue is gone.  The decision (`GuestInitGuiWanted` in
  `FreeCADGuiInit.py`): the switch, a build with the host, and a
  bundled wheel named `fcx_<module>` (`pyodideLayout()['bundled']`,
  the wheels by distribution name).  `RunInitGuiPy` defers to
  `RunInitGuiInGuest` for such a module; every other module's
  InitGui.py runs natively as before.
- **No `gui.rc` op.**  The runner imports the module's `<Mod>/*_rc.py`
  on the host itself before the exec (Draft's is `Draft_rc`, BIM's is
  `Arch_rc`: the name is the module's to know, not the guest's), and
  the guest's `Draft_rc`/`Arch_rc` shims stay empty.
- **The guest half is in the fcx_widgets wheel**, not the prelude:
  `FreeCADGui._run_initgui(source, path, module, tops)` execs the
  file's text with the native runner's globals (`FreeCAD`, `App`,
  `Gui`, `FreeCADGui`, `Workbench`, `Log`, `Err`, `Msg`) and derives a
  command's GROUP from the wheel its class came from (`tops`, the
  wheel's top-level names, read from the zip on the host): the
  host's own rule is the caller's `Mod/<Group>/` path, and a wheel
  module has no such path.
- **A guest hook's failure is an error in the report view**, not the
  modal "Workbench failure" box a native handler's raise ends in: the
  wrapper's `Initialize`/`Activated`/`Deactivated` catch and print the
  guest's traceback, and the workbench comes up with what it
  registered before the raise -- the InitGui.py failure rule ("logged,
  never fatal") extended to the hooks, because a guest workbench is
  partial by design until G3d and G4, and a modal at startup under
  Xvfb is a hang.

**The reset, built:** `ImageHost::bootCount()` counts guests;
`addBootListener` fires after the outermost call that booted one
returns (never inside it), and `SandboxGui.cpp` queues
`FreeCADGui._onGuestBoot(n)` on the event loop, which re-runs every
InitGui.py stamped with an earlier boot and re-activates the active
workbench if it was a guest one.  Two host bugs surfaced on the way,
both fixed: a stand-in is now stamped with its guest (`(boot, id)` is
the identity of a guest proxy: a fresh guest numbers from 1 again, and
the old guest's stand-in of the same number was answering for -- and
on its death dropping -- the new guest's proxy; a stale stand-in now
raises `ReferenceError: guest proxy N belongs to a sandbox guest that
was reset`, and `teardown()` discards the drops queued for the dead
guest), and `Application::initializeWorkbench`'s once-only guard kept
a removed workbench's NAME, so a handler registered again under it
never ran `Initialize()` -- it runs now whenever no C++ workbench of
that name exists yet.  After a reset one UI tick of `IsActive` polls
reaches the stale stand-ins (two `ReferenceError` lines in the report
view) before the re-run replaces them.

**The measurement -- how far the two `Initialize()`s get now**, with
the wheels grown for it (Draft: `DraftTools`, every `draftguitools`
module, the task panels, `init_draft_statusbar`; BIM: `bimcommands`,
`BimStatus`, a `PartGui` shim, `nativeifc.ifc_commands` answering an
empty tool bar and `ifc_observer` no-ops -- no ifcopenshell in the
guest) and the small gaps filled on the way (`FreeCADGui.UserInput` as
the host's IntEnum mirrored by value on first use, `InputHint` and
`HintManager` crossing as data to the main window's `showHint`,
`getMainWindow().getWindowsOfType()` answering none, `Base.TypeId` as
a name, `draftutils.params._param_observer_start` a declared STUB in
the facade -- the native function is `if App.GuiUp:`, 0 in the guest):

    Draft   InitGui module level in the guest at startup; Initialize()
            on activation: pivy's self-test, DraftTools imported (82
            Draft_* commands registered from the guest, Draft_Hatch
            excepted: its registration sits under `if FreeCAD.GuiUp`),
            DraftGui's tray built, the five tool bars and the menus
            appended and filled on the host.  Activated(): the snapper
            shows, then `init_draft_statusbar` stops at the STATUS BAR
            (`statusBar().findChild`, `addStatusBarItem`: widgets in
            the status bar are not in the widget layer yet); the
            WorkingPlane/grid_observer view observers warn (G4).
            IsActive(): False for every Creator -- `get_3d_view()`
            needs a 3D view (G4); the poll crosses (one proxy call).
    BIM     InitGui module level OK; Initialize(): icon and language
            paths cross, `bimcommands` registers 32 commands and stops
            at `BimCovering` -- the one module of 83 that imports
            FreeCADGui under `if FreeCAD.GuiUp` and registers its
            command outside it -- so createTools never reaches its
            tool bars; Activated()/Deactivated() fail on the missing
            `draftingtools` the same way.

So the wall both stopped at was **the `GuiUp` ruling** (G2a: "FreeCAD
.GuiUp stays 0"), made when the guest had no GUI at all.  With the
GUI side in the guest, two registrations hid behind it (Draft_Hatch,
BimCovering -- the latter taking all of BIM's tool bars with it), and
the App side's `if App.GuiUp:` branches (view providers, Qt imports,
selection) are what the ruling protected for the document guests that
share the instance.  Gate `SandboxInitGui` (4 cases: the decision,
Draft and BIM from the guest, the reset re-run) needs a session whose
startup ran the runner, so it runs in a process of its own with
`FCX_INITGUI_IN_GUEST=1` and skips in the default gate list.

**GuiUp FLIPPED 2026-09-06, user ruling ("flip it").**  The guest's
`FreeCAD.GuiUp` is the host's: `ImageHost` reads the host module's
value after a boot and sends `FreeCAD.GuiUp = 1` as the fresh guest's
first request (a headless host -- FreeCADCmd, the test binary --
leaves the guest's 0, so the `*BuiltRouted` gtests measure what they
always did).  What the flip needed, built the same day:

- **The wheels**: every Draft view provider (the objects' `if
  App.GuiUp:` imports reach all of them), BIM's `BimStatus`; shims for
  `QDesktopServices`, `QFileSystemModel` (a base class BIM's library
  browser derives from at import), `QtCore.Slot`.
- **An object's own view provider is in its write scope.**  Draft's
  Dimension `execute()` calls `obj.ViewObject.update()` under GuiUp;
  BIM's Stairs hides its base's view.  `Gui.ViewProviderPy.xml` and
  `ViewProviderDocumentObjectPy.xml` joined the annotated XMLs
  (`show`, `hide`, `isVisible`, `signalChangeIcon`, `update` as call
  tier, `Object` as a handle), the four are OpCall's "view family"
  under the same-document write gate, and `documentOf` resolves a
  view provider handle (any type whose MRO carries
  `Gui.ViewProviderDocumentObject`) to its Object's document, through
  Python -- App links no Gui type.  Property writes on a view
  provider ride the same gate.
- **`gui.user_input` is data** any principal may read: Draft's tool
  modules read the enum at import, and a document guest imports them
  under GuiUp now.
- **A registration a document's import triggers is deferred**, not
  refused: `import Draft` reaches `gui_hatch`, whose module registers
  `Draft_Hatch`; the prelude's `addCommand` queues what the host
  refuses for the principal and flushes the queue with the next
  registration the session makes (Draft's Initialize).  A registration
  only names wheel code.  Widget comm traffic is NOT deferred (tried
  and reverted the same day: a document's models would reach the host
  with the session's next traffic, and the widget gates' "a document
  principal is refused" is the contract) -- so a document guest cannot
  import `DraftTools` (DraftGui builds its tray at import), which is
  BIM's Rebar below.
- **The main window shim** answers `getActiveWindow()` None,
  `getWindows()` empty, `findChild(QMdiArea)` a stub whose
  `subWindowActivated` never fires (the view observers connect to it),
  `getIcon` as data.
- **Two host bugs the flip exposed, fixed.**  `ActionGroup::actions()`
  cached raw `QAction*` past their deletion (a group's action belongs
  to another command; the re-run after a reset replaced 82 of them
  while Draft's groups held theirs, and `Command::testActive` read a
  dead one -- SIGSEGV); every action the group lists is now tracked to
  its destruction.  And the corpus gate's process hung in `exit()`:
  `pthread_cond_destroy` on `MeshLevelSource.cpp`'s static condition
  variable with its refine threads still waiting -- a renderer bug,
  fixed 2026-09-07 at the source (sec 12): the level workers are
  joinable and `PartGui::shutdownMeshLevelWorkers()` stops and joins
  them on the application's `aboutToQuit`; the rig's `os._exit`
  workaround is gone and the gate's process exits normally.

**Measured with the flip** (gate `SandboxCorpusGui`, the G1 corpus
built under the GUI in a routed session and compared with a native
build, object by object -- the GUI twin of the `*BuiltRouted` gtests):

    Draft   111 objects, 70 guest Proxies, 0 invalid, every shape
            equal (Polygon 0 ULP); the view providers native (the
            corpus makes its objects on the host).  STRICT.
            Layer's onChanged calls its view provider's Proxy method
            (`change_view_properties`): an error line, not a failure.
    BIM     68 objects, 57 guest Proxies.  Invalid: the 3 BuildingParts
            (execute calls `obj.ViewObject.Proxy.onChanged`, a host
            Python object's method -- undeclared, G4).  Proxy stayed
            native/None: Rebar (its module imports `bimcommands` under
            GuiUp, which imports DraftTools, which builds Draft's tray
            -- refused for a document), ArchReport (natively broken in
            this fork's GUI: `ToggleVisibility`; in the guest
            `QSyntaxHighlighter` next).  ReportResult missing for the
            same reason; Pipe001 a quarter ULP.  NOT strict: the list
            above is the record, `guest >= 50` and no native Proxy are
            asserted.
    InitGui Draft as before; BIM's `Initialize()` now completes -- its
            tool bars on the host from the guest; `Activated()` stops
            at `QDockWidget` (BimViews) and, for Draft, the status bar.

Suites after the flip: `SandboxCorpusGui` 2/2, `SandboxInitGui` 4/4,
the six other GUI gate modules 13/13, `Tests_run
--gtest_filter='Expression*'` 104 passed + 1 skipped.

### 7.10 Probe A sized: Coin and pivy in the guest **[sized 2026-09-05]**

What 7.9's measurement made the next blocker: Draft's `Initialize()`
opens with `from pivy import coin`.  The facts (the Coin fork on
`LinkVibe`, `~/works/sw/pivy` on `rt-0.6.10`, the guest toolchain):

- **Coin's hard build dependency is Boost headers only** (`scoped_ptr`,
  `intrusive_ptr`, `lexical_cast` ...).  expat is vendored
  (`src/xml/expat`); zlib, bzip2, freetype, fontconfig, simage, GLU,
  OpenAL are `*_RUNTIME_LINKING=ON` by default, never found at
  configure time and `dlopen`ed through `src/glue/dl.cpp` -- under wasm
  those opens return NULL and each feature turns itself off (fonts:
  Coin's built-in bitmap font remains).  `src/threads` compiles
  unconditionally against emscripten's pthread headers (no `-pthread`:
  pyodide is single-threaded); `COIN_THREADSAFE` is OFF already.
- **No no-GL path exists**: `find_package(OpenGL REQUIRED)` is
  unconditional, `find_package(X11 REQUIRED)` fires because `UNIX` is
  true under emcmake (behind `COIN_BUILD_GLX`, OFF-able, with
  `COIN_BUILD_EGL`).  Zero `EMSCRIPTEN` mentions.  Six files include a
  real GL header; 168 include `Inventor/system/gl.h`, a configure-time
  substitution; extension entry points (263 of them) go through the
  glue and resolve at runtime via `cc_glglue_getprocaddress` (NULL =
  absent, fine); but 73 files under `elements/GL`, `nodes`,
  `shapenodes`, `rendering`, `shaders` call core desktop `gl*()`
  directly.  emscripten's `GL/gl.h` declares them all, so it COMPILES;
  nothing implements them in a side module, so it fails at LOAD.  The
  fix is a generated stub translation unit (every core `gl*` symbol as
  an empty function, `glGetString`/`glGetIntegerv` answering "no GL"),
  which is right for the guest: the guest never renders, the host does
  (7.2, the mirror).
- **Nothing fork-specific in the way**: no Qt, no bgfx in Coin's CMake;
  the fork's additions are the `CoinRT` output name, the `coin_fork_abi`
  C API, elements and shapes.
- **pivy** is one SWIG extension (`interfaces/coin.i` over a 736-line
  header list -> a 17.8 MB `coinPYTHON_wrap.cxx`, a 2.0 MB `coin.py`,
  a 33.8 MB `_coin.so` with symbols natively); SoQt/Qt are looked up
  only `if (SoQt_FOUND)`, so a guest build without SoQt drops
  `pivy.gui` by itself; `fake_headers/` already stubs GL/X11/Qt for
  SWIG's parse.  SWIG 4.4.1 is in `.conda/freecad`.
- **The guest toolchain** (`src/App/PyodideHost/guest`, emsdk 5.0.3,
  pyodide xbuildenv 314.0.6, `-fwasm-exceptions -sSIDE_MODULE=2 -flto`,
  `make_wheel.py` taking directories) has NO compiled third-party
  library yet -- Coin would be the first.  `SIDE_MODULE=2` exports
  only listed symbols, so Coin links STATICALLY into `_coin.so` (one
  module, the native shape) rather than as a second side module.
  Today's guest: 1.4 MB `.so`, 0.38 MB wheel.

**The probe, in order:** (1) the Coin fork configures and builds with
`emcmake` -- `COIN_BUILD_GLX=OFF`, `COIN_BUILD_EGL=OFF`, the OpenGL
`find_package` made conditional (a fork commit on `LinkVibe`: an
`COIN_BUILD_GL_STUB` option that skips the GL/X11 lookups and adds the
stub TU), `-fwasm-exceptions`, no `-pthread`, static library; (2) pivy
against it with the guest's Python headers, SWIG 4.4.1, no SoQt, the
single `_coin.so` as `SIDE_MODULE=2` exporting `PyInit__coin`; (3) a
`pivy` wheel through `make_wheel.py`, loaded beside `fcx_image`; gate:
`from pivy import coin; coin.SoDB.getVersion()` in the guest, a 10 k
node graph built and traversed by `SoSearchAction` and `SoGetBoundingBoxAction`
(no GL action), timed; then 7.9's probe re-run -- Draft's `Initialize()`
passes its self-test and reaches `import DraftTools`.  Sizes and boot
cost are the numbers to record.  The fallback if (1) or (2) fails on
something structural is 7.2's Coin-shaped model library.

**PROBE A PASSED 2026-09-05, the same day, on the first complete run
of each step.**  (1) The Coin fork gained `COIN_BUILD_GL_STUB` (commit
`6a79416a69` on `LinkVibe`): an `elseif` before the OpenGL lookup that
sets `HAVE_OPENGL` and nothing else, and `src/glue/gl_stub.cpp`, 420
core `gl*` entry points generated by `gen_gl_stub.py` from the
toolchain's `GL/gl.h` (vendor-suffixed and function-pointer-typed
declarations skipped; `glGetString` answers "1.1 stub", the `glGet*v`
family writes 0).  Configured with `emcmake` -- static, GLX/EGL/sound
off, Boost headers from `.conda/freecad/include`, `-fPIC
-fwasm-exceptions -sSUPPORT_LONGJMP=wasm` -- every one of the 464
real Coin sources compiled unchanged; `libCoinRT.a` is 11.8 MB.  (2)
`src/App/PyodideHost/pivy/CMakeLists.txt`: the host's SWIG 4.4.1 over
pivy's `coin.i` (17.8 MB of wrapper, 2.0 MB `coin.py`), the wrapper
compiled with the guest flags plus `PY_CALL_TRAMPOLINE`, Coin linked in
statically, `-sSIDE_MODULE=2` exporting `PyInit__coin`; `_coin.so` is
13.7 MB, the wheel 2.7 MB.  (3) `Layout::bundledCompiled`: a compiled
wheel beside the pure ones is bundled per (ABI, distribution) and the
runtime loads those of the running fcx_image's ABI after the pure
wheels; `FREECAD_PIVY_WHEEL` ships and mirrors it like the fcx_image
wheel.  Measured by `scripts/sandbox-pivy-probe.py` in the guest:

    import pivy.coin           0.55 s   Coin "SIM Coin 4.0.6rt", 649 SoTypes,
                                        1554 names in the module
    10 000 separators          0.77 s   30 001 nodes, SoTranslation + SoCube each
    SoSearchAction, all cubes  0.013 s  10 000 paths
    SoGetBoundingBoxAction     0.028 s  the right box
    guest boot                 +0.2 s   1.86 s -> 2.09 s with the wheel bundled

So the answer to 7.2's question is REAL COIN in the guest, not a
Coin-shaped model library; the mirror (G4) reads real `SoNode`s.
Draft's `Initialize()` self-test then reached `FreeCADGui.getSoDBVersion()`
(added to the guest module as `gui.sodb_version`) and, past it,
`import DraftTools` -- the same wall BIM hits, which is G3's ("Initializing
one or more of the Draft modules failed", caught by Draft itself, after
which its `Initialize` raises at `FreeCADGui.getMainWindow()`, so the
probe no longer activates from the host by default).  Traps:
the Emscripten toolchain confines `find_library` to its sysroot (name
the archive); the console binary does not flush `stdout` at exit (a
probe prints to stderr); `SoPathList` has no `len()`.

### 7.11 G3 sized: the forms, Qt-shaped **[sized 2026-09-05]**

G3 is U3 over the widget protocol (7.3) for the code as it is written:
`Gui.PySideUic.loadUi(":/ui/X.ui")` then `self.form.<name>.<Qt method>`
(41 panels), and forms built in code from `QtWidgets.QLabel(parent)`,
`QHBoxLayout()`, `layout.addWidget(w)` (DraftGui's toolbar, ArchPrecast,
ArchComponent, ArchWindow).  What the linter counts as U3 (1504 uses)
is, by chain: `SIGNAL`/`QObject.connect` 285, the widget constructors
(`QLabel` 94, `QPushButton` 86, `QWidget` 49, `QHBoxLayout` 49,
`QComboBox` 35, `QVBoxLayout` 29, `QLineEdit` 27, `QCheckBox` 24,
`QGridLayout` 21, `QDoubleSpinBox` 14, `QFormLayout` 13, `QSpinBox` 11,
`QGroupBox` 10, `QRadioButton` 7), the item models (`QStandardItem` 88,
`QTreeWidgetItem` 47, `QTableWidgetItem` 20, `QListWidgetItem` 12,
`QStandardItemModel` 9, `QStyledItemDelegate` 20), `QAction` 76 and
`QToolBar` 24 and `QMenu` 7, `Gui.Control.*` 124, `Gui.getMainWindow`
56, `loadUi` 41, `UiLoader().createWidget` 29 (all `Gui::InputField`
and `Gui::ToolBar`), `Gui.draftToolBar.*` 60.  The methods the panels
call on a form's widgets, by count: `text` 82, `setText` 80,
`isChecked` 72, `setProperty` 68 (`rawValue` on an InputField), `value`
58, `setEnabled` 49, `setValue` 45, `clicked` 44, `property` 38,
`currentIndex` 36, `setChecked` 34, `setCurrentIndex` 29, `addItem` 23,
`hide` 22, `setIcon` 21, `currentIndexChanged` 20, `selectedItems` 19,
`stateChanged`/`checkStateChanged` 34, `pressed` 16, `show` 11,
`expandAll` 11, `currentText` 11.  The `.ui` files (68) use 41 widget
classes: QLabel 409, `Gui::PrefCheckBox` 136, QPushButton 118,
QGroupBox 93, QCheckBox 51, QComboBox 45, `Gui::InputField` 45,
QWidget 44, `Gui::PrefLineEdit` 42, QDialog 32, `Gui::PrefComboBox` 32,
`Gui::ColorButton` 31, QSpinBox 29, QLineEdit 28, QDialogButtonBox 26,
`Gui::PrefSpinBox` 26, `Gui::QuantitySpinBox` 21,
`Gui::PrefColorButton` 21, `Gui::PrefDoubleSpinBox` 20, QRadioButton
15, `Gui::PrefUnitSpinBox` 15, then the trees, lists and tables (QTreeView
10, QListWidget 10, QTreeWidget 7, QTableWidget 2) and singletons.

**The decision: one model per Qt class, Qt's property names as the
state.**  Probe B put UNMODIFIED ipywidgets in the guest and rendered
its core models; the corpus does not speak ipywidgets, it speaks Qt,
and the core models fit it badly (a `QPushButton` is a Button or a
ToggleButton by a runtime `setCheckable`; a `QLabel` has no `enabled`;
every DOMWidget drags a Layout and a Style model, three comms a widget,
where a form is sixty widgets).  So the FreeCAD widget module 7.3
called for is the Qt subset itself: a guest package `freecad.widgets`
(the fcx_widgets wheel; `freecad` is a namespace package the bundled
wheels share -- they all `loadPackage` into one site-packages) of
`ipywidgets.Widget` subclasses (`Widget`, not `DOMWidget`: ONE comm a
widget, no layout/style models) with `_model_module = "freecad.widgets"`
and a model per Qt class -- `QWidgetModel`, `QLabelModel`,
`QPushButtonModel`, `QToolButtonModel`, `QCheckBoxModel`,
`QRadioButtonModel`, `QLineEditModel`, `QTextEditModel`,
`QSpinBoxModel`, `QDoubleSpinBoxModel`, `QComboBoxModel`,
`QGroupBoxModel`, `InputFieldModel` (also `Gui::QuantitySpinBox`),
`ColorButtonModel`, `UiFormModel` -- whose synced traits ARE the Qt
properties (`text`, `checked`, `enabled`, `visible`, `toolTip`,
`value`, `minimum`, `maximum`, `singleStep`, `decimals`, `items`,
`currentIndex`, `rawValue`, `unit`, `color`, ...), so a `.ui`
`<property>` is a state key and the host view's `apply` is a
property-by-property `setX`.  The Qt-flavored accessors (U7) are the
methods of those classes: `setText`/`text`, `setChecked`/`isChecked`,
`setValue`/`value`, `setProperty`/`property`, `show`/`hide`/
`setVisible`/`isVisible`, `setEnabled`/`isEnabled`, `setToolTip`,
`addItem`/`addItems`/`clear`/`count`/`itemText`/`currentText`/
`setCurrentIndex`, `setObjectName`/`objectName`, `findChild`,
`setWindowTitle`, `setWindowIcon`, `setFocus` (a custom message),
`selectAll`, `setStyleSheet` and the size hints (accepted, most
ignored).  The guest classes `PySide.QtWidgets.QLabel` and the rest
ARE those models, so the corpus constructs them unchanged; the
`Gui::Pref*` classes are their base class in the guest plus
`prefEntry`/`prefPath`, and the HOST makes the real `Gui::Pref*`
widget (through uic for a `.ui`, `UiLoader().createWidget` for a built
one), so the preference reading and saving stays native.  The core
ipywidgets models and their views stay for scripts that use them.

**Signals.**  `PySide.QtCore.Signal` is a descriptor yielding a
per-instance signal with `connect`/`disconnect`/`emit`; `SIGNAL("x()")`
is the name and `QObject.connect(obj, sig, fn)` is
`getattr(obj, name).connect(fn)` (the 285 old-style uses).  A value
signal is a trait observer emitting with Qt's argument
(`stateChanged(2)` for checked, `currentIndexChanged(i)`,
`valueChanged(v)`, `textChanged(s)`), so a programmatic `setChecked`
fires it exactly as Qt does; an event signal (`clicked`, `pressed`,
`returnPressed`, `editingFinished`, `textEdited`) is a custom message
`{"event": name, "args": [...]}` from the host view.  A radio button's
exclusivity is native on the host and mirrored in the guest among
siblings of one parent.

**`loadUi` on both sides.**  The guest's `FreeCADGui.PySideUic.loadUi(
path)` reads the file's text through `gui.ui.read` (the host's Qt
resource system or a file under the module roots: data, not code),
parses it with `xml.etree` (every `<widget class= name=>`, its
`<property>`s and `<item>`s), builds the model of each class (an
unknown class is a `QWidgetModel`) and a `UiFormModel` root carrying
`uiFile` and `widgets {name: ref}`, each named widget an attribute of
the root, as uic does.  The values parsed from the file are the
model's initial state but NOT the host's: a model lists in `_touched`
the properties the guest SET (a setter, a constructor argument), the
file's values are written silently, and the host's `UiFormView` loads
the same file through `FreeCADGui.UiLoader().load` (Qt's uic: the
layout exact, the strings TRANSLATED, the custom widgets real) and
BINDS each named child widget to its model -- the same view class,
adopting the existing widget instead of building one -- applying the
touched properties only; a built widget likewise gets the touched
ones and keeps Qt's defaults for the rest (a default `prefPath` of ""
applied to an InputField throws).  A view therefore has two entries,
`build(parent)` and `bind(widget)`.

**The task panel.**  `FreeCADGui.Control.showDialog(panel)` registers
the guest panel as a proxy with the panel hook list (`accept`,
`reject`, `clicked`, `open`, `getStandardButtons`,
`modifyStandardButtons`, `needsFullSpace`, `isAllowedAlterDocument`,
`isAllowedAlterView`, `isAllowedAlterSelection`, `helpRequested`,
`shouldShow`; `isHookName` learns them) and crosses
`gui.control.show [descriptor, form ids]`; the host builds a
`GuestTaskPanel` whose `form` is the rendered forms' widgets and whose
hooks forward to the stand-in (the standard buttons an int of Qt's
values, which the shim's `QDialogButtonBox` enum carries), and shows
it through the native `Control.showDialog`.  `closeDialog`,
`activeDialog`, `clearTaskWatcher` are one op each.  `showWidget`
stays for plain ipywidgets.

**In stages, each with its gate:**

- **G3a** -- the models above for the plain Qt classes plus
  `Gui::InputField`/`Gui::QuantitySpinBox` and `Gui::ColorButton`, the
  signals, `loadUi` on both sides, the task panel bridge.  Gate
  `SandboxForms`: a probe in the guest loads Draft's
  `TaskPanel_OrthoArray.ui` (9 InputFields, 3 spin boxes, 3 check
  boxes, 3 radio buttons, 4 buttons, 4 group boxes), sets fields
  Qt-style, connects old- and new-style signals, shows it as a task
  panel; the host's uic form is the active dialog with the guest's
  values in the bound widgets and a `.ui` default left untouched; a
  host edit of each kind reaches the guest state and its Qt-named
  signal with Qt's argument; OK crosses `accept` where the guest reads
  every field back; and Draft's real `task_orthoarray.py`, exec'd
  unmodified in the guest, constructs, shows, and answers
  `get_numbers()`/`get_intervals()` from the bound form.
- **G3b** -- the rest of the `.ui` subset: `QDialog` with
  `QDialogButtonBox` and a synchronous `exec_()` (a nested Qt event
  loop on the host inside one op; the guest's callbacks nest as the
  slider already does), the tree/list/table family with typed items
  (`QTreeWidgetItem`, `QStandardItem`, `QListWidgetItem`, delegates as
  cell types), `QTabWidget`/`QStackedWidget`/`QScrollArea`/`QSplitter`
  as containers, `Gui::FileChooser` (an `fs` grant), `QFontComboBox`,
  `QTextBrowser`, `QProgressBar`.  Gate: every one of the 41 `loadUi`
  panels of Draft and BIM opens from the guest and round-trips its
  fields (a harness over the list, the way the reopen gate walks the
  corpus).
- **G3c** -- forms built in code: the layout classes as a widget's
  `layout` state (`{type, items}` nested, re-synced on mutation), the
  `QWidget` container, `QSpacerItem`/`QSizePolicy` accepted, `QAction`/
  `QToolBar`/`QMenu`/`QDockWidget`, `QFont`/`QColor`/`QIcon`/`QPixmap`
  as data, `getMainWindow()` as a shim (`addToolBar`,
  `mainWindowClosed.connect`, `addStatusBarItem`), `DraftLineEdit`'s
  `keyPressEvent` and `DraftBaseWidget`'s `eventFilter` as a `keys`
  custom event stream the host sends for the widgets that ask.  Gate:
  `DraftGui` imported in the guest, `draftToolBar.taskUi()`/`lineUi()`
  shown from the guest and `validatePoint` round-tripping the point.
  This is the wall both `Initialize()`s stop at (7.9), so the G2b
  runner follows it.
- **G3d** -- the selection input (U4's `Selection` as a model) and the
  U2 dialogs (`QMessageBox.question`, `QInputDialog.getText`,
  `QFileDialog.getOpenFileName`, `QColorDialog.getColor`: one
  synchronous op each, the static calls the subset already names).

Cost, expected: one comm per widget (a sixty-widget form about 60
opens at 0.5 ms), the panel's construction in the guest, one hop per
host event; measured at G3a.

**G3a BUILT 2026-09-06**, the gate green on the fourth complete run
(`SandboxForms`, 3 cases; all seven GUI gate cases OK).  What the
build settled beyond the sizing:

- **The traits are `q_<property>`.**  A trait named `text` would
  shadow Qt's getter `text()` on the same object (the corpus calls
  `w.text()`, `w.value()`, `w.icon()`, `w.color()`), so every Qt
  property is the trait `q_text`, `q_value`, ...; the host strips the
  prefix and calls Qt's own `setProperty`, with handlers for what is
  not a Q_PROPERTY (a combo's items and icons, an icon path, a color
  as four floats).  `setProperty("rawValue", v)` and `property(...)`
  map by name.
- **`_touched`, not `_uiDefaults`.**  The class default is as wrong to
  apply as the file's value: `prefPath = ""` applied to an InputField
  throws in `GetParameterGroupByPath`, an empty `toolTip` would clear
  uic's.  So a model lists the properties the guest SET (setters,
  constructor arguments), the `.ui` loader writes the file's values
  silently, and a view applies the touched properties at bind/build and
  every key of a later update (equal or not: a text the guest set that
  the file already had still overrides uic's translation of it).
- **Layouts came forward from G3c.**  Draft's `task_orthoarray.py`
  rearranges its form at construction (`group.layout().takeAt(0)`,
  `item.widget().setParent(None)`, `grid.addWidget(w, r, c, 1, 2)`,
  `form.findChild(QtWidgets.QGridLayout, "grid_number")`), so the
  layout classes exist in the guest as plain objects on their widget
  (`QVBoxLayout`, `QHBoxLayout`, `QGridLayout`, `QFormLayout`, items
  and spacers), the `.ui` parser builds them with their names, and a
  mutation crosses as a layout op on the OWNING widget's comm
  (`{"layout": name, "op": addWidget|insertWidget|removeWidget|takeAt|
  addLayout|addStretch|addSpacing|setContentsMargins|setSpacing, ...}`)
  that the host applies to the real layout of that name under that
  widget (a `.ui` file reuses names -- OrthoArray has two
  `gridLayout_5` -- so the search starts at the owner).  A widget the
  guest made in code and adds to a layout gets a view BUILT under the
  layout's widget then (`QLabel(translate(...))` in linear mode).
  `setParent` crosses as an event (`None` hides, as Qt's does).  A
  layout with no name reaches no host layout: code-built forms are
  still G3c.
- **`prefs.write`** joined the catalog (2.2): the panel's first act is
  `params.set_param("LinearModeOn", ...)`, and DraftGui's ContinueMode,
  the 83 `set_param` sites of Draft's GUI side and every `Pref*` widget
  write the store.  DENY for a document (not promptable, like `gui`),
  ALLOW for the session and addons; the guest's `ParamGet(...).Set*` /
  `Rem*` now cross through `freecad.prefs.write`/`remove` instead of
  warning once, and `draftutils.params.set_param*` are a second facade
  entry on the same guest module (the generator merges entries by
  module: one permission per entry).
- **`FreeCADGui.ActiveDocument`**, the first U4 op: every Draft
  panel's `finish()` is `Gui.ActiveDocument.resetEdit()`; the guest
  gets a `GuiDocument` (`resetEdit`, `Document`) while the host has an
  active document, None otherwise; `setEdit`/`getObject` wait for U4.
- **The host's `Control.activeDialog()` is a bool** in this fork (as
  its `showDialog` returns None), so a gate drives OK/Cancel through
  the manager's panel object; `TaskDialogPython::reject` reading None
  as False is what Draft relies on (its `finish()` closes the dialog
  itself through the command's `completed()`).
- Facts of the run: a `Manager.reset()` between cases must keep the
  dispatcher (the guest is the same one; a real reset re-registers);
  `faulthandler` in the gate script needs `sys.__stderr__` (FreeCAD's
  console has no fileno) and the runner prints a failure's traceback
  as it happens, since the crash that followed (a `setParent` on a
  uic child whose form the failed bind had let go) took the buffered
  report with it -- a failed bind now deletes the form itself; the
  guest's `Quantity.UserString` has no decimals setting ("120 mm" to
  the host's "120.00 mm"), so an InputField's text differs between the
  two until a host edit sends the host's -- both parse to the same
  value, which is all the corpus does with it (13).

Measured (RelWithDebInfo, Xvfb, `scripts/sandbox-gui-gate.py`):

    TaskPanel_OrthoArray.ui loaded in the guest,   0.084 s: 40 models, 91 comm
      5 fields set, shown as a task panel           ops, 1 gui.ui.read, 1 control.show
    Draft's TaskPanelOrthoArray() + show           0.067-0.094 s: 127 comm ops (the
      (params, linear-mode re-layout included)       layout ops among them), 12 mod_call
    host spin-box edit -> guest state + signal     0.19-0.21 ms each
    guest setValue -> Qt widget                    0.09 ms each

Gate `SandboxForms` (3 cases): the probe form from the .ui with its
values in uic's widgets and the file's own left alone, a hundred host
edits and a hundred guest sets, every edit kind reaching its Qt-named
signal with Qt's argument, OK reading every field back, Cancel,
close from the guest; Draft's `task_orthoarray.py` unmodified --
constructed (linear mode re-layout included), shown, `get_numbers()`/
`get_intervals()` answered from host edits, its checkbox callback,
its reject; a document principal refused at `loadUi`.  Suites after:
the three GUI gates 7/7; `Tests_run --gtest_filter='Expression*'`
pyodide 104 passed + 1 skipped, wasi 31 + 74 skipped on this box.

### 7.12 The host widget layer sized: native task panels on the same models **[sized 2026-09-06]**

The question, asked at the end of the G3a session: replace the Qt
task panel with imgui through the same API translation the guest
uses, for native code too.  Answered in three rulings the same day,
each on a sizing: imgui out on the desktop, DOM in the browser, and
the host abstraction itself KEPT -- two backends, desktop first.

**What is there.**  The vocabulary is small and already toolkit-
neutral: 22 model classes, about 60 `q_` properties, 9 layout ops,
the custom events (`setFocus`, `selectAll`, `setParent`), 12 panel
hooks; only the consumer, `freecad.widgets.qt` (1121 lines), is
Qt-specific.  Dear ImGui 1.92.8 WIP is vendored under bgfx and, since
`BGFX_BUILD_TOOLS` forces `example-common` on, already compiled and
linked into `FreeCADRenderer` with bgfx's glue, nanovg and the SDF
font code -- unused, harmless, left alone; the WASM build leaves it
out.  The renderer has an overlay pass block fed with geometry, no
text path in the 3D tier, and no input path at all (events go Qt to
Quarter to Coin); Qt owns the GL context and bgfx blits into Qt's
framebuffer.  The browser viewer renders locally from the scene
stream and its chrome is SolidJS over the canvas.

**The native corpus** (counts from the tree at `f9084a4ee6`):

    TaskDialog subclasses / TaskBox subclasses      93 / 35
    lines in the 90 dialog units                    ~60,000
    TaskView framework + Control                    ~5,500
    dialogs driven by a uic'd Ui_* header           74 of 90
    units with item views (tree/list/table)         20
    expression bind( sites                          261 in 40 files
    units with SelectionObserver / gate / QTimer    17 / 14 / 12
    C++ Control().showDialog call sites             171 (56 from setEdit)
    Python Control.showDialog / loadUi sites        109 / 198
    .ui files: 511, uic-compiled 365, Python-loaded 142

    custom widgets the forms depend on             lines
    QuantitySpinBox + ExpressionBinding + label    ~1,400
    DlgExpressionInput + ExpressionCompleter       ~3,500
    PrefWidgets (18 classes)                       ~2,300
    Widgets.cpp (ColorButton, ActionSelector, ...) ~3,000
    InputField, FileChooser, DlgPropertyLink       ~3,500

By module the dialog code is PartDesign 14.8k, Fem 12.7k, TechDraw
10.5k, Part 7.3k, Sketcher 4.8k, Surface 3.4k, Gui 2.0k, then Drawing,
Robot, Mesh, Material.  Draft, BIM, Assembly, Spreadsheet, CAM have
no native TaskDialog.  What makes the hard panels hard is mostly NOT
widget code: selection observers and gates, document observers,
timers, Coin callbacks sit on QtCore and App and survive a widget
swap untouched.  What moves is the form, the item views and the
custom widgets' presentation.  The outliers: Sketcher's constraint
and element lists (custom item widgets, hover to 3D), Fem's
`TaskPostBoxes` (2383 lines, VTK pipelines), Part's `TaskDimension`
(Coin decorations), `TaskAttacher`, PartDesign's `TaskMultiTransform`
(nested sub-dialogs) and `TaskHoleParameters`, `DlgPropertyLink`,
Drawing's `TaskOrthoViews`.

**imgui, and why not.**  On the desktop it cannot give back
accessibility (none upstream, none planned), the stylesheet look, or
rich text; focus, nested `exec()`, native dialogs, timers and
observers would all have been fine, since Qt keeps the event loop.
It would have cost a 3-5k walker, a 1k input/IME bridge and a panel
host, for unification with tiers that do not exist yet.  In the
browser, pure DOM against imgui-with-a-hidden-input: the walker is
1.5-2k of TypeScript beside the existing inspector and ships nothing;
imgui needs the walker plus 1.2-1.6k of browser-only bridge (pointer,
the hidden input under the caret, touch to mouse, kinetic scroll,
clipboard, arbitration with the viewer's navigation), about 1 MB of
wasm plus shipped fonts (CJK a subset or a multi-MB TTF, wasm has no
system fonts), redraws while a caret blinks, a second visual system
on the page, and pixels for tests.  The hidden input fixes entry into
a focused field, with upstream focus/backspace issues open, and
nothing else: not scrolling physics, accessibility, wrap and reflow,
or rich text; the EditContext API that would make it proper is
Chromium-only with no Emscripten binding.  A task panel is mostly
typed fields (`Gui::QuantitySpinBox` is the most common `.ui` control
after labels, and a quantity field is a text field with a unit
parser), so the case imgui was built for is the opposite of this one.

**The host layer's shape.**

- **The core is a property bag.**  Each host widget class is a QObject
  in its own namespace with Qt's class and method names kept
  (`Fw::QLabel::setText`, as the guest's `PySide.QtWidgets.QLabel` IS
  the model), a variant map keyed by Qt's property names, a dirty set,
  the layout op log, and moc'd signals with Qt's names and arguments:
  traitlets in C++.  A backend is a consumer of the bag and nothing
  else; whatever the DOM will need must be in the bag.  That is the
  one design rule.
- **Our own `.ui` generator, not uic.**  On the guest's `uic.py`
  parser, a generator emits a C++ `Ui_X::setupUi` that instantiates
  the host classes with their names, layouts and the file's values,
  so a ported dialog keeps `ui->lengthEdit` as a typed member and its
  `connect` calls compile unchanged.  A ported translation unit never
  includes QtWidgets, so there is no name ambiguity.
- **The Qt backend reuses G3a wholesale.**  For a `.ui` form the C++
  Qt view loads the same file through `UiLoader` and binds each named
  child to its host object, as `UiFormQtView` does for the guest:
  uic's layout exact, strings translated, `Gui::Pref*` and the
  quantity widgets real; layout ops cover mutations; no layout
  fidelity code on the Qt side at all.  Binding writes uic's
  translated strings back into the bag, which is what gives the DOM
  tier translated text later for free.
- **Custom widgets split on a seam that exists.**  `ExpressionBinding`
  is already a non-widget mixin, so the host `QuantitySpinBox` mixes
  it in and owns the binding, while the real `Gui::QuantitySpinBox`
  stays the Qt presentation with its f(x) label and expression dialog.
  Pref widgets are two properties in the bag and the real widget on
  the Qt side.
- **One store for both producers.**  A guest comm open creates the
  same host object by model name, so the C++ Qt view renders guest and
  native panels alike and the Python Qt manager retires; the plain
  ipywidgets views of Probe B may stay in Python behind the same
  `gui.comm` dispatch, split by model module.
- **The task panel framework** keeps `TaskDialog` and its hooks
  (already non-widget) and gains a `TaskBox` model; `Control` and
  `TaskView` are untouched, a realized root being a QWidget to them
  exactly as `GuestTaskPanel` is now.
- **A second backend costs one consumer**, not a second design: the
  C++ Qt view is the port of the Python manager (2.5-3.5k), every
  vocabulary addition then lands once per backend, and gates run per
  backend.  The DOM walker (G7) is 1.5-2k of TypeScript, the manager
  beside the guest in the browser's worker when the panel is a guest
  one, a remote view over the socket when it is native on a serving
  host -- the thin-client model already.

**Stages and gates.**

    H0  the bag, signals, the generator, the class set the first two
        targets need, the C++ Qt view with UiLoader binding, layout
        ops, task panel integration.  Gate: OrthoArray from the guest
        renders through the C++ store, SandboxForms green, the Python
        Qt manager off for freecad.widgets models.
    H1  first native ports: one form-only panel in src/Gui
        (TaskAppearance or TaskOrientation), then a PartDesign panel
        with expression binding (Pad).  Gate: the ported panel drives
        its property, bind and f(x) round-trip, existing suites green.
    H2  the form-only majority, about 60 units, mechanical.  Gate:
        each module's tests plus a per-panel open and round-trip
        harness.
    H3  item views (tree/list/table items, model-backed views), then
        the nine outliers, Sketcher's lists last.

**Cost, desktop only** (lines, new or rewritten):

    property core, signals, the 41 classes' API subset      6-9k
    generator (Python, on uic.py)                           0.5-1k
    C++ Qt view, binding, layout ops, item views            3-4k
    task panel integration, guest store switch              ~2k (retires 1.4k Python)
    custom widget seams (quantity, expression, pref, ...)   1-2k
    dialog ports: 60 light, 29 heavy                        10-15k touched
    total                                                   25-30k

Every line serves the DOM tier later.  Out of scope even then: the
property editor (8.4k), the tree view, preference pages, and the 154
`QDialog` forms -- Qt stays the application shell.  The imgui route
would have added a 3-5k walker, the input bridge and a host, for a
30-40k total; that number is kept here as the record of why it lost.

**Ordering.**  G3b's class set (dialogs, the tree/list/table family,
containers, the file chooser) is H3's; done G3b-first its views would
be written in Python and re-ported.  So H0 and H1 go before G3b, and
G3b then lands on the C++ store.  The G2b runner waits on G3c either
way.  Next step: H0, starting with the property core and the
generator, gated on OrthoArray through the guest before any native
port -- that gate already exists.

**H0 BUILT 2026-09-06**, gate green: OrthoArray from the guest renders
through the C++ store and the C++ Qt view, `SandboxForms` 3/3 with the
Python Qt manager holding no `freecad.widgets` model, the seven GUI
gate cases OK.  What is there, in `src/Gui/Fw/` (4.7k lines, namespace
`Gui::Fw` for the toolkit-neutral half, `Gui::FwQt` for the Qt
consumer):

- **`FwCore`** -- `Fw::Widget`, a QObject whose state is a QVariantMap
  under Qt's property names with declared defaults, a touched set, and
  the signals `propertiesChanged(names, source)`, `requested(name,
  args)` (model to backend: `setFocus`, `selectAll`, `setSelection`,
  `setCursorPosition`, `setParent`), `eventEmitted(name, args)`
  (backend to model: `clicked`, `pressed`, `editingFinished`, ...) and
  `layoutChanged(op)`.  Three writers, one rule each: a typed setter
  (`Source::Native`) marks touched; a backend (`Source::Backend`)
  reporting the user does not; the guest (`Source::Guest`) does not
  either -- it syncs its own `_touched` list, and marking on its
  behalf was the one bug the gate caught (a `.ui` value the guest's
  loader wrote silently came back touched, since traitlets does not
  resend an unchanged `_touched = []`).  `propertiesChanged` names
  every key written, equal or not; the typed value signals fire on a
  real change only, whichever side made it.  `property`/`setProperty`
  HIDE QObject's, since Qt's would read a Q_PROPERTY of the model
  object instead of the bag.  `Fw::Layout` holds items and positions
  and emits every mutation as an op on the OWNING widget (through
  parent layouts), the guest's payload shape exactly, with the widget
  as a `QObject*` instead of a comm id; an unnamed layout emits
  nothing.  The QObject tree IS the model tree: `findChild` works,
  radio exclusivity walks siblings, `setParent` requests the backend
  to move the realized widget.
- **`FwWidgets`** -- the 22 classes with Qt's methods and moc'd
  signals (`Fw::QSpinBox::valueChanged(int)`, `Fw::QCheckBox::
  stateChanged(int)`, `Fw::InputField::valueChanged(double)`, ...),
  `Fw::UiForm` (the file and its named widgets), and the factory
  `createWidget(name)` keyed by Qt class name as a `.ui` spells it OR
  by guest model name (`Gui::PrefCheckBox` -> `Fw::QCheckBox` with
  `qtClass` kept; `InputFieldModel` -> `Fw::InputField`).  An
  `InputField::setValue` formats its `text` in the unit schema at the
  user's decimals, so the bag carries what a DOM tier would show.
- **`FwStore`** -- the guest's objects by comm id: `commOpen` creates
  by `_model_name`, writes the `q_` state silently, takes `_touched`,
  `qtClass`, `uiFile` and the `widgets` map; `commUpdate` is
  `setProperties(Guest)`; `commCustom` is a request, a `setParent`
  (ids resolved to objects) or a layout op relayed; `commClose`
  deletes (children the guest re-parented under it are unparented
  first, their comms being open).  The way back is a sink: a
  non-guest `propertiesChanged` goes out as an `update` with `q_`
  keys, an `eventEmitted` as a `custom`.  The store knows neither
  Python nor Qt.
- **`FwQtView`** -- the port of the Python Qt manager's Qt-shaped
  half.  One `View` per realized model; `build` makes the real widget
  of `qtClass` (FreeCAD's through the widget factory, Qt's through a
  cached `UiLoader`), a `UiForm` loads its file through the same
  loader and binds each named child; `bind` READS the widget back into
  the bag silently first (every bag key the widget has as a
  Q_PROPERTY, `visible` excepted, colors as four floats, enums as
  ints), then applies the touched keys.  `apply` orders range before
  value, items before index, drops `text` beside `rawValue`, and
  otherwise calls Qt's `setProperty`; the Qt signals are connected by
  the REAL widget's type (`qobject_cast` down a fixed list), writing
  the bag from `Source::Backend` under an `applying` guard so nothing
  echoes.  A view lives on the model, watches the widget's `destroyed`,
  and `release`s when a dialog deletes its content.
- **`FwQtPanel`** -- `PanelDialog`, a `TaskDialog` whose content is
  the realized forms and whose hooks go to a `PanelHooks` interface;
  `SandboxGui.cpp` implements it over the guest's stand-in (the
  descriptor's hook list decides `has`, a None answer reads False as
  TaskDialogPython does).  `TaskView` never learns anything new.
- **`FwPy`** -- `FreeCADGui.FormWidgets`: `ids`, `count`, `info(id)`
  (class, qtClass, properties, touched, bound, the form's map),
  `find`, `resolve`, `widget(id)` (the PySide wrapper of the realized
  widget), `setProperty`, `stats`, `reset`, and `accept`/`reject` --
  the active `PanelDialog`'s buttons, needed because this fork's
  `TaskView` never hands a dialog its button box, so
  `Control.activeTaskDialog().accept()` clicks nothing (the gate's
  first false alarm).  Plus the QVariant <-> PyObject conversions the
  bridge shares.
- **`src/Tools/fwuic.py`** (0.4k) -- the generator: `Ui_X::setupUi(
  Fw::UiForm*)` with typed members (`Gui::Fw::InputField* input_X_x`),
  the file's layouts by name (a reused name gets `_2`, as uic does),
  values through `setInitial`, `<item>`s as a combo's lists, spacers
  and positions; the parse rules are the guest loader's, and the
  guest's `qtdata.Qt` supplies the enum values.  `quantity` maps to
  `rawValue`.  Strings are emitted untranslated: the Qt backend reads
  uic's translations back at bind time, and the DOM tier's
  translation is its own question.  The header is named `fwui_X.h`,
  since CMake's AUTOUIC claims every `ui_*.h` include for uic.
- **The bridge** (`SandboxGui.cpp`): a `jupyter.widget` comm whose
  opening state says `_model_module == "freecad.widgets"` goes to the
  store, and every later message on an id the store holds; the plain
  ipywidgets models of Probe B still go to the Python manager, so
  `SandboxWidgets` is untouched.  `gui.control.show` builds a
  `PanelDialog` when every form id is the store's (else the Python
  `show_panel`); `close`/`active`/`query` run in C++ against
  `Control()` and the active Gui document.  `freecad.widgets.qt` lost
  its Qt-shaped views (402 lines); `GuestTaskPanel` stays for plain
  ipywidgets roots.

Measured (RelWithDebInfo, Xvfb, the same gate):

    TaskPanel_OrthoArray.ui loaded in the guest,   0.077 s: 40 objects opened, 50
      5 fields set, shown as a task panel           updates, 91 comm ops (was 0.084)
    Draft's TaskPanelOrthoArray() + show           0.074 s: 42 objects, 75 updates, 10
                                                     customs (layout ops), 127 comm ops
    host spin-box edit -> guest state + signal     0.16-0.20 ms each (was 0.19-0.21)
    guest setValue -> Qt widget                    0.05-0.06 ms each (was 0.09)

Tests: `tests/src/Gui/FormWidgets.cpp` (11 cases, QTest, offscreen: the
bag's defaults, touched and signal semantics, coercion, radio
exclusivity, combo items, layout ops reaching the owner and not from
an unnamed layout, the factory, the generated OrthoArray form's names
and values, the Qt view binding uic's form from the file path -- read-
back, touched applied over uic's, both directions, a layout op on the
real layout, a request, detaching when the widget dies -- and a built
widget).  A fact it recorded: uic writes the file's `quantity` double
into `Gui::InputField`'s `Base::Quantity` property, which does not
take, so the real widget shows 0 and the bag, mirroring the widget,
does too; the generated form holds the file's 100 until bound.
Suites after: the GUI gates 7/7, FormWidgets 11/11, `Tests_run
--gtest_filter='Expression*'` 104 passed + 1 skipped.

**H1a BUILT 2026-09-06**: the first native port, `TaskOrientation`
(`src/Gui/TaskView/`).  Neither candidate is reachable from the app
any more -- the Image module that opened `TaskOrientationDialog` was
removed from this fork, and `TaskView` stopped adding `TaskAppearance`
long ago -- so the port is the mechanics proof and its gate is a test.
The panel is now a QObject over `Fw::UiForm` plus the generated
`Ui_TaskOrientation` (`fwui_TaskOrientation.h`, `fc_wrap_fwui` in
`cMake/FreeCadMacros.cmake`, the .ui in `src/Gui/Fw/fwui.qrc` under
`:/ui/`); its five `connect`s compile unchanged against the models'
signals; the dialog realizes the form through `FwQt::realize` into
its `TaskBox`, and the unit includes no QtWidgets.  What the port
needed and the layer gained: `<size>` properties as the width/height
bag keys (`minimumSize`, `maximumSize`), in the generator and the
guest loader alike; a grid item's `colspan` without `rowspan` (both
readers dropped the span); the generated class named from `<class>`
inside its namespaces, as uic does (`Gui::Ui_TaskOrientation`, plus
the `Ui::` alias); and a pixmap path scheme, `bitmap:<name>`, the Qt
backend renders through the BitmapFactory at the label's size (the
preview icon; the bag carries a name a DOM tier can resolve).  Gate
`test_taskOrientationPort` in `FormWidgets_Tests_run` (12/12): the
file's title reached the model translated, `open()` restores the
placement into the models and the widgets follow, the widget drives
the property (offset, plane, reverse), the model drives the widget,
accept; the GUI gates stay 7/7.

**H1b BUILT 2026-09-06**: Pad and Pocket's panel, with the expression
seam.  Their shared form `TaskPadPocketParameters.ui`, owned by
`TaskExtrudeParameters` (245 `ui->` uses, 10 `bind(` sites, seven
`Gui::PrefQuantitySpinBox`, three `Gui::DoubleSpinBox`), is now the
models fwuic generates (`fwui_TaskPadPocketParameters.h`, the .ui in
`Resources/PartDesign.qrc` under `:/ui/`); `ui->lengthEdit` is a typed
`Gui::Fw::QuantitySpinBox*` and the file's 245 uses, its `connect`s
(retyped to the models' signal classes) and its two grid connections
compile unchanged.  `TaskSketchBasedParameters`'s code-built widgets
(the profile edit, `LinkSubListWidget`, the fitting group, the
operation combo) stay Qt: the port is the FORM, and the surrounding
`TaskBox` is realized into the same `proxy` by `FwQt::realize`.  The
Qt-only calls the panel keeps -- an event filter, a `ButtonGroup`, the
blink target, the select-reference label, `hookPropertyBool` -- reach
the real widget through `FwQt::widgetOf(model)`.

The seam, the sizing's `ExpressionBinding` on the host models:
`Fw::ExpressionBound` mixes `Gui::ExpressionBinding` (already a
non-widget mixin) into `QuantitySpinBox` and `DoubleSpinBox`, so a
bound model owns the binding and mirrors it in two bag keys --
`binding` (the path) and `expression` (the text, or empty).  `bind`
writes `binding` once (rewriting it inside the expression-changed
handler would make the backend re-bind the real widget mid-emission,
a heap corruption the Pad gate caught); the document's own change
comes back through the binding's `onChange` into `expression` and the
value.  The Qt backend, on the `binding` key, binds the REAL
`Gui::PrefQuantitySpinBox`/`Gui::DoubleSpinBox` to the same typed path,
so its f(x) label, its read-only-while-expression state and its
expression dialog work exactly as before; `apply` is the real
widgets' `apply` (the expression stands, else the value goes to the
property through a command).  The pref widgets' history and
`selectNumber` are requests the backend forwards.  A new channel in
the core, `Fw::Backend`, carries a write straight to the one backend
(the widget shows the value BEFORE the typed signals fire, and a
panel's `QSignalBlocker` on a field, which silences the slot it hangs
on, no longer silences the mirror to the widget).

Gates: `test_expressionSeam` in `FormWidgets_Tests_run` (13/13: bind,
document -> bag -> value, the widget's dialog -> document -> bag, clear,
a `DoubleSpinBox` on a sub-path, the signal-blocker semantics) is the
in-process proof.  `SandboxNative.py` (a GUI-gate module, in the
default list) opens Pad's real panel, drives `Pad.Length` from the
widget, sets an expression on the document and reads it back read-only
through the widget, and OKs it into the `ExpressionEngine` -- RESULT
OK, 8/8 for the four-module gate.  It first ran alone: closing a
3D-view document and then pumping the event loop tripped a teardown
crash in this fork, unrelated to the sandbox (reproduced with a bare
empty document), so a run that closed the guest gates' documents and
then opened Pad's aborted.  The cause was in `ViewAreaCell`: the
hosted view is a Qt child, deleted by `~QWidget` AFTER the cell's
member destructors, and the `destroyed` lambda `hostView` connects
then stored nullptr into the already-destructed `QPointer` member --
a second weak release that freed Qt's refcount block under the dying
view, which `QObject::~QObject` then wrote into ("shared QObject was
deleted directly", a corrupted heap at the next malloc).  Fixed
2026-09-06 by deleting the view in `~ViewAreaCell` while the member is
alive; the gate with `SandboxNative` in the default list is the
regression test.

**G3b BUILT 2026-09-06**, on the C++ store: the rest of the `.ui`
subset, and every `.ui` file Draft and BIM load with `loadUi` opens
from the guest and round-trips its fields.  The class set grew by
`QDialog` with `QDialogButtonBox`, `QTabWidget`, `QStackedWidget`,
`QScrollArea`, `QSplitter`, `Gui::FileChooser`, and the item views --
`QListWidget`, `QTreeWidget`, `QTableWidget`, `QTreeView` (with
`QListView`, `QTableView`) -- on both sides (guest `items.py`, 2.9k;
the host classes, the Qt view and the store's additions, 3.3k C++).
What the build settled:

- **The item views are ROWS, and the rows are not in the bag.**  One
  representation serves the four Qt classes: a row is cells (text,
  icon, tool tip, check state, flags, colors, bold, alignment) and,
  for a tree, child rows; a view holds the top-level rows and the
  columns, the selection and the current row as state.  A thousand-row
  tree resending itself on every `setText` is exactly what the bag
  must not carry, so the rows are a tree the `Fw::ItemView` object
  owns, changed by ITEM OPS on the view's comm -- `{"item": "insert" |
  "set" | "row" | "remove" | "clear" | "sort", ...}`, one small message
  per mutation, the layout-op pattern -- that reach the backend through
  `Backend::itemsChanged` and the `itemsChanged` signal; `snapshot()`
  is the tree as data (a DOM tier's first paint, `FormWidgets.info`).
  An item built before it is attached sends nothing; `addTopLevelItem`
  sends its whole subtree as one insert.  The guest's `QStandardItemModel`
  is a plain object, not a comm: its rows cross on the comm of every
  view it is set on (`setModel` resyncs), so a DOM tier sees one kind
  of thing.  User-role data stays on the guest (it holds FreeCAD
  objects the wire could not carry); the host never needs it.
- **One Qt path through the abstract item model.**  The Qt view keeps
  a persistent index per row id and applies every op through
  `QAbstractItemModel` -- `insertRows`, `setData` by role -- whether
  the widget is a `QTreeWidget`, a `QListWidget`, a `QTableWidget`, or
  a `QTreeView` that uic left without a model (the backend makes a
  `QStandardItemModel` for it).  Where the abstract interface refuses,
  the typed call: item flags (`QTreeWidgetItem::setFlags` by walking
  the row path, a table's or a standard item created on demand) and
  header labels (a table's model has no header item to write to;
  `setHorizontalHeaderLabels` makes them).  What the user does comes
  back as state (`selection`, `currentId`, `currentColumn`) and events
  with row ids (`itemClicked`, `itemDoubleClicked`, `itemExpanded`,
  `itemEdited` -- a cell the user edited, merged into the row and
  `itemChanged` fired); the guest maps ids to its item objects and
  fires the Qt-named signals with Qt's arguments (`itemClicked(item,
  col)`, `cellChanged(r, c)`, `clicked(index)` on a view, the selection
  model's `selectionChanged`).  Header calls (`setSectionResizeMode`,
  `setStretchLastSection`, ...) are requests on the view.
- **Delegates are cell types.**  A corpus delegate dispatches
  `createEditor` on the column and returns a combo, a spin box, a
  line edit, a check box (Draft's and BIM's layer delegates).  The
  guest calls it once per column at `setItemDelegate` with a probe
  index, classifies the editor (`{"type": "combo", "items": [...]}`,
  `int`/`double` with range, `text`, `check`), closes the throwaway
  editor and sends `cellTypes`; the host's `CellDelegate` makes that
  editor and writes the text back.  The delegate's own paint,
  `setEditorData` and `setModelData` never run; the color columns'
  `QColorDialog` inside them is G3d.
- **`exec_()` is one op.**  `gui.dialog.exec [form id]` realizes the
  root as a window (`FwQt::realizeTopLevel`: a QDialog root under the
  main window, the widget dying with the model) and runs `QDialog::exec`
  inside the op; what the user does in the nested loop reaches the
  guest's slots nested (the comm messages call into the guest, as
  `addCommand`'s GetResources round trip already did), and the dialog
  code is the op's result.  `accept`/`reject`/`done`/`close`/`move`/
  `resize` are requests, `accepted`/`rejected`/`finished` events,
  `result`/`width`/`height` state.  A guest `form.show()` on a dialog
  root (`visible` TOUCHED true on an unrealized root) shows it as a
  window through the same helper -- only a dialog or a form root: a
  named child's `show()` is a property of a widget in a layout.  The
  `.ui` file's own `buttonBox -> accept` connection is uic's, so OK
  closes the dialog where the file says so and the code's
  `buttonBox.accepted.connect` where it does not.  `UiForm` IS a
  `QDialog` on both sides (a QWidget root never calls that half).
- **Containers.**  A tab widget's pages are child widgets, the titles
  the `tabs` state (translated by uic, read back at bind), the current
  one an index; `addTab`/`insertWidget`/`setWidget` cross as requests
  naming the page by model ref (`IPY_MODEL_<id>`, which the store
  resolves in any request argument), and a container built in code
  gets the pages its model collected before it had a backend.  The
  splitter's `setSizes` comes back as what Qt made of it (the ratio
  within the real width), a backend write.  `Gui::FileChooser` needs no
  file permission: the catalog offers no `fs`, and the path is data the
  HOST consumes (ShapeString's font file); its PySide wrapper is a
  plain `QWidget`, so a host test drives it by Q_PROPERTY.
- **Lessons the gate taught.**  Qt's modeless `QDialog.open()` on the
  guest class shadowed ipywidgets' `Widget.open()`, the method that
  makes the comm: no dialog form had one until the harness asked for
  `model_id` (the guest is exercised on the host's Python now:
  the wheel's modules with a stubbed bridge load all 40 files in a
  second, the loop that found it).  Qt's `close()` keeps the object
  and ipywidgets' closes the comm, so a dialog's `close` is Qt's and
  `__del__` the comm's.  A label's text flows one way (no user edit
  reports it).  `deleteLater` outside an event loop needs the deferred
  deletes flushed explicitly.  A python-mode `evaluate` keeps the
  module object it first imported, so a guest harness module is
  defined once per process and reset per test.  The QtWidgets shim
  in the guest image imports `QDialogButtonBox` from `qtdata` by name,
  so that module answers the name with the widget class through a
  module `__getattr__` (the image untouched; `QtCore.QModelIndex` is
  likewise not in the shim, and the corpus never touches it).
- **Left for later.**  A bare `QWidget` root shown with `show()` as a
  window (still left after G3c: DraftGui's tray is a tool bar the main
  window takes, and its panel goes through Control); `QDialogButtonBox.
  addButton(widget, role)`; `QDockWidget` (one BIM site); the delegates'
  color editors (G3d); a host-side sort leaves the guest's row order as
  inserted (ids still map).

Measured (RelWithDebInfo, Xvfb, `scripts/sandbox-gui-gate.py`,
`SandboxPanels`):

    the 40 .ui files loaded in the guest, shown,   1.4 s in all, 672 objects in the
      round-tripped, closed                          store; load 2-32 ms, show 2-39 ms each
    dialogNudgeValue.ui exec_() from the guest,    0.22 s (the timer's 0.2 s inside it)
      OK clicked by a host timer inside the loop

Gate `SandboxPanels` (2 cases): every one of the 40 files behind the
corpus's 41 `loadUi` sites (one site is Draft's generic
`loadUi(ui_file)` helper) loads in the guest with every named widget
of a known class as that class's model, shows -- a QDialog root as a
window, a QWidget root as a task panel -- with every named widget
bound to uic's, and round-trips one field of each kind it has (text,
plain text, int, double, quantity, check, combo, tabs, list, tree,
table, a tree view over a `QStandardItemModel` with a nested row,
file, the button box's `clicked`/`accepted`, the splitter's sizes,
the scroll area) guest to host and host to guest; `exec_()` returns
the dialog code the host's OK produced, `accepted` having crossed.
`tests/src/Gui/FormWidgets.cpp` gained `test_itemViews` (the four
views built from native models: rows, children, checks, expansion,
selection and current both ways, a widget edit back as `itemChanged`,
clear) and `test_dialogsAndContainers` (tabs both ways, the splitter,
the scroll area, the file chooser, a dialog as a window with its
button box `exec`'d and accepted from a timer, the window dying with
the model): 15/15.  Suites after: the GUI gates 10/10 (five modules),
`Tests_run --gtest_filter='Expression*'` 104 passed + 1 skipped.

**G3c BUILT 2026-09-06**: forms built in code.  Draft's `DraftGui.py`
runs UNMODIFIED in the guest -- `DraftToolBar()` at import builds the
tray tool bar (buttons in code, the style button's icon painted with
`QPainter`, `getMainWindow().addToolBar`), `taskUi()`/`lineUi()` build
the task panel (28 items in 16 nested box layouts, a spacer, a bold
font, the field-lock actions, two event filters), and `validatePoint`
round-trips a point typed on the host.  What the build settled beyond
the sizing (7.11):

- **The layout tree crosses whole, as `layoutSpec`.**  A code-built
  layout carries a generated name (`_fcx_layout_<n>`), and every
  mutation on it re-syncs the OWNING widget's `layoutSpec` (the whole
  tree: `{class, name, items, margins, spacing}`, an item a widget ref,
  a nested layout, a spacer with its policies, a stretch, a spacing, an
  action or a separator) and then sends the op as before -- two
  messages a mutation, sixty-odd for DraftGui's panel, nothing.  The
  store rebuilds the object's `Fw::Layout` from the spec silently; the
  Qt view realizes it when it builds the widget (`View::makeLayout`,
  the real layouts named after the guest's), and the ops that follow
  find them by name.  A widget the guest hid before placing it stays
  hidden (`showInLayout`); a child no layout placed is built as a child
  still.  A `.ui` file's layouts are uic's and cross as ops only
  (`_from_ui`).
- **A tool bar's and a menu's content is a bar** (`Layout::Bar`, name
  `_fcx_bar`): widgets, actions and separators in order, realized
  without a QLayout (`fillBar`/`barOp`; a sub-menu is a `QMenu` widget
  item).  `QAction` is a model like a widget (text, icon, checkable,
  checked, enabled, visible, tool tip, shortcut), rendered by an
  `ActionView` on whatever carries it -- a widget's `addAction` (the
  `actions` state, a line edit's trailing lock icon among them), a bar,
  a menu -- or bound to a real one (the bar's `toggleViewAction`, the
  `toggleViewAction` state).  `getMainWindow()` is a shim: `addToolBar`
  realizes the bar on the host (`gui.mainwindow`, the bar dying with
  its model), `mainWindowClosed.connect` registers a hook, `showMessage`
  and `cursor().pos()` are data; `getActiveWindow`/`getWindows` wait
  for the mirror (G4).  `QMenu.exec_()` is one op (`gui.menu.exec`, a
  nested loop at the cursor), the chosen action its result and the
  menu's `triggered`; `runCommand` is `gui.cmd.run`;
  `Control.addTaskWatcher` registers each watcher as a proxy the host's
  `TaskWatcherPython` reads (`gui.control.add_watcher`).
- **The key event stream is `watchEvents`.**  A widget that installs an
  event filter or overrides `keyPressEvent` lists the QEvent types it
  wants (`watchEvents`, Qt's values; every text input asks for focus
  too, so `hasFocus()` is state), the host's relay sends each as the
  `qevent` event (`[type, key, modifiers, text, autoRepeat]`, a mouse
  event's position and buttons, a focus event's reason), the guest
  runs its filters and handler and answers `eventDone [eaten]` INSIDE
  the event, and an eaten event stops at the relay -- DraftGui's
  snap-cycling key never reaches the line edit, the lock filter's Enter
  does.  A key press costs 1.3 ms end to end.
- **`QTimer.singleShot` is a queue** the image drains at the end of
  every dispatched request (`_drain_timers`, prelude + dispatcher):
  Draft's `todo.delay` schedules `doTasks` before appending to the
  itinerary, so a shot that ran at once ran on an empty list.
- **A `QPainter` on a `QImage` records an SVG** (polygons, rectangles,
  ellipses, lines with the pen and brush) that crosses as a
  `data:image/svg+xml` icon path the host renders (`svgDataIcon`); a
  `QFont` crosses as `{bold, italic, pointSize, family}` over the
  widget's own; `sizeHint()`/`QFontMetrics` are estimates (seven
  pixels a character).  `InputField.valueChanged` carries a `Quantity`
  (the overload PySide connects; DraftGui reads `d.Value`), and
  `UiLoader().createWidget` answers None for a class outside the subset
  (DraftGui's `Gui::ToolBar` fallback).
- **The prelude delegates.**  Every `FreeCADGui` name it does not define
  is `freecad.widgets.gui.attr(name)` (the AttributeError `hasattr(Gui,
  "Snapper")` expects), and `_proxy_register` is exposed to the wheel:
  a new name is a host build, not an image rebuild.  The `fcx_draft`
  wheel gained `DraftGui`, `draftguitools/gui_field_locks` and
  `draftutils/todo`.
- **Lessons the gate taught.**  A later `def` in a class body wins over
  an earlier one (the old `installEventFilter` stub shadowed the real
  one; the host-python harness found it in a second).  Enter on a
  field locks it and, on Z, `validatePoint` unlocks every field right
  after -- natively too.  The fork's sequencer holds a WAIT CURSOR --
  whose filter eats every key and mouse event -- until a poll queued on
  the event loop sees the sequence over (`finishAggregate`), and a gate
  runs its modules in one callback: after SandboxNative's pad recompute
  the keys vanished, so a gate module spins the loop first (`settle`).
  A document principal may not open widgets (correctly refused), so a
  gate builds them through a session exec and reads through evaluate,
  where `__import__` is blocked.

Measured (RelWithDebInfo, Xvfb, `scripts/sandbox-gui-gate.py`,
`SandboxDraftGui`):

    DraftGui imported in the guest (the tray built,    (in the host-python harness,
      the style icon painted, addToolBar)                the whole import 0.15 s)
    draftToolBar.lineUi(): the panel built in code     0.09-0.20 s, 46 objects; 352
      and shown (28 items, 16 sublayouts)                comm ops (39 opens, 253
                                                         updates, 59 ops), 19 mod_call
    a key press on the host -> the guest's filters     1.3 ms
      -> eaten or not
    a guest menu exec_() with a host click 100 ms in   0.12 s

Gate `SandboxDraftGui` (3 cases): the tray as a tool bar of the main
window with its four buttons, the painted icon, the square checkable
tool button, the toggle-view action bound (a host trigger reaching the
guest, the guest's hide reaching the bar); `lineUi()` as the active
task panel with its layout tree, the shown and hidden fields, the bold
label, the lock actions; a point typed on the host (focus, values,
Enter) delivered to the guest's callback by Enter, by the Enter Point
button and by the panel's accept, the lock filter locking on Enter and
unlocking on a double-click, the snap-cycling key eaten, a digit not;
`offsetUi()` and a close from the guest; a command registered and run
from the guest, the main window's message and close hook, a guest
menu run modally with a host click choosing its second item; a
document principal refused.  `tests/src/Gui/FormWidgets.cpp` gained
`test_codeBuiltLayouts` and `test_barsActionsAndKeys`: 17/17.  Suites
after: the six GUI gate modules 13/13, `Tests_run
--gtest_filter='Expression*'` 104 passed + 1 skipped.

The **G2b runner** followed, BUILT 2026-09-06 (7.9): Draft's and BIM's
`InitGui.py` in the guest at startup, Draft's tool bars from the guest;
both `Initialize()`s now stop at the GuiUp ruling (sec 13), then G3d.

**G3d BUILT 2026-09-07**: the selection input and the U2 dialogs.

- **`FreeCADGui.Selection` in the guest is the host's**, one op per
  call (`gui.sel.call [name, args]`, `freecad.widgets.gui.Selection`):
  the arguments cross by value with an object as its handle, and the
  host calls its own `FreeCADGui.Selection.<name>` -- no second
  implementation of the resolve modes, the sub-name grammar or the
  stack.  The subset is the 18 methods Draft and BIM reach for
  (`getSelection` 198 sites, `getSelectionEx` 66, `clearSelection` 55,
  `addSelection` 45, then `hasSelection`, `getCompleteSelection`,
  `removeSelection`, `isSelected`, `countObjectsOfType`, the
  preselection pair, the picked list); a name outside it is an
  AttributeError naming it (`addSelectionGate` is the one the linter
  still lists).  **A SelectionObject crosses by value**: `Object` and
  `Document` as handles, the names and the picked points as data, and
  `SubObjects` resolved in the guest through `Object.getSubObject` on
  first read -- `SelectionObjectPy.xml` needs no annotation, and the
  facade generator's list did not grow.
- **An observer is a guest proxy** with the eight-hook list
  (`SEL_OBSERVER_HOOKS`), and the host's own `SelectionObserverPython`
  holds the stand-in and drives it exactly as it drives a native Python
  observer: one hop per hook the class defines, the arguments by value
  (document and object NAMES, the sub-element, the point).  No new
  transport: the task watchers' route (G3c).  `removeObserver` finds
  the stand-in by proxy id; a guest reset drops every observer
  (their stand-ins point at a guest that is gone) from the boot
  listener that re-runs the InitGui's.  BIM registers ten of these,
  Draft none.
- **The four stock dialogs are one synchronous op each**, in
  `freecad.widgets.models` so both `QtWidgets` and `QtGui` (BIM's
  spelling) find them: `gui.dialog.message` for `QMessageBox`'s
  statics and its instance shape (`setText`, `setStandardButtons`,
  `exec_`; the button pressed as Qt's StandardButton value, the enum
  the `QDialogButtonBox` constants already carried), `gui.dialog.input`
  for `QInputDialog.getText/getItem/getInt/getDouble/getMultiLineText`
  (`(value, ok)` as PySide answers), `gui.dialog.file` for
  `QFileDialog.getOpenFileName/getOpenFileNames/getSaveFileName/
  getExistingDirectory` (`(path, selected filter)`), `gui.dialog.color`
  for `QColorDialog.getColor` (a QColor, invalid when canceled --
  `QColor.isValid()` is real now).  The host runs the nested loop under
  its main window; a `parent` argument is accepted and ignored.  **The
  file dialog's answer is DATA**: whether the guest may then read or
  write that path is the file-system grant's business (7.1, U2), which
  is not built -- there is no `fs` permission in the catalog yet.
- Not in G3d: the delegates' color editors inside the item views (the
  static exists, the delegate's editor path is G3b's `cellTypes`
  contract) and `addSelectionGate`.

Gate `SandboxSelection` (4 cases): the guest selects by handle and by
names, the host sees it; `getSelectionEx` with a face, `SubObjects`
resolved through `getSubObject`; the host's changes are the guest's
next read; an observer sees the host's add, add with a point, remove
and clear in order, sees the guest's own add (a nested entry), and
nothing once removed; each dialog driven from a host timer inside the
nested loop (Yes clicked, an instance's Save, text retyped, the third
item, an int canceled, a file picked, a save named, a color chosen and
a color canceled); a document principal refused.
Measured (RelWithDebInfo, Xvfb, `scripts/sandbox-gui-gate.py`,
`SandboxSelection`): the four cases in 4.1 s; a stock dialog from the
guest's call to its answer, the host's timer clicking it at 30 ms,
35-60 ms each (the first QMessageBox 0.26 s, its construction); the
observer's four host events delivered in order, the guest's own add
seen through the nested entry.  What the build settled: a session
guest holds an object handle for ONE evaluation (no owner document to
re-resolve it in), so `Selection.getSelection()` is read where it is
used, and BIM's observers get names, as natively; `FullName` is the
host's verbatim (the Python expression that re-selects); the widget
QFileDialog lists its directory asynchronously, so a driver selects by
its name edit, not `selectFile`; `getCompleteSelection`,
`getPickedList` and the preselection answer SelectionObjects, as the
host does, wrapped on the guest side by shape.

### 7.13 The session document sized: `FreeCAD.ActiveDocument` for a command **[sized and RULED 2026-09-07; S1 and S2 BUILT 2026-09-07]**

The question, asked after G3d ("next session size it first"): a guest
command can read the selection now, but its `Activated()` runs as the
session principal, and the session has no document -- so Draft's
`gui_base.Activated` (`self.doc = App.ActiveDocument`, `finish()` on
None) and BIM's command modules (490 `ActiveDocument` uses in 60 files)
stop at their first line.  What does it take for a command body to
read, hold and write the document the user is working in, which
command bodies that unblocks, under which permission, and what is the
gate.  Sized, then ruled the same day (below); the build starts
the next session.

**Why the gap exists.**  Reach on the wire is anchored on ONE object,
`HandleTable::owner()`, set by `Private::Transaction` for each
host->guest call (`ExpressionImageHost.cpp`): `evalExpression` and a
proxy op whose first argument is a document object set it (an
`execute()`, an `onChanged`); `exec` and a proxy op with no such
argument -- a command's `Activated`/`IsActive`, a workbench's hooks,
the InitGui runner, and every `gui.*` op made inside them -- pass
nullptr.  Three things hang on the owner (`ExpressionImageBridge.cpp`):
the write gate (`writeGate`: the target is the owner or in the owner's
document, then `doc.write.self`; no owner, no writes -- "writes are
allowed on the evaluation owner's document only"), `resolveByKey` (a
durable handle re-resolves into the owner's document only, under
`doc.read.self`; no owner, refused), and `active_doc` (the owner's
`Document`; None without one).  And the table is cleared after every
routed expression evaluation at depth 0 (`ExpressionEvaluator.cpp:
121`), so a session handle lives until the next expression anywhere
evaluates, then its id is stale and its key has nowhere to
re-resolve: G3d's "a session guest holds a handle for ONE evaluation".
The principal is not the problem: `Runtime::Scope(nullptr)` is
`session`, and the catalog's session column is ALLOW for
`doc.read.self`, `doc.write.self` and `doc.foreign` alike (2.2).  The
session lacks no permission; it lacks a "self" for the reach checks
to anchor on.

**What the corpus needs** (counted 2026-09-07 over
`draftguitools/gui_*.py`, 65 files; `bimcommands/*.py`, 83; and
Draft's `draftmake`/`draftfunctions`/`draftutils`, the make_* layer a
command reaches next):

    use                                 Draft cmds   BIM cmds    draftmake+
    ----------------------------------  ----------   ---------   ----------
    ActiveDocument / self.doc, refs     130 (45 f)   490 (60 f)  ~55
    .recompute                           66 (*)       94 (*)     --
    .openTransaction / .commitTransaction 19 / 19     89 / 77    3 / 3
    .getObject                           16           80          2
    .addObject / .removeObject            4 / 4       18 / 17    35 / 5
    .Objects                              8           26          1
    .findObjects / .copyObject           --            3 / --     1 / 2
    .ActiveObject / .RootObjects         --            2 / 1     --
    .save / .saveAs                      --            1 / 1     --
    FreeCAD.setActiveDocument            --            5         --
    App.activeDocument() (the call)      --           --          3, + todo.py
    Gui.doCommand (strings)              23 + commits  196       --
    Gui.ActiveDocument.ActiveView        --           26         --  (G4)

    (*) mostly inside doCommand strings: "FreeCAD.ActiveDocument.recompute()"

By file: 13 Draft commands never touch the document, 12 touch it
without the 3D view or the snapper, 40 need the view as well; BIM 23 /
40 / 20.  So the session document alone puts 52 command modules' bodies
(12 Draft, 40 BIM) within the guest's reach as far as the DOCUMENT
goes -- `Draft_Heal` (`self.doc.openTransaction`, `heal.heal(sel)`,
`commitTransaction`: direct, no view), `BIM_Trash`/`BIM_EmptyTrash`,
`BIM_Ungroup`, `BIM_Unclone`, `BIM_Reextrude`, `BIM_Glue`, the
classification, material, IFC property and layer panels -- and the 60
that also need the view wait for G4 regardless.  Two more prerequisites
stand in front of most of the rest, and both stand on this one:

- **`Gui.doCommand` is not in the guest at all** (not a `FreeCADGui`
  name the wheel answers: AttributeError).  U4 rules it "runs under
  the CALLER's principal in the caller's guest" (7.1), so
  `Draft.make_line(...)`, `Arch.makeSite()`,
  `FreeCAD.ActiveDocument.recompute()` execute IN the session guest --
  and need the session document.  Every Draft creator commits through
  it (`todo.ToDo.doTasks`: `App.activeDocument().openTransaction`,
  `Gui.doCommand(string)` per string, `commitTransaction`); BIM 196
  uses.  Sized below as S2.
- **`IsActive` is False** for every Draft creator (`bool(
  get_3d_view())`, 7.9) and for BIM's (`hasattr(getMainWindow().
  getActiveWindow(), "getSceneGraph")`; the shim's `getActiveWindow`
  is None until G4).  CORRECTED while building S1: `Command::_invoke`
  DOES consult it ("check if it really works NOW", Command.cpp) --
  `Gui.runCommand` of a command whose IsActive is False is a silent
  no-op.  Draft's `GuiCommandSimplest.IsActive` is
  `bool(App.activeDocument())`, True in the guest since S1, so
  `Draft_Heal` runs; BIM's wait for G4 or a stub view (the gate
  stubs `IsActive` for its two BIM commands).  Not this slice.

**RULED 2026-09-07 (user): "1. agree ... should also apply to
Gui.ActiveDocument.  2. ... for a workbench, I incline to grant it
access to all documents by default."**  The three rulings, and what
each settles:

1. **No new permission for the session's document writes.**  They are
   `doc.write.self` / `doc.read.self`; reach into any other open
   document is `doc.foreign`, which the catalog already ALLOWs the
   session and addons.  The same rule backs `FreeCADGui.ActiveDocument`
   and `Gui.activeDocument()`.
2. **A workbench reaches EVERY open document.**  The session and addon
   principals' reach set is all open documents, not one anchored
   document -- the catalog's stance made mechanism.  The captured
   anchor and the switch rule of the first draft are DROPPED:
   `FreeCAD.ActiveDocument` reads the host's live active document on
   every call (BimLibrary calls `setActiveDocument` and then saves),
   and a kept handle re-resolves by its own document's name wherever
   the user's focus is, as natively -- which closes G3d's "one
   evaluation" limit for session handles outright.  A DOCUMENT
   principal keeps its own document only, always.
3. **`setActiveDocument`, `newDocument`, `closeDocument` are the
   workbench's** under one new enum value, `app.write` (DENY document,
   not promptable; ALLOW session and addon); `listDocuments` and
   `getDocument` under the existing `app.query`.  **`save()` is
   declared** (the user's own path, the document as content: Ctrl+S).
   **`saveAs(path)` takes a PICKER-BLESSED path only**: a path the
   host's file dialog returned to this guest (`gui.dialog.file`) is a
   capability the guest may hand back, for the life of the guest; any
   other path is refused naming the fs slice (7.1, U2).  BimLibrary's
   one `saveAs` passes: its path comes from `getSaveFileName`.
   `mergeProject` (2 uses, a file read) waits with the fs slice.

The concern weighed and how the design answers it: **reach follows
the PRINCIPAL, never the call shape.**  The reach set is computed from
the current principal class (`Runtime` scope stack): a document
principal gets the owner's document only, even if some later op ever
lands it in a transaction with no owner; session and addon get every
open document.  Keyed on "no owner" instead, a document hook reaching
an ownerless nested call could have written the active document under
`doc.write.self` -- no such path exists today (`gui.*` and `doCommand`
are DENY for a document), and the rule keeps a later op from opening
one.  Weighed and accepted: `closeDocument` is silent data loss
natively and stays so for a workbench (an audit line; never a
document's); a workbench writing expressions into document B changes
B's hash and voids B's grants, exactly as native editing does; with
every document open to it, the sandbox's value for a third-party
workbench is the import prompts, the network, crash isolation and the
audit trail -- the standing ruling that document-derived code is the
threat (1.4).

**S1, the design (as ruled).**

- `HandleTable::owner()` stays what it is (the object an owned
  transaction may write, the identity check for value handles).  A new
  helper, `reachable(table, App::Document*)`, answers the three checks
  from the PRINCIPAL: a document principal -> the owner's document
  only (`documentOf(table.owner())`, as now); session or addon -> any
  document in `App::GetApplication().getDocuments()`.  `writeGate`
  (same-document -> reachable, then `doc.write.self`, plus
  `doc.foreign` when the target is not the owner's document),
  `resolveByKey` (a key naming a reachable document re-resolves; a
  closed document is a ReferenceError, an unreachable one a
  PermissionError naming both), and `active_doc` (a document principal:
  the owner's Document as now; session/addon: the host's LIVE
  `getActiveDocument()` as a handle under `doc.read.self`, None with
  nothing open).  Nesting needs no anchor bookkeeping: the principal
  is the scope stack's.
- Guest side (the prelude, `ImageDispatch.cpp`: a guest image
  rebuild): `App.activeDocument()` beside the `ActiveDocument`
  property; `listDocuments()`, `getDocument(name)`, `newDocument(name,
  label, hidden)`, `closeDocument(name)`, `setActiveDocument(name)`
  as module facade entries -- the guest's Application shim knows
  every document the principal may reach, not the transaction's one.
  `FreeCADGui`: `ActiveDocument` becomes a GuiDocument over the same
  live document (`Document`, `getObject(name)` -> a view provider
  handle through the view family G2b resolves, `setEdit`/`resetEdit`,
  `Modified`), `activeDocument()` the function form; `ActiveView`
  stays G4.
- Facade (`DocumentPy.xml`, `call` tier, in the write family):
  `openTransaction`, `commitTransaction`, `abortTransaction`,
  `recompute`, `copyObject`, `save`; `saveAs` under the blessed-path
  check; read: `getObjectsByLabel` (call), `ActiveObject`,
  `RootObjects` (handle), `UndoCount`/`UndoNames`/`FileName`/`Label`
  (value).  NOT declared: `undo`/`redo`/`clearUndos` (unused);
  `App.ActiveDocument.Box` (an object read as a Document attribute) is
  `Document.__getattr__`, not a member -- `getObject` is the answer.
- The blessed paths: `gui.dialog.file` records every path it returns
  in a per-guest set on the host (cleared with the guest); `saveAs`
  checks membership before the call.  The set is the seed of the fs
  slice, not a substitute for it.
- Handles across calls: a kept `self.doc`/`self.obj` re-resolves by
  key into its own document after any `clearHandles`; a kept VIEW
  PROVIDER cannot (`handleKey` is null for one); the corpus re-reads
  `.ViewObject` each time; a `[doc, name, "vo"]` key is the one-line
  extension if something does.
- Cost: about 80 lines across `ExpressionImageBridge.{h,cpp}`,
  `ExpressionImageHost.cpp` and `SandboxGui.cpp` (the blessed set),
  the `app.write` enum value with its catalog row and name, a dozen
  XML annotations, the prelude and the wheel's GuiDocument, the gate.
  Under a day with the gate.

**S2, `doCommand` in the guest** (sized here because it stands on S1
and is the next blocker of every Draft creator; built after S1 on its
own go): `Gui.doCommand(src)` compiles and execs `src` in the guest's
`__main__` dict (the console namespace natively), `Gui.addModule(name)`
imports into it; nothing runs on the host.  One host op,
`gui.docommand {sha256, len}`, does what `Command::doCommand` does
BESIDES executing -- the macro recorder line and the audit line --
under a new enum value `gui.doCommand` (the catalog row exists, "not
in the enum yet": DENY document, ALLOW session, PROMPT addon).  Draft's
commit then runs end to end in the guest: `QTimer.singleShot(0,
doTasks)` is the shim's pending queue, drained when the request that
scheduled it returns (`_drain_timers`), so the commit runs at the end
of the same proxy call -- after `Activated` returns, as natively one
loop turn later.  About 40 lines plus the enum value; gate case: a
preselected `Draft_Upgrade` (with a selection `proceed()` runs
inline, no view callback) and `BIM_Site`.

**Gate `SandboxSessionDoc`** (a GUI gate module in the rig's default
list, seven cases):

1. A guest command's `Activated`: `FreeCAD.ActiveDocument.Name` is
   the host's, `App.activeDocument()` and `Gui.ActiveDocument.Document`
   the same object (by key), None with nothing open; after the guest's
   own `setActiveDocument(other)` all three follow.
2. Writes: `openTransaction`, `addObject("Part::Feature")`, `Label`
   and `Shape`, `ViewObject.Visibility` (the view family),
   `removeObject`, `commitTransaction`, `recompute()` returns a count;
   the host's `UndoNames` carries the name and one undo restores the
   object.
3. A handle kept on the command across two activations with a routed
   expression evaluation between them (`clearHandles`) re-resolves;
   the same handle still resolves and writes after the host activates
   ANOTHER document (the ruled behaviour, native parity); after the
   host closes its document: ReferenceError.
4. Two documents: `listDocuments()` names both, `getDocument(b)` is
   writable from a command while `a` is active; `newDocument` then
   `closeDocument` round-trips (the host's document count); a
   DOCUMENT principal's `execute()` is refused every one of the five
   (`app.write` not promptable; `getDocument` of another document a
   PermissionError).
5. Regression: a DOCUMENT principal's `execute()` sees its OWN
   document as `ActiveDocument` while another is active on the host,
   and its write into the other document is refused.
6. `save()` from a command writes the document's own file (mtime
   moves); `saveAs` to a path the guest's `getSaveFileName` returned
   (the gate's timer types it) writes it; `saveAs` to a path the
   guest made up is refused.
7. Corpus: `Draft_Heal` and `BIM_Trash` then `BIM_EmptyTrash` on a
   selected object from the guest -- direct bodies, no `doCommand`,
   no view: the object healed / in the Trash group and hidden / gone,
   and the host's undo stack holding each command's transaction.

**S1 BUILT 2026-09-07.**  Gate `SandboxSessionDoc` 7/7 in about 9 s;
the full GUI gate 30/30 (26 in the default process, the 4 InitGui
cases in theirs); Expression gtests 104 + 1 skipped (three updated to
the ruling, below).  What landed, and where it departs from the design above:

- **Reach** (`reachable(table, doc)` in `ExpressionImageBridge.cpp`,
  exported as `documentReachable` for Gui): the principal class comes
  from `Runtime::currentPrincipal()`; a document principal reaches
  `documentOf(table.owner())` only; the session, an addon, and host
  code running under NO scope (a test's `eval`, the InitGui runner's
  `exec`) reach every open document.  `writeGate`, `resolveByKey` and
  `active_doc` are as designed; a key into a document no longer open
  is a ReferenceError, into an open one out of reach a PermissionError
  naming both.
- **The document set is five bridge ops, not module facade entries**:
  `app.docs`, `app.doc`, `app.new_doc`, `app.close_doc`,
  `app.set_active_doc` (`FcxWire.h`, `applicationOp`).  A module
  facade checks one permission and nothing else, and `getDocument`
  needs the reach check in C++: a document principal WITH `app.query`
  granted still lists and gets its own document only (gate case 5).
  The three writes call the host's own `FreeCAD` functions, so the
  GUI follows them (a view for a new document, a closed one's views
  gone).  `newDocument` passes keywords: the host's takes no None for
  a name it was not given.
- **`app.write`** is in the enum (`Permission::AppWrite`), DENY and
  not promptable for a document.
- **Prelude** (guest image rebuilt): `activeDocument()`,
  `listDocuments()`, `getDocument`, `newDocument`, `closeDocument`,
  `setActiveDocument` beside the `ActiveDocument` property.
- **Wheel** (`freecad/widgets/gui.py`): `GuiDocument` over one App
  document -- `Document`, `getObject(name)` through the object's
  `ViewObject` (a REAL property on this fork, commit 17300660d8, so
  `read_prop` answers it: no view-family op needed), `setEdit`,
  `resetEdit`, `getInEdit`, `activeObject`, `Modified` through one
  op `gui.doc [name, member, args]` (`SandboxGui.cpp`) under the
  same reach check; `FreeCADGui.activeDocument()` and
  `getDocument(name)`.
- **Facade**: `DocumentPy.xml` as listed (`FileName`/`Label` are
  properties: `read_prop`, no annotation).  `GroupExtensionPy.xml`
  joined the annotated XMLs (`BIM_Trash`'s `trash.addObject(obj)`),
  its membership writes in the write family.
- **Blessed paths**: `blessPath` / `pathBlessed` /
  `clearBlessedPaths` in the bridge, fed by `gui.dialog.file`, cleared
  when a guest boots (`Private::initialize`).  `saveAs` decodes its
  path from the call's wire-encoded argument list (sec 12).
- **Two host findings.**  Any command calling `doc.copyObject`
  aborted natively ("still being filled in": the import's Restoring
  bit claimed as a live load; fixed in `refreshLiveLoad`, sec 12).
  And `Command::_invoke` DOES consult `IsActive` -- the sizing above
  said it did not; corrected in place.  A consequence of S1 itself:
  Draft's `GuiCommandSimplest.IsActive` (`bool(App.activeDocument())`)
  is now True in the guest, so those commands run; BIM's stay False
  until G4.
- **Gtests updated to the ruling**: `appModuleHasNoDocumentGraph`
  (the graph exists now: a PermissionError naming `app.query` for a
  document, not an AttributeError), `guestHandlesDurableAcrossHooks`
  (a forged key into an OPEN other document is the PermissionError, a
  key into no document a ReferenceError), `writePropSameDocument`
  (an ownerless eval, no scope, writes: host code).
- **A loss to list**: the copy `Draft_Heal` makes is a guest Draft
  object now, and its `onDocumentRestored` reaches
  `getattr(vobj, "Proxy", None)` on the HOST view-provider proxy --
  `__bool__` of a host Python instance, `unsafe.getattr`, a
  PermissionError printed on the console (the object is fine).  The
  view-provider Proxy class in the guest is G4's (7.9, BuildingPart's
  losses).

**S2 BUILT 2026-09-07.**  Gate `SandboxSessionDoc` 9/9 (two cases
added); the full GUI gate 28 in the default process + the 4 InitGui
cases in theirs; Expression gtests 104 + 1 skipped.  What landed, and
where it departs from the sizing above:

- **The permission** is in the enum (`Permission::GuiDoCommand`,
  `gui.doCommand`): DENY and not promptable for a document, ALLOW
  session, PROMPT addon -- the one row `catalogDefault` does not
  answer ALLOW for an addon.  `checkPermission` logs denials and
  prompts only, so the ALLOW trail U4 asked for is a new public
  `Runtime::auditAllowed(perm, target, context)` (`auditAllowed` at
  the chokepoint surface): one line per unique (principal, sha256,
  context) per process, like every other line.
- **The op carries the source**, not `{sha256, len}` as sized: the
  macro recorder needs the text (`MacroManager::addLine`, the console
  echo under `ScriptToPyConsole`), so `gui.docommand {src, kind}` with
  kind `app` | `gui` | `module` (`import name` for `addModule`, an App
  line as `Command::addModule` records it); the host hashes it for the
  audit target and puts `kind:len` in the context.  The source is
  never executed on the host: `doCommandRecord` in `SandboxGui.cpp`
  checks, audits and records, then replies; the wheel execs.  The
  check is the op's first act, so a refusal records nothing and the
  guest runs nothing.  The op is dispatched BEFORE the `gui` check --
  its own row, not `gui`'s (an addon holds `gui` and is prompted for
  this one).
- **The wheel** (`freecad/widgets/gui.py`): `doCommand`,
  `doCommandGui`, `addModule` -- the op, then `exec` in the guest's
  own `__main__` (`sys.modules["__main__"].__dict__`, with `FreeCAD`,
  `App`, `FreeCADGui`, `Gui` bound on first use as the host's console
  has them); `addModule` once per module name per guest.  Returns
  None; a SyntaxError or the source's own exception propagates, as
  `PyRun_String` does natively.  No image rebuild for these (the
  prelude delegates unknown `FreeCADGui` names to the wheel).
- **The commit runs in the same proxy call**, as sized: `finish()`
  queues `delayCommit` / `delayAfter` on the shim's timer queue and
  the dispatcher drains it when `Activated` returns -- the audit
  trail of a `Draft_Upgrade` reads `import Draft`, the
  `_objs_ = Draft.upgrade(...)` line, then the recompute line.
- **Three facade gaps the corpus hit**, each a `call` annotation and
  a guest image rebuild: `PropertyContainer.getEditorMode` (Draft's
  `format_object`, the line after `setEditorMode` which was declared)
  and `ViewProvider.listDisplayModes` (the same function), and
  `ViewProvider.addProperty` / `removeProperty` (`_ViewProviderSite.
  setProperties` in the guest; the bridge's view family already gated
  both, only the XML lacked the tier).
- **`Gui.ActiveDocument.ActiveView`** exists in the guest now, as
  the active-object registry only: `getActiveObject(name[, resolve])`
  and `setActiveObject(name, obj[, subname])` through the same
  `gui.doc` op (`ActiveView.<member>`, the document's active view's
  `MDIViewPy`).  `Draft.autogroup` asks it at the end of EVERY
  creator's commit (`getActiveObject("NativeIFC")`, then `"Arch"`,
  then `"part"`), so without it every Draft and BIM creator's commit
  raised after making its object and left the transaction open.  Any
  other view member raises an AttributeError naming G4;
  `hasattr(view, "getSceneGraph")` stays False, so the corpus's
  3D-view tests keep answering "no view".
- **The main window shim's MDI area** answers `findChildren` with an
  empty list: the snapper's `off()` (which `Modifier.finish` calls,
  `Gui.Snapper` existing once `gui_snapper` is imported) walks the
  MDI area's children for a QuarterWidget to set a cursor on.
- **A loss to list**: the Site's view provider is built in the guest
  (`_initializeArchObject` constructs it there), and its `attach`,
  `onChanged` and `updateData` reach `vobj.Annotation`, `RootNode`
  and `SwitchNode` -- Coin nodes, G4's mirror -- so the host prints
  one AttributeError per hook call (four on a `makeSite`); the object,
  the transaction and the recompute are right.  The gate stubs
  `IsActive` for both commands (Draft's `Modifier.IsActive` and BIM's
  ask for the 3D view).
- **A gate fact**: a package's first command module imports them all
  (`draftguitools.gui_heal` brings `gui_upgrade` with it), so a later
  loader that hooks `addCommand` around the import records nothing
  unless it `importlib.reload`s the module.

### 7.14 Host commands from the guest sized: the file and code chokepoints **[sized 2026-09-07]**

The question, asked after S2 ("shall we fine grain control doCommand?
there are commands allow file access" -- then "that's the same trap as
the deny list before sandbox work right? any better way"): a guest can
now run command scripts, and some commands read and write files or run
code from a file.  Where does the gate go.  Sized here; not built.

**`doCommand` is not the hole.**  Its source runs in the guest's own
interpreter under the caller's principal (S2), and reaches the host only
through the bridge ops the caller could call directly, each under its
own permission.  Checked over the whole declared surface (the generated
facades, the module facades): no member takes a host path except
`Document.save` (the document's own file) and `saveAs` (a picker-blessed
path only, S1).  The guest's `open()` reads pyodide's empty in-memory
file system, so `importIFC.insert("/home/x/a.ifc", doc)` in a command
string fails to find the file rather than reading it.  Classifying the
string's text would be evaded with `exec`, `getattr` or string building;
the sha256 in the audit line is a trail, not a filter.  When the fs
slice lands, each file op names its path and carries its own row, and
`doCommand` still adds nothing on top.

**The hole is `gui.cmd.run`.**  The guest's `Gui.runCommand(name,
index)` runs ANY host command by name under the plain `gui` row, with
no per-command check (`runCommandByName`, `SandboxGui.cpp`).  Three
host commands act on a file with no picker, so the user never consents:
`Std_RecentMacros` runs a macro file from the recent list, the index
choosing which (`Action.cpp:2080` -> `MacroManager::run` ->
`Interpreter::runFile`: arbitrary host Python); `Std_RecentFiles` opens
a recent file (`Action.cpp:1871` -> `Gui::Application::open`);
`Std_DlgMacroExecuteDirect` runs the macro editor's content.  A document
principal reaches none of this (`gui` is DENY for it, not promptable);
the session and addons get it for free today.  Addons are the case.

**Why not a list of command names.**  A deny list is the pre-sandbox
trap: every command classified by hand, the default open, and it decays
the moment upstream or an addon adds one.  An allow list fails closed
but is the same classification inverted, and commands are not a
declared surface the way the XML members are -- there is nothing next to
a command's `activated()` that says what it reaches.  The caller is the
wrong gate either way.

**The design: gate the primitive, keyed on the scope stack.**
`Runtime::check` returns at once when the scope stack is empty (host
code outside any evaluation is trusted, 2.4), and while a guest op runs
the stack carries the guest's principal for the whole synchronous call
-- that is what makes the `gui` check in `guiOp` work today.  When a
guest runs `Std_RecentMacros`, the host runs its `activated()` with the
guest still on the stack, so a check inside the primitive it ends up
calling fires under the guest and stays silent for the user's own click.
The primitives that read or write a host file or run code from one are
few, stable and in the core; the commands are hundreds, growing, in
every module.  The chokepoints, from the callers:

    primitive                                        covers
    -----------------------------------------------  ---------------------------------------
    Gui::Application::open / importFrom / exportTo   every Std file command, drag-and-drop,
      (Application.h:77-81)                          Std_RecentFiles, Gui.open/insert/export
    App::Application::openDocument / openDocuments,  the document loaders, loadFile
      loadFile
    App::Document::saveAs / saveCopy                 the writers by path (save writes the
                                                     document's own file: allowed, S1)
    Gui::MacroManager::run (Macro.cpp:382)           Std_RecentMacros, the macro dialog
                                                     (DlgMacroExecuteImp.cpp:393), the editor
    Gui::PythonDebugger::runFile                     the debugger's run
    Base::Interpreter::runFile                       the seam under all of the above; Base
                                                     cannot see App's runtime, so a guard
                                                     callback App installs (or the checks
                                                     sit at the callers above)

Not a chokepoint: `Interpreter::runString`.  Host C++ commands compose
Python and run it through `runString` (Std_Delete, every `doCommand`),
so gating it would refuse everything; caller-chosen content arrives as
a FILE, and `runFile` is the seam.  Any command that reaches a file
hits the gate whatever its name, present or future, Std or addon, and
whatever it went through on the way (a recent macro that opens a file
hits it twice).  Nothing is classified by hand.

**Consent stays a capability, as S1 ruled.**  A picker-driven command
(`Std_Open`, `Std_Import`, `Std_Export`, `Std_SaveAs`) runs its modal
dialog INSIDE the guest's scope -- the nested loop keeps the stack --
so a naive chokepoint would refuse the very file the user just chose.
The blessed-path set answers it: the host's own `FileDialog::
getOpenFileName` / `getSaveFileName` / `getOpenFileNames` bless the
paths they return while a scope is active (today only the guest's
`gui.dialog.file` blesses), and the primitive accepts a blessed path
without a grant.  A command with no picker has no blessed path and is
refused unless granted.  The blessing is per guest and cleared at boot,
as now.

**The rows** are the two the fs slice already designs (7.1, U2) and
one new one:

    permission        document   session   addon   notes
    ----------------  --------   -------   -----   -------------------------------
    fs.read:<path>    DENY (np)  PROMPT    PROMPT  a host file read by path, at the
                                                   chokepoints; blessed paths pass
    fs.write:<path>   DENY (np)  PROMPT    PROMPT  saveAs / saveCopy / exportTo
    host.exec:<path>  DENY (np)  PROMPT    PROMPT  code run from a host file (runFile)

One audit line each, the path as the target.  The session's PROMPT is
a change from "free": the session is the user's console and the bundled
workbenches, and natively `Gui.runCommand("Std_RecentMacros")` just
runs; under the sandbox it prompts once (scope once/session/always),
naming the path.  The user's own click never prompts: no scope.  `gui`
and `gui.doCommand` stay whole-row permissions.

**Two gaps, stated.**  (1) A command that defers its work through a
timer or a posted event finishes after the guest's scope popped, and
the check sees an empty stack.  Carrying the principal across async
boundaries is not solved in general (the widget layer has the same
edge); the file-running paths that defer are few and are to be checked
at the point they are queued -- the build enumerates the
`QTimer::singleShot` / `postEvent` sites on the paths above.  (2) Host
Python reached by host code only (`Gui.open(path)` from the console,
`Part.open`) is not gated and must not be: it has no scope.

**Gate `SandboxHostFiles`** (a GUI gate module): a guest command's
`Gui.runCommand("Std_RecentMacros", 0)` with a recent macro on the host
refused, `host.exec` in the message, an audit line with the macro's
path, the macro's side effect absent; `Std_RecentFiles` refused
(`fs.read`); `Std_Open` from the guest with the gate's timer typing a
path into the host picker opens it (blessed); `Std_SaveAs` the same
writes it; a document principal refused every one, not promptable;
`Gui.open(path)` from the test itself (no scope) unchanged; a session
grant (`ExpressionSecurity.grant`) lets the recent macro run and the
audit line reads allow.

**Cost.**  About 120 lines: the three enum rows with names and catalog
cells, `Interpreter::runFile` guard (a callback in Base, installed by
App) or checks at the two Gui callers, checks at the six App/Gui entry
points above, `FileDialog` blessing under an active scope, the gate.
Under a day with the gate.  Order: the user's call; it shares its
mechanism with N3's network rows and closes the one host-execution
path a guest has, so before N2 is the natural slot.

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
    tests/src/App/ExpressionImageHost.cpp           67    acceptance 6, bench 6 (disabled),
                                                          bridge 8, budget 4, eval 25 (the
                                                          G1a-G1d gates among them, the
                                                          Draft and BIM corpus gates, the
                                                          durable-handle gate),
                                                          host 9, routing 9
    tests/src/App/ExpressionPyodide.cpp             10    layout, verify, scoping, offer
    src/Mod/Test/SandboxPyodide.py                   2    the offer end to end
    src/Mod/Test/SandboxGui.py                       2    G2a: commands and a workbench
                                                          registered from the guest (7.9);
                                                          needs the GUI -- run through
                                                          scripts/sandbox-gui-gate.py on Xvfb
    src/Mod/Test/SandboxWidgets.py                   2    Probe B: an ipywidgets form from
                                                          the guest rendered in Qt, driven
                                                          both ways (7.3); the same gate
                                                          script (its default module list)
    src/Mod/Test/SandboxForms.py                     3    G3a/H0: a Draft .ui form and
                                                          Draft's own OrthoArray panel from
                                                          the guest, Qt-shaped, both ways,
                                                          through the C++ store and the
                                                          C++ Qt view (7.11, 7.12); the
                                                          same gate script
    src/Mod/Test/SandboxNative.py                    1    H1b: Pad's real panel on the
                                                          host widget layer, the expression
                                                          seam (7.12); the same gate script
    src/Mod/Test/SandboxPanels.py                    2    G3b: the 40 .ui files behind
                                                          Draft's and BIM's 41 loadUi sites
                                                          opened from the guest and
                                                          round-tripped, exec_() (7.11);
                                                          the same gate script
    src/Mod/Test/SandboxSelection.py                 4    G3d: FreeCADGui.Selection from
                                                          the guest, a SelectionObject by
                                                          value, an observer as a guest
                                                          proxy, the four stock dialogs
                                                          driven from a host timer (7.11);
                                                          the same gate script
    src/Mod/Test/SandboxSessionDoc.py                9    S1, S2: the session document (7.13):
                                                          a guest command's ActiveDocument,
                                                          writes, a kept handle, two
                                                          documents, the document-principal
                                                          regression, save/saveAs, Draft_Heal
                                                          and BIM_Trash/EmptyTrash from the
                                                          guest; Gui.doCommand / addModule in
                                                          the guest (the audit and macro lines,
                                                          a document refused), Draft_Upgrade's
                                                          commit and Arch_Site through
                                                          doCommand; the same gate script
    src/Mod/Test/SandboxDraftGui.py                  3    G3c: Draft's DraftGui.py in the
                                                          guest -- the tray tool bar, the
                                                          panel built in code, a point
                                                          typed on the host, the key
                                                          stream, a menu, the main window
                                                          shim (7.11); the same gate script
    tests/src/Gui/FormWidgets.cpp                   17    H0-H1, G3b, G3c: the host widget
                                                          layer's property core, class set,
                                                          layout ops, code-built layouts,
                                                          bars, actions, the key relay,
                                                          the fwuic generator's
                                                          output for OrthoArray, the Qt
                                                          backend binding uic's widgets, the
                                                          ports, the item views, dialogs and
                                                          containers (7.12, 7.11);
                                                          FormWidgets_Tests_run, offscreen
    src/Mod/Spreadsheet/TestSpreadsheet*.py          --   run with routing ON for parity

The acceptance harness opens a real saved-and-reopened `.FCStd` under a
real `document:sha256` principal and runs hostile expressions through
every layer.  Suites green at `fd14ba2878`: C++ 536/536, Python 2630;
the sandbox suites at the durable-handles commit (2026-09-05): pyodide
91/91, wasi 80 + 11 skipped, the four corpus gates passing with two
recomputes each.
Every gtest and the corpus gate select a runtime per process through
`FCX_RUNTIME`.

## 10. Configuration reference

Preferences under `User parameter:BaseApp/Preferences/Expression/`:

    Sandbox:Runtime          "pyodide" | "wasi" (default: pyodide when built)
    Sandbox:Evaluate         route evaluation through the image (default OFF);
                             also routes a document object's saved Proxy
                             to the guest at open, failing closed (3.5)
    Sandbox:InitGuiInGuest   run the InitGui.py of a module whose GUI side
                             is a bundled wheel (fcx_draft, fcx_bim) in the
                             guest (default OFF; 7.9 G2b); the rig's
                             FCX_INITGUI_IN_GUEST=1|0 overrides it
    Sandbox:BudgetMs         5000        Sandbox:GraceMs   1000
    Sandbox:ImagePath        Sandbox:StdlibPath          (WASI)
    Sandbox:PyodideDir       Sandbox:PyodideWheel        Sandbox:PyodideUserDir
    Sandbox:PyodidePackages  Sandbox:PyodideUnpinned
    Security:Enforce         default true

Environment: `FCX_RUNTIME`, `FCX_IMAGE`, `FCX_STDLIB`, `FCX_PYODIDE`,
`FCX_PYODIDE_WHEEL`, `FCX_PYODIDE_USER`, `FCX_PYODIDE_PACKAGES`,
`FCX_PYODIDE_UNPINNED`; rig-only `FCX_GATE_ONLY`, `FCX_SHEET_REPORT`,
`FCX_REPO`, `FCX_PROBE_*`, `FCX_INITGUI_IN_GUEST`.

CMake (`cMake/FreeCAD_Helpers/InitializeFreeCADBuildOptions.cmake`
:151-177): `BUILD_EXPR_PYODIDE_HOST` (default `v8-embed_FOUND`),
`BUILD_EXPR_WASI_RUNTIME` (OFF, never turns itself on),
`BUILD_EXPR_IMAGE_HOST` (ON iff either runtime; ON with none is a
configure error), `FREECAD_PYODIDE_DIR` (bundle a distribution in a dev
tree), `FREECAD_FCX_IMAGE_WHEEL` (ship the wheel under
`<datadir>/Pyodide/wheels/`), `FREECAD_PIVY_WHEEL` (the pivy wheel of
7.10, shipped and mirrored the same way), `FREECAD_BUNDLE_WASMTIME`.  With
`BUILD_EXPR_PYODIDE_HOST` and `BUILD_DRAFT`, target `DraftSandboxWheel`
packs `fcx_draft-<ver>-py3-none-any.whl` into the same wheels
directory (5.6); `WidgetsSandboxWheel` packs `fcx_widgets` (the comm
shim, 7.3) unconditionally, and `FREECAD_BUNDLED_WHEELS` (a `;`-list of
pure wheels, `scripts/sandbox-fetch-wheels.py` fetches ipywidgets and
traitlets) is mirrored one target per wheel (`<dist>_wheel`).

Command line: `--grant <permission>[:<target>]`, `--policy <file>`.

## 11. Roadmap, one list

In order; each step ships alone.  Done: Phase 0 audit (2026-08-30),
Phase 1 image and router (2026-08-31), the pyodide runtime and budget
(2026-09-02), N0 demotion, P1 bootstrap and offer, G0 linter
(2026-09-03).

1. **G1** -- Draft's App side in the guest, four stages (7.6): G1a
   the surface, G1b the wheel and loader, G1c rung 2 for one principal
   (one Draft Wire `execute()` byte-identical from a guest Proxy) DONE
   2026-09-04, its two decisions ruled and the Restore route (f) built
   the same day; G1d OPENED the same day -- the Draft test document
   reopens routed with every Proxy a stand-in and every object
   recomputing (harness `scripts/sandbox-reopen-routed.py`, gate
   `draftTestObjectsReopenRouted`); the Polygon ULP question answered
   (libm, gated in ULPs), `Proxy.<attr>` host reads and the
   construction dispatch BUILT the same day (gate
   `draftTestObjectsBuiltRouted`: the whole Draft test document built
   with routing on, 70/70 Proxies in the guest); BIM remains (7.6).
2. **G2** -- U1 + U2 + U7: Draft and BIM register from the guest; the
   subset shim.  SIZED 2026-09-05 (7.9): G2a the registration mechanism
   (the guest's `FreeCADGui`, the `gui.*` op family, stand-in commands,
   wrapped workbenches, gate `SandboxGui`), G2b the InitGui runner and
   the measurement of how far Draft's and BIM's `InitGui.py` get -- their
   `Initialize()` imports forms, the snapper and trackers before one
   toolbar, so the residue is G3's list.
3. **Probe A** -- PASSED 2026-09-05 (7.10): the Coin fork compiles with
   emcc under `COIN_BUILD_GL_STUB`, `pivy.coin` loads in the guest, a
   10 k-node graph builds in 0.77 s, boot grows by 0.2 s; real Coin it
   is.  **Probe B** -- PASSED 2026-09-05 (7.3): ipywidgets and
   traitlets bundled unchanged, the `comm` shim and an `IPython`
   stand-in (`fcx_widgets`), the host manager and twenty Qt views, a
   six-widget form shown as a task panel from a guest script; 0.13 s
   import, 0.5 ms per model, 0.22 ms per slider event, +0.02 s boot;
   gate `SandboxWidgets`.  **G3a** BUILT 2026-09-06 (7.11): the Qt
   subset as models, `loadUi` on both sides, the task panel, layouts
   from `.ui` files, `prefs.write`; Draft's OrthoArray panel runs
   unmodified in the guest; gate `SandboxForms`.  **H0** BUILT
   2026-09-06 (7.12: the host widget layer -- `src/Gui/Fw/`, the same
   22 classes in C++ over a property bag, the store of the guest's
   objects, the C++ Qt view, the `PanelDialog`, `fwuic.py`,
   `FreeCADGui.FormWidgets`; OrthoArray from the guest renders through
   it and the Python Qt-shaped views are gone).  **H1a** BUILT
   2026-09-06 (TaskOrientation onto the generated form, gate
   `test_taskOrientationPort`).  **H1b** BUILT 2026-09-06 (Pad/Pocket's
   form onto the generated models, the expression seam
   `Fw::ExpressionBound` on the `QuantitySpinBox`/`DoubleSpinBox`
   models; gate `test_expressionSeam` 13/13 and `SandboxNative`, in the
   default gate list since the `ViewAreaCell` teardown fix of
   2026-09-06).  **G3b** BUILT 2026-09-06 on the C++ store (7.11: the
   item views as rows with item ops, dialogs with `exec_()`, the
   containers, the file chooser; gate `SandboxPanels`, the 40 files).
   **G3c** BUILT 2026-09-06 (7.12: forms built in code -- the layout
   tree as `layoutSpec`, bars, actions, the main window shim, the key
   event stream; Draft's `DraftGui.py` unmodified in the guest, gate
   `SandboxDraftGui`).  **G2b BUILT** 2026-09-06 (7.9: the runner in
   `FreeCADGuiInit.py` under `InitGuiInGuest`, the re-run after a
   reset, the stand-ins stamped with their guest; Draft's and BIM's
   `InitGui.py` in the guest at startup, Draft's tool bars from the
   guest; gate `SandboxInitGui`).  **GuiUp flipped** the same day
   (the guest's is the host's; gate `SandboxCorpusGui`, the G1 corpus
   under the GUI): both `Initialize()`s complete, both workbenches'
   tool bars come from the guest; `Activated()` stops at the status
   bar and dock widgets, the 3D view (G4) and the selection (G3d)
   are next.
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
7. **G3** -- U3 over the widget protocol, sized in 7.11 as four stages:
   G3a BUILT 2026-09-06 (the Qt classes as models, `loadUi` both sides,
   the task panel, `.ui` layouts, `prefs.write`, `Gui.ActiveDocument`);
   G3b BUILT 2026-09-06 (dialogs with `exec_()`, the tree/list/table
   family as rows with item ops, containers, the file chooser; the
   40-file harness `SandboxPanels` is its gate); G3c BUILT 2026-09-06
   (forms built in code: layouts without a file, actions, tool bars,
   menus, the main window shim, the key event stream; DraftGui's tray
   and panel are the gate, `SandboxDraftGui`); G3d BUILT 2026-09-07
   (the selection input over the host's own Selection, observers as
   guest proxies, the four stock dialogs one op each; gate
   `SandboxSelection`).  **S1 BUILT 2026-09-07** (7.13): the session
   document -- a workbench (session or addon) reaches every open
   document, reach computed from the principal (`reachable` in
   `ExpressionImageBridge.cpp`); `FreeCAD.ActiveDocument` /
   `Gui.ActiveDocument` are the host's live one; `listDocuments` /
   `getDocument` under `app.query`, `newDocument` / `closeDocument` /
   `setActiveDocument` under the new `app.write`; `save` declared,
   `saveAs` on a picker-blessed path.  **S2 BUILT 2026-09-07** (7.13):
   `Gui.doCommand` / `doCommandGui` / `addModule` in the guest under
   the `gui.doCommand` enum value, one host op for the macro and audit
   lines; Draft's commit through `todo.doTasks` and BIM's `Arch_Site`
   run end to end from the guest; gate `SandboxSessionDoc` (9 cases).
   NEXT: the status bar / dock widgets, then G4 (ask first).
   **F1 -- the file and code chokepoints** (7.14, SIZED 2026-09-07, not
   built): `fs.read` / `fs.write` / `host.exec` checked inside the
   core's file and `runFile` primitives under the guest's scope, the
   host's own file dialog blessing the paths it returns; closes the one
   host-execution path a guest has (`Gui.runCommand("Std_RecentMacros")`
   under `gui`).  Order: the user's call; before N2 is the natural
   slot (N3's network rows share the mechanism).
   H0 (BUILT 2026-09-06) and H1 (7.12) come before G3b so G3b's views
   are written once, in C++; H2 and H3, the native ports, interleave
   with G3b-G3d as the class set grows.
8. **G4** -- the mirror: generated Coin models, the reader with its
   allowlist and quotas, host-scene query ops, stand-ins, the event
   stream.  Gate: the Draft test documents render identically (pixel
   compare on a converged scene).
9. **N2** -- `fetch` and the loop primitive.  **N3** -- network rows in
   the existing permissions panel, the addon manifest at install time.
   **N4** -- WebSocket (`net.ws:<origin>`), and `SOCKFS` under its own
   `net.socket` permission.
10. **G5** -- the snapper in C++, when measured to matter.  **G7** --
    the DOM walker over the widget layer in the browser tier (7.12;
    no Draft/BIM gate of its own): every panel, guest or native, in
    the browser from the same models.  The Qt-free manager over bgfx
    (RmlUi or imgui) is DROPPED, 7.4.
11. **Rung 1** -- generated C++ dispatch for `call` members; the
    App-core severance.  **Rung 2** -- per-document guests.
12. **N5 / G6 / the switch** -- `Python/Runtime = pyodide`, Draft and BIM
    with an empty GUI island, the full Draft and BIM suites green with
    routing ON, the browser drawing a Draft line end to end.

Not on the roadmap: a socket-level host capability, UDP, listening
sockets, any network for the reference image, a webview escape hatch.

## 12. Traps

- The GUI process hung in `exit()` once a 3D view refined a mesh
  (found 2026-09-06 under gdb, the corpus gate; FIXED 2026-09-07):
  `pthread_cond_destroy` on the static condition variable of
  `src/Mod/Part/Gui/MeshLevelSource.cpp` with its refine threads still
  waiting on it -- glibc's destroy blocks until every waiter has left,
  and a detached worker parked on a static never does.  The fix keeps
  the workers joinable and stops and joins them in
  `PartGui::shutdownMeshLevelWorkers()`, hooked to `aboutToQuit` and
  to a Qt post routine the first time a worker starts; the mutex and
  condition variable are leaked on purpose so an exit that never ran
  the hook cannot hang either.  A worker inside BRepMesh has no safe
  interruption point, so a build in flight finishes before the join.
  The rig no longer `os._exit`s.  `gdb -p` is refused on this box
  (ptrace scope): run the binary under gdb and send the inferior
  SIGINT.
- `Gui::ActionGroup::actions()` cached raw `QAction*` past deletion
  (fixed 2026-09-06): a group's action may belong to another command,
  and replacing that command left a dead pointer `Command::testActive`
  read on the next UI tick.
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
- The facade generator's XML list is read at CONFIGURE time (both
  guest trees and the main tree take their DEPENDS from
  `--list-xmls`).  A file that joins `ANNOTATED_XMLS` after a tree was
  configured is edited in vain: the table never regenerates, and the
  symptom is the guest reporting `'FeaturePython' object has no
  attribute 'changeAttacherType'` while the host table has it.  Since
  2026-09-04 the generator is a `CMAKE_CONFIGURE_DEPENDS` of both, so
  editing it reconfigures; before that, reconfigure by hand.
- A guest proxy's class is composed from the object's extensions when
  the handle is made.  An extension added by the guest itself
  (`addExtension` inside `__init__`) is invisible to that class until
  the `ext` op recomposes it -- which `addExtension` now does; any
  other host-side path that adds an extension mid-transaction would
  need the same.
- The guest's `math` is the runtime's libm: cos/sin can round one ULP
  from glibc (Polygon, 7.6).  Any gate that compares guest trig to
  native must compare in ULPs, not bytes.
- A value handed out by a handle is a COPY unless it is stamped: native
  FreeCAD's `obj.Placement.Base = v` works only because __getattro
  stamps the Placement as the object's attribute and `startNotify`
  writes it back.  The guest lost every such nested write until the
  proxies stamped what they hand out (3.2); a workbench that works
  natively and silently misplaces geometry in the guest is this shape
  of bug (ArchFrame, 2000 mm).
- `python-mode` through `evaluate()` is the EXPRESSION engine, not
  CPython: no `obj`, the owner is addressed by name (`Frame001.Base`),
  no comprehensions, member access through ObjectIdentifier.  It
  cannot exercise the stamping above; probe such things through a
  guest Proxy or the corpus gates.
- A module property that raises AttributeError reads as "module has
  no attribute": the guest's `FreeCAD.ActiveDocument` property maps a
  refused `get_attr` to RuntimeError so the reason shows.
- The Gui binary has NO `-t` mode: `FreeCAD -t Module` starts the GUI
  and sits there until killed (the console binary's FreeCADTest path is
  not run by `Gui::Application`).  A test that needs the GUI runs from a
  script FreeCAD executes at startup (`scripts/sandbox-gui-gate.py`),
  under Xvfb with `env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb` --
  `QT_QPA_PLATFORM=offscreen` crashes in `QOpenGLWidget` before any
  script runs.  Judge by the result file the script writes, not by the
  exit code.
- The Emscripten toolchain confines `find_library` to its sysroot: a
  cross project naming a library outside it (the wasm Coin archive)
  sets the path directly.
- The console binary does not flush `stdout` at exit: a probe script
  run as `FreeCADCmd script.py` that `print`s its facts loses them --
  write to stderr and flush.
- A host `activateWorkbench` whose Python `Initialize` raises shows a
  modal "Workbench failure" box: under Xvfb a hang until the timeout.
  Call a guest workbench's `Initialize` through `exec` when probing.
- The GUI gates run with `FREECAD_USER_HOME=/tmp/fchome`, where no
  pyodide runtime is installed: without `FCX_PYODIDE` pointing at one
  every sandbox case SKIPS ("the sandbox image did not boot") and the
  gate still says `RESULT OK`.  Read the case lines, not the verdict.
  The same under Xvfb: a document left open at exit is a save prompt,
  and a prompt is a hang until the timeout -- a probe closes its
  documents and dialogs before the main window.
- The guest prelude (`ImageMarshal.cpp`) and dispatcher
  (`ImageDispatch.cpp`) are compiled into the GUEST image: an edit
  needs `cmake --build build/pyodide-guest` (outside the conda env,
  docs/DevEnvironment.md) and the mirror step (`--target
  fcx_image_wheel` on the host), not a host rebuild -- the host build
  silently keeps the old wheel.  Since G3c the prelude delegates every
  `FreeCADGui` name it lacks to `freecad.widgets.gui.attr`, in the
  wheel the HOST build packs: a new name is a host build.
- The fork's sequencer holds a wait cursor until a poll queued on the
  event loop sees the sequence over, and the wait cursor's filter eats
  every key and mouse event: a test that recomputes and then sends
  keys or clicks without returning to the loop sees them vanish
  (SandboxNative's pad, then SandboxDraftGui's Enter).  Spin the loop
  until `QApplication.overrideCursor()` is None first.
- A `def` later in a class body wins over an earlier one of the same
  name: a stub left behind (`installEventFilter: pass`) silently
  replaced the real method above it.  The host-python harness of the
  guest package (a stubbed `_fcx`/`FreeCAD`/`FreeCADGui`/`draftutils.
  params`) shows such a thing in a second; the gate takes a minute.
- A tuple crosses the wire as a tuple (ipywidgets' `_options_labels`),
  a list as a list; compare with `list()` on the host when the guest's
  type is traitlets' choice.
- ONE recompute never meets a cached handle.  The handle table is
  cleared by every expression evaluation (`ExpressionEvaluator` ->
  `clearHandles()`), and a Spreadsheet's numeric cell IS an expression,
  so a Proxy that kept a document object on `self` (ArchReport) failed
  only on the SECOND routed recompute of the corpus.  A gate that
  recomputes once proves nothing about state kept across hooks; the
  corpus gates and the harness recompute twice (3.2, durable handles).
  And `clearHandles()` inside a nested round trip would drop the OUTER
  hook's live arguments -- it is a no-op while nested.
- A `call` op's arguments (`"a"`) cross as ONE wire-encoded list, not
  a JSON array: a host-side check that reads `req["a"][0]` as a JSON
  string sees nothing (the `saveAs` blessed-path check did, 2026-09-07:
  every path was "''").  Decode with `decodeHostValue` first, as
  `callWithWireArgs` does.
- Any command calling `doc.copyObject` aborted with "still being
  filled in" (fixed 2026-09-07, `Gui::Application::refreshLiveLoad`):
  the import reads its fragment through the restore path, which sets
  the Restoring bit, and the live-load claim took that for a load the
  user was watching -- so the copied object's view provider attach
  (a write of `<copy>.ViewObject`) tripped `checkUserEdit` inside the
  command.  Restoring WITH Importing is an import into an open
  document and is not claimed now.  Found by `Draft_Heal` run from the
  guest (7.13, S1); the native command failed the same way.
- A gate reading guest state through a DOCUMENT principal needs that
  principal's document open: after closing every document (the "None
  with nothing open" case) the owner is dead, its scope is the session,
  and the read prompts for `host.import` -- make a fresh document for
  the reads.  And a document's `Name` after `closeDocument` raises
  natively too: keep the name before closing.

## 13. Known gaps and open questions

- **`FreeCAD.GuiUp` in the guest: RULED 2026-09-06, the host's
  value** ("flip it"; 7.9).  What it costs until G4, measured by
  `SandboxCorpusGui`: an App-side hook calling its VIEW PROVIDER'S
  PROXY methods (BuildingPart's `ViewObject.Proxy.onChanged` -- 3
  objects invalid when routed under a GUI; Layer's
  `change_view_properties`, an error line) -- a host Python object's
  method, undeclared by design, until the view providers live in the
  guest; and a document guest importing a module whose GuiUp branch
  builds widgets (`ArchRebar` -> `bimcommands` -> `DraftTools` ->
  DraftGui's tray): refused, Rebar's Proxy stays native.  A document
  principal's widget traffic is not deferred (7.9).  Draft's
  preference observer (`_param_observer_start`) is a declared no-op:
  no change notification crosses to the guest yet, so a preference
  edited on the host does not refresh the guest's tray or grid.
- **Widgets in the status bar and dock widgets** (`statusBar().
  findChild`, `addStatusBarItem`, `BimStatus`, `init_draft_statusbar`,
  `QDockWidget` for BimViews): not in the widget layer; Draft's
  `Activated()` and BIM's stop there (7.9).  G3d or G7 material.
- **`Gui.runCommand` from a guest runs any host command under `gui`**
  (7.14, sized): `Std_RecentMacros`, `Std_RecentFiles` and
  `Std_DlgMacroExecuteDirect` run a file or open one with no picker,
  so a session or an addon reaches host Python and host files by name.
  A document principal cannot (`gui` DENY, not promptable).  The answer
  is F1, the chokepoints at the primitives, not a list of names.
- **A guest view provider's scene is G4's** (7.13, S2 built note):
  `Arch.makeSite()` from the guest builds `_ViewProviderSite` in the
  guest, whose `attach`/`onChanged`/`updateData` reach `Annotation`,
  `RootNode` and `SwitchNode` -- Coin nodes, the mirror -- and print
  an AttributeError each from the host's hook call (the object and
  the document are right; the terrain switches are not built).
  `Gui.ActiveDocument.ActiveView` in the guest is the active-object
  REGISTRY only (`getActiveObject`/`setActiveObject`); every other
  view member is an AttributeError naming G4.  (`Gui.doCommand`
  itself, S2, is BUILT 2026-09-07; the session document is S1.)

- Document OPEN imports, before any expression runs.
  `PropertyPythonObject::Restore`'s `PyImport_ImportModule` on a
  document-chosen module name is CLOSED for document objects with
  routing on (G1c step (f), 2026-09-04): the module is imported in the
  guest, the instance allocated there, the property holds the stand-in,
  and a module the guest cannot serve fails closed (3.5, 7.6).  Still
  open: the same import for a VIEW PROVIDER's Proxy (`Gui::ViewProvider`
  container, no document object -- native until the Gui side is in the
  guest, G2).  CLOSED 2026-09-05, natively: `Base::Type::importModule`'s
  type-string import (a saved property type `Foo::Bar`, a
  PropertyPersistentObject) was a plain `PyImport_ImportModule` of
  whatever name stood before the `::` -- stdlib and site-packages
  included.  User ruling: "restrict it natively, only Mod directories or
  already loaded modules".  It now imports only a module already in
  `sys.modules` or one `importlib.util.find_spec` resolves into a
  registered module root (`Type::addModuleRoot`: the installation's
  `Mod`, the user's `Mod`, the macro directory's `Mod` and every
  `--module-path`, registered by `Application::initApplication` before
  the init script -- the same directories FreeCADInit puts on
  `sys.path`); anything else throws `Base::RuntimeError` without
  importing, and a module that exists nowhere fails with the
  interpreter's own error as before (the callers log it).  Gate:
  `TypeImport.*` in Tests_run (six cases: under a root, on `sys.path`
  outside every root, stdlib, already loaded, missing, core prefixes).
- What BIM's App side cannot do in the guest (the corpus gates' list):
  `ArchSchedule`'s IFC branch imports `nativeifc` (ifcopenshell) --
  nothing of it is in the guest.  CLOSED 2026-09-05: the four writers
  to objects other than the owner (`ArchStairs` makes and writes its
  railing objects, `ArchPipeConnector` trims the pipes it joins,
  `ArchSchedule` and `ArchReport` fill and recompute their Result
  sheet) are same-document writes under the revised `doc.write.self`
  (3.2, 7.6 decision 1); and the one parameter WRITE a recompute made
  (Arch areas, hatches, tessellation toggling TechDraw's
  `allowCrazyEdge` around a projection, ignored in the guest so a
  section of an edge over 10 m differed from native) is gone --
  `TechDraw.findShapeOutline`/`makeGeomHatch` take `allowCrazyEdge=True`
  as a keyword, a scoped `DrawUtil::CrazyEdgeAllowance` on the host,
  and the preference is never written by document code, natively
  either.  A nested write-back's refusal is silent, as it is natively:
  a guest `obj.Placement.Base = v` on a foreign object raises inside
  `startNotify`, which clears it.
- CLOSED 2026-09-05: a Proxy that keeps a document object on `self`
  across hooks (ArchReport's `self.spreadsheet`, ArchSchedule's, the
  `self.obj = obj` of several `onDocumentRestored`s) held a HANDLE of
  the transaction that minted it and the next hook found it stale.
  Durable document-object handles (3.2: a `[document, name]` key on
  every DocumentObject and Document handle, guest-side re-resolution on
  a stale id through `resolve`, `__eq__`/`__hash__` by key, re-resolution
  of a stale argument while decoding) fix it, and the corpus gates
  recompute twice.  Still transaction-scoped: a VALUE object kept across
  hooks (a shape, a curve) -- natively a copy, in the guest a
  `ReferenceError` on the next hook; no corpus object does it.
- Every GUI registration from the guest (7.9) runs as `session`: the
  addon principal for a workbench's guest code, and how a stand-in
  carries it for the hooks the host calls later, is G2b's.
- `getMainWindow()` is a shim (G3c): `addToolBar`, `mainWindowClosed`,
  `showMessage`, `cursor()` -- its document windows (`getActiveWindow`,
  94 corpus sites, `getWindows`, `setActiveWindow`) are the mirror's
  (G4); `QDockWidget` and `addStatusBarItem` are not in the subset.
- The forms (G3a, 7.11): a guest InputField's `text` is the guest's
  `Quantity.UserString` ("120 mm"), the host's its own decimals
  ("120.00 mm") until a host edit sends the host's -- the corpus parses
  the text, never compares it; a code-built widget's `sizeHint()` and
  `QFontMetrics` are estimates (G3c), `hasFocus()` follows the focus
  events the host relays, a `QTimer` fires once at the next drain and
  never repeats, `QMenu.popup()` blocks like `exec_()`, a `QPainter`
  records polygons, rectangles, ellipses and lines only (no text, no
  images); `.ui` strings are untranslated in the guest
  (`translate` is the identity there) while uic's are translated on
  the host -- a guest `setText(translate(...))` therefore shows the
  English; a widget re-added to a layout it is already in duplicates
  the item, as Qt does; `Gui.ActiveDocument` carries `resetEdit` and
  `Document` only.
- The GUI live expression editors evaluate as session, unconfined.
- No memory ceiling for a guest.
- Addon principal granularity (per addon, per file?) is still open.
- Whether a document network grant may ever be "always" (a
  content-hashed identity makes it safe against tampering; a
  long-lived grant is still a long-lived channel).
- The PyPI resolver: host-side against PyPI's JSON API, or micropip in
  the guest with a host-fed index.
- Whether the mirror reader is the widget manager with a second model
  family (Probe A answered real Coin, Probe B built the manager);
  widget-protocol versioning between a shipped guest wheel and an older
  host (the manager refuses a newer major; today it reads no version);
  the browser-only tier's rendering of the mirrored subset.
- The forms' wheels are BUNDLED (5.6), not in the user's package set
  as 7.3 first said: ipywidgets is not in the pyodide lock and there is
  no PyPI source yet (roadmap 6), and a form library the workbenches
  depend on should ship with the FreeCAD that renders it anyway.  Open:
  whether the `IPython` stand-in should yield to a real IPython a user
  installs later: the bundled wheels load before the package set, so
  a real IPython's files would land over the stand-in's in the guest's
  site-packages -- untested, and it would bring the 1.5 s import back.
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
  (the in-canvas route these were surveyed for is dropped, 7.4; the
  viewer-side survey is `docs/ViewerUIResearch.md`, the browser
  chrome ruling `docs/ThinClient.md`)
- Deno permissions: https://docs.deno.com/runtime/reference/permissions/;
  Node permission model: https://nodejs.org/api/permissions.html;
  Spin outbound HTTP: https://spinframework.dev/v3/http-outbound;
  pyodide.http: https://pyodide.org/en/stable/usage/api/python-api/http.html;
  urllib3 emscripten: https://urllib3.readthedocs.io/en/stable/reference/contrib/emscripten.html;
  Emscripten networking: https://emscripten.org/docs/porting/networking.html
- pyodide releases: https://github.com/pyodide/pyodide/releases;
  v8-embed: `~/works/sw/v8-embed-feedstock`
