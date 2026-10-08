# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of the small modules are described to the settings registry.

Help, OpenSCAD, ReverseEngineering, Tux and Material each have a definition
file (freecad.params) that their Init.py imports. It describes the settings
and the code that reads them is left as it is, so a reader's default and the
described one can drift apart. The second test holds them together.

A module that was not built is skipped.
"""

import ast
import glob
import importlib
import os
import re
import unittest

import FreeCAD

# definition file -> how many settings it describes, and the directories
# under its own whose sources are tests, not readers
MODULES = {
    "HelpParams": (12, ()),
    "OpenSCADParams": (15, ("OpenSCADTest",)),
    "ReverseEngineeringParams": (11, ()),
    "TuxParams": (5, ()),
    "MaterialPyParams": (4, ("materialtests",)),
}

# .GetBool("Name", default) and .GetBool('Name', default), over more than one line too
READ = re.compile(
    r"\.\s*\\?\s*Get(Bool|Int|Unsigned|Float|String)\(\s*\\?\s*"
    r"[\"'](\w+)[\"']\s*,\s*([^()]+?)\s*\)",
    re.S,
)


def value_of(row):
    text = row["default"]
    if row["type"] == "Bool":
        return text == "true"
    if row["type"] == "Int":
        return int(text)
    if row["type"] == "Float":
        return float(text)
    return text


def definitions():
    found = []
    for name in sorted(MODULES):
        try:
            found.append(importlib.import_module(name))
        except ImportError:
            pass
    return found


class TestModuleSettings(unittest.TestCase):
    def described(self, module):
        return {
            (r["path"], r["entry"]): r
            for r in FreeCAD.listParams()
            if r["context"] == module.ClassName
        }

    def test_listed(self):
        """Every setting of a definition file is in the registry as the file
        describes it."""
        found = definitions()
        self.assertTrue(found)
        for module in found:
            rows = self.described(module)
            name = module.__name__
            self.assertEqual(len(module.Params), MODULES[name][0], name)
            self.assertEqual(len(rows), len(module.Params), name)
            for param in module.Params:
                row = rows.get((param.registry_path(module.ParamPath), param.param_name))
                self.assertIsNotNone(row, "%s %s" % (name, param.name))
                self.assertEqual(row["namespace"], module.NameSpace, param.name)
                self.assertTrue(row["title"] and 0 < len(row["doc"]) <= 400, param.name)

    def test_kinds(self):
        """A sample of what the pages give a description beyond its default."""
        samples = {
            "OpenSCADParams": ("", "transfermechanism", ("Int", "0", "ComboBox")),
            "ReverseEngineeringParams": ("/BSplineFit", "Size factor", ("Float", "1", "SpinBox")),
            "TuxParams": ("/NavigationIndicator", "Compact", ("Bool", "false", "")),
            "HelpParams": ("", "StyleSheet", ("String", "", "File")),
        }
        for module in definitions():
            if module.__name__ not in samples:
                continue
            sub, entry, wanted = samples[module.__name__]
            row = self.described(module)[(module.ParamPath + sub, entry)]
            got = (row["type"], row["default"], row["proxy"])
            if wanted[0] == "Float":
                self.assertEqual((got[0], float(got[1]), got[2]), ("Float", 1.0, "SpinBox"))
            else:
                self.assertEqual(got, wanted, entry)
            if entry == "transfermechanism":
                self.assertEqual(len(row["items"]), 3)
            if entry == "Size factor":
                self.assertEqual((row["minimum"], row["maximum"]), (1.0, 2.0))

    def test_material_keeps_its_class(self):
        """Material's generated class describes the settings C++ reads; none of
        them gave way to one described here."""
        try:
            import Materials
            import MaterialPyParams
        except ImportError:
            self.skipTest("Material is not built")
        self.assertTrue(Materials and MaterialPyParams)
        prefix = MaterialPyParams.ParamPath
        generated = [
            r
            for r in FreeCAD.listParams("Preferences/Mod/Material")
            if r["path"].startswith(prefix) and r["context"] == "MaterialParams"
        ]
        self.assertEqual(len(generated), 23)

    def test_defaults_are_the_readers(self):
        """Each default described is the one every reader of the setting passes."""
        read = {}
        for module in definitions():
            name = module.__name__
            rows = self.described(module)
            wanted = {}
            for param in module.Params:
                row = rows[(param.registry_path(module.ParamPath), param.param_name)]
                wanted.setdefault(param.param_name, []).append((row["type"], value_of(row)))
            folder = os.path.dirname(module.__file__)
            skipped = tuple(os.sep + d + os.sep for d in MODULES[name][1])
            read[name] = 0
            for source in sorted(glob.glob(os.path.join(folder, "**", "*.py"), recursive=True)):
                if source == module.__file__ or any(d in source for d in skipped):
                    continue
                with open(source, encoding="utf-8") as f:
                    text = f.read()
                for kind, entry, default in READ.findall(text):
                    if entry not in wanted:
                        continue
                    try:
                        value = ast.literal_eval(default)
                    except (ValueError, SyntaxError):
                        continue
                    # the same name is a setting of another kind in another group
                    if kind not in [k for k, _v in wanted[entry]]:
                        continue
                    if kind == "Bool":
                        value = bool(value)
                    read[name] += 1
                    self.assertIn(
                        value,
                        [v for k, v in wanted[entry] if k == kind],
                        "%s in %s" % (entry, source),
                    )
        # the dialog of ReverseEngineering is C++: its widgets read, no code names a setting
        for name, least in (
            ("HelpParams", 10),
            ("OpenSCADParams", 12),
            ("TuxParams", 5),
            ("MaterialPyParams", 4),
        ):
            if name in read:
                self.assertGreaterEqual(read[name], least, name)
