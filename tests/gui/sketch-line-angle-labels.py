"""A sketch line angle's end lines reach the lines it measures.

An angle dimension draws an arc at twice its LabelDistance from the angle's
vertex, with an end line across each end of the arc. For an arc's angle the
end lines run back to the arc (df867a25b2, taken earlier), but for a line
they stayed ticks of a few pixels however far the label arc was from the
lines (upstream 827781ab3f, dca00ec80e):

  - a single line's angle is measured from the horizontal through its
    middle; that reference direction was never drawn, only a tick at the
    label arc;
  - two lines that stop short of the label arc were not continued out to
    it, and two lines that start beyond it were not joined to it.

Every constraint keeps the default LabelDistance of 10, so each label arc
is 20 from its vertex. Checked by label-coloured pixels part way along each
end line, well clear of the tick an end line had before.

GT_RENDER_CACHE sets the render cache mode for the run (default: the
view's own, 3); 0 checks the GL path of SoDatumLabel instead of bgfx's.

Scored against the tree before the fix: see the commit message.
"""
import math
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
MODE = os.environ.get("GT_RENDER_CACHE")
DOC = "SketchLineAngleLabels"
state = {"done": False}

LABEL_R = 20.0  # 2 * the default LabelDistance
ANGLE = math.radians(60)
U0 = (1.0, 0.0)
U60 = (math.cos(ANGLE), math.sin(ANGLE))
# Everything sits 15 above the sketch's X axis, which is drawn in a
# colour the label check cannot tell from a label's.
Y0 = 15.0
# The single line: its middle at M, length 20, at 60 degrees.
M = (0.0, Y0)
# Two line pairs at 0 and 60 degrees from a vertex neither line reaches:
# NEAR's lines run 6..12 from its vertex, inside the label arc; FAR's run
# 26..34, outside it.
NEAR = (60.0, Y0)
FAR = (110.0, Y0)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.0):
    import time
    end = time.monotonic() + seconds
    while True:
        QtCore.QCoreApplication.processEvents()
        if time.monotonic() >= end:
            break
        time.sleep(0.01)


def along(origin, u, d):
    return (origin[0] + u[0] * d, origin[1] + u[1] * d)


def is_label(c):
    return c.red() > c.green() + 50 and c.red() > c.blue() + 40


def grab(tag):
    view = FreeCADGui.getDocument(DOC).ActiveView
    view.redraw()
    settle(0.3)
    view.waitFrameComplete()
    img = view.graphicsView().viewport().grab().toImage()
    img.save(os.path.join(OUT, tag + ".png"))
    return img


def pixel(img, world):
    view = FreeCADGui.getDocument(DOC).ActiveView
    p = view.getPointOnViewport(FreeCAD.Vector(world[0], world[1], 0))
    return int(p[0]), img.height() - 1 - int(p[1])


def label_near(img, world, radius=3):
    """Label-coloured pixels within radius pixels of a world point."""
    x0, y0 = pixel(img, world)
    n = 0
    for y in range(y0 - radius, y0 + radius + 1):
        for x in range(x0 - radius, x0 + radius + 1):
            if 0 <= x < img.width() and 0 <= y < img.height() \
                    and is_label(QtGui.QColor(img.pixel(x, y))):
                n += 1
    return n


def run():
    try:
        import Part
        import Sketcher

        if MODE is not None:
            FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
                "RenderCache", int(MODE))
        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")

        def line(a, b):
            return sk.addGeometry(Part.LineSegment(V(a[0], a[1], 0), V(b[0], b[1], 0)), False)

        g = line(along(M, U60, -10.0), along(M, U60, 10.0))
        sk.addConstraint(Sketcher.Constraint("Angle", g, ANGLE))
        for vertex, d0, d1 in ((NEAR, 6.0, 12.0), (FAR, 26.0, 34.0)):
            g1 = line(along(vertex, U0, d0), along(vertex, U0, d1))
            g2 = line(along(vertex, U60, d0), along(vertex, U60, d1))
            sk.addConstraint(Sketcher.Constraint("Angle", g1, g2, ANGLE))
        doc.recompute()
        state["geo"] = [(tuple(l.StartPoint), tuple(l.EndPoint)) for l in sk.Geometry]
        FreeCADGui.activeDocument().setEdit(sk)
        FreeCADGui.activeDocument().activeView().viewTop()
        state["sk"] = sk
        QtCore.QTimer.singleShot(1500, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure():
    try:
        sk = state["sk"]
        view = FreeCADGui.getDocument(DOC).ActiveView
        if MODE is not None:
            try:
                view.saveRenderDump(os.path.join(OUT, "dump.txt"))
                active = True
            except Exception:
                active = False
            check("the render cache mode is the one asked for (%s)" % MODE,
                  active == (int(MODE) == 3), "renderer active: %s" % active)

        moved = [(tuple(l.StartPoint), tuple(l.EndPoint)) for l in sk.Geometry] != state["geo"]
        check("the constraints hold the lines where they were drawn", not moved)

        # Frame x -10..150, y 0..55, whatever the viewport's shape.
        w = view.graphicsView().viewport()
        aspect = w.width() / float(max(1, w.height()))
        cam = view.getCameraNode()
        cam.position.setValue(70.0, 27.0, cam.position.getValue()[2])
        cam.height.setValue(max(60.0, 170.0 / aspect))
        settle(1.0)
        img = grab("drawn")

        # Where an end line had only its tick, the first probe tells the
        # tick's pixels from the line's: on the label arc at each end.
        n = label_near(img, along(M, U0, LABEL_R))
        check("the single line's label arc is drawn", n > 0, "%d px" % n)

        probes = [
            ("a single line's angle draws its horizontal reference from the line's middle",
             along(M, U0, LABEL_R / 2)),
            ("lines short of the label arc are continued out to it (first line)",
             along(NEAR, U0, 16.0)),
            ("lines short of the label arc are continued out to it (second line)",
             along(NEAR, U60, 16.0)),
            ("lines beyond the label arc are joined to it (first line)",
             along(FAR, U0, 23.0)),
            ("lines beyond the label arc are joined to it (second line)",
             along(FAR, U60, 23.0)),
        ]
        for name, world in probes:
            x, y = pixel(img, world)
            inside = 0 <= x < img.width() and 0 <= y < img.height()
            n = label_near(img, world)
            check(name, inside and n > 0,
                  "%d px at (%d, %d)%s" % (n, x, y, "" if inside else " OUTSIDE the view"))

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
