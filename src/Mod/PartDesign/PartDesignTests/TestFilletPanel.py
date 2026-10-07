# SPDX-License-Identifier: LGPL-2.1-or-later
"""The fillet task panel's setback corners (docs/CornerBlending.md 9.6).

Drives the panel as a user would: the rows of a corner, an edit of a
fillet's setback and of the corner's, a corner handle, Clear, a vertex
picked in toggle mode and its row removed.

Needs the GUI: run it through scripts/sandbox-gui-gate.py, which works
on the offscreen platform

    SANDBOX_GUI_GATE_MODULES=PartDesignTests.TestFilletPanel

Skips headless, and without the OCCT fork's setback corners.
"""

import time
import unittest

import FreeCAD as App


def _nameAt(shape, kind, point):
    """The name of the vertex at point, or of the edges ending there"""
    vertex = [v for v in shape.Vertexes if v.Point.isEqual(point, 1e-7)][0]
    if kind == "Vertex":
        return "Vertex%d" % shape.findSubShape(vertex)[1]
    return [
        "Edge%d" % shape.findSubShape(e)[1]
        for e in shape.Edges
        if any(v.isSame(vertex) for v in e.Vertexes)
    ]


@unittest.skipUnless(App.GuiUp, "needs the GUI")
class TestFilletPanel(unittest.TestCase):
    Setback = 4

    def setUp(self):
        import FreeCADGui as Gui
        from PySide import QtWidgets

        self.Gui = Gui
        self.QtWidgets = QtWidgets
        self.Doc = App.newDocument("PartDesignTestFilletPanel")
        body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = body.newObject("PartDesign::AdditiveBox", "Box")
        self.Doc.recompute()
        self.Vertex = _nameAt(self.Box.Shape, "Vertex", App.Vector(10, 10, 10))
        self.Edges = _nameAt(self.Box.Shape, "Edge", App.Vector(10, 10, 10))
        try:
            shape = self.Box.Shape
            shape.makeFillet(1, [shape.getElement(e) for e in self.Edges], corners={self.Vertex: 0})
        except Exception as e:
            if "OCCT fork" not in str(e):
                raise
            App.closeDocument(self.Doc.Name)
            self.skipTest("setback corners need the OCCT fork")
        self.Fillet = body.newObject("PartDesign::Fillet", "Fillet")
        self.Fillet.Base = (self.Box, self.Edges)
        self.Fillet.Radius = 1
        self.Fillet.Corners = {self.Vertex: (2, {self.Edges[0]: 3})}
        self.Doc.recompute()
        Gui.ActiveDocument.setEdit(self.Fillet)
        self.process()
        self.Tree = self.widget(QtWidgets.QTreeWidget, "treeWidgetReferences")

    def tearDown(self):
        from PySide import QtCore

        if self.Gui.Control.activeDialog():
            self.Gui.Control.closeDialog()
        self.process()
        # the closed panel goes by deleteLater; the next test's must be the
        # only one to find
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        App.closeDocument(self.Doc.Name)

    def process(self):
        self.QtWidgets.QApplication.processEvents()

    def waitFor(self, predicate, timeout=10):
        """The panel recomputes after a delay; wait for what it does"""
        end = time.time() + timeout
        while time.time() < end:
            self.process()
            if predicate():
                return True
            time.sleep(0.02)
        return False

    def expected(self, corners):
        shape = self.Box.Shape
        edges = [shape.getElement(e) for e in self.Edges]
        return shape.makeFillet(1, edges, corners=corners).Volume

    def row(self, name):
        for i in range(self.Tree.topLevelItemCount()):
            item = self.Tree.topLevelItem(i)
            if item.text(0) == name:
                return item
        self.fail("no row " + name)

    def rows(self, item):
        return [(item.child(i).text(0), item.child(i).text(4)) for i in range(item.childCount())]

    def widget(self, cls, name):
        """The open panel's widget"""
        found = [
            w for w in self.Gui.getMainWindow().findChildren(cls, name) if w.isVisible()
        ]
        self.assertEqual(len(found), 1, name)
        return found[0]

    def testRows(self):
        self.assertEqual(self.Tree.columnCount(), 5)
        corner = self.row(self.Vertex)
        self.assertEqual(corner.text(4), "2.00 mm")
        # a fillet's own setback, and the corner's it inherits in brackets
        e = self.Edges
        self.assertEqual(
            self.rows(corner), [(e[0], "3.00 mm"), (e[1], "(2.00 mm)"), (e[2], "(2.00 mm)")]
        )
        # an edge row takes no setback
        self.assertEqual(self.row(e[0]).text(4), "")

    def testEdit(self):
        from PySide import QtCore

        e = self.Edges
        # hold the row: a child reached through a dropped parent wrapper
        # is invalidated with it (PySide)
        corner = self.row(self.Vertex)
        corner.child(1).setData(4, QtCore.Qt.UserRole, 1.5)
        corners = {self.Vertex: (2.0, {e[0]: 3.0, e[1]: 1.5})}
        self.assertEqual(self.Fillet.Corners, corners)
        self.assertTrue(self.waitFor(lambda: self.rows(self.row(self.Vertex))[1][1] == "1.50 mm"))
        self.assertAlmostEqual(self.Fillet.Shape.Volume, self.expected(corners), places=6)
        self.row(self.Vertex).setData(4, QtCore.Qt.UserRole, 2.5)
        self.assertEqual(self.Fillet.Corners[self.Vertex][0], 2.5)
        self.assertTrue(self.waitFor(lambda: self.rows(self.row(self.Vertex))[2][1] == "(2.50 mm)"))

    def testHandle(self):
        # the current corner's handles drive hidden spin boxes, one per fillet
        corner = self.row(self.Vertex)
        self.Tree.setCurrentItem(corner.child(0))
        self.process()
        panel = [
            w
            for w in self.Gui.getMainWindow().findChildren(self.QtWidgets.QWidget)
            if w.metaObject().className() == "PartDesignGui::TaskFilletParameters"
            and w.isVisible()
        ][0]
        boxes = [
            w
            for w in panel.findChildren(self.QtWidgets.QWidget)
            if w.metaObject().className() == "Gui::QuantitySpinBox" and not w.isVisible()
        ]
        values = sorted(w.property("rawValue") for w in boxes)
        # three fillets, the pool's other three unused
        self.assertEqual(values, [0.0, 0.0, 0.0, 2.0, 2.0, 3.0])
        [w for w in boxes if w.property("rawValue") == 3.0][0].setProperty("rawValue", self.Setback)
        corners = {self.Vertex: (2.0, {self.Edges[0]: float(self.Setback)})}
        self.assertEqual(self.Fillet.Corners, corners)
        self.assertTrue(self.waitFor(lambda: self.rows(self.row(self.Vertex))[0][1] == "4.00 mm"))
        self.assertAlmostEqual(self.Fillet.Shape.Volume, self.expected(corners), places=6)

    def testClear(self):
        self.Tree.clearSelection()
        self.row(self.Vertex).setSelected(True)
        self.widget(self.QtWidgets.QPushButton, "btnClear").click()
        # the corner stays; its fillets lose their own setbacks
        self.assertEqual(self.Fillet.Corners, {self.Vertex: (2.0, {})})
        self.assertTrue(self.waitFor(lambda: self.rows(self.row(self.Vertex))[0][1] == "(2.00 mm)"))

    def testPickAndRemove(self):
        vertex = _nameAt(self.Box.Shape, "Vertex", App.Vector(10, 0, 10))
        self.Tree.clearSelection()
        self.Gui.Selection.clearSelection()
        toggle = self.widget(self.QtWidgets.QCheckBox, "buttonRefAdd")
        toggle.setChecked(True)
        self.process()
        self.Gui.Selection.addSelection(self.Doc.Name, self.Box.Name, vertex)
        self.process()
        toggle.setChecked(False)
        # a picked vertex is a corner, set back by the radius
        self.assertIn(vertex, self.Fillet.Base[1])
        self.assertEqual(self.Fillet.Corners[vertex], (1.0, {}))
        self.assertTrue(self.waitFor(lambda: self.row(vertex).text(4) == "1.00 mm"))
        self.assertTrue(self.Fillet.isValid())
        # removing its row removes the corner, and only it
        self.Tree.clearSelection()
        self.row(vertex).setSelected(True)
        self.widget(self.QtWidgets.QPushButton, "btnRemove").click()
        self.process()
        self.assertNotIn(vertex, self.Fillet.Base[1])
        self.assertEqual(list(self.Fillet.Corners), [self.Vertex])
