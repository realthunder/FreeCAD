"""In render cache mode 3 a preselected constraint is the view's own highlight.

A preselected constraint was drawn by writing the preselection colour into
the shared edit graph: its datum label's text colour, its icon (every icon
was rendered again), and for a coincidence the points it holds. Every view
of the session draws that graph, so a hover in one view showed in all of
them, and mode 3 captured the graph again.

Now, where every view the preselection is for can, the views draw it: the
constraint's own node whole, shown on top in the preselection colour the
way a preselected object is, and a coincidence's points as elements, all
in the editing capture's highlight overlay (ViewerContext::
setEditingHighlight). The graph keeps the selection colours alone.

Measured here, all in mode 3:
- preselecting a datum label, an icon and a coincidence in turn leaves
  every node of the edit graph with the node id it had;
- the label and the icon change while preselected, and some of their
  pixels take the preselection colour; the coincident point takes it;
- with a second view of the edit open, the pointer over the icon in one
  view colours it there and not in the other.

Scored against the tree before the change: the preselections changed the
edit graph's nodes, and the pointer's hover showed in both views.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ConstraintHighlightView"
state = {"done": False}

# model points: the Distance label's text, the Horizontal icon's line and
# the coincident point
LABEL = (20, 12)
ICON_EDGE = (-20, -20)
POINT = (40, 0)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def find_all(root, nodetype):
    sa = coin.SoSearchAction()
    sa.setType(nodetype)
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
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        sketch.addGeometry(Part.LineSegment(V(0, 0, 0), V(40, 0, 0)), False)
        sketch.addGeometry(Part.LineSegment(V(-30, -20, 0), V(-10, -20, 0)), False)
        sketch.addGeometry(Part.LineSegment(V(40, 0, 0), V(40, 20, 0)), False)
        sketch.addConstraint(Sketcher.Constraint("Distance", 0, 40.0))
        sketch.setLabelDistance(0, 12.0)
        sketch.addConstraint(Sketcher.Constraint("Horizontal", 1))
        sketch.addConstraint(Sketcher.Constraint("Coincident", 0, 2, 2, 1))
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sketch)
        state["doc"] = doc
        state["sketch"] = sketch
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].fitAll()
        QtCore.QTimer.singleShot(2500, nodes_alone)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def nodes_alone():
    try:
        view = state["view"]
        check("render cache mode 3",
              FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
              .GetInt("RenderCache", 3) == 3)
        root = view.getAuxSceneGraph()
        sel = FreeCADGui.Selection
        sk = state["sketch"]
        sel.clearSelection()
        nodes = find_all(root, coin.SoNode.getClassTypeId())
        before = [n.getNodeId() for n in nodes]
        for name in ("Constraint1", "Constraint2", "Constraint3"):
            sel.setPreselection(sk, name)
            QtWidgets.QApplication.processEvents()
        sel.clearPreselection()
        QtWidgets.QApplication.processEvents()
        after = [n.getNodeId() for n in nodes]
        changed = sorted(set(n.getTypeId().getName().getString()
                             for n, b, a in zip(nodes, before, after) if a != b))
        check("preselecting a label, an icon and a coincidence leaves the edit graph alone",
              not changed, "%d of %d changed: %s" % (
                  sum(1 for b, a in zip(before, after) if a != b), len(before), changed))
        state["want"] = by_name(root)["SelectedCurvesMaterials"].diffuseColor[1]
        view.redraw()
        QtCore.QTimer.singleShot(800, lambda: shots(["plain", "Constraint1", "Constraint2",
                                                     "Constraint3"], {}))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def by_name(root):
    found = {}
    for node in find_all(root, coin.SoMaterial.getClassTypeId()):
        found.setdefault(node.getName().getString(), node)
    return found


def dump(name, view=None):
    path = os.path.join(OUT, name)
    try:
        (view or state["view"]).saveRenderDump(path, "renderer")
    except Exception as e:
        return None, str(e)
    return QtGui.QImage(path), ""


def dist(a, b):
    return abs(a.red() - b.red()) + abs(a.green() - b.green()) + abs(a.blue() - b.blue())


def want():
    w = state["want"]
    return QtGui.QColor.fromRgbF(w[0], w[1], w[2])


def box(img, view, xy, r):
    """The pixels within r of a model point, by viewport position."""
    x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
    y = img.height() - 1 - y
    res = {}
    for j in range(int(y) - r, int(y) + r + 1):
        for i in range(int(x) - r, int(x) + r + 1):
            if 0 <= i < img.width() and 0 <= j < img.height():
                res[(i, j)] = QtGui.QColor(img.pixel(i, j))
    return res


def compare(plain, hov):
    changed = [k for k in plain if dist(plain[k], hov[k]) > 60]
    coloured = [k for k in changed if dist(hov[k], want()) <= 60]
    return changed, coloured


def shots(todo, imgs):
    try:
        if not todo:
            judge(imgs)
            return
        name = todo[0]
        sel = FreeCADGui.Selection
        sel.clearPreselection()
        if name != "plain":
            sel.setPreselection(state["sketch"], name)
        state["view"].redraw()

        def take():
            try:
                imgs[name], err = dump("%s.png" % name)
                if imgs[name] is None:
                    note("ABORT: no dump: " + err)
                    finish()
                    return
                shots(todo[1:], imgs)
            except Exception:
                note("ABORT:\n" + traceback.format_exc())
                finish()
        QtCore.QTimer.singleShot(800, take)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def judge(imgs):
    try:
        view = state["view"]
        FreeCADGui.Selection.clearPreselection()
        # The label's box is tight: the dimension line breaks for the text,
        # so only the text is in it.
        for name, xy, r, what in (("Constraint1", LABEL, 8, "the Distance label's text"),
                                  ("Constraint2", ICON_EDGE, 30, "the Horizontal icon")):
            changed, coloured = compare(box(imgs["plain"], view, xy, r),
                                        box(imgs[name], view, xy, r))
            check("%s changes while preselected" % what, len(changed) > 0,
                  "%d pixels changed" % len(changed))
            check("and some of it takes the preselection colour", len(coloured) > 0,
                  "%d of %d changed pixels near %s" % (len(coloured), len(changed),
                                                       want().name()))
        p = box(imgs["Constraint3"], view, POINT, 2)
        near = [k for k in p if dist(p[k], want()) <= 60]
        check("a preselected coincidence draws its point in the preselection colour",
              len(near) > 0, "%d of %d pixels" % (len(near), len(p)))
        two_views()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def hover_at(view, xy):
    """A pointer move over the model point xy, in that view."""
    gv = view.graphicsView()
    vp = gv.viewport()
    x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    ev = QtGui.QMouseEvent(QtCore.QEvent.MouseMove, pos, gv.mapToGlobal(pos.toPoint()),
                           QtCore.Qt.NoButton, QtCore.Qt.NoButton, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)


def two_views():
    try:
        viewA = state["view"]
        FreeCADGui.runCommand("Std_ViewCreate", 0)
        viewB = FreeCADGui.activeDocument().activeView()
        check("a second view is open", viewB is not viewA)
        mdi = FreeCADGui.getMainWindow().findChild(QtWidgets.QMdiArea)
        mdi.setViewMode(QtWidgets.QMdiArea.SubWindowView)
        mdi.tileSubWindows()
        state["views"] = (viewA, viewB)
        QtCore.QTimer.singleShot(1500, views_fitted)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def views_fitted():
    try:
        for v in state["views"]:
            v.fitAll()
        # fitAll animates; let it land
        QtCore.QTimer.singleShot(1500, views_plain)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def views_plain():
    try:
        state["vplain"] = [dump("two-plain-%d.png" % k, v)[0]
                           for k, v in enumerate(state["views"])]
        # The pointer over the Horizontal icon in view A: it sits off the
        # line's middle by a screen-constant offset, so look for it.
        viewA = state["views"][0]
        found = None
        for dy in range(0, 9):
            for dx in range(-4, 5):
                xy = (ICON_EDGE[0] + dx * 0.5, ICON_EDGE[1] + dy * 0.5)
                hover_at(viewA, xy)
                pre = FreeCADGui.Selection.getPreselection()
                if pre.ObjectName == "Sketch" and "Constraint2" in pre.SubElementNames:
                    found = xy
                    break
            if found:
                break
        check("the pointer in view A preselects the Horizontal icon", found is not None,
              "%s" % (found,))
        for v in state["views"]:
            v.redraw()
        QtCore.QTimer.singleShot(800, views_hovered)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def views_hovered():
    try:
        res = []
        for k, v in enumerate(state["views"]):
            hov, err = dump("two-hover-%d.png" % k, v)
            changed, coloured = compare(box(state["vplain"][k], v, ICON_EDGE, 30),
                                        box(hov, v, ICON_EDGE, 30))
            res.append(len(coloured))
        check("the pointer's preselection colours the icon in view A", res[0] > 0,
              "%d pixels" % res[0])
        check("and not in view B", res[1] == 0, "%d pixels" % res[1])
        FreeCADGui.Selection.clearPreselection()
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
