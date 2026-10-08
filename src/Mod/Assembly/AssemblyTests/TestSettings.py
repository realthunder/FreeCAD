# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings Assembly's Python code reads are described to the settings registry.

AssemblyPyParams.py describes them and the code that reads them is left as it
is, so two things can drift apart: a reader's default and the described one.
The second test holds them together.
"""

import ast
import glob
import os
import re
import unittest

import FreeCAD as App

import AssemblyApp  # its generated class describes the settings C++ reads
import AssemblyPyParams

True if AssemblyApp else False

PATH = "User parameter:BaseApp/Preferences/Mod/Assembly"

# pref.GetBool("Name", default), over more than one line too
READ = re.compile(r'\.Get(Bool|Int|Unsigned|Float|String)\(\s*"(\w+)"\s*,\s*([^()]+?)\s*\)', re.S)


def described():
    names = [p.param_name for p in AssemblyPyParams.Params]
    rows = {r["entry"]: r for r in App.listParams("Preferences/Mod/Assembly") if r["path"] == PATH}
    return names, rows


def value_of(row):
    text = row["default"]
    if row["type"] == "Bool":
        return text == "true"
    if row["type"] in ("Int", "UInt", "Hex"):
        return int(text, 0)
    if row["type"] == "Float":
        return float(text)
    return text


class TestSettings(unittest.TestCase):
    def test_listed(self):
        """Every setting of the definition file is in the registry, as described."""
        names, rows = described()
        self.assertEqual(len(names), 13)
        for name in names:
            self.assertIn(name, rows)
            self.assertEqual(rows[name]["namespace"], "Assembly")
            self.assertEqual(rows[name]["context"], "AssemblyPyParams")
        row = rows["GroundFirstPart"]
        self.assertEqual((row["type"], row["default"], row["proxy"]), ("Int", "0", "ComboBox"))
        self.assertEqual([i[0] for i in row["items"]], ["Ask", "Always", "Never"])
        row = rows["StepLineColor"]
        self.assertEqual(
            (row["type"], row["default"], row["proxy"]), ("Hex", "0xCC333300", "Color")
        )
        # the generated class's are not described a second time
        self.assertEqual(rows["LeaveEditWithEscape"]["context"], "AssemblyParams")

    def test_defaults_are_the_readers(self):
        """Each default described is the one every reader of the setting passes."""
        names, rows = described()
        read = {}
        folder = os.path.dirname(AssemblyPyParams.__file__)
        for source in sorted(glob.glob(os.path.join(folder, "*.py"))):
            with open(source, encoding="utf-8") as f:
                for _kind, name, default in READ.findall(f.read()):
                    if name in names:
                        where = os.path.basename(source)
                        read.setdefault(name, []).append((where, ast.literal_eval(default)))
        for name in names:
            self.assertIn(name, read, name + " is described and nothing reads it")
            for where, default in read[name]:
                self.assertEqual(default, value_of(rows[name]), "%s in %s" % (name, where))
