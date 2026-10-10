# -*- coding: utf-8 -*-
# Tests for Shape.diff(): what became of each element between two states of
# a shape (Part::ShapeDiff, docs/TransactionLog.md sec 32.6). The pairing is
# tested in C++ (tests/src/Mod/Part/App/ShapeDiff.cpp); this is what Python
# is given.

import collections
import unittest

import FreeCAD as App
import Part


class ShapeDiffTest(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("ShapeDiffTest")
        self.box = self.doc.addObject("Part::Box", "Box")
        self.box.Length, self.box.Width, self.box.Height = 40, 20, 10
        self.first = self.cut(self.box, 10)
        self.last = self.cut(self.first, 30)
        self.doc.recompute()

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def cut(self, base, x):
        tool = self.doc.addObject("Part::Cylinder", "Tool")
        tool.Radius, tool.Height = 2, 100
        tool.Placement.Base = App.Vector(x, 10, -50)
        cut = self.doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = base, tool
        return cut

    def testTheSameShape(self):
        shape = self.last.Shape
        diff = shape.diff(shape)
        self.assertTrue(diff["same"])
        self.assertFalse(diff["moved"])
        self.assertNotIn("motion", diff)
        self.assertEqual(len(diff["Face"]), 8)
        self.assertEqual(diff["Face"][0], ("Face1", "Face1", "shape", "same"))
        self.assertNotIn("Edge", diff)
        self.assertNotIn("Vertex", diff)
        self.assertIn("names", diff["cost"])

    def testMoved(self):
        before = self.last.Shape
        self.last.Placement.Base = App.Vector(0, 0, 7)
        self.doc.recompute()
        diff = before.diff(self.last.Shape)
        self.assertFalse(diff["same"])
        self.assertTrue(diff["moved"])
        self.assertAlmostEqual(diff["motion"].A34, 7.0)
        self.assertEqual({e[3] for e in diff["Face"]}, {"moved"})

    def testAParameter(self):
        before = self.last.Shape
        self.box.Height = 12
        self.doc.recompute()
        diff = before.diff(self.last.Shape, edges=True, vertices=True)
        self.assertFalse(diff["same"])
        self.assertFalse(diff["moved"])
        what = collections.Counter(e[3] for e in diff["Face"])
        self.assertEqual(what, {"same": 1, "moved": 1, "changed": 6})
        self.assertEqual({e[2] for e in diff["Face"]}, {"name"})
        self.assertEqual(len(diff["Edge"]), len(before.Edges))
        self.assertEqual(len(diff["Vertex"]), len(before.Vertexes))

    def testAFeaturePutInEarly(self):
        before = self.last.Shape
        self.first.Base = self.cut(self.box, 20)
        self.doc.recompute()
        after = self.last.Shape
        diff = before.diff(after, geometry=False, around=False)
        added = [e for e in diff["Face"] if e[3] == "added"]
        self.assertEqual(len(added), 1)
        self.assertIsNone(added[0][0])
        self.assertEqual(added[0][2], "")
        # The one face only the later shape has is the new hole's wall
        wall = after.getElement(added[0][1])
        self.assertEqual(wall.Surface.TypeId, "Part::GeomCylinder")
        self.assertAlmostEqual(wall.Surface.Center.x, 20.0)
        by = collections.Counter(e[2] for e in diff["Face"])
        self.assertGreater(by["steps"], 0)
        self.assertEqual(by["name"] + by["steps"], 8)
        self.assertEqual([e for e in diff["Face"] if e[3] == "removed"], [])
