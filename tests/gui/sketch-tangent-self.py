"""A curve is not made tangent to itself through its own end point
(the subject of upstream 1050996387, on the path the fork can reach).

Upstream's guard sits where its tangent tool is given the same edge twice.
Here a second pick of a selected edge does not advance the tool, so that
path adds nothing. What did add a constraint: an arc and its OWN end point
selected, then Sketcher_ConstrainTangent -- the end point-to-curve branch
never asked whether the point belongs to the curve, and wrote
Tangent(arc, start, arc).

Edge1 is an arc, Edge2 a line that starts on the arc's end through a
Coincident constraint.

Claims:
  - the arc and its own end point: nothing is added;
  - the arc and the line's end point sitting on it: the corner becomes a
    tangency, as before (the control);
  - the tool given the same edge twice adds nothing.

Scored against the tree before the change: the first added one constraint.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "TangentSelf"
COMMAND = "Sketcher_ConstrainTangent"
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
    for _ in range(3):
        QtWidgets.QApplication.processEvents()


def build_sketch(doc):
    import Part
    import Sketcher

    state["n"] += 1
    sk = doc.addObject("Sketcher::SketchObject", "Sketch%d" % state["n"])
    V = FreeCAD.Vector
    sk.addGeometry(Part.ArcOfCircle(Part.Circle(V(0, 0, 0), V(0, 0, 1), 5), 0, 2), False)
    end = sk.Geometry[0].EndPoint
    sk.addGeometry(Part.LineSegment(end, V(-10, 12, 0)), False)
    sk.addConstraint(Sketcher.Constraint("Coincident", 0, 2, 1, 1))
    doc.recompute()
    return sk


def vertex(sk, geo, pos):
    for i in range(1, 40):
        if sk.getGeoVertexIndex(i - 1) == (geo, pos):
            return "Vertex%d" % i
    raise RuntimeError("no vertex for %d/%d" % (geo, pos))


def case(doc, way, subs):
    """Constraints of a fresh sketch after the command ran over subs."""
    sk = build_sketch(doc)
    FreeCADGui.ActiveDocument.setEdit(sk, 0)
    settle()
    subs = [s if isinstance(s, str) else vertex(sk, *s) for s in subs]
    FreeCADGui.Selection.clearSelection()
    if way == "selection":
        for sub in subs:
            FreeCADGui.Selection.addSelection(doc.Name, sk.Name, sub)
        settle()
        FreeCADGui.runCommand(COMMAND, 0)
        settle()
    else:
        FreeCADGui.runCommand(COMMAND, 0)
        settle()
        for sub in subs:
            FreeCADGui.Selection.addSelection(doc.Name, sk.Name, sub)
            settle()
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.ActiveDocument.resetEdit()
    settle()
    return [(c.Type, c.First, c.FirstPos, c.Second, c.SecondPos) for c in sk.Constraints]


def run():
    try:
        import SketcherGui  # noqa: F401  -- registers the commands

        doc = FreeCAD.newDocument(DOC)
        corner = [("Coincident", 0, 2, 1, 1)]

        got = case(doc, "selection", ["Edge1", (0, 1)])
        check("an arc and its own end point: nothing is added", got == corner, got)

        got = case(doc, "selection", ["Edge1", (1, 1)])
        check("an arc and the line's end on it: the corner becomes a tangency",
              len(got) == 1 and got[0][0] == "Tangent" and {got[0][1], got[0][3]} == {0, 1},
              got)

        got = case(doc, "tool", ["Edge1", "Edge1"])
        check("the tool given one edge twice adds nothing", got == corner, got)
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
