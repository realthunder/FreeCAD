# SPDX-License-Identifier: LGPL-2.1-or-later

#**************************************************************************
#   Copyright (c) 2026 FreeCAD Project Association                        *
#                                                                         *
#   This file is part of the FreeCAD CAx development system.              *
#                                                                         *
#   This program is free software; you can redistribute it and/or modify  *
#   it under the terms of the GNU Lesser General Public License (LGPL)    *
#   as published by the Free Software Foundation; either version 2 of     *
#   the License, or (at your option) any later version.                   *
#   for detail see the LICENCE text file.                                 *
#                                                                         *
#   FreeCAD is distributed in the hope that it will be useful,            *
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#   GNU Library General Public License for more details.                  *
#                                                                         *
#   You should have received a copy of the GNU Library General Public     *
#   License along with FreeCAD; if not, write to the Free Software        *
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
#   USA                                                                   *
#**************************************************************************

"""
Test module for the canonical form of a material card and its content hash.

A card is stored in a document as content-addressed bytes, so identical
content must give identical bytes -- on a different machine, in a different
process, and whatever the reader's preferences say. Everything here is a
guard on that: see docs/MaterialStorage.md sec 4.
"""

import hashlib
import re
import unittest

import FreeCAD
import Materials

STEEL = "92589471-a6cb-4bbc-b748-d425a17dea7d"


class MaterialCanonicalTestCases(unittest.TestCase):
    """Canonical serialization and content hashing"""

    def setUp(self):
        self.MaterialManager = Materials.MaterialManager()
        self.steel = self.MaterialManager.getMaterial(STEEL)

    def testHashIsAHash(self):
        """The hash is SHA-1 hex, matching what the blob store computes"""
        content = self.steel.ContentHash
        self.assertRegex(content, "^[0-9a-f]{40}$")
        self.assertEqual(
            content, hashlib.sha1(self.steel.CanonicalForm.encode("utf-8")).hexdigest()
        )

    def testHashIsStable(self):
        """The same card hashes the same however often it is fetched"""
        again = self.MaterialManager.getMaterial(STEEL)
        self.assertEqual(self.steel.ContentHash, again.ContentHash)
        self.assertEqual(self.steel.CanonicalForm, again.CanonicalForm)

    def testModelsAreSorted(self):
        """
        Model uuids are written in sorted order.

        They live in a QSet, and Qt seeds QHash differently in every process:
        an unsorted write gives the same card a different hash on every run,
        which destroys sharing and leaves a duplicate blob behind each time.
        This cannot be caught in one process, so the order is asserted
        directly.
        """
        blocks = self._blocks(self.steel.CanonicalForm)
        self.assertTrue(blocks)
        self.assertTrue(any(len(block) > 1 for block in blocks))
        for block in blocks:
            self.assertEqual(block, sorted(block))

    @staticmethod
    def _blocks(canonical):
        """The model uuid lists, one per Models:/AppearanceModels: block"""
        blocks = []
        current = None
        for line in canonical.splitlines():
            if line in ("Models:", "AppearanceModels:"):
                current = []
                blocks.append(current)
            elif current is not None and re.match(r"^  \S", line):
                current.append(line.strip().rstrip(":"))
            elif line and not line.startswith(" "):
                current = None
        return blocks

    def testProvenanceIsNotHashed(self):
        """
        Renaming a card, or getting it from another library, is not a
        content change: the display name travels on the property.
        """
        before = self.steel.ContentHash
        self.steel.Name = "Not Steel At All"
        self.steel.Author = "Nobody"
        self.steel.License = "CC0"
        self.assertEqual(self.steel.ContentHash, before)
        self.assertNotIn("Not Steel At All", self.steel.CanonicalForm)
        self.assertNotIn(STEEL, self.steel.CanonicalForm)

    def testValueChangesTheHash(self):
        """An edited value is different content"""
        before = self.steel.ContentHash
        self.steel.setPhysicalValue("Density", "1234.0 kg/m^3")
        self.assertNotEqual(self.steel.ContentHash, before)

    def testDescriptionIsHashed(self):
        """
        Description, source and tags have nowhere else to travel, so they are
        part of the content rather than provenance.
        """
        before = self.steel.ContentHash
        self.steel.Description = "A description this card did not have"
        self.assertNotEqual(self.steel.ContentHash, before)

    def testUnitSchemaDoesNotChangeTheHash(self):
        """
        The reader's unit schema is a preference. Quantities go into the
        canonical form in internal units at full precision, so a user working
        in imperial units hashes the same card to the same bytes -- and no
        value is rounded to that schema's decimal count on the way.
        """
        schema = FreeCAD.Units.getSchema()
        try:
            hashes = set()
            forms = set()
            for candidate in range(len(FreeCAD.Units.listSchemas())):
                FreeCAD.Units.setSchema(candidate)
                material = self.MaterialManager.getMaterial(STEEL)
                hashes.add(material.ContentHash)
                forms.add(material.CanonicalForm)
            self.assertEqual(len(hashes), 1)
            self.assertEqual(len(forms), 1)
        finally:
            FreeCAD.Units.setSchema(schema)

    def testCanonicalFormIsLoadableYaml(self):
        """The canonical form is ordinary .FCMat YAML, not a private format"""
        try:
            import yaml
        except ImportError:
            self.skipTest("PyYAML not available")

        parsed = yaml.safe_load(self.steel.CanonicalForm)
        self.assertIn("General", parsed)
        self.assertIn("UUID", parsed["General"])
        self.assertIn("Models", parsed)
        for uuid, model in parsed["Models"].items():
            self.assertEqual(uuid, model["UUID"])
