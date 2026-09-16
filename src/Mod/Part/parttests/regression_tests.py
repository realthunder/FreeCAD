from FreeCAD import Vector
import Part

import unittest

class RegressionTests(unittest.TestCase):

    def test_issue_4456(self):
        """
        0004456: Regression : Part.Plane.Intersect does not accept plane as argument
        """
        p1 = Part.Plane()
        p2 = Part.Plane(Vector(0, 0, 0), Vector(1, 0, 0))

        result = p1.intersect(p2)
        line = result.pop()
        self.assertEqual(line.Location, Vector(0, 0, 0))
        self.assertEqual(line.Direction, Vector(0, 1, 0))
        # We should now have empty list...
        with self.assertRaises(IndexError):
            result.pop()

    def test_joinWires_tight_bound_tiles_the_region(self):
        """
        Part.joinWires(tighten=True) must return the minimal cycles of an
        arrangement: one wire per cell, each a valid face, together tiling the
        region exactly.

        This pins the contract that findTightBound() used to break. It returned
        non-minimal cycles -- a wire spanning several cells while those cells
        were also emitted on their own, so the areas summed to MORE than the
        region -- and sometimes a reversed, self-intersecting wire of negative
        area. Counting the wires alone would not catch either; the area sum is
        what makes overlaps and gaps visible.

        The line spacing is deliberately uneven, so a pass cannot come from the
        lines being evenly placed.
        """
        xs = [0.0, 37.0, 91.5, 120.0, 178.25, 203.0, 260.5, 291.0, 344.75, 380.0]
        ys = [0.0, 42.5, 88.0, 131.25, 190.0, 226.5, 271.0, 318.75, 355.0, 400.0]

        edges = []
        for y in ys:
            edges.append(Part.LineSegment(Vector(xs[0], y, 0), Vector(xs[-1], y, 0)).toShape())
        for x in xs:
            edges.append(Part.LineSegment(Vector(x, ys[0], 0), Vector(x, ys[-1], 0)).toShape())

        result = Part.joinWires(Part.Compound(edges), split=True, merge=True, tighten=True)
        wires = result.Wires

        # one wire per cell of the grid
        self.assertEqual(len(wires), (len(xs) - 1) * (len(ys) - 1))

        total = 0.0
        for wire in wires:
            self.assertTrue(wire.isClosed())
            area = Part.Face(wire).Area
            # a reversed, self-intersecting cycle does not have a positive area
            self.assertGreater(area, 0.0)
            total += area

        # exactly tiling the region: no cell counted twice, none missing
        region = (xs[-1] - xs[0]) * (ys[-1] - ys[0])
        self.assertAlmostEqual(total, region, delta=region * 1e-9)

    def test_OptimalBox(self):
        box = Part.makeBox(1, 1, 1)
        self.assertTrue(box.optimalBoundingBox(True, False).isValid())
