"""A new sketch's vertex size is the shape point size preference (upstream
c14d6f8848, 0a45527b8b).

A sketch made its vertices 4 pixels whatever View/DefaultShapePointSize
said, while every other new shape takes that preference. Now a new sketch
takes it too, and 4 where it was never set. A sketch already made keeps
its own size, preference or not.

Measured here: with the preference at 6 a new sketch's PointSize is 6;
with the preference removed it is 4; the first sketch still says 6.
Before the change the first read 4.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = "User parameter:BaseApp/Preferences/View"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def run():
    try:
        grp = FreeCAD.ParamGet(VIEW)
        doc = FreeCAD.newDocument("SketchNewPointSize")
        grp.SetInt("DefaultShapePointSize", 6)
        first = doc.addObject("Sketcher::SketchObject", "First")
        size = first.ViewObject.PointSize
        check("a new sketch takes the shape point size preference", size == 6, size)
        grp.RemInt("DefaultShapePointSize")
        second = doc.addObject("Sketcher::SketchObject", "Second")
        size = second.ViewObject.PointSize
        check("and 4 where the preference is not set", size == 4, size)
        size = first.ViewObject.PointSize
        check("a sketch already made keeps its own", size == 6, size)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    FreeCAD.ParamGet(VIEW).RemInt("DefaultShapePointSize")
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
