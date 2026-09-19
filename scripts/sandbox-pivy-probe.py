#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Probe A (docs/Sandbox.md 7.10): pivy.coin inside the pyodide guest.

Boots the sandbox with the bundled pivy wheel beside fcx_image, imports
pivy.coin there, reads Coin's version, builds a 10 k node graph, runs
SoSearchAction and SoGetBoundingBoxAction over it and times each step
in the guest.  Run headless:

    cd build/conda-relwithdebinfo-801
    FREECAD_USER_HOME=/tmp/fchome ~/works/sw/fcad/.conda/run.sh \
      ./bin/FreeCADCmd ~/works/sw/fcad/scripts/sandbox-pivy-probe.py

Prints one line per fact; the guest's own output arrives on stderr.
"""

import os
import sys
import time

import FreeCAD

GUEST = r'''
import os, sys, time
t0 = time.perf_counter()
from pivy import coin
t1 = time.perf_counter()
sys.stderr.write("pivy: import %.3f s, Coin %s, %d names\n" % (t1 - t0, coin.SoDB.getVersion(), len(dir(coin))))
root = coin.SoSeparator()
t2 = time.perf_counter()
N = __N__
sys.stderr.write("pivy: building %d\n" % N)
for i in range(N):
    sep = coin.SoSeparator()
    tr = coin.SoTranslation()
    tr.translation.setValue(i % 100, i // 100, 0)
    sep.addChild(tr)
    cube = coin.SoCube()
    cube.width = 0.5
    sep.addChild(cube)
    root.addChild(sep)
t3 = time.perf_counter()
sa = coin.SoSearchAction()
sa.setType(coin.SoCube.getClassTypeId())
sa.setInterest(coin.SoSearchAction.ALL)
sa.apply(root)
found = sa.getPaths().getLength()
t4 = time.perf_counter()
bb = coin.SoGetBoundingBoxAction(coin.SbViewportRegion(100, 100))
bb.apply(root)
box = bb.getBoundingBox()
lo, hi = box.getMin().getValue(), box.getMax().getValue()
t5 = time.perf_counter()
buf = coin.SoWriteAction()
sys.stderr.write("pivy: build %d nodes %.3f s, search %d cubes %.3f s, bbox %.3f s -> %s..%s\n"
                 % (N * 3 + 1, t3 - t2, found, t4 - t3, t5 - t4, lo, hi))
sys.stderr.write("pivy: SoDB.isInitialized %s, SoType count %d\n" % (coin.SoDB.isInitialized(), coin.SoType.getNumTypes()))
'''


def out(*a):
    sys.stderr.write(" ".join(str(x) for x in a) + "\n")
    sys.stderr.flush()


def main():
    S = FreeCAD.ExpressionSandbox
    info = S.imageInfo()
    out("host", info["host"], "runtime", info["runtime"])
    S.setRouting(True)
    t0 = time.perf_counter()
    ok = S.available()
    out("boot %.2f s available %s" % (time.perf_counter() - t0, ok))
    if not ok:
        return 1
    t0 = time.perf_counter()
    try:
        n = os.environ.get("PIVY_PROBE_NODES", "10000")
        S.exec(GUEST.replace("__N__", n), "fcx_pivyprobe")
        out("guest run %.2f s OK" % (time.perf_counter() - t0))
    except Exception as e:
        out("guest run FAILED: %s" % str(e)[:2000])
        return 1
    return 0


out("probe exit", main())
