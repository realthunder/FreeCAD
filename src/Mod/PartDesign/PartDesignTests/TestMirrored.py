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
import TestSketcherApp

class TestMirrored(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestMirrored")

    def testMirroredSketchCase(self):
        """
        Creates a unit cube cornered at the origin and mirrors it about the Y axis.
        This operation should create a rectangular prism with volume 2.0.
        """
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Rect = self.Doc.addObject('Sketcher::SketchObject','Rect')
        self.Body.addObject(self.Rect)
        TestSketcherApp.CreateRectangleSketch(self.Rect, (0, 0), (1, 1))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad","Pad")
        self.Pad.Profile = self.Rect
        self.Pad.Length = 1
        self.Body.addObject(self.Pad)
        self.Doc.recompute()
        self.Mirrored = self.Doc.addObject("PartDesign::Mirrored","Mirrored")
        self.Mirrored.Originals = [self.Pad]
        self.Mirrored.MirrorPlane = (self.Rect, ["V_Axis"])
        self.Body.addObject(self.Mirrored)
        self.Doc.recompute()
        self.assertAlmostEqual(self.Mirrored.Shape.Volume, 2.0)

    def testMirroredPrimitiveCase(self):
        """
        Tests the same mirroring scenario as in the sketch case,
        but is designed to ensure the same end result occurs with
        a different base object.
        """
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Box.Length=1
        self.Box.Width=1
        self.Box.Height=1
        self.Body.addObject(self.Box)
        self.Doc.recompute()
        self.Mirrored = self.Doc.addObject("PartDesign::Mirrored", "Mirrored")
        self.Mirrored.Originals = [self.Box]
        self.Mirrored.MirrorPlane = (self.Doc.XY_Plane, [""])
        self.Body.addObject(self.Mirrored)
        self.Doc.recompute()
        self.assertAlmostEqual(self.Mirrored.Shape.Volume, 2.0)

    def testMirroredOffsetCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Body.SingleSolid = True
        self.Rect = self.Doc.addObject('Sketcher::SketchObject','Rect')
        self.Body.addObject(self.Rect)
        TestSketcherApp.CreateRectangleSketch(self.Rect, (0, 1), (1, 1))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad","Pad")
        self.Pad.Profile = self.Rect
        self.Pad.Length = 1
        self.Body.addObject(self.Pad)
        self.Doc.recompute()
        self.Mirrored = self.Doc.addObject("PartDesign::Mirrored","Mirrored")
        self.Mirrored.Originals = [self.Pad]
        self.Mirrored.MirrorPlane = (self.Rect, ["H_Axis"])
        self.Body.addObject(self.Mirrored)
        self.Doc.recompute()
        self.assertIn("Up-to-date", self.Mirrored.State)

    def testPlaneWithoutSubname(self):
        """A plane set in the property editor has no subname at all
        (upstream 7d10f5ed73): it was refused as no plane."""
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = self.Body.newObject("PartDesign::AdditiveBox", "Box")
        self.Box.Length = self.Box.Width = self.Box.Height = 10
        self.Box.Placement.Base = FreeCAD.Vector(0, 5, 0)
        xz = [f for f in self.Body.Origin.OriginFeatures if f.Role == "XZ_Plane"][0]
        self.Doc.recompute()
        mirrored = self.Body.newObject("PartDesign::Mirrored", "Mirrored")
        mirrored.Originals = [self.Box]
        mirrored.MirrorPlane = (xz, [])
        self.Doc.recompute()
        self.assertIn("Up-to-date", mirrored.State)
        self.assertAlmostEqual(mirrored.Shape.BoundBox.YMin, -15)

    def testMovedLCSPlane(self):
        """A plane of a moved or turned coordinate system mirrors where the
        plane is, not across its global counterpart."""
        for placement, check in (
                (FreeCAD.Placement(FreeCAD.Vector(0, 0, 20), FreeCAD.Rotation()),
                 lambda box: self.assertAlmostEqual(box.ZMax, 40)),
                (FreeCAD.Placement(FreeCAD.Vector(0, 0, 20),
                                   FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), 90)),
                 lambda box: self.assertAlmostEqual(box.YMin, -10))):
            doc = FreeCAD.newDocument("PartDesignTestMirroredLCS")
            try:
                body = doc.addObject("PartDesign::Body", "Body")
                box = body.newObject("PartDesign::AdditiveBox", "Box")
                box.Length = box.Width = box.Height = 10
                lcs = doc.addObject("App::LocalCoordinateSystem", "LCS")
                lcs.Placement = placement
                doc.recompute()
                xy = [f for f in lcs.OriginFeatures if f.Role == "XY_Plane"][0]
                mirrored = body.newObject("PartDesign::Mirrored", "Mirrored")
                mirrored.Originals = [box]
                mirrored.MirrorPlane = (xy, [""])
                doc.recompute()
                self.assertIn("Up-to-date", mirrored.State)
                check(mirrored.Shape.BoundBox)
            finally:
                FreeCAD.closeDocument(doc.Name)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestMirrored")
        #print ("omit closing document for debugging")
