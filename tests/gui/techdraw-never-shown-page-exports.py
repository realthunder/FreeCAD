"""A TechDraw page nothing has shown is whole when it is exported.

A page makes its Qt items when something needs them (QGSPage::populate,
docs/HandsOnQueue.md entry 47), so a page saved hidden comes back from a load
with an empty scene. Python's exports (exportPageAsPdf, exportPageAsSvg) bring
the page's view up themselves when it has none, as they always did, and write
the file in the same call: the items have to be there by then, or the file
holds the template and nothing else.

What this does NOT drive is TechDraw_Print (TechDraw_PrintAll when this was
written), which prints every page of the
document from its scene with no view opened, through PagePrinter::renderPage;
that is behind the system's print dialog and has no way in from Python.

Claims:
  - a page saved hidden and reopened has no item in its scene;
  - its PDF, exported with nothing having shown it, is the size of the one
    exported while it was on the screen, and its SVG has as many paths and
    the same texts;
  - the export made the page's items;
  - exported again afterwards it is the same once more.
"""
import os
import re
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
FILE = os.path.join(OUT, "NeverShown.FCStd")
STEPS = []
STATE = {"name": "NeverShown"}


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


def red_pixels(tag):
    v = page_view()
    if v is None:
        return -1
    img = v.grab().toImage()
    img.save(os.path.join(OUT, tag + ".png"))
    red = 0
    for y in range(img.height()):
        for x in range(img.width()):
            p = img.pixel(x, y)
            r, g, b = (p >> 16) & 255, (p >> 8) & 255, p & 255
            # by hue, as techdraw-page-items-when-needed.py counts: full red
            # was the digits alone, some twenty pixels on xvfb
            if r > g + 40 and r > b + 40:
                red += 1
    return red


def scene_extent():
    """Asked of the scene as a whole: items() from a script takes the items it
    wraps out of the scene."""
    import TechDrawGui
    page = FreeCAD.getDocument(STATE["name"]).getObject("Page")
    rect = TechDrawGui.getSceneForPage(page).itemsBoundingRect()
    return (rect.isEmpty(), "%.0f x %.0f" % (rect.width(), rect.height()))


def vp(name):
    return FreeCADGui.getDocument(STATE["name"]).getObject(name)


def export(tag):
    """The page as a PDF and as an SVG; what each holds."""
    import TechDrawGui
    page = FreeCAD.getDocument(STATE["name"]).getObject("Page")
    pdf = os.path.join(OUT, tag + ".pdf")
    svg = os.path.join(OUT, tag + ".svg")
    TechDrawGui.exportPageAsPdf(page, pdf)
    TechDrawGui.exportPageAsSvg(page, svg)
    size = os.path.getsize(pdf) if os.path.exists(pdf) else -1
    text = ""
    if os.path.exists(svg):
        with open(svg, encoding="utf-8", errors="replace") as f:
            text = f.read()
    return {"pdf": size,
            "paths": len(re.findall(r"<path\b", text)),
            "texts": sorted(t.strip() for t in re.findall(r"<text\b[^>]*>([^<]*)", text))}


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
    red = red_pixels("1-made")
    check("made and shown: the red dimension is on the page", red > 20, "%d red pixels" % red)
    STATE["shown"] = export("1-shown")
    note("exported while shown: %r" % (STATE["shown"],))
    check("exported while shown: the PDF and the SVG hold the drawing and the dimension",
          STATE["shown"]["pdf"] > 1000 and STATE["shown"]["paths"] > 4
          and any(STATE["shown"]["texts"]), STATE["shown"])
    vp("Page").hide()
    FreeCAD.getDocument(STATE["name"]).saveAs(FILE)
    FreeCAD.closeDocument(STATE["name"])
    STATE["name"] = FreeCAD.openDocument(FILE).Name


@step(4000)
def exported_never_shown():
    check("reopened hidden: no page view", page_view() is None)
    empty, extent = scene_extent()
    check("reopened hidden: the load made no item for the page", empty, extent)
    got = export("2-never-shown")
    want = STATE["shown"]
    note("exported never shown: %r" % (got,))
    check("never shown: the PDF is the size of the one exported while shown",
          want["pdf"] > 0 and abs(got["pdf"] - want["pdf"]) <= 0.05 * want["pdf"],
          "%d against %d bytes" % (got["pdf"], want["pdf"]))
    check("never shown: the SVG has as many paths",
          got["paths"] == want["paths"], "%d against %d" % (got["paths"], want["paths"]))
    check("never shown: the SVG has the same texts, the dimension's value among them",
          got["texts"] == want["texts"] and any(got["texts"]),
          "%r against %r" % (got["texts"], want["texts"]))
    empty, extent = scene_extent()
    check("the export made the page's items", not empty, extent)
    vp("Page").show()


@step(3000)
def shown_after():
    red = red_pixels("3-shown-after")
    check("shown after the export: the dimension is drawn", red > 20, "%d red pixels" % red)
    got = export("3-shown-after")
    want = STATE["shown"]
    check("exported again while shown: the same as the first",
          got["paths"] == want["paths"] and got["texts"] == want["texts"],
          "%r against %r" % (got, want))


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
