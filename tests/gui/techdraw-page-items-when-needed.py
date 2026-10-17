"""A TechDraw page makes its items when something needs them, not at the load.

A document's load used to make the Qt items of every page it holds -- each
dimension laid out and formatted, each section face hatched -- whether or not
the page was ever going to be looked at: four seconds of the wait after the
open of a document with ten pages (docs/HandsOnQueue.md entry 47). A page that
nothing shows now has an empty scene until it is shown, printed or exported
(QGSPage::populate), and is whole from then on.

Claims:
  - a page saved hidden and reopened has no item in its scene, and none after
    a dimension is added to it and the document recomputed;
  - shown for the first time it draws both the dimension it was saved with
    and the one added while nothing showed it;
  - hidden and shown again it still does.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
FILE = os.path.join(OUT, "PageItems.FCStd")
STEPS = []
STATE = {"name": "PageItems"}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def step(delay_ms):
    def deco(fn):
        STEPS.append((delay_ms, fn))
        return fn
    return deco


def page_view():
    views = [v for v in FreeCADGui.getMainWindow().findChildren(QtWidgets.QGraphicsView)
             if v.metaObject().className() == "TechDrawGui::QGVPage" and v.isVisible()]
    return views[-1] if views else None


def painted(tag):
    """How much of the page is the first dimension's red, and the second's green."""
    v = page_view()
    if v is None:
        return (-1, -1)
    img = v.grab().toImage()
    img.save(os.path.join(OUT, tag + ".png"))
    red = green = 0
    for y in range(img.height()):
        for x in range(img.width()):
            p = img.pixel(x, y)
            r, g, b = (p >> 16) & 255, (p >> 8) & 255, p & 255
            # By hue, as techdraw-colour-without-opacity.py counts: on
            # xvfb's 1280x1024 the page view is narrow and a dimension's
            # lines are thinner than a pixel, antialiased pale, so only the
            # digits were full red or green -- some twenty pixels, the
            # floor itself. The page's ground, the 3D view's blue gradient
            # by default, is neither.
            if r > g + 40 and r > b + 40:
                red += 1
            elif g > r + 40 and g > b + 40:
                green += 1
    return (red, green)


def scene_extent():
    """The bounding rectangle of the page's items; empty when it has none.

    Asked of the scene as a whole: items() from a script takes the items it
    wraps out of the scene.
    """
    import TechDrawGui
    page = FreeCAD.getDocument(STATE["name"]).getObject("Page")
    rect = TechDrawGui.getSceneForPage(page).itemsBoundingRect()
    return (rect.isEmpty(), "%.0f x %.0f" % (rect.width(), rect.height()))


def vp(name):
    return FreeCADGui.getDocument(STATE["name"]).getObject(name)


@step(1000)
def make():
    import TechDrawGui  # the view providers
    doc = FreeCAD.newDocument(STATE["name"])
    box = doc.addObject("Part::Box", "Box")
    box.Length = 40
    box.Width = 30
    box.Height = 20
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "View")
    view.Source = [box]
    view.Direction = FreeCAD.Vector(0, 0, 1)
    view.ScaleType = "Custom"
    view.Scale = 3
    view.X = 150
    view.Y = 110
    page.addView(view)
    doc.recompute()
    dim = doc.addObject("TechDraw::DrawViewDimension", "Dim")
    dim.Type = "Distance"
    dim.References2D = [(view, "Edge0")]
    page.addView(dim)
    doc.recompute()
    vp("Page").show()
    vp("Dim").Color = (1.0, 0.0, 0.0, 1.0)


@step(3000)
def shown_then_saved_hidden():
    red, green = painted("1-made")
    check("made and shown: the red dimension is on the page, and no green",
          red > 20 and green == 0, "%d red, %d green pixels" % (red, green))
    empty, extent = scene_extent()
    check("made and shown: the scene has items", not empty, extent)
    vp("Page").hide()
    FreeCAD.getDocument(STATE["name"]).saveAs(FILE)
    FreeCAD.closeDocument(STATE["name"])
    STATE["name"] = FreeCAD.openDocument(FILE).Name


@step(4000)
def loaded_hidden():
    check("reopened hidden: no page view", page_view() is None)
    empty, extent = scene_extent()
    check("reopened hidden: the load made no item for the page", empty, extent)
    doc = FreeCAD.getDocument(STATE["name"])
    dim = doc.addObject("TechDraw::DrawViewDimension", "Dim2")
    dim.Type = "Distance"
    dim.References2D = [(doc.getObject("View"), "Edge1")]
    # beside the view: the two measure the same length and would be drawn
    # one over the other
    dim.X = 75
    doc.getObject("Page").addView(dim)
    doc.recompute()
    vp("Dim2").Color = (0.0, 1.0, 0.0, 1.0)


@step(2000)
def still_none():
    empty, extent = scene_extent()
    check("a dimension added to the page nothing shows: still no item", empty, extent)
    vp("Page").show()


@step(3000)
def first_shown():
    red, green = painted("2-first-shown")
    check("shown for the first time: the dimension it was saved with is drawn",
          red > 20, "%d red pixels" % red)
    check("shown for the first time: the dimension added while hidden is drawn",
          green > 20, "%d green pixels" % green)
    empty, extent = scene_extent()
    check("shown: the scene has items", not empty, extent)
    vp("Page").hide()


@step(1500)
def hidden_again():
    check("hidden again: no page view", page_view() is None)
    vp("Page").show()


@step(3000)
def shown_again():
    red, green = painted("3-shown-again")
    check("shown again: both dimensions are drawn", red > 20 and green > 20,
          "%d red, %d green pixels" % (red, green))


def finish():
    for name in list(FreeCAD.listDocuments()):
        try:
            FreeCAD.closeDocument(name)
        except Exception:
            pass
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
            note("ABORT in %s: %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(delay, run)


advance()
