"""A clipboard copy costs in proportion to what is copied.

Copy writes the selected geometry and its constraints out as a Python script,
which Paste runs. Three things in it grew with the SQUARE of the selection:

  - the command built its table of new geometry ids once per constraint;
  - it looked every constraint's geometry up in the selection by a linear
    search;
  - the converter built each of its two lists by formatting the whole list
    so far, plus one more line, once per element.

A copy of 4000 lines with a constraint each took 0.93 s; of 1000, 0.057 s:
sixteen times as long for four times as much.

Claims:

  - what is copied is what it was: a selection of normal and construction
    geometry with constraints among it, pasted back, comes out as the same
    geometry in the same order, construction as construction, with the same
    constraints on the new geometry;
  - four times as many elements take less than nine times as long (in
    proportion would be four; with the square, sixteen). The fastest of
    three copies is taken at each size.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
V = FreeCAD.Vector
SMALL, LARGE = 1000, 4000
LIMIT = 9.0


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def round_trip():
    import Part
    import Sketcher

    doc = FreeCAD.newDocument("CopyRoundTrip")
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(10, 0, 0)), False)
    sk.addGeometry(Part.LineSegment(V(10, 0, 0), V(10, 10, 0)), True)
    sk.addGeometry(Part.Circle(V(30, 5, 0), V(0, 0, 1), 4), False)
    sk.addGeometry(Part.LineSegment(V(0, 20, 0), V(10, 20, 0)), True)
    sk.addConstraint(Sketcher.Constraint("Horizontal", 0))
    sk.addConstraint(Sketcher.Constraint("Coincident", 0, 2, 1, 1))
    sk.addConstraint(Sketcher.Constraint("Radius", 2, 4.0))
    sk.addConstraint(Sketcher.Constraint("Distance", 3, 10.0))
    doc.recompute()
    FreeCADGui.ActiveDocument.setEdit(sk, 0)
    settle()
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(sk, ["Edge1", "Edge2", "Edge3", "Edge4"])
    settle()
    FreeCADGui.runCommand("Sketcher_CopyClipboard", 0)
    FreeCADGui.runCommand("Sketcher_Paste", 0)
    settle()
    doc.recompute()

    kinds = [g.TypeId for g in sk.Geometry]
    check("pasted, the geometry is there twice, in the same order",
          sk.GeometryCount == 8 and kinds[4:] == kinds[:4], kinds)
    flags = [sk.getConstruction(i) for i in range(sk.GeometryCount)]
    check("construction as construction", flags == [False, True, False, True] * 2, flags)
    got = [(c.Type, c.First, c.Second) for c in sk.Constraints]
    want = [("Horizontal", 0), ("Coincident", 0, 1), ("Radius", 2), ("Distance", 3)]
    first = [(t, a) if t != "Coincident" else (t, a, b) for t, a, b in got[:4]]
    second = [(t, a - 4) if t != "Coincident" else (t, a - 4, b - 4) for t, a, b in got[4:]]
    check("and the constraints are on the new geometry as on the old",
          len(got) == 8 and first == want and second == want, got)

    FreeCADGui.Selection.clearSelection()
    FreeCADGui.ActiveDocument.resetEdit()
    FreeCAD.closeDocument(doc.Name)


def copy_time(count):
    """The fastest of three copies of `count` lines, each with a constraint."""
    import Part
    import Sketcher

    doc = FreeCAD.newDocument("CopyCost%d" % count)
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    sk.addGeometry([Part.LineSegment(V(0, i, 0), V(10, i, 0)) for i in range(count)], False)
    sk.addConstraint([Sketcher.Constraint("Horizontal", i) for i in range(count)])
    doc.recompute()
    FreeCADGui.ActiveDocument.setEdit(sk, 0)
    settle()
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(sk, ["Edge%d" % (i + 1) for i in range(count)])
    settle()
    FreeCADGui.runCommand("Sketcher_CopyClipboard", 0)  # warm
    best = None
    for _ in range(3):
        start = time.perf_counter()
        FreeCADGui.runCommand("Sketcher_CopyClipboard", 0)
        took = time.perf_counter() - start
        best = took if best is None else min(best, took)
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.ActiveDocument.resetEdit()
    FreeCAD.closeDocument(doc.Name)
    return best


def run():
    try:
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        round_trip()

        small = copy_time(SMALL)
        large = copy_time(LARGE)
        ratio = large / small if small > 0 else 0.0
        check("%d elements take less than %g times as long as %d" % (LARGE, LIMIT, SMALL),
              0.0 < ratio < LIMIT,
              "%.3f s and %.3f s: %.1f times" % (large, small, ratio))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
