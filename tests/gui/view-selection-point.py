"""View Selection on a single point recentres the view and keeps its zoom.

Fitting the view to a box asks Coin's viewBoundingBox to frame its
bounding sphere. A vertex's box is a point -- radius zero, or the
tolerance of a shape's vertex -- and framing that left an orthographic
camera with a height of 0 (nothing usable on screen) and put a
perspective camera on the point itself. A box that small now only moves
the view's centre onto it.

Measuring it found a second defect on the way: asked for one element of
an object directly, the view provider dropped the object's own placement
(ViewProviderDocumentObject passed transform=false for a leaf element,
where the whole object's branch passes the caller's), so View Selection
on any face, edge or vertex of a placed object went to where it would be
unplaced -- (0, 0) here instead of (200, 100).

Measured on a Part box's vertex out of edit, top view: the view centre
lands on the vertex, and the camera height is the one it had, with
navigation animations off and on (an animated fit of a near-point box
shrank the height step by step towards nothing).

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


def run():
    try:
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("ViewSelectionPoint")
        box = doc.addObject("Part::Box", "Box")
        box.Length, box.Width, box.Height = 40, 30, 20
        box.Placement.Base = FreeCAD.Vector(200, 100, 0)
        doc.recompute()
        view = FreeCADGui.activeDocument().activeView()
        view.setCameraType("Orthographic")
        state.update(doc=doc, box=box, view=view)
        QtCore.QTimer.singleShot(1500, probe)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def fit_vertex(animate):
    view, box = state["view"], state["box"]
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
        "UseNavigationAnimations", animate)
    settle(0.2)
    view.viewTop()
    cam = view.getCameraNode()
    cam.position.setValue(0.0, 0.0, cam.position.getValue()[2])
    cam.height.setValue(80.0)
    settle(0.3)
    FreeCADGui.Selection.clearSelection()
    # Vertex1 of a box is the corner above its placement: (200, 100, 20).
    FreeCADGui.Selection.addSelection(box, "Vertex1")
    FreeCADGui.runCommand("Std_ViewSelection")
    settle(1.0)
    x, y, _ = cam.position.getValue().getValue()
    h = cam.height.getValue()
    note("INFO animations %s: centre (%.2f, %.2f) height %.4f" % (animate, x, y, h))
    tag = "animated" if animate else "not animated"
    check("%s: the view centre lands on the vertex" % tag,
          abs(x - 200) < 0.5 and abs(y - 100) < 0.5, "(%.2f, %.2f)" % (x, y))
    check("%s: and the zoom is kept" % tag, abs(h - 80.0) < 1e-3, "height %.4f" % h)


def probe():
    try:
        fit_vertex(False)
        fit_vertex(True)
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
