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
import Part

class TestRevolve(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestRevolve")

    def testRevolveFace(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Body.addObject(self.Box)
        self.Box.Length=10.00
        self.Box.Width=10.00
        self.Box.Height=10.00
        self.Doc.recompute()
        self.Revolution = self.Doc.addObject("PartDesign::Revolution","Revolution")
        self.Revolution.Profile = (self.Box, ["Face6"])
        self.Revolution.ReferenceAxis = (self.Doc.Y_Axis,[""])
        self.Revolution.Angle = 180.0
        self.Revolution.Reversed = 1
        self.Body.addObject(self.Revolution)
        self.Doc.recompute()
        # depending on if refinement is done we expect 8 or 10 faces
        self.assertIn(len(self.Revolution.Shape.Faces), (8, 10))

    def testGrooveFace(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Body.addObject(self.Box)
        self.Box.Length=10.00
        self.Box.Width=10.00
        self.Box.Height=10.00
        self.Doc.recompute()
        self.Groove = self.Doc.addObject("PartDesign::Groove","Groove")
        self.Groove.Profile = (self.Box, ["Face6"])
        self.Groove.ReferenceAxis = (self.Doc.X_Axis,[""])
        self.Groove.Angle = 180.0
        self.Groove.Reversed = 1
        self.Body.addObject(self.Groove)
        self.Doc.recompute()
        self.assertEqual(len(self.Groove.Shape.Faces), 5)

    def testRevolveAboutMovedLCSAxis(self):
        # upstream b3a1fd9676: the axis of a coordinate system is taken where
        # that system is, not through the origin along its local X
        import math
        import TestSketcherApp
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        lcs = self.Doc.addObject('App::LocalCoordinateSystem', 'LCS')
        lcs.Placement = FreeCAD.Placement(FreeCAD.Vector(0, 20, 0),
                                          FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 90))
        self.Doc.recompute()
        xAxis = [f for f in lcs.OriginFeatures if f.Role == "X_Axis"][0]
        sketch = self.Body.newObject('Sketcher::SketchObject', 'Sketch')
        # 2 x 2 square beside the LCS X axis, which runs along global Y at x = 0
        TestSketcherApp.CreateRectangleSketch(sketch, (1, 21), (2, 2))
        self.Doc.recompute()
        self.Revolution = self.Body.newObject("PartDesign::Revolution", "Revolution")
        self.Revolution.Profile = sketch
        self.Revolution.ReferenceAxis = (xAxis, [""])
        self.Revolution.Angle = 360.0
        self.Doc.recompute()
        self.assertAlmostEqual(self.Revolution.Shape.Volume, math.pi * (9 - 1) * 2, places=6)
        bb = self.Revolution.Shape.BoundBox
        self.assertAlmostEqual(bb.YMin, 21)
        self.assertAlmostEqual(bb.YMax, 23)

    # The sides and the start (upstream b2da06bfe0, 06a4b1db99; their C++
    # tests/src/Mod/PartDesign/App/Revolution.cpp, here in Python). Every
    # case revolves a circle of radius 10 centred 30 from the Y axis, so a
    # sweep of d degrees is that fraction of a torus.

    profileRadius = 10.0
    axisDistance = 30.0

    def torusVolume(self, degrees):
        import math
        return (2 * math.pi ** 2 * self.axisDistance * self.profileRadius ** 2
                * degrees / 360.0)

    def makeBody(self):
        self.Body = self.Doc.addObject('PartDesign::Body', 'Body')
        return self.Body

    def addCircleSketch(self, centerX, radius, name='Sketch'):
        import Part
        sketch = self.Body.newObject('Sketcher::SketchObject', name)
        sketch.AttachmentSupport = (self.Doc.XY_Plane, [''])
        sketch.MapMode = 'FlatFace'
        sketch.addGeometry(Part.Circle(FreeCAD.Vector(centerX, 0, 0),
                                       FreeCAD.Vector(0, 0, 1), radius), False)
        return sketch

    def addRevolved(self, kind='Revolution', **props):
        if not hasattr(self, 'Body'):
            self.makeBody()
        if not hasattr(self, 'Profile'):
            self.Profile = self.addCircleSketch(self.axisDistance, self.profileRadius, 'Profile')
        feature = self.Body.newObject('PartDesign::' + kind, kind)
        feature.Profile = self.Profile
        feature.ReferenceAxis = (self.Doc.Y_Axis, [''])
        for name, value in props.items():
            setattr(feature, name, value)
        self.Doc.recompute()
        return feature

    def addBaseCylinder(self):
        # Contains every torus below: they reach 40 from Y and 40 along Z
        self.makeBody()
        sketch = self.addCircleSketch(0, 45, 'BaseSketch')
        pad = self.Body.newObject('PartDesign::Pad', 'Pad')
        pad.Profile = sketch
        pad.SideType = 'Symmetric'
        pad.Length = 100
        self.Doc.recompute()
        import math
        return math.pi * 45 * 45 * 100

    def addCrossedBox(self):
        # The orbit of the profile's centre crosses it between x = 5 and
        # x = -5, around 80.4 and 99.6 degrees
        self.makeBody()
        box = self.Body.newObject('PartDesign::AdditiveBox', 'Box')
        box.Length = 10
        box.Width = 40
        box.Height = 30
        box.Placement = FreeCAD.Placement(FreeCAD.Vector(-5, -20, -45), FreeCAD.Rotation())
        self.Doc.recompute()
        return box

    def assertSweep(self, feature, degrees, shape=None):
        # The box faces x = +-5 are off the axis, so a side ending there is
        # not quite radial: 1e-4 of the volume
        self.assertNotIn('Invalid', feature.State)
        shape = feature.AddSubShape if shape is None else shape
        expected = self.torusVolume(degrees)
        self.assertAlmostEqual(shape.Volume, expected, delta=max(1.0, expected * 1e-4))

    def testTwoSidesAngles(self):
        revolution = self.addRevolved(Angle=90)
        self.assertSweep(revolution, 90)
        revolution.SideType = 'Two sides'
        revolution.Angle2 = 90
        self.Doc.recompute()
        self.assertSweep(revolution, 180, revolution.Shape)

    def testTwoSidesSignedAngles(self):
        # A negative second angle turns back into side 1: 90 and -10 is 10 to 90
        revolution = self.addRevolved(SideType='Two sides', Angle=90, Angle2=-10)
        self.assertSweep(revolution, 80)
        # and a negative first angle turns the other way: -50 and 20 is -50 to -20
        revolution.Angle = -50
        revolution.Angle2 = 20
        self.Doc.recompute()
        self.assertSweep(revolution, 30)
        self.assertGreater(revolution.AddSubShape.BoundBox.ZMin, 0)

    def testTwoSidesOverlappingAngles(self):
        revolution = self.addRevolved(SideType='Two sides', Angle=200, Angle2=200)
        self.assertSweep(revolution, 360)

    def testTwoAnglesThatCancelAreAnError(self):
        revolution = self.addRevolved(SideType='Two sides', Angle=40, Angle2=-40)
        self.assertIn('Invalid', revolution.State)

    def testSymmetricAngle(self):
        revolution = self.addRevolved(SideType='Symmetric', Angle=180)
        self.assertSweep(revolution, 180)
        bb = revolution.Shape.BoundBox
        self.assertAlmostEqual(bb.ZMin, -bb.ZMax, places=6)

    def testMidplaneMapsToSideType(self):
        revolution = self.addRevolved(Angle=180)
        revolution.Midplane = True
        self.assertEqual(revolution.SideType, 'Symmetric')
        revolution.Midplane = False
        self.assertEqual(revolution.SideType, 'One side')
        revolution.SideType = 'Symmetric'
        self.assertTrue(revolution.Midplane)

    def testTwoAnglesMapsToTwoSides(self):
        revolution = self.addRevolved(Angle=60)
        revolution.Angle2 = 30
        revolution.Type = 'TwoAngles'
        self.assertEqual(revolution.SideType, 'Two sides')
        self.assertEqual(revolution.Type, 'Angle')
        self.assertEqual(revolution.Type2, 'Angle')
        self.Doc.recompute()
        self.assertSweep(revolution, 90)

    def testGrooveTwoSidesThroughAll(self):
        cylinder = self.addBaseCylinder()
        groove = self.addRevolved('Groove', Type='ThroughAll')
        self.assertAlmostEqual(groove.Shape.Volume, cylinder - self.torusVolume(360), delta=1.0)
        groove.SideType = 'Two sides'
        groove.Type2 = 'ThroughAll'
        self.Doc.recompute()
        self.assertAlmostEqual(groove.Shape.Volume, cylinder - self.torusVolume(360), delta=1.0)

    def testGrooveTypesFollowTheClass(self):
        # The panel's Operation switches AddSubType, not the class: a Groove
        # made additive still reads its second type as Through all
        cylinder = self.addBaseCylinder()
        groove = self.addRevolved('Groove', Type='ThroughAll')
        groove.AddSubType = 'Additive'
        self.Doc.recompute()
        self.assertSweep(groove, 360)
        # The torus lies inside the cylinder: adding it changes nothing
        self.assertAlmostEqual(groove.Shape.Volume, cylinder, delta=1.0)

    def testGrooveStartOffset(self):
        cylinder = self.addBaseCylinder()
        groove = self.addRevolved('Groove', Angle=30, StartType='Offset', StartOffset=90)
        self.assertAlmostEqual(groove.Shape.Volume, cylinder - self.torusVolume(30), delta=1.0)
        self.assertLess(groove.AddSubShape.BoundBox.ZMax, -15)

    def testSecondSideUpToFaceWithoutTargetIsAnError(self):
        revolution = self.addRevolved(Angle=90, SideType='Two sides', Type2='UpToFace')
        self.assertIn('Invalid', revolution.State)

    def testRevolutionStartOffsetAndReference(self):
        # upstream be0d14c042
        import Part
        profile = self.Doc.addObject("Sketcher::SketchObject", "StandaloneProfile")
        points = [FreeCAD.Vector(2, 0), FreeCAD.Vector(3, 0), FreeCAD.Vector(3, 1),
                  FreeCAD.Vector(2, 1)]
        for start, end in zip(points, points[1:] + points[:1]):
            profile.addGeometry(Part.LineSegment(start, end), False)

        axis = self.Doc.addObject("Part::Feature", "Axis")
        axis.Shape = Part.makeLine(FreeCAD.Vector(0, -1, 0), FreeCAD.Vector(0, 2, 0))

        revolution = self.Doc.addObject("PartDesign::Revolution", "OffsetRevolution")
        revolution.Profile = profile
        revolution.ReferenceAxis = (axis, ["Edge1"])
        revolution.Angle = 30
        revolution.StartType = "Offset"
        revolution.StartOffset = 105
        self.Doc.recompute()

        direct = revolution.AddSubShape.BoundBox
        self.assertLess(direct.XMax, -0.5)
        self.assertLess(direct.ZMax, -1.4)

        reference = self.Doc.addObject("Part::Feature", "StartReference")
        reference.Shape = Part.Face(Part.makePolygon([
            FreeCAD.Vector(0, -1, -1), FreeCAD.Vector(0, 2, -1), FreeCAD.Vector(0, 2, -4),
            FreeCAD.Vector(0, -1, -4), FreeCAD.Vector(0, -1, -1)]))
        revolution.StartReference = (reference, ["Face1"])
        revolution.StartType = "Reference"
        revolution.StartOffset = 15
        self.Doc.recompute()

        # The plane x = 0 is met 90 degrees round; 15 more is where 105 was
        ref = revolution.AddSubShape.BoundBox
        for actual, expected in zip(
                (ref.XMin, ref.XMax, ref.YMin, ref.YMax, ref.ZMin, ref.ZMax),
                (direct.XMin, direct.XMax, direct.YMin, direct.YMax, direct.ZMin, direct.ZMax)):
            self.assertAlmostEqual(actual, expected)

    def assertSameBounds(self, a, b, places=3):
        for actual, expected in zip((a.XMin, a.XMax, a.YMin, a.YMax, a.ZMin, a.ZMax),
                                    (b.XMin, b.XMax, b.YMin, b.YMax, b.ZMin, b.ZMax)):
            self.assertAlmostEqual(actual, expected, places=places)

    def testStartReferenceCurvedFace(self):
        # A face that is not planar is where the orbit of the profile's
        # centre first cuts it. A cylinder of radius 3 along Y, centred on
        # that orbit 90 degrees round, is cut 2 asin(3 / 60) before that.
        import math
        self.makeBody()
        cylinder = self.Doc.addObject('Part::Cylinder', 'Cylinder')
        cylinder.Radius = 3
        cylinder.Height = 40
        cylinder.Placement = FreeCAD.Placement(FreeCAD.Vector(0, -20, -30),
                                               FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), -90))
        self.Doc.recompute()
        entry = 90 - math.degrees(2 * math.asin(3.0 / 60))
        byOffset = self.addRevolved(Angle=20, StartType='Offset', StartOffset=entry)
        byReference = self.addRevolved(Angle=20, StartType='Reference')
        byReference.StartReference = (cylinder, ['Face1'])
        self.Doc.recompute()
        self.assertSweep(byReference, 20)
        self.assertSameBounds(byReference.AddSubShape.BoundBox, byOffset.AddSubShape.BoundBox)

    def testStartReferencePlane(self):
        # A plane is met where the orbit crosses it: the box's face x = 5,
        # acos(5 / 30) round
        import math
        box = self.addCrossedBox()
        byOffset = self.addRevolved(Angle=20, StartType='Offset',
                                    StartOffset=math.degrees(math.acos(5.0 / 30)))
        byReference = self.addRevolved(Angle=20, StartType='Reference')
        byReference.StartReference = (box, ['Face2'])
        self.Doc.recompute()
        self.assertSameBounds(byReference.AddSubShape.BoundBox, byOffset.AddSubShape.BoundBox)

    def testUpToFirstAndLast(self):
        # Up to first and last were refused before (upstream 80664a0d30).
        # Up to a face of the base went a full turn: BRepFeat does not stop
        # at a face of the shape it adds to.
        import math
        self.addCrossedBox()
        first = math.degrees(math.acos(5.0 / 30))
        revolution = self.addRevolved(Type='UpToFirst')
        self.assertSweep(revolution, first)
        # What lies inside the box adds nothing, so up to last adds the same
        revolution.Type = 'UpToLast'
        self.Doc.recompute()
        self.assertSweep(revolution, first)
        # The other way round the first face met is x = -5
        revolution.Type = 'UpToFirst'
        revolution.Reversed = True
        self.Doc.recompute()
        self.assertSweep(revolution, 360 - math.degrees(math.acos(-5.0 / 30)))
        revolution.Reversed = False
        revolution.SideType = 'Two sides'
        revolution.Type2 = 'Angle'
        revolution.Angle2 = 45
        self.Doc.recompute()
        self.assertSweep(revolution, first + 45)

    def testUpToFaceToolIsTheSideAlone(self):
        # AddSubShape of a revolution up to a face held the base as well
        import math
        box = self.addCrossedBox()
        revolution = self.addRevolved(Type='UpToFace', UpToFace=(box, ['Face2']))
        first = math.degrees(math.acos(5.0 / 30))
        self.assertSweep(revolution, first)
        self.assertAlmostEqual(revolution.Shape.Volume,
                               box.Shape.Volume + self.torusVolume(first), delta=1.0)

    def testSymmetricUpToFace(self):
        # The face is mirrored in the profile plane for the other side
        import math
        box = self.addCrossedBox()
        revolution = self.addRevolved(Type='UpToFace', SideType='Symmetric',
                                      UpToFace=(box, ['Face2']))
        self.assertSweep(revolution, 2 * math.degrees(math.acos(5.0 / 30)))

    def testGrooveUpToFace(self):
        # A groove up to a face cut the base from itself and failed with
        # "Resulting shape is not a solid"
        cylinder = self.addBaseCylinder()
        stop = self.Body.newObject('PartDesign::Plane', 'Stop')
        stop.MapMode = 'Deactivated'
        # Through the axis, 60 degrees round from the profile
        stop.Placement = FreeCAD.Placement(FreeCAD.Vector(),
                                           FreeCAD.Rotation(FreeCAD.Vector(0, 1, 0), 60))
        self.Doc.recompute()
        groove = self.addRevolved('Groove', Type='UpToFace', UpToFace=(stop, ['']))
        self.assertSweep(groove, 60)
        self.assertAlmostEqual(groove.Shape.Volume, cylinder - self.torusVolume(60), delta=1.0)
        # Symmetric mirrors the datum plane, which is located: 60 each way
        groove.SideType = 'Symmetric'
        self.Doc.recompute()
        self.assertSweep(groove, 120)
        # The same plane for side 2 is met 120 degrees back
        groove.SideType = 'Two sides'
        groove.Type2 = 'UpToFace'
        groove.UpToFace2 = (stop, [''])
        self.Doc.recompute()
        self.assertSweep(groove, 180)

    def testOpenWireBinderProfile(self):
        # A binder of an open wire is a profile a revolution takes: its plane
        # gives the normal (upstream e38fe196d5). It failed "Axis must not be
        # perpendicular to the sketch plane"
        body = self.Doc.addObject('PartDesign::Body', 'OpenBody')
        sketch = self.Doc.addObject('Sketcher::SketchObject', 'OpenSketch')
        sketch.Placement = FreeCAD.Placement(
            FreeCAD.Vector(), FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), 90))
        pts = [FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(10, 0, 0),
               FreeCAD.Vector(10, 20, 0), FreeCAD.Vector(0, 20, 0)]
        for a, b in zip(pts, pts[1:]):
            sketch.addGeometry(Part.LineSegment(a, b), False)
        self.Doc.recompute()
        binder = body.newObject('PartDesign::SubShapeBinder', 'OpenBinder')
        binder.Support = [(sketch, '')]
        self.Doc.recompute()
        rev = body.newObject('PartDesign::Revolution', 'OpenRevolution')
        rev.Profile = binder
        rev.ReferenceAxis = ([o for o in body.Origin.OriginFeatures
                              if o.Role == 'Z_Axis'][0], [''])
        rev.Angle = 360
        self.Doc.recompute()
        self.assertNotIn('Invalid', rev.State)
        self.assertAlmostEqual(rev.Shape.Volume, math.pi * 100 * 20, places=3)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestRevolve")
        # print ("omit closing document for debugging")

