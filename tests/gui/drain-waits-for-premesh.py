"""The visual drain waits for a pre-mesh in flight; it does not turn.

A progressive load builds its Part visuals in slices on the GUI thread,
and hands their shapes to workers first, which mesh private twins
(docs/DocumentLoad.md sec 18). A visual whose shape a worker still has is
not built: it is put back on the queue, and comes round again.

With nothing else left to build, coming round again was all the drain did.
Every turn of the event loop popped the one visual, asked, put it back and
posted itself again: a document of three plates that take a second or two
to mesh ran 13000 to 20000 "slices" with the GUI thread pegged for as long
as the workers took, said "3 of 20223 visuals" in its closing line, and
stepped its progress bar twenty thousand times for a total of three.

Now the drain knows when it has asked about everything it has left since
anything last ended, and waits: it looks at the pre-mesh's count of ended
claims a hundred times a second, and asks again when the count has moved.
A visual put back is counted as that, not as popped twice, and the
progress bar steps once for each visual.

What is done: a document of two plates -- one planar face with 1500 round
holes each, about a second to mesh, far under the face count at which a
shape takes the stand-in path instead -- and twenty spheres is made and
saved, then opened three times in one process, once to warm up.

What is asserted of each of the two loads after it, from the drain's own
closing line and the GUI thread's CPU clock:
  - the case arose: a visual was asked for with its shape in flight, and
    the drain took long enough for a wait to show;
  - every visual is counted once: as many popped as built, as many built
    as the document has;
  - the drain ran in a number of slices that is of the document, not of
    the wait;
  - the GUI thread rested: from the pre-mesh's submit to the drain's end
    it used less than half of the time that passed;
  - every visual is built at the end.

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
PATH = os.path.join(OUT, "drain-waits.FCStd")
PLATES = int(os.environ.get("GT_PLATES", "2"))
HOLES = int(os.environ.get("GT_HOLES", "1500"))
SPHERES = int(os.environ.get("GT_SPHERES", "20"))
COUNT = PLATES + SPHERES
LOADS = 3
SLICES = 100

VISUAL = re.compile(r"progressive load (\S+): (\d+) of (\d+) visuals in (\d+) slices, "
                    r"([-+.\de]+)s(?:, longest ([-+.\de]+)s)?(?:, (\d+) put back)?")
SUBMIT = re.compile(r"pre-mesh (\S+): (\d+) of (\d+) parked shapes submitted")
MESHED = re.compile(r"pre-mesh (\S+): (\d+) of (\d+) claimed shapes meshed in ([-+.\de]+)s")

state = {"done": False, "load": -1, "cur": None, "runs": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def observe(notifier, msg, level):
    """The drain's own lines, read on the GUI thread that says them."""
    cur = state["cur"]
    if cur is None:
        return
    m = SUBMIT.search(msg)
    if m:
        cur["submitted"] = int(m.group(2))
        cur["t_submit"] = time.monotonic()
        cur["cpu_submit"] = time.thread_time()
        return
    m = MESHED.search(msg)
    if m:
        cur["batch"] = float(m.group(4))
        return
    m = VISUAL.search(msg)
    if m:
        cur["t_end"] = time.monotonic()
        cur["cpu_end"] = time.thread_time()
        cur["built"], cur["popped"] = int(m.group(2)), int(m.group(3))
        cur["slices"] = int(m.group(4))
        cur["putback"] = int(m.group(7)) if m.group(7) else None
        QtCore.QTimer.singleShot(0, loaded)


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
        state["cur"] = {}
        doc = FreeCAD.openDocument(PATH)
        state["cur"]["name"] = doc.Name
        load = state["load"]
        QtCore.QTimer.singleShot(60000, lambda: unfinished(load))
    except Exception:
        note("ABORT load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()


def unfinished(load):
    if state["load"] == load and state["cur"] is not None and not state["done"]:
        note("ABORT load %d: the drain's line did not come: %s" % (load, state["cur"]))
        finish()


def loaded():
    cur = state["cur"]
    if cur is None or state["done"]:
        return
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
        cur["wall"] = cur["t_end"] - cur.get("t_submit", cur["t_end"])
        cur["cpu"] = cur["cpu_end"] - cur.get("cpu_submit", cur["cpu_end"])
        state["runs"].append(cur)
        note("INFO load %d: %d built of %d popped in %d slices, put back %s | submit to end "
             "%.2f s, GUI thread %.2f s | %s submitted, batch %s s"
             % (state["load"], cur["built"], cur["popped"], cur["slices"], cur["putback"],
                cur["wall"], cur["cpu"], cur.get("submitted"), cur.get("batch")))
    except Exception:
        note("ABORT after load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()
        return
    QtCore.QTimer.singleShot(700, next_load)


def verdict():
    try:
        for index, run in enumerate(state["runs"]):
            if index == 0:
                continue
            tag = "load %d: " % index
            asked = run["putback"] if run["putback"] is not None else run["popped"] - run["built"]
            check(tag + "a visual was asked for with its shape in flight", asked >= 1,
                  "%d put back" % asked)
            check(tag + "the drain took long enough for a wait to show", run["wall"] >= 0.4,
                  "%.2f s from the submit to its end" % run["wall"])
            check(tag + "every visual is counted once",
                  run["built"] == COUNT and run["popped"] == COUNT,
                  "%d built of %d popped, %d objects" % (run["built"], run["popped"], COUNT))
            check(tag + "the drain's slices are of the document, not of the wait",
                  run["slices"] <= SLICES, "%d slices" % run["slices"])
            check(tag + "the GUI thread rested while the workers meshed",
                  run["cpu"] <= 0.5 * run["wall"],
                  "%.2f s of %.2f s" % (run["cpu"], run["wall"]))
            check(tag + "every visual is built",
                  run["objects"] == COUNT and run["made"] == COUNT,
                  "%d of %d objects, %d built" % (run["objects"], COUNT, run["made"]))
        check("both loads were measured", len(state["runs"]) == LOADS, len(state["runs"]))
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
    """One planar face with HOLES round holes: a second to mesh, and one face."""
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
    return Part.Face(wires)


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        view.SetBool("ShowNaviCube", False)
        view.SetBool("UseNavigationAnimations", False)
        render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
        render.SetBool("ProgressiveLoad", True)
        render.SetBool("PreMeshOnLoad", True)
        # The lines read are log lines of the Part module
        FreeCAD.setLogLevel("Part", "Log")
        FreeCAD.Console.AttachObserver(observe)
        doc = FreeCAD.newDocument("DrainWaits")
        valid = 0
        for i in range(PLATES):
            obj = doc.addObject("Part::Feature", "Plate%d" % i)
            obj.Shape = plate(i)
            obj.Placement.Base = FreeCAD.Vector(0, 0, 40 * (i + 1))
            valid += 1 if obj.Shape.isValid() and len(obj.Shape.Faces) == 1 else 0
        for i in range(SPHERES):
            obj = doc.addObject("Part::Sphere", "S%d" % i)
            obj.Radius = 8 + 0.01 * i
            obj.Placement.Base = FreeCAD.Vector(30 * i, -40, 0)
        doc.recompute()
        doc.saveAs(PATH)
        FreeCAD.closeDocument(doc.Name)
        if not check("the plates are valid shapes of one face", valid == PLATES,
                     "%d of %d" % (valid, PLATES)):
            finish()
            return
        note("INFO %d plates of %d holes and %d spheres saved" % (PLATES, HOLES, SPHERES))
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1000, next_load)


QtCore.QTimer.singleShot(1500, build)
