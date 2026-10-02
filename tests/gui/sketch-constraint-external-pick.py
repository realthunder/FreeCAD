"""Outside picking stacked on the constraint tools (the fork's form of
upstream 999fed9c4e).

While the Dimension tool or a constraint command runs, the external
geometry commands do not replace it: Sketcher_External, Sketcher_Defining
and the two intersection commands switch outside picking on for the
running tool, in their own flavour, and off again on a second press. With
it on, an edge or a vertex picked outside the sketch becomes external
geometry and the tool goes on with it. The state is one setting for all of
these tools and it stays from one tool to the next. The tool's cursor says
so: the icon of the command that switched it on, above the tool's own.

A box, and a sketch on the XY plane with two lines beside the box, seen
from the top. The sketch has a circle too. Claims:

  Parallel (a constraint command):
  - with outside picking off, a click on the box's edge does nothing;
  - Sketcher_External pressed while the tool runs leaves the tool running
    and sets the setting;
  - the sketch's line, then the box's edge: one external geometry, one
    Parallel between the line and it, ONE undo step for both, and undoing
    it takes both;
  - the box's edge first and then Escape: nothing stays in the sketch and
    no undo step is left;
  - pressed again, outside picking is off and the setting says so;
  - the cursor carries the tool's icon alone while it is off, the External
    command's icon too once that is pressed, and loses it on the next press.

  Symmetric (a sequence of three):
  - the line's end, then the box's edge: the end is still selected, so
    still drawn as picked, although the view cleared the selection to
    select the edge.

  What a step can take:
  - Parallel, the line and then the box's corner: a vertex is not offered
    where an edge is asked for, nothing is made;
  - Coincident, the line's end and the box's corner: an external point and
    the constraint, one undo step;
  - Parallel, two of the box's edges: the command refuses a constraint
    between two fixed things, and neither reference stays.

  Dimension:
  - the setting left on reaches the Dimension tool started afterwards;
  - the box's edge, the sketch's line, then a click on empty space: the
    external geometry and a dimension to it, in one undo step;
  - Sketcher_Defining pressed while it runs switches the flavour, and the
    edge picked next is defining external geometry;
  - its cursor starts with the External sign, and the sign changes with
    the flavour;
  - two lines of the sketch picked one after the other are both still
    selected (the tool starts its transaction over at each pick, and the
    sketch clears the selection when one is aborted);
  - the sketch's line picked before an outside edge is still selected
    after it;
  - a circle and a click on empty space finish its dimension although the
    dimension's label, which follows the pointer, is under the click;
  - the box's face and a click on empty space: the pieces of the face go
    again, with nothing dimensioned there is nothing to keep.

  Intersection (seen from the side):
  - Coincident, the line and an upright edge of the box with
    Sketcher_Intersection on: the edge is ONE point in the sketch, where it
    meets the sketch plane, and that point is put on the line. It was two
    points at one place, the projection and the cut, and the tool, given
    several pieces, made nothing.

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
END = V(3, -6, 0)         # the line's first end
LINE2 = V(7, -9.25, 0)    # on the sketch's second line
CIRCLE = V(-6, -6, 0)     # on the sketch's circle
CORNER = V(2, 3, 10)      # a corner of the box
BACK = V(7, 13, 10)       # on the box's top back edge
TOP = V(7, 8, 10)         # on the box's top face
FRONT = V(7, 3, 10)       # on the box's top front edge (y = 3)
RIGHT = V(12, 8, 10)      # on the box's top right edge (x = 12)
UPRIGHT = V(12, 3, 5)     # on the box's upright edge nearest an isometric view
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


def mode():
    return FreeCAD.ParamGet(GENERAL).GetInt("ConstraintExternalPick", 0)


def cursor_quarters(view):
    """The view's cursor: opaque pixels of the tool's quarter (lower right)
    and of the sign's (upper right), and the sign's pixels themselves."""
    img = view.graphicsView().viewport().cursor().pixmap().toImage()
    cursor_quarters.count += 1
    img.save(os.path.join(OUT, "cursor-%d.png" % cursor_quarters.count))
    w, h = img.width(), img.height()
    if not w or not h:
        return 0, 0, ()
    tool = sum(1 for y in range(h // 2, h) for x in range(w // 2, w)
               if QtGui.qAlpha(img.pixel(x, y)))
    sign = tuple(img.pixel(x, y) for y in range(h // 2) for x in range(w // 2, w))
    return tool, sum(1 for p in sign if QtGui.qAlpha(p)), sign


cursor_quarters.count = 0


def externals(sk):
    return [(o.Name, s) for o, subs in sk.ExternalGeometry for s in subs]


def selected(sk):
    return [s for o in FreeCADGui.Selection.getSelectionEx()
            if o.Object == sk for s in o.SubElementNames]


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
        sk.addGeometry(Part.LineSegment(V(3, -9, 0), V(11, -9.5, 0)), False)
        sk.addGeometry(Part.Circle(V(-8, -6, 0), V(0, 0, 1), 2), False)
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
        tool, sign, _ = cursor_quarters(view)
        check("off: the cursor carries the tool's icon and no sign",
              tool > 0 and sign == 0, (tool, sign))
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
        tool, sign, external_sign = cursor_quarters(view)
        check("and the running tool's cursor gains its sign",
              tool > 0 and sign > 0, (tool, sign))
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
        check("and the sign is gone from the cursor", cursor_quarters(view)[1] == 0,
              cursor_quarters(view)[:2])
        click(view, LINE)
        click(view, FRONT)
        check("and a click on the box's edge does nothing again",
              state_of(sk) == clean, state_of(sk))
        FreeCADGui.runCommand("Sketcher_External")
        settle()
        check("on once more, for the next tool", mode() == 1, mode())
        escape()

        # -- an earlier pick stays drawn as picked ---------------------
        FreeCADGui.runCommand("Sketcher_ConstrainSymmetric")
        settle()
        click(view, END)
        check("symmetric: the line's end is selected", selected(sk) == ["Vertex1"],
              selected(sk))
        click(view, FRONT)
        check("and the box's edge is taken", state_of(sk)[0] == 1, state_of(sk))
        check("with the line's end still selected", "Vertex1" in selected(sk),
              selected(sk))
        escape()
        check("left: nothing stays", state_of(sk) == clean, state_of(sk))

        # -- what a step can take --------------------------------------
        FreeCADGui.runCommand("Sketcher_ConstrainParallel")
        settle()
        undo0 = doc.UndoCount
        click(view, LINE)
        click(view, CORNER)
        check("parallel: the box's corner is not taken where an edge is asked for",
              state_of(sk) == clean and doc.UndoCount == undo0,
              (state_of(sk), undo0, doc.UndoCount))
        escape()

        FreeCADGui.runCommand("Sketcher_ConstrainCoincidentUnified")
        settle()
        undo0 = doc.UndoCount
        click(view, END)
        click(view, CORNER)
        st = state_of(sk)
        check("coincident: the line's end on the box's corner, an external point",
              st[0] == 1 and externals(sk)[0][1].startswith("Vertex")
              and st[1] in ([("Coincident", 0, -3)], [("Coincident", -3, 0)]),
              (st, externals(sk)))
        check("in one undo step", doc.UndoCount == undo0 + 1,
              (undo0, doc.UndoCount, doc.UndoNames[:3]))
        doc.undo()
        settle()
        check("undoing it takes both", state_of(sk) == clean, state_of(sk))
        escape()

        FreeCADGui.runCommand("Sketcher_ConstrainParallel")
        settle()
        undo0 = doc.UndoCount
        click(view, FRONT)
        check("parallel: the box's edge first is taken", state_of(sk)[0] == 1, state_of(sk))
        click(view, BACK)
        check("two of the box's edges: refused, and neither reference stays",
              state_of(sk) == clean and doc.UndoCount == undo0,
              (state_of(sk), undo0, doc.UndoCount, doc.UndoNames[:3]))
        escape()

        # -- the Dimension tool ---------------------------------------
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.runCommand("Sketcher_Dimension")
        settle()
        undo0 = doc.UndoCount
        tool, sign, dim_sign = cursor_quarters(view)
        check("the Dimension tool starts with the External sign on its cursor",
              tool > 0 and sign > 0 and dim_sign == external_sign, (tool, sign))
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

        # Nothing of outside picking: the tool starts its transaction
        # over at each pick, and the sketch clears the selection on an
        # abort, so the first of two picks was no longer drawn as picked.
        click(view, LINE)
        click(view, LINE2)
        check("dimension: two lines of the sketch picked, an angle between them",
              [t for t, _f, _s in state_of(sk)[1]] == ["Angle"], state_of(sk))
        check("and both are still selected", sorted(selected(sk)) == ["Edge1", "Edge2"],
              selected(sk))
        escape()
        # The diameter's label follows the pointer, so the sketch has
        # something of its own under the click that ends the dimension.
        undo0 = doc.UndoCount
        click(view, CIRCLE)
        note("dimension, after the circle: %s %s" % (selected(sk), state_of(sk)))
        click(view, EMPTY)
        st = state_of(sk)
        check("dimension: a circle and a click on empty space finish its dimension",
              selected(sk) == [] and [t for t, _f, _s in st[1]] == ["Diameter"]
              and doc.UndoCount == undo0 + 1,
              (selected(sk), st, undo0, doc.UndoCount))
        doc.undo()
        settle()

        undo0 = doc.UndoCount
        click(view, TOP)
        check("dimension: the box's face puts its outline in the sketch",
              state_of(sk)[0] == 1 and len(sk.ExternalGeo) == 6,
              (state_of(sk), len(sk.ExternalGeo)))
        click(view, EMPTY)
        check("nothing dimensioned: the outline goes again and no undo step is left",
              state_of(sk) == clean and doc.UndoCount == undo0,
              (state_of(sk), undo0, doc.UndoCount, doc.UndoNames[:3]))

        click(view, LINE)
        check("dimension: the line is selected", selected(sk) == ["Edge1"], selected(sk))
        FreeCADGui.runCommand("Sketcher_Defining")
        settle()
        check("Sketcher_Defining switches the flavour", mode() == 2, mode())
        tool, sign, defining_sign = cursor_quarters(view)
        check("and the sign on the cursor is another",
              sign > 0 and defining_sign != external_sign, (tool, sign))
        click(view, RIGHT)
        st = state_of(sk)
        note("dimension, defining: %s" % (st,))
        defining = None
        if st[0] == 1:
            import Sketcher
            defining = Sketcher.ExternalGeometryFacade(sk.ExternalGeo[-1]).testFlag("Defining")
        check("the edge picked next is defining external geometry",
              st[0] == 1 and defining is True, (st, defining))
        check("with the line still selected", "Edge1" in selected(sk), selected(sk))
        escape()
        escape()
        check("left: nothing stays", state_of(sk) == clean, state_of(sk))

        # -- an edge taken by intersection ------------------------------
        # Seen from the top an upright edge is its own end; from the side
        # it is a line to click on.
        view.viewIsometric()
        settle(1.0)
        FreeCADGui.runCommand("Sketcher_ConstrainCoincidentUnified")
        settle()
        FreeCADGui.runCommand("Sketcher_Intersection")
        settle()
        undo0 = doc.UndoCount
        geos0 = len(sk.ExternalGeo)
        click(view, LINE)
        click(view, UPRIGHT)
        st = state_of(sk)
        added = [type(g).__name__ for g in sk.ExternalGeo[geos0:]]
        check("intersection: an upright edge of the box is one point in the sketch",
              st[0] == 1 and externals(sk)[0][1].startswith("Edge") and added == ["Point"],
              (st, externals(sk), added))
        check("and it is put on the line", st[1] == [("PointOnObject", -3, 0)], st)
        check("in one undo step", doc.UndoCount == undo0 + 1,
              (undo0, doc.UndoCount, doc.UndoNames[:3]))
        doc.undo()
        settle()
        check("undoing it takes both", state_of(sk) == clean, state_of(sk))
        escape()
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
