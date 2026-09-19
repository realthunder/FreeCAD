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

"""The 41-panel harness (docs/Sandbox.md 7.11, G3b): every `.ui` file
Draft and BIM load with `loadUi` (41 call sites, 40 distinct files --
one site is Draft's generic `loadUi(ui_file)` helper) opens from the
sandbox guest and round-trips its fields.

For each file the guest loads it, the host realizes it -- a QDialog
root as a window of its own (`form.show()`), a QWidget root as a task
panel -- and binds uic's widgets to the guest's models.  Then, per
widget class, a value set on the guest is read from the real widget
and a value set on the real widget is read back on the guest: text,
numbers, checks, combos, tabs, the item views (rows from the guest,
the selection from the host), the file chooser, the button box.  One
file exercises `exec_()`: the nested event loop returns the dialog
code the host's OK produced.

Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Skips headless, on a build without the sandbox host, when the image
cannot boot, and when the forms' wheels are not bundled."""

import sys
import time
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"

# the 40 `.ui` files (all in the Draft and Arch resource files)
DRAFT_UI = [
    "preferences-dxf-import", "TaskShapeString", "TaskPanel_PolarArray", "TaskSelectPlane",
    "TaskPanel_OrthoArray", "TaskPanel_CircularArray", "dialogHatch", "TaskPanel_SetStyle",
    "dialogLayers",
]
BIM_UI = [
    "ArchMaterial", "ArchMultiMaterial", "dialogIfcPropertiesRedux", "ArchSchedule",
    "dialogNudgeValue", "dialogDiff", "dialogConvertType", "dialogImport",
    "dialogCreateProject", "dialogTree", "dialogExport", "dialogAddProperty", "dialogAddPSet",
    "dialogLibrary", "dialogLayersIFC", "dialogWelcome", "ArchNest", "dialogClassification",
    "dialogListWidget", "dialogIfcElements", "dialogPreflight", "dialogPreflightResults",
    "dialogViews", "dialogSetup", "dialogIfcProperties", "dialogIfcQuantities", "dialogReorder",
    "dialogTutorial", "dialogWindows", "dialogMaterialChooser", "dialogProjectManager",
]

# The guest side of the harness: load, show, and the per-class probes.
GUEST = r'''
import FreeCADGui as Gui
from PySide import QtCore, QtGui, QtWidgets

forms = {}
events = []
result = None


class Panel:
    def __init__(self, form):
        self.form = form

    def accept(self):
        return True

    def reject(self):
        return True


def classes(form):
    """{name: (model class, qtClass)} of the named widgets."""
    return {name: (type(w).__name__, w.qtClass) for name, w in form.widgets.items()}


def load(path):
    form = Gui.PySideUic.loadUi(path)
    forms[path] = form
    return {"root": form.qtClass, "id": form.model_id, "widgets": classes(form)}


def show(path):
    form = forms[path]
    if form.qtClass == "QDialog":
        form.show()
    else:
        Gui.Control.showDialog(Panel(form))


def close(path):
    form = forms[path]
    if form.qtClass == "QDialog":
        form.hide()
    else:
        Gui.Control.closeDialog()


def set_field(path, name, kind):
    """A value of the field's kind, set from the guest; the host reads it."""
    w = forms[path].widgets[name]
    del events[:]
    if kind == "text":
        w.setText("guest:" + name)
    elif kind == "plain":
        w.setPlainText("guest:" + name)
    elif kind == "int":
        w.setValue(int(w.q_maximum) if int(w.q_maximum) < 7 else 7)
    elif kind == "double":
        w.setValue(2.5)
    elif kind == "quantity":
        w.setProperty("rawValue", 12.5)
    elif kind == "check":
        w.setChecked(True)
    elif kind == "combo":
        w.addItem("guest item")
        w.setCurrentIndex(w.count() - 1)
    elif kind == "tabs":
        w.setCurrentIndex(w.count() - 1)
    elif kind == "list":
        w.addItem("row one")
        w.addItem(QtWidgets.QListWidgetItem("row two"))
        w.setCurrentRow(1)
    elif kind == "tree":
        top = QtWidgets.QTreeWidgetItem(["top", "t1"])
        top.addChild(QtWidgets.QTreeWidgetItem(["child", "c1"]))
        w.addTopLevelItem(top)
        w.addTopLevelItem(QtWidgets.QTreeWidgetItem(["second"]))
        top.setExpanded(True)
        w.setCurrentItem(top)
        w.itemSelectionChanged.connect(lambda: events.append([name, "sel"]))
    elif kind == "table":
        w.setColumnCount(max(w.columnCount(), 2))
        w.setRowCount(2)
        w.setItem(0, 0, QtWidgets.QTableWidgetItem("cell00"))
        w.setItem(1, 1, QtWidgets.QTableWidgetItem("cell11"))
        w.setCurrentCell(1, 1)
    elif kind == "view":
        model = QtGui.QStandardItemModel()
        model.setHorizontalHeaderLabels(["Name", "Value"])
        w.setModel(model)
        row = [QtGui.QStandardItem("first"), QtGui.QStandardItem("v1")]
        model.appendRow(row)
        row[0].appendRow([QtGui.QStandardItem("nested"), QtGui.QStandardItem("nv")])
        model.appendRow([QtGui.QStandardItem("second"), QtGui.QStandardItem("v2")])
        w.expandAll()
        w.selectionModel().selectionChanged.connect(
            lambda a, b: events.append([name, "sel"]))
    elif kind == "file":
        w.setFileName("/tmp/guest-font.ttf")
    elif kind == "buttons":
        w.accepted.connect(lambda: events.append([name, "accepted"]))
        w.clicked.connect(lambda b: events.append([name, "clicked"]))
    elif kind == "splitter":
        w.setSizes([70, 30])
    elif kind == "scroll":
        w.setWidgetResizable(True)


def read_field(path, name, kind):
    """What the guest sees after the host changed the field."""
    w = forms[path].widgets[name]
    if kind == "text":
        return w.text()
    if kind == "plain":
        return w.toPlainText()
    if kind in ("int", "double"):
        return w.value()
    if kind == "quantity":
        return w.property("rawValue")
    if kind == "check":
        return w.isChecked()
    if kind in ("combo", "tabs"):
        return w.currentIndex()
    if kind == "list":
        return [w.currentRow(), [i.text() for i in w.selectedItems()], w.count()]
    if kind == "tree":
        return [[i.text(0) for i in w.selectedItems()], w.currentItem().text(0),
                w.topLevelItemCount(), w.topLevelItem(0).childCount(),
                w.topLevelItem(0).text(1)]
    if kind == "table":
        return [w.currentRow(), w.currentColumn(), w.item(0, 0).text(), w.rowCount()]
    if kind == "view":
        sel = w.selectedIndexes()
        return [[i.data() for i in sel if i.column() == 0], w.model().rowCount(),
                w.model().item(0, 0).rowCount(), w.model().item(0, 0).child(0).text(),
                w.model().item(1, 1).text()]
    if kind == "file":
        return w.fileName()
    if kind == "buttons":
        return [e for e in events if e[0] == name]
    if kind == "splitter":
        return w.sizes()
    if kind == "scroll":
        return w.widgetResizable()
    return None


def exec_dialog(path):
    return forms[path].exec_()
'''

# the field kinds by Qt class, as the .ui files spell them
KINDS = {
    "QLineEdit": "text", "Gui::PrefLineEdit": "text", "QLabel": "text",
    "QTextEdit": "plain", "QPlainTextEdit": "plain", "Gui::PrefTextEdit": "plain",
    "QSpinBox": "int", "Gui::PrefSpinBox": "int",
    "QDoubleSpinBox": "double", "Gui::PrefDoubleSpinBox": "double", "Gui::DoubleSpinBox": "double",
    "Gui::InputField": "quantity", "Gui::QuantitySpinBox": "quantity",
    "Gui::PrefQuantitySpinBox": "quantity", "Gui::PrefUnitSpinBox": "quantity",
    "QCheckBox": "check", "Gui::PrefCheckBox": "check",
    "QComboBox": "combo", "Gui::PrefComboBox": "combo",
    "QTabWidget": "tabs",
    "QListWidget": "list", "QTreeWidget": "tree", "QTableWidget": "table", "QTreeView": "view",
    "Gui::FileChooser": "file", "QDialogButtonBox": "buttons",
    "QSplitter": "splitter", "QScrollArea": "scroll",
}
# the model class a Qt class must have come out as
MODEL_OF = {
    "QDialog": "QDialog", "QDialogButtonBox": "QDialogButtonBox", "QTabWidget": "QTabWidget",
    "QScrollArea": "QScrollArea", "QSplitter": "QSplitter", "Gui::FileChooser": "FileChooser",
    "QTreeWidget": "QTreeWidget", "QListWidget": "QListWidget", "QTableWidget": "QTableWidget",
    "QTreeView": "QTreeView", "QLineEdit": "QLineEdit", "QSpinBox": "QSpinBox",
    "QCheckBox": "QCheckBox", "QComboBox": "QComboBox", "QLabel": "QLabel",
}


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


class SandboxPanelsTest(unittest.TestCase):
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
        self.doc = FreeCAD.newDocument("SandboxPanels")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        try:
            self.S.exec("import freecad.widgets.models, freecad.widgets.uic", "fcx_panelcheck")
        except Exception as e:
            self.tearDown()
            self.skipTest("the forms are not bundled for the guest: %s" % str(e)[:300])
        import Draft_rc  # noqa: F401
        import Arch_rc  # noqa: F401

        # the guest module once per process: an evaluate's namespace keeps
        # the module object it first imported, so a re-exec'd module would
        # be read stale; a test starts from cleared state instead
        try:
            self.do("fcx_panels.forms.clear()\nfcx_panels.events.clear()\n"
                    "fcx_panels.result = None\n")
        except Exception:
            self.S.exec(GUEST, "fcx_panels")

    def tearDown(self):
        import FreeCADGui

        try:
            if FreeCADGui.Control.activeDialog():
                FreeCADGui.Control.closeDialog()
            from freecad import widgets

            widgets.manager().reset()
            FreeCADGui.FormWidgets.reset()
        finally:
            FreeCAD.closeDocument(self.doc.Name)
            self.prefs.restore()

    def do(self, code):
        """Run in the guest as the session (the GUI ops are its)."""
        self.S.exec("import fcx_panels\n" + code)

    def get(self, expr):
        """Read guest state (a document principal may read; it may not
        touch the GUI, which is why the actions go through `do`)."""
        return self.S.evaluate(self.owner, "import fcx_panels; " + expr, self.opts)

    @staticmethod
    def expected(path):
        """The named widgets of the file, parsed here: {name: class}."""
        import xml.etree.ElementTree as ET
        from PySide import QtCore

        f = QtCore.QFile(path)
        f.open(QtCore.QIODevice.ReadOnly)
        text = bytes(f.readAll().data()).decode("utf-8")
        f.close()
        root = ET.fromstring(text)
        top = root.find("widget")
        out = {}
        for w in top.iter("widget"):
            if w is top:
                continue
            out[w.get("name")] = w.get("class")
        return top.get("class"), out

    def check_host(self, widget, kind, name):
        """The real widget after the guest set the field; then a host
        change, returning what the guest must now read."""
        from PySide import QtCore, QtWidgets

        if kind == "text":
            self.assertEqual(widget.text(), "guest:" + name, name)
            if not isinstance(widget, QtWidgets.QLineEdit):
                # a label has no user edit: its text flows one way
                return "guest:" + name
            widget.setText("host:" + name)
            widget.textEdited.emit(widget.text())
            return "host:" + name
        if kind == "plain":
            self.assertEqual(widget.toPlainText(), "guest:" + name, name)
            widget.setPlainText("host:" + name)
            return "host:" + name
        if kind == "int":
            self.assertIn(widget.value(), (7, widget.maximum()), name)
            v = 3 if widget.maximum() >= 3 else widget.maximum()
            widget.setValue(v)
            return v
        if kind == "double":
            self.assertAlmostEqual(widget.value(), 2.5, msg=name)
            widget.setValue(1.25)
            return 1.25
        if kind == "quantity":
            self.assertAlmostEqual(widget.property("rawValue"), 12.5, msg=name)
            widget.setProperty("rawValue", 40.0)
            return 40.0
        if kind == "check":
            self.assertTrue(widget.isChecked(), name)
            widget.click()
            return False
        if kind == "combo":
            self.assertEqual(widget.itemText(widget.count() - 1), "guest item", name)
            self.assertEqual(widget.currentIndex(), widget.count() - 1, name)
            widget.setCurrentIndex(0)
            return 0
        if kind == "tabs":
            self.assertEqual(widget.currentIndex(), widget.count() - 1, name)
            widget.setCurrentIndex(0)
            return 0
        if kind == "list":
            self.assertEqual(widget.count(), 2, name)
            self.assertEqual(widget.item(1).text(), "row two", name)
            self.assertEqual(widget.currentRow(), 1, name)
            widget.setCurrentRow(0)
            return [0, ["row one"], 2]
        if kind == "tree":
            self.assertEqual(widget.topLevelItemCount(), 2, name)
            top = widget.topLevelItem(0)
            self.assertEqual(top.text(1), "t1", name)
            self.assertEqual(top.childCount(), 1, name)
            self.assertEqual(top.child(0).text(0), "child", name)
            self.assertTrue(top.isExpanded(), name)
            self.assertIs(widget.currentItem(), top, name)
            widget.setCurrentItem(top.child(0))
            return [["child"], "child", 2, 1, "t1"]
        if kind == "table":
            self.assertEqual(widget.rowCount(), 2, name)
            self.assertEqual(widget.item(0, 0).text(), "cell00", name)
            self.assertEqual(widget.item(1, 1).text(), "cell11", name)
            self.assertEqual((widget.currentRow(), widget.currentColumn()), (1, 1), name)
            widget.setCurrentCell(0, 0)
            return [0, 0, "cell00", 2]
        if kind == "view":
            model = widget.model()
            self.assertEqual(model.rowCount(), 2, name)
            self.assertEqual(model.columnCount(), 2, name)
            self.assertEqual(model.headerData(1, QtCore.Qt.Horizontal), "Value", name)
            first = model.item(0, 0)
            self.assertEqual(first.rowCount(), 1, name)
            self.assertEqual(first.child(0, 1).text(), "nv", name)
            self.assertTrue(widget.isExpanded(model.index(0, 0)), name)
            widget.setCurrentIndex(model.index(1, 0))
            return [["second"], 2, 1, "nested", "v2"]
        if kind == "file":
            # a FreeCAD widget class PySide never met: its Q_PROPERTYs
            self.assertEqual(widget.property("fileName"), "/tmp/guest-font.ttf", name)
            widget.setProperty("fileName", "/tmp/host-font.ttf")
            return "/tmp/host-font.ttf"
        if kind == "buttons":
            ok = widget.button(QtWidgets.QDialogButtonBox.Ok)
            if ok is None:
                return []
            ok.click()
            return [[name, "clicked"], [name, "accepted"]]
        if kind == "splitter":
            return widget.sizes()
        if kind == "scroll":
            self.assertTrue(widget.widgetResizable(), name)
            return True
        return None

    def open_panel(self, path):
        import FreeCADGui as Gui
        from PySide import QtWidgets

        FW = Gui.FormWidgets
        root_class, expected = self.expected(path)
        t0 = time.perf_counter()
        self.do("fcx_panels.result = fcx_panels.load(%r)" % path)
        info = self.get("fcx_panels.result")
        t_load = time.perf_counter() - t0
        self.assertEqual(info["root"], root_class, path)
        # every named widget of a class the layer knows came out as that
        # class's model (an unknown class is a QWidget, and the gap shows)
        for name, cls in expected.items():
            if not name:
                continue
            self.assertIn(name, info["widgets"], "%s: %s missing" % (path, name))
            model, qt_class = info["widgets"][name]
            self.assertEqual(qt_class, cls, "%s: %s" % (path, name))
            if cls in MODEL_OF:
                self.assertEqual(model, MODEL_OF[cls], "%s: %s" % (path, name))
        # shown: a dialog as a window, a widget as a task panel; every
        # named widget bound to uic's
        t0 = time.perf_counter()
        self.do("fcx_panels.show(%r)" % path)
        t_show = time.perf_counter() - t0
        root = FW.info(info["id"])
        self.assertTrue(root["bound"], path)
        w = FW.widget(info["id"])
        self.assertIsNotNone(w, path)
        if root_class == "QDialog":
            self.assertIsInstance(w, QtWidgets.QDialog, path)
            self.assertTrue(w.isWindow(), path)
            self.assertTrue(w.isVisible(), path)
        else:
            self.assertTrue(Gui.Control.activeDialog(), path)
        unbound = [n for n, ref in root["widgets"].items() if not FW.info(ref)["bound"]]
        self.assertEqual(unbound, [], path)
        # the round trips, one field of each kind the file has
        done = set()
        for name, cls in expected.items():
            kind = KINDS.get(cls)
            if kind is None or kind in done or not name:
                continue
            if kind == "buttons" and root_class != "QDialog":
                continue
            done.add(kind)
            widget = FW.widget(root["widgets"][name])
            self.assertIsNotNone(widget, "%s: %s" % (path, name))
            self.do("fcx_panels.set_field(%r, %r, %r)" % (path, name, kind))
            expect = self.check_host(widget, kind, name)
            got = self.get("fcx_panels.read_field(%r, %r, %r)" % (path, name, kind))
            if isinstance(expect, float):
                self.assertAlmostEqual(got, expect, msg="%s: %s" % (path, name))
            else:
                self.assertEqual(got, expect, "%s: %s (%s)" % (path, name, kind))
        # (a file's own buttonBox -> accept connection, when it has one,
        # hid the dialog on OK; the guest's hide is idempotent)
        self.do("fcx_panels.close(%r)" % path)
        if root_class == "QDialog":
            self.assertFalse(w.isVisible(), path)
        else:
            self.assertFalse(Gui.Control.activeDialog(), path)
        return t_load, t_show, sorted(done)

    def test_the_41_loadUi_panels(self):
        import FreeCADGui as Gui

        paths = [":/ui/%s.ui" % n for n in DRAFT_UI + BIM_UI]
        self.assertEqual(len(paths), 40)
        kinds = set()
        t_total = time.perf_counter()
        for path in paths:
            t_load, t_show, done = self.open_panel(path)
            kinds.update(done)
            sys.stderr.write("SandboxPanels: %-32s load %.3f s  show %.3f s  %s\n"
                             % (path[5:], t_load, t_show, ",".join(done)))
        sys.stderr.write("SandboxPanels: 40 panels in %.1f s, %d objects in the store, kinds %s\n"
                         % (time.perf_counter() - t_total, Gui.FormWidgets.count(),
                            ",".join(sorted(kinds))))
        for kind in ("text", "int", "quantity", "check", "combo", "tabs", "list", "tree",
                     "table", "view", "file", "buttons", "splitter", "scroll"):
            self.assertIn(kind, kinds)

    def test_exec_returns_the_dialog_code(self):
        """`exec_()` from the guest: the host runs the dialog modally in
        a nested loop, a timer clicks OK inside it, the code comes back."""
        import FreeCADGui as Gui
        from PySide import QtCore, QtWidgets

        FW = Gui.FormWidgets
        path = ":/ui/dialogNudgeValue.ui"
        self.do("fcx_panels.result = fcx_panels.load(%r)" % path)
        info = self.get("fcx_panels.result")
        self.do("fcx_panels.forms[%r].accepted.connect("
                "lambda: fcx_panels.events.append(['exec', 'accepted']))" % path)
        state = {}

        def click_ok():
            w = FW.widget(info["id"])
            state["visible"] = w is not None and w.isVisible() and w.isModal()
            box = w.findChild(QtWidgets.QDialogButtonBox, "buttonBox")
            box.button(QtWidgets.QDialogButtonBox.Ok).click()

        QtCore.QTimer.singleShot(200, click_ok)
        t0 = time.perf_counter()
        self.do("fcx_panels.result = fcx_panels.exec_dialog(%r)" % path)
        code = self.get("fcx_panels.result")
        sys.stderr.write("SandboxPanels: exec_ %.3f s\n" % (time.perf_counter() - t0))
        self.assertEqual(code, 1)
        self.assertTrue(state.get("visible"), state)
        self.assertIn(["exec", "accepted"], self.get("fcx_panels.events"))
        self.assertEqual(self.get("fcx_panels.forms[%r].result()" % path), 1)
        self.assertFalse(FW.widget(info["id"]).isVisible())
