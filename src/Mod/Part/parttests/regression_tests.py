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
        # the two halves of the annulus, the inner disc, and the box with the
        # circle taken out of it: the circle touches the box at one point, so
        # the region between them is one face whose boundary passes that
        # point twice (see test_joinWires_pinched_loop)
        import math
        self.assertEqual(byangle, [round(16 * math.pi, 7), round(42 * math.pi, 7),
                                   round(42 * math.pi, 7), round(1200 - 100 * math.pi, 7)])
        # the search cannot pass a vertex twice and returns the box whole over
        # the circle; that difference is deliberate, everything else agrees
        self.assertEqual(bysearch[:3], byangle[:3])
        self.assertEqual(bysearch[3], 1200.0)

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

    def test_joinWires_bridged_hole(self):
        """
        A hole tied to the boundary of its face by a bridge edge.

        The angle walk goes out over the bridge, around the hole and back, so
        the bridge is in the wire twice, once each way: a wire that Part.Face
        reads at the right area but cannot build a valid face of. The face
        makers drop an edge a wire travels both ways, the way the joiner drops
        one it goes over and straight back, and nest what is left as the outer
        loop and its holes. The region behind the bridge is a wire of its own,
        which the ring maker must not make a face of twice.
        """
        import math

        def seg(a, b):
            return Part.LineSegment(Vector(*a), Vector(*b)).toShape()

        def circle(c, r):
            return Part.Circle(Vector(*c), Vector(0, 0, 1), r).toShape()

        def faces(edges, maker):
            result = Part.joinWires(Part.Compound(edges), split=True, merge=True, tighten=True)
            shape = Part.makeFace(result.Wires, maker)
            self.assertTrue(shape.isValid())
            return sorted(round(f.Area, 3) for f in shape.Faces)

        rect = [seg((-20, 20, 0), (20, 20, 0)), seg((-20, 20, 0), (-20, -20, 0)),
                seg((-20, -20, 0), (20, -20, 0)), seg((20, -20, 0), (20, 20, 0))]
        disk = round(100 * math.pi, 3)
        annulus = round(1600 - 100 * math.pi, 3)

        # one bridge from the rectangle to the circle's seam vertex
        one = rect + [circle((0, 0, 0), 10), seg((10, 0, 0), (20, 0, 0))]
        self.assertEqual(faces(one, "Part::FaceMakerBullseye"), [disk, annulus])
        self.assertEqual(faces(one, "Part::FaceMakerRing"), [disk, annulus])

        # a chain: a second circle bridged from the first, from the inside
        chain = one + [circle((-6, 0, 0), 2), seg((-10, 0, 0), (-8, 0, 0))]
        small = round(4 * math.pi, 3)
        self.assertEqual(faces(chain, "Part::FaceMakerBullseye"),
                         [small, round(disk - small, 3), annulus])
        self.assertEqual(faces(chain, "Part::FaceMakerRing"),
                         [small, round(disk - small, 3), annulus])

        # two holes on two bridges
        two = rect + [circle((5, 0, 0), 3), seg((8, 0, 0), (20, 0, 0)),
                      circle((-5, 0, 0), 3), seg((-20, 0, 0), (-8, 0, 0))]
        hole = round(9 * math.pi, 3)
        self.assertEqual(faces(two, "Part::FaceMakerBullseye"),
                         [hole, hole, round(1600 - 18 * math.pi, 3)])
        self.assertEqual(faces(two, "Part::FaceMakerRing"),
                         [hole, hole, round(1600 - 18 * math.pi, 3)])

        # a hole pinched to the boundary at a vertex, bridged to another
        pinch = rect + [circle((0, 10, 0), 10), circle((0, -10, 0), 5),
                        seg((0, -5, 0), (0, 0, 0))]
        small = round(25 * math.pi, 3)
        self.assertEqual(faces(pinch, "Part::FaceMakerRing"),
                         [small, disk, round(1600 - disk - small, 3)])

        # a free circle inside the bridged hole is a hole of the disk, and,
        # for the ring maker, a face
        nested = one + [circle((0, 0, 0), 3)]
        self.assertEqual(faces(nested, "Part::FaceMakerBullseye"),
                         [round(disk - hole, 3), annulus])
        self.assertEqual(faces(nested, "Part::FaceMakerRing"),
                         [hole, round(disk - hole, 3), annulus])


    def test_joinWires_pinched_loop(self):
        """
        A loop touching the rest of the network at one vertex is a hole
        attached to the boundary of the region around it.

        A circle inside a rectangle touching its top edge: the region between
        them is one face whose boundary passes the touch point twice, and the
        disk is another. Merging used to close the circle into a finished
        chain and drop it from the graph, so the rectangle came out whole
        (1200) over the disk (314), overlapping. The angle walk now keeps such
        a loop as an edge whose two darts leave the same vertex and walks the
        annulus as one wire, which the face makers accept. The search cannot
        pass a vertex twice and still returns the two loops apart: that
        difference between angle=True and angle=False is deliberate.
        """
        import math

        def seg(a, b):
            return Part.LineSegment(Vector(*a), Vector(*b)).toShape()

        def arc(c, r, a0, a1):
            return Part.ArcOfCircle(Part.Circle(Vector(*c), Vector(0, 0, 1), r), a0, a1).toShape()

        def areas(edges, **kw):
            result = Part.joinWires(Part.Compound(edges), split=True, merge=True, tighten=True,
                                    **kw)
            return sorted(round(Part.Face(w).Area, 3) for w in result.Wires), result

        rect = [seg((-20, 10, 0), (20, 10, 0)), seg((-20, 10, 0), (-20, -20, 0)),
                seg((-20, -20, 0), (20, -20, 0)), seg((20, -20, 0), (20, 10, 0))]
        circle = [arc((0, 0, 0), 10, -math.pi / 2, math.pi / 2),
                  arc((0, 0, 0), 10, math.pi / 2, 3 * math.pi / 2)]
        disk = round(100 * math.pi, 3)
        got, result = areas(rect + circle)
        self.assertEqual(got, [disk, round(1200 - 100 * math.pi, 3)])
        for w in result.Wires:
            self.assertTrue(w.isClosed())
        faces = Part.makeFace(result.Wires, "Part::FaceMakerBullseye")
        self.assertTrue(faces.isValid())
        self.assertEqual(sorted(round(f.Area, 3) for f in faces.Faces),
                         [disk, round(1200 - 100 * math.pi, 3)])
        # the outline is the rectangle alone
        outline = Part.joinWires(Part.Compound(rect + circle), split=True, merge=True,
                                 tighten=False, outline=True)
        self.assertEqual([round(Part.Face(w).Area, 3) for w in outline.Wires], [1200.0])
        # the search keeps the two loops apart
        got, _ = areas(rect + circle, angle=False)
        self.assertEqual(got, [disk, 1200.0])

        # touching top and bottom: the disk and the two side regions
        rect2 = [seg((-20, 10, 0), (20, 10, 0)), seg((-20, 10, 0), (-20, -10, 0)),
                 seg((-20, -10, 0), (20, -10, 0)), seg((20, -10, 0), (20, 10, 0))]
        got, result = areas(rect2 + circle)
        side = round((800 - 100 * math.pi) / 2, 3)
        self.assertEqual(got, [side, side, disk])
        faces = Part.makeFace(result.Wires, "Part::FaceMakerBullseye")
        self.assertTrue(faces.isValid())
        self.assertEqual(sorted(round(f.Area, 3) for f in faces.Faces), [side, side, disk])

        # a full circle whose seam vertex is the touch point, on a side
        full = [Part.Circle(Vector(0, 0, 0), Vector(0, 0, 1), 10).toShape(),
                seg((10, -20, 0), (10, 20, 0)), seg((10, 20, 0), (-30, 20, 0)),
                seg((-30, 20, 0), (-30, -20, 0)), seg((-30, -20, 0), (10, -20, 0))]
        got, _ = areas(full)
        self.assertEqual(got, [disk, round(1600 - 100 * math.pi, 3)])

        # two circles tangent from the inside: the same tangent at the touch
        # point, and the tie is broken by curvature, the small one bending
        # harder -- a chord a fraction of the way along cannot tell two arcs
        # of the same angular span apart
        small = [arc((5, 0, 0), 5, 0, math.pi), arc((5, 0, 0), 5, math.pi, 2 * math.pi)]
        big = [arc((0, 0, 0), 10, 0, math.pi), arc((0, 0, 0), 10, math.pi, 2 * math.pi)]
        got, _ = areas(big + small)
        self.assertEqual(got, [round(25 * math.pi, 3), round(75 * math.pi, 3)])

        # equal curvature too: a parabola and its osculating circle, decided
        # by the chord
        par = [(x / 10.0, (x / 10.0) ** 2, 0) for x in range(-20, 21)]

        def bspline(pts):
            b = Part.BSplineCurve()
            b.interpolate([Vector(*p) for p in pts])
            return b.toShape()

        edges = [bspline(par[:21]), bspline(par[20:]), seg((2, 4, 0), (-2, 4, 0)),
                 arc((0, 0.5, 0), 0.5, -math.pi / 2, math.pi / 2),
                 arc((0, 0.5, 0), 0.5, math.pi / 2, 3 * math.pi / 2)]
        got, _ = areas(edges)
        self.assertEqual(got, [round(math.pi * 0.25, 3), round(32.0 / 3 - math.pi * 0.25, 3)])

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

    def test_thickness_by_arc_is_the_same_every_run(self):
        """Thickness with intersection and the Arc join put the offsets in hash
        order -- a shape's hash is its TShape's address -- and the result
        depended on that order: the same input gave up to four different
        results in eight runs (docs/TransactionLog.md sec 27.88)."""
        cases = {
            "boss": (lambda: Part.makeCylinder(6, 4).fuse(
                Part.makeCylinder(3, 4, Vector(0, 0, 4))), 2),
            "lbox": (lambda: Part.makeBox(10, 10, 5).cut(
                Part.makeBox(5, 5, 5, Vector(5, 5, 0))), 6),
        }
        for name, (make, face) in cases.items():
            outcomes = set()
            for _ in range(8):
                shape = make()  # new TShapes, new addresses, every run
                try:
                    result = shape.makeThickness([shape.Faces[face]], 1.0, 1e-3, True, False, 0, 0)
                    outcomes.add((result.isValid(), round(result.Volume, 6)))
                except Exception as e:
                    outcomes.add(type(e).__name__)
            self.assertEqual(len(outcomes), 1, "%s: %s" % (name, outcomes))
        # And the order it settles on gives the lbox its solid.
        valid, volume = outcomes.pop()
        self.assertTrue(valid)
        self.assertAlmostEqual(volume, 399.8746, places=3)

    def test_thickness_of_seam_faces_and_an_ellipse(self):
        """The fork's loop builder broke thickness on faces with a seam: a
        closed edge found twice, one seam wire allowed per face, a closed edge
        losing its orientation; and the offset of an ellipse, a closed B-spline,
        was stretched past its ends. Upstream OCCT gives these volumes
        (docs/TransactionLog.md sec 27.89)."""
        hole = lambda: Part.makeCylinder(5, 10).cut(Part.makeCylinder(2, 10))
        ellipse = lambda: Part.Face(
            Part.Wire(Part.Ellipse(Vector(0, 0, 0), 10, 5).toShape())
        ).extrude(Vector(0, 0, 8))
        pocket = lambda: Part.makeCylinder(6, 6).cut(Part.makeCylinder(3, 3, Vector(0, 0, 3)))
        cases = [
            ("cylinder top, in", lambda: Part.makeCylinder(4, 20), 1, -1.0, 468.0973),
            ("hole wall, out", hole, 3, 1.0, 531.0589),
            ("outer wall, in", hole, 0, -1.0, 257.6106),
            ("ellipse top, out", ellipse, 2, 1.0, 608.6631),
            ("ellipse bottom, in", ellipse, 1, -1.0, 473.2070),
            ("pocketed cylinder bottom, in", pocket, 2, -1.0, 346.7660),
        ]
        for name, make, face, offset, volume in cases:
            shape = make()
            result = shape.makeThickness([shape.Faces[face]], offset, 1e-7, False, False, 0, 0)
            self.assertTrue(result.isValid(), name)
            self.assertEqual(len(result.Shells), 1, name)
            self.assertAlmostEqual(result.Volume, volume, places=3, msg=name)

    def test_thickness_past_a_concave_corner(self):
        """Outward with the Arc join past a concave corner, the loop kept the
        corner of an arc face cut off by the next one as a face of its own;
        with a concave face removed, a stretched edge of the removed face gave
        a plane one of its lines twice (docs/TransactionLog.md sec 27.90).
        The first two are upstream OCCT's volumes, the third the fork's own:
        upstream fails it."""
        lbox = lambda: Part.makeBox(10, 10, 5).cut(Part.makeBox(5, 5, 5, Vector(5, 5, 0)))
        tshape = lambda: (
            Part.makeBox(12, 4, 4).fuse(Part.makeBox(4, 4, 10, Vector(4, 0, 0))).removeSplitter()
        )
        cases = [
            ("L-box arm end", lbox, 3, 388.5671),
            ("T arm end", tshape, 0, 372.9204),
            ("T bar top", tshape, 1, 382.4425),
        ]
        for name, make, face, volume in cases:
            shape = make()
            result = shape.makeThickness([shape.Faces[face]], 1.0, 1e-7, False, False, 0, 0)
            self.assertTrue(result.isValid(), name)
            self.assertEqual(len(result.Shells), 1, name)
            self.assertAlmostEqual(result.Volume, volume, places=3, msg=name)

    def test_thickness_inward_and_in_intersection_mode(self):
        """Three fork breaks (docs/TransactionLog.md sec 27.91): the T's
        concave bar top inward -- where the stretched removed face crossed an
        edge was taken for that edge's own end; the L-box's top and the T's
        back with intersection on and the Intersection join -- an edge two
        faces share was trimmed twice, the guard an indexed map's Add() that
        is never 0; and a holed cone's bottom inward with intersection on -- a
        circle running round the cylinder beyond the seam's span came out as
        a face with no area. Each volume is worked out by hand, and upstream
        OCCT gives all but the first (it fails that one)."""
        lbox = lambda: Part.makeBox(10, 10, 5).cut(Part.makeBox(5, 5, 5, Vector(5, 5, 0)))
        tshape = lambda: (
            Part.makeBox(12, 4, 4).fuse(Part.makeBox(4, 4, 10, Vector(4, 0, 0))).removeSplitter()
        )
        conehole = lambda: Part.makeCone(6, 3, 8).cut(Part.makeCylinder(1.5, 8))
        cases = [
            ("T bar top, in", tshape, 1, -1.0, False, 0, 215.5708),
            ("L-box top, out, intersection join", lbox, 2, 1.0, True, 2, 339.0),
            ("L-box top, in, intersection join", lbox, 2, -1.0, True, 2, 219.0),
            ("T back, out, intersection join", tshape, 7, 1.0, True, 2, 312.0),
            ("holed cone bottom, in, intersection", conehole, 2, -1.0, True, 0, 307.1946),
        ]
        for name, make, face, offset, inter, join, volume in cases:
            shape = make()
            result = shape.makeThickness([shape.Faces[face]], offset, 1e-7, inter, False, 0, join)
            self.assertTrue(result.isValid(), name)
            self.assertEqual(len(result.Shells), 1, name)
            self.assertAlmostEqual(result.Volume, volume, places=3, msg=name)

    def test_thickness_with_the_intersection_join(self):
        """The Intersection join (docs/TransactionLog.md sec 27.92): a thick solid
        came back inside out, oriented by the order its shells were glued in; a
        blind hole's floor, met at a concave edge, got its section on the hole's
        offset the wrong way round; and with intersection on, a removed face at
        a concave edge got a rim the splits had not cut. Each volume is worked
        out by hand or is intersection-off's; upstream OCCT fails them all."""
        tshape = lambda: (
            Part.makeBox(12, 4, 4).fuse(Part.makeBox(4, 4, 10, Vector(4, 0, 0))).removeSplitter()
        )
        pocket = lambda: Part.makeBox(10, 10, 6).cut(Part.makeBox(5, 5, 3, Vector(2.5, 2.5, 3)))
        blindhole = lambda: Part.makeBox(10, 10, 5).cut(Part.makeCylinder(2, 3, Vector(5, 5, 2)))
        lbox = lambda: Part.makeBox(10, 10, 5).cut(Part.makeBox(5, 5, 5, Vector(5, 5, 0)))
        cases = [
            ("T right bar top, in", tshape, 6, -1.0, False, 216.0),
            ("pocket floor, in", pocket, 10, -1.0, False, 367.0),
            ("blind hole floor, out", blindhole, 7, 1.0, False, 533.1327),
            ("blind hole floor, in", blindhole, 7, -1.0, False, 326.8496),
            ("L-box notch wall, out, intersection", lbox, 6, 1.0, True, 423.0),
            ("T post wall, in, intersection", tshape, 2, -1.0, True, 212.0),
        ]
        for name, make, face, offset, inter, volume in cases:
            shape = make()
            result = shape.makeThickness([shape.Faces[face]], offset, 1e-7, inter, False, 0, 2)
            self.assertTrue(result.isValid(), name)
            self.assertEqual(len(result.Shells), 1, name)
            self.assertAlmostEqual(result.Volume, volume, places=3, msg=name)

    def test_thickness_with_a_face_tangent_to_its_neighbours(self):
        """A filleted box with an end or side face removed: the removed face is
        tangent to the fillets, whose offsets never meet it, and the offset
        came back unhollowed or failed (upstream OCCT too). A tube round the
        tangent edge closes it, and outward an eighth of a sphere closes each
        corner (docs/TransactionLog.md sec 27.93). Worked out by hand."""
        box = Part.makeBox(10, 8, 6)
        cases = [
            ("end face, in", 0, -1.0, 261.1150),
            ("end face, out", 0, 1.0, 403.9604),
            ("side face, in", 5, -1.0, 253.1150),
            ("side face, out", 5, 1.0, 388.8188),
        ]
        for name, face, offset, volume in cases:
            shape = box.makeFillet(2, [box.Edges[i] for i in (0, 2, 4, 6)])
            result = shape.makeThickness([shape.Faces[face]], offset, 1e-7, False, False, 0, 0)
            self.assertTrue(result.isValid(), name)
            self.assertEqual(len(result.Shells), 1, name)
            self.assertAlmostEqual(result.Volume, volume, places=3, msg=name)

    def test_thickness_with_a_curved_face_tangent_to_its_neighbours(self):
        """A filleted box with a fillet removed: the removed face is curved and
        tangent to the planes beside it. Outward the offset failed, inward it
        came back valid with more volume than the input (upstream OCCT fails
        too). The tubes and corners are built on the fillet's cylinder, the
        loop on it walks the angles as on a plane, and a piece of an offset
        face left hanging on the shell is dropped (docs/TransactionLog.md sec
        27.95). Worked out by hand."""
        box = Part.makeBox(10, 8, 6)
        cases = [
            ("fillet, in", -1.0, 267.0193),
            ("fillet, out", 1.0, 405.9034),
        ]
        for name, offset, volume in cases:
            shape = box.makeFillet(2, [box.Edges[i] for i in (0, 2, 4, 6)])
            result = shape.makeThickness([shape.Faces[2]], offset, 1e-7, False, False, 0, 0)
            self.assertTrue(result.isValid(), name)
            self.assertEqual(len(result.Shells), 1, name)
            self.assertAlmostEqual(result.Volume, volume, places=3, msg=name)

    def test_thickness_intersection_join_with_a_face_tangent_to_its_neighbours(self):
        """A filleted box with an end, side or fillet face removed, with the
        Intersection join, which builds no tubes: the neighbour's offset ran
        round its cylinder into a lip or never met the removed face, and the
        result came back wrong, unhollowed or not at all. The gap is closed with
        the tube's sharp counterpart, a strip of the neighbour's tangent plane
        and a wall square to the removed face (docs/TransactionLog.md sec
        27.96). Worked out by hand; the fillet of 2.5 has no coincident walls."""
        box = Part.makeBox(10, 8, 6)
        cases = [
            ("end face, in", 2, 0, -1.0, 262.8319),
            ("end face, out", 2, 0, 1.0, 422.7964),
            ("side face, in", 2, 5, -1.0, 254.8319),
            ("side face, out", 2, 5, 1.0, 406.7964),
            ("fillet, in", 2, 2, -1.0, 268.7129),
            ("fillet, out", 2, 2, 1.0, 424.7690),
            ("fillet 2.5, in", 2.5, 2, -1.0, 258.4221),
            ("fillet 2.5, out", 2.5, 2, 1.0, 407.4611),
        ]
        for name, radius, face, offset, volume in cases:
            shape = box.makeFillet(radius, [box.Edges[i] for i in (0, 2, 4, 6)])
            result = shape.makeThickness([shape.Faces[face]], offset, 1e-7, False, False, 0, 2)
            self.assertTrue(result.isValid(), name)
            self.assertEqual(len(result.Shells), 1, name)
            self.assertAlmostEqual(result.Volume, volume, places=3, msg=name)

    def test_thickness_sealed_below_a_removed_face(self):
        """A cone with a through hole, its top removed, inward: the wall is 1.4
        thick at the top, the cone's and the hole's inner offsets cross at
        z=6.485, and no cavity reaches the removed face. The material is every
        point within the thickness of a face that stays, so the cavity is
        closed and the top stays as skin over it (docs/TransactionLog.md sec
        27.100): two shells, in every mode. With its bottom removed and
        intersection off the cavity ran on past the crossing, 0.761 too much.
        Worked out by hand."""
        cone = Part.makeCone(6, 3, 8).cut(Part.makeCylinder(1.5, 8))
        for inter in (False, True):
            for join in (0, 2):
                name = "top, inter=%s, join=%d" % (inter, join)
                result = cone.makeThickness([cone.Faces[1]], -1.0, 1e-7, inter, False, 0, join)
                self.assertTrue(result.isValid(), name)
                volumes = sorted(abs(Part.Solid(s).Volume) for s in result.Shells)
                self.assertEqual(len(volumes), 2, name)
                self.assertAlmostEqual(volumes[0], 112.9243, places=3, msg=name)
                self.assertAlmostEqual(volumes[1], 471.2389, places=3, msg=name)
                self.assertAlmostEqual(result.Volume, 358.3146, places=3, msg=name)
        for join in (0, 2):
            name = "bottom, join=%d" % join
            result = cone.makeThickness([cone.Faces[2]], -1.0, 1e-7, False, False, 0, join)
            self.assertTrue(result.isValid(), name)
            self.assertEqual(len(result.Shells), 1, name)
            self.assertAlmostEqual(result.Volume, 307.1946, places=3, msg=name)

    def test_OptimalBox(self):
        box = Part.makeBox(1, 1, 1)
        self.assertTrue(box.optimalBoundingBox(True, False).isValid())
