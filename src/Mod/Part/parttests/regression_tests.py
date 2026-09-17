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
        # each region on its own: a wire whose edges are not all oriented the
        # way it is travelled makes a face of negative area, and two of those
        # can still add up to 217
        areas = sorted(round(Part.Face(w).Area, 6) for w in wires)
        self.assertEqual(areas, [3.0, 5.0, 20.0, 31.0, 42.0, 44.0, 72.0])

    def test_joinWires_orients_reversed_input_edges(self):
        """
        The wires come out oriented however the input edges were.

        A wire is assembled from the edges as the traversal walks them, and
        that direction is decided by an edge's parametric ends, not by the
        orientation it happens to carry. Half of these edges are handed in
        REVERSED; every cell must still be a positive face, and the diagonal
        makes the two chains between its ends run opposite ways.
        """
        def seg(a, b):
            return Part.LineSegment(Vector(*a), Vector(*b)).toShape()

        edges = []
        for i in range(3):
            c = 10.0 * i
            edges.append(seg((c, 0, 0), (c, 20, 0)))
            edges.append(seg((0, c, 0), (20, c, 0)).reversed())
        edges.append(seg((0, 0, 0), (10, 10, 0)).reversed())
        result = Part.joinWires(Part.Compound(edges), split=True, merge=True,
                                tighten=True)
        areas = sorted(round(Part.Face(w).Area, 6) for w in result.Wires)
        self.assertEqual(areas, [50.0, 50.0, 100.0, 100.0, 100.0])

    def test_joinWires_tolerance_closes_gaps(self):
        """
        A tolerance wider than the gaps between the edges closes them.

        The coincident-vertex lookup is a grid hash sized from the tolerance,
        so this pins that a tolerance given by the caller reaches it: a square
        whose corners miss by 0.02 is four open edges at the default tolerance
        and one closed wire at 0.05.
        """
        def seg(a, b):
            return Part.LineSegment(Vector(*a), Vector(*b)).toShape()

        d = 0.02
        edges = [seg((0, 0, 0), (10, 0, 0)), seg((10, d, 0), (10, 10, 0)),
                 seg((10, 10 + d, 0), (0, 10, 0)), seg((-d, 10, 0), (-d, 0, 0))]
        # nothing closes at the default tolerance; that joinWires raises on
        # an empty result rather than returning one is how it has always
        # behaved and is not what this test is about
        with self.assertRaises(Exception):
            Part.joinWires(Part.Compound(edges), split=True, merge=True,
                           tighten=True)
        result = Part.joinWires(Part.Compound(edges), split=True, merge=True,
                                tighten=True, tol=0.05)
        self.assertEqual(len(result.Wires), 1)
        self.assertAlmostEqual(Part.Face(result.Wires[0]).Area, 100.0, delta=1.0)

    def test_joinWires_plain_path_keeps_a_merged_loop(self):
        """
        Part.joinWires(merge=True, tighten=False) returns a loop that merging
        closed on itself.

        Merging joins the edges of a square into one chain, and the chain is
        closed. The plain (tighten=False) path added closed edges to its result
        only before merging ran, and its search skips a chain that is already
        closed, so a merged loop was dropped: a lone square raised on an empty
        result, and a square next to a circle came back as the circle alone.
        Part::Face with SplitEdges on and TightBound off runs this path.
        """
        def seg(a, b):
            return Part.LineSegment(Vector(*a), Vector(*b)).toShape()

        def square(x, y, s):
            return [seg((x, y, 0), (x + s, y, 0)), seg((x + s, y, 0), (x + s, y + s, 0)),
                    seg((x + s, y + s, 0), (x, y + s, 0)), seg((x, y + s, 0), (x, y, 0))]

        result = Part.joinWires(Part.Compound(square(0, 0, 10)), split=True, merge=True,
                                tighten=False)
        self.assertEqual([len(w.Edges) for w in result.Wires], [4])
        self.assertAlmostEqual(Part.Face(result.Wires[0]).Area, 100.0)

        # a loop that was closed on entry is still emitted, and first
        circle = Part.Circle(Vector(0, 0, 0), Vector(0, 0, 1), 3).toShape()
        result = Part.joinWires(Part.Compound([circle] + square(10, 0, 10)), split=True,
                                merge=True, tighten=False)
        self.assertEqual([len(w.Edges) for w in result.Wires], [1, 4])

        # two loops joined by a bridge: each loop is a closed chain sharing a
        # vertex with the bridge, and the bridge, which bounds nothing, is the
        # one open edge (an untouched input edge, which keep_open drops unless
        # asked to keep the originals)
        edges = square(0, 0, 10) + square(20, 0, 10) + [seg((10, 5, 0), (20, 5, 0))]
        closed, opened = Part.joinWires(Part.Compound(edges), split=True, merge=True,
                                        tighten=False, keep_open=True,
                                        no_open_original=False)
        self.assertEqual(sorted(round(Part.Face(w).Area) for w in closed.Wires), [100, 100])
        self.assertEqual(len(opened.Edges), 1)
        self.assertAlmostEqual(opened.Edges[0].Length, 10.0)

    def test_joinWires_tangent_circles_touch_at_a_point(self):
        """
        Two circles tangent to each other touch at a point, not along a stretch.

        The 2D intersector reports two tangent curves as an overlap segment:
        with its 1e-10 tolerance, circles of radius 10 and 5 stay that close
        for some 3e-5 on either side of the touch point. Taking the segment's
        ends as split points cut two fragments that short off each circle and
        emitted the small disk three times over. A segment that does not end
        where an edge ends is a touch and contributes the touch point alone.
        """
        import math

        def arc(c, r, a0, a1):
            return Part.ArcOfCircle(Part.Circle(Vector(*c), Vector(0, 0, 1), r), a0, a1).toShape()

        # touching at a vertex of both circles
        big = [arc((0, 0, 0), 10, 0, math.pi), arc((0, 0, 0), 10, math.pi, 2 * math.pi)]
        small = [arc((15, 0, 0), 5, 0, math.pi), arc((15, 0, 0), 5, math.pi, 2 * math.pi)]
        result = Part.joinWires(Part.Compound(big + small), split=True, merge=True,
                                tighten=True)
        areas = sorted(round(Part.Face(w).Area, 3) for w in result.Wires)
        self.assertEqual(areas, [round(25 * math.pi, 3), round(100 * math.pi, 3)])
        self.assertEqual(len(result.Edges), 4)

        # touching in the middle of both edges: each circle is split there
        circles = [Part.Circle(Vector(0, 0, 0), Vector(0, 0, 1), 10).toShape(),
                   Part.Circle(Vector(0, 15, 0), Vector(0, 0, 1), 5).toShape()]
        result = Part.joinWires(Part.Compound(circles), split=True, merge=True,
                                tighten=True)
        areas = sorted(round(Part.Face(w).Area, 3) for w in result.Wires)
        self.assertEqual(areas, [round(25 * math.pi, 3), round(100 * math.pi, 3)])
        self.assertEqual(sorted(len(w.Edges) for w in result.Wires), [2, 2])
        for e in result.Edges:
            self.assertGreater(e.Length, 1.0)

    def test_joinWires_splits_a_collinear_overlap(self):
        """
        An edge that runs along part of another is cut where the overlap ends.

        A 3 x 50 rectangle whose right side is drawn as three collinear
        pieces, one of them 0.01 long and lying on both of its neighbours
        (the fixture behind test_28534_truncated_pocket). Two edges that
        share a stretch come back from the intersector as a segment, not as
        points; each end of that stretch is a split. The result is one wire
        of 150 units whose right side is exactly the three pieces, none of
        them overlapping another.
        """
        def seg(a, b):
            return Part.LineSegment(Vector(*a), Vector(*b)).toShape()

        edges = [seg((0, 50, 0), (0, 0, 0)), seg((0, 0, 0), (3, 0, 0)),
                 seg((3, 0, 0), (3, 30, 0)), seg((3, 50, 0), (0, 50, 0)),
                 seg((3, 29.99, 0), (3, 30, 0)), seg((3, 50, 0), (3, 29.99, 0))]
        result = Part.joinWires(Part.Compound(edges), split=True, merge=True,
                                tighten=True)
        self.assertEqual(len(result.Wires), 1)
        self.assertAlmostEqual(Part.Face(result.Wires[0]).Area, 150.0, places=6)
        lengths = sorted(round(e.Length, 6) for e in result.Wires[0].Edges)
        self.assertEqual(lengths, [0.01, 3.0, 3.0, 20.0, 29.99, 50.0])

    def test_joinWires_nested_circles(self):
        """
        Concentric circles do not intersect, and every one is its own wire.

        Every bounding box here contains all the smaller ones, so the box
        query prunes nothing and every pair is checked; this is the input on
        which building a face per pair cost seconds.
        """
        circles = [Part.Circle(Vector(0, 0, 0), Vector(0, 0, 1),
                               100.0 * (i + 1) / 42).toShape() for i in range(40)]
        result = Part.joinWires(Part.Compound(circles), split=True, merge=True,
                                tighten=True)
        self.assertEqual(len(result.Wires), 40)
        self.assertEqual(len(result.Edges), 40)

    def test_OptimalBox(self):
        box = Part.makeBox(1, 1, 1)
        self.assertTrue(box.optimalBoundingBox(True, False).isValid())
