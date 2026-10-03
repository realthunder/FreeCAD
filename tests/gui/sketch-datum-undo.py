"""An undo while a dimension's value is being typed in place.

Ruled 2026-10-03: Std_Undo or Std_Redo while the value is being typed
takes the entry back, and undoes nothing older -- a
spreadsheet's rule for a cell being edited. An undo that does not go
through the GUI first (a Python doc.undo(), another client) is seen only
afterwards: the box then follows its constraint by tag, and closes when
the constraint has gone or the document has taken the box's open
transaction with it.

Measured before the fix (session 120): the box held a constraint INDEX.
An undo that put a deleted constraint back below the edited one made
Enter write the typed value into the WRONG constraint, without a word;
after an undo that took the constraint away the box stayed open over
nothing.

Sketch: three lines, a Distance of 60 on the first.
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


def ancestors(w):
    out = []
    p = w.parentWidget()
    while p is not None:
        out.append(p)
        p = p.parentWidget()
    return out


def boxes():
    """the value editor's line, where one is shown (DatumValueEditor)"""
    return [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QLineEdit,
                                                               "DatumValueEditorLine")
            if w.isVisible()]


def type_in(box, text):
    box.setText(text)
    # what typing does: textEdited, which the editor listens to
    box.textEdited.emit(text)
    settle(3)


def key(box, k):
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(box, QtGui.QKeyEvent(kind, k, QtCore.Qt.NoModifier))
    settle()


def cancel_all():
    """take back an entry still open: Std_Undo while it runs (Escape is Enter)"""
    if boxes():
        FreeCADGui.runCommand("Std_Undo")
    settle()


def values(sk):
    return [(c.First, c.Value) for c in sk.Constraints]


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
        doc = FreeCAD.newDocument("DatumUndo")
        doc.UndoMode = 1
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(-30, 10, 0), V(30, 10, 0)), False)
        sk.addGeometry(Part.LineSegment(V(-30, -20, 0), V(20, -40, 0)), False)
        sk.addGeometry(Part.LineSegment(V(-30, 30, 0), V(30, 40, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Distance", 0, 60.0))
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        settle(60)
        panel = FreeCADGui.getMainWindow().findChild(QtWidgets.QListWidget,
                                                     "listWidgetConstraints")

        # -- Std_Undo on a new dimension: Escape, and nothing older --------
        undo, redo = doc.UndoCount, doc.RedoCount
        distance_on(sk, "Edge2")
        check("a new dimension opens its box", len(boxes()) == 1 and sk.ConstraintCount == 2,
              (len(boxes()), sk.ConstraintCount))
        FreeCADGui.runCommand("Std_Undo")
        settle()
        check("Std_Undo with the box open closes it and takes the new constraint back",
              not boxes() and sk.ConstraintCount == 1, (len(boxes()), sk.ConstraintCount))
        check("and undoes nothing older: no step undone, none to redo",
              doc.UndoCount == undo and doc.RedoCount == redo,
              (doc.UndoCount - undo, doc.RedoCount - redo))

        # -- Std_Undo on an existing dimension ------------------------------
        doc.openTransaction("add two")
        sk.addConstraint(Sketcher.Constraint("Distance", 1, 50.0))
        sk.addConstraint(Sketcher.Constraint("Distance", 2, 40.0))
        doc.commitTransaction()
        doc.recompute()
        settle()
        before = values(sk)
        undo, redo = doc.UndoCount, doc.RedoCount
        panel.itemActivated.emit(panel.item(1))
        settle()
        found = boxes()
        if check("the panel opens a box on an existing dimension",
                 len(found) == 1 and found[0].text().startswith("50"),
                 [b.text() for b in found]):
            type_in(found[0], "51 mm")
        FreeCADGui.runCommand("Std_Undo")
        settle()
        check("Std_Undo closes it and changes nothing",
              not boxes() and values(sk) == before and doc.UndoCount == undo
              and doc.RedoCount == redo,
              (len(boxes()), values(sk), doc.UndoCount - undo, doc.RedoCount - redo))

        # -- Std_Redo the same ---------------------------------------------
        doc.undo()  # "add two" to the redo stack
        doc.recompute()
        settle()
        before = values(sk)
        undo, redo = doc.UndoCount, doc.RedoCount
        panel.itemActivated.emit(panel.item(0))
        settle()
        check("a box again", len(boxes()) == 1, len(boxes()))
        FreeCADGui.runCommand("Std_Redo")
        settle()
        check("Std_Redo closes it and redoes nothing",
              not boxes() and values(sk) == before and doc.UndoCount == undo
              and doc.RedoCount == redo,
              (len(boxes()), values(sk), doc.UndoCount - undo, doc.RedoCount - redo))
        doc.redo()
        doc.recompute()
        settle()

        # -- an undo from Python that renumbers under the box --------------
        doc.openTransaction("delete first")
        sk.delConstraint(0)
        doc.commitTransaction()
        doc.recompute()
        settle()
        # [Edge2 50, Edge3 40]; the box on Edge2's
        panel.itemActivated.emit(panel.item(0))
        settle()
        found = boxes()
        check("a box on Edge2's dimension", len(found) == 1 and found[0].text().startswith("50"),
              [b.text() for b in found])
        doc.undo()  # Edge1's 60 back at index 0
        settle()
        found = boxes()
        check("an undo from Python that renumbers keeps the box",
              len(found) == 1 and sk.ConstraintCount == 3, (len(found), values(sk)))
        if found:
            type_in(found[0], "77 mm")
            key(found[0], QtCore.Qt.Key_Return)
        check("and Enter writes the constraint the box was opened on, not the one now "
              "at its old index", values(sk) == [(0, 60.0), (1, 77.0), (2, 40.0)], values(sk))
        cancel_all()

        # -- an undo from Python that takes the edited constraint away -----
        doc.openTransaction("add one")
        sk.addConstraint(Sketcher.Constraint("DistanceX", 2, 12.0))
        doc.commitTransaction()
        doc.recompute()
        settle()
        panel.itemActivated.emit(panel.item(sk.ConstraintCount - 1))
        settle()
        check("a box on the last one", len(boxes()) == 1, len(boxes()))
        doc.undo()
        settle()
        check("an undo from Python that removes it closes the box",
              not boxes() and sk.ConstraintCount == 3, (len(boxes()), sk.ConstraintCount))

        # -- an undo from Python while a new dimension is typed ------------
        distance_on(sk, "Edge2")
        count = sk.ConstraintCount
        check("a new dimension's box", len(boxes()) == 1 and count == 4,
              (len(boxes()), count))
        doc.undo()
        settle()
        check("an undo from Python takes the box's transaction: the box closes",
              not boxes() and sk.ConstraintCount == 3, (len(boxes()), values(sk)))

        check("no modal dialog on the way", state["dialogs"] == 0, state["dialogs"])
        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
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
