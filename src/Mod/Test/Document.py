# ***************************************************************************
# *   Copyright (c) 2003 Juergen Riegel <juergen.riegel@web.de>             *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful,            *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with FreeCAD; if not, write to the Free Software        *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************/

import FreeCAD, os, unittest, tempfile
import math

# ---------------------------------------------------------------------------
# define the functions to test the FreeCAD Document code
# ---------------------------------------------------------------------------


class Proxy:
    def __init__(self, obj):
        self.Dictionary = {}
        self.obj = obj
        obj.Proxy = self

    def dumps(self):
        return self.Dictionary

    def loads(self, data):
        self.Dictionary = data

def optimizeRecompute():
    # Once OptimizeRecompute is enabled, execute will only be called if there is
    # actually any property changes, or enforceRecompute() has been called.
    return FreeCAD.ParamGet(
            'User parameter:BaseApp/Preferences/Document').GetBool(
                    'OptimizeRecompute',True)

def transactionLogIsOn():
    # With the log on, a bare write outside any invocation opens an implicit
    # transaction, and observers see it open and commit like any other
    # (docs/TransactionLog.md sec 24.13).
    return FreeCAD.ParamGet(
            'User parameter:BaseApp/Preferences/Document').GetInt(
                    'TransactionLog',2) != 0

class DocumentBasicCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("CreateTest")

    def saveAndRestore(self):
        # saving and restoring
        SaveName = tempfile.gettempdir() + os.sep + "CreateTest.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("CreateTest")
        self.Doc = FreeCAD.open(SaveName)
        return self.Doc

    def testNewDocumentLabelOnlyRenamedOnClash(self):
        # Another open document ("CreateTest") must not rename a new one;
        # only a label that is already taken gets a suffix.
        first = FreeCAD.newDocument("LabelNoClash")
        second = None
        try:
            self.assertEqual(first.Label, "LabelNoClash")
            second = FreeCAD.newDocument("LabelNoClash")
            self.assertEqual(second.Label, "LabelNoClash1")
        finally:
            if second:
                FreeCAD.closeDocument(second.Name)
            FreeCAD.closeDocument(first.Name)

    def testAccessByNameOrID(self):
        obj = self.Doc.addObject("App::DocumentObject", "MyName")

        with self.assertRaises(TypeError):
            self.Doc.getObject([1])

        self.assertEqual(self.Doc.getObject(obj.Name), obj)
        self.assertEqual(self.Doc.getObject("Unknown"), None)
        self.assertEqual(self.Doc.getObject(obj.ID), obj)
        self.assertEqual(self.Doc.getObject(obj.ID + 1), None)

    def testCreateDestroy(self):
        # FIXME: Causes somehow a ref count error but it's _not_ FreeCAD.getDocument()!!!
        # If we remove the whole method no error appears.
        self.assertTrue(FreeCAD.getDocument("CreateTest") is not None, "Creating Document failed")

    def testAddition(self):
        # Cannot write a real test case for that but when debugging the
        # C-code there shouldn't be a memory leak (see rev. 1814)
        self.Doc.openTransaction("Add")
        L1 = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.commitTransaction()
        self.Doc.undo()

    def testAddRemoveUndo(self):
        # Bug #0000525
        self.Doc.openTransaction("Add")
        obj = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.commitTransaction()
        self.Doc.removeObject(obj.Name)
        self.Doc.undo()
        self.Doc.undo()

    def testNoRecompute(self):
        L1 = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.recompute()
        L1.TypeNoRecompute = 2
        execcount = L1.ExecCount
        objectcount = self.Doc.recompute()
        self.assertEqual(objectcount, 0)
        self.assertEqual(L1.ExecCount, execcount)

    def testNoRecomputeParent(self):
        L1 = self.Doc.addObject("App::FeatureTest", "Child")
        L2 = self.Doc.addObject("App::FeatureTest", "Parent")
        L2.Source1 = L1
        self.Doc.recompute()
        L1.TypeNoRecompute = 2
        countChild = L1.ExecCount
        countParent = L2.ExecCount
        objectcount = self.Doc.recompute()
        self.assertEqual(objectcount, 1)
        self.assertEqual(L1.ExecCount, countChild)
        self.assertEqual(L2.ExecCount, countParent + 1)

        L1.touch("")
        countChild = L1.ExecCount
        countParent = L2.ExecCount
        objectcount = self.Doc.recompute()
        self.assertEqual(objectcount, 1)
        self.assertEqual(L1.ExecCount, countChild)
        if optimizeRecompute():
            self.assertEqual(L2.ExecCount, countParent)
        else:
            self.assertEqual(L2.ExecCount, countParent+1)

        L1.enforceRecompute()
        countChild = L1.ExecCount
        countParent = L2.ExecCount
        objectcount = self.Doc.recompute()
        self.assertEqual(objectcount, 2)
        self.assertEqual(L1.ExecCount, countChild + 1)
        self.assertEqual(L2.ExecCount, countParent + 1)

    def testAbortTransaction(self):
        self.Doc.openTransaction("Add")
        obj = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.abortTransaction()
        TempPath = tempfile.gettempdir()
        SaveName = TempPath + os.sep + "SaveRestoreTests.FCStd"
        self.Doc.saveAs(SaveName)

    def testRemoval(self):
        # Cannot write a real test case for that but when debugging the
        # C-code there shouldn't be a memory leak (see rev. 1814)
        self.Doc.openTransaction("Add")
        L1 = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.commitTransaction()
        self.Doc.openTransaction("Rem")
        L1 = self.Doc.removeObject("Label")
        self.Doc.commitTransaction()

    def testObjects(self):
        L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        # call members to check for errors in ref counting
        self.Doc.ActiveObject
        self.Doc.Objects
        self.Doc.UndoMode
        self.Doc.UndoRedoMemSize
        self.Doc.UndoCount
        # test read only mechanismus
        try:
            self.Doc.UndoCount = 3
        except Exception:
            FreeCAD.Console.PrintLog("   exception thrown, OK\n")
        else:
            self.fail("no exception thrown")
        self.Doc.RedoCount
        self.Doc.UndoNames
        self.Doc.RedoNames
        self.Doc.recompute()
        self.assertTrue(L1.Integer == 4711)
        self.assertTrue(L1.Float - 47.11 < 0.001)
        self.assertTrue(L1.Bool == True)
        self.assertTrue(L1.String == "4711")
        # temporarily not checked because of strange behavior of boost::filesystem JR
        # self.assertTrue(L1.Path  == "c:/temp")
        self.assertTrue(float(L1.Angle) - 3.0 < 0.001)
        self.assertTrue(float(L1.Distance) - 47.11 < 0.001)

        # test basic property stuff
        self.assertTrue(not L1.getDocumentationOfProperty("Source1") == "")
        self.assertTrue(L1.getGroupOfProperty("Source1") == "Feature Test")
        self.assertTrue(L1.getTypeOfProperty("Source1") == [])
        self.assertTrue(L1.getEnumerationsOfProperty("Source1") is None)

        # test the constraint types ( both are constraint to percent range)
        self.assertTrue(L1.ConstraintInt == 5)
        self.assertTrue(L1.ConstraintFloat - 5.0 < 0.001)
        L1.ConstraintInt = 500
        L1.ConstraintFloat = 500.0
        self.assertTrue(L1.ConstraintInt == 100)
        self.assertTrue(L1.ConstraintFloat - 100.0 < 0.001)
        L1.ConstraintInt = -500
        L1.ConstraintFloat = -500.0
        self.assertTrue(L1.ConstraintInt == 0)
        self.assertTrue(L1.ConstraintFloat - 0.0 < 0.001)

        # test enum property
        # in App::FeatureTest the current value is set to 4
        self.assertTrue(L1.Enum == "Four")
        L1.Enum = "Three"
        self.assertTrue(L1.Enum == "Three", "Different value to 'Three'")
        L1.Enum = 2
        self.assertTrue(L1.Enum == "Two", "Different value to 'Two'")
        try:
            L1.Enum = "SurelyNotInThere!"
        except Exception:
            FreeCAD.Console.PrintLog("   exception thrown, OK\n")
        else:
            self.fail("no exception thrown")
        self.assertTrue(
            sorted(L1.getEnumerationsOfProperty("Enum"))
            == sorted(["Zero", "One", "Two", "Three", "Four"])
        )

        # self.assertTrue(L1.IntegerList  == [4711]   )
        # f = L1.FloatList
        # self.assertTrue(f -47.11<0.001    )
        # self.assertTrue(L1.Matrix  == [1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0,10.0,11.0,12.0,13.0,14.0,15.0,16.0] )
        # self.assertTrue(L1.Vector  == [1.0,2.0,3.0])

        self.assertTrue(L1.Label == "Label_1", "Invalid object name")
        L1.Label = "Label_2"
        self.Doc.recompute()
        self.assertTrue(L1.Label == "Label_2", "Invalid object name")
        self.Doc.removeObject("Label_1")

    def testEnum(self):
        enumeration_choices = ["one", "two"]
        obj = self.Doc.addObject("App::FeaturePython", "Label_2")
        obj.addProperty("App::PropertyEnumeration", "myEnumeration", "Enum", "mytest")
        with self.assertRaises(ValueError):
            obj.myEnumeration = enumeration_choices[0]

        obj.myEnumeration = enumeration_choices
        obj.myEnumeration = 0
        self.Doc.openTransaction("Modify enum")
        obj.myEnumeration = 1
        self.assertTrue(obj.myEnumeration, enumeration_choices[1])
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.assertTrue(obj.myEnumeration, enumeration_choices[0])

    def testWrongTypes(self):
        with self.assertRaises(TypeError):
            self.Doc.addObject("App::DocumentObjectExtension")

        class Feature:
            pass

        with self.assertRaises(TypeError):
            self.Doc.addObject(type="App::DocumentObjectExtension", objProxy=Feature(), attach=True)

        ext = FreeCAD.Base.TypeId.fromName("App::DocumentObjectExtension")
        self.assertEqual(ext.createInstance(), None)

        obj = self.Doc.addObject("App::FeaturePython", "Object")
        with self.assertRaises(TypeError):
            obj.addProperty("App::DocumentObjectExtension", "Property")

        with self.assertRaises(TypeError):
            self.Doc.findObjects(Type="App::DocumentObjectExtension")

        e = FreeCAD.Base.TypeId.fromName("App::LinkExtensionPython")
        self.assertIsNone(e.createInstance())

        if FreeCAD.GuiUp:
            obj = self.Doc.addObject("App::DocumentObject", viewType="App::Extension")
            self.assertIsNone(obj.ViewObject)

    def testDuplicateLinks(self):
        obj = self.Doc.addObject("App::FeatureTest","obj")
        grp = self.Doc.addObject("App::DocumentObjectGroup","group")
        try:
            grp.Group = [obj,obj]
        except Exception:
            pass
        self.assertListEqual(grp.Group, [obj])
        self.Doc.removeObject(obj.Name)
        self.assertListEqual(grp.Group, [])

    def testMem(self):
        self.Doc.MemSize

    def testPlacementList(self):
        obj = self.Doc.addObject("App::FeaturePython", "Label")
        obj.addProperty("App::PropertyPlacementList", "PlmList")
        plm = FreeCAD.Placement()
        plm.Base = (1, 2, 3)
        plm.Rotation = (0, 0, 1, 0)
        obj.PlmList = [plm]
        cpy = self.Doc.copyObject(obj)
        self.assertListEqual(obj.PlmList, cpy.PlmList)

    def testRawAxis(self):
        obj = self.Doc.addObject("App::FeaturePython", "Label")
        obj.addProperty("App::PropertyPlacement", "Plm")
        obj.addProperty("App::PropertyRotation", "Rot")
        obj.Plm.Rotation.Axis = (1, 2, 3)
        obj.Rot.Axis = (3, 2, 1)

        # saving and restoring
        SaveName = tempfile.gettempdir() + os.sep + "CreateTest.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("CreateTest")
        self.Doc = FreeCAD.open(SaveName)
        obj = self.Doc.ActiveObject

        self.assertEqual(obj.Plm.Rotation.RawAxis.x, 1)
        self.assertEqual(obj.Plm.Rotation.RawAxis.y, 2)
        self.assertEqual(obj.Plm.Rotation.RawAxis.z, 3)

        self.assertEqual(obj.Rot.RawAxis.x, 3)
        self.assertEqual(obj.Rot.RawAxis.y, 2)
        self.assertEqual(obj.Rot.RawAxis.z, 1)

    def testAddRemove(self):
        L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        # must delete object
        self.Doc.removeObject(L1.Name)
        try:
            L1.Name
        except Exception:
            self.assertTrue(True)
        else:
            self.assertTrue(False)
        del L1

        # What do we expect here?
        self.Doc.openTransaction("AddRemove")
        L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        self.Doc.removeObject(L2.Name)
        self.Doc.commitTransaction()
        self.Doc.undo()
        try:
            L2.Name
        except Exception:
            self.assertTrue(True)
        else:
            self.assertTrue(False)
        del L2

    def testSubObject(self):
        obj = self.Doc.addObject("App::Origin", "Origin")
        self.Doc.recompute()

        res = obj.getSubObject("X_Axis.", retType=2)
        self.assertEqual(
            res[1].multVec(FreeCAD.Vector(1, 0, 0)).getAngle(FreeCAD.Vector(1, 0, 0)), 0.0
        )

        res = obj.getSubObject("Y_Axis.", retType=2)
        self.assertEqual(
            res[1].multVec(FreeCAD.Vector(1, 0, 0)).getAngle(FreeCAD.Vector(0, 1, 0)), 0.0
        )

        res = obj.getSubObject("Z_Axis.", retType=2)
        self.assertEqual(
            res[1].multVec(FreeCAD.Vector(1, 0, 0)).getAngle(FreeCAD.Vector(0, 0, 1)), 0.0
        )

        res = obj.getSubObject("XY_Plane.", retType=2)
        self.assertEqual(
            res[1].multVec(FreeCAD.Vector(0, 0, 1)).getAngle(FreeCAD.Vector(0, 0, 1)), 0.0
        )

        res = obj.getSubObject("XZ_Plane.", retType=2)
        self.assertEqual(
            res[1].multVec(FreeCAD.Vector(0, 0, 1)).getAngle(FreeCAD.Vector(0, -1, 0)), 0.0
        )

        res = obj.getSubObject("YZ_Plane.", retType=2)
        self.assertEqual(
            res[1].multVec(FreeCAD.Vector(0, 0, 1)).getAngle(FreeCAD.Vector(1, 0, 0)), 0.0
        )

        res = obj.getSubObject("YZ_Plane.", retType=3)
        self.assertEqual(
            res.multVec(FreeCAD.Vector(0, 0, 1)).getAngle(FreeCAD.Vector(1, 0, 0)), 0.0
        )

        res = obj.getSubObject("YZ_Plane.", retType=4)
        self.assertEqual(
            res.multVec(FreeCAD.Vector(0, 0, 1)).getAngle(FreeCAD.Vector(1, 0, 0)), 0.0
        )

        self.assertEqual(
            obj.getSubObject(("XY_Plane.", "YZ_Plane."), retType=4)[0],
            obj.getSubObject("XY_Plane.", retType=4),
        )
        self.assertEqual(
            obj.getSubObject(("XY_Plane.", "YZ_Plane."), retType=4)[1],
            obj.getSubObject("YZ_Plane.", retType=4),
        )

        # Create a second origin object
        obj2 = self.Doc.addObject("App::Origin", "Origin2")
        self.Doc.recompute()

        # Use the names of the origin's features. They are not in OutList here:
        # OriginFeatures is a Prop_Output link, so it declares no dependency, and
        # iterating OutList would leave this check testing nothing at all.
        # Seven: three axes, three planes and the origin point that came
        # with App::Point in the Datums port.
        self.assertEqual(len(obj2.OriginFeatures), 7)
        # a subname names a whole object only with the trailing '.', the same rule
        # GroupExtension applies -- without it the name does not resolve
        for i in obj2.OriginFeatures:
            self.assertEqual(obj2.getSubObject(i.Name + ".", retType=1).Name, i.Name)

    def testExtensions(self):
        # we try to create a normal python object and add an extension to it
        obj = self.Doc.addObject("App::DocumentObject", "Extension_1")
        grp = self.Doc.addObject("App::DocumentObject", "Extension_2")
        # we should have all methods we need to handle extensions
        try:
            self.assertTrue(not grp.hasExtension("App::GroupExtensionPython"))
            grp.addExtension("App::GroupExtensionPython")
            self.assertTrue(grp.hasExtension("App::GroupExtension"))
            self.assertTrue(grp.hasExtension("App::GroupExtensionPython"))
            grp.addObject(obj)
            self.assertTrue(len(grp.Group) == 1)
            self.assertTrue(grp.Group[0] == obj)
        except Exception:
            self.assertTrue(False)

        # test if the method override works
        class SpecialGroup:
            def allowObject(self, obj):
                return False

        callback = SpecialGroup()
        grp2 = self.Doc.addObject("App::FeaturePython", "Extension_3")
        grp2.addExtension("App::GroupExtensionPython")
        grp2.Proxy = callback

        try:
            self.assertTrue(grp2.hasExtension("App::GroupExtension"))
            grp2.addObject(obj)
            self.assertTrue(len(grp2.Group) == 0)
        except Exception:
            self.assertTrue(True)

        self.Doc.removeObject(grp.Name)
        self.Doc.removeObject(grp2.Name)
        self.Doc.removeObject(obj.Name)
        del obj
        del grp
        del grp2

    def testExtensionBug0002785(self):
        class MyExtension:
            def __init__(self, obj):
                obj.addExtension("App::GroupExtensionPython")

        obj = self.Doc.addObject("App::DocumentObject", "myObj")
        MyExtension(obj)
        self.assertTrue(obj.hasExtension("App::GroupExtension"))
        self.assertTrue(obj.hasExtension("App::GroupExtensionPython"))
        self.Doc.removeObject(obj.Name)
        del obj

    def testExtensionGroup(self):
        obj = self.Doc.addObject("App::DocumentObject", "Obj")
        grp = self.Doc.addObject("App::FeaturePython", "Extension_2")
        grp.addExtension("App::GroupExtensionPython")
        grp.Group = [obj]
        self.assertTrue(obj in grp.Group)

    def testExtensionBugViewProvider(self):
        class Layer:
            def __init__(self, obj):
                obj.addExtension("App::GroupExtensionPython")

        class LayerViewProvider:
            def __init__(self, obj):
                obj.addExtension("Gui::ViewProviderGroupExtensionPython")
                obj.Proxy = self

        obj = self.Doc.addObject("App::FeaturePython", "Layer")
        Layer(obj)
        self.assertTrue(obj.hasExtension("App::GroupExtension"))

        if FreeCAD.GuiUp:
            LayerViewProvider(obj.ViewObject)
            self.assertTrue(obj.ViewObject.hasExtension("Gui::ViewProviderGroupExtension"))
            self.assertTrue(obj.ViewObject.hasExtension("Gui::ViewProviderGroupExtensionPython"))

        self.Doc.removeObject(obj.Name)
        del obj

    def testHasSelection(self):
        if FreeCAD.GuiUp:
            import FreeCADGui

            self.assertFalse(FreeCADGui.Selection.hasSelection("", 1))

    def testPropertyLink_Issue2902Part1(self):
        o1 = self.Doc.addObject("App::FeatureTest", "test1")
        o2 = self.Doc.addObject("App::FeatureTest", "test2")
        o3 = self.Doc.addObject("App::FeatureTest", "test3")

        o1.Link = o2
        self.assertEqual(o1.Link, o2)
        o1.Link = o3
        self.assertEqual(o1.Link, o3)
        o2.Placement = FreeCAD.Placement()
        self.assertEqual(o1.Link, o3)

    def testProp_NonePropertyLink(self):
        obj1 = self.Doc.addObject("App::FeaturePython", "Obj1")
        obj2 = self.Doc.addObject("App::FeaturePython", "Obj2")
        obj1.addProperty(
            "App::PropertyLink",
            "Link",
            "Base",
            "Link to another feature",
            FreeCAD.PropertyType.Prop_None,
            False,
            False,
        )
        obj1.Link = obj2
        self.assertEqual(obj1.MustExecute, True)

    def testProp_OutputPropertyLink(self):
        obj1 = self.Doc.addObject("App::FeaturePython", "Obj1")
        obj2 = self.Doc.addObject("App::FeaturePython", "Obj2")
        obj1.addProperty(
            "App::PropertyLink",
            "Link",
            "Base",
            "Link to another feature",
            FreeCAD.PropertyType.Prop_Output,
            False,
            False,
        )
        obj1.Link = obj2
        self.assertEqual(obj1.MustExecute, False)

    def testAttributeOfDynamicProperty(self):
        obj = self.Doc.addObject("App::FeaturePython", "Obj")
        # Prop_NoPersist is the enum with the highest value
        max_value = FreeCAD.PropertyType.Prop_NoPersist
        list_of_types = []
        for i in range(0, max_value + 1):
            obj.addProperty("App::PropertyString", "String" + str(i), "", "", i)
            list_of_types.append(obj.getTypeOfProperty("String" + str(i)))

        # saving and restoring
        SaveName = tempfile.gettempdir() + os.sep + "CreateTest.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("CreateTest")
        self.Doc = FreeCAD.open(SaveName)

        obj = self.Doc.ActiveObject
        for i in range(0, max_value):
            types = obj.getTypeOfProperty("String" + str(i))
            self.assertEqual(list_of_types[i], types)

        # A property with flag Prop_NoPersist won't be saved to the file
        with self.assertRaises(AttributeError):
            obj.getTypeOfProperty("String" + str(max_value))

    def testNotification_Issue2902Part2(self):
        o = self.Doc.addObject("App::FeatureTest", "test")

        plm = o.Placement
        o.Placement = FreeCAD.Placement()
        plm.Base.x = 5
        self.assertEqual(o.Placement.Base.x, 0)
        o.Placement.Base.x = 5
        self.assertEqual(o.Placement.Base.x, 5)

    def testNotification_Issue2996(self):
        if not FreeCAD.GuiUp:
            return
        # works only if Gui is shown
        class ViewProvider:
            def __init__(self, vobj):
                vobj.Proxy = self

            def attach(self, vobj):
                self.ViewObject = vobj
                self.Object = vobj.Object

            def claimChildren(self):
                children = [self.Object.Link]
                return children

        obj = self.Doc.addObject("App::FeaturePython", "Sketch")
        obj.addProperty("App::PropertyLink", "Link")
        ViewProvider(obj.ViewObject)

        ext = self.Doc.addObject("App::FeatureTest", "Extrude")
        ext.Link = obj

        sli = self.Doc.addObject("App::FeaturePython", "Slice")
        sli.addProperty("App::PropertyLink", "Link").Link = ext
        ViewProvider(sli.ViewObject)

        com = self.Doc.addObject("App::FeaturePython", "CompoundFilter")
        com.addProperty("App::PropertyLink", "Link").Link = sli
        ViewProvider(com.ViewObject)

        ext.Label = "test"

        self.assertEqual(ext.Link, obj)
        self.assertNotEqual(ext.Link, sli)

    def testIssue4823(self):
        # https://forum.freecad.org/viewtopic.php?f=3&t=52775
        # The issue was only visible in GUI mode and it crashed in the tree view
        obj = self.Doc.addObject("App::Origin")
        self.Doc.removeObject(obj.Name)

    def testSamePropertyOfLinkAndLinkedObject(self):
        # See also https://github.com/FreeCAD/FreeCAD/pull/6787
        test = self.Doc.addObject("App::FeaturePython", "Python")
        link = self.Doc.addObject("App::Link", "Link")
        test.addProperty("App::PropertyFloat", "Test")
        link.addProperty("App::PropertyFloat", "Test")
        link.LinkedObject = test
        # saving and restoring
        SaveName = tempfile.gettempdir() + os.sep + "CreateTest.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("CreateTest")
        self.Doc = FreeCAD.open(SaveName)
        self.assertIn("Test", self.Doc.Python.PropertiesList)
        self.assertIn("Test", self.Doc.Link.PropertiesList)

    def testNoProxy(self):
        test = self.Doc.addObject("App::DocumentObject", "Object")
        test.addProperty("App::PropertyPythonObject", "Dictionary")
        test.Dictionary = {"Stored data": [3, 5, 7]}

        doc = self.saveAndRestore()
        obj = doc.Object

        self.assertEqual(obj.Dictionary, {"Stored data": [3, 5, 7]})

    def testWithProxy(self):
        test = self.Doc.addObject("App::FeaturePython", "Python")
        proxy = Proxy(test)
        proxy.Dictionary["Stored data"] = [3, 5, 7]

        doc = self.saveAndRestore()
        obj = doc.Python.Proxy

        self.assertEqual(obj.Dictionary, {"Stored data": [3, 5, 7]})

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("CreateTest")


# class must be defined in global scope to allow it to be reloaded on document open
class SaveRestoreSpecialGroup:
    def __init__(self, obj):
        obj.addExtension("App::GroupExtensionPython")
        obj.Proxy = self

    def allowObject(self, obj):
        return False


# class must be defined in global scope to allow it to be reloaded on document open
class SaveRestoreSpecialGroupViewProvider:
    def __init__(self, obj):
        obj.addExtension("Gui::ViewProviderGroupExtensionPython")
        obj.Proxy = self

    def testFunction(self):
        pass


class DocumentSaveRestoreCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("SaveRestoreTests")
        L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        L3 = self.Doc.addObject("App::FeatureTest", "Label_3")
        self.TempPath = tempfile.gettempdir()
        FreeCAD.Console.PrintLog("  Using temp path: " + self.TempPath + "\n")

    def testSaveAndRestore(self):
        # saving and restoring
        SaveName = self.TempPath + os.sep + "SaveRestoreTests.FCStd"
        self.assertTrue(self.Doc.Label_1.TypeTransient == 4711)
        self.Doc.Label_1.TypeTransient = 4712
        # setup Linking
        self.Doc.Label_1.Link = self.Doc.Label_2
        self.Doc.Label_2.Link = self.Doc.Label_3
        self.Doc.Label_1.LinkSub = (self.Doc.Label_2, ["Sub1", "Sub2"])
        self.Doc.Label_2.LinkSub = (self.Doc.Label_3, ["Sub3", "Sub4"])
        # save the document
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("SaveRestoreTests")
        self.Doc = FreeCAD.open(SaveName)
        self.assertTrue(self.Doc.Label_1.Integer == 4711)
        self.assertTrue(self.Doc.Label_2.Integer == 4711)
        # test Linkage
        self.assertTrue(self.Doc.Label_1.Link == self.Doc.Label_2)
        self.assertTrue(self.Doc.Label_2.Link == self.Doc.Label_3)
        self.assertTrue(self.Doc.Label_1.LinkSub == (self.Doc.Label_2, ["Sub1", "Sub2"]))
        self.assertTrue(self.Doc.Label_2.LinkSub == (self.Doc.Label_3, ["Sub3", "Sub4"]))
        # do NOT save transient properties
        self.assertTrue(self.Doc.Label_1.TypeTransient == 4711)
        self.assertTrue(self.Doc == FreeCAD.getDocument(self.Doc.Name))

    def testRestore(self):
        Doc = FreeCAD.newDocument("RestoreTests")
        Doc.addObject("App::FeatureTest", "Label_1")
        # saving and restoring
        FileName = self.TempPath + os.sep + "Test2.FCStd"
        Doc.saveAs(FileName)
        # restore must first clear the current content
        Doc.restore()
        self.assertTrue(len(Doc.Objects) == 1)
        FreeCAD.closeDocument("RestoreTests")

    def testActiveDocument(self):
        # open 2nd doc
        Second = FreeCAD.newDocument("Active")
        FreeCAD.closeDocument("Active")
        try:
            # There might be no active document anymore
            # This also checks for dangling pointers
            Active = FreeCAD.activeDocument()
            # Second is still a valid object
            self.assertTrue(Second != Active)
        except Exception:
            # Okay, no document open
            self.assertTrue(True)

    def testExtensionSaveRestore(self):
        # saving and restoring
        SaveName = self.TempPath + os.sep + "SaveRestoreExtensions.FCStd"
        Doc = FreeCAD.newDocument("SaveRestoreExtensions")
        # we try to create a normal python object and add an extension to it
        obj = Doc.addObject("App::DocumentObject", "Obj")
        grp1 = Doc.addObject("App::DocumentObject", "Extension_1")
        grp2 = Doc.addObject("App::FeaturePython", "Extension_2")

        grp1.addExtension("App::GroupExtensionPython")
        SaveRestoreSpecialGroup(grp2)
        if FreeCAD.GuiUp:
            SaveRestoreSpecialGroupViewProvider(grp2.ViewObject)
        grp2.Group = [obj]

        Doc.saveAs(SaveName)
        FreeCAD.closeDocument("SaveRestoreExtensions")
        Doc = FreeCAD.open(SaveName)

        self.assertTrue(Doc.Extension_1.hasExtension("App::GroupExtension"))
        self.assertTrue(Doc.Extension_2.hasExtension("App::GroupExtension"))
        self.assertTrue(Doc.Extension_2.Group[0] is Doc.Obj)
        self.assertTrue(hasattr(Doc.Extension_2.Proxy, "allowObject"))

        if FreeCAD.GuiUp:
            self.assertTrue(
                Doc.Extension_2.ViewObject.hasExtension("Gui::ViewProviderGroupExtensionPython")
            )
            self.assertTrue(hasattr(Doc.Extension_2.ViewObject.Proxy, "testFunction"))

        FreeCAD.closeDocument("SaveRestoreExtensions")

    def testPersistenceContentDump(self):
        # test smallest level... property
        self.Doc.Label_1.Vector = (1, 2, 3)
        dump = self.Doc.Label_1.dumpPropertyContent("Vector", Compression=9)
        self.Doc.Label_2.restorePropertyContent("Vector", dump)
        self.assertEqual(self.Doc.Label_1.Vector, self.Doc.Label_2.Vector)

        # next higher: object
        self.Doc.Label_1.Distance = 12
        self.Doc.Label_1.String = "test"
        dump = self.Doc.Label_1.dumpContent()
        self.Doc.Label_3.restoreContent(dump)
        self.assertEqual(self.Doc.Label_1.Distance, self.Doc.Label_3.Distance)
        self.assertEqual(self.Doc.Label_1.String, self.Doc.Label_3.String)

        # highest level: document
        dump = self.Doc.dumpContent(9)
        Doc = FreeCAD.newDocument("DumpTest")
        Doc.restoreContent(dump)
        self.assertEqual(len(self.Doc.Objects), len(Doc.Objects))
        self.assertEqual(self.Doc.Label_1.Distance, Doc.Label_1.Distance)
        self.assertEqual(self.Doc.Label_1.String, Doc.Label_1.String)
        self.assertEqual(self.Doc.Label_1.Vector, Doc.Label_1.Vector)
        FreeCAD.closeDocument("DumpTest")

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("SaveRestoreTests")


class DocumentRecomputeCases(unittest.TestCase):
    class RecomputeObserver:
        # records the objects the document reported as skipping recomputation
        def __init__(self):
            self.objs = []

        def slotSkipRecompute(self, _doc, objs):
            self.objs = objs

    def setUp(self):
        self.Doc = FreeCAD.newDocument("RecomputeTests")
        self.L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        self.L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        self.L3 = self.Doc.addObject("App::FeatureTest", "Label_3")

    def testDescent(self):
        # testing the up and downstream stuff
        FreeCAD.Console.PrintLog("def testDescent(self):Testcase not implemented\n")
        self.L1.Link = self.L2
        self.L2.Link = self.L3

    def testRecompute(self):

        # sequence to test recompute behaviour
        #       L1---\    L7
        #      /  \   \    |
        #    L2   L3   \  L8
        #   /  \ /  \  /
        #  L4   L5   L6

        L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        L3 = self.Doc.addObject("App::FeatureTest", "Label_3")
        L4 = self.Doc.addObject("App::FeatureTest", "Label_4")
        L5 = self.Doc.addObject("App::FeatureTest", "Label_5")
        L6 = self.Doc.addObject("App::FeatureTest", "Label_6")
        L7 = self.Doc.addObject("App::FeatureTest", "Label_7")
        L8 = self.Doc.addObject("App::FeatureTest", "Label_8")
        L1.LinkList = [L2, L3, L6]
        L2.Link = L4
        L2.LinkList = [L5]
        L3.LinkList = [L5, L6]
        L7.Link = L8  # make second root

        self.assertTrue(L7 in self.Doc.RootObjects)
        self.assertTrue(L1 in self.Doc.RootObjects)

        self.assertTrue(len(self.Doc.Objects) == len(self.Doc.TopologicalSortedObjects))

        seqDic = {}
        i = 0
        for obj in self.Doc.TopologicalSortedObjects:
            seqDic[obj] = i
            print(obj)
            i += 1

        self.assertTrue(seqDic[L2] > seqDic[L1])
        self.assertTrue(seqDic[L3] > seqDic[L1])
        self.assertTrue(seqDic[L5] > seqDic[L2])
        self.assertTrue(seqDic[L5] > seqDic[L3])
        self.assertTrue(seqDic[L5] > seqDic[L1])

        self.assertTrue(
            (0, 0, 0, 0, 0, 0)
            == (L1.ExecCount, L2.ExecCount, L3.ExecCount, L4.ExecCount, L5.ExecCount, L6.ExecCount)
        )
        self.assertTrue(self.Doc.recompute() == 4)
        self.assertTrue(
            (1, 1, 1, 0, 0, 0)
            == (L1.ExecCount, L2.ExecCount, L3.ExecCount, L4.ExecCount, L5.ExecCount, L6.ExecCount)
        )
        L5.enforceRecompute()
        self.assertTrue(
            (1, 1, 1, 0, 0, 0)
            == (L1.ExecCount, L2.ExecCount, L3.ExecCount, L4.ExecCount, L5.ExecCount, L6.ExecCount)
        )
        self.assertTrue(self.Doc.recompute() == 4)
        self.assertTrue(
            (2, 2, 2, 0, 1, 0)
            == (L1.ExecCount, L2.ExecCount, L3.ExecCount, L4.ExecCount, L5.ExecCount, L6.ExecCount)
        )
        L4.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 3)
        self.assertTrue(
            (3, 3, 2, 1, 1, 0)
            == (L1.ExecCount, L2.ExecCount, L3.ExecCount, L4.ExecCount, L5.ExecCount, L6.ExecCount)
        )
        L5.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 4)
        self.assertTrue(
            (4, 4, 3, 1, 2, 0)
            == (L1.ExecCount, L2.ExecCount, L3.ExecCount, L4.ExecCount, L5.ExecCount, L6.ExecCount)
        )
        L6.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 3)
        self.assertTrue(
            (5, 4, 4, 1, 2, 1)
            == (L1.ExecCount, L2.ExecCount, L3.ExecCount, L4.ExecCount, L5.ExecCount, L6.ExecCount)
        )
        L2.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 2)
        self.assertTrue(
            (6, 5, 4, 1, 2, 1)
            == (L1.ExecCount, L2.ExecCount, L3.ExecCount, L4.ExecCount, L5.ExecCount, L6.ExecCount)
        )
        L1.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 1)
        self.assertTrue(
            (7, 5, 4, 1, 2, 1)
            == (L1.ExecCount, L2.ExecCount, L3.ExecCount, L4.ExecCount, L5.ExecCount, L6.ExecCount)
        )

        self.Doc.removeObject(L1.Name)
        self.Doc.removeObject(L2.Name)
        self.Doc.removeObject(L3.Name)
        self.Doc.removeObject(L4.Name)
        self.Doc.removeObject(L5.Name)
        self.Doc.removeObject(L6.Name)
        self.Doc.removeObject(L7.Name)
        self.Doc.removeObject(L8.Name)

    def testGroupRecompute(self):
        g1 = self.Doc.addObject("App::Part", "g1");
        g2 = self.Doc.addObject("App::Part", "g2");
        g3 = self.Doc.addObject("App::Part", "g3");
        box = self.Doc.addObject("Part::Box", "box");
        link = self.Doc.addObject("App::Link","link")

        g3.addObject(box)
        g2.addObject(g3)
        g1.addObject(g2)
        link.setLink(g1,'{}.{}.{}.'.format(g2.Name,g3.Name,box.Name))

        self.Doc.recompute()

        box.Placement.Base = FreeCAD.Vector(10,10,10)

        observer = self.RecomputeObserver()
        FreeCAD.addDocumentObserver(observer);
        res = self.Doc.recompute()
        FreeCAD.removeDocumentObserver(observer);

        # Placement change will not trigger a full recompute of a Part.Feature, so
        # only 4 objects will be recomputed.
        self.assertEqual(res, 4)
        self.assertFalse(observer.objs)

        box.Length *= 2
        # Other property change shall trigger the recompute, so it shall be 5 this
        # time.
        res = self.Doc.recompute()
        self.assertEqual(res, 5)

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("RecomputeTests")


class UndoRedoCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("UndoTest")
        self.Doc.UndoMode = 0
        self.Doc.addObject("App::FeatureTest", "Base")
        self.Doc.addObject("App::FeatureTest", "Del")
        self.Doc.getObject("Del").Integer = 2

    def testUndoProperties(self):
        # switch on the Undo
        self.Doc.UndoMode = 1

        # first transaction
        self.Doc.openTransaction("Transaction1")
        self.Doc.addObject("App::FeatureTest", "test1")
        self.Doc.getObject("test1").Integer = 1
        self.Doc.getObject("test1").String = "test1"
        self.Doc.getObject("test1").Float = 1.0
        self.Doc.getObject("test1").Bool = 1

        # self.Doc.getObject("test1").IntegerList  = 1
        # self.Doc.getObject("test1").FloatList  = 1.0

        # self.Doc.getObject("test1").Matrix  = (1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0)
        # self.Doc.getObject("test1").Vector  = (1.0,1.0,1.0)

        # second transaction
        self.Doc.openTransaction("Transaction2")
        self.Doc.getObject("test1").Integer = 2
        self.Doc.getObject("test1").String = "test2"
        self.Doc.getObject("test1").Float = 2.0
        self.Doc.getObject("test1").Bool = 0

        # switch on the Undo OFF
        self.Doc.UndoMode = 0

    def testUndoClear(self):
        # switch on the Undo
        self.Doc.UndoMode = 1
        self.assertEqual(self.Doc.UndoNames, [])
        self.assertEqual(self.Doc.UndoCount, 0)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        self.Doc.openTransaction("Transaction1")
        # becomes the active object
        self.Doc.addObject("App::FeatureTest", "test1")
        self.Doc.commitTransaction()
        # removes the active object
        self.Doc.undo()
        self.assertEqual(self.Doc.ActiveObject, None)
        # deletes the active object
        self.Doc.clearUndos()
        self.assertEqual(self.Doc.ActiveObject, None)

    def testUndo(self):
        # switch on the Undo
        self.Doc.UndoMode = 1
        self.assertEqual(self.Doc.UndoNames, [])
        self.assertEqual(self.Doc.UndoCount, 0)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        # first transaction
        self.Doc.openTransaction("Transaction1")
        self.Doc.addObject("App::FeatureTest", "test1")
        self.Doc.getObject("test1").Integer = 1
        self.Doc.getObject("Del").Integer = 1
        self.Doc.removeObject("Del")
        self.assertEqual(self.Doc.UndoNames, ["Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 1)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        # second transaction
        self.Doc.openTransaction("Transaction2")
        # new behavior: no change, no transaction
        self.assertEqual(self.Doc.UndoNames, ["Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 1)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        self.Doc.getObject("test1").Integer = 2
        self.assertEqual(self.Doc.UndoNames, ["Transaction2", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 2)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        # abort second transaction
        self.Doc.abortTransaction()
        self.assertEqual(self.Doc.UndoNames, ["Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 1)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)
        self.assertEqual(self.Doc.getObject("test1").Integer, 1)

        # again second transaction
        self.Doc.openTransaction("Transaction2")
        self.Doc.getObject("test1").Integer = 2
        self.assertEqual(self.Doc.UndoNames, ["Transaction2", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 2)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        # third transaction
        self.Doc.openTransaction("Transaction3")
        self.Doc.getObject("test1").Integer = 3
        self.assertEqual(self.Doc.UndoNames, ["Transaction3", "Transaction2", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 3)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        # fourth transaction
        self.Doc.openTransaction("Transaction4")
        self.Doc.getObject("test1").Integer = 4
        self.assertEqual(
            self.Doc.UndoNames, ["Transaction4", "Transaction3", "Transaction2", "Transaction1"]
        )
        self.assertEqual(self.Doc.UndoCount, 4)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        # undo the fourth transaction
        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 3)
        self.assertEqual(self.Doc.UndoNames, ["Transaction3", "Transaction2", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 3)
        self.assertEqual(self.Doc.RedoNames, ["Transaction4"])
        self.assertEqual(self.Doc.RedoCount, 1)

        # undo the third transaction
        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 2)
        self.assertEqual(self.Doc.UndoNames, ["Transaction2", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 2)
        self.assertEqual(self.Doc.RedoNames, ["Transaction3", "Transaction4"])
        self.assertEqual(self.Doc.RedoCount, 2)

        # undo the second transaction
        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 1)
        self.assertEqual(self.Doc.UndoNames, ["Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 1)
        self.assertEqual(self.Doc.RedoNames, ["Transaction2", "Transaction3", "Transaction4"])
        self.assertEqual(self.Doc.RedoCount, 3)

        # undo the first transaction
        self.Doc.undo()
        self.assertTrue(self.Doc.getObject("test1") is None)
        self.assertTrue(self.Doc.getObject("Del").Integer == 2)
        self.assertEqual(self.Doc.UndoNames, [])
        self.assertEqual(self.Doc.UndoCount, 0)
        self.assertEqual(
            self.Doc.RedoNames, ["Transaction1", "Transaction2", "Transaction3", "Transaction4"]
        )
        self.assertEqual(self.Doc.RedoCount, 4)

        # redo the first transaction
        self.Doc.redo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 1)
        self.assertEqual(self.Doc.UndoNames, ["Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 1)
        self.assertEqual(self.Doc.RedoNames, ["Transaction2", "Transaction3", "Transaction4"])
        self.assertEqual(self.Doc.RedoCount, 3)

        # redo the second transaction
        self.Doc.redo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 2)
        self.assertEqual(self.Doc.UndoNames, ["Transaction2", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 2)
        self.assertEqual(self.Doc.RedoNames, ["Transaction3", "Transaction4"])
        self.assertEqual(self.Doc.RedoCount, 2)

        # undo the second transaction
        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 1)
        self.assertEqual(self.Doc.UndoNames, ["Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 1)
        self.assertEqual(self.Doc.RedoNames, ["Transaction2", "Transaction3", "Transaction4"])
        self.assertEqual(self.Doc.RedoCount, 3)

        # new transaction eight
        self.Doc.openTransaction("Transaction8")
        self.Doc.getObject("test1").Integer = 8
        self.assertEqual(self.Doc.UndoNames, ["Transaction8", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 2)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)
        self.Doc.abortTransaction()
        self.assertEqual(self.Doc.UndoNames, ["Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 1)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        # again new transaction eight
        self.Doc.openTransaction("Transaction8")
        self.Doc.getObject("test1").Integer = 8
        self.assertEqual(self.Doc.UndoNames, ["Transaction8", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 2)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

        # again new transaction nine
        self.Doc.openTransaction("Transaction9")
        self.Doc.getObject("test1").Integer = 9
        self.assertEqual(self.Doc.UndoNames, ["Transaction9", "Transaction8", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 3)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)
        self.Doc.commitTransaction()
        self.assertEqual(self.Doc.UndoNames, ["Transaction9", "Transaction8", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 3)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)
        self.assertEqual(self.Doc.getObject("test1").Integer, 9)

        # undo the ninth transaction
        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 8)
        self.assertEqual(self.Doc.UndoNames, ["Transaction8", "Transaction1"])
        self.assertEqual(self.Doc.UndoCount, 2)
        self.assertEqual(self.Doc.RedoNames, ["Transaction9"])
        self.assertEqual(self.Doc.RedoCount, 1)

        # switch on the Undo OFF
        self.Doc.UndoMode = 0
        self.assertEqual(self.Doc.UndoNames, [])
        self.assertEqual(self.Doc.UndoCount, 0)
        self.assertEqual(self.Doc.RedoNames, [])
        self.assertEqual(self.Doc.RedoCount, 0)

    def testUndoInList(self):

        self.Doc.UndoMode = 1

        self.Doc.openTransaction("Box")
        self.Box = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Cylinder")
        self.Cylinder = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Fuse")
        self.Fuse1 = self.Doc.addObject("App::FeatureTest", "Fuse")
        self.Fuse1.LinkList = [self.Box, self.Cylinder]
        self.Doc.commitTransaction()

        self.Doc.undo()
        self.assertTrue(len(self.Box.InList) == 0)
        self.assertTrue(len(self.Cylinder.InList) == 0)

        self.Doc.redo()
        self.assertTrue(len(self.Box.InList) == 1)
        self.assertTrue(self.Box.InList[0] == self.Doc.Fuse)
        self.assertTrue(len(self.Cylinder.InList) == 1)
        self.assertTrue(self.Cylinder.InList[0] == self.Doc.Fuse)

    def testUndoIssue0003150Part1(self):

        self.Doc.UndoMode = 1

        self.Doc.openTransaction("Box")
        self.Box = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Cylinder")
        self.Cylinder = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Fuse")
        self.Fuse1 = self.Doc.addObject("App::FeatureTest")
        self.Fuse1.LinkList = [self.Box, self.Cylinder]
        self.Doc.commitTransaction()
        self.Doc.recompute()

        self.Doc.openTransaction("Sphere")
        self.Sphere = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Fuse")
        self.Fuse2 = self.Doc.addObject("App::FeatureTest")
        self.Fuse2.LinkList = [self.Fuse1, self.Sphere]
        self.Doc.commitTransaction()
        self.Doc.recompute()

        self.Doc.openTransaction("Part")
        self.Part = self.Doc.addObject("App::Part")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Drag")
        self.Part.addObject(self.Fuse2)
        self.Doc.commitTransaction()

        # 3 undos show the problem of failing recompute
        self.Doc.undo()
        self.Doc.undo()
        self.Doc.undo()
        self.assertTrue(self.Doc.recompute() >= 0)

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("UndoTest")


class DocumentGroupCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("GroupTests")

    def testGroup(self):
        # Add an object to the group
        L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        G1 = self.Doc.addObject("App::DocumentObjectGroup", "Group")
        G1.addObject(L2)
        self.assertTrue(G1.hasObject(L2))

        # Adding the group to itself must fail
        try:
            G1.addObject(G1)
        except Exception:
            FreeCAD.Console.PrintLog("Cannot add group to itself, OK\n")
        else:
            self.fail("Adding the group to itself must not be possible")

        self.Doc.UndoMode = 1

        # Remove object from group
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Label_2")
        self.Doc.commitTransaction()
        self.assertTrue(G1.getObject("Label_2") is None)
        self.Doc.undo()
        self.assertTrue(G1.getObject("Label_2") is not None)

        # Remove first group and then the object
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Group")
        self.Doc.removeObject("Label_2")
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.assertTrue(G1.getObject("Label_2") is not None)

        # Remove first object and then the group in two transactions
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Label_2")
        self.Doc.commitTransaction()
        self.assertTrue(G1.getObject("Label_2") is None)
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Group")
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.Doc.undo()
        self.assertTrue(G1.getObject("Label_2") is not None)

        # Remove first object and then the group in one transaction
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Label_2")
        self.assertTrue(G1.getObject("Label_2") is None)
        self.Doc.removeObject("Group")
        self.Doc.commitTransaction()
        self.Doc.undo()
        # FIXME: See bug #1820554
        self.assertTrue(G1.getObject("Label_2") is not None)

        # Add a second object to the group
        L3 = self.Doc.addObject("App::FeatureTest", "Label_3")
        G1.addObject(L3)
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Label_2")
        self.assertTrue(G1.getObject("Label_2") is None)
        self.Doc.removeObject("Label_3")
        self.assertTrue(G1.getObject("Label_3") is None)
        self.Doc.removeObject("Group")
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.assertTrue(G1.getObject("Label_3") is not None)
        self.assertTrue(G1.getObject("Label_2") is not None)

        self.Doc.UndoMode = 0

        # Cleanup
        self.Doc.removeObject("Group")
        self.Doc.removeObject("Label_2")
        self.Doc.removeObject("Label_3")

    def testSubNameNormalize(self):
        grp = self.Doc.addObject("App::DocumentObjectGroup","Group")
        linkgrp = self.Doc.addObject("App::LinkGroup", "LinkGroup")
        link = self.Doc.addObject('App::Link', 'Link')
        linkarray = self.Doc.addObject('App::Link', 'LinkArray')
        part2 = self.Doc.addObject("App::Part", "Part2")
        part = self.Doc.addObject("App::Part", "Part")
        obj1 = self.Doc.addObject("App::FeatureTest","obj1")
        obj1.Label = 'Object1'
        obj2 = self.Doc.addObject("App::FeatureTest","obj2")
        obj2.Label = 'Object2'
        obj3 = self.Doc.addObject("App::FeatureTest","obj3")
        obj3.Label = 'Object3'
        obj1.Link = obj2
        obj2.Link = obj3

        grp.addObject(obj1)

        path = 'obj1.obj2.obj3.'
        self.assertEqual(grp.normalizeSubName(path+'Face'), (grp, path+'Face'))
        self.assertEqual(grp.normalizeSubName(path+'Face', 'NoElement'), (grp, path))

        path2 = '$Object1.$Object2.obj3.'
        self.assertEqual(grp.normalizeSubName(path2), (grp, path))
        self.assertEqual(grp.normalizeSubName(path2+'Face', 'KeepSubName'), (grp, path2+'Face'))
        self.assertEqual(grp.normalizeSubName(path2+'Face', ['KeepSubName','NoElement']),
                                            (grp, path2))

        link.LinkedObject = grp
        linkgrp.ElementList = [link]
        linkarray.LinkedObject = linkgrp
        linkarray.ElementCount = 2

        path = '1.0.obj1.obj2.obj3.'
        self.assertEqual(linkarray.normalizeSubName(path), (linkarray, path))

        path2 = '1.0.$Object1.obj2.obj3.'
        self.assertEqual(linkarray.normalizeSubName(path2), (linkarray, path))

        part.addObjects([grp, obj1, obj2, obj3])

        path = 'obj1.obj2.obj3.'
        self.assertEqual(grp.normalizeSubName(path), (obj3, ''))

        path = 'Group.obj1.obj2.obj3.'
        path2 = 'obj3.'
        self.assertEqual(part.normalizeSubName(path), (part, path2))

        part2.addObject(part)

        self.assertEqual(part.normalizeSubName(path), (part, path2))

        path = 'Part.Group.obj1.obj2.obj3.'
        path2 = 'Part.obj3.'
        self.assertEqual(part2.normalizeSubName(path), (part2, path2))

        part.addObjects([link, linkgrp, linkarray])

        path = 'Link.obj1.obj2.obj3.'
        self.assertEqual(part.normalizeSubName(path), (part, path))

        path = 'LinkArray.1.0.obj1.obj2.obj3.'
        self.assertEqual(part.normalizeSubName(path), (part, path))

        path = 'LinkGroup.0.obj1.obj2.obj3.'
        self.assertEqual(part.normalizeSubName(path), (part, path))

    def testGroupAndGeoFeatureGroup(self):
        # an object can only be in one group at once, that must be enforced
        obj1 = self.Doc.addObject("App::FeatureTest", "obj1")
        grp1 = self.Doc.addObject("App::DocumentObjectGroup", "Group1")
        grp2 = self.Doc.addObject("App::DocumentObjectGroup", "Group2")
        grp1.addObject(obj1)
        self.assertTrue(obj1.getParentGroup() == grp1)
        self.assertTrue(obj1.getParentGeoFeatureGroup() is None)
        self.assertTrue(grp1.hasObject(obj1))
        grp2.addObject(obj1)
        self.assertTrue(grp1.hasObject(obj1) == False)
        self.assertTrue(grp2.hasObject(obj1))

        # an object is allowed to be in a group and a geofeaturegroup
        prt1 = self.Doc.addObject("App::Part", "Part1")
        prt2 = self.Doc.addObject("App::Part", "Part2")

        prt1.addObject(grp2)
        self.assertTrue(grp2.getParentGeoFeatureGroup() == prt1)
        self.assertTrue(grp2.getParentGroup() is None)
        self.assertTrue(grp2.hasObject(obj1))
        self.assertTrue(prt1.hasObject(grp2))
        self.assertTrue(prt1.hasObject(obj1))

        # it is not allowed to be in 2 geofeaturegroups
        prt2.addObject(grp2)
        self.assertTrue(grp2.hasObject(obj1))
        self.assertTrue(prt1.hasObject(grp2) == False)
        self.assertTrue(prt1.hasObject(obj1) == False)
        self.assertTrue(prt2.hasObject(grp2))
        self.assertTrue(prt2.hasObject(obj1))
        try:
            grp = prt1.Group
            grp.append(obj1)
            prt1.Group = grp
        except Exception:
            grp.remove(obj1)
            self.assertTrue(prt1.Group == grp)
        else:
            self.fail("No exception thrown when object is in multiple Groups")

        # it is not allowed to be in 2 Groups
        prt2.addObject(grp1)
        grp = grp1.Group
        grp.append(obj1)
        try:
            grp1.Group = grp
        except Exception:
            pass
        else:
            self.fail("No exception thrown when object is in multiple Groups")

        # cross linking between GeoFeatureGroups is not allowed
        self.Doc.recompute()
        box = self.Doc.addObject("App::FeatureTest", "Box")
        cyl = self.Doc.addObject("App::FeatureTest", "Cylinder")
        fus = self.Doc.addObject("App::FeatureTest", "Fusion")
        fus.LinkList = [cyl, box]
        self.Doc.recompute()
        self.assertTrue(fus.State[0] == "Up-to-date")
        fus.LinkList = (
            []
        )  # remove all links as addObject would otherwise transfer all linked objects
        prt1.addObject(cyl)
        fus.LinkList = [cyl, box]
        self.Doc.recompute()
        # self.assertTrue(fus.State[0] == 'Invalid')
        fus.LinkList = []
        prt1.addObject(box)
        fus.LinkList = [cyl, box]
        self.Doc.recompute()
        # self.assertTrue(fus.State[0] == 'Invalid')
        fus.LinkList = []
        prt1.addObject(fus)
        fus.LinkList = [cyl, box]
        self.Doc.recompute()
        self.assertTrue(fus.State[0] == "Up-to-date")
        prt2.addObject(box)  # this time addObject should move all dependencies to the new part
        self.Doc.recompute()
        self.assertTrue(fus.State[0] == "Up-to-date")

        # grouping must be resilient against cyclic links and not crash: #issue 0002567
        prt1.addObject(prt2)
        grp = prt2.Group
        grp.append(prt1)
        prt2.Group = grp
        self.Doc.recompute()
        prt2.Group = []
        try:
            prt2.Group = [prt2]
        except Exception:
            pass
        else:
            self.fail("Exception is expected")

        self.Doc.recompute()

    def testIssue0003150Part2(self):
        self.box = self.Doc.addObject("App::FeatureTest")
        self.cyl = self.Doc.addObject("App::FeatureTest")
        self.sph = self.Doc.addObject("App::FeatureTest")

        self.fus1 = self.Doc.addObject("App::FeatureTest")
        self.fus2 = self.Doc.addObject("App::FeatureTest")

        self.fus1.LinkList = [self.box, self.cyl]
        self.fus2.LinkList = [self.sph, self.cyl]

        self.prt = self.Doc.addObject("App::Part")
        self.prt.addObject(self.fus1)
        self.assertTrue(len(self.prt.Group) == 5)
        self.assertTrue(self.fus2.getParentGeoFeatureGroup() == self.prt)
        self.assertTrue(self.prt.hasObject(self.sph))

        self.prt.removeObject(self.fus1)
        self.assertTrue(len(self.prt.Group) == 0)

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("GroupTests")


class DocumentPlatformCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PlatformTests")
        self.Doc.addObject("App::FeatureTest", "Test")
        self.TempPath = tempfile.gettempdir()
        self.DocName = self.TempPath + os.sep + "PlatformTests.FCStd"

    def testFloatList(self):
        self.Doc.Test.FloatList = [-0.05, 2.5, 5.2]

        # saving and restoring
        self.Doc.saveAs(self.DocName)
        FreeCAD.closeDocument("PlatformTests")
        self.Doc = FreeCAD.open(self.DocName)

        self.assertTrue(abs(self.Doc.Test.FloatList[0] + 0.05) < 0.01)
        self.assertTrue(abs(self.Doc.Test.FloatList[1] - 2.5) < 0.01)
        self.assertTrue(abs(self.Doc.Test.FloatList[2] - 5.2) < 0.01)

    def testColorList(self):
        self.Doc.Test.ColourList = [(1.0, 0.5, 0.0), (0.0, 0.5, 1.0)]

        # saving and restoring
        self.Doc.saveAs(self.DocName)
        FreeCAD.closeDocument("PlatformTests")
        self.Doc = FreeCAD.open(self.DocName)

        self.assertTrue(abs(self.Doc.Test.ColourList[0][0] - 1.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[0][1] - 0.5) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[0][2] - 0.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[0][3] - 1.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[1][0] - 0.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[1][1] - 0.5) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[1][2] - 1.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[1][3] - 1.0) < 0.01)

    def testVectorList(self):
        self.Doc.Test.VectorList = [(-0.05, 2.5, 5.2), (-0.05, 2.5, 5.2)]

        # saving and restoring
        self.Doc.saveAs(self.DocName)
        FreeCAD.closeDocument("PlatformTests")
        self.Doc = FreeCAD.open(self.DocName)

        self.assertTrue(len(self.Doc.Test.VectorList) == 2)

    def testPoints(self):
        try:
            self.Doc.addObject("Points::Feature", "Points")

            # saving and restoring
            self.Doc.saveAs(self.DocName)
            FreeCAD.closeDocument("PlatformTests")
            self.Doc = FreeCAD.open(self.DocName)

            self.assertTrue(self.Doc.Points.Points.count() == 0)
        except Exception:
            pass

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("PlatformTests")


class DocumentBacklinks(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("BackLinks")

    def testIssue0003323(self):
        self.Doc.UndoMode = 1
        self.Doc.openTransaction("Create object")
        obj1 = self.Doc.addObject("App::FeatureTest", "Test1")
        obj2 = self.Doc.addObject("App::FeatureTest", "Test2")
        obj2.Link = obj1
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.Doc.openTransaction("Create object")

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("BackLinks")


class DocumentFileIncludeCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("FileIncludeTests")
        # testing with undo
        self.Doc.UndoMode = 1

    def testApplyFiles(self):
        self.Doc.openTransaction("Transaction0")
        self.L1 = self.Doc.addObject("App::DocumentObjectFileIncluded", "FileObject1")
        self.assertTrue(self.L1.File == "")
        self.Filename = self.L1.File

        self.Doc.openTransaction("Transaction1")
        self.TempPath = tempfile.gettempdir()
        # creating a file in the Transient directory of the document
        file = open(self.Doc.getTempFileName("test"), "w")
        file.write("test No1")
        file.close()
        # applying the file
        self.L1.File = (file.name, "Test.txt")
        # The stored file is named by content hash and shared between
        # properties, so its path says nothing about the name -- the name the
        # value was given is the property's own, and is what it saves under.
        self.assertTrue(os.path.exists(self.L1.File))
        # read again
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No1")
        file.close()
        file = open(self.TempPath + "/testNest.txt", "w")
        file.write("test No2")
        file.close()
        # applying the file
        self.Doc.openTransaction("Transaction2")
        self.L1.File = file.name
        self.assertTrue(os.path.exists(self.L1.File))
        # read again
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No2")
        file.close()
        self.Doc.undo()
        self.assertTrue(os.path.exists(self.L1.File))
        # read again
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No1")
        file.close()
        self.Doc.undo()
        # read again
        self.assertTrue(self.L1.File == "")
        self.Doc.redo()
        self.assertTrue(os.path.exists(self.L1.File))
        # read again
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No1")
        file.close()
        self.Doc.redo()
        self.assertTrue(os.path.exists(self.L1.File))
        # read again
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No2")
        file.close()
        # Save restore test
        FileName = self.TempPath + "/FileIncludeTests.fcstd"
        self.Doc.saveAs(FileName)
        FreeCAD.closeDocument("FileIncludeTests")
        self.Doc = FreeCAD.open(self.TempPath + "/FileIncludeTests.fcstd")
        # check if the file is still there
        self.L1 = self.Doc.getObject("FileObject1")
        file = open(self.L1.File, "r")
        res = file.read()
        FreeCAD.Console.PrintLog(res + "\n")
        self.assertTrue(res == "test No2")
        self.assertTrue(os.path.exists(self.L1.File))
        file.close()

        # test for bug #94 (File overlap in PropertyFileIncluded)
        L2 = self.Doc.addObject("App::DocumentObjectFileIncluded", "FileObject2")
        L3 = self.Doc.addObject("App::DocumentObjectFileIncluded", "FileObject3")

        # creating two files in the Transient directory of the document
        file1 = open(self.Doc.getTempFileName("test"), "w")
        file1.write("test No1")
        file1.close()
        file2 = open(self.Doc.getTempFileName("test"), "w")
        file2.write("test No2")
        file2.close()

        # applying the file with the same base name
        L2.File = (file1.name, "Test.txt")
        L3.File = (file2.name, "Test.txt")

        file = open(L2.File, "r")
        self.assertTrue(file.read() == "test No1")
        file.close()
        file = open(L3.File, "r")
        self.assertTrue(file.read() == "test No2")
        file.close()

        # create a second document, copy a file and close the document
        # the test is about to put the file to the correct transient dir
        doc2 = FreeCAD.newDocument("Doc2")
        L4 = doc2.addObject("App::DocumentObjectFileIncluded", "FileObject")
        L5 = doc2.addObject("App::DocumentObjectFileIncluded", "FileObject")
        L6 = doc2.addObject("App::DocumentObjectFileIncluded", "FileObject")
        L4.File = (L3.File, "Test.txt")
        L5.File = L3.File
        L6.File = L3.File
        FreeCAD.closeDocument("FileIncludeTests")
        self.Doc = FreeCAD.open(self.TempPath + "/FileIncludeTests.fcstd")
        self.assertTrue(os.path.exists(L4.File))
        self.assertTrue(os.path.exists(L5.File))
        self.assertTrue(os.path.exists(L6.File))
        # L5 and L6 were given the same content under the same name, so they
        # now share one reference counted file instead of getting a copy each.
        # The file outlives whichever of them is released first.
        self.assertTrue(L5.File == L6.File)
        # copy file from L5 which is in the same directory
        L7 = doc2.addObject("App::DocumentObjectFileIncluded", "FileObject3")
        L7.File = (L5.File, "Copy.txt")
        self.assertTrue(os.path.exists(L7.File))
        FreeCAD.closeDocument("Doc2")

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("FileIncludeTests")


class DocumentPropertyCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PropertyTests")
        self.Obj = self.Doc.addObject("App::FeaturePython", "Test")

    def testDescent(self):
        # testing the up and downstream stuff
        props = self.Obj.supportedProperties()
        for i in props:
            self.Obj.addProperty(i, i.replace(":", "_"))
        tempPath = tempfile.gettempdir()
        tempFile = tempPath + os.sep + "PropertyTests.FCStd"
        self.Doc.saveAs(tempFile)
        FreeCAD.closeDocument("PropertyTests")
        self.Doc = FreeCAD.open(tempFile)

    def testRemoveProperty(self):
        prop = "Something"
        self.Obj.addProperty("App::PropertyFloat", prop)
        self.Obj.Something = 0.01
        self.Doc.recompute()
        self.Doc.openTransaction("modify and remove property")
        self.Obj.Something = 0.00
        self.Obj.removeProperty(prop)
        self.Obj.recompute()
        self.Doc.abortTransaction()

    def testRemovePropertyExpression(self):
        p1 = self.Doc.addObject("App::FeaturePython", "params1")
        p2 = self.Doc.addObject("App::FeaturePython", "params2")
        p1.addProperty("App::PropertyFloat", "a")
        p1.a = 42
        p2.addProperty("App::PropertyFloat", "b")
        p2.setExpression("b", "params1.a")
        self.Doc.recompute()
        p2.removeProperty("b")
        p1.touch()
        self.Doc.recompute()
        self.assertTrue(not p2 in p1.InList)

    def testRemovePropertyOnChange(self):
        class Feature:
            def __init__(self, fp):
                fp.Proxy = self
                fp.addProperty("App::PropertyString", "Test")

            def onBeforeChange(self, fp, prop):
                if prop == "Test":
                    fp.removeProperty("Test")

            def onChanged(self, fp, prop):
                getattr(fp, prop)

        obj = self.Doc.addObject("App::FeaturePython")
        fea = Feature(obj)
        obj.Test = "test"

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("PropertyTests")


class DocumentExpressionCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument()

    def assertAlmostEqual(self, v1, v2):
        if math.fabs(v2 - v1) > 1e-12:
            self.assertEqual(v1, v2)

    def testExpression(self):
        self.Obj1 = self.Doc.addObject("App::FeatureTest", "Test")
        self.Obj2 = self.Doc.addObject("App::FeatureTest", "Test")
        # set the object twice to test that the backlinks are removed when overwriting the expression
        self.Obj2.setExpression(
            "Placement.Rotation.Angle", "%s.Placement.Rotation.Angle" % self.Obj1.Name
        )
        self.Obj2.setExpression(
            "Placement.Rotation.Angle", "%s.Placement.Rotation.Angle" % self.Obj1.Name
        )
        self.Obj1.Placement = FreeCAD.Placement(
            FreeCAD.Vector(0, 0, 0), FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 10)
        )
        self.Doc.recompute()
        self.assertAlmostEqual(
            self.Obj1.Placement.Rotation.Angle, self.Obj2.Placement.Rotation.Angle
        )

        # clear the expression
        self.Obj2.setExpression("Placement.Rotation.Angle", None)
        self.assertAlmostEqual(
            self.Obj1.Placement.Rotation.Angle, self.Obj2.Placement.Rotation.Angle
        )
        self.Doc.recompute()
        self.assertAlmostEqual(
            self.Obj1.Placement.Rotation.Angle, self.Obj2.Placement.Rotation.Angle
        )
        # touch the objects to perform a recompute. Writing a property back to its
        # own value is elided here (OptimizeRecompute), so it would not touch them.
        self.Obj1.touch()
        self.Obj2.touch()
        # must not raise a topological error
        self.assertEqual(self.Doc.recompute(), 2)

        # add test for issue #6948
        self.Obj3 = self.Doc.addObject("App::FeatureTest", "Test")
        self.Obj3.setExpression("Float", "2*(5%3)")
        self.Doc.recompute()
        self.assertEqual(self.Obj3.Float, 4)
        self.assertEqual(self.Obj3.evalExpression(self.Obj3.ExpressionEngine[0][1]), 4)

    def testIssue4649(self):
        class Cls:
            def __init__(self, obj):
                self.MonitorChanges = False
                obj.Proxy = self
                obj.addProperty("App::PropertyFloat", "propA", "group")
                obj.addProperty("App::PropertyFloat", "propB", "group")
                self.MonitorChanges = True
                obj.setExpression("propB", "6*9")

            def onChanged(self, obj, prop):
                print("onChanged", self, obj, prop)
                if self.MonitorChanges and prop == "propA":
                    print("Removing expression...")
                    obj.setExpression("propB", None)

        obj = self.Doc.addObject("App::DocumentObjectGroupPython", "Obj")
        Cls(obj)
        self.Doc.UndoMode = 1
        self.Doc.openTransaction("Expression")
        obj.setExpression("propA", "42")
        self.Doc.recompute()
        self.Doc.commitTransaction()
        self.assertTrue(("propB", None) in obj.ExpressionEngine)
        self.assertTrue(("propA", "42") in obj.ExpressionEngine)

        self.Doc.undo()
        self.assertFalse(("propB", None) in obj.ExpressionEngine)
        self.assertFalse(("propA", "42") in obj.ExpressionEngine)

        self.Doc.redo()
        self.assertTrue(("propB", None) in obj.ExpressionEngine)
        self.assertTrue(("propA", "42") in obj.ExpressionEngine)

        self.Doc.recompute()
        obj.ExpressionEngine

        TempPath = tempfile.gettempdir()
        SaveName = TempPath + os.sep + "ExpressionTests.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(SaveName)

    def testCyclicDependencyOnPlacement(self):
        obj = self.Doc.addObject("App::FeaturePython", "Python")
        obj.addProperty("App::PropertyPlacement", "Placement")
        obj.setExpression(".Placement.Base.x", ".Placement.Base.y + 10mm")
        with self.assertRaises(RuntimeError):
            obj.setExpression(".Placement.Base.y", ".Placement.Base.x + 10mm")

    def testDependency(self):
        self.Obj1 = self.Doc.addObject("App::FeatureTest", "Test")
        self.Obj2 = self.Doc.addObject("App::FeatureTest", "Test")
        self.Obj1.Link = self.Obj2

        try:
            # references parent, cyclic dependency
            self.Obj2.setExpression('Integer', '._self.Document.Test.Integer')
            self.Doc.recompute()
        except Exception as e:
            self.assertTrue(str(e).count('cyclic') > 0)
        else:
            self.assertFalse('Expects excption of cyclic dependency')

        self.Obj2.setExpression('Integer', None)

        self.Sheet = self.Doc.addObject("Spreadsheet::Sheet","Sheet")
        self.Sheet.set('A1', '=Test.Link.Integer')
        self.assertIn(self.Obj1, self.Sheet.OutList)
        self.assertIn(self.Obj2, self.Sheet.OutList)

        self.Sheet.set('A1', '=Test._self.Document.Test001.Integer')
        self.assertIn(self.Obj1, self.Sheet.OutList)
        self.assertIn(self.Obj2, self.Sheet.OutList)

        self.Sheet.set('A1', '=Test.Link')
        self.assertIn(self.Obj1, self.Sheet.OutList)
        # It is by design that if the object identifier references an object without
        # referencing any of its property, it is not counted as a dependency. The
        # only dependency, in this case, is the 'Link' property of object 'Test'
        # (i.e. self.Obj1) . The logic is that even if 'Test001' (self.Obj2)
        # changes, the content of 'Test.Link' does not change, so 'Sheet' should not
        # be recomputed.
        self.assertFalse(self.Obj2 in self.Sheet.OutList)

        # Now 'Sheet' contains identifier referencing Test001.Integer, it should
        # be included as dependency even if it is indirectly referenced.
        self.Sheet.set('A2', '=A1.Integer')
        self.assertIn(self.Obj1, self.Sheet.OutList)

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument(self.Doc.Name)


class DocumentObserverCases(unittest.TestCase):
    class Observer:
        def __init__(self):
            self.clear()

        def clear(self):
            self.signal = []
            self.parameter = []
            self.parameter2 = []

        def slotCreatedDocument(self, doc):
            self.signal.append("DocCreated")
            self.parameter.append(doc)

        def slotDeletedDocument(self, doc):
            self.signal.append("DocDeleted")
            self.parameter.append(doc)

        def slotRelabelDocument(self, doc):
            self.signal.append("DocRelabled")
            self.parameter.append(doc)

        def slotActivateDocument(self, doc):
            self.signal.append("DocActivated")
            self.parameter.append(doc)

        def slotRecomputedDocument(self, doc):
            self.signal.append("DocRecomputed")
            self.parameter.append(doc)

        def slotUndoDocument(self, doc):
            self.signal.append("DocUndo")
            self.parameter.append(doc)

        def slotRedoDocument(self, doc):
            self.signal.append("DocRedo")
            self.parameter.append(doc)

        def slotOpenTransaction(self, doc, name):
            self.signal.append("DocOpenTransaction")
            self.parameter.append(doc)
            self.parameter2.append(name)

        def slotCommitTransaction(self, doc):
            self.signal.append("DocCommitTransaction")
            self.parameter.append(doc)

        def slotAbortTransaction(self, doc):
            self.signal.append("DocAbortTransaction")
            self.parameter.append(doc)

        def slotBeforeChangeDocument(self, doc, prop):
            self.signal.append("DocBeforeChange")
            self.parameter.append(doc)
            self.parameter2.append(prop)

        def slotChangedDocument(self, doc, prop):
            self.signal.append("DocChanged")
            self.parameter.append(doc)
            self.parameter2.append(prop)

        def slotCreatedObject(self, obj):
            self.signal.append("ObjCreated")
            self.parameter.append(obj)

        def slotDeletedObject(self, obj):
            self.signal.append("ObjDeleted")
            self.parameter.append(obj)

        def slotChangedObject(self, obj, prop):
            self.signal.append("ObjChanged")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotBeforeChangeObject(self, obj, prop):
            self.signal.append("ObjBeforeChange")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotRecomputedObject(self, obj):
            self.signal.append("ObjRecomputed")
            self.parameter.append(obj)

        def slotAppendDynamicProperty(self, obj, prop):
            self.signal.append("ObjAddDynProp")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotRemoveDynamicProperty(self, obj, prop):
            self.signal.append("ObjRemoveDynProp")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotChangePropertyEditor(self, obj, prop):
            self.signal.append("ObjChangePropEdit")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotStartSaveDocument(self, obj, name):
            self.signal.append("DocStartSave")
            self.parameter.append(obj)
            self.parameter2.append(name)

        def slotFinishSaveDocument(self, obj, name):
            self.signal.append("DocFinishSave")
            self.parameter.append(obj)
            self.parameter2.append(name)

        def slotBeforeAddingDynamicExtension(self, obj, extension):
            self.signal.append("ObjBeforeDynExt")
            self.parameter.append(obj)
            self.parameter2.append(extension)

        def slotAddedDynamicExtension(self, obj, extension):
            self.signal.append("ObjDynExt")
            self.parameter.append(obj)
            self.parameter2.append(extension)

    class GuiObserver:
        def __init__(self):
            self.clear()

        def clear(self):
            self.signal = []
            self.parameter = []
            self.parameter2 = []

        def slotCreatedDocument(self, doc):
            self.signal.append("DocCreated")
            self.parameter.append(doc)

        def slotDeletedDocument(self, doc):
            self.signal.append("DocDeleted")
            self.parameter.append(doc)

        def slotRelabelDocument(self, doc):
            self.signal.append("DocRelabled")
            self.parameter.append(doc)

        def slotRenameDocument(self, doc):
            self.signal.append("DocRenamed")
            self.parameter.append(doc)

        def slotActivateDocument(self, doc):
            self.signal.append("DocActivated")
            self.parameter.append(doc)

        def slotCreatedObject(self, obj):
            self.signal.append("ObjCreated")
            self.parameter.append(obj)

        def slotDeletedObject(self, obj):
            self.signal.append("ObjDeleted")
            self.parameter.append(obj)

        def slotChangedObject(self, obj, prop):
            self.signal.append("ObjChanged")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotInEdit(self, obj):
            self.signal.append("ObjInEdit")
            self.parameter.append(obj)

        def slotResetEdit(self, obj):
            self.signal.append("ObjResetEdit")
            self.parameter.append(obj)

    def setUp(self):
        self.Obs = self.Observer()
        FreeCAD.addDocumentObserver(self.Obs)

    def testRemoveObserver(self):
        FreeCAD.removeDocumentObserver(self.Obs)
        self.Obs.clear()
        self.Doc1 = FreeCAD.newDocument("Observer")
        FreeCAD.closeDocument(self.Doc1.Name)
        self.assertEqual(len(self.Obs.signal), 0)
        self.assertEqual(len(self.Obs.parameter2), 0)
        self.assertEqual(len(self.Obs.signal), 0)
        FreeCAD.addDocumentObserver(self.Obs)

    def testSave(self):
        TempPath = tempfile.gettempdir()
        SaveName = TempPath + os.sep + "SaveRestoreTests.FCStd"
        self.Doc1 = FreeCAD.newDocument("Observer1")
        self.Doc1.saveAs(SaveName)
        self.assertEqual(self.Obs.signal.pop(), "DocFinishSave")
        self.assertEqual(self.Obs.parameter2.pop(), self.Doc1.FileName)
        # A save that writes the history into the file (the default with the
        # transaction log on, docs/TransactionLog.md sec 27.5) sets the
        # document's History, Version and Branch properties as it goes.
        while self.Obs.parameter2[-1] in ("History", "Version", "Branch"):
            self.Obs.signal.pop()
            self.Obs.parameter.pop()
            self.Obs.parameter2.pop()
        self.assertEqual(self.Obs.signal.pop(), "DocStartSave")
        self.assertEqual(self.Obs.parameter2.pop(), self.Doc1.FileName)
        FreeCAD.closeDocument(self.Doc1.Name)

    def testDocument(self):
        # in case another document already exists then the tests cannot
        # be done reliably
        if FreeCAD.GuiUp and FreeCAD.activeDocument():
            return

        # testing document level signals
        self.Doc1 = FreeCAD.newDocument("Observer1")
        if FreeCAD.GuiUp:
            self.assertEqual(self.Obs.signal.pop(0), "DocActivated")
            self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertEqual(self.Obs.signal.pop(0), "DocCreated")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertEqual(self.Obs.signal.pop(0), "DocBeforeChange")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertEqual(self.Obs.parameter2.pop(0), "Label")
        self.assertEqual(self.Obs.signal.pop(0), "DocChanged")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertEqual(self.Obs.parameter2.pop(0), "Label")
        self.assertEqual(self.Obs.signal.pop(0), "DocRelabled")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        self.Doc2 = FreeCAD.newDocument("Observer2")
        if FreeCAD.GuiUp:
            self.assertEqual(self.Obs.signal.pop(0), "DocActivated")
            self.assertTrue(self.Obs.parameter.pop(0) is self.Doc2)
        self.assertEqual(self.Obs.signal.pop(0), "DocCreated")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc2)
        self.assertEqual(self.Obs.signal.pop(0), "DocBeforeChange")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc2)
        self.assertEqual(self.Obs.parameter2.pop(0), "Label")
        self.assertEqual(self.Obs.signal.pop(0), "DocChanged")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc2)
        self.assertEqual(self.Obs.parameter2.pop(0), "Label")
        self.assertEqual(self.Obs.signal.pop(0), "DocRelabled")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc2)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        FreeCAD.setActiveDocument("Observer1")
        self.assertEqual(self.Obs.signal.pop(), "DocActivated")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc1)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        # undo/redo is not enabled in cmd line mode by default
        self.Doc2.UndoMode = 1

        # Must set Doc2 as active document before start transaction test. If not,
        # then a transaction will be auto created inside the active document if a
        # new transaction is triggered from a non active document
        FreeCAD.setActiveDocument("Observer2")
        self.assertEqual(self.Obs.signal.pop(), "DocActivated")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        self.Doc2.openTransaction("test")
        # openTransaction() now only setup pending transaction, which will only be
        # created when there is actual change
        self.Doc2.addObject("App::FeatureTest", "test")
        self.assertEqual(self.Obs.signal[0], "DocOpenTransaction")
        self.assertEqual(self.Obs.signal.count("DocOpenTransaction"), 1)
        self.assertTrue(self.Obs.parameter[0] is self.Doc2)
        self.assertEqual(self.Obs.parameter2[0], "test")
        self.Obs.clear()

        self.Doc2.commitTransaction()
        self.assertEqual(self.Obs.signal.pop(), "DocCommitTransaction")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        self.Doc2.openTransaction("test2")
        # openTransaction() now only setup pending transaction, which will only be
        # created when there is actual change
        self.Doc2.addObject("App::FeatureTest", "test")
        self.assertEqual(self.Obs.signal[0], "DocOpenTransaction")
        self.assertEqual(self.Obs.signal.count("DocOpenTransaction"), 1)
        self.assertTrue(self.Obs.parameter[0] is self.Doc2)
        self.assertEqual(self.Obs.parameter2[0], "test2")
        # there will be other signals because of the addObject()
        self.Obs.clear()

        self.Doc2.abortTransaction()
        self.assertEqual(self.Obs.signal.pop(), "DocAbortTransaction")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        # there will be other signals because of aborting the above addObject()
        self.Obs.clear()

        self.Doc2.undo()
        self.assertEqual(self.Obs.signal.pop(), "DocUndo")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        # there will be other signals because undoing the above addObject()
        self.Obs.clear()

        self.Doc2.redo()
        self.assertEqual(self.Obs.signal.pop(), "DocRedo")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        # there will be other signals because redoing the above addObject()
        self.Obs.clear()

        self.Doc1.Comment = "test comment"
        if transactionLogIsOn():
            self.assertEqual(self.Obs.signal.pop(0), "DocOpenTransaction")
            self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
            self.assertEqual(self.Obs.parameter2.pop(0), "<implicit>")
        self.assertEqual(self.Obs.signal.pop(0), "DocBeforeChange")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertEqual(self.Obs.parameter2.pop(0), "Comment")
        self.assertEqual(self.Obs.signal.pop(0), "DocChanged")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertEqual(self.Obs.parameter2.pop(0), "Comment")

        FreeCAD.closeDocument(self.Doc2.Name)
        self.assertEqual(self.Obs.signal.pop(), "DocDeleted")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        if FreeCAD.GuiUp:
            # only has document activated signal when running in GUI mode
            self.assertEqual(self.Obs.signal.pop(), "DocActivated")
            self.assertTrue(self.Obs.parameter.pop() is self.Doc1)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        FreeCAD.closeDocument(self.Doc1.Name)
        self.assertEqual(self.Obs.signal.pop(), "DocDeleted")
        self.assertEqual(self.Obs.parameter.pop(), self.Doc1)
        if transactionLogIsOn():
            # the close commits the implicit transaction of the Comment write
            self.assertEqual(self.Obs.signal.pop(), "DocCommitTransaction")
            self.assertEqual(self.Obs.parameter.pop(), self.Doc1)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

    def testObject(self):
        # testing signal on object changes

        self.Doc1 = FreeCAD.newDocument("Observer1")
        self.Obs.clear()

        obj = self.Doc1.addObject("App::DocumentObject", "obj")
        self.assertTrue(self.Obs.signal.pop() == "ObjCreated")
        self.assertTrue(self.Obs.parameter.pop() is obj)
        # there are multiple object change signals
        self.Obs.clear()

        obj.Label = "myobj"
        self.assertTrue(self.Obs.signal.pop(0) == "ObjBeforeChange")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertTrue(self.Obs.parameter2.pop(0) == "Label")
        self.assertTrue(self.Obs.signal.pop(0) == "ObjChanged")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertTrue(self.Obs.parameter2.pop(0) == "Label")
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        obj.enforceRecompute()
        obj.recompute()
        self.assertTrue(self.Obs.signal.pop(0) == "ObjRecomputed")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        obj.enforceRecompute()
        self.Doc1.recompute()
        self.assertTrue(self.Obs.signal.pop(0) == "ObjRecomputed")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertTrue(self.Obs.signal.pop(0) == "DocRecomputed")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        if transactionLogIsOn():
            # the implicit transaction addObject opened, closed with the
            # recompute's invocation
            self.assertTrue(self.Obs.signal.pop(0) == "DocCommitTransaction")
            self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        FreeCAD.ActiveDocument.removeObject(obj.Name)
        self.assertTrue(self.Obs.signal.pop(0) == "ObjDeleted")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        pyobj = self.Doc1.addObject("App::FeaturePython", "pyobj")
        self.Obs.clear()
        pyobj.addProperty("App::PropertyLength", "Prop", "Group", "test property")
        self.assertTrue(self.Obs.signal.pop() == "ObjAddDynProp")
        self.assertTrue(self.Obs.parameter.pop() is pyobj)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        pyobj.setEditorMode("Prop", ["ReadOnly"])
        self.assertTrue(self.Obs.signal.pop() == "ObjChangePropEdit")
        self.assertTrue(self.Obs.parameter.pop() is pyobj)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        pyobj.removeProperty("Prop")
        self.assertTrue(self.Obs.signal.pop() == "ObjRemoveDynProp")
        self.assertTrue(self.Obs.parameter.pop() is pyobj)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        pyobj.addExtension("App::GroupExtensionPython")
        self.assertTrue(self.Obs.signal.pop() == "ObjDynExt")
        self.assertTrue(self.Obs.parameter.pop() is pyobj)
        self.assertTrue(self.Obs.parameter2.pop() == "App::GroupExtensionPython")
        self.assertTrue(self.Obs.signal.pop(0) == "ObjBeforeDynExt")
        self.assertTrue(self.Obs.parameter.pop(0) is pyobj)
        self.assertTrue(self.Obs.parameter2.pop(0) == "App::GroupExtensionPython")
        # a proxy property was changed, hence those events are also in the signal list
        self.Obs.clear()

        FreeCAD.closeDocument(self.Doc1.Name)
        self.Obs.clear()

    def testUndoDisabledDocument(self):

        # testing document level signals
        self.Doc1 = FreeCAD.newDocument("Observer1")
        self.Doc1.UndoMode = 0
        self.Obs.clear()

        self.Doc1.openTransaction("test")
        self.Doc1.commitTransaction()
        self.Doc1.undo()
        self.Doc1.redo()
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)

        FreeCAD.closeDocument(self.Doc1.Name)
        self.Obs.clear()

    def testGuiObserver(self):

        if not FreeCAD.GuiUp:
            return

        # in case another document already exists then the tests cannot
        # be done reliably
        if FreeCAD.activeDocument():
            return

        self.GuiObs = self.GuiObserver()
        FreeCAD.Gui.addDocumentObserver(self.GuiObs)
        self.Doc1 = FreeCAD.newDocument("Observer1")
        self.GuiDoc1 = FreeCAD.Gui.getDocument(self.Doc1.Name)
        self.Obs.clear()
        self.assertTrue(self.GuiObs.signal.pop(0) == "DocCreated")
        self.assertTrue(self.GuiObs.parameter.pop(0) is self.GuiDoc1)
        self.assertTrue(self.GuiObs.signal.pop(0) == "DocActivated")
        self.assertTrue(self.GuiObs.parameter.pop(0) is self.GuiDoc1)
        self.assertTrue(self.GuiObs.signal.pop(0) == "DocRelabled")
        self.assertTrue(self.GuiObs.parameter.pop(0) is self.GuiDoc1)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        self.Doc1.Label = "test"
        self.assertTrue(self.Obs.signal.pop() == "DocRelabled")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc1)
        # not interested in the change signals
        self.Obs.clear()
        self.assertTrue(self.GuiObs.signal.pop(0) == "DocRelabled")
        self.assertTrue(self.GuiObs.parameter.pop(0) is self.GuiDoc1)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        FreeCAD.setActiveDocument(self.Doc1.Name)
        self.assertTrue(self.Obs.signal.pop() == "DocActivated")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc1)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)
        self.assertTrue(self.GuiObs.signal.pop() == "DocActivated")
        self.assertTrue(self.GuiObs.parameter.pop() is self.GuiDoc1)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        obj = self.Doc1.addObject("App::FeaturePython", "obj")
        self.assertTrue(self.Obs.signal.pop() == "ObjCreated")
        self.assertTrue(self.Obs.parameter.pop() is obj)
        # there are multiple object change signals
        self.Obs.clear()
        self.assertTrue(self.GuiObs.signal.pop() == "ObjCreated")
        self.assertTrue(self.GuiObs.parameter.pop() is obj.ViewObject)

        # There are object change signals, caused by sync of obj.Visibility. Same below.
        self.GuiObs.clear()

        obj.ViewObject.Visibility = False
        self.assertTrue(self.Obs.signal.pop() == "ObjChanged")
        self.assertTrue(self.Obs.parameter.pop() is obj)
        self.assertTrue(self.Obs.parameter2.pop() == "Visibility")
        self.assertTrue(self.Obs.signal.pop() == "ObjBeforeChange")
        self.assertTrue(self.Obs.parameter.pop() is obj)
        self.assertTrue(self.Obs.parameter2.pop() == "Visibility")
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)
        self.assertTrue(self.GuiObs.signal.pop(0) == "ObjChanged")
        self.assertTrue(self.GuiObs.parameter.pop(0) is obj.ViewObject)
        self.assertTrue(self.GuiObs.parameter2.pop(0) == "Visibility")
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        obj.ViewObject.addProperty("App::PropertyLength", "Prop", "Group", "test property")
        self.assertTrue(self.Obs.signal.pop() == "ObjAddDynProp")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        obj.ViewObject.setEditorMode("Prop", ["ReadOnly"])
        self.assertTrue(self.Obs.signal.pop() == "ObjChangePropEdit")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        obj.ViewObject.removeProperty("Prop")
        self.assertTrue(self.Obs.signal.pop() == "ObjRemoveDynProp")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        self.GuiDoc1.setEdit("obj", 0)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)
        self.assertTrue(self.GuiObs.signal.pop(0) == "ObjInEdit")
        self.assertTrue(self.GuiObs.parameter.pop(0) is obj.ViewObject)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        self.GuiDoc1.resetEdit()
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)
        self.assertTrue(self.GuiObs.signal.pop(0) == "ObjResetEdit")
        self.assertTrue(self.GuiObs.parameter.pop(0) is obj.ViewObject)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        obj.ViewObject.addExtension("Gui::ViewProviderGroupExtensionPython")
        self.assertTrue(self.Obs.signal.pop() == "ObjDynExt")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Gui::ViewProviderGroupExtensionPython")
        self.assertTrue(self.Obs.signal.pop() == "ObjBeforeDynExt")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Gui::ViewProviderGroupExtensionPython")
        # a proxy property was changed, hence those events are also in the signal list (but of GUI observer)
        self.GuiObs.clear()

        vo = obj.ViewObject
        FreeCAD.ActiveDocument.removeObject(obj.Name)
        self.assertTrue(self.Obs.signal.pop(0) == "ObjDeleted")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertTrue(not self.Obs.signal and not self.Obs.parameter and not self.Obs.parameter2)
        self.assertTrue(self.GuiObs.signal.pop() == "ObjDeleted")
        self.assertTrue(self.GuiObs.parameter.pop() is vo)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        FreeCAD.closeDocument(self.Doc1.Name)
        self.Obs.clear()
        self.assertTrue(self.GuiObs.signal.pop() == "DocDeleted")
        self.assertTrue(self.GuiObs.parameter.pop() is self.GuiDoc1)
        self.assertTrue(
            not self.GuiObs.signal and not self.GuiObs.parameter and not self.GuiObs.parameter2
        )

        FreeCAD.Gui.removeDocumentObserver(self.GuiObs)
        self.GuiObs.clear()

    def tearDown(self):
        # closing doc
        FreeCAD.removeDocumentObserver(self.Obs)
        self.Obs.clear()
        self.Obs = None


class FeatureTestColumn(unittest.TestCase):
    def setUp(self):
        doc = FreeCAD.newDocument("TestColumn")
        self.obj = doc.addObject("App::FeatureTestColumn", "Column")

    def testEmpty(self):
        value = self.obj.Value
        self.obj.Column = ""
        self.assertFalse(self.obj.recompute())
        self.assertEqual(self.obj.Value, value)

    def testA(self):
        self.obj.Column = "A"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 0)

    def testZ(self):
        self.obj.Column = "Z"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 25)

    def testAA(self):
        self.obj.Column = "AA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 26)

    def testAB(self):
        self.obj.Column = "AB"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 27)

    def testAZ(self):
        self.obj.Column = "AZ"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 51)

    def testBA(self):
        self.obj.Column = "BA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 52)

    def testCB(self):
        self.obj.Column = "CB"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 79)

    def testZA(self):
        self.obj.Column = "ZA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 676)

    def testZZ(self):
        self.obj.Column = "ZZ"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 701)

    def testAAA(self):
        self.obj.Column = "AAA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 702)

    def testAAZ(self):
        self.obj.Column = "AAZ"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 727)

    def testCBA(self):
        self.obj.Column = "CBA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 2080)

    def testAZA(self):
        self.obj.Column = "AZA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 1352)

    def testZZA(self):
        self.obj.Column = "ZZA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 18252)

    def testZZZ(self):
        self.obj.Column = "ZZZ"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 18277)

    def testALL(self):
        self.obj.Column = "ALL"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 999)

    def testAb(self):
        value = self.obj.Value
        self.obj.Column = "Ab"
        self.assertFalse(self.obj.recompute())
        self.assertEqual(self.obj.Value, value)

    def testABCD(self):
        value = self.obj.Value
        self.obj.Column = "ABCD"
        self.assertFalse(self.obj.recompute())
        self.assertEqual(self.obj.Value, value)

    def testEmptySilent(self):
        self.obj.Column = ""
        self.obj.Silent = True
        self.assertTrue(self.obj.recompute())
        self.assertEqual(self.obj.Value, -1)

    def testAbSilent(self):
        self.obj.Column = "Ab"
        self.obj.Silent = True
        self.assertTrue(self.obj.recompute())
        self.assertEqual(self.obj.Value, -1)

    def testABCDSilent(self):
        self.obj.Column = "ABCD"
        self.obj.Silent = True
        self.assertTrue(self.obj.recompute())
        self.assertEqual(self.obj.Value, -1)

    def tearDown(self):
        FreeCAD.closeDocument("TestColumn")


class FeatureTestRow(unittest.TestCase):
    def setUp(self):
        doc = FreeCAD.newDocument("TestRow")
        self.obj = doc.addObject("App::FeatureTestRow", "Row")

    def testEmpty(self):
        self.obj.Silent = True
        self.obj.Row = ""
        self.obj.recompute()
        self.assertEqual(self.obj.Value, -1)

    def testA(self):
        self.obj.Silent = True
        self.obj.Row = "A"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, -1)

    def testException(self):
        value = self.obj.Value
        self.obj.Row = "A"
        self.assertFalse(self.obj.recompute())
        self.assertEqual(self.obj.Value, value)

    def test0(self):
        self.obj.Silent = True
        self.obj.Row = "0"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, -1)

    def test1(self):
        self.obj.Silent = True
        self.obj.Row = "1"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 0)

    def test16384(self):
        self.obj.Silent = True
        self.obj.Row = "16384"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 16383)

    def test16385(self):
        self.obj.Silent = True
        self.obj.Row = "16385"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, -1)

    def tearDown(self):
        FreeCAD.closeDocument("TestRow")


class FeatureTestAbsAddress(unittest.TestCase):
    def setUp(self):
        doc = FreeCAD.newDocument("TestAbsAddress")
        self.obj = doc.addObject("App::FeatureTestAbsAddress", "Cell")

    def testAbsoluteA12(self):
        self.obj.Address = "$A$12"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, True)

    def testAbsoluteA13(self):
        self.obj.Address = "A$13"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, True)

    def testAbsoluteAA13(self):
        self.obj.Address = "AA$13"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, True)

    def testAbsoluteZZ12(self):
        self.obj.Address = "$ZZ$12"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, True)

    def testAbsoluteABC1(self):
        self.obj.Address = "$ABC1"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def testAbsoluteABC2(self):
        self.obj.Address = "ABC$2"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def testRelative(self):
        self.obj.Address = "A1"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def testInvalid(self):
        self.obj.Address = "A"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def testEmpty(self):
        self.obj.Address = ""
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def tearDown(self):
        FreeCAD.closeDocument("TestAbsAddress")


class FeatureTestAttribute(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("TestAttribute")
        self.doc.UndoMode = 0

    def testValidAttribute(self):
        obj = self.doc.addObject("App::FeatureTestAttribute", "Attribute")
        obj.Object = obj
        obj.Attribute = "Name"
        self.doc.recompute()
        self.assertIn("Up-to-date", obj.State)

    def testInvalidAttribute(self):
        obj = self.doc.addObject("App::FeatureTestAttribute", "Attribute")
        obj.Object = obj
        obj.Attribute = "Name123"
        self.doc.recompute()
        self.assertIn("Invalid", obj.State)
        self.assertIn("Touched", obj.State)

    def testRemoval(self):
        obj = self.doc.addObject("App::FeatureTestAttribute", "Attribute")
        obj.Object = obj
        self.assertEqual(self.doc.removeObject("Attribute"), None)

    def tearDown(self):
        FreeCAD.closeDocument("TestAttribute")


class TransactionBranchCases(unittest.TestCase):
    # Branches of the transaction log in a file (docs/TransactionLog.md
    # sec 17, 26): the branch travels with the embedded history, and a file
    # edited elsewhere keeps its history as closed branches.

    def setUp(self):
        self.param = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        self.mode = self.param.GetInt("TransactionLog", 2)
        self.param.SetInt("TransactionLog", 2)  # embedded
        self.dir = tempfile.mkdtemp(prefix="fc-branches-")
        self.docs = []

    def tearDown(self):
        for name in self.docs:
            if name in FreeCAD.listDocuments():
                FreeCAD.closeDocument(name)
        self.param.SetInt("TransactionLog", self.mode)

    def track(self, doc):
        self.docs.append(doc.Name)
        return doc

    def saved(self):
        doc = self.track(FreeCAD.newDocument("Branches"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        obj = doc.addObject("App::FeatureTest", "Obj")
        obj.Integer = 1
        doc.commitTransaction()
        doc.createTransactionBranch("side")
        doc.openTransaction("side edit")
        obj.Integer = 2
        doc.commitTransaction()
        path = os.path.join(self.dir, "branches.FCStd")
        doc.saveAs(path)
        return doc, path

    def branches(self, doc):
        return {b["name"]: b for b in doc.getTransactionBranches()}

    def testOpenVersionIsADocumentOfItsOwn(self):
        # Sec 27.7: a version opens beside the document, on the file's one
        # log, once; its first change makes it a branch.
        doc, path = self.saved()
        versions = doc.getTransactionVersions()
        first = versions[0]["num"]
        opened = self.track(doc.openTransactionVersion(first, False))
        self.assertNotEqual(opened.Name, doc.Name)
        # Editable, named for the branch it will continue (sec 27.23).
        self.assertEqual(opened.FileName, doc.FileName + "@main@v%d" % first)
        self.assertTrue(opened.Label.endswith("@main@v%d" % first))
        self.assertIs(doc.openTransactionVersion(first, False), opened)
        cursor = opened.getTransactionCursor()
        self.assertTrue(cursor["detached"])
        self.assertEqual(cursor["version"], first)
        self.assertEqual(sorted(cursor["documents"]), sorted([doc.Name, opened.Name]))
        self.assertEqual(opened.getObject("Obj").Integer, 1)
        with self.assertRaises(ValueError):
            opened.save()
        opened.openTransaction("edit the version")
        opened.getObject("Obj").Integer = 9
        opened.commitTransaction()
        cursor = opened.getTransactionCursor()
        self.assertFalse(cursor["detached"])
        # The document is on `side`, so `main`, whose tip the version is, is
        # free: the version's document continues it.
        names = {b["id"]: b["name"] for b in doc.getTransactionBranches()}
        self.assertEqual(names[cursor["branch"]], "main")
        self.assertEqual(doc.getObject("Obj").Integer, 2)

    def testOpenAClosedFilesVersion(self):
        # Sec 27.13: a version of a file no document has open, read out of
        # the archive.
        doc, path = self.saved()
        first = doc.getTransactionVersions()[0]["num"]
        FreeCAD.closeDocument(doc.Name)
        opened = self.track(FreeCAD.openFileVersion(path, first, False))
        self.assertTrue(opened.FileName.endswith("@v%d" % first))
        self.assertEqual(opened.getObject("Obj").Integer, 1)
        self.assertTrue(opened.getTransactionCursor()["detached"])
        with self.assertRaises(Exception):
            FreeCAD.openFileVersion(path, 999, False)

    def testSaveAVersionAsTheFile(self):
        # Sec 27.16: save() refuses a version document; saveVersionAsFile()
        # writes it over the file, which reopens on the version's branch.
        doc, path = self.saved()
        first = doc.getTransactionVersions()[0]["num"]
        opened = self.track(doc.openTransactionVersion(first, False))
        opened.openTransaction("edit the version")
        opened.getObject("Obj").Integer = 7
        opened.commitTransaction()
        saved = opened.saveVersionAsFile()
        self.assertGreater(saved, first)
        # The name ends in the version it last saved (sec 27.23).
        self.assertTrue(opened.FileName.endswith("@main@v%d" % saved))
        with self.assertRaises(ValueError):
            opened.save()
        for d in (opened, doc):
            FreeCAD.closeDocument(d.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual(doc.getObject("Obj").Integer, 7)
        self.assertEqual(doc.Branch, "main")
        self.assertEqual(int(doc.Version.split()[0]), saved)

        # Unchanged, and not the tip of its branch any more: saved as the
        # file, it makes a branch of its own, and the file reopens on it
        # whatever branch the store last named.
        again = self.track(doc.openTransactionVersion(first, False))
        again.saveVersionAsFile()
        for d in (again, doc):
            FreeCAD.closeDocument(d.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual(doc.getObject("Obj").Integer, 1)
        self.assertEqual(doc.Branch, "main@v%d" % first)
        names = {b["id"]: b["name"] for b in doc.getTransactionBranches()}
        self.assertEqual(names[doc.getTransactionCursor()["branch"]], doc.Branch)

    def testPinnedLinkKeepsItsVersion(self):
        # Sec 16.5, 27.7: a link pinned to a version of another file shows
        # that version, opened as a document of its own, across the linked
        # file's later saves and a reopen; unpinned, it shows the file.
        import zipfile

        part = self.track(FreeCAD.newDocument("PinPart"))
        part.UndoMode = 1
        part.openTransaction("create")
        target = part.addObject("App::FeatureTest", "Obj")
        target.Integer = 1
        part.commitTransaction()
        partPath = os.path.join(self.dir, "pinpart.FCStd")
        part.saveAs(partPath)
        onDisk = int(part.Version.split()[0])

        asm = self.track(FreeCAD.newDocument("PinAsm"))
        asm.UndoMode = 1
        asm.openTransaction("link")
        link = asm.addObject("App::Link", "L")
        link.LinkedObject = target
        asm.commitTransaction()
        asmPath = os.path.join(self.dir, "pinasm.FCStd")
        asm.saveAs(asmPath)

        asm.openTransaction("pin")
        pinned = link.pinLink("LinkedObject")
        asm.commitTransaction()
        self.assertEqual(pinned, onDisk)
        # One step: the version document opened by the pin joins no
        # transaction, so the pin's is not committed half way (sec 27.15).
        self.assertEqual(asm.UndoNames, ["pin", "link"])
        version, uuid, fellBack = link.getLinkPin("LinkedObject")
        self.assertEqual(version, onDisk)
        self.assertTrue(uuid)
        self.assertFalse(fellBack)
        shown = link.LinkedObject
        self.assertTrue(shown.Document.FileName.endswith("@v%d" % onDisk))
        self.track(shown.Document)
        self.assertEqual(shown.Integer, 1)

        # The linked file moves on, saved with the preference off: pinned,
        # it keeps its history anyway (27.5 ruling 1).
        self.param.SetInt("TransactionLog", 1)
        part.openTransaction("two")
        target.Integer = 2
        part.commitTransaction()
        part.save()
        self.assertIn("blobs/History.db", zipfile.ZipFile(partPath).namelist())
        self.assertEqual(link.LinkedObject.Integer, 1)
        self.param.SetInt("TransactionLog", 2)

        # Reopened, with the part closed: the pin opens the version.
        asm.save()
        for doc in (asm, shown.Document, part):
            FreeCAD.closeDocument(doc.Name)
        asm = self.track(FreeCAD.openDocument(asmPath))
        asm.UndoMode = 1
        link = asm.getObject("L")
        shown = link.LinkedObject
        self.assertIsNotNone(shown)
        self.track(shown.Document)
        self.assertTrue(shown.Document.FileName.endswith("@v%d" % onDisk))
        self.assertEqual(shown.Integer, 1)
        self.assertEqual(link.getLinkPin("LinkedObject")[0], onDisk)

        # Unpinned: the file itself, as it is now; undone: the version again.
        asm.openTransaction("unpin")
        link.unpinLink("LinkedObject")
        asm.commitTransaction()
        self.assertIsNone(link.getLinkPin("LinkedObject"))
        live = link.LinkedObject
        self.track(live.Document)
        self.assertEqual(live.Document.FileName, partPath)
        self.assertEqual(live.Integer, 2)
        self.assertEqual(asm.UndoNames, ["unpin"])
        asm.undo()
        self.assertEqual(link.getLinkPin("LinkedObject")[0], onDisk)
        self.assertTrue(link.LinkedObject.Document.FileName.endswith("@v%d" % onDisk))
        asm.redo()
        self.assertIsNone(link.getLinkPin("LinkedObject"))
        self.assertEqual(link.LinkedObject.Document.FileName, partPath)

    def testReleasedPinnedVersionClosesHeadless(self):
        # Sec 27.30, 27.38: with ClosePinnedVersion at close, a version
        # opened for pins is closed once the operation that let go of its
        # last pin has ended -- here, with no Gui, as in the Gui.
        part = self.track(FreeCAD.newDocument("RelPart"))
        part.UndoMode = 1
        part.openTransaction("create")
        target = part.addObject("App::FeatureTest", "Obj")
        part.commitTransaction()
        partPath = os.path.join(self.dir, "relpart.FCStd")
        part.saveAs(partPath)

        asm = self.track(FreeCAD.newDocument("RelAsm"))
        asm.UndoMode = 1
        asm.openTransaction("links")
        link = asm.addObject("App::Link", "L")
        link.LinkedObject = target
        other = asm.addObject("App::Link", "M")
        other.LinkedObject = target
        asm.commitTransaction()
        asm.saveAs(os.path.join(self.dir, "relasm.FCStd"))

        def frozenName():
            return link.LinkedObject.Document.Name

        previous = self.param.GetInt("ClosePinnedVersion", 0)
        self.param.SetInt("ClosePinnedVersion", 1)
        try:
            asm.openTransaction("pin")
            link.pinLink("LinkedObject")
            asm.commitTransaction()
            name = frozenName()
            self.assertIn(name, FreeCAD.listDocuments())

            # Unpinned: closed as soon as the unpin has returned.
            asm.openTransaction("unpin")
            link.unpinLink("LinkedObject")
            asm.commitTransaction()
            self.assertNotIn(name, FreeCAD.listDocuments())
            # Undone: pinned again, and the version opened again for it.
            asm.undo()
            name = frozenName()
            self.assertIn(name, FreeCAD.listDocuments())
            # By file: the undo's copy named the closed document.
            self.assertTrue(FreeCAD.getDocument(name).FileName.startswith(partPath + "@"))

            # A second pin keeps it open when the first goes.
            asm.openTransaction("pin other")
            other.pinLink("LinkedObject", link.getLinkPin("LinkedObject")[0])
            asm.commitTransaction()
            self.assertEqual(other.LinkedObject.Document.Name, name)
            asm.openTransaction("unpin one")
            link.unpinLink("LinkedObject")
            asm.commitTransaction()
            self.assertIn(name, FreeCAD.listDocuments())

            # The last pinning link deleted: closed.
            asm.openTransaction("delete")
            asm.removeObject("M")
            asm.commitTransaction()
            self.assertNotIn(name, FreeCAD.listDocuments())

            # The linking document closed: closed with it.
            asm.openTransaction("pin again")
            link.pinLink("LinkedObject")
            asm.commitTransaction()
            name = frozenName()
            FreeCAD.closeDocument(asm.Name)
            self.assertNotIn(name, FreeCAD.listDocuments())
            self.assertIn(part.Name, FreeCAD.listDocuments())
        finally:
            self.param.SetInt("ClosePinnedVersion", 0)

        try:
            self.askKeeps()
        finally:
            self.param.SetInt("ClosePinnedVersion", previous)

    def askKeeps(self):
        # Ask, with no one to ask, keeps it; so does an explicit drain.
        asm = self.track(FreeCAD.openDocument(os.path.join(self.dir, "relasm.FCStd")))
        asm.UndoMode = 1
        link = asm.getObject("L")
        link.pinLink("LinkedObject")
        name = link.LinkedObject.Document.Name
        self.track(FreeCAD.getDocument(name))
        link.unpinLink("LinkedObject")
        self.assertIn(name, FreeCAD.listDocuments())
        self.assertEqual(FreeCAD.closeReleasedVersions(), 0)
        self.assertIn(name, FreeCAD.listDocuments())

    def testPinnedVersionIsFrozen(self):
        # Sec 27.22-27.24: what a pin shows is the version itself -- frozen,
        # every change refused -- and the same version opens a second time,
        # editable, for work; a link to that one is live, to its branch.
        import zipfile

        part = self.track(FreeCAD.newDocument("FrozenPart"))
        part.UndoMode = 1
        part.openTransaction("create")
        target = part.addObject("App::FeatureTest", "Obj")
        target.Integer = 1
        part.commitTransaction()
        partPath = os.path.join(self.dir, "frozenpart.FCStd")
        part.saveAs(partPath)
        onDisk = int(part.Version.split()[0])

        asm = self.track(FreeCAD.newDocument("FrozenAsm"))
        asm.UndoMode = 1
        link = asm.addObject("App::Link", "L")
        link.LinkedObject = target
        asmPath = os.path.join(self.dir, "frozenasm.FCStd")
        asm.saveAs(asmPath)
        link.pinLink("LinkedObject")
        shown = link.LinkedObject
        frozen = self.track(shown.Document)
        self.assertEqual(frozen.FileName, partPath + "@v%d" % onDisk)
        self.assertTrue(frozen.Label.endswith("@v%d" % onDisk))

        with self.assertRaises(Exception):
            shown.Integer = 99
        self.assertEqual(shown.Integer, 1)
        with self.assertRaises(Exception):
            frozen.addObject("App::FeatureTest", "More")
        with self.assertRaises(Exception):
            frozen.saveVersionAsFile()

        # The file moves on. The editable instance of the pinned version is
        # then a document of its own -- while the file's document was still
        # at the version, it was that document (27.12).
        self.assertIs(FreeCAD.openFileVersion(partPath, onDisk, False), part)
        part.openTransaction("two")
        target.Integer = 2
        part.commitTransaction()
        part.save()
        editable = self.track(FreeCAD.openFileVersion(partPath, onDisk, False))
        self.assertIsNot(editable, frozen)
        self.assertIsNot(editable, part)
        eobj = editable.getObject("Obj")
        editable.openTransaction("edit")
        eobj.Integer = 7
        editable.commitTransaction()
        self.assertEqual(shown.Integer, 1)
        self.assertEqual(link.LinkedObject.Integer, 1)
        branch = editable.FileName[len(partPath) + 1 : editable.FileName.rindex("@")]
        self.assertTrue(branch)

        # A link to the editable instance is live, to its branch: no pin.
        live = asm.addObject("App::Link", "Live")
        live.LinkedObject = eobj
        self.assertIsNone(live.getLinkPin("LinkedObject"))
        self.assertEqual(live.LinkedObject.Integer, 7)
        asm.save()
        xml = zipfile.ZipFile(asmPath).read("Document.xml").decode("utf-8")
        self.assertIn('branch="%s"' % branch, xml)

        # Reopened with the branch still held: the link finds its holder.
        FreeCAD.closeDocument(asm.Name)
        asm = self.track(FreeCAD.openDocument(asmPath))
        live = asm.getObject("Live")
        self.assertIs(live.LinkedObject.Document, editable)
        self.assertEqual(live.LinkedObject.Integer, 7)
        self.assertEqual(asm.getObject("L").LinkedObject.Integer, 1)

    def testLocalLinkPinnedToItsOwnVersion(self):
        # Sec 27.20, 27.21: a link to an object of its own document, pinned
        # to a version of the document's own file -- saved with no file,
        # shown through the version's frozen instance, found again after a
        # reopen and a Save As.
        import zipfile

        doc = self.track(FreeCAD.newDocument("SelfPin"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        obj = doc.addObject("App::FeatureTest", "Obj")
        obj.Integer = 1
        link = doc.addObject("App::Link", "L")
        link.LinkedObject = obj
        doc.commitTransaction()
        path = os.path.join(self.dir, "selfpin.FCStd")
        doc.saveAs(path)
        first = int(doc.Version.split()[0])
        doc.openTransaction("two")
        obj.Integer = 2
        doc.commitTransaction()
        doc.save()

        doc.openTransaction("pin")
        self.assertEqual(link.pinLink("LinkedObject", first), first)
        doc.commitTransaction()
        shown = link.LinkedObject
        frozen = self.track(shown.Document)
        self.assertEqual(frozen.FileName, path + "@v%d" % first)
        self.assertEqual(shown.Integer, 1)
        self.assertEqual(obj.Integer, 2)
        self.assertEqual(link.getLinkPin("LinkedObject")[0], first)
        # The live document moves on; the pin does not.
        doc.openTransaction("three")
        obj.Integer = 3
        doc.commitTransaction()
        self.assertEqual(link.LinkedObject.Integer, 1)

        doc.save()
        xml = zipfile.ZipFile(path).read("Document.xml").decode("utf-8")
        self.assertIn('<XLink file="" stamp=', xml)
        self.assertIn('version="%d"' % first, xml)

        # Reopened: the pin opens its own file's version again.
        for d in (frozen, doc):
            FreeCAD.closeDocument(d.Name)
        doc = self.track(FreeCAD.openDocument(path))
        doc.UndoMode = 1
        link = doc.getObject("L")
        shown = link.LinkedObject
        self.assertIsNotNone(shown)
        self.track(shown.Document)
        self.assertIsNot(shown.Document, doc)
        self.assertEqual(shown.Integer, 1)
        self.assertEqual(doc.getObject("Obj").Integer, 3)

        # Save As: the pin follows the file, whose history went with it.
        path2 = os.path.join(self.dir, "selfpin2.FCStd")
        doc.saveAs(path2)
        FreeCAD.closeDocument(shown.Document.Name)
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path2))
        doc.UndoMode = 1
        link = doc.getObject("L")
        self.track(link.LinkedObject.Document)
        self.assertEqual(link.LinkedObject.Document.FileName, path2 + "@v%d" % first)
        self.assertEqual(link.LinkedObject.Integer, 1)

        # Unpinned: the object of that name in the document; undone: pinned.
        doc.openTransaction("unpin")
        link.unpinLink("LinkedObject")
        doc.commitTransaction()
        self.assertIsNone(link.getLinkPin("LinkedObject"))
        self.assertIs(link.LinkedObject, doc.getObject("Obj"))
        doc.undo()
        self.assertEqual(link.getLinkPin("LinkedObject")[0], first)
        self.assertEqual(link.LinkedObject.Integer, 1)
        doc.redo()
        self.assertIs(link.LinkedObject, doc.getObject("Obj"))

        # Pinned to the version on disk (sec 27.21 Q4): a copy of the saved
        # state, which later edits do not reach.
        doc.openTransaction("pin current")
        onDisk = link.pinLink("LinkedObject")
        doc.commitTransaction()
        self.track(link.LinkedObject.Document)
        self.assertEqual(onDisk, int(doc.Version.split()[0]))
        self.assertEqual(link.LinkedObject.Integer, 3)
        doc.getObject("Obj").Integer = 4
        self.assertEqual(link.LinkedObject.Integer, 3)

        # A version without the object: refused.
        other = doc.addObject("App::FeatureTest", "New")
        link2 = doc.addObject("App::Link", "L2")
        link2.LinkedObject = other
        with self.assertRaises(Exception):
            link2.pinLink("LinkedObject", first)
        self.assertIs(link2.LinkedObject, other)

    def testSaveToTheLogOnly(self):
        # Sec 27.22, 27.28: a version recorded, and kept, in the file's
        # history -- the file written again with that history in it, but
        # opening as it did.
        import re
        import zipfile

        def model(path):
            xml = zipfile.ZipFile(path).read("Document.xml").decode("utf-8")
            return re.sub(r"<History[^>]*/>|<History .*?</History>", "", xml, flags=re.S)

        doc = self.track(FreeCAD.newDocument("LogOnly"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        obj = doc.addObject("App::FeatureTest", "Obj")
        obj.Integer = 1
        doc.commitTransaction()
        path = os.path.join(self.dir, "logonly.FCStd")
        doc.saveAs(path)
        onDisk = int(doc.Version.split()[0])
        before = model(path)

        doc.openTransaction("edit")
        obj.Integer = 5
        doc.commitTransaction()
        num = doc.saveToLog()
        self.assertGreater(num, onDisk)
        self.assertEqual(model(path), before)
        # Sec 27.37: the old history's database is gone -- nothing but the
        # history it replaced referred to it -- and the content index lists
        # what the archive now holds, the new database included.
        import ArchiveMembers

        names = zipfile.ZipFile(path).namelist()
        self.assertEqual(len([n for n in names if n.endswith(".db")]), 1)
        index = ArchiveMembers.readFile(path, "blobs/Content.xml").decode("utf-8")
        listed = sorted("blobs/" + n for n in re.findall(r'<F n="([^"]*)"', index))
        held = sorted(n for n in names if n.startswith("blobs/") and n != "blobs/Content.xml")
        self.assertEqual(listed, held)
        self.assertLess(names.index("blobs/Content.xml"),
                        min(names.index(n) for n in held))
        self.assertEqual(int(doc.Version.split()[0]), onDisk)
        saved = {v["num"]: v for v in doc.getTransactionVersions()}
        self.assertEqual(saved[num]["kind"], "named")
        self.assertEqual(saved[num]["name"], "Saved to history")
        self.assertEqual(obj.Integer, 5)

        # Reopened: the file as it was, its history with the version in it,
        # continued (the guard held), and the version restorable.
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        doc.UndoMode = 1
        self.assertEqual(doc.getObject("Obj").Integer, 1)
        self.assertIn(num, [v["num"] for v in doc.getTransactionVersions()])
        self.assertFalse([b for b in doc.getTransactionBranches() if b["closed"]])
        doc.restoreTransactionVersion(num)
        self.assertEqual(doc.getObject("Obj").Integer, 5)

        # A file that does not carry its history: refused.
        self.param.SetInt("TransactionLog", 1)
        try:
            with self.assertRaises(Exception):
                doc.saveToLog()
        finally:
            self.param.SetInt("TransactionLog", 2)

    def testAFileCarriesOneHistory(self):
        # Sec 27.29: a version holds what the document refers to, not the
        # history database its History property names -- else each save
        # carried the history before it as one more member.
        import zipfile

        doc = self.track(FreeCAD.newDocument("OneHistory"))
        doc.UndoMode = 1
        obj = doc.addObject("App::FeatureTest", "Obj")
        path = os.path.join(self.dir, "onehistory.FCStd")
        for i in range(4):
            doc.openTransaction("edit %d" % i)
            obj.Integer = i
            doc.commitTransaction()
            if i == 0:
                doc.saveAs(path)
            else:
                doc.save()
            dbs = [n for n in zipfile.ZipFile(path).namelist() if n.endswith(".db")]
            self.assertEqual(dbs, ["blobs/History.db"])
        # Reopened and saved again: the version the open records holds no
        # history either.
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        doc.getObject("Obj").Integer = 9
        doc.save()
        dbs = [n for n in zipfile.ZipFile(path).namelist() if n.endswith(".db")]
        self.assertEqual(dbs, ["blobs/History.db"])
        versions = doc.getTransactionVersions()
        self.assertTrue(versions)

    def testTheHistoryCarriesEveryBlobItNames(self):
        # Sec 27.37: a value the log captures from a detached copy of a
        # property -- a Part::Box's material card, made on first use -- went
        # to the process-wide default store, where the save did not look:
        # the file's history named a blob the file did not carry.
        import re
        import sqlite3
        import zipfile
        import ArchiveMembers

        doc = self.track(FreeCAD.newDocument("HistoryBlobs"))
        doc.UndoMode = 1
        doc.openTransaction("box")
        try:
            doc.addObject("Part::Box", "Box")
        except Exception:
            doc.abortTransaction()
            self.skipTest("Part is not available")
        doc.commitTransaction()
        doc.recompute()
        path = os.path.join(self.dir, "historyblobs.FCStd")
        doc.saveAs(path)
        with zipfile.ZipFile(path) as archive:
            xml = archive.read("Document.xml").decode("utf-8")
            start = xml.index("<History db=")
            element = xml[start : xml.index("</History>", start)]
            db = re.search(r'db="([0-9a-f]+)"', element).group(1)
            carried = set(re.findall(r'<Blob hash="([0-9a-f]+)"', element))
            index = ArchiveMembers.read(archive, "blobs/Content.xml").decode("utf-8")
            member = re.search(r'<F n="([^"]*)" h="%s"' % db, index).group(1)
            data = ArchiveMembers.read(archive, "blobs/" + member)
        dbPath = os.path.join(self.dir, "historyblobs.db")
        with open(dbPath, "wb") as f:
            f.write(data)
        con = sqlite3.connect(dbPath)
        try:
            named = {h for (h,) in con.execute(
                "SELECT lower(hex(hash)) FROM entity WHERE kind='blob' AND enc='file'")}
        finally:
            con.close()
        self.assertTrue(named, "the material card is a blob the history holds")
        self.assertEqual(named, carried)

    def testRestoreAndSwitchGoThroughTheRows(self):
        # Sec 27.25 item 1, 27.34: a restore to a version and a branch switch
        # move the document through the rows between -- no scratch document
        # is made, and the values are the version's.
        class Made:
            def __init__(self):
                self.names = []

            def slotCreatedDocument(self, doc):
                self.names.append(doc.Name)

        doc = self.track(FreeCAD.newDocument("Rows"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        obj = doc.addObject("App::FeatureTest", "Obj")
        obj.Integer = 1
        doc.commitTransaction()
        path = os.path.join(self.dir, "rows.FCStd")
        doc.saveAs(path)
        first = int(doc.Version.split()[0])
        doc.openTransaction("two")
        obj.Integer = 2
        extra = doc.addObject("App::FeatureTest", "Extra")
        doc.commitTransaction()
        doc.openTransaction("three")
        obj.Integer = 3
        doc.commitTransaction()

        made = Made()
        FreeCAD.addDocumentObserver(made)
        try:
            doc.restoreTransactionVersion(first)
            self.assertEqual(made.names, [])
            self.assertEqual(obj.Integer, 1)
            self.assertIsNone(doc.getObject("Extra"))
            doc.undo()
            self.assertEqual(doc.getObject("Obj").Integer, 3)
            self.assertIsNotNone(doc.getObject("Extra"))

            # A branch from the first version, edited, and back to main.
            doc.createTransactionBranch("side", first)
            self.assertEqual(made.names, [])
            self.assertEqual(doc.getObject("Obj").Integer, 1)
            doc.openTransaction("side edit")
            doc.getObject("Obj").Integer = 7
            doc.commitTransaction()
            doc.switchTransactionBranch("main")
            self.assertEqual(made.names, [])
            self.assertEqual(doc.getObject("Obj").Integer, 3)
            self.assertIsNotNone(doc.getObject("Extra"))
            doc.switchTransactionBranch("side")
            self.assertEqual(made.names, [])
            self.assertEqual(doc.getObject("Obj").Integer, 7)
            self.assertIsNone(doc.getObject("Extra"))
        finally:
            FreeCAD.removeDocumentObserver(made)

    def testRestoreAcrossAnOpenGoesThroughTheRows(self):
        # Sec 27.57: an open's record was taken for a jump whatever the file
        # was -- the version it records is at the row's parent, not at the row
        # -- so a restore to the file as found read the version whole, 30 times
        # slower on a real model. Opened with no history, the file is version
        # 1, and going back to it is going back through the rows.
        class Made:
            def __init__(self):
                self.names = []

            def slotCreatedDocument(self, doc):
                self.names.append(doc.Name)

        doc = self.track(FreeCAD.newDocument("AcrossOpen"))
        doc.addObject("App::FeatureTest", "Obj").Integer = 1
        path = os.path.join(self.dir, "acrossopen.FCStd")
        self.param.SetInt("TransactionLog", 1)
        try:
            doc.saveAs(path)
        finally:
            self.param.SetInt("TransactionLog", 2)
        FreeCAD.closeDocument(doc.Name)

        doc = self.track(FreeCAD.openDocument(path))
        doc.UndoMode = 1
        for value in (2, 3):
            doc.openTransaction("edit")
            doc.getObject("Obj").Integer = value
            doc.commitTransaction()
        doc.resolveTransactionLog()
        versions = doc.getTransactionVersions()
        self.assertEqual([v["num"] for v in versions], [1])

        made = Made()
        FreeCAD.addDocumentObserver(made)
        try:
            doc.restoreTransactionVersion(1)
            self.assertEqual(made.names, [])
            self.assertEqual(doc.getObject("Obj").Integer, 1)
            doc.undo()
            self.assertEqual(doc.getObject("Obj").Integer, 3)
        finally:
            FreeCAD.removeDocumentObserver(made)

    def testWholeReadAndWalkBringBackARemovedShape(self):
        # Sec 27.60: a switch the rows cannot make reads the version whole --
        # into a document joined to the file's history -- and writes what
        # differs, shapes named by their blobs among it. And the walk back
        # across a dynamic property's removal brought the property back
        # without its value (a null shape).
        class Made:
            def __init__(self):
                self.names = []

            def slotCreatedDocument(self, doc):
                self.names.append(doc.Name)

        doc = self.track(FreeCAD.newDocument("WholeRead"))
        doc.UndoMode = 1
        # Shapes as blobs named by their hash.
        doc.SaveSchemaVersion = 5
        doc.openTransaction("box")
        try:
            import Part

            box = doc.addObject("Part::Box", "Box")
        except Exception:
            doc.abortTransaction()
            self.skipTest("Part is not available")
        doc.commitTransaction()
        doc.recompute()
        # A shape in a dynamic property, as Part keeps a retained base shape
        # (`_BaseShape<N>`): gone by the time the switch reads the version,
        # so the read adds it and writes its value into a fresh property.
        doc.openTransaction("extra")
        box.addProperty("Part::PropertyPartShape", "Extra")
        box.Extra = Part.makeSphere(2)
        doc.commitTransaction()
        path = os.path.join(self.dir, "wholeread.FCStd")
        doc.saveAs(path)
        first = int(doc.Version.split()[0])
        for length in (20, 30):
            doc.openTransaction("length")
            box.Length = length
            doc.recompute()
            doc.commitTransaction()
        doc.openTransaction("no extra")
        box.removeProperty("Extra")
        doc.commitTransaction()
        doc.createTransactionBranch("again", first)
        self.assertAlmostEqual(box.Shape.Volume, 1000.0)
        self.assertAlmostEqual(box.Extra.Volume, 4 / 3 * math.pi * 8, 6)
        doc.switchTransactionBranch("main")
        self.assertAlmostEqual(box.Shape.Volume, 3000.0)
        self.assertFalse(hasattr(box, "Extra"))
        # Every row only main holds goes, with no bridge (sec 27.71): nothing
        # to walk from here to again.
        doc.trimTransactionBranch("main", bridge=False)
        made = Made()
        FreeCAD.addDocumentObserver(made)
        try:
            doc.switchTransactionBranch("again")
        finally:
            FreeCAD.removeDocumentObserver(made)
        self.assertEqual(len(made.names), 1, "the version is read whole")
        self.assertEqual(box.Length.Value, 10.0)
        self.assertAlmostEqual(box.Shape.Volume, 1000.0)
        self.assertAlmostEqual(box.Extra.Volume, 4 / 3 * math.pi * 8, 6)
        doc.switchTransactionBranch("main")
        self.assertEqual(box.Length.Value, 30.0)
        self.assertAlmostEqual(box.Shape.Volume, 3000.0)

    def testTouchedStateThroughTheRows(self):
        # Sec 27.58: the rows carry the touched state -- a set the state
        # before it, a recompute record each object's before and after -- so a
        # restore through the rows leaves every object touched as it was at the
        # version, across recomputes. The walk used to purge an object whose
        # derived values came back and leave the rest alone.
        class Made:
            def __init__(self):
                self.names = []

            def slotCreatedDocument(self, doc):
                self.names.append(doc.Name)

        doc = self.track(FreeCAD.newDocument("TouchedRows"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        a = doc.addObject("App::FeatureTest", "A")
        b = doc.addObject("App::FeatureTest", "B")
        b.Link = a
        c = doc.addObject("App::FeatureTest", "C")
        doc.commitTransaction()
        doc.recompute()
        # A command's pattern: the edit and its recompute in one transaction.
        doc.openTransaction("edit A")
        a.Integer = 2
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("edit C")
        c.Integer = 5
        doc.commitTransaction()
        state = lambda: {o.Name: "Touched" in o.State for o in (a, b, c)}
        atVersion = {"A": False, "B": False, "C": True}
        self.assertEqual(state(), atVersion)
        path = os.path.join(self.dir, "touchedrows.FCStd")
        doc.saveAs(path)
        version = int(doc.Version.split()[0])

        doc.recompute()
        doc.openTransaction("edit A again")
        a.Integer = 3
        doc.commitTransaction()
        atHead = {"A": True, "B": False, "C": False}
        self.assertEqual(state(), atHead)

        made = Made()
        FreeCAD.addDocumentObserver(made)
        try:
            doc.restoreTransactionVersion(version)
            self.assertEqual(made.names, [])
            self.assertEqual(a.Integer, 2)
            self.assertEqual(state(), atVersion)
            # Sec 27.63: undo and redo leave the state the rows record -- not
            # every write touched, and a flag the restore changed with no
            # value (C) put back from the restore's own record.
            doc.undo()
            self.assertEqual(a.Integer, 3)
            self.assertEqual(state(), atHead)
            doc.redo()
            self.assertEqual(a.Integer, 2)
            self.assertEqual(state(), atVersion)
            doc.undo()
            self.assertEqual(state(), atHead)
        finally:
            FreeCAD.removeDocumentObserver(made)

    def testUndoRowsSayTheStateTheyLeft(self):
        # Sec 27.63: an undo's row records the touched state the undo left, so
        # the walk crossing it forward -- a switch back to the branch it is on
        # -- does not take its writes for edits and touch what they wrote.
        doc = self.track(FreeCAD.newDocument("UndoRows"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        a = doc.addObject("App::FeatureTest", "A")
        doc.commitTransaction()
        doc.recompute()
        path = os.path.join(self.dir, "undorows.FCStd")
        doc.saveAs(path)
        first = int(doc.Version.split()[0])
        doc.openTransaction("edit")
        a.Integer = 5
        doc.commitTransaction()
        self.assertIn("Touched", a.State)
        doc.undo()
        self.assertEqual(a.Integer, 4711)
        self.assertNotIn("Touched", a.State, "the undo leaves the state before the edit")
        doc.createTransactionBranch("other", first)
        doc.openTransaction("edit other")
        a.Integer = 6
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        self.assertEqual(a.Integer, 4711)
        self.assertNotIn("Touched", a.State)

    def testOpeningADocumentKeepsAnotherOnesTransaction(self):
        # Sec 27.15: a document made or opened while another has a transaction
        # open joins none, so its restore does not commit that transaction
        # half way and leave the rest of it as a step of its own.
        other = self.track(FreeCAD.newDocument("MidOther"))
        other.addObject("App::FeatureTest", "Obj")
        otherPath = os.path.join(self.dir, "midother.FCStd")
        other.saveAs(otherPath)
        FreeCAD.closeDocument(other.Name)

        doc = self.track(FreeCAD.newDocument("MidTxn"))
        doc.UndoMode = 1
        obj = doc.addObject("App::FeatureTest", "Obj")
        doc.clearUndos()
        doc.openTransaction("edit")
        obj.Integer = 1
        self.track(FreeCAD.newDocument("MidNew"))
        obj.Integer = 2
        self.track(FreeCAD.openDocument(otherPath))
        obj.Integer = 3
        doc.commitTransaction()
        self.assertEqual(doc.UndoNames, ["edit"])
        doc.undo()
        self.assertEqual(obj.Integer, 4711)

    def testAnUnsavedPropertyOpensNoTransaction(self):
        # Sec 27.17: a property never saved, added while a command's
        # transaction is pending (a cache made by an isActive() check), is
        # no change: the command leaves no empty undo step behind.
        doc = self.track(FreeCAD.newDocument("UnsavedProp"))
        doc.UndoMode = 1
        obj = doc.addObject("App::FeatureTest", "Obj")
        doc.clearUndos()
        FreeCAD.setActiveTransaction("command")
        obj.addProperty("App::PropertyInteger", "Cache", "Base", "", 32)   # Prop_NoPersist
        FreeCAD.closeActiveTransaction()
        self.assertEqual(doc.UndoNames, [])
        FreeCAD.setActiveTransaction("command")
        obj.addProperty("App::PropertyInteger", "Kept", "Base", "")
        FreeCAD.closeActiveTransaction()
        self.assertEqual(doc.UndoNames, ["command"])

    def testPinThatCannotBeFoundShowsTheFile(self):
        # Sec 27.5 ruling 2: a version that cannot be opened falls back to
        # the file, with a warning, and the pin stays for the next open.
        part = self.track(FreeCAD.newDocument("PinGone"))
        target = part.addObject("App::FeatureTest", "Obj")
        target.Integer = 5
        partPath = os.path.join(self.dir, "pingone.FCStd")
        part.saveAs(partPath)
        asm = self.track(FreeCAD.newDocument("PinGoneAsm"))
        link = asm.addObject("App::Link", "L")
        link.LinkedObject = target
        asmPath = os.path.join(self.dir, "pingoneasm.FCStd")
        asm.saveAs(asmPath)
        pinned = link.pinLink("LinkedObject")
        self.track(link.LinkedObject.Document)
        asm.save()
        # The part replaced by a copy with no history.
        plain = os.path.join(self.dir, "pingone-plain.FCStd")
        part.saveCopy(plain, False)
        for doc in list(FreeCAD.listDocuments().values()):
            if doc.Name in self.docs:
                FreeCAD.closeDocument(doc.Name)
        os.replace(plain, partPath)
        asm = self.track(FreeCAD.openDocument(asmPath))
        link = asm.getObject("L")
        version, uuid, fellBack = link.getLinkPin("LinkedObject")
        self.assertEqual(version, pinned)
        self.assertTrue(fellBack)
        shown = link.LinkedObject
        self.assertIsNotNone(shown)
        self.track(shown.Document)
        self.assertEqual(shown.Document.FileName, partPath)
        self.assertEqual(shown.Integer, 5)

    def testSwitchAndRestoreKeepTheLabel(self):
        # The version a switch or a restore applies is read into a scratch
        # document with a name of its own; the document keeps its label
        # (sec 27.11).
        doc, path = self.saved()
        label = doc.Label
        doc.switchTransactionBranch("main")
        self.assertEqual(doc.Label, label)
        doc.switchTransactionBranch("side")
        self.assertEqual(doc.Label, label)
        versions = doc.getTransactionVersions()
        doc.restoreTransactionVersion(versions[0]["num"])
        self.assertEqual(doc.Label, label)

    def testSaveWithHistoryIsNoUndoStep(self):
        # The save adds and sets History, Version and Branch itself: none of
        # that is the user's edit, so none of it is a step to undo (sec 27.5).
        doc, path = self.saved()
        before = list(doc.UndoNames)
        doc.save()
        self.assertEqual(list(doc.UndoNames), before)
        self.assertIn("History", doc.PropertiesList)
        self.assertFalse([n for n in doc.UndoNames if n.startswith("<implicit")])

    def testBranchTravelsInTheFile(self):
        doc, path = self.saved()
        self.assertEqual(doc.Branch, "side")
        FreeCAD.closeDocument(doc.Name)
        opened = self.track(FreeCAD.openDocument(path))
        branches = self.branches(opened)
        self.assertEqual(sorted(branches), ["main", "side"])
        self.assertTrue(branches["side"]["current"])
        self.assertEqual(opened.getObject("Obj").Integer, 2)
        # Renamed: the branch the document is on keeps the file's Branch in step.
        self.assertTrue(opened.renameTransactionBranch("side", "wide"))
        self.assertEqual(sorted(self.branches(opened)), ["main", "wide"])
        self.assertEqual(opened.Branch, "wide")
        with self.assertRaises(Exception):
            opened.renameTransactionBranch("wide", "main")  # taken
        with self.assertRaises(Exception):
            opened.renameTransactionBranch("nowhere", "x")
        self.assertTrue(opened.switchTransactionBranch("main"))
        self.assertEqual(opened.getObject("Obj").Integer, 1)

    def testEditedElsewhereKeepsClosedBranches(self):
        doc, path = self.saved()
        before = max(v["num"] for v in doc.getTransactionVersions())
        FreeCAD.closeDocument(doc.Name)
        # Edited elsewhere: a FreeCAD that knows no log restamps the date.
        import re
        import zipfile
        import ArchiveMembers

        edited = os.path.join(self.dir, "edited.FCStd")
        with zipfile.ZipFile(edited, "w", zipfile.ZIP_DEFLATED) as target:
            for item, data in ArchiveMembers.members(path):
                if item.filename == "Document.xml":
                    data, n = re.subn(
                        rb'(name="LastModifiedDate".*?<String value=")[^"]*',
                        rb"\g<1>1999-01-01T00:00:00Z",
                        data,
                        count=1,
                        flags=re.S,
                    )
                    self.assertEqual(n, 1)
                target.writestr(item, data)

        opened = self.track(FreeCAD.openDocument(edited))
        branches = self.branches(opened)
        self.assertEqual(len(branches), 3)
        closed = [b for b in branches.values() if b["closed"]]
        self.assertEqual(len(closed), 2)
        self.assertTrue(any(n.startswith("main@") for n in branches))
        main = branches["main"]
        self.assertTrue(main["current"])
        self.assertFalse(main["closed"])
        self.assertGreaterEqual(main["from_version"], 1)
        # The file as found is the new main's first version, numbered on,
        # and its row roots a chain of its own.
        versions = [v for v in opened.getTransactionVersions() if v["branch"] == "main"]
        self.assertEqual(len(versions), 1)
        self.assertGreater(versions[0]["num"], before)
        rows = [r for r in opened.getTransactionLog() if r["branch"] == "main"]
        self.assertEqual(rows[0]["parent"], 0)
        self.assertEqual(opened.getObject("Obj").Integer, 2)
        # A closed branch is not switched to; one of its versions is branched from.
        with self.assertRaises(Exception):
            opened.switchTransactionBranch("side")
        fork = [v for v in opened.getTransactionVersions() if v["name"] == "branch side"]
        self.assertEqual(len(fork), 1)
        opened.createTransactionBranch("revived", version=fork[0]["num"])
        self.assertEqual(opened.getObject("Obj").Integer, 1)

    def testReopenHandsOutNoDeletedId(self):
        # Sec 27.40 item 1: a deleted object's id is not handed out again
        # after a reopen -- from the file's LastId with the log off, and from
        # the file's counter with it on; and two branches share one counter.
        for mode in (0, 2):
            self.param.SetInt("TransactionLog", mode)
            doc = self.track(FreeCAD.newDocument("Ids"))
            doc.UndoMode = 1
            doc.addObject("App::FeatureTest", "Keep")
            gone = doc.addObject("App::FeatureTest", "Gone")
            goneId = gone.ID
            doc.removeObject("Gone")
            path = os.path.join(self.dir, "ids%d.FCStd" % mode)
            doc.saveAs(path)
            FreeCAD.closeDocument(doc.Name)
            doc = self.track(FreeCAD.openDocument(path))
            made = doc.addObject("App::FeatureTest", "Made")
            self.assertGreater(made.ID, goneId, "mode %d" % mode)
            FreeCAD.closeDocument(doc.Name)
        self.param.SetInt("TransactionLog", 2)
        doc = self.track(FreeCAD.newDocument("Ids"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        doc.addObject("App::FeatureTest", "Obj")
        doc.commitTransaction()
        doc.createTransactionBranch("side")
        doc.openTransaction("side")
        sideId = doc.addObject("App::FeatureTest", "Side").ID
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        doc.openTransaction("main")
        mainId = doc.addObject("App::FeatureTest", "Main").ID
        doc.commitTransaction()
        self.assertGreater(mainId, sideId)
        self.assertTrue(all(b["id_base"] == 0 for b in doc.getTransactionBranches()))

    def testObjectNamesAreFileScope(self):
        # Sec 27.40 item 3, 27.41 Q3: one name per object across the file,
        # never freed; an object coming back has its own name and id.
        doc = self.track(FreeCAD.newDocument("Names"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        doc.addObject("App::FeatureTest", "Box")
        gone = doc.addObject("App::FeatureTest", "Box")
        self.assertEqual(gone.Name, "Box001")
        goneId = gone.ID
        doc.commitTransaction()
        doc.openTransaction("remove")
        doc.removeObject("Box001")
        doc.commitTransaction()
        # Undo brings it back as it was; redo removes it again.
        doc.undo()
        back = doc.getObject("Box001")
        self.assertTrue(back)
        self.assertEqual(back.ID, goneId)
        doc.redo()
        self.assertFalse(doc.getObject("Box001"))
        # A new object does not take the removed one's name.
        doc.openTransaction("again")
        again = doc.addObject("App::FeatureTest", "Box")
        doc.commitTransaction()
        self.assertEqual(again.Name, "Box002")
        # Nor after a reopen.
        path = os.path.join(self.dir, "names.FCStd")
        doc.saveAs(path)
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        doc.UndoMode = 1
        doc.openTransaction("reopened")
        self.assertEqual(doc.addObject("App::FeatureTest", "Box").Name, "Box003")
        doc.commitTransaction()
        # Two branches adding a Pad get two names.
        doc.createTransactionBranch("side")
        doc.openTransaction("side pad")
        sideName = doc.addObject("App::FeatureTest", "Pad").Name
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        doc.openTransaction("main pad")
        mainName = doc.addObject("App::FeatureTest", "Pad").Name
        doc.commitTransaction()
        self.assertEqual(sideName, "Pad")
        self.assertEqual(mainName, "Pad001")
        doc.switchTransactionBranch("side")
        self.assertEqual(doc.getObject("Pad").TypeId, "App::FeatureTest")
        self.assertFalse(doc.getObject("Pad001"))
        # The table travels in the file.
        doc.save()
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual(doc.addObject("App::FeatureTest", "Pad").Name, "Pad002")

    def testFileSharesOneStringHasher(self):
        # Sec 27.40 item 2: every document of a file hashes with the file's
        # one hasher, so a shape has one element map in every version; and
        # a string id the saved state no longer uses is not reused.
        doc = self.track(FreeCAD.newDocument("Hashes"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        a = doc.addObject("Part::Box", "A")
        b = doc.addObject("Part::Box", "B")
        b.Placement.Base = FreeCAD.Vector(5, 5, 5)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base = a
        cut.Tool = b
        doc.recompute()
        doc.commitTransaction()
        self.assertGreater(doc.Hasher.Size, 0)
        dropped = doc.Hasher.getID("not used by anything").Value
        path = os.path.join(self.dir, "hashes.FCStd")
        doc.saveAs(path)
        first = max(v["num"] for v in doc.getTransactionVersions())
        doc.openTransaction("edit")
        a.Length = 20
        doc.recompute()
        doc.commitTransaction()
        doc.save()
        v = self.track(doc.openTransactionVersion(first, False))
        self.assertTrue(v.Hasher.isSame(doc.Hasher))
        a.Length = 10
        doc.recompute()
        self.assertEqual(v.getObject("Cut").Shape.ElementMap, cut.Shape.ElementMap)
        FreeCAD.closeDocument(v.Name)
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertGreater(doc.Hasher.getID("a new string").Value, dropped)

    def cutDocument(self, name):
        doc = self.track(FreeCAD.newDocument(name))
        doc.UndoMode = 1
        doc.openTransaction("create")
        a = doc.addObject("Part::Box", "A")
        b = doc.addObject("Part::Box", "B")
        b.Placement.Base = FreeCAD.Vector(5, 5, 5)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base = a
        cut.Tool = b
        doc.recompute()
        doc.commitTransaction()
        return doc

    def testStringTableIsAMemberOfItsOwn(self):
        # Sec 27.50 items 1-3: a schema-5 file keeps its string table as a
        # member of its own, read before the objects; the log keeps the table
        # once and no version carries one; the file reopened and a version
        # opened from its log have the element maps they had.
        import zipfile

        doc = self.cutDocument("Table")
        em = doc.getObject("Cut").Shape.ElementMap
        path = os.path.join(self.dir, "table.FCStd")
        doc.saveAs(path)
        with zipfile.ZipFile(path) as z:
            self.assertIn("StringTable.txt", z.namelist())
            xml = z.read("Document.xml").decode()
            table = z.read("StringTable.txt").decode("latin-1")
        self.assertEqual(xml.count("<StringHasher2"), 1)
        self.assertIn('<StringHasher2 table="StringTable.txt" hash="', xml)
        self.assertTrue(table.startswith("StringTableStart v1 %d\n" % doc.Hasher.Size))
        versions = doc.getTransactionVersions()
        for v in versions:
            self.assertNotIn("StringTable.txt", [e for e, h in v["manifest"]])
            docxml = [h for e, h in v["manifest"] if e == "Document.xml"][0]
            self.assertIn('<StringHasher2 table="', doc.getTransactionValue(docxml)[0])
        num = max(v["num"] for v in versions)
        # A later save, so that the version is not the file; named, so that
        # the file carries it.
        doc.nameTransactionVersion(num, "first")
        doc.openTransaction("edit")
        doc.getObject("A").Length = 20
        doc.recompute()
        doc.commitTransaction()
        doc.save()
        FreeCAD.closeDocument(doc.Name)

        doc = self.track(FreeCAD.openDocument(path))
        self.assertIn(num, [v["num"] for v in doc.getTransactionVersions()])
        v = self.track(doc.openTransactionVersion(num, False))
        self.assertTrue(v.Hasher.isSame(doc.Hasher))
        self.assertEqual(v.getObject("Cut").Shape.ElementMap, em)
        FreeCAD.closeDocument(v.Name)
        FreeCAD.closeDocument(doc.Name)
        # And with no document of the file open: the history from the
        # archive takes the file's table.
        v = self.track(FreeCAD.openFileVersion(path, num, False))
        self.assertEqual(v.getObject("Cut").Shape.ElementMap, em)

    def testSaveCompactsTheStringTable(self):
        # Sec 27.50 item 4, 27.51 Q3-Q4: a save drops every string nothing
        # holds and no retained version or value uses, and keeps the ones
        # only the log's versions and values use -- by the ids recorded with
        # each, not by reading them back.
        doc = self.cutDocument("Compacts")
        em = doc.getObject("Cut").Shape.ElementMap
        path = os.path.join(self.dir, "compacts.FCStd")
        doc.saveAs(path)
        first = max(v["num"] for v in doc.getTransactionVersions())
        before = set(doc.Hasher.Table)
        unused = doc.Hasher.getID("held by nothing").Value
        doc.openTransaction("remove")
        doc.removeObject("Cut")
        doc.commitTransaction()
        doc.clearUndos()
        doc.save()
        after = set(doc.Hasher.Table)
        self.assertNotIn(unused, after)
        self.assertTrue(before <= after, sorted(before - after))
        doc.restoreTransactionVersion(first)
        self.assertEqual(doc.getObject("Cut").Shape.ElementMap, em)

    def testKeepAllKeepsEveryString(self):
        # Sec 27.51 Q3 (revised): keep-all keeps every string -- no compaction
        # at a save, none asked for -- and is the file's.
        doc = self.cutDocument("KeepAll")
        doc.Hasher.SaveAll = True
        unused = doc.Hasher.getID("held by nothing").Value
        path = os.path.join(self.dir, "keepall.FCStd")
        doc.saveAs(path)
        self.assertIn(unused, doc.Hasher.Table)
        self.assertEqual(doc.compactFileState()["strings"], 0)
        self.assertIn(unused, doc.Hasher.Table)
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertTrue(doc.Hasher.SaveAll)
        self.assertIn(unused, doc.Hasher.Table)
        doc.Hasher.SaveAll = False
        self.assertIn(unused, doc.Hasher.Table)
        doc.save()
        self.assertNotIn(unused, doc.Hasher.Table)

    def testSketchMintsNoGeometryIdTwice(self):
        # Sec 27.40 item 4, 27.41 Q5: a sketch's geometry ids come from the
        # file's last id for that sketch -- not reused after a deletion and a
        # reopen, nor by two branches.
        import Part

        def line(x):
            return Part.LineSegment(FreeCAD.Vector(x, 0, 0), FreeCAD.Vector(x, 10, 0))

        doc = self.track(FreeCAD.newDocument("GeoIds"))
        doc.UndoMode = 1
        doc.openTransaction("sketch")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        for x in (0, 10, 20):
            sk.addGeometry(line(x))
        doc.commitTransaction()
        gone = sk.getGeometryId(2)
        doc.openTransaction("delete")
        sk.delGeometry(2)
        doc.commitTransaction()
        path = os.path.join(self.dir, "geoids.FCStd")
        doc.saveAs(path)
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        doc.UndoMode = 1
        sk = doc.getObject("Sketch")
        doc.openTransaction("again")
        sk.addGeometry(line(30))
        doc.commitTransaction()
        again = sk.getGeometryId(2)
        self.assertGreater(again, gone)
        doc.createTransactionBranch("side")
        doc.openTransaction("side line")
        sk.addGeometry(line(40))
        doc.commitTransaction()
        side = sk.getGeometryId(3)
        doc.switchTransactionBranch("main")
        sk = doc.getObject("Sketch")
        doc.openTransaction("main line")
        sk.addGeometry(line(50))
        doc.commitTransaction()
        self.assertGreater(sk.getGeometryId(3), side)

    def testVersionOfASchema4FileCarriesItsShapes(self):
        # Sec 27.46, 27.62: a file written before schema 5 keeps its shapes as
        # members at the top of the archive. Version 1, the file as found, is
        # the document as read, serialised at schema 5 -- the log holds no
        # other -- so its shapes are blobs by hash and opening it gives them
        # back; and the document itself is now saved at 5.
        doc = self.track(FreeCAD.newDocument("Schema4"))
        box = doc.addObject("Part::Box", "Box")
        box.Length = 7
        fillet = doc.addObject("Part::Fillet", "Fillet")
        fillet.Base = box
        fillet.Edges = [(1, 1.0, 1.0)]
        doc.recompute()
        volume = fillet.Shape.Volume
        doc.SaveSchemaVersion = 4
        path = os.path.join(self.dir, "schema4.FCStd")
        doc.saveAs(path)
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual(doc.SaveSchemaVersion, 5)
        doc.resolveTransactionLog()
        versions = doc.getTransactionVersions()
        self.assertTrue(versions)
        self.assertEqual({v["schema"] for v in versions}, {5})
        first = min(v["num"] for v in versions)
        v = self.track(doc.openTransactionVersion(first, False))
        self.assertFalse(v.getObject("Fillet").Shape.isNull())
        self.assertAlmostEqual(v.getObject("Fillet").Shape.Volume, volume, places=6)
        self.assertAlmostEqual(v.getObject("Box").Shape.Volume, 7 * 10 * 10, places=6)
        FreeCAD.closeDocument(v.Name)

        # Saved for upstream on purpose: the file is schema 4, the version the
        # log keeps of that save is schema 5.
        doc.getObject("Box").Length = 8
        doc.recompute()
        doc.SaveSchemaVersion = 4
        doc.save()
        import zipfile

        with zipfile.ZipFile(path) as archive:
            self.assertIn('SchemaVersion="4"', archive.read("Document.xml").decode("utf-8"))
        doc.resolveTransactionLog()
        versions = doc.getTransactionVersions()
        self.assertEqual({v["schema"] for v in versions}, {5})
        newest = max(v["num"] for v in versions)
        self.assertGreater(newest, first)
        v = self.track(doc.openTransactionVersion(newest, False))
        self.assertAlmostEqual(v.getObject("Box").Shape.Volume, 8 * 10 * 10, places=6)

    def testCompactFileState(self):
        # Sec 27.47: a name and a last geometry id nothing refers to any more
        # -- no document holds the object, no op names it, no version has it
        # -- are forgotten, and the name is free again; the counters stay.
        import Part

        # Explicit only: the automatic compaction of sec 27.48 off.
        size = self.param.GetInt("TransactionLogCompactSize", 256)
        self.param.SetInt("TransactionLogCompactSize", 0)
        self.addCleanup(self.param.SetInt, "TransactionLogCompactSize", size)
        doc = self.track(FreeCAD.newDocument("Compact"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        doc.addObject("App::FeatureTest", "Obj")
        doc.commitTransaction()
        doc.createTransactionBranch("side")
        doc.openTransaction("side")
        temp = doc.addObject("App::FeatureTest", "Temp")
        tempId = temp.ID
        sk = doc.addObject("Sketcher::SketchObject", "TempSketch")
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(), FreeCAD.Vector(10, 0, 0)))
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        # Still on a branch: nothing goes.
        r = doc.compactFileState()
        self.assertEqual((r["names"], r["geo_ids"]), (0, 0))
        doc.openTransaction("taken")
        self.assertEqual(doc.addObject("App::FeatureTest", "Temp").Name, "Temp001")
        doc.commitTransaction()
        doc.undo()
        doc.deleteTransactionBranch("side")
        r = doc.compactFileState()
        # Temp and TempSketch; Temp001 is still named by the undone rows.
        self.assertEqual(r["names"], 2)
        self.assertEqual(r["geo_ids"], 1)
        self.assertIn("strings", r)
        doc.openTransaction("again")
        again = doc.addObject("App::FeatureTest", "Temp")
        doc.commitTransaction()
        self.assertEqual(again.Name, "Temp")
        self.assertGreater(again.ID, tempId)
        # And it stays compact in the file.
        path = os.path.join(self.dir, "compact.FCStd")
        doc.saveAs(path)
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual(doc.addObject("Sketcher::SketchObject", "TempSketch").Name, "TempSketch")
        self.assertEqual(doc.addObject("App::FeatureTest", "Temp").Name, "Temp002")

    def testTrimEstimatesAndCompacts(self):
        # Sec 27.48, 27.49: deleting a branch estimates the objects left
        # referred to by nothing and their bytes, records it, keeps a
        # file-wide count, and compacts once the bytes reach
        # TransactionLogCompactSize.
        import json

        def trimRecord(doc):
            record = [r for r in doc.getTransactionLog() if r["kind"] == "trim"][-1]
            return json.loads(record["script"])

        size = self.param.GetInt("TransactionLogCompactSize", 256)
        self.addCleanup(self.param.SetInt, "TransactionLogCompactSize", size)
        names = ["Unreferenced%02dObjectName" % i for i in range(40)]
        # 40 names of 24 characters, each with its id: over 1 KB, under 256.
        entryBytes = sum(len(n) for n in names) + 40 * 8
        for setting, compacts in ((256, False), (1, True)):
            self.param.SetInt("TransactionLogCompactSize", setting)
            doc = self.track(FreeCAD.newDocument("Estimate"))
            doc.UndoMode = 1
            doc.openTransaction("create")
            doc.addObject("App::FeatureTest", "Obj")
            doc.commitTransaction()
            doc.createTransactionBranch("side")
            doc.openTransaction("side")
            for n in names:
                doc.addObject("App::FeatureTest", n)
            doc.commitTransaction()
            doc.switchTransactionBranch("main")
            doc.deleteTransactionBranch("side")
            info = trimRecord(doc)
            self.assertEqual(info["unreferenced"], 40, setting)
            self.assertEqual(info["unreferenced_bytes"], entryBytes, setting)
            self.assertEqual(info["compacted"], compacts, setting)
            doc.openTransaction("again")
            name = doc.addObject("App::FeatureTest", names[0]).Name
            doc.commitTransaction()
            self.assertEqual(name, names[0] if compacts else names[0] + "001", setting)
            if not compacts:
                # The count is the file's: it survives a reopen, and an
                # explicit compaction resets it.
                self.assertEqual(info["unreferenced_bytes_total"], entryBytes)
                path = os.path.join(self.dir, "estimate.FCStd")
                doc.saveAs(path)
                FreeCAD.closeDocument(doc.Name)
                doc = self.track(FreeCAD.openDocument(path))
                doc.UndoMode = 1
                doc.createTransactionBranch("more")
                doc.openTransaction("more")
                doc.addObject("App::FeatureTest", "Third")
                doc.commitTransaction()
                doc.switchTransactionBranch("main")
                doc.deleteTransactionBranch("more")
                self.assertEqual(trimRecord(doc)["unreferenced_bytes_total"], entryBytes + 5 + 8)
                self.assertEqual(doc.compactFileState()["names"], 41)
            FreeCAD.closeDocument(doc.Name)

    def pushCold(self, doc, steps=21):
        # Past the hot window (20 steps): what was the top becomes a stub
        # whose undo reads its row (sec 24.3). Returns once the stub is next.
        doc.openTransaction("filler")
        filler = doc.addObject("App::FeatureTest", "Filler")
        doc.commitTransaction()
        for i in range(steps):
            doc.openTransaction("filler %d" % i)
            filler.Integer = i + 1
            doc.commitTransaction()
        for i in range(steps + 1):
            doc.undo()

    def testColdUndoBringsBackACopyOnChangeLink(self):
        # Sec 27.67: a copy-on-change link removed with its private copy
        # comes back whole from the row -- the values the transaction began
        # with, not what the removal's cascade left; its target (an XLink);
        # its copy's links to objects removed with it; the properties the
        # link mirrors, and the status that makes them copy-on-change; the
        # expressions; and every shape's own element map.
        import Part

        doc = self.track(FreeCAD.newDocument("CopyOnChange"))
        doc.UndoMode = 1
        doc.openTransaction("body")
        body = doc.addObject("PartDesign::Body", "Body")
        sketch = body.newObject("Sketcher::SketchObject", "Sketch")
        sketch.addGeometry(Part.Circle(FreeCAD.Vector(), FreeCAD.Vector(0, 0, 1), 5))
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sketch
        body.addProperty("App::PropertyLength", "Config_L", "Config")
        body.setPropertyStatus("Config_L", "CopyOnChange")
        body.Config_L = 10
        pad.setExpression("Length", "hiddenref(Body.Config_L)")
        doc.recompute()
        doc.commitTransaction()

        doc.openTransaction("instance")
        link = doc.addObject("App::Link", "Inst")
        link.LinkedObject = body
        link.LinkCopyOnChange = "Owned"
        link.Config_L = 20
        doc.recompute()
        doc.commitTransaction()
        copy = link.getLinkedObject(False)
        self.assertNotEqual(copy.Name, body.Name)
        before = {o.Name: dict(o.Shape.ElementMap) for o in [copy] + copy.Group}
        copyPad = [o for o in copy.Group if o.isDerivedFrom("PartDesign::Pad")][0].Name
        copySketch = [o for o in copy.Group if o.isDerivedFrom("Sketcher::SketchObject")][0].Name
        # The target is in the log (an XLink copy saved nothing before).
        doc.resolveTransactionLog()
        row = [r for r in doc.getTransactionLog() if r["name"] == "instance"][-1]
        target = [o for o in doc.getTransactionOps(row["seq"]) if o["prop"] == "LinkedObject"][0]
        self.assertIn('name="%s"' % copy.Name, doc.getTransactionValue(target["after"])[0])

        copyName = copy.Name
        doc.openTransaction("remove")
        doc.removeObject("Inst")
        doc.recompute()
        doc.commitTransaction()
        self.assertFalse(doc.getObject(copyName))
        self.pushCold(doc)
        self.assertEqual(doc.UndoNames[0], "remove")
        doc.undo()

        link = doc.getObject("Inst")
        self.assertTrue(link)
        copy = link.getLinkedObject(False)
        self.assertNotEqual(copy.Name, "Inst")
        self.assertEqual(link.LinkCopyOnChange, "Owned")
        self.assertTrue(link.LinkCopyOnChangeGroup)
        self.assertEqual(link.Config_L.Value, 20)
        self.assertIn("CopyOnChange", copy.getPropertyStatus("Config_L"))
        pad = doc.getObject(copyPad)
        self.assertEqual(pad.Profile[0].Name, copySketch)
        self.assertEqual([e[0] for e in pad.ExpressionEngine], ["Length"])
        for o in [copy] + copy.Group:
            self.assertEqual(dict(o.Shape.ElementMap), before[o.Name], o.Name)

    def testColdUndoTakesBackAnExpression(self):
        # Sec 27.67: an engine value restored into a live engine replaces
        # what it holds; undoing the step that added an expression takes it
        # away again.
        doc = self.track(FreeCAD.newDocument("Expression"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        a = doc.addObject("App::FeatureTest", "A")
        b = doc.addObject("App::FeatureTest", "B")
        a.setExpression("Integer", "B.Integer + 1")
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("expression")
        a.setExpression("Float", "B.Integer * 2")
        doc.recompute()
        doc.commitTransaction()
        self.pushCold(doc)
        self.assertEqual(doc.UndoNames[0], "expression")
        doc.undo()
        self.assertEqual([e[0] for e in a.ExpressionEngine], ["Integer"])

    def testSwitchMakesNoOriginOfItsOwn(self):
        # Sec 27.67: a copy-on-change link's private body comes back by a
        # switch with the Origin features its rows name. An Origin makes its
        # axes and planes on first demand; recreated before its
        # OriginFeatures were restored, it made seven more, which stayed.
        import Part

        doc = self.track(FreeCAD.newDocument("SwitchOrigin"))
        doc.UndoMode = 1
        doc.saveAs(os.path.join(self.dir, "switchorigin.FCStd"))

        def step(name, fn):
            doc.openTransaction(name)
            fn()
            doc.recompute()
            doc.commitTransaction()

        def build():
            body = doc.addObject("PartDesign::Body", "Body")
            sketch = body.newObject("Sketcher::SketchObject", "Sketch")
            sketch.addGeometry(Part.Circle(FreeCAD.Vector(), FreeCAD.Vector(0, 0, 1), 5))
            pad = body.newObject("PartDesign::Pad", "Pad")
            pad.Profile = sketch
            body.addProperty("App::PropertyLength", "Config_L", "Config")
            body.setPropertyStatus("Config_L", "CopyOnChange")
            body.Config_L = 10
            pad.setExpression("Length", "hiddenref(Body.Config_L)")

        def instance(name, length):
            link = doc.addObject("App::Link", name)
            link.LinkedObject = doc.getObject("Body")
            link.LinkCopyOnChange = "Owned"
            link.Config_L = length

        def names():
            return sorted(o.Name for o in doc.Objects)

        step("build", build)
        step("A", lambda: instance("A", 20))
        doc.save()
        main = names()
        doc.createTransactionBranch("side")
        step("B", lambda: instance("B", 30))
        doc.save()
        side = names()
        for branch, want in (("main", main), ("side", side), ("main", main), ("side", side)):
            doc.switchTransactionBranch(branch)
            self.assertEqual(names(), want, branch)
        b = doc.getObject("B")
        self.assertNotEqual(b.getLinkedObject(False).Name, "B")
        self.assertEqual(b.Config_L.Value, 30)

    def testSwitchBringsBackARemovedCopyOnChangeLink(self):
        # Sec 27.68: an instance made on main, edited and deleted on a side
        # branch, comes back by the switch to main as it was there. A replay
        # counts as performing a transaction: the link does not react to
        # its target being restored -- it dropped itself to Enabled and the
        # properties it mirrors, as it would for a user's edit.
        import Part

        doc = self.track(FreeCAD.newDocument("SwitchBack"))
        doc.UndoMode = 1
        doc.saveAs(os.path.join(self.dir, "switchback.FCStd"))

        def step(name, fn):
            doc.openTransaction(name)
            fn()
            doc.recompute()
            doc.commitTransaction()

        def build():
            body = doc.addObject("PartDesign::Body", "Body")
            sketch = body.newObject("Sketcher::SketchObject", "Sketch")
            sketch.addGeometry(Part.Circle(FreeCAD.Vector(), FreeCAD.Vector(0, 0, 1), 5))
            pad = body.newObject("PartDesign::Pad", "Pad")
            pad.Profile = sketch
            body.addProperty("App::PropertyLength", "Config_L", "Config")
            body.setPropertyStatus("Config_L", "CopyOnChange")
            body.Config_L = 10
            pad.setExpression("Length", "hiddenref(Body.Config_L)")
            link = doc.addObject("App::Link", "A")
            link.LinkedObject = body
            link.LinkCopyOnChange = "Owned"
            link.Config_L = 20

        step("build", build)
        doc.save()
        main = sorted(o.Name for o in doc.Objects)
        copy = doc.A.getLinkedObject(False).Name
        doc.createTransactionBranch("side")
        step("edit", lambda: setattr(doc.A, "Config_L", 25))
        step("delete", lambda: doc.removeObject("A"))
        doc.save()
        doc.switchTransactionBranch("main")
        link = doc.getObject("A")
        self.assertEqual(link.getLinkedObject(False).Name, copy)
        self.assertEqual(link.LinkCopyOnChange, "Owned")
        self.assertEqual(link.Config_L.Value, 20)
        self.assertEqual(sorted(o.Name for o in doc.Objects), main)

    def testSwitchKeepsTheElementMapVersion(self):
        # Sec 27.72: a shape read back from the log carries the element map
        # version its owner's save writes. The capture of a detached copy
        # wrote one without the hasher prefix; the switch's restore took it
        # for a version change, and once saved, the file asked for a
        # recompute of the object at its next open.
        doc = self.track(FreeCAD.newDocument("MapVersion"))
        doc.UndoMode = 1
        doc.saveAs(os.path.join(self.dir, "mapversion.FCStd"))

        def step(name, fn):
            doc.openTransaction(name)
            fn()
            doc.recompute()
            doc.commitTransaction()

        def build():
            # A primitive's element map is empty, which the check passes;
            # a boolean's is not.
            fuse = doc.addObject("Part::MultiFuse", "Fuse")
            box = doc.addObject("Part::Box", "Box")
            fuse.Shapes = [box, doc.addObject("Part::Cylinder", "Cyl")]

        step("fuse", build)
        self.assertTrue(doc.Fuse.Shape.ElementMapSize)
        doc.save()
        doc.createTransactionBranch("side")
        step("edit", lambda: setattr(doc.Box, "Length", 20))
        doc.save()
        doc.switchTransactionBranch("main")
        self.assertEqual(doc.Box.Length.Value, 10)
        self.assertNotIn("Touched", doc.Fuse.State)
        doc.save()
        path = doc.FileName
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual([o.Name for o in doc.Objects if "Touched" in o.State], [])

    def testSwitchKeepsTheNamesAConsumerMadeOfALink(self):
        # Sec 27.72: a link's own shape is made on demand, and the strings
        # its element names hash to are held only while something holds
        # them; a save drops the rest, log or no log, and the next making
        # mints new ids. What is kept is what a consumer stored: a compound
        # of a copy-on-change link keeps its names across a switch that
        # rebuilds the link's copy, and across its own recompute after.
        import Part

        doc = self.track(FreeCAD.newDocument("LinkConsumer"))
        doc.UndoMode = 1
        doc.saveAs(os.path.join(self.dir, "linkconsumer.FCStd"))

        def step(name, fn):
            doc.openTransaction(name)
            fn()
            doc.recompute()
            doc.commitTransaction()

        def build():
            body = doc.addObject("PartDesign::Body", "Body")
            sketch = body.newObject("Sketcher::SketchObject", "Sketch")
            sketch.addGeometry(Part.Circle(FreeCAD.Vector(), FreeCAD.Vector(0, 0, 1), 5))
            pad = body.newObject("PartDesign::Pad", "Pad")
            pad.Profile = sketch
            body.addProperty("App::PropertyLength", "Config_L", "Config")
            body.setPropertyStatus("Config_L", "CopyOnChange")
            body.Config_L = 10
            pad.setExpression("Length", "hiddenref(Body.Config_L)")
            link = doc.addObject("App::Link", "A")
            link.LinkedObject = body
            link.LinkCopyOnChange = "Owned"
            link.Config_L = 20
            doc.addObject("Part::Compound", "C").Links = [link]

        step("build", build)
        doc.save()
        before = dict(doc.C.Shape.ElementMap)
        self.assertTrue(any(n.startswith("#") for n in before))
        doc.createTransactionBranch("side")
        step("edit", lambda: setattr(doc.A, "Config_L", 25))
        doc.save()
        doc.switchTransactionBranch("main")
        self.assertEqual(doc.A.Config_L.Value, 20)
        self.assertEqual(dict(doc.C.Shape.ElementMap), before)
        step("again", lambda: doc.C.touch())
        self.assertEqual(dict(doc.C.Shape.ElementMap), before)

    def testSwitchRecreatesAShapeOnTheFileHasher(self):
        # Sec 27.74: an object removed on one branch comes back by the
        # switch to the other with its shape on the file's hasher. A value
        # captured from a detached copy names no hasher index, and the
        # recreated object had no hasher to read its element map with: the
        # map kept its names and lost the string ids behind them, and once
        # saved, the file asked for a recompute at its next open.
        doc = self.track(FreeCAD.newDocument("RecreatedHasher"))
        doc.UndoMode = 1
        doc.saveAs(os.path.join(self.dir, "recreatedhasher.FCStd"))

        def step(name, fn):
            doc.openTransaction(name)
            fn()
            doc.recompute()
            doc.commitTransaction()

        def build():
            fuse = doc.addObject("Part::MultiFuse", "Fuse")
            box = doc.addObject("Part::Box", "Box")
            fuse.Shapes = [box, doc.addObject("Part::Cylinder", "Cyl")]

        step("fuse", build)
        self.assertTrue(doc.Fuse.Shape.ElementMapSize)
        before = dict(doc.Fuse.Shape.ElementMap)
        doc.save()
        doc.createTransactionBranch("side")
        step("delete", lambda: doc.removeObject("Fuse"))
        doc.save()
        doc.switchTransactionBranch("main")
        self.assertEqual(dict(doc.Fuse.Shape.ElementMap), before)
        self.assertNotIn("Touched", doc.Fuse.State)
        doc.save()
        path = doc.FileName
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual([o.Name for o in doc.Objects if "Touched" in o.State], [])

    def testCopyOnChangeReferencesKeepTheirNames(self):
        # Sec 27.74: a copy-on-change link makes customized objects and
        # links to them, and what refers by element name refers to those:
        # a fillet in the copy names the copy's pad edges, and a reference
        # into the link names the copy's elements, not the link's own shape.
        # Both keep their mapped names through an instance deleted on one
        # branch and brought back by the switch, a recompute and a reopen.
        import io
        import re
        import zipfile

        import Part

        doc = self.track(FreeCAD.newDocument("CopyReferences"))
        doc.UndoMode = 1
        doc.saveAs(os.path.join(self.dir, "copyreferences.FCStd"))

        def step(name, fn):
            doc.openTransaction(name)
            fn()
            doc.recompute()
            doc.commitTransaction()

        def build():
            body = doc.addObject("PartDesign::Body", "Body")
            sketch = body.newObject("Sketcher::SketchObject", "Sketch")
            corners = [(0, 0), (10, 0), (10, 10), (0, 10)]
            for (x0, y0), (x1, y1) in zip(corners, corners[1:] + corners[:1]):
                sketch.addGeometry(
                    Part.LineSegment(FreeCAD.Vector(x0, y0, 0), FreeCAD.Vector(x1, y1, 0))
                )
            pad = body.newObject("PartDesign::Pad", "Pad")
            pad.Profile = sketch
            body.addProperty("App::PropertyLength", "Config_L", "Config")
            body.setPropertyStatus("Config_L", "CopyOnChange")
            body.Config_L = 10
            pad.setExpression("Length", "hiddenref(Body.Config_L)")
            doc.recompute()
            fillet = body.newObject("PartDesign::Fillet", "Fillet")
            fillet.Base = (pad, ["Edge1", "Edge3"])
            fillet.Radius = 1
            link = doc.addObject("App::Link", "A")
            link.LinkedObject = body
            link.LinkCopyOnChange = "Owned"
            link.Config_L = 20
            doc.recompute()
            ref = doc.addObject("App::FeaturePython", "Ref")
            ref.addProperty("App::PropertyLinkSub", "S")
            ref.S = (link, ["Edge1"])

        def shadows(obj, prop):
            data = bytes(obj.dumpPropertyContent(prop, Compression=0))
            archive = zipfile.ZipFile(io.BytesIO(data))
            xml = "".join(archive.read(n).decode() for n in archive.namelist())
            return re.findall(r'shadow="([^"]*)"', xml)

        def references(doc):
            copy = doc.A.getLinkedObject(False)
            fillet = [o for o in copy.Group if o.isDerivedFrom("PartDesign::Fillet")][0]
            return fillet.Name, shadows(fillet, "Base"), shadows(doc.Ref, "S")

        step("build", build)
        doc.save()
        name, filletRefs, linkRefs = references(doc)
        self.assertNotEqual(name, "Fillet")
        tag = ";:H%x," % doc.getObject(name).ID
        # The copy's own elements: the reference into the link ends in the
        # copy's fillet tag, and the fillet's in its pad's names.
        self.assertEqual(len(filletRefs), 2)
        self.assertIn(tag, linkRefs[0])
        before = references(doc)
        doc.createTransactionBranch("side")
        step("delete", lambda: (doc.removeObject("Ref"), doc.removeObject("A")))
        doc.save()
        doc.switchTransactionBranch("main")
        self.assertEqual(references(doc), before)
        step("recompute", lambda: doc.A.getLinkedObject(False).touch())
        self.assertEqual(references(doc), before)
        doc.save()
        path = doc.FileName
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual(references(doc), before)
        self.assertEqual([o.Name for o in doc.Objects if "Touched" in o.State], [])

    def _heldStringsModel(self, docName):
        # A feature holding a pocket's names and nothing else, one of its
        # edges whose ids include one its text does not show, those ids,
        # and the rest of the old shape's (sec 27.75).
        import re

        import Part

        self.param.SetInt("TransactionLog", 0)
        doc = self.track(FreeCAD.newDocument(docName))
        doc.UndoMode = 0
        doc.saveAs(os.path.join(self.dir, docName.lower() + ".FCStd"))
        V = FreeCAD.Vector
        body = doc.addObject("PartDesign::Body", "Body")
        sketch = body.newObject("Sketcher::SketchObject", "Sketch")
        corners = [(0, 0), (20, 0), (20, 20), (0, 20)]
        for a, b in zip(corners, corners[1:] + corners[:1]):
            sketch.addGeometry(Part.LineSegment(V(*a, 0), V(*b, 0)))
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sketch
        pad.Length = 10
        doc.recompute()
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (pad, ["Edge1", "Edge3"])
        fillet.Radius = 1
        doc.recompute()
        circle = body.newObject("Sketcher::SketchObject", "Circle")
        circle.addGeometry(Part.Circle(V(10, 10, 0), V(0, 0, 1), 3))
        pocket = body.newObject("PartDesign::Pocket", "Pocket")
        pocket.Profile = circle
        pocket.Type = 1
        pocket.Reversed = True
        doc.recompute()
        # A feature of its own holding the pocket's names, and nothing else.
        feature = doc.addObject("Part::Feature", "F")
        feature.Shape = pocket.Shape
        doc.recompute()
        body.removeObjectsFromDocument()
        doc.removeObject("Body")
        doc.recompute()

        def value(sid):
            return sid if isinstance(sid, int) else sid.Value

        def ids(shape, name):
            return {value(x) for x in shape.getElementIndexedName(name, True)[1]}

        def closure(found):
            # What a held string is made from stays with it.
            out, todo = set(), list(found)
            while todo:
                i = todo.pop()
                if i not in out:
                    out.add(i)
                    todo.extend(value(r) for r in doc.Hasher.getID(i).Related)
            return out

        shape = feature.Shape
        hidden = [
            (n, e)
            for n, e in sorted(shape.ElementMap.items())
            if ids(shape, n) - {int(x, 16) for x in re.findall(r"#([0-9a-f]+)", n)}
        ]
        self.assertTrue(hidden)
        name, element = hidden[0]
        held = ids(shape, name)
        others = set()
        for n in shape.ElementMap:
            others |= ids(shape, n)
        others -= closure(held)
        self.assertTrue(others)

        return doc, feature, element, held, others

    def testAReferenceHoldsTheStringsItNames(self):
        # Sec 27.75: a reference into an element holds the string ids of the
        # element's name -- the map's for it, which include what the name
        # was made from and its text does not show -- saved beside its
        # shadow and held again at the next open. With the element gone and
        # nothing else holding them, a save's compaction keeps those and
        # drops the rest of the old shape's. The log is off: its retained
        # versions would keep the rest as well.
        import io
        import re
        import zipfile

        import Part

        doc, feature, element, held, others = self._heldStringsModel("HeldStrings")

        ref = doc.addObject("App::FeaturePython", "Ref")
        ref.addProperty("App::PropertyLinkSub", "S")
        ref.S = (feature, [element])
        doc.recompute()

        def saved(doc):
            data = bytes(doc.Ref.dumpPropertyContent("S", Compression=0))
            archive = zipfile.ZipFile(io.BytesIO(data))
            xml = "".join(archive.read(n).decode() for n in archive.namelist())
            found = re.search(r'sids="([^"]*)"', xml)
            return {int(x, 16) for x in found.group(1).split()} if found else set()

        self.assertEqual(saved(doc), held)
        # The element goes; the feature's retained generation would keep
        # the old shape's strings, so it goes too, and a reopen forgets the
        # one it keeps in memory.
        feature.Shape = Part.makeBox(1, 1, 1)
        doc.recompute()
        for prop in [p for p in feature.PropertiesList if p.startswith("_BaseShape")]:
            feature.removeProperty(prop)
        doc.save()
        path = doc.FileName
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual(saved(doc), held)
        doc.save()
        self.assertEqual({i for i in held if doc.Hasher.getID(i)}, held)
        self.assertEqual({i for i in others if doc.Hasher.getID(i)}, set())

    def testAnExpressionHoldsTheStringsItNames(self):
        # Sec 27.77: an expression's element path holds its string ids as a
        # link does (27.75). The expression text has no room for them, so the
        # engine writes them beside the expressions, and a reopen holds them
        # again -- the element missing by then, which is when nothing else
        # does.
        import io
        import re
        import zipfile

        import Part

        doc, feature, element, held, others = self._heldStringsModel("HeldExpression")
        ref = doc.addObject("App::FeaturePython", "Ref")
        ref.addProperty("App::PropertyFloat", "T")
        ref.setExpression("T", "F.<<%s>>._shape.Length" % element)
        doc.recompute()
        self.assertGreater(ref.T, 0)

        def saved(doc):
            data = bytes(doc.Ref.dumpPropertyContent("ExpressionEngine", Compression=0))
            archive = zipfile.ZipFile(io.BytesIO(data))
            xml = "".join(archive.read(n).decode() for n in archive.namelist())
            found = re.search(r'<Ids index="0" ref="0" sids="([^"]*)"', xml)
            return {int(x, 16) for x in found.group(1).split()} if found else set()

        self.assertEqual(saved(doc), held)
        feature.Shape = Part.makeBox(1, 1, 1)
        doc.recompute()
        for prop in [p for p in feature.PropertiesList if p.startswith("_BaseShape")]:
            feature.removeProperty(prop)
        doc.save()
        path = doc.FileName
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        self.assertEqual(saved(doc), held)
        doc.save()
        self.assertEqual({i for i in held if doc.Hasher.getID(i)}, held)
        self.assertEqual({i for i in others if doc.Hasher.getID(i)}, set())

    def testAShapeCrossingDocumentsTakesTheIdsOfItsTable(self):
        # Sec 27.76 items 1-4, 27.77: a shape made from another document's
        # comes into this document's string table -- names, held ids, and the
        # ids its text names -- instead of keeping the other table's numbers
        # as text, which this table reads as its own strings. The external
        # marker names the source document by an imported string of its Uid.
        # With the source closed, the consumers' names read the same, from
        # this file alone, and nothing asks for a recompute.
        import re

        import Part

        self.param.SetInt("TransactionLog", 0)
        B = self.track(FreeCAD.newDocument("CrossB"))
        box = B.addObject("Part::Box", "Box")
        fillet = B.addObject("Part::Fillet", "Fillet")
        fillet.Base = box
        fillet.Edges = [(1, 1, 1)]
        B.recompute()
        B.saveAs(os.path.join(self.dir, "crossb.FCStd"))
        A = self.track(FreeCAD.newDocument("CrossA"))
        # So that A's ids are not B's.
        for i in range(3):
            A.addObject("Part::Sphere", "Sphere%d" % i)
        A.recompute()
        link = A.addObject("App::Link", "Link")
        link.LinkedObject = fillet
        comp = A.addObject("Part::Compound", "Comp")
        comp.Links = [link]
        fuse = A.addObject("Part::MultiFuse", "Fuse")
        fuse.Shapes = [link, A.Sphere0]
        A.recompute()
        A.saveAs(os.path.join(self.dir, "crossa.FCStd"))

        def expand(hasher, text):
            def one(m):
                sid = hasher.getID(int(m.group(1), 16))
                return expand(hasher, sid.Data) if sid else "<missing>"

            return re.sub(r"#([0-9a-f]+)", one, text)

        def byElement(shape):
            return {e: n for n, e in shape.ElementMap.items()}

        linked = Part.getShape(link)
        self.assertTrue(linked.Hasher.isSame(A.Hasher))
        source = byElement(fillet.Shape)
        for element, name in byElement(linked).items():
            # The link's name says what the fillet's says, then the marker.
            said = expand(A.Hasher, name)
            self.assertTrue(said.startswith(expand(B.Hasher, source[element])), (element, said))
            marker = re.search(r";:X#([0-9a-f]+)", name)
            self.assertTrue(marker, name)
            self.assertEqual(A.Hasher.getID(int(marker.group(1), 16)).Data, B.Uid)

        def said(doc):
            return {
                o: {e: expand(doc.Hasher, n) for e, n in byElement(doc.getObject(o).Shape).items()}
                for o in ("Comp", "Fuse")
            }

        before = said(A)
        self.assertTrue(before["Fuse"])
        for names in before.values():
            for text in names.values():
                self.assertNotIn("<missing>", text)
        FreeCAD.closeDocument(A.Name)
        FreeCAD.closeDocument(B.Name)
        # The source gone: what A's file holds is all there is.
        os.rename(
            os.path.join(self.dir, "crossb.FCStd"), os.path.join(self.dir, "crossb.gone")
        )
        A = self.track(FreeCAD.openDocument(os.path.join(self.dir, "crossa.FCStd")))
        self.assertEqual(
            [d for d in FreeCAD.listDocuments().values() if d.FileName.endswith("crossb.FCStd")],
            [],
        )
        self.assertEqual(said(A), before)

    def testAFeatureRefusedAFrozenInputRunsOnACopy(self):
        # Sec 27.82: an algorithm that writes into a frozen input throws
        # LockedShape; the feature runs once more on copies of its inputs,
        # and the value it read is left as it was. What the algorithm
        # changed stays in the result.
        import Part

        if not FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part").GetBool(
            "ImmutableShapeValues", True
        ):
            self.skipTest("shape values are not frozen")

        class Tolerance:
            def __init__(self, obj):
                obj.addProperty("App::PropertyLink", "Base")
                obj.Proxy = self

            def execute(self, obj):
                shape = obj.Base.Shape
                edge = shape.Edges[0]
                edge.fixTolerance(0.01)
                obj.Shape = shape

        self.param.SetInt("TransactionLog", 0)
        doc = self.track(FreeCAD.newDocument("RefusedInput"))
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        feature = doc.addObject("Part::FeaturePython", "Tolerance")
        Tolerance(feature)
        feature.Base = box
        doc.recompute()
        self.assertNotIn("Invalid", feature.State)
        self.assertAlmostEqual(max(e.Tolerance for e in feature.Shape.Edges), 0.01)
        self.assertLess(max(e.Tolerance for e in box.Shape.Edges), 0.01)
        # What the fix did not touch is the value's own again, shared as a
        # result made on the value would share it; the faces around the
        # edge it grew are not.
        own = [any(f.isPartner(g) for g in box.Shape.Faces) for f in feature.Shape.Faces]
        self.assertTrue(any(own), own)
        self.assertFalse(all(own), own)

    def testAnExpressionIntoAnotherDocumentStoresItsName(self):
        # Sec 27.82: an expression's element path into another document --
        # which the parser used to turn into a sub-object label -- names the
        # element, saves its shadow with the name stored in its own table
        # (sec 27.80), and after the target re-mints the string, finds it
        # by content when the target's file loads with the owner.
        import io
        import re
        import zipfile

        import Part

        self.param.SetInt("TransactionLog", 0)
        # Evaluated on the host: the sandbox guest's image is built apart and
        # has neither this parser nor the other document (sec 27.82).
        routing = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Expression/Sandbox")
        if "Evaluate" in routing.GetBools():
            self.addCleanup(routing.SetBool, "Evaluate", routing.GetBool("Evaluate"))
        else:
            self.addCleanup(routing.RemBool, "Evaluate")
        routing.SetBool("Evaluate", False)
        V = FreeCAD.Vector
        B = self.track(FreeCAD.newDocument("exprb"))
        B.UndoMode = 0
        pathB = os.path.join(self.dir, "exprb.FCStd")
        B.saveAs(pathB)
        body = B.addObject("PartDesign::Body", "Body")
        corners = [(0, 0), (20, 0), (20, 20), (0, 20)]
        for name in ("Sketch", "Sketch2"):
            sketch = body.newObject("Sketcher::SketchObject", name)
            for a, b in zip(corners, corners[1:] + corners[:1]):
                sketch.addGeometry(Part.LineSegment(V(*a, 0), V(*b, 0)))
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = B.Sketch
        pad.Length = 10
        B.recompute()
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (pad, ["Edge1"])
        fillet.Radius = 1
        B.recompute()
        B.save()

        def expand(hasher, text):
            def one(m):
                sid = hasher.getID(int(m.group(1), 16))
                return expand(hasher, sid.Data) if sid else "<missing>"

            return re.sub(r"#([0-9a-f]+)", one, text)

        def nameOf(doc, element):
            return {e: n for n, e in doc.Fillet.Shape.ElementMap.items()}[element]

        element = [
            e
            for n, e in sorted(fillet.Shape.ElementMap.items())
            if e.startswith("Face") and "#" in n and "FLT" in n
        ][0]
        area = fillet.Shape.getElement(element).Area
        said = expand(B.Hasher, nameOf(B, element))

        A = self.track(FreeCAD.newDocument("expra"))
        A.UndoMode = 0
        pathA = os.path.join(self.dir, "expra.FCStd")
        A.saveAs(pathA)
        security = FreeCAD.ExpressionSecurity

        def allowForeign(doc):
            # The document's principal follows its content: grant what the
            # refused binding asked for, then recompute.
            doc.recompute()
            for asked in security.pending():
                if asked["permission"] == "doc.foreign":
                    security.grant(asked["principal"], "doc.foreign", asked["target"], True, "session")
                    security.clearPending(asked["principal"], "doc.foreign", asked["target"])
                    self.addCleanup(security.revoke, asked["principal"], "doc.foreign", "*")
            doc.recompute()

        for i in range(3):
            A.addObject("Part::Sphere", "Sphere%d" % i)
        ref = A.addObject("App::FeaturePython", "Ref")
        ref.addProperty("App::PropertyFloat", "T")
        text = "exprb#Fillet.<<%s>>._shape.Area" % element
        ref.setExpression("T", text)
        self.assertEqual(dict(ref.ExpressionEngine)["T"], text)
        allowForeign(A)
        self.assertAlmostEqual(ref.T, area, places=6)
        A.save()

        def saved(doc):
            data = bytes(doc.Ref.dumpPropertyContent("ExpressionEngine", Compression=0))
            archive = zipfile.ZipFile(io.BytesIO(data))
            xml = "".join(archive.read(n).decode() for n in archive.namelist())
            found = re.search(r"<Ids [^>]*>", xml)
            self.assertTrue(found, xml)
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', found.group(0)))
            attrs["ids"] = {int(x, 16) for x in attrs.get("sids", "").split()}
            return attrs

        def stored(doc):
            attrs = saved(doc)
            named = {int(x, 16) for x in re.findall(r"#([0-9a-f]+)", attrs["stored"])}
            self.assertTrue(named)
            self.assertEqual(attrs["ids"], named)
            self.assertEqual({i for i in named if doc.Hasher.getID(i)}, named)
            return expand(doc.Hasher, attrs["stored"][1:].split(".")[0])

        self.assertTrue(saved(A)["shadow"].endswith(";%s.%s" % (nameOf(B, element), element)))
        self.assertEqual(stored(A), said)
        FreeCAD.closeDocument(A.Name)

        # B re-mints the string (sec 27.80's test): gone at a save, made again.
        old = nameOf(B, element)
        pad.Profile = B.Sketch2
        B.recompute()
        B.save()
        FreeCAD.closeDocument(B.Name)
        B = self.track(FreeCAD.openDocument(pathB))
        B.save()
        B.Pad.Profile = B.Sketch
        B.recompute()
        B.save()
        new = nameOf(B, element)
        self.assertNotEqual(new, old)
        self.assertEqual(expand(B.Hasher, new), said)
        FreeCAD.closeDocument(B.Name)

        # A opens B with it; the path is looked up when B's file has loaded.
        A = self.track(FreeCAD.openDocument(pathA))
        self.assertIn("exprb", [d.Name for d in FreeCAD.listDocuments().values()])
        B = FreeCAD.getDocument("exprb")
        self.assertTrue(saved(A)["shadow"].endswith(";%s.%s" % (new, element)))
        self.assertEqual(stored(A), said)
        allowForeign(A)
        self.assertAlmostEqual(A.Ref.T, area, places=6)

    def testAShapeThatCrossedAsTextAsksForARecompute(self):
        # Sec 27.76 item 4: a name carrying the external marker as written
        # before it named its document kept another table's ids as text; the
        # file's read asks for its owner's recompute, which makes the names
        # again. A plain feature here, whose name only the check sees.
        import Part

        self.param.SetInt("TransactionLog", 0)
        doc = self.track(FreeCAD.newDocument("OldCrossing"))
        shape = Part.makeBox(1, 1, 1)
        shape.Tag = 7
        shape.setElementName("Edge1", "Edge1;:Hbd7,E;:X;:Hbd8:3,E", overwrite=True)
        old = doc.addObject("Part::Feature", "Old")
        old.Shape = shape
        doc.recompute()
        doc.saveAs(os.path.join(self.dir, "oldcrossing.FCStd"))
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(os.path.join(self.dir, "oldcrossing.FCStd")))
        self.assertIn("Touched", doc.Old.State)

    def testABinderCrossingDocumentsTakesTheIdsOfItsTable(self):
        # Sec 27.76 item 4, 27.78: a SubShapeBinder bound to another
        # document's shape, as a link is: its names in its own table, the
        # marker naming the source document, and after a reopen with the
        # source's file gone the same names, from this file alone.
        import re

        self.param.SetInt("TransactionLog", 0)
        B = self.track(FreeCAD.newDocument("BindB"))
        box = B.addObject("Part::Box", "Box")
        fillet = B.addObject("Part::Fillet", "Fillet")
        fillet.Base = box
        fillet.Edges = [(1, 1, 1)]
        B.recompute()
        B.saveAs(os.path.join(self.dir, "bindb.FCStd"))
        A = self.track(FreeCAD.newDocument("BindA"))
        for i in range(3):
            A.addObject("Part::Sphere", "Sphere%d" % i)
        A.recompute()
        binder = A.addObject("Part::SubShapeBinder", "Binder")
        binder.Support = [(fillet, "")]
        A.recompute()
        A.saveAs(os.path.join(self.dir, "binda.FCStd"))

        def expand(hasher, text):
            def one(m):
                sid = hasher.getID(int(m.group(1), 16))
                return expand(hasher, sid.Data) if sid else "<missing>"

            return re.sub(r"#([0-9a-f]+)", one, text)

        shape = binder.Shape
        self.assertTrue(shape.Hasher.isSame(A.Hasher))
        self.assertTrue(shape.ElementMap)
        for name in shape.ElementMap:
            marker = re.search(r";:X#([0-9a-f]+)", name)
            self.assertTrue(marker, name)
            self.assertEqual(A.Hasher.getID(int(marker.group(1), 16)).Data, B.Uid)
        before = {e: expand(A.Hasher, n) for n, e in shape.ElementMap.items()}
        for text in before.values():
            self.assertNotIn("<missing>", text)
        FreeCAD.closeDocument(A.Name)
        FreeCAD.closeDocument(B.Name)
        os.rename(os.path.join(self.dir, "bindb.FCStd"), os.path.join(self.dir, "bindb.gone"))
        A = self.track(FreeCAD.openDocument(os.path.join(self.dir, "binda.FCStd")))
        after = {e: expand(A.Hasher, n) for n, e in A.Binder.Shape.ElementMap.items()}
        self.assertEqual(after, before)

    def testAReferenceIntoAnotherDocumentStoresItsNameInItsOwnTable(self):
        # Sec 27.76 item 5: a reference into another document's element
        # stores the name imported into its own document's table, and ids of
        # that table beside it. Opened after the target re-minted the string
        # (dropped by a save while nothing held it, made again), the
        # reference looks the stored name up by content and names the
        # element in the target's new ids; opened while the target has not
        # got the string, the lookup misses and the search by geometry the
        # restore does for a changed target finds the face.
        import io
        import re
        import zipfile

        import Part

        self.param.SetInt("TransactionLog", 0)
        V = FreeCAD.Vector
        B = self.track(FreeCAD.newDocument("StoredB"))
        B.UndoMode = 0
        pathB = os.path.join(self.dir, "storedb.FCStd")
        B.saveAs(pathB)
        body = B.addObject("PartDesign::Body", "Body")
        corners = [(0, 0), (20, 0), (20, 20), (0, 20)]
        for name in ("Sketch", "Sketch2"):
            sketch = body.newObject("Sketcher::SketchObject", name)
            for a, b in zip(corners, corners[1:] + corners[:1]):
                sketch.addGeometry(Part.LineSegment(V(*a, 0), V(*b, 0)))
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = B.Sketch
        pad.Length = 10
        B.recompute()
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (pad, ["Edge1"])
        fillet.Radius = 1
        B.recompute()
        B.save()

        def expand(hasher, text):
            def one(m):
                sid = hasher.getID(int(m.group(1), 16))
                return expand(hasher, sid.Data) if sid else "<missing>"

            return re.sub(r"#([0-9a-f]+)", one, text)

        def nameOf(doc, element):
            return {e: n for n, e in doc.Fillet.Shape.ElementMap.items()}[element]

        element = [
            e
            for n, e in sorted(fillet.Shape.ElementMap.items())
            if e.startswith("Face") and "#" in n and "FLT" in n
        ][0]
        said = expand(B.Hasher, nameOf(B, element))

        A = self.track(FreeCAD.newDocument("StoredA"))
        A.UndoMode = 0
        # So that A's ids are not B's.
        for i in range(3):
            A.addObject("Part::Sphere", "Sphere%d" % i)
        A.recompute()
        ref = A.addObject("App::FeaturePython", "Ref")
        ref.addProperty("App::PropertyXLinkSub", "S")
        ref.S = (fillet, [element])
        A.recompute()
        pathA = os.path.join(self.dir, "storeda.FCStd")
        A.saveAs(pathA)

        def saved(doc):
            data = bytes(doc.Ref.dumpPropertyContent("S", Compression=0))
            archive = zipfile.ZipFile(io.BytesIO(data))
            xml = "".join(archive.read(n).decode() for n in archive.namelist())
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', xml))
            attrs["ids"] = {int(x, 16) for x in attrs.get("sids", "").split()}
            return attrs

        def stored(doc):
            # The stored name, in the owner's table, reads as the target's.
            attrs = saved(doc)
            self.assertEqual(attrs["stored"].split(".")[-1], attrs["shadow"].split(".")[-1])
            named = {int(x, 16) for x in re.findall(r"#([0-9a-f]+)", attrs["stored"])}
            self.assertTrue(named)
            self.assertEqual(attrs["ids"], named)
            self.assertEqual({i for i in named if doc.Hasher.getID(i)}, named)
            return expand(doc.Hasher, attrs["stored"][1:].split(".")[0])

        self.assertEqual(saved(A)["shadow"], ";%s.%s" % (nameOf(B, element), element))
        self.assertEqual(stored(A), said)
        FreeCAD.closeDocument(A.Name)

        def reopenB(B):
            # Nothing but B's own shapes holds its strings after a reopen:
            # a save then drops the rest.
            FreeCAD.closeDocument(B.Name)
            B = self.track(FreeCAD.openDocument(pathB))
            B.save()
            return B

        # B re-mints the string: gone at a save, made again after.
        old = nameOf(B, element)
        pad.Profile = B.Sketch2
        B.recompute()
        B.save()
        B = reopenB(B)
        B.Pad.Profile = B.Sketch
        B.recompute()
        B.save()
        new = nameOf(B, element)
        self.assertNotEqual(new, old)
        self.assertEqual(expand(B.Hasher, new), said)
        A = self.track(FreeCAD.openDocument(pathA))
        self.assertEqual(A.Ref.S[1], [element])
        self.assertEqual(saved(A)["shadow"], ";%s.%s" % (new, element))
        self.assertEqual(stored(A), said)
        FreeCAD.closeDocument(A.Name)

        # B without the string: the face comes from the other sketch now,
        # with the same geometry, which the restore's search finds.
        B.Pad.Profile = B.Sketch2
        B.recompute()
        B.save()
        B = reopenB(B)
        self.assertNotIn(said, {expand(B.Hasher, n) for n in B.Fillet.Shape.ElementMap})
        A = self.track(FreeCAD.openDocument(pathA))
        found = A.Ref.S[1][0]
        self.assertFalse(found.startswith("?"), found)
        self.assertEqual(saved(A)["shadow"], ";%s.%s" % (nameOf(B, found), found))
        self.assertEqual(stored(A), expand(B.Hasher, nameOf(B, found)))

    def kindsOf(self, preview):
        return {c["key"]: c["kind"] + " " + c["op"] for c in preview["changes"]}

    def testMergeRecomputesWhatBothSidesChanged(self):
        # Sec 28.2 item 4: derived values are not merged. Each side changed
        # another input of one box; the merge takes theirs' input and
        # recomputes inside its own step.
        doc = self.track(FreeCAD.newDocument("MergeBoth"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        self.assertAlmostEqual(box.Shape.Volume, 1000.0)
        doc.createTransactionBranch("side")
        doc.openTransaction("longer")
        box.Length = 20
        doc.recompute()
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        box = doc.getObject("Box")
        doc.openTransaction("lower")
        box.Height = 5
        doc.recompute()
        doc.commitTransaction()
        self.assertAlmostEqual(box.Shape.Volume, 500.0)

        preview = doc.previewTransactionMerge("side")
        kinds = self.kindsOf(preview)
        self.assertFalse(preview["fast_forward"])
        self.assertEqual(preview["conflicts"], 0)
        self.assertEqual(kinds["Box.Length"], "take set")
        self.assertEqual(kinds["Box.Shape"], "derived set")
        self.assertNotIn("Box.Height", kinds)

        undos = doc.UndoCount
        result = doc.mergeTransactionBranch("side")
        self.assertGreater(result["seq"], 0)
        self.assertEqual(result["failed"], [])
        self.assertAlmostEqual(box.Length.Value, 20.0)
        self.assertAlmostEqual(box.Height.Value, 5.0)
        self.assertAlmostEqual(box.Shape.Volume, 20 * 10 * 5)
        self.assertNotIn("Touched", box.State)
        self.assertEqual(doc.UndoCount, undos + 1)
        row = [t for t in doc.getTransactionLog() if t["seq"] == result["seq"]][0]
        self.assertEqual(row["kind"], "merge")
        side = self.branches(doc)["side"]
        self.assertEqual(row["merge_from"], side["head"])
        doc.undo()
        self.assertAlmostEqual(box.Length.Value, 10.0)
        self.assertAlmostEqual(box.Shape.Volume, 500.0)
        doc.redo()
        self.assertAlmostEqual(box.Shape.Volume, 1000.0)

    def testMergeTakesTheShapeWhenOursIsUnchanged(self):
        # Sec 28.6 Q1: ours has changed nothing since the base, so theirs is
        # taken whole -- its shape from the log, nothing recomputed.
        doc = self.track(FreeCAD.newDocument("MergeWhole"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        doc.createTransactionBranch("side")
        doc.openTransaction("longer")
        box.Length = 20
        cyl = doc.addObject("Part::Cylinder", "Cyl")
        doc.recompute()
        doc.commitTransaction()
        cylVolume = cyl.Shape.Volume
        doc.switchTransactionBranch("main")
        box = doc.getObject("Box")
        self.assertAlmostEqual(box.Shape.Volume, 1000.0)

        preview = doc.previewTransactionMerge("side")
        self.assertTrue(preview["fast_forward"])
        self.assertEqual(self.kindsOf(preview)["Box.Shape"], "take set")
        recomputed = []

        class Seen:
            def slotRecomputedObject(self, obj):
                recomputed.append(obj.Name)

        seen = Seen()
        FreeCAD.addDocumentObserver(seen)
        try:
            result = doc.mergeTransactionBranch("side")
        finally:
            FreeCAD.removeDocumentObserver(seen)
        self.assertGreater(result["seq"], 0)
        self.assertEqual(recomputed, [])
        self.assertAlmostEqual(box.Shape.Volume, 2000.0)
        self.assertAlmostEqual(doc.getObject("Cyl").Shape.Volume, cylVolume)
        self.assertNotIn("Touched", box.State)
        doc.undo()
        self.assertAlmostEqual(box.Shape.Volume, 1000.0)
        self.assertIsNone(doc.getObject("Cyl"))

    def testMergeABranchMadeFromTheFileAsFound(self):
        # Sec 28.2 item 2: a file opened without a history starts one at the
        # file as found, version 1, which no row precedes. A branch made
        # from it and `main` share that state, and nothing else.
        self.param.SetInt("TransactionLog", 1)
        doc = self.track(FreeCAD.newDocument("MergeFound"))
        obj = doc.addObject("App::FeatureTest", "Obj")
        obj.Integer = 1
        path = os.path.join(self.dir, "found.FCStd")
        doc.saveAs(path)
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        doc.UndoMode = 1
        first = doc.getTransactionVersions()[0]
        self.assertEqual(first["seq"], 0)
        doc.openTransaction("main edit")
        doc.Obj.String = "main"
        doc.commitTransaction()
        doc.createTransactionBranch("side", first["num"])
        self.assertEqual(doc.Obj.String, "4711")
        doc.openTransaction("side edit")
        doc.Obj.Integer = 2
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        preview = doc.previewTransactionMerge("side")
        self.assertEqual(preview["base"], 0)
        self.assertEqual(self.kindsOf(preview), {"Obj.Integer": "take set"})
        self.assertGreater(doc.mergeTransactionBranch("side")["seq"], 0)
        self.assertEqual(doc.Obj.Integer, 2)
        self.assertEqual(doc.Obj.String, "main")

    def testMergeABranchOpenInAnotherDocument(self):
        # Sec 28.2 item 8: theirs may be open in another document of the
        # file. What it has done is in rows before they are read, and a
        # transaction left open there refuses the merge.
        doc, path = self.saved()
        first = doc.getTransactionVersions()[0]["num"]
        other = self.track(doc.openTransactionVersion(first, False))
        other.UndoMode = 1
        other.openTransaction("on main")
        other.getObject("Obj").String = "main"
        other.commitTransaction()
        names = {b["id"]: b["name"] for b in doc.getTransactionBranches()}
        self.assertEqual(names[other.getTransactionCursor()["branch"]], "main")

        other.openTransaction("still open")
        other.getObject("Obj").Float = 2.5
        with self.assertRaises(RuntimeError):
            doc.mergeTransactionBranch("main")
        other.commitTransaction()

        result = doc.mergeTransactionBranch("main")
        self.assertGreater(result["seq"], 0)
        self.assertEqual(doc.Obj.String, "main")
        self.assertAlmostEqual(doc.Obj.Float, 2.5)
        self.assertEqual(doc.Obj.Integer, 2)
        # Theirs is left as it is, and can take ours in turn.
        self.assertEqual(other.getObject("Obj").Integer, 1)
        self.assertGreater(other.mergeTransactionBranch("side")["seq"], 0)
        self.assertEqual(other.getObject("Obj").Integer, 2)
        self.assertEqual(other.getObject("Obj").String, "main")

    def testMergeSuffixesALabelOursHas(self):
        # Sec 28.2 item 5, 27.41 Q4 (a): labels are document scope, so two
        # branches can give one label to two objects; the incoming one is
        # suffixed.
        doc = self.track(FreeCAD.newDocument("MergeLabels"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        doc.addObject("App::FeatureTest", "Obj")
        doc.commitTransaction()
        doc.createTransactionBranch("side")
        doc.openTransaction("side")
        theirs = doc.addObject("App::FeatureTest", "Theirs")
        theirs.Label = "Bracket"
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        doc.openTransaction("main")
        ours = doc.addObject("App::FeatureTest", "Ours")
        ours.Label = "Bracket"
        doc.commitTransaction()
        result = doc.mergeTransactionBranch("side")
        self.assertGreater(result["seq"], 0)
        self.assertEqual(ours.Label, "Bracket")
        label = doc.getObject("Theirs").Label
        self.assertNotEqual(label, "Bracket")
        self.assertTrue(label.startswith("Bracket"))
        row = [t for t in doc.getTransactionLog() if t["seq"] == result["seq"]][0]
        self.assertIn('"relabelled":["Theirs"]', row["script"])

    def testMergeUpToAVersion(self):
        # Sec 28.2 item 9: theirs merged up to one of its versions, then the
        # rest of it from there.
        doc = self.track(FreeCAD.newDocument("MergeVersion"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        obj = doc.addObject("App::FeatureTest", "Obj")
        obj.Integer = 1
        doc.commitTransaction()
        doc.createTransactionBranch("side")
        doc.openTransaction("first")
        obj.Integer = 2
        doc.commitTransaction()
        middle = doc.snapshotTransactionLog()
        doc.openTransaction("second")
        obj.String = "later"
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        with self.assertRaises(ValueError):
            doc.previewTransactionMerge("side", 1000)
        preview = doc.previewTransactionMerge("side", middle)
        self.assertEqual(self.kindsOf(preview), {"Obj.Integer": "take set"})
        first = doc.mergeTransactionBranch("side", {}, "", middle)
        self.assertGreater(first["seq"], 0)
        self.assertEqual(doc.Obj.Integer, 2)
        self.assertEqual(doc.Obj.String, "4711")
        named = [v for v in doc.getTransactionVersions() if v["num"] == middle][0]
        self.assertEqual(named["kind"], "named")
        self.assertEqual(named["name"], "merged into main")
        rest = doc.previewTransactionMerge("side")
        self.assertEqual(self.kindsOf(rest), {"Obj.String": "take set"})
        self.assertEqual(rest["base"], first["preview"]["theirs"])
        self.assertGreater(doc.mergeTransactionBranch("side")["seq"], 0)
        self.assertEqual(doc.Obj.String, "later")

    def testALoadOnFirstReadIsNotAnEdit(self):
        # Sec 30.8: a shape is read from the file when it is first asked for.
        # That is the restore's value arriving late, not a change: no row, no
        # undo step, and a transaction open at the time does not take the
        # value not yet read for what was there before.
        doc = self.track(FreeCAD.newDocument("LazyLoad"))
        doc.addObject("Part::Box", "Box")
        doc.addObject("Part::Box", "Other")
        doc.recompute()
        path = os.path.join(self.dir, "lazy.FCStd")
        doc.saveAs(path)
        FreeCAD.closeDocument(doc.Name)

        doc = self.track(FreeCAD.openDocument(path))
        doc.UndoMode = 1
        rows = len(doc.getTransactionLog())
        self.assertAlmostEqual(doc.Box.Shape.Volume, 1000.0)
        self.assertEqual(doc.UndoNames, [])
        self.assertEqual(len(doc.getTransactionLog()), rows)

        doc.openTransaction("longer")
        self.assertAlmostEqual(doc.Other.Shape.Volume, 1000.0)  # read inside it
        doc.Other.Length = 20
        doc.recompute()
        doc.commitTransaction()
        self.assertAlmostEqual(doc.Other.Shape.Volume, 2000.0)
        self.assertEqual(doc.UndoNames, ["longer"])
        doc.undo()
        self.assertAlmostEqual(doc.Other.Length.Value, 10.0)
        self.assertFalse(doc.Other.Shape.isNull())
        self.assertAlmostEqual(doc.Other.Shape.Volume, 1000.0)
        doc.redo()
        self.assertAlmostEqual(doc.Other.Shape.Volume, 2000.0)

    def testABranchInASecondDocumentIsMergedWhenAsked(self):
        # Sec 30.3 S.a: a branch opened in a second document of the file.
        # Neither follows the other; a merge that is asked for takes the
        # other's rows as they are when this side has not moved, shapes
        # included, with no recompute of its own.
        doc = self.track(FreeCAD.newDocument("Branches"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        path = os.path.join(self.dir, "branches.FCStd")
        doc.saveAs(path)
        self.assertEqual(doc.getTransactionBranchState()["target"], "")

        other = self.track(doc.openTransactionBranch("desk", False))
        self.assertNotEqual(other.Name, doc.Name)
        state = other.getTransactionBranchState()
        self.assertEqual(
            (state["branch"], state["target"], state["ahead"], state["behind"]),
            ("desk", "main", 0, 0),
        )
        self.assertTrue(other.FileName.startswith(path + "@desk@v"), other.FileName)
        branches = self.branches(doc)
        self.assertEqual(branches["desk"]["target"], branches["main"]["id"])
        with self.assertRaises(ValueError):
            doc.openTransactionBranch("desk")

        recomputed = []

        class Seen:
            def slotRecomputedObject(self, obj):
                recomputed.append(obj.Document.Name)

        seen = Seen()
        FreeCAD.addDocumentObserver(seen)
        try:
            other.openTransaction("longer")
            other.Box.Length = 20
            other.recompute()
            other.commitTransaction()
            # Nothing follows: main is where it was.
            self.assertAlmostEqual(doc.Box.Shape.Volume, 1000.0)
            state = other.getTransactionBranchState()
            self.assertGreater(state["ahead"], 0)
            self.assertEqual(state["behind"], 0)

            del recomputed[:]
            preview = doc.previewTransactionMerge("desk")
            self.assertGreater(preview["forward"], 0)
            self.assertEqual(preview["conflicts"], 0)
            merged = doc.mergeTransactionBranch("desk")
            self.assertEqual(merged["forwarded"], preview["forward"])
            self.assertAlmostEqual(doc.Box.Shape.Volume, 2000.0)
            self.assertNotIn(doc.Name, recomputed)
            self.assertNotIn("Touched", doc.Box.State)
            row = [t for t in doc.getTransactionLog() if t["name"] == "longer"][-1]
            self.assertEqual((row["kind"], row["branch"]), ("user", "main"))
            self.assertFalse([t for t in doc.getTransactionLog() if t["kind"] == "merge"])

            # The other way: main's operation, taken when the branch asks.
            doc.openTransaction("lower")
            doc.Box.Height = 5
            doc.recompute()
            doc.commitTransaction()
            self.assertAlmostEqual(other.Box.Shape.Volume, 2000.0)
            self.assertGreater(other.getTransactionBranchState()["behind"], 0)
            del recomputed[:]
            pulled = other.mergeTransactionBranch("main")
            self.assertGreater(pulled["forwarded"], 0)
            self.assertAlmostEqual(other.Box.Shape.Volume, 1000.0)
            self.assertNotIn(other.Name, recomputed)
        finally:
            FreeCAD.removeDocumentObserver(seen)
        state = other.getTransactionBranchState()
        self.assertEqual((state["ahead"], state["behind"]), (0, 0))

        # The steps of the document that took the rows are the rows.
        self.assertEqual(doc.UndoNames[0], "lower")
        self.assertIn("longer", doc.UndoNames)
        doc.undo()
        self.assertAlmostEqual(doc.Box.Height.Value, 10.0)
        self.assertAlmostEqual(other.Box.Height.Value, 5.0)

        # A branch is not its document: closed, it stays.
        FreeCAD.closeDocument(other.Name)
        self.assertIn("desk", self.branches(doc))
        doc.openTransaction("after")
        doc.Box.Width = 3
        doc.commitTransaction()
        self.assertAlmostEqual(doc.Box.Width.Value, 3.0)

    def testTheAuthorOfARowTravelsWithTheFile(self):
        # Sec 30.3 S.b, 30.6: a row is written under whoever acted when its
        # transaction opened; a login is a row with no ops; the users and
        # their sessions are in the history a save embeds, and one who comes
        # again after the file is reopened is the user they were.
        doc = self.track(FreeCAD.newDocument("Authors"))
        doc.UndoMode = 1
        doc.openTransaction("mine")
        obj = doc.addObject("App::FeatureTest", "Obj")
        obj.Integer = 1
        doc.commitTransaction()
        with self.assertRaises(ValueError):
            doc.pushTransactionActor("local", "me")
        self.assertFalse(doc.popTransactionActor())

        login = doc.transactionLogin("verified", "alice@example.com", login=7)
        self.assertGreater(login, 0)
        doc.pushTransactionActor("verified", "alice@example.com", login=7)
        try:
            doc.openTransaction("hers")
            obj.Integer = 2
            doc.commitTransaction()
        finally:
            self.assertTrue(doc.popTransactionActor())
        self.assertGreater(doc.transactionLogin("declared", "carol", access="view", login=8), 0)
        doc.openTransaction("mine again")
        obj.Integer = 3
        doc.commitTransaction()

        def authors(d):
            return [
                (t["name"], t["author"], t["author_kind"])
                for t in d.getTransactionLog()
                if t["kind"] in ("user", "login")
            ]

        expected = [
            ("mine", "host", "local"),
            ("Login alice@example.com", "alice@example.com", "verified"),
            ("hers", "alice@example.com", "verified"),
            ("Login carol", "carol", "declared"),
            ("mine again", "host", "local"),
        ]
        self.assertEqual(authors(doc), expected)
        # A login is not a step, and the desktop's steps are its own (sec
        # 30.10).
        self.assertEqual(doc.UndoNames, ["mine again", "mine"])
        self.assertTrue(doc.transactionLogout("declared", "carol", login=8))
        self.assertFalse(doc.transactionLogout("declared", "carol", login=8))
        sessions = {s["name"]: s for s in doc.getTransactionSessions()}
        self.assertEqual(sessions["carol"]["access"], "view")
        self.assertGreater(sessions["carol"]["closed"], 0)
        self.assertEqual(sessions["alice@example.com"]["closed"], 0)
        alice = sessions["alice@example.com"]["user"]

        path = os.path.join(self.dir, "authors.FCStd")
        doc.saveAs(path)
        FreeCAD.closeDocument(doc.Name)
        doc = self.track(FreeCAD.openDocument(path))
        doc.UndoMode = 1
        self.assertEqual(authors(doc)[: len(expected)], expected)
        # Every session of the run that wrote the file is closed in it.
        for s in doc.getTransactionSessions():
            if s["name"] in ("alice@example.com", "carol"):
                self.assertGreater(s["closed"], 0, s)

        doc.pushTransactionActor("verified", "alice@example.com", login=9)
        try:
            doc.openTransaction("hers, another day")
            doc.Obj.Integer = 4
            doc.commitTransaction()
        finally:
            doc.popTransactionActor()
        last = [t for t in doc.getTransactionLog() if t["kind"] == "user"][-1]
        self.assertEqual((last["name"], last["author"]), ("hers, another day", "alice@example.com"))
        hers = [s for s in doc.getTransactionSessions() if s["name"] == "alice@example.com"]
        self.assertEqual(len(hers), 2)
        self.assertEqual({s["user"] for s in hers}, {alice})

    def testEachAuthorUndoesItsOwnSteps(self):
        # Sec 30.3 S.c, 30.10: an author's undo takes its own newest step
        # though another has written since. It then goes through the log,
        # in one row that holds the recompute of what it changed, and the
        # redo that follows it with nothing written between restores the
        # shapes from that row.
        import math

        doc = self.track(FreeCAD.newDocument("Undoers"))
        doc.UndoMode = 1
        doc.openTransaction("create")
        box = doc.addObject("Part::Box", "Box")
        cyl = doc.addObject("Part::Cylinder", "Cyl")
        doc.recompute()
        doc.commitTransaction()

        def act(name, kind, login, fn):
            doc.pushTransactionActor(kind, name, login=login)
            try:
                return fn()
            finally:
                doc.popTransactionActor()

        def edit(what, change):
            doc.openTransaction(what)
            change()
            doc.recompute()
            doc.commitTransaction()

        act("alice", "verified", 1, lambda: edit("longer", lambda: setattr(box, "Length", 20)))
        act("bob", "invited", 2, lambda: edit("wider", lambda: setattr(cyl, "Radius", 5)))
        self.assertAlmostEqual(box.Shape.Volume, 2000.0)
        self.assertEqual(doc.UndoNames, ["create"])
        self.assertEqual(act("alice", "verified", 1, lambda: doc.UndoNames), ["longer"])
        self.assertEqual(act("bob", "invited", 2, lambda: doc.UndoNames), ["wider"])

        rows = len(doc.getTransactionLog())
        act("alice", "verified", 1, doc.undo)
        self.assertAlmostEqual(box.Length.Value, 10.0)
        self.assertAlmostEqual(box.Shape.Volume, 1000.0)
        self.assertNotIn("Touched", box.State)
        self.assertAlmostEqual(cyl.Radius.Value, 5.0)
        self.assertAlmostEqual(cyl.Shape.Volume, math.pi * 25 * 10, 4)
        new = doc.getTransactionLog()[rows:]
        self.assertEqual(
            [(t["kind"], t["author"]) for t in new],
            [("undo", "alice"), ("recompute", "alice")],
        )
        ops = doc.getTransactionOps(new[0]["seq"])
        self.assertIn("Shape", [o["prop"] for o in ops if o["derived"]])
        self.assertEqual(act("alice", "verified", 1, lambda: doc.RedoNames), ["longer"])
        self.assertEqual(act("alice", "verified", 1, lambda: doc.UndoNames), [])
        self.assertEqual(act("bob", "invited", 2, lambda: doc.RedoNames), [])

        # Nothing written since her undo: the redo puts its row back as it
        # was, shapes and all.
        act("alice", "verified", 1, doc.redo)
        self.assertAlmostEqual(box.Shape.Volume, 2000.0)
        self.assertNotIn("Touched", box.State)

        # His, with her undo and redo written since.
        act("bob", "invited", 2, doc.undo)
        self.assertAlmostEqual(cyl.Radius.Value, 2.0)
        self.assertAlmostEqual(cyl.Shape.Volume, math.pi * 4 * 10, 4)
        self.assertAlmostEqual(box.Shape.Volume, 2000.0)
        self.assertEqual(act("bob", "invited", 2, lambda: doc.RedoNames), ["wider"])
        self.assertEqual(doc.UndoNames, ["create"])
