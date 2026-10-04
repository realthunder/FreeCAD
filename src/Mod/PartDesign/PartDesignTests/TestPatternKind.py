# SPDX-License-Identifier: LGPL-2.1-or-later

"""A PartDesign pattern whose kind is changed: the inputs swap, a reference
the new kind needs is set, the label follows the kind, and a file names the
class of the kind the pattern has then.
"""

import os
import re
import tempfile
import unittest
import zipfile

import FreeCAD
import TestSketcherApp


class TestPatternKind(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPatternKind")
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        self.Body.addObject(self.Box)
        self.Box.Length = 10
        self.Box.Width = 10
        self.Box.Height = 10
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def makePattern(self, cls="PartDesign::LinearPattern"):
        pattern = self.Doc.addObject(cls, "Pattern")
        pattern.Originals = [self.Box]
        self.Body.addObject(pattern)
        return pattern

    def assertReference(self, link, obj, sub):
        self.assertIsNotNone(link)
        self.assertEqual(link[0], obj)
        self.assertEqual(list(link[1]), [sub])

    def savedType(self, path, name):
        with zipfile.ZipFile(path) as archive:
            xml = archive.read("Document.xml").decode("utf-8")
        match = re.search(r'<Object type="([^"]+)" name="%s"' % name, xml)
        return match.group(1) if match else None

    def testLabelFollowsTheKind(self):
        pattern = self.makePattern()
        self.assertEqual(pattern.Name, "Pattern")
        self.assertEqual(pattern.Label, "LinearPattern")
        pattern.PatternType = "Polar"
        self.assertEqual(pattern.Label, "PolarPattern")
        pattern.Label = "Bolt circle"
        pattern.PatternType = "Circular"
        self.assertEqual(pattern.Label, "Bolt circle")

    def testSwitchSwapsTheInputsAndSetsTheAxis(self):
        pattern = self.makePattern()
        pattern.Direction = (self.Doc.X_Axis, [""])
        pattern.Length = 90
        pattern.Occurrences = 10
        self.Doc.recompute()
        self.assertAlmostEqual(pattern.Shape.Volume, 1e4)

        pattern.PatternType = "Polar"
        self.assertFalse(hasattr(pattern, "Direction"))
        self.assertEqual(pattern.getTypeIdOfProperty("Offset"), "App::PropertyAngle")
        # Shared with the same type: kept
        self.assertEqual(pattern.Occurrences, 10)
        # The axis the new kind needs: the body's Z axis, as the command sets it
        self.assertIsNotNone(pattern.Axis)
        self.assertEqual(pattern.Axis[0].Role, "Z_Axis")
        pattern.Occurrences = 4
        self.Doc.recompute()
        self.assertEqual(pattern.getStatusString(), "Valid")
        # The box in each quadrant around Z
        self.assertAlmostEqual(pattern.Shape.Volume, 4000)

        # A path not picked yet: the box alone, and no copies left over
        pattern.PatternType = "Path"
        self.Doc.recompute()
        self.assertEqual(pattern.getStatusString(), "Valid")
        self.assertAlmostEqual(pattern.Shape.Volume, 1000)
        self.assertTrue(pattern.AddSubShape.isNull())

    def testSketchAxesForAPad(self):
        sketch = self.Doc.addObject("Sketcher::SketchObject", "Sketch")
        self.Body.addObject(sketch)
        TestSketcherApp.CreateRectangleSketch(sketch, (0, 0), (10, 10))
        self.Doc.recompute()
        pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(pad)
        pad.Profile = sketch
        pad.Length = 10
        self.Doc.recompute()
        pattern = self.Doc.addObject("PartDesign::PointPattern", "Pattern")
        pattern.Originals = [pad]
        self.Body.addObject(pattern)
        pattern.PatternType = "Linear"
        self.assertReference(pattern.Direction, sketch, "H_Axis")
        self.assertReference(pattern.Direction2, sketch, "V_Axis")
        pattern.PatternType = "Polar"
        self.assertReference(pattern.Axis, sketch, "N_Axis")

    def testSavedAsTheClassOfItsKind(self):
        pattern = self.makePattern()
        pattern.Direction = (self.Doc.X_Axis, [""])
        pattern.PatternType = "Polar"
        pattern.Occurrences = 4
        self.Doc.recompute()
        self.assertEqual(pattern.TypeId, "PartDesign::LinearPattern")
        with tempfile.TemporaryDirectory(prefix="freecad_pd_pattern_") as directory:
            path = os.path.join(directory, "pattern.FCStd")
            self.Doc.saveAs(path)
            self.assertEqual(self.savedType(path, "Pattern"), "PartDesign::PolarPattern")
            name = self.Doc.Name
            FreeCAD.closeDocument(name)
            self.Doc = FreeCAD.openDocument(path)
        pattern = self.Doc.getObject("Pattern")
        self.assertEqual(pattern.TypeId, "PartDesign::PolarPattern")
        self.assertEqual(pattern.PatternType, "Polar")
        self.assertEqual(pattern.Label, "PolarPattern")
        self.assertEqual(pattern.Occurrences, 4)
        pattern.touch()
        self.Doc.recompute()
        self.assertAlmostEqual(pattern.Shape.Volume, 4000)
        # And it may change again
        pattern.PatternType = "Linear"
        self.assertEqual(pattern.Label, "LinearPattern")
        self.assertTrue(hasattr(pattern, "Direction2"))

    def testUndoRedo(self):
        self.Doc.UndoMode = 1
        pattern = self.makePattern()
        pattern.Direction = (self.Doc.X_Axis, [""])
        pattern.Length = 90
        pattern.Occurrences = 10
        self.Doc.recompute()
        self.Doc.openTransaction("Change kind")
        pattern.PatternType = "Polar"
        pattern.Occurrences = 4
        self.Doc.commitTransaction()
        self.Doc.recompute()
        self.assertAlmostEqual(pattern.Shape.Volume, 4000)
        self.Doc.undo()
        self.assertEqual(pattern.PatternType, "Linear")
        self.assertEqual(pattern.Label, "LinearPattern")
        self.assertEqual(pattern.getTypeIdOfProperty("Offset"), "App::PropertyLength")
        self.assertEqual(pattern.Direction[0], self.Doc.X_Axis)
        self.Doc.recompute()
        self.assertAlmostEqual(pattern.Shape.Volume, 1e4)
        self.Doc.redo()
        self.assertEqual(pattern.PatternType, "Polar")
        self.assertEqual(pattern.Occurrences, 4)
        self.Doc.recompute()
        self.assertAlmostEqual(pattern.Shape.Volume, 4000)

    def testInsideAMultiTransform(self):
        multi = self.Doc.addObject("PartDesign::MultiTransform", "MultiTransform")
        multi.Originals = [self.Box]
        self.Body.addObject(multi)
        sub = self.Doc.addObject("PartDesign::LinearPattern", "Pattern")
        sub.Direction = (self.Doc.X_Axis, [""])
        self.Body.addObject(sub)
        multi.Transformations = [sub]
        sub.PatternType = "Polar"
        sub.Occurrences = 4
        self.Doc.recompute()
        self.assertEqual(multi.getStatusString(), "Valid")
        self.assertAlmostEqual(multi.Shape.Volume, 4000)
