"""PartDesign's task watchers show only while the active view is a 3D view.

docs/HandsOnQueue.md entry 152 (the reporter, 2026-10-16 09:40): "PartDesign
task watcher to hide itself if the active view is not 3D view." Over a
TechDraw page the task panel went on offering "Start Part", "Face tools" and
the like, whose commands work in a 3D view.

The workbench marks its watchers (TaskWatcher::setNeeds3DView) and the task
view looks again at each view activation. Claims, the selection empty
throughout (the "Start Part" watcher):
  - in the 3D view the watcher's box shows;
  - a TechDraw page made the active view, it is hidden;
  - the 3D view active again, it shows again.

Runs with QT_QPA_PLATFORM=offscreen.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "WatchersNeed3DView"
TITLE = "Start Part"


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=800):
    # the task view looks at its watchers on a 200 ms timer
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def texts(widget):
    out = []
    for child in widget.findChildren(QtWidgets.QAbstractButton):
        out.append(child.text())
    for child in widget.findChildren(QtWidgets.QLabel):
        out.append(child.text())
    return out


def watcher_boxes():
    """The task boxes titled TITLE; shown or hidden by the task view's
    show()/hide(), so isHidden() is theirs whatever the panel's own state"""
    boxes = []
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::TaskView::TaskBox" and TITLE in texts(w):
            boxes.append(w)
    return boxes


def shown(boxes):
    return [b for b in boxes if not b.isHidden()]


def active_view_type():
    view = FreeCADGui.activeView()
    return type(view).__name__ if view is not None else None


def in_3d_view():
    view = FreeCADGui.activeView()
    return view is not None and hasattr(view, "getCameraNode")


def run():
    try:
        FreeCADGui.activateWorkbench("PartDesignWorkbench")
        doc = FreeCAD.newDocument(DOC)
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Control.showTaskView()
        settle()
        boxes = watcher_boxes()
        check("the workbench has a '%s' watcher box" % TITLE, len(boxes) > 0, len(boxes))
        check("in the 3D view it shows", len(shown(boxes)) > 0,
              "%s, %d shown" % (active_view_type(), len(shown(boxes))))

        # a page opens only with a template
        page = doc.addObject("TechDraw::DrawPage", "Page")
        template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
        template.Template = os.path.join(
            FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
        page.Template = template
        doc.recompute()
        FreeCADGui.getDocument(DOC).getObject("Page").doubleClicked()
        FreeCADGui.Selection.clearSelection()
        settle()
        page_active = not in_3d_view()
        check("a TechDraw page is the active view", page_active, active_view_type())
        check("over the page the watcher is hidden", len(shown(watcher_boxes())) == 0,
              "%d shown" % len(shown(watcher_boxes())))

        FreeCADGui.activateView("Gui::View3DInventor", False)
        settle()
        check("the 3D view is the active view again",
              in_3d_view(), active_view_type())
        check("and the watcher shows again", len(shown(watcher_boxes())) > 0,
              "%d shown" % len(shown(watcher_boxes())))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(3000, run)
