"""Dragging an arc by its centre, and a conic by its edge (upstream 2cd45b07f7).

An arc that is selected and then grabbed by its centre was dragged as an
edge: a selected element's point is widened to the element, and an arc's
edge drag holds a point on the rim, so the radius followed the cursor and
the centre stayed where it was. Now it drags as the same arc unselected
does.

An ellipse, an arc of an ellipse, of a hyperbola or of a parabola grabbed by
its edge had its centre put where the cursor went, not moved by as much as
the cursor moved: the drag was not relative to the press, as it already was
for a line or a B-spline, so the curve jumped the moment it was grabbed.

Driven on the desktop view with the element preselected through Gui.Selection
and real press / move / release events (see sketch-undo-during-drag.py).
"""
import math
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchDragArcConic"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.0):
    import time
    end = time.monotonic() + seconds
    while True:
        QtCore.QCoreApplication.processEvents()
        if time.monotonic() >= end:
            break
        time.sleep(0.01)


def viewport():
    from PySide import QtWidgets
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if ("Quarter" in name or "View3DInventorViewer" in name) and w.isVisible():
            return w
    return None


def mouse(w, typ, world, button, buttons):
    from PySide import QtWidgets
    view = FreeCADGui.getDocument(DOC).ActiveView
    p = view.getPointOnViewport(FreeCAD.Vector(world[0], world[1], 0))
    p = QtCore.QPointF(p[0], w.height() - 1 - p[1])
    QtWidgets.QApplication.sendEvent(
        w, QtGui.QMouseEvent(typ, p, w.mapToGlobal(p), button, buttons,
                             QtCore.Qt.NoModifier))
    settle()


def drag(sk, element, frm, to):
    w = viewport()
    left = QtCore.Qt.LeftButton
    FreeCADGui.Selection.setPreselection(sk, element)
    mouse(w, QtCore.QEvent.MouseButtonPress, frm, left, left)
    mid = ((frm[0] + to[0]) / 2, (frm[1] + to[1]) / 2)
    mouse(w, QtCore.QEvent.MouseMove, mid, QtCore.Qt.NoButton, left)
    mouse(w, QtCore.QEvent.MouseMove, to, QtCore.Qt.NoButton, left)
    mouse(w, QtCore.QEvent.MouseButtonRelease, to, left, QtCore.Qt.NoButton)
    settle(0.5)
    FreeCADGui.Selection.clearPreselection()
    FreeCADGui.Selection.clearSelection()
    settle(0.2)


def xy(v):
    return "(%.2f, %.2f)" % (v.x, v.y)


def run():
    try:
        import Part

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        # 0: an arc about (0, 0); 1: an ellipse about (60, 0), a = 20, b = 10
        sk.addGeometry(Part.ArcOfCircle(Part.Circle(V(0, 0, 0), V(0, 0, 1), 15),
                                        math.radians(30), math.radians(150)), False)
        sk.addGeometry(Part.Ellipse(V(80, 0, 0), V(60, 10, 0), V(60, 0, 0)), False)
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        state.update(sk=sk, doc=doc)
        QtCore.QTimer.singleShot(1500, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure():
    try:
        sk = state["sk"]
        view = FreeCADGui.getDocument(DOC).ActiveView
        cam = view.getCameraNode()
        cam.position.setValue(35.0, 0.0, cam.position.getValue()[2])
        cam.height.setValue(120.0)
        settle(1.0)

        # The arc grabbed by its centre (Vertex3: start, end, centre) and
        # moved by (10, -12), first unselected -- how the solver moves an
        # arc by its centre, which is not rigid (the ends lag, the radius
        # grows) -- then undone, selected, and the same again.
        doc = state["doc"]
        drag(sk, "Vertex3", (0.0, 0.0), (10.0, -12.0))
        plain = sk.Geometry[0]
        doc.undo()
        settle(0.5)
        back = sk.Geometry[0]
        check("undo puts the arc back", abs(back.Center.x) < 1e-6 and abs(back.Radius - 15) < 1e-6,
              "centre %s radius %.3f" % (xy(back.Center), back.Radius))
        FreeCADGui.Selection.addSelection(sk, "Edge1")
        settle(0.2)
        drag(sk, "Vertex3", (0.0, 0.0), (10.0, -12.0))
        arc = sk.Geometry[0]
        check("a selected arc grabbed by its centre is dragged by its centre",
              abs(arc.Center.x - 10.0) < 0.5 and abs(arc.Center.y + 12.0) < 0.5,
              "centre %s, want (10.00, -12.00)" % xy(arc.Center))
        check("the same as when it is not selected",
              (arc.Center - plain.Center).Length < 0.05 and abs(arc.Radius - plain.Radius) < 0.05,
              "centre %s radius %.3f; unselected %s radius %.3f"
              % (xy(arc.Center), arc.Radius, xy(plain.Center), plain.Radius))

        # The ellipse, grabbed on its edge at the end of its major axis and
        # moved by (5, 8): its centre moves by as much.
        c0 = sk.Geometry[1].Center
        drag(sk, "Edge2", (80.0, 0.0), (85.0, 8.0))
        c1 = sk.Geometry[1].Center
        check("an ellipse grabbed by its edge moves by the cursor's move",
              abs(c1.x - c0.x - 5.0) < 0.5 and abs(c1.y - c0.y - 8.0) < 0.5,
              "centre %s -> %s, want moved by (5.00, 8.00)" % (xy(c0), xy(c1)))

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
