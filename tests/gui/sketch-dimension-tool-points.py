"""The Dimension tool on a level line, and on a corner picked twice
(upstream 76a84f63ab, 36dd4b983c).

Two things the tool decides from what it is given.

A horizontal or vertical line has one distance to state. The tool chose
between a horizontal, a vertical and an aligned distance from where the
pointer is, and for a level line that only swapped the constraint's type
back and forth as the pointer left the band above the line -- the value is
the same. A level line now keeps the distance along its own axis wherever
the pointer goes. A slanted line still switches.

Two points that are coincident are one point. A corner where two lines
meet holds one vertex of each, and a box selection takes both; with a
third point the tool was handed three points and offered nothing. The
coincident ones now count once.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False, "n": 0}
DISTANCES = ("Distance", "DistanceX", "DistanceY")


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def move(view, xy):
    gv = view.graphicsView()
    vp = gv.viewport()
    x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    ev = QtGui.QMouseEvent(QtCore.QEvent.MouseMove, pos, gv.mapToGlobal(pos.toPoint()),
                           QtCore.Qt.NoButton, QtCore.Qt.NoButton, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)
    settle(0.2)


def vertex(sk, geo, pos):
    for i in range(1, 40):
        if sk.getGeoVertexIndex(i - 1) == (geo, pos):
            return "Vertex%d" % i
    raise RuntimeError("no vertex for %d/%d" % (geo, pos))


def edit_sketch(geometry, constraints=()):
    """A fresh sketch of the given lines, in edit, seen from the top."""
    import Part

    doc = state["doc"]
    if FreeCADGui.activeDocument().getInEdit():
        FreeCADGui.activeDocument().resetEdit()
        settle(0.2)
    state["n"] += 1
    sk = doc.addObject("Sketcher::SketchObject", "Sketch%d" % state["n"])
    V = FreeCAD.Vector
    for a, b in geometry:
        sk.addGeometry(Part.LineSegment(V(a[0], a[1], 0), V(b[0], b[1], 0)), False)
    for c in constraints:
        sk.addConstraint(c)
    doc.recompute()
    FreeCADGui.activeDocument().setEdit(sk)
    view = FreeCADGui.activeDocument().activeView()
    view.viewTop()
    view.setCameraType("Orthographic")
    view.fitAll()
    # room around the geometry for the pointer to leave it
    cam = view.getCameraNode()
    cam.height.setValue(cam.height.getValue() * 3)
    settle(1.0)
    return sk, view


def preview(sk, base):
    """The types of the distances the tool has in the sketch right now."""
    return [c.Type for c in sk.Constraints[base:] if c.Type in DISTANCES]


def dimension(sk, subs):
    FreeCADGui.Selection.clearSelection()
    for sub in subs:
        FreeCADGui.Selection.addSelection(sk, sub)
    base = len(sk.Constraints)
    FreeCADGui.runCommand("Sketcher_Dimension")
    settle(0.3)
    return base


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "ShowDialogOnDistanceConstraint", False)
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)
        FreeCADGui.getMainWindow().showMaximized()
        state["doc"] = FreeCAD.newDocument("SketchDimensionToolPoints")
        QtCore.QTimer.singleShot(1500, probe)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def probe():
    try:
        import Sketcher

        # A level line: above it, then past its end and above.
        sk, view = edit_sketch([((-20, 0), (20, 0))])
        base = dimension(sk, ["Edge1"])
        move(view, (5, 5.5))
        move(view, (5, 6))
        check("a horizontal line, pointer above it: a horizontal distance",
              preview(sk, base) == ["DistanceX"], preview(sk, base))
        move(view, (30, 5.5))
        move(view, (30, 6))
        check("pointer past its end: still a horizontal distance",
              preview(sk, base) == ["DistanceX"], preview(sk, base))

        sk, view = edit_sketch([((0, -20), (0, 20))])
        base = dimension(sk, ["Edge1"])
        move(view, (5.5, 5))
        move(view, (6, 5))
        check("a vertical line, pointer beside it: a vertical distance",
              preview(sk, base) == ["DistanceY"], preview(sk, base))
        move(view, (5.5, 30))
        move(view, (6, 30))
        check("pointer past its end: still a vertical distance",
              preview(sk, base) == ["DistanceY"], preview(sk, base))

        # The control: a slanted line switches with the pointer.
        sk, view = edit_sketch([((-10, -10), (10, 10))])
        base = dimension(sk, ["Edge1"])
        move(view, (0, 19.5))
        move(view, (0, 20))
        check("a slanted line, pointer above it: a horizontal distance",
              preview(sk, base) == ["DistanceX"], preview(sk, base))
        move(view, (19.5, 20))
        move(view, (20, 20))
        check("pointer off its corner: an aligned distance",
              preview(sk, base) == ["Distance"], preview(sk, base))

        # A corner holds a vertex of each line; with a third point that is
        # two points, not three.
        sk, view = edit_sketch(
            [((0, 0), (10, 0)), ((10, 0), (10, 10)), ((30, 20), (40, 25))],
            [Sketcher.Constraint("Coincident", 0, 2, 1, 1)])
        subs = [vertex(sk, 0, 2), vertex(sk, 1, 1), vertex(sk, 2, 1)]
        base = dimension(sk, subs)
        got = preview(sk, base)
        check("a corner's two vertices and a third point: one distance is offered",
              len(got) == 1, got)

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
