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
ExpressionCore itself (Expression.cpp / ObjectIdentifier.cpp behind
the S1 adapter) is NOT in the image yet, and the image->host bridge
ops (read_prop etc.) wait on the host embedding.

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

## What step 4 still owes (in order)

1. The image->host bridge ops (`read_prop`/`get_attr`/`call`/
   `get_item`/`len`/`release`/`resolve_alias`): the image imports a
   host function and issues these mid-eval when it hits a handle.
   The host->image `eval` op and the value encoding are DONE
   (FcxWire.h, ImageMarshal.cpp; nlohmann::json CBOR both sides).
2. The wasmtime host embedding in FreeCADApp (host-only TU;
   engine/store/instance per principal; handle table per recompute
   transaction; per-op permission checks through
   App::ExpressionSecurity::Runtime).
3. ExpressionCore into the image behind the S1 adapter
   (ObjectIdentifier resolution against the bindings pack instead of
   live Documents) -- the biggest carve, see Phase 0 sec 2 seam list.
4. Generated facades/dispatch from the Py XMLs with the `<Sandbox>`
   annotation (ES sec 7.5), replacing the hand-registered module in
   ImageMain.cpp.
5. Acceptance harness: the ES sec 10 denials plus
   grant->works->revoke->fails against the image.

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
