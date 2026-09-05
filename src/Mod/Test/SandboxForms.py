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

"""Qt-shaped forms from the sandbox guest (docs/Sandbox.md 7.11, G3a):
a guest loads Draft's TaskPanel_OrthoArray.ui with `Gui.PySideUic.loadUi`,
drives the widgets with Qt's own accessors and signals, and shows the
panel through `Gui.Control.showDialog`; the host renders the same .ui
through uic, binds each named child to the guest's model, and what the
user does in the Qt widgets reaches the guest as state and Qt-named
signals.  Then Draft's real task_orthoarray.py, exec'd unmodified in
the guest, constructs, shows and reads its form.

Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Skips headless, on a build without the sandbox host, when the image
cannot boot, and when the forms' wheels are not bundled."""

import os
import sys
import time
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"
UI_FILE = ":/ui/TaskPanel_OrthoArray.ui"

# What the guest runs: Qt as the panels write it.
PROBE = r'''
import FreeCADGui as Gui
from PySide import QtCore, QtGui, QtWidgets

events = []
form = Gui.PySideUic.loadUi(":/ui/TaskPanel_OrthoArray.ui")
form.setWindowTitle("Ortho probe")
form.setWindowIcon(QtGui.QIcon(":/icons/Draft_Array.svg"))
form.spinbox_n_X.setValue(4)
form.input_X_x.setProperty("rawValue", 120.0)
form.checkbox_fuse.setChecked(True)
form.radiobutton_y_axis.setChecked(True)
form.button_linear_mode.setChecked(True)
form.label_n_Z.setText("Z-count")

form.spinbox_n_Y.valueChanged.connect(lambda v: events.append(["nY", v]))
if hasattr(form.checkbox_link, "checkStateChanged"):
    form.checkbox_link.checkStateChanged.connect(lambda s: events.append(["link", s]))
QtCore.QObject.connect(form.button_reset_X, QtCore.SIGNAL("clicked()"),
                       lambda: events.append(["resetX"]))
form.input_Y_y.valueChanged.connect(lambda d: events.append(["Yy", d]))
form.radiobutton_z_axis.toggled.connect(lambda on: events.append(["zaxis", on]))
form.button_linear_mode.clicked.connect(lambda on: events.append(["linear", on]))
form.checkbox_fuse.stateChanged.connect(lambda s: events.append(["fuse", s]))


class Panel:
    def __init__(self):
        self.form = form
        self.accepted = None
        self.rejected = 0

    def getStandardButtons(self):
        return int(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)

    def accept(self):
        f = form
        self.accepted = {
            "nX": f.spinbox_n_X.value(), "nY": f.spinbox_n_Y.value(),
            "Xx": f.input_X_x.property("rawValue"), "Yy": f.input_Y_y.property("rawValue"),
            "Yy_text": f.input_Y_y.text(), "Xx_text": f.input_X_x.text(),
            "fuse": f.checkbox_fuse.isChecked(), "link": f.checkbox_link.isChecked(),
            "z": f.radiobutton_z_axis.isChecked(), "y": f.radiobutton_y_axis.isChecked(),
            "linear": f.button_linear_mode.isChecked(),
            "title": f.windowTitle(),
        }
        return True

    def reject(self):
        self.rejected += 1
        return True


panel = Panel()
Gui.Control.showDialog(panel)
'''


class PrefGuard:
    """Set preferences for the test, put them back after (global state)."""

    def __init__(self):
        self._undo = []

    def set_bool(self, key, value):
        params = FreeCAD.ParamGet(PARAMS)
        had = key in params.GetBools()
        prior = params.GetBool(key, False) if had else None
        self._undo.append((key, had, prior))
        params.SetBool(key, value)

    def restore(self):
        params = FreeCAD.ParamGet(PARAMS)
        for key, had, prior in reversed(self._undo):
            if had:
                params.SetBool(key, prior)
            else:
                params.RemBool(key)
        self._undo = []


class SandboxFormsTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        info = FreeCAD.ExpressionSandbox.imageInfo()
        if not info["host"]:
            self.skipTest("this build has no sandbox host")
        self.S = FreeCAD.ExpressionSandbox
        self.prefs = PrefGuard()
        self.prefs.set_bool("Evaluate", True)
        self.S.setRouting(True)
        if not self.S.available():
            self.prefs.restore()
            self.skipTest("the sandbox image did not boot")
        self.opts = self.S.OptionCallFrame | self.S.OptionPythonMode
        self.doc = FreeCAD.newDocument("SandboxForms")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        try:
            self.S.exec("import freecad.widgets.models, freecad.widgets.uic", "fcx_formcheck")
        except Exception as e:
            self.tearDown()
            self.skipTest("the forms are not bundled for the guest: %s" % str(e)[:300])
        import Draft_rc  # noqa: F401  (the .ui files are Draft's compiled resources)

    def tearDown(self):
        import FreeCADGui

        try:
            if FreeCADGui.Control.activeDialog():
                FreeCADGui.Control.closeDialog()
            from freecad import widgets

            widgets.manager().reset()
        finally:
            FreeCAD.closeDocument(self.doc.Name)
            self.prefs.restore()

    def guest(self, expr, module="fcx_formprobe"):
        """A python-mode read of the probe module's state."""
        return self.S.evaluate(self.owner, "import %s; %s" % (module, expr), self.opts)

    def test_a_ui_form_from_the_guest(self):
        import FreeCADGui as Gui
        from PySide import QtWidgets
        from freecad import widgets

        S = self.S
        M = widgets.manager()
        S.resetStats()
        t0 = time.perf_counter()
        S.exec(PROBE, "fcx_formprobe")
        t_probe = time.perf_counter() - t0
        ops = S.stats()["ops"]
        # the measurement the doc records (docs/Sandbox.md 7.11)
        sys.stderr.write("SandboxForms: loadUi + set + show %.3f s, %d models, ops %s\n"
                         % (t_probe, len(M.models), sorted(ops.items())))
        self.assertEqual(ops.get("gui.ui.read"), 1, ops)
        self.assertEqual(ops.get("gui.control.show"), 1, ops)
        self.assertGreaterEqual(ops.get("gui.comm", 0), 30, ops)  # one comm a widget

        # the models, as the host manager holds them
        forms = M.find("UiFormModel")
        self.assertEqual(len(forms), 1)
        root = forms[0]
        self.assertEqual(root.state["uiFile"], UI_FILE)
        named = {name: M.resolve(ref) for name, ref in root.state["widgets"].items()}
        for name in ("spinbox_n_X", "input_X_x", "checkbox_fuse", "radiobutton_y_axis",
                     "button_linear_mode", "label_n_Z", "group_copies"):
            self.assertIn(name, named)
        self.assertEqual(named["spinbox_n_X"].name, "QSpinBoxModel")
        self.assertEqual(named["input_X_x"].name, "InputFieldModel")
        self.assertEqual(named["input_X_x"].state["qtClass"], "Gui::InputField")
        self.assertEqual(named["group_copies"].name, "QGroupBoxModel")
        # what the guest set is listed; a .ui value is not
        self.assertEqual(named["label_n_X"].state["_touched"], [])
        self.assertEqual(named["label_n_Z"].state["_touched"], ["text"])
        self.assertEqual(named["spinbox_n_X"].state["_touched"], ["value"])
        self.assertEqual(named["spinbox_n_Y"].state["_touched"], [])
        self.assertEqual(named["input_X_x"].state["_touched"], ["rawValue", "text"])

        # the host: uic's form is the active task dialog, the guest's
        # values in the bound widgets, the file's own left to uic
        self.assertTrue(Gui.Control.activeDialog())
        panel = M.panel
        self.assertIsNotNone(panel)
        self.assertEqual(len(panel.form), 1)
        w = panel.form[0]
        self.assertEqual(w.windowTitle(), "Ortho probe")
        self.assertEqual(w.objectName(), "DraftOrthoArrayTaskPanel")
        n_x = w.findChild(QtWidgets.QSpinBox, "spinbox_n_X")
        n_y = w.findChild(QtWidgets.QSpinBox, "spinbox_n_Y")
        x_x = w.findChild(QtWidgets.QLineEdit, "input_X_x")
        y_y = w.findChild(QtWidgets.QLineEdit, "input_Y_y")
        fuse = w.findChild(QtWidgets.QCheckBox, "checkbox_fuse")
        link = w.findChild(QtWidgets.QCheckBox, "checkbox_link")
        r_y = w.findChild(QtWidgets.QRadioButton, "radiobutton_y_axis")
        r_z = w.findChild(QtWidgets.QRadioButton, "radiobutton_z_axis")
        linear = w.findChild(QtWidgets.QPushButton, "button_linear_mode")
        reset_x = w.findChild(QtWidgets.QPushButton, "button_reset_X")
        label_z = w.findChild(QtWidgets.QLabel, "label_n_Z")
        label_x = w.findChild(QtWidgets.QLabel, "label_n_X")
        self.assertEqual(x_x.metaObject().className(), "Gui::InputField")
        self.assertEqual(n_x.value(), 4)
        self.assertEqual(n_y.value(), named["spinbox_n_Y"].state["q_value"])  # the file's
        self.assertEqual(x_x.property("rawValue"), 120.0)
        self.assertTrue(fuse.isChecked())
        self.assertTrue(link.isChecked())  # the file's own default
        self.assertTrue(r_y.isChecked())
        self.assertFalse(r_z.isChecked())
        self.assertTrue(linear.isChecked())
        self.assertEqual(label_z.text(), "Z-count")
        self.assertEqual(label_x.text(), "X")
        for model in named.values():
            self.assertEqual(len(model.views), 1, model)
        # the guest formatted the text it set (its own quantity string,
        # not the host's decimals), the host its own; both parse back
        self.assertEqual(FreeCAD.Units.Quantity(
            self.guest("fcx_formprobe.form.input_X_x.text()")).Value, 120.0)
        self.assertEqual(FreeCAD.Units.Quantity(x_x.text()).Value, 120.0)

        # host -> guest: each kind of edit reaches the state and fires
        # the Qt-named signal with Qt's argument (the probe's own
        # setChecked came before its connect, as Qt would see it)
        self.assertEqual(self.guest("fcx_formprobe.events"), [])
        t0 = time.perf_counter()
        for i in range(100):
            n_y.setValue(10 + i % 5)
        sys.stderr.write("SandboxForms: 100 host spin edits -> guest %.2f ms each\n"
                         % ((time.perf_counter() - t0) * 10))
        t0 = time.perf_counter()
        S.exec("import fcx_formprobe as p\nfor i in range(100):\n"
               "    p.form.spinbox_n_Z.setValue(10 + i % 5)\n")
        sys.stderr.write("SandboxForms: 100 guest spin sets -> Qt %.2f ms each\n"
                         % ((time.perf_counter() - t0) * 10))
        self.assertEqual(len(self.guest("fcx_formprobe.events")), 100)  # one signal each
        S.exec("import fcx_formprobe as p\np.events.clear()\n")
        n_y.setValue(7)
        self.assertEqual(self.guest("fcx_formprobe.form.spinbox_n_Y.value()"), 7)
        link.click()
        self.assertFalse(self.guest("fcx_formprobe.form.checkbox_link.isChecked()"))
        reset_x.click()
        y_y.setProperty("rawValue", 55.0)
        self.assertEqual(self.guest("fcx_formprobe.form.input_Y_y.property('rawValue')"), 55.0)
        self.assertEqual(self.guest("fcx_formprobe.form.input_Y_y.text()"), y_y.text())
        r_z.click()
        self.assertTrue(self.guest("fcx_formprobe.form.radiobutton_z_axis.isChecked()"))
        self.assertFalse(self.guest("fcx_formprobe.form.radiobutton_y_axis.isChecked()"))
        linear.click()
        self.assertFalse(self.guest("fcx_formprobe.form.button_linear_mode.isChecked()"))
        events = self.guest("fcx_formprobe.events")
        self.assertEqual(events, [["nY", 7], ["link", 0], ["resetX"],
                                  ["Yy", 55.0], ["zaxis", True], ["linear", False]])

        # guest -> host: state set from the guest reaches the widgets and
        # the views send nothing back for it
        sent = M.stats["sent"]
        S.exec("import fcx_formprobe as p\n"
               "p.form.spinbox_n_X.setValue(9)\n"
               "p.form.checkbox_fuse.setChecked(False)\n"
               "p.form.input_X_x.setValue(33)\n"
               "p.form.group_copies.setTitle('Copies')\n"
               "p.form.button_reset_Y.setEnabled(False)\n"
               "p.form.group_Z.hide()\n")
        self.assertEqual(n_x.value(), 9)
        self.assertFalse(fuse.isChecked())
        self.assertEqual(x_x.property("rawValue"), 33.0)
        self.assertEqual(w.findChild(QtWidgets.QGroupBox, "group_copies").title(), "Copies")
        self.assertFalse(w.findChild(QtWidgets.QPushButton, "button_reset_Y").isEnabled())
        self.assertTrue(w.findChild(QtWidgets.QGroupBox, "group_Z").isHidden())
        self.assertEqual(M.stats["sent"], sent)
        self.assertEqual(self.guest("fcx_formprobe.events")[-1], ["fuse", 0])

        # OK: the dialog's accept crosses to the guest's panel, which
        # reads every field back; the views detach with the widgets
        # (this fork's Control.activeDialog() is a bool: the panel
        # object the dialog holds is driven directly, as the widgets
        # gate does)
        self.assertTrue(M.panel.accept())
        Gui.Control.closeDialog()
        accepted = self.guest("fcx_formprobe.panel.accepted")
        self.assertEqual(FreeCAD.Units.Quantity(accepted.pop("Xx_text")).Value, 33.0)
        self.assertEqual(accepted, {
            "nX": 9, "nY": 7, "Xx": 33.0, "Yy": 55.0, "Yy_text": y_y.text(),
            "fuse": False, "link": False, "z": True, "y": False,
            "linear": False, "title": "Ortho probe"})
        self.assertFalse(Gui.Control.activeDialog())
        self.assertEqual(root.views, [])
        self.assertEqual(named["spinbox_n_X"].views, [])

        # shown again, cancelled from the host: reject crosses
        S.exec("import fcx_formprobe as p, FreeCADGui\nFreeCADGui.Control.showDialog(p.panel)\n")
        self.assertTrue(Gui.Control.activeDialog())
        self.assertEqual(len(root.views), 1)
        self.assertTrue(M.panel.reject())
        Gui.Control.closeDialog()
        self.assertEqual(self.guest("fcx_formprobe.panel.rejected"), 1)
        self.assertFalse(Gui.Control.activeDialog())

        # closed from the guest (a session exec: a document principal's
        # evaluate cannot touch Control, as the refusal case shows)
        S.exec("import fcx_formprobe as p, FreeCADGui\nFreeCADGui.Control.showDialog(p.panel)\n"
               "p.active = FreeCADGui.Control.activeDialog()\n")
        self.assertTrue(self.guest("fcx_formprobe.active"))
        S.exec("import FreeCADGui\nFreeCADGui.Control.closeDialog()\n")
        self.assertFalse(Gui.Control.activeDialog())
        self.assertEqual(root.views, [])

    def test_draft_orthoarray_panel(self):
        """Draft's task_orthoarray.py, unmodified, in the guest."""
        import FreeCADGui as Gui
        from PySide import QtWidgets
        from freecad import widgets

        path = os.path.join(FreeCAD.getResourceDir(), "Mod", "Draft", "drafttaskpanels",
                            "task_orthoarray.py")
        if not os.path.exists(path):
            path = os.path.join(FreeCAD.getHomePath(), "Mod", "Draft", "drafttaskpanels",
                                "task_orthoarray.py")
        self.assertTrue(os.path.exists(path), path)
        with open(path, encoding="utf-8") as f:
            source = f.read()
        S = self.S
        M = widgets.manager()
        S.exec(source, "fcx_task_orthoarray")
        S.resetStats()
        t0 = time.perf_counter()
        S.exec("import fcx_task_orthoarray as m, FreeCADGui, types\n"
               "panel = m.TaskPanelOrthoArray()\n"
               "panel.source_command = types.SimpleNamespace(completed=lambda: None)\n"
               "FreeCADGui.Control.showDialog(panel)\n", "fcx_orthoprobe")
        sys.stderr.write("SandboxForms: TaskPanelOrthoArray() + show %.3f s, ops %s\n"
                         % (time.perf_counter() - t0, sorted(S.stats()["ops"].items())))
        self.assertTrue(Gui.Control.activeDialog())
        w = M.panel.form[0]
        self.assertEqual(w.windowTitle(), "Orthogonal Array")
        # the panel's __init__ wrote its defaults into the form
        n_x = w.findChild(QtWidgets.QSpinBox, "spinbox_n_X")
        self.assertEqual(n_x.value(), self.guest("fcx_orthoprobe.panel.n_x", "fcx_orthoprobe"))
        x_x = w.findChild(QtWidgets.QLineEdit, "input_X_x")
        self.assertEqual(x_x.property("rawValue"),
                         self.guest("fcx_orthoprobe.panel.v_x.x", "fcx_orthoprobe"))
        # the panel reads its form back through Qt's accessors
        n_x.setValue(5)
        w.findChild(QtWidgets.QLineEdit, "input_Y_y").setProperty("rawValue", 250.0)
        self.assertEqual(self.guest("fcx_orthoprobe.panel.get_numbers()", "fcx_orthoprobe")[0], 5)
        v_x, v_y, v_z = self.guest("fcx_orthoprobe.panel.get_intervals()", "fcx_orthoprobe")
        self.assertEqual(v_y.y, 250.0)
        self.assertEqual(v_x.x, self.guest("fcx_orthoprobe.panel.v_x.x", "fcx_orthoprobe"))
        # its checkbox callbacks run in the guest on a host click
        fuse = w.findChild(QtWidgets.QCheckBox, "checkbox_fuse")
        was = self.guest("fcx_orthoprobe.panel.fuse", "fcx_orthoprobe")
        self.assertEqual(fuse.isChecked(), was)
        fuse.click()
        self.assertEqual(self.guest("fcx_orthoprobe.panel.fuse", "fcx_orthoprobe"), not was)
        self.assertEqual(fuse.isChecked(), not was)
        # cancel from the host: reject -> finish() -> the command's
        # completed(), which natively closes the dialog itself (reject
        # answers None, so the dialog is not auto-closed: the same
        # False as TaskDialogPython reads natively)
        self.assertFalse(M.panel.reject())
        self.assertTrue(Gui.Control.activeDialog())
        Gui.Control.closeDialog()
        self.assertFalse(Gui.Control.activeDialog())

    def test_document_principal_is_refused(self):
        with self.assertRaises(Exception) as cm:
            self.S.evaluate(
                self.owner,
                "import FreeCADGui; FreeCADGui.PySideUic.loadUi(':/ui/TaskPanel_OrthoArray.ui')",
                self.opts)
        self.assertIn("gui", str(cm.exception).lower())
