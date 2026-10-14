"""Where a task panel goes is a state of its view (docs/TaskPanelPerView.md
sec 15, milestone 1).

The preference View/TaskPanelInView is for the panels opened from now on:
one that is open stays where it is, and the next one of a view that holds
no place of its own goes by it. The button on a panel's own title bar,
and the one on the combo view's, act on that panel and its view alone;
the view keeps the place (its Task_Place property) and the next panel of
that view follows it, whatever the preference says. "Apply to live views",
the button beside the preference (Control.applyPlaceToViews from here),
makes every view follow the preference at once: the open panels move, and
a view's own choice is given up.

The side a panel is on in its view is the view's too (Task_Side): dragged
to the other side, the view keeps it and it is remembered as the last
chosen, which a view with no side of its own starts from when its next
panel opens -- a panel that is open is not moved by it. The properties can
be set as any property is, and move the panel of their view. And they are
saved with the view: a document reopened gives the view its place back.

There is no button that folds a panel to its header any more.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (3b38952724), where the
preference moves every panel and the buttons are the preference.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "TaskPlace"
SAVED = os.path.join(OUT, "task-place.FCStd")
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
TASKS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TaskView")
LAST = TASKS.GetGroup("Host")
Control = FreeCADGui.Control
QTest = QtTest.QTest
LEFT = QtCore.Qt.LeftButton

state = {"done": False, "views": {}, "panels": {}, "doc": DOC}
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
    """A check whose reading may raise."""
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


def views3d():
    return FreeCADGui.getDocument(state["doc"]).mdiViewsOfType("Gui::View3DInventor")


def activate(name):
    mw().setActiveWindow(state["views"][name])
    settle(350)


def widget_of(name):
    w = state["views"][name].graphicsView()
    while w is not None and cls(w) != "Gui::View3DInventor":
        w = w.parentWidget()
    return w


def cell_of(name):
    w = widget_of(name)
    while w is not None and cls(w) != "Gui::ViewAreaCell":
        w = w.parentWidget()
    return w


def task_view():
    for w in mw().findChildren(QtWidgets.QWidget):
        try:
            if cls(w) == "Gui::TaskView::TaskView":
                return w
        except RuntimeError:
            pass
    return None


def shown():
    stack = task_view().findChild(QtWidgets.QStackedWidget)
    return "dialog" if cls(stack.currentWidget()) == "Gui::TaskView::TaskPage" else "watchers"


def in_tab(name, key):
    """Whether the panel is in the Tasks tab, which shows it for its view."""
    activate(name)
    tv, form = task_view(), state["panels"][key].form
    try:
        return shown() == "dialog" and tv.isAncestorOf(form) and form.isVisibleTo(tv)
    except RuntimeError:
        return False


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
    view = widget_of(name)
    for host in hosts():
        place = host.parentWidget()
        if place is not None and (place == view or place.isAncestorOf(view)):
            KEEP.append(host)
            return host
    return None


def in_its_view(name, key):
    """Whether the panel is in the view's host, and shown there."""
    host, form = host_of(name), state["panels"][key].form
    try:
        return host is not None and host.isAncestorOf(form) and form.isVisible()
    except RuntimeError:
        return False


def on_right(name):
    host, cell = host_of(name), cell_of(name)
    return host.geometry().center().x() > cell.width() / 2


def host_button(name, which):
    return host_of(name).findChild(QtWidgets.QAbstractButton, which)


def title_button():
    for b in mw().findChildren(QtWidgets.QAbstractButton, "OBTN TaskHost"):
        if b.isVisible():
            return b
    return None


def own(name, prop):
    """What the view holds of its own under a property; None when it has
    no such property."""
    view = state["views"][name]
    if prop not in view.PropertiesList:
        return None
    return getattr(view, prop)


def nothing(value):
    return value in (None, "")


def keep(name, prop, value):
    """Set a property of the view as a script would."""
    view = state["views"][name]
    if prop not in view.PropertiesList:
        view.addProperty("App::PropertyString", prop)
    setattr(view, prop, value)
    settle(400)


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

    def accept(self):
        self.calls.append("accept")
        return True

    def reject(self):
        self.calls.append("reject")
        return True


def show(name, key):
    """Show a panel for a view, and remember it."""
    activate(name)
    panel = Panel(key)
    state["panels"][key] = panel
    try:
        Control.showDialog(panel)
    except RuntimeError as e:
        note("FAIL showing %s: %s" % (key, e))
    settle(400)
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


def drag_to(name, right):
    """Drag the host by its header to one side of its cell."""
    host, cell = host_of(name), cell_of(name)
    grip = host.findChild(QtWidgets.QWidget, "taskPanelHostHeader")
    start = QtCore.QPoint(30, grip.height() // 2)
    to = QtCore.QPoint(cell.width() - 40 if right else 40, 30)
    QTest.mousePress(grip, LEFT, QtCore.Qt.NoModifier, start)
    for i in range(1, 9):
        far = grip.mapFrom(cell, to)
        p = start + (far - start) * i / 8
        QTest.mouseMove(grip, QtCore.QPoint(int(p.x()), int(p.y())))
        settle(20)
    QTest.mouseRelease(grip, LEFT, QtCore.Qt.NoModifier, grip.mapFrom(cell, to))
    settle(300)


def step(fn):
    steps.append(fn)
    return fn


@step
def build():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    VIEW.SetInt("RenderCache", 3)
    VIEW.SetBool("ShowNaviCube", False)
    VIEW.SetBool("UseNavigationAnimations", False)
    VIEW.SetBool("TaskPanelInView", False)
    TASKS.SetBool("TaskPanelAllowConcurrent", True)
    LAST.RemString("Side")
    LAST.RemString("Mode")

    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle()
    mw().setActiveWindow(views3d()[0])
    FreeCADGui.runCommand("Std_ViewCreate")
    settle(500)


@step
def name_views():
    v = views3d()
    if len(v) != 2 or task_view() is None:
        note("ABORT views: %d" % len(v))
        del steps[:]
        return
    state["views"] = {"a1": v[0], "a2": v[1]}
    if cell_of("a1") is None or cell_of("a2") is None or cell_of("a1") == cell_of("a2"):
        note("ABORT the two views are not two cells")
        del steps[:]
        return
    show("a1", "first")


@step
def the_preference_is_for_the_panels_opened_afterwards():
    check("the preference off: a panel is in the Tasks tab", in_tab("a1", "first"))
    state["panels"]["first"].edit.setText("kept")
    VIEW.SetBool("TaskPanelInView", True)
    settle(400)
    check("the preference turned on, the panel that is open stays where it is",
          in_tab("a1", "first") and hosts() == [], len(hosts()))
    check("as it was left", state["panels"]["first"].edit.text() == "kept"
          and calls("first") == ["open"], calls("first"))
    show("a2", "second")
    claim("a panel opened afterwards is in its view", lambda: in_its_view("a2", "second"))
    check("and the first is still in the Tasks tab", in_tab("a1", "first"))
    check("neither view holds a place of its own",
          nothing(own("a1", "Task_Place")) and nothing(own("a2", "Task_Place")),
          (own("a1", "Task_Place"), own("a2", "Task_Place")))
    close("a1")


@step
def the_next_panel_of_the_first_view():
    show("a1", "third")
    claim("the next panel of the first view is in its view", lambda: in_its_view("a1", "third"))
    claim("no button folds a panel to its header",
          lambda: host_button("a1", "taskPanelHostCollapse") is None)
    claim("clicking the button of its header that sends it to the combo view",
          lambda: QTest.mouseClick(host_button("a1", "taskPanelHostToCombo"), LEFT) is None)
    settle(400)
    check("the panel is in the Tasks tab", in_tab("a1", "third"))
    check("it was not closed, nor told anything", calls("third") == ["open"], calls("third"))
    claim("the other view's panel stays in its view", lambda: in_its_view("a2", "second"))
    check("the preference is as it was", VIEW.GetBool("TaskPanelInView", False) is True)
    check("the view holds the place", own("a1", "Task_Place") == "ComboView",
          own("a1", "Task_Place"))
    check("the other view holds none", nothing(own("a2", "Task_Place")), own("a2", "Task_Place"))
    close("a1")


@step
def the_view_keeps_its_place():
    show("a1", "fourth")
    check("the next panel of that view follows what the view holds, not the preference",
          in_tab("a1", "fourth") and host_of("a1") is None)
    activate("a1")
    check("the combo view's title bar has a button", title_button() is not None)
    claim("which is no switch", lambda: not title_button().isCheckable())
    claim("clicking it", lambda: QTest.mouseClick(title_button(), LEFT) is None)
    settle(400)
    claim("sends the panel in front of it into its view", lambda: in_its_view("a1", "fourth"))
    check("not closed, nor told anything", calls("fourth") == ["open"], calls("fourth"))
    check("the view holds the place", own("a1", "Task_Place") == "InView",
          own("a1", "Task_Place"))
    check("the preference is as it was", VIEW.GetBool("TaskPanelInView", False) is True)

    VIEW.SetBool("TaskPanelInView", False)
    settle(400)
    claim("the preference turned off, both open panels stay in their views",
          lambda: in_its_view("a1", "fourth") and in_its_view("a2", "second"))
    close("a1")
    close("a2")


@step
def each_view_by_its_own():
    show("a2", "fifth")
    check("a view with no place of its own: its next panel goes by the preference",
          in_tab("a2", "fifth") and host_of("a2") is None)
    show("a1", "sixth")
    claim("a view that holds a place: its next panel goes by that",
          lambda: in_its_view("a1", "sixth"))

    Control.applyPlaceToViews()
    settle(400)
    check("applied to the live views, every panel goes by the preference",
          in_tab("a1", "sixth") and hosts() == [], len(hosts()))
    check("not closed, nor told anything", calls("sixth") == ["open"], calls("sixth"))
    check("and the view gave its own place up", nothing(own("a1", "Task_Place")),
          own("a1", "Task_Place"))
    VIEW.SetBool("TaskPanelInView", True)
    settle(400)
    claim("the preference changed after that moves no open panel by itself",
          lambda: in_tab("a1", "sixth") and hosts() == [])
    Control.applyPlaceToViews()
    settle(400)
    claim("and applied again it moves them",
          lambda: in_its_view("a1", "sixth") and in_its_view("a2", "fifth"))


def preferences():
    for w in QtWidgets.QApplication.topLevelWidgets():
        if cls(w) == "Gui::Dialog::DlgPreferencesImp" and w.isVisible():
            return w
    return None


def on_the_page():
    """Inside the preferences dialog, on Display > UI, settings stored at OK only."""
    dlg = preferences()
    try:
        if not check("the preferences are open", dlg is not None):
            return
        box = None
        for w in dlg.findChildren(QtWidgets.QCheckBox):
            if bytes(w.property("prefEntry") or b"").decode() == "TaskPanelInView":
                box = w
        button = dlg.findChild(QtWidgets.QPushButton, "applyTaskPanelInView")
        if not check("the page has the check box and, beside it, 'Apply to live views'",
                     box is not None and button is not None and button.isVisible()
                     and button.parentWidget() == box.parentWidget()
                     and abs(button.geometry().center().y() - box.geometry().center().y()) < 12,
                     button.text() if button else "no button"):
            return
        check("no second check box about the panels that are open",
              not [w for w in dlg.findChildren(QtWidgets.QCheckBox)
                   if "panels that are open" in w.text()])
        box.setChecked(False)
        settle(300)
        check("the box unchecked, not stored yet: nothing has moved",
              VIEW.GetBool("TaskPanelInView", False) is True
              and in_its_view("a1", "sixth") and in_its_view("a2", "fifth"))
        button.click()
        settle(500)
        check("the button stores the setting", VIEW.GetBool("TaskPanelInView", True) is False)
        check("and every open panel goes by it", in_tab("a1", "sixth") and hosts() == [],
              len(hosts()))
        check("not closed, nor told anything", calls("sixth") == ["open"], calls("sixth"))
        box.setChecked(True)
        button.click()
        settle(500)
        claim("checked and pressed again, the panels are back in their views",
              lambda: in_its_view("a1", "sixth") and in_its_view("a2", "fifth"))
    finally:
        if dlg is not None:
            dlg.close()


@step
def the_button_on_the_preference_page():
    """docs/HandsOnQueue.md entry 70: "make it a button instead, with label
    some thing like 'apply to live views'", and "button shall commit relevant
    settings (in case auto apply is not on) and then apply to views"."""
    general = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/General")
    general.SetBool("AutoApplyPreference", False)
    QtCore.QTimer.singleShot(2000, on_the_page)
    # modal: returns when on_the_page has closed the dialog
    FreeCADGui.showPreferences("Display", 1)
    general.RemBool("AutoApplyPreference")
    check("the preference is on again, as the steps after this expect",
          VIEW.GetBool("TaskPanelInView", False) is True)


@step
def the_side_is_the_views():
    claim("both panels start on the left", lambda: not on_right("a1") and not on_right("a2"))
    claim("dragging the first to the right", lambda: drag_to("a1", True) is None)
    claim("it stays there", lambda: on_right("a1"))
    check("the view holds the side", own("a1", "Task_Side") == "Right", own("a1", "Task_Side"))
    check("it is remembered as the last chosen", LAST.GetString("Side", "") == "Right",
          LAST.GetString("Side", ""))
    claim("the other panel, which is open, stays on the left", lambda: not on_right("a2"))
    check("its view holds no side", nothing(own("a2", "Task_Side")), own("a2", "Task_Side"))
    close("a2")


@step
def a_later_panel_starts_from_the_last_chosen():
    show("a2", "seventh")
    claim("the next panel of the view with no side of its own is on the right",
          lambda: in_its_view("a2", "seventh") and on_right("a2"))
    keep("a1", "Task_Side", "Left")
    claim("the side set as a property moves that view's panel", lambda: not on_right("a1"))
    claim("and not the other's", lambda: on_right("a2"))
    check("nor what was last chosen", LAST.GetString("Side", "") == "Right",
          LAST.GetString("Side", ""))

    keep("a2", "Task_Place", "ComboView")
    check("the place set as a property moves that view's panel",
          in_tab("a2", "seventh") and host_of("a2") is None)
    claim("and not the other's", lambda: in_its_view("a1", "sixth"))
    keep("a2", "Task_Place", "InView")
    claim("and back", lambda: in_its_view("a2", "seventh"))
    close("a1")
    close("a2")


@step
def saved_with_the_view():
    close_any()
    VIEW.SetBool("TaskPanelInView", False)
    keep("a1", "Task_Place", "InView")
    keep("a2", "Task_Place", "")
    doc = FreeCAD.getDocument(DOC)
    doc.saveAs(SAVED)
    settle(300)
    state["views"] = {}
    FreeCAD.closeDocument(DOC)


@step
def reopened():
    # Named after its file now
    state["doc"] = FreeCAD.openDocument(SAVED).Name
    settle(1500)


@step
def the_view_has_its_place_back():
    v = views3d()
    if not check("the document came back with its two views", len(v) == 2, len(v)):
        return
    state["views"] = {"a1": v[0], "a2": v[1]}
    places = sorted(str(own(n, "Task_Place")) for n in ("a1", "a2"))
    check("one of them holds its place", "InView" in places, places)
    check("and its side", "Left" in (own("a1", "Task_Side"), own("a2", "Task_Side")),
          (own("a1", "Task_Side"), own("a2", "Task_Side")))
    if own("a2", "Task_Place") == "InView":
        state["views"] = {"a1": v[1], "a2": v[0]}
    show("a1", "eighth")
    claim("a panel opened in it is in the view, with the preference off",
          lambda: in_its_view("a1", "eighth"))
    show("a2", "ninth")
    check("and the other view's is in the Tasks tab",
          in_tab("a2", "ninth") and host_of("a2") is None)


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
