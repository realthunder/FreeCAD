"""The overlay the Tasks are in has the "show this task panel in its view"
button on its title bar.

docs/HandsOnQueue.md entry 103: "the switch panel to view button on task
panel no longer visible, even in editing. I tested edit pad" -- "seems so, as
I haven't seen the button in any case", the Tasks panel in an overlay.

The button was on the title bar of the dock that holds the task view, and an
overlaid dock has no title bar of its own: the overlay's is shared by the
docks in it and did not carry the button. So with the Tasks overlaid there
was no such button anywhere. Each overlay's title bar has one now, shown
while the Tasks are in that overlay and show a panel that has a view.

Claims, every dock overlaid (by this script, or already at the start when it
is run with a configuration that has them so):
  - with nothing in edit no overlay shows the button;
  - with a Pad in edit, the title bar of the overlay that holds the Tasks
    has the button, shown -- and no other overlay shows one;
  - pressing it puts the panel into the view (a task panel host shown).
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
STEPS = []
V = FreeCAD.Vector
STATE = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def cls(widget):
    return widget.metaObject().className()


def all_overlays():
    out = []
    for widget in QtWidgets.QApplication.allWidgets():
        try:
            if cls(widget) == "Gui::OverlayTabWidget":
                out.append(widget)
        except RuntimeError:
            pass
    return out


def tasks_overlay():
    """The overlay tab widget that holds the Tasks dock, or None."""
    for widget in all_overlays():
        for i in range(widget.count()):
            if widget.tabText(i).replace("&", "").startswith(("Task", "Combo")):
                return widget
    return None


def host_buttons(tab):
    return [b for b in tab.findChildren(QtWidgets.QAbstractButton)
            if cls(b) == "Gui::OverlayToolButton" and b.objectName().endswith("TaskHost")]


def shown_in(tab):
    return [b for b in host_buttons(tab) if not b.isHidden()]


def start():
    tab = tasks_overlay()
    note("the Tasks dock in an overlay at the start: %s" % (tab is not None))
    if tab is None:
        FreeCADGui.runCommand("Std_DockOverlayAll")
        settle(2000)
        tab = tasks_overlay()
    if not check("the Tasks dock is in an overlay", tab is not None):
        STEPS[:] = []
        return
    doc = FreeCAD.newDocument("TaskTitle")
    body = doc.addObject("PartDesign::Body", "Body")
    sketch = body.newObject("Sketcher::SketchObject", "Sketch")
    corners = [V(0, 0, 0), V(10, 0, 0), V(10, 10, 0), V(0, 10, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]), False)
    for i in range(4):
        sketch.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
    doc.recompute()
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sketch
    pad.Length = 5
    doc.recompute()
    FreeCADGui.SendMsgToActiveView("ViewFit")
    settle(800)
    shown = [w.objectName() for w in all_overlays() if shown_in(w)]
    check("with nothing in edit no overlay shows the button", not shown, shown)
    FreeCADGui.getDocument(doc.Name).setEdit(pad.Name)


def look():
    settle(2000)
    tab = tasks_overlay()
    buttons = host_buttons(tab) if tab else []
    if not check("with a Pad in edit the Tasks overlay has the show-in-view button",
                 bool(buttons), len(buttons)):
        return
    shown = shown_in(tab)
    if not check("the button is shown", bool(shown),
                 [(b.isHidden(), b.isEnabled()) for b in buttons]):
        return
    STATE["button"] = shown[0]
    others = [w.objectName() for w in all_overlays() if w is not tab and shown_in(w)]
    check("no other overlay shows one", not others, others)
    FreeCADGui.getMainWindow().grab().save(os.path.join(OUT, "in-edit.png"))


def press():
    if STATE.get("button") is None:
        return
    # found again: an overlay makes its title bar anew when its state changes
    tab = tasks_overlay()
    shown = shown_in(tab) if tab else []
    if not check("the button is still there to press", bool(shown)):
        return
    shown[0].click()
    settle(1500)
    hosts = [w for w in QtWidgets.QApplication.allWidgets()
             if cls(w) == "Gui::TaskView::TaskPanelHost" and not w.isHidden()]
    check("pressing it shows the panel in the view", bool(hosts), len(hosts))
    FreeCADGui.getMainWindow().grab().save(os.path.join(OUT, "in-view.png"))


def finish():
    try:
        FreeCADGui.Control.closeDialog()
    except Exception:
        pass
    try:
        gdoc = FreeCADGui.getDocument("TaskTitle")
        if gdoc is not None:
            gdoc.resetEdit()
    except Exception:
        pass
    settle(500)
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    step = STEPS.pop(0)
    try:
        step()
    except Exception:
        note("ABORT in %s: %s" % (step.__name__, traceback.format_exc().replace("\n", " | ")))
        STEPS[:] = []
    QtCore.QTimer.singleShot(800, advance)


STEPS[:] = [start, look, press]
QtCore.QTimer.singleShot(2500, advance)
