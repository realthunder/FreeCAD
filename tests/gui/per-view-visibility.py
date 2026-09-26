"""A view's own object visibility (ObjectVisibilities, PerViewVisibilities).

Two 3D views of one document. The view being tested keeps its own
visibility map; the other view must never see any of it. What is
watched here is the Coin side -- SoFCVisibilityElement read by
SoFCSwitch -- through what a view answers per traversal: a PICK at an
object's projected centre, and the scene BOUNDING BOX (what fit-all
frames).

Claims:
  - a bare entry counts only while PerViewVisibilities is on;
  - a bare hide takes the object out wherever it appears in that view,
    a Link to it included;
  - a path entry hides ONE occurrence (the Link, a child inside an
    App::Part) and counts with PerViewVisibilities off;
  - a bare show brings in an object whose Visibility is off, in that
    view only;
  - the other view is untouched throughout.

Scored against the tree before the feature: setObjectVisibility does not
exist, and the run ABORTs.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PerViewVis"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def hit(view, pt):
    """(object name, subname) the view picks at screen point pt, or None."""
    info = view.getObjectInfo((int(pt[0]), int(pt[1])))
    if not info:
        return None
    return (info.get("Object"), info.get("SubName", ""))


def xmax(view):
    from pivy import coin
    vp = view.getViewer().getSoRenderManager().getViewportRegion()
    action = coin.SoGetBoundingBoxAction(vp)
    action.apply(view.getSceneGraph())
    box = action.getBoundingBox()
    if box.isEmpty():
        return None
    return round(box.getMax()[0], 3)


def pixel(view, pt, tag):
    """The backend's own framebuffer (mode 3) at the projection of 3D
    point pt, as (r, g, b). The frame, getPointOnViewport and the ray
    pick share one projection, portrait views included
    (portrait-pick-vs-draw.py). The view is made active first: a view in a hidden
    tab does not paint, and the dump waits for a painted frame."""
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle()
    path = os.path.join(OUT, "%s.png" % tag)
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(pt)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def differ(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) > 30


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        box1 = doc.addObject("Part::Box", "Box1")
        asm = doc.addObject("App::Part", "Asm")
        box2 = doc.addObject("Part::Box", "Box2")
        box2.Placement.Base = FreeCAD.Vector(30, 0, 0)
        asm.addObject(box2)
        link = doc.addObject("App::Link", "Link")
        link.LinkedObject = box1
        link.Placement.Base = FreeCAD.Vector(60, 0, 0)
        box3 = doc.addObject("Part::Box", "Box3")
        box3.Placement.Base = FreeCAD.Vector(90, 0, 0)
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)

        v1 = gdoc.activeView()
        FreeCADGui.runCommand("Std_ViewCreate")
        settle()
        v2 = gdoc.activeView()
        if len(gdoc.mdiViewsOfType("Gui::View3DInventor")) != 2:
            note("ABORT no second 3D view")
            finish()
            return
        for v in (v1, v2):
            v.viewFront()
            v.fitAll()
        settle()
        # Hidden only after the fit, so both views frame it.
        box3.ViewObject.Visibility = False
        settle()

        # Where each object picks, found by a blind sweep: it does not
        # lean on the projection that portrait-pick-vs-draw.py checks.
        w, h = v1.getSize()
        found = {}
        for gy in range(0, h, 4):
            for gx in range(0, w, 4):
                i = v1.getObjectInfo((gx, gy))
                if i:
                    found.setdefault(i.get("Object"), []).append((gx, gy))
        centre = {}
        for k, pts in found.items():
            centre[k] = (sum(p[0] for p in pts) // len(pts),
                         sum(p[1] for p in pts) // len(pts))
        note("sweep centres %s" % centre)
        if not all(k in centre for k in ("Box1", "Box2", "Link")):
            note("ABORT sweep did not find Box1, Box2 and the Link: %s" % centre)
            finish()
            return
        c1, c2, cl = centre["Box1"], centre["Box2"], centre["Link"]
        # Box3 is hidden: one more step of the same spacing along x.
        c3 = (cl[0] + (cl[0] - c2[0]), cl[1])

        def state_of(view):
            return {"box1": hit(view, c1), "box2": hit(view, c2),
                    "link": hit(view, cl), "box3": hit(view, c3),
                    "xmax": xmax(view)}

        base1 = state_of(v1)
        base2 = state_of(v2)
        note("baseline v1 %s" % base1)
        note("baseline v2 %s" % base2)
        if not (base1["box1"] and base1["box2"] and base1["link"]):
            note("ABORT baseline picks miss: %s" % base1)
            finish()
            return
        check("baseline: the hidden Box3 is not picked", base1["box3"] is None,
              base1["box3"])

        # 1. A bare entry with the switch off counts for nothing.
        v1.setObjectVisibility(box1, False)
        settle()
        s = state_of(v1)
        check("bare hide is inert while PerViewVisibilities is off",
              s["box1"] == base1["box1"], s)

        # 2. Switch on: Box1 goes, and so does the Link showing it.
        v1.PerViewVisibilities = True
        settle()
        s = state_of(v1)
        check("bare hide: Box1 no longer picked in the view", s["box1"] is None, s)
        check("bare hide: the Link to Box1 is gone too", s["link"] is None, s)
        check("bare hide: Box2 still picked", s["box2"] == base1["box2"], s)
        check("the other view still picks Box1 and the Link",
              state_of(v2) == base2, state_of(v2))
        check("getObjectVisibility reads the entry back",
              v1.getObjectVisibility(box1) is False, v1.getObjectVisibility(box1))
        v1.setObjectVisibility(box1, None)
        settle()
        s = state_of(v1)
        check("clearing the entry brings Box1 back", s["box1"] == base1["box1"], s)

        # 3. Path entries, with the switch off: one occurrence each.
        v1.PerViewVisibilities = False
        v1.setObjectVisibility(link, False, "")
        v1.setObjectVisibility(asm, False, "Box2.")
        settle()
        s = state_of(v1)
        check("path hide: the Link occurrence is gone", s["link"] is None, s)
        check("path hide: Box2 inside Asm is gone", s["box2"] is None, s)
        check("path hide: Box1 itself stays", s["box1"] == base1["box1"], s)
        check("path hide: the scene box shrinks in this view",
              s["xmax"] is not None and base1["xmax"] is not None
              and s["xmax"] < base1["xmax"], (s["xmax"], base1["xmax"]))
        check("path hide: the other view is untouched",
              state_of(v2) == base2, state_of(v2))
        v1.setObjectVisibility(link, None, "")
        v1.setObjectVisibility(asm, None, "Box2.")
        settle()

        # 4. A bare SHOW of an object whose Visibility is off.
        v1.PerViewVisibilities = True
        v1.setObjectVisibility(box3, True)
        settle()
        s = state_of(v1)
        check("bare show: Box3 is picked in this view",
              s["box3"] is not None and s["box3"][0] == "Box3", s)
        check("bare show: the scene box grows in this view",
              s["xmax"] is not None and s["xmax"] > base1["xmax"],
              (s["xmax"], base1["xmax"]))
        check("bare show: the other view still does not pick Box3",
              hit(v2, c3) is None, hit(v2, c3))
        check("bare show: Box3's own Visibility is untouched",
              box3.ViewObject.Visibility is False)

        # The capture is shared: Box3 is in it now, tagged for the view
        # that shows it, and no other view may count it in its bounds.
        check("bare show: the other view's scene box stays as it was",
              xmax(v2) == base2["xmax"], (xmax(v2), base2["xmax"]))
        v1.ObjectVisibilities = {}
        settle()
        check("empty map: back to the baseline", state_of(v1) == base1,
              state_of(v1))

        p_c1 = FreeCAD.Vector(5, 5, 5)
        p_cl = FreeCAD.Vector(65, 5, 5)
        p_c3 = FreeCAD.Vector(95, 5, 5)

        # 5. What the backend DRAWS (mode 3): the one shared capture,
        # filtered per view at draw time. The pixel at Box1's centre in
        # each view's own framebuffer, against that view's baseline.
        p1 = pixel(v1, p_c1, "v1-base")
        p2 = pixel(v2, p_c1, "v2-base")
        pl = pixel(v1, p_cl, "v1-link-base")
        v1.PerViewVisibilities = True
        v1.setObjectVisibility(box1, False)
        settle()
        h1 = pixel(v1, p_c1, "v1-hidden")
        hl = pixel(v1, p_cl, "v1-link-hidden")
        h2 = pixel(v2, p_c1, "v2-other")
        check("drawn: Box1 is gone from this view's frame", differ(p1, h1),
              (p1, h1))
        check("drawn: the Link to Box1 is gone from this view's frame",
              differ(pl, hl), (pl, hl))
        check("drawn: the other view still draws Box1", not differ(p2, h2),
              (p2, h2))
        v1.setObjectVisibility(box1, None)
        settle()
        r1 = pixel(v1, p_c1, "v1-restored")
        check("drawn: clearing the entry draws Box1 again",
              not differ(p1, r1), (p1, r1))

        # 6. A per-view SHOW of a hidden object reaches the frame of
        # that view alone: the shared capture carries it tagged, and
        # only the showing view admits it.
        g1 = pixel(v1, p_c3, "v1-box3-hidden")
        g2 = pixel(v2, p_c3, "v2-box3-hidden")
        v1.setObjectVisibility(box3, True)
        settle()
        s1 = pixel(v1, p_c3, "v1-box3-shown")
        s2 = pixel(v2, p_c3, "v2-box3-other")
        check("drawn: the hidden Box3 is drawn in the view showing it",
              differ(g1, s1), (g1, s1))
        check("drawn: the other view still does not draw Box3",
              not differ(g2, s2), (g2, s2))
        v1.setObjectVisibility(box3, None)
        settle()
        o1 = pixel(v1, p_c3, "v1-box3-cleared")
        check("drawn: clearing the show hides Box3 again",
              not differ(g1, o1), (g1, o1))
        v1.setObjectVisibility(box3, True)
        settle()
        a1 = pixel(v1, p_c3, "v1-box3-again")
        check("drawn: showing it again draws it again",
              not differ(s1, a1), (s1, a1))
        v1.ObjectVisibilities = {}
        v1.PerViewVisibilities = False
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
