"""The constraints panel: the Named filter, renaming by double click, what a
constraint may be named, and showing only the filtered constraints (upstream
67f8852697, 2d5d8ab86c, 766ee41b55, c0d47c5ecd, 9cd3b31067, 46ec53f4da and
its follow-ups).

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
    a dimension and did nothing for the rest;
  - a name typed into a row is taken if it is letters, digits and
    underscores not starting with a digit, so that an expression can refer
    to it; "My Width", "a'b" and "1st" are refused and the constraint keeps
    what it had, the row's edit text with it. Before, each was taken, two
    blanks as well;
  - an emptied row takes the name away. Before, it did nothing;
  - a name another constraint has is refused;
  - a rename leaves one undo step. Before, three: any change of a row also
    wrote the constraint's virtual space, changed or not.

  "Show only filtered constraints" (Mod/Sketcher/VisualisationTrackingFilter):

  - with it on and "Named" alone checked, the unnamed constraints are not
    drawn (the edit scene's switch per constraint is off for them, and
    each has its own visibility off), the list shows the named ones, no
    constraint has changed its virtual space and no undo step was made.
    Before, the unnamed were MOVED into the other virtual space, with undo
    steps, and neither the scene nor the list followed;
  - the option switched off, all six are drawn again. Before, they stayed
    where the option had moved them;
  - switched on again they go, and the filter box unchecked brings them
    back.
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
        doc.UndoMode = 1
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

    def names():
        return [c.Name for c in sk.Constraints]

    def typed(row, text):
        lw.item(row).setData(QtCore.Qt.EditRole, text)
        settle(0.4)

    for text in ("My Width", "a'b", "1st"):
        typed(0, text)
        check("%r is refused as a name" % text,
              names()[0] == "" and lw.item(0).data(QtCore.Qt.EditRole) == "",
              (names()[0], lw.item(0).data(QtCore.Qt.EditRole)))
    undo0 = sk.Document.UndoCount
    typed(0, "Base_1")
    check("'Base_1' is taken", names()[0] == "Base_1" and lw.item(0).text() == "Base_1",
          (names()[0], lw.item(0).text()))
    check("in one undo step", sk.Document.UndoCount == undo0 + 1,
          (undo0, sk.Document.UndoCount, sk.Document.UndoNames[:3]))
    typed(4, "Base_1")
    check("a name another constraint has is refused",
          names()[4] == "" and lw.item(4).data(QtCore.Qt.EditRole) == "",
          (names()[4], lw.item(4).data(QtCore.Qt.EditRole)))
    typed(2, "")
    check("an emptied row takes the name away",
          names()[2] == "" and lw.item(2).text() == "Constraint3 (10 mm)",
          (names()[2], lw.item(2).text()))
    typed(3, "  ")
    check("and so do blanks", names()[3] == "", names()[3])
    typed(2, "Width")
    typed(3, "Upright")

    # -- show only the filtered constraints -----------------------------
    from pivy import coin

    view = FreeCADGui.getDocument(DOC).activeView()
    setting = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher")

    def drawn():
        """The edit scene's switch of each constraint, "1" where it is on."""
        sa = coin.SoSearchAction()
        sa.setType(coin.SoType.fromName("SmSwitchboard"))
        sa.setInterest(coin.SoSearchAction.ALL)
        sa.setSearchingAll(True)
        sa.apply(view.getAuxSceneGraph())
        res = []
        for i in range(sa.getPaths().getLength()):
            enable = sa.getPaths()[i].getTail().getField("enable")
            res.append("".join(str(int(v)) for v in enable.getValues(0)) if enable.getNum() else "")
        return res

    def flags():
        return ("".join(str(int(c.InVirtualSpace)) for c in sk.Constraints),
                "".join("0" if 'IsVisible="0"' in c.Content else "1" for c in sk.Constraints))

    def track(on):
        setting.SetBool("VisualisationTrackingFilter", on)
        settle(0.5)

    undo0 = sk.Document.UndoCount
    check("to begin with all six are drawn",
          drawn() == ["111111"] and flags() == ("000000", "111111"), (drawn(), flags()))
    track(True)
    check("the option on, no filter: still all six", drawn() == ["111111"], drawn())
    box.setChecked(True)
    settle()
    for r in range(fl.count()):
        fl.item(r).setCheckState(QtCore.Qt.Unchecked)
    settle()
    fl.item(named[0]).setCheckState(QtCore.Qt.Checked)
    settle(0.5)
    # (three are named by now: the first was named "Base_1" above)
    check("Named alone: only the three named constraints are drawn",
          drawn() == ["101100"], drawn())
    check("by their own visibility, no constraint's virtual space changed",
          flags() == ("000000", "101100"), flags())
    check("the list shows the three",
          shown() == ["Base_1", "Width (10 mm)", "Upright"], shown())
    check("and no undo step was made", sk.Document.UndoCount == undo0,
          (undo0, sk.Document.UndoCount, sk.Document.UndoNames[:3]))
    track(False)
    check("the option off: all six are drawn again",
          drawn() == ["111111"] and flags() == ("000000", "111111"), (drawn(), flags()))
    track(True)
    check("on again: the three unnamed go", drawn() == ["101100"], drawn())
    box.setChecked(False)
    settle(0.5)
    check("the filter box unchecked: all six are back",
          drawn() == ["111111"] and flags() == ("000000", "111111"), (drawn(), flags()))
    track(False)


def finish():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "VisualisationTrackingFilter", False)
        gdoc = FreeCADGui.getDocument(DOC)
        if gdoc.getInEdit():
            gdoc.resetEdit()
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
