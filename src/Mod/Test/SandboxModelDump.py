# SPDX-License-Identifier: LGPL-2.1-or-later
"""The widget model dump (docs/Sandbox.md 7.18 (c)): the script that
turns the guest's `freecad.widgets` models into JSON for a backend
that renders them elsewhere, run on the tree and checked for shape.
No GUI, no guest: the script reads the models with `ast`."""

import json
import os
import shutil
import subprocess
import sys
import unittest

import FreeCAD


def _script():
    home = FreeCAD.getHomePath()
    candidates = [
        os.path.join(home, "Tools", "bindings", "dumpWidgetModels.py"),
        # a dev tree: <root>/build/<preset>/ beside <root>/src
        os.path.join(home, "..", "..", "src", "Tools", "bindings", "dumpWidgetModels.py"),
    ]
    src = os.environ.get("FREECAD_SOURCE_DIR")
    if src:
        candidates.insert(0, os.path.join(src, "src", "Tools", "bindings", "dumpWidgetModels.py"))
    here = os.path.dirname(os.path.abspath(__file__))
    candidates.append(os.path.join(here, "..", "..", "Tools", "bindings", "dumpWidgetModels.py"))
    for c in candidates:
        if os.path.exists(c):
            return os.path.normpath(c)
    return None


def _python():
    """A plain interpreter: under FreeCADCmd sys.executable is FreeCAD."""
    for candidate in (
        os.path.join(sys.base_prefix, "bin", "python3"),
        os.path.join(sys.base_prefix, "python.exe"),
        shutil.which("python3"),
        shutil.which("python"),
    ):
        if candidate and os.path.exists(candidate):
            return candidate
    return None


def _root(script):
    return os.path.normpath(
        os.path.join(
            os.path.dirname(script),
            "..", "..", "App", "ExpressionImage", "widgets", "freecad", "widgets",
        )
    )


class SandboxModelDump(unittest.TestCase):
    def setUp(self):
        self.script = _script()
        if not self.script or not os.path.isdir(_root(self.script)):
            self.skipTest("the source tree is not beside this build")
        self.python = _python()
        if not self.python:
            self.skipTest("no plain Python interpreter found")
        out = subprocess.run(
            [self.python, self.script, "--root", _root(self.script)],
            capture_output=True,
            text=True,
            check=True,
        )
        self.dump = json.loads(out.stdout)

    def test_header(self):
        self.assertEqual(self.dump["module"], "freecad.widgets")
        self.assertEqual(self.dump["prefix"], "q_")
        self.assertTrue(self.dump["version"])

    def test_action_model(self):
        """QActionModel: its own traits, the base, the signals."""
        action = self.dump["classes"]["QActionModel"]
        self.assertEqual(action["python"], "QAction")
        self.assertEqual(action["qtClass"], "QAction")
        self.assertEqual(action["base"], "QWidgetModel")
        props = action["properties"]
        self.assertEqual(props["text"], {"type": "string", "default": ""})
        self.assertEqual(props["checkable"], {"type": "bool", "default": False})
        self.assertEqual(props["commandIndex"], {"type": "int", "default": 0})
        self.assertEqual(action["signals"]["triggered"], ["bool"])
        self.assertEqual(action["signals"]["hovered"], [])

    def test_base_chain(self):
        """Every model's base chain ends at QWidgetModel, which has none."""
        classes = self.dump["classes"]
        root = classes["QWidgetModel"]
        self.assertIsNone(root["base"])
        self.assertIn("visible", root["properties"])
        self.assertIn("layoutSpec", root["properties"])
        for name, rec in classes.items():
            seen = []
            base = rec["base"]
            while base is not None:
                self.assertIn(base, classes, (name, base))
                self.assertNotIn(base, seen, name)
                seen.append(base)
                base = classes[base]["base"]
            if name != "QWidgetModel":
                self.assertEqual(seen[-1], "QWidgetModel", name)

    def test_folded_bases(self):
        """A base without a model of its own (QAbstractItemView,
        QAbstractButton) folds into its model subclasses."""
        classes = self.dump["classes"]
        self.assertIn("checkable", classes["QPushButtonModel"]["properties"])
        self.assertIn("checkable", classes["QCheckBoxModel"]["properties"])
        tree = classes["QTreeWidgetModel"]
        self.assertEqual(tree["base"], "QWidgetModel")
        self.assertGreater(len(tree["properties"]), 10)

    def test_qt_classes(self):
        """The CLASSES map names dumped models only, and covers the
        preference widgets by their base model."""
        classes = self.dump["classes"]
        qt = self.dump["qtClasses"]
        for qt_name, model in qt.items():
            self.assertIn(model, classes, qt_name)
        self.assertEqual(qt["QDialog"], "QDialogModel")
        self.assertEqual(qt["Gui::PrefCheckBox"], "QCheckBoxModel")
        self.assertEqual(qt["Gui::ToolBar"], "QToolBarModel")
        self.assertEqual(qt["QListView"], "QListViewModel")

    def test_deterministic(self):
        """The same tree dumps the same text."""
        out = subprocess.run(
            [self.python, self.script, "--root", _root(self.script)],
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(json.loads(out.stdout), self.dump)
