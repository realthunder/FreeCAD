"""A B-spline tool that is cancelled leaves nothing, and one that finishes is
one undo step.

The B-spline tools put geometry in the document as they go: a control
point's circle, or a knot's point, at every click, and the curve at the
end. They open their command when they start -- inside the command that
started them -- and Gui::Command closes whatever transaction is open when
it returns. So on the first use of the tool the points were added with no
transaction open at all: cancelling after one point left its circle in the
sketch, with no undo step to take it back, and the interpolation tool,
which opens another command on the way, left that one open after the tool
was gone. Upstream's own form of this is issue #12473 (`55c36e8c03`).

Claims, each in a document of its own so that one case's leftovers cannot
reach the next:

  - one point, then a right click or Escape, continuous mode off: the tool
    is gone, the sketch is empty, no transaction is open, nothing is on the
    undo stack -- for the control point tool and the knot tool;
  - the same with continuous mode on: the sketch is empty, the tool runs
    on, and leaving it leaves no transaction behind;
  - four points and a right click, continuous mode off: one B-spline, one
    undo step, and that one undo empties the sketch. This crashed: the
    state change to End finishes the tool and, with continuous mode off,
    deletes it, and quit() then called finish() again on the deleted
    handler. Upstream's text has the same second call.

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
V = FreeCAD.Vector
POINTS = [V(-20, -5, 0), V(-8, 9, 0), V(6, -7, 0), V(18, 6, 0)]
AWAY = V(25, -12, 0)
state = {"done": False, "docs": 0}


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


def escape(view):
    gv = view.graphicsView()
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(
            gv, QtGui.QKeyEvent(kind, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier))


def tool_running():
    """The tool's settings widget is in the task panel while a tool runs."""
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "SketcherGui::SketcherToolDefaultWidget" \
                and w.isVisible():
            return True
    return False


def sketch(continuous):
    """A sketch in edit in a document of its own. The documents stay open
    until the end: closing one deletes its view under PySide's wrapper."""
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
        "ContinuousCreationMode", continuous)
    state["docs"] += 1
    doc = FreeCAD.newDocument("BSplineCancel%d" % state["docs"])
    doc.UndoMode = 1
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    doc.recompute()
    gdoc = FreeCADGui.getDocument(doc.Name)
    view = gdoc.activeView()
    view.viewTop()
    view.setCameraType("Orthographic")
    gdoc.setEdit(sk)
    settle(1.0)
    cam = view.getCameraNode()
    cam.position.setValue(0.0, 0.0, cam.position.getValue()[2])
    cam.height.setValue(60.0)
    settle(0.8)
    return doc, sk, view


def left(doc, sk):
    return (len(sk.Geometry), FreeCAD.getActiveTransaction(), doc.UndoNames)


def close(doc):
    """Out of the sketch, whatever the case left running: the next case's
    right click must find its own tool and nothing else."""
    FreeCADGui.getDocument(doc.Name).resetEdit()
    settle(0.5)


def cancel(view, how):
    if how == "a right click":
        click(view, AWAY, QtCore.Qt.RightButton)
    else:
        escape(view)
    settle(0.8)


def cancelled(command, what, how):
    doc, sk, view = sketch(False)
    FreeCADGui.runCommand(command)
    settle(0.6)
    click(view, POINTS[0])
    placed = len(sk.Geometry)
    cancel(view, how)
    check("%s, one point then %s: the point was there" % (what, how), placed == 1, placed)
    check("%s, one point then %s: the tool is gone" % (what, how), not tool_running())
    check("%s, one point then %s: nothing left, no transaction open, nothing to undo"
          % (what, how), left(doc, sk) == (0, None, []), left(doc, sk))
    close(doc)


def cancelled_continuous(command, what):
    doc, sk, view = sketch(True)
    FreeCADGui.runCommand(command)
    settle(0.6)
    click(view, POINTS[0])
    cancel(view, "a right click")
    check("%s, continuous, one point then a right click: the sketch is empty" % what,
          len(sk.Geometry) == 0, left(doc, sk))
    check("%s, continuous: the tool runs on" % what, tool_running())
    escape(view)
    settle(0.6)
    check("%s, continuous: left, and nothing is left behind" % what,
          not tool_running() and left(doc, sk) == (0, None, []), left(doc, sk))
    close(doc)


def finished(command, what):
    """With continuous mode off: finishing deletes the handler, and the tools
    went on to use it -- a crash on the right click, before the change."""
    doc, sk, view = sketch(False)
    FreeCADGui.runCommand(command)
    settle(0.6)
    for p in POINTS:
        click(view, p)
    if not check("%s, four points: the tool is still running" % what, tool_running()):
        close(doc)
        return  # a right click with no tool opens a menu nothing here answers
    click(view, AWAY, QtCore.Qt.RightButton)
    settle(0.8)
    splines = [g for g in sk.Geometry if g.TypeId == "Part::GeomBSplineCurve"]
    check("%s, four points and a right click: one B-spline" % what, len(splines) == 1,
          [g.TypeId.split("::")[-1] for g in sk.Geometry])
    check("%s: it is one undo step, and none is left open" % what,
          len(doc.UndoNames) == 1 and FreeCAD.getActiveTransaction() is None, left(doc, sk))
    doc.undo()
    settle(0.5)
    check("%s: that one undo empties the sketch" % what, len(sk.Geometry) == 0,
          [g.TypeId.split("::")[-1] for g in sk.Geometry])
    close(doc)


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
        poles = ("Sketcher_CreateBSpline", "control points")
        knots = ("Sketcher_CreateBSplineByInterpolation", "knots")
        for command, what in (poles, knots):
            for how in ("a right click", "Escape"):
                cancelled(command, what, how)
        cancelled_continuous(*poles)
        finished(*poles)
        finished(*knots)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        sketcher = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher")
        sketcher.RemBool("EnableEscape")
        sketcher.RemBool("ContinuousCreationMode")
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").RemInt(
            "OnViewParameterVisibility")
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        try:
            FreeCADGui.getDocument(name).resetEdit()
        except Exception:
            pass
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
