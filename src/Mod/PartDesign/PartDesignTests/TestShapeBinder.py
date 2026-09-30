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
