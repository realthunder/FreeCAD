#**************************************************************************
#   Copyright (c) 2017 Kurt Kremitzki <kkremitzki@gmail.com>              *
#                                                                         *
#   This file is part of the FreeCAD CAx development system.              *
#                                                                         *
#   This program is free software; you can redistribute it and/or modify  *
#   it under the terms of the GNU Lesser General Public License (LGPL)    *
#   as published by the Free Software Foundation; either version 2 of     *
#   the License, or (at your option) any later version.                   *
#   for detail see the LICENCE text file.                                 *
#                                                                         *
#   FreeCAD is distributed in the hope that it will be useful,            *
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#   GNU Library General Public License for more details.                  *
#                                                                         *
#   You should have received a copy of the GNU Library General Public     *
#   License along with FreeCAD; if not, write to the Free Software        *
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
#   USA                                                                   *
#**************************************************************************
from math import pi
import unittest

import FreeCAD
import TestSketcherApp

App = FreeCAD

class TestHole(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestHole")
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Box.Length=10
        self.Box.Width=10
        self.Box.Height=10
        self.Body.addObject(self.Box)
        self.Doc.recompute()
        self.HoleSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchHole')
        self.HoleSketch.Support = (self.Doc.XY_Plane, [''])
        self.HoleSketch.MapMode = 'FlatFace'
        self.HoleSketch.MapReversed = True
        self.Body.addObject(self.HoleSketch)
        TestSketcherApp.CreateCircleSketch(self.HoleSketch, (-5, 5),  1)
        self.Doc.recompute()
        self.Hole = self.Doc.addObject("PartDesign::Hole", "Hole")
        self.Hole.Profile = self.HoleSketch
        self.Body.addObject(self.Hole)
        self.Doc.recompute()

    def testPlainHole(self):
        self.Hole.Diameter = 6
        self.Hole.Depth = 10
        # self.Hole.DrillPointAngle = 118.000000
        # self.Hole.TaperedAngle = 90
        self.Hole.ThreadType = 0
        self.Hole.HoleCutType = 0 # 1 = Counterbore, 2 = Countersink
        # self.Hole.HoleCutDiameter = 5
        # self.Hole.HoleCutCountersinkAngle = 90
        # self.Hole.HoleCutDepth = 2 # Counterbore
        self.Hole.DepthType = 0 # 1 = Through all
        self.Hole.DrillPoint = 0 # 1 = Angled
        self.Hole.Tapered = 0 # On/off
        self.Doc.recompute()
        self.assertAlmostEqual(self.Hole.Shape.Volume, 10**3 - pi * 3**2 * 10)

    def testTaperedHole(self):
        self.Hole.Diameter = 6
        self.Hole.Depth = 5
        self.Hole.TaperedAngle = 60
        self.Hole.ThreadType = 0
        self.Hole.HoleCutType = 0
        self.Hole.DepthType = 0
        self.Hole.DrillPoint = 0
        self.Hole.Tapered = 1
        self.Doc.recompute()
        self.assertEqual(len(self.Hole.Shape.Faces), 8)

    def testAngledDrillHole(self):
        self.Hole.Diameter = 6
        self.Hole.Depth = 10
        self.Hole.DrillPointAngle = 118
        self.Hole.ThreadType = 0
        self.Hole.HoleCutType = 0
        self.Hole.DepthType = 0
        self.Hole.DrillPoint = 1
        self.Hole.Tapered = 0
        self.Hole.DrillForDepth = 1
        self.Doc.recompute()
        self.assertEqual(len(self.Hole.Shape.Faces), 8)

    def testCounterboreHole(self):
        self.Hole.Diameter = 6
        self.Hole.Depth = 10
        self.Hole.ThreadType = 0
        self.Hole.HoleCutType = 1
        self.Hole.HoleCutDiameter = 8
        self.Hole.HoleCutDepth = 5
        self.Hole.DepthType = 0
        self.Hole.DrillPoint = 0
        self.Hole.Tapered = 0
        self.Doc.recompute()
        self.assertAlmostEqual(self.Hole.Shape.Volume, 10**3 - pi * 3**2 * 5 - pi * 4**2 * 5)

    def testCountersinkHole(self):
        self.Hole.Diameter = 6
        self.Hole.Depth = 10
        self.Hole.ThreadType = 0
        self.Hole.HoleCutType = 2
        self.Hole.HoleCutDiameter = 9
        self.Hole.HoleCutCountersinkAngle = 90
        self.Hole.DepthType = 0
        self.Hole.DrillPoint = 0
        self.Hole.Tapered = 0
        self.Doc.recompute()
        self.assertAlmostEqual(self.Hole.Shape.Volume, 10**3 - pi * 3**2 * 10 - 24.7400421)

    def _holeOnSecondBox(self, refine):
        """A plain hole through a second, unrefined box that overlaps the
        first, so that refining merges faces the hole leaves split
        (upstream fe7bff5a9a)"""
        self.Box2 = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        self.Box2.Length = 10
        self.Box2.Width = 10
        self.Box2.Height = 10
        self.Box2.AttachmentOffset = App.Placement(App.Vector(1, 0, 0), App.Rotation())
        self.Box2.MapReversed = False
        self.Box2.AttachmentSupport = self.Doc.getObject("XY_Plane")
        self.Box2.MapPathParameter = 0.0
        self.Box2.MapMode = "FlatFace"
        # Unrefined, or the second box would merge into the first
        self.Box2.Refine = False
        self.Body.addObject(self.Box2)
        self.Doc.recompute()

        # Move the hole on top of the body
        self.Body.removeObject(self.Hole)
        self.Body.insertObject(self.Hole, self.Box2, True)
        self.Body.Tip = self.Hole
        self.Hole.Diameter = 6
        self.Hole.Depth = 10
        self.Hole.ThreadType = 0
        self.Hole.HoleCutType = 0
        self.Hole.DepthType = 0
        self.Hole.DrillPoint = 0
        self.Hole.Tapered = 0
        self.Hole.Visibility = True
        self.Hole.Refine = refine
        self.Doc.recompute()

    def testNoRefineHole(self):
        self._holeOnSecondBox(False)
        self.assertEqual(len(self.Hole.Shape.Faces), 15)

    def testRefineHole(self):
        self._holeOnSecondBox(True)
        self.assertEqual(len(self.Hole.Shape.Faces), 7)

    def testProfileWithoutCircle(self):
        """Nothing to drill is an error on the hole (upstream 7a672a3207); it
        emptied the body without a word."""
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = 20
        self.Doc.recompute()
        sketch = self.Body.newObject("Sketcher::SketchObject", "Square")
        sketch.Support = (box, ["Face6"])
        sketch.MapMode = "FlatFace"
        TestSketcherApp.CreateRectangleSketch(sketch, (5, 5), (4, 4))
        self.Doc.recompute()
        hole = self.Body.newObject("PartDesign::Hole", "Hole")
        hole.Profile = sketch
        self.Doc.recompute()
        self.assertIn("Invalid", hole.State)
        self.assertAlmostEqual(box.Shape.Volume, 4000)

    def testBaseProfileType(self):
        """Holes on a sketch's points as well as its circles and arcs
        (upstream 774ec2cc93): BaseProfileType's bits choose, a new hole
        takes the preference (all three by default), and one restored
        without the property keeps circles and arcs."""
        import Part
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = 40
        box.Height = 10
        self.Doc.recompute()
        sketch = self.Body.newObject("Sketcher::SketchObject", "Points")
        sketch.MapMode = "Deactivated"
        sketch.Placement = App.Placement(App.Vector(0, 0, 10), App.Rotation())
        sketch.addGeometry(Part.Circle(App.Vector(10, 10, 0), App.Vector(0, 0, 1), 1))
        point = sketch.addGeometry(Part.Point(App.Vector(30, 30, 0)))
        # a point is added as construction; a real one is toggled
        sketch.setConstruction(point, False)
        self.Doc.recompute()
        hole = self.Body.newObject("PartDesign::Hole", "Hole")
        # as PartDesign_Hole writes it
        hole.Profile = (sketch, [""])
        hole.Diameter = 6
        hole.DepthType = "Dimension"
        hole.Depth = 5
        hole.DrillPoint = "Flat"
        self.assertEqual(hole.BaseProfileType, 7)
        one = pi * 9 * 5
        for bits, holes, at_point in ((7, 2, True), (6, 1, False), (1, 1, True)):
            hole.BaseProfileType = bits
            self.Doc.recompute()
            self.assertNotIn("Invalid", hole.State, bits)
            removed = box.Shape.cut(hole.Shape)
            self.assertAlmostEqual(removed.Volume, holes * one, places=4, msg=bits)
            near_point = [s for s in removed.Solids
                          if (s.CenterOfMass - App.Vector(30, 30, 7.5)).Length < 1e-6]
            self.assertEqual(bool(near_point), at_point, bits)

    def testProfileCylindricalFace(self):
        """A hole on the face of an existing hole is drilled along its axis
        (upstream 7481a5d8dd, af83b6883e); it went radially, the normal at
        the middle of the face, and Reversed changed nothing."""
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = box.Height = 40
        for placement in (App.Placement(App.Vector(20, 20, 0), App.Rotation()),
                          App.Placement(App.Vector(0, 10, 20),
                                        App.Rotation(App.Vector(0, 1, 0), 90))):
            cyl = self.Body.newObject("PartDesign::SubtractiveCylinder", "Cylinder")
            cyl.Radius = 3
            cyl.Height = 40
            cyl.Placement = placement
            self.Doc.recompute()
            face = [i for i, f in enumerate(cyl.Shape.Faces, 1)
                    if f.Surface.TypeId == "Part::GeomCylinder"][0]
            hole = self.Body.newObject("PartDesign::Hole", "Hole")
            hole.Profile = (cyl, ["Face%d" % face])
            hole.Diameter = 8
            hole.DepthType = "Dimension"
            hole.Depth = 10
            hole.DrillPoint = "Flat"
            axis = cyl.Shape.Faces[face - 1].Surface.Axis
            for reversed in (False, True):
                hole.Reversed = reversed
                self.Doc.recompute()
                self.assertNotIn("Invalid", hole.State)
                removed = cyl.Shape.cut(hole.Shape)
                # a ring D8 over D6, 10 deep along the axis, at one end
                self.assertAlmostEqual(removed.Volume, pi * (16 - 9) * 10, places=4,
                                       msg="%s reversed %s" % (placement, reversed))
                bb = removed.BoundBox
                extent = abs(axis.x) * bb.XLength + abs(axis.y) * bb.YLength \
                    + abs(axis.z) * bb.ZLength
                self.assertAlmostEqual(extent, 10, places=4)
            self.Body.removeObject(hole)
            self.Doc.removeObject(hole.Name)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestHole")
        #print ("omit closing document for debugging")

