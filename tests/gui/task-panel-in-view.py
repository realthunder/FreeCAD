"""A task panel sits inside the view it belongs to.

Milestone 3 of docs/TaskPanelPerView.md (sec 5.2, 5.4, 5.5): with the
preference View/TaskPanelInView on, a task dialog's page is hosted in its
owner view -- a child of the view's cell, over the picture -- and stays
there whichever view is active. The Tasks tab keeps the watchers. The
switch moves the pages, it closes nothing.

Document A: a box, a body with a sketch and a pad, two 3D views a1 and a2
in two cells of one view area. Document B: a box, one view b1 in a tab of
its own.

A Python panel shown for a1, in the combo view, then the preference on:

  - its page is a child of a1's cell, shown there, as it was left: the
    same dialog, what was typed still in it, and the panel told nothing;
  - the Tasks tab shows the watchers, with no busy icon;
  - the host keeps to the left edge of the cell and to a third of it,
    and is as tall as the panel needs;
  - with a2 active the panel is still shown in a1, and was deactivated;
  - with b1 active (another tab) the watchers' page says where it is;
  - a click into it makes a1 the active view, on a widget that takes the
    focus and on one that does not.

A panel used while its view is NOT the active one -- a key delivered to
one of its buttons, a timer of its own -- runs as its own view: what it
selects is selected in a1, not in the view that happens to be active.

With the test-only TaskPanelAllowConcurrent, a panel in each cell: both
shown at once; what is typed goes to the one typed into; Enter accepts
and Escape rejects THAT panel and leaves the other.

The host's own button and the combo view's title bar button both flip the
preference and move the page without closing the dialog. The host
collapses to its header, moves to the other side of its cell, and
remembers both.

A sketch edited in a1: its panel is in a1's cell, stays with a2 active,
and the edit survives the switch both ways. A pad edited in a1: a length
typed into the hosted panel lands in the pad.

A view outside any view area hosts its panel in itself, undocked and
docked again; split, it is in a cell and its host goes there with it. A
maximized neighbour hides the host with its cell. Closing a view, and
closing a document, under a hosted panel closes the dialog and leaves
nothing behind.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (26b042b7f1), which has no such
preference: a panel is in the combo view whatever it says.
"""
import ctypes
import ctypes.util
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC_A = "TaskInViewA"
DOC_B = "TaskInViewB"
DOC_D = "TaskInViewD"
V = FreeCAD.Vector
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
TASKS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TaskView")
HOST3D = TASKS.GetGroup("Host").GetGroup("Gui::View3DInventor")
Control = FreeCADGui.Control
QTest = QtTest.QTest
LEFT = QtCore.Qt.LeftButton

state = {"done": False, "views": {}, "panels": {}, "task": None}
steps = []
# The hosts a step has looked at. A widget fetched through a wrapper that
# is then dropped is taken for deleted by the binding (the wrapper of a
# parent that goes takes its children's with it), and a host found among
# all the application's widgets has nothing else to hold it.
KEEP = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def claim(name, fn):
    """A check whose reading may raise: before the change most of them do,
    there being no host to read."""
    try:
        got = fn()
    except Exception as e:
        return check(name, False, "%s: %s" % (type(e).__name__, e))
    if isinstance(got, tuple):
        return check(name, bool(got[0]), got[1])
    return check(name, bool(got))


def settle(ms=200):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def views3d(name):
    return FreeCADGui.getDocument(name).mdiViewsOfType("Gui::View3DInventor")


def activate(name):
    mw().setActiveWindow(state["views"][name])
    settle(350)


def widget_of(name):
    """The view's own widget: what its user closes."""
    w = state["views"][name].graphicsView()
    while w is not None and cls(w) != "Gui::View3DInventor":
        w = w.parentWidget()
    return w


def cell_of(name):
    w = widget_of(name)
    while w is not None and cls(w) != "Gui::ViewAreaCell":
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
            if cls(w) == "Gui::TaskView::TaskView":
                return w
        except RuntimeError:
            pass
    return None


def shown():
    """What the task view shows: 'dialog' (a dialog's page) or 'watchers'."""
    stack = task_view().findChild(QtWidgets.QStackedWidget)
    return "dialog" if cls(stack.currentWidget()) == "Gui::TaskView::TaskPage" else "watchers"


def in_task_view(widget):
    tv = task_view()
    try:
        return tv.isAncestorOf(widget) and widget.isVisibleTo(tv)
    except RuntimeError:
        return False


def tabs():
    return mw().findChild(QtWidgets.QTabWidget, "combiTab")


def busy():
    return not tabs().tabIcon(tabs().indexOf(task_view())).isNull()


def hint():
    """The lines of the hint on the watchers' page: [text]."""
    out = []
    for w in task_view().findChildren(QtWidgets.QWidget, "taskPanelElsewhere"):
        if w.isHidden() or not in_task_view(w):
            continue
        for row in w.findChildren(QtWidgets.QPushButton):
            if not row.isHidden():
                out.append(row.text())
    return out


def hosts():
    """Every panel host there is, wherever its view stands. One awaiting
    deletion is hidden, and is not counted."""
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == "Gui::TaskView::TaskPanelHost" and not w.isHidden():
                out.append(w)
        except RuntimeError:
            pass
    return out


def host_of(name):
    """The host in the view's place: its cell, or the view itself."""
    view = widget_of(name)
    for host in hosts():
        place = host.parentWidget()
        if place is not None and (place == view or place.isAncestorOf(view)):
            KEEP.append(host)
            return host
    return None


def hosted(name, widget):
    """Whether a widget is in the view's host, and shown there."""
    host = host_of(name)
    return host is not None and host.isAncestorOf(widget) and widget.isVisibleTo(host)


def on_screen(widget):
    try:
        return widget.isVisible()
    except RuntimeError:
        return False


def host_button(name, which):
    return host_of(name).findChild(QtWidgets.QAbstractButton, which)


def click_into(name):
    """A click of the real pointer in the middle of the view, through the
    window system (XTEST on X11): a mouse event sent through Qt to a 3D
    view is not handled as a click of the pointer is, and does not move
    the keyboard focus there."""
    widget = widget_of(name)
    at = widget.mapToGlobal(widget.rect().center())
    if hasattr(ctypes, "windll"):
        QTest.mouseClick(widget, LEFT)
        settle(400)
        return
    x11 = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
    xtst = ctypes.CDLL(ctypes.util.find_library("Xtst") or "libXtst.so.6")
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                          ctypes.c_int, ctypes.c_ulong]
    xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int,
                                          ctypes.c_ulong]
    display = x11.XOpenDisplay(None)
    if not display:
        raise RuntimeError("no X display to click on")
    try:
        xtst.XTestFakeMotionEvent(display, -1, at.x(), at.y(), 0)
        x11.XSync(display, 0)
        settle(150)
        xtst.XTestFakeButtonEvent(display, 1, 1, 0)
        x11.XSync(display, 0)
        settle(80)
        xtst.XTestFakeButtonEvent(display, 1, 0, 0)
        x11.XSync(display, 0)
    finally:
        x11.XCloseDisplay(display)
    settle(400)


def title_button():
    """The combo view's title bar button that switches the hosting."""
    for b in mw().findChildren(QtWidgets.QAbstractButton, "OBTN TaskHost"):
        if b.isVisible():
            return b
    return None


def selected():
    return sorted(o.Name for o in FreeCADGui.Selection.getSelection())


class Panel:
    def __init__(self, title):
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(title)
        self.label = QtWidgets.QLabel("a line of " + title, self.form)
        self.edit = QtWidgets.QLineEdit(self.form)
        self.button = QtWidgets.QPushButton("select", self.form)
        layout = QtWidgets.QVBoxLayout(self.form)
        for w in (self.label, self.edit, self.button):
            layout.addWidget(w)
        self.calls = []
        self.picks = []
        self.button.clicked.connect(lambda: self.pick("Box"))
        self.timer = QtCore.QTimer(self.form)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(lambda: self.pick("Body"))

    def pick(self, what):
        """What a panel's own code does: it selects, in 'the' selection."""
        self.picks.append(what)
        FreeCADGui.Selection.addSelection(DOC_A, what)

    def open(self):
        self.calls.append("open")

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


class Watcher:
    """A task watcher that always shows, as a workbench installs one."""

    def __init__(self):
        self.title = "in-view watcher"
        self.body = QtWidgets.QLabel("watcher body")
        self.widgets = [self.body]

    def shouldShow(self):
        return True


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


def in_view(on):
    VIEW.SetBool("TaskPanelInView", on)
    settle(300)


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
    VIEW.SetBool("TaskPanelInView", False)
    TASKS.SetBool("TaskPanelAllowConcurrent", False)
    HOST3D.SetBool("Right", False)
    HOST3D.SetBool("Collapsed", False)

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
    for i in range(4):
        sketch.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sketch
    pad.Length = 10
    a.recompute()
    settle()
    mw().setActiveWindow(views3d(DOC_A)[0])
    FreeCADGui.runCommand("Std_ViewCreate")
    settle(500)


@step
def name_views():
    va, vb = views3d(DOC_A), views3d(DOC_B)
    if len(va) != 2 or len(vb) != 1 or task_view() is None or tabs() is None:
        note("ABORT views: %d of A, %d of B" % (len(va), len(vb)))
        del steps[:]
        return
    state["views"] = {"a1": va[0], "a2": va[1], "b1": vb[0]}
    if cell_of("a1") is None or cell_of("a2") is None or cell_of("a1") == cell_of("a2"):
        note("ABORT the two views of A are not two cells")
        del steps[:]
        return
    state["watcher"] = Watcher()
    Control.addTaskWatcher([state["watcher"]])
    FreeCADGui.Selection.clearSelection()
    activate("a1")
    show("one", "one")


@step
def the_page_moves_into_its_view():
    one = state["panels"]["one"]
    activate("a1")
    check("combo mode: the page is in the Tasks tab", shown() == "dialog" and in_task_view(one.form))
    check("and no view hosts anything", hosts() == [], len(hosts()))
    one.edit.setText("kept")
    before = calls("one")

    in_view(True)
    claim("with the preference on, a1 has a host", lambda: host_of("a1") is not None)
    claim("the host is a child of a1's cell",
          lambda: (host_of("a1").parentWidget() == cell_of("a1"),
                   cls(host_of("a1").parentWidget())))
    claim("the panel is in it, and shown", lambda: hosted("a1", one.form) and on_screen(one.form))
    check("no longer in the Tasks tab", not in_task_view(one.form))
    check("as it was left: what was typed is still there", one.edit.text() == "kept")
    check("the same dialog is still a1's", bool(Control.activeDialog(view=state["views"]["a1"])))
    check("the switch told the panel nothing", calls("one") == before, (before, calls("one")))
    check("the Tasks tab shows the watchers", shown() == "watchers", shown())
    check("the watchers are on it", in_task_view(state["watcher"].body))
    check("with no busy icon", not busy())
    check("and no line about a panel that is in sight", hint() == [], hint())

    def placed():
        host, cell = host_of("a1"), cell_of("a1")
        g = host.geometry()
        ok = (cell.rect().contains(g) and g.center().x() < cell.width() / 2
              and g.width() <= max(240, cell.width() // 3) + 2)
        return ok, (g.getRect(), cell.rect().getRect())

    claim("the host keeps to the left edge and a third of its cell", placed)

    def fitted():
        host, cell = host_of("a1"), cell_of("a1")
        form = QtCore.QRect(one.form.mapTo(host, QtCore.QPoint(0, 0)), one.form.size())
        ok = host.rect().contains(form) and host.height() < cell.height() - 100
        return ok, (host.height(), cell.height(), form.getRect())

    claim("and is as tall as its panel needs, not as the view", fitted)

    activate("a2")
    claim("with a2 active the panel is still shown in a1",
          lambda: hosted("a1", one.form) and on_screen(one.form))
    check("it was deactivated: activation follows the view",
          calls("one")[-1:] == ["deactivate"], calls("one"))
    check("and the watchers' page has no line for it: it is in sight", hint() == [], hint())
    activate("b1")
    check("with another tab active the watchers' page says where it is",
          any(title_of("a1") in text for text in hint()), hint())

    activate("a2")
    QTest.mouseClick(one.edit, LEFT)
    settle(300)
    check("a click into the panel makes its view the active one", active_is("a1"))
    check("which activates it", calls("one")[-1:] == ["activate"], calls("one"))
    activate("a2")
    QTest.mouseClick(one.label, LEFT)
    settle(300)
    check("a click on a widget that takes no focus does too", active_is("a1"))
    claim("and a click back into the other view makes that one the active view",
          lambda: click_into("a2") is None and active_is("a2"))
    # What the click picked in a2 is not this test's business
    FreeCADGui.Selection.clearSelection()


@step
def a_panel_used_from_another_view():
    one = state["panels"]["one"]
    doc = FreeCAD.getDocument(DOC_A)
    activate("a1")
    FreeCADGui.Selection.clearSelection()
    activate("a2")
    FreeCADGui.Selection.clearSelection()
    settle(100)
    # No press, so nothing makes a1 active: a key delivered to the button
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        ev = QtGui.QKeyEvent(kind, QtCore.Qt.Key_Space, QtCore.Qt.NoModifier, " ")
        QtWidgets.QApplication.sendEvent(one.button, ev)
    settle(200)
    check("a key delivered to a1's panel with a2 active reached it", one.picks == ["Box"],
          one.picks)
    check("a2 is still the active view", active_is("a2"))
    check("what the panel selected is not a2's", "Box" not in selected(), selected())
    one.timer.start(0)
    settle(300)
    check("a timer of the panel ran", one.picks == ["Box", "Body"], one.picks)
    check("and what it selected is not a2's either", "Body" not in selected(), selected())
    activate("a1")
    check("both are selected in a1, the panel's own view", selected() == ["Body", "Box"],
          selected())
    FreeCADGui.Selection.clearSelection()
    del doc
    Control.closeDialog(view=state["views"]["a1"])


@step
def a_panel_in_each_cell():
    check("the dialog closed: its host is gone", hosts() == [], len(hosts()))
    close_any()
    TASKS.SetBool("TaskPanelAllowConcurrent", True)
    activate("a1")
    show("pa", "panel of a1")
    activate("a2")
    state["refused"] = show("pb", "panel of a2")


@step
def keys_go_to_their_own_panel():
    pa, pb = state["panels"]["pa"], state["panels"]["pb"]
    check("a second panel, for a2, is shown", state["refused"] == "", state["refused"])
    claim("each cell hosts its own",
          lambda: hosted("a1", pa.form) and hosted("a2", pb.form)
          and host_of("a1") != host_of("a2"))
    check("both are shown at once", on_screen(pa.form) and on_screen(pb.form))
    QTest.mouseClick(pa.edit, LEFT)
    settle(200)
    QTest.keyClicks(pa.edit, "abc")
    settle(100)
    check("what is typed goes to the panel typed into", pa.edit.text() == "abc" and pb.edit.text() == "",
          (pa.edit.text(), pb.edit.text()))
    QTest.keyClick(pa.edit, QtCore.Qt.Key_Return)


@step
def enter_accepted_its_own():
    a1, a2 = state["views"]["a1"], state["views"]["a2"]
    check("Enter in a1's panel accepted it", "accept" in calls("pa"), calls("pa"))
    check("and left a2's alone", calls("pb").count("accept") + calls("pb").count("reject") == 0,
          calls("pb"))
    check("a1 has no dialog, a2 has its own",
          not Control.activeDialog(view=a1) and bool(Control.activeDialog(view=a2)))
    claim("a1's host is gone, a2's stands",
          lambda: host_of("a1") is None and host_of("a2") is not None)
    activate("a1")
    show("pa2", "another for a1")
    pb = state["panels"]["pb"]
    QTest.mouseClick(pb.edit, LEFT)
    settle(200)
    QTest.keyClick(pb.edit, QtCore.Qt.Key_Escape)


@step
def escape_rejected_its_own():
    a1, a2 = state["views"]["a1"], state["views"]["a2"]
    check("Escape in a2's panel rejected it", "reject" in calls("pb"), calls("pb"))
    check("and left a1's alone", calls("pa2").count("accept") + calls("pa2").count("reject") == 0,
          calls("pa2"))
    check("a2 has no dialog, a1 has its own",
          not Control.activeDialog(view=a2) and bool(Control.activeDialog(view=a1)))
    Control.closeDialog(view=a1)
    TASKS.SetBool("TaskPanelAllowConcurrent", False)


@step
def the_two_buttons():
    close_any()
    activate("a1")
    show("two", "two")
    settle(300)
    two = state["panels"]["two"]
    two.edit.setText("stays")
    before = calls("two")
    claim("the host has a button that sends the panel to the combo view",
          lambda: host_button("a1", "taskPanelHostToCombo") is not None)
    claim("clicking it", lambda: QTest.mouseClick(host_button("a1", "taskPanelHostToCombo"), LEFT)
          is None)
    settle(400)
    check("turns the preference off", VIEW.GetBool("TaskPanelInView", True) is False)
    check("the page is in the Tasks tab", shown() == "dialog" and in_task_view(two.form), shown())
    check("no host is left", hosts() == [], len(hosts()))
    check("the dialog was not closed: nothing was told, nothing lost",
          calls("two") == before and two.edit.text() == "stays", (before, calls("two")))
    check("the combo view's title bar has a button for the switch", title_button() is not None)
    claim("clicking it", lambda: QTest.mouseClick(title_button(), LEFT) is None)
    settle(400)
    check("turns the preference on", VIEW.GetBool("TaskPanelInView", False) is True)
    claim("the page is back in a1's cell", lambda: hosted("a1", two.form) and on_screen(two.form))
    check("still the same dialog", calls("two") == before and two.edit.text() == "stays",
          (before, calls("two")))
    claim("the title bar button shows the mode",
          lambda: title_button().isCheckable() and title_button().isChecked())

    def collapse():
        full = host_of("a1").height()
        QTest.mouseClick(host_button("a1", "taskPanelHostCollapse"), LEFT)
        settle(300)
        small = host_of("a1").height()
        return small < 60 and small < full and not on_screen(two.form), (full, small)

    claim("the host collapses to its header", collapse)
    check("and remembers it", HOST3D.GetBool("Collapsed", False) is True)

    def expand():
        QTest.mouseClick(host_button("a1", "taskPanelHostCollapse"), LEFT)
        settle(300)
        return on_screen(two.form) and host_of("a1").height() > 100, host_of("a1").height()

    claim("and opens again", expand)

    def drag_right():
        host, cell = host_of("a1"), cell_of("a1")
        grip = host.findChild(QtWidgets.QWidget, "taskPanelHostHeader")
        start = QtCore.QPoint(30, grip.height() // 2)
        far = grip.mapFrom(cell, QtCore.QPoint(cell.width() - 40, 30))
        QTest.mousePress(grip, LEFT, QtCore.Qt.NoModifier, start)
        for i in range(1, 9):
            p = start + (far - start) * i / 8
            QTest.mouseMove(grip, QtCore.QPoint(int(p.x()), int(p.y())))
            settle(20)
        QTest.mouseRelease(grip, LEFT, QtCore.Qt.NoModifier, grip.mapFrom(cell, QtCore.QPoint(
            cell.width() - 40, 30)))
        settle(300)
        g = host_of("a1").geometry()
        return g.center().x() > cell.width() / 2 and cell.rect().contains(g), g.getRect()

    claim("dragged by its header to the far side, it stays there", drag_right)
    check("and remembers the side", HOST3D.GetBool("Right", False) is True)
    HOST3D.SetBool("Right", False)
    Control.closeDialog(view=state["views"]["a1"])


@step
def a_sketch_edited_in_a1():
    close_any()
    activate("a1")
    FreeCADGui.getDocument(DOC_A).setEdit(FreeCAD.getDocument(DOC_A).Sketch, 0)


@step
def the_sketch_panel_is_in_its_cell():
    gdoc = FreeCADGui.getDocument(DOC_A)
    check("the sketch is being edited", gdoc.getInEdit() is not None)

    def sketch_widgets(root):
        return [w for w in root.findChildren(QtWidgets.QWidget)
                if cls(w).startswith("SketcherGui::")]

    claim("its panel is in a1's cell",
          lambda: (host_of("a1").parentWidget() == cell_of("a1")
                   and len(sketch_widgets(host_of("a1"))) > 0,
                   len(sketch_widgets(host_of("a1")))))
    check("the Tasks tab shows the watchers", shown() == "watchers", shown())
    activate("a2")
    claim("with a2 active it is still shown there",
          lambda: any(on_screen(w) for w in sketch_widgets(host_of("a1"))))
    in_view(False)
    check("switched to the combo view, the edit is still on",
          FreeCADGui.editDocument() is not None)
    activate("a1")
    check("and the Tasks tab shows the sketch's panel",
          shown() == "dialog" and len(sketch_widgets(task_view())) > 0, shown())
    in_view(True)
    check("switched back, the edit is still on",
          FreeCADGui.editDocument() is not None and gdoc.getInEdit() is not None)
    claim("and the panel is in a1's cell again", lambda: len(sketch_widgets(host_of("a1"))) > 0)
    gdoc.resetEdit()


@step
def a_pad_edited_in_a1():
    check("the edit left: its host is gone", hosts() == [], len(hosts()))
    close_any()
    activate("a1")
    FreeCADGui.getDocument(DOC_A).setEdit(FreeCAD.getDocument(DOC_A).Pad, 0)


@step
def a_value_typed_into_the_hosted_panel():
    doc = FreeCAD.getDocument(DOC_A)

    def length_box():
        for w in host_of("a1").findChildren(QtWidgets.QAbstractSpinBox, "lengthEdit"):
            if w.isVisible():
                return w
        return None

    claim("the pad's panel is in a1's cell, with its length box", lambda: length_box() is not None)

    def type_it():
        box = length_box()
        QTest.mouseClick(box, LEFT)
        settle(200)
        box.selectAll()
        QTest.keyClicks(box, "25")
        QTest.keyClick(box, QtCore.Qt.Key_Return)
        settle(600)
        return abs(doc.Pad.Length.Value - 25.0) < 1e-6, doc.Pad.Length.Value

    claim("a length typed into it lands in the pad", type_it)
    check("the pad is still being edited: Enter in a spin box ends the entry, not the dialog",
          FreeCADGui.getDocument(DOC_A).getInEdit() is not None)
    FreeCADGui.getDocument(DOC_A).resetEdit()


@step
def a_view_outside_any_view_area():
    close_any()
    VIEW.SetBool("UseViewArea", False)
    d = FreeCAD.newDocument(DOC_D)
    d.addObject("Part::Box", "Box")
    d.recompute()
    settle(500)
    VIEW.SetBool("UseViewArea", True)
    state["views"]["d1"] = views3d(DOC_D)[0]
    activate("d1")
    if cell_of("d1") is not None:
        note("SKIPPED a view outside a view area: this one is in a cell")
        state["views"].pop("d1")
        return
    show("plainview", "in a plain view")


@step
def it_hosts_in_itself():
    if "d1" not in state["views"]:
        return
    panel = state["panels"]["plainview"]
    claim("a view outside any view area hosts its panel in itself",
          lambda: (host_of("d1").parentWidget() == widget_of("d1"),
                   cls(host_of("d1").parentWidget())))
    claim("shown, inside the view",
          lambda: on_screen(panel.form)
          and widget_of("d1").rect().contains(host_of("d1").geometry()))
    activate("d1")
    FreeCADGui.runCommand("Std_ViewUndock")
    settle(700)
    view = widget_of("d1")
    if not view.isWindow():
        note("SKIPPED the undocked view: Std_ViewUndock left it docked")
    else:
        claim("undocked, the view still hosts its panel",
              lambda: host_of("d1").parentWidget() == view and on_screen(panel.form))
        view.activateWindow()
        mw().setActiveWindow(state["views"]["d1"])
        settle(200)
        FreeCADGui.runCommand("Std_ViewDock")
        settle(700)
        claim("docked again, it still does",
              lambda: hosted("d1", panel.form) and on_screen(panel.form))
    activate("d1")
    FreeCADGui.runCommand("Std_ViewSplitRight")


@step
def split_into_a_cell():
    if "d1" not in state["views"]:
        return
    panel = state["panels"]["plainview"]
    if cell_of("d1") is None:
        note("SKIPPED the view moved into a cell: the split left it outside a view area")
    else:
        claim("split, the view is in a cell now and its host went there with it",
              lambda: (host_of("d1").parentWidget() == cell_of("d1"),
                       cls(host_of("d1").parentWidget())))
        claim("with the panel shown in it", lambda: hosted("d1", panel.form) and on_screen(panel.form))
        check("the dialog was not closed on the way",
              calls("plainview").count("reject") + calls("plainview").count("accept") == 0,
              calls("plainview"))
    FreeCAD.closeDocument(DOC_D)


@step
def a_maximized_neighbour():
    if "d1" in state["views"]:
        check("closing that document closed its dialog", not Control.activeDialog())
        check("and left no host", hosts() == [], len(hosts()))
        state["views"].pop("d1")
    close_any()
    activate("a1")
    show("max", "max")
    settle(300)
    panel = state["panels"]["max"]
    activate("a2")
    FreeCADGui.runCommand("Std_ViewSplitMaximize")
    settle(500)
    check("a2 maximized: a1's panel goes out of sight with its cell", not on_screen(panel.form))
    check("the dialog is still open", bool(Control.activeDialog(view=state["views"]["a1"])))
    FreeCADGui.runCommand("Std_ViewSplitMaximize")
    settle(500)
    claim("restored, it is back", lambda: hosted("a1", panel.form) and on_screen(panel.form))
    Control.closeDialog(view=state["views"]["a1"])


@step
def a_view_closed_under_its_panel():
    close_any()
    second_view()
    activate("a2")
    show("plain", "asks for nothing")
    settle(300)
    claim("a panel hosted in a2", lambda: hosted("a2", state["panels"]["plain"].form))
    activate("a1")
    widget_of("a2").close()


@step
def the_view_closed():
    check("closing the view closed its dialog", not Control.activeDialog())
    check("the panel was rejected", "reject" in calls("plain"), calls("plain"))
    check("and no host is left", hosts() == [], len(hosts()))
    close_any()
    second_view()
    activate("a1")
    show("last", "goes with its document")
    settle(300)
    claim("a panel hosted in a1", lambda: hosted("a1", state["panels"]["last"].form))
    activate("b1")
    FreeCAD.closeDocument(DOC_A)


@step
def the_document_closed():
    check("closing the document closed its dialog", not Control.activeDialog())
    check("and left no host", hosts() == [], len(hosts()))
    check("the Tasks tab shows the watchers, and says nothing of a panel",
          shown() == "watchers" and hint() == [], (shown(), hint()))


def advance():
    if state["done"]:
        return
    if not steps:
        finish()
        return
    fn = steps.pop(0)
    del KEEP[:]
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
    VIEW.SetBool("TaskPanelInView", False)
    VIEW.SetBool("UseViewArea", True)
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
