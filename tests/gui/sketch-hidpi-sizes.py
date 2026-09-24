"""Sketch edit mode is sized in DEVICE pixels on a scaled screen
(upstream 418c09899b, 37b4560893).

Coin draws into the GL framebuffer, whose pixels are device pixels: on a
screen at 200% one logical pixel is two of them. The edit mode's line
widths, point sizes, marker bitmaps, Coin text and datum labels are all
device-pixel quantities, and the fork scaled them by logical DPI / 96 --
which Qt 6 keeps at about 96 on a scaled screen, putting the whole scale
into the device pixel ratio instead. So on a 2x screen every one of them
came out at half its size.

Run with QT_SCALE_FACTOR=2 (the ctest registration sets it), which gives
the viewer a device pixel ratio of 2 under xvfb. The first check is that
precondition: without it every other check is vacuous.

Scored against the tree before the fix: the ratio is 2 and the three
size checks fail, each at exactly half its expected value.
"""
import math
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "HiDpiSizes"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def find(view, name=None, type_name=None):
    sa = coin.SoSearchAction()
    if name:
        sa.setName(name)
    else:
        sa.setType(coin.SoType.fromName(type_name))
    sa.setInterest(coin.SoSearchAction.FIRST)
    sa.setSearchingAll(True)
    for graph in (view.getSceneGraph(), view.getAuxSceneGraph()):
        if graph is None:
            continue
        sa.apply(graph)
        path = sa.getPath()
        if path is not None:
            return path.getTail()
    return None


def run():
    try:
        import Part
        import Sketcher

        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0),
                                            FreeCAD.Vector(20, 0, 0)), False)
        sketch.addConstraint(Sketcher.Constraint("Distance", 0, 20.0))
        doc.recompute()

        FreeCADGui.activeDocument().setEdit(sketch)
        QtCore.QCoreApplication.processEvents()
        view = FreeCADGui.activeDocument().activeView()

        dpr = view.graphicsView().devicePixelRatioF() \
            if hasattr(view, "graphicsView") else \
            FreeCADGui.getMainWindow().devicePixelRatioF()
        if not check("the viewer runs at device pixel ratio 2", abs(dpr - 2.0) < 1e-6,
                     "QT_SCALE_FACTOR=%s dpr=%s"
                     % (os.environ.get("QT_SCALE_FACTOR"), dpr)):
            finish()
            return

        curves = find(view, name="CurvesDrawStyle")
        points = find(view, name="PointsDrawStyle")
        width = curves.lineWidth.getValue() if curves else None
        size = points.pointSize.getValue() if points else None
        check("curve line width is 3 device pixels per logical pixel",
              width == 6.0, width)
        check("point size is 8 device pixels per logical pixel",
              size == 16.0, size)

        # Datum labels take POINTS (SoDatumLabel draws through QFont): the
        # application font's height in logical pixels, times the ratio, in
        # points at the screen's logical DPI -- upstream's sizing, taken at
        # the user's ruling. The fork used to hand them the pixel value.
        logical = QtGui.QFontMetrics(QtWidgets.QApplication.font()).height()
        dpi = QtWidgets.QApplication.primaryScreen().logicalDotsPerInchX()
        want = math.floor(logical * dpr * 72.0 / dpi + 0.5)
        label = find(view, type_name="SoDatumLabel")
        lsize = float(label.getField("size").get().getString()) if label else None
        check("datum label font is the Coin font size in points, scaled",
              lsize is not None and abs(lsize - want) < 0.5,
              "size=%s want=%s logical font height=%s dpi=%s"
              % (lsize, want, logical, dpi))

        FreeCADGui.activeDocument().resetEdit()
        QtCore.QCoreApplication.processEvents()
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
