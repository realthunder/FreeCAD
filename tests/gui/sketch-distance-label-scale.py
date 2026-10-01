"""A distance made while a sketch is in edit puts its label at a distance
scaled to the view (upstream 8a6872e69d).

Only the dimension command scaled a new label (finishDatumConstraint
sets 2 x the view's scale factor); a distance made any other way -- a
tool's on-view parameter, a macro, the Python console -- kept the
default 10 mm, which in a sketch a few millimetres across puts the
label far outside it, and in a large one on top of the line. Now the
sketch scales every new Distance, DistanceX and DistanceY whose label
distance is still the default, however it was made, while it is in
edit.

Measured on a 2 mm line zoomed to fill the view: a Distance, a
DistanceX and a DistanceY added from Python in edit get 2 x the scale
factor (10 before); one added with a label distance of its own keeps
it; a Radius added the same way is not this row's and keeps 10; and a
Distance added outside edit keeps 10.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def run():
    try:
        import Part

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchDistanceLabelScale")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(2, 0, 0)), False)
        sk.addGeometry(Part.Circle(V(1, 1, 0), V(0, 0, 1), 0.5), False)
        doc.recompute()

        # Outside edit: nothing to scale by.
        idle = sk.addConstraint(Sketcher_constraint("Distance", 0, 2.0))
        state["idle"] = sk.Constraints[idle].LabelDistance

        FreeCADGui.activeDocument().setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        state.update(sk=sk, doc=doc, view=view)
        QtCore.QTimer.singleShot(2000, probe)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def scale_factor(view):
    """ViewProviderSketch::getScaleFactor, from the same camera."""
    from pivy import coin

    cam = view.getCameraNode()
    aspect = cam.aspectRatio.getValue()
    volume = cam.getViewVolume(aspect)
    return volume.getWorldToScreenScale(coin.SbVec3f(0, 0, 0), 0.1) / (5 * aspect)


def Sketcher_constraint(*args):
    import Sketcher
    return Sketcher.Constraint(*args)


def probe():
    try:
        sk = state["sk"]
        scale = scale_factor(state["view"])
        note("INFO scale factor %.5f" % scale)
        made = {}
        for kind, args in (("Distance", (0, 2.0)),
                           ("DistanceX", (0, 1, 0, 2, 2.0)),
                           ("DistanceY", (0, 1, 1, 1, 1.0))):
            i = sk.addConstraint(Sketcher_constraint(kind, *args))
            made[kind] = sk.Constraints[i].LabelDistance
        # LabelDistance is read-only on a Python Constraint: give the idle
        # one a distance of its own and add a copy of it.
        sk.setLabelDistance(0, 3.25)
        i = sk.addConstraint(sk.Constraints[0])
        kept = sk.Constraints[i].LabelDistance
        i = sk.addConstraint(Sketcher_constraint("Radius", 1, 0.5))
        radius = sk.Constraints[i].LabelDistance

        note("INFO label distances %s, own %.4f, radius %.4f, idle %.4f"
             % (made, kept, radius, state["idle"]))
        want = 2.0 * scale
        for kind, got in made.items():
            check("a %s added in edit takes 2 x the view's scale" % kind,
                  abs(got - want) < 1e-4 * max(1.0, want), "%.4f, want %.4f" % (got, want))
        for kind, got in made.items():
            check("a %s added in edit is not left at the default 10" % kind,
                  abs(got - 10.0) > 1e-3, "%.4f" % got)
        check("a label distance of its own is kept", abs(kept - 3.25) < 1e-5, "%.4f" % kept)
        check("a Radius added the same way keeps the default",
              abs(radius - 10.0) < 1e-5, "%.4f" % radius)
        check("a Distance added outside edit keeps the default",
              abs(state["idle"] - 10.0) < 1e-5, "%.4f" % state["idle"])
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
