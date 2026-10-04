"""A coordinate dimension on a point that cannot move is a reference.

A horizontal or vertical distance on one vertex fixes that coordinate.
On a vertex of blocked geometry nothing is left to fix, and a driving
dimension there is redundant, so the command makes a reference one -- as
it does for an external point.

The vertical command only asked whether the point was external. On a
vertex of a blocked line it made a DRIVING dimension, where the horizontal
command, asking isPointOrSegmentFixed, made a reference. The asymmetry is
as old as the merge base; upstream lost it when it folded the two commands
into one function, and its test for the pair
(TestConstraintCommandsGui.test_fixed_vertex_coordinates_are_reference)
failed here for Y alone.

Claims, a line from (50, 45) to (10, 15), its first vertex selected:
  - blocked: the horizontal distance is 50 and a reference, the vertical
    one is 45 and a reference, and the sketch still solves with each;
  - not blocked (the control): each is driving.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DistanceFixedVertex"
V = FreeCAD.Vector
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def one(sk, axis, value, blocked):
    doc = sk.Document
    name = "%s, %s" % ("blocked" if blocked else "free", axis)
    before = sk.ConstraintCount
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(sk, "Vertex1")
    FreeCADGui.runCommand("Sketcher_ConstrainDistance" + axis)
    settle(0.4)
    if not check("%s: one constraint was added" % name,
                 sk.ConstraintCount == before + 1, sk.ConstraintCount):
        return
    c = sk.Constraints[before]
    check("%s: it is a Distance%s of %g" % (name, axis, value),
          c.Type == "Distance" + axis and abs(c.Value - value) < 1e-9, (c.Type, c.Value))
    check("%s: and it is %s" % (name, "a reference" if blocked else "driving"),
          sk.getDriving(before) == (not blocked), sk.getDriving(before))
    check("%s: the sketch solves" % name, sk.solve() == 0, sk.solve())
    doc.undo()
    settle(0.3)
    check("%s: undone in one step" % name, sk.ConstraintCount == before, sk.ConstraintCount)


def run():
    try:
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher")
        # No value editor after a driving dimension: the command is done
        # when it returns.
        params.SetBool("ShowDialogOnDistanceConstraint", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        FreeCADGui.getMainWindow().showMaximized()
        FreeCADGui.activateWorkbench("SketcherWorkbench")
        doc = FreeCAD.newDocument(DOC)
        doc.UndoMode = 1
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(50, 45, 0), V(10, 15, 0)), False)
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(sk)
        settle(1.0)
        for axis, value in (("X", 50.0), ("Y", 45.0)):
            one(sk, axis, value, blocked=False)
        doc.openTransaction("Block")
        sk.addConstraint(Sketcher.Constraint("Block", 0))
        doc.commitTransaction()
        doc.recompute()
        settle(0.3)
        for axis, value in (("X", 50.0), ("Y", 45.0)):
            one(sk, axis, value, blocked=True)
        gdoc.resetEdit()
        settle(0.5)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").RemBool(
            "ShowDialogOnDistanceConstraint")
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
