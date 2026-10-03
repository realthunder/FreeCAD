"""A sketch arc's length and angle dimensions, drawn and dragged.

An arc length constraint (a Distance on one arc) had no label at all in an
edit: the Distance case only knew a line segment and left the arc's label
empty (upstream 646b4381f9, the ARCLENGTH datum type, never taken). Dragging
it went down the radius code, which also turned its LabelPosition into an
angle.

An arc's angle label could not be put on the other side of the arc's centre:
the drag set LabelDistance from the cursor's distance, never negative
(upstream f3e1e6cec0, 7bcaa766de). And its end lines stopped at the label
instead of reaching back to the arc (df867a25b2).

Measured on the desktop view under the default renderer (render cache mode
3). Labels are dragged the way sketch-undo-during-drag.py does: the
constraint preselected through Gui.Selection, then real press / move /
release events on the viewport.

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
DOC = "SketchArcLabels"
state = {"done": False}

# Arc A carries the length, arc B the angle; both 30..150 degrees, radius 30,
# so each one's middle direction is straight up.
A = (0.0, 0.0)
B = (100.0, 0.0)
R = 30.0


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


def viewport():
    from PySide import QtWidgets
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if ("Quarter" in name or "View3DInventorViewer" in name) and w.isVisible():
            return w
    return None


def mouse(w, typ, world, button, buttons):
    from PySide import QtWidgets
    view = FreeCADGui.getDocument(DOC).ActiveView
    p = view.getPointOnViewport(FreeCAD.Vector(world[0], world[1], 0))
    p = QtCore.QPointF(p[0], w.height() - 1 - p[1])
    QtWidgets.QApplication.sendEvent(
        w, QtGui.QMouseEvent(typ, p, w.mapToGlobal(p), button, buttons,
                             QtCore.Qt.NoModifier))
    settle()


def drag_label(sk, index, frm, to):
    w = viewport()
    left = QtCore.Qt.LeftButton
    FreeCADGui.Selection.setPreselection(sk, "Constraint%d" % (index + 1))
    mouse(w, QtCore.QEvent.MouseButtonPress, frm, left, left)
    mid = ((frm[0] + to[0]) / 2, (frm[1] + to[1]) / 2)
    mouse(w, QtCore.QEvent.MouseMove, mid, QtCore.Qt.NoButton, left)
    mouse(w, QtCore.QEvent.MouseMove, to, QtCore.Qt.NoButton, left)
    mouse(w, QtCore.QEvent.MouseButtonRelease, to, left, QtCore.Qt.NoButton)
    settle(0.5)
    # A label drag that lands leaves the label preselected, drawn in the
    # preselection colour rather than the one is_label() looks for.
    FreeCADGui.Selection.clearPreselection()
    settle(0.3)


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


def label_near(img, world, radius=3):
    """Label-coloured pixels within radius pixels of a world point."""
    view = FreeCADGui.getDocument(DOC).ActiveView
    p = view.getPointOnViewport(FreeCAD.Vector(world[0], world[1], 0))
    x0, y0 = int(p[0]), img.height() - 1 - int(p[1])
    n = 0
    for y in range(y0 - radius, y0 + radius + 1):
        for x in range(x0 - radius, x0 + radius + 1):
            if 0 <= x < img.width() and 0 <= y < img.height() \
                    and is_label(QtGui.QColor(img.pixel(x, y))):
                n += 1
    return n


def datum_labels():
    from pivy import coin
    view = FreeCADGui.getDocument(DOC).ActiveView
    out = []
    for root in (view.getSceneGraph(), view.getAuxSceneGraph()):
        if root is None:
            continue
        sa = coin.SoSearchAction()
        sa.setType(coin.SoType.fromName("SoDatumLabel"))
        sa.setInterest(coin.SoSearchAction.ALL)
        sa.apply(root)
        paths = sa.getPaths()
        for i in range(paths.getLength()):
            n = paths[i].getTail()
            out.append((str(n.getField("datumtype").get()), str(n.getField("string").get()),
                        len(n.getField("pnts")), round(n.getField("param1").getValue(), 3)))
    return out


def run():
    try:
        import Part
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        for c in (A, B):
            sk.addGeometry(Part.ArcOfCircle(Part.Circle(V(c[0], c[1], 0), V(0, 0, 1), R),
                                            math.radians(30), math.radians(150)), False)
        sk.addConstraint(Sketcher.Constraint("Distance", 0, R * math.radians(120)))
        sk.addConstraint(Sketcher.Constraint("Angle", 1, math.radians(120)))
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        state["sk"] = sk
        QtCore.QTimer.singleShot(1500, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure():
    try:
        sk = state["sk"]
        view = FreeCADGui.getDocument(DOC).ActiveView
        # Frame both arcs and the room below them, the same every run.
        cam = view.getCameraNode()
        cam.position.setValue(50.0, 5.0, cam.position.getValue()[2])
        cam.height.setValue(140.0)
        settle(1.0)

        labels = datum_labels()
        arc = [l for l in labels if l[0] == "ARCLENGTH"]
        check("an arc length constraint has a label of its own",
              len(arc) == 1 and arc[0][2] == 3 and "mm" in arc[0][1], labels)

        # Put the labels where the drawing checks look (LabelDistance is
        # read-only from Python): the length's dimension arc 15 out from the
        # arc, the angle's label arc 2 * 25 = 50 from its centre. The press
        # points are the default labels'; the preselection is what picks.
        pos = sk.Constraints[0].LabelPosition
        drag_label(sk, 0, (A[0], 10.0), (A[0], 45.0))
        d = sk.Constraints[0].LabelDistance
        check("an arc length label drags to the cursor's distance along the arc",
              abs(d - 45.0) < 1.0, "LabelDistance %.3f, want 45" % d)
        check("and the drag leaves its LabelPosition alone",
              abs(sk.Constraints[0].LabelPosition - pos) < 1e-9,
              "%.4f -> %.4f" % (pos, sk.Constraints[0].LabelPosition))
        drag_label(sk, 1, (B[0], 20.0), (B[0], 50.0))
        d = sk.Constraints[1].LabelDistance
        check("an arc angle label drags through the cursor",
              abs(d - 25.0) < 0.5, "LabelDistance %.3f, want 25 (drawn at 2x)" % d)

        img = grab("drawn")
        # The dimension arc's top: the arc raised by 45 - 30 along its middle.
        top = label_near(img, (A[0], 15.0 + R))
        check("the arc length's dimension arc is drawn", top > 0, "%d px" % top)
        # The extension line from the arc's start, half way to the dimension arc.
        start = (A[0] + R * math.cos(math.radians(30)), A[1] + R * math.sin(math.radians(30)))
        ext = label_near(img, (start[0], start[1] + 7.5))
        check("its extension line is drawn", ext > 0, "%d px" % ext)

        # Arc B's angle: an end line runs from the label's arc back to the arc.
        bstart = (B[0] + 40.0 * math.cos(math.radians(30)),
                  B[1] + 40.0 * math.sin(math.radians(30)))
        n = label_near(img, bstart)
        check("an arc angle's end line reaches back toward the arc", n > 0, "%d px" % n)

        # Drag the length's label past the centre.
        drag_label(sk, 0, (A[0], 45.0), (A[0], -20.0))
        d = sk.Constraints[0].LabelDistance
        check("an arc length label drags past the arc's centre",
              abs(d + 20.0) < 1.0, "LabelDistance %.3f, want -20" % d)
        img = grab("length-past-centre")
        # Past the centre, the arc moved down by 50: its top at -20.
        n = label_near(img, (A[0], -20.0))
        check("and is drawn there", n > 0, "%d px" % n)

        # Drag the angle's label to 40 below the arc's centre.
        drag_label(sk, 1, (B[0], 50.0), (B[0], -40.0))
        d = sk.Constraints[1].LabelDistance
        check("an arc angle label drags past the arc's centre, through the cursor",
              abs(d + 20.0) < 0.5, "LabelDistance %.3f, want -20 (drawn at 2x)" % d)
        img = grab("angle-past-centre")
        # Drawn turned half way round about the centre: its middle at -40,
        # beside the number.
        side = (B[0] - 40.0 * math.cos(math.radians(60)), B[1] - 40.0 * math.sin(math.radians(60)))
        n = label_near(img, side)
        check("and its arc is drawn there", n > 0, "%d px" % n)
        # The end line through the centre, from the arc's start to the label.
        thru = (B[0] - 10.0 * math.cos(math.radians(30)), B[1] - 10.0 * math.sin(math.radians(30)))
        n = label_near(img, thru)
        check("with its end line through the centre", n > 0, "%d px" % n)

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
