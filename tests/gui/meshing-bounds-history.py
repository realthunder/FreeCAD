"""A shape tessellates the same whatever mesh it already carries.

Found 2026-09-30 by progressive-load-diff.py on a user file (error.FCStd
of mail/2022-10-11_w_52240, docs/DocumentLoad.md sec 16): a compound of
92 B-spline edges refined to 86569 points in a process's first open and
86662 in its later ones. Every tessellation parameter -- the display
deflection, the exact deflection a coarse-first build hands the refine,
a ladder rung, the texture frame -- derived from a BRepBndLib::Add box,
and that reads any triangulation or 3D polygon the shape holds: a
coarse rung, when a second coarse build ran over the first, gave a box
0.2% smaller, a smaller deflection, 93 more points. OCCT's mesher then
reuses a resident polygon within 10% of the ask, so nothing corrected
it. The box now comes from the geometry alone (PartGui::meshingBounds),
kept per shape since it costs more than the mesh box did.

Scene: two Part::Features holding the same geometry -- 30 cubic
B-splines as free edges, whose poles swing far past the curve (OCCT
boxes a B-spline's geometry by its poles, a polygon by the curve), and
AngularDeflection 90 so the linear deflection decides the point counts.
One shape is meshed coarsely in place before it is assigned
(TopoShape.exportStl meshes the shape it is called on), the other is
built fresh. Claims:
  - the premeshed shape carries its coarse polygons: its mesh box is
    smaller than the geometry's (else nothing below tests the box);
  - once both are built and refined, the two draw the same number of
    line points.

Scored against the tree before the fix: the premeshed object draws 810
points, the fresh one 780.
"""
import os
import tempfile
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
V = FreeCAD.Vector
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def wait(seconds):
    t = time.perf_counter()
    while time.perf_counter() - t < seconds:
        QtCore.QCoreApplication.processEvents()


def curves():
    """B-splines whose poles swing far past the curve: OCCT boxes the
    geometry of one by its poles, any polygon by the curve, so a box read
    off a mesh is a fraction of the geometry's whatever the mesh."""
    edges = []
    for i in range(30):
        bs = Part.BSplineCurve()
        bs.buildFromPoles([V(0, 20 * i, 0), V(100, 20 * i + 300, 0),
                           V(200, 20 * i - 300, 0), V(300, 20 * i, 0)])
        edges.append(bs.toShape())
    return Part.makeCompound(edges)


def line_points(vp):
    sa = coin.SoSearchAction()
    sa.setType(coin.SoCoordinate3.getClassTypeId())
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(vp.RootNode)
    paths = sa.getPaths()
    return max((paths.get(i).getTail().point.getNum() for i in range(paths.getLength())),
               default=0)


def settled(vps, limit=30.0):
    """Coarse first, then the refine: wait until no count moves for 2 s."""
    t = time.perf_counter()
    last, since = None, time.perf_counter()
    while time.perf_counter() - t < limit:
        wait(0.25)
        now = tuple(line_points(vp) for vp in vps)
        if now != last:
            last, since = now, time.perf_counter()
        elif time.perf_counter() - since >= 2.0:
            break
    return last


def run():
    doc = FreeCAD.newDocument("MeshBox")
    premeshed = curves()
    stl = os.path.join(tempfile.mkdtemp(dir=OUT), "coarse.stl")
    premeshed.exportStl(stl, 60.0)
    # Shape.BoundBox is BRepBndLib::Add with the mesh: off the polygons it
    # is smaller than the geometry's box. That difference is what the old
    # deflection inherited.
    bm, bg = premeshed.BoundBox, curves().BoundBox
    check("the premeshed shape carries its coarse polygons",
          bm.YLength < 0.9 * bg.YLength,
          "box off the polygons %.3f x %.3f, geometry %.3f x %.3f"
          % (bm.XLength, bm.YLength, bg.XLength, bg.YLength))
    a = doc.addObject("Part::Feature", "Premeshed")
    b = doc.addObject("Part::Feature", "Fresh")
    for obj in (a, b):
        obj.ViewObject.AngularDeflection = 90
    a.Shape = premeshed
    b.Shape = curves()
    doc.recompute()
    na, nb = settled([a.ViewObject, b.ViewObject])
    check("a premeshed shape draws as many line points as a fresh one",
          na == nb and na > 0, "premeshed %s, fresh %s" % (na, nb))
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)


try:
    run()
except Exception:
    note("ABORT " + traceback.format_exc())
note("DONE")
QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)
