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
writer, the dialog closed through the root.  M2: Sketcher's constraint
list reflected row by row, a client's check reaching the sketch; a
QSvgWidget as one picture by image id, re-sent on change only.  M3: a
QMessageBox exec'd from a panel slot arrives as a `dialog:<n>` root and
a client's button click is the exec code.  Needs
the GUI and no guest: run it through scripts/sandbox-gui-gate.py under
Xvfb.  Skips headless."""

import base64

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

        # the whole gate runs inside one slot of the main loop, so a
        # closed dialog's deleteLater (posted at this level) never runs
        # on its own: flush it, as the desktop's loop would, or a dead
        # panel's list keeps painting its gone sketch
        deferred = QtCore.QEvent.Type.DeferredDelete
        deferred = getattr(deferred, "value", deferred)
        for _ in range(times):
            QtWidgets.QApplication.processEvents()
            QtCore.QCoreApplication.sendPostedEvents(None, deferred)
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
        self.assertEqual(reply.get("dialogs"), [])
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

    def test_sketcher_constraints(self):
        """Sketcher's constraint list, reflected: the rows with their
        checks arrive in the list's open, a client's check write lands
        in the real item and reaches the sketch through the panel's own
        itemChanged slot (the constraint's virtual space), the refilled
        list comes back as ops."""
        Gui = self.Gui
        import TestSketcherApp

        Gui.activateWorkbench("SketcherWorkbench")
        self.spin()
        sketch = self.doc.addObject("Sketcher::SketchObject", "SketchC")
        TestSketcherApp.CreateRectangleSketch(sketch, (0, 0), (20, 10))
        self.doc.recompute()
        n = len(sketch.Constraints)
        self.assertGreater(n, 0)
        self.assertFalse(sketch.Constraints[0].InVirtualSpace)
        self.subscribe()
        self.assertTrue(Gui.ActiveDocument.setEdit(sketch, 0))
        pid, root = self.open_panel()
        wid = self.named("listWidgetConstraints")
        self.assertIsNotNone(wid, "the constraint list was not mirrored")
        snap = self.FW.snapshot(wid)
        self.assertEqual(snap["model"], "QListWidgetModel")
        self.assertEqual(snap["qtClass"], "SketcherGui::ConstraintView")
        rows = snap.get("items", [])
        self.assertEqual(len(rows), n, snap)
        self.assertTrue(all(r["cells"][0].get("text") for r in rows), rows)
        self.assertTrue(all("check" in r["cells"][0] for r in rows), rows)
        self.assertEqual(rows[0]["cells"][0]["check"], 2)  # Checked: shown, not virtual
        opens = [m for _, m in self.pushed(7) if m["method"] == "open" and m["id"] == wid]
        self.assertEqual(len(opens), 1, [m["id"] for _, m in self.pushed(7)])
        self.assertEqual(len(opens[0].get("items", [])), n)

        # a client's check: the real item's check state, the panel's slot,
        # the sketch
        self.FW.pushed()
        reply = self.control(
            {
                "op": "widgets.custom",
                "target": wid,
                "content": {"item": "set", "id": rows[0]["id"], "col": 0, "cell": {"check": 0}},
            },
            7,
        )
        self.assertTrue(reply["ok"], reply)
        self.spin()
        self.assertTrue(sketch.Constraints[0].InVirtualSpace)
        # the panel's own answer to the sketch's change (its list updated
        # in place or refilled) leaves the rows whole, the first one
        # unchecked, whatever ops it took; those ops, the desktop's facts
        # run by the client's own write, reached that client too
        rows = self.FW.snapshot(wid).get("items", [])
        self.assertEqual(len(rows), n)
        self.assertEqual(rows[0]["cells"][0]["check"], 0)
        for _, m in self.pushed(7):
            if m["id"] == wid and m["method"] == "custom":
                self.assertIn("item", m["content"], m)
        self.close_through_root(pid, "reject")

    def test_svg_picture(self):
        """A custom-painted leaf -- a QSvgWidget in a Python panel -- is a
        label model with the real class and an image id, the PNG fetched
        through widgets.image; a repaint with nothing changed sends
        nothing, a change sends one update with a new id."""
        Gui = self.Gui
        from PySide import QtCore

        try:
            from PySide6.QtSvgWidgets import QSvgWidget
        except ImportError as e:
            self.skipTest("no QSvgWidget: %s" % e)

        def svg(color):
            return QtCore.QByteArray(
                (
                    '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">'
                    '<rect width="64" height="64" fill="%s"/></svg>' % color
                ).encode()
            )

        widget = QSvgWidget()
        widget.setObjectName("svgLeaf")
        widget.setWindowTitle("Picture")
        widget.load(svg("#ff0000"))
        widget.setFixedSize(64, 64)

        class Panel:
            def __init__(self, form):
                self.form = form

        self.subscribe()
        Gui.Control.showDialog(Panel(widget))
        pid, root = self.open_panel()
        wid = self.named("svgLeaf")
        self.assertIsNotNone(wid, "the svg widget was not mirrored")
        snap = self.FW.snapshot(wid)
        self.assertEqual(snap["model"], "QLabelModel")
        self.assertEqual(snap["qtClass"], "QSvgWidget")
        pix = snap["state"].get("q_pixmap", "")
        self.assertTrue(pix.startswith("img:"), snap["state"])
        reply = self.control({"op": "widgets.image", "name": pix}, 7)
        self.assertTrue(reply["ok"], reply)
        self.assertEqual(reply["format"], "png")
        self.assertEqual(base64.b64decode(reply["data"])[:4], b"\x89PNG")
        self.assertEqual((reply["width"], reply["height"]), (64, 64))
        reply = self.control({"op": "widgets.image", "name": "img:nope"}, 7)
        self.assertFalse(reply["ok"], reply)

        def pixmap_updates():
            return [
                m["content"]["q_pixmap"]
                for _, m in self.pushed(7)
                if m["id"] == wid and m["method"] == "update" and "q_pixmap" in m["content"]
            ]

        # past the rate cap, then a repaint with nothing changed
        self.spin(60, 3)
        self.FW.pushed()
        widget.update()
        self.spin(60, 4)
        self.FW.panelFlush()
        self.assertEqual(pixmap_updates(), [])
        self.assertEqual(self.FW.snapshot(wid)["state"]["q_pixmap"], pix)
        # a change: one update, a new id, fetchable
        widget.load(svg("#0000ff"))
        self.spin(60, 4)
        self.FW.panelFlush()
        ups = pixmap_updates()
        self.assertEqual(len(ups), 1, ups)
        self.assertNotEqual(ups[0], pix)
        reply = self.control({"op": "widgets.image", "name": ups[0]}, 7)
        self.assertTrue(reply["ok"], reply)
        Gui.Control.closeDialog()
        self.spin()
        self.assertIsNone(self.FW.panelId())

    def test_nested_messagebox(self):
        """A QMessageBox exec'd from a panel slot (M3): while the slot
        blocks in the nested loop the box arrives as a `dialog:<n>` root
        with the real class, its text as Qt's own label, its buttons by
        flag; a client's Yes through the root returns the exec code to
        the slot, the root's close reaches the writer, the panel stays."""
        Gui = self.Gui
        from PySide import QtCore, QtWidgets

        form = QtWidgets.QWidget()
        form.setObjectName("askForm")
        form.setWindowTitle("Ask")
        lay = QtWidgets.QVBoxLayout(form)
        button = QtWidgets.QPushButton("Ask", form)
        button.setObjectName("askButton")
        lay.addWidget(button)
        result = {}
        yes = 0x4000  # QMessageBox.Yes

        def ask():
            box = QtWidgets.QMessageBox(
                QtWidgets.QMessageBox.Icon.Question,
                "Really",
                "Proceed?",
                QtWidgets.QMessageBox.StandardButton.Yes | QtWidgets.QMessageBox.StandardButton.No,
                Gui.getMainWindow(),
            )
            box.setObjectName("askBox")
            result["code"] = box.exec()

        button.clicked.connect(ask)

        class Panel:
            def __init__(self, form):
                self.form = form

        self.subscribe()
        Gui.Control.showDialog(Panel(form))
        pid, root = self.open_panel()
        bid = self.named("askButton")
        self.assertIsNotNone(bid)
        seen = {}

        def probe():
            # inside the slot's exec loop: the walk's tick has run
            try:
                self.spin(30, 3)
                ids = self.FW.dialogIds()
                seen["ids"] = list(ids)
                seen["active"] = self.FW.panelId()
                if ids:
                    snap = self.FW.snapshot(ids[0])
                    seen["root"] = snap
                    label = self.named("qt_msgbox_label")
                    seen["label"] = self.FW.snapshot(label) if label else None
                    seen["opens"] = [
                        m["id"] for _, m in self.pushed(7) if m["method"] == "open"
                    ]
                    seen["buttons"] = sorted(
                        self.FW.snapshot(i)["state"]["q_standardButton"]
                        for i in self.FW.ids()
                        if i.startswith("pw:")
                        and self.FW.snapshot(i)["model"] == "QPushButtonModel"
                        and self.FW.snapshot(i)["state"].get("q_standardButton")
                    )
                    self.FW.pushed()
                    seen["reply"] = self.control(
                        {
                            "op": "widgets.custom",
                            "target": ids[0],
                            "content": {"event": "clicked", "args": [yes]},
                        },
                        7,
                    )
            except Exception as e:  # reported after the slot returns
                seen["error"] = repr(e)

        QtCore.QTimer.singleShot(80, probe)
        self.FW.pushed()
        reply = self.control(
            {"op": "widgets.custom", "target": bid, "content": {"event": "click"}}, 7
        )
        self.assertTrue(reply["ok"], reply)
        self.spin()
        self.assertNotIn("error", seen, seen)
        self.assertEqual(result.get("code"), yes)
        self.assertEqual(len(seen.get("ids", [])), 1, seen)
        did = seen["ids"][0]
        self.assertTrue(did.startswith("dialog:"), did)
        self.assertEqual(seen["active"], pid)
        self.assertEqual(seen["root"]["model"], "QDialogModel")
        self.assertEqual(seen["root"]["qtClass"], "QMessageBox")
        self.assertEqual(seen["root"]["state"]["q_windowTitle"], "Really")
        self.assertTrue(seen["root"]["state"]["q_modal"])
        self.assertEqual(seen["root"]["parent"], "IPY_MODEL_panel")
        self.assertIsNotNone(seen["label"], seen)
        self.assertEqual(seen["label"]["state"]["q_text"], "Proceed?")
        self.assertIn(yes, seen["buttons"])
        self.assertIn(0x10000, seen["buttons"])  # No
        self.assertEqual(seen["opens"][-1], did, seen["opens"])
        self.assertTrue(seen["reply"]["ok"], seen["reply"])
        # the close reached the writer, the box's widgets are gone, the
        # panel is still up
        closes = [m["id"] for _, m in self.pushed(7) if m["method"] == "close"]
        self.assertIn(did, closes)
        self.assertNotIn(pid, closes)
        self.assertEqual(self.FW.dialogIds(), [])
        self.assertFalse([i for i in self.FW.ids() if i.startswith("dialog:")])
        self.assertIsNone(self.named("qt_msgbox_label"))
        self.assertEqual(self.FW.panelId(), pid)
        self.assertIsNotNone(self.named("askButton"))
        self.close_through_root(pid, "reject")
