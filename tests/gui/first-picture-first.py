"""A load gives every object its first picture before it refines any.

The goal of a load is that every object is on the screen, coarse or not,
as soon as possible; waste of processing and of memory is tolerated for it
(the user's ruling, docs/DocumentLoad.md sec 18.11). Two kinds of work
want the GUI thread while a progressive load fills in:

- the drain of parked visuals, which gives an object its FIRST picture --
  and, for a shape over Render/CoarseDeferFaces faces, which is drawn as a
  bounding box by the drain, the landing of its coarse mesh from the
  refine pool;
- the landing of an exact mesh for an object that already has a coarse
  picture, which rebuilds its visual.

They ran as they came. With the camera close enough that everything asks
for its exact mesh, the landings of the second kind took 5.8 to 7.5 s of
the GUI thread while the drain was still running, and the last object had
its picture 12 to 20 s after the open where it has it after 8 to 12 s
with them held.

Now a refinement's landing waits while the application says visuals are
still being built, and the coarse mesh of a boxed shape, which is a first
picture, does not wait and goes ahead of the refinements wherever they
queue. The workers go on meshing refinements in the meantime; what they
make stands in memory until the drain is through.

What is done: a document of 4 solids over the stand-in threshold, 60
solids of 992 faces and 60 spheres is made, the view fitted, and saved;
then opened twice with Render/LevelTolerance at 0.01, so that every
source asks for its exact mesh. The level debug lines name every visual
rebuild with what ran it ("drain 1" the drain, "pump 1" a landing).

What is asserted of the second load:
  - the case arose: refinements were asked for and did land -- at least
    as many rebuilds from landings as there are solids of 992 faces;
  - none of them before the drain's closing line. A boxed shape's first
    rebuild from a landing is its first picture and is not one of them;
  - every boxed shape got its coarse mesh, and while the drain was still
    running: a first picture is not held with the refinements;
  - every visual is built.

Not covered: the ORDER of the jobs on the workers' queue (a boxed shape's
coarse mesh ahead of the refinements standing there). Seen in the
measurements, not asserted here.

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
PATH = os.path.join(OUT, "first-picture.FCStd")
BIG = 4
HEAVY = 60
LIGHT = 60
COUNT = BIG + HEAVY + LIGHT
LOADS = 2
QUIET = 5.0

VISUAL = re.compile(r"progressive load (\S+): (\d+) of (\d+) visuals in (\d+) slices")
BUILD = re.compile(r"slow visual build: \S+#(\w+)\.ViewObject .* drain (\d) pump (\d)")
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
    m = RESOLVED.search(msg)
    if m:
        # Its rebuild follows in the same landing: the first picture
        cur["resolved"][m.group(1)] = now - cur["t0"]
        cur["first_due"].add(m.group(1))
        cur["last"] = now
        if cur["t_drain"] is None:
            cur["resolved_in_drain"] += 1
        return
    m = BUILD.search(msg)
    if m:
        cur["last"] = now
        if m.group(3) != "1":
            return
        name = m.group(1)
        if name in cur["first_due"]:
            cur["first_due"].discard(name)
            return
        cur["refined"] += 1
        if cur["t_drain"] is None:
            cur["refined_in_drain"] += 1
            if len(cur["early"]) < 5:
                cur["early"].append(name)
        return
    m = VISUAL.search(msg)
    if m:
        cur["t_drain"] = now - cur["t0"]
        cur["built"] = int(m.group(2))
        cur["last"] = now


def poll():
    cur = state["cur"]
    if cur is None or state["done"]:
        return
    now = time.monotonic()
    if (cur["t_drain"] is not None and now - cur["last"] > QUIET) or now - cur["t0"] > 150:
        loaded(cur)
        return
    QtCore.QTimer.singleShot(250, poll)


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
        cur = {"resolved": {}, "first_due": set(), "refined": 0, "refined_in_drain": 0,
               "resolved_in_drain": 0, "early": [], "t_drain": None, "built": 0}
        cur["t0"] = time.monotonic()
        cur["last"] = cur["t0"]
        state["cur"] = cur
        doc = FreeCAD.openDocument(PATH)
        cur["name"] = doc.Name
        QtCore.QTimer.singleShot(250, poll)
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
        state["runs"].append(cur)
        last = max(cur["resolved"].values()) if cur["resolved"] else -1.0
        note("INFO load %d: drain's line %.2f s after the open began, %d built | boxed shapes "
             "resolved %d (%d while the drain ran), the last at %.2f s | refinements landed %d, "
             "%d before the drain's line %s"
             % (state["load"], cur["t_drain"] if cur["t_drain"] is not None else -1.0,
                cur["built"], len(cur["resolved"]), cur["resolved_in_drain"], last,
                cur["refined"], cur["refined_in_drain"], cur["early"]))
    except Exception:
        note("ABORT after load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()
        return
    QtCore.QTimer.singleShot(1000, next_load)


def verdict():
    try:
        run = state["runs"][-1]
        check("both loads ran to the drain's end",
              len(state["runs"]) == LOADS and run["t_drain"] is not None, len(state["runs"]))
        check("refinements were asked for, and landed", run["refined"] >= HEAVY,
              "%d rebuilds from landings for %d solids of 992 faces" % (run["refined"], HEAVY))
        check("no refinement landed while visuals were still being built",
              run["refined_in_drain"] == 0,
              "%d before the drain's line, the first of them %s"
              % (run["refined_in_drain"], run["early"]))
        check("every boxed shape got its coarse mesh", len(run["resolved"]) == BIG,
              "%d of %d" % (len(run["resolved"]), BIG))
        check("a first picture is not held with the refinements",
              run["resolved_in_drain"] == BIG,
              "%d of %d resolved while the drain ran" % (run["resolved_in_drain"], BIG))
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
        # Every source asks for its exact mesh, as with the camera close
        render.SetFloat("LevelTolerance", 0.01)
        # The lines that name each rebuild and what ran it
        render.SetBool("LevelDebug", True)
        render.SetInt("LevelSlowBuildMS", 1)
        FreeCAD.setLogLevel("Part", "Log")
        FreeCAD.Console.AttachObserver(observe)
        doc = FreeCAD.newDocument("FirstPicture")
        placed = [0]

        def place(obj):
            n = placed[0]
            obj.Placement.Base = FreeCAD.Vector(90 * (n % 12), 90 * (n // 12), 0)
            placed[0] += 1

        # Over the stand-in threshold first: boxed by the drain's first
        # slices, their coarse meshes land while it works on the rest
        for i in range(BIG):
            obj = doc.addObject("Part::Feature", "Big%d" % i)
            obj.Shape = prism(1500, 30.0 + 0.01 * i, 12)
            place(obj)
        for i in range(HEAVY):
            obj = doc.addObject("Part::Feature", "P%d" % i)
            obj.Shape = prism(990, 30.0 + 0.01 * i, 10)
            place(obj)
        for i in range(LIGHT):
            obj = doc.addObject("Part::Sphere", "S%d" % i)
            obj.Radius = 20 + 0.001 * i
            place(obj)
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        QtCore.QCoreApplication.processEvents()
        doc.saveAs(PATH)
        FreeCAD.closeDocument(doc.Name)
        note("INFO %d boxed + %d of 992 faces + %d spheres saved" % (BIG, HEAVY, LIGHT))
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1500, next_load)


QtCore.QTimer.singleShot(1500, build)
