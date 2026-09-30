"""A merged constraint icon shows at most ten labels, and a "+N" for the rest.

Constraint icons on one spot of the screen are merged into one image: a row
per constraint type, the icon and then the label of every constraint in the
group, in one line. Nothing bounded the line: the grouping is transitive (an
icon joins when it is near ANY member), so on a big sketch seen whole a
whole region is one group -- and an unnamed constraint of a single-icon type
still reserved a ", " of width for its empty label. Sketch028's merged icon
was 44879 pixels wide, nearly all of it blank.

Now empty labels take no room, and a row shows ten labels and then "+N",
whose box picks the constraints it stands for; the icon still picks every
constraint of its type in the group.

Measured here with 30 named Horizontal constraints on one spot, through the
hover pick (SketcherGui.getActiveSketchPreselection) swept over the icon:
- the icon picks all 30;
- ten labels pick one constraint each, and no more do;
- one box picks the other 20 -- the "+20".

Scored against the tree before the change: 30 labels picked one each, and
nothing picked the 20.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "MergedIconLabels"
N = 30
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def run():
    try:
        import Part
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        # Unit lines 0.01 apart, and a line far away that shrinks them to a
        # few pixels in a fitted view: their icons merge into one.
        geos = [Part.LineSegment(V(0, 0.01 * i, 0), V(1, 0.01 * i, 0)) for i in range(N)]
        geos.append(Part.LineSegment(V(2000, 0, 0), V(2010, 0, 0)))
        sketch.addGeometry(geos, False)
        sketch.addConstraint([Sketcher.Constraint("Horizontal", i) for i in range(N)])
        # a named constraint shows its name: one label per constraint
        for i in range(N):
            sketch.renameConstraint(i, "h%d" % (i + 1))
        doc.recompute()
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].viewTop()
        state["view"].fitAll()
        QtCore.QTimer.singleShot(1500, edit)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def edit():
    try:
        FreeCADGui.activeDocument().setEdit(state["doc"].getObject("Sketch"))
        QtCore.QTimer.singleShot(3000, sweep)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def sweep():
    try:
        import SketcherGui

        view = state["view"]
        cx, cy = (int(v) for v in view.getPointOnViewport(FreeCAD.Vector(0.5, 0.15, 0)))
        # every distinct set of constraints one spot of the icon picks
        picks = set()
        for dy in range(-40, 41, 2):
            for dx in range(-700, 701, 2):
                info = SketcherGui.getActiveSketchPreselection((cx + dx, cy + dy))
                if not info or not info.get("ObjectName"):
                    continue
                names = tuple(sorted(n for n in (info.get("SubElementNames") or [])
                                     if n.startswith("Constraint")))
                if names:
                    picks.add(names)
        sizes = sorted(len(p) for p in picks)
        note("pick sets by size: %s" % sizes)
        check("the icon picks all %d" % N, N in sizes, sizes)
        singles = sizes.count(1)
        check("ten labels pick one constraint each, and no more do",
              singles == 10, "%d single picks" % singles)
        check("one box picks the other %d" % (N - 10), (N - 10) in sizes, sizes)
        FreeCADGui.activeDocument().resetEdit()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
