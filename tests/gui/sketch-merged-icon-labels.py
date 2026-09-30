"""A merged constraint icon wraps its labels, and a "+N" stands for the rest.

Constraint icons on one spot of the screen are merged into one image: a row
per constraint type, the icon and then the label of every constraint in the
group, in one line. Nothing bounded the line: the grouping is transitive (an
icon joins when it is near ANY member), so on a big sketch seen whole a
whole region is one group -- and an unnamed constraint of a single-icon type
still reserved a ", " of width for its empty label. Sketch028's merged icon
was 44879 pixels wide, nearly all of it blank.

Now empty labels take no room, and a row wraps its labels onto lines of
View/ConstraintIconLabelsPerLine (10) and shows at most
View/ConstraintIconLabelLines (3) of them; past that the last slot is "+N",
whose box picks the constraints it stands for. The icon still picks every
constraint of its type in the group.

Measured here with 50 named Horizontal constraints on one spot, through the
hover pick (SketcherGui.getActiveSketchPreselection) swept over the icon:
- with the defaults: the icon picks all 50, 29 labels pick one each, and
  one box picks the other 21 -- the "+21";
- set to 5 per line and 2 lines during the edit: 9 labels, and a "+41".

Along the way, two picking defects wrapping made common: a click inside one
label also took the labels within the pick radius of it -- the next line up
and down -- and a blank spot of the icon (a short line's ragged end) picked
the constraint whose node the merge happened to be drawn on.

Scored against the tree before (one line, ten labels): 10 labels picked one
each and the settings changed nothing -- three checks fail. Against the tree
before the cap: every label picked one, and no box picked the rest.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "MergedIconLabels"
N = 50
VIEW = "User parameter:BaseApp/Preferences/View"
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


def picks():
    """Every distinct set of constraints one spot of the icon picks."""
    import SketcherGui

    view = state["view"]
    cx, cy = (int(v) for v in view.getPointOnViewport(FreeCAD.Vector(0.5, 0.15, 0)))
    found = set()
    for dy in range(-60, 61, 2):
        for dx in range(-700, 701, 2):
            info = SketcherGui.getActiveSketchPreselection((cx + dx, cy + dy))
            if not info or not info.get("ObjectName"):
                continue
            names = tuple(sorted(n for n in (info.get("SubElementNames") or [])
                                 if n.startswith("Constraint")))
            if names:
                found.add(names)
    state["singles"] = sorted(int(p[0][len("Constraint"):]) for p in found if len(p) == 1)
    return sorted(len(p) for p in found)


def judge(tag, shown):
    sizes = picks()
    note("%s: pick sets by size: %s" % (tag, sizes))
    check("%s: the icon picks all %d" % (tag, N), N in sizes, sizes)
    singles = sizes.count(1)
    note("%s: single picks name %s" % (tag, state["singles"]))
    check("%s: %d labels pick one constraint each, and no more do" % (tag, shown),
          singles == shown, "%d single picks" % singles)
    check("%s: one box picks the other %d" % (tag, N - shown), (N - shown) in sizes, sizes)


def sweep():
    try:
        judge("defaults", 29)
        grp = FreeCAD.ParamGet(VIEW)
        grp.SetInt("ConstraintIconLabelsPerLine", 5)
        grp.SetInt("ConstraintIconLabelLines", 2)
        # a preference change redraws the edit off a 100 ms timer
        QtCore.QTimer.singleShot(1500, resized)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def resized():
    try:
        judge("5 per line, 2 lines", 9)
        grp = FreeCAD.ParamGet(VIEW)
        grp.RemInt("ConstraintIconLabelsPerLine")
        grp.RemInt("ConstraintIconLabelLines")
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
