"""The "B-spline from knots" commands run the unified B-spline tool.

Sketcher_CreateBSplineByInterpolation and its periodic sibling started a
handler of their own, from before the tools were rebuilt on controllers:
no tool widget, no on-view parameters, and while drawing only the polygon
through the knots, not the curve. Upstream folded interpolation into
DrawSketchHandlerBSpline as its "knots" method long ago, and that handler
is here at upstream's tip; the two commands start it now, as upstream's do.

Claims, for each of the two commands:

  - the tool widget is in the task panel while the tool runs, with its
    method box on the knots entry (it was not there at all);
  - four clicks and a right click make one B-spline through the four
    points, periodic for the periodic command, with its knot points.

Clicks are real press / release events on the viewport.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "BSplineFromKnots"
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


def tool_widget():
    """The tool's settings widget in the task panel, if it is shown: the
    current text of its method box, or None."""
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "SketcherGui::SketcherToolDefaultWidget" \
                and w.isVisible():
            combo = w.findChild(QtWidgets.QComboBox, "comboBox1")
            return combo.currentText() if combo is not None and combo.isVisible() else ""
    return None


def one(command, periodic, sk, view):
    doc = sk.Document
    before = len(sk.Geometry)
    FreeCADGui.runCommand(command)
    settle(0.6)
    method = tool_widget()
    check("%s: the tool widget is shown, on the knots method" % command,
          method is not None and "knot" in method.lower(), method)
    for p in POINTS:
        click(view, p)
    click(view, V(25, -12, 0), QtCore.Qt.RightButton)
    settle(0.6)
    doc.recompute()
    settle(0.3)  # an Escape in the same turn as the recompute is not what a user sends
    made = sk.Geometry[before:]
    splines = [g for g in made if g.TypeId == "Part::GeomBSplineCurve"]
    points = [g for g in made if g.TypeId == "Part::GeomPoint"]
    through = []
    if splines:
        curve = splines[0]
        for p in POINTS:
            u = curve.parameter(p)
            through.append(round(curve.value(u).distanceToPoint(p), 3))
    note("%s made %s" % (command, [g.TypeId.split("::")[-1] for g in made]))
    check("%s: one B-spline through the four points" % command,
          len(splines) == 1 and through and max(through) < 0.12, (len(splines), through))
    check("%s: %s, with a knot point at each" % (
              command, "periodic" if periodic else "not periodic"),
          len(splines) == 1 and splines[0].isPeriodic() == periodic and len(points) >= 4,
          (splines and splines[0].isPeriodic(), len(points)))
    # leave the tool, and what it made, for the next
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if "Quarter" in name or "View3DInventorViewer" in name:
            for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
                QtWidgets.QApplication.sendEvent(
                    w, QtGui.QKeyEvent(kind, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier))
            break
    settle(0.5)
    check("%s: no tool widget once the tool is left" % command, tool_widget() is None,
          tool_widget())
    while len(sk.Geometry):
        sk.delGeometries(list(range(len(sk.Geometry))))
    doc.recompute()
    settle(0.3)


def run():
    try:
        tools = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/Tools")
        tools.SetInt("OnViewParameterVisibility", 0)
        general = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/General")
        general.SetBool("AdjustCamera", False)
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
        check("no tool widget before a tool runs", tool_widget() is None, tool_widget())
        one("Sketcher_CreateBSplineByInterpolation", False, sk, view)
        one("Sketcher_CreatePeriodicBSplineByInterpolation", True, sk, view)
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
