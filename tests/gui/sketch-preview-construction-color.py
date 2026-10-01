"""A curve being drawn is coloured as what it will be (upstream 2da7c9ff17
and 566a724c26).

The preview a creation tool draws under the pointer took one colour of
its own, CreateLineColor, whether the tool was about to make normal or
construction geometry. Now it takes the edge colour in normal mode and
the construction colour in construction mode, and toggling the mode with
a tool running recolours the preview there and then, before the pointer
moves again.

Line style is not part of it here: upstream copies the construction
curves' draw style too, but this fork patterns curves by visual layer,
and a new curve goes to layer 0 in either mode, so the preview's solid
line already is what will be drawn.

Measured on the edit node `EditCurvesMaterials` with three distinct
colours set, after a first click and a pointer move with the Line tool:
edge colour in normal mode, the construction colour right after the
toggle with no move in between, still that after a move, and back after
a second toggle. Before the change every reading was CreateLineColor.

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
state = {"done": False}

# Packed 0xRRGGBBAA, as the preference pages store them.
EDGE = 0xFF000000
CONSTRUCTION = 0x00FF0000
CREATE = 0x0000FF00


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


def mouse(view, kind, xy):
    """A pointer event at the model point xy on the view's GL widget."""
    gv = view.graphicsView()
    vp = gv.viewport()
    x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    button = QtCore.Qt.NoButton if kind == QtCore.QEvent.MouseMove else QtCore.Qt.LeftButton
    buttons = QtCore.Qt.LeftButton if kind == QtCore.QEvent.MouseButtonPress else QtCore.Qt.NoButton
    ev = QtGui.QMouseEvent(kind, pos, gv.mapToGlobal(pos.toPoint()),
                           button, buttons, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)
    settle(0.1)


def name_of(rgb):
    """The packed colour of an SbColor, matched to the three set here."""
    r, g, b = (int(round(c * 255)) for c in rgb)
    packed = (r << 24) | (g << 16) | (b << 8)
    return {EDGE: "edge", CONSTRUCTION: "construction", CREATE: "create"}.get(
        packed, "#%02x%02x%02x" % (r, g, b))


def preview_colours():
    """The set of colours on the preview's material, by name."""
    from pivy import coin

    view = state["view"]
    graph = view.getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setName("EditCurvesMaterials")
    search.setInterest(coin.SoSearchAction.FIRST)
    search.setSearchingAll(True)
    search.apply(graph)
    path = search.getPath()
    if path is None:
        return None
    field = path.getTail().diffuseColor
    return sorted({name_of(field[i].getValue()) for i in range(field.getNum())})


def run():
    try:
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        params.SetUnsigned("EditedEdgeColor", EDGE)
        params.SetUnsigned("ConstructionColor", CONSTRUCTION)
        params.SetUnsigned("CreateLineColor", CREATE)
        # The pointer alone places the points: no on-view parameter to
        # take the click first.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument("SketchPreviewConstructionColor")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.ViewObject.Autoconstraints = False
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sk)
        state["doc"] = doc
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].viewTop()
        state["view"].setCameraType("Orthographic")
        QtCore.QTimer.singleShot(2000, probe)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def probe():
    try:
        view = state["view"]
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.runCommand("Sketcher_CreateLine")
        settle(0.3)
        mouse(view, QtCore.QEvent.MouseMove, (-3, 2))
        mouse(view, QtCore.QEvent.MouseButtonPress, (-3, 2))
        mouse(view, QtCore.QEvent.MouseButtonRelease, (-3, 2))
        mouse(view, QtCore.QEvent.MouseMove, (4, 3))
        normal = preview_colours()
        check("in normal mode the preview takes the edge colour",
              normal == ["edge"], normal)

        FreeCADGui.runCommand("Sketcher_ToggleConstruction")
        settle(0.2)
        toggled = preview_colours()
        check("toggling construction recolours it before the pointer moves",
              toggled == ["construction"], toggled)

        mouse(view, QtCore.QEvent.MouseMove, (5, 1))
        moved = preview_colours()
        check("and a move keeps the construction colour",
              moved == ["construction"], moved)

        FreeCADGui.runCommand("Sketcher_ToggleConstruction")
        settle(0.2)
        back = preview_colours()
        check("toggling back gives the edge colour again", back == ["edge"], back)

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
