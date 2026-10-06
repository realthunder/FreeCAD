"""The combo view's Tasks tab shows the panel of the ACTIVE view.

Milestone 2 of docs/TaskPanelPerView.md, its second half (sec 4.3, 5.1,
5.4): a task dialog belongs to a view, and the task view shows its page
while that view is the active one. With another view active it shows the
watchers, and a line saying which view holds a panel with a button that
goes there. The page is kept, not rebuilt: come back and it is as it was.

Document A: a box, a body with a sketch, an object whose edit opens no
dialog, two 3D views a1 and a2. Document B: a box, one view b1. A third
document to be closed.

A Python panel shown for a1:

  - showDialog() hands back the task dialog, as upstream's does;
  - its page is shown while a1 is active, the Tasks tab is raised and
    carries the busy icon; the panel was opened, then activated;
  - with a2 active the watchers' page is shown -- the watchers on it --
    with the hint naming a1; the icon is off; the panel was deactivated;
  - back in a1 the same page, what was typed into it still there, and the
    tab the user had switched to is NOT taken from them;
  - with a view of another document active, the watchers and the hint;
    the hint's button makes a1 the active view;
  - closed for a1 from a2, the hint goes.

A sketch edited in a1 with PerViewEdit on: the same, and closing a1
rejects the panel and ends the edit. With PerViewEdit off the edit is
every view's of that document, and so is its panel: a2 shows it, b1 does
not.

The auto-close switches: on reset edit, on deleted document, on closed
view (each tells the panel which it was); a view closed with a panel that
asked for nothing rejects it.

With the test-only TaskPanelAllowConcurrent: two panels in two views, each
shown for its own, each closed by its own view's name.

And a contextual panel (the Assembly's solver messages) is shown with the
views of its document only.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (c108db0c04), where the task
view shows the one dialog whichever view is active.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC_A = "TaskComboA"
DOC_B = "TaskComboB"
DOC_C = "TaskComboC"
DOC_ASM = "TaskComboAsm"
V = FreeCAD.Vector
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
TASKS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TaskView")
Control = FreeCADGui.Control

state = {"done": False, "views": {}, "panels": {}, "task": None}
steps = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def claim(name, fn):
    """A check whose reading may raise: before the change some of them do."""
    try:
        got = fn()
    except Exception as e:
        return check(name, False, "%s: %s" % (type(e).__name__, e))
    if isinstance(got, tuple):
        return check(name, got[0], got[1])
    return check(name, bool(got))


def settle(ms=200):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def mw():
    return FreeCADGui.getMainWindow()


def views3d(name):
    return FreeCADGui.getDocument(name).mdiViewsOfType("Gui::View3DInventor")


def activate(name):
    mw().setActiveWindow(state["views"][name])
    settle(350)


def widget_of(name):
    """The view's own widget: what its user closes."""
    w = state["views"][name].graphicsView()
    while w is not None and w.metaObject().className() != "Gui::View3DInventor":
        w = w.parentWidget()
    return w


def title_of(name):
    return widget_of(name).windowTitle().replace("[*]", "")


def active_is(name):
    view = FreeCADGui.ActiveDocument.ActiveView if FreeCADGui.ActiveDocument else None
    known = state["views"][name]
    return view is not None and (view is known or view == known)


def task_view():
    for w in mw().findChildren(QtWidgets.QWidget):
        try:
            if w.metaObject().className() == "Gui::TaskView::TaskView":
                return w
        except RuntimeError:
            pass
    return None


def shown():
    """What the task view shows: 'dialog' (a dialog's page) or 'watchers'."""
    stack = task_view().findChild(QtWidgets.QStackedWidget)
    cls = stack.currentWidget().metaObject().className()
    return "dialog" if cls == "Gui::TaskView::TaskPage" else "watchers"


def in_shown_page(widget):
    """Whether a widget is in the page the task view shows now, and shown
    there: nothing between it and the task view is hidden."""
    if widget is None:
        return False
    tv = task_view()
    page = tv.findChild(QtWidgets.QStackedWidget).currentWidget()
    try:
        return (widget is page or page.isAncestorOf(widget)) and widget.isVisibleTo(tv)
    except RuntimeError:
        return False


def tabs():
    return mw().findChild(QtWidgets.QTabWidget, "combiTab")


def tasks_index():
    return tabs().indexOf(task_view())


def busy():
    return not tabs().tabIcon(tasks_index()).isNull()


def hint():
    """The lines of the hint on the watchers' page: [(text, button)]."""
    out = []
    for w in task_view().findChildren(QtWidgets.QWidget, "taskPanelElsewhere"):
        if w.isHidden() or not in_shown_page(w):
            continue
        for row in w.findChildren(QtWidgets.QPushButton):
            # A line of an earlier state is hidden and deleted later, which
            # an event loop inside a step does not get to
            if not row.isHidden():
                out.append((row.text(), row))
    return out


def hint_names(name):
    title = title_of(name)
    return any(title in text for text, _ in hint())


class Panel:
    def __init__(self, title):
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(title)
        self.edit = QtWidgets.QLineEdit(self.form)
        QtWidgets.QVBoxLayout(self.form).addWidget(self.edit)
        self.calls = []

    def open(self):
        self.calls.append("open")

    # Not activate() and deactivate(): those names are taken in panels
    # for things of their own
    def panelActivated(self):
        self.calls.append("activate")

    def panelDeactivated(self):
        self.calls.append("deactivate")

    def accept(self):
        self.calls.append("accept")
        return True

    def reject(self):
        self.calls.append("reject")
        return True

    def autoClosedOnResetEdit(self):
        self.calls.append("autoClosedOnResetEdit")

    def autoClosedOnDeletedDocument(self):
        self.calls.append("autoClosedOnDeletedDocument")

    def autoClosedOnClosedView(self):
        self.calls.append("autoClosedOnClosedView")


class Watcher:
    """A task watcher that always shows, as a workbench installs one."""

    def __init__(self):
        self.title = "combo watcher"
        self.body = QtWidgets.QLabel("watcher body")
        self.widgets = [self.body]

    def shouldShow(self):
        return True


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


def show(key, title, **kw):
    """Show a panel for the view being handled, and remember both."""
    panel = Panel(title)
    state["panels"][key] = panel
    state["task"] = None
    try:
        state["task"] = Control.showDialog(panel, **kw)
    except RuntimeError as e:
        return str(e)
    return ""


def calls(key):
    return list(state["panels"][key].calls)


def close_any():
    """Leave no dialog and no edit behind, whatever the step before did."""
    for name in list(FreeCAD.listDocuments().keys()):
        try:
            FreeCADGui.getDocument(name).resetEdit()
        except Exception:
            pass
    for _ in range(4):
        if not Control.activeDialog():
            break
        Control.closeDialog()
        settle(100)


def second_view():
    """Give document A its two views again, and name them."""
    v = views3d(DOC_A)
    if len(v) < 2:
        mw().setActiveWindow(v[0])
        settle(200)
        FreeCADGui.runCommand("Std_ViewCreate")
        settle(500)
        v = views3d(DOC_A)
    state["views"]["a1"], state["views"]["a2"] = v[0], v[1]


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
    TASKS.SetBool("TaskPanelAllowConcurrent", False)

    c = FreeCAD.newDocument(DOC_C)
    c.addObject("Part::Box", "Box")
    c.recompute()
    b = FreeCAD.newDocument(DOC_B)
    b.addObject("Part::Box", "Box")
    b.recompute()

    a = FreeCAD.newDocument(DOC_A)
    a.addObject("Part::Box", "Box")
    body = a.addObject("PartDesign::Body", "Body")
    body.Placement.Base = V(0, 30, 0)
    sketch = body.newObject("Sketcher::SketchObject", "Sketch")
    sketch.Support = (a.getObject("XY_Plane"), [""])
    sketch.MapMode = "FlatFace"
    corners = [V(0, 0, 0), V(10, 0, 0), V(10, 10, 0), V(0, 10, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
    quiet = a.addObject("App::FeaturePython", "Quiet")
    QuietEdit(quiet.ViewObject)
    a.recompute()
    settle()
    mw().setActiveWindow(views3d(DOC_A)[0])
    FreeCADGui.runCommand("Std_ViewCreate")
    settle(500)


@step
def name_views():
    va, vb, vc = views3d(DOC_A), views3d(DOC_B), views3d(DOC_C)
    if len(va) != 2 or len(vb) != 1 or len(vc) != 1 or task_view() is None or tabs() is None:
        note("ABORT views: %d of A, %d of B, %d of C" % (len(va), len(vb), len(vc)))
        del steps[:]
        return
    state["views"] = {"a1": va[0], "a2": va[1], "b1": vb[0], "c1": vc[0]}
    state["watcher"] = Watcher()
    Control.addTaskWatcher([state["watcher"]])
    activate("a1")
    tabs().setCurrentIndex(0)
    settle(400)
    check("the watchers are shown with no dialog open", shown() == "watchers", shown())
    show("one", "one")


@step
def a_panel_in_a1():
    one = state["panels"]["one"]
    check("showDialog hands back the task dialog", state["task"] is not None)
    activate("a1")
    check("its page is shown while a1 is active", shown() == "dialog", shown())
    check("the Tasks tab was raised for it", tabs().currentIndex() == tasks_index())
    check("the Tasks tab carries the busy icon", busy())
    check("the panel was opened, then activated", calls("one") == ["open", "activate"],
          calls("one"))
    one.edit.setText("kept")

    activate("a2")
    check("with a2 active the watchers' page is shown", shown() == "watchers", shown())
    check("the watchers are on it", in_shown_page(state["watcher"].body))
    check("with a hint", len(hint()) == 1, [t for t, _ in hint()])
    check("that names a1", hint_names("a1"), ([t for t, _ in hint()], title_of("a1")))
    check("the busy icon is off", not busy())
    check("the panel was deactivated", calls("one")[-1:] == ["deactivate"], calls("one"))
    check("asked for nobody in particular there is still a dialog", Control.activeDialog())
    check("asked for a2 there is none", not Control.activeDialog(view=state["views"]["a2"]))

    tabs().setCurrentIndex(0)
    settle(100)
    activate("a1")
    check("back in a1 its page is shown again", shown() == "dialog", shown())
    check("the same page: what was typed is still there",
          in_shown_page(one.edit) and one.edit.text() == "kept")
    check("the tab the user had switched to is not taken from them", tabs().currentIndex() == 0,
          tabs().currentIndex())
    check("the panel was activated again", calls("one").count("activate") == 2, calls("one"))
    tabs().setCurrentIndex(tasks_index())
    settle(100)

    activate("b1")
    check("with another document's view active, the watchers", shown() == "watchers", shown())
    check("and the hint", hint_names("a1"), [t for t, _ in hint()])
    for _, button in hint()[:1]:
        button.click()
    settle(400)
    check("the hint's button makes a1 the active view", active_is("a1"))
    check("and its page is shown", shown() == "dialog", shown())

    activate("a2")
    Control.closeDialog(view=state["views"]["a1"])


@step
def closed_from_a2():
    check("closed for a1 from a2: no dialog is left", not Control.activeDialog())
    check("the hint is gone", hint() == [], [t for t, _ in hint()])
    check("the watchers are shown", shown() == "watchers", shown())
    got = calls("one")
    check("every activation of the panel was ended",
          got.count("activate") == got.count("deactivate") and got.count("activate") > 0, got)
    close_any()
    activate("a1")
    FreeCADGui.getDocument(DOC_A).setEdit(FreeCAD.getDocument(DOC_A).Sketch, 0)


@step
def a_sketch_in_a1():
    gdoc = FreeCADGui.getDocument(DOC_A)
    activate("a1")
    check("a sketch edited in a1: its panel is shown", shown() == "dialog", shown())
    dlg = Control.activeTaskDialog(view=state["views"]["a1"])
    check("the edit named its document on a1's dialog",
          dlg is not None and dlg.getDocumentName() == DOC_A)
    activate("a2")
    check("with a2 active the watchers, and the hint", shown() == "watchers" and hint_names("a1"),
          (shown(), [t for t, _ in hint()]))
    check("the sketch is still being edited", FreeCADGui.editDocument() is not None)
    activate("a1")
    check("back in a1 the sketch's panel", shown() == "dialog", shown())
    # Something done in the edit, to tell leaving it from cancelling it
    doc = FreeCAD.getDocument(DOC_A)
    doc.openTransaction("a line in the edit")
    doc.Sketch.addGeometry(Part.LineSegment(V(2, 2, 0), V(8, 8, 0)))
    doc.commitTransaction()
    settle(200)
    activate("a2")
    if os.environ.get("GT_NO_EDIT_VIEW_CLOSE"):
        # For scoring the rest against a tree that does not survive this
        note("SKIPPED closing the editing view")
        FreeCADGui.getDocument(DOC_A).resetEdit()
        state["skipped"] = True
        return
    widget_of("a1").close()


@step
def the_editing_view_closed():
    gdoc = FreeCADGui.getDocument(DOC_A)
    if state.get("skipped"):
        close_any()
        second_view()
        VIEW.SetBool("PerViewEdit", False)
        activate("a1")
        gdoc.setEdit(FreeCAD.getDocument(DOC_A).Sketch, 0)
        return
    check("closing the editing view leaves one view", len(views3d(DOC_A)) == 1, len(views3d(DOC_A)))
    check("it ended the edit", FreeCADGui.editDocument() is None)
    check("it left the edit, it did not cancel it: what was drawn is kept",
          len(FreeCAD.getDocument(DOC_A).Sketch.Geometry) == 5,
          len(FreeCAD.getDocument(DOC_A).Sketch.Geometry))
    check("it closed the panel", not Control.activeDialog())
    check("the watchers are shown, with no hint", shown() == "watchers" and hint() == [],
          (shown(), [t for t, _ in hint()]))
    close_any()
    second_view()
    VIEW.SetBool("PerViewEdit", False)
    activate("a1")
    gdoc.setEdit(FreeCAD.getDocument(DOC_A).Sketch, 0)


@step
def an_edit_every_view_shares():
    activate("a1")
    check("PerViewEdit off: the sketch's panel in a1", shown() == "dialog", shown())
    activate("a2")
    check("a2 is in the same edit, and shows the panel too", shown() == "dialog", shown())
    activate("b1")
    check("a view of another document does not", shown() == "watchers" and len(hint()) == 1,
          (shown(), [t for t, _ in hint()]))
    activate("a1")
    FreeCADGui.getDocument(DOC_A).resetEdit()
    VIEW.SetBool("PerViewEdit", True)


@step
def auto_close_on_reset_edit():
    close_any()
    activate("a1")
    show("reset", "closes with the edit")
    claim("a dialog can ask to close when the edit is left",
          lambda: state["task"].setAutoCloseOnResetEdit(True) is None)
    gdoc = FreeCADGui.getDocument(DOC_A)
    gdoc.setEdit(FreeCAD.getDocument(DOC_A).Quiet, 0)
    settle(200)
    check("an edit that opens no dialog was entered", gdoc.getInEdit() is not None)
    gdoc.resetEdit()


@step
def auto_closed_on_reset_edit():
    check("leaving the edit closed the dialog", not Control.activeDialog())
    check("and told the panel so", "autoClosedOnResetEdit" in calls("reset"), calls("reset"))
    close_any()
    activate("c1")
    show("deleted", "closes with the document")
    claim("a dialog can name its document",
          lambda: state["task"].setDocumentName(DOC_C) is None)
    claim("and ask to close with it",
          lambda: state["task"].setAutoCloseOnDeletedDocument(True) is None)
    FreeCAD.closeDocument(DOC_C)


@step
def auto_closed_on_deleted_document():
    check("closing the document closed the dialog", not Control.activeDialog())
    got = calls("deleted")
    check("and told the panel so", "autoClosedOnDeletedDocument" in got, got)
    check("without rejecting it", "reject" not in got, got)
    close_any()
    activate("a2")
    show("plain", "asks for nothing")
    activate("a1")
    widget_of("a2").close()


@step
def a_view_closed_with_a_panel():
    check("a view closed with a panel that asked for nothing: the dialog is gone",
          not Control.activeDialog())
    check("the panel was rejected", "reject" in calls("plain"), calls("plain"))
    close_any()
    second_view()
    activate("a2")
    show("asked", "closes with its view")
    claim("a dialog can ask to close with its view",
          lambda: state["task"].setAutoCloseOnClosedView(True) is None)
    activate("a1")
    widget_of("a2").close()


@step
def auto_closed_on_closed_view():
    check("closing the view closed the dialog", not Control.activeDialog())
    got = calls("asked")
    check("and told the panel so", "autoClosedOnClosedView" in got, got)
    check("without rejecting it", "reject" not in got, got)
    close_any()
    second_view()
    TASKS.SetBool("TaskPanelAllowConcurrent", True)
    activate("a1")
    show("pa", "panel of a1")
    activate("a2")
    state["refused"] = show("pb", "panel of a2")


@step
def two_panels_in_two_views():
    a1, a2 = state["views"]["a1"], state["views"]["a2"]
    pa, pb = state["panels"]["pa"], state["panels"]["pb"]
    check("with the test switch a second panel, for a2, is shown", state["refused"] == "",
          state["refused"])
    check("each view has its dialog",
          Control.activeDialog(view=a1) and Control.activeDialog(view=a2))
    activate("a1")
    check("a1 shows its own", shown() == "dialog" and in_shown_page(pa.form))
    activate("a2")
    check("a2 shows its own", shown() == "dialog" and in_shown_page(pb.form))
    refused = show("pc", "a second for a2")
    check("a second panel for the same view is still refused", refused != "", refused)
    activate("b1")
    check("another document's view shows the watchers and a line for each",
          shown() == "watchers" and len(hint()) == 2, (shown(), [t for t, _ in hint()]))
    activate("a1")
    Control.closeDialog(view=a2)


@step
def one_of_two_closed():
    a1, a2 = state["views"]["a1"], state["views"]["a2"]
    check("closing a2's leaves a1's", Control.activeDialog(view=a1)
          and not Control.activeDialog(view=a2))
    check("a2's panel was not rejected, a1's not touched",
          "reject" not in calls("pb") and "accept" not in calls("pa"), (calls("pa"), calls("pb")))
    activate("a2")
    check("a2 shows the watchers and the hint for a1", shown() == "watchers" and hint_names("a1"),
          (shown(), [t for t, _ in hint()]))
    Control.closeDialog(view=a1)
    TASKS.SetBool("TaskPanelAllowConcurrent", False)


@step
def a_contextual_panel():
    close_any()
    try:
        import AssemblyGui  # noqa: F401

        doc = FreeCAD.newDocument(DOC_ASM)
        asm = doc.addObject("Assembly::AssemblyObject", "Assembly")
        doc.recompute()
        settle(400)
        state["views"]["asm"] = views3d(DOC_ASM)[0]
        activate("asm")
        FreeCADGui.getDocument(DOC_ASM).setEdit(asm, 0)
    except Exception as e:
        note("SKIPPED the contextual panel: %s: %s" % (type(e).__name__, e))
        state["views"].pop("asm", None)


@step
def a_contextual_panel_checked():
    if "asm" not in state["views"]:
        return
    found = [
        w
        for w in task_view().findChildren(QtWidgets.QWidget)
        if w.metaObject().className() == "AssemblyGui::TaskAssemblyMessages"
    ]
    if not found:
        note("SKIPPED the contextual panel: the assembly added none")
        return
    activate("asm")
    check("the assembly's panel is shown with its document's view", in_shown_page(found[0]))
    activate("b1")
    check("and not with another document's", not in_shown_page(found[0]))
    activate("asm")
    check("and again on return", in_shown_page(found[0]))
    FreeCADGui.getDocument(DOC_ASM).resetEdit()


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
    TASKS.SetBool("TaskPanelAllowConcurrent", False)
    try:
        close_any()
        Control.clearTaskWatcher()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, advance)
