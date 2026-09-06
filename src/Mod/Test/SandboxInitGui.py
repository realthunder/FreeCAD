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

"""The InitGui runner (docs/Sandbox.md 7.9, G2b): Draft's and BIM's
InitGui.py ran in the sandbox guest at startup, their workbenches are
guest wrappers on the host, their commands stand-ins; activating
each from the host crosses Initialize, and the tool bars the guest
appends exist here with the guest's commands in them.  A guest reset
(a package install) kills every stand-in: the runner re-runs both
InitGui's on the next boot and the active workbench comes back.

Needs the GUI AND a session whose startup ran the runner:

    SANDBOX_GUI_GATE_MODULES=SandboxInitGui FCX_INITGUI_IN_GUEST=1 \\
      ... xvfb-run ... ./bin/FreeCAD scripts/sandbox-gui-gate.py

(the invocation of scripts/sandbox-gui-gate.py, plus the two
variables).  Skips headless, on a build without the sandbox host, and
in a session where the runner did not run at startup -- the native
Draft would then hold the command names."""

import os
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"


class SandboxInitGuiTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        info = FreeCAD.ExpressionSandbox.imageInfo()
        if not info["host"]:
            self.skipTest("this build has no sandbox host")
        import FreeCADGui

        ran = {entry[1] for entry in getattr(FreeCADGui, "_guestInitGui", [])}
        if not {"Draft", "BIM"} <= ran:
            self.skipTest("the InitGui runner did not run Draft and BIM at startup (ran: %s);"
                          " start FreeCAD with FCX_INITGUI_IN_GUEST=1" % sorted(ran))
        self.S = FreeCAD.ExpressionSandbox
        self.S.setRouting(True)
        if not self.S.available():
            self.skipTest("the sandbox image did not boot")
        self.settle()
        self.previous = FreeCADGui.activeWorkbench().name()
        self.doc = FreeCAD.newDocument("SandboxInitGui")

    def tearDown(self):
        import FreeCADGui

        try:
            self.settle()
            if FreeCADGui.activeWorkbench().name() != self.previous:
                FreeCADGui.activateWorkbench(self.previous)
        finally:
            FreeCAD.closeDocument(self.doc.Name)

    @staticmethod
    def settle():
        """Let the event loop run until no wait cursor is up (a
        recompute's sequencer holds one until its queued poll runs)."""
        from PySide import QtCore, QtWidgets

        for _ in range(100):
            QtWidgets.QApplication.processEvents()
            if QtWidgets.QApplication.overrideCursor() is None:
                return
            loop = QtCore.QEventLoop()
            QtCore.QTimer.singleShot(20, loop.quit)
            loop.exec()

    @staticmethod
    def spin(ms=50, times=4):
        from PySide import QtCore, QtWidgets

        for _ in range(times):
            QtWidgets.QApplication.processEvents()
            loop = QtCore.QEventLoop()
            QtCore.QTimer.singleShot(ms, loop.quit)
            loop.exec()

    def assertGuestWorkbench(self, name):
        import FreeCADGui

        self.assertIn(name, FreeCADGui.listWorkbenches())
        wb = FreeCADGui.getWorkbench(name)
        self.assertTrue(hasattr(wb, "_standin"), "%s is the native workbench" % name)
        return wb

    def test_wanted(self):
        """The decision: the preference or the rig switch, the host, a bundled wheel."""
        import FreeCADGui

        home = FreeCAD.getHomePath()
        self.assertTrue(FreeCADGui._guestInitGuiWanted(os.path.join(home, "Mod", "Draft")))
        self.assertTrue(FreeCADGui._guestInitGuiWanted(os.path.join(home, "Mod", "BIM")))
        # Part has no wheel: its InitGui runs natively
        self.assertFalse(FreeCADGui._guestInitGuiWanted(os.path.join(home, "Mod", "Part")))
        self.assertIn("PartWorkbench", FreeCADGui.listWorkbenches())
        self.assertFalse(hasattr(FreeCADGui.getWorkbench("PartWorkbench"), "_standin"))

    def test_draft_from_the_guest(self):
        """Draft's InitGui.py ran in the guest: the workbench is a wrapper, the
        commands stand-ins; activation crosses Initialize and the tool bars
        the guest appends exist on the host."""
        import FreeCADGui as Gui

        S = self.S
        self.assertGuestWorkbench("DraftWorkbench")
        # (no op count of this activation is asserted: the host's
        # Preferences/Commands lookup initializes Draft nested inside
        # whichever activation first imports DraftTools, BIM's included)
        Gui.activateWorkbench("DraftWorkbench")
        self.settle()
        self.assertEqual(Gui.activeWorkbench().name(), "DraftWorkbench")
        wb = Gui.getWorkbench("DraftWorkbench")
        bars = wb.listToolbars()
        for name in ("Draft Creation", "Draft Annotation", "Draft Modification",
                     "Draft Utility", "Draft Snap"):
            self.assertIn(name, bars, bars)
        items = wb.getToolbarItems()
        self.assertIn("Draft_Line", items["Draft Creation"], items["Draft Creation"])
        self.assertIn("Draft_Move", items["Draft Modification"])
        # the commands are the guest's: DraftTools registered them on
        # its first import (Draft's Initialize, or BIM's, whichever
        # activation came first), and a poll crosses to the guest
        draft = [c for c in Gui.listCommands() if c.startswith("Draft_")]
        self.assertGreater(len(draft), 80, draft)
        cmd = Gui.Command.get("Draft_Line")
        self.assertEqual(cmd.getInfo()["menuText"], "Line")
        S.resetStats()
        # the poll crosses; Draft's answer is False until the guest has
        # a 3D view to ask for (get_3d_view, G4)
        self.assertIsInstance(cmd.isActive(), bool)
        self.assertEqual(S.stats()["proxy_calls"], 1)

    def test_bim_from_the_guest(self):
        """BIM's InitGui.py ran in the guest; activation crosses its
        Initialize, which registers its commands until the GuiUp wall
        (docs/Sandbox.md 7.9, G2b: `bimcommands` stops at the one module
        that reaches FreeCADGui under `if FreeCAD.GuiUp`, so the tool
        bars are not asserted here yet)."""
        import FreeCADGui as Gui

        S = self.S
        self.assertGuestWorkbench("BIMWorkbench")
        S.resetStats()
        Gui.activateWorkbench("BIMWorkbench")
        self.settle()
        ops = S.stats()["ops"]
        self.assertEqual(Gui.activeWorkbench().name(), "BIMWorkbench")
        self.assertGreaterEqual(ops.get("gui.icon_path", 0), 1, ops)
        self.assertGreaterEqual(ops.get("gui.lang_path", 0), 1, ops)
        self.assertGreater(ops.get("gui.cmd.add", 0), 0, ops)
        self.assertIn("BIM_Box", Gui.listCommands())
        self.assertTrue(Gui.Command.get("BIM_Box").getInfo()["menuText"])

    def test_rerun_after_a_reset(self):
        """A reset drops every stand-in; the next boot re-runs both
        InitGui's, replacing the commands and workbenches, and the
        active guest workbench is activated again."""
        import FreeCADGui as Gui

        S = self.S
        Gui.activateWorkbench("DraftWorkbench")
        self.settle()
        before = {entry[1]: entry[2] for entry in Gui._guestInitGui}
        boots = S.bootCount()
        S.reset()
        S.exec("pass")   # boots a fresh guest; the listener queues the re-run
        self.assertEqual(S.bootCount(), boots + 1)
        for _ in range(20):
            self.spin()
            after = {entry[1]: entry[2] for entry in Gui._guestInitGui}
            if all(after[name] > before[name] for name in ("Draft", "BIM")):
                break
        else:
            self.fail("the InitGui's did not re-run after the boot: %s" % after)
        self.settle()
        # the stand-ins are the new guest's: a hook crosses without ReferenceError
        self.assertGuestWorkbench("DraftWorkbench")
        self.assertGuestWorkbench("BIMWorkbench")
        self.assertIsInstance(Gui.Command.get("Draft_Line").isActive(), bool)
        self.assertEqual(Gui.Command.get("Draft_Line").getInfo()["menuText"], "Line")
        self.assertEqual(Gui.activeWorkbench().name(), "DraftWorkbench")
        self.assertIn("Draft_Line", Gui.getWorkbench("DraftWorkbench").getToolbarItems()
                      .get("Draft Creation", []))
