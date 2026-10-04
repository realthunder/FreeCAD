# SPDX-License-Identifier: LGPL-2.1-or-later

# A body's physical material, and the material of what a Part operation
# makes (upstream 0804d80ebf).

import unittest

import FreeCAD


class TestBodyMaterial(unittest.TestCase):
    def setUp(self):
        import Materials
        self.Doc = FreeCAD.newDocument("PartDesignTestBodyMaterial")
        materials = Materials.MaterialManager().Materials
        self.steel = [m for m in materials.values() if m.Name == "Steel-Generic"][0]

    def testBodyAndFeaturesShareMaterial(self):
        """A body's material reaches its features, a feature added later
        takes it, and a feature's reaches the body; each stayed "Default"."""
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        sketch = body.newObject("Sketcher::SketchObject", "Sketch")
        self.Doc.recompute()
        body.ShapeMaterial = self.steel
        self.Doc.recompute()
        self.assertEqual(box.ShapeMaterial.UUID, self.steel.UUID)
        # a sketch has no use for it
        self.assertNotEqual(sketch.ShapeMaterial.UUID, self.steel.UUID)
        cylinder = body.newObject("PartDesign::SubtractiveCylinder", "Cylinder")
        self.Doc.recompute()
        self.assertEqual(cylinder.ShapeMaterial.UUID, self.steel.UUID)

        other = self.Doc.addObject("PartDesign::Body", "Other")
        feature = other.newObject("PartDesign::AdditiveBox", "Box")
        self.Doc.recompute()
        feature.ShapeMaterial = self.steel
        self.assertEqual(other.ShapeMaterial.UUID, self.steel.UUID)

    def testPartOperationsTakeTheBaseMaterial(self):
        """A fillet, a chamfer, a mirror, a boolean, a fuse, a common and a
        compound of a steel box are steel, unless given their own."""
        box = self.Doc.addObject("Part::Box", "Box")
        box.ShapeMaterial = self.steel
        tool = self.Doc.addObject("Part::Box", "Tool")
        tool.Placement.Base = FreeCAD.Vector(5, 0, 0)
        self.Doc.recompute()
        made = []
        fillet = self.Doc.addObject("Part::Fillet", "Fillet")
        fillet.Base = box
        fillet.Edges = [(1, 1.0, 1.0)]
        made.append(fillet)
        chamfer = self.Doc.addObject("Part::Chamfer", "Chamfer")
        chamfer.Base = box
        chamfer.Edges = [(1, 1.0, 1.0)]
        made.append(chamfer)
        mirror = self.Doc.addObject("Part::Mirroring", "Mirror")
        mirror.Source = box
        made.append(mirror)
        cut = self.Doc.addObject("Part::Cut", "Cut")
        cut.Base = box
        cut.Tool = tool
        made.append(cut)
        for kind in ("Part::MultiFuse", "Part::MultiCommon"):
            multi = self.Doc.addObject(kind, "Multi")
            multi.Shapes = [box, tool]
            made.append(multi)
        compound = self.Doc.addObject("Part::Compound", "Compound")
        compound.Links = [box, tool]
        made.append(compound)
        self.Doc.recompute()
        for obj in made:
            self.assertEqual(obj.ShapeMaterial.UUID, self.steel.UUID, obj.TypeId)

    def tearDown(self):
        FreeCAD.closeDocument("PartDesignTestBodyMaterial")
