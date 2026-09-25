"""Double clicks in a sketch edit.

A double click on an edge selects the whole wire it is part of, joined end
to end, external edges included; a second one deselects it (upstream
6db820a580, a9bff78974, 0b1187b2cd). The fork had none of it -- the ledger
took it for synced -- and a double click on an edge only logged.

A double click on the sketch that is already in edit aligns the view to
it instead of leaving the edit and entering it again (upstream 321a782eff,
issue 13826).

Clicks are real press / release events on the viewport, with the edge
preselected through Gui.Selection the way sketch-undo-during-drag.py does
(a synthetic move preselects nothing under the harness).

Scored against the tree before the fix: see the commit message.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchDoubleClick"
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


def click(sk, element, world):
    w = viewport()
    left = QtCore.Qt.LeftButton
    FreeCADGui.Selection.setPreselection(sk, element)
    mouse(w, QtCore.QEvent.MouseButtonPress, world, left, left)
    mouse(w, QtCore.QEvent.MouseButtonRelease, world, left, QtCore.Qt.NoButton)


def double_click(sk, element, world):
    click(sk, element, world)
    click(sk, element, world)
    settle(0.3)


def selected():
    sel = FreeCADGui.Selection.getSelectionEx(DOC)
    return sorted(n for s in sel for n in s.SubElementNames)


def run():
    try:
        import Part
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        box.Placement.Base = V(10, 10, 0)
        doc.recompute()
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        # The box's bottom edge from (20,10) to (20,20) closes the loop.
        ext = None
        for i, e in enumerate(box.Shape.Edges):
            a, b = e.Vertexes[0].Point, e.Vertexes[1].Point
            ends = sorted([(round(a.x), round(a.y), round(a.z)), (round(b.x), round(b.y), round(b.z))])
            if ends == [(20, 10, 0), (20, 20, 0)]:
                ext = "Edge%d" % (i + 1)
        sk.addExternal("Box", ext)
        for a, b in (((20, 10), (30, 10)), ((30, 10), (30, 20)), ((30, 20), (20, 20)),
                     ((40, 10), (50, 10))):
            sk.addGeometry(Part.LineSegment(V(a[0], a[1], 0), V(b[0], b[1], 0)), False)
        doc.recompute()
        box.Visibility = False
        FreeCADGui.activeDocument().setEdit(sk)
        FreeCADGui.activeDocument().activeView().viewTop()
        state["sk"] = sk
        QtCore.QTimer.singleShot(1500, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure():
    try:
        sk = state["sk"]
        view = FreeCADGui.getDocument(DOC).ActiveView
        cam = view.getCameraNode()
        cam.position.setValue(30.0, 15.0, cam.position.getValue()[2])
        cam.height.setValue(60.0)
        settle(1.0)

        loop = ["Edge1", "Edge2", "Edge3", "ExternalEdge1"]
        FreeCADGui.Selection.clearSelection()
        settle(1.0)  # past the double-click interval of anything before
        double_click(sk, "Edge2", (30.0, 15.0))
        got = selected()
        first = check("a double click on an edge selects its wire, external edge included",
                      got == loop, got)

        settle(1.0)
        double_click(sk, "Edge2", (30.0, 15.0))
        got = selected()
        check("a second double click deselects it", first and got == [], got)

        settle(1.0)
        click(sk, "Edge4", (45.0, 10.0))
        settle(1.0)
        check("a single click selects the one edge", selected() == ["Edge4"], selected())

        # The sketch double clicked while it is in edit.
        view.viewIsometric()
        settle(0.5)
        vp = FreeCADGui.getDocument(DOC).getObject("Sketch")
        vp.doubleClicked()
        settle(1.5)
        gdoc = FreeCADGui.getDocument(DOC)
        check("double clicking the sketch in edit keeps it in edit",
              gdoc.getInEdit() is not None and gdoc.getInEdit().Object.Name == "Sketch")
        # Leaving and entering it again dropped the selection; the edit
        # graph's nodes survive a re-entry, so they cannot tell.
        check("without leaving and entering it again: the selection stays",
              selected() == ["Edge4"], selected())
        d = view.getViewDirection()
        # (entering the edit again aligned it too)
        check("and the view is aligned to the sketch", abs(d.z + 1.0) < 1e-3, d)

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
