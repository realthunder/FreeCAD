"""A merged constraint icon wider than 32767 pixels no longer aborts the edit.

Constraint icons that fall on the same spot of the screen are merged into one
image: the icon, then the number of each constraint beside it. On a big
sketch seen whole, a thousand of them can merge -- the corpus's Sketch028
merged 1321 into an image 44879 pixels wide. Coin keeps an image size in a
pair of shorts, so the width wrapped negative, the copy asked for 2^64 bytes,
and the exception left draw() before it reached updateColor(). The
constraint group's switchboard was then never enabled: every constraint --
datum labels, icons -- stayed out of the scene, in every render mode, until
the first hover ran the highlight pass.

Measured here, with the icons of 1400 Horizontal constraints on one spot:
- right after entering edit, with nothing hovered, the Distance label is in
  the scene the view draws;
- so is the merged icon, at most 32767 pixels wide.

Scored against the tree before the change: no label, no icon reachable.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "MergedIconOverflow"
N = 5000
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
    """The nodes of a type a traversal of root reaches: a switchboard only
    traverses its enabled children, whatever the search asks for."""
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
        # A stack of unit lines 0.01 apart, and one line 20 m away that makes
        # a fitted view shrink the stack to a few pixels: every Horizontal
        # icon lands on the same spot and they merge into one.
        geos = [Part.LineSegment(V(0, 0.01 * i, 0), V(1, 0.01 * i, 0)) for i in range(N)]
        geos.append(Part.LineSegment(V(20000, 0, 0), V(20100, 0, 0)))
        sketch.addGeometry(geos, False)
        cons = [Sketcher.Constraint("Horizontal", i) for i in range(N)]
        cons.append(Sketcher.Constraint("Distance", N, 100.0))
        sketch.addConstraint(cons)
        doc.recompute()
        # Fitted before the edit: the edit's first draw merges the icons at
        # the zoom it finds.
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
        QtCore.QTimer.singleShot(3000, inspect)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def inspect():
    try:
        group = coin.SoNode.getByName("ConstraintGroup")
        if not check("the constraint group is found", group is not None):
            finish()
            return
        labels = reachable(group, "SoDatumLabel")
        check("right after entering edit, the Distance label is in the scene",
              len(labels) == 1, "%d labels" % len(labels))
        # A merged icon names its constraints in the SoInfo beside it.
        merged = 0
        for info in reachable(group, "SoInfo"):
            text = info.string.getValue().getString()
            if text:
                merged = max(merged, text.count(",") + 1)
        check("and so is the merged icon, of all %d Horizontal constraints" % N,
              merged == N, "largest merged icon: %d" % merged)
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
