# ***************************************************************************
# *   Copyright (c) 2004 Juergen Riegel <juergen.riegel@web.de>             *
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

import math
import os
import sys
import tempfile
import unittest
import FreeCAD
from FreeCAD import Base


class ProgressTestCase(unittest.TestCase):
    """FreeCAD.getProgress(): the snapshot of running progress sequences."""

    OUTER = "getProgress test outer"
    INNER = "getProgress test inner"

    def find(self, text):
        return [s for s in FreeCAD.getProgress()["sequences"] if s["text"] == text]

    def testKeys(self):
        snap = FreeCAD.getProgress()
        for key in ("sequences", "progress", "total", "roots", "lead"):
            self.assertIn(key, snap)
        self.assertIsInstance(snap["sequences"], list)
        self.assertEqual(self.find(self.OUTER), [])

    def testNestedSequences(self):
        outer = Base.ProgressIndicator()
        inner = Base.ProgressIndicator()
        outer.start(self.OUTER, 10)
        try:
            for _ in range(3):
                outer.next()
            inner.start(self.INNER, 4)
            try:
                inner.next()
                found_outer = self.find(self.OUTER)
                found_inner = self.find(self.INNER)
                self.assertEqual(len(found_outer), 1)
                self.assertEqual(len(found_inner), 1)
                o, i = found_outer[0], found_inner[0]
                self.assertEqual((o["progress"], o["total"]), (3, 10))
                self.assertEqual((i["progress"], i["total"]), (1, 4))
                self.assertEqual(i["depth"], o["depth"] + 1)
                self.assertTrue(o["mainThread"])
                # a nested sequence may be cut off by maxLevels
                self.assertEqual(
                    [s for s in FreeCAD.getProgress(o["depth"] + 1)["sequences"]
                     if s["text"] == self.INNER],
                    [],
                )
            finally:
                inner.stop()
        finally:
            outer.stop()
        self.assertEqual(self.find(self.OUTER), [])
        self.assertEqual(self.find(self.INNER), [])


class ConsoleTestCase(unittest.TestCase):
    def setUp(self):
        self.count = 0

    def testPrint(self):
        FreeCAD.Console.PrintMessage("   Printing message\n")
        FreeCAD.Console.PrintError("   Printing error\n")
        FreeCAD.Console.PrintWarning("   Printing warning\n")
        FreeCAD.Console.PrintLog("   Printing Log\n")

    def testSynchronPrintFromThread(self):
        # http://python-kurs.eu/threads.php
        try:
            import _thread as thread, time
        except Exception:
            import thread, time

        def adder():
            lock.acquire()
            self.count = self.count + 1
            # call of Console method is thread-safe
            FreeCAD.Console.PrintMessage("Call from Python thread: count=" + str(self.count) + "\n")
            lock.release()

        lock = thread.allocate_lock()
        for i in range(10):
            thread.start_new(adder, ())

        time.sleep(3)
        self.assertEqual(self.count, 10, "Synchronization of threads failed")
        FreeCAD.Console.PrintMessage(str(self.count) + "\n")

    def testAsynchronPrintFromThread(self):
        # http://python-kurs.eu/threads.php
        try:
            import _thread as thread, time
        except Exception:
            import thread, time

        def adder():
            self.count = self.count + 1
            # call of Console method is thread-safe
            FreeCAD.Console.PrintMessage(
                "Call from Python thread (not synchronized): count=" + str(self.count) + "\n"
            )

        lock = thread.allocate_lock()
        for i in range(10):
            thread.start_new(adder, ())

        time.sleep(3)
        FreeCAD.Console.PrintMessage(str(self.count) + "\n")

    #    def testStatus(self):
    #        SLog = FreeCAD.GetStatus("Console","Log")
    #        SErr = FreeCAD.GetStatus("Console","Err")
    #        SWrn = FreeCAD.GetStatus("Console","Wrn")
    #        SMsg = FreeCAD.GetStatus("Console","Msg")
    #        FreeCAD.SetStatus("Console","Log",1)
    #        FreeCAD.SetStatus("Console","Err",1)
    #        FreeCAD.SetStatus("Console","Wrn",1)
    #        FreeCAD.SetStatus("Console","Msg",1)
    #        self.assertEqual(FreeCAD.GetStatus("Console","Msg"),1,"Set and read status failed (Console,Msg)")
    #        self.assertEqual(FreeCAD.GetStatus("Console","Err"),1,"Set and read status failed (Console,Err)")
    #        self.assertEqual(FreeCAD.GetStatus("Console","Wrn"),1,"Set and read status failed (Console,Wrn)")
    #        self.assertEqual(FreeCAD.GetStatus("Console","Log"),1,"Set and read status failed (Console,Log)")
    #        FreeCAD.SetStatus("Console","Log",0)
    #        FreeCAD.SetStatus("Console","Err",0)
    #        FreeCAD.SetStatus("Console","Wrn",0)
    #        FreeCAD.SetStatus("Console","Msg",0)
    #        self.assertEqual(FreeCAD.GetStatus("Console","Msg"),0,"Set and read status failed (Console,Msg)")
    #        self.assertEqual(FreeCAD.GetStatus("Console","Err"),0,"Set and read status failed (Console,Err)")
    #        self.assertEqual(FreeCAD.GetStatus("Console","Wrn"),0,"Set and read status failed (Console,Wrn)")
    #        self.assertEqual(FreeCAD.GetStatus("Console","Log"),0,"Set and read status failed (Console,Log)")
    #        FreeCAD.SetStatus("Console","Log",SLog)
    #        FreeCAD.SetStatus("Console","Err",SErr)
    #        FreeCAD.SetStatus("Console","Wrn",SWrn)
    #        FreeCAD.SetStatus("Console","Msg",SMsg)

    def tearDown(self):
        pass

    def testILoggerBlocker(self):
        if FreeCAD.GuiUp:
            import QtUnitGui

            QtUnitGui.testILoggerBlocker()


class ParameterTestCase(unittest.TestCase):
    def setUp(self):
        self.TestPar = FreeCAD.ParamGet("System parameter:Test")

    def testGroup(self):
        # FreeCAD.Console.PrintLog("Base::ParameterTestCase::testGroup\n")
        # check on Group creation
        Temp = self.TestPar.GetGroup("44")
        self.assertTrue(self.TestPar.HasGroup("44"), "Test on created group failed")
        # check on Deletion
        self.TestPar.RemGroup("44")
        self.assertTrue(self.TestPar.HasGroup("44"), "A referenced group must not be deleted")
        Temp = 0

    def testGroupNames(self):
        with self.assertRaises(ValueError):
            # no empty groups allowed
            self.TestPar.GetGroup("")
        grp1 = self.TestPar.GetGroup("////Sub1/////Sub2/////")
        grp2 = self.TestPar.GetGroup("Sub1/Sub2")
        self.assertEqual(grp1.GetGroupName(), "Sub2")
        self.assertEqual(grp2.GetGroupName(), "Sub2")

    # check on special conditions
    def testInt(self):
        # FreeCAD.Console.PrintLog("Base::ParameterTestCase::testInt\n")
        # Temp = FreeCAD.ParamGet("System parameter:Test/44")
        # check on Int
        self.TestPar.SetInt("44", 4711)
        self.assertEqual(self.TestPar.GetInt("44"), 4711, "In and out error at Int")
        # check on Deletion
        self.TestPar.RemInt("44")
        self.assertEqual(self.TestPar.GetInt("44", 1), 1, "Deletion error at Int")

    def testBool(self):
        # FreeCAD.Console.PrintLog("Base::ParameterTestCase::testBool\n")
        # check on Int
        self.TestPar.SetBool("44", 1)
        self.assertEqual(self.TestPar.GetBool("44"), 1, "In and out error at Bool")
        # check on Deletion
        self.TestPar.RemBool("44")
        self.assertEqual(self.TestPar.GetBool("44", 0), 0, "Deletion error at Bool")

    def testFloat(self):
        # FreeCAD.Console.PrintLog("Base::ParameterTestCase::testFloat\n")
        # Temp = FreeCAD.ParamGet("System parameter:Test/44")
        # check on Int
        self.TestPar.SetFloat("44", 4711.4711)
        self.assertEqual(self.TestPar.GetFloat("44"), 4711.4711, "In and out error at Float")
        # check on Deletion
        self.TestPar.RemFloat("44")
        self.assertEqual(self.TestPar.GetFloat("44", 1.1), 1.1, "Deletion error at Float")

    def testString(self):
        # FreeCAD.Console.PrintLog("Base::ParameterTestCase::testFloat\n")
        # Temp = FreeCAD.ParamGet("System parameter:Test/44")
        # check on Int
        self.TestPar.SetString("44", "abcdefgh")
        self.assertEqual(self.TestPar.GetString("44"), "abcdefgh", "In and out error at String")
        # check on Deletion
        self.TestPar.RemString("44")
        self.assertEqual(self.TestPar.GetString("44", "hallo"), "hallo", "Deletion error at String")

    def testNesting(self):
        # Parameter testing
        # FreeCAD.Console.PrintLog("Base::ParameterTestCase::testNesting\n")
        for i in range(50):
            self.TestPar.SetFloat(str(i), 4711.4711)
            self.TestPar.SetInt(str(i), 4711)
            self.TestPar.SetBool(str(i), 1)
            Temp = self.TestPar.GetGroup(str(i))
            for l in range(50):
                Temp.SetFloat(str(l), 4711.4711)
                Temp.SetInt(str(l), 4711)
                Temp.SetBool(str(l), 1)
        Temp = 0

    def testExportImport(self):
        # Parameter testing
        # FreeCAD.Console.PrintLog("Base::ParameterTestCase::testNesting\n")
        self.TestPar.SetFloat("ExTest", 4711.4711)
        self.TestPar.SetInt("ExTest", 4711)
        self.TestPar.SetString("ExTest", "4711")
        self.TestPar.SetBool("ExTest", 1)
        Temp = self.TestPar.GetGroup("ExTest")
        Temp.SetFloat("ExTest", 4711.4711)
        Temp.SetInt("ExTest", 4711)
        Temp.SetString("ExTest", "4711")
        Temp.SetBool("ExTest", 1)
        TempPath = tempfile.gettempdir() + os.sep + "ExportTest.FCExport"

        self.TestPar.Export(TempPath)
        Temp = self.TestPar.GetGroup("ImportTest")
        Temp.Import(TempPath)
        self.assertEqual(Temp.GetFloat("ExTest"), 4711.4711, "ExportImport error")
        Temp = 0

    def tearDown(self):
        # remove all
        TestPar = FreeCAD.ParamGet("System parameter:Test")
        TestPar.Clear()


class ParamRegistryTestCase(unittest.TestCase):
    """The settings registry from Python: FreeCAD.registerParam(), FreeCAD.listParams()
    and the definition files freecad.params loads."""

    PATH = "User parameter:BaseApp/Preferences/Mod/Test"

    def find(self, entry):
        rows = [r for r in FreeCAD.listParams(entry) if r["entry"] == entry]
        self.assertEqual(len(rows), 1, entry)
        return rows[0]

    def testGeneratedClassesAreListed(self):
        rows = FreeCAD.listParams("Preferences/Document checkextension")
        self.assertEqual([r["entry"] for r in rows], ["CheckExtension"])
        row = rows[0]
        self.assertEqual(row["path"], "User parameter:BaseApp/Preferences/Document")
        self.assertEqual(row["displayPath"], "Preferences/Document/CheckExtension")
        self.assertEqual(row["namespace"], "App")
        self.assertEqual(row["context"], "DocumentParams")
        self.assertEqual(row["type"], "Bool")
        self.assertTrue(row["doc"])
        self.assertGreater(len(FreeCAD.listParams()), len(rows))
        self.assertEqual(FreeCAD.listParams("no-such-keyword-anywhere"), [])

    def testRegister(self):
        self.assertTrue(
            FreeCAD.registerParam(
                self.PATH,
                "PyDoorFlag",
                "Bool",
                True,
                title="A flag",
                doc="Described by the registry's Python test.",
                namespace="Test",
                context="PyDoor",
            )
        )
        row = self.find("PyDoorFlag")
        self.assertEqual(row["path"], self.PATH)
        self.assertEqual(row["displayPath"], "Preferences/Mod/Test/PyDoorFlag")
        self.assertEqual(row["name"], "PyDoorFlag")
        self.assertEqual(row["type"], "Bool")
        self.assertEqual(row["default"], "true")
        self.assertEqual(row["title"], "A flag")
        self.assertEqual(row["namespace"], "Test")
        self.assertEqual(row["context"], "PyDoor")
        self.assertEqual(row["proxy"], "")
        self.assertEqual(row["items"], [])
        # the first description stands, and nothing was stored by describing
        self.assertFalse(FreeCAD.registerParam(self.PATH, "PyDoorFlag", "Bool", False, doc="x"))
        self.assertEqual(self.find("PyDoorFlag")["default"], "true")
        self.assertNotIn("PyDoorFlag", FreeCAD.ParamGet(self.PATH).GetBools())
        # nor over a setting of a generated class
        self.assertFalse(
            FreeCAD.registerParam(
                "User parameter:BaseApp/Preferences/Document", "CheckExtension", "Bool", False
            )
        )

    def testDefaultsByType(self):
        doc = "Described by the registry's Python test."
        for entry, kind, value, text in (
            ("PyDoorInt", "Int", -3, "-3"),
            ("PyDoorUInt", "UInt", 7, "7"),
            ("PyDoorHex", "Hex", 0xCC333300, "0xCC333300"),
            ("PyDoorFloat", "Float", 0.25, "0.25"),
            ("PyDoorFloatFromInt", "Float", 2, "2"),
            ("PyDoorString", "String", "a b", "a b"),
        ):
            self.assertTrue(FreeCAD.registerParam(self.PATH, entry, kind, value, doc=doc), entry)
            row = self.find(entry)
            self.assertEqual(row["type"], kind)
            self.assertEqual(row["default"], text)

    def testEditors(self):
        doc = "Described by the registry's Python test."
        self.assertTrue(
            FreeCAD.registerParam(
                self.PATH,
                "PyDoorChoice",
                "Int",
                1,
                doc=doc,
                proxy="ComboBox",
                items=["Ask", ("Always", "Every time"), ("Never",)],
                translateItems=False,
            )
        )
        row = self.find("PyDoorChoice")
        self.assertEqual(row["proxy"], "ComboBox")
        self.assertEqual(
            row["items"], [("Ask", "", None), ("Always", "Every time", None), ("Never", "", None)]
        )
        self.assertFalse(row["translateItems"])
        self.assertTrue(
            FreeCAD.registerParam(
                self.PATH,
                "PyDoorChoiceByText",
                "String",
                "b",
                doc=doc,
                proxy="ComboBox",
                items=[("First", None, "a"), ("Second", None, "b")],
            )
        )
        self.assertEqual(
            self.find("PyDoorChoiceByText")["items"], [("First", "", "a"), ("Second", "", "b")]
        )
        self.assertTrue(
            FreeCAD.registerParam(
                self.PATH,
                "PyDoorSpin",
                "Float",
                0.5,
                doc=doc,
                proxy="SpinBox",
                minimum=0.0,
                maximum=2.0,
                step=0.25,
                decimals=2,
            )
        )
        row = self.find("PyDoorSpin")
        self.assertEqual(
            (row["minimum"], row["maximum"], row["step"], row["decimals"]), (0.0, 2.0, 0.25, 2)
        )
        self.assertTrue(
            FreeCAD.registerParam(
                self.PATH, "PyDoorColor", "Hex", 255, doc=doc, proxy="Color", transparency=True
            )
        )
        self.assertTrue(self.find("PyDoorColor")["transparency"])

    def testRefusals(self):
        with self.assertRaises(ValueError):
            FreeCAD.registerParam(self.PATH, "PyDoorBad", "Colour", 0)
        with self.assertRaises(TypeError):
            FreeCAD.registerParam(self.PATH, "PyDoorBad", "Bool", 1)
        with self.assertRaises(TypeError):
            FreeCAD.registerParam(self.PATH, "PyDoorBad", "Int", True)
        with self.assertRaises(TypeError):
            FreeCAD.registerParam(self.PATH, "PyDoorBad", "Int", "3")
        with self.assertRaises(TypeError):
            FreeCAD.registerParam(self.PATH, "PyDoorBad", "String", 3)
        with self.assertRaises(ValueError):
            FreeCAD.registerParam(self.PATH, "PyDoorBad", "Hex", -1)
        # a path the registry could not read a value through
        with self.assertRaises(ValueError):
            FreeCAD.registerParam("Preferences/Mod/Test", "PyDoorBad", "Bool", True)
        with self.assertRaises(ValueError):
            FreeCAD.registerParam("No such set:Preferences", "PyDoorBad", "Bool", True)
        with self.assertRaises(ValueError):
            FreeCAD.registerParam(self.PATH, "", "Bool", True)
        with self.assertRaises(ValueError):
            FreeCAD.registerParam(
                self.PATH, "PyDoorBad", "String", "a", items=[("A", None, "a"), "B"]
            )
        self.assertEqual([r for r in FreeCAD.listParams("PyDoorBad")], [])

    def testDefinitionFile(self):
        import types
        from freecad import params

        definition = types.SimpleNamespace(
            NameSpace="Test",
            ClassName="PyDoorParams",
            ParamPath=self.PATH,
            Params=[
                params.ParamBool("PyFileFlag", False, title="A flag", doc="A flag of the test."),
                params.ParamInt(
                    "PyFileChoice",
                    2,
                    doc="A choice of the test.",
                    proxy=params.ParamComboBox(["Ask", ("Always", "Every time"), "Never"]),
                ),
                params.ParamHex(
                    "PyFileColor",
                    0xCC333300,
                    doc="A colour of the test.",
                    proxy=params.ParamColor(transparency=False),
                ),
                params.ParamFloat(
                    "PyFileSize",
                    1.5,
                    doc="A size of the test.",
                    proxy=params.ParamSpinBox(0.5, 10, 0.5, 1),
                ),
                params.ParamString(
                    "PyFileText", "a \"b\"", doc="A text of the test.", subpath="Sub"
                ),
                params.ParamUInt("PyFileCount", 3, doc="A count of the test.", param_name="Count2"),
            ],
        )
        self.assertEqual(params.register(definition), [])
        row = self.find("PyFileFlag")
        self.assertEqual((row["type"], row["default"]), ("Bool", "false"))
        self.assertEqual((row["namespace"], row["context"]), ("Test", "PyDoorParams"))
        self.assertEqual(row["title"], "A flag")
        row = self.find("PyFileChoice")
        self.assertEqual((row["type"], row["default"], row["proxy"]), ("Int", "2", "ComboBox"))
        self.assertEqual([i[0] for i in row["items"]], ["Ask", "Always", "Never"])
        self.assertEqual(row["items"][1][1], "Every time")
        # a one-line documentation is the title too, as in a generated class
        self.assertEqual(row["title"], "A choice of the test.")
        row = self.find("PyFileColor")
        self.assertEqual((row["type"], row["default"], row["proxy"]), ("Hex", "0xCC333300", "Color"))
        self.assertFalse(row["transparency"])
        row = self.find("PyFileSize")
        self.assertEqual((row["default"], row["proxy"]), ("1.5", "SpinBox"))
        self.assertEqual((row["minimum"], row["maximum"], row["decimals"]), (0.5, 10.0, 1))
        row = self.find("PyFileText")
        self.assertEqual(row["path"], self.PATH + "/Sub")
        self.assertEqual(row["default"], 'a "b"')
        row = self.find("Count2")
        self.assertEqual((row["name"], row["type"], row["default"]), ("PyFileCount", "UInt", "3"))

        # the same file again is no conflict; the same entry from another file is
        self.assertEqual(params.register(definition), [])
        other = types.SimpleNamespace(
            NameSpace="Test",
            ClassName="PyDoorOtherParams",
            ParamPath=self.PATH,
            Params=[params.ParamBool("PyFileFlag", True, doc="The same flag again.")],
        )
        noted = len(params.conflicts)
        self.assertEqual(params.register(other), [(self.PATH, "PyFileFlag")])
        self.assertEqual(
            params.conflicts[noted:], [("Test::PyDoorOtherParams", self.PATH, "PyFileFlag")]
        )
        del params.conflicts[noted:]
        self.assertEqual(self.find("PyFileFlag")["default"], "false")

        # loading the generator's classes left no stand-in for its build tool behind
        self.assertNotIn("cog", sys.modules)

    def testModulesDefinitions(self):
        """What the modules written in Python registered at start: nothing of it is
        described twice, and every setting has a short documentation, as the C++
        tests ask of the generated classes."""
        from freecad import params

        self.assertEqual(params.conflicts, [])
        for path, entry in params._owners:
            rows = [r for r in FreeCAD.listParams(entry) if (r["path"], r["entry"]) == (path, entry)]
            self.assertEqual(len(rows), 1, entry)
            doc = rows[0]["doc"]
            self.assertTrue(doc, "%s/%s has no documentation" % (path, entry))
            self.assertLessEqual(len(doc), 400, "%s/%s" % (path, entry))
            self.assertTrue(rows[0]["title"], "%s/%s has no title" % (path, entry))


class AlgebraTestCase(unittest.TestCase):
    def setUp(self):
        pass

    def testAngle(self):
        v1 = FreeCAD.Vector(0, 0, 0.000001)
        v2 = FreeCAD.Vector(0, 0.000001, 0)
        self.assertAlmostEqual(v1.getAngle(v2), math.pi / 2)
        self.assertAlmostEqual(v2.getAngle(v1), math.pi / 2)

    def testVector2d(self):
        v = FreeCAD.Base.Vector2d(1.0, 1.0)
        v.rotate(math.pi / 2)
        self.assertAlmostEqual(v.x, -1.0)
        self.assertAlmostEqual(v.y, 1.0)

    def testAngleWithNullVector(self):
        v1 = FreeCAD.Vector(0, 0, 0)
        v2 = FreeCAD.Vector(0, 1, 0)
        self.assertTrue(math.isnan(v1.getAngle(v2)))
        self.assertTrue(math.isnan(v2.getAngle(v1)))

    def testMatrix(self):
        m = FreeCAD.Matrix(4, 2, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        u = m.multiply(m.inverse())
        self.assertEqual(u, FreeCAD.Matrix(), "Invalid inverse of matrix")

    def testRotAndMoveMatrix(self):
        m1 = FreeCAD.Matrix()
        m1.move(10, 5, -3)
        m1.rotateY(0.2)
        m2 = FreeCAD.Matrix()
        m2.rotateY(0.2)
        m2.move(10, 5, -3)
        m3 = FreeCAD.Matrix()
        m3.move(10, 5, -3)
        m4 = FreeCAD.Matrix()
        m4.rotateY(0.2)
        self.assertNotEqual(m1, m3 * m4, "Wrong multiplication order")
        self.assertEqual(m1, m4 * m3, "Wrong multiplication order")
        self.assertEqual(m2, m3 * m4, "Wrong multiplication order")
        self.assertNotEqual(m2, m4 * m3, "Wrong multiplication order")

    def testRotationFromMatrix(self):
        rot = FreeCAD.Rotation(45, 30, 0)
        m_r = rot.toMatrix()
        m_r.move(5, 6, 7)
        m_s = FreeCAD.Matrix()
        m_s.scale(1, 1, 3)
        m_s.move(7, 3, 2)
        target_rot = FreeCAD.Rotation(45, 60, 0)
        err = "Non uniform scale has wrong affect non orthogonal rotation"
        self.assertTrue(FreeCAD.Rotation(m_s * m_r).isSame(target_rot, 1e-12), err)
        err = "Right multiplication with non uniform scale must not affect rotation"
        self.assertTrue(FreeCAD.Rotation(m_r * m_s).isSame(rot, 1e-12), err)
        m_r.scale(-2)
        err = "Uniform scale must not affect rotation"
        self.assertTrue(FreeCAD.Rotation(m_r).isSame(rot, 1e-12), err)

    def testRotation(self):
        r = FreeCAD.Rotation(1, 0, 0, 0)  # 180 deg around (1,0,0)
        self.assertEqual(r.Axis, FreeCAD.Vector(1, 0, 0))
        self.assertAlmostEqual(math.fabs(r.Angle), math.fabs(math.pi))

        r = r.multiply(r)  # identity
        self.assertEqual(r.Axis, FreeCAD.Vector(0, 0, 1))
        self.assertAlmostEqual(r.Angle, 0)

        r = FreeCAD.Rotation(1, 0, 0, 0)
        r.Q = (0, 0, 0, 1)  # update axis and angle
        s = FreeCAD.Rotation(0, 0, 0, 1)
        self.assertEqual(r.Axis, s.Axis)
        self.assertAlmostEqual(r.Angle, s.Angle)
        self.assertTrue(r.isSame(s))

        r = FreeCAD.Rotation(1, 0, 0, 0)
        r.Matrix = FreeCAD.Matrix()  # update axis and angle
        s = FreeCAD.Rotation(0, 0, 0, 1)
        self.assertEqual(r.Axis, s.Axis)
        self.assertAlmostEqual(r.Angle, s.Angle)
        self.assertTrue(r.isSame(s))

        r = FreeCAD.Rotation(1, 0, 0, 0)
        r.Axes = (FreeCAD.Vector(0, 0, 1), FreeCAD.Vector(0, 0, 1))  # update axis and angle
        s = FreeCAD.Rotation(0, 0, 0, 1)
        self.assertEqual(r.Axis, s.Axis)
        self.assertAlmostEqual(r.Angle, s.Angle)
        self.assertTrue(r.isSame(s))

        # add 360 deg to angle
        r = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), 270)
        s = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), 270 + 360)
        self.assertEqual(r.Axis, s.Axis)
        # self.assertAlmostEqual(r.Angle, s.Angle + 2*math.pi)
        self.assertTrue(r.isSame(s))

        # subtract 360 deg from angle using Euler angles
        r = FreeCAD.Rotation(0, 0, 180)
        r.invert()
        s = FreeCAD.Rotation(0, 0, -180)
        self.assertTrue(r.isSame(s))

        # subtract 360 deg from angle using quaternion
        r = FreeCAD.Rotation(1, 0, 0, 0)
        s = FreeCAD.Rotation(-1, 0, 0, 0)
        # angles have the same sign
        if r.Angle * s.Angle > 0:
            self.assertEqual(r.Axis, s.Axis * (-1))
        else:
            self.assertAlmostEqual(r.Angle, -s.Angle)
        self.assertTrue(r.isSame(s))
        r.invert()
        self.assertTrue(r.isSame(s))

        # gimbal lock (north pole)
        r = FreeCAD.Rotation()
        r.setYawPitchRoll(20, 90, 10)
        a = r.getYawPitchRoll()
        s = FreeCAD.Rotation()
        s.setYawPitchRoll(*a)
        self.assertAlmostEqual(a[0], 0.0)
        self.assertAlmostEqual(a[1], 90.0)
        self.assertAlmostEqual(a[2], -10.0)
        self.assertTrue(r.isSame(s, 1e-12))

        # gimbal lock (south pole)
        r = FreeCAD.Rotation()
        r.setYawPitchRoll(20, -90, 10)
        a = r.getYawPitchRoll()
        s = FreeCAD.Rotation()
        s.setYawPitchRoll(*a)
        self.assertAlmostEqual(a[0], 0.0)
        self.assertAlmostEqual(a[1], -90.0)
        self.assertAlmostEqual(a[2], 30.0)
        self.assertTrue(r.isSame(s, 1e-12))

    def testInverted(self):
        p = FreeCAD.Placement()
        p.Rotation.Angle = math.pi / 2
        self.assertEqual(abs(p.inverse().Rotation.Angle), p.Rotation.Angle)

    def testDecomposeScale(self):
        mat = FreeCAD.Matrix()
        mat.scale(FreeCAD.Vector(2, 3, 4))
        res = mat.decompose()
        self.assertTrue(res[0].isUnity())
        self.assertEqual(res[1], mat)
        self.assertTrue(res[2].isUnity())
        self.assertTrue(res[3].isUnity())

    def testDecomposeRotation(self):
        mat = FreeCAD.Matrix()
        mat.rotateX(1)
        mat.rotateY(1)
        mat.rotateZ(1)
        res = mat.decompose()
        self.assertTrue(res[0].isUnity())
        self.assertTrue(res[1].isUnity(1e-12))
        self.assertEqual(res[2], mat)
        self.assertTrue(res[3].isUnity())

    def testDecomposeMove(self):
        mat = FreeCAD.Matrix()
        mat.move(FreeCAD.Vector(1, 2, 3))
        res = mat.decompose()
        self.assertTrue(res[0].isUnity())
        self.assertTrue(res[1].isUnity())
        self.assertTrue(res[2].isUnity())
        self.assertEqual(res[3], mat)

    def testMatrixDecompose(self):
        mat = FreeCAD.Matrix()
        mat.A21 = 1.0
        mat.A14 = 1.0
        mat.A24 = 2.0
        mat.A34 = 3.0
        res = mat.decompose()

        mul = res[3] * res[2] * res[1] * res[0]
        self.assertEqual(mul, mat)

        # shearing
        self.assertAlmostEqual(res[0].determinant(), 1.0)

        # scaling
        self.assertEqual(res[1].hasScale(), FreeCAD.ScaleType.NonUniformLeft)

        # rotation
        rot1 = FreeCAD.Rotation(res[2])
        rot2 = FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 45)
        self.assertTrue(rot1.isSame(rot2, 1e-12))

        # translation
        plm = FreeCAD.Placement(res[3])
        self.assertEqual(plm.Base, FreeCAD.Vector(1, 2, 3))

    def testYawPitchRoll(self):
        def getYPR1(yaw, pitch, roll):
            r = FreeCAD.Rotation()
            r.setYawPitchRoll(yaw, pitch, roll)
            return r

        def getYPR2(yaw, pitch, roll):
            rx = FreeCAD.Rotation()
            ry = FreeCAD.Rotation()
            rz = FreeCAD.Rotation()

            rx.Axis = FreeCAD.Vector(1, 0, 0)
            ry.Axis = FreeCAD.Vector(0, 1, 0)
            rz.Axis = FreeCAD.Vector(0, 0, 1)

            rx.Angle = math.radians(roll)
            ry.Angle = math.radians(pitch)
            rz.Angle = math.radians(yaw)

            return rz.multiply(ry).multiply(rx)

        angles = []
        angles.append((10, 10, 10))
        angles.append((13, 45, -24))
        angles.append((10, -90, 20))

        for i in angles:
            r = getYPR1(*i)
            s = getYPR2(*i)
            self.assertTrue(r.isSame(s, 1e-12))

    def testBounding(self):
        b = FreeCAD.BoundBox()
        b.setVoid()
        self.assertFalse(b.isValid(), "Bbox is not invalid")
        b.add(0, 0, 0)
        self.assertTrue(b.isValid(), "Bbox is invalid")
        self.assertEqual(b.XLength, 0, "X length > 0")
        self.assertEqual(b.YLength, 0, "Y length > 0")
        self.assertEqual(b.ZLength, 0, "Z length > 0")
        self.assertEqual(b.Center, FreeCAD.Vector(0, 0, 0), "Center is not at (0,0,0)")
        self.assertTrue(b.isInside(b.Center), "Center is not inside Bbox")
        b.add(2, 2, 2)
        self.assertTrue(
            b.isInside(b.getIntersectionPoint(b.Center, FreeCAD.Vector(0, 1, 0))),
            "Intersection point is not inside Bbox",
        )
        self.assertTrue(b.intersect(b), "Bbox doesn't intersect with itself")
        self.assertFalse(
            b.intersected(FreeCAD.BoundBox(4, 4, 4, 6, 6, 6)).isValid(),
            "Bbox should not intersect with Bbox outside",
        )
        self.assertEqual(
            b.intersected(FreeCAD.BoundBox(-2, -2, -2, 2, 2, 2)).Center,
            b.Center,
            "Bbox is not a full subset",
        )

    def testMultLeftOrRight(self):
        doc = FreeCAD.newDocument()
        obj = doc.addObject("App::FeatureTestPlacement")

        p1 = Base.Placement()
        p1.Base = Base.Vector(10, 10, 10)

        p2 = Base.Placement()
        p2.Rotation.Angle = math.radians(90)

        obj.Input1 = p1
        obj.Input2 = p2
        doc.recompute()

        self.assertTrue(obj.MultRight.isSame(p1 * p2))
        self.assertFalse(obj.MultRight.isSame(p2 * p1))
        self.assertTrue(obj.MultLeft.isSame(p2 * p1))
        self.assertFalse(obj.MultLeft.isSame(p1 * p2))

        FreeCAD.closeDocument(doc.Name)


class MatrixTestCase(unittest.TestCase):
    def setUp(self):
        self.mat = FreeCAD.Matrix()

    def testOrder(self):
        self.mat = FreeCAD.Matrix(1.0, 2.0, 3.0, 4.0)
        self.assertEqual(self.mat.A11, 1.0)
        self.assertEqual(self.mat.A12, 2.0)
        self.assertEqual(self.mat.A13, 3.0)
        self.assertEqual(self.mat.A14, 4.0)

    def testScalar(self):
        res = self.mat * 0.0
        for i in range(16):
            self.assertEqual(res.A[i], 0.0)

    def testAddition(self):
        res1 = self.mat * 2.0
        res2 = self.mat + self.mat
        for i in range(16):
            self.assertEqual(res1.A[i], res2.A[i])

    def testMinus(self):
        res = self.mat - self.mat
        for i in range(16):
            self.assertEqual(res.A[i], 0.0)

    def testVector(self):
        vec = FreeCAD.Vector(1, 1, 1)
        vec = self.mat * vec
        self.assertEqual(vec.x, 1.0)
        self.assertEqual(vec.y, 1.0)
        self.assertEqual(vec.z, 1.0)

    def testVectorMult(self):
        vec = FreeCAD.Vector(1, 1, 1)
        with self.assertRaises(TypeError):
            vec * "string"

    def testRotation(self):
        rot = FreeCAD.Rotation()
        res = self.mat * rot
        self.assertEqual(type(res), FreeCAD.Matrix)

    def testPlacement(self):
        plm = FreeCAD.Placement()
        res = self.mat * plm
        self.assertEqual(type(res), FreeCAD.Matrix)

    def testMatrix(self):
        mat = FreeCAD.Matrix()
        res = self.mat * mat
        self.assertEqual(type(res), FreeCAD.Matrix)

    def testMatrixPlacementMatrix(self):
        # Example taken from https://forum.freecad.org/viewtopic.php?f=3&t=61000
        mat = FreeCAD.Matrix(
            -0.470847778020266,
            0.8150598976807029,
            0.3376088628746235,
            -11.25290913640202,
            -0.8822144756796808,
            -0.4350066260577338,
            -0.180185641360483,
            -2876.45492562325,
            1.955470978815492e-9,
            -0.3826834326750831,
            0.923879538425552,
            941.3822018176414,
        )
        plm = FreeCAD.Placement(mat)
        mat = plm.toMatrix()
        self.assertEqual(mat.hasScale(), FreeCAD.ScaleType.NoScaling)

    def testAnything(self):
        with self.assertRaises(NotImplementedError):
            self.mat * "string"

    def testUnity(self):
        mat = FreeCAD.Matrix(2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1)
        self.assertFalse(mat.isUnity())
        mat.unity()
        self.assertTrue(mat.isUnity())

    def testPower(self):
        mat = FreeCAD.Matrix(2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1)
        with self.assertRaises(NotImplementedError):
            mat ** "string"

        mat2 = mat**0
        self.assertTrue(mat2.isUnity())
        self.assertEqual(mat**-1, mat.inverse())
        self.assertEqual(mat**1, mat)
        self.assertEqual(mat**2, mat * mat)
        self.assertEqual(mat**3, mat * mat * mat)
        mat.nullify()
        with self.assertRaises(RuntimeError):
            mat**-1

    def testScale(self):
        self.mat.scale(1.0, 2.0, 3.0)
        self.assertEqual(self.mat.determinant(), 6.0)
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.NonUniformLeft)
        self.mat.unity()
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.NoScaling)
        self.mat.scale(2.0, 2.0, 2.0)
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.Uniform)
        self.mat.rotateX(1.0)
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.Uniform)
        self.mat.scale(1.0, 2.0, 3.0)
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.NonUniformLeft)
        self.mat.unity()
        self.mat.scale(1.0, 2.0, 3.0)
        self.mat.rotateX(1.0)
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.NonUniformRight)
        self.mat.unity()
        self.mat.setCol(0, FreeCAD.Vector(1, 2, 3))
        self.mat.setCol(1, FreeCAD.Vector(1, 2, 3))
        self.mat.setCol(2, FreeCAD.Vector(1, 2, 3))
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.Other)
        self.mat.unity()
        self.mat.setRow(0, FreeCAD.Vector(1, 2, 3))
        self.mat.setRow(1, FreeCAD.Vector(1, 2, 3))
        self.mat.setRow(2, FreeCAD.Vector(1, 2, 3))
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.Other)
        self.mat.unity()
        self.mat.scale(-1.0)
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.Uniform)
        self.mat.scale(-2.0)
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.Uniform)

    def testShearing(self):
        self.mat.setRow(1, FreeCAD.Vector(0, 1, 1))
        self.assertEqual(self.mat.hasScale(), FreeCAD.ScaleType.Other)

    def testMultLeftOrRight(self):
        mat1 = FreeCAD.Matrix()
        mat1.rotateX(1.0)

        mat2 = FreeCAD.Matrix()
        mat2.scale(1.0, 2.0, 3.0)
        self.assertEqual((mat1 * mat2).hasScale(), FreeCAD.ScaleType.NonUniformRight)
        self.assertEqual((mat2 * mat1).hasScale(), FreeCAD.ScaleType.NonUniformLeft)

    def testNull(self):
        self.assertFalse(self.mat.isNull())
        self.mat.nullify()
        self.assertTrue(self.mat.isNull())

    def testUnity(self):
        self.assertTrue(self.mat.isUnity())
        self.mat.nullify()
        self.assertFalse(self.mat.isUnity())
        self.mat.unity()
        self.assertTrue(self.mat.isUnity())

    def testColRow(self):
        with self.assertRaises(TypeError):
            self.mat.col("string")
        with self.assertRaises(TypeError):
            self.mat.row("string")
        self.assertEqual(type(self.mat.col(0)), FreeCAD.Vector)
        self.assertEqual(type(self.mat.row(0)), FreeCAD.Vector)
        self.mat.setCol(0, FreeCAD.Vector(1, 0, 0))
        self.mat.setRow(0, FreeCAD.Vector(1, 0, 0))

    def testDiagonal(self):
        self.mat.scale(2.0, 2.0, 2.0)
        self.assertEqual(self.mat.diagonal(), FreeCAD.Vector(2.0, 2.0, 2.0))

    def testNumberProtocol(self):
        with self.assertRaises(NotImplementedError):
            self.mat / 2.0
        with self.assertRaises(NotImplementedError):
            self.mat % 2.0
        with self.assertRaises(NotImplementedError):
            divmod(self.mat, 2.0)
        with self.assertRaises(NotImplementedError):
            float(self.mat)
        with self.assertRaises(NotImplementedError):
            int(self.mat)
        with self.assertRaises(NotImplementedError):
            self.mat | self.mat
        with self.assertRaises(NotImplementedError):
            self.mat & self.mat
        with self.assertRaises(NotImplementedError):
            self.mat ^ self.mat
        with self.assertRaises(NotImplementedError):
            self.mat << 2
        with self.assertRaises(NotImplementedError):
            self.mat >> 2
        with self.assertRaises(NotImplementedError):
            ~self.mat
        with self.assertRaises(NotImplementedError):
            abs(self.mat)
        self.assertEqual(+self.mat, self.mat)
        self.assertEqual(-self.mat, self.mat * -1)
        self.assertTrue(bool(self.mat))


class FileSystem(unittest.TestCase):
    def testEncoding(self):
        self.assertEqual(sys.getfilesystemencoding(), "utf-8")
