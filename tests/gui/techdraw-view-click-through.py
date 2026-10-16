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
    anywhere in its rectangle;
  - entry 148, the last 10 of Front010's (the lines and arrows took the
    press and selected nothing, on a projection group's view): the
    reporter, 2026-10-16, "Dimension line will select dimension" -- a click
    on an extension line lying over an edge selects the dimension, on a
    part view and on a projection group's view; so does one on the
    dimension line away from the label, and one on the label.
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


def extensions(view, prefix):
    """Two horizontal distances between the view's outer corners on one side,
    one labelled 25 mm above the view and one 25 mm below: whichever side the
    corners are on, one of the two runs its extension lines along the whole
    outer sides. Returns the corners' x on the view, and the y middle."""
    doc = FreeCAD.getDocument(DOC)
    points = []
    i = 0
    while True:
        try:
            p = view.getVertexByIndex(i).Point
        except Exception:
            break
        points.append((p.x, p.y, "Vertex%d" % i))
        i += 1
    xmin = min(p[0] for p in points)
    xmax = max(p[0] for p in points)
    ymin = min(p[1] for p in points)
    ymax = max(p[1] for p in points)
    a = [n for x, y, n in points if abs(x - xmin) < 0.01 and abs(y - ymin) < 0.01][0]
    b = [n for x, y, n in points if abs(x - xmax) < 0.01 and abs(y - ymin) < 0.01][0]
    note("INFO %s: corners x %.2f to %.2f, y %.2f to %.2f; dimensions between %s and %s"
         % (view.Name, xmin, xmax, ymin, ymax, a, b))
    for name, dy in (("Up", 25.0), ("Down", -25.0)):
        dim = doc.addObject("TechDraw::DrawViewDimension", prefix + name)
        dim.Type = "DistanceX"
        dim.References2D = [(view, a), (view, b)]
        doc.getObject("Page").addView(dim)
        dim.X = 0.0
        dim.Y = dy
    doc.recompute()
    return xmin, (ymin + ymax) / 2.0


EXT = {}


@step(500)
def extension():
    doc = FreeCAD.getDocument(DOC)
    doc.removeObject("Diagonal")
    EXT["view"] = extensions(doc.getObject("View"), "Extension")


@step(3000)
def under_extension():
    page_view().grab().save(os.path.join(OUT, "extension.png"))
    # the left box's left side at its middle: an extension line lies on it
    x, y = EXT["view"]
    line = click_mm(VIEW_X + x, VIEW_Y - y)
    check("entry 148: a click on a dimension's extension line over an edge selects the dimension",
          len(line) == 1 and line[0][0].startswith("Extension") and line[0][1] in ([], [""]),
          line)
    # the dimension line under its label (set 25 mm above the view), 30 mm
    # to the left: looked for from 14 to 26 mm up, empty page in between
    line = []
    for dy in [14.0 + 0.25 * k for k in range(49)]:
        line = click_mm(VIEW_X - 30.0, VIEW_Y + dy)
        if line:
            note("INFO the dimension line picked %.1f mm above the view's middle" % dy)
            break
    check("entry 148: a click on the dimension line away from its label selects the dimension",
          [n for n, _ in line] == ["ExtensionUp"], line)
    label = []
    for dy in (25.0, 24.5, 25.5, 26.0):
        label = click_mm(VIEW_X, VIEW_Y + dy)
        if label:
            break
    check("entry 148: a click on the dimension's label selects the dimension",
          [n for n, _ in label] == ["ExtensionUp"], label)


GROUP_X, GROUP_Y = VIEW_X, VIEW_Y - 60.0


@step(500)
def group():
    """The same on a projection group's Front view, as on scanner.FCStd's
    Page009, whose dimensions hang off such a view"""
    doc = FreeCAD.getDocument(DOC)
    group = doc.addObject("TechDraw::DrawProjGroup", "Group")
    doc.getObject("Page").addView(group)
    group.Source = [doc.getObject("Left")]
    group.ScaleType = "Custom"
    group.Scale = 1.0
    group.addProjection("Front")
    group.X = GROUP_X
    group.Y = GROUP_Y
    doc.recompute()


@step(3000)
def group_dimensions():
    # the view's lines come from a thread: none right after the recompute
    front = FreeCAD.getDocument(DOC).getObject("Group").Views[0]
    EXT["group"] = (front.Name, front.X.Value, front.Y.Value) + extensions(front, "Group")


@step(3000)
def under_group_extension():
    v = page_view()
    v.centerOn(QtCore.QPointF(GROUP_X * 10.0, -GROUP_Y * 10.0))
    QtWidgets.QApplication.processEvents()
    v.grab().save(os.path.join(OUT, "group.png"))
    name, fx, fy, x, y = EXT["group"]
    line = click_mm(GROUP_X + fx + x, GROUP_Y + fy - y)
    check("entry 148: on a projection group's view, a click on a dimension's"
          " extension line over an edge selects the dimension",
          len(line) == 1 and line[0][0].startswith("Group") and line[0][0] != "Group"
          and line[0][1] in ([], [""]), line)


@step(500)
def drag_group():
    """The group filters its children's presses to drag itself by its views:
    that stays for its own views. The Front view's frame line, dragged 10 mm
    to the right, moves the group by that."""
    doc = FreeCAD.getDocument(DOC)
    group = doc.getObject("Group")
    name, fx, fy, x, y = EXT["group"]
    frame = None
    for dy in [8.0 + 0.25 * k for k in range(25)]:
        got = click_mm(GROUP_X + fx, GROUP_Y + fy + dy)
        if got and got[0][0] == name and got[0][1] in ([], [""]):
            frame = dy
            break
    note("INFO the Front's frame line %s mm above its middle" % frame)
    if frame is None:
        check("the group's Front view has a frame line to drag", False)
        return
    x0 = group.X.Value
    start = at(GROUP_X + fx, GROUP_Y + fy + frame)
    mouse(QtCore.QEvent.MouseMove, start)
    mouse(QtCore.QEvent.MouseButtonPress, start, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton)
    for k in range(1, 11):
        pos = at(GROUP_X + fx + k, GROUP_Y + fy + frame)
        mouse(QtCore.QEvent.MouseMove, pos, QtCore.Qt.NoButton, QtCore.Qt.LeftButton)
        QtWidgets.QApplication.processEvents()
    end = at(GROUP_X + fx + 10.0, GROUP_Y + fy + frame)
    mouse(QtCore.QEvent.MouseButtonRelease, end, QtCore.Qt.LeftButton, QtCore.Qt.NoButton)
    QtWidgets.QApplication.processEvents()
    check("a projection group's view dragged 10 mm moves the group about that",
          abs(group.X.Value - x0 - 10.0) < 1.0, (x0, group.X.Value))


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
