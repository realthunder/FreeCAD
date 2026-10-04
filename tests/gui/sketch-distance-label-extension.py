"""A distance to a line reaches the line (upstream cb5a28acd7).

A point-to-line distance is measured to the line's infinite support. When
the foot of the perpendicular falls past an end of the segment, the
dimension's witness line started at that foot, in mid-air, with a gap
between it and the segment it measures to. The dimension now carries a
helper line from the nearer end of the segment to the foot
(SoDatumLabel::extensionLines), and none when the foot is on the segment.
A circle-to-line distance gets the same.

Upstream sets the field in EditModeConstraintCoinManager, which this fork
does not compile; here the sketch's own draw sets it, and the label draws
it in both of its paths -- Coin's GLRender, and the leader primitives the
render-cache capture takes for the bgfx renderer.

Claims, a segment from (20, 10) to (20, 30):
  - a point at (5, 45): the field is (20, 30) -> (20, 45), and the frame
    shows the dimension's colour half way along it;
  - the same with the segment drawn the other way round: the nearer end,
    drawn;
  - a point at (5, 2): (20, 10) -> (20, 2);
  - a point at (5, 20), foot on the segment: no helper line, and nothing
    of the dimension's colour past the segment's end;
  - a foot on an end to within round-off: none;
  - a circle at (5, 60): (20, 30) -> (20, 60), drawn.

The frame is sampled only where nothing else of the dimension is: its own
witness line runs from the foot along the segment's direction, so a foot
below the segment, or on its end, has that line over the place a helper
would be.

GT_RENDER_CACHE sets the render cache mode for the run (default: the
view's own, 3, the bgfx renderer); 0 is Coin alone. Under 0 the field
checks pass and the drawn ones cannot: a sketch dimension is not drawn at
all there at present -- its GLRender is reached by the shadow-map passes
only -- which is not this change's doing (sketch-line-angle-labels.py
fails under 0 the same way).

Scored against the tree before: mode 3, every helper-line check failed
(no such field) and both of the "nothing drawn" checks passed.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore, QtGui, QtWidgets
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DistanceLabelExtension"
V = FreeCAD.Vector
A = V(20, 10, 0)
B = V(20, 30, 0)
state = {"done": False, "n": 0}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def is_label(c):
    # a driving dimension's red
    return c.red() > c.green() + 50 and c.red() > c.blue() + 40


def drawn_at(view, img, pt, reach=3):
    """Whether the dimension's colour is within `reach` pixels of a point."""
    x, y = view.getPointOnViewport(pt)
    dpr = view.graphicsView().viewport().devicePixelRatioF()
    px = int(round(x))
    py = img.height() - 1 - int(round(y))
    reach = int(round(reach * dpr))
    for yy in range(max(0, py - reach), min(img.height(), py + reach + 1)):
        for xx in range(max(0, px - reach), min(img.width(), px + reach + 1)):
            if is_label(QtGui.QColor(img.pixel(xx, yy))):
                return True
    return False


def helper_lines(view):
    """extensionLines of every datum label in the view, as (x, y) pairs."""
    out = []
    for root in (view.getViewer().getSoRenderManager().getSceneGraph(),):
        search = coin.SoSearchAction()
        search.setType(coin.SoType.fromName("SoDatumLabel"))
        search.setInterest(coin.SoSearchAction.ALL)
        search.setSearchingAll(True)
        search.apply(root)
        paths = search.getPaths()
        for i in range(paths.getLength()):
            field = paths[i].getTail().getField("extensionLines")
            if field is None:
                return None
            out.extend((round(v.getValue()[0], 6), round(v.getValue()[1], 6))
                       for v in field.getValues())
    return out


def one(doc, name, build, want, sample):
    """A sketch made by `build`, in edit: the helper line is `want`, and the
    frame has the dimension's colour at `sample` exactly when there is one."""
    state["n"] += 1
    sk = doc.addObject("Sketcher::SketchObject", "Sketch%d" % state["n"])
    build(sk)
    doc.recompute()
    gdoc = FreeCADGui.getDocument(DOC)
    gdoc.setEdit(sk)
    settle(0.8)
    view = gdoc.activeView()
    cam = view.getCameraNode()
    cam.position.setValue(20.0, 25.0, cam.position.getValue()[2])
    cam.height.setValue(90.0)
    view.redraw()
    settle(1.0)
    try:
        view.waitFrameComplete()
    except Exception:
        pass
    settle(0.3)
    got = helper_lines(view)
    check("%s: the helper line is %s" % (name, want or "none"), got == want, got)
    img = view.graphicsView().viewport().grab().toImage()
    img.save(os.path.join(OUT, "%02d.png" % state["n"]))
    shows = sample is not None and drawn_at(view, img, sample)
    if sample is None:
        pass
    elif want:
        check("%s: and it is drawn" % name, shows, "at %s" % (tuple(sample)[:2],))
    else:
        check("%s: and nothing is drawn past the segment" % name, not shows,
              "at %s" % (tuple(sample)[:2],))
    gdoc.resetEdit()
    settle(0.4)
    sk.Visibility = False


def point_to_line(start, end, point):
    def build(sk):
        p = sk.addGeometry(Part.LineSegment(point, point + V(-3, 0, 0)), False)
        line = sk.addGeometry(Part.LineSegment(start, end), False)
        sk.addConstraint(Sketcher.Constraint("Distance", p, 1, line, 15.0))
    return build


def circle_to_line(sk):
    c = sk.addGeometry(Part.Circle(V(5, 60, 0), V(0, 0, 1), 2), False)
    line = sk.addGeometry(Part.LineSegment(A, B), False)
    sk.addConstraint(Sketcher.Constraint("Distance", c, line, 13.0))


def run():
    try:
        mode = os.environ.get("GT_RENDER_CACHE")
        view_params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        if mode is not None:
            view_params.SetInt("RenderCache", int(mode))
        view_params.SetBool("ShowNaviCube", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/General").SetBool(
            "AdjustCamera", False)
        note("render cache mode %s" % view_params.GetInt("RenderCache", 3))
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        view = FreeCADGui.getDocument(DOC).activeView()
        view.viewTop()
        view.setCameraType("Orthographic")
        settle(0.5)
        past_end = V(20, 37.5, 0)
        one(doc, "past the end", point_to_line(A, B, V(5, 45, 0)),
            [(20.0, 30.0), (20.0, 45.0)], past_end)
        one(doc, "before the start", point_to_line(A, B, V(5, 2, 0)),
            [(20.0, 10.0), (20.0, 2.0)], None)
        one(doc, "the segment reversed", point_to_line(B, A, V(5, 45, 0)),
            [(20.0, 30.0), (20.0, 45.0)], past_end)
        one(doc, "the foot on the segment", point_to_line(A, B, V(5, 20, 0)),
            [], past_end)
        one(doc, "the foot on an end, to round-off", point_to_line(A, B, V(5, 30 + 1e-14, 0)),
            [], None)
        one(doc, "a circle", circle_to_line, [(20.0, 30.0), (20.0, 60.0)], past_end)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
