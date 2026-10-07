"""The report view's settings are settings: listed, defaulted in one place, followed at once.

What the report view shows, in which colours, whether it follows its newest
line and whether it takes Python's output were keys the view read straight
from its parameter group, each with a default of its own at each reader. They
are behind ReportViewParams now (docs/HandsOnQueue.md entry 24), which is
what lists a setting in the omni search, holds its one default and tells the
view when it changed.

Claims:

  - the omni search's "/param go to end" and "/param record warnings" list
    the two settings;
  - a warning shows; with "Record warnings" off (the stored key, as the
    preference page and the context menu write it) the next does not, and a
    normal message still does; on again, it shows;
  - with "Record critical messages" off a critical message does not show and
    a normal one still does (the view used to switch NORMAL messages off for
    that key);
  - an error line is drawn in the error colour, and in another one as soon
    as that colour is changed;
  - with "Redirect Python output" off a print() does not reach the view,
    and does again when it is on.

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
GROUP = "User parameter:BaseApp/Preferences/OutputWindow"
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


def report_edit():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTextEdit):
        if "ReportOutput" in w.metaObject().className():
            return w
    return None


def block_with(edit, text):
    block = edit.document().begin()
    found = None
    while block.isValid():
        if text in block.text():
            found = block
        block = block.next()
    return found


def shown(edit, text):
    settle(1.6)
    return block_with(edit, text) is not None


def colour_of(edit, text):
    block = block_with(edit, text)
    if block is None:
        return None
    formats = block.layout().formats()
    return formats[-1].format.foreground().color().name() if formats else None


def param_titles(query):
    """The rows the omni search lists for a /param query: each is the setting's path."""
    FreeCADGui.runCommand("Std_OmniSearch")
    settle(0.6)
    edit = None
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == "OmniSearchEdit" and w.isVisible():
            edit = w
    if edit is None:
        return None
    text = "/param " + query
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(0.8)
    titles = []
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            titles += [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return titles


def run():
    group = FreeCAD.ParamGet(GROUP)
    try:
        mw = FreeCADGui.getMainWindow()
        mw.showNormal()
        mw.resize(1200, 800)
        edit = report_edit()
        if edit is None:
            raise RuntimeError("no report view")
        for dock in mw.findChildren(QtWidgets.QDockWidget):
            if dock.isAncestorOf(edit):
                dock.show()
                dock.raise_()
        settle(1.0)

        rows = param_titles("go to end") or []
        check("the omni search lists Go to end", any(r.endswith("OutputWindow/checkGoToEnd") for r in rows),
              "%d rows" % len(rows))
        rows = param_titles("record warnings") or []
        check("and Record warnings", any(r.endswith("OutputWindow/checkWarning") for r in rows), rows[:6])

        edit.clear()
        FreeCAD.Console.PrintWarning("alpha settings test: a warning\n")
        check("a warning shows", shown(edit, "alpha settings test"))
        group.SetBool("checkWarning", False)
        settle(0.3)
        FreeCAD.Console.PrintWarning("bravo settings test: a warning while off\n")
        FreeCAD.Console.PrintMessage("charlie settings test: a normal message\n")
        check("with warnings off the next does not", not shown(edit, "bravo settings test"))
        check("and a normal message still does", shown(edit, "charlie settings test"))
        group.SetBool("checkWarning", True)
        settle(0.3)
        FreeCAD.Console.PrintWarning("delta settings test: a warning again\n")
        check("on again, a warning shows", shown(edit, "delta settings test"))

        group.SetBool("checkCritical", False)
        settle(0.3)
        FreeCAD.Console.PrintCritical("echo settings test: a critical message while off\n")
        FreeCAD.Console.PrintMessage("foxtrot settings test: a normal message\n")
        check("with critical messages off one does not show", not shown(edit, "echo settings test"))
        check("and a normal message still does", shown(edit, "foxtrot settings test"))
        group.RemBool("checkCritical")
        settle(0.3)

        FreeCAD.Console.PrintError("golf settings test: an error\n")
        settle(1.6)
        first = colour_of(edit, "golf settings test")
        check("an error line is in the error colour", first == "#ff0000", first)
        group.SetUnsigned("colorError", 0x00aa55ff)
        settle(0.5)
        FreeCAD.Console.PrintError("hotel settings test: an error after the change\n")
        settle(1.6)
        second = colour_of(edit, "hotel settings test")
        check("and in the new one once it is changed", second == "#00aa55", second)
        group.RemUnsigned("colorError")
        settle(0.3)

        group.SetBool("RedirectPythonOutput", False)
        settle(0.3)
        print("india settings test: printed while not redirected")
        check("with the redirection off a print does not reach the view", not shown(edit, "india settings test"))
        group.SetBool("RedirectPythonOutput", True)
        settle(0.3)
        print("juliet settings test: printed while redirected")
        check("and does again when it is on", shown(edit, "juliet settings test"))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in ("checkWarning", "checkCritical", "RedirectPythonOutput"):
            group.RemBool(name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
