"""A selected object shown on top dims the edges its own faces hide.

docs/HandsOnQueue.md entry 54: "preselection show on top highlight will
renders edge that respect its depth regarding to the faces. so edge behind
the face got dimmed. but full selection highlight does not do the same. the
edges are draw as if no depth test".

A highlight shown on top draws an object's lines twice: once with no depth
test, dimmed to View/TransparencyOnTop, and once more, solid, where the
depth test against the object's own faces passes. For the object under the
pointer that gave a dimmed hidden edge. For a fully selected object the
first pass was not dimmed -- every edge at full colour, front or behind --
and the old Coin renderer did not even lay down the selected object's depth.
Both renderers dim it now. An edge selected BY ITSELF is still drawn at full
colour wherever it lies: that is what shows a hidden edge that was picked.

Measured on a box seen from the isometric side, where three of the twelve
edges meet in the corner furthest from the eye and are hidden. For each of
them, at a point of the edge that no other edge comes near in the picture:
how much of the highlight colour the pixels on the edge hold, between the
face they are seen through (0) and a visible edge of the same picture (1).

Claims, for the render engine (render type "Default") and for the old Coin
rendering ("Legacy") alike:
  - under the pointer: every hidden edge is there and dimmed, between 0.2
    and 0.8;
  - selected: the same -- 1.0 before the change;
  - one hidden edge selected by itself: at full colour, over 0.85.
"""
import math
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtOpenGLWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
DOC = "SelectionOnTop"
STEPS = []
CLEAR = 12      # pixels a sample point keeps from every other edge
STATE = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def rgb(value):
    return ((value >> 24) & 255, (value >> 16) & 255, (value >> 8) & 255)


def rgb_at(image, x, y):
    x = min(max(int(round(x)), 0), image.width() - 1)
    y = min(max(int(round(y)), 0), image.height() - 1)
    p = image.pixel(x, y)
    return ((p >> 16) & 255, (p >> 8) & 255, p & 255)


def view3d():
    return FreeCADGui.ActiveDocument.mdiViewsOfType("Gui::View3DInventor")[0]


def gl_widget():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QMainWindow):
        if w.metaObject().className() == "Gui::View3DInventor" and w.isVisible():
            for gl in w.findChildren(QtOpenGLWidgets.QOpenGLWidget):
                if gl.isVisible():
                    return gl
    return None


def backend():
    try:
        return bool(view3d().getRenderStats())
    except Exception:
        return False


def picture(tag):
    """What the view shows: the engine's own capture, or, where Coin draws,
    the view's surface."""
    view = view3d()
    path = os.path.join(OUT, tag + ".png")
    if backend():
        view.saveRenderDump(path)
        return QtGui.QImage(path)
    image = gl_widget().grabFramebuffer()
    image.save(path)
    return image


class Colour:
    """How far a pixel leans to a highlight colour: its strong channels
    against its weak ones."""

    def __init__(self, colour):
        self.strong = [c for c in range(3) if colour[c] >= 128]
        self.weak = [c for c in range(3) if colour[c] < 128] or [0]

    def score(self, c):
        return min(c[k] for k in self.strong) - max(c[k] for k in self.weak)


def distance(p, a, b):
    ax, ay = a
    bx, by = b
    px, py = p
    dx, dy = bx - ax, by - ay
    length = dx * dx + dy * dy
    t = 0.0 if length == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def make():
    VIEW.SetBool("ShowNaviCube", False)
    VIEW.SetBool("CornerCoordSystem", False)
    doc = FreeCAD.newDocument(DOC)
    box = doc.addObject("Part::Box", "Box")
    # not a cube: seen from the isometric side a cube's far corner lies behind
    # its near one and the hidden edges behind visible ones
    box.Length = 20
    box.Width = 12
    box.Height = 6
    doc.recompute()


def setup(kind):
    def fn():
        RENDER.SetString("Type", kind)
    fn.__name__ = "setup_" + kind
    return fn


def refit(kind):
    def fn():
        view = view3d()
        view.viewIsometric()
        view.fitAll()
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.clearPreselection()
    fn.__name__ = "refit_" + kind
    return fn


def plain(kind):
    def fn():
        want = kind == "Default"
        check("%s: the view is drawn by %s" % (kind, "the render engine" if want else "Coin"),
              backend() == want)
        view = view3d()
        image = picture(kind + "-plain")
        gl = gl_widget()
        sx = image.width() / float(gl.width())
        sy = image.height() / float(gl.height())
        box = FreeCAD.ActiveDocument.getObject("Box")
        direction = view.getViewDirection()
        far = max((v.Point for v in box.Shape.Vertexes), key=lambda p: p.dot(direction))

        def project(p):
            x, y = view.getPointOnViewport(p)
            return (x * sx, image.height() - 1 - y * sy)

        ends = []
        for e in box.Shape.Edges:
            a, b = e.Vertexes[0].Point, e.Vertexes[1].Point
            ends.append((a, b, (a - far).Length < 1e-6 or (b - far).Length < 1e-6))
        segments = [(project(a), project(b)) for a, b, _hidden in ends]
        edges = []
        for i, (a, b, hidden) in enumerate(ends):
            best = None
            for t in (0.5, 0.4, 0.6, 0.3, 0.7, 0.25, 0.75):
                p = project(a + (b - a) * t)
                room = min(distance(p, s[0], s[1]) for j, s in enumerate(segments) if j != i)
                if best is None or room > best[0]:
                    best = (room, p)
            (ax, ay), (bx, by) = segments[i]
            length = math.hypot(bx - ax, by - ay) or 1.0
            edges.append({"name": "Edge%d" % (i + 1), "hidden": hidden, "room": best[0],
                          "at": best[1], "along": ((bx - ax) / length, (by - ay) / length)})
        STATE[kind] = {"edges": edges}
        note("NOTE %s: picture %d x %d for a view of %d x %d; hidden %s" % (
            kind, image.width(), image.height(), gl.width(), gl.height(),
            ", ".join("%s (%.0f px clear)" % (e["name"], e["room"]) for e in edges if e["hidden"])))
    fn.__name__ = "plain_" + kind
    return fn


def act(kind, what):
    def fn():
        box = FreeCAD.ActiveDocument.getObject("Box")
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.clearPreselection()
        if what == "pointer":
            FreeCADGui.Selection.setPreselection(box)
        elif what == "selected":
            FreeCADGui.Selection.addSelection(box)
        else:
            hidden = [e for e in STATE[kind]["edges"] if e["hidden"] and e["room"] >= CLEAR]
            if hidden:
                STATE[kind]["alone"] = hidden[0]["name"]
                FreeCADGui.Selection.addSelection(box, hidden[0]["name"])
        view3d().redraw()
    fn.__name__ = "%s_%s" % (what, kind)
    return fn


def lines(image, colour, edge):
    """The strongest pixel across the edge at its sample point, and the face
    it is seen against: the weaker of the two sides, a little way off."""
    x, y = edge["at"]
    ux, uy = edge["along"]
    nx, ny = -uy, ux
    on = max(colour.score(rgb_at(image, x + a * nx + b * ux, y + a * ny + b * uy))
             for a in range(-3, 4) for b in (-1, 0, 1))
    sides = [colour.score(rgb_at(image, x + s * 8 * nx, y + s * 8 * ny)) for s in (-1, 1)]
    return on, sides


def look(kind, what, colour):
    def fn():
        image = picture("%s-%s" % (kind, what))
        state = STATE[kind]
        if what == "alone":
            name = state.get("alone")
            if not check("%s: a hidden edge with room around it to select by itself" % kind,
                         bool(name)):
                return
            edge = [e for e in state["edges"] if e["name"] == name][0]
            on, sides = lines(image, colour, edge)
            face = min(sides)
            full = state["full"]
            strength = (on - face) / float(full - face) if full > face else -1.0
            check("%s: %s selected by itself, behind a face, is drawn at full colour" % (kind, name),
                  strength > 0.85, "%.2f of a visible selected edge" % strength)
            return
        visible = [lines(image, colour, e)[0] for e in state["edges"]
                   if not e["hidden"] and e["room"] >= CLEAR]
        full = max(visible) if visible else 0
        if what == "selected":
            state["full"] = full
        if not check("%s, %s: the visible edges are drawn in the highlight colour" % (kind, what),
                     full > 100 and min(visible) > 0.8 * full,
                     "%d edges, the colour's lean %d to %d" % (
                         len(visible), min(visible) if visible else 0, full)):
            return
        found = []
        for e in state["edges"]:
            if not e["hidden"] or e["room"] < CLEAR:
                continue
            on, sides = lines(image, colour, e)
            face = sum(sides) / 2.0
            found.append((e["name"], (on - face) / float(full - face) if full > face else -1.0))
        text = ", ".join("%s %.2f" % f for f in found)
        if not check("%s, %s: at least two hidden edges can be measured" % (kind, what),
                     len(found) >= 2, text):
            return
        check("%s, %s: every hidden edge is there and dimmed" % (kind, what),
              all(0.2 < s < 0.8 for _name, s in found),
              "of a visible edge's colour over the face in front: " + text)
    fn.__name__ = "look_%s_%s" % (kind, what)
    return fn


def finish():
    RENDER.RemString("Type")
    try:
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.clearPreselection()
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
            note("FAIL the test ran | in %s: %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(int(delay), run)


HOVER = Colour(rgb(VIEW.GetUnsigned("HighlightColor", 0xE1E114FF)))
PICKED = Colour(rgb(VIEW.GetUnsigned("SelectionColor", 0x1CAD1CFF)))

STEPS.append((2000, make))
for _kind in ("Default", "Legacy"):
    STEPS.append((1500, setup(_kind)))
    STEPS.append((3500, refit(_kind)))
    STEPS.append((2500, plain(_kind)))
    STEPS.append((300, act(_kind, "pointer")))
    STEPS.append((1500, look(_kind, "pointer", HOVER)))
    STEPS.append((300, act(_kind, "selected")))
    STEPS.append((1500, look(_kind, "selected", PICKED)))
    STEPS.append((300, act(_kind, "alone")))
    STEPS.append((1500, look(_kind, "alone", PICKED)))
advance()
