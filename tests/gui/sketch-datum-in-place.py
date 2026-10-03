"""A dimension's value is typed in the view, at the constraint's label.

Ruled 2026-10-02: no modal dialog for a dimension's value -- an entry box
at the label (the box of a drawing tool's on-view parameter), Enter
applies, and so do Escape (ruled 2026-10-03: Escape means leave) and a
click elsewhere; Std_Undo while it runs takes it back. On by
default (Mod/Sketcher/General/EditDatumInPlace); the dialog stays for a
value the box cannot edit, and behind "Edit Value".

Sketch: two lines, the first with a Distance of 60.

Checks, the box found as the visible line of the view's value editor

  - a dimension made by a command on a selection opens a box at its label
    and no dialog; typing a value and Enter sets the datum, and the
    constraint and its value are ONE undo step;
  - Escape in the box applies as Enter does, and Std_Undo while it runs
    takes the new constraint back;
  - an existing dimension, from the constraints panel: the box holds the
    value, Enter with another sets it in one step;
  - text that is no value keeps the box open on Enter;
  - a click elsewhere in the view applies what was typed, and is not a
    pick: nothing is selected by it;
  - the Dimension tool's dimension is typed the same way, one undo step
    with its constraint, and the tool goes on afterwards;
  - a dimension driven by an expression opens in the editor too, on its
    expression (sketch-datum-editor.py goes on from there);
  - with the preference off the dialog opens, as it always did.

A watchdog closes any modal dialog and counts it, so a dialog where a box
was expected is a failed check and not a hung run.
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
state = {"done": False, "dialogs": 0}


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


def boxes():
    """the value editor's line, where one is shown (DatumValueEditor)"""
    return [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QLineEdit,
                                                               "DatumValueEditorLine")
            if w.isVisible()]


def ancestors(w):
    out = []
    p = w.parentWidget()
    while p is not None:
        out.append(p)
        p = p.parentWidget()
    return out


def type_in(box, text):
    box.setText(text)
    # what typing does: textEdited, which the editor listens to
    box.textEdited.emit(text)
    settle(3)


def key(box, k):
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(box, QtGui.QKeyEvent(kind, k, QtCore.Qt.NoModifier))
    settle()


def viewer_widget():
    return [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget)
            if "View3DInventorViewer" in w.metaObject().className() and w.isVisible()][0]


def click(view, point):
    """a move, a press and a release of the first button at a model point"""
    target = viewer_widget()
    p = view.getPointOnViewport(point)
    pos = QtCore.QPointF(p[0], target.height() - 1 - p[1])
    for kind, buttons in ((QtCore.QEvent.MouseMove, QtCore.Qt.NoButton),
                          (QtCore.QEvent.MouseMove, QtCore.Qt.NoButton),
                          (QtCore.QEvent.MouseButtonPress, QtCore.Qt.LeftButton),
                          (QtCore.QEvent.MouseButtonRelease, QtCore.Qt.NoButton)):
        button = QtCore.Qt.NoButton if kind == QtCore.QEvent.MouseMove else QtCore.Qt.LeftButton
        QtWidgets.QApplication.sendEvent(target, QtGui.QMouseEvent(
            kind, pos, target.mapToGlobal(pos), button, buttons, QtCore.Qt.NoModifier))
        settle(4)


def distance_on(sk, edge):
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(sk, edge)
    settle(4)
    FreeCADGui.runCommand("Sketcher_ConstrainDistance")
    settle()


def run():
    try:
        import Part
        import Sketcher
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet(GENERAL).RemBool("EditDatumInPlace")
        timer = QtCore.QTimer()
        timer.timeout.connect(watchdog)
        timer.start(150)
        state["timer"] = timer

        FreeCADGui.getMainWindow().showMaximized()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("DatumInPlace")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(-30, 10, 0), V(30, 10, 0)), False)
        sk.addGeometry(Part.LineSegment(V(-30, -20, 0), V(20, -40, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Distance", 0, 60.0))
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        settle(60)

        # -- a new dimension, by a command on a selection ------------------
        undo = doc.UndoCount
        distance_on(sk, "Edge2")
        found = boxes()
        check("a new dimension opens a box in the view, and no dialog",
              len(found) == 1 and state["dialogs"] == 0 and sk.ConstraintCount == 2,
              (len(found), state["dialogs"], sk.ConstraintCount))
        if found:
            note("box text: %r" % found[0].text())
            type_in(found[0], "25 mm")
            key(found[0], QtCore.Qt.Key_Return)
        check("Enter sets the typed value and closes the box",
              sk.ConstraintCount == 2 and abs(sk.Constraints[1].Value - 25.0) < 1e-9
              and not boxes(), (sk.ConstraintCount, [c.Value for c in sk.Constraints]))
        check("the constraint and its value are one undo step", doc.UndoCount == undo + 1,
              doc.UndoCount - undo)

        # -- Escape applies, as Enter --------------------------------------
        doc.undo()
        settle()
        undo = doc.UndoCount
        distance_on(sk, "Edge2")
        found = boxes()
        if check("the box again", len(found) == 1, len(found)):
            type_in(found[0], "31 mm")
            key(found[0], QtCore.Qt.Key_Escape)
        check("Escape sets the typed value and closes the box, in one undo step",
              sk.ConstraintCount == 2 and abs(sk.Constraints[1].Value - 31.0) < 1e-9
              and doc.UndoCount == undo + 1 and not boxes(),
              (sk.ConstraintCount, [c.Value for c in sk.Constraints], doc.UndoCount - undo))
        doc.undo()
        settle()

        # -- Std_Undo takes a new constraint back --------------------------
        undo = doc.UndoCount
        distance_on(sk, "Edge2")
        found = boxes()
        if check("the box once again", len(found) == 1, len(found)):
            type_in(found[0], "31 mm")
            FreeCADGui.runCommand("Std_Undo")
            settle()
        check("Std_Undo while it runs leaves no constraint and no undo step",
              sk.ConstraintCount == 1 and doc.UndoCount == undo and not boxes(),
              (sk.ConstraintCount, doc.UndoCount - undo, len(boxes())))

        # -- an existing dimension, from the panel -------------------------
        panel = FreeCADGui.getMainWindow().findChild(QtWidgets.QListWidget,
                                                     "listWidgetConstraints")
        undo = doc.UndoCount
        panel.itemActivated.emit(panel.item(0))
        settle()
        found = boxes()
        if check("activating a dimension in the panel opens the box with its value",
                 len(found) == 1 and found[0].text().startswith("60"),
                 [b.text() for b in found]):
            type_in(found[0], "not a value")
            key(found[0], QtCore.Qt.Key_Return)
            check("text that is no value keeps the box open on Enter",
                  len(boxes()) == 1 and abs(sk.Constraints[0].Value - 60.0) < 1e-9,
                  (len(boxes()), sk.Constraints[0].Value))
            type_in(found[0], "40 mm")
            key(found[0], QtCore.Qt.Key_Return)
        check("Enter sets it, in one undo step",
              abs(sk.Constraints[0].Value - 40.0) < 1e-9 and doc.UndoCount == undo + 1
              and not boxes(), (sk.Constraints[0].Value, doc.UndoCount - undo))

        # -- a click elsewhere applies -------------------------------------
        panel.itemActivated.emit(panel.item(0))
        settle()
        found = boxes()
        FreeCADGui.Selection.clearSelection()
        if check("the box once more", len(found) == 1, len(found)):
            type_in(found[0], "33 mm")
            # on the second line: a pick there would select it
            click(view, V(-5, -30, 0))
            settle()
        selected = [n for e in FreeCADGui.Selection.getSelectionEx("*")
                    for n in e.SubElementNames]
        check("a click elsewhere applies the typed value",
              abs(sk.Constraints[0].Value - 33.0) < 1e-9 and not boxes(),
              (sk.Constraints[0].Value, len(boxes())))
        check("and is used up by that: it picks nothing", selected == [], selected)

        # -- the Dimension tool: its value, then the tool goes on -----------
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(sk, "Edge2")
        settle(4)
        undo = doc.UndoCount
        count = sk.ConstraintCount
        FreeCADGui.runCommand("Sketcher_Dimension")
        settle()
        # a click on blank paper places the dimension
        click(view, V(25, -5, 0))
        settle()
        found = boxes()
        if check("the Dimension tool's dimension opens a box, and no dialog",
                 len(found) == 1 and state["dialogs"] == 0, (len(found), state["dialogs"])):
            type_in(found[0], "44 mm")
            key(found[0], QtCore.Qt.Key_Return)
        made = [c.Value for c in sk.Constraints][count:]
        check("Enter sets it: the constraint and its value in one undo step",
              made == [44.0] and doc.UndoCount == undo + 1 and not boxes(),
              (made, doc.UndoCount - undo))
        # the tool is running again (continuous mode): leave it
        target = viewer_widget()
        for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
            QtWidgets.QApplication.sendEvent(target, QtGui.QKeyEvent(
                kind, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier))
        settle()
        check("leaving the tool afterwards keeps the dimension",
              [c.Value for c in sk.Constraints][count:] == [44.0],
              [c.Value for c in sk.Constraints])
        doc.undo()
        settle()

        # -- an expression is edited in place too ---------------------------
        sk.setExpression("Constraints[0]", "30 + 5")
        doc.recompute()
        settle()
        dialogs = state["dialogs"]
        panel.itemActivated.emit(panel.item(0))
        settle(25)
        found = boxes()
        check("a dimension driven by an expression opens the editor on its expression",
              state["dialogs"] == dialogs and len(found) == 1
              and found[0].text().startswith("="),
              (state["dialogs"] - dialogs, [b.text() for b in found]))
        for b in found:
            key(b, QtCore.Qt.Key_Escape)
        sk.setExpression("Constraints[0]", None)
        doc.recompute()
        settle()

        # -- the preference off: the dialog, as before ----------------------
        FreeCAD.ParamGet(GENERAL).SetBool("EditDatumInPlace", False)
        dialogs = state["dialogs"]
        panel.itemActivated.emit(panel.item(0))
        settle(25)
        check("with the preference off the dialog opens",
              state["dialogs"] == dialogs + 1 and not boxes(),
              (state["dialogs"] - dialogs, len(boxes())))
        FreeCAD.ParamGet(GENERAL).RemBool("EditDatumInPlace")

        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    FreeCAD.ParamGet(GENERAL).RemBool("EditDatumInPlace")
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    if "timer" in state:
        state["timer"].stop()
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
