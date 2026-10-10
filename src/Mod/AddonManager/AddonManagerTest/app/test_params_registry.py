# SPDX-License-Identifier: LGPL-2.1-or-later
"""The Addon Manager's settings are described to the settings registry.

addonmanager_params_registry describes every row of the defaults file. A
row added to the file without a title and a documentation fails here.
"""

import unittest

import FreeCAD

import addonmanager_params_registry as registry


class TestParamsRegistry(unittest.TestCase):
    MODULE = "test_params_registry"  # file name without extension

    def described(self):
        return {
            r["entry"]: r
            for r in FreeCAD.listParams("Preferences/Addons")
            if r["path"] == registry.PATH
        }

    def test_defaults_file_is_described(self):
        """Every row of the defaults file is in the registry with its default, a title
        and a documentation of at most 400 characters."""
        rows = self.described()
        table = registry.table()
        self.assertGreater(len(table), 35)
        for entry, kind, default in table:
            row = rows.get(entry)
            self.assertIsNotNone(row, entry)
            self.assertEqual(row["type"], kind, entry)
            if kind == "Bool":
                self.assertEqual(row["default"] == "true", default, entry)
            elif kind == "Int":
                self.assertEqual(int(row["default"]), default, entry)
            else:
                self.assertEqual(row["default"], default, entry)
            self.assertTrue(row["title"] and row["title"] != entry, entry)
            self.assertTrue(0 < len(row["doc"]) <= 400, entry)

    def test_written_documentation_is_of_the_file(self):
        names = {entry for entry, _kind, _default in registry.table()}
        self.assertEqual([entry for entry in registry.DOCS if entry not in names], [])
        self.assertEqual([entry for entry in registry.OVERRIDES if entry not in names], [])

    def test_a_choice_has_its_items(self):
        row = self.described()["UpdateFrequencyComboEntry"]
        self.assertEqual((row["type"], row["proxy"]), ("Int", "ComboBox"))
        self.assertEqual([item[0] for item in row["items"]], ["Manual", "Daily", "Weekly"])
        # the proxy switches are switches, whatever kind of value the defaults file has
        self.assertEqual(self.described()["NoProxyCheck"]["type"], "Bool")
