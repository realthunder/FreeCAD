"""The Dimension tool keeps its label clear of the pointer (upstream
aa785f78d6).

While the tool's preview dimension follows the pointer, its label was put
exactly where the pointer is, so the cursor sat on the value being placed.
It is now pulled back by 1% of the view's width plus height -- along the
dimension's normal for a distance, towards the centre for a radius -- the
same as upstream. A label dragged by hand is not offset: that is the
plain move, and it still lands under the pointer.

Measured with a horizontal line selected and the tool started on it, the
pointer moved to a point above the line: the preview distance's label
distance against the pointer's height above the line, with the view's
dimensions read from its camera. Before the change the two were equal.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

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


def move(view, xy):
    gv = view.graphicsView()
    vp = gv.viewport()
    x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    ev = QtGui.QMouseEvent(QtCore.QEvent.MouseMove, pos, gv.mapToGlobal(pos.toPoint()),
                           QtCore.Qt.NoButton, QtCore.Qt.NoButton, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)
    settle(0.2)


def view_offset(view):
    """1% of the view's width plus height, in model units, as
    ViewerContext::getDimensions states them for an orthographic camera:
    the camera height, widened (or the height scaled) by the viewport's
    aspect ratio -- not the camera's own aspectRatio field."""
    cam = view.getCameraNode()
    aspect = view.getViewer().getSoRenderManager().getViewportRegion().getViewportAspectRatio()
    height = width = cam.height.getValue()
    if aspect > 1.0:
        width *= aspect
    else:
        height *= aspect
    return (width + height) * 0.01


def run():
    try:
        import Part

        V = FreeCAD.Vector
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "ShowDialogOnDistanceConstraint", False)
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchDimensionLabelOffset")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(-20, 0, 0), V(20, 0, 0)), False)
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.setCameraType("Orthographic")
        view.fitAll()
        state.update(sk=sk, doc=doc, view=view)
        QtCore.QTimer.singleShot(2000, probe)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def probe():
    try:
        sk, view = state["sk"], state["view"]
        before = len(sk.Constraints)
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(sk, "Edge1")
        FreeCADGui.runCommand("Sketcher_Dimension")
        settle(0.3)
        height = 6.0
        move(view, (5.0, height - 0.5))
        move(view, (5.0, height))
        added = sk.Constraints[before:]
        # A horizontal line is dimensioned by its length along x.
        dist = [c for c in added if c.Type in ("Distance", "DistanceX")]
        if not check("the tool previews a distance on the selected line",
                     len(dist) == 1, [c.Type for c in added]):
            raise RuntimeError("no preview")
        got = dist[0].LabelDistance
        offset = view_offset(view)
        note("INFO label %.4f, pointer %.4f, offset %.4f" % (got, height, offset))
        check("the label is pulled back from the pointer by 1% of the view",
              abs(got - (height - offset)) < 0.02 * offset + 1e-3,
              "label %.4f, want %.4f" % (got, height - offset))
        check("so the pointer is not on it", abs(got - height) > 0.5 * offset,
              "label %.4f, pointer %.4f" % (got, height))
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
