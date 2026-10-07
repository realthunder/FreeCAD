"""The settings of the Sketcher's own group are listed, and a new sketch still takes them.

Twenty-three keys of Preferences/Mod/Sketcher are behind
Sketcher::SketcherParams (docs/HandsOnQueue.md entry 24): the continue
modes, the dialog after a dimension, what a new sketch starts with (internal
faces, arc fitting, B-splines of external geometry, the history level), the
constraint list's switches, and how dimensions and cursor coordinates are
written. Its sub-groups (General, View, Snap, SolverAdvanced, ...) are not
done yet. The Sketcher's pages are held to the definitions by
preferences-ok-keeps-defaults.py, which named one of them: "Use system
decimals" is on to the program and was shown off by the Display page, and
OK stored off.

Claims:

  - "/param geometry creation continue mode" lists the continue mode of
    the geometry tools, and "/param hide base length units" the units
    switch;
  - a sketch made with "generate internal faces" stored off has
    MakeInternals off, one made without the key has it on;
  - a sketch made with the external B-spline degree stored as 3 has that
    degree, one made without the key has 5.

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
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            rows += [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return rows


def run():
    group = FreeCAD.ParamGet(PREFS + "Mod/Sketcher")
    doc = None
    try:
        import Sketcher  # noqa: F401  the module registers its settings when it is loaded
        settle(0.5)

        rows = param_rows("geometry creation continue mode")
        check("the omni search lists the geometry tools' continue mode",
              any(r.endswith("Mod/Sketcher/ContinuousCreationMode") for r in rows), rows[:6])
        rows = param_rows("hide base length units")
        check("and the units switch", any(r.endswith("Mod/Sketcher/HideUnits") for r in rows), rows[:6])

        doc = FreeCAD.newDocument("Entry24Sketcher")
        settle(0.5)
        group.SetBool("MakeInternals", False)
        settle(0.2)
        off = doc.addObject("Sketcher::SketchObject", "SketchPlain").MakeInternals
        group.RemBool("MakeInternals")
        settle(0.2)
        on = doc.addObject("Sketcher::SketchObject", "SketchFaces").MakeInternals
        check("a sketch made with internal faces stored off has them off, one made without the key has them on",
              off is False and on is True, (off, on))

        group.SetInt("ExternalBSplineMaxDegree", 3)
        settle(0.2)
        three = doc.addObject("Sketcher::SketchObject", "SketchDegree3").ExternalBSplineMaxDegree
        group.RemInt("ExternalBSplineMaxDegree")
        settle(0.2)
        five = doc.addObject("Sketcher::SketchObject", "SketchDegree5").ExternalBSplineMaxDegree
        check("a sketch made with the external B-spline degree stored as 3 has 3, one made without the key 5",
              three == 3 and five == 5, (three, five))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        group.RemBool("MakeInternals")
        group.RemInt("ExternalBSplineMaxDegree")
        if doc is not None:
            FreeCAD.closeDocument(doc.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
