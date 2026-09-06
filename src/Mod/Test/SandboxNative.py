# SPDX-License-Identifier: LGPL-2.1-or-later
# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD Project Association                        *
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

"""The native task panels ported onto the host widget layer (docs/Sandbox.md
7.12, H1): the panel is models, the Qt backend renders them, and the
gate drives the REAL widgets as a user would and reads the document.
No sandbox guest is involved; the module runs under the GUI gate
(scripts/sandbox-gui-gate.py) beside the guest modules.

H1b: Pad's panel, with the expression seam.  The Length field is a
`Gui::Fw::QuantitySpinBox` model bound to `Pad.Length`; the real
`Gui::PrefQuantitySpinBox` binds to the same path.  A value typed into
the widget reaches the property through the model's signal; an
expression set on the document reaches the bag (`expression`) and the
widget (read-only, showing the result); OK keeps the expression.  The
three direction fields are `Gui::DoubleSpinBox` models carrying the
seam on a sub-path.

One document, one test: closing a 3D-view document and then pumping
the event loop trips a teardown crash in this fork unrelated to the
sandbox (a deferred `View3DInventor` deletion), so this gate opens a
single document and lets the harness close it once, as the guest
gates do.
"""

import unittest

import FreeCAD


class SandboxNativeTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        import FreeCADGui

        self.doc = FreeCAD.newDocument("SandboxNative")
        FreeCADGui.activateWorkbench("PartDesignWorkbench")

    def tearDown(self):
        import FreeCADGui

        try:
            if FreeCADGui.Control.activeDialog():
                FreeCADGui.Control.closeDialog()
        finally:
            FreeCAD.closeDocument(self.doc.Name)

    def pad(self):
        """A body with a rectangle sketch padded 10 mm."""
        import TestSketcherApp

        doc = self.doc
        body = doc.addObject("PartDesign::Body", "Body")
        sketch = doc.addObject("Sketcher::SketchObject", "SketchPad")
        sketch.Support = (doc.XY_Plane, [""])
        sketch.MapMode = "FlatFace"
        body.addObject(sketch)
        TestSketcherApp.CreateRectangleSketch(sketch, (0, 0), (20, 10))
        doc.recompute()
        pad = doc.addObject("PartDesign::Pad", "Pad")
        pad.Profile = sketch
        pad.Length = 10
        body.addObject(pad)
        doc.recompute()
        self.assertEqual(len(pad.Shape.Faces), 6)
        return pad

    @staticmethod
    def field(name):
        """The real widget of that object name in the active task panel."""
        import FreeCADGui
        from PySide import QtWidgets

        for box in FreeCADGui.Control.activeTaskDialog().getDialogContent():
            w = box.findChild(QtWidgets.QWidget, name)
            if w is not None:
                return w
        return None

    def test_pad_panel_seam(self):
        """Pad's form, ported onto the host widget layer, drives its
        properties and carries the expression seam."""
        import FreeCADGui as Gui

        pad = self.pad()
        self.assertTrue(Gui.ActiveDocument.setEdit(pad, 0))
        self.assertTrue(Gui.Control.activeDialog())

        # the form's fields are the REAL FreeCAD widgets, built by the Qt
        # backend from the models fwuic generated from the .ui
        length = self.field("lengthEdit")
        self.assertIsNotNone(length)
        self.assertEqual(length.metaObject().className(), "Gui::PrefQuantitySpinBox")
        self.assertAlmostEqual(length.property("rawValue"), 10.0)
        # the real widget is bound to the same path the model owns
        self.assertIn("Length", length.property("binding"))
        self.assertEqual(length.property("expression"), "")
        self.assertFalse(length.isReadOnly())

        # the widget drives the model drives the property
        length.setProperty("rawValue", 25.0)
        self.assertAlmostEqual(pad.Length.Value, 25.0)

        # f(x): an expression set on the document reaches the widget,
        # which shows the result and turns read-only
        pad.setExpression("Length", "3 * 7 mm")
        self.assertEqual(length.property("expression"), "3 * 7 mm")
        self.assertAlmostEqual(length.property("rawValue"), 21.0)
        self.assertTrue(length.isReadOnly())

        # a direction field is a Gui::DoubleSpinBox carrying the seam on
        # a sub-path (Direction.x)
        x = self.field("XDirectionEdit")
        self.assertIsNotNone(x)
        self.assertEqual(x.metaObject().className(), "Gui::DoubleSpinBox")
        self.assertFalse(x.isReadOnly())
        pad.setExpression("Direction.x", "0.5")
        self.assertTrue(x.isReadOnly())
        self.assertAlmostEqual(x.value(), 0.5)

        # OK: apply() keeps the expressions; the panel closes
        self.assertTrue(Gui.FormWidgets.accept())
        self.assertFalse(Gui.Control.activeDialog())
        # the length expression survives apply (a bound spin box's apply
        # keeps the expression); Direction is written back as a plain
        # vector by the panel, as it is without the port, so it is not
        # asserted here
        engine = dict(pad.ExpressionEngine)
        self.assertEqual(engine.get("Length"), "3 * 7 mm")
        self.doc.recompute()
        self.assertAlmostEqual(pad.Length.Value, 21.0)
