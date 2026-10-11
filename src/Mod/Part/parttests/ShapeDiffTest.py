# -*- coding: utf-8 -*-
# Tests for Shape.diff(): what became of each element between two states of
# a shape (Part::ShapeDiff, docs/TransactionLog.md sec 32.6). The pairing is
# tested in C++ (tests/src/Mod/Part/App/ShapeDiff.cpp); this is what Python
# is given.

import collections
import os
import tempfile
import unittest

import FreeCAD as App
import Part


class ShapeDiffTest(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("ShapeDiffTest")
        self.box = self.doc.addObject("Part::Box", "Box")
        self.box.Length, self.box.Width, self.box.Height = 40, 20, 10
        self.first = self.cut(self.box, 10)
        self.last = self.cut(self.first, 30)
        self.doc.recompute()

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def cut(self, base, x):
        tool = self.doc.addObject("Part::Cylinder", "Tool")
        tool.Radius, tool.Height = 2, 100
        tool.Placement.Base = App.Vector(x, 10, -50)
        cut = self.doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = base, tool
        return cut

    def testTheSameShape(self):
        shape = self.last.Shape
        diff = shape.diff(shape)
        self.assertTrue(diff["same"])
        self.assertFalse(diff["moved"])
        self.assertNotIn("motion", diff)
        self.assertEqual(len(diff["Face"]), 8)
        self.assertEqual(diff["Face"][0], ("Face1", "Face1", "shape", "same"))
        self.assertNotIn("Edge", diff)
        self.assertNotIn("Vertex", diff)
        self.assertIn("names", diff["cost"])

    def testMoved(self):
        before = self.last.Shape
        self.last.Placement.Base = App.Vector(0, 0, 7)
        self.doc.recompute()
        diff = before.diff(self.last.Shape)
        self.assertFalse(diff["same"])
        self.assertTrue(diff["moved"])
        self.assertAlmostEqual(diff["motion"].A34, 7.0)
        self.assertEqual({e[3] for e in diff["Face"]}, {"moved"})

    def testAParameter(self):
        before = self.last.Shape
        self.box.Height = 12
        self.doc.recompute()
        diff = before.diff(self.last.Shape, edges=True, vertices=True)
        self.assertFalse(diff["same"])
        self.assertFalse(diff["moved"])
        what = collections.Counter(e[3] for e in diff["Face"])
        self.assertEqual(what, {"same": 1, "moved": 1, "changed": 6})
        self.assertEqual({e[2] for e in diff["Face"]}, {"name"})
        self.assertEqual(len(diff["Edge"]), len(before.Edges))
        self.assertEqual(len(diff["Vertex"]), len(before.Vertexes))

    def testAFeaturePutInEarly(self):
        before = self.last.Shape
        self.first.Base = self.cut(self.box, 20)
        self.doc.recompute()
        after = self.last.Shape
        diff = before.diff(after, geometry=False, around=False)
        added = [e for e in diff["Face"] if e[3] == "added"]
        self.assertEqual(len(added), 1)
        self.assertIsNone(added[0][0])
        self.assertEqual(added[0][2], "")
        # The one face only the later shape has is the new hole's wall
        wall = after.getElement(added[0][1])
        self.assertEqual(wall.Surface.TypeId, "Part::GeomCylinder")
        self.assertAlmostEqual(wall.Surface.Center.x, 20.0)
        by = collections.Counter(e[2] for e in diff["Face"])
        self.assertGreater(by["steps"], 0)
        self.assertEqual(by["name"] + by["steps"], 8)
        self.assertEqual([e for e in diff["Face"] if e[3] == "removed"], [])


class ShapeAtAStateTest(unittest.TestCase):
    """Part.getTransactionShape(): a shape the log holds, read with its names
    (docs/TransactionLog.md sec 32.7)."""

    def setUp(self):
        self.setting = App.ParamGet("User parameter:BaseApp/Preferences/Document")
        self.log = self.setting.GetInt("TransactionLog", 2)
        self.setting.SetInt("TransactionLog", 2)
        self.doc = App.newDocument("ShapeAtAState")
        self.doc.UndoMode = 1
        self.doc.openTransaction("base")
        self.box = self.doc.addObject("Part::Box", "Box")
        self.cyl = self.doc.addObject("Part::Cylinder", "Cyl")
        self.cyl.Radius, self.cyl.Height = 2, 30
        self.cyl.Placement.Base = App.Vector(5, 5, -10)
        self.cut = self.doc.addObject("Part::Cut", "Cut")
        self.cut.Base, self.cut.Tool = self.box, self.cyl
        self.doc.recompute()
        self.doc.commitTransaction()
        self.doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-sd-state-"), "state.FCStd"))

    def tearDown(self):
        App.closeDocument(self.doc.Name)
        self.setting.SetInt("TransactionLog", self.log)

    def edit(self, name, **values):
        self.doc.openTransaction(name)
        for prop, value in values.items():
            setattr(self.box, prop, value)
        self.doc.recompute()
        self.doc.commitTransaction()
        return self.cut.Shape.copy(), dict(self.cut.Shape.ElementReverseMap)

    def assertIsThatShape(self, read, shape, names):
        self.assertIsNotNone(read)
        self.assertEqual(dict(read.ElementReverseMap), names)
        self.assertEqual(read.Tag, self.cut.ID)
        self.assertAlmostEqual(read.Volume, shape.Volume)
        # One shape in one place, though not one TShape: a parse of its own
        self.assertTrue(shape.diff(read)["same"])

    def testTheSidesOfAMerge(self):
        doc, cut = self.doc, self.cut
        base = cut.Shape.copy(), dict(cut.Shape.ElementReverseMap)
        doc.createTransactionBranch("side")
        theirs = self.edit("side: longer", Length=14)
        doc.switchTransactionBranch("main")
        ours = self.edit("main: taller", Height=12)
        states = [(o.Name, tuple(o.State)) for o in doc.Objects]
        change = [
            c for c in doc.previewTransactionMerge("side")["changes"] if c["key"] == "Cut.Shape"
        ][0]
        self.assertEqual(change["kind"], "derived")
        self.assertEqual(change["cid"], cut.ID)
        self.assertIsThatShape(Part.getTransactionShape(doc, change["base"], cut.ID), *base)
        read = Part.getTransactionShape(doc, change["theirs"], cut.ID)
        self.assertIsThatShape(read, *theirs)
        # The document is as it was: nothing of it was read into
        self.assertEqual(states, [(o.Name, tuple(o.State)) for o in doc.Objects])
        self.assertAlmostEqual(cut.Shape.Volume, ours[0].Volume)
        # And the two sides against each other: every face is the same face
        diff = cut.Shape.diff(read)
        self.assertEqual({e[2] for e in diff["Face"]}, {"name"})
        self.assertIn("changed", {e[3] for e in diff["Face"]})

    def testARowsBeforeAndAfter(self):
        doc, cut = self.doc, self.cut
        before = cut.Shape.copy(), dict(cut.Shape.ElementReverseMap)
        after = self.edit("taller", Height=12)
        row = [r for r in doc.getTransactionLog() if r["name"] == "taller"][0]
        op = [
            o
            for o in doc.getTransactionOps(row["seq"])
            if o["prop"] == "Shape" and o["cid"] == cut.ID
        ][0]
        self.assertIsThatShape(Part.getTransactionShape(doc, op["before"], cut.ID), *before)
        self.assertIsThatShape(Part.getTransactionShape(doc, op["after"], cut.ID), *after)

    def testOneValueReadTwiceIsOneShape(self):
        doc, cut = self.doc, self.cut
        self.edit("taller", Height=12)
        row = [r for r in doc.getTransactionLog() if r["name"] == "taller"][0]
        op = [
            o
            for o in doc.getTransactionOps(row["seq"])
            if o["prop"] == "Shape" and o["cid"] == cut.ID
        ][0]
        one = Part.getTransactionShape(doc, op["before"], cut.ID)
        two = Part.getTransactionShape(doc, op["before"], cut.ID)
        self.assertTrue(one.isPartner(two))
        self.assertTrue(one.diff(two)["same"])

    def testWhatIsNoShapeIsNone(self):
        doc = self.doc
        self.assertIsNone(Part.getTransactionShape(doc, "0" * 40))
        self.assertIsNone(Part.getTransactionShape(doc, ""))
        self.edit("taller", Height=12)
        row = [r for r in doc.getTransactionLog() if r["name"] == "taller"][0]
        op = [o for o in doc.getTransactionOps(row["seq"]) if o["prop"] == "Height"][0]
        self.assertTrue(op["after"])
        self.assertIsNone(Part.getTransactionShape(doc, op["after"], self.box.ID))
        plain = App.newDocument("ShapeAtAStateNoLog")
        try:
            self.assertIsNone(Part.getTransactionShape(plain, "0" * 40))
        finally:
            App.closeDocument(plain.Name)

    def seq(self, name):
        return [r for r in self.doc.getTransactionLog() if r["name"] == name][-1]["seq"]

    def testTwoStatesCompared(self):
        doc, cut = self.doc, self.cut
        before = cut.Shape.copy(), dict(cut.Shape.ElementReverseMap)
        base = self.seq("base")
        self.assertEqual(doc.compareTransactionStates(base, base), [])
        after = self.edit("taller", Height=12)
        changes = {(c["object"], c["prop"]): c for c in doc.compareTransactionStates(base)}
        # What was set by hand, and what the recompute made of it
        height = changes[("Box", "Height")]
        self.assertFalse(height["derived"])
        self.assertIn("10", doc.getTransactionValue(height["a"])[0])
        self.assertIn("12", doc.getTransactionValue(height["b"])[0])
        shape = changes[("Cut", "Shape")]
        self.assertTrue(shape["derived"])
        self.assertEqual((shape["ckind"], shape["cid"]), ("obj", cut.ID))
        self.assertIsThatShape(Part.getTransactionShape(doc, shape["a"], cut.ID), *before)
        self.assertIsThatShape(Part.getTransactionShape(doc, shape["b"], cut.ID), *after)
        # What no row between the two set is not said
        self.assertNotIn(("Cyl", "Radius"), changes)
        self.assertNotIn(("Box", "Length"), changes)
        # The other way round, the two values change places
        back = {(c["object"], c["prop"]): c for c in doc.compareTransactionStates(0, base)}
        self.assertEqual((back[("Cut", "Shape")]["a"], back[("Cut", "Shape")]["b"]),
                         (shape["b"], shape["a"]))

    def testAnObjectMadeBetweenTheTwo(self):
        doc = self.doc
        base = self.seq("base")
        doc.openTransaction("a ball")
        ball = doc.addObject("Part::Sphere", "Ball")
        doc.recompute()
        doc.commitTransaction()
        changes = doc.compareTransactionStates(base)
        made = [c for c in changes if c["object"] == "Ball" and not c["prop"]]
        self.assertEqual(len(made), 1)
        self.assertEqual((made[0]["inA"], made[0]["inB"], made[0]["cid"]), (False, True, ball.ID))
        self.assertLess(changes.index(made[0]),
                        min(i for i, c in enumerate(changes)
                            if c["object"] == "Ball" and c["prop"]))
        shape = [c for c in changes if c["object"] == "Ball" and c["prop"] == "Shape"][0]
        self.assertEqual(shape["a"], "")
        self.assertFalse(shape["inA"])
        read = Part.getTransactionShape(doc, shape["b"], ball.ID)
        self.assertAlmostEqual(read.Volume, ball.Shape.Volume)
        # And from now back to the base it is there at a and not at b
        gone = [c for c in doc.compareTransactionStates(0, base)
                if c["object"] == "Ball" and not c["prop"]]
        self.assertEqual((gone[0]["inA"], gone[0]["inB"]), (True, False))

    def testTwoBranchesCompared(self):
        doc, cut = self.doc, self.cut
        doc.createTransactionBranch("side")
        theirs = self.edit("side: longer", Length=14)
        side = self.seq("side: longer")
        doc.switchTransactionBranch("main")
        ours = self.edit("main: taller", Height=12)
        changes = {(c["object"], c["prop"]): c for c in doc.compareTransactionStates(side)}
        # Back from the side's row to where the two parted, and on to here
        self.assertIn(("Box", "Length"), changes)
        self.assertIn(("Box", "Height"), changes)
        shape = changes[("Cut", "Shape")]
        self.assertIsThatShape(Part.getTransactionShape(doc, shape["a"], cut.ID), *theirs)
        self.assertIsThatShape(Part.getTransactionShape(doc, shape["b"], cut.ID), *ours)
        with self.assertRaises(Exception):
            doc.compareTransactionStates(10**9)

