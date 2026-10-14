"""What a view shows on top lies between its near and far planes, shown in the
document or not.

docs/HandsOnQueue.md entry 124 (108's side finding): a selected feature of a
body hidden in the document, shown on top, was cut off -- the view's near
and far planes are fitted to the scene's box, in which the hidden body is
nothing. The reporter chose "way 2": the view's bounds take in what it shows
on top.

A small box at the origin, shown; a part hidden in the document holding a
box 2000 mm below it. The view from the top, fitted to what is shown.
Claims, the camera's planes as the last frame set them:
  - before anything is selected the far box lies past the far plane (the
    case is set up as the entry has it);
  - the far box selected through its hidden part, and shown on top: it lies
    between the near and far planes.

Runs on the SCREEN (the planes are set by a frame drawn); the pointer is not
moved.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from pivy import coin
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def depths(view, box):
    """The least and most depth of a box's corners along the view's line of
    sight, and the camera's near and far distances"""
    cam = view.getCameraNode()
    pos = FreeCAD.Vector(*cam.position.getValue().getValue())
    rot = cam.orientation.getValue()
    d = rot.multVec(coin.SbVec3f(0, 0, -1)).getValue()
    look = FreeCAD.Vector(*d)
    values = []
    for x in (box.XMin, box.XMax):
        for y in (box.YMin, box.YMax):
            for z in (box.ZMin, box.ZMax):
                values.append((FreeCAD.Vector(x, y, z) - pos).dot(look))
    return min(values), max(values), cam.nearDistance.getValue(), cam.farDistance.getValue()


def frame(view):
    view.redraw()
    settle(600)
    view.redraw()
    settle(600)


def run():
    try:
        VIEW.SetBool("ShowSelectionOnTop", True)
        doc = FreeCAD.newDocument("OnTopClip")
        near = doc.addObject("Part::Box", "NearBox")
        part = doc.addObject("App::Part", "Holder")
        far = doc.addObject("Part::Box", "FarBox")
        part.addObject(far)
        far.Placement.Base = FreeCAD.Vector(0, 0, -2000)
        doc.recompute()
        part.ViewObject.Visibility = False
        far.ViewObject.Visibility = True
        settle(800)
        view = FreeCADGui.ActiveDocument.ActiveView
        view.viewTop()
        view.fitAll()
        frame(view)
        box = far.Shape.BoundBox
        lo, hi, n, f = depths(view, box)
        note("INFO nothing selected: the far box %.1f to %.1f deep; the planes %.1f, %.1f"
             % (lo, hi, n, f))
        check("set up as the entry has it: the far box lies past the far plane", lo > f,
              "%.1f past %.1f" % (lo, f))

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, part.Name, far.Name + ".")
        frame(view)
        lo, hi, n, f = depths(view, box)
        note("INFO the far box selected and on top: %.1f to %.1f deep; the planes %.1f, %.1f"
             % (lo, hi, n, f))
        check("the far box, selected and shown on top, lies between the near and far planes",
              lo >= n - 1e-3 and hi <= f + 1e-3, "%.1f..%.1f in %.1f..%.1f" % (lo, hi, n, f))
        try:
            view.saveRenderDump(os.path.join(OUT, "selected.png"), metadata=False)
        except Exception as e:
            note("INFO no picture: %s" % e)
        FreeCADGui.Selection.clearSelection()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        VIEW.RemBool("ShowSelectionOnTop")
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(3000, run)
