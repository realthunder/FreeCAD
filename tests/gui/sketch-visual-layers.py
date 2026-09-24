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

Read from the edit graph (`CurvesLineSet`, `PointsCoordinate`) and the
selection. Sketch: three lines, the middle one on layer 2.

Checks:

  - two curves are drawn, not three, and five points (root + 2 x 2);
  - Select All from the 3D view skips the hidden line and its vertices;
  - selecting the hidden line's vertex by name crashes nothing and
    colours no drawn point. The vertex-to-point map was filled with 0 --
    the root point's slot -- so a vertex left out of the drawing would
    have coloured the root point; it is -1 now, and its readers check.

Scored against the tree before the change: every check fails -- three
curves and seven points are drawn, Select All takes Edge2, Vertex3 and
Vertex4, and Vertex3, drawn, is coloured when selected.
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

        curves = coin.SoNode.getByName("CurvesLineSet").numVertices.getNum()
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
