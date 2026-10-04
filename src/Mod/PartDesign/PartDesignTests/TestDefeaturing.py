# SPDX-License-Identifier: LGPL-2.1-or-later

import unittest

import FreeCAD
import Part


class TestDefeaturing(unittest.TestCase):
    """PartDesign::Defeaturing (upstream c70d9b2992): the picked faces of the
    base go, and the solid is healed where they were."""

    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestDefeaturing")
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = self.Body.newObject("PartDesign::AdditiveBox", "Box")
        self.Box.Length = self.Box.Width = self.Box.Height = 10
        self.Doc.recompute()

    def faceWhere(self, feature, test):
        faces = [i for i, f in enumerate(feature.Shape.Faces, 1) if test(f)]
        self.assertEqual(len(faces), 1)
        return "Face%d" % faces[0]

    def testHole(self):
        cylinder = self.Body.newObject("PartDesign::SubtractiveCylinder", "Cylinder")
        cylinder.Radius = 2
        cylinder.Height = 10
        cylinder.Placement.Base = FreeCAD.Vector(5, 5, 0)
        self.Doc.recompute()
        self.assertLess(cylinder.Shape.Volume, 1000 - 100)
        side = self.faceWhere(cylinder, lambda f: f.Surface.TypeId == "Part::GeomCylinder")
        defeat = self.Body.newObject("PartDesign::Defeaturing", "Defeaturing")
        defeat.Base = (cylinder, [side])
        self.Doc.recompute()
        self.assertNotIn("Invalid", defeat.State)
        self.assertAlmostEqual(defeat.Shape.Volume, 1000, places=6)
        self.assertEqual(len(defeat.Shape.Faces), 6)
        # the element map carries through: the box's faces keep their history
        self.assertGreater(defeat.Shape.ElementMapSize, 0)
        self.assertEqual(self.Body.Tip, defeat)

    def testFillet(self):
        fillet = self.Body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (self.Box, ["Edge1"])
        fillet.Radius = 2
        self.Doc.recompute()
        self.assertLess(fillet.Shape.Volume, 1000)
        rounded = self.faceWhere(fillet, lambda f: f.Surface.TypeId == "Part::GeomCylinder")
        defeat = self.Body.newObject("PartDesign::Defeaturing", "Defeaturing")
        defeat.Base = (fillet, [rounded])
        self.Doc.recompute()
        self.assertNotIn("Invalid", defeat.State)
        self.assertAlmostEqual(defeat.Shape.Volume, 1000, places=6)

    def testNothingPicked(self):
        defeat = self.Body.newObject("PartDesign::Defeaturing", "Defeaturing")
        defeat.Base = (self.Box, [])
        self.Doc.recompute()
        self.assertNotIn("Invalid", defeat.State)
        self.assertAlmostEqual(defeat.Shape.Volume, 1000, places=6)

    def testFollowsTheBase(self):
        # parametric: the hole moves, the defeaturing still takes it off
        cylinder = self.Body.newObject("PartDesign::SubtractiveCylinder", "Cylinder")
        cylinder.Radius = 2
        cylinder.Height = 10
        cylinder.Placement.Base = FreeCAD.Vector(5, 5, 0)
        self.Doc.recompute()
        side = self.faceWhere(cylinder, lambda f: f.Surface.TypeId == "Part::GeomCylinder")
        defeat = self.Body.newObject("PartDesign::Defeaturing", "Defeaturing")
        defeat.Base = (cylinder, [side])
        self.Doc.recompute()
        cylinder.Placement.Base = FreeCAD.Vector(4, 6, 0)
        self.Box.Length = 12
        self.Doc.recompute()
        self.assertNotIn("Invalid", defeat.State)
        self.assertAlmostEqual(defeat.Shape.Volume, 1200, places=6)

    def testShapeDefeaturing(self):
        # Shape.defeaturing() maps its elements too
        shape = self.Box.Shape.copy()
        cyl = Part.makeCylinder(2, 10, FreeCAD.Vector(5, 5, 0))
        holed = shape.cut(cyl)
        side = [f for f in holed.Faces if f.Surface.TypeId == "Part::GeomCylinder"][0]
        healed = holed.defeaturing([side])
        self.assertAlmostEqual(healed.Volume, 1000, places=6)
        self.assertGreater(healed.ElementMapSize, 0)

    def tearDown(self):
        FreeCAD.closeDocument("PartDesignTestDefeaturing")
