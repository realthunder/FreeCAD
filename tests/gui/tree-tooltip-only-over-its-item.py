"""A tree item's tooltip comes only with the pointer still over that item.

docs/HandsOnQueue.md entry 84: "tree view item tooltip has a delay, which is
acceptable, but when the time is up, only show the tooltip if the mouse is
still over the item."

The delay (300 ms) was started by a move of the pointer over an item, and
when it was up the tree asked which item is at the pointer -- a question
that still names an item for a point BESIDE the view, where a column is
wider than the view, and under a widget lying over the tree; and the tree's
leaving stopped nothing. So the tip of an item came up, and stayed, with
the pointer in the 3D view.

Run with QT_QPA_PLATFORM=offscreen: the pointer moved here is that
platform's own. On a real screen it would be the user's, and the script
does nothing there.

Claims:
  - resting on an item brings its tip, with its label and internal name;
  - the pointer gone to beside the tree before the delay is up, at the
    item's height, where the tree still answers with that item: no tip;
  - a widget come to lie over the item before the delay is up, the pointer
    not moved: no tip;
  - gone to the tree's empty part: no tip;
  - gone to another item and resting there: that item's tip, not the first's;
  - a document's row, rested on: the file's name (its delay is its own now);
  - and resting on the first item again still brings its tip.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
QTest = QtTest.QTest
DOC = "TipTest"
LONG = "Housing with a label long enough to make the column wider than the tree view is, twice over"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=150):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def mw():
    return FreeCADGui.getMainWindow()


def tree_view():
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if w.metaObject().className() == "Gui::TreeWidget" and w.isVisible():
                return w
        except RuntimeError:
            pass
    return None


def rows(view):
    out = []
    index = view.indexAt(QtCore.QPoint(4, 4))
    while index.isValid():
        out.append(index)
        index = view.indexBelow(index)
    return out


def spot(tree, label):
    """A point of the row with this label, in the tree's area, well inside it"""
    for index in rows(tree):
        text = index.data(QtCore.Qt.DisplayRole) or ""
        if text.startswith(label):
            rect = tree.visualRect(index)
            return QtCore.QPoint(min(rect.left() + 40, tree.viewport().width() - 30),
                                 rect.center().y())
    return None


def tips():
    """The texts of the tips that are up. Not a command's: on this platform
    the pointer starts in the window's corner, over a tool button, and that
    button's tip -- a table, which no tree item's is -- stays up through the
    run."""
    out = []
    for w in QtWidgets.QApplication.topLevelWidgets():
        try:
            if not w.isVisible() or w.windowType() != QtCore.Qt.ToolTip:
                continue
            parts = [label.text() for label in w.findChildren(QtWidgets.QLabel)]
            if isinstance(w, QtWidgets.QLabel):
                parts.append(w.text())
            text = " ".join(p for p in parts if p)
            if text and "<table>" not in text:
                out.append(text)
        except RuntimeError:
            pass
    return out


def point_to(area, pos):
    """The pointer to a point of a widget: the platform's pointer, and the move told"""
    QtGui.QCursor.setPos(area.mapToGlobal(pos))
    settle(30)
    QTest.mouseMove(area, pos)
    settle(30)


def away():
    """The pointer into the middle of the main window, and any tip down"""
    centre = mw().rect().center()
    QtGui.QCursor.setPos(mw().mapToGlobal(centre))
    settle(700)
    if tips():
        QTest.keyClick(mw(), QtCore.Qt.Key_Escape)
        settle(300)
    return not tips()


def run():
    tree = tree_view()
    if not check("the tree view is shown", tree is not None):
        return
    area = tree.viewport()
    first = spot(tree, "Bracket")
    second = spot(tree, "Cover")
    if not check("the rows are found", first is not None and second is not None):
        return

    point_to(area, first)
    settle(800)
    up = tips()
    check("resting on an item brings its tip, label and internal name",
          len(up) == 1 and "Bracket" in up[0] and "Box" in up[0], up)
    check("the tip goes with the pointer", away(), tips())

    beside = QtCore.QPoint(area.width() + 60, first.y())
    still = tree.itemAt(beside)
    check("beside the view the tree still answers with the item (a column wider than the view)",
          still is not None and still.text(0).startswith("Bracket"),
          still.text(0) if still is not None else None)
    point_to(area, first)
    settle(100)
    QtGui.QCursor.setPos(area.mapToGlobal(beside))
    settle(900)
    check("gone to beside the tree before the delay is up: no tip", not tips(), tips())
    away()

    point_to(area, first)
    settle(100)
    cover = QtWidgets.QFrame(mw())
    cover.setStyleSheet("background: #406080;")
    corner = mw().mapFromGlobal(area.mapToGlobal(first)) - QtCore.QPoint(40, 20)
    cover.setGeometry(corner.x(), corner.y(), 80, 40)
    cover.show()
    cover.raise_()
    settle(900)
    under = QtWidgets.QApplication.widgetAt(QtGui.QCursor.pos())
    check("a widget lies over the item, the pointer where it was", under is cover,
          under.metaObject().className() if under is not None else None)
    check("a widget come to lie over the item before the delay is up: no tip", not tips(), tips())
    cover.hide()
    cover.deleteLater()
    away()

    shown = rows(tree)
    below = QtCore.QPoint(first.x(), tree.visualRect(shown[-1]).bottom() + 40)
    check("the tree has an empty part below its rows",
          below.y() < area.height() - 5 and tree.itemAt(below) is None, below)
    point_to(area, first)
    settle(100)
    point_to(area, below)
    settle(900)
    check("gone to the tree's empty part: no tip", not tips(), tips())
    away()

    point_to(area, first)
    settle(100)
    point_to(area, second)
    settle(800)
    up = tips()
    check("gone to another item and resting there: that item's tip",
          len(up) == 1 and "Cover" in up[0] and "Bracket" not in up[0], up)
    away()

    document = spot(tree, DOC)
    if check("the document's row is found", document is not None):
        point_to(area, document)
        settle(800)
        up = tips()
        check("a document's row, rested on: the file's name",
              len(up) == 1 and "TipTest.FCStd" in up[0], up)
        away()

    point_to(area, first)
    settle(800)
    up = tips()
    check("and resting on the first item again brings its tip",
          len(up) == 1 and "Bracket" in up[0], up)
    away()


def start():
    platform = QtGui.QGuiApplication.platformName()
    if platform != "offscreen":
        note("NOT RUN: the platform is %r; this script moves the pointer and runs only with "
             "QT_QPA_PLATFORM=offscreen" % platform)
        finish()
        return
    try:
        doc = FreeCAD.newDocument(DOC)
        for name, label in (("Box", "Bracket"), ("Box001", "Cover"), ("Box002", LONG)):
            box = doc.addObject("Part::Box", name)
            box.Label = label
        doc.recompute()
        doc.saveAs(os.path.join(OUT, "TipTest.FCStd"))
        settle(800)
        FreeCADGui.Selection.clearSelection()
        settle(300)
        run()
    except Exception:
        note("ABORT: " + traceback.format_exc().replace("\n", " | "))
    finish()


def finish():
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


QtCore.QTimer.singleShot(2500, start)
