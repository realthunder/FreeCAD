"""A TechDraw page drawn by the backend has the Qt page's line widths.

docs/HandsOnQueue.md entry 61: "Techdraw bgfx rendering seems to render all
dashed line slightly thicker than qt [...] for some line, like the cosmetic
symmetric line in Page, Top, the hover and selection highlight shows the
thinner dash line, which is barely visible because the underlying thickend
line"; decided: "better make it the same as qt renderer, whcih is thinner
and same width for highlight", "every line, dashed or not".

The Qt page sets the pen of an edge as a whole number of scene units, a
tenth of a millimetre each (QGIPrimPath::setTools, QPen::setWidth(int)): a
line asked for at 0.35 mm is drawn 0.3 mm wide. The backend drew the 0.35
asked for. What it lays over a line that is preselected or selected is read
off the Qt item, pen and all, so the highlight was the thinner of the two
and the line showed either side of it.

Qt's picture is the reference here and the backend is what is switched (the
reporter's point: the other way round is how this was missed). The page is
drawn by Qt and then by the backend at 20 pixels to the millimetre, where a
twentieth of a millimetre is a pixel, and the ink across four lines is
added up -- fractions of a pixel included, so the two rasterizers'
different edges do not count:
  - a visible edge (0.7 mm asked for, 0.7 drawn), a hidden line (0.375,
    Qt 0.3), a dashed cosmetic line (0.35, Qt 0.3), a section line: each as
    wide in the backend's picture as in Qt's, within half a pixel;
  - with the cosmetic line selected, the line under the highlight does not
    show beside it: the ink that is neither the paper's colour nor the
    highlight's, added up across the line, is no more than in Qt's picture
    plus a quarter of a pixel;
  - and the highlight itself is as wide as Qt's.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "WidthCompare"
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
KEYS = (("PageRendererVg", False), ("PageRendererVgComposite", True),
        ("PageRendererVgVerify", False))
HAD = [GEN.GetBool(k, d) for k, d in KEYS]
VIEW_X, VIEW_Y = 110.0, 120.0
ZOOM = 20.0  # pixels to the millimetre
STEPS = []
SEEN = {}
EDGE = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def page_view():
    for v in FreeCADGui.getMainWindow().findChildren(QtWidgets.QGraphicsView):
        if v.metaObject().className() == "TechDrawGui::QGVPage" and v.isVisible():
            return v
    return None


def switch(on):
    # read back into a raster viewport: a widget grab cannot read a GL one
    GEN.SetBool("PageRendererVgComposite", False)
    GEN.SetBool("PageRendererVgVerify", False)
    GEN.SetBool("PageRendererVg", on)


def at(mm_x, mm_y):
    """A point of the view, millimetres from its middle, in the widget's pixels"""
    return page_view().mapFromScene(
        QtCore.QPointF((VIEW_X + mm_x) * 10.0, -(VIEW_Y + mm_y) * 10.0))


def rgb(img, x, y):
    p = img.pixel(x, y)
    return ((p >> 16) & 255, (p >> 8) & 255, p & 255)


def cut(img, x, y, across, half):
    """The pixels of a cut through (x, y), `half` either side: along y when
    the line runs `across`, along x when it is upright"""
    if across:
        return [rgb(img, x, y + d) for d in range(-half, half + 1)]
    return [rgb(img, x + d, y) for d in range(-half, half + 1)]


def ink(pixels, paper):
    """How much of the cut is not paper, in pixels: each pixel by how far it
    is from the paper's colour towards black, the furthest channel"""
    return sum(max(paper[c] - p[c] for c in range(3)) / 255.0 for p in pixels)


def other_ink(pixels, paper, line):
    """How much of the cut is neither paper nor the line's colour, in
    pixels: each pixel's distance from the stretch of colours between the
    two, which is where a rasterizer's soft edge lies"""
    span = [line[c] - paper[c] for c in range(3)]
    length2 = float(sum(s * s for s in span)) or 1.0
    total = 0.0
    for p in pixels:
        off = [p[c] - paper[c] for c in range(3)]
        along = max(0.0, min(1.0, sum(off[c] * span[c] for c in range(3)) / length2))
        total += max(abs(off[c] - along * span[c]) for c in range(3)) / 255.0
    return total


# name: (where the line is, millimetres from the view's middle; whether it
# runs across; the stretch along it to look for ink in, as it may be dashed)
# The model is small enough for the whole view to be in the window at this
# zoom: a box 30 by 20 seen from above, a pocket 16 by 10 hidden in it.
LINES = {
    "visible edge": ((-15.0, -7.5), False, (-9.0, -6.0)),
    "hidden line": ((0.0, 5.0), True, (-6.0, 6.0)),
    "cosmetic line": ((0.0, -2.5), True, (-6.0, 6.0)),
    "section line": ((0.0, 2.5), True, (-6.0, 6.0)),
}


def widest(img, name, measure):
    """The cut through the line where it holds most ink: a dashed line has
    gaps, and a cut through a dash's end holds part of one"""
    (mx, my), across, (lo, hi) = LINES[name]
    half = int(0.65 * ZOOM)
    best = None
    steps = int((hi - lo) * ZOOM)
    for i in range(steps + 1):
        along = lo + i / ZOOM
        p = at(along, my) if across else at(mx, along)
        px = cut(img, p.x(), p.y(), across, half)
        value = measure(px)
        if best is None or value[0] > best[0]:
            best = value
    return best


def measure(tag):
    img = page_view().grab().toImage()
    img.save(os.path.join(OUT, tag + ".png"))
    # the middle of the view, where nothing is drawn
    blank = at(0.0, 0.0)
    paper = rgb(img, blank.x(), blank.y())
    out = {"paper": paper}
    for name in LINES:
        def amount(px):
            mid = px[len(px) // 2]
            return (ink(px, paper), mid, px)
        total, mid, px = widest(img, name, amount)
        out[name] = (total, mid, px)
        note("NOTE %-8s %-14s %.2f px of ink across it (%.3f mm), its middle %s" % (
            tag, name, total, total / ZOOM, mid))
    SEEN[tag] = out


def make():
    import TechDrawGui  # noqa: F401  the view providers
    switch(False)
    doc = FreeCAD.newDocument(DOC)
    outer = doc.addObject("Part::Box", "Outer")
    outer.Length, outer.Width, outer.Height = 30, 20, 10
    inner = doc.addObject("Part::Box", "Inner")
    inner.Length, inner.Width, inner.Height = 16, 10, 5
    inner.Placement.Base = FreeCAD.Vector(7, 5, 0)
    cut_ = doc.addObject("Part::Cut", "Cut")
    cut_.Base, cut_.Tool = outer, inner
    doc.recompute()
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "View")
    page.addView(view)
    view.Source = [cut_]
    view.Direction = FreeCAD.Vector(0, 0, 1)
    view.XDirection = FreeCAD.Vector(1, 0, 0)
    view.ScaleType = "Custom"
    view.Scale = 1.0
    view.HardHidden = True
    view.X, view.Y = VIEW_X, VIEW_Y
    doc.recompute()
    sec = doc.addObject("TechDraw::DrawViewSection", "Section")
    page.addView(sec)
    sec.Source = [cut_]
    sec.BaseView = view
    sec.ScaleType = "Custom"
    sec.Scale = 1.0
    sec.Direction = FreeCAD.Vector(0, 1, 0)
    sec.SectionNormal = FreeCAD.Vector(0, 1, 0)
    sec.SectionOrigin = FreeCAD.Vector(15, 12.5, 5)
    sec.X, sec.Y = 220, 120
    doc.recompute()
    vp = FreeCADGui.getDocument(DOC).getObject("Page")
    vp.ShowFrames = True
    vp.show()


def cosmetic():
    # a dashed cosmetic line 0.35 mm wide, 2.5 below the middle (a width
    # that is a whole number of tenths, the 0.5 a new one is given, is the
    # same either way); a view has no geometry to add one to until its
    # hidden line removal has run, which is some turns after the recompute
    doc = FreeCAD.getDocument(DOC)
    doc.getObject("View").makeCosmeticLine(FreeCAD.Vector(-13, -2.5, 0),
                                           FreeCAD.Vector(13, -2.5, 0), 2, 0.35)
    doc.recompute()


def look():
    v = page_view()
    if v is None:
        raise RuntimeError("no page view")
    v.resetTransform()
    v.scale(ZOOM / 10.0, ZOOM / 10.0)
    v.centerOn(QtCore.QPointF(VIEW_X * 10.0, -VIEW_Y * 10.0))


def find_cosmetic():
    view = FreeCAD.getDocument(DOC).getObject("View")
    for i in range(64):
        try:
            edge = view.getEdgeByIndex(i)
        except Exception:
            break
        ys = [v.Point.y for v in edge.Vertexes]
        xs = [v.Point.x for v in edge.Vertexes]
        if len(ys) == 2 and all(abs(abs(y) - 2.5) < 0.01 for y in ys) and abs(
                abs(xs[0] - xs[1]) - 26.0) < 0.01:
            EDGE["name"] = "Edge%d" % i
            note("NOTE the cosmetic line is %s" % EDGE["name"])
            return
    raise RuntimeError("the cosmetic line was not found among the view's edges")


def select():
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(DOC, "View", EDGE["name"])


def deselect():
    FreeCADGui.Selection.clearSelection()


def measure_selected(tag):
    def run():
        img = page_view().grab().toImage()
        img.save(os.path.join(OUT, tag + "-selected.png"))
        paper = SEEN[tag]["paper"]
        best = widest(img, "cosmetic line", lambda px: (ink(px, paper), px))
        px = best[1]
        colour = px[len(px) // 2]
        SEEN[tag + " selected"] = (best[0], colour, other_ink(px, paper, colour))
        note("NOTE %-8s the cosmetic line selected: %.2f px of ink across it, its middle %s, "
             "%.2f px of it neither paper nor that colour" % (
                 tag, best[0], colour, SEEN[tag + " selected"][2]))
    return run


def to_backend():
    switch(True)


def compare():
    qt, vg = SEEN.get("qt"), SEEN.get("backend")
    if not check("both pictures were taken", qt and vg):
        return
    for name in LINES:
        q, v = qt[name][0], vg[name][0]
        if not check("the %s is in both pictures" % name, q > 0.5 and v > 0.5,
                     "Qt %.2f px, the backend %.2f px" % (q, v)):
            continue
        check("the %s is as wide as Qt's" % name, abs(v - q) <= 0.5,
              "Qt %.2f px (%.3f mm), the backend %.2f px (%.3f mm)" % (
                  q, q / ZOOM, v, v / ZOOM))
    qs, vs = SEEN.get("qt selected"), SEEN.get("backend selected")
    if not check("both pictures of the selected line were taken", qs and vs):
        return
    changed = max(abs(qs[1][c] - qt["cosmetic line"][1][c]) for c in range(3)) > 60
    if not check("Qt's picture shows the line selected", changed,
                 "its middle %s unselected, %s selected" % (qt["cosmetic line"][1], qs[1])):
        return
    check("the selected line is the highlight's colour in the backend's picture",
          max(abs(vs[1][c] - qs[1][c]) for c in range(3)) < 40,
          "Qt %s, the backend %s" % (qs[1], vs[1]))
    check("the line under the highlight does not show beside it",
          vs[2] <= qs[2] + 0.25,
          "ink that is neither paper nor highlight: Qt %.2f px, the backend %.2f px" % (
              qs[2], vs[2]))
    check("the highlight is as wide as Qt's", abs(vs[0] - qs[0]) <= 0.5,
          "Qt %.2f px, the backend %.2f px" % (qs[0], vs[0]))


def finish():
    try:
        compare()
    except Exception:
        note("FAIL the comparison ran | " + traceback.format_exc().replace("\n", " | "))
    for (k, d), had in zip(KEYS, HAD):
        GEN.SetBool(k, had)
    try:
        FreeCADGui.Selection.clearSelection()
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
            note("ABORT in %s: %s" % (fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(delay, run)


def shot(tag):
    return [(600, look), (1500, lambda: measure(tag)), (300, select),
            (1500, measure_selected(tag)), (300, deselect)]


STEPS.append((1000, make))
STEPS.append((5000, cosmetic))
STEPS.append((3000, look))
STEPS.append((300, find_cosmetic))
STEPS.extend(shot("qt"))
STEPS.append((500, to_backend))
STEPS.append((4000, look))
STEPS.extend(shot("backend"))
advance()
