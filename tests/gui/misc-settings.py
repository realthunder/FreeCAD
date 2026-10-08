"""The settings of several small parameter groups are listed.

Fourteen settings that live two or three to a group -- the recent macros
menu, the gizmos' coarse steps, the cache directory, the shortcut timeout,
the workbench tab bar, the two start-up switches for high resolution
screens and software OpenGL, the dependency graph -- are behind MiscParams
(docs/HandsOnQueue.md entry 24), each under the group it has always been
in. Reading them for that found two readers with no default of their own:

  - the size of the recent macros menu was 12 where the menu is filled, 4
    where the Macro page applies it, and 0 where the menu is first sized;
  - the shortcut timeout was 300 ms at the start and 0 when the open
    program was told the key had changed, so a timeout that was stored and
    then removed left none (from the code; not claimed here).

The pages' defaults are held to the definitions by
preferences-ok-keeps-defaults.py.

Claims:

  - "/param size of recent macro list", "/param cache size limit" and
    "/param shortcut sequence timeout" list the settings of those names,
    each under its own group.

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


def run():
    try:
        rows = param_rows("size of recent macro list")
        check("the omni search lists the size of the recent macros menu",
              any("Preferences/RecentMacros/" in r for r in rows), rows[:6])
        rows = param_rows("cache size limit")
        check("the cache directory's limit", any("Preferences/CacheDirectory/" in r for r in rows), rows[:6])
        rows = param_rows("shortcut sequence timeout")
        check("and the shortcut timeout", any("Preferences/Shortcut/Settings/" in r for r in rows), rows[:6])
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
