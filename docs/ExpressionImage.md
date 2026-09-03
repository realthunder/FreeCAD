# The expression sandbox image (Phase 1 step 4 build notes)

> **Superseded 2026-09-03 by `docs/Sandbox.md`**, the consolidated
> sandbox reference audited against the code. This file is kept as the
> historical record; `Sandbox.md` sec 14 says which of its sections
> moved where and which statements here are stale. Do not update this
> file; update `Sandbox.md`.


**Reference implementation since 2026-09-03.**  The wasm32-wasi image
under wasmtime described here is no longer the shipping runtime: the
user's direction of 2026-09-03 (`docs/SandboxNetwork.md` sec 0) makes
pyodide (`docs/PyodideHost.md`) THE runtime and keeps this one as the
reference -- the smallest confinement the `ImageHost` seam can carry,
built only with `BUILD_EXPR_WASI_RUNTIME` (default OFF), fixed when it
breaks, never extended.  Everything below is the record of how it was
built and measured, and stays valid for that build.

Companion to docs/ExpressionSandbox.md (the design; secs 6, 7.4, 7.6)
and docs/ExpressionSandboxPhase0.md (the frozen contracts).  This file
records what step 4 has BUILT so far, the exact toolchain, and how to
reproduce it on this box.  Status 2026-08-30: the Ring 0 slice is
proven -- CPython 3.12.13 plus the Base math bindings (Vector,
Rotation, Placement, Matrix, BoundBox, Quantity, Unit) compiled to a
wasm32-wasi reactor, evaluating expressions under wasmtime with C++
exceptions working end to end (a bad unit string inside
`Units.Quantity(...)` throws Base::ParserError in-image and comes back
as a Python error, not a trap).  The CBOR wire is live for the
host->image direction: `fcx_call` dispatches `eval {src, bindings}`
with the full Phase 0 sec 6.4 value encoding (FcxWire.h /
ImageMarshal.cpp; verified: typed vector/quantity bindings round
trip, bool stays bool, a lambda result refuses with MarshalError per
sec 7.7, handles pass through, exceptions cross as {exc, msg}).
The image->host bridge ops are live too (see "The bridge" below):
live host Python objects cross as per-transaction handles, and
get_attr/call/get_item/len on them round-trip through the host with
per-op permission checks.  ExpressionCore is IN the image as of
2026-08-30 (see "The core carve" below): the real ExpressionParser +
AST walker + ObjectIdentifier compile into the image behind the S1
adapter, evaluate `{lang:"expr"}` requests against the bindings pack,
and produce byte-identical results and error messages to the native
engine (verified: quantities, pack identifiers, range aggregates,
_math, ParserError texts, even the min(1,2)==1.2 comma-decimal
quirk).

## Toolchain (sibling dirs, exact versions matter)

- `~/works/sw/wasi-sdk-33.0-x86_64-linux` -- wasi-sdk 33.  This is
  the FLOOR: 33 is the first release whose sysroot ships the
  exceptions-enabled C++ standard library (`lib/wasm32-wasi/eh/` with
  a real `__cxa_throw` and `libunwind.a`; see CppExceptions.md in the
  wasi-sdk repo).  wasi-sdk 25 was tried first and CANNOT link the
  slice -- its libc++abi has no EH runtime.  wasi-sdk 34 drops the
  `wasm32-wasi` target name that CPython 3.12's build scripts use, so
  33 is also currently the ceiling for the CPython side.
- `~/works/sw/wasmtime-v48.0.1-x86_64-linux` (CLI) and
  `~/works/sw/wasmtime-v48.0.1-x86_64-linux-c-api` (headers +
  libwasmtime for the host embedding).  The wasm exception-handling
  proposal must be enabled explicitly: CLI `-W exceptions=y`, C API
  `wasmtime_config_wasm_exceptions_set(cfg, true)`.
- `~/works/sw/cpython-wasi/Python-3.12.13` -- CPython source, version
  matched to the host interpreter (3.12.13) per the migration
  constraint (ES sec 11: same C++ evaluator against the same CPython).
  Built with:

      cd ~/works/sw/cpython-wasi/Python-3.12.13
      WASI_SDK_PATH=~/works/sw/wasi-sdk-33.0-x86_64-linux \
      PATH=~/works/sw/wasmtime-v48.0.1-x86_64-linux:$PATH \
      MAKEFLAGS=-j6 python3 Tools/wasm/wasm_build.py wasi

  Outputs land in `builddir/build` (native bootstrap, reused across
  SDK switches) and `builddir/wasi` (`python.wasm`, `libpython3.12.a`,
  `pyconfig.h`, and the private static libs the final link needs:
  `Modules/_decimal/libmpdec/libmpdec.a`,
  `Modules/_hacl/libHacl_Hash_SHA2.a`, `Modules/expat/libexpat.a`).
  The ssl module does not build -- correct, not a problem: the image
  has no network by construction (Ring 2 is absent).

## Building the image

`src/App/ExpressionImage/` is a STANDALONE cross project (same pattern
as the wasm viewer): configure command is in its CMakeLists.txt
header.  On this box:

    cd ~/works/sw/fcad
    .conda/run.sh env CFLAGS= CXXFLAGS= LDFLAGS= cmake -B build/wasi-image -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$HOME/works/sw/wasi-sdk-33.0-x86_64-linux/share/cmake/wasi-sdk.cmake \
      -DWASI_SDK_PREFIX=$HOME/works/sw/wasi-sdk-33.0-x86_64-linux \
      -DCPYTHON_WASI_SRC=$HOME/works/sw/cpython-wasi/Python-3.12.13 \
      -DFREECAD_GENERATED_DIR=$PWD/build/conda-relwithdebinfo-801/src \
      -DBOOST_INCLUDE_DIR=$PWD/.conda/freecad/include \
      src/App/ExpressionImage
    .conda/run.sh env CFLAGS= CXXFLAGS= LDFLAGS= ninja -C build/wasi-image

Then point the host build at the result so it installs the image and
mirrors it into the build tree (see "The packaging slice"):

    cmake -B build/conda-relwithdebinfo-801 \
      -DFREECAD_EXPR_IMAGE_DIR=$PWD/build/wasi-image/desktop

The project defaults `CMAKE_BUILD_TYPE` to Release; do not configure it
away.  With an empty build type every TU we compile lands at -O0 while
CPython stays -O3, and the round trip measured 3-6x slower (see "The
evaluation switch-over" below).

The `CFLAGS= CXXFLAGS= LDFLAGS=` scrub matters: the conda env exports
x86 flags (`-march=nocona ...`) that break the cross compile.
`FREECAD_GENERATED_DIR` reuses the host build's generated `*Py.h/cpp`
-- the generator output is target-independent, and the image compiles
the same `*PyImp.cpp` TUs the host does.

Key flags (set by the CMakeLists, recorded here because they are
non-obvious):

- C++ TUs: `-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false` --
  selects the `eh/` sysroot variant of libc++/libc++abi; link adds
  `-lunwind`.  C TUs (CPython, PyCXX's cxxextensions.c) stay plain.
- Link: `-mexec-model=reactor` -- the image is a library-style module
  exporting `fcx_*`, with `_initialize` run once by the host.
- `-DFC_NO_QT` (new, introduced by this work): guards the QString
  helper block in Base/Tools.h.  `__wasi__` additionally selects
  FC_OS_WASM in FCConfig.h and a no-op mutex in Base/Console.h.
- `-DBOOST_DISABLE_THREADS -DBOOST_SYSTEM_DISABLE_THREADS`: wasi
  libc++ is single-threaded (no std::mutex); boost.system's spinlock
  header needs its own macro, not just the boost.config one.

## What is real and what is stubbed

Compiled REAL into the image: Type, BaseClass, Vector3D, Rotation,
Matrix, Placement, DualQuaternion, Quantity (with the bison/flex
quantity parser), Unit, Exception, PyObjectBase, GeometryPyCXX, the
seven `*PyImp.cpp` binding TUs, and PyCXX.

`ImageStubs.cpp` stubs, and their honesty notes:

- ConsoleSingleton (ctor/dtor/Instance/Destruct/postEvent/
  notifyPrivate/GetLogLevel/Refresh) and LogLevel::prefix: diagnostics
  go to stderr, which the host owns.  No observers, no Qt event
  posting.
- UnitsApi::getDecimals (2) and UnitsApi::schemaTranslate: the image
  always speaks the INTERNAL scheme -- factor 1.0, the unit's
  signature string.  Quantities cross the wire by value (Phase 0 sec
  6.4), so user-facing formatting happens host-side; this stub only
  shapes an in-image `.UserString`, which real expressions in the
  corpus do not consult.
- TracePySrc: counter only.

## Smoke procedure

`tools/smokehost.c` (build/usage in its header).  Positive checks:
Vector cross product, `Units.Quantity('10 mm') + Units.Quantity('1
cm')` -> `'20.0 mm'`, Placement rotation, Matrix multiply.  Negative
checks (all must fail INSIDE the image, as `E:` replies or Python
errors, never reaching the host): `open('/etc/passwd')` ->
FileNotFoundError (only /Lib is preopened, read-only);
`socket.socket()` -> OSError 58; `import ctypes` ->
ModuleNotFoundError (not in the image); `subprocess.run` -> OSError
(WASI cannot spawn); `Units.Quantity('garbage')` -> `E:syntax error`
(C++ throw crossing the parser, caught at the binding).

Startup cost observed: ~0.55-0.7 s per cold instantiation, almost all
of it wasmtime JIT-compiling the 32 MB module.  MEASURED FIX:
`wasmtime compile -W exceptions=y` produces a 23 MB .cwasm in 1.35 s
(once per image build), after which instantiate + full fcx_init
(CPython up, FreeCAD module registered) is **19 ms** -- a 30x cut
that confirms the Phase 0 zygote verdict.  The host embedding should
deserialize a cached .cwasm (wasmtime_module_deserialize / the
engine cache) keyed on the image hash; per-principal warmed
instances remain the plan for recompute-time cost.

## The bridge (image->host ops, built 2026-08-30)

The mid-eval reach-back when sandboxed code touches a live host
object.  Wire shapes in `FcxWire.h`; both directions CBOR.

- **Transport**: the image imports `fcx.host_call(req,len)->reply_len`
  and `fcx.host_fetch(dst,cap)->copied` (two-call size-then-fetch: the
  host must never re-enter the guest to allocate the reply, so it
  parks the bytes and the guest fetches them into its own malloc'd
  buffer).  Both return -1 on transport failure.  Host side:
  `ImageHost::Private::hostCallCb/hostFetchCb`, registered on the
  wasmtime linker under module "fcx".  `tools/smokehost.c` satisfies
  the imports with -1 stubs ("host bridge unavailable" in-image);
  `wasmtime compile` needs no imports, so .cwasm precompile is
  unaffected.
- **Image side** (`ImageBridge.cpp`): the `_fcx` module wraps one op
  as `_fcx.op(name, id[, a[, k]])`, wire-encoding the extras with the
  existing ImageMarshal encoder.  The `HostHandle` proxy
  (ImageMarshal.cpp) forwards `__getattr__`/`__call__`/`__getitem__`/
  `__len__` through it, and `__del__` sends `release` -- so handles
  free themselves when the eval globals die, and iteration works via
  the `__getitem__`+IndexError sequence protocol.  Error replies
  re-raise the named builtin when one exists (PermissionError,
  AttributeError, IndexError...), else RuntimeError.
- **Host side** (`ExpressionImageBridge.{h,cpp}`, host-only):
  `HandleTable` (uint64 -> owned PyObject*, per transaction --
  `ImageHost::exportObject/clearHandles/handleCount`), the host
  marshal (mirror of the image's, except non-marshalable objects
  become NEW handles rather than errors -- a chained
  `o.child().name` works), and `dispatchHostOp`.  Every op resolves
  permissions BEFORE touching the object: get_attr/read_prop through
  `checkGetattr` (module results re-enter the import gate), call
  through `checkCallablePermission` (bound doc-object methods ->
  app.query, Base-bound -> geom.call).  PermissionNeededException
  crosses as PermissionError; a stale id as ReferenceError.
- Gtests: 7 `ExpressionImageBridgeTest` cases (same FCX_IMAGE /
  FCX_STDLIB skip rule), including the deny -> grant -> works cycle.

TRAPS hit building this:
- **The v1 catalog defaults ADDON principals to ALLOW across the
  board** (trusted at install time).  A permission test must use a
  document principal (`"document:sha256:" + 64 chars`) -- an addon
  principal exercises no gate, and unsafe.getattr for documents is
  DENY, session PROMPT.
- **Two nlohmann copies**: FreeCADApp compiles against the vendored
  3.11.2 (`src/3rdParty/json/single_include`), the conda env carries
  3.12.0 as `-isystem`.  The versioned ABI inline namespace
  (json_abi_v3_11_2 vs ..._3_12_0) makes any exported function with
  json in its signature (ExpressionImageBridge.h) an undefined
  reference for a consumer that picked the other copy.  Tests_run now
  adds the vendored dir as a plain -I (beats -isystem).  CBOR byte
  vectors cross the boundary fine either way.

## The core carve (built 2026-08-30)

ExpressionCore -- Expression.cpp, ObjectIdentifier.cpp, Range.cpp plus
the in-tree generated parser -- now compiles into the image, selected
by `-DFC_EXPR_IMAGE` (set only by this project's CMakeLists).  The
mechanics:

- **The S1 adapter** (`FcxDocument.{h,cpp}`): image-only classes NAMED
  `App::Document` / `App::DocumentObject` / `App::Property` (+String/
  Bool/Float/Integer/Quantity, PropertyContainer, a GetApplication
  shim, a PropertyLinkBase string-helper stub), carrying exactly the
  ~15-method surface the core dereferences.  Because the names match,
  the ObjectIdentifier.h seam typedefs are untouched and the 43-file
  include blast radius stays zero.  The classes are real
  Base::BaseClass types (freecad_dynamic_cast works; `Fcx::
  initCoreTypes()` is the image's Application::initTypes, run from
  fcx_init, which also registers the `Base.*` Python exception types
  -- without those, Base::Exception::setPyException raises a null and
  every C++ error degenerated to "'' object is not callable").
- **The bindings pack rules resolution** (`Fcx::EvalTransaction`):
  `dispatchEval` with `{lang:"expr", src, ctx:{doc,obj}, owner_h?,
  bindings:{identifier-string: wire value}}` installs a transaction;
  ObjectIdentifier::getPyValue/getValue consult the pack FIRST (key =
  toString(), which both sides compute from identically-parsed
  components), so pre-resolved identifiers cost zero crossings.  On a
  miss the real resolve()/access() run against the adapter world:
  in-image pseudo-modules (_math/_re/_coll/_py, _app -> the in-image
  FreeCAD module) work, `Fcx::DocumentObject::getPropertyByName` is
  pack-backed (keyed via ObjectIdentifier(this,name).toString(), with
  a reentry guard -- toString resolves, resolve calls
  getPropertyByName), which is what makes `sum(A1:A3)`-style range
  aggregates work, and anything else fails with the native error
  message.  The owner's getPyObject() is the HostHandle proxy for
  `owner_h`, so `_self`-style drill-down rides the permission-checked
  bridge ops.
- **Host-only code behind the seam**: security chokepoints compile to
  inline no-ops via ExpressionSecurityRuntime.h's FC_EXPR_IMAGE branch
  (enforcement is host-side at the bridge per ES sec 3.4);
  DocumentObjectPy/ExpressionPy type checks stub false; the
  maintenance overrides of VariableExpression/RangeExpression are
  compiled out of ExpressionParser.h (base no-ops apply; definitions
  live in the host ops TU); function-object values (dbind/lambda
  wrappers = ExpressionPy) throw "not supported in the sandbox image";
  Sequencer().checkAbort is out (wasm fuel is the backstop).
  ImageStubs.cpp gained a lean Base::PyException (fetch-and-clear,
  no PyTools/ExceptionFactory).
- **resolve_alias**: RangeExpression::getRange's alias branch is
  seamed to `Fcx::resolveAlias` -> the dedicated bridge op on the
  owner handle (`FcxImage::hostOp` exposes the C++ transport); the
  host dispatcher answers it by calling getCellFromAlias under
  doc.read.self, NOT as a permission-gated app.query call.
- **Host producer**: `ImageHost::evalExpression(owner, src)` parses
  host-side, enumerates identifiers via getIdentifiers(), resolves
  each with getPyValue(true) under the owner's principal scope
  (PermissionNeededException at pack time fails the evaluation as
  PermissionError -- the foreign-doc grant cycle gtest covers
  deny -> grant -> works -> deny), marshals values-or-handles with
  encodeHostValue, exports the owner handle, ships the request.  A
  host-side parse failure ships anyway: the image raises the
  identical ParserError.
- Smoke tooling: `tools/wiretest.cpp` grew `[lang [doc obj]]` args
  and the -1 bridge stubs.

Traps hit: the core TUs relied on transitive host includes for
std::unordered_set / boost::hash / CStringHasher / FC_STATIC (now in
FcxDocument.h for the image); the parser statics compile fine
single-threaded; PropertyContainerPy's notifier attach is guarded out.

## The generated facades (built 2026-08-31)

The closed member set is now GENERATED from the binding XMLs
(docs/ExpressionSandbox.md sec 7.5), replacing the hand-written getattr
path.  One generator, two outputs, so host and image cannot drift:

- `src/Tools/bindings/generateSandboxFacades.py` reads the
  `<Sandbox tier="value|handle|call"/>` annotation on `<Attribute>` /
  `<Methode>` elements in a fixed list of binding XMLs (ANNOTATED_XMLS:
  ComplexGeoDataPy, DocumentObjectPy, DocumentPy, TopoShapePy, SheetPy).
  A member with no annotation does not exist across the boundary -- DENY
  by default.  Seeded from Phase 0 sec 3.3: 14 members (Name/FullName/
  Document/BoundBox/CenterOfGravity/Placement/ShapeType/Length/Area/
  Volume reads, Document.getObject / TopoShape.isNull /
  Sheet.getCellFromAlias calls).
- Host: `--host-out FcxDispatch.inc` -> a `FacadeMember` table
  `#include`d in ExpressionImageBridge.cpp.  `facadeMemberLookup` walks
  the object's tp_mro; `get_attr` for an unlisted member is a
  ProtocolError, never a getattr.  `read_prop` is answered from the C++
  property system (`getPropertyByName`), never host Python -- this is
  how dynamic properties (not XML members) stay reachable while
  arbitrary attributes do not.  `call` is member-addressed
  (`{"m": <member>}`) and only a declared call-tier member is invocable.
  Every op still passes the sec 3 permission check
  (checkGetattr/checkCallablePermission) after the table check.
  Generated in src/App/CMakeLists.txt under BUILD_EXPR_IMAGE_HOST.
- Image: `--image-out FcxFacades.inc` -> a Python source literal
  `#include`d in ImageMarshal.cpp, exec'd over a hand-written prelude
  (HostHandle + `_attr`/`_method` helpers).  A handle wire value carries
  a `"fc"` facade key; decodeValue instantiates the matching proxy
  class.  Declared attrs forward `get_attr`; `__getattr__` falls through
  to `read_prop`; there is NO `__call__` (an undeclared callable that
  crossed as a handle is inert).  A bound declared method crosses as its
  base handle plus `"m"`, resolved to the facade's bound method in-image
  so pack-resolved method identifiers stay callable.  Generated in
  src/App/ExpressionImage/CMakeLists.txt.

Security review of the whole bridge is now "diff the annotations":
`git log -p` on the five XML files.

## The acceptance harness (built 2026-08-31)

ES sec 10, in ExpressionImageAcceptanceTest (tests/src/App/
ExpressionImageHost.cpp): a real `.FCStd` is saved with a stored
engine expression, closed, reopened (so it carries a real
document:sha256 principal), and hostile expressions are run against the
image.  `_py.open('/etc/passwd')` is blocked by the image's OWN engine
(CallableExpression::securityCheck) before WASI is even reached;
`_py.__import__('subprocess')` likewise; `_app.getDocument(...)` finds
no document graph in the Ring 0 module; `_self.recompute` (a
drill-down past a plain property) is unsafe.getattr DENY for the
document principal; and a foreign-document reference runs the full
grant -> works -> revoke -> fails cycle end to end.

## What step 4 still owes (in order)

1. RESOLVED 2026-08-31 by measurement at the switch-over, not by
   building anything -- see "The residue, measured" below.  Writing/
   setPath, LinkPlacement/LinkMatrix accumulation and getSubObject
   walks all evaluate identically through the router today, because
   pack-first pre-resolves them on the host.
2. Ring 0 pseudo-modules (`_math`/`_re`/`_coll`/`_py`/`_app`) are now
   skipped from the host bindings pack (ExpressionImageHost.cpp) so they
   resolve IN the image -- `_py.open` must mean the image's builtins,
   not ours.  The seam list to widen the facade set is the XML
   annotations; no code change is needed to add a member, only an
   annotation + rebuild.

## Carve audit for step 3 above (measured 2026-08-30)

The complete document-world surface the core TUs touch (grep audit,
confirming Phase 0 sec 2's concentration claim):
ObjectIdentifier.cpp -- owner/obj: getDocument, getNameInDocument,
isAttachedToDocument, getPropertyByName, getPyObject, getSubObject,
getLinkedObject, getFullName, isExporting; doc: getName, getObject,
getObjects; App: GetApplication().getDocument/getDocuments.
Expression.cpp adds only five owner-> / getPropertyByName /
GetApplication sites of its own.  An image-side adapter of ~15
methods (ExpressionDocumentT/ObjectT/PropertyT rebound to Fcx*
classes over the bindings pack + bridge ops) covers the whole carve.

## The browser tier (built 2026-08-31)

Phase 2 of docs/ExpressionSandbox.md sec 10 -- the same image, running in
the browser.  The desktop and browser hosts differ in exactly two places
(the WASI implementation and the bridge), and both differences are
recorded here.

**It runs.**  Measured in Firefox 136 on this box, against the packed
bundle: fetch 128 ms, compile+instantiate 268 ms, `fcx_init` 31 ms, and
140 us per expression round trip (parse + evaluate + CBOR both ways).
Those are -O0 IMAGE numbers (see "The evaluation switch-over" below):
the same measurement on the Release image has not been re-taken, and the
desktop equivalent fell 3-6x when it was.
The full Ring 0 set evaluates -- arithmetic, quantities, engine
functions, `_math`/`_py`/`_coll`/`_re` -- and every confinement negative
behaves as it does under wasmtime.

**The payload.**  The image as built is 39.4 MB, of which 28 MB is DWARF
plus the name section; a browser reads none of it.  Stripped it is
10.9 MB (3.1 MB gzipped).  The stdlib is the surprise: the image opens
**16 CPython files**, 320 KB, because almost the whole stdlib is frozen
into the interpreter.  `tools/pack_image.py` produces both -- it
strips the custom sections itself (the operation is four lines of the
binary format, so the packer needs no toolchain) and copies the measured
file list -- and runs as a POST_BUILD step of the image, into
`build/wasi-image/web`.  `scripts/fcx-web-stage.sh` copies that next to
the viewer.  The file list is explicit rather than discovered, so it can
go stale; the acceptance page is what catches that, by evaluating through
every pseudo-module.

**The 16 files exist because of one commit.**  `ImportModules::
checkCallable` used to import `inspect` eagerly and fail the check if the
import failed -- pulling in dis/opcode/ast/tokenize/linecache and their
closure, megabytes, to answer a question `__module__` had usually already
answered.  It is now imported on first use of the two fallback paths that
need it (1a521bf0d4).  Absence degrades to "no module name", which is the
existing "unknown module" DENIAL, so the change is fail-closed.

**WASI is reimplemented, not reused.**  `web/src/sandbox/wasi.ts` is the
browser's confinement boundary the way wasmtime's WASI is the desktop's.
It serves one read-only preopen (`/Lib`) out of an in-memory tree and
implements nothing else: no network, no spawning, no writable file, no
host filesystem.  The refusals are absences, not policy checks.  `..` in
a path is refused outright rather than normalized -- a resolver that
never ascends cannot be tricked into ascending.  Because it is a second
implementation of the boundary, the confinement negatives are re-run
against it in a real browser (below) rather than argued from the desktop
result.

**The bridge is NOT attached in the browser, on purpose.**  `fcx.host_call`
is a SYNCHRONOUS import, called from inside the C++ evaluator mid-
expression.  On the desktop the host answers in the same thread.  In the
browser the thing worth reaching is the FreeCAD host at the far end of a
WebSocket -- asynchronous -- and a synchronous wasm import cannot await a
promise on the main thread.  So the browser host leaves the import
returning -1 and the image raises "host bridge unavailable" for anything
needing a live host object.  This is the pack-first design working as
intended (sec 7.3): the host pre-resolves every identifier into the
bindings pack before evaluation starts, so an expression over resolved
values never reaches back.  True reach-back needs the image on a worker
with `Atomics.wait` over a SharedArrayBuffer (hence COOP/COEP cross-origin
isolation) or JSPI; neither is built, because nothing needs it yet.
! Do not "fix" this by making the bridge fake a synchronous answer from a
cache -- a stale answer mid-evaluation is a correctness bug that the
error is protecting against.

**Browser requirement: the exnref exception-handling proposal.**  The
image is built with `-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false`,
i.e. the standardized exnref EH, which is also the only flavour wasmtime
implements.  Firefox 131+ and Chrome 137+ ship it.  Measured on this box:
Firefox 136 compiles the image in 277 ms; Chrome 123 REJECTS it
("invalid value type 'exnref'") and needs
`--js-flags=--experimental-wasm-exnref`, after which it compiles in 27 ms
(lazily).  If a wider floor is ever needed, the fallback is a second image
built with legacy EH for browsers only -- but that is two binaries to
verify, and the wasi-sdk 33 `eh/` sysroot would have to be checked for
legacy support first.  Not done, not needed yet.

**The acceptance page.**  `web/public/sandbox-test.html` +
`web/src/sandbox/{acceptance,testmain}.ts` -- the browser twin of
ExpressionImageAcceptanceTest.  It evaluates the positives, runs the
confinement negatives (including a read-only-filesystem write, which
comes back EROFS from the shim), checks that the unattached bridge fails
cleanly, and times a round trip.  `?post=<url>` POSTs the report as JSON
so a headless run can collect a verdict instead of scraping text; the
report is also left on `window.fcxReport`.  To run it:

    ninja -C build/wasi-image                    # image + packed bundle
    cd src/Gui/Renderer/web && npm run build     # sandboxtest.js + the page
    scripts/fcx-web-stage.sh                     # bundle -> build/wasm/web/fcx
    python3 -m http.server -d build/wasm/web     # then open sandbox-test.html

## The evaluation switch-over: what one round trip costs (measured 2026-08-31)

Phase 0 deferred architecture A (the C++ value path, ExpressionSandbox.md
sec 4A) with the caveat that the switch-over is where the verdict could
flip: if a desktop round trip cost anything like the browser's measured
140 us, a whole-sheet recompute through the image would be 10-60x slower
and A would become a prerequisite.  So the switch-over starts with the
number, not a design.

Rig: `ExpressionImageBenchTest` in `tests/src/App/ExpressionImageHost.cpp`
-- DISABLED_ benchmarks that time the native engine and the image path in
ONE binary on ONE box, so the comparison carries no cross-rig error.  Run:

    ninja -C build/conda-relwithdebinfo-801
    FCX_IMAGE=$PWD/build/wasi-image/fcx_image.wasm \
    FCX_STDLIB=$HOME/works/sw/cpython-wasi/Python-3.12.13/Lib \
      build/conda-relwithdebinfo-801/tests/Tests_run \
      --gtest_also_run_disabled_tests --gtest_filter='*Bench*'

### THE IMAGE WAS BUILT -O0

The first run said 79 us for an arithmetic expression and 114 us for one
property read -- squarely in the "architecture A becomes a prerequisite"
band.  It was a build defect, not a boundary cost: the standalone image
project never set `CMAKE_BUILD_TYPE`, so **every TU we compile into the
image was -O0**.  CPython comes from its own `-O3` build, so the
unoptimized half was exactly the glue on the hot path -- nlohmann CBOR,
the marshaller, the expression walker, the bindings.  The CMakeLists now
defaults to Release; rebuilding cut every image number 3-6x and the
payload 39.4 -> 34.3 MB.

  case                             -O0      Release
  transport floor (rejected op)   18.0 us    2.68 us
  transport + 1 kB of payload     43.6 us   10.41 us
  expr literal, no owner          40.8 us    6.00 us
  evalExpression arithmetic       78.8 us   13.36 us
  evalExpression one property    114.2 us   20.92 us
  evalExpression five properties 221.3 us   36.44 us
  one mid-eval bridge hop        +95   us  +31.6  us

Anything measured against the older image (including the browser tier's
140 us round trip) is an -O0 number and should be re-taken.

### The numbers that decide the design

Native engine, in-process, same binary and box:

  parse + eval `1+2*3-4/5`            2.03 us
  eval only, AST already parsed       0.32 us
  parse only                          1.40 us
  parse + eval `Width * 2`            2.96 us

Image, warm instance, Release:

  transport floor (op the image rejects)          2.68 us
  transport, +1 kB request payload               10.41 us  (~7.7 ns/byte)
  expr literal, no owner (no host parse/pack)     6.00 us
  expr literal with owner                         9.21 us
  evalExpression `1+2*3-4/5`                     13.36 us
  evalExpression `Width * 2`                     20.92 us
  evalExpression 5 properties                    36.44 us
    of which the host-side pack alone            12.08 us
  python-lang literal (CPython compile+eval)     11.91 us
  one mid-eval bridge hop (read_prop)            +31.6  us
  instantiate + first eval from the .cwasm       15-16 ms

**Verdict: architecture A is NOT a prerequisite for the switch-over.**
An arithmetic evaluation costs 6.6x native (13.4 vs 2.0 us) and a
property-referencing one 7.1x (20.9 vs 3.0 us) -- an absolute delta of
11-18 us per evaluation.  Against the 13.2 us/cell a 10k-cell sheet
recompute costs today, routing every cell through the image projects to
~24-31 us/cell, i.e. **0.13 s -> ~0.25-0.31 s for the whole sheet**, not
the 1.3-8 s the -O0 numbers implied.  Anything touching `.Shape` (175+
us/cell today) is unaffected in relative terms.  A is still worth having
as an optimization; it is not the gate.

Where the remaining 11-18 us sits, and what would cut it:

- **2.7 us is the irreducible-ish transport** (fcx_alloc + fcx_call +
  two fcx_free wasmtime calls, two memcpys, CBOR both ways).
- **~3.3 us is host-side per-eval work that repeats** -- `Expression::
  parse` (1.4 us, the AST is thrown away every call) plus minting and
  releasing the owner handle.  Both are per-transaction, not per-eval:
  a prepared-expression cache and a transaction-scoped owner handle
  remove them.
- **~3.3 us is the in-image parse** of the same source, likewise once
  per distinct source if the image caches prepared ASTs by key.
- **~2.4 us per binding on the host** (resolve + marshal), which the
  native path pays too, and ~7.7 ns/byte of CBOR either way -- the
  argument for keeping packs small rather than for batching them.
- **A mid-eval bridge hop is 31.6 us**, 12x the transport floor, which
  is the quantitative case for pack-first: reach-back is the expensive
  shape, exactly as sec 7.3 assumed.

### The router (built 2026-08-31)

Two policy decisions, taken by the user on 2026-08-31, shape it:

- **Pre-resolve host-side, then fail closed.**  What the image cannot do
  (`setPath` writes, LinkPlacement/LinkMatrix accumulation, getSubObject
  walks, in-image dbind/lambda function values) gets resolved on the
  host into the bindings pack where that is possible; whatever still
  cannot cross **fails the evaluation**.  There is no silent retry in
  the host -- that would leave the boundary saying one thing and doing
  another.
- **Preference-gated, OFF by default.**
  `BaseApp/Preferences/Expression/Sandbox:Evaluate`.  The corpus
  regression (456 .FCStd, 7628 expressions) is the gate to changing the
  default, not a judgement call.

`src/App/ExpressionEvaluator.{h,cpp}` is the single place a top-level
stored expression is evaluated: `ExpressionSandbox::evaluate(expr,
options)` routes into the image when routing is on and the expression
qualifies, and calls `Expression::getValueAsAny` otherwise.  It is
host-only and deliberately NOT in ExpressionCore -- the core compiles
into the image and must never learn that a sandbox exists, which is
also why the seam sits at the CALLERS rather than inside
`getPyValue` (C1): `getPyValue` is re-entered by nested nodes and by
the spreadsheet, so routing there would cross the boundary per value,
the shape the measurement says costs 12x more.

Wired: `PropertyExpressionEngine`, both evaluation sites (the recompute
of a stored binding, and the hidden-reference update), and the
spreadsheet -- see "The spreadsheet through the router" below.

Mechanics worth knowing:

- **Re-entrancy is the one native path that survives.**  An evaluation
  triggered while an image evaluation is in flight -- a bindings-pack
  resolve that recomputes something -- runs natively, because it IS the
  host-side half of the outer sandboxed evaluation.  A thread-local
  guard detects it.
- **The AST is not re-parsed.**  `evalExpression` takes the caller's
  parsed `Expression*` and enumerates identifiers from it; only the
  source text ships.  (1.4 us/eval, measured.)
- **Handles are transaction-scoped**: the router decodes the result
  BEFORE `clearHandles()`, so a result that is itself a host object
  still resolves.
- **Errors keep the image's message** (parity is verified byte-identical
  down to the ParserError wording) and map the Python exception type to
  the matching `Base::` exception.  A denial arrives as a plain
  `Base::RuntimeError`, NOT the native path's
  `PermissionNeededException` -- the pending request was already
  recorded at pack time, so the popup-blocker UX still fires, and the
  type difference is what the tests use to prove an evaluation really
  crossed.
- `ImageHost::evalCount()` counts crossings, so a test can tell the two
  paths apart when the value cannot (both produce 43.0).

Tests: `ExpressionRoutingTest` -- routed value equals native, routing
off keeps the native path, a bound property recompute really crosses,
an image error does not fall back, and (slice B) python mode crosses in
both the walk and the parse.

### The spreadsheet through the router (slice B, built 2026-08-31)

**The seam is `PropertySheet::eval`.**  It is the sheet's equivalent of
the engine's binding recompute: `Sheet::updateProperty` calls
`cells.eval(expr)` for every dirty cell, and everything else in the
spreadsheet that evaluates a stored cell expression funnels through
`Cell::evalWhole`, which calls it.  So one function decides for the
whole workbench.  It gained a Python-valued twin, `evalPy`, because
several callers want the value and not an AST, and the round trip
through `expressionFromPy` and back was pure loss.

**The python-mode decision: python mode CROSSES, it is not refused.**
The previous slice refused an `OptionPythonMode` request rather than
run it in the host, on the grounds that it was "a different language".
Reading the core says otherwise: python mode is (a) a lexer start
state, and (b) a name-binding rule that makes CPython's builtins
visible on the eval frame.  Both live in `Expression.cpp`, and
`Expression.cpp` is compiled INTO the image.  Nothing had to be built
for it except carrying the option across.

That makes python-mode sheets the strongest case FOR routing rather
than an exception to it.  A python-mode cell can name any builtin --
`open`, `__import__`, `eval`.  In-process that is the host's `open` on
the host's filesystem.  In the image it is the image's builtins under
WASI with one read-only preopen for the stdlib, which is precisely the
confinement the sandbox exists to provide.

**The options now cross with the request.**  `evalExpression` takes an
`App::Expression::EvalOption` mask and ships it as `opts`; the image
parses with `pythonMode = opts & OptionPythonMode` and walks with
`getPyValue(opts)`.  Dropping them had been a latent parity hole even
before the spreadsheet: `PropertyExpressionEngine` passes
`OptionCallFrame`, and without a call frame the walker refuses every
statement outright ("`X` can only be used inside 'eval' or 'func'").
No corpus file had hit it, which is luck, not coverage.

**The gate compares each site with its own options too.**  Before this
slice `corpus_regression.py` evaluated a cell's source with the default
mask, so it compared something no sheet ever does -- and would have
compared a python-mode sheet in non-python mode on BOTH sides, matching
happily while testing nothing.  `collect_expressions` now returns the
mask per site, and `FreeCAD.ExpressionSandbox.evaluate` /
`evaluateNative` take an optional `options` argument (with
`OptionCallFrame` / `OptionPythonMode` exported as module constants).

The discriminator to use when checking this by hand is `hex(255)`: it
is not an expression function, so it evaluates to `'0xff'` only when a
python-mode call frame exists, and `sorted([3, 1, 2])` only PARSES in
python mode.  Both are covered by `ExpressionRoutingTest`, and an
end-to-end sheet recompute (7 plain cells, 4 python-mode cells, native
vs routed) shows 11 evaluations crossing with zero divergence.

Also routed, for the same no-silent-fallback reason: paste-as-value
(`Cell::setExpression` with `PasteValue`), `setContent(value, eval)`,
the `EditQuantity` edit-mode display and validation, `Cell::getPyValue`
in `EditNormal`, and `DlgSheetConf`'s range validation.  These are
edit-time rather than recompute-time, but they evaluate stored cell
code, and leaving them native would both breach the boundary and let an
editor show a value the recompute never produced.  They previously ran
with NO options at all, so they now also gain the sheet's call frame
and python mode -- a behaviour change on the native path, and the
consistent one.

No raw `Expression::eval()` call is left in the spreadsheet.  The only
one inside `evalWhole` is its ownerless fallback, which cannot occur
for a cell that belongs to a sheet.

NOT routed, and worth naming rather than leaving implicit: the GUI
expression editors (`Gui/DlgExpressionInput`, `SpinBox`, `InputField`,
`propertyeditor/PropertyItem`) evaluate what the user is typing, live,
in-process.  Step 3b already pushes a "session" principal there, so
they are policy-gated -- but policy-gated is not confined, and a live
preview has its own questions (a 15 ms instantiate on every keystroke,
errors shown inline) that the recompute path does not.  That is a slice
of its own, not an oversight in this one.

### The residue, measured (2026-08-31)

The "absent in-image by design" list above was written from the carve,
before anything routed.  The switch-over is where it had to be checked,
and checking it retired most of it.  Every one of these evaluates
IDENTICALLY native vs routed, on a document built to contain them
(App::Link over an App::Part over a Part::Box):

    Box._pla / __pla / _matrix / __matrix      LinkPlacement, LinkMatrix
    Link._pla / __pla / __matrix               through a Link
    Part._pla                                  through a container
    Link.Part.Box.Length                       a getSubObject walk
    Box._shape.Volume, Link._shape.Volume      _shape
    Box._self.Name, Part.Group[0].Length       _self, an indexed group

The reason is pack-first: `getIdentifiers` enumerates the WHOLE
identifier, `_pla` and all, and the host resolves it to a value under
its own permission checks.  The image never has to accumulate a
placement or walk a sub-object -- it is handed the answer.

**Writes were never the router's problem either.**  `Host.V = 9`
returns 9 and does NOT change the property -- in BOTH engines.  An
expression's assignment is not what writes a bound property;
`PropertyExpressionEngine::execute` calls `setPath` on the HOST after
the value comes back.  The write is on the host side of the seam by
construction.

**C12 (a cell persisting a live PyObject) holds.**  Object-valued cells
survive the round trip and stay usable: `=Box._self`, `=Box.Shape`,
`=Box.Placement` and dependent cells reading `.Length`, `.Volume`,
`.Base.x`, `.BoundBox.XMax` off them all match native.  The deferred
handle release is what makes this work -- see "a result may hold a host
object".

What genuinely remained was ONE thing, and it is now fixed: error-text
parity for a reference into a foreign document (below).

### Foreign-document errors carry the host's reason (2026-08-31)

The image has no foreign documents at all -- its Application shim only
ever knows the transaction's own document -- so left to itself it
answers any reference into one with "Document 'X' not found".  That is
the wrong REASON whenever X exists on the host and it was the property
or the object inside it that was missing, which is exactly what the
corpus gate's only two non-`same` rows were.

    Other#Pad.Configuration + 1
      native  Property 'Configuration' not found in 'Other#Pad.Configuration'
      image   Document 'Other' not found in 'Other#Pad.Configuration'

So the pack now carries FAILURES as well as values.  When host-side
resolution of an identifier throws and that identifier names a document
other than the owner's, `evalExpression` ships `{exc, msg}` under a new
`binderrs` request field; `EvalTransaction::lookup` raises it, as the
same KIND of exception, at the point the native engine would have
thrown.  Behaviour and text now match for a missing property, a missing
object, and a document missing on both sides.

**The narrow scope is the whole design.**  A blanket "ship every
resolution failure" would be wrong and was the first thing tried on
paper: an identifier that does not resolve on the host may be a
variable BOUND DURING the evaluation.  `a = Width + 1; a * 2` works
precisely because `a` is DROPPED from the pack and binds in-image; a
negative entry for it would break it.  Only foreign-document references
qualify, because only those are ones the image structurally cannot
answer.  `unresolvableLocalNameIsNotShippedAsAnError` guards this.

### What flipping the default actually needs (measured 2026-08-31)

The user's rollout decision was "preference-gated, OFF by default; the
corpus regression is the gate to changing the default".  The gate is
green.  Flipping the default would nonetheless be a **no-op on every
box but this one**, and the reason is packaging, not parity:

- `BUILD_EXPR_IMAGE_HOST` is **OFF by default**
  (`InitializeFreeCADBuildOptions.cmake:152`), so a released build has
  no `ImageHost` at all and `evaluationRouted()` returns false through
  the `#else`.
- There is **no default image path**: `ImagePath` /`StdlibPath` default
  to the empty string, and empty means unavailable
  (`ExpressionImageHost.cpp:156`).
- `src/App/ExpressionImage/CMakeLists.txt` has **no install rule** --
  `fcx_image.wasm` and the 16-file stdlib are build artifacts of a
  separate cross project that nothing packages.
- `libFreeCADApp.so` links `libwasmtime.so` **by absolute path** into a
  sibling dev directory (`${WASMTIME_CAPI_DIR}/lib/libwasmtime.so`), so
  the current build is not even relocatable.

So the flip is one decision plus a packaging slice: build the host on
by default (or detect), install the image + stdlib as data, resolve
`ImagePath` relative to the install prefix when the preference is
empty, and take wasmtime as a real runtime dependency (a conda-forge
package for the feedstocks, ~30 MB of image on top).  None of that is
expression work, and none of it is blocked by the gate.

### The packaging slice (built 2026-08-31)

The four items above are closed.  What a release still needs from
someone else is at the end of this section.

**The payload is the STRIPPED image and sixteen files.**  A development
box hands wasmtime the image as built (34.4 MB, of which 24 MB is DWARF
that only a native debugger reads) and preopens the whole CPython Lib
directory (51 MB).  Neither ships.  `tools/pack_image.py` already
produced the stripped image and the measured stdlib slice for the
browser; it now emits the desktop layout as well
(`--desktop-out`: `fcx_image.wasm` + `Lib/`, the names the host reads
and preopens), so both tiers come out of ONE slice list and cannot drift
apart.  **10.2 MB + 320 KB**, and the whole image gtest suite -- 44
cases including the acceptance set -- passes against it, so the
stripping and the sixteen files are verified rather than assumed.

**wasmtime is detected, not demanded.**  `cMake/FindWasmtime.cmake`
looks at `WASMTIME_CAPI_DIR` (cache or environment) and then the system
prefixes, and `BUILD_EXPR_IMAGE_HOST` now DEFAULTS to whether it found
one.  A box without wasmtime still builds -- `evaluationRouted()`
returns false through the `#else` and evaluation stays in process --
and `-DBUILD_EXPR_IMAGE_HOST=ON` without a runtime is still a
configure error, because that one was asked for.

What the find module checks is the CAPABILITY, not a version: the image
needs the WebAssembly exception-handling proposal, which wasmtime gates
behind `wasmtime_config_wasm_exceptions_set`, so the module greps
`wasmtime/config.h` for the `WASMTIME_CONFIG_PROP(void, wasm_exceptions`
declaration.  "wasmtime without the exception knob" is a sentence a
build log can act on; "wasmtime < 46" is not.

**TRAP: the release tarball's `libwasmtime.so` has NO SONAME**, and
that decides how it must be linked.  `FREECAD_BUNDLE_WASMTIME` (default
ON when the library is outside the install prefix and outside the
system directories) installs a copy beside FreeCAD's own libraries,
which is exactly the RPATH `SET_BIN_DIR` already gives every module.  A
wasmtime that came from a package manager is inside the prefix, so
bundling defaults off there and it stays a package dependency.

The copy is only found if `DT_NEEDED` names the library rather than a
path, and an imported target linked by its `IMPORTED_LOCATION` records
the **absolute path** when the library has no SONAME -- the first cut of
this work shipped a `libFreeCADApp.so` that asked the loader for
`/home/.../wasmtime-v48.0.1-.../lib/libwasmtime.so` by name, which
bundling cannot help.  `IMPORTED_NO_SONAME TRUE` on the imported target
is the documented cure: CMake then links `-L<dir> -lwasmtime`, so
`DT_NEEDED` is the SONAME where there is one and the bare file name
where there is not.  It is honoured only on a target CMake knows is
SHARED, though -- setting it on an `UNKNOWN IMPORTED` library changes
nothing at all and the absolute path comes back, which is the second
half of the same trap.  Worth checking after any change here --
`objdump -p libFreeCADApp.so | grep wasmtime` answers it in one line.

**The image installs as data.**  It is cross-built by a separate
project, so a host build can only install one that already exists:
point `FREECAD_EXPR_IMAGE_DIR` at `build/wasi-image/desktop` (or at
`cmake --install build/wasi-image`, which stages the same layout) and
the host build installs it as `<datadir>/Fcx/{fcx_image.wasm,Lib/}` AND
mirrors it into the build tree, the way `fc_copy_sources` mirrors
module resources.  The mirror is not a convenience: it means a
development build resolves the image through the SAME code path an
installed one uses, so the packaged behaviour is what gets tested.

**Resolution order, with nothing configured**: an explicit
`ImageHost::configure()` (tests, headless flags) wins, then the
`ImagePath` / `StdlibPath` preferences, then `FCX_IMAGE` / `FCX_STDLIB`
(a developer pointing at a build tree), then `<datadir>/Fcx`.  Nothing
found is not an error -- the sandbox is unavailable and evaluation
stays in process, which is what a build without a host does anyway.
`FreeCAD.ExpressionSandbox.imageInfo()` reports all three resolved
paths, because `available() -> False` on a user's box otherwise gives
nobody anything to look at.

**TRAP: the compiled-module cache moved out of the image directory.**  It
used to be `<image>.cwasm`.  An installed image sits in a directory the
running user cannot write, so that cache would silently never be
written and every start would pay ~600 ms of JIT instead of ~20 ms.  It
now lives in `<user cache>/ExpressionSandbox/`, and the file name
carries an FNV-1a hash of the image's full path: a box commonly has two
images with the same base name (a build tree and an install), and the
staleness check is a timestamp comparison, so a name collision there
would deserialize the WRONG module.

Verification rig: `scripts/expr-switchover/packaging_check.py` strips
every hint (environment popped, preferences asserted unset) and checks
that the image resolves to the data-dir bundle, loads, evaluates
through the router, and writes its cache under the user cache
directory and NOT beside the image.

**What a release still needs, and it is not expression work.**  Checked
2026-08-31: **conda-forge has no wasmtime at all**, and Anaconda's
`main` channel has 11.0.1 and 29.0.0 -- both far below the
exception-handling floor, and the CLI rather than the C API.

**DECIDED 2026-09-01 (user): the link stays SHARED, and wasmtime becomes
a package.**  `~/works/sw/wasmtime-capi-feedstock` builds
`wasmtime-capi` for the `realthunder` channel from the upstream release
artifact; once it is published the feedstock adds it to `host` and
`run`, the library lands inside the prefix, and `FREECAD_BUNDLE_WASMTIME`
defaults itself off.

Size did not decide it -- the two forms carry the same code.  Measured
2026-08-31 by relinking this build with
`-DWasmtime_LIBRARY=.../libwasmtime.a`: text goes from **8.3 MB to
33.3 MB**, +25.0 MB folded in, against **23.8 MB** of text in
`libwasmtime.so` standing alone (24.2 MB stripped on disk, versus a
stripped `libFreeCADApp.so` of 8.2 MB).  Within a megabyte of each
other.  What decided it is where this is going:

- The compute direction (`docs/ComputeBoundaries.md`, and the
  out-of-process OCCT goal) means SEVERAL FreeCAD processes at once,
  each recomputing documents and therefore evaluating expressions.
  Shared text is one physical copy the page cache hands to all of them;
  statically linked it is a private copy per process.
- `docs/ExpressionSandbox.md` sec 8 moves progressively more Python
  into the image -- rung 2 scripted-object `Proxy` code, rung 3 the
  console and macros, rung 4 Python workbenches -- and contemplates a
  second instance at addon-grade trust.  More of the process will want
  this runtime, not less, and folding it into `libFreeCADApp.so` makes
  a second consumer link its own copy of the Rust runtime.

The static path still works and is one flag (`-DWasmtime_LIBRARY=` an
archive): the find module supplies the Rust staticlib's own system
dependencies (`dl`, `pthread`, `m`), without which the link fails as a
wall of undefined `dlsym` that reads like a broken toolchain rather
than a missing `-ldl`.

Either way the image itself has to arrive as a prebuilt artifact: a
feedstock box cannot cross-build it without wasi-sdk 33 and a
wasm32-wasi CPython.

### The status-bar switch (built 2026-08-31)

Routing was already a LIVE setting -- `evaluationRouted()` re-reads the
preference on every evaluation, so flipping it takes effect on the next
formula and nothing restarts -- but the only ways to flip it were
`FreeCAD.ExpressionSandbox.setRouting()` and the parameter editor.  A
setting nobody can find is a setting nobody has.

`Gui::Dialog::SandboxIndicator` (in DlgDocumentPermissions.cpp, beside
the permission indicator it is a sibling of) is a permanent status-bar
light: an open amber padlock while expression Python runs in FreeCAD's
own process, a closed green one while it is confined, and a click that
flips between them.  One object in two states, so the button reads as a
switch rather than as a badge.

- **It warns on `enabled && !imagePresent` too.**  That is the state
  that would otherwise look secure and not be: the preference is on,
  the image is missing, and every formula is running in process.
  `SandboxStatus::confined()` is the whole rule.
- **Switching ON loads the image at the click**, behind a wait cursor,
  and puts the preference back if it fails.  A green light over an
  evaluator that never engaged would be worse than the amber one.
- **`App::ExpressionSandbox::sandboxStatus()` is deliberately cheap** --
  a preference read and two stat() calls.  `evaluationRouted()` asks
  whether an image LOADS, which instantiates one (~20 ms warm, ~600 ms
  when the compiled-module cache has to be made): fine at a click, far
  too much for drawing a status bar.  The Gui also cannot test
  `FC_EXPR_IMAGE_HOST`, which is defined only in `src/App`'s scope, so
  `hostBuilt` has to be reported rather than compiled against.
- A `ParameterGrp` observer keeps the icon honest when something else
  writes the preference.

TRAP, and it cost a rebuild: **`--` is illegal inside an XML comment.**
The ASCII rule replaces em dashes with `--` everywhere, and doing that
in a new .svg header made Qt refuse the file, so the confined icon drew
as NOTHING while its amber twin was fine.  `QSvgRenderer::isValid()`
says so in one line; the status bar does not.

### Where the time goes (re-measured 2026-08-31, after slices B and C)

    native.parse+eval.arith          2.02 us
    native.eval.arith.cachedAST      0.33 us    <- parse is 1.4 us of it
    native.parse+eval.prop           3.14 us

    image.transport.floor            2.63 us    <- ping, no CPython
    image.expr.noOwner.literal       7.75 us    <- + expr dispatch/walk
    image.expr.owner.literal         9.48 us    <- + owner handle (1.7)
    image.evalExpression.arith      14.90 us
    image.expr.oneBinding           21.65 us    <- one binding = ~12 us
    image.evalExpression.prop       21.75 us
    image.expr.fiveBindings         36.81 us    <- ~5.5 us each after
    host.pack.fiveBindings          12.06 us    <- 2.4 us each, host side
    image.eval.oneBridgeHop         41.81 us
    image.instantiate+firstEval     16-28 ms

Slices B and C cost ~1.5 us per eval (13.4 -> 14.9 arith), which is the
`opts` and `binderrs` fields on the wire.

The ranked levers, and what each is worth:

1. **The bindings pack, ~12 us for the first binding and ~5.5 us for
   each after.**  This is the biggest item by a distance and it is NOT
   on the slice E list, which named the AST cache, the owner handle and
   instance pooling.  Only 2.4 us of it is the host-side resolve; the
   rest is encode + CBOR + in-image decode + the in-image identifier
   lookup.  Worth an anatomy pass before optimising anything else.
2. **In-image AST re-parse.**  Real -- natively parse is 1.4 us of a
   2.0 us eval, and the image re-parses the same source every call
   during a recompute.  TRAP: `Expression::parse(tx.owner(), ...)`
   stores the transaction's owner pointer in every node, and the
   transaction is destroyed at the end of the eval.  A cached AST
   therefore points at a dead owner unless the in-image owner becomes a
   stable object that the transaction re-binds.  That is a real design
   change, not a cache.
3. **Transaction-scoped owner handle, 1.7 us (11%).**  TRAP: it has to
   survive `clearHandles()`, which is exactly the invariant the
   deferred-release fix depends on -- see "a result may hold a host
   object".  Small win, sharp edges.

### The compatibility gate (built 2026-08-31)

ES sec 11's migration constraint -- old files must evaluate identically
-- is checked by a rig, not by judgement.  `scripts/expr-switchover/`:

- `corpus_regression.py` opens real `.FCStd` files and, for every stored
  expression in them (engine bindings and sheet formulas), evaluates the
  SAME source twice: `evaluateNative` and `evaluate` with routing on.
  Each pair is classified same / differ / both_error / image_only_error /
  native_only_error, and the image-only failures are grouped by message.
  That group IS the gap list, ranked by field frequency.
- `run_gate.sh` runs one FreeCADCmd PER FILE with a hard timeout.  This
  is not fastidiousness: a `.FCStd` saved by OCCT 7.7.2 gets a forced
  geometry recompute when 8.0.1 opens it, and on a big assembly that
  runs for tens of minutes with nothing to interrupt it -- a
  single-process sweep simply stops, 355 files in, and the whole run is
  lost.  Per-file subprocesses turn that into one row in `timeouts.txt`.
- `summarize_gate.py` merges the per-file summaries into the totals and
  the gap list.
- Permission enforcement is OFF by default in the rig (`--enforce` turns
  it on): the gate measures EVALUATION parity, and mixing in policy
  denials would only re-test what the runtime tests already cover.

- `packaging_check.py` (2026-08-31) answers a different question: with
  no preference, no environment and no configure call, does the host
  find the image the way a packaged FreeCAD has to?  See "The packaging
  slice".

Four traps the rigs themselves had to absorb, all cheap to re-learn the
hard way: passing a script PATH to FreeCADCmd runs nothing AND says
nothing (use `-c "exec(open(...).read())"`); `sys.exit()` out of that
`-c` skips the shutdown that flushes a block-buffered stdout, so a
redirected or piped run prints NOTHING and still exits 0 (flush before
exiting -- this one cost a "why is my new rig silent" detour);
FreeCADCmd's restore progress bars drown stdout, so the summary is
written to `<out>.summary` as well as printed; and the rigs change
GLOBAL preferences, so they restore every one of them (`PrefGuard`).

That last one is not hypothetical.  **`FREECAD_USER_HOME` pointing at a
directory that does not exist is silently ignored** --
`Application::getCustomPaths` clears the variable when the path does
not resolve, without a word -- and the run then writes to the user's
real config.  An ad-hoc probe run that way left `Enforce=0` behind, and
the next `ExpressionRoutingTest` run failed with "it throws nothing"
because the permission wall the test relies on had been disarmed
box-wide.  `run_gate.sh` `mkdir -p`s the directory, which is why the
gate itself never hit it; the scripts restore preferences anyway, for
the callers that do not.

**First finding, fixed: a tuple crossed back as a list.**  Both
marshallers encoded any sequence as a JSON array and decoded it as a
list, so `cells[<<A17:|>>]` bound to an Enum property returned
`['M3','M4','M5','M6']` where the native engine returns a tuple -- the
same type-identity loss Phase 0 recorded for bool and told us not to
reproduce.  Tuples now carry an explicit wire tag (`{"t":"tup","v":
[...]}`) on both sides; a plain array still means list.  Covered by
`tupleCrossesBackAsTuple` and `tupleCrossesIntoTheImageAsTuple`.

**First full run, 2026-08-31** (this box, ~/works, files up to 41 MB,
one FreeCADCmd per file, 240 s each):

    files                371
    expressions          262   + 73 from scanner.FCStd (run separately)
    same                 260   + 73
    differ                 0
    both_error             2
    image_only_error       0
    native_only_error      0
    timed out              3

`scanner.FCStd` is the one Phase 0 named as the compatibility gate --
the Assembly3 dbind-heavy assembly, 73 stored expressions -- and **all
73 evaluate identically through the image**.  It failed the sweep only
because the document's addon module is missing on this box, which makes
FreeCADCmd exit 1 AFTER every expression has been compared; the driver
now trusts the summary file rather than the exit code.

Honest coverage note: an XML count over the same roots finds 513 stored
expressions (369 engine bindings + 144 sheet formulas) in 33 files, so
this run covers 335 of them.  The rest sit in two documents whose
7.7.2 -> 8.0.1 forced geometry recompute takes more than ten minutes to
OPEN (`issue474_fillet_edit_crash.FCStd`, 65; `cartridge.FCStd`) --
an environment cost with nothing to do with expressions, but they are
uncovered and should not be counted as passing.

The 2 `both_error` rows are the same expression in two Russian-language
documents referencing a property that no longer exists; both engines
refuse it, with DIFFERENT text -- native says "Property 'Configuration'
not found in ...", the image says "Document 'X' not found in ...",
because the pack could not resolve the foreign document at all.  Parity
of error TEXT for unresolvable foreign references is therefore still
open; parity of behaviour (both fail) holds.

**Second full run, 2026-08-31, after slice B** -- same corpus, but now
each site is compared under its OWN option mask (cells with the sheet's
call frame and python mode, not the default 0):

    files                372
    files_failed           0
    expressions          335
    same                 333
    differ                 0
    both_error             2
    image_only_error       0
    native_only_error      0
    timed out              2

Unchanged totals, and this time `scanner.FCStd` came through the sweep
itself rather than needing a separate run.  The 2 `both_error` rows are
the same two described above; the 2 timeouts are the same two documents
whose 7.7.2 -> 8.0.1 forced recompute exceeds the per-file budget
(`issue474_fillet_edit_crash.FCStd`, `cartridge.FCStd`).

**Third full run, after the foreign-document error fix**: identical
totals -- 372 files, 335 expressions, 0 differ, 0 image-only, 2
`both_error`, 2 timed out -- and the two `both_error` rows now carry
the SAME message on both sides.  `both_error` stays at 2 because both
engines correctly refuse the same expression; that is parity, not a
gap.

The gate measures that now rather than leaving it to be noticed by eye:
a new `both_error_text_differs` counter compares the two messages, and
`error_text()` strips the `sfile`/`iline`/`sfunction` fields out of a
FreeCAD exception's dict repr first.  Those name the THROWING BINARY's
own source path -- the image is a different binary and can never
reproduce them, and they are not evaluation behaviour, so comparing
them would report a permanent false difference.  On the two rows in
question the counter reads **0**.

## Pyodide-on-node, benchmarked and probed (2026-09-01)

The WASI image is the shipping runtime, but the question was reopened:
run **pyodide** (CPython on wasm32-emscripten) on both the desktop and
the browser tier, so the whole prebuilt scientific stack (numpy, scipy,
pandas, ...) comes for free, and so a future browser-side Python
workbench has a runtime.  Two earlier objections turned out to be
partly wrong, so the question was settled with measurement rather than
memory.  Rig and raw numbers are in this session's scratchpad
(`bench_a.mjs`, `embed_bench.cc`, `probe_escape*.mjs`, `RESULTS.txt`);
box was 6-core, node 26.6.0 (V8 14.6.202.34-node.26), pyodide 314.0.6
(CPython 3.14).

### Correction 1: conda DOES ship an embeddable JS engine

The earlier claim "no conda package for embedding a JS engine, so
pyodide reopens the packaging problem" was FALSE.  conda-forge
`nodejs 26.6.0` ships `lib/libnode.so.147` (71 MB, bundles V8) plus the
full `include/node/` tree including 53 V8 headers, built `--shared`, on
every platform we target (linux-64/aarch64, osx-64/arm64, win-64/arm64).
deno, bun, and quickjs are there too.  So an in-process, synchronous JS
host is a package install, not a build.  QtWebEngine (which we do ship)
cannot host this: wrong layer (Gui, needs QApplication; the host lives
in App and must work in FreeCADCmd), and `runJavaScript()` is async
across a Chromium IPC to a separate process, which cannot be called
synchronously from inside recompute.  `QJSEngine` (Qt QML V4) has no
WebAssembly at all.

### Correction 2: the boundary is FAST, and A is not a prerequisite either

  case                             pyodide/node   WASI image (Release)
  transport floor (py callable)      0.34 us          2.68 us
  eval precompiled code object       0.94 us            --
  compile+eval '1+2*3-4/5'          15.6  us         13.36 us
  eval + 1 host hop                  3.02 us         +31.6  us  (bridge hop)
  eval + 5 host hops                11.3  us            --
  cold start                      ~1420  ms         15-16 ms

Two things stand out.  The pyodide **host hop is ~10x cheaper** (3.0 vs
31.6 us): a py->host->py crossing is a direct V8 call into a JS callback,
not a CBOR round trip through a wasmtime linker.  For property-heavy
expressions (the common sheet case) that is the dominant term, so
pyodide would likely be *faster* per cell than the image once bridges
are involved.  Plain arithmetic is a wash (15.6 vs 13.4 us).  The one
regression is **cold start: ~1.4 s vs 15 ms** -- pyodide boots a whole
CPython+emscripten runtime; the image instantiates a `.cwasm`.  A
desktop that boots pyodide once per session eats this at startup; a
per-invocation model cannot.  (Also: use pyodide's `runPython(code)`
sparingly -- the string->module compile path measured 206 us; hold a
compiled code object and `eval` it, 0.94 us.)

The C++ side adds almost nothing: a `libnode` embedder calling a JS
function via `v8::Function::Call` measured **45 ns** per hop
(`embed_bench.cc`, real CommonEnvironmentSetup embed).  The embedding
overhead is not where the cost is; CPython is.

numpy: `loadPackage('numpy')` pulls a 2.9 MB prebuilt cp314 wasm32
wheel (264 ms first time, then cached), imports, and runs
(`np.arange(1000).sum()` in 3.1 us).  This is the entire point --
scipy/pandas/lxml/Pillow/matplotlib are all prebuilt the same way,
versus weeks-to-blocked each against wasi-sdk.

### The real cost is the SECURITY MODEL, and it is a different class

Under DEFAULT pyodide-on-node config the sandbox is **fully escaped**.
Guest Python reaches `js.process`, and through it
`process.binding('fs')`, `process.binding('spawn_sync')`,
`process.dlopen`, `process.env`, `process.exit`.  Even with `js.process`
hidden, `obj.constructor.constructor` (the Function constructor) runs
its body in global scope where `process` still lives.  Verified reading
`/etc/hostname` is only blocked because pyodide's `require` is not a
global; the file-read primitive itself is reachable.

**`del globalThis.process` is NOT sufficient** -- an earlier draft here
said it was, and a deeper audit (2026-09-01) disproved it.  Deleting
`process` does close the `js.process` and `constructor.constructor ->
process` routes, and pyodide/numpy keep working (pyodide captured its own
`require`/`fs` refs at init).  But it leaves a full escape open:

- `js.Function('return import("node:fs")')()` -- dynamic `import()`
  through the Function constructor -- **still resolves to a working
  module**.  Verified: after `del globalThis.process`, the guest read
  `/etc/hostname` (`readFileSync`) AND wrote `/tmp/pwned` (`writeFileSync`).
  `import("node:child_process")` and `import("node:net")` are reachable
  too.  Dynamic `import()` is a realm intrinsic wired to node's module
  loader (`HostImportModuleDynamically`); it needs no `process` and
  cannot be removed by deleting globals.
- `fetch` survives on the global -- node's real `fetch`, i.e. network
  egress, which the WASI image (no sockets) does not have.  `Buffer`,
  timers, `crypto`, `WebAssembly` survive too.

Node's **Permission Model** (`--permission`, C++-enforced, the only
non-JS-bypassable lockdown) is the principled fix, and it does NOT work
with pyodide as shipped: with no grants pyodide cannot read its own
files; grant `--allow-fs-read=<pyodide dir>` and it still dies with
`ERR_ACCESS_DENIED: process.binding` -- pyodide's node loader uses
`process.binding`, which the model blocks.  The model is also
per-PROCESS, so a grant that lets the host load pyodide also lets the
guest use it; there is no host-vs-guest split in one process.

The remaining JS-land option is to run pyodide inside a `node:vm`
context with a scrubbed global (`Object.create(null)`, no `fetch`/
`process`) and a denying `importModuleDynamically` hook.  That IS tested
to work at the mechanism level -- in such a context `import("node:fs")`
rejects ("import denied") and `fetch`/`process` are `undefined` -- but it
needs `--experimental-vm-modules`, requires feeding pyodide controlled
host functions to load its files through the scrubbed global, and is a
DENYLIST that must be re-audited on every node and pyodide bump (each can
add a newly reachable primitive).

This is the crux of the decision.  The **WASI image denies all of this
BY CONSTRUCTION** -- one preopen, no module loader, no sockets, no
subprocess, plus wasmtime fuel metering and epoch interruption.  A
node-hosted pyodide cannot be brought to that guarantee by SUBTRACTION
(delete globals / permission flags); it takes either the `node:vm`
denylist above, or embedding a V8/node build where `fs`/`child_process`/
`net`/the dynamic-import loader are NEVER registered (option B below --
confinement by construction, the same property WASI gives for free).

### Two loaders, do not conflate them (killing one keeps the other)

A natural worry: "if a V8-only embed removes the module loader, does
pyodide lose the ability to load numpy, i.e. must we bundle everything
statically?"  No -- there are TWO independent dynamic-loading mechanisms:

- **Node's JS module loader** -- `import()` / `require`, which resolve to
  node builtins (`fs`, `net`, `child_process`).  This is the ESCAPE
  surface.  A V8-only embed never wires it; killing it removes access to
  node builtins and nothing else.
- **Emscripten's wasm dynamic linking** -- `WebAssembly.instantiate` of
  side modules, which is how pyodide links a wheel's C extensions at
  runtime.  `WebAssembly` is a core V8 API, always present; it needs no
  node loader, no `fs`, no `import()`.

Measured: `loadPackage('numpy')` fired **13 `WebAssembly.instantiate`
calls** (numpy's C extensions linking in) and worked fine AFTER
`del globalThis.process` -- proving numpy loading does not ride the node
`import()`/`fs` route the escape uses.  The only thing the host must
supply is the wheel BYTES; in node mode pyodide reads them via `fs`, but
in a V8-only/browser mode you provide a controlled reader scoped to the
wheel directory -- a WASI-style read-only preopen -- and pyodide links
the bytes with core `WebAssembly`.

So the "must statically link every extension, no wheel installable ever,
`libdl` is a stub" limit is the **WASI image's alone** (wasmtime +
wasi-sdk, where `dlopen` really is a stub).  It is NOT a property of wasm
in general and NOT inherited by pyodide: pyodide (emscripten) keeps
dynamic wheel loading whether hosted on node or on bare V8.  The
confinement target (kill node's JS loader) and the ecosystem win
(dynamically load Python wheels) use different machinery and do not
collide.

### Building V8 ourselves: adapting the node feedstock (corrected 2026-09-01)

An earlier draft of this section said building V8 means `depot_tools` +
`gclient sync` (~8-12 GB, bundled clang) + GN.  **That is the standalone
Chromium V8 path, and node deliberately avoids it** -- so the estimate
was wrong for the recipe we would actually adapt.  What the
`conda-forge/nodejs` feedstock (rattler-build `recipe.yaml`, same shape
as our wasmtime-capi-feedstock) really does:

- Source is the plain `node-v26.6.0.tar.gz` from nodejs.org, which
  **vendors V8 in `deps/v8`**.  No depot_tools, no gclient, no GN, no
  downloaded clang.
- Build is `./configure --ninja --shared ... --with-intl=system-icu`
  then `ninja -C out/Release`, driving **node's own GYP files**
  (`tools/v8_gypfiles/`; the recipe's `abseil.gyp` shim patches one of
  them) with the ordinary conda toolchain (`compiler('c'/'cxx')`, ninja,
  python 3.13, pkg-config, system-icu, system-abseil).  Eight small
  patches, mostly SIMD-guard/RISC-V.

So building V8 from node's tree is cheap and toolchain-free.  Measured,
V8 is ~61% of libnode's code (22.3 MB of 36.6 MB of symbol bytes), so a
V8-only artifact would be ~40-45 MB vs libnode's 71 MB.  Three tiers of
"adapt the recipe":

- **(A) Adapt nothing -- depend on `nodejs`.**  libnode.so is published
  on all six platforms, and the node team backports V8 security fixes
  into their release line, so tracking `nodejs` conda updates gives
  patched V8 for FREE rather than chasing V8's ~4-week cadence.  This is
  the strongest argument and it favours libnode.
- **(B) Fork the feedstock for a slim V8-only lib.**  Same tarball; add a
  `v8_monolith`-style GYP target that bundles node's `v8_*` static libs
  into one shared `libv8`, install it + V8 headers, drop the node runtime
  and npm.  Bounded work, but real: node has no `--shared-v8`, V8 is
  folded statically into libnode by default, so this is a GYP target you
  write, not a flag you set.  Payoff ~40 MB and a bare V8 you fully
  control.
- **(C) What neither A nor B removes:** pyodide's emscripten glue needs a
  host environment -- `TextDecoder`/`TextEncoder`, `URL`, `fetch` or
  `require(node:fs/crypto/url/path)`, `performance.now`, `process.*`
  (grepped from `pyodide.asm.mjs`).  libnode supplies all of it; a
  stripped V8-only lib supplies none, so option B means reimplementing
  that environment.  That cost is independent of how cheaply V8 builds.

So the honest correction: the *build* cost of a self-built V8 is small
(adapt the recipe), not the large bill claimed before.  The reasons to
still ride libnode-as-is are (1) it already provides the emscripten
environment (option C), and (2) it inherits node's V8 security backports.
The one reason to do option B anyway is **security by construction**: a
hand-rolled V8 embedding need never register `fs`/`child_process`/`net`
or a dynamic-import loader at all, so the escape demonstrated above
(dynamic `import()` via the Function constructor, which no global deletion
closes) cannot exist -- WASI-grade confinement.  Given that the audit
showed a node-hosted pyodide CANNOT be confined by subtraction, this is
not merely "nicer": for desktop pyodide it is either option B or the
`node:vm` denylist, and option B is the only one that is confinement by
construction.  Its cost is the environment layer, not the engine.

### Verdict

Nothing here is a blocker; the earlier "keep pyodide rejected for the
desktop" was too strong and rested on the false no-JS-engine premise.
The honest trade is:

- **Runtime speed**: pyodide is competitive to better (host hops 10x
  cheaper), except cold start (1.4 s vs 15 ms) -- fine for a
  boot-once-per-session desktop, bad for per-invocation.
- **Ecosystem**: pyodide wins outright (prebuilt numpy/scipy/pandas/...);
  the WASI image cannot install a wheel ever (`libdl` is a stub).  If the
  goal grows to **Python workbenches in the browser** (Draft/BIM are
  Python), this dominates.  NOTE the two loaders are orthogonal (see
  below): the "no wheels" limit is the WASI image's alone; pyodide keeps
  dynamic wheel loading on bare V8 too.
- **Security**: the WASI image wins outright.  A node-hosted pyodide
  CANNOT be confined by subtraction (`del process` leaves a full FS
  read+write escape via dynamic `import()`; node's permission model
  won't load pyodide); confinement takes either a `node:vm` denylist
  (re-audited every bump) or option B.  For a padlocked expression
  sandbox this is the property that matters most.
- **Cost**: plain pyodide-on-libnode is mostly integration (embed the
  host in App, drive it synchronously, re-cross-build our C++ slice for
  emscripten), no toolchain to own -- BUT it is not truly confined.  A
  confined desktop pyodide costs either the `node:vm` sandbox
  (`--experimental-vm-modules`, controlled global, ongoing re-audit) or
  option B (fork the node recipe for a V8/node build without the modules,
  plus reimplement the emscripten environment).  Both are real bills the
  WASI image simply does not have.

Recommendation unchanged in shape but softened: **keep the WASI image as
the expression sandbox** (its confinement is the whole point), and adopt
**pyodide as a SEPARATE runtime for the browser Python-workbench goal**,
where its ecosystem is decisive and the security model is the browser's
own.  Do not build V8 by hand: libnode already paid those costs, and if
the security surface ever forces a hand-rolled embedding, that is a
scoped future project, not a prerequisite.  The `ImageHost` seam,
FcxWire, and the 456-file corpus gate are runtime-agnostic, so a pyodide
backend slots behind the same interface without discarding the
switch-over work.

### Overtaken (2026-09-02)

The user decided the other way on the last point, and the "scoped
future project" was done the same day: **V8 IS built by hand** --
`realthunder/v8-embed`, a bare engine out of node 26.6.0's tree
(small-icu, shared library, no node modules, all five conda platforms,
Windows from source with clang-cl) -- and **pyodide runs on the desktop
as the second runtime behind `ImageHost`**, selected by the preference
`Expression/Sandbox:Runtime`.  The "not truly confined" cost above was
the node host's; on a bare engine the confinement is by construction
(section 5 of `docs/PyodideHost.md` says exactly what is and is not
proven).  The WASI image stays the shipping default.  Everything from
here on is in `docs/PyodideHost.md`.
