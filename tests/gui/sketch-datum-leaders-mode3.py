"""A Sketcher dimension's arrowheads are drawn under the bgfx renderer
(render cache mode 3, the default), at a size in PIXELS that does not change
when the view zooms.

GL sizes a datum's arrowheads, the gap its line leaves for the number and
its extension-line overshoot in screen pixels, recomputing them from the
view every frame. The render-cache capture is taken once, without the
viewer's camera, and baked them at Coin's default view volume -- two world
units across the viewport, about a hundred times too small -- so under the
backend a dimension had no arrowheads at all. The leader vertices now carry
pixel offsets the backend resolves against the camera it draws with
(Render::MeshData::screenOffsets).

The measurement, on a horizontal DistanceX dimension: the height of the
label-coloured run just inside the dimension line's left end, where only
the line and its arrowhead are. The line alone is two pixels; with the
arrowhead it is about half the label font's height. Taken at the fit, then
again after halving the camera height: the second must match the first.

Scored against the tree before the fix: the renderer check passes and both
arrowhead checks fail.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DatumLeadersMode3"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def is_label(c):
    return c.red() > c.green() + 50 and c.red() > c.blue() + 40


def arrow_height(view, tag):
    """Height of the label-coloured run at the dimension line's left end."""
    img = view.graphicsView().viewport().grab().toImage()
    img.save(os.path.join(OUT, tag + ".png"))
    h = img.height()
    left = view.getPointOnViewport(FreeCAD.Vector(0, 20, 0))
    right = view.getPointOnViewport(FreeCAD.Vector(60, 20, 0))
    x0, x1 = int(left[0]), int(right[0])
    y0 = h - int(left[1])
    # The dimension line: the row with the most label-coloured pixels over
    # the middle half of the dimension, above or below the sketch line.
    best, row = 0, None
    for y in range(max(0, y0 - 200), min(h, y0 + 200)):
        n = sum(1 for x in range((3 * x0 + x1) // 4, (x0 + 3 * x1) // 4, 3)
                if is_label(QtGui.QColor(img.pixel(x, y))))
        if n > best:
            best, row = n, y
    if row is None:
        return 0, "no dimension line"
    rows = set()
    for y in range(max(0, row - 20), min(h, row + 21)):
        for x in range(x0 + 3, x0 + 8):
            if is_label(QtGui.QColor(img.pixel(x, y))):
                rows.add(y)
                break
    extent = (max(rows) - min(rows) + 1) if rows else 0
    return extent, "line row %d, columns %d..%d" % (row, x0 + 3, x0 + 7)


def run():
    try:
        import Part
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        # Off the X axis, which is drawn red too.
        sketch.addGeometry(Part.LineSegment(V(0, 20, 0), V(60, 20, 0)), False)
        sketch.addConstraint(Sketcher.Constraint("DistanceX", 0, 1, 0, 2, 60.0))
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sketch)
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].fitAll()
        QtCore.QTimer.singleShot(2000, measure_fit)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure_fit():
    try:
        view = state["view"]
        try:
            view.saveRenderDump(os.path.join(OUT, "dump.png"))
            active, detail = True, ""
        except Exception as e:
            active, detail = False, str(e)
        if not check("the bgfx renderer draws the view", active, detail):
            finish()
            return
        view.waitFrameComplete()
        state["fit"] = arrow_height(view, "fit")
        # Zoom 2x onto the line's left end, which a zoom about the centre
        # would push out of a small window.
        cam = view.getCameraNode()
        cam.height.setValue(cam.height.getValue() / 2.0)
        pos = cam.position.getValue()
        cam.position.setValue(5.0, 20.0, pos[2])
        view.redraw()
        QtCore.QTimer.singleShot(1500, measure_zoom)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure_zoom():
    try:
        view = state["view"]
        view.waitFrameComplete()
        zoom = arrow_height(view, "zoom")
        fit = state["fit"]
        check("the arrowhead is drawn", fit[0] >= 5,
              "run %d px (a bare line is 2); %s" % fit)
        check("the arrowhead keeps its pixel size when the view zooms",
              fit[0] >= 5 and abs(zoom[0] - fit[0]) <= 1,
              "fit %d px, zoomed 2x %d px; %s" % (fit[0], zoom[0], zoom[1]))
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
