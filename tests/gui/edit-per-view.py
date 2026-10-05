"""An edit runs in the one view it is started in, when the user says so.

docs/ThinClient.md 8.11 made a document's edit one session that every
view of it joins. The preference PerViewEdit (View parameters) makes a
session its initiating view's alone: no other 3D window of the document
joins it, so none hangs the session's editing root, takes its hides and
swaps or routes input to its tool, and each goes on showing the document
as it is. The plumbing was per view already; this is the policy at the
places that would join a view (Gui::EditingRoot::isShared).

A body of a box and a pad on it, the pad edited with the PartDesign
preview on -- the tinted tool (magenta here) is what a view in the
session draws and a view outside it does not. Two 3D windows, and a
third opened while the edit runs.

Claims, with PerViewEdit on:
  - the window the edit is started in draws the tool; the other keeps
    the pad, and picks it;
  - the session's editing root is in the first window's graph and not in
    the second's;
  - the document answers "in edit" for the window that is, and not for
    the one that is not (getInEdit asks the active 3D window);
  - a window opened during the edit does not join;
  - leaving the edit from the window it runs in ends it for the document.

And with it off, the same steps as the control: both windows, and the
one opened mid-edit, draw the tool.

Mode 3 only (the preview is the session's own there).

Not scored against the tree before the preference: there every session
is shared, which is the behaviour the control at the end pins.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
V = FreeCAD.Vector
state = {"done": False, "shot": 0}

VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
PART = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
PART.SetBool("PreviewOnEdit", True)
PART.SetBool("PreviewWithTransparency", True)
PART.SetBool("EditOnTop", False)
PART.SetUnsigned("PreviewAddColor", 0xFF00FF00)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def activate(view):
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle(100)


def hit(view, pt):
    x, y = view.getPointOnViewport(pt)
    info = view.getObjectInfo((int(x), int(y)))
    return info.get("Object", "") if info else ""


def pixel(view, pt, tag):
    """The backend's own framebuffer at the projection of 3D point pt."""
    activate(view)
    state["shot"] += 1
    path = os.path.join(OUT, "%02d-%s.png" % (state["shot"], tag))
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(pt)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def tool(c):
    """Magenta: the preview's tinted tool shape."""
    return c[0] - c[1] > 40 and c[2] - c[1] > 40


def solid(c):
    """Green: a feature's own shape."""
    return c[1] - c[0] > 40 and c[1] - c[2] > 40


def edit_roots(view):
    """How many times the session's editing root is in the view's graph."""
    from pivy import coin

    graph = view.getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setName("EditingRoot")
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(graph)
    return search.getPaths().getLength()


def case(name, per_view):
    """One document, one edit, under one setting of the preference."""
    VIEW.SetBool("PerViewEdit", per_view)
    tag = "per view" if per_view else "shared"
    doc = FreeCAD.newDocument(name)
    body = doc.addObject("PartDesign::Body", "Body")
    box = body.newObject("PartDesign::AdditiveBox", "Box")
    box.Length = box.Width = box.Height = 10
    sketch = body.newObject("Sketcher::SketchObject", "Sketch")
    sketch.Support = (doc.getObject("XY_Plane"), [""])
    sketch.MapMode = "FlatFace"
    corners = [V(3, 3, 0), V(7, 3, 0), V(7, 7, 0), V(3, 7, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sketch
    pad.Length = 20
    doc.recompute()
    for obj in (body, box, pad):
        obj.ViewObject.ShapeColor = (0.0, 0.8, 0.0)
    box.Visibility = False
    sketch.Visibility = False
    gdoc = FreeCADGui.getDocument(name)

    v1 = gdoc.activeView()
    FreeCADGui.runCommand("Std_ViewCreate")
    settle()
    v2 = gdoc.activeView()
    if len(gdoc.mdiViewsOfType("Gui::View3DInventor")) != 2:
        note("ABORT no second 3D view")
        return
    for v in (v1, v2):
        v.setCameraType("Orthographic")
        v.viewFront()
        v.fitAll()
    settle()
    stub = V(6, 5, 15)
    if not (solid(pixel(v1, stub, tag + "-v1-base"))
            and solid(pixel(v2, stub, tag + "-v2-base"))):
        note("ABORT the pad is not what the baseline shows")
        return

    activate(v1)
    gdoc.setEdit(pad, 0)
    settle(1000)
    activate(v1)
    if not check("%s: the edit is on, asked from the window it started in" % tag,
                 gdoc.getInEdit() is not None):
        return
    s1 = pixel(v1, stub, tag + "-v1-edit")
    s2 = pixel(v2, stub, tag + "-v2-edit")
    check("%s: the window the edit started in draws the tool" % tag, tool(s1), s1)
    r1, r2 = edit_roots(v1), edit_roots(v2)
    check("%s: and has the session's editing root in its graph" % tag, r1 == 1, r1)
    activate(v2)
    asked = gdoc.getInEdit() is not None
    picked = hit(v2, stub)
    if per_view:
        check("per view: the other window keeps the pad", solid(s2), s2)
        check("per view: and picks it", picked == "Pad", picked)
        check("per view: the session's root is not in its graph", r2 == 0, r2)
        check("per view: asked from it, the document is not in edit", not asked)
    else:
        check("shared: the other window draws the tool too", tool(s2), s2)
        check("shared: the session's root is in its graph", r2 == 1, r2)
        check("shared: asked from it, the document is in edit", asked)

    FreeCADGui.runCommand("Std_ViewCreate")
    settle()
    v3 = gdoc.activeView()
    v3.setCameraType("Orthographic")
    v3.viewFront()
    v3.fitAll()
    settle()
    s3 = pixel(v3, stub, tag + "-v3-opened")
    if per_view:
        check("per view: a window opened during the edit does not join",
              solid(s3) and edit_roots(v3) == 0, (s3, edit_roots(v3)))
    else:
        check("shared: a window opened during the edit joins",
              tool(s3) and edit_roots(v3) == 1, (s3, edit_roots(v3)))
    check("%s: the first window still draws the tool" % tag,
          tool(pixel(v1, stub, tag + "-v1-still")))

    # The preference changed mid-session moves nobody
    VIEW.SetBool("PerViewEdit", not per_view)
    settle()
    check("%s: the preference changing mid-edit moves no view" % tag,
          edit_roots(v1) == 1 and edit_roots(v2) == r2, (edit_roots(v1), edit_roots(v2)))
    VIEW.SetBool("PerViewEdit", per_view)

    activate(v1)
    gdoc.resetEdit()
    settle(1000)
    left = [gdoc.getInEdit() is None]
    activate(v2)
    left.append(gdoc.getInEdit() is None)
    check("%s: leaving ends the edit for the document" % tag, all(left), left)
    after = [pixel(v, stub, tag + "-after") for v in (v1, v2, v3)]
    check("%s: every window shows the pad again" % tag,
          all(solid(c) for c in after), after)
    check("%s: no window keeps the editing root" % tag,
          [edit_roots(v) for v in (v1, v2, v3)] == [0, 0, 0],
          [edit_roots(v) for v in (v1, v2, v3)])
    FreeCAD.closeDocument(name)
    settle()


def run():
    try:
        if VIEW.GetInt("RenderCache", 3) != 3:
            note("ABORT render cache is not mode 3")
        else:
            case("EditPerView", True)
            case("EditShared", False)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    VIEW.SetBool("PerViewEdit", False)
    # Not asked through getInEdit: that answers for the active window
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCADGui.getDocument(name).resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
