"""An element the sketch already refers to, picked the other way.

A reference is projected, cut by the sketch plane, or both
(SketchObject.ExternalTypes). The External tool used to answer a pick of an
element the sketch already refers to with the geometry it had for it, and
that was all: an edge projected could not be picked for its cut as well.

Claims, with a box and a sketch under it, seen from the side:

  - the External tool takes an upright edge of the box by projection: one
    reference, kind 0;
  - the Intersection tool, the same edge: still one reference, kind 2, one
    geometry more, in one undo step;
  - the same pick again changes nothing;
  - undo takes the cut back and leaves the projection, kind 0.

Not scored against the tree before the change: it had no kinds to read.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchExternalBothWays"
V = FreeCAD.Vector
GENERAL = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"

# The box stands off the sketch's axes: seen from the top, an edge over an
# axis would be a pick of the axis.
UPRIGHT = V(12, 3, 5)     # on the box's upright edge nearest an isometric view


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def mouse(view, kind, pt, button, buttons):
    gv = view.graphicsView()
    vp = gv.viewport()
    x, y = view.getPointOnViewport(pt)
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    ev = QtGui.QMouseEvent(kind, pos, gv.mapToGlobal(pos.toPoint()),
                           button, buttons, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)


def click(view, pt):
    # Twice: the Dimension tool may remake its constraint on a move, and
    # the redraw that follows forgets what was under the pointer until the
    # next move, of which a user's hand sends plenty.
    for _ in range(2):
        mouse(view, QtCore.QEvent.MouseMove, pt, QtCore.Qt.NoButton, QtCore.Qt.NoButton)
        settle(0.4)
    # And once more with nothing in between, before the press and before
    # the release: any redraw forgets it, not only the tool's own, and
    # under load one came due inside those waits -- the click then found
    # nothing under a pointer that had not moved (2 in 45 runs beside the
    # whole suite).
    mouse(view, QtCore.QEvent.MouseMove, pt, QtCore.Qt.NoButton, QtCore.Qt.NoButton)
    mouse(view, QtCore.QEvent.MouseButtonPress, pt, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton)
    settle(0.15)
    mouse(view, QtCore.QEvent.MouseMove, pt, QtCore.Qt.NoButton, QtCore.Qt.LeftButton)
    mouse(view, QtCore.QEvent.MouseButtonRelease, pt, QtCore.Qt.LeftButton, QtCore.Qt.NoButton)
    settle(0.5)


def escape():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if "Quarter" in name or "View3DInventorViewer" in name:
            for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
                QtWidgets.QApplication.sendEvent(
                    w, QtGui.QKeyEvent(kind, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier))
            settle(0.4)
            return True
    return False


def externals(sk):
    return [(o.Name, s) for o, subs in sk.ExternalGeometry for s in subs]


def kinds(sk):
    return list(sk.ExternalTypes)


def run():
    try:
        general = FreeCAD.ParamGet(GENERAL)
        general.SetBool("AdjustCamera", False)
        general.SetBool("RestoreCamera", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        doc.UndoMode = 1
        box = doc.addObject("Part::Box", "Box")
        box.Placement.Base = V(2, 3, 0)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(3, -6, 0), V(11, -5, 0)), False)
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)
        view = gdoc.activeView()
        view.viewTop()
        view.setCameraType("Orthographic")
        view.fitAll()
        cam = view.getCameraNode()
        cam.height.setValue(cam.height.getValue() * 2)
        settle(1.0)
        gdoc.setEdit(sk)
        settle(1.0)
        QtCore.QTimer.singleShot(500, lambda: probe(doc, sk, view))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def probe(doc, sk, view):
    try:
        # Seen from the top an upright edge is its own end; from the side
        # it is a line to click on.
        view.viewIsometric()
        settle(1.0)

        FreeCADGui.runCommand("Sketcher_External")
        settle()
        click(view, UPRIGHT)
        escape()
        FreeCADGui.Selection.clearSelection()
        settle()
        check("the External tool takes the edge by projection",
              len(externals(sk)) == 1 and externals(sk)[0][1].startswith("Edge")
              and kinds(sk) == [0], (externals(sk), kinds(sk)))
        edge = externals(sk)
        geos = len(sk.ExternalGeo)
        undo0 = doc.UndoCount

        FreeCADGui.runCommand("Sketcher_Intersection")
        settle()
        click(view, UPRIGHT)
        check("the Intersection tool, the same edge: one reference of both kinds",
              externals(sk) == edge and kinds(sk) == [2], (externals(sk), kinds(sk)))
        check("with one geometry more", len(sk.ExternalGeo) == geos + 1,
              (geos, len(sk.ExternalGeo)))
        check("in one undo step", doc.UndoCount == undo0 + 1,
              (undo0, doc.UndoCount, doc.UndoNames[:3]))

        click(view, UPRIGHT)
        check("the same pick again changes nothing",
              externals(sk) == edge and kinds(sk) == [2]
              and len(sk.ExternalGeo) == geos + 1 and doc.UndoCount == undo0 + 1,
              (externals(sk), kinds(sk), len(sk.ExternalGeo), doc.UndoCount))
        escape()

        doc.undo()
        settle()
        check("undo takes the cut back and leaves the projection",
              externals(sk) == edge and kinds(sk) == [0] and len(sk.ExternalGeo) == geos,
              (externals(sk), kinds(sk), len(sk.ExternalGeo)))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finish()


def finish():
    try:
        gdoc = FreeCADGui.getDocument(DOC)
        if gdoc.getInEdit():
            gdoc.resetEdit()
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
