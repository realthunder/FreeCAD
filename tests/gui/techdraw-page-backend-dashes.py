"""A TechDraw page drawn by the backend has the Qt page's dashes, at any zoom.

docs/HandsOnQueue.md entry 36: "most dashed line rendering does not behave
the same as Qt, namely view frame, section line, hidden line, and so on",
the zoom above all. Measured before this was written, the same page drawn by
both at 2, 5 and 12 pixels to the millimetre:
  - zoomed out, the backend's dashes closed up -- a hidden line of 0.35 mm
    with gaps of one pixel, to the eye a continuous line -- where Qt counts
    the pattern of a pen thinner than a pixel in pixels and keeps the gaps;
  - zoomed in, a hidden line had six dashes for Qt's seven: Qt's caps
    lengthen each dash by a pen width, and its pen is 0.3 mm wide (the Qt
    page sets a pen's width in whole scene units);
  - a view's frame, a cosmetic pen, had dashes that grew with the zoom,
    where Qt's are the same few pixels at any.
The page layer now works the dashes out for the zoom it draws at
(Render::Page2D::dashedPolyline).

Claims, the page drawn by Qt and then by the backend at each of the three
zooms, the dashes counted along a hidden line, a section line and the top
side of the view's frame:
  - every one of them is dashed in both (the measurement finds what it
    looks for);
  - the hidden line and the section line have as many dashes as Qt's, give
    or take one where the pen is thinner than a pixel -- there the layer's
    pixel is the zoom band's, up to sqrt(2) off the screen's;
  - their gaps can be seen: two pixels or more wherever Qt's are;
  - the frame's dashes are a few pixels long at every zoom, as Qt's are,
    and do not grow with it.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DashCompare"
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
KEYS = (("PageRendererVg", False), ("PageRendererVgComposite", True),
        ("PageRendererVgVerify", False))
HAD = [GEN.GetBool(k, d) for k, d in KEYS]
VIEW_X, VIEW_Y = 110.0, 120.0
# pixels to the millimetre: a scene unit is a tenth of a millimetre
ZOOMS = (2.0, 5.0, 12.0)
STEPS = []
SEEN = {}


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


def dark(img, x, y):
    if not (0 <= x < img.width() and 0 <= y < img.height()):
        return False
    p = img.pixel(x, y)
    return max((p >> 16) & 255, (p >> 8) & 255, p & 255) < 150


def runs(img, a, b, half):
    """Runs of ink from a to b, a line along x or along y, within `half`
    pixels either side of it: (start, length) in pixels along the line."""
    across = a.y() == b.y()
    n = (b.x() - a.x()) if across else (b.y() - a.y())
    step = 1 if n >= 0 else -1
    res = []
    start = None
    for i in range(abs(n) + 1):
        if across:
            x = a.x() + i * step
            on = any(dark(img, x, a.y() + d) for d in range(-half, half + 1))
        else:
            y = a.y() + i * step
            on = any(dark(img, a.x() + d, y) for d in range(-half, half + 1))
        if on and start is None:
            start = i
        elif not on and start is not None:
            res.append((start, i - start))
            start = None
    if start is not None:
        res.append((start, abs(n) + 1 - start))
    return res


def summary(rs):
    """(dashes, the mean length of those not cut by the ends, the mean gap)"""
    if not rs:
        return (0, 0.0, 0.0)
    lengths = [r[1] for r in rs]
    inner = lengths[1:-1] or lengths
    gaps = [rs[i + 1][0] - (rs[i][0] + rs[i][1]) for i in range(len(rs) - 1)]
    return (len(rs), sum(inner) / len(inner), sum(gaps) / len(gaps) if gaps else 0.0)


def frame_side(img):
    """The top side of the view's frame: the row with the most runs a little
    above the box's top edge (20 above the middle, 0.7 thick)"""
    low, high = at(VIEW_X - 25, VIEW_Y + 20.7), at(VIEW_X + 25, VIEW_Y + 24.0)
    best = []
    for y in range(high.y(), low.y() + 1):
        rs = runs(img, QtCore.QPoint(low.x(), y), QtCore.QPoint(high.x(), y), 0)
        if len(rs) > len(best):
            best = rs
    return best


def measure(tag, zoom):
    img = page_view().grab().toImage()
    img.save(os.path.join(OUT, "%s-%g.png" % (tag, zoom)))
    out = {
        # the hidden pocket's upper side: 10 above the middle, from -15 to 15
        "hidden line": summary(runs(img, at(VIEW_X - 13, VIEW_Y + 10),
                                    at(VIEW_X + 13, VIEW_Y + 10), 2)),
        # the section line, 5 above the middle: between the box's side and the pocket's
        "section line": summary(runs(img, at(VIEW_X - 28, VIEW_Y + 5),
                                     at(VIEW_X - 17, VIEW_Y + 5), 2)),
        "frame": summary(frame_side(img)),
    }
    SEEN[(tag, zoom)] = out
    for k, (n, length, gap) in out.items():
        note("NOTE %-8s %4g px/mm  %-12s %2d dashes of %5.1f px, %5.1f px apart" % (
            tag, zoom, k, n, length, gap))


def set_zoom(zoom):
    v = page_view()
    v.resetTransform()
    v.scale(zoom / 10.0, zoom / 10.0)
    v.centerOn(QtCore.QPointF(VIEW_X * 10.0, -(VIEW_Y + 6) * 10.0))


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
    view.HardHidden = True
    view.X, view.Y = VIEW_X, VIEW_Y
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
    sec.X, sec.Y = 220, 120
    doc.recompute()
    vp = FreeCADGui.getDocument(DOC).getObject("Page")
    vp.ShowFrames = True
    vp.show()


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


def compare():
    for zoom in ZOOMS:
        qt, vg = SEEN.get(("qt", zoom)), SEEN.get(("backend", zoom))
        if not check("%g px/mm: both pictures were taken" % zoom, qt and vg):
            continue
        for line in ("hidden line", "section line"):
            qn, qlen, qgap = qt[line]
            vn, vlen, vgap = vg[line]
            if qn < 2:
                # too short on screen at this zoom for two of Qt's dashes: nothing
                # to compare (the stretch of section line measured is 11 mm)
                note("NOTE %g px/mm: the %s is not two dashes long in Qt's picture" % (zoom, line))
                continue
            if not check("%g px/mm: the %s is dashed in both" % (zoom, line),
                         qn >= 2 and vn >= 2, (qt[line], vg[line])):
                continue
            # a pen thinner than a pixel: Qt counts in the screen's pixels, the
            # layer in its zoom band's
            thin = zoom * 0.3 < 1.0
            check("%g px/mm: the %s has as many dashes as Qt's%s" % (
                zoom, line, ", give or take one" if thin else ""),
                abs(vn - qn) <= (1 if thin else 0), "Qt %d, the backend %d" % (qn, vn))
            check("%g px/mm: the %s's gaps can be seen" % (zoom, line),
                  vgap >= 2.0 or vgap >= qgap - 0.5,
                  "Qt %.1f px, the backend %.1f px" % (qgap, vgap))
        qn, qlen, qgap = qt["frame"]
        vn, vlen, vgap = vg["frame"]
        if check("%g px/mm: the frame is dashed in both" % zoom, qn >= 2 and vn >= 2,
                 (qt["frame"], vg["frame"])):
            check("%g px/mm: the frame's dashes are Qt's few pixels, within the band" % zoom,
                  qlen / 1.6 <= vlen <= qlen * 1.6 and vlen <= 12.0,
                  "Qt %.1f px, the backend %.1f px" % (qlen, vlen))


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
advance()
