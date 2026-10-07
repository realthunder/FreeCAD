"""Part's Boolean and geometry check options are listed, and a new Boolean still takes its refine switch.

The fifteen options of Check Geometry (Preferences/Mod/Part/CheckGeometry),
the three of Part's Booleans (Mod/Part/Boolean) and four single keys are
behind the two PartParams classes (docs/HandsOnQueue.md entry 24). They are
kept in sub-groups, under keys that are not their settings' names, which
the generator handles since 0a94fb63c9. Reading them for that found:

  - the "Single-threaded" box of the Check Geometry panel stores
    RunSingleThreaded, and the check read RunBOPCheckSingleThreaded, a key
    nothing writes: the box did nothing (from the code; nothing a script
    can see says how many threads the check ran in).

Claims:

  - "/param geometry check in a single thread" lists the option of the
    sub-group CheckGeometry, and "/param refine model after boolean" the
    one of the sub-group Boolean;
  - a Part Fuse made with the Boolean refine switch on has Refine on, one
    made without the key has it off.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PREFS = "User parameter:BaseApp/Preferences/"
Qt = QtCore.Qt


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def param_rows(query):
    FreeCADGui.runCommand("Std_OmniSearch")
    settle(0.6)
    edit = None
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == "OmniSearchEdit" and w.isVisible():
            edit = w
    if edit is None:
        return []
    text = "/param " + query
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(0.8)
    rows = []
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            rows += [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return rows


def run():
    boolean = FreeCAD.ParamGet(PREFS + "Mod/Part/Boolean")
    doc = None
    try:
        import Part  # noqa: F401  the modules register their settings when they are loaded
        import PartGui  # noqa: F401
        settle(0.5)

        rows = param_rows("geometry check in a single thread")
        check("the omni search lists the geometry check's single thread option",
              any("Mod/Part/CheckGeometry/" in r for r in rows), rows[:6])
        rows = param_rows("refine model after boolean")
        check("and the Boolean refine switch", any("Mod/Part/Boolean/" in r for r in rows), rows[:6])

        doc = FreeCAD.newDocument("Entry24Part")
        settle(0.5)
        boolean.SetBool("RefineModel", True)
        settle(0.2)
        on = doc.addObject("Part::Fuse", "FuseRefined").Refine
        boolean.RemBool("RefineModel")
        settle(0.2)
        off = doc.addObject("Part::Fuse", "FusePlain").Refine
        check("a Fuse made with the Boolean refine switch on has Refine on, one made without the key has it off",
              on is True and off is False, (on, off))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        boolean.RemBool("RefineModel")
        if doc is not None:
            FreeCAD.closeDocument(doc.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
