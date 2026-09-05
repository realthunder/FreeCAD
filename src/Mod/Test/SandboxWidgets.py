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

"""Forms from the sandbox guest (docs/Sandbox.md 7.3, Probe B): a guest
script builds a form with UNMODIFIED ipywidgets; its models cross as
Jupyter comm traffic to freecad.widgets on the host, which renders them
with Qt in a task panel.  What the user does in the Qt widgets reaches
the guest's traitlets observers; what the guest sets reaches the Qt
widgets; OK/Cancel reach the guest as events; closing a widget in the
guest drops its host model.  A document principal cannot open a comm.

Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Skips headless, on a build without the sandbox host, when the image
cannot boot, and when ipywidgets is not bundled."""

import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"

# What the guest runs: the ipywidgets idiom, unchanged.
PROBE = r'''
import ipywidgets as W
import FreeCADGui

events = []
slider = W.IntSlider(value=3, min=0, max=10, description="Count")
text = W.Text(value="hello", description="Name")
check = W.Checkbox(value=False, description="Enabled")
drop = W.Dropdown(options=["Wall", "Window", "Door"], index=1, description="Kind")
button = W.Button(description="Apply")
label = W.Label(value="count 3")
box = W.VBox([slider, text, check, drop, button, label])


def on_slider(change):
    label.value = "count %d" % change["new"]
    events.append(["slider", change["new"]])


slider.observe(on_slider, names="value")
text.observe(lambda c: events.append(["text", c["new"]]), names="value")
check.observe(lambda c: events.append(["check", c["new"]]), names="value")
drop.observe(lambda c: events.append(["drop", c["new"]]), names="index")
button.on_click(lambda b: events.append(["click"]))
box.on_msg(lambda w, content, buffers: events.append(["box", content.get("event")]))
FreeCADGui.showWidget(box, "Sandbox widgets")
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


class SandboxWidgetsTest(unittest.TestCase):
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
        self.doc = FreeCAD.newDocument("SandboxWidgets")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        try:
            self.S.exec("import ipywidgets, comm, traitlets", "fcx_widgetcheck")
        except Exception as e:
            self.tearDown()
            self.skipTest("ipywidgets is not bundled for the guest: %s" % str(e)[:200])

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

    def guest(self, expr, module="fcx_widgetprobe"):
        """A python-mode read of the probe module's state."""
        return self.S.evaluate(self.owner, "import %s; %s" % (module, expr), self.opts)

    def test_a_form_from_the_guest(self):
        import FreeCADGui as Gui
        from freecad import widgets

        S = self.S
        M = widgets.manager()
        S.resetStats()
        S.exec(PROBE, "fcx_widgetprobe")
        ops = S.stats()["ops"]
        self.assertEqual(ops.get("gui.comm.manager"), 1, ops)
        self.assertGreaterEqual(ops.get("gui.comm", 0), 7, ops)  # 7 models + layouts + styles
        self.assertEqual(ops.get("gui.widget.show"), 1, ops)

        # the models, as the host manager holds them
        def one(name, description=None):
            found = M.find(name, description)
            self.assertEqual(len(found), 1, (name, description, found))
            return found[0]

        slider = one("IntSliderModel", "Count")
        text = one("TextModel", "Name")
        check = one("CheckboxModel", "Enabled")
        drop = one("DropdownModel", "Kind")
        button = one("ButtonModel", "Apply")
        label = one("LabelModel")
        box = one("VBoxModel")
        self.assertEqual(slider.state["value"], 3)
        self.assertEqual(slider.state["max"], 10)
        self.assertEqual(list(drop.state["_options_labels"]), ["Wall", "Window", "Door"])
        self.assertEqual(drop.state["index"], 1)
        self.assertEqual([M.resolve(r) for r in box.state["children"]],
                         [slider, text, check, drop, button, label])
        self.assertIsNotNone(M.resolve(slider.state["layout"]))
        self.assertEqual(M.resolve(slider.state["layout"]).name, "LayoutModel")

        # the views: a task panel holding Qt widgets in the box's order
        self.assertTrue(Gui.Control.activeDialog())
        self.assertIsNotNone(box.shown)
        self.assertEqual(box.shown[0], "panel")
        self.assertEqual(box.shown[1].form.windowTitle(), "Sandbox widgets")
        for model in (slider, text, check, drop, button, label, box):
            self.assertEqual(len(model.views), 1, model)
        sv, tv, cv, dv, bv, lv = (m.views[0] for m in (slider, text, check, drop, button, label))
        self.assertEqual(sv.slider.value(), 3)
        self.assertEqual(sv.slider.maximum(), 10)
        self.assertEqual(sv.readout.text(), "3")
        self.assertEqual(sv.label.text(), "Count")
        self.assertEqual(tv.control.text(), "hello")
        self.assertFalse(cv.widget.isChecked())
        self.assertEqual(cv.widget.text(), "Enabled")
        self.assertEqual(dv.control.count(), 3)
        self.assertEqual(dv.control.currentIndex(), 1)
        self.assertEqual(bv.widget.text(), "Apply")
        self.assertEqual(lv.widget.text(), "count 3")

        # host -> guest: what the user does reaches the observers, and
        # what the observer sets comes back to the Qt label in the same
        # round trip
        self.assertEqual(self.guest("fcx_widgetprobe.events"), [])
        sv.slider.setValue(7)
        self.assertEqual(self.guest("fcx_widgetprobe.slider.value"), 7)
        self.assertEqual(self.guest("fcx_widgetprobe.label.value"), "count 7")
        self.assertEqual(label.state["value"], "count 7")
        self.assertEqual(lv.widget.text(), "count 7")
        tv.control.setText("hi")
        tv.control.textEdited.emit("hi")
        self.assertEqual(self.guest("fcx_widgetprobe.text.value"), "hi")
        cv.widget.setChecked(True)
        self.assertTrue(self.guest("fcx_widgetprobe.check.value"))
        dv.control.setCurrentIndex(2)
        self.assertEqual(self.guest("fcx_widgetprobe.drop.value"), "Door")
        bv.widget.click()
        events = self.guest("fcx_widgetprobe.events")
        self.assertEqual(events, [["slider", 7], ["text", "hi"], ["check", True],
                                  ["drop", 2], ["click"]])

        # guest -> host: state the guest sets reaches the Qt widgets, and
        # the views applying it send nothing back
        sent = M.stats["sent"]
        S.exec("import fcx_widgetprobe as p\n"
               "p.slider.max = 20\n"
               "p.slider.value = 15\n"
               "p.text.value = 'bye'\n"
               "p.check.value = False\n"
               "p.drop.index = 0\n"
               "p.button.disabled = True\n"
               "p.label.value = 'set'\n")
        self.assertEqual(sv.slider.maximum(), 20)
        self.assertEqual(sv.slider.value(), 15)
        self.assertEqual(sv.readout.text(), "15")
        self.assertEqual(tv.control.text(), "bye")
        self.assertFalse(cv.widget.isChecked())
        self.assertEqual(dv.control.currentIndex(), 0)
        self.assertFalse(bv.widget.isEnabled())
        self.assertEqual(lv.widget.text(), "set")
        self.assertEqual(M.stats["sent"], sent)
        # the guest's own observers saw its four value writes
        self.assertEqual(len(self.guest("fcx_widgetprobe.events")), 9)

        # OK reaches the guest as an event on the root; the panel closes
        box.shown[1].accept()
        Gui.Control.closeDialog()
        self.assertEqual(self.guest("fcx_widgetprobe.events")[-1], ["box", "accept"])
        self.assertIsNone(box.shown)
        self.assertEqual(box.views, [])
        self.assertEqual(slider.views, [])
        self.assertFalse(Gui.Control.activeDialog())

        # shown again as a window, hidden from the guest
        S.exec("import fcx_widgetprobe as p, FreeCADGui\n"
               "FreeCADGui.showWidget(p.box, 'Again', 'window')\n")
        self.assertEqual(box.shown[0], "window")
        self.assertEqual(len(slider.views), 1)
        S.exec("import fcx_widgetprobe as p, FreeCADGui\nFreeCADGui.hideWidget(p.box)\n")
        self.assertIsNone(box.shown)
        self.assertEqual(slider.views, [])

        # closing in the guest drops the host models
        S.exec("import fcx_widgetprobe as p\np.label.close()\n")
        self.assertEqual(M.find("LabelModel"), [])
        self.assertTrue(label.closed)
        S.exec("import ipywidgets\nipywidgets.Widget.close_all()\n")
        self.assertEqual(M.models, {})

    def test_document_principal_is_refused(self):
        with self.assertRaises(Exception) as cm:
            self.S.evaluate(self.owner, "import ipywidgets; ipywidgets.IntSlider()", self.opts)
        text = str(cm.exception)
        self.assertIn("Permission", text)
        self.assertIn("gui", text)
        from freecad import widgets

        self.assertEqual(widgets.manager().find("IntSliderModel"), [])
