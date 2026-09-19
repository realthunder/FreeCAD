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

"""The sandbox guest registering GUI objects (docs/Sandbox.md 7.9, G2a):
a probe module pushed into the guest registers a workbench, a plain
command, a checkable command and a group through the guest's own
FreeCADGui; the host sees them in its command manager and workbench
registry, drives them (IsActive polls, Activated, the workbench's
Initialize/Activated/Deactivated) through the stand-ins, and the
workbench's toolbars and menus, appended from the guest, exist on the
host.  A document principal is refused.

Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb
(the Gui binary has no -t mode).  Skips headless, on a build without
the sandbox host, and when the image cannot boot."""

import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"

# What the guest runs: unmodified FreeCAD workbench idiom against the
# guest's FreeCADGui.  `calls` and `active` are read back by the host.
PROBE = r'''
import FreeCADGui

calls = []
active = [True]


class ProbeCmd:
    def GetResources(self):
        return {
            "MenuText": "Sandbox probe",
            "ToolTip": "A command living in the guest",
            "Pixmap": "Test_Test",
        }

    def IsActive(self):
        return active[0]

    def Activated(self):
        calls.append("Activated")


class ProbeCheck:
    def GetResources(self):
        return {"MenuText": "Sandbox check", "ToolTip": "A checkable command", "Checkable": True}

    def Activated(self, index):
        calls.append(["Check", index])


class ProbeGroup:
    def GetResources(self):
        return {"MenuText": "Sandbox group", "ToolTip": "A group of guest commands"}

    def GetCommands(self):
        return ("Sandbox_Probe", "Sandbox_Check")

    def GetDefaultCommand(self):
        return 1


class SandboxProbeWorkbench(FreeCADGui.Workbench):
    MenuText = "Sandbox probe"
    ToolTip = "A workbench registered from the guest"

    def Initialize(self):
        calls.append("Initialize")
        self.appendToolbar("Sandbox probe tools", ["Sandbox_Probe", "Sandbox_Check"])
        self.appendMenu("Sandbox probe", ["Sandbox_Group"])
        self.toolbars = self.listToolbars()
        self.items = self.getToolbarItems()

    def Activated(self):
        # the host has switched by now: the guest's own view of it
        calls.append(["WbActivated", FreeCADGui.activeWorkbench() is self])

    def Deactivated(self):
        calls.append("WbDeactivated")


FreeCADGui.addCommand("Sandbox_Probe", ProbeCmd())
FreeCADGui.addCommand("Sandbox_Check", ProbeCheck())
FreeCADGui.addCommand("Sandbox_Group", ProbeGroup())
FreeCADGui.addWorkbench(SandboxProbeWorkbench)
'''

WORKBENCH = "SandboxProbeWorkbench"
COMMANDS = ("Sandbox_Probe", "Sandbox_Check", "Sandbox_Group")


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


class SandboxGuiTest(unittest.TestCase):
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
        self.doc = FreeCAD.newDocument("SandboxGui")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        import FreeCADGui

        self.previous = FreeCADGui.activeWorkbench().name()

    def tearDown(self):
        import FreeCADGui

        try:
            if FreeCADGui.activeWorkbench().name() != self.previous:
                FreeCADGui.activateWorkbench(self.previous)
            if WORKBENCH in FreeCADGui.listWorkbenches():
                FreeCADGui.removeWorkbench(WORKBENCH)
        finally:
            FreeCAD.closeDocument(self.doc.Name)
            self.prefs.restore()

    def guest(self, expr, module="fcx_guiprobe"):
        """A python-mode read of the probe module's state."""
        return self.S.evaluate(self.owner, "import %s; %s" % (module, expr), self.opts)

    def test_registration_from_the_guest(self):
        import FreeCADGui as Gui

        S = self.S
        for name in COMMANDS:
            self.assertNotIn(name, Gui.listCommands(), "left over from an earlier run")
        S.resetStats()
        S.exec(PROBE, "fcx_guiprobe")
        ops = S.stats()["ops"]
        self.assertEqual(ops.get("gui.cmd.add"), 3, ops)
        self.assertEqual(ops.get("gui.wb.add"), 1, ops)

        # the commands, as the command manager sees them
        names = Gui.listCommands()
        for name in COMMANDS:
            self.assertIn(name, names)
        cmd = Gui.Command.get("Sandbox_Probe")
        info = cmd.getInfo()
        self.assertEqual(info["menuText"], "Sandbox probe")
        self.assertEqual(info["toolTip"], "A command living in the guest")
        self.assertEqual(info["pixmap"], "Test_Test")
        self.assertEqual(Gui.Command.get("Sandbox_Group").getInfo()["menuText"], "Sandbox group")

        # IsActive follows the guest's flag; Activated crosses
        self.assertTrue(cmd.isActive())
        S.exec("import fcx_guiprobe; fcx_guiprobe.active[0] = False")
        self.assertFalse(cmd.isActive())
        S.exec("import fcx_guiprobe; fcx_guiprobe.active[0] = True")
        self.assertTrue(cmd.isActive())
        self.assertEqual(self.guest("fcx_guiprobe.calls"), [])
        Gui.runCommand("Sandbox_Probe")
        self.assertEqual(self.guest("fcx_guiprobe.calls"), ["Activated"])
        Gui.runCommand("Sandbox_Check")
        calls = self.guest("fcx_guiprobe.calls")
        self.assertEqual(len(calls), 2, calls)
        self.assertEqual(calls[1][0], "Check")

        # the workbench: registered, activated from the host, its
        # toolbars and menus appended from the guest
        self.assertIn(WORKBENCH, Gui.listWorkbenches())
        wb = Gui.getWorkbench(WORKBENCH)
        self.assertEqual(wb.MenuText, "Sandbox probe")
        self.assertEqual(wb.ToolTip, "A workbench registered from the guest")
        Gui.activateWorkbench(WORKBENCH)
        self.assertEqual(Gui.activeWorkbench().name(), WORKBENCH)
        calls = self.guest("fcx_guiprobe.calls")
        self.assertIn("Initialize", calls)
        self.assertIn(["WbActivated", True], calls)
        self.assertIn("Sandbox probe tools", wb.listToolbars())
        self.assertEqual(wb.getToolbarItems()["Sandbox probe tools"], ["Sandbox_Probe", "Sandbox_Check"])
        self.assertIn("Sandbox probe", wb.listMenus())
        # what the guest read back through its own Workbench methods
        self.assertEqual(
            self.guest("fcx_guiprobe.FreeCADGui.getWorkbench('%s').toolbars" % WORKBENCH),
            wb.listToolbars(),
        )
        self.assertEqual(
            self.guest("fcx_guiprobe.FreeCADGui.getWorkbench('%s').items" % WORKBENCH),
            wb.getToolbarItems(),
        )
        Gui.activateWorkbench(self.previous)
        self.assertIn("WbDeactivated", self.guest("fcx_guiprobe.calls"))

        # a second registration under the same names (a guest that was
        # reset and ran its InitGui again) replaces, natively a KeyError
        S.exec(PROBE, "fcx_guiprobe2")
        self.assertEqual(self.guest("fcx_guiprobe2.calls", "fcx_guiprobe2"), [])
        Gui.runCommand("Sandbox_Probe")
        self.assertEqual(self.guest("fcx_guiprobe2.calls", "fcx_guiprobe2"), ["Activated"])
        ops = S.stats()["ops"]
        self.assertEqual(ops.get("gui.cmd.add"), 6, ops)

    def test_document_principal_is_refused(self):
        with self.assertRaises(Exception) as cm:
            self.S.evaluate(
                self.owner, "import FreeCADGui; FreeCADGui.addIconPath('/nowhere')", self.opts
            )
        text = str(cm.exception)
        self.assertIn("Permission", text)
        self.assertIn("gui", text)
        import FreeCADGui

        self.assertNotIn(WORKBENCH, FreeCADGui.listWorkbenches())
