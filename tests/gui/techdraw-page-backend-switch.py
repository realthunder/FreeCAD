"""A TechDraw page switched to the backend's page layer and back, open.

Mod/TechDraw/General/PageRendererVg makes the page view draw the page
through the render backend underneath its Qt items
(docs/HandsOnQueue.md entry 20).

The switch. Switched on with a page already open, on a session whose
backend is not OpenGL (Direct3D 11, the Windows default), the view swapped
its viewport for a GL one and warmed the backend up from inside that
viewport's first paint; the warm-up left the painter with no current GL
context and Qt dereferenced it -- an access violation in
QOpenGLContext::isOpenGLES. Switched off again, the view kept the GL
viewport and the uncached background it had taken for the layer.

The picture. The layer painted the backdrop over in white; it drew a
projection group's items a second time at the corner of the sheet, their
X/Y being relative to the group; and it drew an arc of circle from the
curve's parameters, which are measured from the circle's own axis, so an
arc came out on the right circle and the wrong part of it.

Claims:
  - with the layer read back into a raster viewport (the compositor
    switched off, which is what a session not on OpenGL gets anyway): the
    backdrop is still there, and the layer puts no ink away from what Qt
    draws -- a group item at the corner or an arc turned round its circle
    would;
  - switching the layer on with the page open is survived, and so is
    switching it off and on again (a crash leaves no DONE line);
  - switched off, the view is back on a raster viewport with its
    background cached, and the page is the one it was before.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PageBackendSwitch"
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
HAD = (GEN.GetBool("PageRendererVg", False), GEN.GetBool("PageRendererVgComposite", True))
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


def is_gl(view):
    return view.viewport().metaObject().className() == "QOpenGLWidget"


def cached(view):
    mode = view.cacheMode()
    return int(getattr(mode, "value", mode)) != 0


def pixels(view):
    """The page as painted now, as rows of 0xRRGGBB."""
    img = view.grab().toImage()
    if STATE.get("save"):
        img.save(os.path.join(OUT, STATE["save"] + ".png"))
    return [[img.pixel(x, y) & 0xFFFFFF for x in range(img.width())]
            for y in range(img.height())]


def ink(p):
    """Drawn on: neither the backdrop nor (nearly) the white of the sheet. A
    thin line of the layer is grey, not black."""
    return p != STATE["backdrop"] and max((p >> 16) & 255, (p >> 8) & 255, p & 255) < 200


def ink_cells(rows):
    """How many drawn-on pixels each CELL x CELL square of a grab holds."""
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


def differing(a, b):
    if len(a) != len(b) or len(a[0]) != len(b[0]):
        return -1
    return sum(1 for ra, rb in zip(a, b) for pa, pb in zip(ra, rb) if pa != pb)


def switch(on, composite=True):
    GEN.SetBool("PageRendererVgComposite", composite)
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
    # A hole through one edge, and the cylinder turned about its axis: the
    # arcs left in the box are on circles whose own X axis is not the page's.
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
    group.X = 170
    group.Y = 130
    doc.recompute()
    FreeCADGui.getDocument(DOC).getObject("Page").show()


@step(4000)
def before():
    v = page_view()
    if not check("the page has its view", v is not None):
        raise RuntimeError("no page view")
    STATE["save"] = "1-qt"
    STATE["qt"] = pixels(v)
    STATE["backdrop"] = STATE["qt"][2][2]
    ink = sum(ink_cells(STATE["qt"]).values())
    check("Qt-painted: raster viewport, cached background, the group drawn",
          not is_gl(v) and cached(v) and ink > 1000,
          (v.viewport().metaObject().className(), cached(v), ink))
    switch(True, composite=False)


@step(4000)
def read_back():
    v = page_view()
    if not check("layer on, read back: survived on a raster viewport",
                 v is not None and not is_gl(v) and not cached(v)):
        raise RuntimeError("no raster viewport to read")
    STATE["save"] = "2-layer"
    now = pixels(v)
    check("layer on: the backdrop is still there",
          now[2][2] == STATE["backdrop"], "%06x" % now[2][2])
    n = ink_away(STATE["qt"], now)
    check("layer on: no ink away from what Qt draws", n < 20, "%d pixels" % n)
    STATE["save"] = None
    switch(False)


@step(3000)
def back():
    v = page_view()
    check("switched off: raster viewport, background cached again",
          v is not None and not is_gl(v) and cached(v))
    switch(True)


@step(4000)
def on():
    v = page_view()
    check("switched on with the page open: survived, background no longer cached",
          v is not None and not cached(v))
    note("NOTE the layer is %s here" % (
        "composited in a GL viewport" if is_gl(v) else "read back into a raster viewport"))
    switch(False)


@step(3000)
def off():
    v = page_view()
    check("switched off: raster viewport again, background cached again",
          v is not None and not is_gl(v) and cached(v),
          (v.viewport().metaObject().className(), cached(v)))
    n = differing(STATE["qt"], pixels(v))
    check("switched off: the page is the one it was", n == 0, "%d pixels differ" % n)
    switch(True)


@step(3000)
def on_again():
    v = page_view()
    check("switched on a second time: survived", v is not None and not cached(v))
    switch(False)


@step(1500)
def off_again():
    v = page_view()
    check("and off: raster viewport, cached", not is_gl(v) and cached(v))


def finish():
    GEN.SetBool("PageRendererVg", HAD[0])
    GEN.SetBool("PageRendererVgComposite", HAD[1])
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
