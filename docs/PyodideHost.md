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

**THE runtime since 2026-09-03** (user direction, `docs/SandboxNetwork.md`
sec 0): pyodide is the sandbox FreeCAD ships and develops; the WASI
image is the reference implementation, built only on request.  The
default runtime name is `pyodide`; a build without v8-embed and
without `BUILD_EXPR_WASI_RUNTIME` has no sandbox host at all and
evaluates in-process as before.  The network permission model, the
missing-package install flow, and the "everything Python in pyodide"
switch are designed in that document, not here.

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
  *Time: DONE, section 10 -- and the caveat was taken seriously: the
  common case is a cooperative interrupt the interpreter raises itself,
  and a hard termination drops the instance rather than trusting it.
  Memory: still open.*
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

**The indirections, removed (same day).**  `-fPIC` makes every call to
a non-hidden symbol interposable, so it goes through the table even
when the callee sits in the same module; `-fvisibility=hidden` on the
guest (only `PyInit__fcx_image` keeps default visibility, through
`PyMODINIT_FUNC`) lets the compiler call directly, and `-flto` lets it
inline across the slice's translation units.  Two flags in
`guest/CMakeLists.txt`, the wheel shrank from 427 to 368 KB, 73/73
gtests, TestSpreadsheet with routing on, and the corpus gate unchanged.
Measured (two runs each, budget on):

    bench                        before        after
    transport floor              10.6 us        5.4 us
    transport 1 kB               34-46         14.1-14.4
    wire floor (eval "1")        38.6-41.3     30.6-37.6
    expression 1+2*3-4/5         36.7-41.7     25.6-33.2
    one property                 51.7-56.7     36.1-37.2
    one bridge hop              109.9-116.1    78.2-80.3

About 2x the WASI image now, and the biggest single lever was the
cheapest.  What is left is mostly pyodide's own JS<->Python crossing
(the 5 us floor is `callLen(n)` through a PyProxy plus two typed-array
copies) and V8 tiering variance, which the spread between runs shows.

1. DONE (section 9), corpus gate and buffered transport included.
2. Marshalling: FcxWire is CBOR over a byte buffer; on this runtime the
   cheaper path is direct V8 values (the 45 ns C++ -> V8 hop measured
   2026-09-01), with the bindings pack handed over as a JS object.
   Measure before choosing.
3. DONE (section 10): the time budget.  Still open from section 5: the
   wasm memory ceiling.
4. Packaging: `v8-embed` is on the channel for linux-64/aarch64,
   win-64 and (pending) both macOS; the pyodide files are one noarch
   package (`pyodide-dist`, ~17 MB with numpy) to be recipe'd like the
   image was going to be.

## 10. The time budget (built 2026-09-02)

A guest that never returns -- `while True` in a python-mode cell, a
pathological comprehension, `10 ** 10 ** 8` -- used to hang FreeCAD
exactly as the native evaluator does.  The sandbox now bounds it, on
both runtimes, through `ImageRuntime` (`src/App/ExpressionImageRuntime.h`,
`Outcome`), in two stages:

- **Soft**: at `BudgetMs` the host asks the guest to stop ITSELF.  On
  pyodide this is the interrupt buffer CPython's emscripten port polls
  from its eval loop (`Py_EMSCRIPTEN_SIGNAL_HANDLING`; pyodide exposes
  it as `setInterruptBuffer`): an `Int32Array` whose single word is a
  `std::atomic<int32_t>` inside the runtime object, wrapped as a
  `SharedArrayBuffer`, so the watchdog thread writes SIGINT (2) into it
  with no engine involvement -- the same way a browser's main thread
  interrupts a worker.  The interpreter raises `KeyboardInterrupt` at
  its next bytecode check, the exception travels the ordinary path
  through the dispatcher's `PyErr_Fetch`, and the guest is exactly as
  consistent afterwards as after any other error.  `ImageHost` sees the
  reply's `KeyboardInterrupt` together with the runtime's `Interrupted`
  outcome and rewrites it to `TimeoutError` (a `KeyboardInterrupt` the
  guest raised on its own is left alone).  If the signal was raised but
  the guest finished first, a pending interrupt could surface in the
  NEXT evaluation, so the host absorbs it in a throwaway `eval "0"`.
- **Hard**: `GraceMs` later, if the guest is still running -- it is in
  native code (`sum(iter(int, 1))` never executes a bytecode), or it
  caught the interrupt -- the engine is stopped from outside:
  `Isolate::TerminateExecution` on V8, an epoch tick on wasmtime
  (`epoch_interruption`, deadline 1 tick before each call, the watchdog
  thread increments).  That unwinds the guest through frames CPython
  never cleaned up, so the probe's "runPython still works afterwards"
  is not trusted: `ImageHost` drops the instance, answers
  `TimeoutError`, and the next evaluation starts a fresh one (14 ms on
  wasi, 1.7 s on pyodide).

The wasi runtime has no soft stage -- the WASI CPython has no
interrupt buffer to poll -- so there every runaway takes the hard path,
which costs little since its instance is cheap.  Preferences, under
`BaseApp/Preferences/Expression/Sandbox`: `BudgetMs` (default 5000, 0
= unbounded) and `GraceMs` (default 1000).  `ImageHost` caches them
through a parameter observer (a DOM lookup per trip measured 2.5 us)
and hands them to the runtime before every trip, so a change applies
at once -- with one exception: wasmtime compiles the epoch checks into
the guest, so an instance started with `BudgetMs = 0` has none (the
two compilations keep separate `.cwasm` cache files); a budget set
later takes effect at the next reset.  Pyodide's polling is toggled with the
budget on the fly.

The watchdog is one thread per runtime (`Watchdog` in the same header),
armed before and disarmed after each call.  Two details that cost
measurements to get right: fire() runs under the same lock disarm()
takes, so a stale deadline can never reach the next call; and the
thread is only woken when its pending timed wait ends AFTER the new
deadline -- a `notify` per call was 2 us of futex traffic.

Tests: `ExpressionImageBudgetTest` (4 cases, both runtimes): a
bytecode loop is stopped and, on pyodide, the instance survives (a
marker set in `sys` before is still there after); a native loop is
terminated and the instance dropped on both; the survivor is a whole
interpreter (quantities, an error, a value); `BudgetMs = 0` evaluates
unbounded.  73/73 sandbox gtests on both runtimes.

What the budget costs per expression (ExpressionImageBenchTest, this
box, 2026-09-02, budget 5000 vs 0, two runs each):

    bench                        wasi 0    wasi 5000    pyodide 0    pyodide 5000
    transport floor               2.6 us     3.1 us      8.7-11.5     8.4-10.1
    wire floor (eval "1")        13.0       16.0         38.3-38.6    39.1-43.8
    expression 1+2*3-4/5         13.5       15.0         36.6-36.9    37.5-47.6
    one property                 22.2       24.5         51.9-52.0    51.0-51.9

On wasi about 1.5 us (11%): ~1 us of epoch checks in the guest, the
rest arming.  On pyodide the polling is inside the run-to-run noise.

## 11. The bootstrap and the package flow: the plan (2026-09-03, for the next session)

The user's direction of 2026-09-03 (no `pyodide-dist` feedstock;
FreeCAD bootstraps pyodide on the user's host; users choose packages)
and the package-install offer of `docs/SandboxNetwork.md` sec 9 are
built FIRST, before the network capability and before the GUI protocol
of `docs/SandboxGui.md`.  The reasons, in order of weight:

1. **It is the ship path.**  With the dist feedstock dropped, no user
   can run the pyodide runtime until the bootstrap exists; everything
   built after it is unreachable in a release without it.
2. **The package offer IS mostly the bootstrap installer** (download,
   verify, `packages/` + manifest, `loadPackage` at boot); P1 adds a
   scan, a finder and a dialog on top.
3. **G1 needs the same loader.**  Draft's and BIM's own Python must
   reach the guest as wheels through this machinery; the "FreeCAD
   modules in the guest" story is the package story with a local
   source.
4. It yields the first user-visible payoff of the whole arc with the
   smallest new surface: `import numpy` in a spreadsheet Python cell,
   an offer, an install, a value.
5. It settles two things every later step inherits: the supported
   pyodide version range, and how the `fcx_image` wheel is
   distributed.

### What the release actually offers (checked 2026-09-03)

GitHub release `314.0.6` assets: `pyodide-core-314.0.6.tar.bz2`
(6.8 MB, the runtime without packages), `pyodide-314.0.6.tar.bz2`
(350 MB, everything), `static-libraries` and `xbuildenv` tarballs.
**No checksum asset.**  The npm package on jsDelivr
(`https://cdn.jsdelivr.net/npm/pyodide@314.0.6/`) is 14 files, of
which four matter: `pyodide.asm.wasm` 9.6 MB, `python_stdlib.zip`
2.5 MB, `pyodide.asm.mjs` 1.25 MB, `pyodide-lock.json` 114 KB; the
lock carries a sha256 per package wheel, the runtime files carry
none.  Consequence: **FreeCAD pins the sha256 of the runtime files per
supported version** in a small table in the tree, which is also the
supported-version list -- a version not in the table cannot be
installed, and the table is widened only after re-auditing the shim
against the new release (the allowlist rule of sec 5).  Package
wheels are verified against the lock; PyPI wheels against PyPI's
hashes.

### Steps

- **B1 -- layout and resolve order.**  `<user app data>/Pyodide/
  <version>/` for the runtime files, `<user app data>/Pyodide/
  packages/` + `manifest.json` beside the versions, a `current`
  marker.  `resolve()` in `ExpressionPyodideRuntime.cpp` puts the
  user-data location FIRST after the explicit preference and
  environment, before `<datadir>/Pyodide` (which stays for dev trees
  and for a package that chooses to bundle).  The scoped root widens
  from the version dir to the `Pyodide/` dir so `packages/` is
  readable by the loader and nothing else is.  While in there: the
  Windows/macOS path handling (`weakly_canonical` on drive letters,
  the `/` prefix test in `scope()`), untested so far.  The binary's
  supported-version table and a loud refusal at boot for a version
  outside it.
- **B2 -- the runtime installer.**  A Python module on the HOST (host
  infrastructure, not user code, so it may use the native interpreter
  and the Addon Manager's `NetworkManager` for proxies and
  certificates; the C++ `NetClient` arrives with N1 and can replace it
  later): `install_runtime(version, source)` with `source` = the
  GitHub core tarball, the jsDelivr per-file set, or a LOCAL tarball
  for air-gapped boxes; downloads to a temp dir, verifies against the
  pinned table, moves into place atomically, updates `current`.
  `list_runtimes()`, `remove_runtime(version)`.  Explicit user action
  only -- nothing downloads at startup.
- **B3 -- the `fcx_image` wheel.**  Bundled in the FreeCAD package
  under `<datadir>/Pyodide/wheels/` (368 KB, matched to the build by
  construction; an ABI change waits for a release, accepted for now),
  with the PyPI route (`fcx-image==<FreeCAD version>`, one file per
  ABI tag, a Linux CI job with emsdk) recorded as the later upgrade
  path.  The installer refuses a runtime whose ABI tag has no matching
  wheel.
- **B4 -- the package installer.**  `install_package(name)`: resolve
  against the lock first (index packages, dependencies from the lock),
  PyPI second (PEP 783 `pyemscripten_<abi>` wheels and pure wheels,
  metadata from PyPI's JSON API, dependencies resolved host-side);
  download into `packages/`, verify, write the manifest.  Boot loads
  the manifest through `loadPackage` from the local files.  First cut:
  index packages only; PyPI in the same session if time allows.
- **B5 -- the offer (P1).**  The pre-run import scan over the source
  the host is about to run, the last-in-line `sys.meta_path` finder
  with the `pkg.missing` op, `PackageNeeded` for document principals,
  the session-principal dialog, deferred install and retry.  Gate:
  `import numpy` in a spreadsheet Python cell with routing ON produces
  the offer, the install, and the value on retry; with routing OFF
  nothing changes.
- **B6 -- the in-place probe (P2), time permitting.**  `unpack_buffer`
  plus a synchronous import of numpy, then scipy over openblas,
  without `loadPackage`; the list in `SandboxNetwork.md` sec 9.3.
- **B7 -- tests.**  gtests for resolve order, the version table and
  the scope widening; a Python test that installs from a LOCAL tarball
  (no network in CI) and from a local `packages/` mirror; path tests
  that run the Windows shapes on Linux by construction.

After this arc: G0 (the porting linter, cheap, any time), then G1
(Draft/BIM App side in the guest), then N1 (the network policy
engine), in that order unless the user says otherwise.

## 12. The bootstrap and the package flow: what was built (2026-09-03)

Sec 11's B1-B5 and B7, in one arc.  B6 (the in-place probe: a wheel
loaded into a RUNNING guest from inside a synchronous import) was not
attempted; the deferred path below is what ships, and it is the one
that always works.  PyPI (PEP 783) sources are not built either: the
installer names only what the lock file names, and says so.

### 12.1 The layout (B1)

    <user app data>/Pyodide/
        current                 the version the runtime boots (one line)
        314.0.6/                pyodide.js pyodide.mjs pyodide.asm.mjs
                                pyodide.asm.wasm python_stdlib.zip
                                pyodide-lock.json package.json
        packages/               the user's chosen wheels, under their
            manifest.json       lock-file names, plus the manifest
    <datadir>/Pyodide/wheels/   fcx_image-<ver>-cp314-cp314-pyodide_<abi>_wasm32.whl
                                (FreeCAD's own guest, shipped with the build)

`App::ExpressionSandbox::Pyodide` (`src/App/ExpressionPyodide.h/.cpp`)
is the single place these facts live: `releases()` (the pinned table),
`layout()`, `verifyDirectory()`, `scopePath()`, the lock lookups and
`missingImport()`.  `FreeCAD.ExpressionSandbox.pyodideReleases()`,
`pyodideLayout()`, `pyodideVerify(dir)` and `pyodideAbi(dir)` hand the
same facts to Python.  Overrides for tests and odd boxes: preferences
`PyodideUserDir` / `PyodidePackages`, environment `FCX_PYODIDE_USER` /
`FCX_PYODIDE_PACKAGES`.

**Resolve order** (`PyodideRuntime::resolve`): explicit `configure()`,
preference `PyodideDir`, `FCX_PYODIDE`, then **the user-data runtime
`current` names**, then `<datadir>/Pyodide` (a dev tree mirrors the npm
directory there with the wheel beside it; a package may bundle one).
The wheel: explicit, `PyodideWheel`, `FCX_PYODIDE_WHEEL`, an
`fcx_image-*.whl` beside the runtime, else the shipped one whose ABI
tag matches the runtime's (`wheelForAbi`).  A wheel whose tag does not
match the runtime's `abi_version` is refused at boot.

**The pinned table.**  One entry per supported version: the six
runtime files with their sha256 (taken from both the npm CDN and the
GitHub core tarball, which agree byte for byte), the ABI tag, the
CPython version, the core tarball's name and hash.  `initialize()`
verifies the directory it boots -- version from `package.json`, then
every file's hash (~14 MB, tens of milliseconds, once per instance) --
and REFUSES anything else.  `FCX_PYODIDE_UNPINNED=1` or the preference
`PyodideUnpinned` turns the refusal into a warning, for bringing up a
new release before the table is widened; the warning repeats on every
boot.

**Scoping.**  The reader and the module loader are confined to a list
of canonical roots -- the runtime directory, the wheel's directory, and
the package set -- rather than one directory: three named places, not
a widened parent.  `scopePath()` compares canonical forms component by
component (a sibling sharing a root's prefix as a string is outside),
case-insensitively on Windows, understands `file:///C:/x`, and follows
symlinks before deciding.  Paths cross into JavaScript as generic
(forward-slash) strings: pyodide's loader does URL arithmetic on them.
One shell-mode fact shapes the reader: pyodide's `resolvePath` is the
IDENTITY there, so the loader asks for a lock-file wheel by its bare
file name and `packageBaseUrl` never reaches the reader.  A relative
name therefore resolves against the runtime directory first and the
package set second (`scopePath`'s `fallbacks`), existing entries only,
still confined to the roots.

### 12.2 The runtime installer (B2): `freecad.pyodide`

Host Python (`src/Ext/freecad/pyodide/__init__.py`), never sandboxed
code.  `install_runtime(version=None, source="github", progress=None)`:

- `github`: the release asset `pyodide-core-<ver>.tar.bz2` (6.8 MB),
  verified against its pinned hash, and the six files plus
  `package.json` read out of it by name (nothing else in the archive
  is touched);
- `jsdelivr`: the npm package file by file
  (`https://cdn.jsdelivr.net/npm/pyodide@<ver>/<file>`);
- a LOCAL tarball or directory, for air-gapped boxes.

Every file is verified against the table before anything moves; the
files are staged beside the target and renamed into place in one step;
the binary's own `pyodideVerify` runs on the result; `current` is
written last.  A runtime whose ABI has no shipped `fcx_image` wheel is
refused up front.  With a GUI up, downloads go through the Addon
Manager's `NetworkManager` (its proxy and certificate handling apply);
headless, through `urllib`.  `list_runtimes()`, `set_current()`,
`remove_runtime()` complete the set.  **Nothing runs at startup**: the
status-bar padlock offers the install when the sandbox is switched on
with no runtime present (`SandboxIndicator::toggleRouting`), and the
console can call it.

### 12.3 The wheel (B3)

`FREECAD_FCX_IMAGE_WHEEL=<path>` installs the wheel under
`<datadir>/Pyodide/wheels/` and mirrors it into the build tree;
`FREECAD_PYODIDE_DIR` keeps bundling a whole distribution for dev
trees.  The PyPI route (`fcx-image==<FreeCAD version>`, one file per
ABI tag) stays the recorded later upgrade path.

### 12.4 The package installer (B4)

`install_package(name, source="index")`: resolves `name` and its
`depends` against the ACTIVE runtime's lock (depth first, dependencies
before dependents, installed ones skipped), fetches each wheel from
pyodide's CDN (`https://cdn.jsdelivr.net/pyodide/v<ver>/full/<file>`)
or a local mirror directory, verifies it against the lock's sha256,
writes it into `packages/` atomically, and rewrites `manifest.json`:

    {"version": 1, "runtime": "314.0.6", "abi": "2026_0",
     "packages": {"numpy": {"version": ..., "file_name": ..., "sha256": ...,
                            "depends": [], "requested_by": "session", ...}},
     "load_order": ["numpy"]}

Boot: `__fcx_boot(root, wheel, packagesDir, names)` passes
`packageBaseUrl: packagesDir` to `loadPyodide`, so pyodide's own
loader fetches lock-file packages from the user's set (through the
scoped reader, which now has that root), and `loadPackage(names)` in
manifest order right after the fcx_image wheel.  A manifest written
for another ABI is ignored with a warning, and the installer starts a
new set when the runtime's ABI differs.  A package that fails to load
is reported and skipped; the import says what is wrong.  Not in the
lock -> "PyPI packages are not supported yet".

### 12.5 The offer (B5), P1 of `SandboxNetwork.md` sec 9

- **The finder.**  `pyodide_glue.js` installs, at boot, a
  `sys.meta_path` finder appended LAST.  Its `find_spec` asks the host
  ONE bridge op, `pkg.missing {a: name}` (`FcxWire::OpPkgMissing`, no
  handle), and gets a string back: empty = unknown (return `None`, the
  ordinary `ModuleNotFoundError` follows); otherwise the message to
  raise as `ModuleNotFoundError`.  Nothing is ever loaded from inside
  an import.
- **The host answer** (`Pyodide::missingImport`): the lock's import
  map (`imports` per package, 304 names) gives the package; if the
  manifest has it, INSTALLED -- the host calls
  `ImageHost::scheduleReset()` and the instance is dropped after this
  round trip, so the next evaluation boots with the package; else OFFER
  -- `ExpressionSecurity::Runtime::requestPending(PkgInstall, name)`
  records a pending request for the current principal and owner (or
  `session` outside any evaluation), audited as a prompt, repeats
  collapsed.
- **`pkg.install:<name>`** is a new `Permission` value, but an ACTION,
  not a grant: `check()` never resolves it, and the panel's allow
  buttons on such a row run `freecad.pyodide.install_package(name)`
  (whatever scope was clicked), clear the request, reset the sandbox,
  and recompute the objects that were blocked -- the same re-run path
  a grant takes.  Deny clears the request.  The package is then
  available to every principal, as sec 9.4 says.
- **Pre-run scan**: not built.  The expression language reaches a
  module only through `import` statements or `import_py()`, both of
  which fail at the guest's import with the offer above and no host
  work wasted; the scan would only save the one failed evaluation.

The gate of sec 11 holds as a Python test (`SandboxPyodide`): with
routing ON, `import numpy; float(numpy.sqrt(4.0))` in python mode on a
document object raises the offer and records `pkg.install:numpy`
against that object; `install_package("numpy", source=<mirror>)` puts
the wheel in the set and clears the request; the same evaluation then
returns 2.0 from a guest that booted with numpy.  With routing OFF
nothing here runs.

### 12.6 Tests (B7)

- `tests/src/App/ExpressionPyodide.cpp` (in `Tests_run`, pyodide host
  builds only): the table, `verifyDirectory` refusing unpinned and
  tampered directories and passing on the staged one, `askedToPath`
  on the Windows URL shapes, `scopePath` (in, out, `..`, siblings, the
  root itself, a symlink out), `layout()` over fake runtimes and the
  `current` marker (a path or a missing version is ignored), the
  resolve order (environment > user data > bundle) through
  `ImageHost::location()`, the lock lookups, the manifest's load order
  and ABI refusal, and the offer end to end on the real guest
  (`missingImportIsOfferedThenLoadedAfterInstall`).
- `src/Mod/Test/SandboxPyodide.py`: `install_runtime` from a LOCAL
  source (the staged `<datadir>/Pyodide`, or `FCX_PYODIDE_CORE_TARBALL`)
  into a scratch user directory, the wrong-hash refusal, the resolve
  order picking the install up, boot, then the package flow above from
  the staged directory as the mirror.  No network anywhere.
