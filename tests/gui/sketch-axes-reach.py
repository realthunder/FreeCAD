"""The sketch's axes reach across any view (upstream 5969df37f4, the
fork's way).

The two axes of a sketch in edit were drawn as long as the sketch is big
-- e to the next whole power above its largest coordinate -- so zooming
out or panning away left them ending in mid-air. Upstream stretches them
to the viewport on every camera change. Here the edit geometry is one
drawing shared by every view and every served client, so nothing in it
may depend on one camera: each axis is a polyline stepped by decades out
to ten kilometres, and stays out of the bounding box as it was. A decade
step keeps each piece's ends within a factor of ten of what a view of it
shows, which is what a single line of that length could not: its ends
would be eight orders past a close view, beyond single precision.

A sketch holding one 10 mm line.

Claims:
  - each axis reaches at least 1e6 either way;
  - a fit in edit still frames the sketch, not the axes;
  - zoomed far out, a point 3 m along the horizontal axis picks H_Axis and
    one on the vertical axis V_Axis;
  - zoomed in to 0.05 mm, the axis is still picked at the origin's side,
    and not 0.01 mm off it;
  - at both zooms the backend's frame has the axis on that point.

Scored against the tree before the change: the axes ended at 148.4, and
the far picks found nothing.
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
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.5):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def axis_points(view):
    from pivy import coin

    search = coin.SoSearchAction()
    search.setName(coin.SbName("RootCrossCoordinate"))
    search.setInterest(coin.SoSearchAction.FIRST)
    search.apply(view.getAuxSceneGraph())
    path = search.getPath()
    if path is None:
        return None
    node = path.getTail()
    return [tuple(node.point[i].getValue()) for i in range(node.point.getNum())]


def picked(view, xy):
    import SketcherGui

    x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
    info = SketcherGui.getActiveSketchPreselection((int(x), int(y)))
    if not info:
        return ()
    return tuple(info.get("SubElementNames") or ())


def pixel(view, xy, tag):
    """The backend's own frame at a point of the sketch plane."""
    path = os.path.join(OUT, "%s.png" % tag)
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def differ(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) > 30


def look(view, centre, height):
    cam = view.getCameraNode()
    cam.position.setValue(centre[0], centre[1], cam.position.getValue()[2])
    cam.height.setValue(height)
    settle(0.6)


def run():
    try:
        import Part

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchAxesReach")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(2, 3, 0), V(12, 3, 0)), False)
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.setCameraType("Orthographic")
        view.fitAll()
        state.update(sk=sk, view=view)
        QtCore.QTimer.singleShot(2000, probe)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def probe():
    try:
        view = state["view"]
        pts = axis_points(view)
        check("the axes are in the edit scene", bool(pts), pts and len(pts))
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        check("the horizontal axis reaches 1e6 either way",
              min(xs) <= -1e6 and max(xs) >= 1e6, (min(xs), max(xs)))
        check("the vertical axis reaches 1e6 either way",
              min(ys) <= -1e6 and max(ys) >= 1e6, (min(ys), max(ys)))

        view.fitAll()
        settle(1.0)
        height = view.getCameraNode().height.getValue()
        check("a fit in edit frames the sketch, not the axes", height < 100, height)

        look(view, (0, 0), 10000)
        got = picked(view, (3000, 0))
        check("3 m out along the horizontal axis picks it", got == ("H_Axis",), got)
        got = picked(view, (0, -3000))
        check("3 m out along the vertical axis picks it", got == ("V_Axis",), got)

        on, off = pixel(view, (3000, 0), "far-on"), pixel(view, (3000, 700), "far-off")
        check("and the frame draws the axis there", differ(on, off), (on, off))

        look(view, (-0.3, 0), 0.05)
        got = picked(view, (-0.31, 0))
        check("zoomed in to 0.05 mm the axis is picked where it is", got == ("H_Axis",), got)
        got = picked(view, (-0.31, 0.01))
        check("and not 0.01 mm off it", got == (), got)
        on, off = pixel(view, (-0.31, 0), "near-on"), pixel(view, (-0.31, 0.01), "near-off")
        check("and the frame draws it there, on the line", differ(on, off), (on, off))

        FreeCADGui.activeDocument().resetEdit()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        if FreeCADGui.activeDocument() and FreeCADGui.activeDocument().getInEdit():
            FreeCADGui.activeDocument().resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
