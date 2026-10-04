# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Chris Jones github.com/ipatch
# SPDX-FileNotice: Part of the FreeCAD project.

################################################################################
#                                                                              #
#   FreeCAD is free software: you can redistribute it and/or modify            #
#   it under the terms of the GNU Lesser General Public License as             #
#   published by the Free Software Foundation, either version 2.1              #
#   of the License, or (at your option) any later version.                     #
#                                                                              #
#   FreeCAD is distributed in the hope that it will be useful,                 #
#   but WITHOUT ANY WARRANTY; without even the implied warranty                #
#   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                    #
#   See the GNU Lesser General Public License for more details.                #
#                                                                              #
#   You should have received a copy of the GNU Lesser General Public           #
#   License along with FreeCAD. If not, see https://www.gnu.org/licenses       #
#                                                                              #
################################################################################

"""A suppressed feature, across a save and reload, and struck out in the tree
(upstream f4be654473, issue #24587).

Upstream's SuppressibleExtension property is Suppressed; the fork's is
PartDesign::Feature::Suppress, and an upstream file's Suppressed is read into
it. Run the Gui half with FreeCAD -t TestPartDesignGui.
"""

import os
import tempfile
import time
import unittest
import zipfile

import FreeCAD
import TestSketcherApp


def _reload(doc, name):
    """Save doc to a temporary file, close it, and open it again."""
    tmpdir = tempfile.mkdtemp(prefix="freecad_test_suppressed_")
    path = os.path.join(tmpdir, name)
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    return FreeCAD.openDocument(path)


class TestSuppressed(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestSuppressed")

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _createBodyWithPadAndFillet(self):
        """A Body with a Pad (10x10x10 box) and a Fillet."""
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.PadSketch = self.Doc.addObject("Sketcher::SketchObject", "SketchPad")
        self.Body.addObject(self.PadSketch)
        TestSketcherApp.CreateRectangleSketch(self.PadSketch, (0, 0), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = self.PadSketch
        self.Pad.Length = 10
        self.Doc.recompute()
        self.Fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        self.Body.addObject(self.Fillet)
        self.Fillet.Base = (self.Pad, ["Edge1"])
        self.Fillet.Radius = 1.0
        self.Doc.recompute()

    def testSuppressChangesShape(self):
        self._createBodyWithPadAndFillet()
        volumeWithFillet = self.Body.Shape.Volume
        self.Fillet.Suppress = True
        self.Doc.recompute()
        self.assertNotAlmostEqual(volumeWithFillet, self.Body.Shape.Volume, places=2)
        self.Fillet.Suppress = False
        self.Doc.recompute()
        self.assertAlmostEqual(self.Body.Shape.Volume, volumeWithFillet, places=2)

    def testSuppressedPersistsAfterReload(self):
        self._createBodyWithPadAndFillet()
        self.Fillet.Suppress = True
        self.Doc.recompute()
        volumeSuppressed = self.Body.Shape.Volume
        self.Doc = _reload(self.Doc, "test_suppressed.FCStd")
        self.assertTrue(self.Doc.getObject("Fillet").Suppress)
        self.assertAlmostEqual(self.Doc.getObject("Body").Shape.Volume, volumeSuppressed, places=2)

    def testUnsuppressedPersistsAfterReload(self):
        self._createBodyWithPadAndFillet()
        self.Fillet.Suppress = True
        self.Doc.recompute()
        self.Fillet.Suppress = False
        self.Doc.recompute()
        volumeActive = self.Body.Shape.Volume
        self.Doc = _reload(self.Doc, "test_unsuppressed.FCStd")
        self.assertFalse(self.Doc.getObject("Fillet").Suppress)
        self.assertAlmostEqual(self.Doc.getObject("Body").Shape.Volume, volumeActive, places=2)

    def testMultipleSuppressedFeaturesReload(self):
        self._createBodyWithPadAndFillet()
        self.Chamfer = self.Doc.addObject("PartDesign::Chamfer", "Chamfer")
        self.Body.addObject(self.Chamfer)
        self.Chamfer.Base = (self.Fillet, ["Edge2"])
        self.Chamfer.Size = 0.5
        self.Doc.recompute()
        self.Fillet.Suppress = True
        self.Chamfer.Suppress = True
        self.Doc.recompute()
        self.Doc = _reload(self.Doc, "test_multi_suppressed.FCStd")
        self.assertTrue(self.Doc.getObject("Fillet").Suppress)
        self.assertTrue(self.Doc.getObject("Chamfer").Suppress)

    def testUpstreamSuppressedName(self):
        # An upstream file saves the state as Suppressed; it loads suppressed
        self._createBodyWithPadAndFillet()
        self.Fillet.Suppress = True
        self.Doc.recompute()
        tmpdir = tempfile.mkdtemp(prefix="freecad_test_suppressed_")
        ours = os.path.join(tmpdir, "ours.FCStd")
        theirs = os.path.join(tmpdir, "theirs.FCStd")
        self.Doc.saveCopy(ours)
        with zipfile.ZipFile(ours) as zin, zipfile.ZipFile(theirs, "w", zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                data = zin.read(item.filename)
                if item.filename == "Document.xml":
                    self.assertIn(b'name="Suppress" type="App::PropertyBool"', data)
                    data = data.replace(b'name="Suppress" type="App::PropertyBool"',
                                        b'name="Suppressed" type="App::PropertyBool"')
                zout.writestr(item, data)
        doc = FreeCAD.openDocument(theirs)
        try:
            self.assertTrue(doc.getObject("Fillet").Suppress)
            self.assertFalse(doc.getObject("Pad").Suppress)
        finally:
            FreeCAD.closeDocument(doc.Name)


def _findTreeWidget():
    """The main tree widget (QTreeWidget) in the FreeCAD GUI."""
    import FreeCADGui
    from PySide import QtWidgets

    mw = FreeCADGui.getMainWindow()
    if mw is None:
        return None
    trees = mw.findChildren(QtWidgets.QTreeWidget)
    for tree in trees:
        if tree.topLevelItemCount() > 0:
            return tree
    return trees[-1] if trees else None


def _getTreeItemStrikeOut(tree, label, parent=None):
    """The strikeOut state of the item labelled label, None if not found."""
    if parent is None:
        for i in range(tree.topLevelItemCount()):
            result = _getTreeItemStrikeOut(tree, label, tree.topLevelItem(i))
            if result is not None:
                return result
        return None
    if parent.text(0) == label:
        return parent.font(0).strikeOut()
    for i in range(parent.childCount()):
        result = _getTreeItemStrikeOut(tree, label, parent.child(i))
        if result is not None:
            return result
    return None


class TestSuppressedStrikethrough(unittest.TestCase):
    """A suppressed feature's tree label is struck out, after a reload too."""

    def setUp(self):
        import FreeCADGui

        if not FreeCAD.GuiUp or FreeCADGui.getMainWindow() is None:
            self.skipTest("Requires GUI")
        self.Doc = FreeCAD.newDocument("TestSuppressedStrikethrough")

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _events(self):
        from PySide import QtWidgets

        QtWidgets.QApplication.processEvents()

    def _strikeOut(self, label):
        tree = _findTreeWidget()
        self.assertIsNotNone(tree, "Could not find tree widget")
        for _ in range(50):
            self._events()
            strikeOut = _getTreeItemStrikeOut(tree, label)
            if strikeOut is not None:
                return strikeOut
            time.sleep(0.05)
        self.fail("Could not find '%s' tree item" % label)

    def _createBodyWithBox(self):
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        self.Body.addObject(self.Box)
        self.Box.Length = self.Box.Width = self.Box.Height = 10.0
        self.Doc.recompute()
        self._events()

    def testSuppressedShowsStrikethrough(self):
        self._createBodyWithBox()
        self.assertFalse(self._strikeOut("Box"))
        self.Box.Suppress = True
        self.Doc.recompute()
        self.assertTrue(self._strikeOut("Box"))

    def testUnsuppressedNoStrikethrough(self):
        self._createBodyWithBox()
        self.Box.Suppress = True
        self.Doc.recompute()
        self._events()
        self.Box.Suppress = False
        self.Doc.recompute()
        self.assertFalse(self._strikeOut("Box"))

    def testStrikethroughPersistsAfterReload(self):
        self._createBodyWithBox()
        self.Box.Suppress = True
        self.Doc.recompute()
        self._events()
        self.Doc = _reload(self.Doc, "test_strikethrough.FCStd")
        self._events()
        self.assertTrue(self.Doc.getObject("Box").Suppress)
        self.assertTrue(self._strikeOut("Box"))

    def testActiveBodyHighlightKeepsTheStrike(self):
        # The active body's highlight resets the font of a body; a suppressed
        # feature keeps its strike when its highlight goes
        import FreeCADGui

        self._createBodyWithBox()
        self.Box.Suppress = True
        self.Doc.recompute()
        self._events()
        view = FreeCADGui.getDocument(self.Doc.Name).ActiveView
        view.setActiveObject("pdbody", self.Body)
        self._events()
        view.setActiveObject("pdbody", None)
        self._events()
        self.assertTrue(self._strikeOut("Box"))
