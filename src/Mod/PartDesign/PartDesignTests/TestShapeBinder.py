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
from FreeCAD import Base
import Part
import Sketcher

class TestShapeBinder(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestShapeBinder")

    def testTwoBodyShapeBinderCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Box.Length=1
        self.Box.Width=1
        self.Box.Height=1
        self.Body.addObject(self.Box)
        self.Doc.recompute()
        self.Body001 = self.Doc.addObject('PartDesign::Body','Body001')
        self.ShapeBinder = self.Doc.addObject('PartDesign::ShapeBinder','ShapeBinder')
        self.ShapeBinder.Support = [(self.Box, 'Face1')]
        self.Body001.addObject(self.ShapeBinder)
        self.Doc.recompute()
        self.assertIn('Box', self.ShapeBinder.OutList[0].Label)
        self.assertIn('Body001', self.ShapeBinder.InList[0].Label)

    def testPointReference(self):
        # A datum point binds as a vertex where it is (upstream c5fbbb3830);
        # the binder was empty
        body = self.Doc.addObject('PartDesign::Body', 'PointBody')
        point = [o for o in body.Origin.OriginFeatures if o.TypeId == 'App::Point'][0]
        binder = body.newObject('PartDesign::ShapeBinder', 'PointBinder')
        binder.Support = [(point, '')]
        self.Doc.recompute()
        self.assertFalse(binder.Shape.isNull())
        self.assertEqual(len(binder.Shape.Vertexes), 1)
        self.assertAlmostEqual(binder.Shape.Vertexes[0].Point.Length, 0)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestShapeBinder")
        #print ("omit closing document for debugging")


class _Triple:
    """A feature whose B is computed, 3 * A; its shape is a box B long.
    At module level, so that a binder's copy of it restores its proxy."""

    def __init__(self, obj):
        obj.Proxy = self
        obj.addProperty("App::PropertyLength", "A")
        obj.addProperty("App::PropertyLength", "B")
        obj.A = 200
        obj.setPropertyStatus("A", ["CopyOnChange"])
        obj.setPropertyStatus("B", ["CopyOnChange", "ReadOnly", "Output"])

    def execute(self, obj):
        obj.B = 3 * obj.A.Value
        obj.Shape = Part.makeBox(obj.B.Value, 10, 10)

    def dumps(self):
        return None

    def loads(self, state):
        return None


class TestSubShapeBinder(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestSubShapeBinder")

    def tearDown(self):
        FreeCAD.closeDocument("PartDesignTestSubShapeBinder")

    def testCopyOnChangeCloses(self):
        """A binder that made a copy on change is closed with its document;
        it crashed, clearing its link to the copy after deleting the copy."""
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.addProperty("App::PropertyLength", "A")
        box.A = 20
        box.setExpression("Length", "A")
        box.setPropertyStatus("A", "CopyOnChange")
        self.Doc.recompute()
        binder = self.Doc.addObject("PartDesign::SubShapeBinder", "Binder")
        binder.Support = [(box, "")]
        binder.BindCopyOnChange = "Enabled"
        self.Doc.recompute()
        binder.A = 30
        self.Doc.recompute()
        self.assertAlmostEqual(binder.Shape.BoundBox.XLength, 30)
        self.assertAlmostEqual(box.Shape.BoundBox.XLength, 20)
        # and the copy is let go when the binder follows its support again
        box.A = 25
        self.Doc.recompute()
        self.assertAlmostEqual(box.Shape.BoundBox.XLength, 25)
        # tearDown closes the document

    def testCopyOnChangeComputedComesBack(self):
        """A computed copy-on-change property -- ReadOnly and Output -- reads
        what the binder's copy computed, not the support's value (upstream
        2501296c95, 66e1c0154d)."""
        feat = self.Doc.addObject("Part::FeaturePython", "Triple")
        _Triple(feat)
        self.Doc.recompute()
        binder = self.Doc.addObject("PartDesign::SubShapeBinder", "Binder")
        binder.Support = [(feat, "")]
        binder.BindCopyOnChange = "Enabled"
        self.Doc.recompute()
        self.assertAlmostEqual(binder.B.Value, 600)
        binder.A = 300
        self.Doc.recompute()
        self.assertEqual(binder.BindCopyOnChange, "Mutated")
        self.assertAlmostEqual(binder.Shape.BoundBox.XLength, 900)
        self.assertAlmostEqual(binder.B.Value, 900)
        self.assertAlmostEqual(feat.B.Value, 600)
        self.assertNotIn("Touched", binder.State)

    def _padAndBinder(self):
        """A pad on five lines, no two of its faces alike, and a binder of
        its body in another body, with three more binders on faces of it"""
        body = self.Doc.addObject("PartDesign::Body", "Body")
        sketch = body.newObject("Sketcher::SketchObject", "Sketch")
        points = [(0, 0), (30, 0), (30, 20), (10, 20), (0, 10)]
        for i, a in enumerate(points):
            b = points[(i + 1) % len(points)]
            sketch.addGeometry(
                Part.LineSegment(Base.Vector(a[0], a[1], 0), Base.Vector(b[0], b[1], 0))
            )
        for i in range(len(points)):
            sketch.addConstraint(
                Sketcher.Constraint("Coincident", i, 2, (i + 1) % len(points), 1)
            )
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sketch
        pad.Length = 12
        holder = self.Doc.addObject("PartDesign::Body", "Holder")
        binder = holder.newObject("PartDesign::SubShapeBinder", "Binder")
        binder.Support = [(body, ("",))]
        self.Doc.recompute()
        for face in ("Face1", "Face3", "Face7"):
            ref = holder.newObject("PartDesign::SubShapeBinder", "Ref" + face)
            ref.Support = [(binder, (face,))]
        self.Doc.recompute()
        return body, sketch, pad, holder, binder

    @staticmethod
    def _faceNames(binder):
        names = binder.Shape.ElementReverseMap
        return sorted((k, v) for k, v in names.items() if k.startswith("Face"))

    def _references(self):
        """What the three binders on faces refer to, and the area they get"""
        res = []
        for face in ("Face1", "Face3", "Face7"):
            ref = self.Doc.getObject("Ref" + face)
            self.assertTrue(ref.isValid(), ref.getStatusString())
            res.append((ref.Support[0][1], round(ref.Shape.Area, 6)))
        return res

    def testCopyOnChangeNamesAreTheSameEveryTime(self):
        """A binder that copies its support names its elements by the copies,
        and the copies are numbered the same every time: each binder copies
        into an emptied temporary document of its own. They all copied into
        one, whose ids went on from copy to copy and started at random in
        every session, so the first recompute after an open renamed every
        element, and a reference to one was lost unless it could be found
        again by its geometry."""
        import os, tempfile

        body, sketch, pad, holder, binder = self._padAndBinder()
        binder.BindCopyOnChange = "Mutated"
        self.Doc.recompute()
        names = self._faceNames(binder)
        references = self._references()
        self.assertEqual(len(names), 7)
        self.assertEqual([r[0] for r in references], [("Face1",), ("Face3",), ("Face7",)])

        copies = binder.getPropertyByName("_CopiedLink")[0].Document
        ids = sorted(o.ID for o in copies.Objects)
        self.assertEqual(ids, list(range(1, len(body.OutListRecursive) + 2)))

        # another binder of the same body, later: the same names for the
        # same faces, up to where its own id comes in
        other = holder.newObject("PartDesign::SubShapeBinder", "Other")
        other.Support = [(body, ("",))]
        other.BindCopyOnChange = "Mutated"
        self.Doc.recompute()
        self.assertNotEqual(other.getPropertyByName("_CopiedLink")[0].Document.Name, copies.Name)
        self.assertEqual(
            [name.split(";:X;")[0] for _, name in self._faceNames(other)],
            [name.split(";:X;")[0] for _, name in names],
        )

        path = os.path.join(tempfile.mkdtemp(), "PartDesignTestSubShapeBinder.FCStd")
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        for obj in self.Doc.Objects:
            obj.touch()
        self.Doc.recompute()
        self.assertEqual(self._faceNames(self.Doc.getObject("Binder")), names)
        self.assertEqual(self._references(), references)

    def testMovedBinderIsSearchedWhereItWent(self):
        """A relative binder whose container moved is recomputed to another
        place. A reference into it that has to be found again by its geometry
        -- the names changed as well -- is looked for where the geometry
        went. It was looked for where it had been, and lost."""
        body, sketch, pad, holder, binder = self._padAndBinder()
        names = self._faceNames(binder)
        references = self._references()
        center = binder.Shape.BoundBox.Center

        holder.Placement = FreeCAD.Placement(
            Base.Vector(3, -4, 53), FreeCAD.Rotation(Base.Vector(0, 1, 0), 3.5)
        )
        # other names for the same faces
        binder.BindCopyOnChange = "Mutated"
        self.Doc.recompute()
        self.assertTrue(binder.isValid(), binder.getStatusString())
        self.assertGreater(binder.Shape.BoundBox.Center.distanceToPoint(center), 50)
        renamed = set(name for _, name in self._faceNames(binder))
        self.assertFalse(renamed.intersection(name for _, name in names))
        self.assertEqual(self._references(), references)

    def testReferenceIntoBinderOfMovedBinder(self):
        """A binder ON a binder that moved goes with it, and is renamed with
        it, without a container of its own having moved: it has no motion to
        report. A reference into IT -- a third binder, a sketch's external
        geometry -- is found again all the same, because a shape that is its
        former self carried off whole says where everything went. It was
        looked for where it had been: "Missing external geometry reference"."""
        body, sketch, pad, holder, binder = self._padAndBinder()
        second = self.Doc.getObject("RefFace3")
        # two edges of the second binder that do not lie along z
        edges = [
            "Edge%d" % (i + 1)
            for i, e in enumerate(second.Shape.Edges)
            if abs(e.tangentAt(e.FirstParameter).z) < 0.5
        ]
        self.assertGreaterEqual(len(edges), 2)
        third = holder.newObject("PartDesign::SubShapeBinder", "Third")
        third.Support = [(second, (edges[0],))]
        outline = holder.newObject("Sketcher::SketchObject", "Outline")
        outline.addExternal(second.Name, edges[1])
        self.Doc.recompute()
        self.assertTrue(third.isValid(), third.getStatusString())
        self.assertTrue(outline.isValid(), outline.getStatusString())
        length = third.Shape.Length
        names = set(second.Shape.ElementReverseMap.values())
        center = second.Shape.BoundBox.Center

        holder.Placement = FreeCAD.Placement(
            Base.Vector(3, -4, 53), FreeCAD.Rotation(Base.Vector(0, 1, 0), 3.5)
        )
        # other names for the first binder's faces, and so for the second's
        binder.BindCopyOnChange = "Mutated"
        self.Doc.recompute()

        # the premise: the second binder went somewhere else under other names
        self.assertTrue(second.isValid(), second.getStatusString())
        self.assertGreater(second.Shape.BoundBox.Center.distanceToPoint(center), 50)
        self.assertFalse(names.intersection(second.Shape.ElementReverseMap.values()))

        self.assertTrue(third.isValid(), third.getStatusString())
        self.assertAlmostEqual(third.Shape.Length, length)
        self.assertTrue(outline.isValid(), outline.getStatusString())
        subs = [sub for _, group in outline.ExternalGeometry for sub in group]
        self.assertEqual(len(subs), 1)
        self.assertFalse(subs[0].startswith("?"), subs[0])

    def testOffsetBinder(self):
        # See PR 7445
        body = self.Doc.addObject('PartDesign::Body','Body')
        box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        body.addObject(box)

        box.Length=10.00000
        box.Width=10.00000
        box.Height=10.00000

        binder = body.newObject('PartDesign::SubShapeBinder','Binder')
        binder.Support=[(box, ("Edge2", "Edge12", "Edge6", "Edge10"))]
        self.Doc.recompute()

        self.assertAlmostEqual(binder.Shape.Length, 40)

        binder.OffsetJoinType="Tangent"
        binder.Offset = 5.00000
        self.Doc.recompute()

        self.assertAlmostEqual(binder.Shape.Length, 80)

    def testBinderBeforeOrAfterPad(self):
        """ Test case for PR #8763 """
        body = self.Doc.addObject('PartDesign::Body','Body')
        sketch = body.newObject('Sketcher::SketchObject','Sketch')
        sketch.Support = (self.Doc.XZ_Plane,[''])
        sketch.MapMode = 'FlatFace'
        self.Doc.recompute()

        geoList = []
        geoList.append(Part.LineSegment(Base.Vector(-21.762587,19.904083,0),Base.Vector(32.074337,19.904083,0)))
        geoList.append(Part.LineSegment(Base.Vector(32.074337,19.904083,0),Base.Vector(32.074337,-27.458027,0)))
        geoList.append(Part.LineSegment(Base.Vector(32.074337,-27.458027,0),Base.Vector(-21.762587,-27.458027,0)))
        geoList.append(Part.LineSegment(Base.Vector(-21.762587,-27.458027,0),Base.Vector(-21.762587,19.904083,0)))
        sketch.addGeometry(geoList,False)

        conList = []
        conList.append(Sketcher.Constraint('Coincident',0,2,1,1))
        conList.append(Sketcher.Constraint('Coincident',1,2,2,1))
        conList.append(Sketcher.Constraint('Coincident',2,2,3,1))
        conList.append(Sketcher.Constraint('Coincident',3,2,0,1))
        conList.append(Sketcher.Constraint('Horizontal',0))
        conList.append(Sketcher.Constraint('Horizontal',2))
        conList.append(Sketcher.Constraint('Vertical',1))
        conList.append(Sketcher.Constraint('Vertical',3))
        sketch.addConstraint(conList)
        del geoList, conList

        self.Doc.recompute()

        binder1 = body.newObject('PartDesign::SubShapeBinder','Binder')
        binder1.Support = sketch
        self.Doc.recompute()
        pad = body.newObject('PartDesign::Pad','Pad')
        pad.Profile = sketch
        pad.Length = 10
        self.Doc.recompute()
        pad.ReferenceAxis = (sketch,['N_Axis'])
        sketch.Visibility = False
        self.Doc.recompute()

        binder2 = body.newObject('PartDesign::SubShapeBinder','Binder001')
        binder2.Support = [pad, "Sketch."]
        self.Doc.recompute()

        self.assertAlmostEqual(binder1.Shape.BoundBox.XLength, binder2.Shape.BoundBox.XLength, 2)
        self.assertAlmostEqual(binder1.Shape.BoundBox.YLength, binder2.Shape.BoundBox.YLength, 2)
        self.assertAlmostEqual(binder1.Shape.BoundBox.ZLength, binder2.Shape.BoundBox.ZLength, 2)

        nor1 = binder1.Shape.Face1.normalAt(0,0)
        nor2 = binder2.Shape.Face1.normalAt(0,0)
        self.assertAlmostEqual(nor1.getAngle(nor2), 0.0, 2)
