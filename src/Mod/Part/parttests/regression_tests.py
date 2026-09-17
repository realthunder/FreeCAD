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

    def test_joinWires_angle_and_search_agree(self):
        """
        Part.joinWires(tighten=True) finds the minimal wires by the angular
        order of the edges at each vertex when the input is planar, and by
        search otherwise. angle=False forces the search. The two must answer
        the same thing.

        The arrangement mixes lines with arcs, puts a tangency at one vertex
        (where two edges leave in the same direction, the case the angular
        order cannot resolve on its own), and hangs an edge off a corner that
        bounds nothing.
        """
        edges = [
            Part.Circle(Vector(0, 0, 0), Vector(0, 0, 1), 10).toShape(),
            Part.Circle(Vector(0, 0, 0), Vector(0, 0, 1), 4).toShape(),
            Part.LineSegment(Vector(4, 0, 0), Vector(10, 0, 0)).toShape(),
            Part.LineSegment(Vector(-10, 0, 0), Vector(-4, 0, 0)).toShape(),
            # a box around the circle, touching it at (0, 10): a tangency
            Part.LineSegment(Vector(-20, 10, 0), Vector(20, 10, 0)).toShape(),
            Part.LineSegment(Vector(-20, 10, 0), Vector(-20, -20, 0)).toShape(),
            Part.LineSegment(Vector(-20, -20, 0), Vector(20, -20, 0)).toShape(),
            Part.LineSegment(Vector(20, -20, 0), Vector(20, 10, 0)).toShape(),
            # bounds nothing, so it belongs in neither answer
            Part.LineSegment(Vector(20, 10, 0), Vector(30, 20, 0)).toShape(),
        ]
        shape = Part.Compound(edges)

        def areas(angle):
            result = Part.joinWires(shape, split=True, merge=True,
                                    tighten=True, angle=angle)
            return sorted(round(Part.Face(w).Area, 7) for w in result.Wires)

        byangle = areas(True)
        bysearch = areas(False)
        self.assertEqual(byangle, bysearch)
        # and it is the right answer: the two halves of the annulus, the inner
        # disc, and what is left of the box outside the circle
        self.assertEqual(len(byangle), 4)

    def test_joinWires_overlapping_rectangles(self):
        """
        Two overlapping rectangles make three regions, three make seven.

        This is the shape of input where the edges between two branch points
        are merged into one chain each: the arrangement comes down to four
        chains between two vertices, so each face boundary is only two edges
        long and says nothing about where those edges went. Anything that
        judges a loop by its end points alone -- which is how the angle
        traversal first told a bounded face from the unbounded one -- gets
        every area here wrong and returns the innermost cell alone.
        """
        def rect(x0, y0, x1, y1):
            corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
            return [Part.LineSegment(Vector(corners[i][0], corners[i][1], 0),
                                     Vector(corners[(i + 1) % 4][0],
                                            corners[(i + 1) % 4][1], 0)).toShape()
                    for i in range(4)]

        two = Part.Compound(rect(0, 0, 10, 10) + rect(5, 5, 15, 15))
        result = Part.joinWires(two, split=True, merge=True, tighten=True)
        areas = sorted(round(Part.Face(w).Area, 6) for w in result.Wires)
        self.assertEqual(areas, [25.0, 75.0, 75.0])

        three = Part.Compound(rect(0, 0, 10, 10) + rect(5, 5, 15, 15)
                              + rect(-3, 4, 6, 13))
        result = Part.joinWires(three, split=True, merge=True, tighten=True)
        wires = result.Wires
        self.assertEqual(len(wires), 7)
        # the union of three 100 unit squares overlapping as placed
        self.assertAlmostEqual(sum(Part.Face(w).Area for w in wires), 217.0,
                               places=6)

    def test_OptimalBox(self):
        box = Part.makeBox(1, 1, 1)
        self.assertTrue(box.optimalBoundingBox(True, False).isValid())
