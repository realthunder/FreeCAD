"""A new sketch made with a group selected goes into the group (upstream
17c3286e52, widened to an App::Part).

New Sketch asks the attacher what the selection can carry. A group
carries nothing, so the command said it could not map the sketch and
made none. Upstream takes a selected plain group as the place for the
sketch; here an App::Part counts as well, being a group in the same
sense. A body does not: PartDesign has its own command for that.

Claims, for a plain group and for an App::Part, each selected whole:
  - the orientation dialog is offered (accepted here), as with nothing
    selected;
  - one sketch is made, it is in the group, and it is being edited.

Scored against the tree before the change: no dialog, no sketch.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "NewInGroup"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=20):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def run_command(obj):
    """Run New Sketch with `obj` selected; accept the orientation dialog,
    cancel any other. Returns the class names of the dialogs seen."""
    seen = []

    def poll():
        w = QtWidgets.QApplication.activeModalWidget()
        if w is None:
            return
        cls = w.metaObject().className()
        seen.append(cls)
        if "Orientation" in cls:
            w.accept()
        else:
            w.reject()

    timer = QtCore.QTimer()
    timer.timeout.connect(poll)
    timer.start(100)
    try:
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(obj)
        settle()
        FreeCADGui.runCommand("Sketcher_NewSketch")
        settle()
    finally:
        timer.stop()
    return seen


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        for kind in ("App::DocumentObjectGroup", "App::Part"):
            grp = doc.addObject(kind, "Holder")
            doc.recompute()
            settle()
            before = set(o.Name for o in doc.Objects)
            seen = run_command(grp)
            new = [o for o in doc.Objects if o.Name not in before]
            note("%s: dialogs=%s new=%s group=%s" % (
                kind, seen, [o.Name for o in new], [o.Name for o in grp.Group]))
            check("%s: the orientation dialog is offered" % kind,
                  len(seen) == 1 and "Orientation" in seen[0], seen)
            check("%s: one sketch is made" % kind,
                  len(new) == 1 and new[0].isDerivedFrom("Sketcher::SketchObject"),
                  [o.Name for o in new])
            check("%s: it is in the group" % kind,
                  len(new) == 1 and new[0] in grp.Group,
                  [o.Name for o in grp.Group])
            check("%s: it is being edited" % kind,
                  FreeCADGui.ActiveDocument.getInEdit() is not None)
            FreeCADGui.ActiveDocument.resetEdit()
            settle()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
