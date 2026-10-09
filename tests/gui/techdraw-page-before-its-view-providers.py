"""A TechDraw page shown before the view providers of its views exist is drawn
again when they do.

docs/HandsOnQueue.md entry 53: "opening the scanner file, it will prompt for
recompute. if do not recompute, the auto opened techdraw page only shows part
of the geometry", a different part at each open -- "sometimes no geometry
are shown. there's one time I saw Top002 is shown with thickened edge" -- and
"both renderer draws line thickened and only back to normal after recompute".

A progressive load (View/Render/ProgressiveLoad, on by default) builds the
view providers in slices after the document has opened, and a page that
comes back with the window layout is drawn in between. Its item for a view
whose view provider was not there yet drew nothing (the Qt page) or drew by
fallback widths, 0.6 mm for every line (the backend's page layer); the view
provider, once built, did not ask for the view to be painted again. Only a
recompute did.

The document here is made to open that way: the page first, then many
objects, the view last, so the view's view provider comes long after the
page's; saved with the page open, a line width of 0.18 mm on the view.

Claims, the document reopened and NOT recomputed, for the page drawn by Qt
and then by the backend, once the load has built everything:
  - the page is on screen and the view's view provider exists (the test
    stands on what it means to test);
  - the view's outline is drawn: both upright edges of the box are found on
    a row of pixels through it;
  - they are as wide as the view provider says, under half the fallback's
    width;
  - asking the view to paint again changes nothing: it was whole already.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import shiboken6
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PageBeforeProviders"
PATH = os.path.join(OUT, DOC + ".FCStd")
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
KEYS = (("PageRendererVg", False), ("PageRendererVgComposite", True),
        ("PageRendererVgVerify", False))
HAD = [GEN.GetBool(k, d) for k, d in KEYS]
FILLERS = 300
VIEW_X, VIEW_Y = 150.0, 100.0
ZOOM = 10.0          # pixels to the millimetre
WIDTH = 0.18         # mm, the view's line width; the fallback draws 0.6
STEPS = []
SEEN = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def page_view():
    for v in FreeCADGui.getMainWindow().findChildren(QtWidgets.QGraphicsView):
        if shiboken6.isValid(v) and v.metaObject().className() == "TechDrawGui::QGVPage" \
                and v.isVisible():
            return v
    return None


def switch(on):
    # read back into a raster viewport: a widget grab cannot read a GL one
    GEN.SetBool("PageRendererVgComposite", False)
    GEN.SetBool("PageRendererVgVerify", False)
    GEN.SetBool("PageRendererVg", on)


def make():
    import TechDrawGui  # noqa: F401  the view providers
    switch(False)
    doc = FreeCAD.newDocument(DOC)
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    for i in range(FILLERS):
        b = doc.addObject("Part::Box", "Filler%03d" % i)
        b.Placement.Base = FreeCAD.Vector(200 + 12 * (i % 20), 12 * (i // 20), 0)
    box = doc.addObject("Part::Box", "Model")
    box.Length, box.Width, box.Height = 40, 30, 20
    doc.recompute()
    view = doc.addObject("TechDraw::DrawViewPart", "View")
    page.addView(view)
    view.Source = [box]
    view.Direction = FreeCAD.Vector(0, -1, 0)
    view.XDirection = FreeCAD.Vector(1, 0, 0)
    view.ScaleType = "Custom"
    view.Scale = 1.0
    view.X, view.Y = VIEW_X, VIEW_Y
    doc.recompute()
    gdoc = FreeCADGui.getDocument(DOC)
    gdoc.getObject("View").LineWidth = WIDTH
    # no frames: a view's frame is a dashed line a millimetre outside its
    # outline, and whether a dash of it is on the row measured is chance
    gdoc.getObject("Page").ShowFrames = False
    gdoc.getObject("Page").doubleClicked()


def save_and_close():
    if page_view() is None:
        raise RuntimeError("the page did not open")
    doc = FreeCAD.getDocument(DOC)
    doc.saveAs(PATH)
    FreeCAD.closeDocument(DOC)


def reopen():
    RENDER.SetBool("ProgressiveLoad", True)
    # a slice builds what it can in this long, then lets the window paint
    RENDER.SetInt("ProgressiveLoadBudgetMS", 1)
    FreeCAD.openDocument(PATH)


def edges():
    """The box's upright edges on a row of pixels through the view: how many
    runs of ink, and how wide they are on average, in pixels"""
    v = page_view()
    v.resetTransform()
    v.scale(ZOOM / 10.0, ZOOM / 10.0)
    v.centerOn(QtCore.QPointF(VIEW_X * 10.0, -VIEW_Y * 10.0))
    QtCore.QCoreApplication.processEvents()
    return v


def measure(tag):
    def fn():
        v = page_view()
        if v is None:
            SEEN[tag] = None
            return
        img = v.grab().toImage()
        img.save(os.path.join(OUT, tag + ".png"))
        mid = v.mapFromScene(QtCore.QPointF(VIEW_X * 10.0, -(VIEW_Y + 4.0) * 10.0))
        left = v.mapFromScene(QtCore.QPointF((VIEW_X - 26.0) * 10.0, 0)).x()
        right = v.mapFromScene(QtCore.QPointF((VIEW_X + 26.0) * 10.0, 0)).x()
        runs = []
        start = None
        for x in range(max(left, 0), min(right, img.width() - 1) + 1):
            p = img.pixel(x, mid.y())
            dark = max((p >> 16) & 255, (p >> 8) & 255, p & 255) < 140
            if dark and start is None:
                start = x
            elif not dark and start is not None:
                runs.append(x - start)
                start = None
        if start is not None:
            runs.append(right - start)
        SEEN[tag] = runs
        note("NOTE %s: runs of ink along the row, in pixels: %s" % (tag, runs))
    fn.__name__ = "measure_" + tag
    return fn


def zoom():
    if page_view() is not None:
        edges()


def loaded(kind):
    def fn():
        gdoc = FreeCADGui.getDocument(DOC)
        ok = check("%s: the page is on screen after the load" % kind, page_view() is not None)
        ok = check("%s: the view's view provider exists" % kind,
                   gdoc.getObject("View") is not None
                   and gdoc.getObject("Filler%03d" % (FILLERS - 1)) is not None) and ok
        if not ok:
            raise RuntimeError("nothing to measure")
    fn.__name__ = "loaded"
    return fn


def repaint():
    FreeCAD.getDocument(DOC).getObject("View").requestPaint()


def compare(kind):
    def fn():
        first, again = SEEN.get(kind + "-loaded"), SEEN.get(kind + "-again")
        if not check("%s: both pictures were taken" % kind, first is not None and again is not None):
            return
        check("%s: the view's outline is drawn as loaded" % kind, len(first) == 2,
              "%d runs of ink where the box has two upright edges" % len(first))
        if len(first) == 2:
            # 0.18 mm is 1.8 px here, the fallback's 0.6 mm is 6
            check("%s: its lines are as wide as the view provider says" % kind,
                  max(first) <= 0.5 * 0.6 * ZOOM,
                  "%s px wide; %.1f px asked, the fallback draws %.0f" % (
                      first, WIDTH * ZOOM, 0.6 * ZOOM))
        check("%s: asking the view to paint again changes nothing" % kind, first == again,
              "%s as loaded, %s after" % (first, again))
    fn.__name__ = "compare"
    return fn


def close():
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass


def to_backend():
    switch(True)


def finish():
    close()
    for (k, d), had in zip(KEYS, HAD):
        GEN.SetBool(k, had)
    RENDER.RemInt("ProgressiveLoadBudgetMS")
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
            note("FAIL the step %s ran | %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(int(delay), run)


STEPS.append((1500, make))
STEPS.append((6000, save_and_close))
for _kind in ("Qt", "backend"):
    if _kind == "backend":
        STEPS.append((500, to_backend))
    STEPS.append((1500, reopen))
    STEPS.append((12000, loaded(_kind)))
    STEPS.append((300, zoom))
    STEPS.append((2500, measure(_kind + "-loaded")))
    STEPS.append((300, repaint))
    STEPS.append((2500, measure(_kind + "-again")))
    STEPS.append((300, compare(_kind)))
    STEPS.append((500, close))
advance()
