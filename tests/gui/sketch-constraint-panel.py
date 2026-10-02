"""The constraints panel: the Named filter, and renaming by double click
(upstream 67f8852697 and 2d5d8ab86c).

A sketch with six constraints, two of them named ("Width", a distance, and
"Upright", a vertical), edited; the panel's list and its filter list are
read as widgets.

  - no filter, and the filter on with every entry checked: all six listed;
  - the filter on with nothing checked: none;
  - with "Named" alone checked: the two named ones. Before, the entry did
    nothing of its own -- a named constraint was listed only if its type
    was checked too, so "Named" alone listed none;
  - activating (a double click) the row of a geometric constraint opens its
    name for editing. Before, only F2 did: a double click edits the value of
    a dimension and did nothing for the rest.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ConstraintPanel"
V = FreeCAD.Vector


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(10, 0, 0)), False)
        sk.addGeometry(Part.LineSegment(V(10, 0, 0), V(10, 8, 0)), False)
        sk.addGeometry(Part.Circle(V(4, 4, 0), V(0, 0, 1), 2), False)
        sk.addConstraint(Sketcher.Constraint("Horizontal", 0))
        sk.addConstraint(Sketcher.Constraint("Coincident", 0, 2, 1, 1))
        sk.renameConstraint(sk.addConstraint(Sketcher.Constraint("Distance", 0, 10.0)), "Width")
        sk.renameConstraint(sk.addConstraint(Sketcher.Constraint("Vertical", 1)), "Upright")
        sk.addConstraint(Sketcher.Constraint("Radius", 2, 2.0))
        sk.setDriving(sk.addConstraint(Sketcher.Constraint("Distance", 1, 8.0)), False)
        doc.recompute()
        FreeCADGui.getDocument(DOC).setEdit(sk)
        settle(1.5)
        probe(sk)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finish()


def probe(sk):
    lw = FreeCADGui.getMainWindow().findChild(QtWidgets.QListWidget, "listWidgetConstraints")
    box = FreeCADGui.getMainWindow().findChild(QtWidgets.QCheckBox, "filterBox")
    # the filter list sits in the filter button's menu, not in the window
    filters = [w for w in QtWidgets.QApplication.allWidgets()
               if w.metaObject().className() == "SketcherGui::ConstraintFilterList"]
    if not check("the panel's list, filter box and filter list are there",
                 lw is not None and box is not None and len(filters) == 1,
                 (lw, box, len(filters))):
        return
    fl = filters[0]

    def shown():
        return [lw.item(r).text() for r in range(lw.count()) if not lw.item(r).isHidden()]

    everything = ["Constraint1", "Constraint2", "Width (10 mm)", "Upright",
                  "Constraint5 (2 mm)", "Constraint6 (8 mm)"]
    check("no filter: all six", shown() == everything, shown())
    box.setChecked(True)
    settle()
    check("the filter on, every entry checked: all six", shown() == everything, shown())
    for r in range(fl.count()):
        fl.item(r).setCheckState(QtCore.Qt.Unchecked)
    settle()
    check("nothing checked: none", shown() == [], shown())
    named = [r for r in range(fl.count()) if fl.item(r).text() == "Named"]
    if check("the filter list has a Named entry", len(named) == 1, named):
        fl.item(named[0]).setCheckState(QtCore.Qt.Checked)
        settle()
        check("Named alone: the two named constraints",
              shown() == ["Width (10 mm)", "Upright"], shown())
    box.setChecked(False)
    settle()

    def editors():
        return [w for w in lw.findChildren(QtWidgets.QLineEdit) if w.isVisible()]

    check("no name is being edited", editors() == [], len(editors()))
    lw.setCurrentRow(1)
    lw.itemActivated.emit(lw.item(1))
    settle()
    open_now = editors()
    check("activating a geometric constraint's row opens its name for editing",
          len(open_now) == 1, len(open_now))
    for w in open_now:
        QtWidgets.QApplication.sendEvent(
            w, QtCore.QEvent(QtCore.QEvent.Close))
        lw.closePersistentEditor(lw.item(1))
    lw.setCurrentRow(-1)
    settle()
    check("the sketch is as it was", [c.Name for c in sk.Constraints]
          == ["", "", "Width", "Upright", "", ""], [c.Name for c in sk.Constraints])


def finish():
    try:
        gdoc = FreeCADGui.getDocument(DOC)
        if gdoc.getInEdit():
            gdoc.resetEdit()
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
