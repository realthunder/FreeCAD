# SPDX-License-Identifier: LGPL-2.1-or-later

"""App::LinkArray, the pattern engine of App, without any geometry module.

Run with FreeCADCmd -t TestLinkArray.
"""

import math
import os
import tempfile
import unittest

import FreeCAD as App


def placements(array):
    if array.ShowElement:
        return [element.Placement for element in array.ElementList]
    return list(array.PlacementList)


class TestLinkArray(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("TestLinkArray")
        self.source = self.doc.addObject("App::FeatureTest", "Source")
        self.array = self.doc.addObject("App::LinkArray", "Array")
        self.array.LinkedObject = self.source

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def reload(self):
        with tempfile.TemporaryDirectory(prefix="freecad_link_array_") as directory:
            path = os.path.join(directory, "array.FCStd")
            self.doc.saveAs(path)
            App.closeDocument(self.doc.Name)
            self.doc = App.openDocument(path)
            self.array = self.doc.getObject("Array")
            self.source = self.doc.getObject("Source")

    def assertPositions(self, expected, array=None):
        array = array or self.array
        actual = [placement.Base for placement in placements(array)]
        self.assertEqual(len(actual), len(expected))
        for position, point in zip(actual, expected):
            self.assertLess((position - point).Length, 1e-7, (position, point))

    def hidden(self, array=None):
        array = array or self.array
        return [i for i in range(array.ElementCount) if not array.isElementVisible(str(i))]

    def testLinearDefaultsToTheLocalAxes(self):
        self.assertEqual(self.array.PatternType, "Linear")
        self.array.Occurrences = 3
        self.array.Occurrences2 = 2
        self.array.Length2 = 20
        self.doc.recompute()
        self.assertEqual(self.array.getStatusString(), "Valid")
        self.assertEqual(self.array.ElementCount, 6)
        V = App.Vector
        self.assertPositions(
            [V(0, 0, 0), V(0, 20, 0), V(50, 0, 0), V(50, 20, 0), V(100, 0, 0), V(100, 20, 0)]
        )

    def testSpacingMode(self):
        self.array.Occurrences = 4
        self.assertEqual(len(self.array.Spacings), 3)
        self.array.Mode = "Spacing"
        self.array.Offset = 5
        self.assertAlmostEqual(self.array.Length.Value, 15)
        self.array.Spacings = [-1, 10, -1]
        self.doc.recompute()
        V = App.Vector
        self.assertPositions([V(0, 0, 0), V(5, 0, 0), V(15, 0, 0), V(20, 0, 0)])

    def testSwitchingKindSwapsTheInputs(self):
        self.array.Occurrences = 4
        self.array.PatternType = "Polar"
        self.assertFalse(hasattr(self.array, "Direction"))
        self.assertFalse(hasattr(self.array, "Occurrences2"))
        self.assertEqual(self.array.getTypeIdOfProperty("Offset"), "App::PropertyAngle")
        self.assertEqual(self.array.getTypeIdOfProperty("Axis"), "App::PropertyLinkSub")
        # Shared with the same type: kept
        self.assertEqual(self.array.Occurrences, 4)
        self.doc.recompute()
        self.assertEqual(self.array.getStatusString(), "Valid")
        self.assertEqual(self.array.ElementCount, 4)
        for i, placement in enumerate(placements(self.array)):
            turned = placement.multVec(App.Vector(1, 0, 0))
            angle = math.radians(i * 90)
            expected = App.Vector(math.cos(angle), math.sin(angle), 0)
            self.assertLess((turned - expected).Length, 1e-7)

        self.array.PatternType = "Circular"
        self.assertFalse(hasattr(self.array, "Occurrences"))
        self.array.RadialDistance = 10
        self.array.TangentialDistance = 10
        self.array.Symmetry = 4
        self.doc.recompute()
        self.assertEqual(self.array.getStatusString(), "Valid")
        # floor(2*pi*10/10) = 6 -> 4, floor(2*pi*20/10) = 12
        self.assertEqual(self.array.ElementCount, 1 + 4 + 12)

        self.array.PatternType = "Linear"
        self.assertEqual(self.array.Occurrences, 2)
        self.assertTrue(hasattr(self.array, "Direction2"))

    def testPathAndPointWithoutReferenceAreEmpty(self):
        for kind in ("Path", "Point"):
            with self.subTest(kind=kind):
                self.array.PatternType = kind
                self.doc.recompute()
                self.assertEqual(self.array.getStatusString(), "Valid")
                self.assertEqual(self.array.ElementCount, 0)

    def testDatumReferences(self):
        lcs = self.doc.addObject("App::LocalCoordinateSystem", "LCS")
        lcs.Placement = App.Placement(App.Vector(10, 0, 0), App.Rotation(App.Vector(0, 0, 1), 90))
        self.array.Direction = (lcs, ["X_Axis"])
        self.array.Occurrences = 3
        self.doc.recompute()
        V = App.Vector
        self.assertEqual(self.array.getStatusString(), "Valid")
        self.assertPositions([V(0, 0, 0), V(0, 50, 0), V(0, 100, 0)])

        # The normal of a plane: local Y of the plane, global X of the turned LCS
        self.array.Direction = (lcs, ["XZ_Plane"])
        self.doc.recompute()
        self.assertEqual(self.array.getStatusString(), "Valid")
        last = placements(self.array)[-1].Base
        self.assertAlmostEqual(abs(last.x), 100)
        self.assertAlmostEqual(last.y, 0)

        # An axis through the base of the line
        self.array.PatternType = "Polar"
        self.array.Axis = (lcs, ["Z_Axis"])
        self.array.Occurrences = 2
        self.doc.recompute()
        self.assertEqual(self.array.getStatusString(), "Valid")
        self.assertPositions([V(0, 0, 0), V(20, 0, 0)])

    def testReferenceInTheFrameOfTheArray(self):
        lcs = self.doc.addObject("App::LocalCoordinateSystem", "LCS")
        self.array.Direction = (lcs, ["X_Axis"])
        self.array.Occurrences = 2
        self.array.Placement = App.Placement(App.Vector(), App.Rotation(App.Vector(0, 0, 1), 90))
        self.doc.recompute()
        # Along the global X axis, whichever way the array turns
        position = self.array.Placement.multVec(placements(self.array)[1].Base)
        self.assertLess((position - App.Vector(100, 0, 0)).Length, 1e-7)

    def testSuppressionKeepsGridPositions(self):
        self.array.Occurrences = 3
        self.array.Occurrences2 = 3
        self.doc.recompute()
        self.array.setElementVisible("0", False)
        self.array.setElementVisible("5", False)  # (1, 2)
        self.array.setElementVisible("8", False)  # (2, 2)
        self.assertEqual(sorted(self.array.SuppressedPositions), [(0, 0), (1, 2), (2, 2)])
        self.assertNotIn("5.", self.array.getSubObjects())
        self.assertIn("4.", self.array.getSubObjects())

        self.array.Occurrences2 = 5
        self.array.Occurrences = 4
        self.doc.recompute()
        self.assertEqual(self.hidden(), [0, 7, 12])

        self.array.Occurrences = 1
        self.array.Occurrences2 = 2
        self.doc.recompute()
        self.assertEqual(self.hidden(), [0])
        self.array.setElementVisible("0", True)

        # The positions outside the grid came back
        self.array.Occurrences = 3
        self.array.Occurrences2 = 3
        self.doc.recompute()
        self.assertEqual(self.hidden(), [5, 8])

    def testSuppressionSurvivesCollapsingAndReload(self):
        self.array.Occurrences = 3
        self.array.Occurrences2 = 3
        self.doc.recompute()
        self.array.setElementVisible("5", False)
        self.array.ShowElement = False
        self.doc.recompute()
        self.assertEqual(self.hidden(), [5])
        self.array.ShowElement = True
        self.doc.recompute()
        self.assertEqual(self.hidden(), [5])

        self.reload()
        self.array.Occurrences2 = 4
        self.doc.recompute()
        self.assertEqual(self.hidden(), [6])

    def testSuppressedPositionsSetDirectly(self):
        self.array.Occurrences = 2
        self.array.Occurrences2 = 2
        self.doc.recompute()
        self.array.SuppressedPositions = [(1, 1)]
        self.assertEqual(self.hidden(), [3])

    def testOtherKindsSuppressByIndex(self):
        self.array.PatternType = "Polar"
        self.doc.recompute()
        self.array.setElementVisible("1", False)
        self.array.Occurrences = 5
        self.doc.recompute()
        self.assertEqual(self.hidden(), [1])
        self.assertNotIn("1.", self.array.getSubObjects())

    def testKindSurvivesReload(self):
        self.array.PatternType = "Polar"
        self.array.Occurrences = 5
        self.array.Offset = 30
        self.array.Mode = "Spacing"
        self.doc.recompute()
        self.reload()
        self.assertEqual(self.array.PatternType, "Polar")
        self.assertFalse(hasattr(self.array, "Direction"))
        self.assertEqual(self.array.getTypeIdOfProperty("Offset"), "App::PropertyAngle")
        self.assertAlmostEqual(self.array.Offset.Value, 30)
        self.assertEqual(self.array.Mode, "Spacing")
        self.assertIn("ReadOnly", self.array.getPropertyStatus("Angle"))
        self.array.touch()
        self.doc.recompute()
        self.assertEqual(self.array.getStatusString(), "Valid")
        self.assertEqual(self.array.ElementCount, 5)
        # The constraint, which the file does not keep
        self.array.Occurrences = 0
        self.assertEqual(self.array.Occurrences, 1)

    def testKindChangeUndoRedo(self):
        self.doc.UndoMode = 1
        self.array.Occurrences = 3
        self.doc.recompute()
        self.doc.openTransaction("Change kind")
        self.array.PatternType = "Polar"
        self.array.Offset = 45
        self.doc.commitTransaction()
        self.doc.undo()
        self.assertEqual(self.array.PatternType, "Linear")
        self.assertTrue(hasattr(self.array, "Direction"))
        self.assertEqual(self.array.getTypeIdOfProperty("Offset"), "App::PropertyLength")
        self.doc.redo()
        self.assertEqual(self.array.PatternType, "Polar")
        self.assertFalse(hasattr(self.array, "Direction"))
        self.assertAlmostEqual(self.array.Offset.Value, 45)
