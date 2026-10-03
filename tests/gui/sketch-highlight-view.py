"""In render cache mode 3 a sketch's preselection is the view's own highlight.

The preselection of the sketch geometry used to be drawn by the four
highlight sets in the edit graph: copies of the hovered curves and points.
Every hover rewrote nodes of the shared edit graph, which every view of the
session draws -- so a hover in one view showed in all of them -- and which
mode 3 then captured again.

Now, in mode 3, the view the preselection came from draws it through its
editing capture's own highlight overlay (ViewerContext::setEditingHighlight),
ordered after the edit graph's overlay, so it is drawn over all of it. The
highlight sets keep the selection only.

Measured here, all in mode 3:
- hovering between edges leaves every node of the edit graph with the node
  id it had, the highlight sets included;
- a hovered edge is not in the PreSelectedCurveSet, and a hovered selected
  edge stays in the SelectedCurveSet in the selection colour;
- the backend draws the hovered edge in the preselection colour where it
  crosses a datum label as much as anywhere else along it, and a hovered
  vertex and axis in it too;
- with a second view of the edit open, a preselection from outside any view
  shows in both, and one made by the pointer in one view shows in that view
  alone.

Scored against the tree before the change: a hover changed 11 of the edit
graph's nodes, the PreSelectedCurveSet took the hovered edge, and the
pointer's preselection showed in both views. The crossing check passed
there too -- in mode 3 the sets' copies were already drawn over the
constraint icon and the label text, whatever their layers say -- so it
guards that the overlay keeps that, and proves nothing more. The vertex
and axis checks came after, and guard that the overlay finds those nodes.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "HighlightView"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def find_all(root, nodetype):
    """Every match. The paths belong to the action and die with it, so the
    nodes are taken out while it lives."""
    sa = coin.SoSearchAction()
    sa.setType(nodetype)
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(root)
    paths = sa.getPaths()
    return [paths[i].getTail() for i in range(paths.getLength())]


def edit_nodes(root):
    """Every node of the edit graph (a node under several parents more
    than once, which is harmless here)."""
    return find_all(root, coin.SoNode.getClassTypeId())


def node_ids(nodes):
    return [n.getNodeId() for n in nodes]


def by_name(root):
    found = {}
    for t in (coin.SoMaterial, coin.SoCoordinate3, coin.SoIndexedLineSet):
        for node in find_all(root, t.getClassTypeId()):
            found.setdefault(node.getName().getString(), node)
    return found


def run():
    try:
        import Part
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        # Edge1 carries a Distance whose label is centred over x = 20, and a
        # Horizontal whose icon sits just above its midpoint; Edge2 runs up
        # through both.
        sketch.addGeometry(Part.LineSegment(V(0, 0, 0), V(40, 0, 0)), False)
        sketch.addGeometry(Part.LineSegment(V(20, -15, 0), V(20, 30, 0)), False)
        sketch.addGeometry(Part.LineSegment(V(-20, 10, 0), V(-5, 25, 0)), False)
        sketch.addConstraint(Sketcher.Constraint("Distance", 0, 40.0))
        sketch.setLabelDistance(0, 12.0)
        sketch.addConstraint(Sketcher.Constraint("Horizontal", 0))
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sketch)
        state["doc"] = doc
        state["sketch"] = sketch
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].fitAll()
        QtCore.QTimer.singleShot(2500, hover_edges)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def hover_edges():
    try:
        view = state["view"]
        check("render cache mode 3",
              FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
              .GetInt("RenderCache", 3) == 3)
        root = view.getAuxSceneGraph()
        sel = FreeCADGui.Selection
        sk = state["sketch"]
        named = by_name(root)
        pre = named.get("PreSelectedCurveSet")
        selset = named.get("SelectedCurveSet")
        if not check("the highlight sets are found", pre is not None and selset is not None):
            finish()
            return
        sel.clearSelection()
        sel.setPreselection(sk, "Edge3")
        nodes = edit_nodes(root)
        before = node_ids(nodes)
        for i in range(4):
            sel.setPreselection(sk, "Edge1" if i % 2 == 0 else "Edge3")
        after = node_ids(nodes)
        changed = sorted(set(n.getTypeId().getName().getString() + ":"
                             + n.getName().getString()
                             for n, b, a in zip(nodes, before, after) if a != b))
        check("hovering between edges leaves every node of the edit graph alone",
              not changed, "%d of %d changed: %s" % (len(changed), len(before), changed[:8]))

        sel.setPreselection(sk, "Edge1")
        check("the hovered edge is not in the PreSelectedCurveSet",
              pre.coordIndex.getNum() == 0, "%d indices" % pre.coordIndex.getNum())
        sel.addSelection(state["doc"].Name, "Sketch", "Edge1")
        sel.setPreselection(sk, "Edge1")
        mats = [selset.materialIndex[i] for i in range(selset.materialIndex.getNum())]
        check("a hovered selected edge stays in the SelectedCurveSet, selection colour",
              pre.coordIndex.getNum() == 0 and mats == [0],
              "pre %d indices, selected materials %s" % (pre.coordIndex.getNum(), mats))
        sel.clearSelection()
        sel.clearPreselection()
        state["want"] = named["SelectedCurvesMaterials"].diffuseColor[1]
        view.redraw()
        QtCore.QTimer.singleShot(800, pixels_plain)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def dump(name, view=None):
    path = os.path.join(OUT, name)
    try:
        (view or state["view"]).saveRenderDump(path, "renderer")
    except Exception as e:
        return None, str(e)
    return QtGui.QImage(path), ""


def edge2_pixels(img):
    """The pixels down Edge2's centre, clear of its ends and of Edge1."""
    view = state["view"]
    x0, y0 = view.getPointOnViewport(FreeCAD.Vector(20, -12, 0))
    x1, y1 = view.getPointOnViewport(FreeCAD.Vector(20, 27, 0))
    res = []
    for y in range(min(y0, y1), max(y0, y1) + 1):
        res.append(QtGui.QColor(img.pixel(int(x0), int(img.height() - 1 - y))))
    return res


def dist(a, b):
    return abs(a.red() - b.red()) + abs(a.green() - b.green()) + abs(a.blue() - b.blue())


def pixels_plain():
    try:
        img, err = dump("plain.png")
        if not check("the bgfx renderer draws the view", img is not None, err):
            finish()
            return
        state["plain"] = edge2_pixels(img)
        FreeCADGui.Selection.setPreselection(state["sketch"], "Edge2")
        state["view"].redraw()
        QtCore.QTimer.singleShot(800, pixels_hovered)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def pixels_hovered():
    try:
        img, err = dump("hovered.png")
        hov = edge2_pixels(img)
        c = state["want"]
        want = QtGui.QColor.fromRgbF(c[0], c[1], c[2])
        plain = state["plain"]
        # The icon and the label are where the plain edge is not drawn on
        # top: its most common colour is the edge's own, and the pixels away
        # from it are theirs.
        counts = {}
        for p in plain:
            counts[p.name()] = counts.get(p.name(), 0) + 1
        edge = max(counts, key=counts.get)
        covered = [i for i, p in enumerate(plain) if dist(p, QtGui.QColor(edge)) > 60]
        check("the icon and the label cover part of the plain edge", len(covered) > 0,
              "%d of %d pixels, edge %s" % (len(covered), len(plain), edge))
        far = [i for i, p in enumerate(hov) if dist(p, want) > 60]
        check("the hovered edge is the preselection colour all along",
              not far, "%d of %d pixels off %s: %s" % (
                  len(far), len(hov), want.name(), [hov[i].name() for i in far[:6]]))
        check("the icon and label crossings included",
              covered and not [i for i in covered if i in far],
              "%d covered pixels, %d still off" % (
                  len(covered), len([i for i in covered if i in far])))
        FreeCADGui.Selection.clearPreselection()
        others(list(OTHERS))
        return
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


# The point set and the cross are nodes of their own: a vertex (Edge2's top
# end) and the horizontal axis clear of Edge1, each hovered by the pointer
# where it is drawn (a preselection message names no axis).
OTHERS = (("Vertex4", (20, 30)), ("H_Axis", (-12, 0)))


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


def preselected():
    pre = FreeCADGui.Selection.getPreselection()
    return [n.split(".")[-1].lower() for n in pre.SubElementNames] if pre.ObjectName else []


def others(todo):
    try:
        if not todo:
            two_views()
            return
        name, xy = todo[0]
        hover_at(state["view"], xy)
        check("the pointer preselects %s" % name, preselected() == [name.lower()],
              preselected())
        state["view"].redraw()
        QtCore.QTimer.singleShot(800, lambda: others_dump(todo))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def others_dump(todo):
    try:
        name, xy = todo[0]
        img, err = dump("hovered-%s.png" % name)
        view = state["view"]
        x, y = view.getPointOnViewport(FreeCAD.Vector(xy[0], xy[1], 0))
        c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y))) if img else None
        check("a hovered %s is drawn in the preselection colour" % name,
              near_want(c), "%s %s" % (c and c.name(), err))
        FreeCADGui.Selection.clearPreselection()
        others(todo[1:])
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


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
        QtCore.QTimer.singleShot(1500, views_control)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def edge2_colour(view, name):
    img, err = dump(name, view)
    if img is None:
        return None, err
    x, y = view.getPointOnViewport(FreeCAD.Vector(20, 22, 0))
    return QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y))), ""


def near_want(c):
    w = state["want"]
    return c is not None and dist(c, QtGui.QColor.fromRgbF(w[0], w[1], w[2])) <= 60


def views_control():
    try:
        FreeCADGui.Selection.setPreselection(state["sketch"], "Edge2")
        for v in state["views"]:
            v.redraw()
        QtCore.QTimer.singleShot(800, views_control_dump)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def views_control_dump():
    try:
        a, ea = edge2_colour(state["views"][0], "control-a.png")
        b, eb = edge2_colour(state["views"][1], "control-b.png")
        check("a preselection from outside any view shows in both views",
              near_want(a) and near_want(b),
              "A %s B %s %s%s" % (a and a.name(), b and b.name(), ea, eb))
        FreeCADGui.Selection.clearPreselection()
        QtCore.QTimer.singleShot(500, views_hover)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def views_hover():
    try:
        viewA = state["views"][0]
        hover_at(viewA, (20, 22))
        pre = FreeCADGui.Selection.getPreselection()
        check("the pointer in view A preselects Edge2",
              pre.ObjectName == "Sketch" and any(
                  s.lower().endswith("edge2") for s in pre.SubElementNames),
              "%s %s" % (pre.ObjectName, pre.SubElementNames))
        for v in state["views"]:
            v.redraw()
        QtCore.QTimer.singleShot(800, views_hover_dump)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def views_hover_dump():
    try:
        a, ea = edge2_colour(state["views"][0], "hover-a.png")
        b, eb = edge2_colour(state["views"][1], "hover-b.png")
        check("the pointer's preselection shows in view A",
              near_want(a), "A %s %s" % (a and a.name(), ea))
        check("and not in view B",
              b is not None and not near_want(b), "B %s %s" % (b and b.name(), eb))
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
