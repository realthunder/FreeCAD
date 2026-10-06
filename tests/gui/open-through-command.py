"""A file opened through a command arrives whole.

File > Open and the recent files list are commands, and a command runs
inside App::Document::UserEditGuard: what it may not do is change a
document that is still filling itself in. A load marks its document as
filling (Gui::Application::refreshLiveLoad) so that a command clicked
while it runs is refused -- and the load a command started ran under
that command's own guard. Every object of the file was refused, "Cannot
create object ... still being filled in", once per object, and the
document came up empty: nothing in the tree, nothing in the view. Opening
from a script, a command line argument or a drop was not affected, which
is every way a test opened a file.

Claims, on a file of three objects saved by this script:

  - opened from Python, it has its three objects (the instrument);
  - opened through Std_RecentFiles, it has its three objects, and the
    shape of one of them;
  - the report view has no "Cannot create object";
  - once the load is over the document is not live any more, and a
    command changes it.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def wait_loaded(name, timeout=60.0):
    """Until the document is there and no longer filling."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        settle(0.2)
        doc = FreeCAD.listDocuments().get(name)
        if doc is not None and not doc.Restoring and not doc.LiveImport:
            return doc
    return FreeCAD.listDocuments().get(name)


class Report:
    """The console's lines, as FreeCAD.Console.AttachObserver delivers them."""

    def __init__(self):
        self.lines = []

    def __call__(self, notifier, msg, level):
        self.lines.append(msg)


def run():
    try:
        path = os.path.join(OUT, "ThreeObjects.FCStd").replace("\\", "/")
        doc = FreeCAD.newDocument("ThreeObjects")
        box = doc.addObject("Part::Box", "Box")
        doc.addObject("Part::Cylinder", "Cylinder")
        doc.addObject("App::DocumentObjectGroup", "Group").addObject(box)
        doc.recompute()
        volume = box.Shape.Volume
        doc.saveAs(path)
        FreeCAD.closeDocument("ThreeObjects")
        settle(0.5)

        doc = FreeCAD.openDocument(path)
        doc = wait_loaded("ThreeObjects")
        if not check("opened from Python, it has its three objects",
                     doc is not None and len(doc.Objects) == 3,
                     doc and len(doc.Objects)):
            raise RuntimeError("the file does not open at all")
        FreeCAD.closeDocument("ThreeObjects")
        settle(0.5)

        # The list follows its preference group when the count changes.
        recent = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/RecentFiles")
        recent.SetString("MRU0", path)
        recent.SetInt("RecentFiles", recent.GetInt("RecentFiles", 4) + 1)
        settle(0.5)

        report = Report()
        FreeCAD.Console.AttachObserver(report)
        FreeCADGui.runCommand("Std_RecentFiles", 0)
        doc = wait_loaded("ThreeObjects")
        if doc is None:
            raise RuntimeError("Std_RecentFiles opened nothing; is the list set?")
        check("opened through Std_RecentFiles, it has its three objects",
              len(doc.Objects) == 3, len(doc.Objects))
        loaded = doc.getObject("Box")
        check("and the shape of one of them",
              loaded is not None and abs(loaded.Shape.Volume - volume) < 1e-6,
              loaded and loaded.Shape.Volume)
        FreeCAD.Console.DetachObserver(report)
        refused = [line for line in report.lines if "Cannot create object" in line]
        check("the report view has no 'Cannot create object'", not refused, len(refused))
        check("once the load is over the document is not live any more",
              not doc.LiveImport and not doc.Restoring)
        before = len(doc.Objects)
        FreeCADGui.runCommand("Std_Part")
        settle(0.5)
        check("and a command changes it", len(doc.Objects) > before, len(doc.Objects))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
