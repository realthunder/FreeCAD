# SPDX-License-Identifier: LGPL-2.1-or-later
# Tests for external geometry ids: a face projected by hidden line removal,
# and an edge of a planar face or a wire, names each piece after the element
# of the reference it came from (ExternalGeometryExtension.RefElement), and
# the id of a piece follows that name across rebuilds instead of its position
# in the projection's output.

import os
import tempfile
import unittest

import FreeCAD as App
import Sketcher
from FreeCAD import Vector


def externals(sketch):
    """(id, RefElement, geometry) of every external geometry with a reference."""
    out = []
    for geo in sketch.ExternalGeo:
        facade = Sketcher.ExternalGeometryFacade(geo)
        if facade.Ref:
            out.append((facade.Id, facade.RefElement, geo))
    return out


class TestSketchExternalGeometry(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("TestSketchExternalGeometry")

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def sideSketchOnCylinder(self):
        # the sketch on XZ, the cylinder side seen across its axis: two
        # silhouette lines and the two rims seen edge on
        cyl = self.doc.addObject("Part::Cylinder", "Cyl")
        sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        sketch.Placement = App.Placement(Vector(), App.Rotation(Vector(1, 0, 0), 90))
        self.doc.recompute()
        sketch.addExternal("Cyl", "Face1")
        self.doc.recompute()
        return cyl, sketch

    def testProjectedPiecesAreNamed(self):
        cyl, sketch = self.sideSketchOnCylinder()
        ext = externals(sketch)
        self.assertEqual(len(ext), 4)
        names = [name for _, name, _ in ext]
        self.assertTrue(all(names), names)
        self.assertEqual(len(set(names)), 4)
        # one silhouette is the algorithm's own, from the side face; the
        # other is where the seam lies, and is the seam's projection
        silhouettes = [name for _, name, geo in ext if abs(geo.length() - cyl.Height.Value) < 1e-6]
        self.assertEqual(sorted(name.split(";")[0] for name in silhouettes), ["Edge1", "Face1"])

    def testIdsFollowTheNameNotThePosition(self):
        cyl, sketch = self.sideSketchOnCylinder()
        before = {name: id for id, name, _ in externals(sketch)}
        # a partial cylinder has more edges, and its projection comes out
        # in another order and count; the pieces that survive keep their ids
        cyl.Angle = 270
        self.doc.recompute()
        after = {name: id for id, name, _ in externals(sketch)}
        common = set(before) & set(after)
        self.assertTrue(common, "nothing survived: %s vs %s" % (before, after))
        for name in common:
            self.assertEqual(before[name], after[name], name)
        # and back: what stayed keeps its id; a piece that vanished and
        # came back (a rim hidden by the partial cylinder) is a new one,
        # nothing kept its old id alive
        cyl.Angle = 360
        self.doc.recompute()
        again = {name: id for id, name, _ in externals(sketch)}
        self.assertEqual(set(again), set(before))
        for name in common:
            self.assertEqual(before[name], again[name], name)
        for name in set(before) - common:
            self.assertNotIn(again[name], before.values(), name)

    def testNamesSurviveSaveAndLoad(self):
        cyl, sketch = self.sideSketchOnCylinder()
        before = sorted((id, name) for id, name, _ in externals(sketch))
        filename = os.path.join(tempfile.gettempdir(), "TestSketchExternalGeometry.FCStd")
        self.doc.saveAs(filename)
        App.closeDocument(self.doc.Name)
        self.doc = App.openDocument(filename)
        sketch = self.doc.getObject("Sketch")
        self.assertEqual(sorted((id, name) for id, name, _ in externals(sketch)), before)
        # a rebuild after loading matches by the stored names
        self.doc.getObject("Cyl").Radius = 3
        self.doc.recompute()
        self.assertEqual(sorted((id, name) for id, name, _ in externals(sketch)), before)

    def notchedTopFace(self):
        # a box with a notch cut into one edge of its top face: the top face
        # keeps three of the box's edges, named after them, and gains the
        # notch's; the sketch lies on that face
        box = self.doc.addObject("Part::Box", "Box")
        notch = self.doc.addObject("Part::Box", "Notch")
        notch.Length = 2
        notch.Width = 2
        notch.Height = 2
        notch.Placement.Base = Vector(4, -1, 9)
        cut = self.doc.addObject("Part::Cut", "Cut")
        cut.Base = box
        cut.Tool = notch
        sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        sketch.Placement = App.Placement(Vector(0, 0, 10), App.Rotation())
        self.doc.recompute()
        top = [i + 1 for i, f in enumerate(cut.Shape.Faces) if abs(f.CenterOfMass.z - 10) < 1e-6]
        self.assertEqual(len(top), 1)
        sketch.addExternal("Cut", "Face%d" % top[0])
        self.doc.recompute()
        return notch, sketch

    def testPlanarFacePiecesAreNamed(self):
        # a planar face goes through the edge path: each piece is named
        # after the edge of the face it came from
        box = self.doc.addObject("Part::Box", "Box")
        sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        self.doc.recompute()
        sketch.addExternal("Box", "Face6")
        self.doc.recompute()
        ext = externals(sketch)
        self.assertEqual(len(ext), 4)
        names = [name for _, name, _ in ext]
        self.assertTrue(all(names), names)
        self.assertEqual(len(set(names)), 4)
        # a primitive's mapped name is its element name
        for name in names:
            self.assertRegex(name, r"^Edge\d+$")

    def testPlanarFaceIdsFollowTheName(self):
        notch, sketch = self.notchedTopFace()
        before = {name: id for id, name, _ in externals(sketch)}
        self.assertEqual(len(before), 8)
        # the notch moves along the edge: the face's own edges keep their
        # names and ids whatever the notch does to the order
        notch.Placement.Base = Vector(6, -1, 9)
        self.doc.recompute()
        after = {name: id for id, name, _ in externals(sketch)}
        common = set(before) & set(after)
        self.assertTrue(len(common) >= 3, "nothing survived: %s vs %s" % (before, after))
        for name in common:
            self.assertEqual(before[name], after[name], name)

    def testCollapsedSegmentIsPositional(self):
        # a planar face seen edge on collapses into one segment spanning
        # its projected edges; that segment is nobody's edge, so it has no
        # name and keeps its id by position, as before
        box = self.doc.addObject("Part::Box", "Box")
        sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        self.doc.recompute()
        sketch.addExternal("Box", "Face1")
        self.doc.recompute()
        ext = externals(sketch)
        self.assertEqual(len(ext), 1)
        self.assertEqual(ext[0][1], "")
        ids = [id for id, _, _ in ext]
        box.Width = 20
        self.doc.recompute()
        ext = externals(sketch)
        self.assertEqual([id for id, _, _ in ext], ids)
        self.assertAlmostEqual(ext[0][2].length(), 20)

    def originSketch(self):
        """A sketch on an App::Part's XY plane, with that origin's features."""
        part = self.doc.addObject("App::Part", "Part")
        roles = {f.Role: f for f in part.Origin.OriginFeatures}
        sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        part.addObject(sketch)
        sketch.AttachmentSupport = [(roles["XY_Plane"], "")]
        sketch.MapMode = "FlatFace"
        self.doc.recompute()
        return roles, sketch

    def testOriginAxisIsExternalGeometry(self):
        # This one already worked: Part::Feature::getTopoShape() hands back
        # a synthesized infinite edge for an App::Line (PartFeature.cpp), so
        # the axis goes through the ordinary path. Here to hold that, since
        # it would be easy to "fix" it by building the edge by hand and lose
        # the element map that path carries.
        roles, sketch = self.originSketch()
        sketch.addExternal(roles["Y_Axis"].Name, "")
        self.doc.recompute()
        self.assertEqual(len(sketch.ExternalGeometry), 1)
        geo = sketch.ExternalGeo[-1]
        self.assertEqual(geo.TypeId, "Part::GeomLineSegment")
        # the Y axis seen on the XY plane is the sketch's own y direction
        direction = geo.EndPoint - geo.StartPoint
        self.assertAlmostEqual(abs(direction.normalize().y), 1.0)

    def testOriginPointIsExternalGeometry(self):
        # App::Point is the one datum element with no shape at all --
        # PartFeature.cpp has no case for it -- so the projection builds the
        # vertex. This is what the Datums port actually unblocked.
        roles, sketch = self.originSketch()
        sketch.addExternal(roles["Origin"].Name, "")
        self.doc.recompute()
        self.assertEqual(len(sketch.ExternalGeometry), 1)
        geo = sketch.ExternalGeo[-1]
        self.assertEqual(geo.TypeId, "Part::GeomPoint")
        self.assertAlmostEqual(geo.X, 0.0)
        self.assertAlmostEqual(geo.Y, 0.0)

    def testOriginExternalsSurviveSaveAndLoad(self):
        # External geometry is re-projected on restore, so the branches
        # above have to hold there too.
        roles, sketch = self.originSketch()
        sketch.addExternal(roles["Y_Axis"].Name, "")
        sketch.addExternal(roles["Origin"].Name, "")
        self.doc.recompute()
        before = [g.TypeId for g in sketch.ExternalGeo]
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "origin_externals.FCStd")
            self.doc.saveAs(path)
            name = self.doc.Name
            App.closeDocument(name)
            self.doc = App.open(path)
            sketch = self.doc.getObject("Sketch")
            self.doc.recompute()
            self.assertEqual([g.TypeId for g in sketch.ExternalGeo], before)
            self.assertNotIn("Invalid", sketch.State)
