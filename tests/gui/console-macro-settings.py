"""The Python console's, the macros' and the dialogs' settings are listed and followed.

The keys of Preferences/PythonConsole, Preferences/Macro and
Preferences/Dialog are behind PythonConsoleParams, MacroParams and
DialogParams (docs/HandsOnQueue.md entry 24). Reading them for that found:

  - a macro path stored EMPTY was taken for the path: the macro dialogs
    listed nothing and FreeCAD.getUserMacroDir(True) returned "";
  - Draft's ShapeString panel sets the file dialog switch while it is open
    and puts it back when it closes -- to False, whatever it was, because
    what it remembered was reset right after it was read. A user who chose
    Qt's file dialog lost that with every ShapeString, and a key that was
    not stored was stored;
  - the Macro dialog stored three hidden switches each time it read them
    ("create parameter"), so that they could be found in the parameter
    editor. They are found in the omni search now, and no longer stored.

Claims:

  - "/param word wrap", "/param macro path" and "/param file dialog" list
    the console's wrap switch, the macro path and the file dialog switch;
  - the console follows its word wrap and block cursor settings at once;
  - with "show script commands in Python console" off a command issued is
    not echoed in the console, and is again when it is back on;
  - a macro path stored empty means the user's macro directory;
  - Draft's ShapeString panel leaves the file dialog switch as it found
    it: stored True stays True, not stored stays not stored.

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
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            rows += [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return rows


def python_console():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QPlainTextEdit):
        if w.metaObject().className() == "Gui::PythonConsole":
            return w
    return None


def shapestring_round_trip():
    """Make Draft's ShapeString panel and close it the way Cancel does; no document is needed for that."""
    import drafttaskpanels.task_shapestring as task

    panel = task.ShapeStringTaskPanel()
    settle(0.2)
    panel.platform_win_dialog("Restore")
    panel.form.deleteLater()
    settle(0.2)


def run():
    console_group = FreeCAD.ParamGet(PREFS + "PythonConsole")
    macro = FreeCAD.ParamGet(PREFS + "Macro")
    dialog = FreeCAD.ParamGet(PREFS + "Dialog")
    try:
        rows = param_rows("word wrap")
        check("the omni search lists the console's word wrap",
              any(r.endswith("PythonConsole/PythonWordWrap") for r in rows), rows[:6])
        rows = param_rows("macro path")
        check("the macro path", any(r.endswith("Macro/MacroPath") for r in rows), rows[:6])
        rows = param_rows("file dialog")
        check("and the file dialog switch", any(r.endswith("Dialog/DontUseNativeDialog") for r in rows), rows[:6])

        console = python_console()
        if check("the Python console is there", console is not None):
            wrapped = console.wordWrapMode()
            console_group.SetBool("PythonWordWrap", False)
            settle(0.2)
            check("word wrap switched off, the console stops wrapping at once",
                  wrapped != QtGui.QTextOption.NoWrap and console.wordWrapMode() == QtGui.QTextOption.NoWrap,
                  (wrapped, console.wordWrapMode()))
            console_group.RemBool("PythonWordWrap")
            settle(0.2)
            check("and wraps again when the setting is its default again", console.wordWrapMode() == wrapped,
                  console.wordWrapMode())
            console_group.SetBool("PythonBlockCursor", True)
            settle(0.2)
            check("the block cursor is a character wide", console.cursorWidth() > 1, console.cursorWidth())
            console_group.RemBool("PythonBlockCursor")
            settle(0.2)
            check("and a line again without the setting", console.cursorWidth() == 1, console.cursorWidth())

            FreeCADGui.doCommand("entry24_marker_one = 1")
            settle(0.3)
            shown = "entry24_marker_one" in console.toPlainText()
            macro.SetBool("ScriptToPyConsole", False)
            settle(0.2)
            FreeCADGui.doCommand("entry24_marker_two = 2")
            settle(0.3)
            hidden = "entry24_marker_two" not in console.toPlainText()
            macro.RemBool("ScriptToPyConsole")
            settle(0.2)
            FreeCADGui.doCommand("entry24_marker_three = 3")
            settle(0.3)
            again = "entry24_marker_three" in console.toPlainText()
            check("a command is echoed in the console, not with the echo off, and again with it back",
                  shown and hidden and again, (shown, hidden, again))

        default_dir = FreeCAD.getUserMacroDir(False)
        macro.SetString("MacroPath", "")
        settle(0.2)
        check("a macro path stored empty means the user's macro directory",
              FreeCAD.getUserMacroDir(True) == default_dir, (FreeCAD.getUserMacroDir(True), default_dir))
        macro.RemString("MacroPath")

        try:
            dialog.SetBool("DontUseNativeDialog", True)
            shapestring_round_trip()
            check("Draft's ShapeString panel leaves a file dialog switch that is on, on",
                  stored("Dialog", "DontUseNativeDialog") == [True], stored("Dialog", "DontUseNativeDialog"))
            dialog.RemBool("DontUseNativeDialog")
            shapestring_round_trip()
            check("and one that is not stored, not stored", stored("Dialog", "DontUseNativeDialog") == [],
                  stored("Dialog", "DontUseNativeDialog"))
        except Exception:
            note("FAIL Draft's ShapeString panel could be made | " + traceback.format_exc().replace("\n", " | "))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        console_group.RemBool("PythonWordWrap")
        console_group.RemBool("PythonBlockCursor")
        macro.RemBool("ScriptToPyConsole")
        macro.RemString("MacroPath")
        dialog.RemBool("DontUseNativeDialog")
        dialog.RemBool("DontUseNativeFontDialog")
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
