"""A sketch's face lying on a solid's face is the one drawn and the one
picked (upstream a2468774d3, the fork's way).

A sketch with internal faces on, attached to the top face of a box: the
two faces are coplanar, so depth alone says nothing about which is in
front. The frame used to show the sketch's face only where drawing order
happened to favour it -- square to the camera it showed the BOX's face
instead -- and a click on it selected the box's face at any pose: the
single pick keeps the first hit at a given distance.

Now the sketch's internal-face view is an overlay on coplanar geometry,
by rule: it is drawn with a polygon offset in front of an ordinary face,
and a pick that ties with another face at one depth goes to the overlay.

Claims, from the top, from an isometric pose and from a grazing one (a
polygon offset has a slope half, an axis-aligned view cannot see it):
  - inside the sketch's outline the frame shows the sketch's colour;
  - a single pick there is the sketch's internal face;
  - the pick list there holds the sketch's face and the box's;
  - outside the outline, on the same box face, frame and pick are the
    box's.

And on the sketch's outline the pick is the sketch's edge: an edge lying
in a face is not behind it (see pick-edge-in-face.py), whichever object
the face belongs to. The points inside are kept clear of the outline by
more than the pick radius at every pose for that reason.

Scored against the tree before the change: the single pick inside the
outline was the box's Face6 at every pose, and the top pose's frame
showed the box's colour there; on the outline the pick was the box's face
or the sketch's by the pose.
"""
import colorsys
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchFaceOnSolid"
V = FreeCAD.Vector

# World points on the box's top face (z = 10); the sketch is the square
# 3..7, so its plane coordinates are the world's.
INSIDE = [V(5, 5, 10), V(4.6, 5.4, 10), V(5.4, 4.6, 10)]
OUTSIDE = [V(1.5, 1.5, 10), V(8.5, 5, 10)]
ON_EDGE = V(5, 3, 10)

# (name, camera rotation as axis + degrees applied in order)
POSES = [
    ("top", FreeCAD.Rotation()),
    ("iso", FreeCAD.Rotation(V(1, 0, 0), 55) .multiply(FreeCAD.Rotation())),
    ("grazing", FreeCAD.Rotation(V(0, 0, 1), 30)
        .multiply(FreeCAD.Rotation(V(1, 0, 0), 80))),
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


def hue_name(img, x, y):
    """'sketch' (red), 'box' (green) or the raw colour, at a viewport
    position with a bottom-left origin."""
    c = QtGui.QColor(img.pixel(int(x), img.height() - 1 - int(y)))
    h, s, v = colorsys.rgb_to_hsv(c.redF(), c.greenF(), c.blueF())
    deg = h * 360.0
    if s > 0.4 and v > 0.15:
        if min(deg, 360 - deg) < 25:
            return "sketch"
        if abs(deg - 120) < 25:
            return "box"
    return "(%d,%d,%d)" % (c.red(), c.green(), c.blue())


def picked(view, pos, single):
    res = view.getViewer().getPickedList(pos=(float(pos[0]), float(pos[1])),
                                         singlePick=single, mapCoords=False,
                                         resolve=0)
    return [(o.Name, s) for o, s in res]


def is_sketch_face(hit):
    return hit[0] == "Sketch" and "Face" in hit[1]


def measure(view, tag, rot):
    cam = view.getCameraNode()
    cam.orientation.setValue(*rot.Q)
    settle()
    view.fitAll()
    settle(40)
    FreeCADGui.updateGui()
    settle()
    path = os.path.join(OUT, "%s.png" % tag)
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    check("%s: the dump is this view's frame" % tag,
          (img.width(), img.height()) == tuple(view.getSize()),
          (img.width(), img.height()))

    for i, pt in enumerate(INSIDE):
        p = view.getPointOnViewport(pt)
        one = picked(view, p, True)
        many = picked(view, p, False)
        note("%s inside%d at %s: frame=%s single=%s list=%s" % (
            tag, i, tuple(p), hue_name(img, *p), one, many))
        check("%s inside%d: the frame shows the sketch's face" % (tag, i),
              hue_name(img, *p) == "sketch", hue_name(img, *p))
        check("%s inside%d: a single pick is the sketch's face" % (tag, i),
              len(one) == 1 and is_sketch_face(one[0]), one)
        faces = [h for h in many if "Face" in h[1]]
        check("%s inside%d: the pick list has both faces" % (tag, i),
              any(is_sketch_face(h) for h in faces)
              and ("Box", "Face6") in faces, many)

    for i, pt in enumerate(OUTSIDE):
        p = view.getPointOnViewport(pt)
        one = picked(view, p, True)
        note("%s outside%d at %s: frame=%s single=%s" % (
            tag, i, tuple(p), hue_name(img, *p), one))
        check("%s outside%d: the frame shows the box's face" % (tag, i),
              hue_name(img, *p) == "box", hue_name(img, *p))
        check("%s outside%d: a single pick is the box's face" % (tag, i),
              one == [("Box", "Face6")], one)

    p = view.getPointOnViewport(ON_EDGE)
    one = picked(view, p, True)
    check("%s: on the outline the pick is the sketch's edge" % tag,
          len(one) == 1 and one[0][0] == "Sketch" and one[0][1].startswith("Edge"),
          one)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.AttachmentSupport = [(box, "Face6")]
        sk.MapMode = "FlatFace"
        pts = [V(3, 3, 0), V(7, 3, 0), V(7, 7, 0), V(3, 7, 0)]
        for i in range(4):
            sk.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]))
        for i in range(4):
            sk.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
        sk.MakeInternals = True
        doc.recompute()
        note("sketch placement %s, internal faces %d" % (
            sk.Placement, len(sk.InternalShape.Faces)))
        box.ViewObject.ShapeColor = (0.0, 1.0, 0.0)
        if hasattr(sk.ViewObject, "AutoColor"):
            sk.ViewObject.AutoColor = False
        sk.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        sk.ViewObject.Transparency = 0
        FreeCADGui.Selection.clearSelection()

        mw = FreeCADGui.getMainWindow()
        mw.showNormal()
        mw.resize(1000, 800)
        settle()
        view = FreeCADGui.getDocument(DOC).activeView()
        check("the internal face exists", len(sk.InternalShape.Faces) == 1)
        # The single pick is decided in two places. With hidden-line
        # selection on top (the default) the pass over the on-top objects
        # leaves the action gathering every hit, and the gathered list is
        # cut down afterwards; with it off the action keeps one hit as it
        # goes. Both must give the tie to the overlay.
        param = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        for on in (True, False):
            param.SetBool("HiddenLineSelectionOnTop", on)
            settle()
            for tag, rot in POSES:
                measure(view, tag + ("" if on else "-kept"), rot)
        param.RemBool("HiddenLineSelectionOnTop")
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finish()


def finish():
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
