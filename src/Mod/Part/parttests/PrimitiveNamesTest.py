# SPDX-License-Identifier: LGPL-2.1-or-later

"""The names of a primitive's elements, by what each is to the primitive.

Part::PrimitiveNames: a box, a cylinder and the rest give every face, edge
and vertex a mapped name that says what it is -- `Top`, `Lateral`, `Start` --
so that a reference follows the element when the primitive counts its
elements another way.
"""

import unittest

import FreeCAD as App


def names(obj, kind):
    shape = obj.Shape
    count = {"Face": len(shape.Faces), "Edge": len(shape.Edges), "Vertex": len(shape.Vertexes)}
    return [shape.getElementMappedName("%s%d" % (kind, i)) for i in range(1, count[kind] + 1)]


def every(obj):
    return names(obj, "Face") + names(obj, "Edge") + names(obj, "Vertex")


def indexed(obj, name):
    """The element the name is of, as the shape counts it: `Face2`."""
    return obj.Shape.ElementMap[name]


def face(obj, name):
    """The face the name is of, as a shape."""
    return obj.Shape.getElement(indexed(obj, name))


class PrimitiveNamesTest(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("PrimitiveNames")

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def make(self, kind, **values):
        obj = self.doc.addObject(kind, kind.split("::")[1])
        for name, value in values.items():
            setattr(obj, name, value)
        self.doc.recompute()
        return obj

    def change(self, obj, **values):
        for name, value in values.items():
            setattr(obj, name, value)
        self.doc.recompute()

    def assertAllNamedApart(self, obj):
        got = every(obj)
        self.assertNotIn("", got)
        self.assertEqual(len(set(got)), len(got), got)
        self.assertFalse([n for n in got if n.startswith("Other")], got)

    def testABoxIsNamedAsTheViewsAre(self):
        box = self.make("Part::Box", Length=10, Width=20, Height=30)
        self.assertEqual(
            sorted(names(box, "Face")), ["Bottom", "Front", "Left", "Rear", "Right", "Top"]
        )
        for name, at in (
            ("Left", ("XMax", 0)),
            ("Right", ("XMin", 10)),
            ("Front", ("YMax", 0)),
            ("Rear", ("YMin", 20)),
            ("Bottom", ("ZMax", 0)),
            ("Top", ("ZMin", 30)),
        ):
            self.assertAlmostEqual(getattr(face(box, name).BoundBox, at[0]), at[1], msg=name)
        # An edge by the two faces it is between, a vertex by the three that meet
        self.assertIn("Front_Top", names(box, "Edge"))
        self.assertIn("Bottom_Front_Left_Corner", names(box, "Vertex"))
        self.assertEqual(len(names(box, "Edge")), 12)
        self.assertAllNamedApart(box)

    def testACylinderCutOpenKeepsWhatItHad(self):
        cyl = self.make("Part::Cylinder", Radius=2, Height=10)
        self.assertEqual(sorted(names(cyl, "Face")), ["Bottom", "Lateral", "Top"])
        self.assertIn("Lateral_Seam", names(cyl, "Edge"))
        top = face(cyl, "Top").BoundBox.ZMin
        self.change(cyl, Angle=90)
        self.assertEqual(sorted(names(cyl, "Face")), ["Bottom", "End", "Lateral", "Start", "Top"])
        self.assertAlmostEqual(face(cyl, "Top").BoundBox.ZMin, top)
        self.assertEqual(face(cyl, "Lateral").Surface.TypeId, "Part::GeomCylinder")
        # The side at the angle nought is in the XZ plane, on +X
        start = face(cyl, "Start").BoundBox
        self.assertAlmostEqual(start.YMax, 0)
        self.assertAlmostEqual(start.XMax, 2)
        self.assertNotIn("Lateral_Seam", names(cyl, "Edge"))
        self.assertAllNamedApart(cyl)
        # Pushed askew, the sides are told by the edge on the floor
        self.change(cyl, Angle=180, FirstAngle=30, SecondAngle=10)
        self.assertEqual(sorted(names(cyl, "Face")), ["Bottom", "End", "Lateral", "Start", "Top"])
        self.assertAllNamedApart(cyl)

    def testAConeThatLosesAFaceRenamesNone(self):
        cone = self.make("Part::Cone", Radius1=2, Radius2=4, Height=10)
        self.assertEqual(sorted(names(cone, "Face")), ["Bottom", "Lateral", "Top"])
        self.change(cone, Radius1=0)
        # Two faces where there were three: by number the top was another
        self.assertEqual(sorted(names(cone, "Face")), ["Lateral", "Top"])
        self.assertAlmostEqual(face(cone, "Top").BoundBox.ZMin, 10)
        self.assertIn("BottomPole", names(cone, "Vertex"))
        self.assertIn("Lateral_BottomPole", names(cone, "Edge"))
        self.assertAllNamedApart(cone)

    def testABallsPolesAreOfTheirEnds(self):
        ball = self.make("Part::Sphere", Radius=5)
        self.assertEqual(names(ball, "Face"), ["Lateral"])
        self.assertEqual(sorted(names(ball, "Vertex")), ["BottomPole", "TopPole"])
        self.assertEqual(
            sorted(names(ball, "Edge")), ["Lateral_BottomPole", "Lateral_Seam", "Lateral_TopPole"]
        )
        # A cap at the bottom leaves the pole at the top the one it was
        self.change(ball, Angle1=-30)
        self.assertEqual(sorted(names(ball, "Face")), ["Bottom", "Lateral"])
        self.assertIn("TopPole", names(ball, "Vertex"))
        self.assertNotIn("BottomPole", names(ball, "Vertex"))
        self.change(ball, Angle2=45, Angle3=90)
        self.assertEqual(sorted(names(ball, "Face")), ["Bottom", "End", "Lateral", "Start", "Top"])
        self.assertAllNamedApart(ball)

    def testATorusAndWhatClosesItsTube(self):
        torus = self.make("Part::Torus", Radius1=10, Radius2=2)
        self.assertEqual(names(torus, "Face"), ["Lateral"])
        self.assertAllNamedApart(torus)
        self.change(torus, Angle3=90)
        self.assertEqual(sorted(names(torus, "Face")), ["End", "Lateral", "Start"])
        for first, last in ((0, 180), (-90, 120), (-180, 90)):
            self.change(torus, Angle1=first, Angle2=last, Angle3=360)
            self.assertEqual(
                sorted(names(torus, "Face")), ["Lateral", "TubeEnd", "TubeStart"], (first, last)
            )
            self.assertAllNamedApart(torus)
        self.change(torus, Angle1=-90, Angle2=120, Angle3=90)
        self.assertEqual(
            sorted(names(torus, "Face")), ["End", "Lateral", "Start", "TubeEnd", "TubeStart"]
        )
        self.assertAllNamedApart(torus)

    def testAPrismsSidesFromTheCornerOnX(self):
        prism = self.make("Part::Prism", Polygon=6, Circumradius=2, Height=10)
        self.assertEqual(
            sorted(names(prism, "Face")), ["Bottom"] + ["Side%d" % i for i in range(1, 7)] + ["Top"]
        )
        # The first side is the one counter-clockwise from the corner on +X
        first = face(prism, "Side1").CenterOfMass
        self.assertGreater(first.x, 0)
        self.assertGreater(first.y, 0)
        self.assertAllNamedApart(prism)

    def testAWedgeAndWhereItClosesToARidge(self):
        wedge = self.make("Part::Wedge")
        self.assertEqual(
            sorted(names(wedge, "Face")), ["Bottom", "Front", "Left", "Rear", "Right", "Top"]
        )
        self.change(wedge, X2min=5, X2max=5)
        self.assertEqual(sorted(names(wedge, "Face")), ["Bottom", "Front", "Left", "Right", "Top"])
        self.assertIn("Left_Right", names(wedge, "Edge"))
        self.assertAllNamedApart(wedge)

    def testAnEllipsoidAndItsHalves(self):
        ellipsoid = self.make("Part::Ellipsoid")
        self.assertEqual(sorted(names(ellipsoid, "Vertex")), ["BottomPole", "TopPole"])
        self.assertAllNamedApart(ellipsoid)
        self.change(ellipsoid, Angle1=-30, Angle2=45, Angle3=90)
        self.assertEqual(
            sorted(names(ellipsoid, "Face")), ["Bottom", "End", "Lateral", "Start", "Top"]
        )
        self.assertAllNamedApart(ellipsoid)

    def testWhatHasNoFaces(self):
        plane = self.make("Part::Plane")
        self.assertEqual(names(plane, "Face"), ["Plane"])
        self.assertEqual(sorted(names(plane, "Edge")), ["Front", "Left", "Rear", "Right"])
        self.assertEqual(
            sorted(names(plane, "Vertex")), ["Front_Left", "Front_Right", "Left_Rear", "Rear_Right"]
        )
        line = self.make("Part::Line")
        self.assertEqual((names(line, "Edge"), names(line, "Vertex")), (["Line"], ["Start", "End"]))
        self.assertEqual(names(self.make("Part::Vertex"), "Vertex"), ["Point"])
        circle = self.make("Part::Circle")
        self.assertEqual((names(circle, "Edge"), names(circle, "Vertex")), (["Circle"], ["Start"]))
        self.change(circle, Angle1=0, Angle2=90)
        self.assertEqual(names(circle, "Vertex"), ["Start", "End"])
        self.assertEqual(names(self.make("Part::Ellipse"), "Edge"), ["Ellipse"])
        polygon = self.make("Part::RegularPolygon", Polygon=5)
        self.assertEqual(sorted(names(polygon, "Edge")), ["Side%d" % i for i in range(1, 6)])
        self.assertEqual(sorted(names(polygon, "Vertex")), ["Corner%d" % i for i in range(1, 6)])
        helix = self.make("Part::Helix", SegmentLength=1)
        self.assertEqual(names(helix, "Edge")[0], "Segment1")
        self.assertEqual(sorted(set(names(helix, "Vertex")) & {"Start", "End"}), ["End", "Start"])
        for obj in (plane, line, circle, polygon, helix, self.make("Part::Spiral")):
            self.assertAllNamedApart(obj)

    def testTheNamesAreInTheFile(self):
        import os
        import tempfile

        made = {}
        for kind, values in (
            ("Part::Box", {}),
            ("Part::Cylinder", {"Angle": 90}),
            ("Part::Sphere", {}),
            ("Part::Prism", {}),
            ("Part::Torus", {"Angle1": -90, "Angle2": 120}),
            ("Part::Ellipsoid", {}),
            ("Part::Plane", {}),
            ("Part::RegularPolygon", {}),
            ("Part::Helix", {"SegmentLength": 1}),
        ):
            obj = self.make(kind, **values)
            made[obj.Name] = every(obj)
        path = os.path.join(tempfile.mkdtemp(prefix="fc-prim-"), "names.FCStd")
        self.doc.saveAs(path)
        App.closeDocument(self.doc.Name)
        self.doc = App.openDocument(path)
        for name, names in made.items():
            obj = self.doc.getObject(name)
            self.assertEqual(every(obj), names, name)
            # Read with its names, a primitive asks for nothing
            self.assertNotIn("Touched", obj.State, name)

    def testWhatIsMadeFromAPrimitiveIsNamedByItsRoles(self):
        box = self.make("Part::Box")
        cyl = self.make("Part::Cylinder", Radius=2, Height=30)
        cyl.Placement.Base = App.Vector(5, 5, -10)
        cut = self.doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = box, cyl
        self.doc.recompute()
        made = names(cut, "Face")
        tag = ";:H%x" % box.ID
        self.assertIn("Left" + tag + ",F", made)
        self.assertTrue([n for n in made if n.startswith("Top;:M;CUT")], made)
        self.assertTrue([n for n in made if n.startswith("Lateral;:M;CUT;:H%x" % cyl.ID)], made)
        self.assertFalse([n for n in made if n.startswith("Face")], made)

    def testAReferenceFollowsTheFaceAndNotItsNumber(self):
        cyl = self.make("Part::Cylinder", Radius=2, Height=10)
        top = indexed(cyl, "Top")
        plane = self.make("Part::Plane")
        support = "AttachmentSupport" if hasattr(plane, "AttachmentSupport") else "Support"
        setattr(plane, support, [(cyl, top)])
        plane.MapMode = "FlatFace"
        self.doc.recompute()
        self.assertAlmostEqual(plane.Placement.Base.z, 10)
        # Cut open, the cylinder has two faces more and its top is another
        # by number: what had that number is a side now
        self.change(cyl, Angle=90)
        self.assertNotEqual(indexed(cyl, "Top"), top)
        self.assertEqual(getattr(plane, support)[0][1][0], indexed(cyl, "Top"))
        self.assertAlmostEqual(plane.Placement.Base.z, 10)
        self.assertAlmostEqual(abs(plane.Placement.Rotation.multVec(App.Vector(0, 0, 1)).z), 1)
        self.assertNotIn("Invalid", plane.State)


class PrimitiveNamesBodyTest(unittest.TestCase):
    """PartDesign's primitives, named as Part's are: first in a body their
    names are the shape's, after another feature they are what the result's
    names are made from."""

    def setUp(self):
        self.doc = App.newDocument("PrimitiveNamesBody")
        self.body = self.doc.addObject("PartDesign::Body", "Body")

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def testEveryPrimitiveFirstInABody(self):
        expect = {
            "Box": ["Bottom", "Front", "Left", "Rear", "Right", "Top"],
            "Cylinder": ["Bottom", "Lateral", "Top"],
            "Sphere": ["Lateral"],
            "Cone": ["Bottom", "Lateral", "Top"],
            "Torus": ["Lateral"],
            "Prism": ["Bottom"] + ["Side%d" % i for i in range(1, 7)] + ["Top"],
            "Wedge": ["Bottom", "Front", "Left", "Rear", "Right", "Top"],
        }
        for kind, faces in expect.items():
            body = self.doc.addObject("PartDesign::Body", "Body" + kind)
            feat = body.newObject("PartDesign::Additive" + kind, kind)
            self.doc.recompute()
            self.assertEqual(sorted(names(feat, "Face")), faces, kind)
            got = every(feat)
            self.assertEqual(len(set(got)), len(got), kind)

    def testAfterAnotherFeatureTheResultIsNamedFromTheRoles(self):
        box = self.body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 30, 30, 2
        cyl = self.body.newObject("PartDesign::AdditiveCylinder", "Cyl")
        cyl.Radius, cyl.Height = 3, 20
        cyl.Placement.Base = App.Vector(15, 15, 0)
        self.doc.recompute()
        made = names(cyl, "Face")
        # The box before it by its id, the cylinder's own by its id negated
        self.assertIn("Left;:H%x,F" % box.ID, made)
        self.assertIn("Top;:H-%x,F" % cyl.ID, made)
        self.assertTrue([n for n in made if n.startswith("Lateral;:M;FUS;:H-%x" % cyl.ID)], made)
        self.assertFalse([n for n in made if n.startswith("Face")], made)
