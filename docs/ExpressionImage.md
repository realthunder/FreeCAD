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
