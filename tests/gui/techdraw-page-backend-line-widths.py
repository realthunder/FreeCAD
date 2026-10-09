"""A TechDraw page drawn by the backend has its lines at the widths asked for,
fractions included, and a highlight is as wide as the line under it.

"change techdraw bgfx rendering to support fractional line width, but make
sure the highlight shows the same width" (2026-10-09).

The Qt page sets the pen of an edge as a whole number of scene units, a
tenth of a millimetre each (QGIPrimPath::setTools, QPen::setWidth(int)): a
line asked for at 0.35 mm is drawn 0.3 mm wide. The backend draws the 0.35.
What it lays over a line that is preselected or selected is captured off the
Qt item; with the item's pen for its width that was the thinner of the two,
the line showing either side of its highlight (docs/HandsOnQueue.md entry
61: "the hover and selection highlight shows the thinner dash line, which is
barely visible because the underlying thickend line"). The width is taken
off the item now, which knows what it was asked for.

The page is drawn by Qt and then by the backend at 20 pixels to the
millimetre, where a twentieth of a millimetre is a pixel, and the ink across
four lines is added up, fractions of a pixel included:
  - a visible edge (0.7 mm asked for), a hidden line (0.375), a dashed
    cosmetic line (0.35), a section line (0.375): each in the backend's
    picture as wide as was asked, within half a pixel -- and Qt's noted
    beside it, which is the whole tenths below;
  - with the cosmetic line selected, and then the hidden line: the line is
    the highlight's colour, as in Qt's picture; the line under the highlight
    does not show beside it (the ink that is neither the paper's colour nor
    the highlight's, added up across the line, is no more than in Qt's
    picture plus a quarter of a pixel); and the highlight is as wide as the
    line was before it was selected, within half a pixel.
"""
import math
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
COSMETIC_MM = 0.35
STEPS = []
SEEN = {}
EDGES = {}
ASKED = {}


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


def away(p, paper):
    """How far a pixel is from the paper's colour, the furthest channel"""
    return max(abs(paper[c] - p[c]) for c in range(3)) / 255.0


def ink(pixels, paper):
    """How much of the cut is not paper, in pixels"""
    return sum(away(p, paper) for p in pixels)


def width(pixels, paper):
    """How wide the line in the cut is, in pixels, whatever its colour: its
    ink, with a pixel the line covers whole counted as one"""
    full = max(away(p, paper) for p in pixels)
    return ink(pixels, paper) / full if full > 0.2 else 0.0


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
SELECTED = ("cosmetic line", "hidden line")


def widest(img, name, paper):
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
        total = ink(px, paper)
        if best is None or total > best[0]:
            best = (total, px)
    return best[1]


def measure(tag):
    img = page_view().grab().toImage()
    img.save(os.path.join(OUT, tag + ".png"))
    # the middle of the view, where nothing is drawn
    blank = at(0.0, 0.0)
    paper = rgb(img, blank.x(), blank.y())
    out = {"paper": paper}
    for name in LINES:
        px = widest(img, name, paper)
        out[name] = (width(px, paper), px[len(px) // 2])
        note("NOTE %-8s %-14s %.2f px wide (%.3f mm), its middle %s" % (
            tag, name, out[name][0], out[name][0] / ZOOM, out[name][1]))
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
                                           FreeCAD.Vector(13, -2.5, 0), 2, COSMETIC_MM)
    doc.recompute()


def look():
    v = page_view()
    if v is None:
        raise RuntimeError("no page view")
    v.resetTransform()
    v.scale(ZOOM / 10.0, ZOOM / 10.0)
    v.centerOn(QtCore.QPointF(VIEW_X * 10.0, -VIEW_Y * 10.0))


def find_edges():
    """The view's edges that are the cosmetic line and the hidden line: by
    where they lie, the sign of y left open (both long sides of the pocket
    are hidden lines, and both are selected)"""
    view = FreeCAD.getDocument(DOC).getObject("View")
    found = {"cosmetic line": [], "hidden line": []}
    for i in range(64):
        try:
            edge = view.getEdgeByIndex(i)
        except Exception:
            break
        ys = [v.Point.y for v in edge.Vertexes]
        xs = [v.Point.x for v in edge.Vertexes]
        if len(ys) != 2:
            continue
        length = abs(xs[0] - xs[1])
        if all(abs(abs(y) - 2.5) < 0.01 for y in ys) and abs(length - 26.0) < 0.01:
            found["cosmetic line"].append("Edge%d" % i)
        elif all(abs(abs(y) - 5.0) < 0.01 for y in ys) and abs(length - 16.0) < 0.01:
            found["hidden line"].append("Edge%d" % i)
    for name, edges in found.items():
        if not edges:
            raise RuntimeError("the %s was not found among the view's edges" % name)
        note("NOTE the %s is %s" % (name, ", ".join(edges)))
    EDGES.update(found)
    vp = FreeCADGui.getDocument(DOC).getObject("View")
    scale = vp.LineScale
    ASKED["visible edge"] = float(vp.LineWidth) * scale
    ASKED["hidden line"] = float(vp.HiddenWidth) * scale
    ASKED["cosmetic line"] = COSMETIC_MM * scale
    ASKED["section line"] = float(vp.HiddenWidth) * scale
    note("NOTE asked for, mm: %s" % ASKED)


def select(name):
    def run():
        FreeCADGui.Selection.clearSelection()
        for edge in EDGES[name]:
            FreeCADGui.Selection.addSelection(DOC, "View", edge)
    return run


def deselect():
    FreeCADGui.Selection.clearSelection()


def measure_selected(tag, name):
    def run():
        img = page_view().grab().toImage()
        img.save(os.path.join(OUT, "%s-%s-selected.png" % (tag, name.split()[0])))
        paper = SEEN[tag]["paper"]
        px = widest(img, name, paper)
        colour = px[len(px) // 2]
        SEEN[(tag, name)] = (width(px, paper), colour, other_ink(px, paper, colour))
        note("NOTE %-8s the %s selected: %.2f px wide, its middle %s, "
             "%.2f px of ink neither paper nor that colour" % (
                 tag, name, SEEN[(tag, name)][0], colour, SEEN[(tag, name)][2]))
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
        asked = ASKED[name]
        check("the %s is as wide as was asked for" % name,
              abs(v - asked * ZOOM) <= 0.5,
              "asked %.3f mm (%.2f px), the backend %.3f mm (%.2f px); Qt %.3f mm, "
              "which sets whole tenths, %.1f" % (
                  asked, asked * ZOOM, v / ZOOM, v, q / ZOOM,
                  math.floor(asked * 10.0 + 1e-6) / 10.0))
    for name in SELECTED:
        qs, vs = SEEN.get(("qt", name)), SEEN.get(("backend", name))
        if not check("both pictures of the selected %s were taken" % name, qs and vs):
            continue
        changed = max(abs(qs[1][c] - qt[name][1][c]) for c in range(3)) > 60
        if not check("Qt's picture shows the %s selected" % name, changed,
                     "its middle %s unselected, %s selected" % (qt[name][1], qs[1])):
            continue
        check("the selected %s is the highlight's colour in the backend's picture" % name,
              max(abs(vs[1][c] - qs[1][c]) for c in range(3)) < 40,
              "Qt %s, the backend %s" % (qs[1], vs[1]))
        check("the %s under its highlight does not show beside it" % name,
              vs[2] <= qs[2] + 0.25,
              "ink that is neither paper nor highlight: Qt %.2f px, the backend %.2f px" % (
                  qs[2], vs[2]))
        check("the highlight of the %s is as wide as the line" % name,
              abs(vs[0] - vg[name][0]) <= 0.5,
              "the line %.2f px, selected %.2f px (Qt's: %.2f and %.2f)" % (
                  vg[name][0], vs[0], qt[name][0], qs[0]))


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
    steps = [(600, look), (1500, lambda: measure(tag))]
    for name in SELECTED:
        steps += [(300, select(name)), (1500, measure_selected(tag, name)), (300, deselect)]
    return steps


STEPS.append((1000, make))
STEPS.append((5000, cosmetic))
STEPS.append((3000, look))
STEPS.append((300, find_edges))
STEPS.extend(shot("qt"))
STEPS.append((500, to_backend))
STEPS.append((4000, look))
STEPS.extend(shot("backend"))
advance()
