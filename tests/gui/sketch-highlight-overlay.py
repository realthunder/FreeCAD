"""A sketch's (pre)selection is drawn by overlays; the geometry is left alone.

updateColor() ran on every (pre)selection change and rewrote the colour and
the layer (z) of every curve and every point of the sketch, and the colour of
every constraint, to change the one or two that the cursor had moved between:
4.6 ms per hover change on the largest corpus sketch, twice per mouse move,
and in render cache mode 3 every rewrite made the whole edit graph be
captured again. The four highlight sets already drew the highlighted curves
and points on top, but in the geometry's own material, which is why that
material had to be rewritten.

Now the sets draw copies of the highlighted vertices, lifted to the highlight
layer, in colours of their own; a selection change runs only that pass, and
touches a constraint only when its highlight changes.

Measured here:
- hovering between two edges leaves the geometry's nodes (materials,
  coordinates, line set) with the node ids they had;
- the PreSelectedCurveSet then holds the hovered edge in the preselection
  colour, and in the preselected-and-selected colour once it is selected;
- leaving a hovered constraint writes that constraint's label and no other,
  and hovering between edges writes none;
- the hovered edge is drawn in the preselection colour by the mode 3
  backend.

Scored against the tree before the change: the geometry's nodes changed on
every hover, and every constraint label was written on each.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "HighlightOverlay"
BASE = ("CurvesMaterials", "CurvesCoordinate", "PointsMaterials", "PointsCoordinate",
        "CurvesLineSet")
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


def nodes_by_name(root):
    found = {}
    for t in (coin.SoMaterial, coin.SoCoordinate3, coin.SoIndexedLineSet):
        for node in find_all(root, t.getClassTypeId()):
            found.setdefault(node.getName().getString(), node)
    return found


def ids(nodes):
    return [nodes[n].getNodeId() for n in BASE if n in nodes]


def labels(root):
    return find_all(root, coin.SoType.fromName(coin.SbName("SoDatumLabel")))


def run():
    try:
        import Part
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        sketch.addGeometry(Part.LineSegment(V(0, 0, 0), V(20, 0, 0)), False)
        sketch.addGeometry(Part.LineSegment(V(20, 0, 0), V(20, 15, 0)), False)
        sketch.addGeometry(Part.LineSegment(V(-20, 10, 0), V(-5, 25, 0)), False)
        sketch.addConstraint(Sketcher.Constraint("Coincident", 0, 2, 1, 1))
        sketch.addConstraint(Sketcher.Constraint("Distance", 2, 21.0))
        sketch.addConstraint(Sketcher.Constraint("DistanceX", 1, 1, 1, 2, 0.0))
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
        root = state["view"].getAuxSceneGraph()
        nodes = nodes_by_name(root)
        state["nodes"] = nodes
        check("the geometry's nodes are found", len(ids(nodes)) == len(BASE),
              sorted(n for n in BASE if n in nodes))
        sel = FreeCADGui.Selection
        sk = state["sketch"]
        sel.clearSelection()
        sel.setPreselection(sk, "Edge2")
        before = ids(nodes)
        for i in range(4):
            sel.setPreselection(sk, "Edge1" if i % 2 == 0 else "Edge2")
        check("hovering between edges leaves the geometry's nodes alone",
              ids(nodes) == before, "%s -> %s" % (before, ids(nodes)))

        ls = labels(root)
        state["labels"] = ls
        if check("the two datum labels are found", len(ls) == 2, len(ls)):
            # the labels are the Distance (Constraint2) and the DistanceX
            # (Constraint3), in that order
            sel.setPreselection(sk, "Constraint2")
            sel.setPreselection(sk, "Constraint3")
            ids0 = [l.getNodeId() for l in ls]
            sel.setPreselection(sk, "Edge3")
            ids1 = [l.getNodeId() for l in ls]
            check("leaving a constraint for an edge writes that label and no other",
                  ids1[0] == ids0[0] and ids1[1] != ids0[1], "%s -> %s" % (ids0, ids1))
            sel.setPreselection(sk, "Edge2")
            ids2 = [l.getNodeId() for l in ls]
            check("hovering between edges writes no constraint label",
                  ids2 == ids1, "%s -> %s" % (ids1, ids2))

        pre = nodes.get("PreSelectedCurveSet")
        mat = nodes.get("SelectedCurvesMaterials")
        if not check("the highlight overlays are found", pre is not None and mat is not None):
            finish()
            return
        sel.setPreselection(sk, "Edge1")
        check("the hovered edge is in the PreSelectedCurveSet, preselection colour",
              pre.coordIndex.getNum() > 0 and pre.materialIndex.getNum() == 1
              and pre.materialIndex[0] == 1,
              "%d indices, materials %s" % (pre.coordIndex.getNum(),
                                           [pre.materialIndex[i] for i in range(pre.materialIndex.getNum())]))
        sel.addSelection(state["doc"].Name, "Sketch", "Edge1")
        sel.setPreselection(sk, "Edge1")
        check("a hovered selected edge takes the preselected-and-selected colour",
              pre.materialIndex.getNum() == 1 and pre.materialIndex[0] == 2,
              [pre.materialIndex[i] for i in range(pre.materialIndex.getNum())])
        sel.clearSelection()
        sel.clearPreselection()
        state["view"].redraw()
        QtCore.QTimer.singleShot(800, pixels_plain)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def dump(name):
    path = os.path.join(OUT, name)
    try:
        state["view"].saveRenderDump(path, "renderer")
    except Exception as e:
        return None, str(e)
    return QtGui.QImage(path), ""


def edge_pixel(img):
    # midpoint of Edge2, a vertical edge clear of the axes and the labels
    vx, vy = state["view"].getPointOnViewport(FreeCAD.Vector(20, 7.5, 0))
    return QtGui.QColor(img.pixel(int(vx), int(img.height() - 1 - vy)))


def pixels_plain():
    try:
        img, err = dump("plain.png")
        if not check("the bgfx renderer draws the view", img is not None, err):
            finish()
            return
        state["plain"] = edge_pixel(img)
        FreeCADGui.Selection.setPreselection(state["sketch"], "Edge2")
        state["view"].redraw()
        QtCore.QTimer.singleShot(800, pixels_hovered)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def pixels_hovered():
    try:
        img, err = dump("hovered.png")
        hov = edge_pixel(img)
        c = state["nodes"]["SelectedCurvesMaterials"].diffuseColor[1]
        want = QtGui.QColor.fromRgbF(c[0], c[1], c[2])
        plain = state["plain"]

        def dist(a, b):
            return (abs(a.red() - b.red()) + abs(a.green() - b.green())
                    + abs(a.blue() - b.blue()))
        check("the hovered edge is drawn nearer the preselection colour than before",
              dist(hov, want) < dist(plain, want) and hov != plain,
              "plain %s hovered %s want %s" % (plain.name(), hov.name(), want.name()))
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
