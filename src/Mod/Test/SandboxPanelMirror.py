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
"""The panel mirror (docs/Sandbox.md 7.19): the desktop's real task
panel -- C++ (Pad's), Python over a .ui file (Draft's OrthoArray),
Python over several .ui pages (a CAM op) -- walked into store models
and streamed, no edit to any workbench; a client's write landing in the
document through the panel's own slots, the read-back reaching the
writer, the dialog closed through the root.  Needs the GUI and no
guest: run it through scripts/sandbox-gui-gate.py under Xvfb.  Skips
headless."""

import json
import unittest

import FreeCAD

REF = "IPY_MODEL_"


def _ref(ref):
    return ref[len(REF):] if isinstance(ref, str) and ref.startswith(REF) else None


class SandboxPanelMirrorTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        import FreeCADGui as Gui

        self.Gui = Gui
        self.FW = Gui.FormWidgets
        self.previous = Gui.activeWorkbench().name()
        if Gui.Control.activeDialog():
            Gui.Control.closeDialog()
            self.spin()
        self.doc = FreeCAD.newDocument("SandboxPanelMirror")
        self.FW.watchMessages()
        self.FW.messages()
        self.FW.pushed()

    def tearDown(self):
        try:
            self.control({"op": "widgets.unsubscribe"}, 7)
            if self.Gui.Control.activeDialog():
                self.Gui.Control.closeDialog()
                self.spin()
            self.FW.mirrorPanels(False)
            self.FW.messages()
            self.FW.pushed()
            self.spin()
            if self.Gui.activeWorkbench().name() != self.previous:
                self.Gui.activateWorkbench(self.previous)
                self.spin()
        finally:
            FreeCAD.closeDocument(self.doc.Name)

    @staticmethod
    def spin(ms=30, times=4):
        from PySide import QtCore, QtWidgets

        for _ in range(times):
            QtWidgets.QApplication.processEvents()
            loop = QtCore.QEventLoop()
            QtCore.QTimer.singleShot(ms, loop.quit)
            loop.exec()

    def control(self, req, client=7):
        req = dict(req)
        req.setdefault("id", 1)
        return json.loads(self.FW.control(json.dumps(req), client))

    def pushed(self, client=None):
        out = []
        for entry in self.FW.pushed():
            if client is None or entry["client"] == client:
                out.append((entry["client"], json.loads(entry["json"])))
        return out

    def subscribe(self):
        reply = self.control({"op": "widgets.subscribe", "panels": True}, 7)
        self.assertTrue(reply["ok"], reply)
        self.assertIn("panel", reply)
        self.assertIsNone(reply["panel"])
        self.spin()
        self.FW.pushed()
        return reply

    def named(self, name):
        """The mirrored widget model of that object name, by id."""
        for wid in self.FW.ids():
            if wid.startswith("pw:") and self.FW.snapshot(wid)["state"].get("q_objectName") == name:
                return wid
        return None

    def open_panel(self):
        """The panel up: its id and root snapshot, once the walk ran."""
        self.spin(60, 6)
        pid = self.FW.panelId()
        self.assertTrue(pid, "no panel mirrored")
        self.assertTrue(pid.startswith("panel:"), pid)
        root = self.FW.snapshot(pid)
        self.assertEqual(root["model"], "QDialogModel")
        return pid, root

    def boxes(self, root):
        out = []
        for item in root["layout"]["items"]:
            snap = self.FW.snapshot(_ref(item["widget"]))
            if snap["qtClass"] == "Gui::TaskView::TaskBox":
                out.append(snap)
        return out

    def button_box(self, root):
        for item in root["layout"]["items"]:
            snap = self.FW.snapshot(_ref(item["widget"]))
            if snap["model"] == "QDialogButtonBoxModel":
                return snap
        return None

    def close_through_root(self, pid, event):
        """A client's accept or reject on the root: the dialog goes, one
        close for the root and none for the widgets."""
        self.FW.pushed()
        reply = self.control(
            {"op": "widgets.custom", "target": pid, "content": {"event": event}}, 7
        )
        self.assertTrue(reply["ok"], reply)
        self.spin(60, 6)
        self.assertFalse(self.Gui.Control.activeDialog())
        self.assertIsNone(self.FW.panelId())
        closes = [m["id"] for _, m in self.pushed(7) if m["method"] == "close"]
        self.assertEqual(closes, [pid])
        self.assertFalse([i for i in self.FW.ids() if i.startswith("pw:")])

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
        return body, pad

    def test_pad_panel(self):
        """Pad's C++ panel: the boxes, the length field as a quantity
        model, a client's write reaching Pad.Length through the panel's
        own slot, the formatted read-back reaching the writer, accept
        through the root."""
        Gui = self.Gui
        Gui.activateWorkbench("PartDesignWorkbench")
        self.spin()
        body, pad = self.pad()
        self.subscribe()
        self.assertTrue(Gui.ActiveDocument.setEdit(pad, 0))
        pid, root = self.open_panel()
        self.assertIn("Pad", root["qtClass"], root["qtClass"])
        self.assertTrue(root["state"]["q_windowTitle"])
        opens = [m for _, m in self.pushed(7) if m["method"] == "open"]
        self.assertTrue(opens)
        self.assertEqual(opens[-1]["id"], pid)
        self.assertTrue(all("parent" in m for m in opens), [m["id"] for m in opens if "parent" not in m])
        boxes = self.boxes(root)
        self.assertGreaterEqual(len(boxes), 1, root)
        self.assertEqual(boxes[0]["model"], "QGroupBoxModel")
        self.assertTrue(boxes[0]["state"]["q_title"])
        self.assertTrue(boxes[0]["state"]["q_checked"])
        buttons = self.button_box(root)
        self.assertIsNotNone(buttons, root)
        flags = [
            self.FW.snapshot(_ref(i["widget"]))["state"]["q_standardButton"]
            for i in buttons["layout"]["items"]
        ]
        self.assertIn(0x400, flags, flags)  # QDialogButtonBox.Ok
        self.assertIn(0x400000, flags, flags)  # Cancel

        length = self.named("lengthEdit")
        self.assertIsNotNone(length, "no lengthEdit model")
        snap = self.FW.snapshot(length)
        self.assertEqual(snap["model"], "QuantitySpinBoxModel")
        self.assertEqual(snap["qtClass"], "Gui::PrefQuantitySpinBox")
        st = snap["state"]
        self.assertAlmostEqual(st["q_rawValue"], 10.0)
        self.assertIn("10", st["q_text"])
        self.assertIn("Length", st["q_binding"])
        self.assertTrue(st["q_visible"])
        self.assertIn("parent", snap)

        # a client's write: the real spin box's valueChanged runs the
        # panel's slot, the document follows
        self.FW.pushed()
        reply = self.control(
            {"op": "widgets.update", "target": length, "state": {"q_rawValue": 25.0}}, 7
        )
        self.assertTrue(reply["ok"], reply)
        self.spin()
        self.FW.panelFlush()
        self.assertAlmostEqual(pad.Length.Value, 25.0)
        # the widget's formatted text came back to the writer (origin 0)
        msgs = [
            m for _, m in self.pushed(7) if m["id"] == length and m["method"] == "update"
        ]
        self.assertTrue(any("25" in m["content"].get("q_text", "") for m in msgs), msgs)
        self.assertIn("25", self.FW.snapshot(length)["state"]["q_text"])

        self.close_through_root(pid, "accept")
        self.doc.recompute()
        self.assertAlmostEqual(pad.Length.Value, 25.0)

    def test_draft_orthoarray(self):
        """Draft's Python panel over its .ui file, natively, unedited:
        the form's widgets by name and class, the grid by name, a spin
        box written from a client, reject through the root."""
        Gui = self.Gui
        box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        Gui.activateWorkbench("DraftWorkbench")
        self.spin()
        self.subscribe()
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(box)
        Gui.runCommand("Draft_OrthoArray")
        pid, root = self.open_panel()
        self.assertIn("TaskDialog", root["qtClass"], root["qtClass"])
        form = self.named("DraftOrthoArrayTaskPanel")
        self.assertIsNotNone(form, "the OrthoArray form was not mirrored")
        snap = self.FW.snapshot(form)
        self.assertEqual(snap["model"], "QWidgetModel")
        self.assertEqual(snap["layout"]["class"], "QGridLayout")
        self.assertEqual(snap["layout"]["name"], "gridLayout_3")
        for name, model, cls in (
            ("spinbox_n_X", "QSpinBoxModel", "QSpinBox"),
            ("input_X_x", "InputFieldModel", "Gui::InputField"),
            ("radiobutton_x_axis", "QRadioButtonModel", "QRadioButton"),
            ("group_X", "QGroupBoxModel", "QGroupBox"),
            ("button_reset_X", "QPushButtonModel", "QPushButton"),
        ):
            wid = self.named(name)
            self.assertIsNotNone(wid, name)
            s = self.FW.snapshot(wid)
            self.assertEqual(s["model"], model, name)
            self.assertEqual(s["qtClass"], cls, name)
        from PySide import QtWidgets

        spin = None
        for w in Gui.Control.activeTaskDialog().getDialogContent():
            spin = w.findChild(QtWidgets.QSpinBox, "spinbox_n_X")
            if spin is not None:
                break
        self.assertIsNotNone(spin)
        target = self.named("spinbox_n_X")
        self.assertEqual(self.FW.snapshot(target)["state"]["q_value"], spin.value())
        reply = self.control(
            {"op": "widgets.update", "target": target, "state": {"q_value": 4}}, 7
        )
        self.assertTrue(reply["ok"], reply)
        self.assertEqual(spin.value(), 4)
        self.close_through_root(pid, "reject")

    def test_cam_op_panel(self):
        """A CAM operation's panel: several .ui pages as boxes, the depths
        page's quantity fields, reject through the root."""
        Gui = self.Gui
        try:
            import Path.Main.Gui.Job as PathJobGui
            import Path.Op.Gui.Base as PathOpGui
            import Path.Op.Gui.Profile as ProfileGui
        except ImportError as e:
            self.skipTest("CAM is not available: %s" % e)
        Gui.activateWorkbench("CAMWorkbench")
        self.spin()
        body, pad = self.pad()
        job = PathJobGui.Create([body], None, openTaskPanel=False)
        self.doc.recompute()
        self.spin()
        if not job.Tools.Group:
            self.skipTest("the job has no tool controller")
        self.subscribe()
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(job)
        res = ProfileGui.Command.res
        res.job = job  # what the command's Activated takes from the selection
        op = PathOpGui.Create(res)
        self.assertIsNotNone(op, "the op was not created")
        pid, root = self.open_panel()
        self.assertIn("TaskDialog", root["qtClass"], root["qtClass"])
        boxes = self.boxes(root)
        self.assertGreaterEqual(len(boxes), 1, root)
        titles = [b["state"]["q_title"] for b in boxes]
        self.assertTrue(all(titles), titles)
        # the op's pages sit in a tab widget inside the box: walked as pages
        tabs = [
            self.FW.snapshot(i) for i in self.FW.ids()
            if i.startswith("pw:") and self.FW.snapshot(i)["model"] == "QTabWidgetModel"
        ]
        self.assertTrue(tabs, "no tab widget mirrored")
        self.assertGreaterEqual(len(tabs[0]["state"]["q_tabs"]), 3, tabs[0]["state"])
        self.assertGreaterEqual(len(tabs[0]["layout"]["items"]), 3)
        depth = self.named("startDepth")
        self.assertIsNotNone(depth, "no startDepth model")
        s = self.FW.snapshot(depth)
        self.assertEqual(s["model"], "QuantitySpinBoxModel")
        self.assertEqual(s["qtClass"], "Gui::QuantitySpinBox")
        self.assertTrue(s["state"]["q_text"])
        self.assertIsNotNone(self.named("finalDepth"))
        self.close_through_root(pid, "reject")
