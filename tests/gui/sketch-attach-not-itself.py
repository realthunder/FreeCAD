"""Attach Sketch does not offer the selected sketch as the one to attach
(upstream 93173ba797, issue 17629).

The command attaches a sketch, chosen from a list, to whatever is
selected. The list held every sketch of the document, the selected ones
too, so a sketch could be attached to itself: the circular-dependency
check that follows looks for the chosen sketch in what the selection
depends on, and a sketch is not in its own out-list.

Claims, read from the dialogs the command opens (each is cancelled):
  - with one of two sketches selected, the list offers only the other,
    and says some are left out;
  - with the only sketch selected there is no list at all;
  - with a box face selected the list offers both sketches, as before.

Scored against the tree before the change: the first list offered both
sketches, and the second case opened a list offering the sketch itself.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "AttachNotItself"
V = FreeCAD.Vector


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


def run_command(select):
    """Run the command on a selection; cancel and record every modal
    dialog it opens. Returns a list of (kind, label, items)."""
    seen = []

    def poll():
        w = QtWidgets.QApplication.activeModalWidget()
        if w is None:
            return
        if isinstance(w, QtWidgets.QInputDialog):
            seen.append(("input", w.labelText(), list(w.comboBoxItems())))
        elif isinstance(w, QtWidgets.QMessageBox):
            seen.append(("message", w.text(), []))
        else:
            seen.append((w.metaObject().className(), "", []))
        w.reject()

    timer = QtCore.QTimer()
    timer.timeout.connect(poll)
    timer.start(100)
    try:
        FreeCADGui.Selection.clearSelection()
        for obj, sub in select:
            FreeCADGui.Selection.addSelection(obj, sub)
        settle()
        FreeCADGui.runCommand("Sketcher_MapSketch")
        settle()
    finally:
        timer.stop()
    return seen


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        s1 = doc.addObject("Sketcher::SketchObject", "First")
        s1.addGeometry(Part.LineSegment(V(0, 0, 0), V(5, 0, 0)))
        s2 = doc.addObject("Sketcher::SketchObject", "Second")
        s2.addGeometry(Part.LineSegment(V(0, 0, 0), V(0, 5, 0)))
        doc.recompute()
        settle()

        seen = run_command([(box, "Face6")])
        note("face selected: %s" % seen)
        check("a face selected: the list offers both sketches",
              len(seen) == 1 and seen[0][0] == "input"
              and sorted(seen[0][2]) == ["First", "Second"], seen)
        plain_label = seen[0][1] if seen else ""

        seen = run_command([(s1, "")])
        note("First selected: %s" % seen)
        check("a sketch selected: the list offers only the other one",
              len(seen) == 1 and seen[0][0] == "input"
              and seen[0][2] == ["Second"], seen)
        check("a sketch selected: the list says some are left out",
              len(seen) == 1 and seen[0][1] != plain_label, seen)

        doc.removeObject("Second")
        doc.recompute()
        settle()
        seen = run_command([(s1, "")])
        note("the only sketch selected: %s" % seen)
        check("the only sketch selected: no list is offered",
              not any(kind == "input" for kind, _l, _i in seen), seen)
        check("nothing was attached",
              s1.MapMode == "Deactivated" and not s1.AttachmentSupport,
              (s1.MapMode, s1.AttachmentSupport))
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
