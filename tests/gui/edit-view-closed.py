"""Closing the view an edit runs in leaves the edit.

docs/TaskPanelPerView.md sec 13.4. A document's edit session is bound to
the view it was started in. Closing that view used to leave the session
bound to a viewer that no longer existed; since a view in an edit has a
selection instance of its own (sec 12), which goes with the view and tells
its observers to read the selection again, that was a segmentation fault
in the sketch's own observer.

The edit is LEFT, as it is when its document is closed: what was done in
it is kept. It is not cancelled.

One document, a body with a sketch and an object whose edit opens no task
dialog, two 3D views. For each of PerViewEdit on and off:

  - a sketch is entered in a1 and a line is added; a1 is closed while a2
    is the active view: the process is still there, nothing is in edit,
    the line is kept, no task dialog is left, one view remains;
  - the same for the object whose edit has no dialog;
  - closing a view that is NOT the edit's leaves the edit running.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (c108db0c04), where the first
close ends the process with SIGSEGV.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "EditViewClosed"
V = FreeCAD.Vector
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
Control = FreeCADGui.Control

state = {"done": False, "views": {}, "lines": 4}
steps = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=200):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def mw():
    return FreeCADGui.getMainWindow()


def views3d():
    return FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")


def two_views():
    """Give the document its two views, and name them."""
    v = views3d()
    if len(v) < 2:
        mw().setActiveWindow(v[0])
        settle(200)
        FreeCADGui.runCommand("Std_ViewCreate")
        settle(500)
        v = views3d()
    state["views"] = {"a1": v[0], "a2": v[1]}


def activate(name):
    mw().setActiveWindow(state["views"][name])
    settle(300)


def close_view(name):
    """Close a view as its user does: its own widget."""
    w = state["views"][name].graphicsView()
    while w is not None and w.metaObject().className() != "Gui::View3DInventor":
        w = w.parentWidget()
    w.close()


def sketch():
    return FreeCAD.getDocument(DOC).Sketch


class QuietEdit:
    """A view provider whose edit opens no task dialog."""

    def __init__(self, vobj):
        vobj.Proxy = self

    def attach(self, vobj):
        pass

    def setEdit(self, vobj, mode=0):
        return True

    def unsetEdit(self, vobj, mode=0):
        return True

    def __getstate__(self):
        return None

    def __setstate__(self, state):
        return None


def step(fn):
    steps.append(fn)
    return fn


@step
def build():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    VIEW.SetInt("RenderCache", 3)
    VIEW.SetBool("ShowNaviCube", False)
    VIEW.SetBool("UseNavigationAnimations", False)
    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    body = doc.addObject("PartDesign::Body", "Body")
    sk = body.newObject("Sketcher::SketchObject", "Sketch")
    sk.Support = (doc.getObject("XY_Plane"), [""])
    sk.MapMode = "FlatFace"
    corners = [V(0, 0, 0), V(10, 0, 0), V(10, 10, 0), V(0, 10, 0)]
    for i in range(4):
        sk.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
    quiet = doc.addObject("App::FeaturePython", "Quiet")
    QuietEdit(quiet.ViewObject)
    doc.recompute()
    settle(400)


def enter(per_view, obj_name):
    def fn():
        VIEW.SetBool("PerViewEdit", per_view)
        two_views()
        activate("a1")
        FreeCADGui.getDocument(DOC).setEdit(FreeCAD.getDocument(DOC).getObject(obj_name), 0)

    fn.__name__ = "enter_%s_%s" % (obj_name, "own" if per_view else "shared")
    return fn


def close_edit_view(per_view, obj_name):
    tag = "%s, PerViewEdit %s" % (obj_name, "on" if per_view else "off")

    def fn():
        activate("a1")
        check("%s: the edit was entered" % tag, FreeCADGui.editDocument() is not None)
        if obj_name == "Sketch":
            doc = FreeCAD.getDocument(DOC)
            doc.openTransaction("a line in the edit")
            sketch().addGeometry(Part.LineSegment(V(2, 2, 0), V(8, 8, 0)))
            doc.commitTransaction()
            state["lines"] += 1
            settle(200)
        activate("a2")
        close_view("a1")

    fn.__name__ = "close_%s_%s" % (obj_name, "own" if per_view else "shared")
    return fn


def closed(per_view, obj_name):
    tag = "%s, PerViewEdit %s" % (obj_name, "on" if per_view else "off")

    def fn():
        check("%s: the edit's view is closed and the process is here" % tag, len(views3d()) == 1,
              len(views3d()))
        check("%s: nothing is in edit" % tag, FreeCADGui.editDocument() is None)
        check("%s: no task dialog is left" % tag, not Control.activeDialog())
        if obj_name == "Sketch":
            got = len(sketch().Geometry)
            check("%s: the edit was left, not cancelled" % tag, got == state["lines"],
                  "%d lines, %d expected" % (got, state["lines"]))

    fn.__name__ = "closed_%s_%s" % (obj_name, "own" if per_view else "shared")
    return fn


for per_view in (True, False):
    for name in ("Sketch", "Quiet"):
        steps.append(enter(per_view, name))
        steps.append(close_edit_view(per_view, name))
        steps.append(closed(per_view, name))


@step
def another_view():
    VIEW.SetBool("PerViewEdit", True)
    two_views()
    activate("a1")
    FreeCADGui.getDocument(DOC).setEdit(sketch(), 0)


@step
def close_another_view():
    activate("a1")
    close_view("a2")


@step
def another_view_closed():
    check("closing a view that is not the edit's leaves one view", len(views3d()) == 1,
          len(views3d()))
    check("and the edit running", FreeCADGui.editDocument() is not None)
    check("with its task dialog", bool(Control.activeDialog()))
    FreeCADGui.getDocument(DOC).resetEdit()


def advance():
    if state["done"]:
        return
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
    QtCore.QTimer.singleShot(600, advance)


def finish():
    if state["done"]:
        return
    state["done"] = True
    VIEW.SetBool("PerViewEdit", False)
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCADGui.getDocument(name).resetEdit()
        if Control.activeDialog():
            Control.closeDialog()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, advance)
