"""The Validate Sketch panel's fixes are commands a macro can keep.

The panel had an analysis of its own and called it directly: what its Fix
buttons did to the sketch was written nowhere, so a recorded macro of a
validation session came out empty of it (upstream issue 13518). Upstream
sends each fix as a Python command on the sketch (`37f0ad43f9`), which made
the panel find with its own analysis and fix with the sketch's -- and fix
nothing (issue 14240) -- until the panel was given the sketch's analysis for
both (`5696ee821c`). The three refactors of the analysis in between
(`5461d0d27f`, `d5c92fee98`, `1c03bc4eaa`) and a lint pass (`989c710d17`) left
those four files with nothing of the fork's own in them, so they are taken as
upstream has them now.

Sketch: two lines that meet at a corner with no coincident constraint, and a
line too short to be one.

Claims:

  - the degenerated line is found and removed, and the removal is in the
    Python console as a command on the sketch;
  - Find reports the missing coincidence and Fix adds the constraint, and
    that is such a command too.

What the panel does to the sketch held before; that it is written did not.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchValidationPanel"
OBJ = "Sketch"
V = FreeCAD.Vector


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=10):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def console_text():
    """Everything the Python console shows: commands are echoed there."""
    text = []
    for edit in FreeCADGui.getMainWindow().findChildren(QtWidgets.QPlainTextEdit):
        if edit.metaObject().className() == "Gui::PythonConsole":
            text.append(edit.toPlainText())
    return "\n".join(text)


def button(name):
    for b in FreeCADGui.getMainWindow().findChildren(QtWidgets.QPushButton):
        if b.objectName() == name:
            return b
    raise RuntimeError("no button named " + name)


def run():
    try:
        import Part
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(10, 0, 0)), False)
        sk.addGeometry(Part.LineSegment(V(10, 0, 0), V(10, 10, 0)), False)
        sk.addGeometry(Part.LineSegment(V(0, 20, 0), V(1e-9, 20, 0)), False)
        doc.recompute()
        check("the sketch starts with three lines and no constraint",
              sk.GeometryCount == 3 and sk.ConstraintCount == 0,
              (sk.GeometryCount, sk.ConstraintCount))
        check("the Python console is there to read", console_text() != "" or any(
            e.metaObject().className() == "Gui::PythonConsole"
            for e in FreeCADGui.getMainWindow().findChildren(QtWidgets.QPlainTextEdit)))

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(DOC, OBJ)
        settle()
        FreeCADGui.runCommand("Sketcher_ValidateSketch", 0)
        settle(20)

        # The short line first: its two ends are a coincidence of their own.
        button("findDegenerated").click()
        settle(20)
        check("the degenerated line is found", button("fixDegenerated").isEnabled())
        button("fixDegenerated").click()
        settle(20)
        check("and removed", sk.GeometryCount == 2, sk.GeometryCount)
        check("by a command on the sketch in the Python console",
              ".removeDegeneratedGeometries(" in console_text(),
              console_text()[-300:].replace("\n", " / "))

        button("findButton").click()
        settle(20)
        check("Find leaves the fix to be made", button("fixButton").isEnabled())
        button("fixButton").click()
        settle(20)
        kinds = [c.Type for c in sk.Constraints]
        check("Fix adds the missing coincidence", kinds == ["Coincident"], kinds)
        check("and that is such a command too",
              ".makeMissingPointOnPointCoincident()" in console_text(),
              console_text()[-300:].replace("\n", " / "))

        FreeCADGui.Control.closeDialog()
        settle(10)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    try:
        FreeCADGui.Control.closeDialog()
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
