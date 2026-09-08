# Turn a STEP assembly into the FCStd that scripts/render-bench.py times.
#
#   FC_STEP_SRC=<in.step> FC_STEP_DST=<out.FCStd> \
#       .conda/run.cmd build/<preset>/bin/FreeCADCmd.exe \
#       scripts/render-bench-import.py
#
# Why a document and not the STEP itself: the benchmark measures frames,
# and a STEP import is minutes of OCCT work that has nothing to do with
# any backend. Import once, then every leg of the comparison opens the
# same file and draws the same shapes.
#
# Headless on purpose. ImportGui would attach per-face colours through
# the view providers, which needs a GUI; the plain Import module reads
# the geometry, which is what a draw-cost benchmark is about. The parts
# come out in the default shape colour, so the scene has one material --
# state a coloured import would fan out into many.
import os
import time

import FreeCAD

SRC = os.environ["FC_STEP_SRC"]
DST = os.environ["FC_STEP_DST"]


def say(s):
    FreeCAD.Console.PrintMessage(s + "\n")


t0 = time.perf_counter()
doc = FreeCAD.newDocument("Bench")
import Import  # noqa: E402  (after the document exists)

Import.insert(SRC, doc.Name)
t1 = time.perf_counter()
say("import %.1f s, %d objects" % (t1 - t0, len(doc.Objects)))

doc.recompute()
t2 = time.perf_counter()
say("recompute %.1f s" % (t2 - t1))

doc.saveAs(DST)
t3 = time.perf_counter()
say("save %.1f s -> %s (%.1f MB)"
    % (t3 - t2, DST, os.path.getsize(DST) / 1048576.0))
