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
"""The mirrors measured (docs/Sandbox.md 8.4, 7.19 M4): what the panel
mirror's watch costs the host per repaint burst -- widgets re-read, keys
read through the meta-object, keys that differed and went out, the time
in the reads and in the writes -- and what a panel puts on the wire at
rest, under a repaint with nothing changed, and while a client types;
Pad's C++ panel, Draft's OrthoArray (.ui over Python), Sketcher's
constraint list (rows reflected, a check toggled), and the tool bar
mirror (7.18) as the baseline: its open, its rest, the selection
timer, a workbench switch.

A measurement, not a gate: it is NOT in scripts/sandbox-gui-gate.py's
default list.  Run it by hand under Xvfb:

    SANDBOX_GUI_GATE_MODULES=SandboxMirrorBench ... sandbox-gui-gate.py

The rows go to stderr as a table and to $SANDBOX_MIRROR_BENCH (default
mirror-bench.json beside the gate's result file).  The few assertions
are the properties the numbers rest on: a repaint with nothing changed
writes no key and sends no byte; a panel at rest sends nothing.  Needs
the GUI and no guest.  Skips headless."""

import json
import os
import sys
import time
import unittest

import FreeCAD

REF = "IPY_MODEL_"
COLUMNS = (
    ("scenario", 12),
    ("phase", 14),
    ("s", 6),
    ("msgs", 6),
    ("bytes", 8),
    ("B/s", 8),
    ("toWrtr", 7),
    ("flush", 6),
    ("wRead", 6),
    ("kRead", 6),
    ("kWrit", 6),
    ("readUs", 7),
    ("writeUs", 7),
    ("grabs", 5),
    ("grabUs", 7),
    ("walkUs", 7),
    ("models", 6),
)


def _ref(ref):
    return ref[len(REF):] if isinstance(ref, str) and ref.startswith(REF) else None


def _result_path():
    path = os.environ.get("SANDBOX_MIRROR_BENCH")
    if path:
        return path
    return os.path.join(FreeCAD.getUserAppDataDir(), "mirror-bench.json")


def _log(line):
    sys.__stderr__.write(line + "\n")
    sys.__stderr__.flush()


class SandboxMirrorBenchTest(unittest.TestCase):
    rows = []

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
        self.doc = FreeCAD.newDocument("SandboxMirrorBench")
        # the Gui document is made on the event loop
        for _ in range(20):
            if Gui.ActiveDocument:
                break
            self.spin(20, 1)
        self.assertIsNotNone(Gui.ActiveDocument)
        self.FW.watchMessages()
        self.FW.messages()
        self.FW.pushed()
        self.scenario = self.id().rsplit(".", 1)[-1].replace("test_", "")

    def tearDown(self):
        try:
            for client in (7, 8):
                self.control({"op": "widgets.unsubscribe"}, client)
            if self.Gui.Control.activeDialog():
                self.Gui.Control.closeDialog()
                self.spin()
            self.FW.mirrorPanels(False)
            self.FW.mirrorToolBars(False)
            self.FW.messages()
            self.FW.pushed()
            self.spin()
            if self.Gui.activeWorkbench().name() != self.previous:
                self.Gui.activateWorkbench(self.previous)
                self.spin()
            self.report()
        finally:
            FreeCAD.closeDocument(self.doc.Name)

    # ---- the harness -------------------------------------------------------

    @staticmethod
    def spin(ms=30, times=4):
        from PySide import QtCore, QtWidgets

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
        """[(client, message, wire bytes)] since the last call."""
        out = []
        for entry in self.FW.pushed():
            if client is None or entry["client"] == client:
                size = len(entry["json"].encode())
                out.append((entry["client"], json.loads(entry["json"]), size))
        return out

    def named(self, name):
        for wid in self.FW.ids():
            if wid.startswith("pw:") and self.FW.snapshot(wid)["state"].get("q_objectName") == name:
                return wid
        return None

    def subscribe(self):
        """Two panel subscribers: 7 writes, 8 only watches -- the wire a
        write costs is what the OTHER clients get (the store skips the
        writer), so `bytes` is 8's and `toWrtr` 7's."""
        for client in (7, 8):
            reply = self.control({"op": "widgets.subscribe", "panels": True}, client)
            self.assertTrue(reply["ok"], reply)
        self.spin()
        self.FW.pushed()
        self.FW.panelStats(True)

    def open_panel(self):
        self.spin(60, 6)
        pid = self.FW.panelId()
        self.assertTrue(pid, "no panel mirrored")
        return pid

    def real_contents(self):
        dialog = self.Gui.Control.activeTaskDialog()
        return list(dialog.getDialogContent()) if dialog else []

    def measure(self, phase, action=None, hold=0.0, client=8, panel=True):
        """Run `action`, hold for `hold` seconds of event loop, flush,
        and record what the phase cost: the wire (that client's pushed
        messages; the writer's own, client 7, apart) and the mirror's
        counters (delta)."""
        self.FW.pushed()
        if panel:
            self.FW.panelStats(True)
        t0 = time.perf_counter()
        if action:
            action()
        deadline = t0 + hold
        while time.perf_counter() < deadline:
            self.spin(20, 1)
        self.spin(30, 3)
        if panel:
            self.FW.panelFlush()
        else:
            self.FW.mirrorFlush()
        seconds = time.perf_counter() - t0
        every = self.pushed()
        msgs = [e for e in every if e[0] == client]
        writer = sum(n for c, _, n in every if c == 7 and client != 7)
        stats = self.FW.panelStats(True) if panel else {}
        methods = {}
        for _, m, _ in msgs:
            key = m.get("method", "?")
            if key == "custom" and isinstance(m.get("content"), dict) and "item" in m["content"]:
                key = "item:" + str(m["content"]["item"])
            methods[key] = methods.get(key, 0) + 1
        nbytes = sum(n for _, _, n in msgs)
        row = {
            "scenario": self.scenario,
            "phase": phase,
            "s": round(seconds, 3),
            "msgs": len(msgs),
            "bytes": nbytes,
            "B/s": int(nbytes / seconds) if seconds > 0 else 0,
            "toWrtr": writer,
            "methods": methods,
        }
        for key, name in (
            ("flushes", "flush"),
            ("widgetsRead", "wRead"),
            ("keysRead", "kRead"),
            ("keysWritten", "kWrit"),
            ("readUs", "readUs"),
            ("writeUs", "writeUs"),
            ("grabs", "grabs"),
            ("grabUs", "grabUs"),
            ("walkUs", "walkUs"),
            ("models", "models"),
        ):
            if key in stats:
                row[name] = stats[key]
        self.rows.append(row)
        _log(self.format_row(row))
        return row, msgs

    @classmethod
    def format_row(cls, row):
        parts = []
        for name, width in COLUMNS:
            value = row.get(name, "")
            parts.append(str(value).rjust(width) if name not in ("scenario", "phase")
                         else str(value).ljust(width))
        return " ".join(parts)

    @classmethod
    def header(cls):
        return cls.format_row({name: name for name, _ in COLUMNS})

    def report(self):
        path = _result_path()
        try:
            with open(path) as f:
                rows = json.load(f)
        except (OSError, ValueError):
            rows = []
        rows = [r for r in rows if r["scenario"] != self.scenario]
        rows.extend(r for r in self.rows if r["scenario"] == self.scenario)
        with open(path, "w") as f:
            json.dump(rows, f, indent=1)

    def repaint_all(self):
        """Every content widget and its children repaint with nothing
        changed: what a hover, a focus change or a scroll costs the watch."""
        for w in self.real_contents():
            w.update()  # the children paint with the parent's region

    def panel_phases(self, opener, typing):
        """The phases every panel gets: the open (`opener` puts the
        panel up), two seconds of rest, a repaint with nothing changed,
        five such bursts spaced out, `typing` (a callable writing one
        value per call, a client typing), the close."""
        _log(self.header())
        row, msgs = self.measure("open", opener)
        self.assertGreater(row["models"], 0)
        self.assertGreater(row["bytes"], 0)
        rest, msgs = self.measure("rest 2s", hold=2.0)
        self.assertEqual(rest["kWrit"], 0, [m for _, m, _ in msgs])
        self.assertEqual(rest["bytes"], 0, [m for _, m, _ in msgs])
        burst, msgs = self.measure("repaint", self.repaint_all)
        self.assertGreater(burst["wRead"], 0)
        self.assertEqual(burst["kWrit"], 0, [m for _, m, _ in msgs])
        self.assertEqual(burst["bytes"], 0, [m for _, m, _ in msgs])

        def five():
            for _ in range(5):
                self.repaint_all()
                self.spin(30, 2)

        self.measure("repaint x5", five)
        n = 10

        def type_all():
            for i in range(n):
                typing(i)
                self.spin(40, 1)

        row, msgs = self.measure("typing x%d" % n, type_all)
        self.assertGreater(row["bytes"], 0)
        pid = self.FW.panelId()
        self.measure("close", lambda: self.control(
            {"op": "widgets.custom", "target": pid, "content": {"event": "reject"}}, 7))

    # ---- the panels ---------------------------------------------------------

    def pad(self):
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
        return body, pad

    def test_pad(self):
        """Pad's C++ panel: a client's typing is one rawValue write per
        keystroke into the length field, each running the panel's slot
        and the pad's recompute."""
        Gui = self.Gui
        Gui.activateWorkbench("PartDesignWorkbench")
        self.spin()
        body, pad = self.pad()
        self.subscribe()
        target = {}

        def opener():
            self.assertTrue(Gui.ActiveDocument.setEdit(pad, 0))
            self.open_panel()
            target["id"] = self.named("lengthEdit")
            self.assertIsNotNone(target["id"])

        def typing(i):
            self.control({"op": "widgets.update", "target": target["id"],
                          "state": {"q_rawValue": 10.0 + i + 0.5}}, 7)

        self.panel_phases(opener, typing)

    def test_orthoarray(self):
        """Draft's OrthoArray: typing into the X count spin box, the
        panel's slot moving the preview."""
        Gui = self.Gui
        box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        Gui.activateWorkbench("DraftWorkbench")
        self.spin()
        self.subscribe()
        target = {}

        def opener():
            Gui.Selection.clearSelection()
            Gui.Selection.addSelection(box)
            Gui.runCommand("Draft_OrthoArray")
            self.open_panel()
            target["id"] = self.named("spinbox_n_X")
            self.assertIsNotNone(target["id"])

        def typing(i):
            self.control({"op": "widgets.update", "target": target["id"],
                          "state": {"q_value": 2 + i}}, 7)

        self.panel_phases(opener, typing)

    def test_sketcher_list(self):
        """Sketcher's constraint list, reflected: the rows in the open, a
        repaint of the list, a client's check toggled (the panel refills
        the list in place: one op per row and per changed cell,
        uncoalesced), the sketch's own recompute repainting the list."""
        Gui = self.Gui
        import TestSketcherApp

        Gui.activateWorkbench("SketcherWorkbench")
        self.spin()
        sketch = self.doc.addObject("Sketcher::SketchObject", "SketchC")
        TestSketcherApp.CreateRectangleSketch(sketch, (0, 0), (20, 10))
        for i in range(3):
            TestSketcherApp.CreateRectangleSketch(sketch, (30 * (i + 1), 0), (20, 10))
        self.doc.recompute()
        n = len(sketch.Constraints)
        self.subscribe()
        _log(self.header())
        row, msgs = self.measure(
            "open %d rows" % n,
            lambda: self.assertTrue(Gui.ActiveDocument.setEdit(sketch, 0)) or self.open_panel())
        wid = self.named("listWidgetConstraints")
        self.assertIsNotNone(wid)
        rows = self.FW.snapshot(wid).get("items", [])
        self.assertEqual(len(rows), n)
        rest, msgs = self.measure("rest 2s", hold=2.0)
        self.assertEqual(rest["bytes"], 0, [m for _, m, _ in msgs])
        burst, msgs = self.measure("repaint", self.repaint_all)
        self.assertEqual(burst["bytes"], 0, [m for _, m, _ in msgs])

        def toggle(check):
            self.control({"op": "widgets.custom", "target": wid,
                          "content": {"item": "set", "id": rows[0]["id"], "col": 0,
                                      "cell": {"check": check}}}, 7)

        self.measure("check off", lambda: toggle(0))
        self.assertTrue(sketch.Constraints[0].InVirtualSpace)
        self.measure("check on", lambda: toggle(2))
        self.assertFalse(sketch.Constraints[0].InVirtualSpace)

        def move():
            # the sketch solved again: the list's every item re-read
            sketch.movePoint(0, 1, FreeCAD.Vector(1, 0, 0), 0)
            self.doc.recompute()

        self.measure("move point", move)
        pid = self.FW.panelId()
        self.measure("close", lambda: self.control(
            {"op": "widgets.custom", "target": pid, "content": {"event": "reject"}}, 7))

    def test_toolbars(self):
        """The tool bar mirror (7.18), the baseline: Draft's bars opened
        to a subscriber, two seconds of rest, the selection timer
        (`testActive` every 150 ms) with a selection changing, a
        workbench switch there and back."""
        Gui = self.Gui
        box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        if Gui.activeWorkbench().name() != "DraftWorkbench":
            Gui.activateWorkbench("DraftWorkbench")
        self.spin()
        self.assertTrue(self.FW.mirrorToolBars(True))
        self.spin()
        self.FW.pushed()
        _log(self.header())
        row, msgs = self.measure(
            "open",
            lambda: self.control({"op": "widgets.subscribe", "toolbars": True}, 8),
            client=8, panel=False)
        self.assertGreater(row["bytes"], 0)
        self.measure("rest 2s", hold=2.0, client=8, panel=False)

        def select():
            for _ in range(5):
                Gui.Selection.addSelection(box)
                self.spin(100, 2)
                Gui.Selection.clearSelection()
                self.spin(100, 2)

        self.measure("select x5", select, client=8, panel=False)
        self.measure("to Part", lambda: Gui.activateWorkbench("PartWorkbench"),
                     hold=0.5, client=8, panel=False)
        self.measure("to Draft", lambda: Gui.activateWorkbench("DraftWorkbench"),
                     hold=0.5, client=8, panel=False)
