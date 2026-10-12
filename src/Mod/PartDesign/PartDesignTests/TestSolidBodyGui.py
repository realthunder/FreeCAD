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
arrows that mark the tips, and moving a tip by dropping its arrow.

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
