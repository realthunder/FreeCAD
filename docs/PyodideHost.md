# The pyodide host: CPython-on-emscripten inside a bare V8

Status as of **2026-09-02**: phase 0 built and measured
(`src/App/PyodideHost/`).  Pyodide boots on the V8 that the `v8-embed`
package ships, with FreeCAD supplying the Web environment and nothing
else, runs Python, calls back into the host, loads numpy from a wheel,
and every confinement probe holds.  Nothing is wired into FreeCAD yet.

This is the runtime the v8-embed feedstock exists for.  The reasoning
that led here -- why pyodide, why not node, why a bare engine -- is in
`docs/ExpressionImage.md` ("Pyodide-on-node, benchmarked and probed")
and is not repeated; this page is the record of what was then built.

## 1. What the host is

Three pieces, and the boundary between them is the design:

- **The engine**: `libv8.so` from `v8-embed` (V8 14.6, built out of
  node 26.6.0's tree; see the feedstock README).  A fresh
  `v8::Context` on it has ECMAScript and `WebAssembly` and no way to
  reach the machine: no `process`, no `require`, no filesystem, no
  network, no module loader.  That is not a policy the host applies, it
  is what an engine is without a host.
- **The shim** (`host_shim.js`): the subset of the Web/d8 platform that
  pyodide's emscripten glue reads, written by hand.  Every name it adds
  is listed in section 3.  What it does NOT add is the security
  mechanism -- an escape needs a primitive, and the primitives are not
  there.
- **The native surface** (`PyodideProbe.cc`, to become
  `PyodideHost.cc`): five functions on a temporary `__fcx_host` object
  that the shim consumes and deletes -- `readText`, `readBytes`,
  `randomBytes`, `now`, `print` -- plus the two callbacks V8 asks an
  embedder for when script uses `import()` and `import.meta`.  Both
  file readers and the module loader refuse any path outside the
  pyodide directory (`weakly_canonical`, then a prefix test against the
  root), so symlinks and `..` cannot leave it.

Pyodide is loaded as shipped in the npm package, unmodified:
`pyodide.js` (the classic-script loader), `pyodide.asm.mjs` (the
emscripten glue, an ES module), `pyodide.asm.wasm`,
`python_stdlib.zip`, `pyodide-lock.json`, and wheels beside them.

## 2. How pyodide sees the host

pyodide.js decides where it is running from globals.  Node needs
`process.versions.node`, a browser needs `window.document`, and a
**shell** is `typeof read == "function" && typeof load == "function"`
-- d8's names.  With none of the three it throws
`Cannot determine runtime environment`.  The host presents as a shell,
which is the smallest of the three surfaces and the one designed for an
engine with no platform:

- the lock file and other text come through `read(path)`;
- binaries (the wasm, the stdlib zip, wheels) through `readbuffer(path)`
  or `read(path, "binary")`;
- `pyodide.asm.mjs` through `import(indexURL + "pyodide.asm.mjs")`,
  which is the one reason the host has a module loader at all.  The
  glue has **no static imports**; every `node:` import in it is a
  dynamic `import()` inside an `if (IN_NODE)` branch that is never
  taken, and would be refused by the loader if it were;
- random bytes: in the shell branch emscripten's `initRandomFill` does
  not look for `crypto`; it runs `head -c N /dev/urandom | base64`
  through `os.system`.  The shim's `os.system` recognises exactly that
  request, answers it from the host RNG, and throws on anything else.
  `crypto.getRandomValues` is provided too, for the paths that use it
  after startup.

The one V8 embedding detail that is not obvious: `WebAssembly.instantiate`
compiles on the platform's worker threads and delivers the result
through the **foreground task queue**, which the embedder must pump
(`v8::platform::PumpMessageLoop`) -- without it the boot promise stays
pending forever and looks like a deadlock in pyodide.  The host's event
loop is: pump platform tasks, run microtasks, fire due shim timers,
sleep until the next one, until the awaited promise settles.

## 3. The shim inventory

Everything the guest can name that the engine did not provide:

| Global | Purpose | Reach |
|---|---|---|
| `console.*`, `print`, `printErr` | output | host stdout/stderr |
| `read`, `readbuffer`, `load` | d8 readers; `load` only for detection and throws | files under the pyodide dir |
| `os.system` | emscripten's shell RNG path | host RNG; refuses every other command |
| `crypto.getRandomValues`, `crypto.randomUUID` | RNG | host RNG |
| `performance.now` | monotonic clock | host clock |
| `setTimeout`/`setInterval`/`clear*`, `queueMicrotask` | timers, a real queue the host drives | none |
| `TextEncoder`, `TextDecoder` (utf-8, utf-16le, latin1) | text codecs | none |
| `URL` | path resolution only; `createObjectURL` throws | none |
| `btoa`, `atob` | the package loader base64-encodes wheel digests | none |
| `self` | alias of `globalThis` | none |

Plus the two engine callbacks: dynamic `import()` (files under the
pyodide dir only) and `import.meta.url` (`file://` + the module path).

Not provided, and checked absent by the probe: `process`, `require`,
`module`, `fs`, `fetch`, `XMLHttpRequest`, `Worker`, `WebSocket`,
`MessageChannel`, `Deno`, `Bun`.

## 4. What phase 0 measured (this box, 6-core, 2026-09-02)

    loadPyodide()                                  booted
    cold start                                     1522 ms
    py.runPython('1+2*3-4/5')                      6.2
    sys.version                                    3.14.2
    host hop js.hostReadProp()                     84
    loadPackage('numpy')                           loaded   (from the local wheel)
    numpy arange(1000).sum()                       499500
    numpy version                                  2.4.6
    import('node:fs') / import('fs') /
      import('/etc/hostname') / Function-ctor import()   all rejected
    read('/etc/hostname'), read(root + '/../x')    threw
    os.system('sh -c id')                          threw
    python open('/etc/hostname')                   OSError
    python js.process                              None
    python socket connect                          OSError
    while True: pass + TerminateExecution          terminated after 300 ms
    runPython / numpy / host hop after termination all still work
    precompiled call / call + host hop             0.53 us / 2.90 us

Beside the node numbers from 2026-09-01 (0.94 us / 3.02 us, cold start
1.42 s) and the WASI image (13.4 us arithmetic, +31.6 us per bridge
hop, 15 ms cold start): the bare engine is as fast as node on the
boundary, and the cold start is pyodide's, not the host's.

The numpy row answers the question the image feedstock order left open
("evaluate the cost of including numpy"): here it costs nothing to
build -- the wheel is prebuilt for `pyemscripten_2026_0_wasm32`, is read
as bytes by the scoped reader, and links through core `WebAssembly`.
The two loaders really are independent, as the earlier analysis said:
the host has no `import()` that leaves the directory, and numpy loads
anyway.

## 5. Where confinement stands, honestly

What is proven: the guest cannot name a filesystem, a socket, a process,
or a loader, because none exists in the context.  The `import()`
callback and the readers are the only ways bytes enter, both scoped to
one directory the host chose, and Python's own `open`/`socket` fail
because emscripten's libc has nothing underneath but its in-memory FS.

What is NOT yet done and belongs to phase 1, not to this probe:

- **Resource limits, half done.**  wasmtime gave the WASI image fuel
  metering and epoch interruption for free.  V8's equivalent is
  `Isolate::TerminateExecution` from a watchdog thread, and the probe
  shows it works on this guest: `while True: pass` stops at the
  watchdog's 300 ms, and afterwards `runPython`, numpy and a host hop
  all still work -- **recovery is not a reboot** (the earlier fear).
  Caveat: three checks say the interpreter is usable, not that every
  CPython invariant survived an unwind through its frames; phase 1
  should run the corpus gate after a termination.  Memory is the other
  half: pyodide's heap is wasm linear memory, which V8's heap limits do
  not govern -- its ceiling is emscripten's `MAXIMUM_MEMORY`, to be read
  off the module and, if needed, bounded by the host's own allocator.
- **The RNG shim is a string match** on emscripten's command line.  It
  is correct for this pyodide; a pyodide bump that changes the command
  breaks startup loudly (throws), never silently.  Prefer making the
  glue take the `crypto` path if a future version offers a switch.
- **Re-audit on every pyodide bump**: the shim is an allowlist, which is
  the right shape (a new primitive pyodide wants is a missing name, i.e.
  a loud failure), but the glue's environment detection can change.
- The probe runs the pyodide directory the npm package lays out; the
  shipping layout (a noarch conda package of the same files) is not
  built yet.

## 6. Building and running the probe

    conda create -n v8embed -c realthunder -c conda-forge --override-channels \
        v8-embed cxx-compiler cmake ninja
    conda run -n v8embed cmake -S src/App/PyodideHost -B build/pyodide-host -G Ninja
    conda run -n v8embed cmake --build build/pyodide-host
    build/pyodide-host/pyodide_probe ~/works/sw/pyodide/npm-314.0.6

`~/works/sw/pyodide/npm-314.0.6` is the `pyodide` npm package as
installed by the 2026-09-01 bench (`npm install pyodide@314.0.6`),
kept beside `pybench-rig-20260901/`, the node-side rig it is compared
against.  The probe exits non-zero on any failed check.

## 7. Phase 1, step 1: the guest toolchain (built 2026-09-02)

Pyodide 314.0.6 is CPython 3.14.2 built with **emscripten 5.0.3**, ABI
`2026_0`.  An extension module for it must be built with that emscripten
against pyodide's own Python headers, and shipped as a wheel:

- `~/works/sw/emsdk-5.0.3` -- a second emsdk clone pinned to 5.0.3, so
  the wasm viewer's emsdk (6.0.6) is never touched.  Both emsdk and
  emcc want Python >= 3.10 and the system python3 is 3.8; and
  `emsdk_env.sh` CLEARS `EMSDK_PYTHON` while it runs.
  `src/App/PyodideHost/guest/emsdk-env.sh` handles both.
- `~/works/sw/pyodide/venv-build` -- `pyodide-build 0.39.0` under a
  Python **3.14** (conda env `py314`): the tool refuses an xbuildenv
  whose Python differs from the host's.  Its cross-build environment
  for 314.0.6 sits at `~/works/sw/pyodide/xbuildenv/` (headers at
  `.../cpython/installs/python-3.14.2/include/python3.14`).
- The flags are pyodide's own (`Makefile.envs`, pyodide-build's
  `pywasmcross`): compile `-fPIC -fwasm-exceptions -sSUPPORT_LONGJMP=wasm
  -DPY_CALL_TRAMPOLINE`, link `-sSIDE_MODULE=2 -sWASM_BIGINT` with the
  `PyInit` exported; extension suffix `.cpython-314-wasm32-emscripten.so`.
- `guest/make_wheel.py` wraps .so files into a wheel with the tag
  pyodide checks (`cp314-cp314-pyodide_2026_0_wasm32`) and a RECORD.
  `py.loadPackage(<path>)` reads it through the scoped reader.

Proven with `guest/hello_ext.c`: built, wrapped, loaded in the probe
(`FCX_PROBE_WHEEL` / `FCX_PROBE_PY` / `FCX_PROBE_EXPECT`), answered.

## 8. Phase 1, step 2: the guest wheel `fcx_image` (built 2026-09-02)

The WASI image is not just CPython: it carries FreeCAD's expression
parser and walker (the core carve), the Base math types, the generated
facades, the marshaller and the bridge, compiled into the guest.  The
pyodide guest carries the SAME slice:

- `src/App/ExpressionImage/ImageSources.cmake` -- the source lists,
  includes and defines, shared by both guests so they cannot drift.
- `ImageDispatch.{h,cpp}` -- the dispatcher and module setup moved out
  of `ImageMain.cpp` unchanged; both entry files call it.
  `ImageMain.cpp` keeps only the WASI exports, the interpreter start and
  the reply buffers.  The rebuilt WASI image passes its 49 gtests.
- `ImageBridge.cpp` -- a second transport under `FC_EXPR_PYODIDE`: a
  side module has no wasm imports of its own, so the bridge calls a
  Python callable the host installs (`_fcx_image.set_host`) with the
  CBOR request as `bytes` and takes the reply back through the buffer
  protocol, or `to_bytes()` for a JsProxy of a Uint8Array.
- `ImageModule.cpp` -- `PyInit__fcx_image`: no interpreter to start;
  registers the in-image `FreeCAD`/`App` module and `_fcx` in
  sys.modules, builds the eval globals, exposes `call(bytes) -> bytes`
  and `set_host(callable)`.
- `src/App/PyodideHost/guest/CMakeLists.txt` -- the emscripten cross
  project.  An `add_executable` with a `.so` suffix, NOT
  `add_library(MODULE)`: CMake's Emscripten platform archives a MODULE
  with emar, and an archive is not a side module.  Output:
  `dist/fcx_image-0.1-cp314-cp314-pyodide_2026_0_wasm32.whl` (1.9 MB
  .so; the whole slice compiled against Python 3.14 headers with zero
  errors).

Smoke-tested in the probe, through the wheel: `{"op":"eval","src":"1+2"}`
-> `{"ok":true,"val":3}`; `{"lang":"expr","src":"2 mm + 3 mm"}` ->
a `quantity` of 5.0 with the length unit; `"1 +"` -> `ParserError`;
`_fcx.op("len", 7)` reaches a JS host function and decodes its reply.
The host receives the request as a PyProxy of `bytes` and reads it
zero-copy with `getBuffer()`.

## 9. Phase 1, step 3: the host runtime (built 2026-09-02)

### Found along the way: two parity gaps the corpus gate could not see

Running the full Python suite with sandbox routing ON (by accident: a
scratch user home that still carried `Evaluate=1`) failed three
`TestSpreadsheet` cases identically on BOTH runtimes -- so in the shared
host/guest code, not in either transport:

- **Literals lost bits on the wire.**  The router shipped
  `expr->toString()`, and `NumberExpression::_toString` prints 15
  significant digits (`digits10`, an upstream display decision), so an
  in-memory literal like `1.000000000000001` reached the guest as `1`
  and `1 >= 1.000000000000001 ? 0 : 1` flipped.  Computed values were
  exact -- `1 + 1e-15` crossed with every bit -- which is what separated
  the wire from the serialisation.  Saved documents already carry the
  15-digit form, which is why 334 stored expressions could never show
  it.  Fix: `App::expressionNumberPrecision()`, a thread-local the
  router raises to `max_digits10` (17) around the serialisation it
  ships; display and persistence keep 15.
- **Error text carried the type name.**  The image's `PyException`
  stub composed `RuntimeError: Cannot invert singular matrix` where the
  native evaluator's `what()` is the bare message, so a cell showed
  `ERR: RuntimeError: ...`.  The type already crosses in the reply's
  `exc` field; the stub now sets `what()` alone.

Rule that came out of it: never run the Python suite under a
`FREECAD_USER_HOME` where `setRouting(True)` was called, and
`TestSpreadsheet` with routing ON is a parity check the gate is not.

`ImageHost` now speaks to its guest through `ImageRuntime`
(`src/App/ExpressionImageRuntime.h`: name / resolve / initialize /
roundTrip / teardown, plus the bridge callback the guest's mid-eval ops
travel back through).  Everything above it -- the handle table, the
bindings pack, `dispatchHostOp`, result decoding -- is untouched and
shared.  Two runtimes:

- `ExpressionWasmtimeRuntime.cpp` -- the wasmtime code moved out of
  ImageHost unchanged.  Name `wasi`, the default.
- `ExpressionPyodideRuntime.cpp` -- the probe, grown up: V8 initialised
  once per process, an isolate and context owned for the session, the
  shim and the glue (`src/App/PyodideHost/pyodide_glue.js`) compiled in
  through `embed_js.py`, pyodide booted from the scoped directory, the
  `fcx_image` wheel loaded, the guest's bridge callable wired to a native
  `__fcx_bridge`.  Name `pyodide`.

Selection: the preference `Expression/Sandbox:Runtime`, else the
`FCX_RUNTIME` environment (how tests and the corpus gate pick a runtime
per process), else `wasi`.  Paths for pyodide: `PyodideDir` /
`PyodideWheel` preferences, else `FCX_PYODIDE` / `FCX_PYODIDE_WHEEL`,
else `<datadir>/Pyodide`; the wheel defaults to the first
`fcx_image-*.whl` in the directory.  Build: `BUILD_EXPR_PYODIDE_HOST`
defaults to whether `v8-embed` is found (it is installed in the dev
env), `FREECAD_PYODIDE_DIR` installs and mirrors a distribution.

**Result: the 69 ExpressionImage / ExpressionRouting /
ExpressionSecurity gtests pass on BOTH runtimes**, so the pyodide guest
honours the same seam contract -- typed values, quantities, bool
identity, tuples, handles and their release, facades, permission gates,
the parity of parser errors, and the routing tests through the real
expression engine.  The one bug on the way: `mod.call` is a BORROWED
attribute proxy in pyodide and died with `mod.destroy()`; `copy()` it.

**The corpus gate on the pyodide runtime** (`FCX_RUNTIME=pyodide
scripts/expr-switchover/run_gate.sh`, 2026-09-02) reads exactly as on
the WASI image: 350 files, 334 expressions compared, 332 same, 0
differ, 2 both_error (the same two, same text), 0 image-only errors, 1
timed-out file (the same slow-opening issue474 document).  Every stored
expression in the field corpus evaluates identically through pyodide.

What one round trip costs on each runtime (ExpressionImageBenchTest,
this box, 2026-09-02, with the FIRST transport -- see below for the
buffered one):

    bench                        pyodide      wasi
    transport floor               40.5 us     2.7 us
    wire floor (eval "1")         89.0 us    12.8 us
    expression 1+2*3-4/5          86.9 us    12.8 us
    one property                 106.6 us    22.2 us
    one bridge hop              +136.8 us   +28.9 us
    instantiate + first eval    1700   ms    14   ms

The engine is not the cost: the probe measured a Python call at 0.53 us
on the same V8.  The cost was the BYTE TRANSPORT as first written -- a
JsProxy minted per request (`to_bytes()`), a PyProxy plus `getBuffer()`
plus a copy per reply, and the same again for every bridge hop.

**The buffered transport** (same day) removes the proxies: the guest
owns two `bytearray`s (`_fcx_image.buffers()`), the glue holds
`getBuffer()` views of them -- typed arrays straight into wasm memory,
re-acquired when a view detaches because memory grew or a buffer was
resized -- writes the request into one, calls `call_len(n)`, and reads
the reply out of the other; the bridge goes the other way through the
same pair (`set_host_buffered`).  Only integers cross the language
boundary.  Measured:

    bench                        first     buffered     wasi
    transport floor               40.5 us    8.4 us     2.6 us
    wire floor (eval "1")         89.0 us   39.0 us    13.2 us
    expression 1+2*3-4/5          86.9 us   36.9 us    14.0 us
    one property                 106.6 us   51.5 us    23.0 us
    one bridge hop              +136.8 us  +72.1 us   +28.8 us

Roughly 3x the WASI image now.  The remaining gap is not in the
crossing (8 us floor) but inside the guest: the same C++ dispatcher
runs ~25 us slower as an emscripten SIDE MODULE than as a wasi reactor,
which is what `-fPIC` dynamic linking costs (every call and global goes
through the table and GOT).  Options, in order of return: build the
dispatcher's hot path with `-O3` and fewer indirections, or have the
host skip CBOR for the common scalar shapes.  Neither is needed for
correctness, and 40 us per expression is far from user-visible.

1. DONE (section 9), corpus gate and buffered transport included.
2. Marshalling: FcxWire is CBOR over a byte buffer; on this runtime the
   cheaper path is direct V8 values (the 45 ns C++ -> V8 hop measured
   2026-09-01), with the bindings pack handed over as a JS object.
   Measure before choosing.
3. Watchdog on a real timer budget (the mechanism is proven), and the
   wasm memory ceiling (section 5).
4. Packaging: `v8-embed` is on the channel for linux-64/aarch64,
   win-64 and (pending) both macOS; the pyodide files are one noarch
   package (`pyodide-dist`, ~17 MB with numpy) to be recipe'd like the
   image was going to be.
