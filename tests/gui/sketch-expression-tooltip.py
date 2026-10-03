"""Hovering a constraint driven by an expression shows the expression
(upstream 00c3422c1f).

The pointer over a dimension whose value comes from an expression puts
"fx = <expression>" (in mathematical italics) on the view as a tooltip;
moving off the constraint takes it away again. A constraint with a plain
value shows none.

Measured here on the view's GL widget, after real pointer moves: the
expression-driven Distance gives a tooltip holding its expression, and
the view shows it when Qt asks for a tooltip (a QHelpEvent delivered by
hand -- the 3D view alone drops it); the plain one none, and empty space
none.
Before the change there was no tooltip at all.
"""
import os
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
    import time
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def hover(view, xy):
    """A pointer move over the model point xy; returns the tooltip the view's
    widgets then carry, and what is preselected."""
    gv = view.graphicsView()
    vp = gv.viewport()
    x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    ev = QtGui.QMouseEvent(QtCore.QEvent.MouseMove, pos, gv.mapToGlobal(pos.toPoint()),
                           QtCore.Qt.NoButton, QtCore.Qt.NoButton, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)
    QtWidgets.QApplication.processEvents()
    tips = [w.toolTip() for w in (vp, gv) if w.toolTip()]
    pre = FreeCADGui.Selection.getPreselection()
    return (tips[0] if tips else ""), list(pre.SubElementNames)


def run():
    try:
        import Part
        import Sketcher
        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchExpressionToolTip")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(40, 0, 0)), False)
        sk.addGeometry(Part.LineSegment(V(0, -30, 0), V(30, -30, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Distance", 0, 40.0))
        sk.setLabelDistance(0, 12.0)
        sk.addConstraint(Sketcher.Constraint("Distance", 1, 30.0))
        sk.setLabelDistance(1, 12.0)
        sk.setExpression("Constraints[0]", "2 * 20 mm")
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sk)
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].fitAll()
        QtCore.QTimer.singleShot(2500, probe)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def probe():
    try:
        view = state["view"]
        tip, pre = hover(view, (20, 12))
        check("the pointer is on the expression-driven Distance", "Constraint1" in pre, pre)
        check("which shows its expression in a tooltip", "2 * 20" in tip and "=" in tip, repr(tip))
        # and the view shows it. Qt's own delay never fires under the test
        # harness (a QLabel's tooltip does not show either), so deliver the
        # event it sends: the 3D view drops it unless the sketch catches it.
        vp = view.graphicsView().viewport()
        pos = QtCore.QPoint(vp.width() // 2, vp.height() // 2)
        QtWidgets.QApplication.sendEvent(
            vp, QtGui.QHelpEvent(QtCore.QEvent.ToolTip, pos, vp.mapToGlobal(pos)))
        settle(0.5)
        shown = QtWidgets.QToolTip.isVisible() and "2 * 20" in QtWidgets.QToolTip.text()
        check("and the tooltip appears", shown,
              "%s %r" % (QtWidgets.QToolTip.isVisible(), QtWidgets.QToolTip.text()))
        tip, pre = hover(view, (15, -18))
        check("the pointer is on the plain Distance", "Constraint2" in pre, pre)
        check("which shows none", tip == "", repr(tip))
        hover(view, (20, 12))
        tip, pre = hover(view, (60, 40))
        check("empty space shows none", tip == "" and not any(pre), "%r %s" % (tip, pre))
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
