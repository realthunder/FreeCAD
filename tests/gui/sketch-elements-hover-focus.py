"""Hovering the Elements list does not take the keyboard from a text being typed.

The list takes the keyboard focus when the pointer enters a row, so that its
keys work without a click. It took it from anything: a constraint being
renamed in the list above it lost its editor the moment the pointer crossed
an element's row (upstream issue 11842, fixed there by `1d4a09366c`), and so
would the value editor at a dimension's label.

The list leaves the focus alone while a text field has it.

Claims:

  - with nothing being typed, a row under the pointer gives the list the
    focus, as before;
  - with a line edit holding the focus, the same hover leaves it there;
  - the same for a spin box.

Driven twice: by the signal a hover raises (`itemEntered`), and by a mouse
move event sent to the row (QTest's mouseMove does not raise the signal).

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
DOC = "SketchElementsHoverFocus"
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


def focus_name():
    widget = QtWidgets.QApplication.focusWidget()
    if widget is None:
        return "nothing"
    return "%s(%s)" % (widget.metaObject().className(), widget.objectName())


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
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(10, 10, 0), V(30, 10, 0)), False)
        sk.addGeometry(Part.LineSegment(V(10, 20, 0), V(30, 20, 0)), False)
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        settle(20)

        tree = [w for w in mw.findChildren(QtWidgets.QTreeWidget)
                if w.objectName() == "elementsWidget"][0]
        first, second = tree.topLevelItem(0), tree.topLevelItem(1)

        # A text field of the same window, off the list: what a rename or a
        # value being typed is to the list.
        edit = QtWidgets.QLineEdit()
        edit.setObjectName("probeEdit")
        mw.statusBar().addWidget(edit)
        settle(5)
        entered = []
        tree.itemEntered.connect(lambda item, column: entered.append(item))

        mw.setFocus()
        settle(5)
        tree.itemEntered.emit(first, 0)
        settle(5)
        check("with nothing being typed, a hovered row gives the list the focus",
              QtWidgets.QApplication.focusWidget() is tree, focus_name())

        edit.setFocus()
        settle(5)
        if not check("the line edit can hold the focus on this display",
                     QtWidgets.QApplication.focusWidget() is edit, focus_name()):
            raise RuntimeError("no keyboard focus to test with")
        tree.itemEntered.emit(second, 0)
        settle(5)
        check("a hover (signal) leaves the focus in the line edit",
              QtWidgets.QApplication.focusWidget() is edit, focus_name())

        edit.setFocus()
        settle(5)
        del entered[:]
        hover(tree, row_center(tree, first))
        if not check("a sent move is a hover to the list", entered == [first], len(entered)):
            raise RuntimeError("the instrument raises no hover")
        check("a pointer moved over a row leaves it there too",
              QtWidgets.QApplication.focusWidget() is edit, focus_name())

        # a spin box is a text being typed as well (the focus is the box's,
        # not its line edit's)
        spin = QtWidgets.QDoubleSpinBox()
        spin.setObjectName("probeSpin")
        mw.statusBar().addWidget(spin)
        settle(5)
        spin.setFocus()
        settle(5)
        hover(tree, row_center(tree, second))
        check("nor does a hover take the focus from a spin box",
              QtWidgets.QApplication.focusWidget() is spin, focus_name())

        mw.statusBar().removeWidget(spin)
        spin.deleteLater()
        mw.statusBar().removeWidget(edit)
        edit.deleteLater()
        settle(5)
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
