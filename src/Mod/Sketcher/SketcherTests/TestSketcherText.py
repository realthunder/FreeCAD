# SPDX-License-Identifier: LGPL-2.1-or-later

"""Text constraints: sketch geometry generated from a string and a font file.

A Text constraint is a group whose first element is a construction line, the
handle, and whose other elements are the curves the glyphs are made of. The
handle gives the text its position, its angle and either its height or its
width; moving the handle moves the whole text.
"""

import os
import unittest

import FreeCAD
import Part
import Sketcher

App = FreeCAD


def find_font():
    """A font file that is there on every platform: one FreeCAD ships itself."""
    candidates = [
        os.path.join(
            App.getResourceDir(), "Mod", "TechDraw", "Resources", "fonts", "osifont-lgpl3fe.ttf"
        ),
        os.path.join(
            App.getResourceDir(), "Mod", "TechDraw", "Resources", "fonts", "Y14.5-FreeCAD.ttf"
        ),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


class TestSketcherText(unittest.TestCase):
    def setUp(self):
        self.font = find_font()
        if self.font is None:
            self.skipTest("no bundled font file to render text with")
        self.Doc = App.newDocument("SketcherTextTest")
        self.sketch = self.Doc.addObject("Sketcher::SketchObject", "Sketch")

    def tearDown(self):
        App.closeDocument(self.Doc.Name)

    def addHandle(self, x0=0.0, y0=0.0, x1=0.0, y1=10.0):
        return self.sketch.addGeometry(
            Part.LineSegment(App.Vector(x0, y0, 0), App.Vector(x1, y1, 0)), True
        )

    def testTextGeneratesGeometry(self):
        handle = self.addHandle()
        index = self.sketch.addConstraint(Sketcher.Constraint("Text", [handle, 0], "Ab", self.font))
        self.Doc.recompute()
        # the constraint alone adds nothing: the geometry comes from setTextAndFont
        self.assertEqual(self.sketch.GeometryCount, 1)

        self.sketch.setTextAndFont(index, "Ab", self.font)
        self.Doc.recompute()
        self.assertGreater(self.sketch.GeometryCount, 1)

        # the handle line gives the height, the text starts at its start point
        box = self.sketch.Shape.BoundBox
        self.assertAlmostEqual(box.YMin, 0.0, places=5)
        self.assertAlmostEqual(box.YMax, 10.0, places=5)
        self.assertAlmostEqual(box.XMin, 0.0, places=5)
        self.assertGreater(box.XMax, 0.0)

    def testChangingTheTextReplacesTheGeometry(self):
        handle = self.addHandle()
        index = self.sketch.addConstraint(Sketcher.Constraint("Text", [handle, 0], "A", self.font))
        self.sketch.setTextAndFont(index, "A", self.font)
        self.Doc.recompute()
        short = self.sketch.GeometryCount
        shortWidth = self.sketch.Shape.BoundBox.XLength

        self.sketch.setTextAndFont(0, "AAAA", self.font)
        self.Doc.recompute()
        # one Text constraint still, wider, and the old glyph geometry is gone
        self.assertEqual([c.Type for c in self.sketch.Constraints], ["Text"])
        self.assertGreater(self.sketch.GeometryCount, short)
        self.assertGreater(self.sketch.Shape.BoundBox.XLength, shortWidth)

    def testWidthInsteadOfHeight(self):
        handle = self.addHandle(0.0, 0.0, 20.0, 0.0)
        index = self.sketch.addConstraint(Sketcher.Constraint("Text", [handle, 0], "Ab", self.font))
        self.sketch.setTextAndFont(index, "Ab", self.font, False)
        self.Doc.recompute()
        # the handle is 20 long and gives the width
        box = self.sketch.Shape.BoundBox
        self.assertAlmostEqual(box.XLength, 20.0, places=4)

    def testTheHandleMovesTheText(self):
        handle = self.addHandle()
        index = self.sketch.addConstraint(Sketcher.Constraint("Text", [handle, 0], "Ab", self.font))
        self.sketch.setTextAndFont(index, "Ab", self.font)
        self.Doc.recompute()
        before = self.sketch.Shape.BoundBox

        self.sketch.moveGeometries([(handle, 0)], App.Vector(25.0, 7.0, 0.0), 1)
        self.Doc.recompute()
        after = self.sketch.Shape.BoundBox

        self.assertAlmostEqual(after.XMin - before.XMin, 25.0, places=5)
        self.assertAlmostEqual(after.YMin - before.YMin, 7.0, places=5)
        self.assertAlmostEqual(after.XLength, before.XLength, places=5)

    def testTextSurvivesSaveAndRestore(self):
        import tempfile

        handle = self.addHandle()
        index = self.sketch.addConstraint(Sketcher.Constraint("Text", [handle, 0], "Ab", self.font))
        self.sketch.setTextAndFont(index, "Ab", self.font)
        self.Doc.recompute()
        count = self.sketch.GeometryCount

        path = os.path.join(tempfile.gettempdir(), "SketcherTextTest.FCStd")
        self.Doc.saveAs(path)
        App.closeDocument(self.Doc.Name)
        self.Doc = App.openDocument(path)

        sketch = self.Doc.Sketch
        self.assertEqual(sketch.GeometryCount, count)
        self.assertEqual([c.Type for c in sketch.Constraints], ["Text"])
        # the text is still editable, so the font and the string came back
        sketch.setTextAndFont(0, "Abc", self.font)
        self.Doc.recompute()
        self.assertGreater(sketch.GeometryCount, 1)
        os.remove(path)

    def testTextRejectsANonTextConstraint(self):
        self.addHandle()
        index = self.sketch.addConstraint(Sketcher.Constraint("Vertical", 0))
        with self.assertRaises(ValueError):
            self.sketch.setTextAndFont(index, "Ab", self.font)
        with self.assertRaises(ValueError):
            self.sketch.setTextAndFont(42, "Ab", self.font)
