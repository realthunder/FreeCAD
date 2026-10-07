"""A task panel over its view looks and behaves as an overlaid dock does
(docs/TaskPanelPerView.md sec 15.5, milestone 3).

Over the picture a panel has the dock overlay's look: no ground of its
own, the overlay's style sheet, the overlay's title bar and buttons, a
page whose widgets are translucent with the task boxes left as they are.
It is the whole length of its side, on any of the four sides -- chosen in
the menu of its title bar, or by dragging the title bar across or down --
and a grip along its inner edge resizes it; the view keeps the side and
the size.

What is between and beside the boxes is not the panel's: nothing is drawn
there, the widget under a point there is the view's, and a turn of the
wheel there zooms the view, where over a box it does not.

Beside its view, and back in the combo view, the panel is a plain one
again: the look is taken off the page when it leaves.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (50560fcf27), where a panel over
its view is an opaque box as tall as its content, on the left or the
right.
"""
import ctypes
import ctypes.util
import gc
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "TaskOverlay"
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
TASKS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TaskView")
LAST = TASKS.GetGroup("Host")
Control = FreeCADGui.Control
QTest = QtTest.QTest
LEFT = QtCore.Qt.LeftButton
TRANSLUCENT = QtCore.Qt.WA_TranslucentBackground

state = {"done": False, "views": {}, "panels": {}, "told": []}
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


def views3d():
    return FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")


def activate(name):
    mw().setActiveWindow(state["views"][name])
    settle(350)


def widget_of(name):
    """The view's own widget (see task-panel-side.py for the retry)."""
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


def all_widgets(name):
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == name:
                out.append(w)
        except RuntimeError:
            pass
    return out


def hosts():
    return [w for w in all_widgets("Gui::TaskView::TaskPanelHost") if not w.isHidden()]


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


def over(name, key):
    host, form = host_of(name), state["panels"][key].form
    return (host is not None and host.parentWidget() == cell_of(name)
            and host.isAncestorOf(form) and form.isVisible())


def child(name, object_name):
    return host_of(name).findChild(QtWidgets.QWidget, object_name)


def choose(name, what):
    action = host_of(name).findChild(QtGui.QAction, "taskPanelHost" + what)
    action.trigger()
    settle(500)


def own(name, prop):
    view = state["views"][name]
    if prop not in view.PropertiesList:
        return None
    return getattr(view, prop)


def ancestor(widget, names, stop):
    """The nearest ancestor of a widget whose class is one of names."""
    w = widget.parentWidget()
    while w is not None and w != stop:
        if cls(w) in names:
            return w
        w = w.parentWidget()
    return None


def page_of(key):
    """The page a panel's form is on, wherever that is."""
    return ancestor(state["panels"][key].form, ("Gui::TaskView::TaskPage",), None)


def box_of(key):
    """The task box a panel's form is in."""
    form = state["panels"][key].form
    return ancestor(form, ("Gui::TaskView::TaskBox", "QSint::ActionGroup"), None) or form


def ground(name, key):
    """A point of the host, in the host, that is clear of the panel's box:
    half way between the box and the far end of the host, along the host."""
    host, box = host_of(name), box_of(key)
    r = QtCore.QRect(box.mapTo(host, QtCore.QPoint(0, 0)), box.size())
    if host.height() - r.bottom() > 60:
        return QtCore.QPoint(host.width() // 2, (r.bottom() + host.height()) // 2)
    return QtCore.QPoint((r.right() + host.width()) // 2, host.height() // 2)


def alpha(name, point):
    return host_of(name).grab().toImage().pixelColor(point).alpha()


def under(name, point):
    """The widget under a point of the host, as the application sees it."""
    return QtWidgets.QApplication.widgetAt(host_of(name).mapToGlobal(point))


def is_views(name, widget):
    view, host = widget_of(name), host_of(name)
    return (widget is not None and not (widget == host or host.isAncestorOf(widget))
            and (widget == view or view.isAncestorOf(widget)))


def wheel_at(global_point, clicks=3):
    """Turn the real pointer's wheel, through the window system (XTEST)."""
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
        raise RuntimeError("no X display to turn a wheel on")
    try:
        xtst.XTestFakeMotionEvent(display, -1, global_point.x(), global_point.y(), 0)
        x11.XSync(display, 0)
        settle(200)
        for _ in range(clicks):
            xtst.XTestFakeButtonEvent(display, 4, 1, 0)
            xtst.XTestFakeButtonEvent(display, 4, 0, 0)
            x11.XSync(display, 0)
            settle(120)
    finally:
        x11.XCloseDisplay(display)
    settle(400)


def camera(name):
    return state["views"][name].getCamera()


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


def calls(key):
    return list(state["panels"][key].calls)


def close_any():
    for _ in range(4):
        if not Control.activeDialog():
            break
        Control.closeDialog()
        settle(100)


def drag(widget, frame, to, steps_=8):
    """Drag a widget that may move under the pointer: where the pointer
    goes is told in a frame that does not."""
    start = widget.mapTo(frame, widget.rect().center())
    QTest.mousePress(widget, LEFT, QtCore.Qt.NoModifier, widget.rect().center())
    for i in range(1, steps_ + 1):
        p = start + (to - start) * i / steps_
        QTest.mouseMove(widget, widget.mapFrom(frame, QtCore.QPoint(int(p.x()), int(p.y()))))
        settle(20)
    QTest.mouseRelease(widget, LEFT, QtCore.Qt.NoModifier, widget.mapFrom(frame, to))
    settle(400)


def placed(name, side):
    """Whether the host is the whole length of a side of its cell."""
    g, cell = host_of(name).geometry(), cell_of(name)
    w, h = cell.width(), cell.height()
    long_ways = g.top() <= 26 and g.bottom() >= h - 26
    wide_ways = g.left() <= 8 and g.right() >= w - 8
    ok = {
        "Left": long_ways and g.left() <= 8 and g.right() < w * 3 // 4,
        "Right": long_ways and g.right() >= w - 8 and g.left() > w // 4,
        "Top": wide_ways and g.top() <= 26 and g.bottom() < h * 3 // 4,
        "Bottom": wide_ways and g.bottom() >= h - 26 and g.top() > h // 4,
    }[side]
    return ok, (g.getRect(), (w, h))


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
    show("a1", "first")


@step
def the_look_of_an_overlaid_dock():
    first = state["panels"]["first"]
    state["told"] = calls("first")
    claim("a panel in its view is over the picture", lambda: over("a1", "first"))
    claim("its host has no ground of its own",
          lambda: not host_of("a1").autoFillBackground()
          and host_of("a1").testAttribute(TRANSLUCENT))
    claim("and wears the dock overlay's style sheet",
          lambda: "OverlayTitleBar" in host_of("a1").styleSheet())
    claim("its title bar is the overlay's",
          lambda: (cls(child("a1", "taskPanelHostHeader")) == "Gui::OverlayTitleBar",
                   cls(child("a1", "taskPanelHostHeader"))))
    claim("with the overlay's buttons",
          lambda: [cls(child("a1", n)) for n in ("taskPanelHostMenu", "taskPanelHostToCombo")]
          == ["Gui::OverlayToolButton"] * 2)
    claim("the page's own ground is translucent",
          lambda: page_of("first").testAttribute(TRANSLUCENT))
    claim("and a task box is left as it is",
          lambda: (not box_of("first").testAttribute(TRANSLUCENT), cls(box_of("first"))))
    claim("it is the whole length of the left side of its view", lambda: placed("a1", "Left"))
    claim("and no wider than a third of it",
          lambda: (host_of("a1").width() <= max(240, cell_of("a1").width() // 3) + 8,
                   host_of("a1").width()))

    claim("nothing is drawn on the ground clear of its box",
          lambda: (alpha("a1", ground("a1", "first")) == 0, alpha("a1", ground("a1", "first"))))
    claim("the title bar is drawn, and lets the picture through",
          lambda: (0 < alpha("a1", QtCore.QPoint(40, 6)) < 255, alpha("a1", QtCore.QPoint(40, 6))))
    claim("a point on the clear ground is the view's, not the panel's",
          lambda: (is_views("a1", under("a1", ground("a1", "first"))),
                   cls(under("a1", ground("a1", "first")))))
    claim("a point on the box is the panel's",
          lambda: host_of("a1").isAncestorOf(
              QtWidgets.QApplication.widgetAt(first.edit.mapToGlobal(first.edit.rect().center()))))

    def zooms():
        before = camera("a1")
        wheel_at(host_of("a1").mapToGlobal(ground("a1", "first")))
        return camera("a1") != before

    claim("a turn of the wheel on the clear ground zooms the view", zooms)

    def scrolls_not():
        before = camera("a1")
        wheel_at(first.label.mapToGlobal(first.label.rect().center()))
        return camera("a1") == before

    claim("and on the box it does not", scrolls_not)
    QTest.mouseClick(first.edit, LEFT)
    settle(200)
    QTest.keyClicks(first.edit, "typed")
    settle(100)
    check("what is typed into the panel lands in it", first.edit.text() == "typed",
          first.edit.text())


@step
def on_any_side():
    def side(what):
        def run():
            choose("a1", what)
            ok, detail = placed("a1", what)
            return (ok and over("a1", "first") and own("a1", "Task_Side") == what,
                    (detail, own("a1", "Task_Side")))
        return run

    claim("to the right", side("Right"))
    claim("to the top, the whole width", side("Top"))
    claim("to the bottom", side("Bottom"))
    claim("and to the left again", side("Left"))
    claim("the ground is clear on the left as it was",
          lambda: alpha("a1", ground("a1", "first")) == 0
          and is_views("a1", under("a1", ground("a1", "first"))))
    check("the dialog was told nothing by any of it", calls("first") == state["told"],
          (state["told"], calls("first")))


@step
def the_title_bar_drags_it():
    def to(point_of, side):
        def run():
            cell = cell_of("a1")
            drag(child("a1", "taskPanelHostHeader"), cell, point_of(cell))
            ok, detail = placed("a1", side)
            return ok and own("a1", "Task_Side") == side, (detail, own("a1", "Task_Side"))
        return run

    claim("dragged across by its title bar, it settles on the right",
          to(lambda c: QtCore.QPoint(c.width() - 40, 30), "Right"))
    claim("dragged down, on the bottom",
          to(lambda c: QtCore.QPoint(c.width() - 60, c.height() - 30), "Bottom"))
    claim("and back by the menu to the left",
          lambda: choose("a1", "Left") is None and placed("a1", "Left")[0])
    check("the last chosen side is the left", LAST.GetString("Side", "") == "Left",
          LAST.GetString("Side", ""))


@step
def the_grip_resizes_it():
    def grip_at_the_inner_edge():
        host, grip = host_of("a1"), child("a1", "taskPanelHostGrip")
        g = grip.geometry()
        return (grip.isVisible() and g.right() >= host.width() - 2 and g.height() >= host.height() - 2,
                (g.getRect(), host.size().toTuple()))

    claim("a grip runs down its inner edge", grip_at_the_inner_edge)

    def wider():
        host, grip, cell = host_of("a1"), child("a1", "taskPanelHostGrip"), cell_of("a1")
        before = host.width()
        to = grip.mapTo(cell, grip.rect().center()) + QtCore.QPoint(60, 0)
        drag(grip, cell, to, 6)
        state["size"] = host_of("a1").width()
        return 50 <= state["size"] - before <= 70, (before, state["size"])

    claim("dragged 60 pixels, the panel is 60 wider", wider)
    claim("and the view holds the size",
          lambda: (own("a1", "Task_Size") == state["size"], own("a1", "Task_Size")))
    claim("still the whole length of the left side", lambda: placed("a1", "Left"))


@step
def a_plain_panel_elsewhere():
    first = state["panels"]["first"]
    claim("beside its view", lambda: choose("a1", "Beside") is None
          and cls(host_of("a1").parentWidget()) == "Gui::ViewAreaPanelCell")
    check("the dialog was told nothing by going there", calls("first") == state["told"],
          calls("first"))
    claim("it has a ground of its own and no overlay style sheet",
          lambda: host_of("a1").autoFillBackground() and host_of("a1").styleSheet() == ""
          and not host_of("a1").testAttribute(TRANSLUCENT))
    claim("its page is a plain page", lambda: not page_of("first").testAttribute(TRANSLUCENT))
    claim("all of it is the panel's: no mask, no grip",
          lambda: host_of("a1").mask().isEmpty()
          and (child("a1", "taskPanelHostGrip") is None
               or not child("a1", "taskPanelHostGrip").isVisible()))
    claim("and it is drawn all over",
          lambda: (alpha("a1", QtCore.QPoint(host_of("a1").width() // 2,
                                             host_of("a1").height() - 10)) == 255,
                   alpha("a1", QtCore.QPoint(host_of("a1").width() // 2,
                                             host_of("a1").height() - 10))))
    claim("over the view again, it has the overlay's look back",
          lambda: choose("a1", "Overlay") is None and over("a1", "first")
          and host_of("a1").testAttribute(TRANSLUCENT) and page_of("first").testAttribute(TRANSLUCENT)
          and alpha("a1", ground("a1", "first")) == 0)
    check("nor by coming back", calls("first") == state["told"], calls("first"))
    claim("at the size the view holds",
          lambda: (abs(host_of("a1").width() - state["size"]) <= 2,
                   (host_of("a1").width(), state.get("size"))))
    claim("sent to the combo view by its title bar's button",
          lambda: QTest.mouseClick(child("a1", "taskPanelHostToCombo"), LEFT) is None)
    settle(500)
    check("no host is left", hosts() == [], len(hosts()))
    claim("in the Tasks tab its page is a plain page",
          lambda: (first.form.isVisible() and not page_of("first").testAttribute(TRANSLUCENT),
                   cls(page_of("first").parentWidget())))
    check("nor by going to the combo view: neither closed nor told anything",
          calls("first") == state["told"], (state["told"], calls("first")))
    show("a2", "second")


@step
def a_later_panel():
    claim("a panel opened afterwards in the other view is over it, on the left",
          lambda: over("a2", "second") and placed("a2", "Left")[0])
    claim("with the overlay's look",
          lambda: host_of("a2").testAttribute(TRANSLUCENT)
          and alpha("a2", ground("a2", "second")) == 0)
    claim("at what its panel asks for, not at the first view's size",
          lambda: (host_of("a2").width() <= max(240, cell_of("a2").width() // 3) + 8,
                   host_of("a2").width()))
    logged = ""
    for w in all_widgets("Gui::DockWnd::ReportOutput"):
        logged = w.toPlainText()
    check("Qt had nothing to say about any of it", "QWidget" not in logged
          and "QLayout" not in logged and "not contained" not in logged, logged[-400:])


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
