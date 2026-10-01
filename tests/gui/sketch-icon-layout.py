"""Constraint icons on one spot are laid out side by side, and a "+N" takes the rest.

Icons that fell on one spot of the screen used to be merged into one image:
the icon, then the label of every constraint in the group. One image has one
material, so a hover could not colour one constraint of it, and its colours
had to be baked into its pixels. Now each constraint keeps its own icon, and
the icons of a group are placed side by side from the first one's place:
View/ConstraintIconLabelsPerLine (10) to a line, at most
View/ConstraintIconLabelLines (3) lines. Past that the last slot is a "+N"
that names the rest; their own icons are not drawn.

Measured here with 50 named Horizontal constraints on one spot, from each
drawn icon's place (its translations) and size (its image):
- with the defaults, 29 icons and a "+21" are drawn, no two of them overlap,
  and every constraint is drawn or named by the "+N" exactly once;
- set to 5 per line and 2 lines during the edit: 9 icons and a "+41".

Scored against the tree before (merged images): one image drawn, and no
"+N" of its own.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "IconLayout"
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
        # few pixels in a fitted view: their icons fall on one spot.
        geos = [Part.LineSegment(V(0, 0.01 * i, 0), V(1, 0.01 * i, 0)) for i in range(N)]
        geos.append(Part.LineSegment(V(2000, 0, 0), V(2010, 0, 0)))
        sketch.addGeometry(geos, False)
        sketch.addConstraint([Sketcher.Constraint("Horizontal", i) for i in range(N)])
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
        QtCore.QTimer.singleShot(3000, defaults)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def icons():
    """Each drawn icon: its screen rectangle and the constraints its SoInfo
    names. A constraint separator is [material, translation, image, info,
    (translation, image, info)]; a second translation applies after the
    first. A layout's offset is in pixels (pixelOffset); the icons of one
    group share their unit offset, so the approximate unit cancels."""
    view = state["view"]
    group = coin.SoNode.getByName("ConstraintGroup")
    unit = 0.02 * view.getSize()[1]  # pixels a SoZoomTranslation unit spans
    res = []
    for c in range(group.getNumChildren()):
        sep = group.getChild(c)
        ab = coin.SbVec3f(0, 0, 0)
        tr = coin.SbVec3f(0, 0, 0)
        px = coin.SbVec2f(0, 0)
        for t, i, n in ((1, 2, 3), (4, 5, 6)):
            if sep.getNumChildren() <= n:
                break
            tnode = sep.getChild(t)
            if not tnode.isOfType(coin.SoType.fromName(coin.SbName("SoZoomTranslation"))):
                break
            ab += tnode.abPos.getValue()
            tr += tnode.translation.getValue()
            if hasattr(tnode, "pixelOffset"):  # absent before the layout
                px += tnode.pixelOffset.getValue()
            # the field's text form starts "width height components"; its
            # getValue() decodes the pixels as text and fails on most
            w, h = (int(v) for v in
                    sep.getChild(i).image.get().getString().split(None, 2)[:2])
            if w <= 0 or h <= 0:
                continue
            x, y = view.getPointOnViewport(FreeCAD.Vector(ab[0], ab[1], 0))
            x += tr[0] * unit + px[0]
            y += tr[1] * unit + px[1]
            ids = [int(s) for s in sep.getChild(n).string.getValue().getString().split(",") if s]
            res.append(((x - w / 2.0, y - h / 2.0, x + w / 2.0, y + h / 2.0), ids))
    return res


def overlaps(rects):
    bad = []
    for a in range(len(rects)):
        for b in range(a + 1, len(rects)):
            p, q = rects[a], rects[b]
            if min(p[2], q[2]) - max(p[0], q[0]) > 0.5 and min(p[3], q[3]) - max(p[1], q[1]) > 0.5:
                bad.append((a, b))
    return bad


def judge(tag, shown):
    found = icons()
    singles = [ids for r, ids in found if len(ids) == 1]
    rests = [ids for r, ids in found if len(ids) > 1]
    note("%s: %d icons drawn, %d naming one constraint, rests %s"
         % (tag, len(found), len(singles), [len(r) for r in rests]))
    check("%s: %d icons drawn, each naming its own constraint" % (tag, shown),
          len(singles) == shown, "%d" % len(singles))
    check("%s: and one \"+%d\" naming the rest" % (tag, N - shown),
          [len(r) for r in rests] == [N - shown], [len(r) for r in rests])
    bad = overlaps([r for r, ids in found])
    check("%s: no two drawn icons overlap" % tag, not bad,
          "%d pairs, e.g. %s" % (len(bad), [(found[a][0], found[b][0]) for a, b in bad[:2]]))
    named = sorted(i for r, ids in found for i in ids)
    check("%s: every constraint is drawn or in the \"+N\", once" % tag,
          named == list(range(N)), "%d named, %d distinct" % (len(named), len(set(named))))


def defaults():
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
