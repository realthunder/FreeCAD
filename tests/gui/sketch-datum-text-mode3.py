"""A Sketcher dimension's number is drawn at its own size under the bgfx
renderer (render cache mode 3, the default).

In mode 3 the raw-GL SoDatumLabel draw is suppressed and the number is a
textured quad the backend scales every frame through an autozoom whose
factor the label computes as k / viewport height. k was 7.5, fitted by eye
while the capture traversal still saw Coin's 100 px default viewport; once
the capture got the real viewport the number came out nearly seven times
too small -- a smudge of a few pixels where the value should be.

The measurement: the vertical extent of label-coloured pixels over the
middle of a horizontal DistanceX dimension, where only the number and the
dimension line are. It must be about the height of the label font's
digits. With the number missing or shrunk, only the line's few pixels
remain.

Scored against the tree before the fix: the renderer check passes and the
extent check fails.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DatumTextMode3"
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
        from pivy import coin

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
        QtCore.QTimer.singleShot(2000, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure():
    try:
        from pivy import coin

        view = state["view"]
        try:
            view.saveRenderDump(os.path.join(OUT, "dump.png"))
            active = True
            detail = ""
        except Exception as e:
            active = False
            detail = str(e)
        if not check("the bgfx renderer draws the view", active, detail):
            finish()
            return
        view.waitFrameComplete()

        sa = coin.SoSearchAction()
        sa.setType(coin.SoType.fromName("SoDatumLabel"))
        sa.setInterest(coin.SoSearchAction.FIRST)
        sa.setSearchingAll(True)
        sa.apply(view.getAuxSceneGraph())
        label = sa.getPath().getTail() if sa.getPath() else None
        if not check("the sketch shows a datum label", label is not None):
            finish()
            return
        font = QtGui.QFont(label.getField("name").get().getString(),
                           int(round(float(label.getField("size").get().getString()))))
        cap = QtGui.QFontMetrics(font).capHeight()

        viewport = view.graphicsView().viewport()
        img = viewport.grab().toImage()
        img.save(os.path.join(OUT, "view.png"))
        h = img.height()
        # The dimension's middle, projected; Coin's y runs up.
        left = view.getPointOnViewport(FreeCAD.Vector(20, 20, 0))
        right = view.getPointOnViewport(FreeCAD.Vector(40, 20, 0))
        x0, x1 = sorted((int(left[0]), int(right[0])))
        yc = h - int(left[1])
        rows = set()
        for y in range(max(0, yc - 120), min(h, yc + 120)):
            for x in range(x0, x1):
                c = QtGui.QColor(img.pixel(x, y))
                if c.red() > c.green() + 50 and c.red() > c.blue() + 40:
                    rows.add(y)
                    break
        extent = (max(rows) - min(rows) + 1) if rows else 0
        check("the number is drawn at its font's size",
              extent >= 0.8 * cap,
              "label-coloured rows span %d px, digit cap height %d px, "
              "columns %d..%d" % (extent, cap, x0, x1))

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
