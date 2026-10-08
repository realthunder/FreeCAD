# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings CAM's Python code reads are described to the settings registry.

CAMPyParams.py describes them and Path/Preferences.py, which reads most of
them, is left as it is, so a reader's default and the described one can
drift apart. The second test holds them together.
"""

import ast
import glob
import os
import re
import unittest

import FreeCAD

import CAMPyParams
import PathApp  # its generated class describes the settings C++ reads

True if PathApp else False

PREFIX = "User parameter:BaseApp/Preferences/Mod/CAM"

# pref.GetBool(Name, default) or pref.GetBool("Name", default): Path/Preferences.py names a
# setting through a constant spelled as the setting is
READ = re.compile(
    r'\.Get(Bool|Int|Unsigned|Float|String)\(\s*"?(\w+)"?\s*,\s*([^()]+?)\s*\)', re.S
)


def value_of(row):
    text = row["default"]
    if row["type"] == "Bool":
        return text == "true"
    if row["type"] in ("Int", "Hex"):
        return int(text, 0)
    if row["type"] == "Float":
        return float(text)
    return text


class TestPathSettingsRegistry(unittest.TestCase):
    def described(self):
        return {
            (r["path"], r["entry"]): r
            for r in FreeCAD.listParams("Preferences/Mod/CAM")
            if r["path"].startswith(PREFIX)
        }

    def test_listed(self):
        """Every setting of the definition file is in the registry as the file describes
        it, and none of them took the place of one the generated class describes."""
        rows = self.described()
        self.assertEqual(len(CAMPyParams.Params), 27)
        for param in CAMPyParams.Params:
            row = rows.get((param.registry_path(PREFIX), param.param_name))
            self.assertIsNotNone(row, param.name)
            self.assertEqual(row["context"], "CAMPyParams", param.name)
            self.assertTrue(row["title"] and 0 < len(row["doc"]) <= 400, param.name)
        row = rows[(PREFIX, "DefaultTaskPanelLayout")]
        self.assertEqual((row["type"], row["default"], row["proxy"]), ("Int", "0", "ComboBox"))
        self.assertEqual(rows[(PREFIX + "/Tools", "ToolUpdateOnLoad")]["default"], "true")
        generated = [r for r in rows.values() if r["context"] == "CAMParams"]
        self.assertEqual(len(generated), 13)

    def test_defaults_are_the_readers(self):
        """Each default described is the one every reader of the setting passes."""
        rows = self.described()
        wanted = {}
        for param in CAMPyParams.Params:
            row = rows[(param.registry_path(PREFIX), param.param_name)]
            wanted[param.param_name] = value_of(row)
        folder = os.path.dirname(CAMPyParams.__file__)
        read = 0
        for source in sorted(glob.glob(os.path.join(folder, "**", "*.py"), recursive=True)):
            if "CAMTests" in source:
                continue
            with open(source, encoding="utf-8") as f:
                for _kind, name, default in READ.findall(f.read()):
                    if name not in wanted:
                        continue
                    try:
                        value = ast.literal_eval(default)
                    except (ValueError, SyntaxError):
                        continue
                    read += 1
                    self.assertEqual(value, wanted[name], "%s in %s" % (name, source))
        self.assertGreater(read, 15)
