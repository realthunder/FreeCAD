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

"""The tree side of PartDesign::SolidBody: what is listed under what, the
arrows that mark the tips, and moving a tip by dropping its arrow. And where
in the 3D view a solid body is highlighted when it is selected.

The arrows are extra icons of the tree items. A drag of one is carried out by
the view providers (ViewProvider.canDragIcon/canDropIcon/dropIcon), which is
what is called here; the tree only finds the icon under the cursor.
"""

import unittest

import FreeCAD
import FreeCADGui

App = FreeCAD
Gui = FreeCADGui

SOLID_TIP = "PartDesign::SolidTip"
BODY_TIP = "PartDesign::BodyTip"
TAG = "PartDesign::Tag"


def names(objs):
    return [o.Name for o in objs]


class TestSolidBodyGui(unittest.TestCase):
    def setUp(self):
        self.Doc = App.newDocument("PartDesignTestSolidBodyGui")
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")

    def tearDown(self):
        App.closeDocument(self.Doc.Name)

    def box(self, name, x=0.0, newSolid=False):
        feat = self.Body.newObjectAt("PartDesign::AdditiveBox", name)
        feat.Length = feat.Width = feat.Height = 10
        feat.Placement.Base = App.Vector(x, 0, 0)
        if newSolid:
            feat.NewSolid = True
        # the one shown of its solid, as after the command that makes it
        feat.Visibility = False
        feat.Visibility = True
        return feat

    def twoSolids(self):
        a = self.box("Box")
        b = self.box("Box001", 5)
        c = self.box("Box002", 100, True)
        d = self.box("Box003", 105)
        self.Doc.recompute()
        return a, b, c, d

    def tips(self, obj):
        """The tip icons of an object's tree item"""
        return [t for t in obj.ViewObject.getExtraIcons() if t in (SOLID_TIP, BODY_TIP, TAG)]

    # -- what is listed under what ---------------------------------------------

    def testFeaturesAreListedUnderTheirSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        self.assertEqual(first.ViewObject.TypeId, "PartDesignGui::ViewProviderSolidBody")
        self.assertEqual(names(first.ViewObject.claimChildren()), ["Box", "Box001"])
        self.assertEqual(names(second.ViewObject.claimChildren()), ["Box002", "Box003"])
        top = names(self.Body.ViewObject.claimChildren())
        self.assertIn(first.Name, top)
        self.assertIn(second.Name, top)
        for feat in (a, b, c, d):
            self.assertNotIn(feat.Name, top)
        # the record for builds without solid bodies is no grouping here
        self.assertEqual(names(d._Siblings), ["Box002"])
        self.assertNotIn(c, d.ViewObject.claimChildren())

    def testOneSolidIsListedUnderTheBody(self):
        a = self.box("Box")
        b = self.box("Box001", 5)
        self.Doc.recompute()
        top = names(self.Body.ViewObject.claimChildren())
        self.assertIn("Box", top)
        self.assertIn("Box001", top)

    def testSwitchedOffListsUnderTheLastFeature(self):
        a, b, c, d = self.twoSolids()
        self.Body.AddSolidBody = False
        self.Doc.recompute()
        self.assertEqual(self.Body.SolidBodies, [])
        self.assertIn(c, d.ViewObject.claimChildren())
        self.assertIn(a, b.ViewObject.claimChildren())

    # -- the arrows ----------------------------------------------------------------

    def testArrowsMarkTheTips(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        self.assertEqual(self.tips(a), [])
        self.assertEqual(self.tips(b), [SOLID_TIP])
        self.assertEqual(self.tips(c), [])
        self.assertEqual(self.tips(d), [SOLID_TIP])
        self.assertEqual(self.tips(first), [])
        self.assertEqual(self.tips(second), [BODY_TIP])
        # the arrow comes first of the extra icons
        self.assertEqual(d.ViewObject.getExtraIcons()[0], SOLID_TIP)

    def testOneSolidHasOneArrow(self):
        a = self.box("Box")
        b = self.box("Box001", 5)
        self.Doc.recompute()
        self.assertEqual(self.tips(a), [])
        self.assertEqual(self.tips(b), [SOLID_TIP])

    def testSwitchedOffHasTags(self):
        self.Body.AddSolidBody = False
        a, b, c, d = self.twoSolids()
        for feat in (a, b, c, d):
            self.assertNotIn(SOLID_TIP, feat.ViewObject.getExtraIcons())
        self.assertIn(TAG, d.ViewObject.getExtraIcons())

    def testToolTipsSayWhichTipAndHowToDrag(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        tip = second.ViewObject.getIconToolTip(BODY_TIP)
        self.assertIn("Body tip", tip)
        self.assertIn("Drag this arrow", tip)
        tip = d.ViewObject.getIconToolTip(SOLID_TIP)
        self.assertIn("Solid tip", tip)
        self.assertIn(second.Label, tip)
        self.assertIn("Drag this arrow", tip)
        # the tip of the solid that is the body's tip says so
        self.assertIn("tip of the body", tip)
        tip = b.ViewObject.getIconToolTip(SOLID_TIP)
        self.assertIn(first.Label, tip)
        self.assertNotIn("tip of the body", tip)

    # -- dragging an arrow -----------------------------------------------------------

    def testOnlyTheArrowsAreDragged(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        self.assertTrue(d.ViewObject.canDragIcon(SOLID_TIP))
        self.assertTrue(second.ViewObject.canDragIcon(BODY_TIP))
        self.assertFalse(d.ViewObject.canDragIcon("tree:vis"))
        self.assertFalse(second.ViewObject.canDragIcon(SOLID_TIP))

    def testSolidArrowStaysInItsSolid(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        self.assertTrue(c.ViewObject.canDropIcon(d, SOLID_TIP))
        self.assertFalse(a.ViewObject.canDropIcon(d, SOLID_TIP))
        self.assertFalse(first.ViewObject.canDropIcon(d, SOLID_TIP))
        self.assertFalse(d.ViewObject.canDropIcon(d, SOLID_TIP))

        c.ViewObject.dropIcon(d, SOLID_TIP)
        self.assertEqual(second.Tip, c)
        # the solid is the body's tip, so the body follows
        self.assertEqual(self.Body.Tip, c)
        self.assertEqual(names(second.Group), ["Box002", "Box003"])
        self.assertTrue(c.Visibility)
        self.assertFalse(d.Visibility)
        self.assertEqual(self.tips(c), [SOLID_TIP])
        self.assertEqual(self.tips(d), [])
        self.assertAlmostEqual(self.Body.Shape.Volume, c.Shape.Volume)

    def testBodyArrowGoesToAnySolid(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        self.assertTrue(a.ViewObject.canDropIcon(second, BODY_TIP))
        self.assertTrue(first.ViewObject.canDropIcon(second, BODY_TIP))
        self.assertTrue(c.ViewObject.canDropIcon(second, BODY_TIP))
        # where it is already
        self.assertFalse(d.ViewObject.canDropIcon(second, BODY_TIP))

        # onto a feature: that solid, rolled back to the feature
        a.ViewObject.dropIcon(second, BODY_TIP)
        self.assertEqual(self.Body.Tip, a)
        self.assertEqual(first.Tip, a)
        self.assertEqual(second.Tip, d)
        self.assertEqual(self.tips(first), [BODY_TIP])
        self.assertEqual(self.tips(second), [])
        self.assertAlmostEqual(self.Body.Shape.Volume, a.Shape.Volume)

        # onto a solid body: that solid, where it is rolled back to
        second.Tip = c
        second.ViewObject.dropIcon(first, BODY_TIP)
        self.assertEqual(self.Body.Tip, c)
        self.assertEqual(self.Body.TipSolidBody, second)
        self.assertEqual(first.Tip, a)

    def testOneSolidArrowMovesTheBodyTip(self):
        a = self.box("Box")
        b = self.box("Box001", 5)
        self.Doc.recompute()
        self.assertTrue(a.ViewObject.canDropIcon(b, SOLID_TIP))
        a.ViewObject.dropIcon(b, SOLID_TIP)
        self.assertEqual(self.Body.Tip, a)
        self.assertEqual(names(self.Body.Group), ["Box", "Box001"])

    def testTagOfABodyWithoutSolidBodies(self):
        self.Body.AddSolidBody = False
        a, b, c, d = self.twoSolids()
        self.assertEqual(self.Body.Tip, d)
        self.assertTrue(d.ViewObject.canDragIcon(TAG))
        # only the tag that marks the tip
        self.assertFalse(a.ViewObject.canDragIcon(TAG))
        self.assertTrue(a.ViewObject.canDropIcon(d, TAG))
        a.ViewObject.dropIcon(d, TAG)
        self.assertEqual(self.Body.Tip, a)

    def testNoDropAcrossBodies(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        other = self.Doc.addObject("PartDesign::Body", "Body001")
        feat = other.newObjectAt("PartDesign::AdditiveBox", "OtherBox")
        self.Doc.recompute()
        self.assertFalse(feat.ViewObject.canDropIcon(d, SOLID_TIP))
        self.assertFalse(feat.ViewObject.canDropIcon(second, BODY_TIP))

    # -- the solid body's own shape ------------------------------------------------------

    def testSolidShapeInPlaceOfTheFeatures(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        self.assertFalse(first.ViewObject.Visibility)
        first.ViewObject.Visibility = True
        self.assertFalse(a.Visibility or b.Visibility)
        self.assertEqual(self.Body.isElementVisible(first.Name), 1)
        b.ViewObject.Visibility = True
        self.assertFalse(first.Visibility)

    def testHiddenSolidComesBackAsItWasSeen(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        a.ViewObject.Visibility = True
        self.assertFalse(b.ViewObject.Visibility)
        # what the eye of the solid body's tree item does
        self.Body.setElementVisible(first.Name, False)
        self.assertFalse(a.ViewObject.Visibility or b.ViewObject.Visibility)
        self.Body.setElementVisible(first.Name, True)
        self.assertTrue(a.ViewObject.Visibility)
        self.assertFalse(b.ViewObject.Visibility or first.ViewObject.Visibility)
        # its own shape comes back the same way
        first.ViewObject.Visibility = True
        self.Body.setElementVisible(first.Name, False)
        self.assertFalse(first.ViewObject.Visibility)
        self.Body.setElementVisible(first.Name, True)
        self.assertTrue(first.ViewObject.Visibility)
        self.assertFalse(a.ViewObject.Visibility or b.ViewObject.Visibility)
        # and the other solid stayed as it was
        self.assertTrue(d.ViewObject.Visibility)

    # -- where a solid body is highlighted -----------------------------------------------
    #
    # A selection, a preselection and a show on top are all drawn on the nodes
    # ViewProvider.getDetailPath() ends at. The solid body's own are switched
    # off while its features draw the solid, so the path is theirs then.

    def path(self, sub, vobj=None):
        """What getDetailPath() answers: the objects whose root is on the
        path, whether every switch on it is on, and the detail"""
        from pivy import coin

        path = coin.SoPath()
        detail = (vobj or self.Body.ViewObject).getDetailPath(sub, path, True)
        nodes = [path.getNode(i) for i in range(path.getLength())]
        roots = {}
        for obj in self.Doc.Objects:
            roots[obj.ViewObject.RootNode.this.__int__()] = obj.Name
        on = True
        for node in nodes:
            if node.isOfType(coin.SoSwitch.getClassTypeId()):
                if coin.cast(node, "SoSwitch").whichChild.getValue() == coin.SO_SWITCH_NONE:
                    on = False
        names = [roots[n.this.__int__()] for n in nodes if n.this.__int__() in roots]
        return names, on, detail

    def faceIndex(self, detail):
        from pivy import coin

        self.assertIsNotNone(detail)
        return coin.cast(detail, "SoFaceDetail").getPartIndex()

    def testSolidBodyIsHighlightedOnItsTip(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        self.assertEqual(self.path("Solid001."), (["Body", "Box003"], True, None))
        self.assertEqual(self.path("Solid."), (["Body", "Box001"], True, None))
        # asked as the feature is: one row and the other end at the same nodes
        self.assertEqual(self.path("Solid001.")[0], self.path("Box003.")[0])
        self.assertEqual(self.path("Solid001.")[0], self.path("Solid001.Box003.")[0])

    def testElementOfSolidBodyIsHighlightedOnItsTip(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        for index in (1, 4, 6):
            names, on, detail = self.path("Solid001.Face%d" % index)
            self.assertEqual((names, on), (["Body", "Box003"], True))
            self.assertEqual(self.faceIndex(detail), index - 1)
        # the face it is on the solid body's own shape
        center = second.Shape.getElement("Face4").CenterOfMass
        self.assertTrue(center.isEqual(d.Shape.getElement("Face4").CenterOfMass, 1e-7))
        names, on, detail = self.path("Solid001.Edge3")
        self.assertEqual((names, on), (["Body", "Box003"], True))
        self.assertIsNotNone(detail)

    def testMappedElementOfSolidBody(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        mapped = second.Shape.getElementMappedName("Face4")
        self.assertTrue(mapped)
        # as a selection has it: the mapped name alone, and with the indexed
        # one behind it
        for sub in ("Solid001.;%s" % mapped, "Solid001.;%s.Face4" % mapped):
            names, on, detail = self.path(sub)
            self.assertEqual((names, on), (["Body", "Box003"], True), sub)
            self.assertEqual(self.faceIndex(detail), 3, sub)

    def testSolidSeenThroughAnotherFeature(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        c.ViewObject.Visibility = True
        self.assertFalse(d.Visibility)
        self.assertEqual(self.path("Solid001."), (["Body", "Box002"], True, None))
        # an element of the solid is not one of that feature: the solid, whole
        self.assertEqual(self.path("Solid001.Face1"), (["Body", "Box002"], True, None))
        # the other solid is where it was
        self.assertEqual(self.path("Solid."), (["Body", "Box001"], True, None))

    def testHiddenSolidIsLeftWithItsOwnNodes(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        d.ViewObject.Visibility = False
        self.assertFalse(c.Visibility or d.Visibility or second.Visibility)
        names, on, detail = self.path("Solid001.")
        self.assertEqual((names, on), (["Body", "Solid001"], False))

    def testShownSolidShapeIsHighlightedItself(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        second.ViewObject.Visibility = True
        self.assertFalse(c.Visibility or d.Visibility)
        self.assertEqual(self.path("Solid001."), (["Body", "Solid001"], True, None))
        names, on, detail = self.path("Solid001.Face4")
        self.assertEqual((names, on), (["Body", "Solid001"], True))
        self.assertEqual(self.faceIndex(detail), 3)

    def testFeatureUnderSolidBodyIsHighlightedAsBefore(self):
        a, b, c, d = self.twoSolids()
        names, on, detail = self.path("Solid001.Box003.Face4")
        self.assertEqual((names, on), (["Body", "Box003"], True))
        self.assertEqual(self.faceIndex(detail), 3)
        # a hidden feature is not on screen, and is not made to look so
        names, on, detail = self.path("Solid001.Box002.")
        self.assertEqual((names, on), (["Body", "Box002"], False))

    def testLinkToSolidBodyKeepsItsOwnNodes(self):
        a, b, c, d = self.twoSolids()
        first, second = self.Body.SolidBodies
        link = self.Doc.addObject("App::Link", "Link")
        link.LinkedObject = second
        self.Doc.recompute()
        for sub in ("", "Face4"):
            names, on, detail = self.path(sub, link.ViewObject)
            self.assertEqual(names, ["Link"], sub)
            self.assertTrue(on, sub)
        self.assertEqual(self.faceIndex(self.path("Face4", link.ViewObject)[2]), 3)
        # and the solid body is highlighted on its tip all the same
        self.assertEqual(self.path("Solid001.")[0], ["Body", "Box003"])

    def testSelectionOfSolidBodyStaysWhileItsFeaturesAreShownAndHidden(self):
        # The highlight is moved to the feature the solid is then seen through
        # (SolidBodyHighlight); the selection itself is not touched by that
        a, b, c, d = self.twoSolids()
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, "Body", "Solid001.")
        Gui.Selection.addSelection(self.Doc.Name, "Body", "Solid001.Face4")
        Gui.Selection.addSelection(self.Doc.Name, "Body", "Solid.")

        def selected():
            return sorted(
                sub for s in Gui.Selection.getSelectionEx("", 0) for sub in s.SubElementNames
            )

        before = selected()
        # the face by its mapped name, which is how a selection keeps it
        self.assertEqual(len(before), 3)
        self.assertEqual(before[:2], ["Solid.", "Solid001."])
        self.assertTrue(before[2].endswith(".Face4"))
        c.ViewObject.Visibility = True
        self.assertEqual(selected(), before)
        c.ViewObject.dropIcon(d, SOLID_TIP)
        self.assertEqual(selected(), before)
        d.ViewObject.dropIcon(c, SOLID_TIP)
        self.assertEqual(selected(), before)
        d.ViewObject.Visibility = False
        self.assertEqual(selected(), before)
        Gui.Selection.clearSelection()
