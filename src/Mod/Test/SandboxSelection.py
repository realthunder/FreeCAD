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
"""The selection input and the U2 dialogs from the sandbox guest
(docs/Sandbox.md 7.11, G3d).  `FreeCADGui.Selection` in the guest is
the host's, one op per call: objects cross as handles, a
SelectionObject by value, an observer as a guest proxy the host's own
SelectionObserverPython drives.  The four stock dialogs -- QMessageBox,
QInputDialog, QFileDialog, QColorDialog -- are one synchronous op each,
a nested loop under the host's main window; a host timer drives the
modal here and the guest reads the answer.  A document principal is
refused (the `gui` permission).
Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Skips headless, on a build without the sandbox host, and when the image
cannot boot."""

import os
import sys
import tempfile
import time
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"

# The guest side of the harness.
GUEST = r'''
import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

events = []     # what the observer saw
results = {}    # what the dialogs answered


class Observer:
    def addSelection(self, doc, obj, sub, pnt):
        events.append(["add", doc, obj, sub, list(pnt)])

    def removeSelection(self, doc, obj, sub):
        events.append(["remove", doc, obj, sub])

    def clearSelection(self, doc):
        events.append(["clear", doc])


observer = Observer()


def watch():
    FreeCADGui.Selection.addObserver(observer)


def unwatch():
    FreeCADGui.Selection.removeObserver(observer)


def round_trip():
    """Within ONE evaluation (a handle lives that long for a session
    guest, which has no owner document to re-resolve it in): the
    selected object as a handle, the selection cleared, the object
    selected again by that handle."""
    Sel = FreeCADGui.Selection
    obj = Sel.getSelection()[0]
    docname = obj.Document.Name
    Sel.clearSelection()
    out = [obj.Name, docname, Sel.hasSelection(), Sel.isSelected(obj)]
    Sel.addSelection(obj)
    out += [Sel.isSelected(obj), names(), Sel.countObjectsOfType("Part::Feature")]
    return out


def select_sub(docname, name, sub):
    FreeCADGui.Selection.addSelection(docname, name, sub)


def names():
    return [o.Name for o in FreeCADGui.Selection.getSelection()]


def complete():
    # SelectionObjects, as the host answers
    return [s.Object.Name + "/" + s.ObjectName for s in FreeCADGui.Selection.getCompleteSelection()]


def ex():
    out = []
    for s in FreeCADGui.Selection.getSelectionEx():
        out.append([s.ObjectName, s.DocumentName, list(s.SubElementNames), s.HasSubObjects,
                    s.Object.Name, s.Document.Name, s.FullName, s.TypeName,
                    [o.ShapeType for o in s.SubObjects], repr(s)])
    return out


def has():
    return FreeCADGui.Selection.hasSelection()


def count(type_name):
    return FreeCADGui.Selection.countObjectsOfType(type_name)


def clear():
    FreeCADGui.Selection.clearSelection()


def missing():
    try:
        FreeCADGui.Selection.addSelectionGate("x")
    except AttributeError as e:
        return str(e)
    return None


def ask_question():
    r = QtWidgets.QMessageBox.question(None, "Probe", "Yes or no?",
                                       QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
    results["question"] = [int(r), r == QtWidgets.QMessageBox.Yes]


def ask_box():
    box = QtWidgets.QMessageBox()
    box.setWindowTitle("Probe box")
    box.setIcon(QtWidgets.QMessageBox.Warning)
    box.setText("an instance")
    box.setStandardButtons(QtWidgets.QMessageBox.Save | QtWidgets.QMessageBox.Cancel)
    box.setDefaultButton(QtWidgets.QMessageBox.Cancel)
    results["box"] = int(box.exec_())


def ask_text():
    results["text"] = list(QtWidgets.QInputDialog.getText(None, "Probe", "Name:",
                                                          QtWidgets.QLineEdit.Normal, "seed"))


def ask_item():
    results["item"] = list(QtWidgets.QInputDialog.getItem(None, "Probe", "Pick:",
                                                          ["a", "b", "c"], 1, False))


def ask_int():
    results["int"] = list(QtWidgets.QInputDialog.getInt(None, "Probe", "How many:", 3, 0, 10))


def ask_open(directory):
    results["open"] = list(QtGui.QFileDialog.getOpenFileName(None, "Probe open", directory,
                                                             "Text (*.txt);;All (*)"))


def ask_save(directory):
    results["save"] = list(QtWidgets.QFileDialog.getSaveFileName(None, "Probe save", directory,
                                                                 "Text (*.txt)"))


def ask_color():
    c = QtGui.QColorDialog.getColor(QtGui.QColor(10, 20, 30), None, "Probe color")
    results["color"] = [c.isValid(), c.name()]


def ask_color_cancel():
    c = QtWidgets.QColorDialog.getColor(QtGui.QColor(10, 20, 30))
    results["color_cancel"] = [c.isValid()]
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


class SandboxSelectionTest(unittest.TestCase):
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
        self.doc = FreeCAD.newDocument("SandboxSelection")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        self.box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        import FreeCADGui

        FreeCADGui.Selection.clearSelection()
        try:
            self.guest("fcx_selprobe.events.clear()\nfcx_selprobe.results.clear()\n",
                       session=True)
        except Exception:
            self.S.exec(GUEST, "fcx_selprobe")

    def tearDown(self):
        import FreeCADGui

        try:
            self.guest("fcx_selprobe.unwatch()", session=True)
        except Exception:
            pass
        FreeCADGui.Selection.clearSelection()
        FreeCAD.closeDocument(self.doc.Name)
        self.prefs.restore()

    def guest(self, expr, module="fcx_selprobe", session=False):
        """A python-mode read of the probe module's state (the document
        principal), or a session exec."""
        if session:
            return self.S.exec("import %s\n%s" % (module, expr))
        return self.S.evaluate(self.owner, "import %s; %s" % (module, expr), self.opts)

    def read(self, expr):
        """A session read of a probe value (the ops are the session's)."""
        self.guest("fcx_selprobe.results['_r'] = %s" % expr, session=True)
        return self.guest("fcx_selprobe.results['_r']")

    @staticmethod
    def settle():
        from PySide import QtCore, QtWidgets

        for _ in range(100):
            QtWidgets.QApplication.processEvents()
            if QtWidgets.QApplication.overrideCursor() is None:
                return
            loop = QtCore.QEventLoop()
            QtCore.QTimer.singleShot(20, loop.quit)
            loop.exec()

    # ---- the selection ---------------------------------------------------

    def test_a_selection(self):
        import FreeCADGui

        Sel = FreeCADGui.Selection
        # the host's selection is the guest's read, the objects as
        # handles; the guest selects by handle and the host sees it
        Sel.addSelection(self.box)
        self.assertEqual(self.read("fcx_selprobe.names()"), ["Box"])
        self.assertTrue(self.read("fcx_selprobe.has()"))
        self.assertEqual(self.read("fcx_selprobe.round_trip()"),
                         ["Box", self.doc.Name, False, False, True, ["Box"], 1])
        self.assertEqual([o.Name for o in Sel.getSelection()], ["Box"])
        # a sub-element by names; getSelectionEx by value, SubObjects
        # resolved in the guest through the object's getSubObject
        self.guest("fcx_selprobe.clear()", session=True)
        self.assertFalse(Sel.hasSelection())
        self.guest("fcx_selprobe.select_sub(%r, 'Box', 'Face1')" % self.doc.Name, session=True)
        ex = self.read("fcx_selprobe.ex()")
        self.assertEqual(len(ex), 1)
        name, docname, subs, has_subs, oname, dname, full, tname, shapes, text = ex[0]
        self.assertEqual((name, docname, subs, has_subs), ("Box", self.doc.Name, ["Face1"], True))
        self.assertEqual((oname, dname), ("Box", self.doc.Name))
        # FullName is the host's, verbatim: the Python expression that
        # re-selects it
        self.assertEqual(full, "(App.getDocument('%s').getObject('Box'),['Face1',])"
                         % self.doc.Name)
        self.assertEqual(tname, "Part::Box")
        self.assertEqual(shapes, ["Face"])
        self.assertIn("Box.Face1", text)
        self.assertEqual(self.read("fcx_selprobe.complete()"), ["Box/Box"])
        # a host-side change is the guest's next read
        Sel.clearSelection()
        Sel.addSelection(self.owner)
        self.assertEqual(self.read("fcx_selprobe.names()"), ["Owner"])
        # outside the subset: an AttributeError naming it
        self.assertIn("addSelectionGate", self.read("fcx_selprobe.missing()"))

    def test_b_observer(self):
        import FreeCADGui

        Sel = FreeCADGui.Selection
        self.guest("fcx_selprobe.watch()", session=True)
        # host changes reach the guest's observer, by value, in order
        Sel.addSelection(self.box)
        Sel.addSelection(self.doc.Name, "Box", "Face2", 1.0, 2.0, 3.0)
        Sel.removeSelection(self.box, "Face2")
        Sel.clearSelection(self.doc.Name)
        events = self.read("fcx_selprobe.events")
        kinds = [e[0] for e in events]
        self.assertEqual(kinds, ["add", "add", "remove", "clear"])
        self.assertEqual(events[0][1:4], [self.doc.Name, "Box", ""])
        self.assertEqual(events[1][1:5], [self.doc.Name, "Box", "Face2", [1.0, 2.0, 3.0]])
        self.assertEqual(events[2][1:4], [self.doc.Name, "Box", "Face2"])
        self.assertEqual(events[3][1], self.doc.Name)
        # a guest change fires the guest's observer too (a nested entry)
        self.guest("fcx_selprobe.events.clear()\nfcx_selprobe.select_sub(%r, 'Owner', '')"
                   % self.doc.Name, session=True)
        events = self.read("fcx_selprobe.events")
        self.assertEqual([e[0] for e in events], ["add"])
        self.assertEqual(events[0][2], "Owner")
        # removed: nothing more arrives
        self.guest("fcx_selprobe.unwatch()\nfcx_selprobe.events.clear()", session=True)
        Sel.clearSelection()
        Sel.addSelection(self.box)
        self.assertEqual(self.read("fcx_selprobe.events"), [])

    # ---- the dialogs -----------------------------------------------------

    def drive(self, call, action, timeout=5.0):
        """Run the guest's `call` (a modal op) while `action(dialog)`
        handles the modal on the host from a timer inside the nested
        loop; how it went."""
        from PySide import QtCore, QtWidgets

        outcome = []
        t0 = time.perf_counter()

        def poll():
            w = QtWidgets.QApplication.activeModalWidget()
            if w is None or not w.isVisible():
                if time.perf_counter() - t0 < timeout:
                    QtCore.QTimer.singleShot(30, poll)
                else:
                    outcome.append("no modal")
                return
            try:
                action(w)
                outcome.append("ok")
            except Exception as e:
                outcome.append(repr(e))
                w.reject()

        QtCore.QTimer.singleShot(30, poll)
        self.guest("fcx_selprobe.%s" % call, session=True)
        self.settle()
        sys.stderr.write("SandboxSelection: %s %.3f s\n" % (call, time.perf_counter() - t0))
        return outcome

    def test_c_dialogs(self):
        from PySide import QtGui, QtWidgets

        # QMessageBox.question: the host clicks Yes
        def yes(w):
            self.assertIsInstance(w, QtWidgets.QMessageBox)
            self.assertEqual(w.windowTitle(), "Probe")
            self.assertEqual(w.text(), "Yes or no?")
            w.button(QtWidgets.QMessageBox.StandardButton.Yes).click()

        self.assertEqual(self.drive("ask_question()", yes), ["ok"])
        self.assertEqual(self.read("fcx_selprobe.results['question']"),
                         [int(QtWidgets.QMessageBox.StandardButton.Yes), True])

        # an instance: title, icon, buttons, the Save clicked
        def save(w):
            self.assertIsInstance(w, QtWidgets.QMessageBox)
            self.assertEqual(w.windowTitle(), "Probe box")
            self.assertEqual(w.icon(), QtWidgets.QMessageBox.Icon.Warning)
            self.assertEqual(w.text(), "an instance")
            self.assertEqual(w.defaultButton(),
                             w.button(QtWidgets.QMessageBox.StandardButton.Cancel))
            w.button(QtWidgets.QMessageBox.StandardButton.Save).click()

        self.assertEqual(self.drive("ask_box()", save), ["ok"])
        self.assertEqual(self.read("fcx_selprobe.results['box']"),
                         int(QtWidgets.QMessageBox.StandardButton.Save))

        # QInputDialog.getText: seeded, retyped, accepted
        def typed(w):
            self.assertIsInstance(w, QtWidgets.QInputDialog)
            self.assertEqual(w.labelText(), "Name:")
            self.assertEqual(w.textValue(), "seed")
            w.setTextValue("typed on the host")
            w.accept()

        self.assertEqual(self.drive("ask_text()", typed), ["ok"])
        self.assertEqual(self.read("fcx_selprobe.results['text']"), ["typed on the host", True])

        # getItem: the third chosen; getInt: canceled keeps the seed
        def third(w):
            self.assertEqual(w.comboBoxItems(), ["a", "b", "c"])
            self.assertEqual(w.textValue(), "b")
            w.setTextValue("c")
            w.accept()

        self.assertEqual(self.drive("ask_item()", third), ["ok"])
        self.assertEqual(self.read("fcx_selprobe.results['item']"), ["c", True])

        def cancel(w):
            w.reject()

        self.assertEqual(self.drive("ask_int()", cancel), ["ok"])
        self.assertEqual(self.read("fcx_selprobe.results['int']"), [3, False])

        # QFileDialog: a file picked on the host, its path the answer
        tmp = tempfile.mkdtemp(prefix="fcx-sel-")
        path = os.path.join(tmp, "probe.txt")
        with open(path, "w") as f:
            f.write("probe\n")

        def pick(w):
            self.assertIsInstance(w, QtWidgets.QFileDialog)
            self.assertEqual(w.windowTitle(), "Probe open")
            self.assertEqual(w.nameFilters()[0], "Text (*.txt)")
            # not selectFile: the widget dialog lists its directory
            # asynchronously, and a file not listed yet selects nothing
            w.findChild(QtWidgets.QLineEdit, "fileNameEdit").setText(path)
            w.accept()

        self.assertEqual(self.drive("ask_open(%r)" % tmp, pick), ["ok"])
        got = self.read("fcx_selprobe.results['open']")
        self.assertEqual(os.path.realpath(got[0]), os.path.realpath(path))
        self.assertEqual(got[1], "Text (*.txt)")

        def name_it(w):
            self.assertEqual(w.acceptMode(), QtWidgets.QFileDialog.AcceptMode.AcceptSave)
            w.findChild(QtWidgets.QLineEdit, "fileNameEdit").setText(os.path.join(tmp, "out.txt"))
            w.accept()

        self.assertEqual(self.drive("ask_save(%r)" % tmp, name_it), ["ok"])
        got = self.read("fcx_selprobe.results['save']")
        self.assertEqual(os.path.basename(got[0]), "out.txt")

        # QColorDialog: the initial color shown, another chosen; a
        # cancel is an invalid QColor
        def choose(w):
            self.assertIsInstance(w, QtWidgets.QColorDialog)
            self.assertEqual(w.windowTitle(), "Probe color")
            self.assertEqual(w.currentColor().name(), "#0a141e")
            w.setCurrentColor(QtGui.QColor(1, 2, 3))
            w.accept()

        self.assertEqual(self.drive("ask_color()", choose), ["ok"])
        self.assertEqual(self.read("fcx_selprobe.results['color']"), [True, "#010203"])
        self.assertEqual(self.drive("ask_color_cancel()", cancel), ["ok"])
        self.assertEqual(self.read("fcx_selprobe.results['color_cancel']"), [False])

    def test_d_document_principal_is_refused(self):
        with self.assertRaises(Exception) as cm:
            self.guest("fcx_selprobe.names()")
        self.assertIn("gui", str(cm.exception).lower())
        with self.assertRaises(Exception):
            self.guest("fcx_selprobe.ask_question()")
