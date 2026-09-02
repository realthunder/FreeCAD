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
Test module for a card's shader graph: making a shader card, and editing
its graph on an object (docs/MaterialStorage.md 17.11 and 17.12).

A library save places the graph's files under the library's materialx/
directory and relinks the card to them, wherever the author picked them.
Materializing puts the card's bytes onto the object as three editable
shader objects; the object keeps wearing the card, and reverting removes
the three and nothing else.
"""

import os
import shutil
import tempfile
import unittest

import FreeCAD
import Materials

FOLDER = "ShaderGraphTest"
CARD = FOLDER + "/Checker.FCMat"
SHADER_GRAPH_RENDERING = "1b396dbe-b345-41e2-a42d-5b2934f50880"
GRAPH = (
    '<?xml version="1.0"?>\n'
    '<materialx version="1.39">\n'
    '  <nodegraph name="maps">\n'
    '    <image name="base" type="color3">\n'
    '      <input name="file" type="filename" value="checker.png" />\n'
    "    </image>\n"
    '    <output name="out" type="color3" nodename="base" />\n'
    "  </nodegraph>\n"
    "</materialx>\n"
)
PIXELS = b"not really a png, and nothing here decodes it"


class ShaderGraphTestCases(unittest.TestCase):
    """Saving a shader card, and materializing its graph on an object"""

    def setUp(self):
        self.MaterialManager = Materials.MaterialManager()
        self.docs = []
        self.tmp = tempfile.mkdtemp(prefix="fc_shader_graph_")
        self.cleanLibrary()

    def tearDown(self):
        for name in list(self.docs):
            if name in FreeCAD.listDocuments():
                FreeCAD.closeDocument(name)
        shutil.rmtree(self.tmp, ignore_errors=True)
        self.cleanLibrary()

    # -- fixtures ---------------------------------------------------------

    def userLibraryRoot(self):
        for name, directory, _icon in self.MaterialManager.MaterialLibraries:
            if name == "User":
                return directory
        self.fail("no writable User library")

    def cleanLibrary(self):
        """Leave the user's real library as it was found: the card's folder
        and the files placed for it, both under a name nothing else uses"""
        root = self.userLibraryRoot()
        removed = False
        for path in (os.path.join(root, FOLDER), os.path.join(root, "materialx", FOLDER)):
            if os.path.isdir(path):
                shutil.rmtree(path, ignore_errors=True)
                removed = True
        if removed:
            self.MaterialManager.refresh()

    def pickedFiles(self):
        """The graph and its image where an author might have them: not
        in any library, the image in a folder of its own"""
        picked = os.path.join(self.tmp, "picked")
        os.makedirs(picked, exist_ok=True)
        mtlx = os.path.join(picked, "checker.mtlx")
        png = os.path.join(picked, "checker.png")
        with open(mtlx, "w", encoding="utf-8") as f:
            f.write(GRAPH)
        with open(png, "wb") as f:
            f.write(PIXELS)
        return mtlx, png

    def libraryCard(self):
        """A shader card saved into the User library from picked files, read
        back off the library so it is hashed and resolved as any card is"""
        mtlx, png = self.pickedFiles()
        card = Materials.Material()
        card.Name = "Checker"
        card.addAppearanceModel(SHADER_GRAPH_RENDERING)
        card.setAppearanceValue("MaterialXShaderGraph", "checker.mtlx")
        card.setAppearanceValue("MaterialXNames", ["checker.mtlx", "checker.png"])
        card.setAppearanceValue("MaterialXFiles", [mtlx, png])
        self.MaterialManager.save("User", card, CARD, overwrite=True)
        self.MaterialManager.refresh()
        return self.MaterialManager.getMaterialByPath(CARD, "User")

    def boxWith(self, card, name="ShaderGraphDoc"):
        doc = FreeCAD.newDocument(name)
        self.docs.append(doc.Name)
        obj = doc.addObject("Part::Box", "Box")
        obj.ShapeMaterial = card
        doc.recompute()
        return doc, obj

    # -- the library save ---------------------------------------------------

    def testSavingACardPlacesItsFilesInTheLibrary(self):
        card = self.libraryCard()
        root = self.userLibraryRoot()
        placed = os.path.join(root, "materialx", FOLDER, "Checker")
        self.assertTrue(os.path.isfile(os.path.join(placed, "checker.mtlx")), placed)
        self.assertTrue(os.path.isfile(os.path.join(placed, "checker.png")), placed)
        with open(os.path.join(placed, "checker.png"), "rb") as f:
            self.assertEqual(f.read(), PIXELS)
        with open(os.path.join(root, CARD), encoding="utf-8") as f:
            yaml = f.read()
        self.assertIn(FOLDER + "/Checker/checker.png", yaml)
        self.assertNotIn("picked", yaml)
        # what the graph calls the image did not move
        self.assertIn('"checker.png"', yaml)
        self.assertEqual(card.Name, "Checker")

    # -- materializing --------------------------------------------------------

    def testMaterializeMakesThreeObjectsFromTheCardsBytes(self):
        doc, obj = self.boxWith(self.libraryCard())
        self.assertIsNone(Materials.shaderGraphBinding(obj))

        binding = Materials.materializeShaderGraph(obj, "ShapeMaterial")
        self.assertIsNotNone(binding)
        self.assertEqual(binding.TypeId, "App::ShaderBinding")
        self.assertEqual(binding.Scope, "Object")
        self.assertIn(obj, binding.ElementList)
        shader = binding.ElementList[0]
        self.assertEqual(shader.TypeId, "App::Shader")
        self.assertEqual(shader.Demo, "None")
        program = shader.Programs[0]
        self.assertEqual(program.TypeId, "App::ShaderProgram")
        self.assertEqual(program.Stage, "material")
        self.assertEqual(program.Dialect, "MATERIALX")
        self.assertEqual(program.FragmentProgram, GRAPH)
        # the image travels under the name the graph calls it by, and its
        # bytes are the card's
        self.assertEqual(list(program.Images.keys()), ["checker.png"])
        with open(program.Images["checker.png"], "rb") as f:
            self.assertEqual(f.read(), PIXELS)

        # the object still wears the card; the binding is what is drawn
        self.assertEqual(obj.ShapeMaterial.Name, "Checker")
        self.assertEqual(Materials.shaderGraphBinding(obj).Name, binding.Name)
        self.assertFalse(Materials.shaderGraphEdited(obj))
        # twice is once
        self.assertEqual(Materials.materializeShaderGraph(obj, "ShapeMaterial").Name, binding.Name)
        self.assertEqual(len([o for o in doc.Objects if o.TypeId == "App::ShaderProgram"]), 1)

    def testAnEditIsNoticedAndRevertRemovesTheThree(self):
        doc, obj = self.boxWith(self.libraryCard())
        binding = Materials.materializeShaderGraph(obj, "ShapeMaterial")
        shader = binding.ElementList[0]
        program = shader.Programs[0]
        names = [binding.Name, shader.Name, program.Name]

        program.FragmentProgram = GRAPH + "<!-- edited -->\n"
        self.assertTrue(Materials.shaderGraphEdited(obj))

        self.assertTrue(Materials.revertShaderGraph(obj))
        for name in names:
            self.assertIsNone(doc.getObject(name), name)
        self.assertIsNone(Materials.shaderGraphBinding(obj))
        self.assertFalse(Materials.revertShaderGraph(obj))
        # the card was never taken off
        self.assertEqual(obj.ShapeMaterial.Name, "Checker")
        self.assertIsNotNone(doc.getObject(obj.Name))

    def testACardWithoutAGraphMaterializesNothing(self):
        steel = self.MaterialManager.getMaterial("92589471-a6cb-4bbc-b748-d425a17dea7d")
        doc, obj = self.boxWith(steel)
        self.assertIsNone(Materials.materializeShaderGraph(obj, "ShapeMaterial"))
        self.assertIsNone(Materials.shaderGraphBinding(obj))
        self.assertFalse(Materials.shaderGraphEdited(obj))
        self.assertFalse(Materials.revertShaderGraph(obj))

    def testTheThreeSurviveASaveAndReopen(self):
        doc, obj = self.boxWith(self.libraryCard())
        binding = Materials.materializeShaderGraph(obj, "ShapeMaterial")
        program = binding.ElementList[0].Programs[0]
        path = os.path.join(self.tmp, "materialized.FCStd")
        doc.saveAs(path)
        docName, objName = doc.Name, obj.Name
        bindingName, programName = binding.Name, program.Name
        FreeCAD.closeDocument(docName)
        self.docs.remove(docName)

        doc = FreeCAD.openDocument(path)
        self.docs.append(doc.Name)
        obj = doc.getObject(objName)
        again = Materials.shaderGraphBinding(obj)
        self.assertIsNotNone(again)
        self.assertEqual(again.Name, bindingName)
        reopened = again.ElementList[0].Programs[0]
        self.assertEqual(reopened.Name, programName)
        self.assertEqual(reopened.FragmentProgram, GRAPH)
        self.assertEqual(list(reopened.Images.keys()), ["checker.png"])
        self.assertFalse(Materials.shaderGraphEdited(obj))
