#***************************************************************************
#*   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU Lesser General Public License (LGPL)    *
#*   as published by the Free Software Foundation; either version 2 of     *
#*   the License, or (at your option) any later version.                   *
#*   for detail see the LICENCE text file.                                 *
#*                                                                         *
#*   This program is distributed in the hope that it will be useful,       *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU Library General Public License for more details.                  *
#*                                                                         *
#*   You should have received a copy of the GNU Library General Public     *
#*   License along with this program; if not, write to the Free Software   *
#*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
#*   USA                                                                   *
#*                                                                         *
#***************************************************************************

import unittest

import FreeCAD

App = FreeCAD

class TestBoolean(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestBoolean")

    def testBooleanFuseCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Box.Length=10
        self.Box.Width=10
        self.Box.Height=10
        self.Body.addObject(self.Box)
        self.Doc.recompute()
        self.Body001 = self.Doc.addObject('PartDesign::Body','Body001')
        self.Box001 = self.Doc.addObject('PartDesign::AdditiveBox','Box001')
        self.Box001.Length=10
        self.Box001.Width=10
        self.Box001.Height=10
        self.Box001.Placement.Base = App.Vector(-5,0,0)
        self.Body001.addObject(self.Box001)
        self.Doc.recompute()
        self.BooleanFuse = self.Doc.addObject('PartDesign::Boolean','BooleanFuse')
        self.Body001.addObject(self.BooleanFuse)
        self.Doc.recompute()
        self.BooleanFuse.setObjects([self.Body,])
        self.BooleanFuse.Type = 0
        self.Doc.recompute()
        self.assertAlmostEqual(self.BooleanFuse.Shape.Volume, 1500)

    def testBooleanCutCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Box.Length=10
        self.Box.Width=10
        self.Box.Height=10
        self.Body.addObject(self.Box)
        self.Doc.recompute()
        self.Body001 = self.Doc.addObject('PartDesign::Body','Body001')
        self.Box001 = self.Doc.addObject('PartDesign::AdditiveBox','Box001')
        self.Box001.Length=10
        self.Box001.Width=10
        self.Box001.Height=10
        self.Box001.Placement.Base = App.Vector(-5,0,0)
        self.Body001.addObject(self.Box001)
        self.Doc.recompute()
        self.BooleanCut = self.Doc.addObject('PartDesign::Boolean','BooleanCut')
        self.Body001.addObject(self.BooleanCut)
        self.Doc.recompute()
        self.BooleanCut.setObjects([self.Body,])
        self.BooleanCut.Type = 1
        self.Doc.recompute()
        self.assertAlmostEqual(self.BooleanCut.Shape.Volume, 500)

    def testBooleanCommonCase(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Box.Length=10
        self.Box.Width=10
        self.Box.Height=10
        self.Body.addObject(self.Box)
        self.Doc.recompute()
        self.Body001 = self.Doc.addObject('PartDesign::Body','Body001')
        self.Box001 = self.Doc.addObject('PartDesign::AdditiveBox','Box001')
        self.Box001.Length=10
        self.Box001.Width=10
        self.Box001.Height=10
        self.Box001.Placement.Base = App.Vector(-5,0,0)
        self.Body001.addObject(self.Box001)
        self.Doc.recompute()
        self.BooleanCommon = self.Doc.addObject('PartDesign::Boolean','BooleanCommon')
        self.Body001.addObject(self.BooleanCommon)
        self.Doc.recompute()
        self.BooleanCommon.setObjects([self.Body,])
        self.BooleanCommon.Type = 2
        self.Doc.recompute()
        self.assertAlmostEqual(self.BooleanCommon.Shape.Volume, 500)

    def testBooleanToolShape(self):
        """ToolShape holds what the edit preview draws: the tools alone,
        where the boolean puts them, and every tool when there is no base"""
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Box.Length=10
        self.Box.Width=10
        self.Box.Height=10
        self.Body.addObject(self.Box)
        self.Body.Placement.Base = App.Vector(0,0,20)
        self.Body001 = self.Doc.addObject('PartDesign::Body','Body001')
        self.Box001 = self.Doc.addObject('PartDesign::AdditiveBox','Box001')
        self.Box001.Length=10
        self.Box001.Width=10
        self.Box001.Height=10
        self.Box001.Placement.Base = App.Vector(-5,0,0)
        self.Body001.addObject(self.Box001)
        self.Doc.recompute()
        self.BooleanCut = self.Doc.addObject('PartDesign::Boolean','BooleanCut')
        self.Body001.addObject(self.BooleanCut)
        self.BooleanCut.setObjects([self.Body,])
        self.BooleanCut.Type = 1
        self.Doc.recompute()
        tool = self.BooleanCut.ToolShape
        self.assertAlmostEqual(tool.Volume, 1000)
        # the tool body is placed 20 up, so nothing is cut
        self.assertAlmostEqual(tool.BoundBox.ZMin, 20)
        self.assertAlmostEqual(self.BooleanCut.Shape.Volume, 1000)

        self.BooleanCut.NewSolid = True
        self.Doc.recompute()
        # no base: the base feature is added as a tool, and all are drawn
        self.assertAlmostEqual(self.BooleanCut.ToolShape.Volume, 2000)

    def testBooleanCutUsesActiveBodyPlacement(self):
        self.Body = self.Doc.addObject("PartDesign::Body", "ToolBody")
        self.Body.Placement.Base = App.Vector(25, 0, 0)
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "ToolBox")
        self.Box.Length = 10
        self.Box.Width = 10
        self.Box.Height = 10
        self.Body.addObject(self.Box)
        self.Doc.recompute()

        self.Body001 = self.Doc.addObject("PartDesign::Body", "ActiveBody")
        self.Body001.Placement.Base = App.Vector(20, 0, 0)
        self.Box001 = self.Doc.addObject("PartDesign::AdditiveBox", "BaseBox")
        self.Box001.Length = 10
        self.Box001.Width = 10
        self.Box001.Height = 10
        self.Body001.addObject(self.Box001)
        self.Doc.recompute()

        self.BooleanCut = self.Doc.addObject("PartDesign::Boolean", "BooleanCut")
        self.Body001.addObject(self.BooleanCut)
        self.BooleanCut.setObjects([self.Body])
        self.BooleanCut.Type = 1
        self.Doc.recompute()

        self.assertAlmostEqual(self.BooleanCut.Shape.Volume, 500)
        self.assertAlmostEqual(self.BooleanCut.Shape.BoundBox.XMin, 0)
        self.assertAlmostEqual(self.BooleanCut.Shape.BoundBox.XMax, 5)

    def testBooleanCutUsesActiveBodyGlobalPlacement(self):
        container = self.Doc.addObject("App::Part", "Part")
        container.Placement.Base = App.Vector(100, 0, 0)

        self.Body = self.Doc.addObject("PartDesign::Body", "ToolBody")
        container.addObject(self.Body)
        self.Body.Placement.Base = App.Vector(25, 0, 0)
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "ToolBox")
        self.Box.Length = 10
        self.Box.Width = 10
        self.Box.Height = 10
        self.Body.addObject(self.Box)
        self.Doc.recompute()

        self.Body001 = self.Doc.addObject("PartDesign::Body", "ActiveBody")
        container.addObject(self.Body001)
        self.Body001.Placement.Base = App.Vector(20, 0, 0)
        self.Box001 = self.Doc.addObject("PartDesign::AdditiveBox", "BaseBox")
        self.Box001.Length = 10
        self.Box001.Width = 10
        self.Box001.Height = 10
        self.Body001.addObject(self.Box001)
        self.Doc.recompute()

        self.BooleanCut = self.Doc.addObject("PartDesign::Boolean", "BooleanCut")
        self.Body001.addObject(self.BooleanCut)
        self.BooleanCut.setObjects([self.Body])
        self.assertIn(self.Body, container.Group)
        self.BooleanCut.Type = 1
        self.Doc.recompute()

        self.assertAlmostEqual(self.BooleanCut.Shape.Volume, 500)
        self.assertAlmostEqual(self.BooleanCut.Shape.BoundBox.XMin, 0)
        self.assertAlmostEqual(self.BooleanCut.Shape.BoundBox.XMax, 5)

    def testBooleanCutUsesNestedContainerGlobalPlacement(self):
        outer = self.Doc.addObject("App::Part", "OuterPart")
        outer.Placement.Base = App.Vector(100, 0, 0)
        inner = self.Doc.addObject("App::Part", "InnerPart")
        outer.addObject(inner)
        inner.Placement.Base = App.Vector(10, 0, 0)

        self.Body = self.Doc.addObject("PartDesign::Body", "ToolBody")
        inner.addObject(self.Body)
        self.Body.Placement.Base = App.Vector(25, 0, 0)
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "ToolBox")
        self.Box.Length = 10
        self.Box.Width = 10
        self.Box.Height = 10
        self.Body.addObject(self.Box)
        self.Doc.recompute()

        self.Body001 = self.Doc.addObject("PartDesign::Body", "ActiveBody")
        inner.addObject(self.Body001)
        self.Body001.Placement.Base = App.Vector(20, 0, 0)
        self.Box001 = self.Doc.addObject("PartDesign::AdditiveBox", "BaseBox")
        self.Box001.Length = 10
        self.Box001.Width = 10
        self.Box001.Height = 10
        self.Body001.addObject(self.Box001)
        self.Doc.recompute()

        self.BooleanCut = self.Doc.addObject("PartDesign::Boolean", "BooleanCut")
        self.Body001.addObject(self.BooleanCut)
        self.BooleanCut.setObjects([self.Body])
        self.assertIn(self.Body, inner.Group)
        self.BooleanCut.Type = 1
        self.Doc.recompute()

        self.assertAlmostEqual(self.BooleanCut.Shape.Volume, 500)
        self.assertAlmostEqual(self.BooleanCut.Shape.BoundBox.XMin, 0)
        self.assertAlmostEqual(self.BooleanCut.Shape.BoundBox.XMax, 5)

    def testBooleanCutUsesDifferentContainerGlobalPlacements(self):
        tool_container = self.Doc.addObject("App::Part", "ToolContainer")
        tool_container.Placement.Base = App.Vector(100, 0, 0)
        active_container = self.Doc.addObject("App::Part", "ActiveContainer")
        active_container.Placement.Base = App.Vector(200, 0, 0)

        self.Body = self.Doc.addObject("PartDesign::Body", "ToolBody")
        tool_container.addObject(self.Body)
        self.Body.Placement.Base = App.Vector(25, 0, 0)
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "ToolBox")
        self.Box.Length = 10
        self.Box.Width = 10
        self.Box.Height = 10
        self.Body.addObject(self.Box)
        self.Doc.recompute()

        self.Body001 = self.Doc.addObject("PartDesign::Body", "ActiveBody")
        active_container.addObject(self.Body001)
        self.Body001.Placement.Base = App.Vector(-80, 0, 0)
        self.Box001 = self.Doc.addObject("PartDesign::AdditiveBox", "BaseBox")
        self.Box001.Length = 10
        self.Box001.Width = 10
        self.Box001.Height = 10
        self.Body001.addObject(self.Box001)
        self.Doc.recompute()

        self.BooleanCut = self.Doc.addObject("PartDesign::Boolean", "BooleanCut")
        self.Body001.addObject(self.BooleanCut)
        self.BooleanCut.setObjects([self.Body])
        self.assertIn(self.Body, tool_container.Group)
        self.BooleanCut.Type = 1
        self.Doc.recompute()

        self.assertAlmostEqual(self.BooleanCut.Shape.Volume, 500)
        self.assertAlmostEqual(self.BooleanCut.Shape.BoundBox.XMin, 0)
        self.assertAlmostEqual(self.BooleanCut.Shape.BoundBox.XMax, 5)

    def testBooleanFirstFeatureUsesSourcePlacement(self):
        box = self.Doc.addObject("Part::Box", "Box")
        box.Length = 10
        box.Width = 10
        box.Height = 10
        box.Placement.Base = App.Vector(20, 0, 0)

        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.BooleanFuse = self.Doc.addObject("PartDesign::Boolean", "BooleanFuse")
        self.Body.addObject(self.BooleanFuse)
        self.assertFalse(self.BooleanFuse.UseLegacyBodyPlacement)
        self.BooleanFuse.setObjects([box])
        self.BooleanFuse.Type = 0
        self.Doc.recompute()

        self.assertAlmostEqual(self.Body.Shape.BoundBox.XMin, 20)
        self.assertAlmostEqual(self.Body.Shape.BoundBox.XMax, 30)

    def testBooleanFirstFeatureUsesSubShapeBinderPlacement(self):
        box = self.Doc.addObject("Part::Box", "Box")
        box.Length = 10
        box.Width = 10
        box.Height = 10
        box.Placement.Base = App.Vector(20, 0, 0)

        binder = self.Doc.addObject("PartDesign::SubShapeBinder", "Binder")
        binder.Support = [box]
        self.Doc.recompute()

        self.assertAlmostEqual(binder.Shape.BoundBox.XMin, 20)
        self.assertAlmostEqual(binder.Shape.BoundBox.XMax, 30)

        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.BooleanFuse = self.Doc.addObject("PartDesign::Boolean", "BooleanFuse")
        self.Body.addObject(self.BooleanFuse)
        self.assertFalse(self.BooleanFuse.UseLegacyBodyPlacement)
        self.BooleanFuse.setObjects([binder])
        self.BooleanFuse.Type = 0
        self.Doc.recompute()

        self.assertAlmostEqual(self.Body.Shape.BoundBox.XMin, 20)
        self.assertAlmostEqual(self.Body.Shape.BoundBox.XMax, 30)

    def testBooleanLegacyPlacementKeepsOldFirstFeatureBehavior(self):
        box = self.Doc.addObject("Part::Box", "Box")
        box.Length = 10
        box.Width = 10
        box.Height = 10
        box.Placement.Base = App.Vector(20, 0, 0)

        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.BooleanFuse = self.Doc.addObject("PartDesign::Boolean", "BooleanFuse")
        self.Body.addObject(self.BooleanFuse)
        self.BooleanFuse.UseLegacyBodyPlacement = True
        self.BooleanFuse.setObjects([box])
        self.BooleanFuse.Type = 0
        self.Doc.recompute()

        self.assertAlmostEqual(self.Body.Shape.BoundBox.XMin, 20)
        self.assertAlmostEqual(self.Body.Shape.BoundBox.XMax, 30)

    def testBooleanDeleteKeepsReferencedTool(self):
        # The Boolean deletes the tools it owns (its binders), not a body it
        # only refers to
        self.Body = self.Doc.addObject("PartDesign::Body", "ToolBody")
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "ToolBox")
        self.Body.addObject(self.Box)
        self.Body001 = self.Doc.addObject("PartDesign::Body", "ActiveBody")
        self.Box001 = self.Doc.addObject("PartDesign::AdditiveBox", "BaseBox")
        self.Body001.addObject(self.Box001)
        self.BooleanCut = self.Doc.addObject("PartDesign::Boolean", "BooleanCut")
        self.Body001.addObject(self.BooleanCut)
        self.BooleanCut.setObjects([self.Body])
        self.Doc.recompute()
        self.Body001.removeObject(self.BooleanCut)
        self.Doc.removeObject(self.BooleanCut.Name)
        self.assertIsNotNone(self.Doc.getObject("ToolBody"))

    def testBooleanOwnsItsBinder(self):
        # What the Boolean command makes: a SubShapeBinder per tool, owned by
        # the Boolean and drawn in its frame, as before
        self.Body = self.Doc.addObject("PartDesign::Body", "ToolBody")
        self.Body.Placement.Base = App.Vector(25, 0, 0)
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "ToolBox")
        self.Body.addObject(self.Box)
        self.Body001 = self.Doc.addObject("PartDesign::Body", "ActiveBody")
        self.Body001.Placement.Base = App.Vector(20, 0, 0)
        self.Box001 = self.Doc.addObject("PartDesign::AdditiveBox", "BaseBox")
        self.Body001.addObject(self.Box001)
        self.Doc.recompute()
        self.BooleanCut = self.Doc.addObject("PartDesign::Boolean", "BooleanCut")
        self.Body001.addObject(self.BooleanCut)
        self.BooleanCut.Type = 1
        binder = self.Body001.newObject("PartDesign::SubShapeBinder", "Reference")
        self.BooleanCut.addObject(binder)
        binder.Support = [self.Body]
        self.Doc.recompute()
        self.assertIn(binder, self.BooleanCut.Group)
        self.assertNotIn(binder, self.Body001.Group)
        self.assertAlmostEqual(self.BooleanCut.Shape.Volume, 500)
        self.assertAlmostEqual(self.BooleanCut.Shape.BoundBox.XMax, 5)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestBoolean")
        #print ("omit closing document for debugging")

