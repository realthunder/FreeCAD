"""A dimension made from the geometry states the geometry's value, to the
precision of a double (the subject of upstream b9155035fe).

A constraint command measures the geometry and writes the measurement
into a Python call as text. It was written with "%f", six decimals: a
length of 0.000123456789 became 0.000123, and every value moved its
geometry by up to half a millionth on the next solve. Upstream writes
eight significant digits, which mends the small values and loses more than
before above 100 (1234.123456789 becomes 1234.1235). The value is written
with fifteen significant digits here: what a double holds less its last
bit or two, and short enough that 0.1 is still written 0.1.

Two lines already constrained perpendicular are a right angle exactly:
the angle command writes pi/2 for them rather than the angle it measures.

Claims, each on a fresh sketch, relative error against the geometry as it
was before the command:
  - a long line's length, a short line's length, a large radius, an angle;
  - the angle of two perpendicular lines is pi/2, to those fifteen digits.

Scored against the tree before the change: every one failed.
"""
import math
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ConstraintValuePrecision"
TOLERANCE = 1e-13
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


def case(doc, geometry, subs, command, constraints=()):
    """The value of the constraint the command adds to a fresh sketch."""
    state["n"] += 1
    sk = doc.addObject("Sketcher::SketchObject", "Sketch%d" % state["n"])
    for geo in geometry:
        sk.addGeometry(geo, False)
    for c in constraints:
        sk.addConstraint(c)
    doc.recompute()
    FreeCADGui.ActiveDocument.setEdit(sk, 0)
    settle()
    before = sk.ConstraintCount
    FreeCADGui.Selection.clearSelection()
    for sub in subs:
        FreeCADGui.Selection.addSelection(doc.Name, sk.Name, sub)
    settle()
    FreeCADGui.runCommand(command, 0)
    settle()
    added = sk.Constraints[before:]
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.ActiveDocument.resetEdit()
    settle()
    if len(added) != 1:
        raise RuntimeError("%s added %d constraints" % (command, len(added)))
    return added[0].Value


def close(name, got, want):
    err = abs(got - want) / abs(want)
    return check(name, err < TOLERANCE, "%r, geometry %r, relative error %.3g" % (got, want, err))


def run():
    try:
        import Part
        import Sketcher
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "ShowDialogOnDistanceConstraint", False)
        doc = FreeCAD.newDocument(DOC)
        V = FreeCAD.Vector

        a, b = V(0.1, 0.2, 0), V(1234.223456789, 777.123456789, 0)
        got = case(doc, [Part.LineSegment(a, b)], ["Edge1"], "Sketcher_ConstrainDistance")
        close("a long line's length", got, a.distanceToPoint(b))

        a, b = V(1, 1, 0), V(1.000123456789, 1.0000987654321, 0)
        got = case(doc, [Part.LineSegment(a, b)], ["Edge1"], "Sketcher_ConstrainDistance")
        close("a short line's length", got, a.distanceToPoint(b))

        radius = 1234.123456789
        got = case(doc, [Part.Circle(V(0, 0, 0), V(0, 0, 1), radius)], ["Edge1"],
                   "Sketcher_ConstrainRadius")
        close("a large radius", got, radius)

        lines = [Part.LineSegment(V(0, 0, 0), V(10, 0, 0)),
                 Part.LineSegment(V(0, 0, 0), V(7, 0.0123456789, 0))]
        got = case(doc, lines, ["Edge1", "Edge2"], "Sketcher_ConstrainAngle")
        close("a small angle", abs(got), math.atan2(0.0123456789, 7))

        lines = [Part.LineSegment(V(0, 0, 0), V(10, 3, 0)),
                 Part.LineSegment(V(0, 0, 0), V(-3, 10, 0))]
        got = case(doc, lines, ["Edge1", "Edge2"], "Sketcher_ConstrainAngle",
                   [Sketcher.Constraint("Perpendicular", 0, 1)])
        close("two perpendicular lines are a right angle", abs(got), math.pi / 2)
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
