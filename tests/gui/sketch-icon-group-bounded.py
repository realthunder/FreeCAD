"""A merged constraint icon gathers the icons near its own, not a chain.

Constraint icons near one another on screen are merged into one image,
drawn where the first of them is. An icon used to join a group when it was
near ANY member, so a chain of neighbours was one group however far it ran:
Sketch028's merged icon swallowed 1321 constraints of a whole region, drawn
at one end of it. Now an icon joins when it is near the icon the group
starts from, and a group spans at most twice the merge distance.

Measured here with 200 short horizontal lines in a row, a fitted view
spreading them a few pixels apart across the screen, read from the
constraint ids each merged icon names in the SoInfo beside it:
- every merged icon's lines lie within a tenth of the view's width;
- the row makes several icons;
- every constraint is still in exactly one icon.

Scored against the tree before: one icon of all 200, spanning the view.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "IconGroupBounded"
N = 200
STEP = 1.0
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def reachable(root, typename):
    sa = coin.SoSearchAction()
    sa.setType(coin.SoType.fromName(coin.SbName(typename)))
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(root)
    paths = sa.getPaths()
    return [paths[i].getTail() for i in range(paths.getLength())]


def run():
    try:
        import Part
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        # A row of short lines, each STEP from the next: fitted, a few
        # pixels apart, so each icon is near its neighbours and the row
        # runs across the screen.
        geos = [Part.LineSegment(V(STEP * i, 0, 0), V(STEP * i + 0.5 * STEP, 0, 0))
                for i in range(N)]
        sketch.addGeometry(geos, False)
        sketch.addConstraint([Sketcher.Constraint("Horizontal", i) for i in range(N)])
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
        state["view"].viewTop()
        state["view"].fitAll()
        # fitAll animates; the icons are drawn again at the zoom it ends on
        QtCore.QTimer.singleShot(3000, inspect)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def px(i):
    x, _ = state["view"].getPointOnViewport(FreeCAD.Vector(STEP * i + 0.25 * STEP, 0, 0))
    return x


def inspect():
    try:
        view = state["view"]
        width = view.getSize()[0]
        note("view width %d px, lines %.1f px apart" % (width, px(1) - px(0)))
        group = coin.SoNode.getByName("ConstraintGroup")
        if not check("the constraint group is found", group is not None):
            finish()
            return
        groups = []
        for info in reachable(group, "SoInfo"):
            text = info.string.getValue().getString()
            if "," in text:
                groups.append([int(t) for t in text.split(",")])
        if not check("the row merges", bool(groups)):
            finish()
            return
        spans = sorted(max(px(i) for i in g) - min(px(i) for i in g) for g in groups)
        note("%d merged icons, sizes %s" % (len(groups), sorted(len(g) for g in groups)))
        note("spans in px %s" % ["%.0f" % s for s in spans])
        check("every merged icon's lines lie within a tenth of the view's width",
              spans[-1] <= width / 10.0, "widest %.0f px of %d" % (spans[-1], width))
        check("the row makes several icons", len(groups) >= 5, "%d" % len(groups))
        members = [i for g in groups for i in g]
        check("no constraint is in two merged icons",
              len(members) == len(set(members)), "%d ids, %d distinct"
              % (len(members), len(set(members))))
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
