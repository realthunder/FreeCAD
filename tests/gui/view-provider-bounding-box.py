"""ViewObject.getBoundingBox takes each of its five arguments.

getBoundingBox(subname=None, transform=True, view=None, mat=None, depth=0):
the parse handed PyArg_ParseTupleAndKeywords one pointer too many, a second
&subname ahead of the matrix type, so everything from `mat` on landed one
slot off -- `mat` was type-checked against the address of a char pointer
and stored into the Matrix type object, and `depth` was written into the
matrix pointer, which was then read as a matrix.

A 10 mm box at x = 10.

Claims:
  - no argument, and each keyword at its default, give the same box;
  - mat moves the box by the matrix;
  - depth alone is accepted and changes nothing;
  - all five given by position work as by keyword;
  - something that is not a matrix is a TypeError, not a crash.

Scored against the tree before the fix: depth=0 passes only because the
zero it wrote left the matrix pointer null, and any mat is refused with a
UnicodeDecodeError raised from the name of a type that is not one.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ViewProviderBoundingBox"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def span(box):
    return tuple(round(v, 3) for v in (box.XMin, box.YMin, box.ZMin,
                                       box.XMax, box.YMax, box.ZMax))


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        box.Placement.Base = FreeCAD.Vector(10, 0, 0)
        doc.recompute()
        settle()
        view = FreeCADGui.getDocument(DOC).ActiveView
        vp = box.ViewObject

        plain = span(vp.getBoundingBox())
        check("the box is where the object is",
              plain == (10, 0, 0, 20, 10, 10), plain)

        got = span(vp.getBoundingBox(view=view))
        check("view alone changes nothing", got == plain, got)

        # written before each call that used to kill the process, so the
        # result file names the call the run died in
        note("CALL depth=0")
        got = span(vp.getBoundingBox(depth=0))
        check("depth alone changes nothing", got == plain, got)

        # a depth that is not zero was read back as the matrix pointer
        note("CALL depth=1")
        got = span(vp.getBoundingBox(depth=1))
        check("a depth that is not zero changes nothing", got == plain, got)

        note("CALL mat=identity")
        got = span(vp.getBoundingBox(mat=FreeCAD.Matrix()))
        check("an identity mat changes nothing", got == plain, got)

        move = FreeCAD.Matrix()
        move.move(FreeCAD.Vector(100, 0, 0))
        moved = (110, 0, 0, 120, 10, 10)
        note("CALL mat=move")
        got = span(vp.getBoundingBox(mat=move))
        check("mat moves the box", got == moved, got)

        note("CALL every keyword")
        got = span(vp.getBoundingBox(subname="", transform=True, view=view,
                                     mat=move, depth=0))
        check("every keyword together", got == moved, got)

        note("CALL by position")
        got = span(vp.getBoundingBox("", True, view, move, 0))
        check("all five by position", got == moved, got)

        note("CALL mat=not a matrix")
        try:
            vp.getBoundingBox(mat=FreeCAD.Vector())
            check("something that is not a matrix is refused", False, "accepted")
        except TypeError as e:
            check("something that is not a matrix is refused", True, e)

        check("the Matrix type still makes a matrix",
              type(FreeCAD.Matrix()).__name__ == "Matrix",
              type(FreeCAD.Matrix()).__name__)
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
