"""The view following a selection does not mesh the object selected.

With the tree's sync view on (its default) a selection in the tree has the
3D view make sure the object is on the screen ("ViewSelectionExtend"). It
leaves the camera alone when the object already is, and to know that it
projects the object's bounding box and then, where the box meets the
screen, the triangles of its faces. It asked the shape for those
triangles, and a shape that has none is MESHED to answer
(Part::TopoShape::getDomains): on the GUI thread, for a question about
what is on the screen. Right after a load, while the visuals are still
being built, no shape has a mesh yet -- and with the load's parallel
pre-mesh on, a worker may be meshing that very shape at that moment, so
that one click in the tree put a second mesher on it
(docs/DocumentLoad.md sec 18.8).

An object whose visual is still to be built is now answered for by its
bounding box.

What is done: a document of spheres is saved with all of them on the
screen and reopened with the pre-mesh off and the drain's budget at its
smallest, so that the visuals stay unbuilt for a while and nothing else
meshes them. As soon as the last sphere's view provider answers with a
bounding box -- the load makes the view providers after it hands the
document back -- and while that sphere still has no mesh, it is selected
and the view told to follow, as the tree does.

How a mesh is seen without making one: OCCT boxes a shape that carries a
triangulation from the triangulation and one that carries none from its
geometry. A sphere's box is its diameter exactly until it is meshed, and
the box of its mesh is not (0.22 narrower at the display's deflection).

What is asserted:
  - the sphere had no mesh before the view was told to follow (or the
    test would prove nothing), and its visual was still to be built;
  - it has none after;
  - the camera was left where it was: the sphere is on the screen;
  - the control: once the load has built the visual, the same sphere's
    box is NOT its diameter -- the measure can tell.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout). Scored against the tree before the change (ef586a0981), where
the sphere is meshed by the question.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PATH = os.path.join(OUT, "spheres.FCStd")
COUNT = 120
RADIUS = 10.0
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
TREE = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TreeView")

state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def excess(obj):
    """How far the sphere's box is from its diameter: nothing for a shape
    without a mesh."""
    return obj.Shape.BoundBox.XLength - 2.0 * RADIUS


def meshed(obj):
    return abs(excess(obj)) > 1e-3


def camera():
    cam = FreeCADGui.ActiveDocument.ActiveView.getCameraNode()
    pos = cam.position.getValue()
    return (round(pos[0], 3), round(pos[1], 3), round(pos[2], 3),
            round(cam.height.getValue(), 3) if hasattr(cam, "height") else 0)


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        VIEW.SetBool("ShowNaviCube", False)
        VIEW.SetBool("UseNavigationAnimations", False)
        doc = FreeCAD.newDocument("Spheres")
        for i in range(COUNT):
            obj = doc.addObject("Part::Sphere", "S%d" % i)
            obj.Radius = RADIUS
            obj.Placement.Base = FreeCAD.Vector(40 * (i % 12), 40 * (i // 12), 0)
        doc.recompute()
        FreeCADGui.ActiveDocument.ActiveView.viewTop()
        FreeCADGui.SendMsgToActiveView("ViewFit")
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1500, save_and_close)


def save_and_close():
    try:
        doc = FreeCAD.getDocument("Spheres")
        doc.saveAs(PATH)
        FreeCAD.closeDocument(doc.Name)
        # Nothing but the question under test may mesh a shape of the load,
        # and the load takes its time over the visuals
        RENDER.SetBool("PreMeshOnLoad", False)
        RENDER.SetInt("ProgressiveLoadBudgetMS", 1)
        TREE.SetBool("SyncView", True)
    except Exception:
        note("ABORT save:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(800, reopen_and_follow)


def reopen_and_follow():
    try:
        doc = FreeCAD.openDocument(PATH)
        state["doc"] = doc.Name
        state["polls"] = 0
    except Exception:
        note("ABORT open:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(0, follow_when_asked_about)


def follow_when_asked_about():
    """Waits for the moment the question can be put at all: the view
    provider has a bounding box, the shape has no mesh yet."""
    try:
        doc = FreeCAD.getDocument(state["doc"])
        last = doc.getObject("S%d" % (COUNT - 1))
        state["polls"] += 1
        if meshed(last):
            check("the sphere was caught with a bounding box and no mesh", False,
                  "meshed after %d polls: nothing to ask" % state["polls"])
            finish()
            return
        if not last.ViewObject.getBoundingBox().isValid():
            if state["polls"] > 2000:
                check("the sphere was caught with a bounding box and no mesh", False,
                      "no bounding box in %d polls" % state["polls"])
                finish()
                return
            QtCore.QTimer.singleShot(5, follow_when_asked_about)
            return
        check("the sphere was caught with a bounding box and no mesh", True,
              "after %d polls, %.6f from its diameter" % (state["polls"], excess(last)))
        was = camera()
        # What a selection in the tree does with sync view on
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, last.Name)
        FreeCADGui.SendMsgToActiveView("ViewSelectionExtend")
        check("the view told to follow the selection did not mesh it",
              not meshed(last), "%.6f from its diameter" % excess(last))
        check("and left the camera where it was: the sphere is on the screen",
              camera() == was, "%s, was %s" % (camera(), was))
    except Exception:
        note("ABORT follow:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(9000, control)


def control():
    try:
        doc = FreeCAD.getDocument(state["doc"])
        last = doc.getObject("S%d" % (COUNT - 1))
        built = excess(last)
        check("the control: built by the load, the same sphere's box is not its diameter",
              meshed(last), "%.6f from its diameter" % built)
        was = camera()
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, last.Name)
        FreeCADGui.SendMsgToActiveView("ViewSelectionExtend")
        check("and the view following it then, by its triangles, leaves the camera too",
              camera() == was, "%s, was %s" % (camera(), was))
    except Exception:
        note("ABORT control:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    RENDER.RemBool("PreMeshOnLoad")
    RENDER.RemInt("ProgressiveLoadBudgetMS")
    TREE.RemBool("SyncView")
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, build)
