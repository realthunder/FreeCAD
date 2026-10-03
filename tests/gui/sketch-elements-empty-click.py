"""A press in the Elements list acts on the row under it, or on none.

The list kept the row the pointer last ENTERED and, when its selection
changed, toggled that row -- whichever row, or none, the press was on. A
mouse moves before it presses, so on the desktop the two were the same row
and nothing showed. They are not the same row for a press that comes without
a move (a tap, a click forwarded from a browser), and not for a press on the
empty part of the list:

  - a press on a row never hovered selected nothing;
  - a press on a row, the hover still on another, deselected the other and
    selected nothing;
  - a press below the last row, the hover on an unselected row, SELECTED
    that row -- where upstream's list clears the selection (`98d64f9939`).

The row is taken from the press now.

Claims, with two lines in the list:

  - a press on a row with no hover at all selects its element;
  - a press on the second row, the hover still on the first, selects the
    second;
  - a press below the last row clears the selection: with the first line
    selected, and with the pointer having rested on the other row before;
  - with nothing selected, it selects nothing.

The hover is a mouse move event sent to the list; QTest's mouseMove does not
raise the list's itemEntered.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchElementsEmptyClick"
OBJ = "Sketch"
V = FreeCAD.Vector


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=10):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def selected():
    names = []
    for entry in FreeCADGui.Selection.getSelectionEx("*"):
        if entry.ObjectName == OBJ:
            names += list(entry.SubElementNames)
    return sorted(names)


def row_center(tree, item):
    return tree.visualRect(tree.indexFromItem(item, 1)).center()


def empty_point(tree):
    """A point of the viewport below the last row."""
    last = tree.topLevelItem(tree.topLevelItemCount() - 1)
    rect = tree.visualRect(tree.indexFromItem(last, 1))
    return QtCore.QPoint(rect.center().x(), rect.bottom() + 3 * rect.height())


def hover(tree, point):
    """Rest the pointer at a point of the list: the event a real move sends."""
    viewport = tree.viewport()
    event = QtGui.QMouseEvent(QtCore.QEvent.MouseMove, QtCore.QPointF(point),
                              QtCore.QPointF(viewport.mapToGlobal(point)),
                              QtCore.Qt.NoButton, QtCore.Qt.NoButton, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(viewport, event)
    settle(5)


def press(tree, point):
    """Press and release at a point, with no move before it."""
    QtTest.QTest.mouseClick(tree.viewport(), QtCore.Qt.LeftButton,
                            QtCore.Qt.NoModifier, point)
    settle(10)


def run():
    try:
        import Part
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw = FreeCADGui.getMainWindow()
        mw.showMaximized()
        mw.activateWindow()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry(Part.LineSegment(V(10, 10, 0), V(30, 10, 0)), False)
        sk.addGeometry(Part.LineSegment(V(10, 20, 0), V(30, 20, 0)), False)
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        settle(20)

        tree = [w for w in mw.findChildren(QtWidgets.QTreeWidget)
                if w.objectName() == "elementsWidget"][0]
        first, second = tree.topLevelItem(0), tree.topLevelItem(1)
        point = empty_point(tree)
        if not check("there is room below the last row to click in",
                     tree.itemAt(point) is None
                     and tree.viewport().rect().contains(point),
                     (point.x(), point.y(), tree.viewport().height())):
            raise RuntimeError("no empty area to click")

        entered = []
        tree.itemEntered.connect(lambda item, column: entered.append(item))

        press(tree, row_center(tree, first))
        check("a press on a row never hovered selects its element",
              selected() == ["Edge1"], selected())
        check("(and no hover was raised by the press)", entered == [], len(entered))

        hover(tree, row_center(tree, first))
        if not check("a sent move is a hover to the list", entered == [first], len(entered)):
            raise RuntimeError("the instrument raises no hover")
        press(tree, row_center(tree, second))
        check("a press on the second row, the hover still on the first, selects the second",
              selected() == ["Edge2"], selected())

        FreeCADGui.Selection.clearSelection()
        settle(5)
        FreeCADGui.Selection.addSelection(DOC, OBJ, "Edge1")
        settle(5)
        check("the first line is selected again", selected() == ["Edge1"], selected())
        press(tree, point)
        check("a press below the last row clears the selection", selected() == [], selected())

        FreeCADGui.Selection.addSelection(DOC, OBJ, "Edge1")
        settle(5)
        hover(tree, row_center(tree, second))
        press(tree, point)
        check("after the pointer rested on the other row, it still selects nothing",
              selected() == [], selected())

        press(tree, point)
        check("with nothing selected, the press selects nothing", selected() == [], selected())

        hover(tree, row_center(tree, first))
        press(tree, row_center(tree, first))
        check("a hovered row still selects its element when pressed",
              selected() == ["Edge1"], selected())

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.ActiveDocument.resetEdit()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
