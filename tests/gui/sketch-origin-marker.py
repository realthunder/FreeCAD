"""The origin is drawn as an outline while a drawing tool is active.

Upstream's 16908241f0 flips the origin's marker from CIRCLE_FILLED to
CIRCLE_LINE for as long as a tool is running, so that the origin reads as
somewhere to snap to rather than as another vertex of the sketch, and puts
it back when the tool goes away. Upstream can do that by naming a scene
node of its own -- EditModeCoinManager keeps OriginPointSet apart from the
rest -- and this fork cannot: the origin here is point 0 of the same
SoMarkerSet as every other vertex.

So the adaptation gives that one marker set a value per point instead of
the single value standing for all of them, which is the same thing a
sketch holding a group already does. What this test pins is the part that
could silently do nothing: that point 0's marker actually differs from the
others while a tool is up, and that it is the same as the others again
afterwards. A one-value field reads as "every point the same", so both
readings are taken from the field itself.

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
DOC = "SketchOriginMarker"
OBJ = "Sketch"

state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def pump(turns=30):
    for _ in range(turns):
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def point_markers():
    """The edit scene's vertex marker set, read as one value per point.

    The nodes live under the viewer's auxiliary scene graph once edit mode
    has hoisted them there, not under the view provider's own root. A field
    holding a single value means every point wears it, so it is expanded
    here rather than compared short.
    """
    from pivy import coin

    view = FreeCADGui.ActiveDocument.ActiveView
    search = coin.SoSearchAction()
    search.setName(coin.SbName("PointSet"))
    search.setInterest(coin.SoSearchAction.FIRST)
    search.apply(view.getAuxSceneGraph())
    path = search.getPath()
    if path is None:
        return None
    node = path.getTail()
    return [node.markerIndex[i] for i in range(node.markerIndex.getNum())]


def send_escape():
    """Escape to the 3D view, as a key event rather than a command.

    Synthetic keyboard events do reach Coin here (synthetic mouse events
    are the ones that preselect nothing), so this is how a tool is asked
    to end without tearing edit mode down with it.
    """
    mw = FreeCADGui.getMainWindow()
    seen = []
    for w in mw.findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        seen.append(name)
        if "Quarter" in name or "View3DInventorViewer" in name:
            for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
                QtWidgets.QApplication.sendEvent(
                    w, QtGui.QKeyEvent(kind, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier)
                )
            return True
    note("no viewer widget among: " + ", ".join(sorted(set(seen))))
    return False


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.ActiveDocument.resetEdit()
        pump()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def run():
    try:
        import Part
        import Sketcher

        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        # One segment, so the sketch has the origin and two vertices of its
        # own: with a single point there is nothing for the origin to differ
        # from and the test could not fail.
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(5, 5, 0),
                                            FreeCAD.Vector(15, 5, 0)), False)
        doc.recompute()
        pump()

        FreeCADGui.ActiveDocument.setEdit(sketch, 0)
        pump()
        check("the sketch is in edit",
              FreeCADGui.ActiveDocument.getInEdit() is not None)

        idle = point_markers()
        if not check("the edit scene has a vertex marker set", idle is not None):
            finish()
            return
        # One value covers every point, which is how a sketch with no tool
        # running and no group in it is drawn.
        check("with no tool running every vertex wears the same marker",
              len(set(idle)) == 1, idle)
        filled = idle[0]

        FreeCADGui.runCommand("Sketcher_CreateLine")
        pump()
        active = point_markers()
        check("a tool is running and the marker set now speaks per point",
              active is not None and len(active) > 1, active)
        if active and len(active) > 1:
            check("the origin's marker changed", active[0] != filled,
                  (active[0], filled))
            check("and it is the only vertex that changed",
                  set(active[1:]) == {filled}, active)

        # Escape, rather than resetEdit: leaving edit mode tears the whole
        # scene down, which would prove nothing about the restore. There is
        # no Python call that purges a handler, so this is the key the user
        # presses, sent to the widget that would receive it.
        check("the tool took Escape and went away", send_escape())
        pump()
        back = point_markers()
        check("with the tool gone the origin is drawn like the rest again",
              back is not None and len(set(back)) == 1, back)
        if back:
            check("and it is the marker it started with", back[0] == filled,
                  (back[0], filled))
    except Exception:
        note("ABORT run:\n" + traceback.format_exc())
    finish()


QtCore.QTimer.singleShot(1500, run)
