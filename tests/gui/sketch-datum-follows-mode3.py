"""A Sketcher dimension's drawing follows its constraint under the bgfx
renderer (render cache mode 3, the default).

The leaders and the number of a datum label reach the render-cache capture
through companion shapes that are not below the label, so a change of the
label's fields told the capture nothing: it went on drawing the label as it
first found it. Setting a DistanceX from 60 to 30 left "60 mm" drawn across
the old 60 units while the line shrank to 30.

The measurement: the horizontal extent of the label-coloured pixels in a
band around the dimension line, before and after the change. The line is
kept off the X axis, which is drawn in the same red.

Scored against the tree before the fix: the renderer check passes, the
follow check fails (extent 0..60 after the change).
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DatumFollowsMode3"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    import time
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.01)


def is_label(c):
    return c.red() > c.green() + 50 and c.red() > c.blue() + 40


def extent(tag):
    """World x range of label-coloured pixels between y = 22 and 40."""
    view = state["view"]
    view.redraw()
    settle(0.5)
    view.waitFrameComplete()
    img = view.graphicsView().viewport().grab().toImage()
    img.save(os.path.join(OUT, tag + ".png"))
    xs = []
    for wx in range(-5, 71):
        for wy in range(22, 40):
            p = view.getPointOnViewport(FreeCAD.Vector(wx, wy, 0))
            x, y = int(p[0]), img.height() - 1 - int(p[1])
            if is_label(QtGui.QColor(img.pixel(x, y))):
                xs.append(wx)
                break
    return (min(xs), max(xs)) if xs else None


def run():
    try:
        import Part
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(0, 20, 0), V(60, 20, 0)), False)
        sk.addConstraint(Sketcher.Constraint("DistanceX", 0, 1, 0, 2, 60.0))
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        cam = view.getCameraNode()
        cam.position.setValue(30.0, 20.0, cam.position.getValue()[2])
        cam.height.setValue(120.0)
        state.update(view=view, sk=sk, doc=doc)
        QtCore.QTimer.singleShot(1500, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure():
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
        before = extent("before")
        state["sk"].setDatum(0, FreeCAD.Units.Quantity("30 mm"))
        state["doc"].recompute()
        settle(0.5)
        after = extent("after")
        line = state["sk"].Geometry[0]
        lo, hi = line.StartPoint.x, line.EndPoint.x
        check("the dimension spans the line before the change",
              before is not None and abs(before[0] - 0) <= 1 and abs(before[1] - 60) <= 1,
              "label x %s" % (before,))
        check("and follows it when the constraint changes",
              after is not None and abs(after[0] - lo) <= 1.5 and abs(after[1] - hi) <= 1.5,
              "label x %s, line x %.1f..%.1f" % (after, lo, hi))
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
