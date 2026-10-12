# SPDX-License-Identifier: LGPL-2.1-or-later

# ***************************************************************************
# *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************

"""PartDesign::SolidBody: the persistent group of one solid of a body.

A body of more than one solid keeps a SolidBody for each (Body.AddSolidBody).
The BaseFeature chains stay the record of what belongs to which solid; the
SolidBody objects are kept in step with them, and Feature._Siblings is written
the way a build without SolidBody groups a solid, under its last feature.
"""

import os
import re
import tempfile
import unittest
import zipfile

import ArchiveMembers
import FreeCAD

App = FreeCAD

FIXTURES = os.path.join(os.path.dirname(__file__), "Fixtures")


def names(objs):
    return [o.Name for o in objs]


class TestSolidBody(unittest.TestCase):
    def setUp(self):
        self.Doc = App.newDocument("PartDesignTestSolidBody")
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.files = []

    def tearDown(self):
        for doc in list(App.listDocuments().values()):
            if doc.Name.startswith("PartDesignTestSolidBody") or doc.FileName in self.files:
                App.closeDocument(doc.Name)
        for name in self.files:
            if os.path.exists(name):
                os.remove(name)

    # -- helpers -----------------------------------------------------------

    def box(self, name, x=0.0, size=10.0, newSolid=False, body=None):
        """An additive box of the body, added the way a command adds one"""
        body = body or self.Body
        feat = body.newObjectAt("PartDesign::AdditiveBox", name)
        feat.Length = size
        feat.Width = size
        feat.Height = size
        feat.Placement.Base = App.Vector(x, 0, 0)
        if newSolid:
            feat.NewSolid = True
        return feat

    def twoSolids(self):
        """Box and Box001 make one solid, Box002 and Box003 another"""
        a = self.box("Box")
        b = self.box("Box001", 5)
        c = self.box("Box002", 100, newSolid=True)
        d = self.box("Box003", 105)
        self.Doc.recompute()
        return a, b, c, d

    def solidBodies(self, body=None):
        return (body or self.Body).SolidBodies

    def tempFile(self, name):
        path = os.path.join(tempfile.gettempdir(), name)
        self.files.append(path)
        return path

    def assertInStep(self, body=None):
        """Each solid body lists one solid, and every solid has one"""
        body = body or self.Body
        solids = [names(s) for s in body.Solids]
        listed = [names(s.Group) for s in body.SolidBodies]
        self.assertEqual(sorted(solids), sorted(listed))
        for solid in body.SolidBodies:
            self.assertIn(solid, body.Group)
            self.assertIn(solid.Tip, solid.Group)
            for feat in solid.Group:
                self.assertEqual(body.getSolidBody(feat), solid)
        tip = body.TipSolidBody
        if body.SolidBodies:
            self.assertIsNotNone(tip)
            self.assertEqual(tip.Tip, body.Tip)

    # -- when a solid body exists -------------------------------------------

    def testNewBodyHasItOn(self):
        self.assertTrue(self.Body.AddSolidBody)

    def testSingleSolidHasNone(self):
        self.box("Box")
        self.box("Box001", 5)
        self.Doc.recompute()
        self.assertEqual(len(self.Body.Solids), 1)
        self.assertEqual(self.solidBodies(), [])
        self.assertEqual(names(self.Body.Group), ["Box", "Box001"])

    def testSecondSolidGivesEverySolidOne(self):
        a, b, c, d = self.twoSolids()
        solids = self.solidBodies()
        self.assertEqual(len(solids), 2)
        for solid in solids:
            self.assertEqual(solid.TypeId, "PartDesign::SolidBody")
        self.assertEqual(names(solids[0].Group), ["Box", "Box001"])
        self.assertEqual(names(solids[1].Group), ["Box002", "Box003"])
        self.assertInStep()
        # never a solid feature itself
        self.assertEqual([names(s) for s in self.Body.Solids],
                         [["Box", "Box001"], ["Box002", "Box003"]])
        self.assertTrue(self.Body.isValid())
        for solid in solids:
            self.assertTrue(solid.isValid())

    def testActiveObjectStaysTheFeature(self):
        self.box("Box")
        feat = self.Body.newObjectAt("PartDesign::AdditiveBox", "Box001")
        self.assertEqual(self.Doc.ActiveObject, feat)
        feat.NewSolid = True
        self.assertEqual(len(self.solidBodies()), 2)
        self.assertEqual(self.Doc.ActiveObject, feat)

    def testNamedSolid(self):
        """Solid, Solid001: a path names an object with a dot behind it, so
        Solid001 is never taken for the element Solid1"""
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.assertEqual(names([first, second]), ["Solid", "Solid001"])
        self.assertEqual(self.Body.getSubObject("Solid001.", retType=1), second)
        self.assertEqual(self.Body.getSubObject("Solid001.Face1", retType=1), second)
        self.assertEqual(self.Body.getSubObject("Solid001.Face1").ShapeType, "Face")
        self.assertEqual(self.Body.getSubObject("Solid001.Box003.Face1", retType=1), d)
        # without the dot it is an element of the body, as it always was
        self.assertEqual(self.Body.getSubObject("Solid1", retType=1), self.Body)

    def testStaysWhenTheOtherSolidIsDeleted(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        for feat in (d, c):
            self.Body.removeObject(feat)
            self.Doc.removeObject(feat.Name)
        self.Doc.recompute()
        # one solid left, and it keeps the solid body it had
        self.assertEqual(len(self.Body.Solids), 1)
        self.assertEqual(self.solidBodies(), [first])
        self.assertEqual(names(first.Group), ["Box", "Box001"])
        self.assertIsNone(self.Doc.getObject("Solid001"))
        self.assertInStep()

    def testGoesWithItsLastFeature(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.removeObject(d)
        self.Doc.removeObject(d.Name)
        self.assertEqual(names(second.Group), ["Box002"])
        self.Body.removeObject(c)
        self.Doc.removeObject(c.Name)
        self.assertEqual(self.solidBodies(), [first])

    def testFeatureAddedJoinsTheTipSolid(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.assertEqual(self.Body.TipSolidBody, second)
        e = self.box("Box004", 110)
        self.assertEqual(names(second.Group), ["Box002", "Box003", "Box004"])
        self.assertEqual(second.Tip, e)
        self.assertEqual(self.Body.Tip, e)
        self.assertEqual(names(first.Group), ["Box", "Box001"])
        self.assertInStep()

    # -- identity -------------------------------------------------------------

    def testHeadDeletedKeepsTheSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.removeObject(a)
        self.Doc.removeObject(a.Name)
        self.Doc.recompute()
        self.assertEqual(self.solidBodies(), [first, second])
        self.assertEqual(names(first.Group), ["Box001"])
        self.assertInStep()

    def testNewSolidInTheMiddleSplits(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        d.NewSolid = True
        self.Doc.recompute()
        solids = self.solidBodies()
        self.assertEqual(len(solids), 3)
        # the earlier part keeps the solid body, the later part gets a new one
        self.assertEqual(names(second.Group), ["Box002"])
        third = [s for s in solids if s not in (first, second)][0]
        self.assertEqual(names(third.Group), ["Box003"])
        self.assertInStep()

        # and joined again the new one goes, the old one has both
        d.NewSolid = False
        self.Doc.recompute()
        self.assertEqual(self.solidBodies(), [first, second])
        self.assertEqual(names(second.Group), ["Box002", "Box003"])
        self.assertInStep()

    def testJoiningTwoSolidsKeepsTheFirst(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        c.NewSolid = False
        self.Doc.recompute()
        self.assertEqual(len(self.Body.Solids), 1)
        self.assertEqual(self.solidBodies(), [first])
        self.assertEqual(names(first.Group), ["Box", "Box001", "Box002", "Box003"])
        self.assertInStep()

    def testRemovedFromTheDocumentDirectly(self):
        """No Body.removeObject(): the recompute puts it right"""
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Doc.removeObject(d.Name)
        self.Doc.removeObject(c.Name)
        self.Doc.recompute()
        self.assertEqual(self.solidBodies(), [first])
        self.assertInStep()

    # -- tips -------------------------------------------------------------------

    def testBodyTipIsAFeature(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.assertEqual(self.Body.Tip, d)
        # a solid body given as the tip stands for its own tip feature
        self.Body.Tip = first
        self.assertEqual(self.Body.Tip, b)
        self.assertEqual(self.Body.TipSolidBody, first)
        self.Doc.recompute()
        self.assertAlmostEqual(self.Body.Shape.Volume, b.Shape.Volume)

    def testBodyTipSetsTheSolidTip(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.Tip = a
        self.assertEqual(first.Tip, a)
        self.assertEqual(self.Body.TipSolidBody, first)
        # the other solid stays where it was
        self.assertEqual(second.Tip, d)
        self.Doc.recompute()
        self.assertAlmostEqual(first.Shape.Volume, a.Shape.Volume)
        self.assertAlmostEqual(self.Body.Shape.Volume, a.Shape.Volume)
        self.assertAlmostEqual(second.Shape.Volume, d.Shape.Volume)

    def testSolidTipOfTheTipSolidMovesTheBodyTip(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        second.Tip = c
        self.assertEqual(self.Body.Tip, c)
        self.Doc.recompute()
        self.assertAlmostEqual(self.Body.Shape.Volume, c.Shape.Volume)
        self.assertAlmostEqual(second.Shape.Volume, c.Shape.Volume)

    def testSolidTipOfAnotherSolidLeavesTheBodyTip(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.Tip = a
        self.assertEqual(self.Body.Tip, d)
        self.assertEqual(self.Body.TipSolidBody, second)
        self.Doc.recompute()
        self.assertAlmostEqual(first.Shape.Volume, a.Shape.Volume)

    def testNewFeatureGoesAfterTheSolidTip(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.Tip = a
        e = self.box("Box004", 2)
        # in the tip solid, right behind its tip, and the tip moves on to it
        self.assertEqual(names(first.Group), ["Box", "Box004", "Box001"])
        self.assertEqual(e.BaseFeature, a)
        self.assertEqual(b.BaseFeature, e)
        self.assertEqual(first.Tip, e)
        self.assertEqual(self.Body.Tip, e)
        self.assertInStep()

    def testDeletingTheTipMovesBothTips(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.removeObject(d)
        self.Doc.removeObject(d.Name)
        self.assertEqual(second.Tip, c)
        self.assertEqual(self.Body.Tip, c)
        # the last of its solid: the body's tip goes to where the other
        # solid is rolled back to
        first.Tip = a
        self.Body.removeObject(c)
        self.Doc.removeObject(c.Name)
        self.assertEqual(self.Body.Tip, a)
        self.assertEqual(first.Tip, a)

    # -- shape ------------------------------------------------------------------

    def testShapeIsTheTipsInBodyCoordinates(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.assertAlmostEqual(first.Shape.Volume, b.Shape.Volume)
        self.assertAlmostEqual(second.Shape.Volume, d.Shape.Volume)
        self.assertTrue(second.Placement.isIdentity())
        self.assertAlmostEqual(second.Shape.BoundBox.XMin, 100)
        self.assertEqual(len(second.Shape.Faces), len(d.Shape.Faces))
        # no feature of the body may depend on what follows it
        self.assertNotIn(first, a.OutListRecursive)

    def testShapeFollowsTheTip(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        d.Length = 30
        self.Doc.recompute()
        self.assertAlmostEqual(second.Shape.Volume, d.Shape.Volume)
        self.assertAlmostEqual(self.Body.Shape.Volume, d.Shape.Volume)

    # -- sub-object paths ---------------------------------------------------------

    def testPathThroughTheSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        # the tree's path, features under their solid body
        sub = "%s.Box003.Face1" % second.Name
        self.assertEqual(self.Body.getSubObject(sub, retType=1), d)
        self.assertEqual(self.Body.getSubObject(sub).ShapeType, "Face")
        # flattened it is the path there always was
        self.assertEqual(names(self.Body.getSubObjectList(sub, True)), ["Body", "Box003"])
        self.assertEqual(self.Body.normalizeSubName(sub)[1], "Box003.Face1")
        # entered from the solid body itself
        self.assertEqual(second.getSubObject("Box003.Face1", retType=1), d)

    def testPathToTheSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        sub = "%s.Face1" % second.Name
        self.assertEqual(self.Body.getSubObject(sub, retType=1), second)
        face = self.Body.getSubObject(sub)
        self.assertEqual(face.ShapeType, "Face")
        self.assertTrue(face.isSame(second.Shape.Face1))
        # a reference to the solid itself is not flattened away
        self.assertEqual(names(self.Body.getSubObjectList(sub, True)),
                         ["Body", second.Name])

    def testPathWhereTheSolidBodyIsMissing(self):
        """What a build without SolidBody resolves: the object is not loaded
        there, and the body skips the name it does not know"""
        self.twoSolids()
        sub = "Solid007.Box003.Face1"
        self.assertEqual(self.Body.getSubObject(sub, retType=1).Name, "Box003")

    def testPlacedBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.Placement.Base = App.Vector(0, 0, 50)
        self.Doc.recompute()
        face = self.Body.getSubObject("%s.Face1" % second.Name)
        self.assertAlmostEqual(face.BoundBox.ZMin, 50)
        face = self.Body.getSubObject("%s.Box003.Face1" % second.Name)
        self.assertAlmostEqual(face.BoundBox.ZMin, 50)

    # -- visibility -----------------------------------------------------------------

    def testHidingAndShowingASolid(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.assertFalse(first.Visibility)
        self.assertEqual(self.Body.isElementVisible(first.Name), 1)
        self.Body.setElementVisible(first.Name, False)
        self.assertEqual(self.Body.isElementVisible(first.Name), 0)
        self.assertFalse(a.Visibility or b.Visibility)
        self.assertTrue(d.Visibility)
        first.Tip = a
        self.Body.setElementVisible(first.Name, True)
        self.assertTrue(a.Visibility)
        self.assertFalse(b.Visibility)

    def testOwnShapeOrAFeature(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.assertTrue(b.Visibility)
        first.Visibility = True
        self.assertFalse(a.Visibility or b.Visibility)
        self.assertEqual(self.Body.isElementVisible(first.Name), 1)
        b.Visibility = True
        self.assertFalse(first.Visibility)

    def shown(self, solid):
        """The names of what a solid is seen through"""
        return names(o for o in [solid] + solid.Group if o.Visibility)

    def show(self, obj):
        """Show a feature the way its eye does. One made by a script is
        visible from the start, beside the others of its solid."""
        obj.Visibility = False
        obj.Visibility = True

    def twoSolidsAsSeen(self):
        """twoSolids(), each solid seen through its tip alone"""
        a, b, c, d = self.twoSolids()
        self.show(b)
        self.show(d)
        return a, b, c, d

    def testNothingIsRememberedAtFirst(self):
        self.twoSolidsAsSeen()
        for solid in self.solidBodies():
            self.assertIsNone(solid._Shown)
            self.assertFalse(solid._ShownShape)

    def testShownThroughTheFeatureItWasSeenThrough(self):
        a, b, c, d = self.twoSolidsAsSeen()
        first, second = self.solidBodies()
        self.show(a)
        self.assertEqual(self.shown(first), ["Box"])
        self.Body.setElementVisible(first.Name, False)
        self.assertEqual(self.shown(first), [])
        self.Body.setElementVisible(first.Name, True)
        self.assertEqual(self.shown(first), ["Box"])
        # and the other solid was never part of it
        self.assertEqual(self.shown(second), ["Box003"])
        self.assertIsNone(second._Shown)

    def testShownThroughItsOwnShapeAgain(self):
        a, b, c, d = self.twoSolidsAsSeen()
        first, second = self.solidBodies()
        first.Visibility = True
        self.Body.setElementVisible(first.Name, False)
        self.assertEqual(self.shown(first), [])
        self.Body.setElementVisible(first.Name, True)
        self.assertEqual(self.shown(first), [first.Name])
        # a feature shown takes over, and is what comes back from then on
        self.show(a)
        self.Body.setElementVisible(first.Name, False)
        self.Body.setElementVisible(first.Name, True)
        self.assertEqual(self.shown(first), ["Box"])

    def testFeatureHiddenByItselfIsRemembered(self):
        a, b, c, d = self.twoSolidsAsSeen()
        first, second = self.solidBodies()
        self.show(a)
        a.Visibility = False
        self.assertEqual(self.Body.isElementVisible(first.Name), 0)
        self.Body.setElementVisible(first.Name, True)
        self.assertEqual(self.shown(first), ["Box"])

    def testHidingASiblingByShowingAnotherIsNotRemembered(self):
        a, b, c, d = self.twoSolidsAsSeen()
        first, second = self.solidBodies()
        self.show(a)
        self.show(b)
        self.assertIsNone(first._Shown)
        self.Body.setElementVisible(first.Name, False)
        self.Body.setElementVisible(first.Name, True)
        self.assertEqual(self.shown(first), ["Box001"])

    def testSeenThroughTheTipFollowsTheTip(self):
        a, b, c, d = self.twoSolidsAsSeen()
        first, second = self.solidBodies()
        self.Body.setElementVisible(first.Name, False)
        self.assertIsNone(first._Shown)
        first.Tip = a
        self.Body.setElementVisible(first.Name, True)
        self.assertEqual(self.shown(first), ["Box"])

    def testRememberedFeatureDeleted(self):
        a, b, c, d = self.twoSolidsAsSeen()
        first, second = self.solidBodies()
        self.show(c)
        self.Body.setElementVisible(second.Name, False)
        self.assertEqual(second._Shown, c)
        self.Doc.removeObject(c.Name)
        self.Doc.recompute()
        self.assertEqual(names(second.Group), ["Box003"])
        self.assertIsNone(second._Shown)
        self.Body.setElementVisible(second.Name, True)
        self.assertEqual(self.shown(second), ["Box003"])

    def testRememberedFeatureLeftTheSolid(self):
        a, b, c, d = self.twoSolidsAsSeen()
        first, second = self.solidBodies()
        first.Tip = a
        self.Body.setElementVisible(first.Name, False)
        self.assertEqual(first._Shown, b)
        # Box001 starts a solid of its own, and is no view of the first one
        b.NewSolid = True
        self.Doc.recompute()
        self.assertInStep()
        self.assertEqual(self.Body.getSolidBody(a), first)
        other = self.Body.getSolidBody(b)
        self.assertNotEqual(other, first)
        self.Body.setElementVisible(first.Name, True)
        self.assertEqual(self.shown(first), ["Box"])
        self.assertEqual(self.shown(other), [])
        self.Body.setElementVisible(other.Name, True)
        self.assertEqual(self.shown(other), ["Box001"])
        self.assertEqual(self.shown(first), ["Box"])

    def testRememberingIsNoUndoStep(self):
        a, b, c, d = self.twoSolidsAsSeen()
        first, second = self.solidBodies()
        self.Doc.UndoMode = 1
        count = self.Doc.UndoCount
        self.show(a)
        self.Body.setElementVisible(first.Name, False)
        self.Body.setElementVisible(first.Name, True)
        self.assertEqual(self.Doc.UndoCount, count)
        self.assertEqual(self.shown(first), ["Box"])

    # -- the look and the material of a solid ------------------------------------------
    #
    # A solid body given a look gives it to its features; one given a look of
    # its own keeps that. Its material card is theirs.

    RED = (1.0, 0.0, 0.0)
    GREEN = (0.0, 1.0, 0.0)
    BLUE = (0.0, 0.0, 1.0)

    def rgb(self, obj):
        return tuple(round(c, 3) for c in obj.ShapeColor[:3])

    def drawn(self, obj):
        """The colours the faces of an object are drawn in"""
        faces = obj.ElementAppearance.Faces
        return {tuple(round(c, 3) for c in faces[i].DiffuseColor[:3]) for i in range(len(faces))}

    def testFeaturesTakeTheLookOfTheirSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        grey = self.rgb(c)
        self.assertNotEqual(grey, self.RED)
        first.ShapeColor = self.RED
        self.assertEqual(self.rgb(a), self.RED)
        self.assertEqual(self.rgb(b), self.RED)
        self.Doc.recompute()
        self.assertEqual(self.drawn(a), {self.RED})
        self.assertEqual(self.drawn(b), {self.RED})
        self.assertEqual(self.drawn(first), {self.RED})
        # the other solid is not its
        for obj in (c, d, second):
            self.assertEqual(self.rgb(obj), grey)
            self.assertEqual(self.drawn(obj), {grey})

    def testTransparencyOfASolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.Transparency = 40
        self.assertEqual(a.Transparency, 40)
        self.assertEqual(b.Transparency, 40)
        self.assertEqual(c.Transparency, 0)

    def testFeatureGivenALookKeepsIt(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        a.ShapeColor = self.BLUE
        first.ShapeColor = self.RED
        self.assertEqual(self.rgb(a), self.BLUE)
        self.assertEqual(self.rgb(b), self.RED)
        self.Doc.recompute()
        # the faces Box001 has of Box are Box's colour still
        self.assertEqual(self.drawn(b), {self.RED, self.BLUE})
        # given one after its solid body was: the same
        first.ShapeColor = self.GREEN
        self.assertEqual(self.rgb(a), self.BLUE)
        self.assertEqual(self.rgb(b), self.GREEN)
        b.ShapeColor = self.RED
        first.ShapeColor = self.BLUE
        self.assertEqual(self.rgb(b), self.RED)

    def testFeatureGivingUpItsLookTakesTheSolidBodys(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.ShapeColor = self.RED
        b.ShapeColor = self.GREEN
        self.assertEqual(self.rgb(b), self.GREEN)
        b.ElementAppearance.clear()
        self.assertEqual(self.rgb(b), self.RED)
        first.ShapeColor = self.BLUE
        self.assertEqual(self.rgb(b), self.BLUE)

    def testNewFeatureTakesTheLook(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.ShapeColor = self.RED
        self.Body.Tip = b
        e = self.box("Box004", 8)
        self.Doc.recompute()
        self.assertEqual(self.Body.getSolidBody(e), first)
        self.assertEqual(self.rgb(e), self.RED)

    def testFeatureLeavingASolidKeepsWhatItHad(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.ShapeColor = self.RED
        b.NewSolid = True
        self.Doc.recompute()
        self.assertInStep()
        other = self.Body.getSolidBody(b)
        self.assertNotEqual(other, first)
        self.assertEqual(self.rgb(b), self.RED)
        # and takes the look its new solid body is given
        other.ShapeColor = self.GREEN
        self.assertEqual(self.rgb(b), self.GREEN)
        self.assertEqual(self.rgb(a), self.RED)

    def testFeatureJoiningASolidTakesItsLook(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.ShapeColor = self.RED
        second.ShapeColor = self.GREEN
        self.assertEqual(self.rgb(c), self.GREEN)
        # Box002 and what follows it are of the first solid now
        c.NewSolid = False
        self.Doc.recompute()
        self.assertEqual(self.Body.getSolidBody(c), first)
        self.assertEqual(self.rgb(c), self.RED)
        self.assertEqual(self.rgb(d), self.RED)

    def testLookOfASolidBodyIsUndone(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        grey = self.rgb(a)
        self.Doc.UndoMode = 1
        self.Doc.openTransaction("colour")
        first.ShapeColor = self.RED
        self.Doc.commitTransaction()
        self.assertEqual(self.rgb(b), self.RED)
        self.Doc.undo()
        self.assertEqual(self.rgb(first), grey)
        self.assertEqual(self.rgb(a), grey)
        self.assertEqual(self.rgb(b), grey)
        self.Doc.redo()
        self.assertEqual(self.rgb(a), self.RED)
        self.assertEqual(self.rgb(b), self.RED)

    def testLookOfASolidBodyAfterAReload(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.ShapeColor = self.RED
        b.ShapeColor = self.GREEN
        self.Doc.recompute()
        saved = self.tempFile("PartDesignTestSolidBody_look.FCStd")
        self.Doc.saveAs(saved)
        App.closeDocument(self.Doc.Name)
        doc = App.openDocument(saved)
        try:
            body = doc.getObject("Body")
            first = body.SolidBodies[0]
            a, b = doc.getObject("Box"), doc.getObject("Box001")
            self.assertEqual(self.rgb(first), self.RED)
            self.assertEqual(self.rgb(a), self.RED)
            self.assertEqual(self.rgb(b), self.GREEN)
            # which of them was given its look is in the file
            first.ShapeColor = self.BLUE
            self.assertEqual(self.rgb(a), self.BLUE)
            self.assertEqual(self.rgb(b), self.GREEN)
        finally:
            App.closeDocument(doc.Name)

    def testSolidBodySavedWithNoLookIsGivenOne(self):
        # what a solid body made before it was given a look when made is
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.ElementAppearance.clear()
        saved = self.tempFile("PartDesignTestSolidBody_nolook.FCStd")
        self.Doc.saveAs(saved)
        App.closeDocument(self.Doc.Name)
        doc = App.openDocument(saved)
        try:
            first = doc.getObject("Body").SolidBodies[0]
            self.assertEqual(self.rgb(first), self.rgb(doc.getObject("Box002")))
            first.ShapeColor = self.RED
            self.assertEqual(self.rgb(doc.getObject("Box")), self.RED)
            self.assertEqual(self.rgb(doc.getObject("Box001")), self.RED)
        finally:
            App.closeDocument(doc.Name)

    def line(self, obj):
        return tuple(round(c, 3) for c in obj.LineColor[:3])

    def point(self, obj):
        return tuple(round(c, 3) for c in obj.PointColor[:3])

    def testLinesAndPointsOfASolidBodyAreItsFeatures(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        dark = self.line(c)
        grey = self.rgb(c)
        first.LineColor = self.RED
        self.assertEqual(self.line(a), self.RED)
        self.assertEqual(self.line(b), self.RED)
        # the lines alone: nothing else was given
        self.assertEqual(self.point(a), self.point(c))
        self.assertEqual(self.rgb(a), grey)
        first.PointColor = self.BLUE
        self.assertEqual(self.point(a), self.BLUE)
        self.assertEqual(self.point(b), self.BLUE)
        self.assertEqual(self.line(b), self.RED)
        for obj in (c, d, second):
            self.assertEqual(self.line(obj), dark)
            self.assertNotEqual(self.point(obj), self.BLUE)

    def testFeatureGivenALineColourKeepsIt(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        a.LineColor = self.GREEN
        first.LineColor = self.RED
        self.assertEqual(self.line(a), self.GREEN)
        self.assertEqual(self.line(b), self.RED)
        # and its faces are the solid body's all the same
        first.ShapeColor = self.BLUE
        self.assertEqual(self.rgb(a), self.BLUE)
        self.assertEqual(self.line(a), self.GREEN)

    def testNewFeatureTakesTheLines(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.LineColor = self.RED
        self.Body.Tip = b
        e = self.box("Box004", 8)
        self.Doc.recompute()
        self.assertEqual(self.Body.getSolidBody(e), first)
        self.assertEqual(self.line(e), self.RED)

    def twoCards(self):
        import Materials

        materials = Materials.MaterialManager().Materials
        steel = [m for m in materials.values() if m.Name == "Steel-Generic"][0]
        other = [m for m in materials.values() if m.Name == "Aluminum-Generic"][0]
        return steel, other

    def testMaterialOfASolidBodyIsItsFeatures(self):
        steel, aluminum = self.twoCards()
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        default = self.Body.ShapeMaterial.UUID
        self.assertEqual(first.ShapeMaterial.UUID, default)
        first.ShapeMaterial = steel
        for obj in (a, b):
            self.assertEqual(obj.ShapeMaterial.UUID, steel.UUID)
        for obj in (c, d, second, self.Body):
            self.assertEqual(obj.ShapeMaterial.UUID, default)
        # the material of a feature is its solid body's, and so its siblings'
        d.ShapeMaterial = aluminum
        for obj in (c, d, second):
            self.assertEqual(obj.ShapeMaterial.UUID, aluminum.UUID)
        for obj in (a, b, first):
            self.assertEqual(obj.ShapeMaterial.UUID, steel.UUID)
        # a feature added takes the one of the solid it is added to
        self.Body.Tip = b
        e = self.box("Box004", 8)
        self.Doc.recompute()
        self.assertEqual(self.Body.getSolidBody(e), first)
        self.assertEqual(e.ShapeMaterial.UUID, steel.UUID)

    def testMaterialOfTheBodyIsEverySolids(self):
        steel, aluminum = self.twoCards()
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.ShapeMaterial = aluminum
        self.Body.ShapeMaterial = steel
        for obj in (a, b, c, d, first, second):
            self.assertEqual(obj.ShapeMaterial.UUID, steel.UUID)

    def testNewSolidBodyIsOfTheBodysMaterial(self):
        steel, aluminum = self.twoCards()
        a = self.box("Box")
        self.Doc.recompute()
        self.Body.ShapeMaterial = steel
        b = self.box("Box001", 100, newSolid=True)
        self.Doc.recompute()
        for solid in self.solidBodies():
            self.assertEqual(solid.ShapeMaterial.UUID, steel.UUID)
        self.assertEqual(a.ShapeMaterial.UUID, steel.UUID)
        self.assertEqual(b.ShapeMaterial.UUID, steel.UUID)

    # -- a reference to the solid body from inside its own solid ---------------------
    #
    # The solid body has the shape of its tip, so what the tip is made of
    # cannot refer to it. Picked for a feature of the same solid, it means
    # the solid as that feature finds it: the feature before it.

    def flat(self, obj, x=0.0):
        """The highest face lying flat, the one starting at x where there are two"""
        best = None
        for i, face in enumerate(obj.Shape.Faces):
            bb = face.BoundBox
            if abs(bb.ZMin - bb.ZMax) > 1e-6:
                continue
            key = (round(bb.ZMax, 6), abs(bb.XMin - x) < 1e-6)
            if best is None or key > best[0]:
                best = (key, "Face%d" % (i + 1))
        return best[1]

    def sketchOn(self, obj, face, x=2.0):
        import Part

        sketch = self.Body.newObject("Sketcher::SketchObject", "Sketch")
        sketch.AttachmentSupport = [(obj, face)]
        sketch.MapMode = "FlatFace"
        sketch.addGeometry(Part.Circle(App.Vector(x, 2, 0), App.Vector(0, 0, 1), 1.0), False)
        return sketch

    def support(self, sketch):
        return [(obj.Name, list(subs)) for obj, subs in sketch.AttachmentSupport]

    def padOn(self, sketch, name="Pad", length=5.0):
        pad = self.Body.newObjectAt("PartDesign::Pad", name)
        pad.Profile = sketch
        pad.Length = length
        return pad

    def assertRecomputes(self):
        self.Doc.recompute()
        self.assertEqual([o.Name for o in self.Doc.Objects if "Invalid" in o.State], [])
        self.assertEqual([o.Name for o in self.Doc.Objects if "Touched" in o.State], [])

    def testSketchOnTheSolidBodyForAFeatureOfTheSolid(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.Tip = b
        self.Doc.recompute()
        face = self.flat(first)
        sketch = self.sketchOn(first, face)
        self.Doc.recompute()
        # nothing of the solid is made of it yet
        self.assertEqual(self.support(sketch), [(first.Name, [face])])
        pad = self.padOn(sketch)
        self.assertEqual(first.Tip, pad)
        # the solid as the pad finds it
        self.assertEqual(self.support(sketch), [(b.Name, [face])])
        self.assertRecomputes()
        self.assertAlmostEqual(sketch.Placement.Base.z, 10.0)
        self.assertGreater(pad.Shape.Volume, b.Shape.Volume)
        self.assertAlmostEqual(first.Shape.Volume, pad.Shape.Volume)

    def testFeatureReferringToItsOwnSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.Tip = b
        self.Doc.recompute()
        sketch = self.sketchOn(a, self.flat(a))
        pad = self.padOn(sketch)
        self.assertEqual(first.Tip, pad)
        # picked on the solid body while it had the shape of Box001
        face = self.flat(first)
        pad.Type = "UpToFace"
        pad.UpToFace = (first, [face])
        self.assertEqual(pad.UpToFace[0], b)
        self.assertEqual(list(pad.UpToFace[1]), [face])

    def testSolidBodyReferredToAfterAReload(self):
        a, b, c, d = self.twoSolids()
        self.Body.Tip = b
        self.Doc.recompute()
        path = self.tempFile("PartDesignTestSolidBodyOwnReference.FCStd")
        self.Doc.saveAs(path)
        App.closeDocument(self.Doc.Name)
        self.Doc = App.openDocument(path)
        self.Body = self.Doc.Body
        first, second = self.solidBodies()
        tip = first.Tip
        face = self.flat(first)
        sketch = self.sketchOn(first, face)
        pad = self.padOn(sketch)
        self.assertEqual(self.support(sketch), [(tip.Name, [face])])
        self.assertRecomputes()

    def testFeatureOfAnotherSolidRefersToTheSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        face = self.flat(first)
        sketch = self.sketchOn(first, face)
        pad = self.padOn(sketch)
        self.assertEqual(self.Body.getSolidBody(pad), second)
        # the moving target it was made to be
        self.assertEqual(self.support(sketch), [(first.Name, [face])])
        self.assertRecomputes()
        first.Tip = a
        self.assertRecomputes()
        self.assertEqual(self.support(sketch)[0][0], first.Name)

    def testFeatureBehindTheTipRefersToTheSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.Tip = b
        self.Doc.recompute()
        face = self.flat(first)
        sketch = self.sketchOn(first, face)
        pad = self.padOn(sketch)
        self.assertEqual(self.support(sketch), [(b.Name, [face])])
        self.assertRecomputes()
        # rolled back, a sketch on the solid as it is then, for nothing yet
        first.Tip = b
        self.Doc.recompute()
        again = self.sketchOn(first, self.flat(first), 4.0)
        self.assertRecomputes()
        self.assertEqual(self.support(again)[0][0], first.Name)

    def testReferenceWithTheNamesOfAnotherShapeIsLeft(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.Tip = b
        self.Doc.recompute()
        sketch = self.sketchOn(a, self.flat(a))
        pad = self.padOn(sketch)
        self.assertRecomputes()
        # the sketch of the pad attached to the solid the pad is the tip of:
        # the face is one of the pad's shape, not of the box before it
        face = self.flat(first)
        sketch.AttachmentSupport = [(first, face)]
        self.assertEqual(self.support(sketch), [(first.Name, [face])])
        # put right by hand, it recomputes
        sketch.AttachmentSupport = [(a, self.flat(a))]
        self.assertRecomputes()

    # -- what a build without SolidBody reads ----------------------------------------

    def testSiblingsAreGroupedUnderTheLastFeature(self):
        a, b, c, d = self.twoSolids()
        e = self.box("Box004", 110)
        self.assertEqual(names(e._Siblings), ["Box003", "Box002"])
        self.assertEqual(names(b._Siblings), ["Box"])
        for feat in (a, c, d):
            self.assertEqual(feat._Siblings, [])

    def testSingleSolidIsNotGrouped(self):
        a = self.box("Box")
        b = self.box("Box001", 5)
        self.assertEqual(a._Siblings, [])
        self.assertEqual(b._Siblings, [])

    def testBodyTipIsReadableWithoutSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        self.Body.Tip = first
        self.assertEqual(self.Body.Tip.TypeId, "PartDesign::AdditiveBox")

    def stripSolidBodies(self, source, target):
        """Rewrite a saved document the way a build without SolidBody saves it
        again: the objects it could not make are gone, and so is the property
        it does not have"""
        items = ArchiveMembers.members(source)
        xml = dict((item.filename, data) for item, data in items)["Document.xml"].decode("utf-8")
        gone = re.findall(r'<Object type="PartDesign::SolidBody" name="([^"]+)"', xml)
        self.assertTrue(gone)

        def without(pattern, text, expected=None):
            text, count = re.subn(pattern, "", text, flags=re.S)
            if expected is not None:
                self.assertEqual(count, expected, pattern)
            return text

        def recount(tag, attr, child):
            """Set every <tag attr="N"> to the number of <child> it holds"""
            def fix(m):
                return "%s%d%s" % (m.group(1), len(re.findall("<%s " % child, m.group(4))),
                                   m.group(3) + m.group(4) + m.group(5))
            return re.sub(r'(<%s [^>]*?%s=")(\d+)("[^>/]*>)(.*?)(</%s>)' % (tag, attr, tag),
                          fix, xml, flags=re.S)

        xml = without(r'\s*<Object type="PartDesign::SolidBody"[^>]*/>', xml, len(gone))
        for name in gone:
            xml = without(r'\s*<ObjectDeps Name="%s"[^>]*>.*?</ObjectDeps>' % name, xml, 1)
            xml = without(r'\s*<Object name="%s"[^>]*>.*?</Object>' % name, xml, 1)
            xml = without(r'\s*<Dep Name="%s"/>' % name, xml)
            xml = without(r'\s*<Link value="%s"/>' % name, xml)
        xml = recount("ObjectDeps", "Count", "Dep")
        xml = recount("LinkList", "count", "Link")

        # the property a build without SolidBody does not have
        m = re.search(r'\s*<Property name="AddSolidBody".*?</Property>', xml, flags=re.S)
        self.assertIsNotNone(m)
        head = xml[:m.start()]
        k = head.rfind('<Properties Count="')
        count = int(re.match(r'<Properties Count="(\d+)"', head[k:]).group(1))
        head = head[:k] + head[k:].replace('Count="%d"' % count, 'Count="%d"' % (count - 1), 1)
        xml = head + xml[m.end():]

        for tag in ("Objects", "ObjectData"):
            xml = re.sub(r'<%s Count="(\d+)"' % tag,
                         lambda m: '<%s Count="%d"' % (tag, int(m.group(1)) - len(gone)),
                         xml, count=1)
        self.assertEqual(re.findall(r'::SolidBody|"Solid\d*"', xml), [])

        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as zout:
            for item, data in items:
                if item.filename == "GuiDocument.xml":
                    continue
                if item.filename == "Document.xml":
                    data = xml.encode("utf-8")
                zout.writestr(item, data)

    def testComesBackFromABuildWithoutSolidBody(self):
        a, b, c, d = self.twoSolids()
        saved = self.tempFile("PartDesignTestSolidBody_new.FCStd")
        stripped = self.tempFile("PartDesignTestSolidBody_old.FCStd")
        history = ArchiveMembers.HistoryLeftOut()
        try:
            self.Doc.saveAs(saved)
        finally:
            history.release()
        self.stripSolidBodies(saved, stripped)
        doc = App.openDocument(stripped)
        try:
            body = doc.getObject("Body")
            # a body as it was before SolidBody: off, and grouped by _Siblings
            self.assertFalse(body.AddSolidBody)
            self.assertEqual(body.SolidBodies, [])
            self.assertEqual(names(body.Group), ["Box", "Box001", "Box002", "Box003"])
            self.assertEqual(body.Tip.Name, "Box003")
            self.assertEqual(names(doc.getObject("Box003")._Siblings), ["Box002"])
            self.assertEqual(names(doc.getObject("Box001")._Siblings), ["Box"])
            doc.recompute()
            self.assertEqual(body.SolidBodies, [])
            self.assertTrue(body.isValid())
            # and switched on it has its solid bodies again
            body.AddSolidBody = True
            self.assertEqual(len(body.SolidBodies), 2)
            self.assertInStep(body)
        finally:
            App.closeDocument(doc.Name)

    def testOldDocumentsStayOff(self):
        for name in ("ModelFromV021.FCStd", "FaceRefs_fork_whole_names.FCStd"):
            path = os.path.join(FIXTURES, name)
            if not os.path.exists(path):
                continue
            doc = App.openDocument(path)
            try:
                bodies = [o for o in doc.Objects if o.isDerivedFrom("PartDesign::Body")]
                self.assertTrue(bodies)
                for body in bodies:
                    self.assertFalse(body.AddSolidBody)
                    self.assertEqual(body.SolidBodies, [])
            finally:
                App.closeDocument(doc.Name)

    # -- the switch -----------------------------------------------------------------

    def testOffIsTheOldGrouping(self):
        self.Body.AddSolidBody = False
        a, b, c, d = self.twoSolids()
        self.assertEqual(self.solidBodies(), [])
        self.assertEqual(names(self.Body.Group), ["Box", "Box001", "Box002", "Box003"])

    def testSwitchingOnAndOff(self):
        self.Body.AddSolidBody = False
        a, b, c, d = self.twoSolids()
        self.Body.AddSolidBody = True
        self.assertEqual(len(self.solidBodies()), 2)
        self.assertInStep()
        self.assertEqual(names(d._Siblings), ["Box002"])
        self.Doc.recompute()
        self.Body.AddSolidBody = False
        self.assertEqual(self.solidBodies(), [])
        self.assertEqual(names(self.Body.Group), ["Box", "Box001", "Box002", "Box003"])
        # what is left is the grouping an older build shows
        self.assertEqual(names(d._Siblings), ["Box002"])
        self.assertEqual(self.Body.Tip, d)

    # -- persistence ------------------------------------------------------------------

    def testSaveAndReload(self):
        a, b, c, d = self.twoSolids()
        first, second = self.solidBodies()
        first.Tip = a
        self.Doc.recompute()
        # hidden while seen through Box001, which is not its tip
        self.Body.setElementVisible(first.Name, False)
        solidNames = [first.Name, second.Name]
        saved = self.tempFile("PartDesignTestSolidBody_save.FCStd")
        self.Doc.saveAs(saved)
        App.closeDocument(self.Doc.Name)
        doc = App.openDocument(saved)
        try:
            body = doc.getObject("Body")
            self.assertTrue(body.AddSolidBody)
            solids = body.SolidBodies
            self.assertEqual(names(solids), solidNames)
            self.assertEqual(names(solids[0].Group), ["Box", "Box001"])
            self.assertEqual(names(solids[1].Group), ["Box002", "Box003"])
            self.assertEqual(solids[0].Tip.Name, "Box")
            self.assertEqual(body.Tip.Name, "Box003")
            self.assertInStep(body)
            self.assertAlmostEqual(solids[0].Shape.Volume, 1000)
            # what the first solid was seen through is kept by the file
            self.assertEqual(body.isElementVisible(solids[0].Name), 0)
            body.setElementVisible(solids[0].Name, True)
            self.assertTrue(doc.getObject("Box001").Visibility)
            self.assertFalse(doc.getObject("Box").Visibility)
            # nothing to do after a load
            group = names(body.Group)
            doc.recompute()
            self.assertEqual(names(body.Group), group)
        finally:
            App.closeDocument(doc.Name)

    def testUndoRedo(self):
        self.Doc.UndoMode = 1
        self.Doc.openTransaction("first solid")
        a = self.box("Box")
        self.Doc.recompute()
        self.Doc.commitTransaction()
        self.Doc.openTransaction("second solid")
        b = self.box("Box001", 100, newSolid=True)
        self.Doc.recompute()
        self.Doc.commitTransaction()
        self.assertEqual(len(self.solidBodies()), 2)
        self.Doc.undo()
        self.assertEqual(self.solidBodies(), [])
        self.assertEqual(names(self.Body.Group), ["Box"])
        self.Doc.recompute()
        self.assertEqual(self.solidBodies(), [])
        self.Doc.redo()
        self.assertEqual(len(self.solidBodies()), 2)
        self.assertInStep()
        self.Doc.recompute()
        self.assertEqual(len(self.solidBodies()), 2)
        self.assertInStep()

    # -- with the rest of a body --------------------------------------------------------

    def testOtherBodyIsLeftAlone(self):
        other = self.Doc.addObject("PartDesign::Body", "Body001")
        self.box("OtherBox", body=other)
        self.twoSolids()
        self.assertEqual(other.SolidBodies, [])
        self.assertEqual(len(self.solidBodies()), 2)
        for solid in self.solidBodies():
            self.assertEqual(solid._Body, self.Body)

    def testDeletingTheBody(self):
        self.twoSolids()
        self.Body.removeObjectsFromDocument()
        self.Doc.removeObject(self.Body.Name)
        self.assertEqual([o.Name for o in self.Doc.Objects
                          if o.isDerivedFrom("PartDesign::SolidBody")], [])
