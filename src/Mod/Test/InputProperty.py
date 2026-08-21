# ***************************************************************************
# *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
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
# ***************************************************************************

"""Tests for the Input property status.

An input property is one the owning object's execute() never writes. The claim
is enforced: a write during execute() that actually changes the value is a hard
recompute error. See docs/InputProperties.md.
"""

import unittest

import FreeCAD


class Writer:
    """FeaturePython proxy whose execute() writes one property."""

    def __init__(self, prop=None, value=None):
        self.prop = prop
        self.value = value

    def execute(self, obj):
        if self.prop:
            setattr(obj, self.prop, self.value)


class InputPropertyCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("InputPropertyTest")

    def tearDown(self):
        FreeCAD.closeDocument("InputPropertyTest")

    def makeObject(self, name, prop=None, value=None, markInput=True):
        obj = self.Doc.addObject("App::FeaturePython", name)
        obj.addProperty("App::PropertyLength", "Length", "Params")
        obj.Length = 10
        obj.addProperty("App::PropertyLength", "Plain", "Params")
        obj.Plain = 10
        obj.addProperty("App::PropertyIntegerList", "Numbers", "Params")
        obj.Numbers = [1, 2, 3]
        if markInput:
            obj.setPropertyStatus("Length", ["Input"])
            obj.setPropertyStatus("Numbers", ["Input"])
        obj.Proxy = Writer(prop, value)
        obj.touch()
        return obj

    def testStatusRoundTrip(self):
        obj = self.makeObject("Probe")
        self.assertIn("Input", obj.getPropertyStatus("Length"))
        self.assertNotIn("Input", obj.getPropertyStatus("Plain"))

        obj.setPropertyStatus("Length", ["-Input"])
        self.assertNotIn("Input", obj.getPropertyStatus("Length"))

    def testChangeDuringExecuteIsAnError(self):
        obj = self.makeObject("ChangeInput", "Length", 20)
        self.Doc.recompute()
        self.assertIn("Invalid", obj.State)

    def testUnchangedWriteDuringExecuteIsAllowed(self):
        # The check is value-based: writing the same value back does not break
        # the claim that execute() will not change the value.
        obj = self.makeObject("SameInput", "Length", 10)
        self.Doc.recompute()
        self.assertNotIn("Invalid", obj.State)

    def testListPropertyChangeIsAnError(self):
        # List properties write through AtomicPropertyChange, whose destructor
        # swallows exceptions. This is the case the violation latch exists for:
        # without it the throw would degrade to a console message.
        obj = self.makeObject("ChangeList", "Numbers", [4, 5, 6])
        self.Doc.recompute()
        self.assertIn("Invalid", obj.State)

    def testNonInputPropertyIsUnaffected(self):
        obj = self.makeObject("ChangePlain", "Plain", 20)
        self.Doc.recompute()
        self.assertNotIn("Invalid", obj.State)
        self.assertAlmostEqual(obj.Plain.Value, 20)

    def testWriteOutsideExecuteIsAllowed(self):
        obj = self.makeObject("OutsideWrite")
        self.Doc.recompute()
        obj.Length = 42
        self.assertAlmostEqual(obj.Length.Value, 42)
        self.Doc.recompute()
        self.assertNotIn("Invalid", obj.State)

    def testViolationDoesNotLeak(self):
        # The latch is per-thread and drained by the recompute that sees it, so
        # an error on one object must not be attributed to the next.
        bad = self.makeObject("Bad", "Length", 20)
        good = self.makeObject("Good")
        self.Doc.recompute()
        self.assertIn("Invalid", bad.State)
        self.assertNotIn("Invalid", good.State)

    def testClearingStatusReallowsTheWrite(self):
        obj = self.makeObject("Cleared", "Length", 20)
        self.Doc.recompute()
        self.assertIn("Invalid", obj.State)

        obj.setPropertyStatus("Length", ["-Input"])
        obj.touch()
        self.Doc.recompute()
        self.assertNotIn("Invalid", obj.State)
        self.assertAlmostEqual(obj.Length.Value, 20)

    def testStatusSurvivesSaveAndRestore(self):
        # Input takes a status bit that was previously unused, so it round-trips
        # through the document without disturbing any other status.
        obj = self.makeObject("Persist")
        self.Doc.recompute()
        obj.setPropertyStatus("Plain", ["ReadOnly"])

        tempFile = FreeCAD.getTempPath() + "InputPropertyTest.FCStd"
        self.Doc.saveAs(tempFile)
        FreeCAD.closeDocument("InputPropertyTest")
        self.Doc = FreeCAD.openDocument(tempFile)

        restored = self.Doc.getObject("Persist")
        self.assertIn("Input", restored.getPropertyStatus("Length"))
        self.assertNotIn("Input", restored.getPropertyStatus("Plain"))
        self.assertIn("ReadOnly", restored.getPropertyStatus("Plain"))
