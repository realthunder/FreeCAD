"""Outside picking stacked on the constraint tools (the fork's form of
upstream 999fed9c4e).

While the Dimension tool or a constraint command runs, the external
geometry commands do not replace it: Sketcher_External, Sketcher_Defining
and the two intersection commands switch outside picking on for the
running tool, in their own flavour, and off again on a second press. With
it on, an edge or a vertex picked outside the sketch becomes external
geometry and the tool goes on with it. The state is one setting for all of
these tools and it stays from one tool to the next.

A box, and a sketch on the XY plane with a line beside the box, seen from
the top. Claims:

  Parallel (a constraint command):
  - with outside picking off, a click on the box's edge does nothing;
  - Sketcher_External pressed while the tool runs leaves the tool running
    and sets the setting;
  - the sketch's line, then the box's edge: one external geometry, one
    Parallel between the line and it, ONE undo step for both, and undoing
    it takes both;
  - the box's edge first and then Escape: nothing stays in the sketch and
    no undo step is left;
  - pressed again, outside picking is off and the setting says so.

  Dimension:
  - the setting left on reaches the Dimension tool started afterwards;
  - the box's edge, the sketch's line, then a click on empty space: the
    external geometry and a dimension to it, in one undo step;
  - Sketcher_Defining pressed while it runs switches the flavour, and the
    edge picked next is defining external geometry.

Scored against the tree before the change: the External command replaced
the running tool, and nothing outside the sketch could be picked in one.
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
DOC = "ConstraintExternalPick"
V = FreeCAD.Vector
GENERAL = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"

# The box stands off the sketch's axes: seen from the top, an edge over an
# axis would be a pick of the axis.
LINE = V(7, -5.5, 0)      # on the sketch's line, beside the box
FRONT = V(7, 3, 10)       # on the box's top front edge (y = 3)
RIGHT = V(12, 8, 10)      # on the box's top right edge (x = 12)
EMPTY = V(-8, -10, 0)


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
    mouse(view, QtCore.QEvent.MouseButtonPress, pt, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton)
    settle(0.15)
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


def mode():
    return FreeCAD.ParamGet(GENERAL).GetInt("ConstraintExternalPick", 0)


def externals(sk):
    return [(o.Name, s) for o, subs in sk.ExternalGeometry for s in subs]


def state_of(sk):
    return (len(externals(sk)), [(c.Type, c.First, c.Second) for c in sk.Constraints])


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
        gdoc = FreeCADGui.getDocument(DOC)
        clean = state_of(sk)

        # -- a constraint command -------------------------------------
        FreeCADGui.runCommand("Sketcher_ConstrainParallel")
        settle()
        click(view, LINE)
        click(view, FRONT)
        check("off: a click on the box's edge does nothing", state_of(sk) == clean,
              state_of(sk))
        escape()

        FreeCADGui.runCommand("Sketcher_ConstrainParallel")
        settle()
        FreeCADGui.runCommand("Sketcher_External")
        settle()
        check("the External command sets the setting", mode() == 1, mode())
        undo0 = doc.UndoCount
        click(view, LINE)
        click(view, FRONT)
        st = state_of(sk)
        note("after line + box edge: %s, undo %d -> %d, names %s" % (
            st, undo0, doc.UndoCount, doc.UndoNames[:3]))
        check("the tool was not replaced: one external geometry of the box",
              st[0] == 1 and externals(sk)[0][0] == "Box", externals(sk))
        check("a Parallel between the line and it",
              st[1] == [("Parallel", 0, -3)] or st[1] == [("Parallel", -3, 0)], st[1])
        check("one undo step for both", doc.UndoCount == undo0 + 1,
              (undo0, doc.UndoCount, doc.UndoNames[:3]))
        doc.undo()
        settle()
        check("undoing it takes both", state_of(sk) == clean, state_of(sk))

        # the tool is still running (continuous mode): the outside edge
        # first, then leave
        undo0 = doc.UndoCount
        click(view, FRONT)
        note("after the box edge alone: %s" % (state_of(sk),))
        check("the box's edge alone is in the sketch while the tool waits",
              state_of(sk)[0] == 1, state_of(sk))
        escape()
        check("left before the constraint: nothing stays", state_of(sk) == clean,
              state_of(sk))
        check("and no undo step is left", doc.UndoCount == undo0,
              (undo0, doc.UndoCount, doc.UndoNames[:3]))

        FreeCADGui.runCommand("Sketcher_ConstrainParallel")
        settle()
        FreeCADGui.runCommand("Sketcher_External")
        settle()
        check("pressed again: the setting is off", mode() == 0, mode())
        click(view, LINE)
        click(view, FRONT)
        check("and a click on the box's edge does nothing again",
              state_of(sk) == clean, state_of(sk))
        FreeCADGui.runCommand("Sketcher_External")
        settle()
        check("on once more, for the next tool", mode() == 1, mode())
        escape()

        # -- the Dimension tool ---------------------------------------
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.runCommand("Sketcher_Dimension")
        settle()
        undo0 = doc.UndoCount
        click(view, FRONT)
        note("dimension, after the box edge: %s" % (state_of(sk),))
        check("the setting reached the Dimension tool: the box's edge is taken",
              state_of(sk)[0] == 1, state_of(sk))
        click(view, LINE)
        click(view, EMPTY)
        st = state_of(sk)
        note("dimension finished: %s, undo %d -> %d, names %s" % (
            st, undo0, doc.UndoCount, doc.UndoNames[:3]))
        check("a dimension to the external geometry",
              st[0] == 1 and len(st[1]) >= 1
              and any(-3 in (f, s) for _t, f, s in st[1]), st)
        check("in one undo step", doc.UndoCount == undo0 + 1,
              (undo0, doc.UndoCount, doc.UndoNames[:3]))
        doc.undo()
        settle()
        check("undoing it takes the geometry and the dimension",
              state_of(sk) == clean, state_of(sk))

        FreeCADGui.runCommand("Sketcher_Defining")
        settle()
        check("Sketcher_Defining switches the flavour", mode() == 2, mode())
        click(view, RIGHT)
        st = state_of(sk)
        note("dimension, defining: %s" % (st,))
        defining = None
        if st[0] == 1:
            import Sketcher
            defining = Sketcher.ExternalGeometryFacade(sk.ExternalGeo[-1]).testFlag("Defining")
        check("the edge picked next is defining external geometry",
              st[0] == 1 and defining is True, (st, defining))
        escape()
        escape()
        check("left: nothing stays", state_of(sk) == clean, state_of(sk))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finish()


def finish():
    try:
        FreeCAD.ParamGet(GENERAL).SetInt("ConstraintExternalPick", 0)
        gdoc = FreeCADGui.getDocument(DOC)
        if gdoc.getInEdit():
            gdoc.resetEdit()
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
