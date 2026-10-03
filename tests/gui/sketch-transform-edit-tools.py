"""Sketcher tools stay off while a sketch is being moved (Transform edit)
(upstream 93abfc4fa4).

Transform -- the placement dragger from the context menu -- puts a sketch
in edit too, in another mode. The Sketcher commands asked only whether a
sketch view provider was in edit, so they were all active there, and
running one reached for edit data that only the sketch's own edit makes:
Sketcher_CreateLine crashed in deactivateHandler(). They ask whether the
sketch itself is being edited now.

Measured here with the sketch in Transform edit: the tools report
inactive, and running one leaves the process alive with nothing made.
In the sketch's own edit they are active, as before. Before the change
all four were active and Sketcher_CreateLine crashed.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}
TOOLS = ["Sketcher_CreateLine", "Sketcher_ConstrainHorizontal",
         "Sketcher_ToggleConstruction", "Sketcher_LeaveSketch"]
TRANSFORM = 1


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def active():
    return {c: FreeCADGui.Command.get(c).isActive() for c in TOOLS}


def run():
    try:
        import Part
        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchTransformEditTools")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(40, 0, 0)), False)
        doc.recompute()
        state["sk"] = sk
        FreeCADGui.activeDocument().setEdit(sk, TRANSFORM)
        QtCore.QTimer.singleShot(1500, in_transform)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def in_transform():
    try:
        sk = state["sk"]
        check("the sketch is in Transform edit",
              FreeCADGui.activeDocument().getInEdit() is not None)
        act = active()
        check("the Sketcher tools are inactive there", not any(act.values()), act)
        FreeCADGui.Selection.addSelection(sk, "Edge1")
        FreeCADGui.runCommand("Sketcher_CreateLine", 0)
        FreeCADGui.runCommand("Sketcher_ConstrainHorizontal", 0)
        check("and running them makes nothing, and crashes nothing",
              len(sk.Constraints) == 0 and len(sk.Geometry) == 1,
              "%d constraints, %d geometries" % (len(sk.Constraints), len(sk.Geometry)))
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.activeDocument().resetEdit()
        FreeCADGui.activeDocument().setEdit(sk)
        QtCore.QTimer.singleShot(1500, in_sketch)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def in_sketch():
    try:
        act = active()
        check("in the sketch's own edit the tools are active",
              act["Sketcher_CreateLine"] and act["Sketcher_LeaveSketch"], act)
        FreeCADGui.activeDocument().resetEdit()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
