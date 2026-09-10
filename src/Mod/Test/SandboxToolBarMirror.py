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
"""The tool bar mirror (docs/Sandbox.md 7.18): the desktop's native
tool bars as `QToolBar` / `QAction` models in the widget store, the
store's fan-out signal, and the widget stream over the scene control
channel with an injected sender.  Needs the GUI and no guest: run it
through scripts/sandbox-gui-gate.py under Xvfb.  Skips headless."""

import json
import unittest

import FreeCAD

REF = "IPY_MODEL_"


def _ref(ref):
    return ref[len(REF):] if isinstance(ref, str) and ref.startswith(REF) else None


class SandboxToolBarMirrorTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        import FreeCADGui as Gui

        self.Gui = Gui
        self.FW = Gui.FormWidgets
        self.previous = Gui.activeWorkbench().name()
        self.doc = FreeCAD.newDocument("SandboxToolBarMirror")
        self.FW.watchMessages()
        self.FW.messages()
        self.FW.pushed()
        if Gui.activeWorkbench().name() != "DraftWorkbench":
            Gui.activateWorkbench("DraftWorkbench")
        self.spin()
        self.assertTrue(self.FW.mirrorToolBars(True))
        self.spin()
        self.FW.messages()

    def tearDown(self):
        try:
            for client in (7, 8):
                self.control({"op": "widgets.unsubscribe"}, client)
            self.FW.mirrorToolBars(False)
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

    def bar_items(self, bar_id):
        snap = self.FW.snapshot(bar_id)
        self.assertTrue(snap, bar_id)
        return snap["layout"]["items"]

    def order(self):
        return [_ref(item["widget"]) for item in self.bar_items("toolbars")]

    def test_structure(self):
        """Draft's bars as models: the order object, a bar's state and
        layout, a command action, a separator, a widget action."""
        order = self.order()
        self.assertIn("toolbar:Draft Creation", order)
        self.assertIn("toolbar:View", order)
        # the order is by area, then geometry: the top bars first
        areas = [self.FW.snapshot(i)["state"]["q_area"] for i in order]
        self.assertEqual(areas[0], "top", areas)
        seen_other = False
        for a in areas:
            if a != "top":
                seen_other = True
            elif seen_other:
                self.fail("a top bar after another area: %s" % areas)
        snap = self.FW.snapshot("toolbar:Draft Creation")
        self.assertEqual(snap["model"], "QToolBarModel")
        st = snap["state"]
        self.assertEqual(st["q_objectName"], "Draft Creation")
        self.assertTrue(st["q_windowTitle"])
        self.assertTrue(st["q_visible"])
        self.assertEqual(st["q_area"], "top")
        ids = [_ref(item.get("action")) for item in snap["layout"]["items"]]
        self.assertIn("cmd:Draft_Line", ids)
        line = self.FW.snapshot("cmd:Draft_Line")
        self.assertEqual(line["model"], "QActionModel")
        st = line["state"]
        self.assertEqual(st["q_command"], "Draft_Line")
        self.assertEqual(st["q_commandIndex"], 0)
        self.assertTrue(st["q_icon"], st)
        self.assertTrue(st["q_text"])
        self.assertTrue(st["q_toolTip"])
        self.assertNotIn("<", st["q_toolTip"])
        self.assertIn("q_shortcut", st)
        self.assertFalse(st["q_separator"])
        # the referenced before the referrer
        full = self.FW.snapshotOrder()
        self.assertLess(full.index("cmd:Draft_Line"), full.index("toolbar:Draft Creation"))
        self.assertEqual(full[-1], "toolbars")
        # a separator in the View bar, and the workbench chooser as a
        # widget item somewhere
        view = self.bar_items("toolbar:View")
        self.assertTrue(any(item.get("separator") for item in view), view)
        widgets = [
            _ref(item["widget"]) for bar in order for item in self.bar_items(bar)
            if "widget" in item
        ]
        self.assertTrue(widgets, "no widget action mirrored")
        w = self.FW.snapshot(widgets[0])
        self.assertEqual(w["model"], "QWidgetModel")
        self.assertTrue(w["qtClass"])
        self.assertTrue(widgets[0].startswith("widget:"))

    def test_group(self):
        """A group command: its members, the default, and a member's
        trigger from a client moving the default the desktop's way."""
        snap = self.FW.snapshot("cmd:Std_DrawStyle")
        st = snap["state"]
        members = [_ref(r) for r in st["q_members"]]
        self.assertGreater(len(members), 2, st)
        self.assertEqual(members[0], "cmd:Std_DrawStyle#1")
        self.assertTrue(st["q_exclusive"])
        self.assertTrue(st["q_dropDown"])
        default = st["q_defaultAction"]
        self.assertGreaterEqual(default, 0)
        # the members are real models, checkable (an exclusive set)
        checkable = [
            m for m in members
            if self.FW.snapshot(m)["state"]["q_checkable"]
            and not self.FW.snapshot(m)["state"]["q_separator"]
        ]
        self.assertGreater(len(checkable), 1)
        member = self.FW.snapshot(checkable[1])["state"]
        self.assertEqual(member["q_command"], "Std_DrawStyle")
        self.assertGreater(member["q_commandIndex"], 0)
        self.assertTrue(member["q_memberCommand"])
        target = checkable[1] if members[default] != checkable[1] else checkable[0]
        k = int(target.rsplit("#", 1)[1])
        self.FW.messages()
        reply = self.control(
            {"op": "widgets.custom", "target": target, "content": {"event": "trigger"}}, 7
        )
        self.assertTrue(reply["ok"], reply)
        self.spin()
        self.FW.mirrorFlush()
        st = self.FW.snapshot("cmd:Std_DrawStyle")["state"]
        self.assertEqual(st["q_defaultAction"], k - 1)
        self.assertTrue(self.FW.snapshot(target)["state"]["q_checked"])
        msgs = self.FW.messages()
        self.assertTrue(
            any(
                m["id"] == "cmd:Std_DrawStyle" and "q_defaultAction" in m["content"]
                for m in msgs
            ),
            msgs,
        )
        # back to where the desktop was
        self.control(
            {"op": "widgets.custom", "target": members[default],
             "content": {"event": "trigger"}}, 7
        )
        self.spin()
        self.FW.mirrorFlush()
        self.assertEqual(self.FW.snapshot("cmd:Std_DrawStyle")["state"]["q_defaultAction"],
                         default)

    def test_coalescing(self):
        """A selection change: one update per action whose state
        changed, nothing for the rest, and a write of the same value
        sends nothing."""
        box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        self.spin(60, 6)
        self.FW.mirrorFlush()
        self.FW.messages()
        before = self.FW.snapshot("cmd:Std_Copy")["state"]["q_enabled"]
        self.Gui.Selection.addSelection(box)
        self.spin(60, 6)
        self.FW.mirrorFlush()
        msgs = [m for m in self.FW.messages() if m["method"] == "update"]
        self.assertTrue(msgs)
        # every update changed something, and no key is sent twice with
        # the same value in a row
        last = {}
        for m in msgs:
            for key, value in m["content"].items():
                self.assertNotEqual(last.get((m["id"], key), object()), value, (m, key))
                last[(m["id"], key)] = value
        after = self.FW.snapshot("cmd:Std_Copy")["state"]["q_enabled"]
        self.assertNotEqual(before, after)
        self.assertTrue(any(m["id"] == "cmd:Std_Copy" for m in msgs))
        # the same value written from a client: the write itself is
        # echoed under the client's origin (a write is a write, and the
        # stream skips the writer), and nothing comes back from the
        # real action
        self.FW.messages()
        self.control({"op": "widgets.update", "target": "cmd:Std_Copy",
                      "state": {"q_enabled": after}}, 7)
        self.spin()
        self.FW.mirrorFlush()
        msgs = [m for m in self.FW.messages() if m["id"] == "cmd:Std_Copy"]
        self.assertEqual([m for m in msgs if m["origin"] != 7], [], msgs)
        self.assertLessEqual(len(msgs), 1, msgs)
        self.Gui.Selection.clearSelection()

    def test_transport(self):
        """The stream: subscribe, the snapshot after the reply, a live
        change as one op, an icon by name, unsubscribe."""
        reply = self.control({"op": "widgets.subscribe", "toolbars": True}, 7)
        self.assertTrue(reply["ok"], reply)
        self.assertIn("theme", reply)
        self.assertIn("locale", reply)
        self.assertEqual(self.pushed(7), [])
        self.spin()
        opens = [m for _, m in self.pushed(7)]
        self.assertTrue(opens)
        self.assertTrue(all(m["op"] == "widgets" and m["method"] == "open" for m in opens))
        ids = [m["id"] for m in opens]
        self.assertEqual(ids[-1], "toolbars")
        self.assertLess(ids.index("cmd:Draft_Line"), ids.index("toolbar:Draft Creation"))
        bar = [m for m in opens if m["id"] == "toolbar:Draft Creation"][0]
        self.assertEqual(bar["model"], "QToolBarModel")
        self.assertIn("layout", bar)
        # a live change: hide a bar -- one bar update and one order update
        from PySide import QtWidgets

        tb = self.Gui.getMainWindow().findChild(QtWidgets.QToolBar, "Draft Creation")
        self.assertIsNotNone(tb)
        try:
            tb.hide()
            self.spin()
            self.FW.mirrorFlush()
            msgs = [m for _, m in self.pushed(7)]
            hidden = [m for m in msgs if m["id"] == "toolbar:Draft Creation"]
            self.assertTrue(hidden, msgs)
            self.assertEqual(hidden[-1]["content"]["q_visible"], False)
            self.assertLessEqual(len([m for m in msgs if m["id"] == "toolbars"]), 2)
            self.assertFalse([m for m in msgs if m["method"] in ("open", "close")], msgs)
        finally:
            tb.show()
            self.spin()
        # the icon of a command, by the name the stream carries
        name = self.FW.snapshot("cmd:Draft_Line")["state"]["q_icon"]
        reply = self.control({"op": "widgets.icon", "name": name, "size": 24}, 7)
        self.assertTrue(reply["ok"], reply)
        self.assertIn(reply["format"], ("svg", "png"))
        self.assertTrue(reply["data"])
        if reply["format"] == "svg":
            self.assertIn("<svg", reply["data"])
        reply = self.control({"op": "widgets.icon", "name": "no-such-icon-anywhere"}, 7)
        self.assertFalse(reply["ok"])
        self.assertEqual(reply["code"], "UnknownIcon")
        # gone
        reply = self.control({"op": "widgets.unsubscribe"}, 7)
        self.assertTrue(reply["ok"])
        self.pushed()
        tb.hide()
        self.spin()
        tb.show()
        self.spin()
        self.assertEqual(self.pushed(7), [])

    def test_workbench_switch(self):
        """A switch: the bars that went are closed, the new ones opened,
        the order sent once, and a shared command's id untouched."""
        reply = self.control({"op": "widgets.subscribe", "toolbars": True}, 8)
        self.assertTrue(reply["ok"], reply)
        self.spin()
        self.pushed()
        self.Gui.activateWorkbench("PartWorkbench")
        self.spin(60, 6)
        self.FW.mirrorFlush()
        msgs = [m for _, m in self.pushed(8)]
        closed = [m["id"] for m in msgs if m["method"] == "close"]
        opened = [m["id"] for m in msgs if m["method"] == "open"]
        self.assertIn("toolbar:Draft Creation", closed, msgs)
        self.assertTrue(any(i.startswith("toolbar:Part") for i in opened), opened)
        self.assertNotIn("cmd:Std_Copy", closed)
        self.assertNotIn("cmd:Std_Copy", opened)
        orders = [m for m in msgs if m["id"] == "toolbars" and m["method"] == "update"]
        self.assertGreaterEqual(len(orders), 1)
        self.assertLessEqual(len(orders), 2, orders)
        self.assertIn("toolbar:View", [_ref(i["widget"]) for i in
                                       orders[-1]["content"]["layoutSpec"]["items"]])
        self.Gui.activateWorkbench("DraftWorkbench")
        self.spin(60, 6)
