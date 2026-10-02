"""A new distance's label is put on a fixed side, clear of both points
(upstream 3d87975faf).

A dimension made by a constraint command got a label distance and nothing
else, so which side the label fell on followed the order of the
constraint's two points: for a line drawn downhill the horizontal
distance's label sat between the two ends, across the geometry. The label
now goes below both points for a horizontal distance, beside both for a
vertical one (on the side of the upper point), and up and to the left of
an aligned one -- each by the same view-scaled distance as before.

The label's place is worked out from the constraint as moveConstraint
writes it: the second point, plus LabelDistance along the normal of the
dimension's direction.

Claims, each on a fresh sketch with one line, the command run on it:
  - horizontal distance of a downhill and of an uphill line: below both ends;
  - vertical distance: right of both ends when the upper end is the right
    one, left of both when it is the left one;
  - aligned distance: on the upper left of the line;
  - every label is the same distance clear.

Scored against the tree before the change: the downhill horizontal label
was between the ends, the uphill one above.
"""
import math
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DistanceLabelSide"
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


def label(sk, c):
    """Where the label's dimension line is, as moveConstraint defines it."""
    if c.FirstPos == 0:
        # the distance of a line: its own two ends
        line = sk.Geometry[c.First]
        p1, p2 = line.StartPoint, line.EndPoint
    else:
        p1 = sk.getPoint(c.First, c.FirstPos)
        p2 = sk.getPoint(c.Second, c.SecondPos)
    if c.Type == "DistanceX":
        d = (1.0 if p2.x - p1.x >= 1e-7 else -1.0, 0.0)
    elif c.Type == "DistanceY":
        d = (0.0, 1.0 if p2.y - p1.y >= 1e-7 else -1.0)
    else:
        length = math.hypot(p2.x - p1.x, p2.y - p1.y)
        d = ((p2.x - p1.x) / length, (p2.y - p1.y) / length)
    normal = (-d[1], d[0])
    return (p2.x + normal[0] * c.LabelDistance, p2.y + normal[1] * c.LabelDistance)


def case(doc, a, b, command):
    """(label point, the constraint) for the command run on a line a-b."""
    import Part

    state["n"] += 1
    sk = doc.addObject("Sketcher::SketchObject", "Sketch%d" % state["n"])
    V = FreeCAD.Vector
    sk.addGeometry(Part.LineSegment(V(a[0], a[1], 0), V(b[0], b[1], 0)), False)
    # the same frame for every case, so the view-scaled distance is one number
    sk.addGeometry(Part.LineSegment(V(-30, -30, 0), V(60, 60, 0)), True)
    doc.recompute()
    FreeCADGui.ActiveDocument.setEdit(sk, 0)
    view = FreeCADGui.ActiveDocument.ActiveView
    view.viewTop()
    view.fitAll()
    settle()
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(doc.Name, sk.Name, "Edge1")
    settle()
    FreeCADGui.runCommand(command, 0)
    settle()
    c = sk.Constraints[-1]
    point = label(sk, c)
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.ActiveDocument.resetEdit()
    settle()
    return point, c


def run():
    try:
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "ShowDialogOnDistanceConstraint", False)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        clear = []

        p, c = case(doc, (0, 8), (10, 0), "Sketcher_ConstrainDistanceX")
        check("horizontal distance, downhill line: the label is below both ends",
              p[1] < 0, "label y %.3f, ends at 8 and 0" % p[1])
        clear.append(-p[1])

        p, c = case(doc, (0, 0), (10, 8), "Sketcher_ConstrainDistanceX")
        check("horizontal distance, uphill line: the label is below both ends",
              p[1] < 0, "label y %.3f, ends at 0 and 8" % p[1])
        clear.append(-p[1])

        p, c = case(doc, (0, 0), (8, 10), "Sketcher_ConstrainDistanceY")
        check("vertical distance, upper end on the right: the label is right of both",
              p[0] > 8, "label x %.3f, ends at 0 and 8" % p[0])
        clear.append(p[0] - 8)

        p, c = case(doc, (8, 0), (0, 10), "Sketcher_ConstrainDistanceY")
        check("vertical distance, upper end on the left: the label is left of both",
              p[0] < 0, "label x %.3f, ends at 8 and 0" % p[0])
        clear.append(-p[0])

        p, c = case(doc, (0, 0), (10, 10), "Sketcher_ConstrainDistance")
        side = (p[1] - p[0]) / math.sqrt(2)  # distance to the line y = x, upper left positive
        check("aligned distance: the label is on the upper left of the line",
              side > 0, "label %.3f, %.3f" % p)
        clear.append(side)

        note("INFO clearances %s" % ["%.4f" % v for v in clear])
        check("every label is the same distance clear",
              min(clear) > 0 and max(clear) - min(clear) < 0.02 * max(clear),
              ["%.4f" % v for v in clear])
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
