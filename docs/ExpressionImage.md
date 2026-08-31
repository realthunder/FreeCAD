# The expression sandbox image (Phase 1 step 4 build notes)

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

1. Writing/setPath, LinkPlacement/LinkMatrix accumulation and
   getSubObject walks are absent in-image by design (host pre-resolves
   or the evaluation fails cleanly); revisit when the evaluation
   switch-over lands.
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
into the interpreter.  `tools/webpack_image.py` produces both -- it
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

Wired so far: `PropertyExpressionEngine`, both evaluation sites (the
recompute of a stored binding, and the hidden-reference update).  The
spreadsheet is the next slice, and it is the one that has to decide
what python-mode sheets do -- for now `evaluate()` REFUSES an
`OptionPythonMode` request while routing is on rather than quietly
running it in the host.

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

Tests: `ExpressionRoutingTest` (5 cases) -- routed value equals native,
routing off keeps the native path, a bound property recompute really
crosses, an image error does not fall back, python-mode is refused.
C++ ctest 507/507, Python 2628 OK with the preference off (the default).
