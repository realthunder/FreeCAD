"""The status bar's messageChanged signal hears the action messages too.

The main window's status bar carries two kinds of message. Console output
(showStatus) goes through QStatusBar::showMessage, so
`getMainWindow().statusBar().messageChanged` announced it. Action messages
(MainWindow::showMessage: "Selection not allowed by filter", preselection,
command hints) are written into a QLabel of the window's own, and nothing
announced them -- a macro or a test listening on the signal heard none.

Checked here:

  - an action message -- a selection the gate refuses -- arrives on
    messageChanged with its text;
  - announcing it leaves the console warning beside it alone: the main
    window itself listens on the signal and resets the console pane's
    colouring for any message it did not post, which an announced action
    message must not trigger;
  - when the action message times out, messageChanged announces the empty
    string, as QStatusBar does when its own message clears.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "StatusMessageSignal"
VIEW = "User parameter:BaseApp/Preferences/View"

state = {"done": False, "heard": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.0):
    end = time.monotonic() + seconds
    while True:
        QtCore.QCoreApplication.processEvents()
        if time.monotonic() >= end:
            break
        time.sleep(0.01)


def heard(msg):
    state["heard"].append(msg)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        # A short action message timeout, so the clear is seen.
        state["timeout"] = FreeCAD.ParamGet(VIEW).GetInt("StatusMessageTimeout", -12345)
        FreeCAD.ParamGet(VIEW).SetInt("StatusMessageTimeout", 500)

        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle(0.3)

        bar = FreeCADGui.getMainWindow().statusBar()
        bar.messageChanged.connect(heard)

        # A console warning first: it goes through QStatusBar itself and
        # colours the bar.
        FreeCAD.Console.PrintTranslatedUserWarning("Test", "console warning\n")
        settle(0.3)
        warned = (bar.currentMessage(), bar.styleSheet())
        check("a console warning is on the bar, coloured",
              "console warning" in warned[0] and warned[1] not in ("", "#statusBar{}"),
              warned)

        # An action message: a selection the gate refuses.
        state["heard"] = []
        FreeCADGui.Selection.addSelectionGate("SELECT Part::Feature SUBELEMENT Vertex")
        FreeCADGui.Selection.addSelection(DOC, "Box", "Face1")
        FreeCADGui.Selection.removeSelectionGate()
        settle(0.1)
        check("the refused selection's action message is heard on messageChanged",
              any("not allowed" in m.lower() for m in state["heard"]), state["heard"])
        now = (bar.currentMessage(), bar.styleSheet())
        check("and the console warning beside it keeps its text and colour",
              now == warned, "%s -> %s" % (warned, now))

        # The action message times out.
        state["heard"] = []
        settle(1.2)
        check("its timing out is heard as the empty string",
              "" in state["heard"], state["heard"])
        bar.messageChanged.disconnect(heard)
    except Exception:
        note("ABORT run:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.Selection.removeSelectionGate()
    except Exception:
        pass
    if state.get("timeout", -12345) == -12345:
        FreeCAD.ParamGet(VIEW).RemInt("StatusMessageTimeout")
    else:
        FreeCAD.ParamGet(VIEW).SetInt("StatusMessageTimeout", state["timeout"])
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
