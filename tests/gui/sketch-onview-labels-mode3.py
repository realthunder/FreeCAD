"""In render cache mode 3 a sketch tool's on-view parameters are drawn.

A drawing tool shows its parameters in the view as dimension labels
(Gui::EditableDatumLabel: an SoDatumLabel under an SoAnnotation hung on
the viewer's scene graph), with an entry box over each. While the
backend draws the edit graph the label's own GLRender stands down, and
the label gives the render-cache capture nothing by itself: a Sketcher
constraint's datum reaches the backend through the companion sub-graph
SoDatumLabel::getImageNode(), and the on-view label never added one. So
in mode 3, the default, nobody drew the labels at all.

The measurement: the backend's own framebuffer (saveRenderDump, source
"renderer") with the line tool running, against the same view before the
tool started. Besides the labels only the tool's 7x7 point marker
appears, so the frame must change by far more than that. Whether the
tool opened any labels is checked on its own first -- the pointer is
synthetic, and a harness that raised none would otherwise read as the
defect.

Scored against the tree before the fix: the labels were opened (two
annotations) and 8 pixels changed, a 7x7 box -- the marker. After it,
510.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "OnViewLabels"
TOOLS = "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def dump(view, name):
    path = os.path.join(OUT, name)
    view.saveRenderDump(path, "renderer")
    return QtGui.QImage(path)


def run():
    try:
        state["visibility"] = FreeCAD.ParamGet(TOOLS).GetInt(
            "OnViewParameterVisibility", 1)
        FreeCAD.ParamGet(TOOLS).SetInt("OnViewParameterVisibility", 2)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        doc.recompute()
        FreeCADGui.getDocument(DOC).getObject("Sketch").ShowGrid = False
        FreeCADGui.activeDocument().setEdit(sketch)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        state["view"] = view
        QtCore.QTimer.singleShot(1500, start_tool)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def start_tool():
    try:
        view = state["view"]
        view.waitFrameComplete()
        try:
            state["before"] = dump(view, "before.png")
            active, detail = True, ""
        except Exception as e:
            active, detail = False, str(e)
        if not check("the bgfx renderer draws the view", active, detail):
            finish()
            return
        state["children"] = view.getSceneGraph().getNumChildren()
        FreeCADGui.runCommand("Sketcher_CreateLine")
        vp = view.graphicsView().viewport()
        w, h = vp.width(), vp.height()
        for fx, fy in ((0.40, 0.55), (0.42, 0.54), (0.45, 0.52)):
            pos = QtCore.QPoint(int(w * fx), int(h * fy))
            ev = QtGui.QMouseEvent(QtCore.QEvent.MouseMove, QtCore.QPointF(pos),
                                   QtCore.QPointF(vp.mapToGlobal(pos)),
                                   QtCore.Qt.NoButton, QtCore.Qt.NoButton,
                                   QtCore.Qt.NoModifier)
            QtWidgets.QApplication.sendEvent(vp, ev)
        QtCore.QTimer.singleShot(2000, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure():
    try:
        view = state["view"]
        view.waitFrameComplete()
        added = view.getSceneGraph().getNumChildren() - state["children"]
        if not check("the line tool opened its on-view parameters", added > 0,
                     "%d nodes added to the scene graph" % added):
            finish()
            return
        before, after = state["before"], dump(view, "after.png")
        changed, box = 0, None
        for y in range(after.height()):
            for x in range(after.width()):
                a = QtGui.QColor(before.pixel(x, y))
                b = QtGui.QColor(after.pixel(x, y))
                if (abs(a.red() - b.red()) + abs(a.green() - b.green())
                        + abs(a.blue() - b.blue())) > 30:
                    changed += 1
                    box = (x, y, x, y) if box is None else (
                        min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y))
        check("the backend draws the on-view parameters", changed > 100,
              "%d pixels changed, box %s" % (changed, box))
        FreeCADGui.activeDocument().resetEdit()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    if "visibility" in state:
        FreeCAD.ParamGet(TOOLS).SetInt("OnViewParameterVisibility",
                                       state["visibility"])
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
