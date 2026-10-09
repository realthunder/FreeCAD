"""While one document loads, the active view's document goes on refining; and
of two documents filling in at once, the one in front is filled in first.

A load gives every object its first picture before it refines any
(docs/DocumentLoad.md sec 18.11). The question "is a load still building
visuals" was asked of the application, so ANY document's load held EVERY
document's refinements, and the visual drain split each slice evenly
between the documents filling in. The user's ruling (sec 18.15): priority
goes to the active view's document, and to the later opened document's
first pictures ahead of an earlier load's.

- A refinement waits while ITS document has visuals left to build; one of
  a document that is not the active view's waits for any document's as
  well. So what the user is working in refines while another loads behind
  it.
- The drain serves the documents in order -- the active view's first, then
  the later opened -- and each takes what it can use of the slice. One that
  got nothing for a second builds one visual, and stays visibly alive.

What is done, with Render/LevelTolerance at 0.01 so that every solid asks
for its exact mesh, four times over:

  1. `Old` (40 solids of 992 faces) is opened; as its drain ends `New` (80)
     is opened and Old's view made the active one again. Old's refinements
     are landing while New fills in.
  2. The same, New's view left active: Old's refinements wait, as before.
  3. `New` is opened and `Small` (20) right after it, Small's view active:
     both fill in at once, Small the later opened.
  4. The same, and New's view made the active one: the active document
     goes ahead of the later opened.

What is asserted:
  1. the case arose (Old had refinements still to land when New was
     opened, and Old's view was the active one); 15 or more of Old's 40
     refinements landed while New was loading; none of New's own did;
  2. with New's view active 5 of Old's at most landed in that time (a
     landing or two get in before the load has parked its first visual);
  3. while Small was filling in, New built few visuals (a quarter of
     Small's 20 at most), and Small was through first;
  4. with New's view active, New was through first, and Small was not
     yet built when it was. Small does build while New fills in: what the
     document in front cannot use of a slice -- its visuals all waiting
     for a pre-mesh -- is the next one's, and one visual a second keeps
     it alive besides. It built 4 of its 20 in that time while a slice
     was still charged for the frame drawn inside it, which mostly left
     nothing to hand on, and builds 14 or 15 since (docs/DocumentLoad.md
     sec 18.16): New through after 9.8 to 10.7 s where it took 9.7 to
     10.0, Small after 10.5 to 11.7 s where it took 13.0 to 13.3.

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
FILES = {"Old": ("A", 40), "New": ("B", 80), "Small": ("C", 20)}
QUIET = 4.0

VISUAL = re.compile(r"progressive load (\S+): (\d+) of (\d+) visuals in (\d+) slices")
BUILD = re.compile(r"slow visual build: \S+#([A-Z])\d+\.ViewObject .* drain (\d) pump (\d)")

state = {"done": False, "events": [], "t0": 0.0, "results": {}}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def path(name):
    return os.path.join(OUT, name + ".FCStd")


def observe(notifier, msg, level):
    now = time.monotonic() - state["t0"]
    m = BUILD.search(msg)
    if m:
        kind = "landing" if m.group(3) == "1" else ("build" if m.group(2) == "1" else None)
        if kind:
            state["events"].append((now, kind, m.group(1)))
        return
    m = VISUAL.search(msg)
    if m:
        state["events"].append((now, "drained", m.group(1)))


def count(kind, letter, t_from, t_to):
    return len([1 for t, k, n in state["events"]
                if k == kind and n == letter and t_from <= t < t_to])


def drained(doc):
    for t, k, n in state["events"]:
        if k == "drained" and n == doc:
            return t
    return None


def last_event():
    return state["events"][-1][0] if state["events"] else 0.0


def now():
    return time.monotonic() - state["t0"]


def wait(cond, then, limit=90.0):
    """Call `then` once cond() is true; give the sequence up after `limit` s."""
    deadline = time.monotonic() + limit

    def tick():
        if state["done"]:
            return
        try:
            if cond():
                then()
                return
            if time.monotonic() > deadline:
                note("ABORT waited %.0f s for %s"
                     % (limit, getattr(cond, "__name__", "a condition")))
                finish()
                return
        except Exception:
            note("ABORT:\n" + traceback.format_exc())
            finish()
            return
        QtCore.QTimer.singleShot(20, tick)

    QtCore.QTimer.singleShot(20, tick)


def activate(doc):
    views = FreeCADGui.getDocument(doc).mdiViewsOfType("Gui::View3DInventor")
    FreeCADGui.getMainWindow().setActiveWindow(views[0])
    QtCore.QCoreApplication.processEvents()


def active_doc():
    return FreeCADGui.ActiveDocument.Document.Name if FreeCADGui.ActiveDocument else None


def start_sequence():
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    QtCore.QCoreApplication.processEvents()
    state["events"] = []
    state["t0"] = time.monotonic()


def refine_sequence(key, old_active, then):
    """Old settles its drain, New is opened; who refines while New fills in."""
    start_sequence()
    old = FreeCAD.openDocument(path("Old")).Name
    run = {}

    def old_drained():
        return drained(old) is not None

    def open_new():
        run["landed_before"] = count("landing", "A", 0.0, 1e9)
        run["t_new"] = now()
        run["new"] = FreeCAD.openDocument(path("New")).Name
        if old_active:
            activate(old)
        run["active"] = active_doc()
        wait(new_drained, settle)

    def new_drained():
        return drained(run["new"]) is not None

    def settle():
        run["t_new_drained"] = drained(run["new"])
        wait(lambda: now() - last_event() > QUIET, record)

    def record():
        # While New was loading: from its open to its drain's closing
        # line. A landing or two get in at the head of that even where a
        # load holds them, before the load has parked its first visual.
        t0, t1 = run["t_new"], run["t_new_drained"]
        run["t_fill"] = min([t for t, k, n in state["events"] if k == "build" and n == "B"]
                            or [0.0])
        run["old_during"] = count("landing", "A", t0, t1)
        run["old_after"] = count("landing", "A", t1, 1e9)
        run["new_during"] = count("landing", "B", t0, t1)
        run["new_after"] = count("landing", "B", t1, 1e9)
        run["old_is_active"] = run["active"] == old
        state["results"][key] = run
        note("INFO %s: New opened %.2f s, built visuals from %.2f to %.2f s, active view's "
             "document %s | Old's refinements landed: %d before New was opened, %d while New "
             "loaded, %d after | New's: %d while it loaded, %d after"
             % (key, t0, run["t_fill"], t1, "Old" if run["old_is_active"] else "New",
                run["landed_before"],
                run["old_during"], run["old_after"], run["new_during"], run["new_after"]))
        then()

    wait(old_drained, open_new)


def order_sequence(key, new_active, then):
    """New and Small fill in at once; whose visuals are built first."""
    start_sequence()
    new = FreeCAD.openDocument(path("New")).Name
    small = FreeCAD.openDocument(path("Small")).Name
    if new_active:
        activate(new)
    run = {"active": active_doc(), "t_active": now()}

    def both_drained():
        return drained(new) is not None and drained(small) is not None

    def record():
        t_new, t_small = drained(new), drained(small)
        first_small = min([t for t, k, n in state["events"] if k == "build" and n == "C"] or [0.0])
        run["t_new"], run["t_small"] = t_new, t_small
        # While Small was filling in, from its first visual to its last
        run["new_while_small"] = count("build", "B", first_small, t_small)
        run["small_built"] = count("build", "C", 0.0, 1e9)
        # While New was filling in, from the moment its view was the active one
        run["small_while_new"] = count("build", "C", run["t_active"], t_new)
        run["new_while_new"] = count("build", "B", run["t_active"], t_new)
        run["new_is_active"] = run["active"] == new
        state["results"][key] = run
        note("INFO %s: active view's document %s | Small filled in by %.2f s, New by %.2f s | "
             "while Small filled in New built %d (Small %d) | while New filled in Small built %d "
             "(New %d)"
             % (key, "New" if run["new_is_active"] else "Small", t_small, t_new,
                run["new_while_small"], run["small_built"], run["small_while_new"],
                run["new_while_new"]))
        then()

    wait(both_drained, lambda: wait(lambda: now() - last_event() > QUIET, record))


def verdict():
    try:
        res = state["results"]
        r = res["1"]
        check("1 the case arose: Old's view is the active one and Old had refinements to land",
              r["old_is_active"] and r["old_during"] + r["old_after"] >= 10,
              "%d landed before New was opened, %d after" % (r["landed_before"],
                                                             r["old_during"] + r["old_after"]))
        check("1 the active view's document refines while another document loads",
              r["old_during"] >= 15, "%d of Old's refinements landed while New loaded"
              % r["old_during"])
        check("1 the loading document's own refinements wait for its visuals",
              r["new_during"] == 0,
              "%d while it filled in, %d after" % (r["new_during"], r["new_after"]))
        r = res["2"]
        check("2 the case arose: New's view is the active one and Old had refinements to land",
              not r["old_is_active"] and r["old_during"] + r["old_after"] >= 10,
              "%d after New was opened" % (r["old_during"] + r["old_after"]))
        check("2 a document in the background waits for the load in front of it",
              r["old_during"] <= 5, "%d of Old's refinements landed while New loaded"
              % r["old_during"])
        r = res["3"]
        check("3 the case arose: Small's view is the active one, and all of Small was built",
              not r["new_is_active"] and r["small_built"] == FILES["Small"][1], r["small_built"])
        check("3 the later opened document is filled in first",
              r["t_small"] < r["t_new"] and r["new_while_small"] <= FILES["Small"][1] // 4,
              "New built %d visuals while Small built its %d"
              % (r["new_while_small"], r["small_built"]))
        r = res["4"]
        check("4 the case arose: New's view is the active one, and it built visuals as such",
              r["new_is_active"] and r["new_while_new"] >= 30, r["new_while_new"])
        check("4 the active view's document goes ahead of the later opened one",
              r["t_new"] < r["t_small"] and r["small_while_new"] < FILES["Small"][1],
              "Small built %d of its %d visuals while New built its %d; New filled in by %.2f s, "
              "Small by %.2f s" % (r["small_while_new"], r["small_built"], r["new_while_new"],
                                   r["t_new"], r["t_small"]))
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
        for name, (letter, n) in FILES.items():
            doc = FreeCAD.newDocument(name)
            for i in range(n):
                obj = doc.addObject("Part::Feature", "%s%d" % (letter, i))
                obj.Shape = prism(990, 30.0 + 0.01 * i, 10)
                obj.Placement.Base = FreeCAD.Vector(90 * (i % 10), 90 * (i // 10), 0)
            doc.recompute()
            FreeCADGui.SendMsgToActiveView("ViewFit")
            QtCore.QCoreApplication.processEvents()
            doc.saveAs(path(name))
            FreeCAD.closeDocument(doc.Name)
        FreeCAD.Console.AttachObserver(observe)
        note("INFO saved: " + ", ".join("%s %d solids" % (k, v[1]) for k, v in FILES.items()))
        # One sequence thrown away: nothing is read off a first open
        refine_sequence("warm", True, lambda: refine_sequence(
            "1", True, lambda: refine_sequence(
                "2", False, lambda: order_sequence(
                    "3", False, lambda: order_sequence("4", True, verdict)))))
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()


QtCore.QTimer.singleShot(1500, build)
