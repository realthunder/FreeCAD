"""A TechDraw page drawn by the backend has the Qt page's hatch on a cut face.

docs/HandsOnQueue.md entry 46: "the hatch of a section's cut face is bright
green lines far apart, where Qt draws a fine grey-green pattern". The hatch
is an SVG tile (simple.svg: lines a tenth of a millimetre wide, 1.6 mm apart
along a row) that the feed rasterizes once, a texel to the tenth of a
millimetre. At two pixels to the millimetre five texels fall on a pixel, and
a picture sampled without anything coarser to sample from keeps one texel of
them: most lines are dropped and the ones that are hit come out at full
strength. The page layer now keeps coarser copies of every picture and draws
from the one that fits the zoom (Render::Page2D, the image upload).

The hatch is also no longer a picture of the whole face but one tile, laid
side by side into the face's outline (Page2D::Recorder::fillImage), placed
as the Qt page places its tiles and sharp far into the zoom.

Claims, the page drawn by Qt and then by the backend at 2, 5, 12 and 30
pixels to the millimetre, a strip inside the section's cut face read back
and each pixel placed between the face's own colour (0) and the hatch's (1):
  - the strip is inside the face in both pictures and the hatch is there
    (the measurement finds what it looks for);
  - the backend's lines are as close together as Qt's: as many of them
    cross a row of pixels;
  - none of them is stronger than a line of its width can be -- a line a
    fifth of a pixel wide inks a pixel it runs through by a fifth and a bit
    -- and where a line is a pixel wide or more it is no weaker than six
    tenths of Qt's (see below);
  - the face is as inked over all as Qt's;
  - where a line is a pixel wide or more, the backend's lines lie on Qt's:
    the two pictures differ by less than they would with the lines apart.
  - the hatch stops under the face's outline, as Qt's does: an image used
    to be drawn over all the line work of a page.
Then the hatch turned by 30 degrees and shifted, at 12 pixels to the
millimetre, the backend first and Qt after it: the same claims, so the
turn and the shift are Qt's too.

Not claimed, two differences that are left:
  - a line under a pixel wide is not as PALE as Qt's. Qt inks such a line
    by about three quarters of what it covers (0.064 of the face for the
    0.088 the pattern covers, at 2 and at 5 pixels to the millimetre); the
    backend inks what is covered;
  - a line about a pixel wide is paler than Qt's, 0.70 of the hatch colour
    for Qt's 0.94 at 12 pixels to the millimetre. The tile is read from the
    copy whose pixels are nearest the screen's in size, which can be up to
    1.41 screen pixels each: a line one such pixel wide is spread over two
    of the screen's. At 30 pixels to the millimetre both are at full
    strength.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "HatchCompare"
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
KEYS = (("PageRendererVg", False), ("PageRendererVgComposite", True),
        ("PageRendererVgVerify", False))
HAD = [GEN.GetBool(k, d) for k, d in KEYS]
SEC_X, SEC_Y = 220.0, 120.0
# the strip read back, page millimetres: the half of the cut face beside the
# pocket, which the section cuts through on the other half
STRIP = (SEC_X + 1.0, SEC_Y - 20.0, SEC_X + 9.0, SEC_Y + 20.0)
# the inner half of the face's outline on that side: the face ends 10 mm from
# the middle, in the middle of an edge 0.7 mm wide
OUTLINE = (SEC_X + 9.75, SEC_X + 9.95)
# pixels to the millimetre: a scene unit is a tenth of a millimetre
ZOOMS = (2.0, 5.0, 12.0, 30.0)
TURNED = "turned"
STEPS = []
SEEN = {}
COLOURS = {}


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
    return page_view().mapFromScene(QtCore.QPointF(mm_x * 10.0, -mm_y * 10.0))


def rgb(p):
    return ((p >> 16) & 255, (p >> 8) & 255, p & 255)


def lines_in(row, prominence):
    """How many lines cross a row of ink values: the peaks that stand
    `prominence` above the valley on either side."""
    count = 0
    low = high = row[0]
    rising = True
    for v in row[1:]:
        if rising:
            if v > high:
                high = v
            elif v < high - prominence:
                count += 1
                rising = False
                low = v
        else:
            if v < low:
                low = v
            elif v > low + prominence:
                rising = True
                high = v
    return count


def measure(tag, zoom):
    img = page_view().grab().toImage()
    img.save(os.path.join(OUT, "%s-%g.png" % (tag, zoom)))
    a, b = at(STRIP[0], STRIP[3]), at(STRIP[2], STRIP[1])
    # zoomed far in the strip is taller than the view: what of it is on screen
    # (the picture is the widget's, scroll bars and all: the viewport's height)
    top = max(a.y(), 4)
    bottom = min(b.y(), page_view().viewport().height() - 5)
    fill, hatch = COLOURS["fill"], COLOURS["hatch"]
    axis = [hatch[i] - fill[i] for i in range(3)]
    norm = float(sum(c * c for c in axis)) or 1.0
    rows = []
    paper = ink_free = 0
    for y in range(top, bottom + 1):
        row = []
        for x in range(a.x(), b.x() + 1):
            p = rgb(img.pixel(x, y))
            if min(p) > 250:
                paper += 1
            row.append(sum((p[i] - fill[i]) * axis[i] for i in range(3)) / norm)
        rows.append(row)
    flat = sorted(v for row in rows for v in row)
    n = len(flat)
    low, high = flat[int(n * 0.02)], flat[int(n * 0.98)]
    for v in flat:
        if abs(v) < 0.02:
            ink_free += 1
    prominence = max(0.25 * (high - low), 0.01)
    # hatch colour on the outline: pixels far greener than they are anything else
    on_outline = 0
    for y in range(top, bottom + 1):
        for x in range(at(OUTLINE[0], SEC_Y).x(), at(OUTLINE[1], SEC_Y).x() + 1):
            p = rgb(img.pixel(x, y))
            if p[1] > p[0] + 60 and p[1] > p[2] + 60:
                on_outline += 1
    per_row = [lines_in(row, prominence) for row in rows]
    out = {
        "pixels": n,
        "width": b.x() - a.x() + 1,
        "paper": paper,
        "mean": sum(flat) / n,
        "strong": high,
        "lines": sum(per_row) / float(len(per_row)),
        "clear": ink_free / float(n),
        "rows": rows,
        "outline": on_outline,
    }
    SEEN[(tag, zoom)] = out
    note("NOTE %-8s %4g px/mm  strip %d px wide, %d pixels: ink %.3f over all, "
         "strongest %.2f, %.1f lines across a row, %.0f%% of it clear" % (
             tag, zoom, out["width"], n, out["mean"], out["strong"], out["lines"],
             100.0 * out["clear"]))


def apart(qt, vg):
    """How far the two pictures of the strip are from each other: the mean
    difference of a pixel's ink, as a share of what it would be with no line
    of one on a line of the other (the ink of both, added)."""
    total = count = 0
    for qrow, vrow in zip(qt["rows"], vg["rows"]):
        for q, v in zip(qrow, vrow):
            total += abs(q - v)
            count += 1
    both = qt["mean"] + vg["mean"]
    return (total / count) / both if count and both > 0 else 1.0


def set_zoom(zoom):
    v = page_view()
    v.resetTransform()
    v.scale(zoom / 10.0, zoom / 10.0)
    v.centerOn(QtCore.QPointF((SEC_X + 5.0) * 10.0, -SEC_Y * 10.0))


def make():
    import TechDrawGui  # noqa: F401  the view providers
    switch(False)
    doc = FreeCAD.newDocument(DOC)
    outer = doc.addObject("Part::Box", "Outer")
    outer.Length, outer.Width, outer.Height = 60, 40, 20
    inner = doc.addObject("Part::Box", "Inner")
    inner.Length, inner.Width, inner.Height = 30, 20, 10
    inner.Placement.Base = FreeCAD.Vector(15, 10, 0)
    cut = doc.addObject("Part::Cut", "Cut")
    cut.Base, cut.Tool = outer, inner
    doc.recompute()
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "View")
    page.addView(view)
    view.Source = [cut]
    view.Direction = FreeCAD.Vector(0, 0, 1)
    view.XDirection = FreeCAD.Vector(1, 0, 0)
    view.ScaleType = "Custom"
    view.Scale = 1.0
    view.X, view.Y = 110, 120
    doc.recompute()
    sec = doc.addObject("TechDraw::DrawViewSection", "Section")
    page.addView(sec)
    sec.Source = [cut]
    sec.BaseView = view
    sec.ScaleType = "Custom"
    sec.Scale = 1.0
    sec.Direction = FreeCAD.Vector(0, 1, 0)
    sec.SectionNormal = FreeCAD.Vector(0, 1, 0)
    sec.SectionOrigin = FreeCAD.Vector(30, 25, 10)
    sec.X, sec.Y = SEC_X, SEC_Y
    doc.recompute()
    gdoc = FreeCADGui.getDocument(DOC)
    gdoc.getObject("Page").show()
    svp = gdoc.getObject("Section")
    COLOURS["fill"] = [int(round(c * 255)) for c in svp.CutSurfaceColor[:3]]
    COLOURS["hatch"] = [int(round(c * 255)) for c in svp.HatchColor[:3]]
    note("NOTE the cut face is shown as %s, pattern %s at scale %g; face colour %s, "
         "hatch colour %s" % (sec.CutSurfaceDisplay, os.path.basename(sec.SvgIncluded),
                              sec.HatchScale, COLOURS["fill"], COLOURS["hatch"]))


def look():
    if page_view() is None:
        raise RuntimeError("no page view")


def zoom_steps(tag, zoom):
    def set_it():
        set_zoom(zoom)

    def read_it():
        measure(tag, zoom)

    return [(600, set_it), (1500, read_it)]


def to_backend():
    switch(True)


def to_qt():
    switch(False)


def turn():
    sec = FreeCAD.getDocument(DOC).getObject("Section")
    sec.HatchRotation = 30.0
    sec.HatchOffset = FreeCAD.Vector(7.0, 3.0, 0.0)
    FreeCAD.getDocument(DOC).recompute()


def compare():
    for zoom in ZOOMS + (TURNED,):
        if zoom == TURNED:
            qt, vg = SEEN.get(("qt-turned", 12.0)), SEEN.get(("backend-turned", 12.0))
            zoom, name = 12.0, "turned and shifted, 12"
        else:
            qt, vg = SEEN.get(("qt", zoom)), SEEN.get(("backend", zoom))
            name = "%g" % zoom
        compare_one(name, zoom, qt, vg)


def compare_one(name, zoom, qt, vg):
    if not check("%s px/mm: both pictures were taken" % name, qt and vg):
        return
    if not check("%s px/mm: the strip is inside the cut face in both" % name,
                 qt["paper"] == 0 and vg["paper"] == 0,
                 "pixels of bare paper: Qt %d, the backend %d" % (qt["paper"], vg["paper"])):
        return
    if not check("%s px/mm: the hatch is there in both" % name,
                 qt["mean"] > 0.01 and vg["mean"] > 0.01,
                 "ink over all: Qt %.3f, the backend %.3f" % (qt["mean"], vg["mean"])):
        return
    check("%s px/mm: the lines are as close together as Qt's" % name,
          abs(vg["lines"] - qt["lines"]) <= max(0.2 * qt["lines"], 0.5),
          "lines across a row of %d px: Qt %.1f, the backend %.1f" % (
              qt["width"], qt["lines"], vg["lines"]))
    # a line a tenth of a millimetre wide, at 45 degrees through a pixel
    can_be = min(1.0, zoom * 0.1 * 1.4142 + 0.1)
    check("%s px/mm: no line is stronger than a line that wide can be" % name,
          vg["strong"] <= can_be,
          "the backend %.2f of the hatch colour, at most %.2f; Qt %.2f" % (
              vg["strong"], can_be, qt["strong"]))
    check("%s px/mm: the face is as inked over all as Qt's" % name,
          qt["mean"] / 1.5 <= vg["mean"] <= qt["mean"] * 1.5,
          "Qt %.3f, the backend %.3f" % (qt["mean"], vg["mean"]))
    off = apart(qt, vg)
    # a line of the pattern is a tenth of a millimetre wide
    if zoom * 0.1 < 1.0:
        note("NOTE %s px/mm: the two pictures are %.2f apart (1 = no line on a line); "
             "a line is under a pixel wide, not scored" % (name, off))
        return
    check("%s px/mm: no line is weaker than six tenths of Qt's strongest" % name,
          vg["strong"] >= 0.6 * qt["strong"],
          "Qt %.2f, the backend %.2f of the hatch colour" % (qt["strong"], vg["strong"]))
    check("%s px/mm: the backend's lines lie on Qt's" % name, off <= 0.5,
          "%.2f apart, where 1 is no line of one on a line of the other" % off)
    check("%s px/mm: the hatch stops under the outline, as Qt's does" % name,
          vg["outline"] <= qt["outline"],
          "pixels of hatch colour on the outline's inner half: Qt %d, the backend %d" % (
              qt["outline"], vg["outline"]))


def finish():
    try:
        compare()
    except Exception:
        note("FAIL the comparison ran | " + traceback.format_exc().replace("\n", " | "))
    for (k, d), had in zip(KEYS, HAD):
        GEN.SetBool(k, had)
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
            note("ABORT in %s: %s" % (fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(delay, run)


STEPS.append((1000, make))
STEPS.append((5000, look))
for _z in ZOOMS:
    STEPS.extend(zoom_steps("qt", _z))
STEPS.append((500, to_backend))
STEPS.append((4000, look))
for _z in ZOOMS:
    STEPS.extend(zoom_steps("backend", _z))
STEPS.append((500, turn))
STEPS.append((3000, look))
STEPS.extend(zoom_steps("backend-turned", 12.0))
STEPS.append((500, to_qt))
STEPS.append((4000, look))
STEPS.extend(zoom_steps("qt-turned", 12.0))
advance()
