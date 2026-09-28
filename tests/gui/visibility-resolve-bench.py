"""What keeping per-view visibility entries resolved costs, and what a
node-sensor trigger would cost instead.

A view's entries (ObjectVisibilities) are node keys (docs/CoinRetirement.md
5.23), resolved when set and again after every STRUCTURE change: a new or
deleted object, any link property changed, a restore, a container's 3D
children rebuilt (ViewVisibility::sceneChanged). Each such change schedules
ONE deferred pass for the event-loop turn, which rebuilds every table that
has entries, resolving every entry again (an object lookup, a subname walk,
getDetailPath). The alternative asked about: watch each resolved key's
nodes with SoNodeSensors and resolve only an entry whose path changed.

Scene: 20 App::Parts of 50 boxes and a Link to each Part (2000 drawn
occurrences), mode 3, plus up to four 3D views. Entries hide boxes:
  path  "PartN.Bn_i."  (the occurrence in the Part)
  link  "LinkN.Bn_i."  (through the Link: a deeper resolution)
  bare  "Bn_i"          (PerViewVisibilities on; the object's own root)

Measured with the ViewVisibility counters (FreeCADGui.viewVisibilityStats,
exact pass counts and the passes' own time, so frame cost cannot leak in):
  PASS    one deferred pass per structure change, E entries x T views: a
          PropertyLink of a lone FeaturePython flipped, 20 times.
  SET     one table replaced (view.ObjectVisibilities = map): a full
          rebuild, so building a map one entry at a time is O(E^2).
  STORM   what the passes cost over an operation, path entries:
          geom   every box's Height changed and the document recomputed
          turns  20 event-loop turns, each adding a box to a Part
          load   the document saved with E entries and reopened
          pd_recompute / pd_expr  a spreadsheet-driven PartDesign Body in
                 a SECOND document recomputed / its Pad's expression edited
  SENSOR  the alternative: priority-0 SoNodeSensors (pivy, counting only)
          on every group node of each resolved path but its last (the
          nodes whose child lists make the key), through geom and turns.
          calls = callbacks a C++ sensor would take (priced at ~50 ns
          each by a standalone Coin program, recorded in the doc); hits =
          the ones that are a child-list change on the watched node itself,
          i.e. the entries it would resolve again.

  scripts/gui-test.sh tests/gui/visibility-resolve-bench.py /tmp/vrb --timeout 1800
BENCH_PHASES=pd,load (comma separated) runs only those phases.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "VisResolveBench"
NPART = 20
BX, BY = 10, 5
PITCH = 5.0
PART_DX = BX * PITCH + 6
PART_DY = BY * PITCH + 6
PCOLS = 5
LINK_DY = (NPART // PCOLS) * PART_DY + 10
ES = (1, 10, 100, 1000)
TS = (1, 4)
PASS_REPS = 20
TURNS = 20
PHASES = os.environ.get("BENCH_PHASES", "pass,storm,sensor,pd,load").split(",")

FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(turns=5):
    for _ in range(turns):
        QtCore.QCoreApplication.processEvents()


def stats(reset=False):
    return FreeCADGui.viewVisibilityStats(reset)


def hidden_set(e):
    """e (part, box) pairs spread over the Parts and the boxes."""
    n = BX * BY
    return [(i % NPART, (i // NPART + (i % NPART) * 7) % n) for i in range(e)]


def entries(kind, e):
    m = {}
    for p, b in hidden_set(e):
        if kind == "path":
            m["Part%d.B%d_%d." % (p, p, b)] = "0"
        elif kind == "link":
            m["Link%d.B%d_%d." % (p, p, b)] = "0"
        else:
            m["B%d_%d" % (p, b)] = "0"
    return m


class Bench:
    def __init__(self):
        doc = FreeCAD.newDocument(DOC)
        self.doc = doc
        self.parts = []
        for p in range(NPART):
            part = doc.addObject("App::Part", "Part%d" % p)
            part.Placement.Base = FreeCAD.Vector((p % PCOLS) * PART_DX,
                                                 (p // PCOLS) * PART_DY, 0)
            boxes = []
            for i in range(BX * BY):
                b = doc.addObject("Part::Box", "B%d_%d" % (p, i))
                b.Length = b.Width = b.Height = 1
                b.Placement.Base = FreeCAD.Vector((i % BX) * PITCH, (i // BX) * PITCH, 0)
                boxes.append(b)
            part.addObjects(boxes)
            self.parts.append(part)
        for p, part in enumerate(self.parts):
            link = doc.addObject("App::Link", "Link%d" % p)
            link.LinkedObject = part
            link.Placement.Base = part.Placement.Base + FreeCAD.Vector(0, LINK_DY, 0)
        # The trigger: a structure change that moves no node of the scene.
        trig = doc.addObject("App::FeaturePython", "Trig")
        trig.addProperty("App::PropertyLink", "Target")
        self.trig = trig
        self.targets = (doc.getObject("B0_0"), doc.getObject("B0_1"))
        doc.recompute()
        self.gdoc = FreeCADGui.getDocument(DOC)
        for _ in range(max(TS) - 1):
            FreeCADGui.runCommand("Std_ViewCreate")
            settle()
        self.views = self.gdoc.mdiViewsOfType("Gui::View3DInventor")
        for v in self.views:
            try:
                v.getViewer().setEnabledNaviCube(False)
            except Exception:
                pass
            v.viewTop()
            v.fitAll()
        settle(10)
        self.height = 1.0
        self.extra = []

    def set_tables(self, kind, e, t):
        """Replace the tables of the first t views; the SET stats."""
        m = entries(kind, e) if e else {}
        stats(True)
        for i, v in enumerate(self.views):
            if i < t:
                v.PerViewVisibilities = (kind == "bare")
                v.ObjectVisibilities = m
        settle()
        return stats(True)

    def clear(self):
        for v in self.views:
            v.ObjectVisibilities = {}
            v.PerViewVisibilities = False
        settle()
        stats(True)

    def flip(self):
        # Always to the other one: setting the value it has signals nothing.
        a, b = self.targets
        self.trig.Target = b if self.trig.Target == a else a

    # -- storms -----------------------------------------------------------
    def geom(self):
        self.height = 1.5 if self.height == 1.0 else 1.0
        for part in self.parts:
            for b in part.Group:
                b.Height = self.height
        self.doc.recompute()
        settle()

    def turns(self):
        part = self.parts[-1]
        for i in range(TURNS):
            b = self.doc.addObject("Part::Box", "X%d" % len(self.extra))
            b.Length = b.Width = b.Height = 0.5
            b.Placement.Base = FreeCAD.Vector(-3, i, 0)
            part.addObject(b)
            self.extra.append(b.Name)
            settle(3)

    def drop_extra(self):
        for name in self.extra:
            self.doc.removeObject(name)
        self.extra = []
        self.doc.recompute()
        settle()


def fmt_pass(st):
    passes = st["passes"] or 1
    ent = st["passEntries"] or 1
    return ("passes=%d tables=%d entries=%d resolved=%d pass_us=%.1f entry_us=%.2f"
            % (st["passes"], st["passTables"], st["passEntries"], st["passResolved"],
               st["passNs"] / passes / 1e3, st["passNs"] / ent / 1e3))


def fmt_storm(st, wall):
    return ("wall_ms=%.1f triggers=%d scheduled=%d passes=%d entries=%d resolved=%d "
            "pass_ms=%.2f sets=%d set_ms=%.2f draws=%d draw_keys=%d draw_ms=%.2f"
            % (wall * 1e3, st["triggers"], st["scheduled"], st["passes"],
               st["passEntries"], st["passResolved"], st["passNs"] / 1e6,
               st["sets"], st["setNs"] / 1e6, st["draws"], st["drawKeys"],
               st["drawNs"] / 1e6))


def run_pass(bench):
    for rnd in range(2):
        for kind in ("path", "link", "bare"):
            for t in TS:
                for e in ES:
                    s = bench.set_tables(kind, e, t)
                    sets = s["sets"] or 1
                    note("SET round=%d kind=%s E=%d T=%d sets=%d resolved=%d set_ms=%.2f "
                         "per_entry_us=%.2f"
                         % (rnd, kind, e, t, s["sets"], s["setResolved"],
                            s["setNs"] / sets / 1e6,
                            s["setNs"] / max(1, s["setEntries"]) / 1e3))
                    if rnd == 0:
                        check("%s E=%d T=%d: every entry resolves" % (kind, e, t),
                              s["setResolved"] == s["setEntries"],
                              (s["setResolved"], s["setEntries"]))
                    for i in range(PASS_REPS):
                        bench.flip()
                        settle(3)
                    st = stats(True)
                    if rnd == 0:
                        check("%s E=%d T=%d: one pass per flip" % (kind, e, t),
                              st["passes"] == PASS_REPS, st["passes"])
                    note("PASS round=%d kind=%s E=%d T=%d %s"
                         % (rnd, kind, e, t, fmt_pass(st)))
                    bench.clear()


def run_storms(bench):
    for rnd in range(2):
        for t in TS:
            for e in (0, 100, 1000):
                bench.set_tables("path", e, t)
                stats(True)
                w = time.perf_counter()
                bench.geom()
                w = time.perf_counter() - w
                note("STORM round=%d op=geom E=%d T=%d %s"
                     % (rnd, e, t, fmt_storm(stats(True), w)))
                w = time.perf_counter()
                bench.turns()
                w = time.perf_counter() - w
                note("STORM round=%d op=turns E=%d T=%d %s"
                     % (rnd, e, t, fmt_storm(stats(True), w)))
                bench.drop_extra()
                bench.clear()


def run_sensor(bench):
    from pivy import coin
    groups = (coin.SoNotRec.GROUP_ADDCHILD, coin.SoNotRec.GROUP_INSERTCHILD,
              coin.SoNotRec.GROUP_REPLACECHILD, coin.SoNotRec.GROUP_REMOVECHILD,
              coin.SoNotRec.GROUP_REMOVEALLCHILDREN)
    for e in (100, 1000):
        watched = {}
        for p, b in hidden_set(e):
            vp = bench.parts[p].ViewObject
            path = coin.SoPath()
            path.ref()
            vp.getDetailPath("B%d_%d." % (p, b), path, True)
            n = path.getLength()
            for i in range(n - 1):
                node = path.getNode(i)
                if node.isOfType(coin.SoGroup.getClassTypeId()):
                    watched.setdefault(int(node.this), node)
            path.unref()
        count = {"calls": 0, "hits": 0}

        def cb(key, sensor):
            count["calls"] += 1
            trig = sensor.getTriggerNode()
            if trig is not None and int(trig.this) == key \
                    and sensor.getTriggerOperationType() in groups:
                count["hits"] += 1

        sensors = []
        for key, node in watched.items():
            s = coin.SoNodeSensor(cb, key)
            s.setPriority(0)
            s.attach(node)
            sensors.append(s)
        for op in ("geom", "turns"):
            count["calls"] = count["hits"] = 0
            getattr(bench, op)()
            note("SENSOR op=%s E=%d watched=%d calls=%d hits=%d"
                 % (op, e, len(watched), count["calls"], count["hits"]))
        for s in sensors:
            s.detach()
        bench.drop_extra()


def run_pd(bench):
    """A spreadsheet-driven PartDesign Body in a SECOND document, while the
    bench document holds the entries: the pass is process-wide."""
    import TestSketcherApp
    doc = FreeCAD.newDocument("VisResolvePD")
    sheet = doc.addObject("Spreadsheet::Sheet", "Sheet")
    sheet.set("A1", "10")
    sheet.setAlias("A1", "len")
    body = doc.addObject("PartDesign::Body", "Body")
    sk = body.newObject("Sketcher::SketchObject", "Sketch")
    TestSketcherApp.CreateRectangleSketch(sk, (0, 0), (20, 10))
    doc.recompute()
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sk
    pad.setExpression("Length", "Sheet.len")
    doc.recompute()
    fillet = body.newObject("PartDesign::Fillet", "Fillet")
    fillet.Base = (pad, ["Edge1", "Edge3"])
    fillet.Radius = 1
    doc.recompute()
    settle(10)
    check("pd: the Body is valid", body.Shape.isValid() and len(body.Shape.Faces) > 6,
          len(body.Shape.Faces))
    for rnd in range(2):
        for e in (0, 100):
            bench.set_tables("path", e, 1)
            stats(True)
            w = time.perf_counter()
            for i in range(5):
                sheet.set("A1", str(10 + (i + rnd * 5) % 3))
                doc.recompute()
                settle()
            w = time.perf_counter() - w
            note("STORM round=%d op=pd_recompute E=%d T=1 %s"
                 % (rnd, e, fmt_storm(stats(True), w)))
            w = time.perf_counter()
            for i in range(5):
                pad.setExpression("Length", "Sheet.len + %d" % (i + 1))
                settle()
            w = time.perf_counter() - w
            note("STORM round=%d op=pd_expr E=%d T=1 %s"
                 % (rnd, e, fmt_storm(stats(True), w)))
            bench.clear()
    FreeCAD.closeDocument(doc.Name)
    settle(10)


def run_load(bench):
    paths = {}
    for e in (0, 100, 1000):
        bench.set_tables("path", e, 1)
        paths[e] = os.path.join(OUT, "load%d.FCStd" % e)
        bench.doc.saveAs(paths[e])
    bench.clear()
    FreeCAD.closeDocument(bench.doc.Name)
    settle(10)
    for rnd in range(2):
        for e in (0, 100, 1000):
            stats(True)
            w = time.perf_counter()
            doc = FreeCAD.openDocument(paths[e])
            while FreeCADGui.isBuildingVisuals():
                QtCore.QCoreApplication.processEvents()
            settle(10)
            w = time.perf_counter() - w
            note("STORM round=%d op=load E=%d T=1 %s" % (rnd, e, fmt_storm(stats(True), w)))
            if e:
                # The next pass on the loaded scene: is the load's first
                # pass dear per entry, or work pulled forward?
                trig = doc.getObject("Trig")
                cur = trig.Target.Name if trig.Target else ""
                trig.Target = doc.getObject("B0_1" if cur == "B0_0" else "B0_0")
                settle(3)
                note("PASS round=%d kind=after_load E=%d T=1 %s"
                     % (rnd, e, fmt_pass(stats(True))))
            if rnd == 0 and e:
                v = FreeCADGui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")
                has = [len(x.ObjectVisibilities) for x in v]
                check("load E=%d: the map came back" % e, e in has, has)
            FreeCAD.closeDocument(doc.Name)
            settle(10)


def run():
    try:
        bench = Bench()
        try:
            bench.views[0].saveRenderDump(os.path.join(OUT, "dump.png"))
            backend = True
        except Exception:
            backend = False
        if not check("render mode is 3", backend):
            return
        check("%d 3D views" % max(TS), len(bench.views) == max(TS), len(bench.views))
        for phase, fn in (("pass", run_pass), ("storm", run_storms),
                          ("sensor", run_sensor), ("pd", run_pd), ("load", run_load)):
            if phase in PHASES:
                fn(bench)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    for name in list(FreeCAD.listDocuments()):
        try:
            FreeCAD.closeDocument(name)
        except Exception:
            pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
