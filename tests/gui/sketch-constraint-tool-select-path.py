"""The constraint tools select on the path of the sketch being edited
(upstream 0c34c93fe4: not applicable here, and this is the evidence).

A sketch edited inside a container is one OCCURRENCE of the sketch: the
selection that says so names the container and the path down to the
sketch, "Part" + "Sketch.Edge1". That is how the sketch selects what is
clicked and how its panels do (ViewProviderSketch::selectElement). The
Dimension tool and the constraint commands call the selection with the
bare sketch, "Sketch" + "Edge1"; upstream changed its tools to go through
the view provider for that reason.

Here the selection itself puts an object on its top parent's path
(SelectionSingleton::checkTopParent, for every add, removal and query), so
both ways end as the same entry. Had they not, the selection would hold
two kinds of entries for one sketch, and an element selected one way
would not be unselected the other.

A sketch with two lines inside an App::Part that stands off the origin,
edited through the Part. Claims, each read from the selection unresolved
(top object and path, the element by its plain name):

  - no tool: a click on a line selects it through the Part (the baseline);
  - the Dimension tool started on that selection, and the second line
    picked: both are selected through the Part, and nothing by the bare
    sketch -- after the tool started its transaction over, which is where
    it selects its picks again;
  - the second line clicked again is unselected, the first stays;
  - a constraint command (Parallel): the line picked is selected through
    the Part.
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
DOC = "ConstraintToolSelectPath"
V = FreeCAD.Vector
GENERAL = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"

# The Part the sketch is in stands off the origin, so a point of the sketch
# is not where its own numbers say.
OFF = V(1, 2, 0)
LINE = V(7, -5.5, 0) + OFF     # on the sketch's first line
LINE2 = V(7, -9.25, 0) + OFF   # on its second
EMPTY = V(-8, -10, 0) + OFF


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


def entries():
    """The selection as it is held: (top object, path + element) pairs.

    The element by its plain name: a mapped name is dropped, and the
    sketch's own click names an edge of its edit geometry "edge1"."""
    res = []
    for o in FreeCADGui.Selection.getSelectionEx("", 0):
        for sub in (o.SubElementNames or ("",)):
            parts = sub.split(".")
            element = parts[-1][:1].upper() + parts[-1][1:]
            path = [p for p in parts[:-1] if not p.startswith(";")]
            res.append((o.Object.Name, ".".join(path + [element])))
    return sorted(res)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "ShowDialogOnDistanceConstraint", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
            "OnViewParameterVisibility", 0)
        general = FreeCAD.ParamGet(GENERAL)
        general.SetBool("AdjustCamera", False)
        general.SetBool("RestoreCamera", False)
        general.SetInt("ConstraintExternalPick", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        part = doc.addObject("App::Part", "Part")
        part.Placement.Base = OFF
        sk = part.newObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(3, -6, 0), V(11, -5, 0)), False)
        sk.addGeometry(Part.LineSegment(V(3, -9, 0), V(11, -9.5, 0)), False)
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)
        view = gdoc.activeView()
        view.viewTop()
        view.setCameraType("Orthographic")
        view.fitAll()
        cam = view.getCameraNode()
        cam.height.setValue(cam.height.getValue() * 2)
        settle(1.0)
        gdoc.setEdit(part, 0, "Sketch.")
        settle(1.0)
        QtCore.QTimer.singleShot(500, lambda: probe(doc, sk, view))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def probe(doc, sk, view):
    try:
        gdoc = FreeCADGui.getDocument(DOC)
        check("the sketch is edited through the Part",
              gdoc.getInEdit() is not None and gdoc.getInEdit().Object == sk
              and [o.Name for o in sk.InList] == ["Part"],
              (gdoc.getInEdit() and gdoc.getInEdit().Object.Name,))
        e1 = ("Part", "Sketch.Edge1")
        e2 = ("Part", "Sketch.Edge2")

        click(view, LINE)
        check("no tool: a click selects the line through the Part", entries() == [e1],
              entries())

        FreeCADGui.runCommand("Sketcher_Dimension")
        settle()
        click(view, LINE2)
        check("dimension: an angle between the two lines",
              [c.Type for c in sk.Constraints] == ["Angle"],
              [c.Type for c in sk.Constraints])
        check("both lines are selected through the Part, nothing by the bare sketch",
              entries() == [e1, e2], entries())
        click(view, LINE2)
        check("the second line clicked again is unselected, the first stays",
              entries() == [e1], entries())
        escape()
        escape()
        check("left: nothing made", len(sk.Constraints) == 0, len(sk.Constraints))

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.runCommand("Sketcher_ConstrainParallel")
        settle()
        click(view, LINE)
        check("parallel: the line picked is selected through the Part", entries() == [e1],
              entries())
        click(view, LINE2)
        check("and the constraint is made", [c.Type for c in sk.Constraints] == ["Parallel"],
              [c.Type for c in sk.Constraints])
        escape()
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
