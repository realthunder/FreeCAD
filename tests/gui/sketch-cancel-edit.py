"""Cancelling a sketch edit (upstream 189d86ee53, facca5c426).

The edit's task panel has Ok and Cancel instead of Close, and a command
Sketcher_CancelSketch leaves the edit reverting it. Escape presses Ok: it
leaves keeping what was done.

The fork reverts by undo, back to where the undo history stood when the
edit began, so the cancelled edit can be redone. The history keeps
MaxUndoSize steps (20); an edit longer than that is reverted from a copy
of the sketch taken when it began, in one "Cancel sketch editing" step.

Each step of an edit is one transaction here, as each command of a real
one is.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchCancelEdit"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    import time
    end = time.monotonic() + seconds
    while True:
        QtCore.QCoreApplication.processEvents()
        if time.monotonic() >= end:
            break
        time.sleep(0.01)


def in_edit():
    return FreeCADGui.getDocument(DOC).getInEdit() is not None


def edit_steps(sk, n):
    import Part
    doc = sk.Document
    for i in range(n):
        doc.openTransaction("Add line %d" % i)
        y = 10.0 + 2.0 * i
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(0, y, 0), FreeCAD.Vector(10, y, 0)), False)
        doc.commitTransaction()
    settle()


def enter(sk):
    FreeCADGui.getDocument(DOC).setEdit(sk)
    settle(1.0)


def task_button(which):
    from PySide import QtWidgets
    for box in FreeCADGui.getMainWindow().findChildren(QtWidgets.QDialogButtonBox):
        b = box.button(which)
        if b is not None and b.isVisible():
            return b
    return None


def run():
    try:
        import Part

        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        doc.UndoMode = 1
        doc.openTransaction("Setup")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(10, 0, 0)), False)
        doc.commitTransaction()
        doc.recompute()
        settle()

        # A short edit, cancelled by the command: undone, and can be redone.
        enter(sk)
        check("the edit's task panel has a Cancel button",
              task_button(QtWidgets_button("Cancel")) is not None)
        check("and an Ok button", task_button(QtWidgets_button("Ok")) is not None)
        edit_steps(sk, 3)
        FreeCADGui.runCommand("Sketcher_CancelSketch")
        settle(1.0)
        check("Sketcher_CancelSketch leaves the edit", not in_edit())
        check("and reverts it", len(sk.Geometry) == 1, "%d lines" % len(sk.Geometry))
        check("by undo: the edit can be redone", doc.RedoCount >= 3,
              "RedoCount %d, RedoNames %s" % (doc.RedoCount, doc.RedoNames))

        # An edit longer than the undo history, cancelled by the button.
        enter(sk)
        edit_steps(sk, 25)
        button = task_button(QtWidgets_button("Cancel"))
        if button is not None:
            button.click()
        settle(1.0)
        check("Cancel in the task panel leaves the edit", button is not None and not in_edit())
        check("and reverts an edit longer than the undo history", len(sk.Geometry) == 1,
              "%d lines" % len(sk.Geometry))
        check("in one undoable step", doc.UndoNames[:1] == ["Cancel sketch editing"],
              doc.UndoNames[:3])
        doc.undo()
        settle(0.5)
        check("which undoes back to the edit", len(sk.Geometry) == 26,
              "%d lines" % len(sk.Geometry))
        doc.redo()
        settle(0.5)

        # Escape presses Ok: the edit is kept.
        enter(sk)
        edit_steps(sk, 2)
        from PySide import QtWidgets
        tv = None
        for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
            if w.metaObject().className() == "Gui::TaskView::TaskView" and w.isVisible():
                tv = w
        if tv is not None:
            QtWidgets.QApplication.sendEvent(
                tv, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, QtCore.Qt.Key_Escape,
                                    QtCore.Qt.NoModifier))
        settle(1.0)
        check("Escape in the task panel leaves the edit", tv is not None and not in_edit())
        check("keeping it", len(sk.Geometry) == 3, "%d lines" % len(sk.Geometry))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def QtWidgets_button(name):
    from PySide import QtWidgets
    return getattr(QtWidgets.QDialogButtonBox, name)


def finish():
    if state["done"]:
        return
    state["done"] = True
    if FreeCAD.listDocuments().get(DOC) and in_edit():
        FreeCADGui.getDocument(DOC).resetEdit()
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
