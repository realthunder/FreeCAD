"""The Sketcher's solver settings are listed, and the solver box stores a value under its own key.

The 27 keys of Preferences/Mod/Sketcher/SolverAdvanced are behind
Sketcher::SketcherParams (docs/HandsOnQueue.md entry 24). They are set in
the "Advanced solver control" box of the sketch task panel, which is built
for every sketch in edit, shown or not. Reading that box found three slips
in it:

  - the three parameters of the solver that looks for REDUNDANT
    constraints were stored by which solver the MAIN combo box names: with
    Levenberg-Marquardt above and DogLeg below, a value typed into the
    DogLeg field R.Tolg was stored as the Levenberg-Marquardt R.Eps, and
    DogLeg's own key stayed as it was;
  - when the redundant solver is Levenberg-Marquardt its tau was set from
    eps1 (from the code; nothing a script can see says what the solver was
    given);
  - unticking "sketch size multiplier" of the redundant solver switched it
    on (from the code, likewise).

Claims, with a sketch in edit:

  - "/param default solver" lists the solver of the sub-group
    SolverAdvanced;
  - with Levenberg-Marquardt as the solver and DogLeg as the redundant
    solver, a value typed into the first redundant parameter is stored as
    Redundant_DL_tolg, and Redundant_LM_eps is not stored.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PREFS = "User parameter:BaseApp/Preferences/"
Qt = QtCore.Qt
KEYS = ("DefaultSolver", "RedundantDefaultSolver", "Redundant_DL_tolg", "Redundant_LM_eps")


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def stored(name):
    group = FreeCAD.ParamGet(PREFS + "Mod/Sketcher/SolverAdvanced")
    return [value for kind, key, value in group.GetContents() or [] if key == name]


def named(name):
    # the task panel is not always a child of the main window (overlays)
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == name:
            return w
    return None


def param_rows(query):
    FreeCADGui.runCommand("Std_OmniSearch")
    settle(0.6)
    edit = None
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == "OmniSearchEdit" and w.isVisible():
            edit = w
    if edit is None:
        return []
    text = "/param " + query
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(0.8)
    rows = []
    for w in QtWidgets.QApplication.topLevelWidgets():
        if not isinstance(w, QtWidgets.QAbstractItemView) or w.model() is None:
            continue
        found = [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
        # The list is not shown while another application is in front, which a
        # test cannot prevent on a desktop in use; its rows are there all the same.
        if w.isVisible() or any(r.startswith("Preferences/") for r in found):
            rows += found
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return rows


def run():
    group = FreeCAD.ParamGet(PREFS + "Mod/Sketcher/SolverAdvanced")
    doc = None
    try:
        import Sketcher  # noqa: F401
        import SketcherGui  # noqa: F401
        settle(0.5)
        rows = param_rows("default solver")
        check("the omni search lists the sketch solver",
              any(r.endswith("Mod/Sketcher/SolverAdvanced/DefaultSolver") for r in rows), rows[:6])

        doc = FreeCAD.newDocument("Entry24Solver")
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        doc.recompute()
        settle(0.5)
        FreeCADGui.ActiveDocument.setEdit(sketch.Name)
        settle(2.0)
        solver = named("comboBoxDefaultSolver")
        redundant = named("comboBoxRedundantDefaultSolver")
        field = named("lineEditRedundantSolverParam1")
        note("INFO in edit: %s; task panel widgets named *Solver*: %d"
             % (FreeCADGui.ActiveDocument.getInEdit() is not None,
                sum(1 for w in QtWidgets.QApplication.allWidgets() if "Solver" in w.objectName())))
        if check("a sketch in edit has the solver box", None not in (solver, redundant, field)):
            solver.setCurrentIndex(1)  # Levenberg-Marquardt
            settle(0.3)
            redundant.setCurrentIndex(2)  # DogLeg
            settle(0.3)
            field.setText("1E-50")
            field.editingFinished.emit()
            settle(0.5)
            dogleg, lm = stored("Redundant_DL_tolg"), stored("Redundant_LM_eps")
            check("a value typed for the redundant DogLeg solver is stored under DogLeg's key, not Levenberg-Marquardt's",
                  len(dogleg) == 1 and "50" in str(dogleg[0]) and lm == [], (dogleg, lm))
        FreeCADGui.ActiveDocument.resetEdit()
        settle(1.0)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        for key in KEYS:
            group.RemInt(key)
            group.RemString(key)
        if doc is not None:
            FreeCAD.closeDocument(doc.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
