"""The navigation cube moves out from under a panel laid over its view, and
the panels leave it no room.

docs/HandsOnQueue.md entry 76: "any panel in overlay mode that is docked at
the side of navicube leaves space for the navicube", then: "Do not leave space
on overlay panel at all. Let navicube monitor any overlay panel (including in
view panel) to move out of the way. Add a setting to enable that behavior,
default true. remove the old panel leave space setting"; and as answered: it
slides along the edge, same corner; with the setting off a panel simply covers
it; it moves for a momentary panel too and comes back when that goes.

Claims, the cube in the top right corner of the one 3D view:
  - with the docks docked the cube is shown where the view states it;
  - the docks overlaid: the right one begins at the top of the area (it left a
    cube's height there before) and the cube is shown clear of every dock,
    still on the top edge, left of the right dock; the view's own NaviCubeX
    and NaviCubeY are what they were;
  - a task panel shown over the view on the right: the cube is clear of it
    too; the panel closed, the cube is back beside the dock;
  - the setting off: the cube is shown where the view states it, covered;
    on again, clear again;
  - the docks docked again: the cube is back where the view states it.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
NAVI = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube")
TASKS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TaskView")
Control = FreeCADGui.Control
STEPS = []
S = {}


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
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def all_widgets(name):
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == name:
                out.append(w)
        except RuntimeError:
            pass
    return out


def on_screen(w):
    return QtCore.QRect(w.mapToGlobal(QtCore.QPoint(0, 0)), w.size())


def docks():
    out = {}
    for w in all_widgets("Gui::OverlayTabWidget"):
        if w.isVisible() and w.count():
            out[w.objectName().replace("Overlay", "")] = on_screen(w)
    return out


def hosts():
    return [h for h in all_widgets("Gui::TaskView::TaskPanelHost") if not h.isHidden()]


def cube(at=None):
    """Where the cube is shown, on the screen: the view's rectangle and the
    fractions the viewer answers -- or \a at, to ask where a position would
    be -- placed as the cube places itself."""
    view = S["view"]
    gl = view.graphicsView().viewport()
    FreeCADGui.updateGui()
    shown = at if at is not None else view.getViewer().getNaviCubeShown()
    size = NAVI.GetInt("CubeSize", 132)
    ratio = gl.devicePixelRatioF()
    w, h = int(gl.width() * ratio), int(gl.height() * ratio)
    m = int(0.05 * size)
    x = m + int(round(shown[0] * max(0, w - size - 2 * m)))
    y = m + int(round(shown[1] * max(0, h - size - 2 * m)))
    origin = gl.mapToGlobal(QtCore.QPoint(0, 0))
    rect = QtCore.QRect(origin.x() + int(x / ratio), origin.y() + int(y / ratio),
                        int(size / ratio), int(size / ratio))
    return shown, rect


def covered_by():
    shown, rect = cube()
    hit = [side for side, r in docks().items() if r.intersects(rect)]
    hit += ["task panel" for h in hosts() if on_screen(h).intersects(rect)]
    return hit, (tuple(round(v, 3) for v in shown), rect.getRect(),
                 {s: r.getRect() for s, r in docks().items()},
                 [on_screen(h).getRect() for h in hosts()])


def stated():
    return (round(S["view"].NaviCubeX, 3), round(S["view"].NaviCubeY, 3))


class Panel:
    def __init__(self):
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle("a panel")
        layout = QtWidgets.QVBoxLayout(self.form)
        for i in range(12):
            layout.addWidget(QtWidgets.QLabel("line %d of a panel" % i, self.form))

    def accept(self):
        return True

    def reject(self):
        return True


def start():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    VIEW.SetBool("UseViewArea", True)
    VIEW.SetBool("ShowNaviCube", True)
    VIEW.SetInt("CornerNaviCube", 1)
    VIEW.SetBool("DockOverlayMoveNaviCube", True)
    VIEW.SetBool("TaskPanelInView", False)
    TASKS.GetGroup("Host").SetString("Side", "Right")
    TASKS.GetGroup("Host").RemString("Mode")
    doc = FreeCAD.newDocument("CubeClear")
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle(800)
    S["view"] = FreeCADGui.getDocument("CubeClear").mdiViewsOfType("Gui::View3DInventor")[0]
    S["view"].viewIsometric()
    S["view"].fitAll()
    settle(500)


def docked():
    shown, rect = cube()
    check("docked: the cube is shown where the view states it, the top right",
          tuple(round(v, 3) for v in shown) == stated() == (1.0, 0.0), (shown, stated()))
    check("and no dock is overlaid", docks() == {}, list(docks()))
    S["home"] = rect
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)


def overlaid():
    d = docks()
    if not check("the docks are laid over the window, one on the right", "Right" in d, sorted(d)):
        raise RuntimeError("no dock on the right")
    area = on_screen(mw().centralWidget())
    check("the right dock leaves no room for a cube: it begins at the top of the area",
          d["Right"].top() - area.top() < 60, (d["Right"].getRect(), area.getRect()))
    # the view is wider now that the docks are laid over it: where its top
    # right corner puts the cube, as the view states it
    S["home"] = cube(stated())[1]
    if not check("the right dock is where the view states the cube", d["Right"].intersects(S["home"]),
                 (d["Right"].getRect(), S["home"].getRect())):
        raise RuntimeError("the dock does not reach the cube: nothing to move out of")
    hit, detail = covered_by()
    check("the cube is shown clear of every dock", not hit, (hit, detail))
    shown, rect = cube()
    check("still on the top edge, and left of the right dock",
          abs(rect.top() - S["home"].top()) <= 2 and rect.right() < d["Right"].left(),
          (rect.getRect(), d["Right"].getRect()))
    check("the view's own position for the cube is what it was", stated() == (1.0, 0.0), stated())
    S["beside"] = rect

    VIEW.SetBool("TaskPanelInView", True)
    Control.showDialog(Panel())
    settle(1200)


def with_a_panel():
    h = hosts()
    if not check("a task panel is shown over the view", len(h) == 1, len(h)):
        return
    panel = on_screen(h[0])
    check("on the right, where the cube had gone", panel.intersects(S["beside"]),
          (panel.getRect(), S["beside"].getRect()))
    hit, detail = covered_by()
    check("the cube is clear of the panel and of the docks", not hit, (hit, detail))
    Control.closeDialog()
    settle(1200)


def panel_gone():
    shown, rect = cube()
    check("the panel closed: the cube is back beside the dock",
          abs(rect.left() - S["beside"].left()) <= 2 and abs(rect.top() - S["beside"].top()) <= 2,
          (rect.getRect(), S["beside"].getRect()))
    VIEW.SetBool("DockOverlayMoveNaviCube", False)
    settle(800)
    shown, rect = cube()
    hit, detail = covered_by()
    check("the setting off: the cube is shown where the view states it, covered",
          tuple(round(v, 3) for v in shown) == (1.0, 0.0) and "Right" in hit, (hit, detail))
    VIEW.SetBool("DockOverlayMoveNaviCube", True)
    settle(800)
    hit, detail = covered_by()
    check("on again: clear again", not hit, (hit, detail))
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)


def docked_again():
    check("the docks are docks again", docks() == {}, list(docks()))
    shown, rect = cube()
    check("and the cube is back where the view states it",
          tuple(round(v, 3) for v in shown) == (1.0, 0.0), shown)


def finish():
    try:
        if docks():
            FreeCADGui.runCommand("Std_DockOverlayAll")
            settle(800)
        if Control.activeDialog():
            Control.closeDialog()
    except Exception:
        pass
    VIEW.SetBool("TaskPanelInView", False)
    TASKS.GetGroup("Host").RemString("Side")
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


def advance():
    if not STEPS:
        finish()
        return
    delay, fn = STEPS.pop(0)

    def run():
        try:
            fn()
        except Exception:
            note("ABORT in %s: %s" % (fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(delay, run)


STEPS.append((2500, start))
STEPS.append((500, docked))
STEPS.append((500, overlaid))
STEPS.append((500, with_a_panel))
STEPS.append((500, panel_gone))
STEPS.append((500, docked_again))
advance()
