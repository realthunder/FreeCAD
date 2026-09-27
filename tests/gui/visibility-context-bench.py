"""What a per-occurrence hide costs the traversals that must honour it.

The measurement step 1 of the served edit root plan asks for before the
per-view contexts are built: a view's hide of ONE occurrence of an object
(the sketch as edited through its Body, not the same sketch reached
through a Link) can be carried two ways, and they cost the per-view
traversals differently.

  path   what is built today: an entry in the view's visibility table
         (view.setObjectVisibility(top, False, subname)), read through
         SoFCVisibilityElement by the object's switch, which resolves
         the object chain of the traversal against the table.
  tail   the secondary selection context: SoSelectionElementAction Hide
         on the occurrence's path (ViewProvider.partialRender with the
         hidden marker) stores a hideAll context in the hidden node's
         contextMap2, keyed by the stack of selection roots, and every
         traversal of that node matches its own stack's tails against
         the map (SoFCSelectionRoot::getNodeContext2).

Both give up the caches ABOVE the hidden node in every occurrence of it
(the path entry through SoCacheElement::invalidate in the switch, the tail
hide through the node's SoFCSelectionCounter), so the question is what
each costs on top of that, and what either costs a scene with no hide.

Scene: 20 App::Parts of 50 boxes each and a Link to every Part, i.e. 2000
drawn occurrences of 1000 boxes, in top view. k = 1, 10, 100 boxes are
hidden in their Part occurrence (never the Link one). Timed per cell, as
the median of five batches:
  bbox   SoGetBoundingBoxAction over the view's scene graph
  gap    a ray pick through a gap between boxes inside a Part (culling)
  hit    a ray pick on a visible box
  frame  SoRenderManager.render() in the viewport's own GL context: the
         Coin traversal and GL submission of one frame (mode 0: the GL
         frame, render caches included; mode 3: the bgfx frame plus Coin's
         pass). Rasterisation is deferred and the same in every cell.
         Neither a widget repaint() nor grabFramebuffer() rendered here.

Correctness witness: a hidden box does not pick in its Part occurrence.
Through the Link it still picks under a path entry, and does NOT under a
tail hide -- which is what partialRender asks for, not a leak: it resolves
its path without append (getDetailPath(..., False)), so the key starts at
the Part's CHILDREN root, and a Link replaces the Part's own root and
switch but shares that children root, so [children root, box] is a tail
of the Link's chain too. The per-occurrence key is the appended one
([Part root, children root, box]), which nothing in Python applies today;
its traversal cost is the same, since getNodeContext2 makes the same
lookups whatever the key's length. The tail arm therefore hides 2k
occurrences to the path arm's k.

Run once per render mode:
  BENCH_MODE=0 scripts/gui-test.sh tests/gui/visibility-context-bench.py /tmp/vcb0 --timeout 900
  BENCH_MODE=3 scripts/gui-test.sh tests/gui/visibility-context-bench.py /tmp/vcb3 --timeout 900
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
MODE = int(os.environ.get("BENCH_MODE", "3"))
DOC = "VisCtxBench"
NPART = 20
PCOLS = 5
BX, BY = 10, 5          # boxes per Part, as a grid
PITCH = 5.0             # box pitch inside a Part; boxes are 1 wide, so a gap
                        # is 2 clear of both neighbours, outside the pick radius
PART_DX = BX * PITCH + 6
PART_DY = BY * PITCH + 6
LINK_DY = (NPART // PCOLS) * PART_DY + 10
KS = (1, 10, 100)
ROUNDS = 2
BATCHES = 5

FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", MODE)
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


def settle():
    for _ in range(10):
        QtCore.QCoreApplication.processEvents()


def median_batches(fn, reps):
    fn()
    batches = []
    for _ in range(BATCHES):
        t = time.perf_counter()
        for _ in range(reps):
            fn()
        batches.append((time.perf_counter() - t) / reps * 1e6)
    return sorted(batches)[BATCHES // 2]


def box_centre(part_idx, box_idx, link=False):
    pc, pr = part_idx % PCOLS, part_idx // PCOLS
    a, b = box_idx % BX, box_idx // BX
    x = pc * PART_DX + a * PITCH + 0.5
    y = pr * PART_DY + b * PITCH + 0.5 + (LINK_DY if link else 0)
    return x, y


def hidden_set(k):
    """k (part, box) pairs spread over the Parts and the boxes."""
    return [(i % NPART, (i * 7 + i // NPART) % (BX * BY)) for i in range(k)]


class Bench:
    def __init__(self):
        doc = FreeCAD.newDocument(DOC)
        self.doc = doc
        self.parts = []
        for p in range(NPART):
            part = doc.addObject("App::Part", "Part%d" % p)
            pc, pr = p % PCOLS, p // PCOLS
            part.Placement.Base = FreeCAD.Vector(pc * PART_DX, pr * PART_DY, 0)
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
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)
        self.view = gdoc.activeView()
        self.viewer = self.view.getViewer()
        try:
            self.viewer.setEnabledNaviCube(False)
        except Exception:
            pass
        # One pixel: the gap pick is about culling, and the default radius
        # reaches an edge 2 units away at this zoom.
        self.viewer.setPickRadius(1.0)
        self.view.viewTop()
        self.view.fitAll()
        settle()
        from pivy import coin
        from PySide6.QtOpenGLWidgets import QOpenGLWidget
        self.coin = coin
        # The view's viewport, typed as what it is: viewport() hands back a
        # plain QWidget, which has no makeCurrent().
        gls = [w for w in FreeCADGui.getMainWindow().findChildren(QOpenGLWidget)
               if w.isVisible()]
        if len(gls) != 1:
            raise RuntimeError("expected one 3D viewport, found %d" % len(gls))
        self.glwidget = gls[0]
        self.rmgr = self.viewer.getSoRenderManager()
        self.sg = self.viewer.getSceneGraph()
        self.bbox = coin.SoGetBoundingBoxAction(
            self.viewer.getSoRenderManager().getViewportRegion())
        mid = NPART // 2 + PCOLS // 2
        cx, cy = box_centre(mid, BX // 2 + BX * (BY // 2))
        self.gap = self.at(cx + PITCH / 2, cy + PITCH / 2)
        self.hit = self.at(*box_centre(mid, 0))
        self.tail = []
        self.path = []

    def at(self, x, y):
        p = self.view.getPointOnViewport(FreeCAD.Vector(x, y, 0.5))
        return (int(p[0]), int(p[1]))

    def picked(self, pt):
        info = self.view.getObjectInfo(pt)
        if not info:
            return None
        return (info.get("Object"), info.get("SubName", ""))

    def refresh(self):
        self.view.redraw()
        settle()
        self.paint()
        self.bbox.apply(self.sg)
        self.view.getObjectInfo(self.gap)

    def paint(self):
        self.glwidget.makeCurrent()
        try:
            self.rmgr.render(True, True)
        finally:
            self.glwidget.doneCurrent()

    # -- the two arms -------------------------------------------------------
    def hide_tail(self, pairs):
        for p, b in pairs:
            n = self.parts[p].ViewObject.partialRender(
                ["B%d_%d.%s" % (p, b, "!hide")])
            if n != 1:
                raise RuntimeError("partialRender hid %r of B%d_%d" % (n, p, b))
            self.tail.append((p, b))

    def hide_path(self, pairs):
        for p, b in pairs:
            self.view.setObjectVisibility(self.parts[p], False, "B%d_%d." % (p, b))
            self.path.append((p, b))

    def clear(self):
        for p, b in self.tail:
            self.parts[p].ViewObject.partialRender(["B%d_%d.%s" % (p, b, "!hide")], True)
        for p, b in self.path:
            self.view.setObjectVisibility(self.parts[p], None, "B%d_%d." % (p, b))
        self.tail = []
        self.path = []

    # -- measurement --------------------------------------------------------
    def cell(self, arm, k, rnd):
        self.refresh()
        self.refresh()
        bbox = median_batches(lambda: self.bbox.apply(self.sg), 50)
        gap = median_batches(lambda: self.view.getObjectInfo(self.gap), 200)
        hit = median_batches(lambda: self.view.getObjectInfo(self.hit), 200)
        frame = median_batches(self.paint, 10) / 1000.0
        note("CELL mode=%d arm=%s k=%d round=%d bbox_us=%.1f gap_us=%.1f hit_us=%.1f "
             "frame_ms=%.2f" % (MODE, arm, k, rnd, bbox, gap, hit, frame))

    def witness(self, arm):
        """The first hidden box misses in its Part; through its Link it picks
        under a path entry and not under a tail hide (see the module doc)."""
        p, b = hidden_set(1)[0]
        self.refresh()
        inpart = self.picked(self.at(*box_centre(p, b)))
        inlink = self.picked(self.at(*box_centre(p, b, link=True)))
        name = "B%d_%d" % (p, b)
        check("%s: %s hidden in Part%d" % (arm, name, p),
              not inpart or name not in (inpart[0] or "") + (inpart[1] or ""), inpart)
        shown = bool(inlink) and name in (inlink[1] or "")
        if arm == "path":
            check("path: %s still picks through Link%d" % (name, p), shown, inlink)
        else:
            check("tail: %s hidden through Link%d too (shared children root)" % (name, p),
                  not shown, inlink)


def run():
    try:
        bench = Bench()
        note("scene: %d Parts x %d boxes + %d Links; mode %d; scene graph %s"
             % (NPART, BX * BY, NPART, MODE, bench.sg.getTypeId().getName()))
        # The mode, confirmed, not assumed: a render dump needs the backend.
        try:
            bench.view.saveRenderDump(os.path.join(OUT, "dump.png"))
            backend = True
        except Exception:
            backend = False
        if not check("render mode is %d" % MODE, backend == (MODE == 3),
                     "backend active: %s" % backend):
            return
        p, b = hidden_set(1)[0]
        check("gap picks nothing", bench.picked(bench.gap) is None, bench.picked(bench.gap))
        check("hit picks a box", bench.picked(bench.hit) is not None, bench.picked(bench.hit))
        check("unhidden box picks in its Part",
              "B%d_%d" % (p, b) in str(bench.picked(bench.at(*box_centre(p, b)))),
              bench.picked(bench.at(*box_centre(p, b))))

        bench.hide_tail(hidden_set(1))
        bench.witness("tail")
        bench.clear()
        bench.hide_path(hidden_set(1))
        bench.witness("path")
        bench.clear()

        for rnd in range(ROUNDS):
            bench.cell("none", 0, rnd)
            for k in KS:
                bench.hide_tail(hidden_set(k))
                bench.cell("tail", k, rnd)
                bench.clear()
            for k in KS:
                bench.hide_path(hidden_set(k))
                bench.cell("path", k, rnd)
                bench.clear()
        bench.cell("none", 0, ROUNDS)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
