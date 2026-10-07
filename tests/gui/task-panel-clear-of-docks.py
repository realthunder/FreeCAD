"""A task panel over its view stands clear of the overlaid docks
(docs/TaskPanelPerView.md sec 5.3; the last part of sec 15.7's milestone 4).

"Under view attachment mode, the panels must be aware of overlay panel to
not overlap." With the docks laid over the window -- the tree on the left,
the combo view on the right, the Python console along the bottom -- a
panel over a view that reaches those edges begins where the dock ends, on
whichever side it is put, and has its room back when the docks are docks
again. Its dialog is told nothing by any of it.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (7a45fa84b4), where the panel
is laid out in its cell as if no dock were over it.
"""
import gc
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "TaskClear"
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
TASKS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TaskView")
LAST = TASKS.GetGroup("Host")
Control = FreeCADGui.Control

state = {"done": False, "views": {}, "panels": {}, "told": []}
steps = []
KEEP = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def claim(name, fn):
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


def widget_of(name):
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


def host_of(name):
    view = widget_of(name)
    for host in all_widgets("Gui::TaskView::TaskPanelHost"):
        place = host.parentWidget()
        if host.isHidden() or place is None:
            continue
        if place == view or place.isAncestorOf(view):
            KEEP.append(host)
            return host
    return None


def on_screen(w):
    """Where a widget is, on the screen."""
    return QtCore.QRect(w.mapToGlobal(QtCore.QPoint(0, 0)), w.size())


def docks():
    """The overlaid docks that are shown: {side: where, on the screen}."""
    out = {}
    for w in all_widgets("Gui::OverlayTabWidget"):
        if w.isVisible() and w.count():
            out[w.objectName().replace("Overlay", "")] = on_screen(w)
    return out


def choose(name, what):
    host_of(name).findChild(QtGui.QAction, "taskPanelHost" + what).trigger()
    settle(700)


def clear(name):
    """Whether the host touches none of the overlaid docks."""
    host = on_screen(host_of(name))
    hit = [side for side, r in docks().items() if r.intersects(host)]
    return not hit, (host.getRect(), {s: r.getRect() for s, r in docks().items()})


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


def calls(key):
    return list(state["panels"][key].calls)


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
    LAST.RemString("Side")
    LAST.RemString("Mode")
    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle(500)


@step
def a_panel_over_the_view():
    v = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")
    state["views"] = {"a1": v[0]}
    if cell_of("a1") is None:
        note("ABORT the view is in no cell")
        del steps[:]
        return
    mw().setActiveWindow(v[0])
    settle(300)
    panel = Panel("first")
    state["panels"]["first"] = panel
    Control.showDialog(panel)
    settle(600)
    state["told"] = calls("first")
    claim("with the docks docked, the panel is at the left edge of its view",
          lambda: (host_of("a1").x() <= 8, host_of("a1").geometry().getRect()))
    check("and no dock is overlaid", docks() == {}, list(docks()))
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(1800)


@step
def clear_of_them_on_every_side():
    d = docks()
    if not check("the docks are laid over the window: left, right and bottom",
                 all(s in d for s in ("Left", "Right", "Bottom")), sorted(d)):
        return
    cell = on_screen(cell_of("a1"))
    check("and each reaches into the view", all(d[s].intersects(cell)
                                                for s in ("Left", "Right", "Bottom")),
          (cell.getRect(), {s: r.getRect() for s, r in d.items()}))

    claim("the panel, on the left, touches none of them", lambda: clear("a1"))
    claim("it begins where the left dock ends",
          lambda: (abs(on_screen(host_of("a1")).left() - (docks()["Left"].right() + 1)) <= 2,
                   (on_screen(host_of("a1")).left(), docks()["Left"].right())))
    claim("and ends above the bottom dock",
          lambda: (on_screen(host_of("a1")).bottom() < docks()["Bottom"].top(),
                   (on_screen(host_of("a1")).bottom(), docks()["Bottom"].top())))

    claim("on the right it touches none of them",
          lambda: choose("a1", "Right") is None and clear("a1")[0])
    claim("and ends where the right dock begins",
          lambda: (abs(on_screen(host_of("a1")).right() + 1 - docks()["Right"].left()) <= 2,
                   (on_screen(host_of("a1")).right(), docks()["Right"].left())))
    claim("on the bottom it touches none of them",
          lambda: choose("a1", "Bottom") is None and clear("a1")[0])
    claim("and lies between the left dock and the right, above the bottom one",
          lambda: (on_screen(host_of("a1")).left() > docks()["Left"].right()
                   and on_screen(host_of("a1")).right() < docks()["Right"].left()
                   and on_screen(host_of("a1")).bottom() < docks()["Bottom"].top(),
                   on_screen(host_of("a1")).getRect()))
    claim("on the top it touches none of them",
          lambda: choose("a1", "Top") is None and clear("a1")[0])
    claim("and on the left again",
          lambda: choose("a1", "Left") is None and clear("a1")[0])
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(1800)


@step
def the_room_back():
    check("the docks are docks again", docks() == {}, list(docks()))
    claim("and the panel is at the left edge of its view",
          lambda: (host_of("a1").x() <= 8, host_of("a1").geometry().getRect()))
    check("its dialog was told nothing by any of it", calls("first") == state["told"],
          (state["told"], calls("first")))
    Control.closeDialog()


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
    try:
        if docks():
            FreeCADGui.runCommand("Std_DockOverlayAll")
            settle(800)
    except Exception:
        pass
    VIEW.SetBool("TaskPanelInViewAll", False)
    VIEW.SetBool("TaskPanelInView", False)
    LAST.RemString("Side")
    LAST.RemString("Mode")
    try:
        if Control.activeDialog():
            Control.closeDialog()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, advance)
