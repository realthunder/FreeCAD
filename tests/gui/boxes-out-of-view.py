"""A shape drawn as a box gets its mesh in the background when it is out of view.

A shape over Render/CoarseDeferFaces faces is drawn by a load as a
12-triangle bounding box, and its coarse mesh is made on the refine pool
when the level plan asks for it. The plan asked only for what the camera
sees: a boxed shape out of view stayed a box until the camera turned to
it, and then it was a box on the screen for as long as its mesh took
(docs/DocumentLoad.md sec 18.12, the reference assembly opened with its
saved close-up camera: 9 of 14 boxed shapes stay boxes).

Now the plan asks for those too, as the last class of work: behind the
first pictures and the refinements of what is in view, their landings
held while a load is still building visuals. A box that comes into view
before its mesh is made moves to the front.

What is done: a document of 3 boxed solids in view, 5 boxed solids far to
the right of what the camera sees (added after the fit, so that the saved
camera does not show them), 40 solids of 992 faces and 20 spheres is made
and saved; then opened twice. Nothing moves the camera. The level debug
lines name each shape drawn as a box and each one whose mesh came in.

What is asserted of the second load:
  - the case arose: all 8 were drawn as boxes, the 3 are on the screen and
    the 5 are not, and the camera at the end is the camera at the drain's
    closing line;
  - every boxed shape in view got its coarse mesh;
  - every boxed shape out of view got its coarse mesh too;
  - none of those before the last of the ones in view;
  - none of those while visuals were still being built;
  - every visual is built.

With GT_LEISURE=0 the switch Render/CoarseDeferAtLeisure is turned off and
the same load is asserted the other way: the 5 out of view stay boxes.

Not covered here: the move to the front of a box that comes into view
(see boxes-out-of-view-turn.py), and that nothing is asked for under a
memory ceiling or over the GPU budget (unit cases in
tests/src/Gui/SceneLadder.cpp).

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout). Scored against the tree before the change: see the commit
message.
"""
import math
import os
import re
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PATH = os.path.join(OUT, "boxes-out-of-view.FCStd")
NEAR = 3
FAR = 5
HEAVY = 40
LIGHT = 20
COUNT = NEAR + FAR + HEAVY + LIGHT
LOADS = 2
# How long nothing may happen after the drain's line before the load is
# taken as settled: the plan fires 0.3 s after a change, a boxed prism
# meshes in well under a second.
QUIET = float(os.environ.get("GT_QUIET", "8"))
# The switch (Render/CoarseDeferAtLeisure), and which way the load is read
LEISURE = os.environ.get("GT_LEISURE", "1") == "1"

VISUAL = re.compile(r"progressive load (\S+): (\d+) of (\d+) visuals in (\d+) slices")
BOXED = re.compile(r"#(\w+)\.ViewObject bounding-box stand-in")
RESOLVED = re.compile(r"#(\w+)\.ViewObject stand-in resolved")

state = {"done": False, "load": -1, "cur": None, "runs": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def observe(notifier, msg, level):
    cur = state["cur"]
    if cur is None:
        return
    now = time.monotonic()
    m = BOXED.search(msg)
    if m:
        cur["boxed"].add(m.group(1))
        return
    m = RESOLVED.search(msg)
    if m:
        cur["resolved"].setdefault(m.group(1), now - cur["t0"])
        cur["last"] = now
        if cur["t_drain"] is None:
            cur["in_drain"].add(m.group(1))
        return
    m = VISUAL.search(msg)
    if m:
        cur["t_drain"] = now - cur["t0"]
        cur["built"] = int(m.group(2))
        cur["last"] = now


def camera():
    """Where the camera stands and what it takes in.

    Without the clipping distances: those follow the bounds of what is
    drawn, and a box turning into its shape changes them.
    """
    view = FreeCADGui.ActiveDocument.ActiveView
    lines = [ln.strip() for ln in view.getCamera().splitlines()]
    return "\n".join(ln for ln in lines
                     if ln and not ln.startswith(("nearDistance", "farDistance")))


def on_screen(obj):
    """Whether the centre of the object's box projects into the view."""
    view = FreeCADGui.ActiveDocument.ActiveView
    width, height = view.getSize()
    x, y = view.getPointOnViewport(obj.Shape.BoundBox.Center)
    return 0 <= x < width and 0 <= y < height


def poll():
    cur = state["cur"]
    if cur is None or state["done"]:
        return
    now = time.monotonic()
    if cur["t_drain"] is not None and cur["camera"] is None:
        cur["camera"] = camera()
    settled = cur["t_drain"] is not None and (
        len(cur["resolved"]) == NEAR + FAR or now - cur["last"] > QUIET)
    if settled or now - cur["t0"] > 150:
        loaded(cur)
        return
    QtCore.QTimer.singleShot(100, poll)


def next_load():
    if state["done"]:
        return
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCAD.closeDocument(name)
        state["load"] += 1
        if state["load"] >= LOADS:
            verdict()
            return
        cur = {"boxed": set(), "resolved": {}, "in_drain": set(), "t_drain": None,
               "built": 0, "camera": None}
        cur["t0"] = time.monotonic()
        cur["last"] = cur["t0"]
        state["cur"] = cur
        doc = FreeCAD.openDocument(PATH)
        cur["name"] = doc.Name
        QtCore.QTimer.singleShot(100, poll)
    except Exception:
        note("ABORT load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()


def loaded(cur):
    state["cur"] = None
    try:
        doc = FreeCAD.getDocument(cur["name"])
        made = 0
        for obj in doc.Objects:
            box = obj.ViewObject.getBoundingBox()
            if box.isValid() and box.DiagonalLength > 0:
                made += 1
        cur["made"] = made
        cur["objects"] = len(doc.Objects)
        cur["camera_end"] = camera()
        cur["near_seen"] = [on_screen(doc.getObject("Near%d" % i)) for i in range(NEAR)]
        cur["far_seen"] = [on_screen(doc.getObject("Far%d" % i)) for i in range(FAR)]
        state["runs"].append(cur)
        near = [cur["resolved"][n] for n in cur["resolved"] if n.startswith("Near")]
        far = [cur["resolved"][n] for n in cur["resolved"] if n.startswith("Far")]
        note("INFO load %d: drain's line %.2f s after the open began, %d built | boxed %d | in "
             "view resolved %d (%s) | out of view resolved %d (%s), %d of them while the drain ran"
             % (state["load"], cur["t_drain"] if cur["t_drain"] is not None else -1.0,
                cur["built"], len(cur["boxed"]),
                len(near), " ".join("%.2f" % t for t in sorted(near)),
                len(far), " ".join("%.2f" % t for t in sorted(far)),
                len([n for n in cur["in_drain"] if n.startswith("Far")])))
    except Exception:
        note("ABORT after load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()
        return
    QtCore.QTimer.singleShot(1000, next_load)


def verdict():
    try:
        run = state["runs"][-1]
        names = set(["Near%d" % i for i in range(NEAR)] + ["Far%d" % i for i in range(FAR)])
        near = [run["resolved"][n] for n in run["resolved"] if n.startswith("Near")]
        far = [run["resolved"][n] for n in run["resolved"] if n.startswith("Far")]
        far_in_drain = [n for n in run["in_drain"] if n.startswith("Far")]
        check("both loads ran to the drain's end",
              len(state["runs"]) == LOADS and run["t_drain"] is not None, len(state["runs"]))
        check("the case arose: every big shape was drawn as a box", run["boxed"] == names,
              sorted(run["boxed"]))
        check("the case arose: the near ones are on the screen and the far ones are not",
              all(run["near_seen"]) and not any(run["far_seen"]),
              "near %s far %s" % (run["near_seen"], run["far_seen"]))
        still = run["camera"] is not None and run["camera"] == run["camera_end"]
        check("the case arose: nothing moved the camera", still,
              "" if still else "at the drain's line:\n%s\nat the end:\n%s"
              % (run["camera"], run["camera_end"]))
        check("every boxed shape in view got its coarse mesh", len(near) == NEAR,
              "%d of %d" % (len(near), NEAR))
        if LEISURE:
            check("every boxed shape out of view got its coarse mesh, with no camera move",
                  len(far) == FAR, "%d of %d" % (len(far), FAR))
            check("none of those before the last of the ones in view",
                  len(far) > 0 and len(near) == NEAR and min(far) >= max(near),
                  "in view by %.2f s, out of view from %.2f s"
                  % (max(near) if near else -1.0, min(far) if far else -1.0))
            check("none of those while visuals were still being built", not far_in_drain,
                  sorted(far_in_drain))
        else:
            check("the switch off: no boxed shape out of view got a mesh", len(far) == 0,
                  "%d of %d, %.0f s after the drain's line" % (len(far), FAR, QUIET))
        check("every visual is built", run["objects"] == COUNT and run["made"] == COUNT,
              "%d of %d objects, %d built" % (run["objects"], COUNT, run["made"]))
    except Exception:
        note("ABORT verdict:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def prism(sides, radius, height):
    """A solid of sides + 2 planar faces: slow to turn into a picture."""
    points = []
    for k in range(sides):
        a = 2 * math.pi * k / sides
        r = radius * (1.0 + 0.2 * math.sin(7 * a))
        points.append(FreeCAD.Vector(r * math.cos(a), r * math.sin(a), 0))
    points.append(points[0])
    return Part.Face(Part.makePolygon(points)).extrude(FreeCAD.Vector(0, 0, height))


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        view.SetBool("ShowNaviCube", False)
        view.SetBool("UseNavigationAnimations", False)
        render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
        render.SetBool("ProgressiveLoad", True)
        render.SetBool("CoarseDeferAtLeisure", LEISURE)
        # The lines that name each shape drawn as a box and each mesh in
        render.SetBool("LevelDebug", True)
        FreeCAD.setLogLevel("Part", "Log")
        FreeCAD.Console.AttachObserver(observe)
        doc = FreeCAD.newDocument("BoxesOutOfView")
        placed = [0]

        def place(obj):
            n = placed[0]
            obj.Placement.Base = FreeCAD.Vector(90 * (n % 9), 90 * (n // 9), 0)
            placed[0] += 1

        # The boxed ones first: boxed by the drain's first slices, so that
        # the plans that run while it works on the rest find them
        near = []
        for i in range(NEAR):
            obj = doc.addObject("Part::Feature", "Near%d" % i)
            obj.Shape = prism(1500, 30.0 + 0.01 * i, 12)
            place(obj)
            near.append(obj)
        far = []
        for i in range(FAR):
            obj = doc.addObject("Part::Feature", "Far%d" % i)
            obj.Shape = prism(1500, 31.0 + 0.01 * i, 12)
            far.append(obj)
        for i in range(HEAVY):
            obj = doc.addObject("Part::Feature", "P%d" % i)
            obj.Shape = prism(990, 30.0 + 0.01 * i, 10)
            place(obj)
        for i in range(LIGHT):
            obj = doc.addObject("Part::Sphere", "S%d" % i)
            obj.Radius = 20 + 0.001 * i
            place(obj)
        # The far ones stay hidden for the fit, then go far to the right of
        # what the camera sees: the saved camera does not show them
        for obj in far:
            obj.ViewObject.Visibility = False
        doc.recompute()
        gview = FreeCADGui.ActiveDocument.ActiveView
        gview.viewTop()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        QtCore.QCoreApplication.processEvents()
        right = gview.getCameraOrientation().multVec(FreeCAD.Vector(1, 0, 0))
        for i, obj in enumerate(far):
            obj.Placement.Base = right * (20000.0 + 200.0 * i)
            obj.ViewObject.Visibility = True
        doc.recompute()
        QtCore.QCoreApplication.processEvents()
        doc.saveAs(PATH)
        FreeCAD.closeDocument(doc.Name)
        note("INFO %d boxed in view + %d boxed out of view + %d of 992 faces + %d spheres saved"
             % (NEAR, FAR, HEAVY, LIGHT))
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1500, next_load)


QtCore.QTimer.singleShot(1500, build)
