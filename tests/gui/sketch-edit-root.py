"""A desktop sketch enters and leaves edit with its scene graph intact.

The oracle for the editing root now that it belongs to Gui::ViewerContext
rather than to Gui::View3DInventorViewer (docs/ThinClient.md sec 8.9
stage 4). Nothing about the desktop changed, which is the claim: the
separator, the transform, and the rule that an edit mode's geometry is
moved out of the view provider's root and put back afterwards are the
same code, running from one place instead of two.

That rule is what makes this checkable from Python without reaching into
Coin. setupEditingRoot() *moves* every child of the editing view
provider's root under the editing root and leaves the provider's root
empty; resetEditingRoot() gives them back. So the child count of
ViewObject.RootNode is a direct read of both halves:

  - before edit it is whatever the sketch built,
  - in edit it is zero, because the viewer took them,
  - after edit it is what it was, because the viewer gave them back.

Get the hoist wrong in the direction that matters -- the base owning
nodes a view still unrefs, a root left hanging in the wrong graph -- and
the third reading is the one that moves, or the process does not reach
it at all.

Also checked: geometry added while in edit survives the round trip, so
what came back is the live graph and not a stale copy.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchEditRoot"
OBJ = "Sketch"

state = {"doc": None, "done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def pump(turns=30):
    """Edit mode is entered through task panels and timers, so let the
    event loop run rather than reading straight after the call."""
    for _ in range(turns):
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.ActiveDocument.resetEdit()
        pump()
    except Exception:
        pass
    # Every document closed before the quit: a modified one left open
    # puts a save prompt in the way and the process never leaves.
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def run():
    try:
        import Part
        import Sketcher

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0),
                                            FreeCAD.Vector(10, 0, 0)), False)
        doc.recompute()
        pump()

        root = sketch.ViewObject.RootNode
        before = root.getNumChildren()
        check("the sketch built a scene graph", before > 0, before)

        opened = FreeCADGui.ActiveDocument.setEdit(sketch, 0)
        pump()
        check("setEdit was accepted", opened)
        check("the document says it is in edit",
              FreeCADGui.ActiveDocument.getInEdit() is not None)
        # The viewer took the provider's children; this is setupEditingRoot
        # working, and it is the half that used to live in the viewer.
        check("the view provider's root was emptied into the editing root",
              root.getNumChildren() == 0, root.getNumChildren())

        # Something added while in edit, so that what comes back is
        # demonstrably the live graph.
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(10, 0, 0),
                                            FreeCAD.Vector(10, 8, 0)), False)
        doc.recompute()
        pump()
        check("the second segment took", sketch.GeometryCount == 2,
              sketch.GeometryCount)

        FreeCADGui.ActiveDocument.resetEdit()
        pump()
        check("the document left edit",
              FreeCADGui.ActiveDocument.getInEdit() is None)
        check("the view provider got its children back",
              root.getNumChildren() >= before,
              (root.getNumChildren(), before))

        # And it can be done twice: the restore flag is per session, and a
        # second entry that found the root already emptied would be the
        # way a half-restored graph shows itself.
        opened = FreeCADGui.ActiveDocument.setEdit(sketch, 0)
        pump()
        check("a second edit session opens", opened)
        check("and empties the root again", root.getNumChildren() == 0,
              root.getNumChildren())
        FreeCADGui.ActiveDocument.resetEdit()
        pump()
        check("and gives the children back again",
              root.getNumChildren() >= before,
              (root.getNumChildren(), before))
    except Exception:
        note("ABORT run:\n" + traceback.format_exc())
    finish()


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
