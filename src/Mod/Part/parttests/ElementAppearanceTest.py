# SPDX-License-Identifier: LGPL-2.1-or-later

"""What a shape's faces look like, made by the object.

docs/ShapeAppearanceDesign.md sec 14.6: the looks of an object's elements
are a value of the object (ElementAppearance) and are made by the object,
with a view provider and without. These run where there is none.
"""

import os
import tempfile
import unittest

import FreeCAD as App
import Part

RED = (1.0, 0.0, 0.0, 1.0)
GREEN = (0.0, 1.0, 0.0, 1.0)
BLUE = (0.0, 0.0, 1.0, 1.0)


def material(color, shininess=0.25):
    mat = App.Material()
    mat.DiffuseColor = color
    mat.Shininess = shininess
    return mat


def drawn(obj):
    """The look of each face of the object, as the object draws it."""
    ea = obj.ElementAppearance
    faces = ea.Faces
    count = len(obj.Shape.Faces)
    if faces.Count == 0:
        return [ea.Face] * count
    if faces.Count == 1:
        return [faces.Base] * count
    return [faces[i] for i in range(count)]


def colors(obj):
    return sorted({tuple(round(v, 3) for v in m.DiffuseColor[:3]) for m in drawn(obj)})


class ElementAppearanceMadeTest(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("ElementAppearanceMade")
        self.box = self.doc.addObject("Part::Box", "Box")
        self.cyl = self.doc.addObject("Part::Cylinder", "Cyl")
        self.cyl.Radius = 2
        self.cyl.Height = 30
        self.cyl.Placement.Base = App.Vector(5, 5, -10)
        self.cut = self.doc.addObject("Part::Cut", "Cut")
        self.cut.Base = self.box
        self.cut.Tool = self.cyl
        for obj in (self.box, self.cyl, self.cut):
            obj.MapFaceColor = True
            obj.MapTransparency = False
        self.doc.recompute()

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def testANewObjectIsGivenALookAndKeepsNoMore(self):
        for obj in (self.box, self.cut):
            ea = obj.ElementAppearance
            self.assertEqual(ea.keys(), [])
            # Its own look and nothing made of it: the preference's colours,
            # with no view provider to give them
            self.assertEqual(ea.Faces.Count, 1)
            self.assertEqual(ea.Edges.Count, 1)
            self.assertNotEqual(
                tuple(round(v, 3) for v in ea.Face.DiffuseColor[:3]),
                tuple(round(v, 3) for v in App.Material().DiffuseColor[:3]),
            )
            self.assertEqual(obj.getGroupOfProperty("ElementAppearance"), "Appearances")
            self.assertEqual(obj.getGroupOfProperty("MapFaceColor"), "Appearances")

    def testAFaceTakesTheColourOfTheFaceItWasMadeFrom(self):
        self.box.ElementAppearance.Face = material(RED)
        self.cyl.ElementAppearance.Face = material(BLUE)
        # No recompute: what was made from them is told
        self.assertFalse("Touched" in self.cut.State)
        self.assertEqual(colors(self.cut), sorted([RED[:3], BLUE[:3]]))
        self.assertEqual(self.cut.ElementAppearance.keys(), [])
        # Through a recompute that changes the faces
        self.cyl.Radius = 3
        self.doc.recompute()
        self.assertEqual(colors(self.cut), sorted([RED[:3], BLUE[:3]]))
        self.cut.MapFaceColor = False
        self.assertEqual(len(colors(self.cut)), 1)
        self.assertNotIn(RED[:3], colors(self.cut))

    def testWhatIsDrawnIsInTheFile(self):
        self.box.ElementAppearance.Face = material(RED)
        self.cyl.ElementAppearance.Face = material(BLUE)
        self.cut.ElementAppearance["Face1"] = GREEN
        was = [tuple(round(v, 3) for v in m.DiffuseColor) for m in drawn(self.cut)]
        self.assertIn(GREEN, was)
        folder = tempfile.mkdtemp(prefix="fc-ea-")
        path = os.path.join(folder, "made.FCStd")
        self.doc.saveAs(path)
        App.closeDocument(self.doc.Name)
        self.doc = App.openDocument(path)
        cut = self.doc.getObject("Cut")
        self.assertEqual(cut.ElementAppearance.Faces.Count, len(cut.Shape.Faces))
        self.assertEqual([tuple(round(v, 3) for v in m.DiffuseColor) for m in drawn(cut)], was)
        self.assertEqual(len(cut.ElementAppearance.keys()), 1)

    def testACopyMadeOnceStatesWhatItTakes(self):
        self.box.ElementAppearance.Face = material(RED)
        self.cyl.ElementAppearance.Face = material(BLUE)
        copy = Part.show(self.cut)
        self.assertEqual(colors(copy), sorted([RED[:3], BLUE[:3]]))
        self.assertGreater(len(copy.ElementAppearance.keys()), 0)
        # Stated, so it is the copy's whatever comes of the box
        self.box.ElementAppearance.Face = material(GREEN)
        self.assertIn(GREEN[:3], colors(self.cut))
        self.assertEqual(colors(copy), sorted([RED[:3], BLUE[:3]]))

    def testTheOwnLookTakesTheMaterialCard(self):
        try:
            import Materials
        except ImportError:
            self.skipTest("no Materials module")
        manager = Materials.MaterialManager()
        ea = self.box.ElementAppearance

        def look():
            m = ea.Face
            return (tuple(round(v, 3) for v in m.DiffuseColor), round(m.Shininess, 3))

        was = look()
        cards = []
        for uuid in manager.Materials:
            try:
                self.box.ShapeMaterial = manager.getMaterial(uuid)
            except Exception:
                continue
            if look() != was and look() not in [c[1] for c in cards]:
                cards.append((uuid, look()))
                was = look()
            if len(cards) == 2:
                break
        if len(cards) < 2:
            self.skipTest("fewer than two material cards that say something of a look")
        # A card given: the own look is the card's, and the next card's
        self.assertEqual(look(), cards[1][1])
        self.box.ShapeMaterial = manager.getMaterial(cards[0][0])
        self.assertEqual(look(), cards[0][1])
        # A look somebody chose ends that
        ea.Face = material(RED)
        self.box.ShapeMaterial = manager.getMaterial(cards[1][0])
        self.assertEqual(look()[0], RED)
