"""An origin's axis is picked where it is seen dashed through a solid.

docs/HandsOnQueue.md entry 99: "change the picking strategy to prioritize on
axis and plan border picking. actually there is special handling to lower
picking priority for original plane. but with this samll size of origin, they
are all blocked by object, hence non-pickable practically. so make the axis
and border picking normal would be enought, because they are in on-top show
group"; and as answered: the dashed, hidden parts of the axes and of the
planes' borders are picked through a face in front of them; a plane's face
keeps its lowered priority.

A datum draws what is hidden of it dashed, over the scene. The pick did not
follow: the face in front was nearer and took it. The edges and vertices under
that dashed part are picked through what is in front now; a face never is.

A body holding a box that encloses its origin, the origin shown. Claims, the
pointer at a point of the X axis inside the box:
  - the X axis is what is under the pointer, not the box;
  - beside it, on the box's face and off every line of the origin, the box is
    -- although a plane of the origin lies behind that point too;
  - the box hidden: inside a plane of the origin, off its lines, the plane is
    picked by its face (the late pick, which under the renderer found nothing:
    its paths, like the dashed lines', start below the camera).

Needs the renderer, so a screen: both picks go along paths the render cache
manager keeps, and offscreen there is no render engine to make it capture
any. The pointer is not moved: a move is sent to the view as an event of the
scene.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
STEPS = []
S = {}
V = FreeCAD.Vector


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


def preselected():
    try:
        pre = FreeCADGui.Selection.getPreselection()
        if not pre or not pre.Object:
            return None
        # the top object and the path below it: an origin's axis in a body
        # reads "Body" and "Origin.X_Axis..."
        return pre.Object.Name + "." + ".".join(pre.SubElementNames)
    except Exception:
        return None


def at(point):
    """A pointer move to where the view shows a point of the model, sent to
    the view as events of the scene -- the real pointer stays where it is --
    coming from away and with a small move at the end: a view preselects on a
    move. Returns what is preselected."""
    from pivy import coin
    view = S["view"]
    manager = view.getViewer().getSoEventManager()
    x, y = view.getPointOnViewport(point)
    for dx, wait in ((60, 300), (6, 250), (2, 250), (0, 900)):
        event = coin.SoLocation2Event()
        event.setPosition(coin.SbVec2s(int(x) + dx, int(y) + dx))
        manager.processEvent(event)
        settle(wait)
    return preselected(), (int(x), int(y))


def start():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    VIEW.SetBool("EnablePreselection", True)
    doc = FreeCAD.newDocument("DatumPick")
    body = doc.addObject("PartDesign::Body", "Body")
    box = body.newObject("PartDesign::AdditiveBox", "Box")
    box.Length, box.Width, box.Height = 400, 400, 400
    box.Placement.Base = V(-200, -200, -200)
    doc.recompute()
    body.Origin.ViewObject.Visibility = True
    for feature in body.Origin.OriginFeatures:
        feature.ViewObject.Visibility = True
    S["x"] = [f for f in body.Origin.OriginFeatures if f.Role == "X_Axis"][0].Name
    S["origin"] = set(f.Name for f in body.Origin.OriginFeatures)
    view = FreeCADGui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")[0]
    S["view"] = view
    view.setCameraType("Orthographic")
    view.viewIsometric()
    settle(800)
    view.fitAll()
    settle(2000)
    FreeCADGui.Selection.clearSelection()


def look():
    view = S["view"]
    # where the axis is drawn: its length on the screen is the datum's, not
    # the model's, so a point of it is found from the screen -- a tenth of
    # the way from the origin towards a point far along x
    x0, y0 = view.getPointOnViewport(V(0, 0, 0))
    x1, y1 = view.getPointOnViewport(V(150, 0, 0))
    length = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
    note("the origin at (%.0f, %.0f), 150 along x at (%.0f, %.0f): %.0f pixels"
         % (x0, y0, x1, y1, length))
    hits = []
    for step in (12, 20, 28, 36):
        point = V(150.0 * step / max(length, 1.0), 0, 0)
        got, pos = at(point)
        hits.append((step, got, pos))
    note("along the x axis, by pixels from the origin: %s" % hits)
    on_axis = [h for h in hits if h[1] and ("." + S["x"] + ".") in ("." + h[1] + ".")]
    check("the X axis is picked through the box, where it is seen dashed", bool(on_axis),
          [(h[0], h[1]) for h in hits])
    FreeCADGui.getMainWindow().grab().save(os.path.join(OUT, "on-the-axis.png"))

    # off every line of the origin: a point of the box's top face above the
    # XY plane's inside, well away from the axes and from the plane's border
    got, pos = at(V(9.0 * 150.0 / max(length, 1.0), 9.0 * 150.0 / max(length, 1.0), 200))
    check("beside the lines, on the box's face, the box is picked",
          got is not None and ".Box.Face" in ("." + got)
          and not any(("." + name + ".") in ("." + got + ".") for name in S["origin"]),
          (got, pos))

    # The box out of the way: inside a plane of the origin, off its lines,
    # the plane is picked by its face. That pick is made late, after
    # everything else, along paths the render cache manager keeps -- which
    # start below the camera, and applied by themselves found nothing.
    FreeCAD.getDocument("DatumPick").getObject("Box").ViewObject.Visibility = False
    settle(1200)
    unit = 150.0 / max(length, 1.0)
    got, pos = at(V(14 * unit, 14 * unit, 0))
    # (whichever plane the ray meets first there: two of them lie behind
    # that point of the screen)
    check("with the box hidden, inside a plane of the origin and off its lines, a plane is picked",
          got is not None and "_Plane" in got, (got, pos))


def finish():
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    step = STEPS.pop(0)
    try:
        step()
    except Exception:
        note("ABORT in %s: %s" % (step.__name__, traceback.format_exc().replace("\n", " | ")))
        STEPS[:] = []
    QtCore.QTimer.singleShot(800, advance)


STEPS[:] = [start, look]
QtCore.QTimer.singleShot(2500, advance)
