"""A coincidence that would collapse an element is refused, on both ways
into the command (upstream e38154474a, 084379651a).

Sketcher_ConstrainCoincidentUnified is reached with a selection already
made, or as a tool that takes its picks afterwards. Joining the two ends of
one line folds the line onto a point; so does joining one end to a point
that is already coincident with the other end. A B-spline is the exception,
since joining its ends closes it.

Edge1 (0,0)-(10,0) and Edge2 (10,0)-(10,10) share a corner through a
Coincident constraint; Edge3 and Edge4 are free lines; Edge5 is an open
B-spline. Every case runs on a fresh sketch, so one that goes wrong cannot
colour the next.

Claims, each for the selection way and the tool way:
  - the two ends of Edge1 are refused;
  - Edge1's start and Edge2's start (which sits on Edge1's end) are refused;
  - two ends of different, unconnected lines are joined;
  - the two ends of the B-spline are joined.

Scored against the tree before the change: the selection way joined the
ends of one line, both ways joined through the shared corner, and the tool
way refused the B-spline.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "CoincidentSameElement"
COMMAND = "Sketcher_ConstrainCoincidentUnified"
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
    sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(10, 0, 0)), False)
    sk.addGeometry(Part.LineSegment(V(10, 0, 0), V(10, 10, 0)), False)
    sk.addConstraint(Sketcher.Constraint("Coincident", 0, 2, 1, 1))
    sk.addGeometry(Part.LineSegment(V(30, 0, 0), V(40, 0, 0)), False)
    sk.addGeometry(Part.LineSegment(V(50, 0, 0), V(60, 5, 0)), False)
    spline = Part.BSplineCurve()
    spline.buildFromPoles([V(0, 30, 0), V(5, 40, 0), V(10, 25, 0), V(15, 35, 0)], False, 3)
    sk.addGeometry(spline, False)
    doc.recompute()
    return sk


def vertex(sk, geo, pos):
    """The Vertex name of an end of a curve, read from the sketch."""
    for i in range(1, 40):
        if sk.getGeoVertexIndex(i - 1) == (geo, pos):
            return "Vertex%d" % i
    raise RuntimeError("no vertex for %d/%d" % (geo, pos))


def case(doc, way, first, second):
    """Run the command over two points of a fresh sketch.

    Returns (constraints added, length of Edge1 afterwards).
    """
    sk = build_sketch(doc)
    FreeCADGui.ActiveDocument.setEdit(sk, 0)
    settle()
    before = sk.ConstraintCount
    subs = [vertex(sk, *first), vertex(sk, *second)]
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
    doc.recompute()
    line = sk.Geometry[0]
    return (sk.ConstraintCount - before,
            round(line.StartPoint.distanceToPoint(line.EndPoint), 3))


def run():
    try:
        import SketcherGui  # noqa: F401  -- registers the commands

        doc = FreeCAD.newDocument(DOC)
        for way in ("selection", "tool"):
            got = case(doc, way, (0, 1), (0, 2))
            check("%s: the two ends of one line are refused" % way, got == (0, 10.0), got)

            got = case(doc, way, (0, 1), (1, 1))
            check("%s: an end and a point on the other end are refused" % way,
                  got == (0, 10.0), got)

            got = case(doc, way, (2, 2), (3, 1))
            check("%s: ends of two unconnected lines are joined" % way,
                  got == (1, 10.0), got)

            got = case(doc, way, (4, 1), (4, 2))
            check("%s: the two ends of a B-spline are joined" % way,
                  got == (1, 10.0), got)
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
