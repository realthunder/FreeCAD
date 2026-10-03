"""Per-view visibility and the caches every view shares (docs/CoinRetirement.md 5.25).

The document's nodes are shared by every view, and so are the bounding
box caches of its separators. A view's visibility table is read at the
switches of the objects some view has an entry for, and every cache open
above records the read. Until 5.25 the record was the table's ADDRESS:
each view has its own table, so after any change each view rebuilt the
caches the previous one had built, walking every object an entry names --
on every render, since every view's auto clipping takes a bounding box
pass (62 ms a round for 1000 entries in four views, against 0.13 ms).

Now the auto clipping answers for no view (Superset: nothing hidden,
shown what some view shows), and every other pass matches a table by its
CONTENT. What this checks:
  A. the clip pass is a superset in every view: it holds an object only
     one view shows, and one only one view hides;
  B. the exact passes are still per view -- the viewer's own scene box
     (the one fit-all takes with a map) -- even right after a clip pass
     built the shared caches, the order a cache would leak in;
  C. picks still answer per view, and still cull (a miss through a gap
     costs about what it does with no entries at all);
  D. a round of clip passes over views with DIFFERENT maps after a change
     in one Part costs about what it does with no entries.

  scripts/gui-test.sh tests/gui/per-view-clip-cache.py /tmp/pvcc
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PerViewClipCache"
NPART, NBOX, PITCH = 10, 20, 5.0
FAR = 1000.0

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


def median_us(fn, n=21):
    fn()
    ts = []
    for _ in range(n):
        t = time.perf_counter()
        fn()
        ts.append((time.perf_counter() - t) * 1e6)
    ts.sort()
    return ts[n // 2]


class Views:
    def __init__(self, views):
        from pivy import coin
        self.coin = coin
        self.views = views

    def box(self, view, clip):
        """The bounding box of \\a view's clip pass (applied to its render
        manager's scene, as the auto clipping is) or of an exact pass (the
        view's scene graph, as per-view-visibility.py's xmax takes it)."""
        rm = view.getViewer().getSoRenderManager()
        action = self.coin.SoGetBoundingBoxAction(rm.getViewportRegion())
        action.apply(rm.getSceneGraph() if clip else view.getSceneGraph())
        b = action.getBoundingBox()
        return None if b.isEmpty() else (b.getMin()[0], b.getMax()[0])


def hit(view, pt):
    info = view.getObjectInfo((int(pt[0]), int(pt[1])))
    return (info.get("Object"), info.get("SubName", "")) if info else None


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        parts = []
        for p in range(NPART):
            part = doc.addObject("App::Part", "Part%d" % p)
            part.Placement.Base = FreeCAD.Vector(0, p * 10, 0)
            boxes = []
            for i in range(NBOX):
                b = doc.addObject("Part::Box", "B%d_%d" % (p, i))
                b.Length = b.Width = b.Height = 1
                b.Placement.Base = FreeCAD.Vector(i * PITCH, 0, 0)
                boxes.append(b)
            part.addObjects(boxes)
            parts.append(part)
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)
        for _ in range(2):
            FreeCADGui.runCommand("Std_ViewCreate")
            settle()
        views = gdoc.mdiViewsOfType("Gui::View3DInventor")
        if not check("three 3D views", len(views) == 3, len(views)):
            return
        for v in views:
            v.viewTop()
            v.fitAll()
        settle()
        # Shown by one view only (Visibility off, a per-view show), and
        # hidden by one view only; made after the fit, which they would
        # zoom out of pick range.
        far = doc.addObject("Part::Box", "Far")
        far.Placement.Base = FreeCAD.Vector(FAR, 0, 0)
        near = doc.addObject("Part::Box", "Neg")
        near.Placement.Base = FreeCAD.Vector(-FAR, 0, 0)
        doc.recompute()
        far.ViewObject.Visibility = False
        settle()
        vb = Views(views)
        v1, v2, v3 = views

        # Baselines with no entry anywhere.
        gap_pt = v1.getPointOnViewport(FreeCAD.Vector(2.5, 20.5, 0.5))
        box_pt = v1.getPointOnViewport(FreeCAD.Vector(5.5, 20.5, 0.5))
        gap0 = median_us(lambda: v1.getObjectInfo((int(gap_pt[0]), int(gap_pt[1]))), 101)
        root = parts[-1].ViewObject.RootNode

        def clip_round():
            root.touch()
            for v in views:
                vb.box(v, True)
        round0 = median_us(clip_round)

        # Every box hidden in its Part in every view, each view's map
        # missing a different one; Far shown in v1, Neg hidden in v1.
        entries = {"Part%d.B%d_%d." % (p, p, i): "0"
                   for p in range(NPART) for i in range(NBOX)}
        drop = ["Part2.B2_%d." % i for i in (0, 1, 2)]
        for v, key in zip(views, drop):
            m = dict(entries)
            m.pop(key)
            v.ObjectVisibilities = m
        v1.PerViewVisibilities = True
        v1.setObjectVisibility(far, True)
        v1.setObjectVisibility(near, False)
        settle()

        # A: the clip pass is every view's superset.
        for i, v in enumerate(views):
            b = vb.box(v, True)
            check("A: v%d clip pass holds Far (shown in v1 only)" % (i + 1),
                  b is not None and b[1] >= FAR, b)
            check("A: v%d clip pass holds Neg (hidden in v1 only)" % (i + 1),
                  b is not None and b[0] <= -FAR + 1, b)

        # B: exact passes per view, each right after the clip passes of
        # every view rebuilt the shared caches.
        for rnd in range(2):
            clip_round()
            b1 = vb.box(v1, False)
            clip_round()
            b2 = vb.box(v2, False)
            check("B%d: v1 exact box has Far and not Neg" % rnd,
                  b1 is not None and b1[1] >= FAR and b1[0] > -FAR + 1, b1)
            check("B%d: v2 exact box has Neg and not Far" % rnd,
                  b2 is not None and b2[0] <= -FAR + 1 and b2[1] < FAR, b2)

        # C: picks per view, and culled.
        clip_round()
        settle()
        in_v1 = hit(v1, box_pt)
        check("C: B2_1 hidden in v1's Part2 is not picked there",
              not in_v1 or "B2_1" not in str(in_v1), in_v1)
        pt2 = v2.getPointOnViewport(FreeCAD.Vector(5.5, 20.5, 0.5))
        in_v2 = hit(v2, pt2)
        check("C: v2 has no entry for B2_1 and picks it", "B2_1" in str(in_v2), in_v2)
        gap1 = median_us(lambda: v1.getObjectInfo((int(gap_pt[0]), int(gap_pt[1]))), 101)
        check("C: a miss through a gap still culls (%.0f us, %.0f without entries)"
              % (gap1, gap0), gap1 <= 3 * gap0 + 50, (gap1, gap0))

        # D: views with different maps share the clip pass's caches.
        round1 = median_us(clip_round)
        check("D: a clip round after a one-Part change, 3 views with different maps: "
              "%.0f us (%.0f without entries)" % (round1, round0),
              round1 <= 3 * round0 + 100, (round1, round0))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(500, run)
