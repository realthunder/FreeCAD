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
import os
import tempfile
import unittest
import zipfile

import FreeCAD
import Part
import Sketcher
import TestSketcherApp

App = FreeCAD

class TestPipe(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPipe")

    def testSimpleAdditivePipeCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.ProfileSketch = self.Doc.addObject('Sketcher::SketchObject', 'ProfileSketch')
        self.Body.addObject(self.ProfileSketch)
        TestSketcherApp.CreateCircleSketch(self.ProfileSketch, (0, 0), 1)
        self.Doc.recompute()
        self.SpineSketch = self.Doc.addObject('Sketcher::SketchObject', 'SpineSketch')
        self.Body.addObject(self.SpineSketch)
        self.SpineSketch.MapMode = 'FlatFace'
        self.SpineSketch.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        self.SpineSketch.addGeometry(Part.LineSegment(App.Vector(0.0,0.0,0),App.Vector(0,1,0)),False)
        self.SpineSketch.addConstraint(Sketcher.Constraint('Coincident',0,1,-1,1))
        self.SpineSketch.addConstraint(Sketcher.Constraint('PointOnObject',0,2,-2))
        self.SpineSketch.addConstraint(Sketcher.Constraint('DistanceY',0,1,0,2,1))
        self.Doc.recompute()
        self.AdditivePipe = self.Doc.addObject("PartDesign::AdditivePipe","AdditivePipe")
        self.Body.addObject(self.AdditivePipe)
        self.AdditivePipe.Profile = self.ProfileSketch
        self.AdditivePipe.Spine = self.SpineSketch
        self.Doc.recompute()
        self.assertAlmostEqual(self.AdditivePipe.Shape.Volume, 3.14159265)

    def testSimpleSubtractivePipeCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.ProfileSketch = self.Doc.addObject('Sketcher::SketchObject', 'ProfileSketch')
        self.Body.addObject(self.ProfileSketch)
        TestSketcherApp.CreateCircleSketch(self.ProfileSketch, (0, 0), 1)
        self.Doc.recompute()
        self.SpineSketch = self.Doc.addObject('Sketcher::SketchObject', 'SpineSketch')
        self.Body.addObject(self.SpineSketch)
        self.SpineSketch.MapMode = 'FlatFace'
        self.SpineSketch.Support = (self.Doc.XZ_Plane, [''])
        self.Doc.recompute()
        self.SpineSketch.addGeometry(Part.LineSegment(App.Vector(0.0,0.0,0),App.Vector(0,1,0)),False)
        self.SpineSketch.addConstraint(Sketcher.Constraint('Coincident',0,1,-1,1))
        self.SpineSketch.addConstraint(Sketcher.Constraint('PointOnObject',0,2,-2))
        self.SpineSketch.addConstraint(Sketcher.Constraint('DistanceY',0,1,0,2,1))
        self.Doc.recompute()
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'PadSketch')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (-5, -5), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 1.0
        self.Doc.recompute()
        self.SubtractivePipe = self.Doc.addObject("PartDesign::SubtractivePipe","SubtractivePipe")
        self.Body.addObject(self.SubtractivePipe)
        self.SubtractivePipe.Profile = self.ProfileSketch
        self.SubtractivePipe.Spine = self.SpineSketch
        self.Doc.recompute()
        self.assertAlmostEqual(self.SubtractivePipe.Shape.Volume, 100 - 3.14159265)

    def testBinderSpine(self):
        # A spine given as a SubShapeBinder whose Shape was set by a script,
        # with no support to follow, sweeps as the sketch it copies (upstream
        # 422da33962)
        body = self.Doc.addObject('PartDesign::Body', 'BinderSpineBody')
        profile = body.newObject('Sketcher::SketchObject', 'Circle')
        profile.AttachmentSupport = (self.Doc.XY_Plane, [''])
        profile.MapMode = 'FlatFace'
        profile.addGeometry(Part.Circle(FreeCAD.Vector(), FreeCAD.Vector(0, 0, 1), 0.5), False)
        line = body.newObject('Sketcher::SketchObject', 'Line')
        line.AttachmentSupport = (self.Doc.XZ_Plane, [''])
        line.MapMode = 'FlatFace'
        line.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(0, 1, 0)), False)
        self.Doc.recompute()
        volumes = []
        for name, spine in (('SketchSpine', line), ('BinderSpine', None)):
            if spine is None:
                spine = body.newObject('PartDesign::SubShapeBinder', 'Binder')
                spine.Shape = line.Shape
            pipe = body.newObject('PartDesign::AdditivePipe', name)
            pipe.Profile = profile
            pipe.Spine = spine
            self.Doc.recompute()
            self.assertNotIn('Invalid', pipe.State, name)
            volumes.append(pipe.AddSubShape.Volume)
        self.assertAlmostEqual(volumes[0], math.pi * 0.25, places=3)
        self.assertAlmostEqual(volumes[1], volumes[0], places=6)

    def testAuxiliarySpineNames(self):
        # The auxiliary spine properties were misspelled; upstream renamed
        # them (fa3c6e1068). A file under either spelling keeps its spine
        body = self.Doc.addObject('PartDesign::Body', 'AuxBody')
        profile = body.newObject('Sketcher::SketchObject', 'Profile')
        profile.addGeometry(Part.Circle(FreeCAD.Vector(), FreeCAD.Vector(0, 0, 1), 2), False)
        spine = body.newObject('Sketcher::SketchObject', 'Spine')
        spine.Placement = FreeCAD.Placement(
            FreeCAD.Vector(), FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), 90))
        spine.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(0, 30, 0)), False)
        aux = body.newObject('Sketcher::SketchObject', 'Aux')
        aux.Placement = spine.Placement
        aux.addGeometry(Part.LineSegment(FreeCAD.Vector(10, 0, 0), FreeCAD.Vector(20, 30, 0)), False)
        pipe = body.newObject('PartDesign::AdditivePipe', 'AuxPipe')
        pipe.Profile = profile
        pipe.Spine = (spine, ['Edge1'])
        pipe.Mode = 'Auxiliary'
        pipe.AuxiliarySpine = (aux, ['Edge1'])
        pipe.AuxiliaryCurvilinear = False
        self.Doc.recompute()
        self.assertNotIn('Invalid', pipe.State)
        volume = pipe.Shape.Volume
        tmp = tempfile.mkdtemp()
        path = os.path.join(tmp, 'auxspine.FCStd')
        # a copy: saveAs would rename the test's document
        self.Doc.saveCopy(path)
        # the same file under the old names
        oldpath = os.path.join(tmp, 'auxspine-old.FCStd')
        with zipfile.ZipFile(path) as zin, \
                zipfile.ZipFile(oldpath, 'w', zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                data = zin.read(item.filename)
                if item.filename == 'Document.xml':
                    self.assertIn(b'name="AuxiliarySpine"', data)
                    for new, old in ((b'AuxiliarySpine"', b'AuxillerySpine"'),
                                     (b'AuxiliarySpineTangent"', b'AuxillerySpineTangent"'),
                                     (b'AuxiliaryCurvilinear"', b'AuxilleryCurvelinear"')):
                        data = data.replace(b'name="' + new, b'name="' + old)
                zout.writestr(item, data)
        for f in (path, oldpath):
            doc = FreeCAD.openDocument(f)
            try:
                loaded = doc.getObject('AuxPipe')
                self.assertEqual(loaded.AuxiliarySpine[0].Name, 'Aux', f)
                self.assertFalse(loaded.AuxiliaryCurvilinear, f)
                loaded.touch()
                doc.recompute()
                self.assertNotIn('Invalid', loaded.State, f)
                self.assertAlmostEqual(loaded.Shape.Volume, volume, places=6)
            finally:
                FreeCAD.closeDocument(doc.Name)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestPipe")
        #print ("omit closing document for debugging")

