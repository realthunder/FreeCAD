// Phase 0 deliverable (c): CPython-wasm side of the measurement.
// Measures: (1) value-layer throughput of CPython-on-wasm vs whatever native
// python3 gives (run native_py_bench.py separately for the native number);
// (2) the JS<->wasm bridge-op round-trip cost (the browser-tier proxy hop).
import { loadPyodide } from "pyodide";

const t0 = performance.now();
const py = await loadPyodide();
const t1 = performance.now();
console.log(`STARTUP pyodide load+init: ${(t1 - t0).toFixed(0)} ms`);

const bench = `
import time, json
def run():
    r = {}
    N = 200000
    # arithmetic expression eval-shaped work: precompiled code object per eval
    code = compile("1 + 2*3 - 4/5", "<e>", "eval")
    t0 = time.perf_counter()
    for _ in range(N):
        eval(code)
    r["eval-precompiled-arith"] = (time.perf_counter() - t0) / N * 1e6
    # parse+eval per call (matches FreeCAD evalExpression = parse + walk)
    M = 20000
    t0 = time.perf_counter()
    for _ in range(M):
        eval(compile("1 + 2*3 - 4/5", "<e>", "eval"))
    r["parse+eval-arith"] = (time.perf_counter() - t0) / M * 1e6
    # number-protocol microcosts (what calc() does per operator)
    a, b = 3.7, 1.2
    t0 = time.perf_counter()
    for _ in range(N):
        _ = a + b * a - b / a
    r["float-ops-x4"] = (time.perf_counter() - t0) / N * 1e6
    # method call + attribute read on an object (facade-proxy shaped)
    class V:
        __slots__ = ("x",)
        def __init__(s): s.x = 1.0
        def get(s): return s.x
    v = V()
    t0 = time.perf_counter()
    for _ in range(N):
        _ = v.get() + v.x
    r["attr+method"] = (time.perf_counter() - t0) / N * 1e6
    # math funcs
    import math
    t0 = time.perf_counter()
    for _ in range(N):
        _ = math.sin(0.5236)
    r["math.sin"] = (time.perf_counter() - t0) / N * 1e6
    return json.dumps(r)
run()
`;
const resJson = py.runPython(bench);
const res = JSON.parse(resJson);
for (const [k, v] of Object.entries(res))
    console.log(`WASMBENCH ${k.padEnd(28)} ${v.toFixed(3)} us`);

// bridge-op round trip: Python-in-wasm calling a JS host function
globalThis.hostRead = (h, name) => 42.5;
py.runPython(`
import js, time, json
N = 100000
t0 = time.perf_counter()
for _ in range(N):
    _ = js.hostRead(1, "Volume")
hop = (time.perf_counter() - t0) / N * 1e6
print("WASMBENCH %-28s %.3f us" % ("js-hop hostRead(h,name)", hop))
`);
// and the reverse: JS driving a Python call per "eval request"
const evalFn = py.runPython(`
def evaluate(src):
    return eval(compile(src, "<e>", "eval"))
evaluate
`);
{
    const N = 20000;
    const t0 = performance.now();
    for (let i = 0; i < N; i++) evalFn("1 + 2*3 - 4/5");
    const t1 = performance.now();
    console.log(`WASMBENCH eval-request-from-JS         ${((t1 - t0) / N * 1000).toFixed(3)} us`);
}
console.log("DONE");
