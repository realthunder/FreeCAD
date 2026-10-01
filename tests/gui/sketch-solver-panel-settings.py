"""The sketch's solver panel is Core's TaskSolverMessages, with a settings
menu (upstream 35f151d99e).

The panel had its own copy of what Assembly's solver panel shares from
Core. Now it is that class: titled "Sketch Edit", and its settings
button drops a menu holding the auto-update switch (once the manual
update button's menu) and the grid, snap and rendering order widgets the
toolbar buttons drop down.

Measured on the panel of a sketch in edit: the settings button is shown
and has a menu; opening it fills the grid spacing from the sketch's
GridSize and leaves the widgets enabled; its auto-update checkbox writes
Mod/Sketcher AutoRecompute both ways. Before the change there was no
settings button on the panel.

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


def visible(cls, name):
    return [w for w in FreeCADGui.getMainWindow().findChildren(cls)
            if w.objectName() == name and w.isVisible()]


def run():
    try:
        import Part

        V = FreeCAD.Vector
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "AutoRecompute", False)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchSolverPanelSettings")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(10, 0, 0)), False)
        sk.ViewObject.GridSize = 7.5
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sk)
        state.update(sk=sk)
        QtCore.QTimer.singleShot(2000, probe)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def probe():
    try:
        sk = state["sk"]
        # Other panels name a button settingsButton too: find the solver
        # panel by its status label first, then the button beside it.
        labels = visible(QtWidgets.QLabel, "labelStatus")
        if not check("the solver panel is Core's (its status label is labelStatus)",
                     len(labels) == 1, len(labels)):
            raise RuntimeError("no solver panel")
        # Walk the panel's own children: a findChild here has handed back a
        # stale PySide wrapper ("already deleted") for a live button.
        button = next((c for c in labels[0].parentWidget().children()
                       if c.objectName() == "settingsButton"), None)
        buttons = [button] if button is not None and button.isVisible() else []
        if not check("the solver panel shows a settings button", len(buttons) == 1,
                     len(buttons)):
            raise RuntimeError("no settings button")
        menu = buttons[0].menu()
        if not check("which drops a menu", menu is not None):
            raise RuntimeError("no menu")
        menu.aboutToShow.emit()
        settle(0.2)

        boxes = [w for w in menu.findChildren(QtWidgets.QWidget) if w.objectName() == "gridSize"]
        check("the menu holds the grid spacing", len(boxes) == 1, len(boxes))
        if boxes:
            value = boxes[0].property("rawValue")
            check("filled from the sketch's GridSize as it opens",
                  value is not None and abs(value - 7.5) < 1e-6, value)
            check("and enabled", boxes[0].isEnabled())
        lists = menu.findChildren(QtWidgets.QListWidget)
        check("and the rendering order list", len(lists) == 1, len(lists))

        boxes = [w for w in menu.findChildren(QtWidgets.QCheckBox) if w.text() == "Auto-update"]
        if check("and the auto-update switch", len(boxes) == 1, len(boxes)):
            params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher")
            boxes[0].setChecked(True)
            settle(0.1)
            on = params.GetBool("AutoRecompute", False)
            boxes[0].setChecked(False)
            settle(0.1)
            off = params.GetBool("AutoRecompute", True)
            check("which writes AutoRecompute both ways", on and not off, (on, off))
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
