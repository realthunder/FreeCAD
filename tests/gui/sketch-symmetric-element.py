"""An element is made symmetric about a line or a point by its two ends
(upstream bc3c0dc19a).

Sketcher_ConstrainSymmetric took "a line and a symmetry point". It now
takes any element that has two ends -- a line, an arc, an open B-spline --
with a symmetry line, an axis or a point, and writes
Symmetric(element start, element end, reference). An element without ends
is refused, with a symmetry point too: the tool used to accept a circle
there and wrote a constraint between ends a circle does not have.

Edge1 a line, Edge2 an arc, Edge3 a circle, Edge4 a second line, and a
loose point. Every case runs on a fresh sketch, for the selection way and
the tool way.

Claims:
  - a line and the vertical axis: its ends are symmetric about the axis;
  - the axis first, then the line: the same constraint;
  - an arc and a line: the arc's ends are symmetric about the line;
  - an arc and a point: its ends are symmetric about the point;
  - a line and a circle, a circle and a line, a circle and a point: refused.

Scored against the tree before the change: 8 of 14 failed -- nothing took
an element and a line, the selection way took no arc with a point, and the
tool took a circle with one.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SymmetricElement"
COMMAND = "Sketcher_ConstrainSymmetric"
V_AXIS = -2
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

    state["n"] += 1
    sk = doc.addObject("Sketcher::SketchObject", "Sketch%d" % state["n"])
    V = FreeCAD.Vector
    sk.addGeometry(Part.LineSegment(V(2, 3, 0), V(8, 7, 0)), False)
    sk.addGeometry(Part.ArcOfCircle(Part.Circle(V(20, 20, 0), V(0, 0, 1), 5), 0, 2), False)
    sk.addGeometry(Part.Circle(V(40, 0, 0), V(0, 0, 1), 4), False)
    sk.addGeometry(Part.LineSegment(V(0, 40, 0), V(30, 45, 0)), False)
    sk.addGeometry(Part.Point(V(-20, -20, 0)), False)
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
    return [(c.Type, c.First, c.FirstPos, c.Second, c.SecondPos, c.Third, c.ThirdPos)
            for c in sk.Constraints]


def run():
    try:
        import SketcherGui  # noqa: F401  -- registers the commands

        doc = FreeCAD.newDocument(DOC)
        loose = (4, 1)
        for way in ("selection", "tool"):
            got = case(doc, way, ["Edge1", "V_Axis"])
            check("%s: a line and the vertical axis" % way,
                  got == [("Symmetric", 0, 1, 0, 2, V_AXIS, 0)], got)

            got = case(doc, way, ["V_Axis", "Edge1"])
            check("%s: the axis first, then the line" % way,
                  got == [("Symmetric", 0, 1, 0, 2, V_AXIS, 0)], got)

            got = case(doc, way, ["Edge2", "Edge4"])
            check("%s: an arc and a line" % way,
                  got == [("Symmetric", 1, 1, 1, 2, 3, 0)], got)

            got = case(doc, way, ["Edge2", loose])
            check("%s: an arc and a point" % way,
                  got == [("Symmetric", 1, 1, 1, 2, 4, 1)], got)

            got = case(doc, way, ["Edge1", "Edge3"])
            check("%s: a line and a circle are refused" % way, got == [], got)

            got = case(doc, way, ["Edge3", "Edge4"])
            check("%s: a circle and a line are refused" % way, got == [], got)

            got = case(doc, way, ["Edge3", loose])
            check("%s: a circle and a point are refused" % way, got == [], got)
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
