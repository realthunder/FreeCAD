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

    def testNegativeAngle(self):
        # A negative angle drafts the other way (upstream eb886449c2); it
        # was clamped to 0
        volumes = []
        for angle in (5, -5):
            body = self.Doc.addObject('PartDesign::Body', 'NegBody')
            box = body.newObject('PartDesign::AdditiveBox', 'NegBox')
            box.Length = box.Width = box.Height = 20
            self.Doc.recompute()
            draft = body.newObject('PartDesign::Draft', 'NegDraft')
            draft.Base = (box, ['Face1', 'Face2', 'Face3', 'Face4'])
            draft.NeutralPlane = (box, ['Face5'])
            draft.Angle = angle
            self.Doc.recompute()
            self.assertAlmostEqual(draft.Angle.Value, angle)
            self.assertNotIn('Invalid', draft.State)
            volumes.append(draft.Shape.Volume)
        self.assertLess(volumes[0], 8000)
        self.assertGreater(volumes[1], 8000)

    def testGuessedNeutralPlaneKeepsItsEdge(self):
        """With no neutral plane given the draft takes one from an edge of
        the face, and which edge decides which way it goes. It was "the first
        that will do" in the order the face lists its edges, and a base
        recomputed by another kernel version listed them differently: the
        draft of a file turned the other way. The edge is written down now."""
        import os, tempfile
        body = self.Doc.addObject("PartDesign::Body", "Body")
        plate = body.newObject("PartDesign::AdditiveBox", "Plate")
        plate.Length = 12
        plate.Width = 12
        plate.Height = 1
        self.Doc.recompute()

        def top_face(shape):
            return max(range(len(shape.Faces)), key=lambda i: shape.Faces[i].CenterOfMass.z)

        def edge_name(shape, edge):
            for i, e in enumerate(shape.Edges):
                if e.isSame(edge):
                    return "Edge%d" % (i + 1)
            self.fail("an edge of the face is not an edge of the shape")

        top = top_face(plate.Shape)
        draft = body.newObject("PartDesign::Draft", "Draft")
        draft.Base = (plate, ["Face%d" % (top + 1)])
        draft.Angle = 11
        self.Doc.recompute()
        if not draft.isValid():
            # about the edge the guess took the face has to tip up, not down
            # through the plate
            draft.Reversed = True
            self.Doc.recompute()
        self.assertTrue(draft.isValid(), draft.getStatusString())
        volume = draft.Shape.Volume
        reversed_ = draft.Reversed

        # the edge is written down, and it is one of the face's
        self.assertEqual(draft._NeutralEdge[0], plate)
        guessed = draft._NeutralEdge[1][0]
        face = plate.Shape.Faces[top]
        names = [edge_name(plate.Shape, e) for e in face.Edges]
        self.assertIn(guessed, names)

        # ... with the side of it the pull direction is on, told by the face
        sense = draft._NeutralSense
        self.assertIn(sense, (1, -1))
        plain = plate.Shape.Volume
        self.assertGreater(volume, plain)
        high = draft.Shape.BoundBox.ZMax

        # The other side turns the draft over: what tipped up tips down
        # through the plate, as Reversed would.
        draft._NeutralSense = -sense
        draft.touch()  # the record is the feature's own: writing it asks for nothing
        self.Doc.recompute()
        self.assertTrue(not draft.isValid() or draft.Shape.Volume < plain)
        self.assertEqual(draft._NeutralSense, -sense)
        draft._NeutralSense = sense
        draft.touch()  # the record is the feature's own: writing it asks for nothing
        self.Doc.recompute()
        self.assertTrue(draft.isValid(), draft.getStatusString())
        self.assertAlmostEqual(draft.Shape.Volume, volume, places=6)

        # The opposite edge, the same side of it: the mirror image.
        here = plate.Shape.getElement(guessed).CenterOfMass
        opposite = max(names, key=lambda n: (plate.Shape.getElement(n).CenterOfMass - here).Length)
        self.assertNotEqual(opposite, guessed)
        draft._NeutralEdge = (plate, [opposite])
        draft.touch()  # the record is the feature's own: writing it asks for nothing
        self.Doc.recompute()
        self.assertTrue(draft.isValid(), draft.getStatusString())
        self.assertAlmostEqual(draft.Shape.Volume, volume, places=6)
        self.assertAlmostEqual(draft.Shape.BoundBox.ZMax, high, places=6)
        mirrored = draft.Shape.CenterOfMass
        # and the record is what ruled, not a guess made afresh
        self.assertEqual(draft._NeutralEdge[1][0], opposite)
        self.assertEqual(draft._NeutralSense, sense)

        # It is saved with the file ...
        folder = tempfile.mkdtemp()
        path = os.path.join(folder, "draft.FCStd")
        self.Doc.saveCopy(path)
        doc = FreeCAD.openDocument(path)
        try:
            restored = doc.getObject("Draft")
            self.assertEqual(restored._NeutralEdge[1][0], opposite)
            self.assertEqual(restored._NeutralSense, sense)
            restored.touch()
            doc.recompute()
            self.assertTrue(restored.isValid(), restored.getStatusString())
            self.assertAlmostEqual(restored.Shape.Volume, volume, places=6)
            self.assertLess((restored.Shape.CenterOfMass - mirrored).Length, 1e-6)
        finally:
            FreeCAD.closeDocument(doc.Name)

        # ... and a file from before it gets it as it is restored, from the
        # base's shape as the file has it
        draft._NeutralEdge = None
        draft._NeutralSense = 0
        self.assertIsNone(draft._NeutralEdge)
        old = os.path.join(folder, "before.FCStd")
        self.Doc.saveCopy(old)
        doc = FreeCAD.openDocument(old)
        try:
            restored = doc.getObject("Draft")
            self.assertIsNotNone(restored._NeutralEdge)
            self.assertEqual(restored._NeutralEdge[0], doc.getObject("Plate"))
            self.assertEqual(restored._NeutralEdge[1][0], guessed)
            self.assertEqual(restored._NeutralSense, sense)
        finally:
            FreeCAD.closeDocument(doc.Name)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestDraft")
        # print ("omit closing document for debugging")

