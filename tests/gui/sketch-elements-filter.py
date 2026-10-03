"""The elements list's Mode filter: element kinds and geometry types, combined.

The fork's Mode was a single choice -- All, Normal, Construction or
External -- with no way to list only circles, to tell the internal geometry
of an ellipse or a B-spline from ordinary construction, or to show two
kinds at once. It is now upstream's checkable list, in the Mode button's
pop-up: Normal, Construction, Internal, External, then "All types" and the
nine geometry types. An element is listed when its kind and its type are
both ticked. The ticks are upstream's ElementFilterState parameter, one bit
per entry.

Sketch: a line, a construction line, a circle and an ellipse with its
internal geometry exposed (two axis lines and two focus points, all
internal). Checks, reading which rows the list hides:

  - with everything ticked, every row is listed and the button reads All;
  - unticking Construction hides the construction line only -- internal
    geometry is its own kind now -- and the button reads Filtered;
  - unticking Internal hides the ellipse's four internal elements;
  - unticking Line hides the lines of every kind still listed;
  - unticking All types hides every typed row, and ticking it back lists
    them again under the kinds still ticked;
  - the state is saved in ElementFilterState.

Scored against the tree before the change: there is no filter button, so
the test stops at its first check.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
GENERAL = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"
state = {"done": False}

NORMAL, CONSTRUCTION, INTERNAL, EXTERNAL, ALL_TYPES = 0, 1, 2, 3, 4
POINT, LINE, CIRCLE, ELLIPSE = 5, 6, 7, 8


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


def widget(name):
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.objectName() == name:
            return w
    return None


def run():
    try:
        import Part
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet(GENERAL).RemInt("ElementFilterState")
        FreeCADGui.getMainWindow().showMaximized()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("ElementsFilter")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(10, 0, 0)), False)        # 0
        sk.addGeometry(Part.LineSegment(V(0, 5, 0), V(10, 5, 0)), True)         # 1
        sk.addGeometry(Part.Circle(V(20, 0, 0), V(0, 0, 1), 3), False)          # 2
        sk.addGeometry(Part.Ellipse(V(40, 0, 0), 6, 3), False)                   # 3
        sk.exposeInternalGeometry(3)                                             # 4..7
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        settle(20)

        # the constraints panel has a filterButton of its own
        button = None
        for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QToolButton):
            if (w.objectName() == "filterButton"
                    and w.parent().objectName() == "SketcherGui__TaskSketcherElements"):
                button = w
        if not check("the Mode filter is a button with a list", button is not None
                     and button.menu() is not None):
            finish()
            return
        # Hold the action's wrapper: chained off a temporary, PySide deletes
        # the list with it.
        action = button.menu().actions()[0]
        flist = action.defaultWidget()
        tree = widget("elementsWidget")

        def listed():
            """geo ids of the rows the list shows"""
            out = []
            for i in range(tree.topLevelItemCount()):
                item = tree.topLevelItem(i)
                if not item.isHidden():
                    out.append(i)
            return out

        def tick(row, on):
            flist.item(row).setCheckState(QtCore.Qt.Checked if on else QtCore.Qt.Unchecked)
            settle()

        rows = tree.topLevelItemCount()
        internal = [i for i in range(4, rows)]
        note("rows %d, internal %s" % (rows, internal))
        check("everything ticked lists every row", listed() == list(range(rows)), listed())
        check("and the button reads All", button.text() == "All", button.text())

        tick(CONSTRUCTION, False)
        check("unticking Construction hides the construction line only",
              listed() == [0, 2, 3] + internal, listed())
        check("and the button reads Filtered", button.text() == "Filtered", button.text())

        tick(INTERNAL, False)
        check("unticking Internal hides the ellipse's internal elements",
              listed() == [0, 2, 3], listed())

        tick(CONSTRUCTION, True)
        tick(INTERNAL, True)
        tick(LINE, False)
        shown = listed()
        lines_left = [i for i in shown if sk.Geometry[i].TypeId == "Part::GeomLineSegment"]
        check("unticking Line hides the lines of every kind", lines_left == [], shown)
        check("and All types shows partly ticked",
              flist.item(ALL_TYPES).checkState() == QtCore.Qt.PartiallyChecked,
              flist.item(ALL_TYPES).checkState())

        tick(ALL_TYPES, False)
        check("unticking All types hides every typed row", listed() == [], listed())
        tick(ALL_TYPES, True)
        check("ticking it back lists them again", listed() == list(range(rows)), listed())

        tick(EXTERNAL, False)
        saved = FreeCAD.ParamGet(GENERAL).GetInt("ElementFilterState")
        want = sum(1 << i for i in range(14) if i != EXTERNAL)
        check("the state is saved in ElementFilterState", saved == want,
              "%s vs %s" % (bin(saved), bin(want)))

        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    FreeCAD.ParamGet(GENERAL).RemInt("ElementFilterState")
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
