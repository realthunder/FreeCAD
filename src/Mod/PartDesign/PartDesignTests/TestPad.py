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

import unittest

import FreeCAD
import Part
from FreeCAD import Base
import TestSketcherApp

class TestPad(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPad")

    def testBoxCase(self):
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject','SketchPad')
        TestSketcherApp.CreateSlotPlateSet(self.PadSketch)
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad","Pad")
        self.Pad.Profile = self.PadSketch
        self.Doc.recompute()
        self.assertEqual(len(self.Pad.Shape.Faces), 6)

    def testSketchOnPlane(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject','SketchPad')
        self.PadSketch.Support = (self.Doc.XY_Plane, [''])
        self.PadSketch.MapMode = 'FlatFace'
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateSlotPlateSet(self.PadSketch)
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad","Pad")
        self.Pad.Profile = self.PadSketch
        self.Body.addObject(self.Pad)
        self.Doc.recompute()
        self.assertEqual(len(self.Pad.Shape.Faces), 6)

    def testStartOffsetAndReference(self):
        # upstream bcc3e296fa
        self.PadSketch = self.Doc.addObject("Sketcher::SketchObject", "SketchPad")
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (1, 1))
        self.Doc.recompute()

        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Pad.Profile = self.PadSketch
        self.Pad.StartType = "Offset"
        self.Pad.StartOffset = 2
        self.Pad.Length = 3
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMin, 2.0)
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMax, 5.0)

        reference = self.Doc.addObject("Part::Feature", "Reference")
        outer = Part.makeCylinder(2, 1, Base.Vector(0, 0, 10))
        inner = Part.makeCylinder(1, 1, Base.Vector(0, 0, 10))
        reference.Shape = outer.cut(inner)
        top_face = max(
            range(1, len(reference.Shape.Faces) + 1),
            key=lambda index: reference.Shape.Faces[index - 1].CenterOfMass.z,
        )
        self.Pad.StartReference = (reference, [f"Face{top_face}"])
        self.Pad.StartType = "Reference"
        self.Pad.StartOffset = 2
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMin, 13.0)
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMax, 16.0)

    def testStartOffsetForTwoSidedAndSymmetricPad(self):
        # upstream bcc3e296fa
        self.PadSketch = self.Doc.addObject("Sketcher::SketchObject", "SketchPad")
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (1, 1))
        self.Doc.recompute()

        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Pad.Profile = self.PadSketch
        self.Pad.StartType = "Offset"
        self.Pad.StartOffset = 2
        self.Pad.SideType = "Two sides"
        self.Pad.Length = 1
        self.Pad.Length2 = 1
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMin, 1.0)
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMax, 3.0)

        self.Pad.SideType = "Symmetric"
        self.Pad.Length = 4
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMin, 0.0)
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMax, 4.0)

    def testStartOffsetReversedTaperedAndBySketch(self):
        # The start moves along the extrusion, so a reversed pad starts below
        self.PadSketch = self.Doc.addObject("Sketcher::SketchObject", "SketchPad")
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (1, 1))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Pad.Profile = self.PadSketch
        self.Pad.StartType = "Offset"
        self.Pad.StartOffset = 2
        self.Pad.Length = 3
        self.Pad.Reversed = True
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMin, -5.0)
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMax, -2.0)

        # A taper builds by another path; it starts at the offset too
        self.Pad.Reversed = False
        self.Pad.TaperAngle = 10
        self.Doc.recompute()
        self.assertTrue(self.Pad.Shape.isValid())
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMin, 2.0)
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMax, 5.0)

        # A sketch is its placement's plane, plus the offset
        self.Pad.TaperAngle = 0
        reference = self.Doc.addObject("Sketcher::SketchObject", "Reference")
        reference.Placement = FreeCAD.Placement(Base.Vector(0, 0, 7), FreeCAD.Rotation())
        self.Pad.StartType = "Reference"
        self.Pad.StartReference = (reference, [""])
        self.Pad.StartOffset = -1
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMin, 6.0)
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMax, 9.0)

        # Back to the profile: the offset is kept but not used
        self.Pad.StartType = "Profile plane"
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMin, 0.0)
        self.assertAlmostEqual(self.Pad.Shape.BoundBox.ZMax, 3.0)
        self.assertTrue(self.Pad.getPropertyStatus("StartReference").count("ReadOnly"))

    def testPadToFirstCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        # Make first offset cube Pad
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 1), (1, 1))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 1
        self.Doc.recompute()
        # Make second pad on different plane and pad to first
        self.PadSketch1 = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad1')
        self.Body.addObject(self.PadSketch1)
        self.PadSketch1.MapMode = 'FlatFace'
        self.PadSketch1.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.PadSketch1, (0, 0), (1, 1))
        self.Doc.recompute()
        self.Pad1 = self.Doc.addObject("PartDesign::Pad", "Pad1")
        self.Body.addObject(self.Pad1)
        self.Pad1.Profile = self.PadSketch1
        self.Pad1.Type = 2
        self.Pad1.Reversed = 1
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad1.Shape.Volume, 2.0)

    def testPadtoLastCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        # Make first offset cube Pad
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0.5, 1), (0.5, 2))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 1
        self.Doc.recompute()
        # Make second pad on different plane and pad to first
        self.PadSketch1 = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad1')
        self.Body.addObject(self.PadSketch1)
        self.PadSketch1.MapMode = 'FlatFace'
        self.PadSketch1.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.PadSketch1, (0, 0), (1, 1))
        self.Doc.recompute()
        self.Pad1 = self.Doc.addObject("PartDesign::Pad", "Pad1")
        self.Body.addObject(self.Pad1)
        self.Pad1.Profile = self.PadSketch1
        self.Pad1.Type = 1
        self.Pad1.Reversed = 1
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad1.Shape.Volume, 3.0)

    def testPadToFaceCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        # Make first offset cube Pad
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 1), (1, 1))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 1
        self.Doc.recompute()
        # Make second pad on different plane and pad to face on first
        self.PadSketch1 = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad1')
        self.Body.addObject(self.PadSketch1)
        self.PadSketch1.MapMode = 'FlatFace'
        self.PadSketch1.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.PadSketch1, (0, 0), (1, 1))
        self.Doc.recompute()
        self.Pad1 = self.Doc.addObject("PartDesign::Pad", "Pad1")
        self.Body.addObject(self.Pad1)
        self.Pad1.Profile = self.PadSketch1
        self.Pad1.Type = 3
        self.Pad1.UpToFace = (self.Pad, ["Face3"])
        self.Pad1.Reversed = 1
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad1.Shape.Volume, 2.0)

    def testPadTwoDimensionsCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        # Make first offset cube Pad
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 1), (1, 1))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 1
        self.Doc.recompute()
        # Make second pad on different plane and pad to face on first
        self.PadSketch1 = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad1')
        self.Body.addObject(self.PadSketch1)
        self.PadSketch1.MapMode = 'FlatFace'
        self.PadSketch1.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.PadSketch1, (0, 0), (1, 1))
        self.Doc.recompute()
        self.Pad1 = self.Doc.addObject("PartDesign::Pad", "Pad1")
        self.Body.addObject(self.Pad1)
        self.Pad1.Profile = self.PadSketch1
        self.Pad1.Type = 4
        self.Pad1.Length = 1.0
        self.Pad1.Length2 = 2.0
        self.Pad1.Reversed = 1
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad1.Shape.Volume, 4.0)

    def testSketchOnDatumPlane(self):
        # upstream 0b4e01047f
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.DatumPlane = self.Doc.addObject('PartDesign::Plane','DatumPlane')
        self.DatumPlane.AttachmentSupport = (self.Doc.XY_Plane, [''])
        self.DatumPlane.MapMode = 'FlatFace'
        self.Body.addObject(self.DatumPlane)
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject','SketchPad')
        self.PadSketch.AttachmentSupport = (self.DatumPlane, [''])
        self.PadSketch.MapMode = 'FlatFace'
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateSlotPlateSet(self.PadSketch)
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad","Pad")
        self.Pad.Profile = self.PadSketch
        self.Body.addObject(self.Pad)
        self.Doc.recompute()
        self.assertEqual(len(self.Pad.Shape.Faces), 6)

    def testPadToPlaneCustomDir(self):
        # upstream 490d6c5abc: the parallel check is against the direction,
        # not the sketch normal
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 1), (1, 1))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Type = 3 # UpToFace
        self.Pad.UseCustomVector = True
        self.Pad.Direction = FreeCAD.Vector(0,1,1)
        self.Pad.UpToFace = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad.Shape.Volume, 1.5)

    def testPadToConcaveCase(self):
        # upstream 8b9f5bdc4f: up to the first face, which is the inside of
        # a half ring and so concave seen from the sketch
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.RevolutionSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.RevolutionSketch)
        TestSketcherApp.CreateRectangleSketch(self.RevolutionSketch, (9, 0), (10, 5))
        self.Doc.recompute()
        self.Revolution = self.Doc.addObject("PartDesign::Revolution", "Revolution")
        self.Body.addObject(self.Revolution)
        self.Revolution.Profile = self.RevolutionSketch
        self.Revolution.ReferenceAxis = (self.RevolutionSketch, ['V_Axis'])
        self.Revolution.Angle = 180
        self.Doc.recompute()
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (1, 1))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Type = 2 # UpToFirst
        self.Pad.Reversed = True
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pad.Shape.Volume, 2208.0963, places=4)

    def testUpToMovedLCSPlane(self):
        """Up to a plane of a moved coordinate system (upstream 194ec0820c):
        the plane was taken at its own local placement, in the sketch."""
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        lcs = self.Doc.addObject("App::LocalCoordinateSystem", "LCS")
        lcs.Placement = FreeCAD.Placement(FreeCAD.Vector(0, 0, 20), FreeCAD.Rotation())
        self.Doc.recompute()
        xy = [f for f in lcs.OriginFeatures if f.Role == "XY_Plane"][0]
        sketch = self.Body.newObject("Sketcher::SketchObject", "Square")
        sketch.Support = ([f for f in self.Body.Origin.OriginFeatures
                           if f.Role == "XY_Plane"][0], [""])
        sketch.MapMode = "FlatFace"
        TestSketcherApp.CreateRectangleSketch(sketch, (0, 0), (10, 10))
        self.Doc.recompute()
        pad = self.Body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sketch
        pad.Type = "UpToFace"
        pad.UpToFace = (xy, [""])
        self.Doc.recompute()
        self.assertIn("Up-to-date", pad.State)
        self.assertAlmostEqual(pad.Shape.BoundBox.ZMax, 20)
        self.assertAlmostEqual(pad.Shape.Volume, 2000)

    def testPadUpToShape(self):
        """Up to one face, several, or a whole shape (upstream 309dd6e30d,
        8b9f5bdc4f): the pad stops at the nearest; the Type was offered and
        failed with "Unknown method 'UpToShape'"."""
        plate = self.Doc.addObject("Part::Box", "Plate")
        plate.Length = plate.Width = 10
        plate.Height = 1
        plate.Placement.Base = FreeCAD.Vector(20, 0, 30)
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        sketch = self.Body.newObject("Sketcher::SketchObject", "SketchPad")
        TestSketcherApp.CreateRectangleSketch(sketch, (20, 0), (10, 10))
        self.Doc.recompute()
        pad = self.Body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sketch
        pad.Type = "UpToShape"
        bottom = [i for i, f in enumerate(plate.Shape.Faces, 1)
                  if abs(f.CenterOfMass.z - 30) < 1e-6][0]
        top = [i for i, f in enumerate(plate.Shape.Faces, 1)
               if abs(f.CenterOfMass.z - 31) < 1e-6][0]
        for faces in (["Face%d" % bottom], ["Face%d" % bottom, "Face%d" % top], [""]):
            pad.UpToShape = [(plate, faces)]
            self.Doc.recompute()
            self.assertNotIn("Invalid", pad.State, faces)
            self.assertAlmostEqual(pad.Shape.Volume, 3000, msg=faces)
            self.assertAlmostEqual(pad.Shape.BoundBox.ZMax, 30, msg=faces)
        # one face can be offset, as an up to face; several cannot
        pad.Offset = 1
        self.Doc.recompute()
        self.assertIn("Invalid", pad.State)
        pad.UpToShape = [(plate, ["Face%d" % bottom])]
        self.Doc.recompute()
        self.assertNotIn("Invalid", pad.State)
        self.assertAlmostEqual(pad.Shape.BoundBox.ZMax, 31)

    def testTwoLengthsIgnoresMidplane(self):
        # A TwoLengths pad with Midplane left set from before goes Length up
        # and Length2 down, not centred (upstream 529779fb53, the test of the
        # fix taken as e20dcdaf5d)
        body = self.Doc.addObject('PartDesign::Body', 'Body')
        sketch = body.newObject('Sketcher::SketchObject', 'Sketch')
        sketch.AttachmentSupport = (self.Doc.XY_Plane, [''])
        sketch.MapMode = 'FlatFace'
        sketch.addGeometry(Part.Circle(FreeCAD.Vector(), FreeCAD.Vector(0, 0, 1), 10), False)
        self.Doc.recompute()
        pad = body.newObject('PartDesign::Pad', 'Pad')
        pad.Profile = sketch
        pad.Midplane = True
        pad.Length = 10
        pad.Length2 = 20
        pad.Type = 'TwoLengths'
        self.Doc.recompute()
        self.assertNotIn('Invalid', pad.State)
        box = pad.Shape.BoundBox
        self.assertAlmostEqual(box.XMin, -10)
        self.assertAlmostEqual(box.XMax, 10)
        self.assertAlmostEqual(box.ZMax, 10)
        self.assertAlmostEqual(box.ZMin, -20)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestPad")
        #print ("omit closing document for debugging")

