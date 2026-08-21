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


class InputStratumCases(unittest.TestCase):
    """Phase 2: the input recompute stratum.

    An input property needs no ordering edge back from its readers, so a
    container's own children can read its parameters. Section 5 of
    docs/InputProperties.md is the specification.
    """

    def setUp(self):
        self.Doc = FreeCAD.newDocument("InputStratumTest")

    def tearDown(self):
        FreeCAD.closeDocument("InputStratumTest")

    def makeParam(self, name, value=10, markInput=True):
        obj = self.Doc.addObject("App::FeaturePython", name)
        obj.addProperty("App::PropertyFloat", "Length", "Params")
        obj.Length = value
        if markInput:
            obj.setPropertyStatus("Length", ["Input"])
        return obj

    def makeContainer(self, name, child, value=10, markInput=True):
        """An object that links its child, so a binding back is a cycle."""
        cont = self.makeParam(name, value, markInput)
        cont.addProperty("App::PropertyLink", "Child", "Params")
        cont.Child = child
        return cont

    def bindError(self, obj, prop, expr):
        try:
            obj.setExpression(prop, expr)
        except Exception as exc:
            # The message is what the test asserts on, so hand it back whole.
            return str(exc)
        return ""

    def testContainerParameterIsReadableFromItsChild(self):
        # The motivating case. Cont links Child, so Child must recompute first;
        # Child reading Cont.Length would close the loop if Length were not an
        # input property.
        child = self.makeParam("Child", 1, markInput=False)
        cont = self.makeContainer("Cont", child, 7)

        self.assertEqual(self.bindError(child, "Length", "Cont.Length"), "")
        self.Doc.recompute()
        self.assertAlmostEqual(child.Length, 7)

        # ...and the edge really is gone, which is the point.
        self.assertNotIn(child, cont.InList)

    def testNonInputContainerParameterStillCycles(self):
        child = self.makeParam("Child", 1, markInput=False)
        cont = self.makeContainer("Cont", child, 7, markInput=False)

        error = self.bindError(child, "Length", "Cont.Length")
        self.assertIn("cyclic", error)

    def testParameterChangePropagatesToTheChild(self):
        child = self.makeParam("Child", 1, markInput=False)
        cont = self.makeContainer("Cont", child, 7)
        child.setExpression("Length", "Cont.Length")
        self.Doc.recompute()
        self.assertAlmostEqual(child.Length, 7)

        # Nothing links Cont to Child any more, so this only arrives if the
        # stratum pushed Child into the object phase.
        cont.Length = 21
        self.Doc.recompute()
        self.assertAlmostEqual(child.Length, 21)

    def testInputChainSettlesInOneRecompute(self):
        # A.Length -> B.Length -> C.Length, the first two input, the last not.
        # The stratum orders the input pair; the object phase sees B settled.
        a = self.makeParam("A", 2)
        b = self.makeParam("B", 0)
        c = self.makeParam("C", 0, markInput=False)
        b.setExpression("Length", "A.Length * 2")
        c.setExpression("Length", "B.Length + 1")
        self.Doc.recompute()
        self.assertAlmostEqual(b.Length, 4)
        self.assertAlmostEqual(c.Length, 5)

        a.Length = 5
        self.Doc.recompute()
        self.assertAlmostEqual(b.Length, 10)
        self.assertAlmostEqual(c.Length, 11)

    def testClosureRuleRejectsAComputedSource(self):
        # Section 4: an input property may only read input properties, or its
        # value could move during the object phase.
        a = self.makeParam("A", 2, markInput=False)
        b = self.makeParam("B", 0)
        error = self.bindError(b, "Length", "A.Length")
        self.assertIn("only reference input properties", error)

    def testCycleInsideTheStratumIsRejected(self):
        # The object level check cannot see these references, because removing
        # their edges is the whole point. Section 5.1.
        a = self.makeParam("A", 2)
        b = self.makeParam("B", 3)
        self.assertEqual(self.bindError(b, "Length", "A.Length"), "")
        error = self.bindError(a, "Length", "B.Length")
        self.assertIn("cyclic", error)

    def testSelfReferenceIsRejected(self):
        a = self.makeParam("A", 2)
        error = self.bindError(a, "Length", "A.Length + 1")
        self.assertIn("cyclic", error)

    def testStatusChangeRebuildsReferrerDeps(self):
        # Section 7. What a binding depends on is decided by the Input status
        # of what it reads, and that status can change after the binding was
        # set. Neither direction fixes itself: the dependencies were computed
        # once, when the expression was bound.
        a = self.makeParam("A", 5)
        b = self.makeParam("B", 0, markInput=False)
        b.setExpression("Length", "A.Length")
        self.assertNotIn(b, a.InList)

        a.setPropertyStatus("Length", ["-Input"])
        self.assertIn(b, a.InList)

        a.setPropertyStatus("Length", ["Input"])
        self.assertNotIn(b, a.InList)

    def testMarkingInputAfterTheFactRelievesTheCycle(self):
        # The order the fix exists for: bind first, mark the parameter second,
        # then link. Before phase 3 the edge from the binding survived and the
        # link closed a cycle.
        child = self.makeParam("Child", 1, markInput=False)
        cont = self.makeParam("Cont", 7, markInput=False)
        child.setExpression("Length", "Cont.Length")
        self.assertIn(child, cont.InList)

        cont.setPropertyStatus("Length", ["Input"])
        cont.addProperty("App::PropertyLink", "Child", "Params")
        cont.Child = child

        self.Doc.recompute()
        self.assertAlmostEqual(child.Length, 7)
        self.assertNotIn(child, cont.InList)

    def testAStatusChangeMovesNoValue(self):
        # Rebuilding the dependencies is a graph edit, not a write. Nothing is
        # touched by it, so no recompute is provoked.
        a = self.makeParam("A", 5)
        b = self.makeParam("B", 0, markInput=False)
        b.setExpression("Length", "A.Length")
        self.Doc.recompute()
        self.assertNotIn("Touched", b.State)

        a.setPropertyStatus("Length", ["-Input"])
        self.assertNotIn("Touched", b.State)

    def testCrossDocumentReferenceIsUnaffected(self):
        # Section 8: the edge is only ever dropped inside one document, so a
        # cross document reader has nothing to rebuild, and is not a referrer
        # the warning would name.
        other = FreeCAD.newDocument("InputStratumOther")
        try:
            a = self.makeParam("A", 5)
            b = other.addObject("App::FeaturePython", "B")
            b.addProperty("App::PropertyFloat", "Length", "Params")
            b.setExpression("Length", "InputStratumTest#A.Length")
            self.assertEqual(a.getPropertyReferrers("Length"), [])

            before = list(a.InList)
            a.setPropertyStatus("Length", ["-Input"])
            self.assertEqual(list(a.InList), before)
        finally:
            FreeCAD.closeDocument("InputStratumOther")

    def testReferrerListNamesTheBinding(self):
        # The property editor warning of section 7 asks this question, and it
        # is answered by a scan rather than by the recompute time record, so
        # that it can be asked before the status changes.
        a = self.makeParam("A", 5)
        b = self.makeParam("B", 0, markInput=False)
        self.assertEqual(a.getPropertyReferrers("Length"), [])

        b.setExpression("Length", "A.Length")
        self.assertEqual(a.getPropertyReferrers("Length"), [(b, "Length")])

    def testBindingSurvivesSaveAndRestore(self):
        child = self.makeParam("Child", 1, markInput=False)
        cont = self.makeContainer("Cont", child, 7)
        child.setExpression("Length", "Cont.Length")
        self.Doc.recompute()

        tempFile = FreeCAD.getTempPath() + "InputStratumTest.FCStd"
        self.Doc.saveAs(tempFile)
        FreeCAD.closeDocument("InputStratumTest")
        self.Doc = FreeCAD.openDocument(tempFile)

        cont = self.Doc.getObject("Cont")
        child = self.Doc.getObject("Child")
        self.assertAlmostEqual(child.Length, 7)
        self.assertNotIn(child, cont.InList)

        cont.Length = 30
        self.Doc.recompute()
        self.assertAlmostEqual(child.Length, 30)
