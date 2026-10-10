"""A TechDraw colour a document stored with no opacity is drawn.

The fourth component of a colour used to be a transparency that nothing in
TechDraw looked at, and documents hold it as 0. Since the colour conversion
carries it as an opacity, a dimension restored from such a document was
drawn fully transparent: in the tree, selectable, and not on the page -- the
pages of a real document showed none of their dimensions and balloons
(docs/HandsOnQueue.md entry 12). Upstream reads such a colour as opaque when
the view provider is restored (FixColorAlphaOnLoad); so does the fork now.

Claims:
  - a dimension and a geometric hatch given colours with no opacity, saved
    and reopened, have opaque colours and are on the page (the hatch's view
    provider is not a drawing view, and has the repair for itself);
  - with Mod/TechDraw/General/FixColorAlphaOnLoad off the colours are left
    as stored and neither is drawn, which is the reading the fork had and
    the way out for whoever wants a colour with no opacity.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
FILE = os.path.join(OUT, "ColourOpacity.FCStd")
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
HAD = GEN.GetBool("FixColorAlphaOnLoad", True)
BLUE_FLOOR = 300
STEPS = []
STATE = {"name": "ColourOpacity"}


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
    """How much of the page is the dimension's red, and the hatch's blue."""
    v = page_view()
    if v is None:
        return (-1, -1)
    img = v.grab().toImage()
    img.save(os.path.join(OUT, tag + ".png"))
    red = blue = 0
    for y in range(img.height()):
        for x in range(img.width()):
            p = img.pixel(x, y)
            r, g, b = (p >> 16) & 255, (p >> 8) & 255, p & 255
            # The dimension by its full red. The hatch by hue: its thin
            # lines are antialiased pale -- and so are the colour fringes
            # of a label's text, a few dozen pixels that BLUE_FLOOR is for.
            if r > 180 and g < 90 and b < 90:
                red += 1
            elif b > r + 40 and b > g + 40:
                blue += 1
    return (red, blue)


def colours():
    gdoc = FreeCADGui.getDocument(STATE["name"])
    return (gdoc.getObject("Dim").Color, gdoc.getObject("GeomHatch").ColorPattern)


def reopen():
    STATE["name"] = FreeCAD.openDocument(FILE).Name
    FreeCADGui.getDocument(STATE["name"]).getObject("Page").show()


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
    hatch = doc.addObject("TechDraw::DrawGeomHatch", "GeomHatch")
    hatch.Source = (view, ["Face0"])
    doc.recompute()
    gdoc = FreeCADGui.getDocument(doc.Name)
    gdoc.getObject("Page").show()
    gdoc.getObject("Dim").Color = (1.0, 0.0, 0.0, 1.0)
    gdoc.getObject("GeomHatch").ColorPattern = (0.0, 0.0, 1.0, 1.0)


@step(3000)
def opaque():
    red, blue = painted("1-opaque")
    check("a red dimension and a blue hatch are on the page", red > 20 and blue > BLUE_FLOOR,
          "%d red, %d blue pixels" % (red, blue))
    # what an old document holds: the colour, and no opacity
    gdoc = FreeCADGui.getDocument(STATE["name"])
    gdoc.getObject("Dim").Color = (1.0, 0.0, 0.0, 0.0)
    gdoc.getObject("GeomHatch").ColorPattern = (0.0, 0.0, 1.0, 0.0)
    FreeCAD.getDocument(STATE["name"]).saveAs(FILE)
    FreeCAD.closeDocument(STATE["name"])
    GEN.SetBool("FixColorAlphaOnLoad", True)
    reopen()


@step(4000)
def fixed():
    dim, hatch = colours()
    red, blue = painted("2-reopened")
    check("reopened: the dimension's colour, stored with no opacity, reads opaque",
          abs(dim[3] - 1.0) < 1e-6, dim)
    check("reopened: the dimension is on the page", red > 20, "%d red pixels" % red)
    check("reopened: the hatch's colour reads opaque", abs(hatch[3] - 1.0) < 1e-6, hatch)
    check("reopened: the hatch is on the page", blue > BLUE_FLOOR, "%d blue pixels" % blue)
    FreeCAD.closeDocument(STATE["name"])
    GEN.SetBool("FixColorAlphaOnLoad", False)
    reopen()


@step(4000)
def as_stored():
    dim, hatch = colours()
    red, blue = painted("3-preference-off")
    check("FixColorAlphaOnLoad off: the colours are left as stored, and neither is drawn",
          dim[3] == 0.0 and hatch[3] == 0.0 and red == 0 and blue < BLUE_FLOOR,
          (dim, hatch, "%d red, %d blue pixels" % (red, blue)))


def finish():
    GEN.SetBool("FixColorAlphaOnLoad", HAD)
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
