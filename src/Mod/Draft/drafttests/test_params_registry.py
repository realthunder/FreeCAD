# SPDX-License-Identifier: LGPL-2.1-or-later
"""Draft's table of settings is described to the settings registry.

draftutils.params describes its table through draftutils.params_registry when
it is loaded. The table gives the name, type and default; the title, the
documentation and the editor come from the preference pages' widgets or are
written in params_registry.DOCS. A setting added to the table without either
fails here.
"""

import ast
import glob
import os
import re
import unittest

import FreeCAD as App
from draftutils import params
from draftutils import params_registry

PREFIX = "User parameter:BaseApp/Preferences/"
OWN = ("Mod/Draft", "Mod/Arch", "Mod/BIM")


def described():
    return {(r["path"], r["entry"]): r for r in App.listParams("Preferences/Mod/")}


class DraftParamsRegistry(unittest.TestCase):
    """The settings of Draft and BIM in the settings registry."""

    def own(self):
        for path, entries in params.PARAM_DICT.items():
            if path.startswith(OWN):
                for entry, (typ, default) in entries.items():
                    yield path, entry, typ, default

    def test_table_is_described(self):
        """Every row of the table that is Draft's or BIM's is in the registry, with the
        table's type and default."""
        rows = described()
        count = 0
        for path, entry, typ, default in self.own():
            count += 1
            row = rows.get((PREFIX + path, entry))
            self.assertIsNotNone(row, path + "/" + entry)
            self.assertEqual(row["type"], params_registry._TYPES[typ], entry)
            text = row["default"]
            if typ == "bool":
                self.assertEqual(text == "true", default, entry)
            elif typ == "int":
                self.assertEqual(int(text), default, entry)
            elif typ == "unsigned":
                self.assertEqual(int(text, 16), default, entry)
            elif typ == "float":
                self.assertAlmostEqual(float(text), default, 9, entry)
            else:
                self.assertEqual(text, default, entry)
        self.assertGreater(count, 350)

    def test_documented(self):
        """Every setting Draft and BIM describe has a title and a documentation of at most
        400 characters."""
        missing = []
        count = 0
        for row in App.listParams():
            if row["namespace"] not in ("Draft", "BIM"):
                continue
            count += 1
            entry = row["entry"]
            if not row["title"] or row["title"] == entry or not 0 < len(row["doc"]) <= 400:
                missing.append("%s/%s (%d)" % (row["path"], entry, len(row["doc"])))
        self.assertEqual(missing, [])
        self.assertGreater(count, 400)

    def test_settings_outside_the_table(self):
        """BIM's settings that the table lacks: those of the NativeIFC page, and those its
        code reads by name -- each of the latter with the default every reader passes."""
        rows = described()
        row = rows[(PREFIX + "Mod/NativeIFC", "ShapeMode")]
        self.assertEqual((row["type"], row["default"], row["proxy"]), ("Int", "1", "ComboBox"))
        self.assertEqual(len(row["items"]), 3)
        self.assertEqual(rows[(PREFIX + "Mod/NativeIFC", "LoadOrphans")]["default"], "true")

        read = re.compile(
            r'\.Get(Bool|Int|Unsigned|Float|String)\(\s*"(\w+)"\s*,\s*([^()]+?)\s*\)', re.S
        )
        found = {}
        folder = os.path.join(App.getHomePath(), "Mod", "BIM")
        for source in glob.glob(os.path.join(folder, "**", "*.py"), recursive=True):
            if "bimtests" in source:
                continue
            with open(source, encoding="utf-8") as f:
                for _kind, name, default in read.findall(f.read()):
                    try:
                        found.setdefault(name, set()).add(ast.literal_eval(default))
                    except (ValueError, SyntaxError):
                        pass
        for path, entries in params_registry.EXTRA.items():
            for entry, (_typ, default, _title, _doc) in entries.items():
                self.assertIn((PREFIX + path, entry), rows)
                self.assertEqual(found.get(entry), {default}, path + "/" + entry)
        # what the C++ DXF code of the Import module reads from Draft's group
        row = rows[(PREFIX + "Mod/Draft", "DxfVersionOut")]
        self.assertEqual((row["type"], row["default"]), ("Int", "14"))
        self.assertEqual(rows[(PREFIX + "Mod/Draft", "ExportPoints")]["default"], "false")
        self.assertEqual(rows[(PREFIX + "Mod/Draft", "dxfUseDraftVisGroups")]["default"], "true")

    def test_written_documentation_is_of_the_table(self):
        """params_registry.DOCS names no setting that neither the table nor a page has."""
        pages = {(row[0], row[1]) for row in params_registry.extra_page_settings()}
        stale = [
            path + "/" + entry
            for path, entries in params_registry.DOCS.items()
            for entry in entries
            if entry not in params.PARAM_DICT.get(path, {}) and (path, entry) not in pages
        ]
        self.assertEqual(stale, [])

    def test_editors_come_from_the_pages(self):
        rows = described()
        row = rows[(PREFIX + "Mod/Draft", "defaultWP")]
        self.assertEqual(row["proxy"], "ComboBox")
        self.assertEqual(
            [item[0] for item in row["items"]],
            ["Automatic", "XY (Top)", "XZ (Front)", "YZ (Side)"],
        )
        self.assertEqual(row["title"], "Default working plane")
        self.assertTrue(row["doc"].startswith("The default working plane"))
        row = rows[(PREFIX + "Mod/Draft", "gridColor")]
        self.assertEqual((row["type"], row["proxy"]), ("Hex", "Color"))
        row = rows[(PREFIX + "Mod/Draft", "gridTransparency")]
        self.assertEqual(row["proxy"], "SpinBox")
        self.assertGreater(row["maximum"], row["minimum"])
        # a setting no page shows, with a choice written for it
        row = rows[(PREFIX + "Mod/Arch", "WallAlignment")]
        self.assertEqual([item[0] for item in row["items"]], ["Center", "Left", "Right"])
        self.assertEqual(row["namespace"], "BIM")

    def test_other_modules_settings_are_left_to_them(self):
        """The table also has settings of other groups that Draft reads; Draft does not
        describe those."""
        for row in App.listParams():
            if row["namespace"] in ("Draft", "BIM"):
                self.assertTrue(
                    row["path"].startswith(
                        tuple(PREFIX + own for own in OWN + ("Mod/NativeIFC",))
                    ),
                    row["path"],
                )
