"""The settings of Preferences/General are settings: listed, and stored where they are read.

The keys of the General group -- the language, the number format, the icon
sizes, the splash screen, the export file name patterns and two dozen more
-- were read straight from the group at some thirty places. They are behind
GeneralParams now (docs/HandsOnQueue.md entry 24), which lists them in the
omni search with one default each. Reading them for that found three things
that did not work:

  - the General page's "tool tip icon size" stored a key of the General
    group that nothing reads; the size in use is the View group's;
  - the "apply preferences at once" box is meant to apply ITSELF at once,
    and called a read where it meant the write;
  - the change handler of the decimal separator setting was registered
    under a name that is not the key's (not claimed here: nothing a script
    can see changes with it);
  - "apply preferences at once" switched OFF did nothing for a dialog
    opened afterwards: a preference widget made while it is off connected
    its save all the same, so every change was still stored as it was made
    (found later, with the test that cancels the preferences untouched).

Claims:

  - "/param splash" lists the splash screen setting, and "/param export
    file name" the two patterns;
  - a tool tip icon size set on the General page is stored in the View
    group, and nothing of that name in the General group;
  - unticking "apply preferences at once" stores the setting before OK is
    pressed;
  - a box changed in the preferences is stored before OK while preferences
    are applied at once, and only by OK while they are not.

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


def stored(group, name):
    return [value for kind, key, value in FreeCAD.ParamGet(PREFS + group).GetContents() or [] if key == name]


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


def in_preferences(action):
    """Open the General page, run action(dialog) inside the dialog and press OK. Returns its result."""
    got = {}

    def act():
        dialog = QtWidgets.QApplication.activeModalWidget()
        try:
            if dialog is None:
                got["error"] = "no modal dialog"
                return
            got["value"] = action(dialog)
        except Exception:
            got["error"] = traceback.format_exc().replace("\n", " | ")
        finally:
            if dialog is not None:
                dialog.findChild(QtWidgets.QDialogButtonBox).button(QtWidgets.QDialogButtonBox.Ok).click()

    QtCore.QTimer.singleShot(2000, act)
    FreeCADGui.showPreferences("General", 0)
    settle(1.0)
    if "error" in got:
        note("in the dialog: " + got["error"])
    return got.get("value")


def run():
    general = FreeCAD.ParamGet(PREFS + "General")
    view = FreeCAD.ParamGet(PREFS + "View")
    notification = FreeCAD.ParamGet(PREFS + "NotificationArea")
    try:
        rows = param_rows("splash")
        check("the omni search lists the splash screen setting",
              any(r.endswith("General/ShowSplasher") for r in rows), rows[:6])
        rows = param_rows("export file name")
        check("and the two export file name patterns",
              sum(1 for r in rows if "General/ExportDefaultFilename" in r) == 2, rows[:6])

        def set_size(dialog):
            box = dialog.findChild(QtWidgets.QSpinBox, "toolTipIconSize")
            box.setValue(96)
            return box.value()

        value = in_preferences(set_size)
        check("the page has the tool tip icon size box", value == 96, value)
        check("a size set there is stored in the View group", stored("View", "ToolTipIconSize") == [96],
              stored("View", "ToolTipIconSize"))
        check("and nothing of that name in the General group", stored("General", "ToolTipIconSize") == [],
              stored("General", "ToolTipIconSize"))
        view.RemInt("ToolTipIconSize")
        general.RemInt("ToolTipIconSize")

        def untick(dialog):
            box = dialog.findChild(QtWidgets.QCheckBox, "AutoApply")
            before = box.isChecked()
            box.setChecked(False)
            QtCore.QCoreApplication.processEvents()
            return before, stored("General", "AutoApplyPreference")

        value = in_preferences(untick)
        check("unticking 'apply at once' stores the setting before OK", value is not None and value[1] == [False],
              value)
        general.RemBool("AutoApplyPreference")
        settle(0.3)

        # A box of a page nobody is looking at, whose setting moves nothing
        # on screen. The widget saves a tenth of a second after a change,
        # when it saves at all.
        def untick_auto_remove(dialog):
            box = dialog.findChild(QtWidgets.QCheckBox, "autoRemoveUserNotifications")
            before = box.isChecked()
            box.setChecked(not before)
            settle(0.6)
            return before, stored("NotificationArea", "AutoRemoveUserNotifications")

        value = in_preferences(untick_auto_remove)
        check("applied at once, a box unticked in the preferences is stored before OK",
              value == (True, [False]), value)
        notification.RemBool("AutoRemoveUserNotifications")
        general.SetBool("AutoApplyPreference", False)
        settle(0.3)
        value = in_preferences(untick_auto_remove)
        check("not applied at once, it is not stored before OK", value == (True, []), value)
        check("and OK stores it", stored("NotificationArea", "AutoRemoveUserNotifications") == [False],
              stored("NotificationArea", "AutoRemoveUserNotifications"))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        general.RemBool("AutoApplyPreference")
        notification.RemBool("AutoRemoveUserNotifications")
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
