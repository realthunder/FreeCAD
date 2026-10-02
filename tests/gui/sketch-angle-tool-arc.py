"""The angle tool takes an arc's angle by one pick (upstream 129c7d4d03).

With an arc selected first, Sketcher_ConstrainAngle constrains the arc's
own angle. Started as a tool it did not: an arc is an edge, the tool
waited for a second edge to measure an angle against, and an arc could
only be dimensioned by selecting it and then running the command. An arc
of a circle now completes the tool on its own. A line still waits for the
second line, and tools that do not ask for an arc see one as the edge it
always was.

Claims, each on a fresh sketch holding an arc and two lines:
  - the tool, then the arc: one Angle constraint on the arc, of the arc's
    angle;
  - the tool, then a line: nothing yet; the second line: the angle between
    them;
  - the tangent tool, then the arc and a line: a tangency, as before.

Scored against the tree before the change: the first added nothing.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "AngleToolArc"
state = {"n": 0}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def constraints(sk):
    return [(c.Type, c.First, c.Second, round(c.Value, 6)) for c in sk.Constraints]


def case(doc, command, subs):
    """Constraints after each pick of subs into the running tool."""
    import Part

    state["n"] += 1
    sk = doc.addObject("Sketcher::SketchObject", "Sketch%d" % state["n"])
    V = FreeCAD.Vector
    sk.addGeometry(Part.ArcOfCircle(Part.Circle(V(0, 0, 0), V(0, 0, 1), 20), 0.25, 1.5), False)
    sk.addGeometry(Part.LineSegment(V(40, 0, 0), V(60, 0, 0)), False)
    sk.addGeometry(Part.LineSegment(V(40, 0, 0), V(60, 20, 0)), False)
    doc.recompute()
    FreeCADGui.ActiveDocument.setEdit(sk, 0)
    settle()
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.runCommand(command, 0)
    settle()
    steps = []
    for sub in subs:
        FreeCADGui.Selection.addSelection(doc.Name, sk.Name, sub)
        settle()
        steps.append(constraints(sk))
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.ActiveDocument.resetEdit()
    settle()
    return steps


def run():
    try:
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "ShowDialogOnDistanceConstraint", False)
        doc = FreeCAD.newDocument(DOC)

        steps = case(doc, "Sketcher_ConstrainAngle", ["Edge1"])
        check("the angle tool, then the arc: the arc's own angle",
              steps[0] == [("Angle", 0, -2000, 1.25)], steps[0])

        steps = case(doc, "Sketcher_ConstrainAngle", ["Edge2", "Edge3"])
        check("the angle tool, then a line: nothing yet", steps[0] == [], steps[0])
        check("the second line: the angle between them",
              len(steps[1]) == 1 and steps[1][0][0] == "Angle"
              and {steps[1][0][1], steps[1][0][2]} == {1, 2}
              and abs(abs(steps[1][0][3]) - 0.785398) < 1e-5, steps[1])

        steps = case(doc, "Sketcher_ConstrainTangent", ["Edge1", "Edge2"])
        check("the tangent tool still sees the arc as an edge",
              steps[0] == [] and len(steps[1]) == 1 and steps[1][0][0] == "Tangent", steps)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    try:
        FreeCADGui.Selection.clearSelection()
        if FreeCADGui.ActiveDocument and FreeCADGui.ActiveDocument.getInEdit():
            FreeCADGui.ActiveDocument.resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
