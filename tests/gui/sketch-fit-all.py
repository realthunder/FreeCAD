"""Fit All in a sketch frames the sketch, not what a tool left behind.

Two things went into the box a fit frames that are not the sketch:

  - Tool feedback. The edit curve, the edit markers, the two hint line
    sets and the cursor's coordinate text each sit in the edit root with
    coordinates of their own, and a coordinate node with nothing set
    still holds one point at the origin. The cursor text is worse: when
    a tool ends its string is emptied and its position is not, and its
    glyph companion claims a point wherever it stands. So after a tool,
    a fit framed where the pointer had last been.

  - The origin of an empty sketch. With no geometry the origin point is
    all there is, the viewer widens a box that is a point by a hundredth
    each way so that a lone point can be framed at all, and the fit went
    to that: a camera height of 0.028, a hundred and fifty times closer
    than a new document's.

Now the feedback is left out of the scene's box, as the axes are, and a
scene that holds nothing but one point of what is being edited is
nothing to frame: the fit does not zoom and does not turn. It does go
to that point -- a sketch's origin may well be off the screen, entering
a sketch goes there only while FitSketchOnEdit is on, and the view may
have been moved since.

What must not change, and is checked: a sketch's fit takes its origin
with its geometry; an empty sketch beside a model is framed with it; a
document whose only object is a vertex is still framed (the reason the
viewer widens a point box in the first place).

Scored against the tree before the fix: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}


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


def mouse_move(view, pt):
    gv = view.graphicsView()
    vp = gv.viewport()
    x, y = view.getPointOnViewport(pt)
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    none = QtCore.Qt.NoButton
    QtWidgets.QApplication.sendEvent(
        vp, QtGui.QMouseEvent(QtCore.QEvent.MouseMove, pos, gv.mapToGlobal(pos.toPoint()),
                              none, none, QtCore.Qt.NoModifier))


def escape(view):
    gv = view.graphicsView()
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(
            gv, QtGui.QKeyEvent(kind, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier))


def camera(view):
    """(centre x, centre y, height) of a view looking down the Z axis."""
    cam = view.getCameraNode()
    pos = cam.position.getValue()
    return pos[0], pos[1], cam.height.getValue()


def frame(view, x, y, height):
    cam = view.getCameraNode()
    cam.position.setValue(x, y, cam.position.getValue()[2])
    cam.height.setValue(height)
    settle(0.5)


def fit(view, how):
    if how == "command":
        FreeCADGui.runCommand("Std_ViewFitAll")
    else:
        view.fitAll()
    # the fit is animated over ten frames
    settle(1.2)
    return camera(view)


def near(got, want, tolerance):
    return all(abs(g - w) <= tolerance for g, w in zip(got, want))


def off_centre(view, point):
    """How far, in pixels, a point is drawn from the centre of the view."""
    vp = view.graphicsView().viewport()
    dpr = vp.devicePixelRatioF()
    x, y = view.getPointOnViewport(point)
    return ((x - vp.width() * dpr / 2.0) ** 2 + (y - vp.height() * dpr / 2.0) ** 2) ** 0.5


def describe(cam):
    return "centre (%.4g, %.4g) height %.5g" % cam


def new_view(name):
    doc = FreeCAD.newDocument(name)
    view = FreeCADGui.getDocument(name).activeView()
    return doc, view


def edit(doc, sk, view):
    doc.recompute()
    FreeCADGui.getDocument(doc.Name).setEdit(sk)
    view.viewTop()
    settle(1.5)
    return FreeCADGui.getDocument(doc.Name).getInEdit() is not None


def run():
    try:
        import Part

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()

        # 1. An empty sketch: nothing to frame, by the call and by the
        # command. The view goes to the origin at the zoom it had.
        doc, view = new_view("FitEmpty")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        check("the empty sketch is in edit", edit(doc, sk, view))
        for how in ("call", "command"):
            frame(view, 5.0, 5.0, 50.0)
            cam = fit(view, how)
            check("an empty sketch: the fit goes to its origin and does not zoom (%s)" % how,
                  near(cam, (0.0, 0.0, 50.0), 0.01), describe(cam))

        # The origin selected: its highlight is a copy one layer higher,
        # which made the box a point no longer.
        FreeCADGui.Selection.addSelection(doc.Name, sk.Name, "RootPoint")
        settle(0.5)
        frame(view, 5.0, 5.0, 50.0)
        cam = fit(view, "call")
        check("nor does its origin, selected, give a fit something to zoom to",
              len(FreeCADGui.Selection.getSelectionEx()) == 1
              and near(cam, (0.0, 0.0, 50.0), 0.01), describe(cam))
        FreeCADGui.Selection.clearSelection()

        # 2. Geometry: the fit is the geometry and the origin.
        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle(0.5)
        sk.addGeometry(Part.LineSegment(V(20, 10, 0), V(30, 15, 0)))
        check("the sketch with a line is in edit", edit(doc, sk, view))
        want = (15.0, 7.5, (30.0 ** 2 + 15.0 ** 2) ** 0.5)
        frame(view, 100.0, 50.0, 300.0)
        cam = fit(view, "call")
        check("a sketch's fit takes its geometry and its origin",
              near(cam, want, 0.5), describe(cam))

        # 3. A tool, its pointer far from the geometry, left with Escape.
        frame(view, 100.0, 50.0, 300.0)
        FreeCADGui.runCommand("Sketcher_CreateLine")
        settle(0.5)
        for pt in (V(120, 60, 0), V(180, 90, 0), V(180, 90, 0)):
            mouse_move(view, pt)
            settle(0.3)
        escape(view)
        settle(0.6)
        check("Escape left the tool and not the sketch, and drew nothing",
              FreeCADGui.getDocument(doc.Name).getInEdit() is not None
              and len(sk.Geometry) == 1, "%d geometries" % len(sk.Geometry))
        cam = fit(view, "call")
        check("where a tool's pointer last was is not part of the fit",
              near(cam, want, 0.5), describe(cam))

        # 4. An empty sketch beside a model: both are framed.
        doc2, view2 = new_view("FitModel")
        box = doc2.addObject("Part::Box", "Box")
        box.Placement.Base = V(100, 100, 0)
        sk2 = doc2.addObject("Sketcher::SketchObject", "Sketch")
        check("the empty sketch beside a model is in edit", edit(doc2, sk2, view2))
        frame(view2, 300.0, 300.0, 20.0)
        cam = fit(view2, "call")
        check("an empty sketch beside a model is framed with it, origin and all",
              near(cam[:2], (55.0, 55.0), 1.0) and cam[2] > 110.0, describe(cam))

        # 5. A lone vertex, no edit: still framed.
        doc3, view3 = new_view("FitVertex")
        vertex = doc3.addObject("Part::Vertex", "Vertex")
        vertex.X, vertex.Y = 30, 20
        doc3.recompute()
        view3.viewTop()
        settle(0.5)
        frame(view3, 0.0, 0.0, 50.0)
        cam = fit(view3, "call")
        check("a document whose only object is a vertex is still framed",
              near(cam[:2], (30.0, 20.0), 0.01) and cam[2] < 1.0, describe(cam))

        # 6. An empty sketch off the global origin, on another plane.
        # Entering it turns the view to its plane and goes to its origin
        # (FitSketchOnEdit, which nothing here has stored: the default).
        # Then the view is moved away, and the fit brings the origin back.
        doc4, view4 = new_view("FitPlaced")
        origin = V(100, 50, 20)
        sk4 = doc4.addObject("Sketcher::SketchObject", "Sketch")
        sk4.Placement = FreeCAD.Placement(origin, FreeCAD.Rotation(V(1, 0, 0), 90))
        doc4.recompute()
        view4.getCameraNode().height.setValue(60.0)
        FreeCADGui.getDocument(doc4.Name).setEdit(sk4)
        settle(2.0)
        direction = view4.getViewDirection()
        check("entering the placed sketch turns the view to its plane and goes to its origin",
              abs(direction.y - 1.0) < 1e-3 and off_centre(view4, origin) < 2.0,
              "direction (%.2f, %.2f, %.2f), origin %.0f px off" % (
                  direction.x, direction.y, direction.z, off_centre(view4, origin)))
        cam4 = view4.getCameraNode()
        pos = cam4.position.getValue()
        cam4.position.setValue(pos[0] + 40.0, pos[1], pos[2] + 15.0)
        settle(0.5)
        check("the view moved away, the origin is well off its centre",
              off_centre(view4, origin) > 100.0, "%.0f px off" % off_centre(view4, origin))
        view4.fitAll()
        settle(1.2)
        after = view4.getViewDirection()
        check("the fit brings the origin to the centre of the view",
              off_centre(view4, origin) < 2.0, "%.1f px off" % off_centre(view4, origin))
        check("at the zoom the view had, and facing the way it did",
              abs(view4.getCameraNode().height.getValue() - 60.0) < 0.01
              and (after - direction).Length < 1e-4,
              "height %.5g" % view4.getCameraNode().height.getValue())

        for name in ("FitEmpty", "FitModel", "FitPlaced"):
            FreeCADGui.getDocument(name).resetEdit()
        settle(0.5)
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
