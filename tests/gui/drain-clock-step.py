"""A step of the wall clock does not stretch a slice of a progressive load.

A progressive load does its GUI work after the open has returned, in
slices on the GUI thread: first the view providers (Gui::Document's
deferred restore), then the Part visuals (ViewProviderPartExt's drain;
docs/DocumentLoad.md sec 13). A slice works until its budget is spent --
Render/ProgressiveLoadBudgetMS, 100 ms -- and gives the thread back to the
event loop. That budget is the load's whole promise to the user: the
window answers ten times a second while the document fills in.

The slices timed themselves with std::chrono::high_resolution_clock, which
in libstdc++ is the time of day. The time of day is not monotonic: where
it is resynced in steps it moves back (this WSL2 box, two time masters
disagreeing: 0.97 s every 32 s). A step back inside a slice left it with a
negative time spent, and it worked on until it had made the step good: a
slice of a tenth of a second ran for more than a second, and the time the
drain reported for itself was short by the step -- below zero for a small
document.

What is done: a document of 2400 solids is made and saved, then opened
three times in one process with a slice budget of 10 ms -- once to warm
up, once as it is, and once with the time of day stepping back 0.97 s
every 100 ms from the moment of the open. The step is made by a
preloaded clock (clock-step-shim.c) whose steps follow the steady clock,
so they fall inside the slices.

What is asserted, from the two drains' own closing lines (their number of
slices, the time they say they spent and their longest slice):
  - the clock did step during the stepped load, and the time of day the
    process reads is the stepped one;
  - each drain of the stepped load ran in about as many slices as the
    plain load's -- a slice that makes a step good swallows the slices
    after it;
  - the time each drain says it spent is not negative, and about the plain
    load's;
  - no slice of the stepped load ran for anything like a step: the longest
    is under 0.4 s (the plain load's longest is a few times the budget --
    a slice ends after the object that spent it);
  - with the clock standing still, no slice of the view provider drain
    runs for ten budgets. One phase of it had no budget at all: the
    records that name an archive entry -- a colour array, so every solid's
    -- are restored inside the open and replayed after the sweep of
    updates, and that replay was one loop over all of them, 0.16 to
    0.32 s here and growing with the document;
  - a load does not time itself by the time of day at all: the shim
    counts the reads, and there were 417000 of them in one load of these
    2400 objects, 174 an object, from the timing macros of Base/Console.h,
    the visual build timers and the restore statistics -- reporters, which
    a step made lie in what they reported. Three are left, this test's
    own;
  - every visual is built at the end of each load.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), with LD_PRELOAD and FC_CLOCK_SHIM naming the shim. Scored
against the tree before the change: see the commit message.
"""
import ctypes
import os
import re
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PATH = os.path.join(OUT, "drain-clock.FCStd")
COUNT = int(os.environ.get("GT_COUNT", "2400"))
BUDGET_MS = int(os.environ.get("GT_BUDGET", "10"))
STEP = -0.97
EVERY = float(os.environ.get("GT_EVERY", "0.1"))
LONGEST = 0.4
# Ten budgets: the slice ends after the object that spent the budget, and
# the progress bar runs events from inside it
UNSLICED = 0.1
# Reads of the time of day a load may make: none of ours, and room for
# whatever a library does
READS = 1000
LOADS = ["warm", "plain", "stepped"]

RESTORE = re.compile(r"progressive restore (\S+): (\d+) view providers in (\d+) slices, "
                     r"([-+.\de]+)s(?:, longest ([-+.\de]+)s)?")
VISUAL = re.compile(r"progressive load (\S+): (\d+) of (\d+) visuals in (\d+) slices, "
                    r"([-+.\de]+)s(?:, longest ([-+.\de]+)s)?")
SUBMIT = re.compile(r"pre-mesh (\S+): (\d+) of (\d+) parked shapes submitted")

state = {"done": False, "load": -1, "shim": None, "runs": {}, "cur": None}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def observe(notifier, msg, level):
    """The drains' own closing lines."""
    cur = state["cur"]
    if cur is None:
        return
    m = RESTORE.search(msg)
    if m:
        cur["restore"] = (int(m.group(3)), float(m.group(4)), int(m.group(2)))
        cur["restore_longest"] = float(m.group(5)) if m.group(5) else None
        cur["t_restore"] = time.monotonic() - cur["t0"]
        return
    m = SUBMIT.search(msg)
    if m:
        cur["submitted"] = (int(m.group(2)), int(m.group(3)))
        return
    m = VISUAL.search(msg)
    if m:
        cur["visual"] = (int(m.group(4)), float(m.group(5)), int(m.group(2)), int(m.group(3)))
        cur["visual_longest"] = float(m.group(6)) if m.group(6) else None
        cur["t_visual"] = time.monotonic() - cur["t0"]
        QtCore.QTimer.singleShot(0, loaded)


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
        cur = {"kind": kind, "steps": 0, "wall": 0.0, "reads": 0}
        state["cur"] = cur
        shim = state["shim"]
        cur["reads0"] = shim.fc_clock_reads()
        cur["wall0"] = time.time() - time.monotonic()
        cur["t0"] = time.monotonic()
        if kind == "stepped":
            shim.fc_clock_schedule(STEP, 0.0, EVERY, 1000)
        doc = FreeCAD.openDocument(PATH)
        cur["name"] = doc.Name
        cur["t_open"] = time.monotonic() - cur["t0"]
        load = state["load"]
        QtCore.QTimer.singleShot(90000, lambda: unfinished(load))
    except Exception:
        note("ABORT load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()


def unfinished(load):
    if state["load"] == load and state["cur"] is not None and not state["done"]:
        note("ABORT load %d: the drains' lines did not come: %s" % (load, state["cur"]))
        finish()


def loaded():
    """Both drains of this load have said their line."""
    cur = state["cur"]
    if cur is None or state["done"]:
        return
    try:
        shim = state["shim"]
        # How far the time of day this process reads has moved against the
        # steady clock over the load, read BEFORE the schedule is ended
        cur["wall"] = (time.time() - time.monotonic()) - cur["wall0"]
        cur["steps"] = shim.fc_clock_unschedule()
        cur["reads"] = shim.fc_clock_reads() - cur["reads0"]
        doc = FreeCAD.getDocument(cur["name"])
        built = 0
        for obj in doc.Objects:
            box = obj.ViewObject.getBoundingBox()
            if box.isValid() and box.DiagonalLength > 0:
                built += 1
        cur["built"] = built
        cur["objects"] = len(doc.Objects)
        state["runs"][cur["kind"]] = cur
        state["cur"] = None
        note("INFO %s: open %.2f s, restore line at %.2f s, visual line at %.2f s | "
             "restore %s slices %.3f s | visual %s slices %.3f s, %s built of %s popped, "
             "submitted %s | longest slice %s / %s s | steps %d, wall moved %.2f s, "
             "wall reads %d"
             % (cur["kind"], cur["t_open"], cur.get("t_restore", -1), cur["t_visual"],
                cur.get("restore", ("-", 0, 0))[0], cur.get("restore", ("-", 0.0, 0))[1],
                cur["visual"][0], cur["visual"][1], cur["visual"][2], cur["visual"][3],
                cur.get("submitted"), cur.get("restore_longest"), cur.get("visual_longest"),
                cur["steps"], cur["wall"], cur["reads"]))
    except Exception:
        note("ABORT after load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()
        return
    QtCore.QTimer.singleShot(700, next_load)


def verdict():
    try:
        runs = state["runs"]
        plain, stepped = runs["plain"], runs["stepped"]
        check("the clock stepped during the stepped load",
              stepped["steps"] >= 2, "%d steps" % stepped["steps"])
        moved = STEP * stepped["steps"]
        check("the time of day the process reads is the stepped one",
              abs(stepped["wall"] - moved) < 0.5 and abs(plain["wall"]) < 0.5,
              "moved %.2f s for %d steps of %.2f s; the plain load %.2f s"
              % (stepped["wall"], stepped["steps"], STEP, plain["wall"]))
        for key, what in (("restore", "the view provider drain"), ("visual", "the visual drain")):
            ps, pt = plain[key][0], plain[key][1]
            ss, st = stepped[key][0], stepped[key][1]
            check("%s has slices to lose" % what, ps >= 6, "%d slices in the plain load" % ps)
            check("%s runs in as many slices with the clock stepping" % what,
                  ss >= 0.5 * ps, "%d slices against %d" % (ss, ps))
            check("%s does not report a negative time" % what, st >= 0.0, "%.3f s" % st)
            check("%s reports the time it spent" % what,
                  0.4 * pt <= st <= 2.5 * pt, "%.3f s against %.3f s" % (st, pt))
            longest = stepped[key + "_longest"]
            check("%s has no slice the length of a step" % what,
                  longest is not None and longest < LONGEST,
                  "longest %s s with the clock stepping, %s s without"
                  % (longest, plain[key + "_longest"]))
        longest = plain["restore_longest"]
        check("the view provider drain has no phase without a budget",
              longest is not None and longest < UNSLICED,
              "longest slice %s s under a budget of %.3f s" % (longest, BUDGET_MS / 1000.0))
        check("a load does not time itself by the time of day",
              plain["reads"] < READS and stepped["reads"] < READS,
              "%d and %d reads of it in the plain and the stepped load"
              % (plain["reads"], stepped["reads"]))
        for kind in LOADS:
            run = runs[kind]
            check("every visual is built after the %s load" % kind,
                  run["objects"] == COUNT and run["built"] == COUNT,
                  "%d of %d objects, %d built" % (run["objects"], COUNT, run["built"]))
    except Exception:
        note("ABORT verdict:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    if state["shim"] is not None:
        state["shim"].fc_clock_unschedule()
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def build():
    try:
        shim = ctypes.CDLL(os.environ["FC_CLOCK_SHIM"])
        shim.fc_clock_schedule.argtypes = [ctypes.c_double, ctypes.c_double, ctypes.c_double,
                                           ctypes.c_int]
        shim.fc_clock_schedule.restype = None
        shim.fc_clock_unschedule.restype = ctypes.c_long
        shim.fc_clock_reads.restype = ctypes.c_long
        state["shim"] = shim

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        view.SetBool("ShowNaviCube", False)
        view.SetBool("UseNavigationAnimations", False)
        render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
        render.SetBool("ProgressiveLoad", True)
        render.SetInt("ProgressiveLoadBudgetMS", BUDGET_MS)
        # The lines read are log lines of the two modules
        FreeCAD.setLogLevel("Part", "Log")
        FreeCAD.setLogLevel("Gui", "Log")
        FreeCAD.Console.AttachObserver(observe)
        doc = FreeCAD.newDocument("DrainClock")
        for i in range(COUNT):
            kind = i % 3
            if kind == 0:
                obj = doc.addObject("Part::Torus", "T%d" % i)
                obj.Radius1 = 20 + (i % 7)
                # Below the smallest Radius1 for every i: a tube wider than
                # its ring is no torus, and an object without a shape has
                # no visual to build
                obj.Radius2 = 3 + 0.001 * i
            elif kind == 1:
                obj = doc.addObject("Part::Sphere", "S%d" % i)
                obj.Radius = 8 + 0.01 * i
            else:
                obj = doc.addObject("Part::Cylinder", "C%d" % i)
                obj.Radius = 6 + 0.01 * i
                obj.Height = 30
            obj.Placement.Base = FreeCAD.Vector(70 * (i % 25), 70 * (i // 25), 0)
        doc.recompute()
        doc.saveAs(PATH)
        FreeCAD.closeDocument(doc.Name)
        note("INFO %d objects saved, budget %d ms, a step of %.2f s every %.2f s"
             % (COUNT, BUDGET_MS, STEP, EVERY))
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1000, next_load)


QtCore.QTimer.singleShot(1500, build)
