"""A sketch edited without the Sketcher toolbar must not crash.

`Sketcher_Grid`, `Sketcher_Snap` and `Sketcher_RenderingOrder` each swap
their toolbar icon to show their state, and each reaches for that icon
from `isActive()` -- which the command framework calls on every command
update, for every registered command, whether or not anything ever put
that command on a toolbar or in a menu.

`Gui::Command::getAction()` is documented to return null when no action
exists, and it does: entering sketch edit mode normally switches to the
Sketcher workbench, and it is building THAT workbench's toolbars which
creates the actions. Clear the sketch's "Editing workbench" preference
-- a supported setting -- and nothing builds them, so `isActive()` runs
with no action at all.

`CmdSketcherSnap::updateIcon` dereferenced it unguarded:

    #0  libc
    #1  Gui::Action::setIcon(QIcon const&)
    #2  CmdSketcherSnap::isActive()

Its two siblings in the same file were already guarded, which is what
made it an oversight rather than a design. Upstream's `a479197f0b` and
`c828c5d1d1` reach the same place by deleting the icon-swapping outright;
this fork keeps the state icons and keeps the guard.

The test asserts the invariant for all three, since all three have the
same shape and only one of them was wrong.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt. A crash here
shows up as a missing DONE, which is a failure.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchToolbarNoAction"

# All three swap an icon from isActive(); all three must tolerate no action.
COMMANDS = ["Sketcher_Grid", "Sketcher_Snap", "Sketcher_RenderingOrder"]


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def run():
    try:
        import Part
        import SketcherGui  # noqa: F401  -- registers the commands

        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0),
                                        FreeCAD.Vector(5, 5, 0)), False)
        doc.recompute()

        # Empty: edit mode then does NOT activate the Sketcher workbench, so
        # nothing builds its toolbars and none of the commands gets an action.
        sk.ViewObject.EditingWorkbench = ""
        before = FreeCADGui.activeWorkbench().name()
        check("the Sketcher workbench is not the active one", before != "SketcherWorkbench",
              before)

        FreeCADGui.ActiveDocument.setEdit(sk, 0)
        check("the sketch is in edit mode",
              FreeCADGui.ActiveDocument.getInEdit() is not None)
        after = FreeCADGui.activeWorkbench().name()
        check("and edit mode did not switch workbench", after == before, after)

        for name in COMMANDS:
            cmd = FreeCADGui.Command.get(name)
            if not check("%s is registered" % name, cmd is not None):
                continue
            # The crash was here: isActive() -> updateIcon() -> getAction()
            # -> null -> setIcon. Reaching the next line at all is the test.
            active = cmd.isActive()
            check("%s.isActive() survives with no action" % name, True, active)

        FreeCADGui.ActiveDocument.resetEdit()
        check("edit mode is left cleanly",
              FreeCADGui.ActiveDocument.getInEdit() is None)
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
