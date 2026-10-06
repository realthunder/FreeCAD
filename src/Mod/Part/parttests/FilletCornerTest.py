# -*- coding: utf-8 -*-
# Tests for makeFillet()'s corners: setback corners, where the fillets at a
# vertex each stop a distance from it and one patch closes the opening
# (docs/CornerBlending.md). Only the OCCT fork has them; elsewhere the tests
# check that asking for one fails, and skip the rest.

import unittest

import FreeCAD as App
import Part


def cornerOf(shape, point):
    """The vertex of shape at point, and the edges ending there"""
    vertex = [v for v in shape.Vertexes if v.Point.isEqual(point, 1e-7)][0]
    edges = [e for e in shape.Edges if any(v.isSame(vertex) for v in e.Vertexes)]
    return vertex, edges


def nameOf(shape, sub):
    kind, index = shape.findSubShape(sub)
    return "%s%d" % (kind, index)


def patchesOf(shape):
    return [f for f in shape.Faces if f.Surface.TypeId == "Part::GeomBSplineSurface"]


class FilletCornerTest(unittest.TestCase):
    def setUp(self):
        self.box = Part.makeBox(10, 10, 10)
        self.vertex, self.edges = cornerOf(self.box, App.Vector(10, 10, 10))
        try:
            self.box.makeFillet(1, self.edges, corners={self.vertex: 0})
        except Exception as e:
            if "OCCT fork" not in str(e):
                raise
            self.skipTest("setback corners need the OCCT fork")

    def fillet(self, corners, edges=None):
        shape = self.box.makeFillet(1, self.edges if edges is None else edges, corners=corners)
        self.assertTrue(shape.isValid())
        self.assertEqual(len(shape.Solids), 1)
        return shape

    def testSetbackGrowsCorner(self):
        plain = self.box.makeFillet(1, self.edges)
        volumes = []
        for setback in (0, 2, 4):
            shape = self.fillet({self.vertex: setback})
            self.assertEqual(len(patchesOf(shape)), 1)
            volumes.append(shape.Volume)
        # At 0 the corner is set back only as far as the fillets meet: today's
        # sphere corner, built as a patch
        self.assertAlmostEqual(volumes[0], plain.Volume, delta=0.01)
        self.assertGreater(volumes[0], volumes[1])
        self.assertGreater(volumes[1], volumes[2])
        # the corner moves away from the vertex as it is set back
        dist = [
            self.fillet({self.vertex: s}).distToShape(self.vertex)[0] for s in (0, 4)
        ]
        self.assertGreater(dist[1], dist[0] + 0.3)

    def testForms(self):
        expect = self.fillet({self.vertex: 2}).Volume
        names = [nameOf(self.box, e) for e in self.edges]
        forms = [
            {nameOf(self.box, self.vertex): 2},
            [(self.vertex, 2.0)],
            {self.vertex: {e: 2 for e in names}},
            {self.vertex: [(e, 2) for e in self.edges]},
            {self.vertex: (2, {names[0]: 2})},
        ]
        for corners in forms:
            self.assertAlmostEqual(self.fillet(corners).Volume, expect, places=6, msg=corners)
        self.assertAlmostEqual(
            self.box.makeFillet(1, 1, self.edges, corners={self.vertex: 2}).Volume,
            expect,
            places=6,
        )
        self.assertAlmostEqual(
            self.box.makeFillet(radius=1, edges=self.edges, corners={self.vertex: 2}).Volume,
            expect,
            places=6,
        )

    def testPerEdge(self):
        names = [nameOf(self.box, e) for e in self.edges]
        even = self.fillet({self.vertex: 2}).Volume
        uneven = self.fillet({self.vertex: {names[0]: 3, names[1]: 1.5, names[2]: 2}})
        self.assertEqual(len(patchesOf(uneven)), 1)
        self.assertNotAlmostEqual(uneven.Volume, even, places=3)
        # an edge's own setback is over the vertex's
        both = self.fillet({self.vertex: (2, {names[0]: 3})})
        self.assertNotAlmostEqual(both.Volume, even, places=3)

    def testSharpEdge(self):
        # two fillets and a sharp edge: the patch cuts the sharp edge as far
        # back as the fillets beside it
        shape = self.fillet({self.vertex: 2}, self.edges[:2])
        self.assertEqual(len(patchesOf(shape)), 1)
        self.assertLess(shape.Volume, self.box.makeFillet(1, self.edges[:2]).Volume)

    def testCornerFaceName(self):
        doc = App.newDocument()
        try:
            box = doc.addObject("Part::Box", "Box")
            doc.recompute()
            vertex, edges = cornerOf(box.Shape, App.Vector(10, 10, 10))
            vname = nameOf(box.Shape, vertex)
            plain = box.Shape.makeFillet(1, edges)
            shape = box.Shape.makeFillet(1, edges, corners={vertex: 2})
            # the patch is generated from the vertex, as the sphere corner
            # it stands for is
            patch = shape.findSubShape(patchesOf(shape)[0])
            name = shape.getElementMappedName("Face%d" % patch[1])
            self.assertTrue(name.startswith(vname + ";"), name)
            sphere = [f for f in plain.Faces if f.Surface.TypeId == "Part::GeomSphere"][0]
            sphere = plain.getElementMappedName("Face%d" % plain.findSubShape(sphere)[1])
            self.assertEqual(name, sphere)
        finally:
            App.closeDocument(doc.Name)

    def testErrors(self):
        far = cornerOf(self.box, App.Vector(0, 0, 0))[0]
        with self.assertRaisesRegex(Exception, "no fillet ends at corner"):
            self.box.makeFillet(1, self.edges, corners={far: 1})
        sharp = self.edges[2]
        with self.assertRaisesRegex(Exception, "is not filleted"):
            self.box.makeFillet(1, self.edges[:2], corners={self.vertex: {sharp: 1}})
        # a fillet on an edge away from the corner
        other = [
            e
            for e in self.box.Edges
            if not any(v.isSame(self.vertex) for v in e.Vertexes)
            and any(v.isSame(far) for v in e.Vertexes)
        ][0]
        with self.assertRaisesRegex(Exception, "does not end there"):
            self.box.makeFillet(1, self.edges + [other], corners={self.vertex: {other: 1}})
        with self.assertRaises(Exception):
            self.box.makeFillet(1, self.edges, corners={self.edges[0]: 1})
        with self.assertRaises(TypeError):
            self.box.makeFillet(1, self.edges, corners={1: 1})
        with self.assertRaises(TypeError):
            self.box.makeFillet(1, self.edges, corners="Vertex1")
        with self.assertRaises(Exception):
            self.box.makeFillet(1, self.edges, corners={"Vertex99": 1})
