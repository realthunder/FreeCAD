"""A TechDraw page drawn by the backend is drawn once.

Mod/TechDraw/General/PageRendererVg has the render backend draw the page
(docs/TechDrawPortAndSection.md sec 37, docs/HandsOnQueue.md entry 20). As
first built, the backend's layer went UNDER the Qt items and the Qt items
were painted over it all the same: every line and every letter twice, and
bolder for it. Now the layer is the page's picture and the Qt items of
what it holds are not painted; they stay in the scene for the mouse.

That leaves the layer to show what only the Qt items used to: a view's
frame and label, and what is preselected or selected.

Claims, all with the layer read back into a raster viewport (the
compositor switched off -- a widget grab cannot read a GL viewport, and it
is what a session not on OpenGL gets anyway):
  - the Qt items of the views are left to the layer, not painted;
  - the picture is the Qt-painted page's: no ink of either away from the
    other's (a frame or a label the layer lacked would be Qt's ink with
    none of the layer's near it), and about as much of it;
  - PageRendererVgVerify paints the Qt items over the layer again, and
    that is another picture, and a heavier one: thousands of pixels
    change where the same strokes are blended twice. So a page that is
    the same pixel for pixel with the switch off again was drawn once
    (without this control the claims here could not fail);
  - a selected edge and a preselected one are in the highlight colours on
    the page, with the Qt items still not painting, and the page is back
    to what it was, pixel for pixel, when the selection is cleared;
  - nothing is fed again while nothing happens;
  - a group moved takes its picture with it: after the move the page is
    again the Qt-painted one.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PageBackendSingleDraw"
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
KEYS = (("PageRendererVg", False), ("PageRendererVgComposite", True),
        ("PageRendererVgVerify", False))
HAD = [GEN.GetBool(k, d) for k, d in KEYS]
CELL = 8
STEPS = []
STATE = {}


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
    for v in FreeCADGui.getMainWindow().findChildren(QtWidgets.QGraphicsView):
        if v.metaObject().className() == "TechDrawGui::QGVPage":
            return v
    return None


def pixels(view, save=None):
    """The page as painted now, as rows of 0xRRGGBB."""
    img = view.grab().toImage()
    if save:
        img.save(os.path.join(OUT, save + ".png"))
    return [[img.pixel(x, y) & 0xFFFFFF for x in range(img.width())]
            for y in range(img.height())]


def channels(p):
    return (p >> 16) & 255, (p >> 8) & 255, p & 255


def ink(p):
    """Drawn on: neither the backdrop nor (nearly) the white of the sheet."""
    return p != STATE["backdrop"] and max(channels(p)) < 200


def ink_cells(rows):
    cells = {}
    for y, row in enumerate(rows):
        for x, p in enumerate(row):
            if ink(p):
                key = (x // CELL, y // CELL)
                cells[key] = cells.get(key, 0) + 1
    return cells


def ink_away(base, now):
    """Drawn-on pixels of `now` in squares with none of `base` in or around them."""
    has = ink_cells(base)
    n = 0
    for (cx, cy), count in ink_cells(now).items():
        if not any((cx + dx, cy + dy) in has for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
            n += count
    return n


def weight(rows):
    """How much is drawn: the darkness of everything that is not backdrop,
    summed. The same strokes blended twice are darker at their edges."""
    total = 0
    for row in rows:
        for p in row:
            if p != STATE["backdrop"]:
                total += 255 - max(channels(p))
    return total


def coloured(rows):
    """Pixels in a colour: the page is black on white, the highlights are not."""
    n = 0
    for row in rows:
        for p in row:
            c = channels(p)
            # a thin line in a highlight colour is that colour thinned
            # out over the sheet's white
            if p != STATE["backdrop"] and max(c) - min(c) > 40:
                n += 1
    return n


def differing(a, b):
    if len(a) != len(b) or len(a[0]) != len(b[0]):
        return -1
    return sum(1 for ra, rb in zip(a, b) for pa, pb in zip(ra, rb) if pa != pb)


def counts(view):
    return (view.property("vgItemsCovered"), view.property("vgItemsPainted"),
            view.property("vgViewFeeds"))


def switch(on, composite=False, verify=False):
    GEN.SetBool("PageRendererVgComposite", composite)
    GEN.SetBool("PageRendererVgVerify", verify)
    GEN.SetBool("PageRendererVg", on)


@step(1000)
def make():
    import TechDrawGui  # the view providers
    switch(False)
    doc = FreeCAD.newDocument(DOC)
    box = doc.addObject("Part::Box", "Box")
    box.Length = 30
    box.Width = 20
    box.Height = 10
    cyl = doc.addObject("Part::Cylinder", "Cyl")
    cyl.Radius = 8
    cyl.Height = 30
    cyl.Placement = FreeCAD.Placement(
        FreeCAD.Vector(30, 10, -5), FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 37))
    cut = doc.addObject("Part::Cut", "Cut")
    cut.Base = box
    cut.Tool = cyl
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    doc.recompute()
    group = doc.addObject("TechDraw::DrawProjGroup", "Group")
    page.addView(group)
    group.Source = [cut]
    group.ScaleType = "Custom"
    group.Scale = 2
    group.addProjection("Front")
    group.Anchor.Direction = FreeCAD.Vector(0, 0, 1)
    group.Anchor.RotationVector = FreeCAD.Vector(1, 0, 0)
    group.Anchor.recompute()
    group.addProjection("Top")
    group.addProjection("Right")
    group.X = 150
    group.Y = 130
    doc.recompute()
    STATE["anchor"] = group.Anchor.Name
    FreeCADGui.getDocument(DOC).getObject("Page").show()


@step(4000)
def before():
    v = page_view()
    if not check("the page has its view", v is not None):
        raise RuntimeError("no page view")
    FreeCADGui.Selection.clearSelection()
    STATE["qt"] = pixels(v, "1-qt")
    STATE["backdrop"] = STATE["qt"][2][2]
    n = sum(ink_cells(STATE["qt"]).values())
    check("Qt-painted: the group is drawn, and no item is the layer's",
          n > 1000 and counts(v)[0] == 0, (n, counts(v)))
    switch(True)


@step(4000)
def layer():
    v = page_view()
    STATE["layer"] = pixels(v, "2-layer")
    covered, painted, feeds = counts(v)
    STATE["painted"] = painted
    STATE["feeds"] = feeds
    check("layer on: the Qt items of the views are left to it",
          covered > 20 and painted < covered, "covered %d, painted %d" % (covered, painted))
    n = ink_away(STATE["qt"], STATE["layer"])
    check("layer on: no ink away from what Qt draws", n < 20, "%d pixels" % n)
    n = ink_away(STATE["layer"], STATE["qt"])
    check("layer on: nothing Qt draws is missing", n < 20, "%d pixels" % n)
    STATE["w_qt"] = weight(STATE["qt"])
    STATE["w_layer"] = weight(STATE["layer"])
    ratio = STATE["w_layer"] / float(STATE["w_qt"])
    check("layer on: as much drawn as Qt draws", 0.8 < ratio < 1.2, "%.3f of Qt's" % ratio)
    switch(True, verify=True)


@step(3000)
def verify():
    v = page_view()
    both = pixels(v, "3-verify")
    covered, painted, _ = counts(v)
    ratio = weight(both) / float(STATE["w_layer"])
    n = differing(STATE["layer"], both)
    # how much heavier depends on the line widths of the configuration:
    # 1.04 with the defaults, 1.01 with thin lines
    check("verify: the Qt items painted over the layer are another, heavier picture",
          covered == 0 and painted > 20 and n > 500 and ratio > 1.0,
          "covered %d, painted %d, %d pixels differ, %.3f of the layer alone"
          % (covered, painted, n, ratio))
    switch(True)


@step(3000)
def single_again():
    v = page_view()
    now = pixels(v)
    n = differing(STATE["layer"], now)
    check("verify off: the layer alone again", n == 0, "%d pixels differ" % n)
    STATE["feeds"] = counts(v)[2]


@step(2000)
def idle():
    v = page_view()
    feeds = counts(v)[2]
    check("nothing is fed again while nothing happens", feeds == STATE["feeds"],
          "%d feeds in two idle seconds" % (feeds - STATE["feeds"]))
    STATE["plain"] = coloured(STATE["layer"])
    FreeCADGui.Selection.addSelection(DOC, STATE["anchor"], "Edge1")


@step(2000)
def selected():
    v = page_view()
    now = pixels(v, "4-selected")
    n = coloured(now)
    covered, painted, _ = counts(v)
    check("an edge selected is in the selection colour on the page",
          n > STATE["plain"] + 20, "%d coloured pixels, %d before" % (n, STATE["plain"]))
    check("selected: the Qt items still do not paint",
          covered > 20 and painted <= STATE["painted"],
          "covered %d, painted %d (%d before)" % (covered, painted, STATE["painted"]))
    FreeCADGui.Selection.clearSelection()


@step(2000)
def cleared():
    v = page_view()
    now = pixels(v, "5-cleared")
    n = differing(STATE["layer"], now)
    check("selection cleared: the page is what it was", n == 0, "%d pixels differ" % n)
    FreeCADGui.Selection.setPreselection(
        FreeCAD.getDocument(DOC).getObject(STATE["anchor"]), "Edge2")


@step(2000)
def preselected():
    v = page_view()
    now = pixels(v, "6-preselected")
    n = coloured(now)
    check("an edge preselected is in the preselection colour on the page",
          n > STATE["plain"] + 20, "%d coloured pixels, %d before" % (n, STATE["plain"]))
    FreeCADGui.Selection.clearPreselection()


@step(2000)
def unpreselected():
    v = page_view()
    n = differing(STATE["layer"], pixels(v))
    check("preselection gone: the page is what it was", n == 0, "%d pixels differ" % n)
    group = FreeCAD.getDocument(DOC).getObject("Group")
    group.X = 170
    FreeCAD.getDocument(DOC).recompute()


@step(3000)
def moved():
    v = page_view()
    STATE["moved"] = pixels(v, "7-moved")
    n = differing(STATE["layer"], STATE["moved"])
    check("the group moved on the page", n > 500, "%d pixels differ" % n)
    switch(False)


@step(3000)
def moved_qt():
    v = page_view()
    qt = pixels(v, "8-moved-qt")
    n = ink_away(qt, STATE["moved"])
    check("moved: no ink left where the group was", n < 20, "%d pixels" % n)
    n = ink_away(STATE["moved"], qt)
    check("moved: nothing Qt draws is missing", n < 20, "%d pixels" % n)


def finish():
    for (key, _), had in zip(KEYS, HAD):
        GEN.SetBool(key, had)
    try:
        FreeCAD.closeDocument(DOC)
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
