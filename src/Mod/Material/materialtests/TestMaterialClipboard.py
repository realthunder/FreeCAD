# SPDX-License-Identifier: LGPL-2.1-or-later
# ***************************************************************************
# *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>              *
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
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************

"""
Test module for Copy Material / Paste Material (docs/MaterialStorage.md
17.12, item 4): a material as a self-contained clipboard payload -- the
card with the files its shader graph refers to, and the look -- pasted
onto objects in other documents, whole or onto faces.
"""

import os
import unittest

import FreeCAD
import Materials

from materialtests.TestShaderGraph import ShaderGraphTestCases, GRAPH, PIXELS

RED = (1.0, 0.0, 0.0, 1.0)


class MaterialClipboardTestCases(ShaderGraphTestCases):
    """Pack on one object, apply on another"""

    def boxWithLook(self, doc, name, card=None):
        obj = doc.addObject("Part::Box", name)
        if card is not None:
            obj.ShapeMaterial = card
        # The look the view provider would hold, as a property of the
        # object's own so the headless test can reach it
        obj.addProperty("App::PropertyAppearanceList", "Look")
        obj.Look = [FreeCAD.Material(DiffuseColor=(0.5, 0.5, 0.5, 1.0))] * 6
        doc.recompute()
        return obj

    def testThePayloadCarriesTheCardAndItsFiles(self):
        doc = FreeCAD.newDocument("ClipboardSource")
        self.docs.append(doc.Name)
        source = self.boxWithLook(doc, "Source", self.libraryCard())
        data = Materials.packMaterial(source, "ShapeMaterial", "Look")
        self.assertIsInstance(data, bytes)
        self.assertTrue(data.startswith(b"FreeCAD Material 1\n"))
        self.assertIn(GRAPH.encode("utf-8"), data)
        self.assertIn(PIXELS, data)
        self.assertEqual(Materials.materialClipboardMimeType(), "application/x-freecad-material")
        # nothing to carry is nothing; a bare box is not that, it wears the
        # Default card
        bare = doc.addObject("Part::Box", "Bare")
        self.assertEqual(Materials.packMaterial(bare, "", ""), b"")
        self.assertNotEqual(Materials.packMaterial(bare, "ShapeMaterial"), b"")

    def testPasteAcrossDocumentsWearsTheCardAndBringsTheFiles(self):
        source_doc = FreeCAD.newDocument("ClipboardSource")
        self.docs.append(source_doc.Name)
        card = self.libraryCard()
        source = self.boxWithLook(source_doc, "Source", card)
        data = Materials.packMaterial(source, "ShapeMaterial", "Look")

        target_doc = FreeCAD.newDocument("ClipboardTarget")
        self.docs.append(target_doc.Name)
        target = self.boxWithLook(target_doc, "Target")
        self.assertTrue(Materials.applyMaterial(target, data, "ShapeMaterial", "Look"))
        self.assertEqual(target.ShapeMaterial.UUID, card.UUID)
        self.assertEqual(target.ShapeMaterial.Name, "Checker")
        # the graph's bytes came along: materializing on the target reads them
        binding = Materials.materializeShaderGraph(target, "ShapeMaterial")
        self.assertIsNotNone(binding)
        program = binding.ElementList[0].Programs[0]
        self.assertEqual(program.FragmentProgram, GRAPH)
        with open(program.Images["checker.png"], "rb") as f:
            self.assertEqual(f.read(), PIXELS)

    def testACustomLookPastesWholeAndAFaceSelectionPastesAnOverride(self):
        source_doc = FreeCAD.newDocument("ClipboardSource")
        self.docs.append(source_doc.Name)
        source = self.boxWithLook(source_doc, "Source")
        red = FreeCAD.Material(DiffuseColor=RED)
        source.Look = [red] * 6
        # a look of the object's own, not the card's: what a paste carries
        # over the target's look (a following one only sets the card)
        source.Look.FollowMaterial = False
        following = Materials.packMaterial(source, "", "Look")
        self.assertIn(b"custom 1\n1\n", following)
        data = Materials.packMaterial(source, "ShapeMaterial", "Look")
        self.assertNotEqual(data, b"")

        target_doc = FreeCAD.newDocument("ClipboardTarget")
        self.docs.append(target_doc.Name)
        whole = self.boxWithLook(target_doc, "Whole")
        self.assertTrue(Materials.applyMaterial(whole, data, "ShapeMaterial", "Look"))
        for i in range(6):
            self.assertEqual(whole.Look[i].DiffuseColor, RED)

        faces = self.boxWithLook(target_doc, "Faces")
        self.assertTrue(Materials.applyMaterial(faces, data, "ShapeMaterial", "Look", [0, 2]))
        self.assertEqual(faces.Look[0].DiffuseColor, RED)
        self.assertEqual(faces.Look[2].DiffuseColor, RED)
        self.assertNotEqual(faces.Look[1].DiffuseColor, RED)
        self.assertNotEqual(faces.Look[5].DiffuseColor, RED)

    def testGarbageIsNotAPayload(self):
        doc = FreeCAD.newDocument("ClipboardTarget")
        self.docs.append(doc.Name)
        target = self.boxWithLook(doc, "Target")
        self.assertFalse(Materials.applyMaterial(target, b"not a payload", "ShapeMaterial", "Look"))
