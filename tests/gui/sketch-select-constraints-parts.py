"""Select Associated Constraints answers for a point and an axis too.

The command went through the selection and looked only at names beginning
with "Edge": a selected end point, an axis or an external edge selected
nothing, without a word. Upstream asks the sketch what each selected name is
and takes the constraints on that edge, or on that point of it
(`732501d89d`). So does this.

Sketch: a horizontal line (Edge1) whose start lies on the horizontal axis and
whose end meets a vertical line (Edge2).

  Constraint1  Horizontal   Edge1
  Constraint2  Coincident   Edge1 end, Edge2 start
  Constraint3  PointOnObject  Edge1 start, H_Axis
  Constraint4  Vertical     Edge2

Claims:

  - an edge still selects every constraint on it or on its points;
  - an end point selects the constraints on that point, and not those on
    the edge or on its other end;
  - the horizontal axis selects what is constrained to it.

Scored against the tree before the change: see the commit message.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchSelectConstraintsParts"
OBJ = "Sketch"
V = FreeCAD.Vector


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


def selected():
    names = []
    for entry in FreeCADGui.Selection.getSelectionEx("*"):
        if entry.ObjectName == OBJ:
            names += list(entry.SubElementNames)
    return sorted(names)


def associated(sub):
    FreeCADGui.Selection.clearSelection()
    settle()
    FreeCADGui.Selection.addSelection(DOC, OBJ, sub)
    settle()
    FreeCADGui.runCommand("Sketcher_SelectConstraints", 0)
    settle()
    return selected()


def run():
    try:
        import Part
        import Sketcher
        import SketcherGui  # noqa: F401  -- registers the commands

        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry(Part.LineSegment(V(5, 0, 0), V(30, 0, 0)), False)
        sk.addGeometry(Part.LineSegment(V(30, 0, 0), V(30, 20, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Horizontal", 0))
        sk.addConstraint(Sketcher.Constraint("Coincident", 0, 2, 1, 1))
        sk.addConstraint(Sketcher.Constraint("PointOnObject", 0, 1, -1))
        sk.addConstraint(Sketcher.Constraint("Vertical", 1))
        doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(sk, 0)
        settle()

        got = associated("Edge1")
        check("an edge selects every constraint on it or on its points",
              got == ["Constraint1", "Constraint2", "Constraint3"], got)
        got = associated("Vertex2")
        check("an end point selects the constraints on that point alone",
              got == ["Constraint2"], got)
        got = associated("Vertex1")
        check("the other end selects its own", got == ["Constraint3"], got)
        got = associated("H_Axis")
        check("the horizontal axis selects what is constrained to it",
              got == ["Constraint3"], got)

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.ActiveDocument.resetEdit()
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
