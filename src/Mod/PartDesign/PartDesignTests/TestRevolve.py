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

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestRevolve")
        # print ("omit closing document for debugging")

