# SPDX-License-Identifier: LGPL-2.1-or-later
# Tests for external geometry ids: a face projected by hidden line removal
# names each piece after the element of the reference it came from
# (ExternalGeometryExtension.RefElement), and the id of a piece follows that
# name across rebuilds instead of its position in the projection's output.

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

    def testUnnamedReferenceKeepsPositionalIds(self):
        # a planar face goes through the edge path, which names nothing:
        # the ids are handed out by position, as before
        box = self.doc.addObject("Part::Box", "Box")
        sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        self.doc.recompute()
        sketch.addExternal("Box", "Face6")
        self.doc.recompute()
        ext = externals(sketch)
        self.assertEqual(len(ext), 4)
        self.assertEqual([name for _, name, _ in ext], [""] * 4)
        ids = [id for id, _, _ in ext]
        box.Length = 20
        self.doc.recompute()
        self.assertEqual([id for id, _, _ in externals(sketch)], ids)
