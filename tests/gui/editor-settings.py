"""The editors' settings are listed, and a macro editor opened later has the stored ones.

The keys of Preferences/Editor are behind EditorParams
(docs/HandsOnQueue.md entry 24). Reading them for that found:

  - Spaces was off to the Tab key and on to the automatic indentation
    after Enter, so in one editor Tab inserted a tab character and Enter
    indented with spaces. The Editor page showed "Keep tabs" for it, and OK
    stored that;
  - the Editor page looked for the font in use under a generic family name
    no list of fonts has, showed the first fixed-pitch font instead and OK
    stored it;
  - the report view's line limit is the view's own, stored by its context
    menu, and was read at every start from the Editor group, which has no
    such key: a limit that had been set was 10000 again (not claimed here,
    it takes a second start; measured with a seeded profile, see the
    commit message).

The Editor page's defaults are held to the definitions by
preferences-ok-keeps-defaults.py.

Claims:

  - "/param font size", "/param keyword colour" and "/param block comment"
    list the font size, the keyword colour and the block comment colour;
  - with a keyword colour stored, a macro editor opened afterwards draws a
    keyword in it (it did before: the editor's colour table is the class's
    now, and this holds the editor to what it did);
  - on a profile that stores nothing, Tab in a macro editor inserts four
    spaces;
  - the report view takes a line limit when it is set, and its default
    when the key is removed.

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
KEEP = []


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


def sweep_boxes():
    """A box nobody answers would hold the session on the desktop: it is refused, and named."""
    box = QtWidgets.QApplication.activeModalWidget()
    if isinstance(box, QtWidgets.QMessageBox):
        note("INFO a box was refused: " + box.text()[:120])
        for role in (QtWidgets.QMessageBox.Discard, QtWidgets.QMessageBox.No, QtWidgets.QMessageBox.Cancel):
            if box.button(role) is not None:
                QtCore.QTimer.singleShot(0, lambda b=box, r=role: b.button(r).click())
                return
        QtCore.QTimer.singleShot(0, box.reject)


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


def widget_of_class(name):
    for w in QtWidgets.QApplication.allWidgets():
        if w.metaObject().className() == name:
            return w
    return None


def first_word_colour(editor):
    """The colour the syntax highlighter gave the first word of the first line."""
    block = editor.document().firstBlock()
    for span in block.layout().formats():
        if span.start == 0:
            return span.format.foreground().color().name()
    return None


def run():
    editor_group = FreeCAD.ParamGet(PREFS + "Editor")
    output = FreeCAD.ParamGet(PREFS + "OutputWindow")
    sweeper = QtCore.QTimer()
    sweeper.timeout.connect(sweep_boxes)
    sweeper.start(500)
    KEEP.append(sweeper)
    editor = None
    try:
        rows = param_rows("font size")
        check("the omni search lists the editors' font size", any(r.endswith("Editor/FontSize") for r in rows),
              rows[:6])
        rows = param_rows("keyword colour")
        check("the keyword colour", any(r.endswith("Editor/Keyword") for r in rows), rows[:6])
        rows = param_rows("block comment")
        check("and the block comment colour, whose key has a space",
              any("Editor/Block" in r for r in rows), rows[:6])

        editor_group.SetUnsigned("Keyword", 0x12345600)
        settle(0.2)
        path = os.path.join(OUT, "entry24_editor.FCMacro")
        with open(path, "w") as f:
            f.write("import os\n")
        FreeCADGui.open(path)
        settle(1.0)
        editor = widget_of_class("Gui::PythonEditor")
        if check("a macro opens in an editor", editor is not None):
            colour = first_word_colour(editor)
            check("the editor opened after a keyword colour was stored draws the keyword in it",
                  colour == "#123456", colour)
            editor_group.RemUnsigned("Keyword")
            settle(0.3)

            editor.setPlainText("")
            editor.setFocus()
            settle(0.2)
            for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
                QtWidgets.QApplication.sendEvent(editor, QtGui.QKeyEvent(kind, Qt.Key_Tab, Qt.NoModifier, "\t"))
            settle(0.2)
            check("Tab in a macro editor inserts four spaces while nothing says otherwise",
                  editor.toPlainText() == "    ", repr(editor.toPlainText()))
            editor.document().setModified(False)

        report = widget_of_class("Gui::DockWnd::ReportOutput")
        if check("the report view is there", report is not None):
            output.SetInt("MaxLines", 500)
            settle(0.4)
            limited = report.document().maximumBlockCount()
            output.RemInt("MaxLines")
            settle(0.4)
            check("the report view takes a line limit, and its default when the key is removed",
                  limited == 500 and report.document().maximumBlockCount() == 10000,
                  (limited, report.document().maximumBlockCount()))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        editor_group.RemUnsigned("Keyword")
        output.RemInt("MaxLines")
        try:
            if editor is not None:
                editor.document().setModified(False)
            area = FreeCADGui.getMainWindow().findChild(QtWidgets.QMdiArea)
            if area is not None:
                area.closeAllSubWindows()
            settle(0.5)
        except Exception:
            note("INFO closing the editor: " + traceback.format_exc().replace("\n", " | "))
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
