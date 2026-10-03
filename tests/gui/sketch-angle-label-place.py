"""A new angle's label is put where the geometry is (upstream 9663cf8dd4).

An angle made by the constraint command drew its arc at the default label
distance around the point where the two lines cross. When the lines do not
reach that point -- two segments that would only meet if extended -- the
arc and its number sat by an empty crossing, away from both lines. The arc
is now drawn past the nearer of the two near ends, by the same view-scaled
distance; an arc's own angle is labelled outside the arc. Two lines that
do meet at the crossing keep the default.

An angle's label distance is half the radius its arc is drawn at
(moveConstraint).

Claims, each on a fresh sketch:
  - two lines whose crossing is 30 away from the nearer end: the angle's
    arc is drawn beyond 30, by the default clearance;
  - an arc of radius 20: its angle is labelled outside it, by the same;
  - two lines sharing a corner: the default, unchanged.

Scored against the tree before the change: the first two were drawn at the
default radius, inside 30 and inside the arc.
"""
import math
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "AngleLabelPlace"
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


def case(doc, geometry, subs):
    """The label distance of the angle the command makes over subs."""
    import Part

    state["n"] += 1
    sk = doc.addObject("Sketcher::SketchObject", "Sketch%d" % state["n"])
    V = FreeCAD.Vector
    for geo in geometry:
        sk.addGeometry(geo, False)
    # the same frame for every case, so the view-scaled distance is one number
    sk.addGeometry(Part.LineSegment(V(-50, -50, 0), V(90, 90, 0)), True)
    doc.recompute()
    FreeCADGui.ActiveDocument.setEdit(sk, 0)
    view = FreeCADGui.ActiveDocument.ActiveView
    view.viewTop()
    view.fitAll()
    settle()
    FreeCADGui.Selection.clearSelection()
    for sub in subs:
        FreeCADGui.Selection.addSelection(doc.Name, sk.Name, sub)
    settle()
    before = sk.ConstraintCount
    FreeCADGui.runCommand("Sketcher_ConstrainAngle", 0)
    settle()
    added = sk.Constraints[before:]
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.ActiveDocument.resetEdit()
    settle()
    if len(added) != 1 or added[0].Type != "Angle":
        raise RuntimeError("no angle: %s" % [c.Type for c in added])
    return added[0].LabelDistance


def run():
    try:
        import Part
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "ShowDialogOnDistanceConstraint", False)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        V = FreeCAD.Vector

        corner = case(doc, [Part.LineSegment(V(0, 0, 0), V(40, 0, 0)),
                            Part.LineSegment(V(0, 0, 0), V(40, 40, 0))], ["Edge1", "Edge2"])
        default = 2 * abs(corner)
        check("two lines sharing a corner: a default radius", default > 0, default)

        apart = case(doc, [Part.LineSegment(V(30, 0, 0), V(40, 0, 0)),
                           Part.LineSegment(V(30, 30, 0), V(40, 40, 0))], ["Edge1", "Edge2"])
        radius = 2 * abs(apart)
        check("two lines 30 from their crossing: the arc is drawn beyond 30",
              radius > 30, "radius %.3f" % radius)
        check("by the default clearance", abs(radius - 30 - default / 2) < 0.02 * default,
              "radius %.3f, default label distance %.3f" % (radius, default / 2))

        arc = case(doc, [Part.ArcOfCircle(Part.Circle(V(0, 0, 0), V(0, 0, 1), 20), 0.2, 1.3)],
                   ["Edge1"])
        radius = 2 * abs(arc)
        check("an arc of radius 20: its angle is labelled outside it",
              radius > 20, "radius %.3f" % radius)
        check("by the same clearance", abs(radius - 20 - default / 2) < 0.02 * default,
              "radius %.3f, default label distance %.3f" % (radius, default / 2))
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
