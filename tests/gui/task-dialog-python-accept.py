"""Gui.Control.activeTaskDialog().accept() and .reject() press the buttons.

TaskDialogPy's accept() and reject() click the dialog's Ok or Cancel
button, so that a script closes a task dialog the way a user does: the
dialog's own accept runs and the task view closes it. They find the
buttons through the dialog's button box, which the task view hands to the
dialog when it shows it (TaskDialogAttorney::setButtonBox, upstream
ff6f04dcc2, 2022).

That hand-over was missing here -- the line is in the merge base and went
in a merge -- so the dialog's button box was null, and both calls
returned None having done nothing, for every task dialog. Found by
upstream's Sketcher GUI tests, which leave a sketch with them
(TestOnViewParameterGui.test_task_dialog_accept_exits_sketch_edit and
..._reject_...).

Claims:
  - a Python panel: accept() calls the panel's accept once and the dialog
    is gone; reject() the panel's reject, likewise;
  - a panel whose accept returns False is asked and stays open;
  - a sketch in edit: accept() leaves the edit, and so does reject().
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "TaskDialogAccept"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


class Panel:
    def __init__(self, agree=True):
        self.form = QtWidgets.QWidget()
        self.agree = agree
        self.accepted = 0
        self.rejected = 0

    def accept(self):
        self.accepted += 1
        return self.agree

    def reject(self):
        self.rejected += 1
        return True


def shown(panel):
    FreeCADGui.Control.showDialog(panel)
    settle(0.4)
    return FreeCADGui.Control.activeTaskDialog()


def panel_cases():
    panel = Panel()
    dlg = shown(panel)
    if check("a panel: its task dialog is up", dlg is not None):
        dlg.accept()
        settle(0.4)
        check("a panel: accept() ran the panel's accept, once",
              (panel.accepted, panel.rejected) == (1, 0), (panel.accepted, panel.rejected))
        check("a panel: and the dialog is gone", not FreeCADGui.Control.activeDialog())
    if FreeCADGui.Control.activeDialog():
        FreeCADGui.Control.closeDialog()
        settle(0.3)

    panel = Panel()
    dlg = shown(panel)
    if check("a second panel: its task dialog is up", dlg is not None):
        dlg.reject()
        settle(0.4)
        check("a second panel: reject() ran the panel's reject, once",
              (panel.accepted, panel.rejected) == (0, 1), (panel.accepted, panel.rejected))
        check("a second panel: and the dialog is gone", not FreeCADGui.Control.activeDialog())
    if FreeCADGui.Control.activeDialog():
        FreeCADGui.Control.closeDialog()
        settle(0.3)

    panel = Panel(agree=False)
    dlg = shown(panel)
    if check("a panel that says no: its task dialog is up", dlg is not None):
        dlg.accept()
        settle(0.4)
        check("a panel that says no: it was asked",
              panel.accepted == 1, panel.accepted)
        check("a panel that says no: and it stays open",
              bool(FreeCADGui.Control.activeDialog()))
    if FreeCADGui.Control.activeDialog():
        FreeCADGui.Control.closeDialog()
        settle(0.3)


def sketch_case(doc, how):
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    doc.recompute()
    gdoc = FreeCADGui.getDocument(DOC)
    gdoc.setEdit(sk)
    settle(0.8)
    dlg = FreeCADGui.Control.activeTaskDialog()
    if check("a sketch, %s: in edit with its task dialog" % how,
             gdoc.getInEdit() is not None and dlg is not None):
        getattr(dlg, how)()
        settle(0.8)
        check("a sketch, %s: the edit is left" % how, gdoc.getInEdit() is None)
        check("a sketch, %s: and the dialog is gone" % how,
              not FreeCADGui.Control.activeDialog())
    if gdoc.getInEdit() is not None:
        gdoc.resetEdit()
        settle(0.5)


def run():
    try:
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        settle(0.5)
        panel_cases()
        sketch_case(doc, "accept")
        sketch_case(doc, "reject")
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    if FreeCADGui.Control.activeDialog():
        FreeCADGui.Control.closeDialog()
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
