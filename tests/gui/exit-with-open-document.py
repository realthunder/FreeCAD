"""The application left with a document still open.

QCoreApplication.exit ends the event loop without asking the main window
to close, so nothing has closed the documents when the loop returns. The
main window was then destroyed with their views in it, the last view of a
document closed that document from its destructor, and closing a document
asked the main window that was being destroyed for its active view: a
segmentation fault on the way out (docs/DocumentLoad.md sec 18.8). A
script that ends that way, or a test, died after its work was done.

What is done: a document with a box in it, a second one with a
spreadsheet open in its view beside the 3D view, and then
QCoreApplication.exit(0) with both open and one of them modified.

What is asserted is the exit itself: this script writes DONE before it
asks for the exit, and scripts/gui-test.sh fails a test whose process
wrote DONE and then left with a status other than 0.

Scored against the tree before the change (5ae3607abd): three runs of
three wrote DONE and exited with status 1.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        first = FreeCAD.newDocument("First")
        first.addObject("Part::Box", "Box")
        first.recompute()
        second = FreeCAD.newDocument("Second")
        second.addObject("Part::Cylinder", "Cylinder")
        second.addObject("Spreadsheet::Sheet", "Sheet").set("A1", "1")
        second.recompute()
        FreeCADGui.getDocument("Second").getObject("Sheet").doubleClicked()
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
    QtCore.QTimer.singleShot(1500, leave)


def leave():
    open_now = len(FreeCAD.listDocuments())
    modified = [name for name in FreeCAD.listDocuments() if FreeCADGui.getDocument(name).Modified]
    note(("PASS " if open_now == 2 else "FAIL ")
         + "two documents are open at the exit, and modified | %d open, modified %s"
         % (open_now, modified))
    # The verdict from here on is the status the process leaves with
    note("DONE")
    QtCore.QCoreApplication.exit(0)


QtCore.QTimer.singleShot(1500, build)
