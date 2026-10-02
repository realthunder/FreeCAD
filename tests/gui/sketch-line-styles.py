"""A sketch's curves are drawn with a line width and pattern by what they are.

Upstream b140feabaf (and 1155182ac3 for defining external geometry): normal,
construction, internal alignment, external and defining external geometry
each have a width and a pattern in the preferences (Mod/Sketcher/View:
EdgeWidth/EdgePattern, ConstructionWidth/..., InternalWidth/...,
ExternalWidth/..., ExternalDefiningWidth/...). Upstream draws them from one
line set per sub layer in EditModeGeometryCoinManager, which this fork does
not build; here ViewProviderSketch::draw() sorts the curves into one indexed
set per class over the one coordinate and material list, each under its own
draw style.

A visual layer with a pattern of its own (layer 1) still wins over the
class's pattern: such a curve goes to its class's second set ("Dashed..."),
which has the class's width and the layer's pattern.

Sketch: a line, a construction line, an ellipse with its internal geometry
exposed, a circle, one edge of a box as external geometry and one as
defining, and a short line with a dimension.

Checks:

  - each class's set holds its curves;
  - each draw style has its preference's pattern, and the widths are in the
    preferences' ratio;
  - a changed preference reaches the open sketch, and only its class;
  - a construction line moved to layer 1 is drawn with the layer's pattern
    at the construction width;
  - hovering the construction line and the external edge preselects each
    (picking maps a set's polyline back to its curve);
  - the mode 3 backend draws the pattern: sampled along the lines, the
    normal line is unbroken and the construction line has gaps.

Scored against the tree before the change: every curve is in the one solid
set, the construction, internal and external sets do not exist, and the
construction line is drawn unbroken.

With them, upstream's other appearance settings of the same page:

  - a point is coloured as what it belongs to (f5da655429): the ends of a
    normal line in the curve colour, a construction line's and a
    circle's centre in the construction colour, an external edge's in the
    external colour. Before: all of them the vertex colour, red;
  - defining external geometry has a colour preference of its own
    (411cdadf49, View/ExternalDefiningColor), the external colour until
    set. Before: the external colour made lighter, and no preference;
  - a dimension's leaders have a width and a pattern (c2d6248bc7,
    DimensionalConstraintLineWidth/Pattern), and so have the two axes
    (90ca7a30d9, AxisLineWidth/Pattern). Before: neither is read.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = "User parameter:BaseApp/Preferences/Mod/Sketcher/View"
CLASSES = ("", "Construction", "Internal", "External", "ExternalDefining")
PREFS = ("Edge", "Construction", "Internal", "External", "ExternalDefining",
         "DimensionalConstraintLine", "AxisLine")
COLOURS = "User parameter:BaseApp/Preferences/View"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))
    return cond


def settle(n=10):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def clear_prefs():
    grp = FreeCAD.ParamGet(VIEW)
    for name in PREFS:
        grp.RemInt(name + "Width")
        grp.RemInt(name + "Pattern")
    FreeCAD.ParamGet(COLOURS).RemUnsigned("ExternalDefiningColor")


def polylines(name):
    node = coin.SoNode.getByName(name)
    if node is None:
        return None
    idx = list(node.coordIndex.getValues()) if node.coordIndex.getNum() else []
    return 0 if not idx else idx.count(-1) + 1


def counts(prefix=""):
    return [polylines("%sCurves%sLineSet" % (prefix, c)) for c in CLASSES]


def style(cls, prefix=""):
    """(width, pattern, pattern scale) of a class's draw style, or None"""
    node = coin.SoNode.getByName("%sCurves%sDrawStyle" % (prefix, cls))
    if node is None:
        return None
    return (node.lineWidth.getValue(), node.linePattern.getValue(),
            node.linePatternScaleFactor.getValue())


def hover(view, point):
    vp = [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget)
          if "View3DInventorViewer" in w.metaObject().className() and w.isVisible()][0]
    p = view.getPointOnViewport(point)
    pos = QtCore.QPointF(p[0], vp.height() - 1 - p[1])
    for _ in range(2):
        QtWidgets.QApplication.sendEvent(vp, QtGui.QMouseEvent(
            QtCore.QEvent.MouseMove, pos, vp.mapToGlobal(pos), QtCore.Qt.NoButton,
            QtCore.Qt.NoButton, QtCore.Qt.NoModifier))
        settle(6)
    pre = FreeCADGui.Selection.getPreselection()
    return ([n.split(".")[-1].lower() for n in pre.SubElementNames]
            if pre.ObjectName == "Sketch" else [])


def coverage(view, img, y, is_line):
    """The part of the samples along the line y = const, x in -28..28, that
    is the line's colour. By colour, not by "differs from the background":
    the background is a gradient and a grid line runs under each line."""
    lit = total = 0
    x0, py = view.getPointOnViewport(FreeCAD.Vector(-28, y, 0))
    x1, _ = view.getPointOnViewport(FreeCAD.Vector(28, y, 0))
    row = int(img.height() - 1 - py)
    for px in range(int(x0), int(x1) + 1):
        total += 1
        if any(is_line(QtGui.QColor(img.pixel(px, row + dy))) for dy in (-2, -1, 0, 1, 2)):
            lit += 1
    return lit / float(max(total, 1))


def white(c):
    return c.red() > 215 and c.green() > 215 and c.blue() > 215


def blue(c):
    """the construction colour, (0, 0, 220)"""
    return c.blue() > 150 and c.red() < 90 and c.green() < 90


def rgb(c):
    return tuple(int(round(v * 255)) for v in (c[0], c[1], c[2]))


def point_colours(sk):
    """{(GeoId, PosId): colour} of the drawn points, by position."""
    coords = coin.SoNode.getByName("PointsCoordinate").point.getValues()
    colours = coin.SoNode.getByName("PointsMaterials").diffuseColor.getValues()
    found = {}
    for name, (x, y) in {"line start": (-30, 10), "line end": (30, 10),
                         "construction start": (-30, 20), "circle centre": (-20, -15),
                         "external start": (-40, -45), "defining start": (-40, -37)}.items():
        for i, p in enumerate(coords):
            if abs(p[0] - x) < 1e-4 and abs(p[1] - y) < 1e-4:
                found[name] = rgb(colours[i])
    return found


def curve_colour(set_name):
    """the colour of the first curve of a class's set"""
    node = coin.SoNode.getByName(set_name)
    mats = coin.SoNode.getByName("CurvesMaterials").diffuseColor.getValues()
    if node is None or node.materialIndex.getNum() == 0:
        return None
    return rgb(mats[node.materialIndex.getValues()[0]])


def label_style(view):
    sa = coin.SoSearchAction()
    sa.setType(coin.SoType.fromName(coin.SbName("SoDatumLabel")))
    sa.setInterest(coin.SoSearchAction.FIRST)
    sa.setSearchingAll(True)
    sa.apply(view.getAuxSceneGraph())
    path = sa.getPath()
    if path is None:
        return None
    label = path.getTail()
    pattern = label.getField("linePattern")
    return (float(label.getField("lineWidth").get().getString()),
            int(pattern.get().getString(), 0) if pattern is not None else None)


def run():
    try:
        import Part
        import SketcherGui
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        clear_prefs()
        FreeCADGui.getMainWindow().showMaximized()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("LineStyles")
        box = doc.addObject("Part::Box", "Box")
        box.Length, box.Width, box.Height = 20, 8, 5
        box.Placement.Base = V(-40, -45, -10)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(-30, 10, 0), V(30, 10, 0)), False)
        sk.addGeometry(Part.LineSegment(V(-30, 20, 0), V(30, 20, 0)), True)
        ellipse = sk.addGeometry(Part.Ellipse(V(20, -20, 0), V(0, -10, 0), V(0, -20, 0)), False)
        sk.exposeInternalGeometry(ellipse)
        import Sketcher
        # a circle, for a point that is no end: its centre
        sk.addGeometry(Part.Circle(V(-20, -15, 0), V(0, 0, 1), 5), False)
        # a dimension, on a line of its own clear of the others
        dimensioned = sk.addGeometry(Part.LineSegment(V(45, -10, 0), V(45, 5, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Distance", dimensioned, 15.0))
        doc.recompute()
        # the box's bottom edges along X: y = -45 and y = -37
        edges = [i + 1 for i, e in enumerate(box.Shape.Edges)
                 if abs(e.Vertexes[0].Z + 10) < 1e-6 and abs(e.Vertexes[1].Z + 10) < 1e-6
                 and abs(e.Vertexes[0].Y - e.Vertexes[1].Y) < 1e-6]
        low = [n for n in edges if abs(box.Shape.Edges[n - 1].Vertexes[0].Y + 45) < 1e-6][0]
        high = [n for n in edges if n != low][0]
        sk.addExternal("Box", "Edge%d" % low)
        sk.addExternal("Box", "Edge%d" % high, True)
        doc.recompute()
        internal = sum(1 for g in sk.Geometry
                       if g.TypeId == "Part::GeomLineSegment") - 3
        note("internal lines: %d, external: %s" % (internal, sk.ExternalGeometry))

        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        settle(60)

        got = counts()
        note("sets: %s" % (got,))
        check("each class's set holds its curves",
              got == [4, 1, internal, 1, 1] and internal >= 2, got)

        styles = [style(c) for c in CLASSES]
        note("styles: %s" % (styles,))
        if check("each class has a draw style", all(s is not None for s in styles), styles):
            patterns = [s[1] for s in styles]
            check("with its preference's default pattern",
                  patterns == [0xFFFF, 0xFCFC, 0xFCFC, 0xFCFC, 0xFFFF],
                  [hex(p) for p in patterns])
            widths = [s[0] for s in styles]
            check("and width: all the same by default", len(set(widths)) == 1 and widths[0] > 0,
                  widths)
            unit = widths[0] / 2.0

            grp = FreeCAD.ParamGet(VIEW)
            grp.SetInt("ConstructionWidth", 5)
            grp.SetInt("ConstructionPattern", 0xFF00)
            settle(40)
            after = [style(c) for c in CLASSES]
            check("a changed preference reaches the open sketch",
                  abs(after[1][0] - 5 * unit) < 1e-6 and after[1][1] == 0xFF00, after[1])
            check("and only its class",
                  [after[i] for i in (0, 2, 3, 4)] == [styles[i] for i in (0, 2, 3, 4)], after)
            grp.RemInt("ConstructionWidth")
            grp.RemInt("ConstructionPattern")
            settle(40)
            check("and taking it back restores the default", style("Construction") == styles[1],
                  style("Construction"))

        got = hover(view, V(0, 20, 0))
        check("hovering the construction line preselects it", got == ["edge2"], got)
        got = hover(view, V(-30, -45, 0))
        check("hovering the external edge preselects it", got == ["externaledge1"], got)
        FreeCADGui.Selection.clearPreselection()
        hover(view, V(-45, 30, 0))

        # the backend's frame
        view.redraw()
        settle(40)
        path = os.path.join(OUT, "styles.png")
        try:
            view.saveRenderDump(path, "renderer")
            img = QtGui.QImage(path)
        except Exception as e:
            img = None
            note("no backend frame: %s" % e)
        if check("the bgfx renderer draws the view", img is not None and not img.isNull()):
            solid, dashed = coverage(view, img, 10, white), coverage(view, img, 20, blue)
            check("the normal line is drawn unbroken", solid > 0.97, "%.2f" % solid)
            check("the construction line is drawn with gaps", 0.4 < dashed < 0.92,
                  "%.2f" % dashed)

        # -- points, the defining colour, a dimension's leaders, the axes --
        # the construction colour is 0.86 of 255
        WHITE, BLUE, PINK = (255, 255, 255), (0, 0, 219), (204, 51, 153)
        got = point_colours(sk)
        note("points: %s" % (got,))
        check("the ends of a normal line are drawn in the curve colour",
              got.get("line start") == WHITE and got.get("line end") == WHITE, got)
        check("a construction line's points and a centre in the construction colour",
              got.get("construction start") == BLUE and got.get("circle centre") == BLUE, got)
        check("external geometry's points in the external colour",
              got.get("external start") == PINK and got.get("defining start") == PINK, got)

        check("defining external geometry is the external colour until its own is set",
              curve_colour("CurvesExternalDefiningLineSet") == PINK,
              curve_colour("CurvesExternalDefiningLineSet"))
        FreeCAD.ParamGet(COLOURS).SetUnsigned("ExternalDefiningColor", 0x00ff00ff)
        settle(40)
        check("and its own preference then",
              curve_colour("CurvesExternalDefiningLineSet") == (0, 255, 0)
              and curve_colour("CurvesExternalLineSet") == PINK,
              (curve_colour("CurvesExternalDefiningLineSet"),
               curve_colour("CurvesExternalLineSet")))
        FreeCAD.ParamGet(COLOURS).RemUnsigned("ExternalDefiningColor")

        normal = style("")
        unit = normal[0] / 2.0 if normal else 1.0
        cross = coin.SoNode.getByName("RootCrossDrawStyle")
        before = (label_style(view), (cross.lineWidth.getValue(), cross.linePattern.getValue()))
        grp = FreeCAD.ParamGet(VIEW)
        grp.SetInt("DimensionalConstraintLineWidth", 4)
        grp.SetInt("DimensionalConstraintLinePattern", 0xFCFC)
        grp.SetInt("AxisLineWidth", 3)
        grp.SetInt("AxisLinePattern", 0xAAAA)
        settle(40)
        cross = coin.SoNode.getByName("RootCrossDrawStyle")
        after = (label_style(view), (cross.lineWidth.getValue(), cross.linePattern.getValue()))
        note("label and axis style: %s -> %s" % (before, after))
        check("a dimension's leaders take their width and pattern from the preferences",
              before[0] == (2 * unit, 0xFFFF) and after[0] == (4 * unit, 0xFCFC),
              (before[0], after[0]))
        check("and so do the axes",
              before[1] == (2 * unit, 0xFFFF) and after[1] == (3 * unit, 0xAAAA),
              (before[1], after[1]))
        for name in ("DimensionalConstraintLine", "AxisLine"):
            grp.RemInt(name + "Width")
            grp.RemInt(name + "Pattern")
        settle(40)

        # a visual layer's own pattern wins over the class's
        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle(10)
        geos = sk.Geometry
        ext = SketcherGui.ViewProviderSketchGeometryExtension()
        ext.VisualLayerId = 1
        geos[1].setExtension(ext)
        sk.Geometry = geos
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        settle(40)
        got, layered = counts(), counts("Dashed")
        check("a construction line on layer 1 is drawn in its class's layer set",
              got[1] == 0 and layered == [0, 1, 0, 0, 0], "%s, layer sets %s" % (got, layered))
        s, c = style("Construction", "Dashed"), style("Construction")
        check("with the layer's pattern at the construction width",
              s is not None and c is not None and s[1] == 0x7E7E and s[0] == c[0], (s, c))

        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    clear_prefs()
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
