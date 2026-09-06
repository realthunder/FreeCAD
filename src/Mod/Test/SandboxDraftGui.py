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

"""Forms built in code from the sandbox guest (docs/Sandbox.md 7.11,
G3c): Draft's `DraftGui.py`, unmodified, imported in the guest.  Its
`DraftToolBar()` builds the tray tool bar at import (buttons in code,
an icon painted with QPainter, `getMainWindow().addToolBar`), and its
`taskUi()`/`lineUi()` build the task panel in code -- labels, input
fields, buttons, check boxes in nested box layouts, a spacer, a bold
font, the field-lock actions on the inputs, event filters for the
snap-cycling key and the lock shortcuts.  The host realizes the layout
tree from the widget's layout spec, relays the key events the widgets
asked for, and `validatePoint` round-trips the point typed on the host.

Also: a menu built in the guest run modally, a command run by name,
the main window's message and close hook, a document principal refused.

Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Skips headless, on a build without the sandbox host, when the image
cannot boot, and when DraftGui is not bundled for the guest."""

import sys
import time
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"

# The guest side of the harness, beside DraftGui.
GUEST = r'''
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

got = []      # what validatePoint delivered: (x, y, z, global, relative)
hits = []     # tray events
activated = []


def hook_point():
    tb = FreeCADGui.draftToolBar
    tb.pointcallback = lambda p, g, r: got.append([p.x, p.y, p.z, g, r])


def watch_tray():
    tb = FreeCADGui.draftToolBar
    a = tb.tray.toggleViewAction()
    a.setVisible(True)
    a.triggered.connect(lambda on: hits.append(["toggle", on]))
    tb.wplabel.clicked.connect(lambda on=False: hits.append(["wp"]))
    tb.constrButton.toggled.connect(lambda on: hits.append(["constr", on]))


class ProbeCommand:
    def GetResources(self):
        return {"MenuText": "G3c probe", "ToolTip": "runs from the guest"}

    def IsActive(self):
        return True

    def Activated(self):
        activated.append(len(activated))


FreeCADGui.addCommand("Fcx_G3cProbe", ProbeCommand())

menu = None
picked = []


def make_menu():
    global menu
    menu = QtWidgets.QMenu()
    menu.setTitle("Probe")
    a = menu.addAction("First")
    b = menu.addAction(QtGui.QIcon(":/icons/Draft_Line.svg"), "Second")
    menu.addSeparator()
    sub = menu.addMenu("More")
    sub.addAction("Third")
    b.triggered.connect(lambda on=False: picked.append("second"))
    menu.triggered.connect(lambda act: picked.append(["menu", act.text()]))
    return menu.model_id


def run_menu():
    chosen = menu.exec_()
    picked.append(["chosen", chosen.text() if chosen is not None else None])
    return picked
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


class SandboxDraftGuiTest(unittest.TestCase):
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
        self.settle()
        self.doc = FreeCAD.newDocument("SandboxDraftGui")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        import Draft_rc  # noqa: F401  (the icons DraftGui names)

        try:
            self.S.exec("import DraftGui", "fcx_draftguicheck")
        except Exception as e:
            self.tearDown()
            self.skipTest("DraftGui is not bundled for the guest: %s" % str(e)[:400])
        try:
            self.guest("fcx_draftprobe.got.clear()\nfcx_draftprobe.hits.clear()\n"
                       "fcx_draftprobe.picked.clear()\n", session=True)
        except Exception:
            self.S.exec(GUEST, "fcx_draftprobe")

    def tearDown(self):
        import FreeCADGui

        try:
            if FreeCADGui.Control.activeDialog():
                FreeCADGui.Control.closeDialog()
        finally:
            FreeCAD.closeDocument(self.doc.Name)
            self.prefs.restore()

    def guest(self, expr, module="fcx_draftprobe", session=False):
        """A python-mode read of the probe module's state (the document
        principal), or a session exec."""
        if session:
            return self.S.exec("import %s\n%s" % (module, expr))
        return self.S.evaluate(self.owner, "import %s; %s" % (module, expr), self.opts)

    @staticmethod
    def settle():
        """Let the event loop run until no wait cursor is up.  The
        sequencer of a recompute holds a wait cursor -- whose filter
        eats every key and mouse event -- until its poll, queued on the
        event loop, sees the sequence over; a gate runs its modules in
        one callback, so a recompute an earlier module made (the
        PartDesign pad of SandboxNative) leaves the cursor up here."""
        from PySide import QtCore, QtWidgets

        for _ in range(100):
            QtWidgets.QApplication.processEvents()
            if QtWidgets.QApplication.overrideCursor() is None:
                return
            loop = QtCore.QEventLoop()
            QtCore.QTimer.singleShot(20, loop.quit)
            loop.exec()

    @staticmethod
    def focus(widget, on=True):
        """The focus events the guest's `hasFocus()` follows, sent
        outright: a window's activation is not this test's to have."""
        from PySide import QtCore, QtGui, QtWidgets

        ev = QtGui.QFocusEvent(QtCore.QEvent.Type.FocusIn if on else QtCore.QEvent.Type.FocusOut,
                               QtCore.Qt.OtherFocusReason)
        QtWidgets.QApplication.sendEvent(widget, ev)

    @staticmethod
    def key(widget, key, text=""):
        from PySide import QtCore, QtGui, QtWidgets

        for t in (QtCore.QEvent.Type.KeyPress, QtCore.QEvent.Type.KeyRelease):
            ev = QtGui.QKeyEvent(t, key, QtCore.Qt.NoModifier, text)
            QtWidgets.QApplication.sendEvent(widget, ev)

    def test_a_tray_and_task_panel(self):
        import FreeCADGui as Gui
        from PySide import QtCore, QtGui, QtWidgets

        S = self.S
        FW = Gui.FormWidgets
        mw = Gui.getMainWindow()

        # the tray: built at import, a tool bar of the main window
        bars = FW.find("QToolBarModel")
        self.assertEqual(len(bars), 1, bars)
        tray = FW.widget(bars[0])
        self.assertIsNotNone(tray, "the tray was not realized on the main window")
        self.assertEqual(tray.objectName(), "Draft tray")
        self.assertEqual(tray.windowTitle(), "Draft Tray")
        self.assertIn(tray, mw.findChildren(QtWidgets.QToolBar))
        self.assertTrue(tray.isHidden())
        actions = tray.actions()
        self.assertEqual(len(actions), 4, [a.text() for a in actions])
        buttons = [a.defaultWidget() for a in actions]
        self.assertEqual([b.objectName() for b in buttons],
                         ["wplabel", "stylebutton", "constrButton", "autoGroup"])
        # the style button's icon was painted in the guest (QPainter on
        # a QImage, crossing as an SVG); the tool button is square
        self.assertFalse(buttons[1].icon().isNull())
        self.assertIsInstance(buttons[2], QtWidgets.QToolButton)
        self.assertTrue(buttons[2].isCheckable())
        self.assertEqual(buttons[2].maximumWidth(), buttons[2].maximumHeight())
        self.assertEqual(buttons[3].text(), "None")
        self.assertTrue(buttons[3].isFlat())
        # the tray's toggle action is the real bar's own; a host trigger
        # reaches the guest's slot, the guest's show reaches the bar
        self.guest("fcx_draftprobe.watch_tray()", session=True)
        self.assertTrue(tray.toggleViewAction().isVisible())
        tray.toggleViewAction().trigger()
        buttons[2].click()
        self.assertEqual(self.guest("fcx_draftprobe.hits"),
                         [["toggle", True], ["constr", True]])
        self.assertTrue(tray.isVisible())
        self.assertTrue(self.guest("FreeCADGui.draftToolBar.constrMode", "FreeCADGui"))
        S.exec("import FreeCADGui\nFreeCADGui.draftToolBar.tray.hide()\n")
        self.assertTrue(tray.isHidden())

        # the task panel: lineUi() builds it in code and shows it
        FW.resetStats()
        S.resetStats()
        t0 = time.perf_counter()
        S.exec("import FreeCADGui\nFreeCADGui.draftToolBar.lineUi()\n")
        t_ui = time.perf_counter() - t0
        ops = S.stats()["ops"]
        sys.stderr.write("SandboxDraftGui: lineUi() %.3f s, %d objects, ops %s, store %s\n"
                         % (t_ui, FW.count(), sorted(ops.items()), sorted(FW.stats().items())))
        self.assertTrue(Gui.Control.activeDialog())
        w = Gui.Control.activeTaskDialog().getDialogContent()[0]
        self.assertEqual(w.windowTitle(), "Line")
        self.assertFalse(w.windowIcon().isNull())
        lay = w.layout()
        self.assertIsInstance(lay, QtWidgets.QVBoxLayout)
        self.assertTrue(lay.objectName().startswith("_fcx_layout_"), lay.objectName())
        self.assertEqual(lay.count(), 28)
        rows = [lay.itemAt(i).layout() for i in range(lay.count())]
        self.assertEqual(sum(1 for r in rows if isinstance(r, QtWidgets.QHBoxLayout)), 16)
        self.assertIsNotNone(lay.itemAt(lay.count() - 1).spacerItem())

        x = w.findChild(QtWidgets.QLineEdit, "xValue")
        y = w.findChild(QtWidgets.QLineEdit, "yValue")
        z = w.findChild(QtWidgets.QLineEdit, "zValue")
        length = w.findChild(QtWidgets.QLineEdit, "lengthValue")
        radius = w.findChild(QtWidgets.QLineEdit, "radiusValue")
        label_x = w.findChild(QtWidgets.QLabel, "labelx")
        cmdlabel = w.findChild(QtWidgets.QLabel, "cmdlabel")
        point = w.findChild(QtWidgets.QPushButton, "addButton")
        finish = w.findChild(QtWidgets.QPushButton, "finishButton")
        continue_cmd = w.findChild(QtWidgets.QCheckBox, "continueCmd")
        for name, widget in (("x", x), ("y", y), ("z", z), ("length", length),
                             ("radius", radius), ("labelx", label_x), ("cmdlabel", cmdlabel),
                             ("point", point), ("finish", finish),
                             ("continue", continue_cmd)):
            self.assertIsNotNone(widget, name)
        self.assertEqual(x.metaObject().className(), "Gui::InputField")
        # what lineUi shows and hides, the widths, the bold font, the
        # lock action each input carries (hidden until a lock)
        for shown in (x, y, z, length, label_x, point, continue_cmd):
            self.assertFalse(shown.isHidden(), shown.objectName())
        for hidden in (radius, finish):  # wireUi shows Finish, lineUi does not
            self.assertTrue(hidden.isHidden(), hidden.objectName())
        self.assertEqual(x.minimumWidth(), 110)
        self.assertTrue(cmdlabel.font().bold())
        self.assertFalse(label_x.font().bold())
        self.assertEqual(label_x.text(), self.guest("fcx_draftprobe.FreeCADGui.draftToolBar"
                                                     ".labelx.text()"))
        self.assertEqual(len(x.actions()), 1)
        self.assertFalse(x.actions()[0].isVisible())
        self.assertEqual(FreeCAD.Units.Quantity(x.text()).Value, 0.0)

        # a point typed on the host: focus, values, Enter -- the guest's
        # changeXValue and friends read `hasFocus()` first
        self.guest("fcx_draftprobe.hook_point()", session=True)
        for widget, value in ((x, 10.0), (y, 20.0), (z, 30.0)):
            self.focus(widget)
            widget.setProperty("rawValue", value)
            self.focus(widget, False)
        self.assertEqual(self.guest("[fcx_draftprobe.FreeCADGui.draftToolBar.x,"
                                    " fcx_draftprobe.FreeCADGui.draftToolBar.y,"
                                    " fcx_draftprobe.FreeCADGui.draftToolBar.z]"),
                         [10.0, 20.0, 30.0])
        self.assertEqual(self.guest("fcx_draftprobe.got"), [])
        # Enter on xValue: the lock filter locks the field (its action
        # shows) and returnPressed -> checkx moves on to Y
        self.focus(x)
        t0 = time.perf_counter()
        self.key(x, QtCore.Qt.Key_Return, "\r")
        t_key = time.perf_counter() - t0
        self.assertTrue(self.guest("fcx_draftprobe.FreeCADGui.draftToolBar._locks"
                                   ".is_locked('x')"),
                        (x.text(), QtWidgets.QApplication.overrideCursor()))
        self.assertTrue(x.actions()[0].isVisible())
        sys.stderr.write("SandboxDraftGui: Enter on a field -> lock filter + returnPressed"
                         " %.2f ms\n" % (t_key * 1000))
        # Enter on zValue: returnPressed -> validatePoint, which delivers
        # the point and unlocks every field (as natively)
        self.focus(z)
        self.key(z, QtCore.Qt.Key_Return, "\r")
        got = self.guest("fcx_draftprobe.got")
        self.assertEqual(got, [[10.0, 20.0, 30.0, False, True]])
        self.assertFalse(self.guest("fcx_draftprobe.FreeCADGui.draftToolBar._locks"
                                    ".is_locked('x')"))
        self.assertFalse(x.actions()[0].isVisible())
        # the snap-cycling key (the backquote, Draft's default) is eaten
        # by the guest's event filter (DraftBaseWidget.eventFilter
        # answers True): the text stays
        before = x.text()
        self.focus(x)
        self.key(x, QtCore.Qt.Key_QuoteLeft, "`")
        self.assertEqual(x.text(), before)
        # a plain character is not
        self.key(x, QtCore.Qt.Key_5, "5")
        self.assertNotEqual(x.text(), before)
        # a double-click unlocks (the lock filter's MouseButtonDblClick)
        self.focus(y)
        self.key(y, QtCore.Qt.Key_Return, "\r")
        self.assertTrue(self.guest("fcx_draftprobe.FreeCADGui.draftToolBar._locks"
                                   ".is_locked('y')"))
        self.assertTrue(y.actions()[0].isVisible())
        dbl = QtGui.QMouseEvent(QtCore.QEvent.Type.MouseButtonDblClick, QtCore.QPointF(3, 4),
                                QtCore.Qt.LeftButton, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier)
        QtWidgets.QApplication.sendEvent(y, dbl)
        self.assertFalse(self.guest("fcx_draftprobe.FreeCADGui.draftToolBar._locks"
                                    ".is_locked('y')"))
        self.assertFalse(y.actions()[0].isVisible())

        # the Enter Point button and the panel's accept both validate
        point.click()
        self.assertEqual(len(self.guest("fcx_draftprobe.got")), 2)
        self.assertTrue(FW.accept())
        self.assertEqual(len(self.guest("fcx_draftprobe.got")), 3)
        self.assertFalse(Gui.Control.activeDialog())

        # shown again with another ui, closed from the guest
        S.exec("import FreeCADGui\nFreeCADGui.draftToolBar.offsetUi()\n")
        self.assertTrue(Gui.Control.activeDialog())
        w = Gui.Control.activeTaskDialog().getDialogContent()[0]
        self.assertEqual(w.windowTitle(), "Offset")
        self.assertFalse(w.findChild(QtWidgets.QLineEdit, "radiusValue").isHidden())
        self.assertTrue(w.findChild(QtWidgets.QLineEdit, "xValue").isHidden())
        self.assertFalse(w.findChild(QtWidgets.QCheckBox, "isCopy").isHidden())
        S.exec("import FreeCADGui\nFreeCADGui.draftToolBar.offUi()\n")
        self.assertFalse(Gui.Control.activeDialog())

    def test_b_menu_command_main_window(self):
        import FreeCADGui as Gui
        from PySide import QtCore, QtWidgets

        S = self.S
        FW = Gui.FormWidgets
        mw = Gui.getMainWindow()

        # a command registered from the guest runs from the guest by name
        S.exec("import FreeCADGui\nFreeCADGui.runCommand('Fcx_G3cProbe')\n")
        self.assertEqual(self.guest("fcx_draftprobe.activated"), [0])

        # the main window shim: a message, the close hook registered
        S.exec("import FreeCADGui\nmw = FreeCADGui.getMainWindow()\n"
               "mw.showMessage('from the guest', 5000)\n"
               "mw.mainWindowClosed.connect(lambda: None)\n"
               "assert mw.windowTitle() == %r\n" % mw.windowTitle())
        # this fork's showMessage writes its own status-bar label
        labels = [lab.text() for lab in mw.statusBar().findChildren(QtWidgets.QLabel)]
        self.assertIn("from the guest", labels)

        # a menu built in the guest, run modally: a host click on its
        # second item (a timer inside the nested loop) is the result
        # (a session's: a document principal may not open widgets)
        self.guest("fcx_draftprobe.make_menu()", session=True)
        menu_id = self.guest("fcx_draftprobe.menu.model_id")
        self.assertTrue(menu_id)

        def click_second():
            menu = FW.widget(menu_id)
            if menu is None or not menu.isVisible():
                QtCore.QTimer.singleShot(50, click_second)
                return
            try:
                from PySide6 import QtTest

                action = menu.actions()[1]
                QtTest.QTest.mouseClick(menu, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier,
                                        menu.actionGeometry(action).center())
            except Exception:
                menu.close()

        def give_up():
            menu = FW.widget(menu_id)
            if menu is not None and menu.isVisible():
                menu.close()

        QtCore.QTimer.singleShot(100, click_second)
        QtCore.QTimer.singleShot(5000, give_up)
        t0 = time.perf_counter()
        self.guest("fcx_draftprobe.run_menu()", session=True)
        sys.stderr.write("SandboxDraftGui: menu exec_() %.3f s\n" % (time.perf_counter() - t0))
        picked = self.guest("fcx_draftprobe.picked")
        menu = FW.widget(menu_id)
        self.assertIsNotNone(menu)
        self.assertIsInstance(menu, QtWidgets.QMenu)
        self.assertEqual(menu.title(), "Probe")
        self.assertEqual([a.text() for a in menu.actions()], ["First", "Second", "", "More"])
        self.assertTrue(menu.actions()[2].isSeparator())
        self.assertEqual([a.text() for a in menu.actions()[3].menu().actions()], ["Third"])
        self.assertFalse(menu.actions()[1].icon().isNull())
        self.assertEqual(picked, ["second", ["menu", "Second"], ["chosen", "Second"]])

    def test_document_principal_is_refused(self):
        with self.assertRaises(Exception) as cm:
            self.S.evaluate(self.owner,
                            "import FreeCADGui; FreeCADGui.getMainWindow().showMessage('x')",
                            self.opts)
        self.assertIn("gui", str(cm.exception).lower())
