"""An element row's single icon is a button that picks one of its parts.

The fork's elements list keeps one icon per row (upstream's list has one per
part), by user ruling. The global Type combo that decided which part a click
took -- edge, start, end or centre point -- is retired, with the Z key that
cycled it and "Auto-switch to Edge". Instead each row's icon is a button,
drawn with a grouped tool button's small arrow: clicking it drops down the
parts that element has, with upstream's per-part icons, and picks one. Ctrl
adds to the selection. The icon, and the Name column beside it, show the
part of that element selected last, whichever way it was selected; with
none selected, its edge (a point: its vertex). A click elsewhere on the row
still selects the whole element.

Driven with real clicks on the icon (its rectangle from the same style call
the delegate paints with); the pop-up is answered by setting its action and
pressing Return. The active part is read from the Name column.

Sketch: a line (Vertex1, Vertex2), a circle (Vertex3), an arc (Vertex4 start,
Vertex5 end, Vertex6 centre) and a point (Vertex7).

The icon's size is a preference, ElementIconSize (Mod/Sketcher/Elements,
on the Display page), 32 px by default; the drop-down arrow has a strip of
its own on the icon's left, so the button is a rectangle.

Checks:

  - the icons are 32 px by default, with the arrow strip beside them;
  - the Type combo and the auto-switch box are gone;
  - hovering the icon explains it;
  - the line's menu offers Edge, Start point and End point; the circle's
    Edge and Centre point; the point's only Point;
  - picking Start point selects exactly Vertex1, and the line's row then
    names Vertex1;
  - Ctrl-picking End point adds Vertex2, and the row names Vertex2;
  - picking Edge alone selects Edge1;
  - a part selected from the 3D view is followed too: the arc's centre
    makes its row name Vertex6;
  - clearing the selection puts every row back on its default part.

Scored against the tree before the change: 11 of the 14 part checks fail --
the Type combo is there, there is no tooltip and no part menu, and a part
selected in the 3D view leaves the row on Edge3. The three that pass hold
either way: nothing is selected, and the rows sit on their defaults. The
two size checks fail before the icon-size preference: the list had no icon
size of its own (the style default, reported as -1x-1) and no strip.
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
state = {"done": False, "menu": None}
COL_NAME = 1


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))
    return cond


def settle(n=10):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def selected_names():
    names = []
    for e in FreeCADGui.Selection.getSelectionEx("*"):
        if e.ObjectName == "Sketch":
            names += list(e.SubElementNames)
    return sorted(names)


def icon_center(tree, item):
    """Centre of the row's icon, in viewport coordinates."""
    index = tree.indexFromItem(item, 0)
    opt = QtWidgets.QStyleOptionViewItem()
    opt.initFrom(tree)
    opt.rect = tree.visualRect(index)
    opt.features = (QtWidgets.QStyleOptionViewItem.HasDecoration
                    | QtWidgets.QStyleOptionViewItem.HasDisplay
                    | QtWidgets.QStyleOptionViewItem.HasCheckIndicator)
    opt.icon = item.icon(0)
    opt.text = item.text(0)
    size = tree.iconSize()
    if not size.isValid():
        m = tree.style().pixelMetric(QtWidgets.QStyle.PM_SmallIconSize, None, tree)
        size = QtCore.QSize(m, m)
    opt.decorationSize = size
    opt.checkState = item.checkState(0)
    rect = tree.style().subElementRect(QtWidgets.QStyle.SE_ItemViewItemDecoration, opt, tree)
    return rect.center()


def open_menu(tree, item, pick=None, ctrl=False):
    """Click the row's icon; return the menu's entries, picking `pick`."""
    seen = {}

    def answer():
        menu = QtWidgets.QApplication.activePopupWidget()
        if not isinstance(menu, QtWidgets.QMenu):
            seen["entries"] = None
            return
        seen["entries"] = [a.text() for a in menu.actions()]
        target = [a for a in menu.actions() if a.text() == pick] if pick else []
        if target:
            menu.setActiveAction(target[0])
            QtTest.QTest.keyClick(menu, QtCore.Qt.Key_Return,
                                  QtCore.Qt.ControlModifier if ctrl else QtCore.Qt.NoModifier)
        else:
            menu.close()

    QtCore.QTimer.singleShot(300, answer)
    mods = QtCore.Qt.ControlModifier if ctrl else QtCore.Qt.NoModifier
    QtTest.QTest.mouseClick(tree.viewport(), QtCore.Qt.LeftButton, mods,
                            icon_center(tree, item))
    settle(15)
    return seen.get("entries")


def run():
    try:
        import Part
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw = FreeCADGui.getMainWindow()
        mw.showMaximized()
        mw.activateWindow()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("ElementsIcon")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(10, 10, 0), V(30, 10, 0)), False)
        sk.addGeometry(Part.Circle(V(50, 20, 0), V(0, 0, 1), 5), False)
        sk.addGeometry(Part.ArcOfCircle(Part.Circle(V(70, 20, 0), V(0, 0, 1), 5),
                                        0.0, 2.0), False)
        sk.addGeometry(Part.Point(V(90, 20, 0)), False)
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        settle(20)

        names = [w.objectName() for w in mw.findChildren(QtWidgets.QWidget)]
        check("the Type combo and the auto-switch box are gone",
              "comboBoxElementFilter" not in names and "autoSwitchBox" not in names)

        tree = [w for w in mw.findChildren(QtWidgets.QTreeWidget)
                if w.objectName() == "elementsWidget"][0]
        line, circle, arc, point = (tree.topLevelItem(i) for i in range(4))
        size = tree.iconSize()
        check("the icons are 32 px, with the arrow strip beside them",
              size.height() == 32 and size.width() > 32, "%dx%d" % (size.width(), size.height()))

        # the tooltip
        pos = icon_center(tree, line)
        QtWidgets.QApplication.sendEvent(
            tree.viewport(),
            QtGui.QHelpEvent(QtCore.QEvent.ToolTip, pos, tree.viewport().mapToGlobal(pos)))
        settle(5)
        tip = QtWidgets.QToolTip.text()
        check("hovering the icon explains it", "part of this element" in tip, tip[:60])
        QtWidgets.QToolTip.hideText()

        entries = open_menu(tree, line)
        check("the line's menu offers its edge and both ends",
              entries == ["Edge", "Start point", "End point"], entries)
        check("and opening it selects nothing", selected_names() == [], selected_names())
        entries = open_menu(tree, circle)
        check("the circle's menu offers its edge and centre",
              entries == ["Edge", "Centre point"], entries)
        entries = open_menu(tree, point)
        check("a point's menu offers only the point", entries == ["Point"], entries)

        open_menu(tree, line, "Start point")
        check("picking Start point selects the line's start",
              selected_names() == ["Vertex1"], selected_names())
        check("and the row shows it", line.text(COL_NAME) == "Vertex1", line.text(COL_NAME))

        open_menu(tree, line, "End point", ctrl=True)
        check("Ctrl-picking End point adds it",
              selected_names() == ["Vertex1", "Vertex2"], selected_names())
        check("and the row shows the part picked last",
              line.text(COL_NAME) == "Vertex2", line.text(COL_NAME))

        open_menu(tree, line, "Edge")
        check("picking Edge alone selects the edge", selected_names() == ["Edge1"],
              selected_names())
        check("and the row shows the edge", line.text(COL_NAME) == "Edge1",
              line.text(COL_NAME))

        FreeCADGui.Selection.clearSelection()
        settle()
        FreeCADGui.Selection.addSelection(sk, "Vertex6")
        settle()
        check("a part selected in the 3D view is followed",
              arc.text(COL_NAME) == "Vertex6", arc.text(COL_NAME))

        FreeCADGui.Selection.clearSelection()
        settle()
        rows = [tree.topLevelItem(i).text(COL_NAME) for i in range(4)]
        check("clearing the selection puts every row on its default part",
              rows == ["Edge1", "Edge2", "Edge3", "Vertex7"], rows)

        # the size is a preference, applied from the next edit
        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle(10)
        prefs = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/Elements")
        prefs.SetInt("ElementIconSize", 24)
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        settle(20)
        trees = [w for w in mw.findChildren(QtWidgets.QTreeWidget)
                 if w.objectName() == "elementsWidget"]
        # the ended edit's panel is only deleted later: take the newest
        tree = [t for t in trees if t.isVisible()][-1]
        check("ElementIconSize sets the icon size", tree.iconSize().height() == 24,
              tree.iconSize().height())
        prefs.RemInt("ElementIconSize")
        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle()
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
