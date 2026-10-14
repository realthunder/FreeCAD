"""The ground of a TechDraw page view, around the sheet, is the 3D view's
background.

docs/HandsOnQueue.md entry 112: "techdraw page background same as 3d view" --
around the sheet only, the gradient too, and "add a setting to gate techdraw
background default same as 3d view. Check if you can do it in qt renderer as
well. do it if easy."

The ground is Qt's painting whichever way the page is drawn (the render engine
lays the page over it), so one rule serves both.

Claims, a blank page, its sheet small in the middle of the view:
  - with a linear gradient set for the 3D view (two colours given here), the
    page view's top row is the first colour and its bottom row the second;
  - with the gradient off, the whole ground is the 3D view's plain colour;
  - with Colors/BackgroundAs3DView off, it is TechDraw's own background
    colour, top and bottom alike;
  - on again, the gradient again: the setting applies at once.

Runs offscreen as well as on a screen; the pointer is not moved.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
COLORS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/Colors")
STEPS = []
S = {}
TOP = (40, 60, 160)
BOTTOM = (200, 180, 90)
PLAIN = (30, 120, 60)
OWN = (112, 112, 112)


def packed(rgb):
    return (rgb[0] << 24) | (rgb[1] << 16) | (rgb[2] << 8) | 255


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


def near(a, b, tol=10):
    return max(abs(a[i] - b[i]) for i in range(3)) <= tol


def ground(tag):
    """The colours of the page view's top and bottom rows, at its left edge:
    clear of the sheet, which is in the middle"""
    view = S["view"]
    view.viewport().update()
    settle(800)
    image = view.viewport().grab().toImage().convertToFormat(QtGui.QImage.Format_RGB32)
    image.save(os.path.join(OUT, tag + ".png"))
    x = 6
    top = QtGui.QColor(image.pixel(x, 3))
    bottom = QtGui.QColor(image.pixel(x, image.height() - 4))
    return (top.red(), top.green(), top.blue()), (bottom.red(), bottom.green(), bottom.blue())


def start():
    VIEW.SetBool("Simple", False)
    VIEW.SetBool("RadialGradient", False)
    VIEW.SetBool("Gradient", True)
    VIEW.SetBool("UseBackgroundColorMid", False)
    VIEW.SetUnsigned("BackgroundColor2", packed(TOP))
    VIEW.SetUnsigned("BackgroundColor3", packed(BOTTOM))
    VIEW.SetUnsigned("BackgroundColor", packed(PLAIN))
    COLORS.SetUnsigned("Background", packed(OWN))
    COLORS.RemBool("BackgroundAs3DView")
    doc = FreeCAD.newDocument("PageGround")
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates",
                                     "A4_Landscape_blank.svg")
    page.Template = template
    doc.recompute()
    FreeCADGui.getDocument(doc.Name).getObject("Page").doubleClicked()
    settle(2500)
    views = [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QGraphicsView)
             if w.metaObject().className() == "TechDrawGui::QGVPage" and w.isVisible()]
    if not check("the page has a view", bool(views), len(views)):
        STEPS[:] = []
        return
    S["view"] = views[0]
    # the sheet small in the middle, the ground all around it
    views[0].resetTransform()
    views[0].scale(0.04, 0.04)
    views[0].centerOn(views[0].sceneRect().center())
    settle(800)


def gradient():
    top, bottom = ground("1-gradient")
    check("with a gradient for the 3D view, the page view's top row is its first colour",
          near(top, TOP), top)
    check("and its bottom row the second", near(bottom, BOTTOM), bottom)


def plain():
    VIEW.SetBool("Gradient", False)
    settle(600)
    top, bottom = ground("2-plain")
    check("with the gradient off the ground is the 3D view's plain colour",
          near(top, PLAIN) and near(bottom, PLAIN), (top, bottom))
    VIEW.SetBool("Gradient", True)
    settle(400)


def own():
    COLORS.SetBool("BackgroundAs3DView", False)
    settle(600)
    top, bottom = ground("3-own")
    check("with the setting off it is TechDraw's own background colour",
          near(top, OWN) and near(bottom, OWN), (top, bottom))
    COLORS.SetBool("BackgroundAs3DView", True)
    settle(600)
    top, bottom = ground("4-gradient-again")
    check("on again, the gradient again", near(top, TOP) and near(bottom, BOTTOM), (top, bottom))


def finish():
    COLORS.RemBool("BackgroundAs3DView")
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    step = STEPS.pop(0)
    try:
        step()
    except Exception:
        note("ABORT in %s: %s" % (step.__name__, traceback.format_exc().replace("\n", " | ")))
        STEPS[:] = []
    QtCore.QTimer.singleShot(900, advance)


STEPS[:] = [start, gradient, plain, own]
QtCore.QTimer.singleShot(3000, advance)
