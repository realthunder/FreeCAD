"""Upstream's Sketcher preselection tests, run on the fork's hover pick.

src/Mod/Sketcher/SketcherTests/TestConstraintPreselectionGui.py is
upstream's file, unchanged: which element a hover at a viewport position
preselects when a vertex, a curve, an axis and a constraint's icon or
number overlap. Upstream wrote it for its screen-space preselection rework
(2f3161f312 and the commits after it); the fork keeps its own pick, and
this runs the same checks against it through the same probe,
SketcherGui.getActiveSketchPreselection().

Under the default renderer (render cache mode 3) the number of a dimension
could not be picked at all, whatever lay under it: the two datum text
checks failed. Mode 0 passed all five.

Each unittest case is written as a PASS/FAIL line; GT_RENDER_CACHE sets
the render cache mode for the run (default: the view's own, 3).
"""
import os
import traceback
import unittest

import FreeCAD
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


class Result(unittest.TextTestResult):
    def addSuccess(self, test):
        super().addSuccess(test)
        note("PASS " + test.id())

    def addFailure(self, test, err):
        super().addFailure(test, err)
        note("FAIL " + test.id() + " | " + str(err[1])[:2000])

    def addError(self, test, err):
        super().addError(test, err)
        note("FAIL " + test.id() + " | " + "".join(traceback.format_exception(*err))[-2000:])

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        note("FAIL skipped " + test.id() + " | " + reason)


def run():
    try:
        mode = os.environ.get("GT_RENDER_CACHE")
        if mode is not None:
            FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
                "RenderCache", int(mode))
        from SketcherTests import TestConstraintPreselectionGui

        suite = unittest.defaultTestLoader.loadTestsFromModule(TestConstraintPreselectionGui)
        with open(os.path.join(OUT, "unittest.txt"), "w") as stream:
            result = unittest.TextTestRunner(
                stream=stream, resultclass=Result, verbosity=2).run(suite)
        if result.testsRun == 0:
            note("FAIL no test ran")
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
