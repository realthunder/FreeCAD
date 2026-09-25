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
import math
import TestSketcherApp

class TestPocket(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPocket")

    def testPocketDimensionCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'PadSketch')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 1
        self.Pad.Reversed = 1
        self.Doc.recompute()
        self.PocketSketch = self.Doc.addObject('Sketcher::SketchObject', 'PocketSketch')
        self.Body.addObject(self.PocketSketch)
        TestSketcherApp.CreateRectangleSketch(self.PocketSketch, (2.5, 2.5), (5, 5))
        self.Doc.recompute()
        self.Pocket = self.Doc.addObject("PartDesign::Pocket", "Pocket")
        self.Body.addObject(self.Pocket)
        self.Pocket.Profile = self.PocketSketch
        self.Pocket.Length = 1
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pocket.Shape.Volume, 75.0)

    def testPocketThroughAllCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'PadSketch')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 1
        self.Pad.Reversed = 1
        self.Doc.recompute()
        self.PocketSketch = self.Doc.addObject('Sketcher::SketchObject', 'PocketSketch')
        self.Body.addObject(self.PocketSketch)
        TestSketcherApp.CreateRectangleSketch(self.PocketSketch, (2.5, 2.5), (5, 5))
        self.Doc.recompute()
        self.Pocket = self.Doc.addObject("PartDesign::Pocket", "Pocket")
        self.Body.addObject(self.Pocket)
        self.Pocket.Profile = self.PocketSketch
        self.Pocket.Length = 1
        self.Doc.recompute()
        self.PocketSketch1 = self.Doc.addObject('Sketcher::SketchObject', 'PocketSketch')
        self.Body.addObject(self.PocketSketch1)
        self.PocketSketch1.MapMode = 'FlatFace'
        self.PocketSketch1.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.PocketSketch1, (2.5, -0.75), (5, 0.50))
        self.Doc.recompute()
        self.Pocket001 = self.Doc.addObject("PartDesign::Pocket", "Pocket001")
        self.Body.addObject(self.Pocket001)
        self.Pocket001.Profile = self.PocketSketch1
        self.Pocket001.Type = 1
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pocket001.Shape.Volume, 62.5)

    def testPocketToFirstCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'PadSketch')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 1
        self.Pad.Reversed = 1
        self.Doc.recompute()
        self.PocketSketch = self.Doc.addObject('Sketcher::SketchObject', 'PocketSketch')
        self.Body.addObject(self.PocketSketch)
        TestSketcherApp.CreateRectangleSketch(self.PocketSketch, (2.5, 2.5), (5, 5))
        self.Doc.recompute()
        self.Pocket = self.Doc.addObject("PartDesign::Pocket", "Pocket")
        self.Body.addObject(self.Pocket)
        self.Pocket.Profile = self.PocketSketch
        self.Pocket.Length = 1
        self.Doc.recompute()
        self.PocketSketch1 = self.Doc.addObject('Sketcher::SketchObject', 'PocketSketch')
        self.Body.addObject(self.PocketSketch1)
        self.PocketSketch1.MapMode = 'FlatFace'
        self.PocketSketch1.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.PocketSketch1, (2.5, -1), (5, 1))
        self.Doc.recompute()
        self.Pocket001 = self.Doc.addObject("PartDesign::Pocket", "Pocket001")
        self.Body.addObject(self.Pocket001)
        self.Pocket001.Profile = self.PocketSketch1
        self.Pocket001.Type = 2
        self.Doc.recompute()
        self.assertAlmostEqual(self.Pocket001.Shape.Volume, 62.5)

    def testPocketToFaceCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'PadSketch')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 1
        self.Pad.Reversed = 1
        self.Doc.recompute()
        self.PocketSketch = self.Doc.addObject('Sketcher::SketchObject', 'PocketSketch')
        self.Body.addObject(self.PocketSketch)
        TestSketcherApp.CreateRectangleSketch(self.PocketSketch, (2.5, 2.5), (5, 5))
        self.Doc.recompute()
        self.Pocket = self.Doc.addObject("PartDesign::Pocket", "Pocket")
        self.Body.addObject(self.Pocket)
        self.Pocket.Profile = self.PocketSketch
        self.Pocket.Length = 1
        self.Doc.recompute()
        self.PocketSketch1 = self.Doc.addObject('Sketcher::SketchObject', 'PocketSketch')
        self.Body.addObject(self.PocketSketch1)
        self.PocketSketch1.MapMode = 'FlatFace'
        self.PocketSketch1.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        TestSketcherApp.CreateRectangleSketch(self.PocketSketch1, (0, -1), (10, 1))
        self.Doc.recompute()
        self.Pocket001 = self.Doc.addObject("PartDesign::Pocket", "Pocket001")
        self.Body.addObject(self.Pocket001)
        self.Pocket001.Profile = self.PocketSketch1
        self.Pocket001.Type = 3
        # Handle face-naming inconsistency in OCC < 7
        self.FaceNumber = 7
        self.Pocket001.UpToFace = (self.Pocket, ["Face"+str(self.FaceNumber)])
        self.Doc.recompute()
        while (('Invalid' in self.Pocket001.State or round(self.Pocket001.Shape.Volume, 7) != 50.0) and self.FaceNumber < 11):
            self.FaceNumber += 1
            self.Pocket001.UpToFace = (self.Pocket, ["Face"+str(self.FaceNumber)])
            self.Doc.recompute()
        self.assertAlmostEqual(self.Pocket001.Shape.Volume, 50.0)

    def testPocketToCurvedFaceFromHollow(self):
        # upstream issue 16690: a pocket sketched inside a pipe, up to one of
        # its cylinders, cut beyond the face instead of up to it
        import Part
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        self.PadSketch.addGeometry(Part.Circle(FreeCAD.Vector(), FreeCAD.Vector(0, 0, 1), 10))
        self.PadSketch.addGeometry(Part.Circle(FreeCAD.Vector(), FreeCAD.Vector(0, 0, 1), 8))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 30
        self.Doc.recompute()
        self.PocketSketch = self.Doc.addObject('Sketcher::SketchObject', 'PocketSketch')
        self.Body.addObject(self.PocketSketch)
        self.PocketSketch.MapMode = 'FlatFace'
        self.PocketSketch.Support = (self.Doc.XZ_Plane, [''])
        self.PocketSketch.addGeometry(Part.Circle(FreeCAD.Vector(0, 15, 0), FreeCAD.Vector(0, 0, 1), 3))
        self.Doc.recompute()
        self.Pocket = self.Doc.addObject("PartDesign::Pocket", "Pocket")
        self.Body.addObject(self.Pocket)
        self.Pocket.Profile = self.PocketSketch
        self.Pocket.Type = 3 # UpToFace
        faces = {}
        for i, face in enumerate(self.Pad.Shape.Faces):
            if isinstance(face.Surface, Part.Cylinder):
                faces[round(face.Surface.Radius)] = "Face%d" % (i + 1)
        padVolume = self.Pad.Shape.Volume
        for reversed_ in (False, True):
            self.Pocket.Reversed = reversed_
            # through the wall: a hole of radius 3 in a 2 mm wall
            self.Pocket.UpToFace = (self.Pad, [faces[10]])
            self.Doc.recompute()
            self.assertAlmostEqual(padVolume - self.Pocket.Shape.Volume, 57.38, places=1)
            # up to the inside of the wall: nothing to remove
            self.Pocket.UpToFace = (self.Pad, [faces[8]])
            self.Doc.recompute()
            self.assertAlmostEqual(self.Pocket.Shape.Volume, padVolume, places=1)

    def testThroughAllWithTaper(self):
        """A taper through all goes through all (upstream d52260b2f4); the
        taper path took Length, so it cut only Length deep."""
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = 40
        box.Height = 30
        self.Doc.recompute()
        sketch = self.Body.newObject("Sketcher::SketchObject", "Square")
        sketch.Support = (box, ["Face6"])
        sketch.MapMode = "FlatFace"
        TestSketcherApp.CreateRectangleSketch(sketch, (15, 15), (10, 10))
        self.Doc.recompute()
        pocket = self.Body.newObject("PartDesign::Pocket", "Pocket")
        pocket.Profile = sketch
        pocket.Length = 5
        pocket.TaperAngle = 5
        pocket.Type = "ThroughAll"
        self.Doc.recompute()
        self.assertIn("Up-to-date", pocket.State)
        self.assertFalse(pocket.getPropertyStatus("TaperAngle"))
        # a frustum from 10 x 10 at the top, widening 30 * tan(5 deg) a side
        bottom = 10 + 2 * 30 * math.tan(math.radians(5))
        removed = 30 / 3 * (100 + bottom ** 2 + 10 * bottom)
        self.assertAlmostEqual(40 * 40 * 30 - pocket.Shape.Volume, removed, delta=0.5)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestPocket")
        #print ("omit closing document for debugging")
