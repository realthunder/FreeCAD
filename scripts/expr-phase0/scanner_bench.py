# Phase 0 (c): real bound-property assembly -- eval every stored expression
# of scanner.FCStd in place, timed. ASCII only.
import time, FreeCAD as App

doc = App.openDocument("/home/thunder/works/sw/models/scanner.FCStd")
exprs = []
for obj in doc.Objects:
    try:
        ee = obj.ExpressionEngine
    except AttributeError:
        continue
    for path, e in ee:
        exprs.append((obj, path, e))
print("SCANNER expressions bound:", len(exprs))

ok, failed = 0, 0
t_total = 0.0
per = []
R = 50
for obj, path, e in exprs:
    try:
        obj.evalExpression(e)
    except Exception as ex:
        failed += 1
        continue
    t0 = time.perf_counter()
    for _ in range(R):
        obj.evalExpression(e)
    dt = (time.perf_counter() - t0) / R * 1e6
    per.append((dt, e))
    t_total += dt
    ok += 1
print("SCANNER evaluable: %d, failed(ctx): %d" % (ok, failed))
if per:
    per.sort()
    print("SCANNER mean %.1f us  median %.1f us  p90 %.1f us  max %.1f us" % (
        t_total/ok, per[len(per)//2][0], per[int(len(per)*0.9)][0], per[-1][0]))
    print("SCANNER slowest 5:")
    for dt, e in per[-5:]:
        print("   %9.1f us  %s" % (dt, e[:100]))
    print("SCANNER fastest 3:")
    for dt, e in per[:3]:
        print("   %9.1f us  %s" % (dt, e[:100]))
