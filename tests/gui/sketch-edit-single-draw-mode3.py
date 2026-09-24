"""In render cache mode 3 the Sketcher's edit graph is drawn once: by the
bgfx backend, not by the backend AND by Coin on top of it.

The backend draws the edit graph from its editing overlay feed. Coin's GL
pass, which composites what the backend does not draw, traversed the edit
root as well -- only datum labels and images stood down -- so the sketch
lines, point markers and grid were drawn a second time over the backend's
frame. The second copy is what the eye sees: Coin's grid over the datums
the backend had put on top of it, Coin's white edges over the red
extension lines, Coin's round markers over the backend's.

The measurement: the viewport as it is on screen against the backend's
own framebuffer (saveRenderDump, source "renderer", read back BEFORE Coin
composites). What differs is what Coin drew. The sketch sits on the 10 mm
grid so the second copy has something to cover.

Scored against the tree before the fix: about 1% of the viewport differs
(the grid, the edges and the markers, drawn again); after it, well under
a tenth of that.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "EditSingleDraw"
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
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        pts = [V(0, 0, 0), V(60, 0, 0), V(60, 30, 0), V(0, 30, 0)]
        for i in range(4):
            sketch.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]), False)
        for i in range(4):
            sketch.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
        sketch.addConstraint(Sketcher.Constraint("DistanceX", 0, 1, 0, 2, 60.0))
        sketch.addConstraint(Sketcher.Constraint("DistanceY", 1, 1, 1, 2, 30.0))
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sketch)
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].fitAll()
        QtCore.QTimer.singleShot(2000, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure():
    try:
        view = state["view"]
        backend = os.path.join(OUT, "backend.png")
        try:
            view.saveRenderDump(backend, "renderer")
            active, detail = True, ""
        except Exception as e:
            active, detail = False, str(e)
        if not check("the bgfx renderer draws the view", active, detail):
            finish()
            return
        view.waitFrameComplete()
        screen = view.graphicsView().viewport().grab().toImage()
        screen.save(os.path.join(OUT, "screen.png"))
        mine = QtGui.QImage(backend)
        if not check("the backend frame and the viewport are the same size",
                     mine.size() == screen.size(),
                     "%s vs %s" % (mine.size(), screen.size())):
            finish()
            return
        w, h = screen.width(), screen.height()
        differ = 0
        for y in range(h):
            for x in range(w):
                a = QtGui.QColor(screen.pixel(x, y)).lightness()
                b = QtGui.QColor(mine.pixel(x, y)).lightness()
                if abs(a - b) > 12:
                    differ += 1
        share = differ / float(w * h)
        check("Coin draws nothing of the edit graph over the backend",
              share < 0.002,
              "%d of %d px differ (%.4f)" % (differ, w * h, share))
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
