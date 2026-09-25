"""A sketch element on the hidden visual layer is not drawn in edit mode.

A sketch has visual layers (`VisualLayerList`: 0 normal, 1 dashed,
2 hidden) and each internal geometry a layer id, set through
`SketcherGui.ViewProviderSketchGeometryExtension.VisualLayerId`. Upstream
draws by layer in `EditModeGeometryCoinManager`, which this fork does not
build; the fork's own `ViewProviderSketch::draw()` read neither, so the
layer was saved with the file and had no effect.

Now an element on a layer that is not visible is neither drawn nor
picked, and nothing that selects by what is on screen takes it: a box,
or Select All from the 3D view.

Read from the edit graph (`CurvesLineSet` and `DashedCurvesLineSet`,
indexed sets over one coordinate list; `PointsCoordinate`) and the
selection. Sketch: three lines, the middle one on layer 2.

Checks:

  - two curves are drawn, not three, and five points (root + 2 x 2);
  - Select All from the 3D view skips the hidden line and its vertices;
  - selecting the hidden line's vertex by name crashes nothing and
    colours no drawn point. The vertex-to-point map was filled with 0 --
    the root point's slot -- so a vertex left out of the drawing would
    have coloured the root point; it is -1 now, and its readers check.

The elements panel shows the layer as a checkbox per row, ticked when
shown; toggling it moves the geometry between layer 0 and the hidden layer
in one transaction:

  - the hidden line's row is unticked, the others ticked;
  - unticking the first line's row hides it (one curve drawn), and it is
    one undo step;
  - ticking the hidden line's row shows it again.

Layer 1 carries a line pattern (0x7E7E); its curves are drawn in their own
indexed set with that pattern, over the same coordinates and materials:

  - a line on layer 1 is drawn in the dashed set, the others solid;
  - with the layer's pattern;
  - hovering it preselects that very curve (picking maps the dashed set's
    polyline back to its curve).

Scored against the tree before the change: every check fails -- three
curves and seven points are drawn, Select All takes Edge2, Vertex3 and
Vertex4, and Vertex3, drawn, is coloured when selected. With the drawing
fixed and the panel not, the four checkbox checks fail: no row has a
checkbox, and ticking one changes nothing. Before the dashed set, the three
layer-1 checks fail: the line is drawn solid with the others (2 solid, 0
dashed) -- upstream draws layer 1 solid too; its layers' patterns are
never read.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}
HIDDEN = 2


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))


def settle(n=10):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def polylines(name):
    """polylines of a line set: an indexed one's coordIndex runs split at -1
    (a plain SoLineSet, as before the dashed set, counts numVertices; a set
    that does not exist counts 0)"""
    node = coin.SoNode.getByName(name)
    if node is None:
        return 0
    if not hasattr(node, "coordIndex"):
        return node.numVertices.getNum()
    idx = list(node.coordIndex.getValues()) if node.coordIndex.getNum() else []
    return 0 if not idx else idx.count(-1) + 1


def curves_drawn():
    return polylines("CurvesLineSet") + polylines("DashedCurvesLineSet")


def selected_names():
    names = []
    for e in FreeCADGui.Selection.getSelectionEx("*"):
        if e.ObjectName == "Sketch":
            names += list(e.SubElementNames)
    return sorted(names)


def selected_points():
    """Points drawn in the selection colour (0.11, 0.68, 0.11)."""
    mat = coin.SoNode.getByName("PointsMaterials")
    return sum(1 for c in mat.diffuseColor.getValues()
               if abs(c[0] - 0.11) < 0.05 and abs(c[1] - 0.68) < 0.05)


def run():
    try:
        import Part
        import SketcherGui
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCADGui.getMainWindow().showMaximized()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("Layers")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        for i in range(3):
            sk.addGeometry(Part.LineSegment(V(10, 10 + i * 10, 0),
                                            V(30, 10 + i * 10, 0)), False)
        geos = sk.Geometry
        ext = SketcherGui.ViewProviderSketchGeometryExtension()
        ext.VisualLayerId = HIDDEN
        geos[1].setExtension(ext)
        sk.Geometry = geos
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        settle(20)

        curves = curves_drawn()
        points = coin.SoNode.getByName("PointsCoordinate").point.getNum()
        note("drawn: %d curves, %d points" % (curves, points))
        check("a line on the hidden layer is not drawn", curves == 2, curves)
        check("nor are its vertices", points == 5, points)

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.runCommand("Std_SelectAll")
        settle()
        names = selected_names()
        note("select all: %s" % (names,))
        check("Select All from the view skips the hidden line",
              "Edge2" not in names and "Vertex3" not in names and "Vertex4" not in names
              and "Edge1" in names and "Edge3" in names, names)

        FreeCADGui.Selection.clearSelection()
        settle()
        FreeCADGui.Selection.addSelection(sk, "Vertex3")
        settle()
        got = selected_points()
        check("selecting a hidden vertex colours no drawn point", got == 0, got)
        FreeCADGui.Selection.clearSelection()
        settle()

        # The elements panel's checkbox.
        from PySide import QtWidgets
        tree = [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTreeWidget)
                if w.objectName() == "elementsWidget"][0]

        def rows():
            return [tree.topLevelItem(i).checkState(0) == QtCore.Qt.Checked
                    for i in range(tree.topLevelItemCount())]

        check("the hidden line's row is unticked, the others ticked",
              rows() == [True, False, True], rows())
        undo = doc.UndoCount
        tree.topLevelItem(0).setCheckState(0, QtCore.Qt.Unchecked)
        settle(15)
        curves = curves_drawn()
        check("unticking a row hides its line", curves == 1 and rows() == [False, False, True],
              "%d curves, rows %s" % (curves, rows()))
        check("as one undo step", doc.UndoCount == undo + 1, doc.UndoCount - undo)
        tree.topLevelItem(1).setCheckState(0, QtCore.Qt.Checked)
        settle(15)
        curves = curves_drawn()
        check("ticking the hidden line's row shows it again",
              curves == 2 and rows() == [False, True, True],
              "%d curves, rows %s" % (curves, rows()))

        # Layer 1 carries a line pattern (0x7E7E): its curves are drawn in
        # a set of their own, with that pattern.
        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle(10)
        geos = sk.Geometry
        ext1 = SketcherGui.ViewProviderSketchGeometryExtension()
        ext1.VisualLayerId = 1
        geos[2].setExtension(ext1)
        sk.Geometry = geos
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        settle(40)
        solid, dashed = polylines("CurvesLineSet"), polylines("DashedCurvesLineSet")
        style = coin.SoNode.getByName("DashedCurvesDrawStyle")
        pattern = style.linePattern.getValue() if style else 0xFFFF
        note("solid %d, dashed %d, pattern 0x%x" % (solid, dashed, pattern))
        check("a line on layer 1 is drawn in the dashed set",
              dashed == 1 and solid == curves_drawn() - 1, "%d solid, %d dashed" % (solid, dashed))
        check("with layer 1's pattern", pattern == 0x7E7E, hex(pattern))

        # picking maps a dashed polyline back to its curve: hover it
        dashedSet = coin.SoNode.getByName("DashedCurvesLineSet")
        dashedCurve = dashedSet.materialIndex.getValues()[0] if dashedSet else -1
        vp = [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget)
              if "View3DInventorViewer" in w.metaObject().className() and w.isVisible()][0]
        p = view.getPointOnViewport(FreeCAD.Vector(20, 30, 0))
        pos = QtCore.QPointF(p[0], vp.height() - 1 - p[1])
        from PySide import QtGui
        QtWidgets.QApplication.sendEvent(vp, QtGui.QMouseEvent(
            QtCore.QEvent.MouseMove, pos, vp.mapToGlobal(pos), QtCore.Qt.NoButton,
            QtCore.Qt.NoButton, QtCore.Qt.NoModifier))
        settle(10)
        colours = coin.SoNode.getByName("CurvesMaterials").diffuseColor.getValues()
        hovered = [i for i, c in enumerate(colours)
                   if abs(c[0] - 0.88) < 0.1 and abs(c[1] - 0.88) < 0.1 and c[2] < 0.3]
        check("hovering the dashed line preselects that curve", hovered == [dashedCurve],
              "%s vs %s" % (hovered, dashedCurve))

        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle()
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
