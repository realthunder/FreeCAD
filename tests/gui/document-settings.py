"""The rest of the document settings are listed, and the page stores no key nothing reads.

Ten more keys of Preferences/Document are behind App::DocumentParams
(docs/HandsOnQueue.md entry 24): auto recovery (on, interval, compressed,
binary shapes), recovery at startup, a new document at startup, undo (on,
steps), whether a view change modifies the document, and the indentation
of saved Python objects. Reading them for that found:

  - auto recovery was set up at the start and by OK on the Document page
    only: a change of its settings made any other way waited for the next
    start. It follows them now (not claimed here: nothing a script can see
    says what the timer is set to);
  - the Document page stored two keys nothing has ever read, from two check
    boxes it hides: SaveTransactions and TransactionsDiscard;
  - the auto saver switches thumbnails off while it saves, and read that
    setting as off where its default is on, so with the key not stored it
    switched nothing off (from the code; not claimed here).

Claims:

  - "/param auto-recovery interval" lists the interval, and "/param undo
    steps" the number of undo steps;
  - OK in the preferences, nothing changed, stores neither SaveTransactions
    nor TransactionsDiscard;
  - a document made with undo switched off records no undo steps, and one
    made with it on again does.

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


def ok_the_preferences():
    """Open the preferences, touch nothing, press OK."""
    state = {"ticks": 0, "pressed": 0}
    timer = QtCore.QTimer()

    def act():
        state["ticks"] += 1
        dialog = QtWidgets.QApplication.activeModalWidget()
        if dialog is None or state["ticks"] < 4:
            return
        if isinstance(dialog, QtWidgets.QMessageBox):
            QtCore.QTimer.singleShot(0, lambda: dialog.button(QtWidgets.QMessageBox.No).click())
            return
        if state["pressed"] % 10 == 0:
            QtCore.QTimer.singleShot(0, lambda: dialog.findChild(QtWidgets.QDialogButtonBox).button(
                QtWidgets.QDialogButtonBox.Ok).click())
        state["pressed"] += 1

    timer.timeout.connect(act)
    timer.start(500)
    FreeCADGui.showPreferences()
    timer.stop()
    settle(0.5)


def run():
    document = FreeCAD.ParamGet(PREFS + "Document")
    made = []
    try:
        rows = param_rows("auto-recovery interval")
        check("the omni search lists the auto-recovery interval",
              any(r.endswith("Document/AutoSaveTimeout") for r in rows), rows[:6])
        rows = param_rows("undo steps")
        check("and the number of undo steps", any(r.endswith("Document/MaxUndoSize") for r in rows), rows[:6])

        ok_the_preferences()
        check("OK in the preferences stores neither of the two keys nothing reads",
              stored("Document", "SaveTransactions") == [] and stored("Document", "TransactionsDiscard") == [],
              (stored("Document", "SaveTransactions"), stored("Document", "TransactionsDiscard")))

        document.SetBool("UsingUndo", False)
        settle(0.2)
        doc = FreeCAD.newDocument("Entry24NoUndo")
        made.append(doc.Name)
        settle(0.5)
        off = doc.UndoMode
        document.SetBool("UsingUndo", True)
        settle(0.2)
        doc = FreeCAD.newDocument("Entry24Undo")
        made.append(doc.Name)
        settle(0.5)
        check("a document made with undo off records none, one made with it on does",
              off == 0 and doc.UndoMode == 1, (off, doc.UndoMode))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in made:
            try:
                FreeCAD.closeDocument(name)
            except Exception:
                pass
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
