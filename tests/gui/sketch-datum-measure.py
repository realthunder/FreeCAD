"""A circle's size, said as its radius or as its diameter, in the value editor.

Upstream put two radio buttons in its datum dialog (`c9041132f9`): Radius or
Diameter, chosen while the value is typed. Here the dialog is only the
fallback; the value is typed in the editor over the dimension's label
(docs/SketcherPort.md "One editor for a constraint's value"), so the switch
is a button in that editor, beside the driving/reference toggle. Ruled
2026-10-03.

What the switch does:

  - it shows only for a radius or a diameter dimension;
  - while nothing has been typed, the circle keeps its size: the number in
    the line is restated in the other measure (5 mm as a radius is 10 mm as
    a diameter);
  - a number that WAS typed is left as typed: it is what the user means in
    the measure being chosen;
  - Enter applies the kind with the value, in one undo step: the constraint
    becomes the other kind in place, its name and its place in the list
    kept (SketchObject.setDiameter);
  - an expression is left as it is and then gives the other measure;
  - a reference stays a reference;
  - Ctrl+Shift+R is its key.

Sketch: a circle of radius 5 with a Radius dimension named "Hole", an arc
of radius 10 with a Diameter dimension, and a line with a Distance.

The editor is the visible QFrame "DatumValueEditor"; the switch is its tool
button "DatumValueEditorMeasure".

Before this commit there was no such button and no setDiameter: a feature,
not a defect, so there is no before-state to score but the absence.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
GENERAL = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"
SHOT = os.environ.get("GT_SHOT", "")
state = {"done": False, "dialogs": 0}
RADIUS, DIAMETER, DISTANCE = 0, 1, 2


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))
    return cond


def settle(n=12):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def watchdog():
    dialog = QtWidgets.QApplication.activeModalWidget()
    if dialog is not None:
        state["dialogs"] += 1
        dialog.reject()


def editor():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QFrame, "DatumValueEditor"):
        if w.isVisible():
            return w
    return None


def line():
    e = editor()
    return e.findChild(QtWidgets.QLineEdit, "DatumValueEditorLine") if e else None


def switch():
    e = editor()
    return e.findChild(QtWidgets.QToolButton, "DatumValueEditorMeasure") if e else None


def text():
    w = line()
    return w.text() if w is not None else None


def type_in(value):
    w = line()
    w.setText(value)
    # what typing does: textEdited, which the editor listens to
    w.textEdited.emit(value)
    settle(3)


def key(k, mods=QtCore.Qt.NoModifier):
    target = line()
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(target, QtGui.QKeyEvent(kind, k, mods))
    settle()


def open_on(panel, row):
    panel.itemActivated.emit(panel.item(row))
    settle()


def kind(sk, index):
    c = sk.Constraints[index]
    return (c.Type, round(c.Value, 6))


def run():
    try:
        import Part
        import Sketcher
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet(GENERAL).RemBool("EditDatumInPlace")
        FreeCAD.ParamGet(GENERAL).RemBool("DatumEscapeTakesBack")
        timer = QtCore.QTimer()
        timer.timeout.connect(watchdog)
        timer.start(150)
        state["timer"] = timer

        FreeCADGui.getMainWindow().showMaximized()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("DatumMeasure")
        doc.UndoMode = 1
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.Circle(V(-20, 5, 0), V(0, 0, 1), 5), False)
        sk.addGeometry(Part.ArcOfCircle(Part.Circle(V(20, 5, 0), V(0, 0, 1), 10), 0.3, 2.6),
                       False)
        sk.addGeometry(Part.LineSegment(V(-20, -20, 0), V(20, -20, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Radius", 0, 5.0))
        sk.addConstraint(Sketcher.Constraint("Diameter", 1, 20.0))
        sk.addConstraint(Sketcher.Constraint("Distance", 2, 40.0))
        sk.renameConstraint(RADIUS, "Hole")
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        settle(60)
        panel = FreeCADGui.getMainWindow().findChild(QtWidgets.QListWidget,
                                                     "listWidgetConstraints")

        # -- where the switch is ------------------------------------------
        open_on(panel, DISTANCE)
        check("a distance has no radius/diameter switch",
              editor() is not None and switch() is not None and not switch().isVisible())
        key(QtCore.Qt.Key_Return)

        open_on(panel, RADIUS)
        check("a radius has one, and it says Radius",
              switch() is not None and switch().isVisible()
              and switch().toolTip().startswith("Radius"),
              switch().toolTip() if switch() else None)
        check("with an icon", switch() is not None and not switch().icon().isNull())
        check("the line holds the radius", text().startswith("5"), text())
        if SHOT:
            FreeCADGui.getMainWindow().grab().save(os.path.join(SHOT, "measure-radius.png"))

        # -- untouched: the number is restated, the circle kept ------------
        undo = doc.UndoCount
        switch().click()
        settle()
        check("the switch restates the untouched number as a diameter",
              text().startswith("10"), text())
        check("and says Diameter now", switch().toolTip().startswith("Diameter"),
              switch().toolTip())
        check("nothing is applied before Enter", kind(sk, RADIUS) == ("Radius", 5.0),
              kind(sk, RADIUS))
        if SHOT:
            FreeCADGui.getMainWindow().grab().save(os.path.join(SHOT, "measure-diameter.png"))
        key(QtCore.Qt.Key_Return)
        check("Enter makes the constraint a diameter of 10, in place",
              kind(sk, RADIUS) == ("Diameter", 10.0) and len(sk.Constraints) == 3,
              (kind(sk, RADIUS), len(sk.Constraints)))
        check("its name kept", sk.Constraints[RADIUS].Name == "Hole", sk.Constraints[RADIUS].Name)
        check("the circle is what it was", abs(sk.Geometry[0].Radius - 5.0) < 1e-9,
              sk.Geometry[0].Radius)
        check("in one undo step", doc.UndoCount == undo + 1, doc.UndoCount - undo)
        doc.undo()
        settle()
        check("which takes the kind back with the value", kind(sk, RADIUS) == ("Radius", 5.0),
              kind(sk, RADIUS))

        # -- there and back is nothing -------------------------------------
        undo = doc.UndoCount
        open_on(panel, RADIUS)
        switch().click()
        settle()
        key(QtCore.Qt.Key_R, QtCore.Qt.ControlModifier | QtCore.Qt.ShiftModifier)
        check("Ctrl+Shift+R switches too: back to the radius, the number as it was",
              text().startswith("5") and switch().toolTip().startswith("Radius"),
              (text(), switch().toolTip()))
        key(QtCore.Qt.Key_Return)
        check("there and back changes nothing and leaves no undo step",
              kind(sk, RADIUS) == ("Radius", 5.0) and doc.UndoCount == undo,
              (kind(sk, RADIUS), doc.UndoCount - undo))

        # -- typed: the number is the user's -------------------------------
        open_on(panel, RADIUS)
        type_in("8 mm")
        switch().click()
        settle()
        check("a number that was typed is left as typed", text() == "8 mm", text())
        key(QtCore.Qt.Key_Return)
        check("and is applied as the measure chosen: a diameter of 8",
              kind(sk, RADIUS) == ("Diameter", 8.0) and abs(sk.Geometry[0].Radius - 4.0) < 1e-9,
              (kind(sk, RADIUS), sk.Geometry[0].Radius))
        doc.undo()
        settle()

        # -- a diameter, said as a radius ----------------------------------
        open_on(panel, DIAMETER)
        check("a diameter says Diameter and holds 20",
              switch().toolTip().startswith("Diameter") and text().startswith("20"),
              (switch().toolTip(), text()))
        switch().click()
        settle()
        check("switched, the line holds the radius, 10", text().startswith("10"), text())
        key(QtCore.Qt.Key_Return)
        check("Enter makes it a radius of 10, the arc as it was",
              kind(sk, DIAMETER) == ("Radius", 10.0)
              and abs(sk.Geometry[1].Radius - 10.0) < 1e-9,
              (kind(sk, DIAMETER), sk.Geometry[1].Radius))
        doc.undo()
        settle()

        # -- an expression is left alone, and gives the other measure ------
        sk.setExpression("Constraints[%d]" % RADIUS, "2 mm + 3 mm")
        doc.recompute()
        settle()
        open_on(panel, RADIUS)
        before = text()
        switch().click()
        settle()
        check("an expression is not rewritten by the switch",
              text() == before and before.startswith("="), (before, text()))
        key(QtCore.Qt.Key_Return)
        doc.recompute()
        settle()
        check("it gives the diameter now: 5 mm across",
              kind(sk, RADIUS) == ("Diameter", 5.0) and abs(sk.Geometry[0].Radius - 2.5) < 1e-9,
              (kind(sk, RADIUS), sk.Geometry[0].Radius))
        check("and is still bound",
              any("Constraints" in path for path, _ in sk.ExpressionEngine),
              sk.ExpressionEngine)
        doc.undo()
        settle()
        sk.setExpression("Constraints[%d]" % RADIUS, None)
        doc.recompute()
        settle()

        # -- a reference stays one -----------------------------------------
        sk.setDriving(RADIUS, False)
        doc.recompute()
        settle()
        open_on(panel, RADIUS)
        switch().click()
        settle()
        key(QtCore.Qt.Key_Return)
        c = sk.Constraints[RADIUS]
        check("a reference becomes a reference diameter",
              c.Type == "Diameter" and c.Driving is False and abs(c.Value - 10.0) < 1e-6,
              (c.Type, c.Driving, c.Value))

        check("no modal dialog was raised along the way", state["dialogs"] == 0,
              state["dialogs"])
        FreeCADGui.ActiveDocument.resetEdit()
        settle()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        state["timer"].stop()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
