# Phase 0 deliverable (c): native value-layer measurement.
# Run under FreeCADCmd. ASCII only. Bounded: every loop is capped by count.
import time, sys, json, gc

import FreeCAD as App
import Part

results = {}

def bench(name, fn, n, unit_per_call=1):
    fn()  # warm
    gc.disable()
    t0 = time.perf_counter()
    fn()
    t1 = time.perf_counter()
    gc.enable()
    per = (t1 - t0) / (n * unit_per_call) * 1e6
    results[name] = per
    print("BENCH %-42s %10.3f us/eval  (n=%d, total %.3fs)" % (name, per, n, t1 - t0))

doc = App.newDocument("bench")
box = doc.addObject("Part::Box", "Box")
obj = doc.addObject("App::FeaturePython", "Scratch")
doc.recompute()

N = 20000
ev = obj.evalExpression

def loop(expr, n=N):
    def run():
        for _ in range(n):
            ev(expr)
    return run

# 1. parse+eval per expression class (evalExpression = parse + eval each call)
bench("arith  1+2*3-4/5",              loop("1 + 2*3 - 4/5"), N)
bench("arith  ternary+cmp",            loop("1 < 2 ? 10 : 20"), N)
bench("qty    10mm+5mm*2",             loop("10 mm + 5 mm * 2"), N)
bench("func   sin(30deg)",             loop("sin(30 deg)"), N)
bench("str    <<a>>+<<b>>",            loop("<<a>> + <<b>>"), N)
bench("prop   Box.Shape.Volume",             loop("Box.Shape.Volume"), N)
bench("prop   Box.Height",             loop("Box.Height"), N)
bench("chain  Box.Placement.Rot.Angle",loop("Box.Placement.Rotation.Angle"), N)
bench("shape  Box.Shape.BoundBox.ZMin",loop("Box.Shape.BoundBox.ZMin"), 2000)
bench("vector vector(1,2,3).Length",   loop("vector(1, 2, 3).Length"), N)

# 2. eval-only cost via a cached binding: PropertyExpressionEngine caches the AST;
#    touch the input and recompute -> re-eval without re-parse.
sheetdep = doc.addObject("Spreadsheet::Sheet", "Dep")
sheetdep.set("A1", "1")
doc.recompute()

# 3. big sheet, cell-chain arithmetic (the pure by-value world), 10000 cells
sheet = doc.addObject("Spreadsheet::Sheet", "Big")
NC = 10000
cols = 10
rows = NC // cols
letters = [chr(ord('A') + c) for c in range(cols)]
sheet.set("A1", "1")
for r in range(1, rows + 1):
    for c in range(cols):
        cell = "%s%d" % (letters[c], r)
        if r == 1 and c == 0:
            continue
        if c == 0:
            sheet.set(cell, "=%s%d + 1" % (letters[cols-1], r-1))
        else:
            sheet.set(cell, "=%s%d * 2 + %d" % (letters[c-1], r, c))
t0 = time.perf_counter()
doc.recompute()
t1 = time.perf_counter()
print("BENCH %-42s %10.3f us/cell  (n=%d, total %.3fs)" % ("sheet  10k-cell chain recompute", (t1-t0)/NC*1e6, NC, t1-t0))
results["sheet-10k-recompute"] = (t1-t0)/NC*1e6
# re-recompute with touch (ASTs cached)
sheet.set("A1", "2")
t0 = time.perf_counter()
doc.recompute()
t1 = time.perf_counter()
print("BENCH %-42s %10.3f us/cell  (n=%d, total %.3fs)" % ("sheet  10k re-recompute (cached AST)", (t1-t0)/NC*1e6, NC, t1-t0))
results["sheet-10k-rerecompute"] = (t1-t0)/NC*1e6

# 4. property-read-heavy sheet: 2000 cells of =Box.Shape.Volume / i  (the proxy-class read)
sheet2 = doc.addObject("Spreadsheet::Sheet", "Props")
NP = 2000
for i in range(NP):
    sheet2.set("A%d" % (i+1), "=Box.Shape.Volume / %d + Box.Height" % (i+1))
t0 = time.perf_counter()
doc.recompute()
t1 = time.perf_counter()
print("BENCH %-42s %10.3f us/cell  (n=%d, total %.3fs)" % ("sheet  2k Box.Shape.Volume cells recompute", (t1-t0)/NP*1e6, NP, t1-t0))
results["sheet-2k-proprefs"] = (t1-t0)/NP*1e6
box.Height = 11.0
t0 = time.perf_counter()
doc.recompute()
t1 = time.perf_counter()
print("BENCH %-42s %10.3f us/cell  (n=%d, total %.3fs)" % ("sheet  2k prop cells re-recompute", (t1-t0)/NP*1e6, NP, t1-t0))
results["sheet-2k-proprefs-re"] = (t1-t0)/NP*1e6

with open(__import__("os").environ.get("BENCH_OUT","/tmp/native_bench.json"), "w") as f:
    json.dump(results, f, indent=1)
print("DONE")
