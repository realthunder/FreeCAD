# SPDX-License-Identifier: LGPL-2.1-or-later

"""Link arrays over shapes: upstream's Part::LinkArray* classes, which are
App::LinkArray with the kind of pattern preset, and the references Part
resolves for App::Pattern. Adapted from upstream's TestLinkArray* tests.
"""

import os
import tempfile
import unittest
import zipfile

import FreeCAD as App
import Part

V = App.Vector


def placements(array):
    if array.ShowElement:
        return [element.Placement for element in array.ElementList]
    return list(array.PlacementList)


class LinkArrayCase(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument(type(self).__name__)
        self.source = self.doc.addObject("Part::Box", "Source")
        self.source.Length = 2
        self.source.Width = 2
        self.source.Height = 2

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def addArray(self, kind, showElement=False):
        array = self.doc.addObject("Part::LinkArray" + kind, "Array")
        array.LinkedObject = self.source
        array.ShowElement = showElement
        return array

    def assertPositions(self, array, expected):
        self.assertEqual(array.getStatusString(), "Valid")
        actual = [placement.Base for placement in placements(array)]
        self.assertEqual(len(actual), len(expected))
        for position, point in zip(actual, expected):
            self.assertLess((position - point).Length, 1e-7, (position, point))

    def hidden(self, array):
        return [i for i in range(array.ElementCount) if not array.isElementVisible(str(i))]


class TestLinkArrayClasses(LinkArrayCase):
    def testUpstreamClassesPresetTheKind(self):
        for kind in ("Linear", "Polar", "Circular", "Path", "Point"):
            with self.subTest(kind=kind):
                array = self.addArray(kind)
                self.assertTrue(array.isDerivedFrom("App::LinkArray"))
                self.assertEqual(array.PatternType, kind)
                self.doc.removeObject(array.Name)

    def testSavedAsTheClassOfItsKind(self):
        array = self.addArray("Linear")
        array.PatternType = "Polar"
        array.Occurrences = 5
        array.Mode = "Spacing"
        array.Offset = 30
        self.doc.recompute()
        with tempfile.TemporaryDirectory(prefix="freecad_part_link_array_") as directory:
            path = os.path.join(directory, "array.FCStd")
            self.doc.saveAs(path)
            with zipfile.ZipFile(path) as archive:
                xml = archive.read("Document.xml").decode("utf-8")
            self.assertRegex(xml, r'<Object type="Part::LinkArrayPolar" name="Array"')
            # The extension is the object's properties, not recorded itself
            self.assertNotIn("App::PatternExtension", xml)
            App.closeDocument(self.doc.Name)
            self.doc = App.openDocument(path)
        array = self.doc.getObject("Array")
        self.assertEqual(array.TypeId, "Part::LinkArrayPolar")
        self.assertEqual(array.PatternType, "Polar")
        self.assertEqual(array.Occurrences, 5)
        self.assertAlmostEqual(array.Offset.Value, 30)

    def testUpstreamPropertyTypes(self):
        array = self.addArray("Path")
        self.assertEqual(array.getTypeIdOfProperty("StartOffset"), "App::PropertyLength")
        self.assertEqual(array.getTypeIdOfProperty("EndOffset"), "App::PropertyLength")
        array.PatternType = "Circular"
        self.assertEqual(array.getTypeIdOfProperty("RadialDistance"), "App::PropertyLength")

    def testSuppressedElementsLeaveTheShape(self):
        array = self.addArray("Linear", True)
        array.Occurrences = 3
        array.Length = 20
        self.doc.recompute()
        self.assertEqual(len(Part.getShape(array).Solids), 3)
        array.setElementVisible("1", False)
        self.doc.recompute()
        self.assertEqual(len(Part.getShape(array).Solids), 2)
        self.assertNotIn("1.", array.getSubObjects())


class TestLinkArrayReferences(LinkArrayCase):
    def testEdgeAndFaceDirections(self):
        line = self.doc.addObject("Part::Feature", "Line")
        line.Shape = Part.makeLine(V(0, 0, 0), V(0, 0, 5))
        array = self.addArray("Linear")
        array.Direction = (line, ["Edge1"])
        array.Occurrences = 2
        self.doc.recompute()
        self.assertPositions(array, [V(0, 0, 0), V(0, 0, 100)])

        box = self.doc.addObject("Part::Box", "Box")
        box.Placement.Rotation = App.Rotation(V(0, 0, 1), 90)
        self.doc.recompute()
        # Face2 of a box is at +X, turned to +Y
        array.Direction = (box, ["Face2"])
        self.doc.recompute()
        self.assertEqual(array.getStatusString(), "Valid")
        last = placements(array)[-1].Base
        self.assertAlmostEqual(last.x, 0)
        self.assertAlmostEqual(abs(last.y), 100)

        circle = self.doc.addObject("Part::Circle", "Circle")
        self.doc.recompute()
        array.Direction = (circle, ["Edge1"])
        self.doc.recompute()
        self.assertIn("straight", array.getStatusString())

    def testCircularAxisEdge(self):
        array = self.addArray("Circular")
        array.RadialDistance = 10
        array.TangentialDistance = 10
        array.NumberCircles = 3
        array.Symmetry = 4
        axis = self.doc.addObject("Part::Feature", "Axis")
        axis.Shape = Part.makeLine(V(5, 0, 0), V(5, 10, 0))
        array.Axis = (axis, ["Edge1"])
        self.doc.recompute()
        self.assertEqual(array.getStatusString(), "Valid")
        for placement in placements(array):
            self.assertAlmostEqual(placement.Base.y, 0)
        # The edge gives the center as well as the direction
        self.assertAlmostEqual(placements(array)[2].Base.x, -5)

    def testCircularRingPopulation(self):
        array = self.addArray("Circular")
        array.RadialDistance = 10
        array.TangentialDistance = 10
        array.NumberCircles = 3
        array.Symmetry = 4
        self.doc.recompute()
        self.assertEqual(array.getStatusString(), "Valid")
        self.assertEqual(array.ElementCount, 1 + 4 + 12)
        self.assertEqual(array.PlacementList[0], App.Placement())
        self.assertAlmostEqual(array.PlacementList[1].Base.Length, 10)
        self.assertAlmostEqual(array.PlacementList[5].Base.Length, 20)

        array.NumberCircles = 2
        array.RadialDistance = 1000000
        array.TangentialDistance = 0.001
        self.doc.recompute()
        self.assertIn("10000", array.getStatusString())

    def testPolarAxisCircle(self):
        circle = self.doc.addObject("Part::Circle", "Circle")
        circle.Placement.Base = V(10, 0, 0)
        array = self.addArray("Polar")
        array.Axis = (circle, ["Edge1"])
        array.Occurrences = 2
        self.doc.recompute()
        self.assertPositions(array, [V(0, 0, 0), V(20, 0, 0)])


class TestLinkArrayPath(LinkArrayCase):
    def setUp(self):
        super().setUp()
        self.path = self.doc.addObject("Part::Feature", "Path")
        self.path.Shape = Part.makePolygon([V(0, 0, 0), V(10, 0, 0), V(10, 10, 0)])
        self.array = self.addArray("Path")
        self.array.Path = (self.path, ["Edge1", "Edge2"])

    def testFixedCountFollowsConnectedEdges(self):
        self.array.Count = 5
        self.doc.recompute()
        self.assertPositions(
            self.array, [V(0, 0, 0), V(5, 0, 0), V(10, 0, 0), V(10, 5, 0), V(10, 10, 0)]
        )

    def testWholeShape(self):
        self.array.Path = self.path
        self.array.Count = 3
        self.doc.recompute()
        self.assertPositions(self.array, [V(0, 0, 0), V(10, 0, 0), V(10, 10, 0)])

    def testFixedSpacingAndOffsets(self):
        self.array.SpacingMode = "Fixed spacing"
        self.array.Spacing = 4
        self.array.StartOffset = 2
        self.array.EndOffset = 3
        self.doc.recompute()
        self.assertPositions(self.array, [V(2, 0, 0), V(6, 0, 0), V(10, 0, 0), V(10, 4, 0)])

    def testReversePath(self):
        self.array.Count = 3
        self.array.ReversePath = True
        self.doc.recompute()
        self.assertPositions(self.array, [V(10, 10, 0), V(10, 0, 0), V(0, 0, 0)])

    def testAlignFollowsPathTangent(self):
        self.array.Count = 3
        self.array.Align = True
        self.doc.recompute()
        self.assertEqual(self.array.getStatusString(), "Valid")
        direction = self.array.PlacementList[-1].Rotation.multVec(V(1, 0, 0))
        self.assertLess((direction - V(0, 1, 0)).Length, 1e-7)

    def testReverseAndAlign(self):
        self.array.Count = 3
        self.array.ReversePath = True
        self.array.Align = True
        self.doc.recompute()
        self.assertPositions(self.array, [V(10, 10, 0), V(10, 0, 0), V(0, 0, 0)])
        direction = self.array.PlacementList[0].Rotation.multVec(V(1, 0, 0))
        self.assertLess((direction - V(0, -1, 0)).Length, 1e-7)

    def testUnsetPathIsSilent(self):
        self.array.Path = None
        self.doc.recompute()
        self.assertEqual(self.array.getStatusString(), "Valid")
        self.assertEqual(self.array.ElementCount, 0)

    def testHiddenInputsFollowTheMode(self):
        self.assertIn("Hidden", self.array.getPropertyStatus("VerticalVector"))
        self.array.Align = True
        self.assertNotIn("Hidden", self.array.getPropertyStatus("VerticalVector"))
        self.array.SpacingMode = "Fixed spacing"
        self.assertIn("Hidden", self.array.getPropertyStatus("Count"))


class TestLinkArrayPoint(LinkArrayCase):
    def setUp(self):
        super().setUp()
        self.points = self.doc.addObject("Part::Feature", "Points")
        self.points.Shape = Part.makeCompound(
            [Part.Vertex(V(2, 3, 4)), Part.Vertex(V(12, 3, 4)), Part.Vertex(V(12, 13, 4))]
        )
        self.array = self.addArray("Point")
        self.array.PointObject = self.points

    def testCopiesArePlacedAtPointCoordinates(self):
        self.doc.recompute()
        self.assertPositions(self.array, [V(2, 3, 4), V(12, 3, 4), V(12, 13, 4)])

    def testDuplicateVerticesAreIgnored(self):
        self.points.Shape = Part.makeCompound(
            [Part.Vertex(V(1, 2, 3)), Part.Vertex(V(1, 2, 3)), Part.Vertex(V(4, 5, 6))]
        )
        self.doc.recompute()
        self.assertEqual(self.array.getStatusString(), "Valid")
        self.assertEqual(self.array.ElementCount, 2)

    def testDatumPoints(self):
        lcs = self.doc.addObject("App::LocalCoordinateSystem", "LCS")
        lcs.Placement.Base = V(1, 2, 3)
        self.array.PointObject = (lcs, ["Origin"])
        self.doc.recompute()
        self.assertPositions(self.array, [V(1, 2, 3)])


class TestLinkArraySuppression(LinkArrayCase):
    """Upstream's linear suppression tests, suppression being hiding here"""

    def setUp(self):
        super().setUp()
        self.array = self.addArray("Linear", True)
        self.array.Occurrences = 3
        self.array.Occurrences2 = 3
        self.doc.recompute()

    def reload(self):
        with tempfile.TemporaryDirectory(prefix="freecad_linear_array_") as directory:
            path = os.path.join(directory, "array.FCStd")
            self.doc.saveAs(path)
            App.closeDocument(self.doc.Name)
            self.doc = App.openDocument(path)
            self.array = self.doc.getObject("Array")

    def suppress(self, index, suppressed=True):
        self.array.setElementVisible(str(index), not suppressed)

    def testSuppressionSurvivesReloadOutsideGrid(self):
        self.suppress(5)
        self.array.Occurrences2 = 1
        self.doc.recompute()
        self.reload()
        self.array.Occurrences2 = 4
        self.doc.recompute()
        self.assertEqual(self.hidden(self.array), [6])

    def testPendingResizeUsesExistingElementCoordinates(self):
        self.array.Occurrences2 = 4
        self.suppress(5)  # Still the old (1, 2) element
        self.reload()
        self.doc.recompute()
        self.assertEqual(self.hidden(self.array), [6])

    def testFailedResizeKeepsExistingElementCoordinates(self):
        self.array.Occurrences2 = 4
        self.array.Length2 = 0
        self.doc.recompute()
        self.suppress(5)
        self.array.Length2 = 100
        self.doc.recompute()
        self.assertEqual(self.hidden(self.array), [6])

    def testResizeUndoRedo(self):
        self.doc.UndoMode = 1
        self.suppress(5)
        self.doc.recompute()
        self.doc.openTransaction("Resize array")
        self.array.Occurrences2 = 4
        self.doc.recompute()
        self.doc.commitTransaction()
        self.assertEqual(self.hidden(self.array), [6])
        self.doc.undo()
        self.doc.recompute()
        self.assertEqual(self.hidden(self.array), [5])
        self.doc.redo()
        self.doc.recompute()
        self.assertEqual(self.hidden(self.array), [6])

    def testSuppressionUndoRedo(self):
        self.doc.UndoMode = 1
        self.doc.openTransaction("Suppress instance")
        self.suppress(5)
        self.doc.recompute()
        self.doc.commitTransaction()
        self.doc.undo()
        self.doc.recompute()
        self.assertEqual(self.hidden(self.array), [])
        self.doc.redo()
        self.doc.recompute()
        self.assertEqual(self.hidden(self.array), [5])
