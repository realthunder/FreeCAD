"""A view that is editing selects into an instance of its own.

docs/TaskPanelPerView.md sec 12. A view in an edit, or owning a task
dialog, takes a selection instance of its own, started as a copy of the
shared one. What is picked in that view, what the tree and the commands do
while it is the active view, and the gate a dialog sets all land in that
instance; the other views of the main window go on sharing theirs. The
tree and the other panels show the selection of the ACTIVE view.

One document, two boxes (BoxA, BoxB) and a body with a pad, two 3D views
a1 and a2, render cache mode 3.

A task dialog shown for a1, BoxA selected beforehand:

  - a1 starts on a copy: BoxA is selected in it;
  - BoxB added while a1 is active is not selected when a2 is, and is
    again when a1 is;
  - a clear while a2 is active leaves a1's selection alone;
  - a gate set while a1 is active does not gate a2, and does gate a1;
  - a click on BoxB in a2 selects it for a2 and not for a1; a click on
    BoxA in a1 selects it for a1 and not for a2;
  - the tree shows the active view's selection, and a global Python
    observer is told of it when the active view changes;
  - BoxB selected in a1 alone is drawn selected in a1 and not in a2;
  - closing the dialog hands a1's selection back to the shared one.

And an edit of the pad in a1: what is selected while a1 is active is not
selected when a2 is, and after it the two views share one selection again.
A sketch closed in a1 stays selected, for every view, as it always has:
what an edit leaves selected is handed back at its end.

And with the preference PerViewSelection, in a second document: two idle
views, no edit and no dialog, each with a selection of its own.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (76d3a68d1e), where every view
of the main window selects into the one shared instance.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SelPerView"
V = FreeCAD.Vector
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
Control = FreeCADGui.Control
Sel = FreeCADGui.Selection

state = {"done": False, "views": {}, "shot": 0, "base": {}, "told": None}
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


def activate(name):
    FreeCADGui.getMainWindow().setActiveWindow(state["views"][name])
    settle(150)


def selected():
    """What is selected, as the commands of the active view see it."""
    return sorted(
        o.ObjectName + ("." + ",".join(o.SubElementNames) if o.SubElementNames else "")
        for o in Sel.getSelectionEx(DOC)
    )


def seen_from(name):
    activate(name)
    return selected()


def viewport(name):
    """The widget a view's mouse events are delivered to."""
    return state["views"][name].graphicsView().viewport()


def spot(name, pt):
    """A 3D point's place in a view's viewport, in widget coordinates."""
    view, vp = state["views"][name], viewport(name)
    dpr = vp.devicePixelRatioF()
    x, y = view.getPointOnViewport(pt)
    return vp, QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)


def mouse(vp, kind, pos, button, buttons):
    ev = QtGui.QMouseEvent(
        kind, pos, vp.mapToGlobal(pos.toPoint()), button, buttons, QtCore.Qt.NoModifier
    )
    QtWidgets.QApplication.sendEvent(vp, ev)


def click(name, pt):
    """A left click on a 3D point in one view, as its user would make it."""
    vp, pos = spot(name, pt)
    left, none = QtCore.Qt.LeftButton, QtCore.Qt.NoButton
    mouse(vp, QtCore.QEvent.MouseMove, pos, none, none)
    settle(250)
    mouse(vp, QtCore.QEvent.MouseButtonPress, pos, left, left)
    settle(60)
    mouse(vp, QtCore.QEvent.MouseButtonRelease, pos, left, none)
    settle(350)


def pixel(name, pt, tag):
    """The backend's own framebuffer at the projection of a 3D point."""
    view = state["views"][name]
    activate(name)
    view.redraw()
    settle(500)
    try:
        view.waitFrameComplete()
    except Exception:
        pass
    state["shot"] += 1
    path = os.path.join(OUT, "%02d-%s-%s.png" % (state["shot"], name, tag))
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(pt)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def differs(a, b):
    return sum(abs(p - q) for p, q in zip(a, b)) > 45


def tree_selected():
    """The labels the tree shows selected, of this document's objects."""
    names = set()
    for tree in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTreeWidget):
        for item in tree.selectedItems():
            names.add(item.text(0))
    return sorted(n for n in names if n in ("BoxA", "BoxB", "Body", "Pad", "Sketch"))


class Told:
    """A global observer, as a workbench or an addon registers one."""

    def __init__(self):
        self.objects = set()

    def addSelection(self, doc, obj, sub, pnt):
        self.objects.add(obj)

    def removeSelection(self, doc, obj, sub):
        self.objects.discard(obj)

    def setSelection(self, doc):
        # "Read the selection again": of no document in particular
        self.objects = set(o.Name for o in Sel.getSelection(DOC))

    def clearSelection(self, doc):
        self.objects = set()


class Panel:
    def __init__(self, title):
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(title)

    def accept(self):
        return True

    def reject(self):
        return True


CENTRE_A = V(5, 5, 10)
CENTRE_B = V(35, 5, 10)


def step(fn):
    steps.append(fn)
    return fn


@step
def build():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    VIEW.SetInt("RenderCache", 3)
    VIEW.SetBool("PerViewEdit", True)
    VIEW.SetBool("ShowNaviCube", False)
    VIEW.SetBool("UseNavigationAnimations", False)
    doc = FreeCAD.newDocument(DOC)
    a = doc.addObject("Part::Box", "BoxA")
    b = doc.addObject("Part::Box", "BoxB")
    b.Placement.Base = V(30, 0, 0)
    body = doc.addObject("PartDesign::Body", "Body")
    body.Placement.Base = V(0, 30, 0)
    sketch = body.newObject("Sketcher::SketchObject", "Sketch")
    sketch.Support = (doc.getObject("XY_Plane"), [""])
    sketch.MapMode = "FlatFace"
    corners = [V(0, 0, 0), V(10, 0, 0), V(10, 10, 0), V(0, 10, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sketch
    pad.Length = 10
    doc.recompute()
    sketch.Visibility = False
    for obj in (a, b):
        obj.ViewObject.ShapeColor = (0.6, 0.6, 0.6)
    settle()
    v = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")
    FreeCADGui.getMainWindow().setActiveWindow(v[0])
    FreeCADGui.runCommand("Std_ViewCreate")
    settle(400)


@step
def frame():
    v = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")
    if len(v) != 2:
        note("ABORT %d 3D views" % len(v))
        del steps[:]
        return
    state["views"] = {"a1": v[0], "a2": v[1]}
    for name in ("a1", "a2"):
        activate(name)
        state["views"][name].viewTop()
        state["views"][name].fitAll()
        settle(600)
    Sel.clearSelection()
    state["told"] = Told()
    Sel.addObserver(state["told"])
    settle(200)
    for name in ("a1", "a2"):
        state["base"][name] = pixel(name, CENTRE_B, "plain")
    activate("a1")
    Sel.addSelection(DOC, "BoxA")
    settle(150)
    Control.showDialog(Panel("selection per view"))


@step
def a_dialog_in_a1():
    activate("a1")
    check("the dialog's view starts on a copy: BoxA is selected in it", selected() == ["BoxA"],
          selected())
    Sel.addSelection(DOC, "BoxB")
    settle(150)
    check("BoxB added while a1 is active is selected for a1", selected() == ["BoxA", "BoxB"],
          selected())
    got = seen_from("a2")
    check("it is not selected when a2 is the active view", got == ["BoxA"], got)
    check("the tree shows a2's selection", tree_selected() == ["BoxA"], tree_selected())
    told = sorted(state["told"].objects)
    check("a global observer was told of a2's selection", told == ["BoxA"], told)
    Sel.clearSelection()
    settle(150)
    got = seen_from("a1")
    check("a clear while a2 is active leaves a1's selection", got == ["BoxA", "BoxB"], got)
    check("the tree shows a1's selection again", tree_selected() == ["BoxA", "BoxB"],
          tree_selected())
    told = sorted(state["told"].objects)
    check("the observer was told of a1's selection", told == ["BoxA", "BoxB"], told)


@step
def the_gate():
    activate("a1")
    Sel.clearSelection()
    Sel.addSelectionGate("SELECT Part::Feature SUBELEMENT Edge")
    activate("a2")
    Sel.clearSelection()
    Sel.addSelection(DOC, "BoxA", "Face1")
    settle(100)
    check("a gate set while a1 is active does not gate a2", selected() == ["BoxA.Face1"],
          selected())
    Sel.clearSelection()
    activate("a1")
    Sel.addSelection(DOC, "BoxA", "Face1")
    settle(100)
    check("it does gate a1", selected() == [], selected())
    Sel.addSelection(DOC, "BoxA", "Edge1")
    settle(100)
    check("and lets through what it allows", selected() == ["BoxA.Edge1"], selected())
    Sel.removeSelectionGate()
    Sel.clearSelection()
    settle(100)


@step
def clicks():
    for name in ("a1", "a2"):
        activate(name)
        Sel.clearSelection()
    settle(150)
    click("a2", CENTRE_B)
    got2, got1 = seen_from("a2"), seen_from("a1")
    check("a click on BoxB in a2 selects it for a2", got2 and got2[0].startswith("BoxB"), got2)
    check("and not for a1", got1 == [], got1)
    click("a1", CENTRE_A)
    got1, got2 = seen_from("a1"), seen_from("a2")
    check("a click on BoxA in a1 selects it for a1", got1 and got1[0].startswith("BoxA"), got1)
    check("and a2 keeps its own", got2 and got2[0].startswith("BoxB") and len(got2) == 1, got2)


@step
def drawn():
    for name in ("a1", "a2"):
        activate(name)
        Sel.clearSelection()
    activate("a1")
    Sel.addSelection(DOC, "BoxB")
    settle(200)
    lit1 = pixel("a1", CENTRE_B, "boxb-selected-in-a1")
    lit2 = pixel("a2", CENTRE_B, "boxb-selected-in-a1")
    check("BoxB selected in a1 is drawn selected in a1", differs(lit1, state["base"]["a1"]),
          "%s was %s" % (lit1, state["base"]["a1"]))
    check("and not in a2", not differs(lit2, state["base"]["a2"]),
          "%s was %s" % (lit2, state["base"]["a2"]))


@step
def close_the_dialog():
    activate("a2")
    Sel.clearSelection()
    activate("a1")
    Sel.clearSelection()
    Sel.addSelection(DOC, "BoxA")
    settle(150)
    Control.closeDialog()


@step
def dialog_closed():
    got = seen_from("a2")
    check("closing the dialog hands a1's selection back to the shared one", got == ["BoxA"], got)
    got = seen_from("a1")
    check("a1 shares it again", got == ["BoxA"], got)
    Sel.clearSelection()
    settle(100)
    activate("a1")
    FreeCADGui.getDocument(DOC).setEdit(FreeCAD.getDocument(DOC).Pad, 0)


@step
def an_edit_in_a1():
    activate("a1")
    Sel.clearSelection()
    Sel.addSelection(DOC, "BoxB")
    settle(150)
    check("in an edit, BoxB selected while a1 is active is a1's", selected() == ["BoxB"],
          selected())
    got = seen_from("a2")
    check("it is not selected when a2 is the active view", got == [], got)
    Sel.addSelection(DOC, "BoxA")
    settle(100)
    got = seen_from("a1")
    check("what a2 selects does not reach the edit", got == ["BoxB"], got)
    activate("a2")
    Sel.clearSelection()
    activate("a1")
    FreeCADGui.getDocument(DOC).resetEdit()


@step
def edit_left():
    got2, got1 = seen_from("a2"), seen_from("a1")
    check("after the edit a1 and a2 share one selection again", got1 == got2, (got1, got2))
    Sel.clearSelection()
    settle(100)
    activate("a1")
    FreeCADGui.getDocument(DOC).setEdit(FreeCAD.getDocument(DOC).Sketch, 0)


@step
def a_sketch_in_a1():
    activate("a2")
    Sel.clearSelection()
    Sel.addSelection(DOC, "BoxA")
    settle(100)
    activate("a1")
    FreeCADGui.getDocument(DOC).resetEdit()


@step
def sketch_left():
    # The sketcher selects the sketch it leaves, ready for the next
    # command: that is the edit's own selection, handed back at its end.
    got = seen_from("a2")
    check("a sketch stays selected after it is closed, for every view", got == ["Sketch"], got)


@step
def every_view_its_own():
    # The preference: a selection per view from the start, with no edit
    # and no dialog. It is read when a view is made.
    VIEW.SetBool("PerViewSelection", True)
    doc = FreeCAD.newDocument(DOC + "Own")
    doc.addObject("Part::Box", "BoxA")
    doc.addObject("Part::Box", "BoxB").Placement.Base = V(30, 0, 0)
    doc.recompute()
    settle(300)
    v = FreeCADGui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")
    FreeCADGui.getMainWindow().setActiveWindow(v[0])
    FreeCADGui.runCommand("Std_ViewCreate")
    settle(400)


@step
def every_view_its_own_checked():
    name = DOC + "Own"
    v = FreeCADGui.getDocument(name).mdiViewsOfType("Gui::View3DInventor")
    if len(v) != 2:
        check("two views of the second document", False, len(v))
        return
    mw = FreeCADGui.getMainWindow()

    def names():
        return sorted(o.Name for o in Sel.getSelection(name))

    mw.setActiveWindow(v[0])
    settle(150)
    Sel.addSelection(name, "BoxA")
    settle(100)
    mw.setActiveWindow(v[1])
    settle(150)
    got = names()
    check("with PerViewSelection, what one view selects the other does not", got == [], got)
    Sel.addSelection(name, "BoxB")
    settle(100)
    mw.setActiveWindow(v[0])
    settle(150)
    got = names()
    check("and each keeps its own across a switch", got == ["BoxA"], got)
    VIEW.SetBool("PerViewSelection", False)


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
    QtCore.QTimer.singleShot(500, advance)


def finish():
    if state["done"]:
        return
    state["done"] = True
    VIEW.SetBool("PerViewEdit", False)
    VIEW.SetBool("PerViewSelection", False)
    try:
        if state["told"] is not None:
            Sel.removeObserver(state["told"])
        Sel.removeSelectionGate()
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
