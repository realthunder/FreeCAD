"""A click on a TechDraw view's empty inside goes through; its frame and its
label still pick it.

docs/HandsOnQueue.md entry 149 (the reporter, 2026-10-15 16:40): "right now
Techdraw view is opaque on picking, even they have transparent background.
Try make the pick ignore transparent background. but border should be
pickable". A view took a click anywhere inside its frame: Qt picks an item
by its shape, and a view's was its whole bounding rectangle.

A part view at 1:1 of two 20 mm boxes 40 mm apart: its frame spans both,
the gap between them is empty. Claims:
  - a click in the gap selects nothing;
  - a click on the frame's top line, over the gap, selects the view;
  - a click on the view's label selects the view;
  - a click on an edge selects that edge, as before;
  - entry 148 (the reporter, 16:38 and 16:47: edges and faces that light
    on hover and select nothing): with a diagonal dimension across the
    view, whose rectangle covers it all, a click on an edge away from the
    dimension's lines still selects the edge. A dimension took a press
    anywhere in its rectangle.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ViewClickThrough"
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
HAD = GEN.GetBool("PageRendererVg", False)
VIEW_X, VIEW_Y = 140.0, 110.0
STEPS = []


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


def at(mm_x, mm_y):
    """Viewport position of a point given in page mm."""
    return page_view().mapFromScene(QtCore.QPointF(mm_x * 10.0, -mm_y * 10.0))


def mouse(kind, pos, button=QtCore.Qt.NoButton, buttons=QtCore.Qt.NoButton):
    vp = page_view().viewport()
    ev = QtGui.QMouseEvent(kind, QtCore.QPointF(pos), QtCore.QPointF(vp.mapToGlobal(pos)),
                           button, buttons, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)


def click(pos):
    mouse(QtCore.QEvent.MouseMove, pos)
    mouse(QtCore.QEvent.MouseButtonPress, pos, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton)
    mouse(QtCore.QEvent.MouseButtonRelease, pos, QtCore.Qt.LeftButton, QtCore.Qt.NoButton)


def selected():
    return [(s.ObjectName, list(s.SubElementNames))
            for s in FreeCADGui.Selection.getSelectionEx("", 0)]


def click_mm(x, y):
    FreeCADGui.Selection.clearSelection()
    click(at(x, y))
    QtWidgets.QApplication.processEvents()
    return selected()


@step(1000)
def make():
    import TechDrawGui  # the view providers
    GEN.SetBool("PageRendererVg", False)
    doc = FreeCAD.newDocument(DOC)
    left = doc.addObject("Part::Box", "Left")
    right = doc.addObject("Part::Box", "Right")
    for b in (left, right):
        b.Length = b.Width = b.Height = 20
    right.Placement.Base = FreeCAD.Vector(60, 0, 0)
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "View")
    page.addView(view)
    view.Source = [left, right]
    view.Direction = FreeCAD.Vector(0, 0, 1)
    view.XDirection = FreeCAD.Vector(1, 0, 0)
    view.ScaleType = "Custom"
    view.Scale = 1.0
    view.X = VIEW_X
    view.Y = VIEW_Y
    doc.recompute()
    FreeCADGui.getDocument(DOC).getObject("Page").show()


@step(5000)
def zoom():
    v = page_view()
    if not check("the page has its view", v is not None):
        raise RuntimeError("no page view")
    v.scale(3.0, 3.0)
    v.centerOn(QtCore.QPointF(VIEW_X * 10.0, -VIEW_Y * 10.0))


@step(1500)
def clicks():
    page_view().grab().save(os.path.join(OUT, "view.png"))
    # the two boxes are 40 mm wide each side of the gap's middle, 20 high:
    # x from -40 to 40 about the view's place, the gap from -20 to 20
    gap = click_mm(VIEW_X, VIEW_Y)
    check("a click in the gap between the boxes selects nothing", gap == [], gap)
    # the frame's top line, over the gap, a hair above the geometry
    frame = []
    for dy in (10.2, 10.0, 10.5, 11.0):
        frame = click_mm(VIEW_X, VIEW_Y + dy)
        if frame:
            note("INFO the frame's line picked %.1f mm above the view's middle" % dy)
            break
    check("a click on the frame's top line selects the view",
          [n for n, _ in frame] == ["View"] and frame[0][1] in ([], [""]), frame)
    label = []
    for dy in (13.0, 12.0, 14.0, 15.0):
        label = click_mm(VIEW_X, VIEW_Y - dy)
        if label:
            note("INFO the label picked %.1f mm below the view's middle" % dy)
            break
    check("a click on the view's label selects the view",
          [n for n, _ in label] == ["View"], label)
    edge = []
    for dx in (0.0, 0.1, -0.1, 0.2):
        edge = click_mm(VIEW_X + 30.0 + dx, VIEW_Y + 10.0)
        if edge and edge[0][1] and edge[0][1][0].startswith("Edge"):
            break
    check("a click on an edge selects that edge",
          len(edge) == 1 and edge[0][0] == "View" and edge[0][1]
          and edge[0][1][0].startswith("Edge"), edge)


@step(500)
def dimension():
    """A distance from the left box's top left corner to the right box's
    bottom right one: its rectangle covers the whole view"""
    doc = FreeCAD.getDocument(DOC)
    view = doc.getObject("View")

    def nearest(x, y):
        best, name = None, None
        i = 0
        while True:
            try:
                p = view.getVertexByIndex(i).Point
            except Exception:
                break
            d = (p.x - x) ** 2 + (p.y - y) ** 2
            if best is None or d < best:
                best, name = d, "Vertex%d" % i
            i += 1
        return name

    a, b = nearest(-40.0, 10.0), nearest(40.0, -10.0)
    note("INFO the dimension between %s and %s" % (a, b))
    dim = doc.addObject("TechDraw::DrawViewDimension", "Diagonal")
    dim.Type = "Distance"
    dim.References2D = [(view, a), (view, b)]
    doc.getObject("Page").addView(dim)
    doc.recompute()


@step(3000)
def under_dimension():
    page_view().grab().save(os.path.join(OUT, "dimension.png"))
    # the left box's top edge, inside the dimension's rectangle, away from
    # its lines
    edge = []
    for dx in (0.0, 0.1, -0.1, 0.2):
        edge = click_mm(VIEW_X - 30.0 + dx, VIEW_Y + 10.0)
        if edge and edge[0][1] and edge[0][1][0].startswith("Edge"):
            break
    check("entry 148: under a dimension's rectangle a click on an edge selects the edge",
          len(edge) == 1 and edge[0][0] == "View" and edge[0][1]
          and edge[0][1][0].startswith("Edge"), edge)


def finish():
    GEN.SetBool("PageRendererVg", HAD)
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
