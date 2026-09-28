# SPDX-License-Identifier: LGPL-2.1-or-later

import unittest

import FreeCAD
import Part


class TestPointPattern(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("PartDesignTestPointPattern")

    def tearDown(self):
        FreeCAD.closeDocument(self.doc.Name)

    def testPointPatternCreatesCopiesAtPointCoordinates(self):
        body = self.doc.addObject("PartDesign::Body", "Body")
        points = body.newObject("PartDesign::Feature", "Points")
        points.Shape = Part.makeCompound(
            [
                Part.Vertex(FreeCAD.Vector(5, 5, 5)),
                Part.Vertex(FreeCAD.Vector(10, 5, 5)),
                Part.Vertex(FreeCAD.Vector(5, 10, 5)),
            ]
        )

        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = 2
        box.Width = 2
        box.Height = 2
        self.doc.recompute()

        pattern = body.newObject("PartDesign::PointPattern", "PointPattern")
        pattern.Originals = [box]
        pattern.PointObject = points
        self.doc.recompute()

        self.assertEqual(pattern.getStatusString(), "Valid")
        self.assertFalse(pattern.Shape.isNull())
        self.assertAlmostEqual(pattern.Shape.Volume, 3 * box.Shape.Volume)
        self.assertAlmostEqual(pattern.Shape.BoundBox.XMin, 5)
        self.assertAlmostEqual(pattern.Shape.BoundBox.YMin, 5)
        self.assertAlmostEqual(pattern.Shape.BoundBox.ZMin, 5)
        self.assertAlmostEqual(pattern.Shape.BoundBox.XMax, 12)
        self.assertAlmostEqual(pattern.Shape.BoundBox.YMax, 12)
        self.assertAlmostEqual(pattern.Shape.BoundBox.ZMax, 7)

    def testPointPatternPreservesSourceOrientation(self):
        body = self.doc.addObject("PartDesign::Body", "Body")
        points = body.newObject("PartDesign::Feature", "Points")
        points.Shape = Part.makeCompound(
            [Part.Vertex(FreeCAD.Vector(5, 5, 5)), Part.Vertex(FreeCAD.Vector(10, 5, 5))]
        )
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = 2
        box.Width = 1
        box.Height = 1
        box.Placement = FreeCAD.Placement(
            FreeCAD.Vector(20, 30, 40), FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 90)
        )
        self.doc.recompute()
        pattern = body.newObject("PartDesign::PointPattern", "PointPattern")
        pattern.Originals = [box]
        pattern.PointObject = points
        self.doc.recompute()

        self.assertEqual(pattern.getStatusString(), "Valid")
        self.assertAlmostEqual(pattern.Shape.Volume, 2 * box.Shape.Volume)
        bounds = pattern.Shape.BoundBox
        for actual, expected in zip(
            (bounds.XMin, bounds.YMin, bounds.ZMin, bounds.XMax, bounds.YMax, bounds.ZMax),
            (4, 5, 5, 10, 7, 6),
        ):
            self.assertAlmostEqual(actual, expected)

    def _plateBoxAndPoints(self):
        body = self.doc.addObject("PartDesign::Body", "Body")
        points = body.newObject("PartDesign::Feature", "Points")
        points.Shape = Part.makeCompound(
            [Part.Vertex(FreeCAD.Vector(5, 5, 0)), Part.Vertex(FreeCAD.Vector(20, 5, 0))]
        )
        plate = body.newObject("PartDesign::AdditiveBox", "Plate")
        plate.Length = 4
        plate.Width = 4
        plate.Height = 1
        plate.Placement = FreeCAD.Placement(FreeCAD.Vector(-50, -50, 0), FreeCAD.Rotation())
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = 2
        box.Width = 1
        box.Height = 1
        box.Placement = FreeCAD.Placement(FreeCAD.Vector(3, 4, 0), FreeCAD.Rotation())
        self.doc.recompute()
        pattern = body.newObject("PartDesign::PointPattern", "PointPattern")
        pattern.Originals = [box]
        pattern.PointObject = points
        return pattern

    @staticmethod
    def _bounds(shape):
        return sorted(
            tuple(round(v, 6) for v in (b.XMin, b.YMin, b.XMax, b.YMax))
            for b in (s.BoundBox for s in shape.Solids)
        )

    def testSubTransformKeepsTheBase(self):
        # The fork's: transforming features, only the original moves, from
        # its own origin to each point; the base stays and the original is
        # not left in place
        pattern = self._plateBoxAndPoints()
        self.doc.recompute()
        self.assertEqual(pattern.getStatusString(), "Valid")
        self.assertEqual(
            self._bounds(pattern.Shape),
            [(-50, -50, -46, -46), (5, 5, 7, 6), (20, 5, 22, 6)],
        )

    def testSubTransformHidingTheBaseFeature(self):
        # HideBaseFeature must not drop the copy on the first point
        pattern = self._plateBoxAndPoints()
        pattern.HideBaseFeature = True
        self.doc.recompute()
        self.assertEqual(pattern.getStatusString(), "Valid")
        self.assertEqual(self._bounds(pattern.Shape), [(5, 5, 7, 6), (20, 5, 22, 6)])

    def testWholeShapesMoveAsUpstream(self):
        # Transforming whole shapes, the support moves so that the base
        # feature's origin lands on the first point, as upstream's does
        pattern = self._plateBoxAndPoints()
        pattern.SubTransform = False
        self.doc.recompute()
        self.assertEqual(pattern.getStatusString(), "Valid")
        self.assertEqual(
            self._bounds(pattern.Shape),
            [(-48, -49, -44, -45), (-33, -49, -29, -45), (5, 5, 7, 6), (20, 5, 22, 6)],
        )
