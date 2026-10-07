"""A task panel beside its view, in a cell of its own (docs/TaskPanelPerView.md
sec 15.4, milestone 2).

A panel in its view stands over the picture, or -- chosen in the menu of
its header -- beside it: the view's cell is split, the panel takes the
new cell on the left, the right, the top or the bottom, and the view the
rest. The picture is not covered, the other cells of the view area keep
their place, and the handle between the two resizes the panel. Mode, side
and size are the view's (Task_Mode, Task_Side, Task_Size): its next panel
opens the same way, and a view that holds none starts from what was last
chosen.

The panel cell is no cell of the view area: a view split while its panel
is beside it gets the new cell outside the two, a maximized view keeps its
panel, the layout saved with the document does not know of it, and it
goes when the dialog closes, when the panel is put over the picture again,
and when its view is closed. A view in a tab of its own is put into a
view area for it.

A view asks for 400 pixels across at the least, so the test makes room:
the two views of its document one above the other, with the Python
console out of the way. With less room than the two ask for a pair takes
it from its neighbours and gives it back when it goes; that is measured
by hand, not here (docs/TaskPanelPerView.md sec 15.10).

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (c969119033), where a panel in
its view is over the picture and nothing else.
"""
import gc
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "TaskSide"
DOC_T = "TaskSideTab"
SAVED = os.path.join(OUT, "task-side.FCStd")
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
TASKS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TaskView")
LAST = TASKS.GetGroup("Host")
Control = FreeCADGui.Control
QTest = QtTest.QTest
LEFT = QtCore.Qt.LeftButton

state = {"done": False, "views": {}, "panels": {}, "doc": DOC, "was": {}}
steps = []
# See task-panel-in-view.py: a host found among all the application's
# widgets has nothing else to hold its wrapper
KEEP = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def claim(name, fn):
    """A check whose reading may raise: before the change most of them do."""
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


def views3d(doc=None):
    return FreeCADGui.getDocument(doc or state["doc"]).mdiViewsOfType("Gui::View3DInventor")


def activate(name):
    mw().setActiveWindow(state["views"][name])
    settle(350)


def widget_of(name):
    """The view's own widget. The binding now and then hands out the
    wrapper of a widget that is gone for one made where it stood -- a view
    opened after another was closed -- and calls it deleted; collecting
    the stale wrapper is what clears it."""
    for attempt in range(4):
        try:
            w = state["views"][name].graphicsView()
            while w is not None and cls(w) != "Gui::View3DInventor":
                w = w.parentWidget()
            return w
        except RuntimeError:
            if attempt == 3:
                raise
            w = None
            gc.collect()
            settle(50)
    return None


def cell_of(name):
    w = widget_of(name)
    while w is not None and cls(w) != "Gui::ViewAreaCell":
        w = w.parentWidget()
    return w


def rect(w):
    """Where a widget is, in the main window."""
    return QtCore.QRect(w.mapTo(mw(), QtCore.QPoint(0, 0)), w.size())


def cells():
    # Not mw().findChildren(QWidget): the binding adopts what that returns
    # as the main window's children for good, the widget inside each view
    # among them, and a view opened later where a closed one stood is then
    # handed the dead one's wrapper ("already deleted").
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == "Gui::ViewAreaCell" and w.isVisible():
                out.append(w)
        except RuntimeError:
            pass
    return out


def panel_cells():
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == "Gui::ViewAreaPanelCell" and w.isVisible():
                out.append(w)
        except RuntimeError:
            pass
    return out


def hosts():
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == "Gui::TaskView::TaskPanelHost" and not w.isHidden():
                out.append(w)
        except RuntimeError:
            pass
    return out


def host_of(name):
    """The view's host: over its cell, or in the panel cell beside it."""
    view, cell = widget_of(name), cell_of(name)
    for host in hosts():
        place = host.parentWidget()
        if place is None:
            continue
        over = place == view or place.isAncestorOf(view)
        beside = (cls(place) == "Gui::ViewAreaPanelCell" and cell is not None
                  and place.parentWidget() == cell.parentWidget())
        if over or beside:
            KEEP.append(host)
            return host
    return None


def pair_of(name):
    """The splitter that holds the view's cell and its panel cell, or None."""
    sp = cell_of(name).parentWidget()
    return sp if sp is not None and sp.objectName() == "viewAreaPanelPair" else None


def panel_of(name):
    """The panel cell beside the view, or None."""
    host = host_of(name)
    place = host.parentWidget() if host is not None else None
    return place if place is not None and cls(place) == "Gui::ViewAreaPanelCell" else None


def beside(name, key):
    """Whether the panel is shown in a cell of its own beside the view."""
    panel, form = panel_of(name), state["panels"][key].form
    return (panel is not None and pair_of(name) is not None
            and panel.isAncestorOf(form) and form.isVisible())


def over(name, key):
    """Whether the panel is shown over the view's cell."""
    host, form = host_of(name), state["panels"][key].form
    return (host is not None and host.parentWidget() == cell_of(name)
            and host.isAncestorOf(form) and form.isVisible())


def choose(name, what):
    """Trigger an entry of the menu in the header of the view's panel."""
    action = host_of(name).findChild(QtGui.QAction, "taskPanelHost" + what)
    action.trigger()
    settle(500)


def own(name, prop):
    view = state["views"][name]
    if prop not in view.PropertiesList:
        return None
    return getattr(view, prop)


class Panel:
    def __init__(self, title):
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(title)
        self.label = QtWidgets.QLabel("a line of " + title, self.form)
        self.edit = QtWidgets.QLineEdit(self.form)
        layout = QtWidgets.QVBoxLayout(self.form)
        for w in (self.label, self.edit):
            layout.addWidget(w)
        self.calls = []

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


def show(name, key):
    activate(name)
    panel = Panel(key)
    state["panels"][key] = panel
    try:
        Control.showDialog(panel)
    except RuntimeError as e:
        note("FAIL showing %s: %s" % (key, e))
    settle(500)
    return panel


def close(name):
    Control.closeDialog(view=state["views"][name])


def calls(key):
    return list(state["panels"][key].calls)


def close_any():
    for _ in range(4):
        if not Control.activeDialog():
            break
        Control.closeDialog()
        settle(100)


def logged():
    """What the report view holds: a warning of Qt's lands there, and
    brings the view up over whatever the test laid out."""
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == "Gui::DockWnd::ReportOutput":
                return w.toPlainText()
        except RuntimeError:
            pass
    return ""


def near(a, b, slack=3):
    return abs(a - b) <= slack


def same_rect(a, b, slack=3):
    return all(near(x, y, slack) for x, y in zip(a.getRect(), b.getRect()))


def where(name):
    """A cell is where it was: (whether, what it is and what it was)."""
    now, was = rect(cell_of(name)), state["was"][name]
    return same_rect(now, was), (now.getRect(), was.getRect())


def pair_where(name):
    """A view's pair fills the slot the view had."""
    now, was = rect(pair_of(name)), state["was"][name]
    return same_rect(now, was, 4), (now.getRect(), was.getRect())


def step(fn):
    steps.append(fn)
    return fn


@step
def build():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    VIEW.SetInt("RenderCache", 3)
    VIEW.SetBool("ShowNaviCube", False)
    VIEW.SetBool("UseNavigationAnimations", False)
    VIEW.SetBool("UseViewArea", True)
    VIEW.SetBool("TaskPanelInViewAll", False)
    VIEW.SetBool("TaskPanelInView", True)
    TASKS.SetBool("TaskPanelAllowConcurrent", True)
    LAST.RemString("Side")
    LAST.RemString("Mode")

    for dock in mw().findChildren(QtWidgets.QDockWidget):
        if dock.objectName() in ("Python console", "Report view"):
            dock.hide()
    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle()
    mw().setActiveWindow(views3d()[0])
    FreeCADGui.runCommand("Std_ViewSplitDown")
    settle(500)


@step
def name_views():
    v = views3d()
    if len(v) != 2:
        note("ABORT views: %d" % len(v))
        del steps[:]
        return
    state["views"] = {"a1": v[0], "a2": v[1]}
    if cell_of("a1") is None or cell_of("a2") is None or cell_of("a1") == cell_of("a2"):
        note("ABORT the two views are not two cells")
        del steps[:]
        return
    state["was"] = {"a1": rect(cell_of("a1")), "a2": rect(cell_of("a2"))}
    show("a1", "first")


@step
def beside_its_view():
    first = state["panels"]["first"]
    claim("a panel in its view starts over the picture", lambda: over("a1", "first"))
    claim("and its view's cell is as large as it was", lambda: where("a1"))
    first.edit.setText("kept")
    state["told"] = calls("first")
    claim("choosing 'beside the view' in its header's menu",
          lambda: choose("a1", "Beside") is None)
    claim("the panel stands in a cell of its own", lambda: beside("a1", "first"))
    check("not closed, nor told anything, and as it was left",
          calls("first") == state["told"] and first.edit.text() == "kept",
          (state["told"], calls("first")))

    def geometry():
        panel, cell, view = rect(panel_of("a1")), rect(cell_of("a1")), rect(widget_of("a1"))
        was = state["was"]["a1"]
        ok = (panel.right() < cell.left() + 2 and not panel.intersects(view)
              and near(panel.top(), was.top()) and near(panel.height(), was.height())
              and abs(panel.width() + cell.width() - was.width()) <= 12
              and near(panel.left(), was.left()) and near(cell.right(), was.right()))
        return ok, (panel.getRect(), cell.getRect(), was.getRect())

    claim("on the left of the view, not over it, the two filling the slot the view had",
          geometry)
    claim("the other cell is where it was", lambda: where("a2"))
    check("the view area still has its two cells", len(cells()) == 2, len(cells()))
    check("the view holds the mode", own("a1", "Task_Mode") == "Side", own("a1", "Task_Mode"))
    check("which is the last chosen", LAST.GetString("Mode", "") == "Side",
          LAST.GetString("Mode", ""))
    QTest.mouseClick(first.edit, LEFT)
    settle(200)
    QTest.keyClicks(first.edit, "+")
    settle(100)
    check("what is typed into it lands in it", first.edit.text() == "kept+", first.edit.text())


@step
def on_any_side():
    def side(what, test):
        def run():
            choose("a1", what)
            panel, cell = rect(panel_of("a1")), rect(cell_of("a1"))
            was = state["was"]["a1"]
            whole = panel.united(cell)
            ok = (beside("a1", "first") and test(panel, cell) and same_rect(whole, was, 4)
                  and own("a1", "Task_Side") == what)
            return ok, (panel.getRect(), cell.getRect(), own("a1", "Task_Side"))
        return run

    claim("to the right", side("Right", lambda p, c: p.left() > c.right() - 2))
    claim("to the top", side("Top", lambda p, c: p.bottom() < c.top() + 2
                             and near(p.width(), c.width())))
    claim("to the bottom", side("Bottom", lambda p, c: p.top() > c.bottom() - 2
                                and near(p.width(), c.width())))
    claim("and to the left again", side("Left", lambda p, c: p.right() < c.left() + 2))
    claim("the other cell never moved", lambda: where("a2"))
    check("the dialog was told nothing on the way", calls("first") == state["told"],
          (state["told"], calls("first")))


@step
def the_handle_resizes_it():
    def drag():
        pair, panel = pair_of("a1"), panel_of("a1")
        before = panel.width()
        handle = pair.handle(1)
        # The handle moves under the pointer: where the pointer is, is
        # told in the pair, which does not
        start = handle.mapTo(pair, handle.rect().center())
        QTest.mousePress(handle, LEFT, QtCore.Qt.NoModifier, handle.rect().center())
        for i in range(1, 7):
            QTest.mouseMove(handle, handle.mapFrom(pair, start + QtCore.QPoint(10 * i, 0)))
            settle(20)
        QTest.mouseRelease(handle, LEFT, QtCore.Qt.NoModifier,
                           handle.mapFrom(pair, start + QtCore.QPoint(60, 0)))
        settle(300)
        after = panel_of("a1").width()
        state["size"] = after
        return 40 <= after - before <= 80, (before, after)

    claim("the handle between the two, dragged, makes the panel wider", drag)
    claim("and the view holds the size",
          lambda: (own("a1", "Task_Size") == state["size"], own("a1", "Task_Size")))
    claim("the two still fill the slot", lambda: pair_where("a1"))
    check("still told nothing", calls("first") == state["told"], calls("first"))
    show("a2", "second")


@step
def a_view_with_none_of_its_own():
    claim("a panel opened afterwards in a view that holds no mode is beside it too",
          lambda: beside("a2", "second"))
    check("it was opened and activated, once", calls("second") == ["open", "activate"],
          calls("second"))
    check("the first was deactivated for it, once: its view is no longer the active one",
          calls("first") == state["told"] + ["deactivate"], calls("first"))
    check("and Qt had nothing to say about the task view's stack",
          "not contained in stack" not in logged(), logged()[-300:])
    check("that view holds none", own("a2", "Task_Mode") in (None, ""), own("a2", "Task_Mode"))
    claim("and the first is where it was",
          lambda: beside("a1", "first") and near(panel_of("a1").width(), state["size"]))
    close("a2")


@step
def split_with_its_panel_beside():
    check("the second dialog closed: its panel cell is gone",
          len(panel_cells()) == 1 and pair_of("a2") is None, len(panel_cells()))
    claim("and its view's cell is where it was", lambda: where("a2"))
    activate("a1")
    FreeCADGui.runCommand("Std_ViewSplitRight")
    settle(600)
    v = views3d()
    fresh = [x for x in v if x not in (state["views"]["a1"], state["views"]["a2"])]
    if not check("splitting the view gives a third cell", len(fresh) == 1 and len(cells()) == 3,
                 (len(v), len(cells()))):
        return
    state["views"]["a3"] = fresh[0]
    claim("the panel is still beside its own view", lambda: beside("a1", "first"))
    claim("the new cell is outside the two",
          lambda: cell_of("a3").parentWidget() != pair_of("a1")
          and not rect(cell_of("a3")).intersects(rect(pair_of("a1"))))
    claim("and they are to its left, panel and view side by side",
          lambda: rect(pair_of("a1")).right() < rect(cell_of("a3")).left() + 2)
    activate("a3")
    FreeCADGui.runCommand("Std_ViewSplitClose")
    settle(600)


@step
def maximized_with_its_panel():
    state["views"].pop("a3", None)
    check("the third cell closed", len(cells()) == 2, len(cells()))
    claim("the panel is beside its view", lambda: beside("a1", "first"))
    claim("the two filling the slot as before", lambda: pair_where("a1"))
    activate("a1")
    FreeCADGui.runCommand("Std_ViewSplitMaximize")
    settle(500)
    claim("the view maximized, its panel is still beside it",
          lambda: beside("a1", "first") and not cell_of("a2").isVisible())
    FreeCADGui.runCommand("Std_ViewSplitMaximize")
    settle(500)
    claim("and restored, the panel is beside it", lambda: beside("a1", "first"))
    claim("the two fill the slot", lambda: pair_where("a1"))
    claim("and the other cell is where it was", lambda: where("a2"))

    FreeCAD.getDocument(DOC).saveAs(SAVED)
    settle(300)

    claim("choosing 'over the view'", lambda: choose("a1", "Overlay") is None)
    claim("puts the panel over the picture again", lambda: over("a1", "first"))
    check("the panel cell is gone", panel_cells() == [] and pair_of("a1") is None,
          len(panel_cells()))
    claim("and the view has its slot back", lambda: where("a1"))
    check("the dialog is the same, and was neither accepted nor rejected",
          "accept" not in calls("first") and "reject" not in calls("first"), calls("first"))
    claim("beside it again", lambda: choose("a1", "Beside") is None and beside("a1", "first"))
    claim("at the size the view holds", lambda: near(panel_of("a1").width(), state["size"]))
    close("a1")


@step
def the_dialog_closed():
    check("the dialog closed: no host, no panel cell",
          hosts() == [] and panel_cells() == [], (len(hosts()), len(panel_cells())))
    claim("and the view has its slot back", lambda: where("a1"))
    show("a1", "third")
    claim("the next panel of that view is beside it at once, at the size it had",
          lambda: beside("a1", "third") and near(panel_of("a1").width(), state["size"]))
    show("a2", "fourth")
    claim("with one beside the other view too", lambda: beside("a2", "fourth"))
    activate("a2")
    FreeCADGui.runCommand("Std_ViewSplitClose")
    settle(600)


@step
def a_view_closed_under_its_panel():
    state["views"].pop("a2", None)
    check("the view closed under its panel: its dialog is closed",
          "reject" in calls("fourth"), calls("fourth"))
    check("one cell is left, and one panel cell", len(cells()) == 1 and len(panel_cells()) == 1,
          (len(cells()), len(panel_cells())))
    claim("the other view's panel is beside it still", lambda: beside("a1", "third"))
    close("a1")


@step
def a_view_in_a_tab_of_its_own():
    VIEW.SetBool("UseViewArea", False)
    doc = FreeCAD.newDocument(DOC_T)
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle(500)
    VIEW.SetBool("UseViewArea", True)
    v = views3d(DOC_T)
    state["views"]["t1"] = v[0]
    check("a view of a document opened with tiling off is in no cell", cell_of("t1") is None)
    show("t1", "tab")


@step
def is_put_into_a_view_area():
    claim("its panel is beside it, in a view area made for it",
          lambda: cell_of("t1") is not None and beside("t1", "tab"))
    close("t1")


@step
def both_documents_closed():
    state["views"] = {}
    FreeCAD.closeDocument(DOC_T)
    FreeCAD.closeDocument(DOC)


@step
def reopened():
    # Saved with a panel beside the first view
    state["doc"] = FreeCAD.openDocument(SAVED).Name
    settle(1500)


@step
def the_saved_layout_knows_no_panel():
    v = views3d()
    if not check("the document came back with its two views", len(v) == 2, len(v)):
        return
    state["views"] = {"a1": v[0], "a2": v[1]}
    check("in two cells, with no panel cell", len(cells()) == 2 and panel_cells() == [],
          (len(cells()), len(panel_cells())))
    sizes = sorted(rect(cell_of(n)).size().toTuple() for n in ("a1", "a2"))
    was = sorted(state["was"][n].size().toTuple() for n in ("a1", "a2"))
    check("as large as they were before a panel stood beside one",
          all(abs(a[0] - b[0]) <= 8 and abs(a[1] - b[1]) <= 8 for a, b in zip(sizes, was)),
          (sizes, was))
    modes = [own(n, "Task_Mode") for n in ("a1", "a2")]
    check("one of the views holds the mode", "Side" in modes, modes)
    check("nothing in the whole of it made Qt warn about the stack",
          "not contained in stack" not in logged(), logged()[-300:])


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
    VIEW.SetBool("UseViewArea", True)
    VIEW.SetBool("TaskPanelInViewAll", False)
    VIEW.SetBool("TaskPanelInView", False)
    TASKS.SetBool("TaskPanelAllowConcurrent", False)
    LAST.RemString("Side")
    LAST.RemString("Mode")
    try:
        close_any()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, advance)
