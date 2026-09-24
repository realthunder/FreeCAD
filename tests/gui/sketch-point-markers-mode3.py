"""In render cache mode 3 a sketch's vertices are drawn as their markers.

The Sketcher draws its vertices with a SoMarkerSet: each point is a
bitmap, CIRCLE_FILLED at the marker size, which GL puts on screen with
glBitmap. The bgfx backend was handed them as plain points and drew each
as a square of the draw style's point size -- 8x8 where GL draws a 7x7
disk. While Coin drew the edit graph a second time on top, its disks
covered the squares; once the edit graph was drawn only by the backend
(4fa781fc58) the squares were what the user saw.

The measurement: the backend's own framebuffer (saveRenderDump, source
"renderer", read before Coin composites anything), around isolated
points placed off the grid lines. Each footprint must span the marker
size, not the point size, leave the corners of its box empty, and fill
about pi/4 of that box, as a disk does and a square does not.

Scored against the tree before the fix: every footprint was 8x8 with
all 64 pixels set.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PointMarkers"
# Off the 10 mm grid lines, so nothing but the marker is near them.
POINTS = [(23.5, 13.5), (-23.5, 13.5), (23.5, -13.5)]
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def run():
    try:
        import Part

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        for x, y in POINTS:
            sketch.addGeometry(Part.Point(V(x, y, 0)), False)
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sketch)
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].fitAll()
        QtCore.QTimer.singleShot(2000, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def footprint(img, cx, cy, reach, bg):
    """The pixels around (cx, cy) that differ from the background."""
    on = set()
    for y in range(cy - reach, cy + reach + 1):
        for x in range(cx - reach, cx + reach + 1):
            c = QtGui.QColor(img.pixel(x, y))
            d = (abs(c.red() - bg.red()) + abs(c.green() - bg.green())
                 + abs(c.blue() - bg.blue()))
            if d > 60:
                on.add((x, y))
    return on


def measure():
    try:
        view = state["view"]
        path = os.path.join(OUT, "backend.png")
        try:
            view.saveRenderDump(path, "renderer")
            active, detail = True, ""
        except Exception as e:
            active, detail = False, str(e)
        if not check("the bgfx renderer draws the view", active, detail):
            finish()
            return
        img = QtGui.QImage(path)
        size = FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/View").GetInt("MarkerSize", 7)
        h = img.height()
        for x, y in POINTS:
            vx, vy = view.getPointOnViewport(FreeCAD.Vector(x, y, 0))
            cx, cy = int(vx), int(h - 1 - vy)
            bg = QtGui.QColor(img.pixel(cx + 16, cy + 16))
            on = footprint(img, cx, cy, 8, bg)
            if not check("a marker is drawn at (%g, %g)" % (x, y), on):
                continue
            xs = [p[0] for p in on]
            ys = [p[1] for p in on]
            x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
            w, hh = x1 - x0 + 1, y1 - y0 + 1
            where = "%dx%d box, %d px" % (w, hh, len(on))
            check("the marker at (%g, %g) spans the marker size %d" % (x, y, size),
                  w == size and hh == size, where)
            corners = [(x0, y0), (x1, y0), (x0, y1), (x1, y1)]
            check("the marker at (%g, %g) leaves its corners empty" % (x, y),
                  not any(c in on for c in corners), where)
            share = len(on) / float(w * hh)
            check("the marker at (%g, %g) fills its box as a disk does" % (x, y),
                  0.65 < share < 0.9, "%s, %.3f of the box" % (where, share))
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
