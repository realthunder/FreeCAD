"""A Draft command's task panel belongs to the view the command was run in.

Draft shows its panels from its todo queue, a zero timer after the command
returns. Nothing is being handled when the queue runs, so a show that
names no view takes whichever view is active by then
(docs/TaskPanelPerView.md sec 3, the deferred show). Draft takes
Gui.Control.currentOwner() when it queues the show and passes it.

One document with two 3D views, a1 and a2. A Draft command is run with a1
active and a2 is activated before control returns to the event loop:

  - Draft_SelectPlane's panel (gui_selectplane.py) is a1's;
  - Draft_Line's panel (DraftGui.py, taskUi) is a1's.

Not covered: gui_scale.py's second panel, which needs two picked points.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the Draft files before the change, on the same binaries:
both panels were a2's.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
Control = FreeCADGui.Control
state = {"views": {}, "done": False}
steps = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))


def settle(ms=200):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def activate(view):
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle(100)


def which(view):
    for name, known in state["views"].items():
        if view is not None and (view is known or view == known):
            return name
    return "none" if view is None else "unknown"


def owner():
    dlg = Control.activeTaskDialog()
    if dlg is None:
        return ("no dialog", "")
    return (dlg.getOwnerKind(), which(dlg.getAssociatedView()))


def step(fn):
    steps.append(fn)
    return fn


def end_draft():
    cmd = getattr(FreeCAD, "activeDraftCommand", None)
    if cmd is not None:
        cmd.finish()
    if Control.activeDialog():
        Control.closeDialog()


@step
def build():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    doc = FreeCAD.newDocument("DraftOwner")
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle()
    v = FreeCADGui.getDocument("DraftOwner").mdiViewsOfType("Gui::View3DInventor")
    activate(v[0])
    FreeCADGui.runCommand("Std_ViewCreate")
    settle()
    import DraftTools  # noqa: F401  registers the Draft commands


@step
def select_plane():
    v = FreeCADGui.getDocument("DraftOwner").mdiViewsOfType("Gui::View3DInventor")
    state["views"] = {"a1": v[0], "a2": v[1]}
    FreeCADGui.Selection.clearSelection()
    activate(v[0])
    FreeCADGui.runCommand("Draft_SelectPlane")
    # The panel is shown from Draft's queue; by then a2 is the active view
    FreeCADGui.getMainWindow().setActiveWindow(v[1])


@step
def select_plane_shown():
    check(
        "Draft_SelectPlane run in a1, a2 active when its queue runs: the panel is a1's",
        owner() == ("view", "a1"),
        owner(),
    )
    end_draft()


@step
def line():
    activate(state["views"]["a1"])
    FreeCADGui.runCommand("Draft_Line")
    FreeCADGui.getMainWindow().setActiveWindow(state["views"]["a2"])


@step
def line_shown():
    check(
        "Draft_Line run in a1, a2 active when its queue runs: the panel is a1's",
        owner() == ("view", "a1"),
        owner(),
    )
    end_draft()


def advance():
    if not steps:
        finish()
        return
    fn = steps.pop(0)
    try:
        fn()
    except Exception:
        note("ABORT step %s:\n%s" % (fn.__name__, traceback.format_exc()))
        finish()
        return
    QtCore.QTimer.singleShot(700, advance)


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        end_draft()
    except Exception:
        note("cleanup: " + traceback.format_exc())
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, advance)
