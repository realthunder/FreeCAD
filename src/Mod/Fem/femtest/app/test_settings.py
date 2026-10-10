# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings Fem's Python code reads are described to the settings registry.

FemPyParams.py describes them and the code that reads them is left as it is,
so a reader's default and the described one can drift apart. The second
test holds them together.
"""

import ast
import glob
import os
import re
import unittest

import FreeCAD

import Fem  # its generated class describes the settings C++ reads
import FemPyParams

True if Fem else False

PREFIX = "User parameter:BaseApp/Preferences/Mod/Fem"

# prefs.GetBool("Name", default), over more than one line too
READ = re.compile(r'\.Get(Bool|Int|Unsigned|Float|String)\(\s*"(\w+)"\s*,\s*([^()]+?)\s*\)', re.S)


def value_of(row):
    text = row["default"]
    if row["type"] == "Bool":
        return text == "true"
    if row["type"] == "Int":
        return int(text)
    if row["type"] == "Float":
        return float(text)
    return text


class TestSettings(unittest.TestCase):
    def described(self):
        return {
            (r["path"], r["entry"]): r
            for r in FreeCAD.listParams("Preferences/Mod/Fem")
            if r["path"].startswith(PREFIX)
        }

    def test_listed(self):
        """Every setting of the definition file is in the registry as the file describes
        it, and none of them took the place of one the generated class describes."""
        rows = self.described()
        self.assertEqual(len(FemPyParams.Params), 47)
        for param in FemPyParams.Params:
            row = rows.get((param.registry_path(PREFIX), param.param_name))
            self.assertIsNotNone(row, param.name)
            self.assertEqual(row["context"], "FemPyParams", param.name)
            self.assertTrue(row["title"] and 0 < len(row["doc"]) <= 400, param.name)
        row = rows[(PREFIX + "/Ccx", "Solver")]
        self.assertEqual((row["type"], row["default"], row["proxy"]), ("Int", "0", "ComboBox"))
        self.assertEqual(len(row["items"]), 6)
        generated = [r for r in rows.values() if r["context"] == "FemParams"]
        self.assertEqual(len(generated), 8)

    def test_defaults_are_the_readers(self):
        """Each default described is the one every reader of the setting passes."""
        rows = self.described()
        wanted = {}
        for param in FemPyParams.Params:
            row = rows[(param.registry_path(PREFIX), param.param_name)]
            wanted.setdefault(param.param_name, []).append(value_of(row))
        folder = os.path.dirname(FemPyParams.__file__)
        read = 0
        for source in sorted(glob.glob(os.path.join(folder, "**", "*.py"), recursive=True)):
            if "femtest" in source:
                continue
            with open(source, encoding="utf-8") as f:
                for _kind, name, default in READ.findall(f.read()):
                    if name not in wanted:
                        continue
                    try:
                        value = ast.literal_eval(default)
                    except (ValueError, SyntaxError):
                        continue
                    # the same name is a setting of another kind in another group
                    if type(value) is not type(wanted[name][0]):
                        continue
                    read += 1
                    self.assertIn(value, wanted[name], "%s in %s" % (name, source))
        self.assertGreater(read, 30)
