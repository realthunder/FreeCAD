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

    def makeDraftOn(self, shape, face, neutral, method, angle=5, stop=True, reversed=False):
        """A Draft of the faces picked by <face> about the face picked by
        <neutral>, on a Part::Feature holding <shape> as the body's base."""
        base = self.Doc.addObject("Part::Feature", "Base")
        base.Shape = shape
        body = self.Doc.addObject("PartDesign::Body", "Body")
        body.BaseFeature = base
        self.Doc.recompute()
        faces = ["Face%d" % i for i, f in enumerate(shape.Faces, 1) if face(f)]
        neutrals = ["Face%d" % i for i, f in enumerate(shape.Faces, 1) if neutral(f)]
        self.assertEqual(len(neutrals), 1)
        draft = body.newObject("PartDesign::Draft", "Draft")
        draft.Base = (base, faces)
        draft.NeutralPlane = (base, neutrals)
        draft.Angle = angle
        draft.Method = method
        draft.StopAtBody = stop
        draft.Reversed = reversed
        self.Doc.recompute()
        return draft

    @staticmethod
    def planeAt(axis, value, xmin=None, xmax=None):
        def pick(f):
            if f.Surface.__class__.__name__ != "Plane":
                return False
            b = f.BoundBox
            if abs(getattr(b, axis + "Min") - value) > 1e-9:
                return False
            if abs(getattr(b, axis + "Max") - value) > 1e-9:
                return False
            return xmin is None or (b.XMin > xmin - 1e-9 and b.XMax < xmax + 1e-9)
        return pick

    def testDraftSplitWallPiece(self):
        # Two 10x10x5 boxes side by side, not refined: their front wall y=0
        # is two coplanar pieces. One piece drafted about the floor: the
        # classic draft cannot keep the other piece, the new one drafts the
        # pieces as one face. The rest of the body is not refined: floor,
        # top and back stay in two pieces each.
        V = App.Vector
        shape = Part.makeBox(10, 10, 5).fuse(Part.makeBox(10, 10, 5, V(10, 0, 0)))
        volume = 1000 - 20 * 5 * 5 * math.tan(math.radians(5)) / 2
        for method in ("Auto", "New"):
            draft = self.makeDraftOn(shape, self.planeAt("Y", 0, 10, 20),
                                     self.planeAt("Z", 0, 0, 10), method)
            self.assertNotIn("Invalid", draft.State, method)
            self.assertTrue(draft.Shape.isValid(), method)
            self.assertAlmostEqual(draft.Shape.Volume, volume, 6, method)
            self.assertEqual(len(draft.Shape.Faces), 9, method)

    def testDraftSplitFloorCorner(self):
        # A prism whose front wall y=0 (x in [10,20]) meets a slanted wall
        # at (10,0), floor and top split along x=10 from that corner. The
        # wall drafted about the end x=20: its corner slides along the
        # slanted wall, past the split, which the classic draft refuses.
        V = App.Vector
        prism = Part.Face(Part.makePolygon([V(0, -5, 0), V(10, 0, 0), V(10, 10, 0),
                                            V(0, 10, 0), V(0, -5, 0)])).extrude(V(0, 0, 5))
        shape = prism.fuse(Part.makeBox(10, 10, 5, V(10, 0, 0)))
        # The drafted wall y = -(20 - x) tan(5 deg) meets the slanted wall
        # y = (x - 10) / 2; the solid is that outline extruded 5.
        t = math.tan(math.radians(5))
        x = (5 - 20 * t) / (0.5 - t)
        pts = [(0, -5), (x, (x - 10) / 2), (20, 0), (20, 10), (0, 10)]
        area = abs(sum(pts[i][0] * pts[i - 1][1] - pts[i - 1][0] * pts[i][1]
                       for i in range(len(pts)))) / 2
        draft = self.makeDraftOn(shape, self.planeAt("Y", 0), self.planeAt("X", 20), "Auto")
        self.assertNotIn("Invalid", draft.State)
        self.assertTrue(draft.Shape.isValid())
        self.assertAlmostEqual(draft.Shape.Volume, 5 * area, 6)
        # the splits of floor and top are kept
        self.assertEqual(len(draft.Shape.Faces), 10)

    def testDraftWallBreaksThrough(self):
        # A 20 cube, a 4x6 slot 2 off its front wall y=0, 18 deep. The slot's
        # front wall drafted about its floor at 15 deg swings 18 tan(15 deg)
        # toward the front, through the 2 thick wall at 2 / tan(15 deg) above
        # the floor. The classic draft refuses; Auto falls back to the new
        # draft, which cuts the front wall open.
        V = App.Vector
        shape = Part.makeBox(20, 20, 20).cut(
            Part.makeBox(4, 6, 18, V(8, 2, 2))).removeSplitter()
        h = 2 / math.tan(math.radians(15))
        volume = 8000 - 4 * 6 * 18 - 4 * (h * 2 / 2 + 2 * (18 - h))
        draft = self.makeDraftOn(shape, self.planeAt("Y", 2), self.planeAt("Z", 2), "Auto",
                                 angle=15)
        self.assertNotIn("Invalid", draft.State)
        self.assertTrue(draft.Shape.isValid())
        self.assertAlmostEqual(draft.Shape.Volume, volume, 6)
        # the drafted face and the front wall it breaks through are named
        # after the base's faces, the same over a recompute
        names = dict(draft.Shape.ElementMap)
        front = [i for i, f in enumerate(shape.Faces, 1) if self.planeAt("Y", 0)(f)][0]
        cut = [n for n, e in names.items()
               if n.startswith("Face%d;" % front) and e.startswith("Face")]
        self.assertEqual(len(cut), 1)
        self.assertLess(draft.Shape.getElement(names[cut[0]]).Area, 400 - 1)
        draft.touch()
        self.Doc.recompute()
        self.assertEqual(dict(draft.Shape.ElementMap), names)

    def testDraftStopAtBody(self):
        # A 20x10x10 block, a notch [0,10]x[0,5]x[5,10] off its front top.
        # The ledge z=5 drafted about the notch's back wall at 60 deg rises
        # 5 tan(60 deg) = 8.66 at its front, past the block's top at 10. By
        # default it stops at the top's plane, which closes over the notch's
        # front; without the stop a fin stands over the top.
        V = App.Vector
        shape = Part.makeBox(20, 10, 10).cut(Part.makeBox(10, 5, 5, V(0, 0, 5)))
        shape = shape.removeSplitter()
        t = math.tan(math.radians(60))
        for stop, volume in ((False, 1750 + 125 * t),
                             (True, 1750 + 125 * t - 10 * (5 * t - 5) ** 2 / (2 * t))):
            draft = self.makeDraftOn(shape, self.planeAt("Z", 5), self.planeAt("Y", 5), "New",
                                     angle=60, stop=stop)
            self.assertNotIn("Invalid", draft.State, stop)
            self.assertTrue(draft.Shape.isValid(), stop)
            self.assertAlmostEqual(draft.Shape.Volume, volume, 6, stop)

    def testDraftFaceVanishes(self):
        # A wedge truncated by a 0.2 wide face at x=10, its walls meeting
        # 0.2 past it. Drafted outward about a plane 50 below, the face's
        # new plane lies past where the walls meet: the face would vanish,
        # which the new draft refuses rather than hand back a body without it.
        V = App.Vector
        shape = Part.Face(Part.makePolygon([V(0, -5, 0), V(10, -0.1, 0), V(10, 0.1, 0),
                                            V(0, 5, 0), V(0, -5, 0)])).extrude(V(0, 0, 10))
        base = self.Doc.addObject("Part::Feature", "Base")
        base.Shape = shape
        plane = self.Doc.addObject("Part::Feature", "Plane")
        plane.Shape = Part.makePlane(200, 200, V(-100, -100, -50))
        body = self.Doc.addObject("PartDesign::Body", "Body")
        body.BaseFeature = base
        self.Doc.recompute()
        face = [i for i, f in enumerate(shape.Faces, 1) if self.planeAt("X", 10)(f)][0]
        draft = body.newObject("PartDesign::Draft", "Draft")
        draft.Base = (base, ["Face%d" % face])
        draft.NeutralPlane = (plane, ["Face1"])
        draft.Angle = 5
        draft.Method = "New"
        draft.Reversed = True
        self.Doc.recompute()
        self.assertIn("Invalid", draft.State)
        self.assertIn("FaceVanishes", draft.getStatusString())

    def testDraftAutoCrossesRib(self):
        # A 20x10x2 plate with two ribs 2x6x10, 3 apart. The first rib's
        # inner face drafted outward at 20 deg about the plate's top leans
        # 10 tan(20 deg) = 3.64 over the gap, into the second rib. The
        # classic draft's solid is valid, but that face crosses the second
        # rib's, and its volume counts the overlap twice. Auto checks the
        # classic result and takes the new draft, which fuses the ribs.
        V = App.Vector
        shape = Part.makeBox(20, 10, 2).fuse([Part.makeBox(2, 6, 10, V(0, 2, 2)),
                                              Part.makeBox(2, 6, 10, V(5, 2, 2))])
        shape = shape.removeSplitter()
        t = math.tan(math.radians(20))
        classic = 640 + 300 * t
        overlap = 6 * (50 * t - 30 + 4.5 / t)
        for method, volume in (("Classic", classic), ("Auto", classic - overlap)):
            draft = self.makeDraftOn(shape, self.planeAt("X", 2), self.planeAt("Z", 2), method,
                                     angle=20, reversed=True)
            self.assertNotIn("Invalid", draft.State, method)
            self.assertTrue(draft.Shape.isValid(), method)
            self.assertAlmostEqual(draft.Shape.Volume, volume, 6, method)
            if method == "Classic":
                self.assertRaises(ValueError, draft.Shape.check, True)
            else:
                draft.Shape.check(True)

    def testDraftAutoStopAtBody(self):
        # A 20x10x10 block stepped down to 5 over its front half. The ledge
        # z=5 drafted about the step's wall at 60 deg rises 5 tan(60 deg) =
        # 8.66 at its front, past the top at 10. The classic draft makes the
        # fin; with the stop on, Auto finds the body grown past its top and
        # takes the new draft, which stops at the top's plane.
        V = App.Vector
        shape = Part.makeBox(20, 10, 10).cut(Part.makeBox(20, 5, 5, V(0, 0, 5)))
        shape = shape.removeSplitter()
        t = math.tan(math.radians(60))
        fin = 1500 + 250 * t
        for method, stop, volume in (("Classic", True, fin),
                                     ("Auto", False, fin),
                                     ("Auto", True, fin - 20 * (5 * t - 5) ** 2 / (2 * t))):
            draft = self.makeDraftOn(shape, self.planeAt("Z", 5), self.planeAt("Y", 5), method,
                                     angle=60, stop=stop)
            self.assertNotIn("Invalid", draft.State, (method, stop))
            self.assertTrue(draft.Shape.isValid(), (method, stop))
            self.assertAlmostEqual(draft.Shape.Volume, volume, 6, (method, stop))

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestDraft")
        # print ("omit closing document for debugging")

