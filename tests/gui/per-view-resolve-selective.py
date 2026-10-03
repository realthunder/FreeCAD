"""A structure change resolves again only the entries it can have moved.

A view's entries are node keys (docs/CoinRetirement.md 5.23), resolved
again after structure changes. Every entry of every table used to be
resolved again on every change anywhere in the process; now each
resolution remembers the objects it went through (every step of its path
and, for a link, what it links to) and a change re-resolves only the
entries through the object it names. An unresolved entry depends on the
steps it did resolve, where a change can complete it, and is also tried
again on a new object and on a container's children rebuilt (what a
missing top object or view provider waits for). docs/CoinRetirement.md 5.26.

The risk is a missed dependency: a key left as it was after its path
moved. Each case below changes one thing and checks, by the resolution
counters (FreeCADGui.viewVisibilityStats) and where it shows by a pick,
that the entries it concerns were resolved again and the others were not:
  1. an unrelated structure change resolves nothing again;
  2. a box moved out of the Part an entry names it through: that entry;
  3. a box taken out of the Part a Link shows: the entry through the Link
     (a dependency through the link's target, not a step of its path);
  4. the Link relinked to another Part: an unresolved entry through it
     resolves, and hides the box it names there;
  5. a new object where an unresolved entry names it: resolves, hides;
  6. a deleted object: its bare entry stops resolving.
Setting a map resolves only the entries the view did not hold
(docs/CoinRetirement.md 5.29): a held entry keeps its resolution whatever
its value, and one made stale by a structure change in the same turn is
resolved again by the pass the change queued:
  7. one entry's value flipped: nothing resolved, and the pick follows;
  8. one entry added: that one resolved, and it hides its box;
  9. a box moved out of its Part and the map set again in the same turn:
     the pass still resolves the kept entry, which no longer resolves;
 10. twenty entries added one setObjectVisibility at a time: twenty
     resolved in all, not the map's size each time.

  scripts/gui-test.sh tests/gui/per-view-resolve-selective.py /tmp/pvrs
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PerViewResolveSelective"

FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=10):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def stats():
    return FreeCADGui.viewVisibilityStats(True)


def hit(view, p):
    pt = view.getPointOnViewport(FreeCAD.Vector(*p))
    info = view.getObjectInfo((int(pt[0]), int(pt[1])))
    return (info.get("Object"), info.get("SubName", "")) if info else None


def run():
    try:
        doc = FreeCAD.newDocument(DOC)

        def part(name, y, prefix, n, dx):
            p = doc.addObject("App::Part", name)
            p.Placement.Base = FreeCAD.Vector(0, y, 0)
            boxes = []
            for i in range(n):
                b = doc.addObject("Part::Box", "%s%d" % (prefix, i))
                b.Length = b.Width = b.Height = 1
                b.Placement.Base = FreeCAD.Vector(dx + i * 5, 0, 0)
                boxes.append(b)
            p.addObjects(boxes)
            return p
        pa = part("PartA", 0, "A", 5, 0)
        pb = part("PartB", 20, "B", 5, 2.5)
        link = doc.addObject("App::Link", "L")
        link.LinkedObject = pa
        link.Placement.Base = FreeCAD.Vector(0, 40, 0)
        trig = doc.addObject("App::FeaturePython", "Trig")
        trig.addProperty("App::PropertyLink", "Target")
        doc.recompute()
        view = FreeCADGui.getDocument(DOC).activeView()
        view.viewTop()
        view.fitAll()
        settle()

        view.PerViewVisibilities = True
        view.ObjectVisibilities = {
            "PartA.A1.": "0",       # a box in its Part
            "L.A2.": "0",           # a box through the Link
            "L.B4.": "0",           # through the Link, once it links PartB
            "PartB.Missing.": "0",  # an object that does not exist yet
            "B3": "0",              # bare
        }
        settle()
        base = stats()
        check("set: three of five entries resolve", base["setResolved"] == 3, base)

        def op(fn):
            stats()
            fn()
            settle()
            return stats()

        # 1. Unrelated.
        st = op(lambda: setattr(trig, "Target", doc.getObject("A0")))
        check("1: an unrelated link change resolves nothing again",
              st["passes"] == 1 and st["passResolves"] == 0 and st["passResolved"] == 3,
              st)

        # 2. A1 leaves PartA (for PartB): PartA.A1. stops resolving.
        st = op(lambda: pb.addObject(doc.getObject("A1")))
        check("2: moving A1 out of PartA resolves its entry again, and it no longer resolves",
              st["passResolves"] >= 1 and st["passResolved"] == 2, st)
        check("2: A1 now picks in PartB", hit(view, (5.5, 20.5, 0.5)) is not None
              and "A1" in str(hit(view, (5.5, 20.5, 0.5))), hit(view, (5.5, 20.5, 0.5)))

        # 3. A2 leaves the Part the Link shows: L.A2. goes with it.
        st = op(lambda: pa.removeObject(doc.getObject("A2")))
        check("3: taking A2 out of the Link's target resolves L.A2. again, and it "
              "no longer resolves", st["passResolves"] >= 1 and st["passResolved"] == 1, st)

        # 4. The Link relinked to PartB: L.B4. resolves and hides B4 there.
        st = op(lambda: setattr(link, "LinkedObject", pb))
        check("4: relinking L resolves L.B4.", st["passResolved"] == 2, st)
        through = hit(view, (22.5 + 0.5, 40.5, 0.5))
        check("4: B4 through L is hidden", through is None or "B4" not in str(through),
              through)
        check("4: B4 in PartB is not", "B4" in str(hit(view, (23.0, 20.5, 0.5))),
              hit(view, (23.0, 20.5, 0.5)))

        # 5. A new object where an unresolved entry names it.
        def make():
            m = doc.addObject("Part::Box", "Missing")
            m.Placement.Base = FreeCAD.Vector(40, 0, 0)
            doc.recompute()
            pb.addObject(m)
        st = op(make)
        check("5: the new object resolves PartB.Missing.", st["passResolved"] == 3, st)
        check("5: and it is hidden", "Missing" not in str(hit(view, (40.5, 20.5, 0.5))),
              hit(view, (40.5, 20.5, 0.5)))

        # 6. Deleted: the bare entry stops resolving.
        st = op(lambda: doc.removeObject("B3"))
        check("6: deleting B3 resolves its bare entry again, and it no longer resolves",
              st["passResolves"] >= 1 and st["passResolved"] == 2, st)

        def set_map(update):
            m = dict(view.ObjectVisibilities)
            m.update(update)
            view.ObjectVisibilities = m

        # 7. One entry's value flipped: L.B4. shown again, nothing resolved.
        st = op(lambda: set_map({"L.B4.": "1"}))
        check("7: flipping one value resolves nothing", st["sets"] == 1
              and st["setResolves"] == 0 and st["setResolved"] == 2, st)
        check("7: B4 through L picks again", "B4" in str(hit(view, (23.0, 40.5, 0.5))),
              hit(view, (23.0, 40.5, 0.5)))

        # 8. One entry added: A3 in PartA hidden.
        st = op(lambda: set_map({"PartA.A3.": "0"}))
        check("8: adding one entry resolves that one", st["setResolves"] == 1
              and st["setResolved"] == 3, st)
        check("8: A3 in PartA is hidden", "A3" not in str(hit(view, (15.5, 0.5, 0.5))),
              hit(view, (15.5, 0.5, 0.5)))

        # 9. A3 moved to PartB and the map set again before the pass: the
        # kept resolution is stale, and the queued pass must still see it.
        def move_and_set():
            pb.addObject(doc.getObject("A3"))
            set_map({"PartB.Other.": "0"})
        st = op(move_and_set)
        check("9: the set resolved only the new entry", st["setResolves"] == 1, st)
        check("9: the pass resolved the kept PartA.A3. again, and it no longer resolves",
              st["passResolves"] >= 1 and st["passResolved"] == 2, st)
        check("9: A3 picks in PartB", "A3" in str(hit(view, (15.5, 20.5, 0.5))),
              hit(view, (15.5, 20.5, 0.5)))

        # 10. One at a time.
        def one_by_one():
            for i in range(20):
                view.setObjectVisibility(pb, False, "N%d." % i)
        st = op(one_by_one)
        check("10: twenty entries added singly resolve twenty in all",
              st["sets"] == 20 and st["setResolves"] == 20, st)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(500, run)
