"""The number of a dimension picks over the whole of its text.

A datum label is picked by its text box alone (SoDatumLabel::
generatePrimitives), and two things cut holes in it:
- the distance, diameter and angle labels emitted the box as a QUAD with
  its corners in the order lower-left, upper-left, lower-right,
  upper-right -- a bowtie. Coin cuts a quad into the triangles (v0, v1, v2)
  and (v0, v2, v3), so the triangle from the two upper corners down to the
  centre was never covered: the pointer over the middle of the number, or
  above it, picked nothing.
- Coin picks a shape only where the ray meets its bounding box, whenever
  that box is cached for the camera (SoShape::rayPick). A diameter's box
  lay along its dimension line with no height at all, so with the cache
  in place only a band the pick radius wide picked. Whether the cache is
  in place depends on what traversed last, so the number picked whole in
  one run and clipped in the next.

Measured here, by moving the pointer over each label pixel by pixel with
the shapes' bounding boxes cached for the view's camera: the pixels that
preselect the constraint fill the rectangle they span, and that rectangle
is the size of the label's text. Before the fixes a quarter of it -- the
notch -- was missing (fill 0.75), and the diameter's was 10 pixels high
for a 17 pixel number. Checked for a distance above and below its line, a
diameter and an angle label in one view, and for a distance label again
in each of two tiled views of the same edit.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DatumLabelPick"
state = {"done": False}

# constraint -> (what, model rectangle (x0, y0, x1, y1) its number lies in,
# its SoDatumLabel's datumtype, its label distance)
LABELS = {
    "Constraint1": ("distance", (0, 4, 40, 18), "DISTANCE", 12.0),
    "Constraint2": ("diameter", (88, -12, 135, 12), "DIAMETER", 10.0),
    "Constraint3": ("angle", (-5, -45, 45, -2), "ANGLE", None),
    "Constraint4": ("distance below its line", (0, 18, 40, 28), "DISTANCE", -8.0),
}
MIN_FILL = 0.95
# pixels the hit rectangle may differ from the text's size by
SIZE_SLACK = 2


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def cache_bboxes(view):
    """Have every shape cache its bounding box for the view's camera, as the
    view's own bounding box queries leave it; a ray pick then culls each
    shape by that box."""
    rm = view.getViewer().getSoRenderManager()
    coin.SoGetBoundingBoxAction(rm.getViewportRegion()).apply(rm.getSceneGraph())


def hover_px(view, x, y):
    """A pointer move to viewport pixel (x, y), y up, in that view; returns
    the constraints it preselects."""
    cache_bboxes(view)
    gv = view.graphicsView()
    vp = gv.viewport()
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    ev = QtGui.QMouseEvent(QtCore.QEvent.MouseMove, pos, gv.mapToGlobal(pos.toPoint()),
                           QtCore.Qt.NoButton, QtCore.Qt.NoButton, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)
    pre = FreeCADGui.Selection.getPreselection()
    names = list(pre.SubElementNames) if pre.ObjectName == "Sketch" else []
    FreeCADGui.Selection.clearPreselection()
    return names


def label_fill(view, name, rect):
    """Find the label in its model rectangle on a coarse grid, then sweep the
    pixels around what was found; returns (fill, hits, box)."""
    x0, y0 = view.getPointOnViewport(FreeCAD.Vector(rect[0], rect[1], 0))
    x1, y1 = view.getPointOnViewport(FreeCAD.Vector(rect[2], rect[3], 0))
    w, h = view.getSize()
    x0, x1 = max(0, min(x0, x1)), min(w - 1, max(x0, x1))
    y0, y1 = max(0, min(y0, y1)), min(h - 1, max(y0, y1))
    coarse = [(x, y) for y in range(y0, y1 + 1, 3) for x in range(x0, x1 + 1, 3)
              if name in hover_px(view, x, y)]
    if not coarse:
        return 0.0, 0, None
    pad = 4
    bx0 = min(p[0] for p in coarse) - pad
    bx1 = max(p[0] for p in coarse) + pad
    by0 = min(p[1] for p in coarse) - pad
    by1 = max(p[1] for p in coarse) + pad
    hits = [(x, y) for y in range(by0, by1 + 1) for x in range(bx0, bx1 + 1)
            if name in hover_px(view, x, y)]
    hx0 = min(p[0] for p in hits)
    hx1 = max(p[0] for p in hits)
    hy0 = min(p[1] for p in hits)
    hy1 = max(p[1] for p in hits)
    area = (hx1 - hx0 + 1) * (hy1 - hy0 + 1)
    return len(hits) / float(area), len(hits), (hx0, hy0, hx1, hy1)


def text_sizes(view):
    """datumtype -> [(width, height)] in pixels of the labels' text, as
    SoDatumLabel::drawImage sizes its image from the font."""
    sa = coin.SoSearchAction()
    sa.setType(coin.SoType.fromName("SoDatumLabel"))
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.apply(view.getViewer().getSoRenderManager().getSceneGraph())
    sizes = {}

    def field(node, name):
        return node.getField(name).get().getString().strip().strip('"')

    for i in range(sa.getPaths().getLength()):
        node = sa.getPaths()[i].getTail()
        fm = QtGui.QFontMetrics(QtGui.QFont(field(node, "name"), int(float(field(node, "size")))))
        sizes.setdefault(field(node, "datumtype"), []).append(
            (fm.horizontalAdvance(field(node, "string")), fm.height()))
    return sizes


def check_label(view, name, tag=""):
    what, rect, dtype, _ = LABELS[name]
    fill, n, box = label_fill(view, name, rect)
    check("%sthe %s label picks over its whole box" % (tag, what), fill >= MIN_FILL,
          "fill %.3f, %d pixels in %s, view %s" % (fill, n, box, view.getSize()))
    if not box:
        return
    w, h = box[2] - box[0] + 1, box[3] - box[1] + 1
    sizes = state["sizes"].get(dtype, [])
    check("%sand over the whole of its text" % tag,
          any(abs(w - tw) <= SIZE_SLACK and abs(h - th) <= SIZE_SLACK for tw, th in sizes),
          "hit %dx%d, text %s" % (w, h, sizes))


def run():
    try:
        import Part
        import Sketcher
        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(40, 0, 0)), False)
        sk.addGeometry(Part.Circle(V(80, 0, 0), V(0, 0, 1), 15), False)
        sk.addGeometry(Part.LineSegment(V(0, -40, 0), V(30, -40, 0)), False)
        sk.addGeometry(Part.LineSegment(V(0, -40, 0), V(20, -20, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Distance", 0, 40.0))
        sk.setLabelDistance(0, 12.0)
        sk.addConstraint(Sketcher.Constraint("Diameter", 1, 30.0))
        sk.setLabelDistance(1, 10.0)
        sk.addConstraint(Sketcher.Constraint("Angle", 2, 3, 0.785398))
        sk.addGeometry(Part.LineSegment(V(0, 30, 0), V(40, 30, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Distance", 4, 40.0))
        sk.setLabelDistance(3, LABELS["Constraint4"][3])
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sk)
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].fitAll()
        # fitAll animates; let it land
        QtCore.QTimer.singleShot(2500, one_view)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def one_view():
    try:
        view = state["view"]
        state["sizes"] = text_sizes(view)
        for name in LABELS:
            check_label(view, name)
        FreeCADGui.runCommand("Std_ViewCreate", 0)
        state["views"] = (view, FreeCADGui.activeDocument().activeView())
        mdi = FreeCADGui.getMainWindow().findChild(QtWidgets.QMdiArea)
        mdi.setViewMode(QtWidgets.QMdiArea.SubWindowView)
        mdi.tileSubWindows()
        QtCore.QTimer.singleShot(1500, views_fit)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def views_fit():
    try:
        for v in state["views"]:
            v.fitAll()
        QtCore.QTimer.singleShot(1500, two_views)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def two_views():
    try:
        for tag, v in zip("AB", state["views"]):
            check_label(v, "Constraint1", "in tiled view %s " % tag)
        FreeCADGui.activeDocument().resetEdit()
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
