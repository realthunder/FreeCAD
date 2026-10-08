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

    def testAnObjectAddedWithItsProxyIsGivenALookToo(self):
        # Document.addObject(attach=True), which Assembly3 and Draft's link
        # arrays make their objects with, calls no setupObject(): the look
        # is given all the same
        class Proxy:
            def attach(self, obj):
                self.attached = obj.Name

            def execute(self, obj):
                pass

        plain = self.doc.addObject("Part::FeaturePython", "Plain")
        proxy = Proxy()
        made = self.doc.addObject("Part::FeaturePython", "Made", proxy, None, True)
        self.assertEqual(proxy.attached, made.Name)
        for name in ("Face", "Edge", "Vertex"):
            self.assertEqual(
                getattr(made.ElementAppearance, name).DiffuseColor,
                getattr(plain.ElementAppearance, name).DiffuseColor,
                name,
            )
        self.assertEqual(made.ShapeColor, plain.ShapeColor)
        self.assertEqual(made.ElementAppearance.Faces.Count, 1)

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


def rgb(color):
    return tuple(round(v, 3) for v in color[:3])


class ElementAppearanceNamesTest(unittest.TestCase):
    """The looks by the names they have always had, on the object: names over
    ElementAppearance, written with no view provider anywhere
    (docs/ShapeAppearanceDesign.md sec 14.6.1)."""

    def setUp(self):
        self.doc = App.newDocument("ElementAppearanceNames")
        self.doc.UndoMode = 1
        self.box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def testTheNamesSayWhatTheObjectHas(self):
        box, ea = self.box, self.box.ElementAppearance
        for name in ("ShapeAppearance", "ShapeColor", "Transparency", "LineColor", "PointColor"):
            self.assertEqual(box.getGroupOfProperty(name), "Appearances")
        self.assertEqual(box.getGroupOfProperty("ShapeMaterial"), "Appearances")
        self.assertEqual(rgb(box.ShapeColor), rgb(ea.Face.DiffuseColor))
        self.assertEqual(rgb(box.LineColor), rgb(ea.Edge.DiffuseColor))
        self.assertEqual(rgb(box.PointColor), rgb(ea.Vertex.DiffuseColor))
        self.assertEqual(box.Transparency, 0)
        self.assertEqual(rgb(box.ShapeAppearance.Base.DiffuseColor), rgb(ea.Face.DiffuseColor))
        # A look stated by any other way is what the names say after
        ea.Face = material(RED)
        ea.Edge = material(GREEN)
        self.assertEqual(rgb(box.ShapeColor), RED[:3])
        self.assertEqual(rgb(box.LineColor), GREEN[:3])
        self.assertEqual(rgb(box.ShapeAppearance.Base.DiffuseColor), RED[:3])

    def testAWriteToANameIsAWriteToTheObject(self):
        box, ea = self.box, self.box.ElementAppearance
        box.ShapeColor = GREEN[:3]
        self.assertEqual(rgb(ea.Face.DiffuseColor), GREEN[:3])
        box.Transparency = 40
        self.assertAlmostEqual(ea.Face.Transparency, 0.4, places=3)
        # The colour and what is seen through it are two things
        self.assertEqual(rgb(box.ShapeColor), GREEN[:3])
        box.ShapeColor = BLUE[:3]
        self.assertEqual(box.Transparency, 40)
        box.LineColor = RED[:3]
        box.PointColor = GREEN[:3]
        self.assertEqual(rgb(ea.Edge.DiffuseColor), RED[:3])
        self.assertEqual(rgb(ea.Vertex.DiffuseColor), GREEN[:3])
        # Of the object, and of no element; and no shape to make again
        self.assertEqual(ea.keys(), [])
        self.assertFalse("Touched" in box.State)

    def testWhatWasMadeFromItFollows(self):
        cyl = self.doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius = 2
        cyl.Height = 30
        cyl.Placement.Base = App.Vector(5, 5, -10)
        cut = self.doc.addObject("Part::Cut", "Cut")
        cut.Base = self.box
        cut.Tool = cyl
        cut.MapFaceColor = True
        self.doc.recompute()
        self.box.ShapeColor = RED[:3]
        cyl.ShapeColor = BLUE[:3]
        self.assertEqual(colors(cut), sorted([RED[:3], BLUE[:3]]))
        self.assertEqual(cut.ShapeAppearance.Count, len(cut.Shape.Faces))
        self.assertNotIn(rgb(cut.ShapeColor), (RED[:3], BLUE[:3]))

    def testAFaceWrittenThroughTheListIsStated(self):
        box, ea = self.box, self.box.ElementAppearance
        was = rgb(box.ShapeColor)
        # An object nobody painted draws one look: a face is given another
        # by a list that has one for each
        faces = box.ShapeAppearance.copy()
        faces.setSize(6)
        faces[2] = material(RED)
        box.ShapeAppearance = faces
        self.assertEqual(len(ea.keys()), 1)
        self.assertEqual(rgb(drawn(box)[2].DiffuseColor), RED[:3])
        self.assertEqual(box.ShapeAppearance.Count, 6)
        self.assertEqual(rgb(box.ShapeAppearance[2].DiffuseColor), RED[:3])
        # The object is the colour it was, and so is every other face
        self.assertEqual(rgb(box.ShapeColor), was)
        self.assertEqual(colors(box), sorted([was, RED[:3]]))
        # Given the object's look again, the face states nothing
        box.ShapeAppearance[2] = ea.Face
        self.assertEqual(ea.keys(), [])

    def testTheNamesAreInNoFile(self):
        import zipfile

        self.box.ShapeColor = RED[:3]
        self.box.Transparency = 30
        self.box.LineColor = BLUE[:3]
        folder = tempfile.mkdtemp(prefix="fc-ea-")
        path = os.path.join(folder, "names.FCStd")
        self.doc.saveAs(path)
        with zipfile.ZipFile(path) as z:
            xml = z.read("Document.xml").decode("utf-8")
        self.assertIn('name="ElementAppearance"', xml)
        for name in ("ShapeAppearance", "ShapeColor", "Transparency", "LineColor", "PointColor"):
            self.assertNotIn('name="%s"' % name, xml)
        App.closeDocument(self.doc.Name)
        self.doc = App.openDocument(path)
        box = self.doc.getObject("Box")
        self.assertEqual(rgb(box.ShapeColor), RED[:3])
        self.assertEqual(box.Transparency, 30)
        self.assertEqual(rgb(box.LineColor), BLUE[:3])

    def testAnUndoTakesAWriteToANameBack(self):
        box, ea = self.box, self.box.ElementAppearance
        was = rgb(box.ShapeColor)
        self.doc.openTransaction("red")
        box.ShapeColor = RED[:3]
        box.Transparency = 50
        self.doc.commitTransaction()
        self.doc.undo()
        self.assertEqual(rgb(ea.Face.DiffuseColor), was)
        self.assertEqual(rgb(box.ShapeColor), was)
        self.assertEqual(box.Transparency, 0)
        self.doc.redo()
        self.assertEqual(rgb(box.ShapeColor), RED[:3])
        self.assertEqual(box.Transparency, 50)


class ElementAppearanceLinkTest(unittest.TestCase):
    """What a link lays over what it shows, held by the link and written
    with no view provider anywhere (docs/ShapeAppearanceDesign.md sec 14.6.4)."""

    def setUp(self):
        self.doc = App.newDocument("ElementAppearanceLink")
        self.doc.UndoMode = 1
        self.box = self.doc.addObject("Part::Box", "Box")
        self.link = self.doc.addObject("App::Link", "Link")
        self.link.LinkedObject = self.box
        self.link.Placement.Base = App.Vector(20, 0, 0)
        self.doc.recompute()

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def give(self, color):
        self.link.OverrideMaterial = True
        look = self.link.ShapeAppearance.Base
        look.DiffuseColor = color
        self.link.ShapeAppearance.Base = look

    def testALinkGivesNoLookUntilItIsGivenOne(self):
        link, ea = self.link, self.link.ElementAppearance
        self.assertFalse(link.OverrideMaterial)
        self.assertNotIn("Face", ea)
        self.assertEqual(ea.keys(), [])
        self.assertIsNone(link.ColoredElements)
        for name in ("ElementAppearance", "OverrideMaterial", "ShapeAppearance"):
            self.assertEqual(link.getGroupOfProperty(name), "Appearances")

    def testTheOverrideIsTheLinksOwnLook(self):
        link, ea = self.link, self.link.ElementAppearance
        self.give(RED)
        self.assertIn("Face", ea)
        self.assertEqual(rgb(ea.Face.DiffuseColor), RED[:3])
        # Given to the store, the names say it
        ea.Face = material(GREEN)
        self.assertTrue(link.OverrideMaterial)
        self.assertEqual(rgb(link.ShapeAppearance.Base.DiffuseColor), GREEN[:3])
        link.OverrideMaterial = False
        self.assertNotIn("Face", ea)
        # ... and the look it had is the one it has again
        link.OverrideMaterial = True
        self.assertEqual(rgb(ea.Face.DiffuseColor), GREEN[:3])

    def testElementsAreNamedByTheirPaths(self):
        link, ea = self.link, self.link.ElementAppearance
        ea["Face6"] = RED
        ea.setLook("Face1", material(BLUE))
        self.assertEqual(ea.keys(), ["Face6", "Face1"])
        self.assertEqual(list(link.ColoredElements[1]), ["Face6", "Face1"])
        self.assertTrue(ea.isNamed("Face6"))
        self.assertIn("Face1", ea)
        self.assertNotIn("Face2", ea)
        self.assertEqual(rgb(ea["Face6"].DiffuseColor), RED[:3])
        # A colour is a look whose own field is the colour; a material is all its own
        self.assertLess(len(ea.own("Face6")), len(ea.own("Face1")))
        # Never by number: a link has no shape to count
        with self.assertRaises(Exception):
            ea[5] = GREEN
        del ea["Face1"]
        self.assertEqual(list(link.ColoredElements[1]), ["Face6"])
        # The names written as they have always been are names that state nothing
        link.ColoredElements = (link, ["Face6", "Face2"])
        self.assertEqual(ea.keys(), ["Face6", "Face2"])
        self.assertEqual(ea.own("Face2"), ())
        self.assertEqual(rgb(ea["Face6"].DiffuseColor), RED[:3])
        link.ColoredElements = None
        self.assertEqual(ea.keys(), [])

    def testWhatIsMadeFromALinkTakesWhatItLaysOver(self):
        cyl = self.doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius = 2
        cyl.Height = 30
        cyl.Placement.Base = App.Vector(25, 5, -10)
        cut = self.doc.addObject("Part::Cut", "Cut")
        cut.Base = self.link
        cut.Tool = cyl
        cut.MapFaceColor = True
        self.doc.recompute()
        self.box.ShapeColor = RED[:3]
        self.assertIn(RED[:3], colors(cut))
        # No recompute, and nothing asked of a view provider
        self.give(GREEN)
        self.assertFalse("Touched" in cut.State)
        self.assertIn(GREEN[:3], colors(cut))
        self.assertNotIn(RED[:3], colors(cut))
        self.link.OverrideMaterial = False
        self.assertIn(RED[:3], colors(cut))
        self.assertNotIn(GREEN[:3], colors(cut))

    def testALookFollowsItsFaceThroughAShapeMadeAgain(self):
        cyl = self.doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius = 2
        cyl.Height = 30
        cyl.Placement.Base = App.Vector(5, 5, -40)
        cut = self.doc.addObject("Part::Cut", "Cut")
        cut.Base = self.box
        cut.Tool = cyl
        self.link.LinkedObject = cut
        self.doc.recompute()
        top = face(cut, ZMin=10)
        ea = self.link.ElementAppearance
        ea.setLook(top, material(GREEN))
        # Drilled through: seven faces, and the top is another by number.
        # ColoredElements is told of that as the store is, and the name it
        # then says is not a face given no look
        cyl.Placement.Base = App.Vector(5, 5, -10)
        self.doc.recompute()
        self.assertEqual(len(cut.Shape.Faces), 7)
        self.assertNotEqual(face(cut, ZMin=10), top)
        top = face(cut, ZMin=10)
        self.assertEqual(ea.keys(), [top])
        self.assertEqual(list(self.link.ColoredElements[1]), [top])
        self.assertEqual(rgb(ea[top].DiffuseColor), GREEN[:3])
        self.assertIn("Shininess", ea.own(top))

    def testTheLooksAreInTheFileAndTheirNamesAreNot(self):
        import zipfile

        self.give(RED)
        self.link.ElementAppearance["Face6"] = BLUE
        folder = tempfile.mkdtemp(prefix="fc-ea-")
        path = os.path.join(folder, "link.FCStd")
        self.doc.saveAs(path)
        with zipfile.ZipFile(path) as z:
            xml = z.read("Document.xml").decode("utf-8")
        for name in ("OverrideMaterial", "ShapeAppearance"):
            self.assertNotIn('name="%s"' % name, xml)
        # What upstream knows a link by is a name over the store too, and in
        # no file this build alone reads; written for upstream, at schema 4,
        # as it always was
        self.assertNotIn('name="ColoredElements"', xml)
        self.doc.SaveSchemaVersion = 4
        four = os.path.join(folder, "link4.FCStd")
        self.doc.saveAs(four)
        with zipfile.ZipFile(four) as z:
            self.assertIn('name="ColoredElements"', z.read("Document.xml").decode("utf-8"))
        App.closeDocument(self.doc.Name)
        self.doc = App.openDocument(path)
        link = self.doc.getObject("Link")
        ea = link.ElementAppearance
        self.assertTrue(link.OverrideMaterial)
        self.assertEqual(rgb(ea.Face.DiffuseColor), RED[:3])
        self.assertEqual(rgb(link.ShapeAppearance.Base.DiffuseColor), RED[:3])
        self.assertEqual(ea.keys(), ["Face6"])
        self.assertEqual(rgb(ea["Face6"].DiffuseColor), BLUE[:3])
        self.assertEqual(list(link.ColoredElements[1]), ["Face6"])

    def testTheLookALinkWouldGiveIsInTheFile(self):
        link, ea = self.link, self.link.ElementAppearance
        nobodys = rgb(link.ShapeAppearance.Base.DiffuseColor)
        # Given a look and then none: the one it gave is the one it would give
        self.give(GREEN)
        link.OverrideMaterial = False
        self.assertNotIn("Face", ea)
        self.assertEqual(rgb(link.ShapeAppearance.Base.DiffuseColor), GREEN[:3])
        # ... and so is one it is given while it gives none, which is no look
        # given: nothing made from the link takes it
        self.doc.openTransaction("a look to give")
        look = link.ShapeAppearance.Base
        look.DiffuseColor = BLUE
        link.ShapeAppearance.Base = look
        self.doc.commitTransaction()
        self.assertFalse(link.OverrideMaterial)
        self.assertNotIn("Face", ea)
        self.assertEqual(ea.keys(), [])
        self.assertEqual(rgb(link.ShapeAppearance.Base.DiffuseColor), BLUE[:3])
        self.doc.undo()
        self.assertEqual(rgb(link.ShapeAppearance.Base.DiffuseColor), GREEN[:3])
        self.doc.redo()
        self.assertEqual(rgb(link.ShapeAppearance.Base.DiffuseColor), BLUE[:3])
        # In the file whatever its schema: at 5 no name is written, and the
        # look was in nothing else
        folder = tempfile.mkdtemp(prefix="fc-ea-")
        paths = []
        for schema in (5, 4):
            self.doc.SaveSchemaVersion = schema
            paths.append(os.path.join(folder, "kept%d.FCStd" % schema))
            self.doc.saveAs(paths[-1])
        for path in paths:
            App.closeDocument(self.doc.Name)
            self.doc = App.openDocument(path)
            link = self.doc.getObject("Link")
            ea = link.ElementAppearance
            self.assertFalse(link.OverrideMaterial, path)
            self.assertNotIn("Face", ea, path)
            self.assertEqual(rgb(link.ShapeAppearance.Base.DiffuseColor), BLUE[:3], path)
            link.OverrideMaterial = True
            self.assertEqual(rgb(ea.Face.DiffuseColor), BLUE[:3], path)
        # Everything let go is the look nobody chose again
        link.ElementAppearance = None
        self.assertFalse(link.OverrideMaterial)
        self.assertEqual(rgb(link.ShapeAppearance.Base.DiffuseColor), nobodys)

    def testAnUndoTakesALookBack(self):
        link, ea = self.link, self.link.ElementAppearance
        self.doc.openTransaction("looks")
        self.give(RED)
        ea["Face6"] = BLUE
        self.doc.commitTransaction()
        self.doc.undo()
        self.assertFalse(link.OverrideMaterial)
        self.assertNotIn("Face", ea)
        self.assertEqual(ea.keys(), [])
        self.assertIsNone(link.ColoredElements)
        self.doc.redo()
        self.assertTrue(link.OverrideMaterial)
        self.assertEqual(rgb(ea.Face.DiffuseColor), RED[:3])
        self.assertEqual(list(link.ColoredElements[1]), ["Face6"])

    def testAPartHoldsItsLooksAsALinkDoes(self):
        part = self.doc.addObject("App::Part", "Part")
        part.addObject(self.box)
        ea = part.ElementAppearance
        self.assertFalse(part.OverrideMaterial)
        part.OverrideMaterial = True
        self.assertIn("Face", ea)
        ea["Box.Face6"] = GREEN
        self.assertEqual(ea.keys(), ["Box.Face6"])
        self.assertEqual(list(part.ColoredElements[1]), ["Box.Face6"])
        part.OverrideMaterial = False
        self.assertNotIn("Face", ea)
        self.assertEqual(ea.keys(), ["Box.Face6"])


class ElementAppearanceFileTest(unittest.TestCase):
    """The looks through a STEP file, written and read with no view provider
    (docs/ShapeAppearanceDesign.md sec 14.6.7)."""

    def setUp(self):
        try:
            import Import  # noqa: F401
        except ImportError:
            self.skipTest("no Import module")
        self.doc = App.newDocument("ElementAppearanceFile")
        self.read = None

    def tearDown(self):
        App.closeDocument(self.doc.Name)
        if self.read:
            App.closeDocument(self.read.Name)

    def through(self, objs, name):
        import Import

        path = os.path.join(tempfile.mkdtemp(prefix="fc-ea-"), name)
        Import.export(objs, path)
        self.read = App.newDocument("ElementAppearanceRead")
        Import.insert(path, self.read.Name)
        self.read.recompute()
        return [o for o in self.read.Objects if o.isDerivedFrom("Part::Feature")]

    def testAShapesColoursGoThroughAStepFile(self):
        box = self.doc.addObject("Part::Box", "Box")
        cyl = self.doc.addObject("Part::Cylinder", "Cyl")
        cyl.Placement.Base = App.Vector(30, 0, 0)
        self.doc.recompute()
        box.ShapeColor = RED[:3]
        faces = box.ShapeAppearance.copy()
        faces.setSize(6)
        faces[2] = material(BLUE)
        box.ShapeAppearance = faces
        cyl.ShapeColor = GREEN[:3]
        cyl.LineColor = BLUE[:3]
        parts = self.through([box, cyl], "shapes.step")
        self.assertEqual(len(parts), 2)
        looks = sorted(colors(p) for p in parts)
        self.assertEqual(looks, sorted([sorted([RED[:3], BLUE[:3]]), [GREEN[:3]]]))
        for part in parts:
            if colors(part) == [GREEN[:3]]:
                self.assertEqual(rgb(part.ShapeColor), GREEN[:3])
                self.assertEqual(rgb(part.LineColor), BLUE[:3])
            else:
                # The object is red and one face of it is blue
                self.assertEqual(rgb(part.ShapeColor), RED[:3])
                self.assertEqual(len(part.ElementAppearance.keys()), 1)

    def testWhatALinkLaysOverGoesThroughAStepFile(self):
        box = self.doc.addObject("Part::Box", "Box")
        link = self.doc.addObject("App::Link", "Link")
        link.LinkedObject = box
        link.Placement.Base = App.Vector(30, 0, 0)
        self.doc.recompute()
        box.ShapeColor = RED[:3]
        link.OverrideMaterial = True
        look = link.ShapeAppearance.Base
        look.DiffuseColor = GREEN
        link.ShapeAppearance.Base = look
        self.through([box, link], "link.step")
        seen = set()
        for obj in self.read.Objects:
            if obj.isDerivedFrom("Part::Feature"):
                seen.add(rgb(obj.ShapeColor))
            elif obj.isDerivedFrom("App::Link") and obj.OverrideMaterial:
                seen.add(rgb(obj.ElementAppearance.Face.DiffuseColor))
        self.assertIn(RED[:3], seen)
        self.assertIn(GREEN[:3], seen)


def face(obj, **at):
    """The face of the object whose bounding box is so, as the shape counts it."""
    for i, f in enumerate(obj.Shape.Faces):
        if all(abs(getattr(f.BoundBox, k) - v) < 1e-6 for k, v in at.items()):
            return "Face%d" % (i + 1)
    return None


class ElementAppearanceMergeTest(unittest.TestCase):
    """The looks given on two branches of the transaction log, merged by what
    they are given to (docs/ShapeAppearanceDesign.md sec 14.6.5)."""

    def setUp(self):
        self.setting = App.ParamGet("User parameter:BaseApp/Preferences/Document")
        self.log = self.setting.GetInt("TransactionLog", 2)
        self.setting.SetInt("TransactionLog", 2)
        self.doc = App.newDocument("ElementAppearanceMerge")
        self.doc.UndoMode = 1
        self.doc.openTransaction("base")
        self.box = self.doc.addObject("Part::Box", "Box")
        self.cyl = self.doc.addObject("Part::Cylinder", "Cyl")
        self.cyl.Radius = 2
        self.cyl.Height = 30
        self.cyl.Placement.Base = App.Vector(5, 5, -40)
        self.cut = self.doc.addObject("Part::Cut", "Cut")
        self.cut.Base = self.box
        self.cut.Tool = self.cyl
        self.doc.recompute()
        self.doc.commitTransaction()
        self.doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-ea-merge-"), "merge.FCStd"))

    def tearDown(self):
        App.closeDocument(self.doc.Name)
        self.setting.SetInt("TransactionLog", self.log)

    def paint(self, name, obj, element, look):
        self.doc.openTransaction(name)
        obj.ElementAppearance[element] = look
        self.doc.commitTransaction()

    def stated(self, obj):
        ea = obj.ElementAppearance
        return {
            k: (tuple(round(v, 3) for v in m.DiffuseColor[:3]), round(m.Shininess, 3))
            for k, m in ea.items()
        }

    def testFacesNamedOnBothBranches(self):
        doc, cut = self.doc, self.cut
        top, side = face(cut, ZMin=10), face(cut, XMin=10)
        doc.createTransactionBranch("side")
        self.paint("side: a material", cut, side, material(RED, 0.25))
        doc.switchTransactionBranch("main")
        self.assertEqual(cut.ElementAppearance.keys(), [])
        # Ours paints another face and drills through: seven faces where
        # theirs had six, and theirs' face is not the one it was by number
        doc.openTransaction("main: a colour, and drilled")
        cut.ElementAppearance[top] = GREEN
        self.cyl.Placement.Base = App.Vector(5, 5, -10)
        doc.recompute()
        doc.commitTransaction()
        self.assertEqual(len(cut.Shape.Faces), 7)
        preview = doc.previewTransactionMerge("side")
        self.assertEqual(preview["conflicts"], 0)
        merged = doc.mergeTransactionBranch("side")
        self.assertEqual((merged["unresolved"], merged["failed"]), ([], []))
        doc.recompute()
        ea = cut.ElementAppearance
        top, side = face(cut, ZMin=10), face(cut, XMin=10)
        self.assertEqual(sorted(ea.Names), sorted([top, side]))
        self.assertEqual(self.stated(cut)[side], (RED[:3], 0.25))
        self.assertEqual(self.stated(cut)[top][0], GREEN[:3])
        # Each with what of its look is its own
        self.assertEqual(ea.own(top), ("DiffuseColor",))
        self.assertIn("Shininess", ea.own(side))
        doc.undo()
        self.assertEqual(sorted(cut.ElementAppearance.Names), [face(cut, ZMin=10)])

    def testFacesNumberedOnBothBranches(self):
        # A box has no names for its faces: by number, a face at a time
        doc, box = self.doc, self.box
        top, bottom = face(box, ZMin=10), face(box, ZMax=0)
        doc.createTransactionBranch("side")
        self.paint("side: a material", box, top, material(RED, 0.25))
        doc.switchTransactionBranch("main")
        self.paint("main: a colour", box, bottom, GREEN)
        self.assertEqual(doc.previewTransactionMerge("side")["conflicts"], 0)
        merged = doc.mergeTransactionBranch("side")
        self.assertEqual((merged["unresolved"], merged["failed"]), ([], []))
        self.assertEqual(box.ElementAppearance.Names, ())
        stated = self.stated(box)
        self.assertEqual(stated[top], (RED[:3], 0.25))
        self.assertEqual(stated[bottom][0], GREEN[:3])

    def testOneFaceOnBothBranchesIsRuled(self):
        doc, cut = self.doc, self.cut
        top = face(cut, ZMin=10)
        doc.createTransactionBranch("side")
        self.paint("side: red", cut, top, RED)
        doc.switchTransactionBranch("main")
        self.paint("main: blue", cut, top, BLUE)
        try:
            self.setting.SetString("TransactionLogMergeFacePaint", "asked")
            self.assertEqual(doc.previewTransactionMerge("side")["conflicts"], 1)
            self.setting.SetString("TransactionLogMergeFacePaint", "theirs")
            self.assertEqual(doc.previewTransactionMerge("side")["conflicts"], 0)
            merged = doc.mergeTransactionBranch("side")
            self.assertEqual((merged["unresolved"], merged["failed"]), ([], []))
            self.assertEqual(self.stated(cut)[top][0], RED[:3])
        finally:
            self.setting.RemString("TransactionLogMergeFacePaint")

