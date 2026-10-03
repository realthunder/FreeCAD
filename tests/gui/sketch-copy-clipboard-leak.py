"""Copying sketch elements to the clipboard gives back what it allocates.

Copy (and Cut) write the selected geometry and its constraints out as Python
text. To do that the command cloned every selected geometry and every
constraint among them -- and never deleted a clone (upstream `5268aa43db`,
in the form of `fe8d2845ea`). Each Copy of a selection left a copy of it on
the heap for the life of the process.

Measured as the growth of the process's resident memory over repeated copies
of one large selection: 3000 lines, each with a constraint. The clipboard
text itself is replaced by every copy and does not accumulate.

Claims:

  - the copy puts the sketch's text on the clipboard (the control: the
    command ran);
  - after a warm-up, 30 more copies grow the process by less than 16 MB.

Scored against the tree before the change: see the commit message.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchCopyClipboardLeak"
OBJ = "Sketch"
V = FreeCAD.Vector
LINES = 3000
WARMUP = 5
COPIES = 30
LIMIT_MB = 16.0


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def resident_mb():
    with open("/proc/self/statm") as f:
        pages = int(f.read().split()[1])
    return pages * os.sysconf("SC_PAGE_SIZE") / (1024.0 * 1024.0)


def run():
    try:
        import Part
        import Sketcher
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry([Part.LineSegment(V(0, i, 0), V(10, i, 0)) for i in range(LINES)], False)
        sk.addConstraint([Sketcher.Constraint("Horizontal", i) for i in range(LINES)])
        doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(sk, 0)
        settle()
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(sk, ["Edge%d" % (i + 1) for i in range(LINES)])
        settle()

        clipboard = QtGui.QGuiApplication.clipboard()
        clipboard.setText("")
        FreeCADGui.runCommand("Sketcher_CopyClipboard", 0)
        settle()
        text = clipboard.text()
        check("the copy puts the sketch's text on the clipboard",
              text.startswith("# Copied from sketcher.") and text.count("LineSegment") >= LINES,
              (len(text), text.count("LineSegment")))

        for _ in range(WARMUP):
            FreeCADGui.runCommand("Sketcher_CopyClipboard", 0)
        settle()
        before = resident_mb()
        for _ in range(COPIES):
            FreeCADGui.runCommand("Sketcher_CopyClipboard", 0)
        settle()
        grown = resident_mb() - before
        check("%d more copies grow the process by less than %g MB" % (COPIES, LIMIT_MB),
              grown < LIMIT_MB, "%.1f MB" % grown)

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.ActiveDocument.resetEdit()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
