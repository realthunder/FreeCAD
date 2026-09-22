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
import os
import unittest

import FreeCAD

REF = "IPY_MODEL_"


def _ref(ref):
    return ref[len(REF):] if isinstance(ref, str) and ref.startswith(REF) else None


class _Recorder:
    """Records the wire for the browser gate (docs/Sandbox.md 7.22).

    Every frame `pushed()` hands back and every `widgets.subscribe`
    reply, kept so the DOM walker can be gated on a replay of what the
    desktop really sent rather than on a hand-written sample.  A proxy
    rather than a patch: the gate binds `self.FW` once, so nothing else
    here changes and no module attribute is touched.  Off unless
    SANDBOX_PANEL_FIXTURES names a directory.
    """

    def __init__(self, fw):
        self._fw = fw
        self.frames = []
        self.replies = []

    def __getattr__(self, name):
        return getattr(self._fw, name)

    def pushed(self):
        out = self._fw.pushed()
        for entry in out:
            self.frames.append(
                {"client": entry["client"], "frame": json.loads(entry["json"])}
            )
        return out

    def control(self, payload, client):
        reply = self._fw.control(payload, client)
        try:
            if json.loads(payload).get("op") == "widgets.subscribe":
                self.replies.append(json.loads(reply))
        except (ValueError, TypeError):
            pass
        return reply


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
        # after the drains, so a fixture carries this case only
        self.fixtureDir = os.environ.get("SANDBOX_PANEL_FIXTURES")
        if self.fixtureDir:
            self.FW = _Recorder(Gui.FormWidgets)

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
            self.writeFixture()
            FreeCAD.closeDocument(self.doc.Name)

    def writeFixture(self):
        """The case's frames, for the browser gate's replay."""
        if not getattr(self, "fixtureDir", None) or not isinstance(self.FW, _Recorder):
            return
        os.makedirs(self.fixtureDir, exist_ok=True)
        name = self.id().rsplit(".", 1)[-1]
        if name.startswith("test_"):
            name = name[len("test_"):]
        path = os.path.join(self.fixtureDir, name + ".json")
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(
                {
                    "case": name,
                    "subscribeReplies": self.FW.replies,
                    "frames": self.FW.frames,
                },
                handle,
                indent=1,
                sort_keys=True,
            )
        FreeCAD.Console.PrintMessage(
            "panel fixture: %s (%d frames)\n" % (path, len(self.FW.frames))
        )

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

    def test_file_chooser(self):
        """A Gui::FileChooser in a panel (W4b): a LEAF carrying its path
        and filter as data -- the walk never goes into it, so the browse
        button that raises the host's own QFileDialog is not a model any
        client can click -- an upload landing where the HOST chose, a
        name that is a name and not a path, and a pick through
        `fileSelected`, which is where the desktop's own pick ends."""
        Gui = self.Gui
        from PySide import QtWidgets

        form = QtWidgets.QWidget()
        form.setObjectName("chooserForm")
        form.setWindowTitle("Pick")
        lay = QtWidgets.QVBoxLayout(form)
        try:
            chooser = Gui.UiLoader().createWidget("Gui::FileChooser", form)
        except Exception as e:  # no loader, no custom widget: nothing to gate
            self.skipTest("no Gui::FileChooser from the loader: %r" % e)
        if chooser is None:
            self.skipTest("no Gui::FileChooser from the loader")
        chooser.setObjectName("fontFile")
        chooser.setProperty("filter", "Fonts (*.ttf)")
        chooser.setProperty("fileName", "/tmp/before.ttf")
        lay.addWidget(chooser)

        class Panel:
            def __init__(self, form):
                self.form = form

        self.subscribe()
        Gui.Control.showDialog(Panel(form))
        pid, root = self.open_panel()
        cid = self.named("fontFile")
        self.assertIsNotNone(cid, "the file chooser was not mirrored")
        snap = self.FW.snapshot(cid)
        self.assertEqual(snap["model"], "FileChooserModel")
        self.assertEqual(snap["qtClass"], "Gui::FileChooser")
        self.assertEqual(snap["state"].get("q_fileName"), "/tmp/before.ttf")
        self.assertEqual(snap["state"].get("q_filter"), "Fonts (*.ttf)")
        # A leaf: a container's model gets a layout built from its real
        # children, and this one has none -- which is what keeps the
        # chooser's "..." button off the wire entirely.
        self.assertFalse(snap.get("layout"))

        # The upload. The client names a FILE, never a path, and the host
        # decides where it lands.
        payload = b"\x00\x01 a font, near enough"
        reply = self.control(
            {
                "op": "widgets.upload",
                "name": "my font.ttf",
                "data": base64.b64encode(payload).decode(),
            },
            7,
        )
        self.assertTrue(reply["ok"], reply)
        path = reply["path"]
        self.assertTrue(os.path.isfile(path), path)
        with open(path, "rb") as handle:
            self.assertEqual(handle.read(), payload)
        # DERIVED from the name, not equal to it. The upload directory is
        # the host's own and it persists, so a second run of this gate
        # meets "my font.ttf" already sitting there and is handed
        # "my font-1.ttf" -- the no-overwrite rule working. Asserting the
        # basename outright passed once here and failed for ever after.
        # The sanitized name the host echoes back IS exact, so that is
        # what pins the sanitizing.
        base = os.path.basename(path)
        self.assertTrue(base.startswith("my font") and base.endswith(".ttf"), base)
        self.assertEqual(reply["name"], "my font.ttf")

        # A path in the name is not a path: it is reduced to a name, and
        # lands in the same directory as anything else.
        reply = self.control({"op": "widgets.upload", "name": "../../escape.ttf", "data": ""}, 7)
        self.assertTrue(reply["ok"], reply)
        escaped = os.path.basename(reply["path"])
        self.assertTrue(escaped.startswith("escape") and escaped.endswith(".ttf"), escaped)
        self.assertEqual(reply["name"], "escape.ttf")
        self.assertEqual(os.path.dirname(reply["path"]), os.path.dirname(path))
        # The same name twice does not overwrite what a panel may still
        # be pointing at.
        reply = self.control(
            {
                "op": "widgets.upload",
                "name": "my font.ttf",
                "data": base64.b64encode(b"other").decode(),
            },
            7,
        )
        self.assertTrue(reply["ok"], reply)
        self.assertNotEqual(reply["path"], path)
        with open(path, "rb") as handle:
            self.assertEqual(handle.read(), payload)
        # A nameless upload, and one that is not base64, are refused.
        self.assertFalse(self.control({"op": "widgets.upload", "name": "", "data": ""}, 7)["ok"])
        self.assertFalse(
            self.control({"op": "widgets.upload", "name": "x.ttf", "data": "!!!!"}, 7)["ok"]
        )

        # The pick. A `q_fileName` write would move the line edit and fire
        # fileNameChanged only; a panel's slot is connected to
        # fileNameSelected, so the request is what a client sends.
        reply = self.control(
            {
                "op": "widgets.custom",
                "target": cid,
                "content": {"event": "fileSelected", "args": [path]},
            },
            7,
        )
        self.assertTrue(reply["ok"], reply)
        self.spin()
        self.assertEqual(chooser.property("fileName"), path)
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

    def test_reject_dialog_through_its_root(self):
        """A mirrored dialog REJECTED through its own root (W5).

        Named to sort AFTER test_draft_orthoarray, which is not tidiness:
        Draft's command does not raise its panel in a run where a nested
        QMessageBox case precedes it, and this case is the only one that
        did.  Four runs pin it -- without this case the suite is 7/7; with
        it after Draft's, 8/8; with it before Draft's, Draft fails whether
        this case REJECTS the box or answers it with a button, so it is
        not the reject.  The mirror is innocent: at the failure
        Control.activeTaskDialog() is None and no modal widget is up, so
        no task dialog opened on the desktop at all.  test_nested_messagebox
        has always sat after Draft's and so never showed it.  The cause is
        in Draft's command and is written down in docs/Sandbox.md 7.22
        rather than guessed at here.

        The regression test for a crash, and it lives here rather than in
        the browser gate because the whole failure is host-side: `reject`
        reaches the real QDialog, its `reject()` ends the nested exec()
        loop, the Hide that follows closes the root, and `hide()` deletes
        the root's models -- including the very Widget whose `request()`
        is still on the stack.  The `Q_EMIT requested` after the backend
        call then ran on freed memory, and the first drive ever to press
        Escape on a mirrored dialog took the desktop down with it: SIGSEGV
        in QObjectPrivate::maybeSignalConnected, one frame under
        Gui::Fw::Widget::requested (docs/Sandbox.md 7.22).

        So the first thing this asserts is that the process is STILL HERE
        afterwards.  The exec code says the rest: 0 is QDialog::reject()'s
        own answer -- no button -- which is the desktop really rejecting
        rather than a client closing a layer of its own.
        """
        Gui = self.Gui
        from PySide import QtCore, QtWidgets

        form = QtWidgets.QWidget()
        form.setObjectName("askForm")
        lay = QtWidgets.QVBoxLayout(form)
        button = QtWidgets.QPushButton("Ask", form)
        button.setObjectName("askButton")
        lay.addWidget(button)
        result = {}

        def ask():
            box = QtWidgets.QMessageBox(
                QtWidgets.QMessageBox.Icon.Question,
                "Really",
                "Proceed?",
                QtWidgets.QMessageBox.StandardButton.Yes | QtWidgets.QMessageBox.StandardButton.No,
                Gui.getMainWindow(),
            )
            box.setObjectName("askBox")
            result["box"] = box
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
            # inside the slot's exec loop, where the box is up
            try:
                self.spin(30, 3)
                ids = self.FW.dialogIds()
                seen["ids"] = list(ids)
                if ids:
                    self.FW.pushed()
                    seen["reply"] = self.control(
                        {
                            "op": "widgets.custom",
                            "target": ids[0],
                            "content": {"event": "reject"},
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
        self.assertEqual(len(seen.get("ids", [])), 1, seen)
        did = seen["ids"][0]
        self.assertTrue(seen["reply"]["ok"], seen["reply"])
        # QDialog::reject() -- no button answered it
        self.assertEqual(result.get("code"), 0, result)
        # the dialog went, the panel stayed
        closes = [m["id"] for _, m in self.pushed(7) if m["method"] == "close"]
        self.assertIn(did, closes)
        self.assertEqual(self.FW.dialogIds(), [])
        self.assertEqual(self.FW.panelId(), pid)
        # The box itself is a CHILD of the main window, so rejecting it
        # hides it and nothing more: it outlives this test, and the next
        # one to open a panel found none mirrored.  An answered box (see
        # test_nested_messagebox) gets away with it only because nothing
        # follows it here.
        result["box"].deleteLater()
        self.spin()
        self.close_through_root(pid, "reject")
        # And the mirror still works AFTERWARDS, which is the half a reject
        # could plausibly break.  Asserted here rather than left for the next
        # test to trip over: adding this case made test_draft_orthoarray fail
        # with "no panel mirrored", and a failure that lands in someone else's
        # test is one nobody can read.
        again = QtWidgets.QWidget()
        again.setObjectName("afterForm")
        QtWidgets.QVBoxLayout(again).addWidget(QtWidgets.QLabel("after", again))
        Gui.Control.showDialog(Panel(again))
        pid2, _root2 = self.open_panel()
        self.assertTrue(pid2, "the mirror did not take a panel after a dialog reject")
        self.close_through_root(pid2, "reject")
