# SPDX-License-Identifier: LGPL-2.1-or-later
# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD Project Association                        *
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
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************
"""What the tree view keeps of an object that has no row.

The tree has an entry for an object in each document it has rows under, in
the document item's own map and in a table of the whole tree that is looked
up by the object's address. A row that went away took the entry out of the
map and left it in the table. Nothing that takes entries out found it there
-- the close of a document goes by the map -- so it outlived the object, its
view provider and its document item, and was found again by whichever object
was made at that address in a later document: the tree then asked a view
provider long gone whether to show the row, and the application was gone
with it.

How a row goes away without its object: something claims the object, as a
shader binding claims its shader, and has no row of its own for it yet.

The tree also lists the objects that changed since its last pass, by
address, and nothing took an object off that list when it went away, or its
document: until the next pass the list named memory that was freed, and an
object made at that address in between was taken for a changed one.

FreeCADGui.treeTableStats() counts the entries, and the changes listed of
objects that are gone, so none of this needs a crash to be seen. Needs the
GUI: run it through scripts/sandbox-gui-gate.py, or in the running
application. Skips headless.
"""

import time
import unittest

import FreeCAD

App = FreeCAD


def settle(sec=0.4):
    """Let the tree catch up: it updates from a timer"""
    from PySide import QtCore

    until = time.time() + sec
    while time.time() < until:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.01)


class TreeEntriesCases(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        import FreeCADGui

        self.Gui = FreeCADGui
        if not hasattr(FreeCADGui, "treeTableStats"):
            self.skipTest("no treeTableStats() in this build")
        settle()
        self.before = FreeCADGui.treeTableStats()
        self.Doc = None

    def tearDown(self):
        for name in ("TreeEntries", "TreeEntriesNext"):
            if name in App.listDocuments():
                App.closeDocument(name)

    NOTHING = {"objects": 0, "entries": 0, "rowless": 0, "changedGone": 0}

    def scene(self, name="TreeEntries"):
        """A box, and a shader with its program, each with a row of its own"""
        self.Doc = doc = App.newDocument(name)
        box = doc.addObject("Part::Box", "Box")
        program = doc.addObject("App::ShaderProgram", "Program")
        shader = doc.addObject("App::Shader", "Shader")
        shader.Programs = [program]
        shader.Demo = "None"
        doc.recompute()
        settle()
        return doc, box, shader

    def bind(self, doc, box, shader):
        """A binding claims the shader and a link to the box"""
        link = doc.addObject("App::Link", "Target")
        link.LinkedObject = box
        binding = doc.addObject("App::ShaderBinding", "Binding")
        binding.Scope = "Object"
        binding.ElementList = [shader, link]
        doc.recompute()
        return binding, link

    def stats(self):
        now = self.Gui.treeTableStats()
        return {key: now[key] - self.before[key] for key in now}

    def testRowTakenAwayTakesItsEntry(self):
        doc, box, shader = self.scene()
        self.assertEqual(self.stats()["rowless"], 0)
        made = self.stats()["entries"]
        self.assertGreaterEqual(made, 2)
        for _ in range(4):
            binding, link = self.bind(doc, box, shader)
            settle()
            self.assertEqual(self.stats()["rowless"], 0)
            doc.removeObject(binding.Name)
            doc.removeObject(link.Name)
            doc.recompute()
            settle()
            now = self.stats()
            self.assertEqual(now["rowless"], 0)
            # nothing is added up either: what was claimed has its one row
            # and its one entry back
            self.assertEqual(now["entries"], made)
            self.assertEqual(now["objects"], made)

    def testClosedDocumentLeavesNothing(self):
        doc, box, shader = self.scene()
        for _ in range(3):
            binding, link = self.bind(doc, box, shader)
            settle()
            doc.removeObject(binding.Name)
            doc.removeObject(link.Name)
            doc.recompute()
            # closed below before the tree has caught up with the last turn
        binding, link = self.bind(doc, box, shader)
        settle()
        doc.removeObject(binding.Name)
        doc.removeObject(link.Name)
        doc.recompute()
        App.closeDocument(doc.Name)
        settle()
        self.assertEqual(self.stats(), self.NOTHING)

        # and the objects of the next document, made where those were, are
        # looked up without finding them
        doc, box, shader = self.scene("TreeEntriesNext")
        binding, link = self.bind(doc, box, shader)
        settle()
        for obj in doc.Objects:
            obj.ViewObject.ShowInTree = False
        settle()
        for obj in doc.Objects:
            obj.ViewObject.ShowInTree = True
        doc.recompute()
        settle()
        self.assertEqual(self.stats()["rowless"], 0)
        App.closeDocument(doc.Name)
        settle()
        self.assertEqual(self.stats(), self.NOTHING)

    def testChangeOfAnObjectTakenAwayIsNotKept(self):
        doc, box, shader = self.scene()
        box.touch()
        doc.removeObject(box.Name)
        # read before the tree's next pass, which empties the list
        self.assertEqual(self.Gui.treeTableStats()["changedGone"], 0)
        settle()
        self.assertEqual(self.Gui.treeTableStats()["changedGone"], 0)

    def testChangeInAClosedDocumentIsNotKept(self):
        doc, box, shader = self.scene()
        box.touch()
        shader.touch()
        App.closeDocument(doc.Name)
        self.assertEqual(self.Gui.treeTableStats()["changedGone"], 0)
        settle()
        self.assertEqual(self.Gui.treeTableStats()["changedGone"], 0)

