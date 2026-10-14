"""TechDraw's colour page has a check box for the page view's ground following
the 3D view's.

docs/HandsOnQueue.md entry 112: "add a setting to gate techdraw background
default same as 3d view". The setting, Mod/TechDraw/Colors/BackgroundAs3DView,
was a parameter with no control; the page that holds the page view's own
background colour has one now.

Claims, the preferences open on TechDraw's pages:
  - the check box is there, and checked while the setting has never been
    written (its default);
  - unchecked and applied, the setting is off; checked and applied, on.

Runs with QT_QPA_PLATFORM=offscreen as well as on a screen; the pointer is not
moved. The dialog is left with close(), not reject(): a rejected dialog with a
setting touched asks whether to revert.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
COLORS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/Colors")
GOT = {"done": False, "ticks": 0}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def written():
    """The setting as it is stored: None while it has never been written"""
    for kind, key, value in COLORS.GetContents() or []:
        if key == "BackgroundAs3DView":
            return bool(value)
    return None


def apply(dialog):
    for box in dialog.findChildren(QtWidgets.QDialogButtonBox):
        button = box.button(QtWidgets.QDialogButtonBox.Apply)
        if button is not None:
            button.click()
            return True
    return False


def look(dialog):
    box = None
    for w in dialog.findChildren(QtWidgets.QCheckBox):
        if w.objectName() == "pcbBackgroundAs3DView":
            box = w
    if not check("the colour page has the check box", box is not None,
                 sorted(w.objectName() for w in dialog.findChildren(QtWidgets.QCheckBox)
                        if w.objectName().startswith("pcb"))[:12]):
        return
    check("checked while the setting has never been written",
          box.isChecked() and written() is None, (box.isChecked(), written()))
    box.setChecked(False)
    settle(200)
    check("the dialog has an Apply button", apply(dialog))
    settle(600)
    check("unchecked and applied: the setting is off", written() is False, written())
    box.setChecked(True)
    settle(200)
    apply(dialog)
    settle(600)
    check("checked and applied: the setting is on", written() is True, written())


def act():
    GOT["ticks"] += 1
    dialog = QtWidgets.QApplication.activeModalWidget()
    if dialog is None:
        return
    if isinstance(dialog, QtWidgets.QMessageBox):
        note("INFO a box came up: %s" % dialog.text())
        QtCore.QTimer.singleShot(0, lambda: dialog.button(QtWidgets.QMessageBox.No).click())
        return
    if GOT["done"]:
        if GOT["ticks"] % 6 == 0:
            QtCore.QTimer.singleShot(0, dialog.close)
        return
    GOT["done"] = True
    try:
        look(dialog)
    except Exception:
        note("ABORT: " + traceback.format_exc().replace("\n", " | "))
    QtCore.QTimer.singleShot(0, dialog.close)


def run():
    COLORS.RemBool("BackgroundAs3DView")
    # the module's pages are there once its workbench has been loaded
    try:
        FreeCADGui.activateWorkbench("TechDrawWorkbench")
    except Exception:
        note("INFO the TechDraw workbench: " + traceback.format_exc().replace("\n", " | "))
    settle(1500)
    timer = QtCore.QTimer()
    timer.timeout.connect(act)
    timer.start(500)
    try:
        FreeCADGui.showPreferences("TechDraw", 0)
    except Exception:
        note("ABORT: " + traceback.format_exc().replace("\n", " | "))
    timer.stop()
    settle(500)
    check("the preferences were looked at", GOT["done"])
    COLORS.RemBool("BackgroundAs3DView")
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(2500, run)
