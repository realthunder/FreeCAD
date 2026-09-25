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

class TestDatumPoint(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestDatumPoint")

    def testOriginDatumPoint(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.DatumPoint = self.Doc.addObject('PartDesign::Point','DatumPoint')
        self.DatumPoint.Support = [(self.Doc.XY_Plane,'')]
        self.DatumPoint.MapMode = 'ObjectOrigin'
        self.Body.addObject(self.DatumPoint)
        self.Doc.recompute()
        self.assertEqual(self.DatumPoint.AttachmentOffset.Base, App.Vector(0))

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestDatumPoint")
        #print ("omit closing document for debugging")

class TestDatumLine(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestDatumLine")

    def testXAxisDatumLine(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.DatumLine = self.Doc.addObject('PartDesign::Line','DatumLine')
        self.DatumLine.Support = [(self.Doc.XY_Plane,'')]
        self.DatumLine.MapMode = 'ObjectX'
        self.Body.addObject(self.DatumLine)
        self.Doc.recompute()
        self.assertNotIn('Invalid', self.DatumLine.State)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestDatumLine")
        #print ("omit closing document for debugging")

class TestDatumPlane(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestDatumPlane")

    def testXYDatumPlane(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.DatumPlane = self.Doc.addObject('PartDesign::Plane','DatumPlane')
        self.DatumPlane.Support = [(self.Doc.XY_Plane,'')]
        self.DatumPlane.MapMode = 'FlatFace'
        self.Body.addObject(self.DatumPlane)
        self.Doc.recompute()
        self.DatumPlaneNormal = self.DatumPlane.Shape.Surface.Axis
        self.assertEqual(abs(self.DatumPlaneNormal.dot(App.Vector(0,0,1))), 1)

    def testNearlyPlanarBSplineFace(self):
        """A sketch or a datum plane sits on a B-spline face that strays from
        its plane by less than 2e-7 -- OCCT's default flatness test, 1e-7,
        refused it (upstream eebb7f7829, issue 21242). A face that is really
        curved is still refused."""
        import Part

        def face(dz):
            # one pole of a flat 4 x 4 net raised by dz; the fitted plane
            # takes most of it, which leaves the face 1e-7..2e-7 off plane
            # at dz = 2e-6
            surf = Part.BSplineSurface()
            poles = [[App.Vector(10 * i, 10 * j, 0) for j in range(4)] for i in range(4)]
            poles[1][1] = App.Vector(10, 10, dz)
            surf.buildFromPolesMultsKnots(poles, [4, 4], [4, 4], [0, 1], [0, 1],
                                          False, False, 3, 3)
            return surf.toShape()

        near = self.Doc.addObject('Part::Feature', 'Near')
        near.Shape = face(2e-6)
        self.assertFalse(near.Shape.Faces[0].isPlanarFace(1e-7))
        self.assertTrue(near.Shape.Faces[0].isPlanarFace(2e-7))
        bent = self.Doc.addObject('Part::Feature', 'Bent')
        bent.Shape = face(1e-3)

        self.Body = self.Doc.addObject('PartDesign::Body', 'Body')
        for support, ok in ((near, True), (bent, False)):
            sketch = self.Doc.addObject('Sketcher::SketchObject', 'Sketch')
            plane = self.Doc.addObject('PartDesign::Plane', 'DatumPlane')
            for obj in (sketch, plane):
                self.Body.addObject(obj)
                obj.AttachmentSupport = [(support, 'Face1')]
                obj.MapMode = 'FlatFace'
            self.Doc.recompute()
            for obj in (sketch, plane):
                self.assertEqual('Invalid' not in obj.State, ok,
                                 '%s on %s: %s' % (obj.Name, support.Name, obj.State))
            if ok:
                self.assertAlmostEqual(abs(sketch.Placement.Rotation.multVec(
                    App.Vector(0, 0, 1)).z), 1.0, places=6)
            for obj in (sketch, plane):
                self.Body.removeObject(obj)
                self.Doc.removeObject(obj.Name)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestDatumPlane")
        #print ("omit closing document for debugging")

