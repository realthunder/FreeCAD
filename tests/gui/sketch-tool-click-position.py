"""A drawing tool puts its point where the pointer is.

On a press the sketch looks for what is under the pointer, within the pick
radius, and took the 3D point of that hit as the position of the click --
of any hit, where upstream takes a vertex's only. The fork widened it for
dragging a curve, which has to start on the curve it grabs (1b87d4f072).
A running tool was handed the same point, and a tool's own preview is
drawn right under the pointer and picked like anything else in the edit
scene: the B-spline tool's preview runs from the last point to the
pointer, so its next point was put ON the preview, short of the click.
The tool's mouse move got the pointer's own position all along, so the
preview showed one place and the click made another.

Measured before, one model unit about nine pixels: clicks at (-8, 9),
(6, -7) and (18, 6) after a first at (-20, -5) put the second pole at
(-8.197, 8.725) -- 0.34 off, three pixels -- and with the periodic tool
the third and fourth at (6.099, -6.787) and (18.081, 5.722). The line and
polyline tools were exact with the same clicks.

Claims: with the B-spline tool and the periodic B-spline tool, each of
four clicks makes a pole within a pixel of the click.

Clicks are real press / release events on the viewport.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ToolClickPosition"
V = FreeCAD.Vector
POINTS = [V(-20, -5, 0), V(-8, 9, 0), V(6, -7, 0), V(18, 6, 0)]
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def mouse(view, kind, pt, button, buttons):
    gv = view.graphicsView()
    vp = gv.viewport()
    x, y = view.getPointOnViewport(pt)
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    QtWidgets.QApplication.sendEvent(
        vp, QtGui.QMouseEvent(kind, pos, gv.mapToGlobal(pos.toPoint()), button, buttons,
                              QtCore.Qt.NoModifier))


def click(view, pt, button=QtCore.Qt.LeftButton):
    none = QtCore.Qt.NoButton
    mouse(view, QtCore.QEvent.MouseMove, pt, none, none)
    settle(0.3)
    mouse(view, QtCore.QEvent.MouseMove, pt, none, none)
    mouse(view, QtCore.QEvent.MouseButtonPress, pt, button, button)
    settle(0.1)
    mouse(view, QtCore.QEvent.MouseButtonRelease, pt, button, none)
    settle(0.6)


def escape():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if "Quarter" in name or "View3DInventorViewer" in name:
            for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
                QtWidgets.QApplication.sendEvent(
                    w, QtGui.QKeyEvent(kind, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier))
            break
    settle(0.5)


def one(command, sk, view, pixel):
    FreeCADGui.runCommand(command)
    settle(0.6)
    for p in POINTS:
        click(view, p)
    # the poles, before the curve is made: each click adds a circle
    poles = [g.Center for g in sk.Geometry if g.TypeId == "Part::GeomCircle"]
    off = [round(c.distanceToPoint(p), 3) for c, p in zip(poles, POINTS)]
    note("%s poles %s" % (command, [(round(c.x, 3), round(c.y, 3)) for c in poles]))
    check("%s: four clicks, four poles, each within a pixel of its click" % command,
          len(poles) == 4 and max(off) <= pixel, (off, "pixel %.3f" % pixel))
    # A right click ends the curve, and one Escape the tool (a second one
    # would leave the sketch).
    click(view, V(25, -12, 0), QtCore.Qt.RightButton)
    escape()
    check("%s: still editing the sketch" % command,
          FreeCADGui.getDocument(DOC).getInEdit() is not None)
    while len(sk.Geometry):
        sk.delGeometries(list(range(len(sk.Geometry))))
    sk.Document.recompute()
    settle(0.3)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
            "OnViewParameterVisibility", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/General").SetBool(
            "AdjustCamera", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "EnableEscape", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)
        view = gdoc.activeView()
        view.viewTop()
        view.setCameraType("Orthographic")
        gdoc.setEdit(sk)
        settle(1.0)
        cam = view.getCameraNode()
        cam.position.setValue(0.0, 0.0, cam.position.getValue()[2])
        cam.height.setValue(60.0)
        settle(0.8)
        a = view.getPointOnViewport(V(0, 0, 0))
        b = view.getPointOnViewport(V(0, 10, 0))
        pixel = 10.0 / abs(b[1] - a[1])
        note("one pixel is %.3f" % pixel)
        one("Sketcher_CreateBSpline", sk, view, pixel)
        one("Sketcher_CreatePeriodicBSpline", sk, view, pixel)
        gdoc.resetEdit()
        settle(0.5)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").RemBool("EnableEscape")
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").RemInt(
            "OnViewParameterVisibility")
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
