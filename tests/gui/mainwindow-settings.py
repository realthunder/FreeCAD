"""The main window's and the theme's settings are listed, and two of them follow a change that did not.

The keys of Preferences/MainWindow and the three accent colours of
Preferences/Themes are behind MainWindowParams and ThemeParams
(docs/HandsOnQueue.md entry 24). Reading them for that found, among others:

  - the accent colours 2 and 3 had four defaults: the style sheet's own,
    the first accent's on the Theme page (which OK then stored for all
    three), the same in a theme being saved, and black in the style
    parameter source;
  - a change of the global tool bar area alone moved nothing: the change was
    only looked for under the name of the workbench tool bar area.

Claims:

  - "/param accent colour" lists the three accent colours, and "/param
    colour scheme" the scheme;
  - the Theme page shows, for accent colours that are not stored, the
    colours the style sheet uses for them, three different ones;
  - with the global tool bar area set to Left the File tool bar is on the
    left shortly after, and back on top when the setting is.

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


def theme_page_accents():
    """The colours the three accent buttons of the preferences show, read with the dialog open; it is cancelled."""
    got = {"ticks": 0, "cancelled": 0}
    timer = QtCore.QTimer()

    def click(owner, find):
        # From a timer of its own: Cancel may raise a box, a nested event
        # loop, and the timer looking for that box does not fire while its
        # own slot is still inside one. The button is looked up then, from
        # its dialog: one taken from a button box nobody references any
        # more is a dead object to PySide.
        QtCore.QTimer.singleShot(0, lambda: find(owner).click())

    def act():
        got["ticks"] += 1
        dialog = QtWidgets.QApplication.activeModalWidget()
        if dialog is None:
            return
        try:
            if isinstance(dialog, QtWidgets.QMessageBox):
                got.setdefault("boxes", []).append(dialog.text())
                click(dialog, lambda box: box.button(QtWidgets.QMessageBox.No))
                return
            if "value" not in got:
                got["value"] = [
                    button.property("color").name() if button is not None else None
                    for button in (dialog.findChild(QtWidgets.QPushButton, name)
                                   for name in ("ThemeAccentColor1", "ThemeAccentColor2", "ThemeAccentColor3"))]
            if got["cancelled"] % 10 == 0:
                click(dialog, lambda d: d.findChild(QtWidgets.QDialogButtonBox).button(
                    QtWidgets.QDialogButtonBox.Cancel))
            got["cancelled"] += 1
        except Exception:
            got["error"] = traceback.format_exc().replace("\n", " | ")

    timer.timeout.connect(act)
    timer.start(500)
    FreeCADGui.showPreferences()
    timer.stop()
    settle(1.0)
    if "error" in got:
        note("in the dialog: " + got["error"])
    if "boxes" in got:
        note("INFO Cancel asked: %s" % got["boxes"])
    return got.get("value")


def file_toolbar_area():
    mw = FreeCADGui.getMainWindow()
    for bar in mw.findChildren(QtWidgets.QToolBar):
        if bar.objectName() == "File":
            return mw.toolBarArea(bar)
    return None


def run():
    main = FreeCAD.ParamGet(PREFS + "MainWindow")
    try:
        rows = param_rows("accent colour")
        check("the omni search lists the three accent colours",
              sum(1 for r in rows if "Themes/ThemeAccentColor" in r) == 3, rows[:6])
        rows = param_rows("colour scheme")
        check("and the colour scheme", any(r.endswith("MainWindow/ColorScheme") for r in rows), rows[:6])

        stored = [n for k, n, v in FreeCAD.ParamGet(PREFS + "Themes").GetContents() or [] if n.startswith("ThemeAccent")]
        colours = theme_page_accents()
        if not stored:
            check("the Theme page shows the style sheet's accent colours for unset ones",
                  colours == ["#557bb6", "#405c89", "#4b6ca0"], colours)
        else:
            note("accent colours are stored in this profile (%s): the page's defaults not judged" % stored)

        top = file_toolbar_area()
        if check("the File tool bar is there, on top", top == Qt.TopToolBarArea, top):
            main.SetString("GlobalToolBarArea", "Left")
            settle(1.5)
            check("with the global area set to Left it is on the left", file_toolbar_area() == Qt.LeftToolBarArea,
                  file_toolbar_area())
            main.SetString("GlobalToolBarArea", "Top")
            settle(1.5)
            check("and back on top when the setting is", file_toolbar_area() == Qt.TopToolBarArea,
                  file_toolbar_area())
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        main.RemString("GlobalToolBarArea")
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
