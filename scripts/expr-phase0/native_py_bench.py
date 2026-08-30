# native CPython twin of the wasm bench (same workloads)
import time, json, math
r = {}
N = 200000
code = compile("1 + 2*3 - 4/5", "<e>", "eval")
t0 = time.perf_counter()
for _ in range(N):
    eval(code)
r["eval-precompiled-arith"] = (time.perf_counter() - t0) / N * 1e6
M = 20000
t0 = time.perf_counter()
for _ in range(M):
    eval(compile("1 + 2*3 - 4/5", "<e>", "eval"))
r["parse+eval-arith"] = (time.perf_counter() - t0) / M * 1e6
a, b = 3.7, 1.2
t0 = time.perf_counter()
for _ in range(N):
    _ = a + b * a - b / a
r["float-ops-x4"] = (time.perf_counter() - t0) / N * 1e6
class V:
    __slots__ = ("x",)
    def __init__(s): s.x = 1.0
    def get(s): return s.x
v = V()
t0 = time.perf_counter()
for _ in range(N):
    _ = v.get() + v.x
r["attr+method"] = (time.perf_counter() - t0) / N * 1e6
t0 = time.perf_counter()
for _ in range(N):
    _ = math.sin(0.5236)
r["math.sin"] = (time.perf_counter() - t0) / N * 1e6
for k, v2 in r.items():
    print("NATIVEPY %-28s %.3f us" % (k, v2))
