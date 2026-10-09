"""A frame takes in a bounded number of edge sets, and the rest in the frames after.

An edge set that is not on the GPU yet is a buffer to build and to hand
the driver in the frame that first draws it, and there are moments when
thousands are first drawn in one frame: the frame after a load, which
holds every edge and vertex back while it fills in (Render/
LoadDropElements, docs/SceneStreaming.md 13b), a camera fitted to an
assembly it showed a corner of, a document opened whole. On a 17000-
object assembly the frame after the load took in 14500 sets and was a
single call of 8.8 s into the driver, the longest stall of the whole
load (docs/DocumentLoad.md sec 18.14).

A frame now takes in a bounded number of the edge and point sets that are
not on the GPU yet (Render/ElementTakeInSets, ElementTakeInKB), holds the
rest back and asks for the next frame, which takes in as many again
(sec 18.17). A frame that holds sets back is not a finished picture, and
says so.

What is done: a document of 1200 boxes is made, fitted and saved, and
opened whole -- no progressive load, every mesh exact from the start, so
that the first frame after the open has every edge set to draw and none
of them uploaded -- once to warm up and once to be read, with a frame
allowed 100 sets. The renderer's own line is read (Render/LevelDebug):
"element gates", which says how many edge draws are held back each time
that number changes.

What is asserted:
  - the case arose: the frames after the open had an edge set for every
    box;
  - the number of edge draws not yet drawn came down by no more than a
    few frames' worth at a time (three times the bound: a line is
    written when the number changes), from the first frame on;
  - it took at least a third of the frames the bound makes of it;
  - every edge is drawn in the end;
  - a frame was not a finished picture while sets were held back, and the
    view does come to one.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout). The way it was is GT_SET=0 -- the bound not set, every set in
the frame that first draws it -- and that is what the test was scored
against: see the commit message.
"""
import os
import re
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PATH = os.path.join(OUT, "take-in.FCStd")
COUNT = int(os.environ.get("GT_COUNT", "1200"))
BOUND = int(os.environ.get("GT_BOUND", "100"))
# What the parameter is set to: 0 for the way it was, every set in the
# frame that first draws it
SET = int(os.environ.get("GT_SET", str(BOUND)))
LOADS = ["warm", "read"]

GATES = re.compile(r"render levels: element gates.*?: (\d+) eligible, suppressed (\d+) point "
                   r"\+ (\d+) line")

state = {"done": False, "load": -1, "runs": {}, "cur": None}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def view_of(cur):
    doc = FreeCADGui.getDocument(cur["name"])
    return doc.ActiveView if doc else None


def observe(notifier, msg, level):
    cur = state["cur"]
    if cur is None or "t_open" not in cur:
        return
    m = GATES.search(msg)
    if not m:
        return
    now = time.monotonic() - cur["t0"]
    eligible, lines = int(m.group(1)), int(m.group(3))
    cur["lines"].append((now, eligible, lines))
    if lines > 0 and len(cur["incomplete"]) < 400:
        # Asked from inside the frame's own line: the verdict of the frame
        # before, which held sets back as well once the first has
        try:
            view = view_of(cur)
            if view is not None:
                cur["incomplete"].append(not view.isFrameComplete())
        except Exception:
            cur["incomplete"].append(None)


def next_load():
    if state["done"]:
        return
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCAD.closeDocument(name)
        state["load"] += 1
        if state["load"] >= len(LOADS):
            verdict()
            return
        kind = LOADS[state["load"]]
        cur = {"kind": kind, "lines": [], "incomplete": [], "t0": time.monotonic()}
        state["cur"] = cur
        doc = FreeCAD.openDocument(PATH)
        cur["name"] = doc.Name
        cur["objects"] = len(doc.Objects)
        # The frames after the open are the ones read
        cur["t_open"] = time.monotonic() - cur["t0"]
        cur["deadline"] = time.monotonic() + 120.0
        QtCore.QTimer.singleShot(200, settle)
    except Exception:
        note("ABORT load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()


def settle():
    """Over when the number of edges held back has stood still for two
    seconds, a frame having been drawn."""
    cur = state["cur"]
    if cur is None or state["done"]:
        return
    try:
        now = time.monotonic()
        t = now - cur["t0"]
        if cur["lines"] and t - cur["lines"][-1][0] > 2.0:
            loaded()
            return
        if now > cur["deadline"]:
            note("INFO %s: not settled after 120 s, %d lines of the gates"
                 % (cur["kind"], len(cur["lines"])))
            loaded()
            return
    except Exception:
        note("ABORT settle:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(200, settle)


def loaded():
    cur = state["cur"]
    try:
        view = view_of(cur)
        t0 = time.monotonic()
        cur["complete"] = bool(view.waitFrameComplete(60000)) if view is not None else False
        cur["t_complete"] = time.monotonic() - t0
        state["runs"][cur["kind"]] = cur
        state["cur"] = None
        note("INFO %s: open %.2f s, %d objects | eligible sets %d | edge draws held back: %s | "
             "complete at the end %s (waited %.2f s)"
             % (cur["kind"], cur["t_open"], cur["objects"],
                max([e for t, e, n in cur["lines"]] or [0]),
                " ".join("%d@%.2f" % (n, t) for t, e, n in cur["lines"][:60]),
                cur["complete"], cur["t_complete"]))
    except Exception:
        note("ABORT after load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()
        return
    QtCore.QTimer.singleShot(700, next_load)


def verdict():
    try:
        run = state["runs"]["read"]
        eligible = max([e for t, e, n in run["lines"]] or [0])
        check("the case arose: the frames after the open had an edge set for every box",
              run["objects"] == COUNT and eligible >= COUNT,
              "%d objects, %d sets the gates count" % (run["objects"], eligible))
        # From an edge set a box, none of them drawn, to nothing held
        # back, by the lines written
        left = [COUNT] + [n for t, e, n in run["lines"] if e >= COUNT]
        steps = [a - b for a, b in zip(left, left[1:]) if a > b]
        worst = max(steps or [0])
        check("the edges come in a bounded number at a time",
              bool(steps) and worst <= 3 * BOUND,
              "the largest step %d of %d steps, a frame takes in %d sets"
              % (worst, len(steps), BOUND))
        want = COUNT / float(BOUND) / 3.0
        check("the edges come in over several frames",
              len(steps) >= want, "%d steps, %.0f or more expected" % (len(steps), want))
        check("every edge is drawn in the end", len(left) > 1 and left[-1] == 0,
              "%d edge draws still held back" % left[-1])
        seen = [v for v in run["incomplete"] if v is not None]
        check("a frame that holds sets back is not a finished picture",
              len(seen) >= 3 and sum(1 for v in seen if v) >= len(seen) - 2,
              "%d of %d frames asked said so" % (sum(1 for v in seen if v), len(seen)))
        check("the view comes to a finished picture", run["complete"],
              "waited %.2f s" % run["t_complete"])
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


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        view.SetBool("ShowNaviCube", False)
        view.SetBool("UseNavigationAnimations", False)
        render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
        # Opened whole, and every mesh exact from the start: an edge set
        # is not drawn over faces that are a coarse rung still
        render.SetBool("ProgressiveLoad", False)
        render.SetInt("CoarseTessellation", -1)
        render.SetBool("LevelDebug", True)
        render.SetInt("ElementTakeInSets", SET)
        render.SetInt("ElementTakeInKB", 0)
        FreeCAD.Console.AttachObserver(observe)
        doc = FreeCAD.newDocument("TakeIn")
        side = 40
        for i in range(COUNT):
            obj = doc.addObject("Part::Box", "B%d" % i)
            obj.Length = 8 + (i % 5)
            obj.Width = 8 + (i % 3)
            obj.Height = 6 + (i % 7)
            obj.Placement.Base = FreeCAD.Vector(20 * (i % side), 20 * (i // side), 0)
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        doc.saveAs(PATH)
        FreeCAD.closeDocument(doc.Name)
        note("INFO %d boxes saved, a frame takes in %d sets (asserted: %d)" % (COUNT, SET, BOUND))
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1000, next_load)


QtCore.QTimer.singleShot(1500, build)
