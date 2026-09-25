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

App = FreeCAD

class TestDraft(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestDraft")

    def testSimpleDraft(self):
        # fix: create datum plane on YZ. create datum line on Z + 10i
        # find top face by first making list comprehension of Z-normal faces
        # and then find which has the higher center of mass Z-value
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Body.addObject(self.Box)
        self.Box.Length=10.00
        self.Box.Width=10.00
        self.Box.Height=10.00
        self.Doc.recompute()
        self.DatumPlane = self.Doc.addObject('PartDesign::Plane','DatumPlane')
        self.DatumPlane.Support = [(self.Doc.YZ_Plane,'')]
        self.DatumPlane.MapMode = 'FlatFace'
        self.Body.addObject(self.DatumPlane)
        self.Doc.recompute()
        self.DatumLine = self.Doc.addObject('PartDesign::Line','DatumLine')
        self.DatumLine.Support = [(self.Doc.X_Axis,'')]
        self.DatumLine.MapMode = 'TwoPointLine'
        self.Body.addObject(self.DatumLine)
        self.Doc.recompute()
        self.Draft = self.Doc.addObject("PartDesign::Draft","Draft")
        # Draft.Base needs to be top face
        self.Faces = self.Box.Shape.Faces
        # Grab the two faces with Z-normals and find the higher one
        self.ZFaceIndexes = [i for i in range(len(self.Faces)) if self.Faces[i].Surface.Axis == App.Vector(0,0,1)]
        if self.Faces[self.ZFaceIndexes[0]].CenterOfMass.z > self.Faces[self.ZFaceIndexes[1]].CenterOfMass.z:
            self.TopFaceIndex = self.ZFaceIndexes[0]
        else:
            self.TopFaceIndex = self.ZFaceIndexes[1]
        self.Draft.Base = (self.Box, ["Face"+str(self.TopFaceIndex+1)])
        self.Draft.NeutralPlane = (self.DatumPlane, [''])
        self.Draft.PullDirection = (self.DatumLine, [''])
        self.Draft.Angle = 45.0
        self.Draft.Reversed = 1
        self.Body.addObject(self.Draft)
        self.Doc.recompute()
        if 'Invalid' in self.Draft.State:
            self.Draft.Reversed = 0
            self.Doc.recompute()
        self.assertAlmostEqual(self.Draft.Shape.Volume, 1500)

    def testSketchNeutralPlane(self):
        """A whole sketch is a neutral plane, as a datum plane where it is
        (upstream 51f4ad7432); it was "No neutral plane reference
        specified"."""
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = box.Height = 10
        sketch = body.newObject("Sketcher::SketchObject", "Sketch")
        sketch.MapMode = "Deactivated"
        sketch.Placement = App.Placement(App.Vector(0, 0, 3), App.Rotation())
        plane = body.newObject("PartDesign::Plane", "DatumPlane")
        plane.MapMode = "Deactivated"
        plane.Placement = sketch.Placement
        self.Doc.recompute()
        side = [i for i, f in enumerate(box.Shape.Faces, 1)
                if abs(f.normalAt(0, 0).x + 1) < 1e-9][0]
        draft = body.newObject("PartDesign::Draft", "Draft")
        draft.Base = (box, ["Face%d" % side])
        draft.Angle = 10
        volumes = []
        for reference in (plane, sketch):
            draft.NeutralPlane = (reference, [""])
            self.Doc.recompute()
            self.assertNotIn("Invalid", draft.State)
            volumes.append(draft.Shape.Volume)
        self.assertNotAlmostEqual(volumes[0], 1000)
        self.assertAlmostEqual(volumes[1], volumes[0])

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestDraft")
        # print ("omit closing document for debugging")

