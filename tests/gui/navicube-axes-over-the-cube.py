"""The navigation cube's axes are drawn over the cube, and the corner
coordinate system is off unless asked for.

docs/HandsOnQueue.md entry 75: "make ShowAxisCross default false. Make axis
cross in navicube show on top of the cube so it can be seen clearly", and as
answered: the setting meant is View/CornerCoordSystem, off by default; the
cube's axes are "drawn over, stay where they are"; "change default is enough".
The three axes start at the cube's far bottom corner and ran behind its faces
from most directions, the isometric among them.

Claims, on a profile that stores neither setting, one 3D view in isometric:
  - the view shows no coordinate system in its corner, shows one when the
    setting is switched on, and none again when the setting is removed;
  - in the picture of the view, in the cube's rectangle, each of the three
    axes shows as a run of its colour -- red, green and blue -- over the cube;
  - with the cube's axes switched off (NaviCube/ShowCS) those runs are gone,
    so they were the axes and nothing else.
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
STEPS = []
S = {}
# pixels of an axis' colour that count as "the axis is seen"
SEEN = 40


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


def cube_colours(tag):
    """How many pixels of each axis colour the cube's rectangle has, in the
    view as the backend drew it."""
    view = S["view"]
    path = os.path.join(OUT, tag + ".png")
    view.saveRenderDump(path, metadata=False)
    img = QtGui.QImage(path)
    gl = view.graphicsView().viewport()
    shown = view.getViewer().getNaviCubeShown()
    size = NAVI.GetInt("CubeSize", 132)
    scale = img.width() / float(gl.width())
    edge = int(size * scale / gl.devicePixelRatioF())
    m = int(0.05 * edge)
    x = m + int(round(shown[0] * max(0, img.width() - edge - 2 * m)))
    y = m + int(round(shown[1] * max(0, img.height() - edge - 2 * m)))
    crop = img.copy(x, y, edge, edge)
    crop.save(os.path.join(OUT, tag + "-cube.png"))
    red = green = blue = 0
    for py in range(crop.height()):
        for px in range(crop.width()):
            p = crop.pixel(px, py)
            r, g, b = (p >> 16) & 255, (p >> 8) & 255, p & 255
            if r > 190 and g < 80 and b < 80:
                red += 1
            elif g > 190 and r < 80 and b < 80:
                green += 1
            elif b > 190 and r < 80 and g < 80:
                blue += 1
    return {"red": red, "green": green, "blue": blue, "edge": edge}


def start():
    S["stored"] = ("CornerCoordSystem" in VIEW.GetBools(), "ShowCS" in NAVI.GetBools())
    VIEW.SetBool("ShowNaviCube", True)
    doc = FreeCAD.newDocument("CubeAxes")
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle(800)
    S["view"] = FreeCADGui.getDocument("CubeAxes").mdiViewsOfType("Gui::View3DInventor")[0]
    S["view"].viewIsometric()
    S["view"].fitAll()
    settle(1500)


def corner_off():
    # asked of the viewer: the corner coordinate system is Coin's own drawing,
    # which no picture of the render engine's shows
    viewer = S["view"].getViewer()
    check("the profile stores neither setting", S["stored"] == (False, False), S["stored"])
    check("by default the view shows no coordinate system in its corner",
          viewer.isCornerCoordSystemShown() is False)
    VIEW.SetBool("CornerCoordSystem", True)
    settle(800)
    check("switched on, the view shows it: the question sees one when there is one",
          viewer.isCornerCoordSystemShown() is True)
    VIEW.RemBool("CornerCoordSystem")
    settle(800)
    check("the setting removed again, it is gone", viewer.isCornerCoordSystemShown() is False)


def axes_over():
    got = cube_colours("axes-on")
    note("with the cube's axes: %r" % (got,))
    check("the X axis shows over the cube, in red", got["red"] >= SEEN, got)
    check("the Y axis shows over the cube, in green", got["green"] >= SEEN, got)
    check("the Z axis shows over the cube, in blue", got["blue"] >= SEEN, got)
    S["on"] = got
    NAVI.SetBool("ShowCS", False)
    settle(1500)


def axes_off():
    got = cube_colours("axes-off")
    note("without them: %r" % (got,))
    check("with the cube's axes off those colours are gone: they were the axes",
          got["red"] < SEEN // 4 and got["green"] < SEEN // 4 and got["blue"] < SEEN // 4, got)
    NAVI.RemBool("ShowCS")
    settle(500)


def finish():
    NAVI.RemBool("ShowCS")
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


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
STEPS.append((300, corner_off))
STEPS.append((500, axes_over))
STEPS.append((500, axes_off))
advance()
