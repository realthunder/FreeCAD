"""A sketch holding only external geometry is not reported empty
(upstream 5961651547).

The solver messages called a sketch "Empty sketch" whenever it had no
geometry of its own, so a sketch that only projects an edge -- where the
user is about to constrain to it -- said it was empty. External geometry
always holds the two axes; anything past them counts now.

Measured here, in the task panel's status label: an empty sketch says
"Empty sketch"; one holding a single external edge does not. Before the
change the second said "Empty sketch" too.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def status_text():
    labels = [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QLabel)
              if w.objectName() == "labelStatus" and w.isVisible()]
    return labels[0].text() if labels else None


def run():
    try:
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchEmptyMessage")
        doc.addObject("Part::Box", "Box")
        # the box needs its shape before an edge of it can be referenced
        doc.recompute()
        state["empty"] = doc.addObject("Sketcher::SketchObject", "Empty")
        ext = doc.addObject("Sketcher::SketchObject", "External")
        ext.addExternal("Box", "Edge2")
        doc.recompute()
        state["ext"] = ext
        FreeCADGui.activeDocument().setEdit(state["empty"])
        QtCore.QTimer.singleShot(1500, empty_read)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def empty_read():
    try:
        text = status_text()
        check("an empty sketch is reported empty", text is not None and "Empty" in text, text)
        FreeCADGui.activeDocument().resetEdit()
        check("the other holds an external edge past the axes",
              len(state["ext"].ExternalGeo) > 2, len(state["ext"].ExternalGeo))
        FreeCADGui.activeDocument().setEdit(state["ext"])
        QtCore.QTimer.singleShot(1500, ext_read)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def ext_read():
    try:
        text = status_text()
        check("a sketch holding only external geometry is not",
              text is not None and "Empty" not in text, text)
        FreeCADGui.activeDocument().resetEdit()
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
