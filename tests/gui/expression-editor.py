"""The expression editor on a live desktop (docs/ExpressionEditor.md).

What the gtest binary next to the editor cannot reach, because it needs
the main window, the tool bar manager and the view placement policy all
running at once:

  - the "Expression editor" tool bar is unavailable until an editor is
    the active view, and goes again when another view is activated or the
    editor closes;
  - the editor opened on a spreadsheet's expressions does not take the
    spreadsheet's cell (the Split target's reuse step, kept by
    ViewPlacement::place's `keep`), and lands in the same view area;
  - a click in the line number margin folds a block, and the cursor
    arriving in a folded block unfolds it;
  - Apply, Diff, Revert, Refresh and Unbind, driven as the commands they
    are (Gui.runCommand), and the one question asked when blocks were
    deleted from the text.

Run it the way the tests run:

  scripts/gui-test.sh tests/gui/expression-editor.py /tmp/expr-editor --timeout 180
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ExpressionEditorGui"
TOOLBAR = "Expression editor"

state = {"done": False, "prompts": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def pump(turns=20):
    for _ in range(turns):
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def main_window():
    return FreeCADGui.getMainWindow()


def widgets_of(class_name):
    return [
        w
        for w in main_window().findChildren(QtWidgets.QWidget)
        if w.metaObject().className() == class_name
    ]


def toolbar_available():
    bar = main_window().findChild(QtWidgets.QToolBar, TOOLBAR)
    return bar is not None and bar.toggleViewAction().isVisible()


def editor_active():
    # Only an expression editor answers its commands' messages.
    return FreeCADGui.Command.get("Std_ExpressionRefresh").isActive()


def command_active(name):
    return FreeCADGui.Command.get(name).isActive()


def text_editor(view):
    return [e for e in view.findChildren(QtWidgets.QPlainTextEdit) if not e.isReadOnly()][0]


def diff_editor(view):
    return [e for e in view.findChildren(QtWidgets.QPlainTextEdit) if e.isReadOnly()][0]


def area_of(widget):
    while widget is not None:
        if widget.metaObject().className() == "Gui::ViewArea":
            return widget
        widget = widget.parentWidget()
    return None


def click(widget, x, y):
    pos = QtCore.QPointF(x, y)
    for kind in (QtCore.QEvent.MouseButtonPress, QtCore.QEvent.MouseButtonRelease):
        event = QtGui.QMouseEvent(
            kind,
            pos,
            QtCore.QPointF(widget.mapToGlobal(pos.toPoint())),
            QtCore.Qt.LeftButton,
            QtCore.Qt.LeftButton,
            QtCore.Qt.NoModifier,
        )
        QtWidgets.QApplication.sendEvent(widget, event)
    pump()


def replace(editor, old, new):
    cursor = editor.document().find(old)
    if cursor.isNull():
        return False
    cursor.insertText(new)
    pump()
    return True


def answer_prompt(label, tries=40):
    """Click the button of the modal box that is about to open."""
    box = QtWidgets.QApplication.activeModalWidget()
    if box is not None:
        for button in box.findChildren(QtWidgets.QAbstractButton):
            if button.text().replace("&", "") == label:
                state["prompts"].append(dialog_text(box))
                button.click()
                return
    if tries > 0:
        QtCore.QTimer.singleShot(50, lambda: answer_prompt(label, tries - 1))


def dialog_text(box):
    return " / ".join(label.text() for label in box.findChildren(QtWidgets.QLabel) if label.text())


def dismiss_modal():
    """A modal box nobody answers hangs the run until the driver's timeout:
    say what it asked, as a failure, and close it."""
    box = QtWidgets.QApplication.activeModalWidget()
    if box is not None:
        note("FAIL unexpected modal dialog | " + dialog_text(box))
        box.reject()


def bindings(obj):
    return dict(obj.ExpressionEngine)


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def run():
    try:
        steps()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def steps():
    doc = FreeCAD.newDocument(DOC)
    a = doc.addObject("App::FeatureTest", "A")
    b = doc.addObject("App::FeatureTest", "B")
    a.setExpression("Integer", "B.Integer + 1")
    b.setExpression("Float", "2.5")
    sheet = doc.addObject("Spreadsheet::Sheet", "Sheet")
    sheet.set("A1", "=B.Integer * 3")
    doc.recompute()
    pump(50)

    check("tool bar is unavailable before any editor", not toolbar_available())

    # --- Placement: the spreadsheet's cell is not reused -----------------
    FreeCADGui.ActiveDocument.setEdit(sheet)
    pump(50)
    sheets = widgets_of("SpreadsheetGui::SheetView")
    if not check("spreadsheet view open", len(sheets) == 1, len(sheets)):
        return
    sheet_area = area_of(sheets[0])

    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(sheet)
    pump()
    FreeCADGui.runCommand("Std_ExpressionEditSelected")
    pump(50)
    editors = widgets_of("Gui::ExpressionEditorView")
    check("editor opened on the selection", len(editors) == 1, len(editors))
    check(
        "the spreadsheet view survives the editor opening",
        len(widgets_of("SpreadsheetGui::SheetView")) == 1,
    )
    if sheet_area is not None:
        check(
            "the editor splits the spreadsheet's area",
            area_of(editors[0]) is sheet_area,
        )
    check("editor holds the sheet's binding", "#Sheet." in text_editor(editors[0]).toPlainText())
    check("tool bar is available while the editor is active", toolbar_available())

    FreeCADGui.runCommand("Std_ExpressionEditSelected")
    pump(20)
    check("the same scope reveals the open editor", len(widgets_of("Gui::ExpressionEditorView")) == 1)

    FreeCADGui.activateView("Gui::View3DInventor", False)
    pump(30)
    check("tool bar goes when a 3D view is activated", not toolbar_available())

    FreeCADGui.Selection.clearSelection()
    FreeCADGui.runCommand("Std_ExpressionEditDocument")
    pump(50)
    views = [v for v in widgets_of("Gui::ExpressionEditorView") if "#A." in text_editor(v).toPlainText()]
    if not check("editor opened on the document", len(views) == 1, len(views)):
        return
    view = views[0]
    editor = text_editor(view)
    check("tool bar is back with the editor", toolbar_available() and editor_active())
    check("nothing to apply yet", not command_active("Std_ExpressionApply"))

    # --- Folding -------------------------------------------------------
    document = editor.document()
    markers = [
        w for w in editor.findChildren(QtWidgets.QWidget) if w.metaObject().className() == "Gui::LineMarker"
    ]
    if check("line number margin found", len(markers) == 1, len(markers)):
        editor.setTextCursor(QtGui.QTextCursor(document.findBlockByNumber(0)))
        y = editor.cursorRect(QtGui.QTextCursor(document.findBlockByNumber(0))).center().y()
        click(markers[0], 4, y)
        check("a margin click folds the block", not document.findBlockByNumber(1).isVisible())
        check("the header line stays", document.findBlockByNumber(0).isVisible())
        editor.setTextCursor(QtGui.QTextCursor(document.findBlockByNumber(2)))
        pump()
        check("the cursor in a folded block unfolds it", document.findBlockByNumber(1).isVisible())
        click(markers[0], 4, y)
        click(markers[0], 4, y)
        check("a second click unfolds", document.findBlockByNumber(1).isVisible())

    # --- Apply ---------------------------------------------------------
    check("edit found", replace(editor, "B.Integer + 1", "B.Integer + 5"))
    check("an edit enables Apply", command_active("Std_ExpressionApply"))
    FreeCADGui.runCommand("Std_ExpressionApply")
    pump(30)
    check("Apply rebinds", "B.Integer + 5" in bindings(a).get("Integer", ""), bindings(a))
    check("Apply reloads: nothing left to apply", not command_active("Std_ExpressionApply"))

    # --- Diff and Revert -------------------------------------------------
    replace(editor, "2.5", "3.5")
    FreeCADGui.runCommand("Std_ExpressionDiff")
    pump(20)
    diff = diff_editor(view)
    check("Diff shows the diff view", diff.isVisible() and not editor.isVisible())
    text = diff.toPlainText()
    check("the diff has both sides", "2.5" in text and "3.5" in text, text)
    colored = [
        blk.blockFormat().background().color().name()
        for blk in (diff.document().findBlockByNumber(i) for i in range(diff.document().blockCount()))
        if "2.5" in blk.text() or "3.5" in blk.text()
    ]
    check("changed lines are colored", len(colored) == 2 and all(c != "#000000" for c in colored), colored)
    FreeCADGui.runCommand("Std_ExpressionDiff")
    pump()
    check("Diff toggles back", editor.isVisible())
    FreeCADGui.runCommand("Std_ExpressionRevert")
    pump()
    text = editor.toPlainText()
    check("Revert restores the loaded text", "2.5" in text and "3.5" not in text)
    check("Revert leaves nothing to apply", not command_active("Std_ExpressionApply"))
    check("B untouched by the reverted edit", bindings(b).get("Float") == "2.5", bindings(b))

    # --- Deleted block: one question -------------------------------------
    start = document.find("#B.")
    begin = document.findBlock(start.position())
    end = begin.next().next()
    while end.isValid() and not end.text().startswith("##@@ "):
        end = end.next()
    cursor = QtGui.QTextCursor(begin)
    cursor.setPosition(end.position() if end.isValid() else document.characterCount() - 1, QtGui.QTextCursor.KeepAnchor)
    cursor.removeSelectedText()
    pump()
    check("B's block deleted", "#B." not in editor.toPlainText())
    QtCore.QTimer.singleShot(100, lambda: answer_prompt("Unbind"))
    FreeCADGui.runCommand("Std_ExpressionApply")
    pump(30)
    check("one question for the deleted block", len(state["prompts"]) == 1, state["prompts"])
    check("Unbind unbinds the deleted block", "Float" not in bindings(b), bindings(b))

    # --- Unbind command --------------------------------------------------
    editor.setTextCursor(document.find("#A."))
    pump()
    check("Unbind is enabled", command_active("Std_ExpressionUnbind"))
    FreeCADGui.runCommand("Std_ExpressionUnbind")
    pump()
    lines = editor.toPlainText().split("\n")
    header = [i for i, line in enumerate(lines) if "#A." in line][0]
    check("Unbind turns the body into '#'", lines[header + 2] == "#", lines[header:header + 4])
    check("Unbind only edits the text", "Integer" in bindings(a))
    FreeCADGui.runCommand("Std_ExpressionApply")
    pump(30)
    check("Apply of an unbinding block unbinds", "Integer" not in bindings(a), bindings(a))

    # --- Refresh ---------------------------------------------------------
    a.setExpression("Integer", "7")
    pump()
    check("a change elsewhere is not in the text yet", "#A." not in editor.toPlainText())
    FreeCADGui.runCommand("Std_ExpressionRefresh")
    pump()
    check("Refresh reloads from the document", "#A." in editor.toPlainText())

    # --- Closing -----------------------------------------------------------
    check("the editor is the active view before closing", editor_active())
    check("nothing unapplied before closing", not command_active("Std_ExpressionApply"))
    watchdog = QtCore.QTimer()
    watchdog.timeout.connect(dismiss_modal)
    watchdog.start(500)
    # In a view area, close the editor's cell: Std_CloseActiveWindow closes
    # the whole tab, the document's 3D view with it, and that asks to save.
    if area_of(view) is not None:
        FreeCADGui.runCommand("Std_ViewSplitClose")
    else:
        FreeCADGui.runCommand("Std_CloseActiveWindow")
    pump(50)
    watchdog.stop()
    check("editor closed", view not in widgets_of("Gui::ExpressionEditorView"))
    check("tool bar goes with the last active editor", not toolbar_available() or editor_active())


QtCore.QTimer.singleShot(1500, run)
