"""What a box selection in a sketch edit leaves behind.

Upstream's box-selection family, as it applies to the fork:

  - 9bff63e38d: a line turned into construction geometry in the edit
    (Sketcher_ToggleConstruction, as a user does it) is
    drawn as construction geometry, and stays so after a box selection.
    The box's redraw asked for the solver's copy of the geometry, and the
    solver is brought up to date lazily -- so a construction flag set
    since the last solve drew the line as a normal one again. Not
    observable in the fork, measured: its toggle command solves on its
    way out, construction is a colour read off the object, and the box's
    redraw does not recolour (the mode is still the rubber band's). The
    check guards what the user sees.

The desktop is driven with synthetic mouse events on the viewer, which
reach the sketch's rubber band (tests/gui/sketch-bulk-selection.py does
the same); the curve colours are read from the edit graph's
`CurvesMaterials` node, one diffuse colour per curve.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchBoxSelection"

# The line, and a box corner pair over empty space clear of it and of
# both axes.
L_START = (-20.0, 10.0)
L_END = (-5.0, 18.0)
EMPTY_A = (5.0, -5.0)
EMPTY_B = (15.0, -15.0)
# ViewProviderSketch::CurveDraftColor, the construction colour
DRAFT = (0.0, 0.0, 0.86)

state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.0):
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


def pixel(w, world):
    view = FreeCADGui.getDocument(DOC).ActiveView
    p = view.getPointOnViewport(FreeCAD.Vector(world[0], world[1], 0))
    return (p[0], w.height() - 1 - p[1])


def mouse(w, typ, pos, button, buttons):
    from PySide import QtGui, QtWidgets
    p = QtCore.QPointF(pos[0], pos[1])
    QtWidgets.QApplication.sendEvent(
        w, QtGui.QMouseEvent(typ, p, w.mapToGlobal(p), button, buttons,
                             QtCore.Qt.NoModifier))
    settle()


def box(w, corner_a, corner_b):
    """A left-button box between two sketch points; released."""
    # Past the double-click interval since the last click, or the press
    # can be taken as the second click of a double click.
    settle(0.8)
    a, b = pixel(w, corner_a), pixel(w, corner_b)
    mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    left, none = QtCore.Qt.LeftButton, QtCore.Qt.NoButton
    mouse(w, QtCore.QEvent.MouseMove, a, none, none)
    mouse(w, QtCore.QEvent.MouseButtonPress, a, left, left)
    mouse(w, QtCore.QEvent.MouseMove, mid, none, left)
    mouse(w, QtCore.QEvent.MouseMove, b, none, left)
    mouse(w, QtCore.QEvent.MouseButtonRelease, b, left, none)
    settle(0.3)


def curve_colours():
    mat = coin.SoNode.getByName("CurvesMaterials")
    if not mat:
        return None
    return [tuple(round(c, 2) for c in col.getValue()) for col in mat.diffuseColor.getValues()]


def is_draft(col):
    return col is not None and all(abs(col[i] - DRAFT[i]) < 0.02 for i in range(3))


def run():
    try:
        import Part

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)

        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(L_START[0], L_START[1], 0),
                                        FreeCAD.Vector(L_END[0], L_END[1], 0)), False)
        doc.recompute()

        FreeCADGui.getDocument(DOC).setEdit(sk)
        settle(0.3)
        view = FreeCADGui.getDocument(DOC).ActiveView
        view.viewTop()
        view.fitAll()
        settle(0.3)
        w = viewport()
        if not check("the viewer widget is found", w is not None):
            finish()
            return

        # A: construction toggled in the edit, then a box over nothing.
        FreeCADGui.Selection.addSelection(sk, "Edge1")
        FreeCADGui.runCommand("Sketcher_ToggleConstruction")
        FreeCADGui.Selection.clearSelection()
        settle(0.5)
        cols = curve_colours()
        first = cols[0] if cols else None
        check("the line toggled to construction draws as construction",
              sk.getConstruction(0) and is_draft(first), "%s, colours %s" % (
                  sk.getConstruction(0), cols))
        box(w, EMPTY_A, EMPTY_B)
        cols = curve_colours()
        check("a box selection over nothing leaves it drawn as construction",
              is_draft(cols[0] if cols else None), cols)
        check("and selects nothing", len(FreeCADGui.Selection.getSelectionEx("*")) == 0,
              [(s.ObjectName, s.SubElementNames) for s in FreeCADGui.Selection.getSelectionEx("*")])
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
