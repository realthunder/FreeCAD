"""Ctrl+A in a focused spreadsheet selects its cells.

Ctrl+A is Std_SelectAll's window shortcut (it selects a sketch edit's
elements, or the document's objects). A focused widget keeps Ctrl+A only
by claiming it in ShortcutOverride. Text and line edits do; the
spreadsheet's table did not, so with the shortcut bound Ctrl+A in a sheet
selected the document's objects instead of the cells -- upstream #27151,
fixed by upstream 4f4e9244e6, taken with the shortcut.

Driven with a QTest key click, which goes through ShortcutOverride and the
window shortcut the way a real key does.

Checks, on a sheet with values in A1..C3:

  - Ctrl+A with the table focused selects every cell of the table;
  - and selects no document object.

Scored against the tree with the shortcut bound but without the table's
claim: no cells are selected and the sheet and its document's objects are.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))


def settle(n=10):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def run():
    try:
        import Spreadsheet  # noqa: F401
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw = FreeCADGui.getMainWindow()
        mw.showMaximized()
        mw.activateWindow()
        doc = FreeCAD.newDocument("SheetSelectAll")
        doc.addObject("Part::Box", "Box")
        sheet = doc.addObject("Spreadsheet::Sheet", "Sheet")
        for col in "ABC":
            for row in (1, 2, 3):
                sheet.set("%s%d" % (col, row), "1")
        doc.recompute()
        sheet.ViewObject.doubleClicked()
        settle(20)
        table = None
        for w in mw.findChildren(QtWidgets.QTableView):
            if w.isVisible() and w.metaObject().className().endswith("SheetTableView"):
                table = w
        if not table:
            note("FAIL no spreadsheet table view")
            finish()
            return
        FreeCADGui.Selection.clearSelection()
        table.setFocus(QtCore.Qt.OtherFocusReason)
        settle()
        QtTest.QTest.keyClick(table, QtCore.Qt.Key_A, QtCore.Qt.ControlModifier)
        settle()
        # The corners: only a select-all covers both (the sheet has
        # 16384 x 702 cells, too many to list).
        model = table.model()
        sm = table.selectionModel()
        first = sm.isSelected(model.index(0, 0))
        last = sm.isSelected(model.index(model.rowCount() - 1, model.columnCount() - 1))
        objs = sorted(o.Name for o in FreeCADGui.Selection.getSelection("*"))
        note("table %dx%d, first cell %s, last cell %s, objects %s"
             % (model.rowCount(), model.columnCount(), first, last, objs))
        check("Ctrl+A in a focused sheet selects every cell", first and last,
              "first %s, last %s" % (first, last))
        check("and no document object", objs == [], objs)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
