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

- **Resource limits.**  wasmtime gave the WASI image fuel metering and
  epoch interruption for free.  V8 has `Isolate::TerminateExecution`
  from another thread and heap limits in `ResourceConstraints`; an
  expression that loops forever needs the former wired to a watchdog.
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

## 7. What comes next (phase 1, not started)

1. Turn the probe into `App::PyodideHost`: isolate + context owned for
   the session (the 1.5 s boot happens once), the shim installed from a
   resource, a `PyodideRuntime` behind the same seam the WASI image
   uses (`ImageHost::eval` / FcxWire), so the corpus gate runs against
   it unchanged.
2. Marshalling: FcxWire is CBOR over a byte buffer; on this runtime the
   cheaper path is direct V8 values (the 45 ns C++ -> V8 hop measured
   2026-09-01), with the bindings pack handed over as a JS object.
   Measure before choosing.
3. Watchdog + heap limit (section 5).
4. Packaging: `v8-embed` is on the channel for linux-64/aarch64,
   win-64 and (pending) both macOS; the pyodide files are one noarch
   package (`pyodide-dist`, ~17 MB with numpy) to be recipe'd like the
   image was going to be.
