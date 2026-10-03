# SPDX-License-Identifier: LGPL-2.1-or-later

"""SketchObject.setDiameter: a radius constraint said as a diameter, and back.

The constraint changes kind in place -- its index, its name and whether it
drives stay -- and the circle keeps its size: the value is doubled or halved
with the kind. A value an expression gives is left to the expression, which
then gives the other measure.
"""

import unittest

import FreeCAD
import Part
import Sketcher

App = FreeCAD


class TestSketchRadiusDiameter(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("TestSketchRadiusDiameter")
        self.sketch = self.Doc.addObject("Sketcher::SketchObject", "Sketch")
        self.sketch.addGeometry(
            Part.Circle(App.Vector(0, 0, 0), App.Vector(0, 0, 1), 5), False
        )
        self.sketch.addGeometry(
            Part.LineSegment(App.Vector(20, 0, 0), App.Vector(40, 0, 0)), False
        )
        self.radius = self.sketch.addConstraint(Sketcher.Constraint("Radius", 0, 5.0))
        self.distance = self.sketch.addConstraint(Sketcher.Constraint("Distance", 1, 20.0))
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def constraint(self):
        return self.sketch.Constraints[self.radius]

    def circleRadius(self):
        return self.sketch.Geometry[0].Radius

    def testRadiusToDiameterKeepsTheCircle(self):
        self.sketch.renameConstraint(self.radius, "Hole")
        self.sketch.setDiameter(self.radius, True)
        self.Doc.recompute()
        self.assertEqual(self.constraint().Type, "Diameter")
        self.assertAlmostEqual(self.constraint().Value, 10.0)
        self.assertEqual(self.constraint().Name, "Hole")
        self.assertTrue(self.constraint().Driving)
        self.assertAlmostEqual(self.circleRadius(), 5.0)
        self.assertEqual(len(self.sketch.Constraints), 2)

    def testAndBack(self):
        self.sketch.setDiameter(self.radius, True)
        self.sketch.setDiameter(self.radius, False)
        self.Doc.recompute()
        self.assertEqual(self.constraint().Type, "Radius")
        self.assertAlmostEqual(self.constraint().Value, 5.0)
        self.assertAlmostEqual(self.circleRadius(), 5.0)

    def testTheKindItAlreadyIsChangesNothing(self):
        self.sketch.setDiameter(self.radius, False)
        self.assertEqual(self.constraint().Type, "Radius")
        self.assertAlmostEqual(self.constraint().Value, 5.0)

    def testAValueOfTheNewKindDrivesTheCircle(self):
        self.sketch.setDiameter(self.radius, True)
        self.sketch.setDatum(self.radius, App.Units.Quantity("8 mm"))
        self.Doc.recompute()
        self.assertAlmostEqual(self.circleRadius(), 4.0)

    def testAnExpressionGivesTheNewMeasure(self):
        self.sketch.setExpression("Constraints[%d]" % self.radius, "2 mm + 3 mm")
        self.Doc.recompute()
        self.assertAlmostEqual(self.circleRadius(), 5.0)
        self.sketch.setDiameter(self.radius, True)
        self.Doc.recompute()
        self.assertEqual(self.constraint().Type, "Diameter")
        # the expression still says 5 mm, and that is the diameter now
        self.assertAlmostEqual(self.constraint().Value, 5.0)
        self.assertAlmostEqual(self.circleRadius(), 2.5)

    def testAReferenceStaysAReference(self):
        self.sketch.setDriving(self.radius, False)
        self.sketch.setDiameter(self.radius, True)
        self.Doc.recompute()
        self.assertEqual(self.constraint().Type, "Diameter")
        self.assertFalse(self.constraint().Driving)
        self.assertAlmostEqual(self.constraint().Value, 10.0)
        self.assertAlmostEqual(self.circleRadius(), 5.0)

    def testOnlyARadiusOrADiameter(self):
        with self.assertRaises(ValueError):
            self.sketch.setDiameter(self.distance, True)
        with self.assertRaises(ValueError):
            self.sketch.setDiameter(99, True)
        self.assertEqual(self.sketch.Constraints[self.distance].Type, "Distance")
