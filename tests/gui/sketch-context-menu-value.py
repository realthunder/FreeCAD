"""The sketch's context menu offers to change a constraint's value only for
a dimension (upstream a7b501c95c).

With one constraint selected the menu always offered
Sketcher_ChangeDimensionConstraint, which for a Horizontal or a
Coincident opens nothing: the datum dialog returns at once for a
constraint without a value. Now the entry is there for one dimensional
constraint alone.

Measured here, by a right click on empty space with the selection set:
a Distance alone offers the entry; a Horizontal alone does not; the two
together do not. Before the change the Horizontal offered it.
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
EMPTY = (20, 25)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.time() + seconds
    while time.time() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


class PopupCatcher:
    """Records the entries of any popup and closes it (a context menu's
    exec() is modal)."""

    def __init__(self):
        self.entries = None
        self.timer = QtCore.QTimer()
        self.timer.setInterval(30)
        self.timer.timeout.connect(self.poll)

    def poll(self):
        popup = QtWidgets.QApplication.activePopupWidget()
        if popup is not None:
            self.entries = [a.text() for a in popup.actions()]
            popup.close()

    def __enter__(self):
        self.timer.start()
        return self

    def __exit__(self, *exc):
        settle(0.3)
        self.poll()
        self.timer.stop()
        return False


def right_click(view, xy):
    from PySide6.QtTest import QTest
    w = view.graphicsView().viewport()
    x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
    dpr = w.devicePixelRatioF()
    p = QtCore.QPoint(int(x / dpr), int(w.height() - 1 - y / dpr))
    with PopupCatcher() as catcher:
        QTest.mouseMove(w, p)
        settle()
        QTest.mousePress(w, QtCore.Qt.RightButton, QtCore.Qt.NoModifier, p)
        settle()
        QTest.mouseRelease(w, QtCore.Qt.RightButton, QtCore.Qt.NoModifier, p)
        settle()
    return catcher.entries


def run():
    try:
        import Part
        import Sketcher
        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchContextMenuValue")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(40, 0, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Horizontal", 0))
        sk.addConstraint(Sketcher.Constraint("Distance", 0, 40.0))
        doc.recompute()
        state["sk"] = sk
        FreeCADGui.activeDocument().setEdit(sk)
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].fitAll()
        QtCore.QTimer.singleShot(2500, menus)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def menus():
    try:
        text = FreeCADGui.Command.get("Sketcher_ChangeDimensionConstraint").getInfo()["menuText"]
        text = text.replace("&", "")
        sel = FreeCADGui.Selection
        found = {}
        for name, subs in (("Distance", ["Constraint2"]), ("Horizontal", ["Constraint1"]),
                           ("both", ["Constraint1", "Constraint2"])):
            sel.clearSelection()
            for sub in subs:
                sel.addSelection(state["sk"], sub)
            settle(0.5)
            entries = right_click(state["view"], EMPTY)
            if entries is None:
                note("ABORT: no context menu for %s" % name)
                return
            found[name] = any(e.replace("&", "") == text for e in entries)
        check("a Distance alone offers to change its value", found["Distance"], text)
        check("a Horizontal alone does not", not found["Horizontal"])
        check("nor the two together", not found["both"])
        sel.clearSelection()
        FreeCADGui.activeDocument().resetEdit()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finally:
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
