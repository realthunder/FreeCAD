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
        # BIM's first activation runs BIM_Welcome, a modal the guest
        # exec's (7.15: its geometry arithmetic works now) -- a hang
        # under Xvfb, as natively on a first run; the rig is not one
        bim = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/BIM")
        self.first_time = bim.GetBool("FirstTime", True)
        bim.SetBool("FirstTime", False)
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
            FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/BIM").SetBool(
                "FirstTime", self.first_time)

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

    def test_status_bar_registry(self):
        """The host's own registry (docs/Sandbox.md 7.15), the fork repair:
        `addStatusBarItem` from host Python places two items by `order`
        in the permanent band, left of the fork's fixtures; a titled item
        the user hid (its id under MainWindow/StatusBar) comes back
        hidden; `removeStatusBarItem` takes one out."""
        import FreeCADGui as Gui
        from PySide import QtWidgets

        mw = Gui.getMainWindow()
        sb = mw.statusBar()
        a = QtWidgets.QLabel("A")
        a.setObjectName("SandboxInitGui_A")
        b = QtWidgets.QLabel("B")
        b.setObjectName("SandboxInitGui_B")
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/MainWindow/StatusBar")
        try:
            mw.addStatusBarItem(b, id="SandboxInitGui_B", title="Sandbox item B",
                                slot="Right", order=560)
            mw.addStatusBarItem(a, id="SandboxInitGui_A", title="Sandbox item A",
                                slot="Right", order=550)
            self.spin(20, 2)
            self.assertEqual(mw.statusBarItem("SandboxInitGui_A").objectName(), "SandboxInitGui_A")
            self.assertTrue(a.isVisible() and b.isVisible())
            # the order is the bar's layout order: left to right
            perm = sb.findChild(QtWidgets.QWidget, "SB_PermissionIndicator")
            self.assertIsNotNone(perm)
            self.assertTrue(self.wait_until(lambda: a.x() > 0 and b.x() > a.x(), times=20),
                            (a.x(), b.x(), perm.x()))
            self.assertLess(b.x(), perm.x())
            self.assertLess(sb.findChild(QtWidgets.QWidget, "actionLabel").x(), a.x())
            # the user's choice persists per id
            params.SetBool("SandboxInitGui_A", False)
            mw.removeStatusBarItem("SandboxInitGui_A")
            mw.addStatusBarItem(a, id="SandboxInitGui_A", title="Sandbox item A", order=550)
            self.spin(20, 2)
            self.assertFalse(a.isVisible())
            self.assertTrue(b.isVisible())
            self.assertIsNotNone(mw.statusBarItem("SandboxInitGui_B"))
            mw.removeStatusBarItem("SandboxInitGui_B")
            self.assertIsNone(mw.statusBarItem("SandboxInitGui_B"))
        finally:
            params.RemBool("SandboxInitGui_A")
            mw.removeStatusBarItem("SandboxInitGui_A")
            mw.removeStatusBarItem("SandboxInitGui_B")
            a.deleteLater()
            b.deleteLater()

    def wait_until(self, predicate, ms=50, times=60):
        for _ in range(times):
            self.spin(ms, 1)
            if predicate():
                return True
        return predicate()

    def test_draft_status_bar(self):
        """Draft's `Activated()` runs to its end from the guest (docs/
        Sandbox.md 7.15): 500 ms host timers later the HOST status bar
        holds the guest's scale and snap widgets; the snap bar carries
        the commands' OWN actions (`Command.get(...).getAction()` bound,
        not copied), the lock button its popup of the other snap
        commands and the width Draft set; the scale button's menu is an
        action group whose choice, made on the host, runs the guest's
        slot (the preference and the label); deactivation hides both."""
        import FreeCADGui as Gui
        from PySide import QtWidgets

        self.assertGuestWorkbench("DraftWorkbench")
        Gui.activateWorkbench("DraftWorkbench")
        self.settle()
        mw = Gui.getMainWindow()
        sb = mw.statusBar()
        find = lambda name: sb.findChild(QtWidgets.QToolBar, name)  # noqa: E731
        self.assertTrue(self.wait_until(
            lambda: find("draft_snap_widget") is not None and find("draft_scale_widget") is not None
            and find("draft_snap_widget").isVisible() and find("draft_scale_widget").isVisible()),
            "the status bar widgets did not appear: %s" % [w.objectName() for w in sb.children()])
        snap = find("draft_snap_widget")
        scale = find("draft_scale_widget")
        self.assertEqual(mw.statusBarItem("draft_snap_widget").objectName(), "draft_snap_widget")
        self.assertEqual(mw.statusBarItem("draft_scale_widget").objectName(), "draft_scale_widget")
        # the snap bar's actions are the commands' own
        actions = snap.actions()
        for name in ("Draft_ToggleGrid", "Draft_Snap_Lock", "Draft_Snap_Dimensions",
                     "Draft_Snap_Ortho", "Draft_Snap_WorkingPlane"):
            own = Gui.Command.get(name).getAction()[0]
            self.assertIn(own, actions, name)
        grid = Gui.Command.get("Draft_ToggleGrid").getAction()[0]
        self.assertEqual(actions[0], grid)
        # the lock button: widened, and its popup lists the other snaps
        lock = Gui.Command.get("Draft_Snap_Lock").getAction()[0]
        button = snap.widgetForAction(lock)
        self.assertIsInstance(button, QtWidgets.QToolButton)
        self.assertEqual(button.minimumWidth(), 40)
        endpoint = Gui.Command.get("Draft_Snap_Endpoint").getAction()[0]
        self.assertIn(endpoint, button.actions())
        # the scale button and its group menu
        label = scale.findChild(QtWidgets.QPushButton, "ScaleLabel")
        self.assertIsNotNone(label)
        self.assertIsNotNone(label.menu())
        texts = [a.text() for a in label.menu().actions()]
        self.assertIn("1:50", texts, texts)
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Draft")
        before = params.GetFloat("DefaultAnnoScaleMultiplier", 1.0)
        try:
            [a for a in label.menu().actions() if a.text() == "1:50"][0].trigger()
            self.settle()
            self.spin(20, 2)
            self.assertAlmostEqual(params.GetFloat("DefaultAnnoScaleMultiplier"), 50.0)
            self.assertEqual(label.text(), "1:50")
        finally:
            params.SetFloat("DefaultAnnoScaleMultiplier", before)
        # deactivation: hidden 500 ms later, both still registered
        Gui.activateWorkbench("PartWorkbench")
        self.settle()
        self.assertTrue(self.wait_until(lambda: not snap.isVisible() and not scale.isVisible()))
        self.assertIsNotNone(mw.statusBarItem("draft_snap_widget"))

    def test_bim_status_bar_and_views(self):
        """BIM's `Activated()` runs to its end from the guest (docs/
        Sandbox.md 7.15): the status widget with its two action buttons
        and the nudge button's menu; the Views Manager dock made through
        the host's dock manager, shown, its tree listing a Building and
        its two levels; the status bar's views button toggling the dock
        through the guest's `BIM_Views`; deactivation hiding both."""
        import FreeCADGui as Gui
        from PySide import QtWidgets
        import Arch

        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/BIM")
        restore = params.GetBool("RestoreBimViews", True)
        first = params.GetBool("FirstTime", True)
        params.SetBool("RestoreBimViews", True)
        params.SetBool("FirstTime", False)
        doc = self.doc
        lower = Arch.makeFloor()
        lower.Label = "Lower"
        upper = Arch.makeFloor()
        upper.Label = "Upper"
        upper.Placement.Base.z = 3000
        building = Arch.makeBuilding([lower, upper])
        doc.recompute()
        self.settle()
        mw = Gui.getMainWindow()
        sb = mw.statusBar()
        try:
            self.assertGuestWorkbench("BIMWorkbench")
            Gui.activateWorkbench("BIMWorkbench")
            self.settle()
            status = lambda: sb.findChild(QtWidgets.QToolBar, "BIMStatusWidget")  # noqa: E731
            self.assertTrue(self.wait_until(lambda: status() is not None and status().isVisible()),
                            "no BIMStatusWidget: %s" % [w.objectName() for w in sb.children()])
            widget = status()
            self.assertEqual(mw.statusBarItem("BIMStatusWidget").objectName(), "BIMStatusWidget")
            actions = widget.actions()
            views = [a for a in actions if "Views" in a.toolTip()]
            self.assertEqual(len(views), 1, [a.toolTip() for a in actions])
            views = views[0]
            self.assertTrue(any("background" in a.toolTip() for a in actions))
            nudge = widget.findChild(QtWidgets.QPushButton)
            self.assertIsNotNone(nudge)
            self.assertEqual(nudge.text(), "Auto")
            self.assertIsNotNone(nudge.menu())
            self.assertIn("1 mm", [a.text() for a in nudge.menu().actions()])
            # the Views Manager: a dock of the host's manager, shown, filled
            dock = mw.findChild(QtWidgets.QDockWidget, "BIM Views Manager")
            self.assertIsNotNone(dock)
            self.assertTrue(self.wait_until(dock.isVisible))
            self.assertTrue(views.isChecked())
            tree = dock.findChild(QtWidgets.QTreeWidget, "tree")
            self.assertIsNotNone(tree)
            self.assertTrue(self.wait_until(lambda: tree.topLevelItemCount() >= 1))
            self.assertEqual(tree.topLevelItemCount(), 1)
            top = tree.topLevelItem(0)
            self.assertEqual(top.text(0), building.Label)
            self.assertEqual(top.childCount(), 2)
            self.assertEqual([top.child(i).text(0) for i in range(2)], ["Lower", "Upper"])
            # the status bar's views button toggles the dock (BIM_Views in the guest)
            views.trigger()
            self.settle()
            self.assertTrue(self.wait_until(lambda: not dock.isVisible(), times=20))
            self.assertFalse(views.isChecked())
            views.trigger()
            self.settle()
            self.assertTrue(self.wait_until(dock.isVisible, times=20))
            self.assertTrue(views.isChecked())
            # deactivation hides both and keeps the restore flag
            Gui.activateWorkbench("PartWorkbench")
            self.settle()
            self.assertTrue(self.wait_until(lambda: not widget.isVisible() and not dock.isVisible()))
            self.assertTrue(params.GetBool("RestoreBimViews"))
        finally:
            params.SetBool("RestoreBimViews", restore)
            params.SetBool("FirstTime", first)

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
