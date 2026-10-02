"""An edge or a vertex lying in a face is picked within the pick radius,
from either side and at any pose.

The single pick took an edge over a face only where the edge was the
NEARER hit along the view (or at the very same point). An edge lies in
the faces it bounds, so its nearest point to the pick ray is nearer or
farther than where the ray meets the face by the slope of the face and
nothing else: square to a face the two tie and the face won, on a slanted
face the edge won from one side only. What lies in the plane of the face
hit first is now not behind it.

A box. The pointer is put a few pixels (inside the pick radius) to
either side of an edge shared by two visible faces, and a few pixels
inside a face from its corner. Claims, square on and at two slanted
poses, by both ways the single pick is decided:
  - beside the edge, on either face, the pick is the edge;
  - beside the corner the pick is the vertex;
  - well inside a face the pick is the face.

Scored against the tree before the change: with hidden-line selection on
top (the default) all of it held already, for an object's own edges; with
it off, square on the face won beside the edge, and beside the corner the
face or an edge won at every pose (4 of 12 failed).
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PickEdgeInFace"
V = FreeCAD.Vector
OFF = 3  # pixels from the edge; the pick radius is 5

POSES = [
    ("top", FreeCAD.Rotation()),
    ("iso", FreeCAD.Rotation(V(0, 0, 1), 35).multiply(FreeCAD.Rotation(V(1, 0, 0), 55))),
    ("low", FreeCAD.Rotation(V(0, 0, 1), -20).multiply(FreeCAD.Rotation(V(1, 0, 0), 75))),
]


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=20):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def picked(view, pos):
    res = view.getViewer().getPickedList(pos=(float(pos[0]), float(pos[1])),
                                         singlePick=True, mapCoords=False,
                                         resolve=0)
    return [(o.Name, s) for o, s in res]


def element(box, kind, *pts):
    """The name of the box's edge between two points, or vertex at one."""
    if kind == "Vertex":
        for i, v in enumerate(box.Shape.Vertexes):
            if v.Point.isEqual(pts[0], 1e-6):
                return "Vertex%d" % (i + 1)
    else:
        for i, e in enumerate(box.Shape.Edges):
            ends = [v.Point for v in e.Vertexes]
            if (ends[0].isEqual(pts[0], 1e-6) and ends[1].isEqual(pts[1], 1e-6)) \
                    or (ends[0].isEqual(pts[1], 1e-6) and ends[1].isEqual(pts[0], 1e-6)):
                return "Edge%d" % (i + 1)
    raise RuntimeError("no %s at %s" % (kind, pts))


def measure(view, box, tag, rot):
    cam = view.getCameraNode()
    cam.orientation.setValue(*rot.Q)
    settle()
    view.fitAll()
    settle(40)
    FreeCADGui.updateGui()
    settle()

    # The top face's front edge (y = 0, z = 10), shared with the front
    # face, which the slanted poses see as well.
    edge = element(box, "Edge", V(0, 0, 10), V(10, 0, 10))
    mid = view.getPointOnViewport(V(5, 0, 10))
    top_in = view.getPointOnViewport(V(5, 5, 10))
    # screen direction from the edge into the top face
    dx, dy = top_in[0] - mid[0], top_in[1] - mid[1]
    n = max((dx * dx + dy * dy) ** 0.5, 1e-9)
    ux, uy = dx / n, dy / n
    inside = (mid[0] + OFF * ux, mid[1] + OFF * uy)
    other = (mid[0] - OFF * ux, mid[1] - OFF * uy)
    a = picked(view, inside)
    b = picked(view, other)
    note("%s edge %s at %s: top side %s, other side %s" % (tag, edge, tuple(mid), a, b))
    check("%s: beside the edge on the top face, the edge" % tag,
          a == [("Box", edge)], a)
    # square on, the other side is off the box, where the edge is the
    # only hit and always was picked
    check("%s: beside the edge on the other side, the edge" % tag,
          b == [("Box", edge)], b)

    vertex = element(box, "Vertex", V(10, 10, 10))
    corner = view.getPointOnViewport(V(10, 10, 10))
    dx, dy = top_in[0] - corner[0], top_in[1] - corner[1]
    n = max((dx * dx + dy * dy) ** 0.5, 1e-9)
    near = (corner[0] + OFF * dx / n, corner[1] + OFF * dy / n)
    c = picked(view, near)
    note("%s vertex %s at %s: %s" % (tag, vertex, tuple(corner), c))
    check("%s: beside the corner, the vertex" % tag, c == [("Box", vertex)], c)

    d = picked(view, top_in)
    check("%s: well inside the face, the face" % tag, d == [("Box", "Face6")], d)


def run():
    try:
        param = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        param.SetBool("ShowNaviCube", False)
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        FreeCADGui.Selection.clearSelection()
        mw = FreeCADGui.getMainWindow()
        mw.showNormal()
        mw.resize(1000, 800)
        settle()
        view = FreeCADGui.getDocument(DOC).activeView()
        for on in (True, False):
            param.SetBool("HiddenLineSelectionOnTop", on)
            settle()
            for tag, rot in POSES:
                measure(view, box, tag + ("" if on else "-kept"), rot)
        param.RemBool("HiddenLineSelectionOnTop")
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
