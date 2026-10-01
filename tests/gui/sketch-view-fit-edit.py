"""Fitting the view to a sketch in edit (upstream 5587b48a0f, its Sketcher
half; the Core half is this fork's own).

View Selection on an element of a sketch in edit measured the element
by the sketch's Shape. The Shape's edges and vertices are numbered
differently from the edit names and leave construction geometry out
altogether, so a picked construction line fitted the view to nothing
-- or to some other edge. The sketch now answers the bounding box of an
edit element itself, by geometry id.

And, behind Mod/Sketcher/General FitSketchOnEdit (off by default, on the
user's ruling: upstream does it on every edit), entering edit centres
the camera on the sketch's plane and fits the view to its geometry.

Measured on the view's camera, top view, orthographic:

  - a far construction line selected in edit and Std_ViewSelection: the
    view centre lands on it, and its height shrinks to the line's size;
  - a vertex the same way: the centre on the point;
  - a sketch whose geometry is far from the origin entered with the
    preference off: the camera centre does not move; with it on: the
    centre is on the box of its geometry and its origin, inside a placed
    container too.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}
PARAM = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def centre(view):
    """The point the camera looks at, in the XY plane of a top view."""
    cam = view.getCameraNode()
    x, y, _ = cam.position.getValue().getValue()
    return x, y, cam.height.getValue()


def near(xy, want, tol):
    return abs(xy[0] - want[0]) < tol and abs(xy[1] - want[1]) < tol


def run():
    try:
        import Part

        V = FreeCAD.Vector
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "UseNavigationAnimations", False)
        FreeCAD.ParamGet(PARAM).SetBool("FitSketchOnEdit", False)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchViewFitEdit")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(-5, 0, 0), V(5, 0, 0)), False)
        sk.addGeometry(Part.LineSegment(V(400, 300, 0), V(420, 300, 0)), True)
        far = doc.addObject("Sketcher::SketchObject", "Far")
        far.addGeometry(Part.LineSegment(V(1000, 800, 0), V(1010, 800, 0)), False)
        # A sketch inside a placed container: its line's global centre is
        # (510, 300), and the container's placement must count once.
        body = doc.addObject("App::Part", "Body")
        body.Placement.Base = V(100, 0, 0)
        inner = doc.addObject("Sketcher::SketchObject", "Inner")
        body.addObject(inner)
        inner.addGeometry(Part.LineSegment(V(-5, 0, 0), V(5, 0, 0)), False)
        inner.addGeometry(Part.LineSegment(V(400, 300, 0), V(420, 300, 0)), True)
        doc.recompute()
        far.ViewObject.Visibility = False
        view = FreeCADGui.activeDocument().activeView()
        view.setCameraType("Orthographic")
        view.viewTop()
        state.update(sk=sk, far=far, body=body, inner=inner, doc=doc, view=view)
        FreeCADGui.activeDocument().setEdit(sk)
        QtCore.QTimer.singleShot(2000, probe_selection)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def view_selection(sub, obj=None):
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(obj or state["sk"], sub)
    settle(0.2)
    FreeCADGui.runCommand("Std_ViewSelection")
    settle(0.8)
    return centre(state["view"])


def probe_selection():
    try:
        x, y, h = view_selection("Edge2")
        note("INFO construction line: centre (%.2f, %.2f) height %.2f" % (x, y, h))
        check("View Selection on a construction line centres on it",
              near((x, y), (410, 300), 2.0), "(%.2f, %.2f)" % (x, y))
        check("and fits to its size", h < 100.0, "%.2f" % h)
        x, y, h = view_selection("Vertex3")
        note("INFO vertex: centre (%.2f, %.2f) height %.2f" % (x, y, h))
        check("View Selection on a vertex centres on the point",
              near((x, y), (400, 300), 2.0), "(%.2f, %.2f)" % (x, y))
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.activeDocument().resetEdit()
        settle(0.5)
        FreeCADGui.activeDocument().setEdit(state["body"], 0, "Inner.")
        settle(1.0)
        x, y, h = view_selection("Inner.Edge2", state["body"])
        note("INFO construction line in a placed container: centre (%.2f, %.2f) height %.2f"
             % (x, y, h))
        check("in a placed container it centres where the line is drawn",
              near((x, y), (510, 300), 2.0), "(%.2f, %.2f), want (510, 300)" % (x, y))
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.activeDocument().resetEdit()
        settle(0.5)
        probe_entry(False)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def enter(fit, target):
    view = state["view"]
    FreeCAD.ParamGet(PARAM).SetBool("FitSketchOnEdit", fit)
    settle(0.3)
    view.viewTop()
    cam = view.getCameraNode()
    cam.position.setValue(0.0, 0.0, cam.position.getValue()[2])
    cam.height.setValue(50.0)
    settle(0.3)
    if target == "inner":
        FreeCADGui.activeDocument().setEdit(state["body"], 0, "Inner.")
    else:
        FreeCADGui.activeDocument().setEdit(state[target])
    settle(1.5)
    return centre(view)


def probe_entry(fit):
    try:
        view = state["view"]
        FreeCAD.ParamGet(PARAM).SetBool("FitSketchOnEdit", fit)
        settle(0.3)
        view.viewTop()
        cam = view.getCameraNode()
        cam.position.setValue(0.0, 0.0, cam.position.getValue()[2])
        cam.height.setValue(50.0)
        settle(0.3)
        FreeCADGui.activeDocument().setEdit(state["far"])
        settle(1.5)
        x, y, h = centre(view)
        note("INFO entry with FitSketchOnEdit=%s: centre (%.2f, %.2f) height %.2f"
             % (fit, x, y, h))
        if fit:
            # The whole sketch's box holds its origin too: (0..1010, 0..800).
            check("with FitSketchOnEdit the view fits the sketch on entry",
                  near((x, y), (505, 400), 2.0), "(%.2f, %.2f), want (505, 400)" % (x, y))
        else:
            check("without it the view centre stays put",
                  near((x, y), (0, 0), 1.0), "(%.2f, %.2f)" % (x, y))
        FreeCADGui.activeDocument().resetEdit()
        settle(0.5)
        if not fit:
            probe_entry(True)
            return
        x, y, h = enter(True, "inner")
        note("INFO entry into the placed container's sketch: centre (%.2f, %.2f) height %.2f"
             % (x, y, h))
        # The whole sketch: (-5..420, 0..300) about its origin, +100 in x.
        check("and a sketch in a placed container is framed where it is drawn",
              near((x, y), (307.5, 150), 3.0), "(%.2f, %.2f), want (307.5, 150)" % (x, y))
        FreeCADGui.activeDocument().resetEdit()
        settle(0.5)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    FreeCAD.ParamGet(PARAM).SetBool("FitSketchOnEdit", False)
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
