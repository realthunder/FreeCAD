#***************************************************************************
#*   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU Lesser General Public License (LGPL)    *
#*   as published by the Free Software Foundation; either version 2 of     *
#*   the License, or (at your option) any later version.                   *
#*   for detail see the LICENCE text file.                                 *
#*                                                                         *
#*   This program is distributed in the hope that it will be useful,       *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU Library General Public License for more details.                  *
#*                                                                         *
#*   You should have received a copy of the GNU Library General Public     *
#*   License along with this program; if not, write to the Free Software   *
#*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
#*   USA                                                                   *
#*                                                                         *
#***************************************************************************

import math
import unittest

import FreeCAD
from FreeCAD import Base
from FreeCAD import Units
import Part
import Sketcher
import TestSketcherApp

class TestLoft(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestLoft")

    def testSimpleAdditiveLoftCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.ProfileSketch = self.Doc.addObject('Sketcher::SketchObject', 'ProfileSketch')
        self.Body.addObject(self.ProfileSketch)
        TestSketcherApp.CreateRectangleSketch(self.ProfileSketch, (0, 0), (1, 1))
        self.Doc.recompute()
        self.LoftSketch = self.Doc.addObject('Sketcher::SketchObject', 'LoftSketch')
        self.Body.addObject(self.LoftSketch)
        self.LoftSketch.MapMode = 'FlatFace'
        self.LoftSketch.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.LoftSketch, (0, 1), (1, 1))
        self.Doc.recompute()
        self.AdditiveLoft = self.Doc.addObject("PartDesign::AdditiveLoft","AdditiveLoft")
        self.Body.addObject(self.AdditiveLoft)
        self.AdditiveLoft.Profile = self.ProfileSketch
        self.AdditiveLoft.Sections = [self.LoftSketch]
        self.Doc.recompute()
        self.assertAlmostEqual(self.AdditiveLoft.Shape.Volume, 1)

    @staticmethod
    def _addRectangle(sketch, xMin, yMin, xMax, yMax):
        sketch.addGeometry(
            [
                Part.LineSegment(Base.Vector(xMin, yMax, 0), Base.Vector(xMax, yMax, 0)),
                Part.LineSegment(Base.Vector(xMax, yMax, 0), Base.Vector(xMax, yMin, 0)),
                Part.LineSegment(Base.Vector(xMax, yMin, 0), Base.Vector(xMin, yMin, 0)),
                Part.LineSegment(Base.Vector(xMin, yMin, 0), Base.Vector(xMin, yMax, 0)),
            ],
            False,
        )

    @staticmethod
    def _addCapsule(sketch, radius, centerDistance):
        sketch.addGeometry(
            [
                Part.ArcOfCircle(
                    Part.Circle(Base.Vector(0, 0, 0), Base.Vector(0, 0, 1), radius),
                    math.pi / 2,
                    3 * math.pi / 2,
                ),
                Part.ArcOfCircle(
                    Part.Circle(Base.Vector(centerDistance, 0, 0), Base.Vector(0, 0, 1), radius),
                    -math.pi / 2,
                    math.pi / 2,
                ),
                Part.LineSegment(Base.Vector(0, radius, 0), Base.Vector(centerDistance, radius, 0)),
                Part.LineSegment(
                    Base.Vector(0, -radius, 0), Base.Vector(centerDistance, -radius, 0)
                ),
            ],
            False,
        )

    def _makeIssue6130RectangleLoft(self, name, reverseTopWires):
        body = self.Doc.addObject("PartDesign::Body", f"{name}Body")
        bottom = body.newObject("Sketcher::SketchObject", f"{name}Bottom")
        self._addRectangle(bottom, -25.078129, -25.156250, 26.171875, 23.281252)
        self._addRectangle(bottom, -18.984377, -18.750000, 20.703131, 17.031252)

        top = body.newObject("Sketcher::SketchObject", f"{name}Top")
        top.Placement.Base.z = 60
        topWires = [
            (-31.484377, -30.781250, 36.171883, 29.531252),
            (-28.515621, -27.656250, 32.421879, 26.406252),
        ]
        if reverseTopWires:
            topWires.reverse()
        for wire in topWires:
            self._addRectangle(top, *wire)

        loft = body.newObject("PartDesign::AdditiveLoft", f"{name}Loft")
        loft.Profile = bottom
        loft.Sections = [top]
        return loft

    def _makeIssue6130CircleLoft(self, name, reverseTopWires):
        body = self.Doc.addObject("PartDesign::Body", f"{name}Body")
        bottom = body.newObject("Sketcher::SketchObject", f"{name}Bottom")
        bottom.addGeometry(
            Part.Circle(Base.Vector(0, 0, 0), Base.Vector(0, 0, 1), 47.132468), False
        )
        bottom.addGeometry(
            Part.Circle(Base.Vector(0, 0.385132, 0), Base.Vector(0, 0, 1), 41.109848),
            False,
        )

        top = body.newObject("Sketcher::SketchObject", f"{name}Top")
        top.Placement.Base.z = 50
        topRadii = [67.753235, 58.631832]
        if reverseTopWires:
            topRadii.reverse()
        for radius in topRadii:
            top.addGeometry(Part.Circle(Base.Vector(0, 0, 0), Base.Vector(0, 0, 1), radius), False)

        loft = body.newObject("PartDesign::AdditiveLoft", f"{name}Loft")
        loft.Profile = bottom
        loft.Sections = [top]
        return loft

    def _makeIssue6130SubtractiveCircleLoft(self, name, reverseTopWires):
        body = self.Doc.addObject("PartDesign::Body", f"{name}Body")
        padSketch = body.newObject("Sketcher::SketchObject", f"{name}PadSketch")
        self._addRectangle(padSketch, -90, -90, 90, 90)
        pad = body.newObject("PartDesign::Pad", f"{name}Pad")
        pad.Profile = padSketch
        pad.Length = 60
        self.Doc.recompute()

        bottom = body.newObject("Sketcher::SketchObject", f"{name}Bottom")
        bottom.addGeometry(
            Part.Circle(Base.Vector(0, 0, 0), Base.Vector(0, 0, 1), 47.132468), False
        )
        bottom.addGeometry(
            Part.Circle(Base.Vector(0, 0.385132, 0), Base.Vector(0, 0, 1), 41.109848),
            False,
        )

        top = body.newObject("Sketcher::SketchObject", f"{name}Top")
        top.Placement.Base.z = 50
        topRadii = [67.753235, 58.631832]
        if reverseTopWires:
            topRadii.reverse()
        for radius in topRadii:
            top.addGeometry(Part.Circle(Base.Vector(0, 0, 0), Base.Vector(0, 0, 1), radius), False)

        loft = body.newObject("PartDesign::SubtractiveLoft", f"{name}Loft")
        loft.Profile = bottom
        loft.Sections = [top]
        return loft

    def _makeCapsuleLoft(self, name, reverseTopWires, subtractive):
        body = self.Doc.addObject("PartDesign::Body", f"{name}Body")
        if subtractive:
            padSketch = body.newObject("Sketcher::SketchObject", f"{name}PadSketch")
            self._addRectangle(padSketch, -100, -100, 100, 100)
            pad = body.newObject("PartDesign::Pad", f"{name}Pad")
            pad.Profile = padSketch
            pad.Length = 30
            self.Doc.recompute()

        bottom = body.newObject("Sketcher::SketchObject", f"{name}Bottom")
        self._addCapsule(bottom, 10, 20)
        self._addCapsule(bottom, 9, 20)

        top = body.newObject("Sketcher::SketchObject", f"{name}Top")
        top.Placement.Base.z = 20
        topCapsules = [(22, 40), (20, 40)]
        if reverseTopWires:
            topCapsules.reverse()
        for radius, centerDistance in topCapsules:
            self._addCapsule(top, radius, centerDistance)

        featureType = "PartDesign::SubtractiveLoft" if subtractive else "PartDesign::AdditiveLoft"
        loft = body.newObject(featureType, f"{name}Loft")
        loft.Profile = bottom
        loft.Sections = [top]
        return loft

    def _makeNestedIslandLoft(self, name, permuteTopWires):
        body = self.Doc.addObject("PartDesign::Body", f"{name}Body")
        bottom = body.newObject("Sketcher::SketchObject", f"{name}Bottom")
        for radius in (30, 20, 8):
            bottom.addGeometry(
                Part.Circle(Base.Vector(0, 0, 0), Base.Vector(0, 0, 1), radius), False
            )

        top = body.newObject("Sketcher::SketchObject", f"{name}Top")
        top.Placement.Base.z = 40
        topRadii = (10, 36, 24) if permuteTopWires else (36, 24, 10)
        for radius in topRadii:
            top.addGeometry(Part.Circle(Base.Vector(0, 0, 0), Base.Vector(0, 0, 1), radius), False)

        loft = body.newObject("PartDesign::AdditiveLoft", f"{name}Loft")
        loft.Profile = bottom
        loft.Sections = [top]
        return loft

    def testIssue6130NestedRectanglesIgnoreCreationOrder(self):
        """Nested rectangle pairing must not depend on sketch geometry order."""
        reversedOrder = self._makeIssue6130RectangleLoft("Reversed", True)
        consistentOrder = self._makeIssue6130RectangleLoft("Consistent", False)
        self.Doc.recompute()

        reversedOrder.Shape.check(True)  # raises when the shape is broken
        self.assertAlmostEqual(reversedOrder.Shape.Volume, consistentOrder.Shape.Volume)

    def testIssue6130NestedCirclesIgnoreCreationOrder(self):
        """Nested circle pairing must not depend on sketch geometry order."""
        reversedOrder = self._makeIssue6130CircleLoft("Reversed", True)
        consistentOrder = self._makeIssue6130CircleLoft("Consistent", False)
        self.Doc.recompute()

        reversedOrder.Shape.check(True)  # raises when the shape is broken
        self.assertAlmostEqual(reversedOrder.Shape.Volume, consistentOrder.Shape.Volume)

    def testIssue6130SubtractiveLoftIgnoresCreationOrder(self):
        """Nested wire pairing must also be stable for subtractive lofts."""
        reversedOrder = self._makeIssue6130SubtractiveCircleLoft("ReversedCut", True)
        consistentOrder = self._makeIssue6130SubtractiveCircleLoft("ConsistentCut", False)
        self.Doc.recompute()

        reversedOrder.Shape.check(True)  # raises when the shape is broken
        self.assertAlmostEqual(reversedOrder.Shape.Volume, consistentOrder.Shape.Volume)

    def testCapsuleAdditiveLoftIgnoresCreationOrder(self):
        """Capsule wire pairing must not depend on sketch geometry order."""
        reversedOrder = self._makeCapsuleLoft("ReversedCapsuleAdd", True, False)
        consistentOrder = self._makeCapsuleLoft("ConsistentCapsuleAdd", False, False)
        self.Doc.recompute()

        reversedOrder.Shape.check(True)  # raises when the shape is broken
        self.assertAlmostEqual(reversedOrder.Shape.Volume, consistentOrder.Shape.Volume)

    def testCapsuleSubtractiveLoftIgnoresCreationOrder(self):
        """Capsule wire pairing must also be stable for subtractive lofts."""
        reversedOrder = self._makeCapsuleLoft("ReversedCapsuleCut", True, True)
        consistentOrder = self._makeCapsuleLoft("ConsistentCapsuleCut", False, True)
        self.Doc.recompute()

        reversedOrder.Shape.check(True)  # raises when the shape is broken
        self.assertAlmostEqual(reversedOrder.Shape.Volume, consistentOrder.Shape.Volume)

    def testNestedIslandLoftIgnoresCreationOrder(self):
        """Pair outer, hole, and island wires by nesting depth."""
        permutedOrder = self._makeNestedIslandLoft("Permuted", True)
        consistentOrder = self._makeNestedIslandLoft("Consistent", False)
        self.Doc.recompute()

        permutedOrder.Shape.check(True)  # raises when the shape is broken
        self.assertAlmostEqual(permutedOrder.Shape.Volume, consistentOrder.Shape.Volume)

    def testSimpleSubtractiveLoftCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (1, 1))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 2
        self.Doc.recompute()
        self.ProfileSketch = self.Doc.addObject('Sketcher::SketchObject', 'ProfileSketch')
        self.Body.addObject(self.ProfileSketch)
        TestSketcherApp.CreateRectangleSketch(self.ProfileSketch, (0, 0), (1, 1))
        self.Doc.recompute()
        self.LoftSketch = self.Doc.addObject('Sketcher::SketchObject', 'LoftSketch')
        self.Body.addObject(self.LoftSketch)
        self.LoftSketch.MapMode = 'FlatFace'
        self.LoftSketch.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.LoftSketch, (0, 1), (1, 1))
        self.Doc.recompute()
        self.SubtractiveLoft = self.Doc.addObject("PartDesign::SubtractiveLoft","SubtractiveLoft")
        self.Body.addObject(self.SubtractiveLoft)
        self.SubtractiveLoft.Profile = self.ProfileSketch
        self.SubtractiveLoft.Sections = [self.LoftSketch]
        self.Doc.recompute()
        self.assertAlmostEqual(self.SubtractiveLoft.Shape.Volume, 1)

    def testClosedAdditiveLoftCase(self):
        """ Test issue #6156: Loft tool "Closed" option not working """
        body = self.Doc.addObject('PartDesign::Body','Body')

        sketch1 = body.newObject('Sketcher::SketchObject','Sketch')
        sketch1.Support = (self.Doc.XZ_Plane,[''])
        sketch1.MapMode = 'FlatFace'
        sketch1.addGeometry(Part.Circle(Base.Vector(-40.0,0.0,0.0),Base.Vector(0,0,1),10.0), False)
        sketch1.addConstraint(Sketcher.Constraint('PointOnObject',0,3,-1))
        sketch1.addConstraint(Sketcher.Constraint('Diameter',0,20.0))
        sketch1.setDatum(1,Units.Quantity('20.000000 mm'))
        sketch1.addConstraint(Sketcher.Constraint('Distance',-1,1,0,3,40.0))
        sketch1.setDatum(2,Units.Quantity('40.000000 mm'))

        sketch2 = body.newObject('Sketcher::SketchObject','Sketch001')
        sketch2.Support = (self.Doc.YZ_Plane,'')
        sketch2.MapMode = 'FlatFace'
        sketch2.addGeometry(Part.Circle(Base.Vector(-10.0,0.0,0.0),Base.Vector(0,0,1),10.0),False)
        sketch2.addConstraint(Sketcher.Constraint('PointOnObject',0,3,-1))
        sketch2.addConstraint(Sketcher.Constraint('Diameter',0,20.0))
        sketch2.setDatum(1,Units.Quantity('20.000000 mm'))
        sketch2.addConstraint(Sketcher.Constraint('Distance',-1,1,0,3,40.0))
        sketch2.setDatum(2,Units.Quantity('40.000000 mm'))

        sketch3 = body.newObject('Sketcher::SketchObject','Sketch002')
        sketch3.Support = (self.Doc.getObject('YZ_Plane'),'')
        sketch3.MapMode = 'FlatFace'
        sketch3.addGeometry(Part.Circle(Base.Vector(40.0,0.0,0.0),Base.Vector(0,0,1),10.0),False)
        sketch3.addConstraint(Sketcher.Constraint('PointOnObject',0,3,-1))
        sketch3.addConstraint(Sketcher.Constraint('Distance',-1,1,0,3,40.0))
        sketch3.setDatum(1,Units.Quantity('40.000000 mm'))
        sketch3.addConstraint(Sketcher.Constraint('Diameter',0,20.0))
        sketch3.setDatum(2,Units.Quantity('20.000000 mm'))

        sketch4 = body.newObject('Sketcher::SketchObject','Sketch003')
        sketch4.Support = (self.Doc.XZ_Plane,'')
        sketch4.MapMode = 'FlatFace'
        sketch4.addGeometry(Part.Circle(Base.Vector(40.0,0.0,0.0),Base.Vector(0,0,1),10.0),False)
        sketch4.addConstraint(Sketcher.Constraint('PointOnObject',0,3,-1))
        sketch4.addConstraint(Sketcher.Constraint('Distance',-1,1,0,3,40.0))
        sketch4.setDatum(1,Units.Quantity('40.000000 mm'))
        sketch4.addConstraint(Sketcher.Constraint('Diameter',0,20.0))
        sketch4.setDatum(2,Units.Quantity('20.000000 mm'))

        self.Doc.recompute()

        loft = body.newObject('PartDesign::AdditiveLoft','AdditiveLoft')
        loft.Profile = sketch1
        loft.Sections = [sketch2, sketch4, sketch3]
        loft.Closed = True

        sketch1.Visibility = False
        sketch2.Visibility = False
        sketch3.Visibility = False
        sketch4.Visibility = False

        self.Doc.recompute()

        self.assertGreater(loft.Shape.Volume, 80000.0) # 85105.5788704151

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestLoft")
        #print ("omit closing document for debugging")

