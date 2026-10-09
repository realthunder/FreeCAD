"""A slice of a progressive load grows with what the event loop costs.

A progressive load does its GUI work after the open has returned, in
slices on the GUI thread: first the view providers (Gui::Document's
deferred restore), then the Part visuals (ViewProviderPartExt's drain;
docs/DocumentLoad.md sec 13). A slice worked until its budget was spent --
Render/ProgressiveLoadBudgetMS, 100 ms -- and gave the thread back to the
event loop, which draws a frame before the next slice gets its turn.

Where a frame is quick that is the whole promise: the window answers ten
times a second. Where a frame is dear -- a large assembly, a second and
more a frame while it fills in -- a fixed slice is a fixed, small share of
the thread, and the load takes as long as its number of slices times a
frame. The landing pump of the level meshes had the same arithmetic and
got a rule for it (Gui::turnBudget, docs/DocumentLoad.md sec 18.12 and
18.16): with work left over, a turn may run a share of the time the event
loop then took to give the thread back, up to five budgets and never less
than one. The two drains take their turns by that rule now, and their
share is the whole of it.

And the frame is not drawn where one would look for it. A slice posts the
next slice as it ends, and that is served before the timer that would
draw the frame; the frame is drawn when the next slice's progress bar
lets events through, a fifth of a second after it last did -- INSIDE the
slice. The slice charged itself for it, was over its budget before it had
done anything, and gave up after one object; and to the rule above the
event loop between two slices cost nothing. So the time a slice spends
letting events through is not its own (Gui::TurnPace::Yield), and is the
event loop's cost for the slice after.

What is done: a document of 2400 solids is made and saved, then opened
three times in one process with a slice budget of 10 ms -- once to warm
up, once as it is, and once with a frame that costs 120 ms. The frame is
this script's own, and made to behave as a frame does: the script runs
the event loop itself, a turn at a time, and after each turn arms a timer
that sleeps when it fires -- at the next moment events are served,
whoever serves them.

What is asserted, from the two drains' own closing lines (their number of
slices, the time they say they spent and their longest slice):
  - the frame was dear in the dear load: its cost was paid about once a
    slice;
  - each drain has slices to lose in the plain load;
  - each drain of the dear load ran in under half the slices of the plain
    load's. The work is the same; a slice that ends early, for whatever
    reason, only adds to the count;
  - no slice of the dear load ran away: the longest is under five budgets
    and what one object or one frame let in by the progress bar may add
    (the ceiling itself is the rule's, and tested with it:
    tests/src/Gui/TurnBudget.cpp);
  - the plain load's slices are still slices of the budget: the time a
    drain spent, over its slices, is under three budgets;
  - every visual is built at the end of each load.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout). Scored against the tree before the change: see the commit
message.
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
PATH = os.path.join(OUT, "drain-turn.FCStd")
COUNT = int(os.environ.get("GT_COUNT", "2400"))
BUDGET_MS = int(os.environ.get("GT_BUDGET", "10"))
BUDGET = BUDGET_MS / 1000.0
# What a frame costs in the dear load: over ten budgets, so that half of
# it is past the ceiling of five
DEAR = float(os.environ.get("GT_DEAR", "0.12"))
LOADS = ["warm", "plain", "dear"]

RESTORE = re.compile(r"progressive restore (\S+): (\d+) view providers in (\d+) slices, "
                     r"([-+.\de]+)s(?:, longest ([-+.\de]+)s)?")
VISUAL = re.compile(r"progressive load (\S+): (\d+) of (\d+) visuals in (\d+) slices, "
                    r"([-+.\de]+)s(?:, longest ([-+.\de]+)s)?")

state = {"done": False, "load": -1, "runs": {}, "cur": None, "frame": None}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def frame():
    """What a frame costs in the dear load, paid where events are served."""
    cur = state["cur"]
    if cur is None or state["done"] or "visual" in cur:
        return
    time.sleep(DEAR)
    cur["paid"] += 1
    if "restore" not in cur:
        cur["paid_restore"] += 1


def drive():
    """The event loop, a turn at a time, and a frame owed after each turn.

    One call of processEvents() is one turn: what is posted while it runs
    -- the next slice, by the slice that ran -- waits for the next call.
    The frame is a timer armed between two turns, so it is due at the next
    moment events are served: by the progress bar of the next slice, from
    inside it, or else behind that slice.
    """
    cur = state["cur"]
    if cur is None or state["done"] or cur.get("driving"):
        return
    cur["driving"] = True
    app = QtCore.QCoreApplication.instance()
    timer = state["frame"]
    while state["cur"] is cur and not state["done"]:
        t0 = time.monotonic()
        app.processEvents()
        cur["turns"].append(time.monotonic() - t0)
        if cur["kind"] == "dear" and "visual" not in cur and not timer.isActive():
            timer.start(0)
    timer.stop()


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
        cur = {"kind": kind, "turns": [], "paid": 0, "paid_restore": 0}
        cur["t0"] = time.monotonic()
        doc = FreeCAD.openDocument(PATH)
        cur["name"] = doc.Name
        cur["t_open"] = time.monotonic() - cur["t0"]
        # From here on: the open itself is one blocking call
        state["cur"] = cur
        load = state["load"]
        QtCore.QTimer.singleShot(150000, lambda: unfinished(load))
        QtCore.QTimer.singleShot(0, drive)
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
        # The turns in which something ran: the loop is driven, not waited on
        gaps = sorted(t for t in cur["turns"] if t >= 0.001) or [0.0]
        note("INFO %s: open %.2f s, restore line at %.2f s, visual line at %.2f s | "
             "restore %d slices %.3f s longest %s s | visual %d slices %.3f s longest %s s, "
             "%s built of %s popped | cost paid %d times, %d of them in the restore | "
             "turns of the event loop over 1 ms %d, median %.4f s, longest %.3f s"
             % (cur["kind"], cur["t_open"], cur.get("t_restore", -1), cur["t_visual"],
                cur["restore"][0], cur["restore"][1], cur.get("restore_longest"),
                cur["visual"][0], cur["visual"][1], cur.get("visual_longest"),
                cur["visual"][2], cur["visual"][3], cur["paid"], cur["paid_restore"],
                len(gaps), gaps[len(gaps) // 2], gaps[-1]))
    except Exception:
        note("ABORT after load %d:\n%s" % (state["load"], traceback.format_exc()))
        finish()
        return
    QtCore.QTimer.singleShot(700, next_load)


def verdict():
    try:
        runs = state["runs"]
        plain, dear = runs["plain"], runs["dear"]
        slices = dear["restore"][0] + dear["visual"][0]
        check("the frame was dear in the dear load",
              dear["paid"] >= 0.5 * slices and plain["paid"] == 0,
              "the cost of %.2f s paid %d times for %d slices" % (DEAR, dear["paid"], slices))
        for key, what in (("restore", "the view provider drain"), ("visual", "the visual drain")):
            ps, pt = plain[key][0], plain[key][1]
            ds, dt = dear[key][0], dear[key][1]
            check("%s has slices to lose" % what, ps >= 10, "%d slices in the plain load" % ps)
            check("%s takes longer turns where the frame is dear" % what,
                  ds < 0.5 * ps,
                  "%d slices and %.3f s against %d slices and %.3f s" % (ds, dt, ps, pt))
            longest = dear[key + "_longest"]
            ceiling = 5 * BUDGET + 0.3
            check("%s has no slice that ran away" % what,
                  longest is not None and longest < ceiling,
                  "longest slice %s s, five budgets are %.3f s" % (longest, 5 * BUDGET))
            check("%s keeps to the budget where the frame is quick" % what,
                  pt / max(1, ps) < 3 * BUDGET,
                  "%.4f s a slice under a budget of %.3f s" % (pt / max(1, ps), BUDGET))
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
        render.SetBool("ProgressiveLoad", True)
        render.SetInt("ProgressiveLoadBudgetMS", BUDGET_MS)
        # The lines read are log lines of the two modules
        FreeCAD.setLogLevel("Part", "Log")
        FreeCAD.setLogLevel("Gui", "Log")
        FreeCAD.Console.AttachObserver(observe)
        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(frame)
        state["frame"] = timer
        doc = FreeCAD.newDocument("DrainTurn")
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
        note("INFO %d objects saved, budget %d ms, a frame costs %.2f s in the dear load"
             % (COUNT, BUDGET_MS, DEAR))
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1000, next_load)


QtCore.QTimer.singleShot(1500, build)
