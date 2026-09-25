"""Undo while a sketch drag is still held (upstream 16aff10544).

A drag remembers what it is dragging by index: `Dragged` holds GeoIds
and `DragConstraintSet` constraint ids, taken when the button went down
and used on every move after it and on the release. An undo that lands
in the middle -- Ctrl+Z with the mouse button still held -- can take
away the very element those indices name, and the drag then carries on
with them.

Two drags on the desktop view, each started from a preselection the
test sets through `Gui.Selection` -- the way the elements and constraint
lists preselect on hover -- because synthetic mouse moves preselect
nothing, while synthetic presses, moves and releases do reach the edit
(tests/gui/sketch-bulk-selection.py drags a box the same way):

  - a line's start point, undone until the line itself is gone, then
    moved and released;
  - a Distance constraint's label, undone until the constraint itself is
    gone, then moved and released. The first undo takes back the drag's
    own "Drag Constraint" transaction; the rest walk back to the
    constraint.

What is asserted, for each: the drag had hold of its element before the
undo (the drawn point, or the label distance, followed the pointer), the
undo removed that element, and the move and the release after it change
nothing -- the undo cancelled the drag rather than leaving it to carry
on with indices that now name nothing. Reaching the end at all is the
crash check: the constraint drag read its id out of the constraint list
unchecked, and before the fix the first move after the undo was a
SIGSEGV in moveConstraint(). Between the two, the line is redone and
dragged whole, so the cancelled drag is shown to leave the edit able to
drag again.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchUndoDrag"

# Line L (GeoId 0) carries the Distance constraint (Constraint1); the
# label is moved by its distance from the line, LabelDistance.
L_START = (-24.0, 4.0)
L_END = (-4.0, 4.0)
LABEL_FROM = (-14.0, 14.0)
LABEL_TO = (-14.0, 20.0)
LABEL_AFTER = (-14.0, 24.0)
# Line B (GeoId 1), added last in a transaction of its own, so a single
# undo removes it. Its start point is Vertex3.
B_START = (14.0, -14.0)
B_END = (24.0, -18.0)
B_TO = (20.0, -8.0)
B_AFTER = (26.0, -4.0)

state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def viewport():
    from PySide import QtWidgets
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if ("Quarter" in name or "View3DInventorViewer" in name) and w.isVisible():
            return w
    return None


def settle(seconds=0.0):
    import time
    end = time.monotonic() + seconds
    while True:
        QtCore.QCoreApplication.processEvents()
        if time.monotonic() >= end:
            break
        time.sleep(0.01)


def mouse(w, typ, world, button, buttons):
    from PySide import QtGui, QtWidgets
    view = FreeCADGui.getDocument(DOC).ActiveView
    p = view.getPointOnViewport(FreeCAD.Vector(world[0], world[1], 0))
    p = QtCore.QPointF(p[0], w.height() - 1 - p[1])
    QtWidgets.QApplication.sendEvent(
        w, QtGui.QMouseEvent(typ, p, w.mapToGlobal(p), button, buttons,
                             QtCore.Qt.NoModifier))
    settle()


def press(w, world):
    left = QtCore.Qt.LeftButton
    mouse(w, QtCore.QEvent.MouseButtonPress, world, left, left)


def drag_to(w, world):
    mouse(w, QtCore.QEvent.MouseMove, world, QtCore.Qt.NoButton, QtCore.Qt.LeftButton)


def release(w, world):
    mouse(w, QtCore.QEvent.MouseButtonRelease, world, QtCore.Qt.LeftButton,
          QtCore.Qt.NoButton)
    # A drag that lands recomputes and redraws off a timer, and that
    # redraw drops a preselection set before it.
    settle(0.5)


def drawn_near(world, tol=0.35):
    """Whether the edit scene draws a point at world. A drag moves the
    solver's copy and redraws, and writes the property only on release,
    so the drawing is where a drag in progress shows."""
    from pivy import coin
    view = FreeCADGui.getDocument(DOC).ActiveView
    sa = coin.SoSearchAction()
    sa.setType(coin.SoCoordinate3.getClassTypeId())
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(view.getAuxSceneGraph())
    paths = sa.getPaths()
    for i in range(paths.getLength()):
        pts = paths[i].getTail().point
        for j in range(pts.getNum()):
            v = pts[j].getValue()
            if abs(v[0] - world[0]) <= tol and abs(v[1] - world[1]) <= tol:
                return True
    return False


def snapshot(sk):
    return {"geo": [(round(g.StartPoint.x, 4), round(g.StartPoint.y, 4),
                     round(g.EndPoint.x, 4), round(g.EndPoint.y, 4))
                    for g in sk.Geometry],
            "cst": [(c.Type, c.First, round(c.LabelDistance, 3),
                     round(c.LabelPosition, 3)) for c in sk.Constraints]}


def undo_until(doc, gone):
    """Undo until gone() holds, at most five steps; the steps taken."""
    steps = 0
    while not gone() and doc.UndoCount > 0 and steps < 5:
        doc.undo()
        settle()
        steps += 1
    return steps


def run():
    try:
        import Part
        import Sketcher

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        # The drag has to land where the pointer says.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Snap").SetBool(
                "Snap", False)
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)

        doc = FreeCAD.newDocument(DOC)
        doc.UndoMode = 1
        doc.openTransaction("sketch")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(L_START[0], L_START[1], 0),
                                        FreeCAD.Vector(L_END[0], L_END[1], 0)), False)
        doc.commitTransaction()
        doc.openTransaction("distance")
        sk.addConstraint(Sketcher.Constraint("Distance", 0, 20.0))
        doc.commitTransaction()
        doc.openTransaction("line B")
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(B_START[0], B_START[1], 0),
                                        FreeCAD.Vector(B_END[0], B_END[1], 0)), False)
        doc.commitTransaction()
        doc.recompute()
        check("the sketch has two lines and one constraint",
              len(sk.Geometry) == 2 and len(sk.Constraints) == 1,
              "%d geo, %d cst" % (len(sk.Geometry), len(sk.Constraints)))
        check("undo is on, three steps deep", doc.UndoCount >= 3, doc.UndoCount)

        FreeCADGui.getDocument(DOC).setEdit(sk)
        settle()
        view = FreeCADGui.getDocument(DOC).ActiveView
        view.viewTop()
        view.fitAll()
        settle()
        w = viewport()
        if not check("the viewer widget is found", w is not None):
            finish()
            return

        # The point drag. The first move after the press starts the drag
        # and is consumed doing so; the second one moves the point.
        FreeCADGui.Selection.setPreselection(sk, "Vertex3")
        press(w, B_START)
        drag_to(w, ((B_START[0] + B_TO[0]) / 2, (B_START[1] + B_TO[1]) / 2))
        drag_to(w, B_TO)
        check("the point drag had hold of B's start before the undo",
              drawn_near(B_TO), "drawn at %s?" % (B_TO,))
        steps = undo_until(doc, lambda: len(sk.Geometry) < 2)
        undone = snapshot(sk)
        check("one undo removed line B", len(undone["geo"]) == 1 and steps == 1,
              "%d steps, %s" % (steps, undone))
        drag_to(w, B_AFTER)
        release(w, B_AFTER)
        after = snapshot(sk)
        check("the move and release after the undo changed nothing",
              after == undone, "%s -> %s" % (undone, after))

        # The edit still works after a cancelled drag: B back, and a whole
        # drag of its start point lands.
        doc.redo()
        settle()
        FreeCADGui.Selection.setPreselection(sk, "Vertex3")
        press(w, B_START)
        drag_to(w, ((B_START[0] + B_TO[0]) / 2, (B_START[1] + B_TO[1]) / 2))
        drag_to(w, B_TO)
        release(w, B_TO)
        p = sk.getPoint(1, 1) if len(sk.Geometry) > 1 else None
        check("after the cancelled drag, a whole drag still moves a point",
              p is not None and abs(p.x - B_TO[0]) < 0.35 and abs(p.y - B_TO[1]) < 0.35,
              p and "(%.3f, %.3f)" % (p.x, p.y))

        # The label drag, the same way. Undone back past B's drag and B.
        FreeCADGui.Selection.setPreselection(sk, "Constraint1")
        press(w, LABEL_FROM)
        drag_to(w, ((LABEL_FROM[0] + LABEL_TO[0]) / 2, (LABEL_FROM[1] + LABEL_TO[1]) / 2))
        drag_to(w, LABEL_TO)
        held = round(sk.Constraints[0].LabelDistance, 3) if sk.Constraints else None
        want = LABEL_TO[1] - L_START[1]
        check("the label drag had hold of the label before the undo",
              held is not None and abs(held - want) < 1.0, "%s vs %s" % (held, want))
        steps = undo_until(doc, lambda: len(sk.Constraints) == 0)
        undone = snapshot(sk)
        check("undo removed the constraint", len(undone["cst"]) == 0,
              "%d steps, %s" % (steps, undone))
        drag_to(w, LABEL_AFTER)
        release(w, LABEL_AFTER)
        after = snapshot(sk)
        check("the move and release after that undo changed nothing",
              after == undone, "%s -> %s" % (undone, after))

    except Exception:
        note("ABORT run:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.getDocument(DOC).resetEdit()
    except Exception:
        pass
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
