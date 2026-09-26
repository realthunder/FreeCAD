# SPDX-License-Identifier: LGPL-2.1-or-later
# ***************************************************************************
# *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>              *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This library is free software; you can redistribute it and/or         *
# *   modify it under the terms of the GNU Library General Public           *
# *   License as published by the Free Software Foundation; either          *
# *   version 2 of the License, or (at your option) any later version.      *
# *                                                                         *
# *   This library  is distributed in the hope that it will be useful,      *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# ***************************************************************************

"""Pad and Pocket sides: SideType, the second side's type, and the aliases
kept for older files and scripts (Midplane, TwoLengths, UpToFace)."""

import os
import tempfile
import unittest

import FreeCAD
import TestSketcherApp


class TestExtrudeSides(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestExtrudeSides")
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def sketch(self, name, corner, size, z=0.0):
        sketch = self.Body.newObject("Sketcher::SketchObject", name)
        sketch.AttachmentSupport = (self.Doc.getObject("XY_Plane"), [""])
        sketch.MapMode = "FlatFace"
        sketch.AttachmentOffset = FreeCAD.Placement(FreeCAD.Vector(0, 0, z), FreeCAD.Rotation())
        TestSketcherApp.CreateRectangleSketch(sketch, corner, size)
        return sketch

    def box(self):
        """A 40 x 40 x 10 box, z 0..10"""
        pad = self.Body.newObject("PartDesign::Pad", "Box")
        pad.Profile = (self.sketch("BoxSketch", (-20, -20), (40, 40)), [""])
        pad.Length = 10
        self.Doc.recompute()
        return pad

    @staticmethod
    def faceAt(obj, z):
        for i, face in enumerate(obj.Shape.Faces):
            if abs(face.CenterOfMass.z - z) < 1e-6 and abs(face.normalAt(0, 0).z) > 0.99:
                return "Face%d" % (i + 1)
        raise RuntimeError("no face at z=%s" % z)

    def zRange(self, shape):
        box = shape.BoundBox
        return round(box.ZMin, 6), round(box.ZMax, 6)

    def pad(self, z=0.0, name="Pad"):
        pad = self.Body.newObject("PartDesign::Pad", name)
        pad.Profile = (self.sketch(name + "Sketch", (0, 0), (10, 10), z), [""])
        return pad

    def testTwoLengthsFromScript(self):
        pad = self.pad()
        pad.Type = "TwoLengths"
        pad.Length = 7
        pad.Length2 = 3
        self.Doc.recompute()
        self.assertEqual(pad.SideType, "Two sides")
        self.assertEqual(pad.Type, "Length")
        self.assertEqual(pad.Type2, "Length")
        self.assertAlmostEqual(pad.Shape.Volume, 1000)
        self.assertEqual(self.zRange(pad.Shape), (-3, 7))

    def testMidplaneIsSymmetric(self):
        pad = self.pad()
        pad.Length = 8
        pad.Midplane = True
        self.assertEqual(pad.SideType, "Symmetric")
        self.Doc.recompute()
        self.assertEqual(self.zRange(pad.Shape), (-4, 4))
        pad.SideType = "One side"
        self.assertFalse(pad.Midplane)
        pad.SideType = "Symmetric"
        self.assertTrue(pad.Midplane)

    def testNegativeSecondLength(self):
        pad = self.pad()
        pad.SideType = "Two sides"
        pad.Length = 10
        pad.Length2 = -4
        self.Doc.recompute()
        self.assertAlmostEqual(pad.Shape.Volume, 600)
        self.assertEqual(self.zRange(pad.Shape), (4, 10))

    def testLengthAndUpToFace(self):
        box = self.box()
        pad = self.pad(z=20)
        pad.SideType = "Two sides"
        pad.Length = 5
        pad.Type2 = "UpToFace"
        pad.UpToFace2 = (box, [self.faceAt(box, 10)])
        self.Doc.recompute()
        self.assertTrue(pad.isValid())
        self.assertAlmostEqual(pad.AddSubShape.Volume, 1500)
        self.assertEqual(self.zRange(pad.AddSubShape), (10, 25))
        self.assertAlmostEqual(pad.Shape.Volume, 16000 + 1500)

    def testSymmetricUpToFace(self):
        box = self.box()
        pad = self.pad(z=20)
        pad.SideType = "Symmetric"
        pad.Type = "UpToFace"
        pad.UpToFace = (box, [self.faceAt(box, 10)])
        self.Doc.recompute()
        self.assertTrue(pad.isValid())
        # down to the box, and the mirror of that up from the profile
        self.assertEqual(self.zRange(pad.AddSubShape), (10, 30))
        self.assertAlmostEqual(pad.AddSubShape.Volume, 2000)

    def testSideRunningIntoTheOther(self):
        # The second side, up to a plane on the first side's way, runs back
        # into the first: what they share cancels, as a negative length did
        plane = self.Body.newObject("PartDesign::Plane", "Plane")
        plane.AttachmentSupport = [(self.Doc.getObject("XY_Plane"), "")]
        plane.MapMode = "FlatFace"
        plane.AttachmentOffset = FreeCAD.Placement(FreeCAD.Vector(0, 0, 25), FreeCAD.Rotation())
        pad = self.pad(z=20)
        pad.SideType = "Two sides"
        pad.Length = 10
        pad.Type2 = "UpToFace"
        pad.UpToFace2 = (plane, [""])
        self.Doc.recompute()
        self.assertTrue(pad.isValid())
        self.assertEqual(self.zRange(pad.Shape), (25, 30))
        self.assertAlmostEqual(pad.Shape.Volume, 500)

    def testUpToShapeSync(self):
        box = self.box()
        top = self.faceAt(box, 10)
        bottom = self.faceAt(box, 0)
        pad = self.pad(z=20)
        pad.Type = "UpToShape"
        pad.UpToShape = [(box, [top])]
        # a single face is written as UpToFace too
        self.assertEqual(pad.Type, "UpToFace")
        self.assertEqual(pad.UpToFace[0], box)
        self.assertEqual(list(pad.UpToFace[1]), [top])
        self.Doc.recompute()
        self.assertEqual(self.zRange(pad.AddSubShape), (10, 20))
        # several faces are UpToShape only
        pad.UpToShape = [(box, [top, bottom])]
        self.assertEqual(pad.Type, "UpToShape")
        self.assertIsNone(pad.UpToFace)
        self.Doc.recompute()
        self.assertTrue(pad.isValid())
        self.assertEqual(self.zRange(pad.AddSubShape), (10, 20))
        # and UpToFace set by a script comes into UpToShape
        pad.UpToFace = (box, [top])
        self.assertEqual([(o, list(s)) for o, s in pad.UpToShape], [(box, [top])])

    def testWholeObjectUpToFaceMeansFirstFace(self):
        # UpToFace has always taken a whole object's first face; UpToShape,
        # its mirror, names that face rather than meaning the whole shape
        box = self.box()
        pad = self.pad(z=20)
        pad.UpToFace = (box, [])
        self.assertEqual([(o, list(s)) for o, s in pad.UpToShape], [(box, ["Face1"])])

    def testPocketTwoSides(self):
        box = self.box()
        pocket = self.Body.newObject("PartDesign::Pocket", "Pocket")
        pocket.Profile = (self.sketch("PocketSketch", (-5, -5), (10, 10), z=5), [""])
        pocket.SideType = "Two sides"
        pocket.Length = 2
        pocket.Type2 = "UpToFace"
        pocket.UpToFace2 = (box, [self.faceAt(box, 10)])
        self.Doc.recompute()
        self.assertTrue(pocket.isValid())
        # 2 down from the profile at z 5, and up to the top
        self.assertAlmostEqual(pocket.Shape.Volume, 16000 - 700)
        pocket.Type = "ThroughAll"
        pocket.Type2 = "ThroughAll"
        self.Doc.recompute()
        self.assertAlmostEqual(pocket.Shape.Volume, 16000 - 1000)

    def testSaveAndRestore(self):
        box = self.box()
        pad = self.pad(z=20)
        pad.SideType = "Two sides"
        pad.Length = 5
        pad.Type2 = "UpToShape"
        pad.UpToShape2 = [(box, [self.faceAt(box, 10), self.faceAt(box, 0)])]
        self.Doc.recompute()
        volume = pad.Shape.Volume
        path = os.path.join(tempfile.gettempdir(), "PartDesignTestExtrudeSides.FCStd")
        self.Doc.saveAs(path)
        name = self.Doc.Name
        FreeCAD.closeDocument(name)
        self.Doc = FreeCAD.openDocument(path)
        pad = self.Doc.getObject("Pad")
        self.assertEqual(pad.SideType, "Two sides")
        self.assertEqual(pad.Type2, "UpToShape")
        self.assertIsNone(pad.UpToFace2)
        self.assertEqual(len(pad.UpToShape2[0][1]), 2)
        self.assertFalse(pad.Midplane)
        pad.touch()
        self.Doc.recompute()
        self.assertAlmostEqual(pad.Shape.Volume, volume)
        os.remove(path)
