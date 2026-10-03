"""The Elements list selects what its own selection says.

When the list's selection changed, the panel toggled one remembered row --
the one last pressed or entered -- and, without Ctrl or Shift, deselected
the others. It never read which rows the list had selected. For a press on
an unselected row the two agree. They do not for anything else:

  - an arrow key selected nothing: the list reports that change twice, and
    the remembered row was toggled off and on again;
  - Shift+arrow DESELECTED the remembered row instead of extending to the
    next one;
  - End did nothing;
  - with two rows selected, a press on one of them deselected both, where
    any list keeps the pressed row.

The rows' flags are read from the list's selection now.

Claims, with four lines in the list:

  - Down, Up and End select the row they move to, alone (Home is the
    application's shortcut for the home view and never reaches the list);
  - Shift+Down and Shift+Up extend and shrink the selection;
  - an arrow key in a list that was only hovered, never pressed, selects
    the row it moves to;
  - with two rows selected, a press on one of them leaves that one selected;
  - a press on the only selected row keeps it;
  - Ctrl+press adds a row and takes it away again, Shift+press selects the
    rows in between;
  - a point picked in the 3D view stays selected through a Ctrl+press and a
    Shift+arrow in the list, and goes with a plain press or a plain arrow.

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
DOC = "SketchElementsKeys"
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


def hover(tree, point):
    """Rest the pointer at a point of the list: the event a real move sends."""
    viewport = tree.viewport()
    event = QtGui.QMouseEvent(QtCore.QEvent.MouseMove, QtCore.QPointF(point),
                              QtCore.QPointF(viewport.mapToGlobal(point)),
                              QtCore.Qt.NoButton, QtCore.Qt.NoButton, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(viewport, event)
    settle(5)


def press(tree, point, modifier=QtCore.Qt.NoModifier):
    """Press and release at a point, with no move before it."""
    QtTest.QTest.mouseClick(tree.viewport(), QtCore.Qt.LeftButton, modifier, point)
    settle(10)


def key(tree, code, modifier=QtCore.Qt.NoModifier):
    QtTest.QTest.keyClick(tree, code, modifier)
    settle(10)


def clear():
    FreeCADGui.Selection.clearSelection()
    settle(5)


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
        for y in (10, 20, 30, 40):
            sk.addGeometry(Part.LineSegment(V(10, y, 0), V(30, y, 0)), False)
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        settle(20)

        tree = [w for w in mw.findChildren(QtWidgets.QTreeWidget)
                if w.objectName() == "elementsWidget"][0]
        row = [row_center(tree, tree.topLevelItem(i)) for i in range(4)]
        Shift, Ctrl = QtCore.Qt.ShiftModifier, QtCore.Qt.ControlModifier

        def rows():
            return [i for i in range(4) if tree.topLevelItem(i).isSelected()]

        def both(name, edges):
            """The scene's selection, and the list showing the same rows."""
            want = ["Edge%d" % (i + 1) for i in edges]
            check(name, selected() == want and rows() == edges, (selected(), rows()))

        press(tree, row[1])
        both("a press selects its row", [1])
        key(tree, QtCore.Qt.Key_Down)
        both("Down selects the next row, alone", [2])
        key(tree, QtCore.Qt.Key_Up)
        both("Up selects the row before, alone", [1])
        key(tree, QtCore.Qt.Key_End)
        both("End selects the last row", [3])

        press(tree, row[0])
        key(tree, QtCore.Qt.Key_Down, Shift)
        both("Shift+Down extends the selection by a row", [0, 1])
        key(tree, QtCore.Qt.Key_Down, Shift)
        both("and by another", [0, 1, 2])
        key(tree, QtCore.Qt.Key_Up, Shift)
        both("Shift+Up takes the last one back", [0, 1])

        clear()
        both("(the selection is cleared from outside)", [])
        hover(tree, row[0])
        check("a hover selects nothing", selected() == [] and tree.hasFocus(), selected())
        key(tree, QtCore.Qt.Key_Down)
        check("an arrow key in a list only hovered selects one row",
              len(selected()) == 1 and len(rows()) == 1
              and selected() == ["Edge%d" % (rows()[0] + 1)], (selected(), rows()))

        clear()
        press(tree, row[0])
        press(tree, row[2], Ctrl)
        both("Ctrl+press adds a row", [0, 2])
        press(tree, row[0])
        both("a press on one of two selected rows leaves that one", [0])
        press(tree, row[0])
        both("a press on the only selected row keeps it", [0])
        press(tree, row[2], Ctrl)
        press(tree, row[2], Ctrl)
        both("Ctrl+press on a selected row takes it away", [0])
        clear()
        press(tree, row[0])
        press(tree, row[3], Shift)
        both("Shift+press selects the rows in between", [0, 1, 2, 3])

        # Vertex1 is the start point of the first line: a part of its row that
        # the row's highlight does not stand for.
        clear()
        FreeCADGui.Selection.addSelection(DOC, OBJ, "Vertex1")
        settle(5)
        press(tree, row[2], Ctrl)
        check("a point picked in the view stays through a Ctrl+press",
              selected() == ["Edge3", "Vertex1"], selected())
        key(tree, QtCore.Qt.Key_Down, Shift)
        check("and through a Shift+arrow",
              selected() == ["Edge3", "Edge4", "Vertex1"], selected())
        press(tree, row[1])
        check("a plain press replaces it", selected() == ["Edge2"], selected())
        clear()
        FreeCADGui.Selection.addSelection(DOC, OBJ, "Vertex1")
        settle(5)
        press(tree, row[2], Ctrl)
        key(tree, QtCore.Qt.Key_Down)
        check("and so does a plain arrow", selected() == ["Edge4"], selected())

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
