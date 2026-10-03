"""Selecting in a sketch re-edited within one event-loop turn.

When a sketch edit ends, its elements panel lets go of the sketch
(`TaskSketcherElements::sketchClosed()`): it clears its tree widget, which
deletes every item. But the panel is only deleted later, when control
returns to the event loop, and until then it still observes the selection.
It kept `itemMap` -- geometry index to tree item -- pointing at the deleted
items. A script that ends an edit and starts one on the same sketch before
returning to the event loop, then selects an edge, sent the old panel an
AddSelection for its own sketch; it looked the edge up in `itemMap` and
called `setSelected()` on a deleted item. SIGSEGV.

Scored against the tree before the fix: the process dies at the selection,
so the result stops at "re-edited" and never reaches DONE.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))


def run():
    try:
        import Part
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        doc = FreeCAD.newDocument("ReEdit")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0),
                                        FreeCAD.Vector(10, 0, 0)), False)
        doc.recompute()
        gdoc = FreeCADGui.getDocument(doc.Name)
        # All in this one callback: the first edit's panel is not deleted
        # until the event loop runs again.
        gdoc.setEdit(sk)
        QtCore.QCoreApplication.processEvents()
        gdoc.resetEdit()
        QtCore.QCoreApplication.processEvents()
        gdoc.setEdit(sk)
        QtCore.QCoreApplication.processEvents()
        note("re-edited")
        FreeCADGui.Selection.addSelection(sk, "Edge1")
        QtCore.QCoreApplication.processEvents()
        ex = [e for e in FreeCADGui.Selection.getSelectionEx("*") if e.Object == sk]
        subs = ex[0].SubElementNames if ex else ()
        check("an edge selected in the re-edited sketch is selected",
              tuple(subs) == ("Edge1",), subs)
        FreeCADGui.Selection.clearSelection()
        gdoc.resetEdit()
        QtCore.QCoreApplication.processEvents()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
