"""The notification area's settings are listed, and the area still follows them at once.

The eleven keys of Preferences/NotificationArea are behind
NotificationAreaParams (docs/HandsOnQueue.md entry 24): the omni search
lists them, and the notification area, the main window and the notify
helpers ask the class instead of reading the group with a default of their
own each. Nothing was found wrong in this group; the behaviour claims are
there to hold the conversion to what the area did before.

Claims:

  - "/param notification area" lists the switch of the area, and "/param
    notification width" the width (under the name its key has always had);
  - the area is in the status bar and visible;
  - with the area switched off it is hidden at once, and shown again when
    the setting is back.

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
PREFS = "User parameter:BaseApp/Preferences/"
Qt = QtCore.Qt


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def param_rows(query):
    FreeCADGui.runCommand("Std_OmniSearch")
    settle(0.6)
    edit = None
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == "OmniSearchEdit" and w.isVisible():
            edit = w
    if edit is None:
        return []
    text = "/param " + query
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(0.8)
    rows = []
    for w in QtWidgets.QApplication.topLevelWidgets():
        if not isinstance(w, QtWidgets.QAbstractItemView) or w.model() is None:
            continue
        found = [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
        # The list is not shown while another application is in front, which a
        # test cannot prevent on a desktop in use; its rows are there all the same.
        if w.isVisible() or any(r.startswith("Preferences/") for r in found):
            rows += found
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return rows


def area():
    return FreeCADGui.getMainWindow().findChild(QtWidgets.QPushButton, "notificationArea")


def run():
    group = FreeCAD.ParamGet(PREFS + "NotificationArea")
    try:
        rows = param_rows("notification area")
        check("the omni search lists the notification area's switch",
              any(r.endswith("NotificationArea/NotificationAreaEnabled") for r in rows), rows[:6])
        rows = param_rows("notification width")
        check("and its width", any(r.endswith("NotificationArea/NotificiationWidth") for r in rows), rows[:6])

        widget = area()
        if check("the notification area is in the status bar, visible",
                 widget is not None and widget.isVisible(), widget):
            group.SetBool("NotificationAreaEnabled", False)
            settle(0.3)
            check("switched off, it is hidden at once", not widget.isVisible())
            group.SetBool("NotificationAreaEnabled", True)
            settle(0.3)
            check("and shown again when the setting is back", widget.isVisible())
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        group.RemBool("NotificationAreaEnabled")
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
