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

class TestLinearPattern(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestLinearPattern")

    def testXAxisLinearPattern(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Body.addObject(self.Box)
        self.Box.Length=10.00
        self.Box.Width=10.00
        self.Box.Height=10.00
        self.Doc.recompute()
        self.LinearPattern = self.Doc.addObject("PartDesign::LinearPattern","LinearPattern")
        self.LinearPattern.Originals = [self.Box]
        self.LinearPattern.Direction = (self.Doc.X_Axis,[""])
        self.LinearPattern.Length = 90.0
        self.LinearPattern.Occurrences = 10
        self.Body.addObject(self.LinearPattern)
        self.Doc.recompute()
        self.assertAlmostEqual(self.LinearPattern.Shape.Volume, 1e4)

    def testYAxisLinearPattern(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Body.addObject(self.Box)
        self.Box.Length=10.00
        self.Box.Width=10.00
        self.Box.Height=10.00
        self.Doc.recompute()
        self.LinearPattern = self.Doc.addObject("PartDesign::LinearPattern","LinearPattern")
        self.LinearPattern.Originals = [self.Box]
        self.LinearPattern.Direction = (self.Doc.Y_Axis,[""])
        self.LinearPattern.Length = 90.0
        self.LinearPattern.Occurrences = 10
        self.Body.addObject(self.LinearPattern)
        self.Doc.recompute()
        self.assertAlmostEqual(self.LinearPattern.Shape.Volume, 1e4)

    def testZAxisLinearPattern(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Body.addObject(self.Box)
        self.Box.Length=10.00
        self.Box.Width=10.00
        self.Box.Height=10.00
        self.Doc.recompute()
        self.LinearPattern = self.Doc.addObject("PartDesign::LinearPattern","LinearPattern")
        self.LinearPattern.Originals = [self.Box]
        self.LinearPattern.Direction = (self.Doc.Z_Axis,[""])
        self.LinearPattern.Length = 90.0
        self.LinearPattern.Occurrences = 10
        self.Body.addObject(self.LinearPattern)
        self.Doc.recompute()
        self.assertAlmostEqual(self.LinearPattern.Shape.Volume, 1e4)

    def testNormalSketchAxisLinearPattern(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 10
        self.Doc.recompute()
        self.LinearPattern = self.Doc.addObject("PartDesign::LinearPattern","LinearPattern")
        self.LinearPattern.Originals = [self.Pad]
        self.LinearPattern.Direction = (self.PadSketch,["N_Axis"])
        self.LinearPattern.Length = 90.0
        self.LinearPattern.Occurrences = 10
        self.Body.addObject(self.LinearPattern)
        self.Doc.recompute()
        self.assertAlmostEqual(self.LinearPattern.Shape.Volume, 1e4)
        # the whole sketch, as a plane, is its normal too; it was an error
        self.LinearPattern.Direction = (self.PadSketch, [""])
        self.Doc.recompute()
        self.assertNotIn("Invalid", self.LinearPattern.State)
        self.assertAlmostEqual(self.LinearPattern.Shape.Volume, 1e4)
        self.assertAlmostEqual(self.LinearPattern.Shape.BoundBox.ZMax, 100)

    def testVerticalSketchAxisLinearPattern(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 10
        self.Doc.recompute()
        self.LinearPattern = self.Doc.addObject("PartDesign::LinearPattern","LinearPattern")
        self.LinearPattern.Originals = [self.Pad]
        self.LinearPattern.Direction = (self.PadSketch,["V_Axis"])
        self.LinearPattern.Length = 90.0
        self.LinearPattern.Occurrences = 10
        self.Body.addObject(self.LinearPattern)
        self.Doc.recompute()
        self.assertAlmostEqual(self.LinearPattern.Shape.Volume, 1e4)

    def testHorizontalSketchAxisLinearPattern(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.PadSketch = self.Doc.addObject('Sketcher::SketchObject', 'SketchPad')
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 10
        self.Doc.recompute()
        self.LinearPattern = self.Doc.addObject("PartDesign::LinearPattern","LinearPattern")
        self.LinearPattern.Originals = [self.Pad]
        self.LinearPattern.Direction = (self.PadSketch,["H_Axis"])
        self.LinearPattern.Length = 90.0
        self.LinearPattern.Occurrences = 10
        self.Body.addObject(self.LinearPattern)
        self.Doc.recompute()
        self.assertAlmostEqual(self.LinearPattern.Shape.Volume, 1e4)

    def testOccurrencesKeepLengthAndOffset(self):
        """Occurrences changes the gap count, so the derived one of Length and
        Offset follows it (upstream fa0702956c); one occurrence divided by
        zero."""
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = self.Body.newObject("PartDesign::AdditiveBox", "Box")
        pattern = self.Body.newObject("PartDesign::LinearPattern", "LinearPattern")
        pattern.Originals = [self.Box]
        pattern.Direction = ([f for f in self.Body.Origin.OriginFeatures
                              if f.Role == "X_Axis"][0], [""])
        pattern.Mode = "Extent"
        pattern.Length = 40
        pattern.Occurrences = 3
        self.assertAlmostEqual(pattern.Offset.Value, 20)
        pattern.Occurrences = 5
        self.assertAlmostEqual(pattern.Offset.Value, 10)
        pattern.Mode = "Spacing"
        pattern.Offset = 20
        self.assertAlmostEqual(pattern.Length.Value, 80)
        pattern.Occurrences = 2
        self.assertAlmostEqual(pattern.Length.Value, 20)
        pattern.Mode = "Extent"
        pattern.Occurrences = 1
        pattern.Length = 30
        self.assertAlmostEqual(pattern.Offset.Value, 30)
        self.Doc.recompute()
        self.assertIn("Up-to-date", pattern.State)

    def _boxPattern(self):
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Body.newObject("PartDesign::AdditiveBox", "Box")
        pattern = self.Body.newObject("PartDesign::LinearPattern", "LinearPattern")
        pattern.Originals = [box]
        axes = {f.Role: f for f in self.Body.Origin.OriginFeatures}
        pattern.Direction = (axes["X_Axis"], [""])
        return pattern, axes

    def _check(self, pattern, volume, xmax, ymax):
        self.Doc.recompute()
        self.assertIn("Up-to-date", pattern.State)
        self.assertAlmostEqual(pattern.Shape.Volume, volume)
        box = pattern.Shape.BoundBox
        self.assertAlmostEqual(box.XMax, xmax)
        self.assertAlmostEqual(box.YMax, ymax)

    def testTwoDirections(self):
        """A grid of Occurrences x Occurrences2, each direction with its own
        mode (upstream 5d2037c820)"""
        pattern, axes = self._boxPattern()
        pattern.Mode = "Extent"
        pattern.Length = 40
        pattern.Occurrences = 3
        self._check(pattern, 3000, 50, 10)
        pattern.Direction2 = (axes["Y_Axis"], [""])
        pattern.Mode2 = "Spacing"
        pattern.Offset2 = 20
        pattern.Occurrences2 = 2
        self.assertAlmostEqual(pattern.Length2.Value, 20)
        self._check(pattern, 6000, 50, 30)
        pattern.Reversed2 = True
        self.Doc.recompute()
        self.assertAlmostEqual(pattern.Shape.BoundBox.YMin, -20)
        # One occurrence leaves the second direction off, set or not
        pattern.Occurrences2 = 1
        pattern.Direction2 = None
        self._check(pattern, 3000, 50, 10)

    def testSpacings(self):
        """Individual spacings, then the spacing pattern, then Offset
        (upstream 5d2037c820)"""
        pattern, _ = self._boxPattern()
        pattern.Mode = "Spacing"
        pattern.Offset = 20
        pattern.Occurrences = 4
        # one item per gap, -1 for the gaps that follow Offset
        self.assertEqual(pattern.Spacings, [-1.0, -1.0, -1.0])
        self._check(pattern, 4000, 70, 10)
        pattern.Spacings = [-1, 30, -1]
        self._check(pattern, 4000, 80, 10)
        pattern.Occurrences = 5
        self.assertEqual(pattern.Spacings, [-1.0, 30.0, -1.0, -1.0])
        self._check(pattern, 5000, 100, 10)
        pattern.Spacings = []
        pattern.SpacingPattern = [15, 25]
        # a short list reads -1: 0, 15, 40, 55, 80
        self._check(pattern, 5000, 90, 10)
        pattern.Spacings = [-1, -1, 12]
        self._check(pattern, 5000, 87, 10)
        # The list grows to 1000 gaps at most; the gaps after read -1
        pattern.Occurrences = 5000
        self.assertEqual(len(pattern.Spacings), 1000)
        self.assertEqual(pattern.Spacings[:4], [-1.0, -1.0, 12.0, -1.0])
        pattern.Occurrences = 5
        self.assertEqual(pattern.Spacings, [-1.0, -1.0, 12.0, -1.0])
        # Extent mode ignores them all
        pattern.Mode = "Extent"
        pattern.Length = 40
        self._check(pattern, 5000, 50, 10)

    def testOriginPlaneDirection(self):
        """An origin plane is the direction of its normal (upstream
        cf0412b7e2); it was refused"""
        pattern, axes = self._boxPattern()
        pattern.Direction = (axes["XY_Plane"], [""])
        pattern.Length = 40
        pattern.Occurrences = 3
        self._check(pattern, 3000, 10, 10)
        self.assertAlmostEqual(pattern.Shape.BoundBox.ZMax, 50)

    def testOriginalPlacedApartFromSupport(self):
        # The original is turned and the support, a later feature, is moved:
        # the copies of the original must stay above it (upstream 5d8162107a;
        # the fork composed the two placements the other way round)
        body = self.Doc.addObject('PartDesign::Body', 'Body')
        box1 = body.newObject('PartDesign::AdditiveBox', 'Box1')
        box1.Length = 2
        box1.Width = 1
        box1.Height = 1
        box1.Placement = FreeCAD.Placement(
            FreeCAD.Vector(), FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 90))
        box2 = body.newObject('PartDesign::AdditiveBox', 'Box2')
        box2.Placement = FreeCAD.Placement(FreeCAD.Vector(50, 0, 0), FreeCAD.Rotation())
        self.Doc.recompute()
        pattern = body.newObject('PartDesign::LinearPattern', 'LinearPattern')
        pattern.Originals = [box1]
        pattern.Direction = (self.Doc.Z_Axis, [''])
        pattern.Length = 20
        pattern.Occurrences = 2
        self.Doc.recompute()
        self.assertEqual(pattern.getStatusString(), 'Valid')
        copies = [s.BoundBox for s in pattern.Shape.Solids if s.BoundBox.XMax < 10]
        self.assertEqual(len(copies), 2)
        for bound in copies:
            self.assertAlmostEqual(bound.XMin, -1)
            self.assertAlmostEqual(bound.YMin, 0)
            self.assertAlmostEqual(bound.YMax, 2)
        self.assertAlmostEqual(max(b.ZMax for b in copies), 21)

    def testOffsetFirstInstanceKeepsTheBaseInPlace(self):
        # TransformOffset moves the first instance by rewriting the history:
        # the support is the original's base, which must stay at its own
        # placement rather than take the pattern's
        body = self.Doc.addObject('PartDesign::Body', 'Body')
        plate = body.newObject('PartDesign::AdditiveBox', 'Plate')
        plate.Length = 4
        plate.Width = 4
        plate.Height = 1
        plate.Placement = FreeCAD.Placement(FreeCAD.Vector(-50, -50, 0), FreeCAD.Rotation())
        box = body.newObject('PartDesign::AdditiveBox', 'Box')
        box.Length = 2
        box.Width = 1
        box.Height = 1
        box.Placement = FreeCAD.Placement(FreeCAD.Vector(3, 4, 0), FreeCAD.Rotation())
        self.Doc.recompute()
        pattern = body.newObject('PartDesign::LinearPattern', 'LinearPattern')
        pattern.Originals = [box]
        pattern.Direction = (self.Doc.X_Axis, [''])
        pattern.Length = 20
        pattern.Occurrences = 2
        pattern.TransformOffset = FreeCAD.Placement(FreeCAD.Vector(0, 0, 10), FreeCAD.Rotation())
        self.Doc.recompute()
        self.assertEqual(pattern.getStatusString(), 'Valid')
        bounds = sorted((round(b.XMin, 6), round(b.YMin, 6), round(b.ZMin, 6))
                        for b in (s.BoundBox for s in pattern.Shape.Solids))
        self.assertEqual(bounds, [(-50, -50, 0), (3, 4, 10), (23, 4, 10)])

    def testUpstreamWholeShapeMode(self):
        # Upstream's TransformMode 1 ("Transform body", "Whole shape" since
        # 9535371265) patterns the whole shape and ignores the Originals,
        # which it leaves in the file. Saved as an index, read from either.
        import os
        import re
        import tempfile
        import zipfile
        body = self.Doc.addObject('PartDesign::Body', 'Body')
        box = body.newObject('PartDesign::AdditiveBox', 'Box')
        box.Length = box.Width = box.Height = 10
        cyl = body.newObject('PartDesign::AdditiveCylinder', 'Cyl')
        cyl.Radius = 2
        cyl.Height = 5
        cyl.Placement.Base = FreeCAD.Vector(5, 5, 10)
        pattern = body.newObject('PartDesign::LinearPattern', 'Pattern')
        pattern.Originals = [cyl]
        pattern.Direction = (self.Doc.X_Axis, [''])
        pattern.Length = 30
        pattern.Occurrences = 2
        self.Doc.recompute()
        one = box.Shape.Volume + cyl.AddSubShape.Volume
        self.assertAlmostEqual(pattern.Shape.Volume, one + cyl.AddSubShape.Volume, 6)

        tmp = tempfile.gettempdir()
        src = os.path.join(tmp, 'PDWholeShapeFork.FCStd')
        dst = os.path.join(tmp, 'PDWholeShapeUpstream.FCStd')
        self.Doc.saveAs(src)
        with zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as zo:
            for item in zi.infolist():
                data = zi.read(item.filename)
                if item.filename == 'Document.xml':
                    xml = data.decode('utf-8')
                    m = re.search(r'<Object name="Pattern"[^>]*>\s*<Properties Count="(\d+)', xml)
                    xml = xml[:m.start(1)] + str(int(m.group(1)) + 1) + xml[m.end(1):]
                    i = xml.index('<Property ', m.end())
                    xml = (xml[:i] + '<Property name="TransformMode" '
                           'type="App::PropertyEnumeration">\n<Integer value="1"/>\n'
                           '</Property>\n' + xml[i:])
                    data = xml.encode('utf-8')
                zo.writestr(item, data)
        doc = FreeCAD.openDocument(dst)
        try:
            pattern = doc.getObject('Pattern')
            self.assertEqual(pattern.Originals, [])
            self.assertFalse(pattern.SubTransform)
            doc.recompute()
            self.assertTrue(pattern.isValid())
            self.assertAlmostEqual(pattern.Shape.Volume, 2 * one, 6)
        finally:
            FreeCAD.closeDocument(doc.Name)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestLinearPattern")
        # print ("omit closing document for debugging")

