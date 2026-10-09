"""A box asked for at leisure moves to the front when the camera turns to it.

A shape a load drew as a bounding box and the camera does not see is asked
for at leisure: its coarse mesh is made behind everything asked for in
view (docs/DocumentLoad.md sec 18.13, tests/gui/boxes-out-of-view.py). The
jobs of several such shapes stand in a line, and each is a large shape. A
camera that turns to the last of them must not find it waiting behind the
others: the plan that sees it turns its ask into one for the view, and the
job it already has moves ahead of the ones still at leisure.

What is done: a document of one sphere in view and 8 plates of 1100 round
holes far to the right of it -- over the stand-in threshold, and a good
part of a second each to mesh -- is made and saved, then opened twice with
one worker (Render/LevelThreads 1), so that the line is long enough to
jump. In the second load, as soon as a plan has asked for the plates at
leisure, the camera is fitted to the LAST of them.

What is asserted of the second load:
  - the case arose: all 8 were drawn as boxes, none of them on the screen
    before the turn, a plan had asked for them at leisure, and at the turn
    the last one and at least 4 others had no mesh yet;
  - the last one is on the screen after the turn, and got its mesh;
  - ahead of the line: of the others without a mesh at the turn, at most
    two got theirs before it (one a worker had in hand, and one for the
    0.3 s the plan waits for the camera to settle);
  - the others got theirs too, with the camera where it then stood;
  - every visual is built.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout). The move itself was checked by taking it out: see the commit
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
PATH = os.path.join(OUT, "boxes-out-of-view-turn.FCStd")
FAR = int(os.environ.get("GT_FAR", "8"))
HOLES = int(os.environ.get("GT_HOLES", "1100"))
LOADS = 2
QUIET = float(os.environ.get("GT_QUIET", "20"))
TARGET = "Far%d" % (FAR - 1)

VISUAL = re.compile(r"progressive load (\S+): (\d+) of (\d+) visuals in (\d+) slices")
BOXED = re.compile(r"#(\w+)\.ViewObject bounding-box stand-in")
RESOLVED = re.compile(r"#(\w+)\.ViewObject stand-in resolved")
LEISURE = re.compile(r"plan: refine \d+ demote \d+ downgrade \d+ at leisure (\d+)")

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
        if m.group(1) not in cur["resolved"]:
            cur["resolved"][m.group(1)] = now - cur["t0"]
            cur["order"].append(m.group(1))
        cur["last"] = now
        return
    m = LEISURE.search(msg)
    if m:
        if int(m.group(1)) > 0 and cur["t_leisure"] is None:
            cur["t_leisure"] = now - cur["t0"]
        return
    m = VISUAL.search(msg)
    if m:
        cur["t_drain"] = now - cur["t0"]
        cur["last"] = now


def on_screen(obj):
    """Whether the centre of the object's box projects into the view."""
    view = FreeCADGui.ActiveDocument.ActiveView
    width, height = view.getSize()
    x, y = view.getPointOnViewport(obj.Shape.BoundBox.Center)
    return 0 <= x < width and 0 <= y < height


def turn(cur):
    """Fit the camera to the last plate: what a user looking for it does."""
    doc = FreeCAD.getDocument(cur["name"])
    cur["far_seen_before"] = [on_screen(doc.getObject("Far%d" % i)) for i in range(FAR)]
    cur["pending_at_turn"] = [n for n in ("Far%d" % i for i in range(FAR))
                              if n not in cur["resolved"]]
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(doc.Name, TARGET)
    FreeCADGui.SendMsgToActiveView("ViewSelection")
    FreeCADGui.Selection.clearSelection()
    cur["t_turn"] = time.monotonic() - cur["t0"]
    cur["last"] = time.monotonic()


def poll():
    cur = state["cur"]
    if cur is None or state["done"]:
        return
    now = time.monotonic()
    try:
        if (state["load"] == LOADS - 1 and cur["t_turn"] is None
                and cur["t_drain"] is not None and cur["t_leisure"] is not None):
            turn(cur)
    except Exception:
        note("ABORT turn:\n" + traceback.format_exc())
        finish()
        return
    settled = cur["t_drain"] is not None and (
        len(cur["resolved"]) == FAR or now - cur["last"] > QUIET)
    if settled or now - cur["t0"] > 200:
        loaded(cur)
        return
    QtCore.QTimer.singleShot(20, poll)


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
        cur = {"boxed": set(), "resolved": {}, "order": [], "t_drain": None,
               "t_leisure": None, "t_turn": None, "far_seen_before": [],
               "pending_at_turn": []}
        cur["t0"] = time.monotonic()
        cur["last"] = cur["t0"]
        state["cur"] = cur
        doc = FreeCAD.openDocument(PATH)
        cur["name"] = doc.Name
        QtCore.QTimer.singleShot(20, poll)
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
        cur["target_seen"] = on_screen(doc.getObject(TARGET))
        state["runs"].append(cur)
        note("INFO load %d: drain's line %.2f s | asked at leisure %.2f s | turned %.2f s with %d "
             "of %d without a mesh | meshes in: %s"
             % (state["load"], cur["t_drain"] if cur["t_drain"] is not None else -1.0,
                cur["t_leisure"] if cur["t_leisure"] is not None else -1.0,
                cur["t_turn"] if cur["t_turn"] is not None else -1.0,
                len(cur["pending_at_turn"]), FAR,
                " ".join("%s@%.2f" % (n, cur["resolved"][n]) for n in cur["order"])))
    except Exception:
        note("ABORT after load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()
        return
    QtCore.QTimer.singleShot(1000, next_load)


def verdict():
    try:
        run = state["runs"][-1]
        names = set("Far%d" % i for i in range(FAR))
        pending = run["pending_at_turn"]
        others = [n for n in pending if n != TARGET]
        order = run["order"]
        check("both loads ran to the drain's end",
              len(state["runs"]) == LOADS and run["t_drain"] is not None, len(state["runs"]))
        check("the case arose: every plate was drawn as a box", run["boxed"] == names,
              sorted(run["boxed"]))
        check("the case arose: none of them on the screen before the turn",
              run["t_turn"] is not None and not any(run["far_seen_before"]),
              run["far_seen_before"])
        check("the case arose: a plan had asked for them at leisure",
              run["t_leisure"] is not None and run["t_turn"] is not None
              and run["t_leisure"] <= run["t_turn"])
        check("the case arose: at the turn the last one and 4 others or more had no mesh",
              TARGET in pending and len(others) >= 4, pending)
        check("the last one is on the screen after the turn", run["target_seen"])
        check("the last one got its mesh", TARGET in run["resolved"])
        ahead = -1
        if TARGET in order:
            ahead = len([n for n in order[:order.index(TARGET)] if n in others])
        check("ahead of the line: at most two of the others got theirs before it",
              0 <= ahead <= 2 and len(others) >= 4,
              "%d of the %d others without a mesh at the turn" % (ahead, len(others)))
        check("the others got theirs too", set(run["resolved"]) == names,
              "%d of %d" % (len(run["resolved"]), FAR))
        check("every visual is built", run["objects"] == FAR + 1 and run["made"] == FAR + 1,
              "%d objects, %d built" % (run["objects"], run["made"]))
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


def plate(index):
    """A plate of HOLES round holes: HOLES + 6 faces, and slow to mesh."""
    side = int(math.ceil(math.sqrt(HOLES)))
    pitch = 10.0 + 0.01 * index
    size = pitch * (side + 1)
    wires = [Part.makePolygon([FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(size, 0, 0),
                               FreeCAD.Vector(size, size, 0), FreeCAD.Vector(0, size, 0),
                               FreeCAD.Vector(0, 0, 0)])]
    for k in range(HOLES):
        circle = Part.Circle(FreeCAD.Vector(pitch * (k % side + 1), pitch * (k // side + 1), 0),
                             FreeCAD.Vector(0, 0, 1), 3.0)
        hole = Part.Wire([circle.toShape()])
        # A hole runs the other way round than the outline
        hole.reverse()
        wires.append(hole)
    return Part.Face(wires).extrude(FreeCAD.Vector(0, 0, 5))


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        view.SetBool("ShowNaviCube", False)
        view.SetBool("UseNavigationAnimations", False)
        render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
        render.SetBool("ProgressiveLoad", True)
        # One worker: the line of jobs at leisure is worked one at a time
        render.SetInt("LevelThreads", 1)
        # The lines that name each shape drawn as a box, each mesh in, and
        # what each plan asked for
        render.SetBool("LevelDebug", True)
        FreeCAD.setLogLevel("Part", "Log")
        FreeCAD.Console.AttachObserver(observe)
        doc = FreeCAD.newDocument("BoxesTurn")
        ball = doc.addObject("Part::Sphere", "S0")
        ball.Radius = 50
        far = []
        for i in range(FAR):
            obj = doc.addObject("Part::Feature", "Far%d" % i)
            obj.Shape = plate(i)
            obj.ViewObject.Visibility = False
            far.append(obj)
        doc.recompute()
        gview = FreeCADGui.ActiveDocument.ActiveView
        gview.viewTop()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        QtCore.QCoreApplication.processEvents()
        right = gview.getCameraOrientation().multVec(FreeCAD.Vector(1, 0, 0))
        for i, obj in enumerate(far):
            obj.Placement.Base = right * (20000.0 + 3000.0 * i)
            obj.ViewObject.Visibility = True
        doc.recompute()
        QtCore.QCoreApplication.processEvents()
        doc.saveAs(PATH)
        FreeCAD.closeDocument(doc.Name)
        note("INFO 1 sphere in view + %d plates of %d holes out of view saved" % (FAR, HOLES))
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1500, next_load)


QtCore.QTimer.singleShot(1500, build)
