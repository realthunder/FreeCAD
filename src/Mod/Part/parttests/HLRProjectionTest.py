# -*- coding: utf-8 -*-
# Tests for the hidden line projection with element history: Shape.makeHLR,
# the Python face of Part::HLRProjector / TopoShape::makEHLR.
#
# HLRBRep_HLRToShape returns bare compounds. The projector replays its
# traversal keeping the source of every edge, so each projected edge is named
# after the element of the input it came from: an edge of the input, or the
# face a silhouette belongs to, with a fragment index when hiding cuts one
# source into several pieces.

import unittest

import FreeCAD as App
import Part
from FreeCAD import Vector


def mappedNames(shape):
    return [shape.getElementName("Edge%d" % (i + 1), 1) for i in range(len(shape.Edges))]


class HLRProjectionTest(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("HLRProjectionTest")

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def testUnnamedInputProjectsWithoutNames(self):
        # a plain shape has no element map, so the result has none either,
        # and is what HLRToShape returns
        box = Part.makeBox(10, 10, 10)
        hlr = box.makeHLR(Vector(1, 1, 1))
        self.assertEqual(hlr.ElementMapSize, 0)
        self.assertEqual(len(hlr.Edges), 9)

    def testAFaceSeenEdgeOnKeepsItsEdges(self):
        # a box seen along Z: its side faces are seen edge on, and the
        # outliner lists their boundary edges as their outline; those are
        # the box's own edges and are named as such, not after the face
        box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        for hidden in (False, True):
            hlr = box.Shape.makeHLR(Vector(0, 0, 1), Visible=not hidden, Hidden=hidden)
            self.assertEqual(len(hlr.Edges), 4)
            names = mappedNames(hlr)
            self.assertEqual(len(set(names)), 4)
            for name in names:
                self.assertTrue(name.startswith("Edge"), name)

    def testVisibleAndHiddenSelection(self):
        box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        shape = box.Shape
        visible = shape.makeHLR(Vector(1, 1, 1))
        hidden = shape.makeHLR(Vector(1, 1, 1), Visible=False, Hidden=True)
        both = shape.makeHLR(Vector(1, 1, 1), Hidden=True)
        # a box seen along its diagonal: nine visible edges, three hidden
        self.assertEqual(len(visible.Edges), 9)
        self.assertEqual(len(hidden.Edges), 3)
        self.assertEqual(len(both.Edges), 12)
        # every edge named, every name distinct, all from the box's edges
        for hlr in (visible, hidden, both):
            names = mappedNames(hlr)
            self.assertTrue(all(names), names)
            self.assertEqual(len(set(names)), len(names))
            for name in names:
                self.assertIn(";HLR", name)
        # the visible and the hidden sets partition the whole
        self.assertEqual(set(mappedNames(visible)) | set(mappedNames(hidden)), set(mappedNames(both)))
        # no Rg1/RgN edges on a box: the sharp edges are all there is
        hard = shape.makeHLR(Vector(1, 1, 1), EdgeTypes="Hard")
        self.assertEqual(mappedNames(hard), mappedNames(visible))
        smooth = shape.makeHLR(Vector(1, 1, 1), EdgeTypes=["Smooth", "Seam"])
        self.assertEqual(len(smooth.Edges), 0)

    def testSilhouetteIsNamedFromItsFace(self):
        cyl = self.doc.addObject("Part::Cylinder", "Cyl")
        self.doc.recompute()
        shape = cyl.Shape
        # seen from the side: two silhouette lines (the length of the
        # cylinder) and the two rims seen edge on, from the circles. One
        # silhouette is invented by the algorithm and named from the side
        # face; the other is where the seam (Edge1, at angle 0) lies, and
        # that projected edge is the seam's own, an input edge
        hlr = shape.makeHLR(Vector(0, 1, 0), EdgeTypes=["Hard", "Outline"])
        # a primitive's own names are its element names, so the projection's
        # names start with the source element, then the generated postfix
        silhouettes = []
        for i, edge in enumerate(hlr.Edges):
            name = hlr.getElementName("Edge%d" % (i + 1), 1)
            self.assertTrue(name, "Edge%d unnamed" % (i + 1))
            if abs(edge.Length - cyl.Height.Value) < 1e-6 and isinstance(edge.Curve, Part.Line):
                silhouettes.append(name.split(";")[0])
        self.assertEqual(sorted(silhouettes), ["Edge1", "Face1"])

    def testHiddenPiecesOfOneEdgeAreToldApart(self):
        # a post in front of a wide box, seen along Y: the box's top front
        # edge is hidden where the post covers it, so its projection is two
        # visible pieces of one source edge, named from it with a fragment
        # index, and one hidden piece
        box = self.doc.addObject("Part::Box", "Box")
        box.Length = 30
        box.Width = 10
        box.Height = 10
        post = self.doc.addObject("Part::Box", "Post")
        post.Length = 4
        post.Width = 4
        post.Height = 20
        post.Placement.Base = Vector(13, -10, 0)
        comp = self.doc.addObject("Part::Compound", "Compound")
        comp.Links = [box, post]
        self.doc.recompute()
        both = comp.Shape.makeHLR(Vector(0, 1, 0), Hidden=True)
        names = mappedNames(both)
        self.assertEqual(len(set(names)), len(names))
        # the pieces of one source share its name and differ by fragment
        stems = {}
        for name in names:
            stems.setdefault(name.split(";:G")[0], []).append(name)
        split = [group for group in stems.values() if len(group) > 1]
        self.assertTrue(split, "no source was cut into pieces: %s" % names)

    def testOnShapeGivesEdgesOnTheShape(self):
        box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        flat = box.Shape.makeHLR(Vector(1, 1, 1))
        on = box.Shape.makeHLR(Vector(1, 1, 1), OnShape=True)
        self.assertEqual(len(flat.Edges), len(on.Edges))
        self.assertEqual(mappedNames(flat), mappedNames(on))
        # the flat result lies in a plane, the 3D one spans the box
        self.assertLess(flat.BoundBox.ZLength, 1e-6)
        self.assertGreater(on.BoundBox.ZLength, 9.0)
